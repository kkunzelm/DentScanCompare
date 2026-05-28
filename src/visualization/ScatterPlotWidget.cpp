#include "ScatterPlotWidget.h"

#include <QVTKOpenGLNativeWidget.h>
#include <QVBoxLayout>

#include <vtkContextScene.h>
#include <vtkChartLegend.h>
#include <vtkPlotPoints.h>
#include <vtkTable.h>
#include <vtkFloatArray.h>
#include <vtkAxis.h>
#include <vtkTextProperty.h>
#include <vtkWindowToImageFilter.h>
#include <vtkImageData.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

ScatterPlotWidget::ScatterPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    buildPipeline();
}

void ScatterPlotWidget::buildPipeline()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    layout->addWidget(m_vtkWidget);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_contextView = vtkSmartPointer<vtkContextView>::New();
    m_contextView->SetRenderWindow(m_renderWindow);
    m_contextView->GetRenderer()->SetBackground(1.0, 1.0, 1.0);

    m_chart = vtkSmartPointer<vtkChartXY>::New();
    m_chart->SetShowLegend(true);
    m_chart->GetLegend()->GetLabelProperties()->SetFontSize(11);
    m_contextView->GetScene()->AddItem(m_chart);

    auto* axBot = m_chart->GetAxis(vtkAxis::BOTTOM);
    axBot->SetTitle("|Mean Curvature| κ_H [1/mm]");
    axBot->GetTitleProperties()->SetFontSize(12);
    axBot->GetLabelProperties()->SetFontSize(10);
    axBot->LogScaleOn();

    auto* axLeft = m_chart->GetAxis(vtkAxis::LEFT);
    axLeft->SetTitle("Triangle Area [mm²]");
    axLeft->GetTitleProperties()->SetFontSize(12);
    axLeft->GetLabelProperties()->SetFontSize(10);
    axLeft->LogScaleOn();
}

void ScatterPlotWidget::setScans(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    int maxPointsPerScan)
{
    m_chart->ClearPlots();
    if (scans.empty()) { m_renderWindow->Render(); return; }

    auto colors = ColorMapLUT::scannerColors();
    std::mt19937 rng(42);

    for (std::size_t si = 0; si < scans.size(); ++si) {
        const auto& scan = scans[si];
        if (!scan->tessellationMetricsComputed) continue;

        const auto& areas = scan->faceArea;
        const auto& curvs = scan->faceMeanCurv;
        if (areas.empty()) continue;

        // build index list (filter zeros)
        std::vector<std::size_t> idx;
        idx.reserve(areas.size());
        for (std::size_t i = 0; i < areas.size(); ++i) {
            if (areas[i] > 1e-10 && curvs[i] > 1e-10)
                idx.push_back(i);
        }

        // subsample if needed
        if (static_cast<int>(idx.size()) > maxPointsPerScan) {
            std::shuffle(idx.begin(), idx.end(), rng);
            idx.resize(maxPointsPerScan);
        }

        auto xArr = vtkSmartPointer<vtkFloatArray>::New();
        xArr->SetName("|κ_H|");
        auto yArr = vtkSmartPointer<vtkFloatArray>::New();
        yArr->SetName(scan->scannerName.c_str());

        xArr->SetNumberOfValues(static_cast<vtkIdType>(idx.size()));
        yArr->SetNumberOfValues(static_cast<vtkIdType>(idx.size()));
        for (std::size_t i = 0; i < idx.size(); ++i) {
            xArr->SetValue(static_cast<vtkIdType>(i),
                           static_cast<float>(curvs[idx[i]]));
            yArr->SetValue(static_cast<vtkIdType>(i),
                           static_cast<float>(areas[idx[i]]));
        }

        auto table = vtkSmartPointer<vtkTable>::New();
        table->AddColumn(xArr);
        table->AddColumn(yArr);

        auto* plot = vtkPlotPoints::SafeDownCast(
            m_chart->AddPlot(vtkChart::POINTS));
        plot->SetInputData(table, 0, 1);

        std::size_t ci = si % colors.size();
        plot->SetColor(static_cast<unsigned char>(colors[ci][0] * 255),
                       static_cast<unsigned char>(colors[ci][1] * 255),
                       static_cast<unsigned char>(colors[ci][2] * 255),
                       180);
        plot->SetMarkerSize(1.5f);
        plot->SetMarkerStyle(vtkPlotPoints::CIRCLE);
    }

    m_renderWindow->Render();
}

void ScatterPlotWidget::clear()
{
    m_chart->ClearPlots();
    m_renderWindow->Render();
}

vtkSmartPointer<vtkImageData> ScatterPlotWidget::renderToImage(
    int width, int height)
{
    auto prevSize = m_renderWindow->GetSize();
    m_renderWindow->SetSize(width, height);
    m_renderWindow->Render();

    auto filter = vtkSmartPointer<vtkWindowToImageFilter>::New();
    filter->SetInput(m_renderWindow);
    filter->SetScale(1);
    filter->ReadFrontBufferOff();
    filter->Update();

    auto img = vtkSmartPointer<vtkImageData>::New();
    img->DeepCopy(filter->GetOutput());

    m_renderWindow->SetSize(prevSize[0], prevSize[1]);
    m_renderWindow->Render();
    return img;
}
