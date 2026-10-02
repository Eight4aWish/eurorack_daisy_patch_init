#!/usr/bin/env python3
"""
notices.py — the licence notices that go with a released binary.

A release .bin is our code compiled together with other people's, and several of their
licences ask for their notices to travel with a binary, not only with the source: MIT
(Alchemy SDK, libDaisy, DaisySP, Braids, stmlib), BSD-3-Clause (ST's HAL driver),
Apache-2.0 (CMSIS) and, for Sorrow, the GPL itself. This writes <app>/NOTICES.txt for
each released firmware from the licence files and headers in the sources its build uses,
so nothing is retyped. Each release attaches it as <tag>-NOTICES.txt.

What each binary contains was read off its ELF (arm-none-eabi-nm) on 2026-10-02: every
one links ST's HAL, CMSIS and ST's USB device library; Sorrow links DaisySP; Mirth links
FatFs (for its captures); none links CMSIS-DSP. Re-check when a build adds a library.

    python3 common/tools/notices.py             # every released app
    python3 common/tools/notices.py secret      # one
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DAISY = ROOT / "deps" / "daisy"
ALCHEMY = ROOT / "deps" / "alchemy-sdk"
MUTABLE = ROOT / "deps" / "mutable" / "eurorack"


def own_mit():
    """The repo's MIT licence, without its note about third-party code."""
    text = (ROOT / "LICENSE").read_text()
    return text.split("\n---\n")[0].rstrip() + "\n"


def header(path, stop):
    """A source file's leading licence comment, up to the line containing `stop`."""
    out = []
    for line in path.read_text().splitlines():
        if not line.startswith("//"):
            break
        body = line[2:].removeprefix(" ")
        if stop in body:
            out.append(body)
            break
        out.append(body)
    return "\n".join(out).rstrip() + "\n"


def file(path):
    return lambda: path.read_text()


# The libraries every Daisy build carries, from whichever libDaisy the app builds against.
def libdaisy(base):
    return [
        ("libDaisy", "Electrosmith", "MIT", file(base / "LICENSE")),
        ("STM32H7xx HAL driver (in libDaisy)", "STMicroelectronics", "BSD-3-Clause",
         file(base / "Drivers" / "STM32H7xx_HAL_Driver" / "LICENSE.md")),
        ("CMSIS core and STM32H7xx device files (in libDaisy)", "Arm Limited, STMicroelectronics",
         "Apache-2.0", file(base / "Drivers" / "CMSIS_5" / "LICENSE.txt")),
    ]



def fatfs():
    """ChaN's FatFs notice, from the header of ff.c (a '/ '-prefixed comment block)."""
    lines = (DAISY / "libDaisy" / "Middlewares" / "Third_Party" / "FatFs" / "src" / "ff.c").read_text().splitlines()
    start = next(i for i, l in enumerate(lines) if "Copyright (C)" in l and "ChaN" in l)
    out = []
    for l in lines[start:]:
        if l.startswith("/---"):
            break
        out.append(l[2:] if l.startswith("/ ") else l.lstrip("/"))
    return "\n".join(out).rstrip() + "\n"


NADAVB = "https://forum.electro-smith.com/t/nam-a2-on-daisy-seed/9186"


def nam_runtime():
    return ("Based on code nadavb shared on the Daisy forum, " + NADAVB + ", offered there\n"
            "as: \"Free to use. You can just point this forum thread as reference in the\n"
            "header.\" The runtime's header does so.\n\n"
            + (ROOT / "daisy_neural" / "LICENSE-daisyseedprojects.txt").read_text())


def fallback_capture():
    """The credit block generate into daisy_neural/nam/fallback_capture.h, with the licence link."""
    lines = (ROOT / "daisy_neural" / "nam" / "fallback_capture.h").read_text().splitlines()
    credit = [l[2:].strip() for l in lines if l.startswith("//   ")]
    return ("One capture is compiled into the firmware as its fallback:\n\n  "
            + "\n  ".join(credit)
            + "\n\nCC BY 4.0: https://creativecommons.org/licenses/by/4.0/\n"
            "The twelve starter captures are a separate download; each is credited in the\n"
            "STARTER_CAPTURES.md that comes with them.\n")


def mirth_components(title):
    return [
        (title, "David Baghurst", "MIT", own_mit),
        ("NAM A2 runtime", "Keith Shepherd (bkshepherd/DaisySeedProjects), after nadavb", "MIT", nam_runtime),
        ("Built-in capture: Orange TH100", "tupalosa", "CC BY", fallback_capture),
        ("FatFs (in libDaisy)", "ChaN", "FatFs licence (BSD-style)", fatfs),
        *libdaisy(DAISY / "libDaisy"),
    ]

USB_NOTE = ("STM32 USB Device Library (in libDaisy), STMicroelectronics, SLA0044 (Ultimate\n"
            "Liberty). Its clause 2 asks for no notices with a binary that is a software\n"
            "update for an STMicroelectronics device, which this is. Listed for completeness.")

BRAIDS = ("Mutable Instruments Braids and stmlib", "Emilie Gillet", "MIT",
          lambda: header(MUTABLE / "braids" / "macro_oscillator.cc", "THE SOFTWARE."))

APPS = {
    "secret": {
        "dir": "daisy_chaos", "title": "Secret", "bin": "secret.bin",
        "src": "daisy_chaos/, common/chaos_core/ and deps/",
        "components": [
            ("Secret and chaos_core", "David Baghurst", "MIT", own_mit),
            ("Alchemy SDK", "Hermetic Modular LLC", "MIT", file(ALCHEMY / "LICENSE")),
            *libdaisy(ALCHEMY / "vendor" / "libDaisy"),
        ],
        "not_affiliated": "Hermetic Modular, Electrosmith, STMicroelectronics or Arm",
    },
    "joy": {
        "dir": "daisy_braids_oled", "title": "Joy", "bin": "joy.bin",
        "src": "daisy_braids_oled/, common/ and deps/",
        "components": [
            ("Joy", "David Baghurst", "MIT", own_mit),
            BRAIDS,
            *libdaisy(DAISY / "libDaisy"),
        ],
        "not_affiliated": "Mutable Instruments, Electrosmith, STMicroelectronics or Arm",
    },
    "joy_lite": {
        "dir": "daisy_joy_lite", "title": "Joy Lite", "bin": "joy_lite.bin",
        "src": "daisy_joy_lite/, common/ and deps/",
        "components": [
            ("Joy Lite", "David Baghurst", "MIT", own_mit),
            BRAIDS,
            *libdaisy(DAISY / "libDaisy"),
        ],
        "not_affiliated": "Mutable Instruments, Electrosmith, STMicroelectronics or Arm",
    },
    "sorrow": {
        "dir": "daisy_grids", "title": "Sorrow", "bin": "sorrow-vX.Y.Z.bin",
        "src": "daisy_grids/ and deps/",
        "gpl": True,
        "components": [
            ("Sorrow, including its port of Mutable Instruments Grids", "David Baghurst; "
             "Grids by Emilie Gillet", "GPL-3.0-or-later",
             lambda: "Sorrow: Copyright (c) 2026 David Baghurst, GPL-3.0-or-later.\n\n"
             "Its pattern generator is derived from Grids, whose notice is:\n\n"
             + header(MUTABLE / "grids" / "pattern_generator.cc", "along with this program")
             + "\nThe GNU General Public License, version 3, is reproduced in full at the end of\n"
               "this file.\n"),
            ("DaisySP", "Electrosmith; its drum and physical-modelling voices with Emilie Gillet",
             "MIT",
             lambda: "The drum and physical-modelling voices Sorrow uses are ports of Emilie\n"
             "Gillet's Plaits and Rings; their copyright line is \"Copyright (c) 2020\n"
             "Electrosmith, Corp, Emilie Gillet\", under the MIT licence below.\n\n"
             + (DAISY / "DaisySP" / "LICENSE").read_text()),
            *libdaisy(DAISY / "libDaisy"),
        ],
        "not_affiliated": "Mutable Instruments, Electrosmith, STMicroelectronics or Arm",
    },
    "mirth": {
        "dir": "daisy_neural", "title": "Mirth", "bin": "mirth-vX.Y.Z.bin",
        "src": "daisy_neural/, common/ and deps/",
        "components": mirth_components("Mirth"),
        "out": "NOTICES-mirth.txt",
        "not_affiliated": ("Neural Amp Modeler, TONE3000, the makers of the amplifiers and pedals\n"
                           "captured, Electrosmith, STMicroelectronics or Arm"),
    },
    "mirth_lite": {
        "dir": "daisy_neural", "title": "Mirth Lite", "bin": "mirth_lite-vX.Y.Z.bin",
        "src": "daisy_neural/ and deps/",
        "components": mirth_components("Mirth Lite"),
        "out": "NOTICES-mirth_lite.txt",
        "not_affiliated": ("Neural Amp Modeler, TONE3000, the makers of the amplifiers and pedals\n"
                           "captured, Electrosmith, STMicroelectronics or Arm"),
    },
}

RULE = "=" * 80


def notices(key):
    app = APPS[key]
    out = [f"{app['title']} — licence notices for {app['bin']}", ""]
    out += [
        f"{app['bin']} is {app['title']}'s own code compiled together with the libraries",
        "below. Each keeps its own licence, and the notices that licence asks to go with a",
        "binary are reproduced here in full.",
        "",
        "Source: https://github.com/Eight4aWish/eurorack_daisy_patch_init, at the release",
        f"tag, in {app['src']}.",
    ]
    if app.get("gpl"):
        out += [
            "",
            f"{app['title']} is licensed GPL-3.0-or-later as a whole, because its pattern",
            "generator is derived from Mutable Instruments Grids. Its complete corresponding",
            "source is the repository above at the release tag; build it with `make` in",
            f"{app['dir']}/ after `git submodule update --init --recursive`.",
        ]
    out += [""]
    for name, holder, lic, _ in app["components"]:
        out.append(f"  - {name.splitlines()[0].strip()}")
        out.append(f"      {holder} · {lic}")
    out += ["  - " + USB_NOTE.replace("\n", "\n    "), ""]
    out += [f"Not affiliated with, or endorsed by, {app['not_affiliated']}.", ""]
    for name, holder, lic, text in app["components"]:
        out += ["", RULE, f"{name} — {holder} — {lic}", "", text().rstrip(), ""]
    if app.get("gpl"):
        out += ["", RULE, "GNU General Public License, version 3", "",
                (ROOT / app["dir"] / "COPYING").read_text().rstrip(), ""]
    path = ROOT / app["dir"] / app.get("out", "NOTICES.txt")
    path.write_text("\n".join(out))
    print(f"wrote {path.relative_to(ROOT)} ({len(app['components'])} components)")


def main():
    keys = sys.argv[1:] or list(APPS)
    for k in keys:
        if k not in APPS:
            raise SystemExit(f"unknown app {k!r}; one of {', '.join(APPS)}")
        notices(k)


if __name__ == "__main__":
    main()
