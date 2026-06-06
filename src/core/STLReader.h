#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>


#include "Mesh.h"
#include <memory>
#include <string>

namespace STLReader {

// Reads a binary STL file into ScanData.
// Extracts the 80-byte header, builds a CGAL SurfaceMesh via polygon-soup
// conversion (handles duplicate vertices automatically).
// Returns nullptr on error; errorMsg is set accordingly.
std::shared_ptr<ScanData> read(const std::string& filePath, std::string& errorMsg);

} // namespace STLReader
