#pragma once

/**
 * @file MeshReader.h
 * @brief Unified 3D mesh file reader for DentScanCompare
 *
 * Reads various 3D mesh formats into ScanData with CGAL SurfaceMesh:
 * - STL (binary)
 * - PLY (with faces)
 * - OBJ
 *
 * Cross-platform compatible (Linux, Windows).
 */

#include "Mesh.h"
#include <memory>
#include <string>

namespace MeshReader {

/// Supported mesh file formats
enum class MeshFormat {
    Unknown,
    STL,
    PLY,
    OBJ
};

/**
 * @brief Detect mesh format from file extension
 * @param filePath Path to mesh file
 * @return Detected format, or Unknown if not recognized
 */
MeshFormat detectFormat(const std::string& filePath);

/**
 * @brief Read a mesh file (auto-detect format)
 *
 * Reads STL, PLY, or OBJ files based on extension.
 *
 * @param filePath Full path to the mesh file
 * @param errorMsg Output: error message if reading fails
 * @return Shared pointer to ScanData, or nullptr on error
 */
std::shared_ptr<ScanData> read(const std::string& filePath, std::string& errorMsg);

/**
 * @brief Read a PLY mesh file
 *
 * Reads PLY files with face data into CGAL SurfaceMesh.
 *
 * @param filePath Full path to the PLY file
 * @param errorMsg Output: error message if reading fails
 * @return Shared pointer to ScanData, or nullptr on error
 */
std::shared_ptr<ScanData> readPLY(const std::string& filePath, std::string& errorMsg);

/**
 * @brief Read an OBJ mesh file
 *
 * @param filePath Full path to the OBJ file
 * @param errorMsg Output: error message if reading fails
 * @return Shared pointer to ScanData, or nullptr on error
 */
std::shared_ptr<ScanData> readOBJ(const std::string& filePath, std::string& errorMsg);

/**
 * @brief Get file filter string for open dialogs
 * @return Filter string like "3D Mesh Files (*.stl *.ply *.obj)"
 */
std::string getMeshFileFilter();

} // namespace MeshReader
