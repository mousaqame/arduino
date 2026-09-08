using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace WheelForge.Hid
{
    // Streams input reports from one HID device on a background thread.
    //
    // Opened GENERIC_READ with both share flags, so a game can keep the wheel
    // at the same time -- you can tune in WheelForge while the sim is running.
    //
    // Shutdown is deliberate: CancelIoEx first, let the blocked ReadFile return,
    // join the thread, and only then CloseHandle. Closing a handle out from
    // under a blocked read tears down Windows overlapped IO mid-read, which is
    // what once made a board disappear until it was physically replugged.
    public class HidReader : IDisposable
    {
        private IntPtr _handle = HidNative.INVALID_HANDLE_VALUE;
        private IntPtr _preparsed = IntPtr.Zero;
        private Thread _thread;
        private volatile bool _stopping;
        private int _reportLength;

        private readonly Stopwatch _clock = new Stopwatch();
        private long _reportsSeen;

        public HidDeviceInfo Device { get; private set; }
        public HidReportLayout Layout { get; private set; }

        // Raised on the reader thread. Marshal to the UI before touching controls.
        public event Action<HidState> StateChanged;
        public event Action<string> Stopped;

        public bool IsRunning
        {
            get { return _thread != null && _thread.IsAlive; }
        }

        public double ReportsPerSecond
        {
            get
            {
                double seconds = _clock.Elapsed.TotalSeconds;
                if (seconds < 0.25) return 0.0;
                return _reportsSeen / seconds;
            }
        }

        // Returns null on success, or a human-readable reason on failure.
        public string Open(HidDeviceInfo device)
        {
            Close();

            if (device == null) return "No device selected.";
            if (device.InputReportLength <= 0) return "Device declares no input reports.";

            IntPtr h = HidNative.CreateFileW(
                device.DevicePath,
                HidNative.GENERIC_READ,
                HidNative.FILE_SHARE_READ | HidNative.FILE_SHARE_WRITE,
                IntPtr.Zero, HidNative.OPEN_EXISTING, 0, IntPtr.Zero);

            if (h == HidNative.INVALID_HANDLE_VALUE)
            {
                int err = Marshal.GetLastWin32Error();
                if (err == HidNative.ERROR_ACCESS_DENIED)
                {
                    return "Windows refused a read handle to this device (access denied). "
                         + "It does that for system keyboards and mice, and for devices a "
                         + "driver has claimed exclusively.";
                }
                return "Could not open device. Windows error " + err + ".";
            }

            IntPtr preparsed;
            if (!HidNative.HidD_GetPreparsedData(h, out preparsed))
            {
                HidNative.CloseHandle(h);
                return "Could not read the device report descriptor.";
            }

            HidNative.HIDP_CAPS caps = new HidNative.HIDP_CAPS();
            if (HidNative.HidP_GetCaps(preparsed, ref caps) != HidNative.HIDP_STATUS_SUCCESS)
            {
                HidNative.HidD_FreePreparsedData(preparsed);
                HidNative.CloseHandle(h);
                return "Could not parse the device capabilities.";
            }

            // A deeper queue costs nothing and stops fast wheels dropping reports
            // while the UI thread is busy repainting.
            HidNative.HidD_SetNumInputBuffers(h, 64);

            _handle = h;
            _preparsed = preparsed;
            _reportLength = caps.InputReportByteLength;
            Device = device;
            Layout = HidReportLayout.Read(preparsed, caps);

            return null;
        }

        public void Start()
        {
            if (_handle == HidNative.INVALID_HANDLE_VALUE) return;
            if (IsRunning) return;

            _stopping = false;
            _reportsSeen = 0;
            _clock.Restart();

            _thread = new Thread(ReadLoop);
            _thread.IsBackground = true;
            _thread.Name = "WheelForge HID reader";
            _thread.Start();
        }

        private void ReadLoop()
        {
            byte[] buffer = new byte[_reportLength];
            string reason = null;

            while (!_stopping)
            {
                uint read;
                bool ok = HidNative.ReadFile(_handle, buffer, (uint)buffer.Length, out read, IntPtr.Zero);

                if (!ok)
                {
                    int err = Marshal.GetLastWin32Error();
                    if (_stopping || err == HidNative.ERROR_OPERATION_ABORTED) break;
                    if (err == HidNative.ERROR_DEVICE_NOT_CONNECTED)
                    {
                        reason = "Device was unplugged.";
                        break;
                    }
                    reason = "Read failed. Windows error " + err + ".";
                    break;
                }

                if (read == 0) continue;

                _reportsSeen++;

                HidState state = Layout.Decode(_preparsed, buffer, (int)read);
                if (state != null)
                {
                    byte[] raw = new byte[read];
                    Buffer.BlockCopy(buffer, 0, raw, 0, (int)read);
                    state.Raw = raw;
                }

                Action<HidState> handler = StateChanged;
                if (handler != null && state != null) handler(state);
            }

            Action<string> stopped = Stopped;
            if (stopped != null) stopped(reason);
        }

        public void Stop()
        {
            if (!IsRunning)
            {
                _stopping = true;
                return;
            }

            _stopping = true;

            // Unblock the ReadFile the worker is sitting in, then wait for it to
            // actually return before anyone closes the handle.
            if (_handle != HidNative.INVALID_HANDLE_VALUE)
                HidNative.CancelIoEx(_handle, IntPtr.Zero);

            if (!_thread.Join(2000))
            {
                // The read did not come back. Leaking the handle is strictly
                // better than closing it under a live read.
                _thread = null;
                _handle = HidNative.INVALID_HANDLE_VALUE;
                _preparsed = IntPtr.Zero;
                return;
            }

            _thread = null;
            _clock.Stop();
        }

        public void Close()
        {
            Stop();

            if (_preparsed != IntPtr.Zero)
            {
                HidNative.HidD_FreePreparsedData(_preparsed);
                _preparsed = IntPtr.Zero;
            }

            if (_handle != HidNative.INVALID_HANDLE_VALUE)
            {
                HidNative.CloseHandle(_handle);
                _handle = HidNative.INVALID_HANDLE_VALUE;
            }

            Device = null;
            Layout = null;
        }

        public void Dispose()
        {
            Close();
        }
    }
}
