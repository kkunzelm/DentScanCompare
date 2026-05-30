# DentScanCompare – Developer Handoff

## Goal

Compare 5 dental intraoral scanners (Trios5, Primescan, Mediti700, iTeroLumina, FussenS6000)
that each scanned the same test surface (DefektIIa, an artificial dental defect model).
No external reference geometry is available → the reference surface is computed internally
via Generalized Procrustes Analysis (GPA) as the mean of all aligned scans.

STL test files live at: `/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/`

---

## Build environment (Debian 13 / Linux 6.12)

| Library | Version | Notes |
|---------|---------|-------|
| Qt | **5.15** (not 6!) | VTK 9.3 on this machine was compiled against Qt5; using Qt6 causes `INTERFACE_QT_MAJOR_VERSION` conflict at link time |
| VTK | 9.3 | Has MPI dependency – see CMake pitfall below |
| CGAL | 6.0.1 | Property-map API changed in 6.0 – see code pitfall below |
| Eigen | 3.4.0 | Matrix math for ICP |
| nanoflann | 1.7 | Header-only KD-tree; lives in `/usr/include/nanoflann.hpp` |

### Critical CMake setup (`CMakeLists.txt` root)

```cmake
project(DentScanCompare VERSION 1.0 LANGUAGES CXX C)   # C language required
find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Concurrent PrintSupport OpenGL)
find_package(MPI QUIET)                                 # must come BEFORE VTK
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)
endif()
find_package(VTK 9.3 REQUIRED COMPONENTS ...)
```

`LANGUAGES CXX C` enables the MPI C-language detection.  VTK's targets file unconditionally
references `MPI::MPI_C`; without the C language enabled, CMake cannot create that target and
the configure step fails.

---

## Source layout

```
src/
├── main.cpp
├── MainWindow.{h,cpp}         Qt main window, 6 tabs, async analysis pipeline
├── core/
│   ├── Mesh.h                 SurfaceMesh type aliases + ScanData struct
│   ├── MetricReport.h         Plain metric aggregate struct (no logic)
│   ├── STLReader.{h,cpp}      Binary STL → CGAL SurfaceMesh
│   ├── CurvatureAnalysis.{h,cpp}   CGAL interpolated_corrected_curvatures
│   ├── TessellationMetrics.{h,cpp} ATI, mean edge, aspect ratio, density
│   ├── ICPRegistration.{h,cpp}     Point-to-plane ICP (nanoflann + Eigen SVD)
│   ├── GPAReference.{h,cpp}        GPA: PCA → 4-orient test → ICP → mean mesh
│   ├── DistanceField.{h,cpp}       CGAL AABB-tree → per-vertex signed distances
│   ├── ToothSegmentation.{h,cpp}   Dijkstra crown segmentation (geodesic + CEJ guard)
│   └── ArchMetrics.{h,cpp}         Open boundary, hole count, stitching angles
└── visualization/
    ├── VTKMeshWidget.{h,cpp}        QVTKOpenGLNativeWidget + vtkPolyData pipeline
    ├── ColorMapLUT.{h,cpp}          VTK LUTs + scanner color palette
    ├── ScatterPlotWidget.{h,cpp}    QPainter log-log scatter plot (NOT vtkChartXY)
    └── MetricsTableWidget.{h,cpp}   QTableWidget + CSV export
export/
    ├── ReportExporter.{h,cpp}       PNG export via VTK offscreen render
    └── VTKSceneExporter.{h,cpp}     High-res VTK scene export
```

---

## Analysis pipeline (in order)

1. **STLReader** – Binary STL → polygon soup → CGAL `SurfaceMesh`.
   Per-face: use stored STL normal to verify winding order; if cross-product of edges is
   anti-aligned with STL normal, swap vertices 1 and 2.  This handles scanners (Primescan)
   that export faces wound in the opposite direction.
   After soup construction: `repair_polygon_soup` → `orient_polygon_soup` → `polygon_soup_to_polygon_mesh`.

2. **CurvatureAnalysis** – `CGAL::Polygon_mesh_processing::interpolated_corrected_curvatures`.
   Stores mean curvature in vertex property map `"v:mean_curv"`, Gaussian in `"v:gauss_curv"`.

3. **TessellationMetrics** – Per-face area, |κ_H|, aspect ratio.  Computes ATI (Spearman
   correlation of |κ_H| vs. 1/area).

4. **GPAReference::compute** – Three-stage alignment:
   - **Stage 1 – PCA coarse alignment**: Translate to centroid, rotate largest-variance
     axis → X, smallest → Z (occlusal normal).  Z-sign from curvature: high-curvature
     side (teeth) → +Z.
   - **Stage 2 – 4-orientation Z-rotation test**: PCA leaves 180° (and potentially 90°/270°)
     ambiguity.  Deep-copies each scan, tries 0°/90°/180°/270° Z rotations, evaluates each
     with 8 ICP iterations at 20 mm search radius, picks the best.  Keep this – it's needed
     for generic scanner support.
   - **Stage 3 – GPA iterations**: First cycle: coarse ICP (15 mm, 30 iter), then fine ICP
     (5 mm, 100 iter).  Subsequent cycles: fine ICP only.  Converges when max scan displacement
     < `convergenceThresh` (default 0.01 mm).
   - **Stage 4 – True mean mesh**: After convergence, update the reference mesh vertices to
     the mean of closest points on all aligned scans (AABB query per scan, average results).
     This ensures ALL 5 scanners show non-zero distance metrics.

5. **DistanceField** – CGAL AABB tree on the GPA mean reference.  Per vertex of each scan:
   closest point + primitive, signed by dot(diff, face_normal).
   `fillReport(scan, report, coverageThreshold, zWindowMm, plane, toothMask)`:
   Filter priority (highest wins):
   - `toothMask` non-empty → use only vertices where `toothMask[v.idx()]` is true.
   - `plane.active` → restrict to slab `[-belowMm, +aboveMm]` around fitted occlusal plane.
   - `zWindowMm > 0` → simple global Z-window (legacy; still useful as fallback).
   - Otherwise: all vertices.

6. **ToothSegmentation** – Multi-source Dijkstra on the face adjacency graph.
   Given one seed per tooth crown (clicked on the occlusal/incisal surface), expands
   outward face-by-face.  Edge cost is **curvature-weighted geodesic distance**:

   ```
   W(f, nb) = dist(centroid_f, centroid_nb)
              × (1 + curvatureRepulsion × max(0, −κ_min_avg))
   ```

   where `κ_min_avg = (κ_min(f) + κ_min(nb)) / 2` and `κ_min` is the minimum
   principal curvature, computed per face from the existing property maps as
   `κ_H − √max(0, κ_H² − κ_G)`.  κ_min is more sensitive than κ_H at saddle-like
   CEJ geometry (convex along the arch, concave circumferentially), where κ_H ≈ 0
   but κ_min is strongly negative.  With `curvatureRepulsion = 0.1` (default):

   | Zone | κ_min | Edge factor | Effect |
   |------|-------|-------------|--------|
   | Convex crown | ≥ 0 | 1.0 | No penalty |
   | Crown fissure | ≈ −3 | 1.3 | Still reachable within budget |
   | CEJ saddle zone | ≈ −4.5 | 1.45 | Budget consumed faster → natural deceleration |
   | Gingival sulcus | ≈ −10 | 2.0 | Hard stop still backs it up |

   Stopping criteria (all must pass to expand into a neighbour):
   - **Primary**: accumulated curvature-weighted cost ≤ `maxGeodesicMm` (default 12).
     On convex crown surfaces this closely equals physical mm; concave zones cost extra.
     A full clinical crown reaches the cusp tip within budget for every tooth type:
     molars 8–12 mm, premolars 7–10 mm, canines 9–12 mm, incisors 10–13 mm.
   - **Hard stop – crease angle**: normal dot-product between adjacent faces must be
     above `cos(maxCreaseAngleDeg)` (default 50°).  Safety net for abrupt CEJ kinks.
   - **Hard stop – curvature floor**: face mean κ_H must be > `minMeanCurvature`
     (default -4 mm⁻¹).  Safety net for deep gingival sulcus.
   Setting `curvatureRepulsion = 0` restores the original pure-geodesic behaviour.
   Returns `std::vector<bool>` per vertex (true = tooth crown).
   Stored in `MainWindow::m_toothMask`; recomputed whenever operator adds seed points.

7. **ArchMetrics** – Open boundary edge sum, Euler-characteristic hole count,
   stitching artifact angle via adjacent half-edge normals.

---

## Known pitfalls / historical bugs

### CGAL 6.0 property_map getter API change

```cpp
// OLD (CGAL 5.x) – returns std::pair<PropertyMap, bool>
auto [map, ok] = mesh.property_map<VertexDesc, double>("v:mean_curv");

// NEW (CGAL 6.0) – returns std::optional<PropertyMap>
auto opt = mesh.property_map<VertexDesc, double>("v:mean_curv");
if (opt.has_value()) { auto map = opt.value(); ... }
```

`add_property_map()` still returns `std::pair` in CGAL 6.0 – only the getter changed.

### vtkChartXY invisible data (Qt5 + VTK 9.3)

`vtkChartXY` renders axes and legend but silently drops data points when used inside
`QVTKOpenGLNativeWidget` in this Qt5/VTK9.3 configuration.
`ScatterPlotWidget` was fully rewritten as a QPainter-based log-log scatter plot.
Do **not** reintroduce VTK chart headers there.

### STL winding and normal orientation

Primescan exports triangles wound opposite to the other four scanners.
`CGAL::orient_polygon_soup()` makes winding consistent *within* a mesh but cannot
determine the absolute outward direction for open meshes (dental arches are open).
The fix is in `STLReader.cpp`: per-face cross-product check against the stored STL normal.

### GPA reference = Primescan problem

Before Option B was implemented, the GPA reference was a static copy of Primescan
(highest triangle count).  Primescan was excluded from ICP, so its distance metrics
were always 0.  The mean-mesh update step (`updateToMeanMesh`) fixes this.

### ICP alignment of FussenS6000 and iTeroLumina

These two scanners use a coordinate system 28 mm away from the others.  A direct fine ICP
(5 mm search radius) produced no correspondences → result was identity.  Fixed by PCA
coarse alignment.  Residual 180° flip handled by the 4-orientation test.

---

## Achieved alignment quality (five-scanner test dataset)

| Scanner | RMS [mm] | Notes |
|---------|---------|-------|
| Primescan | ~0.000 | At GPA mean; was 0.000 before Option B |
| Mediti700 | 0.668 | Largest deviation |
| Trios5 | 0.265 | |
| FussenS6000 | 0.127 | Required PCA + 4-orient fix |
| iTeroLumina | 0.107 | Required PCA + 4-orient fix |

---

## GUI layout (7 tabs)

**Left sidebar** (fixed 200 px, visible on all tabs):

| Widget | Description |
|--------|-------------|
| "Loaded Scans" QGroupBox | `QListWidget m_scanList` — one row per loaded scan.  Clicking a row highlights that scanner in the fingerprint scatter plot.  In "Fixed reference scan:" mode, the click also selects that scan as the GPA registration reference; the row gets a **★** marker and bold font. |
| "Reference Surface" QGroupBox | Two `QRadioButton`s: **"GPA mean (all scans)"** (default) and **"Fixed reference scan:"**.  A `QLabel m_refFixedLabel` below the second radio shows the current reference name.  Switching to "Fixed reference scan:" adopts the currently highlighted scan; clicking a scan in the list while this radio is active changes the reference immediately.  `int m_fixedRefScanIdx = -1` is the single source of truth (−1 = GPA mode). |
| Status / progress | `QLabel m_statusBar` + `QProgressBar m_progress` |

**Tab contents:**

| Tab | Content |
|-----|---------|
| Overview | N × VTKMeshWidget (one per loaded scan) in a horizontal-scrolling `QScrollArea`.  Widgets are created by `rebuildScanWidgets()` called from `onLoadFinished()`; each has `setMinimumWidth(240)` so ≤ 5 scans fill the viewport, 6+ trigger the horizontal scrollbar. |
| Fingerprint | QPainter log-log scatter (triangle area vs. \|κ\|), ATI table.  Legend rows and Loaded-Scans list items are clickable: selects a highlight series; unselected series are dimmed to alpha=18. |
| Registration | Single `VTKMeshWidget m_overlayWidget` on the right (all scans semi-transparent, or tooth-segmentation overlay).  Left side: embedded `QTabWidget innerTabs` (min 210, max 420 px) with four sub-tabs, inside a `QSplitter` (sizes {280, 800}):<br>**Setup** — method combo (synced from sidebar), `m_maxIterSpin`, `m_sampleSpin`, `m_zWindowSpin`, Run Analysis button, `m_registrationStatus` label.<br>**Segmentation** — `m_pickBtn` (📍/🛑), `m_segStatusLabel`, `m_undoSeedBtn` (disabled until first seed), `m_clearPickBtn`, Max geodesic / CEJ crease / Min curvature spinboxes, `m_recomputeBtn`; **Gingiva Eraser** section: `m_eraseBtn` toggle (mutually exclusive with pickBtn), `m_eraseBrushSpin` (0.5–10 mm, default 2 mm), Clear Erase Zones; **Segmentation File** section: `m_saveSegBtn` (disabled until seeds exist), `m_exportSubsetBtn` (Export Crown Subset…), `m_loadSegBtn`.<br>**Plane** — `m_planeAboveSpin`, `m_planeBelowSpin`, `m_pickCountLabel`, `m_showPlanesChk` (starts unchecked).<br>**Re-Registration** — `m_reregisterBtn` (crown-restricted ICP warm-start), `m_keepSegChk` (default checked). |
| Distance Maps | N × VTKMeshWidget with diverging colour map (blue–white–red), same scroll behaviour as Overview.  Control bar at top: `± [spinbox] mm` colour-scale with `⟳ Auto` button.  When a tooth-crown mask is active, crown vertices use the LUT and non-crown vertices are forced to RGBA (55,55,55,255). |
| Metrics | MetricsTableWidget: rows = scanners, cols = metrics, green/red best/worst highlight |
| Export | Directory chooser → fingerprint PNG, distance map PNGs, metrics CSV |
| About | Rich-text QLabel with author name (Prof. Dr. Karl-Heinz Kunzelmann), clickable link to www.kunzelmann.de, build-stack list (Qt / VTK / CGAL / Eigen), and pipeline summary. |

**Window title:** `DentScanCompare – Dental Scan Quality Analyzer   |   Prof. Dr. Karl-Heinz Kunzelmann`

**GPU usage:** VTK rendering uses OpenGL (GPU).  All computation — CGAL curvature, ICP/GPA, distance field, metrics — runs on CPU only.  No CUDA/OpenCL path.

---

## What remains / potential improvements

- Intermolar distance and arch form deviation metrics (`archFormDeviation`,
  `intermolarDistance` in MetricReport) are not yet computed; ArchMetrics only computes
  boundary and stitching metrics.
- Tab 6 Export buttons are all wired to the same `showExportDialog()` slot instead of
  individual actions per export type.
- The GPA mean-mesh update adds ~30 s for large scans (696 k triangles × 5 AABB queries).
  Could be parallelised with `QtConcurrent::map`.
- CSV export uses UTF-8 BOM for Windows compatibility; column headers contain Unicode
  (κ, °, ²) – these render correctly on Windows with BOM.
- Seeds and erase zones are now saved/loaded via `.dsc_seg` (JSON) files — manual re-clicking
  across sessions is no longer required.  Automatic seed detection (local Z/κ_H maxima per
  tooth) would eliminate the initial manual step entirely.

---

## Changelog (reverse chronological)

### 2026-05-30 – Export Crown Subset + Registration tab split into four sub-tabs

**Export Crown Subset** (`m_exportSubsetBtn`, "Export Crown Subset…").  New button in the
**Segmentation** sub-tab (below "Save Segmentation…").  `exportCrownSubset()` implementation:

1. Finds the reference scan iterator.  Computes the effective mask (`applyEraseZones(m_toothMask, *ref)`) and derives its axis-aligned bounding box from the world-space positions of all `true` vertices.
2. Prompts the user for an output directory via `QFileDialog::getExistingDirectory`.
3. Iterates `m_scans`.  For each scan, calls `writeBBoxSubsetSTL(scan, outPath, bMin, bMax)`:
   - Includes a face if its centroid lies inside the bbox.
   - Writes binary STL: 80-byte header, `quint32` face count, then per-face: 12-byte normal (cross-product of edges), 3 × 12-byte vertices, 2-byte attribute (`quint16` 0).
4. If `<stem>-subset.stl` already exists, asks the user for an alternative name via `QInputDialog::getText`.

The bounding box is computed from the **reference scan only** but applied to **all scans** (they share the same coordinate frame post-GPA registration), so all subset files can be loaded together into a third-party tool and remain correctly aligned.

**Registration tab split.**  The single `QScrollArea` control panel was replaced by an embedded
`QTabWidget` with four sub-tabs.  Only the left panel changed — the single `m_overlayWidget`
(VTKMeshWidget) stays on the right side of the `QSplitter` (no VTK duplication, no extra GPU
resources).

```
setupTab3Registration() helper lambdas:
  makeScrolled(panel)  →  QScrollArea* wrapping panel, same scroll policy as before
  makeForm(parent)     →  std::pair<QWidget*, QFormLayout*>
                          (C++17 structured binding: auto [panel, form] = makeForm())

innerTabs structure:
  "Setup"          → method, iterations, sample count, Z-window, run btn, status label
  "Segmentation"   → seeds, undo, clear, Dijkstra params, recompute, eraser, file I/O
  "Plane"          → above/below spinboxes, pick count, show-planes checkbox
  "Re-Registration"→ reregister btn, keep-seg checkbox

Splitter initial sizes: {280, 800}
```

All signal/slot connections (unchanged from before) remain at the bottom of
`setupTab3Registration()` — the refactor only reorganised the widget tree.

### 2026-05-30 – Seed undo, gingiva eraser, segmentation save/load, viewport fixes

**Undo Last Seed** (`m_undoSeedBtn`).  New button added to the Tooth Crown Segmentation
panel, disabled until the first seed is placed.  `clicked` handler pops `m_pickedPts.back()`,
calls `m_overlayWidget->showPickSpheres()` and (if ≥ 3 seeds remain) `fitOcclusalPlane()`,
then calls `runSegmentation()`.  When the last seed is removed it delegates to
`clearPickedPoints()`.  Button re-enabled state is maintained in `onPointPicked()`,
`clearPickedPoints()`, and the undo handler itself.

**Gingiva Eraser.**  New UI section in the Registration tab control panel between "Clear All
Seeds" and "Segmentation Parameters".  Two new members:
- `QPushButton* m_eraseBtn` (toggle, mutually exclusive with `m_pickBtn` via `toggled` handlers)
- `QDoubleSpinBox* m_eraseBrushSpin` (0.5–10 mm, default 2 mm)

Both buttons reuse the existing `VTKMeshWidget::setPickMode(bool)` / `pointPicked` signal so
no new VTK machinery is needed.  `onPointPicked()` checks `m_eraseBtn->isChecked()` at the
top and routes to `onErasePointPicked(x, y, z)` before the seed-placement logic.

`onErasePointPicked()` appends `{centre, radius}` to
`std::vector<std::pair<std::array<double,3>, double>> m_eraseZones` (new member), then calls
`applyEraseZones(m_toothMask, **refIt)` and updates the overlay and distance maps.

`applyEraseZones(mask, scan)` — new const method — iterates all mesh vertices and sets
`mask[v.idx()] = false` for any vertex whose world-space position falls within any stored
sphere.  Called in:
- `runSegmentation()` — applies zones to `m_toothMask` before `showToothSegmentation()`
- `recomputeMetrics()` — applies zones to each per-scan mask before `fillReport()`
- `updateDistanceMapsTab()` — applies zones to each per-scan mask before `showDistanceMap()`

`clearPickedPoints()` also clears `m_eraseZones` and resets `m_eraseBtn` to unchecked.
"Clear Erase Zones" button lambda clears `m_eraseZones` and refreshes the overlay with the
raw `m_toothMask`.  Key design: `m_toothMask` always stores the **raw Dijkstra result**;
erase zones are applied on top at every display/metric site — so parameter changes re-run
Dijkstra from scratch and then re-apply all existing erase zones automatically.

**Segmentation file I/O.**  New members `m_saveSegBtn` / `m_loadSegBtn` and methods
`saveSegmentation()` / `loadSegmentation()`.  File format: JSON (Qt `QJsonDocument`) with
extension `.dsc_seg`.  Schema:
```json
{
  "format_version": 1,
  "reference": "<scannerName or GPA_mean>",
  "seeds": [[x,y,z], ...],
  "erase_zones": [{"center":[x,y,z], "radius": r}, ...],
  "params": {"max_geodesic_mm": 12.0, "max_crease_deg": 50.0, "min_mean_curvature": -4.0}
}
```
Default save path: `<lastSegDir>/<refName>_segmentation.dsc_seg`.  `lastSegDir` persisted
via `QSettings`.  On load, spinbox values are set with `QSignalBlocker` to prevent premature
`runSegmentation()` calls; seeds are restored, then `runSegmentation()` is called once.
`m_saveSegBtn` is disabled until at least one seed is placed.

**Viewport / camera fixes in `VTKMeshWidget`:**
- `showToothSegmentation()` no longer calls `ResetCamera()`.  Previously every seed placement
  or parameter change reset the camera to the default position.  The camera set by the last
  `setOverlayMeshes()` call (triggered at load time) is now preserved across all subsequent
  segmentation updates.
- `buildPipeline()`: `m_titleLabel` now has `QSizePolicy::Fixed` vertically and is added to
  the layout with `stretch=0`; the `QVTKOpenGLNativeWidget` is added with `stretch=1`.
  Previously both had implicit `stretch=0`, causing Qt to divide vertical space evenly;
  now the label stays at its natural one-line height and the 3-D viewport fills all remaining
  vertical space.

**Button text normalisation.**  Emoji glyphs (`↩`, `🧹`, `💾`, `📂`) removed from the Undo,
Erase, Save, and Load buttons — they rendered as blank boxes on this platform.  The
pre-existing emoji on Pick Seeds (`📍`/`🛑`) and Recompute (`⟳`) were unaffected.

### 2026-05-30 – Curvature-weighted edge cost in tooth segmentation (κ_min)

**Problem**: The Dijkstra edge cost was a pure centroid-to-centroid physical distance.
Curvature (κ_H) was used only as a binary hard stop (floor −4 mm⁻¹), making the CEJ
boundary either too permissive (gradual transitions missed) or too tight (fissures
clipped) depending on the threshold.

**Change** (`ToothSegmentation.{h,cpp}`): Edge cost is now a curvature-weighted geodesic
distance — concave faces incur an extra multiplicative penalty so the budget is consumed
faster there and the algorithm decelerates naturally at the gingival margin:

```
W(f, nb) = dist × (1 + curvatureRepulsion × max(0, −κ_min_avg))
```

`κ_min` (minimum principal curvature) is computed per face from the existing κ_H and
κ_G property maps: `κ_H − √max(0, κ_H² − κ_G)`.  κ_min is more sensitive than κ_H at
saddle-like CEJ geometry (convex along the arch, concave circumferentially), where κ_H is
near zero but κ_min is significantly negative.  The `gaussian_curvature` (`v:gauss_curv`)
property map — already written by `CurvatureAnalysis::compute` — is read in
`buildFaceData()`; if unavailable, `κ_min` falls back to κ_H.

New `Params` field: `double curvatureRepulsion = 0.1`.  Setting it to 0 restores the
original pure-geodesic behaviour exactly.  The existing hard stops (crease-angle guard and
κ_H floor) are kept unchanged as safety nets.  `FaceData` gains a `minPrincipalCurv`
field populated in `buildFaceData()`.

Motivation: Curvature-Driven Graph Cut literature (the "Minima Rule") shows that using
curvature-weighted shortest-path costs — rather than binary thresholds alone — is the
most robust strategy for gingival margin detection.  The modification is fully backward-
compatible with the existing seed-placement UX (seeds on occlusal/incisal surfaces).

### 2026-05-30 – Dynamic scan-window count + sidebar reference-surface selector

**Dynamic scan-window count.**  `setupTab1Overview()` and `setupTab4DistanceMaps()`
previously pre-created exactly 5 `VTKMeshWidget` instances.  Both functions now only
create the empty `QHBoxLayout` (stored as `m_overviewHBox` / `m_distHBox` in
`MainWindow`).  A new helper `rebuildScanWidgets()` is called from `onLoadFinished()`:
it deletes all existing widgets from both layouts, clears `m_overviewWidgets` /
`m_distWidgets`, then creates exactly N new widgets (one per loaded scan) with
`setMinimumWidth(240)`.  With the existing `Qt::ScrollBarAsNeeded` horizontal policy on
the scroll area, ≤ 5 scans fill the 1 200 px viewport without a scrollbar; 6+ scans
overflow and the scrollbar appears automatically.  Widget titles come from
`scan->scannerName` rather than "Scan N".  Reloading a different number of scans
correctly rebuilds both tabs; the stale growth-only `while` loops in
`updateOverviewTab()` and `updateDistanceMapsTab()` have been removed.

**Sidebar reference-surface selector.**  A new **"Reference Surface"** `QGroupBox` was
added to the left sidebar (visible on all tabs), positioned between "Loaded Scans" and
the status bar.  It contains:
- `QRadioButton* m_refGPARadio` – "GPA mean (all scans)" — default, checked at startup
  and whenever new STL files are loaded.
- `QRadioButton* m_refFixedRadio` – "Fixed reference scan:"
- `QLabel* m_refFixedLabel` — shows the name of the currently selected reference, or
  "(select a scan above)" when none is chosen.

When `m_refFixedRadio` is active, clicking any row in `m_scanList` sets that scan as the
fixed reference **and** updates the fingerprint scatter-plot highlight (dual use).  The
selected scan gets a **★** suffix and bold font in the list (via `refreshScanListMarkers()`,
which updates items in-place using `QSignalBlocker` to prevent recursive signals).  The
existing `m_methodCombo` in the Registration tab is kept in sync: the sidebar drives the
combo, not the other way around.  `int m_fixedRefScanIdx = -1` is the single source of
truth (−1 = GPA mode).  `openSTLFiles()` resets all reference-selection state to GPA
mode when new files are chosen.

### 2026-05-28 – About tab, window title, manuscript PDF

- `MainWindow::setupTab7About()`: new 7th tab with rich-text author credit, clickable
  `www.kunzelmann.de` link, build-stack list, and pipeline summary paragraph.
- `setWindowTitle()` updated to include `Prof. Dr. Karl-Heinz Kunzelmann` alongside the
  application name.
- `docs/manuscript.md` updated: Methods rewritten to include Dijkstra segmentation (Stage 5)
  and crown-restricted ICP (Stage 6); Results updated with crown-restricted metrics tables
  (showing rank reversal: Medit i700 best, FussenS6000 worst at crown level); Discussion
  extended with rank-reversal significance and crown-ICP methodology sections.
- `build/manuscript.pdf` generated via pandoc → HTML → Chromium headless; figures scaled
  to 60% page width; page-break-before each Figure 2–6 via Python HTML post-processing.

### 2026-05-28 – Fingerprint highlight, distance-map scale control, UX fixes

**Scatter-plot single-scanner highlight.**  `ScatterPlotWidget` now supports a highlighted
series via `setHighlightSeries(int idx)` (−1 = all equal).  In highlight mode the selected
series is drawn last (on top) at alpha=200, radius=3 px; all other series are drawn first
at alpha=18, radius=1 px so they remain visible as context but don't compete.  The legend
row of the selected scanner gets a yellow fill and bold text; a "click to highlight" hint
appears below the legend when nothing is selected.  `mousePressEvent` hit-tests against
`m_legendRowRects` (populated by `drawChart` on each paint) to toggle the highlight on
legend clicks.  Clicking the same entry again deselects (→ -1).
`MainWindow::setupUI()` wires `m_scanList::currentRowChanged(int)` to
`m_scatterPlot->setHighlightSeries(int)` so the Loaded-Scans list on the left panel drives
the same highlight.

**Distance-map colour-scale spinbox.**  `setupTab4DistanceMaps()` now adds a horizontal
control bar above the viewport row: a `QDoubleSpinBox m_distScaleSpin` (range 0.05–10 mm,
step 0.05, suffix " mm") and a `⟳ Auto` button.  `bool m_distRangeAuto` (default `true`)
tracks whether the user has taken manual control.  `updateDistanceMapsTab()` auto-computes
`autoMaxDist` as before (H95 capped at 2 mm); when `m_distRangeAuto` is true the spinbox
is updated (via `QSignalBlocker`) to match; when false the spinbox value is used.  The
`⟳ Auto` button sets `m_distRangeAuto = true` and calls `updateDistanceMapsTab()`.  The
spinbox `valueChanged` sets `m_distRangeAuto = false` and refreshes (guarded: only when
`m_gpaReference` is non-null).  Statistics are unaffected — only the VTK LUT range changes.

**Plane disks only show on explicit checkbox check.**  Previously `onPointPicked()` called
`updatePlaneVisualization()` (and thus showed the three disk actors) automatically once ≥ 3
seeds were placed; spinbox changes also auto-updated the disks.  Behaviour changed:
`fitOcclusalPlane()` runs silently on every pick (so the plane data is always fresh for
metrics), but `updatePlaneVisualization()` is only called when `m_showPlanesChk->isChecked()`.
`m_showPlanesChk` default changed from `true` to `false`.  The checkbox `toggled` handler
was updated: on check → if plane active, call `updatePlaneVisualization()` (creates actors);
on uncheck → call `setPlanesVisible(false)`.  The above/below spinbox `valueChanged` lambdas
also guard on `m_showPlanesChk->isChecked()`.

**Keep-segmentation checkbox.**  New `QCheckBox* m_keepSegChk` ("Keep segmentation after
registration", default checked) added in the Tooth Crown Segmentation panel after the
Recompute Registration button.  `updateRegistrationTab()` now calls `runSegmentation()`
after `setOverlayMeshes()` when `!m_pickedPts.empty() && m_keepSegChk->isChecked()`, so
the ivory/grey overlay is always restored.  When unchecked the standard semi-transparent
overlay is shown instead.

**Button re-enable after recompute registration.**  `onAnalysisFinished()` now re-enables
`m_recomputeBtn` when seeds are present, and re-enables `m_reregisterBtn` when seeds are
present AND `m_keepSegChk->isChecked()` AND `m_gpaReference != nullptr`.  This allows
repeated Recompute Registration → Recompute Metrics cycles without manually re-placing seeds.

**Pick button explicit initial state.**  `m_pickBtn->setChecked(false)` added immediately
after `setCheckable(true)` so the button always starts in the "📍 Pick Tooth Seeds"
(unpressed) visual state, regardless of platform or Qt theme.

### 2026-05-28 – Per-scan masks, QSettings, Registration-tab restructure, masked distance maps

**Per-scan segmentation mask fix.**  `MainWindow::recomputeMetrics()` previously computed
one mask on `m_scans[0]` and reused it for all scans.  `DistanceField::fillReport` guards
`toothMask.size() == scan.mesh.num_vertices()` — so the mask was silently rejected for every
scan except the reference, and all metrics were computed on the full mesh.  Fix: call
`ToothSegmentation::segmentFromPoints(*m_scans[i], m_pickedPts, segParams)` per scan inside
the loop.  `updateDistanceMapsTab()` does the same.

**QSettings directory persistence.**  `openSTLFiles()` and `showExportDialog()` now restore
and persist `lastOpenDir` / `lastExportDir` via `QSettings("DentScanCompare","DentScanCompare")`.

**Registration tab restructure.**  Left control panel reorganised into three named sections:
(1) Registration Settings (method, ICP iterations, sample points, occlusal-zone spinbox),
(2) Tooth Crown Segmentation (pick button, seed status, Clear Seeds, parameter spinboxes,
Recompute Metrics, Recompute Registration), (3) Fitted Occlusal Plane (above/below spinboxes,
plane-point label, Show plane disks checkbox).  Panel wrapped in `QScrollArea` (min 200, max
400 px) inside a `QSplitter` so narrow screens can still read all controls.

**Live segmentation parameters.**  Three `QDoubleSpinBox` controls added to the Tooth Crown
Segmentation panel: Max geodesic (3–25 mm, default 12), CEJ crease (10–80°, default 50°),
Min curvature (−10–0 /mm, default −4).  `runSegmentation()` reads these values and re-runs
the Dijkstra pass immediately; re-picking seeds is not required when only adjusting
parameters.

**Camera rotation in pick mode.**  `VTKMeshWidget::eventFilter` was rewritten to detect
click-vs-drag by recording the mouse-press position in `m_pressPos` and comparing against
the release position (< 6 px Manhattan distance = seed pick; otherwise VTK handles rotation
normally).  Both press and release events return `false` (not consumed) so VTK's interactor
still sees them for rotation.

**Recompute Registration (warm-start crown ICP).**  `MainWindow::recomputeRegistration()`
runs via `QtConcurrent::run`; for each scan it calls `ICPRegistration::alignMasked()` with
the per-scan crown mask and a 3 mm correspondence radius (warm start from existing alignment).
After all scans are realigned, `GPAReference::updateMeanMesh()` is called to keep the mean
surface current, then `DistanceField::compute()` rebuilds the per-vertex distances.
Button is enabled only after at least one seed is placed **and** GPA has run at least once.

**Plane visibility checkbox.**  "Show plane disks" `QCheckBox` toggles `VTKMeshWidget::
setPlanesVisible(bool)` on the overlay widget.  `setPlanesVisible` iterates `m_planeActors`
and calls `SetVisibility`.

**Z-window interlock.**  When `runSegmentation()` produces a mask, the "Occlusal zone"
spinbox is disabled (`setEnabled(false)`) so the (less-accurate) Z-window cannot conflict
with the tooth mask in `fillReport`.  `clearPickedPoints()` re-enables it.

**VTK actor separation.**  `m_pickActors` vector renamed/split into `m_sphereActors` (seed
point yellow spheres) and `m_planeActors` (three occlusal-plane disk actors).  `showPickSpheres`
writes to `m_sphereActors`; `showOcclusalPlane` writes to `m_planeActors`.  `clearPickActors`
clears both.  `setPlanesVisible` touches only `m_planeActors`, allowing seeds and plane disks
to be controlled independently.

**Masked distance map rendering.**  `VTKMeshWidget::showDistanceMap(scan, min, max, toothMask)`
overload added.  Crown vertices get their colour via the existing VTK LUT; non-crown vertices
are written as RGBA (55,55,55,255) directly into a `vtkUnsignedCharArray`.  The array is
assigned with `SetColorModeToDirectScalars()` so VTK bypasses the LUT for non-crown cells.
The scalar bar remains visible and correctly calibrated to the crown-vertex colour range.

**Segmentation visibility fix.**  `VTKMeshWidget::showToothSegmentation()` previously wrote
vertex colours to `m_polyData` / `m_actor` but `m_actor` was hidden by `setOverlayMeshes()`
(which calls `m_actor->VisibilityOff()`).  Fix: `showToothSegmentation()` now calls
`clearOverlayActors()` first, which removes the semi-transparent overlay actors and turns
`m_actor` back on.

### 2026-05-28 – Dijkstra tooth-crown segmentation + occlusal-plane UI

- `ToothSegmentation` rewritten: BFS + `maxNormalTiltDeg` criterion replaced by
  multi-source Dijkstra with geodesic distance as the primary stopping criterion.
  Reason: the old 75° normal-tilt test correctly includes incisor labial surfaces
  (~65° from +Z) but fails the palatal/lingual surface (110–120° from +Z), leaving
  anterior crowns half-segmented.  Geodesic distance ≤ 12 mm covers every tooth type
  regardless of orientation.  CEJ kink guard (50°) and curvature floor (−4 mm⁻¹) kept
  as secondary criteria.
- `DistanceField::fillReport`: new signature adds `OcclusalPlane plane` and
  `std::vector<bool> toothMask` parameters with priority-ranked filtering.
- `MainWindow`: Registration tab gains interactive occlusal-plane fitting.
  Operator clicks ≥ 3 points on the mesh surface; least-squares plane is fitted via PCA
  of the covariance matrix (smallest eigenvector = normal).  Asymmetric above/below
  spinboxes (default +2/−12 mm) define the slab.  Plane + slab rendered as three
  oriented disks (grey/green/cyan) in the Registration viewport.
  Simultaneously, clicked points become seeds for `ToothSegmentation::segmentFromPoints`;
  the resulting mask overrides the plane-slab filter in `fillReport` (highest priority).
  Registration tab also gains: method combo (GPA mean vs. fixed scanner), ICP iterations
  spinbox, sample-count spinbox — all wired into `runAnalysis()`.

### 2026-05-28 – Occlusal-zone restriction + documentation
- `DistanceField::fillReport`: added `zWindowMm` parameter; when > 0, only vertices
  within that window below Z_max are included in distance statistics.
- `MainWindow` Registration tab: "Occlusal zone" QDoubleSpinBox (0 = all, recommended
  12 mm); spinbox value passed to `fillReport` at analysis time.
- `docs/interpretation.txt`: full guide to CSV columns and scatter-plot interpretation.
- `docs/manuscript.md`: draft M&M / Results / Discussion for dental journal.

### 2026-05-28 – STL winding fix + GPA mean reference (Option B)
- `STLReader`: per-face winding verified against stored STL normal; inverted triangles
  corrected before polygon soup construction.  Fixes Primescan reversed-normal issue.
- `GPAReference`: after ICP convergence, `updateToMeanMesh()` moves reference vertices
  to centroid of nearest points on all 5 aligned scans → neutral mean surface, all
  scanners show non-zero distances.
- `docs/developer-handoff.md`: initial version created.

### 2026-05-27 – Registration, scatter, overlay, CSV fixes
- ICP misalignment (FussenS6000/iTeroLumina 28 mm off): PCA coarse alignment + 4-
  orientation Z-rotation test; reduced to 0.107–0.127 mm RMS.
- `ScatterPlotWidget`: replaced vtkChartXY (silent data drop in Qt5/VTK9.3) with
  custom QPainter log-log scatter.
- `VTKMeshWidget::setOverlayMeshes`: Registration tab overlay was black; implemented
  and called from `updateRegistrationTab()`.
- CSV UTF-8 BOM added for Windows code-page compatibility.
- Export tab CSV button wired; `MetricsTableWidget::exportToFile()` added.

### 2026-05-26 – Initial working build
- Full pipeline: STL load → curvature → tessellation → GPA → distance field → metrics.
- Qt5/VTK9.3 build environment resolved (Qt6 conflict, MPI target, CGAL 6.0 API).
