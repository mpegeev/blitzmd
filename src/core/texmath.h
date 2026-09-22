// Readable rendering of LaTeX math ($...$, $$...$$, ```math) without a TeX
// engine: TeX is converted to Unicode math text (italic variables, Greek,
// operators, super/subscripts, fractions, roots, accents, matrices).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mdv {

struct MathPiece {
    std::wstring text;
    uint32_t flags = 0;   // kSup / kSub (see document.h)
};

// Converts TeX source to Unicode pieces. `display` enables multi-line layout
// of environments like aligned/cases/matrix (rows become lines).
void TexToUnicode(std::wstring_view tex, bool display, std::vector<MathPiece>& out);

}  // namespace mdv
