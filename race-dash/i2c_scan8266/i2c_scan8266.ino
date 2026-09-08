// i2c_scan8266.ino - prove both panels before anything else. NodeMCU / ESP8266.
//
// Flash this FIRST. It answers the two questions that cause most of the pain:
//
//   1. Is each panel actually wired to the pins you think it is?
//   2. What address is it really at - 0x3C or 0x3D?
//
// The ESP8266 has one I2C bus, but it is bit-banged in software, so the same
// bus can be pointed at a different pair of pins whenever you like. This scans
// each pin pair in turn, exactly the way racedash8266 drives the two panels.
//
// Expected output with everything correct:
//
//   panel A on D2/D1 (GPIO4/5):   0x3C
//   panel B on D6/D5 (GPIO12/14): 0x3C
//
// "nothing found" is a wiring fault, not a code fault.
// A list of every address from 0x01 upward means SDA and SCL are swapped.

#include <Wire.h>

#define SDA_A 4    // D2
#define SCL_A 5    // D1
#define SDA_B 12   // D6
#define SCL_B 14   // D5

void scan(const char *name, int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(100000);   // scan slowly; long jumper wires are noisy

  Serial.print(name);
  Serial.print(": ");

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found) Serial.print(", ");
      Serial.print("0x");
      if (addr < 16) Serial.print('0');
      Serial.print(addr, HEX);
      found++;
    }
    yield();
  }

  if (!found)         Serial.println("nothing found");
  else if (found > 8) Serial.println("   <- too many, SDA/SCL likely swapped");
  else                Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("# i2c_scan8266 - scanning both pin pairs");
}

void loop() {
  scan("panel A on D2/D1 (GPIO4/5)  ", SDA_A, SCL_A);
  scan("panel B on D6/D5 (GPIO12/14)", SDA_B, SCL_B);
  Serial.println();
  delay(3000);
}
