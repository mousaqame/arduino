using System;
using System.Collections.Generic;
using Microsoft.Win32;

namespace WheelForge.Core
{
    // A development board sitting on a COM port.
    //
    // These never appear in the HID device list, and that is not a fault: an
    // Uno, a Nano, a classic ESP32 have no USB device hardware, so to Windows
    // they are a serial adapter and nothing else. Before this existed the app
    // simply said "no game controllers found" while an Arduino sat plugged in
    // two feet away, which is a miserable thing to do to someone.
    public class SerialBoard
    {
        public string PortName;
        public string FriendlyName;
        public ushort Vid;
        public ushort Pid;

        // The BoardCatalog id this hardware matches, or null if unrecognised.
        public string BoardId;

        public string VidPid
        {
            get { return Vid.ToString("X4") + ":" + Pid.ToString("X4"); }
        }

        public string DisplayName
        {
            get
            {
                string name = FriendlyName;
                if (string.IsNullOrEmpty(name))
                {
                    BoardProfile p = BoardId != null ? BoardCatalog.ById(BoardId) : null;
                    name = p != null ? p.Name : ("Serial device " + VidPid);
                }
                return name;
            }
        }
    }

    public static class SerialBoardScanner
    {
        // USB ids for the boards WheelForge builds for, plus the generic serial
        // bridge chips a clone might use. A CH340 or CP2102 could be anything,
        // so those map to no board and only say what they are.
        private class Match
        {
            public ushort Vid, Pid;
            public string BoardId;
            public string Label;

            public Match(ushort vid, ushort pid, string boardId, string label)
            {
                Vid = vid; Pid = pid; BoardId = boardId; Label = label;
            }
        }

        private static readonly Match[] Known = new Match[]
        {
            // Arduino, both the arduino.cc and arduino.org vendor ids
            new Match(0x2341, 0x0043, "uno", "Arduino Uno"),
            new Match(0x2341, 0x0001, "uno", "Arduino Uno"),
            new Match(0x2A03, 0x0043, "uno", "Arduino Uno"),
            new Match(0x2A03, 0x0001, "uno", "Arduino Uno"),
            new Match(0x2341, 0x0243, "uno", "Arduino Uno"),

            new Match(0x2341, 0x0042, "mega2560", "Arduino Mega 2560"),
            new Match(0x2A03, 0x0042, "mega2560", "Arduino Mega 2560"),
            new Match(0x2341, 0x0010, "mega2560", "Arduino Mega 2560"),

            new Match(0x2341, 0x8036, "leonardo", "Arduino Leonardo"),
            new Match(0x2341, 0x0036, "leonardo", "Arduino Leonardo (bootloader)"),
            new Match(0x2A03, 0x8036, "leonardo", "Arduino Leonardo"),
            new Match(0x2A03, 0x0036, "leonardo", "Arduino Leonardo (bootloader)"),

            new Match(0x2341, 0x8037, "micro", "Arduino Micro"),
            new Match(0x2341, 0x0037, "micro", "Arduino Micro (bootloader)"),
            new Match(0x1B4F, 0x9205, "micro", "SparkFun Pro Micro"),
            new Match(0x1B4F, 0x9206, "micro", "SparkFun Pro Micro"),
            new Match(0x1B4F, 0x9203, "micro", "SparkFun Pro Micro (bootloader)"),

            new Match(0x2341, 0x0069, null, "Arduino Uno R4 Minima"),
            new Match(0x2341, 0x1002, null, "Arduino Uno R4 WiFi"),

            // Serial bridge chips: the board behind them is anyone's guess.
            new Match(0x1A86, 0x7523, null, "CH340 serial (Nano or ESP clone)"),
            new Match(0x1A86, 0x5523, null, "CH341 serial"),
            new Match(0x1A86, 0x55D4, null, "CH9102 serial (ESP board)"),
            new Match(0x0403, 0x6001, "nano", "FT232 serial (Arduino Nano)"),
            new Match(0x10C4, 0xEA60, null, "CP2102 serial (ESP board)"),
        };

        private static Match Lookup(ushort vid, ushort pid)
        {
            foreach (Match m in Known)
                if (m.Vid == vid && m.Pid == pid) return m;
            return null;
        }

        // Espressif and Raspberry Pi use whole vendor ids rather than a handful
        // of product ids, so those are matched by vendor alone.
        private static Match LookupVendor(ushort vid)
        {
            if (vid == 0x303A) return new Match(vid, 0, "esp32s3", "Espressif ESP32-S2/S3");
            if (vid == 0x2E8A) return new Match(vid, 0, "pico", "Raspberry Pi RP2040");
            return null;
        }

        // Walks the USB enumeration in the registry, which is where Windows
        // records the COM port each device was given. This needs no admin
        // rights and no WMI.
        public static List<SerialBoard> Scan()
        {
            List<SerialBoard> found = new List<SerialBoard>();

            RegistryKey usb = null;
            try
            {
                usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB");
            }
            catch (Exception)
            {
                return found;
            }

            if (usb == null) return found;

            using (usb)
            {
                foreach (string deviceName in SafeSubKeys(usb))
                {
                    ushort vid, pid;
                    if (!ParseVidPid(deviceName, out vid, out pid)) continue;

                    using (RegistryKey device = SafeOpen(usb, deviceName))
                    {
                        if (device == null) continue;

                        foreach (string instanceName in SafeSubKeys(device))
                        {
                            using (RegistryKey instance = SafeOpen(device, instanceName))
                            {
                                if (instance == null) continue;

                                string port = PortOf(instance);
                                if (port == null) continue;

                                SerialBoard b = new SerialBoard();
                                b.PortName = port;
                                b.Vid = vid;
                                b.Pid = pid;
                                b.FriendlyName = instance.GetValue("FriendlyName") as string;

                                Match m = Lookup(vid, pid);
                                if (m == null) m = LookupVendor(vid);
                                if (m != null)
                                {
                                    b.BoardId = m.BoardId;
                                    if (string.IsNullOrEmpty(b.FriendlyName))
                                        b.FriendlyName = m.Label + " (" + port + ")";
                                }

                                found.Add(b);
                            }
                        }
                    }
                }
            }

            // Only report ports that are actually present. The registry keeps a
            // record of every device ever plugged in, so without this the list
            // fills up with boards that went home months ago.
            List<string> live = new List<string>(ToolLocator.SerialPorts());
            List<SerialBoard> present = new List<SerialBoard>();
            foreach (SerialBoard b in found)
                if (live.Contains(b.PortName)) present.Add(b);

            present.Sort(delegate(SerialBoard a, SerialBoard b)
            {
                return string.Compare(a.PortName, b.PortName, StringComparison.OrdinalIgnoreCase);
            });

            return present;
        }

        private static string PortOf(RegistryKey instance)
        {
            using (RegistryKey parameters = SafeOpen(instance, "Device Parameters"))
            {
                if (parameters == null) return null;
                string port = parameters.GetValue("PortName") as string;
                if (string.IsNullOrEmpty(port)) return null;
                if (!port.StartsWith("COM", StringComparison.OrdinalIgnoreCase)) return null;
                return port;
            }
        }

        private static bool ParseVidPid(string key, out ushort vid, out ushort pid)
        {
            vid = 0; pid = 0;
            if (key == null) return false;

            int v = key.IndexOf("VID_", StringComparison.OrdinalIgnoreCase);
            int p = key.IndexOf("PID_", StringComparison.OrdinalIgnoreCase);
            if (v < 0 || p < 0) return false;
            if (key.Length < v + 8 || key.Length < p + 8) return false;

            try
            {
                vid = Convert.ToUInt16(key.Substring(v + 4, 4), 16);
                pid = Convert.ToUInt16(key.Substring(p + 4, 4), 16);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static string[] SafeSubKeys(RegistryKey key)
        {
            try { return key.GetSubKeyNames(); }
            catch (Exception) { return new string[0]; }
        }

        private static RegistryKey SafeOpen(RegistryKey parent, string name)
        {
            // Some enumeration subkeys are readable only by SYSTEM. Skipping
            // them quietly is correct: nothing WheelForge cares about lives there.
            try { return parent.OpenSubKey(name); }
            catch (Exception) { return null; }
        }
    }
}
