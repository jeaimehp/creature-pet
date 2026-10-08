#include "display_rgb.h"

#include "board.hpp"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/semphr.h>
#include <string.h>

static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t *frame = nullptr;
static SemaphoreHandle_t vsyncSem = nullptr;

// 20 lines per bounce buffer: about 0.9 ms of slack before the DMA runs dry.
static constexpr int kBounceLines = 20;

static void spiBits(uint32_t data, int bits) {
  for (uint32_t mask = 1u << (bits - 1); mask; mask >>= 1) {
    digitalWrite(kPanelSck, LOW);
    digitalWrite(kPanelSda, (data & mask) ? HIGH : LOW);
    digitalWrite(kPanelSck, HIGH);
  }
}

// 9-bit 3-wire SPI: a leading 0 marks a command byte, 1 a data byte.
static void spiCommand(uint8_t c) { spiBits(c, 9); }
static void spiData(uint8_t d) { spiBits(0x100 | d, 9); }

static void st7701Init() {
  pinMode(kPanelCs, OUTPUT);
  pinMode(kPanelSck, OUTPUT);
  pinMode(kPanelSda, OUTPUT);
  digitalWrite(kPanelSck, LOW);
  digitalWrite(kPanelCs, LOW);

  // Same preamble LovyanGFX sends before the board list: command page 0x10,
  // line count for 480 lines, and RGBCTRL for these sync polarities.
  spiCommand(0xFF);
  for (uint8_t b : {0x77, 0x01, 0x00, 0x00, 0x10}) spiData(b);
  spiCommand(0xC0);
  spiData((kPanelH >> 3) + 1);
  spiData(0x00);
  spiCommand(0xC3);
  for (uint8_t b : {0x01, 0x10, 0x08}) spiData(b);

  const uint8_t *p = kSt7701Init;
  for (;;) {
    const uint8_t cmd = *p++;
    uint8_t argc = *p++;
    if (cmd == 0xFF && argc == 0xFF) break;
    spiCommand(cmd);
    const bool wait = argc & 0x80;
    argc &= 0x7F;
    while (argc--) spiData(*p++);
    if (wait) {
      const uint8_t ms = *p++;
      delay(ms == 255 ? 500 : ms);
    }
  }
  digitalWrite(kPanelCs, HIGH);
}

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSem, &woken);
  return woken == pdTRUE;
}

bool panelBegin() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = kPanelPclkHz;
  cfg.timings.h_res = kPanelW;
  cfg.timings.v_res = kPanelH;
  cfg.timings.hsync_pulse_width = 8;
  cfg.timings.hsync_back_porch = 50;
  cfg.timings.hsync_front_porch = 10;
  cfg.timings.vsync_pulse_width = 8;
  cfg.timings.vsync_back_porch = 20;
  cfg.timings.vsync_front_porch = 10;
  cfg.timings.flags.de_idle_high = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = kPanelW * kBounceLines;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = kPanelHsync;
  cfg.vsync_gpio_num = kPanelVsync;
  cfg.de_gpio_num = kPanelDe;
  cfg.pclk_gpio_num = kPanelPclk;
  cfg.disp_gpio_num = -1;
  // The framebuffer holds byte-swapped pixels (LovyanGFX sprite order), so the
  // two byte lanes trade places: data line i carries pin i ^ 8.
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = kPanelData[i ^ 8];
  cfg.flags.fb_in_psram = 1;

  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;
  vsyncSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  void *fb = nullptr;
  esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb);
  frame = static_cast<uint16_t *>(fb);
  if (frame) memset(frame, 0, (size_t)kPanelW * kPanelH * 2);

  st7701Init();
  pinMode(kBacklight, OUTPUT);
  digitalWrite(kBacklight, HIGH);
  return frame != nullptr;
}

void panelCopy(const uint16_t *canvas, int x0, int y0, int x1, int y1) {
  if (!frame || x0 >= x1 || y0 >= y1) return;
  const size_t rowBytes = (size_t)(x1 - x0) * 2;
  for (int y = y0; y < y1; ++y) {
    memcpy(frame + y * kPanelW + x0, canvas + y * kPanelW + x0, rowBytes);
  }
}

void panelWaitVsync(uint32_t timeoutMs) {
  if (!vsyncSem) return;
  xSemaphoreTake(vsyncSem, 0);  // drop a stale one so we start at a fresh frame
  xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(timeoutMs));
}

static lgfx::Touch_GT911 touch;

bool touchBegin() {
  auto cfg = touch.config();
  cfg.x_min = 0;
  cfg.x_max = kPanelW - 1;
  cfg.y_min = 0;
  cfg.y_max = kPanelH - 1;
  cfg.bus_shared = false;
  cfg.offset_rotation = 0;
  cfg.i2c_port = 1;
  cfg.i2c_addr = kTouchAddr;
  cfg.pin_int = -1;
  cfg.pin_sda = kTouchSda;
  cfg.pin_scl = kTouchScl;
  cfg.pin_rst = -1;
  cfg.freq = 400000;
  touch.config(cfg);
  return touch.init();
}

bool touchRead(int *x, int *y) {
  lgfx::touch_point_t tp;
  if (touch.getTouchRaw(&tp, 1) == 0) return false;
  *x = tp.x;
  *y = tp.y;
  return true;
}
