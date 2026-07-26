cmake_minimum_required(VERSION 3.16)
# Phase 0+：Perlin 噪声区块 + 飞行相机（QtQuick3D 实例化）。代号 voxelsandbox（最终名待定，见 docs/PLAN.md §9）。
project(voxelsandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Quick Qml Quick3D)

qt_standard_project_setup()

qt_add_executable(voxelsandbox
    main.cpp
    blockregistry.cpp
    blockregistry.h
    chunkgeometry.cpp
    chunkgeometry.h
    world.cpp
    world.h
    playercontroller.cpp
    playercontroller.h
    raycast.cpp
    raycast.h
    wiresquare.cpp
    wiresquare.h
    hotbar.cpp
    hotbar.h
)

qt_add_qml_module(voxelsandbox
    URI VoxelSandbox
    VERSION 1.0
    QML_FILES
        Main.qml
        BlockParticles.qml
)

qt_add_resources(voxelsandbox "voxel_textures"
    PREFIX "/"
    FILES
        textures/atlas.png
        textures/default_cobble.png
        textures/default_dirt.png
        textures/default_grass_side.png
        textures/default_grass_top.png
        textures/default_leaves.png
        textures/default_sand.png
        textures/default_stone.png
        textures/default_tree.png
        textures/default_tree_top.png
        textures/default_wood.png
        textures/icon_cobble.png
        textures/icon_dirt.png
        textures/icon_grass.png
        textures/icon_leaves.png
        textures/icon_log.png
        textures/icon_planks.png
        textures/icon_sand.png
        textures/icon_stone.png
)

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

# 运行期部署（Windows）：windeployqt 把 Qt 运行时 + QML 模块插件拷进 exe 同级目录。
# --qmldir 扫描 QML 源目录的 import 语句，确保「按需引入」的 QML 模块（如 t14 的
# QtQuick3D.Particles3D）也被部署；否则新增 QML 依赖不会随旧依赖一起部署 → 运行期
# "模块没有安装" → Main.qml 加载失败 → app 退 -1。每次构建重跑，保证 fresh build 自洽。
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET voxelsandbox POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                    "$<TARGET_FILE:voxelsandbox>"
            COMMENT "Deploying Qt runtime + QML plugins (incl. Particles3D) to build dir")
    else()
        message(WARNING "windeployqt 未找到；Qt 运行时/QML 插件需手动部署")
    endif()
endif()
