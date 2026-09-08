#pragma once

// A HID interface for the ATmega32u4 with an interrupt OUT endpoint.
//
// This exists because the Arduino AVR core's own HID library declares only an
// IN endpoint. That is fine for a keyboard or a gamepad, but force feedback is
// entirely made of reports flowing the other way, so a device built on the
// stock HID_ class can never receive a single force. Replacing it with a
// PluggableUSB module that declares both directions is the only way in.
//
// Do not include <HID.h> alongside this. Merely including it instantiates the
// core's HID_ singleton, which plugs itself in and consumes an endpoint.

#include <PluggableUSB.h>

#define WF_HID_GET_REPORT      0x01
#define WF_HID_GET_IDLE        0x02
#define WF_HID_GET_PROTOCOL    0x03
#define WF_HID_SET_REPORT      0x09
#define WF_HID_SET_IDLE        0x0A
#define WF_HID_SET_PROTOCOL    0x0B

#define WF_HID_DESCRIPTOR_TYPE        0x21
#define WF_HID_REPORT_DESCRIPTOR_TYPE 0x22

typedef struct {
  uint8_t  len;
  uint8_t  dtype;
  uint16_t hidVersion;
  uint8_t  country;
  uint8_t  numDescriptors;
  uint8_t  descriptorType;
  uint16_t descriptorLength;
} __attribute__((packed)) WfHidClassDescriptor;

typedef struct {
  InterfaceDescriptor  iface;
  WfHidClassDescriptor hid;
  EndpointDescriptor   in;
  EndpointDescriptor   out;
} __attribute__((packed)) WfHidInterfaceDescriptor;

class WfHidModule : public PluggableUSBModule {
public:
  WfHidModule() : PluggableUSBModule(2, 1, epType) {
    epType[0] = EP_TYPE_INTERRUPT_IN;
    epType[1] = EP_TYPE_INTERRUPT_OUT;
    PluggableUSB().plug(this);
  }

  bool sendReport(uint8_t id, const void* data, int len) {
    if (USB_Send(pluggedEndpoint | TRANSFER_RELEASE, &id, 1) < 0) return false;
    return USB_Send(pluggedEndpoint | TRANSFER_RELEASE, data, len) >= 0;
  }

  // Drains anything the host has sent to the OUT endpoint. Called from the
  // main loop rather than an interrupt, so effect parsing never runs with
  // interrupts disabled.
  void poll() {
    uint8_t out = (uint8_t)(pluggedEndpoint + 1);
    uint8_t buf[24];

    while (USB_Available(out)) {
      int n = USB_Recv(out, buf, sizeof(buf));
      if (n <= 0) break;
      ffbHandleOutput(buf, (uint16_t)n);
    }
  }

protected:
  int getInterface(uint8_t* interfaceCount) {
    *interfaceCount += 1;

    WfHidInterfaceDescriptor d = {
      D_INTERFACE(pluggedInterface, 2, USB_DEVICE_CLASS_HUMAN_INTERFACE, 0, 0),
      {
        sizeof(WfHidClassDescriptor),
        WF_HID_DESCRIPTOR_TYPE,
        0x0111,                       // HID 1.11
        0,                            // no country code
        1,                            // one class descriptor follows
        WF_HID_REPORT_DESCRIPTOR_TYPE,
        (uint16_t)sizeof(kReportDescriptor)
      },
      D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint),
                 USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01),
      D_ENDPOINT(USB_ENDPOINT_OUT(pluggedEndpoint + 1),
                 USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01)
    };

    return USB_SendControl(0, &d, sizeof(d));
  }

  int getDescriptor(USBSetup& setup) {
    if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) return 0;
    if (setup.wValueH != WF_HID_REPORT_DESCRIPTOR_TYPE) return 0;
    if (setup.wIndex != pluggedInterface) return 0;

    // TRANSFER_PGM because the descriptor lives in flash on AVR.
    return USB_SendControl(TRANSFER_PGM, kReportDescriptor, sizeof(kReportDescriptor));
  }

  bool setup(USBSetup& setup) {
    if (pluggedInterface != setup.wIndex) return false;

    uint8_t request = setup.bRequest;
    uint8_t requestType = setup.bmRequestType;

    if (requestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
      if (request == WF_HID_GET_REPORT) {
        // The host reads Block Load here right after creating an effect. This
        // is the half of the handshake that tells it which block it got.
        uint8_t buf[8];
        uint8_t n = ffbHandleGetFeature(setup.wValueL, buf, sizeof(buf));
        if (n > 0) {
          USB_SendControl(0, buf, n);
          return true;
        }
        return false;
      }
      if (request == WF_HID_GET_PROTOCOL) {
        uint8_t p = 1;
        USB_SendControl(0, &p, 1);
        return true;
      }
      if (request == WF_HID_GET_IDLE) {
        uint8_t i = 0;
        USB_SendControl(0, &i, 1);
        return true;
      }
      return false;
    }

    if (requestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
      if (request == WF_HID_SET_REPORT) {
        uint16_t len = setup.wLength;
        if (len == 0) return true;
        if (len > 24) len = 24;

        uint8_t buf[24];
        USB_RecvControl(buf, len);

        // wValueH is the report type: 2 output, 3 feature. Create New Effect
        // is a feature write; some hosts also push output reports here rather
        // than over the interrupt endpoint.
        if (setup.wValueH == 3) ffbHandleSetFeature(buf, len);
        else ffbHandleOutput(buf, len);
        return true;
      }
      if (request == WF_HID_SET_PROTOCOL || request == WF_HID_SET_IDLE) return true;
      return false;
    }

    return false;
  }

private:
  uint8_t epType[2];
};

static WfHidModule g_wfHid;

static void usbBegin() {
  // The module plugs itself in from its constructor; nothing to do here.
}

static void usbSend(const WheelReport& r) {
  g_wfHid.sendReport(WF_RID_INPUT_JOYSTICK, &r, sizeof(r));
}

static void usbPoll() {
  g_wfHid.poll();
}
