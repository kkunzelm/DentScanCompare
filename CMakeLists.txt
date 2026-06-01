cmake_minimum_required(VERSION 3.20)
project(DentScanCompare VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent PrintSupport OpenGL OpenGLWidgets)

# VTK 9.3 linked against Qt6; MPI may be listed as transitive dependency
# even when not used — create a dummy target if MPI is not found.
find_package(MPI QUIET)
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)
endif()

find_package(VTK 9.3 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    CommonColor
    RenderingCore
    RenderingOpenGL2
    RenderingAnnotation
    RenderingContext2D
    RenderingContextOpenGL2
    GUISupportQt
    InteractionStyle
    ChartsCore
    ViewsContext2D
    IOExport
    IOExportGL2PS
    IOImage
    IOPLY
)

find_package(CGAL 6.0 REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

add_subdirectory(src)
