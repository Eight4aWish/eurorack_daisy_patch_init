// panel_check — render the OLED panel on a PC and assert nothing falls off it.
//
// The 64x48 screen is small enough that layout bugs are silent rather than
// ugly: a row placed one pixel too low vanishes entirely, and a string one
// character too long used to wrap x past 255 and redraw its own middle over the
// left of the panel. Both shipped once. Neither is visible by reading the code,
// and both cost a bench cycle to find. So this builds the real SSD1306 driver
// and the real legend layout against a stub, renders into the real framebuffer,
// and checks what actually landed.
//
// Two things it can prove that eyes cannot:
//   - DrawString returns the pixels it drew. If that differs from the pixels
//     the string wanted, it clipped -- no counting characters by hand.
//   - A row whose glyphs land outside the framebuffer draws nothing at all,
//     which is exactly what happened to the fifth engine in the boot menu.
//
//   make check
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "multiosc_core/legend_layout.h"
#include "oled_soft_i2c.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace oled;
using multiosc::ComposeLegend;
using multiosc::LegendText;

namespace {

SSD1306 d;
int     failures = 0;

void Fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::printf("  FAIL: ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
    failures++;
}

// True when every glyph of `s` fit: DrawString reports the width it drew, so a
// short return means it hit the right-hand edge and stopped.
bool Fits(uint8_t x, uint8_t y, const char* s)
{
    d.Clear();
    const uint8_t drew = d.DrawString(x, y, s, false);
    return drew == std::strlen(s) * 6;
}

bool FitsCentered(uint8_t y, const char* s)
{
    d.Clear();
    const uint8_t drew = d.DrawStringCentered(y, s, false);
    return drew == std::strlen(s) * 6;
}

// Anything drawn at all in the framebuffer?
bool AnyPixels()
{
    for(size_t i = 0; i < OLED_BUFFER_SIZE; i++)
        if(d.buffer_[i])
            return true;
    return false;
}

//--------------------------------------------------------------------------
// 1. Every engine's title, both pages, every selection.
//--------------------------------------------------------------------------
void CheckTitle(const char* name, const char* sel, const char* esel, bool edit)
{
    const LegendText lt = ComposeLegend(name, sel, esel, edit);
    if(!FitsCentered(0, lt.title))
        Fail("%s %s: title \"%s\" clips", name, edit ? "EDIT" : "PLAY", lt.title);
    // The selection line may truncate by design, but only for a selection that
    // is prose rather than a tag -- flag anything else so a new engine with an
    // over-long tag cannot slip through silently.
    if(lt.has_selection() && !FitsCentered(33, lt.selection)
       && std::strchr(lt.selection, ' ') == nullptr)
        Fail("%s: selection \"%s\" clips and is not prose", name, lt.selection);
}

void CheckAllTitles()
{
    std::printf("Titles: every engine, both pages, every selection\n");
    for(auto s : {"PARA", "SERI", "FDBK"})
        CheckTitle("FM4OP", s, nullptr, false);
    CheckTitle("FM4OP", nullptr, nullptr, true);

    for(auto s : {"Sin", "Tri", "Saw", "Sqr", "SwSi", "SwTr", "SwSq", "SqSi",
                  "SqTr", "SqSw"})
        CheckTitle("INTVL", s, nullptr, false);
    CheckTitle("INTVL", nullptr, nullptr, true);

    for(auto s : {"Pulse", "Bump", "Two", "Noise"})
        CheckTitle("SCAN", s, nullptr, false);
    CheckTitle("SCAN", nullptr, nullptr, true);

    CheckTitle("SINE", "", nullptr, false);
    CheckTitle("SINE", nullptr, nullptr, true);

    // BYTEBEAT: the name alone is eight of the ten columns, so its Edit page
    // puts the family on the selection line. All six must fit there.
    for(auto b : {"TEXT", "NOISE", "PERC", "RHYTM", "MELOD", "REF"})
    {
        CheckTitle("BYTEBEAT", nullptr, b, true);
        const LegendText lt = ComposeLegend("BYTEBEAT", nullptr, b, true);
        if(!lt.has_selection())
            Fail("BYTEBEAT EDIT:%s should sit on the selection line", b);
        else if(!FitsCentered(33, lt.selection))
            Fail("BYTEBEAT selection \"%s\" clips", lt.selection);
    }
    // Play page: short formula names must fit whole; long ones truncate, but
    // the engine name must never be the thing that gets dropped.
    for(auto f : {"ref A440", "Lofi Drone", "Oldskool Tune", "Failing Electro Loop"})
    {
        const LegendText lt = ComposeLegend("BYTEBEAT", f, nullptr, false);
        if(std::strcmp(lt.title, "BYTEBEAT") != 0)
            Fail("BYTEBEAT play title became \"%s\"", lt.title);
        CheckTitle("BYTEBEAT", f, nullptr, false);
    }
}

//--------------------------------------------------------------------------
// 2. The boot menu. Mirrors Host::DrawMenu's geometry; the point is that every
//    entry lands inside the framebuffer for any plausible engine count.
//--------------------------------------------------------------------------
void CheckBootMenu()
{
    std::printf("Boot menu: every entry on screen, 1..10 engines\n");
    using multiosc::kMenuRows;
    using multiosc::MenuRowY;
    using multiosc::MenuWindowStart;

    for(int count = 1; count <= 10; count++)
    {
        for(int sel = 0; sel < count; sel++)
        {
            const int first = MenuWindowStart(sel, count);
            // The highlighted engine must be inside the visible window.
            if(sel < first || sel >= first + kMenuRows)
            {
                Fail("%d engines, sel %d: scrolled out of view", count, sel);
                continue;
            }
            // ...and its row must actually put pixels in the buffer.
            const uint8_t y = (uint8_t)MenuRowY(sel - first);
            d.Clear();
            d.DrawString(2, y, "> BYTEBEAT", false);
            if(!AnyPixels())
                Fail("%d engines, sel %d: row at y=%u drew nothing", count, sel, y);
            if(!Fits(2, y, "> BYTEBEAT"))
                Fail("%d engines, sel %d: \"> BYTEBEAT\" clips at y=%u", count, sel, y);
        }
    }
}

//--------------------------------------------------------------------------
// 3. The driver's own edge cases, which is where both shipped bugs lived.
//--------------------------------------------------------------------------
void CheckDriverEdges()
{
    std::printf("Driver: over-long strings clip rather than wrap\n");
    // 40 characters is far past the 10 the panel holds. The old centring
    // arithmetic underflowed uint8_t and pushed x to ~230, from where it
    // wrapped past 255 and drew the middle of the string on the left.
    const char* huge = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCD";
    d.Clear();
    d.DrawStringCentered(0, huge, false);
    // Whatever it drew must start at the left edge, not somewhere in the middle.
    bool col0 = false;
    for(int y = 0; y < 7; y++)
        if((d.buffer_[0 + (y / 8) * OLED_WIDTH] >> (y % 8)) & 1)
            col0 = true;
    if(!col0)
        Fail("a too-long centred string did not start at the left edge");

    // A string exactly at the limit must fit whole; one past it must not grow.
    if(!FitsCentered(0, "0123456789"))
        Fail("ten characters should fit exactly");
    d.Clear();
    const uint8_t eleven = d.DrawStringCentered(0, "01234567890", false);
    if(eleven > OLED_WIDTH)
        Fail("eleven characters drew %u px into a %u px panel", eleven, OLED_WIDTH);
}

} // namespace

int main()
{
    std::printf("panel_check — real SSD1306 driver, real legend layout\n\n");
    CheckAllTitles();
    CheckBootMenu();
    CheckDriverEdges();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "All panel checks passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
