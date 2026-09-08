using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;

namespace WheelForge.Core
{
    // Finds the flashing tools already on the machine and drives them.
    //
    // WheelForge does not ship avrdude or esptool. Both arrive with tools the
    // user almost certainly already has -- the Arduino IDE, arduino-cli, or
    // pip -- and shipping private copies would mean shipping a second set to
    // keep up to date and signed.
    public static class ToolLocator
    {
        private static string LocalAppData
        {
            get { return Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); }
        }

        private static string ProgramFilesX86
        {
            get
            {
                string p = Environment.GetEnvironmentVariable("ProgramFiles(x86)");
                if (!string.IsNullOrEmpty(p)) return p;
                return Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            }
        }

        // Newest avrdude wins, so an up to date arduino-cli install is
        // preferred over an ageing IDE one.
        public static string FindAvrdude()
        {
            List<string> found = new List<string>();

            string cliTools = Path.Combine(LocalAppData, @"Arduino15\packages\arduino\tools\avrdude");
            if (Directory.Exists(cliTools))
            {
                foreach (string version in Directory.GetDirectories(cliTools))
                {
                    string exe = Path.Combine(version, @"bin\avrdude.exe");
                    if (File.Exists(exe)) found.Add(exe);
                }
            }

            string ide = Path.Combine(ProgramFilesX86, @"Arduino\hardware\tools\avr\bin\avrdude.exe");
            if (File.Exists(ide)) found.Add(ide);

            if (found.Count == 0) return null;
            found.Sort(StringComparer.OrdinalIgnoreCase);
            return found[found.Count - 1];
        }

        // avrdude will not run without its config, and the copy that sits
        // beside the binary is the one that matches it.
        public static string FindAvrdudeConf(string avrdudeExe)
        {
            if (string.IsNullOrEmpty(avrdudeExe)) return null;

            string bin = Path.GetDirectoryName(avrdudeExe);
            string root = Path.GetDirectoryName(bin);

            string[] candidates = new string[]
            {
                Path.Combine(root, @"etc\avrdude.conf"),
                Path.Combine(bin, "avrdude.conf"),
                Path.Combine(root, "avrdude.conf")
            };

            foreach (string c in candidates)
                if (File.Exists(c)) return c;

            return null;
        }

        public static string FindEsptool()
        {
            string shim = Path.Combine(LocalAppData, @"Programs\Python\Python314\Scripts\esptool.exe");
            if (File.Exists(shim)) return shim;

            string scripts = Path.Combine(LocalAppData, @"Programs\Python");
            if (Directory.Exists(scripts))
            {
                foreach (string py in Directory.GetDirectories(scripts))
                {
                    string exe = Path.Combine(py, @"Scripts\esptool.exe");
                    if (File.Exists(exe)) return exe;
                }
            }

            return null;
        }

        // An RP2040 in bootloader mode mounts as a removable drive holding
        // INFO_UF2.TXT. That file is the only reliable way to tell it apart
        // from a USB stick.
        public static string FindUf2Drive()
        {
            try
            {
                foreach (DriveInfo d in DriveInfo.GetDrives())
                {
                    if (!d.IsReady) continue;
                    if (d.DriveType != DriveType.Removable) continue;
                    if (File.Exists(Path.Combine(d.RootDirectory.FullName, "INFO_UF2.TXT")))
                        return d.RootDirectory.FullName;
                }
            }
            catch (IOException)
            {
                // A drive disappeared mid-enumeration. Nothing to report.
            }
            return null;
        }

        public static string[] SerialPorts()
        {
            try
            {
                string[] ports = SerialPort.GetPortNames();
                Array.Sort(ports, new PortComparer());
                return ports;
            }
            catch (Exception)
            {
                return new string[0];
            }
        }

        // COM10 must sort after COM9, which a plain string sort gets wrong.
        private class PortComparer : IComparer<string>
        {
            public int Compare(string a, string b)
            {
                int na = Number(a), nb = Number(b);
                if (na >= 0 && nb >= 0) return na.CompareTo(nb);
                return string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
            }

            private static int Number(string port)
            {
                if (port == null || !port.StartsWith("COM", StringComparison.OrdinalIgnoreCase)) return -1;
                int n;
                return int.TryParse(port.Substring(3), out n) ? n : -1;
            }
        }
    }

    public class FlashResult
    {
        public bool Success;
        public string Message;
    }

    public class Flasher
    {
        private readonly Action<string> _log;

        public Flasher(Action<string> log)
        {
            _log = log;
        }

        private void Log(string line)
        {
            if (_log != null) _log(line);
        }

        // Runs on a background thread. Everything it reports comes back through
        // the log callback.
        public FlashResult Flash(BoardProfile board, string port, string imagePath)
        {
            FlashResult result = new FlashResult();

            if (board == null) return Fail(result, "No board selected.");
            if (!board.IsSupported)
                return Fail(result, board.Name + " cannot be a USB game controller, so there is nothing to flash to it.");
            if (string.IsNullOrEmpty(imagePath) || !File.Exists(imagePath))
                return Fail(result, "Firmware image not found: " + imagePath);

            switch (board.Tool)
            {
                case FlashTool.Uf2Copy: return FlashUf2(result, imagePath);
                case FlashTool.AvrdudeAvr109: return FlashAvr(result, board, port, imagePath, true);
                case FlashTool.Avrdude: return FlashAvr(result, board, port, imagePath, false);
                case FlashTool.Esptool: return FlashEsp(result, board, port, imagePath);
                default:
                    return Fail(result, "No flashing method is defined for this board.");
            }
        }

        private FlashResult Fail(FlashResult r, string message)
        {
            r.Success = false;
            r.Message = message;
            Log("ERROR: " + message);
            return r;
        }

        // Windows keeps listing a COM port for a moment after its device has
        // gone, so a board that was unplugged, reset onto a different port, or
        // simply lost its cable still shows up in the picker. Handing that port
        // to avrdude produces "cannot open port ... The system cannot find the
        // file specified", which says nothing useful about what to do next.
        //
        // Opening it here first turns that into an answer. DtrEnable stays off
        // so this check does not reset the board on its way past.
        private string CheckPort(string port)
        {
            try
            {
                using (SerialPort probe = new SerialPort(port, 9600))
                {
                    probe.DtrEnable = false;
                    probe.RtsEnable = false;
                    probe.Open();
                }
                return null;
            }
            catch (UnauthorizedAccessException)
            {
                return port + " is already open in another program. Close whatever has it -- "
                     + "the Arduino IDE's serial monitor, a terminal, or WheelForge's own "
                     + "Wheel test page -- and try again.";
            }
            catch (IOException)
            {
                return port + " is listed by Windows but will not open. The board is no longer "
                     + "there: either it was unplugged, or it reset and came back on a different "
                     + "port. Unplug it, plug it back in, press Refresh, and pick the port again.";
            }
            catch (Exception ex)
            {
                return "Could not open " + port + ": " + ex.Message;
            }
        }

        // ------------------------------------------------------------------ AVR

        private FlashResult FlashAvr(FlashResult result, BoardProfile board,
                                     string port, string imagePath, bool avr109)
        {
            string avrdude = ToolLocator.FindAvrdude();
            if (avrdude == null)
                return Fail(result, "avrdude not found. Install the Arduino IDE or arduino-cli.");

            string conf = ToolLocator.FindAvrdudeConf(avrdude);
            if (conf == null)
                return Fail(result, "Found avrdude but not its avrdude.conf next to it.");

            if (string.IsNullOrEmpty(port))
                return Fail(result, "Pick the COM port the board is on.");

            string portProblem = CheckPort(port);
            if (portProblem != null) return Fail(result, portProblem);

            Log("avrdude : " + avrdude);
            Log("config  : " + conf);
            Log("");

            string targetPort = port;

            if (avr109)
            {
                // A 32u4 running a sketch is not a programmer. Opening its port
                // at 1200 baud and dropping DTR makes it reset into the
                // bootloader, which enumerates as a *different* COM port for a
                // few seconds. That new port is the one to flash.
                Log("Resetting into the bootloader (1200 baud touch on " + port + ")...");
                string bootPort = TouchForBootloader(port);
                if (bootPort == null)
                    return Fail(result, "The board did not come back as a bootloader port. "
                                      + "On a Pro Micro the window is short -- try tapping reset "
                                      + "twice and flashing straight away.");

                Log("Bootloader is on " + bootPort);
                targetPort = bootPort;
            }

            string args = "-C\"" + conf + "\""
                        + " -v"
                        + " -p" + board.AvrdudePart
                        + " -c" + board.AvrdudeProgrammer
                        + " -P" + targetPort
                        + " -b" + board.UploadBaud
                        + " -D"
                        + " -Uflash:w:\"" + imagePath + "\":i";

            return RunTool(result, avrdude, args, "avrdude");
        }

        // Returns the bootloader port, or null if one never appeared.
        private string TouchForBootloader(string port)
        {
            List<string> before = new List<string>(ToolLocator.SerialPorts());

            try
            {
                using (SerialPort sp = new SerialPort(port, 1200))
                {
                    sp.Open();
                    sp.DtrEnable = false;
                    sp.RtsEnable = false;
                    Thread.Sleep(80);
                }
            }
            catch (Exception ex)
            {
                Log("  the 1200 baud touch failed (" + ex.Message + ") -- carrying on anyway");
            }

            // The board drops off the bus, then the bootloader enumerates. Give
            // it a moment before looking, or the old port is still listed.
            Thread.Sleep(600);

            for (int attempt = 0; attempt < 40; attempt++)   // up to ~10s
            {
                string[] now = ToolLocator.SerialPorts();

                foreach (string p in now)
                    if (!before.Contains(p)) return p;

                // Some boards come back on the same port number.
                if (attempt > 12)
                {
                    foreach (string p in now)
                        if (p == port) return p;
                }

                Thread.Sleep(250);
            }

            return null;
        }

        // ------------------------------------------------------------------ ESP

        private FlashResult FlashEsp(FlashResult result, BoardProfile board,
                                     string port, string imagePath)
        {
            string esptool = ToolLocator.FindEsptool();
            if (esptool == null)
                return Fail(result, "esptool not found. Install it with:  pip install esptool");

            if (string.IsNullOrEmpty(port))
                return Fail(result, "Pick the COM port the board is on.");

            string portProblem = CheckPort(port);
            if (portProblem != null) return Fail(result, portProblem);

            Log("esptool : " + esptool);

            // An ESP32 will not boot from the app image alone. It needs the
            // second stage bootloader and the partition table too, and the
            // firmware build leaves both beside the image. Without them the
            // chip flashes cleanly and then boot-loops, which looks like a
            // broken build rather than a missing file.
            string dir = Path.GetDirectoryName(imagePath);
            string bootloader = Path.Combine(dir, board.Id + ".bootloader.bin");
            string partitions = Path.Combine(dir, board.Id + ".partitions.bin");

            StringBuilder files = new StringBuilder();

            if (board.BootloaderOffset >= 0 && File.Exists(bootloader) && File.Exists(partitions))
            {
                files.Append("0x" + board.BootloaderOffset.ToString("X") + " \"" + bootloader + "\" ");
                files.Append("0x8000 \"" + partitions + "\" ");
                Log("bootloader + partition table found, flashing all three images");
            }
            else if (board.BootloaderOffset >= 0)
            {
                Log("WARNING: no bootloader/partition images beside " + Path.GetFileName(imagePath) + ".");
                Log("Flashing the app alone only works if this chip was already set up.");
            }

            files.Append("0x" + board.FlashOffset.ToString("X") + " \"" + imagePath + "\"");
            Log("");

            string chip = string.IsNullOrEmpty(board.EsptoolChip) ? "auto" : board.EsptoolChip;
            string args = "--chip " + chip + " --port " + port + " --baud 460800"
                        + " write-flash -z " + files.ToString();

            return RunTool(result, esptool, args, "esptool");
        }

        // ------------------------------------------------------------------ UF2

        private FlashResult FlashUf2(FlashResult result, string imagePath)
        {
            string drive = ToolLocator.FindUf2Drive();
            if (drive == null)
                return Fail(result, "No board in UF2 bootloader mode found. Hold BOOTSEL while "
                                  + "plugging the board in -- it should appear as a drive.");

            Log("Bootloader drive: " + drive);

            try
            {
                string dest = Path.Combine(drive, Path.GetFileName(imagePath));
                File.Copy(imagePath, dest, true);
            }
            catch (IOException ex)
            {
                // The board reboots the instant the file lands, so the copy
                // often reports an error even though it worked. Only a failure
                // before any bytes moved is a real failure.
                Log("Copy reported: " + ex.Message);
                Log("That is normal -- an RP2040 reboots as soon as the image is written.");
                result.Success = true;
                result.Message = "Image copied. The board should have rebooted into it.";
                return result;
            }
            catch (Exception ex)
            {
                return Fail(result, "Could not copy the image: " + ex.Message);
            }

            result.Success = true;
            result.Message = "Image copied to " + drive + ". The board reboots into it automatically.";
            Log(result.Message);
            return result;
        }

        // ------------------------------------------------------------------ runner

        private FlashResult RunTool(FlashResult result, string exe, string args, string label)
        {
            Log("> " + Path.GetFileName(exe) + " " + args);
            Log("");

            try
            {
                ProcessStartInfo psi = new ProcessStartInfo(exe, args);
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                psi.StandardOutputEncoding = Encoding.UTF8;
                psi.StandardErrorEncoding = Encoding.UTF8;

                using (Process p = new Process())
                {
                    p.StartInfo = psi;

                    // avrdude writes almost everything to stderr, including its
                    // progress bars, so both streams have to be read -- and read
                    // concurrently, or a full pipe buffer deadlocks the child.
                    p.OutputDataReceived += delegate(object s, DataReceivedEventArgs e)
                    {
                        if (e.Data != null) Log(e.Data);
                    };
                    p.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e)
                    {
                        if (e.Data != null) Log(e.Data);
                    };

                    p.Start();
                    p.BeginOutputReadLine();
                    p.BeginErrorReadLine();

                    if (!p.WaitForExit(120000))
                    {
                        try { p.Kill(); } catch (Exception) { }
                        return Fail(result, label + " did not finish within two minutes.");
                    }

                    // WaitForExit(int) can return before the async readers have
                    // drained. The parameterless call flushes them.
                    p.WaitForExit();

                    if (p.ExitCode != 0)
                        return Fail(result, label + " exited with code " + p.ExitCode + ".");
                }
            }
            catch (Exception ex)
            {
                return Fail(result, "Could not run " + label + ": " + ex.Message);
            }

            result.Success = true;
            result.Message = "Flashed successfully.";
            Log("");
            Log(result.Message);
            return result;
        }
    }
}
