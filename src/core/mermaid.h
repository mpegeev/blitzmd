// Mermaid diagrams (```mermaid blocks), parsed and laid out natively: no
// browser, no JavaScript. Supported types draw as scenes (diagram.h); others,
// and sources with errors, stay code with a note saying why.
#pragma once

#include <dwrite.h>

#include <memory>
#include <string>
#include <string_view>

#include "core/diagram.h"

namespace mdv {

// Limits for hostile input: Mermaid's own (maxTextSize).
constexpr size_t kMaxMermaidSource = 50000;  // characters

// Whether `source` is a diagram BlitzMD draws. If not, `error` says why
// ("Unsupported diagram type: gitGraph", "Line 3: expected ']'").
bool CheckMermaid(std::wstring_view source, std::wstring* error);

// Parses and lays out `source` with text in `family` at `size` DIPs per em.
// Returns nullptr (and the reason in `error`) if it can't.
std::shared_ptr<DiagramScene> LayoutMermaid(std::wstring_view source, IDWriteFactory* dw, const std::wstring& family,
                                            float size, std::wstring* error);

// In place of a diagram that parsed but couldn't be laid out (too large to):
// a note saying why.
std::shared_ptr<DiagramScene> MermaidErrorScene(const std::wstring& error, IDWriteFactory* dw,
                                                const std::wstring& family, float size);

}  // namespace mdv
