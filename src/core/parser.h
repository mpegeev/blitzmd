#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "core/document.h"

namespace mdv {

struct ParseOptions {
    bool emoji_shortcodes = true;   // :rocket: -> 🚀
    bool math = true;               // $...$ and $$...$$
    bool mermaid = true;            // ```mermaid blocks as diagrams
    bool highlight_code = true;     // syntax highlighting in code blocks
    int footnote_nesting = 0;       // internal: footnote bodies parse recursively
    int footnote_base = 0;          // internal: a body's own footnotes are numbered after this
    // At most this many blocks (a few hundred bytes each: tiny paragraphs
    // make a model ~100 times the file). Past it the parse stops and the
    // document ends with a warning.
    size_t max_blocks = sizeof(void*) == 8 ? 2'000'000 : 500'000;
};

// Parses UTF-8 Markdown (GitHub Flavored Markdown + GitHub extensions:
// alerts, footnotes, math, emoji shortcodes, a safe subset of raw HTML).
// `path` is used to resolve relative links and images. Never throws except
// std::bad_alloc.
std::unique_ptr<Document> ParseMarkdown(std::string_view utf8, std::wstring_view path,
                                        const ParseOptions& options = {});

// GitHub-compatible heading slug ("Hello, World!" -> "hello-world").
std::wstring MakeSlug(std::wstring_view text);

}  // namespace mdv
