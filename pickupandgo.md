# Pickup and Go — Guition cyber buddy

Living plan for the cute cyber buddy on the Guition ESP32-4848S040.
Update this file as each step finishes.

## Status

**Running.** The NO NAME card is in the TF slot, and all 18 sprites and four landscapes load. The panel runs on ESP-IDF `esp_lcd` with bounce buffers and stays steady with Wi-Fi active. README screenshots are pixel-exact framebuffer dumps (`tools/capture.py`).

Open items:
- Real-finger touch has only been tested lightly; most testing used serial `tap`. If taps land in the wrong place, compare the `tap x,y` log lines with where you touched.
- `sdcard/byte/sprites/12.bin` and `13.bin` in the repo have the wing-gap fix, but the card in the board still has the old files. The firmware fixes them at load, so copying is optional.

Himop should:

- Soar by cycling the six flying poses in `12.bin`–`17.bin`
- Walk, rarely, by alternating the two step frames
- Nap curled up with his eyes closed, only on screen (he walks back in first if away), and only after 5 minutes without a touch. Each nap lasts 30 minutes unless he's touched. He lies still, and a single "z" floats up.
- Leave the screen on half of flights. Soaring can climb out the top; exploring heads out the left, right, or bottom. He lingers out of view for 1–3 s, then comes back in from any edge. Soaring moves 3.2 px/frame (`kSoarStep`), and trips off and back 4.5 px/frame (`kExploreStep`).
- Nibble by biting leaves, chewing, then looking full
- Roar “ROW, ROW, ROW” only when a kooker appears
- When tapped, come over to the touch (wake first if napping; walk if on the ground, otherwise fly), then show the happy pose and hearts. Taps keep him on screen: 30 s for the first, plus 10 s per extra tap, up to 5 min.
- Show the current temperature and conditions, with no forecast
- Show a ☰ menu button in the lower-right corner. **Display** has checkboxes for Time, Date, Weather, Name, and Description (the activity line), saved in NVS key `show`. Wi-Fi settings: scan and pick a network, type a hidden one, and enter the password on an on-screen keyboard. Credentials are saved in NVS (namespace `himop`) and override `secrets.h`.
- Show labels in the corners on dimmed see-through panels: clock with the date under it at top left, weather at top right, name with the caption under it at bottom left. The middle stays clear for Himop. `description=` from settings.txt is not shown.
- Peek in from an edge while away, hold 2–4 s, and back out
- Change size with depth: 0.5× far to 1.0× near
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

PlatformIO Arduino project. `src/display_rgb.cpp` bit-bangs the ST7701 init over 3-wire SPI, the same sequence LovyanGFX sends, and runs the panel with ESP-IDF's `esp_lcd` RGB driver using two 20-line bounce buffers in internal SRAM. LovyanGFX draws into a 480×480 sprite in PSRAM, which is byte-swapped RGB565. The driver's data lanes are swapped (`i ^ 8`) so that sprite copies straight into the framebuffer. Each frame waits for vsync, then copies only the dirty boxes. GT911 touch uses LovyanGFX's `Touch_GT911` on its own.

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
- [x] Copy `night.bin` back onto the NO NAME card
- [x] Move the card into the TF slot and reboot so the poses and landscapes appear
- [x] Fix washed-out pictures (byte order) and move the labels to the corners
- [x] Stop the screen jumping (redraw and push only what changed)
- [x] Wi-Fi menu button (scan, keyboard, NVS) and correct mid-tone colors (ST7701 back to RGB666)
- [x] Steady panel (esp_lcd bounce buffers, vsync-timed copies, 12 MHz pixel clock)
- [x] Naps only after 5 idle minutes (30-minute naps), off-screen exploring, display checkboxes

## Reflash later

```bash
export PATH="$HOME/Library/Python/3.11/bin:$PATH"
cd /Users/jeaimehp/Documents/esp32-buddy
pio run -t upload --upload-port /dev/tty.usbserial-12430
pio device monitor --port /dev/tty.usbserial-12430 --baud 115200
```

Serial test commands (115200 baud): `tap X Y` simulates a touch. `act NAME` forces the next behavior (`nap soar soarup explore walk kooker graze`). `shot` dumps the current frame. `rec N K` dumps N frames, one every K ticks of a fixed 33 ms clock. `tools/capture.py` drives these and writes PNGs and GIFs. Dumps switch the link to 1 Mbaud and send 1 KB chunks, each with an Adler-32 and an ACK/resend, because the CH340 link drops bytes at high rates (seen at 2 Mbaud).

To test touch without a finger, send `tap X Y` over serial at 115200 and the firmware treats it as a tap. The menu button is at about `tap 450 450`, and "Choose network" is at `tap 240 282`. Opening the port resets the board, so wait about 14 seconds for boot.

Serial is easiest to read with `~/.platformio/penv/bin/python` and pyserial. The system `python3` does not have pyserial installed. Take webcam photos with `imagesnap -d "HD Pro Webcam C920" -w 4 out.jpg`. The 4-second warm-up matters, because a shorter one gives a black frame.

If upload cannot reset the board, hold BOOT, tap RESET, release BOOT, and run the upload again.

## If the picture is wrong

- Blank screen: the backlight boost on GPIO 38 stays off if it is PWM'd around 20 kHz. Firmware now drives that pin high after init. A dark mirror in a photo means the backlight is still off.
- Shifted or torn image: change H/V polarity or the back porches in `src/display_rgb.cpp`.
- Washed out, or dark areas turned bright and speckled: the pixel bytes are reversed. Card `.bin` files are little-endian RGB565, but LovyanGFX 16-bit sprites store each pixel byte-swapped. `src/sd_store.cpp` swaps card pixels once when it loads them, and `blitHimop` swaps the built-in art. Raw pixels copied into `canvas` must be swapped.
- Screen jumps, shifts, or shows doubled text: the panel DMA is underrunning. LovyanGFX's `Bus_RGB` read the framebuffer straight from PSRAM, and its vsync ISR restarted the DMA every frame, so PSRAM traffic from drawing or Wi-Fi (its buffers are in PSRAM) and late interrupts shifted the picture. Now the panel runs on `esp_lcd` with bounce buffers. If shifts come back, raise `kBounceLines` or lower `kPanelPclkHz`. With a landscape loaded, `drawBuddy` restores only last frame's moving area (`dynBox`) plus any label whose text changed, and `loop` pushes only those boxes. Anything new that moves must call `markDirty`, or it leaves trails. Full-screen redraws now happen only when the landscape changes, and on the grid fallback.
- Wrong colors, where pure red, green, and blue look right but mid-tones are off (steel blue shows olive, maroon shows peach): the ST7701 pixel format is wrong. Keep `0x3A` = `0x60` (RGB666, the stock Guition value) in `src/board.hpp`. The board wires its 16 data lines for that mode. An earlier fix switched to `0x50` (RGB565) when the real problem was the byte order, and that scrambled the bits within each channel.
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
- 2026-10-07: Confirmed the board joins Wi-Fi. The first weather read was `0F clear` because the parser hit the units label. Parsing now starts at the `current` object.
- 2026-10-07: Saved four 480×480 landscapes at `/byte/bg/morning.bin`, `afternoon.bin`, `evening.bin`, and `night.bin`. The board copies the one that matches the local hour once the card is in the TF slot.
- 2026-10-07: Removed the description from the screen. Name, clock, date, weather, and caption use a large bold font on dark labels.
- 2026-10-07: Split the labels onto their own rows so the name, clock, date, weather, and caption no longer overlap.
- 2026-10-07: Sprites and landscapes are shown at half the file size. Landscapes are stretched back to the full screen as larger pixels.
- 2026-10-07: The panel bus is 16-bit RGB565, but the controller was left in 18-bit RGB666. Init now selects RGB565, and the pictures are copied one pixel to one pixel again.
- 2026-10-07: Rewrote the card pictures as 16-bit RGB565. Landscapes are exactly 480×480. Sprites are capped so they fit between the labels. Flying cycles six new poses in `/byte/sprites/12.bin` through `17.bin`.
- 2026-10-07: Replaced the four landscapes with the new 16-bit scenes, scaled to 480×480 RGB565 on the card.
- 2026-10-07: Last card check found sprites `0.bin`–`17.bin` plus morning, afternoon, and evening. `night.bin` was not on the card. The card was unplugged before it could be copied. Next step is to mount NO NAME again, add `night.bin`, then move the card to the TF slot.
- 2026-10-07: The card was in the TF slot with `night.bin`. All 18 sprites and four landscapes loaded.
- 2026-10-07: A C920 photo showed the picture washed out, almost white. Cause: card pixels are little-endian, but they were copied raw into the LovyanGFX sprite, which stores pixels byte-swapped. That turned dark blues into bright tan. Card pixels are now swapped at load, and the built-in art is swapped as it is drawn.
- 2026-10-07: New layout. Clock and date are at top left, weather at top right, and name and caption at bottom left, each on a panel that dims the scene to a quarter, with a drop shadow on the text. Himop's flight band is now y 190–280. Flashed, and the photo confirms true color.
- 2026-10-07: The screen was jumping. Cause: every frame copied the full 460 KB landscape and pushed the full 460 KB canvas, about 55 MB/s of PSRAM traffic. That starved the panel DMA, which reads the framebuffer from PSRAM. Now each frame restores and pushes only the box around Himop, his shadow, bubble, hearts, "z", and the kooker, plus labels whose text changed. Drifting sparks are off over landscapes, since they would dirty the whole screen. Himop now draws above the labels. A C920 clip at 6 fps showed no doubled labels or shifts; the only soft frames were camera refocus.
- 2026-10-07: Added `README.md` with screen photos, the 18 sprites, the four landscapes, a flying-cycle GIF, and credit to Silas Rangel, who created Himop for his Creature Adventure Series. The images are in `docs/images/`. The sprite and landscape PNGs are converted from the card `.bin` files.
- 2026-10-07: Added the ☰ Wi-Fi menu in the lower-right corner (`src/wifi_menu.cpp`). It has a main page (network, status, Choose network, Close), an async scan list (strongest first, padlock, signal bars, Back/Rescan/Other/More), and a keyboard (letters, shift, digits, symbols, del, Cancel, Save; "Next" when typing a hidden SSID). Credentials are saved in NVS and override `secrets.h`. Wi-Fi retries pause during a scan. Buttons act on touch-down; holding still pets Himop. Serial `tap X Y` simulates a tap.
- 2026-10-07: A color test (pure primaries next to mid-tones) showed the mid-tones scrambled. Pure R/G/B were right, but (30,90,150) showed olive and (110,40,40) showed peach. Cause: `0x3A` = `0x50`. Back to `0x60` (stock RGB666), and every hue is now correct. The class is renamed `Panel_ST7701_guition_board`.
- 2026-10-07: Fixed a spurious "wifi retry" right after reconnecting. The unsigned `now - gLastWifiTry` wrapped when connectWifi() stamped a later time than the tick's `now`.
- 2026-10-07: Drove the menu end to end over serial taps and the C920. The scan found 2 networks, the keyboard typed, Cancel reconnected, and Close returned to Himop. Nothing was saved, so the board still uses `secrets.h`.
- 2026-10-07: Still jittery. Measured with a script that records 30 fps, keeps Himop flying with serial taps, and counts frames where the static "Himop" label moves. At 14 MHz, 21 of 200 frames were shifted (worst error 47). At 12 MHz, 0 of 200. Kept 12 MHz.
- 2026-10-07: Jitter reported even while napping, so it was not just drawing load. LovyanGFX's `Bus_RGB` restarts the DMA from its vsync ISR every frame and reads PSRAM directly. Wi-Fi buffers also live in PSRAM. Replaced it with `src/display_rgb.cpp`: `esp_lcd` RGB panel, 20-line bounce buffers, vsync semaphore, and copies right after vsync to avoid tearing. `lgfx_board.hpp` became `board.hpp` (pins, timing, init list). 40 s flying test: no shifted frames.
- 2026-10-07: Tried aligning pose silhouettes so the body would hold still across the flying cycle. Whole-mask and body-only matching both looked worse than plain centering, because the art redraws the body in each pose. Removed it.
- 2026-10-07: Naps now happen only after 5 minutes without a touch and last 30 minutes, with no bob and a smoothly rising "z". Otherwise he flies, walks, nibbles, or meets kookers. A third of flights explore off-screen and return (`exploreStage`). Seen on camera: off-screen about 8 s, then back from the right.
- 2026-10-07: Added Display checkboxes (Time, Date, Weather, Name, Description) to the ☰ menu. Hidden labels take no space, so the others close up. Tested over serial taps with the C920: Weather and Description hidden, then restored.
- 2026-10-07: Faster flight. Soaring went from 1.7 to 3.2 px/frame, and off-screen trips go 4.5 px/frame. Half of flights now explore off-screen (was a third), lingering 1–3 s (was 2–6 s).
- 2026-10-07: Off-screen trips use all four edges. Trips: 3/6 normal soaring, 1/6 soaring up out the top, 2/6 exploring out the left, right, or bottom (`offscreenPoint`). He returns from a random edge.
- 2026-10-07: A tap now calls Himop over (`kCome`) instead of making him instantly happy. Napping, he stays curled for 0.9 s ("waking up"). On the ground, he walks ("walking over", 1.8 px/frame). Otherwise he flies ("flying over", 4 px/frame, in from off-screen if needed) to the tap point. On arrival (`startEnjoying`) he shows the happy pose, hearts, and "enjoys that" for `hold_ms`. More touches while he's there extend it, with hearts at most every 250 ms. Checked on camera: called in from off-screen left, flew to the tap, then enjoyed it.
- 2026-10-07: Peeking. While away on an off-screen trip (up to 2 per trip), he eases in from a random edge at 2 px/frame until about a third shows (`kPeekInset`), holds 2–4 s facing in, then backs out. Seen from the right and bottom edges.
- 2026-10-07: Depth. `buddy.z` (0 far, 1 near) eases toward `tz` at 0.006/frame, and he draws at 0.5–1.0× scale (nearest neighbor), shadow too. New flight targets pick a random depth. Ground actions use 0.8, and a tap brings him to 1.0.
- 2026-10-07: Black patch between the wings in flying poses 12 and 13: background enclosed by wing, antenna, and body was never cut out. `clearEnclosedGaps` (src/sd_store.cpp) clears near-black regions of at least 300 px that fill more than 40% of their box, plus two rim passes, when sprites load. Eye outlines are too sparse to match. The same fix was applied to `sdcard/byte/sprites/12.bin` and `13.bin` and the README PNGs/GIF, so the card can be refreshed later, but the firmware fix works with the card as it is.
- 2026-10-07: Pixel-exact screenshots. Added serial `act`, `shot`, and `rec N K`, plus `tools/capture.py`. A pattern test at 2 Mbaud showed the CH340 link dropping bytes mid-stream (first loss at byte 2630), so dumps use 1 Mbaud, 1 KB chunks, Adler-32, and ACK/resend. The board waits 2 s for a reply and the host times out at 0.5 s, so resends never overlap. One 480×480 frame takes about 5 s. `rec` advances a fixed 33 ms clock per tick, so GIFs play at real speed. The "action" log line now includes the caption, and capture's `until TEXT` waits for it.
- 2026-10-07: Bug found by the screenshots: a ground behavior (nap, nibble, walk, kooker) picked while he was off-screen or at an edge happened out of view or half cut off, since a nap doesn't move him. `pickNext` now flies him into the play area (`kMinX..kMaxX`, `kMinY..kMaxY`) first (`kExploreBack`) and keeps the choice in `forcedChoice` until he arrives.
- 2026-10-07: Replaced the README webcam photos with pixel-exact stills (`docs/images/screens/`) and animations (`docs/images/anim/`).
- 2026-10-07: Naps only on screen. If a nap comes due while he's off-screen, he starts at ground level just past a side edge and walks in ("walking back", 1.8 px/frame) to the play area, then naps. If he's on screen but outside the play area, he flies in first.
- 2026-10-07: Petting keeps him on screen. The first tap sets `stayUntil` to 30 s, each further tap (new press) adds 10 s, capped at 5 min. While it runs, `rollChoice` turns off-screen trips (soar-up, explore) into plain soaring. Other behaviors continue.
- 2026-10-08: Added a real webcam photo of the board (`docs/images/photo/on-the-desk.jpg`, from the C920) near the top of the README. Use `imagesnap -d "HD Pro Webcam C920" -w 2 out.jpg` (still capture: sharp and properly exposed). A frame grabbed with ffmpeg avfoundation came out soft and blown out. Crop about (690,0)-(1470,720) of the 1920x1080 shot. Opening the serial port resets the board, so wait about 25 s for Wi-Fi and the clock before tapping. Don't use the npm `uvcc` camera tool: it hangs on macOS and left the C920 stuck for ffmpeg. PWM on the backlight pin (38) doesn't dim the panel; any level below full looks off.
