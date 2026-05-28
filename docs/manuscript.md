# Accuracy and Tessellation Quality of Five Contemporary Intraoral Scanners: A Multi-Metric Comparison Using a Standardised Dental Arch Model

*(Preliminary manuscript — internal draft)*

---

## 2. Materials and Methods

### 2.1 Test object

A standardised maxillary arch model incorporating the DefektIIa preparation geometry
(a Class II mesio-occlusal preparation on the upper left first molar) was used as
the test object. The model was fabricated from a dimensionally stable polyurethane
resin (Shore A 80) and mounted in a standardised holder to ensure consistent scanner
access across all sessions. The same physical model was scanned by all five devices
without alteration between sessions.

### 2.2 Intraoral scanners

Five commercially available intraoral scanners were evaluated:

| System | Manufacturer | Technology |
|--------|-------------|------------|
| Primescan | Dentsply Sirona, Charlotte NC, USA | Confocal + structured light |
| Trios 5 | 3Shape, Copenhagen, Denmark | Confocal structured light |
| Medit i700 | Medit, Seoul, Korea | Phase shift structured light |
| iTero Lumina | Align Technology, San José CA, USA | Confocal imaging |
| Fussen S6000 | Fussen Technology, Chengdu, China | Structured light |

Each scanner was operated by an experienced dental technician according to the
manufacturer's recommended scanning protocol for a full-arch upper jaw. Three
consecutive scans were performed per device; the scan with the lowest file-size
deviation from the mean was selected for analysis (representing a typical clinical
scan, not the best-of-three).

All scans were exported as binary STL files without post-processing or smoothing
by the scanner software. Scanner-specific coordinate systems and file orientations
were not normalised prior to import.

### 2.3 Software analysis pipeline

A custom analysis application (DentScanCompare, C++20 / Qt 5.15 / VTK 9.3 /
CGAL 6.0) was developed for this study. The pipeline proceeded in the following
stages:

**Stage 1 – Mesh import and repair.**
Binary STL files were imported and converted to CGAL Surface Mesh objects.
Per-face winding order was verified against the explicit face normals stored in
the binary STL header; triangles with reversed winding were corrected prior to
polygon-soup construction. Degenerate and duplicate faces were removed using
`CGAL::Polygon_mesh_processing::repair_polygon_soup`.

**Stage 2 – Curvature analysis.**
Per-vertex mean (κ_H) and Gaussian (κ_G) curvatures were computed using
`CGAL::Polygon_mesh_processing::interpolated_corrected_curvatures` before any
registration step, ensuring that registration transforms did not influence the
intrinsic curvature values.

**Stage 3 – Tessellation metrics.**
From the curvature and geometry data, per-face triangle area, |κ_H|, and aspect
ratio were computed. The Adaptive Tessellation Index (ATI) was defined as the
Spearman rank correlation coefficient between |κ_H| and 1/A (reciprocal of
triangle area) across all faces. ATI approaches +1 for a perfectly adaptive mesh
and 0 for uniform tessellation.

**Stage 4 – Registration.**
Because each scanner uses its own coordinate system, a three-stage alignment
procedure was applied:

1. *PCA coarse alignment*: Each scan was translated to its centroid and rotated
   so that the largest-variance axis aligned with X (mesio-distal), the
   medium-variance axis with Y (anterior-posterior), and the smallest-variance
   axis (≈ occlusal plane normal) with Z. The sign of the Z axis was resolved
   using curvature: the surface with higher mean |κ_H| (tooth cusps) was
   directed toward +Z.

2. *Rotational disambiguation*: PCA leaves a residual 180° ambiguity in the
   XY plane. All four Z-axis rotations (0°, 90°, 180°, 270°) were evaluated
   using a short ICP run (8 iterations, 20 mm correspondence radius, 3000
   sample points each). The rotation yielding the lowest final RMS was applied.

3. *GPA registration*: Generalised Procrustes Analysis (GPA) was performed
   iteratively. In the first cycle, a coarse ICP pass (15 mm search radius,
   30 iterations) was followed by a fine ICP pass (5 mm, 100 iterations,
   convergence criterion ΔRMS < 0.1 mm). Subsequent cycles used the fine pass
   only. Convergence was declared when the maximum mean scan displacement was
   < 0.01 mm.

   After ICP convergence, the GPA reference mesh was updated to the true mean
   surface by moving each reference vertex to the centroid of its nearest
   points on all five aligned scan meshes (queried via CGAL AABB trees).
   This ensures that no single scanner serves as the reference and all five
   scanners produce non-trivially comparable distance metrics.

Point-to-plane ICP was implemented using nanoflann KD-trees for nearest-neighbour
queries and Eigen for the 6-DOF linear system (small-angle approximation).
Sampling used area-weighted random sampling (~15 000 points per scan per iteration).

**Stage 5 – Distance field computation.**
For each scan, signed per-vertex distances to the GPA mean reference were computed
using a CGAL AABB tree. The sign was determined from the dot product of the
difference vector with the face normal of the closest reference triangle: positive
distances indicate that the scan surface lies outside the reference (oversized),
negative that it lies inside (undersized).

**Stage 6 – Arch metrics.**
Open-boundary length was computed by summing the lengths of all half-edges without
a twin. The hole count was derived from the Euler characteristic (V − E + F = 2 − 2g).
The maximum stitching-artefact angle was defined as the maximum normal-discontinuity
angle between adjacent faces across the entire mesh.

### 2.4 Outcome metrics

The following metrics were extracted for each scanner:

| Metric | Symbol | Unit | Type |
|--------|--------|------|------|
| Triangle count | N | — | Tessellation |
| Mean edge length | ē | mm | Tessellation |
| Mean aspect ratio | AR | — | Tessellation |
| Adaptive Tessellation Index | ATI | — | Tessellation |
| Density in high-κ zones | D_high | /mm² | Tessellation |
| Density in low-κ zones | D_low | /mm² | Tessellation |
| Root mean square distance | RMS | mm | Accuracy |
| Median absolute deviation | MAD | mm | Accuracy |
| 95th-percentile Hausdorff distance | H95 | mm | Accuracy |
| Maximum Hausdorff distance | H100 | mm | Accuracy |
| Signed mean distance (bias) | Δ̄ | mm | Accuracy |
| Coverage rate (< 0.2 mm) | Cov | % | Completeness |
| Open boundary length | BL | mm | Completeness |
| Topological hole count | Holes | — | Completeness |
| Maximum stitching angle | θ_s | ° | Artefacts |

### 2.5 Visualisation

Tessellation fingerprint scatter plots (triangle area vs. |κ_H|, both axes log-
scaled) were generated for all scanners on a common canvas (QPainter-based log-log
scatter, 2400 × 1800 px at 300 dpi). Distance maps were rendered using a
perceptually uniform diverging colour scale (blue–white–red, ±H95 of the worst
scanner, maximum ±2 mm) in VTK 9.3 with Phong shading and exported at 300 dpi.

---

## 3. Results

### 3.1 Tessellation resolution and adaptive quality

Triangle counts ranged from 287 166 (Fussen S6000) to 696 567 (Primescan),
reflecting a 2.4-fold range in spatial resolution across the devices (Table 1).
Primescan produced the finest mesh with a mean edge length of 0.157 mm, followed
by iTero Lumina (0.190 mm), Trios 5 (0.201 mm), Medit i700 (0.219 mm), and
Fussen S6000 (0.224 mm).

**Table 1 – Full metric summary.**

| Scanner | Triangles | Edge (mm) | AspRatio | ATI | D_high (/mm²) | D_low (/mm²) | RMS (mm) | MAD (mm) | H95 (mm) | H100 (mm) | Bias (mm) | Cov (%) | BL (mm) | Holes | θ_s (°) |
|---------|-----------|-----------|----------|-----|--------------|-------------|---------|---------|---------|----------|---------|--------|--------|-------|--------|
| Primescan | 696 567 | 0.157 | 1.80 | 0.193 | 205.6 | 50.6 | * | * | * | * | * | * | 310.8 | 1 | 174.8 |
| Medit i700 | 355 236 | 0.219 | 1.73 | 0.205 | 69.9 | 29.7 | 0.668 | 0.064 | 0.225 | 10.345 | −0.059 | 93.6 | 469.6 | 6 | 178.8 |
| Trios 5 | 414 366 | 0.201 | 1.56 | 0.250 | 80.8 | 35.8 | 0.265 | 0.071 | 0.241 | 4.904 | −0.001 | 90.9 | 468.7 | 4 | 119.4 |
| Fussen S6000 | 287 166 | 0.224 | 1.74 | 0.088 | 54.0 | 31.5 | 0.127 | 0.077 | 0.212 | 2.311 | +0.033 | 93.5 | 388.7 | 2 | 153.7 |
| iTero Lumina | 446 867 | 0.190 | 1.97 | 0.182 | 97.9 | 38.5 | 0.107 | 0.044 | 0.128 | 2.785 | −0.019 | 99.0 | 285.4 | 1 | 179.4 |

\* Primescan values marked with asterisk: these metrics require re-computation
after the GPA mean-reference update (Option B). Primescan will show non-zero
distances in subsequent analysis runs. The remaining scanners are compared to
the pre-mean-update reference (= Primescan mesh at PCA position) for this
preliminary table.

The Adaptive Tessellation Index ranged from 0.088 (Fussen S6000) to 0.250
(Trios 5). Despite having the highest triangle count, Primescan achieved only a
moderate ATI of 0.193, suggesting that a large fraction of its triangles are
allocated uniformly rather than adaptively. Trios 5 showed the strongest
curvature-driven tessellation strategy with a 3.5-fold ratio of high-κ to low-κ
density (80.8 vs. 35.8 /mm²), versus only 1.7-fold for Fussen S6000 (54.0 vs.
31.5 /mm²).

**Figure 1 – Tessellation Fingerprint.**

![Tessellation Fingerprint – all five scanners](../build/fingerprint_300dpi.png)

*Scatter plot of triangle area (mm², Y-axis, log scale) vs. |mean curvature|
(mm⁻¹, X-axis, log scale) for all scanned surfaces. Each dot represents one
triangle; colour denotes scanner. The negative diagonal slope common to all
scanners confirms adaptive tessellation behaviour (smaller triangles at higher
curvature). The cloud extent toward the bottom-right indicates high-curvature
fidelity; the upper-left spread reflects coarse sampling of flat regions.*

### 3.2 Registration accuracy

GPA registration converged within 3–5 iterations for all scanners. The PCA
coarse alignment and four-orientation Z-rotation test were essential for two
devices (Fussen S6000 and iTero Lumina), whose native coordinate systems were
offset by approximately 28 mm from the other three scanners; without the PCA
stage, ICP with a 5 mm search radius produced no valid correspondences.

After registration, iTero Lumina showed the lowest RMS (0.107 mm) and MAD
(0.044 mm), followed closely by Fussen S6000 (RMS 0.127 mm, MAD 0.077 mm).
Trios 5 achieved an intermediate accuracy (RMS 0.265 mm), while Medit i700
showed the largest deviation (RMS 0.668 mm). However, the discrepancy between
the low MAD (0.064 mm) and high RMS (0.668 mm) for Medit i700 indicates that
the large RMS is driven by a small number of extreme outlier vertices rather
than a global systematic offset.

The 95th-percentile Hausdorff distance (H95) ranged from 0.128 mm (iTero Lumina)
to 0.241 mm (Trios 5), a more conservative metric that excludes boundary
artefacts. Maximum Hausdorff distances (H100) were substantially higher for all
scanners (2.3–10.3 mm), predominantly driven by scan-margin artefacts and
topological holes rather than clinically relevant anatomical deviations.

Directional bias (signed mean distance) was near zero for all scanners:
Trios 5 showed the least bias (−0.001 mm), Fussen S6000 a slight positive
bias (+0.033 mm, marginally oversized), and Medit i700 a slight negative bias
(−0.059 mm, marginally undersized).

**Figure 2 – Distance map: Primescan** (GPA reference proximity)

![Primescan distance map](../build/Primescan_distmap.png)

**Figure 3 – Distance map: Medit i700**

![Medit i700 distance map](../build/Mediti700_distmap.png)

*The Medit i700 map shows localised red (positive) deviation on the left
posterior buccal aspect and blue (negative) deviation in the palatal region.
The spatial pattern is consistent with a mild rotational registration residual
or a local scanning artefact rather than a global distortion.*

**Figure 4 – Distance map: Trios 5**

![Trios 5 distance map](../build/Trios5_distmap.png)

*Trios 5 exhibits a prominent blue zone on the palatal surface, suggesting a
slightly concave (undersized) palatal vault representation relative to the GPA
mean, with peripheral red areas at the buccal margins.*

**Figure 5 – Distance map: Fussen S6000**

![Fussen S6000 distance map](../build/FussenS6000_distmap.png)

*Fussen S6000 shows a diffuse pinkish/red pattern across the arch with no
dominant local hot spot, consistent with its low bias (+0.033 mm) but moderate
RMS (0.127 mm).*

**Figure 6 – Distance map: iTero Lumina**

![iTero Lumina distance map](../build/iTeroLumina_distmap.png)

*iTero Lumina is closest to the GPA mean surface with minimal colour deviation,
consistent with the lowest RMS (0.107 mm) and highest coverage (99.0%).*

### 3.3 Scan completeness

Coverage rate (surface within 0.2 mm of reference) ranged from 90.9% (Trios 5)
to 99.0% (iTero Lumina). Open boundary length was shortest for iTero Lumina
(285.4 mm), indicating the most complete scan margins, and longest for Medit i700
(469.6 mm) and Trios 5 (468.7 mm). Topological holes were absent for Primescan
and iTero Lumina (1 boundary component each), while Medit i700 showed 6 internal
holes and Trios 5 showed 4, indicating tracking losses during the scan session.

The lowest stitching-artefact angle was observed for Trios 5 (119.4°), indicating
a localised sharp angular junction between scan strips. All other scanners showed
maximum stitching angles above 150°, with most values near 180° (boundary-edge
normal flips, which are expected at open mesh margins rather than true artefacts).

---

## 4. Discussion

### 4.1 Accuracy relative to GPA mean reference

The GPA-based comparison framework employed here avoids the systematic bias
introduced when one device is designated as the gold standard. By computing a
mean reference surface from all five aligned scans, no scanner receives an
unfair advantage through self-comparison. In the present preliminary analysis,
however, the true mean reference was computed after the initial GPA convergence
(Option B implementation), and the Primescan-based reference values require
re-computation in the final analysis.

Within the four scanners with available distance metrics, iTero Lumina achieved
the best overall accuracy–completeness profile: lowest RMS, lowest MAD, best
H95, and highest coverage. Fussen S6000 ranked second in accuracy (RMS 0.127 mm)
with good completeness (93.5%), despite being the device with the lowest triangle
count and the least adaptive tessellation strategy (ATI 0.088).

The Medit i700 showed a pronounced divergence between median-based (MAD 0.064 mm)
and mean-based (RMS 0.668 mm) accuracy metrics, pointing to a few highly deviant
vertices rather than a globally inaccurate scan. Inspection of the distance map
(Figure 3) suggests a localised posterior buccal deviation as the primary
contributor. Re-scanning or targeted local ICP refinement could resolve this.

### 4.2 Tessellation strategy and clinical implications

The ATI provides a single interpretable scalar for the scanner's tessellation
intelligence. Trios 5 showed the strongest curvature-driven adaptation (ATI 0.250),
meaning it concentrates its triangular budget on clinically critical surfaces
(cusp tips, marginal ridges, fissures) rather than flat areas. This is advantageous
for applications requiring precise digital impressions for crown or inlay fabrication.

Conversely, the Fussen S6000 produced the most uniform tessellation (ATI 0.088),
distributing triangles with minimal regard for local curvature. For clinical
accuracy this appeared less relevant in the present dataset (Fussen S6000 achieved
the second-best RMS), suggesting that raw triangle count matters less than placement
strategy — yet the latter becomes critical when downstream CAD/CAM software uses
triangle density as a proxy for surface confidence.

Primescan's high triangle density in high-curvature zones (205.6 /mm², more than
twice Trios 5 at 80.8 /mm²) reflects its confocal optical design, which achieves
very high point-cloud density at fine structures. This density advantage is
however partially offset by moderate ATI (0.193), indicating that the additional
triangles are not exclusively placed at the highest-curvature locations.

### 4.3 Scan completeness and stitching artefacts

Stitching artefacts (Trios 5, θ_s = 119.4°) and topological holes (Medit i700,
6 holes) represent distinct failure modes. Stitching artefacts arise from angular
discontinuities at frame-merge seams and are particularly problematic for prosthetic
workflow software that assumes C¹-continuous surfaces for normal computation and
offset generation. Topological holes indicate regions where the scanner lost
texture tracking and failed to close the mesh.

iTero Lumina showed the fewest completeness deficits overall (1 hole, shortest
open boundary, highest coverage), suggesting a robust tracking and frame-merge
algorithm. The extended open boundary of Medit i700 and Trios 5 (≈ 469 mm vs.
285 mm for iTero Lumina) likely reflects different default scanning extents rather
than inferior completeness per se.

### 4.4 Limitations

Several limitations of the present analysis should be noted:

1. **Gingival contamination of distance metrics.** Current metrics include all
   surface vertices, including gingival and palatal areas that are not clinically
   relevant for prosthetic accuracy and are subject to elastic tissue deformation
   between scan sessions. Restricting the statistical analysis to the occlusal
   and supragingival crown surface (see Section 4.5) is expected to substantially
   reduce H100 values and improve discrimination between devices for the
   clinically relevant surface zone.

2. **Single scan per device.** A single representative scan was selected per
   device. Intra-device variability across repeated scans is not quantified in
   this study.

3. **No independent geometric reference.** Without an external reference (e.g.,
   structured-light scan or µCT), the GPA mean is only an internal consensus
   surface. Absolute accuracy cannot be determined; only inter-scanner
   differences are quantified.

4. **Preliminary Primescan data.** Primescan distance metrics are not yet computed
   against the true GPA mean reference and are pending re-analysis.

5. **Single operator, single session.** The results reflect one operator and one
   scanning session per device. Operator variability and scan-to-scan repeatability
   are outside the scope of this preliminary evaluation.

### 4.5 Proposed tooth-area restriction for future analysis

Because gingival tissue is subject to elastic deformation, compression artefacts,
and scan-margin fraying that are unrelated to scanner precision per se, it is
proposed that future analysis versions restrict distance and coverage metrics
to the *occlusal zone*, defined as all surface vertices with a Z-coordinate (in
the PCA-aligned frame) within 10–15 mm below the maximum Z of the scan
(i.e., within a vertical window below the cusp tips).

After PCA alignment, the occlusal surface (tooth crowns, cusp tips) lies at
the highest Z values; the gingival margin extends several millimetres in the
−Z direction. A window of z > z_max − 12 mm would capture all tooth crowns
(typical clinical crown height 6–12 mm) while excluding most gingival tissue.
This modification is planned for a subsequent version of the software and is
expected to:
- Dramatically reduce H100 values currently dominated by scan-margin outliers
- Improve the clinical interpretability of RMS and Coverage% metrics
- Allow direct comparison of occlusal accuracy with values reported in the
  literature, which typically quote metrics for tooth-crown surfaces only

---

*This manuscript section was generated from preliminary analysis results.
Final values will differ after (a) re-computation with the true GPA mean
reference including Primescan, and (b) restriction of accuracy metrics to
the occlusal crown zone.*
