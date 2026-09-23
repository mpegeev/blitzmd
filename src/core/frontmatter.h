// YAML front matter (Jekyll, Hugo, Docusaurus...) at the top of a Markdown
// file, shown like GitHub does: as a table of the top-level keys.
#pragma once

#include <string>
#include <string_view>

namespace mdv {

// If `md` starts with a front matter block ("---" ... "---" or "..."),
// returns its length in bytes (closing line included) and sets `replacement`
// to Markdown that renders it: an HTML table, or a ```yaml code block when
// the YAML is beyond the supported subset. Returns 0 otherwise.
size_t RenderFrontMatter(std::string_view md, std::string& replacement);

}  // namespace mdv
