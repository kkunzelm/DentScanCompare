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
#include <QSpinBox>
#include <QFutureWatcher>
#include <Eigen/Core>
#include <array>
#include <memory>
#include <vector>

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
    void updateMethodCombo();   // repopulate method combo from loaded scans

    // ---- occlusal-plane picking ----
    void onPointPicked(double x, double y, double z);
    void clearPickedPoints();
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

    // ---- Tab 1: Overview ----
    QWidget*                    m_tab1       = nullptr;
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
    QPushButton*    m_clearPickBtn       = nullptr;
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

    // tooth-segmentation mask (per vertex, for the first loaded scan used as
    // the segmentation reference; applied to all scans in recomputeMetrics)
    std::vector<bool>                 m_toothMask;

    // ---- Tab 4: Distance Maps ----
    QWidget*                    m_tab4           = nullptr;
    std::vector<VTKMeshWidget*> m_distWidgets;
    QDoubleSpinBox*             m_distScaleSpin  = nullptr;
    bool                        m_distRangeAuto  = true;

    // ---- Tab 5: Metrics ----
    MetricsTableWidget* m_metricsTable = nullptr;

    // ---- async workers ----
    QFutureWatcher<void>* m_loadWatcher     = nullptr;
    QFutureWatcher<void>* m_analysisWatcher = nullptr;
};
