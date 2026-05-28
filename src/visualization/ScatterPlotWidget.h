#pragma once

#include "../core/Mesh.h"
#include "../core/MetricReport.h"
#include "ColorMapLUT.h"
#include <QWidget>
#include <vtkSmartPointer.h>
#include <vtkContextView.h>
#include <vtkChartXY.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <memory>
#include <vector>

class QVTKOpenGLNativeWidget;

// VTK-based scatter plot: |mean curvature| vs. triangle area for all scanners.
// One colour-coded series per scanner. Log scale on both axes.
class ScatterPlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScatterPlotWidget(QWidget* parent = nullptr);
    ~ScatterPlotWidget() override = default;

    // Rebuild the scatter plot from all scans.
    // Requires scan->tessellationMetricsComputed == true for each entry.
    // maxPointsPerScan: subsampling limit for rendering performance.
    void setScans(const std::vector<std::shared_ptr<ScanData>>& scans,
                  int maxPointsPerScan = 30000);

    void clear();

    // Render to an offscreen image for export.
    // Returns a vtkImageData with the chart rendered at the given pixel size.
    vtkSmartPointer<vtkImageData> renderToImage(int width, int height);

private:
    void buildPipeline();

    QVTKOpenGLNativeWidget*  m_vtkWidget   = nullptr;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkContextView> m_contextView;
    vtkSmartPointer<vtkChartXY>     m_chart;
};
