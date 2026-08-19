// legend_layout.h — how the legend's title and selection lines are composed.
//
// Pulled out of Host::DrawLegend so that it is reachable without a display or a
// DaisyPatchSM behind it. tools/panel_check builds this same header and asserts
// against it, which only means anything because it is the code the firmware
// runs rather than a second copy of the rules that drifts from it.
//
// The rule it encodes: a 64 px line at 6 px per character is TEN characters.
// "NAME:SEL" is the house style and fits for engines with short selections
// (FM4OP:SwTr), but an engine whose selection is words rather than an
// abbreviation -- bytebeat reports the formula, "Oldskool Tune" -- blows past
// it. Rather than clip the engine name away, the selection drops onto its own
// line under the knob grid, where there is room.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include <cstdio>
#include <cstring>

namespace multiosc {

// A 64 px panel at 6 px per glyph. Anything wider than this clips.
constexpr size_t kLegendCols = 10;

struct LegendText {
    char title[24];     // the centred line at the top
    char selection[24]; // drawn under the knob grid; empty when it fits above

    bool has_selection() const { return selection[0] != '\0'; }
};

// name: engine name.  sel: Play-page selection.  esel: Edit-page selection, or
// null for engines whose Edit page has nothing of its own to name.
inline LegendText
ComposeLegend(const char* name, const char* sel, const char* esel, bool edit,
              size_t cols = kLegendCols)
{
    LegendText out;
    out.title[0] = out.selection[0] = '\0';

    // On the Edit page, name the page's own selection alongside it where the
    // engine offers one: "EDIT:NOISE" is ten characters, exactly the line, and
    // tells you which family the short press is about to walk.
    char        tagbuf[24];
    const char* tag = sel;
    if(edit)
    {
        if(esel && esel[0])
        {
            std::snprintf(tagbuf, sizeof(tagbuf), "EDIT:%s", esel);
            tag = tagbuf;
        }
        else
            tag = "EDIT";
    }

    if(tag && tag[0])
    {
        std::snprintf(out.title, sizeof(out.title), "%s:%s", name, tag);
        if(std::strlen(out.title) > cols)
        {
            std::snprintf(out.title, sizeof(out.title), "%s", name);
            std::snprintf(out.selection, sizeof(out.selection), "%s", tag);
        }
    }
    else
        std::snprintf(out.title, sizeof(out.title), "%s", name);

    return out;
}

//----------------------------------------------------------------------------
// Boot-menu geometry. Here for the same reason as the legend: the check in
// tools/panel_check has to reason about the same numbers the firmware draws
// with, and a second copy of them is a second thing to forget to update. The
// fifth engine once landed at y=48 on a 48 px panel and drew nothing at all --
// not clipped, not ugly, simply absent -- which is the failure this guards.
//----------------------------------------------------------------------------

constexpr int kMenuTop    = 9; // below the "SELECT" header
constexpr int kMenuStep   = 8; // 7 px glyph + 1
constexpr int kMenuHeight = 48;
constexpr int kGlyphRows  = 7;

// How many entries fit between the header and the bottom edge.
constexpr int kMenuRows = (kMenuHeight - kMenuTop - kGlyphRows) / kMenuStep + 1;

// Index of the first entry to draw, so that `sel` is always on screen. Returns
// 0 until the lineup outgrows the panel, then scrolls to keep the highlight
// roughly centred.
inline int MenuWindowStart(int sel, int count)
{
    if(count <= kMenuRows)
        return 0;
    int first = sel - kMenuRows / 2;
    if(first < 0)
        first = 0;
    if(first > count - kMenuRows)
        first = count - kMenuRows;
    return first;
}

// Top pixel row for the i'th visible entry.
inline int MenuRowY(int i_visible)
{
    return kMenuTop + i_visible * kMenuStep;
}

} // namespace multiosc
