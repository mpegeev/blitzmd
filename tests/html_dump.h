// Serializes a Document back to HTML in the format used by the CommonMark /
// GFM spec examples, so the parser + document builder can be verified
// against the official test suites.
#pragma once

#include <string>

#include "core/document.h"

namespace mdv::test {

std::string DumpHtml(const Document& doc);

// Normalizes HTML for comparison: collapses whitespace outside <pre>, drops
// whitespace around block-level tags, sorts attributes, decodes entities.
std::string NormalizeHtml(const std::string& html);

}  // namespace mdv::test
