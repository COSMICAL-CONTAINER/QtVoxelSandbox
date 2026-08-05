import QtQuick
import QtQuick3D
// t41：QML 源文件迁入 src/ui/ 子目录后，不再位于模块根 → 丢失对模块 C++ 类型
// （World / Hotbar / PlayerController / ChunkGeometry / CrackBox …）的隐式访问。
// 显式 import 自身模块以恢复类型解析（Qt6 子目录 QML 文件的标准做法）。行为不变。
import VoxelSandbox

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — 主菜单 · 创造沙盒"
    color: "#101010"

    property int fps: 0 // main.cpp 经 frameSwapped 回填
    // F3 调试叠层显隐（t10，PLAN §2-F）：playing 态按 F3 切换左上角多行调试文本（fps / pos /
    // yaw,pitch / 模式 / chunk 数 / 总顶点 / 地面态）。仅 playing 态显；z 高于 HUD。
    property bool f3Visible: false
    // t143 F3+B 修饰键语义：f3Held 跟踪 F3 键物理按下态（pressed=true / released=false），无条件更新
    //   （与 shiftHeld 同模式——状态跟踪不受 appState 守卫影响）。B 键切换碰撞箱的条件用 f3Held 而非
    //   f3Visible，对齐 MC 真实行为：F3+B 是「按住 F3 同时按 B」的组合键（F3 作 B 的修饰键），而非
    //   「先开 F3 叠层再单独按 B」。差异场景：按 F3 开叠层后松开 F3（f3Held=false 但 f3Visible 仍 true），
    //   此时单独按 B 不触发 hitbox 切换（F3 未按住）。
    property bool f3Held: false
    // t116 F3+B 碰撞箱（PLAN §2-F F3 调试叠层扩展）：显隐各实体（mob / 掉落物 / 玩家）的
    //   WireCube AABB + 朝向箭头。MC F3+B 语义——B 键仅在 F3 按住时生效（见 f3Held，t143）；
    //   showHitboxes 一旦开启，松开 F3 不影响（独立 bool，同 MC：关 F3 后碰撞箱仍显直到再 F3+B 关）。
    property bool showHitboxes: false
    // t277 F3+G 区块边界显示（机制等价 MC F3+G chunk boundary display）：显隐 16×16 区块边界线框叠层
    //   （ChunkGridLines 纵线 + 顶/底水平连线，红色 NoLighting 线）。MC F3+G 语义——G 键仅在 F3 按住
    //   （f3Held）时 toggle showChunkBounds，即「按住 F3 同时按 G」的组合键（F3 作 G 修饰键，与上方
    //   F3+B 同模式）。showChunkBounds 独立于 f3Held/f3Visible：松开 F3 后边界线仍显直到再按 F3+G 关
    //   （同 MC 行为，与 showHitboxes 一致）。仅 playing 态有意义（叠层在 View3D 场景内）。
    property bool showChunkBounds: false

    // app 状态机（t17 / t176）：menu（启动首显主菜单）→ worldlist（单人模式：世界列表 / 新建）→
    //   playing（显 View3D/HUD + grab 指针）。playing 经 ESC「保存并退出」回 worldlist；worldlist「返回」回 menu。
    //   准星/HUD 仅 playing 态显。初始 menu：启动不直接进游戏。
    property string appState: "menu"

    // 背包子态（t18）：仅 playing 态有意义。开 → 释放指针（光标可见，可点格子，类暂停）；
    // 关 → 恢复 grab。Esc/E/点遮罩均可关。开时抑制暂停叠层（二者互斥：都是 !captured 态）。
    property bool inventoryOpen: false
    // t50 工作台子态：右键工作台方块 → player.craftingTableOpened → 显本面板（3×3 合成）+ 释放指针。
    // 与 inventoryOpen 互斥（关一个再开另一个）；E/Esc 关 → 恢复 grab。开时抑制暂停叠层。
    property bool craftingTableOpen: false
    // t87 熔炉子态：右键熔炉方块 → player.furnaceOpened → 显本面板（冶炼）+ 释放指针。与 inventoryOpen
    // / craftingTableOpen 互斥（关一个再开另一个）；E/Esc 关 → 恢复 grab。开时抑制暂停叠层。
    property bool furnaceOpen: false
    // t173/t179 箱子子态：右键箱子方块 → player.chestOpened(x,y,z) → 显 ChestUI（箱子 27 槽 + 玩家主栏 +
    //   hotbar）+ 释放指针。与 inventoryOpen / craftingTableOpen / furnaceOpen 互斥；E/Esc 关 → 恢复 grab。
    //   chestX/Y/Z 记当前所开箱子的方块世界坐标（ChestStore 据此寻址该箱子的 27 槽；切箱子时坐标变）。
    property bool chestOpen: false
    property int chestX: 0
    property int chestY: 0
    property int chestZ: 0
    // t225 当前所开箱子的「前面（锁面）」朝向（0=+X 1=-X 2=+Z 3=-Z；与 BlockRegistry::chestFrontFace /
    //   horizontalFacing 同源编码 = 前面所朝方向）。openChest 读 theWorld.stateAt(x,y,z) 设置；驱动盖子
    //   铰链侧（chestLidYaw）→ 放置时锁面朝玩家，开盖铰链在锁面背侧。默认 3（-Z，对齐旧默认 / 兜底）。
    //   纯呈现层态（只读 World stateAt，不写栅格，PLAN §2 分层）。
    property int chestFacing: 3
    // t196 箱子盖子开合动画态：chestLidAngle = 盖子绕后铰链的翻开角度（0=合，kChestLidOpenAngle=全开）。
    //   openChest → 设全开角（下方 Behavior 平滑翻开，~240ms）；closeChest → 设 0（合回）。盖子可见性由
    //   chestLidPivot.visible = chestOpen || angle>0 推导（合盖动画播放期间仍可见，角到位 0 后自动隐）。
    //   仅一处盖子（一次只开一只箱子，chestOpen 单 bool），坐标读 chestX/Y/Z。同步 ChestUI 显隐
    //   （二者同源于 openChest/closeChest）。纯呈现层态，不写栅格（PLAN §2 分层）。
    property real chestLidAngle: 0
    // 盖子全开角（度）：略过 90° 让盖子后仰（机制等价 MC 箱子开盖姿态）；铰链子节点绕局部 X 旋让前缘上扬。
    readonly property real kChestLidOpenAngle: 105
    // t225 盖子铰链 yaw（据 chestFacing 把「局部 +Z = 背侧（铰链所在）」转到箱子实际背侧方向）。
    //   前面 = 玩家侧（放置时锁面朝玩家）；背侧 = 前面反向 = 铰链侧。local +Z 经 Y 旋转 → 世界背侧：
    //   前面 +X(0)→背 -X→yaw -90 / 前面 -X(1)→背 +X→yaw 90 / 前面 +Z(2)→背 -Z→yaw 180 / 前面 -Z(3)→背 +Z→yaw 0。
    readonly property real chestLidYaw: {
        switch (window.chestFacing) {
        case 0: return -90
        case 1: return 90
        case 2: return 180
        default: return 0
        }
    }
    // chestLidAngle 的平滑过渡（开 / 合双向同缓动；OutCubic 开盖先快后慢、合盖自然）。 redirects 自动
    //   （开盖中按关 → Behavior 接力到 0，无需手动 stop）。
    Behavior on chestLidAngle {
        NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
    }
    // t139 ESC 设置菜单子态：仅在暂停叠层（playing 且 !captured）下有意义。暂停叠层「设置」按钮置
    //   true → 显设置面板（手臂调试 ArmSlider 等）覆盖在暂停叠层之上；「返回」按钮置 false 回暂停菜单。
    //   回主菜单 / 点击恢复游戏时一并复位。属纯呈现态，PLAN §2 分层（UI 层）。
    property bool settingsOpen: false
    // t312 聊天栏子态（PLAN §2 UI 层，纯呈现）：T/Enter 开 / Esc/Enter 关。开 → release 指针（光标可见
    //   供打字 + TextField 取焦点，同背包面板模式）；关 → 恢复 grab + 焦点回键位层。开时抑制暂停叠层
    //   （二者互斥：都是 !captured 态）+ 抑制滚轮切 hotbar（防打字时误切槽）。与背包/工作台/熔炉/箱子
    //   子态互斥（开其一前关其它）。死亡态不开（playerState.dead 已接管 !captured 态，死亡信息走死亡屏）。
    property bool chatOpen: false
    // t312 玩家显示名（聊天「名: 文本」的名；机制等价 MC 玩家名，§9 通用词非专名）。单机 Phase 1.0 无账号名
    //   系统 → 用「玩家」作通用默认；Phase 3 联机接入时改为联机昵称（LocalServer/RemoteServer）。纯呈现态，
    //   不进存档（存档只有 worldName，非玩家名；worldName 是世界标识 ≠ 玩家身份）。
    readonly property string playerName: "玩家"

    // t110 Shift/数字键守卫所需 window 级态：
    //   - shiftHeld：Shift 按下态（keyInput Keys.onPressed/Released 始终追踪，**不论背包是否开**）。
    //     各背包面板的槽 TapHandler 读此属性 → 区分普通左键 vs Shift+左键搬运。不放进各面板是因为 Shift
    //     按下/松开是窗口级键盘事件（keyInput 持焦点），面板自身不接收 Keys。
    //   - hoveredSlotKey：当前指针所在槽的「组:下标」字符串（如 "main:5" / "hotbar:0" / "craft:2"）。
    //     由当前可见的背包面板的 hoveredKey 提升而来（keyInput 数字键交换需 window 级读，见 spec t110
    //     「需 window.hoveredSlotKey 提升到 window 级」）。面板隐藏 / 无背包开 → ""。
    property bool shiftHeld: false
    property string hoveredSlotKey: {
        if (window.appState !== "playing") return ""
        // 据当前可见面板绑定（hoveredKey 由各面板 HoverHandler 维护；面板 visible=false 时其 HoverHandler
        // 不触发，但显式判 visible 更稳，避免读隐藏面板的陈旧 hoveredKey）。
        if (craftingTablePanel.visible) return craftingTablePanel.hoveredKey
        if (furnacePanel.visible)        return furnacePanel.hoveredKey
        if (chestPanel.visible)          return chestPanel.hoveredKey
        if (inventoryPanel.visible)      return inventoryPanel.hoveredKey
        if (survivalPanel.visible)       return survivalPanel.hoveredKey
        return ""
    }

    // 第一人称手臂 viewmodel 的角度 / 位置 window 级中转属性（t129 引入作临时调试；t156 固化为用户终值）。
    //   t139 起 ESC 暂停叠层「设置」面板的 ArmSlider 写这些属性 → viewModelHand（下方 PerspectiveCamera
    //   子节点）绑定读取 → 手臂 baseTilt / position 实时变。t156 用户经滑动条调定终值并写死为此默认：
    //   baseTilt -34.56°、position (0.36, -0.12, -0.39)。
    //   ⚠️ position.z=-0.39 已 < -0.3，贴方块时手会伸进前方实体方块（穿模，见 viewModelHand 注释 t52/t73）
    //      —— 此为用户主动选定（贴近方块的手感优先于 t73 的不穿模几何），有意保留。ArmSlider 仍在设置面板
    //      可继续微调（未移除），故保留 window 级中转而非内联进 viewModelHand。
    property real handBaseTilt: -34.56
    property real handPosX: 0.36
    property real handPosY: -0.12
    property real handPosZ: -0.39

    // t166 暗度参数（用户「黑的地方稍太黑」）：terrainLight / tintBySkyLight 的 floor（夜间 / 阴暗处场景
    //   最低亮度乘子）。默认 0.4（原固定值）；ESC 设置面板滑条实时调（0.20..0.60）→ 越大夜间 / 阴暗越亮。
    //   调高 = 整体暗部变亮（兼顾可辨识），调低 = 夜更黑（氛围）。洞穴 / 阴影顶点色地板另由 C++ kVcMin（0.08）托底。
    property real minLight: 0.4

    // t166b 阴影开关（用户「卡顿疑似阴影所致，加开关测」）：false → 全 chunk sunShadowAt 返 0（关 PCF 软影，
    //   meshing 提速；顶点光基底只剩 flood-fill 光场）。ESC 设置面板开关绑此。默认 true。
    property bool shadowsEnabled: true
    // t178/t183 贪婪网格化开关（PLAN §4 性能打磨）：true → chunk mesher 合并同 (tile,光) 共面为单个矩形
    //   （顶点/三角大幅下降，F3 可观测）；false → 回退逐格 culled（贴图逐格清晰）。绑各 ChunkGeometry。
    //   ⚠️ 图集（CLAMP 采样）路径下合并 quad 的贴图必然**拉伸**铺满（UV 超 [u0,u1] 采到相邻瓦片，无法 REPEAT
    //   逐格平铺；细分成 per-block 子格又与 culled 输出一致、无顶点收益）→ t183 默认 false 恢复逐格清晰贴图。
    //   逐格清晰 + 顶点预算兼得需纹理数组（自研 RHI，dev-plan 偏差 1/2）。ESC 设置面板仍可手动开 greedy 对比。
    property bool greedyMeshing: false
    // t223 水贴图动画 phase（flipbook 帧索引 0/1）：仅水段 ChunkGeometry 绑定。下方 waterAnimTimer 每
    //   ~800ms 切 0↔1（spec「静止水 2 帧慢播，勿快」）→ 水段在 {19,24}(静水)/{23,25}(流水) 间换帧 →
    //   静水荡漾 / 流水斜纹流动动势。仅 playing 态 tick（菜单态水段已离场，无需动）。
    property int waterAnimPhase: 0
    // t166c 第一人称手持方块位置（用户「加滑动条调手持方块位置」）：viewModelHand 内 BlockCube 的相对偏移。
    //   默认 (0,0,0)（t156「手前方」基线）；ESC 滑条实时调。
    property real heldBlockX: 0.0
    property real heldBlockY: 0.13
    property real heldBlockZ: 0.22

    // t276 大世界网格尺寸（PLAN §4 放大阶段 / R18g）：世界 = worldChunksPerSide×worldChunksPerSide 个
    //   chunk 列（每 chunk 16×16），即边长 = worldChunksPerSide*16 方块。默认 10 → 10×10=100 chunk / 160×160。
    //   单一权威：theWorld.width/depth 由它派生（下方 World 绑定），chunkAnchor.onCompleted 据 theWorld.chunksX/Z
    //   动态生成对应数量的 chunk Model，F3 顶点统计自动覆盖全幅 → 改此一处即整体扩 / 缩（流式加载推迟 Phase 2，
    //   本工程仍为「整片固定网格」全驻留）。worldgen 与 ChunkManager 全程维度无关（按 m_width/m_depth 迭代），
    //   故扩容只改尺寸 + chunk Model 数量，不动 worldgen 逻辑。改值需同步 playercontroller kSpawnX/Z（居中）。
    property int worldChunksPerSide: 10
    // t276 动态 chunk Model 跟踪态：chunkAnchor.onCompleted 用 Component.createObject 按 chunksX×chunksZ
    //   生成地形 + 水两段 Model（createObject + reparent 进 3D 场景 Node，t16/t170 已验证路径）。固定网格 → 仅一次
    //   （chunksBuilt 守卫）。terrainGeos 持地形段 ChunkGeometry 引用；每个地形段经 Connections(onMeshRebuilt)
    //   调 recomputeMeshStats 把全幅顶点 / 三角面汇总写入 meshVertices/meshTriangles 标量属性 —— F3 叠层**只读
    //   标量**（不把 var 数组进 text 绑定，否则 QML 把 var 属性读判为 binding loop）。chunkObjects 持 Model 引用防 GC。
    property var terrainGeos: []
    property var chunkObjects: []
    property bool chunksBuilt: false
    property int meshVertices: 0     // 全幅地形段顶点汇总（recomputeMeshStats 写；F3 只读）
    property int meshTriangles: 0    // 全幅地形段三角面汇总（recomputeMeshStats 写；F3 只读）
    // 全幅顶点 / 三角面汇总（遍历 terrainGeos 求和 → 写标量属性）。由每个地形段 ChunkGeometry 的 meshRebuilt
    //   信号经 Connections 触发（buildChunkModels 末也调一次取初值）。在普通函数里读 var 数组不创建绑定依赖，
    //   故不触发 binding loop（与在 text 绑定里读 var 数组相反）。
    function recomputeMeshStats() {
        const geos = window.terrainGeos
        let v = 0, t = 0
        for (let i = 0; i < geos.length; ++i) {
            v += geos[i].vertexCount
            t += geos[i].triangleCount
        }
        window.meshVertices = v
        window.meshTriangles = t
    }

    // 进入游戏：切 playing 态 + 锁定指针（隐藏光标）+ 焦点回键位层。
    function startGame() {
        appState = "playing"
        player.grab()
        keyInput.forceActiveFocus()
        audio.startAmbient()   // t177 环境音：进游戏启风声床（幂等；menu/worldlist 态已 stop）
    }
    // t176 进入指定世界（从世界列表点「进入 / 创建并进入」调）：打开存档 → 按是否有 chunk blob 分流
    //   （有 → 加载存档地形；无 → 新世界 worldgen）→ 加载玩家态（无 → 默认出生）→ 清实体残留 → 进游戏。
    //  机制等价 MC：选世界进游戏时若曾保存则恢复地形 + 玩家位姿，否则按 seed 新生。
    function enterWorld(file, name) {
        currentWorldFile = file
        currentWorldName = name
        if (!worldStore.openWorld(file)) {
            console.warn("[t176] openWorld failed:", file)
            return
        }
        const meta = worldStore.loadMeta()
        let seed = parseInt(meta.seed, 10)
        if (isNaN(seed)) seed = 42
        if (worldStore.hasChunks()) {
            // 已保存地形 → 加载存档（玩家编辑过的地形恢复，而非 worldgen 重生）
            theWorld.beginLoad(seed)
            worldStore.loadChunks()
            theWorld.finishLoad()
        } else {
            // 新世界（仅 meta 无 chunk blob）→ 按 seed 全量 worldgen（recreate 网格 + 地形 + 光场）
            theWorld.regenerate(seed)
        }
        applyPlayerState(worldStore.loadPlayerData())
        // t188 箱子按世界持久化 + 修跨世界泄漏：chestStore 跨世界长驻（同 hotbarVM），进世界前 loadAll
        //   整体替换内存（先清后填）—— 无存档 chests 表 → 空列表 → 清空，杜绝上一世界箱子残留串入新世界。
        //   存档 chests 由 saveAndExitToWorldList 经 saveAll(name, chestStore.allChests()) 落盘。
        chestStore.loadAll(worldStore.loadChests())
        // 清上一世界的掉落物 / mob 残留（实体非体素，不进存档，切世界必清）
        itemEntities.clearAll()
        entityManager.clearAll()
        // t312：清聊天历史（不持久化 / 不跨世界；新世界从空起）。
        chatMessages.clear()
        // t240 进世界生成猪 / 牛 / 羊各一只于出生点附近地表（ EntityManager 已注册 3 类 mobType 1/2/3；
        //   生物蛋生成系统推迟到 t243，故本任务暂以固定 spawn 验证模型 + 贴图可见）。坐标取出生列 (40,40)
        //   附近三格、Y = worldgen 地表 +1（落地上方一格 → 重力 tick 贴地表不摔伤）。§9 区隔：模型 / 贴图
        //   原创方块化（不照搬 MC），机制对齐 MC 1.0 passive mob（猪 / 牛 / 羊三种）。spawnMobTyped 第五参
        //   color 仅 mobType 0（测试生物）单色路径读，pig/cow/sheep 走 MobModel + 贴图 → 传占位串即可。
        spawnInitialMobs()
        appState = "playing"
        player.grab()
        keyInput.forceActiveFocus()
        audio.startAmbient()   // t177 环境音：进世界启风声床
    }
    // t176 把存档玩家态（QVariantMap）应用到 player / playerState / hotbarVM。空 map（新世界）→ 默认出生态。
    //   t187：背包清空必须放在「空 data 早 return」之前两路径共用 —— 旧版空分支漏清 hotbarVM，导致上一世界
    //   物品残留内存 VM、带进同种子新世界（串世界根因）。hotbarVM 是跨世界长驻的内存对象，存档层按世界
    //   （每 .sqlite 独立 player_state 表）隔离无 bug，但切世界时 VM 不重置就会泄漏到新世界。
    function applyPlayerState(data) {
        // 背包清空（两路径共用）：hotbarVM 跨世界长驻，进任何世界前都必须先清，再按存档写或留空。
        for (let i = 0; i < 9; ++i) hotbarVM.setStack(i, 0, 0)
        for (let j = 0; j < 27; ++j) hotbarVM.mainSetStack(j, 0, 0)
        hotbarVM.heldBlock = 0
        if (!data || Object.keys(data).length === 0) {
            // 新世界：出生点 + 满血满饥 + 默认模式（player 构造默认 Spectator）+ 背包清空（上文已清）
            player.respawn()
            playerState.respawn()
            return
        }
        // 显式 undefined 检查：JS 的 `||` 对 falsy 值（0/NaN）会误兜底 —— 存档里 px/pz=0（世界边沿）或
        // pitch=0（水平视角）本是合法值，用 `||` 会被回退成默认值，破坏 round-trip 保真。`!== undefined ?`
        // 把「字段缺省」与「字段值为 0」严格分开（gatherPlayerState 总写齐 6 字段，缺省仅旧存档场景）。
        const DF = { px: 40, py: 44, pz: 40, yaw: 0, pitch: -42, mode: 0 }
        player.loadSavedState(
            data.px    !== undefined ? data.px    : DF.px,
            data.py    !== undefined ? data.py    : DF.py,
            data.pz    !== undefined ? data.pz    : DF.pz,
            data.yaw   !== undefined ? data.yaw   : DF.yaw,
            data.pitch !== undefined ? data.pitch : DF.pitch,
            data.mode  !== undefined ? data.mode  : DF.mode)
        playerState.setHealth(data.health !== undefined ? data.health : 20)
        playerState.setHunger(data.hunger !== undefined ? data.hunger : 20)
        // t238 同步 Physics 层饥饿镜像（存档持久化 playerState.hunger；此处把同一值灌回 PlayerController.m_hunger，
        //   使两层一致、depletion 从存档值起算）。player.setHunger 内部 emit hungerUpdated → 上面 onHungerUpdated
        //   路由回 playerState.setHunger（幂等：值已一致则无变化静默）。
        player.setHunger(data.hunger !== undefined ? data.hunger : 20)
        // 背包已在上文两路径共用处清空，此处直接按存档写
        if (data.hotbar) for (let i = 0; i < data.hotbar.length && i < 9; ++i)
            hotbarVM.setStack(i, data.hotbar[i].id, data.hotbar[i].count)
        if (data.main) for (let i = 0; i < data.main.length && i < 27; ++i)
            hotbarVM.mainSetStack(i, data.main[i].id, data.main[i].count)
        // 同上：`||` 对 0（第 0 槽）会误兜底，恰好 0==默认值巧合正确，但显式检查更稳健且与上面一致。
        hotbarVM.selectedSlot = data.selectedSlot !== undefined ? data.selectedSlot : 0
    }
    // t176 收集当前玩家态为 QVariantMap（存档用）：位姿 / 模式 / 血饥 / hotbar 9 + main 27 背包 / 选中槽。
    function gatherPlayerState() {
        const hotbar = []
        for (let i = 0; i < 9; ++i) hotbar.push({ id: hotbarVM.blockIdAt(i), count: hotbarVM.countAt(i) })
        const main = []
        for (let i = 0; i < 27; ++i) main.push({ id: hotbarVM.mainBlockIdAt(i), count: hotbarVM.mainCountAt(i) })
        return {
            version: 1,
            px: player.feetPosition.x, py: player.feetPosition.y, pz: player.feetPosition.z,
            yaw: player.yaw, pitch: player.pitch,
            mode: player.mode,
            health: playerState.health, hunger: playerState.hunger,
            selectedSlot: hotbarVM.selectedSlot,
            hotbar: hotbar, main: main
        }
    }
    // t176 保存并退出到世界列表（ESC 暂停叠层「保存并退出」按钮）：归还手持物 → 存玩家态 + 存地形 +
    //   截封面 → 关库 → 清实体 → 切 worldlist 态。spec「退出存」：每次退出都把当前进度落盘。
    //   t232 封面黑屏修复：旧用 view3d.grabToImage() → 全黑（grabToImage 只经 2D 场景图重渲，拍不到 View3D
    //   的 3D 渲染 pass）。改 ScreenGrab.grab(window) → QQuickWindow::grabWindow（含 3D pass）→ 真拍到场景。
    //   抓帧前 coverHideUi=true 把 View3D 抬到最上层盖住暂停叠层 / HUD → 封面为纯 3D 场景。ScreenGrab 等下一帧
    //   渲染完（盖住后）再抓 → onGrabbed 收尾（saveCover + finishExit）。兜底定时器防 frameSwapped 不发卡退出。
    function saveAndExitToWorldList() {
        if (coverGrabPending) return   // 防连点退出按钮重复触发（已有一次退出在进行）
        returnHeldToHotbar()
        const file = currentWorldFile
        const hasOpen = worldStore.isOpen()
        if (hasOpen) {
            worldStore.savePlayerData(gatherPlayerState())
            // t188：箱子内容随地形 / meta 同事务落盘（saveAll 第 2 参 = ChestStore::allChests() 产物）。
            worldStore.saveAll(currentWorldName, chestStore.allChests())
        }
        coverGrabPending = true   // 标记退出进行中（防 onGrabbed + 兜底定时器双调 finish）
        // 截封面：仅在 playing（View3D 抓得到画面）+ 有世界文件名（saveCover 据此写 sidecar PNG）时抓。
        if (hasOpen && file.length > 0 && view3d.visible) {
            window.coverHideUi = true       // 抬 View3D 盖住 UI（下一帧渲染生效），拍到无 UI 的纯场景
            coverExitFallback.restart()     // 兜底：frameSwapped 不发 → 500ms 直接 finish（不卡退出）
            screenGrab.grab(window)         // 等下一帧 frameSwapped → grabWindow → onGrabbed 收尾
            return                          // 收尾交 onGrabbed / 兜底定时器
        }
        window.finishExitToWorldList()
    }
    // t232 ScreenGrab.grab 完成回调：写封面 PNG + 收尾退出。image 为 QVariant<QImage>（空图 → saveCover
    //   内 isNull 降级为「无封面」灰块，不阻塞退出，§2-E）。与 coverExitFallback 兜底两路汇集，coverGrabPending
    //   守门防重复 finish。
    function onCoverGrabbed(image) {
        coverExitFallback.stop()
        window.coverHideUi = false          // 复位（马上切 worldlist 态离场，复位保干净 / 防残留）
        if (worldStore.isOpen())
            worldStore.saveCover(currentWorldFile, image)   // null image → saveCover 内降级 qWarning
        window.finishExitToWorldList()
    }
    // t191 saveAndExitToWorldList 的收尾段（closeWorld + 清实体 + 切态）。从 ready 回调 / 兜底定时器 / 抓帧跳过
    //   三路径汇集，coverGrabPending 守门防重复执行。
    function finishExitToWorldList() {
        if (!coverGrabPending) return
        coverGrabPending = false
        coverExitFallback.stop()
        window.coverHideUi = false   // t232：复位 View3D z（防兜底路径漏复位 → 再进世界 View3D 仍盖住 HUD）
        if (worldStore.isOpen()) worldStore.closeWorld()
        inventoryOpen = false
        craftingTableOpen = false
        furnaceOpen = false
        chestOpen = false
        chestLidAngle = 0    // t196：复位盖子角（防 worldlist→再进世界时残留半开盖子；scene 已离场，动画不可见）
        settingsOpen = false
        itemEntities.clearAll()
        entityManager.clearAll()
        player.release()
        appState = "worldlist"
        audio.stopAmbient()   // t177 环境音：退出世界停风声床（菜单态无声）
        audio.stopWaterFlow() // t223 水流声：退出世界停（菜单态无声；离开流水范围本会自停，此处显式保干净）
    }
    // 返回主菜单：先释放指针（恢复光标 + 清按住的按键），关存档连接 + 清实体，再切 menu 态。
    function returnToMenu() {
        inventoryOpen = false
        craftingTableOpen = false
        furnaceOpen = false
        chestOpen = false
        chatOpen = false                  // t312：回菜单关聊天（防遗留；非死亡流）
        chestLidAngle = 0    // t196：复位盖子角（防回菜单 / 再进世界残留半开盖子）
        settingsOpen = false           // t139：回菜单时关设置面板（防遗留）
        // t312：清聊天历史（不持久化 / 不跨世界；下一局从空起）。
        chatMessages.clear()
        returnHeldToHotbar()           // t56：返回菜单前归还光标手持栈（防遗留 heldBlock）
        worldStore.closeWorld()        // t176：回主菜单关存档连接（防残留打开库）
        itemEntities.clearAll()        // t176：清实体残留
        entityManager.clearAll()
        player.release()
        appState = "menu"
        audio.stopAmbient()   // t177 环境音：回主菜单停风声床
        audio.stopWaterFlow() // t223 水流声：回主菜单停（菜单态无声）
    }
    // t240 进世界生成猪 / 牛 / 羊各一只（出生点附近地表）。EntityManager 已注册 mobType 1/2/3 + spawnMobTyped
    //   入口；生物蛋系统推迟到 t243，故本任务暂以固定 spawn 让模型 + 贴图肉眼可见。坐标取出生列
    //   （kSpawnX/Z，t276 大世界居中=80,80）附近三格、Y = theWorld.heightAt(x,z) + 1（worldgen 地表上方一格 →
    //   重力 tick 贴地表，出生落差 0 不摔伤）。
    //   §9 区隔：三种 mob 模型 / 贴图全原创方块化（不照搬 MC）；机制对齐 MC 1.0 passive mob 三种。
    //   spawnMobTyped 第五参 color 仅 mobType 0（通用测试生物）单色路径读；pig/cow/sheep 走 MobModel + 贴图，
    //   传占位串。maxHealth=10（MC 1.0 passive mob 5 心）。
    function spawnInitialMobs() {
        // t276：出生点跟随大世界居中（kSpawnX/Z=80）。三 mob 散布在其左 / 前 / 右各两格。
        // 猪（mobType 1）— 出生点左侧两格。
        let h = theWorld.heightAt(76, 78)
        if (h > 0) entityManager.spawnMobTyped(76, h + 1, 78, 1, "#f0a8b0", 10)
        // 牛（mobType 2）— 出生点前方两格。
        h = theWorld.heightAt(80, 76)
        if (h > 0) entityManager.spawnMobTyped(80, h + 1, 76, 2, "#5a4030", 10)
        // 羊（mobType 3）— 出生点右侧两格。
        h = theWorld.heightAt(84, 78)
        if (h > 0) entityManager.spawnMobTyped(84, h + 1, 78, 3, "#f5f0e8", 10)
    }
    // t78 立即重生（死亡界面按钮）：满血 + 清死亡态 + 传回出生点 + 清挖掘/飞行态 + 重新锁定指针回游戏。
    //   PlayerState.respawn 复位血量/死亡态；PlayerController.respawn 传回出生点 + 清物理态；
    //   重新 grab（死亡时已 release 让光标点按钮）→ 回到 captured 游戏态。
    function respawnPlayer() {
        playerState.respawn()   // 清 dead（visible 绑自动隐死亡界面）+ 满血满饥
        player.respawn()        // 传回出生点 + 清速度/挖掘/飞行/蹲下疾跑
        player.grab()
        keyInput.forceActiveFocus()
    }
    // t78 死亡界面 → 回主菜单：清死亡态（dismiss 死亡遮罩 + 满血，下次 Start Game 干净）+ 走标准 returnToMenu。
    //   不复用 returnToMenu 内的复位：returnToMenu 也被暂停叠层「Main Menu」按钮用（非死亡流），保持其语义单一。
    // t175：调 player.respawn() 把玩家复位到出生点 + 清 m_dead 镜像 → 下次 Start Game 从干净态起（否则
    //   m_dead 残留 = pickupScan 永久关闭，且玩家仍停死亡点）。returnToMenu 内不含定位复位，故此处补。
    function deathReturnToMenu() {
        playerState.respawn()   // 清 dead（visible 绑自动隐）+ 满血（防死亡态遗留到下次进游戏）
        player.respawn()        // t175：复位到出生点 + 清 m_dead 镜像（下次进游戏干净态）
        window.returnToMenu()
    }
    // t56：把光标手持栈（heldBlock/heldCount）归还进背包。关背包 / 工作台 / 返回菜单时调。
    //   根因：旧版关包只 returnCraftToHotbar（合成格），**不归还 heldBlock** → 用户从调色板拾取到光标后
    //   关包，heldBlock 残留为隐形孤儿（浮动图标仅背包开时显）。此后按 Q → dropHeld 读选中槽（空）→
    //   早退无丢弃（spec 现象「手持物按 Q 看不到丢弃 + 打开背包还在手上」：手上=heldBlock 残留）。
    //   修法：关包时把 heldBlock 全部塞回背包，再清空 heldBlock。背包满（极端）则把余量丢弃为前方实体
    //   （dropHeldCursor，同拖出丢弃；不静默吞，§2-E）。
    // t97：原走 addStack（只看 hotbar 9 槽），主栏 VM 后改走 addToAny（跨 main + hotbar：先合并 main 同 id →
    //   再 hotbar 同 id → 再空槽 main→hotbar）→ 关包归还的物品能合并进主栏同 id（修「丢弃回栏不合并」根因）。
    function returnHeldToHotbar() {
        if (!hotbarVM.heldBlock || hotbarVM.heldCount <= 0) return
        // t263 归还手持工具时保真耐久（addToAny 第 3 参 dur；非工具 dur=0 inert）。
        const leftover = hotbarVM.addToAny(hotbarVM.heldBlock, hotbarVM.heldCount, hotbarVM.heldDurability)
        if (leftover > 0) {
            // 背包满：余量丢弃为实体（先把 heldCount 收到余量再 dropHeldCursor，它清 heldBlock + 发 spawnItem）。
            // 注：heldBlock/heldCount 是 Q_PROPERTY（WRITE setter），不能当函数调（.setHeldBlock(0) 会 TypeError），
            //   经赋值触发 setter。
            hotbarVM.heldCount = leftover
            player.dropHeldCursor()
        } else {
            hotbarVM.heldBlock = 0 // 全入 → 清光标手持栈（setHeldBlock(0) 同步清 count）
        }
    }
    // t110 数字键交换（spec「数字键 1-9：背包开 + hoveredKey → 与 hotbar[idx] 交换」）：把当前指针 hover 的
    //   槽内容与 hotbar[idx] 整栈互换（MC 1.0 数字键快捷栏交换）。逻辑分发到当前可见的背包面板 —— 各面板持
    //   readSlot/writeSlot 知道如何读 / 写其本地槽组（craft / in / fuel / out）+ 经 VM 的 main/hotbar；故把
    //   「源 / 目标读写」下放到面板、Main.qml 只做路由（不在 window 层复制读写逻辑，避免漏 craft / 熔炉 3 槽
    //   等面板本地存储）。无面板开 → 无操作。
    function swapHoveredWithHotbar(hotbarIdx) {
        if (craftingTablePanel.visible)      craftingTablePanel.swapHoveredWithHotbar(hotbarIdx)
        else if (furnacePanel.visible)       furnacePanel.swapHoveredWithHotbar(hotbarIdx)
        else if (chestPanel.visible)         chestPanel.swapHoveredWithHotbar(hotbarIdx)
        else if (inventoryPanel.visible)     inventoryPanel.swapHoveredWithHotbar(hotbarIdx)
        else if (survivalPanel.visible)      survivalPanel.swapHoveredWithHotbar(hotbarIdx)
    }

    // t229 Q / Ctrl+Q 背包内悬停槽丢弃（spec「背包内悬停槽 Q=丢 1 / Ctrl+Q=丢整栈。适用所有背包面板」）。
    //   解析 window.hoveredSlotKey（"组:下标"）→ 路由到当前可见面板的 readSlot/writeSlot（每面板薄委托
    //   InventoryOps.readSlot/writeSlot，按组分发 main/hotbar/craft/in/fuel/out/chest，VM 与本地存储通吃）→
    //   据 dropAll 取「整栈 / 1 件」量、写回槽（清空或 -1）→ 调 player.dropItemAtFront(id, n) 在玩家前方 spawn
    //   实体。空槽 / 无悬停 / 无面板 → 无操作。槽读改在 UI/VM 层（InventoryOps 单一权威）；实体生成 + 位置
    //   在 Game/Physics 层（dropItemAtFront），分层干净（PLAN §2）。不触碰光标手持栈（heldBlock）—— MC 行为：
    //   Q 直接从悬停槽丢、与光标内容无关。
    //   ⚠️ qmlcachegen 仅词法校验：函数体内 hoveredSlotKey 解析为 JS 字符串 split 必须运行期实测（同 lessons-
    //      learned「QML/JS 信号处理器静默退化」自检）。
    function dropFromHoveredSlot(dropAll) {
        const key = window.hoveredSlotKey
        if (key === "") return
        const sep = key.indexOf(":")
        if (sep < 0) return
        const group = key.substring(0, sep)
        const index = parseInt(key.substring(sep + 1), 10)
        // 路由到当前可见面板（同 swapHoveredWithHotbar 的面板分发顺序）。
        let panel = null
        if (craftingTablePanel.visible)      panel = craftingTablePanel
        else if (furnacePanel.visible)       panel = furnacePanel
        else if (chestPanel.visible)         panel = chestPanel
        else if (inventoryPanel.visible)     panel = inventoryPanel
        else if (survivalPanel.visible)      panel = survivalPanel
        if (!panel) return
        const st = panel.readSlot(group, index)
        if (!st || st.id === 0 || st.count <= 0) return           // 空槽 → 无操作
        const n = dropAll ? st.count : 1
        if (dropAll || st.count <= 1) panel.writeSlot(group, index, 0, 0)            // 清空
        else                          panel.writeSlot(group, index, st.id, st.count - 1) // -1 件
        player.dropItemAtFront(st.id, n)                          // 玩家前方生成实体（Game 层语义事件）
    }

    // 背包开关（t18）：E 键调。开 → release（光标可见点格子）；关 → grab + 焦点回键位层。
    function toggleInventory() {
        if (inventoryOpen) closeInventory()
        else openInventory()
    }
    function openInventory() {
        if (appState !== "playing" || inventoryOpen) return
        // t23/t24：E 键按模式分流 —— 创造模式开创造背包（t23 Inventory）；生存模式开生存背包（t24
        // SurvivalInventory）；观察者模式 E 无反应（t21：观察者无背包/破放）。inventoryOpen 只是「背包层
        // 在显」，显哪个面板由各组件 visible 绑定 player.mode 决定（Creative→Inventory / Survival→SurvivalInventory）。
        // 指针捕获/未捕获都走此分流（keyInput 始终持焦点）。
        if (player.mode === PlayerController.Spectator) return
        inventoryOpen = true
        player.release() // 释放指针 → 光标可见（点击/拖拽背包格子）；暂停叠层被 inventoryOpen 抑制
    }
    function closeInventory() {
        if (!inventoryOpen) return
        inventoryOpen = false
        returnHeldToHotbar()           // t56：关包归还光标手持栈（防 Q 读空槽无丢弃）
        player.grab()                  // 恢复指针锁定（隐藏光标 + 居中）
        keyInput.forceActiveFocus()
    }
    // t50 打开 / 关闭工作台面板。打开 → release（光标可见点 3×3 格）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen 互斥（开工作台前关背包，反之同）。
    function openCraftingTable() {
        if (appState !== "playing" || craftingTableOpen) return
        if (inventoryOpen) closeInventory()
        craftingTableOpen = true
        player.release()
    }
    function closeCraftingTable() {
        if (!craftingTableOpen) return
        craftingTableOpen = false
        returnHeldToHotbar()           // t56：关包归还光标手持栈（同 closeInventory）
        player.grab()
        keyInput.forceActiveFocus()
    }
    // t87 打开 / 关闭熔炉面板。打开 → release（光标可见点槽位）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen / craftingTableOpen 互斥（开熔炉前关其它两个，反之同）。
    function openFurnace() {
        if (appState !== "playing" || furnaceOpen) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        furnaceOpen = true
        player.release()
    }
    function closeFurnace() {
        if (!furnaceOpen) return
        furnaceOpen = false
        returnHeldToHotbar()           // t56：关包归还光标手持栈（同 closeInventory / closeCraftingTable）
        player.grab()
        keyInput.forceActiveFocus()
    }
    // t173/t179 打开 / 关闭箱子面板。打开 → release（光标可见点箱子 / 主栏槽）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen / craftingTableOpen / furnaceOpen 互斥（开箱子前关其它三个，反之同）。
    //   x/y/z = 所开箱子的方块世界坐标（player.chestOpened 携带 → ChestStore 据此寻址该箱子的 27 槽）。
    function openChest(x, y, z) {
        if (appState !== "playing" || chestOpen) return
        // t226 箱子上方阻挡开盖判定（机制等价 MC 1.0：箱子正上方格若为「完整立方」方块 → 盖子被压住，
        //   不开 UI / 不放盖子动画；上方为空气 / 不完整方块（半砖 / 栅栏 / 楼梯 / 火把 / 水）或另一只箱子 → 可开）。
        //   谓词用 BlockRegistry::isFullCube（theWorld.isFullCubeAt；t213/t220/t226 共用基础谓词的世界坐标版）。
        //   **箱子(id 22)虽 shape=ShapeFull 但显式豁免**：机制等价 MC「箱子是非遮挡方块」—— 两箱上下叠放各
        //   自可开盖（dev-plan t226 验收把箱子列入「上方能开」集）。其余完整立方（石 / 土 / 木板 / 工作台 /
        //   熔炉 / 原木 / 树叶 / 沙等）上方均阻挡。越界（y+1 出世界顶）→ blockAt 返回 air → isFullCubeAt=false
        //   → 不阻挡（顶格箱子无遮挡可开）。右键被压住的箱子 → 无任何反馈（不开 / 不放 / 不挥臂；placeBlock 已
        //   在 chest 分支 return 不放置，机制等价 MC 被压箱子静默不响应）。分层（PLAN §2）：纯呈现层门控，只读
        //   World（blockAt + isFullCubeAt），不写栅格；chestOpened 信号仍由 Game/Physics 发，本处决定是否呈现开盖。
        const aboveId = theWorld.blockAt(x, y + 1, z)
        if (aboveId !== 22 /* BlockRegistry::Chest */ && theWorld.isFullCubeAt(x, y + 1, z)) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        chestX = x; chestY = y; chestZ = z
        // t225 读箱子朝向 state（前面所朝方向；placeBlock 写入 = horizontalFacing^1，锁面朝玩家）→
        //   驱动盖子铰链侧（chestLidYaw）。& 3 防御性掩码（与 BlockRegistry::chestFrontFace 的 state&3 同源）。
        chestFacing = theWorld.stateAt(x, y, z) & 3
        chestOpen = true
        // t196：触发盖子翻开动画（chestLidAngle 0→全开，Behavior 平滑过渡）；chestLidPivot 据坐标 + 朝向摆位。
        chestLidAngle = kChestLidOpenAngle
        player.release()
    }
    function closeChest() {
        if (!chestOpen) return
        chestOpen = false
        // t196：触发盖子合回动画（chestLidAngle→0）；可见性绑定让合盖期间盖子仍显，到位后自动隐。
        chestLidAngle = 0
        returnHeldToHotbar()           // t56：关包归还光标手持栈（同 closeInventory / closeCraftingTable / closeFurnace）
        player.grab()
        keyInput.forceActiveFocus()
    }

    // t312 聊天栏开关 / 收发（PLAN §2 UI 层，纯呈现；聊天历史 = ListModel 呈现态，无 C++ ViewModel ——
    //   单机 Phase 1.0 无联机，聊天仅为「输入回显 + 系统播报」容器，Phase 3 联机接入时改走 LocalServer/
    //   RemoteServer 协议层收发）。开 → release 指针（光标可见 + TextField 取焦点，同背包面板模式）；关 →
    //   grab + 焦点回键位层。与背包/工作台/熔炉/箱子互斥（开前关其它）。
    function openChat() {
        if (appState !== "playing" || chatOpen) return
        // 互斥：先关任何已开背包面板（归还光标手持栈 + grab），随后 release 让聊天接管光标。
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        if (chestOpen) closeChest()
        // 死亡态不开聊天（死亡信息已由死亡屏接管；防聊天 input 抢死亡按钮焦点）。
        if (playerState.dead) return
        chatOpen = true
        player.release()               // 释放指针 → 光标可见 + TextField 可取焦点打字
        chatInput.forceActiveFocus()   // 焦点进 TextField（keyInput 持焦时 WASD 透传 player；改焦后不再透传）
    }
    // 关聊天：regrab=true（默认，回游戏态）；send 路径发完一条后调 regrab=true；Esc 取消调 regrab=true。
    //   regrab=false 预留给「聊天 → 切到其它面板」的级联（暂无此路径）。
    function closeChat(regrab) {
        if (!chatOpen) return
        chatOpen = false
        if (regrab) {
            player.grab()
            keyInput.forceActiveFocus()
        }
    }
    // 发送：把 TextField 文本（去首尾空白后非空）作为玩家消息入聊天历史（显示「名: 文本」），随后关聊天回游戏。
    //   单机无服务端 → 不广播、不持久化；Phase 3 联机时此处改为 sendChatToServer(text) 走协议层。
    //   t314 `/give` 调试聊天命令（spec t314）：debug 命令路由 → 转交 Hotbar VM 解析 + 放入背包 + 返回回显
    //     文案（系统色灰）；不进玩家消息回显（MC 聊天惯例：命令只显结果）。debug 命令无视游戏模式（创造 /
    //     生存 / 观察者都可调），见 hotbarVM.give。
    function sendChat() {
        const raw = chatInput.text
        const txt = raw.trim()
        if (txt.length > 0) {
            // /give 路由：截 `/give` 之后剩余串（含分隔空格，trim 由 C++ split SkipEmptyParts 兜底）。
            //   仅匹配 `/give` 精确前缀（避免 /givex 误触）；其余 `/` 命令暂未实现，统一当普通玩家消息。
            if (txt === "/give" || txt.startsWith("/give ")) {
                const rest = txt.slice("/give".length)  // "/give 3 64" → " 3 64"；"/give" → ""
                const reply = hotbarVM.give(rest)
                appendChatMessage("", reply, true)
            } else {
                appendChatMessage(window.playerName, txt, false)
            }
        }
        chatInput.text = ""
        closeChat(true)
    }
    // t312 系统播报入口（死亡消息 / 未来事件如 join/leave）：sender=系统来源名（玩家消息填玩家名、
    //   系统消息填 "" 由 delegate 隐藏「名:」前缀直接显文本）；isSystem=true 走灰红色（区别玩家白）。
    //   死亡播报由 onDied 路由调本函数（playerState.deathCauseText 给文案）。
    function appendChatMessage(sender, text, isSystem) {
        chatMessages.append({sender: sender, text: text, isSystem: isSystem === true})
        // 历史上限（防无限增长吃内存 / 渲染）：保留最近 kChatHistoryMax 条，从头删溢出。
        while (chatMessages.count > chatHistoryMax) chatMessages.remove(0)
        // 触发消息行重显（fade 动画重启）+ 自动滚到底。
        chatFadeTimer.restart()
    }
    // t312 聊天历史上限（保留最近 N 条；超出从头删。MC 1.0 聊天亦有限滚动缓冲，避免无限增长）。
    readonly property int chatHistoryMax: 50

    // 单一体素世界（内部 3×3=9 chunk，世界 48×48×16；QML API 不变）：网格(ChunkGeometry)
    // 与物理(PlayerController)共用同一份栅格。
    // t276 大世界（可配网格）：width/depth = worldChunksPerSide*16（默认 10 → 160×160=10×10=100 chunk）。
    //   高度 128（t307：地表抬高至 ~64，留出树冠 + 天空间 / 飞行；原 64 已不够 new surface+树）。worldgen
    //   覆盖全幅（ChunkManager / generate 按 m_width/m_depth 迭代，维度无关；chunk 跨满高，128 即 taller column）。
    World { id: theWorld; width: window.worldChunksPerSide * 16; depth: window.worldChunksPerSide * 16; height: 128; seed: 1337 }

    // t176 存档系统（SQLite，PLAN §2-L）：世界列表 / 新建 / 删除 / 打开 / 保存 / 加载。绑定 theWorld
    //   使 WorldStore 经 chunks() 序列化 chunk blob。玩家态（pos/血/背包/模式）以裸原语经 gather /
    //   apply 函数在 QML 编排（WorldStore 不持 Game 层对象引用，保依赖只向下）。saves/ 目录由 WorldStore
    //   解析（<exeDir>/../saves 开发期 / AppLocalDataLocation 部署期）。
    WorldStore { id: worldStore; world: theWorld }
    // t232 封面黑屏修复：窗口级截图工具（grabToImage 拍不到 View3D 3D 场景 → 改 grabWindow）。仅依赖 Qt，
    //   无自有层依赖。grab(window) 等下一帧 frameSwapped → 离屏重渲含 3D pass → emit grabbed(QImage)。
    ScreenGrab { id: screenGrab }
    // t176 当前世界会话：进入世界时记 file/name，保存退出时 saveAll(name) / 显示用。
    property string currentWorldFile: ""
    property string currentWorldName: ""
    // t191 截封面退出进行中标志：grabToImage 异步，ready 回调与兜底定时器两路可能都发 → 此标志 + finishExitToWorldList
    //   入口守门防重复收尾；saveAndExitToWorldList 开头也据它防连点重复触发。
    property bool coverGrabPending: false
    // t232 抓帧时把 View3D 抬到最上层（z=999 > 暂停叠层 100 / 菜单 200）→ 覆盖暂停叠层 + HUD →
    //   grabWindow 拍到无 UI 的纯 3D 场景。仅 saveAndExitToWorldList 抓帧期间为 true（~1 帧），抓完复位。
    //   单点控 UI 隐藏（绑 view3d.z），免逐个 gate 各 HUD 叠层 visible。
    property bool coverHideUi: false
    // t191 抓帧兜底定时器：ready 500ms 内未发（极端情况，View3D 不可抓帧）→ 直接收尾，绝不卡退出。
    Timer {
        id: coverExitFallback
        interval: 500
        repeat: false
        onTriggered: window.finishExitToWorldList()
    }
    // t232 ScreenGrab.grab → onCoverGrabbed 桥接（grabbed 信号驱动收尾）。
    Connections {
        target: screenGrab
        function onGrabbed(image) { window.onCoverGrabbed(image) }
    }

    // 昼夜时钟（t09，PLAN §2-H）：~20 分钟周期的天光亮度乘子 lerp（**非**旋转方向光）。
    // dayPhase 0..1 循环（0=正午 / 0.5=子夜）；skyLight [0,1] 是纯函数派生的天光乘子，供下面
    // SceneEnvironment.clearColor 与 DirectionalLight.brightness lerp 昼(#9ec6e8/1.5)↔夜(#0b1026/0.25)。
    // 呈现层只读消费、绝不反向写时间（PLAN §2 分层）。F6 切调试加速（~30s 一周期）便于肉眼验收。
    WorldClock { id: worldClock }

    // t155 编辑活跃期 → 太阳步进节流桥接：World 任一编辑（破 / 放 / 落沙着地 / 尺寸初始化）发 worldChanged；
    //   呈现层把「编辑活跃」反馈给 WorldClock.noteEditActivity()，使其在编辑活跃期（近 1.5s 内有编辑）跳过
    //   太阳跨步全量 mesh 重建（避免与编辑即时重建争帧）。纯 QML 桥接，不引入 C++ 跨层依赖
    //   （WorldClock 为 Game 层、不 include World；QML 同时持二者引用并桥接 = 向下合法，PLAN §2 分层不破）。
    //   连接为同线程直连 → noteEditActivity 在 setBlock 的 emit worldChanged 栈内同步执行，时间戳精准。
    Connections { target: theWorld; function onWorldChanged() { worldClock.noteEditActivity() } }
    // t276 全幅 mesh 顶点 / 三角面汇总刷新：所有「几何变化」事件（setBlock / setBlockFromEntity / setWaterSilent /
    //   generate / regenerate / load finishLoad）都 emit worldChanged → 在此重算 meshVertices/meshTriangles 标量供
    //   F3 只读。sun-step 重建只改顶点色不改几何 → 顶点/三角面数不变 → 不需在 sunChanged 重算（省一次全幅扫）。
    //   不连每个 chunk 的 meshRebuilt：100 chunk 创建期会级联 100 次 recompute → meshVertices 写 → F3 text 绑定
    //   连续重算 → QML binding-loop 检测器误报（虽自收敛，但留 WRN）。worldChanged 是几何变化的唯一汇聚点，足够。
    Connections { target: theWorld; function onWorldChanged() { window.recomputeMeshStats() } }

    // t177 环境音强度 ←→ 昼夜：风声夜间更静谧（level = 0.5 + 0.5*skyLight：白天 1.0、子夜 0.5）。
    //   dayPhaseChanged 每 100ms tick 发（与 clearColor / DirectionalLight 同节拍）→ setAmbientLevel
    //   即时改 looping 风声的音量（在播时；未播仅记值，下次 startAmbient 生效）。纯呈现层桥接，
    //   PLAN §2 分层：Game 层 worldClock.skyLight（只读）→ Core/Platform 层 audio.setAmbientLevel。
    Connections {
        target: worldClock
        function onDayPhaseChanged() { audio.setAmbientLevel(0.5 + 0.5 * worldClock.skyLight) }
    }

    // t223 水贴图动画 flipbook 驱动：每 ~800ms 把 window.waterAnimPhase 0↔1 翻转 → 水段 ChunkGeometry
    //   （绑了 waterAnimPhase）setWaterAnimPhase 触发 buildMesh(Water) 换帧。spec「静止水 2 帧慢播，勿快」：
    //   800ms 节拍肉眼读作「轻微荡漾」而非快闪刺眼。仅 playing 态跑（菜单态 View3D 已隐、水段离场，无需动；
    //   且避免菜单态无谓重建水段 mesh 浪费主线程）。triggered 翻转 phase：0→1→0 循环。
    //   分层（PLAN §2）：纯 QML 呈现层 Timer（不进 Game 层 WorldClock；动画是呈现层选择，非时间语义）。
    Timer {
        id: waterAnimTimer
        interval: 800
        repeat: true
        running: window.appState === "playing"
        onTriggered: window.waterAnimPhase = (window.waterAnimPhase === 0) ? 1 : 0
    }
    // t223 近流水 proximity 水流声：PlayerController.flowSoundLevel（每 ~0.25s 节流扫描更新，0=无近流水 / 近=1）
    //   → AudioManager.startWaterFlow/stopWaterFlow/setWaterFlowLevel。level>0 启动并设音量；level<=0 停。
    //   start/stop 幂等（AudioManager 内部守 waterFlowPlaying）；setWaterFlowLevel 仅在播时即时改音量。纯呈现层
    //   桥接（Game 层 player.flowSoundLevel 只读 → Core 层 audio 消费，PLAN §2 分层）。引擎 / clip 失败时
    //   AudioManager 内部静默降级（§2-E），此处无需守卫。
    Connections {
        target: player
        function onFlowSoundLevelChanged() {
            const lvl = player.flowSoundLevel
            if (lvl > 0.0) {
                audio.startWaterFlow()
                audio.setWaterFlowLevel(lvl)
            } else {
                audio.stopWaterFlow()
            }
        }
    }

    // 昼↔夜颜色 / 亮度 lerp 辅助（t09）：m=worldClock.skyLight ∈ [0,1]（0=子夜、1=正午）。
    // day 颜色 #9ec6e8 = (0.620,0.776,0.910)；night 颜色 #0b1026 = (0.043,0.063,0.149)。
    // 不影响 NoLighting 材质方块的自发光（地形仍按其材质常数显——昼夜只改环境/天光，spec）。
    function dayNightColor(m) {
        return Qt.rgba(0.043 + (0.620 - 0.043) * m,
                       0.063 + (0.776 - 0.063) * m,
                       0.149 + (0.910 - 0.149) * m,
                       1.0)
    }
    function dayNightBrightness(m) { return 0.25 + (1.5 - 0.25) * m }

    // t121：地形 chunk 顶点色现已承载天光遮蔽（见天 1.0 / 地下 0.2，mesher 按 chunk heightmap 烘焙进
    //   ColorSemantic）。移除原 nightTint 全屏叠层后，昼夜天光乘子改由材质 baseColor（灰阶亮度）承载——
    //   PrincipledMaterial vertexColorsEnabled=true 时最终色 = baseColor × vertexColor × 贴图，即
    //   「昼夜乘子 × 天光遮蔽 × 贴图」：地表昼 = 1.0×1.0×tex、地表夜 = 0.4×1.0×tex、洞穴昼 = 1.0×0.2×tex。
    //   m=skyLight ∈ [0,1]（0=子夜、1=正午）；floor 0.4 ≈ 原 nightTint alpha 0.6 把地形拉暗的等效量级
    //   （夜间仍可辨识地形轮廓，spec t09）。纯灰阶乘子（不偏色）——夜色基调由 sky clearColor 提供。
    function terrainLight(m) {
        const fl = window.minLight
        const b = fl + (1.0 - fl) * m
        return Qt.rgba(b, b, b, 1.0)
    }

    // t144：把任意颜色按天光亮度乘子调暗（掉落实体材质昼夜适配）。
    //   与 terrainLight(m) 同 floor 0.4 公式，但作用于给定 (r,g,b)∈[0,1] 而非纯灰阶——
    //   掉落物的工具 tier 色（木褐 / 石灰 / 铁银白）与浅灰外壳也须夜间变暗，与地形 / 方块段
    //   统一。机制等价地形 baseColor=terrainLight × vertexColor：掉落物 BlockCube 无顶点色（恒白=1.0），
    //   故昼夜乘子只由 baseColor 承载；本函数给「带自身色调」的材质（工具 / 外壳）用。
    function tintBySkyLight(r, g, b, m) {
        const fl = window.minLight
        const k = fl + (1.0 - fl) * m
        return Qt.rgba(r * k, g * k, b * k, 1.0)
    }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }
    // t173/t179 箱子内容存储 VM（按方块世界坐标键控的 27 槽；ChestUI 读写 + onBlockBroken(Chest) 清孤儿）。
    //   纯 Game/ViewModel 层，不依赖 World/Renderer；物品栈语义同 Hotbar（id=0=空）。ChestUI 经 chestX/Y/Z
    //   寻址当前所开箱子；多只箱子各自独立 27 槽，跨 UI 开关持久。
    ChestStore { id: chestStore }

    // 玩家状态（生命/饥饿，t22）：满血满饥初值。心/饥饿条读其 health/hunger；掉落伤害经
    // 下面的 Connections 路由到 takeDamage（呈现层只读，绝不反向写数值；PLAN §2 分层）。
    PlayerState { id: playerState }

    // 方块掉落实体管理器（t35）：生存破可掉落方块时在该格生成 item entity（旋转 / 浮动小方块
    // 图标），等 t36 拾取。纯数据持有（pos + itemId），呈现层（下方 View3D 的 Repeater）只读。
    // 触发由 PlayerController 发 spawnItem 信号，下面 Connections 转发到 spawnItem()（单向事件流）。
    ItemEntityManager { id: itemEntities }

    // t95 统一实体管理器（Entities 层）：为后续 Mob/AI 系统铺垫的统一实体基类（pos / 半径 / 可推动
    // 标志 / 渲染外观）。本轮持有测试生物（pushable=true，纯色方块），玩家走碰可推动；掉落物
    // （itemEntities）机制等价「pushable=false、被拾取」的实体变体（本轮不迁移、仅确立基类形态）。
    // 重力 / 推动物理由 PlayerController.tick 驱动（持 EntityManager*）；呈现层（下方 mobHost Repeater）
    // 只读 count/posAt/colorAt 自发渲染（PLAN §2 分层：呈现只消费 Entities 数据）。
    EntityManager { id: entityManager }

    // t220 落沙遇不完整方块失撑 → 变掉落物：EntityManager 发 fallingBlockDropped（坐标 = 不完整方块上方
    //   一格、id = 沙方块 id）→ 转发到 itemEntities.spawnItem 生成掉落实体（机制等价 MC「沙落火把上 → 沙
    //   碎成掉落物」）。单向事件流：EntityManager 不持有 ItemEntityManager（同 player.spawnItem 模式；
    //   PLAN §2 分层：Entities 层发语义事件，呈现层只消费）。
    Connections {
        target: entityManager
        function onFallingBlockDropped(x, y, z, blockId) { itemEntities.spawnItem(x, y, z, blockId, 1) }
        // t297 爆炸掉落（EntityManager detonateStalker 内 ~50% 概率/破坏块发）：转发到
        //   ItemEntityManager.spawnItem 生成掉落实体（机制等价 MC 爆炸把被毁方块弹成物品）。itemId 已是
        //   BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落）。同 fallingBlockDropped 模式：单向事件流
        //   （PLAN §2 分层：Entities 层发语义事件、呈现层只消费）。掉落实体上限 kCap=200 自管防溢出。
        function onExplosionDroppedItem(x, y, z, itemId) { itemEntities.spawnItem(x, y, z, itemId, 1) }
        // t242 mob 死亡掉落（spec「血 0→死亡掉落物：猪:生猪排 / 牛:皮革+生牛肉 / 羊:羊毛」）：damageEntity
        //   扣血到 ≤0 时 EntityManager 发 mobDied(x,y,z,mobType) → 据子类 id 转发到 ItemEntityManager.spawnItem
        //   生成对应掉落实体（机制等价 MC 1.0 被动生物掉落；数量取 MC 1.0 量级：猪 1-2 生猪排 / 牛 1 皮革
        //   + 1-2 生牛肉 / 羊 1 羊毛；MobTest 不掉落）。同 fallingBlockDropped 模式：单向事件流（PLAN §2 分层：
        //   Entities 层发语义事件、呈现层只消费）。坐标 = mob 死亡格 floor(pos)，与 spawnItem 整数格约定一致。
        //   t299 敌对掉落（spec「敌对掉落物：骸骨→骨头 / 蹒跚者→腐肉 / 蜘蛛→线」）：骸骨 1-2 骨头 / 蹒跚者 1-2
        //   腐肉 / 蜘蛛 1-2 线；MobStalker（爆炸型）无常规掉落（爆炸破坏块掉落归 t297 explosionDroppedItem）。
        //   ⚠️ QML 无法直接 import RecipeRegistry（C++ 静态类），故用字面量 id（同 MaterialIcon.qml 约定）：
        //     0x20B=生猪排 / 0x20C=生牛肉 / 0x20D=皮革 / 0x20E=羊毛（RecipeRegistry::RawPorkchopId 等）。
        //     0x217=骨头 / 0x218=腐肉 / 0x219=线（RecipeRegistry::BoneId / RottenFleshId / StringId，t299）。
        //     id 改动须同步 src/Game/recipe.h（单一权威）。
        function onMobDied(x, y, z, mobType) {
            if (mobType === EntityManager.MobPig) {
                itemEntities.spawnItem(x, y, z, 0x20B, 1)   // 生猪排 ×1-2
                itemEntities.spawnItem(x, y, z, 0x20B, 1)
            } else if (mobType === EntityManager.MobCow) {
                itemEntities.spawnItem(x, y, z, 0x20D, 1)   // 皮革 ×1
                itemEntities.spawnItem(x, y, z, 0x20C, 1)   // 生牛肉 ×1-2
                itemEntities.spawnItem(x, y, z, 0x20C, 1)
            } else if (mobType === EntityManager.MobSheep) {
                itemEntities.spawnItem(x, y, z, 0x20E, 1)   // 羊毛 ×1
            } else if (mobType === EntityManager.MobBones) {
                // t301 敌对掉落：骸骨（骷髅）→ 骨头 ×1-2 + 箭 ×0-2 + 弓（~50%）。
                //   机制等价 MC 1.0 骷髅掉骨头 + 箭 + 有时弓（spec t301：弓 ~50% 概率非 100%，区别于被动掉落的恒定数量）。
                //   弓 id=0x10F（ToolRegistry::Bow，工具段；不可堆叠 maxStack=1）/ 箭 id=0x21A（RecipeRegistry::ArrowId）/
                //   骨头 id=0x217（RecipeRegistry::BoneId）。⚠️ QML 不 import C++ 静态类，故用字面量（同上注释约定）。
                //   弓 / 箭的两次 Math.random() 独立判定 → 箭总量 0-2、弓 0-1，每件独立掉落实体（同被动掉落模式）。
                itemEntities.spawnItem(x, y, z, 0x217, 1)   // 骨头 ×1-2（恒掉，每件独立实体）
                itemEntities.spawnItem(x, y, z, 0x217, 1)
                if (Math.random() < 0.5) itemEntities.spawnItem(x, y, z, 0x21A, 1)  // 箭 ~50% ×1
                if (Math.random() < 0.5) itemEntities.spawnItem(x, y, z, 0x21A, 1)  // 箭 ~50% ×1（独立 → 总量 0-2）
                if (Math.random() < 0.5) itemEntities.spawnItem(x, y, z, 0x10F, 1)  // 弓 ~50%（ToolRegistry::Bow）
            } else if (mobType === EntityManager.MobShambler) {
                // t299 敌对掉落：蹒跚者（僵尸）→ 腐肉 ×1-2（机制等价 MC 1.0 僵尸掉腐肉）。
                itemEntities.spawnItem(x, y, z, 0x218, 1)   // 腐肉 ×1-2
                itemEntities.spawnItem(x, y, z, 0x218, 1)
            } else if (mobType === EntityManager.MobSpider) {
                // t299 敌对掉落：蜘蛛 → 线 ×1-2（机制等价 MC 1.0 蜘蛛掉线；弓 / 钓竿原料，t304 弓配方用）。
                itemEntities.spawnItem(x, y, z, 0x219, 1)   // 线 ×1-2
                itemEntities.spawnItem(x, y, z, 0x219, 1)
            }
            // MobTest（通用测试生物）/ MobStalker（潜行者；爆炸型，机制等价 MC 苦力怕无常规掉落）不掉落 —— 调试 /
            //   爆炸型无游戏内常规产出。Stalker 爆炸破坏方块的掉落由 detonateStalker 的 explosionDroppedItem 单独发（t297）。
        }
        // t300 剪羊毛掉落（spec「剪刀右键羊 → 羊变裸 + 掉羊毛物品」）：EntityManager shearSheep 内发
        //   sheepSheared(x,y,z)（坐标 = 羊当前格 floor(pos)，与 spawnItem 整数格约定一致）→ 转发到
        //   ItemEntityManager.spawnItem 生成羊毛物品掉落实体（机制等价 MC 1.0 剪羊毛掉落羊毛；杀羊掉落羊毛
        //   归 onMobDied 的 MobSheep 分支，二者独立 —— 剪羊毛不杀羊、杀羊前已剪则死时不再多掉）。
        //   0x20E = RecipeRegistry::WoolId（材料段羊毛物品；⚠️ QML 不 import C++ 静态类故字面量，同 onMobDied 约定）。
        //   单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只消费，同 fallingBlockDropped / mobDied 模式）。
        function onSheepSheared(x, y, z) { itemEntities.spawnItem(x, y, z, 0x20E, 1) }
        // t281 敌对 mob 近战攻击 / t283 骷髅箭 / t284 Stalker 爆炸命中玩家（spec「attack」）：EntityManager 发
        //   mobAttackedPlayer(amount, mobType, kbX, kbZ) → 仅 Survival 应用伤害（Creative/Spectator 无伤跳过，机制
        //   等价 MC 创造/观察者无敌）。复用 PlayerState.takeDamage → damaged 红闪 / 视角晃 / 受伤音链（同
        //   fallDamageTaken 路径；PLAYER 无伤模式经此门控不进 takeDamage）。单向事件流（PLAN §2 分层：Entities
        //   发语义事件、呈现层只消费）。
        // t296 玩家受击击退：kbX/kbZ = 欲推开玩家的水平单位方向（近战=玩家−mob / 箭=箭速方向 / 爆炸=玩家−Stalker）。
        //   调 player.applyHitKnockback 给玩家一个水平冲量 + 小跳（机制等价 MC 玩家被击退；仅 Survival 生效，方法内
        //   自守模式）。先击退后扣血（顺序无关 —— 击退写冲量、扣血走 PlayerState，互不依赖）。
        function onMobAttackedPlayer(amount, mobType, kbX, kbZ) {
            if (player.mode === PlayerController.Survival) {
                player.applyHitKnockback(kbX, kbZ)
                // t311 据 mobType 映射致死来源（PlayerState::DeathCause）：蹒跚者/骸骨/蜘蛛近战或箭、潜行者爆炸。
                //   EntityManager 与 PlayerState 两枚举在此汇合（QML 是唯一同时见两者的层），保持 PlayerState 与
                //   EntityManager 解耦（不引入 C++ 反向依赖）。未知 mobType → Generic。
                var cause = PlayerState.Generic
                if (mobType === EntityManager.MobShambler) cause = PlayerState.Shambler
                else if (mobType === EntityManager.MobBones) cause = PlayerState.Bones
                else if (mobType === EntityManager.MobSpider) cause = PlayerState.Spider
                else if (mobType === EntityManager.MobStalker) cause = PlayerState.Stalker
                playerState.takeDamage(amount, cause)
            }
        }
        // t284 Stalker 爆炸（EntityManager detonateStalker 发）：爆炸的单一音/视反馈入口 —— 播爆炸音
        //   （playExplosion）+ 白色迸发粒子（burstExplosion）。方块破坏走 setWaterSilent 不发 blockBroken
        //   → 免球形内每块破块粒子 spam，故本信号是爆炸音/视的唯一驱动（同 fallDamageTaken→takeDamage 模式；
        //   PLAN §2 分层：Entities 层发语义事件、呈现/音频层只消费）。
        function onExplosion(x, y, z) {
            audio.playExplosion()
            if (particleLoader.item) particleLoader.item.burstExplosion(x, y, z)
        }
        // t304 玩家箭命中 mob（spec「抛物+伤害 mobs」）：damageEntity 已扣血 + 红闪（delegate 绑 hurtFlashAt）+
        //   归零 mobDied；本信号驱动命中音（同近战 attackMob→onMobAttacked→playMobHurt 模式）。
        function onArrowHitMob(mobType) { audio.playMobHurt(mobType) }
    }

    // t89 / t118 / t177 音效（Core/Platform 层，miniaudio 封装）：破 / 放 / 挖 / 脚步 / 拾取 / 门开关 /
    //   受伤 / 环境 SFX，按方块材质分组（石/木/草/沙/叶）clip 池（spec「playBreak/playMining/playStep
    //   按 group 选」）。触发由 Game 层信号发出（World::blockBroken/blockPlaced、PlayerController::
    //   miningParticle / itemPickedUp / walkPhaseChanged、PlayerState::damaged），呈现层经 Connections
    //   转发到 playBreak/playPlace/playMining/playPickup/playStep/playHurt（音频层只消费，PLAN §2 分层）。
    //   t177 环境音（风声床）：startAmbient 进 playing 启 / stopAmbient 退菜单停（见 startGame/enterWorld/
    //   saveAndExitToWorldList/returnToMenu）；setAmbientLevel 据昼夜 skyLight 调强度（上方 worldClock
    //   Connections 驱动，夜间更静谧）。
    //   引擎初始化 / 音频加载失败时 AudioManager 内部静默降级（§2-E），此处无需守卫。
    //   _prevWalkPhase / _walkAccum 是脚步节律追踪态（QML 实例局部属性，非音频引擎态）：
    //   walkPhaseChanged 累加相位差（2π 回绕感知），每半步（Δ≥π）播一次脚步音。
    AudioManager {
        id: audio
        property real _prevWalkPhase: 0.0
        property real _walkAccum: 0.0
    }

    // 玩家控制器：指针锁定鼠标 + WASD/跳/飞 + 三模式物理。
    //   selectedBlock（右键放置用）绑 hotbarVM.selectedBlockId（工具槽→Air→不放置）。
    //   selectedItem（t34 挖掘速度用）绑 hotbarVM.selectedItemId（工具段透传 → ToolRegistry 算速度）。
    //   hotbar / itemEntities（t36）：PlayerController 持 peer VM 指针做拾取扫描 / 丢弃（经 Q_PROPERTY
    //   绑定注入，同 world 模式）。拾取 = tick 扫附近实体 → addStack；丢弃 = Q 键 takeStack → spawnItem。
    PlayerController {
        id: player
        world: theWorld
        hotbar: hotbarVM
        itemEntities: itemEntities
        entityManager: entityManager
        worldClock: worldClock
        selectedBlock: hotbarVM.selectedBlockId
        selectedItem: hotbarVM.selectedItemId
    }

    // 玩家掉落伤害 → 生命（t22）：PlayerController 在 Survival 着地结算时发 fallDamageTaken(hp)，
    // 呈现层经此 Connections 路由到 PlayerState.takeDamage（与破/放信号→粒子同模式：Game 层发
    // 语义事件，呈现层只消费）。PlayerController 不持有 PlayerState，保持单向事件流、分层干净。
    Connections {
        target: player
        function onFallDamageTaken(hp, cause) { playerState.takeDamage(hp, cause) } // t311 透传致死来源（Fall/Suffocation/Drowning/Starvation）
        // t238 饥饿回血 → PlayerState.heal（饱腹态每 4s 回 1HP；同 fallDamageTaken→takeDamage 反向配对）。
        function onHealed(hp) { playerState.heal(hp) }
        // t202 气泡值更新 → PlayerState.air（Physics 层算时序、Game 层持显值、呈现层路由；同 fallDamageTaken→
        //   takeDamage 模式）。溺水扣血复用 onFallDamageTaken（→ takeDamage → damaged 红闪 / 视角晃）。
        function onAirUpdated(air) { playerState.setAir(air) }
        // t238 饥饿值更新 → PlayerState.setHunger（Physics 层 m_hunger 推进 / 食用恢复时发；同 airUpdated→
        //   setAir 模式）。饥饿归零扣血复用 onFallDamageTaken（→ takeDamage → damaged 红闪 / 视角晃）。
        function onHungerUpdated(hunger) { playerState.setHunger(hunger) }
        // t35：生存破可掉落方块（drop=true）→ player 发 spawnItem → 转发到 manager 生成实体。
        // 创造 / 不可采掘时 player 不发本信号（无实体产出）。ViewModel 不持有 PlayerController，
        // 经 Connections 解耦（同 fallDamageTaken→PlayerState 模式；PLAN §2 分层）。
        // t64：spawnItem 信号带 count 参数（整栈丢弃为 1 实体；破块掉落走 BlockDef.dropCount）。
        function onSpawnItem(x, y, z, id, count) { itemEntities.spawnItem(x, y, z, id, count) }
        // t61：挖掘过程粒子 —— 生存累积挖掘时每跨一阶，player 发 miningParticle（被挖方块坐标+id），
        // 转发到 BlockParticles.burstMine（复用破块碎屑 emitter / 色逻辑 / 重力，少量迸发，进度反馈）。
        // 破块完成时的 +30% 大迸发仍由 onBlockBroken → burstBreak 驱动（burstBreak 已在此任务内 +30%）。
        // t165：挖掘音改由下方 onMiningSound 统一驱动（含基岩等不可挖方块的 hold-mine 音反馈，
        // spec「保持 mining 态挥臂+音」）；本处仅迸碎屑（碎屑仍只对可挖方块，基岩不破无碎屑）。
        function onMiningParticle(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstMine(x, y, z, id)
        }
        // t267 进食屑粒（持面包按住右键累积进食时每跨一节拍 player 发 eatingParticle，携嘴部世界坐标）：
        //   转发到 BlockParticles.burstEat（面包色屑粒从嘴部迸发）。机制等价 MC 进食屑粒。
        //   x/y/z 为 float 世界坐标（玩家眼位），非方块格 → burstEat 内不加 +0.5（区别 burstBreak/burstMine）。
        function onEatingParticle(x, y, z) {
            if (particleLoader.item) particleLoader.item.burstEat(x, y, z)
        }
        // t165：挖掘击打音（每节拍一响）—— player 发 miningSound（被挖方块 id），**含不可挖基岩**的
        //   hold-mine 音反馈（spec「生存基岩可持续挖 ... 保持 mining 态挥臂+音」；机制等价 MC 镐撞基岩响）。
        //   id 给 AudioManager 按材质组选 mining clip。音与碎屑解耦：音对所有被挖方块，碎屑仅可挖。
        function onMiningSound(id) { audio.playMining(id) }
        // t118：拾取掉落实体 → player 发 itemPickedUp(id, count) → 拾取音（pickup clip，不分材质）。
        // 信号在 pickupScan 实际入栈时（全 / 部分）才发；全满装不下不发（无伪触发）。机制等价 MC
        // 「拾起物品啵一声」。t120：同时启动 handPopAnim（手 Y 弹跳，音 + 手弹双反馈）。
        function onItemPickedUp(id, count) { audio.playPickup(); handPopAnim.start() }
        // t50：右键工作台 → player 发 craftingTableOpened → 开 3×3 合成面板（释放指针 / 关包互斥）。
        function onCraftingTableOpened() { window.openCraftingTable() }
        // t87：右键熔炉 → player 发 furnaceOpened → 开 FurnaceUI 冶炼面板（释放指针 / 关包互斥）。
        function onFurnaceOpened() { window.openFurnace() }
        // t173/t179：右键箱子 → player 发 chestOpened(x,y,z) → 开 ChestUI（释放指针 / 关包互斥）。
        //   坐标供 ChestStore 寻址该箱子的 27 槽。
        function onChestOpened(x, y, z) { window.openChest(x, y, z) }
        // t152：右键门 / 活版门 useBlock → player 发 doorToggled(open) → 路由到 AudioManager 开门 / 关门音。
        //   一次开合动作 = 一次音（门两格同翻 player 只发一次）。音频层只消费，PLAN §2 分层。
        function onDoorToggled(open) { open ? audio.playDoorOpen() : audio.playDoorClose() }
        // t242/t248/t295 玩家攻击 mob（spec「受伤音效」）→ 据 mobType 播对应受击音：被动（0-3）走通用
        //   mob_hurt.wav（t248 专属 mob 受击声，区别于玩家 hurt.wav；spec「受击音换专属 mob 受伤声」，替代
        //   旧复用 playHurt 路径）；敌对（4-7）走各专属音（t295「骨头敲击/蜘蛛嘶/僵尸哀嚎/苦力怕爆炸声」——
        //   Shambler 哀嚎 / Bones 骨头敲击 / Spider 蜘蛛嘶嗡 / Stalker 嘶嘶，复用其 ambient idle clip；
        //   Stalker 爆炸专属音走 onExplosion→playExplosion 的 detonation 路径）。mob 红闪由
        //   EntityManager.damageEntity 设 hurtFlash → QML delegate baseColor 绑定 hurtFlashAt>0 ? "#ff0000" 已
        //   驱动；扣血由 damageEntity 内完成。t248 攻击冷却在 PlayerController::attackMob 内门控（长按连击
        //   每 0.5s 一次伤害，防瞬秒），此信号仅在真扣血时发（冷却内 attackMob 早退不发）。呈现 / 音频层只
        //   消费 Game/Physics 语义事件（同 swingArm / blockBroken 模式；PLAN §2 分层）。
        function onMobAttacked(mobType, crit) { audio.playMobHurt(mobType) }
        // t23/t24：背包打开时按 G 循环切模式 —— 切到观察者（无背包）则关闭；Creative↔Survival 间切换
        // 则保留背包打开，面板由各组件 visible 绑定 player.mode 自动换（创造背包↔生存背包）。避免任一
        // 背包在不兼容模式下滞留（Spectator 无背包/破放，t21）。
        function onModeChanged() {
            if (window.inventoryOpen && player.mode === PlayerController.Spectator)
                window.closeInventory()
            // t171：切模式**不动背包** —— 创造↔生存切换保留物品（用户诉求「cycleMode 切换不清空背包，
            //   仅切模式不动背包」）。不再调 hotbarVM.resetForMode（旧 t32/t49 逻辑切模式即清空 9 hotbar
            //   + 27 主栏 + 光标手持栈，违用户期望）。背包仅在构造期空起，其后由玩家拾取 / 调色板点取填入；
            //   切观察者只是隐藏 hotbar UI（t20），数据保留，回创造 / 生存仍在。切观察者时上方
            //   closeInventory 已 returnHeldToHotbar 归还光标手持栈，无物品丢失。
        }
    }

    // t89 脚步音节律（PLAN §2 分层：Game 层 walkPhase 信号驱动，音频层只消费）。
    //   walkPhaseChanged 仅在走时（moveSpeed>0.1）发 —— Survival / Creative-未飞 按住 WASD 才推进；
    //   飞行 / Spectator moveSpeed=0 不发（playercontroller.cpp:805 守），故天然无脚步音（spec 验收项）。
    //   半步节律：一个完整 stride = 2π 含两次脚落地（左+右），故每累积 Δphase≥π 播一次脚步音。
    //   2π 回绕感知：phase 从 ~2π 跳回 0 时 raw delta<0 → 补 2π（playercontroller 在 >=2π 时减 2π 回绕）。
    //   t118：脚步音按脚下表面方块材质分流（spec「playStep 按 group 选」）。表面 = 脚底 -0.1 那一格
    //   （脚底 m_pos.y 减一点防恰在整数边界踩空 → 越界返 air → GroupDefault 兜底 Stone step，仍响）。
    //   t269：脚位在水中（player.feetInWater）→ 改播水中走路声 playWaterStep（不分材质，水下听感统一闷浊；
    //     机制等价 MC 水中步声），覆盖材质分流。feetInWater 涵盖「眼在水面上但脚在水里」的涉水走步。
    Connections {
        target: player
        function onWalkPhaseChanged() {
            const phase = player.walkPhase
            let d = phase - audio._prevWalkPhase
            if (d < 0) d += 6.28318530718  // 2π 回绕（phase 从 ~2π 跳回 0）
            audio._prevWalkPhase = phase
            audio._walkAccum += d
            if (audio._walkAccum >= Math.PI) {
                audio._walkAccum -= Math.PI
                if (player.feetInWater) {
                    // t269 水中迈步 → 水声（替代按材质 step）。
                    audio.playWaterStep()
                } else {
                    // 脚下表面方块 id（材质组判定用）；越界 / air → 0 → GroupDefault 兜底 Stone step。
                    const feet = player.feetPosition
                    const sid = theWorld.blockAt(Math.floor(feet.x),
                                                 Math.floor(feet.y - 0.1),
                                                 Math.floor(feet.z))
                    audio.playStep(sid)
                }
            }
        }
    }

    // t250 mob 环境音（牛叫/羊叫/猪叫 idle + 走路声）：EntityManager tick 内周期 emit mobAmbient(mobType)
    //   （idle 叫声）+ walkPhase 半步 emit mobStep(mobType, blockId)（走路声），均经听者范围（kAudioRange）
    //   门控（近 mob 才发声）。呈现层经此 Connections 路由到 AudioManager.playMobAmbient/playMobStep（音频层
    //   只消费，PLAN §2 分层：Entities 层发语义事件、Core/Platform 层音频只消费，绝不反向写）。同
    //   onMobAttacked→playMobHurt / onMobDied→spawnItem 模式（单向事件流）。引擎 / clip 失败时 AudioManager
    //   内部静默降级（§2-E），此处无需守卫。
    Connections {
        target: entityManager
        function onMobAmbient(mobType) { audio.playMobAmbient(mobType) }
        function onMobStep(mobType, blockId) { audio.playMobStep(mobType, blockId) }
    }

    View3D {
        id: view3d
        anchors.fill: parent
        // t232 抓封面期间抬到最上层：盖住暂停叠层 / HUD，让 grabWindow 拍到纯 3D 场景（无「PAUSED」面板 /
        //   hotbar / HUD 文字）。平时 z=0（叠层在上层正常显）。opaque clearColor 背景 → 抬高后完全盖住 UI。
        z: window.coverHideUi ? 999 : 0
        environment: SceneEnvironment {
            // t09：clearColor 随天光乘子 lerp 昼(#9ec6e8)↔夜(#0b1026)；方向固定（PLAN §2-H 非
            // 旋转方向光）。绑定 skyLight → 每周期 tick 自动刷新（debugFast 下 ~30s 一圈）。
            clearColor: dayNightColor(worldClock.skyLight)
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            id: cam
            // F5 相机模式循环（t27）：第一人称 / 第三人称-后 / 第三人称-前。
            //   第一人称：眼位 + 欧拉 (pitch,yaw,0)
            //   第三人称-后：眼位 − look·d + 欧拉 (pitch,yaw,0) —— 相机退到玩家身后朝前看，玩家在视野下前方
            //   第三人称-前：眼位 + look·d + 欧拉 (−pitch,yaw+180,0) —— 相机绕到玩家正前方回看其正面
            // lookVector 由 yaw/pitch 派生（PlayerController.lookDirection）；偏移沿视线方向，旋转据模式翻转。
            // mode 标志属控制器（Game 层），摆位属 QML 呈现层（PLAN §2 分层）。
            // t40：第三人称距离 d 不再写死 3.5，改读 player.cameraDistance（控制器每帧沿偏移方向 DDA 钳制到
            // 首个实体命中距离；无命中=3.5，命中则贴在面前）→ 相机贴墙不穿入。Math.min 为安全钳（值已 ≤3.5）。
            position: {
                const eye = player.position
                const look = player.lookVector
                const m = player.cameraMode
                if (m === PlayerController.FirstPerson) return eye
                const d = Math.min(3.5, player.cameraDistance)
                const s = (m === PlayerController.ThirdPersonBack) ? -d : d  // 后退 vs 前推
                return Qt.vector3d(eye.x + look.x * s, eye.y + look.y * s, eye.z + look.z * s)
            }
            eulerRotation: {
                // t67 受伤视角晃动：shakePitch/shakeYaw 小幅抖动叠加进相机俯仰/偏航（onDamaged 触发 shakeAnim，
                //   ~0.2s 衰减到 0 → 平时为 0 不影响视角）。三模式都加（第一人称最显；第三人称相机方向微抖）。
                const sp = cam.shakePitch, sy = cam.shakeYaw
                if (player.cameraMode === PlayerController.ThirdPersonFront)
                    return Qt.vector3d(-player.pitch + sp, player.yaw + 180 + sy, 0) // 回看正面：俯仰反向 + 偏航 +180
                return Qt.vector3d(player.pitch + sp, player.yaw + sy, 0)            // 第一人称 & 第三人称-后：朝前看
            }
            // 受伤视角晃动偏移（t67）：由 shakeAnim 驱动（衰减抖动，静止恒 0）。读 cam.shakePitch/shakeYaw 进
            //   上方 eulerRotation 绑定 → NOTIFY 触发绑定重算 → 视角随抖动偏移；静止时 = 0 不影响。
            property real shakePitch: 0.0
            property real shakeYaw: 0.0
            clipNear: 0.05
            clipFar: 1000

            // 第一人称手 viewmodel（t29）：手臂 Model 作 PerspectiveCamera 子节点 → 随相机移动/转向
            // （相机本地空间：+X=右、-Y=下、-Z=前向）。纯色肤色 PrincipledMaterial 自绘原创（§9 override (a)，
            // 不拷贝 MC 皮肤/手贴图）。模型属呈现层、纯装饰——不进 World/Physics（PLAN §2 分层）。
            // 显隐：仅第一人称可见（第三人称看玩家全身模型 t28，手隐藏；与 playerModel 的 visible 互补）。
            //   t46：观察者模式第一人称也隐手（与观察者禁放破 / 幽灵半透一致——观察者无动作，无需显手）。
            // 挥动由下方 Connections(onSwingArm) → SequentialAnimation 驱动 viewModelHand.swingAngle，
            // 其 eulerRotation.x 绑定自动跟随：手臂从「略前抬」下挥（前推/下劈）再回位 ~200ms。
            Node {
                id: viewModelHand
                visible: player.cameraMode === PlayerController.FirstPerson
                         && player.mode !== PlayerController.Spectator
                // t52/t73 手不穿模（渲染于地形之上）：旧 pivot z=-0.4 + baseTilt 65° + scale 0.16 使手指尖伸到
                //   相机本地 z≈-0.49，深于玩家 AABB 半宽 0.3（实体方块最近可到 z=-0.3）→ 手被前方邻块遮挡 / 穿进墙。
                //   t52 注释写了修法但代码没改（仍 z=-0.4/tilt65/scale0.16）；t73 落实：pivot 收到 z=-0.2、baseTilt
                //   减到 30°、臂段 scale 收到 0.09 → 手段中心相机本地 z∈[-0.25(静止), -0.13(挥峰)]，始终近于 0.3 →
                //   depth 测试恒胜地形 → 手臂恒在所有实体方块之前（不穿模）。pivot.y 上移到 0.05 使手仍落视野下中。
                //   t91：pivot.x 0.35→0.20 整手左移（旧 0.35 偏右，手持方块出右框）；y/z 不动（不穿模余量不变）。
                //   t156 固化：用户经设置面板 ArmSlider 重新调定终值（baseTilt -34.56°、position (0.36,-0.12,-0.39)），
                //     有意放弃 t73/t91 的「不穿模几何」换取「贴近方块」手感（position.z=-0.39 深于 AABB 半宽 0.3）；
                //     上方 window.handBaseTilt/handPosX/Y/Z 为权威默认，本节点 baseTilt/position 全读它们。
                // t120 popY：拾取/拿取时整手 Y 弹跳（0→-0.08→0，~200ms；下方 handPopAnim 驱动）。
                //   叠加进 position.y → 手下沉一点再回位（「拿到东西手一沉」反馈）。与 swingAngle 正交：
                //   swing 改 eulerRotation.x（绕肩挥动）、pop 改 position.y（位移），互不干扰、可叠加
                //   （拾取时手不挥、破/放时手挥不弹）。
                position: Qt.vector3d(window.handPosX, window.handPosY + viewModelHand.popY - viewModelHand.eatDropY, window.handPosZ + viewModelHand.bowPullZ)
                readonly property real baseTilt: window.handBaseTilt  // 读 window 级（t129 引入、t139 起由 ESC 设置面板 ArmSlider 实时调）；t156 固化用户终值 -34.56（见 window.handBaseTilt 注释）
                property real swingAngle: 0.0          // 挥动增量（度）；0=静止。下挥=负（手往下/前劈），回位=0
                property real popY: 0.0                 // t120：拾取/拿取弹跳位移（Y）；0=静止，负=下沉
                // t267 进食动画量：eatDropY = 手下沉位移（进食时手落下到嘴边，正=下沉，绑进 position.y 减去）；
                //   eatTilt = 抖动增量（度，绑进 eulerRotation.x；进食时高频小幅振荡 = 嚼动）。均由下方
                //   eatDropAnim/eatRiseAnim/eatShakeAnim 驱动（onEatingStateChanged 启停）。与 swingAngle/popY 正交
                //   （各改不同属性），不与 armSwingAnim/handPopAnim 冲突。
                property real eatDropY: 0.0
                property real eatTilt: 0.0
                // t304 弓拉弓动画：拉弓时整手微仰（arm raises into aim pose）+ 略后拉（z 向相机回收）。
                //   bowPullTilt = 蓄力 ×18°（满弓仰 18°）、bowPullZ = 蓄力 ×0.06（z 后拉 0.06 格）。
                //   均直读 player.bowDrawProgress（连续，无动画对象；progress 0→1 平滑跟随蓄力）。
                readonly property real bowPullTilt: player.bowDrawing ? player.bowDrawProgress * 18.0 : 0.0
                readonly property real bowPullZ: player.bowDrawing ? player.bowDrawProgress * 0.06 : 0.0
                eulerRotation: Qt.vector3d(viewModelHand.baseTilt + viewModelHand.swingAngle + viewModelHand.eatTilt + viewModelHand.bowPullTilt, 0, 0)

                // 上臂袖段（t73 蓝袖子）：覆盖肩-肘（上半段），上衣色 #3a6a9a（hurtTint 0.227/0.416/0.604，与
                //   第三人称左/右臂袖段同色）。原第一人称手臂单肉色无袖子（蓝袖子只在第三人称）→ 加此段对齐。
                //   作 viewModelHand 子节点 → 随挥动同步旋转。UnitCube + NoLighting（同地形/线框已验证可见路径）。
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.10, 0)     // t91：袖段下移（旧 +0.02 在上=右上蓝，应袖近躯干在下）
                    scale: Qt.vector3d(0.12, 0.18, 0.12)   // t81：加粗（0.09→0.12），手明显变大（零穿模：z 深度未动）
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.416, 0.604) }
                }
                // 前臂/手（肤色 #caa472；肘下手段）。原整臂一色，t73 拆为袖+手两段（上半蓝袖、下半肤色手）。
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, 0.02, 0)     // t91：手段上移（旧 -0.10 在下，应手前伸在上=左上肤色）
                    scale: Qt.vector3d(0.11, 0.16, 0.11)  // t81：加粗+加长 Y（0.085/0.12→0.11/0.16），手变长变大
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.792, 0.643, 0.447) }
                }
                // 手持方块（t73 可见性修复；t156 位置重定）：持有方块（selectedBlock≠0）时，手前显该方块。
                //   t156：手臂 baseTilt 由 100° 改 -34.56° 后，旧 z=-0.11 在屏幕上落到「手下方」（绕肩旋转使
                //     原「手前」方向转为偏下）→ 方块显在手下面非手中。修：方块沿本地 -Z（手臂朝向）再前移到
                //     z=-0.22（脱离手段 z 包围 [-0.055,0.055]，方块 z 范围 [-0.28,-0.16] 全在手腕前方）、与手持
                //     工具(z=-0.22)同深 → 方块稳稳落在手腕前方、从相机侧可见、不被手皮遮挡。
                //   scale 0.12（大于手段 0.085，更显眼）；BlockCube + 共享图集 voxelAtlas → per-face 贴图
                //   （草顶/草侧…），复用地形贴图（零 MC 资产）。作 viewModelHand 子节点 → 随挥动同步运动（块在手中）。
                //   selectedBlock=0 时 BlockCube 兜底为 Stone 但 Model.visible=false 不渲染（blockId 兜底仅防空 UV）。
                Model {
                    visible: player.selectedBlock !== 0 && player.selectedBlock !== 13 && !hotbarVM.isPartialBlock(player.selectedBlock)
                    geometry: BlockCube { blockId: player.selectedBlock }
                    position: Qt.vector3d(0.0 + window.heldBlockX, 0.02 + window.heldBlockY, -0.22 + window.heldBlockZ)    // t156 基线 + t166c ESC 滑条偏移（heldBlockX/Y/Z）
                    scale: Qt.vector3d(0.12, 0.12, 0.12)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
                        alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）已走下方 billboard 分支，本立方路径不再处理它们
                    }
                }
                // t219 手持木板衍生方块（第一人称）：异形段（台阶/楼梯/栅栏/压力板/门/活板门）在世界内非整立方
                //   → 走 billboard 平图标（dimetric 立体图标 icon_wood_*.png），非 BlockCube 满格木板立方（异形
                //   各面 tile=planks → BlockCube 渲成「一块木板」与木板不可辨）。机制同手持火把 / 材料 billboard：
                //   作 viewModelHand 子节点会继承手 baseTilt/swing 的 Rx 旋转 → billboard +Z 不再正对相机；补偿
                //   local eulerRotation.x = -(baseTilt+swing) 抵消手 X 旋转 → 世界旋转 = 相机旋转 → +Z 恒指回相机。
                //   scale 0.18（同手持材料 billboard，平图标稍大显眼）；alphaCutoff:0.5 + opacity:0.99 沿用透明底
                //   alpha-test 契约（图标外透明底不丢弃会被当不透明黑 → 坍黑块）。partialIconTex.source 绑定
                //   iconSourceForBlock(selectedBlock) → 选不同异形自动换图（单一 Texture 覆盖全部 6 类）。
                Model {
                    visible: hotbarVM.isPartialBlock(player.selectedBlock)
                    geometry: BillboardQuad {}
                    position: Qt.vector3d(0.02 + window.heldBlockX, 0.04 + window.heldBlockY, -0.22 + window.heldBlockZ)
                    scale: Qt.vector3d(0.18, 0.18, 0.18)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaCutoff: 0.5
                        opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                        baseColorMap: partialIconTex
                    }
                }
                // t218/t260 手持火把（第一人称）：火把非立方（世界内异形），手持走 billboard 平图标（细立柱），
                //   非上方 BlockCube（6 面立方贴图 → 肉眼「贴火把的小立方」非「火把」）。BillboardQuad 单面 +Z
                //   法线 + icon_torch.png（透明底火把本体）→ 渲染成一根细长火把而非方块。
                //   t260 放大：scale 0.10×0.22 → 0.14×0.32（用户「手持贴图太小 → 放大」）。
                //   t260 燃烧动画：火把顶端加多色焰（外橙 + 中黄 + 白核）+ heldFlameS 跳动 → 持火把时火焰活跃
                //     燃烧（机制对齐世界内 torchFlame 三层焰 + default_torch 贴图焰心配色）。
                //   作 viewModelHand 子节点会继承手 baseTilt/swing 的 Rx 旋转 → billboard +Z 不再正对相机；
                //   补偿 local eulerRotation.x = -(baseTilt+swing) 抵消手 X 旋转 → 世界旋转 = 相机旋转 →
                //   +Z 恒指回相机、正面可见（同手持材料 BillboardQuad / 掉落物材料段 billboard 模式）。
                //   alphaCutoff:0.5 + opacity:0.99 沿用 alpha-test 契约（透明底不丢弃会被当不透明黑 → 火把坍黑块）。
                //   13 = BlockRegistry::Torch（与既有字面量 + 注释模式同源）。
                Node {
                    id: heldTorchFp
                    visible: player.selectedBlock === 13
                    position: Qt.vector3d(0.0 + window.heldBlockX, 0.04 + window.heldBlockY, -0.22 + window.heldBlockZ)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)

                    // 火把 billboard 平图标（t260 放大）
                    Model {
                        geometry: BillboardQuad {}
                        scale: Qt.vector3d(0.14, 0.32, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            baseColorMap: torchIconTex
                        }
                    }

                    // t260 燃烧动画：火把顶端多色焰（外橙 + 中黄 + 白核），heldFlameS 缩放跳动。
                    //   位置：billboard 顶端（scale Y 0.32 → 顶端 y≈0.16；纹理火焰占顶 ~5/16、焰心约 y≈0.14）；
                    //   z=+0.01 略前移（local +Z 经上方 eulerRotation 补偿后指回相机）→ 焰在 billboard 之前、无 z-fight。
                    property real heldFlameS: 0.05
                    Node {
                        position: Qt.vector3d(0, 0.14, 0.01)
                        Model {   // 外焰（橙）
                            geometry: UnitCube {}
                            scale: Qt.vector3d(heldTorchFp.heldFlameS, heldTorchFp.heldFlameS * 1.10, heldTorchFp.heldFlameS)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff8a1a" }
                        }
                        Model {   // 中焰（黄）
                            geometry: UnitCube {}
                            scale: Qt.vector3d(heldTorchFp.heldFlameS * 0.70, heldTorchFp.heldFlameS * 0.78, heldTorchFp.heldFlameS * 0.70)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffd23c" }
                        }
                        Model {   // 焰心（暖白）
                            geometry: UnitCube {}
                            scale: Qt.vector3d(heldTorchFp.heldFlameS * 0.45, heldTorchFp.heldFlameS * 0.50, heldTorchFp.heldFlameS * 0.45)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#fff4c4" }
                        }
                    }
                    SequentialAnimation on heldFlameS {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.05; to: 0.065; duration: 110 }
                        NumberAnimation { from: 0.065; to: 0.04; duration: 150 }
                        NumberAnimation { from: 0.04; to: 0.055; duration: 90 }
                        NumberAnimation { from: 0.055; to: 0.05; duration: 130 }
                    }
                }
                // 手持工具（t75 木镐 3D / t233 锄 3D / t264 斧铲剑 3D）：选中工具槽（isTool(selectedItem)）时，
                //   手前显工具 3D。根因：旧分支只看 selectedBlock（工具槽→Air→selectedBlock=0）→ 选工具时无渲染分支
                //   → 木镐不可见。修：加本分支，可见性读 isTool(player.selectedItem)（selectedItem 含工具段，
                //   selectedBlockId 不再把工具「归零隐藏」——放置语义仍走 selectedBlock=Air 不放置，互不干扰）。
                //   PickaxeGeometry / HoeGeometry / AxeGeometry / ShovelGeometry / SwordGeometry 是纯实色体素几何
                //   （无贴图 / 无 alpha）→ 永不黑（修「工具贴图黑」根因）。
                //   t264：按 toolType 选 5 类工具几何（镐 / 锄 / 斧 / 铲 / 剑）—— 五个互斥 Model（共享位姿 / 材质参数，
                //   仅 geometry + visible 不同），避免 Loader 装载 3D 的 reparent 坑（lessons-learned t16）。
                //   baseColor 按 tier 着色（木褐 / 石灰 / 铁银白，五类同 tier 同色，同 2D ToolIcon 配色）。
                //   作 viewModelHand 子节点 → 随挥动同步运动（工具在手中）；eulerRotation 给对角手持姿态。
                // t266 镐手持贴图修：旧版整把镐用 PickaxeGeometry（pos-only 单一 baseColor）→ 铁镐整把银白
                //   （柄也变白 =「纯白铁棍」）；且单色使柄/头不分 → 观感像「手拿镐头中间」。spec 要「木质柄 +
                //   镐头、正握」。改两段着色：木柄恒木褐（与 tier 无关，机制对齐 MC 镐柄恒木），镐头（横梁 + 两
                //   下勾）按 tier 着色（木褐 / 石灰 / 铁银白）。用 4 个 UnitCube 复刻 pickaxe.cpp 的 addBox 4 盒
                //   形状（UnitCube ±0.5 满格 → scale = 2×半长，1:1 对齐几何尺寸），各盒独立材质 → 柄 / 头各走各的
                //   baseColor。PickaxeGeometry 单色无法分染（一个 baseColor 乘全体），故手持走 UnitCube 组合；
                //   第三人称 / 掉落物仍用 PickaxeGeometry（侧 / 俯视角单色观感可接受，本任务范围 = viewModelHand）。
                //   正握：position.y 上移（旧 0.04→0.10）使手（手段 y≈0.02）握住柄下段、镐头朝上前方；eulerRotation
                //   给对角手持（柄下右、镐头上左，类 MC）。作 viewModelHand 子节点 → 随挥动同步运动（工具在手中）。
                Node {
                    id: heldPickaxeFp
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 1
                    position: Qt.vector3d(0.02, 0.10, -0.22)     // t266：y 上移让手握柄下段（正握），镐头朝上前方；z=-0.22 脱离手臂 z 包围
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, -15)      // 对角手持（柄下右、镐头上左，类 MC 手持）
                    // 镐头 tier 配色（柄恒木褐，头随 tier）：木褐 / 石灰 / 铁银白（同 2D ToolIcon 配色）
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁镐银白
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石镐中灰
                                                                                                 : "#8a5a2e"                                                   // 木镐褐（默认 / tier 1）
                    // 木柄（竖直）：心 (0,-0.05,0)，半长 0.04×0.40×0.04（同 pickaxe.cpp 木柄 addBox）→ scale 2×半长
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.05, 0)
                        scale: Qt.vector3d(0.08, 0.80, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木柄恒木褐（与 tier 无关）
                    }
                    // 镐头横梁：心 (0, 0.40,0)，半长 0.30×0.05×0.06（同 pickaxe.cpp 横梁 addBox）
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.40, 0)
                        scale: Qt.vector3d(0.60, 0.10, 0.12)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldPickaxeFp.headColor }
                    }
                    // 左下勾：心 (-0.26, 0.31,0)，半长 0.05×0.07×0.06
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.26, 0.31, 0)
                        scale: Qt.vector3d(0.10, 0.14, 0.12)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldPickaxeFp.headColor }
                    }
                    // 右下勾：心 (0.26, 0.31,0)，半长 0.05×0.07×0.06
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.26, 0.31, 0)
                        scale: Qt.vector3d(0.10, 0.14, 0.12)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldPickaxeFp.headColor }
                    }
                }
                // t233 锄（type=Hoe）：同位姿 / 同 tier 配色，仅几何换 HoeGeometry（宽扁锄刃替代镐横梁 + 下勾）。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 2
                    geometry: HoeGeometry {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, -15)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                }
                // t264 斧（type=Axe）：同位姿 / 同 tier 配色，几何换 AxeGeometry（单边厚刃替代镐横梁 + 下勾）。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 3
                    geometry: AxeGeometry {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, -15)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                }
                // t264 铲（type=Shovel）：同位姿 / 同 tier 配色，几何换 ShovelGeometry（方形铲斗）。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 4
                    geometry: ShovelGeometry {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, -15)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                }
                // t264 剑（type=Sword）：纵向长刃几何，位姿竖直前指（区别于工具的对角手持）；eulerRotation.x
                //   略前倾使刃尖朝前上方（持剑突刺姿态），同 tier 配色。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 5
                    geometry: SwordGeometry {}
                    position: Qt.vector3d(0.02, 0.02, -0.22)
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(20, -15, -10)    // 剑身竖直略前倾、刃尖朝前上
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                }
                // t304/t330 弓（type=Bow）：BowGeometry C 形弓身（XY 面清晰 C 弯）+ 子节点 BowStringGeometry
                //   白弦（凹侧）。弓身平面 XY 正对相机（local +Z→world +Z）→ 弧清晰可见（旧版弧在 YZ 深度面被
                //   压成直棍，观感「两根棍」）。拉弓动画由 viewModelHand 父 Node 的 bowPullTilt/bowPullZ 驱动。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 7
                    geometry: BowGeometry {}
                    position: Qt.vector3d(0.02, 0.04, -0.24)
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    // rotY=0：弓身平面 XY 正对相机（旧版 180° 是为旧 YZ 弧翻 belly 朝前；新版弧在 XY 不需翻）。
                    // 微仰 10° 表「持弓瞄准」。凸侧 -X（左）、凹侧 +X（右，弦所在）。
                    eulerRotation: Qt.vector3d(10, 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                    // t330 白弦（蜘蛛丝白，独立于 tier）：子节点继承父 position/scale/eulerRotation → 与弓身同位。
                    Model {
                        geometry: BowStringGeometry {}
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#f5f5f5"
                        }
                    }
                }
                // t329 剪刀（type=Shears）第一人称手持：剪刀无独立 3D 几何 → billboard ToolIcon 平图标
                //   （同材料段手持路径：BillboardQuad + Canvas sourceItem + 抵消手 X 旋转 → billboard 世界朝向 =
                //   相机朝向，+Z 恒指回相机）。剪刀恒铁色（ToolIcon toolType===6 内部强制铁灰，不跟随 tier），
                //   与掉落物 / 背包槽图标一致；机制对齐 MC 第一人称剪刀平贴手心。alphaCutoff:0.5 + opacity:0.99
                //   沿用 MaterialIcon 透明底 alpha-test 契约（Canvas 透明底不丢弃会被当不透明黑 → 图标坍成黑块）。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 6
                    geometry: BillboardQuad {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.18, 0.18, 0.18)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaCutoff: 0.5
                        opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                        baseColor: terrainLight(worldClock.skyLight)
                        baseColorMap: Texture {
                            flipV: false
                            sourceItem: ToolIcon {
                                toolType: 6
                                width: 64; height: 64
                            }
                        }
                    }
                }
                // t169 手持材料（木棒/煤/木炭/铁锭 等）：选中材料段槽（isMaterial(selectedItem)）时，手前显
                //   该材料的平图标 billboard。机制对齐 MC（手持非方块物品=平图标贴脸相机）+ spec t169
                //   「四类贴图都要有」覆盖材料段（①背包槽 MaterialIcon / ②本手持 / ③掉落物 BillboardQuad+
                //   MaterialIcon / ④3D 方块 N/A 材料不可放置 —— 三类齐）。复用 BillboardQuad + MaterialIcon
                //   Canvas（同掉落物材料段路径），icon 统一（同一份自绘图）。
                //   朝相机：作 viewModelHand 子节点会继承手 baseTilt/swing 的 Rx 旋转 → billboard +Z 不再正对
                //   相机。补偿：local eulerRotation.x = -(baseTilt+swing) 抵消手 X 旋转 → billboard 世界旋转 =
                //   相机旋转（同掉落物材料段 cam.eulerRotation 减 rotY 抵消的 billboard 模式）→ +Z 恒指回相机。
                //   scale 0.18（介于手段 0.11 与手持方块 0.12 之间，平图标稍大显眼），z=-0.22 同手持方块/工具
                //   （脱离手臂 z 包围）。alphaCutoff:0.5 + opacity:0.99 沿用 MaterialIcon 透明底 alpha-test 契约
                //   （Canvas 透明底不丢弃会被当不透明黑 → 图标坍成黑块）。
                Model {
                    visible: hotbarVM.isMaterial(player.selectedItem)
                    geometry: BillboardQuad {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.18, 0.18, 0.18)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaCutoff: 0.5
                        opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                        baseColor: terrainLight(worldClock.skyLight)
                        baseColorMap: Texture {
                            // t186 修复(b)：flipV 改 false。MaterialIcon Canvas 以左上为原点（y 向下）画——
                            //   桶开口在顶部小 row、桶底在大 row，与背包槽直接用 MaterialIcon（无 flipV）显的
                            //   「开口朝上」一致。旧 flipV:true 把 V 翻 → 桶开口渲染成朝下（spec「开口朝下 反了」）。
                            //   材料段所有图标同此 BillboardQuad 路径 + Canvas 同约定，flipV 翻转使全部材料上下倒置
                            //   （木棒 / 铁锭等因对称或罕见手持未被察觉）；改 false 后与背包槽图标一致（开口 / 高光朝上）。
                            flipV: false
                            sourceItem: MaterialIcon {
                                materialId: player.selectedItem
                                width: 64; height: 64
                            }
                        }
                    }
                }
                // [t31] 诊断：确认手 Node 加载 + parent（相机）。
                Component.onCompleted: console.info("[t31] viewModelHand UP parent=" + viewModelHand.parent + " vis=" + visible)
            }
        }

        // 第一人称手挥动动画（t29）+ 第三人称挖掘挥臂动画（t45）：破/放动作真发生（player.swingArm）时
        //   同时启动两条 SequentialAnimation——
        //   (1) armSwingAnim：第一人称手 viewmodel 绕肩枢轴下挥 ~75° 再回位（同 t29）。
        //   (2) bodySwingAnim：第三人称玩家模型双臂 mineBlend 0→1→0，前抬 70° 覆盖行走摆臂再回落（t45）。
        //   连续点击：start() 重启进行中的动画（重新挥一次），契合快速挖掘手感。
        //   分层（PLAN §2）：swing 信号由 Game/Physics 层(breakBlock/placeBlock)发，呈现层只消费
        //   （同 blockBroken→粒子 / fallDamageTaken→PlayerState 模式）。两条动画都在 View3D 内（playerModel
        //   与 viewModelHand 均在此作用域），按 id 直接 target。
        Connections {
            target: player
            function onSwingArm() { armSwingAnim.start(); bodySwingAnim.start() }
        }
        SequentialAnimation {
            id: armSwingAnim
            loops: 1
            NumberAnimation { target: viewModelHand; property: "swingAngle"; to: -75; duration: 90; easing.type: Easing.InQuad }
            NumberAnimation { target: viewModelHand; property: "swingAngle"; to: 0; duration: 140; easing.type: Easing.OutQuad }
        }
        // t120 拾取/拿取手弹跳动画：player.itemPickedUp（生存走过掉落物拾取）或创造背包拿取
        //   （Inventory.itemTaken → 宿主 onItemTaken）时启动。整手 Y 下沉 0.08 再回位（~200ms），
        //   「拿到东西手一沉」反馈。与 armSwingAnim 正交（改 popY 位移 vs swingAngle 旋转），
        //   拾取时不挥手、挥动时不弹；连点 start() 重启进行中的动画。
        //   分层（PLAN §2）：纯呈现层动画，消费 Game 层语义事件（同 armSwingAnim / 粒子模式）。
        SequentialAnimation {
            id: handPopAnim
            loops: 1
            NumberAnimation { target: viewModelHand; property: "popY"; to: -0.08; duration: 100; easing.type: Easing.InQuad }
            NumberAnimation { target: viewModelHand; property: "popY"; to: 0; duration: 100; easing.type: Easing.OutQuad }
        }
        // 第三人称挖掘挥臂（t45）：mineBlend 0→1（前 80ms 抬臂）→ 0（后 160ms 回落），总 ~240ms。
        //   双臂枢轴的 eulerRotation 绑定读 mineBlend：>0 时双臂前抬 70°（覆盖行走摆臂），=0 时正常行走/中性。
        //   仅第三人称可见时显效（第一人称 playerModel.visible=false，动画仍跑但肉眼不见——无副作用）。
        SequentialAnimation {
            id: bodySwingAnim
            loops: 1
            NumberAnimation { target: playerModel; property: "mineBlend"; to: 1.0; duration: 80; easing.type: Easing.OutQuad }
            NumberAnimation { target: playerModel; property: "mineBlend"; to: 0.0; duration: 160; easing.type: Easing.InQuad }
        }
        // t267 进食动画（手落下到嘴边 + 嚼动抖动）：player.eatingStateChanged 翻转时启停（下方 Connections）。
        //   eatDropAnim/eatRiseAnim：手 Y 下沉 / 回位（位移，绑进 viewModelHand.position.y 减去 eatDropY）。
        //   eatShakeAnim：eulerRotation.x 高频小幅振荡（嚼动；±6°，loops:Infinite，进食期间持续）。
        //   spec「手落下 + 抖动 + 屑粒动画 → 消耗」的前两者由此三动画落地；屑粒由 eatingParticle → burstEat。
        //   分层（PLAN §2）：纯呈现层动画，消费 Game 层语义事件（同 armSwingAnim / handPopAnim 模式）。
        NumberAnimation { id: eatDropAnim; target: viewModelHand; property: "eatDropY"; to: 0.18; duration: 120; easing.type: Easing.OutQuad }
        NumberAnimation { id: eatRiseAnim; target: viewModelHand; property: "eatDropY"; to: 0.0;  duration: 120; easing.type: Easing.InQuad }
        SequentialAnimation {
            id: eatShakeAnim
            loops: Animation.Infinite
            NumberAnimation { target: viewModelHand; property: "eatTilt"; from: 0; to: 6;  duration: 55 }
            NumberAnimation { target: viewModelHand; property: "eatTilt"; from: 6; to: -6; duration: 55 }
            NumberAnimation { target: viewModelHand; property: "eatTilt"; from: -6; to: 0; duration: 55 }
        }
        // t267 进食态翻转 → 启停进食动画（onEatingStateChanged）。进食开始 = 下沉 + 抖动启；结束 = 回位 +
        //   抖动停 + eatTilt 归零（防动画停在非零相位留残角）。与 onSwingArm（armSwingAnim）正交：进食期间
        //   不发 swingArm（updateEating 节拍只发 eatingParticle），仅完成时 finishEating 发一次 swingArm
        //   （此时 eating 已结束 → 本 handler 已停 eatShakeAnim，无冲突）。
        Connections {
            target: player
            function onEatingStateChanged() {
                if (player.eating) { eatDropAnim.start(); eatShakeAnim.start() }
                else { eatRiseAnim.start(); eatShakeAnim.stop(); viewModelHand.eatTilt = 0 }
            }
        }

        // t67 受伤反馈动画（替换 t51 全屏 damageOverlay 红闪）：
        //   hurtAnim：playerModel.hurt 1→0（~0.4s）→ 各身体部件 baseColor 经 hurtTint lerp 回正色（模型变红后回正）。
        //   shakeAnim：cam.shakePitch / shakeYaw 衰减抖动 0→±幅→0（~0.2s）→ 相机轻微晃动。
        //   两者都由 PlayerState.damaged 触发（onDamaged 里 start()）；连击 start() 重启进行中的动画。
        //   分层（PLAN §2）：纯呈现层动画，消费 Game 层语义事件（同 swingArm 模式）。
        NumberAnimation {
            id: hurtAnim
            target: playerModel
            property: "hurt"
            from: 1.0
            to: 0.0
            duration: 400
            easing.type: Easing.OutQuad
        }
        ParallelAnimation {
            id: shakeAnim
            loops: 1
            // pitch 抖动（上下）：4 段衰减，总 ~0.2s（45+45+45+65=200ms）
            SequentialAnimation {
                NumberAnimation { target: cam; property: "shakePitch"; from: 0; to: 3.5; duration: 45; easing.type: Easing.OutQuad }
                NumberAnimation { target: cam; property: "shakePitch"; to: -2.5; duration: 45 }
                NumberAnimation { target: cam; property: "shakePitch"; to: 1.5; duration: 45 }
                NumberAnimation { target: cam; property: "shakePitch"; to: 0; duration: 65; easing.type: Easing.InQuad }
            }
            // yaw 抖动（左右）：与 pitch 反向相位，更自然（同 ~0.2s 总长）
            SequentialAnimation {
                NumberAnimation { target: cam; property: "shakeYaw"; from: 0; to: -3.0; duration: 45; easing.type: Easing.OutQuad }
                NumberAnimation { target: cam; property: "shakeYaw"; to: 2.0; duration: 45 }
                NumberAnimation { target: cam; property: "shakeYaw"; to: -1.0; duration: 45 }
                NumberAnimation { target: cam; property: "shakeYaw"; to: 0; duration: 65; easing.type: Easing.InQuad }
            }
        }

        // t09 昼夜：brightness 随天光乘子 lerp 1.5(昼)↔0.25(夜)；**方向固定不变**（PLAN §2-H：
        // 亮度乘子 lerp，非旋转方向光）。NoLighting 材质地形不受 DirectionalLight 影响（自发光恒定），
        // 故地形不会变暗——昼夜视觉由 sky clearColor 渲染；DirectionalLight 只影响场景内 lit 元素。
        DirectionalLight {
            eulerRotation.x: -40
            eulerRotation.y: -25
            brightness: dayNightBrightness(worldClock.skyLight)
        }

        // t135 可视太阳：t123 顶点光有方向调制但天空空（无可视太阳）→ 加一个暖白大球挂在天空，
        //   让昼夜「太阳划过天空」肉眼可见。位置 = 相机眼位 + worldClock.sunDir·40 → 太阳始终在
        //   「朝太阳方向」40 格远的天空；sunDir 随 dayPhase 量化跨步演变（kSunSteps=360 调试周期下
        //   ~0.083s 一步）→ 太阳缓慢划过天空（F6 加速 ~30s 一圈最直观）。
        //   PLAN §2-H「非旋转方向光」仍成立：此 Model 是纯呈现层装饰球，**不参与光照计算**——光照
        //   方向仍由顶点色 sunDir 烘焙（chunkgeometry.cpp），DirectionalLight 的 eulerRotation 固定不变。
        //   材质 NoLighting 自发光（同地形 / 线框 / 玩家模型已验证可见路径；lessons-learned「所有可见
        //   Model 用 NoLighting」）。太阳落到地平线下（sunDir.y<=0，夜间）隐藏。
        //   分层（PLAN §2）：纯呈现层，只读 worldClock.sunDir（Game 层 Q_PROPERTY），绝不反向写。
        //
        //   t164 太阳贴图：几何用 BillboardQuad（自定义广告牌四边形，+Z 法线单面），**替代内置
        //   #Sphere**。根因（t31 诊断）：本工程实测静态 `source:"#Cube"/"#Sphere"` Model 不渲染
        //   （自定义 QQuick3DGeometry——地形/线框/BillboardQuad 掉落物——均可见，内置 primitives 不可见），
        //   故太阳之前虽挂了 sun.png 贴图却永远空（R18「太阳依旧不显示」即此）。改走 BillboardQuad
        //   自定义几何 + NoLighting PrincipledMaterial 这条**已验证可见**路径 → 太阳显贴图而非空。
        //   **朝相机（billboard）**：QQuick3DNode 无 lookAt 属性（写 `lookAt:` 会触发 objectCreationFailed，
        //   见 lessons-learned qmlcachegen 坑），故令承载 Model 的世界欧拉 = 相机欧拉（cam.eulerRotation）
        //   → 四边形 +Z 法线恒 = -相机 forward → 指回相机 → 正面恒正对玩家（同 Main.qml 材料段掉落物
        //   BillboardQuad 已验证路径，第一/第三人称三模式都对）。太阳盘「贴脸相机」恒呈轴对齐圆盘（不随
        //   视角透视压缩），机制对齐 MC 1.0 天空太阳（平面盘）。
        //
        //   t169 太阳依旧不显示修复：v1 把太阳摆在 eye + sunDir·40 = 距眼 40 格。但 5×5 chunk 世界
        //   （b6b34e0，80×80）地形从原点向 ±40 延伸 —— 太阳距眼 40 恰好落在地形体积内（黄昏/黎明
        //   sunDir.y≈0 时太阳水平距 40 = 地形边缘），且不透明 PrincipledMaterial 走深度测试 → 太阳被
        //   与视线相交的地形/树叶遮挡而不可见（正午直射时太阳在玩家头顶尚可见，其余时段被地形遮蔽）。
        //   机制对齐 MC 1.0：太阳贴在「天空穹顶」= 视点远端无穷远处，永远在地形之后。落地：把太阳推到
        //   距眼 500 格（远超 5×5 世界边界 ±40 + 玩家偏移，地形永不在太阳与相机之间），scale 同比放大
        //   到 80（角尺寸 80/500≈0.16 rad≈9°，与 v1 8/40≈0.2 rad 相近，视觉显眼不糊屏）。clipFar=1000
        //   足以容纳（500 < 1000）。
        Model {
            visible: worldClock.sunDir.y > 0.0   // 太阳在地平线上才显（夜间隐藏）
            geometry: BillboardQuad {}
            position: {
                const eye = player.position
                const s = worldClock.sunDir
                return Qt.vector3d(eye.x + s.x * 500, eye.y + s.y * 500, eye.z + s.z * 500)
            }
            eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y, 0)   // billboard 朝相机
            scale: Qt.vector3d(80.0, 80.0, 80.0)   // 天空太阳盘（距相机 500 格 ×80；远超地形边界，永不遮蔽）
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffffff"   // 白底乘贴图（纹理原色直出）
                baseColorMap: Texture { source: "qrc:/textures/sun.png" } // 太阳贴图（实心不透明盘）
            }
        }

        // 共享图集纹理：3×3=9 个 per-chunk Model 共用一份 atlas（声明一次、按 id 引用）。
        Texture { id: voxelAtlas; source: "qrc:/textures/atlas.png"; generateMipmaps: false }

        // t240 猪牛羊贴图：三种 passive mob 各一张「全脸」贴图（build_mob.py 程序生成原创像素图，§9a 区隔
        //   不照搬 MC）。MobModel 几何每面铺整张贴图 [0,1]×[0,1]（同 CrackBox 全脸 UV）→ mobHost delegate 据
        //   entityManager.mobTypeAt 选 pig/cow/sheep 贴图。实心无 alpha → 走不透明 PrincipledMaterial（无需
        //   alphaCutoff / opacity 契约）。受击红闪（hurtFlashAt>0）由 baseColor 红覆盖贴图（mobPigRed 等）。
        Texture { id: mobPigTex;   source: "qrc:/textures/mob_pig.png";   generateMipmaps: false }
        Texture { id: mobCowTex;   source: "qrc:/textures/mob_cow.png";   generateMipmaps: false }
        Texture { id: mobSheepTex; source: "qrc:/textures/mob_sheep.png"; generateMipmaps: false }
        // t282 蹒跩者（Shambler；机制等价 MC 1.0 僵尸，§9 改名 + 原创贴图）：暗绿腐肉底 + 霉斑 + 腐痕 +
        //   破布残片 + 缝合痕（build_mob.py 程序生成原创像素图，§9a 区隔不照搬 MC 皮肤）。MobModel 人形几何
        //   （躯干/头/双臂前伸/双腿）每面铺整张贴图 [0,1]×[0,1]（同猪牛羊全脸 UV）；实心无 alpha → 不透明材质。
        Texture { id: mobShamblerTex; source: "qrc:/textures/mob_shambler.png"; generateMipmaps: false }

        // t218 火把手持/掉落贴图：火把在世界内是异形（torchHost 木柄+火焰小立方，非 1×1×1 立方体），
        //   但手持/掉落旧路径走 BlockCube（6 面立方贴图集 tile 17）→ 即便 alphaCutoff 丢弃透明底，肉眼仍是
        //   「贴了火把贴图的小立方」而非「细火把」。改走 BillboardQuad 单面平图标 + icon_torch.png（tools/
        //   build_cube_icons.py 的 render_flat_2d 产物：64×64 透明底只含火把本体，非 MC 资产）。三处手持/掉落
        //   路径共用本贴图（单一 source，lessons-learned「同一份带 alpha 贴图被多路径共用须各自履行契约」）。
        Texture { id: torchIconTex; source: "qrc:/textures/icon_torch.png"; generateMipmaps: false }

        // t219 木板衍生方块（异形段 [FirstPartial,LastPartial]：台阶/楼梯/栅栏/压力板/门/活板门）手持/掉落贴图。
        //   这些方块在世界内是异形（PartialBlockGeometry 按 (id,state) 生成），但手持/掉落旧路径走 BlockCube
        //   → BlockCube.tileIndex 对异形段各方块全返回 planks(8)（异形各面同贴图=木板，t134 设计）→ 渲染成
        //   「满格木板立方」肉眼与木板无法区分（楼梯/门/栅栏全都长得像一块木板）。改走 BillboardQuad 单面平
        //   图标 + 各自 dimetric 立体图标 icon_wood_*.png（build_cube_icons.py render_partial_3d 产物：64×64
        //   透明底 + 按实际形状投影的木板材质立体图，与 hotbar/创造调色板槽位图标同源）。source 动态绑定
        //   iconSourceForBlock(selectedBlock) → 选不同异形自动换图标（单一 Texture 实例覆盖全部 6 类异形）。
        //   两处手持路径（第一人称 viewModelHand + 第三人称 playerModel）共享本贴图（同读 selectedBlock）；
        //   掉落实体另用 inline Texture（读 per-entity entId）。alpha 契约沿用 torch billboard（alphaCutoff:0.5
        //   + opacity:0.99：透明底不丢弃会被当不透明黑 → 图标坍黑块）。
        Texture {
            id: partialIconTex
            source: hotbarVM.iconSourceForBlock(player.selectedBlock)
            generateMipmaps: false
        }

        // 挖掘裂纹 6 阶贴图（t34）：tools/build_cracks.py 程序生成（透明底 + 黑裂纹，§9a 自绘）。
        // 裂纹叠层 Model 据 player.miningStage（0..5）取对应 Texture 作 baseColorMap。
        // 索引即 stage：0=0% / 1=20% / 2=40% / 3=60% / 4=80% / 5=100%（progress 满 = 破）。
        Texture { id: crack0; source: "qrc:/textures/crack_0.png"; generateMipmaps: false }
        Texture { id: crack1; source: "qrc:/textures/crack_1.png"; generateMipmaps: false }
        Texture { id: crack2; source: "qrc:/textures/crack_2.png"; generateMipmaps: false }
        Texture { id: crack3; source: "qrc:/textures/crack_3.png"; generateMipmaps: false }
        Texture { id: crack4; source: "qrc:/textures/crack_4.png"; generateMipmaps: false }
        Texture { id: crack5; source: "qrc:/textures/crack_5.png"; generateMipmaps: false }

        // 每 chunk culled mesh（t03 / t276 大世界动态化）：地形段 + 水段各一个 ChunkGeometry，由 chunkAnchor.
        //   onCompleted 按 theWorld.chunksX×chunksZ 用 Component.createObject 动态生成（替代旧 50 个显式 Model）。
        //   lessons-learned 已验证路径（t16 Loader / t170 torchHost）：createObject 第一参 = chunkAnchor（已在
        //   View3D 场景内）→ Model 被领养进 3D 场景图渲染（非孤儿，parent=QQuick3DNode*）。固定网格下 chunk 数
        //   恒定 → 仅一次（chunksBuilt 守卫）；切世界只换 seed 不换尺寸 → 无需销毁重建。可配网格
        //   （worldChunksPerSide）下显式声明不现实（10×10=200 Model），动态创建使网格尺寸单一权威 → 改一处全幅
        //   扩 / 缩。不用 Repeater（t03 验证：Repeater 的 3D Model delegate 触发「Delegate must not be of Item
        //   type」告警 + 孤儿不渲染之虞）。流式加载推迟 Phase 2，本工程仍为整片固定网格全驻留。
        //   跨 chunk 边界面剔除经 world.blockAt 路由（相邻实体共边面剔除无夹层 / 一侧空气画出 / 越界=空气）；
        //   dirty 驱动（setBlock 标目标 + 边界邻接脏；worldChanged → onWorldChanged 仅脏 chunk 重建）不变。
        //   分层（PLAN §2）：本段属 Renderer 呈现层，只读 World（blockAt/stateAt/光场），不写栅格。
        Node {
            id: chunkAnchor   // chunk Model 领养锚点（createObject 第一参 → Model 进 3D 场景图渲染）
            Component.onCompleted: {
                // t276：按 theWorld.chunksX×chunksZ 动态生成地形 + 水两段 Model（createObject + reparent，
                //   t16/t170 已验证）。固定网格 → 仅一次（chunksBuilt 守卫）。terrainGeos 供 F3 顶点汇总。
                if (window.chunksBuilt) return
                window.chunksBuilt = true
                const nx = theWorld.chunksX, nz = theWorld.chunksZ
                const geos = [], objs = []
                for (let cz = 0; cz < nz; ++cz) {
                    for (let cx = 0; cx < nx; ++cx) {
                        const t = terrainChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })
                        objs.push(t); geos.push(t.geometry)
                        objs.push(waterChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz }))
                        objs.push(crossChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })) // t326 cutout 段（草丛/作物/树苗）
                    }
                }
                window.terrainGeos = geos
                window.chunkObjects = objs
                window.recomputeMeshStats()   // 取初值（createObject 时各段已 buildMesh；后续 meshRebuilt 增量刷新）
                console.info("[t276] built", objs.length, "chunk Models (" + nx + "x" + nz + "=" + (nx*nz) + " chunks)")
            }
        }

        // 地形段 chunk Model 模板（culled mesh，非水方块）。cx/cz 由 createObject initial properties 注入；
        //   内层 ChunkGeometry 经 terrainModel（本组件实例根 id）读 chunkCX/CZ —— 与 t170 torchDelegate 子引用
        //   torchGlow 同一 Component 内根-id 引用模式（已验证）。
        Component {
            id: terrainChunkComp
            Model {
                id: terrainModel
                property int chunkCX: 0
                property int chunkCZ: 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    world: theWorld
                    cx: terrainModel.chunkCX
                    cz: terrainModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; alphaCutoff: 0.5; baseColor: terrainLight(worldClock.skyLight) }
            }
        }

        // 水段 chunk Model 模板（t148：waterOnly 只网格化 Water，opacity 0.7 半透；t223 waterAnimPhase flipbook）。
        //   摆位与地形段同（chunk 世界起点）；透明物体由 QtQuick3D 渲染队列自动排在不透明地形之后。
        Component {
            id: waterChunkComp
            Model {
                id: waterModel
                property int chunkCX: 0
                property int chunkCZ: 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    world: theWorld
                    cx: waterModel.chunkCX
                    cz: waterModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    waterOnly: true
                    waterAnimPhase: window.waterAnimPhase
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; opacity: 0.7; baseColor: terrainLight(worldClock.skyLight) }
            }
        }

        // t326 cross cutout 段 chunk Model 模板：cross 广告牌方块（草丛 / 小麦作物 / 树苗）的独立半透段。
        //   cross 贴图带 alpha 透明底（草叶 / 树苗本体 alpha=255、底 alpha=0），须 alpha-test cutout 才显透明
        //   间隙（否则显成两片实心板挡视线）。PrincipledMaterial 在本 D3D11 后端 **alphaCutoff 仅在 opacity<1**
        //   （透明通道）下生效（见 crack 材质 B1 注释 / lessons-learned alpha 契约条）—— 地形段材质 opacity=1 →
        //   alpha 被忽略 → 透明底当不透明显成实心板（用户「草丛挡住视线」根因）。本段配 opacity:0.99 +
        //   alphaCutoff:0.5（沿用 torch / crack / MaterialIcon alpha-test 契约）。terrain / water 段不能整体
        //   降 opacity（全地形半透 + 透明通道无深度写 = z-fight），故拆独立段（机制等价 waterOnly 的透明分流）。
        //   cutoutOnly:true 让 ChunkGeometry 仅网格化 cross（PASS 1 pushCross）、跳过立方面（PASS 2）。
        //   几何顶点为 chunk 局部坐标、position 同 terrain/water 段；顶点色光照（terrainLight + vertexColors）沿用同管线。
        Component {
            id: crossChunkComp
            Model {
                id: crossModel
                property int chunkCX: 0
                property int chunkCZ: 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    world: theWorld
                    cx: crossModel.chunkCX
                    cz: crossModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    cutoutOnly: true   // t326：仅 cross 方块（草丛/作物/树苗）→ 半透 cutout 材质 cutout 透明底
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; alphaCutoff: 0.5; opacity: 0.99; baseColor: terrainLight(worldClock.skyLight) }
            }
        }

        // t277 F3+G 区块边界显示（机制等价 MC F3+G chunk boundary display）：ChunkGridLines 按
        //   世界尺寸 + 16 格 chunk 边长画区块边界线框叠层（每个 chunk 边界交点处纵线 y=0..height +
        //   顶/底水平连线 = 完整格线框，从任意角度可见）。红色 NoLighting 线（lessons-learned：所有
        //   可见 3D Model 必须 NoLighting，否则 lit 材质不出像素）。Model 摆 position 0,0,0 —— 几何顶点
        //   已是世界坐标（与 chunk Model 同坐标系：chunk 摆 (cx*16,0,cz*16)，边界恰落在 x/z=16n）。
        //   仅 playing + showChunkBounds（F3+G toggle）时显；走正常深度测试（被前景地形遮挡，高出地表
        //   部分始终可见 = 既不喧宾夺主、又能看清边界）。
        //   分层（PLAN §2）：呈现层只读 worldChunksPerSide / theWorld.height（世界尺寸单一权威），不写栅格。
        Model {
            visible: window.appState === "playing" && window.showChunkBounds
            position: Qt.vector3d(0, 0, 0)
            geometry: ChunkGridLines {
                worldWidth: window.worldChunksPerSide * 16
                worldDepth: window.worldChunksPerSide * 16
                worldHeight: theWorld.height
                chunkSize: 16
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                // t316 亮饱和黄（旧 #ff3030 红色亮度低、在绿/棕地形上过细难辨；黄色相对亮度 ~3x，
                //   机制等价 MC F3+G chunk border 黄色亮线；配 ChunkGridLines 加粗几何）。
                baseColor: "#FFD500"
            }
        }

        // 选中框（射线选体 t04 / t52 / t146 / t216）：单一 SelectionWireBoxes 模型按
        //   BlockRegistry::selectionAABBs(hitId,hitState) 画命中方块的 sub-AABB 12 棱（Lines，**纯 AABB
        //   棱、无对角线 / 无叉叉**）—— 完整方块 selectionAABBs={0,0,0,1,1,1} 还原 ±0.5 立方框（与旧
        //   WireCube 同观感），不完整方块贴合实际形状（下半砖下半盒棱 / 栅栏中心立柱 / 楼梯下步+背墙
        //   双盒棱 / 薄板等），**非整立方黑边**。火把（ShapeNone → selectionAABBs 空）走独立
        //   selectionBoxTorch（WireCube 木柄定向，见下方）。
        //   t216 统一：旧 selectionBox(WireCube 全格) + selectionBoxPartial(SelectionWireBoxes) 双模型 +
        //   isPartial 魔法数 [15,20] 路由合并为单一 SelectionWireBoxes 模型 —— 一切方块经 selectionAABBs
        //   单一权威取形状，消除「完整 vs 不完整」路由分叉与魔法数漂移风险（段后追加整立方如 Chest=22 曾因
        //   单边 `>=15` 误判为异形）；并强制 qmlcache 重编，修「整立方黑边 / 对角叉叉」类 stale-binary 视觉
        //   回归（lessons-learned t41：qmlcachegen mtime 停在旧布局 → 增量构建漏重编 → 运行期回退旧 QML）。
        //   放大系数 1.005（t76 收紧）：几何 ±0.5 居中 × scale 1.005 = ±0.5025，紧贴方块边、邻块不吞噬远侧
        //   棱（与 CrackBox 叠层同防 z-fight 量级）。几何对称、与朝向无关 → eulerRotation=0（不似 WireSquare
        //   需按命中面法线旋转）。未命中 / 暂停（未捕获）时 hasHit=false → 隐藏。
        //   分层（PLAN §2）：呈现层只读 World（blockAt/stateAt）+ Core BlockRegistry 单一权威取形状，不写栅格。
        Model {
            id: selectionBox
            // 火把（ShapeNone → selectionAABBs 空）走 selectionBoxTorch；其余一切方块（完整 + 不完整）
            //   走本 SelectionWireBoxes（selectionAABBs 给形状：完整→全格立方框、不完整→贴合 sub-AABB）。
            visible: player.hasHit && !isTorch
            property int worldRev: 0   // 编辑改 state（活版门开合 / 楼梯朝向 / 双半砖合并位）→ worldChanged ++ 触发 hitState 重算
            Connections { target: theWorld; function onWorldChanged() { ++selectionBox.worldRev } }
            readonly property int hitId: player.hasHit ? theWorld.blockAt(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z) : 0
            readonly property bool isTorch: hitId === 13   // 13 = BlockRegistry::Torch；火把 ShapeNone → 走 selectionBoxTorch
            // 命中方块的 state（异形方块朝向 / 开合 / 半位）：供 SelectionWireBoxes 几何重建用（stairs 朝向 /
            //   slab 上下半 / door 开合 / trapdoor 开合 等）。读 worldRev 使 worldChanged 后重算 → 选中框棱随之更新。
            readonly property int hitState: {
                selectionBox.worldRev;
                return player.hasHit ? theWorld.stateAt(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z) : 0
            }
            position: Qt.vector3d(player.hitBlock.x + 0.5, player.hitBlock.y + 0.5, player.hitBlock.z + 0.5)
            scale: Qt.vector3d(1.005, 1.005, 1.005)
            eulerRotation: Qt.vector3d(0, 0, 0)
            geometry: SelectionWireBoxes {
                blockId: selectionBox.hitId
                state: selectionBox.hitState
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#101010"
            }
        }

        // 火把选中框（t126）：火把 ShapeNone → selectionAABBs 空（无 sub-AABB 棱可画）→ 走独立 WireCube
        //   木柄定向框（scale 0.12/0.6/0.12 + position/euler 镜像 torchHost 木柄 Model，同一份 computeTorchOrient
        //   + torchHandleWorldPos/Scale/Euler），故选中框贴合火把外缘、非全格。13 = BlockRegistry::Torch
        //   （魔法数与下方 onWorldChanged 火把校验同源）。
        //   分层（PLAN §2）：呈现层只读 World（blockAt/isCollidable）+ torchPositions，不写栅格。
        Model {
            id: selectionBoxTorch
            visible: player.hasHit && selectionBox.isTorch
            property int worldRev: 0   // 邻居破/放会改火把有效朝向 → worldChanged ++ 触发 torchOrient 重算
            Connections { target: theWorld; function onWorldChanged() { ++selectionBoxTorch.worldRev } }
            // Q_INVOKABLE（isCollidable/blockAt）无自带 QML 绑定依赖 → 显式读 worldRev，使 worldChanged 后
            //   本绑定重算（邻居破/放会改火把有效朝向）。
            readonly property string torchOrient: {
                selectionBoxTorch.worldRev;
                return computeTorchOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z,
                                          findTorchPrefOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z))
            }
            position: torchHandleWorldPos(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z, torchOrient)
            scale: torchHandleScale(torchOrient)
            eulerRotation: torchHandleEuler(torchOrient)
            geometry: WireCube {}
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#101010"
            }
        }

        // 挖掘裂纹叠层（t34）：仅生存持续挖掘时显（miningStage >= 0；创造瞬破不进入累积态，
        // stage=-1 → 隐藏）。叠在目标方块上（position = miningBlock + 0.5 中心），略放大 1.005
        // 防 z-fight；baseColorMap 按 miningStage 切 6 阶裂纹 PNG（0/20/40/60/80/100%）。
        // 分层（PLAN §2）：呈现层只读 player.miningStage（Game 层算），不反向写进度；裂纹贴图
        // 自绘原创（tools/build_cracks.py 程序生成，§9 override (a)）。
        // 材质：NoLighting + hasTransparency → 透明底（alpha=0）不遮方块本色，仅黑裂纹显示。
        // cullMode 默认 Back（仅外法线面可见）→ 玩家看得到的几面才显裂纹（背向面被剔除）。
        Model {
            visible: player.miningStage >= 0
            position: Qt.vector3d(player.miningBlock.x + 0.5,
                                  player.miningBlock.y + 0.5,
                                  player.miningBlock.z + 0.5)
            scale: Qt.vector3d(1.005, 1.005, 1.005) // 微放大防与方块面 z-fight
            geometry: CrackBox {}
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                // alphaCutoff：裂纹贴图含 alpha（透明底 alpha=0 + 半透黑裂纹 alpha≈220）。设 0.5 启用
                // alpha-test：透明底像素被丢弃（不遮方块本色）、裂纹像素保留为不透明黑。比「靠 opacity=1 时
                // PrincipledMaterial 自动 blend」可靠——后者透明底可能被当不透明渲染成黑块遮住整个方块（t34
                // correctness 报告标记的风险）。MC 风格硬边裂纹，走 opaque 通道无 blend 排序问题。
                alphaCutoff: 0.5
                opacity: 0.99   // B1 修复：opacity<1 强制走透明通道 → 贴图 alpha 被尊重（透明底 alpha=0 不渲染、仅裂纹显）。
                                 // 旧 opacity=1 时纹理 alpha 被忽略，透明底（RGB 0,0,0）被当不透明渲染 → 整个方块变黑。
                // 6 阶裂纹贴图按 miningStage（0..5）取（id 引用全文件可见；数组内联构造）。
                // miningStage=-1（无累积）时 Model 已 visible=false，此处仍需合法索引防 undefined →
                // Math.max(0, ...) 钳到 0（不可见时取哪张贴图无所谓，避免 WRN 噪音）。
                baseColorMap: [crack0, crack1, crack2, crack3, crack4, crack5][Math.max(0, player.miningStage)]
            }
        }

        // 玩家 3D 模型（t28）：方块化人形（头/躯干/双臂/双腿），跟随玩家脚底位置 + yaw 朝向。
        // 纯色 PrincipledMaterial 自绘原创（肤色/上衣/裤色，对齐 SurvivalInventory 预览配色），不拷贝任何
        // MC 皮肤/玩家贴图（§9 override (a)）。模型属呈现层、纯装饰——碰撞仍走 PlayerController 的 AABB，
        // 模型不进 World/Physics（PLAN §2 分层）。
        // 显隐：仅第三人称可见（第一人称看不到自己身体，visible 绑 cameraMode）。不透明度绑模式：观察者
        // = 半透幽灵 0.35（opacity<1 时 PrincipledMaterial 自动走透明混合），创造/生存=不透明 1.0。
        // 身体（躯干/四肢）只随水平 yaw 转、不随 pitch 倾；抬头只动相机 + 头部 Node（t66），不动身体躯干。
        Node {
            id: playerModel
            visible: player.cameraMode !== PlayerController.FirstPerson
            position: player.feetPosition
            eulerRotation: Qt.vector3d(0, player.yaw, 0)

            // 半透幽灵态（各 body Part 的材质读此属性）：观察者半透 0.35，其余不透明。
            readonly property real bodyOpacity: player.mode === PlayerController.Spectator ? 0.35 : 1.0

            // 受伤变红混合系数（t67，替换 t51 全屏 damageOverlay 红闪）：PlayerState.damaged 触发 hurtAnim
            //   把 hurt 从 1.0 淡到 0（~0.4s）。各身体部件 baseColor 经 hurtTint(hurt,...) lerp 向纯红 →
            //   「模型本身变红」（非全屏叠层）。0 = 正常肤色/衣色。连击受伤：start() 重启动画 → 重新变红。
            //   分层（PLAN §2）：hurt 是纯呈现层态（QML 动画），由 Game 层语义事件 damaged 驱动；呈现层
            //   只消费 PlayerState 信号，绝不反向写数值（同 fallDamageTaken→takeDamage）。
            property real hurt: 0.0
            // 把基础 RGB（0..1 三通道）按 hurt 系数 lerp 向纯红 (1,0,0)。hurt 作参数显式传入 → 绑定依赖
            //   挂在 hurt 上（与 dayNightColor(worldClock.skyLight) 同模式：变化的 NOTIFY 属性作函数参数）。
            //   hurt=0 返回原色、hurt=1 返回纯红。纯函数（不存状态）。眼睛（白/瞳）不调此函数 → 不变红。
            function hurtTint(hurt, r, g, b) {
                return Qt.rgba(r + (1.0 - r) * hurt, g * (1.0 - hurt), b * (1.0 - hurt), 1.0)
            }

            // 行走动画混合系数（t45）：moveSpeed>0.1 → 1（四肢摆动），否则 0（中性位）。QML 据此缩放
            //   腿/臂的 sin() 摆幅 → 静止时四肢归零（不再生硬平移）。0/1 二值切换（spec：静止归零）。
            readonly property real walkBlend: player.moveSpeed > 0.1 ? 1.0 : 0.0
            // t51 状态驱动摆动幅度：疾跑 ×1.4（更夸张的大步）、蹲下 ×0.5（拘谨小步）、走 ×1.0。
            //   频率由 moveSpeed 自带（speedMul 已乘入 → walkPhase 推进速率随之变）；幅度在此缩放四肢摆角。
            //   接 t45 动画：spec「状态驱动模型动画频率/幅度」。
            readonly property real swingAmp: player.moveState === PlayerController.Sprint ? 1.4
                                           : player.moveState === PlayerController.Crouch ? 0.5
                                           : 1.0
            // 挖掘挥臂混合系数（t45）：onSwingArm 触发 NumberAnimation 0→1→0（~240ms）。>0 时双臂
            //   前抬（覆盖行走摆臂），呈现「挖掘挥动」。0 = 无挖掘（行走摆臂正常）。
            property real mineBlend: 0.0

            // 蹲下姿态（t65 下沉+腿弯；t71 改为上半身绕髋前倾鞠躬）：Shift 蹲下时上半身绕髋 pitch 前倾
            //   （鞠躬），腿弯（大腿前抬 + 膝盖回折）使髋下沉、脚仍贴地——取代 t65「上半身逐件平移下沉」。
            //   crouchBlend = 1（Crouch）/ 0（Walk/Sprint）；据此驱动上半身鞠躬 + 腿膝盖弯曲。
            //   crouchDrop ≈ 0.18：髋枢轴下沉量（upperBody 枢轴与双腿枢轴共用 → 躯干底贴髋无断身缝隙；
            //     腿加膝盖关节弯折后脚仍贴近地面，不陷太深）。
            //   crouchBow ≈ 35°：上半身绕髋前倾幅值（t71 新增）；鞠躬方向 = 躯干顶（+Y）旋向玩家前方（-Z），
            //     按右手法则这是 -x 旋转（+x 会把 +Y 旋向 +Z=身后=后仰）→ upperBody 用 -crouchBow，见其注释。
            //   腿分大腿 + 小腿两段 + 膝盖 Node：站立膝盖 0° → 腿直立（与重组前一致，无回归）；
            //     蹲下大腿前抬 crouchThigh、小腿在膝处回折 crouchKnee（=−crouchThigh，使小腿保持竖直、
            //     脚前移落地）→ 大腿近水平 / 小腿竖直的蹲姿轮廓（机制等价 MC 蹲）。
            //   ⚠️ crouchKnee/crouchThigh/crouchBow 必须乘 crouchBlend（曾出过 crouchKnee 自引用 crouchKnee
            //     的递归笔误 → 蹲下膝盖不弯；此处统一以 crouchBlend 为唯一蹲态开关，杜绝自引用）。
            //   Walk/Sprint（crouchBlend=0）所有蹲量归零 → 上半身直立、腿直立（无回归）。分层（PLAN §2）：
            //   姿态纯呈现层（QML 据 moveState 算），只读 Game 层 moveState，绝不反向写（同 swingAmp 模式）。
            readonly property real crouchBlend: player.moveState === PlayerController.Crouch ? 1.0 : 0.0
            readonly property real crouchDrop: 0.18 * playerModel.crouchBlend
            readonly property real crouchBow: 35.0 * playerModel.crouchBlend      // t71：上半身绕髋前倾鞠躬幅值（度；前倾= -x，见 upperBody 注释）
            readonly property real crouchThigh: 60.0 * playerModel.crouchBlend   // 蹲时大腿前抬（度；+x = 腿尖前摆 = -Z）
            readonly property real crouchKnee: -60.0 * playerModel.crouchBlend     // 蹲时膝盖回折（度；= −crouchThigh → 小腿保持竖直、脚前移落地）

            // [t31] 诊断：确认本 Node 已加载、parent=场景节点（非 null 孤儿）、feetPosition 合法、visible 状态。
            // 打印到 voxelsandbox.log。若运行后日志无此行 → Main.qml 未进二进制（stale build）。
            Component.onCompleted: console.info("[t31] playerModel UP  parent=" + playerModel.parent
                + "  feet=" + player.feetPosition + "  vis=" + visible
                + "  cam=" + player.cameraMode + "  mode=" + player.mode)

            // 上半身枢轴 Node（t71）：包 head/躯干/双臂，枢轴在髋（y = 0.6 - crouchDrop，与双腿枢轴同高 →
            //   躯干底贴髋、无断身缝隙）。蹲下绕髋 pitch 前倾鞠躬，取代 t65「上半身逐件 position.y − crouchDrop
            //   平移下沉」——头/躯干/臂在此用「相对髋」的固定本地坐标（站立时世界坐标与重组前完全一致，无
            //   回归），鞠躬由本 Node 旋转统一驱动。
            //   旋转符号（关键，同 t66 pitch 一族易错）：鞠躬 = 躯干顶（+Y）旋向玩家前方（-Z）。按右手法则绕 +x
            //     转 +θ 把 +Y 旋向 +Z（= 玩家身后 = 后仰），故前倾鞠躬须用 -θ：eulerRotation.x = -crouchBow。
            //     dev-spec 原稿写「+crouchBow」是符号直觉误（误把「+x=前摆」（仅对 -Y 下垂的手臂成立）套用到
            //     +Y 上挺的躯干）——此处据几何修正为 -，否则蹲下会变成后仰看天。
            //   Walk/Sprint（crouchBlend=0 → crouchBow=0）upperBody 归零直立（无回归）。分层（PLAN §2）：纯呈现
            //   层姿态，只读 moveState，不反向写（同 crouchBlend 模式）。
            Node {
                id: upperBody
                position: Qt.vector3d(0, 0.6 - playerModel.crouchDrop, 0)   // 髋枢：与双腿枢轴同高，蹲下随髋下沉
                eulerRotation: Qt.vector3d(-playerModel.crouchBow, 0, 0)     // t71：前倾鞠躬（-x；+x 会后仰，见上注）

                // 头部枢轴 Node（t66）：头 + 双眼打包成一个子 Node，绕「颈部」俯仰，让第三人称头部跟随视线 pitch。
                //   t71：迁入 upperBody；本地颈枢 y=0.7（髋枢 0.6 + 0.7 = 世界 1.3，与重组前一致），鞠躬时随上半身
                //     绕髋前倾（头世界旋转 = Ry(yaw)·Rx(−crouchBow)·Rx(+pitch)，鞠躬 ∘ 视线俯仰）。
                //   旋转符号：与相机一致用 +player.pitch（相机 eulerRotation.x = +pitch；pollMouse 中鼠标上推 →
                //     m_pitch 增大 → 抬头看上方，故 pitch>0 = 抬头）。头部作为身体(yaw)的子节点，世界旋转与相机
                //     同向 → 头朝视线方向。dev-spec 原稿写「-pitch」是符号笔误，此处据相机约定修正为 +pitch。
                //   clamp ±60°：player.pitch 全程 ±89°，头部限 ±60° 即可表达俯仰且不至「折颈」过倾穿身。
                //   颈枢（非头心）：头绕脖子转（解剖正确），低头下巴前伸 / 抬头后仰，比绕头心转自然；极端低头
                //     时下巴与胸口轻微相贴（与真人低头一致，非穿模瑕疵）。
                //   分层（PLAN §2）：pitch 是 Game 层 Q_PROPERTY（playercontroller.h 已暴露 + pitchChanged），
                //     头部俯仰纯呈现层（QML 绑定只读，绝不反向写 player.pitch），同 yaw/crouchBlend 模式。
                Node {
                    id: headNode
                    position: Qt.vector3d(0, 0.7, 0)   // 颈枢（相对 upperBody）：头底/躯干顶；世界 = 髋枢+0.7
                    eulerRotation: Qt.vector3d(Math.max(-60, Math.min(60, player.pitch)), 0, 0)

                    // 头（≈0.5³，肤色）。相对颈枢：头心在颈上方 0.25（世界 y=1.55）。pitch=0 时与重组前完全一致。
                    Model {
                        geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 改自定义 UnitCube 几何（同地形/线框的已验证路径）
                        position: Qt.vector3d(0, 0.25, 0)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.792, 0.643, 0.447); opacity: playerModel.bodyOpacity }
                    }
                    // 眼睛（t39 / t52 贴脸修正）：头部正面（朝 -Z = 玩家朝向；t04 约定 yaw=0 时前向 = (0,0,-1)）
                    // 的两个对称小方块，使第三人称能看到「脸」。白眼底 (#e8e8e8) + 深色瞳 (#1a1a1a) 两层，原创纯色
                    // （§9 override (a)，无 MC 皮肤）。作 headNode 子节点 → 随头部俯仰（眼贴头表面 → 跟随看视线方向）。
                    //
                    // t52：贴脸 z（头半厚 0.25，头前面 z=-0.25）：白眼底 z=-0.25、瞳 z=-0.26（略凸出 0.01，
                    //   在白眼底前；z 须 ≤-0.25 才不被不透明头遮挡）。|z|≈头半径 0.25 → 贴头表面而非外飘。
                    // t66：眼从 playerModel 直系子迁入 headNode（颈枢本地坐标）；眼相对颈 y=0.32（世界 1.62-1.3），
                    //   pitch=0 时绝对坐标与重组前一致（无回归）；贴脸 z 不变。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.1, 0.32, -0.25)
                        scale: Qt.vector3d(0.1, 0.12, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8"; opacity: playerModel.bodyOpacity }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.1, 0.32, -0.25)
                        scale: Qt.vector3d(0.1, 0.12, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8"; opacity: playerModel.bodyOpacity }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.1, 0.32, -0.26)
                        scale: Qt.vector3d(0.05, 0.06, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a"; opacity: playerModel.bodyOpacity }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.1, 0.32, -0.26)
                        scale: Qt.vector3d(0.05, 0.06, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a"; opacity: playerModel.bodyOpacity }
                    }
                }
                // 躯干（上衣色；y 0.6→1.3，宽 0.5 深 0.3）。t71：迁入 upperBody；本地中心 y=0.35（髋枢+0.35=世界 0.95），
                //   蹲下随上半身绕髋前倾鞠躬（非 t65 逐件下沉）。
                Model {
                    geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 改自定义 UnitCube 几何（同地形/线框的已验证路径）
                    position: Qt.vector3d(0, 0.35, 0)
                    scale: Qt.vector3d(0.5, 0.7, 0.3)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.416, 0.604); opacity: playerModel.bodyOpacity }
                }
                // 左臂枢轴（t45 走 / t52 仅右手挖）：枢轴位于左肩（相对 upperBody：-0.375, 0.7, 0；世界 -0.375, 1.3, 0），
                //   袖+手作子节点本地 -Y 偏移（袖中心 -0.25、手中心 -0.6）→ 绕肩旋转时手在弧线末端摆动（自然关节运动）。
                //   静止时几何中心与重组前完全一致（袖 y=1.05、手 y=0.7），总高不变。
                //   摆动：行走时左臂与左腿反相（左腿前=−sin → 左臂后=+sin；对侧臂腿同相）。
                //   t52：挖掘/放置只动右手——左臂仅行走摆动，不读 mineBlend（旧版双臂同挖，用户反馈「双手都动」）。
                //   +eulerRotation.x = 臂尖前摆（-Y→-Z，朝玩家前向）。t71：随 upperBody 鞠躬前倾。
                Node {
                    id: leftArmPivot
                    position: Qt.vector3d(-0.375, 0.7, 0)   // 左肩（相对 upperBody）；世界 = 髋枢+0.7
                    eulerRotation: {
                        // 行走摆臂：与右腿同相（+sin；右腿前则左臂前）。静止 walkBlend=0 → 归零。
                        // t51：摆幅 ×swingAmp（疾跑夸张 / 蹲下拘谨）。
                        const walk = Math.sin(player.walkPhase) * 22 * playerModel.walkBlend * playerModel.swingAmp
                        return Qt.vector3d(walk, 0, 0)
                    }
                    // 袖段（上衣色 #3a6a9a；y 0.8→1.3 = 肩下 0.25..0.5）
                    Model {
                        geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 自定义 UnitCube（同地形/线框已验证路径）
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.25, 0.5, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.416, 0.604); opacity: playerModel.bodyOpacity }
                    }
                    // 手（肤色 #caa472；臂末端 y 0.6→0.8 = 肩下 0.5..0.7）
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.6, 0)
                        scale: Qt.vector3d(0.25, 0.2, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.792, 0.643, 0.447); opacity: playerModel.bodyOpacity }
                    }
                }
                // 右臂枢轴（t45 / t52 持方块）：与左臂对称（右肩相对 upperBody：0.375, 0.7, 0），行走与左腿同相（−sin）。
                //   t52：挖掘/放置只动右手——仅右臂读 mineBlend（挖掘挥臂前抬 70°）；左臂已不读（见上）。t71：随 upperBody 鞠躬。
                Node {
                    id: rightArmPivot
                    position: Qt.vector3d(0.375, 0.7, 0)   // 右肩（相对 upperBody）；世界 = 髋枢+0.7
                    eulerRotation: {
                        // t51：摆幅 ×swingAmp（与左臂对称；疾跑夸张 / 蹲下拘谨）。
                        const walk = -Math.sin(player.walkPhase) * 22 * playerModel.walkBlend * playerModel.swingAmp
                        const x = walk * (1 - playerModel.mineBlend) + 70 * playerModel.mineBlend
                        return Qt.vector3d(x, 0, 0)
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.25, 0.5, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.416, 0.604); opacity: playerModel.bodyOpacity }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.6, 0)
                        scale: Qt.vector3d(0.25, 0.2, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.792, 0.643, 0.447); opacity: playerModel.bodyOpacity }
                    }
                    // 手持方块（t52）：持有方块（selectedBlock≠0）时，第三人称右手上显该方块小图标。
                    //   作 rightArmPivot 子节点 → 随右臂行走 / 挖掘挥臂同步运动（块在手中，自然跟随）。
                    //   BlockCube + 共享图集 → per-face 贴图（草顶 / 草侧…），复用地形贴图（零 MC 资产）。
                    //   仅第三人称 + 非观察者显（第一人称见 viewModelHand 的手持；观察者无动作不持物）。
                    //   t169 火把黑底修复：同第一人称 viewModelHand / 掉落物路径 —— 火把 tile 透明底需
                    //   alphaCutoff 0.5 丢弃透明像素，否则材质把透明底当不透明 → 渲成黑色立方体（用户实测
                    //   「手持火把黑方块」）。仅火把（id 13）启用；其余方块贴图无 alpha，alphaCutoff=0。
                    Model {
                        visible: player.selectedBlock !== 0 && player.selectedBlock !== 13 && !hotbarVM.isPartialBlock(player.selectedBlock) && player.mode !== PlayerController.Spectator
                        geometry: BlockCube { blockId: player.selectedBlock }
                        position: Qt.vector3d(0, -0.55, -0.30)   // t72：移到手前方（手心前缘 z≈-0.125 前），不嵌进手里
                        scale: Qt.vector3d(0.22, 0.22, 0.22)
                        eulerRotation: Qt.vector3d(-12, 42, 0)   // t72：绕 Y ~42° 倾斜 + 微 pitch，像 MC 手持姿态
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            opacity: playerModel.bodyOpacity
                            alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）走下方 billboard 分支
                        }
                    }
                    // t219 手持木板衍生方块（第三人称）：异形段（台阶/楼梯/栅栏/压力板/门/活板门）非整立方 →
                    //   billboard 平图标（dimetric 立体图标），非满格木板立方。作 rightArmPivot 子节点随臂行走/
                    //   挖掘挥动同步。BillboardQuad +Z 法线默认 backface 剔除 → 第三人称-前（相机在玩家前）见背面
                    //   被剔 → 图标消失；故 cullMode:NoCulling 双面渲染（背面镜像图标，异形近对称无明显差异）→
                    //   三相机模式都可见。scale 0.22（同手持方块立方，平图标等大）；opacity 跟 bodyOpacity（观察者
                    //   半透一致）；alphaCutoff:0.5 沿用透明底 alpha-test 契约。partialIconTex 共享第一人称同一份。
                    Model {
                        visible: hotbarVM.isPartialBlock(player.selectedBlock) && player.mode !== PlayerController.Spectator
                        geometry: BillboardQuad {}
                        position: Qt.vector3d(0, -0.55, -0.30)
                        scale: Qt.vector3d(0.22, 0.22, 0.22)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            cullMode: Material.NoCulling   // 双面（第三人称-前见背面；异形图标近对称）
                            alphaCutoff: 0.5
                            opacity: 0.99   // visible 已排除 Spectator → bodyOpacity 恒 1.0；<1 尊重贴图 alpha（透明底不渲染）
                            baseColorMap: partialIconTex
                        }
                    }
                    // t218/t260 手持火把（第三人称）：火把非立方 → billboard 平图标（细立柱）。作 rightArmPivot 子节点
                    //   随臂行走/挖掘挥动同步。BillboardQuad +Z 法线默认 backface 剔除 → 第三人称-前（相机在玩家
                    //   前）会看到背面被剔 → 火把消失；故 cullMode:Material.NoCulling 双面渲染，背面（镜像火把，
                    //   火把近对称无明显差异）也显 → 三相机模式都可见。静止时臂本地 +Z 指玩家身后 = 第三人称-后
                    //   相机方向，billboard 正对相机；臂挥动时火把随之倾（自然）。
                    //   t260 放大：scale 0.16×0.30 → 0.22×0.42（与第一人称手持火把同步放大）。
                    //   t260 燃烧动画：顶端多色焰（外橙 + 中黄 + 白核）+ tpFlameS 跳动（第一人称同步；F5 第三人称
                    //     也见火把燃烧）。opacity 跟 bodyOpacity（观察者半透一致）。
                    Node {
                        id: heldTorchTp
                        visible: player.selectedBlock === 13 && player.mode !== PlayerController.Spectator
                        position: Qt.vector3d(0, -0.55, -0.30)
                        Model {
                            geometry: BillboardQuad {}
                            scale: Qt.vector3d(0.22, 0.42, 1.0)
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                cullMode: Material.NoCulling   // 双面（第三人称-前见背面；火把近对称）
                                alphaCutoff: 0.5
                                opacity: 0.99   // visible 已排除 Spectator → bodyOpacity 恒 1.0；<1 尊重贴图 alpha（透明底不渲染）
                                baseColorMap: torchIconTex
                            }
                        }
                        // t260 顶端多色焰（位置在 billboard 顶端：scale Y 0.42 → 顶端 y≈0.21，焰心约 y≈0.19）。
                        property real tpFlameS: 0.07
                        Node {
                            position: Qt.vector3d(0, 0.19, 0)
                            Model {
                                geometry: UnitCube {}
                                scale: Qt.vector3d(heldTorchTp.tpFlameS, heldTorchTp.tpFlameS * 1.10, heldTorchTp.tpFlameS)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff8a1a" }
                            }
                            Model {
                                geometry: UnitCube {}
                                scale: Qt.vector3d(heldTorchTp.tpFlameS * 0.70, heldTorchTp.tpFlameS * 0.78, heldTorchTp.tpFlameS * 0.70)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffd23c" }
                            }
                            Model {
                                geometry: UnitCube {}
                                scale: Qt.vector3d(heldTorchTp.tpFlameS * 0.45, heldTorchTp.tpFlameS * 0.50, heldTorchTp.tpFlameS * 0.45)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#fff4c4" }
                            }
                        }
                        SequentialAnimation on tpFlameS {
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.07; to: 0.09; duration: 110 }
                            NumberAnimation { from: 0.09; to: 0.055; duration: 150 }
                            NumberAnimation { from: 0.055; to: 0.08; duration: 90 }
                            NumberAnimation { from: 0.08; to: 0.07; duration: 130 }
                        }
                    }
                    // 手持工具（t75 木镐 3D / t233 锄 3D / t264 斧铲剑 3D）：选中工具槽时，第三人称右手上显工具 3D
                    //   （同第一人称分支，读 isTool(selectedItem)；不再 CrackBox 兜底）。五类工具几何纯实色体素 → 永不黑。
                    //   t264：按 toolType 选 5 类工具几何（镐 / 锄 / 斧 / 铲 / 剑，五互斥 Model）。
                    //   作 rightArmPivot 子节点 → 随右臂行走 / 挖掘挥臂同步（工具在手中）；握把（几何 y≈-0.45）
                    //   落在手位（rightArmPivot 本地 y≈-0.6），故 position.y=-0.55 使握把贴手心。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 1
                        geometry: PickaxeGeometry {}
                        position: Qt.vector3d(0, -0.55, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(0, 20, -35)   // 柄沿小臂方向、镐头斜上，自然手持
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // t233 锄（type=Hoe）第三人称手持：同位姿 / 同 tier 配色，几何换 HoeGeometry。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 2
                        geometry: HoeGeometry {}
                        position: Qt.vector3d(0, -0.55, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(0, 20, -35)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // t264 斧（type=Axe）第三人称手持：同位姿 / 同 tier 配色，几何换 AxeGeometry。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 3
                        geometry: AxeGeometry {}
                        position: Qt.vector3d(0, -0.55, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(0, 20, -35)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // t264 铲（type=Shovel）第三人称手持：同位姿 / 同 tier 配色，几何换 ShovelGeometry。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 4
                        geometry: ShovelGeometry {}
                        position: Qt.vector3d(0, -0.55, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(0, 20, -35)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // t264 剑（type=Sword）第三人称手持：纵向长刃，刃尖朝前上（持剑姿态）。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 5
                        geometry: SwordGeometry {}
                        position: Qt.vector3d(0, -0.50, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(-10, 20, -25)   // 剑身略竖直、刃尖斜上
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // t304/t330 弓第三人称手持：BowGeometry C 形弓身 + 子节点白弦。
                    //   拉弓动画由 player.bowDrawProgress 不在此分支驱动（第三人称手持静态；拉弓视觉主要在第一人称）。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 7
                        geometry: BowGeometry {}
                        position: Qt.vector3d(0, -0.48, -0.18)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        eulerRotation: Qt.vector3d(-10, 200, -25)   // 弓竖直、平面斜对相机（Y 180°+ 略偏）
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
                            opacity: playerModel.bodyOpacity
                        }
                        // t330 白弦（子节点继承父变换 + bodyOpacity 随身淡入淡出）。
                        Model {
                            geometry: BowStringGeometry {}
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                baseColor: "#f5f5f5"
                                opacity: playerModel.bodyOpacity
                            }
                        }
                    }
                    // t329 剪刀（type=Shears）第三人称手持：billboard ToolIcon 平图标（同手持火把第三人称路径：
                    //   BillboardQuad + cullMode NoCulling 三相机模式均可见 + 随臂行走 / 挥动同步）。静止时臂本地
                    //   +Z 指玩家身后 = 第三人称-后相机方向 → billboard 正对相机；NoCulling 让第三人称-前也见。
                    //   剪刀恒铁色（ToolIcon toolType===6 内部强制，不跟随 tier）。opacity:0.99 强制透明通道。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
                                 && hotbarVM.toolType(player.selectedItem) === 6
                        geometry: BillboardQuad {}
                        position: Qt.vector3d(0, -0.55, -0.30)
                        scale: Qt.vector3d(0.30, 0.30, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            cullMode: Material.NoCulling   // 双面（第三人称-前见背面；剪刀近对称）
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            baseColorMap: Texture {
                                flipV: false
                                sourceItem: ToolIcon {
                                    toolType: 6
                                    width: 64; height: 64
                                }
                            }
                        }
                    }
                }
            }
            // 左腿枢轴（t45 走 / t65 蹲下膝盖弯曲）：枢轴位于左髋 (-0.125, 0.6, 0)，蹲下时髋随上半身
            //   下沉 crouchDrop（与躯干底对齐，无断身缝隙）。腿分大腿 + 小腿两段 + 膝盖关节 Node：
            //   站立膝盖 0° → 大腿小腿成直线（总长 0.6，脚在 y=0，与重组前一致无回归）；蹲下大腿前抬
            //   crouchThigh、小腿在膝处回折 crouchKnee（=−crouchThigh → 小腿保持竖直、脚前移落地）→ 蹲姿。
            //   行走摆动叠加在大腿上（与右臂同相 −sin → 与右腿反相；右腿前则左腿后）。+eulerRotation.x = 腿前摆。
            //   静止归零（walkBlend=0）；仅走路模式有 walkPhase 推进（Spectator/飞=0 → 腿不摆）。
            Node {
                id: leftLegPivot
                position: Qt.vector3d(-0.125, 0.6 - playerModel.crouchDrop, 0)
                eulerRotation: {
                    // 行走摆幅（t51 ×swingAmp）+ 蹲下大腿前抬（t65 crouchThigh）。
                    const walk = -Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp
                    return Qt.vector3d(walk + playerModel.crouchThigh, 0, 0)
                }
                // 大腿段（裤色 #3a3a5a；髋下 0..0.3，中心 -0.15、scale.y=0.3）
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                }
                // 膝盖关节（t65）：位于大腿末端（髋下 0.3）。站立 0°（小腿续大腿成直线）；蹲下回折
                //   crouchKnee（=−crouchThigh）→ 小腿相对大腿弯折、整体腿弯曲，有效竖直高度缩短配合身体下沉。
                Node {
                    id: leftKneePivot
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(playerModel.crouchKnee, 0, 0)
                    // 小腿段（膝下 0..0.3，中心 -0.15、scale.y=0.3）
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                    }
                }
            }
            // 右腿枢轴（t45 / t65）：与左腿对称（右髋 0.125, 0.6, 0），行走与左臂同相（+sin）；蹲下同步下沉+膝盖弯。
            Node {
                id: rightLegPivot
                position: Qt.vector3d(0.125, 0.6 - playerModel.crouchDrop, 0)
                eulerRotation: {
                    // 行走摆幅（t51 ×swingAmp；与左腿对称）+ 蹲下大腿前抬（t65 crouchThigh）。
                    const walk = Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp
                    return Qt.vector3d(walk + playerModel.crouchThigh, 0, 0)
                }
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                }
                Node {
                    id: rightKneePivot
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(playerModel.crouchKnee, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                    }
                }
            }
        }

        // t116 F3+B 玩家碰撞箱（spec「玩家 0.6×1.8×0.6」+ 朝向箭头）：玩家 AABB 0.6×1.8×0.6，
        //   WireCube ±0.5 居中 → 摆到 AABB 中心 = feet + (0, 0.9, 0)（feet.y 到 feet.y+1.8 的中点），
        //   scale (0.6, 1.8, 0.6) → 各轴覆盖 ±0.3 / ±0.9 = AABB 实际范围。玩家 AABB 内是人形部件（无立方
        //   表面）→ 无 z-fight，scale 用精确值即可（不似 mob / 掉落物需 1% 外扩避面重叠）。
        //   朝向箭头：眼位高度（feet + 1.62）的细 UnitCube 棒，绕 Y 转 yaw；本地 -Z = 玩家前向（与
        //   playerModel / camera 同约定：yaw=0 时前向 (0,0,-1)）。棒中心前移 0.3（半长）→ 从眼位延伸到 -0.6
        //   处（视线方向 0.6 格长的红色指示棒，机制等价 MC 的 eye-line）。
        //   分层（PLAN §2）：纯呈现层调试叠层，只读 player.feetPosition/yaw（Game 层 Q_PROPERTY），绝不反向写。
        //   t143：第一人称（cameraMode===FirstPerson）看不到自己身体 → 玩家 hitbox 额外加 cameraMode 门控隐藏；
        //   mob / 掉落物 hitbox 无此门控（全视角可见，见各自 delegate）。
        Model {
            visible: window.showHitboxes && player.cameraMode !== PlayerController.FirstPerson
            position: Qt.vector3d(player.feetPosition.x, player.feetPosition.y + 0.9, player.feetPosition.z)
            scale: Qt.vector3d(0.6, 1.8, 0.6)
            geometry: WireCube {}
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
        Node {
            visible: window.showHitboxes && player.cameraMode !== PlayerController.FirstPerson
            position: Qt.vector3d(player.feetPosition.x, player.feetPosition.y + 1.62, player.feetPosition.z)
            eulerRotation: Qt.vector3d(0, player.yaw, 0)
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0, -0.3)
                scale: Qt.vector3d(0.03, 0.03, 0.6)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
            }
        }

        // 破/放粒子（t14/t16）：BlockParticles.qml 经 Loader 动态加载，隔离 Particles3D import，
        // 使该模块运行期缺失时仅 Loader 失败、顶层 Main.qml 仍正常加载（不命中 objectCreationFailed→exit(-1)）。
        // 分层（PLAN §2）：触发由 World(Game 层) 发，呈现层只消费、绝不反向写栅格。
        //
        // t16 根因修复：Loader 是 2D QQuickItem，其加载出的 3D Node 默认 parent=null（孤儿），
        // 不会并入 View3D 场景图 → 粒子永不渲染（「不可见」根因，实测 parent=null 证实）。
        // 故在此放一个场景内锚点 Node，Loader.onLoaded 时把加载到的 Node 领养进它，
        // 使整棵粒子子树并入 3D 场景图。Particles3D 不可用 → Loader Error → item 为 null，
        // onLoaded 不触发 → 无领养 → 粒子静默降级（§2-E「保持运行而非崩溃」），且状态落 console 可核验。
        Node { id: particlesHost } // 场景内锚点：粒子内容被领养进此节点才渲染

        Loader {
            id: particleLoader
            active: true
            source: "BlockParticles.qml"
            onLoaded: {
                // 关键：领养进场景锚点 Node（否则加载到的 Node parent=null → 孤儿 → 不渲染）。
                particleLoader.item.parent = particlesHost
                console.info("[t16] BlockParticles adopted into scene graph (parent=Node)")
            }
            onStatusChanged: {
                // 加载状态落 console（落 voxelsandbox.log），使「粒子节点加载状态在 console
                // 可见且非 Error」可被运行期核验；Error 时显式告警（§2-E：不得静默吞）。
                if (status === Loader.Ready)
                    console.info("[t16] BlockParticles Loader status = Ready")
                else if (status === Loader.Error)
                    console.warn("[t16] BlockParticles Loader status = Error — Particles3D 运行期不可用，粒子已降级关闭（§2-E）")
            }
        }

        // t157 火把顶部少量烟雾粒子：TorchSmoke.qml 经 Loader 动态加载（同 particleLoader 的 Particles3D
        //   隔离模式 — 模块运行期缺失时仅本 Loader 失败 + 显式告警，Main.qml 仍正常加载，§2-E「保持运行
        //   而非崩溃，且不静默吞」）。TorchSmoke.qml 内单 ParticleSystem3D + Repeater（每火把一个
        //   ParticleEmitter3D：慢速上飘 + 横向轻扰 + 淡入淡出）。
        //   分层（PLAN §2）：呈现层只消费 World 的火把位置列表（torchPositions，与 torchHost 同源），
        //   经 Loader.onLoaded 把 ListModel 引用注入 TorchSmoke.torchModel；Repeater 反应式跟随
        //   torchPositions 增删（ListModel 信号驱动），不反向写栅格。
        Loader {
            id: smokeLoader
            active: true
            source: "TorchSmoke.qml"
            onLoaded: {
                // 领养进同一个 particlesHost 锚点（复用既有 3D 场景节点，无需另开锚点）。
                smokeLoader.item.parent = particlesHost
                // 注入火把位置 ListModel（引用稳定；TorchSmoke 内 Repeater 据其 count/角色反应式更新）。
                smokeLoader.item.torchModel = torchPositions
                console.info("[t157] TorchSmoke adopted into scene graph; torches=" + torchPositions.count)
            }
            onStatusChanged: {
                if (status === Loader.Ready)
                    console.info("[t157] TorchSmoke Loader status = Ready")
                else if (status === Loader.Error)
                    console.warn("[t157] TorchSmoke Loader status = Error — Particles3D 运行期不可用，火把烟雾已降级关闭（§2-E）")
            }
        }

        // 方块掉落实体渲染（t35）：item entity 的小方块图标在此渲染。Repeater 父节点 = 场景内
        // 3D Node（itemHost）→ delegate（Node/Model，3D 对象）被领养进 3D 场景图（lessons-learned
        // 「动态 3D 对象必须挂到场景 Node，否则孤儿不渲染」—— t03 Repeater 直接挂 View3D 会成孤儿
        // / 告警；此处挂 Node 下，delegate.parent = itemHost = 3D Node → 进场景）。
        //
        // 触发：itemEntities.count 随 spawn 自增（NOTIFY entitiesChanged）→ Repeater 追加 delegate
        // （int model 不重建已有 → 各实体动画连续不被打断）。
        // 分层（PLAN §2）：实体数据属 Game（ItemEntityManager），呈现属 View（本 Repeater）；
        // 旋转 / 浮动是纯呈现动画，呈现层自发、不反向写数据。
        Node {
            id: itemHost
            Component.onCompleted: {
                console.info("[t53] itemHost UP parent=" + itemHost.parent + " (须为 3D Node 非 null；null=孤儿不渲染)")
            }

            Repeater {
                model: itemEntities.count
                delegate: Node {
                    // t256 slot-reuse：掉落物拾取（removeAt / setCountAt(0)）改 releaseSlot 标空（不 erase）→
                    //   count 单调不降 → 本 Repeater 永不销毁 delegate（修掉落沙衍生掉落物 + 生存挖掘产出的
                    //   spawn/拾取抖动致 delegate 泄漏，同 mobHost 族；lessons-learned t170）。空槽 aliveAt=false
                    //   → visible=false 隐藏；复用时 aliveAt=true + revision bump → 重显重绑。索引稳定。
                    visible: { itemEntities.revision; return itemEntities.aliveAt(index) }
                    // 基准位置 + 物品 id + count：触碰 itemEntities.revision（Q_PROPERTY NOTIFY=entitiesChanged）
                    // 建立依赖。t36 removeAt 用 releaseSlot（标空，slot 稳定不 shift），revision 自增 → 本绑定
                    // 重算 → delegate[k] 对齐 slot[k] 的 pos/itemId/count。外层 Node 持基准 pos + 绕 Y 旋转。
                    // t64 加 count 触碰：部分拾取后 setCountAt bump revision → 数量重算。
                    id: entRoot
                    position: { itemEntities.revision; return itemEntities.posAt(index) }
                    property int entId: { itemEntities.revision; return itemEntities.itemIdAt(index) }
                    property int entCount: { itemEntities.revision; return itemEntities.countAt(index) }
                    property real rotY: 0       // 绕 Y 旋转角（度）
                    property real bobY: 0       // 上下浮动偏移（格）
                    eulerRotation: Qt.vector3d(0, rotY, 0)

                    // [bug1 修复] Repeater 创建的 3D delegate 默认 parent=null（孤儿不渲染——同 t16 Loader
                    //   坑、t03 Repeater 坑：QQuickRepeater 不会把 3D delegate 领养进 3D 场景图）。实测 log
                    //   曾打 parent=null。修法同 t16：onCompleted 显式 reparent 进 itemHost（3D Node）→ 进场景渲染。
                    Component.onCompleted: {
                        if (parent === null) parent = itemHost
                        console.info("[t53] entity delegate[" + index + "] parent=" + parent
                            + " pos=" + position + " id=" + entRoot.entId + " count=" + entRoot.entCount
                            + " (须 QQuick3DNode 非 null)")
                    }

                    // —— t64 实体贴图按 id 段分流（修「木棒/木镐 Q 丢弃后贴图是 Stone」根因）——
                    //   根因：原 BlockCube.setBlockId 对越界 id（>= BlockRegistry::Count，如工具段 0x100 / 材料
                    //   段 0x200）兜底为 Stone → 整段外观坍缩成石头。HUD hotbar 早已 isTool/isMaterial 分流到
                    //   ToolIcon/MaterialIcon 自绘；掉落实体 Repeater 没有这层分流，故坍缩。
                    //   分流（机制等价 HUD hotbar delegate 的三分互斥 visible 模式）：
                    //   - 方块段（!isTool && !isMaterial）→ BlockCube + 共享图集 voxelAtlas（现状不变）；
                    //   - 工具段（isTool）→ PickaxeGeometry 3D 镐形（t75，纯实色体素，baseColor 按 tier）；
                    //   - 材料段（isMaterial）→ BillboardQuad（t112，单面 +Z 法线）+ Texture.sourceItem =
                    //     MaterialIcon Canvas + eulerRotation 朝相机（billboard 平图标，替代旧 CrackBox 6 面立方）。
                    //   Texture.flipV：t186 改 false（旧 true 把 sourceItem 的 Canvas 上下翻 → 桶开口朝下；
                    //     详见手持材料路径 t186 注释）。材料段图标与背包槽 MaterialIcon（无 flipV）一致。
                    //   分层（PLAN §2）：呈现层只消费 ItemEntityManager 数据；ToolIcon/MaterialIcon 是纯呈现
                    //   层 QML 自绘（§9a 原创），无 MC 资产 / 反向写栅格。

                    // 方块段：BlockCube 走图集 per-face UV（草顶 / 草侧 …）。
                    //   t144：baseColor 乘 terrainLight(skyLight)（0.4..1.0 灰阶）与地形 chunk 同公式——
                    //   掉落物浮空无天光遮蔽（BlockCube 无顶点色，等效 vertexColor=1.0），故仅靠 baseColor
                    //   承载昼夜乘子；夜间随地形一起变暗（spec「阴影/夜间变暗」）。绑定 skyLight NOTIFY
                    //   → 每周期 tick 自动刷新（同 chunk Model）。
                    //   t169 火把掉落物黑底修复：火把贴图（tile 17）是透明底（alpha=0）+ 火把像素（alpha=255）。
                    //   BlockCube 把它铺到 1×1×1 立方体六面，材质无 alpha 处理时透明底被当不透明 → 渲成黑色
                    //   填充整面（用户实测「火把掉落物黑底」）。alpha-test（alphaCutoff 0.5）丢弃透明底像素、
                    //   仅留火把像素 → 透明底不再显黑（机制同手持火把 viewModelHand / CrackBox 的 alphaCutoff 路径）。
                    //   仅火把（id 13）启用；其余方块贴图无 alpha，保持 alphaCutoff=0（默认不透明）。
                    Model {
                        visible: entRoot.entId !== 13 && !hotbarVM.isPartialBlock(entRoot.entId) && !hotbarVM.isTool(entRoot.entId) && !hotbarVM.isMaterial(entRoot.entId)
                        geometry: BlockCube { blockId: entRoot.entId }
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: terrainLight(worldClock.skyLight)
                            alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）走下方 billboard 分支
                        }
                    }
                    // t219 木板衍生方块掉落实体：异形段（台阶/楼梯/栅栏/压力板/门/活板门）非整立方 → BillboardQuad
                    //   平图标（dimetric 立体图标 icon_wood_*.png），非 BlockCube 满格木板立方（异形各面 tile=planks
                    //   → BlockCube 渲成「一块木板」与木板不可辨）。机制同火把 / 材料段掉落 billboard（朝相机单面 +Z）：
                    //   本 Model 是 entRoot（绕 Y 自转 rotY）子节点，本地 yaw 减 rotY 抵消继承 → 世界旋转 = 相机旋转
                    //   → +Z 恒指回相机、正面恒可见。scale 0.3（同方块段 / 材料段掉落物统一）；baseColor 乘
                    //   terrainLight(skyLight) 夜间变暗（同方块段）；alphaCutoff:0.5 + opacity:0.99 沿用 alpha-test 契约。
                    //   Texture inline 读 per-entity entId（每个掉落物各显示自己的异形图标）。
                    Model {
                        visible: hotbarVM.isPartialBlock(entRoot.entId)
                        geometry: BillboardQuad {}
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y - entRoot.rotY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            baseColor: terrainLight(worldClock.skyLight)
                            baseColorMap: Texture {
                                source: hotbarVM.iconSourceForBlock(entRoot.entId)
                                generateMipmaps: false
                            }
                        }
                    }
                    // t218 火把掉落实体：火把非立方 → BillboardQuad 平图标（细立柱），非 BlockCube 6 面立方
                    //   （肉眼「贴火把的小立方」非「火把」）。机制同材料段 billboard（朝相机单面 +Z）：本 Model 是
                    //   entRoot（绕 Y 自转 rotY）子节点，本地 yaw 减 rotY 抵消继承 → 世界旋转 = 相机旋转 → +Z 恒
                    //   指回相机、正面恒可见（火把图标始终正对玩家，不随 entRoot 自转「转背面」）。scale 0.24×0.42
                    //   非等比细高（火把像素约占图 0.75 高 → 渲染火把 ~0.31 高，与方块段 0.3 立方相当、但细）。
                    //   baseColor 乘 terrainLight(skyLight) 夜间变暗（同方块段 / 材料段掉落物统一）。
                    //   alphaCutoff:0.5 + opacity:0.99 沿用 alpha-test 契约（透明底不丢弃会被当不透明黑）。
                    Model {
                        visible: entRoot.entId === 13
                        geometry: BillboardQuad {}
                        scale: Qt.vector3d(0.24, 0.42, 1.0)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y - entRoot.rotY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            baseColor: terrainLight(worldClock.skyLight)
                            baseColorMap: torchIconTex
                        }
                    }
                    // 工具段（t75 改用 PickaxeGeometry 3D 镐形，不再 CrackBox 兜底；t233 加 HoeGeometry 锄形；
                    //   t264 加 AxeGeometry / ShovelGeometry / SwordGeometry 斧铲剑形）：
                    //   旧 CrackBox + ToolIcon(透明底 RGB0) 贴图无 alphaCutoff → 透明底被当不透明黑 → 6 面黑立方体
                    //   （「工具贴图黑」根因）。五类工具几何是纯实色体素几何（无贴图 / 无 alpha）→ 永不黑。
                    //   baseColor 按 tier 着色（木褐 / 石灰 / 铁银白，五类同 tier 同色）；绕 Y 自转时正面恒有工具形可见。
                    //   t264：按 toolType 选 5 类工具几何（镐 / 锄 / 斧 / 铲 / 剑，五互斥 Model）。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 1
                        geometry: PickaxeGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // t144：tier 色乘天光乘子（tintBySkyLight）夜间变暗，与方块段统一。
                            //   原 hex #d8d8e6 / #9a9a9a / #8a5a2e = (216,216,230)/(154,154,154)/(138,90,46)。
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                    }
                    // t233 锄掉落物（type=Hoe）：同 scale / 位置 / tier 配色，几何换 HoeGeometry。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 2
                        geometry: HoeGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                    }
                    // t264 斧掉落物（type=Axe）：同 scale / 位置 / tier 配色，几何换 AxeGeometry。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 3
                        geometry: AxeGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                    }
                    // t264 铲掉落物（type=Shovel）：同 scale / 位置 / tier 配色，几何换 ShovelGeometry。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 4
                        geometry: ShovelGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                    }
                    // t264 剑掉落物（type=Sword）：纵向长刃，同 scale / 位置 / tier 配色，几何换 SwordGeometry。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 5
                        geometry: SwordGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                    }
                    // t304/t330 弓掉落物（type=Bow）：BowGeometry C 形弓身 + 子节点白弦，随 entRoot 绕 Y 自转。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 7
                        geometry: BowGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
                                     : t === 2 ? tintBySkyLight(154/255, 154/255, 154/255, m)
                                     : tintBySkyLight(138/255, 90/255, 46/255, m)
                            }
                        }
                        // t330 白弦（蜘蛛丝白；随天光变暗，同弓身 / 方块掉落物）。
                        Model {
                            geometry: BowStringGeometry {}
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                baseColor: tintBySkyLight(245/255, 245/255, 245/255, worldClock.skyLight)
                            }
                        }
                    }
                    // t329 剪刀掉落物（type=Shears）：billboard ToolIcon 平图标（同材料段掉落路径：朝相机单面 +Z，
                    //   eulerRotation 抵消 entRoot 自转 rotY → 世界旋转 = 相机旋转 → +Z 恒指回相机）。剪刀恒铁色
                    //   （ToolIcon toolType===6 内部强制，不跟随 tier）。alphaCutoff:0.5 + opacity:0.99 沿用 alpha-test
                    //   契约（透明底不丢弃会被当不透明黑）；baseColor 乘 terrainLight 夜间变暗（同方块 / 材料段掉落物）。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId) && hotbarVM.toolType(entRoot.entId) === 6
                        geometry: BillboardQuad {}
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y - entRoot.rotY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            baseColor: terrainLight(worldClock.skyLight)
                            baseColorMap: Texture {
                                flipV: false
                                sourceItem: ToolIcon {
                                    toolType: 6
                                    width: 64; height: 64
                                }
                            }
                        }
                    }
                    // 材料段（t112 改 BillboardQuad + 朝相机）：木棒 / 煤 / 铁原矿 / 铁锭 / 玻璃 / 木炭
                    //   （RecipeRegistry::*Id，0x200 段）均无等距立方体 PNG，走 MaterialIcon Canvas 自绘。
                    //   旧 CrackBox 把同一张 2D 图标贴到立方体 6 面 → 斜视时 6 个半透面互相穿插、呈
                    //   「未封闭超立方体」观感（图标边缘透明底露出后面面的图标像素）。改单面 BillboardQuad
                    //   （+Z 法线）+ 令其世界朝向 = 相机朝向 → 恒以一张完整平图标正对玩家（MC 1.0 掉落物 billboard）。
                    //   **朝相机实现**：QQuick3DNode 无 lookAt 属性（仅 QQuick3DCamera 有 lookAt()/lookAtNode；
                    //   写 `Node{ lookAt: cam.position }` 是不存在的属性 → 运行期 objectCreationFailed，见
                    //   lessons-learned qmlcachegen 坑）。故用 eulerRotation 显式算：承载 Model 的**世界**欧拉
                    //   = 相机欧拉（cam.eulerRotation）→ 其 +Z 法线恒 = -相机 forward → 指回相机 → 正面恒可见。
                    //   本 Model 是 entRoot（绕 Y 自转 rotY，服务于 block/tool 立方段）的子节点，会继承 rotY 自转，
                    //   故本地 yaw 减 rotY 抵消继承：世界 = Ry(rotY)·Ry(camYaw-rotY)·Rx(camPitch) = Ry(camYaw)·Rx(camPitch)
                    //   = 相机旋转（QtQuick3D eulerRotation 按 fromEulerAngles(pitch,yaw,roll)=Ry·Rx 当 roll=0）。
                    //   cam.eulerRotation NOTIFY eulerRotationChanged（每帧随玩家视角 / 受伤抖动变）+ rotY 自转
                    //   NOTIFY → 绑定每帧重算，billboard 始终贴脸相机。第一/第三人称-后/前三模式都对（相机恒朝
                    //   玩家区域看，billboard 镜像相机朝向即恒正对相机）。
                    //   **alphaCutoff + opacity<1**（沿用 t85 alpha-test 契约）：MaterialIcon Canvas 透明底若不
                    //   丢弃会被当不透明黑 → 图标坍成黑块。alphaCutoff:0.5 + opacity:0.99 → 仅图标像素显。
                    //   flipV：t186 改 false（旧 true 翻 V → 桶开口朝下；同手持材料路径）。
                    Model {
                        visible: hotbarVM.isMaterial(entRoot.entId)
                        geometry: BillboardQuad {}
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y - entRoot.rotY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            alphaCutoff: 0.5
                            opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                            // t144：baseColor 乘 terrainLight(skyLight) 灰阶夜间变暗，与方块段统一
                            //   （MaterialIcon 贴图 × baseColor × 不变 alpha；alpha=1.0 不与 opacity/alphaCutoff 冲突）。
                            baseColor: terrainLight(worldClock.skyLight)
                            baseColorMap: Texture {
                                // t186 修复(b)：flipV 改 false（同上方手持材料路径）——旧 true 把 MaterialIcon
                                //   Canvas 上下翻 → 桶开口朝下；改 false 后与背包槽图标一致（开口 / 高光朝上）。
                                flipV: false
                                sourceItem: MaterialIcon {
                                    materialId: entRoot.entId
                                    width: 64; height: 64
                                }
                            }
                        }
                    }
                    // t43：浅灰半透外壳（spec「外裹浅灰半透球 / 半透外壳」）—— 略大于内方块的
                    // UnitCube 半透壳，包裹小方块图标形成「光晕包裹」视觉。用 UnitCube（本工程静态
                    // #Cube/#Sphere 内置 mesh 实测不渲染 → 自定义几何是已验证可见路径，见 lessons-learned）
                    // + NoLighting + opacity<1（<1 自动走透明混合，同观察者幽灵半透模式 bodyOpacity 0.35）。
                    // 与内方块共享外层 Node 的绕 Y 旋转 + 浮动（position 读 entRoot.bobY 同步上下浮）。
                    // 三类图标共用此壳（外观统一，与内方块图标类型无关）。
                    //   t144：外壳 baseColor 也乘天光乘子（tintBySkyLight）—— 内方块夜间变暗时外壳
                    //   同步变暗，否则夜间「亮光晕裹暗方块」割裂（spec「阴影/夜间变暗」覆盖整掉落物）。
                    //   #b0b0b0 = (176,176,176)；半透 opacity 0.35 与 baseColor 解耦，不变。
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: tintBySkyLight(176/255, 176/255, 176/255, worldClock.skyLight)
                            opacity: 0.35          // 半透（<1 触发透明混合）
                        }
                    }
                    // t116 F3+B 掉落物碰撞箱（spec「掉落物 0.3」+ 朝向箭头）：掉落物 AABB = 0.3 立方（与图标
                    //   Model scale 0.3 一致）。WireCube ±0.5 scale 0.31（+1% 外扩避与图标 / 浅灰外壳立方表面
                    //   z-fight——三立方共面，精确 0.3 会让线被面遮挡看不见）。随浮动跟随 bobY。
                    //   ⚠ AABB 轴对齐：父 entRoot 自转 rotY（eulerRotation.y=rotY）会被继承 → AABB 跟着转，
                    //   但 AABB 应世界轴对齐（不随物品自转）。子节点本地 eulerRotation.y = -rotY 抵消继承 →
                    //   世界 Y 旋转 = rotY + (-rotY) = 0（轴对齐）。
                    //   朝向箭头：相反——应「绑 yaw」，对掉落物即绑自转 rotY（物品唯一朝向态）。父已转 rotY →
                    //   子本地不加额外旋转即世界 yaw=rotY，箭头随物品自转指向（与 MC 实体 eye-line 等价的朝向指示）。
                    //   分层（PLAN §2）：纯呈现层调试叠层，只读 entRoot.bobY/rotY（呈现层自发动画态）。
                    Model {
                        visible: window.showHitboxes
                        geometry: WireCube {}
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        eulerRotation: Qt.vector3d(0, -entRoot.rotY, 0)   // 抵消父自转 → AABB 轴对齐
                        scale: Qt.vector3d(0.31, 0.31, 0.31)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                    }
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, entRoot.bobY, -0.15)     // 棒中心前移 0.15（半长）→ -Z 方向延伸 0.3
                        scale: Qt.vector3d(0.02, 0.02, 0.3)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                    }
                    // 绕 Y 匀速自转（~3s 一圈），loops 无限。
                    NumberAnimation on rotY { from: 0; to: 360; duration: 3000; loops: Animation.Infinite }
                    // 上下浮动 0.15 格（~2s 周期），InOutSine 近似 sin 手感（spec：上下浮动 sin）。
                    SequentialAnimation on bobY {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: 0.15; duration: 1000; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.15; to: 0; duration: 1000; easing.type: Easing.InOutSine }
                    }
                }
            }
        }

        // t95 测试生物渲染（统一 EntityManager 的 pushable 实体）：Repeater 父节点 = 场景内 3D Node
        // （mobHost）→ delegate（Node/Model，3D 对象）被领养进 3D 场景图（lessons-learned「动态 3D 对象
        // 必须挂到场景 Node，否则孤儿不渲染」—— 同 itemHost / torchHost 模式）。
        //
        // 触发：entityManager.count 随 spawnMob 自增（NOTIFY entitiesChanged）→ Repeater 追加 delegate
        // （int model 不重建已有）。位置随玩家推动 / 重力下落 bump revision → {revision; posAt} 绑定重算。
        // 分层（PLAN §2）：实体数据属 Entities（EntityManager），呈现属 View（本 Repeater）；只读消费、
        // 绝不反向写（同 itemEntities Repeater 模式）。
        Node {
            id: mobHost
            Component.onCompleted: {
                console.info("[t95] mobHost UP parent=" + mobHost.parent + " (须为 3D Node 非 null)")
            }

            Repeater {
                model: entityManager.count
                delegate: Node {
                    // t256 slot-reuse：实体移除（沙着地 / mob 死亡 / 跌出）改 releaseSlot 标空（不 erase）→
                    //   count 单调不降 → 本 Repeater 永不销毁 delegate（修掉落沙频繁 spawn/land 致 delegate
                    //   泄漏：reparent 后的 3D delegate count 减小不销毁，lessons-learned t170）。空槽 aliveAt=false
                    //   → 本 Node visible=false 隐藏整棵子树；slot 被复用时 aliveAt=true + revision bump → 重显
                    //   并重绑新实体数据。索引稳定（release 不 shift）→ delegate[index] 恒对齐 slot[index]。
                    visible: { entityManager.revision; return entityManager.aliveAt(index) }
                    // 触碰 revision 建立依赖（push 位移 / 重力下落 / t239 AI 行走 / 受击红闪 / 死亡移除
                    //   bump revision → 位置 / 配色 / kind / yaw 重算）。t117 FallingBlock 着地 releaseSlot 后
                    //   revision 自增 → delegate 对齐新 entity 数据（同 itemEntities delegate 模式）。
                    position: { entityManager.revision; return entityManager.posAt(index) }
                    property int entKind: { entityManager.revision; return entityManager.kindAt(index) }
                    // t239 身体朝向：Mob 按 yawAt 转（模型本地 -Z 正对 AI 行走方向，与 player.yaw 同约定）；
                    //   FallingBlock（沙立方）对称 → 不转（bodyYaw=0）。子节点（Mob Model / F3+B 箭头）随之继承。
                    property real bodyYaw: { entityManager.revision; return entKind === EntityManager.Mob ? entityManager.yawAt(index) : 0 }
                    eulerRotation: Qt.vector3d(0, bodyYaw, 0)
                    Component.onCompleted: {
                        // [lessons-learned] Repeater 创建的 3D delegate 默认 parent=null（孤儿不渲染），
                        // onCompleted 显式 reparent 进 mobHost（同 itemHost / torchHost delegate）。
                        if (parent === null) parent = mobHost
                        console.info("[t95] entity delegate[" + index + "] parent=" + parent
                            + " pos=" + position + " (须 QQuick3DNode 非 null)")
                    }

                    // t117 FallingBlock：贴图方块（BlockCube + 共享图集 voxelAtlas，复用地形贴图），scale 1.0
                    //   = 1×1×1（与地形方块外观一致；落体过程视觉读作「移动的方块」）。NoLighting（可见 Model
                    //   必须 NoLighting，lessons-learned）。blockId 据 entity 携带值（沙=8，与地形同贴图）。
                    //   t257 掉落沙光影：BlockCube 接 world + worldPos + sunDir + shadowsEnabled → 顶点色按方块
                    //   世界位采样光场 + PCF 软影（VoxelLight::vertexLight，与 chunkgeometry 立方面同公式）。
                    //   材质 baseColor 乘 terrainLight(skyLight)（昼夜灰阶，同 chunk Model）+ vertexColorsEnabled:true
                    //   → 最终色 = terrainLight × vertexColor × 贴图，与地形同亮度曲线。修「沙掉落时变亮 / 暗处
                    //   挖底沙→掉落沙明显变亮」（旧版无 baseColor 也无顶点色 → 恒全亮，洞穴/夜间格外刺眼）。
                    //   worldPos 绑 posAt（触碰 revision 建依赖 → 每帧位移 / 重力下落重烘顶点色）；sunDir 绑
                    //   worldClock.sunDir（太阳跨步重烘）；shadowsEnabled 绑 window.shadowsEnabled（开关同步）。
                    Model {
                        visible: entKind === EntityManager.FallingBlock
                        geometry: BlockCube {
                            blockId: { entityManager.revision; return entityManager.blockIdAt(index) }
                            world: theWorld
                            worldPos: { entityManager.revision; return entityManager.posAt(index) }
                            sunDir: worldClock.sunDir
                            shadowsEnabled: window.shadowsEnabled
                        }
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: terrainLight(worldClock.skyLight)  // t257 昼夜灰阶（同 chunk Model）
                            vertexColorsEnabled: true                      // t257 顶点色光场 × PCF 软影
                        }
                    }
                    // Mob（原 t95 测试生物；t239 生物基类；t240 猪牛羊模型 + 贴图）：
                    //   - mobType 0（通用测试生物）：仍走 UnitCube 单色立方（保 t95 行为不变，spec「mobType 0 不进 MobModel」）；
                    //   - mobType 1/2/3（猪/牛/羊）：走 MobModel 方块化原创 3D 模型 + 各自贴图（四肢+躯干+头，
                    //     §9 区隔不照搬 MC），随 delegate Node eulerRotation.y=bodyYaw 转向 AI 行走方向（MobModel
                    //     头朝 -Z = 行走方向；纯色立方对称看不出，方块化模型有前/后可见）。
                    //   NoLighting（lessons-learned 红线：可见 Model 必须 NoLighting）。受击红闪（hurtFlashAt>0 →
                    //   全红，机制等价 MC mob 受击 10 tick 红闪）：mobType 0 走 baseColor 红；mobType 1/2/3 走
                    //   纯红 Texture（mobCowTex 的内容被红 #ff0000 baseColor 调制 → 视觉全红）。
                    property int entMobType: { entityManager.revision; return entityManager.mobTypeAt(index) }
                    // t252/t293 碰撞箱尺寸（halfW/halfH）：C++ 按 mobType 设（t293 收紧贴合身体：pig/sheep
                    //   0.40/0.45、cow 0.40/0.50、敌对 0.30/0.90、spider 0.45/0.30、MobTest/FallingBlock 0.5）。
                    //   WireCube hitbox scale + 朝向棒长度读它们（旧版固定 1×1×1）。
                    property real mobHalfW: { entityManager.revision; return entityManager.radiusAt(index) }
                    property real mobHalfH: { entityManager.revision; return entityManager.halfHeightAt(index) }
                    // t252 模型 Y 偏移：collision 中心（pos.y）≠ 模型躯干中心（halfH 变后二者分离）→ 模型
                    //   需 Y 偏移使腿底贴 collision 底面（= 地面）。offset = modelLegBottom − halfH
                    //   （modelLegBottom = MobModel 腿底本地 |y|：pig 0.48 / cow 0.50 / sheep 0.44；MobTest
                    //   UnitCube ±0.5 → 0.50）。cow halfH=0.70 → offset −0.20（模型下移贴地，免悬空）。
                    property real mobModelYOff: {
                        if (entMobType === 1) return 0.48 - mobHalfH   // pig
                        if (entMobType === 2) return 0.50 - mobHalfH   // cow
                        if (entMobType === 3) return 0.44 - mobHalfH   // sheep
                        // t282 Shambler（人形）：MobModel 腿底本地 |y|=0.90（halfH=0.90 → offset=0，腿底贴 collision 底面）。
                        if (entMobType === EntityManager.MobShambler) return 0.90 - mobHalfH
                        // t284 Stalker（潜行者/苦力怕）：MobModel 腿底本地 |y|=0.90（halfH=0.90 → offset=0）。
                        if (entMobType === EntityManager.MobStalker) return 0.90 - mobHalfH
                        if (entMobType === EntityManager.MobBones) return 0.90 - mobHalfH   // t287 Bones 人形（腿底 0.90）
                        if (entMobType === EntityManager.MobSpider) return 0.30 - mobHalfH  // t285 Spider 宽矮（腿底 0.30）
                        return 0.50 - mobHalfH                          // MobTest（UnitCube ±0.5）
                    }
                    Model {
                        // mobType 0 / 5：通用测试生物（t95/t239）+ t280 敌对 Bones(5)（骷髅，t283 待做原创模型）
                        //   —— UnitCube 单色立方（原创几何，§9 区隔不照搬 MC 美术）。敌对走 EntityManager.spawnHostileMob
                        //   设的 colorAt（Bones 灰白 #d8d4c4）；mobType 0 仍走 spawnMob 的 #ff5555。
                        //   受击红闪：hurtFlashAt>0 → baseColor #ff0000 覆盖。t280 燃烧：isBurningAt>0 → baseColor
                        //   偏橙（火焰色调制单色立方，与下方 flame Model 共显「着火」感）。
                        //   t282：Shambler(4) 不再走本 UnitCube 路径 —— 已迁到下方专属 MobModel 人形 + 贴图分支。
                        visible: entKind === EntityManager.Mob
                                 && entMobType === 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                entityManager.revision
                                if (entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                // t280 燃烧中 → 橙红偏色（与 flame Model 叠加显「着火」），否则走 mob 配色。
                                if (entityManager.isBurningAt(index)) return "#ff7a3a"
                                return entityManager.colorAt(index)
                            }
                        }
                    }
                    // t280 敌对生物日光燃烧火焰视觉（spec「白天燃烧消失」）：hostile 暴露日光时 EntityManager
                    //   标 burning=true（tickHostileLife 每 tick 重算）→ 本 Model 显「火焰」—— 一个略大于 mob 的
                    //   橙黄半透立方叠在 mob 外，opacity 快速抖动模拟火苗窜动（机制等价 MC 僵尸 / 骷髅日光着火
                    //   视觉；原创自绘非照搬）。仅 hostile + burning 显；passive 永不显。NoLighting（可见 Model 必须
                    //   NoLighting，lessons-learned 红线）。绑 revision 触碰 → 翻入 / 翻出 burning 时重算 visible。
                    Model {
                        visible: { entityManager.revision; return entityManager.isBurningAt(index) }
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, mobModelYOff, 0) // 同 mob 本体对齐（腿底贴 collision 底面）
                        scale: Qt.vector3d(1.06, 1.10, 1.06)      // 略大于 mob（火苗包覆感）
                        opacity: 0.7  // t280 修：恢复提交漏定义 flameOpacity 致 ReferenceError，暂用常量（火苗明灭动画留后续）
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#ff8a2a"
                            // opacity 绑定 flameOpacity.value：SequentialAnimation 抖动 → 火苗明灭窜动感（机制等价）。
                            opacity: 0.7  // t280 修：恢复提交漏定义 flameOpacity 致 ReferenceError，暂用常量（火苗明灭动画留后续）
                        }
                        property real flameOpacity: 0.55
                        SequentialAnimation on flameOpacity {
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.40; to: 0.78; duration: 110 }
                            NumberAnimation { from: 0.78; to: 0.40; duration: 140 }
                        }
                    }
                    Model {
                        // t240 猪（mobType 1）：MobModel 方块化原创模型 + mob_pig 贴图。
                        // t241 行走动画：walkPhase 绑定驱动 4 腿对角摆动（moveSpeed>0 时 EntityManager 每帧推进相位）。
                        visible: entKind === EntityManager.Mob && entMobType === 1
                        geometry: MobModel {
                            mobType: 1
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面（halfH 变后免悬空 / 穿地）
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // 受击红闪：hurtFlashAt>0 → baseColor=#ff0000 调制贴图全红（同 mobType 0 红闪语义）。
                            baseColor: { entityManager.revision; return entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight) }
                            baseColorMap: mobPigTex
                        }
                        // t251 眼睛：mob 贴图是「全脸」身体纹（铺每盒每面，无五官）→ 头部正面无眼显「怪」。补 2 白眼底
                        //   (#e8e8e8) + 2 深色瞳 (#1a1a1a) 小方块作 MobModel 子节点（同 t39 玩家眼睛模式：呈现层独立
                        //   纯色 Model，不进 MobModel 几何 / 不共享 mob 贴图 → 实心眼色不受身体贴图调制）。NoLighting（红线）。
                        //   眼作 mob Model 子节点 → 继承 bodyYaw（眼朝 AI 行走方向 -Z）+ 父 visible（mobType 切换同步隐显）。
                        //   猪/牛 headPitch 恒 0（EntityManager.headPitchAt 非 sheep 返 0）→ 眼直接定位头前面、无需俯仰 Node。
                        //   位置 = MobModel 局部坐标（原点躯干中心）：猪头心 (0,0.05,-0.50) 半 (0.22,0.22,0.18) → 前面 z=-0.68，
                        //   眼在上半 y≈0.13、左右 x=±0.10；z 贴头前面（眼底 z=-0.68、瞳 z=-0.69 略凸出，同 t52 贴脸防 z-fight）。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.10, 0.13, -0.68)
                            scale: Qt.vector3d(0.08, 0.10, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.10, 0.13, -0.68)
                            scale: Qt.vector3d(0.08, 0.10, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.10, 0.13, -0.69)
                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.10, 0.13, -0.69)
                            scale: Qt.vector3d(0.04, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                    }
                    Model {
                        // t240 牛（mobType 2）：MobModel + mob_cow 贴图（高大长身 + 头顶两小角盒）。
                        // t241 行走动画：walkPhase 绑定驱动腿摆（同猪）。
                        visible: entKind === EntityManager.Mob && entMobType === 2
                        geometry: MobModel {
                            mobType: 2
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t252 cow halfH=0.70 → offset −0.20 腿底贴地
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: { entityManager.revision; return entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight) }
                            baseColorMap: mobCowTex
                        }
                        // t251 眼睛（牛）：同猪眼模式（mob Model 子节点，纯色 NoLighting，继承 bodyYaw + visible）。
                        //   牛头心 (0,0.15,-0.60) 半 (0.20,0.22,0.20) → 前面 z=-0.80；眼上半 y≈0.22、x=±0.09。
                        //   牛 headPitch 恒 0 → 直接定位，无俯仰 Node（同猪）。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.09, 0.22, -0.80)
                            scale: Qt.vector3d(0.07, 0.09, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.09, 0.22, -0.80)
                            scale: Qt.vector3d(0.07, 0.09, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.09, 0.22, -0.81)
                            scale: Qt.vector3d(0.035, 0.045, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.09, 0.22, -0.81)
                            scale: Qt.vector3d(0.035, 0.045, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                    }
                    Model {
                        // t240 羊（mobType 3）毛茸态：MobModel + mob_sheep 贴图（圆胖躯干 + 小头 + 短腿）。
                        // t241 行走动画 + 吃草低头：walkPhase 驱动腿摆；headPitch 驱动头部俯仰（仅吃草周期内非零，
                        //   headPitchAt 据 eatTimer 返 sin(πp) 包络 → 低头→嚼→抬头；草丛在 C++ tick 内被消耗）。
                        // t300 剪羊毛态：shearedAt=false（未剪羊毛 / 已重新长毛）→ 显本毛茸贴图 Model；sheared=true
                        //   时切到下方裸粉色 Model（互斥 visible，由 revision 触碰刷新）。机制等价 MC 1.0 剪羊毛后
                        //   羊裸露粉色皮肤。
                        visible: {
                            entityManager.revision
                            return entKind === EntityManager.Mob && entMobType === 3
                                   && !entityManager.shearedAt(index)
                        }
                        geometry: MobModel {
                            mobType: 3
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                            headPitch: { entityManager.revision; return entityManager.headPitchAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: { entityManager.revision; return entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight) }
                            baseColorMap: mobSheepTex
                        }
                        // t251 眼睛（羊）：同猪/牛模式（mob Model 子节点纯色 NoLighting），但羊吃草时 MobModel 把头绕
                        //   颈枢俯仰（headPitch<0=低头，几何内部旋转）→ 若眼直接定位则会与俯仰的头脱离（眼悬浮原位、
                        //   头下沉）。故把眼放进一个「颈枢 Node」：position = MobModel 头部颈附着点 (0, 0.10, -0.29)
                        //   （= 头心 cy=0.10、cz+hz=-0.45+0.16；MobModel addHeadRot 绕此点 X 轴旋转），eulerRotation.x 绑
                        //   headPitchAt → 眼随头同步俯仰（QML X 轴旋转与 MobModel addBoxRot 同向，绕同枢 → 视觉一致）。
                        //   眼位置相对颈枢：头前面 abs z=-0.61 → 相对 -0.32；眼上半 abs y≈0.16 → 相对 0.06；x=±0.055。
                        Node {
                            position: Qt.vector3d(0, 0.10, -0.29)
                            // headPitch 用 property 暂存（QML 绑定里 {block} 不能作 Qt.vector3d 内联参数 → 先算成属性）。
                            property real headPitch: { entityManager.revision; return entityManager.headPitchAt(index) }
                            eulerRotation: Qt.vector3d(headPitch, 0, 0)
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(-0.055, 0.06, -0.32)
                                scale: Qt.vector3d(0.05, 0.06, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(0.055, 0.06, -0.32)
                                scale: Qt.vector3d(0.05, 0.06, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(-0.055, 0.06, -0.33)
                                scale: Qt.vector3d(0.025, 0.03, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(0.055, 0.06, -0.33)
                                scale: Qt.vector3d(0.025, 0.03, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                            }
                        }
                    }
                    Model {
                        // t300 羊（mobType 3）裸态：剪羊毛后（shearedAt=true）的羊外观。复用 MobModel 几何（同
                        //   毛茸态四肢 + 头 + 躯干），但去贴图改裸粉肤色 #e8b8b8（机制等价 MC 1.0 剪羊毛后羊裸露
                        //   粉色皮肤；无 mob_sheep 毛茸贴图 → 直接 baseColor 实色渲染，受 terrainLight 调制保昼夜
                        //   明暗 + hurtFlash 红闪仍生效）。与上方毛茸态 Model 互斥 visible（shearedAt 翻转 → 切换）。
                        //   walkPhase / headPitch 同步绑定 → 裸羊照常行走 + 吃草低头动画。
                        //   重长毛（C++ tick 内吃草方块 → sheared=false）→ 上方毛茸 Model 显、本 Model 隐。
                        visible: {
                            entityManager.revision
                            return entKind === EntityManager.Mob && entMobType === 3
                                   && entityManager.shearedAt(index)
                        }
                        geometry: MobModel {
                            mobType: 3
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                            headPitch: { entityManager.revision; return entityManager.headPitchAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面（同毛茸态）
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // 受击红闪覆盖裸粉色（同毛茸态红闪语义）；否则裸粉肤色 × terrainLight 调昼夜明暗。
                            baseColor: { entityManager.revision; return entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : "#e8b8b8" }
                            // 无 baseColorMap → PrincipaledMaterial 走纯 baseColor 实色路径（默认即无贴图）。
                        }
                        // 裸态眼同步（同毛茸态颈枢 Node 结构；复用 headPitchAt 绑头俯仰）。
                        Node {
                            position: Qt.vector3d(0, 0.10, -0.29)
                            property real headPitch: { entityManager.revision; return entityManager.headPitchAt(index) }
                            eulerRotation: Qt.vector3d(headPitch, 0, 0)
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(-0.055, 0.06, -0.32)
                                scale: Qt.vector3d(0.05, 0.06, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(0.055, 0.06, -0.32)
                                scale: Qt.vector3d(0.05, 0.06, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(-0.055, 0.06, -0.33)
                                scale: Qt.vector3d(0.025, 0.03, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                            }
                            Model {
                                geometry: UnitCube {}
                                position: Qt.vector3d(0.055, 0.06, -0.33)
                                scale: Qt.vector3d(0.025, 0.03, 0.02)
                                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                            }
                        }
                    }
                    Model {
                        // t282 蹒跚者（Shambler，mobType 4；机制等价 MC 1.0 僵尸，§9 改名 + 原创模型/贴图）：
                        //   MobModel 人形几何（躯干 + 头 + 双臂前伸僵尸姿态 + 双腿 walkPhase 摆动）+ mob_shambler 贴图。
                        //   近战 AI（detect→pathfind→attack，t281 已就绪）→ 走向玩家攻击；本任务仅交付原创模型 + 贴图。
                        //   walkPhase 绑定驱动双腿绕髋左右反相摆动（biped walk cycle，EntityManager moveSpeed>0 时推进）。
                        visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobShambler
                        geometry: MobModel {
                            mobType: 4
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t282 halfH=0.90 → offset 0（腿底贴 collision 底面）
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // 受击红闪：hurtFlashAt>0 → baseColor=#ff0000 调制贴图全红（同 mobType 0/1/2/3 红闪语义）。
                            baseColor: { entityManager.revision; return entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight) }
                            baseColorMap: mobShamblerTex
                        }
                        // t282 眼睛：亡灵红眼（不沿用猪牛羊的白眼底+深瞳 —— 不死亡灵的赤红发光眼更贴「僵尸」语义，
                        //   且红眼不受身体贴图调制 → 实心红 #b01818 独立 Model，原创纯色 §9a）。mob Model 子节点 →
                        //   继承 bodyYaw（眼朝 AI 行走方向 -Z）+ 父 visible。MobModel 头心 (0,0.57,0) 半 (0.22,0.22,0.22)
                        //   → 前面 z=-0.22；眼在上半 y≈0.62、x=±0.09；z 贴头前面略凸（-0.23，同 t52 贴脸防 z-fight）。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.09, 0.62, -0.23)
                            scale: Qt.vector3d(0.07, 0.08, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#b01818" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.09, 0.62, -0.23)
                            scale: Qt.vector3d(0.07, 0.08, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#b01818" }
                        }
                    }
                    Model {
                        // t284 潜行者（Stalker，mobType 6；机制等价 MC 1.0 苦力怕，§9 改名 + 原创模型/纯色无贴图）：
                        //   MobModel 四短腿 + 高瘦躯干 + 小头（mobType 5）。近距蓄力 → 爆炸（C++ aiStalker 已就绪）。
                        //   walkPhase 绑定驱动四腿对角 walk cycle（EntityManager moveSpeed>0 时推进相位）。
                        //   蓄力膨胀：inflateAt(i) 驱动 Model scale（1+inflate·0.5，机制等价 MC 苦力怕近距蓄力膨胀）+
                        //     baseColor 蓄力发白（绿→白 lerp by inflate；机制等价 MC 苦力怕蓄力发白闪烁）。
                        visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobStalker
                        property real inflate: { entityManager.revision; return entityManager.inflateAt(index) }
                        geometry: MobModel {
                            mobType: 6
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0) // t284 halfH=0.90 → offset 0（腿底贴 collision 底面）
                        // 蓄力膨胀：scale 随 inflate 增长（0 → 1.0、满蓄力 → 1.5；机制等价 MC 苦力怕膨胀）。
                        scale: Qt.vector3d(1.0 + inflate * 0.5, 1.0 + inflate * 0.5, 1.0 + inflate * 0.5)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // 受击红闪优先；否则青绿色（terrainLight 调昼夜暗），蓄力时 lerp 向白（蓄力发白）。
                            baseColor: {
                                entityManager.revision
                                const tl = terrainLight(worldClock.skyLight)
                                if (entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                let r = 0.37, g = 0.66, b = 0.23 // Stalker 青绿色（呈现层视觉约定色，原创）
                                const infl = entityManager.inflateAt(index)
                                if (infl > 0) {
                                    const t = Math.min(1, infl)
                                    r = r * (1 - t) + 1.0 * t
                                    g = g * (1 - t) + 1.0 * t
                                    b = b * (1 - t) + 1.0 * t
                                }
                                return Qt.rgba(r * tl.r, g * tl.g, b * tl.b, 1.0)
                            }
                        }
                        // t284 眼睛：潜行者的深色眼（头部前面，原创纯色 §9a；mob Model 子节点继承 bodyYaw +
                        //   蓄力 scale）。MobModel 头心 (0,0.66,0) 半 (0.15,0.15,0.15) → 前面 z=-0.15；眼 y≈0.68、x=±0.06。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.06, 0.68, -0.17)
                            scale: Qt.vector3d(0.05, 0.06, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.06, 0.68, -0.17)
                            scale: Qt.vector3d(0.05, 0.06, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                    }
                    // t287/t301 Bones（骸骨/骷髅；mobType 5）：MobModel 瘦骨人形 + 持弓（t301：窄躯干/细四肢/小头骨 +
                    //   右手弧形弓几何）。灰白骨色 baseColor（无专属贴图，纯色原创 §9a；弓共用此色 = 骨弓）。受击红闪。
                    //   远程射箭由 EntityManager 负责。
                    Model {
                        visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobBones
                        geometry: MobModel {
                            mobType: 5
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0)
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                entityManager.revision
                                const tl = terrainLight(worldClock.skyLight)
                                if (entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                return Qt.rgba(0.85 * tl.r, 0.84 * tl.g, 0.77 * tl.b, 1.0) // 灰白骨色（弓同色 = 骨弓）
                            }
                        }
                        // t301 骷髅黑色眼窝（头骨标志性的空洞眼窝，纯色 NoLighting §9a；mob Model 子节点继承 bodyYaw +
                        //   父 visible）。区别于 Shambler 的赤红亡灵眼 —— Bones 用纯黑 #1a1a1a 显「空洞眼窝」而非「发光
                        //   眼」，更贴头骨语义。MobModel 头骨心 (0,0.57,0) 半 (0.16,0.18,0.16) → 前面 z=-0.16；眼贴头
                        //   前面 z=-0.17（略凸出防 z-fight，同 t52 贴脸）。眼在上半 y≈0.62（头骨上半 = 眼眶位）、x=±0.06。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.06, 0.62, -0.17)
                            scale: Qt.vector3d(0.06, 0.07, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.06, 0.62, -0.17)
                            scale: Qt.vector3d(0.06, 0.07, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                        }
                    }
                    // t285/t302 Spider（蜘蛛；mobType 7）：MobModel 宽矮躯干 + 前伸小头 + **8 腿**（原创 §9，4 对
                    //   沿躯干 Z 分布；t302 升级自 t285 简化 4 腿。爬墙留后续）。暗黑红 baseColor（纯色原创 §9a）。
                    //   受击红闪。hostile → EntityManager AI 自动追击玩家。t302 加 4 颗红眼（蜘蛛标志性，纯色子 Model）。
                    Model {
                        visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobSpider
                        geometry: MobModel {
                            mobType: 7
                            walkPhase: { entityManager.revision; return entityManager.walkPhaseAt(index) }
                        }
                        position: Qt.vector3d(0, mobModelYOff, 0)
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: {
                                entityManager.revision
                                const tl = terrainLight(worldClock.skyLight)
                                if (entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                return Qt.rgba(0.16 * tl.r, 0.10 * tl.g, 0.10 * tl.b, 1.0) // 暗黑红
                            }
                        }
                        // t302 蜘蛛眼（4 颗红眼；蜘蛛标志性 8 眼简化为 4 颗醒目红眼，原创纯色 NoLighting §9a）：
                        //   mob Model 子节点 → 继承 bodyYaw（眼朝 AI 行走方向 -Z）+ 父 visible。同猪/牛/羊眼模式
                        //   （呈现层独立纯色 Model，不进 MobModel 几何 / 不共享 mob 贴图 → 实心眼色不受身体色调制）。
                        //   Spider 头心 (0,-0.02,-0.32) 半 (0.18,0.14,0.18) → 前面 z=-0.50；眼贴头前面 z=-0.51（略凸出
                        //   防与头部面 z-fight，同 t52 贴脸）。4 颗分上下两对（y=+0.04 / -0.08；x=±0.07），暗体上红眼醒目。
                        //   受击红闪时身体变 #ff0000 → 红眼暂融色（短暂可接受，非 bug；与猪/牛/羊红闪同理）。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.07, 0.04, -0.51)
                            scale: Qt.vector3d(0.05, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.07, 0.04, -0.51)
                            scale: Qt.vector3d(0.05, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(-0.07, -0.08, -0.51)
                            scale: Qt.vector3d(0.05, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.07, -0.08, -0.51)
                            scale: Qt.vector3d(0.05, 0.05, 0.02)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                        }
                    }
                    // t293 mob 碰撞箱仅 F3+B：旧版 t253「准星瞄准的单个 mob 常驻显白色目标框」（hover 即显）
                    //   被用户判为「hover 常显碰撞箱」——MC 1.0 准星瞄准 mob 无 wireframe（仅十字准星），
                    //   故移除该常驻目标框。mob 碰撞箱（WireCube AABB + 朝向箭头）现**仅在 F3+B**（showHitboxes）
                    //   显，符合 spec「mob 碰撞箱仅 F3+B」。攻击命中判定不受影响——beginMining 仍即时调
                    //   findMobHit（点击瞬间最新视线，不读 m_targetedMob 缓存），故移除呈现框不破坏近距攻击。
                    //   分层（PLAN §2）：呈现层只读 showHitboxes + delegate 位置 / halfW / halfH，绝不反向写。
                    // t116/t252/t293 F3+B mob 碰撞箱（spec「mob scale 1.0」+ 朝向箭头）：mob AABB = halfW×halfH×halfW
                    //   （t293 收紧后：pig/sheep 0.8×0.9、cow 0.8×1.0、敌对 0.6×1.8、spider 0.9×0.6；旧版固定 1×1×1）。
                    //   WireCube ±0.5 居中 → scale = (2·halfW, 2·halfH, 2·halfW) 覆盖实际 AABB；+0.01 外扩避与 mob 模型表面 z-fight。
                    //   t239：mob delegate Node 按 bodyYaw 转（AI 行走方向）→ 子节点继承，箭头本地 -Z 指向 mob
                    //   朝向。WireCube 立方对称 → 转无异，但箭头正确反映 yaw（mob facing line，spec「F3+B 显朝向」）。
                    //   分层（PLAN §2）：纯呈现层调试叠层，只读 delegate 位置 / yaw / halfW / halfH。
                    Model {
                        visible: window.showHitboxes
                        geometry: WireCube {}
                        scale: Qt.vector3d(mobHalfW * 2.0 + 0.01, mobHalfH * 2.0 + 0.01, mobHalfW * 2.0 + 0.01)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                    }
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        // 朝向棒：从 collision 中心沿本地 -Z（mob 前 = 头朝向）延伸 mobHalfW+0.05（略超前壁辨识）。
                        //   UnitCube ±0.5 scale sz → 棒长 sz；position z = −sz/2 使棒从 z=0 延伸到 z=−sz（前向）。
                        position: Qt.vector3d(0, 0, -(mobHalfW + 0.05) * 0.5)
                        scale: Qt.vector3d(0.03, 0.03, mobHalfW + 0.05)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                    }
                    // t283 箭矢（Arrow）：骷髅弓箭手远程射出的投射物。细长杆 Model 沿飞行速度定向（yaw + pitch，
                    //   arrowYawAt/arrowPitchAt 据 vel 算）。delegate Node 已摆 position（箭世界坐标）+ 不转（bodyYaw=0
                    //   非 Mob）；本子 Node 据 yaw/pitch 转杆朝飞行方向。机制等价 MC 1.0 骷髅射箭抛物 + 命中伤害；
                    //   名称 / 视觉全原创（§9 区隔，纯色自绘非 MC 美术）。NoLighting（可见 Model 红线）。
                    //   杆本地 -Z = 飞行方向（同 player/mob 模型 -Z 前）；UnitCube ±0.5 scale (0.05,0.05,0.5) → 细杆长 0.5
                    //   沿 Z；position z=-0.25 让杆从中心向前伸（箭头在前）。箭头 / 箭羽为杆子节点同向继承定向。
                    Node {
                        visible: { entityManager.revision; return entKind === EntityManager.Arrow }
                        property real arrYaw: { entityManager.revision; return entityManager.arrowYawAt(index) }
                        property real arrPitch: { entityManager.revision; return entityManager.arrowPitchAt(index) }
                        eulerRotation: Qt.vector3d(arrPitch, arrYaw, 0)
                        // 箭杆（深棕细长杆）
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0, 0, -0.25)
                            scale: Qt.vector3d(0.05, 0.05, 0.5)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#4a3a26" }
                        }
                        // 箭头（杆前端小尖，灰）
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0, 0, -0.52)
                            scale: Qt.vector3d(0.05, 0.05, 0.1)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a8a8a" }
                        }
                        // 箭羽（杆尾小十字，浅色）
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0, 0, 0.02)
                            scale: Qt.vector3d(0.13, 0.02, 0.05)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#cccccc" }
                        }
                    }
                }
            }
        }

        // t88/t114 火把伪光源 + 异形模型：在每个火把方块位置渲染「木柄 + 火焰」两个 Model
        // （NoLighting 高 baseColor 暖色，**非** PointLight）。根因：lit 材质 / PointLight 在本场景不可行
        // （lessons-learned 红线「lit 材质不渲染」），故火把动态光源走伪光源——纯色自发光小立方
        // （动态闪烁焰心），视觉读作「发光点」，不参与场景实际光照计算（真 flood-fill 方块光留 PLAN §M）。
        //
        // t114 异形：mesher 不再为火把画 1×1×1 立方面（chunkgeometry.cpp Torch 特例 continue）→ 火把外观
        // 全部由此 delegate 负责：木柄（细长棕立方）+ 火焰（t260 三层渐变焰：外橙 + 中黄 + 白核 + 闪烁动画）。
        //
        // t157 移除外层光晕：原 delegate 含第三个「光晕」Model（0.42 半透橙立方包覆火焰）—— 此静态大橙
        //   光源在破火把后视觉上残留为「橙色贴图残像」（外层光晕是固定 opacity 无动画的半透立方，常被
        //   读作一片贴图而非发光），且整体观感偏离 MC 火把（MC 火把仅小焰心、无大光晕）。移除后只保留
        //   内层动态焰（torchFlame，闪烁动画），更贴 1.0 + 消除残像；顶部少量烟雾粒子另由 TorchSmoke.qml
        //   经 smokeLoader 加载（见文件下方）补充。
        // t260 多色焰：原单层暖白立方被读作「白炽灯泡」→ 改三层渐变焰（外橙 #ff8a1a / 中黄 #ffd23c / 心
        //   #fff4c4，对齐 default_torch 贴图焰心配色 + MC 火把焰外橙内黄白核），同步 flickerS 缩放跳动 →
        //   读作「跳动的火焰」而非「灯泡」。
        //
        // t114 朝向：运行期据邻居 solid 推断 —— 下格 solid=垂直插地；侧格 solid=横插该向（贴墙伸出）。
        // 邻居查询走 theWorld.isCollidable（BlockRegistry::isSolid 语义；只认实体方块，不挂到另一火把 /
        // 空气）。worldChanged 时重算（破/放邻居后火把朝向随之更新，机制等价 MC「插墙火把朝向跟随墙面」）。
        //
        // 火把位置列表（ListModel）：World::blockPlaced(id=Torch) 时追加、blockBroken 时移除；
        // worldChanged 时校验清理（worldgen 重生会清除旧火把，blockPlaced 不会对 worldgen 触发）。
        // 分层（PLAN §2）：呈现层只消费 World 的 blockPlaced/blockBroken 语义事件，绝不反向写栅格
        // （同 blockBroken→粒子 / spawnItem→实体 模式）。
        //
        // Repeater 父节点 = 场景内 3D Node（torchHost）→ delegate（Node/Model，3D 对象）被领养进 3D
        // 场景图（lessons-learned「动态 3D 对象必须挂到场景 Node，否则孤儿不渲染」—— 同 itemHost /
        // particlesHost 模式）。
        Node {
            id: torchHost
            Component.onCompleted: {
                console.info("[t88] torchHost UP parent=" + torchHost.parent + " (须为 3D Node 非 null)")
            }

            // t170 火把 delegate 实例表：key="x,y,z" → delegate Node（Component.createObject 创建）。
            //   根因：QML Repeater 无法可靠销毁**被 reparent 的 3D delegate** —— QQuickRepeater 的 delegate
            //   跟踪表是 QQuickItem* 类型，3D delegate（QQuick3DNode）不进表；且 onCompleted 的
            //   `parent = torchHost` reparent 把 QObject 所有权转给 torchHost。结果：无论 model 是 ListModel
            //   还是 int-count，count 减小时 Repeater 都找不到 delegate 来销毁 → onDestruction 永不触发 →
            //   挖掉火把后木柄 + 火焰 Model 永久残留（t131 removeTorchAt 逻辑正确但 Repeater 不销毁 delegate
            //   = 治标失效，用户实测「挖掉火把贴图不清除」）。修法：放弃 Repeater，改 Component.createObject
            //   显式创建 + .destroy() 显式销毁（QML 动态对象标准生命周期：createObject 对象由本表 JS 引用
            //   持有、destroy 可靠回收，无 Repeater 中间层）。数据源仍是 torchPositions ListModel（供
            //   TorchSmoke Repeater + findTorchPrefOrient 选中框读取），本表仅管「木柄+火焰」视觉 delegate 的
            //   生死，与 torchPositions 经同一组信号（blockBroken/torchPlaced/worldChanged）同步增删。
            property var torchObjs: ({})

            // 新增火把视觉 delegate（去重：同坐标已存在则跳过）。prefOrient 来自玩家点击面定向。
            function addTorchVis(x, y, z, prefOrient) {
                const key = x + "," + y + "," + z
                if (torchObjs[key]) return
                torchObjs[key] = torchDelegate.createObject(torchHost,
                    {cellX: x, cellY: y, cellZ: z, cellPref: prefOrient || "up"})
            }
            // 移除火把视觉 delegate（.destroy() 可靠回收 Node + 子 Model + 动画）。
            function removeTorchVis(x, y, z) {
                const key = x + "," + y + "," + z
                const o = torchObjs[key]
                if (o) { o.destroy(); delete torchObjs[key] }
            }
            // 兜底清孤儿（worldgen 重生 / setBlockFromEntity 等系统改写栅格不经 blockBroken）：扫表删
            //   blockAt != Torch(13) 的条目，保证视觉 delegate 与栅格真值一致（机制同 torchPositions 的
            //   onWorldChanged 兜底，二者并行各管各的容器）。
            function cleanupVis() {
                for (const key in torchObjs) {
                    const p = key.split(",")
                    if (theWorld.blockAt(parseInt(p[0]), parseInt(p[1]), parseInt(p[2])) !== 13) {
                        torchObjs[key].destroy(); delete torchObjs[key]
                    }
                }
            }
        }

        // t196 / t225 箱子盖子开合动画（场景内 3D Node，与 torchHost / itemHost 同层）。仅当前所开箱子
        //   （chestX/Y/Z）一处显盖子（一次只开一只箱子，chestOpen 单 bool）。chestLidAngle 由 openChest/
        //   closeChest 驱动（window 级 Behavior 平滑过渡 0↔全开角）。
        //
        // t225 朝向 + 格内薄板（修「盖子像占用上一格方块刷新动画」）：
        //   - 朝向：箱子放置时前面（锁面）朝玩家（placeBlock 写 state=horizontalFacing^1）。盖子铰链在锁面
        //     **背侧**（开盖向远离玩家翻）。用「外层 pivot（顶面中心 + Yaw）+ 内层铰链（局部背棱 + X 旋转）」
        //     嵌套，使局部「+Z=背 / -Z=前」约定对所有朝向统一：pivot 的 Yaw 把局部 +Z 转到箱子实际背侧方向
        //     （chestLidYaw 据 chestFacing 算），铰链摆在局部 +Z=+0.5（背棱），其 X 旋转翻起局部 -Z（前缘）。
        //   - 格内薄板：盖子缩为 0.9×0.10×0.9（厚 0.10，远薄于 t196 旧 0.16），摆在箱顶之上 0.005（lidY=0.055）。
        //     closed 平覆箱顶呈「薄盖板」（非旧 0.16 厚「像上一格方块」的板），开合动画贴合格内顶面、读作
        //     「箱子自己的盖子翻开」而非「上一格有块在刷新」。开盖仍会向上翻起（铰链在箱顶棱，机制等价 MC
        //     箱子开盖必向上翻），但薄板 + 锁面朝玩家使动画明确归属本格箱子。
        //
        // 盖子本体 = BlockCube(Chest) 薄板（复用图集 per-face 贴图 chest_top/side/front → 与箱子本体 mesher
        //   整立方同外观，零 MC 资产）。可见性：playing 态且（chestOpen 或 角>0）→ 合盖动画播放期间
        //   （chestOpen 已 false 但角未到 0）盖子仍显，角到位 0 后自动隐。分层（PLAN §2）：纯呈现层，
        //   只读 chestX/Y/Z + chestFacing + chestOpen/chestLidAngle。
        Node {
            id: chestLidPivot
            visible: window.appState === "playing" && (window.chestOpen || window.chestLidAngle > 0.01)
            // 外层 pivot：箱子顶面中心；Yaw 把局部 +Z（背侧）转到箱子实际背侧（chestLidYaw 据朝向算）。
            position: Qt.vector3d(window.chestX + 0.5, window.chestY + 1.0, window.chestZ + 0.5)
            eulerRotation: Qt.vector3d(0, window.chestLidYaw, 0)

            // 内层铰链：局部背棱（local +Z = +0.5，即箱子背侧顶棱中点）；绕局部 X 翻开（chestLidAngle）
            //   → 局部 -Z（前缘 = 锁面侧）上扬 = 开盖向背侧翻（机制等价 MC 箱子后铰链前翻）。
            Node {
                position: Qt.vector3d(0, 0, 0.5)
                eulerRotation: Qt.vector3d(window.chestLidAngle, 0, 0)

                Model {
                    // 盖子薄板：BlockCube 顶点 ±0.5，缩 (0.9, 0.10, 0.9) → 0.9 宽 × 0.10 厚 × 0.9 深 薄板。
                    //   摆位相对铰链：X 居中（0）、Y 顶面之上（+0.055 → 底面 y=cy+1.005 微抬避顶面 z-fight）、
                    //   Z 向前（-0.45 → 覆盖 cell [前 0.10, 背 1.0]，前缘留 0.10 唇）。closed 平覆；随铰链 +X 翻开。
                    geometry: BlockCube { blockId: 22 }   // 22 = BlockRegistry::Chest（与 blockregistry.h Id 枚举同源；同 torch=13 既有字面量 + 注释模式）
                    position: Qt.vector3d(0.0, 0.055, -0.45)
                    scale: Qt.vector3d(0.9, 0.10, 0.9)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
                        baseColor: terrainLight(worldClock.skyLight)  // 与箱子本体同昼夜亮度（顶点光基底由 baseColor 承载）
                    }
                }
            }
        }

        // t170 火把视觉 delegate 模板（木柄 + 火焰）：经 torchHost.addTorchVis 用 Component.createObject
        //   实例化（initial props 注入 cellX/Y/Z/cellPref）、removeTorchVis/cleanupVis 用 .destroy() 回收。
        //   内容与原 Repeater delegate 完全一致（t114 异形木柄+火焰 / t125 朝向 / t150e/f 位姿 / t157 焰心），
        //   仅承载方式从 Repeater 换成手动 createObject（修 Repeater 不销毁 3D delegate 的根因）。
        Component {
            id: torchDelegate
            Node {
                id: torchGlow
                // cell 坐标 + 定向：由 createObject initial properties 注入（非 Repeater model 角色）。
                property int cellX: 0
                property int cellY: 0
                property int cellZ: 0
                property string cellPref: "up"
                // 火把格底面中心（cell [x,x+1]×[y,y+1]×[z,z+1] 的底面中心）；子 Model 在此局部坐标内摆位。
                position: Qt.vector3d(cellX + 0.5, cellY, cellZ + 0.5)

                // t114 朝向态：up=垂直插地；px/nx/pz/nz=横插 ±X/±Z 向（贴对应墙、柄伸向 cell 中央）。
                //   t125：定向权威改为「玩家点击面」（prefOrient，由 placeBlock 经 torchPlaced 传入的命中面
                //   外法线推导），recomputeOrient 优先采用之；旧固定优先级（下>-X>+X>-Z>+Z）仅作退化兜底。
                property string orient: "up"

                // t126 朝向逻辑抽出为顶层 computeTorchOrient（与选中框共用同一份判定，确保两者
                //   orient 永远一致 → 选中框贴合火把实际形状）。语义不变（t125）：优先 prefOrient
                //   （玩家点击面）、其支撑邻居仍实体即采用；否则按下 / 4 侧顺序首个实体邻居兜底；
                //   全无实体则保留玩家意图方向（无 pop-off 机制，宁可按原朝向画也不突兀翻转）。
                function recomputeOrient() {
                    torchGlow.orient = computeTorchOrient(cellX, cellY, cellZ, cellPref)
                }

                Component.onCompleted: {
                    if (parent === null) parent = torchHost  // createObject 已传 torchHost，双保险
                    torchGlow.recomputeOrient()
                }

                // t114：邻居破/放后火把朝向重算（worldChanged 信号）。
                Connections {
                    target: theWorld
                    function onWorldChanged() { torchGlow.recomputeOrient() }
                }

                // 木柄：细长棕立方（UnitCube；原木暗棕 #6b4f24，与木棒 MaterialIcon 同色系）。竖直时贴
                //   cell 底部上伸（柄中心 0.3、scale Y 0.6 → 柄顶 0.6）；墙火把 30° 倾斜上伸 + ±0.30 深嵌
                //   + scale Y 0.7（t150e/f：柄更长、嵌更深、上扬更陡）。scale 走 torchHandleScale（墙 0.7 /
                //   地 0.6），与选中框共用同一份（DRY）。
                Model {
                    geometry: UnitCube {}
                    scale: torchHandleScale(torchGlow.orient)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: "#6b4f24"   // 木柄暗棕（原木色，与木棒图标同色系）
                    }
                    // 柄中心位置（局部坐标，相对 torchGlow 底面中心）。t126 抽出 torchHandleLocalPos
                    //   与选中框共用同一份 switch，确保选中框位姿与渲染出的火把柄完全一致。
                    position: torchHandleLocalPos(torchGlow.orient)
                    // 旋转：墙火把把竖柄（默认沿 +Y）倾斜到对应朝向（t126 抽出 torchHandleEuler 与选中框共用）。
                    //   ±X 向：绕 Z 轴 ±60°；±Z 向：绕 X 轴 ±60°（t132：自 ±90° 水平改 ±60° 上倾）。
                    eulerRotation: torchHandleEuler(torchGlow.orient)
                }

                // 火焰（t260 多色焰）：原单层暖白立方（像白炽灯泡）→ 三层渐变焰（外橙 + 中黄 + 白核），
                //   共享 flickerS 缩放跳动 → 读作「跳动的火焰」而非「灯泡」。配色对齐 default_torch 贴图焰心
                //   （外橙 #ff8a1a / 中黄 #ffd23c / 心 #fff4c4）+ MC 火把焰（外橙内黄白核）。机制等价 MC 1.0
                //   火把光源（仅小焰心 + 顶部偶发烟雾，无大光晕）。三层均 UnitCube + NoLighting（同已验证
                //   可见路径，lessons-learned「所有可见 Model 用 NoLighting」）。
                // 摆在柄顶端（竖直时柄顶 Y=0.65；墙火把 30° 倾斜 + ±0.30 深嵌后柄末端，见 torchFlameLocalPos）。
                //   顶部偶发烟雾粒子由 TorchSmoke.qml 经 smokeLoader 加载（见文件下方）补充。
                Node {
                    id: torchFlame
                    // t150f：焰位读 torchFlameLocalPos（柄 30° 倾斜 + ±0.30 深嵌 + scale 0.7 后末端重算）。
                    position: torchFlameLocalPos(torchGlow.orient)
                    // 闪烁：自定义 flickerS（标量）由 SequentialAnimation 循环驱动；三层 scale 统一由它派生
                    //   （Y 轴略加长 = 火苗上窜感）。本工具链 Vector3DAnimation 未注册（运行期「is not a
                    //   type」），故走「NumberAnimation on 标量属性 + scale 绑定」等价路径（与 cam.shakeYaw
                    //   / itemHost bobY 同 NumberAnimation 模式）。
                    property real flickerS: 0.18
                    // 外焰（橙，最大；三层由大到小嵌套 → 渐变焰心）
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(torchFlame.flickerS, torchFlame.flickerS * 1.10, torchFlame.flickerS)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff8a1a" }
                    }
                    // 中焰（黄，中等）
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(torchFlame.flickerS * 0.74, torchFlame.flickerS * 0.82, torchFlame.flickerS * 0.74)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffd23c" }
                    }
                    // 焰心（暖白，最小、最亮）
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(torchFlame.flickerS * 0.48, torchFlame.flickerS * 0.54, torchFlame.flickerS * 0.48)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#fff4c4" }
                    }
                    SequentialAnimation on flickerS {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.18; to: 0.21; duration: 110 }
                        NumberAnimation { from: 0.21; to: 0.16; duration: 150 }
                        NumberAnimation { from: 0.16; to: 0.19; duration: 90 }
                        NumberAnimation { from: 0.19; to: 0.18; duration: 130 }
                    }
                }
            }
        }
    }

    // t201 水下蓝滤镜：眼位在水里（player.eyeInWater）→ 全屏浅蓝半透叠层，表示玩家潜入水中
    //   （机制等价 MC 水下视野蓝雾）。1/2/3 人称统一（基于眼位 blockAt，与相机模式无关）。放在 View3D
    //   之后、HUD/背包/暂停/死亡叠层之前 → 蓝雾只染 3D 场景，HUD/背包仍清晰可读（同 nightTint 经验：
    //   全屏 tint 不应压暗 HUD / 背包 / 粒子等非场景层）。纯 Rectangle 无 MouseArea → 不拦截鼠标（指针
    //   锁定 / 破放走 main.cpp 事件过滤器，不受此叠层影响）。状态驱动 = player.eyeInWater（tickImpl 每 tick
    //   重算、翻转才发 eyeInWaterChanged，无每帧抖动）。仅 playing 态显（其余态 View3D 已隐）。
    Rectangle {
        anchors.fill: parent
        visible: window.appState === "playing" && player.eyeInWater
        color: Qt.rgba(0.20, 0.45, 0.70, 0.35)
    }

    // t88 火把位置列表（火把伪光源 Repeater 的 model；blockPlaced/blockBroken/worldChanged 维护）。
    ListModel { id: torchPositions }
    // t312 聊天历史（PLAN §2 UI 层呈现态）：{sender, text, isSystem}。玩家消息（openChat 输入）+
    //   系统播报（死亡原因等）共入此列表。单机无联机服务端 → 历史不持久化（回主菜单 / 切世界清空，
    //   见 returnToMenu / enterWorld）。Phase 3 联机接入时改为协议层收发（LocalServer/RemoteServer）。
    //   上下限由 appendChatMessage 维持（chatHistoryMax=50）。
    ListModel { id: chatMessages }
    // t121：原此处为 nightTint 全屏深蓝 Rectangle（地形 NoLighting 不随昼夜变暗的兜底）。
    //   现顶点色承载天光遮蔽 + 材质 baseColor 承载昼夜乘子（见 terrainLight）后已移除该叠层——
    //   全屏 tint 会再压暗 HUD/粒子等非地形层；改走 per-vertex + baseColor 后地形由网格数据精确控光、
    //   其余呈现层（HUD/背包/暂停）不再被夜色叠层误伤。

    // t67 受伤反馈：模型变红 + 视角晃动（替换 t51 的全屏 damageOverlay 红闪）。
    //   根因：旧受伤 = 全屏 #ff0000 半透 Rectangle 淡出 600ms（damageOverlay），playerModel 本身无 hurt
    //   材质绑定。用户要改为「人物模型本身变红 + 视角稍微晃动」。现 PlayerState.damaged(amount) 触发两条
    //   呈现层动画（非全屏叠层）：
    //   (1) hurtAnim：playerModel.hurt 1→0（~0.4s）→ 各身体部件 baseColor 经 hurtTint(hurt,...) lerp 向纯红。
    //   (2) shakeAnim：cam.shakePitch/shakeYaw 衰减抖动 0→±幅→0（~0.2s），叠加进相机 eulerRotation → 视角晃。
    //   连击受伤：start() 重启进行中的动画（重新变红 + 重新晃）。分层（PLAN §2）：呈现层只消费 PlayerState
    //   语义事件，绝不反向写数值（同 fallDamageTaken→takeDamage）。仅 Survival 触发（damaged 由 Survival
    //   掉落伤害经 takeDamage 发出；Creative 无伤 / Spectator noclip 不走重力分支）。
    Connections {
        target: playerState
        function onDamaged(amount) {
            playerModel.hurt = 1.0    // 立即满红（hurtAnim 从 1.0 淡到 0）
            hurtAnim.start()           // 400ms 回正（重启进行中的动画 → 连击重新变红）
            shakeAnim.start()          // 200ms 视角晃动
            // t177 受伤音：与变红 / 视角晃同源事件（PlayerState.damaged）触发，机制等价 MC 玩家受伤声。
            audio.playHurt()
        }
        // t78：死亡 → 释放指针（光标可见点死亡界面按钮）+ 关背包 / 工作台（防面板卡在死亡遮罩下）+
        //   归还光标手持栈（防遗留 heldBlock）。died 由 takeDamage 扣血到 ≤0 时发（仅 Survival 走此路径）。
        //   死亡界面 visible 绑 playerState.dead（deadChanged NOTIFY 自动显），无需在此手动切显隐。
        // t175：归还 held 后调 player.dropAllItems() —— 整个背包（hotbar + main + held）掉落为物品实体
        //   于死亡点 + 清空背包（resetForMode）。掉落先于 release（顺序无强耦合，但逻辑上「死前掉落」）。
        //   玩家随后在死亡界面点「立即重生」→ respawnPlayer → 传回固定出生点（kSpawn，非原地复活）；
        //   掉落物留在死亡点，玩家走回死亡点拾取（机制等价 MC 死亡掉落 + 全局出生点）。
        function onDied() {
            if (window.inventoryOpen) window.inventoryOpen = false
            if (window.craftingTableOpen) window.craftingTableOpen = false
            if (window.furnaceOpen) window.furnaceOpen = false
            if (window.chestOpen) window.chestOpen = false
            if (window.chatOpen) window.chatOpen = false   // t312：死亡关聊天（死亡屏接管光标）
            window.returnHeldToHotbar()
            player.dropAllItems()     // t175：死亡掉落整个背包到死亡点 + 清空背包
            player.release()           // 释放指针 → 光标可见（点「立即重生 / 回主菜单」按钮）
            // t312 死亡播报：聊天栏推一条系统消息（机制等价 MC 1.0 死亡消息「<player> <death reason>」）。
            //   文案 = 玩家名 + 空格 + playerState.deathCauseText（如「玩家 从高处坠落」）。deathCauseText 是
            //   Q_PROPERTY（非 Q_INVOKABLE）→ 属性访问不带括号；带括号会抛 TypeError 致播报静默失败（lessons：
            //   QML/JS 信号处理器内异常被吞、功能静默退化）。t313 死亡屏与本期聊天用同一份 Game 层权威文案。
            window.appendChatMessage("", window.playerName + " " + playerState.deathCauseText, true)
        }
    }

    // 破/放信号 → 粒子迸发（呈现层消费 World 语义事件）。
    // 现代函数式 Connections（Qt6）；粒子降级（Loader.item 为 null）时安全跳过（PLAN §2-E）。
    Connections {
        target: theWorld
        function onBlockBroken(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstBreak(x, y, z, id)
            // t89：破块音（按被破方块 id；AudioManager 当前统一一份，id 留分流接口）。
            audio.playBreak(id)
            // t88：火把被破 → 从伪光源列表移除（id=13=BlockRegistry::Torch；C++ 侧未把枚举暴露 QML，
            // 此处用字面量 13 + 注释，与 blockregistry.h Id 枚举同源）。
            // t170：同步销毁视觉 delegate（木柄+火焰 Model）—— torchPositions 供 TorchSmoke/选中框读，
            //   torchHost.torchObjs 供本场景渲染，二者经同一信号并行增删。
            if (id === 13) { removeTorchAt(x, y, z); torchHost.removeTorchVis(x, y, z) }
            // t173/t179：箱子被破 → 清 ChestStore 该坐标条目（防孤儿内容；机制等价 MC 破箱清空。
            //   spec「破箱掉落内容」属 Phase 1.1+，本轮直接弃内容）。id=22=BlockRegistry::Chest（与
            //   blockregistry.h Id 枚举同源；此处用字面量 + 注释，同 torch=13 / sand=8 既有模式）。
            if (id === 22) chestStore.clearChest(x, y, z)
            // t117：被破格上方若为沙 → 失支撑塌落（maybeTrigger 内部 setBlock(air) 递归触发更上方沙链）。
            maybeTriggerFallingBlock(x, y + 1, z)
        }
        function onBlockPlaced(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstPlace(x, y, z, id)
            // t89：放块音（按新放方块 id）。
            audio.playPlace(id)
            // t125：火把伪光源改由下方 player.onTorchPlaced 追加（需携带玩家点击面法线定向）；本通用
            //   放块信号不再处理火把，避免「两处追加」重复。
            // t32：生存放置消耗 1 件（创造=无限源不耗）。worldgen 经 m_chunks.setBlock 直写、不经
            // World::setBlock → 不会发 blockPlaced；游戏内该信号仅玩家 placeBlock 触发，故此处即
            // 「玩家放置成功」语义事件。ViewModel 观察 World 事件做栈突变（PLAN §2 分层：VM 只依赖
            // World/Game 数据，不反向写）。takeStack 取至 0 → 选中栈变 Air → player.selectedBlock
            // 经 selectedBlockId 绑定变 Air → 右键不再放置（playercontroller 守 Air）。
            // t117：FallingBlock 着地走 World::setBlockFromEntity（不发 blockPlaced）→ 不会误触本分支。
            if (player.mode === PlayerController.Survival)
                hotbarVM.takeStack(hotbarVM.selectedSlot, 1)
            // t117：新放的沙若下方空气 → 自身塌落（玩家在半空放沙立即落）。
            if (id === 8) maybeTriggerFallingBlock(x, y, z)
        }
        // t88：worldgen 重生（seed 变 / 初始生成）清除旧火把 → 伪光源列表校验清理。worldgen 不发
        // blockBroken（m_chunks.setBlock 直写），故旧火把位置不会经 onBlockBroken 移除；此处扫描
        // torchPositions，把已不再是火把的条目删掉。setBlock 编辑也会触发 worldChanged，但此时
        // 火把刚放/刚破已被 onBlockPlaced/broken 同步，本扫描是幂等校验（不重复加 / 不误删）。
        // t131 兜底：作为「清孤儿」的最后一道防线 —— 即便 onTorchPlaced 去重 + removeTorchAt 删全部
        //   仍漏（如 setBlockFromEntity 着地改写 / 未来的实体改写栅格路径不经 blockBroken），本扫描按
        //   blockAt 真值清掉一切 blockAt != Torch 的残留条目，保证光源列表与栅格一致。
        function onWorldChanged() {
            for (let i = torchPositions.count - 1; i >= 0; --i) {
                const e = torchPositions.get(i)
                if (theWorld.blockAt(e.x, e.y, e.z) !== 13) torchPositions.remove(i)
            }
            // t170：同步清视觉 delegate 孤儿（与 torchPositions 兜底并行，各管各的容器）。
            torchHost.cleanupVis()
        }
    }

    // t315 工具耐久归零破损音：Hotbar::damageSelectedItem 归零分支 emit toolBroken(itemId) → 路由到
    //   AudioManager.playToolBreak（呈现层消费语义事件，PLAN §2 分层：音频层只消费、不反向写）。槽位清空在
    //   emit 前已完成（Hotbar 内）；此处仅播音。engine / clip 失败时 AudioManager 内部静默降级（§2-E）。
    Connections {
        target: hotbarVM
        function onToolBroken(itemId) { audio.playToolBreak() }
    }

    // t88 工具：按坐标移除火把伪光源（破块 / 校验清理共用）。从后往前扫，删**所有**匹配 (x,y,z) 的条目。
    //   t131：原版仅删首个即 return —— onTorchPlaced 旧无去重 + 信号竞态会产生同坐标重复条目，单删留孤儿
    //   → 挖掉火把后伪光源仍在（用户实测「火把挖掉光源残留」）。改删全部，配合 onTorchPlaced 源头去重
    //   + onWorldChanged 兜底清孤儿，三重保险确保「挖掉火把 → 光源即消失」。
    function removeTorchAt(x, y, z) {
        for (let i = torchPositions.count - 1; i >= 0; --i) {
            const e = torchPositions.get(i)
            if (e.x === x && e.y === y && e.z === z) torchPositions.remove(i)
        }
    }

    // t125 命中面外法线 → 火把定向串。法线指向玩家侧（射线进入面外法线）：
    //   +Y(顶面)→up 垂直；+X 面→火把贴 -X 墙、柄伸 +X(px)；-X 面→贴 +X 墙、柄伸 -X(nx)；±Z 同理(pz/nz)。
    //   与 torchHost delegate 内柄位姿 switch 同源约定（柄嵌命中面、火焰在柄自由端）。
    function orientFromNormal(nx, ny, nz) {
        if (ny > 0) return "up"
        if (nx > 0) return "px"
        if (nx < 0) return "nx"
        if (nz > 0) return "pz"
        if (nz < 0) return "nz"
        return "up"
    }

    // t126 火把朝向 / 木柄位姿公共逻辑：抽出供「伪光源 delegate」与「选中框」共用，确保两者算出
    //   同一 orient 与同一柄 transform → 选中框贴合火把实际形状（小立柱）而非全格。语义与原 delegate
    //   内联 switch 完全一致，仅去重为单一权威（DRY：改定向规则只改一处）。
    //
    // orient→支撑邻居：up 看下方、px 嵌 -X 墙、nx 嵌 +X 墙、pz 嵌 -Z 墙、nz 嵌 +Z 墙。
    //   用 isCollidable（BlockRegistry::isSolid）—— 只认实体方块，不把另一火把 / 空气当支撑
    //   （避免两火把互挂成悬空）。
    function torchNeighborSolid(o, x, y, z) {
        switch (o) {
        case "up": return theWorld.isCollidable(x, y - 1, z)
        case "px": return theWorld.isCollidable(x - 1, y, z)
        case "nx": return theWorld.isCollidable(x + 1, y, z)
        case "pz": return theWorld.isCollidable(x, y, z - 1)
        case "nz": return theWorld.isCollidable(x, y, z + 1)
        }
        return false
    }

    // 火把有效朝向（原 delegate recomputeOrient 抽出）：优先 prefOrient（玩家点击面），其支撑邻居
    //   仍实体即采用；否则按下 / -X / +X / -Z / +Z 顺序首个实体邻居兜底；全无实体则保留 prefOrient
    //   （悬空不翻转，无 pop-off 机制）。
    function computeTorchOrient(x, y, z, prefOrient) {
        const pref = prefOrient || "up"
        if (torchNeighborSolid(pref, x, y, z)) return pref
        if (theWorld.isCollidable(x, y - 1, z)) return "up"
        if (theWorld.isCollidable(x - 1, y, z)) return "px"
        if (theWorld.isCollidable(x + 1, y, z)) return "nx"
        if (theWorld.isCollidable(x, y, z - 1)) return "pz"
        if (theWorld.isCollidable(x, y, z + 1)) return "nz"
        return pref
    }

    // 火把木柄中心局部位置（相对 cell 底面中心 [x+0.5, y, z+0.5]）。竖直时贴 cell 底上伸（柄中心 0.3）；
    //   t150e 墙火把沿墙法线偏移 ±0.30（旧 ±0.20 → 深嵌：柄根更贴墙、视觉更牢嵌墙面），Y=0.5。
    function torchHandleLocalPos(o) {
        switch (o) {
        case "up": return Qt.vector3d(0.0, 0.30, 0.0)
        case "px": return Qt.vector3d(-0.30, 0.50, 0.0) // 贴 -X 墙、柄伸 +X（t150e 深嵌）
        case "nx": return Qt.vector3d( 0.30, 0.50, 0.0) // 贴 +X 墙、柄伸 -X（t150e 深嵌）
        case "pz": return Qt.vector3d(0.0, 0.50, -0.30) // 贴 -Z 墙、柄伸 +Z（t150e 深嵌）
        case "nz": return Qt.vector3d(0.0, 0.50,  0.30) // 贴 +Z 墙、柄伸 -Z（t150e 深嵌）
        }
        return Qt.vector3d(0.0, 0.30, 0.0)
    }

    // t150e 火把木柄 scale：墙火把柄加长 0.6→0.7（spec「柄 scale 0.7」），配合 ±0.30 深嵌 + 30° 上倾
    //   （t150f），柄伸出更长、火焰更高。地面立柱保持 0.6（加长会下沉穿地：中心 0.30 + 半 0.35 = 0.65
    //   顶、−0.05 底穿地）。选中框镜像同值（贴合实际形状，非全格）。
    function torchHandleScale(o) {
        return o === "up" ? Qt.vector3d(0.12, 0.6, 0.12) : Qt.vector3d(0.12, 0.7, 0.12)
    }

    // 火把木柄世界位置 = cell 底面中心 + 局部位姿。供选中框 Model.position 直接绑定。
    function torchHandleWorldPos(x, y, z, o) {
        const lp = torchHandleLocalPos(o)
        return Qt.vector3d(x + 0.5 + lp.x, y + lp.y, z + 0.5 + lp.z)
    }

    // 火把木柄 euler 旋转：墙火把把竖柄（默认沿 +Y）自竖直倾 ~30°（**非** 90° 水平贴墙）——
    //   柄自墙根斜向上伸（柄端高于柄根，机制等价 MC 墙火把上倾），不再是水平贴墙。
    //   ±X 向：绕 Z 轴 ±30°；±Z 向：绕 X 轴 ±30°；up 不转。
    //   t132：原 ±90°（水平）改 ±60°（倾斜）；t150f：±60° → ±30°（更陡上扬，柄端更高、贴近 MC 墙火把观感）。
    function torchHandleEuler(o) {
        switch (o) {
        case "px": return Qt.vector3d(0, 0, -30)
        case "nx": return Qt.vector3d(0, 0,  30)
        case "pz": return Qt.vector3d( 30, 0, 0)
        case "nz": return Qt.vector3d(-30, 0, 0)
        }
        return Qt.vector3d(0, 0, 0)
    }

    // 火把火焰局部位置（相对 cell 底面中心）= 木柄末端（柄中心 + 沿「旋转后柄轴」半柄长到自由端）。
    //   t150f：柄改 30° 上倾 + ±0.30 深嵌 + 墙柄半长 0.35（scale 0.7）后重算。半柄长沿墙法线分量 =
    //   0.35·sin30°=0.175、垂直分量 0.35·cos30°≈0.303。墙柄中心 (±0.30, 0.50, ±0.30) + 这两分量：
    //     px：(-0.30+0.175, 0.50+0.303, 0) = (-0.125, 0.803, 0) → 取 (-0.13, 0.80, 0)
    //   up：柄竖直半长 0.30，焰心略高于柄顶（0.65 vs 柄顶 0.60）让焰立方叠在柄顶端（不变）。
    function torchFlameLocalPos(o) {
        switch (o) {
        case "up": return Qt.vector3d(0.0, 0.65, 0.0)
        case "px": return Qt.vector3d(-0.13, 0.80, 0.0)
        case "nx": return Qt.vector3d( 0.13, 0.80, 0.0)
        case "pz": return Qt.vector3d(0.0, 0.80, -0.13)
        case "nz": return Qt.vector3d(0.0, 0.80,  0.13)
        }
        return Qt.vector3d(0.0, 0.65, 0.0)
    }

    // t126 查 torchPositions 里某 cell 的 prefOrient（玩家放置时记的命中面定向）；未找到返回 "up"
    //   （理论上不会出现 —— 火把仅经 onTorchPlaced 入表、worldChanged 校验清理；退化安全）。
    function findTorchPrefOrient(x, y, z) {
        for (let i = 0; i < torchPositions.count; ++i) {
            const e = torchPositions.get(i)
            if (e.x === x && e.y === y && e.z === z) return e.prefOrient || "up"
        }
        return "up"
    }

    // t125 火把放置 → 追加伪光源 + 记录玩家点击面定向（prefOrient）。携带命中面外法线的语义事件由
    //   placeBlock 发出（见 playercontroller torchPlaced）；呈现层据此定向，绝不反向写栅格（PLAN §2 分层）。
    //   火把生命周期仍对称走 World 信号（破 → onBlockBroken 移除 / worldChanged 校验清理），此处仅「加」。
    //   t131 去重：插入前查同坐标已存在则跳过 —— placeBlock 走 setBlock(Torch) 后才发 torchPlaced，但
    //   信号竞态 / CD 前的连点 / 重入路径可能对同坐标二次追加，旧版无条件 append 产生重复条目。源头去重
    //   与 removeTorchAt（删全部）+ onWorldChanged（兜底清孤儿）共同保证挖掉火把后光源不留残。
    Connections {
        target: player
        function onTorchPlaced(x, y, z, nx, ny, nz) {
            const orient = orientFromNormal(nx, ny, nz)
            // t170：先建视觉 delegate（addTorchVis 自带去重）；与 torchPositions 数据追加并行。
            torchHost.addTorchVis(x, y, z, orient)
            for (let i = 0; i < torchPositions.count; ++i) {
                const e = torchPositions.get(i)
                if (e.x === x && e.y === y && e.z === z) return
            }
            torchPositions.append({x: x, y: y, z: z, prefOrient: orient})
        }
    }

    // t117/t220 沙子重力触发：查 (x,y,z) 是否为沙且**下方非完整立方支撑** → 先把沙格置 air（经 World::setBlock
    //   发 blockBroken 递归触发上方沙链）再 spawn 下落方块实体。仅 id=8（BlockRegistry::Sand）参与；其余方块无重力。
    //   t220「仅完整方块可支撑沙」：下方为完整立方（isFullCubeAt）→ 有支撑不落；下方为 air / 水 / 不完整方块
    //   （火把 / 半砖 / ...）→ 失撑触发下落（沙落水穿透填堵水格、沙遇不完整方块变掉落物 由 EntityManager.tick
    //   落体判定）。旧版查「下方非空气」把水 / 火把 / 半砖当支撑，致沙卡在水上一格 / 粘在火把上（t220 (b)(c)）。
    //   「先置 air 再 spawn」使链式塌落自然：setBlock(air) → blockBroken(x,y,z,Sand) → onBlockBroken 再查
    //   (x,y+1,z) 沙并递归 trigger（沙柱一次塌完，机制等价 MC 沙链）。
    //   分层（PLAN §2）：呈现层（Main.qml）消费 World 语义事件（blockPlaced/broken）→ EntityManager 生成
    //   实体；实体物理（重力 / 着地）由 Game/Entities 层 tick 自治（同 spawnItem→掉落物 模式），呈现层不反向写。
    function maybeTriggerFallingBlock(x, y, z) {
        if (y < 0 || y >= theWorld.height) return
        if (x < 0 || z < 0 || x >= theWorld.width || z >= theWorld.depth) return
        if (theWorld.blockAt(x, y, z) !== 8) return // 仅沙（BlockRegistry::Sand=8）
        // t220：仅完整立方可支撑沙。下方为完整立方 → 有支撑不落；下方为 air/水/不完整方块 → 失撑触发。
        if (y > 0 && theWorld.isFullCubeAt(x, y - 1, z)) return
        // 下方失撑 → 触发：先置 air（递归触发上方沙链），再 spawn 下落实体。
        theWorld.setBlock(x, y, z, 0)
        entityManager.spawnFallingBlock(x, y, z, 8)
    }

    // [t55] 诊断：HUD hotbar 刷新追踪。slotsChanged（setStack/addStack/takeStack/resetForMode）时打印
    //   全 9 槽 id + 选中槽，与 hotbar.cpp 的 `[inv] addStack ... slots=[...]` 交叉验证 ——
    //   若 [inv] 有数据但 [hud] 这行没出 = NOTIFY 未到 QML（绑定断）；两行都有但 HUD 仍空 = 绑定重算
    //   了但取值错（应不可能，blockIdAt 直读 m_slots）；最常见是 [hud] 这行出了 + HUD 图标随之刷新
    //   = 修复生效。无论 hotbarBar 是否可见都打（playing 全程诊断），便于和 [inv] 同时间线对照。
    Connections {
        target: hotbarVM
        function onSlotsChanged() {
            console.info("[hud] slotsChanged rev=" + hotbarVM.slotRevision
                + " ids=[" + hotbarVM.blockIdAt(0) + " " + hotbarVM.blockIdAt(1) + " " + hotbarVM.blockIdAt(2)
                + " " + hotbarVM.blockIdAt(3) + " " + hotbarVM.blockIdAt(4) + " " + hotbarVM.blockIdAt(5)
                + " " + hotbarVM.blockIdAt(6) + " " + hotbarVM.blockIdAt(7) + " " + hotbarVM.blockIdAt(8) + "]"
                + " sel=" + hotbarVM.selectedSlot)
        }
    }

    // 键盘：G 切模式、1–9 直选 hotbar 槽、WASD/Space/Shift 传给控制器。Esc 由 C++ 事件过滤器拦截。
    // 注：原 1/2/3 用于直选模式，现让位给 hotbar（t06 验收要求 1–9 选槽）；模式切换统一由 G 循环
    // （N 与数字键无冲突认知，但 G 是更通用的「Game mode」约定，避免与未来键位争用）。
    // 切换在指针捕获与未捕获时都可用 —— keyInput 始终持焦点（未捕获时也可预选槽）。
    Item {
        id: keyInput
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if (e.isAutoRepeat) return                               // 忽略自动重复（否则长按空格反复触发双击→飞行闪烁）
            // t312 聊天栏打开：T / Enter（playing 且非死亡态且无背包/工作台/熔炉/箱子面板开）。机制等价
            //   MC 1.0 T 打开聊天。Enter 与 T 同义（spec「T/Enter 打开聊天栏」）；Esc/E 在背包面板作关面板，
            //   故聊天打开键不与 Esc/E 冲突（聊天开着时 Esc 由 chatInput 自己处理关聊天，见下方 TextField）。
            //   聊天开期间 keyInput 不持焦点（chatInput 持焦）→ movement 键不透传 player（无需额外守卫，
            //   与背包面板同模式）。
            if ((e.key === Qt.Key_T || e.key === Qt.Key_Return || e.key === Qt.Key_Enter)
                    && window.appState === "playing" && !playerState.dead
                    && !window.inventoryOpen && !window.craftingTableOpen && !window.furnaceOpen && !window.chestOpen
                    && !window.chatOpen) {
                window.openChat()
                e.accepted = true; return
            }
            // 背包（t18）：E 开关。Esc 在背包打开时关闭（captured=false 时 Esc 不被 C++ 事件过滤器
            // 拦截，落到 QML；captured=true 时 Esc 仍走 C++ → release → 暂停叠层，原行为不变）。
            // t50：工作台面板同样 E/Esc 关（与背包互斥）。t87：熔炉面板亦同（E / Esc 关）。
            if (e.key === Qt.Key_E && window.appState === "playing") {
                if (window.craftingTableOpen) window.closeCraftingTable()
                else if (window.furnaceOpen) window.closeFurnace()
                else if (window.chestOpen) window.closeChest()
                else window.toggleInventory()
                e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.inventoryOpen) {
                window.closeInventory(); e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.craftingTableOpen) {
                window.closeCraftingTable(); e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.furnaceOpen) {
                window.closeFurnace(); e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.chestOpen) {
                window.closeChest(); e.accepted = true; return
            }
            // F3 调试叠层切换（t10，PLAN §2-F）：playing 态按 F3 显/隐左上角调试文本。
            //   t143：同时跟踪 f3Held=true（无条件，menu 态也设，与 shiftHeld 同模式），供 B 键修饰判定。
            //   f3Visible 仅 playing 态 toggle（menu 态主菜单全屏覆盖，叠层不可见）；切换不依赖指针捕获。
            if (e.key === Qt.Key_F3) {
                window.f3Held = true
                if (window.appState === "playing") window.f3Visible = !window.f3Visible
                e.accepted = true; return
            }
            // F3+B 碰撞箱（t116/t143，PLAN §2-F）：MC F3+B 真实语义——B 键仅在 F3 按住（f3Held）时
            //   toggle showHitboxes，即「按住 F3 同时按 B」的组合键（F3 作 B 修饰键），而非「先开叠层再按 B」。
            //   showHitboxes 独立于 f3Held/f3Visible：松开 F3 后碰撞箱仍显直到再按 F3+B 关（同 MC 行为）。
            if (e.key === Qt.Key_B && window.appState === "playing" && window.f3Held) {
                window.showHitboxes = !window.showHitboxes; e.accepted = true; return
            }
            // t277 F3+G 区块边界显示（机制等价 MC F3+G chunk boundary display）：G 键仅在 F3 按住
            //   （f3Held）时 toggle showChunkBounds（与上方 F3+B 同「F3 作修饰键」语义）。showChunkBounds
            //   独立于 f3Held/f3Visible：松开 F3 后边界线仍显直到再按 F3+G 关（同 MC 行为）。
            //   仅 playing 态 toggle（叠层在 View3D 场景内，菜单态不可见）。
            if (e.key === Qt.Key_G && window.appState === "playing" && window.f3Held) {
                window.showChunkBounds = !window.showChunkBounds; e.accepted = true; return
            }
            if (e.key === Qt.Key_F5) { player.cycleCamera(); e.accepted = true; return } // 相机模式循环（t27）
            if (e.key === Qt.Key_F6) { worldClock.toggleDebugFast(); e.accepted = true; return } // 昼夜调试加速（t09）
            if (e.key === Qt.Key_G) { player.cycleMode(); e.accepted = true; return }
            // t239 调试：按 M 在玩家前方生成一个测试 mob（验证 AI wander / 重力 / 碰撞 / 受击 / 死亡基类）。
            //   t243 spawn eggs 落地后此键可移除；现阶段无 spawn 入口（t142 已删旧测试 mob），靠它让 base 可观测。
            //   生成在玩家脚底前 2 格、高 1 格（重力 tick 落到地表）；走过去可推动，左键攻击路径待 t242 接。
            if (e.key === Qt.Key_M && window.appState === "playing") {
                const fp = player.feetPosition
                const lv = player.lookVector
                entityManager.spawnMob(Math.floor(fp.x + lv.x * 2), Math.floor(fp.y) + 1, Math.floor(fp.z + lv.z * 2))
                e.accepted = true; return
            }
            // t229 Q / Ctrl+Q 丢弃热键（spec「第一人称 Q=丢 1 / Ctrl+Q=丢整栈（手持槽）；背包内悬停槽
            //   Q=丢 1 / Ctrl+Q=丢整栈。适用所有背包面板」）。Ctrl 修饰由 e.modifiers 直接判（窗口级键盘事件，
            //   无需常驻 ctrlHeld 态；同 shiftHeld 跟踪不同的是 Q 非移动键、press 即早退无需 release 守卫）。
            //   背包开 + 悬停某槽 → 从**悬停槽**丢（Q=1件 / Ctrl+Q=整栈；任意组 main/hotbar/craft/in/fuel/
            //   out/chest，经 InventoryOps.readSlot/writeSlot 路由）；否则（游戏内 / 未悬停）→ 从**选中槽**丢
            //   （dropHeld/dropHeldStack 自检捕获态：未捕获/背包开时早退，与既有行为不回退）。Q 始终早退 →
            //   不透传 player.setKey（Q 非移动键）。
            if (e.key === Qt.Key_Q) {
                const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen
                const ctrl = (e.modifiers & Qt.ControlModifier) !== 0
                if (bagOpen && window.hoveredSlotKey !== "") {
                    window.dropFromHoveredSlot(ctrl)        // 背包内悬停槽：Ctrl=整栈 / 否则 1 件
                } else if (!bagOpen) {
                    if (ctrl) player.dropHeldStack()         // 第一人称 Ctrl+Q 丢选中槽整栈
                    else      player.dropHeld()              // 第一人称 Q 丢选中槽 1 件（t36，行为不变）
                }
                e.accepted = true; return
            }

            // t110 背包开时的 Shift / 数字键守卫（spec「背包开时 Shift/数字键不透传到 player」+「数字键交换」）。
            //   根因：原代码无差别地把 Shift 与数字键透传给 player —— Shift 进 m_keys → 关包后 release 已清，
            //   但若「关包瞬间 Shift 仍按住」会重新捕获进 m_keys → 蹲下；数字键在背包开时仍改 selectedSlot，
            //   与背包内整理物品的语义冲突（MC 1.0 背包开时数字键 = 与该 hotbar 槽交换 hover 物品，非切选中）。
            const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen

            // Shift：始终追踪 shiftHeld（供背包槽 TapHandler 读做 Shift+左键搬运）；背包开时不透传给 player。
            //   shiftHeld 不论背包开关都更新 —— 闭包后若用户仍按住 Shift，下一次 TapHandler 读到的 shiftHeld
            //   仍准确（松开时 Keys.onReleased 翻回 false）。
            if (e.key === Qt.Key_Shift) {
                window.shiftHeld = true
                if (bagOpen) { e.accepted = true; return }    // 蹲下守卫：背包开时不让 Shift 进 player.m_keys
            }

            if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {            // 1–9 直选 hotbar 槽 0..8（属性赋值走 WRITE setter）
                if (bagOpen) {
                    // 数字键：背包开 + 当前 hover 槽 → 与 hotbar[idx] 整栈互换（MC 1.0）；无 hover → 不动 selectedSlot。
                    if (window.hoveredSlotKey !== "") window.swapHoveredWithHotbar(e.key - Qt.Key_1)
                } else {
                    hotbarVM.selectedSlot = e.key - Qt.Key_1
                }
                e.accepted = true; return
            }
            player.setKey(e.key, true)
        }
        Keys.onReleased: (e) => {
            if (e.isAutoRepeat) return
            // t143：F3 松开同步 f3Held=false（无条件，与 shiftHeld 同模式）；F3 不透传 player.setKey。
            if (e.key === Qt.Key_F3) { window.f3Held = false; return }
            // t110：Shift 松开同步 shiftHeld；背包开时不透传 player.setKey（与 press 守卫对称，防 Shift 状态
            //   与 player.m_keys 不同步）。非 Shift / 非背包态照旧透传。
            if (e.key === Qt.Key_Shift) {
                window.shiftHeld = false
                const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen
                if (bagOpen) return
            }
            player.setKey(e.key, false)
        }

        // 光标位置追踪已上移到独立 cursorTrackLayer（z=250，高于背包/工作台/熔炉面板 z=150）——
        // 见下方浮动光标前。原放 keyInput（z=0）内被 z=150 面板截断 hover → 浮动光标按下期间
        // point.position 停旧位（偏离鼠标，拿起卡顿）。t107 修复。

        // 滚轮循环切换 hotbar。WheelHandler 按指针位置抓取，与光标显隐/锁定无关，
        // 故捕获与未捕获都生效。只消费滚轮 —— 鼠标按键仍走 C++ 窗口级事件过滤（破/放）。
        // t140：背包 / 工作台 / 熔炉面板打开时滚轮应滚动面板（调色板 Flickable），不切 hotbar。
        //   未守时背包内滚轮会偷切选中槽 → 右键放置方块跟着变，与「调面板里滚轮浏览」的预期冲突。
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                if (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen) return
                if (window.chatOpen) return   // t312：聊天开时不切 hotbar（打字时滚轮误触；聊天 input 顶层已捕获，双重保险）
                // t210 滚轮行为按模式切换：观察者（spectator）滚轮调 flySpeedMul（有效 4..20 blocks/sec，
                //   前滚加速 / 后滚减速）；创造 / 生存滚轮恒切 hotbar 槽（无论是否在飞）。即「滚轮语义」由
                //   player.mode 单一派生，不再混入 player.flying 运动态——创造飞态滚轮也能选 hotbar（t159 旧逻辑
                //   把创造-飞 与 spectator 一并判为飞态调速，创造飞时滚轮选不了槽，违 MC 1.0：滚轮=选槽，飞行
                //   速度无滚轮旋钮）。§2-D 单一输入路径：滚轮事件只此一处消费，按模式分流。
                if (player.mode === PlayerController.Spectator) {
                    if (event.angleDelta.y > 0)      player.adjustFlySpeed(+1)
                    else if (event.angleDelta.y < 0) player.adjustFlySpeed(-1)
                } else {
                    if (event.angleDelta.y > 0)      hotbarVM.scroll(-1) // 上滚 → 左移（下标-1，环绕）
                    else if (event.angleDelta.y < 0) hotbarVM.scroll(1)  // 下滚 → 右移
                }
            }
        }
    }

    // 暂停 / 未捕获 覆盖层（仅 playing 态）：点击任意处 → 进入（锁定指针）。
    // 主菜单态（appState="menu"）不显本叠层（由 MainMenu 覆盖全屏）。
    // 背包 / 工作台打开时（同为 !captured 态）抑制本叠层 —— 三者互斥，避免面板下面透出暂停叠层。
    // t78：死亡态（playerState.dead）也抑制本叠层 —— 死亡时同样 !captured，但应由死亡界面（z=180）接管，
    //   不让「点击恢复」的暂停叠层透出（死亡必须走按钮，不可点击恢复）。
    // t312：聊天栏打开时（同为 !captured 态）抑制本叠层 —— 聊天 input（z=170）接管光标打字。
    Item {
        id: pauseOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && !player.captured
                 && !window.inventoryOpen && !window.craftingTableOpen && !window.furnaceOpen && !window.chestOpen
                 && !playerState.dead
                 && !window.chatOpen
        z: 100
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.55)
            // 点击恢复游戏（t139：设置面板开时不响应背景点击 → 必须先「返回」关设置面板）。
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (window.settingsOpen) return
                    player.grab(); keyInput.forceActiveFocus()
                }
            }
        }
        Rectangle {
            width: 360; height: 250; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.centerIn: parent; spacing: 12
                Text { text: "PAUSED"; color: "#eeeeee"; font.pixelSize: 26; font.bold: true
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "click to play"; color: "#bbbbbb"; font.pixelSize: 15
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[G] cycle mode   [1-9] select block   wheel cycle   [F5] camera"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[Esc] release   WASD move   dbl-tap W sprint   Shift crouch/sneak   Space jump/fly"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[LMB] break block   [RMB] place block   [Q] drop item"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[T] / [Enter] open chat   (Enter send · Esc cancel)"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[F6] toggle fast day/night (" + (worldClock.debugFast ? "ON · ~30s" : "OFF · ~20min") + ")"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[F3] toggle debug overlay (fps / frame / cpu ms / draw-calls / mesh)   [F3+B] toggle hitboxes   [F3+G] toggle chunk bounds"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                // 按钮行：t139 设置 + 返回主菜单。Row 居中，两按钮间距 10。
                Row {
                    spacing: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    // 设置（t139）：进入设置面板（手臂调试 ArmSlider 等）。消费点击，不冒泡到背景 grab。
                    Rectangle {
                        width: 110; height: 32; radius: 6
                        color: settingsBtnArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                        border.color: "#3a5a7a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "设置"
                               color: "#7fb0e5"; font.pixelSize: 13 }
                        MouseArea {
                            id: settingsBtnArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.settingsOpen = true
                        }
                    }
                    // t176 保存并退出到世界列表（原「Main Menu」：退出即落盘 + 回世界列表，机制等价 MC「保存并退出」）。
                    //   消费点击，不冒泡到背景 grab。
                    Rectangle {
                        width: 180; height: 32; radius: 6
                        color: backMenuArea.containsMouse ? "#2a3a2a" : "#1a2a1a"
                        border.color: "#3a6a3a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "保存并退出"
                               color: "#7fe57f"; font.pixelSize: 13 }
                        MouseArea {
                            id: backMenuArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.saveAndExitToWorldList()
                        }
                    }
                }
            }
        }

        // t139 设置面板：暂停叠层「设置」按钮触发（settingsOpen）。覆盖在暂停叠层之上（z 同 pauseOverlay
        //   内更高层），含手臂调试 ArmSlider（从 SurvivalInventory 调试区迁来：baseTilt/posXYZ 实时调）。
        //   仅 settingsOpen 显；「返回」按钮关回暂停菜单。背景遮罩仅吸收点击（§9 lessons「全屏遮罩 onClicked
        //   会误关」→ 此处无 close 语义，纯防穿透到背后暂停叠层的恢复 grab）。
        Item {
            anchors.fill: parent
            visible: window.settingsOpen
            z: 50 // 在 pauseOverlay 内暂停内容之上
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.7)
                MouseArea { anchors.fill: parent; onClicked: {} } // 吸收点击，不穿透到背后暂停叠层
            }
            Rectangle {
                width: 440; height: 580; radius: 10
                anchors.centerIn: parent
                color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
                Column {
                    anchors.fill: parent; anchors.margins: 20; spacing: 10
                    Text { text: "设置"; color: "#eeeeee"; font.pixelSize: 22; font.bold: true
                           anchors.horizontalCenter: parent.horizontalCenter }
                    // 手臂调试（从生存背包 t129 调试区迁入）：滑动写回 window 级属性 → viewModelHand
                    //   绑定读取 → 第一人称手臂 baseTilt / position 实时变。
                    Text { text: "手臂调试（实时）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    ArmSlider {
                        width: parent.width
                        label: "baseTilt (°)"
                        from: -180; to: 180
                        value: window.handBaseTilt
                        onValueChanged: window.handBaseTilt = value
                    }
                    ArmSlider {
                        width: parent.width
                        label: "position.x"
                        from: -0.5; to: 0.5
                        value: window.handPosX
                        onValueChanged: window.handPosX = value
                    }
                    ArmSlider {
                        width: parent.width
                        label: "position.y"
                        from: -0.5; to: 0.5
                        value: window.handPosY
                        onValueChanged: window.handPosY = value
                    }
                    ArmSlider {
                        width: parent.width
                        label: "position.z"
                        from: -0.5; to: 0.5
                        value: window.handPosZ
                        onValueChanged: window.handPosZ = value
                    }
                    Text { text: "⚠ position.z < -0.3 会穿模（当前 -0.39 为 t156 用户选定）；滑动条仅微调，默认值已固化"
                           color: "#b08060"; font.pixelSize: 10; wrapMode: Text.WordWrap; width: parent.width }
                    // t166b 阴影开关（用户「加开关打开关闭阴影，在阴影质量上面」）：关 → 全 chunk 跳过 PCF 软影
                    //   （meshing 提速 / 诊断卡顿是否阴影所致）。自定义开关（Rectangle + 勾），不依赖 CheckBox import。
                    Text { text: "阴影（PCF 软影）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    Row {
                        spacing: 8
                        Rectangle {
                            id: shadowToggleBox
                            width: 22; height: 22; radius: 4
                            color: window.shadowsEnabled ? "#2a5a3a" : "#2a2a2a"
                            border.color: window.shadowsEnabled ? "#5fe57f" : "#555555"; border.width: 1
                            Text { anchors.centerIn: parent; text: window.shadowsEnabled ? "✓" : ""
                                   color: "#7fe57f"; font.pixelSize: 16; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: window.shadowsEnabled = !window.shadowsEnabled }
                        }
                        Text { text: window.shadowsEnabled ? "开（软影，较吃性能）" : "关（无影，更快 — 测卡顿用）"
                               color: "#cccccc"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    }
                    // t166 暗度参数：minLight 滑条（terrainLight floor）。越大夜间/阴暗越亮（用户「黑的地方稍太黑」可调）。
                    Text { text: "暗度（minLight：夜间/阴暗最低亮度）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    ArmSlider {
                        width: parent.width
                        label: "minLight"
                        from: 0.20; to: 0.60
                        value: window.minLight
                        onValueChanged: window.minLight = value
                    }
                    // t166c 手持方块位置（用户「加滑动条调第一人称手上方块位置」）：相对 t156 基线的偏移。
                    Text { text: "手持方块位置（相对手腕前方偏移）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    ArmSlider {
                        width: parent.width
                        label: "heldBlock.x"
                        from: -0.5; to: 0.5
                        value: window.heldBlockX
                        onValueChanged: window.heldBlockX = value
                    }
                    ArmSlider {
                        width: parent.width
                        label: "heldBlock.y"
                        from: -0.5; to: 0.5
                        value: window.heldBlockY
                        onValueChanged: window.heldBlockY = value
                    }
                    ArmSlider {
                        width: parent.width
                        label: "heldBlock.z"
                        from: -0.5; to: 0.5
                        value: window.heldBlockZ
                        onValueChanged: window.heldBlockZ = value
                    }
                    // 返回按钮：关设置面板回暂停菜单。
                    Rectangle {
                        width: 120; height: 32; radius: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: backSettingsArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                        border.color: "#3a5a7a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "返回"
                               color: "#7fb0e5"; font.pixelSize: 13 }
                        MouseArea {
                            id: backSettingsArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.settingsOpen = false
                        }
                    }
                }
            }
        }
    }
    // t78 死亡界面（仅 Survival 血量 0 触发）：半透黑遮罩 + 「你死了」+ 死因文案 + [立即重生] / [回主菜单]。
    //   触发链：PlayerState.takeDamage 扣血到 ≤0 → dead=true + emit died → 上方 Connections(onDied) 释放指针
    //   + 关背包/工作台；本叠层 visible 绑 playerState.dead（deadChanged NOTIFY 自动显隐）。
    //   z=180（高于暂停 100 / 背包 150，低于主菜单 200 → 「回主菜单」后 MainMenu 覆盖）。
    //   立即重生 = window.respawnPlayer()（满血 + 传回出生点 + 重新 grab）；回主菜单 = window.deathReturnToMenu()
    //   （清死亡态 + returnToMenu）。遮罩 MouseArea 仅吸收点击（死亡态不可点恢复，必须走按钮；§9 lessons
    //   「全屏遮罩 onClicked 会让点外部误关」 → 这里无 close 语义，纯吸收防穿透到背后游戏层）。
    //   分层（PLAN §2）：死亡态属 PlayerState（Game 层），呈现层只读消费 + 按钮调 Q_INVOKABLE（不反向写数值）。
    //   GUI 自绘原创（Rectangle 组合，无 MC GUI PNG；§9 override (a)）。
    // t313 死因文案：标题「你死了」下显一行 `<玩家> <死因>`（机制等价 MC 1.0 死亡屏消息），文案与 onDied
    //   路由的聊天播报（同文件）**同格式、同源**（playerName + 空格 + playerState.deathCauseText()）——
    //   spec 要求「两处:聊天+死亡屏」用同一份 Game 层权威文案（deathCauseText），呈现层不另存副本。
    //   deadChanged 置 dead=true 同帧已发 deathCauseChanged（playerstate.cpp takeDamage 致死分支），故本绑定的
    //   deathCauseText 在死亡屏显出时已是本局致死来源（非上一局残留）。respawn 复位死因 → 死亡屏隐时同步清空。
    Item {
        id: deathOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && playerState.dead
        z: 180
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.65)
            MouseArea { anchors.fill: parent; onClicked: {} } // 吸收点击，不穿透到背后游戏层
        }
        Rectangle {
            width: 360; height: 244; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.centerIn: parent; spacing: 22
                // 标题 + 死因文案（t313）：内层紧排（spacing 6），作为外层 Column 单个子项，不打乱按钮间距。
                Column {
                    spacing: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    Text {
                        text: "你死了"
                        color: "#ff5555"; font.pixelSize: 30; font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                        style: Text.Outline; styleColor: "#000000"
                    }
                    // t313 死因副标题（灰白小字 + 黑描边，亮/暗背景均可读）：玩家名 + 死因，与聊天播报同格式。
                    Text {
                        // deathCauseText 是 Q_PROPERTY（READ 访问器、非 Q_INVOKABLE）→ 属性访问不带括号
                        //   （带括号会在绑定求值时抛 TypeError「not a function」，死亡屏死因显示为空）。
                        text: window.playerName + " " + playerState.deathCauseText
                        color: "#c8c8c8"; font.pixelSize: 15
                        anchors.horizontalCenter: parent.horizontalCenter
                        style: Text.Outline; styleColor: "#000000"
                    }
                }
                // 立即重生（主操作，绿色强调；按钮风格对齐 MainMenu）
                Rectangle {
                    width: 220; height: 44; radius: 8
                    color: deathRespawnArea.containsPress ? "#335c33"
                          : deathRespawnArea.containsMouse ? "#4f8a4f" : "#3a6a3a"
                    border.color: "#7fe57f"
                    border.width: deathRespawnArea.containsMouse ? 2 : 1
                    Behavior on border.width { NumberAnimation { duration: 80 } }
                    anchors.horizontalCenter: parent.horizontalCenter
                    Text { anchors.centerIn: parent; text: "立即重生"
                           color: "#eaf6ea"; font.pixelSize: 16; font.bold: true }
                    MouseArea {
                        id: deathRespawnArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.respawnPlayer()
                    }
                }
                // 回主菜单（次操作，中性；对齐 MainMenu「Quit」配色）
                Rectangle {
                    width: 220; height: 44; radius: 8
                    color: deathMenuArea.containsPress ? "#272f37"
                          : deathMenuArea.containsMouse ? "#333c47" : "#222a32"
                    border.color: "#3a444f"
                    border.width: deathMenuArea.containsMouse ? 2 : 1
                    Behavior on border.width { NumberAnimation { duration: 80 } }
                    anchors.horizontalCenter: parent.horizontalCenter
                    Text { anchors.centerIn: parent; text: "回主菜单"
                           color: "#cdd6dd"; font.pixelSize: 16 }
                    MouseArea {
                        id: deathMenuArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.deathReturnToMenu()
                    }
                }
            }
        }
    }

    // t312 聊天显示叠层（PLAN §2 UI 层，纯呈现）：左下角显示最近若干条聊天 / 系统消息，玩家消息显
    //   「名: 文本」、系统消息（isSystem=true，如死亡播报）显灰红文本无「名:」前缀。机制等价 MC 1.0
    //   聊天行（左下贴底、半透描边、随新消息滚入）。
    //   显隐：playing 且（指针捕获中 = 正常游戏 / 聊天 input 开着 / 死亡态）才显；暂停 / 背包 / 菜单态隐
    //   （被更高 z 叠层覆盖或非游戏态）。z 默认 95（高于 HUD/F3 z=50，低于暂停 100 → 暂停时被遮，符合「暂停
    //   不显聊天」）；死亡态抬到 185（高于死亡遮罩 180、低于主菜单 200）—— 死亡时 onDied 已 release 指针 +
    //   关聊天（captured/chatOpen 俱假），若仍按原 visible/z 则刚推入的死亡播报被死亡遮罩盖住看不见（t327）。
    //   GUI 自绘原创（Text + 半透背板，无 MC GUI PNG；§9 override (a)）。
    Item {
        id: chatDisplay
        // t327：补 playerState.dead —— 死亡时 captured=false 且 chatOpen=false，否则死亡播报虽已 append 进
        //   chatMessages 但本叠层 visible=false 不可见（用户报「聊天栏没播报」根因；与死亡屏同文案须能显）。
        visible: window.appState === "playing" && (player.captured || window.chatOpen || playerState.dead)
                 && chatMessages.count > 0
        // 锚左下：底部留出 hotbar 高度（hotbar 底边距 18 + 槽高 46 + vitalsBar ~20 ≈ 90），左贴边 12。
        //   不与底部居中的 hotbar 重叠（hotbar 居中、聊天靠左，水平错开）。
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 96
        width: 460
        z: playerState.dead ? 185 : 95   // t327：死亡时抬到死亡遮罩(180)之上显播报，低于主菜单(200)

        // 由下往上排列最近 N 条（count 受 chatHistoryMax 限；显示窗最多 chatVisibleLines 行，超出由
        //   ListView 自管滚动 / 裁剪）。用 ListView 而非 Column：ListView 对 append/remove 有原生动画 +
        //   自动滚到底（positionViewAtEnd），无需手动管布局。verticalLayoutDirection BottomToTop 让最新
        //   在底部（贴近 input 输入位置 / 符合聊天从下往上堆叠的观感）。
        readonly property int chatVisibleLines: 10
        readonly property int chatLineHeight: 18

        ListView {
            id: chatListView
            anchors.fill: parent
            // 从底向顶布局：index 0（最早）在顶、最新（count-1）在底。新消息 append 后滚到底（最新可见）。
            verticalLayoutDirection: ListView.BottomToTop
            interactive: window.chatOpen   // 仅聊天开着时可滚回顾历史；游戏中（捕获态）非交互（防误触）
            clip: true
            model: chatMessages
            // delegate：单行 Text。玩家消息「名: 文本」（名段浅蓝 + 文本白，富文本分段染色）；系统消息
            //   （isSystem，如死亡播报）整行灰红纯文本、无「名:」前缀。HTML 转义防注入（用户文本 / 名进 span）。
            delegate: Item {
                width: chatDisplay.width
                height: msgText.implicitHeight
                Text {
                    id: msgText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    font.pixelSize: 14
                    style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.85)   // 描边 → 亮/暗地形背景均可读
                    // 系统消息纯文本走 Text.color（灰红）；玩家消息富文本（span 内显式设色，Text.color 被覆盖）。
                    color: model.isSystem ? "#ff8088" : "#ffffff"
                    textFormat: model.isSystem ? Text.PlainText : Text.RichText
                    // 单一 text 绑定（避免 onTextChanged 改 text 的写后读时序坑）：系统消息原样；玩家消息拼富文本。
                    text: {
                        const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                        if (model.isSystem) return model.text
                        return "<span style=\"color:#a8c8ff\">" + esc(model.sender)
                             + ":</span> <span style=\"color:#ffffff\">" + esc(model.text) + "</span>"
                    }
                }
            }
            // 自动滚到底（最新可见）：count 变（append / remove 溢出）时定位到末尾。
            onCountChanged: Qt.callLater(positionViewAtEnd)
        }
    }

    // t312 聊天淡出计时器：游戏中（非聊天开态）新消息显若干秒后整体淡出（贴近 MC 旧消息渐隐观感）。
    //   仅触发一次淡出动画（chatDisplay.opacity 1→0），重新 append 重启计时器并复位 opacity=1。
    //   聊天开着（chatOpen）时不淡出（用户在打字 / 读历史）。
    Timer {
        id: chatFadeTimer
        interval: 8000
        repeat: false
        onTriggered: {
            if (!window.chatOpen) chatFadeOut.start()
        }
    }
    // 淡出动画（单 NumberAnimation）。无 alwaysRunToEnd：新消息到来时 Connections 先 stop() 再复位
    //   opacity=1.0 —— alwaysRunToEnd 会让 stop() 后动画仍跑到末尾（opacity→0）覆盖复位，造成「新消息也淡」。
    NumberAnimation {
        id: chatFadeOut
        target: chatDisplay; property: "opacity"; to: 0.0; duration: 1200; easing.type: Easing.OutQuad
    }
    // appendChatMessage 调 chatFadeTimer.restart()；restart 前先把 opacity 复位（停掉进行中的淡出，重新可见）。
    //   用 Binding 监听 count 变化（restart 已在 appendChatMessage 内做，此处仅复位 opacity）。
    Connections {
        target: chatMessages
        function onCountChanged() { chatFadeOut.stop(); chatDisplay.opacity = 1.0 }
    }

    // t312 聊天输入栏（仅 chatOpen 时显）：左下贴底、TextInput + 提示符「>」+ 半透背板。z=170（高于暂停 100 /
    //   HUD 50，低于死亡 180 → 死亡时死亡屏仍在上，但死亡态不开聊天故不冲突）。Enter 发送、Esc 取消（关闭）。
    //   用 TextInput（纯 QtQuick）而非 TextField —— Main.qml 是根文档，顶层 import QtQuick.Controls 是硬加载
    //   期依赖（部署缺口 → app exit -1，见 lessons-learned「顶层 import 是硬依赖」）；TextInput 无此风险，
    //   且 WorldList.qml 已用同模式（项目未链接 Qt6::QuickControls2，TextInput 是既已验证的可编辑文本路径）。
    //   聊天 input 取焦点后 keyInput 不再透传 movement 键给 player（打字期间玩家静止，机制等价 MC 聊天冻结输入）。
    //   分层（PLAN §2）：纯呈现层；输入文本不入 Game/World 层（单机无服务端），Phase 3 联机时改走协议层收发。
    Item {
        id: chatInputBar
        visible: window.appState === "playing" && window.chatOpen
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 96
        width: 460
        height: 30
        z: 170
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.55)
            radius: 4
        }
        // 提示符「>」（MC 风格聊天前缀；浅灰）。
        Text {
            id: chatPrompt
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: ">"
            color: "#b0b0b0"
            font.pixelSize: 14
            font.bold: true
        }
        TextInput {
            id: chatInput
            anchors.left: chatPrompt.right
            anchors.leftMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            color: "#ffffff"
            font.pixelSize: 14
            selectByMouse: true
            clip: true
            verticalAlignment: Text.AlignVCenter
            // 聚焦后立刻全选（防 appendChatMessage 残留旧文本；首次打开文本恒空故无害，双保险）。
            onActiveFocusChanged: if (activeFocus) selectAll()
            // Enter 发送 / Esc 取消。Esc 走 Keys（非 C++ 事件过滤器：聊天态 !captured，Esc 不被拦截落 QML）。
            Keys.onPressed: (event) => {
                if (event.isAutoRepeat) return
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    window.sendChat()
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    chatInput.text = ""    // 取消丢弃草稿
                    window.closeChat(true)
                    event.accepted = true
                }
            }
        }
    }

    // 准星（仅 playing 且捕获时）：中心十字，白色核心 + 黑色描边 → 亮/暗背景均可见。
    // 自绘原创（Rectangle 组合，非 MC GUI PNG；§9 override (a)）。Spectator 亦显（导航辅助）。
    Item {
        visible: window.appState === "playing" && player.captured
        anchors.centerIn: parent
        width: 24; height: 24
        Rectangle { color: "#000000"; anchors.centerIn: parent; width: 22; height: 4 } // 横 描边
        Rectangle { color: "#000000"; anchors.centerIn: parent; width: 4; height: 22 } // 竖 描边
        Rectangle { color: "#ffffff"; anchors.centerIn: parent; width: 20; height: 2 } // 横 核心
        Rectangle { color: "#ffffff"; anchors.centerIn: parent; width: 2; height: 20 } // 竖 核心
    }

    // HUD：模式 + 地面状态 + FPS（仅 playing 态显）
    Text {
        visible: window.appState === "playing"
        x: 12; y: 8
        color: "#7fe57f"; font.pixelSize: 20; font.bold: true
        style: Text.Outline; styleColor: "#000000"
        text: "MODE: " + (player.mode === PlayerController.Spectator ? "SPECTATOR (noclip · no break/place)" :
               player.mode === PlayerController.Creative ? ("CREATIVE " + (player.flying ? "(flying)" : "(walk · dbl-tap Space to fly)")) : "SURVIVAL (gravity + jump)")
              + "    FPS: " + window.fps
    }
    Text {
        visible: window.appState === "playing"
        x: 12; y: 36
        color: "#cccccc"; font.pixelSize: 13
        text: "pos: " + player.position.x.toFixed(1) + ", " + player.position.y.toFixed(1) + ", " + player.position.z.toFixed(1)
              + "   yaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch)
              + (player.captured ? ("   ground: " + (player.onGround ? "yes" : "no")
                                   + "   held: " + hotbarVM.nameAt(hotbarVM.selectedSlot)
                                   + " (#" + player.selectedBlock + ")"
                                   + " ×" + hotbarVM.countAt(hotbarVM.selectedSlot)) : "   pointer free")
    }

    // F3 调试叠层（t10，PLAN §2-F）：左上角多行调试文本，绑各运行期计数 / 玩家态 / chunk 网格统计，
    // 用于诊断帧抖与 meshing 吞吐（§2-F：无此叠层则帧率验收无法诊断）。F3 切换显隐；仅 playing 态显；
    // z 高于 HUD（叠层 z=50，HUD 默认 0）；不遮挡准星核心区（准星居中，叠层在左上角）。
    //
    // 分层（§2）：叠层属 UI；计数一律从 World/Renderer 的公共属性取（chunk 数读 theWorld.chunksX/Z、
    // 顶点/三角面读各 ChunkGeometry.vertexCount/triangleCount），**不**在 UI 层持有副本。各 geo_NN 的
    // NOTIFY=meshRebuilt → 任一 chunk 重建即刷新汇总（编辑后立刻反映新顶点数）。
    //
    // t178 帧时间切分（PLAN §4 验收）：fps 旁加 frameMs(=1000/fps) + cpu sim ms（player.simMs，主线程 tick
    //   1s 平均）；draw-call 改为**估算值**（≈，非伪造：chunks 地形+水两段 + 掉落物 + mob + 火把 + ~6 固定
    //   场景 Model）—— QtQuick3D 路径仍不暴露逐帧真值，真计时 / 真 draw-call 待自研 RHI 迁移（QRhiGpuTimer）。
    //   mesh 行加模式（greedy/culled）—— greedy 顶点/三角大幅降，可观测 PLAN §4 性能打磨成效。
    Text {
        visible: window.appState === "playing" && window.f3Visible
        x: 12; y: 62
        z: 50
        color: "#ffff00"                        // 单色（黄）+ 黑描边，亮/暗背景均高对比可读
        style: Text.Outline; styleColor: "#000000"
        font.pixelSize: 12; font.family: "monospace"
        text: {
            // t276 全幅 chunk 顶点 / 三角面汇总（meshVertices / meshTriangles 标量由每个地形段 meshRebuilt
            //   经 Connections → recomputeMeshStats 增量刷新；F3 只读标量，不把 var 数组进 text 绑定 → 无 loop）。
            //   旧 5×5 仅汇总中心 3×3（geo00..geo22），动态化后改为全幅（更诚实的 mesh 预算观测）。
            const vx = window.meshVertices, tr = window.meshTriangles
            const modeName = player.mode === PlayerController.Spectator ? "SPECTATOR"
                           : player.mode === PlayerController.Creative ? "CREATIVE" : "SURVIVAL"
            const camName = player.cameraMode === PlayerController.FirstPerson ? "1st"
                          : player.cameraMode === PlayerController.ThirdPersonBack ? "3rd-back" : "3rd-front"
            // t51：移动态（walk/sprint/crouch）入 F3 叠层，便于核对疾跑 / 蹲下触发。
            const moveName = player.moveState === PlayerController.Sprint ? "sprint"
                           : player.moveState === PlayerController.Crouch ? "crouch" : "walk"
            // t276：chunk 列数读 window.worldChunksPerSide（单一权威），不读 theWorld.chunksX/Z —— World.width/depth
            //   现为 QML 绑定（← worldChunksPerSide），在 text 绑定里读 theWorld.chunksX（NOTIFY widthChanged）会与
            //   width 绑定链构成 QML binding loop（误报，但留 WRN）。worldChunksPerSide 是常量 int，读它无 loop。
            const ncx = window.worldChunksPerSide, ncz = window.worldChunksPerSide
            // t178 帧时间切分（PLAN §4 验收「写死帧时间切分 CPU/GPU ms + draw-call 预算」）：
            //   - frameMs：总帧预算 = 1000/fps（fps=0 → 0，防除零）。
            //   - cpuSimMs：主线程 tick() CPU 耗时 1s 平均（player.simMs；物理/射线/实体/挖掘/拾取）。
            //   - drawEst：估算 draw-call 数（chunks 地形+水两段 + 掉落物 + mob + 火把 + ~6 固定场景 Model：
            //     太阳/玩家模型/手/选框/裂纹/粒子；明确标 ≈ 因 QtQuick3D 路径不暴露逐帧真值，spec 禁伪造）。
            //   GPU 真计时 / 逐帧 draw-call 待自研 RHI 迁移（QRhiGpuTimer / RHI stats）。
            const frameMs = window.fps > 0 ? (1000.0 / window.fps) : 0.0
            // t256：draw 估算用 liveCount（活体实体）非 count（含已 release 的空槽）—— 空槽 delegate
            //   visible=false 不参与绘制，count 会高估（slot-reuse 后 count=槽位数 ≠ 渲染实体数）。
            const itemLive = itemEntities.liveCount(), mobLive = entityManager.liveCount()
            const drawEst = ncx * ncz * 2 + itemLive + mobLive + torchPositions.count + 6
            const meshMode = window.greedyMeshing ? "greedy" : "culled"
            return "voxelsandbox  [F3 debug]"
                 + "\nfps: " + window.fps + "  frame: " + frameMs.toFixed(1) + "ms  cpu sim: " + player.simMs.toFixed(2) + "ms"
                 + "\npos: " + player.position.x.toFixed(2) + "  " + player.position.y.toFixed(2) + "  " + player.position.z.toFixed(2)
                 + "  (feet " + player.feetPosition.x.toFixed(1) + "," + player.feetPosition.y.toFixed(1) + "," + player.feetPosition.z.toFixed(1) + ")"
                 + "\nyaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch) + "  look " + camName
                 + "\nmode: " + modeName + (player.flying ? " (fly)" : "") + "  move: " + moveName + "  ground: " + (player.onGround ? "yes" : "no")
                 + "\nspeed: " + player.speed.toFixed(2) + " b/s" // t159：实际水平速度（位移/dt；含疾跑/飞/水下倍数/撞墙归零）
                 + (player.flying || player.mode === PlayerController.Spectator
                    ? "  fly: " + player.flySpeed.toFixed(1) + " b/s (x" + player.flySpeedMul.toFixed(2) + ")"
                    : "") // t159/t210：飞态额外报当前有效飞速 + 倍数（仅 spectator 滚轮可调；创造飞态恒 x1.00）
                 + (player.hasHit ? "  hit: " + player.hitBlock.x + "," + player.hitBlock.y + "," + player.hitBlock.z : "  hit: -")
                 // t276：world 行读 worldChunksPerSide（权威）+ theWorld.height（literal，非绑定，无 loop），
                 //   不读 theWorld.width/depth（绑定链 → binding loop）。t307：height 64→128。
                 + "\nworld: " + (window.worldChunksPerSide * 16) + "×" + (window.worldChunksPerSide * 16) + "×" + theWorld.height
                 + "  chunks: " + ncx + "×" + ncz + " = " + (ncx * ncz) + " (all meshed)"
                 + "\nmesh: " + meshMode + "  vertices: " + vx + "  triangles: " + tr // t178：mesh 模式 + 顶点/三角（greedy 大幅降）
                 + "\ndraw-calls: ~" + drawEst + "  (chunks×2 " + (ncx * ncz * 2) + " + items " + itemLive
                 + " + mobs " + mobLive + " + torches " + torchPositions.count + " +6 scene)  threads: 0/0 (sync meshing)"
                 + "\nday: phase " + worldClock.dayPhase.toFixed(2) + "  sky " + worldClock.skyLight.toFixed(2)
                 + (worldClock.debugFast ? "  (fast)" : "")
                 + "\n[B] hitboxes: " + (window.showHitboxes ? "ON" : "off")
                 + "   [G] chunk bounds: " + (window.showChunkBounds ? "ON" : "off")
        }
    }

    // Hotbar（9 槽，1.0 风格）：底部居中，方形凹槽槽框 + 选中槽选框（凸起边框，随 selectedSlot 位移）。
    // 全部槽框/选框/准星为本项目自绘原创（Rectangle 组合，无外部 MC PNG；§9 override (a)）。
    // Spectator 模式不显（1.0 spectator 无 hotbar）；保留 1–9/滚轮选槽 + 选中高亮。
    Item {
        id: hotbarBar
        visible: window.appState === "playing"
                 && player.mode !== PlayerController.Spectator
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        width: 9 * hotbarBar.slotSize
        height: hotbarBar.slotSize

        readonly property int slotSize: 46 // 单格方形尺寸（图标 38 居中，留井边呼吸）
        readonly property int selMargin: 1 // 选框相对槽的外扩（凸起边框略大于槽）

        // t315 HUD hotbar 悬停 tooltip：指针位于某槽时显其名（工具附「耐久: cur/max」行）。指针锁定 FPS 瞄
        //   准态下 HoverHandler 不触发（光标居中隐藏），仅指针释放（暂停 / 面板开但未盖底栏）时生效。hoveredSlot
        //   = 当前指针所在槽下标（-1=无）；每槽 HoverHandler onHoveredChanged 维护（进入写、离开按 index 守卫清除，
        //   防相邻槽进出竞态互清，同背包面板 hoveredKey 模式）。触碰 slotRevision 令槽内容改写后 tooltip 刷新。
        property int hoveredSlot: -1
        // 当前 hover 槽的物品 id（tooltip 显名 / 耐久行用）。空槽 / 无 hover → 0（tooltip 不显）。
        property int hoveredItemId: {
            hotbarVM.slotRevision  // 触碰：栈写入后重算
            return hotbarBar.hoveredSlot >= 0 ? hotbarVM.blockIdAt(hotbarBar.hoveredSlot) : 0
        }

        // 9 个方形凹槽槽框（sunken bevel：顶/左 1px 暗边=阴影、底/右 1px 亮边=受光 → 凹陷观感）。
        Row {
            id: slotRow
            spacing: 0
            Repeater {
                // t55 修复（HUD hotbar 拾取/放入后不显图标）：
                //   根因 —— 旧 `model: { slotRevision; slotList() }` 用 JS 数组（QVariantList）作 Repeater
                //   model。slotRevision 变时绑定虽重算、返回新数组，但**新数组长度恒 9（不变）** →
                //   QQuickRepeater 视作「count 未变」→ **复用既有 delegate、不重建**，而 modelData 在
                //   delegate 创建期一次性注入、不复算 → 图标 / 数量永停在初值（全空）。背包内面板
                //   「能看见」是假象：面板 visible 切换时其 Repeater delegate 被销毁重建，偶发读到新值；
                //   持久 HUD 的 Repeater 自启动起 9 delegate 一直复用 → 必然空。
                //
                //   修法（参照 SurvivalInventory 主栏已验证的 revision-touch 模式）：改用**固定整数 model**
                //   （slotCount=9，CONSTANT → delegate 一次创建永驻），把「刷新」责任从「Repeater 重建」
                //   下放到**每个依赖槽内容的绑定**：每绑定显式 `触碰 slotRevision`（Q_PROPERTY，NOTIFY=
                //   slotsChanged）→ NOTIFY 触发该绑定重算 → 经 Q_INVOKABLE blockIdAt(index)/countAt(index)
                //   取**最新**栈值。Q_INVOKABLE 返回值本身不被 NOTIFY 跟踪，但只要同绑定内先读了 NOTIFY
                //   属性（slotRevision），整绑定就挂在该信号上 → slotsChanged 后重算。这与 t97 主栏
                //   （mainRevision 触碰 + mainBlockIdAt(index) 读 VM）同构，是「栈写入靠版本号 NOTIFY
                //   触发刷新」的通用稳健写法（不依赖 Repeater 对等长数组的重建行为，那在不同 Qt 版本表现不一致）。
                model: hotbarVM.slotCount
                delegate: Item {
                    width: hotbarBar.slotSize
                    height: hotbarBar.slotSize
                    Rectangle { anchors.fill: parent; color: "#2f2f2f" } // 槽内深色井底
                    // 凹陷斜面：顶/左 1px 暗边（阴影）
                    Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                    Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                    // 凹陷斜面：底/右 1px 亮边（受光）
                    Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                    Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }

                    // 物品图标：方块段 → 等距立方体 Image；工具段（t33，isTool）→ ToolIcon Canvas 自绘像素镐；
                    // 材料段（t50 木棒，isMaterial）→ MaterialIcon 自绘（§9a，非 MC 资产）。空槽 id=0 → 不显。
                    // 三者同尺寸同居中、互斥 visible。每绑定触碰 slotRevision → 拾取/放入后重算 blockIdAt(index)。
                    Item {
                        anchors.centerIn: parent
                        width: 38; height: 38
                        visible: { hotbarVM.slotRevision; return hotbarVM.blockIdAt(index) !== 0 }
                        Image {
                            anchors.fill: parent
                            visible: { hotbarVM.slotRevision; const id = hotbarVM.blockIdAt(index)
                                       return !hotbarVM.isTool(id) && !hotbarVM.isMaterial(id) }
                            source: { hotbarVM.slotRevision; return hotbarVM.iconSourceForBlock(hotbarVM.blockIdAt(index)) }
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        ToolIcon {
                            anchors.fill: parent
                            visible: { hotbarVM.slotRevision; return hotbarVM.isTool(hotbarVM.blockIdAt(index)) }
                            tier: { hotbarVM.slotRevision; return hotbarVM.toolTier(hotbarVM.blockIdAt(index)) }
                            toolType: { hotbarVM.slotRevision; return hotbarVM.toolType(hotbarVM.blockIdAt(index)) }
                        }
                        MaterialIcon {
                            anchors.fill: parent
                            visible: { hotbarVM.slotRevision; return hotbarVM.isMaterial(hotbarVM.blockIdAt(index)) }
                            materialId: { hotbarVM.slotRevision; return hotbarVM.blockIdAt(index) }
                        }
                    }
                    // 栈数量（t32）：count>1 时右下角显数字（MC 风格：单件不显数）。触碰 slotRevision 刷新
                    // （countAt 是 Q_INVOKABLE，靠版本号触发）。白字黑描边保证亮/暗槽底均可读。
                    Text {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 3
                        anchors.bottomMargin: 1
                        visible: { hotbarVM.slotRevision; return hotbarVM.countAt(index) > 1 }
                        text: { hotbarVM.slotRevision; return hotbarVM.countAt(index) }
                        color: "#ffffff"
                        style: Text.Outline; styleColor: "#000000"
                        font.pixelSize: 14; font.bold: true
                    }

                    // t315 工具耐久条：槽底薄条，宽 ∝ remaining/max，色绿(>50%)/黄(20–50%)/红(<20%)。
                    //   仅工具（isTool）且 remaining<max（满耐久工具不显条）时可见。触碰 slotRevision 令耐久
                    //   消耗后重算（durabilityAt / toolMaxDurability 是 Q_INVOKABLE，靠版本号触发）。机制等价
                    //   MC 1.0 工具耐久条（绿色随耗变黄转红、满耐久隐）；原创自绘 Rectangle，零 MC 资产（§9）。
                    Item {
                        id: durabilityBar
                        property int curDur: { hotbarVM.slotRevision; return hotbarVM.durabilityAt(index) }
                        property int maxDur: { hotbarVM.slotRevision; return hotbarVM.toolMaxDurability(hotbarVM.blockIdAt(index)) }
                        property real ratio: maxDur > 0 ? curDur / maxDur : 0.0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 3
                        anchors.rightMargin: 3
                        anchors.bottomMargin: 2
                        height: 3
                        visible: { hotbarVM.slotRevision
                            const id = hotbarVM.blockIdAt(index)
                            return hotbarVM.isTool(id) && durabilityBar.curDur > 0 && durabilityBar.curDur < durabilityBar.maxDur }
                        // 槽底凹槽底色（耐久条的「空段」背景，凸显已耗部分）。
                        Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.55 }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width * (parent.maxDur > 0 ? parent.curDur / parent.maxDur : 0)
                            // 绿 >50% / 黄 20–50% / 红 <20%（MC 1.0 耐久条配色量级）。
                            color: parent.ratio > 0.5 ? "#5fd35f" : (parent.ratio >= 0.2 ? "#e8e85a" : "#e05050")
                        }
                    }

                    // t315 HUD hotbar 悬停 tooltip：进入写 hoveredSlot、离开按 index 守卫清除（防相邻槽进出竞态
                    //   互清，同背包面板 hoveredKey 模式）。指针锁定 FPS 瞄准态不触发（光标居中隐藏）。
                    HoverHandler {
                        id: slotHover
                        onHoveredChanged: {
                            if (hovered) hotbarBar.hoveredSlot = index
                            else if (hotbarBar.hoveredSlot === index) hotbarBar.hoveredSlot = -1
                        }
                    }

                    // [t55] 诊断：delegate 创建时打一行 —— 确认恰好 9 个 delegate 生成 + 初始 id（启动全空=0）。
                    // 若日志只见 9 行且 id=0，之后拾取无新日志 = delegate 未重建（预期，因 model 是常数）；
                    // 此时刷新靠下方 onSlotsChanged 打印 + 各绑定 slotRevision 触碰（应见图标更新）。
                    Component.onCompleted: console.info("[hud] slot " + index + " delegate created id=" + hotbarVM.blockIdAt(index))
                }
            }
        }

        // 选中槽选框（raised bevel：顶/左 亮、底/右 暗 → 凸起观感），随 selectedSlot 位移。
        // 单独 overlay（不放进 Repeater）→ 选中态唯一；Behavior 让 1–9/滚轮切换有平滑滑动感。
        Item {
            x: hotbarVM.selectedSlot * hotbarBar.slotSize - hotbarBar.selMargin
            y: -hotbarBar.selMargin
            width: hotbarBar.slotSize + 2 * hotbarBar.selMargin
            height: hotbarBar.slotSize + 2 * hotbarBar.selMargin
            Behavior on x { NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }
            // 选框：四边统一白色（用户反馈「只左/上白、右/下灰不协调」→ 去掉 raised bevel 的暗底/右）。
            Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.top: parent.top }
            Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.left: parent.left }
            Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.bottom: parent.bottom }
            Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.right: parent.right }
        }

        // t315 HUD hotbar 悬停 tooltip（纯 QtQuick 自绘；同背包面板 t94 模式，不引入 QtQuick.Controls）。
        //   显 hoveredSlot 槽物品名；工具附「耐久: cur/max」行（spec「name\n\n耐久: x/x」）。非工具 / 空槽 / 无
        //   hover → 不显。触碰 slotRevision 令槽内容改写后刷新（durabilityAt 等是 Q_INVOKABLE，靠版本号触发）。
        //   定位：水平居中于 hoveredSlot 上方，左右贴边时夹紧；垂直在 hotbar 行之上（y=-height-6），顶部空间
        //   不足（极小窗口）翻到行下方。
        Rectangle {
            id: hudTip
            visible: hotbarBar.hoveredItemId !== 0 && hudTipLabel.text !== ""
            z: 1000
            width: hudTipLabel.implicitWidth + 14
            height: hudTipLabel.implicitHeight + 8
            color: "#101216"
            opacity: 0.94
            border.color: "#3a444f"
            border.width: 1
            radius: 3
            x: {
                const cx = hotbarBar.hoveredSlot * hotbarBar.slotSize + hotbarBar.slotSize / 2
                let px = cx - width / 2
                if (px < 2) px = 2
                const maxX = hotbarBar.width - width - 2
                if (px > maxX) px = maxX
                return px
            }
            y: {
                let py = -height - 6
                if (py < 2) py = hotbarBar.height + 6 // 顶部空间不足 → 翻到行下方
                return py
            }
            Text {
                id: hudTipLabel
                anchors.centerIn: parent
                // 工具槽附「\n\n耐久: cur/max」行（spec 多行格式）；非工具仅显名。触碰 slotRevision 刷新。
                text: {
                    hotbarVM.slotRevision
                    const id = hotbarBar.hoveredItemId
                    if (id === 0) return ""
                    const name = hotbarVM.nameForBlock(id)
                    if (!hotbarVM.isTool(id)) return name
                    const cur = hotbarBar.hoveredSlot >= 0 ? hotbarVM.durabilityAt(hotbarBar.hoveredSlot) : 0
                    const mx = hotbarVM.toolMaxDurability(id)
                    return name + "\n\n耐久: " + cur + "/" + mx
                }
                color: "#f2f2f2"
                font.pixelSize: 12
            }
        }
    }

    // 生命心 + 饥饿鼓腿条（t22，仅 Survival）：hotbar 上方，左 10 心、右 10 鼓腿。
    // 每心/鼓腿 = 2 点；满/半/空三态自绘原创像素图（VitalIcon.qml 的 Canvas，§9 override (a)：
    // 非 MC GUI PNG）。Creative/Spectator 不显（1.0：非生存模式无生命/饥饿）。
    // 心/鼓腿的「每格 full/half/empty」由 delegate 据 playerState.health/hunger 直接算出
    // （绑定 NOTIFY 自动刷新，无需 Q_PROPERTY(QVariantList)，避开 moc 限制）。
    // 第 index 格代表的点数余额 = health - index*2；右侧（index 大）先空（符合 MC 心从右耗尽）。
    Item {
        id: vitalsBar
        visible: window.appState === "playing"
                 && player.mode === PlayerController.Survival
        anchors.bottom: hotbarBar.top
        anchors.bottomMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        width: hotbarBar.width
        height: 18

        readonly property int iconSize: 18
        // 心/鼓腿三态：据点数余额返回 2=full 1=half 0=empty（每格 2 点）。
        function levelFor(curValue, index) {
            const bal = curValue - index * 2
            return bal >= 2 ? 2 : (bal >= 1 ? 1 : 0)
        }

        // 左：10 颗生命心（index 0 = 最左；右耗尽）。底部对齐 hotbar。
        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            spacing: 0
            Repeater {
                model: 10
                delegate: VitalIcon {
                    kind: "heart"
                    level: vitalsBar.levelFor(playerState.health, index)
                }
            }
        }

        // 右：10 根饥饿鼓腿（index 0 = 最左；右耗尽；与心对称）。
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0
            Repeater {
                model: 10
                delegate: VitalIcon {
                    kind: "hunger"
                    level: vitalsBar.levelFor(playerState.hunger, index)
                }
            }
        }
    }

    // t202 / t227 气泡条（仅 Survival）：置 vitalsBar 上一行，且**右对齐贴在饥饿（食物）鼓腿条正上方**
    //   （非居中于血+食上方）。机制等价 MC 1.0：氧气泡排在右侧食物条上方（左侧食物条上方为护甲，本项目未做）。
    //   airBar.width === vitalsBar.width 且两者同 horizontalCenter → 右边沿对齐 → 内 Row 锚 parent.right
    //   即与下方饥饿 Row（同样 anchors.right: parent.right）同列对齐，气泡逐颗落在鼓腿正上方。
    //   显隐：仅 Survival 且（眼位入水 或 气泡未满）→ 头没入水首次出现、出水回满后消失（spec）。
    //   每气泡 = 1 air（无半态）；level = air > index ? 2 : 0（index 0 = 最左；右耗尽，与心一致）。
    //   气泡图复用 VitalIcon kind="bubble"（自绘原创 Canvas，§9 override (a) 非 MC GUI PNG）。
    //   分层（PLAN §2）：呈现层只读 playerState.air（Game 层显值）+ player.eyeInWater（Physics 层水态），
    //   绝不反向写；air 由 PlayerController.airUpdated 信号驱动刷新（经 Connections 路由到 setAir）。
    Item {
        id: airBar
        visible: window.appState === "playing"
                 && player.mode === PlayerController.Survival
                 && (player.eyeInWater || playerState.air < playerState.maxAir)
        anchors.bottom: vitalsBar.top
        anchors.bottomMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: vitalsBar.width
        height: 18

        // 第 index 颗气泡的态：air > index → full(2)，否则 empty(0)。右耗尽（index 大的先空）。
        function levelForAir(curValue, index) {
            return curValue > index ? 2 : 0
        }

        // t227：Row 锚 parent.right（非 horizontalCenter）→ 与下方饥饿鼓腿 Row 同列，气泡落在鼓腿正上方。
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0
            Repeater {
                model: playerState.maxAir
                delegate: VitalIcon {
                    kind: "bubble"
                    level: airBar.levelForAir(playerState.air, index)
                }
            }
        }
    }

    // 主菜单（t17）：启动首显（appState="menu"）；Start/Quit 信号连到 startGame / Qt.quit。
    // 仅 menu 态可见，z 高于暂停叠层（100）与所有 HUD，全屏覆盖。「退出」→ Qt.quit() 直接退进程。
    // 仅依赖 QtQuick（无特殊模块），直接实例化（非 Loader 隔离）——加载失败即 app 致命，故无需降级。
    MainMenu {
        id: mainMenu
        anchors.fill: parent
        visible: window.appState === "menu"
        z: 200
        onStartRequested: window.appState = "worldlist" // t176：单人模式 → 世界列表（新建 / 选择存档）
        onQuitRequested: Qt.quit()
    }

    // t176 世界列表 / 新建世界（单人模式入口）：列出已有存档 + 新建（名字 + 种子默认 42）+ 进入 / 删除。
    //   仅 worldlist 态显，z=200（与主菜单同级全屏覆盖）。playRequested → enterWorld；backRequested → 回主菜单。
    WorldList {
        id: worldListPanel
        anchors.fill: parent
        store: worldStore
        visible: window.appState === "worldlist"
        z: 200
        onPlayRequested: function(file, name) { window.enterWorld(file, name) }
        onBackRequested: window.appState = "menu"
    }

    // 创造背包 1.0（t23，t18 升级）：可滚动全方块调色板 + 底部 9 槽 hotbar 栏（同步游戏内）+ 销毁槽。
    // 仅 playing && inventoryOpen && Creative 时显（t23/t24 E 键分流：Creative→本面板，Survival→生存背包，
    // Spectator 无反应）。z 高于暂停叠层(100)/HUD，低于主菜单(200)。方块集 / 图标 / 中文名 / 槽位改写
    // 全部经 hotbar VM（ViewModel 读 BlockRegistry）；本组件只做呈现 + 输入转发。关闭（closed 信号）→ 宿主恢复 grab。
    // 仅依赖 QtQuick（无特殊模块），直接实例化（非 Loader 隔离）。
    // t166f 背包叠层祖先容器：包裹 创造/生存/工作台/熔炉面板 + 浮动光标。HoverHandler 在此（祖先）→
    //   hover 同时到本层与各面板槽位（后代），不再被顶层 sibling 截走 → 槽位 tooltip + 拖动均分恢复。
    //   z=150 与原面板同级（高于暂停 100、低于主菜单 200 / 死亡 180）；内部面板各自 z 不变。
    Item {
        id: overlayRoot
        anchors.fill: parent
        z: 150
        HoverHandler { id: cursorTracker }

    Inventory {
        id: inventoryPanel
        anchors.fill: parent
        hotbar: hotbarVM
        visible: window.appState === "playing" && window.inventoryOpen
                 && player.mode === PlayerController.Creative
        z: 150
        onClosed: window.closeInventory()
        // t292：创造背包「归还 / 丢弃光标手持物」（点面板外遮罩 / 换拿覆盖）→ 应**凭空消失**，非丢出到世界
        //   生成实体。机制等价 MC 1.0 创造模式：调色板=无限源，手持物 dismiss 即回虚空、不落地。生存背包
        //   （survivalPanel）的手持物是真实物品，仍走 dropHeldCursor 落地（见下方）。分层（PLAN §2）：呈现层只发
        //   dismiss 意图信号，是否落地由宿主按模式决定（创造=清栈 / 生存=spawn 实体）；setHeldBlock(0) 同步清
        //   count+durability，浮动光标图标自动隐。
        onDiscardHeldRequested: hotbarVM.heldBlock = 0
        // 右键「逐个 dismiss」同源：≤1 → 整栈凭空消失；>1 → 光标 -1（dismiss 的那 1 件消失，余量留光标）。
        onDiscardHeldOneRequested: {
            if (hotbarVM.heldCount <= 1) hotbarVM.heldBlock = 0
            else hotbarVM.heldCount = hotbarVM.heldCount - 1
        }
        // t120：创造拿物品（调色板点击）→ 手弹跳（同生存拾取的手弹反馈，spec「创造拿物品到手也触发」）。
        onItemTaken: handPopAnim.start()
    }

    // 生存背包 1.0（t24）：2×2 合成 + 结果槽 + 4 护甲槽 + 角色预览 + 3×9 主栏 + 9 槽 hotbar 栏。
    // 仅 playing && inventoryOpen && Survival 时显（与创造背包互斥共享 E 键分流）。合成 / 护甲 / 主栏
    // 为占位（Phase 1.1 真实逻辑），槽位布局存在且不崩即满足验收。hotbar 栏同步游戏内 hotbar（读 VM、
    // 点击切换选中槽）。全部槽框 / 护甲图 / 角色预览自绘原创（§9 override (a)）。关闭（closed 信号）→ 宿主恢复 grab。
    // 仅依赖 QtQuick（无特殊模块），直接实例化（非 Loader 隔离）—— 加载失败即 app 致命，故无需降级。
    SurvivalInventory {
        id: survivalPanel
        anchors.fill: parent
        hotbar: hotbarVM
        visible: window.appState === "playing" && window.inventoryOpen
                 && player.mode === PlayerController.Survival
        z: 150
        onClosed: window.closeInventory()
        // t49：拖出丢弃（手持物点遮罩区）→ player 把光标手持栈丢为前方实体（同 Q 丢弃）。
        onDiscardHeldRequested: player.dropHeldCursor()
        // t228：右键拖出 → 只丢 1 件（左键整栈走上面的 dropHeldCursor）。
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
    }

    // t50 工作台 3×3 合成面板：右键工作台方块打开（player.craftingTableOpened → openCraftingTable）。
    // 仅 playing && craftingTableOpen 时显（与背包面板互斥）。E/Esc/关闭信号关 → 宿主恢复 grab。
    // z 与背包面板一致（150），低于主菜单（200）；光标手持物浮动图标 z=300 仍在其上。合成检测 /
    // 栈操作经 hotbar VM（recipeMatch / addStack / setStack）；关包时 onVisibleChanged 归还合成栏。
    CraftingTableUI {
        id: craftingTablePanel
        anchors.fill: parent
        hotbar: hotbarVM
        visible: window.appState === "playing" && window.craftingTableOpen
        z: 150
        onClosed: window.closeCraftingTable()
        onDiscardHeldRequested: player.dropHeldCursor()
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
    }

    // t87 熔炉冶炼面板：右键熔炉方块打开（player.furnaceOpened → openFurnace）。仅 playing &&
    // furnaceOpen 时显（与背包 / 工作台面板互斥）。E/Esc/关闭信号关 → 宿主恢复 grab。
    // 冶炼 tick 由 WorldClock.ticked 驱动（下方 Connections 转发，10Hz）；槽状态 / 进度面板自持、跨开关持久。
    FurnaceUI {
        id: furnacePanel
        anchors.fill: parent
        hotbar: hotbarVM
        visible: window.appState === "playing" && window.furnaceOpen
        z: 150
        onClosed: window.closeFurnace()
        onDiscardHeldRequested: player.dropHeldCursor()
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
    }

    // t173/t179 箱子物品栏面板：右键箱子方块打开（player.chestOpened → openChest）。仅 playing &&
    // chestOpen 时显（与背包 / 工作台 / 熔炉面板互斥）。E/Esc/关闭信号关 → 宿主恢复 grab。
    // 箱子 27 槽内容存 ChestStore（按 chestX/Y/Z 寻址；跨开关持久）；主栏 / hotbar 共享 hotbar VM。
    // z 与其它面板一致（150）；光标手持物浮动图标 z=300 仍在其上。
    ChestUI {
        id: chestPanel
        anchors.fill: parent
        hotbar: hotbarVM
        chestStore: chestStore
        chestX: window.chestX
        chestY: window.chestY
        chestZ: window.chestZ
        visible: window.appState === "playing" && window.chestOpen
        z: 150
        onClosed: window.closeChest()
        onDiscardHeldRequested: player.dropHeldCursor()
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
    }

    // t87 冶炼 tick：WorldClock 每 100ms 发 ticked(0.1) → 转发到 furnacePanel.tick 推进冶炼。
    // 单一时间权威（PLAN §2）：所有按时间推进的子系统都消费 WorldClock，不在 QML 各自起 Timer。
    // tick 内自检无活干（无燃料 / 无输入）即静默 return，故常驻连接无开销。
    Connections {
        target: worldClock
        function onTicked(dt) {
            furnacePanel.tick(dt)
            // t185 水流蔓延 tick：WorldClock 每 100ms tick → 驱动 World.tickWaterFlow（内部节流到 ~0.3s
            //   把波前推进 1 格 → 1 格/tick 流动动画可见）。纯 QML 桥接（WorldClock 为 Game 层不 include World；
            //   QML 同时持二者向下合法，PLAN §2 分层不破）。tickWaterFlow 内部对 settled 流场（无变化）静默 → 无重建开销。
            theWorld.tickWaterFlow()
            // t236 小麦作物生长 tick：WorldClock 每 100ms tick → 驱动 World.tickCropGrowth（内部节流到 ~每 2.5s
            //   做一次成长判定，作物据光强 + 耕地支撑 + 散布概率逐步升生长阶段）。纯 QML 桥接（WorldClock 为
            //   Game 层不 include World；QML 同时持二者向下合法，PLAN §2 分层不破）。tickCropGrowth 内部对稳态
            //   （全成熟 / 无作物 / 全暗）静默 → 无重建开销。
            theWorld.tickCropGrowth()
            // t305 树苗生长 tick：WorldClock 每 100ms tick → 驱动 World.tickSaplingGrowth（内部节流到 ~每 5s
            //   做一次成长判定，树苗据光强 + 草地/泥土支撑 + 主干列畅通 + 散布概率逐步长成完整橡树）。纯 QML
            //   桥接（同 tickCropGrowth 模式）。tickSaplingGrowth 内部对稳态（无树苗 / 全不满足）静默 → 无重建开销。
            theWorld.tickSaplingGrowth()
            // t325 树叶渐进消退 tick：WorldClock 每 100ms tick → 驱动 World.tickLeafDecay（内部节流到 ~每 0.3s
            //   开一窗，队列内每叶按散布概率 2%/窗独立判定是否消失 → 几何分布散布 ~10-30s 渐退，非瞬时全消）。
            //   纯 QML 桥接（同 tickCropGrowth/tickSaplingGrowth 模式）。tickLeafDecay 内部对稳态（无失撑叶 /
            //   本窗无命中）静默 → 无写入、无重建开销。
            theWorld.tickLeafDecay()
        }
    }

    // 光标位置追踪层（t107）：独立全屏层，z=250 高于所有背包/工作台/熔炉面板（z=150）。
    // 原 cursorTracker HoverHandler 放在 keyInput（z=0，anchors.fill 窗口）内 —— 面板 z=150 在其
    // 之上截断 hover：按下 TapHandler/DragHandler 期间 point.position 停在旧位 → 浮动手持图标偏离
    // 鼠标（拿起卡顿）。提到面板之上后 hover 不再被截断，光标实时跟鼠标。
    // HoverHandler 为被动追踪（passive grab），**不消费 press/click** —— 不抢下方槽 TapHandler/DragHandler
    // 的 grab（本 Item 无任何 press handler，点击穿透到面板）。注意：enabled:false 会连 HoverHandler 一起
    // 禁用，故本层保持默认 enabled；纯被动 hover 不阻断下方点击（与 z=300 浮动光标 enabled:false 同理）。
    // t166f: cursorTrackLayer 删除。旧版 z=250 全屏 HoverHandler 在背包面板(z=150)之上 → 作为顶层
    //   sibling 截走 hover，槽位 HoverHandler 不触发 = tooltip/拖动均分失效。改把 HoverHandler 放到
    //   包裹这些叠层的 overlayRoot（祖先）—— hover 传给祖先+后代，不挡槽位。光标位置仍由它驱动浮动图标。


    // 光标手持物浮动图标（背包点击拾取后「拿在鼠标上」的物品栈；hotbarVM.heldBlock/heldCount 驱动）。
    // 仅背包 / 工作台打开且手持非空时显，z 最高（盖过背包面板 z=150）。位置跟随 cursorTracker（窗口坐标）。
    // t32：count>1 时右下角显数量（手持整栈移动时可见剩余数）。t33：手持工具 → ToolIcon 自绘（非 Image）；
    // t50：手持材料 → MaterialIcon 自绘（木棒）。t37：enabled:false 显式声明本 Item 不参与指针事件——
    // z=300 浮在面板(z=150)之上，若参与事件捕获会抢走下方槽位 TapHandler 的点击。纯呈现层。
    Item {
        visible: (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen) && hotbarVM.heldBlock !== 0
        enabled: false
        z: 300
        x: cursorTracker.point.position.x - 16
        y: cursorTracker.point.position.y - 16
        width: 32; height: 32
        Image {
            anchors.fill: parent
            visible: !hotbarVM.isTool(hotbarVM.heldBlock) && !hotbarVM.isMaterial(hotbarVM.heldBlock)
            source: hotbarVM.iconSourceForBlock(hotbarVM.heldBlock)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        ToolIcon {
            anchors.fill: parent
            visible: hotbarVM.isTool(hotbarVM.heldBlock)
            tier: hotbarVM.toolTier(hotbarVM.heldBlock)
            toolType: hotbarVM.toolType(hotbarVM.heldBlock)
        }
        MaterialIcon {
            anchors.fill: parent
            visible: hotbarVM.isMaterial(hotbarVM.heldBlock)
            materialId: hotbarVM.heldBlock
        }
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: hotbarVM.heldCount > 1
            text: hotbarVM.heldCount
            color: "#ffffff"
            style: Text.Outline; styleColor: "#000000"
            font.pixelSize: 13; font.bold: true
        }
    }
    } // t166f overlayRoot close（包裹背包叠层）
}
