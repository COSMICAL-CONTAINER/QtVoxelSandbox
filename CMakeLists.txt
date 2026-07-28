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
    src/Core/blockregistry.cpp
    src/Core/blockregistry.h
    src/World/chunk.cpp
    src/World/chunk.h
    src/World/chunkmanager.cpp
    src/World/chunkmanager.h
    src/World/chunkgeometry.cpp
    src/World/chunkgeometry.h
    src/World/world.cpp
    src/World/world.h
    src/World/worldclock.cpp
    src/World/worldclock.h
    src/Renderer/wiresquare.cpp
    src/Renderer/wiresquare.h
    src/Renderer/unitcube.cpp
    src/Renderer/unitcube.h
    src/Renderer/crackbox.cpp
    src/Renderer/crackbox.h
    src/Renderer/blockcube.cpp
    src/Renderer/blockcube.h
    src/Game/playercontroller.cpp
    src/Game/playercontroller.h
    src/Game/playerstate.cpp
    src/Game/playerstate.h
    src/Game/hotbar.cpp
    src/Game/hotbar.h
    src/Game/toolregistry.cpp
    src/Game/toolregistry.h
    src/Game/itementitymanager.cpp
    src/Game/itementitymanager.h
    src/Game/raycast.cpp
    src/Game/raycast.h
    src/Game/recipe.cpp
    src/Game/recipe.h
)

# 分层（PLAN §2 铁律：依赖只向下）。把各层子目录加入 include 搜索路径，使
# 既有的 `#include "xxx.h"`（无子目录前缀）跨目录仍可解析——这是「搬迁中性重构」
# 的关键：移动文件不改动任何 #include 语句，行为零回归。
#   src/Core      ← math/数据原语（blockregistry）
#   src/World     ← chunks/worldgen/mesher 数据层
#   src/Renderer  ← QQuick3DGeometry 呈现层
#   src/Game      ← 玩家/物品/工具/射线（Physics-Game 逻辑层）
target_include_directories(voxelsandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/World
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game
)

qt_add_qml_module(voxelsandbox
    URI VoxelSandbox
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/BlockParticles.qml
        src/ui/MainMenu.qml
        src/ui/Inventory.qml
        src/ui/InvSlot.qml
        src/ui/SurvivalInventory.qml
        src/ui/CraftingTableUI.qml
        src/ui/VitalIcon.qml
        src/ui/ToolIcon.qml
        src/ui/MaterialIcon.qml
)

qt_add_resources(voxelsandbox "voxel_textures"
    PREFIX "/"
    FILES
        textures/atlas.png
        textures/crack_0.png
        textures/crack_1.png
        textures/crack_2.png
        textures/crack_3.png
        textures/crack_4.png
        textures/crack_5.png
        textures/default_cobble.png
        textures/default_crafting_table_side.png
        textures/default_crafting_table_top.png
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
        textures/icon_crafting_table.png
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
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
                    "$<TARGET_FILE:voxelsandbox>"
            COMMENT "Deploying Qt runtime + QML plugins (incl. Particles3D) to build dir")
    else()
        message(WARNING "windeployqt 未找到；Qt 运行时/QML 插件需手动部署")
    endif()
endif()
