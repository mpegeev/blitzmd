#pragma once

#include <string>

namespace mdv::test {

// Runs all examples of a CommonMark/GFM spec file through the parser and
// compares the serialized document with the expected HTML. Returns 0 if
// every example passes, except those involving raw HTML and those where we
// render as md4c does and md4c differs from the spec (unless `strict`), and
// at least one example ran.
int RunSpec(const std::string& path, bool verbose, int only_example, bool strict = false);

}  // namespace mdv::test
