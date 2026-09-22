#pragma once
#include <string_view>

namespace mdv {
// Looks up a GitHub emoji shortcode name (WITHOUT the surrounding colons),
// e.g. "rocket", "+1", "thumbsup", "flag-ua"... Matching is case-sensitive,
// exactly like GitHub. Returns the emoji as UTF-16 text, or an empty view
// if the name is unknown.
std::wstring_view LookupEmojiShortcode(std::string_view name);
}
