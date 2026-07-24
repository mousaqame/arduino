// Copy this file to `secrets.h` and fill in your WiFi details.
//
// `secrets.h` is gitignored, so your password stays on your machine and never
// reaches GitHub. Without it the robot still works — it just starts its own
// hotspot instead of joining your network.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// Optional: the hotspot the robot falls back to when the WiFi above isn't
// reachable. Defaults to RobotBot / robot1234 if you leave these out.
// #define AP_SSID "RobotBot"
// #define AP_PASS "robot1234"
