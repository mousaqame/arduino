#pragma once

// ESP32-S2 / ESP32-S3 native USB HID, with force feedback reports.
//
// The ESP32 Arduino USBHID stack hands output and feature reports to virtual
// methods on a USBHIDDevice, so all that is needed is to forward them into the
// PID engine.

#include <USB.h>
#include <USBHID.h>

static USBHID g_usbHid;

class WfHidDevice : public USBHIDDevice {
public:
  WfHidDevice() {
    static bool registered = false;
    if (!registered) {
      registered = true;
      g_usbHid.addDevice(this, sizeof(kReportDescriptor));
    }
  }

  uint16_t _onGetDescriptor(uint8_t* dst) {
    memcpy(dst, kReportDescriptor, sizeof(kReportDescriptor));
    return sizeof(kReportDescriptor);
  }

  uint16_t _onGetFeature(uint8_t reportId, uint8_t* buffer, uint16_t len) {
    uint8_t buf[8];
    uint8_t n = ffbHandleGetFeature(reportId, buf, sizeof(buf));
    if (n == 0) return 0;

    // The stack has already dealt with the report id, so the payload starts
    // one byte in.
    uint16_t copy = (uint16_t)(n - 1);
    if (copy > len) copy = len;
    memcpy(buffer, buf + 1, copy);
    return copy;
  }

  void _onSetFeature(uint8_t reportId, const uint8_t* buffer, uint16_t len) {
    uint8_t tmp[24];
    tmp[0] = reportId;
    uint16_t n = (uint16_t)(len + 1);
    if (n > sizeof(tmp)) n = sizeof(tmp);
    if (n > 1) memcpy(tmp + 1, buffer, n - 1);
    ffbHandleSetFeature(tmp, n);
  }

  void _onOutput(uint8_t reportId, const uint8_t* buffer, uint16_t len) {
    uint8_t tmp[24];
    uint16_t n;

    if (reportId == 0) {
      n = len;
      if (n > sizeof(tmp)) n = sizeof(tmp);
      memcpy(tmp, buffer, n);
    } else {
      tmp[0] = reportId;
      n = (uint16_t)(len + 1);
      if (n > sizeof(tmp)) n = sizeof(tmp);
      if (n > 1) memcpy(tmp + 1, buffer, n - 1);
    }

    ffbHandleOutput(tmp, n);
  }
};

static WfHidDevice g_hidDevice;

static void usbBegin() {
  g_usbHid.begin();
  USB.begin();
}

static void usbSend(const WheelReport& r) {
  if (g_usbHid.ready()) g_usbHid.SendReport(WF_RID_INPUT_JOYSTICK, &r, sizeof(r));
}

static void usbPoll() {
  // Callback driven; nothing to pump.
}
