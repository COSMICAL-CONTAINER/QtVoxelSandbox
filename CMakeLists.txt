cmake_minimum_required(VERSION 3.16)
# Phase 0+：用 QtQuick3D 显示一个贴图方块（公开 API，默认 D3D11 后端）。
# 待 Qt 私有头装上后做真正的体素渲染器（QRhi）。代号 voxelsandbox（最终名待定，见 docs/PLAN.md §9）。
project(voxelsandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Quick Qml Quick3D)

qt_standard_project_setup()

qt_add_executable(voxelsandbox
    main.cpp
)

qt_add_qml_module(voxelsandbox
    URI VoxelSandbox
    VERSION 1.0
    QML_FILES
        Main.qml
)

qt_add_resources(voxelsandbox "voxel_textures"
    PREFIX "/"
    FILES
        textures/default_cobble.png
)

# 先用控制台子系统，方便我自测崩溃；确认 OK 后切回 WIN32 GUI。
set_target_properties(voxelsandbox PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_link_libraries(voxelsandbox PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::Quick3D
)
