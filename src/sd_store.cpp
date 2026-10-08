#include "sd_store.h"

#include <SD.h>
#include <SPI.h>

#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

static constexpr int kSdSck = 48;
static constexpr int kSdMiso = 41;
static constexpr int kSdMosi = 47;
static constexpr int kSdCs = 42;
static constexpr int kDisplayCs = 39;

static void copyTrimmed(char *dst, size_t dstLen, const String &src) {
  size_t start = 0;
  while (start < src.length() && (src[start] == ' ' || src[start] == '\t' || src[start] == '\r')) {
    start++;
  }
  size_t end = src.length();
  while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\r')) {
    end--;
  }
  size_t n = end - start;
  if (n >= dstLen) n = dstLen - 1;
  for (size_t i = 0; i < n; ++i) dst[i] = src[start + i];
  dst[n] = '\0';
}

static int loadLines(const char *path, char store[][32], int cap) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("SD missing %s\n", path);
    return 0;
  }
  int count = 0;
  while (file.available() && count < cap) {
    String line = file.readStringUntil('\n');
    char buf[40];
    copyTrimmed(buf, sizeof(buf), line);
    if (buf[0] == '\0' || buf[0] == '#') continue;
    strncpy(store[count], buf, 31);
    store[count][31] = '\0';
    count++;
  }
  file.close();
  Serial.printf("SD %s lines: %d\n", path, count);
  return count;
}

static void loadSettings(char *name, size_t nameLen, uint32_t *holdMs, char *description,
                         size_t descriptionLen) {
  File file = SD.open("/byte/settings.txt", FILE_READ);
  if (!file) {
    Serial.println("SD missing /byte/settings.txt");
    return;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    char buf[128];
    copyTrimmed(buf, sizeof(buf), line);
    if (buf[0] == '\0' || buf[0] == '#') continue;
    char *eq = strchr(buf, '=');
    if (!eq) continue;
    *eq = '\0';
    const char *key = buf;
    const char *value = eq + 1;
    if (strcmp(key, "name") == 0 && name && nameLen > 0) {
      strncpy(name, value, nameLen - 1);
      name[nameLen - 1] = '\0';
    } else if (strcmp(key, "hold_ms") == 0 && holdMs) {
      int ms = atoi(value);
      if (ms >= 800 && ms <= 8000) *holdMs = (uint32_t)ms;
    } else if (strcmp(key, "description") == 0 && description && descriptionLen > 0) {
      strncpy(description, value, descriptionLen - 1);
      description[descriptionLen - 1] = '\0';
    }
  }
  file.close();
}

bool loadPetCard(char *name, size_t nameLen, uint32_t *holdMs, char *description,
                 size_t descriptionLen, char phrases[][32], int phraseCap, int *phraseCount,
                 char pets[][32], int petCap, int *petCount) {
  if (phraseCount) *phraseCount = 0;
  if (petCount) *petCount = 0;

  pinMode(kDisplayCs, OUTPUT);
  digitalWrite(kDisplayCs, HIGH);

  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  if (!SD.begin(kSdCs, SPI, 10000000)) {
    Serial.println("SD not mounted, using built-in lines");
    return false;
  }
  Serial.println("SD mounted");

  loadSettings(name, nameLen, holdMs, description, descriptionLen);
  if (phrases && phraseCount) *phraseCount = loadLines("/byte/phrases.txt", phrases, phraseCap);
  if (pets && petCount) *petCount = loadLines("/byte/pet.txt", pets, petCap);
  return true;
}

bool loadHimopSprites(HimopCardFrame *frames, int count) {
  if (!frames || count < 1 || count > 24) return false;
  int loaded = 0;
  for (int i = 0; i < count; ++i) {
    frames[i].px = nullptr;
    frames[i].mask = nullptr;
    frames[i].w = 0;
    frames[i].h = 0;
    char path[32];
    snprintf(path, sizeof(path), "/byte/sprites/%d.bin", i);
    File file = SD.open(path, FILE_READ);
    if (!file) {
      Serial.printf("SD missing %s\n", path);
      continue;
    }
    uint16_t wh[2] = {};
    if (file.read(reinterpret_cast<uint8_t *>(wh), 4) != 4) {
      file.close();
      continue;
    }
    const int w = wh[0];
    const int h = wh[1];
    if (w < 8 || h < 8 || w > 400 || h > 400) {
      Serial.printf("SD bad sprite %s\n", path);
      file.close();
      continue;
    }
    const size_t pixBytes = (size_t)w * (size_t)h * 2;
    const size_t maskBytes = ((size_t)w * (size_t)h + 7) / 8;
    if ((size_t)file.size() < 4 + pixBytes + maskBytes) {
      file.close();
      continue;
    }
    uint16_t *px = static_cast<uint16_t *>(heap_caps_malloc(pixBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint8_t *mask = static_cast<uint8_t *>(heap_caps_malloc(maskBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!px || !mask || file.read(reinterpret_cast<uint8_t *>(px), pixBytes) != (int)pixBytes ||
        file.read(mask, maskBytes) != (int)maskBytes) {
      heap_caps_free(px);
      heap_caps_free(mask);
      file.close();
      Serial.printf("SD read fail %s\n", path);
      continue;
    }
    file.close();
    frames[i].px = px;
    frames[i].mask = mask;
    frames[i].w = w;
    frames[i].h = h;
    loaded++;
    Serial.printf("sprite %d %dx%d\n", i, w, h);
  }
  return loaded == count;
}

bool loadDayBackgrounds(uint16_t **slots, int count) {
  static const char *kNames[] = {"morning", "afternoon", "evening", "night"};
  if (!slots || count < 4) return false;
  int loaded = 0;
  for (int i = 0; i < 4; ++i) {
    slots[i] = nullptr;
    char path[40];
    snprintf(path, sizeof(path), "/byte/bg/%s.bin", kNames[i]);
    File file = SD.open(path, FILE_READ);
    if (!file) {
      Serial.printf("SD missing %s\n", path);
      continue;
    }
    uint16_t wh[2] = {};
    if (file.read(reinterpret_cast<uint8_t *>(wh), 4) != 4 || wh[0] != 480 || wh[1] != 480) {
      Serial.printf("SD bad background %s\n", path);
      file.close();
      continue;
    }
    const size_t pixBytes = 480UL * 480UL * 2UL;
    if ((size_t)file.size() < 4 + pixBytes) {
      file.close();
      continue;
    }
    uint16_t *px = static_cast<uint16_t *>(heap_caps_malloc(pixBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!px || file.read(reinterpret_cast<uint8_t *>(px), pixBytes) != (int)pixBytes) {
      heap_caps_free(px);
      file.close();
      Serial.printf("SD read fail %s\n", path);
      continue;
    }
    file.close();
    slots[i] = px;
    loaded++;
    Serial.printf("background %s\n", kNames[i]);
  }
  return loaded == 4;
}
