# OLED Test for Daisy Patch.Init

Simple "Hello World" example using a 64x48 SSD1306 I2C OLED display connected to the expansion header UART pins.

## Hardware Connection

Connect your I2C OLED to the **12-pin expansion header** on the Patch.Init:

| OLED Pin | Expansion Header | Patch SM Pin | GPIO |
|----------|------------------|--------------|------|
| VCC | Pin 1 (3V3) | A10 | - |
| GND | Pin 4 (GND) | A4 | - |
| SDA | Pin 9 (RX) | A2 | PORTA, 1 |
| SCL | Pin 10 (TX) | A3 | PORTA, 0 |

**Note:** This uses software I2C (bit-banging) since the hardware I2C peripheral cannot be remapped to these pins.

## Display Specs

- Resolution: 64x48 pixels
- Controller: SSD1306
- Interface: I2C (address 0x3C)
- Size: 0.66"

## 2x2 Matrix Viability

For a 2x2 grid display:
- Each cell: 32x24 pixels
- With 5x7 font: ~5 characters wide, ~3 lines tall per cell
- **Verdict:** Tight but workable for basic labels/values

## Building

```bash
make clean && make
```

## Flashing

Put Patch.Init in DFU mode (hold BOOT while pressing RESET), then:

```bash
make program-dfu
```

## What It Does

1. Initializes soft I2C on the expansion header pins
2. Initializes the 64x48 SSD1306 OLED
3. Displays "Hello", "World!", "Daisy" on three lines
4. Blinks the onboard LED to show it's running
