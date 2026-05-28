#include "DistanceField.h"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace DistanceField {

namespace {

using Primitive  = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
using AABBTraits = CGAL::AABB_traits_3<Kernel, Primitive>;
using AABBTree   = CGAL::AABB_tree<AABBTraits>;

} // namespace

void compute(ScanData& scan, const ScanData& reference)
{
    const SurfaceMesh& refMesh = reference.mesh;

    AABBTree tree(faces(refMesh).first, faces(refMesh).second, refMesh);
    tree.accelerate_distance_queries();

    std::size_t nVerts = scan.mesh.num_vertices();
    scan.distanceToRef.resize(nVerts);

    for (auto v : scan.mesh.vertices()) {
        const Point3& p = scan.mesh.point(v);
        auto result = tree.closest_point_and_primitive(p);
        const Point3& closestPt = result.first;
        FaceDesc      primId    = result.second;

        double dist = std::sqrt(CGAL::to_double(
            CGAL::squared_distance(p, closestPt)));

        // sign: dot(p - closestPt, face_normal)
        // compute face normal on the fly to avoid non-const property map
        auto hh = refMesh.halfedge(primId);
        const Point3& fp0 = refMesh.point(refMesh.source(hh));
        const Point3& fp1 = refMesh.point(refMesh.target(hh));
        const Point3& fp2 = refMesh.point(refMesh.target(refMesh.next(hh)));
        Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

        Vector3K diff(p.x() - closestPt.x(),
                      p.y() - closestPt.y(),
                      p.z() - closestPt.z());
        double dot = CGAL::to_double(diff * fn);
        scan.distanceToRef[v.idx()] = (dot >= 0.0) ? dist : -dist;
    }

    scan.distanceComputed = true;
}

void fillReport(const ScanData& scan, MetricReport& report,
                double coverageThreshold, double zWindowMm)
{
    if (!scan.distanceComputed || scan.distanceToRef.empty()) return;

    // Optionally restrict to occlusal zone: find Z_max then keep only
    // vertices within zWindowMm below it (tooth crowns, not gingiva).
    double zThresh = -std::numeric_limits<double>::infinity();
    if (zWindowMm > 0.0) {
        double zMax = -std::numeric_limits<double>::infinity();
        for (auto v : scan.mesh.vertices())
            zMax = std::max(zMax, CGAL::to_double(scan.mesh.point(v).z()));
        zThresh = zMax - zWindowMm;
    }

    std::vector<double> d;
    d.reserve(scan.distanceToRef.size());
    for (auto v : scan.mesh.vertices()) {
        if (zWindowMm > 0.0 &&
            CGAL::to_double(scan.mesh.point(v).z()) < zThresh)
            continue;
        d.push_back(scan.distanceToRef[v.idx()]);
    }
    if (d.empty()) return;

    const std::size_t n = d.size();

    // RMS
    double rms2 = 0.0;
    for (double v : d) rms2 += v * v;
    report.rmsDistance = std::sqrt(rms2 / n);

    // signed mean
    double sum = std::accumulate(d.begin(), d.end(), 0.0);
    report.signedMean = sum / n;

    // absolute values for MAD and Hausdorff
    std::vector<double> abs_d(n);
    for (std::size_t i = 0; i < n; ++i) abs_d[i] = std::abs(d[i]);
    std::sort(abs_d.begin(), abs_d.end());

    report.madDistance  = abs_d[n / 2];
    report.hausdorff100 = abs_d.back();
    report.hausdorff95  = abs_d[static_cast<std::size_t>(0.95 * n)];

    // coverage rate
    std::size_t covered = std::count_if(abs_d.begin(), abs_d.end(),
        [&](double v){ return v <= coverageThreshold; });
    report.coverageRate = 100.0 * covered / n;
}

} // namespace DistanceField
