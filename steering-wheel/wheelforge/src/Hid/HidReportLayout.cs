using System;
using System.Collections.Generic;

namespace WheelForge.Hid
{
    // One analogue control the device declares in its report descriptor.
    public class HidAxis
    {
        public ushort UsagePage;
        public ushort Usage;
        public int LogicalMin;
        public int LogicalMax;
        public int BitSize;
        public string Name;

        public bool IsHat
        {
            get { return UsagePage == 0x01 && Usage == 0x39; }
        }

        public int Range
        {
            get { return LogicalMax - LogicalMin; }
        }
    }

    // The set of axes and buttons a device declares, discovered from its
    // preparsed data rather than assumed.
    //
    // This is the reason WheelForge works against any controller -- EMC Lite,
    // a G29, vJoy, or our own firmware -- without knowing its byte layout in
    // advance. Windows already parsed the report descriptor; we ask it what is
    // in there instead of hardcoding offsets.
    public class HidReportLayout
    {
        public List<HidAxis> Axes = new List<HidAxis>();
        public int ButtonCount;
        public ushort ButtonUsageMin = 1;
        public int MaxUsageListLength;

        // Internal because HIDP_CAPS is: the P/Invoke surface stays private to
        // the assembly.
        internal static HidReportLayout Read(IntPtr preparsed, HidNative.HIDP_CAPS caps)
        {
            HidReportLayout layout = new HidReportLayout();

            if (caps.NumberInputValueCaps > 0)
            {
                HidNative.HIDP_VALUE_CAPS[] valueCaps =
                    new HidNative.HIDP_VALUE_CAPS[caps.NumberInputValueCaps];
                ushort count = caps.NumberInputValueCaps;

                if (HidNative.HidP_GetValueCaps(HidNative.HidP_Input, valueCaps, ref count, preparsed)
                    == HidNative.HIDP_STATUS_SUCCESS)
                {
                    for (int i = 0; i < count; i++)
                    {
                        HidNative.HIDP_VALUE_CAPS vc = valueCaps[i];

                        // A range cap covers several consecutive usages in one
                        // entry, e.g. X..Rz declared as 0x30..0x35.
                        ushort first = vc.UsageMin;
                        ushort last = vc.IsRange ? vc.UsageMax : vc.UsageMin;

                        for (ushort u = first; u <= last; u++)
                        {
                            HidAxis axis = new HidAxis();
                            axis.UsagePage = vc.UsagePage;
                            axis.Usage = u;
                            axis.LogicalMin = vc.LogicalMin;
                            axis.LogicalMax = vc.LogicalMax;
                            axis.BitSize = vc.BitSize;
                            axis.Name = UsageNames.Describe(vc.UsagePage, u);
                            layout.Axes.Add(axis);

                            if (last < first) break;   // guard against a bad descriptor
                        }
                    }
                }
            }

            if (caps.NumberInputButtonCaps > 0)
            {
                HidNative.HIDP_BUTTON_CAPS[] buttonCaps =
                    new HidNative.HIDP_BUTTON_CAPS[caps.NumberInputButtonCaps];
                ushort count = caps.NumberInputButtonCaps;

                if (HidNative.HidP_GetButtonCaps(HidNative.HidP_Input, buttonCaps, ref count, preparsed)
                    == HidNative.HIDP_STATUS_SUCCESS)
                {
                    int highest = 0;
                    ushort lowest = ushort.MaxValue;

                    for (int i = 0; i < count; i++)
                    {
                        HidNative.HIDP_BUTTON_CAPS bc = buttonCaps[i];
                        if (bc.UsagePage != 0x09) continue;   // Button page only

                        ushort first = bc.UsageMin;
                        ushort last = bc.IsRange ? bc.UsageMax : bc.UsageMin;
                        if (first < lowest) lowest = first;
                        if (last > highest) highest = last;
                    }

                    if (highest > 0)
                    {
                        layout.ButtonUsageMin = lowest == ushort.MaxValue ? (ushort)1 : lowest;
                        layout.ButtonCount = highest - layout.ButtonUsageMin + 1;
                    }
                }

                layout.MaxUsageListLength =
                    (int)HidNative.HidP_MaxUsageListLength(HidNative.HidP_Input, 0x09, preparsed);
            }

            return layout;
        }

        // Pulls one input report apart into axis values and pressed buttons.
        // Returns null only if the report is unusable.
        public HidState Decode(IntPtr preparsed, byte[] report, int length)
        {
            HidState state = new HidState();
            state.AxisRaw = new int[Axes.Count];
            state.AxisNormalised = new double[Axes.Count];

            for (int i = 0; i < Axes.Count; i++)
            {
                HidAxis axis = Axes[i];
                uint raw;
                int status = HidNative.HidP_GetUsageValue(
                    HidNative.HidP_Input, axis.UsagePage, 0, axis.Usage,
                    out raw, preparsed, report, (uint)length);

                if (status != HidNative.HIDP_STATUS_SUCCESS)
                {
                    // Not present in this report id. Leave it at rest.
                    state.AxisRaw[i] = axis.LogicalMin;
                    state.AxisNormalised[i] = 0.0;
                    continue;
                }

                int value = SignExtend(raw, axis);
                state.AxisRaw[i] = value;

                int range = axis.Range;
                if (range > 0)
                {
                    double n = (double)(value - axis.LogicalMin) / range;
                    if (n < 0.0) n = 0.0;
                    if (n > 1.0) n = 1.0;
                    state.AxisNormalised[i] = n;
                }
            }

            if (MaxUsageListLength > 0)
            {
                ushort[] usages = new ushort[MaxUsageListLength];
                uint usageCount = (uint)MaxUsageListLength;

                int status = HidNative.HidP_GetUsages(
                    HidNative.HidP_Input, 0x09, 0, usages, ref usageCount,
                    preparsed, report, (uint)length);

                if (status == HidNative.HIDP_STATUS_SUCCESS)
                {
                    List<int> pressed = new List<int>();
                    for (int i = 0; i < usageCount; i++)
                        pressed.Add(usages[i] - ButtonUsageMin);   // zero based
                    state.PressedButtons = pressed;
                }
            }

            return state;
        }

        // HidP_GetUsageValue hands back an unsigned field. When the descriptor
        // declares a negative logical minimum the value is really signed, so the
        // sign bit at BitSize has to be extended by hand.
        private static int SignExtend(uint raw, HidAxis axis)
        {
            if (axis.LogicalMin >= 0) return (int)raw;
            if (axis.BitSize <= 0 || axis.BitSize >= 32) return (int)raw;

            uint signBit = 1u << (axis.BitSize - 1);
            if ((raw & signBit) == 0) return (int)raw;

            uint mask = uint.MaxValue << axis.BitSize;
            return (int)(raw | mask);
        }
    }

    // A single decoded snapshot of the device.
    public class HidState
    {
        public int[] AxisRaw;
        public double[] AxisNormalised;
        public List<int> PressedButtons = new List<int>();

        // The bytes this snapshot was decoded from. Kept because seeing the raw
        // report is what makes an unknown wheel's layout figure-out-able.
        public byte[] Raw;

        public bool IsPressed(int zeroBasedButton)
        {
            return PressedButtons.Contains(zeroBasedButton);
        }
    }

    public static class BridgeLayout
    {
        // A bridge board does not enumerate, so Windows never parses a
        // descriptor for it and there is nothing to read capabilities from.
        // This is that descriptor written out by hand: it has to match the
        // WheelReport struct the firmware sends over serial.
        public static HidReportLayout Build()
        {
            HidReportLayout layout = new HidReportLayout();

            HidAxis steering = new HidAxis();
            steering.UsagePage = 0x01; steering.Usage = 0x30;
            steering.LogicalMin = -32768; steering.LogicalMax = 32767;
            steering.BitSize = 16; steering.Name = "Steering";
            layout.Axes.Add(steering);

            layout.Axes.Add(Pedal(0xC4, "Accelerator"));
            layout.Axes.Add(Pedal(0xC5, "Brake"));
            layout.Axes.Add(Pedal(0xC6, "Clutch"));

            layout.ButtonCount = 24;
            layout.ButtonUsageMin = 1;
            layout.MaxUsageListLength = 24;
            return layout;
        }

        private static HidAxis Pedal(ushort usage, string name)
        {
            HidAxis a = new HidAxis();
            a.UsagePage = 0x02; a.Usage = usage;
            a.LogicalMin = 0; a.LogicalMax = 16383;
            a.BitSize = 16; a.Name = name;
            return a;
        }
    }

    public static class UsageNames
    {
        // Usages 0xB0..0xC8 are the Simulation Controls names (Accelerator,
        // Brake, Clutch, Steering...). They belong on usage page 2, but plenty
        // of real devices -- vJoy among them -- declare them on the Generic
        // Desktop page instead, where those ids are undefined. The intent is
        // unmistakable either way, so both pages get the simulation names.
        public static bool IsSimulationUsage(ushort page, ushort usage)
        {
            if (page != 0x01 && page != 0x02) return false;
            return usage >= 0xB0 && usage <= 0xC8;
        }

        public static string Describe(ushort page, ushort usage)
        {
            if (page == 0x01 && usage >= 0x30 && usage <= 0x39)
            {
                switch (usage)
                {
                    case 0x30: return "X";
                    case 0x31: return "Y";
                    case 0x32: return "Z";
                    case 0x33: return "Rx";
                    case 0x34: return "Ry";
                    case 0x35: return "Rz";
                    case 0x36: return "Slider";
                    case 0x37: return "Dial";
                    case 0x38: return "Wheel";
                    case 0x39: return "Hat";
                }
            }

            if (IsSimulationUsage(page, usage))
            {
                switch (usage)
                {
                    case 0xB0: return "Aileron";
                    case 0xB5: return "Rudder";
                    case 0xBA: return "Rudder";
                    case 0xBB: return "Throttle";
                    case 0xC4: return "Accelerator";
                    case 0xC5: return "Brake";
                    case 0xC6: return "Clutch";
                    case 0xC7: return "Shifter";
                    case 0xC8: return "Steering";
                }
            }

            return "Usage " + page.ToString("X2") + ":" + usage.ToString("X2");
        }
    }
}
