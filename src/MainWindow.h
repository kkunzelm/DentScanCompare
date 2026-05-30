#pragma once

#include "core/Mesh.h"
#include "core/MetricReport.h"
#include "core/DistanceField.h"
#include <QMainWindow>
#include <QTabWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QFutureWatcher>
#include <Eigen/Core>
#include <array>
#include <memory>
#include <vector>

class QHBoxLayout;
class VTKMeshWidget;
class ScatterPlotWidget;
class MetricsTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void openSTLFiles();
    void runAnalysis();
    void showExportDialog();
    void onLoadFinished();
    void onAnalysisFinished();

private:
    // ---- UI setup ----
    void setupUI();
    void setupMenuAndToolbar();
    void setupTab1Overview();
    void setupTab2Fingerprint();
    void setupTab3Registration();
    void setupTab4DistanceMaps();
    void setupTab5Metrics();
    void setupTab6Export();
    void setupTab7About();

    // ---- update helpers ----
    void updateScannerList();
    void updateMethodCombo();       // repopulate method combo from loaded scans
    void refreshScanListMarkers();  // update bold/star markers in-place without clearing
    void rebuildScanWidgets();      // delete + recreate overview/distmap widgets for current scan count

    // ---- occlusal-plane picking ----
    void onPointPicked(double x, double y, double z);
    void onErasePointPicked(double x, double y, double z);
    void clearPickedPoints();

    // ---- gingiva eraser ----
    // Returns a copy of mask with all vertices inside any erase zone set to false.
    std::vector<bool> applyEraseZones(std::vector<bool> mask, const ScanData& scan) const;

    // ---- segmentation file I/O ----
    void saveSegmentation();
    void loadSegmentation();
    void fitOcclusalPlane();        // least-squares plane through m_pickedPts
    void updatePlaneVisualization();
    void recomputeMetrics();          // re-run distance+arch steps without re-running ICP
    void runSegmentation();           // re-run tooth segmentation with current params+seeds
    void recomputeRegistration();     // warm-start ICP using tooth mask, then update metrics
    void updateOverviewTab();
    void updateFingerprintTab();
    void updateRegistrationTab();
    void updateDistanceMapsTab();
    void updateMetricsTab();
    void setStatus(const QString& msg);

    // ---- data ----
    std::vector<std::shared_ptr<ScanData>> m_scans;
    std::shared_ptr<ScanData>              m_gpaReference;
    std::vector<MetricReport>              m_reports;

    // ---- main layout ----
    QTabWidget*   m_tabs      = nullptr;
    QListWidget*  m_scanList  = nullptr;
    QLabel*       m_statusBar = nullptr;
    QProgressBar* m_progress  = nullptr;

    // ---- Sidebar: reference surface selection ----
    QRadioButton* m_refGPARadio     = nullptr;  // "GPA mean (all scans)"
    QRadioButton* m_refFixedRadio   = nullptr;  // "Fixed reference scan:"
    QLabel*       m_refFixedLabel   = nullptr;  // shows selected reference name
    int           m_fixedRefScanIdx = -1;        // -1 = GPA mean; >=0 = index in m_scans

    // ---- Tab 1: Overview ----
    QWidget*                    m_tab1          = nullptr;
    QHBoxLayout*                m_overviewHBox  = nullptr;
    std::vector<VTKMeshWidget*> m_overviewWidgets;

    // ---- Tab 2: Fingerprint ----
    ScatterPlotWidget* m_scatterPlot = nullptr;
    QLabel*            m_atiLabel    = nullptr;

    // ---- Tab 3: Registration ----
    QWidget*        m_tab3               = nullptr;
    VTKMeshWidget*  m_overlayWidget      = nullptr;
    QLabel*         m_registrationStatus = nullptr;
    QComboBox*      m_methodCombo        = nullptr;
    QSpinBox*       m_maxIterSpin        = nullptr;
    QSpinBox*       m_sampleSpin         = nullptr;
    QDoubleSpinBox* m_zWindowSpin        = nullptr;

    // tooth-segmentation / occlusal-plane picking controls
    QPushButton*    m_pickBtn            = nullptr;
    QLabel*         m_segStatusLabel     = nullptr;  // shows seed count + vertex count
    QPushButton*    m_undoSeedBtn        = nullptr;  // remove most-recent seed
    QPushButton*    m_clearPickBtn       = nullptr;
    QPushButton*    m_eraseBtn           = nullptr;  // toggle: erase-gingiva mode
    QDoubleSpinBox* m_eraseBrushSpin     = nullptr;  // brush radius [mm]
    QPushButton*    m_saveSegBtn         = nullptr;  // save seeds+zones+params to file
    QPushButton*    m_loadSegBtn         = nullptr;  // load from file
    QPushButton*    m_recomputeBtn       = nullptr;
    QPushButton*    m_reregisterBtn      = nullptr;  // crown-restricted ICP warm start
    QCheckBox*      m_showPlanesChk      = nullptr;  // show/hide the three plane disks
    QCheckBox*      m_keepSegChk         = nullptr;  // restore seg overlay after registration update
    QDoubleSpinBox* m_segGeodesicSpin    = nullptr;  // maxGeodesicMm
    QDoubleSpinBox* m_segCreaseSpin      = nullptr;  // maxCreaseAngleDeg
    QDoubleSpinBox* m_segCurvSpin        = nullptr;  // minMeanCurvature
    QDoubleSpinBox* m_planeAboveSpin     = nullptr;  // zone above fitted plane [mm]
    QDoubleSpinBox* m_planeBelowSpin     = nullptr;  // zone below fitted plane [mm]
    QLabel*         m_pickCountLabel     = nullptr;  // plane point count (fallback section)

    // picking / segmentation state
    std::vector<std::array<double,3>> m_pickedPts;
    DistanceField::OcclusalPlane      m_occlusalPlane;

    // tooth-segmentation mask (per vertex, for the reference scan).
    // This is the raw Dijkstra result; erase zones are applied on top at display time.
    std::vector<bool>                 m_toothMask;

    // erase zones: world-space (centre, radius) spheres that exclude vertices
    // from the tooth mask after Dijkstra (manual gingiva correction)
    std::vector<std::pair<std::array<double,3>, double>> m_eraseZones;

    // ---- Tab 4: Distance Maps ----
    QWidget*                    m_tab4      = nullptr;
    QHBoxLayout*                m_distHBox  = nullptr;
    std::vector<VTKMeshWidget*> m_distWidgets;
    QDoubleSpinBox*             m_distScaleSpin  = nullptr;
    bool                        m_distRangeAuto  = true;

    // ---- Tab 5: Metrics ----
    MetricsTableWidget* m_metricsTable = nullptr;

    // ---- async workers ----
    QFutureWatcher<void>* m_loadWatcher     = nullptr;
    QFutureWatcher<void>* m_analysisWatcher = nullptr;
};
