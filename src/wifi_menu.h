#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// On-screen settings. Wi-Fi: pick a scanned network or type a hidden one, then
// enter the password on a touch keyboard (saved through weatherSetWifi()).
// Display: checkboxes for which labels show, saved in NVS.

// Lower-right menu button on the pet screen.
static constexpr int kMenuButtonSize = 60;
static constexpr int kMenuButtonX = 480 - 12 - kMenuButtonSize;
static constexpr int kMenuButtonY = 480 - 12 - kMenuButtonSize;

enum ShowFlag : uint8_t {
  kShowTime = 1,
  kShowDate = 2,
  kShowWeather = 4,
  kShowName = 8,
  kShowCaption = 16,  // the activity line under the name ("soaring", "napping")
};

void menuBegin(LGFX_Sprite *canvas);
bool menuShows(uint8_t flag);
bool menuIsOpen();
void menuOpen();
// Call on each new touch (finger down) while the menu is open.
void menuPress(int x, int y);
// Redraws and pushes when something changed. Returns false once the menu closes.
bool menuTick(uint32_t now);
