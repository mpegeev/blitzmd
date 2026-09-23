// TeX math parsing: the source of $…$, $$…$$ and ```math becomes a tree of
// math atoms (TeX's noads) that mathlayout.* typesets. Covers what READMEs
// use: fractions, roots, scripts and limits, big operators, \left…\right,
// matrices and other environments, accents, math fonts, \text, spacing and
// colors. Parsing never fails: unknown commands are kept as their source.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mdv {

// Atom classes: the spacing between atoms depends on them (TeXbook ch. 18).
enum class MathClass : uint8_t { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

enum class MathKind : uint8_t {
    List,     // kids: a horizontal list; a {group} is an atom of class `cls`
    Glyphs,   // text: math symbols forming one atom ("𝑥", "≠", "∑")
    Text,     // text: upright text (\text, operator names); spaces kept
    Space,    // value: width in mu (1/18 em), may be negative
    Style,    // value: style of the rest of the list (0 display, 1 text, 2 script, 3 scriptscript)
    Scripts,  // kids: base, subscript, superscript (empty lists when absent)
    Frac,     // kids: numerator, denominator; value: rule (-1 default, 0 none); left/right
    Radical,  // kids: radicand, degree (empty list when absent)
    Accent,   // kids: base; text: accent; stretchy: as wide as the base
    Over,     // kids: base, over (empty when `text` is a stretchy brace drawn above)
    Under,    // kids: base, under (idem, below)
    Overline, // kids: base; value: 0 \overline, 1 \underline
    Fenced,   // kids: the lists between \left, \middle… and \right; text: the delimiters
    Delim,    // text: a delimiter (0 = none); value: height in em (\big…)
    Array,    // kids: rows (lists of cell lists); text: column alignments; env; left/right
    Arrow,    // text: the arrow; kids: label above, label below (\xrightarrow)
    Boxed,    // kids: content (\boxed)
    Phantom,  // kids: content, measured but not drawn; value: 0 both, 1 \hphantom, 2 \vphantom
};

// Array environments: they differ in style, spacing and alignment.
enum class MathEnv : uint8_t { Matrix, Cases, Aligned, Gathered, Array, Small };

struct MathNode {
    MathKind kind = MathKind::List;
    MathClass cls = MathClass::Ord;
    uint8_t limits = 0;         // Op: 0 in display style only, 1 \limits, 2 \nolimits
    bool large = false;         // Glyphs: big operator (∑, ∫…), bigger in display style
    bool stretchy = false;      // Accent: widened to the base
    bool error = false;         // unknown command, drawn in red like MathJax does
    int8_t style = -1;          // Frac: forced style (\dfrac, \tfrac); -1 = the usual
    MathEnv env = MathEnv::Matrix;
    float value = 0;
    wchar_t left = 0, right = 0;  // Frac / Array delimiters (0 = none)
    uint32_t color = 0;         // 0 = inherited, else 0xAARRGGBB
    std::wstring text;
    std::vector<MathNode> kids;
};

// Parses TeX math source.
MathNode ParseTex(std::wstring_view tex);

// Compact description of a tree (tests).
std::wstring DumpMath(const MathNode& node);

}  // namespace mdv
