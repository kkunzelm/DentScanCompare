#pragma once

#include "../core/Mesh.h"
#include <QWidget>
#include <QImage>
#include <memory>
#include <vector>

// QPainter-based log-log scatter plot: |mean curvature| vs. triangle area.
// One colour-coded series per scanner.
class ScatterPlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScatterPlotWidget(QWidget* parent = nullptr);

    void setScans(const std::vector<std::shared_ptr<ScanData>>& scans,
                  int maxPointsPerScan = 30000);
    void clear();

    // Render to an off-screen image for export.
    QImage renderToImage(int width, int height);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct Series {
        std::string name;
        std::vector<float> xLog; // log10(|kH|)
        std::vector<float> yLog; // log10(area)
        QColor color;
    };

    std::vector<Series> m_series;
    float m_xMin = -3.f, m_xMax = 1.f;
    float m_yMin = -4.f, m_yMax = 0.f;

    void  drawChart(QPainter& p, int w, int h) const;
    static QColor seriesColor(int idx);
};
