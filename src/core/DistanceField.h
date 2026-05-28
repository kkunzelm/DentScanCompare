#pragma once

#include "Mesh.h"
#include "MetricReport.h"
#include <memory>

namespace DistanceField {

// Computes per-vertex signed distance from each scan to the GPA reference.
// Uses a CGAL AABB tree on the reference mesh for closest-point queries.
// Sign: positive = scan vertex is on the outer side of the reference normal.
// Stores results in scan->distanceToRef and sets scan->distanceComputed = true.
void compute(ScanData& scan, const ScanData& reference);

// Fills the local-accuracy section of report from scan->distanceToRef.
// coverageThreshold: vertex counts as "covered" when |d| <= this [mm].
// zWindowMm: if > 0, only vertices within zWindowMm below the scan's
//   maximum Z (occlusal tip) are included — restricts metrics to the tooth
//   crown zone, excluding gingiva.  0 = use all vertices.
void fillReport(const ScanData& scan, MetricReport& report,
                double coverageThreshold = 0.2,
                double zWindowMm = 0.0);

} // namespace DistanceField
