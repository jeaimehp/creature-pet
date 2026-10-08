#pragma once

#include <Arduino.h>
#include <stdint.h>

enum WeatherKind : uint8_t {
  kWeatherNone = 0,
  kWeatherClear,
  kWeatherCloudy,
  kWeatherFog,
  kWeatherDrizzle,
  kWeatherRain,
  kWeatherShowers,
  kWeatherSnow,
  kWeatherStorm
};

enum WifiState : uint8_t { kWifiUnset, kWifiConnecting, kWifiConnected };

void weatherBegin();
// Saves credentials to flash (NVS) and reconnects. They override secrets.h.
void weatherSetWifi(const char *ssid, const char *pass);
const char *weatherSsid();
WifiState weatherWifiState();
String weatherIp();
// Stops connection attempts so a network scan can run.
void weatherPauseWifi();
void weatherResumeWifi();
void weatherTick(uint32_t now);
const char *weatherCurrent();
const char *weatherDate();
const char *weatherTime();
int weatherHour();
WeatherKind weatherKind();
