# Accuracy and Tessellation Quality of Five Contemporary Intraoral Scanners: A Multi-Metric Comparison Using a Standardised Dental Arch Model

*(Preliminary manuscript — internal draft)*

---

## 2. Materials and Methods

### 2.1 Test object

A standardised maxillary arch model incorporating the DefektIIa preparation geometry was used. The same physical model was scanned by all five devices
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
manufacturer's recommended scanning protocol for a full-arch upper jaw. blabla...

All scans were exported as binary STL files without post-processing or smoothing
by the scanner software. Scanner-specific coordinate systems and file orientations were not normalised prior to import.

### 2.3 Software analysis pipeline

A custom analysis application (DentScanCompare/Prof. K. H. Kunzelmann, C++20 / Qt 5.15 / VTK 9.3 / CGAL 6.0) was developed for this study. The pipeline proceeded in the following
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

**Stage 5 – Tooth-crown segmentation.**
To restrict accuracy metrics to the clinically relevant crown surface and exclude
gingival tissue, scan margins, and palatal areas — which are subject to elastic
deformation and boundary fraying unrelated to scanner precision — a Dijkstra-based
region-growing segmentation was applied to each aligned scan.

One seed point was manually placed on the occlusal or incisal surface of each
tooth crown visible in the scan. Starting from these seeds, the algorithm expanded
face-by-face using centroid-to-centroid geodesic distance as the edge cost in a
multi-source Dijkstra traversal. Three stopping criteria were applied in combination:

- *Geodesic distance*: expansion halted when the accumulated surface path from
  the nearest seed exceeded 12 mm. This threshold was chosen to encompass every
  tooth type (molars 8–12 mm, premolars 7–10 mm, canines 9–12 mm, incisors
  10–13 mm) while excluding the gingival zone. The criterion is orientation-
  independent, unlike normal-tilt filters that fail on lingual and palatal surfaces.

- *CEJ crease guard*: expansion across an inter-face dihedral angle exceeding 50°
  was blocked, exploiting the characteristic kink at the cemento-enamel junction
  where the convex crown surface meets the more concave gingival collar.

- *Curvature floor*: faces with mean κ_H below −4 mm⁻¹ were excluded, preventing
  expansion into the concave gingival sulcus.

The segmentation was computed independently for each scanner's mesh using the same
world-space seed coordinates, as vertex counts differ between devices.

**Stage 6 – Crown-restricted ICP refinement.**
Following segmentation, a second point-to-plane ICP pass was performed for each
scan using only the tooth-crown vertices as correspondence candidates (3 mm search
radius, warm start from the GPA alignment). This crown-restricted refinement
reduces residual registration error introduced by gingival tissue noise in the
initial GPA pass. After convergence, the GPA mean surface was updated
(`GPAReference::updateMeanMesh`) and distance fields were recomputed.

**Stage 7 – Distance field computation.**
For each scan, signed per-vertex distances to the GPA mean reference were computed
using a CGAL AABB tree. The sign was determined from the dot product of the
difference vector with the face normal of the closest reference triangle: positive
distances indicate that the scan surface lies outside the reference (oversized),
negative that it lies inside (undersized).

Distance statistics were computed exclusively over the tooth-crown mask established
in Stage 5, excluding gingival vertices. Reference vertices are denoted n_crown.

**Stage 8 – Arch and completeness metrics.**
Open-boundary length was computed by summing the lengths of all half-edges without
a twin. The hole count was derived from the Euler characteristic (V − E + F = 2 − 2g).
The maximum stitching-artefact angle was defined as the maximum normal-discontinuity
angle between adjacent faces across the entire mesh.

### 2.4 Outcome metrics

Tessellation metrics were computed from the raw unregistered mesh (Stages 2–3).
Accuracy and completeness metrics were computed from the crown-restricted aligned
mesh (Stages 4–7). Table 1 additionally provides full-mesh accuracy values
(without ROI restriction) for comparison.

| Metric | Symbol | Unit | Type | ROI |
|--------|--------|------|------|-----|
| Triangle count | N | — | Tessellation | Full mesh |
| Mean edge length | ē | mm | Tessellation | Full mesh |
| Mean aspect ratio | AR | — | Tessellation | Full mesh |
| Adaptive Tessellation Index | ATI | — | Tessellation | Full mesh |
| Density in high-κ zones | D_high | /mm² | Tessellation | Full mesh |
| Density in low-κ zones | D_low | /mm² | Tessellation | Full mesh |
| Root mean square distance | RMS | mm | Accuracy | Crown only |
| Median absolute deviation | MAD | mm | Accuracy | Crown only |
| 95th-percentile Hausdorff distance | H95 | mm | Accuracy | Crown only |
| Maximum Hausdorff distance | H100 | mm | Accuracy | Crown only |
| Signed mean distance (bias) | Δ̄ | mm | Accuracy | Crown only |
| Coverage rate (< 0.2 mm) | Cov | % | Completeness | Crown only |
| Open boundary length | BL | mm | Completeness | Full mesh |
| Topological hole count | Holes | — | Completeness | Full mesh |
| Maximum stitching angle | θ_s | ° | Artefacts | Full mesh |

### 2.5 Visualisation

Tessellation fingerprint scatter plots (triangle area vs. |κ_H|, both axes log-
scaled) were generated for all scanners on a common canvas (QPainter-based log-log
scatter, 2400 × 1800 px at 300 dpi). Individual scanners can be isolated by
clicking the legend or the scanner name, which highlights that series at full
opacity while dimming the others.

Distance maps were rendered using a perceptually uniform diverging colour scale
(blue–white–red) in VTK 9.3 with Phong shading and exported at 300 dpi. When the
tooth-crown segmentation is active, gingival vertices are rendered in uniform dark
grey, restricting the colour scale to the clinically relevant crown surface. The
colour-scale range is set to ±H95 of the worst scanner (capped at ±2 mm) and can
be adjusted interactively for visual inspection; the statistical metrics remain
unaffected by this visual setting.

---

## 3. Results

### 3.1 Tessellation resolution and adaptive quality

Triangle counts ranged from 287 166 (Fussen S6000) to 696 567 (Primescan),
reflecting a 2.4-fold range in spatial resolution across the devices (Table 1).
Primescan produced the finest mesh with a mean edge length of 0.157 mm, followed
by iTero Lumina (0.190 mm), Trios 5 (0.201 mm), Medit i700 (0.219 mm), and
Fussen S6000 (0.224 mm).

**Table 1 – Full metric summary (accuracy metrics: tooth-crown surface only).**

| Scanner | Triangles | Edge (mm) | AspRatio | ATI | D_high (/mm²) | D_low (/mm²) | RMS (mm) | MAD (mm) | H95 (mm) | H100 (mm) | Bias (mm) | Cov (%) | BL (mm) | Holes | θ_s (°) |
|---------|-----------|-----------|----------|-----|--------------|-------------|---------|---------|---------|----------|---------|--------|--------|-------|--------|
| Primescan   | 696 567 | 0.157 | 1.80 | 0.193 | 205.6 | 50.6 | 0.040 | 0.026 | 0.078 | 0.168 | −0.003 | 100.0 | 310.8 | 1 | 174.8 |
| Medit i700  | 355 236 | 0.219 | 1.73 | 0.205 | 69.9  | 29.7 | 0.034 | 0.023 | 0.064 | 0.145 | −0.010 | 100.0 | 469.6 | 6 | 178.8 |
| Trios 5     | 414 366 | 0.201 | 1.56 | 0.250 | 80.8  | 35.8 | 0.035 | 0.023 | 0.070 | 0.158 | −0.001 | 100.0 | 468.7 | 4 | 119.4 |
| Fussen S6000| 287 166 | 0.224 | 1.74 | 0.088 | 54.0  | 31.5 | 0.094 | 0.070 | 0.182 | 0.288 | +0.035 | 97.3  | 388.7 | 2 | 153.7 |
| iTero Lumina| 446 867 | 0.190 | 1.97 | 0.182 | 97.9  | 38.5 | 0.043 | 0.029 | 0.087 | 0.208 | −0.018 | 100.0 | 285.4 | 1 | 179.4 |

**Table 2 – Full-mesh accuracy (no ROI restriction), for comparison with Table 1.**

| Scanner | RMS (mm) | MAD (mm) | H95 (mm) | H100 (mm) | Bias (mm) | Cov (%) |
|---------|---------|---------|---------|----------|---------|--------|
| Primescan    | 0.560 | 0.038 | 0.439 |  6.114 | −0.019 | 93.1 |
| Medit i700   | 0.827 | 0.049 | 0.651 | 11.018 | −0.076 | 92.7 |
| Trios 5      | 0.371 | 0.040 | 0.227 |  6.377 | −0.001 | 94.7 |
| Fussen S6000 | 0.127 | 0.068 | 0.186 |  2.687 | +0.034 | 96.3 |
| iTero Lumina | 0.172 | 0.028 | 0.108 |  3.906 | −0.019 | 97.7 |

The Adaptive Tessellation Index ranged from 0.088 (Fussen S6000) to 0.250
(Trios 5). Despite having the highest triangle count, Primescan achieved only a
moderate ATI of 0.193, suggesting that a large fraction of its triangles are
allocated uniformly rather than adaptively. Trios 5 showed the strongest
curvature-driven tessellation strategy with a 2.3-fold ratio of high-κ to low-κ
density (80.8 vs. 35.8 /mm²), versus only 1.7-fold for Fussen S6000
(54.0 vs. 31.5 /mm²).

**Figure 1 – Tessellation Fingerprint.**

![Tessellation Fingerprint – all five scanners](../build/fingerprint_300dpi.png)

*Scatter plot of triangle area (mm², Y-axis, log scale) vs. |mean curvature|
(mm⁻¹, X-axis, log scale) for all scanned surfaces. Each dot represents one
triangle; colour denotes scanner. The negative diagonal slope common to all
scanners confirms adaptive tessellation behaviour (smaller triangles at higher
curvature). The cloud extent toward the bottom-right indicates high-curvature
fidelity; the upper-left spread reflects coarse sampling of flat regions.*

### 3.2 Registration accuracy (crown surface)

GPA registration converged within 3–5 iterations for all scanners. The PCA
coarse alignment and four-orientation Z-rotation test were essential for two
devices (Fussen S6000 and iTero Lumina), whose native coordinate systems were
offset by approximately 28 mm from the other three scanners; without the PCA
stage, ICP with a 5 mm search radius produced no valid correspondences.

Crown-restricted ICP refinement (Stage 6) reduced RMS by 0.002–0.021 mm
compared to the initial GPA alignment with tooth-crown metrics (Table 3); the
largest improvement was observed for Medit i700 (−0.021 mm, −38%) and Trios 5
(−0.015 mm, −30%), indicating that gingival tissue in those scans had introduced
a residual registration offset that the crown-restricted pass corrected.

**Table 3 – Effect of crown-restricted ICP refinement on RMS distance.**

| Scanner | RMS after GPA + crown metrics (mm) | RMS after crown ICP refinement (mm) | Improvement (mm) |
|---------|-------------------------------------|--------------------------------------|-----------------|
| Primescan    | 0.050 | 0.040 | −0.010 |
| Medit i700   | 0.055 | 0.034 | −0.021 |
| Trios 5      | 0.050 | 0.035 | −0.015 |
| Fussen S6000 | 0.099 | 0.094 | −0.005 |
| iTero Lumina | 0.045 | 0.043 | −0.002 |

After crown-restricted ICP, the final accuracy ranking (crown RMS) was: Medit i700
(0.034 mm) ≈ Trios 5 (0.035 mm) < Primescan (0.040 mm) < iTero Lumina (0.043 mm)
< Fussen S6000 (0.094 mm). The four leading scanners were closely clustered
within a 0.009 mm range; Fussen S6000 showed approximately 2.3-fold higher crown
RMS than the best performer.

MAD values were consistent with RMS rankings: Medit i700 and Trios 5 tied at
0.023 mm, Primescan 0.026 mm, iTero Lumina 0.029 mm, and Fussen S6000 0.070 mm.
The near-equality of RMS and MAD × √2 for all scanners indicates that crown
distance distributions are approximately Gaussian with few outliers — in contrast
to the full-mesh distributions (Table 2), where the RMS/MAD divergence indicated
heavy-tailed distributions driven by gingival outliers.

The 95th-percentile Hausdorff distance (H95) on the crown surface ranged from
0.064 mm (Medit i700) to 0.182 mm (Fussen S6000). All H100 values were below
0.3 mm (Fussen S6000: 0.288 mm), compared to 2.3–11.0 mm for the full-mesh
analysis — confirming that the extreme H100 values in the unfiltered analysis
were artefacts of scan margins and gingival fraying rather than crown-surface
error.

Directional bias (signed mean distance) was near zero for all scanners: Trios 5
(−0.001 mm) and Primescan (−0.003 mm) showed negligible systematic offset. Fussen
S6000 exhibited a slight positive crown bias (+0.035 mm, marginally oversized arch).

**Figure 2 – Distance map: Primescan**

![Primescan distance map](../build/Primescan_distmap.png)

*Primescan crown surface showing minimal deviation from the GPA mean
(RMS 0.040 mm). The muted colour pattern confirms near-reference accuracy across
all tooth crowns. Gingival vertices are shown in dark grey.*

**Figure 3 – Distance map: Medit i700**

![Medit i700 distance map](../build/Mediti700_distmap.png)

*Medit i700 crown surface (RMS 0.034 mm, best overall). The restricted crown view
reveals a spatially uniform, near-white colour distribution confirming tight
agreement with the reference mean. The localised posterior buccal deviation visible
in the full-mesh analysis (Table 2, RMS 0.827 mm) was attributable to gingival and
margin tissue outside the crown zone.*

**Figure 4 – Distance map: Trios 5**

![Trios 5 distance map](../build/Trios5_distmap.png)

*Trios 5 crown surface (RMS 0.035 mm). A mild blue zone on the palatal cusps
persists after crown-restricted registration, suggesting a slight systematic
palatal undercut specific to this scanner's capture geometry.*

**Figure 5 – Distance map: Fussen S6000**

![Fussen S6000 distance map](../build/FussenS6000_distmap.png)

*Fussen S6000 crown surface (RMS 0.094 mm, highest among the five). A diffuse
positive (red) bias is distributed across the occlusal surface, consistent with
the slight systematic oversize observed in the bias metric (+0.035 mm) and reduced
coverage (97.3%).*

**Figure 6 – Distance map: iTero Lumina**

![iTero Lumina distance map](../build/iTeroLumina_distmap.png)

*iTero Lumina crown surface (RMS 0.043 mm). Near-white crown coloration and
100% coverage confirm high accuracy across all tooth surfaces.*

### 3.3 Scan completeness

Crown-surface coverage rate (proportion of GPA reference crown vertices within
0.2 mm of the scan) was 100% for four of five scanners; only Fussen S6000 fell
below this threshold (97.3%), indicating that approximately 2.7% of the reference
crown surface was not captured within the 0.2 mm tolerance. Full-mesh coverage
(Table 2) was lower for all scanners (92.7–97.7%), reflecting gingival and margin
incompleteness rather than crown-surface gaps.

Open boundary length was shortest for iTero Lumina (285.4 mm), indicating the most
complete scan margins, and longest for Medit i700 (469.6 mm) and Trios 5 (468.7 mm).
These large boundary lengths likely reflect different default scanning extents —
both devices captured a wider gingival border — rather than inferior completeness
per se. Topological holes were absent for Primescan and iTero Lumina (1 boundary
component each), while Medit i700 showed 6 internal holes and Trios 5 showed 4,
indicating tracking losses during the scan session.

The lowest stitching-artefact angle was observed for Trios 5 (119.4°), indicating
a localised sharp angular junction between scan strips. All other scanners showed
maximum stitching angles above 150°, with values near 180° for Medit i700 and
iTero Lumina (boundary-edge normal flips expected at open margins rather than true
stitching artefacts).

---

## 4. Discussion

### 4.1 Impact of region-of-interest restriction on accuracy rankings

The most striking finding of this analysis is the near-complete reversal of accuracy
rankings between the full-mesh and crown-restricted analyses (Tables 1 vs. 2). In
the full-mesh analysis Fussen S6000 ranked first (RMS 0.127 mm) and Medit i700
ranked last (RMS 0.827 mm) — a 6.5-fold range. After restricting the analysis to
the tooth-crown surface, Medit i700 ranked first (0.034 mm) and Fussen S6000 ranked
last (0.094 mm) — a 2.8-fold range. The absolute values for the crown surface are
also substantially lower across all devices.

This inversion arises because Fussen S6000 captured the smallest gingival area
(lowest boundary length: 388.7 mm) and fewest holes, so its full-mesh statistics
were dominated by clinically relevant crown vertices. Conversely, Medit i700 captured
a wide gingival margin (469.6 mm boundary, 6 holes) with considerable scan-margin
fraying, driving up its full-mesh RMS without reflecting true crown accuracy.

These results emphasise that using unfiltered full-mesh metrics to compare intraoral
scanners is methodologically problematic: scanners with more conservative scanning
extents or fewer margin artefacts will appear systematically more accurate regardless
of their true crown fidelity. Crown-restricted metrics are clinically more relevant
for prosthetic applications where only the supragingival tooth surface matters.

### 4.2 Crown-restricted ICP refinement

The two-stage registration (initial GPA followed by crown-restricted ICP) improved
crown RMS by 0.002–0.021 mm. The improvement was largest for devices that initially
included more gingival tissue in their scan (Medit i700: −38%, Trios 5: −30%)
and negligible for devices with smaller gingival capture areas (Fussen S6000: −5%,
iTero Lumina: −4%). This validates the assumption that gingival tissue introduces
a registration gradient when used as part of the ICP objective: the crown-restricted
refinement effectively decouples crown accuracy from margin-capture extent.

### 4.3 Tessellation strategy and clinical implications

The ATI provides a single interpretable scalar for the scanner's tessellation
intelligence. Trios 5 showed the strongest curvature-driven adaptation (ATI 0.250),
meaning it concentrates its triangular budget on clinically critical surfaces
(cusp tips, marginal ridges, fissures) rather than flat areas. This is advantageous
for applications requiring precise digital impressions for crown or inlay fabrication.

Conversely, the Fussen S6000 produced the most uniform tessellation (ATI 0.088),
distributing triangles with minimal regard for local curvature. Despite this, its
crown accuracy was the lowest in the study. This decoupling between tessellation
strategy and crown accuracy suggests that the Fussen S6000's accuracy deficit
originates in the optical measurement stage rather than in the mesh reconstruction
step, since adaptive tessellation would not compensate for point-cloud noise.

Primescan's high triangle density in high-curvature zones (205.6 /mm², more than
twice Trios 5 at 80.8 /mm²) reflects its confocal optical design. This density
advantage is however partially offset by moderate ATI (0.193), indicating that the
additional triangles are not exclusively placed at the highest-curvature locations.

### 4.4 Scan completeness and stitching artefacts

Stitching artefacts (Trios 5, θ_s = 119.4°) and topological holes (Medit i700,
6 holes) represent distinct failure modes. Stitching artefacts arise from angular
discontinuities at frame-merge seams and are particularly problematic for prosthetic
workflow software that assumes C¹-continuous surfaces for normal computation and
offset generation. Topological holes indicate regions where the scanner lost
texture tracking and failed to close the mesh.

iTero Lumina showed the fewest completeness deficits overall (1 boundary component,
shortest open boundary, 100% crown coverage), suggesting a robust tracking and
frame-merge algorithm. The extended open boundary of Medit i700 and Trios 5
(≈ 469 mm vs. 285 mm for iTero Lumina) likely reflects different default scanning
extents rather than inferior completeness per se.

### 4.5 Limitations

Several limitations of the present analysis should be noted:

1. **Single scan per device.** A single representative scan was selected per
   device. Intra-device variability across repeated scans is not quantified in
   this study.

2. **No independent geometric reference.** Without an external reference (e.g.,
   structured-light scan or µCT), the GPA mean is only an internal consensus
   surface. Absolute accuracy cannot be determined; only inter-scanner
   differences are quantified.

3. **Manual seed placement for crown segmentation.** One operator placed tooth
   seeds interactively. Although the Dijkstra algorithm makes the result robust
   to seed position within the crown surface, operator-to-operator variability
   in seed placement has not been assessed.

4. **Single operator, single session.** The results reflect one operator and one
   scanning session per device. Operator variability and scan-to-scan repeatability
   are outside the scope of this preliminary evaluation.

---

*This manuscript section was generated from analysis results exported by
DentScanCompare. Final crown-restricted accuracy values (Table 1) reflect the
full analysis pipeline: GPA registration with true mean-reference update,
Dijkstra tooth-crown segmentation, crown-restricted ICP refinement, and
CGAL AABB-tree distance computation restricted to segmented crown vertices.*
