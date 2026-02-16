#pragma once
#include "core.h"

namespace rcx {

// Import C/C++ struct definitions from source code into a NodeTree.
// Supports two modes (auto-detected):
//   1. With comment offsets (// 0xNN) - trusts the offset values
//   2. Without comment offsets - computes offsets from type sizes
// Returns an empty NodeTree on failure; populates errorMsg if non-null.
NodeTree importFromSource(const QString& sourceCode, QString* errorMsg = nullptr);

} // namespace rcx
