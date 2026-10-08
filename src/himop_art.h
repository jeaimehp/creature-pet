#pragma once
#include <stdint.h>
struct HimopFrame { const uint16_t *px; const uint8_t *mask; int w; int h; };
extern const uint16_t himopFly_px[95940];
extern const uint8_t himopFly_mask[11993];
extern const HimopFrame himopFly;
extern const uint16_t himopNap_px[55890];
extern const uint8_t himopNap_mask[6987];
extern const HimopFrame himopNap;
