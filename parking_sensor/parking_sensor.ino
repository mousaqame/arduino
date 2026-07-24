#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pins
const int trigPin = 9;
const int echoPin = 10;
const int ledPin = 6;
const int buzzerPin = 7;

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  digitalWrite(ledPin, LOW);
  noTone(buzzerPin);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(15, 8);
  display.println("READY");
  display.display();

  delay(2000);
}

void loop() {

  // Measure distance
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);

  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);

  float distance;

  if (duration == 0) {
    distance = 999;
  } else {
    distance = duration * 0.0343 / 2;
  }

  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Distance: ");
  display.print(distance, 0);
  display.println(" cm");

  display.setTextSize(2);
  display.setCursor(0, 16);

  digitalWrite(ledPin, LOW);
  noTone(buzzerPin);

  if (distance > 100) {
    display.print("FAR");
  }

  else if (distance > 50) {
    display.print("CLOSE");
  }

  else if (distance > 20) {
    display.print("GOOD");
    digitalWrite(ledPin, HIGH);

    tone(buzzerPin, 1000);
    delay(100);
    noTone(buzzerPin);
    delay(400);
  }

  else if (distance > 10) {
    display.print("V.CLOSE");
    digitalWrite(ledPin, HIGH);

    tone(buzzerPin, 1500);
    delay(100);
    noTone(buzzerPin);
    delay(150);
  }

  else {
    display.print("STOP!");
    digitalWrite(ledPin, HIGH);
    tone(buzzerPin, 2000);
  }

  display.display();

  delay(50);
}
