using System;
using System.IO;
using System.Runtime.InteropServices;

namespace WheelForge.Core
{
    // Feeds a vJoy virtual joystick from a bridge-mode board.
    //
    // A board with no USB device hardware cannot be a game controller itself.
    // It streams its state over serial instead, and this turns that stream into
    // a device Windows and games actually see. That is the whole reason an Uno,
    // a Nano or an ESP8266 can drive a game at all.
    //
    // vJoy is not bundled: it is a signed kernel driver and has to be installed
    // by the user. If it is missing this says so plainly rather than failing
    // somewhere further down.
    public class VJoyFeeder : IDisposable
    {
        // vJoy axis selectors are HID usage ids.
        private const uint HID_USAGE_X = 0x30;
        private const uint HID_USAGE_Y = 0x31;
        private const uint HID_USAGE_Z = 0x32;
        private const uint HID_USAGE_RX = 0x33;

        private const int VJD_STAT_OWN = 0;
        private const int VJD_STAT_FREE = 1;
        private const int VJD_STAT_BUSY = 2;
        private const int VJD_STAT_MISS = 3;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string path);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool vJoyEnabled();

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GetVJDStatus(uint rID);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool AcquireVJD(uint rID);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void RelinquishVJD(uint rID);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ResetVJD(uint rID);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetAxis(int value, uint rID, uint axis);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetBtn([MarshalAs(UnmanagedType.Bool)] bool value, uint rID, byte nBtn);

        [DllImport("vJoyInterface.dll", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetVJDAxisExist(uint rID, uint axis);

        private static bool _loaded;
        private static bool _loadFailed;

        private uint _device;
        private bool _acquired;

        // Only what actually changed is pushed. A wheel sitting still then costs
        // nothing, and a moving one costs one call per axis that moved rather
        // than twenty-eight calls per report.
        private readonly int[] _lastAxis = new int[4];
        private uint _lastButtons = 0xFFFFFFFF;
        private bool _firstFeed = true;

        public bool IsAcquired
        {
            get { return _acquired; }
        }

        public uint DeviceId
        {
            get { return _device; }
        }

        // vJoyInterface.dll is not on the search path, so it is loaded by full
        // path first; every DllImport below then binds to the already-loaded
        // module rather than searching for it.
        private static bool EnsureLoaded()
        {
            if (_loaded) return true;
            if (_loadFailed) return false;

            string[] candidates = new string[]
            {
                @"C:\Program Files\vJoy\x64\vJoyInterface.dll",
                @"C:\Program Files\vJoy\x86\vJoyInterface.dll",
                @"C:\Program Files (x86)\vJoy\x64\vJoyInterface.dll",
                @"C:\Program Files (x86)\vJoy\x86\vJoyInterface.dll"
            };

            foreach (string path in candidates)
            {
                if (!File.Exists(path)) continue;
                // A 64 bit process cannot load the x86 build and vice versa;
                // trying the wrong one simply fails and the loop moves on.
                if (LoadLibraryW(path) != IntPtr.Zero)
                {
                    _loaded = true;
                    return true;
                }
            }

            _loadFailed = true;
            return false;
        }

        public static bool IsInstalled()
        {
            if (!EnsureLoaded()) return false;
            try { return vJoyEnabled(); }
            catch (Exception) { return false; }
        }

        // Returns null on success, or a readable reason.
        public string Acquire(uint device)
        {
            Release();

            if (!EnsureLoaded())
                return "vJoy is not installed. Bridge boards need it to appear as a controller. "
                     + "Get it from sourceforge.net/projects/vjoystick.";

            try
            {
                if (!vJoyEnabled())
                    return "vJoy is installed but its driver is not enabled.";

                int status = GetVJDStatus(device);
                if (status == VJD_STAT_MISS)
                    return "vJoy device " + device + " does not exist. Add it in vJoyConf.";
                if (status == VJD_STAT_BUSY)
                    return "vJoy device " + device + " is already owned by another program.";
                if (status != VJD_STAT_FREE && status != VJD_STAT_OWN)
                    return "vJoy device " + device + " is not available (status " + status + ").";

                if (!AcquireVJD(device))
                    return "Could not acquire vJoy device " + device + ".";

                ResetVJD(device);
            }
            catch (DllNotFoundException)
            {
                return "vJoyInterface.dll could not be loaded.";
            }
            catch (EntryPointNotFoundException)
            {
                return "This vJoy build is missing functions WheelForge needs. Try a newer vJoy.";
            }
            catch (Exception ex)
            {
                return "vJoy error: " + ex.Message;
            }

            _device = device;
            _acquired = true;
            _firstFeed = true;
            _lastButtons = 0xFFFFFFFF;
            return null;
        }

        // Called from the serial reader thread.
        public void Feed(WheelInput input)
        {
            if (!_acquired || input == null) return;

            try
            {
                // vJoy axes run 0..32767. Steering arrives signed and centred;
                // pedals arrive 0..16383 on the wire.
                int steering = ((int)input.Steering + 32768) / 2;
                int throttle = input.Throttle * 2;
                int brake = input.Brake * 2;
                int clutch = input.Clutch * 2;

                PushAxis(0, HID_USAGE_X, steering);
                PushAxis(1, HID_USAGE_Y, throttle);
                PushAxis(2, HID_USAGE_Z, brake);
                PushAxis(3, HID_USAGE_RX, clutch);

                uint buttons = (uint)(input.Buttons[0]
                                    | (input.Buttons[1] << 8)
                                    | (input.Buttons[2] << 16));

                if (buttons != _lastButtons)
                {
                    for (int i = 0; i < 24; i++)
                    {
                        uint bit = 1u << i;
                        if (!_firstFeed && ((buttons ^ _lastButtons) & bit) == 0) continue;
                        SetBtn((buttons & bit) != 0, _device, (byte)(i + 1));
                    }
                    _lastButtons = buttons;
                }

                _firstFeed = false;
            }
            catch (Exception)
            {
                // The driver went away mid-feed. Stop rather than throwing on
                // the reader thread, where nothing would catch it.
                _acquired = false;
            }
        }

        private void PushAxis(int slot, uint usage, int value)
        {
            if (value < 0) value = 0;
            if (value > 32767) value = 32767;

            if (!_firstFeed && _lastAxis[slot] == value) return;
            _lastAxis[slot] = value;
            SetAxis(value, _device, usage);
        }

        public void Release()
        {
            if (!_acquired) return;
            _acquired = false;

            try
            {
                ResetVJD(_device);
                RelinquishVJD(_device);
            }
            catch (Exception)
            {
                // Driver already gone; nothing to release.
            }
        }

        public void Dispose()
        {
            Release();
        }
    }
}
