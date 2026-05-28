#pragma once

#include "Mesh.h"
#include "MetricReport.h"
#include <Eigen/Core>

namespace DistanceField {

// Defines a fitted occlusal plane with asymmetric offsets.
// After the user picks ≥3 cusp points, a least-squares plane is fitted.
// Only mesh vertices within [origin − belowMm·n, origin + aboveMm·n] are
// included in the accuracy statistics.
struct OcclusalPlane {
    Eigen::Vector3d normal = {0.0, 0.0, 1.0}; // unit normal pointing toward teeth (+Z after PCA)
    Eigen::Vector3d origin = {0.0, 0.0, 0.0}; // centroid of picked cusp points
    double aboveMm = 2.0;   // include vertices up to this far above the plane [mm]
    double belowMm = 12.0;  // include vertices up to this far below the plane [mm]
    bool   active  = false;
};

// Computes per-vertex signed distance from each scan to the GPA reference.
// Sign: positive = scan vertex is on the outer side of the reference normal.
// Stores results in scan->distanceToRef and sets scan->distanceComputed = true.
void compute(ScanData& scan, const ScanData& reference);

// Fills the local-accuracy section of report from scan->distanceToRef.
// Priority:  plane.active → plane-based filter (ignores zWindowMm)
//            zWindowMm > 0 → simple Z-max window (legacy)
//            otherwise → all vertices
void fillReport(const ScanData& scan, MetricReport& report,
                double coverageThreshold = 0.2,
                double zWindowMm = 0.0,
                const OcclusalPlane& plane = {});

} // namespace DistanceField
