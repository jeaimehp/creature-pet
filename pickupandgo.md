# Pickup and Go — Guition cyber buddy

Living plan for the cute cyber buddy on the Guition ESP32-4848S040.
Update this file as each step finishes.

## Status

**Not ready to move the card.** The board is flashed for 16-bit RGB565, the six flying poses, and the four landscapes. The last time the NO NAME card was mounted, sprites `0.bin`–`17.bin` and morning, afternoon, and evening were on it. `night.bin` was missing, and the card was unplugged before that file could be copied back.

Put **NO NAME** in the Mac again. After `night.bin` is on the card, eject it, put it in the TF slot above the USB port, then unplug the board and plug it back in.

After that reboot, Himop should:

- Soar by cycling the six flying poses in `12.bin`–`17.bin`
- Walk, rarely, by alternating the two step frames
- Nap curled up with his eyes closed
- Nibble by biting leaves, chewing, then looking full
- Roar “ROW, ROW, ROW” only when a kooker appears
- Show the happy pose when tapped, and wake up first if he was napping
- Show the current temperature and conditions, with no forecast
- Show the name, clock, date, weather, and caption in large bold type on dark labels. The description is not shown.
- Use the landscape that matches the clock: morning 5:00–11:59, afternoon 12:00–4:59, evening 5:00–8:59, night otherwise

## Sprites

Files live at `/byte/sprites/` on the card and in `sdcard/byte/sprites/` in this repo. Each `.bin` is width, height, RGB565 pixels, then a 1-bit mask.

| File | Pose |
| --- | --- |
| `0.bin` | Idle, mouth open |
| `1.bin` | Idle, mouth closed |
| `2.bin` | Flying |
| `3.bin` | Flying angled |
| `4.bin` | Walking 1 |
| `5.bin` | Walking 2 |
| `6.bin` | Sleeping |
| `7.bin` | Happy |
| `8.bin` | Threatened, the roar |
| `9.bin` | Eating |
| `10.bin` | Chewing |
| `11.bin` | Finished eating |
| `12.bin`–`17.bin` | Flying, six wing positions. He cycles these while soaring. |

## Goal

A Himop desk-pet on the Guition ESP32-4848S040 at `/dev/tty.usbserial-12430`.

He is a herbivore people ride, about 12 feet 6 inches tall, and he flies more than he walks. Left alone he soars at a medium float. He does not talk. The only sound is the loud “ROW, ROW, ROW” when a kooker threatens him. A tap is a pet, not a threat.

Settings on the card, folder `/byte/`:

- `settings.txt` holds `name`, `hold_ms`, and `description`
- `phrases.txt` and `pet.txt` are still read, and he does not speak them
- `sprites/0.bin` through `sprites/17.bin` are the poses. `12.bin`–`17.bin` are the flying cycle.
- `bg/morning.bin`, `afternoon.bin`, `evening.bin`, and `night.bin` are 480×480 RGB565 landscapes. The clock picks one: morning 5:00–11:59, afternoon 12:00–4:59, evening 5:00–8:59, night otherwise.

Wi-Fi credentials are in `include/secrets.h`. That file is gitignored. The password is not written here. On 2026-10-07 the board joined Wi-Fi. The screen shows the current conditions only.

## Hardware

| Item | Value |
| --- | --- |
| Board | Guition ESP32-4848S040 |
| MCU | ESP32-S3, 16 MB QIO flash, 8 MB OPI PSRAM (N16R8) |
| Panel | 4.0 inch, 480×480, ST7701, 16-bit RGB |
| Panel init | 3-wire SPI, CS 39, SCK 48, SDA 47 |
| Touch | GT911 on I2C, SDA 19, SCL 45 (address 0x5D or 0x14) |
| Backlight | GPIO 38, PWM, active high |
| USB serial | CH340, not native USB CDC |
| Port | `/dev/tty.usbserial-12430` (confirmed present) |

RGB data pins:

- R0–R4: 11, 12, 13, 14, 0
- G0–G5: 8, 20, 3, 46, 9, 10
- B0–B4: 4, 5, 6, 7, 15
- DE 18, VSYNC 17, HSYNC 16, PCLK 21

Timing to start with (the usual working set for this panel): PCLK 14 MHz, H/V polarity 1, front porch 10, pulse 8, H back porch 50, V back porch 20.

## Approach

PlatformIO Arduino project using LovyanGFX and its built-in `Panel_ST7701_guition_esp32_4848S040` panel. Draw the buddy into a 480×480 RGB565 sprite in PSRAM, then push that sprite each frame. Any touch counts as a pet, so touch rotation does not have to be perfect.

USB CDC stays off so GPIO 19 and GPIO 20 remain free for touch and the green data bus. The CH340 is the serial console.

Toolchain: `pio` at `/Users/jeaimehp/Library/Python/3.11/bin/pio` (not on the default PATH).

Platform is pioarduino `55.03.312` (Arduino 3.3.12 / ESP-IDF 5.5). LovyanGFX only compiles the RGB panel when `esp_lcd_panel_rgb.h` exists, which Arduino 2 does not ship. Board id `esp32-s3-devkitc1-n16r8` sets `memory_type = qio_opi` and `BOARD_HAS_PSRAM`.

TF card, after the display is up and CS 39 is held high: CS 42, MOSI 47, MISO 41, SCK 48. FAT32 only.

## Steps

- [x] Confirm `/dev/tty.usbserial-12430` exists
- [x] Install PlatformIO 6.2.0
- [x] Add `platformio.ini` for ESP32-S3 N16R8 (QIO flash, OPI PSRAM, CDC off)
- [x] Add the LovyanGFX board config (pins, timing, backlight, GT911)
- [x] Draw and animate Himop
- [x] Build
- [x] Flash to `/dev/tty.usbserial-12430`
- [x] Read the serial boot log (PSRAM size, display init, sprite alloc)
- [x] Save the 12 labeled poses on the NO NAME card
- [x] Flash the loader that plays those poses
- [x] Confirm Wi-Fi connects and the current conditions show
- [x] Flash 16-bit RGB565 panel mode and the six-pose flying cycle
- [ ] Copy `night.bin` back onto the NO NAME card
- [ ] Move the card into the TF slot and reboot so the poses and landscapes appear

## Reflash later

```bash
export PATH="$HOME/Library/Python/3.11/bin:$PATH"
cd /Users/jeaimehp/Documents/esp32-buddy
pio run -t upload --upload-port /dev/tty.usbserial-12430
pio device monitor --port /dev/tty.usbserial-12430 --baud 115200
```

If upload cannot reset the board, hold BOOT, tap RESET, release BOOT, and run the upload again.

## If the picture is wrong

- Blank screen: the backlight boost on GPIO 38 stays off if it is PWM'd around 20 kHz. Firmware now drives that pin high after init. A dark mirror in a photo means the backlight is still off.
- Shifted or torn image: change H/V polarity or the back porches in `src/lgfx_board.hpp`.
- Wrong colors: the 16 data wires are RGB565. The stock Guition init leaves the ST7701 in RGB666 (`0x3A` = `0x60`). Firmware now sends `0x50` instead. Do not switch that byte back.
- Touch feels ignored: any touch should still extend the happy timer. If none register, try GT911 address 0x14 and `offset_rotation`.

## Log

- 2026-10-07: Port confirmed. PlatformIO 6.2.0 installed with `pip install --user`.
- 2026-10-07: Added `platformio.ini`, `src/lgfx_board.hpp`, and `src/main.cpp` (Byte: idle, blink, pet, sleepy). Build started.
- 2026-10-07: First build failed. Stock PlatformIO espressif32 7.1.3 is Arduino 2.0.17 and does not provide `esp_lcd_panel_rgb.h`, so `Bus_RGB` was compiled out. esptool also lacked the `intelhex` module. PSRAM was not selected (board default is N8, no PSRAM).
- 2026-10-07: Retargeted to pioarduino 55.03.312 and board `esp32-s3-devkitc1-n16r8` (16 MB QIO + 8 MB OPI). Rebuild started.
- 2026-10-07: Build succeeded. Flash image about 407 KB. RAM 7.9%, flash partition 6.2%. Upload started.
- 2026-10-07: Flashed and verified. Chip is ESP32-S3 rev v0.2, 16 MB flash, 8 MB embedded PSRAM, MAC `3c:84:27:c9:66:b4`. Boot log:

```
cyber buddy boot
PSRAM bytes: 8388608
display init ok
sprite ok
```

ROM reported flash mode DIO. That is what this platform writes for QIO boards, and boot still succeeded with the full 8 MB of PSRAM.
- 2026-10-07: C920 photo showed a dark mirror, no image. Cause was GPIO 38 PWM at 20 kHz. Forced the backlight pin high, flashed again, and the next photo shows Byte on the grid.
- 2026-10-07: Initialized a local git repo on `main`. No commits yet.
- 2026-10-07: Flashed roaming, faces, and speech bubbles. Bubbles say short positive lines and sit beside the head. A new tap picks a warmer line.
- 2026-10-07: Moved the bubble tail from eye level to the crown of the head and flashed it.
- 2026-10-07: Taps now pick a new face. Wrote `/byte/settings.txt`, `phrases.txt`, and `pet.txt` onto the FAT32 card "NO NAME" and flashed the reader (CS 42, MOSI 47, MISO 41, SCK 48). The card is still in the Mac, so the board is using built-in lines until it is moved to the TF slot.
- 2026-10-07: Renamed the pet from Byte to Himop in the firmware default and in `name=` on the card.
- 2026-10-07: Added Himop’s description under the name and a flying drift. The line is also `description=` in `/byte/settings.txt`.
- 2026-10-07: Replaced the round robot with Himop. He soars, naps, rarely walks, nibbles, and only calls “ROW, ROW, ROW” when a kooker appears. A tap wakes him and he enjoys it.
- 2026-10-07: Saved Wi-Fi credentials in `include/secrets.h` and flashed so the board can fetch weather. The password is not written in this file.
- 2026-10-07: The screen shows the current temperature and conditions only. The forecast line is gone.
- 2026-10-07: Replaced the drawn-with-shapes Himop with the flying picture, and the curled picture while he naps.
- 2026-10-07: Saved the sprite sheet as `/byte/sprites/0.bin`–`6.bin` on the NO NAME card. The board plays them after that card is inserted and it reboots.
- 2026-10-07: Replaced those with the 12 labeled poses. Flying alternates the two flight frames, walking alternates the two step frames, and eating goes from bite to chew to finished.
- 2026-10-07: Brought this plan up to date. Next human step is to move the NO NAME card into the TF slot and power-cycle the board.
- 2026-10-07: Confirmed Wi-Fi. The board joined Wi-Fi. The first weather read was `0F clear` because the parser hit the units label. Parsing now starts at the `current` object.
- 2026-10-07: Saved four 480×480 landscapes at `/byte/bg/morning.bin`, `afternoon.bin`, `evening.bin`, and `night.bin`. The board copies the one that matches the local hour once the card is in the TF slot.
- 2026-10-07: Removed the description from the screen. Name, clock, date, weather, and caption use a large bold font on dark labels.
- 2026-10-07: Split the labels onto their own rows so the name, clock, date, weather, and caption no longer overlap.
- 2026-10-07: Sprites and landscapes are shown at half the file size. Landscapes are stretched back to the full screen as larger pixels.
- 2026-10-07: The panel bus is 16-bit RGB565, but the controller was left in 18-bit RGB666. Init now selects RGB565, and the pictures are copied one pixel to one pixel again.
- 2026-10-07: Rewrote the card pictures as 16-bit RGB565. Landscapes are exactly 480×480. Sprites are capped so they fit between the labels. Flying cycles six new poses in `/byte/sprites/12.bin` through `17.bin`.
- 2026-10-07: Replaced the four landscapes with the new 16-bit scenes, scaled to 480×480 RGB565 on the card.
- 2026-10-07: Last card check found sprites `0.bin`–`17.bin` plus morning, afternoon, and evening. `night.bin` was not on the card. The card was unplugged before it could be copied. Next step is to mount NO NAME again, add `night.bin`, then move the card to the TF slot.
