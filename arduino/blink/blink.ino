// Blink pattern: flash N times, then pause, repeat.
// Pin 6 is the LED on the parking-sensor circuit.

const int LED_PIN      = 6;
const int BLINK_COUNT  = 5;    // flashes per burst
const int ON_MS        = 200;  // LED on time
const int OFF_MS       = 200;  // gap between flashes
const int PAUSE_MS     = 5000; // pause after each burst

void setup() {
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  for (int i = 0; i < BLINK_COUNT; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ON_MS);
    digitalWrite(LED_PIN, LOW);
    delay(OFF_MS);
  }
  delay(PAUSE_MS);
}
