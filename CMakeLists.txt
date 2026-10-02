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
find_package(Qt6Keychain 0.17 CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavutil)

# Linux-only reader for credentials saved by the previous libsecret backend.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
    find_package(Threads REQUIRED)
endif()

qt_standard_project_setup(REQUIRES 6.6)

add_library(nekotune_paths STATIC shared/app_paths.cpp)
target_include_directories(nekotune_paths PUBLIC ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(nekotune_paths PUBLIC Qt6::Core)

add_subdirectory(backend)
add_subdirectory(frontend/qt-qml)

enable_testing()
add_subdirectory(tests)
