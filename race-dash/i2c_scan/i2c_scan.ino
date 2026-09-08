// i2c_scan.ino — prove both I2C buses and both panels before anything else.
//
// Flash this FIRST. It answers the two questions that cause most of the pain
// in this project:
//
//   1. Is each panel actually wired to the bus you think it is?
//   2. What address is it really at — 0x3C or 0x3D?
//
// Expected output with everything correct:
//
//   bus 0 (SDA=21 SCL=22): 0x3C
//   bus 1 (SDA=25 SCL=26): 0x3D          <- or 0x3C, either is fine
//
// A bus that reports "nothing found" is a wiring fault, not a code fault.
// A bus that reports every address from 0x01 to 0x7F has SDA and SCL swapped,
// or is missing its pull-up resistors.

#include <Wire.h>

#define SDA0 21
#define SCL0 22
#define SDA1 25
#define SCL1 26

void scan(TwoWire &bus, const char *name, int sda, int scl) {
  Serial.print(name);
  Serial.print(" (SDA=");  Serial.print(sda);
  Serial.print(" SCL=");   Serial.print(scl);
  Serial.print("): ");

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      if (found) Serial.print(", ");
      Serial.print("0x");
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      found++;
    }
  }

  if (!found)       Serial.println("nothing found");
  else if (found > 8) Serial.println("   <- too many, SDA/SCL likely swapped");
  else              Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("# i2c_scan — two-bus scanner");

  Wire.begin(SDA0, SCL0, 100000);    // scan slowly; long jumper wires are noisy
  Wire1.begin(SDA1, SCL1, 100000);
}

void loop() {
  scan(Wire,  "bus 0", SDA0, SCL0);
  scan(Wire1, "bus 1", SDA1, SCL1);
  Serial.println();
  delay(3000);
}
