cmake_minimum_required(VERSION 3.16)

project(KardsAutoBot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

qt_standard_project_setup()

set(PROJECT_SOURCES
    src/main.cpp
    src/appconfig.h
    src/appconfig.cpp
    src/screencapture.h
    src/screencapture.cpp
    src/matcher.h
    src/matcher.cpp
    src/clicker.h
    src/clicker.cpp
    src/botengine.h
    src/botengine.cpp
    src/roipicker.h
    src/roipicker.cpp
    src/mainwindow.h
    src/mainwindow.cpp
)

qt_add_executable(KardsAutoBot ${PROJECT_SOURCES})

target_include_directories(KardsAutoBot PRIVATE src)

target_link_libraries(KardsAutoBot PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
)

if(WIN32)
    target_link_libraries(KardsAutoBot PRIVATE user32 gdi32)
    set_target_properties(KardsAutoBot PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

