using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Text;
using System.Threading;

namespace WheelForge.Core
{
    // One decoded input report from a bridge-mode board.
    public class WheelInput
    {
        public byte[] Buttons = new byte[3];
        public short Steering;
        public ushort Throttle;
        public ushort Brake;
        public ushort Clutch;

        public bool IsPressed(int zeroBased)
        {
            if (zeroBased < 0 || zeroBased >= 24) return false;
            return (Buttons[zeroBased >> 3] & (1 << (zeroBased & 7))) != 0;
        }

        public List<int> PressedButtons()
        {
            List<int> list = new List<int>();
            for (int i = 0; i < 24; i++)
                if (IsPressed(i)) list.Add(i);
            return list;
        }
    }

    // The serial link to a WheelForge board.
    //
    // Carries two things: text commands and replies (settings, force feedback
    // tests, status), and — on boards with no USB device hardware — the input
    // reports themselves.
    //
    // The board drops any test effect it has not heard about for three seconds.
    // That watchdog is the whole safety story for force feedback, so the effect
    // keepalive lives here rather than in a page that might be closed, hidden
    // or busy repainting: if this link dies, the motor stops on its own.
    public class WheelLink : IDisposable
    {
        private SerialPort _port;
        private Thread _reader;
        private volatile bool _stopping;

        private readonly object _effectLock = new object();
        private string _effectCommand;
        private Timer _keepalive;

        public event Action<string> Line;
        public event Action<WheelInput> Input;
        public event Action<string> Closed;

        public string PortName { get; private set; }

        public bool IsOpen
        {
            get { return _port != null && _port.IsOpen; }
        }

        // Asks a board on this port what it is, and returns its settings, or
        // null if nothing running WheelForge firmware answered.
        //
        // This is how the app tells a board it has already flashed from one it
        // has not, rather than labelling everything "needs setting up" and
        // hoping. An Uno reboots when the port opens, so the wait is not
        // optional -- it is how long the bootloader takes to hand over.
        public static Dictionary<string, string> Probe(string port, int timeoutMs)
        {
            WheelLink link = new WheelLink();
            Dictionary<string, string> settings = new Dictionary<string, string>();
            bool sawEnd = false;

            link.Line += delegate(string line)
            {
                if (line.StartsWith("--- end")) { sawEnd = true; return; }

                int eq = line.IndexOf('=');
                if (eq <= 0) return;
                lock (settings)
                    settings[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
            };

            if (link.Open(port) != null) return null;

            try
            {
                DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);

                // The board is still booting for the first couple of seconds,
                // so keep asking rather than asking once and giving up.
                while (DateTime.UtcNow < deadline && !sawEnd)
                {
                    link.Send("?");
                    Thread.Sleep(400);
                }
            }
            finally
            {
                link.Close();
            }

            lock (settings)
            {
                if (settings.ContainsKey("version") && settings.ContainsKey("board"))
                    return new Dictionary<string, string>(settings);
            }
            return null;
        }

        // Returns null on success, or a readable reason.
        public string Open(string portName)
        {
            Close();

            if (string.IsNullOrEmpty(portName)) return "No port selected.";

            try
            {
                _port = new SerialPort(portName, 115200);
                _port.ReadTimeout = 250;
                _port.WriteTimeout = 1000;
                _port.DtrEnable = true;      // a 32u4 sends nothing without this
                _port.RtsEnable = true;
                _port.Open();
            }
            catch (UnauthorizedAccessException)
            {
                _port = null;
                return portName + " is already open in another program.";
            }
            catch (Exception ex)
            {
                _port = null;
                return "Could not open " + portName + ": " + ex.Message;
            }

            PortName = portName;
            _stopping = false;

            _reader = new Thread(ReadLoop);
            _reader.IsBackground = true;
            _reader.Name = "WheelForge serial reader";
            _reader.Start();

            return null;
        }

        public void Send(string command)
        {
            if (!IsOpen) return;
            try
            {
                _port.Write(command + "\n");
            }
            catch (Exception ex)
            {
                Raise(Closed, "Write failed: " + ex.Message);
            }
        }

        // ---------------------------------------------------------------- effects

        // Starts an effect and keeps it alive. The board expires it by itself if
        // these stop arriving.
        public void StartEffect(string command)
        {
            lock (_effectLock)
            {
                _effectCommand = command;
                if (_keepalive == null)
                    _keepalive = new Timer(KeepaliveTick, null, 0, 1000);
                else
                    _keepalive.Change(0, 1000);
            }
        }

        public void StopEffect()
        {
            lock (_effectLock)
            {
                _effectCommand = null;
                if (_keepalive != null) _keepalive.Change(Timeout.Infinite, Timeout.Infinite);
            }
            Send("STOP");
        }

        private void KeepaliveTick(object state)
        {
            string cmd;
            lock (_effectLock) { cmd = _effectCommand; }
            if (cmd != null && IsOpen) Send(cmd);
        }

        // ---------------------------------------------------------------- reading

        // A5 5A <type> <len> <payload> <xor>
        //   type 1 = binary input report
        //   type 2 = text line
        private void ReadLoop()
        {
            byte[] chunk = new byte[512];
            List<byte> buffer = new List<byte>(1024);
            string reason = null;

            while (!_stopping)
            {
                int read;
                try
                {
                    read = _port.Read(chunk, 0, chunk.Length);
                }
                catch (TimeoutException)
                {
                    continue;
                }
                catch (Exception ex)
                {
                    if (!_stopping) reason = "Serial read failed: " + ex.Message;
                    break;
                }

                for (int i = 0; i < read; i++) buffer.Add(chunk[i]);

                int consumed = ParseFrames(buffer);
                if (consumed > 0) buffer.RemoveRange(0, consumed);

                // A stream that never syncs would otherwise grow without bound.
                if (buffer.Count > 4096) buffer.RemoveRange(0, buffer.Count - 1024);
            }

            if (reason != null) Raise(Closed, reason);
        }

        private int ParseFrames(List<byte> buf)
        {
            int pos = 0;

            while (true)
            {
                // Find the header.
                while (pos + 1 < buf.Count && !(buf[pos] == 0xA5 && buf[pos + 1] == 0x5A))
                    pos++;

                if (pos + 5 > buf.Count) return pos;      // need header + type + len + xor

                byte type = buf[pos + 2];
                byte len = buf[pos + 3];

                int total = 4 + len + 1;
                if (pos + total > buf.Count) return pos;  // wait for the rest

                byte xorsum = (byte)(type ^ len);
                for (int i = 0; i < len; i++) xorsum ^= buf[pos + 4 + i];

                if (xorsum != buf[pos + 4 + len])
                {
                    // Corrupt frame. Step past this header and resynchronise
                    // rather than trusting the length byte.
                    pos += 2;
                    continue;
                }

                byte[] payload = new byte[len];
                buf.CopyTo(pos + 4, payload, 0, len);
                Dispatch(type, payload);

                pos += total;
            }
        }

        private void Dispatch(byte type, byte[] payload)
        {
            if (type == 2)
            {
                Raise(Line, Encoding.ASCII.GetString(payload).TrimEnd('\0'));
            }
            else if (type == 1 && payload.Length >= 11)
            {
                WheelInput input = new WheelInput();
                input.Buttons[0] = payload[0];
                input.Buttons[1] = payload[1];
                input.Buttons[2] = payload[2];
                input.Steering = (short)(payload[3] | (payload[4] << 8));
                input.Throttle = (ushort)(payload[5] | (payload[6] << 8));
                input.Brake = (ushort)(payload[7] | (payload[8] << 8));
                input.Clutch = (ushort)(payload[9] | (payload[10] << 8));

                Action<WheelInput> h = Input;
                if (h != null) h(input);
            }
        }

        private static void Raise(Action<string> handler, string text)
        {
            if (handler != null) handler(text);
        }

        // ---------------------------------------------------------------- teardown

        public void Close()
        {
            lock (_effectLock)
            {
                _effectCommand = null;
                if (_keepalive != null)
                {
                    _keepalive.Dispose();
                    _keepalive = null;
                }
            }

            // Best effort: tell the board to drop any effect before the link
            // goes. If this does not get through, the board's own three second
            // watchdog does the same job.
            if (IsOpen)
            {
                try { _port.Write("STOP\n"); }
                catch (Exception) { }
            }

            _stopping = true;

            if (_reader != null)
            {
                _reader.Join(1500);
                _reader = null;
            }

            if (_port != null)
            {
                try { if (_port.IsOpen) _port.Close(); }
                catch (Exception) { }
                _port.Dispose();
                _port = null;
            }
        }

        public void Dispose()
        {
            Close();
        }
    }
}
