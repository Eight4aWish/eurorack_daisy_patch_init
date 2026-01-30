/**
 * OLED Hello World for Daisy Patch.Init
 * 
 * Uses soft I2C on expansion header pins:
 *   - Pin 9 (A2 / PORTA,1) -> SDA
 *   - Pin 10 (A3 / PORTA,0) -> SCL
 * 
 * Target: 64x48 SSD1306 I2C OLED (0.66")
 */

#include "daisy_patch_sm.h"

using namespace daisy;
using namespace patch_sm;

DaisyPatchSM hw;

// Soft I2C pins (accent expansion header)
static GPIO pin_sda;
static GPIO pin_scl;

// I2C address for SSD1306 (typically 0x3C or 0x3D)
constexpr uint8_t OLED_ADDR = 0x3C;

// Display dimensions
constexpr uint8_t OLED_WIDTH = 64;
constexpr uint8_t OLED_HEIGHT = 48;

// Frame buffer (64x48 / 8 = 384 bytes)
static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];

//--------------------------------------------------
// Soft I2C Implementation
//--------------------------------------------------

static inline void I2C_Delay()
{
    // ~5us delay for ~100kHz I2C
    for(volatile int i = 0; i < 50; i++) { __NOP(); }
}

static inline void SDA_High() { pin_sda.Write(true); }
static inline void SDA_Low()  { pin_sda.Write(false); }
static inline void SCL_High() { pin_scl.Write(true); }
static inline void SCL_Low()  { pin_scl.Write(false); }

static void I2C_Start()
{
    SDA_High();
    SCL_High();
    I2C_Delay();
    SDA_Low();
    I2C_Delay();
    SCL_Low();
    I2C_Delay();
}

static void I2C_Stop()
{
    SDA_Low();
    SCL_High();
    I2C_Delay();
    SDA_High();
    I2C_Delay();
}

static bool I2C_WriteByte(uint8_t data)
{
    for(int i = 7; i >= 0; i--)
    {
        if(data & (1 << i))
            SDA_High();
        else
            SDA_Low();
        I2C_Delay();
        SCL_High();
        I2C_Delay();
        SCL_Low();
        I2C_Delay();
    }
    
    // ACK bit (we ignore it for simplicity)
    SDA_High();
    I2C_Delay();
    SCL_High();
    I2C_Delay();
    SCL_Low();
    I2C_Delay();
    
    return true;
}

static void OLED_WriteCommand(uint8_t cmd)
{
    I2C_Start();
    I2C_WriteByte(OLED_ADDR << 1);  // Write address
    I2C_WriteByte(0x00);             // Command mode
    I2C_WriteByte(cmd);
    I2C_Stop();
}

static void OLED_WriteData(uint8_t* data, size_t len)
{
    I2C_Start();
    I2C_WriteByte(OLED_ADDR << 1);
    I2C_WriteByte(0x40);  // Data mode
    for(size_t i = 0; i < len; i++)
    {
        I2C_WriteByte(data[i]);
    }
    I2C_Stop();
}

//--------------------------------------------------
// SSD1306 64x48 Initialization
//--------------------------------------------------

static void OLED_Init()
{
    System::Delay(100);  // Wait for OLED power-up
    
    // Init sequence for 64x48 SSD1306
    OLED_WriteCommand(0xAE);  // Display off
    OLED_WriteCommand(0xD5);  // Set display clock
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);  // Set multiplex ratio
    OLED_WriteCommand(0x2F);  // 48 rows (0x2F = 47)
    OLED_WriteCommand(0xD3);  // Set display offset
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);  // Set start line
    OLED_WriteCommand(0x8D);  // Charge pump
    OLED_WriteCommand(0x14);  // Enable charge pump
    OLED_WriteCommand(0x20);  // Memory mode
    OLED_WriteCommand(0x00);  // Horizontal addressing
    OLED_WriteCommand(0xA1);  // Segment remap
    OLED_WriteCommand(0xC8);  // COM scan direction
    OLED_WriteCommand(0xDA);  // COM pins config
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);  // Contrast
    OLED_WriteCommand(0x8F);
    OLED_WriteCommand(0xD9);  // Pre-charge period
    OLED_WriteCommand(0x25);
    OLED_WriteCommand(0xDB);  // VCOMH deselect
    OLED_WriteCommand(0x34);
    OLED_WriteCommand(0xA4);  // Display from RAM
    OLED_WriteCommand(0xA6);  // Normal display (not inverted)
    OLED_WriteCommand(0xAF);  // Display on
}

static void OLED_Update()
{
    // Set column address (centered for 64px in 128px controller)
    OLED_WriteCommand(0x21);  // Column address
    OLED_WriteCommand(32);    // Start at 32 (centered)
    OLED_WriteCommand(95);    // End at 95
    
    // Set page address
    OLED_WriteCommand(0x22);  // Page address
    OLED_WriteCommand(0);     // Start page
    OLED_WriteCommand(5);     // End page (6 pages for 48 rows)
    
    // Send framebuffer
    OLED_WriteData(framebuffer, sizeof(framebuffer));
}

static void OLED_Clear(bool color = false)
{
    memset(framebuffer, color ? 0xFF : 0x00, sizeof(framebuffer));
}

static void OLED_SetPixel(uint8_t x, uint8_t y, bool on)
{
    if(x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    
    if(on)
        framebuffer[x + (y / 8) * OLED_WIDTH] |= (1 << (y % 8));
    else
        framebuffer[x + (y / 8) * OLED_WIDTH] &= ~(1 << (y % 8));
}

//--------------------------------------------------
// Simple 5x7 Font (just enough for "Hello World")
//--------------------------------------------------

static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // A (1)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E (5)
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H (8)
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L (12)
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O (15)
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R (18)
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T (20)
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W (23)
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x00, 0x00, 0x00}, // [ placeholder
    {0x00, 0x00, 0x00, 0x00, 0x00}, // \ placeholder  
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ] placeholder
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ^ placeholder
    {0x00, 0x00, 0x00, 0x00, 0x00}, // _ placeholder
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ` placeholder (32)
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a (33)
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d (36)
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e (37)
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h (40)
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i (41)
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l (44)
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o (47)
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r (50)
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t (52)
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w (55)
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // ! (59)
};

static int CharToIndex(char c)
{
    if(c == ' ') return 0;
    if(c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if(c >= 'a' && c <= 'z') return c - 'a' + 33;
    if(c == '!') return 59;
    return 0;
}

static void OLED_DrawChar(uint8_t x, uint8_t y, char c)
{
    int idx = CharToIndex(c);
    for(int col = 0; col < 5; col++)
    {
        uint8_t line = font5x7[idx][col];
        for(int row = 0; row < 7; row++)
        {
            if(line & (1 << row))
                OLED_SetPixel(x + col, y + row, true);
        }
    }
}

static void OLED_DrawString(uint8_t x, uint8_t y, const char* str)
{
    while(*str)
    {
        OLED_DrawChar(x, y, *str);
        x += 6;  // 5 pixels + 1 space
        str++;
    }
}

//--------------------------------------------------
// Main
//--------------------------------------------------

int main(void)
{
    hw.Init();
    
    // Initialize soft I2C pins
    // A2 (PORTA, 1) = SDA, A3 (PORTA, 0) = SCL
    pin_sda.Init(hw.A2, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL, GPIO::Speed::HIGH);
    pin_scl.Init(hw.A3, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL, GPIO::Speed::HIGH);
    
    // Both start high (idle)
    SDA_High();
    SCL_High();
    
    System::Delay(100);
    
    // Initialize OLED
    OLED_Init();
    
    // Clear and draw
    OLED_Clear();
    OLED_DrawString(4, 10, "Hello");
    OLED_DrawString(4, 22, "World!");
    OLED_DrawString(4, 36, "Daisy");
    OLED_Update();
    
    // Blink LED to show we're running
    while(1)
    {
        hw.SetLed(true);
        System::Delay(500);
        hw.SetLed(false);
        System::Delay(500);
    }
}
