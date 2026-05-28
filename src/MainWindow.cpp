#include "MainWindow.h"

#include "core/STLReader.h"
#include "core/CurvatureAnalysis.h"
#include "core/TessellationMetrics.h"
#include "core/ICPRegistration.h"
#include "core/GPAReference.h"
#include "core/DistanceField.h"
#include "core/ArchMetrics.h"
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
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
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

    // left: controls
    auto* ctrlPanel = new QGroupBox("Registration Settings", m_tab3);
    ctrlPanel->setFixedWidth(220);
    auto* ctrlLayout = new QFormLayout(ctrlPanel);

    auto* methodCombo = new QComboBox(ctrlPanel);
    methodCombo->addItem("GPA (Hybrid, Recommended)");
    methodCombo->addItem("Reference: Primescan");
    methodCombo->addItem("Reference: Trios5");
    ctrlLayout->addRow("Method:", methodCombo);

    auto* maxIterSpin = new QSpinBox(ctrlPanel);
    maxIterSpin->setRange(1, 200); maxIterSpin->setValue(100);
    ctrlLayout->addRow("ICP iterations:", maxIterSpin);

    auto* sampleSpin = new QSpinBox(ctrlPanel);
    sampleSpin->setRange(1000, 100000); sampleSpin->setSingleStep(1000);
    sampleSpin->setValue(20000);
    ctrlLayout->addRow("ICP sample pts:", sampleSpin);

    auto* runBtn = new QPushButton("Run Registration", ctrlPanel);
    connect(runBtn, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    ctrlLayout->addRow(runBtn);

    m_registrationStatus = new QLabel("Not yet run.", ctrlPanel);
    m_registrationStatus->setWordWrap(true);
    ctrlLayout->addRow(m_registrationStatus);

    hlay->addWidget(ctrlPanel);

    // right: overlay view
    m_overlayWidget = new VTKMeshWidget(m_tab3);
    m_overlayWidget->setTitle("Overlay (all registered scans)");
    hlay->addWidget(m_overlayWidget, 1);

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
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open STL files",
        QString(),
        "STL files (*.stl *.STL);;All files (*)");
    if (paths.isEmpty()) return;

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

        // Step 3: registration (GPA)
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Running ICP / GPA registration…"); m_progress->setValue(40); }, Qt::QueuedConnection);
        GPAReference::Params gpaParams;
        gpaParams.icpParams.sampleCount = 15000;
        m_gpaReference = GPAReference::compute(m_scans, gpaParams,
            [this](int cycle, int scanIdx, double rms) {
                Q_UNUSED(cycle); Q_UNUSED(scanIdx); Q_UNUSED(rms);
            });

        // Step 4: distance fields
        QMetaObject::invokeMethod(this, [this](){
            setStatus("Computing distance fields…"); m_progress->setValue(70); }, Qt::QueuedConnection);
        if (m_gpaReference) {
            for (std::size_t i = 0; i < m_scans.size(); ++i) {
                DistanceField::compute(*m_scans[i], *m_gpaReference);
                DistanceField::fillReport(*m_scans[i], m_reports[i]);
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
    updateFingerprintTab();
    updateDistanceMapsTab();
    updateMetricsTab();

    // switch to Fingerprint tab to show first result
    m_tabs->setCurrentIndex(1);

    // update registration status
    if (m_gpaReference) {
        m_registrationStatus->setText(
            QString("GPA converged. Reference: %1 triangles.")
            .arg(m_gpaReference->triangleCount));
    }
}

void MainWindow::showExportDialog()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select export directory", QDir::homePath());
    if (dir.isEmpty()) return;

    // Export fingerprint
    if (m_scatterPlot) {
        auto img = m_scatterPlot->renderToImage(2400, 1800);
        ReportExporter::imageToPNG(img, dir + "/fingerprint_300dpi.png");
    }

    // Export distance maps
    for (std::size_t i = 0; i < m_distWidgets.size() && i < m_scans.size(); ++i) {
        QString fn = dir + "/" + QString::fromStdString(m_scans[i]->scannerName) + "_distmap.png";
        auto* rw = m_distWidgets[i]->renderer()->GetRenderWindow();
        if (rw) ReportExporter::toPNG(rw, fn, 2);
    }

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
