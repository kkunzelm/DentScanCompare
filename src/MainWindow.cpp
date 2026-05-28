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
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <Eigen/Eigenvalues>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("DentScanCompare – Dental Scan Quality Analyzer");
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

    sideLayout->addWidget(scanGroup);
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

    topLayout->addWidget(sidebar);
    topLayout->addWidget(m_tabs, 1);
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
    auto* container = new QWidget;
    auto* hbox = new QHBoxLayout(container);
    hbox->setSpacing(4);

    // pre-create 5 placeholder widgets
    for (int i = 0; i < 5; ++i) {
        auto* w = new VTKMeshWidget(container);
        w->setTitle(QString("Scan %1").arg(i + 1));
        hbox->addWidget(w);
        m_overviewWidgets.push_back(w);
    }
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

    m_clearPickBtn = new QPushButton("Clear Seeds", ctrlPanel);
    m_clearPickBtn->setToolTip("Remove all seed points and reset the segmentation.");
    ctrlLayout->addRow(m_clearPickBtn);

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
    m_showPlanesChk->setChecked(true);
    m_showPlanesChk->setToolTip(
        "Toggle visibility of the three semi-transparent occlusal plane disks\n"
        "(grey = plane, green = above, cyan = below).");
    ctrlLayout->addRow(m_showPlanesChk);

    // ── wire picking signals ──────────────────────────────────────────────
    connect(m_pickBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_overlayWidget) m_overlayWidget->setPickMode(on);
        m_pickBtn->setText(on ? "🛑 Stop Picking" : "📍 Pick Tooth Seeds");
    });
    connect(m_clearPickBtn, &QPushButton::clicked,
            this, &MainWindow::clearPickedPoints);
    connect(m_recomputeBtn,   &QPushButton::clicked,
            this, &MainWindow::recomputeMetrics);
    connect(m_reregisterBtn,  &QPushButton::clicked,
            this, &MainWindow::recomputeRegistration);
    connect(m_showPlanesChk,  &QCheckBox::toggled, this, [this](bool on) {
        if (m_overlayWidget) m_overlayWidget->setPlanesVisible(on);
    });
    connect(m_planeAboveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ updatePlaneVisualization(); });
    connect(m_planeBelowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ updatePlaneVisualization(); });
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
        "Blue = below reference (−), red = above reference (+). Scale: ±1 mm.", m_tab4);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("font-size: 10px; color: #444; padding: 2px;");
    vlay->addWidget(infoLabel);

    auto* scroll = new QScrollArea(m_tab4);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* container = new QWidget;
    auto* hbox = new QHBoxLayout(container);
    hbox->setSpacing(4);

    for (int i = 0; i < 5; ++i) {
        auto* w = new VTKMeshWidget(container);
        w->setTitle(QString("Scan %1 – distance").arg(i + 1));
        hbox->addWidget(w);
        m_distWidgets.push_back(w);
    }
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
    m_scanList->clear();
    for (const auto& scan : m_scans) {
        auto* item = new QListWidgetItem(
            QString::fromStdString(scan->scannerName));
        item->setToolTip(
            QString::fromStdString(scan->filePath) + "\n"
            + QString::number(scan->triangleCount) + " triangles\n"
            + "[" + QString::fromStdString(scan->stlHeader) + "]");
        m_scanList->addItem(item);
    }
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

void MainWindow::updateOverviewTab()
{
    // resize widget list if needed
    while (m_overviewWidgets.size() < m_scans.size()) {
        auto* w = new VTKMeshWidget;
        auto* scroll = qobject_cast<QScrollArea*>(
            m_tab1->layout()->itemAt(1)->widget());
        if (scroll && scroll->widget() && scroll->widget()->layout())
            scroll->widget()->layout()->addWidget(w);
        m_overviewWidgets.push_back(w);
    }

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
    double maxDist = 0.5;
    for (const auto& r : m_reports)
        if (!std::isnan(r.hausdorff95))
            maxDist = std::max(maxDist, r.hausdorff95);
    maxDist = std::min(maxDist, 2.0);

    // resize distance widgets if needed
    while (m_distWidgets.size() < m_scans.size()) {
        auto* w = new VTKMeshWidget;
        auto* scroll = qobject_cast<QScrollArea*>(
            m_tab4->layout()->itemAt(1)->widget());
        if (scroll && scroll->widget() && scroll->widget()->layout())
            scroll->widget()->layout()->addWidget(w);
        m_distWidgets.push_back(w);
    }

    for (std::size_t i = 0; i < m_scans.size(); ++i) {
        m_distWidgets[i]->setMesh(m_scans[i]);
        if (m_scans[i]->distanceComputed)
            m_distWidgets[i]->showDistanceMap(m_scans[i], -maxDist, maxDist);
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
    m_pickedPts.push_back({x, y, z});
    const int n = static_cast<int>(m_pickedPts.size());

    if (m_overlayWidget)
        m_overlayWidget->showPickSpheres(m_pickedPts);

    if (n >= 3) {
        fitOcclusalPlane();
        updatePlaneVisualization();
    }

    runSegmentation();
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

        const std::size_t nTooth = std::count(m_toothMask.begin(), m_toothMask.end(), true);

        if (m_segStatusLabel)
            m_segStatusLabel->setText(
                QString("<span style='color:green;'>Active: %1 seed%2, ~%3 tooth vertices</span><br>"
                        "<span style='color:#555;'>Overlay: ivory=crown, grey=gingiva</span>")
                    .arg(n).arg(n == 1 ? "" : "s").arg(nTooth));

        if (m_pickCountLabel)
            m_pickCountLabel->setText(
                n >= 3 ? QString("Plane fitted through %1 points.").arg(n)
                       : QString("Need %1 more point%2 to fit plane.")
                             .arg(3 - n).arg(3 - n == 1 ? "" : "s"));

        if (m_overlayWidget)
            m_overlayWidget->showToothSegmentation(*refIt, m_toothMask);

        // Disable the Z-window spinbox — the tooth mask takes priority
        if (m_zWindowSpin) m_zWindowSpin->setEnabled(false);
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
    m_occlusalPlane.active = false;
    if (m_segStatusLabel) m_segStatusLabel->setText("No seeds placed.");
    if (m_pickCountLabel) m_pickCountLabel->setText("Plane: not fitted yet.");
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
        if (haveMask)
            mask = ToothSegmentation::segmentFromPoints(*m_scans[i], m_pickedPts, segParams);
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
