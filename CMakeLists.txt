cmake_minimum_required(VERSION 3.16)
# Phase 0+：Perlin 噪声区块 + 飞行相机（QtQuick3D 实例化）。代号 voxelsandbox（最终名待定，见 docs/PLAN.md §9）。
project(voxelsandbox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Gui Quick Qml Quick3D Sql)

qt_standard_project_setup()

# t813 构建版本戳（R19.13 🅱 组）：每次 build 生成 build/generated/build_stamps.h
#   （VOXEL_BUILD_STAMP 构建时间 YYYY-MM-DD HH:MM + VOXEL_BUILD_GIT 短哈希），经 src/Core/buildinfo.cpp
#   单 TU 消化为 QString API（BuildInfo QML 单例）。三处机制（缺一不可）：
#   1) configure 期 bootstrap（execute_process）：保证首个增量编译前头文件就存在；
#   2) add_custom_target 每次构建恒跑（custom target 恒 out-of-date）→ 跨过分钟边界 / 换 commit
#      即刷新 stamp；
#   3) 脚本「内容变才覆写」（保 mtime）→ 未变不重编；变了也只重编唯一 include 该头的 buildinfo.cpp
#      （其余 TU 只见 buildinfo.h 的 QString API，宏不外泄 → 不触发全量重编）。
#   目的：主菜单角落 + F3 + 启动日志常驻「构建时间 @ 哈希」，用户复测前先核版本，
#   终结「陈旧 exe 报已修 bug」争议（t740/t744/t792/t772/t805 六连疑）。
#   生成头在 build/generated/（build/ 已 gitignore）→ 生成文件不入库，无需额外 ignore 条目。
set(VOXEL_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${VOXEL_GENERATED_DIR}")
set(VOXEL_STAMP_HEADER "${VOXEL_GENERATED_DIR}/build_stamps.h")
execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DSTAMP_FILE=${VOXEL_STAMP_HEADER}"
        "-DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildStamp.cmake")
add_custom_target(build_stamps
    BYPRODUCTS "${VOXEL_STAMP_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DSTAMP_FILE=${VOXEL_STAMP_HEADER}"
            "-DWORKDIR=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildStamp.cmake"
    COMMENT "t813 refreshing build_stamps.h (build time + git short hash)")

qt_add_executable(voxelsandbox
    main.cpp
    src/Core/blockregistry.cpp
    src/Core/blockregistry.h
    # t232 世界列表封面黑屏修复：QQuickWindow::grabWindow 封装（grabToImage 拍不到 View3D 3D 场景 → 全黑）。
    #   仅依赖 Qt（Quick/Gui/Qml），无自有层依赖 → Core 叶子工具，依赖只向下铁律成立。
    src/Core/screengrab.cpp
    src/Core/screengrab.h
    # t414 资源包加载器（Core 叶子）：启动期解析 pack 路径（settings.json / 环境变量 / 默认探查），
    #   以 qrc 程序生成图集为底、把包内贴图缩放到 TILE=16 覆盖对应瓦片，合成运行期图集。
    #   只读本地 gitignored 路径的 PNG，绝不 bake MC 资产进 qrc（PLAN §9 红线）。合成图集经
    #   main.cpp 注册的 QQuickImageProvider("rp") 以 image://rp/atlas 暴露给 QML；无包时回退 qrc 默认。
    #   Core 只依赖 Qt Core/Gui；QtQuick（image provider）留在 main.cpp 胶水层（依赖只向下）。
    src/Core/resourcepackmanager.cpp
    src/Core/resourcepackmanager.h
    # t1022 键位重映射（Core 叶子）：「动作 → Qt 键」映射表单一权威 + settings.json "keyBindings" 节
    #   持久化（读缺节/缺行/坏值落默认向后兼容；写保留其它字段）+ 冲突检测拒收。QML 设置页经
    #   Q_INVOKABLE 编辑；PlayerController::setKey 经 canonicalKey 在单一入口规范化（引擎内部零改动）。
    src/Core/keybindmanager.cpp
    src/Core/keybindmanager.h
    # perf：帧时间分解探针（PLAN §2-F / §4 帧时间切分）。进程全局单例：C++ 各热路径
    #   （PlayerController tickImpl / ChunkGeometry buildMesh / World tick 函数）经 RAII Scope 把各阶段
    #   CPU 耗时累加进具名桶，每 ~1s flush → QML F3 叠层只读 + qInfo 落日志。诊断「每帧固定开销」
    #   （实体 tick / mesh 重建 / 物理 / ...）瓶颈，不再猜。Core 叶子（只依赖 Qt Core/Qml）。
    src/Core/frameprofiler.cpp
    src/Core/frameprofiler.h
    # t813 构建版本戳（Core 叶子，QML 单例）：stamp/gitHash/full 三只读属性读 CMake 每次
    #   build 生成的 build_stamps.h（唯一 include 点在本 .cpp → stamp 刷新只重编此 TU）。
    #   主菜单角落 / F3 / 启动日志三处消费；redstone_matrix_test 直编本源做格式探针。
    src/Core/buildinfo.cpp
    src/Core/buildinfo.h
    # R20.05 基础类型（Core 叶子，header-only）：floorDiv/floorMod（负坐标语义单一权威）/
    #   ChunkKey（chunk 坐标→可哈希键）/ BlockPos（方块坐标+邻接）/ Tick（固定 tick 基准）。
    #   只立类型 + 最小示范采用（chunkmanager 路由 / worldclock 声明点），调用点迁移留后续单。
    src/Core/mathtypes.h
    # R20.05 基础错误类型（Core 叶子，header-only）：Error / Result<T> 最小雏形（§5.1
    #   voxel_base 方向；只立类型不迁移调用点，R20.06 Command/Event 骨架时接线）。
    src/Core/result.h
    src/World/chunk.cpp
    src/World/chunk.h
    src/World/chunkmanager.cpp
    src/World/chunkmanager.h
    src/World/chunkgeometry.cpp
    src/World/chunkgeometry.h
    # R20.13 MeshBuilder（网格算法单一权威，自 chunkgeometry.cpp 整体迁出）：CPU mesher 纯函数
    #   （输入稠密快照 / 输出 owning ChunkMeshData，QObjectFree 值纪律；零 QQuick3D——QQuick3D 灌注
    #   留在 chunkgeometry 适配面）。World 层同 mesher 子系统（partialblockgeometry/voxellight）同层。
    src/World/meshbuilder.cpp
    src/World/meshbuilder.h
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
    # §29.5-W5b 流式 UI（r2028，§29.5 登记的 QML 例外单）：Game 层 StreamingBridge——W5 后端会话
    #   （gamesession.h，header-only 编排壳自本单起生产实例化）在 QML 面的最小消费投影（QML 单例，
    #   BuildInfo 同款模式；五个 Q_INVOKABLE = D2 创建链标志 / 标志读面 / D3 opt-in 转换 / 进入分流 /
    #   保存链前置冲洗——变更面集中守护见 tools/matrix/section31_streaming_ui.cpp r2028c）。
    #   分层：Game → World/WorldStore 向下依赖（gamesession.h 同门先例）。随会话首度生产实例化，
    #   chunkstore.cpp（D5 附加表 + stream_worlds 元数据域）自本单入 app 目标（W3-W5 期间仅矩阵
    #   目标直编——生产零激活面随本单闭合）。
    src/World/chunkstore.cpp
    src/World/chunkstore.h
    src/Game/gamesession.h
    src/Game/streamingbridge.cpp
    src/Game/streamingbridge.h
    # t1057 SaveCoordinator 生产接线（refactor-plan §29.6，§29.6 登记的 QML 例外单）：Game 层
    #   SaveBridge——r2015 存档协调层（marker-first/代次/恢复状态机）在 QML 面的最小消费投影
    #   （QML 单例，StreamingBridge 同款模式；两个 Q_INVOKABLE = saveViaCoordinator 一口进
    #   [runExitSave 三写段置换] / recoveryState 恢复态读面[enterWorld Interrupted toast]）。
    #   分层：Game → World（savecoordinator/worldstore）向下依赖（streamingbridge 同门先例）。
    #   随桥首度生产实例化，savecoordinator.cpp（r2015 协调层本体 + t1057 #5② OpenError 修复）
    #   自本单入 app 目标（r2015-t1056 期间仅矩阵目标直编——生产零接线面随本单闭合；
    #   review0916 #5 指认的「生产目标根本不编译它」随本单清偿）。
    src/World/savecoordinator.cpp
    src/World/savecoordinator.h
    src/Game/savebridge.cpp
    src/Game/savebridge.h
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
    # t697 符文字迹几何（附魔台悬浮书页面细节）：8 条错落扁盒条组成的「字行」，压在书页纸面上读作
    # 手写符文（程序生成，§9 原创）。t732 悬浮书重贴图后页面符文由贴图自带（叠层与贴图字迹会错位
    # 重影）→ QML 撤用；类保留注册（几何库一员，后续 UI 可复用）。
    src/Renderer/glyphlines.cpp
    src/Renderer/glyphlines.h
    src/Renderer/crackbox.cpp
    src/Renderer/crackbox.h
    src/Renderer/blockcube.cpp
    src/Renderer/itemshapegeometry.cpp   # t880 方块异形物品 3D 模型几何（掉落物 + 资源查看器共用；活板门/火把/台阶/楼梯/雪层/cross/附魔台）
    src/Renderer/blockcube.h
    # t784 床完整低 3D 模型几何（资源浏览器床条目预览）：盒布局复用 World 层 bedHalfBoxes 单一权威
    #   （游戏内 chunk mesh 床 case 同源，双半拼双格床）→ 浏览器 / 游戏内床模型不各自漂移。
    src/Renderer/bedmodelgeometry.cpp
    src/Renderer/bedmodelgeometry.h
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
    # t718 盔甲 layer 薄壳盒几何（Renderer 层）：玩家 + 人形 mob 护甲叠加壳的共享「通用 layer 渲染器」。
    #   ±0.5 单位盒 + MC armor layer box-UV 六面子区（同 MobModel R19 C3 公式）→ 从 layer_1/2 贴图
    #   （程序层 / pack 运行期映射）按部位采样。贴图选择在呈现层（Main.qml），本类只产出几何 + UV。
    src/Renderer/armorlayerbox.cpp
    src/Renderer/armorlayerbox.h
    # t731 玩家皮肤盒几何（Renderer 层）：playerModel 第三人称 + CharacterPreview3D 预览共用。±0.5
    #   单位盒 + MC 皮肤 64×32 box-UV 六面子区（同 ArmorLayerBox 公式）+ subV0/1 腿分段行区间裁切。
    #   贴图选择在呈现层（Main.qml skinTexture()），本类只产出几何 + UV。
    src/Renderer/playerskinbox.cpp
    src/Renderer/playerskinbox.h
    # t732 矿车斗贴图盒几何（Renderer 层）：cartHost delegate（t565）的底板 / 纵帮 / 端帮从 UnitCube
    #   纯色换贴图采样。±0.5 单位盒 + 「部件 × 六面」像素矩形表（qrc 程序 / demo 包双布局实测分区）。
    #   贴图选择在呈现层（Main.qml），pack 探测在 Core entitySource，本类只产出几何 + UV。
    src/Renderer/minecartbox.cpp
    src/Renderer/minecartbox.h
    # t732 附魔台悬浮书贴图盒几何（Renderer 层）：bookDelegate（t679）的封面页 / 纸页 / 书脊 / 翻页片
    #   从 UnitCube 纯色换贴图采样（解决 t679「整书 UV 与两页盒不符」遗留）。同双布局像素矩形表。
    #   贴图选择在呈现层（Main.qml），pack 探测在 Core entitySource，本类只产出几何 + UV。
    src/Renderer/enchantbookbox.cpp
    src/Renderer/enchantbookbox.h
    # t331 骸骨持用弓几何（独立于 MobModel —— 弓需木褐色材质 + 拉弓动画；QML 独立 Model 消费）。
    #   drawAmount Q_PROPERTY 驱动弦后拉 + 肢增弯；EntityManager::drawAmountAt 设值（aimTimer 驱动）。
    src/Renderer/mobbow.cpp
    src/Renderer/mobbow.h
    # t112 广告牌四边形（单面 +Z 法线）：煤/木炭/铁原矿/木棒/铁锭/玻璃 掉落实体材料段
    #   从 CrackBox 6 面立方改为单面平图标 + 朝相机（billboard），消除「6 面未封闭超立方体」观感。
    src/Renderer/billboardquad.cpp
    src/Renderer/billboardquad.h
    # t389 天穹球（UV 球，内表面铺星空贴图）：绕相机眼位的星点球壳。夜间抬头见散布星点；
    #   居中于相机 → 每条视线交球一次 → 恒作最远天幕（地形 / 日月皆在其内）。Renderer 层只产出几何。
    src/Renderer/skydome.cpp
    src/Renderer/skydome.h
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
    # t858（R19.14）经验球 instancing 试点：QQuick3DInstancing C++ feeder（Game 层呈现适配器，
    #   向下只读 Entities 层 XpOrbManager；Quick3D 公开 API，无自定义 shader——RHI 囚笼合规）。
    src/Game/xporbinstancing.cpp
    src/Game/xporbinstancing.h
    # t1027（R19.21）掉落物 instancing 治理首批：方块整立方掉落族 per-id 桶 feeder（Game 层呈现适配器，
    #   向下只读 Game 层 ItemEntityManager + 家族谓词静态权威；t858 同款 Quick3D 公开 API 模式）。
    src/Game/blockdropinstancing.cpp
    src/Game/blockdropinstancing.h
    # t1039（R19.23）掉落物 instancing 批 3：光晕壳族全族 feeder（UnitCube 半透壳 1 draw/全族；
    #   per-instance color 承载附魔紫/灰×天光 + 呼吸 opacity 进实例表；槽序前 kShellCap 活体收纳，
    #   QML delegate 排除侧薄委托同源谓词）。向下只读 ItemEntityManager，同批 1/2 呈现适配器分层。
    src/Game/glowshellinstancing.cpp
    src/Game/glowshellinstancing.h
    # t1041（R19.23）掉落物 instancing 批 4 收官：工具 3D 族 per-id 桶 feeder（tier 色×天光 per-instance
    #   color，t1039 color 实例表先例；stringPass 弓弦双表——instancing 不承载 per-instance 子树，t1038 先例）
    #   + billboard 图标族双池单 feeder（itemIconFamily 切换异形方块图标族/工具材料图标族；朝相机旋转
    #   camPitch/camYaw 进实例表；per-id 贴图 = per-id 桶池选型，无自定义 shader——PLAN §2-A 合规）。
    src/Game/tooldropinstancing.cpp
    src/Game/tooldropinstancing.h
    src/Game/billboarddropinstancing.cpp
    src/Game/billboarddropinstancing.h
    # t95 统一实体管理器（Entities 层）：测试生物 / 玩家推动 / 重力。为后续 Mob/AI 系统铺垫统一基类。
    src/Entities/entitymanager.cpp
    src/Entities/entitymanager.h
    # t402 经验球管理器（Entities 层）：磁吸 + 拾取；杀怪 / 冶炼产出经验球，玩家吸收累积 XP。
    src/Entities/xporbmanager.cpp
    src/Entities/xporbmanager.h
    # t469 船实体管理器（Entities 层）：浮水 + 骑乘 + WASD 操控 + 冰上加速 + 撞坏掉落（机制等价 MC 1.0 boat）。
    #   新实体类型（Boat），slot-reuse 模型（同 EntityManager / ItemEntityManager）。
    src/Entities/boatmanager.cpp
    src/Entities/boatmanager.h
    # t565 矿车实体管理器（Entities 层）：轨上行驶 + 骑乘 + WASD 前后推 + 拐角自动转弯 + 挖掉矿车物品
    #   （机制等价 MC 1.0 minecart）。新实体类型（Cart），slot-reuse 模型（同 BoatManager）。
    src/Entities/minecartmanager.cpp
    src/Entities/minecartmanager.h
    # R20.14 EntityStore（Entities 层）：掉落物族模拟单一权威（spawn 物理/合并/拾取/despawn/焚毁/
    #   批量收口/槽位复用/EntityId 稳定 id + 可见实体快照）——非 QObject 值语义组件（plan §29.3
    #   R20.14「模拟实体不依赖 QML」）。ItemEntityManager（src/Game）收敛为过渡 Adapter 委托本类。
    src/Entities/entitystore.cpp
    src/Entities/entitystore.h
    src/Game/raycast.cpp
    src/Game/raycast.h
    src/Game/recipe.cpp
    src/Game/recipe.h
    # t345 护甲注册表（Game 层）：5 套材质 × 4 部位 = 20 件护甲的护甲值 / 耐久 / 名（机制等价 MC 1.0 护甲）。
    #   纯静态数据表（同 ToolRegistry 模式）；id 段定义在 recipe.h（ArmorIdBase=0x300）。减伤 / 装备槽 / 图标走它。
    src/Game/armor.cpp
    src/Game/armor.h
    src/Game/smelting.cpp
    src/Game/smelting.h
    # t173/t179 箱子内容存储 VM（Game 层）：按方块世界坐标键控的 27 槽物品内容；ChestUI 读写 + 破箱清孤儿。
    #   纯存储类（无光 / 无 World 依赖）；物品栈语义同 Hotbar（id=0=空）。t173 落地时漏注册 → ChestStore
    #   运行期「is not a type」→ 箱子右键无 UI；t179 补注册。
    src/Game/cheststore.cpp
    src/Game/cheststore.h
    # t177 二轮复盘 熔炉内容存储 VM（Game 层）：按方块世界坐标键控的 in/fuel/out 3 槽 + 冶炼 tick 进度
    #   （burnRemain/smeltProgress）；FurnaceUI 读写 + 破熔炉清孤儿 + 掉内容。修旧 bug：FurnaceUI 旧把
    #   in/fuel/out 存 QML 本地属性 → 全世界熔炉共享一个物品栏（打开任何熔炉都是同一内容）。本类把内容
    #   下沉到 C++ VM 层、按坐标寻址，结构对齐已 per-block 的 ChestStore。纯存储类（无光 / 无 World 依赖）。
    src/Game/furnacestore.cpp
    src/Game/furnacestore.h
    # t542 发射器内容存储 VM（Game 层）：按方块世界坐标键控的 9 槽（3×3）容器；DispenserUI 读写 +
    #   破发射器清孤儿 + 掉内容。修旧 bug（t517 遗留）：DispenserUI 旧把 9 槽存 QML 本地数组 → 全世界发射器
    #   共享一个物品栏（打开任何发射器都是同一内容）、打掉不掉。本类把内容下沉到 C++ VM 层、按坐标寻址，
    #   结构对齐已 per-block 的 ChestStore（箱子 27 槽）/ FurnaceStore（熔炉 3 槽）。纯存储类（无光 / 无 World 依赖）。
    src/Game/dispenserstore.cpp
    src/Game/dispenserstore.h
    # progress 玩家进度系统（Game 层 ViewModel）：统计（时间/挖放/路程/击杀/死亡/合成/拾取）+ 成就解锁。
    #   跨世界持久化（同 ChestStore/FurnaceStore，存 worldstore progress 表）。各事件源经 QML 桥接调埋点。
    src/Game/playerprogress.cpp
    src/Game/playerprogress.h
    # t393 战利品表（Game 层）：纯静态数据 + 纯函数（条目池 + 加权抽取）；地牢箱 + 渔获共用。
    #   无 Q_OBJECT / 无实例；item id 引用 recipe.h 材料段常量。ChestStore::populateDungeonLoot 调它填箱。
    src/Game/loottable.cpp
    src/Game/loottable.h
    # t475 附魔表（Game 层）：附魔属性集 + 据等级/种子选附魔的纯静态注册表；hotbar(ItemStack 附魔字段) 调它。
    src/Game/enchantregistry.cpp
    src/Game/enchantregistry.h
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
    # t813 生成的 build_stamps.h 搜索路径（buildinfo.cpp 唯一消费）。
    ${VOXEL_GENERATED_DIR}
)
# t813 头文件先于编译就绪（target 级顺序：build_stamps 全部完成后才编 voxelsandbox 对象）。
add_dependencies(voxelsandbox build_stamps)

qt_add_qml_module(voxelsandbox
    URI VoxelSandbox
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/BlockParticles.qml
        src/ui/EnchantRunes.qml # t649 附魔台符文粒子（书架→台 漂浮字符；Model+Timer 池，无 Particles3D 依赖）
        src/ui/EnchantGlyphFlow.qml # t765/t797 书架→附魔台白色小字形粒子流（常驻：台×书架对低频发射向书心渐隐；Model+Timer 池）
        src/ui/TorchSmoke.qml
        src/ui/WeatherParticles.qml
        src/ui/AmbientParticles.qml
        src/ui/MainMenu.qml
        src/ui/WorldList.qml
        src/ui/Inventory.qml
        src/ui/InvSlot.qml
        src/ui/DurabilityBar.qml # t640② 背包 main/hotbar/craft 槽工具·护甲耐久条（HUD hotbar t315 模式推广）
        src/ui/HeldItemNameFlash.qml # t977 切槽物品名浮显（血量/饱食度上方中央白字，hold 2s + fade 0.3s；文本组装走 Hotbar::slotDetailText 单一权威）
        src/ui/SurvivalInventory.qml
        src/ui/CharacterPreview3D.qml
        src/ui/CraftingTableUI.qml
        src/ui/FurnaceUI.qml
        src/ui/ChestUI.qml
        src/ui/EnchantingTableUI.qml
        src/ui/AnvilUI.qml
        src/ui/DispenserUI.qml
        src/ui/VitalIcon.qml
        src/ui/ToolIcon.qml
        src/ui/MaterialIcon.qml
        src/ui/ArmSlider.qml
        src/ui/ResourceBrowser.qml
        src/ui/DarkScrollBar.qml # t591 项目统一暗色细条滚动条（背包/世界列表/资源查看器共用）
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
        textures/cloud.png                # t384 云层贴图（build_cloud.py 程序生成可无缝平铺的云密度图；昼白夜灰由 baseColor 乘灰阶实现）
        textures/stars.png                # t389 星空贴图（build_stars.py 程序生成透明底+白/蓝星点；铺到 SkyDome 内表面，夜间淡入）
        textures/glyphs.png               # t765 符文字形图集（build_glyph_sprites.py 程序生成 4×4 原创字形；附魔台 UI 开时书架→悬浮书 glyph 粒子贴图）
        textures/moon_0.png               # t389 月相 8 帧贴图（build_moon.py 程序生成；满→盈凸→上弦→蛾眉→新月→残月→下弦→亏凸，WorldClock.moonPhase 选其一）
        textures/moon_1.png
        textures/moon_2.png
        textures/moon_3.png
        textures/moon_4.png
        textures/moon_5.png
        textures/moon_6.png
        textures/moon_7.png
        textures/default_cobble.png
        textures/default_coal_ore.png
        textures/default_copper_ore.png          # t308 铜矿石贴图（build_ore.py 程序生成原创像素图；散布于 stone 浅中层 y∈[5,45]）
        textures/default_diamond_ore.png          # t279 钻矿石贴图（build_ore.py 程序生成原创像素图；散布于 stone 深层 y∈[5,40]）
        textures/default_gold_ore.png            # t308 金矿石贴图（build_ore.py 程序生成原创像素图；散布于 stone 深层 y∈[5,25]）
        textures/default_lapis_ore.png           # t471 青金矿石贴图（build_ore.py 程序生成原创像素图；石头底+群青深蓝斑簇+黄铁矿金点；散布于 stone 深层 y∈[5,31]）
        textures/default_redstone_ore.png        # t569 红石矿石贴图（build_ore.py 程序生成原创像素图；石头底+鲜红菱斑矿粒=复制钻石矿布局改红；散布于 stone 最深层 y∈[5,16]）
        textures/default_enchanting_table_top.png # t474 附魔台顶面贴图（build_enchanting_table.py；黑曜石深紫黑底+钻石青白四角+中央立书轮廓）
        textures/default_enchanting_table_side.png # t474 附魔台侧面/底面/前面贴图（build_enchanting_table.py；黑曜石深紫黑底+钻石四角+边缘暗化）
        textures/default_bookshelf.png           # t474 书架各面贴图（build_bookshelf.py；橡木木板边框+彩色书脊书列）
        textures/default_iron_block.png          # t477 铁块各面贴图（build_anvil.py；金属灰底+铆钉网格+高光；9 铁锭存储方块）
        textures/default_coal_block.png          # t620 煤炭块各面贴图（build_mineral_blocks.py；近黑煤层压缩块+高光棱线；9 煤↔1 块；燃料 800s）
        textures/default_lapis_block.png         # t620 青金石块各面贴图（build_mineral_blocks.py；深群青底+黄铁矿金点；9 青金石↔1 块）
        textures/default_diamond_block.png       # t620 钻石块各面贴图（build_mineral_blocks.py；浅青底+青白菱面镶格；9 钻石↔1 块）
        textures/default_gold_block.png          # t620 金块各面贴图（build_mineral_blocks.py；金黄底+近白金高光；9 金锭↔1 块）
        textures/default_redstone_block.png      # t620 红石块各面贴图（build_mineral_blocks.py；鲜红底+亮红矿粒；9 红石粉↔1 块）
        textures/default_redstone_lamp_off.png   # t620 红石灯 off 态贴图（build_mineral_blocks.py；暗黄褐哑壳+中央暗红红石芯纹；mesher 据 state bit0 选 off/on）
        textures/default_redstone_lamp_on.png    # t620 红石灯 on 态贴图（build_mineral_blocks.py；暖琥珀亮壳+中心白热核；on 态光 15）
        textures/default_anvil_top.png           # t477 铁砧完好顶面贴图（build_anvil.py；深铁砧台+宽砧面+尖角+边缘暗化）
        textures/default_anvil_base.png          # t477 铁砧侧/底/前面贴图（build_anvil.py；深铁砧身+横向分层暗带；三损坏阶段共享）
        textures/default_anvil_damaged_1_top.png # t477 微损铁砧顶面贴图（build_anvil.py；砧台+一条细裂纹）
        textures/default_anvil_damaged_2_top.png # t477 重损铁砧顶面贴图（build_anvil.py；砧台+粗裂纹网+尖角缺角）
        textures/default_pumpkin_side.png        # t482 南瓜侧面贴图（build_pumpkin.py；深橙底+纵向瓜棱深纹；Pumpkin sideTile）
        textures/default_pumpkin_face.png        # t482 南瓜刻面前面贴图（build_pumpkin.py；橙底+刻面双眼+锯齿嘴+短茎；Pumpkin frontTile=-Z）
        textures/default_pumpkin_top.png         # t482 南瓜顶/底面贴图（build_pumpkin.py；橙色瓜顶+中央短茎+高光；Pumpkin top/bottomTile）
        textures/default_cobweb.png              # t484 蜘蛛网 cross 贴图（build_cobweb.py；透明底+灰白蛛丝放射网纹；alphaCutoff cutout；Cobweb 各面）
        textures/default_rail.png                # t484 铁轨贴地薄板 flat 贴图（build_rail.py；透明底+棕色枕木+灰铁双轨；alphaCutoff cutout；Rail 各面）
        textures/default_rail_corner.png         # t565 铁轨 90° 拐角贴图（build_rail.py；轨自南进向左弯出；mesher 据轨 state 连接位选）
        textures/default_rail_cross.png          # t565 铁轨十字交叉贴图（build_rail.py；南北+东西双轨叠交+中央方枕木）
        textures/default_tnt.png                 # t485/t646 TNT 侧·前面贴图（build_tnt.py；深红药柱底+横向深棕捆带+中央亮黄标识；沙漠神殿陷阱方块；t646 起 side/front 专用）
        textures/default_tnt_top.png             # t646 TNT 顶面贴图（build_tnt.py；药柱截面+捆带俯视压痕+中央亮黄引线接口俯视；TntBlock topTile=164）
        textures/default_tnt_bottom.png          # t646 TNT 底面贴图（build_tnt.py；纯药柱底板+暗捆带无标记；TntBlock bottomTile=165）
        textures/default_cut_sandstone.png       # t485 切制砂岩各面贴图（build_cut_sandstone.py；暖沙色平滑底+内陷矩形装饰边框；金字塔外框装饰变体）
        textures/default_mossy_cobble.png        # t486 苔石各面贴图（build_mossy_cobble.py；圆石灰底+散布暗绿苔藓斑簇；丛林神殿主体）
        textures/default_dispenser_top.png       # t486 发射器顶/底面贴图（build_dispenser.py；石质灰底+中央圆形排出口俯视环纹）
        textures/default_dispenser_side.png      # t486 发射器侧面贴图（build_dispenser.py；石质灰底+边框暗带+四角铆钉）
        textures/default_dispenser_front.png     # t486 发射器前面（排出口所朝面）贴图（build_dispenser.py；石质灰底+中央暗腔排出口）
        textures/default_dropper_front.png       # t609 投掷器前面（排出口所朝面）贴图（build_dropper.py；石质灰底+中央小方形暗孔；顶/底/侧复用熔炉 12/13）
        textures/default_endframe_side.png       # t620 末影祭坛侧/底面贴图（build_endframe.py；灰白细孔框身石纹；EndPortal endframe 化侧/底）
        textures/default_endframe_top.png        # t620 末影祭坛顶面贴图（build_endframe.py；灰白框面+中央暗绿凹槽（未放末影之眼））
        textures/default_endframe_eye.png        # t620 末影祭坛顶面贴图（build_endframe.py；框面+中央之眼亮纹（已放末影之眼激活态））
        textures/default_door_wood_upper.png     # t620 橡木门上半贴图（build_door.py；门板+上部格栅窗；door case 据 state bit3 选）
        textures/default_door_wood_lower.png     # t620 橡木门下半贴图（build_door.py；门板+锁孔板+底部横带）
        textures/default_door_spruce_upper.png   # t620 云杉门上半贴图（build_door.py；深色门板+格栅窗）
        textures/default_door_spruce_lower.png   # t620 云杉门下半贴图（build_door.py；深色门板+锁孔板+底部横带）
        textures/default_stone_brick.png         # t487 石砖各面贴图（build_stone_brick.py；石质灰底+砖块缝纹网格；要塞墙体主体）
        textures/default_end_portal.png          # t487 末地传送门未激活态贴图（build_end_portal.py；深紫黑星空底+中心暗绿旋涡）
        textures/default_end_portal_active.png   # t487 末地传送门激活态贴图（build_end_portal.py；深紫黑星空底+中心亮绿旋涡+白绿高光）
        textures/default_lever.png               # t490 杠杆贴图（build_lever_button.py；木质底座+竖直扳柄+圆柄头；手动点火机关）
        textures/default_wood_button.png         # t490 木按钮贴图（build_lever_button.py；木质底座+凸起圆钮）
        textures/default_stone_button.png        # t490 石按钮贴图（build_lever_button.py；石质底座+凸起圆钮）
        textures/default_stone_pressure_plate.png # t627 石压力板贴图（build_pressure_plates.py；石质灰底+边框暗带+中央板面微亮内圈）
        textures/default_iron_pressure_plate.png  # t627 铁压力板贴图（build_pressure_plates.py；金属浅灰底+四角铆钉；重质板）
        textures/default_gold_pressure_plate.png  # t627 金压力板贴图（build_pressure_plates.py；金黄底+中央亮金板面；轻质板）
        textures/default_golden_rail.png          # t638 动力铁轨断常态贴图（build_rail_family.py；金轨双线+红石连接点暗红；矿车加速）
        textures/default_detector_rail.png        # t638 探测铁轨断常态贴图（build_rail_family.py；铁轨+石枕+暗红探测点）
        textures/default_rail_golden_on.png       # t638 动力轨通变态贴图（恒断电不消费，留图集备用）
        textures/default_rail_detector_on.png     # t638 探测轨通电视觉贴图（矿车驶过 state bit4 → mesher 换贴）
        textures/default_redstone_torch.png       # t638 红石火把 cross 贴图（build_rail_family.py；深棕柄+亮红焰头；常亮光 7）
        textures/default_dust_line_off.png        # t656 红石粉断电线向贴图（build_redstone_dust.py；暗红粉线；线/点×断/通四瓦片）
        textures/default_dust_dot_off.png         # t656 红石粉断电孤立点贴图（build_redstone_dust.py；暗红粉堆）
        textures/default_dust_line_on.png         # t656 红石粉通电线向贴图（build_redstone_dust.py；亮红粉线+白热高光粒）
        textures/default_dust_dot_on.png          # t656 红石粉通电点贴图（build_redstone_dust.py；亮红粉堆+白热心）
        textures/default_redstone_torch_off.png   # t657 红石火把熄灭态贴图（build_redstone_dust.py；暗红熄焰；反相 NOT 门视觉）
        textures/default_dust_line_lvl1.png      # t692 红石粉弱电线向贴图（build_redstone_dust.py；中暗红+稀疏微亮粒；电力 1-5 档）
        textures/default_dust_line_lvl2.png      # t692 红石粉半亮线向贴图（build_redstone_dust.py；中亮红+少量暖心粒；电力 6-10 档）
        textures/default_dust_dot_lvl1.png       # t692 红石粉弱电点贴图（build_redstone_dust.py；中暗红粉堆；电力 1-5 档）
        textures/default_dust_dot_lvl2.png       # t692 红石粉半亮点贴图（build_redstone_dust.py；中亮红粉堆+暖心粒；电力 6-10 档）
        textures/default_enchant_book.png         # t638 附魔台顶摊开书页贴图（build_book.py；白纸底+灰字线+中央书脊；无 pack 等价）
        textures/default_cactus_bottom.png        # t638 仙人掌底面贴图（build_cactus.py；更暗绿截面+外圈皮层暗环；观察者视角可见）
        textures/default_crafting_table_side.png
        textures/default_crafting_table_top.png
        textures/default_dirt.png
        textures/default_furnace_front.png
        textures/default_furnace_front_on.png     # t494 熔炉点燃态前面贴图（build_furnace.py；圆石底+拱框+亮黄橙火焰；mesher 据 Furnace state 燃烧 bit 选 front/front_on）
        textures/default_furnace_side.png
        textures/default_furnace_top.png
        textures/default_grass_side.png
        textures/default_grass_top.png
        textures/default_gravel.png             # t761 沙砾贴图（build_gravel.py；灰砾石+卵石碎砾斑；tile 179；重力方块，挖掉概率掉燧石）
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
        textures/default_lava.png                # t343 岩浆贴图（build_lava.py 程序生成原创像素图；深红橙底+亮黄橙鼓泡+白炽热点）
        textures/water_strip.png                 # t489 水条带 flipbook（build_fluid_strips.py；2 列×32 帧：静水|流水；材质级 positionV 动画，零 mesh 重建）
        textures/lava_strip.png                  # t489 岩浆条带 flipbook（build_fluid_strips.py；1 列×16 帧；材质级 positionV 动画）
        textures/fire_strip.png                  # t724 火焰条带 flipbook（build_fire.py；1 列×32 帧；fireHost delegate scaleV/positionV 动画）
        textures/portal_strip.png                # t725 余烬门条带 flipbook（build_portal.py；1 列×32 帧；portalHost delegate scaleV/positionV 动画）
        textures/default_obsidian.png            # t411/t472 黑曜石贴图（build_obsidian.py 程序生成原创像素图；深紫黑火山玻璃底+紫红纹理嵌点+品紫微反光；流体交互产物）
        textures/default_bed_red.png             # t387 床方块 8 色变体贴图（build_bed.py 程序生成原创像素图；彩色被面+枕垫亮带+绗缝针脚）
        textures/default_bed_orange.png
        textures/default_bed_yellow.png
        textures/default_bed_green.png
        textures/default_bed_cyan.png
        textures/default_bed_blue.png
        textures/default_bed_magenta.png
        textures/default_bed_black.png
        textures/default_bed_white.png             # t455 16 色床补齐 8 色新变体贴图（build_bed.py 程序生成；与同色羊毛同色板）
        textures/default_bed_light_blue.png
        textures/default_bed_lime.png
        textures/default_bed_pink.png
        textures/default_bed_gray.png
        textures/default_bed_light_gray.png
        textures/default_bed_purple.png
        textures/default_bed_brown.png
        textures/default_wool.png                  # t300/t455 羊毛白色贴图（build_wool.py；曾漏 qrc 致创造调色板羊毛图标空，t455 补登）
        textures/default_wool_orange.png           # t455 16 色 wool 其余 15 色变体贴图（build_wool.py 程序生成卷绒纹+标准 16 色着色）
        textures/default_wool_magenta.png
        textures/default_wool_light_blue.png
        textures/default_wool_yellow.png
        textures/default_wool_lime.png
        textures/default_wool_pink.png
        textures/default_wool_gray.png
        textures/default_wool_light_gray.png
        textures/default_wool_cyan.png
        textures/default_wool_purple.png
        textures/default_wool_blue.png
        textures/default_wool_brown.png
        textures/default_wool_green.png
        textures/default_wool_red.png
        textures/default_wool_black.png
        textures/default_spawner.png              # t392 刷怪笼贴图（build_spawner.py 程序生成原创像素图；暗蓝灰底+铁灰栅栏+中心青绿光斑）
        textures/default_sandstone_top.png        # t394 砂岩顶面贴图（build_sandstone.py 程序生成；压实沙面+细密噪点+暗框）
        textures/default_sandstone_side.png       # t394 砂岩侧面/底面贴图（build_sandstone.py；暖沙底+横向层理带）
        textures/default_cactus_top.png           # t394 仙人掌顶面/底面贴图（build_cactus.py；绿截面+同心方框环纹+中央凹陷）
        textures/default_cactus_side.png          # t394 仙人掌侧面贴图（build_cactus.py；深绿底+4 垂直棱脊+棱上刺点）
        textures/default_dead_bush.png            # t394 枯死的灌木 cross 贴图（build_dead_bush.py；透明底+棕褐放射干枝；alphaCutoff cutout）
        textures/default_snow.png                 # t395 积雪层贴图（build_snow.py；冷白底+细密冰晶噪点；SnowLayer 各面）
        textures/default_ice.png                  # t395 冰贴图（build_ice.py；浅蓝底+反光裂纹；Ice 各面）
        textures/default_pack_ice.png             # t468/t495 浮冰贴图（build_ice.py；淡蓝白压实冰+密实细裂纹+反光高光；PackIce 各面）
        textures/default_blue_ice.png             # t468 蓝冰贴图（build_ice.py；淡蓝底+纵向纹路；BlueIce 各面）
        textures/default_spruce_log_top.png       # t395 云杉原木顶/底面贴图（build_spruce.py；深棕同心年轮截面）
        textures/default_spruce_log_side.png      # t395 云杉原木侧面贴图（build_spruce.py；深棕垂直树皮条带）
        textures/default_spruce_planks.png        # t466 云杉木板贴图（build_spruce.py；深色木纹；SprucePlanks/Slab/Fence/Door 共享 tile 102）
        textures/default_spruce_leaves.png        # t714 云杉树叶贴图（build_spruce.py；深蓝绿针叶+透明孔；SpruceLeaves tile 175）
        textures/default_door_iron_upper.png      # t717 铁门上半贴图（build_doors_iron.py；门板+下部 2×2 格栅窗；tile 176；t722 IronDoor 接入）
        textures/default_door_iron_lower.png      # t717 铁门下半贴图（build_doors_iron.py；门板+锁孔板+底部横带；tile 177；t722 IronDoor 接入）
        textures/default_iron_trapdoor.png        # t717 铁活板门贴图（build_doors_iron.py；格子板+栅格孔；tile 178；t723 IronTrapdoor 接入）
        textures/default_note_block.png           # t1028 音符盒贴图（build_note_block.py；深木框+盆膜+原创音符标记；tile 184；NoteBlock 接入）
        textures/default_lily_pad.png             # t396 睡莲浮叶贴图（build_lily_pad.py；透明底+绿圆叶+V缺口；alphaCutoff cutout）
        textures/default_mushroom.png             # t396 蘑菇 cross 贴图（build_mushroom.py；透明底+米色菌柄+红底白斑菌盖；alphaCutoff cutout）
        textures/default_brown_mushroom.png       # t507 白蘑菇 / 棕蘑菇 cross 贴图（build_brown_mushroom.py；透明底+米色菌柄+棕色菌盖浅黄褐斑；alphaCutoff cutout；蘑菇汤原料）
        textures/default_flower_red.png           # t397 花 4 色变体 cross 贴图（build_flower.py；透明底+绿茎+彩色花头；alphaCutoff cutout）
        textures/default_flower_yellow.png
        textures/default_flower_blue.png
        textures/default_flower_white.png
        textures/default_sugarcane.png            # t397 甘蔗 cross 贴图（build_sugarcane.py；透明底+绿色节段细茎+顶部尖叶；alphaCutoff cutout）
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
        textures/default_carrot_crop_0.png      # t407 胡萝卜作物 4 阶段贴图（build_carrot_potato.py 程序生成原创像素图；cross 几何段 + alphaCutoff cutout；mesher 据 CarrotCrop state 选 69 + state/2）
        textures/default_carrot_crop_1.png
        textures/default_carrot_crop_2.png
        textures/default_carrot_crop_3.png
        textures/default_potato_crop_0.png      # t407 马铃薯作物 4 阶段贴图（build_carrot_potato.py；mesher 据 PotatoCrop state 选 73 + state/2）
        textures/default_potato_crop_1.png
        textures/default_potato_crop_2.png
        textures/default_potato_crop_3.png
        textures/default_ladder.png              # t413/t519 木梯贴墙贴图（build_ladder.py；透明底 + 棕色两纵轨贴瓦片两侧 + 4 道横梯级满铺轨间；alphaCutoff cutout；t519 满格版修「放下形状上下宽粗糙」）
        textures/mob_pig.png                # t240 猪/牛/羊生物贴图（build_mob.py 程序生成原创像素图，§9a）
        textures/mob_cow.png                # 三种 passive mob 各一张「全脸」贴图，MobModel 几何每面铺同图（非 MC UV 拆皮）
        textures/mob_sheep.png
        textures/mob_sheep_sheared.png     # t749 剪毛羊贴图（裸肤 #d6b890 底 + 头顶/背部残羊毛块；build_mob.py；图鉴剪毛变体 + 游戏内 shearedAt 羊共用）
        textures/mob_sheep_head.png        # t876 羊头贴图（自然羊头色：裸肤脸 + 头顶羊毛帽 + 吻部暗带；build_mob.py；sheepSkinHead 头 subset 的 pack 关态纹理源，不吃毛色 tint）
        textures/mob_shambler.png           # t282 蹒跚者（Shambler；机制等价 MC 僵尸，§9 改名）人形不死亡灵贴图（build_mob.py）
        textures/mob_baby_shambler.png      # t952 小蹒跚者（BabyShambler；机制等价 MC 幼体僵尸，§9 区隔命名）亮黄绿幼体人形贴图（build_mob.py）
        textures/mob_chicken.png            # t398 鸡（Chicken；机制等价 MC 鸡，§9 原创）小型鸟白羽 + 棕褐斑贴图（build_mob.py）
        textures/mob_squid.png              # t399 鱿鱼（Squid；机制等价 MC squid，§9 原创）水生软体深褐橘斑贴图（build_mob.py）
        textures/mob_wolf.png               # t480 狼（Wolf；机制等价 MC 狼，§9 原创）灰狼毛皮贴图（build_mob.py）
        textures/mob_ocelot.png             # t481 豹猫（Ocelot；机制等价 MC 豹猫，§9 原创）斑点橙棕猫科贴图（build_mob.py）
        textures/mob_cat_tabby.png          # t481 猫变体 0 棕虎斑（t963 家猫花纹返修：旧全黑档退役；驯服豹猫随机毛色；build_mob.py）
        textures/mob_cat_ginger.png         # t481 猫变体 1 姜黄虎斑（驯服豹猫随机毛色；build_mob.py）
        textures/mob_cat_cream.png          # t481 猫变体 2 奶油暹罗（驯服豹猫随机毛色；build_mob.py）
        textures/mob_silverfish.png         # t487 银鱼（Silverfish；机制等价 MC 1.0 银鱼，§9 原创）灰白甲壳+体节横纹贴图（build_mob.py）
        textures/mob_nightwalker.png        # t727 夜行者（Nightwalker；机制等价 MC 1.0 末影人，§9 原创）暗紫黑细长黑影贴图（build_mob.py）
        textures/mob_nightwalker_eyes.png   # t727 夜行者独立眼睛发光层（透明底 + 亮紫白竖眼；QML 顶层小盒铺，pack 关同 pack 命中并存
        textures/entity_nightwalker.png     # t717 夜行者（Nightwalker；机制等价 MC enderman，§9 改名）64×32 实体 UV 贴图（build_entities_pack.py；黑曜石黑体+微紫缀点；t727 接入）
        textures/entity_nightwalker_eyes.png # t717 夜行者眼睛发光层（全透明+头前两粒紫光眼；机制等价 MC enderman_eyes；t727 接入）
        textures/entity_emberling.png       # t717 燃烬者（Emberling；机制等价 MC blaze，§9 改名）64×32 贴图（黄焰头+白热核+烟灰棒；t728 接入）
        textures/mob_fireball.png           # t728 燃烬者火球程序生成贴图（build_mob.py；16×16 橙黄自发光火球，与 Main.qml 火球 delegate 配色同源）
        textures/entity_endereye.png        # t729 暗渊之眼实体贴图（build_mob.py；16×16 小绿瞳珠：深绿珠身+暗绿竖瞳+白高光，EndEyeId 0x23A 寻路要塞）
        textures/mob_heart.png              # t878③ 爱心粒子贴图（build_mob.py；16×16 透明底像素心红 #ff3a5a：求偶/驯服爱心 BillboardQuad 升腾渐隐动画用，替代旧静态菱形立方）
        textures/entity_squid.png           # t717 鱿鱼 64×32 实体 UV 贴图（深蓝头+浅肚+触腕条；t730 自拼 3D 接入）
        textures/entity_minecart.png        # t717 矿车 64×32 贴图（铁灰壁+铆钉+木底板；t732 矿车斗重贴图；t768 底板带扩满重画为橡木板 course）
        textures/entity_enchant_book.png    # t717 附魔书 64×32 贴图（棕封金边+白纸页；t732 悬浮书重贴图）
        textures/entity_skin_default.png    # t717 玩家默认皮肤 64×32（蓝裤+棕发+肤色脸；机制等价 MC steve 皮肤布局，§9 程序自绘；t731 接入）
        textures/entity_skin_alex.png       # t717 皮肤变体 64×32（橙发+绿衣；机制等价 MC alex 变体；t731 接入）
        textures/armor_leather_layer_1.png  # t717 皮革盔甲 layer_1（头盔+胸甲+护腿；64×32 MC 盔甲 UV；t718 盔甲 3D 接入）
        textures/armor_leather_layer_2.png  # t717 皮革盔甲 layer_2（靴）
        textures/armor_iron_layer_1.png     # t717 铁盔甲 layer_1（浅灰+铆钉）
        textures/armor_iron_layer_2.png     # t717 铁盔甲 layer_2（靴）
        textures/armor_copper_layer_1.png   # t718 铜盔甲 layer_1（铜橙；本工程自创档无 pack 等价，恒程序层）
        textures/armor_copper_layer_2.png   # t718 铜盔甲 layer_2（靴）
        textures/armor_gold_layer_1.png     # t717 金盔甲 layer_1（金黄）
        textures/armor_gold_layer_2.png     # t717 金盔甲 layer_2（靴）
        textures/armor_diamond_layer_1.png  # t717 钻石盔甲 layer_1（青）
        textures/armor_diamond_layer_2.png  # t717 钻石盔甲 layer_2（靴）
        textures/armor_chainmail_layer_1.png # t717 链甲 layer_1（深灰+网眼孔）
        textures/armor_chainmail_layer_2.png # t717 链甲 layer_2（靴）
        textures/default_painting_kebab.png    # t717 画作 27 张（build_paintings.py 程序自绘抽象像素画；§9 原创画面；t720 Painting 方块接入）
        textures/default_painting_aztec.png
        textures/default_painting_aztec2.png
        textures/default_painting_bomb.png
        textures/default_painting_plant.png
        textures/default_painting_wasteland.png
        textures/default_painting_back.png
        textures/default_painting_alban.png
        textures/default_painting_courbet.png
        textures/default_painting_sea.png
        textures/default_painting_creebet.png
        textures/default_painting_sunset.png
        textures/default_painting_pool.png
        textures/default_painting_graham.png
        textures/default_painting_wanderer.png
        textures/default_painting_match.png
        textures/default_painting_skull_and_roses.png
        textures/default_painting_stage.png
        textures/default_painting_void.png
        textures/default_painting_bust.png
        textures/default_painting_wither.png
        textures/default_painting_donkey_kong.png
        textures/default_painting_skeleton.png
        textures/default_painting_burning_skull.png
        textures/default_painting_pigscene.png
        textures/default_painting_pointer.png
        textures/default_painting_fighters.png
        textures/icon_cobble.png
        textures/icon_coal_ore.png
        textures/icon_copper_ore.png             # t308 铜矿石立方体图标（build_cube_icons.py；石头底+橙铜斑+孔雀绿锈）
        textures/icon_diamond_ore.png            # t279 钻矿石立方体图标（build_cube_icons.py；石头底+青白菱斑晶体）
        textures/icon_gold_ore.png               # t308 金矿石立方体图标（build_cube_icons.py；石头底+金黄斑簇）
        textures/icon_lapis_ore.png              # t471 青金矿石立方体图标（build_cube_icons.py；石头底+群青深蓝斑簇+黄铁矿金点）
        textures/icon_redstone_ore.png           # t569 红石矿石立方体图标（build_cube_icons.py；石头底+鲜红菱斑矿粒；走过/挖掘点亮微弱红光）
        textures/icon_obsidian.png               # t472 黑曜石立方体图标（build_cube_icons.py；深紫黑火山玻璃；流体交互产物，钻石镐采掘）
        textures/icon_enchanting_table.png       # t474/t644 附魔台 0.75 矮盒图标（build_cube_icons.py --from-pack：pack 贴图 3D 渲染，与放置态一致）
        textures/icon_bookshelf.png              # t474/t644 书架立方体图标（--from-pack：顶=oak_planks 侧=bookshelf，与放置态 per-face 一致）
        textures/icon_iron_block.png             # t477/t644 铁块立方体图标（--from-pack：pack iron_block 贴图 3D 渲染，与放置态一致）
        textures/icon_coal_block.png             # t620/t644 煤炭块立方体图标（--from-pack：pack 贴图 3D 渲染）
        textures/icon_lapis_block.png            # t620/t644 青金石块立方体图标（--from-pack：pack 贴图 3D 渲染）
        textures/icon_diamond_block.png          # t620/t644 钻石块立方体图标（--from-pack：pack 贴图 3D 渲染）
        textures/icon_gold_block.png             # t620/t644 金块立方体图标（--from-pack：pack 贴图 3D 渲染）
        textures/icon_redstone_block.png         # t620/t644 红石块立方体图标（--from-pack：pack 贴图 3D 渲染）
        textures/icon_redstone_lamp.png          # t620/t644 红石灯立方体图标（--from-pack：off 态贴图 3D 渲染，与放置态默认一致）
        textures/icon_anvil.png                  # t477 铁砧立方体图标（build_cube_icons.py；顶=砧台+砧面+尖角 / 侧=深铁砧身+横向分层）
        textures/icon_anvil_chipped.png          # t477 微损铁砧立方体图标（build_cube_icons.py；顶=砧台+细裂纹 / 侧=深铁砧身）
        textures/icon_anvil_damaged.png          # t477 重损铁砧立方体图标（build_cube_icons.py；顶=砧台+粗裂纹网+缺角 / 侧=深铁砧身）
        textures/icon_pumpkin.png                # t482 南瓜立方体图标（build_cube_icons.py；顶=橙色瓜顶+短茎 / 侧=橙色瓜棱）
        textures/icon_effect_poison.png          # t715 中毒效果图标（build_effect_icons.py 程序自绘原创；绿色药水瓶剪影）
        textures/icon_effect_slowness.png        # t715 缓慢效果图标（build_effect_icons.py 程序自绘原创；深灰蓝厚重靴剪影）
        textures/icon_effect_fire.png            # t715 着火效果图标（build_effect_icons.py 程序自绘原创；黄芯火苗剪影）
        textures/icon_snow.png                   # t482 雪块立方体图标（build_cube_icons.py；各面=冷白冰晶噪点，同积雪层）
        textures/icon_end_portal.png             # t634/t644 末地传送门立方体图标（--from-pack：endframe_top+side 裁顶空白，与放置态一致）
        textures/icon_cobweb.png                 # t484 蜘蛛网 flat 2D 图标（build_cube_icons.py；透明底+灰白蛛丝放射网纹）
        textures/icon_rail.png                   # t484/t644 铁轨 flat 2D 图标（--from-pack：pack 直轨贴图，与放置态一致）
        textures/icon_golden_rail.png            # t638/t644 动力铁轨 flat 2D 图标（--from-pack：pack powered_rail 贴图）
        textures/icon_detector_rail.png          # t638/t644 探测铁轨 flat 2D 图标（--from-pack：pack detector_rail 贴图）
        textures/icon_redstone_torch.png         # t638/t644 红石火把 flat 2D 图标（--from-pack：pack 常亮态贴图）
        textures/icon_redstone_dust.png          # t660 红石粉导线 flat 2D 图标（断电线向贴图放大；红石 tab 方块形态条目）
        textures/icon_tnt.png                    # t485 TNT 立方体图标（build_cube_icons.py；各面=深红药柱+横向捆带+亮黄标识；沙漠神殿陷阱方块）
        textures/icon_lapis_outline.png          # t957 青金石「空缺风格」轮廓图标（build_lapis_outline.py 边缘提取离线生成：读 icon_lapis_item.png 像素→预乘亮度 Sobel→边缘近黑+内部压暗半透；附魔台青金石槽空位占位，EnchantingTableUI EnchantInputSlot）
        textures/icon_cut_sandstone.png          # t485 切制砂岩立方体图标（build_cube_icons.py；各面=暖沙色+内陷矩形装饰边框；金字塔外框装饰变体）
        textures/icon_mossy_cobble.png           # t486 苔石立方体图标（build_cube_icons.py；各面=圆石灰底+暗绿苔藓斑簇；丛林神殿主体）
        textures/icon_dispenser.png              # t486/t644 发射器立方体图标（--from-pack：正面为主投影，pack 贴图，与放置态一致）
        textures/icon_dropper.png                # t609/t644 投掷器立方体图标（--from-pack：正面为主投影，pack 贴图）
        textures/icon_stone_brick.png            # t487 石砖立方体图标（build_cube_icons.py；各面=石质灰底+砖块缝纹网格；要塞墙体主体）
        textures/icon_spawner.png                # t760 刷怪笼立方体图标（build_cube_icons.py；各面=铁灰栅栏笼格，cutout 孔被均值色填实；创造背包/中键复制 item 用）
        textures/icon_stone_brick_slab.png       # t487 石砖台阶立方体图标（build_cube_icons.py；3D 半高盒，砖纹）
        textures/icon_stone_brick_stairs.png     # t487 石砖楼梯立方体图标（build_cube_icons.py；3D L 阶，砖纹）
        textures/icon_lever.png                  # t490 杠杆立方体图标（build_cube_icons.py；pressure_plate shape+lever 贴图；手动点火机关）
        textures/icon_wood_button.png            # t490 木按钮立方体图标（build_cube_icons.py；pressure_plate shape+wood_button 贴图）
        textures/icon_stone_button.png           # t490 石按钮立方体图标（build_cube_icons.py；pressure_plate shape+stone_button 贴图）
        textures/icon_stone_pressure_plate.png   # t627/t644 石压力板 3D 薄板图标（--from-pack：smooth_stone 贴图）
        textures/icon_iron_pressure_plate.png    # t627/t644 铁压力板 3D 薄板图标（--from-pack：iron_block 贴图；重质）
        textures/icon_gold_pressure_plate.png    # t627/t644 金压力板 3D 薄板图标（--from-pack：gold_block 贴图；轻质）
        textures/icon_crafting_table.png         # t50/t492 工作台立方体图标（build_cube_icons.py；正面为主投影，显正面网格）
        textures/icon_dirt.png
        textures/icon_furnace.png                # t80/t492 熔炉立方体图标（build_cube_icons.py；正面为主投影，显炉口）
        textures/icon_grass.png
        textures/icon_gravel.png               # t761 沙砾立方体图标（build_cube_icons.py；灰砾石 3D 投影）
        textures/icon_iron_ore.png
        textures/icon_leaves.png
        textures/icon_log.png
        textures/icon_planks.png
        textures/icon_sand.png
        textures/icon_stone.png
        textures/icon_bedrock.png              # misc 二轮 基岩立方体图标（build_cube_icons.py 程序生成；创造可取 bedrock 物品）
        textures/icon_torch.png
        textures/icon_bed_red.png               # t387 床方块 8 色变体立方体图标（build_cube_icons.py 程序生成）
        textures/icon_bed_orange.png
        textures/icon_bed_yellow.png
        textures/icon_bed_green.png
        textures/icon_bed_cyan.png
        textures/icon_bed_blue.png
        textures/icon_bed_magenta.png
        textures/icon_bed_black.png
        textures/icon_bed_white.png               # t455 16 色床补齐 8 色新变体立方体图标（build_cube_icons.py 程序生成）
        textures/icon_bed_light_blue.png
        textures/icon_bed_lime.png
        textures/icon_bed_pink.png
        textures/icon_bed_gray.png
        textures/icon_bed_light_gray.png
        textures/icon_bed_purple.png
        textures/icon_bed_brown.png
        textures/icon_wool.png                    # t300/t455 羊毛白色立方体图标（build_cube_icons.py；曾漏 qrc 致创造调色板羊毛图标空，t455 补登）
        textures/icon_wool_orange.png             # t455 16 色 wool 其余 15 色变体立方体图标（build_cube_icons.py 程序生成）
        textures/icon_wool_magenta.png
        textures/icon_wool_light_blue.png
        textures/icon_wool_yellow.png
        textures/icon_wool_lime.png
        textures/icon_wool_pink.png
        textures/icon_wool_gray.png
        textures/icon_wool_light_gray.png
        textures/icon_wool_cyan.png
        textures/icon_wool_purple.png
        textures/icon_wool_blue.png
        textures/icon_wool_brown.png
        textures/icon_wool_green.png
        textures/icon_wool_red.png
        textures/icon_wool_black.png
        textures/icon_chest.png                 # t173/t179 箱子立方体图标（顶盖缝+侧铁箍；曾漏注册→运行时 WRN 无图标）
        textures/icon_farmland.png              # t234 耕地立方体图标（顶=干态翻耕土+侧泥土）
        textures/icon_wood_slab.png           # t145 不完整方块 flat 2D 区分图标
        textures/icon_wood_stairs.png
        textures/icon_wood_fence.png
        textures/icon_wood_door.png               # t644 --from-pack：上下半 per-face（upper+lower）3D 两格高薄板，与放置态一致
        textures/icon_iron_door.png               # t722 --from-pack：pack door_iron 上下半 3D 两格高薄板，与放置态一致（红石 tab）
        textures/icon_iron_trapdoor.png           # t723 --from-pack：pack iron_trapdoor 格子板 3D 合态薄板（红石 tab）
        textures/icon_wood_trapdoor.png
        textures/icon_wood_pressure_plate.png
        textures/icon_cobble_slab.png             # t412 圆石变体 3D dimetric 立体图标（圆石贴图按形状投影）
        textures/icon_cobble_stairs.png
        textures/icon_cobble_fence.png
        textures/icon_cobble_pressure_plate.png
        textures/icon_tall_grass.png               # t244 草丛 cross 方块 flat 2D 图标（透明底 + 绿草叶）
        textures/icon_wheat_crop.png               # t244 小麦作物 cross 方块 flat 2D 图标（取成熟阶段 7 麦穗）
        textures/icon_sandstone.png                # t394 砂岩立方体图标（顶=压实沙面 / 侧=层理带；build_cube_icons.py）
        textures/icon_cactus.png                   # t394 仙人掌立方体图标（顶=绿截面环纹 / 侧=棱脊+刺点；build_cube_icons.py）
        textures/icon_dead_bush.png                # t394 枯死的灌木 cross 方块 flat 2D 图标（透明底 + 棕褐干枝；build_cube_icons.py）
        textures/icon_snow_layer.png               # t525 积雪层薄板图标（1/8 厚薄板，区别于雪块满格立方；build_cube_icons.py render_partial_3d snow_layer）
        textures/icon_ice.png                      # t395 冰立方体图标（各面=浅蓝反光裂纹；build_cube_icons.py）
        textures/icon_pack_ice.png                 # t468 浮冰立方体图标（各面=实白细裂纹；build_cube_icons.py）
        textures/icon_blue_ice.png                 # t468 蓝冰立方体图标（各面=淡蓝纵向纹路；build_cube_icons.py）
        textures/icon_spruce_log.png               # t395 云杉原木立方体图标（顶=年轮截面 / 侧=深棕树皮；build_cube_icons.py）
        textures/icon_spruce_leaves.png            # t714 云杉树叶立方体图标（--from-pack：pack spruce_leaves × 云杉深蓝绿 tint）
        textures/icon_spruce_planks.png            # t466 云杉木板立方体图标（各面=深色木纹；build_cube_icons.py）
        textures/icon_spruce_slab.png              # t466 云杉台阶 3D 立体图标（半高盒深色木纹；build_cube_icons.py）
        textures/icon_spruce_fence.png             # t466 云杉栅栏 3D 立体图标（立柱+横档深色木纹；build_cube_icons.py）
        textures/icon_spruce_door.png              # t466/t644 云杉门 3D 立体图标（--from-pack：upper+lower 贴图，与放置态一致）
        textures/icon_lily_pad.png                 # t396 睡莲 flat 2D 图标（透明底+绿圆叶+V缺口；build_cube_icons.py）
        textures/icon_mushroom.png                 # t396 蘑菇 flat 2D 图标（透明底+米色菌柄+红底白斑菌盖；build_cube_icons.py）
        textures/icon_brown_mushroom.png           # t507 白蘑菇 / 棕蘑菇 flat 2D 图标（透明底+米色菌柄+棕色菌盖浅黄褐斑；build_cube_icons.py；蘑菇汤原料）
        textures/icon_flower_red.png               # t397 花 4 色 flat 2D 图标（透明底+绿茎+彩色花头；build_cube_icons.py）
        textures/icon_flower_yellow.png
        textures/icon_flower_blue.png
        textures/icon_flower_white.png
        textures/icon_sugarcane.png                # t397 甘蔗 flat 2D 图标（透明底+绿色节段细茎+顶部尖叶；build_cube_icons.py）
        textures/icon_ladder.png                   # t413/t519 木梯 flat 2D 图标（透明底 + 棕色两纵轨 + 4 道横梯级；build_ladder.py；t519 满格版）
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
        sounds/ui_click.wav
        sounds/door_open.wav
        sounds/door_close.wav
        sounds/hurt.wav
        sounds/mob_hurt.wav
        sounds/explosion.wav
        sounds/tool_break.wav                       # t315 工具耐久归零破损声（Hotbar::toolBroken → playToolBreak）
        sounds/ambient_wind.wav
        sounds/water_flow.wav
        sounds/water_step.wav
        sounds/lava.wav                             # t343 岩浆 proximity 环境音（build_sounds.py gen_lava 程序合成，§9 原创）
        sounds/thunder.wav                          # t386 雷声（build_sounds.py gen_thunder 程序合成，§9 原创；lightningStruck → playThunder）
        sounds/mob_idle.wav
        sounds/mob_idle_pig.wav
        sounds/mob_idle_cow.wav
        sounds/mob_idle_sheep.wav
        sounds/mob_idle_shambler.wav
        sounds/mob_idle_bones.wav
        sounds/mob_idle_stalker.wav
        sounds/mob_idle_spider.wav
        sounds/stronghold_hum.wav                   # t1021 要塞低鸣（要塞 region 门控 8s 循环；build_sounds.py 程序合成 §9 原创）
        sounds/mineshaft_drip.wav                   # t1021 矿井滴水（矿井 region 门控随机间隔单滴；同上）
        sounds/jungle_chirps.wav                    # t1021 丛林虫鸣（丛林神殿 region 门控颤音簇，昼稀/夜密由调度间隔控制）
        sounds/desert_night_wind.wav                # t1021 沙漠夜风（沙漠神殿区+夜+露天三重门控 8s 循环；同上）
        sounds/achievement.wav                      # t1021 成就解锁 toast 音（structureEntered 等成就 toast 同源共享）
        sounds/chest_open.wav                       # t1021 箱子开启音（方块箱 + 箱子矿车同源；openChest 单一通道）
        sounds/chest_close.wav                      # t1021 箱子关闭音（closeChest 单一通道）
        sounds/note_pitch_00.wav                    # t1028 音符盒 25 档音高钢琴音（build_sounds.py gen_note_piano
        sounds/note_pitch_01.wav                    #   程序合成 §9 原创；n=0..24 半音，n=9=A4=440Hz；
        sounds/note_pitch_02.wav                    #   AudioManager.noteClips[pitch] 按调音段选播；音色族=
        sounds/note_pitch_03.wav                    #   播放速率倍移近似（木×0.5/石×1.0/沙×2.0，登记简化）。
        sounds/note_pitch_04.wav
        sounds/note_pitch_05.wav
        sounds/note_pitch_06.wav
        sounds/note_pitch_07.wav
        sounds/note_pitch_08.wav
        sounds/note_pitch_09.wav
        sounds/note_pitch_10.wav
        sounds/note_pitch_11.wav
        sounds/note_pitch_12.wav
        sounds/note_pitch_13.wav
        sounds/note_pitch_14.wav
        sounds/note_pitch_15.wav
        sounds/note_pitch_16.wav
        sounds/note_pitch_17.wav
        sounds/note_pitch_18.wav
        sounds/note_pitch_19.wav
        sounds/note_pitch_20.wav
        sounds/note_pitch_21.wav
        sounds/note_pitch_22.wav
        sounds/note_pitch_23.wav
        sounds/note_pitch_24.wav
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

# t880：Main.qml qmlcachegen AOT 产物超 PE/COFF 32768 段上限（每 AOT 函数一段；Main.qml 13.5k 行 + 本批
#   t876/t878/t880 增量后越限 → as 报 "too many sections (32768)"）。-mbig-obj 抬段上限到 2^31（GAS 选项；
#   本工具链 g++ 驱动不识别裸 -mbig-obj，须 -Wa,-mbig-obj 传汇编器）。仅 Windows/MinGW 有此限制
#   （MSVC/ELF 无段上限），MINGW 门控。
if(MINGW)
    target_compile_options(voxelsandbox PRIVATE -Wa,-mbig-obj)
endif()

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

# t740 红石激活矩阵冒烟测试（无 GUI 控制台程序）：直接实例化 World（Core+World 两层源码直编，
#   不链接主 exe），按「信号源×接收器」全组合驱动 tickRedstone 核对通电 / 降沿 + 场景探针
#   （火把后放 TNT / 粉衰减 / 超距 / torch-on-block 斜下粉 / NOT 门）。tools/ 是开发期工具
#   （同 build_*.py 资产生成脚本），不随主程序部署；全过 exit 0，FAIL 行 = 电力链断点直接暴露。
#   t737 环线探针附加：World 层 mesher（partialblockgeometry —— 拐角象限断言同源直调）与
#   Entities 层 MinecartManager（矿车绕圈；两者向下只依赖 Core+World，不引入 Game/QML）。
#   t762 黑曜石挖掘规则探针附加：Game 层 ToolRegistry（miningTime/canHarvest 纯表查询——只向下依赖
#   Core 的 BlockRegistry，不引入 PlayerController/QML）。
#   t763 附魔数值生效链探针附加：Game 层 Hotbar（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源；随附
#   enchantregistry/armor/recipe/smelting 表 + Core resourcepackmanager —— Hotbar 向下仅依赖这些纯表）。
add_executable(redstone_matrix_test
    # R20.03 测试分层：原 3.4MB 单 TU（tools/redstone_matrix_test.cpp，-j 1 编译 ~85min）拆为
    #   main + helpers + 8 段 TU（tools/matrix/；段体 = 原 main 体连续行区间逐字节搬移，
    #   段 md5 存证 build/r2003_proof/）——加探针只重编对应段 + 链接。
    tools/matrix/matrix_main.cpp
    tools/matrix/matrix_helpers.cpp
    tools/matrix/section01_redstone_core.cpp
    tools/matrix/section02_early_probes.cpp
    tools/matrix/section03_mid_probes.cpp
    tools/matrix/section04_instancing.cpp
    tools/matrix/section05_render_ui.cpp
    tools/matrix/section06_worldgen_drag.cpp
    tools/matrix/section07_chests_mobs.cpp
    tools/matrix/section08_recent.cpp
    # R20.05 基础类型探针段（新段置尾：runAll 末执行，不与原八段共享 rig 写状态；4 腿
    #   r2005a-d，filter 词 r2005）。随段直编两个 Core 头（mathtypes/result，header-only）。
    tools/matrix/section09_foundations.cpp
    src/Core/mathtypes.h
    src/Core/result.h
    # R20.06 Command/Event/Snapshot 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触——
    #   3 腿 r2006a-c，filter 词 r2006）。随段直编三个 Core 头（command/event/snapshot，
    #   header-only；result.h 本单增补错误码域 kErrQueueFull + QObjectFree 概念）。
    tools/matrix/section10_command_event_snapshot.cpp
    src/Core/command.h
    src/Core/event.h
    src/Core/snapshot.h
    # R20.07 GameSession 探针段（置尾先例沿用：runAll 末执行，filter 词 r2007；4 腿
    #   r2007a-d = 双路径一致 / 固定 Tick+源码钉 / 暂停+无头连跑 / 阴性轮。随段直编
    #   src/Game/gamesession.h（header-only 编排壳，Q_OBJECT → AUTOMOC 经本清单入册；
    #   自建 fresh 小世界 ×3——world/chunk 系源文件已列，无新增 .cpp）。
    tools/matrix/section11_gamesession.cpp
    src/Game/gamesession.h
    # R20.08 WorldFacade 探针段（置尾先例沿用：runAll 末执行，filter 词 r2008；4 腿
    #   r2008a-d = 查询面等价 / 写入面等价 / 结构钉+mesher 脏门端到端 / 阴性常驻+会话回归。
    #   随段直编 src/World/worldfacade.h（header-only 收窄视图，非 QObject 无 AUTOMOC）；
    #   自建 fresh 小世界 ×4——world/chunk 系源文件已列，无新增 .cpp）。
    tools/matrix/section12_worldfacade.cpp
    src/World/worldfacade.h
    # R20.09 EditBuffer 探针段（置尾先例沿用：runAll 末执行，filter 词 r2009；4 腿
    #   r2009a-d = 类型面可测 / 同格合并+终态 parity / 多 chunk 批量收口 / 源码钉+blockState。
    #   随段直编 src/Core/editbuffer.h（header-only 值类型组件，非 QObject 无 AUTOMOC）+
    #   src/Core/command.h（R20.09 落 Review #3① blockState 扩字段）；r2009a 纯类型腿零世界，
    #   r2009b-d 自建 fresh 小世界 ×4——world/chunk 系源文件已列，无新增 .cpp）。
    tools/matrix/section13_editbuffer.cpp
    src/Core/editbuffer.h
    src/Core/command.h
    # R20.10 Chunk lifecycle 探针段（置尾先例沿用：runAll 末执行，filter 词 r2010；4 腿
    #   r2010a-d = 六态全图 / 固定世界默认稳态 / 卸载-重载 mesher 门端到端 / 源码钉+存档往返。
    #   随段直编 src/World/chunklifecycle.h（header-only 六态类型 + 转移表单一权威，非 QObject
    #   无 AUTOMOC）；r2010a 纯图腿零世界，r2010b-d 自建 fresh 小世界——world/chunk 系源文件
    #   已列，无新增 .cpp）。
    tools/matrix/section14_chunklifecycle.cpp
    src/World/chunklifecycle.h
    # R20.11 GenerationJob 同步版 ChunkScheduler 探针段（置尾先例沿用：runAll 末执行，filter
    #   词 r2011；4 腿 r2011a-d = 请求模型（ID/重复合并/优先级/满载）/ 取消+过期丢弃 / 失败面
    #   Result 穿透+worker 可替换性 / 生命周期互锁+固定世界零变化+源码钉。随段直编
    #   src/World/generationjob.h（header-only 请求编排层，非 QObject 无 AUTOMOC）；
    #   r2011a-c 纯请求模型腿零世界，r2011d 裸 3×3 ChunkManager + fresh 小世界——world/chunk
    #   系源文件已列，无新增 .cpp）。
    tools/matrix/section15_generationjob.cpp
    src/World/generationjob.h
    # R20.12 后台 GenerationJob 探针段（置尾先例沿用：runAll 末执行，filter 词 r2012；4 腿
    #   r2012a-d = 非阻塞真线程身份 / 纯函数逐位恒等（承重墙：后台 ≡ 同步共享函数 ≡ 真世界锚）/
    #   epoch 丢弃+取消的后台版 / 退出安全+R20.10 生命周期互锁+固定世界零变化+源码钉。随段直编
    #   src/World/backgroundgeneration.h（真线程 worker：标准线程原语 + 锁护卫 FIFO，非 QObject
    #   无 AUTOMOC；本单头注释立此存照的线程选型依据在其中）+ src/World/terraingen.h（纯地形
    #   生成单一权威——generate() 列体迁出后的共用函数）；r2012a-c 零世界（裸 scheduler+后台
    #   worker），r2012d 裸 3×3 ChunkManager + fresh 小世界——world/chunk 系源文件已列，无新增
    #   .cpp；t1023c 事实钉同变更修订（src/ 首个受认可线程原语落点 = backgroundgeneration.h））。
    tools/matrix/section16_backgroundgen.cpp
    src/World/backgroundgeneration.h
    src/World/terraingen.h
    # R20.13 MeshBuilder 探针段（置尾先例沿用：runAll 末执行，filter 词 r2013；4 腿 r2013a-d =
    #   端到端逐位等价承重墙[9 变体×2 chunk 顶点/索引字节比对 + 已知场景精确 Δ]/快照自持性/
    #   FrameProfiler 计数穿透+消费面回归/结构钉+worldgen 锚。随段直编 src/World/meshbuilder.{h,cpp}
    #   （网格算法单一权威，非 QObject 无 AUTOMOC；chunkgeometry.cpp 已在下方直编——委托面回归用）；
    #   r2013a-c 自建 fresh 小世界 48×48×96 s82（section11+ 先例，rig 世界 w 零接触），r2013d 零世界
    #   （纯源码钉 + 裸快照）。
    tools/matrix/section17_meshbuilder.cpp
    src/World/meshbuilder.cpp
    src/World/meshbuilder.h
    # R20.14 EntityStore 探针段（置尾先例沿用：runAll 末执行，filter 词 r2014；4 腿 r2014a-d =
    #   store↔Adapter 孪生行为等价承重墙[同操作序列 spawn/合并/拾取/despawn/焚毁逐位一致]/
    #   可见实体快照权威[沿重建+独立复制]/Adapter 消费面回归[notify 沿计数+feeder 面零变化]/
    #   结构钉[QObjectFree 复述+搬移件落位+模拟本体禁回流反探+零 QML]。随段直编
    #   src/Entities/entitystore.cpp（模拟单一权威，非 QObject 无 AUTOMOC——已列下方探针附加）；
    #   itementitymanager.cpp 已在下方直编（Adapter 委托面回归用）；r2014a-d 零世界（r2014a 焚毁
    #   子面用裸 World 直排岩浆格——非 rig 世界 w）。
    tools/matrix/section18_entitystore.cpp
    # R20.15 SaveCoordinator 探针段（置尾先例沿用：runAll 末执行，filter 词 r2015；4 腿 r2015a-d =
    #   统一 SaveGeneration 代次语义[三部分一代次+两域正交+单调]/冻结-持久化分离承重墙[冻结后改动
    #   活体→重读=冻结点≠活体+损坏 chunk 降级一致]/故障注入+恢复三态[真锁·注入 Player·崩溃前收尾·
    #   缺台账→Fresh]/结构钉[QObjectFree 复述+协议关键步+SQLiteAdapter 全盲反探+零 QML 零接线+
    #   schema 版本零 bump]。随段直编 src/World/savecoordinator.{h,cpp}（存档协调层，非 QObject
    #   无 AUTOMOC；WorldStore 零改动 = SQLiteAdapter 现状保持——plan R20.15「先保持 SQLiteAdapter」）。
    #   r2015a-c 自建 fresh 小世界 + 临时 SQLite 库（QDir::temp() pid 键名，测试自清理），r2015d 零世界。
    tools/matrix/section19_savecoordinator.cpp
    src/World/savecoordinator.cpp
    src/World/savecoordinator.h
    # §29.4-P1 GenerationPolicy 策略层骨架探针段（置尾先例沿用：runAll 末执行，filter 词
    #   r2016；4 腿 r2016a-d = 默认关零变化承重墙[disabled 任意输入两列表恒空]/激活请求面
    #   [toRequest 恰 = 生成半径内 Absent 集 + priority 升序字典序全确定性]/驱逐面[半径外驻留
    #   恰入一次距离降序 + [render,gen] 环带不驱逐（D4）+ 驱逐只看 gen 半径]/确定性+规整一致性
    #   +结构钉[禁 QObject/线程原语/World 指针反探 + 零调用点迁移零 QML]。随段直编
    #   src/World/generationpolicy.h（header-only 非 QObject 纯决策值组件，无 AUTOMOC，
    #   §29.4 D1/D4/D5 语义依据在其头注释）；r2016a-d 全段零世界（缝 = QMap 合成生命周期表），
    #   无新增 .cpp——world/chunk 系源文件已列）。
    tools/matrix/section20_generationpolicy.cpp
    src/World/generationpolicy.h
    # §29.4-P2 ChunkStreamDriver 玩家移动驱动探针段（置尾先例沿用：runAll 末执行，filter 词
    #   r2018；4 腿 r2018a-d = 默认关惰性承重墙[含关态在途账逐位不动]/请求跟随+取消面[含合并
    #   别名 dedupe + 完成后重入重提交]/风暴背压[满载拒绝可见+排干收敛]/异步收割+结构钉[kind
    #   纯度+确定性重放+反探]。随段直编 src/World/chunkstreamdriver.h（header-only 非 QObject
    #   纯编排值组件，无 AUTOMOC，§29.4 P2 原文 + D1/D4 语义依据在其头注释）；r2018a-d 全段
    #   零世界（缝 = QMap 合成生命周期表，rig 零接触），无新增 .cpp——world/chunk 系源文件已列）。
    tools/matrix/section21_chunkstreamdriver.cpp
    src/World/chunkstreamdriver.h
    # §29.4-P3 ChunkEvictor 卸载+Edits-on-evict 探针段（置尾先例沿用：runAll 末执行，filter 词
    #   r2019；4 腿 r2019a-d = 默认关惰性承重墙[关态两缝不可达 + P2 形态双生恒等]/驱逐回灌往返
    #   [真 SaveCoordinator + fresh 临时库 + 真世界生命周期表：persist 先于转移调用序柱 + 合法边
    #   ⑥⑦ + Load kind 重入回灌逐位恒等]/失败中止[persist 失败 aborted 不转移 + 重报收敛 + 拒绝
    #   有界 + null 缝 fail-safe]/结构钉+kind 选择[双头钉 + Load 恰经缝门控 + 确定性重放]。随段
    #   直编 src/World/chunkevictor.h（header-only 非 QObject 纯编排值组件，无 AUTOMOC，§29.4
    #   P3 原文 + D5 语义依据在其头注释）；r2019a/c/d 零世界（自校验 QMap 缝），r2019b 真世界
    #   + 临时库（r2015 先例），rig 世界 w 零接触，无新增 .cpp——world/chunk 系源文件已列）。
    tools/matrix/section22_chunkevictor.cpp
    src/World/chunkevictor.h
    # D6 worker meshing 探针段（置尾先例沿用：runAll 末执行，filter 词 r2020；4 腿 r2020a-d =
    #   确定性等价承重墙[8 形态快照 worker ≡ 同步直调逐位恒等：顶点/索引/统计三面]/队列纪律
    #   [容量压小满载 kErrQueueFull 可见拒绝 + 拒绝项不入账 + 接受项恰一产出按 requestId 对账]/
    #   析构卫生[积压在途析构 join 有界 + 丢弃经 teardown 外哨可见 + 可重建性]/结构钉+修订后
    #   t1023c 双文件白名单段内自证+零生产接线反探[MeshWorker 记号只在 meshworker.h]+线程身份。
    #   随段直编 src/World/meshworker.h（header-only 非 QObject，r2012 backgroundgeneration.h
    #   同门形态：std::thread+mutex+cv、析构 stop+discard+join、队列 64 满载可见拒绝、worker 执
    #   行体唯一调用 MeshBuilder::build——meshbuilder.cpp 已列；无新增 .cpp）；r2020a 自建 fresh
    #   小世界 48×48×96 s82 采集真实快照（section11+ 先例，rig 世界 w 零接触），r2020b/c/d 零世界）。
    tools/matrix/section23_meshworker.cpp
    src/World/meshworker.h
    # §29.4-P4 QML 动态化探针段（置尾先例沿用：runAll 末执行，filter 词 r2021；4 腿 r2021a-d =
    #   固定世界零变化承重墙[驻留集枚举 ≡ 旧 t276 固定网格循环序逐项恒等：3×3 方阵 + 2×3 非方阵
    #   序判别 + revision 初始 0 + 编辑零沿 + keyAt 越界空表 + 枚举键直驱 ChunkGeometry 非空
    #   mesh]/模型响应性[合法转移沿 revision/发射精确对账：⑥-1 ⑦零 bump ①②③恰 +1 于 ③ ⑤④
    #   集内零沿 非法拒零沿 ⑥⑧加回 +1；枚举随增删逐项跟随]/QML 真链响应性[真 QQmlEngine × 真
    #   World × setData slot 池镜像（Main.qml rebuildChunkSlotPool 消费逻辑最小复刻，t874 家族
    #   同门）：沿同步直达 handler、池随增删、零警告]/钉面[world.h 三新成员逐一正面钉 +
    #   world.cpp 枚举序单点权威钉 + Main.qml 消费面精确计数 + 旧固定网格循环反探退役 +
    #   setChunkLifecycle 禁入 QML + 玩法路径标记在位 + r2007b 零迁移复述]。随段零新增 src——
    #   world/chunk 系源文件已列；r2021a/b 自建 fresh 小世界，r2021c 真链 harness，rig 世界 w
    #   零接触）。
    tools/matrix/section24_qmldynamization.cpp
    # §29.5-W1 稀疏世界核探针段（置尾先例沿用：runAll 末执行，filter 词 r2022；4 腿 =
    #   fixed 零变化承重墙[构造模式/六态稳态/驻留集全网格/越界门逐门/D4 参数归一化]/
    #   未加载域 OOB 等价语义族[正负出界逐门同值 + 加载翻转 + 生命周期 forwarder 驱逐回环]/
    #   同 seed 区域恒等承重[双世界纯函数门三面对账 + 已加载 chunk 对权威缓冲逐体素恒等含
    #   负坐标 + 内陆 envelope 锚]/结构钉[Fixed 默认参 + worldstore 禁触反探 + 单列权威 +
    #   生命周期唯一入口 + QML/组件族禁出]。随段直编 src/World/backgroundgeneration.h
    #   （generateTerrainChunk 权威缓冲对照面——已列）+ chunklifecycle.h（已列）；r2022 前三腿
    #   自建 fresh 小世界族（固定世界四 setter incantation + sparse 构造缝同 seed 同 dims
    #   双世界），rig 世界 w 零接触；无新增 .cpp——world/chunk 系源文件已列）。
    tools/matrix/section25_sparseworld.cpp
    # §29.5-W1b sparse population parity 探针段（置尾先例沿用：runAll 末执行，filter 词 r2023；
    #   4 腿 r2023a-d = fixed 生成零变化墙[同 seed 双 generate 逐位恒等 + generate() 全 pass 调用
    #   序字面钉 + population 反探]/parity 承重墙[sparse ≡ fixed 9 内部 chunk 全 16×16×H 体素
    #   id+state 逐位恒等——(b) 级重放 pass 全纳入 + 结构族豁免面前置确认 + (b) 签名非空转]/
    #   加载顺序无关[radius 0 双世界两种 loadChunkAt 次序同组 chunk（含负坐标）逐位恒等]/
    #   结构钉[populate 驱动体代表窗口调用 + scaffold 生命周期/拆卸单点 + 结构族豁免反探]。
    #   r2023a-c 自建 fresh 小世界族（80×80×96 s82——5×5 chunk，radius 2 预生成恰覆全核心域/
    #   radius 0 按需 loadChunkAt），rig 世界 w 零接触；r2023d 纯源码钉；无新增 .cpp——
    #   world/chunk 系源文件已列）。
    tools/matrix/section26_sparse_population.cpp
    # §29.5-W2 位置源 + 驱动接线 探针段（置尾先例沿用：runAll 末执行，filter 词 r2024；
    #   4 腿 r2024a-d = fixed 零活动承重墙[fixed 世界 GameSession 无驱动器无 worker，全
    #   tick/移动扫掠全状态逐位不动]/通电走查承重[真 PlayerController 走查 × 真驱动器 × 真线程
    #   worker：①②③沿真实驱动 + OOB 门翻真 + residentChunkRevision 沿 + 半径取消 + 内容
    #   抽样柱 ≡ fixed 孪生]/真线程收割+#7 双面同拍契约[每 job 恰一数据一条 outcome + 双面
    #   排干零积压 + 线程身份 + 背压恰 17 拒绝 + 重请求重生成]/结构钉[会话通电门 + #7 函数域
    #   同拍钉 + adopt 落位链钉 + QML/worldstore 反探]。
    #   r2024a-c 自建 fresh 小世界族（sparse 80×80×96 s42 radius 0 + fixed 48×48×96 s82 孪生），
    #   rig 世界 w 零接触；无新增 .cpp——gamesession.h/world 系源文件已列）。
    tools/matrix/section27_streaming_wiring.cpp
    # §29.5-W3 驱逐 + Edits-on-evict 落盘 探针段（置尾先例沿用：runAll 末执行，filter 词
    #   r2025；4 腿 r2025a-d = fixed 零活动墙[fixed 会话连附加表都不构造 + 全状态逐位不动]/
    #   驱逐回灌承重[真临时库 fresh+用后即删：编辑过的半径外 chunk → persist 先于转移调用序
    #   柱 → ⑥⑦到 Absent + 擦槽 → residentChunkRevision 沿 → 重入半径附加表命中 → blob 物化
    #   逐位恒等（编辑+未编辑区都验）+ population 跳过断言]/失败中止+实体语义[persist 真锁
    #   注入失败 → 保持驻留 + aborted 计数 + 重报收敛；驱逐区内 mob/掉落物先于转移移除
    #   （EntityStore/EntityManager despawnInChunk——MC 引证随单）]/结构钉[worldstore 禁触
    #   反探 + 附加表 additive 正面钉（零 bump/旧码盲）+ 先落盘后转移源钉 + restore 跳
    #   population 函数域钉 + QML 零触碰 + fixed 墙]。
    #   r2025a-c 自建 fresh 小世界族（sparse 80×80×96 s42 radius 0 + 真 PlayerController 沿
    #   面）+ 真临时 SQLite 库（QDir::temp() pid 键名，测试自清理，绝触 saves/）；r2025d 纯
    #   源码钉。随段直编 src/World/chunkstore.{h,cpp}（per-chunk 附加表，r2015 additive 先例
    #   同款——独立命名连接零 bump；worldstore.{h,cpp} 一行禁触）。
    tools/matrix/section28_eviction_persistence.cpp
    # §29.5-W4 bake→worker 网格化 探针段（置尾先例沿用：runAll 末执行，filter 词 r2026；
    #   4 腿 r2026a-d = fixed 零变化墙[无 worker 构造 + 全同步内联 + F3 计数恒零]/异步等价
    #   承重墙[sparse 流式世界真执行器线程 + tick 尾收割拍 → 收割应用 ≡ 同步直调
    #   MeshBuilder::build 逐位恒等（4 段几何全顶点/索引缓冲）+ residentChunkRevision 沿
    #   零漂移 + 执行器账面对账]/回退+卫生[满载/已停拒绝 → 内联回退计数可见 + 最新快照胜
    #  （路由层注销 miss 可见 + 交付回调自证）+ 解绑回固定门 + clearMesh 注销卫生]/结构钉
    #   [F3 win 行 worker 列正面钉 + 行为级 flush 报告钉 + QML 零触碰反探 + 收割拍单点
    #   收口 + fixed 零构造源序钉 + World 桥面钉 + 单一权威兼容反探]。
    #   r2026a/b/d 自建 fresh 小世界族（sparse 80×80×96 s42 radius 0 + fixed 48×48×96 s82
    #   孪生）；r2026c 独立执行器 + 手工收割（不经会话收割拍——NEG-1 独立性设计）；真线程
    #   收敛全走 deadline 轮询（防 flake），rig 世界 w 零接触；无新增 .cpp——chunkgeometry/
    #   gamesession/world 系源文件已列）。
    tools/matrix/section29_wiring_meshworker.cpp
    # §29.5-W5 后端 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触；4 腿 r2027a-d =
    #   fixed 零活动墙 / 流式存读往返承重墙 / D3 转换往返 / 结构钉。filter 词 r2027。
    #   随段直编 src/World/chunkstore.cpp（W5 扩展：stream_worlds 元数据 + 行全量读回）+
    #   src/World/worldstore.{h,cpp}（读档 blob 通路只读 Q_INVOKABLE 消费面——worldstore
    #   零改动纯使用）；自建 fresh 小世界族（sparse 80×80×96 s42 radius 0 + fixed/sparse-core
    #   48×48×96 s82）+ 真临时 SQLite 库（fresh + 用后即删，saves/ 零触碰）+ 真线程收敛
    #  （deadline 轮询防 flake））。
    tools/matrix/section30_streaming_persistence.cpp
    src/World/chunkstore.cpp
    src/World/chunkstore.h
    # §29.5-W5b 流式 UI 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触；3 腿 r2028a-c =
    #   默认关零变化墙 / 开关+转换通电面[真 QQmlEngine×真桥单例 real-chain] / QML 面钉。filter 词
    #   r2028。随段直编 src/Game/streamingbridge.{h,cpp}（W5b QML 消费桥——QML 单例生产/QML 同
    #   对象）+ 复用已列 chunkstore/worldstore/gamesession/playercontroller/worldclock 系源文件；
    #   真临时 SQLite 库（fresh + 用后即删，saves/ 零触碰）+ 真线程收敛（deadline 轮询防 flake））。
    tools/matrix/section31_streaming_ui.cpp
    src/Game/streamingbridge.cpp
    src/Game/streamingbridge.h
    src/Core/blockregistry.cpp
    src/Core/frameprofiler.cpp
    src/Core/resourcepackmanager.cpp
    # t1055 agent-review 残余清偿合集一 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触；
    #   4 腿 r2029a-d = scheduler detachWorker 销账守卫 / lastDirtyChunks 值快照语义 /
    #   GenerationWorker submitAsync fail-fast 默认[kErrAsyncNotImplemented=204 + pumpAsync
    #   非背压交接失败可见穿透] / weather 0 值回落 qInfo 诊断。filter 词 r2029。裸 scheduler +
    #   零线程脚本化替身 worker 族（r2010bb/r2017b 先例）+ fresh 小世界 ×2。）
    tools/matrix/section32_residual_sweep.cpp
    # t1057 SaveCoordinator 生产接线 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触；
    #   4 腿 r2031a-d = 零变化墙[Clean 路径返回语义/saveOkCount/参数透传/旧档 Fresh 读档回归] /
    #   marker→complete 往返+中断恢复收敛[真保存走桥 + FaultHook 注入中途失败] /
    #   #5② 开库失败可区分[真锁占 + open 级失败双面目] / 结构钉[worldstore 零触碰反探 +
    #   additive 正面钉 + QML 两处例外面钉]。filter 词 r2031。随段直编 src/Game/savebridge.{h,cpp}
    #   （t1057 QML 消费桥——QML 单例生产/QML 同对象）+ 复用已列 savecoordinator/worldstore/
    #   streamingbridge 系源文件；真 QQmlEngine real-chain（r2028b 同门）+ 真临时 SQLite 库
    #   （fresh + 用后即删，saves/ 零触碰）。）
    tools/matrix/section33_savebridge_wiring.cpp
    src/Game/savebridge.cpp
    src/Game/savebridge.h
    # t1059 P5 观测前置 探针段（置尾先例沿用：runAll 末执行，rig 世界零接触；3 腿 r2033a-c =
    #   fixed 全零行+行格式钉[行恒在零值 + 字段解析钉 + frameprofiler 拼行层/gamesession 推送面
    #   源码正面钉] / 流式行计数与权威观测面逐项恒等[四窗 flush 界定：初载 3×3 精确 8 / 取消
    #   走查恰 9 / ChunkGeometry 桥提交网格收割 / 真临时库驱逐 evP=1 evE=4 evS=4——行字段 ==
    #   stats 差分/收割计数/轨迹分域增量] / dt 负/NaN 分域计数[三类精确 + 零变化墙 + 读面钉]。
    #   filter 词 r2033。随段复用已列 gamesession/frameprofiler/chunkgeometry/chunkstore 系源
    #   文件；sparse fresh 小世界族（80×80×96 s42 radius 0）+ 真临时 SQLite 库（fresh + 用后
    #   即删，saves/ 零触碰）+ 真线程收敛（deadline 轮询防 flake）。）
    tools/matrix/section34_observation_lines.cpp
    # t1022 键位映射表探针附加：Core 层 KeybindManager（默认表完整性 / 冲突拒收 / settings.json
    #   round-trip——显式 storePath 密闭不触真实档）。PlayerController setKey 规范化消费同链直编。
    src/Core/keybindmanager.cpp
    src/Core/keybindmanager.h
    # t813 构建版本戳探针（stamp YYYY-MM-DD HH:MM 格式 / git 短哈希 7-10 hex；Core 叶子直编）。
    src/Core/buildinfo.cpp
    src/Game/toolregistry.cpp
    src/Game/enchantregistry.cpp
    src/Game/armor.cpp
    src/Game/recipe.cpp
    src/Game/smelting.cpp
    src/Game/hotbar.cpp
    # t755 死亡态硬锁探针附加：Game 层 PlayerState（致死落库 0 / heal 死亡免疫 / respawn 复位链；
    # 只向下依赖 Core/Qt，不引入 PlayerController/World/QML）。
    src/Game/playerstate.cpp
    src/Game/playerstate.h
    src/World/chunk.cpp
    src/World/chunkmanager.cpp
    src/World/world.cpp
    src/World/partialblockgeometry.cpp
    src/Entities/minecartmanager.cpp
    # 审查 #1 末影眼巡航高度回归探针附加：Entities 层 EntityManager（spawnEnderEye 后断言
    # enderEyeCruiseYAt == origin.y()+8；向下只依赖 Core+World，不引入 Game/QML）。
    src/Entities/entitymanager.cpp
    # t804 掉落物火焚探针附加：Game 层 ItemEntityManager（item 入 Fire 格 0.8s 焚毁 + itemBurned 烟信号
    # + 出火熄火；向下只依赖 Core/World/Qt，不引入 PlayerController/QML）。
    src/Game/itementitymanager.cpp
    # R20.14 EntityStore 探针附加：Entities 层掉落物模拟单一权威直驱（r2014a 孪生等价 / r2014b
    #   快照权威 / r2014c Adapter 消费面回归 / r2014d 结构钉；向下只依赖 Core/World，不引入 QML）。
    src/Entities/entitystore.cpp
    # t805 船上岸回归探针附加：Entities 层 BoatManager（水/陆速比 + 同层湿沙挡停 + 冰面豁免保留 +
    # 贴岸倒退；向下只依赖 Core+World，不引入 Game/QML）。
    src/Entities/boatmanager.cpp
    # review #22 农夫计数回放链式补前置探针附加：Game 层 PlayerProgress（loadVariant 计数回放 →
    # farmer 全祖先链解锁 + sniper 不补链对照；向下只依赖 Core/Qt，不引入 World/QML）。
    src/Game/playerprogress.cpp
    src/Game/playerprogress.h
    # t814 真消费端探针附加：Game 层 PlayerController 直编（firePowerTnt / fireDispenserAtQml 真方法执行
    # —— 此前矩阵只对 World 原始信号计数，消费端从未被自动化测试跑过）。PlayerController 是 QQuickItem
    # 派生 → 链 Qt6::Quick；随附其全部向下依赖（dispenserstore / loottable / raycast / xporbmanager /
    # worldclock —— 均只向下依赖 Core/World/Entities，不引入 QML 场景）。
    src/Game/playercontroller.cpp
    src/Game/playercontroller.h
    src/Game/dispenserstore.cpp
    src/Game/dispenserstore.h
    # t1013 箱子矿车内容键探针附加：Game 层 ChestStore（转正 / 回生 / 掉落链断言源；同 dispenserstore
    #   依赖面 —— 只向下依赖 Core/Qt，不引入 World/QML）。
    src/Game/cheststore.cpp
    src/Game/cheststore.h
    src/Game/loottable.cpp
    src/Game/raycast.cpp
    src/Entities/xporbmanager.cpp
    src/Entities/xporbmanager.h
    src/World/worldclock.cpp
    src/World/worldclock.h
    # t822 铁砧附魔实机复现二探针附加：World 层 WorldStore（savePlayerData/loadPlayerData 真链 SQLite
    #   round-trip —— t622 附魔/实例名序列化此前从未有自动化探针；探针用临时目录绝对路径库，
    #   saves/ 不受影响。Qt6::Sql 随 target_link_libraries 补）。
    src/World/worldstore.cpp
    src/World/worldstore.h
    # t880 异形物品 3D 族探针：ItemShapeGeometry 几何契约直调（顶点数 / bounds 行为级断言）——
    #   QQuick3DGeometry 实例化需 Qt6::Quick3D 链接（已在下方 target_link_libraries）。
    src/Renderer/itemshapegeometry.cpp
    src/Renderer/itemshapegeometry.h
    # t965 形态按钮组探针：BlockCube 状态态变顶面瓦片（耕地干/湿、末地框无眼/有眼）行为级直调
    #   （stateTileOverride Core 权威消费；同 ItemShapeGeometry t880 直调先例，Quick3D 已链）。
    src/Renderer/blockcube.cpp
    src/Renderer/blockcube.h
    # t860 cutout 段折叠探针：World 层 ChunkGeometry 直调（terrain 段 mesh 计入 cross 顶点的行为级断言
    #   —— 折叠 / 回退各留一枚探针红绿可辨）。依赖 partialblockgeometry（已列）+ world（已列）。
    src/World/chunkgeometry.cpp
    src/World/chunkgeometry.h
    # t858 经验球 instancing 探针：Game 层 XpOrbInstancing feeder 直调（实例表内容断言：位置派生 /
    #   活体过滤）。依赖 xporbmanager（已列）；Quick3D 公开 API。
    src/Game/xporbinstancing.cpp
    src/Game/xporbinstancing.h
    # t1027 掉落物 instancing 首批探针：Game 层 BlockDropInstancing feeder 直调（族分桶正确性 /
    #   实例数=活体数 / 拾取-合并-despawn 零扰动）。依赖 itementitymanager（已列）+ blockregistry（已列）。
    src/Game/blockdropinstancing.cpp
    src/Game/blockdropinstancing.h
    # t1039 掉落物 instancing 批 3 探针：GlowShellInstancing feeder 直调（壳族收纳/实例数/颜色实例表/
    #   呼吸 alpha/空转门/kShellCap 溢出降级）。依赖 itementitymanager（已列）；Quick3D 公开 API。
    src/Game/glowshellinstancing.cpp
    src/Game/glowshellinstancing.h
    # t1041 掉落物 instancing 批 4 收官探针：ToolDropInstancing（工具 3D 族 tier 色 per-instance color /
    #   弓弦 stringPass 双表/空转门）+ BillboardDropInstancing（异形方块图标族/工具材料图标族双池谓词/
    #   朝相机旋转进实例表/空转门）。依赖 itementitymanager（已列）+ toolregistry（已列）。
    src/Game/tooldropinstancing.cpp
    src/Game/tooldropinstancing.h
    src/Game/billboarddropinstancing.cpp
    src/Game/billboarddropinstancing.h
    # t946 坐姿变换链连续域探针附加：Renderer 层 MobModel 直编（真几何顶点射线剪影断言——t880
    #   ItemShapeGeometry 行为级先例；review24 时「只用头内 constexpr、不链 mobmodel.cpp」的旧注释口径
    #   自本探针起放开为「头 constexpr 静态断言 + 行为级直编并存」）。
    src/Renderer/mobmodel.cpp
    src/Renderer/mobmodel.h
    # t966 拖拽方向 rig 探针附加：真 QQmlEngine 直载源树 ResourceBrowser.qml（预览变换图行为级断言），
    #   组件实例化需浏览器用到的全部 Renderer 几何类型注册（BlockCube/ItemShapeGeometry/MobModel 已列，
    #   下列四个补齐：UnitCube 羊眼/腿/傀儡头 overlay、BedModelGeometry 床分支、EnchantBookBox 附魔台书、
    #   MobBowGeometry 骷髅持弓——均 QQuick3DGeometry 公开 API，Quick3D 已链）。
    src/Renderer/unitcube.cpp
    src/Renderer/unitcube.h
    src/Renderer/bedmodelgeometry.cpp
    src/Renderer/bedmodelgeometry.h
    src/Renderer/enchantbookbox.cpp
    src/Renderer/enchantbookbox.h
    src/Renderer/mobbow.cpp
    src/Renderer/mobbow.h
)
target_include_directories(redstone_matrix_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Game
    ${CMAKE_CURRENT_SOURCE_DIR}/src/World
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Entities
    # review24 低危收尾（#35）：Renderer 头（mobmodel.h）——探针持「白名单长度 ↔ MobType 上界」跨层
    #   static_assert（测试 TU 合法 include 全栈；Renderer 自身在 Entities 之下不得反向 include）。
    #   t946 起另链 mobmodel.cpp 做坐姿真几何行为级探针（Quick3D 链接已具备）。
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer
    # t813 生成的 build_stamps.h 搜索路径（buildinfo.cpp 消费；探针断言两宏格式）。
    ${VOXEL_GENERATED_DIR}
)
add_dependencies(redstone_matrix_test build_stamps)
target_link_libraries(redstone_matrix_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    # t814 真消费端探针：PlayerController 是 QQuickItem 派生（Qt Quick）→ 链 Quick。
    Qt6::Quick
    # review24 低危收尾（#35）：mobmodel.h（QQuick3DGeometry 派生）的 include 路径经 Quick3D target
    #   传播——跨层 static_assert 用头内 constexpr；t946 起行为级坐姿探针直编 mobmodel.cpp（公开 API）。
    Qt6::Quick3D
    # t822 存档 round-trip 探针：WorldStore 的 SQLite 落盘读回。
    Qt6::Sql
)
