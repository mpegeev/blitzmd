// Typesetting of TeX math (texmath.h) with an OpenType MATH font (Cambria
// Math): TeX's rules for fractions, radicals, scripts and limits (TeXbook
// appendix G) driven by the font's MATH constants; delimiters, radicals,
// big operators, wide accents and braces stretched with its glyph variants
// and assemblies. The result is a list of positioned glyphs and rules.
#pragma once

#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/util.h"

namespace mdv {

// A laid-out formula, around an origin at the left end of its baseline
// (DIPs, y down).
struct MathBox {
    struct Glyph {
        float x = 0, y = 0;   // pen position on the baseline
        float size = 0;       // em size
        uint16_t index = 0;
        uint16_t group = 0;   // nonzero: parts of one stretched glyph, filled as one shape
        uint8_t face = 0;
        uint32_t color = 0;   // 0 = the text color, else 0xAARRGGBB
    };
    struct Rule {
        float x = 0, y = 0, w = 0, h = 0;  // y: top edge
        uint32_t color = 0;
    };
    // Text that needs shaping (\text in complex scripts, emoji).
    struct Text {
        float x = 0, y = 0;   // top-left corner of the layout
        D2D1_RECT_F ink{};    // around its ink, from (x, y)
        uint32_t color = 0;
        ComPtr<IDWriteTextLayout> layout;
    };
    float width = 0, ascent = 0, descent = 0;
    std::vector<Glyph> glyphs;
    std::vector<Rule> rules;
    std::vector<Text> texts;
    // Filled shapes of the stretched glyphs, built on first draw (for the
    // Direct2D factory they belong to).
    mutable ComPtr<ID2D1Factory> shapes_factory;
    mutable std::unordered_map<uint16_t, ComPtr<ID2D1PathGeometry>> shapes;
};

class MathTypesetter {
public:
    MathTypesetter();
    ~MathTypesetter();
    MathTypesetter(const MathTypesetter&) = delete;
    MathTypesetter& operator=(const MathTypesetter&) = delete;

    // Loads the math font; false if there is none (formulas stay source).
    bool Init(IDWriteFactory* dw);

    // Typesets TeX source at `size` DIPs per em, in display or text style.
    std::shared_ptr<const MathBox> Layout(std::wstring_view tex, bool display, float size);

    // x-height of the math font, per em (math is scaled to match the text).
    float XHeight() const;

    // Draws a formula with its origin at `origin`, scaled by `scale`; rules
    // snap to device pixels.
    void Draw(ID2D1RenderTarget* rt, const MathBox& box, D2D1_POINT_2F origin, float scale, ID2D1Brush* brush) const;

    struct Impl;  // font data (mathlayout.cpp)

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace mdv
