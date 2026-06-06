// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>

#include "ScatterPlotWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QFontMetrics>

#include <algorithm>
#include <cmath>
#include <random>

static const QColor kColors[] = {
    QColor( 31, 119, 180),
    QColor(255, 127,  14),
    QColor( 44, 160,  44),
    QColor(214,  39,  40),
    QColor(148, 103, 189),
};

QColor ScatterPlotWidget::seriesColor(int idx)
{
    return kColors[idx % 5];
}

ScatterPlotWidget::ScatterPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
}

void ScatterPlotWidget::clear()
{
    m_series.clear();
    m_highlightIdx = -1;
    update();
}

void ScatterPlotWidget::setHighlightSeries(int idx)
{
    // idx may be a scan-list row; clamp to valid range (-1 = all)
    if (idx >= static_cast<int>(m_series.size())) idx = -1;
    m_highlightIdx = idx;
    update();
}

void ScatterPlotWidget::mousePressEvent(QMouseEvent* ev)
{
    for (int i = 0; i < static_cast<int>(m_legendRowRects.size()); ++i) {
        if (m_legendRowRects[i].contains(ev->pos())) {
            m_highlightIdx = (m_highlightIdx == i) ? -1 : i; // toggle
            update();
            return;
        }
    }
    QWidget::mousePressEvent(ev);
}

void ScatterPlotWidget::setScans(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    int maxPointsPerScan)
{
    m_series.clear();

    std::mt19937 rng(42);
    bool hasData = false;

    for (std::size_t si = 0; si < scans.size(); ++si) {
        const auto& scan = scans[si];
        if (!scan || !scan->tessellationMetricsComputed) continue;

        const auto& areas = scan->faceArea;
        const auto& curvs = scan->faceMeanCurv;
        if (areas.empty()) continue;

        std::vector<std::size_t> idx;
        idx.reserve(areas.size());
        for (std::size_t i = 0; i < areas.size(); ++i)
            if (areas[i] > 1e-10 && curvs[i] > 1e-10)
                idx.push_back(i);

        if (idx.empty()) continue;

        if (static_cast<int>(idx.size()) > maxPointsPerScan) {
            std::shuffle(idx.begin(), idx.end(), rng);
            idx.resize(maxPointsPerScan);
        }

        Series s;
        s.name  = scan->scannerName;
        s.color = seriesColor(static_cast<int>(si));
        s.xLog.reserve(idx.size());
        s.yLog.reserve(idx.size());
        for (auto i : idx) {
            s.xLog.push_back(static_cast<float>(std::log10(curvs[i])));
            s.yLog.push_back(static_cast<float>(std::log10(areas[i])));
        }
        m_series.push_back(std::move(s));
        hasData = true;
    }

    if (hasData) {
        m_xMin = m_xMax = m_series[0].xLog[0];
        m_yMin = m_yMax = m_series[0].yLog[0];
        for (const auto& s : m_series) {
            for (float v : s.xLog) { m_xMin = std::min(m_xMin,v); m_xMax = std::max(m_xMax,v); }
            for (float v : s.yLog) { m_yMin = std::min(m_yMin,v); m_yMax = std::max(m_yMax,v); }
        }
        // snap to decade boundaries with half-decade padding
        m_xMin = std::floor(m_xMin - 0.3f); m_xMax = std::ceil(m_xMax + 0.3f);
        m_yMin = std::floor(m_yMin - 0.3f); m_yMax = std::ceil(m_yMax + 0.3f);
    }

    update();
}

QImage ScatterPlotWidget::renderToImage(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    drawChart(p, w, h);
    p.end();
    return img;
}

void ScatterPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    drawChart(p, width(), height());
}

void ScatterPlotWidget::drawChart(QPainter& p, int W, int H) const
{
    p.fillRect(0, 0, W, H, Qt::white);

    if (m_series.empty()) {
        p.setPen(QColor(120, 120, 120));
        QFont f = p.font(); f.setPointSize(10); p.setFont(f);
        p.drawText(0, 0, W, H, Qt::AlignCenter,
                   "Run Analysis to compute tessellation fingerprint.");
        return;
    }

    // Margins
    const int mL = 82, mR = 165, mT = 36, mB = 72;
    int pw = W - mL - mR;
    int ph = H - mT - mB;
    if (pw < 60 || ph < 60) return;

    const float xRange = m_xMax - m_xMin;
    const float yRange = m_yMax - m_yMin;

    auto toPixX = [&](float v) -> int {
        return mL + static_cast<int>((v - m_xMin) / xRange * pw + 0.5f);
    };
    auto toPixY = [&](float v) -> int {
        return mT + ph - static_cast<int>((v - m_yMin) / yRange * ph + 0.5f);
    };

    // --- Grid ---
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(230, 230, 230), 1));
    for (int e = (int)m_xMin; e <= (int)m_xMax; ++e)
        p.drawLine(toPixX((float)e), mT, toPixX((float)e), mT + ph);
    for (int e = (int)m_yMin; e <= (int)m_yMax; ++e)
        p.drawLine(mL, toPixY((float)e), mL + pw, toPixY((float)e));

    // --- Border ---
    p.setPen(QPen(QColor(60, 60, 60), 1));
    p.drawRect(mL, mT, pw, ph);

    // --- Data points ---
    // When one series is highlighted: draw all others first (dimmed), then the
    // highlighted series on top at full opacity so it is never occluded.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    auto drawSeries = [&](int si, int alpha, int radius) {
        QColor c = m_series[si].color;
        c.setAlpha(alpha);
        p.setBrush(c);
        for (std::size_t i = 0; i < m_series[si].xLog.size(); ++i) {
            int px = toPixX(m_series[si].xLog[i]);
            int py = toPixY(m_series[si].yLog[i]);
            if (px < mL || px > mL+pw || py < mT || py > mT+ph) continue;
            p.drawEllipse(px - radius, py - radius, 2*radius, 2*radius);
        }
    };

    if (m_highlightIdx < 0 || m_highlightIdx >= static_cast<int>(m_series.size())) {
        for (int si = 0; si < static_cast<int>(m_series.size()); ++si)
            drawSeries(si, 80, 2);
    } else {
        for (int si = 0; si < static_cast<int>(m_series.size()); ++si)
            if (si != m_highlightIdx) drawSeries(si, 18, 1);
        drawSeries(m_highlightIdx, 200, 3);
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    // --- Tick labels ---
    QFont sFont = p.font();
    sFont.setPointSize(std::max(7, W / 110));
    p.setFont(sFont);
    p.setPen(Qt::black);
    QFontMetrics fm(sFont);

    auto tickLabel = [](int exp) -> QString {
        if      (exp ==  2) return "100";
        else if (exp ==  1) return "10";
        else if (exp ==  0) return "1";
        else if (exp == -1) return "0.1";
        else if (exp == -2) return "0.01";
        else if (exp == -3) return "0.001";
        else if (exp == -4) return "0.0001";
        else                return QString("1e%1").arg(exp);
    };

    for (int e = (int)m_xMin; e <= (int)m_xMax; ++e) {
        int x = toPixX((float)e);
        if (x < mL || x > mL+pw) continue;
        p.drawLine(x, mT+ph, x, mT+ph+5);
        QString lbl = tickLabel(e);
        int tw = fm.horizontalAdvance(lbl);
        p.drawText(x - tw/2, mT+ph+7, tw+2, fm.height()+4, Qt::AlignHCenter, lbl);
    }
    for (int e = (int)m_yMin; e <= (int)m_yMax; ++e) {
        int y = toPixY((float)e);
        if (y < mT || y > mT+ph) continue;
        p.drawLine(mL-5, y, mL, y);
        QString lbl = tickLabel(e);
        int tw = fm.horizontalAdvance(lbl);
        p.drawText(mL - tw - 8, y - fm.height()/2, tw+4, fm.height()+4, Qt::AlignRight, lbl);
    }

    // --- Axis titles ---
    QFont tFont = sFont; tFont.setPointSize(std::max(8, W / 90));
    p.setFont(tFont);

    // X title
    p.drawText(mL, H - fm.height() - 4, pw, fm.height()+4,
               Qt::AlignHCenter, "|Mean Curvature| (1/mm)");

    // Y title (rotated)
    p.save();
    p.translate(10, mT + ph / 2);
    p.rotate(-90);
    p.drawText(-ph/2, -fm.height()/2 - 2, ph, fm.height()+4,
               Qt::AlignHCenter, "Triangle Area (mm2)");
    p.restore();

    // Chart title
    QFont hFont = tFont; hFont.setBold(true); hFont.setPointSize(std::max(9, W / 75));
    p.setFont(hFont);
    p.drawText(mL, 4, pw, mT - 6, Qt::AlignHCenter | Qt::AlignVCenter,
               "Tessellation Fingerprint");
    p.setFont(sFont);

    // --- Legend ---
    int lx = mL + pw + 12;
    int ly = mT + 4;
    int rowH = 22;
    int lh = static_cast<int>(m_series.size()) * rowH + 12;
    int lw = mR - 18;

    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(QColor(255, 255, 255, 220));
    p.drawRect(lx, ly, lw, lh);

    m_legendRowRects.resize(m_series.size());
    for (std::size_t si = 0; si < m_series.size(); ++si) {
        int gy = ly + 6 + static_cast<int>(si) * rowH;
        QRect rowRect(lx + 2, gy, lw - 4, rowH - 2);
        m_legendRowRects[si] = rowRect;

        // Highlight background for the selected entry
        const bool selected = (static_cast<int>(si) == m_highlightIdx);
        if (selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 240, 150, 200));
            p.drawRect(rowRect);
        }

        // Colour swatch
        p.setPen(Qt::NoPen);
        p.setBrush(m_series[si].color);
        p.drawRect(lx + 6, gy + 3, 14, 12);

        // Label — bold when selected
        QFont lf = sFont;
        lf.setBold(selected);
        p.setFont(lf);
        p.setPen(Qt::black);
        p.drawText(lx + 25, gy, lw - 30, rowH - 2, Qt::AlignVCenter,
                   QString::fromStdString(m_series[si].name));
    }
    p.setFont(sFont); // restore

    // "Click to highlight" hint below the legend
    if (m_highlightIdx < 0) {
        QFont hintF = sFont; hintF.setPointSize(std::max(6, hintF.pointSize() - 1));
        p.setFont(hintF);
        p.setPen(QColor(130, 130, 130));
        p.drawText(lx, ly + lh + 2, lw, 20, Qt::AlignHCenter,
                   "click to highlight");
        p.setFont(sFont);
    }
}
