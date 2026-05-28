#pragma once

#include "../core/MetricReport.h"
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <vector>

// A table widget showing all MetricReport values for all scanners.
// Rows = scanners, columns = metrics.
// Best value per column is highlighted green, worst red.
class MetricsTableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MetricsTableWidget(QWidget* parent = nullptr);
    ~MetricsTableWidget() override = default;

    // Replace all displayed reports.
    void setReports(const std::vector<MetricReport>& reports);
    void clear();

private slots:
    void exportCSV();

private:
    void populate(const std::vector<MetricReport>& reports);
    void highlightBestWorst();

    QTableWidget* m_table  = nullptr;
    QPushButton*  m_export = nullptr;
};
