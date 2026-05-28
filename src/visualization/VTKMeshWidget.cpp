#include "VTKMeshWidget.h"
#include "ColorMapLUT.h"

#include <QVTKOpenGLNativeWidget.h>
#include <QVBoxLayout>

#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkPolyDataNormals.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkTextProperty.h>

VTKMeshWidget::VTKMeshWidget(QWidget* parent)
    : QWidget(parent)
{
    buildPipeline();
}

void VTKMeshWidget::buildPipeline()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    layout->addWidget(m_titleLabel);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setMinimumSize(280, 280);
    layout->addWidget(m_vtkWidget);

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.15, 0.15, 0.15);
    m_renderWindow->AddRenderer(m_renderer);

    // pipeline
    m_polyData = vtkSmartPointer<vtkPolyData>::New();
    m_mapper   = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapper->SetInputData(m_polyData);
    m_mapper->ScalarVisibilityOff();

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);
    m_actor->GetProperty()->SetAmbient(0.1);
    m_actor->GetProperty()->SetDiffuse(0.7);
    m_actor->GetProperty()->SetSpecular(0.3);
    m_actor->GetProperty()->SetSpecularPower(30.0);
    m_renderer->AddActor(m_actor);

    // scalar bar (hidden by default)
    m_colorBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_colorBar->SetOrientationToVertical();
    m_colorBar->SetWidth(0.08);
    m_colorBar->SetHeight(0.6);
    m_colorBar->GetPositionCoordinate()->SetValue(0.88, 0.2);
    m_colorBar->GetTitleTextProperty()->SetFontSize(10);
    m_colorBar->GetLabelTextProperty()->SetFontSize(8);
    m_colorBar->VisibilityOff();
    m_renderer->AddActor2D(m_colorBar);

    // interactor style
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    m_vtkWidget->interactor()->SetInteractorStyle(style);
}

// static
vtkSmartPointer<vtkPolyData> VTKMeshWidget::cgalToVTK(const SurfaceMesh& mesh)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToFloat();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.num_vertices()));

    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        points->SetPoint(static_cast<vtkIdType>(v.idx()),
                         p.x(), p.y(), p.z());
    }

    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        vtkIdType ids[3];
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h))
            ids[i++] = static_cast<vtkIdType>(vd.idx());
        cells->InsertNextCell(3, ids);
    }

    auto polydata = vtkSmartPointer<vtkPolyData>::New();
    polydata->SetPoints(points);
    polydata->SetPolys(cells);

    // compute smooth normals
    auto normFilter = vtkSmartPointer<vtkPolyDataNormals>::New();
    normFilter->SetInputData(polydata);
    normFilter->ComputePointNormalsOn();
    normFilter->ComputeCellNormalsOff();
    normFilter->SplittingOff();
    normFilter->Update();

    return normFilter->GetOutput();
}

void VTKMeshWidget::setMesh(const std::shared_ptr<ScanData>& scan)
{
    if (!scan) return;

    auto pd = cgalToVTK(scan->mesh);
    m_polyData->DeepCopy(pd);
    m_mapper->SetInputData(m_polyData);
    m_mapper->ScalarVisibilityOff();
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);
    m_colorBar->VisibilityOff();

    std::string info = scan->scannerName
        + "\n" + std::to_string(scan->triangleCount) + " triangles";
    if (!scan->stlHeader.empty())
        info += "\n[" + scan->stlHeader + "]";
    m_titleLabel->setText(QString::fromStdString(info));

    resetCamera();
    m_renderWindow->Render();
}

void VTKMeshWidget::showDistanceMap(const std::shared_ptr<ScanData>& scan,
                                     double rangeMin, double rangeMax)
{
    if (!scan || !scan->distanceComputed) return;

    // copy distance values into vtkDoubleArray
    auto scalars = vtkSmartPointer<vtkDoubleArray>::New();
    scalars->SetName("Distance [mm]");
    scalars->SetNumberOfValues(
        static_cast<vtkIdType>(scan->distanceToRef.size()));
    for (std::size_t i = 0; i < scan->distanceToRef.size(); ++i)
        scalars->SetValue(static_cast<vtkIdType>(i), scan->distanceToRef[i]);

    m_polyData->GetPointData()->SetScalars(scalars);

    auto lut = ColorMapLUT::divergingBWR(rangeMin, rangeMax);
    m_mapper->SetLookupTable(lut);
    m_mapper->SetScalarRange(rangeMin, rangeMax);
    m_mapper->ScalarVisibilityOn();

    m_colorBar->SetLookupTable(lut);
    m_colorBar->SetTitle("mm");
    m_colorBar->VisibilityOn();

    m_renderWindow->Render();
}

void VTKMeshWidget::showPhongShading()
{
    m_mapper->ScalarVisibilityOff();
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);
    m_colorBar->VisibilityOff();
    m_renderWindow->Render();
}

void VTKMeshWidget::setColorBarVisible(bool visible)
{
    m_colorBar->SetVisibility(visible ? 1 : 0);
    m_renderWindow->Render();
}

QString VTKMeshWidget::title() const
{
    return m_titleLabel->text();
}

void VTKMeshWidget::setTitle(const QString& t)
{
    m_titleLabel->setText(t);
}

void VTKMeshWidget::resetCamera()
{
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}
