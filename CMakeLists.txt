cmake_minimum_required(VERSION 3.16)
# Phase 0+：Perlin 噪声区块 + 飞行相机（QtQuick3D 实例化）。代号 voxelsandbox（最终名待定，见 docs/PLAN.md §9）。
project(voxelsandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Quick Qml Quick3D Sql)

qt_standard_project_setup()

qt_add_executable(voxelsandbox
    main.cpp
    src/Core/blockregistry.cpp
    src/Core/blockregistry.h
    # t232 世界列表封面黑屏修复：QQuickWindow::grabWindow 封装（grabToImage 拍不到 View3D 3D 场景 → 全黑）。
    #   仅依赖 Qt（Quick/Gui/Qml），无自有层依赖 → Core 叶子工具，依赖只向下铁律成立。
    src/Core/screengrab.cpp
    src/Core/screengrab.h
    src/World/chunk.cpp
    src/World/chunk.h
    src/World/chunkmanager.cpp
    src/World/chunkmanager.h
    src/World/chunkgeometry.cpp
    src/World/chunkgeometry.h
    # t257 体素光照 / PCF 软影单点实现（mesher 与 BlockCube 掉落沙共用同一光场 + 软影公式）：
    #   header-only inline，属 World 层（只读 World 光场 / heightmap），Renderer（BlockCube）向下依赖合规。
    src/World/voxellight.h
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
    # t176 存档系统（SQLite，PLAN §2-L）：自描述 chunk blob（voxels+state+light）+ 玩家态 JSON +
    #   迁移注册表（PRAGMA user_version）。World 层（依赖只向下：Qt Sql + Chunk/ChunkManager/World），
    #   玩家态经裸 QVariantMap 边界传入（不 include Game 层 playercontroller/hotbar，保依赖只向下）。
    src/World/worldstore.cpp
    src/World/worldstore.h
    src/Renderer/wiresquare.cpp
    src/Renderer/wiresquare.h
    src/Renderer/wirecube.cpp
    src/Renderer/wirecube.h
    # t277 F3 区块边界显示（机制等价 MC F3+G chunk boundary）：按世界尺寸 + chunkSize 画 16×16 区块
    #   边界线框叠层（纵线 + 顶/底水平连线 = 完整格线框）。Main.qml chunkBounds Model 绑此几何 +
    #   showChunkBounds（F3+G toggle）。Renderer 层（只按尺寸产出几何，不读栅格 / worldgen）。
    src/Renderer/chunkgridlines.cpp
    src/Renderer/chunkgridlines.h
    # t146 异形方块选中框线框（按 BlockRegistry::selectionAABBs 画 sub-AABB 12 棱）：dynamic 几何，
    #   blockId/state Q_PROPERTY 改变即 rebuild。Main.qml selectionBoxPartial Model 绑此几何。
    src/Renderer/selectionwireboxes.cpp
    src/Renderer/selectionwireboxes.h
    src/Renderer/unitcube.cpp
    src/Renderer/unitcube.h
    src/Renderer/crackbox.cpp
    src/Renderer/crackbox.h
    src/Renderer/blockcube.cpp
    src/Renderer/blockcube.h
    src/Renderer/pickaxe.cpp
    src/Renderer/pickaxe.h
    # t233 锄 3D 几何（与 pickaxe.h 同层同风格，复制 addBox 模式仅改头部形状：锄刃 = 宽扁向前伸的横刃，
    #   区别于镐的横梁 + 两端下勾）。手持（第一/第三人称）+ 掉落实体三处统一消费，按 toolType 选镐 vs 锄。
    src/Renderer/hoe.cpp
    src/Renderer/hoe.h
    # t264 完整工具集：斧 / 铲 / 剑 3D 几何（与 pickaxe/hoe 同层同风格，复制 addBox 模式仅改形状）。
    #   斧 = 单边厚刃、铲 = 方形铲斗、剑 = 纵向长刃 + 护手 + 柄。手持（第一/第三人称）+ 掉落实体三处统一消费，
    #   按 toolType 选 5 类工具几何（Pickaxe/Hoe/Axe/Shovel/Sword）。纯实色体素几何 + NoLighting → 永不黑。
    src/Renderer/axe.cpp
    src/Renderer/axe.h
    src/Renderer/shovel.cpp
    src/Renderer/shovel.h
    src/Renderer/sword.cpp
    src/Renderer/sword.h
    # t304 弓 3D 几何（与 sword / pickaxe 同层同风格，复制 addBox 模式仅改形状：弓臂弧 + 弓弦 + 握把）。
    #   手持（第一 / 第三人称）+ 掉落实体三处统一消费，按 toolType===Bow（BlockRegistry::Bow=7）选本几何。
    src/Renderer/bow.cpp
    src/Renderer/bow.h
    # t240 猪牛羊方块化原创 3D 模型几何（四肢+躯干+头，§9 区隔不照搬 MC）。mobType Q_PROPERTY 选猪/牛/羊
    #   各自比例；每面铺整张贴图（全脸 UV，非 MC UV 拆皮）。EntityManager 的 Mob 实体（mobTypeAt）经
    #   Main.qml mobHost Repeater delegate 据其 mobType 设本几何 + 选对应贴图。Renderer 层只产出几何，
    #   mobType / 血量 / 名称在 Entities 层，贴图在呈现层（Main.qml Texture）—— 依赖只向下。
    src/Renderer/mobmodel.cpp
    src/Renderer/mobmodel.h
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
    # t173/t179 箱子内容存储 VM（Game 层）：按方块世界坐标键控的 27 槽物品内容；ChestUI 读写 + 破箱清孤儿。
    #   纯存储类（无光 / 无 World 依赖）；物品栈语义同 Hotbar（id=0=空）。t173 落地时漏注册 → ChestStore
    #   运行期「is not a type」→ 箱子右键无 UI；t179 补注册。
    src/Game/cheststore.cpp
    src/Game/cheststore.h
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
        src/ui/TorchSmoke.qml
        src/ui/MainMenu.qml
        src/ui/WorldList.qml
        src/ui/Inventory.qml
        src/ui/InvSlot.qml
        src/ui/SurvivalInventory.qml
        src/ui/CraftingTableUI.qml
        src/ui/FurnaceUI.qml
        src/ui/ChestUI.qml
        src/ui/VitalIcon.qml
        src/ui/ToolIcon.qml
        src/ui/MaterialIcon.qml
        src/ui/ArmSlider.qml
        src/ui/InventoryOps.js
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
        textures/sun.png
        textures/default_cobble.png
        textures/default_coal_ore.png
        textures/default_diamond_ore.png          # t279 钻矿石贴图（build_ore.py 程序生成原创像素图；散布于 stone 深层 y∈[5,16]）
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
        textures/default_water.png
        textures/default_water_2.png             # t223 静水动画第二帧（build_water.py 程序生成原创像素图）
        textures/default_water_flow.png          # t197 流水贴图（build_water_flow.py；曾漏注 qrc 清单，补登）
        textures/default_water_flow_2.png        # t223 流水动画第二帧（build_water_flow.py 程序生成原创像素图）
        textures/default_chest_top.png         # t173/t179 箱子各面贴图（build_chest.py 程序生成原创像素图）
        textures/default_chest_side.png
        textures/default_chest_front.png
        textures/default_farmland_dry.png      # t234 耕地顶面干态贴图（build_farmland.py 程序生成原创像素图）
        textures/default_farmland_wet.png      # t234 耕地顶面湿态贴图（mesher 据 Farmland state bit0 选 dry/wet）
        textures/default_tall_grass.png        # t235 草丛 cross 贴图（build_tall_grass.py 程序生成原创像素图；alpha 透明底 cutout）
        textures/default_wheat_stage_0.png     # t236 小麦作物 8 阶段贴图（build_wheat.py 程序生成原创像素图；cross 几何段 + alphaCutoff cutout；mesher 据 WheatCrop state 选 0..7）
        textures/default_wheat_stage_1.png
        textures/default_wheat_stage_2.png
        textures/default_wheat_stage_3.png
        textures/default_wheat_stage_4.png
        textures/default_wheat_stage_5.png
        textures/default_wheat_stage_6.png
        textures/default_wheat_stage_7.png
        textures/mob_pig.png                # t240 猪/牛/羊生物贴图（build_mob.py 程序生成原创像素图，§9a）
        textures/mob_cow.png                # 三种 passive mob 各一张「全脸」贴图，MobModel 几何每面铺同图（非 MC UV 拆皮）
        textures/mob_sheep.png
        textures/mob_shambler.png           # t282 蹒跚者（Shambler；机制等价 MC 僵尸，§9 改名）人形不死亡灵贴图（build_mob.py）
        textures/icon_cobble.png
        textures/icon_coal_ore.png
        textures/icon_diamond_ore.png            # t279 钻矿石立方体图标（build_cube_icons.py；石头底+青白菱斑晶体）
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
        textures/icon_chest.png                 # t173/t179 箱子立方体图标（顶盖缝+侧铁箍；曾漏注册→运行时 WRN 无图标）
        textures/icon_farmland.png              # t234 耕地立方体图标（顶=干态翻耕土+侧泥土）
        textures/icon_wood_slab.png           # t145 不完整方块 flat 2D 区分图标
        textures/icon_wood_stairs.png
        textures/icon_wood_fence.png
        textures/icon_wood_door.png
        textures/icon_wood_trapdoor.png
        textures/icon_wood_pressure_plate.png
        textures/icon_tall_grass.png               # t244 草丛 cross 方块 flat 2D 图标（透明底 + 绿草叶）
        textures/icon_wheat_crop.png               # t244 小麦作物 cross 方块 flat 2D 图标（取成熟阶段 7 麦穗）
)

# t89 / t118 音效资源（tools/build_sounds.py 程序合成，§4 原创 SFX；零 MC 资产）。
# 经 qrc 打包（:/sounds/*.wav）；AudioManager 从 qrc 读 WAV → miniaudio 解码播放。
# 注：PLAN §2-L 终态是从磁盘加载（便于热补丁），本任务先 qrc 跑通 + 留接口（见 audiomanager.cpp）。
# t118：按方块材质分组（石/木/草/沙/叶）clip 池 —— break/mining/step 各 5 个变体（按 BlockRegistry::
# materialGroup 选播，spec「按方块材质分组」）；place / pickup 单件（放置 / 拾取不分材质）。
# t152：door_open / door_close 单件（门/活板门右键开合声；doorToggled 信号 → AudioManager.playDoor*）。
# t177：hurt 单件（玩家受伤声；PlayerState.damaged → playHurt）+ ambient_wind 长循环风声床
#   （playing 态 startAmbient / 退菜单 stopAmbient；setAmbientLevel 据昼夜 skyLight 调强度）。
# t250：mob_idle（通用 / 猪 / 牛 / 羊）idle 叫声单件 —— EntityManager 周期 emit mobAmbient(mobType) →
#   AudioManager.playMobAmbient 据 mobType 选播（机制等价 MC 1.0 被动生物偶发 idle call；§9 原创）。
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
        sounds/door_open.wav
        sounds/door_close.wav
        sounds/hurt.wav
        sounds/mob_hurt.wav
        sounds/explosion.wav
        sounds/ambient_wind.wav
        sounds/water_flow.wav
        sounds/water_step.wav
        sounds/mob_idle.wav
        sounds/mob_idle_pig.wav
        sounds/mob_idle_cow.wav
        sounds/mob_idle_sheep.wav
        sounds/mob_idle_shambler.wav
        sounds/mob_idle_bones.wav
        sounds/mob_idle_stalker.wav
        sounds/mob_idle_spider.wav
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
    Qt6::Sql
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
