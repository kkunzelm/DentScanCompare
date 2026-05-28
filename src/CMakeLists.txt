set(SOURCES
    main.cpp
    MainWindow.cpp
    core/STLReader.cpp
    core/CurvatureAnalysis.cpp
    core/TessellationMetrics.cpp
    core/ICPRegistration.cpp
    core/GPAReference.cpp
    core/DistanceField.cpp
    core/ArchMetrics.cpp
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
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::PrintSupport
    Qt5::OpenGL
    ${VTK_LIBRARIES}
    CGAL::CGAL
    Eigen3::Eigen
)

vtk_module_autoinit(
    TARGETS DentScanCompare
    MODULES ${VTK_LIBRARIES}
)
