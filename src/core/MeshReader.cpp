// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>

#include "MeshReader.h"
#include "STLReader.h"

#include <CGAL/IO/PLY.h>
#include <CGAL/IO/OBJ.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair_polygon_soup.h>

// Surface reconstruction from point clouds
#include <CGAL/Advancing_front_surface_reconstruction.h>
#include <CGAL/disable_warnings.h>

// For normal estimation (required by some reconstruction methods)
#include <CGAL/pca_estimate_normals.h>
#include <CGAL/mst_orient_normals.h>
#include <CGAL/property_map.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace MeshReader {

namespace {

// Helper: get lowercase file extension
std::string getLowerExtension(const std::string& filePath) {
    std::filesystem::path p(filePath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// Helper: extract scanner name from filename
// e.g. "DefektIIa_Primescan_30_3min23s_r3" -> "Primescan"
std::string extractScannerName(const std::string& filePath) {
    std::filesystem::path p(filePath);
    std::string stem = p.stem().string();
    std::istringstream ss(stem);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(ss, token, '_'))
        tokens.push_back(token);
    return tokens.size() >= 2 ? tokens[1] : stem;
}

// Helper: compute bounding box for mesh
void computeBounds(std::shared_ptr<ScanData>& scanData) {
    if (!scanData || scanData->mesh.is_empty()) return;

    bool first = true;
    for (auto v : scanData->mesh.vertices()) {
        const Point3& p = scanData->mesh.point(v);
        if (first) {
            scanData->boundsMin = {p.x(), p.y(), p.z()};
            scanData->boundsMax = {p.x(), p.y(), p.z()};
            first = false;
        } else {
            scanData->boundsMin[0] = std::min(scanData->boundsMin[0], p.x());
            scanData->boundsMin[1] = std::min(scanData->boundsMin[1], p.y());
            scanData->boundsMin[2] = std::min(scanData->boundsMin[2], p.z());
            scanData->boundsMax[0] = std::max(scanData->boundsMax[0], p.x());
            scanData->boundsMax[1] = std::max(scanData->boundsMax[1], p.y());
            scanData->boundsMax[2] = std::max(scanData->boundsMax[2], p.z());
        }
    }
}

// Priority functor for Advancing Front - controls triangle quality
// Lower radius bound means fewer long/skinny triangles
struct AdvancingFrontPriority {
    double bound;

    explicit AdvancingFrontPriority(double b = 0.0) : bound(b) {}

    template <typename AdvancingFront, typename Cell_handle>
    double operator()(const AdvancingFront& adv, Cell_handle& c,
                      const int& index) const {
        // Convex hull cells have infinite radius - skip them
        if (bound == 0.0) {
            return adv.smallest_radius_delaunay_sphere(c, index);
        }

        // Use smallest Delaunay sphere radius as priority
        double r = adv.smallest_radius_delaunay_sphere(c, index);

        // If radius exceeds bound, return infinity to skip this facet
        if (r > bound) {
            return std::numeric_limits<double>::infinity();
        }
        return r;
    }
};

// Triangulate a point cloud using CGAL's Advancing Front Surface Reconstruction
// This is well-suited for dental scan data which is typically well-sampled
bool triangulatePointCloud(const std::vector<Point3>& points,
                           SurfaceMesh& mesh,
                           std::string& errorMsg) {
    if (points.size() < 3) {
        errorMsg = "Point cloud has fewer than 3 points";
        return false;
    }

    try {
        // Compute approximate bounding box diagonal for radius bound estimation
        Point3 pmin = points[0], pmax = points[0];
        for (const auto& p : points) {
            pmin = Point3(std::min(pmin.x(), p.x()),
                          std::min(pmin.y(), p.y()),
                          std::min(pmin.z(), p.z()));
            pmax = Point3(std::max(pmax.x(), p.x()),
                          std::max(pmax.y(), p.y()),
                          std::max(pmax.z(), p.z()));
        }
        double diag = std::sqrt(CGAL::squared_distance(pmin, pmax));

        // Use a radius bound proportional to point cloud size
        // This helps filter out very large triangles at boundaries
        // For dental scans, typical spacing is ~0.1mm, so we use a generous bound
        double radiusBound = diag * 0.05; // 5% of diagonal

        // Collect triangle indices from advancing front reconstruction
        std::vector<std::array<std::size_t, 3>> triangleIndices;

        // Perform advancing front surface reconstruction with priority
        AdvancingFrontPriority priority(radiusBound);

        CGAL::advancing_front_surface_reconstruction(
            points.begin(),
            points.end(),
            std::back_inserter(triangleIndices),
            priority);

        if (triangleIndices.empty()) {
            // Try again without radius bound (more permissive)
            CGAL::advancing_front_surface_reconstruction(
                points.begin(),
                points.end(),
                std::back_inserter(triangleIndices));
        }

        if (triangleIndices.empty()) {
            errorMsg = "Surface reconstruction produced no triangles";
            return false;
        }

        // Convert to polygon soup format for mesh construction
        std::vector<std::vector<std::size_t>> polygons;
        polygons.reserve(triangleIndices.size());
        for (const auto& tri : triangleIndices) {
            polygons.push_back({tri[0], tri[1], tri[2]});
        }

        // Repair and orient the polygon soup, then convert to mesh
        namespace PMP = CGAL::Polygon_mesh_processing;

        // Make a copy of points since repair_polygon_soup may modify it
        std::vector<Point3> pointsCopy = points;
        PMP::repair_polygon_soup(pointsCopy, polygons);
        PMP::orient_polygon_soup(pointsCopy, polygons);
        PMP::polygon_soup_to_polygon_mesh(pointsCopy, polygons, mesh);

        if (mesh.is_empty()) {
            errorMsg = "Surface reconstruction produced empty mesh";
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        errorMsg = std::string("Surface reconstruction failed: ") + e.what();
        return false;
    }
}

} // anonymous namespace

MeshFormat detectFormat(const std::string& filePath) {
    std::string ext = getLowerExtension(filePath);
    if (ext == ".stl") return MeshFormat::STL;
    if (ext == ".ply") return MeshFormat::PLY;
    if (ext == ".obj") return MeshFormat::OBJ;
    return MeshFormat::Unknown;
}

std::shared_ptr<ScanData> read(const std::string& filePath, std::string& errorMsg) {
    MeshFormat format = detectFormat(filePath);

    switch (format) {
    case MeshFormat::STL:
        return STLReader::read(filePath, errorMsg);
    case MeshFormat::PLY:
        return readPLY(filePath, errorMsg);
    case MeshFormat::OBJ:
        return readOBJ(filePath, errorMsg);
    default:
        errorMsg = "Unknown file format: " + filePath;
        return nullptr;
    }
}

std::shared_ptr<ScanData> readPLY(const std::string& filePath, std::string& errorMsg) {
    auto scanData = std::make_shared<ScanData>();

    // Try to read directly as a polygon mesh first
    std::ifstream input(filePath);
    if (!input) {
        errorMsg = "Cannot open file: " + filePath;
        return nullptr;
    }

    if (!CGAL::IO::read_PLY(input, scanData->mesh)) {
        // If direct read fails, try polygon soup approach
        input.close();
        input.open(filePath);

        std::vector<Point3> points;
        std::vector<std::vector<std::size_t>> polygons;

        if (!CGAL::IO::read_PLY(input, points, polygons)) {
            errorMsg = "Failed to read PLY file: " + filePath;
            return nullptr;
        }

        if (polygons.empty()) {
            // This is a point cloud - triangulate it using surface reconstruction
            if (!triangulatePointCloud(points, scanData->mesh, errorMsg)) {
                return nullptr;
            }
            scanData->stlHeader = "(PLY point cloud - triangulated)";
        } else {
            // Has faces - repair and convert polygon soup
            namespace PMP = CGAL::Polygon_mesh_processing;
            PMP::repair_polygon_soup(points, polygons);
            PMP::orient_polygon_soup(points, polygons);
            PMP::polygon_soup_to_polygon_mesh(points, polygons, scanData->mesh);
            scanData->stlHeader = "(PLY mesh file)";
        }
    } else {
        scanData->stlHeader = "(PLY mesh file)";
    }

    if (scanData->mesh.is_empty()) {
        errorMsg = "PLY mesh is empty or failed to convert: " + filePath;
        return nullptr;
    }

    // Fill metadata
    scanData->filePath = filePath;
    scanData->scannerName = extractScannerName(filePath);
    scanData->triangleCount = scanData->mesh.number_of_faces();

    // Compute bounding box
    computeBounds(scanData);

    return scanData;
}

std::shared_ptr<ScanData> readOBJ(const std::string& filePath, std::string& errorMsg) {
    auto scanData = std::make_shared<ScanData>();

    std::ifstream input(filePath);
    if (!input) {
        errorMsg = "Cannot open file: " + filePath;
        return nullptr;
    }

    if (!CGAL::IO::read_OBJ(input, scanData->mesh)) {
        // If direct read fails, try polygon soup approach
        input.close();
        input.open(filePath);

        std::vector<Point3> points;
        std::vector<std::vector<std::size_t>> polygons;

        if (!CGAL::IO::read_OBJ(input, points, polygons)) {
            errorMsg = "Failed to read OBJ file: " + filePath;
            return nullptr;
        }

        if (polygons.empty()) {
            // OBJ without faces - triangulate point cloud
            if (!triangulatePointCloud(points, scanData->mesh, errorMsg)) {
                return nullptr;
            }
            scanData->stlHeader = "(OBJ point cloud - triangulated)";
        } else {
            // Repair and orient the polygon soup
            namespace PMP = CGAL::Polygon_mesh_processing;
            PMP::repair_polygon_soup(points, polygons);
            PMP::orient_polygon_soup(points, polygons);
            PMP::polygon_soup_to_polygon_mesh(points, polygons, scanData->mesh);
            scanData->stlHeader = "(OBJ mesh file)";
        }
    } else {
        scanData->stlHeader = "(OBJ mesh file)";
    }

    if (scanData->mesh.is_empty()) {
        errorMsg = "OBJ mesh is empty or failed to convert: " + filePath;
        return nullptr;
    }

    // Fill metadata
    scanData->filePath = filePath;
    scanData->scannerName = extractScannerName(filePath);
    scanData->triangleCount = scanData->mesh.number_of_faces();

    // Compute bounding box
    computeBounds(scanData);

    return scanData;
}

std::string getMeshFileFilter() {
    return "3D Mesh Files (*.stl *.STL *.ply *.PLY *.obj *.OBJ);;"
           "STL Files (*.stl *.STL);;"
           "PLY Files (*.ply *.PLY);;"
           "OBJ Files (*.obj *.OBJ);;"
           "All Files (*)";
}

} // namespace MeshReader
