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
//     which is exactly what happened to the fifth engine in the old boot menu.
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
    std::printf("Titles: every engine, main page and hold layer, every tap selection\n");
    // Only INTVL's tap selects something; the others' titles are the name alone.
    for(auto n : {"FM4OP", "SCAN", "WAVES"})
    {
        CheckTitle(n, nullptr, nullptr, false);
        CheckTitle(n, nullptr, nullptr, true);
    }
    for(auto s : {"Sin", "Tri", "Saw", "Sqr", "SwSi", "SwTr", "SwSq", "SqSi",
                  "SqTr", "SqSw"})
    {
        CheckTitle("INTVL", s, nullptr, false);
        const LegendText lt = ComposeLegend("INTVL", s, nullptr, false);
        if(lt.has_selection())
            Fail("INTVL:%s dropped to the selection line", s);
    }
    CheckTitle("INTVL", "SwTr", nullptr, true);
}

//--------------------------------------------------------------------------
// 2. The knob grid: every cell each engine can show, in both rows, main page
//    and hold layer. ComposeGridRow truncates a cell that is too long, so the
//    cells' own lengths are checked too: a truncated label fits and is wrong.
//--------------------------------------------------------------------------
void CheckCell(const char* engine, const char* s, size_t max)
{
    if(std::strlen(s) > max)
        Fail("%s: \"%s\" is longer than its cell (%zu)", engine, s, max);
}

void CheckRow(const char* engine, const char* left, const char* right, int y)
{
    CheckCell(engine, left, multiosc::kGridLeftCols - 1);
    CheckCell(engine, right, multiosc::kGridRightCols);
    char row[24];
    multiosc::ComposeGridRow(row, sizeof(row), left, right);
    if(!Fits(0, (uint8_t)y, row))
        Fail("%s: row \"%s\" clips at y=%d", engine, row, y);
    d.Clear();
    d.DrawString(0, (uint8_t)y, row, false);
    if(!AnyPixels())
        Fail("%s: row at y=%d drew nothing", engine, y);
}

void CheckGrid()
{
    std::printf("Grid: every engine's cells, main page and hold layer\n");
    using multiosc::kGridRow1Y;
    using multiosc::kGridRow2Y;
    // pot 1's choices against pot 2's labels; pots 3 and 4 below. These copy the engines' labels, which
    // need DaisySP to build: keep them in step with Choice() and PotLabel().
    for(auto c : {"Alg1", "Alg8"})
        CheckRow("FM4OP", c, "Rat", kGridRow1Y);
    CheckRow("FM4OP", "Idx", "Fbk", kGridRow2Y);
    CheckRow("INTVL", "Intv", "Intv", kGridRow1Y);
    CheckRow("INTVL", "Ratl", "Rat", kGridRow1Y);
    CheckRow("INTVL", "Det", "PW", kGridRow2Y);
    for(auto c : {"Pulse", "Bump", "Two", "Noise"})
        CheckRow("SCAN", c, "Tens", kGridRow1Y);
    CheckRow("SCAN", "Hamr", "Cntr", kGridRow2Y);
    for(auto c : {"Sin", "Tri", "Saw", "Sqr"})
        CheckRow("WAVES", c, "PW", kGridRow1Y);
    CheckRow("WAVES", "-", "-", kGridRow2Y);
    // the hold layer
    CheckRow("hold", "Vol", "-", kGridRow1Y);
    CheckRow("hold", "Atk", "Dec", kGridRow2Y);
    CheckRow("hold", "-", "Damp", kGridRow2Y);
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
    CheckGrid();
    CheckDriverEdges();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "All panel checks passed",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
