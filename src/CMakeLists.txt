set(SOURCES
    main.cpp
    MainWindow.cpp
    core/STLReader.cpp
    core/MeshReader.cpp
    core/CurvatureAnalysis.cpp
    core/TessellationMetrics.cpp
    core/ICPRegistration.cpp
    core/GPAReference.cpp
    core/DistanceField.cpp
    core/ArchMetrics.cpp
    core/ToothSegmentation.cpp
    visualization/VTKMeshWidget.cpp
    visualization/ColorMapLUT.cpp
    visualization/ScatterPlotWidget.cpp
    visualization/MetricsTableWidget.cpp
    export/ReportExporter.cpp
    export/VTKSceneExporter.cpp
)

add_executable(DentScanCompare ${SOURCES})

target_include_directories(DentScanCompare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DentScanCompare PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
    CGAL::CGAL
    Eigen3::Eigen
)

vtk_module_autoinit(
    TARGETS DentScanCompare
    MODULES ${VTK_LIBRARIES}
)
