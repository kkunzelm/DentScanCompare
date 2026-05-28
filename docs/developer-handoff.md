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

## GUI layout (6 tabs)

| Tab | Content |
|-----|---------|
| Overview | 5 × VTKMeshWidget with Phong shading |
| Fingerprint | QPainter log-log scatter (triangle area vs. |κ|), ATI table |
| Registration | Overlay VTKMeshWidget (all 5 scans semi-transparent), RMS status |
| Distance Maps | 5 × VTKMeshWidget with diverging colour map (blue–white–red, ±maxDist mm) |
| Metrics | MetricsTableWidget: rows = scanners, cols = metrics, green/red best/worst highlight |
| Export | Directory chooser → fingerprint PNG, distance map PNGs, metrics CSV |

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
  eliminate the manual step.

---

## Changelog (reverse chronological)

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
