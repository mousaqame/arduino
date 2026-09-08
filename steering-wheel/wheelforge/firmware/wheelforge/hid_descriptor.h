#pragma once

// The HID report descriptor: a joystick, plus a PID (Physical Interface
// Device) force feedback interface.
//
// PID is what DirectInput actually talks to. A game creates an effect, the
// driver allocates a block on the device, sends parameters, and tells it to
// play -- all through the output and feature reports declared below. Get this
// descriptor wrong and the device still enumerates as a joystick; it just
// never receives a single force.
//
// Report id map. Input, Output and Feature are separate namespaces, which is
// why 5, 6 and 7 appear in two of them.
//
//   Input    1  joystick state
//            2  PID state
//   Output   1  set effect          8  download force sample
//            2  set envelope       10  effect operation
//            3  set condition      11  block free
//            4  set periodic       12  device control
//            5  set constant force 13  device gain
//            6  set ramp force     14  set custom force
//            7  custom force data
//   Feature  5  create new effect
//            6  block load          (device answers)
//            7  pool report         (device answers)

#define WF_RID_INPUT_JOYSTICK   1
#define WF_RID_INPUT_PID_STATE  2

#define WF_RID_SET_EFFECT       1
#define WF_RID_SET_ENVELOPE     2
#define WF_RID_SET_CONDITION    3
#define WF_RID_SET_PERIODIC     4
#define WF_RID_SET_CONSTANT     5
#define WF_RID_SET_RAMP         6
#define WF_RID_EFFECT_OPERATION 10
#define WF_RID_BLOCK_FREE       11
#define WF_RID_DEVICE_CONTROL   12
#define WF_RID_DEVICE_GAIN      13

#define WF_RID_CREATE_EFFECT    5
#define WF_RID_BLOCK_LOAD       6
#define WF_RID_POOL             7

// Each block costs about 40 bytes of RAM. A 32u4 has 2560 in total and the
// rest of the firmware already wants a third of it, so it gets a smaller pool.
// Games rarely hold more than a handful of effects at once.
#if defined(__AVR__)
  #define WF_MAX_EFFECTS       12
#else
  #define WF_MAX_EFFECTS       20
#endif

#if defined(WF_USB_AVR)
  #define WF_DESC_ATTR PROGMEM
#else
  #define WF_DESC_ATTR
#endif

static const uint8_t kReportDescriptor[] WF_DESC_ATTR = {

  // ==================================================================== joystick

  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x04,        // Usage (Joystick)
  0xa1, 0x01,        // Collection (Application)

  0x85, WF_RID_INPUT_JOYSTICK,

  0x05, 0x09,        //   Usage Page (Button)
  0x19, 0x01,        //   Usage Minimum (1)
  0x29, 0x18,        //   Usage Maximum (24)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x18,        //   Report Count (24)
  0x81, 0x02,        //   Input (Data,Var,Abs)

  0x05, 0x01,        //   Usage Page (Generic Desktop)
  0x09, 0x30,        //   Usage (X)
  0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
  0x26, 0xff, 0x7f,  //   Logical Maximum (32767)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x02,        //   Input (Data,Var,Abs)

  0x05, 0x02,        //   Usage Page (Simulation Controls)
  0x09, 0xc4,        //   Usage (Accelerator)
  0x09, 0xc5,        //   Usage (Brake)
  0x09, 0xc6,        //   Usage (Clutch)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xff, 0x3f,  //   Logical Maximum (16383)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x03,        //   Report Count (3)
  0x81, 0x02,        //   Input (Data,Var,Abs)

  // ================================================================= PID state

  0x05, 0x0f,        //   Usage Page (Physical Interface Device)
  0x09, 0x92,        //   Usage (PID State Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_INPUT_PID_STATE,
  0x09, 0x9f,        //     Usage (Device Paused)
  0x09, 0xa0,        //     Usage (Actuators Enabled)
  0x09, 0xa4,        //     Usage (Safety Switch)
  0x09, 0xa5,        //     Usage (Actuator Override Switch)
  0x09, 0xa6,        //     Usage (Actuator Power)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x75, 0x01,        //     Report Size (1)
  0x95, 0x05,        //     Report Count (5)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0x95, 0x03,        //     Report Count (3)
  0x81, 0x03,        //     Input (Cnst,Var,Abs)   padding to a byte
  0x09, 0x94,        //     Usage (Effect Playing)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x75, 0x01,        //     Report Size (1)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x07,        //     Report Size (7)
  0x95, 0x01,        //     Report Count (1)
  0x81, 0x02,        //     Input (Data,Var,Abs)
  0xc0,              //   End Collection

  // ================================================================ set effect

  0x09, 0x21,        //   Usage (Set Effect Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_EFFECT,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)

  0x09, 0x25,        //     Usage (Effect Type)
  0xa1, 0x02,        //     Collection (Logical)
  0x09, 0x26,        //       Usage (ET Constant Force)
  0x09, 0x27,        //       Usage (ET Ramp)
  0x09, 0x30,        //       Usage (ET Square)
  0x09, 0x31,        //       Usage (ET Sine)
  0x09, 0x32,        //       Usage (ET Triangle)
  0x09, 0x33,        //       Usage (ET Sawtooth Up)
  0x09, 0x34,        //       Usage (ET Sawtooth Down)
  0x09, 0x40,        //       Usage (ET Spring)
  0x09, 0x41,        //       Usage (ET Damper)
  0x09, 0x42,        //       Usage (ET Inertia)
  0x09, 0x43,        //       Usage (ET Friction)
  0x15, 0x01,        //       Logical Minimum (1)
  0x25, 0x0b,        //       Logical Maximum (11)
  0x75, 0x08,        //       Report Size (8)
  0x95, 0x01,        //       Report Count (1)
  0x91, 0x00,        //       Output (Data,Ary,Abs)
  0xc0,              //     End Collection

  0x09, 0x50,        //     Usage (Duration)
  0x09, 0x54,        //     Usage (Trigger Repeat Interval)
  0x09, 0x51,        //     Usage (Sample Period)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x7f,  //     Logical Maximum (32767)
  0x66, 0x03, 0x10,  //     Unit (Eng Lin: Time)
  0x55, 0xfd,        //     Unit Exponent (-3)  milliseconds
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x03,        //     Report Count (3)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x55, 0x00,        //     Unit Exponent (0)
  0x66, 0x00, 0x00,  //     Unit (None)

  0x09, 0x52,        //     Usage (Gain)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)

  0x09, 0x53,        //     Usage (Trigger Button)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x18,        //     Logical Maximum (24)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)

  0x09, 0x55,        //     Usage (Axes Enable)
  0xa1, 0x02,        //     Collection (Logical)
  0x05, 0x01,        //       Usage Page (Generic Desktop)
  0x09, 0x30,        //       Usage (X)
  0x09, 0x31,        //       Usage (Y)
  0x15, 0x00,        //       Logical Minimum (0)
  0x25, 0x01,        //       Logical Maximum (1)
  0x75, 0x01,        //       Report Size (1)
  0x95, 0x02,        //       Report Count (2)
  0x91, 0x02,        //       Output (Data,Var,Abs)
  0xc0,              //     End Collection
  0x05, 0x0f,        //     Usage Page (PID)
  0x09, 0x56,        //     Usage (Direction Enable)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x95, 0x05,        //     Report Count (5)
  0x91, 0x03,        //     Output (Cnst,Var,Abs)   padding

  0x09, 0x57,        //     Usage (Direction)
  0xa1, 0x02,        //     Collection (Logical)
  0x0b, 0x01, 0x00, 0x0a, 0x00,   //   Usage (Ordinals: Instance 1)
  0x0b, 0x02, 0x00, 0x0a, 0x00,   //   Usage (Ordinals: Instance 2)
  0x66, 0x14, 0x00,  //       Unit (Eng Rot: Degrees)
  0x55, 0xfe,        //       Unit Exponent (-2)
  0x15, 0x00,        //       Logical Minimum (0)
  0x26, 0xff, 0x00,  //       Logical Maximum (255)
  0x35, 0x00,        //       Physical Minimum (0)
  0x47, 0xa0, 0x8c, 0x00, 0x00,   //   Physical Maximum (35999)
  0x75, 0x08,        //       Report Size (8)
  0x95, 0x02,        //       Report Count (2)
  0x91, 0x02,        //       Output (Data,Var,Abs)
  0x55, 0x00,        //       Unit Exponent (0)
  0x66, 0x00, 0x00,  //       Unit (None)
  0xc0,              //     End Collection
  0xc0,              //   End Collection

  // ============================================================== set envelope

  0x09, 0x5a,        //   Usage (Set Envelope Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_ENVELOPE,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x5b,        //     Usage (Attack Level)
  0x09, 0x5d,        //     Usage (Fade Level)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x95, 0x02,        //     Report Count (2)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x5c,        //     Usage (Attack Time)
  0x09, 0x5e,        //     Usage (Fade Time)
  0x26, 0xff, 0x7f,  //     Logical Maximum (32767)
  0x66, 0x03, 0x10,  //     Unit (Time)
  0x55, 0xfd,        //     Unit Exponent (-3)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x02,        //     Report Count (2)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x55, 0x00,        //     Unit Exponent (0)
  0x66, 0x00, 0x00,  //     Unit (None)
  0xc0,              //   End Collection

  // ============================================================= set condition

  0x09, 0x5f,        //   Usage (Set Condition Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_CONDITION,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x23,        //     Usage (Parameter Block Offset)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x75, 0x04,        //     Report Size (4)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x75, 0x04,        //     Report Size (4)
  0x91, 0x03,        //     Output (Cnst,Var,Abs)   padding
  0x09, 0x60,        //     Usage (CP Offset)
  0x09, 0x61,        //     Usage (Positive Coefficient)
  0x09, 0x62,        //     Usage (Negative Coefficient)
  0x15, 0x80,        //     Logical Minimum (-128)
  0x25, 0x7f,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x03,        //     Report Count (3)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x63,        //     Usage (Positive Saturation)
  0x09, 0x64,        //     Usage (Negative Saturation)
  0x09, 0x65,        //     Usage (Dead Band)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x95, 0x03,        //     Report Count (3)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // ============================================================== set periodic

  0x09, 0x6e,        //   Usage (Set Periodic Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_PERIODIC,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x70,        //     Usage (Magnitude)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x6f,        //     Usage (Offset)
  0x15, 0x80,        //     Logical Minimum (-128)
  0x25, 0x7f,        //     Logical Maximum (127)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x71,        //     Usage (Phase)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x72,        //     Usage (Period)
  0x26, 0xff, 0x7f,  //     Logical Maximum (32767)
  0x66, 0x03, 0x10,  //     Unit (Time)
  0x55, 0xfd,        //     Unit Exponent (-3)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x55, 0x00,        //     Unit Exponent (0)
  0x66, 0x00, 0x00,  //     Unit (None)
  0xc0,              //   End Collection

  // ======================================================== set constant force

  0x09, 0x73,        //   Usage (Set Constant Force Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_CONSTANT,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x70,        //     Usage (Magnitude)
  0x16, 0x01, 0xff,  //     Logical Minimum (-255)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // ============================================================ set ramp force

  0x09, 0x74,        //   Usage (Set Ramp Force Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_SET_RAMP,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x75,        //     Usage (Ramp Start)
  0x09, 0x76,        //     Usage (Ramp End)
  0x15, 0x80,        //     Logical Minimum (-128)
  0x25, 0x7f,        //     Logical Maximum (127)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x02,        //     Report Count (2)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // =========================================================== effect operation

  0x09, 0x77,        //   Usage (Effect Operation Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_EFFECT_OPERATION,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0x09, 0x78,        //     Usage (Effect Operation)
  0xa1, 0x02,        //     Collection (Logical)
  0x09, 0x79,        //       Usage (Op Effect Start)
  0x09, 0x7a,        //       Usage (Op Effect Start Solo)
  0x09, 0x7b,        //       Usage (Op Effect Stop)
  0x15, 0x01,        //       Logical Minimum (1)
  0x25, 0x03,        //       Logical Maximum (3)
  0x75, 0x08,        //       Report Size (8)
  0x95, 0x01,        //       Report Count (1)
  0x91, 0x00,        //       Output (Data,Ary,Abs)
  0xc0,              //     End Collection
  0x09, 0x7c,        //     Usage (Loop Count)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // ================================================================ block free

  0x09, 0x90,        //   Usage (PID Block Free Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_BLOCK_FREE,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // ============================================================ device control

  0x09, 0x96,        //   Usage (PID Device Control)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_DEVICE_CONTROL,
  0x09, 0x97,        //     Usage (DC Enable Actuators)
  0x09, 0x98,        //     Usage (DC Disable Actuators)
  0x09, 0x99,        //     Usage (DC Stop All Effects)
  0x09, 0x9a,        //     Usage (DC Device Reset)
  0x09, 0x9b,        //     Usage (DC Device Pause)
  0x09, 0x9c,        //     Usage (DC Device Continue)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, 0x06,        //     Logical Maximum (6)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x00,        //     Output (Data,Ary,Abs)
  0xc0,              //   End Collection

  // =============================================================== device gain

  0x09, 0x7d,        //   Usage (Device Gain Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_DEVICE_GAIN,
  0x09, 0x7e,        //     Usage (Device Gain)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0x91, 0x02,        //     Output (Data,Var,Abs)
  0xc0,              //   End Collection

  // ======================================================== create new effect

  0x09, 0xab,        //   Usage (Create New Effect Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_CREATE_EFFECT,
  0x09, 0x25,        //     Usage (Effect Type)
  0xa1, 0x02,        //     Collection (Logical)
  0x09, 0x26,        //       Usage (ET Constant Force)
  0x09, 0x27,        //       Usage (ET Ramp)
  0x09, 0x30,        //       Usage (ET Square)
  0x09, 0x31,        //       Usage (ET Sine)
  0x09, 0x32,        //       Usage (ET Triangle)
  0x09, 0x33,        //       Usage (ET Sawtooth Up)
  0x09, 0x34,        //       Usage (ET Sawtooth Down)
  0x09, 0x40,        //       Usage (ET Spring)
  0x09, 0x41,        //       Usage (ET Damper)
  0x09, 0x42,        //       Usage (ET Inertia)
  0x09, 0x43,        //       Usage (ET Friction)
  0x15, 0x01,        //       Logical Minimum (1)
  0x25, 0x0b,        //       Logical Maximum (11)
  0x75, 0x08,        //       Report Size (8)
  0x95, 0x01,        //       Report Count (1)
  0xb1, 0x00,        //       Feature (Data,Ary,Abs)
  0xc0,              //     End Collection
  0x05, 0x01,        //     Usage Page (Generic Desktop)
  0x09, 0x3b,        //     Usage (Byte Count)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x01,  //     Logical Maximum (511)
  0x75, 0x0a,        //     Report Size (10)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0x75, 0x06,        //     Report Size (6)
  0xb1, 0x03,        //     Feature (Cnst,Var,Abs)   padding
  0xc0,              //   End Collection

  // ================================================================ block load

  0x05, 0x0f,        //   Usage Page (PID)
  0x09, 0x89,        //   Usage (PID Block Load Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_BLOCK_LOAD,
  0x09, 0x22,        //     Usage (Effect Block Index)
  0x15, 0x01,        //     Logical Minimum (1)
  0x25, WF_MAX_EFFECTS,
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0x09, 0x8b,        //     Usage (Block Load Status)
  0xa1, 0x02,        //     Collection (Logical)
  0x09, 0x8c,        //       Usage (Block Load Success)
  0x09, 0x8d,        //       Usage (Block Load Full)
  0x09, 0x8e,        //       Usage (Block Load Error)
  0x15, 0x01,        //       Logical Minimum (1)
  0x25, 0x03,        //       Logical Maximum (3)
  0x75, 0x08,        //       Report Size (8)
  0x95, 0x01,        //       Report Count (1)
  0xb1, 0x00,        //       Feature (Data,Ary,Abs)
  0xc0,              //     End Collection
  0x09, 0xac,        //     Usage (RAM Pool Available)
  0x15, 0x00,        //     Logical Minimum (0)
  0x27, 0xff, 0xff, 0x00, 0x00,   //   Logical Maximum (65535)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0xc0,              //   End Collection

  // =============================================================== pool report

  0x09, 0x7f,        //   Usage (PID Pool Report)
  0xa1, 0x02,        //   Collection (Logical)
  0x85, WF_RID_POOL,
  0x09, 0x80,        //     Usage (RAM Pool Size)
  0x15, 0x00,        //     Logical Minimum (0)
  0x27, 0xff, 0xff, 0x00, 0x00,   //   Logical Maximum (65535)
  0x75, 0x10,        //     Report Size (16)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0x09, 0x83,        //     Usage (Simultaneous Effects Max)
  0x15, 0x00,        //     Logical Minimum (0)
  0x26, 0xff, 0x00,  //     Logical Maximum (255)
  0x75, 0x08,        //     Report Size (8)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0x09, 0xa9,        //     Usage (Device Managed Pool)
  0x09, 0xaa,        //     Usage (Shared Parameter Blocks)
  0x15, 0x00,        //     Logical Minimum (0)
  0x25, 0x01,        //     Logical Maximum (1)
  0x75, 0x01,        //     Report Size (1)
  0x95, 0x02,        //     Report Count (2)
  0xb1, 0x02,        //     Feature (Data,Var,Abs)
  0x75, 0x06,        //     Report Size (6)
  0x95, 0x01,        //     Report Count (1)
  0xb1, 0x03,        //     Feature (Cnst,Var,Abs)   padding
  0xc0,              //   End Collection

  0xc0               // End Collection
};
