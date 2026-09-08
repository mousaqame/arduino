#pragma once

// Per-board pin maps and transport selection.
//
// One sketch covers every board. What changes between them is not the wheel
// logic -- it is how the board talks to the PC:
//
//   WF_TRANSPORT_HID     the MCU has USB device hardware and enumerates as a
//                        real game controller by itself
//   WF_TRANSPORT_BRIDGE  the MCU has no USB device hardware, so it streams
//                        over serial and WheelForge feeds a vJoy device on
//                        the PC side
//
// Nothing in software can move a board from the second group to the first.
// See the Boards page in the app.

// ---------------------------------------------------------------------------
//  ATmega32u4 -- Leonardo, Micro, Pro Micro
// ---------------------------------------------------------------------------
#if defined(__AVR_ATmega32U4__)

  #define WF_BOARD_NAME        "32u4"
  #define WF_TRANSPORT_HID     1
  #define WF_USB_AVR           1

  // D0/D1 are INT2/INT3 and sit on PORTD, so the ISR reads one port.
  #define PIN_ENC_A            0
  #define PIN_ENC_B            1

  #define PIN_THROTTLE         A0
  #define PIN_BRAKE            A1
  #define PIN_CLUTCH           A2

  // Matches the EMC Lite wiring already on this rig, so the firmware drops
  // onto the existing hardware with nothing to rewire.
  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 5, 6, 7, 12 }
  #define BUTTON_ROW_PINS      { 14, 15, 16, 4 }

  // D9/D10 are Timer1 PWM; D8 picks direction.
  #define PIN_MOTOR_PWM_A      9
  #define PIN_MOTOR_PWM_B      10
  #define PIN_MOTOR_DIR        8
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       13
  #define WF_ADC_BITS          10

// ---------------------------------------------------------------------------
//  RP2040 -- Raspberry Pi Pico and friends
// ---------------------------------------------------------------------------
#elif defined(ARDUINO_ARCH_RP2040)

  #define WF_BOARD_NAME        "rp2040"
  #define WF_TRANSPORT_HID     1
  #define WF_USB_TINYUSB       1

  #define PIN_ENC_A            2
  #define PIN_ENC_B            3

  #define PIN_THROTTLE         A0      // GP26
  #define PIN_BRAKE            A1      // GP27
  #define PIN_CLUTCH           A2      // GP28

  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 6, 7, 8, 9 }
  #define BUTTON_ROW_PINS      { 10, 11, 12, 13 }

  #define PIN_MOTOR_PWM_A      14
  #define PIN_MOTOR_PWM_B      15
  #define PIN_MOTOR_DIR        16
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       25
  #define WF_ADC_BITS          12

// ---------------------------------------------------------------------------
//  ESP32-S2 / ESP32-S3 -- native USB OTG
// ---------------------------------------------------------------------------
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)

  #define WF_BOARD_NAME        "esp32sx"
  #define WF_TRANSPORT_HID     1
  #define WF_USB_ESP32         1

  #define PIN_ENC_A            4
  #define PIN_ENC_B            5

  // ADC1 only. ADC2 stops working the moment WiFi is enabled.
  #define PIN_THROTTLE         1
  #define PIN_BRAKE            2
  #define PIN_CLUTCH           3

  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 6, 7, 15, 16 }
  #define BUTTON_ROW_PINS      { 17, 18, 8, 9 }

  #define PIN_MOTOR_PWM_A      10
  #define PIN_MOTOR_PWM_B      11
  #define PIN_MOTOR_DIR        12
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       13
  #define WF_ADC_BITS          12

// ---------------------------------------------------------------------------
//  ESP32 classic (WROOM-32) -- no USB device hardware, bridge only
// ---------------------------------------------------------------------------
#elif defined(ARDUINO_ARCH_ESP32)

  #define WF_BOARD_NAME        "esp32"
  #define WF_TRANSPORT_BRIDGE  1

  #define PIN_ENC_A            18
  #define PIN_ENC_B            19

  // GPIO34/35 are input only, which is fine for a pot wiper.
  #define PIN_THROTTLE         34
  #define PIN_BRAKE            35
  #define PIN_CLUTCH           32

  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 4, 16, 17, 5 }
  #define BUTTON_ROW_PINS      { 13, 12, 14, 27 }

  #define PIN_MOTOR_PWM_A      25
  #define PIN_MOTOR_PWM_B      26
  #define PIN_MOTOR_DIR        33
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       2
  #define WF_ADC_BITS          12

// ---------------------------------------------------------------------------
//  ESP8266 -- no USB and no Bluetooth, bridge only, and only one ADC channel
// ---------------------------------------------------------------------------
#elif defined(ARDUINO_ARCH_ESP8266)

  #define WF_BOARD_NAME        "esp8266"
  #define WF_TRANSPORT_BRIDGE  1

  #define PIN_ENC_A            14     // D5
  #define PIN_ENC_B            12     // D6

  // The ESP8266 has exactly one analogue input. Throttle gets it; brake and
  // clutch need an external ADC and are reported as released.
  #define PIN_THROTTLE         A0
  #define PIN_BRAKE            -1
  #define PIN_CLUTCH           -1

  // GPIO0, 2 and 15 decide the boot mode, so a button holding one at the wrong
  // level stops the board booting. Only safe pins are used, which leaves room
  // for a 2x2 matrix rather than 4x4.
  #define BUTTON_COLS          2
  #define BUTTON_ROWS          2
  #define BUTTON_COL_PINS      { 5, 4 }     // D1, D2
  #define BUTTON_ROW_PINS      { 13, 16 }   // D7, D0

  #define PIN_MOTOR_PWM_A      -1
  #define PIN_MOTOR_PWM_B      -1
  #define PIN_MOTOR_DIR        -1
  #define WF_PWM_MAX           1023

  #define PIN_STATUS_LED       2
  #define WF_ADC_BITS          10

// ---------------------------------------------------------------------------
//  ATmega2560 -- Mega, bridge over its 16U2
// ---------------------------------------------------------------------------
#elif defined(__AVR_ATmega2560__)

  #define WF_BOARD_NAME        "mega2560"
  #define WF_TRANSPORT_BRIDGE  1

  #define PIN_ENC_A            2      // INT4
  #define PIN_ENC_B            3      // INT5

  #define PIN_THROTTLE         A0
  #define PIN_BRAKE            A1
  #define PIN_CLUTCH           A2

  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 22, 24, 26, 28 }
  #define BUTTON_ROW_PINS      { 30, 32, 34, 36 }

  #define PIN_MOTOR_PWM_A      11     // Timer1 OC1A
  #define PIN_MOTOR_PWM_B      12     // Timer1 OC1B
  #define PIN_MOTOR_DIR        4
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       13
  #define WF_ADC_BITS          10

// ---------------------------------------------------------------------------
//  ATmega328P -- Uno, Nano. Bridge only.
// ---------------------------------------------------------------------------
#elif defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)

  #define WF_BOARD_NAME        "atmega328p"
  #define WF_TRANSPORT_BRIDGE  1

  // D2/D3 are the only external interrupts on a 328P, so the encoder has to
  // live there.
  #define PIN_ENC_A            2      // INT0
  #define PIN_ENC_B            3      // INT1

  #define PIN_THROTTLE         A0
  #define PIN_BRAKE            A1
  #define PIN_CLUTCH           A2

  #define BUTTON_COLS          4
  #define BUTTON_ROWS          4
  #define BUTTON_COL_PINS      { 5, 6, 7, 8 }
  #define BUTTON_ROW_PINS      { 11, 12, A3, A4 }

  #define PIN_MOTOR_PWM_A      9      // Timer1 OC1A
  #define PIN_MOTOR_PWM_B      10     // Timer1 OC1B
  #define PIN_MOTOR_DIR        4
  #define WF_PWM_MAX           255

  #define PIN_STATUS_LED       13
  #define WF_ADC_BITS          10

#else
  #error "No WheelForge pin map for this board. Add one to boards.h."
#endif

// A board with no motor pins cannot run the force feedback tests.
#if (PIN_MOTOR_PWM_A >= 0) && (PIN_MOTOR_PWM_B >= 0)
  #define WF_HAS_MOTOR 1
#else
  #define WF_HAS_MOTOR 0
#endif
