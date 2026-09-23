// Country and subdivision flags as emoji. Windows' emoji font has none (a
// pair of regional indicators shows as two letters), so flags are drawn from
// an embedded set of Twemoji SVGs (scripts/gen_flags.py) over their original
// characters: copying and searching still see the text.
#pragma once

#include <string>
#include <vector>

#include "core/document.h"

namespace mdv {

// Covers every flag in the document's leaves (regional indicator pairs such
// as 🇺🇸, tag sequences such as England's 🏴 + "gbeng") with an inline
// object showing an ImageRef::flag image. Flags missing from the embedded
// set, and those in superscript or subscript, stay text.
void AddFlagImages(Document& doc);

// SVG sources of the flags with the given region codes ("us", "gbeng"), in
// the same order; empty for unknown codes or if the set can't be read
// (Windows 7 has no decompressor).
std::vector<std::string> FlagSvgs(const std::vector<std::string>& keys);

}  // namespace mdv
