using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace WheelForge.Hid
{
    // One HID interface as Windows reports it. A single USB device can expose
    // several of these, which is why matching looks at usage page / usage
    // rather than just VID and PID.
    public class HidDeviceInfo
    {
        public string DevicePath;
        public ushort VendorId;
        public ushort ProductId;
        public ushort Version;
        public string Manufacturer;
        public string Product;
        public string SerialNumber;

        public ushort UsagePage;
        public ushort Usage;
        public int InputReportLength;
        public int OutputReportLength;
        public int FeatureReportLength;
        public int AxisCount;
        public int ButtonCount;

        // Usage page 1 (Generic Desktop), usage 4 (Joystick) or 5 (Gamepad).
        // This is the interface carrying the axes on a wheel.
        public bool IsGameController
        {
            get { return UsagePage == 0x01 && (Usage == 0x04 || Usage == 0x05); }
        }

        public string VidPid
        {
            get { return VendorId.ToString("X4") + ":" + ProductId.ToString("X4"); }
        }

        public string DisplayName
        {
            get
            {
                string name = Product;
                if (string.IsNullOrEmpty(name)) name = "(no product string)";
                return name + "  [" + VidPid + "]";
            }
        }

        // Devices that are known quantities get a friendlier label in the picker.
        public string KnownAs
        {
            get
            {
                if (VendorId == 0x0013 && ProductId == 0x1984) return "EMC / EMC Lite";
                if (VendorId == 0x1234 && ProductId == 0xBEAD) return "vJoy virtual joystick";
                if (VendorId == 0x046D)
                {
                    switch (ProductId)
                    {
                        case 0xC24F: return "Logitech G29";
                        case 0xC260: return "Logitech G29 (PS4 mode)";
                        case 0xC262: return "Logitech G920";
                        case 0xC29B: return "Logitech G27";
                        case 0xC299: return "Logitech G25";
                        case 0xC294: return "Logitech Driving Force";
                        case 0xC295: return "Logitech Momo";
                        case 0xCA03: return "Logitech Momo Racing";
                    }
                    return "Logitech wheel";
                }
                if (VendorId == 0x2341 || VendorId == 0x2A03) return "Arduino";
                if (VendorId == 0x1B4F) return "SparkFun / Pro Micro";
                if (VendorId == 0x2E8A) return "Raspberry Pi / RP2040";
                if (VendorId == 0x303A) return "Espressif ESP32-S2/S3";
                return null;
            }
        }
    }

    public static class HidEnumerator
    {
        // Enumerates every present HID interface.
        //
        // Each device is opened with desired access 0, which asks for identity
        // only. Windows answers that even for devices another program already
        // owns exclusively, so this listing still works while a game has the
        // wheel open.
        public static List<HidDeviceInfo> Enumerate()
        {
            List<HidDeviceInfo> found = new List<HidDeviceInfo>();

            Guid hidGuid;
            HidNative.HidD_GetHidGuid(out hidGuid);

            IntPtr set = HidNative.SetupDiGetClassDevsW(
                ref hidGuid, IntPtr.Zero, IntPtr.Zero,
                HidNative.DIGCF_PRESENT | HidNative.DIGCF_DEVICEINTERFACE);

            if (set == HidNative.INVALID_HANDLE_VALUE) return found;

            try
            {
                uint index = 0;
                while (true)
                {
                    HidNative.SP_DEVICE_INTERFACE_DATA iface = new HidNative.SP_DEVICE_INTERFACE_DATA();
                    iface.cbSize = Marshal.SizeOf(typeof(HidNative.SP_DEVICE_INTERFACE_DATA));

                    if (!HidNative.SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref hidGuid, index, ref iface))
                        break;   // ERROR_NO_MORE_ITEMS

                    index++;

                    string path = GetDevicePath(set, ref iface);
                    if (path == null) continue;

                    HidDeviceInfo info = Describe(path);
                    if (info != null) found.Add(info);
                }
            }
            finally
            {
                HidNative.SetupDiDestroyDeviceInfoList(set);
            }

            return found;
        }

        private static string GetDevicePath(IntPtr set, ref HidNative.SP_DEVICE_INTERFACE_DATA iface)
        {
            uint required;
            HidNative.SetupDiGetDeviceInterfaceDetailW(set, ref iface, IntPtr.Zero, 0, out required, IntPtr.Zero);
            if (required == 0) return null;

            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try
            {
                // The struct is { DWORD cbSize; WCHAR DevicePath[1]; }. Its cbSize
                // is 8 when packed for 64-bit and 6 for 32-bit, but DevicePath
                // always begins at byte offset 4 either way.
                Marshal.WriteInt32(buffer, 0, IntPtr.Size == 8 ? 8 : 6);

                uint written;
                if (!HidNative.SetupDiGetDeviceInterfaceDetailW(
                        set, ref iface, buffer, required, out written, IntPtr.Zero))
                    return null;

                return Marshal.PtrToStringUni(new IntPtr(buffer.ToInt64() + 4));
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }

        // Opens for identity only (access 0) and reads everything that does not
        // require a read handle.
        public static HidDeviceInfo Describe(string devicePath)
        {
            IntPtr h = HidNative.CreateFileW(
                devicePath, 0,
                HidNative.FILE_SHARE_READ | HidNative.FILE_SHARE_WRITE,
                IntPtr.Zero, HidNative.OPEN_EXISTING, 0, IntPtr.Zero);

            if (h == HidNative.INVALID_HANDLE_VALUE) return null;

            try
            {
                HidDeviceInfo info = new HidDeviceInfo();
                info.DevicePath = devicePath;

                HidNative.HIDD_ATTRIBUTES attrs = new HidNative.HIDD_ATTRIBUTES();
                attrs.Size = Marshal.SizeOf(typeof(HidNative.HIDD_ATTRIBUTES));
                if (HidNative.HidD_GetAttributes(h, ref attrs))
                {
                    info.VendorId = attrs.VendorID;
                    info.ProductId = attrs.ProductID;
                    info.Version = attrs.VersionNumber;
                }

                info.Manufacturer = ReadString(h, 1);
                info.Product = ReadString(h, 2);
                info.SerialNumber = ReadString(h, 3);

                IntPtr preparsed;
                if (HidNative.HidD_GetPreparsedData(h, out preparsed))
                {
                    try
                    {
                        HidNative.HIDP_CAPS caps = new HidNative.HIDP_CAPS();
                        if (HidNative.HidP_GetCaps(preparsed, ref caps) == HidNative.HIDP_STATUS_SUCCESS)
                        {
                            info.UsagePage = caps.UsagePage;
                            info.Usage = caps.Usage;
                            info.InputReportLength = caps.InputReportByteLength;
                            info.OutputReportLength = caps.OutputReportByteLength;
                            info.FeatureReportLength = caps.FeatureReportByteLength;

                            HidReportLayout layout = HidReportLayout.Read(preparsed, caps);
                            info.AxisCount = layout.Axes.Count;
                            info.ButtonCount = layout.ButtonCount;
                        }
                    }
                    finally
                    {
                        HidNative.HidD_FreePreparsedData(preparsed);
                    }
                }

                return info;
            }
            finally
            {
                HidNative.CloseHandle(h);
            }
        }

        private static string ReadString(IntPtr handle, int which)
        {
            byte[] buffer = new byte[510];   // HID string descriptors cap at 126 wchars
            bool ok;
            switch (which)
            {
                case 1: ok = HidNative.HidD_GetManufacturerString(handle, buffer, buffer.Length); break;
                case 2: ok = HidNative.HidD_GetProductString(handle, buffer, buffer.Length); break;
                default: ok = HidNative.HidD_GetSerialNumberString(handle, buffer, buffer.Length); break;
            }
            if (!ok) return null;

            string s = Encoding.Unicode.GetString(buffer);
            int nul = s.IndexOf('\0');
            if (nul >= 0) s = s.Substring(0, nul);
            return s.Trim();
        }

        // Picks the interface most likely to be the wheel: a game controller
        // wins over anything else, and among game controllers the one with the
        // most axes wins.
        public static HidDeviceInfo PickBestWheel(List<HidDeviceInfo> devices)
        {
            HidDeviceInfo best = null;
            foreach (HidDeviceInfo d in devices)
            {
                if (!d.IsGameController) continue;
                if (best == null || d.AxisCount > best.AxisCount) best = d;
            }
            return best;
        }
    }
}
