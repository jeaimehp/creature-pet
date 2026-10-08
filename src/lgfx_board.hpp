#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// The Guition init leaves the ST7701 in RGB666. This bus is 16 wires, RGB565.
class Panel_ST7701_guition_rgb565 : public lgfx::Panel_ST7701_guition_esp32_4848S040 {
 protected:
  const uint8_t *getInitCommands(uint8_t listno) const override {
    static constexpr const uint8_t list0[] = {
        0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x10, 0xC0, 0x02, 0x3B, 0x00, 0xC1,
        0x02, 0x0D, 0x02, 0xC2, 0x02, 0x31, 0x05, 0xCD, 0x01, 0x00, 0xB0, 0x10,
        0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12,
        0x0F, 0xAA, 0x31, 0x18, 0xB1, 0x10, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07,
        0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18, 0xFF, 0x05,
        0x77, 0x01, 0x00, 0x00, 0x11, 0xB0, 0x01, 0x60, 0xB1, 0x01, 0x32, 0xB2,
        0x01, 0x07, 0xB3, 0x01, 0x80, 0xB5, 0x01, 0x49, 0xB7, 0x01, 0x85, 0xB8,
        0x01, 0x21, 0xC1, 0x01, 0x78, 0xC2, 0x01, 0x78, 0xE0, 0x03, 0x00, 0x1B,
        0x02, 0xE1, 0x0B, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00,
        0x44, 0x44, 0xE2, 0x0C, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
        0xEC, 0xA0, 0x00, 0x00, 0xE3, 0x04, 0x00, 0x00, 0x11, 0x11, 0xE4, 0x02,
        0x44, 0x44, 0xE5, 0x10, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
        0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0, 0xE6, 0x04, 0x00, 0x00,
        0x11, 0x11, 0xE7, 0x02, 0x44, 0x44, 0xE8, 0x10, 0x09, 0xE8, 0xD8, 0xA0,
        0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
        0xEB, 0x07, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40, 0xEC, 0x02, 0x3C,
        0x00, 0xED, 0x10, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA, 0xFF, 0x05, 0x77, 0x01, 0x00,
        0x00, 0x13, 0xE5, 0x01, 0xE4, 0xFF, 0x05, 0x77, 0x01, 0x00, 0x00, 0x00,
        0x3A, 0x01, 0x50, 0x11, 0x80, 0x78, 0x29, 0x00, 0xFF, 0xFF,
    };
    switch (listno) {
      case 0:
        return list0;
      default:
        return nullptr;
    }
  }
};

// Guition ESP32-4848S040: ST7701 480x480 RGB panel + GT911 + PWM backlight.
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB _bus_instance;
  Panel_ST7701_guition_rgb565 _panel_instance;
  lgfx::Touch_GT911 _touch_instance;
  lgfx::Light_PWM _light_instance;

  LGFX() {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = 480;
      cfg.memory_height = 480;
      cfg.panel_width = 480;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _panel_instance.config_detail();
      cfg.pin_cs = 39;
      cfg.pin_sclk = 48;
      cfg.pin_mosi = 47;
      cfg.use_psram = 2;
      _panel_instance.config_detail(cfg);
    }

    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0 = GPIO_NUM_4;    // B0
      cfg.pin_d1 = GPIO_NUM_5;    // B1
      cfg.pin_d2 = GPIO_NUM_6;    // B2
      cfg.pin_d3 = GPIO_NUM_7;    // B3
      cfg.pin_d4 = GPIO_NUM_15;   // B4
      cfg.pin_d5 = GPIO_NUM_8;    // G0
      cfg.pin_d6 = GPIO_NUM_20;   // G1
      cfg.pin_d7 = GPIO_NUM_3;    // G2
      cfg.pin_d8 = GPIO_NUM_46;   // G3
      cfg.pin_d9 = GPIO_NUM_9;    // G4
      cfg.pin_d10 = GPIO_NUM_10;  // G5
      cfg.pin_d11 = GPIO_NUM_11;  // R0
      cfg.pin_d12 = GPIO_NUM_12;  // R1
      cfg.pin_d13 = GPIO_NUM_13;  // R2
      cfg.pin_d14 = GPIO_NUM_14;  // R3
      cfg.pin_d15 = GPIO_NUM_0;   // R4

      cfg.pin_henable = GPIO_NUM_18;
      cfg.pin_vsync = GPIO_NUM_17;
      cfg.pin_hsync = GPIO_NUM_16;
      cfg.pin_pclk = GPIO_NUM_21;
      cfg.freq_write = 14000000;

      cfg.hsync_polarity = 1;
      cfg.hsync_front_porch = 10;
      cfg.hsync_pulse_width = 8;
      cfg.hsync_back_porch = 50;
      cfg.vsync_polarity = 1;
      cfg.vsync_front_porch = 10;
      cfg.vsync_pulse_width = 8;
      cfg.vsync_back_porch = 20;
      cfg.pclk_idle_high = 0;
      cfg.de_idle_high = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_38;
      cfg.invert = false;
      // AP3031 enable on this board ignores ~20 kHz PWM and stays dark.
      cfg.freq = 200;
      cfg.pwm_channel = 1;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 479;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x5D;
      cfg.pin_int = GPIO_NUM_NC;
      cfg.pin_sda = GPIO_NUM_19;
      cfg.pin_scl = GPIO_NUM_45;
      cfg.pin_rst = GPIO_NUM_NC;
      cfg.freq = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};
