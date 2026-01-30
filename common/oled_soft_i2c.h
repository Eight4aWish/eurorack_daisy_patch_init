/**
 * Soft I2C OLED Driver for Daisy Patch.Init
 * 
 * Uses expansion header pins A2/A3 (UART TX/RX) for I2C:
 *   - A2 (PORTA,1) -> SDA
 *   - A3 (PORTA,0) -> SCL
 * 
 * Supports SSD1306 64x48 and 128x64 displays
 */

#pragma once

#include "daisy_patch_sm.h"
#include <cstring>

namespace oled {

// Display dimensions (configure for your display)
constexpr uint8_t OLED_WIDTH = 64;
constexpr uint8_t OLED_HEIGHT = 48;
constexpr size_t  OLED_BUFFER_SIZE = OLED_WIDTH * OLED_HEIGHT / 8;

// I2C address (typically 0x3C or 0x3D)
constexpr uint8_t OLED_ADDR = 0x3C;

class SoftI2C {
public:
    void Init(daisy::Pin sda_pin, daisy::Pin scl_pin) {
        sda_.Init(sda_pin, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL, daisy::GPIO::Speed::HIGH);
        scl_.Init(scl_pin, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL, daisy::GPIO::Speed::HIGH);
        sda_.Write(true);
        scl_.Write(true);
    }

    void Start() {
        sda_.Write(true);
        scl_.Write(true);
        Delay();
        sda_.Write(false);
        Delay();
        scl_.Write(false);
        Delay();
    }

    void Stop() {
        sda_.Write(false);
        scl_.Write(true);
        Delay();
        sda_.Write(true);
        Delay();
    }

    bool WriteByte(uint8_t data) {
        for (int i = 7; i >= 0; i--) {
            sda_.Write((data >> i) & 1);
            Delay();
            scl_.Write(true);
            Delay();
            scl_.Write(false);
            Delay();
        }
        // ACK (ignored)
        sda_.Write(true);
        Delay();
        scl_.Write(true);
        Delay();
        scl_.Write(false);
        Delay();
        return true;
    }

private:
    daisy::GPIO sda_, scl_;
    
    inline void Delay() {
        for (volatile int i = 0; i < 50; i++) { __NOP(); }
    }
};

class SSD1306 {
public:
    void Init(SoftI2C* i2c) {
        i2c_ = i2c;
        daisy::System::Delay(100);
        InitSequence();
    }

    void Init(daisy::Pin sda_pin, daisy::Pin scl_pin) {
        i2c_internal_.Init(sda_pin, scl_pin);
        i2c_ = &i2c_internal_;
        daisy::System::Delay(100);
        InitSequence();
    }

    void Clear(bool color = false) {
        memset(buffer_, color ? 0xFF : 0x00, OLED_BUFFER_SIZE);
    }

    void SetPixel(uint8_t x, uint8_t y, bool on) {
        if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
        if (on)
            buffer_[x + (y / 8) * OLED_WIDTH] |= (1 << (y % 8));
        else
            buffer_[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
    }

    void Update() {
        // Set column address (centered for 64px in 128px controller)
        SendCommand(0x21);
        SendCommand(32);  // Start
        SendCommand(95);  // End
        
        // Set page address
        SendCommand(0x22);
        SendCommand(0);
        SendCommand((OLED_HEIGHT / 8) - 1);
        
        // Send buffer
        i2c_->Start();
        i2c_->WriteByte(OLED_ADDR << 1);
        i2c_->WriteByte(0x40);  // Data mode
        for (size_t i = 0; i < OLED_BUFFER_SIZE; i++) {
            i2c_->WriteByte(buffer_[i]);
        }
        i2c_->Stop();
    }

    // Drawing primitives
    void DrawHLine(uint8_t x, uint8_t y, uint8_t w, bool on = true) {
        for (uint8_t i = 0; i < w; i++) SetPixel(x + i, y, on);
    }

    void DrawVLine(uint8_t x, uint8_t y, uint8_t h, bool on = true) {
        for (uint8_t i = 0; i < h; i++) SetPixel(x, y + i, on);
    }

    void DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on = true) {
        DrawHLine(x, y, w, on);
        DrawHLine(x, y + h - 1, w, on);
        DrawVLine(x, y, h, on);
        DrawVLine(x + w - 1, y, h, on);
    }

    void FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on = true) {
        for (uint8_t j = 0; j < h; j++) {
            DrawHLine(x, y + j, w, on);
        }
    }

    // Text rendering (5x7 font, returns width drawn)
    uint8_t DrawChar(uint8_t x, uint8_t y, char c, bool invert = false);
    uint8_t DrawString(uint8_t x, uint8_t y, const char* str, bool invert = false);
    uint8_t DrawStringCentered(uint8_t y, const char* str, bool invert = false);

    // Large text (scaled 2x)
    uint8_t DrawCharLarge(uint8_t x, uint8_t y, char c, bool invert = false);
    uint8_t DrawStringLarge(uint8_t x, uint8_t y, const char* str, bool invert = false);
    uint8_t DrawStringLargeCentered(uint8_t y, const char* str, bool invert = false);

public:
    SoftI2C* i2c_;
    SoftI2C  i2c_internal_;
    uint8_t buffer_[OLED_BUFFER_SIZE];

    void SendCommand(uint8_t cmd) {
        i2c_->Start();
        i2c_->WriteByte(OLED_ADDR << 1);
        i2c_->WriteByte(0x00);  // Command mode
        i2c_->WriteByte(cmd);
        i2c_->Stop();
    }

private:
    void InitSequence() {
        // Init sequence for 64x48
        SendCommand(0xAE);  // Display off
        SendCommand(0xD5);  // Clock divide
        SendCommand(0x80);
        SendCommand(0xA8);  // Multiplex ratio
        SendCommand(OLED_HEIGHT - 1);
        SendCommand(0xD3);  // Display offset
        SendCommand(0x00);
        SendCommand(0x40);  // Start line
        SendCommand(0x8D);  // Charge pump
        SendCommand(0x14);
        SendCommand(0x20);  // Memory mode
        SendCommand(0x00);  // Horizontal
        SendCommand(0xA1);  // Segment remap
        SendCommand(0xC8);  // COM scan direction
        SendCommand(0xDA);  // COM pins
        SendCommand(0x12);
        SendCommand(0x81);  // Contrast
        SendCommand(0x8F);
        SendCommand(0xD9);  // Pre-charge
        SendCommand(0x25);
        SendCommand(0xDB);  // VCOMH
        SendCommand(0x34);
        SendCommand(0xA4);  // Display from RAM
        SendCommand(0xA6);  // Normal (not inverted)
        SendCommand(0xAF);  // Display on
        
        Clear();
        Update();
    }
};

// 5x7 Font data (ASCII 32-127)
extern const uint8_t font5x7[][5];

} // namespace oled
