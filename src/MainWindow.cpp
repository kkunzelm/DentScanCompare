#include "MainWindow.h"

#include "core/STLReader.h"
#include "core/CurvatureAnalysis.h"
#include "core/TessellationMetrics.h"
#include "core/ICPRegistration.h"
#include "core/GPAReference.h"
#include "core/DistanceField.h"
#include "core/ArchMetrics.h"
#include "core/ToothSegmentation.h"
#include "visualization/VTKMeshWidget.h"
#include "visualization/ScatterPlotWidget.h"
#include "visualization/MetricsTableWidget.h"
#include "export/ReportExporter.h"
#include "export/VTKSceneExporter.h"

#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QScrollArea>
#include <QSplitter>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <Eigen/Eigenvalues>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("DentScanCompare – Dental Scan Quality Analyzer   |   Prof. Dr. Karl-Heinz Kunzelmann");
    resize(1400, 900);
    setupUI();
    setupMenuAndToolbar();

    m_loadWatcher     = new QFutureWatcher<void>(this);
    m_analysisWatcher = new QFutureWatcher<void>(this);
    connect(m_loadWatcher,     &QFutureWatcher<void>::finished,
            this, &MainWindow::onLoadFinished);
    connect(m_analysisWatcher, &QFutureWatcher<void>::finished,
            this, &MainWindow::onAnalysisFinished);
}

// -----------------------------------------------------------------------
// UI setup
// -----------------------------------------------------------------------

void MainWindow::setupUI()
{
    auto* central  = new QWidget(this);
    auto* topLayout = new QHBoxLayout(central);
    setCentralWidget(central);

    // ---- left sidebar ----
    auto* sidebar = new QWidget(central);
    sidebar->setFixedWidth(200);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(4, 4, 4, 4);

    auto* scanGroup = new QGroupBox("Loaded Scans", sidebar);
    auto* sgLayout  = new QVBoxLayout(scanGroup);
    m_scanList = new QListWidget(scanGroup);
    m_scanList->setAlternatingRowColors(true);
    sgLayout->addWidget(m_scanList);

    m_statusBar = new QLabel("Ready.", sidebar);
    m_statusBar->setWordWrap(true);
    m_statusBar->setStyleSheet("font-size: 10px; color: #555;");

    m_progress = new QProgressBar(sidebar);
    m_progress->setRange(0, 100);
    m_progress->hide();

    // Reference Surface selector — visible on all tabs
    auto* refGroup = new QGroupBox("Reference Surface", sidebar);
    auto* refLayout = new QVBoxLayout(refGroup);
    refLayout->setContentsMargins(4, 4, 4, 4);
    refLayout->setSpacing(4);

    m_refGPARadio = new QRadioButton("GPA mean (all scans)", refGroup);
    m_refGPARadio->setChecked(true);
    m_refGPARadio->setToolTip(
        "Iterative Generalized Procrustes Analysis:\n"
        "all scans are aligned to the evolving mean surface.\n"
        "No individual scanner is privileged as the reference.");

    m_refFixedRadio = new QRadioButton("Fixed reference scan:", refGroup);
    m_refFixedRadio->setToolTip(
        "One scan is held fixed as the reference;\n"
        "all others are aligned to it.\n"
        "Highlight the desired scan in the list above,\n"
        "then select this option (or select it first,\n"
        "then click the desired scan).");

    m_refFixedLabel = new QLabel("(select a scan above)", refGroup);
    m_refFixedLabel->setStyleSheet("font-size: 10px; color: #666; margin-left: 14px;");
    m_refFixedLabel->setWordWrap(true);

    refLayout->addWidget(m_refGPARadio);
    refLayout->addWidget(m_refFixedRadio);
    refLayout->addWidget(m_refFixedLabel);

    sideLayout->addWidget(scanGroup);
    sideLayout->addWidget(refGroup);
    sideLayout->addWidget(m_statusBar);
    sideLayout->addWidget(m_progress);
    sideLayout->addStretch();

    // ---- tab widget ----
    m_tabs = new QTabWidget(central);
    m_tabs->setDocumentMode(true);

    setupTab1Overview();
    setupTab2Fingerprint();
    setupTab3Registration();
    setupTab4DistanceMaps();
    setupTab5Metrics();
    setupTab6Export();
    setupTab7About();

    topLayout->addWidget(sidebar);
    topLayout->addWidget(m_tabs, 1);

    // Wire scanner list → scatter-plot highlight AND (in fixed-ref mode) reference selection.
    connect(m_scanList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_scatterPlot) m_scatterPlot->setHighlightSeries(row);
        if (m_refFixedRadio && m_refFixedRadio->isChecked()
                && row >= 0 && row < static_cast<int>(m_scans.size())) {
            m_fixedRefScanIdx = row;
            m_refFixedLabel->setText(QString::fromStdString(m_scans[row]->scannerName));
            if (m_methodCombo && row + 1 < m_methodCombo->count())
                m_methodCombo->setCurrentIndex(row + 1);
            refreshScanListMarkers();
        }
    });

    // GPA-mean radio: reset fixed reference
    connect(m_refGPARadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        m_fixedRefScanIdx = -1;
        m_refFixedLabel->setText("(select a scan above)");
        if (m_methodCombo) m_methodCombo->setCurrentIndex(0);
        refreshScanListMarkers();
    });

    // Fixed-reference radio: adopt currently highlighted scan (if any)
    connect(m_refFixedRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) return;
        const int row = m_scanList->currentRow();
        if (row >= 0 && row < static_cast<int>(m_scans.size())) {
            m_fixedRefScanIdx = row;
            m_refFixedLabel->setText(QString::fromStdString(m_scans[row]->scannerName));
            if (m_methodCombo && row + 1 < m_methodCombo->count())
                m_methodCombo->setCurrentIndex(row + 1);
        } else {
            m_fixedRefScanIdx = -1;
            m_refFixedLabel->setText("(select a scan above)");
        }
        refreshScanListMarkers();
    });
}

void MainWindow::setupMenuAndToolbar()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* actOpen  = fileMenu->addAction("&Open STL files…");
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::openSTLFiles);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", qApp, &QApplication::quit, QKeySequence::Quit);

    auto* analysisMenu = menuBar()->addMenu("&Analysis");
    auto* actRun = analysisMenu->addAction("&Run Full Analysis");
    actRun->setShortcut(Qt::Key_F5);
    connect(actRun, &QAction::triggered, this, &MainWindow::runAnalysis);

    auto* exportMenu = menuBar()->addMenu("&Export");
    auto* actExport  = exportMenu->addAction("&Export…");
    connect(actExport, &QAction::triggered, this, &MainWindow::showExportDialog);

    // toolbar
    auto* tb = addToolBar("Main");
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* tbOpen    = tb->addAction("Open STL", this, &MainWindow::openSTLFiles);
    auto* tbAnalyse = tb->addAction("▶  Run Analysis", this, &MainWindow::runAnalysis);
    auto* tbExport  = tb->addAction("Export…", this, &MainWindow::showExportDialog);
    (void)tbOpen; (void)tbAnalyse; (void)tbExport;
}

void MainWindow::setupTab1Overview()
{
    m_tab1 = new QWidget;
    auto* layout = new QVBoxLayout(m_tab1);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* infoLabel = new QLabel(
        "Overview: all loaded scans with Phong shading. "
        "Click and drag to rotate, scroll to zoom.", m_tab1);
    infoLabel->setStyleSheet("font-size: 10px; color: #444; padding: 2px;");
    layout->addWidget(infoLabel);

    auto* scroll = new QScrollArea(m_tab1);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* container = new QWidget;
    m_overviewHBox = new QHBoxLayout(container);
    m_overviewHBox->setSpacing(4);
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    m_tabs->addTab(m_tab1, "Overview");
}

void MainWindow::setupTab2Fingerprint()
{
    auto* tab2 = new QWidget;
    auto* vlay = new QVBoxLayout(tab2);
    vlay->setContentsMargins(4, 4, 4, 4);

    auto* infoLabel = new QLabel(
        "Tessellation Fingerprint: triangle area vs. |mean curvature| per scanner. "
        "An adaptive scanner concentrates small triangles where curvature is high (upper-left cluster).", tab2);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("font-size: 10px; color: #444; padding: 2px;");
    vlay->addWidget(infoLabel);

    m_scatterPlot = new ScatterPlotWidget(tab2);
    vlay->addWidget(m_scatterPlot, 1);

    m_atiLabel = new QLabel("Run analysis to compute ATI values.", tab2);
    m_atiLabel->setStyleSheet("font-size: 10px; padding: 4px; background: #f0f0f0;");
    vlay->addWidget(m_atiLabel);

    m_tabs->addTab(tab2, "Fingerprint");
}

void MainWindow::setupTab3Registration()
{
    m_tab3 = new QWidget;
    auto* hlay = new QHBoxLayout(m_tab3);
    hlay->setContentsMargins(4, 4, 4, 4);
    hlay->setSpacing(0);

    // left: controls inside a scroll area so the panel is resizable and
    // all text remains readable even in narrow windows
    auto* ctrlPanel = new QGroupBox("Registration Settings");
    ctrlPanel->setMinimumWidth(180);
    auto* ctrlLayout = new QFormLayout(ctrlPanel);
    ctrlLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto* ctrlScroll = new QScrollArea(m_tab3);
    ctrlScroll->setWidget(ctrlPanel);
    ctrlScroll->setWidgetResizable(true);
    ctrlScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ctrlScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ctrlScroll->setMinimumWidth(200);
    ctrlScroll->setMaximumWidth(400);

    m_methodCombo = new QComboBox(ctrlPanel);
    m_methodCombo->addItem("GPA – mean reference (recommended)");
    m_methodCombo->setToolTip(
        "GPA: aligns all scans iteratively and reports distances to the\n"
        "mean surface.  No scanner is privileged as the reference.\n\n"
        "Fixed ref: one scanner is held fixed; all others are aligned to it.\n"
        "Load scans first — the combo is updated with the available names.");
    ctrlLayout->addRow("Method:", m_methodCombo);

    m_maxIterSpin = new QSpinBox(ctrlPanel);
    m_maxIterSpin->setRange(1, 500);
    m_maxIterSpin->setValue(100);
    m_maxIterSpin->setToolTip("Maximum ICP iterations per scan per GPA cycle.");
    ctrlLayout->addRow("ICP iterations:", m_maxIterSpin);

    m_sampleSpin = new QSpinBox(ctrlPanel);
    m_sampleSpin->setRange(1000, 100000);
    m_sampleSpin->setSingleStep(1000);
    m_sampleSpin->setValue(15000);
    m_sampleSpin->setToolTip(
        "Number of surface points sampled per ICP iteration.\n"
        "More points → more accurate but slower per iteration.");
    ctrlLayout->addRow("ICP sample pts:", m_sampleSpin);

    // Occlusal zone restriction
    m_zWindowSpin = new QDoubleSpinBox(ctrlPanel);
    m_zWindowSpin->setRange(0.0, 30.0);
    m_zWindowSpin->setSingleStep(1.0);
    m_zWindowSpin->setValue(0.0);
    m_zWindowSpin->setDecimals(1);
    m_zWindowSpin->setSuffix(" mm");
    m_zWindowSpin->setSpecialValueText("All (gingiva incl.)");
    m_zWindowSpin->setToolTip(
        "Restrict distance metrics to the top N mm of the aligned scan.\n"
        "After PCA, Z+ points toward the occlusal surface; this window\n"
        "captures tooth crowns and excludes gingiva.\n"
        "Recommended: 12 mm.  0 = use all vertices.");
    ctrlLayout->addRow("Occlusal zone:", m_zWindowSpin);

    auto* runBtn = new QPushButton("▶  Run Registration", ctrlPanel);
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    ctrlLayout->addRow(runBtn);

    // ── Section separator ─────────────────────────────────────────────────
    auto* sepLine = new QFrame(ctrlPanel);
    sepLine->setFrameShape(QFrame::HLine);
    sepLine->setFrameShadow(QFrame::Sunken);
    ctrlLayout->addRow(sepLine);

    // ── Tooth Crown Segmentation (primary ROI tool) ───────────────────────
    auto* segHeading = new QLabel("<b>Tooth Crown Segmentation</b>", ctrlPanel);
    ctrlLayout->addRow(segHeading);

    auto* segInfo = new QLabel(
        "Click once on each tooth crown\n"
        "(cusp tip or incisal edge).\n"
        "The region-growing algorithm\n"
        "isolates the full crown automatically.",
        ctrlPanel);
    segInfo->setStyleSheet("color:#555; font-size:10px;");
    segInfo->setWordWrap(true);
    ctrlLayout->addRow(segInfo);

    m_pickBtn = new QPushButton("📍 Pick Tooth Seeds", ctrlPanel);
    m_pickBtn->setCheckable(true);
    m_pickBtn->setChecked(false);
    m_pickBtn->setToolTip(
        "Enable pick mode, then left-click once on each tooth crown\n"
        "in the overlay viewport.\n"
        "One click per tooth is enough — place it on the occlusal/\n"
        "incisal surface (cusp tip, central fossa, or incisal edge).\n"
        "Do NOT click on gingiva.");
    ctrlLayout->addRow(m_pickBtn);

    m_segStatusLabel = new QLabel("No seeds placed.", ctrlPanel);
    m_segStatusLabel->setWordWrap(true);
    m_segStatusLabel->setStyleSheet("color:#555; font-size:10px;");
    ctrlLayout->addRow(m_segStatusLabel);

    m_undoSeedBtn = new QPushButton("Undo Last Seed", ctrlPanel);
    m_undoSeedBtn->setEnabled(false);
    m_undoSeedBtn->setToolTip(
        "Remove the most recently placed seed point and re-run segmentation.\n"
        "Can be pressed repeatedly to undo multiple seeds in reverse order.");
    ctrlLayout->addRow(m_undoSeedBtn);

    m_clearPickBtn = new QPushButton("Clear All Seeds", ctrlPanel);
    m_clearPickBtn->setToolTip("Remove all seed points, erase zones, and reset the segmentation.");
    ctrlLayout->addRow(m_clearPickBtn);

    // ── Gingiva Eraser ────────────────────────────────────────────────────
    auto* eraseInfo = new QLabel(
        "If the segmentation bleeds onto the\n"
        "gingiva, enable Erase mode and\n"
        "click on the incorrectly included\n"
        "area to paint it out.",
        ctrlPanel);
    eraseInfo->setStyleSheet("color:#555; font-size:10px;");
    eraseInfo->setWordWrap(true);
    ctrlLayout->addRow(eraseInfo);

    m_eraseBtn = new QPushButton("Erase Gingiva", ctrlPanel);
    m_eraseBtn->setCheckable(true);
    m_eraseBtn->setChecked(false);
    m_eraseBtn->setToolTip(
        "Enable erase mode, then left-click on any incorrectly\n"
        "segmented gingival area in the overlay viewport.\n"
        "Each click removes all vertices within the brush radius\n"
        "from the tooth mask.  Multiple clicks accumulate.\n"
        "Erase zones persist when segmentation parameters change.");
    ctrlLayout->addRow(m_eraseBtn);

    m_eraseBrushSpin = new QDoubleSpinBox(ctrlPanel);
    m_eraseBrushSpin->setRange(0.5, 10.0);
    m_eraseBrushSpin->setSingleStep(0.5);
    m_eraseBrushSpin->setValue(2.0);
    m_eraseBrushSpin->setSuffix(" mm");
    m_eraseBrushSpin->setToolTip(
        "Radius of the erase brush in world-space millimetres.\n"
        "All tooth-mask vertices within this radius of the clicked\n"
        "point are removed from the segmentation.\n"
        "Increase for large gingival bleeds; decrease for fine edits.");
    ctrlLayout->addRow("Brush radius:", m_eraseBrushSpin);

    auto* clearEraseBtn = new QPushButton("Clear Erase Zones", ctrlPanel);
    clearEraseBtn->setToolTip("Remove all erase zones and restore the full Dijkstra mask.");
    ctrlLayout->addRow(clearEraseBtn);

    // ── Segmentation file I/O ─────────────────────────────────────────────
    auto* sepSegFile = new QFrame(ctrlPanel);
    sepSegFile->setFrameShape(QFrame::HLine);
    sepSegFile->setFrameShadow(QFrame::Sunken);
    ctrlLayout->addRow(sepSegFile);

    auto* segFileHeading = new QLabel("<b>Segmentation File</b>", ctrlPanel);
    ctrlLayout->addRow(segFileHeading);

    auto* segFileInfo = new QLabel(
        "Save seeds, erase zones, and\n"
        "parameters to a .dsc_seg file.\n"
        "The file records the reference\n"
        "surface name for identification.",
        ctrlPanel);
    segFileInfo->setStyleSheet("color:#555; font-size:10px;");
    segFileInfo->setWordWrap(true);
    ctrlLayout->addRow(segFileInfo);

    m_saveSegBtn = new QPushButton("Save Segmentation…", ctrlPanel);
    m_saveSegBtn->setEnabled(false);
    m_saveSegBtn->setToolTip(
        "Save the current seeds, erase zones, and parameters to a .dsc_seg file.\n"
        "The file is named after the reference surface by default.\n"
        "Load it later with 'Load Segmentation' to restore the setup.");
    ctrlLayout->addRow(m_saveSegBtn);

    m_loadSegBtn = new QPushButton("Load Segmentation…", ctrlPanel);
    m_loadSegBtn->setToolTip(
        "Load a previously saved .dsc_seg file.\n"
        "Seeds, erase zones, and parameters are restored and\n"
        "segmentation is re-run immediately on the current scans.");
    ctrlLayout->addRow(m_loadSegBtn);

    // ── Segmentation parameters (tunable live) ───────────────────────────
    m_segGeodesicSpin = new QDoubleSpinBox(ctrlPanel);
    m_segGeodesicSpin->setRange(3.0, 25.0);
    m_segGeodesicSpin->setSingleStep(0.5);
    m_segGeodesicSpin->setValue(12.0);
    m_segGeodesicSpin->setSuffix(" mm");
    m_segGeodesicSpin->setToolTip(
        "Maximum surface-path (geodesic) distance from the seed to any\n"
        "included crown vertex.\n"
        "Decrease if the segmentation overshoots onto adjacent teeth or\n"
        "gingiva.  Increase if parts of the crown are missing.\n"
        "Typical range: molars 10–13 mm, incisors 8–11 mm.");
    ctrlLayout->addRow("Max geodesic:", m_segGeodesicSpin);

    m_segCreaseSpin = new QDoubleSpinBox(ctrlPanel);
    m_segCreaseSpin->setRange(10.0, 80.0);
    m_segCreaseSpin->setSingleStep(5.0);
    m_segCreaseSpin->setValue(50.0);
    m_segCreaseSpin->setSuffix(" °");
    m_segCreaseSpin->setToolTip(
        "Maximum allowed crease angle between adjacent faces.\n"
        "The cementoenamel junction (CEJ) creates a sharp kink;\n"
        "expansion stops when the angle between neighbour normals\n"
        "exceeds this threshold.\n"
        "Decrease (e.g. 35°) to stop earlier at the CEJ.\n"
        "Increase (e.g. 65°) if the crown surface has local sharp ridges.");
    ctrlLayout->addRow("CEJ crease:", m_segCreaseSpin);

    m_segCurvSpin = new QDoubleSpinBox(ctrlPanel);
    m_segCurvSpin->setRange(-10.0, 0.0);
    m_segCurvSpin->setSingleStep(0.5);
    m_segCurvSpin->setValue(-4.0);
    m_segCurvSpin->setSuffix(" /mm");
    m_segCurvSpin->setToolTip(
        "Minimum mean curvature κ_H a face must have to be included.\n"
        "The gingival sulcus is concave (κ_H strongly negative);\n"
        "expansion stops when the face curvature falls below this floor.\n"
        "Increase toward 0 (e.g. −2) to stop earlier at concavities.\n"
        "Decrease (e.g. −6) if shallow concavities on the crown are\n"
        "being excluded.  Requires curvature computation to have run.");
    ctrlLayout->addRow("Min curvature:", m_segCurvSpin);

    m_recomputeBtn = new QPushButton("⟳  Recompute Metrics", ctrlPanel);
    m_recomputeBtn->setToolTip(
        "Re-run distance statistics using the current region of interest.\n"
        "Does NOT re-run ICP registration (fast).");
    m_recomputeBtn->setEnabled(false);
    ctrlLayout->addRow(m_recomputeBtn);

    m_reregisterBtn = new QPushButton("⟳  Recompute Registration", ctrlPanel);
    m_reregisterBtn->setToolTip(
        "Re-run point-to-plane ICP for each scan starting from the current\n"
        "alignment (warm start), using only tooth-crown vertices.\n"
        "This refines the registration by excluding gingival tissue and\n"
        "scan margins that introduce noise.\n"
        "Runs in background — updates Registration, Distance Maps and Metrics.");
    m_reregisterBtn->setEnabled(false);
    ctrlLayout->addRow(m_reregisterBtn);

    m_keepSegChk = new QCheckBox("Keep segmentation after registration", ctrlPanel);
    m_keepSegChk->setChecked(true);
    m_keepSegChk->setToolTip(
        "When checked, the segmentation colour overlay is restored in the\n"
        "Registration viewport after running or recomputing registration.\n"
        "Uncheck to see the semi-transparent multi-scan overlay instead.");
    ctrlLayout->addRow(m_keepSegChk);

    // ── Occlusal Plane (fallback when no seeds are placed) ────────────────
    auto* sepLine2 = new QFrame(ctrlPanel);
    sepLine2->setFrameShape(QFrame::HLine);
    sepLine2->setFrameShadow(QFrame::Sunken);
    ctrlLayout->addRow(sepLine2);

    auto* planeHeading = new QLabel("<b>Occlusal Plane</b> (fallback)", ctrlPanel);
    planeHeading->setToolTip(
        "When seed points are placed, the tooth segmentation above takes\n"
        "priority and these plane controls are ignored.\n"
        "When no seeds are placed, the plane slab is used as a coarser\n"
        "region-of-interest filter.\n"
        "The plane is fitted automatically through the seed points as\n"
        "soon as 3 or more are placed.");
    ctrlLayout->addRow(planeHeading);

    m_planeAboveSpin = new QDoubleSpinBox(ctrlPanel);
    m_planeAboveSpin->setRange(0.0, 10.0);
    m_planeAboveSpin->setSingleStep(0.5);
    m_planeAboveSpin->setValue(2.0);
    m_planeAboveSpin->setSuffix(" mm");
    m_planeAboveSpin->setToolTip("Include surface up to this far above the fitted plane.");
    ctrlLayout->addRow("Above plane:", m_planeAboveSpin);

    m_planeBelowSpin = new QDoubleSpinBox(ctrlPanel);
    m_planeBelowSpin->setRange(0.0, 20.0);
    m_planeBelowSpin->setSingleStep(0.5);
    m_planeBelowSpin->setValue(12.0);
    m_planeBelowSpin->setSuffix(" mm");
    m_planeBelowSpin->setToolTip("Include surface up to this far below the fitted plane (full crown height).");
    ctrlLayout->addRow("Below plane:", m_planeBelowSpin);

    m_pickCountLabel = new QLabel("Plane: not fitted yet.", ctrlPanel);
    m_pickCountLabel->setStyleSheet("color:#555; font-size:10px;");
    m_pickCountLabel->setWordWrap(true);
    ctrlLayout->addRow(m_pickCountLabel);

    m_showPlanesChk = new QCheckBox("Show plane disks", ctrlPanel);
    m_showPlanesChk->setChecked(false);
    m_showPlanesChk->setToolTip(
        "Check to compute and display the three semi-transparent occlusal plane\n"
        "disks (grey = plane, green = above, cyan = below).\n"
        "The plane is fitted through the seed points when 3 or more are placed.\n"
        "Uncheck to hide the disks without removing the seed spheres.");
    ctrlLayout->addRow(m_showPlanesChk);

    // ── wire picking / erasing signals ───────────────────────────────────
    connect(m_pickBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_overlayWidget) m_overlayWidget->setPickMode(on);
        m_pickBtn->setText(on ? "🛑 Stop Picking" : "📍 Pick Tooth Seeds");
        if (on && m_eraseBtn && m_eraseBtn->isChecked())
            m_eraseBtn->setChecked(false);
    });
    connect(m_eraseBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_overlayWidget) m_overlayWidget->setPickMode(on);  // reuse pick mode for click routing
        m_eraseBtn->setText(on ? "Stop Erasing" : "Erase Gingiva");
        if (on && m_pickBtn && m_pickBtn->isChecked())
            m_pickBtn->setChecked(false);
    });
    connect(clearEraseBtn, &QPushButton::clicked, this, [this]() {
        m_eraseZones.clear();
        if (!m_toothMask.empty() && !m_scans.empty()) {
            auto refIt = std::max_element(m_scans.begin(), m_scans.end(),
                [](const auto& a, const auto& b){
                    return a->triangleCount < b->triangleCount; });
            const std::size_t nTooth = std::count(m_toothMask.begin(), m_toothMask.end(), true);
            if (m_segStatusLabel)
                m_segStatusLabel->setText(
                    QString("<span style='color:green;'>Active: %1 seed%2, ~%3 tooth vertices</span><br>"
                            "<span style='color:#555;'>Overlay: ivory=crown, grey=gingiva</span>")
                        .arg(static_cast<int>(m_pickedPts.size()))
                        .arg(m_pickedPts.size() == 1 ? "" : "s")
                        .arg(nTooth));
            if (m_overlayWidget)
                m_overlayWidget->showToothSegmentation(*refIt, m_toothMask);
            updateDistanceMapsTab();
        }
    });
    connect(m_undoSeedBtn, &QPushButton::clicked, this, [this]() {
        if (m_pickedPts.empty()) return;
        m_pickedPts.pop_back();
        const int n = static_cast<int>(m_pickedPts.size());
        if (n == 0) {
            clearPickedPoints();
            return;
        }
        if (m_overlayWidget) m_overlayWidget->showPickSpheres(m_pickedPts);
        if (n >= 3) {
            fitOcclusalPlane();
            if (m_showPlanesChk && m_showPlanesChk->isChecked())
                updatePlaneVisualization();
        } else {
            m_occlusalPlane.active = false;
            if (m_overlayWidget && m_showPlanesChk && m_showPlanesChk->isChecked())
                m_overlayWidget->setPlanesVisible(false);
        }
        runSegmentation();
        m_undoSeedBtn->setEnabled(n > 0);
        m_recomputeBtn->setEnabled(!m_scans.empty());
        if (m_reregisterBtn) m_reregisterBtn->setEnabled(!m_scans.empty() && m_gpaReference != nullptr);
    });
    connect(m_clearPickBtn, &QPushButton::clicked,
            this, &MainWindow::clearPickedPoints);
    connect(m_saveSegBtn, &QPushButton::clicked, this, &MainWindow::saveSegmentation);
    connect(m_loadSegBtn, &QPushButton::clicked, this, &MainWindow::loadSegmentation);
    connect(m_recomputeBtn,   &QPushButton::clicked,
            this, &MainWindow::recomputeMetrics);
    connect(m_reregisterBtn,  &QPushButton::clicked,
            this, &MainWindow::recomputeRegistration);
    connect(m_showPlanesChk,  &QCheckBox::toggled, this, [this](bool on) {
        if (!m_overlayWidget) return;
        if (on && m_occlusalPlane.active)
            updatePlaneVisualization();   // create actors now if not yet drawn
        else
            m_overlayWidget->setPlanesVisible(false);
    });
    connect(m_planeAboveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){
                if (m_showPlanesChk && m_showPlanesChk->isChecked()) updatePlaneVisualization();
            });
    connect(m_planeBelowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){
                if (m_showPlanesChk && m_showPlanesChk->isChecked()) updatePlaneVisualization();
            });
    // Re-run segmentation live when any parameter spinbox changes
    auto reSegment = [this](double){ if (!m_pickedPts.empty()) runSegmentation(); };
    connect(m_segGeodesicSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, reSegment);
    connect(m_segCreaseSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, reSegment);
    connect(m_segCurvSpin,     QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, reSegment);

    m_registrationStatus = new QLabel("Not yet run.", ctrlPanel);
    m_registrationStatus->setWordWrap(true);
    ctrlLayout->addRow(m_registrationStatus);

    // right: overlay view
    m_overlayWidget = new VTKMeshWidget(m_tab3);
    m_overlayWidget->setTitle("Overlay – enable 'Pick Tooth Seeds' then left-click one cusp per tooth");
    connect(m_overlayWidget, &VTKMeshWidget::pointPicked,
            this, &MainWindow::onPointPicked);

    auto* splitter = new QSplitter(Qt::Horizontal, m_tab3);
    splitter->addWidget(ctrlScroll);
    splitter->addWidget(m_overlayWidget);
    splitter->setStretchFactor(0, 0); // control panel: don't stretch by default
    splitter->setStretchFactor(1, 1); // overlay: takes all extra space
    splitter->setSizes({240, 800});
    hlay->addWidget(splitter);

    m_tabs->addTab(m_tab3, "Registration");
}

void MainWindow::setupTab4DistanceMaps()
{
    m_tab4 = new QWidget;
    auto* vlay = new QVBoxLayout(m_tab4);
    vlay->setContentsMargins(4, 4, 4, 4);

    auto* infoLabel = new QLabel(
        "Distance Maps: signed distance from each scan to the GPA reference. "
        "Blue = below reference (−), red = above reference (+).", m_tab4);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("font-size: 10px; color: #444; padding: 2px;");
    vlay->addWidget(infoLabel);

    // ── colour-scale control bar ─────────────────────────────────────────
    auto* ctrlBar = new QWidget(m_tab4);
    auto* ctrlRow = new QHBoxLayout(ctrlBar);
    ctrlRow->setContentsMargins(0, 0, 0, 0);
    ctrlRow->setSpacing(6);

    ctrlRow->addWidget(new QLabel("Colour scale: ±", ctrlBar));
    m_distScaleSpin = new QDoubleSpinBox(ctrlBar);
    m_distScaleSpin->setRange(0.05, 10.0);
    m_distScaleSpin->setSingleStep(0.05);
    m_distScaleSpin->setDecimals(2);
    m_distScaleSpin->setSuffix(" mm");
    m_distScaleSpin->setValue(1.0);
    m_distScaleSpin->setFixedWidth(110);
    m_distScaleSpin->setToolTip(
        "Clipping range for the false-colour distance maps.\n"
        "Distances outside ±value are shown as solid red/blue.\n"
        "This is a visual-only control — statistics are not affected.");
    ctrlRow->addWidget(m_distScaleSpin);

    auto* autoBtn = new QPushButton("⟳ Auto", ctrlBar);
    autoBtn->setFixedWidth(80);
    autoBtn->setToolTip(
        "Reset to the automatic range (±H95 of the worst scanner, max 2 mm).");
    ctrlRow->addWidget(autoBtn);
    ctrlRow->addStretch();
    vlay->addWidget(ctrlBar);

    connect(m_distScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                m_distRangeAuto = false;
                if (m_gpaReference) updateDistanceMapsTab();
            });
    connect(autoBtn, &QPushButton::clicked, this, [this] {
        m_distRangeAuto = true;
        if (m_gpaReference) updateDistanceMapsTab();
    });

    // ── viewport scroll area ──────────────────────────────────────────────
    auto* scroll = new QScrollArea(m_tab4);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* container = new QWidget;
    m_distHBox = new QHBoxLayout(container);
    m_distHBox->setSpacing(4);
    scroll->setWidget(container);
    vlay->addWidget(scroll, 1);

    m_tabs->addTab(m_tab4, "Distance Maps");
}

void MainWindow::setupTab5Metrics()
{
    auto* tab5 = new QWidget;
    auto* vlay = new QVBoxLayout(tab5);
    vlay->setContentsMargins(4, 4, 4, 4);

    auto* infoLabel = new QLabel(
        "All metrics for all scanners. Green = best value, red = worst value in column. "
        "Export to CSV for publication tables.", tab5);
    infoLabel->setStyleSheet("font-size: 10px; color: #444; padding: 2px;");
    vlay->addWidget(infoLabel);

    m_metricsTable = new MetricsTableWidget(tab5);
    vlay->addWidget(m_metricsTable, 1);

    m_tabs->addTab(tab5, "Metrics");
}

void MainWindow::setupTab6Export()
{
    auto* tab6 = new QWidget;
    auto* vlay = new QVBoxLayout(tab6);
    vlay->setContentsMargins(12, 12, 12, 12);

    auto* group = new QGroupBox("Export Publication Figures", tab6);
    auto* form  = new QFormLayout(group);

    auto* dpiCombo = new QComboBox(group);
    dpiCombo->addItems({"150 dpi (screen)", "300 dpi (print)", "600 dpi (high-res)"});
    dpiCombo->setCurrentIndex(1);
    form->addRow("Resolution:", dpiCombo);

    auto* btnOverview  = new QPushButton("Export Overview (PNG)", group);
    auto* btnFingerprint = new QPushButton("Export Fingerprint (PNG + SVG)", group);
    auto* btnDistMaps  = new QPushButton("Export Distance Maps (PNG)", group);
    auto* btnMetrics   = new QPushButton("Export Metrics (CSV)", group);

    connect(btnOverview,    &QPushButton::clicked, this, &MainWindow::showExportDialog);
    connect(btnFingerprint, &QPushButton::clicked, this, &MainWindow::showExportDialog);
    connect(btnDistMaps,    &QPushButton::clicked, this, &MainWindow::showExportDialog);
    connect(btnMetrics,     &QPushButton::clicked, this, &MainWindow::showExportDialog);

    form->addRow(btnOverview);
    form->addRow(btnFingerprint);
    form->addRow(btnDistMaps);
    form->addRow(btnMetrics);

    vlay->addWidget(group);
    vlay->addStretch();

    m_tabs->addTab(tab6, "Export");
}

void MainWindow::setupTab7About()
{
    auto* tab7 = new QWidget;
    auto* vlay = new QVBoxLayout(tab7);
    vlay->setContentsMargins(40, 40, 40, 40);
    vlay->setSpacing(12);

    auto makeLabel = [&](const QString& html, bool center = true) {
        auto* l = new QLabel(html, tab7);
        l->setTextFormat(Qt::RichText);
        l->setOpenExternalLinks(true);
        l->setWordWrap(true);
        if (center) l->setAlignment(Qt::AlignHCenter);
        return l;
    };

    vlay->addStretch(1);

    vlay->addWidget(makeLabel(
        "<span style='font-size:22pt; font-weight:bold;'>DentScanCompare</span>"));

    vlay->addWidget(makeLabel(
        "<span style='font-size:12pt; color:#555;'>"
        "Dental Intraoral Scanner Quality Analyzer</span>"));

    vlay->addSpacing(20);

    vlay->addWidget(makeLabel(
        "<span style='font-size:14pt; font-weight:bold;'>"
        "Prof. Dr. Karl-Heinz Kunzelmann</span>"));

    vlay->addWidget(makeLabel(
        "<span style='font-size:11pt;'>"
        "<a href='https://www.kunzelmann.de'>www.kunzelmann.de</a></span>"));

    vlay->addSpacing(20);

    vlay->addWidget(makeLabel(
        "<span style='font-size:9pt; color:#666;'>"
        "Built with Qt 5.15 · VTK 9.3 · CGAL 6.0 · Eigen 3.4 · nanoflann 1.7</span>"));

    vlay->addWidget(makeLabel(
        "<span style='font-size:9pt; color:#666;'>"
        "C++20 · GPL v2 or later</span>"));

    vlay->addSpacing(16);

    auto* sep = new QFrame(tab7);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setMaximumWidth(500);
    auto* sepHlay = new QHBoxLayout;
    sepHlay->addStretch(); sepHlay->addWidget(sep); sepHlay->addStretch();
    vlay->addLayout(sepHlay);

    vlay->addSpacing(8);

    vlay->addWidget(makeLabel(
        "<span style='font-size:9pt; color:#555;'>"
        "Analysis pipeline: PCA coarse alignment · 4-orientation Z-rotation test · "
        "GPA with true mean-reference update · Dijkstra tooth-crown segmentation · "
        "Crown-restricted ICP refinement · CGAL AABB signed distance field · "
        "Tessellation fingerprint (ATI)</span>",
        false));

    vlay->addStretch(2);

    m_tabs->addTab(tab7, "About");
}

// -----------------------------------------------------------------------
// Slots
// -----------------------------------------------------------------------

void MainWindow::openSTLFiles()
{
    QSettings s("DentScanCompare", "DentScanCompare");
    const QString lastOpenDir = s.value("lastOpenDir", QDir::homePath()).toString();

    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open STL files",
        lastOpenDir,
        "STL files (*.stl *.STL);;All files (*)");
    if (paths.isEmpty()) return;
    s.setValue("lastOpenDir", QFileInfo(paths.first()).absolutePath());

    m_scans.clear();
    m_gpaReference.reset();
    m_reports.clear();
    m_scanList->clear();
    m_fixedRefScanIdx = -1;
    if (m_refGPARadio)   m_refGPARadio->setChecked(true);
    if (m_refFixedLabel) m_refFixedLabel->setText("(select a scan above)");
    if (m_methodCombo)   m_methodCombo->setCurrentIndex(0);

    setStatus(QString("Loading %1 file(s)…").arg(paths.size()));
    m_progress->setValue(0);
    m_progress->show();

    // load in background
    auto future = QtConcurrent::run([this, paths]() {
        int loaded = 0;
        for (const auto& p : paths) {
            std::string err;
            auto scan = STLReader::read(p.toStdString(), err);
            if (scan) {
                QMetaObject::invokeMethod(this, [this, scan, &loaded, &paths]() {
                    m_scans.push_back(scan);
                    m_progress->setValue(
                        static_cast<int>(100 * m_scans.size() / paths.size()));
                }, Qt::QueuedConnection);
            }
            ++loaded;
        }
    });
    m_loadWatcher->setFuture(future);
}

void MainWindow::onLoadFinished()
{
    m_progress->hide();
    setStatus(QString("%1 scan(s) loaded. Click 'Run Analysis' to proceed.")
              .arg(m_scans.size()));
    rebuildScanWidgets();
    updateScannerList();
    updateMethodCombo();
    updateOverviewTab();
}

void MainWindow::runAnalysis()
{
    if (m_scans.empty()) {
        QMessageBox::information(this, "No scans",
            "Please load STL files first (File → Open STL files).");
        return;
    }
    setStatus("Running analysis…");
    m_progress->setValue(0);
    m_progress->show();

    auto future = QtConcurrent::run([this]() {
        // Step 1: curvature (parallel over scans)
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Computing curvature…"); m_progress->setValue(10); }, Qt::QueuedConnection);
        for (auto& scan : m_scans)
            CurvatureAnalysis::compute(*scan);

        // Step 2: tessellation metrics
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Computing tessellation metrics…"); m_progress->setValue(25); }, Qt::QueuedConnection);
        m_reports.resize(m_scans.size());
        for (std::size_t i = 0; i < m_scans.size(); ++i) {
            TessellationMetrics::compute(*m_scans[i]);
            TessellationMetrics::fillReport(*m_scans[i], m_reports[i]);
        }

        // Step 3: registration – read settings from UI widgets
        // (widgets live on the GUI thread; capture values before the lambda)
        const int    methodIdx  = m_methodCombo  ? m_methodCombo->currentIndex()  : 0;
        const int    maxIter    = m_maxIterSpin   ? m_maxIterSpin->value()          : 100;
        const int    samplePts  = m_sampleSpin    ? m_sampleSpin->value()           : 15000;

        // Index 0 = GPA; index N>0 = fixed reference (combo item N-1 in m_scans)
        std::string fixedRef;
        if (methodIdx > 0 && methodIdx - 1 < static_cast<int>(m_scans.size()))
            fixedRef = m_scans[static_cast<std::size_t>(methodIdx - 1)]->scannerName;

        const QString modeStr = (fixedRef.empty())
            ? "Running GPA registration…"
            : QString("Registering to %1…").arg(QString::fromStdString(fixedRef));
        QMetaObject::invokeMethod(this, [this, modeStr](){
            setStatus(modeStr); m_progress->setValue(40); }, Qt::QueuedConnection);

        GPAReference::Params gpaParams;
        gpaParams.icpParams.maxIterations    = maxIter;
        gpaParams.icpParams.sampleCount      = samplePts;
        gpaParams.fixedRefScannerName        = fixedRef;

        m_gpaReference = GPAReference::compute(m_scans, gpaParams,
            [this](int cycle, int scanIdx, double rms) {
                Q_UNUSED(cycle); Q_UNUSED(scanIdx); Q_UNUSED(rms);
            });

        // Step 4: distance fields
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Computing distance fields…"); m_progress->setValue(70); }, Qt::QueuedConnection);
        const double zWindow = m_zWindowSpin ? m_zWindowSpin->value() : 0.0;
        if (m_gpaReference) {
            for (std::size_t i = 0; i < m_scans.size(); ++i) {
                DistanceField::compute(*m_scans[i], *m_gpaReference);
                DistanceField::fillReport(*m_scans[i], m_reports[i], 0.2, zWindow);
            }
        }

        // Step 5: arch metrics
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Computing boundary and arch metrics…"); m_progress->setValue(88); }, Qt::QueuedConnection);
        for (std::size_t i = 0; i < m_scans.size(); ++i) {
            ArchMetrics::computeBoundaryMetrics(*m_scans[i], m_reports[i]);
            ArchMetrics::computeStitchingArtifacts(*m_scans[i], m_reports[i]);
        }

        QMetaObject::invokeMethod(this, [this](){ m_progress->setValue(100); },
                                  Qt::QueuedConnection);
    });
    m_analysisWatcher->setFuture(future);
}

void MainWindow::onAnalysisFinished()
{
    m_progress->hide();
    setStatus("Analysis complete.");

    // Switch to Fingerprint tab BEFORE updating so the widget is visible
    // when QPainter first redraws it.
    m_tabs->setCurrentIndex(1);

    updateOverviewTab();      // re-render with post-registration mesh positions
    updateFingerprintTab();
    updateRegistrationTab();
    updateDistanceMapsTab();
    updateMetricsTab();

    // Re-enable segmentation buttons when seeds are still placed after a
    // recompute-registration cycle (buttons were disabled at the start of that run).
    const bool haveSeeds = !m_pickedPts.empty() && !m_scans.empty();
    if (m_recomputeBtn)
        m_recomputeBtn->setEnabled(haveSeeds);
    if (m_reregisterBtn && m_keepSegChk && m_keepSegChk->isChecked())
        m_reregisterBtn->setEnabled(haveSeeds && m_gpaReference != nullptr);
}

void MainWindow::showExportDialog()
{
    QSettings s("DentScanCompare", "DentScanCompare");
    const QString lastExportDir = s.value("lastExportDir", QDir::homePath()).toString();

    QString dir = QFileDialog::getExistingDirectory(
        this, "Select export directory", lastExportDir);
    if (dir.isEmpty()) return;
    s.setValue("lastExportDir", dir);

    // Export fingerprint (QPainter renders to QImage, saved directly)
    if (m_scatterPlot) {
        QImage img = m_scatterPlot->renderToImage(2400, 1800);
        img.save(dir + "/fingerprint_300dpi.png");
    }

    // Export distance maps
    for (std::size_t i = 0; i < m_distWidgets.size() && i < m_scans.size(); ++i) {
        QString fn = dir + "/" + QString::fromStdString(m_scans[i]->scannerName) + "_distmap.png";
        auto* rw = m_distWidgets[i]->renderer()->GetRenderWindow();
        if (rw) ReportExporter::toPNG(rw, fn, 2);
    }

    // Export metrics CSV
    if (m_metricsTable)
        m_metricsTable->exportToFile(dir + "/DentScanCompare_metrics.csv");

    QMessageBox::information(this, "Export done",
        "Files saved to:\n" + dir);
}

// -----------------------------------------------------------------------
// Update helpers
// -----------------------------------------------------------------------

void MainWindow::updateScannerList()
{
    QSignalBlocker blocker(m_scanList);
    m_scanList->clear();
    for (std::size_t i = 0; i < m_scans.size(); ++i) {
        const auto& scan = m_scans[i];
        const bool isRef = (static_cast<int>(i) == m_fixedRefScanIdx);
        QString name = QString::fromStdString(scan->scannerName);
        auto* item = new QListWidgetItem(isRef ? name + QStringLiteral(" ★") : name);
        item->setToolTip(
            QString::fromStdString(scan->filePath) + "\n"
            + QString::number(scan->triangleCount) + " triangles\n"
            + "[" + QString::fromStdString(scan->stlHeader) + "]");
        if (isRef) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_scanList->addItem(item);
    }
    if (m_fixedRefScanIdx >= 0 && m_fixedRefScanIdx < m_scanList->count())
        m_scanList->setCurrentRow(m_fixedRefScanIdx);
}

void MainWindow::refreshScanListMarkers()
{
    const int selRow = m_scanList->currentRow();
    QSignalBlocker blocker(m_scanList);
    for (int i = 0; i < m_scanList->count(); ++i) {
        auto* item = m_scanList->item(i);
        if (!item) continue;
        const bool isRef = (i == m_fixedRefScanIdx);
        QString name = (i < static_cast<int>(m_scans.size()))
            ? QString::fromStdString(m_scans[i]->scannerName) : item->text();
        item->setText(isRef ? name + QStringLiteral(" ★") : name);
        QFont f = item->font();
        f.setBold(isRef);
        item->setFont(f);
    }
    m_scanList->setCurrentRow(selRow);
}

void MainWindow::updateMethodCombo()
{
    if (!m_methodCombo) return;
    const int prev = m_methodCombo->currentIndex();
    m_methodCombo->clear();
    m_methodCombo->addItem("GPA – mean reference (recommended)");
    for (const auto& scan : m_scans)
        m_methodCombo->addItem(
            QString("Fixed ref: %1").arg(
                QString::fromStdString(scan->scannerName)));
    // restore previous selection if still valid
    if (prev > 0 && prev < m_methodCombo->count())
        m_methodCombo->setCurrentIndex(prev);
}

void MainWindow::rebuildScanWidgets()
{
    // Delete all existing overview widgets
    for (auto* w : m_overviewWidgets) {
        m_overviewHBox->removeWidget(w);
        delete w;
    }
    m_overviewWidgets.clear();

    // Delete all existing distance-map widgets
    for (auto* w : m_distWidgets) {
        m_distHBox->removeWidget(w);
        delete w;
    }
    m_distWidgets.clear();

    // Create exactly one widget per loaded scan.
    // Minimum width of 240 px: up to 5 scans fit without scrolling in the default
    // 1400 px window; 6+ scans trigger the horizontal scrollbar automatically.
    for (std::size_t i = 0; i < m_scans.size(); ++i) {
        const QString label = QString::fromStdString(m_scans[i]->scannerName);

        auto* ov = new VTKMeshWidget;
        ov->setMinimumWidth(240);
        ov->setTitle(label);
        m_overviewHBox->addWidget(ov);
        m_overviewWidgets.push_back(ov);

        auto* dm = new VTKMeshWidget;
        dm->setMinimumWidth(240);
        dm->setTitle(label + " – distance");
        m_distHBox->addWidget(dm);
        m_distWidgets.push_back(dm);
    }
}

void MainWindow::updateOverviewTab()
{
    for (std::size_t i = 0; i < m_scans.size(); ++i)
        m_overviewWidgets[i]->setMesh(m_scans[i]);
}

void MainWindow::updateFingerprintTab()
{
    m_scatterPlot->setScans(m_scans);

    // build ATI summary string
    QString atiText = "<b>Adaptive Tessellation Index (ATI)</b><br>"
        "ATI ≈ 1: density high near teeth (good adaptive tessellation)<br>"
        "ATI ≈ 0: uniform tessellation (poor adaptation)<br><br>";
    for (const auto& r : m_reports) {
        atiText += QString("<b>%1</b>: ATI = %2, ⌀ edge = %3 mm<br>")
            .arg(QString::fromStdString(r.scannerName))
            .arg(r.ati,     0, 'f', 3)
            .arg(r.meanEdgeLength, 0, 'f', 3);
    }
    m_atiLabel->setText(atiText);
}

void MainWindow::updateRegistrationTab()
{
    if (m_overlayWidget && !m_scans.empty())
        m_overlayWidget->setOverlayMeshes(m_scans);

    // Restore segmentation overlay if seeds are placed and user opted to keep it.
    if (!m_pickedPts.empty() && m_keepSegChk && m_keepSegChk->isChecked())
        runSegmentation();

    if (m_gpaReference && !m_reports.empty()) {
        double zw = m_zWindowSpin ? m_zWindowSpin->value() : 0.0;
        QString zoneStr = (zw > 0.0)
            ? QString("Occlusal zone: top %1 mm").arg(zw, 0, 'f', 0)
            : "Zone: full scan (gingiva included)";
        QString s = QString("GPA complete. Reference: %1 triangles.\n%2\n\n")
                        .arg(m_gpaReference->triangleCount)
                        .arg(zoneStr);
        for (const auto& r : m_reports) {
            if (!std::isnan(r.rmsDistance))
                s += QString("%1: RMS = %2 mm\n")
                         .arg(QString::fromStdString(r.scannerName))
                         .arg(r.rmsDistance, 0, 'f', 3);
        }
        m_registrationStatus->setText(s.trimmed());
    }
}

void MainWindow::updateDistanceMapsTab()
{
    if (!m_gpaReference) return;

    // auto-range: use 95th percentile of all distances
    double autoMaxDist = 0.5;
    for (const auto& r : m_reports)
        if (!std::isnan(r.hausdorff95))
            autoMaxDist = std::max(autoMaxDist, r.hausdorff95);
    autoMaxDist = std::min(autoMaxDist, 2.0);

    double maxDist;
    if (m_distRangeAuto) {
        if (m_distScaleSpin) {
            QSignalBlocker blk(m_distScaleSpin);
            m_distScaleSpin->setValue(autoMaxDist);
        }
        maxDist = autoMaxDist;
    } else {
        maxDist = m_distScaleSpin ? m_distScaleSpin->value() : autoMaxDist;
    }

    // Build per-scan tooth masks when seeds are placed, so the distance maps
    // grey out gingival tissue and focus colour on the crown area.
    const bool haveMask = !m_pickedPts.empty();
    ToothSegmentation::Params segParams;
    if (haveMask) {
        if (m_segGeodesicSpin) segParams.maxGeodesicMm     = m_segGeodesicSpin->value();
        if (m_segCreaseSpin)   segParams.maxCreaseAngleDeg = m_segCreaseSpin->value();
        if (m_segCurvSpin)     segParams.minMeanCurvature  = m_segCurvSpin->value();
    }

    for (std::size_t i = 0; i < m_scans.size(); ++i) {
        m_distWidgets[i]->setMesh(m_scans[i]);
        if (!m_scans[i]->distanceComputed) continue;

        if (haveMask) {
            auto mask = ToothSegmentation::segmentFromPoints(
                *m_scans[i], m_pickedPts, segParams);
            mask = applyEraseZones(mask, *m_scans[i]);
            m_distWidgets[i]->showDistanceMap(m_scans[i], -maxDist, maxDist, mask);
        } else {
            m_distWidgets[i]->showDistanceMap(m_scans[i], -maxDist, maxDist);
        }
    }
}

void MainWindow::updateMetricsTab()
{
    m_metricsTable->setReports(m_reports);
}

void MainWindow::setStatus(const QString& msg)
{
    m_statusBar->setText(msg);
}

// ── Occlusal-plane picking ────────────────────────────────────────────────────

void MainWindow::onPointPicked(double x, double y, double z)
{
    if (m_eraseBtn && m_eraseBtn->isChecked()) {
        onErasePointPicked(x, y, z);
        return;
    }

    m_pickedPts.push_back({x, y, z});
    const int n = static_cast<int>(m_pickedPts.size());

    if (m_overlayWidget)
        m_overlayWidget->showPickSpheres(m_pickedPts);

    if (n >= 3) {
        fitOcclusalPlane();
        if (m_showPlanesChk && m_showPlanesChk->isChecked())
            updatePlaneVisualization();
    }

    runSegmentation();
    if (m_undoSeedBtn) m_undoSeedBtn->setEnabled(true);
    if (m_saveSegBtn)  m_saveSegBtn->setEnabled(true);
    m_recomputeBtn->setEnabled(n >= 1 && !m_scans.empty());
    if (m_reregisterBtn) m_reregisterBtn->setEnabled(n >= 1 && !m_scans.empty() && m_gpaReference != nullptr);
}

void MainWindow::runSegmentation()
{
    const int n = static_cast<int>(m_pickedPts.size());
    if (n == 0) return;

    ToothSegmentation::Params params;
    if (m_segGeodesicSpin) params.maxGeodesicMm     = m_segGeodesicSpin->value();
    if (m_segCreaseSpin)   params.maxCreaseAngleDeg = m_segCreaseSpin->value();
    if (m_segCurvSpin)     params.minMeanCurvature  = m_segCurvSpin->value();

    if (!m_scans.empty()) {
        auto refIt = std::max_element(m_scans.begin(), m_scans.end(),
            [](const auto& a, const auto& b){
                return a->triangleCount < b->triangleCount; });

        m_toothMask = ToothSegmentation::segmentFromPoints(**refIt, m_pickedPts, params);
        auto displayMask = applyEraseZones(m_toothMask, **refIt);

        const std::size_t nTooth = std::count(displayMask.begin(), displayMask.end(), true);
        const std::size_t nErase = m_eraseZones.size();

        if (m_segStatusLabel) {
            QString status =
                QString("<span style='color:green;'>Active: %1 seed%2, ~%3 tooth vertices</span><br>"
                        "<span style='color:#555;'>Overlay: ivory=crown, grey=gingiva</span>")
                    .arg(n).arg(n == 1 ? "" : "s").arg(nTooth);
            if (nErase > 0)
                status += QString("<br><span style='color:#a06000;'>Erase zones: %1</span>").arg(nErase);
            m_segStatusLabel->setText(status);
        }

        if (m_pickCountLabel)
            m_pickCountLabel->setText(
                n >= 3 ? QString("Plane fitted through %1 points.").arg(n)
                       : QString("Need %1 more point%2 to fit plane.")
                             .arg(3 - n).arg(3 - n == 1 ? "" : "s"));

        if (m_overlayWidget)
            m_overlayWidget->showToothSegmentation(*refIt, displayMask);

        // Disable the Z-window spinbox — the tooth mask takes priority
        if (m_zWindowSpin) m_zWindowSpin->setEnabled(false);

        // Refresh distance maps so they immediately reflect the new mask
        updateDistanceMapsTab();
    } else {
        if (m_segStatusLabel)
            m_segStatusLabel->setText(
                QString("%1 seed%2 placed — load scans to run segmentation.")
                    .arg(n).arg(n == 1 ? "" : "s"));
        if (m_pickCountLabel)
            m_pickCountLabel->setText("Load scans first.");
    }
}

void MainWindow::clearPickedPoints()
{
    m_pickedPts.clear();
    m_toothMask.clear();
    m_eraseZones.clear();
    m_occlusalPlane.active = false;
    if (m_segStatusLabel) m_segStatusLabel->setText("No seeds placed.");
    if (m_pickCountLabel) m_pickCountLabel->setText("Plane: not fitted yet.");
    if (m_eraseBtn    && m_eraseBtn->isChecked())   m_eraseBtn->setChecked(false);
    if (m_undoSeedBtn) m_undoSeedBtn->setEnabled(false);
    if (m_saveSegBtn)  m_saveSegBtn->setEnabled(false);
    m_recomputeBtn->setEnabled(false);
    if (m_reregisterBtn)  m_reregisterBtn->setEnabled(false);
    if (m_zWindowSpin)    m_zWindowSpin->setEnabled(true);
    if (m_overlayWidget) {
        m_overlayWidget->clearPickActors();
        if (!m_scans.empty()) {
            // Restore overlay after clearing segmentation colours
            m_overlayWidget->setOverlayMeshes(m_scans);
        }
    }
}

// ── Gingiva eraser ────────────────────────────────────────────────────────────

void MainWindow::onErasePointPicked(double x, double y, double z)
{
    const double radius = m_eraseBrushSpin ? m_eraseBrushSpin->value() : 2.0;
    m_eraseZones.push_back({{x, y, z}, radius});

    if (!m_scans.empty() && !m_toothMask.empty()) {
        auto refIt = std::max_element(m_scans.begin(), m_scans.end(),
            [](const auto& a, const auto& b){
                return a->triangleCount < b->triangleCount; });
        auto displayMask = applyEraseZones(m_toothMask, **refIt);
        const std::size_t nTooth = std::count(displayMask.begin(), displayMask.end(), true);
        const std::size_t nErase = m_eraseZones.size();
        if (m_segStatusLabel)
            m_segStatusLabel->setText(
                QString("<span style='color:green;'>Active: %1 seed%2, ~%3 tooth vertices</span><br>"
                        "<span style='color:#a06000;'>Erase zones: %4 (radius %5 mm each click)</span>")
                    .arg(static_cast<int>(m_pickedPts.size()))
                    .arg(m_pickedPts.size() == 1 ? "" : "s")
                    .arg(nTooth)
                    .arg(nErase)
                    .arg(radius, 0, 'f', 1));
        if (m_overlayWidget)
            m_overlayWidget->showToothSegmentation(*refIt, displayMask);
        updateDistanceMapsTab();
    }
}

std::vector<bool> MainWindow::applyEraseZones(std::vector<bool> mask,
                                               const ScanData& scan) const
{
    if (m_eraseZones.empty()) return mask;
    const auto& mesh = scan.mesh;
    for (auto v : mesh.vertices()) {
        if (!mask[v.idx()]) continue;
        const Point3& p = mesh.point(v);
        const double px = CGAL::to_double(p.x());
        const double py = CGAL::to_double(p.y());
        const double pz = CGAL::to_double(p.z());
        for (const auto& [centre, radius] : m_eraseZones) {
            const double dx = px - centre[0];
            const double dy = py - centre[1];
            const double dz = pz - centre[2];
            if (dx*dx + dy*dy + dz*dz <= radius*radius) {
                mask[v.idx()] = false;
                break;
            }
        }
    }
    return mask;
}

// ── Segmentation file I/O ─────────────────────────────────────────────────────

void MainWindow::saveSegmentation()
{
    if (m_pickedPts.empty()) return;

    QString refName;
    if (m_fixedRefScanIdx >= 0 && m_fixedRefScanIdx < static_cast<int>(m_scans.size()))
        refName = QString::fromStdString(m_scans[m_fixedRefScanIdx]->scannerName);
    else
        refName = "GPA_mean";

    QSettings s("DentScanCompare", "DentScanCompare");
    const QString lastDir = s.value("lastSegDir", QDir::homePath()).toString();
    const QString defaultPath = lastDir + "/" + refName + "_segmentation.dsc_seg";

    const QString path = QFileDialog::getSaveFileName(
        this, "Save Segmentation", defaultPath,
        "DentScanCompare Segmentation (*.dsc_seg);;All files (*)");
    if (path.isEmpty()) return;
    s.setValue("lastSegDir", QFileInfo(path).absolutePath());

    QJsonObject root;
    root["format_version"] = 1;
    root["reference"]      = refName;

    QJsonArray seeds;
    for (const auto& p : m_pickedPts) {
        QJsonArray pt; pt.append(p[0]); pt.append(p[1]); pt.append(p[2]);
        seeds.append(pt);
    }
    root["seeds"] = seeds;

    QJsonArray zones;
    for (const auto& [centre, radius] : m_eraseZones) {
        QJsonObject z;
        QJsonArray c; c.append(centre[0]); c.append(centre[1]); c.append(centre[2]);
        z["center"] = c;
        z["radius"] = radius;
        zones.append(z);
    }
    root["erase_zones"] = zones;

    QJsonObject params;
    params["max_geodesic_mm"]    = m_segGeodesicSpin ? m_segGeodesicSpin->value() : 12.0;
    params["max_crease_deg"]     = m_segCreaseSpin   ? m_segCreaseSpin->value()   : 50.0;
    params["min_mean_curvature"] = m_segCurvSpin     ? m_segCurvSpin->value()     : -4.0;
    root["params"] = params;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save failed",
            "Could not open file for writing:\n" + path);
        return;
    }
    file.write(QJsonDocument(root).toJson());
    setStatus("Segmentation saved: " + QFileInfo(path).fileName());
}

void MainWindow::loadSegmentation()
{
    QSettings s("DentScanCompare", "DentScanCompare");
    const QString lastDir = s.value("lastSegDir", QDir::homePath()).toString();

    const QString path = QFileDialog::getOpenFileName(
        this, "Load Segmentation", lastDir,
        "DentScanCompare Segmentation (*.dsc_seg);;All files (*)");
    if (path.isEmpty()) return;
    s.setValue("lastSegDir", QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Load failed",
            "Could not open file:\n" + path);
        return;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (doc.isNull()) {
        QMessageBox::warning(this, "Load failed",
            "Invalid file format:\n" + err.errorString());
        return;
    }
    const QJsonObject root = doc.object();

    // Restore parameters first (with signals blocked so we don't trigger
    // premature runSegmentation() calls before the seeds are restored)
    const QJsonObject params = root["params"].toObject();
    if (!params.isEmpty()) {
        if (m_segGeodesicSpin && params.contains("max_geodesic_mm")) {
            QSignalBlocker b(m_segGeodesicSpin);
            m_segGeodesicSpin->setValue(params["max_geodesic_mm"].toDouble());
        }
        if (m_segCreaseSpin && params.contains("max_crease_deg")) {
            QSignalBlocker b(m_segCreaseSpin);
            m_segCreaseSpin->setValue(params["max_crease_deg"].toDouble());
        }
        if (m_segCurvSpin && params.contains("min_mean_curvature")) {
            QSignalBlocker b(m_segCurvSpin);
            m_segCurvSpin->setValue(params["min_mean_curvature"].toDouble());
        }
    }

    // Restore seeds
    m_pickedPts.clear();
    for (const QJsonValue& v : root["seeds"].toArray()) {
        const QJsonArray pt = v.toArray();
        if (pt.size() == 3)
            m_pickedPts.push_back({pt[0].toDouble(), pt[1].toDouble(), pt[2].toDouble()});
    }

    // Restore erase zones
    m_eraseZones.clear();
    for (const QJsonValue& v : root["erase_zones"].toArray()) {
        const QJsonObject z = v.toObject();
        const QJsonArray  c = z["center"].toArray();
        if (c.size() == 3)
            m_eraseZones.push_back(
                {{c[0].toDouble(), c[1].toDouble(), c[2].toDouble()},
                  z["radius"].toDouble()});
    }

    const int n = static_cast<int>(m_pickedPts.size());

    if (m_overlayWidget && n > 0)
        m_overlayWidget->showPickSpheres(m_pickedPts);
    if (n >= 3) {
        fitOcclusalPlane();
        if (m_showPlanesChk && m_showPlanesChk->isChecked())
            updatePlaneVisualization();
    }

    if (m_undoSeedBtn) m_undoSeedBtn->setEnabled(n > 0);
    if (m_saveSegBtn)  m_saveSegBtn->setEnabled(n > 0);
    m_recomputeBtn->setEnabled(n > 0 && !m_scans.empty());
    if (m_reregisterBtn)
        m_reregisterBtn->setEnabled(n > 0 && !m_scans.empty() && m_gpaReference != nullptr);

    if (n > 0) {
        runSegmentation();
        const QString refName  = root["reference"].toString("unknown");
        const QString fileName = QFileInfo(path).fileName();
        setStatus(QString("Loaded %1 — ref: %2, %3 seeds, %4 erase zone%5")
            .arg(fileName).arg(refName).arg(n)
            .arg(m_eraseZones.size()).arg(m_eraseZones.size() == 1 ? "" : "s"));
    } else {
        if (m_segStatusLabel) m_segStatusLabel->setText("No seeds in file.");
    }
}

void MainWindow::fitOcclusalPlane()
{
    const int n = static_cast<int>(m_pickedPts.size());
    if (n < 3) return;

    // Centroid
    Eigen::Vector3d centroid = Eigen::Vector3d::Zero();
    for (const auto& p : m_pickedPts)
        centroid += Eigen::Vector3d(p[0], p[1], p[2]);
    centroid /= n;

    // Covariance matrix of picked points
    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& p : m_pickedPts) {
        Eigen::Vector3d d = Eigen::Vector3d(p[0], p[1], p[2]) - centroid;
        cov += d * d.transpose();
    }

    // Eigenvector for smallest eigenvalue = plane normal (least-squares fit)
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
    Eigen::Vector3d normal = eig.eigenvectors().col(0);

    // Ensure normal points toward +Z (occlusal surface = tooth side after PCA)
    if (normal.z() < 0.0) normal = -normal;
    normal.normalize();

    m_occlusalPlane.normal = normal;
    m_occlusalPlane.origin = centroid;
    m_occlusalPlane.active = true;
}

void MainWindow::updatePlaneVisualization()
{
    if (!m_overlayWidget || !m_occlusalPlane.active) return;

    const double above = m_planeAboveSpin ? m_planeAboveSpin->value() : 2.0;
    const double below = m_planeBelowSpin ? m_planeBelowSpin->value() : 12.0;

    // Re-draw spheres first (showOcclusalPlane clears all pick actors)
    m_overlayWidget->showPickSpheres(m_pickedPts);
    m_overlayWidget->showOcclusalPlane(m_occlusalPlane.normal,
                                       m_occlusalPlane.origin,
                                       above, below);
}

void MainWindow::recomputeMetrics()
{
    if (m_scans.empty() || !m_gpaReference) {
        setStatus("Run registration first.");
        return;
    }

    // Determine active filter (priority: segmentation mask > plane > Z-window)
    const bool  haveMask = !m_pickedPts.empty();
    DistanceField::OcclusalPlane plane = m_occlusalPlane;
    if (plane.active && !haveMask) {
        plane.aboveMm = m_planeAboveSpin ? m_planeAboveSpin->value() : 2.0;
        plane.belowMm = m_planeBelowSpin ? m_planeBelowSpin->value() : 12.0;
    }
    const double zWindow = (!haveMask && !plane.active && m_zWindowSpin)
                           ? m_zWindowSpin->value() : 0.0;

    ToothSegmentation::Params segParams;
    if (m_segGeodesicSpin) segParams.maxGeodesicMm     = m_segGeodesicSpin->value();
    if (m_segCreaseSpin)   segParams.maxCreaseAngleDeg = m_segCreaseSpin->value();
    if (m_segCurvSpin)     segParams.minMeanCurvature  = m_segCurvSpin->value();

    setStatus("Recomputing metrics…");
    std::size_t totalToothVerts = 0;
    for (std::size_t i = 0; i < m_scans.size(); ++i) {
        std::vector<bool> mask;
        if (haveMask) {
            mask = ToothSegmentation::segmentFromPoints(*m_scans[i], m_pickedPts, segParams);
            mask = applyEraseZones(mask, *m_scans[i]);
        }
        DistanceField::fillReport(*m_scans[i], m_reports[i],
                                  0.2, zWindow, plane, mask);
        ArchMetrics::computeBoundaryMetrics(*m_scans[i], m_reports[i]);
        ArchMetrics::computeStitchingArtifacts(*m_scans[i], m_reports[i]);
        if (!mask.empty())
            totalToothVerts += std::count(mask.begin(), mask.end(), true);
    }

    updateMetricsTab();
    updateRegistrationTab();
    updateDistanceMapsTab();

    QString filterDesc = haveMask
        ? QString("tooth segmentation (~%1 vertices/scan)")
              .arg(totalToothVerts / m_scans.size())
        : plane.active
            ? QString("plane slab (+%1/−%2 mm)")
                  .arg(plane.aboveMm, 0,'f',1).arg(plane.belowMm, 0,'f',1)
            : zWindow > 0
                ? QString("Z-window %1 mm").arg(zWindow, 0,'f',0)
                : "full scan";
    setStatus("Metrics updated – filter: " + filterDesc + ".");
}

void MainWindow::recomputeRegistration()
{
    if (m_scans.empty() || !m_gpaReference || m_pickedPts.empty()) {
        setStatus("Run initial registration and place tooth seeds first.");
        return;
    }

    ToothSegmentation::Params segParams;
    if (m_segGeodesicSpin) segParams.maxGeodesicMm     = m_segGeodesicSpin->value();
    if (m_segCreaseSpin)   segParams.maxCreaseAngleDeg = m_segCreaseSpin->value();
    if (m_segCurvSpin)     segParams.minMeanCurvature  = m_segCurvSpin->value();

    ICPRegistration::Params icpParams;
    if (m_maxIterSpin) icpParams.maxIterations = m_maxIterSpin->value();
    if (m_sampleSpin)  icpParams.sampleCount   = m_sampleSpin->value();
    icpParams.maxCorrespDist = 3.0; // tighter than initial GPA (already nearby)

    setStatus("Recomputing registration from tooth crowns...");
    m_progress->setValue(0);
    m_progress->show();
    if (m_reregisterBtn) m_reregisterBtn->setEnabled(false);
    if (m_recomputeBtn)  m_recomputeBtn->setEnabled(false);

    auto scans     = m_scans;
    auto gpaRef    = m_gpaReference;
    auto pickedPts = m_pickedPts;
    auto sParams   = segParams;
    auto iParams   = icpParams;

    auto future = QtConcurrent::run([scans, gpaRef, pickedPts, sParams, iParams]() mutable {
        for (auto& scan : scans) {
            std::vector<bool> mask =
                ToothSegmentation::segmentFromPoints(*scan, pickedPts, sParams);
            auto res = ICPRegistration::alignMasked(*scan, *gpaRef, mask, iParams);
            if (res.iterations > 0)
                ICPRegistration::applyTransform(*scan, res.transform);
        }
        GPAReference::updateMeanMesh(*gpaRef, scans);
        for (auto& scan : scans)
            DistanceField::compute(*scan, *gpaRef);
    });

    if (!m_analysisWatcher) {
        m_analysisWatcher = new QFutureWatcher<void>(this);
        connect(m_analysisWatcher, &QFutureWatcher<void>::finished,
                this, &MainWindow::onAnalysisFinished);
    }
    m_analysisWatcher->setFuture(future);
}
