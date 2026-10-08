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

// Card files are little-endian RGB565. LovyanGFX 16-bit sprites hold each pixel
// byte-swapped, so swap once here and the frame loop can copy them as is.
static void toSpriteOrder(uint16_t *px, size_t count) {
  for (size_t i = 0; i < count; ++i) px[i] = __builtin_bswap16(px[i]);
}

static bool isDark(uint16_t spritePx, int limit5, int limit6) {
  const uint16_t c = __builtin_bswap16(spritePx);
  return ((c >> 11) & 31) < limit5 && ((c >> 5) & 63) < limit6 && (c & 31) < limit5 + 1;
}

// Some poses kept the cut-out background where it was enclosed, such as the gap
// between a wing and the body, so it draws as a black patch. Clear any large,
// mostly solid near-black region from the mask, then its dark antialiased rim.
// Thin dark lines like eye outlines are sparse in their box and stay.
static int clearEnclosedGaps(const uint16_t *px, uint8_t *mask, int w, int h) {
  const int n = w * h;
  auto opaque = [&](int i) { return (mask[i >> 3] & (0x80 >> (i & 7))) != 0; };
  auto clearBit = [&](int i) { mask[i >> 3] &= ~(0x80 >> (i & 7)); };
  uint8_t *state = static_cast<uint8_t *>(heap_caps_calloc(n, 1, MALLOC_CAP_SPIRAM));  // 1 seen, 2 cleared
  int32_t *stack = static_cast<int32_t *>(heap_caps_malloc((size_t)n * 4, MALLOC_CAP_SPIRAM));
  int32_t *comp = static_cast<int32_t *>(heap_caps_malloc((size_t)n * 4, MALLOC_CAP_SPIRAM));
  int cleared = 0;
  if (state && stack && comp) {
    for (int start = 0; start < n; ++start) {
      if (state[start] || !opaque(start) || !isDark(px[start], 5, 10)) continue;
      int top = 0, count = 0;
      int minX = w, minY = h, maxX = 0, maxY = 0;
      stack[top++] = start;
      state[start] = 1;
      while (top) {
        const int i = stack[--top];
        comp[count++] = i;
        const int x = i % w, y = i / w;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        const int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
        for (int j : nb) {
          if (j < 0 || state[j] || !opaque(j) || !isDark(px[j], 5, 10)) continue;
          state[j] = 1;
          stack[top++] = j;
        }
      }
      const int boxArea = (maxX - minX + 1) * (maxY - minY + 1);
      if (count < 300 || count * 10 < boxArea * 4) continue;
      for (int k = 0; k < count; ++k) {
        clearBit(comp[k]);
        state[comp[k]] = 2;
      }
      cleared += count;
    }
    // Two passes over the rim: dim pixels touching a cleared one go too.
    for (int pass = 0; pass < 2 && cleared; ++pass) {
      int rim = 0;
      for (int i = 0; i < n; ++i) {
        if (!opaque(i) || !isDark(px[i], 12, 24)) continue;
        const int x = i % w, y = i / w;
        if ((x > 0 && state[i - 1] == 2) || (x < w - 1 && state[i + 1] == 2) || (y > 0 && state[i - w] == 2) ||
            (y < h - 1 && state[i + w] == 2)) {
          comp[rim++] = i;
        }
      }
      for (int k = 0; k < rim; ++k) {
        clearBit(comp[k]);
        state[comp[k]] = 2;
      }
      cleared += rim;
    }
  }
  heap_caps_free(state);
  heap_caps_free(stack);
  heap_caps_free(comp);
  return cleared;
}

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
    toSpriteOrder(px, (size_t)w * (size_t)h);
    const int gaps = clearEnclosedGaps(px, mask, w, h);
    if (gaps) Serial.printf("sprite %d: cleared %d px of enclosed background\n", i, gaps);
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
    toSpriteOrder(px, 480UL * 480UL);
    slots[i] = px;
    loaded++;
    Serial.printf("background %s\n", kNames[i]);
  }
  return loaded == 4;
}
