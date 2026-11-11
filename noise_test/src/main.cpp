// Noise test firmware, temporary: for the noise comparison in
// eurorack_electronics/docs/noise_test.md. Not a module; reflash the real firmware after.
//
// One schedule (noise_schedule.h) on every board: output silence, an output tone and
// input-to-output pass-through, with the screen off, still and redrawing, three times over.
// Only audio and the screen run: no ADC or CV scanning, no LED, so nothing else on the board
// draws current on a schedule of its own.
//
// Builds (see the Makefile):
//   BOARD=patchsm          the stock patch.init, no screen
//   BOARD=patchsm OLED=1   the patch.init with the 64x48 SSD1306 on soft I2C (A2 SDA, A3 SCL),
//                          driven by the same driver its apps use (common/oled_soft_i2c.h)
//   BOARD=seed             the hand-built Daisy Seed module, 128x64 SSD1306 on hardware I2C1
//                          (D11 SCL, D12 SDA) at 1 MHz, through libDaisy's driver as in
//                          daisy_multifx_seed
//
// Screen states: off = never started, or display off and charge pump off (0xAE, 0x8D 0x10);
// still = a 50% checkerboard drawn once; redrawing = the checkerboard and its inverse
// alternately, a whole frame every 33 ms (the ~30 fps cap daisy_multifx_seed uses).

#include "daisy.h"
#include "noise_schedule.h"

#if defined(BOARD_PATCHSM)
#include "daisy_patch_sm.h"
#if OLED
#include "oled_soft_i2c.h"
#endif
#elif defined(BOARD_SEED)
#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#else
#error "BOARD_PATCHSM or BOARD_SEED"
#endif

using namespace daisy;

#if defined(BOARD_PATCHSM)
using namespace patch_sm;
static DaisyPatchSM hw;
#else
static DaisySeed hw;
#endif

static noise::Player player;

#if defined(BOARD_PATCHSM) && OLED
#define HAS_SCREEN 1
static oled::SSD1306 panel;
constexpr int W = oled::OLED_WIDTH, H = oled::OLED_HEIGHT;
static void PanelStart() { panel.Init(hw.A2, hw.A3); }  // its init ends display-on
static void PanelCommand(uint8_t c) { panel.SendCommand(c); }
static void PanelFrame(bool phase) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) panel.SetPixel(x, y, ((x + y + phase) & 1) != 0);
  panel.Update();
}
#elif defined(BOARD_SEED)
#define HAS_SCREEN 1
using Panel = OledDisplay<SSD130xI2c128x64Driver>;
static Panel panel;
static I2CHandle cmd_i2c;  // the same bus, for the off/on commands libDaisy's driver doesn't expose
constexpr int W = 128, H = 64;
static void PanelStart() {
  Panel::Config cfg;  // default: I2C1, PB8/PB9, 1 MHz, 0x3C, as daisy_multifx_seed
  panel.Init(cfg);
  I2CHandle::Config c;
  c.periph = I2CHandle::Config::Peripheral::I2C_1;
  c.speed = I2CHandle::Config::Speed::I2C_1MHZ;
  c.mode = I2CHandle::Config::Mode::I2C_MASTER;
  c.pin_config.scl = Pin(PORTB, 8);
  c.pin_config.sda = Pin(PORTB, 9);
  cmd_i2c.Init(c);
}
static void PanelCommand(uint8_t cmd) {
  uint8_t buf[2] = {0x00, cmd};
  cmd_i2c.TransmitBlocking(0x3C, buf, 2, 1000);
}
static void PanelFrame(bool phase) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) panel.DrawPixel(x, y, ((x + y + phase) & 1) != 0);
  panel.Update();
}
#else
#define HAS_SCREEN 0
#endif

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
  player.Process(in[0], in[1], out[0], out[1], size);
}

int main(void) {
  hw.Init();
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  hw.SetAudioBlockSize(48);
  player.Begin(HAS_SCREEN);
  hw.StartAudio(AudioCallback);

#if HAS_SCREEN
  bool started = false, phase = false;
  noise::Screen shown = noise::OFF;  // the panel powers up off
  uint32_t last_frame = 0;
  while (1) {
    noise::Screen want = player.screen;
    if (want != shown) {
      if (want == noise::OFF) {
        if (started) {
          PanelCommand(0xAE);  // display off
          PanelCommand(0x8D);  // charge pump ...
          PanelCommand(0x10);  // ... off
        }
      } else {
        if (!started) {
          PanelStart();
          started = true;
        } else {
          PanelCommand(0x8D);
          PanelCommand(0x14);  // charge pump on
          PanelCommand(0xAF);  // display on
        }
        phase = false;
        PanelFrame(phase);
        last_frame = System::GetNow();
      }
      shown = want;
    }
    if (shown == noise::REDRAW && System::GetNow() - last_frame >= 33) {
      last_frame = System::GetNow();
      phase = !phase;
      PanelFrame(phase);
    }
  }
#else
  while (1) System::Delay(100);
#endif
}
