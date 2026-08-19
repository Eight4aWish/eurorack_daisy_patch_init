# Host-side panel checks

```sh
make check
```

Builds the **real** `oled_soft_i2c` driver and the **real** legend/menu layout
from `multiosc_core/legend_layout.h` with a PC compiler, renders into the actual
framebuffer, and asserts on what landed. `stub/` stands in for the slice of
libDaisy the driver touches; it is never compiled into firmware.

The 64x48 panel fails *silently* rather than visibly, which is why this exists:

- **A row one pixel too low draws nothing at all.** Adding a fifth engine put
  the last boot-menu entry at y=48 on a 48 px screen, so `SetPixel` discarded
  every one of its pixels and SINE simply was not there.
- **A string one character too long used to wrap.** `DrawStringCentered`
  computed `(64 - width) / 2`, which goes negative past ten characters and lands
  in a `uint8_t` as ~230; `x` then wrapped past 255 and redrew the middle of the
  string over the left of the panel.

Neither is visible by reading the code and both cost a bench cycle to find.

What it checks:

- **Titles** — every engine, both pages, every selection. `DrawString` returns
  the width it drew, so comparing that against the width the string wanted
  detects clipping without counting characters by hand.
- **Boot menu** — for 1..10 engines and every selection, the highlighted entry
  is inside the scroll window *and* its row actually puts pixels in the buffer.
- **Driver edges** — an over-long centred string starts at the left edge and
  clips, rather than wrapping; ten characters fit exactly; eleven do not
  overflow the panel.

The layout constants live in `legend_layout.h` and are shared with `Host`, not
copied here — a check reasoning about its own copy of the geometry would pass
happily while the firmware drew something else.

Both historical bugs were re-introduced deliberately to confirm this fails on
them; it does.
