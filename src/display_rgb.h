#pragma once

#include <stdint.h>

// Drives the ST7701 through ESP-IDF's RGB panel driver with bounce buffers, so
// the panel's DMA reads internal SRAM instead of competing for PSRAM. Pixels use
// LovyanGFX sprite byte order (byte-swapped RGB565), so a canvas copies as is.

bool panelBegin();

// Copies a rectangle of a 480-wide canvas into the panel framebuffer.
void panelCopy(const uint16_t *canvas, int x0, int y0, int x1, int y1);

// Blocks until the next vertical sync, or timeoutMs. Copies made right after
// vsync stay ahead of the scan, so moving pictures don't tear.
void panelWaitVsync(uint32_t timeoutMs);

bool touchBegin();
// True while a finger is down; x and y are screen pixels.
bool touchRead(int *x, int *y);
