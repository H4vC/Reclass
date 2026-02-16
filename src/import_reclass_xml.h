#pragma once
#include "core.h"

namespace rcx {

// Import a ReClass XML file (.reclass, .MemeCls, etc.) into a NodeTree.
// Supports ReClassEx, MemeClsEx, ReClass 2011/2013/2016 XML formats.
// Returns an empty NodeTree on failure; populates errorMsg if non-null.
NodeTree importReclassXml(const QString& filePath, QString* errorMsg = nullptr);

} // namespace rcx
