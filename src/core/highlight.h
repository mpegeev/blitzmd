#pragma once
// Fast, self-contained syntax highlighter for fenced code blocks.
//
// Hand-written, table-driven scanners (no regex, no allocation besides the
// output vector, no exceptions, trivial static initialization). Linear time
// in the input size; safe on arbitrary (garbage) input.
#include <cstdint>
#include <string_view>
#include <vector>

namespace mdv {

enum class TokenKind : uint8_t {
    Plain = 0,   // never emitted as a span; gaps between spans are plain
    Comment,     // comments, shebang lines
    Keyword,     // language keywords, storage modifiers (if/for/class/public/static/return/import...)
    String,      // string & char literals, heredocs, template strings (the non-interpolated parts)
    Number,      // numeric literals
    Constant,    // true/false/null/nil/None/undefined, well-known builtin constants, ALL_CAPS constants (optional)
    Function,    // function/method names at definition or call site (identifier followed by '(' in C-like langs)
    Type,        // type/class names: builtin types (int, string, bool...), identifiers after class/struct/enum/interface/new, PascalCase names in statically-typed languages
    Variable,    // shell/PHP/Perl/PowerShell $variables, Ruby @ivars, CSS custom properties
    Tag,         // HTML/XML tag names (including closing tags), Dockerfile instructions
    Attribute,   // HTML/XML attribute names
    Property,    // JSON/YAML/TOML/INI keys, CSS property names
    Meta,        // preprocessor directives (#include, #define), decorators/annotations (@Override, @dataclass), Rust attributes #[..], INI [section] headers
    Regex,       // regex literals (JS /re/g, Ruby, Perl)
    Escape,      // escape sequences inside strings (\n, \x41, ሴ, %d is NOT needed)
    Inserted,    // diff lines starting with '+'
    Deleted,     // diff lines starting with '-'
    DiffRange,   // diff hunk headers "@@ -1,2 +1,3 @@" and diff file headers (diff --git, index, ---/+++ lines may be Heading)
    Heading,     // markdown headings / emphasis markers in markdown code blocks; diff file header lines
    Count
};

struct HighlightSpan {
    uint32_t start;   // offset in UTF-16 code units into the input
    uint32_t length;  // length in UTF-16 code units (> 0)
    TokenKind kind;   // never Plain
};

// Highlights `code` written in language `lang` (the info string of a fenced
// code block, e.g. L"cpp", L"Python", L"js", L"c++", L"shell-session"; only
// the first word matters, compare case-insensitively; may be empty).
// Appends spans to `out` (does not clear it) in strictly ascending, non-overlapping
// order. Returns false (and appends nothing) if the language is unknown/empty.
bool HighlightCode(std::wstring_view code, std::wstring_view lang,
                   std::vector<HighlightSpan>& out);

// True if `lang` names a language HighlightCode supports.
bool IsKnownLanguage(std::wstring_view lang);

}  // namespace mdv
