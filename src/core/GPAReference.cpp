#include "GPAReference.h"

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace GPAReference {

namespace {

// Centre the mesh at its centroid and optionally align PCA axes.
// Returns the 4×4 transform that was applied (so the caller can accumulate it).
Eigen::Matrix4d pcaCoarseAlign(ScanData& scan)
{
    const auto& mesh = scan.mesh;
    std::size_t n = mesh.num_vertices();
    if (n == 0) return Eigen::Matrix4d::Identity();

    // --- centroid ---
    Eigen::Vector3d mu = Eigen::Vector3d::Zero();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        mu += Eigen::Vector3d(p.x(), p.y(), p.z());
    }
    mu /= static_cast<double>(n);

    // --- covariance matrix ---
    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        Eigen::Vector3d d(p.x()-mu[0], p.y()-mu[1], p.z()-mu[2]);
        cov += d * d.transpose();
    }
    cov /= static_cast<double>(n);

    // Eigenvalues sorted ascending; col(2) = largest variance direction.
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
    Eigen::Matrix3d R;
    R.col(0) = eig.eigenvectors().col(2); // largest  → X (left–right)
    R.col(1) = eig.eigenvectors().col(1); // medium   → Y (front–back)
    R.col(2) = eig.eigenvectors().col(0); // smallest → Z (≈ occlusal normal)
    if (R.determinant() < 0) R.col(0) = -R.col(0);

    // --- resolve Z-sign: occlusal (high curvature) should be at +Z ---
    auto meanMapOpt = mesh.property_map<VertexDesc, double>("v:mean_curv");
    if (meanMapOpt.has_value()) {
        const auto& mm = meanMapOpt.value();
        const Eigen::Vector3d& zAx = R.col(2);

        // median curvature as threshold
        std::vector<double> kv;
        kv.reserve(n);
        for (auto v : mesh.vertices())
            kv.push_back(std::abs(get(mm, v)));
        std::nth_element(kv.begin(), kv.begin() + n/2, kv.end());
        double kMed = kv[n/2];

        double sumHighZ = 0.0, sumLowZ = 0.0;
        int nHigh = 0, nLow = 0;
        for (auto v : mesh.vertices()) {
            const Point3& p = mesh.point(v);
            Eigen::Vector3d d(p.x()-mu[0], p.y()-mu[1], p.z()-mu[2]);
            double z = zAx.dot(d);
            if (std::abs(get(mm, v)) >= kMed) { sumHighZ += z; ++nHigh; }
            else                               { sumLowZ  += z; ++nLow;  }
        }
        double meanHighZ = (nHigh > 0) ? sumHighZ / nHigh : 0.0;
        double meanLowZ  = (nLow  > 0) ? sumLowZ  / nLow  : 0.0;

        // If high-curvature (teeth) is on the -Z side, flip Z and Y to keep right-handed.
        if (meanHighZ < meanLowZ) {
            R.col(2) = -R.col(2);
            R.col(1) = -R.col(1);
        }
    }

    // Transform: T(x) = R^T * (x - mu)
    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    T.block<3,3>(0,0) = R.transpose();
    T.block<3,1>(0,3) = -(R.transpose() * mu);

    ICPRegistration::applyTransform(scan, T);
    return T;
}

} // namespace

std::shared_ptr<ScanData> compute(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const Params& params,
    std::function<void(int, int, double)> progressCallback)
{
    if (scans.empty()) return nullptr;

    // --- Step 0: PCA coarse alignment (handles large translational +
    //     moderate rotational offsets between scanner coordinate systems) ---
    for (auto& scan : scans)
        pcaCoarseAlign(*scan);

    // --- Initial reference: deepcopy of the scan with most triangles ---
    auto refIt = std::max_element(scans.begin(), scans.end(),
        [](const auto& a, const auto& b){
            return a->triangleCount < b->triangleCount; });
    auto gpaRef = std::make_shared<ScanData>();
    gpaRef->mesh          = (*refIt)->mesh;
    gpaRef->scannerName   = "GPA_Reference";
    gpaRef->triangleCount = gpaRef->mesh.number_of_faces();

    // --- GPA iterations ---
    // Use multi-pass ICP per scan: coarse (large radius) then fine (tight radius).
    ICPRegistration::Params fineP = params.icpParams;

    ICPRegistration::Params coarseP = fineP;
    coarseP.maxCorrespDist = 25.0; // [mm] – bridges residual offset after PCA
    coarseP.maxIterations  = 40;
    coarseP.convergenceRms = 0.1;

    for (int cycle = 0; cycle < params.maxGPAIterations; ++cycle) {
        double maxDisp = 0.0;

        for (std::size_t si = 0; si < scans.size(); ++si) {
            auto& scan = scans[si];
            if (scan.get() == gpaRef.get()) continue;

            // Coarse pass – needed mainly in cycle 0 for misaligned scans.
            if (cycle == 0) {
                auto r0 = ICPRegistration::align(*scan, *gpaRef, coarseP);
                ICPRegistration::applyTransform(*scan, r0.transform);
            }

            // Fine pass.
            auto r1 = ICPRegistration::align(*scan, *gpaRef, fineP,
                [&](int it, double rms){
                    if (progressCallback) progressCallback(cycle, (int)si, rms);
                });
            ICPRegistration::applyTransform(*scan, r1.transform);
            maxDisp = std::max(maxDisp, r1.finalRms);
        }

        if (maxDisp < params.convergenceThresh) break;
    }

    return gpaRef;
}

} // namespace GPAReference
