#pragma once

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

void weatherBegin();
void weatherTick(uint32_t now);
const char *weatherCurrent();
const char *weatherDate();
const char *weatherTime();
int weatherHour();
WeatherKind weatherKind();
