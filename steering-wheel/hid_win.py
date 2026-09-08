#!/usr/bin/env python3
"""hid_win.py - talk to USB HID devices on Windows with no dependencies.

    python hid_win.py              # list every HID device
    python hid_win.py --wheel      # just the EMC wheel
    python hid_win.py --watch      # live input reports from the wheel

Uses ctypes against setupapi.dll and hid.dll directly, so there is nothing to
pip install. That matters here: this is meant to be a tool you can still run
after a fresh Windows install, on the machine your wheel is plugged into.

READ ONLY. Nothing in this file writes to a device. The wheel is running EMC
Lite and a stray write is the one thing that could disturb it mid-race, so
output and feature *writes* are deliberately absent until they are needed and
can be tested deliberately.
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import time
from ctypes import wintypes

if sys.platform != "win32":
    raise SystemExit("hid_win.py is Windows-only")

setupapi = ctypes.WinDLL("setupapi")
hid = ctypes.WinDLL("hid")
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

# ---------------------------------------------------------------------------
# Win32 plumbing
# ---------------------------------------------------------------------------

DIGCF_PRESENT = 0x02
DIGCF_DEVICEINTERFACE = 0x10
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x01
FILE_SHARE_WRITE = 0x02
OPEN_EXISTING = 3
FILE_FLAG_OVERLAPPED = 0x40000000
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
ERROR_IO_PENDING = 997
WAIT_TIMEOUT = 0x102
WAIT_OBJECT_0 = 0


class GUID(ctypes.Structure):
    _fields_ = [("Data1", wintypes.DWORD),
                ("Data2", wintypes.WORD),
                ("Data3", wintypes.WORD),
                ("Data4", ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD),
                ("InterfaceClassGuid", GUID),
                ("Flags", wintypes.DWORD),
                ("Reserved", ctypes.POINTER(wintypes.ULONG))]


class SP_DEVICE_INTERFACE_DETAIL_DATA_W(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD),
                ("DevicePath", wintypes.WCHAR * 1)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("Size", wintypes.ULONG),
                ("VendorID", wintypes.USHORT),
                ("ProductID", wintypes.USHORT),
                ("VersionNumber", wintypes.USHORT)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [("Usage", wintypes.USHORT),
                ("UsagePage", wintypes.USHORT),
                ("InputReportByteLength", wintypes.USHORT),
                ("OutputReportByteLength", wintypes.USHORT),
                ("FeatureReportByteLength", wintypes.USHORT),
                ("Reserved", wintypes.USHORT * 17),
                ("NumberLinkCollectionNodes", wintypes.USHORT),
                ("NumberInputButtonCaps", wintypes.USHORT),
                ("NumberInputValueCaps", wintypes.USHORT),
                ("NumberInputDataIndices", wintypes.USHORT),
                ("NumberOutputButtonCaps", wintypes.USHORT),
                ("NumberOutputValueCaps", wintypes.USHORT),
                ("NumberOutputDataIndices", wintypes.USHORT),
                ("NumberFeatureButtonCaps", wintypes.USHORT),
                ("NumberFeatureValueCaps", wintypes.USHORT),
                ("NumberFeatureDataIndices", wintypes.USHORT)]


# Every prototype is declared. Without argtypes, ctypes guesses - and on 64-bit
# it truncates a device handle to an int, which fails with a confusing
# "int too long to convert" rather than anything about handles.
setupapi.SetupDiGetClassDevsW.restype = wintypes.HANDLE
setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), wintypes.LPCWSTR,
                                          wintypes.HWND, wintypes.DWORD]
setupapi.SetupDiEnumDeviceInterfaces.restype = wintypes.BOOL
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [
    wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(GUID), wintypes.DWORD,
    ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
setupapi.SetupDiGetDeviceInterfaceDetailW.restype = wintypes.BOOL
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA), ctypes.c_void_p,
    wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
setupapi.SetupDiDestroyDeviceInfoList.restype = wintypes.BOOL
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [wintypes.HANDLE]

hid.HidD_GetHidGuid.argtypes = [ctypes.POINTER(GUID)]
hid.HidD_GetAttributes.restype = wintypes.BOOLEAN
hid.HidD_GetAttributes.argtypes = [wintypes.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hid.HidD_GetPreparsedData.restype = wintypes.BOOLEAN
hid.HidD_GetPreparsedData.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
hid.HidD_FreePreparsedData.restype = wintypes.BOOLEAN
hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]
hid.HidD_GetManufacturerString.restype = wintypes.BOOLEAN
hid.HidD_GetManufacturerString.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]
hid.HidD_GetProductString.restype = wintypes.BOOLEAN
hid.HidD_GetProductString.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.ULONG]

kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                 ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                 wintypes.HANDLE]
kernel32.ReadFile.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                              ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
kernel32.CloseHandle.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


def _string_prop(handle, fn, size=254) -> str:
    buf = ctypes.create_unicode_buffer(size)
    if fn(handle, buf, ctypes.sizeof(buf)):
        return buf.value
    return ""


# ---------------------------------------------------------------------------
# Enumeration
# ---------------------------------------------------------------------------

def enumerate_devices() -> list:
    """Every HID device Windows currently has, with its capabilities."""
    guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))

    info = setupapi.SetupDiGetClassDevsW(
        ctypes.byref(guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    if info == INVALID_HANDLE_VALUE:
        raise OSError("SetupDiGetClassDevs failed")

    found = []
    try:
        index = 0
        while True:
            iface = SP_DEVICE_INTERFACE_DATA()
            iface.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)
            if not setupapi.SetupDiEnumDeviceInterfaces(
                    info, None, ctypes.byref(guid), index, ctypes.byref(iface)):
                break
            index += 1

            # First call asks how much room the path needs.
            need = wintypes.DWORD()
            setupapi.SetupDiGetDeviceInterfaceDetailW(
                info, ctypes.byref(iface), None, 0, ctypes.byref(need), None)
            if not need.value:
                continue

            buf = ctypes.create_string_buffer(need.value)
            detail = ctypes.cast(buf, ctypes.POINTER(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            # cbSize is the size of the fixed header only, not the whole buffer.
            detail.contents.cbSize = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            if not setupapi.SetupDiGetDeviceInterfaceDetailW(
                    info, ctypes.byref(iface), detail, need.value, None, None):
                continue

            path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
            info_dict = _describe(path)
            if info_dict:
                found.append(info_dict)
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(info)

    return found


def _describe(path: str) -> dict | None:
    """Open a device just far enough to read its identity, then let go.

    Access 0 asks for metadata only. Game controllers and system keyboards
    refuse a read handle to anything but their owner, but they will always
    answer this - which is why enumeration works even while a game has the
    wheel open.
    """
    handle = kernel32.CreateFileW(
        path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
    if handle == INVALID_HANDLE_VALUE:
        return None
    try:
        attrs = HIDD_ATTRIBUTES()
        attrs.Size = ctypes.sizeof(HIDD_ATTRIBUTES)
        if not hid.HidD_GetAttributes(handle, ctypes.byref(attrs)):
            return None

        caps = HIDP_CAPS()
        preparsed = ctypes.c_void_p()
        if hid.HidD_GetPreparsedData(handle, ctypes.byref(preparsed)):
            hid.HidP_GetCaps(preparsed, ctypes.byref(caps))
            hid.HidD_FreePreparsedData(preparsed)

        return {
            "path": path,
            "vid": attrs.VendorID,
            "pid": attrs.ProductID,
            "version": attrs.VersionNumber,
            "manufacturer": _string_prop(handle, hid.HidD_GetManufacturerString),
            "product": _string_prop(handle, hid.HidD_GetProductString),
            "usage_page": caps.UsagePage,
            "usage": caps.Usage,
            "input_len": caps.InputReportByteLength,
            "output_len": caps.OutputReportByteLength,
            "feature_len": caps.FeatureReportByteLength,
        }
    finally:
        kernel32.CloseHandle(handle)


# The wheel, as EMC Lite reports itself. Confirmed against the DirectInput
# registry, where VID_0013&PID_1984 is named "EMC".
EMC_VID, EMC_PID = 0x0013, 0x1984

# Usage page 1 (Generic Desktop), usage 4 = Joystick, 5 = Gamepad. A device can
# expose several HID interfaces; this is the one carrying the axes.
USAGE_JOYSTICK, USAGE_GAMEPAD = 4, 5


def find_wheel(vid: int = EMC_VID, pid: int = EMC_PID) -> dict | None:
    """The wheel's joystick interface, or None if it is not plugged in."""
    matches = [d for d in enumerate_devices() if d["vid"] == vid and d["pid"] == pid]
    if not matches:
        return None
    for d in matches:
        if d["usage_page"] == 1 and d["usage"] in (USAGE_JOYSTICK, USAGE_GAMEPAD):
            return d
    return matches[0]


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------

class HidReader:
    """Reads input reports. Opens shared, so a game can keep using the device."""

    def __init__(self, path: str, input_len: int):
        self.input_len = input_len
        self.handle = kernel32.CreateFileW(
            path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            None, OPEN_EXISTING, 0, None)
        if self.handle == INVALID_HANDLE_VALUE:
            err = ctypes.get_last_error()
            raise OSError("could not open device for reading (error %d). "
                          "Windows refuses a read handle to some HID classes "
                          "while another program owns them." % err)

    def read(self) -> bytes | None:
        buf = ctypes.create_string_buffer(self.input_len)
        got = wintypes.DWORD()
        ok = kernel32.ReadFile(self.handle, buf, self.input_len,
                               ctypes.byref(got), None)
        if not ok:
            return None
        return buf.raw[:got.value]

    def close(self):
        if self.handle and self.handle != INVALID_HANDLE_VALUE:
            kernel32.CloseHandle(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


# ---------------------------------------------------------------------------
# Decoding a report into named axes and buttons
# ---------------------------------------------------------------------------
#
# The alternative is counting bytes into the report and hoping. Windows already
# parsed the device's report descriptor when it enumerated, so asking it "what
# is the X axis right now" is both shorter and correct on any device - which
# matters because this descriptor is EMC Lite's, not one we chose.

HIDP_INPUT, HIDP_OUTPUT, HIDP_FEATURE = 0, 1, 2
HIDP_STATUS_SUCCESS = 0x00110000

# Generic Desktop usages (page 0x01). A wheel normally puts steering on X and
# the pedals on whichever axes were left free.
USAGE_NAMES = {
    0x30: "X", 0x31: "Y", 0x32: "Z", 0x33: "Rx", 0x34: "Ry", 0x35: "Rz",
    0x36: "Slider", 0x37: "Dial", 0x38: "Wheel", 0x39: "Hat",
}


class _CapsRange(ctypes.Structure):
    _fields_ = [("UsageMin", wintypes.USHORT), ("UsageMax", wintypes.USHORT),
                ("StringMin", wintypes.USHORT), ("StringMax", wintypes.USHORT),
                ("DesignatorMin", wintypes.USHORT), ("DesignatorMax", wintypes.USHORT),
                ("DataIndexMin", wintypes.USHORT), ("DataIndexMax", wintypes.USHORT)]


class _CapsNotRange(ctypes.Structure):
    _fields_ = [("Usage", wintypes.USHORT), ("Reserved1", wintypes.USHORT),
                ("StringIndex", wintypes.USHORT), ("Reserved2", wintypes.USHORT),
                ("DesignatorIndex", wintypes.USHORT), ("Reserved3", wintypes.USHORT),
                ("DataIndex", wintypes.USHORT), ("Reserved4", wintypes.USHORT)]


class _CapsUnion(ctypes.Union):
    _fields_ = [("Range", _CapsRange), ("NotRange", _CapsNotRange)]


class HIDP_VALUE_CAPS(ctypes.Structure):
    _fields_ = [("UsagePage", wintypes.USHORT),
                ("ReportID", ctypes.c_ubyte),
                ("IsAlias", ctypes.c_ubyte),
                ("BitField", wintypes.USHORT),
                ("LinkCollection", wintypes.USHORT),
                ("LinkUsage", wintypes.USHORT),
                ("LinkUsagePage", wintypes.USHORT),
                ("IsRange", ctypes.c_ubyte),
                ("IsStringRange", ctypes.c_ubyte),
                ("IsDesignatorRange", ctypes.c_ubyte),
                ("IsAbsolute", ctypes.c_ubyte),
                ("HasNull", ctypes.c_ubyte),
                ("Reserved", ctypes.c_ubyte),
                ("BitSize", wintypes.USHORT),
                ("ReportCount", wintypes.USHORT),
                ("Reserved2", wintypes.USHORT * 5),
                ("UnitsExp", wintypes.ULONG),
                ("Units", wintypes.ULONG),
                ("LogicalMin", wintypes.LONG),
                ("LogicalMax", wintypes.LONG),
                ("PhysicalMin", wintypes.LONG),
                ("PhysicalMax", wintypes.LONG),
                ("u", _CapsUnion)]


class HIDP_BUTTON_CAPS(ctypes.Structure):
    _fields_ = [("UsagePage", wintypes.USHORT),
                ("ReportID", ctypes.c_ubyte),
                ("IsAlias", ctypes.c_ubyte),
                ("BitField", wintypes.USHORT),
                ("LinkCollection", wintypes.USHORT),
                ("LinkUsage", wintypes.USHORT),
                ("LinkUsagePage", wintypes.USHORT),
                ("IsRange", ctypes.c_ubyte),
                ("IsStringRange", ctypes.c_ubyte),
                ("IsDesignatorRange", ctypes.c_ubyte),
                ("IsAbsolute", ctypes.c_ubyte),
                ("Reserved", ctypes.c_ubyte * 61),
                ("u", _CapsUnion)]


hid.HidP_GetValueCaps.argtypes = [ctypes.c_int, ctypes.POINTER(HIDP_VALUE_CAPS),
                                  ctypes.POINTER(wintypes.USHORT), ctypes.c_void_p]
hid.HidP_GetButtonCaps.argtypes = [ctypes.c_int, ctypes.POINTER(HIDP_BUTTON_CAPS),
                                   ctypes.POINTER(wintypes.USHORT), ctypes.c_void_p]
hid.HidP_GetUsageValue.argtypes = [ctypes.c_int, wintypes.USHORT, wintypes.USHORT,
                                   wintypes.USHORT, ctypes.POINTER(wintypes.ULONG),
                                   ctypes.c_void_p, ctypes.c_char_p, wintypes.ULONG]
hid.HidP_GetUsages.argtypes = [ctypes.c_int, wintypes.USHORT, wintypes.USHORT,
                               ctypes.POINTER(wintypes.USHORT),
                               ctypes.POINTER(wintypes.ULONG), ctypes.c_void_p,
                               ctypes.c_char_p, wintypes.ULONG]
hid.HidP_MaxUsageListLength.restype = wintypes.ULONG
hid.HidP_MaxUsageListLength.argtypes = [ctypes.c_int, wintypes.USHORT, ctypes.c_void_p]


class HidDecoder:
    """Turns raw input reports into named axes and pressed buttons.

    Holds the device's preparsed data - what Windows built from the report
    descriptor when it enumerated. Opened with access 0, identity only, so
    this keeps working while a game owns the device.
    """

    def __init__(self, path):
        self._handle = kernel32.CreateFileW(
            path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
        if self._handle == INVALID_HANDLE_VALUE:
            raise OSError("could not open %s to read its descriptor" % path)

        self._pp = ctypes.c_void_p()
        if not hid.HidD_GetPreparsedData(self._handle, ctypes.byref(self._pp)):
            kernel32.CloseHandle(self._handle)
            raise OSError("no preparsed data for %s" % path)

        self.axes = []
        self.buttons = []
        self.input_len = 0
        self._read_caps()

    def _read_caps(self):
        caps = HIDP_CAPS()
        hid.HidP_GetCaps(self._pp, ctypes.byref(caps))
        self.input_len = caps.InputReportByteLength

        n = wintypes.USHORT(caps.NumberInputValueCaps)
        if n.value:
            arr = (HIDP_VALUE_CAPS * n.value)()
            if hid.HidP_GetValueCaps(HIDP_INPUT, arr, ctypes.byref(n), self._pp) == HIDP_STATUS_SUCCESS:
                for c in arr[:n.value]:
                    usage = c.u.Range.UsageMin if c.IsRange else c.u.NotRange.Usage
                    self.axes.append({
                        "usage_page": c.UsagePage,
                        "usage": usage,
                        "name": USAGE_NAMES.get(usage, "usage_%02X" % usage),
                        "logical_min": c.LogicalMin,
                        "logical_max": c.LogicalMax,
                        "bits": c.BitSize,
                        "link": c.LinkCollection,
                    })

        n = wintypes.USHORT(caps.NumberInputButtonCaps)
        if n.value:
            arr = (HIDP_BUTTON_CAPS * n.value)()
            if hid.HidP_GetButtonCaps(HIDP_INPUT, arr, ctypes.byref(n), self._pp) == HIDP_STATUS_SUCCESS:
                for c in arr[:n.value]:
                    lo = c.u.Range.UsageMin if c.IsRange else c.u.NotRange.Usage
                    hi = c.u.Range.UsageMax if c.IsRange else c.u.NotRange.Usage
                    self.buttons.append({
                        "usage_page": c.UsagePage,
                        "usage_min": lo, "usage_max": hi,
                        "count": hi - lo + 1,
                        "link": c.LinkCollection,
                    })

    def button_count(self):
        return sum(b["count"] for b in self.buttons)

    def decode(self, report):
        """One report -> {"axes": {name: {...}}, "buttons": [1-based ...]}"""
        buf = ctypes.c_char_p(bytes(report))
        rlen = wintypes.ULONG(len(report))
        out_axes = {}

        for a in self.axes:
            val = wintypes.ULONG(0)
            st = hid.HidP_GetUsageValue(
                HIDP_INPUT, a["usage_page"], a["link"], a["usage"],
                ctypes.byref(val), self._pp, buf, rlen)
            if st != HIDP_STATUS_SUCCESS:
                continue
            raw = val.value
            lo, hi = a["logical_min"], a["logical_max"]

            # HidP_GetUsageValue hands back the field's bits UNSIGNED. On a
            # signed axis - steering is -32768..32767 - a small left-hand value
            # like -100 therefore arrives as 65436, and normalising that lands
            # way past full scale. Every turn off centre then pinned the needle.
            # Sign-extend from the field's own width to undo it.
            bits = a.get("bits") or 0
            if lo < 0 and bits and raw >= (1 << (bits - 1)):
                raw -= (1 << bits)

            # A descriptor declaring min == max is rare but would divide by zero.
            span = (hi - lo) or 1
            norm = (raw - lo) / float(span)
            out_axes[a["name"]] = {
                "raw": raw,
                "min": lo,
                "max": hi,
                "norm": max(0.0, min(1.0, norm)),
                # Steering reads better centred; pedals read better as 0..1, so
                # both forms are offered and the page picks.
                "centered": max(-1.0, min(1.0, norm * 2.0 - 1.0)),
            }

        pressed = []
        for b in self.buttons:
            maxlen = hid.HidP_MaxUsageListLength(HIDP_INPUT, b["usage_page"], self._pp)
            if not maxlen:
                continue
            lst = (wintypes.USHORT * maxlen)()
            n = wintypes.ULONG(maxlen)
            st = hid.HidP_GetUsages(HIDP_INPUT, b["usage_page"], b["link"],
                                    lst, ctypes.byref(n), self._pp, buf, rlen)
            if st == HIDP_STATUS_SUCCESS:
                # Usages are already 1-based, which is what joy.cpl shows.
                pressed.extend(int(u) for u in lst[:n.value])

        return {"axes": out_axes, "buttons": sorted(set(pressed))}

    def close(self):
        if self._pp:
            hid.HidD_FreePreparsedData(self._pp)
            self._pp = None
        if self._handle and self._handle != INVALID_HANDLE_VALUE:
            kernel32.CloseHandle(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def print_table(devices: list) -> None:
    print("%-10s %-5s %-5s %-6s %-28s %s"
          % ("VID:PID", "PAGE", "USE", "IN/OUT", "PRODUCT", "PATH"))
    for d in devices:
        print("%04X:%04X  %-5d %-5d %-6s %-28s %s"
              % (d["vid"], d["pid"], d["usage_page"], d["usage"],
                 "%d/%d" % (d["input_len"], d["output_len"]),
                 (d["product"] or "-")[:28],
                 d["path"][:58]))


def main() -> int:
    ap = argparse.ArgumentParser(description="Windows HID explorer (read only)")
    ap.add_argument("--wheel", action="store_true", help="only the EMC wheel")
    ap.add_argument("--watch", action="store_true",
                    help="stream the wheel's input reports")
    ap.add_argument("--vid", type=lambda s: int(s, 16), default=EMC_VID)
    ap.add_argument("--pid", type=lambda s: int(s, 16), default=EMC_PID)
    ap.add_argument("--decode", action="store_true",
                    help="show the wheel's named axes and buttons live")
    ap.add_argument("--seconds", type=float, default=15.0)
    args = ap.parse_args()

    if args.decode:
        dev = find_wheel(args.vid, args.pid)
        if not dev:
            print("Wheel %04X:%04X is not plugged in." % (args.vid, args.pid))
            return 1
        with HidDecoder(dev["path"]) as dec:
            print("%s - %d axes, %d buttons, %d-byte reports"
                  % (dev["product"] or "wheel", len(dec.axes),
                     dec.button_count(), dec.input_len))
            for a in dec.axes:
                print("   axis %-8s usage 0x%02X  range %d..%d  (%d bits)"
                      % (a["name"], a["usage"], a["logical_min"],
                         a["logical_max"], a["bits"]))
            for b in dec.buttons:
                print("   buttons %d..%d on page 0x%02X"
                      % (b["usage_min"], b["usage_max"], b["usage_page"]))
            print("")
            print("Turn the wheel and press buttons. Ctrl+C to stop.")
            try:
                with HidReader(dev["path"], dev["input_len"]) as r:
                    end = time.time() + args.seconds
                    last = None
                    while time.time() < end:
                        data = r.read()
                        if not data:
                            continue
                        st = dec.decode(data)
                        line = "  ".join("%s%+6.2f" % (k, v["centered"])
                                         for k, v in sorted(st["axes"].items()))
                        if st["buttons"]:
                            line += "   btn " + ",".join(str(b) for b in st["buttons"])
                        if line != last:
                            last = line
                            print(line)
            except KeyboardInterrupt:
                pass
            except OSError as exc:
                print(exc)
                return 1
        return 0

    if args.watch:
        dev = find_wheel(args.vid, args.pid)
        if not dev:
            print("Wheel %04X:%04X is not plugged in." % (args.vid, args.pid))
            return 1
        print("Watching %s  (%d-byte input reports)"
              % (dev["product"] or "wheel", dev["input_len"]))
        print("Turn the wheel and press buttons. Ctrl+C to stop.\n")
        try:
            with HidReader(dev["path"], dev["input_len"]) as r:
                last = None
                end = time.time() + args.seconds
                while time.time() < end:
                    data = r.read()
                    if data and data != last:
                        last = data
                        print(" ".join("%02X" % b for b in data))
        except KeyboardInterrupt:
            pass
        except OSError as exc:
            print(exc)
            return 1
        return 0

    devices = enumerate_devices()
    if args.wheel:
        devices = [d for d in devices if d["vid"] == args.vid and d["pid"] == args.pid]
        if not devices:
            print("Wheel %04X:%04X is not plugged in." % (args.vid, args.pid))
            return 1

    print_table(devices)
    print("\n%d HID device(s)" % len(devices))
    return 0


if __name__ == "__main__":
    sys.exit(main())
