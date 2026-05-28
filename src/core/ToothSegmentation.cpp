#include "ToothSegmentation.h"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>

#include <Eigen/Core>

#include <array>
#include <cmath>
#include <numbers>
#include <queue>
#include <vector>

namespace ToothSegmentation {

namespace {

// ── Per-face precomputed data ─────────────────────────────────────────────────

struct FaceData {
    Eigen::Vector3d normal;     // outward unit normal
    double          meanCurv;   // mean |κ_H| averaged over 3 vertices
    double          centerZ;    // Z of face centroid
};

std::vector<FaceData> buildFaceData(const ScanData& scan)
{
    const auto& mesh = scan.mesh;
    std::vector<FaceData> fd(mesh.number_of_faces());

    auto curvMapOpt = mesh.property_map<VertexDesc, double>("v:mean_curv");
    const bool haveCurv = curvMapOpt.has_value();

    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc, 3> vs;
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h)) vs[i++] = vd;

        const Point3& p0 = mesh.point(vs[0]);
        const Point3& p1 = mesh.point(vs[1]);
        const Point3& p2 = mesh.point(vs[2]);

        // Face normal via cross product
        Vector3K fn = CGAL::cross_product(p1 - p0, p2 - p0);
        double   len = std::sqrt(CGAL::to_double(fn.squared_length()));
        Eigen::Vector3d n(0.0, 0.0, 1.0);
        if (len > 1e-12)
            n = Eigen::Vector3d(CGAL::to_double(fn.x()) / len,
                                CGAL::to_double(fn.y()) / len,
                                CGAL::to_double(fn.z()) / len);

        // Face centroid Z
        double cz = (CGAL::to_double(p0.z()) +
                     CGAL::to_double(p1.z()) +
                     CGAL::to_double(p2.z())) / 3.0;

        // Face mean curvature (average of vertex curvatures)
        double kMean = 0.0;
        if (haveCurv) {
            const auto& cm = curvMapOpt.value();
            kMean = (get(cm, vs[0]) + get(cm, vs[1]) + get(cm, vs[2])) / 3.0;
        }

        fd[f.idx()] = {n, kMean, cz};
    }
    return fd;
}

// ── Face adjacency: find all faces sharing an edge with f ────────────────────

std::vector<FaceDesc> adjacentFaces(const SurfaceMesh& mesh, FaceDesc f)
{
    std::vector<FaceDesc> nbrs;
    for (auto h : mesh.halfedges_around_face(mesh.halfedge(f))) {
        auto opp = mesh.opposite(h);
        if (!mesh.is_border(opp))
            nbrs.push_back(mesh.face(opp));
    }
    return nbrs;
}

// ── Find vertex nearest to a 3-D world position ──────────────────────────────

VertexDesc nearestVertex(const SurfaceMesh& mesh,
                         const std::array<double,3>& pt)
{
    VertexDesc best;
    double bestSq = std::numeric_limits<double>::infinity();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        double dx = p.x() - pt[0];
        double dy = p.y() - pt[1];
        double dz = p.z() - pt[2];
        double sq = dx*dx + dy*dy + dz*dz;
        if (sq < bestSq) { bestSq = sq; best = v; }
    }
    return best;
}

// ── Find a face incident to vertex v ─────────────────────────────────────────

FaceDesc incidentFace(const SurfaceMesh& mesh, VertexDesc v)
{
    for (auto h : mesh.halfedges_around_target(mesh.halfedge(v)))
        if (!mesh.is_border(h))
            return mesh.face(h);
    return SurfaceMesh::null_face();
}

} // anonymous namespace

// ── Main segmentation entry point ────────────────────────────────────────────

std::vector<bool> segment(const ScanData& scan,
                          const std::vector<VertexDesc>& seedVertices,
                          const Params& params)
{
    const SurfaceMesh& mesh = scan.mesh;
    const std::size_t  nF   = mesh.number_of_faces();
    const std::size_t  nV   = mesh.num_vertices();

    if (nF == 0 || seedVertices.empty())
        return std::vector<bool>(nV, false);

    // Pre-compute per-face data once
    const auto fd = buildFaceData(scan);

    // Threshold cosines (pre-computed for performance)
    const double cosMaxTilt   = std::cos(params.maxNormalTiltDeg  * std::numbers::pi / 180.0);
    const double cosMaxCrease = std::cos(params.maxCreaseAngleDeg * std::numbers::pi / 180.0);
    const Eigen::Vector3d Zup(0.0, 0.0, 1.0);

    // Per-face label: -1 = unvisited, ≥0 = seed index
    std::vector<int> faceLabel(nF, -1);

    // Multi-source BFS (one queue per seed, interleaved for fair expansion)
    // Implemented as a single BFS with per-seed counters
    struct Entry { FaceDesc face; int seed; };
    std::queue<Entry> frontier;

    for (int si = 0; si < static_cast<int>(seedVertices.size()); ++si) {
        FaceDesc seedFace = incidentFace(mesh, seedVertices[si]);
        if (seedFace == SurfaceMesh::null_face()) continue;

        // Accept the seed face unconditionally (it IS a tooth crown by definition)
        if (faceLabel[seedFace.idx()] == -1) {
            faceLabel[seedFace.idx()] = si;
            frontier.push({seedFace, si});
        }
    }

    // Per-seed expansion count (safety cap)
    std::vector<int> expanded(seedVertices.size(), 0);

    while (!frontier.empty()) {
        auto [f, seed] = frontier.front();
        frontier.pop();

        if (expanded[seed] >= params.maxFacesPerSeed) continue;
        ++expanded[seed];

        const FaceData& cur = fd[f.idx()];

        for (FaceDesc nb : adjacentFaces(mesh, f)) {
            if (faceLabel[nb.idx()] != -1) continue; // already labelled

            const FaceData& nf = fd[nb.idx()];

            // ── Stopping criterion 1: face normal too tilted from +Z ──────
            if (nf.normal.dot(Zup) < cosMaxTilt) continue;

            // ── Stopping criterion 2: sharp crease at the shared edge ──────
            // (CEJ creates a kink where enamel meets root surface)
            if (cur.normal.dot(nf.normal) < cosMaxCrease) continue;

            // ── Stopping criterion 3: concave gingival sulcus ─────────────
            if (nf.meanCurv < params.minMeanCurvature) continue;

            faceLabel[nb.idx()] = seed;
            frontier.push({nb, seed});
        }
    }

    // Map face labels to per-vertex mask.
    // A vertex is "tooth" when at least one incident face is labelled.
    std::vector<bool> toothVertex(nV, false);
    for (auto v : mesh.vertices()) {
        for (auto h : mesh.halfedges_around_target(mesh.halfedge(v))) {
            if (!mesh.is_border(h) &&
                faceLabel[mesh.face(h).idx()] >= 0) {
                toothVertex[v.idx()] = true;
                break;
            }
        }
    }

    return toothVertex;
}

std::vector<bool> segmentFromPoints(const ScanData& scan,
                                    const std::vector<std::array<double,3>>& pts,
                                    const Params& params)
{
    std::vector<VertexDesc> seeds;
    seeds.reserve(pts.size());
    for (const auto& pt : pts)
        seeds.push_back(nearestVertex(scan.mesh, pt));
    return segment(scan, seeds, params);
}

} // namespace ToothSegmentation
