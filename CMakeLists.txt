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
    # t133 不完整方块异形几何（mesher 子系统，与 chunkgeometry 同层）：
    #   switch(blockId) 生成异形顶点（slab/stairs/fence/door/trapdoor/pressure plate），合批进 chunk mesh。
    #   放 src/World/ 而非 spec 建议的 src/Renderer/ —— 唯一调用者 chunkgeometry 在 World，放 Renderer 会
    #   造成 World→Renderer 向上依赖（破 PLAN §2 铁律，t41 同族先例）。骨架就绪、t134 填具体方块几何。
    src/World/partialblockgeometry.cpp
    src/World/partialblockgeometry.h
    src/World/world.cpp
    src/World/world.h
    src/World/worldclock.cpp
    src/World/worldclock.h
    src/Renderer/wiresquare.cpp
    src/Renderer/wiresquare.h
    src/Renderer/wirecube.cpp
    src/Renderer/wirecube.h
    src/Renderer/unitcube.cpp
    src/Renderer/unitcube.h
    src/Renderer/crackbox.cpp
    src/Renderer/crackbox.h
    src/Renderer/blockcube.cpp
    src/Renderer/blockcube.h
    src/Renderer/pickaxe.cpp
    src/Renderer/pickaxe.h
    # t112 广告牌四边形（单面 +Z 法线）：煤/木炭/铁原矿/木棒/铁锭/玻璃 掉落实体材料段
    #   从 CrackBox 6 面立方改为单面平图标 + 朝相机（billboard），消除「6 面未封闭超立方体」观感。
    src/Renderer/billboardquad.cpp
    src/Renderer/billboardquad.h
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
    # t95 统一实体管理器（Entities 层）：测试生物 / 玩家推动 / 重力。为后续 Mob/AI 系统铺垫统一基类。
    src/Entities/entitymanager.cpp
    src/Entities/entitymanager.h
    src/Game/raycast.cpp
    src/Game/raycast.h
    src/Game/recipe.cpp
    src/Game/recipe.h
    src/Game/smelting.cpp
    src/Game/smelting.h
    # t89 音效（Core/Platform 层）：miniaudio 单头 + AudioManager 封装。
    #   miniaudio.h 是 vendored 第三方单头（~4MB），仅 miniaudio_impl.cpp 含 MINIAUDIO_IMPLEMENTATION；
    #   audiomanager.h/cpp 是项目自有代码（pimpl 隔离重头）。
    src/Audio/audiomanager.cpp
    src/Audio/audiomanager.h
    src/Audio/miniaudio_impl.cpp
)

# 分层（PLAN §2 铁律：依赖只向下）。把各层子目录加入 include 搜索路径，使
# 既有的 `#include "xxx.h"`（无子目录前缀）跨目录仍可解析——这是「搬迁中性重构」
# 的关键：移动文件不改动任何 #include 语句，行为零回归。
#   src/Core      ← math/数据原语（blockregistry）
#   src/World     ← chunks/worldgen/mesher 数据层
#   src/Renderer  ← QQuick3DGeometry 呈现层
#   src/Game      ← 玩家/物品/工具/射线（Physics-Game 逻辑层）
#   src/Entities  ← 统一实体（测试生物 / 掉落物基类；t95）
#   src/Audio     ← miniaudio 封装（Core/Platform 层，t89）
target_include_directories(voxelsandbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/World
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Entities
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Audio
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
        src/ui/FurnaceUI.qml
        src/ui/VitalIcon.qml
        src/ui/ToolIcon.qml
        src/ui/MaterialIcon.qml
        src/ui/ArmSlider.qml
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
        textures/default_coal_ore.png
        textures/default_crafting_table_side.png
        textures/default_crafting_table_top.png
        textures/default_dirt.png
        textures/default_furnace_front.png
        textures/default_furnace_side.png
        textures/default_furnace_top.png
        textures/default_grass_side.png
        textures/default_grass_top.png
        textures/default_iron_ore.png
        textures/default_leaves.png
        textures/default_bedrock.png
        textures/default_sand.png
        textures/default_stone.png
        textures/default_tree.png
        textures/default_tree_top.png
        textures/default_wood.png
        textures/default_torch.png
        textures/icon_cobble.png
        textures/icon_coal_ore.png
        textures/icon_crafting_table.png
        textures/icon_dirt.png
        textures/icon_furnace.png
        textures/icon_grass.png
        textures/icon_iron_ore.png
        textures/icon_leaves.png
        textures/icon_log.png
        textures/icon_planks.png
        textures/icon_sand.png
        textures/icon_stone.png
        textures/icon_torch.png
)

# t89 / t118 音效资源（tools/build_sounds.py 程序合成，§4 原创 SFX；零 MC 资产）。
# 经 qrc 打包（:/sounds/*.wav）；AudioManager 从 qrc 读 WAV → miniaudio 解码播放。
# 注：PLAN §2-L 终态是从磁盘加载（便于热补丁），本任务先 qrc 跑通 + 留接口（见 audiomanager.cpp）。
# t118：按方块材质分组（石/木/草/沙/叶）clip 池 —— break/mining/step 各 5 个变体（按 BlockRegistry::
# materialGroup 选播，spec「按方块材质分组」）；place / pickup 单件（放置 / 拾取不分材质）。
qt_add_resources(voxelsandbox "voxel_sounds"
    PREFIX "/"
    FILES
        sounds/break_stone.wav
        sounds/break_wood.wav
        sounds/break_grass.wav
        sounds/break_sand.wav
        sounds/break_leaves.wav
        sounds/mining_stone.wav
        sounds/mining_wood.wav
        sounds/mining_grass.wav
        sounds/mining_sand.wav
        sounds/mining_leaves.wav
        sounds/step_stone.wav
        sounds/step_wood.wav
        sounds/step_grass.wav
        sounds/step_sand.wav
        sounds/step_leaves.wav
        sounds/place.wav
        sounds/pickup.wav
)

# miniaudio 是 vendored 第三方单头库（src/Audio/miniaudio.h）。
# PLAN §4 允许抑制第三方头警告（该零警告门只扫项目自有代码）。
# miniaudio_impl.cpp 含 MINIAUDIO_IMPLEMENTATION，编进整个实现体 → 整 TU 关警告（-w）。
# audiomanager.cpp 仅 include 头（已在源内用 #pragma GCC diagnostic 包裹 include 抑制）。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Audio/miniaudio_impl.cpp
        PROPERTIES COMPILE_OPTIONS "-w")
endif()

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
