#pragma once

#include "Mesh.h"
#include <vector>

// ── Tooth Crown Segmentation ─────────────────────────────────────────────────
//
// Implements a mesh-based region growing ("snapping snake") that expands
// outward from one seed point per tooth crown and stops at the gingival
// margin.
//
// The gingival margin is detected via three complementary geometric cues that
// are available from STL data without colour or texture information:
//
//   1. Absolute normal tilt from the occlusal axis (+Z after PCA alignment):
//      tooth surfaces face mostly upward; gingival surfaces face sideways or
//      downward.  Faces whose normal deviates more than maxNormalTiltDeg from
//      +Z are excluded.
//
//   2. Adjacent-face crease angle: the cementoenamel junction (CEJ) creates a
//      sharp kink between the enamel and root/gingival surfaces.  Region
//      growth is blocked when the angle between adjacent face normals exceeds
//      maxCreaseAngleDeg.
//
//   3. Mean curvature sign: the gingival sulcus is concave (κ_H < 0).  Faces
//      with mean curvature below minMeanCurvature are excluded.
//      Requires scan.curvatureComputed == true.
//
// Usage:
//   1. Compute curvature (CurvatureAnalysis::compute).
//   2. Register all scans (GPAReference::compute).
//   3. Let operator pick one point per tooth (already wired in MainWindow).
//   4. Call segment() — returns a per-vertex bool mask (true = tooth crown).
//   5. Pass mask to DistanceField::fillReport() to restrict metrics.

namespace ToothSegmentation {

struct Params {
    double maxNormalTiltDeg  = 75.0;  // exclude face if normal deviates > this from +Z [°]
    double maxCreaseAngleDeg = 50.0;  // block expansion across edges creased > this [°]
    double minMeanCurvature  = -4.0;  // exclude concave (gingival sulcus) faces [1/mm]
    int    maxFacesPerSeed   = 30000; // safety cap per seed (avoids run-away growth)
};

// Returns a per-vertex boolean mask.  true = vertex belongs to a tooth crown.
// seedVertices: one VertexDesc per tooth (typically the cusp-tip vertex nearest
//               to each operator-picked point).
// Requires curvatureComputed == true on the scan.
std::vector<bool> segment(
    const ScanData& scan,
    const std::vector<VertexDesc>& seedVertices,
    const Params& params = {});

// Convenience overload: seeds given as raw 3D world positions (e.g. from
// vtkCellPicker).  Each position is snapped to the nearest mesh vertex.
std::vector<bool> segmentFromPoints(
    const ScanData& scan,
    const std::vector<std::array<double,3>>& seedPoints,
    const Params& params = {});

} // namespace ToothSegmentation
