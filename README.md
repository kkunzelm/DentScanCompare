# DentScanCompare

A desktop application for the systematic quality comparison of dental intraoral scanner
outputs.  Multiple STL scans of the same physical object are loaded, automatically
co-registered via Generalized Procrustes Analysis (GPA), and evaluated across a
comprehensive set of accuracy, tessellation, and completeness metrics.  All results are
displayed in interactive 3-D colour maps and a sortable metrics table, and can be
exported as PNG images and a CSV file.

---

## Table of Contents

1. [Purpose](#purpose)
2. [Dependencies](#dependencies)
3. [Building](#building)
4. [Workflow and Usage](#workflow-and-usage)
   - [Step 1 – Load STL files](#step-1--load-stl-files)
   - [Step 2 – Inspect the Tessellation Fingerprint](#step-2--inspect-the-tessellation-fingerprint)
   - [Step 3 – Configure registration](#step-3--configure-registration)
   - [Step 4 – Define the region of interest](#step-4--define-the-region-of-interest)
   - [Step 5 – Run the analysis](#step-5--run-the-analysis)
   - [Step 6 – Read the Distance Maps](#step-6--read-the-distance-maps)
   - [Step 7 – Read the Metrics table](#step-7--read-the-metrics-table)
   - [Step 8 – Export results](#step-8--export-results)
5. [Metric Reference](#metric-reference)
   - [Tessellation quality metrics](#tessellation-quality-metrics)
   - [Accuracy metrics](#accuracy-metrics)
   - [Completeness metrics](#completeness-metrics)
6. [Tessellation Fingerprint – Interpretation Guide](#tessellation-fingerprint--interpretation-guide)
7. [Distance Map – Interpretation Guide](#distance-map--interpretation-guide)
8. [STL file naming convention](#stl-file-naming-convention)
9. [Important considerations](#important-considerations)

---

## Purpose

Dental intraoral scanners are evaluated under identical conditions: each scanner captures
the same physical test model (e.g., DefektIIa) and produces a binary STL file.  Because
no external reference geometry is available (no CT or CMM ground truth), the application
constructs a neutral reference surface internally using Generalized Procrustes Analysis —
the mean of all aligned scans.  This makes the comparison self-contained and scanner-
independent.

The software answers three clinical questions:

- **How accurate is each scanner?** — per-vertex signed distances to the GPA mean reference,
  summarised as RMS, MAD, Hausdorff percentiles, and systematic bias.
- **How intelligently does each scanner allocate triangles?** — the Adaptive Tessellation
  Index (ATI) measures whether the scanner concentrates fine triangles where the surface
  curves most.
- **How complete is each scan?** — coverage percentage, open boundary length, hole count,
  and stitching artefact angles quantify gaps and registration failures.

---

## Dependencies

All dependencies must be present **before** running CMake.  On Debian/Ubuntu:

| Library | Version | Install command |
|---------|---------|-----------------|
| Qt      | **5.15** (not Qt6) | `apt install qtbase5-dev qtbase5-private-dev libqt5opengl5-dev` |
| VTK     | 9.3     | `apt install libvtk9-dev libvtk9-qt5-dev` |
| CGAL    | 6.0+    | `apt install libcgal-dev` |
| Eigen3  | 3.4+    | `apt install libeigen3-dev` |
| nanoflann | 1.7+  | `apt install libnanoflann-dev` |
| CMake   | 3.20+   | `apt install cmake` |
| GCC/Clang | C++20  | `apt install build-essential` |

**Why Qt5 and not Qt6?**  The system VTK 9.3 package on Debian 13 is compiled against Qt5.
Mixing Qt5 VTK with Qt6 application code causes an `INTERFACE_QT_MAJOR_VERSION` conflict
at link time.  Install `libvtk9-qt5-dev` and use `find_package(Qt5 ...)` — do not switch to
Qt6 unless you rebuild VTK from source against Qt6.

**Qt6 Charts** is not used.  All scatter plots are rendered with a custom QPainter widget
because `vtkChartXY` silently drops data points in the Qt5/VTK9.3 rendering context.

---

## Building

```bash
# Clone or unpack the source
cd DentScanCompare

# Configure (out-of-tree build recommended)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Compile (use -j to parallelise)
cmake --build build -j$(nproc)

# Run
./build/src/DentScanCompare
```

A `compile_commands.json` is written to the build directory automatically
(`CMAKE_EXPORT_COMPILE_COMMANDS ON`) and a symlink at the project root allows
clangd/LSP tools to find it.

**CMake quirk — MPI dummy target.**  VTK's CMake targets file unconditionally references
`MPI::MPI_C` even when MPI is not used at runtime.  The root `CMakeLists.txt` handles this:

```cmake
project(DentScanCompare VERSION 1.0 LANGUAGES CXX C)   # C required for MPI detection
find_package(MPI QUIET)
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)  # dummy when MPI absent
endif()
find_package(VTK 9.3 REQUIRED ...)
```

Do not remove the `C` language from the `project()` line or change the order of these
three blocks — the configure step will fail.

---

## Workflow and Usage

### Step 1 – Load STL files

**File → Open STL files** (or the toolbar button).  Select one or more binary STL files
in a single dialog.  There is no upper limit on the number of files, but the GPA and
AABB distance computations scale with triangle count; five scans of ~500 k triangles each
complete in roughly 60–90 seconds on a modern desktop.

The scanner name is extracted from the filename using the convention described in
[STL file naming convention](#stl-file-naming-convention).  Once loaded, all meshes
appear in **Tab 1 – Overview** with Phong shading.  The scanner list on the left shows
triangle counts.

The number of 3-D viewports in **Tab 1 – Overview** and **Tab 4 – Distance Maps**
automatically matches the number of loaded files: fewer files → fewer viewports (no empty
placeholder panels); more than five files → viewports scroll horizontally, each keeping a
minimum width of 240 px for readability.

**What the loader does automatically:**
- Reads the binary STL header and per-face stored normals.
- Corrects inconsistent winding order per face: if the cross-product of the two edge
  vectors is anti-aligned with the stored STL normal, the two non-pivot vertices are
  swapped.  This is necessary because some scanners (Primescan) export faces wound in
  the opposite direction from the others.
- Runs `repair_polygon_soup` → `orient_polygon_soup` → `polygon_soup_to_polygon_mesh`
  to produce a clean CGAL `Surface_mesh`.
- Computes per-vertex mean and Gaussian curvature via CGAL
  `interpolated_corrected_curvatures`.

---

### Step 2 – Inspect the Tessellation Fingerprint

Navigate to **Tab 2 – Fingerprint** before running the analysis.  Tessellation metrics
are computed from the raw loaded mesh and do not depend on registration; this tab is
therefore available immediately after loading.

The scatter plot shows one point per triangle for every loaded scan, colour-coded by
scanner.  With all five scanners visible simultaneously the plot can look crowded.
**To inspect a single scanner:** click its name in the **Loaded Scans** list on the left,
or click its row in the plot legend.  The selected scanner's dots are drawn at full opacity
and on top; all other scanners are dimmed to near-invisible.  Click the same entry again
to deselect and return to the full view.  Detailed interpretation is in
[Tessellation Fingerprint – Interpretation Guide](#tessellation-fingerprint--interpretation-guide).

---

### Step 3 – Choose a reference surface and configure registration

#### Reference surface (sidebar — visible on all tabs)

The **"Reference Surface"** group box in the left sidebar lets you choose how distances
are measured:

| Radio button | Behaviour |
|---|---|
| **GPA mean (all scans)** | Iterative Generalized Procrustes Analysis: all scans are aligned to the evolving mean surface.  No individual scanner is privileged.  Recommended for unbiased comparison. |
| **Fixed reference scan:** *(default)* | One scan is held fixed; all others are ICP-aligned to it.  The fixed scan's distance metrics will be zero.  Use this when one scan is a trusted ground truth. |

**"Fixed reference scan:"** is the default selection at startup.  To assign the reference,
click the desired scan in the **Loaded Scans** list — it gets a **★** marker and bold text
and the label below the radio button confirms the name.  Selecting the radio button first
and then clicking a scan works equally well.  Loading new STL files resets to GPA mode.

The **Loaded Scans** and **Reference Surface** panel widths are adjustable: drag the
divider between the sidebar and the main area to widen or narrow the panel, which is useful
for long scanner filenames.

#### Registration parameters (Tab 3 – Registration)

Tab 3 organises its controls in four sub-tabs on the left; the 3-D overlay viewport
occupies the right side throughout.

| Sub-tab | Contents |
|---------|----------|
| **Setup** | Method combo (synced from sidebar), Max ICP iterations, Sample points, Occlusal zone Z-window, Run Analysis button |
| **Segmentation** | Seed picking, Undo Last Seed, Clear All Seeds, Dijkstra parameters, Recompute Metrics, Eraser Tool, Save/Export/Load segmentation |
| **Plane** | Above / Below plane spinboxes, plane-point count label, Show plane disks checkbox |
| **Re-Registration** | Crown-restricted ICP warm-start button, Keep-segmentation checkbox |

The **"Setup"** sub-tab exposes the core ICP tuning options:

| Control | Default | Meaning |
|---------|---------|---------|
| Method | *(synced from sidebar)* | Mirrors the sidebar reference-surface selection; both controls are kept in sync. |
| Max ICP iterations | 100 | Maximum iterations for the fine ICP stage per GPA cycle. |
| Sample points | 20000 | Number of points sub-sampled from each mesh for ICP correspondence search.  Reduce for speed; increase for accuracy on very coarse meshes. |

---

### Step 4 – Define the region of interest

Restricting distance metrics to the tooth-crown surfaces eliminates contributions from
gingival tissue, scan margins, and boundary artefacts.  Three approaches are available
(only one is active at a time; priority decreases down this list):

#### Option A – Tooth-crown segmentation (highest precision, recommended)

1. In **Tab 3 – Registration**, find the **Tooth Crown Segmentation** panel and click
   **📍 Pick Tooth Seeds**.  The button turns into **🛑 Stop Picking** while active.
2. Click once on the **occlusal or incisal surface** of each tooth crown you want to
   include.  A yellow sphere marks each clicked point.  You need one click per tooth crown
   (not per cusp — one click anywhere on the crown is sufficient).
   **Camera navigation still works in pick mode:** drag to rotate, scroll to zoom.  Only a
   short stationary click (< 6 pixels of mouse movement) registers as a seed point.
   **Zoom and pan are preserved** when seeds are placed — the view does not reset.
3. Click **🛑 Stop Picking** when done.  The software immediately runs a curvature-weighted
   Dijkstra region-growing algorithm from each seed point and colours the mesh:
   - **Ivory** = tooth crown (included in metrics)
   - **Dark grey** = gingiva / margins (excluded)
   The status label below the button reports the vertex count of the segmented crown area.
4. **Correct seed mistakes:** click **Undo Last Seed** to remove the most recently placed
   seed and immediately re-run segmentation.  The button can be pressed repeatedly to step
   back through seeds one by one.  To remove all seeds at once, click **Clear All Seeds**.
5. If the segmentation overshoots (gingiva included) or undershoots (parts of crown
   missing), adjust the segmentation parameters:

| Spinbox | Default | Effect |
|---------|---------|--------|
| Max geodesic | 10 mm | Curvature-weighted geodesic budget.  On convex crown surfaces this closely equals physical mm; concave zones (CEJ, gingival sulcus) consume the budget faster.  Decrease if gingiva bleeds in; increase if parts of the crown are cut off. |
| CEJ crease | 35° | Hard-stop crease angle between adjacent faces.  Decrease to stop earlier at sharp CEJ kinks; increase (e.g. 50–65°) if the crown has abrupt ridges that falsely trigger the stop. |
| Min curvature | −2 /mm | Hard-stop floor on face mean κ_H.  Expansion is blocked when κ_H drops below this threshold (gingival sulcus guard).  Increase toward 0 to stop earlier; decrease (e.g. −4 to −6) if shallow crown concavities are being cut off. |

   Segmentation re-runs automatically whenever a spinbox changes (no need to re-place seeds).

6. **Fine-tune with the Eraser Tool:** if the segmentation still bleeds onto gingival
   tissue in isolated spots, click **Eraser Tool** (turns into **Stop Erasing**), then
   left-click on each incorrectly included area in the 3-D viewport.  Each click removes
   all ivory-coloured vertices within the **Brush radius** (default 2 mm) of the click
   point.  Multiple clicks accumulate.  Click **Stop Erasing** when finished, or click
   **Clear Erase Zones** to undo all erases and restore the full Dijkstra result.
   Erase zones are world-space spheres and persist even if you later change a parameter
   spinbox — the Dijkstra step re-runs but the erase zones are always re-applied on top.

7. Check **Keep segmentation after registration** (default: on) if you want the ivory/grey
   segmentation overlay to be restored automatically whenever the registration is updated.
   Uncheck it if you prefer to see the semi-transparent multi-scan overlay instead.
8. Click **⟳  Recompute Metrics** to refresh distance statistics and the metrics table with
   the new crown mask.
9. Optionally click **⟳  Recompute Registration** for a crown-restricted ICP refinement
   pass.  This re-runs ICP using only tooth-crown vertices for correspondences (3 mm search
   radius, warm start from the existing alignment), then updates the GPA mean surface and
   recomputes all distance fields.  Typical improvement: 0.02–0.05 mm RMS reduction by
   excluding noisy gingival tissue from the ICP objective.
   After the run completes, **⟳  Recompute Metrics** and **⟳  Recompute Registration** are
   re-enabled automatically (if "Keep segmentation after registration" is checked), so you
   can run the cycle again or compare metrics before and after.
10. **Save and reload the segmentation:** click **Save Segmentation…** to write the current
    seeds, erase zones, and parameter values to a `.dsc_seg` file.  The default filename is
    `<ReferenceName>_segmentation.dsc_seg` (e.g. `GPA_mean_segmentation.dsc_seg`).  The file
    records which reference surface the segmentation was built on.  To reload it in a later
    session, click **Load Segmentation…**, choose the file — seeds, erase zones, and
    parameters are restored and segmentation re-runs automatically.
11. **Export the crown subset STL:** click **Export Crown Subset…** to save each loaded scan
    trimmed to the bounding box of the active tooth mask.  One binary STL file per scanner is
    written to a chosen directory; each file is named `<original_stem>-subset.stl`.  All
    subset files share the same axis-aligned bounding box (derived from the reference scan's
    effective mask), so they are already co-registered when loaded into another tool.
    If a file with that name already exists you will be asked to provide a new name.
12. If the segmentation is still wrong, click **Clear All Seeds** and repeat from step 1.

The segmentation expands from each seed using a **curvature-weighted geodesic distance**
as the primary stopping criterion.  Each face-to-face edge costs physical distance ×
(1 + penalty for concavity), where the penalty is derived from the minimum principal
curvature κ_min = κ_H − √(κ_H²−κ_G).  This makes the budget consumed faster in
concave regions (CEJ saddle zone, gingival sulcus), so expansion decelerates naturally
at the tooth-gum margin — even before the two hard-stop safety nets fire:
a crease-angle check (≥ 35° kink between face normals signals the CEJ) and
a curvature floor (mean κ_H > −2 mm⁻¹ rejects the gingival sulcus).

The segmentation is computed **independently for every loaded scan** using the same
world-space seed coordinates.  This is necessary because different scanners capture
different numbers of vertices; sharing a mask by vertex index would silently produce wrong
results for all scans except the one the mask was built on.

#### Option B – Fitted occlusal plane (medium precision)

Without placing tooth seeds, the picked points define an occlusal plane.  Once ≥ 3 points
are picked, the software fits a least-squares plane through them (smallest eigenvector of
the covariance matrix).  The "Above plane [mm]" and "Below plane [mm]" spinboxes define
an asymmetric slab around this plane that is kept for metrics.

Default offsets: +2 mm above (minimal gingival inclusion), −12 mm below (includes full
crown depth).  Adjust if the arch is unusually deep or the plane is picked on gingiva.

To visualise the fitted plane, check **Show plane disks** in the Occlusal Plane section.
The checkbox starts unchecked; checking it draws three semi-transparent disks in the
Registration viewport (grey = plane, green = above zone, cyan = below zone).  The plane is
fitted silently whenever ≥ 3 seeds are placed and remains available for metrics even when
the disks are hidden.  Adjusting "Above plane" or "Below plane" spinboxes only redraws
the disks when the checkbox is checked.

#### Option C – Z-window (coarse, legacy)

The **Occlusal zone [mm]** spinbox (0 = disabled) restricts metrics to vertices within
that many millimetres below the scan's Z maximum (the occlusal tips after PCA alignment).
Set to 12 mm as a quick filter without picking any points.  Less accurate than Options
A and B because PCA alignment is not always perfectly Z-perpendicular.

---

### Step 5 – Run the analysis

Click **Run Analysis** (toolbar or menu).  A progress bar tracks the four pipeline stages:

1. **Coarse alignment** – PCA centering and principal-axis rotation for all scans.
2. **Orientation disambiguation** – 4-rotation Z-test (0°/90°/180°/270°) to resolve
   the PCA 180° ambiguity.  Required for scanners that export in an arbitrary coordinate
   frame.
3. **GPA iterations** – alternating ICP → mean-mesh update cycles until the maximum
   scan displacement between cycles is < 0.01 mm.
4. **Distance field and metrics** – CGAL AABB tree on the GPA mean surface, per-vertex
   signed distances, metric aggregation.

Typical runtime: 60–120 seconds for five scans of 500 k triangles each.  The mean-mesh
update (Stage 4 of GPA) is the slowest step (~30 s) because it performs an AABB query
for every reference vertex against every scan.

After completion, Tabs 3, 4, and 5 are populated automatically.

---

### Step 6 – Read the Distance Maps

**Tab 4 – Distance Maps** shows one colour-coded 3-D mesh per scanner.  See
[Distance Map – Interpretation Guide](#distance-map--interpretation-guide) for colour
encoding.  Drag to rotate, scroll to zoom.  All viewports share the same colour scale for
direct visual comparison.

**Masked display (when tooth-crown seeds have been placed):**  When a tooth-crown
segmentation is active, the distance maps highlight only the crown vertices in the diverging
blue–white–red colour map.  Non-crown vertices (gingiva, margins, palatinal tissue) are
rendered in uniform **dark grey**.  This focuses the visual comparison on the clinically
relevant crown area and removes gingival noise from the colour scale.  To see the full-mesh
distance map, click **Clear Seeds** in the Registration tab and then **⟳  Recompute Metrics**.

**Adjusting the colour scale:**  A **Colour scale: ± [value] mm** spinbox sits at the top
of the Distance Maps tab.  Reducing the value clips the most extreme distances to solid
red/blue and stretches the gradient over the remaining range, making small systematic
differences visible.  This is a purely visual control — the statistical metrics (RMS, MAD,
Hausdorff) in Tab 5 are not affected.  Click **⟳ Auto** to revert to the automatic range
(±H95 of the worst scanner, max 2 mm).

---

### Step 7 – Read the Metrics table

**Tab 5 – Metrics** lists all scanners in rows.  Column headers match the abbreviations
in the [Metric Reference](#metric-reference) section below.  Cells are highlighted green
(best value in column) and red (worst value) to make outliers immediately visible.  Click
any column header to sort.

---

### Step 8 – Export results

**Tab 6 – Export** writes all results to a chosen directory:

- `fingerprint.png` — tessellation scatter plot at 300 dpi
- `distance_<scanner>.png` — distance map per scanner at 300 dpi
- `metrics.csv` — full metrics table with UTF-8 BOM (opens correctly in Excel on Windows)

---

### About tab

**Tab 7 – About** shows author information:

- **Prof. Dr. Karl-Heinz Kunzelmann** — displayed in bold
- Clickable link to [www.kunzelmann.de](http://www.kunzelmann.de)
- Build stack (Qt, VTK, CGAL, Eigen versions)
- Brief pipeline summary for orientation

The **window title bar** also carries the author name:
`DentScanCompare – Dental Scan Quality Analyzer   |   Prof. Dr. Karl-Heinz Kunzelmann`

---

## Metric Reference

### Tessellation quality metrics

Tessellation metrics are computed before registration from the raw loaded mesh.  They
reflect intrinsic scanner properties, not accuracy.

| Column | Full name | Unit | Interpretation |
|--------|-----------|------|----------------|
| **Triangles** | Triangle count | – | Total faces after import and repair.  Reflects the spatial resolution chosen by the scanner firmware.  More triangles → finer detail, larger file, longer processing time. |
| **Edge** | Mean edge length | mm | Average of all three edge lengths per triangle, averaged over all triangles.  Smaller = finer mesh.  Typical range for modern intraoral scanners: 0.10–0.25 mm. |
| **AspRatio** | Mean aspect ratio | – | (Longest edge) / (shortest edge) per triangle, averaged.  An equilateral triangle gives 1.0.  Values above ~2.5 indicate elongated "needle" triangles which degrade curvature estimation and FEA accuracy. |
| **ATI** | Adaptive Tessellation Index | –1 to +1 | Spearman rank correlation between |mean curvature| (|κ_H|) and the reciprocal of triangle area (1/A), per triangle.  ATI ≈ +1: ideal adaptive mesh (small triangles at cusps and margins, large on flat areas).  ATI ≈ 0: uniform tessellation.  ATI < 0: inverted (coarser where curvature is highest). |
| **DensHighκ** | Triangle density in high-curvature zone | /mm² | Triangles per mm² for vertices where |κ_H| exceeds the per-scan median.  Higher = more detail in the clinically critical cusp/fissure region. |
| **DensLowκ** | Triangle density in low-curvature zone | /mm² | Triangles per mm² for vertices where |κ_H| ≤ median (palate, buccal gingiva).  Lower relative to DensHighκ = better adaptive behaviour. |

### Accuracy metrics

Accuracy metrics require a completed GPA registration.

| Column | Full name | Unit | Interpretation |
|--------|-----------|------|----------------|
| **RMS** | Root Mean Square distance | mm | √(Σ dᵢ² / n).  Primary accuracy metric.  Sensitive to outliers — a few large errors inflate this strongly.  Lower is better. |
| **MAD** | Median Absolute Deviation | mm | Median of |dᵢ|.  Robust to scan-boundary artefacts.  Reflects the "typical" error of the central 50% of the surface.  Read alongside RMS: large RMS + small MAD → few extreme outlier regions rather than a global systematic error. |
| **H95** | 95th-percentile Hausdorff | mm | The 95th percentile of |dᵢ|.  Clinically meaningful: 95% of the scan surface is within H95 mm of the reference.  Boundary artefacts are in the top 5% and therefore excluded. |
| **H100** | Maximum Hausdorff | mm | The single largest |dᵢ|.  Dominated by scan margins, incomplete patches, and topological holes.  Interpret alongside Coverage% and Holes.  High H100 with low RMS/MAD = accurate surface, fraying edges. |
| **Bias** | Signed mean distance | mm | Mean of dᵢ (signed).  Positive = scan surface lies systematically outside the reference (expansive distortion, oversized arch).  Negative = systematically undersized (gingival compression, inward collapse).  Near zero = no directional systematic error. |

### Completeness metrics

| Column | Full name | Unit | Interpretation |
|--------|-----------|------|----------------|
| **Coverage%** | Surface coverage | % | Percentage of GPA reference vertices whose nearest scan vertex is within 0.2 mm.  100% = full capture of the reference anatomy. |
| **Boundary** | Open boundary length | mm | Total length of edges belonging to only one face.  Every arch scan has at least one open boundary (the gingival margin), so a non-zero value is expected and normal.  Very high values indicate fragmented scan margins. |
| **Holes** | Topological hole count | – | Number of closed holes counted via Euler characteristic (V − E + F).  An ideal arch scan has 0 interior holes (the gingival margin is an open boundary, not a hole).  Each additional hole represents a missing patch where the scanner lost tracking. |
| **Stitch** | Maximum stitching-artefact angle | ° | Maximum normal-discontinuity angle between adjacent faces across the entire mesh.  Values above 90° indicate visible stitching artefacts at strip-merge junctions.  Values near 180° at the mesh boundary are expected (normal flip at open edges). |

---

## Tessellation Fingerprint – Interpretation Guide

The scatter plot in **Tab 2** shows one dot per triangle for every loaded scan.

**Axes (both log scale):**

- **X-axis (horizontal):** |Mean curvature| (1/mm).  Left = flat regions (palate, buccal
  gingiva, flat tooth surfaces).  Right = highly curved regions (cusp tips, fissures,
  marginal ridges).  A sphere of radius r has |κ_H| = 1/r; a typical molar cusp has
  radii of ~0.3–1.0 mm, giving |κ_H| of 1–3 mm⁻¹.
- **Y-axis (vertical):** Triangle area (mm²).  Top = large, coarse triangles.
  Bottom = small, fine triangles.

**What to look for:**

**Direction of the cloud.**  An adaptive scanner concentrates fine triangles where
curvature is high: the cloud slopes from top-left (flat, coarse) to bottom-right (curved,
fine).  This negative diagonal is expected for a well-adapted mesh.  ATI quantifies the
strength of this slope as a single number.

**Width and scatter.**  A tight, narrow cloud means consistent tessellation behaviour
across the scan.  A diffuse cloud or bimodal distribution suggests the scanner uses very
different strategies in different anatomical zones (e.g., coarser on the palate, fine on
tooth crowns).

**Position relative to other scanners.**  A cloud shifted toward bottom-left has many fine
triangles even on flat surfaces (high total triangle count, possibly wasteful but safe).
A cloud shifted toward top-right has coarse triangles even on curved surfaces — clinically
concerning because marginal ridges and cusps are poorly resolved.

**Comparing scanners.**  Each scanner has its own colour.  Overlapping clouds indicate
similar tessellation strategies.  A scanner whose cloud extends further toward the
bottom-right corner captures finer curvature detail.

**Typical findings (five-scanner DefektIIa dataset):**  All five scanners show the
expected negative diagonal trend, confirming at least some degree of adaptive
tessellation.  Primescan shows the densest cluster at high curvature (bottom-right)
consistent with its highest triangle count.  Trios5 achieves the best ATI (≈ 0.250),
meaning its triangle-to-curvature correlation is strongest.  FussenS6000 has the lowest
ATI (≈ 0.088), indicating the most uniform tessellation strategy.

---

## Distance Map – Interpretation Guide

**Colour encoding (diverging blue–white–red, shared scale across all scanners):**

- **Red** = positive distance = scan surface lies **outside** the reference (oversized,
  buccally expanded arch)
- **White** = zero deviation (on the reference surface)
- **Blue** = negative distance = scan surface lies **inside** the reference (undersized,
  inward collapse)

The colour scale is set automatically to ±H95 of the worst scanner in the set (clamped
to ±2 mm).  All five maps share the same scale for direct visual comparison.

**Clinical interpretation:**

- Widespread red on buccal aspects → scanner produces an expanded arch
- Widespread blue on the occlusal surface → scanner records teeth as shorter/less
  protruding than the reference
- Mixed red/blue with sharp boundaries → stitching artefact, local registration failure,
  or a true scanner-specific deformation pattern
- Large grey region in the centre → the DefektIIa standardised defect (artificial missing-
  tooth area); colours there may be artefactual if coverage is incomplete

**Note on signed distances.**  Positive = scan is outside the reference (farther from the
palate / tongue).  Negative = scan is inside (closer to the palate / tongue).  This
convention holds after GPA alignment with the occlusal surface pointing toward +Z.

---

## STL file naming convention

The scanner name is extracted from the STL **filename** using underscore `_` as a
delimiter: the second token is taken as the scanner name.

```
DefektIIa_Primescan_30_3min23s_r3.stl   →  "Primescan"
DefektIIa_Trios5_30_4min01s_r1.stl      →  "Trios5"
DefektIIa_Mediti700_30_2min45s_r2.stl   →  "Mediti700"
```

If the filename has fewer than two underscore-delimited tokens, the full stem is used as
the scanner name.  To ensure correct scanner identification, follow the
`<model>_<Scanner>_<...>.stl` naming pattern when adding new scans.

---

## Important considerations

**Tooth-crown seeds and erase zones can be saved to disk.**  Click **Save Segmentation…**
in the Registration tab to write a `.dsc_seg` file containing the seed coordinates, erase
zones, and parameter values.  The default filename includes the reference surface name for
easy identification.  **Load Segmentation…** restores the full setup and re-runs
segmentation automatically — so you do not need to re-click seed points when reopening the
application for the same dataset.

**One seed per tooth crown is sufficient.**  The Dijkstra region-growing algorithm expands
outward from each seed using geodesic distance (≤ 10 mm surface path from the seed) as
its primary criterion.  This radius covers every tooth type: molars (8–12 mm), premolars
(7–10 mm), canines (9–12 mm), and incisors (10–13 mm).  Placing multiple seeds on the
same tooth is harmless but unnecessary.

**Do not place seeds on gingiva.**  The algorithm expands outward from the seed; a seed on
gingival tissue will try to grow into gingiva first.  Place seeds on the occlusal/incisal
surface (cusps, marginal ridges, incisal edges).

**The "Keep segmentation after registration" checkbox controls what you see in the
Registration viewport.**  When checked (default), the ivory/grey segmentation overlay is
restored automatically every time the registration is updated — after both **Run Analysis**
and **⟳  Recompute Registration**.  When unchecked, the standard semi-transparent
multi-scan overlay is shown instead.  The segmentation data itself (`m_pickedPts`,
`m_toothMask`) is preserved either way; only the visual layer changes.

**The fingerprint scatter plot can highlight one scanner at a time.**  Click a scanner
name in the **Loaded Scans** list or in the plot legend to bring that scanner to the
foreground.  The selected scanner's dots are drawn at full opacity on top of all others;
the rest are dimmed.  Click the same entry again to return to the full view.  The
highlighted state does not affect the ATI table or any statistics.

**Camera navigation works normally in pick and erase modes.**  While **📍 Pick Tooth Seeds**
or **Erase Gingiva** is active, drag with the left mouse button to rotate the model and
scroll the mouse wheel to zoom.  Only a short stationary click (< 6 pixels of movement) is
interpreted as a seed placement or erase action.  You never need to exit the mode to
re-orient the view.  **The zoom and pan position are preserved** when seeds are placed or
parameters change — the viewport does not reset to the default camera position.

**Segmentation is per-scan.**  The seed coordinates are world-space positions on the
registered meshes.  The same seed points are applied independently to each loaded scan,
so each scanner's mesh gets its own crown mask.  This is essential because different
scanners have different vertex counts; a mask shared by vertex index would be silently
wrong for all but the reference scan.

**Last-used directories are remembered.**  The application stores the last directory you
opened STL files from and the last export directory in your user settings
(QSettings, application "DentScanCompare").  These are restored automatically the next
time you open the file dialog or the export dialog.

**Reference mode resets when new files are loaded.**  Whenever you open a new set of STL
files, the reference-surface selector in the sidebar automatically returns to "GPA mean
(all scans)".  If you want to use a fixed reference, re-select it from the Loaded Scans
list after loading.

**GPA is self-referential.**  The mean reference surface is the centroid of all loaded
scans.  Adding or removing a scan changes the reference, which changes the distance
metrics for all other scans.  Always compare the same set of scans in a single session.

**The GPA mean reference is not a ground truth.**  If one scanner has a gross systematic
error (e.g., a whole arch expanded by 0.3 mm), the mean reference is shifted by 0.3/N in
that direction.  This slightly underestimates errors of the outlier scanner and slightly
overestimates errors of the others.  For a five-scanner comparison this effect is small
(~0.06 mm) but should be noted in publications.

**Primescan winding quirk.**  Primescan exports triangle faces wound in the opposite
direction from the other scanners tested.  The STL loader corrects this automatically
using the stored per-face normal; no user action is required.  If a new scanner produces
systematically inverted normals in the distance map, the loader's winding check is the
first place to inspect.

**Analysis runtime.**  On a desktop with a modern CPU (8+ cores), the full analysis of
five scans at ~500 k triangles each takes 60–120 seconds.  The bottleneck is the GPA
mean-mesh update (~30 s), which performs an AABB closest-point query for every reference
vertex against every scan and is currently single-threaded.
