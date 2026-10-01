cmake_minimum_required(VERSION 3.28)

project(NekoTune
    VERSION 0.1.0
    DESCRIPTION "Local-first Linux music player with a decoupled C++ backend"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Sql
    Multimedia
    Quick
    Qml
    QuickControls2
    Test
)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.6)

add_subdirectory(backend)
add_subdirectory(frontend/qt-qml)

enable_testing()
add_subdirectory(tests)
