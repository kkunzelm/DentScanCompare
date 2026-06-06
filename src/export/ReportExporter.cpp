// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Prof. Dr. Karl-Heinz Kunzelmann <www.kunzelmann.de>

#include "ReportExporter.h"

#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkGL2PSExporter.h>

#include <QFile>
#include <QTextStream>

namespace ReportExporter {

bool toPNG(vtkRenderWindow* window, const QString& filePath, int scale)
{
    if (!window) return false;

    auto filter = vtkSmartPointer<vtkWindowToImageFilter>::New();
    filter->SetInput(window);
    filter->SetScale(scale);
    filter->ReadFrontBufferOff();
    window->Render();
    filter->Update();

    auto writer = vtkSmartPointer<vtkPNGWriter>::New();
    writer->SetFileName(filePath.toLocal8Bit().constData());
    writer->SetInputConnection(filter->GetOutputPort());
    writer->Write();
    return true;
}

bool imageToPNG(vtkImageData* image, const QString& filePath)
{
    if (!image) return false;
    auto writer = vtkSmartPointer<vtkPNGWriter>::New();
    writer->SetFileName(filePath.toLocal8Bit().constData());
    writer->SetInputData(image);
    writer->Write();
    return true;
}

bool toSVG(vtkRenderWindow* window, const QString& filePath)
{
    if (!window) return false;
    auto exporter = vtkSmartPointer<vtkGL2PSExporter>::New();
    exporter->SetRenderWindow(window);
    exporter->SetFileFormat(vtkGL2PSExporter::SVG_FILE);
    exporter->SetFilePrefix(filePath.toLocal8Bit().constData());
    exporter->SetTitle("DentScanCompare");
    exporter->Write();
    return true;
}

bool toCSV(const QString& csvContent, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << csvContent;
    return true;
}

} // namespace ReportExporter
