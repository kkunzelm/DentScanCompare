#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>


#include "../core/Mesh.h"
#include <QWidget>
#include <QImage>
#include <QRect>
#include <memory>
#include <vector>

// QPainter-based log-log scatter plot: |mean curvature| vs. triangle area.
// One colour-coded series per scanner.
// Click a legend row (or call setHighlightSeries) to bring one scanner to the
// foreground at full opacity; all others are dimmed.  Click again to deselect.
class ScatterPlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScatterPlotWidget(QWidget* parent = nullptr);

    void setScans(const std::vector<std::shared_ptr<ScanData>>& scans,
                  int maxPointsPerScan = 30000);
    void clear();

    // Highlight a single series by its index in the scan list (-1 = show all equally).
    void setHighlightSeries(int seriesIdx);
    int  highlightSeries() const { return m_highlightIdx; }

    // Render to an off-screen image for export.
    QImage renderToImage(int width, int height);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* ev) override;

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
    int   m_highlightIdx = -1;

    // Legend hit-test rects — updated by drawChart() on every paint.
    mutable std::vector<QRect> m_legendRowRects;

    void  drawChart(QPainter& p, int w, int h) const;
    static QColor seriesColor(int idx);
};
