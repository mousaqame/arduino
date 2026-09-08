using System;
using System.Collections.Generic;

namespace WheelForge.Core
{
    // How a board gets to be a game controller at all.
    public enum UsbPath
    {
        // The main MCU has USB device hardware. It enumerates as a real game
        // controller by itself.
        Native,

        // No USB on the main MCU, but the board carries an ATmega16U2 as its
        // USB bridge. Reflashing that chip would make it a real HID device.
        // Not written yet -- these boards ship in bridge mode today.
        Via16u2,

        // No USB device hardware anywhere. The board streams over serial and
        // WheelForge feeds a vJoy device on the PC.
        Bridged,

        // Cannot be a game controller by any route.
        None
    }

    // What the shipped firmware image actually does, as opposed to what the
    // board could theoretically do.
    public enum LinkMode
    {
        Hid,        // enumerates as a USB game controller on its own
        Bridge,     // streams over serial, WheelForge feeds vJoy
        None
    }

    public enum FlashTool
    {
        Avrdude,        // .hex over the normal serial bootloader
        AvrdudeAvr109,  // .hex, Leonardo style: 1200 baud touch, then the bootloader port
        Esptool,        // .bin over the ESP serial bootloader
        Uf2Copy,        // .uf2 copied onto the mass storage bootloader drive
        None
    }

    public class BoardProfile
    {
        public string Id;
        public string Name;
        public string Mcu;
        public UsbPath Usb;
        public LinkMode Mode;
        public FlashTool Tool;
        public string ImageExtension;
        public string ImageName;

        public string AvrdudePart;
        public string AvrdudeProgrammer;
        public int UploadBaud;

        // Flashed at this offset by esptool. 0x10000 is where the application
        // image goes on an ESP32; the ESP8266 puts it at 0.
        public int FlashOffset;

        // An ESP32 needs three images to boot from blank, not one: the second
        // stage bootloader, the partition table at 0x8000, and the app. The
        // bootloader offset moves between chip families.
        public int BootloaderOffset;
        public string EsptoolChip;

        public bool SupportsFfb;
        public string Note;

        // The pinout the shipped image expects, straight from firmware/boards.h.
        public string[] Wiring = new string[0];

        // How to get this board into a state where it will accept a flash.
        // Every family does it differently, and getting it wrong just gives an
        // unhelpful timeout from the flashing tool.
        public string[] BootMode = new string[0];

        public bool HasImage
        {
            get { return !string.IsNullOrEmpty(ImageName); }
        }

        public bool IsSupported
        {
            get { return Mode != LinkMode.None; }
        }

        public string Verdict
        {
            get
            {
                switch (Mode)
                {
                    case LinkMode.Hid:
                        return "USB game controller";
                    case LinkMode.Bridge:
                        return "Bridged through vJoy";
                    default:
                        return "Not usable as a controller";
                }
            }
        }

        // The short version of what you give up, if anything.
        public string Caveat
        {
            get
            {
                switch (Mode)
                {
                    case LinkMode.Hid:
                        return SupportsFfb
                            ? "Works standalone. Force feedback in games."
                            : "Works standalone.";
                    case LinkMode.Bridge:
                        return "Needs vJoy and WheelForge running. Motor test only, "
                             + "no force feedback in games.";
                    default:
                        return "";
                }
            }
        }
    }

    public static class BoardCatalog
    {
        private static List<BoardProfile> _all;

        public static List<BoardProfile> All
        {
            get
            {
                if (_all == null) _all = Build();
                return _all;
            }
        }

        public static BoardProfile ById(string id)
        {
            foreach (BoardProfile b in All)
                if (b.Id == id) return b;
            return null;
        }

        public static List<BoardProfile> Flashable()
        {
            List<BoardProfile> list = new List<BoardProfile>();
            foreach (BoardProfile b in All)
                if (b.IsSupported) list.Add(b);
            return list;
        }

        private static readonly string[] AutoReset = new string[]
        {
            "Nothing to press. Just plug the board in.",
            "",
            "avrdude pulls the reset line through the serial port, so the board drops",
            "into its bootloader on its own. Pick the port and press Flash."
        };

        private static readonly string[] Avr109Reset = new string[]
        {
            "Normally nothing to press. Just plug the board in.",
            "",
            "WheelForge opens the port at 1200 baud, which makes a 32u4 reboot into its",
            "bootloader. That bootloader appears as a SECOND, different COM port for a",
            "few seconds, and it is the one that gets flashed -- so do not be alarmed",
            "when the port number changes part way through.",
            "",
            "If it times out (common on a Pro Micro, where the window is very short):",
            "  1. Press Flash",
            "  2. Immediately tap the RESET button twice, quickly",
            "A double tap holds the bootloader open for about eight seconds."
        };

        private static BoardProfile Avr(string id, string name, string mcu, LinkMode mode,
                                        string image, string part, string programmer, int baud,
                                        bool avr109, bool ffb, string note, string[] wiring)
        {
            BoardProfile b = new BoardProfile();
            b.Id = id; b.Name = name; b.Mcu = mcu; b.Mode = mode;
            b.Usb = mode == LinkMode.Hid ? UsbPath.Native : UsbPath.Via16u2;
            b.Tool = avr109 ? FlashTool.AvrdudeAvr109 : FlashTool.Avrdude;
            b.ImageExtension = ".hex";
            b.ImageName = image;
            b.AvrdudePart = part;
            b.AvrdudeProgrammer = programmer;
            b.UploadBaud = baud;
            b.SupportsFfb = ffb;
            b.Note = note;
            b.Wiring = wiring;
            return b;
        }

        private static List<BoardProfile> Build()
        {
            List<BoardProfile> list = new List<BoardProfile>();
            BoardProfile b;

            // ---- native USB ---------------------------------------------

            list.Add(Avr("leonardo", "Arduino Leonardo", "ATmega32u4", LinkMode.Hid,
                "wheelforge-leonardo.hex", "atmega32u4", "avr109", 57600, true, true,
                "Native USB on the main chip, and what EMC Lite runs on. Flashing resets "
              + "it with a 1200 baud touch, then writes the bootloader port that appears "
              + "a moment later.",
                new string[] {
                    "Encoder A / B      D0, D1   (INT2 / INT3, pullups on)",
                    "Pedals             A0 throttle, A1 brake, A2 clutch",
                    "Button columns     D5, D6, D7, D12   driven low one at a time",
                    "Button rows        D14, D15, D16, D4  INPUT_PULLUP",
                    "Motor PWM          D9, D10   (Timer1)",
                    "Motor direction    D8",
                    "Status LED         D13   driven, leave it free",
                    "",
                    "D14/D15/D16 are on the 2x6 ICSP header, not the digital strip.",
                    "Each button goes between a column pin and a row pin -- never to GND.",
                    "Two or more buttons at once needs a 1N4148 per button, band to the row."
                }));
            list[list.Count - 1].BootMode = Avr109Reset;

            list.Add(Avr("micro", "Arduino Micro / Pro Micro", "ATmega32u4", LinkMode.Hid,
                "wheelforge-micro.hex", "atmega32u4", "avr109", 57600, true, true,
                "Same chip as the Leonardo in a smaller board. On a Pro Micro the "
              + "bootloader window is short, so tap reset twice and flash straight away.",
                new string[] {
                    "Encoder A / B      D0, D1",
                    "Pedals             A0 throttle, A1 brake, A2 clutch",
                    "Button columns     D5, D6, D7, D12",
                    "Button rows        D14, D15, D16, D4",
                    "Motor PWM          D9, D10",
                    "Motor direction    D8",
                    "Status LED         D13"
                }));
            list[list.Count - 1].BootMode = Avr109Reset;

            b = new BoardProfile();
            b.Id = "pico"; b.Name = "Raspberry Pi Pico / RP2040"; b.Mcu = "RP2040";
            b.Usb = UsbPath.Native; b.Mode = LinkMode.Hid; b.Tool = FlashTool.Uf2Copy;
            b.ImageExtension = ".uf2"; b.ImageName = "wheelforge-pico.uf2";
            b.SupportsFfb = true;
            b.Note = "The best target of the lot: native USB, far more pins and speed than a "
                   + "32u4, and no flashing tool at all. Hold BOOTSEL, plug in, and the board "
                   + "mounts as a drive to copy the image onto.";
            b.Wiring = new string[] {
                    "Encoder A / B      GP2, GP3",
                    "Pedals             GP26 throttle, GP27 brake, GP28 clutch  (ADC0-2)",
                    "Button columns     GP6, GP7, GP8, GP9",
                    "Button rows        GP10, GP11, GP12, GP13",
                    "Motor PWM          GP14, GP15",
                    "Motor direction    GP16",
                    "Status LED         GP25   (the on-board LED)",
                    "",
                    "The Pico is 3.3 V. A 5 V encoder or motor driver needs a level shifter,",
                    "and its ADC pins must never see more than 3.3 V -- divide the pot supply."
                };
            b.BootMode = new string[] {
                    "Hold BOOTSEL, then plug the USB cable in. Release BOOTSEL.",
                    "",
                    "The board appears as a removable drive called RPI-RP2. WheelForge finds",
                    "it by the INFO_UF2.TXT file on it, so there is no COM port to pick.",
                    "",
                    "Press Flash and the image is copied across. The board reboots the moment",
                    "the copy lands, so Windows may say the drive was removed unexpectedly.",
                    "That is normal, not an error.",
                    "",
                    "Already running firmware? Unplug, hold BOOTSEL, plug back in."
                };
            list.Add(b);

            b = new BoardProfile();
            b.Id = "esp32s3"; b.Name = "ESP32-S3"; b.Mcu = "ESP32-S3";
            b.Usb = UsbPath.Native; b.Mode = LinkMode.Hid; b.Tool = FlashTool.Esptool;
            b.ImageExtension = ".bin"; b.ImageName = "wheelforge-esp32s3.bin";
            b.FlashOffset = 0x10000; b.BootloaderOffset = 0x0; b.EsptoolChip = "esp32s3";
            b.SupportsFfb = true;
            b.BootMode = new string[] {
                    "Most ESP32-S3 boards reset themselves and need nothing pressed.",
                    "",
                    "If esptool says Failed to connect:",
                    "  1. Hold the BOOT (or IO0) button",
                    "  2. Tap RESET (or EN) while still holding BOOT",
                    "  3. Release BOOT",
                    "The board is now in download mode. Press Flash.",
                    "",
                    "Flash over the USB-TO-SERIAL socket, not the native USB one. A board",
                    "with two sockets has one of each, and only one of them can flash."
                };
            b.Note = "Has native USB OTG, unlike the classic ESP32, so it is a real controller "
                   + "on its own. Flash over the USB-to-serial port, not the native USB port.";
            b.Wiring = new string[] {
                    "Encoder A / B      GPIO4, GPIO5",
                    "Pedals             GPIO1 throttle, GPIO2 brake, GPIO3 clutch  (ADC1)",
                    "Button columns     GPIO6, GPIO7, GPIO15, GPIO16",
                    "Button rows        GPIO17, GPIO18, GPIO8, GPIO9",
                    "Motor PWM          GPIO10, GPIO11",
                    "Motor direction    GPIO12",
                    "Status LED         GPIO13",
                    "",
                    "ADC1 only -- ADC2 stops working the moment WiFi is enabled.",
                    "3.3 V logic: level shift anything 5 V."
                };
            list.Add(b);

            b = new BoardProfile();
            b.Id = "esp32s2"; b.Name = "ESP32-S2"; b.Mcu = "ESP32-S2";
            b.Usb = UsbPath.Native; b.Mode = LinkMode.Hid; b.Tool = FlashTool.Esptool;
            b.ImageExtension = ".bin"; b.ImageName = "wheelforge-esp32s2.bin";
            b.FlashOffset = 0x10000; b.BootloaderOffset = 0x0; b.EsptoolChip = "esp32s2";
            b.SupportsFfb = true;
            b.BootMode = new string[] {
                    "Most boards reset themselves. If esptool says Failed to connect:",
                    "  1. Hold BOOT (IO0)",
                    "  2. Tap RESET (EN)",
                    "  3. Release BOOT",
                    "",
                    "Flash over the USB-to-serial socket, not the native USB one."
                };
            b.Note = "Native USB OTG, same as the S3 with one core instead of two.";
            b.Wiring = new string[] {
                    "Encoder A / B      GPIO4, GPIO5",
                    "Pedals             GPIO1 throttle, GPIO2 brake, GPIO3 clutch",
                    "Button columns     GPIO6, GPIO7, GPIO15, GPIO16",
                    "Button rows        GPIO17, GPIO18, GPIO8, GPIO9",
                    "Motor PWM          GPIO10, GPIO11",
                    "Motor direction    GPIO12",
                    "Status LED         GPIO13"
                };
            list.Add(b);

            // ---- bridged -------------------------------------------------

            list.Add(Avr("uno", "Arduino Uno R3", "ATmega328P", LinkMode.Bridge,
                "wheelforge-uno.hex", "atmega328p", "arduino", 115200, false, true,
                "The 328P has no USB, so it streams over serial and WheelForge feeds vJoy. "
              + "Everything works except in-game force feedback -- the motor test still does. "
              + "Making it a real HID device means reflashing the ATmega16U2 beside the USB "
              + "socket over DFU, which is not written yet.",
                new string[] {
                    "Encoder A / B      D2, D3   (INT0 / INT1 -- the only interrupt pins)",
                    "Pedals             A0 throttle, A1 brake, A2 clutch",
                    "Button columns     D5, D6, D7, D8",
                    "Button rows        D11, D12, A3, A4",
                    "Motor PWM          D9, D10   (Timer1)",
                    "Motor direction    D4",
                    "Status LED         D13",
                    "",
                    "D0/D1 are the USB serial link and must stay clear.",
                    "A5 is spare."
                }));
            list[list.Count - 1].BootMode = AutoReset;

            list.Add(Avr("mega2560", "Arduino Mega 2560", "ATmega2560", LinkMode.Bridge,
                "wheelforge-mega2560.hex", "atmega2560", "wiring", 115200, false, true,
                "Same story as the Uno -- bridged over serial -- but with far more pins, so "
              + "a bigger button matrix is easy to grow into.",
                new string[] {
                    "Encoder A / B      D2, D3   (INT4 / INT5)",
                    "Pedals             A0 throttle, A1 brake, A2 clutch",
                    "Button columns     D22, D24, D26, D28",
                    "Button rows        D30, D32, D34, D36",
                    "Motor PWM          D11, D12   (Timer1)",
                    "Motor direction    D4",
                    "Status LED         D13"
                }));
            list[list.Count - 1].BootMode = AutoReset;

            list.Add(Avr("nano", "Arduino Nano", "ATmega328P + CH340/FT232", LinkMode.Bridge,
                "wheelforge-nano.hex", "atmega328p", "arduino", 115200, false, true,
                "Identical to the Uno inside. Its USB chip is a fixed-function serial "
              + "converter with no firmware to replace, so bridge mode is the only route -- "
              + "there is no 16U2 here to reflash.",
                new string[] {
                    "Encoder A / B      D2, D3",
                    "Pedals             A0 throttle, A1 brake, A2 clutch",
                    "Button columns     D5, D6, D7, D8",
                    "Button rows        D11, D12, A3, A4",
                    "Motor PWM          D9, D10",
                    "Motor direction    D4",
                    "Status LED         D13",
                    "",
                    "Old bootloader clones need 57600 baud instead of 115200."
                }));
            list[list.Count - 1].BootMode = new string[] {
                    "Nothing to press. Plug it in and press Flash.",
                    "",
                    "If avrdude times out with not in sync, this is an older clone carrying",
                    "the pre-2018 bootloader, which runs at 57600 baud rather than 115200.",
                    "",
                    "No COM port appearing at all means the CH340 driver is missing."
                };

            b = new BoardProfile();
            b.Id = "esp32"; b.Name = "ESP32 (WROOM-32, classic)"; b.Mcu = "ESP32";
            b.Usb = UsbPath.Bridged; b.Mode = LinkMode.Bridge; b.Tool = FlashTool.Esptool;
            b.ImageExtension = ".bin"; b.ImageName = "wheelforge-esp32.bin";
            b.FlashOffset = 0x10000; b.BootloaderOffset = 0x1000; b.EsptoolChip = "esp32";
            b.SupportsFfb = true;
            b.Note = "No USB device hardware, so it bridges over serial. Plenty of pins and a "
                   + "fast core, and the motor test works.";
            b.Wiring = new string[] {
                    "Encoder A / B      GPIO18, GPIO19",
                    "Pedals             GPIO34 throttle, GPIO35 brake, GPIO32 clutch",
                    "Button columns     GPIO4, GPIO16, GPIO17, GPIO5",
                    "Button rows        GPIO13, GPIO12, GPIO14, GPIO27",
                    "Motor PWM          GPIO25, GPIO26",
                    "Motor direction    GPIO33",
                    "Status LED         GPIO2",
                    "",
                    "GPIO34 and 35 are input only, which suits a pot wiper fine.",
                    "ADC1 pins only -- ADC2 dies when WiFi comes up.",
                    "GPIO12 must be low at boot, so do not hold that button while resetting."
                };
            b.BootMode = new string[] {
                    "Most ESP32 boards reset themselves and need nothing pressed.",
                    "",
                    "If esptool says Wrong boot mode detected:",
                    "  1. Hold the BOOT (or IO0) button",
                    "  2. Press Flash in WheelForge",
                    "  3. Keep holding until Connecting turns into Writing",
                    "",
                    "A board with no BOOT button needs GPIO0 pulled down to GND instead."
                };
            list.Add(b);

            b = new BoardProfile();
            b.Id = "esp8266"; b.Name = "ESP8266 / NodeMCU"; b.Mcu = "ESP8266";
            b.Usb = UsbPath.Bridged; b.Mode = LinkMode.Bridge; b.Tool = FlashTool.Esptool;
            b.ImageExtension = ".bin"; b.ImageName = "wheelforge-esp8266.bin";
            b.FlashOffset = 0x0; b.BootloaderOffset = -1; b.EsptoolChip = "esp8266";
            b.SupportsFfb = false;
            b.Note = "The most limited board here, but it does work as a bridge. One analogue "
                   + "input means one pedal; a 2x2 button matrix, because most of its other "
                   + "pins decide the boot mode. No motor driver pins are assigned, so there "
                   + "is no force feedback test.";
            b.Wiring = new string[] {
                    "Encoder A / B      D5 (GPIO14), D6 (GPIO12)",
                    "Pedal              A0 throttle only -- the chip has one ADC channel",
                    "Button columns     D1 (GPIO5), D2 (GPIO4)",
                    "Button rows        D7 (GPIO13), D0 (GPIO16)",
                    "Status LED         GPIO2",
                    "",
                    "Brake and clutch need an external ADC such as an ADS1115.",
                    "GPIO0, 2 and 15 set the boot mode -- a button holding one at the wrong",
                    "level stops the board booting, which is why the matrix is only 2x2.",
                    "No motor pins are assigned: FFB is not available on this board."
                };
            b.BootMode = new string[] {
                    "A NodeMCU or Wemos D1 resets itself. Nothing to press.",
                    "",
                    "A bare ESP-12 module needs GPIO0 held to GND while it powers up.",
                    "",
                    "If esptool cannot connect at all, the CH340 or CP2102 driver is missing."
                };
            list.Add(b);

            return list;
        }
    }
}
