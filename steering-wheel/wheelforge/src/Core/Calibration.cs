using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace WheelForge.Core
{
    // Calibration for one axis: where its travel really starts and ends, where
    // centre is, how much of each end to ignore, and how hard the response
    // curve bends.
    //
    // All of it is computed on the PC from values the device already reports.
    // Nothing here needs a firmware change, which is why it comes before
    // anything that writes to a board.
    public class AxisCalibration
    {
        public ushort UsagePage;
        public ushort Usage;
        public string Label = "";

        // Travel actually observed, rather than what the descriptor claims. A
        // pot that only swings a third of its range still gives full throw once
        // these are captured.
        public int Min;
        public int Max;
        public int Centre;

        // Centred axes (steering) run -1..+1 about Centre. Everything else
        // (pedals) runs 0..1 from Min.
        public bool Centred;

        public bool Invert;

        public double DeadzoneCentre;   // 0..0.5, ignored unless Centred
        public double DeadzoneEnds;     // 0..0.5, trimmed off each end
        public double Gamma = 1.0;      // 1 is linear, >1 softer near centre

        public AxisCalibration Clone()
        {
            AxisCalibration c = new AxisCalibration();
            c.UsagePage = UsagePage; c.Usage = Usage; c.Label = Label;
            c.Min = Min; c.Max = Max; c.Centre = Centre;
            c.Centred = Centred; c.Invert = Invert;
            c.DeadzoneCentre = DeadzoneCentre; c.DeadzoneEnds = DeadzoneEnds;
            c.Gamma = Gamma;
            return c;
        }

        public bool HasTravel
        {
            get { return Max > Min; }
        }

        // Raw device value in, 0..1 out, with 0.5 meaning centred for a centred
        // axis. Same shape as the normalised value the monitor shows, so the two
        // can be drawn on the same kind of bar.
        public double Apply(int raw)
        {
            if (!HasTravel) return Centred ? 0.5 : 0.0;

            double t = (raw - (double)Min) / (Max - Min);
            t = Clamp01(t);
            if (Invert) t = 1.0 - t;

            double dzEnd = Clamp(DeadzoneEnds, 0.0, 0.45);

            if (!Centred)
            {
                double span = 1.0 - dzEnd * 2.0;
                double v = span <= 0.0 ? 0.0 : (t - dzEnd) / span;
                v = Clamp01(v);
                if (Math.Abs(Gamma - 1.0) > 0.001) v = Math.Pow(v, Gamma);
                return Clamp01(v);
            }

            // Where centre sits inside the captured travel.
            double c = (Centre - (double)Min) / (Max - Min);
            c = Clamp01(c);
            if (Invert) c = 1.0 - c;

            double signed;
            if (t >= c) signed = (c >= 1.0) ? 0.0 : (t - c) / (1.0 - c);
            else signed = (c <= 0.0) ? 0.0 : -((c - t) / c);

            double sign = signed < 0 ? -1.0 : 1.0;
            double mag = Math.Abs(signed);

            double dzC = Clamp(DeadzoneCentre, 0.0, 0.9);
            if (mag <= dzC) mag = 0.0;
            else mag = (mag - dzC) / (1.0 - dzC);

            if (dzEnd > 0.0) mag = Clamp01(mag / (1.0 - dzEnd));
            if (Math.Abs(Gamma - 1.0) > 0.001) mag = Math.Pow(Clamp01(mag), Gamma);

            return Clamp01((sign * Clamp01(mag) + 1.0) / 2.0);
        }

        private static double Clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
        private static double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

        public string Key
        {
            get { return UsagePage.ToString("X2") + ":" + Usage.ToString("X2"); }
        }
    }

    // A named set of axis calibrations, tied to the device it was captured on.
    public class CalibrationProfile
    {
        public string Name = "Default";
        public string DeviceVidPid = "";
        public List<AxisCalibration> Axes = new List<AxisCalibration>();

        public AxisCalibration Find(ushort page, ushort usage)
        {
            foreach (AxisCalibration a in Axes)
                if (a.UsagePage == page && a.Usage == usage) return a;
            return null;
        }

        // ------------------------------------------------------------------
        // Stored as flat key=value text rather than JSON. It is a handful of
        // numbers per axis, it wants to be readable and hand-editable, and a
        // format this small cannot go wrong the way a hand-rolled JSON parser
        // can.

        public static string ProfileDirectory
        {
            get
            {
                string dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "WheelForge", "profiles");
                return dir;
            }
        }

        private static string SafeFileName(string name)
        {
            StringBuilder sb = new StringBuilder();
            foreach (char ch in name)
                sb.Append(Array.IndexOf(Path.GetInvalidFileNameChars(), ch) >= 0 ? '_' : ch);
            string s = sb.ToString().Trim();
            return s.Length == 0 ? "profile" : s;
        }

        public string Save()
        {
            Directory.CreateDirectory(ProfileDirectory);
            string path = Path.Combine(ProfileDirectory, SafeFileName(Name) + ".wfprofile");

            CultureInfo ic = CultureInfo.InvariantCulture;
            StringBuilder sb = new StringBuilder();
            sb.AppendLine("# WheelForge calibration profile");
            sb.AppendLine("name=" + Name);
            sb.AppendLine("device=" + DeviceVidPid);

            foreach (AxisCalibration a in Axes)
            {
                sb.AppendLine();
                sb.AppendLine("[axis " + a.Key + "]");
                sb.AppendLine("label=" + a.Label);
                sb.AppendLine("min=" + a.Min.ToString(ic));
                sb.AppendLine("max=" + a.Max.ToString(ic));
                sb.AppendLine("centre=" + a.Centre.ToString(ic));
                sb.AppendLine("centred=" + (a.Centred ? "1" : "0"));
                sb.AppendLine("invert=" + (a.Invert ? "1" : "0"));
                sb.AppendLine("deadzone_centre=" + a.DeadzoneCentre.ToString("0.####", ic));
                sb.AppendLine("deadzone_ends=" + a.DeadzoneEnds.ToString("0.####", ic));
                sb.AppendLine("gamma=" + a.Gamma.ToString("0.####", ic));
            }

            File.WriteAllText(path, sb.ToString());
            return path;
        }

        public static CalibrationProfile Load(string path)
        {
            CultureInfo ic = CultureInfo.InvariantCulture;
            CalibrationProfile p = new CalibrationProfile();
            AxisCalibration current = null;

            foreach (string rawLine in File.ReadAllLines(path))
            {
                string line = rawLine.Trim();
                if (line.Length == 0 || line.StartsWith("#")) continue;

                if (line.StartsWith("[axis ") && line.EndsWith("]"))
                {
                    string key = line.Substring(6, line.Length - 7).Trim();
                    string[] bits = key.Split(':');
                    if (bits.Length != 2) { current = null; continue; }

                    current = new AxisCalibration();
                    current.UsagePage = Convert.ToUInt16(bits[0], 16);
                    current.Usage = Convert.ToUInt16(bits[1], 16);
                    p.Axes.Add(current);
                    continue;
                }

                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string k = line.Substring(0, eq).Trim();
                string v = line.Substring(eq + 1).Trim();

                if (current == null)
                {
                    if (k == "name") p.Name = v;
                    else if (k == "device") p.DeviceVidPid = v;
                    continue;
                }

                switch (k)
                {
                    case "label": current.Label = v; break;
                    case "min": current.Min = ParseInt(v); break;
                    case "max": current.Max = ParseInt(v); break;
                    case "centre": current.Centre = ParseInt(v); break;
                    case "centred": current.Centred = (v == "1"); break;
                    case "invert": current.Invert = (v == "1"); break;
                    case "deadzone_centre": current.DeadzoneCentre = ParseDouble(v); break;
                    case "deadzone_ends": current.DeadzoneEnds = ParseDouble(v); break;
                    case "gamma": current.Gamma = ParseDouble(v); break;
                }
            }

            if (string.IsNullOrEmpty(p.Name))
                p.Name = Path.GetFileNameWithoutExtension(path);

            return p;
        }

        private static int ParseInt(string s)
        {
            int v;
            return int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out v) ? v : 0;
        }

        private static double ParseDouble(string s)
        {
            double v;
            return double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out v) ? v : 0.0;
        }

        public static List<string> ListProfiles()
        {
            List<string> names = new List<string>();
            if (!Directory.Exists(ProfileDirectory)) return names;

            foreach (string f in Directory.GetFiles(ProfileDirectory, "*.wfprofile"))
                names.Add(Path.GetFileNameWithoutExtension(f));

            names.Sort();
            return names;
        }

        public static string PathFor(string name)
        {
            return Path.Combine(ProfileDirectory, SafeFileName(name) + ".wfprofile");
        }
    }
}
