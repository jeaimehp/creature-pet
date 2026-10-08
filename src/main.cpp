#include "himop_art.h"
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.hpp"
#include "display_rgb.h"
#include "sd_store.h"
#include "weather.h"
#include "wifi_menu.h"

#include <math.h>
#include <string.h>

static LGFX_Sprite canvas;

static constexpr int W = 480;
static constexpr int H = 480;
static constexpr int kMinX = 150;
static constexpr int kMaxX = 330;
static constexpr int kMinY = 190;
static constexpr int kMaxY = 280;

static int himopCrownX = 240;
static int himopCrownY = 90;
static HimopCardFrame cardFrames[18];
static bool haveCardSprites = false;
static uint16_t *dayBg[4] = {};
static bool haveBackgrounds = false;
static int himopSpriteH = 220;

// Himop naps only after 5 minutes without a touch. Each nap lasts 30 minutes,
// and a touch wakes him (pollTouch resets buddy.until).
static constexpr uint32_t kNapAfterMs = 5UL * 60UL * 1000UL;
static constexpr uint32_t kNapMs = 30UL * 60UL * 1000UL;

static uint32_t rngState = 0xC0FFEE01;

static uint32_t rnd() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

static int rndRange(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Screen rectangle, half-open. Empty when x0 >= x1.
struct Box {
  int x0, y0, x1, y1;
};

static bool boxEmpty(const Box &b) { return b.x0 >= b.x1 || b.y0 >= b.y1; }

static void growBox(Box &b, int x, int y, int w, int h) {
  const int x0 = clampi(x, 0, W), y0 = clampi(y, 0, H);
  const int x1 = clampi(x + w, 0, W), y1 = clampi(y + h, 0, H);
  if (x0 >= x1 || y0 >= y1) return;
  if (boxEmpty(b)) {
    b = {x0, y0, x1, y1};
    return;
  }
  if (x0 < b.x0) b.x0 = x0;
  if (y0 < b.y0) b.y0 = y0;
  if (x1 > b.x1) b.x1 = x1;
  if (y1 > b.y1) b.y1 = y1;
}

// Everything that moves this frame. Only this area (plus last frame's) is
// redrawn and pushed, so the CPU leaves PSRAM free for the panel's DMA.
static Box dynBox = {0, 0, 0, 0};

static void markDirty(int x, int y, int w, int h) { growBox(dynBox, x, y, w, h); }

enum Face : uint8_t {
  kNeutral,
  kHappy,
  kSleepy,
  kSurprise,
  kWink,
  kSmirk,
  kDizzy,
  kLove,
  kGrumpy,
  kSilly,
  kThink,
  kExcited,
  kGlitch
};

enum Action : uint8_t {
  kIdle,
  kWander,
  kHop,
  kWiggle,
  kPeek,
  kSpin,
  kDance,
  kYawn,
  kGlitchAct,
  kFly,
  kGraze,
  kFlee,
  kCome  // heading to where he was tapped
};

enum EyeStyle : uint8_t {
  kEyeOpen,
  kEyeHappy,
  kEyeSleepy,
  kEyeShut,
  kEyeDizzy,
  kEyeLove,
  kEyeWide
};

struct Mote {
  float x, y, vy;
  uint8_t life;
  uint8_t kind;  // 0 ambient spark, 1 heart
};

static constexpr int kMotes = 28;
static Mote motes[kMotes];

struct Motion {
  float x, y;
  float tx, ty;
  float z, tz;  // depth: 0 far away, 1 close up
  float anchorX, anchorY;
  uint8_t action;
  uint8_t face;
  uint32_t until;
  bool winkLeft;
  const char *caption;
};

static Motion buddy;

static const char *kKindWords[] = {
    "you matter",    "nice work",      "hello friend", "you got this",
    "so proud",      "be kind",        "good day",     "you rock",
    "stay curious",  "i like you",     "high five",    "shine on",
    "all is well",   "you belong",     "keep going",   "way to go",
    "have fun",      "you are enough", "good luck",    "proud of you",
};
static const char *kPetWords[] = {
    "that was nice", "yay a pet", "more please", "you are the best", "i like this",
};

struct Speech {
  const char *text;
  uint32_t until;
  bool visible;
};

static Speech speech;
static int kookerX = -80;
static uint32_t kookerUntil = 0;
static char petName[16] = "Himop";
static char petDescription[96] = "It may be 12 feet 6 inches tall, but it easily flies, too.";
static uint32_t reactionMs = 2800;
static const char **kindLines = nullptr;
static int kindCount = 0;
static const char *petLinePtrs[12];
static int petLineCount = 0;
static char phraseStore[24][32];
static char petStore[12][32];
static const char *phrasePtrs[24];
static uint32_t lastPetMs = 0;
static uint32_t bootMs = 0;
static bool seenTouch = false;

// A tap calls Himop over (kCome): he finishes waking if he was napping, walks
// if he was on the ground, otherwise flies. Only when he arrives does he enjoy
// the pet, with the happy pose and hearts, until enjoyUntil.
static uint32_t enjoyUntil = 0;
static uint32_t wakeUntil = 0;
static uint32_t lastHeartMs = 0;
static bool comeWalking = false;
static constexpr float kComeFlyStep = 4.0f;
static constexpr float kComeWalkStep = 1.8f;

static bool isHappy(uint32_t now) { return (int32_t)(enjoyUntil - now) > 0; }

static void spawnSpark(Mote &m) {
  m.kind = 0;
  m.x = (float)rndRange(16, W - 16);
  m.y = (float)rndRange(8, H - 8);
  m.vy = 0.15f + (rnd() % 40) / 80.0f;
  m.life = (uint8_t)rndRange(40, 140);
}

static void spawnHeart(Mote &m, float originX, float originY) {
  m.kind = 1;
  m.x = originX + (float)rndRange(-40, 40);
  m.y = originY + (float)rndRange(-20, 16);
  m.vy = -(0.7f + (rnd() % 50) / 80.0f);
  m.life = (uint8_t)rndRange(40, 70);
}

static void drawHeart(int x, int y, int s, uint16_t color) {
  canvas.fillCircle(x - s, y, s, color);
  canvas.fillCircle(x + s, y, s, color);
  canvas.fillTriangle(x - s * 2, y + s / 2, x + s * 2, y + s / 2, x, y + s * 3, color);
}

static void drawSmile(int cx, int cy, int w, int depth, uint16_t color, int thick) {
  int prevX = cx - w;
  int prevY = cy;
  for (int i = 1; i <= 14; ++i) {
    float t = (i / 14.0f) * 2.0f - 1.0f;
    int x = cx + (int)(t * w);
    int y = cy + (int)((1.0f - t * t) * depth);
    canvas.drawWideLine(prevX, prevY, x, y, (float)thick, color);
    prevX = x;
    prevY = y;
  }
}

static void drawEye(int cx, int cy, float open, int lookX, int lookY, EyeStyle style) {
  const uint16_t glow = canvas.color565(18, 90, 110);
  const uint16_t iris = canvas.color565(120, 245, 255);
  const uint16_t pupil = canvas.color565(6, 16, 32);
  const uint16_t shine = canvas.color565(240, 255, 255);
  const uint16_t pink = canvas.color565(255, 90, 160);

  if (style == kEyeHappy) {
    canvas.drawWideLine(cx - 26, cy + 8, cx - 2, cy - 16, 4, iris);
    canvas.drawWideLine(cx - 2, cy - 16, cx + 22, cy + 10, 4, iris);
    canvas.fillCircle(cx + 16, cy + 4, 3, shine);
    return;
  }
  if (style == kEyeShut) {
    canvas.fillRoundRect(cx - 28, cy - 3, 56, 6, 3, iris);
    return;
  }
  if (style == kEyeDizzy) {
    canvas.fillEllipse(cx, cy, 30, 28, glow);
    canvas.drawWideLine(cx - 16, cy - 14, cx + 16, cy + 14, 3, iris);
    canvas.drawWideLine(cx - 16, cy + 14, cx + 16, cy - 14, 3, iris);
    return;
  }
  if (style == kEyeLove) {
    drawHeart(cx, cy + 2, 9, pink);
    canvas.fillCircle(cx - 4, cy - 4, 2, shine);
    return;
  }

  float o = open;
  if (style == kEyeSleepy) o *= 0.28f;
  if (style == kEyeWide) o = 1.15f;
  if (o < 0.08f) {
    canvas.fillRoundRect(cx - 28, cy - 3, 56, 6, 3, iris);
    return;
  }

  int rx = (style == kEyeWide) ? 36 : 30;
  int eh = (int)(38.0f * o);
  if (eh < 4) eh = 4;
  canvas.fillEllipse(cx, cy, rx + 6, eh + 8, glow);
  canvas.fillEllipse(cx, cy, rx, eh, iris);

  int px = cx + lookX;
  int py = cy + (int)(lookY * fminf(o, 1.0f));
  int pr = (int)((style == kEyeWide ? 8 : 12) * fminf(o, 1.0f));
  if (pr < 3) pr = 3;
  canvas.fillCircle(px, py + 2, pr, pupil);
  canvas.fillCircle(px - 4, py - (int)(5 * fminf(o, 1.0f)), 3, shine);
}

static const char *randomLine(const char **lines, int count) {
  return lines[rndRange(0, count - 1)];
}

static void showSpeech(uint32_t now, const char *text, int holdMs) {
  speech.text = text;
  speech.visible = true;
  speech.until = now + (uint32_t)holdMs;
}

static void splitTwoLines(const char *text, char *first, size_t firstLen, char *second,
                          size_t secondLen) {
  first[0] = '\0';
  second[0] = '\0';
  if (text == nullptr || text[0] == '\0') return;
  const char *comma = strchr(text, ',');
  if (comma != nullptr && comma[1] != '\0') {
    size_t n = (size_t)(comma - text) + 1;
    if (n >= firstLen) n = firstLen - 1;
    memcpy(first, text, n);
    first[n] = '\0';
    const char *rest = comma + 1;
    while (*rest == ' ') rest++;
    strncpy(second, rest, secondLen - 1);
    second[secondLen - 1] = '\0';
    return;
  }
  strncpy(first, text, firstLen - 1);
  first[firstLen - 1] = '\0';
}

static void rollSpeech(uint32_t now) {
  if (now < speech.until) return;
  speech.visible = false;
}

static const uint8_t kTouchFaces[] = {
    kHappy, kLove, kExcited, kWink, kSilly, kSurprise, kSmirk, kDizzy, kThink, kGrumpy,
};

static const char *captionFor(uint8_t face) {
  switch (face) {
    case kHappy: return "affection++";
    case kSmirk: return "heh";
    case kThink: return "hmm";
    case kSurprise: return "oh!";
    case kWink: return "wink";
    case kGrumpy: return "hmph";
    case kSilly: return "blep";
    case kLove: return "<3";
    case kExcited: return "yay";
    case kDizzy: return "wheee";
    case kGlitch: return "bzzt";
    case kSleepy: return "low power ...";
    default: return "tap to pet";
  }
}

static uint8_t nextTouchFace() {
  const int count = (int)(sizeof(kTouchFaces) / sizeof(kTouchFaces[0]));
  uint8_t pick = buddy.face;
  for (int tries = 0; tries < 8; ++tries) {
    pick = kTouchFaces[rndRange(0, count - 1)];
    if (pick != buddy.face) break;
  }
  return pick;
}

// Dims the scene to a quarter inside a rounded rect so labels read like smoked glass.
static void shadePanel(int x, int y, int w, int h, int r) {
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  int32_t cx, cy, cw, ch;
  canvas.getClipRect(&cx, &cy, &cw, &ch);
  const int x0 = clampi(x, cx, cx + cw), x1 = clampi(x + w, cx, cx + cw);
  const int y0 = clampi(y, cy, cy + ch), y1 = clampi(y + h, cy, cy + ch);
  for (int py = y0; py < y1; ++py) {
    const int ey = py < y + r ? y + r - py : (py >= y + h - r ? py - (y + h - r - 1) : 0);
    for (int px = x0; px < x1; ++px) {
      const int ex = px < x + r ? x + r - px : (px >= x + w - r ? px - (x + w - r - 1) : 0);
      if (ex * ex + ey * ey > r * r) continue;
      uint16_t c = __builtin_bswap16(buf[py * W + px]);
      buf[py * W + px] = __builtin_bswap16((c >> 2) & 0x39E7);
    }
  }
}

static constexpr int kLabelPadX = 12;
static constexpr int kLabelPadY = 4;

enum LabelSlot { kLabelTime, kLabelDate, kLabelWeather, kLabelName, kLabelCaption, kLabelCount };

struct Label {
  char text[32];
  const lgfx::GFXfont *font;
  uint16_t color;
  Box box;  // shaded panel, empty when the label is hidden
};

// Sizes a label's panel. (x, y) is the panel's outer corner named by datum.
// Returns the panel height so stacked labels can be placed below or above it.
static int placeLabel(Label &label, const char *text, int x, int y, uint8_t datum,
                      const lgfx::GFXfont *font, uint16_t color) {
  strncpy(label.text, text ? text : "", sizeof(label.text) - 1);
  label.text[sizeof(label.text) - 1] = '\0';
  label.font = font;
  label.color = color;
  label.box = {0, 0, 0, 0};
  if (label.text[0] == '\0') return 0;
  canvas.setFont(font);
  const int pw = canvas.textWidth(label.text) + kLabelPadX * 2;
  const int ph = canvas.fontHeight() + kLabelPadY * 2;
  int left = x;
  if (datum == top_right || datum == bottom_right) left = x - pw;
  int top = y;
  if (datum == bottom_left || datum == bottom_right) top = y - ph;
  label.box = {left, top, left + pw, top + ph};
  return ph;
}

// Corners: clock and date top-left, weather top-right, name and caption
// bottom-left. The middle stays clear for Himop.
static void layoutLabels(Label labels[kLabelCount]) {
  constexpr int kEdge = 12;
  constexpr int kGap = 4;
  const uint16_t white = canvas.color565(255, 255, 255);
  const uint16_t soft = canvas.color565(200, 225, 255);
  const lgfx::GFXfont *hud = &fonts::FreeSansBold18pt7b;
  const lgfx::GFXfont *small = &fonts::FreeSansBold12pt7b;
  const lgfx::GFXfont *title = &fonts::FreeSansBold24pt7b;
  // Hidden labels get empty text, so they take no space and the others close up.
  auto pick = [](uint8_t flag, const char *text) { return menuShows(flag) ? text : ""; };
  int y = kEdge;
  const int timeH = placeLabel(labels[kLabelTime], pick(kShowTime, weatherTime()), kEdge, y, top_left, hud, white);
  if (timeH > 0) y += timeH + kGap;
  placeLabel(labels[kLabelDate], pick(kShowDate, weatherDate()), kEdge, y, top_left, small, soft);
  placeLabel(labels[kLabelWeather], pick(kShowWeather, weatherCurrent()), W - kEdge, kEdge, top_right, hud,
             white);
  y = H - kEdge;
  const int captionH = placeLabel(labels[kLabelCaption], pick(kShowCaption, buddy.caption ? buddy.caption : "soaring"),
                                  kEdge, y, bottom_left, small, soft);
  if (captionH > 0) y -= captionH + kGap;
  placeLabel(labels[kLabelName], pick(kShowName, petName), kEdge, y, bottom_left, title, white);
}

// Lower-right menu button: three bars on a shaded panel. Opens Wi-Fi settings.
static void drawMenuButton() {
  shadePanel(kMenuButtonX, kMenuButtonY, kMenuButtonSize, kMenuButtonSize, 10);
  const uint16_t bar = canvas.color565(255, 255, 255);
  const int x = kMenuButtonX + 15;
  for (int i = 0; i < 3; ++i) {
    canvas.fillRoundRect(x, kMenuButtonY + 17 + i * 11, kMenuButtonSize - 30, 5, 2, bar);
  }
}

static bool inMenuButton(int x, int y) {
  constexpr int kSlop = 10;
  return x >= kMenuButtonX - kSlop && y >= kMenuButtonY - kSlop;
}

// Draws text on a shaded panel, honoring the canvas clip rect.
static void drawLabel(const Label &label) {
  if (boxEmpty(label.box)) return;
  const Box &b = label.box;
  shadePanel(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, 10);
  canvas.setFont(label.font);
  canvas.setTextDatum(middle_left);
  const int midY = (b.y0 + b.y1) / 2;
  canvas.setTextColor(canvas.color565(0, 0, 0));
  canvas.drawString(label.text, b.x0 + kLabelPadX + 2, midY + 2);
  canvas.setTextColor(label.color);
  canvas.drawString(label.text, b.x0 + kLabelPadX, midY);
}

static void drawSpeech(int hx, int hy) {
  if (!speech.visible || speech.text == nullptr) return;

  bool roar = speech.text[0] == 'R';
  canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextDatum(middle_center);
  char line1[96];
  char line2[96];
  splitTwoLines(speech.text, line1, sizeof(line1), line2, sizeof(line2));
  bool two = line2[0] != '\0' && canvas.textWidth(speech.text) > 260;
  int tw = two ? canvas.textWidth(line1) : canvas.textWidth(speech.text);
  if (two) {
    int tw2 = canvas.textWidth(line2);
    if (tw2 > tw) tw = tw2;
  }
  int th = canvas.fontHeight();
  int bw = tw + 24;
  int bh = two ? th * 2 + 18 : th + 16;

  const int crownX = himopCrownX;
  const int crownY = himopCrownY;

  bool onRight = hx <= W / 2;
  int bx = onRight ? hx + 20 : hx - bw - 20;
  int by = crownY - bh - 16;
  bx = clampi(bx, 6, W - bw - 6);
  by = clampi(by, 150, crownY - bh - 8);

  const uint16_t fill = roar ? canvas.color565(255, 236, 220) : canvas.color565(236, 252, 255);
  const uint16_t edge = roar ? canvas.color565(180, 40, 36) : canvas.color565(36, 130, 160);
  const uint16_t ink = roar ? canvas.color565(140, 16, 16) : canvas.color565(14, 40, 64);

  int attachX = onRight ? bx + 18 : bx + bw - 18;
  int attachY = by + bh - 1;
  canvas.fillTriangle(attachX - 8, attachY, attachX + 8, attachY, crownX, crownY, fill);
  canvas.drawWideLine(attachX - 8, attachY, crownX, crownY, 1.6f, edge);
  canvas.drawWideLine(attachX + 8, attachY, crownX, crownY, 1.6f, edge);

  canvas.fillRoundRect(bx, by, bw, bh, 12, fill);
  canvas.drawRoundRect(bx, by, bw, bh, 12, edge);
  markDirty(bx - 2, by - 2, bw + 4, bh + 4);

  canvas.setTextColor(ink);
  if (two) {
    canvas.drawString(line1, bx + bw / 2, by + 8 + th / 2);
    canvas.drawString(line2, bx + bw / 2, by + 10 + th + th / 2);
  } else {
    canvas.drawString(speech.text, bx + bw / 2, by + bh / 2 + 1);
  }
}

// Exploring: fly off one side, linger out of view, then come back on screen.
// Peeking happens while away: ease part way in from an edge, hold, back out.
enum Explore : uint8_t {
  kExploreNone,
  kExploreOut,
  kExploreAway,
  kExploreBack,
  kPeekIn,
  kPeekHold,
  kPeekOut,
};
static uint8_t peeksLeft = 0;
static uint8_t peekEdge = 0;
static constexpr float kPeekStep = 2.0f;
static constexpr int kPeekInset = 40;  // center this far past the edge: about a third shows
static uint8_t exploreStage = kExploreNone;
static uint32_t exploreUntil = 0;
static constexpr int kOffscreen = 170;   // past a side edge: the widest pose is 300 px
static constexpr int kOffscreenY = 130;  // past the top or bottom: the tallest pose is 190 px

enum Edge : uint8_t { kEdgeLeft, kEdgeRight, kEdgeTop, kEdgeBottom };

// A point just out of view past the given edge, at a random spot along it.
static void offscreenPoint(uint8_t edge, float *x, float *y) {
  switch (edge) {
    case kEdgeLeft: *x = -kOffscreen; *y = (float)rndRange(kMinY, kMaxY); break;
    case kEdgeRight: *x = W + kOffscreen; *y = (float)rndRange(kMinY, kMaxY); break;
    case kEdgeTop: *x = (float)rndRange(kMinX, kMaxX); *y = -kOffscreenY; break;
    default: *x = (float)rndRange(kMinX, kMaxX); *y = H + kOffscreenY; break;
  }
}
// Flight speeds in pixels per frame (about 30 frames a second).
static constexpr float kSoarStep = 3.2f;
static constexpr float kExploreStep = 4.5f;

// Depth sets his size: half size far away, full size close up.
static constexpr float kFarScale = 0.5f;
static constexpr float kNearScale = 1.0f;
static constexpr float kGroundDepth = 0.8f;
static constexpr float kDepthStep = 0.006f;  // per frame, so a full change takes about 5 s

static float himopScale() { return kFarScale + (kNearScale - kFarScale) * buddy.z; }

// Sets an on-screen target and a new random depth to drift toward.
static void placeTarget(int x, int y) {
  buddy.tx = (float)clampi(x, kMinX, kMaxX);
  buddy.ty = (float)clampi(y, kMinY, kMaxY);
  buddy.tz = rndRange(0, 100) / 100.0f;
}

// Behaviors pickNext can choose. Serial "act <name>" forces the next one.
enum Choice : int8_t { kChooseNone = -1, kChooseNap, kChooseSoar, kChooseSoarUp, kChooseExplore,
                       kChooseWalk, kChooseKooker, kChooseGraze };
static int8_t forcedChoice = kChooseNone;

// Petting keeps him around: the first tap holds him on screen for 30 s, each
// further tap adds 10 s, up to 5 minutes. Meanwhile he skips off-screen trips.
static uint32_t stayUntil = 0;
static constexpr uint32_t kStayFirstMs = 30000;
static constexpr uint32_t kStayPerTapMs = 10000;
static constexpr uint32_t kStayMaxMs = 5UL * 60UL * 1000UL;

static void addStay(uint32_t now) {
  if ((int32_t)(stayUntil - now) <= 0) {
    stayUntil = now + kStayFirstMs;
  } else {
    stayUntil += kStayPerTapMs;
    if (stayUntil - now > kStayMaxMs) stayUntil = now + kStayMaxMs;
  }
}

static int8_t rollChoice(uint32_t now) {
  if ((now - lastPetMs) > kNapAfterMs) return kChooseNap;
  const int roll = rndRange(0, 7);
  if (roll <= 5) {
    // Half of flights leave the screen: soaring climbs out the top, exploring
    // heads out a side or down the bottom.
    const int trip = rndRange(0, 5);
    if (trip < 3 || (int32_t)(stayUntil - now) > 0) return kChooseSoar;
    return trip == 3 ? kChooseSoarUp : kChooseExplore;
  }
  if (roll == 6) return kChooseWalk;
  return (rnd() % 3) == 0 ? kChooseKooker : kChooseGraze;
}

static void pickNext(uint32_t now) {
  exploreStage = kExploreNone;
  buddy.anchorX = buddy.x;
  buddy.anchorY = buddy.y;
  buddy.winkLeft = (rnd() & 1) != 0;

  const int8_t choice = forcedChoice != kChooseNone ? forcedChoice : rollChoice(now);
  forcedChoice = kChooseNone;

  // Everything but a flight happens in his play area (fully on screen). If he's
  // outside it, he flies in first, and the choice waits until he arrives.
  const bool away = buddy.x < kMinX || buddy.x > kMaxX || buddy.y < kMinY || buddy.y > kMaxY;
  const bool flight = choice == kChooseSoar || choice == kChooseSoarUp || choice == kChooseExplore;
  const bool offscreen = buddy.x < 0 || buddy.x > W || buddy.y < 0 || buddy.y > H;
  if (away && choice == kChooseNap && offscreen) {
    // Sleepy and out of view: come back on foot. He's unseen, so start him at
    // ground level just past a side edge.
    forcedChoice = choice;
    if (buddy.x >= 0 && buddy.x <= W) buddy.x = (rnd() & 1) ? -kOffscreen : W + kOffscreen;
    buddy.y = kMaxY - 8;
    buddy.z = kGroundDepth;
    buddy.action = kWander;
    buddy.face = kNeutral;
    buddy.caption = "walking back";
    placeTarget(rndRange(kMinX, kMaxX), kMaxY - 8);
    buddy.tz = kGroundDepth;
    exploreStage = kExploreBack;  // ends on arrival, then he naps
    buddy.until = now + 90000;
    return;
  }
  if (away && !flight) {
    forcedChoice = choice;
    buddy.action = kFly;
    buddy.face = kNeutral;
    buddy.caption = "soaring";
    placeTarget(rndRange(kMinX, kMaxX), rndRange(kMinY, kMaxY));
    exploreStage = kExploreBack;  // ends on arrival, then pickNext runs the choice
    buddy.until = now + 90000;
    return;
  }

  uint32_t dur = 5000;
  switch (choice) {
    case kChooseNap:
      buddy.action = kYawn;
      buddy.face = kSleepy;
      buddy.caption = "napping";
      placeTarget((int)buddy.x, (int)buddy.y + 10);
      buddy.tz = buddy.z;  // curl up where he is
      dur = kNapMs;
      break;
    case kChooseSoarUp:
    case kChooseExplore: {
      // Ends once he's back on screen (updateBuddy).
      const bool soarUp = choice == kChooseSoarUp;
      buddy.action = kFly;
      buddy.face = kNeutral;
      buddy.caption = soarUp ? "soaring" : "exploring";
      exploreStage = kExploreOut;
      peeksLeft = (uint8_t)rndRange(0, 2);
      const uint8_t edge = soarUp ? kEdgeTop : (uint8_t)rndRange(0, 2) == 2 ? kEdgeBottom : (uint8_t)rndRange(0, 1);
      offscreenPoint(edge, &buddy.tx, &buddy.ty);
      buddy.tz = rndRange(0, 100) / 100.0f;
      dur = 90000;
      break;
    }
    case kChooseWalk:
      buddy.action = kWander;
      buddy.face = kNeutral;
      buddy.caption = "on foot";
      placeTarget(rndRange(kMinX, kMaxX), kMaxY - 8);
      buddy.tz = kGroundDepth;
      dur = (uint32_t)rndRange(2800, 4200);
      break;
    case kChooseKooker: {
      buddy.action = kFlee;
      buddy.face = kSurprise;
      buddy.caption = "kooker!";
      const bool fromLeft = buddy.x >= (kMinX + kMaxX) / 2;
      kookerX = fromLeft ? 50 : W - 50;  // far enough in that the "kooker" label fits
      kookerUntil = now + 2800;
      placeTarget(fromLeft ? kMaxX : kMinX, rndRange(kMinY, kMinY + 40));
      showSpeech(now, "ROW, ROW, ROW", 2800);
      dur = 2800;
      break;
    }
    case kChooseGraze:
      buddy.action = kGraze;
      buddy.face = kNeutral;
      buddy.caption = "nibbling";
      placeTarget(rndRange(kMinX, kMaxX), kMaxY);
      buddy.tz = kGroundDepth;
      dur = (uint32_t)rndRange(2800, 4600);
      break;
    default:
      buddy.action = kFly;
      buddy.face = kNeutral;
      buddy.caption = "soaring";
      placeTarget(rndRange(kMinX, kMaxX), rndRange(kMinY, kMaxY));
      dur = (uint32_t)rndRange(4200, 7000);
      break;
  }

  buddy.until = now + dur;
  Serial.printf("action %u face %u %s\n", buddy.action, buddy.face, buddy.caption);
}

static void spawnHearts(uint32_t now) {
  if (now - lastHeartMs < 250) return;
  lastHeartMs = now;
  int spawned = 0;
  for (int i = 0; i < kMotes && spawned < 2; ++i) {
    if (motes[i].kind == 1 && motes[i].life > 0) continue;
    if (motes[i].life > 30 && motes[i].kind == 0) continue;
    spawnHeart(motes[i], buddy.x, buddy.y - 40);
    spawned++;
  }
}

static void startEnjoying(uint32_t now) {
  buddy.action = kIdle;
  buddy.face = kHappy;
  buddy.caption = "enjoys that";
  enjoyUntil = now + reactionMs;
  buddy.until = enjoyUntil;
  lastHeartMs = 0;
  spawnHearts(now);
  Serial.println("pet arrived, enjoying");
}

static void updateBuddy(uint32_t now) {
  if (buddy.until == 0 || now >= buddy.until) pickNext(now);

  if (isHappy(now)) buddy.face = kHappy;

  float step = 0.0f;
  if (buddy.action == kFly) {
    if (exploreStage == kExploreNone) step = kSoarStep;
    else if (exploreStage == kPeekHold) step = 0.0f;
    else if (exploreStage == kPeekIn || exploreStage == kPeekOut) step = kPeekStep;
    else step = kExploreStep;
  }
  else if (buddy.action == kFlee) step = 4.0f;
  else if (buddy.action == kGraze) step = 1.0f;
  else if (buddy.action == kWander) step = exploreStage == kExploreBack ? kComeWalkStep : 0.65f;
  else if (buddy.action == kCome) {
    if ((int32_t)(now - wakeUntil) < 0) {
      step = 0.0f;  // still curled up, waking
    } else {
      if (buddy.face == kSleepy) buddy.face = kNeutral;
      step = comeWalking ? kComeWalkStep : kComeFlyStep;
    }
  }

  if (step > 0.0f) {
    float dx = buddy.tx - buddy.x;
    float dy = buddy.ty - buddy.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > step) {
      buddy.x += dx / dist * step;
      buddy.y += dy / dist * step;
    } else if (buddy.action == kCome) {
      startEnjoying(now);
    } else if (buddy.action == kFly && exploreStage == kExploreOut) {
      exploreStage = kExploreAway;
      exploreUntil = now + (uint32_t)rndRange(1000, 3000);
    } else if (buddy.action == kFly && exploreStage == kPeekIn) {
      exploreStage = kPeekHold;
      exploreUntil = now + (uint32_t)rndRange(2000, 4000);
      // Face into the screen while holding: he looks the way he'd travel.
      if (peekEdge == kEdgeLeft) buddy.tx = buddy.x + 20;
      else if (peekEdge == kEdgeRight) buddy.tx = buddy.x - 20;
    } else if (buddy.action == kFly && exploreStage == kPeekOut) {
      exploreStage = kExploreAway;
      exploreUntil = now + (uint32_t)rndRange(1000, 3000);
    } else if (exploreStage == kExploreBack) {  // flew or walked back in
      exploreStage = kExploreNone;
      buddy.until = now;
    } else if (buddy.action == kFly && exploreStage == kExploreNone && (buddy.until - now) > 800) {
      placeTarget(rndRange(kMinX, kMaxX), rndRange(kMinY, kMaxY));
    }
  }

  const float dz = buddy.tz - buddy.z;
  buddy.z += fminf(fmaxf(dz, -kDepthStep), kDepthStep);

  // Done peeking: slip back out the same edge.
  if (exploreStage == kPeekHold && (int32_t)(now - exploreUntil) >= 0) {
    offscreenPoint(peekEdge, &buddy.tx, &buddy.ty);
    if (peekEdge == kEdgeLeft || peekEdge == kEdgeRight) buddy.ty = buddy.y;
    else buddy.tx = buddy.x;
    exploreStage = kPeekOut;
  }

  // Out of view: sometimes peek in, otherwise come back in from any edge.
  if (exploreStage == kExploreAway && (int32_t)(now - exploreUntil) >= 0 && peeksLeft > 0 && (rnd() & 1)) {
    peeksLeft--;
    peekEdge = (uint8_t)rndRange(0, 3);
    offscreenPoint(peekEdge, &buddy.x, &buddy.y);
    buddy.tx = buddy.x;
    buddy.ty = buddy.y;
    switch (peekEdge) {
      case kEdgeLeft: buddy.tx = -kPeekInset; break;
      case kEdgeRight: buddy.tx = W + kPeekInset; break;
      case kEdgeTop: buddy.ty = -kPeekInset / 2; break;
      default: buddy.ty = H + kPeekInset / 2; break;
    }
    buddy.caption = "peeking";
    exploreStage = kPeekIn;
    Serial.printf("peek from edge %u\n", peekEdge);
  } else if (exploreStage == kExploreAway && (int32_t)(now - exploreUntil) >= 0) {
    offscreenPoint((uint8_t)rndRange(0, 3), &buddy.x, &buddy.y);
    placeTarget(rndRange(kMinX, kMaxX), rndRange(kMinY, kMaxY));
    if (strcmp(buddy.caption, "peeking") == 0) buddy.caption = "exploring";
    exploreStage = kExploreBack;
  }
}

static void drawFace(int hx, int hy, int lookX, int lookY, float blinkOpen, uint32_t now) {
  const uint16_t blush = canvas.color565(255, 90, 150);
  const uint16_t smile = canvas.color565(140, 230, 240);
  const uint16_t pink = canvas.color565(255, 150, 190);
  const uint16_t belly = canvas.color565(8, 18, 32);
  const Face face = (Face)buddy.face;

  int jx = 0;
  int jy = 0;
  if (face == kGlitch && ((now / 90) & 1)) {
    jx = 7;
    jy = -5;
  }

  float open = blinkOpen;
  if (face == kSurprise || face == kExcited) open = 1.0f;

  EyeStyle left = kEyeOpen;
  EyeStyle right = kEyeOpen;
  switch (face) {
    case kHappy:
      left = right = kEyeHappy;
      break;
    case kSleepy:
      left = right = kEyeSleepy;
      break;
    case kSurprise:
    case kExcited:
      left = right = kEyeWide;
      break;
    case kWink:
      left = buddy.winkLeft ? kEyeShut : kEyeOpen;
      right = buddy.winkLeft ? kEyeOpen : kEyeShut;
      break;
    case kDizzy:
      left = right = kEyeDizzy;
      break;
    case kLove:
      left = right = kEyeLove;
      break;
    case kSilly:
      left = kEyeWide;
      right = kEyeHappy;
      break;
    default:
      break;
  }

  int lookYUse = lookY;
  if (face == kThink) lookYUse = -8;
  if (face == kSleepy) lookYUse = 5;
  if (face == kGrumpy) lookYUse = 3;

  drawEye(hx - 30 + jx, hy - 14, open, lookX, lookYUse, left);
  drawEye(hx + 30 - jx, hy - 14 + jy, open, lookX, lookYUse, right);

  if (face == kGrumpy || face == kThink || face == kSmirk) {
    canvas.drawWideLine(hx - 52, hy - 42, hx - 16, hy - 34, 3, smile);
    int raise = (face == kThink || face == kSmirk) ? -10 : 0;
    canvas.drawWideLine(hx + 16, hy - 34 + raise, hx + 52, hy - 44 + raise, 3, smile);
  } else if (face == kSurprise || face == kExcited) {
    canvas.drawWideLine(hx - 48, hy - 50, hx - 16, hy - 46, 3, smile);
    canvas.drawWideLine(hx + 16, hy - 46, hx + 48, hy - 50, 3, smile);
  }

  if (face == kHappy || face == kLove || face == kSilly || face == kExcited) {
    canvas.fillEllipse(hx - 54, hy + 16, 11, 6, blush);
    canvas.fillEllipse(hx + 54, hy + 16, 11, 6, blush);
  }

  switch (face) {
    case kHappy:
    case kExcited:
      drawSmile(hx, hy + 22, 26, 16, pink, 3);
      break;
    case kSleepy:
      drawSmile(hx, hy + 28, 14, 5, smile, 2);
      break;
    case kSurprise:
      canvas.fillCircle(hx, hy + 28, 10, smile);
      canvas.fillCircle(hx, hy + 28, 5, belly);
      break;
    case kWink:
    case kSmirk:
      drawSmile(hx + 6, hy + 24, 18, 8, smile, 3);
      break;
    case kDizzy:
      drawSmile(hx, hy + 26, 20, (int)(6 * sinf(now * 0.01f)), smile, 3);
      break;
    case kLove:
      drawSmile(hx, hy + 24, 22, 12, pink, 3);
      break;
    case kGrumpy:
      drawSmile(hx, hy + 36, 18, -10, smile, 3);
      break;
    case kSilly:
      drawSmile(hx, hy + 18, 20, 8, smile, 3);
      canvas.fillEllipse(hx + 4, hy + 34, 8, 10, canvas.color565(255, 110, 150));
      break;
    case kThink:
      canvas.fillRoundRect(hx - 10, hy + 24, 20, 5, 2, smile);
      break;
    case kGlitch:
      canvas.fillRect(hx - 22, hy + 22, 12, 4, smile);
      canvas.fillRect(hx - 4, hy + 28, 14, 4, canvas.color565(255, 80, 160));
      canvas.fillRect(hx + 14, hy + 20, 10, 4, smile);
      break;
    default:
      drawSmile(hx, hy + 24, 20, 11, smile, 3);
      break;
  }

  canvas.setFont(&fonts::Font4);
  canvas.setTextColor(canvas.color565(190, 255, 245));
  if (face == kThink) canvas.drawString("?", hx + 78, hy - 108);
  if (face == kSurprise) canvas.drawString("!", hx + 78, hy - 108);
  if (face == kGlitch) canvas.drawString("#", hx + 78, hy - 108);
}

static void drawHood(int hx, int hy, uint16_t cloth, uint16_t edge) {
  canvas.fillCircle(hx, hy - 6, 80, cloth);
  canvas.drawCircle(hx, hy - 6, 80, edge);
  canvas.drawCircle(hx, hy - 6, 74, edge);
}

static void drawCoat(int hx, int hy, uint16_t cloth, uint16_t edge, bool zipper) {
  canvas.fillRoundRect(hx - 74, hy + 16, 148, 78, 20, cloth);
  canvas.drawRoundRect(hx - 74, hy + 16, 148, 78, 20, edge);
  canvas.fillRoundRect(hx - 88, hy + 18, 22, 36, 8, cloth);
  canvas.fillRoundRect(hx + 66, hy + 18, 22, 36, 8, cloth);
  if (zipper) canvas.drawWideLine(hx, hy + 22, hx, hy + 86, 2, edge);
}

static void drawBoots(int foot, int cy, uint16_t boot, uint16_t edge) {
  canvas.fillRoundRect(foot - 46, cy + 76, 34, 24, 7, boot);
  canvas.fillRoundRect(foot + 12, cy + 76, 34, 24, 7, boot);
  canvas.drawRoundRect(foot - 46, cy + 76, 34, 24, 7, edge);
  canvas.drawRoundRect(foot + 12, cy + 76, 34, 24, 7, edge);
}

static void drawScarf(int hx, int hy, uint16_t cloth, uint16_t edge) {
  canvas.fillRoundRect(hx - 40, hy + 6, 80, 18, 8, cloth);
  canvas.drawRoundRect(hx - 40, hy + 6, 80, 18, 8, edge);
  canvas.fillRoundRect(hx + 16, hy + 16, 16, 32, 6, cloth);
  canvas.drawRoundRect(hx + 16, hy + 16, 16, 32, 6, edge);
}

static void drawOutfit(int hx, int hy, int cx, int cy, int foot) {
  const WeatherKind kind = weatherKind();
  const uint16_t edge = canvas.color565(20, 28, 40);
  if (kind == kWeatherNone) return;

  if (kind == kWeatherClear) {
    const uint16_t tee = canvas.color565(36, 168, 196);
    drawCoat(hx, hy + 6, tee, canvas.color565(180, 245, 255), false);
    canvas.fillCircle(hx, hy + 48, 4, canvas.color565(255, 214, 80));
  } else if (kind == kWeatherCloudy) {
    const uint16_t hoodie = canvas.color565(78, 104, 138);
    drawCoat(hx, hy, hoodie, canvas.color565(200, 214, 230), true);
    canvas.fillCircle(hx - 18, hy - 58, 16, hoodie);
    canvas.fillCircle(hx + 18, hy - 58, 16, hoodie);
  } else if (kind == kWeatherFog) {
    drawScarf(hx, hy, canvas.color565(176, 186, 196), edge);
  } else if (kind == kWeatherDrizzle || kind == kWeatherRain || kind == kWeatherShowers) {
    uint16_t coat = canvas.color565(70, 150, 196);
    if (kind == kWeatherRain) coat = canvas.color565(236, 186, 42);
    if (kind == kWeatherShowers) coat = canvas.color565(46, 118, 168);
    drawCoat(hx, hy, coat, edge, true);
    drawBoots(foot, cy, canvas.color565(28, 36, 52), edge);
  } else if (kind == kWeatherSnow) {
    const uint16_t puff = canvas.color565(214, 226, 236);
    const uint16_t berry = canvas.color565(196, 54, 84);
    drawCoat(hx, hy, puff, canvas.color565(120, 150, 170), true);
    drawScarf(hx, hy, canvas.color565(46, 150, 168), edge);
    canvas.fillCircle(hx - 78, hy + 40, 10, berry);
    canvas.fillCircle(hx + 78, hy + 40, 10, berry);
    drawBoots(foot, cy, canvas.color565(52, 64, 84), edge);
  } else if (kind == kWeatherStorm) {
    const uint16_t coat = canvas.color565(42, 32, 78);
    drawCoat(hx, hy, coat, canvas.color565(170, 150, 255), true);
    canvas.fillTriangle(hx - 8, hy + 34, hx + 6, hy + 34, hx - 2, hy + 52, canvas.color565(255, 214, 64));
    canvas.fillTriangle(hx + 2, hy + 50, hx + 14, hy + 50, hx + 4, hy + 70, canvas.color565(255, 214, 64));
    drawBoots(foot, cy, canvas.color565(24, 20, 40), edge);
  }

  (void)cx;
}

static void drawSunglasses(int hx, int hy) {
  if (weatherKind() != kWeatherClear) return;
  const uint16_t frame = canvas.color565(24, 28, 36);
  const uint16_t lens = canvas.color565(70, 150, 170);
  canvas.fillRoundRect(hx - 50, hy - 24, 40, 18, 6, frame);
  canvas.fillRoundRect(hx + 10, hy - 24, 40, 18, 6, frame);
  canvas.fillRoundRect(hx - 46, hy - 21, 32, 12, 4, lens);
  canvas.fillRoundRect(hx + 14, hy - 21, 32, 12, 4, lens);
  canvas.drawWideLine(hx - 10, hy - 16, hx + 10, hy - 16, 2, frame);
}

// Sprite buffers hold byte-swapped RGB565. Card frames are swapped at load;
// the built-in art is native order, so it passes swap = true.
// Draws a pose scaled about its center (nearest neighbor), optionally mirrored.
static void blitHimop(int cx, int cy, const HimopFrame &frame, bool mirror, bool swap, float scale) {
  uint16_t *dst = (uint16_t *)canvas.getBuffer();
  const int stride = canvas.width();
  const int dw = frame.w * scale + 0.5f > 1 ? (int)(frame.w * scale + 0.5f) : 1;
  const int dh = frame.h * scale + 0.5f > 1 ? (int)(frame.h * scale + 0.5f) : 1;
  const int x0 = cx - dw / 2;
  const int y0 = cy - dh / 2;
  const uint32_t stepX = ((uint32_t)frame.w << 16) / dw;
  const uint32_t stepY = ((uint32_t)frame.h << 16) / dh;
  for (int y = 0; y < dh; ++y) {
    const int dy = y0 + y;
    if ((unsigned)dy >= (unsigned)H) continue;
    const int row = (int)((y * stepY) >> 16) * frame.w;
    for (int x = 0; x < dw; ++x) {
      const int dx = x0 + x;
      if ((unsigned)dx >= (unsigned)W) continue;
      int sx = (int)((x * stepX) >> 16);
      if (mirror) sx = frame.w - 1 - sx;
      const int i = row + sx;
      if ((frame.mask[i >> 3] & (0x80 >> (i & 7))) == 0) continue;
      dst[dy * stride + dx] = swap ? __builtin_bswap16(frame.px[i]) : frame.px[i];
    }
  }
  markDirty(x0, y0, dw, dh);
  himopCrownX = mirror ? x0 + dw - dw / 5 : x0 + dw / 5;
  himopCrownY = y0 + dh / 7;
  himopSpriteH = dh;
}

static void blitCard(int cx, int cy, const HimopCardFrame &f, bool mirror) {
  HimopFrame frame = {f.px, f.mask, f.w, f.h};
  blitHimop(cx, cy, frame, mirror, false, himopScale());
}

static void drawHimop(int hx, int hy, uint32_t now) {
  const bool curled = buddy.face == kSleepy || buddy.action == kYawn;
  const bool mirror = !curled && buddy.tx > buddy.x + 8.0f;
  int index = 0;
  if (curled) {
    index = 6;
  } else if (buddy.action == kFlee) {
    index = 8;
  } else if (isHappy(now) || buddy.face == kHappy) {
    index = 7;
  } else if (buddy.action == kWander || (buddy.action == kCome && comeWalking)) {
    index = ((now / 220) & 1) ? 5 : 4;
  } else if (buddy.action == kGraze) {
    const uint32_t left = buddy.until > now ? buddy.until - now : 0;
    if (left < 900) index = 11;
    else index = ((now / 280) & 1) ? 10 : 9;
  } else if (buddy.action == kFly || buddy.action == kCome) {
    index = 12 + (int)((now / 120) % 6);
  } else {
    index = ((now / 900) & 1) ? 1 : 0;
  }

  if (haveCardSprites && index < 18 && cardFrames[index].px != nullptr) {
    blitCard(hx, hy, cardFrames[index], mirror);
  } else {
    const HimopFrame &frame = curled ? himopNap : himopFly;
    blitHimop(hx, hy, frame, mirror, true, himopScale());
  }
}

// Areas to push to the panel this frame, filled by drawBuddy.
static constexpr int kMaxPush = 2 + kLabelCount * 2;
static Box pushBoxes[kMaxPush];
static int pushCount = 0;

static void drawGrid(uint32_t now) {
  const uint16_t grid = canvas.color565(10, 16, 24);
  const uint16_t gridHot = canvas.color565(18, 36, 48);
  canvas.fillScreen(canvas.color565(0, 0, 0));
  for (int x = 24; x < W; x += 28) canvas.drawFastVLine(x, 0, H, grid);
  for (int y = 20; y < H; y += 28) canvas.drawFastHLine(0, y, W, grid);
  canvas.drawFastVLine((int)((now / 18) % W), 0, H, gridHot);
}

// Copies the landscape back over one box and redraws the labels clipped to it.
static void restoreBox(const Box &b, const uint16_t *scene, const Label labels[kLabelCount]) {
  if (boxEmpty(b)) return;
  uint16_t *buf = (uint16_t *)canvas.getBuffer();
  const size_t rowBytes = (size_t)(b.x1 - b.x0) * 2;
  for (int y = b.y0; y < b.y1; ++y) memcpy(buf + y * W + b.x0, scene + y * W + b.x0, rowBytes);
  canvas.setClipRect(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
  for (int i = 0; i < kLabelCount; ++i) drawLabel(labels[i]);
  drawMenuButton();
  canvas.clearClipRect();
}

static void addPush(const Box &b) {
  if (!boxEmpty(b) && pushCount < kMaxPush) pushBoxes[pushCount++] = b;
}

// Set when something else (the menu) has drawn over the canvas.
static bool needFullRedraw = true;

static void drawBuddy(uint32_t now) {
  static Box prevDyn = {0, 0, 0, 0};
  static Label shown[kLabelCount] = {};
  static const uint16_t *shownScene = nullptr;
  const bool happy = isHappy(now);
  const bool sleepy = buddy.face == kSleepy;

  int hour = weatherHour();
  int bgIndex = -1;
  if (hour >= 5 && hour < 12) bgIndex = 0;
  else if (hour >= 12 && hour < 17) bgIndex = 1;
  else if (hour >= 17 && hour < 21) bgIndex = 2;
  else if (hour >= 0) bgIndex = 3;
  const uint16_t *scene = (haveBackgrounds && bgIndex >= 0) ? dayBg[bgIndex] : nullptr;

  Label labels[kLabelCount];
  layoutLabels(labels);

  // With a landscape, only last frame's moving area and any changed labels are
  // restored. The grid fallback animates everywhere, so it redraws in full.
  pushCount = 0;
  const Box whole = {0, 0, W, H};
  if (scene == nullptr) {
    drawGrid(now);
    for (int i = 0; i < kLabelCount; ++i) drawLabel(labels[i]);
    drawMenuButton();
    addPush(whole);
  } else if (scene != shownScene || needFullRedraw) {
    restoreBox(whole, scene, labels);
    addPush(whole);
  } else {
    restoreBox(prevDyn, scene, labels);
    addPush(prevDyn);
    for (int i = 0; i < kLabelCount; ++i) {
      if (strcmp(labels[i].text, shown[i].text) == 0) continue;
      restoreBox(shown[i].box, scene, labels);
      restoreBox(labels[i].box, scene, labels);
      addPush(shown[i].box);
      addPush(labels[i].box);
    }
  }
  shownScene = scene;
  needFullRedraw = false;
  memcpy(shown, labels, sizeof(shown));
  dynBox = {0, 0, 0, 0};

  // Asleep, he lies still; a 2 px bob in whole-pixel steps reads as jitter.
  int bob = sleepy ? 0 : (int)(sinf(now * 0.0022f) * 5.0f);
  int hop = 0;
  const bool airborne = buddy.action == kFly || buddy.action == kFlee || (buddy.action == kCome && !comeWalking);
  if (airborne) {
    hop = 6 + (int)(sinf(now * 0.0032f) * 4.0f);
  }
  if (happy) hop += 6;

  int sway = airborne ? (int)(sinf(now * 0.002f) * 4.0f) : 0;

  const int cx = (int)buddy.x;
  const int cy = (int)buddy.y - hop + bob;
  const int hx = cx + sway;
  const int hy = cy;

  int shadowRx = (int)((46 - hop / 5) * himopScale());
  if (shadowRx < 14) shadowRx = 14;
  const int shadowY = hy + himopSpriteH / 2 - 6;
  canvas.fillEllipse(cx, shadowY, shadowRx, 8, canvas.color565(8, 8, 12));
  markDirty(cx - shadowRx - 1, shadowY - 9, shadowRx * 2 + 3, 19);

  if (now < kookerUntil) {
    const uint16_t kook = canvas.color565(48, 18, 72);
    canvas.fillEllipse(kookerX, hy + 10, 22, 16, kook);
    canvas.fillCircle(kookerX - 6, hy + 6, 3, canvas.color565(230, 220, 80));
    canvas.fillCircle(kookerX + 6, hy + 6, 3, canvas.color565(230, 220, 80));
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(canvas.color565(255, 236, 250));
    canvas.drawString("kooker", kookerX, hy + 36);
    markDirty(kookerX - 60, hy - 10, 120, 64);
  }

  drawHimop(hx, hy, now);

  for (int i = 0; i < kMotes; ++i) {
    Mote &m = motes[i];
    if (m.life == 0) continue;
    // Drifting sparks belong to the grid. Over a landscape they would dirty the whole screen.
    if (m.kind == 0 && scene != nullptr) {
      m.life = 0;
      continue;
    }
    m.y += m.vy;
    m.life--;
    if (m.kind == 1) {
      drawHeart((int)m.x, (int)m.y, 5, canvas.color565(255, 120 + (m.life % 40), 180));
      markDirty((int)m.x - 12, (int)m.y - 7, 24, 25);
    } else {
      uint8_t a = (uint8_t)(40 + m.life);
      if (a > 180) a = 180;
      canvas.fillCircle((int)m.x, (int)m.y, (m.life > 20) ? 2 : 1, canvas.color565(a / 3, a, a));
    }
    if (m.life == 0 || m.y < -10 || m.y > H + 10) {
      if (!happy || m.kind == 0) spawnSpark(m);
      else m.life = 0;
    }
  }

  drawSpeech(hx, hy);

  if (sleepy) {
    // One "z" rises 40 px every 2.4 s, then starts again.
    const int zy = hy - 70 - (int)((now % 2400) * 40 / 2400);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(canvas.color565(255, 255, 255));
    canvas.drawString("z", hx + 54, zy);
    markDirty(hx + 30, zy - 30, 50, 60);
  }

  addPush(dynBox);
  prevDyn = dynBox;
}

static void petTouch(uint32_t now, int x, int y, bool newTap) {
  lastPetMs = now;
  seenTouch = true;
  if (newTap) addStay(now);

  // Already here: keep enjoying while the finger stays or taps again.
  if (isHappy(now)) {
    enjoyUntil = now + reactionMs;
    buddy.until = enjoyUntil;
    spawnHearts(now);
    return;
  }

  if (buddy.action != kCome) {
    const bool napping = buddy.face == kSleepy || buddy.action == kYawn;
    const bool onScreen = buddy.x > 0 && buddy.x < W && buddy.y > 0 && buddy.y < H;
    comeWalking = onScreen && (buddy.action == kWander || buddy.action == kGraze);
    exploreStage = kExploreNone;
    kookerUntil = 0;
    speech.visible = false;
    buddy.action = kCome;
    buddy.anchorX = buddy.x;
    buddy.anchorY = buddy.y;
    buddy.until = now + 60000;  // ends on arrival (updateBuddy)
    if (napping) {
      buddy.face = kSleepy;
      wakeUntil = now + 900;
      buddy.caption = "waking up";
    } else {
      buddy.face = kNeutral;
      wakeUntil = now;
      buddy.caption = comeWalking ? "walking over" : "flying over";
    }
    Serial.printf("pet called, %s\n", buddy.caption);
  }
  // Head for the touch; on foot he stays on the ground.
  placeTarget(x, comeWalking ? kMaxY - 8 : y);
  buddy.tz = 1.0f;  // comes right up close to you
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("cyber buddy boot");
  Serial.printf("PSRAM bytes: %u\n", (unsigned)ESP.getPsramSize());

  rngState ^= micros();
  for (int i = 0; i < kMotes; ++i) spawnSpark(motes[i]);

  buddy.z = buddy.tz = kGroundDepth;
  buddy.x = buddy.tx = buddy.anchorX = 240;
  buddy.y = buddy.ty = buddy.anchorY = 250;
  buddy.face = kNeutral;
  buddy.action = kFly;
  buddy.caption = "soaring";
  buddy.until = 0;

  const bool displayOk = panelBegin();
  const bool touchOk = touchBegin();
  Serial.printf("display init %s, touch %s\n", displayOk ? "ok" : "FAILED", touchOk ? "ok" : "FAILED");

  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if (!canvas.createSprite(W, H)) {
    Serial.println("sprite alloc failed");
  } else {
    Serial.println("sprite ok");
  }

  bootMs = millis();
  lastPetMs = bootMs;

  if (loadPetCard(petName, sizeof(petName), &reactionMs, petDescription, sizeof(petDescription),
                  phraseStore, 24, &kindCount, petStore, 12, &petLineCount)) {
    for (int i = 0; i < kindCount; ++i) phrasePtrs[i] = phraseStore[i];
    if (kindCount > 0) kindLines = phrasePtrs;
    for (int i = 0; i < petLineCount; ++i) petLinePtrs[i] = petStore[i];
    Serial.printf("card name '%s' hold %u phrases %d pet lines %d\n", petName, (unsigned)reactionMs,
                  kindCount, petLineCount);
    haveCardSprites = loadHimopSprites(cardFrames, 18);
    haveBackgrounds = loadDayBackgrounds(dayBg, 4);
    Serial.println(haveCardSprites ? "card sprites ok" : "card sprites missing");
    Serial.println(haveBackgrounds ? "card backgrounds ok" : "card backgrounds missing");
  }
  speech.visible = false;
  menuBegin(&canvas);
  weatherBegin();
}

// Frame capture for pixel-exact screenshots. The header goes out at the normal
// rate; the raw canvas (480x480, byte-swapped RGB565) at kDumpBaud in 1 KB
// chunks, each followed by its Adler-32. The host answers 'A' (good) or 'R'
// (resend). The USB serial bridge drops bytes at high rates, and this keeps a
// single chunk in flight.
static constexpr uint32_t kDumpBaud = 1000000;
static constexpr size_t kDumpChunk = 1024;
static bool shotPending = false;
static int recordLeft = 0;
static int recordEvery = 1;
static int recordTick = 0;
static uint32_t recordNow = 0;

static uint32_t adler32(const uint8_t *p, size_t n) {
  uint32_t a = 1, b = 0;
  while (n--) {
    a = (a + *p++) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

static void dumpCanvas() {
  Serial.printf("SHOT %d %d\n", W, H);
  Serial.flush();
  Serial.updateBaudRate(kDumpBaud);
  delay(300);  // time for the host to switch rates
  while (Serial.available()) Serial.read();
  const uint8_t *data = (const uint8_t *)canvas.getBuffer();
  const size_t total = (size_t)W * H * 2;
  for (size_t off = 0; off < total; off += kDumpChunk) {
    const size_t len = total - off < kDumpChunk ? total - off : kDumpChunk;
    const uint32_t sum = adler32(data + off, len);
    for (int tries = 0; tries < 10; ++tries) {
      Serial.write(data + off, len);
      Serial.write((const uint8_t *)&sum, 4);
      Serial.flush();
      // Wait longer than the host's read timeout, so a resend never overlaps.
      const uint32_t until = millis() + 2000;
      int reply = -1;
      while (reply < 0 && (int32_t)(millis() - until) < 0) reply = Serial.read();
      if (reply == 'A') break;
      delay(20);
      while (Serial.available()) Serial.read();
    }
  }
  delay(100);
  Serial.updateBaudRate(115200);
  delay(50);
}

void loop() {
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (recordLeft > 0) {
    // Recording runs on its own clock so every frame is 33 ms apart, however
    // long each dump takes.
    recordNow += 33;
    now = recordNow;
  } else {
    if (now - lastFrame < 33) return;
    lastFrame = now;
  }

  // Buttons act on the touch-down edge; petting continues while held.
  static bool wasDown = false;
  int tx = 0;
  int ty = 0;
  bool down = touchRead(&tx, &ty);
  bool pressed = down && !wasDown;
  wasDown = down;
  // Serial test commands (see pickupandgo.md):
  //   tap X Y      simulate a touch
  //   act NAME     force the next behavior (nap soar soarup explore walk kooker graze)
  //   shot         dump the current frame
  //   rec N [K]    dump N frames on a fixed 33 ms clock, one every K ticks
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    int sx, sy, n;
    if (sscanf(line.c_str(), "tap %d %d", &sx, &sy) == 2) {
      tx = sx;
      ty = sy;
      pressed = true;
      down = true;
    } else if (line.startsWith("act ")) {
      static const char *kNames[] = {"nap", "soar", "soarup", "explore", "walk", "kooker", "graze"};
      const String name = line.substring(4);
      for (int i = 0; i < 7; ++i) {
        if (name == kNames[i]) {
          forcedChoice = (int8_t)i;
          buddy.until = now;  // pick it on this frame
          wakeUntil = enjoyUntil = now;
        }
      }
    } else if (line == "shot") {
      shotPending = true;
    } else if (sscanf(line.c_str(), "rec %d", &n) == 1 && n > 0) {
      int k = 1;
      sscanf(line.c_str(), "rec %*d %d", &k);
      recordLeft = n;
      recordEvery = k > 0 ? k : 1;
      recordTick = 0;
      recordNow = now;
    }
  }
  if (pressed) Serial.printf("tap %d,%d\n", (int)tx, (int)ty);

  if (menuIsOpen()) {
    if (pressed) menuPress(tx, ty);
    weatherTick(now);
    if (!menuTick(now)) needFullRedraw = true;
    if (shotPending) {
      shotPending = false;
      dumpCanvas();
    }
    return;
  }
  if (pressed && inMenuButton(tx, ty)) {
    menuOpen();
    menuTick(now);
    return;
  }

  if (down) petTouch(now, tx, ty, pressed);
  updateBuddy(now);
  rollSpeech(now);
  if (recordLeft == 0) weatherTick(now);
  drawBuddy(now);
  if (shotPending) {
    shotPending = false;
    dumpCanvas();
  } else if (recordLeft > 0 && (recordTick++ % recordEvery) == 0) {
    recordLeft--;
    dumpCanvas();
  }
  panelWaitVsync(30);
  const uint16_t *pixels = (const uint16_t *)canvas.getBuffer();
  for (int i = 0; i < pushCount; ++i) {
    const Box &b = pushBoxes[i];
    panelCopy(pixels, b.x0, b.y0, b.x1, b.y1);
  }
}
