cmake_minimum_required(VERSION 3.16)
project(faultsim_gui LANGUAGES CXX)

# Enable Qt's automoc for QObject classes
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer system Qt6 if available
set(_QT6_HINT "/usr/lib/x86_64-linux-gnu/cmake/Qt6")
if(EXISTS "${_QT6_HINT}")
    list(PREPEND CMAKE_PREFIX_PATH "${_QT6_HINT}")
endif()

# Try Qt6 first, then Qt5
find_package(Qt6 COMPONENTS Widgets Concurrent CONFIG QUIET)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 COMPONENTS Widgets Concurrent REQUIRED)
    set(QT_VERSION_MAJOR 5)
endif()

add_executable(faultsim_gui
    src/main.cpp
    src/MainWindow.cpp
    src/domain/MarchModel.cpp
    src/models/ElementsModel.cpp
    src/models/OpsModel.cpp
    src/services/LookAheadService.cpp
)

target_include_directories(faultsim_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(faultsim_gui PRIVATE Qt6::Widgets Qt6::Concurrent)
else()
    target_link_libraries(faultsim_gui PRIVATE Qt5::Widgets Qt5::Concurrent)
endif()

set_target_properties(faultsim_gui PROPERTIES
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF
)
