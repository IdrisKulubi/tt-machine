// Copy to secrets.h (git-ignored) and fill in. Never commit secrets.h.
// The stick joins this 2.4 GHz network (the ESP32 cannot join 5 GHz) and
// connects OUT to the hub. HUB_HOST is the only place the hub address lives:
// change this one line when the network changes or the hub moves to the robot.
#pragma once

#define WIFI_SSID "your-2.4GHz-network"
#define WIFI_PASS "your-password"
#define HUB_HOST "192.168.1.50"  // laptop IP on the same network
#define HUB_PORT 8000
#define HUB_PATH "/ws/robot"
