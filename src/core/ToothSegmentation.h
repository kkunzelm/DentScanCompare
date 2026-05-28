#pragma once

#include "Mesh.h"
#include <vector>

// ── Tooth Crown Segmentation ─────────────────────────────────────────────────
//
// Implements a Dijkstra-based region growing ("snapping snake") that expands
// outward from one seed point per tooth crown and stops at the gingival margin.
//
// PRIMARY stopping criterion – geodesic distance (orientation-independent):
//   The accumulated surface-path distance from the seed is limited to
//   maxGeodesicMm.  A full clinical crown is never more than 10–15 mm of
//   surface path from the cusp tip, regardless of tooth type:
//     Molars      8–12 mm   (wide, occlusal surface flat)
//     Premolars   7–10 mm
//     Canines     9–12 mm   (tall, pointed)
//     Incisors   10–13 mm   (thin blade; palatal surface included)
//   This replaces the earlier normal-tilt-from-Z criterion which incorrectly
//   excluded the palatal/lingual surface of anterior teeth (which faces
//   110–120° from +Z, far above any sensible threshold).
//
// SECONDARY stopping criteria – geometry guards that catch CEJ features:
//   Adjacent-face crease angle: the cementoenamel junction (CEJ) always
//     creates a kink regardless of tooth orientation.  Default 50°.
//   Mean curvature floor: the gingival sulcus is concave (κ_H < 0).
//     Default -4 mm⁻¹.  Both criteria require curvatureComputed == true.
//
// How to pick seeds:
//   Place one seed on the occlusal/incisal surface of each tooth crown.
//   A single click per tooth is sufficient; the geodesic limit handles
//   the rest.  Posterior molars need a larger radius (12 mm) than
//   anterior incisors (10 mm) — a single shared value of 12 mm covers all.

namespace ToothSegmentation {

struct Params {
    double maxGeodesicMm      = 12.0;  // primary: max surface-path distance from seed [mm]
    double maxCreaseAngleDeg  = 50.0;  // secondary: CEJ kink guard [°]
    double minMeanCurvature   = -4.0;  // secondary: gingival sulcus concavity guard [1/mm]
    int    maxFacesPerSeed    = 40000; // hard cap on BFS nodes per seed
};

// Returns a per-vertex boolean mask.  true = vertex belongs to a tooth crown.
// seedVertices: one VertexDesc per tooth (cusp-tip vertex nearest to each
//               operator-picked point).
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
