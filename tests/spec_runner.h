#pragma once

#include <string>

namespace mdv::test {

// Runs all examples of a CommonMark/GFM spec file through the parser and
// compares the serialized document with the expected HTML. Returns 0 if
// every example that does not involve raw HTML passes.
int RunSpec(const std::string& path, bool verbose, int only_example);

}  // namespace mdv::test
