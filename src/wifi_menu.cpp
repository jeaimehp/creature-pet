#include "wifi_menu.h"

#include "display_rgb.h"
#include "weather.h"

#include <Preferences.h>
#include <WiFi.h>
#include <string.h>

static constexpr int W = 480;
static constexpr int H = 480;

static LGFX_Sprite *gCanvas = nullptr;

enum Page : uint8_t { kPageClosed, kPageMain, kPageScan, kPageKeyboard, kPageDisplay };

static Page page = kPageClosed;
static bool dirty = false;

static uint8_t showFlags = kShowTime | kShowDate | kShowWeather | kShowName | kShowCaption;

// Main page status line, redrawn when it changes.
static char statusShown[64] = "";

// Scan results, strongest first, duplicates removed.
static constexpr int kMaxNets = 24;
static constexpr int kRowsPerPage = 6;
struct Net {
  char ssid[33];
  int rssi;
  bool locked;
};
static Net nets[kMaxNets];
static int netCount = 0;
static int netPage = 0;
static bool scanning = false;

// Keyboard state. The keyboard edits either the SSID (hidden network) or the password.
static bool editingSsid = false;
static char ssidBuf[33] = "";
static char passBuf[65] = "";
static bool shifted = false;
static bool symbols = false;

struct Button {
  int x, y, w, h;
};

static bool hit(const Button &b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return gCanvas->color565(r, g, b); }

static void drawButton(const Button &b, const char *text, uint16_t fill, const lgfx::GFXfont *font) {
  gCanvas->fillRoundRect(b.x, b.y, b.w, b.h, 10, fill);
  gCanvas->drawRoundRect(b.x, b.y, b.w, b.h, 10, rgb(90, 120, 160));
  gCanvas->setFont(font);
  gCanvas->setTextDatum(middle_center);
  gCanvas->setTextColor(rgb(255, 255, 255));
  gCanvas->drawString(text, b.x + b.w / 2, b.y + b.h / 2);
}

// Draws text left-aligned, keeping the end visible when it is too wide.
static void drawTail(const char *text, int x, int y, int maxW) {
  const char *p = text;
  while (*p && gCanvas->textWidth(p) > maxW) ++p;
  gCanvas->drawString(p, x, y);
}

static void drawTitle(const char *text) {
  gCanvas->fillScreen(rgb(8, 12, 24));
  gCanvas->setFont(&fonts::FreeSansBold18pt7b);
  gCanvas->setTextDatum(top_left);
  gCanvas->setTextColor(rgb(255, 255, 255));
  gCanvas->drawString(text, 16, 14);
}

// ---------------------------------------------------------------------------
// Main page

static const Button kChooseBtn = {16, 230, 448, 64};
static const Button kDisplayBtn = {16, 306, 448, 64};
static const Button kCloseBtn = {16, 400, 448, 64};

static void statusText(char *out, size_t len) {
  switch (weatherWifiState()) {
    case kWifiUnset:
      snprintf(out, len, "Not set up");
      break;
    case kWifiConnecting:
      snprintf(out, len, "Connecting ...");
      break;
    case kWifiConnected:
      snprintf(out, len, "Connected  %s", weatherIp().c_str());
      break;
  }
}

static void drawMain() {
  drawTitle("Wi-Fi");
  gCanvas->setTextDatum(top_left);
  gCanvas->setFont(&fonts::FreeSans12pt7b);
  gCanvas->setTextColor(rgb(160, 180, 210));
  gCanvas->drawString("Network", 16, 78);
  gCanvas->drawString("Status", 16, 160);
  gCanvas->setFont(&fonts::FreeSansBold18pt7b);
  gCanvas->setTextColor(rgb(255, 255, 255));
  const char *ssid = weatherSsid();
  drawTail(ssid[0] ? ssid : "none", 16, 104, W - 32);
  statusText(statusShown, sizeof(statusShown));
  gCanvas->setFont(&fonts::FreeSansBold12pt7b);
  gCanvas->drawString(statusShown, 16, 188);
  drawButton(kChooseBtn, "Choose network", rgb(30, 90, 150), &fonts::FreeSansBold18pt7b);
  drawButton(kDisplayBtn, "Display", rgb(30, 90, 150), &fonts::FreeSansBold18pt7b);
  drawButton(kCloseBtn, "Close", rgb(40, 48, 64), &fonts::FreeSansBold18pt7b);
}

// ---------------------------------------------------------------------------
// Display page

struct ShowItem {
  uint8_t flag;
  const char *label;
};
static const ShowItem kShowItems[] = {
    {kShowTime, "Time"},
    {kShowDate, "Date"},
    {kShowWeather, "Weather"},
    {kShowName, "Name"},
    {kShowCaption, "Description"},
};
static constexpr int kShowCount = sizeof(kShowItems) / sizeof(kShowItems[0]);
static constexpr int kShowY = 70;
static constexpr int kShowH = 64;
static const Button kDisplayBackBtn = {16, 400, 448, 64};

static void saveShowFlags() {
  Preferences prefs;
  if (prefs.begin("himop", false)) {
    prefs.putUChar("show", showFlags);
    prefs.end();
  }
}

static void drawCheckbox(int x, int y, bool on) {
  constexpr int kBox = 36;
  if (on) {
    gCanvas->fillRoundRect(x, y, kBox, kBox, 8, rgb(30, 140, 200));
    gCanvas->drawWideLine(x + 8, y + 19, x + 15, y + 27, 3.5f, rgb(255, 255, 255));
    gCanvas->drawWideLine(x + 15, y + 27, x + 29, y + 10, 3.5f, rgb(255, 255, 255));
  } else {
    gCanvas->fillRoundRect(x, y, kBox, kBox, 8, rgb(24, 34, 52));
    gCanvas->drawRoundRect(x, y, kBox, kBox, 8, rgb(120, 140, 170));
  }
}

static void drawDisplay() {
  drawTitle("Display");
  for (int i = 0; i < kShowCount; ++i) {
    const int y = kShowY + i * kShowH;
    gCanvas->fillRoundRect(8, y, W - 16, kShowH - 8, 10, rgb(24, 34, 52));
    drawCheckbox(24, y + (kShowH - 8 - 36) / 2, (showFlags & kShowItems[i].flag) != 0);
    gCanvas->setFont(&fonts::FreeSansBold18pt7b);
    gCanvas->setTextDatum(middle_left);
    gCanvas->setTextColor(rgb(255, 255, 255));
    gCanvas->drawString(kShowItems[i].label, 80, y + (kShowH - 8) / 2);
  }
  drawButton(kDisplayBackBtn, "Back", rgb(40, 48, 64), &fonts::FreeSansBold18pt7b);
}

static void pressDisplay(int x, int y) {
  if (hit(kDisplayBackBtn, x, y)) {
    page = kPageMain;
    dirty = true;
    return;
  }
  if (y < kShowY || y >= kShowY + kShowCount * kShowH) return;
  const ShowItem &item = kShowItems[(y - kShowY) / kShowH];
  showFlags ^= item.flag;
  saveShowFlags();
  Serial.printf("menu show %s %s\n", item.label, (showFlags & item.flag) ? "on" : "off");
  dirty = true;
}

// ---------------------------------------------------------------------------
// Scan page

static constexpr int kRowY = 66;
static constexpr int kRowH = 54;
static const Button kBackBtn = {8, 412, 110, 60};
static const Button kRescanBtn = {126, 412, 110, 60};
static const Button kOtherBtn = {244, 412, 110, 60};
static const Button kMoreBtn = {362, 412, 110, 60};

static void startScan() {
  netCount = 0;
  netPage = 0;
  scanning = true;
  weatherPauseWifi();
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  dirty = true;
}

static void collectScan(int found) {
  netCount = 0;
  for (int i = 0; i < found; ++i) {
    String name = WiFi.SSID(i);
    if (name.length() == 0) continue;
    const int rssi = WiFi.RSSI(i);
    const bool locked = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    int existing = -1;
    for (int j = 0; j < netCount; ++j) {
      if (name == nets[j].ssid) existing = j;
    }
    if (existing >= 0) {
      if (rssi > nets[existing].rssi) nets[existing].rssi = rssi;
      continue;
    }
    if (netCount >= kMaxNets) continue;
    Net &n = nets[netCount++];
    strncpy(n.ssid, name.c_str(), sizeof(n.ssid) - 1);
    n.ssid[sizeof(n.ssid) - 1] = '\0';
    n.rssi = rssi;
    n.locked = locked;
  }
  for (int i = 1; i < netCount; ++i) {
    for (int j = i; j > 0 && nets[j].rssi > nets[j - 1].rssi; --j) {
      Net t = nets[j];
      nets[j] = nets[j - 1];
      nets[j - 1] = t;
    }
  }
  WiFi.scanDelete();
}

static void drawBars(int x, int y, int rssi) {
  const int level = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    const int bh = 6 + i * 6;
    const uint16_t c = i < level ? rgb(140, 230, 255) : rgb(50, 64, 84);
    gCanvas->fillRect(x + i * 8, y - bh, 6, bh, c);
  }
}

static void drawLock(int x, int y) {
  const uint16_t c = rgb(200, 210, 230);
  gCanvas->drawRoundRect(x + 3, y - 20, 10, 12, 4, c);
  gCanvas->fillRoundRect(x, y - 12, 16, 12, 2, c);
}

static void drawScan() {
  drawTitle(scanning ? "Scanning ..." : "Choose network");
  if (!scanning) {
    if (netCount == 0) {
      gCanvas->setFont(&fonts::FreeSans12pt7b);
      gCanvas->setTextDatum(top_left);
      gCanvas->setTextColor(rgb(200, 210, 230));
      gCanvas->drawString("No networks found.", 16, 90);
    }
    const int first = netPage * kRowsPerPage;
    for (int r = 0; r < kRowsPerPage && first + r < netCount; ++r) {
      const Net &n = nets[first + r];
      const int y = kRowY + r * kRowH;
      gCanvas->fillRoundRect(8, y, W - 16, kRowH - 6, 8, rgb(24, 34, 52));
      gCanvas->setFont(&fonts::FreeSansBold12pt7b);
      gCanvas->setTextDatum(middle_left);
      gCanvas->setTextColor(rgb(255, 255, 255));
      gCanvas->setClipRect(20, y, W - 120, kRowH - 6);
      gCanvas->drawString(n.ssid, 20, y + (kRowH - 6) / 2);
      gCanvas->clearClipRect();
      if (n.locked) drawLock(W - 92, y + kRowH - 18);
      drawBars(W - 60, y + kRowH - 16, n.rssi);
    }
  }
  drawButton(kBackBtn, "Back", rgb(40, 48, 64), &fonts::FreeSansBold12pt7b);
  drawButton(kRescanBtn, "Rescan", rgb(40, 48, 64), &fonts::FreeSansBold12pt7b);
  drawButton(kOtherBtn, "Other", rgb(40, 48, 64), &fonts::FreeSansBold12pt7b);
  if (netCount > kRowsPerPage) drawButton(kMoreBtn, "More", rgb(40, 48, 64), &fonts::FreeSansBold12pt7b);
}

// ---------------------------------------------------------------------------
// Keyboard page

enum KeyCode : int16_t { kKeyShift = -1, kKeyBack = -2, kKeySym = -3, kKeyCancel = -4, kKeySave = -5 };

struct Key {
  Button box;
  int16_t code;  // printable character, or a KeyCode
};

static constexpr int kKeyTop = 150;
static constexpr int kKeyRowH = 64;
static constexpr int kUnit = 48;
static constexpr int kMaxKeys = 48;

static int addRow(Key *keys, int n, const char *chars, int row, int startX) {
  const int y = kKeyTop + row * kKeyRowH;
  int x = startX;
  for (const char *c = chars; *c && n < kMaxKeys; ++c) {
    keys[n++] = {{x + 2, y + 2, kUnit - 4, kKeyRowH - 6}, (int16_t)*c};
    x += kUnit;
  }
  return n;
}

static int addKey(Key *keys, int n, int x, int row, int units, int16_t code) {
  const int y = kKeyTop + row * kKeyRowH;
  keys[n++] = {{x + 2, y + 2, units * kUnit / 2 - 4, kKeyRowH - 6}, code};
  return n;
}

// Builds the keys for the current mode. Widths for special keys are in half units.
static int layoutKeys(Key *keys) {
  int n = 0;
  if (symbols) {
    n = addRow(keys, n, "!@#$%^&*()", 0, 0);
    n = addRow(keys, n, "-_=+[]{}\\|", 1, 0);
    n = addRow(keys, n, ";:'\",.<>/?", 2, 0);
    n = addRow(keys, n, "`~", 3, 0);
    n = addKey(keys, n, 7 * kUnit, 3, 6, kKeyBack);
  } else {
    n = addRow(keys, n, "1234567890", 0, 0);
    n = addRow(keys, n, shifted ? "QWERTYUIOP" : "qwertyuiop", 1, 0);
    n = addRow(keys, n, shifted ? "ASDFGHJKL" : "asdfghjkl", 2, kUnit / 2);
    n = addKey(keys, n, 0, 3, 3, kKeyShift);
    n = addRow(keys, n, shifted ? "ZXCVBNM" : "zxcvbnm", 3, kUnit * 3 / 2);
    n = addKey(keys, n, kUnit * 17 / 2, 3, 3, kKeyBack);
  }
  n = addKey(keys, n, 0, 4, 4, kKeySym);
  n = addKey(keys, n, 2 * kUnit, 4, 8, ' ');
  n = addKey(keys, n, 6 * kUnit, 4, 4, kKeyCancel);
  n = addKey(keys, n, 8 * kUnit, 4, 4, kKeySave);
  return n;
}

static const char *keyLabel(const Key &k, char *buf) {
  switch (k.code) {
    case kKeyShift: return shifted ? "SHIFT" : "shift";
    case kKeyBack: return "del";
    case kKeySym: return symbols ? "abc" : "#+=";
    case kKeyCancel: return "Cancel";
    case kKeySave: return editingSsid ? "Next" : "Save";
    case ' ': return "space";
    default:
      buf[0] = (char)k.code;
      buf[1] = '\0';
      return buf;
  }
}

static void drawKeyboard() {
  char prompt[64];
  if (editingSsid) snprintf(prompt, sizeof(prompt), "Network name");
  else snprintf(prompt, sizeof(prompt), "Password for %s", ssidBuf);
  gCanvas->fillScreen(rgb(8, 12, 24));
  gCanvas->setFont(&fonts::FreeSansBold12pt7b);
  gCanvas->setTextDatum(top_left);
  gCanvas->setTextColor(rgb(160, 180, 210));
  gCanvas->setClipRect(16, 0, W - 32, 60);
  gCanvas->drawString(prompt, 16, 18);
  gCanvas->clearClipRect();

  gCanvas->fillRoundRect(12, 60, W - 24, 72, 10, rgb(24, 34, 52));
  gCanvas->setFont(&fonts::FreeSansBold18pt7b);
  gCanvas->setTextDatum(middle_left);
  gCanvas->setTextColor(rgb(255, 255, 255));
  const char *text = editingSsid ? ssidBuf : passBuf;
  drawTail(text, 26, 96, W - 64);
  const int cx = 26 + gCanvas->textWidth(text[0] ? text : "") + 2;
  gCanvas->fillRect(cx < W - 34 ? cx : W - 34, 80, 3, 34, rgb(140, 230, 255));

  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  char buf[2];
  for (int i = 0; i < n; ++i) {
    const Key &k = keys[i];
    uint16_t fill = rgb(40, 52, 72);
    if (k.code == kKeySave) fill = rgb(30, 110, 70);
    else if (k.code == kKeyCancel) fill = rgb(110, 40, 40);
    else if (k.code < 0) fill = rgb(28, 36, 52);
    const lgfx::GFXfont *font =
        (k.code >= 0 && k.code != ' ') ? &fonts::FreeSansBold12pt7b : &fonts::FreeSans9pt7b;
    gCanvas->fillRoundRect(k.box.x, k.box.y, k.box.w, k.box.h, 8, fill);
    gCanvas->setFont(font);
    gCanvas->setTextDatum(middle_center);
    gCanvas->setTextColor(rgb(255, 255, 255));
    gCanvas->drawString(keyLabel(k, buf), k.box.x + k.box.w / 2, k.box.y + k.box.h / 2);
  }
}

static void openKeyboard(bool forSsid) {
  editingSsid = forSsid;
  if (forSsid) ssidBuf[0] = '\0';
  passBuf[0] = '\0';
  shifted = false;
  symbols = false;
  page = kPageKeyboard;
  dirty = true;
}

static void save() {
  Serial.printf("menu saving wifi '%s'\n", ssidBuf);
  weatherSetWifi(ssidBuf, passBuf);
  page = kPageMain;
  dirty = true;
}

static void pressKeyboard(int x, int y) {
  Key keys[kMaxKeys];
  const int n = layoutKeys(keys);
  for (int i = 0; i < n; ++i) {
    if (!hit(keys[i].box, x, y)) continue;
    char *text = editingSsid ? ssidBuf : passBuf;
    const size_t cap = editingSsid ? sizeof(ssidBuf) : sizeof(passBuf);
    const size_t len = strlen(text);
    switch (keys[i].code) {
      case kKeyShift:
        shifted = !shifted;
        break;
      case kKeySym:
        symbols = !symbols;
        break;
      case kKeyBack:
        if (len > 0) text[len - 1] = '\0';
        break;
      case kKeyCancel:
        weatherResumeWifi();
        page = kPageMain;
        break;
      case kKeySave:
        if (editingSsid) {
          if (len > 0) openKeyboard(false);
        } else {
          save();
        }
        break;
      default:
        if (len + 1 < cap) {
          text[len] = (char)keys[i].code;
          text[len + 1] = '\0';
        }
        if (shifted && !symbols) shifted = false;
        break;
    }
    dirty = true;
    return;
  }
}

// ---------------------------------------------------------------------------

void menuBegin(LGFX_Sprite *canvas) {
  gCanvas = canvas;
  Preferences prefs;
  if (prefs.begin("himop", true)) {
    showFlags = prefs.getUChar("show", showFlags);
    prefs.end();
  }
}

bool menuIsOpen() { return page != kPageClosed; }

bool menuShows(uint8_t flag) { return (showFlags & flag) != 0; }

void menuOpen() {
  page = kPageMain;
  dirty = true;
  Serial.println("menu open");
}

void menuPress(int x, int y) {
  Serial.printf("menu tap %d,%d\n", x, y);
  if (page == kPageMain) {
    if (hit(kChooseBtn, x, y)) {
      page = kPageScan;
      startScan();
    } else if (hit(kDisplayBtn, x, y)) {
      page = kPageDisplay;
      dirty = true;
    } else if (hit(kCloseBtn, x, y)) {
      page = kPageClosed;
    }
    return;
  }
  if (page == kPageScan) {
    if (hit(kBackBtn, x, y)) {
      WiFi.scanDelete();
      scanning = false;
      weatherResumeWifi();
      page = kPageMain;
      dirty = true;
    } else if (hit(kRescanBtn, x, y)) {
      startScan();
    } else if (hit(kOtherBtn, x, y)) {
      scanning = false;
      openKeyboard(true);
    } else if (hit(kMoreBtn, x, y) && netCount > kRowsPerPage) {
      netPage = (netPage + 1) * kRowsPerPage < netCount ? netPage + 1 : 0;
      dirty = true;
    } else if (!scanning && y >= kRowY && y < kRowY + kRowsPerPage * kRowH) {
      const int i = netPage * kRowsPerPage + (y - kRowY) / kRowH;
      if (i < netCount) {
        strncpy(ssidBuf, nets[i].ssid, sizeof(ssidBuf) - 1);
        ssidBuf[sizeof(ssidBuf) - 1] = '\0';
        if (nets[i].locked) {
          openKeyboard(false);
        } else {
          passBuf[0] = '\0';
          save();
        }
      }
    }
    return;
  }
  if (page == kPageKeyboard) pressKeyboard(x, y);
  if (page == kPageDisplay) pressDisplay(x, y);
}

bool menuTick(uint32_t now) {
  (void)now;
  if (page == kPageClosed) return false;

  if (page == kPageScan && scanning) {
    const int found = WiFi.scanComplete();
    if (found >= 0) {
      collectScan(found);
      scanning = false;
      dirty = true;
      Serial.printf("menu scan found %d\n", netCount);
    } else if (found == WIFI_SCAN_FAILED) {
      scanning = false;
      dirty = true;
      Serial.println("menu scan failed");
    }
  }
  if (page == kPageMain) {
    char status[64];
    statusText(status, sizeof(status));
    if (strcmp(status, statusShown) != 0) dirty = true;
  }

  if (dirty) {
    dirty = false;
    if (page == kPageMain) drawMain();
    else if (page == kPageScan) drawScan();
    else if (page == kPageDisplay) drawDisplay();
    else drawKeyboard();
    panelWaitVsync(30);
    panelCopy((const uint16_t *)gCanvas->getBuffer(), 0, 0, W, H);
  }
  return true;
}
