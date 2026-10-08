#include "weather.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static constexpr double kLat = 28.60;
static constexpr double kLon = -81.26;
static constexpr uint32_t kRefreshMs = 5UL * 60UL * 1000UL;
static constexpr uint32_t kRetryMs = 30UL * 1000UL;

static char gNow[56] = "wifi ...";
static char gDate[24] = "";
static char gTime[16] = "--:--";
static int gHour = -1;
static WeatherKind gKind = kWeatherNone;
static bool gHaveWeather = false;
static uint32_t gLastFetch = 0;
static uint32_t gLastWifiTry = 0;

const char *weatherCurrent() { return gNow; }
const char *weatherDate() { return gDate; }
const char *weatherTime() { return gTime; }
int weatherHour() { return gHour; }
WeatherKind weatherKind() { return gKind; }

static WeatherKind kindFromCode(int code) {
  if (code < 0) return kWeatherNone;
  if (code == 0) return kWeatherClear;
  if (code <= 3) return kWeatherCloudy;
  if (code <= 48) return kWeatherFog;
  if (code <= 57) return kWeatherDrizzle;
  if (code <= 67) return kWeatherRain;
  if (code <= 77) return kWeatherSnow;
  if (code <= 82) return kWeatherShowers;
  if (code <= 86) return kWeatherSnow;
  if (code >= 95) return kWeatherStorm;
  return kWeatherCloudy;
}

static const char *conditionName(int code) {
  if (code == 0) return "clear";
  if (code <= 3) return "cloudy";
  if (code <= 48) return "fog";
  if (code <= 57) return "drizzle";
  if (code <= 67) return "rain";
  if (code <= 77) return "snow";
  if (code <= 82) return "showers";
  if (code <= 86) return "snow";
  if (code >= 95) return "storms";
  return "weather";
}

static int roundTemp(float v) { return (int)(v + (v >= 0.0f ? 0.5f : -0.5f)); }

static bool findNumberAfter(const char *json, const char *key, float *out) {
  const char *p = strstr(json, key);
  if (!p) return false;
  p = strchr(p, ':');
  if (!p) return false;
  *out = strtof(p + 1, nullptr);
  return true;
}

static void applyJson(const char *json) {
  const char *current = strstr(json, "\"current\":");
  if (current == nullptr) current = json;
  float temp = 0;
  float code = -1;
  if (!findNumberAfter(current, "\"temperature_2m\"", &temp)) return;
  findNumberAfter(current, "\"weather_code\"", &code);

  gKind = kindFromCode((int)code);
  snprintf(gNow, sizeof(gNow), "%dF  %s", roundTemp(temp), conditionName((int)code));
  gHaveWeather = true;
  Serial.printf("weather %s\n", gNow);
}

static bool fetchWeather() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
           "&current=temperature_2m,weather_code"
           "&temperature_unit=fahrenheit&timezone=auto",
           kLat, kLon);

  HTTPClient http;
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    Serial.println("weather begin failed");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("weather http %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  applyJson(body.c_str());
  return gHaveWeather;
}

static void refreshClock() {
  struct tm nowTm;
  if (!getLocalTime(&nowTm, 0)) return;
  gHour = nowTm.tm_hour;
  strftime(gTime, sizeof(gTime), "%I:%M %p", &nowTm);
  if (gTime[0] == '0') memmove(gTime, gTime + 1, strlen(gTime));
  strftime(gDate, sizeof(gDate), "%a %b %d", &nowTm);
}

void weatherBegin() {
  if (WIFI_SSID[0] == '\0') {
    snprintf(gNow, sizeof(gNow), "no wifi");
    Serial.println("wifi secrets missing");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  gLastWifiTry = millis();
  Serial.printf("wifi connecting to %s\n", WIFI_SSID);
}

void weatherTick(uint32_t now) {
  if (WIFI_SSID[0] == '\0') return;
  if (WiFi.status() != WL_CONNECTED) {
    if (!gHaveWeather) snprintf(gNow, sizeof(gNow), "wifi ...");
    if (now - gLastWifiTry > 15000) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      gLastWifiTry = now;
      Serial.println("wifi retry");
    }
    return;
  }

  static bool announced = false;
  if (!announced) {
    announced = true;
    configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    Serial.printf("wifi connected %s\n", WiFi.localIP().toString().c_str());
    if (!gHaveWeather) snprintf(gNow, sizeof(gNow), "weather ...");
  }
  refreshClock();

  uint32_t wait = gHaveWeather ? kRefreshMs : kRetryMs;
  if (gLastFetch != 0 && (now - gLastFetch) < wait) return;
  gLastFetch = now;
  if (!fetchWeather() && !gHaveWeather) snprintf(gNow, sizeof(gNow), "weather ...");
}
