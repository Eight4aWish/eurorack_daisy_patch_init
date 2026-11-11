# noise_test: temporary test firmware, not a module

For the noise comparison in [`eurorack_electronics/docs/noise_test.md`](https://github.com/Eight4aWish/eurorack_electronics/blob/main/docs/noise_test.md),
which has the method, what to plug and the procedure. One schedule (`src/noise_schedule.h`, kept
identical to the copy in `eurorack_modules/src/noise_test/`) on every board, 48 kHz with
48-sample blocks: output silence, an output tone and input-to-output pass-through, with the screen
off, still and redrawing, three times over, starting 30 s after power-up.

```sh
./build_all.sh      # three images, each in its own folder under build/
```

| Image | Board |
|---|---|
| `build/patchsm/noise_test_patchsm.bin` | the stock patch.init (no screen) |
| `build/patchsm_oled/noise_test_patchsm_oled.bin` | the patch.init with the 64x48 OLED on soft I2C (A2 SDA, A3 SCL) |
| `build/seed/noise_test_seed.bin` | the hand-built Daisy Seed module, 128x64 OLED on I2C1 |

Flash with DFU (`make BOARD=... BUILD_DIR=build/... program-dfu`, or the Daisy Web Programmer), and
reflash the board's own firmware afterwards. Only audio and the screen run: no ADC or CV scanning.
