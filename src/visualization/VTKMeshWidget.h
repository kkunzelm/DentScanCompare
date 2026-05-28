#pragma once

#include "../core/Mesh.h"
#include <QWidget>
#include <QLabel>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkScalarBarActor.h>
#include <vtkLookupTable.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <memory>

class QVTKOpenGLNativeWidget;

// A Qt widget embedding a single VTK 3D viewport for one ScanData mesh.
// Supports plain Phong shading and color-mapped distance display.
class VTKMeshWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VTKMeshWidget(QWidget* parent = nullptr);
    ~VTKMeshWidget() override = default;

    // Load mesh geometry from ScanData (converts CGAL → vtkPolyData).
    void setMesh(const std::shared_ptr<ScanData>& scan);

    // Switch to distance-map display using scan->distanceToRef.
    void showDistanceMap(const std::shared_ptr<ScanData>& scan,
                         double rangeMin = -1.0, double rangeMax = 1.0);

    // Reset to plain Phong shading.
    void showPhongShading();

    // Show/hide the scalar color bar.
    void setColorBarVisible(bool visible);

    // Expose the renderer for camera synchronisation.
    vtkRenderer* renderer() const { return m_renderer; }

    QString title() const;
    void    setTitle(const QString& t);

    // Reset camera to fit the current actor.
    void resetCamera();

private:
    void buildPipeline();

    // Converts CGAL SurfaceMesh to vtkPolyData (with normals).
    static vtkSmartPointer<vtkPolyData> cgalToVTK(const SurfaceMesh& mesh);

    QLabel*                  m_titleLabel  = nullptr;
    QVTKOpenGLNativeWidget*  m_vtkWidget   = nullptr;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer>     m_renderer;
    vtkSmartPointer<vtkPolyData>     m_polyData;
    vtkSmartPointer<vtkPolyDataMapper> m_mapper;
    vtkSmartPointer<vtkActor>        m_actor;
    vtkSmartPointer<vtkScalarBarActor> m_colorBar;
};
