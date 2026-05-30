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
   outward face-by-face using centroid-to-centroid distance as edge cost.
   Stopping criteria (all must pass to expand into a neighbour):
   - **Primary**: accumulated geodesic distance ≤ `maxGeodesicMm` (default 12 mm).
     A full clinical crown is ≤ 12 mm surface-path from the cusp tip for every tooth
     type: molars 8–12 mm, premolars 7–10 mm, canines 9–12 mm, incisors 10–13 mm.
     Completely orientation-independent (replaces the old normal-tilt-from-Z BFS which
     failed incisor palatal/lingual surfaces at 110–120° from +Z).
   - **Secondary – crease angle**: normal dot-product between adjacent faces must be
     above `cos(maxCreaseAngleDeg)` (default 50°).  The CEJ always creates a kink.
   - **Secondary – curvature floor**: face mean κ_H must be > `minMeanCurvature`
     (default -4 mm⁻¹).  The gingival sulcus is concave.
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
| Registration | Overlay VTKMeshWidget (all scans semi-transparent), RMS status.  Left panel (QScrollArea) contains three sections: (1) **Registration Settings** — method combo (synced from sidebar), ICP iterations, sample points, occlusal-zone spinbox, Run Analysis button; (2) **Tooth Crown Segmentation** — pick button (📍/🛑, starts unchecked), seed status label, Clear Seeds, Max geodesic / CEJ crease / Min curvature spinboxes, Recompute Metrics, Recompute Registration, Keep-segmentation checkbox; (3) **Fitted Occlusal Plane** — Above/Below spinboxes, plane-point count label, Show plane disks checkbox (starts unchecked). |
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
- Tooth segmentation seed points must be placed manually once per session; they are not
  persisted across restarts.  Automatic seed detection (local Z/κ_H maxima per tooth) would
  eliminate the manual step.  The seed coordinates are world-space (post-PCA) so they would
  need to be stored together with the GPA transform to be reusable across sessions.

---

## Changelog (reverse chronological)

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
