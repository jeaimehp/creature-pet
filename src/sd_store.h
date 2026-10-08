#pragma once

#include <stddef.h>
#include <stdint.h>

// Reads /byte/settings.txt, /byte/phrases.txt, and /byte/pet.txt from the
// Guition TF slot (SPI: CS 42, MOSI 47, MISO 41, SCK 48).
// Call this after the display has been initialized.
bool loadPetCard(char *name, size_t nameLen, uint32_t *holdMs, char *description,
                 size_t descriptionLen, char phrases[][32], int phraseCap, int *phraseCount,
                 char pets[][32], int petCap, int *petCount);

struct HimopCardFrame {
  uint16_t *px;
  uint8_t *mask;
  int w;
  int h;
};

// Loads /byte/sprites/0.bin through 6.bin. Call only after loadPetCard succeeds.
bool loadHimopSprites(HimopCardFrame *frames, int count);

// Loads /byte/bg/morning.bin, afternoon.bin, evening.bin, and night.bin.
// Each slot is a 480x480 RGB565 buffer in PSRAM, or null if that file is missing.
bool loadDayBackgrounds(uint16_t **slots, int count);
