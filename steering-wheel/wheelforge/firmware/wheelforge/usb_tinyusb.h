#pragma once

// TinyUSB HID with an OUT endpoint, for the RP2040.
//
// Much less work than the AVR side: TinyUSB already supports OUT and Feature
// reports, it just needs telling that this interface has an OUT endpoint (the
// last constructor argument) and where to deliver what arrives.

#include <Adafruit_TinyUSB.h>

static uint16_t wfGetReportCb(uint8_t reportId, hid_report_type_t type,
                              uint8_t* buffer, uint16_t reqlen);
static void wfSetReportCb(uint8_t reportId, hid_report_type_t type,
                          uint8_t const* buffer, uint16_t bufsize);

static Adafruit_USBD_HID g_usbHid(kReportDescriptor, sizeof(kReportDescriptor),
                                  HID_ITF_PROTOCOL_NONE, 2, true);

// TinyUSB strips the report id into its own argument when the host used a
// numbered Feature or Set_Report request, but leaves it in the buffer when the
// data arrived on the interrupt OUT endpoint. The engine always wants it in
// the buffer, so put it back when it is missing.
static void wfSetReportCb(uint8_t reportId, hid_report_type_t type,
                          uint8_t const* buffer, uint16_t bufsize) {
  uint8_t tmp[24];
  uint16_t n;

  if (reportId == 0) {
    n = bufsize;
    if (n > sizeof(tmp)) n = sizeof(tmp);
    memcpy(tmp, buffer, n);
  } else {
    tmp[0] = reportId;
    n = (uint16_t)(bufsize + 1);
    if (n > sizeof(tmp)) n = sizeof(tmp);
    if (n > 1) memcpy(tmp + 1, buffer, n - 1);
  }

  if (type == HID_REPORT_TYPE_FEATURE) ffbHandleSetFeature(tmp, n);
  else ffbHandleOutput(tmp, n);
}

static uint16_t wfGetReportCb(uint8_t reportId, hid_report_type_t type,
                              uint8_t* buffer, uint16_t reqlen) {
  if (type != HID_REPORT_TYPE_FEATURE) return 0;

  uint8_t buf[8];
  uint8_t n = ffbHandleGetFeature(reportId, buf, sizeof(buf));
  if (n == 0) return 0;

  // The reply must not repeat the report id TinyUSB has already handled.
  uint16_t copy = (uint16_t)(n - 1);
  if (copy > reqlen) copy = reqlen;
  memcpy(buffer, buf + 1, copy);
  return copy;
}

static void usbBegin() {
  g_usbHid.setReportCallback(wfGetReportCb, wfSetReportCb);
  g_usbHid.setStringDescriptor("WheelForge Wheel");
  g_usbHid.begin();
}

static void usbSend(const WheelReport& r) {
  if (g_usbHid.ready()) g_usbHid.sendReport(WF_RID_INPUT_JOYSTICK, &r, sizeof(r));
}

static void usbPoll() {
  // Callback driven; nothing to pump.
}
