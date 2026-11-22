// legend_layout.h — how the legend's lines are composed.
//
// Pulled out of Host::DrawLegend so that it is reachable without a display or a
// DaisyPatchSM behind it. tools/panel_check builds this same header and asserts
// against it, which only means anything because it is the code the firmware
// runs rather than a second copy of the rules that drifts from it.
//
// The rule it encodes: a 64 px line at 6 px per character is TEN characters.
// "NAME:SEL" is the house style and fits for engines with short selections
// (INTVL:SwTr), but a selection that is words rather than an abbreviation --
// the BYTEBEAT engine reported its formula, "Oldskool Tune" -- blows past it.
// Rather than clip the engine name away, the selection drops onto its own line
// under the knob grid, where there is room.
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

// name: engine name.  sel: the tap's selection, or null for an engine whose tap
// does nothing.  esel: a selection of the hold layer's own, or null.  edit: the
// hold layer is showing, so the title says EDIT.
inline LegendText
ComposeLegend(const char* name, const char* sel, const char* esel, bool edit,
              size_t cols = kLegendCols)
{
    LegendText out;
    out.title[0] = out.selection[0] = '\0';

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

// One row of the knob grid, laid out as the pots sit: pot 1 and 2 on the top
// row, 3 and 4 below. The left cell is six columns (five characters and a
// space), the right four, so a row is the panel's ten.
constexpr int kGridLeftCols  = 6;
constexpr int kGridRightCols = 4;
constexpr int kGridRow1Y     = 13;
constexpr int kGridRow2Y     = 23;
constexpr int kSelectionY    = 33;

inline void ComposeGridRow(char* out, size_t n, const char* left, const char* right)
{
    std::snprintf(out, n, "%-*.*s%.*s", kGridLeftCols, kGridLeftCols - 1, left, kGridRightCols, right);
}

} // namespace multiosc
