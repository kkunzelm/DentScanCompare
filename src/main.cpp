#include "MainWindow.h"
#include <QApplication>
#include <QVTKOpenGLNativeWidget.h>
#include <QSurfaceFormat>

int main(int argc, char* argv[])
{
    // VTK requires the OpenGL surface format to be set before QApplication.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    app.setApplicationName("DentScanCompare");
    app.setOrganizationName("DentalResearch");
    app.setApplicationVersion("1.0");

    MainWindow w;
    w.show();
    return app.exec();
}
