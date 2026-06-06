// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>

#include "MetricsTableWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QColor>
#include <QTableWidgetItem>
#include <cmath>
#include <limits>

// Column definitions: {header, tooltip, lower-is-better}
static const struct ColDef { const char* name; const char* tip; bool lowerBetter; } kCols[] = {
    {"Scanner",            "Scanner name",                                                 false},
    {"Triangles",          "Total triangle count",                                         false},
    {"Edge [mm]",          "Mean edge length [mm]",                                        false},
    {"AspRatio",           "Mean triangle aspect ratio (max/min edge)",                    true },
    {"ATI",                "Adaptive Tessellation Index: Spearman(|κ|, 1/area). 1=best",  false},
    {"DensHighκ [/mm²]",   "Triangle density in high-curvature zones",                    false},
    {"DensLowκ [/mm²]",    "Triangle density in low-curvature zones",                     false},
    {"RMS [mm]",           "RMS distance to GPA reference [mm]",                          true },
    {"MAD [mm]",           "Median absolute distance to GPA reference [mm]",              true },
    {"H95 [mm]",           "95th percentile Hausdorff distance [mm]",                     true },
    {"H100 [mm]",          "Maximum Hausdorff distance [mm]",                             true },
    {"Bias [mm]",          "Signed mean distance (positive = oversized scan) [mm]",       false},
    {"Coverage%",          "% of reference surface within 0.2 mm",                        false},
    {"Boundary [mm]",      "Total open boundary length [mm]",                             true },
    {"Holes",              "Number of topological holes",                                  true },
    {"Stitch [°]",         "Max normal discontinuity angle (stitching artifact) [°]",     true },
};
static const int kNumCols = sizeof(kCols) / sizeof(kCols[0]);

MetricsTableWidget::MetricsTableWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kNumCols);
    QStringList headers;
    for (int c = 0; c < kNumCols; ++c) {
        headers << kCols[c].name;
    }
    m_table->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < kNumCols; ++c)
        m_table->horizontalHeaderItem(c)->setToolTip(kCols[c].tip);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    mainLayout->addWidget(m_table);

    auto* btnRow = new QHBoxLayout;
    m_export = new QPushButton("Export CSV…", this);
    connect(m_export, &QPushButton::clicked, this, &MetricsTableWidget::exportCSV);
    btnRow->addStretch();
    btnRow->addWidget(m_export);
    mainLayout->addLayout(btnRow);
}

void MetricsTableWidget::setReports(const std::vector<MetricReport>& reports)
{
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(reports.size()));
    populate(reports);
    highlightBestWorst();
}

void MetricsTableWidget::clear()
{
    m_table->clearContents();
    m_table->setRowCount(0);
}

static QString fmt(double v, int decimals = 3)
{
    if (std::isnan(v)) return "—";
    return QString::number(v, 'f', decimals);
}

void MetricsTableWidget::populate(const std::vector<MetricReport>& reports)
{
    for (int r = 0; r < static_cast<int>(reports.size()); ++r) {
        const auto& rp = reports[r];
        int c = 0;
        auto set = [&](const QString& text) {
            m_table->setItem(r, c++, new QTableWidgetItem(text));
        };
        set(QString::fromStdString(rp.scannerName));
        set(QString::number(static_cast<qulonglong>(rp.triangleCount)));
        set(fmt(rp.meanEdgeLength));
        set(fmt(rp.meanAspectRatio, 2));
        set(fmt(rp.ati, 3));
        set(fmt(rp.densityHighCurv, 1));
        set(fmt(rp.densityLowCurv, 1));
        set(fmt(rp.rmsDistance));
        set(fmt(rp.madDistance));
        set(fmt(rp.hausdorff95));
        set(fmt(rp.hausdorff100));
        set(fmt(rp.signedMean));
        set(fmt(rp.coverageRate, 1));
        set(fmt(rp.openBoundaryLength, 1));
        set(QString::number(rp.holeCount));
        set(fmt(rp.maxStitchingAngle, 1));
    }
}

void MetricsTableWidget::highlightBestWorst()
{
    int nRows = m_table->rowCount();
    if (nRows < 2) return;

    // highlight columns 3..kNumCols-1 (skip scanner name and triangle count)
    for (int c = 2; c < kNumCols; ++c) {
        double best  = std::numeric_limits<double>::infinity();
        double worst = -std::numeric_limits<double>::infinity();
        int bestRow = -1, worstRow = -1;

        for (int r = 0; r < nRows; ++r) {
            auto* item = m_table->item(r, c);
            if (!item) continue;
            bool ok; double v = item->text().toDouble(&ok);
            if (!ok) continue;
            if (v < best)  { best = v;  bestRow  = r; }
            if (v > worst) { worst = v; worstRow = r; }
        }

        if (bestRow  >= 0) m_table->item(bestRow,  c)->setBackground(QColor(180, 230, 180));
        if (worstRow >= 0 && worstRow != bestRow)
            m_table->item(worstRow, c)->setBackground(QColor(255, 180, 180));
    }
}

bool MetricsTableWidget::exportToFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out.setGenerateByteOrderMark(true);
    for (int c = 0; c < m_table->columnCount(); ++c) {
        out << m_table->horizontalHeaderItem(c)->text();
        out << (c < m_table->columnCount() - 1 ? "," : "\n");
    }
    for (int r = 0; r < m_table->rowCount(); ++r) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto* item = m_table->item(r, c);
            out << (item ? item->text() : "");
            out << (c < m_table->columnCount() - 1 ? "," : "\n");
        }
    }
    return true;
}

void MetricsTableWidget::exportCSV()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Export Metrics", "DentScanCompare_metrics.csv",
        "CSV files (*.csv)");
    if (path.isEmpty()) return;
    exportToFile(path);
}
