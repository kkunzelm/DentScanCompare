#pragma once

#include <QString>
#include <vtkSmartPointer.h>
#include <vtkRenderWindow.h>
#include <vtkImageData.h>

namespace ReportExporter {

// Export a VTK render window to PNG at the given DPI scaling factor.
// scale=1 → screen resolution; scale=3 → ~300 dpi on a 96dpi screen.
bool toPNG(vtkRenderWindow* window, const QString& filePath, int scale = 3);

// Export a vtkImageData (e.g. from ScatterPlotWidget::renderToImage) to PNG.
bool imageToPNG(vtkImageData* image, const QString& filePath);

// Export the scatter plot chart as SVG vector graphics.
// Requires IOExportGL2PS VTK module.
bool toSVG(vtkRenderWindow* window, const QString& filePath);

// Write a summary CSV from all metric reports.
// Reports are passed as CSV-formatted string (caller builds it from MetricsTableWidget).
bool toCSV(const QString& csvContent, const QString& filePath);

} // namespace ReportExporter
