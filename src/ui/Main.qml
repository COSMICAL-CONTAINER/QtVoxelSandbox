import QtQuick
import QtQuick3D
// t415c 资源包目录选择器（FolderDialog；原生文件夹拾取，MC 式 UX）。
import QtQuick.Dialogs
// t41：QML 源文件迁入 src/ui/ 子目录后，不再位于模块根 → 丢失对模块 C++ 类型
// （World / Hotbar / PlayerController / ChunkGeometry / CrackBox …）的隐式访问。
// 显式 import 自身模块以恢复类型解析（Qt6 子目录 QML 文件的标准做法）。行为不变。
import VoxelSandbox

// qml-touch 三轮：全文件「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen AOT
//   把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

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
    // t494 熔炉格世界坐标（furnaceOpened 携坐标存此；FurnaceUI 冶炼 tick 据 burnRemain 跨 0 边界经
    //   player.setFurnaceLit(furnaceX/Y/Z, lit) 翻转该格 state 的燃烧 bit → mesher 切 front/front_on 贴图）。
    property int furnaceX: 0
    property int furnaceY: 0
    property int furnaceZ: 0
    // t173/t179 箱子子态：右键箱子方块 → player.chestOpened(x,y,z) → 显 ChestUI（箱子 27 槽 + 玩家主栏 +
    //   hotbar）+ 释放指针。与 inventoryOpen / craftingTableOpen / furnaceOpen 互斥；E/Esc 关 → 恢复 grab。
    //   chestX/Y/Z 记当前所开箱子的方块世界坐标（ChestStore 据此寻址该箱子的 27 槽；切箱子时坐标变）。
    property bool chestOpen: false
    property int chestX: 0
    property int chestY: 0
    property int chestZ: 0
    // t474 附魔台子态：右键附魔台方块 → player.enchantingTableOpened(x,y,z) → 显 EnchantingTableUI（3
    //   附魔选项槽 + XP 等级 / 青金石消耗）+ 释放指针。与 inventoryOpen / craftingTableOpen / furnaceOpen /
    //   chestOpen 互斥；E/Esc 关 → 恢复 grab。enchantX/Y/Z 记当前所开附魔台的方块世界坐标（UI 据此查
    //   theWorld.countBookshelvesAround 算书架加成 → 提升可选附魔等级上限）。
    property bool enchantingTableOpen: false
    property int enchantX: 0
    property int enchantY: 0
    property int enchantZ: 0
    // t477 铁砧子态：右键铁砧方块 → player.anvilOpened(x,y,z) → 显 AnvilUI（修复 / 附魔合并 / 重命名三功能，
    //   各消耗 XP 等级）+ 释放指针。与 inventoryOpen / craftingTableOpen / furnaceOpen / chestOpen /
    //   enchantingTableOpen 互斥；E/Esc 关 → 恢复 grab。anvilX/Y/Z 记当前所开铁砧的方块世界坐标（UI 据此
    //   调 player.damageAnvil 推进铁砧损坏阶段）。
    property bool anvilOpen: false
    property int anvilX: 0
    property int anvilY: 0
    property int anvilZ: 0
    // t517 发射器子态：右键发射器方块 → player.dispenserOpened(x,y,z) → 显 DispenserUI（3×3 发射器容器槽 +
    //   玩家主栏 + hotbar）+ 释放指针。与 inventoryOpen / craftingTableOpen / furnaceOpen / chestOpen /
    //   enchantingTableOpen / anvilOpen 互斥；E/Esc 关 → 恢复 grab。dispenserX/Y/Z 记当前所开发射器的方块世界
    //   坐标（t517 本轮「界面先行」：9 槽内容暂存 QML 本地数组，坐标暂留作后续 DispenserStore per-block 寻址用）。
    property bool dispenserOpen: false
    property int dispenserX: 0
    property int dispenserY: 0
    property int dispenserZ: 0
    // t549 世界栅格编辑版本号：每次玩家放 / 破方块自增。附魔台 UI 的 bookshelfPower 绑定触碰它 →
    //   「UI 开着时放 / 破书架」也能刷新书架计数（countBookshelvesAround 是 Q_INVOKABLE，无 NOTIFY，
    //   不触碰则绑定永不重算 → 用户报「旁边放书架显示还是 0」根因之一）。低频（每次放 / 破 +1）。
    property int worldEditRev: 0
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
    // 盖子全开角（度）：t441 改 95（≈90° 略后仰，机制等价 MC 箱子开盖姿态；浅盖 0.5 深立起 ~0.5 高，无需过冲）。
    //   旧 105° 配满深 1.0 盖 → 立起满高像第二只箱子（旧 bug 根因之一）；浅盖 + 95° 立起 ~0.5 明显矮于本体。
    readonly property real kChestLidOpenAngle: 95
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
    // t139 ESC 设置菜单子态（仅在暂停叠层（playing 且 !captured）下有意义）。暂停叠层「选项」按钮置
    //   true → 显选项面板（手臂调试 ArmSlider 等）覆盖在暂停叠层之上；「返回」按钮置 false 回暂停菜单。
    //   回主菜单 / 点击恢复游戏时一并复位。属纯呈现态，PLAN §2 分层（UI 层）。
    // pause-menu ESC 关：settingsOpen 为 true 时按 ESC → settingsOpen=false（回暂停菜单，不直接 unpause）。
    property bool settingsOpen: false
    // pause-menu 暂停菜单「进度」按钮子态（5 行布局行2）：显成就列表（progress.achievements() delegate +
    //   revision 触碰刷新）。仅暂停叠层有意义（!captured）；Esc / 返回按钮关。纯呈现态，PLAN §2 分层（UI 层）。
    property bool progressOpen: false
    // pause-menu 暂停菜单「统计」按钮子态（5 行布局行2）：显统计列表（progress.statsList() delegate +
    //   revision 触碰刷新）。同 progressOpen 模式（暂停叠层 !captured；Esc / 返回关）。纯呈现态。
    property bool statsOpen: false
    // pause-menu 右下角 toast（成就解锁 + 「敬请期待」占位提示通用通道）：单一 string 缓存 + Timer 3 秒后清空。
    //   触发源：progress.achievementUnlocked 信号（成就解锁，文案「成就解锁：{name}」）/ 占位按钮（showInfoToast）。
    //   仅 playing 态显（菜单 / worldlist 不显）。纯呈现态，PLAN §2 分层（UI 层）。
    property string infoToastText: ""
    property bool infoToastVisible: false
    // t458 资源查看器子态（PLAN §2 UI 层，纯呈现）：设置面板「资源查看器」按钮触发 → 显 JEI 式浏览面板
    //   （全物品网格 + 选中物 3D 旋转 / 大图标预览）。仅在暂停 + 设置态下有意义（!captured）；Esc / 返回按钮关。
    //   不改指针态（设置面板已 release；关闭回设置面板仍 !captured）。属纯呈现态，PLAN §2 分层（UI 层）。
    property bool resourceBrowserOpen: false
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
        if (enchantingPanel.visible)     return enchantingPanel.hoveredKey
        if (anvilPanel.visible)          return anvilPanel.hoveredKey
        if (dispenserPanel.visible)      return dispenserPanel.hoveredKey
        if (inventoryPanel.visible)      return inventoryPanel.hoveredKey
        if (survivalPanel.visible)       return survivalPanel.hoveredKey
        return ""
    }
    // t512 创造调色板 hover 物品 id（提升到 window 级，供 keyInput 数字键 1-9 路由读取）。仅创造背包面板
    //   （Inventory.qml）持 creativeHoveredItemId（调色板单元格 hover 写 / 离开按 id 守卫清）；其余面板无此源
    //   （生存/工作台/熔炉/箱子无创造无限调色板）→ 0。面板 visible=false 时返 0（避免读隐藏面板陈旧态）。
    property int creativeHoveredItemId: {
        if (window.appState !== "playing") return 0
        if (inventoryPanel.visible)        return inventoryPanel.creativeHoveredItemId
        return 0
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

    // perf-t520 天光基色缓存：原 ~300 chunk Model material 各自 `baseColor: terrainLight(skyLight)`
    //   （terrain / water / cross 段，每段 100 chunk = 300 material 实例）→ 10Hz skyLight 变更触发 300 绑定重算
    //   （3000 次/秒 JS 调用 + Qt.rgba QColor 分配 + scenegraph 标脏）。改为单一 window.skyBaseColor 绑定 + 各
    //   material 只读它 → 10Hz 仅 1 次重算，材料绑定从「计算 + QColor 分配」降为「常数属性读取」。
    //   tintBySkyLight（mob tier 色）不在 chunk material 路径，结构不同（mob delegate 数 << 300），保留独立调。
    // R19 B6（PLAN §2-H 夜间火把发光修复）：地形 / 水 / 玻璃 / 冰 / cutout 材质 baseColor 改**白**
    //   （昼夜乘子改由 C++ 烘进顶点色的**天光分量**承担，方块光独立时间不变）。skyBaseColor 现仅供**不采光场**的
    //   呈现路径（mob 体色 / 掉落物无 world 的 BlockCube / 手持图标等 —— 它们顶点色恒白 1.0、baseColor 是唯一昼夜
    //   调制点）继续用，故保留。新增 skyDayMul 供地形 / 掉落沙 ChunkGeometry / BlockCube 注入 mesher 烘天空分量。
    property color skyBaseColor: terrainLight(worldClock.skyLight)
    // R19 B6：供地形 ChunkGeometry / 掉落沙 BlockCube 的 dayMul 属性注入（= terrainLight(skyLight) 标量）。
    //   dayPhaseChanged（10Hz）推本属性；mesher 端量化门控（dayMul 累计变超 0.03 才重建，非 10Hz 全量重建）。
    property real skyDayMul: terrainLight(worldClock.skyLight).r  // .r = 纯灰阶标量（terrainLight 返 (b,b,b,1)）

    // t384 云层漂移累积（单一时间权威 WorldClock.ticked 推进，不在 QML 另起 Timer，PLAN §2）。
    //   云 Model 的 position.x = player.x + (cloudDrift) —— cloudDrift 随时间增长并在「一个 tile 世界宽」
    //   处取模回绕（贴图可无缝平铺 → 回绕点无接缝），实现「缓慢漂移的无限云盖」。driftSpeed=1.5 格/s。
    property real cloudDrift: 0.0
    readonly property real kCloudDriftSpeed: 1.5    // 云漂移速度（格/秒；MC 云缓慢漂移）
    readonly property real kCloudPlaneSize: 600.0   // 云平面边长（世界格；远超 5×5 世界 80×80 边界）
    readonly property real kCloudTiles: 12.0        // 贴图重复次数（scaleU/V；一 tile = 平面宽/重复数）
    readonly property real kCloudTileWorld: 600.0 / 12.0   // 一个 tile 的世界宽 = 漂移回绕周期（=50 格）
    readonly property real kCloudAltitude: 90.0     // 云盖在玩家眼位之上的高度（格；始终在头顶上方）
    // t386 闪电击中点附近实体伤害参数（机制等价 MC 1.0 雷击对击中点附近实体造成伤害）。kLightningHurtRadius =
    //   3.0 格欧氏半径（覆盖击中点 ±3 立体范围）；kLightningDamage = 5 HP（约 MC 雷击 5 心伤害量级，可见可验收）。
    readonly property real kLightningHurtRadius: 3.0
    readonly property int  kLightningDamage: 5

    // t166b 阴影开关（用户「卡顿疑似阴影所致，加开关测」）：false → 全 chunk sunShadowAt 返 0（关 PCF 软影，
    //   meshing 提速；顶点光基底只剩 flood-fill 光场）。ESC 设置面板开关绑此。默认 true。
    property bool shadowsEnabled: true
    // t178/t183 贪婪网格化开关（PLAN §4 性能打磨）：true → chunk mesher 合并同 (tile,光) 共面为单个矩形
    //   （顶点/三角大幅下降，F3 可观测）；false → 回退逐格 culled（贴图逐格清晰）。绑各 ChunkGeometry。
    //   ⚠️ 图集（CLAMP 采样）路径下合并 quad 的贴图必然**拉伸**铺满（UV 超 [u0,u1] 采到相邻瓦片，无法 REPEAT
    //   逐格平铺；细分成 per-block 子格又与 culled 输出一致、无顶点收益）→ t183 默认 false 恢复逐格清晰贴图。
    //   逐格清晰 + 顶点预算兼得需纹理数组（自研 RHI，dev-plan 偏差 1/2）。ESC 设置面板仍可手动开 greedy 对比。
    property bool greedyMeshing: false
    // t223/tXXX 水贴图动画 phase（flipbook 帧索引）：**已废弃删除**（tXXX 水动画重建消除，静态水单帧）——
    //   旧属性驱动 2s 一次水段全量重建（Swamp 261 段/次，mesh 重建风暴第二根因）。waterAnimTimer 随动删除，
    //   水段 mesh 恒用 phase 0 帧（C++ 侧 setWaterAnimPhase 也不再触发重建；属性 + Timer 无消费方即删）。
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
    // perf-t520 F3 文本节流（PLAN §4 性能打磨：用户报 <10FPS 主因之一 = F3 文本绑定重算）。
    //   旧 F3 text 绑定读 player.position/feetPosition/yaw/pitch/speed/onGround/hasHit/hitBlock +
    //   theWorld.biomeIdAt(...) Q_INVOKABLE + liveCount() Q_INVOKABLE 等 —— 这些属性 NOTIFY 60Hz
    //   （player.positionChanged 每 tick 发）→ 整块多行字符串（~30 行 concat + Q_INVOKABLE 调用）
    //   每帧重算，每秒 ~60 次。10Hz Timer 把所有读取移出 text 绑定，buildF3Text() 内读最新值，
    //   结果写 f3Text 单一 string 属性 → text 元素只绑 f3Text（NOTIFY 10Hz）→ F3 文本重算频率
    //   从 60Hz 降到 10Hz（~6× 降）。F3 是调试叠层（非玩法关键），100ms 延迟可接受。
    //   perf-t520 加 frame 行（main_total / render_cpu）—— 同窗口桶已在 FrameProfiler.flush() 拼。
    property string f3Text: ""
    property string hudPosText: ""
    // t470 渲染距离 culling（PLAN §4 性能打磨；用户报 9 FPS 主因 = 100 chunk × 6 段 = 600 Model 全渲染无视距）。
    //   仅玩家所在 chunk 周围 renderDistance 切比雪夫半径内的 chunk Model 设 visible=true，其余 false → 远端
    //   chunk 不参与 draw call、不进渲染队列排序，按距离大幅省 GPU / draw-call 开销。
    //   - 单位 = chunk（每 chunk 16×16 格）；默认 3 → 玩家周围 7×7=49 chunk 可见（中心出生 49/100，省 51% chunk）。
    //     MC 标准视距 8-12 chunk（128-192 格）适用于 200+ chunk 大世界；本工程 10×10 chunk 小世界取 3 平衡
    //     性能与可见范围（半径 ≥5 时小世界中心全可见 = 无 culling 收益）。改值经 ESC 设置滑条 + 立即重算。
    //   - 玩家跨 chunk 边界才重算（每 ~16 格位移触发一次），开销 O(chunk 数) 一次性遍历 chunkObjects；
    //     非每帧扫描、非每 positionChanged 扫描（_playerCX/_playerCZ 缓存守门）。
    //   - 数据保留：不可见 chunk 的栅格数据仍在 World 内存（setBlock / 流体 tick 等仍可读写），只是 Model.visible=false
    //     不绘制几何；玩家走近时 Model.visible=true 即时复显（mesh 已在 chunkAnchor.onCompleted 一次性建好）。
    //   分层（PLAN §2）：本属呈现层 visibility 决策，只读 player.feetPosition（Game 层 Q_PROPERTY）+ 写各 chunk Model
    //     的 visible（Renderer 层自有属性），不写栅格 / 不反向依赖 Game。机制等价 MC render distance（仅渲视野半径内 chunk）。
    property int renderDistance: 3
    property int _playerCX: -1       // 玩家所在 chunk X（缓存；未初始化 -1，跨 chunk 边界才刷新）
    property int _playerCZ: -1       // 玩家所在 chunk Z（同上）
    property int visibleChunkCount: 0 // 当前 chunkInRange=true 的 chunk 数（F3 显示；可见 ≠ 全 meshed 100）
    // t470 实际 visible=true 的段 Model 数（chunkInRange && geo.vertexCount>0；空段被剔）。draw-call 估算读它
    //   （替代旧 ncx*ncz*2 / visibleChunkCount*6 上界——空段不出 draw，此为更诚实的估算）。
    //   注意：值在 _refreshChunkVisibility 末刷新；meshRebuilt 改 vertexCount 时各段 visible 绑定自动重算，
    //   但本属性不实时跟踪（仅在 chunk 跨界 / 渲染距离调整时刷新）—— F3 draw-call 行仅示性，无需逐帧精度。
    property int visibleSegmentCount: 0
    // t489 流体材质级动画帧（水/岩浆条带 flipbook）。由下方 waterAnimTimer / lavaAnimTimer 推进；
    //   绑水/岩浆段 Texture 的 positionV（= frame / stripFrames）→ 帧切换纯材质参数，零 mesh 重建
    //   （修 t222/t223「水 2s 全量重建水段 261 段/次」回归；F3 [w]/[s] reb 不回升）。
    property int waterAnimFrame: 0   // 0..(waterStripFrames-1)，循环
    property int lavaAnimFrame: 0    // 0..(lavaStripFrames-1)，循环
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
    // perf-t520 F3 文本节流：把原 F3 text 绑定的全部读取 / 字符串拼接抽成普通函数 —— 由 10Hz Timer
    //   调用（f3RefreshTimer），结果写 window.f3Text 单一 string 属性，F3 Text 元素只读它。这样所有
    //   per-tick 属性（player.position / feetPosition / yaw / pitch / speed / onGround / hasHit / hitBlock /
    //   liveCount() / biomeIdAt() / dayPhase / skyLight）从「QML binding 依赖」降为「普通 JS 读取」——
    //   即 QML 引擎不再为它们建立 NOTIFY 触发链，F3 整块字符串的重算频率由 60Hz（每帧）降到 10Hz（每秒 10 次）。
    //   100ms 延迟对调试叠层零影响（人眼读字 ~200ms 起跳），但 main thread binding 工作量直降 6×。
    //   F3 未显（f3Visible=false）时 f3RefreshTimer.running=false → 零开销。
    function buildF3Text() {
        // 同原 F3 text 绑定 body（不动逻辑，仅从「绑定 body」抽成「函数 body」）。
        const vx = window.meshVertices, tr = window.meshTriangles
        const modeName = player.mode === PlayerController.Spectator ? "SPECTATOR"
                       : player.mode === PlayerController.Creative ? "CREATIVE" : "SURVIVAL"
        const camName = player.cameraMode === PlayerController.FirstPerson ? "1st"
                      : player.cameraMode === PlayerController.ThirdPersonBack ? "3rd-back" : "3rd-front"
        const moveName = player.moveState === PlayerController.Sprint ? "sprint"
                       : player.moveState === PlayerController.Crouch ? "crouch" : "walk"
        const ncx = window.worldChunksPerSide, ncz = window.worldChunksPerSide
        const frameMs = window.fps > 0 ? (1000.0 / window.fps) : 0.0
        const itemLive = itemEntities.liveCount(), mobLive = entityManager.liveCount(), orbLive = xpOrbs.liveCount()
        const drawEst = window.visibleSegmentCount + itemLive + mobLive + torchPositions.count + 6
        const meshMode = window.greedyMeshing ? "greedy" : "culled"
        const fpx = Math.floor(player.feetPosition.x), fpz = Math.floor(player.feetPosition.z)
        const biomeNames = ["Plains", "Hills", "Desert", "Forest", "Snowy", "Swamp"]
        const bid = theWorld.biomeIdAt(fpx, fpz)
        const biomeName = (bid >= 0 && bid < biomeNames.length) ? biomeNames[bid] : ("?" + bid)
        const dayPhase = worldClock.dayPhase
        const totalMin = ((12.0 + dayPhase * 24.0) % 24.0) * 60.0
        const hh = Math.floor(totalMin / 60.0), mm = Math.floor(totalMin % 60.0)
        const timeStr = (hh < 10 ? "0" : "") + hh + ":" + (mm < 10 ? "0" : "") + mm
        return "voxelsandbox  [F3 debug]"
             + "\nfps: " + window.fps + "  frame: " + frameMs.toFixed(1) + "ms  cpu sim: " + player.simMs.toFixed(2) + "ms"
             + "\npos: " + player.position.x.toFixed(2) + "  " + player.position.y.toFixed(2) + "  " + player.position.z.toFixed(2)
             + "  (feet " + player.feetPosition.x.toFixed(1) + "," + player.feetPosition.y.toFixed(1) + "," + player.feetPosition.z.toFixed(1) + ")"
             + "\nyaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch) + "  look " + camName
             + "\nmode: " + modeName + (player.flying ? " (fly)" : "") + "  move: " + moveName + "  ground: " + (player.onGround ? "yes" : "no")
             + "\nspeed: " + player.speed.toFixed(2) + " b/s"
             + (player.flying || player.mode === PlayerController.Spectator
                ? "  fly: " + player.flySpeed.toFixed(1) + " b/s (x" + player.flySpeedMul.toFixed(2) + ")"
                : "")
             + (player.hasHit ? "  hit: " + player.hitBlock.x + "," + player.hitBlock.y + "," + player.hitBlock.z : "  hit: -")
             + "\nbiome: " + biomeName + "  (col " + fpx + "," + fpz + ")"
             + "\nworld: " + (window.worldChunksPerSide * 16) + "×" + (window.worldChunksPerSide * 16) + "×" + theWorld.height
             + "  chunks: " + ncx + "×" + ncz + " = " + (ncx * ncz)
             + "  render r=" + window.renderDistance + " visible " + window.visibleChunkCount + "/" + (ncx * ncz)
             + "\nmesh: " + meshMode + "  vertices: " + vx + "  triangles: " + tr
             + "\nentities: mobs " + mobLive + "/" + entityManager.count + "  items " + itemLive + "/" + itemEntities.count
             + "  orbs " + orbLive + "/" + xpOrbs.count
             + "\ndraw-calls: ~" + drawEst + "  (segs vis " + window.visibleSegmentCount
             + " + items " + itemLive
             + " + mobs " + mobLive + " + torches " + torchPositions.count + " +6 scene)  threads: 0/0 (sync meshing)"
             + "\ntime: " + timeStr + "  day " + worldClock.dayCount + "  moon " + worldClock.moonPhase
             + "  phase " + dayPhase.toFixed(2) + "  sky " + worldClock.skyLight.toFixed(2)
             + (worldClock.debugFast ? "  (fast)" : "")
             + "\nxp: " + playerState.xp + (playerState.xp > 0 ? "  lvl " + playerState.level : "")
             + "\n[B] hitboxes: " + (window.showHitboxes ? "ON" : "off")
             + "   [G] chunk bounds: " + (window.showChunkBounds ? "ON" : "off")
    }
    // perf-t520 HUD pos 文本节流：同上，把原 HUD 顶部小条 text 绑定（player.position/yaw/pitch/onGround +
    //   hotbarVM.nameAt/countAt）抽成普通函数，由 10Hz Timer 调用。HUD 不是 F3 调试，玩家正常游戏时也显
    //   （playing 态常驻），节流收益对**所有**玩家有效（非仅 F3 开时）。
    function buildHudPosText() {
        const held = player.captured
            ? ("   ground: " + (player.onGround ? "yes" : "no")
               + "   held: " + hotbarVM.nameAt(hotbarVM.selectedSlot)
               + " (#" + player.selectedBlock + ")"
               + " ×" + hotbarVM.countAt(hotbarVM.selectedSlot))
            : "   pointer free"
        return "pos: " + player.position.x.toFixed(1) + ", " + player.position.y.toFixed(1) + ", " + player.position.z.toFixed(1)
              + "   yaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch)
              + held
    }
    // t470 玩家跨 chunk 边界检测 → 触发远端 chunk visibility 刷新。每 positionChanged 调一次（60Hz tick 玩家
    //   移动时），但仅在 chunk 坐标真变时才走 _refreshChunkVisibility（O(chunk 数) 全扫）。stationary 玩家零开销。
    function _updatePlayerChunk() {
        if (!window.chunksBuilt) return // chunkObjects 未就绪（启动初期 / 切世界途中）→ 等创建完再刷新
        const p = player.feetPosition
        const cx = Math.floor(p.x / 16)
        const cz = Math.floor(p.z / 16)
        if (cx === window._playerCX && cz === window._playerCZ) return
        window._playerCX = cx
        window._playerCZ = cz
        window._refreshChunkVisibility()
    }
    // t470 切比雪夫半径内的 chunk Model.chunkInRange=true；外的 false。每 chunk 6 段 Model 共享 chunkCX/CZ，遍历
    //   chunkObjects 一次性处理（每段独立设 chunkInRange，相邻段同 chunk 一并切换）。Model.visible 由各模板
    //   的 `visible: chunkInRange && geo.vertexCount > 0` 绑定自动决定（双重剔除：远端 + 空段）。
    //   visible 计数写 visibleChunkCount 供 F3 显示。玩家越界（cx<0 等）不影响——判 abs(dcx)<=r 自然过滤。
    function _refreshChunkVisibility() {
        const objs = window.chunkObjects
        const pcx = window._playerCX, pcz = window._playerCZ
        const r = window.renderDistance
        let vis = 0
        // chunk Model 6 段共享同 chunkCX/chunkCZ；用「首段」统计 unique chunk 数（避免重复计 6 段）。
        // 简化：每 6 段为一组，组内首段命中即 chunk 计数 +1。
        const totalChunks = window.worldChunksPerSide * window.worldChunksPerSide
        const segmentsPerChunk = 6 // terrain + water + lava + cross + glass + ice
        for (let i = 0; i < objs.length; ++i) {
            const o = objs[i]
            if (!o) continue
            const inRange = (Math.abs(o.chunkCX - pcx) <= r && Math.abs(o.chunkCZ - pcz) <= r)
            o.chunkInRange = inRange // visible 由各模板绑定读 chunkInRange && geo.vertexCount > 0
            // 每 segmentsPerChunk 段为一组；组首（i % 6 == 0）且 inRange → chunk 计数 +1。
            if (inRange && (i % segmentsPerChunk) === 0) ++vis
        }
        window.visibleChunkCount = vis
        // 防御：若 chunkObjects 长度 ≠ totalChunks*6（构造期未齐 / 异常），visibleChunkCount 不超 totalChunks。
        if (window.visibleChunkCount > totalChunks) window.visibleChunkCount = totalChunks
        // t470 诊断日志：首次刷新 + chunk 跨界时记录可见数（确认 culling 生效 + 玩家移动后正确更新）。
        // segVis = 实际 visible=true 的段数（chunkInRange && geo.vertexCount>0），验证空段剔除生效。
        let segVis = 0
        for (let j = 0; j < objs.length; ++j) {
            const o = objs[j]
            if (o && o.visible) ++segVis
        }
        window.visibleSegmentCount = segVis
        console.info("[t470] chunk visibility: player chunk (" + pcx + "," + pcz + ") r=" + r
                     + " chunks " + vis + "/" + totalChunks
                     + " segs visible " + segVis + "/" + objs.length)
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
        // t177 二轮复盘 熔炉按世界持久化 + 修跨世界泄漏：furnaceStore 跨世界长驻（同 hotbarVM / chestStore），
        //   进世界前 loadAll 整体替换内存（先清后填）—— 无存档 furnaces 表 → 空列表 → 清空，杜绝上一世界熔炉
        //   残留串入新世界。存档 furnaces 由 saveAndExitToWorldList 经 saveAll(name, chests, furnaces) 落盘。
        furnaceStore.loadAll(worldStore.loadFurnaces())
        // t542 发射器按世界持久化 + 修跨世界泄漏：dispenserStore 跨世界长驻（同 hotbarVM / chestStore /
        //   furnaceStore），进世界前 loadAll 整体替换内存（先清后填）—— 无存档 dispensers 表 → 空列表 → 清空，
        //   杜绝上一世界发射器残留串入新世界。存档 dispensers 由 saveAndExitToWorldList 经 saveAll(name, ...,
        //   dispenserStore.allDispensers()) 落盘。
        dispenserStore.loadAll(worldStore.loadDispensers())
        // progress 按世界持久化：进世界前 loadVariant 整体替换内存（清旧世界残留 + 填本世界进度）。无存档
        //   progress 表 → 空 map → 重置默认（全 0 统计 + 全未解锁成就）。存档由 saveAndExit saveProgress 落盘。
        progress.loadVariant(worldStore.loadProgress())
        // 清上一世界的掉落物 / mob / 经验球残留（实体非体素，不进存档，切世界必清）
        itemEntities.clearAll()
        entityManager.clearAll()
        xpOrbs.clearAll()   // t402 经验球同族实体，切世界必清
        carts.clearAll()    // t565 矿车同族实体（非体素不进存档），切世界必清（清骑乘态 + 空槽复用）
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
        //   t382：护甲 4 槽同样跨世界长驻 → 一并清（旧版漏清 → 上世界装备串入新世界）。
        for (let i = 0; i < 9; ++i) hotbarVM.setStack(i, 0, 0)
        for (let j = 0; j < 27; ++j) hotbarVM.mainSetStack(j, 0, 0)
        for (let k = 0; k < 4; ++k) hotbarVM.armorSetStack(k, 0, 0)
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
        // t402 经验值回填（存档持久化 playerState.xp；旧存档无 xp 字段 → undefined → 默认 0）。
        playerState.setXp(data.xp !== undefined ? data.xp : 0)
        // t238 同步 Physics 层饥饿镜像（存档持久化 playerState.hunger；此处把同一值灌回 PlayerController.m_hunger，
        //   使两层一致、depletion 从存档值起算）。player.setHunger 内部 emit hungerUpdated → 上面 onHungerUpdated
        //   路由回 playerState.setHunger（幂等：值已一致则无变化静默）。
        player.setHunger(data.hunger !== undefined ? data.hunger : 20)
        // 背包已在上文两路径共用处清空，此处直接按存档写
        //   t382：每栈透传 durability（缺省 -1 = 自动满；存档有值则保真工具磨损）。非工具 durability 经
        //   Hotbar::normalizeDurability 归一为 0（inert），故传 0 对方块栈安全。
        if (data.hotbar) for (let i = 0; i < data.hotbar.length && i < 9; ++i) {
            const s = data.hotbar[i]
            hotbarVM.setStack(i, s.id, s.count, s.durability !== undefined ? s.durability : -1)
        }
        if (data.main) for (let i = 0; i < data.main.length && i < 27; ++i) {
            const s = data.main[i]
            hotbarVM.mainSetStack(i, s.id, s.count, s.durability !== undefined ? s.durability : -1)
        }
        // t382 护甲 4 槽回填（旧存档无 armor 字段 → data.armor undefined → 跳过，保持上文已清的空装备）。
        if (data.armor) for (let k = 0; k < data.armor.length && k < 4; ++k) {
            const s = data.armor[k]
            hotbarVM.armorSetStack(k, s.id, s.count, s.durability !== undefined ? s.durability : -1)
        }
        // 同上：`||` 对 0（第 0 槽）会误兜底，恰好 0==默认值巧合正确，但显式检查更稳健且与上面一致。
        hotbarVM.selectedSlot = data.selectedSlot !== undefined ? data.selectedSlot : 0
    }
    // t176 收集当前玩家态为 QVariantMap（存档用）：位姿 / 模式 / 血饥 / hotbar 9 + main 27 背包 / 选中槽。
    //   t382 round-trip 保真补全：每栈多记 durability（旧版只存 id/count → 工具磨损 round-trip 后回满），
    //   并新增 armor 4 槽（旧版完全没存护甲 → 装备退出即丢）。version 2 = +durability +armor（自描述 JSON，
    //   applyPlayerState 对缺字段降级，旧 v1 存档仍可读）。
    function gatherPlayerState() {
        const hotbar = []
        for (let i = 0; i < 9; ++i) hotbar.push({
            id: hotbarVM.blockIdAt(i), count: hotbarVM.countAt(i), durability: hotbarVM.durabilityAt(i) })
        const main = []
        for (let i = 0; i < 27; ++i) main.push({
            id: hotbarVM.mainBlockIdAt(i), count: hotbarVM.mainCountAt(i), durability: hotbarVM.mainDurabilityAt(i) })
        const armor = []
        for (let k = 0; k < 4; ++k) armor.push({
            id: hotbarVM.armorBlockIdAt(k), count: hotbarVM.armorCountAt(k), durability: hotbarVM.armorDurabilityAt(k) })
        return {
            version: 2,
            px: player.feetPosition.x, py: player.feetPosition.y, pz: player.feetPosition.z,
            yaw: player.yaw, pitch: player.pitch,
            mode: player.mode,
            health: playerState.health, hunger: playerState.hunger,
            xp: playerState.xp,   // t402 经验值累积持久化
            selectedSlot: hotbarVM.selectedSlot,
            hotbar: hotbar, main: main, armor: armor
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
            // t177 二轮复盘：熔炉内容同事务落盘（saveAll 第 3 参 = FurnaceStore::allFurnaces() 产物）。
            // t542：发射器内容同事务落盘（saveAll 第 4 参 = DispenserStore::allDispensers() 产物）。
            worldStore.saveAll(currentWorldName, chestStore.allChests(), furnaceStore.allFurnaces(), dispenserStore.allDispensers())
            // progress 落盘（统计 + 成就，独立 upsert 单行表）。
            worldStore.saveProgress(progress.toVariant())
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
        progressOpen = false    // pause-menu：退出世界关进度面板（防遗留）
        statsOpen = false       // pause-menu：退出世界关统计面板（防遗留）
        resourceBrowserOpen = false   // t458：退出世界时关资源查看器（防遗留）
        itemEntities.clearAll()
        entityManager.clearAll()
        xpOrbs.clearAll()   // t402 经验球同族实体，切世界必清
        carts.clearAll()    // t565 矿车同族实体，切世界必清
        player.release()
        appState = "worldlist"
        audio.stopAmbient()   // t177 环境音：退出世界停风声床（菜单态无声）
        audio.stopWaterFlow() // t223 水流声：退出世界停（菜单态无声；离开流水范围本会自停，此处显式保干净）
        audio.stopLavaFlow()  // t343 岩浆声：退出世界停（同水流声）
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
        progressOpen = false           // pause-menu：回菜单关进度面板（防遗留）
        statsOpen = false              // pause-menu：回菜单关统计面板（防遗留）
        resourceBrowserOpen = false    // t458：回菜单时关资源查看器（防遗留）
        // t312：清聊天历史（不持久化 / 不跨世界；下一局从空起）。
        chatMessages.clear()
        returnHeldToHotbar()           // t56：返回菜单前归还光标手持栈（防遗留 heldBlock）
        worldStore.closeWorld()        // t176：回主菜单关存档连接（防残留打开库）
        itemEntities.clearAll()        // t176：清实体残留
        entityManager.clearAll()
        xpOrbs.clearAll()   // t402 经验球同族实体，切世界必清
        carts.clearAll()    // t565 矿车同族实体，回主菜单必清
        player.release()
        appState = "menu"
        audio.stopAmbient()   // t177 环境音：回主菜单停风声床
        audio.stopWaterFlow() // t223 水流声：回主菜单停（菜单态无声）
        audio.stopLavaFlow()  // t343 岩浆声：回主菜单停（同水流声）
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

        // t374 群系化被动生物分布：在整张地图随机散布一群被动生物，每只类型按其所在群系加权选取
        //   （entityManager.pickPassiveMobType ← theWorld.biomeIdAt）。机制等价 MC 1.0 出生时被动生物群按群系
        //   分布（平原多牛羊、森林多猪；非排斥，仅概率差异）。上方固定 3 只保出生点附近必见三类；本散布群提供
        //   群系化的统计偏移（spec「草原多见牛羊、森林多见猪」）。散布点取整图随机列、地表上方一格；跳水面 /
        //   头顶非空（树干 / 原木）列避免刷进树里或水里。
        //   t562：散布数量 20→10（「白天一堆怪」—— 旧散布 + 繁殖（kPassiveMobCap=24）+ 敌对可堆到 60+ 只，
        //   小世界密度过高）。散布仍是「进世界一次性生成」非持续刷怪；数量收紧到小世界合理密度。
        const kScatterCount = 10
        const wdim = theWorld.width
        for (let i = 0; i < kScatterCount; ++i) {
            const sx = 4 + Math.floor(Math.random() * (wdim - 8)) // [4, wdim-4)，避世界边
            const sz = 4 + Math.floor(Math.random() * (wdim - 8))
            const sh = theWorld.heightAt(sx, sz)
            if (sh <= 0) continue
            const surface = theWorld.blockAt(sx, sh, sz)
            if (surface === 21 /* Water */ || surface === 0 /* Air */) continue // 非陆地 / 水面
            const headroom = theWorld.blockAt(sx, sh + 1, sz)
            if (headroom !== 0 /* Air */ && headroom !== 24 /* TallGrass */) continue // 头顶非空（树干等）
            const mt = entityManager.pickPassiveMobType(theWorld.biomeIdAt(sx, sz))
            const col = (mt === EntityManager.MobCow) ? "#5a4030"
                     : (mt === EntityManager.MobSheep) ? "#f5f0e8"
                     : (mt === EntityManager.MobChicken) ? "#f5f0e4" : "#f0a8b0"
            entityManager.spawnMobTyped(sx, sh + 1, sz, mt, col, 10)
        }

        // t399/t450 鱿鱼水生散布（spec「squid ... swims in water bodies」；机制等价 MC 1.0 squid 在水里生成）：
        //   在整图随机散布一群鱿鱼，**仅取水面列**（blockAt==Water；跳陆地 / 空气），在水面格生成 → EntityManager
        //   aiSquid 喷水推进游动（feet 入水即触发水中物理 + 周期上浮）。数量少（kSquidTargetCount）免水底塞满。
        //   无群系门控（MC 1.0 squid 各群系水体均可，本工程简化为全图水面随机）。col 占位串（MobSquid 走 MobModel
        //   + 贴图，不读 color；mobType 0 UnitCube 路径才读，此处不涉）。
        //
        //   t450 根因：旧实现用 heightAt 取「水面 y」—— heightAt 是**纯 worldgen fBm 自然地表高度**（不含水 / 不含
        //   海域重塑），对任何水柱恒指向「水底地形或水面之上的空气」：
        //     - 海域列：generate 把地形重塑成沙海底（seaColumnHeight，~52-59），fillWater 在 [海底+1, 水位58] 灌水；
        //       而 heightAt 返纯 fBm（~62-66，高于水位）→ blockAt(heightAt) 是水面之上的空气 → 非水。
        //     - 沼泽/湖泊列：水偶发恰好压在自然地表高度上才命中，概率极低。
        //   故 blockAt(heightAt)==Water 恒 false → 鱿鱼 0 生成。改用 heightmapAt（列顶首个非空气，**含水面** ——
        //   fillWater 把水置入后 setBlock 增量维护把 heightmap 抬到水面 y）：对水柱返水面 y、blockAt==Water 命中；
        //   对陆地返草/树顶不命中跳过；空列返 -1 由 qsh<=0 跳过。这才是「在水面格生成」的正确高度查询。
        //
        //   另：水柱占比低（海角四分之一盘 + 沼泽/湖泊零星 ≈9%）。旧「固定 kSquidScatterCount 次随机拒绝采样」期望
        //   命中仅 ~0.5、过半存档 0 鱿鱼（即便修了 heightmap 仍不可靠）。改「刷到目标 kSquidTargetCount 或耗尽
        //   kSquidMaxAttempts 次尝试」：每命中 1 个水柱就 +1，达目标即停（充分采样水柱）；无水世界耗尽尝试刷 0（正确）。
        //   每次尝试仅 2 次 O(1) 读（heightmapAt + blockAt），200 次上限在进世界时一次性开销可忽略。
        const kSquidTargetCount = 3
        const kSquidMaxAttempts = 200
        let squidSpawned = 0
        for (let i = 0; i < kSquidMaxAttempts && squidSpawned < kSquidTargetCount; ++i) {
            const qsx = 4 + Math.floor(Math.random() * (wdim - 8)) // [4, wdim-4)，避世界边
            const qsz = 4 + Math.floor(Math.random() * (wdim - 8))
            const qsh = theWorld.heightmapAt(qsx, qsz) // 列顶首个非空气（含水面；非 worldgen 自然地表 heightAt）
            if (qsh <= 0) continue
            if (theWorld.blockAt(qsx, qsh, qsz) !== 21 /* Water */) continue // 非水面 → 跳（仅水里生成）
            entityManager.spawnMobTyped(qsx, qsh, qsz, EntityManager.MobSquid, "#6a4a3a", 10)
            ++squidSpawned
        }
        console.info("[t450] squid scattered: " + squidSpawned + "/" + kSquidTargetCount) // 进世界一次性核对（非每帧）

        // t480 狼（Wolf）散布（spec「森林/针叶林群系生成」；机制等价 MC 1.0 狼在森林/针叶林群系生成）：
        //   在整图随机散布一群狼，**仅取森林 / 针叶林群系列**（theWorld.biomeIdAt==3 Forest / ==4 Snowy；跳过其余
        //   群系，机制等价 MC 狼只在 taiga/forest 群系刷）。每只落在该列地表上方一格（同被动散布：跳水面 / 头顶
        //   非空列）。数量少（kWolfTargetCount=4）免成群威胁 + 繁殖可增（驯服后喂肉繁殖）。col 占位串（MobWolf 走
        //   MobModel + mob_wolf 贴图，不读 color；mobType 0 UnitCube 路径才读，此处不涉）。maxHealth=10（同被动）。
        //   未驯服狼敌对玩家（aiWolf）→ 散布到森林 / 雪原深处（不在出生点平原），玩家探索时偶遇（机制等价 MC 野狼）。
        const kWolfTargetCount = 2
        const kWolfMaxAttempts = 160
        let wolfSpawned = 0
        for (let i = 0; i < kWolfMaxAttempts && wolfSpawned < kWolfTargetCount; ++i) {
            const wx = 4 + Math.floor(Math.random() * (wdim - 8)) // [4, wdim-4)，避世界边
            const wz = 4 + Math.floor(Math.random() * (wdim - 8))
            const bio = theWorld.biomeIdAt(wx, wz)
            if (bio !== 3 && bio !== 4) continue               // 仅森林(3) / 针叶林(4) 群系（spec）
            const wh = theWorld.heightAt(wx, wz)
            if (wh <= 0) continue
            const wsurface = theWorld.blockAt(wx, wh, wz)
            if (wsurface === 21 /* Water */ || wsurface === 0 /* Air */) continue // 非陆地 / 水面
            const wheadroom = theWorld.blockAt(wx, wh + 1, wz)
            if (wheadroom !== 0 /* Air */ && wheadroom !== 24 /* TallGrass */) continue // 头顶非空（树干等）
            entityManager.spawnMobTyped(wx, wh + 1, wz, EntityManager.MobWolf, "#6a6a6a", 10)
            ++wolfSpawned
        }
        console.info("[t480] wolf scattered: " + wolfSpawned + "/" + kWolfTargetCount) // 进世界一次性核对（非每帧）

        // t481 豹猫（Ocelot）散布（spec「丛林群系生成」；机制等价 MC 1.0 豹猫在丛林群系生成）：
        //   在整图随机散布一群豹猫，**仅取丛林群系列**（theWorld.biomeIdAt==6 Jungle；跳过其余群系，机制等价
        //   MC 1.0 豹猫只在丛林刷）。每只落在该列地表上方一格（同被动散布：跳水面 / 头顶非空列）。数量少
        //   （kOcelotTargetCount=3）免成片聚集 + 繁殖可增（驯服后喂生鱼繁殖）。col 占位串（MobOcelot 走 MobModel
        //   + 贴图，不读 color；mobType 0 UnitCube 路径才读，此处不涉）。maxHealth=10（同被动）。未驯服豹猫被动
        //   游荡（aiOcelot 未驯服分支）→ 散布到丛林深处（不在出生点平原），玩家探索丛林时偶遇（机制等价 MC 野豹猫）。
        const kOcelotTargetCount = 2
        const kOcelotMaxAttempts = 200
        let ocelotSpawned = 0
        for (let i = 0; i < kOcelotMaxAttempts && ocelotSpawned < kOcelotTargetCount; ++i) {
            const ox = 4 + Math.floor(Math.random() * (wdim - 8)) // [4, wdim-4)，避世界边
            const oz = 4 + Math.floor(Math.random() * (wdim - 8))
            if (theWorld.biomeIdAt(ox, oz) !== 6) continue        // 仅丛林群系（biomeIdAt==6 Jungle，spec）
            const oh = theWorld.heightAt(ox, oz)
            if (oh <= 0) continue
            const osurface = theWorld.blockAt(ox, oh, oz)
            if (osurface === 21 /* Water */ || osurface === 0 /* Air */) continue // 非陆地 / 水面
            const oheadroom = theWorld.blockAt(ox, oh + 1, oz)
            if (oheadroom !== 0 /* Air */ && oheadroom !== 24 /* TallGrass */) continue // 头顶非空（树干等）
            entityManager.spawnMobTyped(ox, oh + 1, oz, EntityManager.MobOcelot, "#c8924a", 10)
            ++ocelotSpawned
        }
        console.info("[t481] ocelot scattered: " + ocelotSpawned + "/" + kOcelotTargetCount) // 进世界一次性核对（非每帧）
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
        else if (enchantingPanel.visible)    enchantingPanel.swapHoveredWithHotbar(hotbarIdx)
        else if (anvilPanel.visible)         anvilPanel.swapHoveredWithHotbar(hotbarIdx)
        else if (dispenserPanel.visible)     dispenserPanel.swapHoveredWithHotbar(hotbarIdx)
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
        else if (enchantingPanel.visible)    panel = enchantingPanel
        else if (anvilPanel.visible)         panel = anvilPanel
        else if (dispenserPanel.visible)     panel = dispenserPanel
        else if (inventoryPanel.visible)     panel = inventoryPanel
        else if (survivalPanel.visible)      panel = survivalPanel
        if (!panel) return
        const st = panel.readSlot(group, index)
        if (!st || st.id === 0 || st.count <= 0) return           // 空槽 → 无操作
        const n = dropAll ? st.count : 1
        // review rv3：写回透传实例耐久 / 附魔（st 由 panel.readSlot 读出，含 durability/enchants）——
        //   -1 件路径若只传 (id, count) 会把工具 / 护甲槽清成「新实例」（耐久回满 / 附魔丢失）。
        if (dropAll || st.count <= 1) panel.writeSlot(group, index, 0, 0)            // 清空
        else                          panel.writeSlot(group, index, st.id, st.count - 1, st.durability, st.enchants) // -1 件（保真）
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
        progress.onInventoryOpened()  // progress 成就：打开背包
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
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        craftingTableOpen = true
        progress.onInventoryOpened()  // progress 成就：打开背包（工作台）
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
    // t494 携熔炉格世界坐标（fx/fy/fz）→ 存 window.furnaceX/Y/Z，供 FurnaceUI 经 setFurnaceLit 翻燃烧 bit。
    function openFurnace(fx, fy, fz) {
        if (appState !== "playing" || furnaceOpen) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        furnaceX = fx; furnaceY = fy; furnaceZ = fz  // t494 记熔炉格坐标（供 FurnaceUI setFurnaceLit）
        furnaceOpen = true
        progress.onInventoryOpened()  // progress 成就：打开背包（熔炉）
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
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        chestX = x; chestY = y; chestZ = z
        // t225 读箱子朝向 state（前面所朝方向；placeBlock 写入 = horizontalFacing^1，锁面朝玩家）→
        //   驱动盖子铰链侧（chestLidYaw）。& 3 防御性掩码（与 BlockRegistry::chestFrontFace 的 state&3 同源）。
        chestFacing = theWorld.stateAt(x, y, z) & 3
        // t393 地牢箱首开填充战利品：worldgen placeDungeons 给地牢箱 state 置 ChestStateDungeonFlag(bit2) →
        //   isDungeonChest 返 true。首开（ChestStore 无该坐标条目）时由 LootTable 抽 8 件分散入随机空槽
        //   （坐标确定性 seed → 同箱同战利品；已开过则 populateDungeonLoot 返 false 不再生）。玩家自放箱无此
        //   标记 → 不填（机制对齐 MC）。在 chestOpen=true / 盖子动画之前填 → ChestUI 显时即见战利品。
        if (theWorld.isDungeonChest(x, y, z)) chestStore.populateDungeonLoot(x, y, z)
        // t484 废弃矿井箱首开填充战利品：worldgen placeMineshaft 给矿井箱 state 置 ChestStateMineshaftFlag(bit3) →
        //   isMineshaftChest 返 true。首开时由 LootTable::mineshaftChestPool 抽 6 件（矿物 / 附魔书 / 铁锭等）
        //   分散入随机空槽（坐标确定性 seed → 同箱同战利品）。同地牢箱机制（一份首开一次性 roll）。
        if (theWorld.isMineshaftChest(x, y, z)) chestStore.populateMineshaftLoot(x, y, z)
        // t485 沙漠神殿箱首开填充战利品：worldgen placeDesertTemple 给神殿箱 state 置 ChestStatePyramidFlag(bit4) →
        //   isPyramidChest 返 true。首开时由 LootTable::pyramidChestPool 抽 4 件（钻石 / 金 / 青金石 / 骨头 / 腐肉等）
        //   分散入随机空槽（坐标确定性 seed → 同箱同战利品）。同地牢 / 矿井箱机制（一份首开一次性 roll）。
        if (theWorld.isPyramidChest(x, y, z)) chestStore.populatePyramidLoot(x, y, z)
        // t486 丛林神殿箱首开填充战利品：worldgen placeJungleTemple 给神殿箱 state 置 ChestStateJungleFlag(bit5) →
        //   isJungleTempleChest 返 true。首开时由 LootTable::jungleTempleChestPool 抽 5 件（骨头 / 腐肉 / 铁 / 金 /
        //   钻石 / 箭 / 附魔书等）分散入随机空槽（坐标确定性 seed → 同箱同战利品）。同地牢 / 矿井 / 神殿箱机制
        //   （一份首开一次性 roll）。
        if (theWorld.isJungleTempleChest(x, y, z)) chestStore.populateJungleTempleLoot(x, y, z)
        // t487 要塞箱首开填充战利品：worldgen placeStronghold 给要塞箱 state 置 ChestStateStrongholdFlag(bit6) →
        //   isStrongholdChest 返 true。首开时由 LootTable::strongholdChestPool 抽 6 件（末影之眼 / 骨头 / 腐肉 /
        //   铁锭 / 青金石 / 红石 / 钻石 / 附魔书等）分散入随机空槽（坐标确定性 seed → 同箱同战利品；末影之眼是
        //   激活传送门的关键物品，机制等价 MC 1.0 要塞战利品）。同地牢 / 矿井 / 神殿 / 丛林神殿箱机制（一份首开一次性 roll）。
        if (theWorld.isStrongholdChest(x, y, z)) chestStore.populateStrongholdLoot(x, y, z)
        chestOpen = true
        progress.onInventoryOpened()  // progress 成就：打开背包（箱子）
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
    // t474 打开 / 关闭附魔台面板。打开 → release（光标可见点选项槽）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen / craftingTableOpen / furnaceOpen / chestOpen 互斥（开附魔台前关其它四个，反之同）。
    //   x/y/z = 所开附魔台的方块世界坐标（player.enchantingTableOpened 携带 → UI 据此查书架加成）。
    function openEnchantingTable(x, y, z) {
        if (appState !== "playing" || enchantingTableOpen) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        if (chestOpen) closeChest()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        enchantX = x; enchantY = y; enchantZ = z
        enchantingTableOpen = true
        // t548：不再调 progress.onInventoryOpened —— 该调用会把「打开背包」成就解锁当 toast 弹在面板之上
        //   （z=170 黑色小 UI 盖主 UI 右下角，即用户报「三 UI 底部黑色残留」根因；附魔台非背包，语义亦不符）。
        player.release()
    }
    function closeEnchantingTable() {
        if (!enchantingTableOpen) return
        enchantingTableOpen = false
        player.grab()
        keyInput.forceActiveFocus()
    }

    // t477 打开 / 关闭铁砧面板。打开 → release（光标可见点按钮 / 输入名）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen / craftingTableOpen / furnaceOpen / chestOpen / enchantingTableOpen 互斥。
    //   x/y/z = 所开铁砧的方块世界坐标（player.anvilOpened 携带 → UI 据此调 player.damageAnvil 推进损坏）。
    function openAnvil(x, y, z) {
        if (appState !== "playing" || anvilOpen) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        if (chestOpen) closeChest()
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        anvilX = x; anvilY = y; anvilZ = z
        anvilOpen = true
        // t548：同附魔台 —— 铁砧非背包，不触发 open_inventory 成就 toast（黑色小 UI 残留根因）。
        player.release()
    }
    function closeAnvil() {
        if (!anvilOpen) return
        anvilOpen = false
        player.grab()
        keyInput.forceActiveFocus()
    }
    // t517 打开 / 关闭发射器面板。打开 → release（光标可见点发射器 / 主栏槽）；关 → grab + 焦点回键位层。
    // 与 inventoryOpen / craftingTableOpen / furnaceOpen / chestOpen / enchantingTableOpen / anvilOpen 互斥
    //   （开发射器前关其它六个，反之同）。x/y/z = 所开发射器的方块世界坐标（player.dispenserOpened 携带；
    //   t517 本轮坐标暂留作后续 DispenserStore per-block 寻址用）。关包归还光标手持栈（同 closeChest /
    //   closeFurnace —— DispenserUI 共享 hotbar VM 光标栈，关包须归还有物）。
    function openDispenser(x, y, z) {
        if (appState !== "playing" || dispenserOpen) return
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        if (chestOpen) closeChest()
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        dispenserX = x; dispenserY = y; dispenserZ = z
        dispenserOpen = true
        // t548：同附魔台 / 铁砧 —— 发射器非背包，不触发 open_inventory 成就 toast（黑色小 UI 残留根因）。
        player.release()
    }
    function closeDispenser() {
        if (!dispenserOpen) return
        dispenserOpen = false
        returnHeldToHotbar()           // t56：关包归还光标手持栈（同 closeChest / closeFurnace）
        player.grab()
        keyInput.forceActiveFocus()
    }

    // t312 聊天栏开关 / 收发（PLAN §2 UI 层，纯呈现；聊天历史 = ListModel 呈现态，无 C++ ViewModel ——
    //   单机 Phase 1.0 无联机，聊天仅为「输入回显 + 系统播报」容器，Phase 3 联机接入时改走 LocalServer/
    //   RemoteServer 协议层收发）。开 → release 指针（光标可见 + TextField 取焦点，同背包面板模式）；关 →
    //   grab + 焦点回键位层。与背包/工作台/熔炉/箱子互斥（开前关其它）。
    // pause-menu 右下角 toast 显示（成就解锁 + 占位按钮「敬请期待」共用）。仅 playing 态显（菜单态无 toast）。
    //   3 秒后 infoToastTimer 清空；连点不同源（如连续解锁两成就）→ restart 计时延后 + 文案覆盖最新。
    function showInfoToast(text) {
        infoToastText = text
        infoToastVisible = window.appState === "playing"
        if (infoToastVisible) infoToastTimer.restart()
    }
    function openChat() {
        if (appState !== "playing" || chatOpen) return
        // 互斥：先关任何已开背包面板（归还光标手持栈 + grab），随后 release 让聊天接管光标。
        if (inventoryOpen) closeInventory()
        if (craftingTableOpen) closeCraftingTable()
        if (furnaceOpen) closeFurnace()
        if (chestOpen) closeChest()
        if (enchantingTableOpen) closeEnchantingTable()
        if (anvilOpen) closeAnvil()
        if (dispenserOpen) closeDispenser()
        // 死亡态不开聊天（死亡信息已由死亡屏接管；防聊天 input 抢死亡按钮焦点）。
        if (playerState.dead) return
        chatOpen = true
        historyCursor = -1             // t347：每次开聊天从草稿态起（不延续上次浏览位置）
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
    //   t314 `/give` 调试聊天命令（spec t314）：debug 命令无视游戏模式（创造 / 生存 / 观察者都可调，见
    //     hotbarVM.give），返回回显文案（系统色灰）；不进玩家消息回显（MC 聊天惯例：命令只显结果）。
    //   t347 命令分发（spec t347）：`/` 开头按「命令词（首个空白前）」查 commandRegistry 分发到对应 handler；
    //     未注册的 `/xxx` 仍当普通玩家消息（保留旧回退语义，避免破坏既有用法）。新增命令 = 往 commandRegistry
    //     加一项，无需改本函数（机制等价 MC 命令分发器：注册即生效）。命令词按空白边界匹配 → /givex 不会
    //     误触 /give（比旧 startsWith 前缀判更严格且自然）。
    //   t347 命令历史（spec t347）：每条已发送非空行 pushHistory 入 commandHistory，供 chatInput Up/Down 回溯。
    function sendChat() {
        const raw = chatInput.text
        const txt = raw.trim()
        if (txt.length > 0) {
            pushHistory(txt)
            if (txt.charAt(0) === "/") {
                const sp = txt.search(/\s/)                 // 首个空白（命令词边界）；-1 = 无参命令
                const name = (sp < 0) ? txt.slice(1) : txt.slice(1, sp)
                const rest = (sp < 0) ? "" : txt.slice(sp)  // 含前导空格（与原 /give rest 语义一致，C++ split 兜底 trim）
                const entry = commandRegistry[name]
                if (entry) {
                    const echo = entry.run(rest)
                    if (echo && echo.length > 0)
                        appendChatMessage("", echo, true)
                } else {
                    appendChatMessage(window.playerName, txt, false)   // 未知 / 命令 → 当玩家消息
                }
            } else {
                appendChatMessage(window.playerName, txt, false)
            }
        }
        chatInput.text = ""
        historyCursor = -1
        closeChat(true)
    }
    // t346 `/give` 参数提示文案（spec t346）：据当前输入文本返回下一个待输入参数的提示串（MC 风格 inline hint，
    //   由下方 giveHint Text 行显示）。参数序：id → 数量 → 耐久。光标在 token 中途 → 期望当前 token；已敲尾空格
    //   → 期望下一 token（等价 MC 命令补全按空格推进）。仅 `/give` 精确前缀触发（同 sendChat 路由，避免 /givex
    //   误触）；非 /give 输入返空串（提示行隐藏）。纯呈现逻辑，零 Game/World 依赖。
    function giveHintText(t) {
        if (t !== "/give" && !t.startsWith("/give ")) return ""
        const rest = t.slice(5)                              // "/give" 之后剩余（"" / " 3" / " 3 64" ...）
        const endsWithSpace = rest.endsWith(" ")
        const trimmed = rest.trim()
        const n = trimmed.length === 0 ? 0 : trimmed.split(/\s+/).length
        // 特例 t==="/give"（无尾空格）→ 还没开始首参，期望 idx=0；否则尾空格 → 新 token 期望 idx=n，中途 → idx=n-1。
        let idx = (t === "/give") ? 0 : (endsWithSpace ? n : n - 1)
        const names = ["id（方块/工具/材料 id）", "数量（缺省 1）", "耐久（仅工具，缺省满耐久）"]
        if (idx < 0 || idx >= names.length) return ""
        return "下一个参数: " + names[idx]
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

    // t347 命令分发表（spec t347）：command → {desc, run(rest)} 的可扩展注册表，替代硬编码 if 链。
    //   新增命令 = 往此对象加一项（desc 供 /help 列出；run 接「命令词后剩余串（含前导空格）」返系统回显串，
    //   返空串则不显）；sendChat 按命令词查表分发，无需改路由逻辑。机制等价 MC 命令分发器（注册即生效）。
    property var commandRegistry: ({
        "give": {
            desc: "/give <id> [数量] [耐久] —— 给予玩家物品",
            run: function(rest) { return hotbarVM.give(rest) }
        },
        "help": {
            desc: "/help —— 列出可用命令",
            run: function(rest) { return window.helpText() }
        },
        // t378 /kill 命令（spec t378）：无参=自杀；@e=清除所有非玩家实体；@e[type=类型]=按实体类型清除。
        //   选择器机制等价 MC 1.0（@e=all entities；§9 区隔：僵尸/骷髅/苦力怕 → shambler/bones/stalker）。
        "kill": {
            desc: "/kill [@e[type=类型]] —— 无参自杀；@e 清除所有非玩家实体；@e[type=类型] 按类型清除",
            run: function(rest) { return window.runKill(rest) }
        },
        // misc 二轮 `/time` 指令（spec：聊天一键设/加时间）。语义：phase 0=正午 0.25=黄昏 0.5=子夜 0.75=黎明。
        //   /time set day|night|midnight|<num>[d]  /time add <num>。num 当 MC 0-24000 ticks（0=正午 6000=日落
        //   12000=子夜 18000=日出）→ phase=num/24000；num+d（如 3d）= 设第几天（月相）。
        "time": {
            desc: "/time set <day|night|midnight|数字[d]> | /time add <数字> —— 设/加时间",
            run: function(rest) { return window.runTime(rest) }
        }
    })
    // misc 二轮 `/time` 解析（spec）：rest 含前导空格如 " set day"。子命令 set/add。
    //   phase 语义：0=正午 0.25=黄昏 0.5=子夜 0.75=黎明（与 WorldClock 一致）。
    //   · set day → phase 0（白天）；set night → 0.5（深夜）；set midnight → 0.5；set <num> → phase=num/24000（MC ticks）；
    //     set <num>d → 设第 num 天（月相=num%8），phase 不变。
    //   · add <num> → 当前 phase + num/24000（跨天自动）。
    //   返系统回显串（如「时间设为白天」）。
    function runTime(rest)
    {
        const args = rest.trim().split(/\s+/)        // ["set","day"] / ["add","1000"] 等
        if (args.length < 2 || args[0] === "")
            return "用法: /time set <day|night|midnight|数字[d]> | /time add <数字>"
        const sub = args[0].toLowerCase()
        const val = args[1]
        if (sub === "set") {
            const v = val.toLowerCase()
            if (v === "day")       { worldClock.setPhase(0.0);  return "时间设为白天（正午）" }
            if (v === "night")     { worldClock.setPhase(0.5);  return "时间设为夜晚（子夜）" }
            if (v === "midnight")  { worldClock.setPhase(0.5);  return "时间设为子夜" }
            // <num>d → 设第几天（月相）；<num> → phase=num/24000
            const m = val.match(/^(-?\d+)d$/i)
            if (m) { worldClock.setDay(parseInt(m[1], 10)); return "设为第 " + m[1] + " 天（月相刷新）" }
            const n = parseFloat(val)
            if (!isNaN(n)) { worldClock.setPhase(n / 24000.0); return "时间设为 " + val + " ticks" }
            return "未知时间值: " + val + "（用 day/night/midnight/数字/数字d）"
        }
        if (sub === "add") {
            const n = parseFloat(val)
            if (isNaN(n)) return "/time add 需要数字（ticks）"
            worldClock.addPhase(n / 24000.0)
            return "时间增加 " + val + " ticks"
        }
        return "未知子命令: " + sub + "（用 set 或 add）"
    }
    // t378 实体类型名 → EntityManager.MobType 枚举 id 的映射（/kill @e[type=...] 用；§9 区隔改名 mob）。
    //   name 命中 → 对应枚举 id；未知 → -1。机制等价 MC 1.0 实体类型选择器（@e[type=pig]）。
    function mobTypeIdFromName(name) {
        const m = {
            "test": EntityManager.MobTest,
            "pig": EntityManager.MobPig,
            "cow": EntityManager.MobCow,
            "sheep": EntityManager.MobSheep,
            "shambler": EntityManager.MobShambler,
            "bones": EntityManager.MobBones,
            "stalker": EntityManager.MobStalker,
            "spider": EntityManager.MobSpider,
            "chicken": EntityManager.MobChicken,
            "squid": EntityManager.MobSquid
        }
        return (name in m) ? m[name] : -1
    }
    // t378 /kill 命令分发（spec t378）：rest = 命令词后剩余串（含前导空格，由 sendChat 传）。
    //   trim 后空 → 自杀（扣满血击杀玩家，走标准 takeDamage → 死亡界面 / 聊天播报链）；
    //   "@e" / "@e[type=类型]" → 实体选择器：无 type 过滤 → 清除所有非玩家实体（mob / 下落方块 / 箭矢）；
    //   有 type 过滤 → 仅清匹配类型的 Mob（下落方块 / 箭矢无 mobType 语义，type 过滤跳过）。player 不在
    //   EntityManager → clearAll / 过滤天然不含玩家（满足 spec「除玩家外所有实体」）。未知选择器 / 类型 → 回显用法。
    function runKill(rest) {
        const arg = rest.trim()
        if (arg.length === 0) {
            playerState.takeDamage(playerState.health, PlayerState.Generic)
            return "已自杀"
        }
        if (arg.charAt(0) !== "@") return "用法: /kill [@e[type=类型]]"
        const sel = arg.slice(1)
        if (sel !== "e" && !sel.startsWith("e[")) return "未知选择器: @" + sel + "（仅支持 @e）"

        // 解析 [type=类型] 过滤（容缺右 ]、容空白）。
        let typeFilter = ""
        const lb = sel.indexOf("[")
        if (lb >= 0) {
            const rb = sel.lastIndexOf("]")
            const body = rb > lb ? sel.slice(lb + 1, rb) : sel.slice(lb + 1)
            const m = body.match(/type\s*=\s*(\w+)/)
            if (m) typeFilter = m[1].toLowerCase()
        }

        if (typeFilter.length === 0) {
            entityManager.clearAll()      // mobs + 下落方块 + 箭矢 + 雪球（Entities 全清）
            itemEntities.clearAll()       // 掉落物（修 /kill @e 漏清掉落物 → F3 items 不归零）
            xpOrbs.clearAll()             // 经验球（同族一并清）
            carts.clearAll()              // t565 矿车（同族一并清）
            // 三类 clearAll 各自 emit 自家 entitiesChanged（mobs/items/orbs）→ 对应 Repeater
            //   delegate 据存活标记 visible=false 隐藏，F3 entities 行 mobs/items/orbs 全归零。
            return "已清除所有非玩家实体"
        }
        const tid = window.mobTypeIdFromName(typeFilter)
        if (tid < 0) {
            return "未知实体类型: " + typeFilter +
                   "（可用: test, pig, cow, sheep, shambler, bones, stalker, spider）"
        }
        let removed = 0
        for (let i = 0; i < entityManager.count; ++i) {
            if (entityManager.aliveAt(i)
                    && entityManager.kindAt(i) === EntityManager.Mob
                    && entityManager.mobTypeAt(i) === tid) {
                entityManager.removeEntityAt(i)
                ++removed
            }
        }
        return "已清除 " + removed + " 个 " + typeFilter
    }
    // t347 /help 文案：依 commandRegistry 实时生成（注册表加项 → /help 自动列出，无需手维护）。多行用 \n
    //   分隔（系统消息走 Text.PlainText，\n 在 PlainText 内作换行，delegate height=implicitHeight 自撑）。
    function helpText() {
        const keys = Object.keys(commandRegistry).sort()
        const lines = ["可用命令:"]
        for (let i = 0; i < keys.length; ++i)
            lines.push("  " + commandRegistry[keys[i]].desc)
        return lines.join("\n")
    }

    // t347 已发送命令历史（spec t347）：最早在前、最新在后；chatInput Up/Down 经 browseHistory 回溯。
    //   仅作 input 草稿回溯用（呈现态），不入 Game/World 层；historyMax 防无限增长。
    property var commandHistory: []
    property int historyCursor: -1            // -1 = 草稿态（未浏览）；>=0 = 指向 commandHistory[index]
    property string historyDraft: ""          // 开始浏览前保存的草稿，Down 越过最新时还原
    readonly property int historyMax: 50
    function pushHistory(text) {
        commandHistory.push(text)
        while (commandHistory.length > historyMax) commandHistory.shift()
    }
    // Up(delta=-1, 更旧) / Down(delta=+1, 更新)：空历史忽略；草稿态按 Down 无效；Down 越过最新回草稿态还原；
    //   其余夹紧。每步把命令行写回 chatInput.text 并把光标置尾（贴近 MC：Up 取出可继续编辑 / 续发）。
    function browseHistory(delta) {
        if (commandHistory.length === 0) return
        if (historyCursor < 0) {
            if (delta > 0) return              // 草稿态按 Down 无效
            historyDraft = chatInput.text      // 首次 Up：保存当前草稿
            historyCursor = commandHistory.length - 1   // 指向最新（末尾）
        } else {
            historyCursor += delta
            if (historyCursor >= commandHistory.length) {
                historyCursor = -1             // Down 越过最新 → 回草稿态还原
                chatInput.text = historyDraft
                chatInput.cursorPosition = chatInput.text.length
                return
            }
            if (historyCursor < 0) historyCursor = 0
        }
        chatInput.text = commandHistory[historyCursor]
        chatInput.cursorPosition = chatInput.text.length
    }

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

    // t414 资源包加载器（Core 层，QML 门面）：启动期解析资源包（settings.json "resourcePack" /
    //   环境变量 / 默认探查），把包内方块贴图缩放到 TILE=16 覆盖程序生成图集对应瓦片。active=true 时
    //   atlasSource = image://rp/atlas（合成图集，由 main.cpp 注册的 provider 提供）；否则回退 qrc 默认。
    //   只读本地 gitignored 包 PNG，零 MC 资产进 qrc（PLAN §9 红线）。无包时引擎仍用程序生成图集正常工作。
    ResourcePackManager { id: resourcePack }

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
    // t470 玩家位置变 → 检测 chunk 边界跨越 → 刷新 chunk visibility。仅 chunksBuilt 后生效（启动初期跳过）。
    //   _updatePlayerChunk 内 _playerCX/_playerCZ 缓存守门：仅在 chunk 坐标真变时才走 O(chunk 数) visibility 重算，
    //   不每 positionChanged 都重算（玩家在同 chunk 内移动 60Hz positionChanged 但零刷新开销）。
    Connections { target: player; function onPositionChanged() { window._updatePlayerChunk() } }
    // progress 走过路程埋点：player 每帧 emit moved(水平位移增量) → progress.onMove 累加（内部 ~0.5s flush）。
    //   纯水平 √(dx²+dz²)，不含跳跃 dy；reportHorizSpeed 是 step 各出口唯一位移瓶颈 → 每帧每路径只计一次。
    //   同 playerMined→onBlockMined / blockPlaced→onBlockPlaced 单向事件流模式（PLAN §2 分层）。
    Connections { target: player; function onMoved(deltaBlocks) { progress.onMove(deltaBlocks) } }

    // t386 闪电击中（雷雨天随机，World::strikeLightning 发）：闪光 + 雷声 + 击中点附近实体伤害的单一入口。
    //   分层（PLAN §2）：World 低层只发 lightningStruck(x,y,z) 语义事件 + 自身焚毁木类方块；呈现层（白闪动画 +
    //   playThunder）与实体层（mob / 玩家近击中点伤害）在此消费。机制等价 MC 1.0 雷击（闪光 + 雷声 + 点燃 + 伤害）。
    //   损伤半径 kLightningHurtRadius（玩家 + mob 距击中点欧氏距离内 → 扣 kLightningDamage HP）。玩家走
    //   playerState.takeDamage（仅 Survival 生效，Creative / Spectator 无伤），mob 走 damageEntity（复用受击链）。
    Connections {
        target: theWorld
        function onLightningStruck(x, y, z) {
            // (1) 屏幕白闪：重启 flashAnim（闪现满白 ~0.85 → 300ms 淡到 0；连击雷重新闪，机制等价 MC 雷击瞬时全屏白闪）。
            lightningFlashAnim.start()
            // (2) 雷声（程序合成，§9 原创）。与白闪 / 引燃同源事件触发。
            audio.playThunder()
            // (3) 击中点附近实体伤害：遍历活体 mob，欧氏距离 ≤ kLightningHurtRadius → damageEntity（扣血 + 红闪 +
            //   归零 mobDied 死亡掉落，复用受击链）。闪电是稀有事件 → 全表扫开销可忽略。
            const r = window.kLightningHurtRadius
            const r2 = r * r
            for (let i = 0; i < entityManager.count(); ++i) {
                if (!entityManager.aliveAt(i)) continue
                const p = entityManager.posAt(i)
                const dx = p.x - x, dy = p.y - y, dz = p.z - z
                if (dx*dx + dy*dy + dz*dz <= r2)
                    entityManager.damageEntity(i, window.kLightningDamage)
            }
            // (4) 玩家近击中点伤害（仅 Survival 生效：takeDamage 内 dead / 模式守；Creative / Spectator 不走此伤害路径）。
            const eye = player.position
            const pdx = eye.x - x, pdy = eye.y - y, pdz = eye.z - z
            if (pdx*pdx + pdy*pdy + pdz*pdz <= r2)
                playerState.takeDamage(window.kLightningDamage)
        }
    }

    // t177 环境音强度 ←→ 昼夜：风声夜间更静谧（level = 0.5 + 0.5*skyLight：白天 1.0、子夜 0.5）。
    //   dayPhaseChanged 每 100ms tick 发（与 clearColor / DirectionalLight 同节拍）→ setAmbientLevel
    //   即时改 looping 风声的音量（在播时；未播仅记值，下次 startAmbient 生效）。纯呈现层桥接，
    //   PLAN §2 分层：Game 层 worldClock.skyLight（只读）→ Core/Platform 层 audio.setAmbientLevel。
    Connections {
        target: worldClock
        function onDayPhaseChanged() { audio.setAmbientLevel(0.5 + 0.5 * worldClock.skyLight) }
    }

    // t223/tXXX 水贴图动画 flipbook 驱动：**已移除**（tXXX 水动画重建消除）。旧 Timer 每 ~2s 翻转
    //   window.waterAnimPhase → 水段 ChunkGeometry setWaterAnimPhase 触发全量 buildMesh(Water) 换 2 帧 UV
    //   （Swamp 场景 261 段/次，mesh 重建风暴第二根因）。2 帧 UV 子区换帧不必重建整段 → 现改**静态水**：
    //   水段 mesh 恒用 phase 0 帧（静水 tile 19 / 流水 tile 23）+ 烘死的空间涟漪（t391，明暗波带质感），
    //   setWaterAnimPhase 不再触发重建（C++ 侧），故本 Timer / waterAnimPhase 属性一并删除（否则每 2s 一次
    //   无意义的 phase 翻转 + QML 绑定重算）。水动画重建从 261 段/2s → 0；视觉损失 = 两帧交替的荡漾动势
    //   （保留单帧 + 涟漪，水面仍有明暗波带、非全平死板；material 级动画（UV offset / shader 位移）留待将来）。
    //   分层（PLAN §2）：动画本属呈现层选择（不进 Game 层 WorldClock），删除后无残留依赖。
    // perf-t520 F3 / HUD 文本节流 Timer：每 100ms（10Hz）调 buildF3Text + buildHudPosText 写
    //   window.f3Text / hudPosText 单一 string 属性。原 text 绑定读 60Hz player.position 等 → 每帧重算
    //   ~30 行字符串 + Q_INVOKABLE（biomeIdAt / liveCount）。节流后频率降到 10Hz（6× 降）。
    //   进 playing 或 F3 切换时由下方 Connections 立即跑一次（避免首帧空白 / 100ms 延迟）。
    //   HUD pos 文本（playing 常驻）：10Hz 节流仍近实时（100ms 延迟肉眼读字 < 200ms 起跳阈值）。
    //   F3 未显时 f3Text 不算（节省）；HUD pos 在 playing 时 always-on 计算。
    Timer {
        id: f3RefreshTimer
        interval: 100
        repeat: true
        running: window.appState === "playing"
        onTriggered: {
            window.hudPosText = window.buildHudPosText()
            if (window.f3Visible) window.f3Text = window.buildF3Text()
        }
    }
    // t489 流体材质级 flipbook 驱动：推进 waterAnimFrame / lavaAnimFrame → 绑水/岩浆段 Texture.positionV
    //   重算 → 帧切换。**绝不触发 setWaterAnimPhase / buildMesh**（C++ 侧 setWaterAnimPhase 已不重建）。
    //   节拍对齐 MC 1.0：水 32 帧 × ~150ms ≈ 4.8s/圈（MC frametime=2 tick=100ms → 3.2s/圈，本引擎略慢保不刺眼）；
    //   岩浆 16 帧 × ~250ms = 4s/圈（MC frametime=2-3 tick）。每帧仅 2 个属性写（waterStripTex/lavaStripTex 的
    //   positionV 绑定重算）→ 开销可忽略（F3 [w]/[s] reb 不回升，验收「不重建 mesh」）。
    //   分层（PLAN §2）：纯呈现层动画（不进 Game 层 WorldClock）；水/岩浆声（onFlowSoundLevelChanged）与
    //   本动画正交（声音走 PlayerController 节流扫描，动画走本 Timer）。
    Timer {
        id: waterAnimTimer
        interval: 150
        repeat: true
        running: true   // 不门控 appState：菜单态水/岩浆段不渲染（visible 绑 vertexCount>0），Timer 空跑零成本
        onTriggered: window.waterAnimFrame = (window.waterAnimFrame + 1) % resourcePack.waterStripFrames
    }
    Timer {
        id: lavaAnimTimer
        interval: 250
        repeat: true
        running: true
        onTriggered: window.lavaAnimFrame = (window.lavaAnimFrame + 1) % resourcePack.lavaStripFrames
    }
    // perf-t520 进 playing 立即刷新（避免 hudPosText 首帧空白），F3 切换 on 时立即刷一次。
    //   本 two-phase Connections 与 10Hz Timer 并行（Timer 100ms 后接管），用 QML 内置信号无需 triggeredOnStartup。
    Connections {
        target: window
        function onAppStateChanged() {
            if (window.appState === "playing") {
                window.hudPosText = window.buildHudPosText()
                if (window.f3Visible) window.f3Text = window.buildF3Text()
            }
        }
        function onF3VisibleChanged() {
            if (window.appState === "playing" && window.f3Visible)
                window.f3Text = window.buildF3Text()
        }
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
    // t343 近岩浆 proximity 岩浆声：PlayerController.lavaSoundLevel（每 ~0.25s 节流扫描更新，0=无近岩浆 / 近=1）
    //   → AudioManager.startLavaFlow/stopLavaFlow/setLavaFlowLevel。机制同水流声（level>0 启动设音量；<=0 停）。
    Connections {
        target: player
        function onLavaSoundLevelChanged() {
            const lvl = player.lavaSoundLevel
            if (lvl > 0.0) {
                audio.startLavaFlow()
                audio.setLavaFlowLevel(lvl)
            } else {
                audio.stopLavaFlow()
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

    // t385 天气变暗的天空色（spec「天空变暗」）：把昼夜 dayNightColor 按天气暗度（theWorld.weatherDarkness
    //   0..1；Thunder 最暗）向暗灰蓝（阴沉 overcast）lerp。雷暴 / 雨天 → 天色明显变暗；晴 → 不变（d=0 原色）。
    //   纯呈现层，只读 worldClock.skyLight + theWorld.weatherDarkness（World 层 Q_PROPERTY；二者 NOTIFY 驱动刷新）。
    function weatherSkyColor() {
        const m = worldClock.skyLight
        let r = 0.043 + (0.620 - 0.043) * m
        let g = 0.063 + (0.776 - 0.063) * m
        let b = 0.149 + (0.910 - 0.149) * m
        const d = theWorld.weatherDarkness
        // 向暗灰蓝 (0.30, 0.32, 0.38) lerp d（保留冷调，非纯灰）。
        r += (0.30 - r) * d
        g += (0.32 - g) * d
        b += (0.38 - b) * d
        // t389 日出日落霞光（spec「天穹日出日落颜色渐变，非仅亮度变」）：黄昏 / 黎明窗口把天色向暖橙红
        //   lerp（sunsetTint），使日出日落呈红 / 橙 / 黄而非仅变暗的蓝。雷暴 / 雨天 d 抑制（云遮日无霞光）。
        const tint = sunsetTint() * (1.0 - 0.8 * d)
        r += (1.00 - r) * tint * 0.85   // 暖橙红目标 (1.00, 0.42, 0.16)；×0.85 上限避免纯橙
        g += (0.42 - g) * tint * 0.85
        b += (0.16 - b) * tint * 0.85
        return Qt.rgba(r, g, b, 1.0)
    }

    // t389 日出日落霞光强度 [0,1]：在黄昏(dayPhase 0.25) / 黎明(0.75) 各开一窗（±0.12 phase），峰值 1、窗外 0。
    //   读 dayPhase 而非 skyLight —— skyLight 在黄昏 / 黎明同为 0.5，无法与接近正午 / 子夜（0.9 / 0.1）区分；
    //   唯有 dayPhase 能定位「太阳贴地平」的瞬间。三角窗 + smoothstep 使起落柔和（无突变）。
    //   纯呈现层，只读 worldClock.dayPhase（dayPhaseChanged 每 tick 驱动刷新）。
    function sunsetTint() {
        const p = worldClock.dayPhase
        const dusk = Math.max(0, 1 - Math.abs(p - 0.25) / 0.12)
        const dawn = Math.max(0, 1 - Math.abs(p - 0.75) / 0.12)
        const t = Math.max(dusk, dawn)
        return t * t * (3 - 2 * t)
    }

    // t389 星空透明度 [0,1]：夜间淡入（skyLight≤0.2 满星、≥0.5 全隐），昼隐；雷暴 / 雨天 weatherDarkness
    //   抑制（云遮星，d=1 → 几乎无星）。供 SkyDome Model opacity 绑定（dayPhaseChanged 每 tick 刷）。
    function starOpacity() {
        const m = worldClock.skyLight
        const a = Math.max(0, Math.min(1, (0.5 - m) / 0.3))
        return a * (1.0 - 0.85 * theWorld.weatherDarkness)
    }

    // t384 云层 baseColor（昼白 / 夜灰暗）：m=worldClock.skyLight ∈ [0,1]（0=子夜、1=正午）。
    //   lerp 灰阶 0.30(夜)↔1.0(昼)：夜云呈暗灰（用户「grey/dark night」）、昼云呈白（「white day」）。
    //   纯灰阶（不偏色）—— 与地形 terrainLight 同设计：夜色基调由 sky clearColor 提供，云只调明度。
    //   t385：天气暗度（theWorld.weatherDarkness）把云再向暗灰 0.45 拉（风暴时云变暗灰，非白）。
    //   NoLighting 材质下最终色 = baseColor × 贴图（白）→ baseColor 直接决定云的明暗。
    function cloudColor(m) {
        let k = 0.30 + (1.0 - 0.30) * m
        k += (0.45 - k) * theWorld.weatherDarkness   // t385 风暴时云变暗灰
        return Qt.rgba(k, k, k, 1.0)
    }

    // t121：地形 chunk 顶点色现已承载天光遮蔽（见天 1.0 / 地下 0.2，mesher 按 chunk heightmap 烘焙进
    //   ColorSemantic）。移除原 nightTint 全屏叠层后，昼夜天光乘子改由材质 baseColor（灰阶亮度）承载——
    //   PrincipledMaterial vertexColorsEnabled=true 时最终色 = baseColor × vertexColor × 贴图，即
    //   「昼夜乘子 × 天光遮蔽 × 贴图」：地表昼 = 1.0×1.0×tex、地表夜 = 0.4×1.0×tex、洞穴昼 = 1.0×0.2×tex。
    //   m=skyLight ∈ [0,1]（0=子夜、1=正午）；floor 0.4 ≈ 原 nightTint alpha 0.6 把地形拉暗的等效量级
    //   （夜间仍可辨识地形轮廓，spec t09）。纯灰阶乘子（不偏色）——夜色基调由 sky clearColor 提供。
    //
    // R19 B6（PLAN §2-H 夜间火把发光修复）：本函数不再用于**地形 / 水 / 玻璃 / 冰 / cutout / 掉落沙**材质
    //   的 baseColor（它们的昼夜乘子已改由 C++ mesher 烘进顶点色**天光分量** dayMul，方块光时间不变、夜间火把
    //   全亮发光）。本函数仍供：(1) skyDayMul 标量（注入 ChunkGeometry/BlockCube dayMul）；(2) 顶点色恒白 1.0 的
    //   呈现路径（mob 体色 / 不接 world 的 BlockCube 掉落物 / 手持图标 / 箱子方块）作 baseColor 灰阶调制 ——
    //   这些路径无 flood-fill 方块光可被压暗，baseColor 是唯一昼夜调制点，旧用法正确保留。
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

    // t377 mob 护甲 Model 的 baseColor：tier 色（与玩家 / MaterialIcon 护甲配色同源）× 昼夜 terrainLight，
    //   受击红闪（hurtFlashAt>0 → #ff0000，同 mob 体色红闪）。armorId = 0x300 + tier*4 + piece → tier = (id-0x300)/4。
    //   spec t377「mobs spawn with RANDOM armor ... material-colored」。
    function mobArmorColor(entIdx, armorId) {
        entityManager.revision
        if (entityManager.hurtFlashAt(entIdx) > 0) return "#ff0000"
        const tier = Math.floor((armorId - 0x300) / 4)
        const cols = [
            [0.541, 0.353, 0.169], // 皮革
            [0.847, 0.847, 0.847], // 铁
            [0.784, 0.471, 0.314], // 铜
            [0.980, 0.847, 0.251], // 金
            [0.306, 0.878, 0.784], // 钻石
        ]
        const c = cols[tier] || cols[1]
        const tl = terrainLight(worldClock.skyLight)
        return Qt.rgba(c[0] * tl.r, c[1] * tl.g, c[2] * tl.b, 1.0)
    }

    // t560 护甲腿摆角度（度）：MobModel 几何腿绕髋枢做 X 轴摆动，sw = kLegSwingAmp(0.5 rad) × sin(walkPhase)，
    //   且 MobModel::setWalkPhase 量化到 12 腿姿/周期（kStep=2π/12，round 对齐）才 rebuild —— 盔甲枢轴必须用
    //   **同一量化相位**算摆角，否则与几何腿摆错位（最多差半格腿姿）。返回 eulerRotation.x 度数；
    //   sign = +1 左腿 / -1 右腿（几何 addBoxRot 左腿 +sw、右腿 −sw；QtQuick3D eulerRotation.x 正 = 同几何正角）。
    function mobArmorLegSwingDeg(phase, sign) {
        const step = 2 * Math.PI / 12
        const q = Math.round(phase / step) * step
        return sign * 0.5 * Math.sin(q) * 57.2958
    }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }
    // t173/t179 箱子内容存储 VM（按方块世界坐标键控的 27 槽；ChestUI 读写 + onBlockBroken(Chest) 清孤儿）。
    //   纯 Game/ViewModel 层，不依赖 World/Renderer；物品栈语义同 Hotbar（id=0=空）。ChestUI 经 chestX/Y/Z
    //   寻址当前所开箱子；多只箱子各自独立 27 槽，跨 UI 开关持久。
    ChestStore { id: chestStore }
    // t177 二轮复盘 熔炉内容存储 VM（按方块世界坐标键控的 in/fuel/out 3 槽 + 冶炼进度；FurnaceUI 读写 +
    //   onBlockBroken(Furnace) 清孤儿 + 掉内容）。纯 Game/ViewModel 层，不依赖 World/Renderer；物品栈语义同
    //   Hotbar（id=0=空）。FurnaceUI 经 furnaceX/Y/Z 寻址当前所开熔炉；多只熔炉各自独立内容 + 进度，跨 UI 开关
    //   持久（修旧 bug：旧 FurnaceUI 把 in/fuel/out 存 QML 本地属性 → 全世界熔炉共享一个物品栏）。
    FurnaceStore { id: furnaceStore }
    // t542 发射器内容存储 VM（按方块世界坐标键控的 9 槽 3×3；DispenserUI 读写 + onBlockBroken(Dispenser) 清
    //   孤儿 + 掉内容）。纯 Game/ViewModel 层，不依赖 World/Renderer；物品栈语义同 Hotbar（id=0=空）。
    //   修旧 bug（t517 遗留）：DispenserUI 旧把 9 槽存 QML 本地数组 → 全世界发射器共享一个物品栏、打掉不掉。
    DispenserStore { id: dispenserStore }
    // progress 玩家进度系统 VM（统计 + 成就；跨世界持久化存 worldstore progress 表）。各事件源经 QML 桥接
    //   调埋点（onBlockMined/onCraft/onMobKilled 等）；成就解锁弹 toast（achievementUnlocked 信号）。
    PlayerProgress { id: progress }

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

    // t402 经验球管理器（Entities 层）：杀怪 / 冶炼产出经验球，玩家靠近磁吸飞来 → 吸收累积 XP。
    //   纯数据持有（pos + amount），呈现层（下方 xpOrbHost Repeater）只读；磁吸 / 拾取由
    //   PlayerController.tick 驱动（持 XpOrbManager*）。拾取经 xpPickedUp 语义信号 → 下方 Connections
    //   路由到 playerState.addXp（单向事件流，PLAN §2 分层：Entities 发语义事件、呈现层只消费）。
    XpOrbManager { id: xpOrbs }

    // t469 船实体管理器（Entities 层）：浮水 + 骑乘 + WASD 操控 + 冰上加速 + 撞坏掉落（机制等价 MC 1.0 boat）。
    //   纯数据持有（pos + 变体 + 朝向），呈现层（下方 boatHost Repeater）只读；浮水 / 骑乘操控由
    //   PlayerController.tick / step 驱动（持 BoatManager*）。撞坏掉船物品经 boatBroken 语义信号 → 下方
    //   Connections 路由到 itemEntities.spawnItem（单向事件流，PLAN §2 分层）。
    BoatManager { id: boats }

    // t565 矿车实体管理器（Entities 层）：轨上骑乘 + WASD 前后推 + 拐角自动转弯（机制等价 MC 1.0 minecart）。
    //   纯数据持有（pos + 行进向 + yaw + speed），呈现层（下方 cartHost Repeater）只读；轨上推进 / 骑乘操控
    //   由 PlayerController.step 骑乘分支驱动（持 MinecartManager*）。挖矿车掉矿车物品经 cartBroken 语义
    //   信号 → 下方 Connections 路由到 itemEntities.spawnItem（单向事件流，PLAN §2 分层；同 boats 模式）。
    MinecartManager { id: carts }

    // t508 挖船 → 掉完整船物品（语义事件路由，同 fallingBlockDropped→spawnItem 模式；PLAN §2 分层）。
    //   boatBroken 由 hitBoatFromRay（攻击 / 挖船）发 → boatType 决定掉哪种船物品（Oak → OakBoatId / Spruce →
    //   SpruceBoatId）。机制等价 MC 1.0 攻击船 → 船破坏掉完整船物品（区别撞坏掉散件，见 onBoatWrecked）。
    Connections {
        target: boats
        function onBoatBroken(x, y, z, boatType) {
            // t556「撞坏掉橡木板 / 放云杉船变橡木样」（用户报①）根因排查：分色 / 掉落按变体（Oak/Spruce）分流时
            //   用实例作用域枚举 `boats.Spruce` —— 与全工程「类型作用域枚举（EntityManager.MobCow / PlayerController.Sprint）」
            //   风格不一致、实测不可靠（解析为 undefined → `=== boats.Spruce` 恒 false → 一切走 Oak 分支）。改回类型作用域
            //   `BoatManager.Spruce`（QML_NAMED_ELEMENT 类型名；同全工程既有枚举引用模式），分色 / 掉落分流可靠生效。
            const itemId = (boatType === BoatManager.Spruce) ? 0x235 /*SpruceBoatId*/ : 0x234 /*OakBoatId*/
            itemEntities.spawnItem(x, y, z, itemId, 1)
        }
        // t535 船撞坏 → 掉木板 + 木棍（非完整船；机制等价 MC 1.0 船高速撞墙损坏 → 3 木板 + 2 木棍）。
        //   boatWrecked 由 breakRiddenBoat（骑乘期高速撞硬墙 / 撞岸边方块，outCrashed=true）发。
        //   木板按变体（Oak→Planks 6 / Spruce→SprucePlanks 86），木棍固定 0x200（StickId；同橡 / 云杉，无变体区分）。
        //   t556：BoatManager.Spruce 类型作用域枚举（同 onBoatBroken，实例作用域 boats.Spruce 不可靠 → 撞坏恒掉橡木板）。
        //   两次 spawnItem（木板 ×3、木棍 ×2）→ 各 1 次实体（合并由 ItemEntityManager 近邻合并处理，机制等价 MC
        //   掉落物合并）。同一条 Connections 内多 handler 共享 target（boats），勿拆。
        function onBoatWrecked(x, y, z, boatType) {
            const plankId = (boatType === BoatManager.Spruce) ? 86 /*SprucePlanks*/ : 6 /*Planks*/
            itemEntities.spawnItem(x, y, z, plankId, 3)   // 木板 ×3（变体对应）
            itemEntities.spawnItem(x, y, z, 0x200 /*StickId*/, 2)  // 木棍 ×2
        }
    }

    // t565 挖矿车 → 掉矿车物品（语义事件路由，同 onBoatBroken→spawnItem 模式；PLAN §2 分层）。
    //   cartBroken 由 hitCartFromRay（攻击 / 挖矿车）发 → 呈层据它 spawnItem 掉 MinecartId（可重放）。
    //   机制等价 MC 1.0 攻击矿车 → 矿车破坏掉完整矿车物品。
    Connections {
        target: carts
        function onCartBroken(x, y, z) {
            itemEntities.spawnItem(x, y, z, 0x23E /*MinecartId*/, 1)
        }
    }

    // t402 经验球拾取 → 玩家累积 XP（语义事件路由，同 fallDamageTaken→takeDamage 模式；
    //   PLAN §2 分层：Entities 发语义事件、呈现层只消费）。拾取音复用掉落物拾取声（playPickup）。
    Connections {
        target: xpOrbs
        function onXpPickedUp(amount) {
            playerState.addXp(amount)
            audio.playPickup()
        }
    }
    // pause-menu 成就解锁 toast（progress.achievementUnlocked 信号；机制等价 MC advancement toast）。
    //   PlayerProgress 在埋点判定成就首次解锁时 emit（id / name / desc），此处路由到右下角 toast 显
    //   「成就解锁：{name}」（仅 playing 态；菜单 / worldlist 不显）。3 秒后 infoToastTimer 清空。
    Connections {
        target: progress
        function onAchievementUnlocked(id, name, desc) {
            window.showInfoToast("成就解锁：" + name)
        }
    }

    // t220 落沙遇不完整方块失撑 → 变掉落物：EntityManager 发 fallingBlockDropped（坐标 = 不完整方块上方
    //   一格、id = 沙方块 id）→ 转发到 itemEntities.spawnItem 生成掉落实体（机制等价 MC「沙落火把上 → 沙
    //   碎成掉落物」）。单向事件流：EntityManager 不持有 ItemEntityManager（同 player.spawnItem 模式；
    //   PLAN §2 分层：Entities 层发语义事件，呈现层只消费）。
    Connections {
        target: entityManager
        function onFallingBlockDropped(x, y, z, blockId) { itemEntities.spawnItem(x, y, z, blockId, 1) }
        // rv-low-batch1 塌落雪层掉雪球（修「塌落雪层落半砖 / 落另一雪层溢出时层数丢失只掉 1 份」）：
        //   EntityManager FallingBlock(SnowLayer) 着地遇非完整立方支撑 / 叠层溢出时发本信号（itemId=0x23D
        //   雪球字面量，count=层数，每层 1 雪球同玩家铲挖语义）→ 转发 spawnItem 一次 emit count 件（1 实体携
        //   多雪球，拾取 addStack 一次入多件，同玩家挖雪层模式）。单向事件流（PLAN §2 分层：Entities 发语义
        //   事件、呈现层只消费，同 fallingBlockDropped 模式）。
        function onSnowLayerCollapseDropped(x, y, z, itemId, count) { itemEntities.spawnItem(x, y, z, itemId, count) }
        // t297 爆炸掉落（EntityManager detonateStalker 内 ~50% 概率/破坏块发）：转发到
        //   ItemEntityManager.spawnItem 生成掉落实体（机制等价 MC 爆炸把被毁方块弹成物品）。itemId 已是
        //   BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落）。同 fallingBlockDropped 模式：单向事件流
        //   （PLAN §2 分层：Entities 层发语义事件、呈现层只消费）。掉落实体上限 kCap=200 自管防溢出。
        function onExplosionDroppedItem(x, y, z, itemId) {
            // t354 批量收口爆炸掉落（修 t320 复发根因）：detonateStalker 对球内每破坏块发一次本信号 → 旧路径
            //   逐个 spawnItem，每次 emit entitiesChanged → N 次 Repeater model 变更 + N 轮全体 delegate 重算 =
            //   O(N²) 绑定风暴 + N 次重 3D delegate 即时实例化 → 一帧数十 ms（FPS 崩 + 落地前续卡）。t320 只批了
            //   World 层 worldChanged，漏了本 Item 层 entitiesChanged。批态集中在 C++（m_batchDepth）：仅首发掉落
            //   开批（batchActive 守卫 → beginBatch 恰一次），后续掉落 spawnItem 经 notifyChanged 只标 dirty 不 emit；
            //   批尾由下方 onExplosion 收口（detonateStalker 末尾恒发 explosion，与掉落同栈同步 → 必在所有掉落后）。
            if (!itemEntities.batchActive()) itemEntities.beginBatch()
            itemEntities.spawnItem(x, y, z, itemId, 1)
        }
        // t242 mob 死亡掉落（spec「血 0→死亡掉落物：猪:生猪排 / 牛:皮革+生牛肉 / 羊:羊毛」）：damageEntity
        //   扣血到 ≤0 时 EntityManager 发 mobDied(x,y,z,mobType,burned,wasBaby) → 据子类 id 转发到 ItemEntityManager.spawnItem
        //   生成对应掉落实体（机制等价 MC 1.0 被动生物掉落；数量取 MC 1.0 量级：猪 1-2 生猪排 / 牛 1 皮革
        //   + 1-2 生牛肉 / 羊 1 羊毛；MobTest 不掉落）。同 fallingBlockDropped 模式：单向事件流（PLAN §2 分层：
        //   Entities 层发语义事件、呈现层只消费）。坐标 = mob 死亡格 floor(pos)，与 spawnItem 整数格约定一致。
        //   t299 敌对掉落（spec「敌对掉落物：骸骨→骨头 / 蹒跚者→腐肉 / 蜘蛛→线」）：骸骨 1-2 骨头 / 蹒跚者 1-2
        //   腐肉 / 蜘蛛 1-2 线；MobStalker（爆炸型）无常规掉落（爆炸破坏块掉落归 t297 explosionDroppedItem）。
        //   ⚠️ QML 无法直接 import RecipeRegistry（C++ 静态类），故用字面量 id（同 MaterialIcon.qml 约定）：
        //     0x20B=生猪排 / 0x20C=生牛肉 / 0x20D=皮革 / 0x20E=羊毛（RecipeRegistry::RawPorkchopId 等）。
        //     0x217=骨头 / 0x218=腐肉 / 0x219=线（RecipeRegistry::BoneId / RottenFleshId / StringId，t299）。
        //     0x228=羽毛 / 0x229=生鸡肉 / 0x22A=熟鸡肉（RecipeRegistry::FeatherId 等，t398 鸡掉落）。
        //     id 改动须同步 src/Game/recipe.h（单一权威）。
        function onMobDied(x, y, z, mobType, burned, wasBaby) {
            progress.onMobKilled(mobType)  // progress 统计击杀 + 成就「怪物猎人」（敌对 mob）
            // t479 幼崽死亡不掉落（机制等价 MC 1.0 幼崽不掉落）：幼崽（baby）死亡 → 不掉战利品 + 不掉 XP。
            //   wasBaby = EntityManager 致死瞬间快照（deathBaby）—— 0.5s 死亡动画窗口内 growTimer 可能到 0 长大，
            //   快照保「致死时是幼崽」语义（同 deathBurned 快照模式）。成体（wasBaby=false）走既有掉落流程。
            if (wasBaby) return
            // t402 杀怪产经验球（spec「killing mobs spawns XP orbs」；机制等价 MC 1.0 杀怪掉经验）。
            //   t443 放宽到所有 mob（spec「杀被动 mob 也掉 XP」）：敌对 mob 给较多（5 XP），被动 mob 给少量
            //   （1-3 XP 随机，量级远小于敌对，鼓励杀怪经济）。MobTest（调试）不在表 → 不掉。XP 数值为本工程
            //   小世界量身调（非 MC 精确复刻，PLAN §4 机制对标非数值 1:1）。
            const xpForMob = {}
            xpForMob[EntityManager.MobShambler] = 5   // 蹒跚者（僵尸）：5 XP
            xpForMob[EntityManager.MobBones]    = 5   // 骸骨（骷髅）：5 XP
            xpForMob[EntityManager.MobStalker]  = 5   // 潜行者（苦力怕）：5 XP（自爆型，同敌对量级）
            xpForMob[EntityManager.MobSpider]   = 5   // 蜘蛛：5 XP
            // t443 被动 mob 也掉少量 XP（spec「杀被动 mob（牛/羊/猪/鸡）也掉 XP」）：1-3 XP 随机。
            //   牛/羊/猪/鸡/鱿鱼均掉（被动经济生物）；MobTest 不在表 → 不掉。每次死亡独立掷骰（mob 死亡非
            //   worldgen，无确定性约束，同 onMobDied 既有 Math.random 稀有掉落模式）。
            xpForMob[EntityManager.MobPig]     = 1 + Math.floor(Math.random() * 3) // 猪：1-3 XP
            xpForMob[EntityManager.MobCow]     = 1 + Math.floor(Math.random() * 3) // 牛：1-3 XP
            xpForMob[EntityManager.MobSheep]   = 1 + Math.floor(Math.random() * 3) // 羊：1-3 XP
            xpForMob[EntityManager.MobChicken] = 1 + Math.floor(Math.random() * 3) // 鸡：1-3 XP
            xpForMob[EntityManager.MobSquid]   = 1 + Math.floor(Math.random() * 3) // 鱿鱼：1-3 XP
            xpForMob[EntityManager.MobWolf]    = 1 + Math.floor(Math.random() * 3) // t480 狼：1-3 XP（被动经济生物；无常规掉落）
            xpForMob[EntityManager.MobOcelot]  = 1 + Math.floor(Math.random() * 3) // t481 豹猫/猫：1-3 XP（被动经济生物；无常规掉落）
            // t482/t483 防御造物 XP：造物为玩家搭建的防御单位，杀之给少量 XP（机制等价 MC 造物无明确 XP 但本工程
            //   统一杀 mob 给 XP）。铁傀儡 5 XP（重型造物，同敌对量级）；雪傀儡 1-3 XP（轻型造物）。
            xpForMob[EntityManager.MobSnowGolem] = 1 + Math.floor(Math.random() * 3) // t482 雪傀儡：1-3 XP
            xpForMob[EntityManager.MobIronGolem] = 5 // t483 铁傀儡：5 XP（重型防御造物）
            xpForMob[EntityManager.MobSilverfish] = 5 // t487 银鱼：5 XP（敌对近战小虫，同敌对量级；无常规掉落）
            const xpAmt = xpForMob[mobType]
            if (xpAmt && xpAmt > 0) xpOrbs.spawnOrb(x, y, z, xpAmt)
            // t344 burned = mob 燃烧态（fireTimer>0）致死 → 被动动物的「生肉掉落」替换为熟肉（机制等价 MC 1.0
            //   着火死亡掉熟肉）：猪→熟猪排 / 牛→熟牛肉（皮革非肉、不变）/ 羊→熟羊肉（替代羊毛）。熟肉 id：
            //   0x221 熟猪排 / 0x222 熟牛肉 / 0x223 熟羊肉（RecipeRegistry::CookedPorkchopId 等；⚠️ QML 用字面量同上约定）。
            if (mobType === EntityManager.MobPig) {
                // t473 spec：猪也掉皮革（机制等价 MC 1.0 leather 来源；spec「leather from cow+pig」，
                //   皮革非肉 → 燃烧不变，同 MobCow 皮革掉落约定）。0x20D=RecipeRegistry::LeatherId。
                itemEntities.spawnItem(x, y, z, 0x20D, 1) // 皮革 ×1（t473 扩到猪；非肉，燃烧不变）
                const meat = burned ? 0x221 : 0x20B      // 熟猪排 / 生猪排 ×1-2
                itemEntities.spawnItem(x, y, z, meat, 1)
                itemEntities.spawnItem(x, y, z, meat, 1)
            } else if (mobType === EntityManager.MobCow) {
                itemEntities.spawnItem(x, y, z, 0x20D, 1) // 皮革 ×1（非肉，燃烧不变）
                const meat = burned ? 0x222 : 0x20C       // 熟牛肉 / 生牛肉 ×1-2
                itemEntities.spawnItem(x, y, z, meat, 1)
                itemEntities.spawnItem(x, y, z, meat, 1)
            } else if (mobType === EntityManager.MobSheep) {
                // 燃烧致死 → 熟羊肉（替代羊毛；机制等价 MC cooked mutton）；否则羊毛 ×1。
                itemEntities.spawnItem(x, y, z, burned ? 0x223 : 0x20E, 1)
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
                // t407 稀有掉落（机制等价 MC 1.0 僵尸罕见掉落）：胡萝卜 / 马铃薯各 2.5% 独立概率（MC 1.0 zombie
                //   rareDropChance = 2.5%，iron/carrot/potato 三者各 2.5% 独立判定，本工程实现 carrot/potato 两种）。
                //   稀有掉落是「杀怪偶尔掉作物」的经济入口（玩家由此获得胡萝卜 / 马铃薯 → 种耕地长作物）。
                //   0x22F = RecipeRegistry::CarrotId / 0x230 = RecipeRegistry::PotatoId（⚠️ QML 用字面量同上注释约定）。
                if (Math.random() < 0.025) itemEntities.spawnItem(x, y, z, 0x22F, 1)  // 胡萝卜 ~2.5%
                if (Math.random() < 0.025) itemEntities.spawnItem(x, y, z, 0x230, 1)  // 马铃薯 ~2.5%
            } else if (mobType === EntityManager.MobSpider) {
                // t299 敌对掉落：蜘蛛 → 线 ×1-2（机制等价 MC 1.0 蜘蛛掉线；弓 / 钓竿原料，t304 弓配方用）。
                itemEntities.spawnItem(x, y, z, 0x219, 1)   // 线 ×1-2
                itemEntities.spawnItem(x, y, z, 0x219, 1)
            } else if (mobType === EntityManager.MobStalker) {
                // t485 潜行者（苦力怕）掉落：火药 ×1-2（机制等价 MC 1.0 苦力怕掉火药 gunpowder）。
                //   0x239 = RecipeRegistry::GunpowderId（材料段火药；⚠️ QML 不 import C++ 静态类故用字面量，同 onMobDied
                //   既有约定）。火药是 TNT 合成原料（5 火药 + 4 沙 → 1 TNT，t485 沙漠神殿 TNT 陷阱方块），玩家由杀潜行者
                //   获得 → 合成 TNT → 创造之外的生存获取路径。爆炸型 mob 常规掉落归本分支（区别于 t297
                //   explosionDroppedItem 是爆炸破坏方块的掉落，二者独立）。
                itemEntities.spawnItem(x, y, z, 0x239, 1)   // 火药 ×1-2
                itemEntities.spawnItem(x, y, z, 0x239, 1)
            } else if (mobType === EntityManager.MobChicken) {
                // t398 鸡掉落：羽毛 ×1-2 + 生鸡肉 ×1（机制等价 MC 1.0 鸡掉羽毛 + 生鸡肉）。
                //   burned=true（着火致死）→ 生鸡肉替换为熟鸡肉（机制等价 MC 1.0 着火死亡掉熟肉，同猪/牛/羊）。
                //   0x228=羽毛 / 0x229=生鸡肉 / 0x22A=熟鸡肉（RecipeRegistry::FeatherId / RawChickenId / CookedChickenId）。
                itemEntities.spawnItem(x, y, z, 0x228, 1)   // 羽毛 ×1-2（非肉，燃烧不变）
                itemEntities.spawnItem(x, y, z, 0x228, 1)
                itemEntities.spawnItem(x, y, z, burned ? 0x22A : 0x229, 1)  // 熟鸡肉 / 生鸡肉 ×1
            } else if (mobType === EntityManager.MobSquid) {
                // t399 鱿鱼掉落：墨囊 ×1-3（机制等价 MC 1.0 鱿鱼掉墨囊 ink sac）。墨囊非食物 / 非燃料
                //   （§9 简化预留，未来染料 / 书与笔原料），着火不替换（无熟变体）。0x22D=RecipeRegistry::InkSacId。
                itemEntities.spawnItem(x, y, z, 0x22D, 1)             // 墨囊 ×1（恒掉）
                if (Math.random() < 0.66) itemEntities.spawnItem(x, y, z, 0x22D, 1)  // ~66% ×2
                if (Math.random() < 0.33) itemEntities.spawnItem(x, y, z, 0x22D, 1)  // ~33% ×3（独立 → 总量 1-3）
            } else if (mobType === EntityManager.MobIronGolem) {
                // t483 铁傀儡掉落：铁锭 ×3-5 + 罂粟（红花）×0-1（机制等价 MC 1.0 铁傀儡掉铁锭 + 罂粟 poppy）。
                //   0x203=RecipeRegistry::IronIngotId（材料段铁锭）/ FlowerRed=49（红花方块，机制等价 MC 罂粟 poppy，
                //   §9 区隔：红花的 MC 等价是罂粟，方块 id 即物品 id）。⚠️ QML 不 import C++ 静态类故用字面量，
                //   同 onMobDied 既有约定。铁锭总量 3-5（恒掉 3 + ~66%/33% 独立加成，机制等价 MC 铁傀儡 3-5 铁锭）。
                itemEntities.spawnItem(x, y, z, 0x203, 1)             // 铁锭 ×1（恒掉）
                itemEntities.spawnItem(x, y, z, 0x203, 1)
                itemEntities.spawnItem(x, y, z, 0x203, 1)
                if (Math.random() < 0.66) itemEntities.spawnItem(x, y, z, 0x203, 1)  // ~66% ×4
                if (Math.random() < 0.33) itemEntities.spawnItem(x, y, z, 0x203, 1)  // ~33% ×5（独立 → 总量 3-5）
                if (Math.random() < 0.5) itemEntities.spawnItem(x, y, z, 49, 1)      // 红花（罂粟）~50% ×1
            }
            // t510 雪傀儡（MobSnowGolem）死亡掉落雪球 0-15 个（spec「死掉雪球」；机制等价 MC 1.0 雪傀儡死亡
            //   掉落 0-15 雪球）。雪球 id=0x23D（RecipeRegistry::SnowballId；⚠️ QML 不 import C++ 静态类故用字面量，
            //   同 onMobDied 既有约定）。机制等价 MC 1.0：每只雪傀儡死亡独立掷 0-15 雪球（Math.floor(random*16)，
            //   0..15 等概率）→ 每件独立 spawnItem 实体（同被动多件掉落模式）。t482 旧版「雪傀儡死亡不掉落」改
            //   为本分支（spec t510 新增需求「死亡掉雪球」）。融化（沙漠/水/雨）慢扣血致死同样走本分支掉雪球
            //   （机制等价 MC 雪傀儡无论死因都掉雪球）。
            else if (mobType === EntityManager.MobSnowGolem) {
                const snowballCount = Math.floor(Math.random() * 16) // 0-15 雪球（机制等价 MC 雪傀儡 0-15 drops）
                for (let i = 0; i < snowballCount; ++i)
                    itemEntities.spawnItem(x, y, z, 0x23D, 1) // 雪球 ×1（每件独立实体，同被动多件掉落）
            }
            // MobTest（通用测试生物）不掉落 —— 调试生物无游戏内常规产出。Stalker 爆炸破坏方块的掉落由
            //   detonateStalker 的 explosionDroppedItem 单独发（t297）；MobStalker 常规击杀掉火药归本 onMobDied 上方分支（t485）。
        }
        // t300 剪羊毛掉落（spec「剪刀右键羊 → 羊变裸 + 掉羊毛物品」）：EntityManager shearSheep 内发
        //   sheepSheared(x,y,z)（坐标 = 羊当前格 floor(pos)，与 spawnItem 整数格约定一致）→ 转发到
        //   ItemEntityManager.spawnItem 生成羊毛物品掉落实体（机制等价 MC 1.0 剪羊毛掉落羊毛；杀羊掉落羊毛
        //   归 onMobDied 的 MobSheep 分支，二者独立 —— 剪羊毛不杀羊、杀羊前已剪则死时不再多掉）。
        //   0x20E = RecipeRegistry::WoolId（材料段羊毛物品；⚠️ QML 不 import C++ 静态类故字面量，同 onMobDied 约定）。
        //   单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只消费，同 fallingBlockDropped / mobDied 模式）。
        function onSheepSheared(x, y, z) { itemEntities.spawnItem(x, y, z, 0x20E, 1) }
        // t510 雪傀儡剪南瓜头掉落（spec「剪刀右键雪傀儡 → 南瓜掉落 + 雪傀儡变无头 derpy 形态」）：EntityManager
        //   shearSnowGolem 内发 snowGolemSheared(x,y,z)（坐标 = golem 当前格 floor(pos)，与 spawnItem 整数格约定一致）
        //   → 转发到 ItemEntityManager.spawnItem 生成南瓜方块掉落实体（机制等价 MC 1.0 剪刀剪雪傀儡南瓜头 → 南瓜掉落）。
        //   100 = BlockRegistry::Pumpkin（南瓜方块；⚠️ QML 不 import C++ 静态类故字面量，同 onMobDied 约定；方块 id 即
        //   物品 id，可放置回）。单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只消费，同 sheepSheared 模式）。
        function onSnowGolemSheared(x, y, z) { itemEntities.spawnItem(x, y, z, 100, 1) }
        // t398 鸡下蛋（spec「periodically lays an EGG item」）：EntityManager tick 内 MobChicken eggTimer 周期到 →
        //   发 chickenLaidEgg(x,y,z)（坐标 = 鸡当前格 floor(pos)，与 spawnItem 整数格约定一致）→ 转发到
        //   ItemEntityManager.spawnItem 生成蛋物品掉落实体（机制等价 MC 1.0 鸡 5-10 分钟下一枚蛋）。
        //   0x22B = RecipeRegistry::EggId（材料段蛋物品；⚠️ QML 不 import C++ 静态类故字面量，同 onMobDied 约定）。
        //   单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只消费，同 sheepSheared / mobDied 模式）。
        function onChickenLaidEgg(x, y, z) { itemEntities.spawnItem(x, y, z, 0x22B, 1) }
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
                else if (mobType === EntityManager.MobTnt) cause = PlayerState.Tnt   // t494：TNT 爆炸死因（独立于潜行者自爆）
                // t345 护甲减伤 + t476 保护族附魔减伤（mob 近战 / 箭 / 爆炸命中也走护甲值 + 附魔 EPF 减伤 + 耐久损耗）。
                //   护甲值每点 4%（cap 0.80）+ 附魔 EPF 每点 4%（cap 0.80），合计 cap 0.85；至少 1 点穿透。
                var finalAmt = amount
                const totalArmor = hotbarVM.totalArmorPoints
                const epf = hotbarVM.armorProtectionFactor(cause)
                if (amount > 0 && (totalArmor > 0 || epf > 0)) {
                    const ratio = Math.min(0.85, Math.min(0.80, totalArmor * 0.04) + Math.min(0.80, epf * 0.04))
                    finalAmt = Math.max(1, Math.round(amount * (1 - ratio)))
                    hotbarVM.damageArmor()
                }
                playerState.takeDamage(finalAmt, cause)
            }
            player.wakeUp()  // t388 受击即醒（mob 近战 / 箭 / 爆炸中断睡觉 fade；非 Survival 亦醒，防御）
        }
        // t284 Stalker 爆炸（EntityManager detonateStalker 发）：爆炸的单一音/视反馈入口 —— 播爆炸音
        //   （playExplosion）+ 白色迸发粒子（burstExplosion）。方块破坏走 setWaterSilent 不发 blockBroken
        //   → 免球形内每块破块粒子 spam，故本信号是爆炸音/视的唯一驱动（同 fallDamageTaken→takeDamage 模式；
        //   PLAN §2 分层：Entities 层发语义事件、呈现/音频层只消费）。
        function onExplosion(x, y, z) {
            // t354 收口本发爆炸的批量掉落（depth 归 0 → 1 次 emit 补齐所有新 delegate；水中爆炸无掉落 →
            //   未 beginBatch → endBatch 守卫 no-op）。先于音 / 视反馈，保证 delegate 当帧就位。
            if (itemEntities.batchActive()) itemEntities.endBatch()
            audio.playExplosion()
            if (particleLoader.item) {
                particleLoader.item.burstExplosion(x, y, z)
                particleLoader.item.burstExplosionSmoke(x, y, z)   // t494：爆炸后灰烟上飘慢慢消散
            }
        }
        // t304 玩家箭命中 mob（spec「抛物+伤害 mobs」）：damageEntity 已扣血 + 红闪（delegate 绑 hurtFlashAt）+
        //   归零 mobDied；本信号驱动命中音（同近战 attackMob→onMobAttacked→playMobHurt 模式）。
        function onArrowHitMob(mobType) { audio.playMobHurt(mobType) }
        // t505 雪球撞方块破碎（spec「砸地面 → 破碎动画消失」）：EntityManager tick 内 Snowball 命中方块时发
        //   snowballBreak(x,y,z)（float 命中点世界坐标）→ 转发到 BlockParticles.burstSnowball 迸发冷白雪沫
        //   （机制对标 MC 1.0 雪球撞方块碎裂）。单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只消费，
        //   同 blockBroken→burstBreak 模式）。
        function onSnowballBreak(x, y, z) {
            if (particleLoader.item) particleLoader.item.burstSnowball(x, y, z)
        }
        // t583 鸡蛋命中碎裂（机制等价 MC 1.0 鸡蛋砸任何东西都碎裂）：EntityManager tick 内 Egg 命中（方块 /
        //   mob）移除时发 eggBreak(x,y,z)（float 命中点世界坐标）→ 转发到 BlockParticles.burstEgg 迸发奶白
        //   蛋壳碎屑（机制对标 MC 1.0 鸡蛋碎裂）。单向事件流（PLAN §2 分层：Entities 发语义事件、呈现层只
        //   消费，同 snowballBreak 模式）。孵化小鸡由 Entities 层内部完成（spawnMobCore → entitiesChanged）。
        function onEggBreak(x, y, z) {
            if (particleLoader.item) particleLoader.item.burstEgg(x, y, z)
        }
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
        xpOrbManager: xpOrbs
        boatManager: boats
        minecartManager: carts
        // t579：注入发射器内容存储（踩压力板触发发射器取内容物发射 / 扣库存；同 peer VM 注入模式）。
        dispenserStore: dispenserStore
        selectedBlock: hotbarVM.selectedBlockId
        selectedItem: hotbarVM.selectedItemId
    }

    // 玩家掉落伤害 → 生命（t22）：PlayerController 在 Survival 着地结算时发 fallDamageTaken(hp)，
    // 呈现层经此 Connections 路由到 PlayerState.takeDamage（与破/放信号→粒子同模式：Game 层发
    // 语义事件，呈现层只消费）。PlayerController 不持有 PlayerState，保持单向事件流、分层干净。
    Connections {
        target: player
        // t345 护甲减伤 + t476 保护族附魔减伤：路由到 takeDamage 前先按 totalArmor（每点 4%，cap 0.80）+
        //   附魔 EPF（每点 4%，cap 0.80）合计算减伤比例（cap 0.85）。至少 1 点穿透（护甲不彻底免伤）。
        //   护甲受击 -1 耐久（damageArmor；归零破损消失）。spec「armor reduces incoming damage by its armor value」
        //   +「DURABILITY degrades on hits」。覆盖所有走 fallDamageTaken 的伤害源（坠落 / 窒息 / 溺水 / 饥饿 / 燃烧）；
        //   mobAttackedPlayer 同此减伤。t476 EPF 据 cause 取匹配保护族（Fall→摔落保护 / Fire→火焰保护 / 余→通用保护）。
        function onFallDamageTaken(hp, cause) {
            let finalDmg = hp
            const totalArmor = hotbarVM.totalArmorPoints
            const epf = hotbarVM.armorProtectionFactor(cause)
            if (hp > 0 && (totalArmor > 0 || epf > 0)) {
                const ratio = Math.min(0.85, Math.min(0.80, totalArmor * 0.04) + Math.min(0.80, epf * 0.04))
                finalDmg = Math.max(1, Math.round(hp * (1 - ratio)))
                hotbarVM.damageArmor()
            }
            playerState.takeDamage(finalDmg, cause) // t311 透传致死来源（Fall/Suffocation/Drowning/Starvation/Fire）
            player.wakeUp()  // t388 受击即醒（坠落/窒息/溺水/饥饿/燃烧中断睡觉 fade）
        }
        // t238 饥饿回血 → PlayerState.heal（饱腹态每 4s 回 1HP；同 fallDamageTaken→takeDamage 反向配对）。
        function onHealed(hp) { playerState.heal(hp) }
        // t202 气泡值更新 → PlayerState.air（Physics 层算时序、Game 层持显值、呈现层路由；同 fallDamageTaken→
        //   takeDamage 模式）。溺水扣血复用 onFallDamageTaken（→ takeDamage → damaged 红闪 / 视角晃）。
        function onAirUpdated(air) { playerState.setAir(air) }
        // t238 饥饿值更新 → PlayerState.setHunger（Physics 层 m_hunger 推进 / 食用恢复时发；同 airUpdated→
        //   setAir 模式）。饥饿归零扣血复用 onFallDamageTaken（→ takeDamage → damaged 红闪 / 视角晃）。
        function onHungerUpdated(hunger) { playerState.setHunger(hunger) }
        // t388 睡觉被拒（白天 / 附近有怪物）→ 系统播报中文文案（同死亡播报 appendChatMessage 模式）。
        function onSleepRefused(reason) { window.appendChatMessage("", reason, true) }
        // t35：生存破可掉落方块（drop=true）→ player 发 spawnItem → 转发到 manager 生成实体。
        // 创造 / 不可采掘时 player 不发本信号（无实体产出）。ViewModel 不持有 PlayerController，
        // 经 Connections 解耦（同 fallDamageTaken→PlayerState 模式；PLAN §2 分层）。
        // t64：spawnItem 信号带 count 参数（整栈丢弃为 1 实体；破块掉落走 BlockDef.dropCount）。
        function onSpawnItem(x, y, z, id, count) { itemEntities.spawnItem(x, y, z, id, count) }
        // t401 钓获物（拉起咬钩 → player 发 fishCaught，携获物 id + 数量 + 浮标整数格）→ 转发到 manager 生成
        //   掉落实体（同 spawnItem / mobDied 模式；单向事件流：Game 层发语义事件、呈现层只消费）。
        function onFishCaught(itemId, count, x, y, z) { itemEntities.spawnItem(x, y, z, itemId, count) }
        // t61：挖掘过程粒子 —— 生存累积挖掘时每跨一阶，player 发 miningParticle（被挖方块坐标+id），
        // 转发到 BlockParticles.burstMine（复用破块碎屑 emitter / 色逻辑 / 重力，少量迸发，进度反馈）。
        // 破块完成时的 +30% 大迸发仍由 onBlockBroken → burstBreak 驱动（burstBreak 已在此任务内 +30%）。
        // t165：挖掘音改由下方 onMiningSound 统一驱动（含基岩等不可挖方块的 hold-mine 音反馈，
        // spec「保持 mining 态挥臂+音」）；本处仅迸碎屑（碎屑仍只对可挖方块，基岩不破无碎屑）。
        function onMiningParticle(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstMine(x, y, z, id)
        }
        // t267 进食屑粒（持面包按住右键累积进食时每跨一节拍 player 发 eatingParticle，携嘴部世界坐标）：
        //   转发到 BlockParticles.burstEat（屑粒从嘴部迸发）。机制等价 MC 进食屑粒。
        //   x/y/z 为 float 世界坐标（玩家眼位），非方块格 → burstEat 内不加 +0.5（区别 burstBreak/burstMine）。
        //   t513：携 itemId（正在吃的食物）→ burstEat 据此按食物取屑粒色（甜浆果=暗红 / 胡萝卜=橙 / 土豆=土黄 /
        //   面包=金黄 / 蘑菇汤=棕），替换旧固定面包色（spec「吃甜浆果吐橙色方块」→ 各食物本色屑粒）。
        function onEatingParticle(x, y, z, itemId) {
            if (particleLoader.item) particleLoader.item.burstEat(x, y, z, itemId)
        }
        // t165：挖掘击打音（每节拍一响）—— player 发 miningSound（被挖方块 id），**含不可挖基岩**的
        //   hold-mine 音反馈（spec「生存基岩可持续挖 ... 保持 mining 态挥臂+音」；机制等价 MC 镐撞基岩响）。
        //   id 给 AudioManager 按材质组选 mining clip。音与碎屑解耦：音对所有被挖方块，碎屑仅可挖。
        function onMiningSound(id) { audio.playMining(id) }
        // t118：拾取掉落实体 → player 发 itemPickedUp(id, count) → 拾取音（pickup clip，不分材质）。
        // 信号在 pickupScan 实际入栈时（全 / 部分）才发；全满装不下不发（无伪触发）。机制等价 MC
        // 「拾起物品啵一声」。t120：同时启动 handPopAnim（手 Y 弹跳，音 + 手弹双反馈）。
        function onItemPickedUp(id, count) { audio.playPickup(); handPopAnim.start(); progress.onItemPicked(id) }
        // progress 统计：玩家挖掘方块 +1（playerMined 信号；创造瞬破 / 生存累积完成均发）。
        function onPlayerMined(x, y, z, blockId, drop) { progress.onBlockMined() }
        // progress 统计：玩家放置方块 +1（blockPlaced 信号；placeBlock 末尾 emit）。
        function onBlockPlaced() { progress.onBlockPlaced() }
        // t50：右键工作台 → player 发 craftingTableOpened → 开 3×3 合成面板（释放指针 / 关包互斥）。
        function onCraftingTableOpened() { window.openCraftingTable() }
        // t87/t494：右键熔炉 → player 发 furnaceOpened(x,y,z) → 开 FurnaceUI 冶炼面板（释放指针 / 关包互斥）。
        //   坐标存 window.furnaceX/Y/Z，供 FurnaceUI 经 setFurnaceLit 翻燃烧 bit 切 front/front_on 贴图。
        function onFurnaceOpened(x, y, z) { window.openFurnace(x, y, z) }
        // t173/t179：右键箱子 → player 发 chestOpened(x,y,z) → 开 ChestUI（释放指针 / 关包互斥）。
        //   坐标供 ChestStore 寻址该箱子的 27 槽。
        function onChestOpened(x, y, z) { window.openChest(x, y, z) }
        // t474：右键附魔台 → player 发 enchantingTableOpened(x,y,z) → 开 EnchantingTableUI（释放指针 / 关包互斥）。
        //   坐标供 UI 查 theWorld.countBookshelvesAround 算书架加成 → 提升可选附魔等级上限。
        function onEnchantingTableOpened(x, y, z) { window.openEnchantingTable(x, y, z) }
        // t477：右键铁砧 → player 发 anvilOpened(x,y,z) → 开 AnvilUI（释放指针 / 关包互斥）。
        //   坐标供 UI 调 player.damageAnvil 推进铁砧损坏阶段（每次成功操作 ~1/3 概率损坏 +1）。
        function onAnvilOpened(x, y, z) { window.openAnvil(x, y, z) }
        // t517：右键发射器 → player 发 dispenserOpened(x,y,z) → 开 DispenserUI（释放指针 / 关包互斥）。
        //   坐标供后续 DispenserStore per-block 寻址（t517 本轮坐标暂留）。
        function onDispenserOpened(x, y, z) { window.openDispenser(x, y, z) }
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
            // t385：天气暗度（theWorld.weatherDarkness）再向暗阴灰蓝 lerp（雷暴 / 雨天天空变暗）。
            clearColor: weatherSkyColor()
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
                let eye = player.position
                // t457/t496 二轮复盘 睡觉躺下（第一人称视角降低）：sleepLie 0→1 平滑降低相机 Y（从站立眼位 1.62 降到
                //   床垫顶 + 枕头高 ~0.62，机制等价 MC 躺床第一人称视点降到床高平视）。与 sleepFade 同步 ramp
                //   （Lying 阶段 0→1）。仅 sleeping 时 sleepLie 非 0（非睡觉恒 0 → 不影响常规视角）。三模式都加。
                //   t496 二轮复盘：旧降量 1.4 把眼位降到 0.22（近地 / 入床下实体方块）→ 用户「镜头落到底黑屏」。
                //   改降量 1.0 → 眼位 0.62（床垫顶 0.31 之上 + 枕头高，平视床面不穿地，机制等价 MC 床高平视躺姿）。
                if (player.sleepLie > 0.0)
                    eye = Qt.vector3d(eye.x, eye.y - player.sleepLie * 1.0, eye.z)
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
                // t457/t496 二轮复盘 睡觉躺下「转躺」：sleepLie 0→1 平滑上仰相机（+pitch 看 ~10° 上方，平躺略仰，
                //   机制等价 MC 躺床平视略仰）。与 position Y 降低同步 ramp（Lying 阶段）。
                //   t496 二轮复盘：旧仰角 20° 太陡（眼位已降到床高，再仰 20° → 视线偏向天花板 / 床头板正上方，
                //   配合降量过大显「黑屏」）。改 10° → 平视床尾方向，眼位 0.62 平视不被床头板 / 天花板遮满。
                const lieTilt = player.sleepLie * 10.0
                if (player.cameraMode === PlayerController.ThirdPersonFront)
                    return Qt.vector3d(-player.pitch + sp + lieTilt, player.yaw + 180 + sy, 0) // 回看正面：俯仰反向 + 偏航 +180
                return Qt.vector3d(player.pitch + sp + lieTilt, player.yaw + sy, 0)            // 第一人称 & 第三人称-后：朝前看
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
                    visible: player.selectedBlock !== 0 && player.selectedBlock !== 13 && !hotbarVM.isPartialBlock(player.selectedBlock) && !hotbarVM.isCrossBlock(player.selectedBlock) && !hotbarVM.isBed(player.selectedBlock)
                    geometry: BlockCube { blockId: player.selectedBlock }
                    position: Qt.vector3d(0.0 + window.heldBlockX, 0.02 + window.heldBlockY, -0.22 + window.heldBlockZ)    // t156 基线 + t166c ESC 滑条偏移（heldBlockX/Y/Z）
                    scale: Qt.vector3d(0.12, 0.12, 0.12)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
                        alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）/ cross 段（isCrossBlock）/ 床段（isBed，t496）已走下方 billboard 分支，本立方路径不再处理它们
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
                // t496 二轮复盘 床亦走本 billboard 分支：床 ShapeBed 在世界内是双格横置低异形（非整立方），手持走
                //   bed 图标（icon_bed_<color>.png / pack 染色 bed 图），非 BlockCube 满格被面色立方（用户复盘
                //   「第一人称手持拿的是方块立方体」修复）。partialIconTex 已含 bed（iconSourceForBlock 命中床返
                //   床图标），无需另建 Texture。
                Model {
                    visible: hotbarVM.isPartialBlock(player.selectedBlock) || hotbarVM.isCrossBlock(player.selectedBlock) || hotbarVM.isBed(player.selectedBlock)
                    geometry: BillboardQuad {}
                    position: Qt.vector3d(0.02 + window.heldBlockX, 0.04 + window.heldBlockY, -0.22 + window.heldBlockZ)
                    scale: Qt.vector3d(0.18, 0.18, 0.18)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaCutoff: 0.5
                        opacity: 0.99   // <1 强制走透明通道 → 贴图 alpha 被尊重（透明底不渲染）
                        baseColorMap: partialIconTex   // t440：cross 段（花/蘑菇/睡莲/树苗/枯木/草丛…）透明底 flat 图标同 partialIconTex（iconSourceForBlock 返回 icon_*.png）；t496 床段亦同
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
                    eulerRotation: Qt.vector3d(15, -20, 28)       // t369 修 Z 符号：正 Z roll 把几何头（+Y）摆向屏幕左（柄下右/头上左对角，类 MC 手持）；旧 -15 反把头摆向右、与「头上左」注释相悖（手本地 X 轴不受手 baseTilt 的 X 旋转影响 → Z 符号直接定头左右）
                    // 头部 tier 配色（柄恒木褐，头随 tier）：木褐 / 石灰 / 铁银白 / 钻石青绿 / 金黄 / 铜橙（同 2D ToolIcon 配色）
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金镐金黄
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜镐铜橙
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // 钻石镐青绿（t472）
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁镐银白
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
                // t332 锄（type=Hoe）手持木柄修：旧版整把单 baseColor（HoeGeometry pos-only 单色）→ 石 / 铁锄
                //   木柄也变灰 / 银（应恒木）。改 Node + UnitCube 组合复刻 hoe.cpp 3 盒，各盒独立材质
                //   （机制对齐 t266 镐手持「木柄恒木褐、头随 tier」；第三人称 / 掉落物仍用 HoeGeometry 单色）。
                Node {
                    id: heldHoeFp
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 2
                    position: Qt.vector3d(0.02, 0.10, -0.22)       // t369：y 对齐镐（0.04→0.10），握把贴手心、与镐一致
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, 28)        // t369：正 Z roll 把锄刃（+Y）摆向屏幕左（柄下右/头上左对角，同镐）
                    // 头部 tier 配色（柄恒木褐，头随 tier）：木褐 / 石灰 / 铁银白 / 钻石青绿（t589）/ 金黄 / 铜橙（同 2D ToolIcon 配色）
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金锄金黄
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜锄铜橙
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // t589 钻石锄青绿
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁锄银白
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石锄中灰
                                                                                                 : "#8a5a2e"                                                   // 木锄褐（默认 / tier 1）
                    // 木柄（竖直）：心 (0,-0.05,0)，半长 0.04×0.40×0.04（同 hoe.cpp 木柄 addBox）→ scale 2×半长
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.05, 0)
                        scale: Qt.vector3d(0.08, 0.80, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木柄恒木褐（与 tier 无关）
                    }
                    // 颈节（柄→刃连接）：心 (0,0.36,0.04)，半长 0.06×0.04×0.06
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.36, 0.04)
                        scale: Qt.vector3d(0.12, 0.08, 0.12)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldHoeFp.headColor }
                    }
                    // 锄刃（宽扁向前伸）：心 (0,0.34,0.18)，半长 0.28×0.03×0.12
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.34, 0.18)
                        scale: Qt.vector3d(0.56, 0.06, 0.24)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldHoeFp.headColor }
                    }
                }
                // t332 斧（type=Axe）手持木柄修：旧版整把单 baseColor（AxeGeometry pos-only 单色）→ 石 / 铁斧
                //   木柄也变灰 / 银（应恒木）。改 Node + UnitCube 组合复刻 axe.cpp 4 盒，各盒独立材质
                //   （机制对齐 t266 镐手持「木柄恒木褐、头随 tier」；第三人称 / 掉落物仍用 AxeGeometry 单色）。
                Node {
                    id: heldAxeFp
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 3
                    position: Qt.vector3d(0.02, 0.10, -0.22)       // t369：y 对齐镐（0.04→0.10），握把贴手心、与镐一致
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, 28)        // t369：正 Z roll 把斧刃（+Y）摆向屏幕左（柄下右/头上左对角，同镐）
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金斧金黄
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜斧铜橙
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // t589 钻石斧青绿
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁斧银白
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石斧中灰
                                                                                                 : "#8a5a2e"                                                   // 木斧褐（默认 / tier 1）
                    // 木柄（竖直）：心 (0,-0.05,0)，半长 0.04×0.40×0.04（同 axe.cpp 木柄 addBox）→ scale 2×半长
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.05, 0)
                        scale: Qt.vector3d(0.08, 0.80, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木柄恒木褐（与 tier 无关）
                    }
                    // 颈节（柄→刃连接）：心 (0.03,0.36,0)，半长 0.05×0.04×0.05
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.03, 0.36, 0)
                        scale: Qt.vector3d(0.10, 0.08, 0.10)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldAxeFp.headColor }
                    }
                    // 斧刃主块（单边厚刃）：心 (0.18,0.34,0)，半长 0.14×0.06×0.05
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.18, 0.34, 0)
                        scale: Qt.vector3d(0.28, 0.12, 0.10)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldAxeFp.headColor }
                    }
                    // 刃口（右下收窄）：心 (0.30,0.26,0)，半长 0.04×0.06×0.05
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.30, 0.26, 0)
                        scale: Qt.vector3d(0.08, 0.12, 0.10)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldAxeFp.headColor }
                    }
                }
                // t332 铲（type=Shovel）手持木柄修：旧版整把单 baseColor（ShovelGeometry pos-only 单色）→ 石 / 铁铲
                //   木柄也变灰 / 银（应恒木）。改 Node + UnitCube 组合复刻 shovel.cpp 3 盒，各盒独立材质
                //   （机制对齐 t266 镐手持「木柄恒木褐、头随 tier」；第三人称 / 掉落物仍用 ShovelGeometry 单色）。
                Node {
                    id: heldShovelFp
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 4
                    position: Qt.vector3d(0.02, 0.10, -0.22)       // t369：y 对齐镐（0.04→0.10），握把贴手心、与镐一致
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, 28)        // t369：正 Z roll 把铲斗（+Y）摆向屏幕左（柄下右/头上左对角，同镐）
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金铲金黄
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜铲铜橙
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // t589 钻石铲青绿
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁铲银白
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石铲中灰
                                                                                                 : "#8a5a2e"                                                   // 木铲褐（默认 / tier 1）
                    // 木柄（竖直）：心 (0,-0.05,0)，半长 0.04×0.40×0.04（同 shovel.cpp 木柄 addBox）→ scale 2×半长
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.05, 0)
                        scale: Qt.vector3d(0.08, 0.80, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木柄恒木褐（与 tier 无关）
                    }
                    // 颈节（柄→铲斗连接）：心 (0,0.36,0)，半长 0.05×0.04×0.05
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.36, 0)
                        scale: Qt.vector3d(0.10, 0.08, 0.10)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldShovelFp.headColor }
                    }
                    // 铲斗（方形扁斗）：心 (0,0.30,0)，半长 0.14×0.08×0.05
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.30, 0)
                        scale: Qt.vector3d(0.28, 0.16, 0.10)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldShovelFp.headColor }
                    }
                }
                // t332 剑（type=Sword）手持木柄修：旧版整把单 baseColor（SwordGeometry pos-only 单色）→ 石 / 铁剑
                //   的护手 / 剑柄 / 柄首也变灰 / 银（应恒木）。改 Node + UnitCube 组合复刻 sword.cpp 5 盒，各盒独立
                //   材质：刃 + 尖 tier 色、护手 + 柄 + 柄首木色（同 2D ToolIcon 剑策略：刃 tier、护手 / 柄 / 柄首木）。
                //   机制对齐 t266 镐手持；第三人称 / 掉落物仍用 SwordGeometry 单色。
                Node {
                    id: heldSwordFp
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 5
                    position: Qt.vector3d(0.02, 0.04, -0.22)     // t369：y 微抬（0.02→0.04），护手贴手心
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(20, -15, 15)       // t369：正 Z roll 把刃尖（+Y）摆向屏幕左（旧 -10 反摆向右）；剑身竖直略前倾、刃尖朝前上
                    readonly property color headColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金剑金黄
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜剑铜橙
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // t589 钻石剑青绿
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁剑银白
                                                                                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石剑中灰
                                                                                                 : "#8a5a2e"                                                   // 木剑褐（默认 / tier 1）
                    // 剑刃（纵向长刃，tier 金属色）：心 (0,0.10,0)，半长 0.03×0.34×0.025（同 sword.cpp 剑刃 addBox）→ scale 2×半长
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.10, 0)
                        scale: Qt.vector3d(0.06, 0.68, 0.05)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldSwordFp.headColor }
                    }
                    // 刃尖（顶端收窄，tier 金属色）：心 (0,0.42,0)，半长 0.02×0.04×0.02
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.42, 0)
                        scale: Qt.vector3d(0.04, 0.08, 0.04)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: heldSwordFp.headColor }
                    }
                    // 护手（横向短梁，木色）：心 (0,-0.26,0)，半长 0.10×0.02×0.03
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.26, 0)
                        scale: Qt.vector3d(0.20, 0.04, 0.06)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木色（与 tier 无关）
                    }
                    // 剑柄（下半短柄，木色）：心 (0,-0.34,0)，半长 0.025×0.06×0.025
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.34, 0)
                        scale: Qt.vector3d(0.05, 0.12, 0.05)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木色（与 tier 无关）
                    }
                    // 柄首（柄底圆头，木色）：心 (0,-0.42,0)，半长 0.035×0.03×0.035
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.42, 0)
                        scale: Qt.vector3d(0.07, 0.06, 0.07)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a5a2e" }   // 木色（与 tier 无关）
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
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金弓金黄
                                 : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜弓铜橙
                                 : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"
                                 : "#8a5a2e"
                    }
                    // t330 白弦 + t368 搭箭（nocked arrow）：弦与箭同处一 Node，随 drawAmount 沿 +X 后拉
                    //   （player.bowDrawProgress 0..1）→ 弦中点 (0.06,0,0) 与箭尾（nocks）恒同位，免脱弦。
                    //   箭仅拉弓时可见（player.bowDrawing）；箭身沿 -X（朝准星方向 = 朝目标）。机制等价 MC 1.0 拉弓搭箭。
                    //   t451 方向修正：BowGeometry 的 belly/握把在 -X（前=目标侧）、弓梢+弦在 +X（后=射手侧），
                    //     箭沿 -X 指向目标 → 故「向后拉（向射手）」= +X。旧版误写 -X（= 朝目标侧=往前推），观感
                    //     「拉弓时弦+箭往前走」；翻号为 +X 后，弓本体不动、弦+箭随蓄力向射手侧后移蓄力（机制正确）。
                    //     lighting:NoLighting（沿用具名路径）；无 pack pulling 贴图依赖（本弓为程序几何，非贴图精灵）。
                    Node {
                        position: Qt.vector3d(player.bowDrawing ? player.bowDrawProgress * 0.05 : 0.0, 0.0, 0.0)
                        // 白弦（蜘蛛丝白，独立于 tier）：继承父 position/scale/eulerRotation → 与弓身同位。
                        Model {
                            geometry: BowStringGeometry {}
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                baseColor: "#f5f5f5"
                            }
                        }
                        // 搭箭（t368）：BowArrowGeometry 局部原点=箭尾，摆到弦中点 (0.06,0,0) → 箭尾贴弦。
                        Model {
                            visible: player.bowDrawing
                            geometry: BowArrowGeometry {}
                            position: Qt.vector3d(0.06, 0.0, 0.0)
                            materials: PrincipledMaterial {
                                lighting: PrincipledMaterial.NoLighting
                                baseColor: "#9c8050"   // 中褐木色（独立于 tier；与深木弓身 #8a5a2e 分离、辨识）
                            }
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
                // t401 钓鱼竿（type=FishingRod）第一人称手持：钓竿无独立 3D 几何 → billboard ToolIcon 平图标
                //   （同剪刀路径：BillboardQuad + Canvas sourceItem + 抵消手 X 旋转 → billboard +Z 恒指回相机）。
                //   ToolIcon toolType===8 自绘钓竿（杆 + 线 + 浮标）；alphaCutoff:0.5 + opacity:0.99 沿用透明底 alpha-test。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem) && hotbarVM.toolType(player.selectedItem) === 8
                    geometry: BillboardQuad {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)
                    scale: Qt.vector3d(0.18, 0.18, 0.18)
                    eulerRotation: Qt.vector3d(-(viewModelHand.baseTilt + viewModelHand.swingAngle), 0, 0)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        alphaCutoff: 0.5
                        opacity: 0.99
                        baseColor: terrainLight(worldClock.skyLight)
                        baseColorMap: Texture {
                            flipV: false
                            sourceItem: ToolIcon {
                                toolType: 8
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

        // t389 可视月亮（机制等价 MC 1.0 月相；§9 自绘 moon_<phase>.png，零 MC 资产）：与太阳互补 ——
        //   月在「背太阳方向」(eye − sunDir·500)。sunDir.y<0（夜间 / 太阳在地平下）时月在地平上 → 显；
        //   sunDir.y>0（昼）隐藏（与上方太阳 visible:sunDir.y>0 互补，二者永不同时显）。月相由
        //   WorldClock.moonPhase(0..7) 选 moon_<phase>.png（满→盈凸→上弦→蛾眉→新月→残月→下弦→亏凸，8 天一轮回）。
        //   billboard 朝相机 + scale 80（与太阳同角尺寸 80/500）、距眼 500（在 SkyDome 600 之前 → 渲于星空之上）。
        //   **t570 正方形月亮**（用户复盘「月亮背景灰色 PNG 消不掉」→ 机制等价 MC 1.0 月亮本就是正方形贴图）：
        //   贴图改为满画布不透明正方形月（moon_<phase>.png 64×64 全 alpha=255，相位明暗画在方形边界内）→
        //   材质删 alphaCutoff（无透明像素可剔）→ 圆盘软边半透像素混夜空色差的灰背景一步根除。
        //   PLAN §2-H「非旋转方向光」仍成立：月是纯呈现层装饰盘，不参与光照。lit 红线：NoLighting（同太阳）。
        //   分层（PLAN §2）：纯呈现层，只读 worldClock.sunDir / moonPhase（Game 层 Q_PROPERTY），绝不反向写。
        Model {
            visible: worldClock.sunDir.y < 0.0   // 夜间（太阳在地平下）才显，与太阳互补
            geometry: BillboardQuad {}
            position: {
                const eye = player.position
                const s = worldClock.sunDir
                return Qt.vector3d(eye.x - s.x * 500, eye.y - s.y * 500, eye.z - s.z * 500)
            }
            eulerRotation: Qt.vector3d(cam.eulerRotation.x, cam.eulerRotation.y, 0)   // billboard 朝相机
            scale: Qt.vector3d(80.0, 80.0, 80.0)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffffff"
                // t570：正方形月亮全贴图不透明（暗部暗蓝灰画在方形内，机制等价 MC 月暗部不透明地面）→
                //   无 alphaCutoff（灰背景根除）。moonPhase 0..7 → 选对应月相贴图（moonPhaseChanged 刷新）。
                baseColorMap: Texture { source: "qrc:/textures/moon_" + worldClock.moonPhase + ".png" }
            }
        }

        // t389 星空天穹（机制等价 MC 1.0 夜空星点；§9 自绘 stars.png，零 MC 资产）：一张绕相机眼位的
        //   UV 球（SkyDome，scale 600 = 距眼 600 格）铺星空贴图，内表面朝相机（SkyDome 按「内表面 = 正面」
        //   生成，默认 backface 剔除下显内表面）。居中于眼位（position 绑 player.position）→ 相机恒在球心 →
        //   每条视线交球一次 → 恒作最远天幕（地形 / 云 / 日月皆在 600 之内 → 渲于其前可见；clipFar=1000 容纳）。
        //   opacity 随天光淡入（夜显星 / 昼隐）+ 雷暴雨天抑制（云遮星）。贴图透明底 → 缝透出 sky clearColor、
        //   星点处显白 / 蓝。缓慢自转 eulerRotation.y=dayPhase·360 → 星空随天周期绕极轴转一圈（天球周日视运动）。
        //   t570：星点再缩 ~45%（tools/build_stars.py 半径分布 0.22..0.45 / 亮星 0.5..0.85 → 绝大多数单像素
        //   细点）—— 用户报「星星比月亮大」；缩小后星点远小于（正方形）月盘，读作自然细星。
        //   lit 红线：NoLighting（同地形 / 太阳已验证可见路径）。分层（PLAN §2）：纯呈现层，只读 worldClock，绝不反向写。
        Model {
            geometry: SkyDome {}
            position: player.position
            eulerRotation.y: worldClock.dayPhase * 360.0   // 天球周日视运动（一圈 / 天周期）
            scale: Qt.vector3d(600.0, 600.0, 600.0)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffffff"
                opacity: starOpacity()                      // 夜间淡入 / 昼隐 / 雨天抑制（dayPhaseChanged 每 tick 刷）
                alphaCutoff: 0.02                            // 仅丢弃近乎全透像素（保星柔边 + 杜绝黑底回退，同云契约）
                baseColorMap: Texture {
                    source: "qrc:/textures/stars.png"
                    generateMipmaps: false
                }
            }
        }

        // t384 高空云盖（机制等价 MC 1.0 漂移云层；§9 自绘 cloud.png，零 MC 资产）：一张覆盖整片天空的
        //   大水平四边形（BillboardQuad 绕 X 转 90° 摊平成 XZ 平面、前向法线朝下 -Y → 从下方抬头可见），
        //   贴可无缝平铺的 cloud.png（scaleU/V=12 重复）→ 缝里透出 sky clearColor、云团处显白/灰。
        //   缓慢漂移：position.x = player.x + cloudDrift（cloudDrift 由 WorldClock.ticked 按 1.5 格/s 累积、
        //   每 50 格回绕；贴图可无缝平铺 → 回绕无接缝）→ 抬头见自然漂移的云盖。X/Z 跟随玩家 → 始终覆盖头顶。
        //   昼夜变色：baseColor = cloudColor(skyLight)（昼白 / 夜灰暗）。Y = 眼位 + 90（始终在头顶上方）。
        //   lit 红线：PrincipledMaterial 必须 NoLighting（同地形 / 太阳已验证可见路径；lessons-learned）。
        //   alpha：贴图存云密度（云 alpha、缝 alpha=0），材质 opacity<1 走透明通道 → 缝透出天空、云半透。
        //   alphaCutoff:0.02 仅丢弃近乎全透像素（保柔边 + 杜绝「透明底当不透明黑」黑底回退，t169 同族契约）。
        //   分层（PLAN §2）：纯呈现层，只读 worldClock.skyLight（Game 层 Q_PROPERTY）+ player.position，绝不反向写。
        Model {
            geometry: BillboardQuad {}
            // 绕 X 转 90°：BillboardQuad（XY 平面、前向 +Z）→ XZ 水平面、前向 -Y（朝下，抬头可见）。
            eulerRotation: Qt.vector3d(90, 0, 0)
            scale: Qt.vector3d(window.kCloudPlaneSize, window.kCloudPlaneSize, window.kCloudPlaneSize)
            position: {
                const eye = player.position
                return Qt.vector3d(eye.x + window.cloudDrift, eye.y + window.kCloudAltitude, eye.z)
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: cloudColor(worldClock.skyLight)
                opacity: 0.9
                alphaCutoff: 0.02
                baseColorMap: Texture {
                    source: "qrc:/textures/cloud.png"
                    generateMipmaps: false
                    scaleU: window.kCloudTiles
                    scaleV: window.kCloudTiles
                    tilingModeHorizontal: Texture.Repeat
                    tilingModeVertical: Texture.Repeat
                }
            }
        }

        // 共享图集纹理：3×3=9 个 per-chunk Model 共用一份 atlas（声明一次、按 id 引用）。
        // t414：resourcePack.active 时用 image://rp/atlas（合成图集 = 程序生成图集 + 包覆盖瓦片），
        //   否则 qrc:/textures/atlas.png（程序生成默认）。无包 / 禁用时 100% 沿用旧路径（零回归）。
        Texture { id: voxelAtlas; source: resourcePack.atlasSource; generateMipmaps: false }

        // t489 流体条带纹理（材质级 flipbook，替代 t222/t223 重建式水动画）：水/岩浆段独立材质 baseColorMap
        //   指向本条带（不走共享图集 voxelAtlas）→ 动画 positionV 只动水/岩浆面，不动其它方块。
        //   source：active → file:/// 落盘合成条带（程序生成条带 + 包内帧覆盖）；否则 qrc 程序生成条带。
        //   positionV：Qt 6.11 把旧 Texture.vOffset 更名为 positionV（positive positionV 上移采样 → 帧 k 在 v∈[k/N,(k+1)/N]）。
        //     绑 waterAnimFrame / lavaAnimFrame（下方 Timer 驱动），帧切换纯材质参数 → **零 mesh 重建**
        //     （F3 [w]/[s] reb 不回升；修 t222/t223「水 2s 全量重建水段 261 段/次」回归）。
        //   tilingMode Repeat：positionV 在 6.11 实测会把 UV 偏移到 [0,1] 外（采样坐标超出条带）→ 设 Repeat
        //     使超界部分回绕到对侧帧，避免 ClampToEdge 在动画中段把采样钳死在边缘帧（实测：ClampToEdge 下
        //     posV=0.5 采到 1.0 边缘条带而非中部帧）。N 帧 / 帧像素 16 由 resourcePack.stripFrames（与 BlockRegistry
        //     kWaterStripFrames=32 / kLavaStripFrames=16 同源）单一权威。
        Texture {
            id: waterStripTex
            source: resourcePack.waterStripSource
            generateMipmaps: false
            tilingModeHorizontal: Texture.Repeat
            tilingModeVertical: Texture.Repeat
            positionV: window.waterAnimFrame / resourcePack.waterStripFrames
        }
        Texture {
            id: lavaStripTex
            source: resourcePack.lavaStripSource
            generateMipmaps: false
            tilingModeHorizontal: Texture.Repeat
            tilingModeVertical: Texture.Repeat
            positionV: window.lavaAnimFrame / resourcePack.lavaStripFrames
        }

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
        // t398 鸡（Chicken；机制等价 MC 1.0 鸡，§9 原创）：白羽底 + 棕褐翅尖 / 尾羽斑 + 浅暖黄腹部（build_mob.py
        //   程序生成原创像素图，§9a 区隔不照搬 MC）。MobModel 小型鸟几何（躯干/头/尾/2 腿）每面铺整张贴图。
        Texture { id: mobChickenTex; source: "qrc:/textures/mob_chicken.png"; generateMipmaps: false }
        // t399 鱿鱼（Squid；机制等价 MC 1.0 squid，§9 原创）：深褐橘斑软体底 + 浅腹纹 + 暗点（build_mob.py
        //   程序生成原创像素图，§9a 区隔不照搬 MC）。MobModel 水生软体几何（躯干 + 顶端尖 + 8 触腕）每面铺整张贴图。
        Texture { id: mobSquidTex; source: "qrc:/textures/mob_squid.png"; generateMipmaps: false }
        // t480 狼（Wolf；机制等价 MC 1.0 狼，§9 原创）：灰狼毛皮底 + 深灰背脊 / 侧纹 + 浅灰腹纹（build_mob.py
        //   程序生成原创像素图，§9a 区隔不照搬 MC）。MobModel 犬科几何（细长躯干 + 尖头 + 立耳 + 4 腿）每面铺整张贴图。
        Texture { id: mobWolfTex; source: "qrc:/textures/mob_wolf.png"; generateMipmaps: false }
        Texture { id: mobOcelotTex;    source: "qrc:/textures/mob_ocelot.png";    generateMipmaps: false } // t481 豹猫（未驯服）
        Texture { id: mobCatBlackTex;  source: "qrc:/textures/mob_cat_black.png"; generateMipmaps: false } // t481 猫变体 0（黑）
        Texture { id: mobCatGingerTex; source: "qrc:/textures/mob_cat_ginger.png"; generateMipmaps: false } // t481 猫变体 1（姜黄）
        Texture { id: mobCatCreamTex;  source: "qrc:/textures/mob_cat_cream.png"; generateMipmaps: false } // t481 猫变体 2（奶油）
        // t487 银鱼（Silverfish；机制等价 MC 1.0 银鱼，§9 原创）：灰白甲壳底 + 深灰体节横纹 + 暗头斑（build_mob.py
        //   程序生成原创像素图，§9a 区隔不照搬 MC）。MobModel 小型虫几何（分节躯干 + 前伸小头 + 多对短腿）每面铺整张贴图。
        Texture { id: mobSilverfishTex; source: "qrc:/textures/mob_silverfish.png"; generateMipmaps: false }
        // t421 资源包生物贴图（pack entity texture）：pack 启用且 resourcePack.mobTextureSource(mobType) 命中包内
        //   entity PNG 时，source 为 file:///<entityDir>/<mob>/<mob>.png → 各 mob delegate 把 baseColorMap 切到本
        //   Texture + MobModel.packTextured=true（几何按 T 字 UV 展开进贴图）。pack 关 / 包内无该贴图 → source 空 →
        //   delegate 回退程序生成 mob_*.png（pig/cow/sheep/shambler/chicken）或纯色 baseColor（stalker/bones/spider）。
        //   source 绑定触碰 resourcePack.active → pack 切换即时刷新。运行期读本地 gitignored pack PNG，不 bake 进
        //   qrc/VCS（红线 §9）。mobType: pig=1/cow=2/sheep=3/shambler=4(zombie)/bones=5(skeleton)/stalker=6(creeper)/
        //   spider=7/chicken=8。Squid(9) 不映射（spec 未列，保留程序生成）。
        Texture { id: mobPigPackTex;      source: resourcePack.active ? resourcePack.mobTextureSource(1) : ""; generateMipmaps: false }
        Texture { id: mobCowPackTex;      source: resourcePack.active ? resourcePack.mobTextureSource(2) : ""; generateMipmaps: false }
        Texture { id: mobSheepPackTex;    source: resourcePack.active ? resourcePack.mobTextureSource(3) : ""; generateMipmaps: false }
        Texture { id: mobShamblerPackTex; source: resourcePack.active ? resourcePack.mobTextureSource(4) : ""; generateMipmaps: false }
        Texture { id: mobBonesPackTex;    source: resourcePack.active ? resourcePack.mobTextureSource(5) : ""; generateMipmaps: false }
        Texture { id: mobStalkerPackTex;  source: resourcePack.active ? resourcePack.mobTextureSource(6) : ""; generateMipmaps: false }
        Texture { id: mobSpiderPackTex;   source: resourcePack.active ? resourcePack.mobTextureSource(7) : ""; generateMipmaps: false }
        Texture { id: mobChickenPackTex;  source: resourcePack.active ? resourcePack.mobTextureSource(8) : ""; generateMipmaps: false }
        // feat 雪/铁傀儡 pack entity 贴图（机制等价 MC 1.0 雪傀儡 / 铁傀儡，§9 区隔：贴图仅贴雪块身 / 铁块身；
        //   南瓜头 + 刻面眼/嘴是单独的橙色南瓜 Model，不是贴图的一部分）。pack 命中 → Main.qml 傀儡 delegate 把
        //   几何切到 MobModel + T 字 UV 展开进该贴图（snow_golem.png 扁平 / iron_golem/iron_golem.png 子目录）；
        //   pack 关 → source 空 → 回退纯色雪白 / 铁灰（MobModel packTextured=false 全脸 UV，无程序生成贴图）。
        //   修 dev-plan C「铁傀儡全白」：pack iron_golem.png 铁纹才显铁质（程序纯色铁灰 #7d848c 在用户视角读作「白」）。
        Texture { id: mobSnowGolemPackTex; source: resourcePack.active ? resourcePack.mobTextureSource(12) : ""; generateMipmaps: false }
        Texture { id: mobIronGolemPackTex; source: resourcePack.active ? resourcePack.mobTextureSource(13) : ""; generateMipmaps: false }

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
                        objs.push(lavaChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })) // t343 岩浆段
                        objs.push(crossChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })) // t326 cutout 段（草丛/作物/树苗）
                        objs.push(glassChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })) // t405 玻璃段（透明）
                        objs.push(iceChunkComp.createObject(chunkAnchor, { chunkCX: cx, chunkCZ: cz })) // t468 冰段（半透）
                    }
                }
                window.terrainGeos = geos
                window.chunkObjects = objs
                window.recomputeMeshStats()   // 取初值（createObject 时各段已 buildMesh；后续 meshRebuilt 增量刷新）
                console.info("[t276] built", objs.length, "chunk Models (" + nx + "x" + nz + "=" + (nx*nz) + " chunks)")
                // t470 首次刷新 chunk visibility：player.feetPosition 此前可能已 emit positionChanged 但 chunksBuilt
                //   守门跳过；chunks 现就绪 → 显式初始化 _playerCX/_playerCZ + 应用 culling（远端 chunk 不可见）。
                window._updatePlayerChunk()
            }
        }

        // 地形段 chunk Model 模板（culled mesh，非水方块）。cx/cz 由 createObject initial properties 注入；
        //   内层 ChunkGeometry 经 terrainModel（本组件实例根 id）读 chunkCX/CZ —— 与 t170 torchDelegate 子引用
        //   torchGlow 同一 Component 内根-id 引用模式（已验证）。
        // t442 alphaMode 修复（根因）：terrain 段唯一的 alpha 带 tile = leaves(9)。oak_leaves 贴图是「fancy 叶」
        //   ——约 21% 像素带 alpha（叶间隙），且这些透明像素的 RGB 多为纯黑 (0,0,0)（fancy 叶贴图的间隙约定）。
        //   旧材质虽写了 `alphaCutoff:0.5` 但**未设 alphaMode**（缺省 Opaque）→ Qt 6.8+ 下 alphaCutoff 仅在
        //   alphaMode:Mask 时才生效（lessons-learned t439）：Opaque 模式 alpha 被完全忽略 → 叶间隙的黑 RGB
        //   透明像素当不透明渲染 = 整叶面散布黑斑、且无透空 → 用户「树叶颜色/贴图不对」（t416/t422 加 tint / 改
        //   grass_side 均未触及此根因——根因在材质 alphaMode，非 tint / 非贴图源 / 非路由误分类）。修：加
        //   `alphaMode:Mask` 让既有 alphaCutoff 生效（同 cross/cutout 段契约，t439）：叶间隙 alpha<0.5 像素硬丢弃
        //   → 干净透空绿叶、黑斑消失；地形其余 tile（grass/dirt/stone/...）全 alpha=255 → Mask 下零丢弃、外观不变。
        //   Mask 走不透明 pass 写深度，与旧 Opaque 深度行为一致 → 零剔除 / z-fight 回归。Leaves 经 PASS 2 立方面
        //   路由（solid 整立方，非 cross / 非异形），leaf tint 仍由 ResourcePackManager 对包内灰度 oak_leaves.png
        //   applyFoliageTint 处理（tile 9 isFoliageTinted=true）；本默认图集 leaves 已是预染绿，无需再 tint。
        Component {
            id: terrainChunkComp
            Model {
                id: terrainModel
                property int chunkCX: 0
                property int chunkCZ: 0
                // t470 渲染距离 + 空段剔除：chunkInRange 由 _refreshChunkVisibility 设（玩家跨 chunk 边界时）；
                //   geo.vertexCount > 0 跳过空段（本段几乎总有内容，但绑定统一为「在范围内且非空」）。
                property bool chunkInRange: true
                visible: chunkInRange && terrainGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: terrainGeo
                    world: theWorld
                    cx: terrainModel.chunkCX
                    cz: terrainModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量，方块光时间不变）
                    chunkInRange: terrainModel.chunkInRange // t472：视距门控传给 mesher（远端跳过 sun/water/编辑重建）
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; alphaMode: PrincipledMaterial.Mask; alphaCutoff: 0.5; baseColor: Qt.rgba(1.0, 1.0, 1.0, 1.0) }
            }
        }

        // 水段 chunk Model 模板（t148：waterOnly 只网格化 Water，opacity 0.7 半透；tXXX 静态水——mesh 恒用
        //   phase 0 帧（静水 tile 19 / 流水 tile 23）+ 烘死的空间涟漪，flipbook 换帧驱动已删，见 tXXX 注释）。
        Component {
            id: waterChunkComp
            Model {
                id: waterModel
                property int chunkCX: 0
                property int chunkCZ: 0
                property bool chunkInRange: true
                visible: chunkInRange && waterGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: waterGeo
                    world: theWorld
                    cx: waterModel.chunkCX
                    cz: waterModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    chunkInRange: waterModel.chunkInRange // t472：视距门控传给 mesher（远端水段跳过重建）
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量，方块光时间不变）
                    waterOnly: true
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: waterStripTex; vertexColorsEnabled: true; opacity: 0.7; alphaMode: PrincipledMaterial.Blend; baseColor: Qt.rgba(1.0, 1.0, 1.0, 1.0) }
            }
        }

        // t343 岩浆段 chunk Model 模板（lavaOnly 只网格化 Lava，opacity≈0.95 近不透 + NoLighting 暖色 baseColor 显
        //   自发光感）。机制等价 MC 1.0 岩浆（近不透浓稠流体；慢流、不可破、玩家穿过）。岩浆段复用 culled/greedy 立方面
        //   路径（满格立方 + 自剔 nb==Lava + 邻实体剔），不效仿水的变高水面。baseColor 用暖橙略提亮（NoLighting 下 baseColor
        //   直接乘进最终色 → 岩浆显炽热自发光感，区别于地形的环境光调制）。摆位同地形/水段；透明物体由 QtQuick3D 渲染队列
        //   自动排在不透明地形之后。lit 红线：PrincipledMaterial 必须 NoLighting（默认 lit 在 D3D11 不出像素）。
        Component {
            id: lavaChunkComp
            Model {
                id: lavaModel
                property int chunkCX: 0
                property int chunkCZ: 0
                property bool chunkInRange: true
                visible: chunkInRange && lavaGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: lavaGeo
                    world: theWorld
                    cx: lavaModel.chunkCX
                    cz: lavaModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    chunkInRange: lavaModel.chunkInRange // t472：视距门控传给 mesher（远端岩浆段跳过 sun 重建）
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量；岩浆自发光由 block flood-fill 时间不变）
                    lavaOnly: true
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: lavaStripTex; vertexColorsEnabled: true; opacity: 0.95; alphaMode: PrincipledMaterial.Blend; baseColor: Qt.rgba(1.0, 0.82, 0.6, 1.0) }
            }
        }

        // t405 玻璃段 chunk Model 模板（glassOnly 只网格化 Glass，opacity≈0.45 半透 + NoLighting + 顶点色光照 →
        //   透过玻璃可见背后的方块 / 实体）。机制等价 MC 1.0 玻璃（glass：透明整立方，透视背后物）。研究要点：本
        //   D3D11 / QtQuick3D 后端的「真透明」走 PrincipledMaterial 的 opacity<1（透明渲染队列，QtQuick3D 自动排在不
        //   透明地形之后、按深度排序），而非 alpha-cutout（cutout 是硬透明边、无半透，适合草丛 / 火把透明底，不适合
        //   玻璃「整体半透」观感）。玻璃段走 culled/greedy 整立方面路径（满格立方 + 自剔 nb==Glass + 邻实体剔 + 邻空气画）；
        //   Glass solid=false → 相邻实体方块不剔面 → 透过半透玻璃可见背后方块（关键透视保证）。baseColor 取浅青白略提亮
        //   （NoLighting 下 baseColor 直接乘进最终色 → 玻璃显淡青玻璃质感；区别于岩浆暖橙自发光）。摆位同地形 / 水 / 岩浆段。
        //   lit 红线：PrincipledMaterial 必须 NoLighting（默认 lit 在 D3D11 不出像素，见 lessons-learned 渲染盲区静态化条）。
        Component {
            id: glassChunkComp
            Model {
                id: glassModel
                property int chunkCX: 0
                property int chunkCZ: 0
                property bool chunkInRange: true
                visible: chunkInRange && glassGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: glassGeo
                    world: theWorld
                    cx: glassModel.chunkCX
                    cz: glassModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    chunkInRange: glassModel.chunkInRange // t472：视距门控传给 mesher（远端玻璃段跳过 sun 重建）
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量，方块光时间不变）
                    glassOnly: true
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; opacity: 0.45; alphaMode: PrincipledMaterial.Blend; baseColor: Qt.rgba(0.92, 0.97, 1.0, 1.0) }
            }
        }

        // t468 冰段 chunk Model 模板（iceOnly 只网格化冰族 Ice/PackIce/BlueIce，opacity≈0.7 半透 + NoLighting + 顶点色
        //   光照 → 透过半透冰可见背后的方块 / 实体，机制等价 MC 1.0 半透冰）。机制 / 路由完全同玻璃段（glassOnly）：
        //   透明整立方走 culled/greedy 立方面路径（满格立方 + 自剔 nb==冰族 + 邻实体剔 + 邻空气画）；冰族 solid=false →
        //   相邻实体方块不剔面 → 透过半透冰可见背后方块（关键透视保证，同 glass）。三冰种（Ice/PackIce/BlueIce）共用
        //   一个半透材质——视觉差异由各自贴图（ice 浅蓝 / pack_ice 实白 / blue_ice 淡蓝）表达，半透度统一 0.7（避免
        //   per-block 多材质 / 多段的复杂度）。baseColor 取冷青白略提亮（NoLighting 下 baseColor 直接乘进最终色 → 冰显
        //   冷玻璃质感；区别于玻璃的浅青白）。摆位同地形 / 水 / 岩浆 / 玻璃段。lit 红线：NoLighting（默认 lit 不出像素）。
        Component {
            id: iceChunkComp
            Model {
                id: iceModel
                property int chunkCX: 0
                property int chunkCZ: 0
                property bool chunkInRange: true
                visible: chunkInRange && iceGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: iceGeo
                    world: theWorld
                    cx: iceModel.chunkCX
                    cz: iceModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    chunkInRange: iceModel.chunkInRange // t472：视距门控传给 mesher（远端冰段跳过 sun 重建）
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量，方块光时间不变）
                    iceOnly: true
                }
                // t495 二轮复盘 冰改不透明渲染：去掉 opacity:0.7 + alphaMode:Blend，让冰走**不透明 pass**（深度写 ON）。
                //   根因：冰贴图（default_ice.png，build_ice.py base() alpha=255 全不透）本无透明像素，旧材质强行 opacity:0.7
                //   + alphaMode:Blend 把冰塞进**透明 pass**（深度写 OFF、按 Model 排序）。冰-水接触圈：冰面与水面在邻接
                //   处几何上紧贴 / 微叠（冰格底面贴水格顶面，或冰水相邻格侧壁），两透明面进入同一透明排序桶 →
                //   QtQuick3D 透明 pass 按 Model 质心距离排序，相机移动 / 转视角时两冰水 Model 的相对深度顺序逐帧翻转 →
                //   一帧冰盖水面、下一帧水盖冰面 = 用户实测「冰水接触一圈移动时闪烁」（静止排序稳 → 不闪，动则闪；
                //   教科书透明排序 z-fight）。机制等价 MC 1.0：**冰在 MC 是不透明方块**（不像水半透），故 MC 冰水边界稳
                //   定不闪。修：冰走不透明 pass 写深度 → 透明排序只余水（单一透明材质，自排序无歧义）→ 冰水边界稳定。
                //   视觉：冰失去 0.7 半透感（不能再透视冰后方块），换 MC 一致的不透明冰质感（冰裂纹贴图 + 冷青蓝底
                //   仍显冰质，非损失）。lit 红线：NoLighting（默认 lit 在 D3D11 不出像素，PLAN §2-H）。
                //   R19 B6：baseColor 改白（昼夜乘子由 C++ 烘进顶点色天空分量 dayMul 承担，方块光时间不变；同地形 / 水段）。
                //   alphaMode 缺省 = Opaque（深度写 ON）。不变量：冰不发光（无 emissiveFactors），符合 lighting 铁律。
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: Qt.rgba(1.0, 1.0, 1.0, 1.0) }
                // t495 一轮 冰-水透明度过渡：冰浅蓝由贴图 tile 58 自身提供（baseColor 白只调亮度，亮度由顶点色 dayMul 承载）。
            }
        }

        // t326 cross cutout 段 chunk Model 模板：cross 广告牌方块（草丛 / 小麦作物 / 树苗）的独立 cutout 段。
        //   cross 贴图带 alpha 透明底（草叶 / 树苗本体 alpha=255、底 alpha=0），须 alpha-test cutout 才显透明
        //   间隙（否则显成两片实心板挡视线）。
        // t439 透明 Z-fighting 修复（核心）：改用 **alphaMode: Mask**（Qt 6.8+ 原生 alpha-test）。Mask 模式让本段在
        //   **不透明 pass** 渲染（深度写 ON、alpha 硬丢弃：alpha<alphaCutoff 的像素直接 discard、保留像素按不透明写深度），
        //   而非透明 pass。旧实现靠 `opacity:0.99` 强制走透明通道（pre-6.8 alphaCutoff 仅在 opacity<1 下生效的 backend
        //   workaround），把 cutout 草丛错误地塞进透明 pass（深度写 OFF、按 Model 排序）→ 草丛不写深度、相邻草丛 / 远处
        //   透明面无正确遮挡 → 「透过草丛看远处闪烁 / 穿透错乱」（spec t439 根因）。Mask 模式 = 教科书 cutout foliage：
        //   草叶写深度 → 近草丛正确遮挡远草丛 / 远处水与玻璃、无 z-fight；透明底照常 discard（不显黑底、不挡视线）。
        //   alphaCutoff:0.5 沿用 torch / crack / MaterialIcon alpha-test 契约（Mask 模式下 alphaCutoff 真正生效，
        //   无需再降 opacity）。cutoutOnly:true 让 ChunkGeometry 仅网格化 cross（PASS 1 pushCross）、跳过立方面（PASS 2）。
        //   几何顶点为 chunk 局部坐标、position 同 terrain/water 段；顶点色光照（terrainLight + vertexColors）沿用同管线。
        //   不变量（PLAN §2-H）：lighting 仍 NoLighting（lit 在 D3D11 不出像素，见 lessons-learned）。
        Component {
            id: crossChunkComp
            Model {
                id: crossModel
                property int chunkCX: 0
                property int chunkCZ: 0
                property bool chunkInRange: true
                visible: chunkInRange && crossGeo.vertexCount > 0
                position: Qt.vector3d(chunkCX * 16, 0, chunkCZ * 16)
                geometry: ChunkGeometry {
                    id: crossGeo
                    world: theWorld
                    cx: crossModel.chunkCX
                    cz: crossModel.chunkCZ
                    sunDir: worldClock.sunDir
                    shadowsEnabled: window.shadowsEnabled
                    greedyMeshing: window.greedyMeshing
                    chunkInRange: crossModel.chunkInRange // t472：视距门控传给 mesher（远端 cutout 段跳过 sun 重建）
                    dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量，方块光时间不变）
                    cutoutOnly: true   // t326：仅 cross 方块（草丛/作物/树苗）→ cutout 材质（t439 alphaMode:Mask 不透明 pass）
                }
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; alphaMode: PrincipledMaterial.Mask; alphaCutoff: 0.5; baseColor: Qt.rgba(1.0, 1.0, 1.0, 1.0) }
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
                const _r = selectionBox.worldRev;
                return _r >= 0 ? (player.hasHit ? theWorld.stateAt(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z) : 0) : 0
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
                const _r = selectionBoxTorch.worldRev;
                return _r >= 0 ? computeTorchOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z,
                                          findTorchPrefOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z)) : ""
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

        // t401 钓鱼浮标（仅 player.fishing 时显）：小立方体浮在水面（player.bobberPosition），咬钩时下沉一点
        //   （hasBite → y -0.12，表「鱼扯浮标」）。NoLighting（同地形 / 线框已验证可见路径）。红顶 + 白底表浮标。
        //   分层（PLAN §2）：呈现层只读 player.fishing / bobberPosition / hasBite（Game 层算时序），不反向写。
        Model {
            visible: player.fishing
            position: Qt.vector3d(player.bobberPosition.x,
                                  player.bobberPosition.y - (player.hasBite ? 0.12 : 0.0),
                                  player.bobberPosition.z)
            scale: Qt.vector3d(0.14, 0.14, 0.14)
            geometry: UnitCube {}
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#d83838" // 浮标红（与 ToolIcon 浮标红顶同色）
            }
        }

        // 挖掘裂纹叠层（t34 / t410 异形贴合）：仅生存持续挖掘时显（miningStage >= 0；创造瞬破不进入
        //   累积态，stage=-1 → 隐藏）。叠在目标方块上（position = miningBlock + 0.5 中心）；baseColorMap 按
        //   miningStage 切 6 阶裂纹 PNG（0/20/40/60/80/100%）。
        //   t410：裂纹叠层按被挖方块形状缩放（CrackBox 据 blockId/state 走 selectionAABBs 各 sub-AABB 一盒），
        //     故破台阶显半高叠层、破栅栏显立柱叠层、破楼梯显下步+背墙双盒——不再恒为整立方（spec t410）。
        //     blockId/state 绑 player.miningBlock 处的 blockAt/stateAt（miningBlock 切换 → 重算 → CrackBox rebuild）。
        // 分层（PLAN §2）：呈现层只读 player.miningStage + World（blockAt/stateAt，只读）+ Core BlockRegistry
        //   单一权威取形状，不反向写进度 / 栅格；裂纹贴图自绘原创（tools/build_cracks.py 程序生成，§9 override (a)）。
        // 材质：NoLighting + hasTransparency → 透明底（alpha=0）不遮方块本色，仅黑裂纹显示。
        // cullMode 默认 Back（仅外法线面可见）→ 玩家看得到的几面才显裂纹（背向面被剔除）。
        Model {
            visible: player.miningStage >= 0
            position: Qt.vector3d(player.miningBlock.x + 0.5,
                                  player.miningBlock.y + 0.5,
                                  player.miningBlock.z + 0.5)
            scale: Qt.vector3d(1.005, 1.005, 1.005) // 微放大防与方块面 z-fight（CrackBox 另烘焙 per-face kEps 缝，异形 sub-AABB 不以中心对称亦无重面闪烁）
            geometry: CrackBox {
                blockId: theWorld.blockAt(player.miningBlock.x, player.miningBlock.y, player.miningBlock.z)
                state: theWorld.stateAt(player.miningBlock.x, player.miningBlock.y, player.miningBlock.z)
            }
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

            // t377 护甲 tier → 基础色 (r,g,b) 浮点（与 MaterialIcon 护甲配色同源：皮革棕 / 铁银 / 铜橙 / 金黄 /
            //   钻石青）。3rd-person 装备护甲 Model 据此着色（material-colored，spec t377「THIRD-PERSON player model
            //   shows equipped armor (per piece, material-colored)」）。armorId 非护甲 → 兜底铁银灰。
            function armorBaseColor(armorId) {
                const tier = hotbarVM.armorTier(armorId)
                switch (tier) {
                case 0: return [0.541, 0.353, 0.169] // 皮革 #8a5a2b
                case 1: return [0.847, 0.847, 0.847] // 铁 #d8d8d8
                case 2: return [0.784, 0.471, 0.314] // 铜 #c87850
                case 3: return [0.980, 0.847, 0.251] // 金 #fad840
                case 4: return [0.306, 0.878, 0.784] // 钻石 #4ee0c8
                default: return [0.847, 0.847, 0.847]
                }
            }
            // t377 护甲 Model 的 baseColor = tier 色 lerp 向红（受伤变红，同身体 hurtTint）+ bodyOpacity。
            //   与身体部件统一走 hurtTint（不叠 terrainLight —— 玩家身体本身不随昼夜变暗，护甲一致）。
            function armorMatColor(armorId) {
                const c = playerModel.armorBaseColor(armorId)
                return playerModel.hurtTint(playerModel.hurt, c[0], c[1], c[2])
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

            // t508 二轮复盘修「坐上去是站着」（用户报④）：骑船时玩家应呈坐姿（大腿前抬近水平 + 小腿竖直下垂），
            //   机制等价 MC 船骑乘坐姿。旧版骑乘期 m_pos 同步到船中心但玩家模型仍走站立姿态（腿笔直竖、与船舱
            //   不贴合）→ 肉眼读作「站着」。加 sitBlend（=1 骑船 / 0 否则），驱动大腿前抬 sitThigh +
            //   小腿回折 sitKnee → 大腿水平前伸、小腿竖直下垂 = 坐姿。
            //   sitThigh 与 crouchThigh 叠加（蹲+骑乘不会同时 —— 蹲需走路模式，骑乘禁走路，故二者互斥；为稳
            //   妥两量相加，crouchBlend 骑乘期恒 0 → sitThigh 独占）。坐姿不抬高身体（船中心已是骑乘 m_pos，
            //   body 上半身在 m_pos 之上正常高度，仅腿弯成坐姿）。
            //   ⚠ 绑定依赖：ridingIndex() 是 Q_INVOKABLE 无 NOTIFY → 直接调不随骑乘切重算。显式读 boats.revision
            //   （NOTIFY=entitiesChanged，tryMount/dismount/clearAll 都发）建依赖 → 上 / 下船时 revision bump →
            //   isRidingBoat / sitBlend 重算 → 腿姿切换（同 boats delegate {revision; posAt} 模式）。
            //   t556：语句块形式 → 表达式形式（lessons-learned t498：NOTIFY 属性须参与值计算才可靠注册依赖；
            //   静态 playerModel 节点上语句块形式有漏注册风险，boat revision 高频掩盖过、但统一表达式形式绝后患）。
            readonly property bool isRidingBoat: boats.revision >= 0
                                                 ? (player.boatManager ? player.boatManager.ridingIndex() >= 0 : false)
                                                 : false
            // t565 骑矿车同坐姿（机制等价 MC 1.0 矿车骑乘坐姿；feet = 车中心 - 0.3 ≈ 轨面，与船同「坐进斗」
            //   几何 → 复用同一 sitBlend / sitThigh / sitKnee / sitDrop）。同 isRidingBoat 的 revision 触碰模式。
            readonly property bool isRidingCart: carts.revision >= 0
                                                 ? (player.minecartManager ? player.minecartManager.ridingIndex() >= 0 : false)
                                                 : false
            readonly property real sitBlend: (playerModel.isRidingBoat || playerModel.isRidingCart) ? 1.0 : 0.0
            // t532「坐姿 = 腿与身 90°，非卡地底」复盘：旧 sitThigh/sitKnee=±85°（钝角非直角）+ sitDrop=0.42
            //   → 髋枢降到 feet+0.18，大腿水平时小腿竖直下垂 0.3 → 脚落 feet−0.12（穿船底 / 穿地 =「人卡地底」用户报）。
            //   几何推导：大腿绕髋 +θ 转，膝（本地 (0,−0.3,0)）→ 世界 (0,−0.3·cosθ, −0.3·sinθ)；θ=90° → 膝同髋高、前伸 0.3。
            //   小腿绕膝 −θ（sitKnee=−sitThigh）回正 → 复合旋转 0°（竖直），脚 = 膝位 + (0,−0.3,0) → 脚 Y = 髋 Y − 0.3。
            //   要脚落 feet（Y=0）→ 髋 Y=0.3 → upperBody/腿枢轴 y=0.6−sitDrop=0.3 → sitDrop=0.3。
            //   修：(a) 角度 ±85→±90（真直角，机制等价 MC 船坐姿「大腿水平、小腿垂直」）；(b) sitDrop 0.42→0.3
            //   → 脚恰落 feet（甲板 / 水面），身体坐进船舱（头胸露舷上），不穿地 / 不穿船底。区别蹲：crouch=60°（钝角，脚前移落地走）；sit=90°（直角，纯坐）。
            readonly property real sitThigh: 90.0 * playerModel.sitBlend      // 坐姿大腿前抬（度；+x = 腿尖前摆 = -Z，真水平 90°）
            readonly property real sitKnee: -90.0 * playerModel.sitBlend      // 坐姿膝盖回折（度；= −sitThigh → 小腿垂直下垂）
            // boat 三轮「坐上去是站着」（用户报①）：旧 sitBlend 只折腿（大腿前抬 + 小腿回折），**上半身不
            //   下沉** —— 髋仍站在 feet+0.6，躯干在船舷之上直立到 feet+1.8，配 0.45 高小船 → 肉眼读作「站着
            //   站在船里」。修：坐姿把髋（upperBody + 双腿枢轴）整体下沉 sitDrop → 身体「坐进船舱」，只露头 +
            //   半胸在船舷之上（机制等价 MC 船骑乘坐姿）。
            //   与 crouchDrop 独立（骑乘期 moveState=Walk → crouchBlend 恒 0，两者互斥不叠加；即便叠加也安全，
            //   只是下沉更多）。下船（dismount bump revision）→ sitBlend=0 → sitDrop=0 → 恢复站姿。
            //   t532：sitDrop=0.3 → 髋枢 feet+0.3，配 sitThigh=90°（大腿水平）+ sitKnee=−90°（小腿垂直下垂 0.3）
            //     → 脚落回 feet（不穿地 / 不穿船底）。几何推导见 sitThigh 注。
            readonly property real sitDrop: 0.3 * playerModel.sitBlend

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
                position: Qt.vector3d(0, 0.6 - playerModel.crouchDrop - playerModel.sitDrop, 0)   // 髋枢：与双腿枢轴同高，蹲下随髋下沉；boat 三轮骑乘时随坐姿下沉入船
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
                    // t377/t452/t498 头盔（装备槽 0）：作 headNode 子节点 → 随头部俯仰。visible 绑装备槽 0 是否有护甲；
                    //   材质色 = tier（armorMatColor）。
                    //   t498 真因（用户「穿装甲 F5 第三人称看不见」，前 3-4 次只调凸出量仍未好）：armId 绑定用
                    //     语句块形式 `{ hotbarVM.armorRevision; return armorBlockIdAt(0) }` —— 该形式在静态构建的
                    //     QQuick3D Model 上**不注册 armorRevision 的 NOTIFY 依赖**（实测：装备后 armId 恒 0、visible 恒
                    //     false，armorSlotsChanged 信号已到 playerModel 但 Model 内 armId 绑定不重算）。mob 护甲
                    //     (t377) 同语句块形式却"看似工作"只因 entityManager.revision 每帧随实体移动高频刷新、顺带
                    //     重算；player armorRevision 仅装备时变 → 语句块依赖漏注册即永久不更新。修：改表达式形式
                    //     `armorRevision >= 0 ? armorBlockIdAt(0) : 0` —— NOTIFY 属性参与值计算，依赖被可靠注册，
                    //     装备/脱下后 armId/visible 正确刷新（实测 onArmIdChanged→772、visible→true）。此模式同时应用
                    //     到胸/腿/小腿/靴共 8 个护甲 Model。凸出量（z scale 0.56 等）本身是对的，不是根因。
                    Model {
                        id: playerArmorHead
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(0) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.30, 0.06)
                        scale: Qt.vector3d(0.60, 0.58, 0.56)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorHead.armId); opacity: playerModel.bodyOpacity }
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
                // t377/t452/t498 胸甲（装备槽 1）：作 upperBody 子节点 → 随鞠躬前倾。t452 放大包裹躯干
                //   （X 探 0.04 / Y 探 0.02 / Z 探 0.07）；t498 绑定改表达式形式（见头盔注：语句块形式不注册
                //   armorRevision NOTIFY → armId/visible 恒 0）。
                Model {
                    id: playerArmorChest
                    property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(1) : 0
                    visible: armId !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, 0.35, 0)
                    scale: Qt.vector3d(0.58, 0.74, 0.44)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorChest.armId); opacity: playerModel.bodyOpacity }
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
                    // t498 二轮复盘：左臂胸甲袖（装备槽 1 胸甲覆盖手臂；机制等价 MC 1.0 chestplate 覆盖躯干+双臂）。
                    //   作 leftArmPivot 子节点 → 随左臂行走摆动。略大于袖段（X/Z 探 0.02 包裹袖）。
                    //   visible 绑装备槽 1 是否有护甲（armId 表达式形式，同胸甲 playerArmorChest）。
                    //   红线：NoLighting（同现有护甲 Model 不变量 §2）。
                    Model {
                        id: playerArmorSleeveL
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(1) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.30, 0.52, 0.30)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorSleeveL.armId); opacity: playerModel.bodyOpacity }
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
                    // t498 二轮复盘：右臂胸甲袖（同左臂 playerArmorSleeveL，镜像；覆盖右袖，随右臂行走/挖掘挥动）。
                    Model {
                        id: playerArmorSleeveR
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(1) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.30, 0.52, 0.30)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorSleeveR.armId); opacity: playerModel.bodyOpacity }
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
                        visible: player.selectedBlock !== 0 && player.selectedBlock !== 13 && !hotbarVM.isPartialBlock(player.selectedBlock) && !hotbarVM.isCrossBlock(player.selectedBlock) && !hotbarVM.isBed(player.selectedBlock) && player.mode !== PlayerController.Spectator
                        geometry: BlockCube { blockId: player.selectedBlock }
                        position: Qt.vector3d(0, -0.55, -0.30)   // t72：移到手前方（手心前缘 z≈-0.125 前），不嵌进手里
                        scale: Qt.vector3d(0.22, 0.22, 0.22)
                        eulerRotation: Qt.vector3d(-12, 42, 0)   // t72：绕 Y ~42° 倾斜 + 微 pitch，像 MC 手持姿态
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            opacity: playerModel.bodyOpacity
                            alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）/ cross 段（isCrossBlock）/ 床段（isBed，t496）走下方 billboard 分支
                        }
                    }
                    // t219 手持木板衍生方块（第三人称）：异形段（台阶/楼梯/栅栏/压力板/门/活板门）非整立方 →
                    //   billboard 平图标（dimetric 立体图标），非满格木板立方。作 rightArmPivot 子节点随臂行走/
                    //   挖掘挥动同步。BillboardQuad +Z 法线默认 backface 剔除 → 第三人称-前（相机在玩家前）见背面
                    //   被剔 → 图标消失；故 cullMode:NoCulling 双面渲染（背面镜像图标，异形近对称无明显差异）→
                    //   三相机模式都可见。scale 0.22（同手持方块立方，平图标等大）；opacity 跟 bodyOpacity（观察者
                    //   半透一致）；alphaCutoff:0.5 沿用透明底 alpha-test 契约。partialIconTex 共享第一人称同一份。
                    // t496 二轮复盘 床亦走本 billboard 分支（同第一人称 viewModelHand；bed 图标而非满格被面色立方）。
                    Model {
                        visible: (hotbarVM.isPartialBlock(player.selectedBlock) || hotbarVM.isCrossBlock(player.selectedBlock) || hotbarVM.isBed(player.selectedBlock)) && player.mode !== PlayerController.Spectator
                        geometry: BillboardQuad {}
                        position: Qt.vector3d(0, -0.55, -0.30)
                        scale: Qt.vector3d(0.22, 0.22, 0.22)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            cullMode: Material.NoCulling   // 双面（第三人称-前见背面；异形图标近对称）
                            alphaCutoff: 0.5
                            opacity: 0.99   // visible 已排除 Spectator → bodyOpacity 恒 1.0；<1 尊重贴图 alpha（透明底不渲染）
                            baseColorMap: partialIconTex   // t440：cross 段（花/蘑菇/睡莲/树苗…）flat 图标同 partialIconTex；t496 床段亦同
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金镐金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜镐铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 4 ? "#4fd9d2"   // t472 钻石镐青绿
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金工具金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜工具铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金工具金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜工具铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金工具金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜工具铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金工具金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜工具铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                            baseColor: hotbarVM.toolTier(player.selectedItem) === 5 ? "#f2c832"   // t557 金工具金黄
                                     : hotbarVM.toolTier(player.selectedItem) === 6 ? "#c87850"   // t557 铜工具铜橙
                                     : hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"
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
                position: Qt.vector3d(-0.125, 0.6 - playerModel.crouchDrop - playerModel.sitDrop, 0)
                eulerRotation: {
                    // 行走摆幅（t51 ×swingAmp）+ 蹲下大腿前抬（t65 crouchThigh）+ 坐姿大腿前抬（t508 sitThigh）。
                    const walk = -Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp
                    return Qt.vector3d(walk + playerModel.crouchThigh + playerModel.sitThigh, 0, 0)
                }
                // 大腿段（裤色 #3a3a5a；髋下 0..0.3，中心 -0.15、scale.y=0.3）
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                }
                // t377/t452 左护腿-大腿段（装备槽 2）：作大腿枢轴子节点 → 随大腿行走 / 蹲下摆动。t452 放大
                //   （X/Z 探 0.025、Y 探 0.02）使第三人称可见；小腿段见 leftKneePivot 内 playerArmorCalfL（MC 护腿
                //   覆盖整条腿，故分大腿 / 小腿两段随膝关弯折）。t498 绑定改表达式形式（见头盔注）。
                Model {
                    id: playerArmorLegL
                    property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(2) : 0
                    visible: armId !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.34, 0.34, 0.34)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorLegL.armId); opacity: playerModel.bodyOpacity }
                }
                // 膝盖关节（t65）：位于大腿末端（髋下 0.3）。站立 0°（小腿续大腿成直线）；蹲下回折
                //   crouchKnee（=−crouchThigh）→ 小腿相对大腿弯折、整体腿弯曲，有效竖直高度缩短配合身体下沉。
                Node {
                    id: leftKneePivot
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(playerModel.crouchKnee + playerModel.sitKnee, 0, 0)
                    // 小腿段（膝下 0..0.3，中心 -0.15、scale.y=0.3）
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                    }
                    // t452 左护腿-小腿段（装备槽 2）：作膝盖枢轴子节点 → 随小腿 / 蹲下弯折。与大腿段 playerArmorLegL
                    //   共享装备槽 2（护腿覆盖整条腿）；放大同大腿段（探 0.025），第三人称小腿护甲清晰可见。
                    //   t498 绑定改表达式形式（见头盔注）。
                    Model {
                        id: playerArmorCalfL
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(2) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.34, 0.34, 0.34)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorCalfL.armId); opacity: playerModel.bodyOpacity }
                    }
                    // t377/t452 左靴（装备槽 3）：作膝盖枢轴子节点 → 随小腿摆动。t452 放大并前移（-Z=玩家前方）
                    //   形成明显靴头：X 探 0.025、Z 前探 0.075（靴头超出小腿）、覆盖脚踝。脚底约贴地（微入地 <0.02）。
                    //   t498 绑定改表达式形式（见头盔注）。
                    Model {
                        id: playerArmorBootL
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(3) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.24, -0.03)
                        scale: Qt.vector3d(0.34, 0.14, 0.38)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorBootL.armId); opacity: playerModel.bodyOpacity }
                    }
                }
            }
            // 右腿枢轴（t45 / t65）：与左腿对称（右髋 0.125, 0.6, 0），行走与左臂同相（+sin）；蹲下同步下沉+膝盖弯。
            Node {
                id: rightLegPivot
                position: Qt.vector3d(0.125, 0.6 - playerModel.crouchDrop - playerModel.sitDrop, 0)
                eulerRotation: {
                    // 行走摆幅（t51 ×swingAmp；与左腿对称）+ 蹲下大腿前抬（t65 crouchThigh）+ 坐姿大腿前抬（t508 sitThigh）。
                    const walk = Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp
                    return Qt.vector3d(walk + playerModel.crouchThigh + playerModel.sitThigh, 0, 0)
                }
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                }
                // t377/t452 右护腿-大腿段（装备槽 2）：镜像左大腿护腿（t452 放大；小腿段见 rightKneePivot）。
                //   t498 绑定改表达式形式（见头盔注）。
                Model {
                    id: playerArmorLegR
                    property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(2) : 0
                    visible: armId !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.34, 0.34, 0.34)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorLegR.armId); opacity: playerModel.bodyOpacity }
                }
                Node {
                    id: rightKneePivot
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(playerModel.crouchKnee + playerModel.sitKnee, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.hurtTint(playerModel.hurt, 0.227, 0.227, 0.353); opacity: playerModel.bodyOpacity }
                    }
                    // t452 右护腿-小腿段（装备槽 2）：镜像左小腿护腿（随小腿 / 蹲下弯折；与右大腿段共享槽 2）。
                    //   t498 绑定改表达式形式（见头盔注）。
                    Model {
                        id: playerArmorCalfR
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(2) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.34, 0.34, 0.34)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorCalfR.armId); opacity: playerModel.bodyOpacity }
                    }
                    // t377/t452 右靴（装备槽 3）：镜像左靴（t452 放大 + 前移成靴头；随小腿摆动）。
                    //   t498 绑定改表达式形式（见头盔注）。
                    Model {
                        id: playerArmorBootR
                        property int armId: hotbarVM.armorRevision >= 0 ? hotbarVM.armorBlockIdAt(3) : 0
                        visible: armId !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.24, -0.03)
                        scale: Qt.vector3d(0.34, 0.14, 0.38)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: playerModel.armorMatColor(playerArmorBootR.armId); opacity: playerModel.bodyOpacity }
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

        // t385 天气降水粒子（雨 / 雪覆盖层）：WeatherParticles.qml 经 Loader 动态加载（同 particleLoader /
        //   smokeLoader 的 Particles3D 隔离模式 — 模块运行期缺失时仅本 Loader 失败 + 显式告警，Main.qml 仍
        //   正常加载，§2-E「保持运行而非崩溃，且不静默吞」）。WeatherParticles.qml 内单 ParticleSystem3D +
        //   雨 / 雪两套 emitter（emitRate 据 weatherStateAt 群系解析的局部降水类型切换；沙漠→无、山地→雪、
        //   草原/森林→雨）。Node 跟随玩家眼位（粒子云始终笼罩玩家）。
        //   分层（PLAN §2）：天气态由 World(Game 层) 算（weatherStateAt），呈现层只读消费、绝不反向写。
        Loader {
            id: weatherLoader
            active: true
            source: "WeatherParticles.qml"
            onLoaded: {
                // 领养进同一个 particlesHost 锚点（复用既有 3D 场景节点；否则 Loader 加载到的 Node parent=null
                // → 孤儿 → 不渲染，t16 同族坑）。
                weatherLoader.item.parent = particlesHost
                // 注入 World + PlayerController（WeatherParticles 据此查天气态 + 跟随眼位）。
                weatherLoader.item.world = theWorld
                weatherLoader.item.player = player
                console.info("[t385] WeatherParticles adopted into scene graph")
            }
            onStatusChanged: {
                if (status === Loader.Ready)
                    console.info("[t385] WeatherParticles Loader status = Ready")
                else if (status === Loader.Error)
                    console.warn("[t385] WeatherParticles Loader status = Error — Particles3D 运行期不可用，天气粒子已降级关闭（§2-E）")
            }
        }

        // t390 环境点缀粒子（雨溅 / 叶飘 / 火把火星）：AmbientParticles.qml 经 Loader 动态加载（同
        //   particleLoader / smokeLoader / weatherLoader 的 Particles3D 隔离模式 — 模块运行期缺失时仅本
        //   Loader 失败 + 显式告警，Main.qml 仍正常加载，§2-E「保持运行而非崩溃，且不静默吞」）。
        //   分层（PLAN §2）：呈现层只读消费 World 天气态 / 群系（weatherStateAt / biomeIdAt）+ torchPositions，
        //   绝不反向写栅格。雨溅联动 t385 天气（降水态玩家脚边水花）；叶飘仅在森林群系；火把火星复用
        //   torchPositions（与 TorchSmoke 烟雾互补）。粒子量克制（spec「不抢戏」）。
        Loader {
            id: ambientLoader
            active: true
            source: "AmbientParticles.qml"
            onLoaded: {
                // 领养进同一个 particlesHost 锚点（复用既有 3D 场景节点；否则 Loader 加载到的 Node parent=null
                //   → 孤儿 → 不渲染，t16 同族坑）。
                ambientLoader.item.parent = particlesHost
                // 注入 World + PlayerController + torchPositions（AmbientParticles 据此查天气 / 群系 + 跟随眼位 + 火把位置）。
                ambientLoader.item.world = theWorld
                ambientLoader.item.player = player
                ambientLoader.item.torchModel = torchPositions
                console.info("[t390] AmbientParticles adopted into scene graph; torches=" + torchPositions.count)
            }
            onStatusChanged: {
                if (status === Loader.Ready)
                    console.info("[t390] AmbientParticles Loader status = Ready")
                else if (status === Loader.Error)
                    console.warn("[t390] AmbientParticles Loader status = Error — Particles3D 运行期不可用，环境粒子已降级关闭（§2-E）")
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
                    visible: { const _r = itemEntities.revision; return _r >= 0 ? (itemEntities.aliveAt(index)) : false }
                    // 基准位置 + 物品 id + count：触碰 itemEntities.revision（Q_PROPERTY NOTIFY=entitiesChanged）
                    // 建立依赖。t36 removeAt 用 releaseSlot（标空，slot 稳定不 shift），revision 自增 → 本绑定
                    // 重算 → delegate[k] 对齐 slot[k] 的 pos/itemId/count。外层 Node 持基准 pos + 绕 Y 旋转。
                    // t64 加 count 触碰：部分拾取后 setCountAt bump revision → 数量重算。
                    id: entRoot
                    position: { const _r = itemEntities.revision; return _r >= 0 ? (itemEntities.posAt(index)) : Qt.vector3d(0, 0, 0) }
                    property int entId: { const _r = itemEntities.revision; return _r >= 0 ? (itemEntities.itemIdAt(index)) : 0 }
                    property int entCount: { const _r = itemEntities.revision; return _r >= 0 ? (itemEntities.countAt(index)) : 0 }
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
                        visible: entRoot.entId !== 13 && !hotbarVM.isPartialBlock(entRoot.entId) && !hotbarVM.isCrossBlock(entRoot.entId) && !hotbarVM.isBed(entRoot.entId) && !hotbarVM.isTool(entRoot.entId) && !hotbarVM.isMaterial(entRoot.entId)
                        geometry: BlockCube { blockId: entRoot.entId }
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: terrainLight(worldClock.skyLight)
                            alphaCutoff: 0.0   // 火把（id 13）/ 异形段（isPartialBlock）/ cross 段（isCrossBlock）/ 床段（isBed，t496）走下方 billboard 分支
                        }
                    }
                    // t219 木板衍生方块掉落实体：异形段（台阶/楼梯/栅栏/压力板/门/活板门）非整立方 → BillboardQuad
                    //   平图标（dimetric 立体图标 icon_wood_*.png），非 BlockCube 满格木板立方（异形各面 tile=planks
                    //   → BlockCube 渲成「一块木板」与木板不可辨）。机制同火把 / 材料段掉落 billboard（朝相机单面 +Z）：
                    //   本 Model 是 entRoot（绕 Y 自转 rotY）子节点，本地 yaw 减 rotY 抵消继承 → 世界旋转 = 相机旋转
                    //   → +Z 恒指回相机、正面恒可见。scale 0.3（同方块段 / 材料段掉落物统一）；baseColor 乘
                    //   terrainLight(skyLight) 夜间变暗（同方块段）；alphaCutoff:0.5 + opacity:0.99 沿用 alpha-test 契约。
                    //   Texture inline 读 per-entity entId（每个掉落物各显示自己的异形图标）。
                    // t496 二轮复盘 床掉落亦走本 billboard 分支（bed 图标而非满格被面色立方）。
                    Model {
                        visible: hotbarVM.isPartialBlock(entRoot.entId) || hotbarVM.isCrossBlock(entRoot.entId) || hotbarVM.isBed(entRoot.entId)
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
                                source: hotbarVM.iconSourceForBlock(entRoot.entId)   // t440：cross 段（花/蘑菇/睡莲/树苗…）flat 图标同此（icon_*.png 透明底）；t496 床段返染色 bed 图标
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
                            //   t472 钻石镐 #4fd9d2 = (79,217,210)（采掘 Obsidian 的唯一工具）。
                            baseColor: {
                                const m = worldClock.skyLight
                                const t = hotbarVM.toolTier(entRoot.entId)
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金镐金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜镐铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m)
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金工具金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜工具铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m) // t589 钻石青绿
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金工具金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜工具铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m) // t589 钻石青绿
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金工具金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜工具铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m) // t589 钻石青绿
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金工具金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜工具铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m) // t589 钻石青绿
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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
                                return t === 5 ? tintBySkyLight(242/255, 200/255, 50/255, m)  // t557 金工具金黄
                                         : t === 6 ? tintBySkyLight(200/255, 120/255, 80/255, m) // t557 铜工具铜橙
                                         : t === 4 ? tintBySkyLight(79/255, 217/255, 210/255, m) // t589 钻石青绿
                                         : t === 3 ? tintBySkyLight(216/255, 216/255, 230/255, m)
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

        // t402 经验球渲染（XpOrbManager 的发光小球实体）：Repeater 父节点 = 场景内 3D Node（xpOrbHost）
        //   → delegate 被 reparent 进 3D 场景图（同 itemHost / mobHost 模式；lessons-learned「动态 3D 对象
        //   必须挂到场景 Node，否则孤儿不渲染」）。slot-reuse：count 单调不降 → 空槽 delegate visible=false
        //   隐藏不销毁（同 itemHost 族；lessons-learned t170/t256）。
        // 触发：xpOrbs.count 随 spawnOrb 自增（NOTIFY entitiesChanged）→ Repeater 追加 delegate。位置随
        //   磁吸 bump revision → {revision; posAt} 绑定重算（呈现层只读消费，绝不反向写；PLAN §2 分层）。
        // 外观：纯色发光小球（PrincipledMaterial.NoLighting + 绿色 baseColor；§9a 自绘纯色，无 MC 资产）。
        //   上下浮动 + 缩放呼吸由呈现层自发（不反向写数据）；amountAt 驱动颜色深浅（大球更显眼）。
        Node {
            id: xpOrbHost
            Component.onCompleted: {
                console.info("[t402] xpOrbHost UP parent=" + xpOrbHost.parent + " (须为 3D Node 非 null)")
            }

            Repeater {
                model: xpOrbs.count
                delegate: Node {
                    visible: { const _r = xpOrbs.revision; return _r >= 0 ? (xpOrbs.aliveAt(index)) : false }
                    id: orbRoot
                    position: { const _r = xpOrbs.revision; return _r >= 0 ? (xpOrbs.posAt(index)) : Qt.vector3d(0, 0, 0) }
                    property int orbAmount: { const _r = xpOrbs.revision; return _r >= 0 ? (xpOrbs.amountAt(index)) : 0 }
                    property real bobY: 0
                    property real pulse: 1.0   // 缩放呼吸（0.85..1.15）

                    Component.onCompleted: {
                        if (parent === null) parent = xpOrbHost
                    }

                    // 经验球本体：纯色发光小球（NoLighting 必备 —— lit 材质在本 D3D11 后端不渲染，
                    //   lessons-learned「所有可见 Model 必须用 NoLighting」）。绿色 baseColor；amount 大 →
                    //   更亮（黄绿）凸显。scale ~0.18 + bob + pulse 呼吸（呈现层自发动画）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, orbRoot.bobY, 0)
                        scale: Qt.vector3d(0.18 * orbRoot.pulse, 0.18 * orbRoot.pulse, 0.18 * orbRoot.pulse)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // 大额球更亮黄（机制等价 MC 大经验球更显眼）；小额偏深绿。
                            baseColor: orbRoot.orbAmount >= 5 ? "#b8e635" : "#7fd13b"
                        }
                    }
                    // 上下浮动 0.12 格（~1.4s 周期；比掉落物快 = 经验球活泼感）。
                    SequentialAnimation on bobY {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0; to: 0.12; duration: 700; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 0.12; to: 0; duration: 700; easing.type: Easing.InOutSine }
                    }
                    // 缩放呼吸（0.85..1.15，~1s 周期）= 发光脉动感。
                    SequentialAnimation on pulse {
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.85; to: 1.15; duration: 500; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.15; to: 0.85; duration: 500; easing.type: Easing.InOutSine }
                    }
                }
            }
        }

        // t469 船渲染（BoatManager 的船实体）：Repeater 父节点 = 场景内 3D Node（boatHost）→ delegate 被
        //   reparent 进 3D 场景图（同 itemHost / mobHost / xpOrbHost 模式；lessons-learned「动态 3D 对象必须挂到
        //   场景 Node，否则孤儿不渲染」）。slot-reuse：count 单调不降 → 空槽 delegate visible=false 隐藏不销毁
        //   （同 itemHost 族；lessons-learned t170/t256）。
        // 触发：boats.count 随 spawnBoat 自增（NOTIFY entitiesChanged）→ Repeater 追加 delegate。位置 / 朝向随
        //   浮水 / 骑乘操控 bump revision → {revision; posAt / yawAt} 绑定重算（呈现层只读消费，绝不反向写；PLAN §2）。
        // 外观（t508 重做碗形船体 + 真木板贴图；§9a 原创程序几何，无 MC 资产）：原模型仅平底舱 + 两端翘块，呈
        //   U 形（左右无舷）+ 无贴图（纯色）→ 用户报「碗形错 / 贴图错」。改「碗形」= 1 块封闭底板（船底，挡水
        //   不漏）+ 4 面等高船舷壁（前后左右整圈上凸、中间凹下成舱），即「四面凸中间凹」的方碗。贴图用 BlockCube
        //   按木方面查图集（Oak→Planks 浅木 / Spruce→SprucePlanks 深木），复用既有 atlas 半纹素内缩防渗色。
        //   NoLighting 必备（可见 Model 红线；lit 材质在本 D3D11 后端不渲染）。
        Node {
            id: boatHost
            Component.onCompleted: {
                console.info("[t469] boatHost UP parent=" + boatHost.parent + " (须为 3D Node 非 null)")
            }

            Repeater {
                model: boats.count
                delegate: Node {
                    // t556：visible/position/boatYaw/bt 从语句块形式 `{ const _r = boats.revision; return ... }` 改
                    //   **表达式形式** `boats.revision >= 0 ? ... : fallback`（lessons-learned t498：NOTIFY 属性须**参与值
                    //   计算**才可靠注册依赖；语句块形式在静态构建节点上会静默漏注册 → 属性恒初值。boat delegate 虽是
                    //   Repeater 动态创建、高频 revision 下旧形式「看似工作」，但分色 bt 属低频敏感量 → 统一改表达式形式
                    //   绝后患）。`boats.Spruce` 实例作用域枚举同样不可靠（t556 根因之一）→ 改类型作用域 BoatManager.Spruce。
                    visible: boats.revision >= 0 ? boats.aliveAt(index) : false
                    id: boatRoot
                    // 船中心位（C++ 浮水 / 骑乘操控写入；呈现层只读）。绕 Y 转船头朝向（yawAt）。
                    position: boats.revision >= 0 ? boats.posAt(index) : Qt.vector3d(0, 0, 0)
                    // 船头朝向（度；先读进 property，再喂 eulerRotation —— 块表达式不能作函数实参）。
                    property real boatYaw: boats.revision >= 0 ? boats.yawAt(index) : 0
                    eulerRotation: Qt.vector3d(0, boatRoot.boatYaw, 0)
                    // 变体（Oak→Planks 浅木 / Spruce→SprucePlanks 深木；§9a 原创贴图，区别于 MC 资产）。
                    //   btBlockId 给 BlockCube 按方块查图集瓦片序号 → 每面铺整张木纹 tile（半纹素内缩防渗色）。
                    property int bt: boats.revision >= 0 ? boats.boatTypeAt(index) : 0
                    property int btBlockId: boatRoot.bt === BoatManager.Spruce ? 86 /*SprucePlanks*/ : 6 /*Planks*/

                    Component.onCompleted: {
                        if (parent === null) parent = boatHost
                    }

                    // t508 碗形船体（spec「碗形 = 四面凸中间凹」；§9a 原创程序几何 BlockCube 组合，参考船轮廓但
                    //   原创，无 MC 资产）。原模型 U 形（仅前后凸、左右无舷）+ 纯色无贴图 → 用户报错。重做「方碗」：
                    //   1 块封闭底板（船底甲板，沉到水面略下挡水不漏）+ 4 面等高舷壁（前后左右整圈，构成碗沿），
                    //   中间（4 壁之间）凹下成舱 = 「四面凸中间凹」的方碗。
                    //   坐标约定：长轴 Z（船头 = -Z 前，eulerRotation.y=boatYaw 对齐行进方向）、宽轴 X、高 Y。
                    //   t556「碰撞箱太大 / 船 4 角闪烁」（用户报③⑥）：(a) 船体总尺寸从 长 1.6×宽 1.0×高 ~0.65 缩到
                    //     长 1.4×宽 1.0×高 ~0.5（舷顶 0.25 / 舱底 -0.15）→ 与 kBoatHalfLen=0.7 / kBoatHalfW=0.5 /
                    //     kBoatHalfH=0.35 对齐（footprint 1.4×1.0，碰撞盒匹配船体，非整格大）。
                    //     (b) 四角闪烁根因 = 旧版横壁跨满宽（x∈[-0.5,0.5]）+ 舷壁全长 1.6 → 四角（舷壁段 × 横壁段）
                    //     两块同材质立方体**空间重叠** → 深度测试交替 → 闪烁。改「横壁跨满宽 + 舷壁只嵌中间（长 1.0）」：
                    //     四角只被横壁覆盖、舷壁端面与横壁内侧背对背共面 → 无重叠无缝隙（各 Model 摆位见下方）。所有块 NoLighting 必备。
                    //   贴图：BlockCube 按木方面查图集 → Planks(6) 橡木 / SprucePlanks(86) 云杉；baseColorMap = voxelAtlas
                    //   （item entity / 手持方块同源；半纹素内缩防渗色）。无 world → BlockCube 顶点色恒白（全亮，
                    //   船不被地形光场调制，与天光无关 —— 船是实体非地形块）。vertexColorsEnabled 不开（恒白顶点色
                    //   × baseColor × 贴图 = 贴图本色 × baseColor；baseColor 取白色免二次调制）。
                    //
                    // 船底甲板（封闭整底）：宽 1.0 × 高 0.1 × 长 1.4（t556 长 1.6→1.4 匹配碰撞盒），沉到水面略下
                    //   （吃水 -0.1）。船的「碗底」：封闭整面挡住下方水，水不从船舱中间漏上来（spec「船中间不要显示水」）。
                    //   同时是骑乘玩家的「甲板」。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(1.0, 0.1, 1.4)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // t533 船舱内封底（spec「船内有水」用户报：船凹下去水显示在里面，应没水 —— 船体不透明阻水）。
                    //   根因：船中心贴水面顶（kBoatDraft=0 → 船中心 Y = 水面 Y）。水段网格在此 cell 顶部画水面面
                    //   （Y=船中心），从上俯视船舱开口 → 见到舱内水面贴图（透明 opacity 0.7）从舱底甲板（Y=-0.15，
                    //   在水面下）上方透出 → 肉眼「船里有水」。船底甲板在水面下挡不住「其上方的水面」（深度测试：
                    //   水面 Y=0 比甲板顶 Y=-0.10 离相机近 → 水面先入深度缓冲 → 甲板被水面遮，非反之）。
                    //   修：加一块不透明「舱内封底」紧贴水面之上（顶面 +0.03 > 水面 0 → 深度测试遮水面），尺寸略
                    //   小于舱内（X/Z 内缩 0.1 留舷壁厚度，不超出碗沿）→ 从上俯视只见封底木纹、不见水面。机制对齐
                    //   MC 船（船内为封闭甲板、无水可见；MC 用整船模型自遮，本工程用第二块板显式遮水面网格）。
                    //   t556：长 1.4→1.0（缩到新船头/尾横壁之间 z∈[-0.5,0.5]，端面与横壁内侧齐平 → 无内部重叠）。
                    //   NoLighting + 白 baseColor（同船底甲板：贴图本色、不被地形光场调制）。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(0, 0.0, 0)        // 中心贴水面：顶面 +0.025（水面之上）遮水面贴图
                        scale: Qt.vector3d(0.8, 0.05, 1.0)        // 内缩 0.1 留舷壁；薄板 0.05 高
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // 左舷壁（-X 纵长壁）：宽 0.1 × 高 0.4 × 长 1.0（t556 从 1.6 缩短：舷壁只嵌船头/船尾两横壁之间，
                    //   z∈[-0.5,0.5]；横壁跨满宽盖住四角 → 角部无重叠无缝隙），贴 -X 边。等高于前后舷 → 碗沿连续。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(-0.45, 0.05, 0)
                        scale: Qt.vector3d(0.1, 0.4, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // 右舷壁（+X 纵长壁）：贴 +X 边（与左舷对称）。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(0.45, 0.05, 0)
                        scale: Qt.vector3d(0.1, 0.4, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // 船头舷壁（-Z 端横壁，跨满宽 x∈[-0.5,0.5]）：t556 消除四角闪烁（⑥）—— 旧版横壁跨满宽 + 舷壁也
                    //   全长 1.6 → 四角（x∈[-0.5,-0.4] 或 [0.4,0.5] × z∈[-0.8,-0.6] 或 [0.6,0.8]）两块同材质立方体
                    //   **空间重叠** → 深度测试交替 → 闪烁。改「横壁跨满宽 + 舷壁只嵌中间（1.0 长）」→ 四角只被横壁
                    //   覆盖（舷壁端面与横壁内侧共面、背对背 → 无重叠、无缝隙）。等高于左右舷 → 碗沿四角闭合。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(0, 0.05, -0.6)
                        scale: Qt.vector3d(1.0, 0.4, 0.2)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // 船尾舷壁（+Z 端横壁；与船头对称）。
                    Model {
                        geometry: BlockCube { blockId: boatRoot.btBlockId }
                        position: Qt.vector3d(0, 0.05, 0.6)
                        scale: Qt.vector3d(1.0, 0.4, 0.2)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            baseColor: "#ffffff"
                        }
                    }
                    // t508 二轮复盘修「F3+B 船没有碰撞箱」（用户报⑤）：船是实体（BoatManager 命中盒 kBoatHalfW /
                    //   kBoatHalfH），但旧版 F3+B hitbox 只画玩家 / mob / 掉落物（Main.qml 各自 delegate），boatHost
                    //   Repeater 内未加 → 用户报「船不是实体」。补船 hitbox（WireCube ±0.5 居中 → scale = (2·半W, 2·半H, 2·半L)）
                    //   + 朝向棒（船头 -Z 方向，boatYaw 已在 boatRoot Node 继承）。同 mob hitbox 模式（PLAN §2-F F3 调试叠层）。
                    //   t556：kBoatHalfW=0.5 / kBoatHalfH=0.35 / kBoatHalfLen=0.7 → scale=(1.01, 0.71, 1.41) 对齐新碰撞盒。
                    Model {
                        visible: window.showHitboxes
                        geometry: WireCube {}
                        scale: Qt.vector3d(1.01, 0.71, 1.41) // 2·kBoatHalfW+0.01 / 2·kBoatHalfH+0.01 / 2·kBoatHalfLen+0.01（外扩 0.01 避面重叠）
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                    }
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        // 朝向棒：从船中心沿本地 -Z（船头）延伸 0.65（船头前伸辨识朝向）。
                        position: Qt.vector3d(0, 0, -0.325)
                        scale: Qt.vector3d(0.03, 0.03, 0.65)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                    }
                }
            }
        }

        // t565 矿车渲染（MinecartManager 的矿车实体；同 boatHost 模式）：Repeater 父节点 = 场景内 3D Node
        //   （cartHost）→ delegate 被 reparent 进 3D 场景图（同 itemHost / mobHost / boatHost 模式；
        //   lessons-learned「动态 3D 对象必须挂到场景 Node，否则孤儿不渲染」）。slot-reuse：count 单调不降 →
        //   空槽 delegate visible=false 隐藏不销毁（同 itemHost 族）。
        // 触发：carts.count 随 spawnCart 自增（NOTIFY entitiesChanged）→ Repeater 追加 delegate。位置 / 朝向随
        //   骑乘物理推进 bump revision → {revision; posAt / yawAt} 绑定重算（呈现层只读消费，绝不反向写；PLAN §2）。
        // 外观（§9a 原创程序几何 UnitCube 拼装，无 MC 资产）：斗形（四面车帮中间凹的敞口车斗）—— 1 块车底板
        //   （宽 0.8 × 长 0.9）+ 4 面车帮壁（前后左右整圈上凸），整体 ~0.9×0.3×1.0（X 宽 × Y 高 × Z 长，长轴沿
        //   行进方向 Z；与 kCartHalfW=0.45 / kCartHalfL=0.5 / kCartHalfH=0.45 命中盒 XZ 对齐）。NoLighting 必备
        //   （可见 Model 红线；lit 材质在本 D3D11 后端不渲染）。车斗常驻铁灰金属色（原创配色）。
        // t565 接入说明（审查修）：MinecartManager 本体 / PlayerController 矿车分支（放置 / 骑乘 / 行驶 / 下车）
        //   已存在，但 QML 未实例化 / 未绑 minecartManager 属性 / 无渲染 → m_minecartManager 恒 nullptr，
        //   所有矿车路径被守卫跳过。本段补齐呈现层接线（实例化 + 属性绑定 + 渲染 Repeater + cartBroken 路由）。
        Node {
            id: cartHost
            Component.onCompleted: {
                console.info("[t565] cartHost UP parent=" + cartHost.parent + " (须为 3D Node 非 null)")
            }

            Repeater {
                model: carts.count
                delegate: Node {
                    // lessons-learned t498/t556：NOTIFY 属性须以**表达式形式参与值计算**（`carts.revision >= 0 ? ... : fallback`）
                    //   才可靠注册依赖；语句块形式在节点上会静默漏注册 → 属性恒初值。统一表达式形式（同 boat delegate）。
                    visible: carts.revision >= 0 ? carts.aliveAt(index) : false
                    id: cartRoot
                    // 矿车中心位（C++ 骑乘物理写入；呈现层只读）。绕 Y 转车头朝向（yawAt；车头 = -Z）。
                    position: carts.revision >= 0 ? carts.posAt(index) : Qt.vector3d(0, 0, 0)
                    // 车头朝向（度；先读进 property 再喂 eulerRotation —— 块表达式不能作函数实参）。
                    property real cartYaw: carts.revision >= 0 ? carts.yawAt(index) : 0
                    eulerRotation: Qt.vector3d(0, cartRoot.cartYaw, 0)

                    Component.onCompleted: {
                        if (parent === null) parent = cartHost
                    }

                    // 车底板（封闭整底）：宽 0.8 × 高 0.06 × 长 0.9，中心下方（车斗底贴轨面 —— 矿车中心已在轨面上
                    //   kCartRideH=0.3，底板下沿 ~轨面 +0.03 不穿轨）。斗形「底」：封闭整面 + 骑乘玩家的「地板」。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.12, 0)
                        scale: Qt.vector3d(0.8, 0.06, 0.9)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#9a9a9a" }
                    }
                    // 左车帮（-X 纵长壁）：厚 0.08 × 高 0.24 × 长 0.9，贴 -X 边。斗形「帮」：四面整圈上凸中间凹。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.36, 0.03, 0)
                        scale: Qt.vector3d(0.08, 0.24, 0.9)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a8a8a" }
                    }
                    // 右车帮（+X 纵长壁；与左对称）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.36, 0.03, 0)
                        scale: Qt.vector3d(0.08, 0.24, 0.9)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a8a8a" }
                    }
                    // 车头帮（-Z 端横壁，跨满宽 x∈[-0.4,0.4]；盖住四角 → 与纵壁端面共面无重叠，同船 t556 消闪烁手法）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.03, -0.41)
                        scale: Qt.vector3d(0.8, 0.24, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a8a8a" }
                    }
                    // 车尾帮（+Z 端横壁；与车头对称）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.03, 0.41)
                        scale: Qt.vector3d(0.8, 0.24, 0.08)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#8a8a8a" }
                    }
                    // F3+B 矿车碰撞箱（同 boat hitbox 模式；PLAN §2-F F3 调试叠层）：
                    //   kCartHalfW=0.45 / kCartHalfH=0.45 / kCartHalfL=0.5 → scale=(2·半W, 2·半H, 2·半L)+0.01 外扩避面重叠。
                    Model {
                        visible: window.showHitboxes
                        geometry: WireCube {}
                        scale: Qt.vector3d(0.91, 0.91, 1.01)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                    }
                    // 朝向棒：从矿车中心沿本地 -Z（车头）延伸 0.5（辨识行进朝向）。
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0, -0.25)
                        scale: Qt.vector3d(0.03, 0.03, 0.5)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
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
                    id: mobDelegate
                    // t256 slot-reuse：实体移除（沙着地 / mob 死亡 / 跌出）改 releaseSlot 标空（不 erase）→
                    //   count 单调不降 → 本 Repeater 永不销毁 delegate（修掉落沙频繁 spawn/land 致 delegate
                    //   泄漏：reparent 后的 3D delegate count 减小不销毁，lessons-learned t170）。空槽 aliveAt=false
                    //   → 本 Node visible=false 隐藏整棵子树；slot 被复用时 aliveAt=true + revision bump → 重显
                    //   并重绑新实体数据。索引稳定（release 不 shift）→ delegate[index] 恒对齐 slot[index]。
                    visible: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.aliveAt(index)) : false }
                    // 触碰 revision 建立依赖（push 位移 / 重力下落 / t239 AI 行走 / 受击红闪 / 死亡移除
                    //   bump revision → 位置 / 配色 / kind / yaw 重算）。t117 FallingBlock 着地 releaseSlot 后
                    //   revision 自增 → delegate 对齐新 entity 数据（同 itemEntities delegate 模式）。
                    // t400 幼崽缩放（babyScaleAt：成体 1.0 / 幼崽 0.5）：Node scale 围绕原点（= 碰撞中心 pos）缩放
                    //   子模型 → 腿底（local y=-halfH）缩到 pos.y - s·halfH，比地面（pos.y - halfH）高 halfH·(1-s)
                    //   → 幼崽悬空。故 position.y 下移 mobHalfH·(1-s) 把腿底拉回地面（mobHalfH 定义于下方，QML 绑定
                    //   按名解析不依赖声明顺序）。revision bump（长大 baby→false）→ entBabyScale 重算 → 重缩 + 重定位。
                    property real entBabyScale: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.babyScaleAt(index)) : 0 }
                    position: {
                        const _r = entityManager.revision
                        const p = entityManager.posAt(index)
                        return _r >= 0 ? Qt.vector3d(p.x, p.y - mobHalfH * (1.0 - entBabyScale), p.z) : Qt.vector3d(0, 0, 0)
                    }
                    scale: Qt.vector3d(entBabyScale, entBabyScale, entBabyScale)
                    property int entKind: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.kindAt(index)) : 0 }
                    // t239 身体朝向：Mob 按 yawAt 转（模型本地 -Z 正对 AI 行走方向，与 player.yaw 同约定）；
                    //   FallingBlock（沙立方）对称 → 不转（bodyYaw=0）。子节点（Mob Model / F3+B 箭头）随之继承。
                    property real bodyYaw: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob ? entityManager.yawAt(index) : 0) : 0 }
                    // t449 死亡过渡：血归零 → dead=true（dying 态，C++ 冻结 AI/重力/攻击，延迟 ~500ms 才掉落 + 移除）。
                    //   本 delegate 据 deadAt 翻 true 的瞬间：① spawn 白烟（消散感）② 播侧倒旋转 ~90°（围绕身体前向
                    //   轴 = local Z，模型本地 -Z 朝行走方向，绕 Z 倒向侧边 = MC 式「侧倒」）。
                    //   deathTilt 由 deathFallAnim 推 0→90；slot 复用（新 mob 进空槽）时 entDead 翻 false → 即时归 0。
                    property real deathTilt: 0.0
                    property bool wasDead: false
                    property bool entDead: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entityManager.deadAt(index)) : false }
                    onEntDeadChanged: {
                        if (entDead && !wasDead) {
                            // 死亡起始：白烟 puff（复用 BlockParticles 的 Model+Timer 池，t465 模式）+ 启动侧倒动画。
                            //   烟源 = mob 碰撞中心（pos）；上飘 + 渐隐 = 消散感（机制等价 MC mob 倒地白烟）。
                            wasDead = true
                            if (particleLoader.item) {
                                const dp = entityManager.posAt(index)
                                particleLoader.item.burstDeathSmoke(dp.x, dp.y, dp.z)
                            }
                            deathFallAnim.start()
                        } else if (!entDead && wasDead) {
                            // slot 复用：上一任 mob 死后释放的槽被新 mob 占用 → 立即归位（新 mob 不应继承侧倒态）。
                            wasDead = false
                            deathFallAnim.stop()
                            mobDelegate.deathTilt = 0.0
                        }
                    }
                    // 侧倒动画：0 → 90°（300ms，ease-out 缓冲如「倒地砸下」；C++ deathTimer≈500ms 留尾段稳态躺地
                    //   + 烟雾消散，再 emit mobDied 掉落）。绕 Z（bodyYaw 已在 Y 朝向行走方向；Z 叠加 = 侧倒）。
                    NumberAnimation {
                        id: deathFallAnim
                        target: mobDelegate
                        property: "deathTilt"
                        from: 0; to: 90
                        duration: 300
                        easing.type: Easing.OutCubic
                    }
                    eulerRotation: Qt.vector3d(0, bodyYaw, deathTilt)
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
                    //   t490 PrimedTnt（引燃态 TNT）白闪脉冲：primed 实体（isPrimedAt）据 fuseProgressAt 驱动
                    //   白闪频率（fuse 将尽时加快，机制等价 MC TNT 引信将尽闪烁加快）+ 微缩 scale 0.98（机制等价
                    //   MC TNT 引燃收缩）。tntFlashPhase 由循环 NumberAnimation 推 0→1，duration 随 fuseProgress
                    //   递减缩短（progress 1=刚点燃慢闪 / 0=即将引爆快闪）。baseColor 在原色 ↔ 白色间 lerp by
                    //   sin(phase·π) 脉冲（参考 :5212 Stalker 蓄力发白 lerp 模式）。
                    Model {
                        id: fallingBlockModel
                        visible: entKind === EntityManager.FallingBlock
                        geometry: BlockCube {
                            blockId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.blockIdAt(index)) : 0 }
                            world: theWorld
                            worldPos: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.posAt(index)) : Qt.vector3d(0, 0, 0) }
                            sunDir: worldClock.sunDir
                            shadowsEnabled: window.shadowsEnabled
                            dayMul: window.skyDayMul  // R19 B6：昼夜天光乘子（仅乘天光分量；掉落沙夜间不压暗火把旁 block 光）
                        }
                        // t527 falling 雪层薄板渲染：积雪层下落实体（blockId==44=SnowLayer）按 state 缩放薄板高度
                        //   （1/8..1.0；保留层数 metadata 的可视化），区别于沙/圆石等满格立方。slabH=(state+1)/8
                        //   （state 0..7 = 1..8 层）；BlockCube geometry ±0.5 居中 → scale.y=slabH、position.y=-0.5+slabH/2
                        //   使板底贴 cell 底（entity pos = cell 中心，板跨 cell [y, y+slabH]）。满格（state 7）→ slabH=1
                        //   = 满格立方（机制对标 MC 雪层 8 层 ≈ 雪块）。顶点光 / 贴图复用（BlockCube 内部按 blockId 取
                        //   SnowLayer tile 57 = 冷白冰晶噪点，与 worldgen / 掉落物贴图一致）。
                        property int entBlockId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.blockIdAt(index)) : 0 }
                        property int entBlockState: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.blockStateAt(index)) : 0 }
                        property bool isSnowFall: entBlockId === 44
                        property real slabH: isSnowFall ? Math.max(1.0/8.0, Math.min(1.0, (entBlockState + 1) / 8.0)) : 1.0
                        position: Qt.vector3d(0.0, isSnowFall ? (-0.5 + slabH / 2.0) : 0.0, 0.0) // 薄板底贴 cell 底（非雪 0）
                        // t490 PrimedTnt 引燃收缩 scale 0.98（机制等价 MC TNT 引燃收缩）；雪层薄板按 slabH 缩放；其余 1.0。
                        property bool entPrimed: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.isPrimedAt(index)) : false }
                        property real entFuseProg: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.fuseProgressAt(index)) : 0 }
                        scale: {
                            if (entPrimed) return Qt.vector3d(0.98, 0.98, 0.98)          // PrimedTnt 引燃收缩
                            if (isSnowFall) return Qt.vector3d(1.0, slabH, 1.0)           // t527 雪层薄板（按层数缩放）
                            return Qt.vector3d(1.0, 1.0, 1.0)                              // 沙石等满格立方
                        }
                        // t490 白闪脉冲相位（0..1 循环）。仅 primed 实体跑动画（非 primed 静止 0 不影响 baseColor）。
                        //   t492 Bug C：原用 `NumberAnimation on tntFlashPhase` + running 绑定 + loops:1 onFinished 重启，
                        //   该组合在「单循环结束 running 翻 false 而 running:entPrimed 仍 true」的绑定竞争中 + delegate
                        //   创建/slot 复用瞬间不可靠（primed 进新槽 entPrimed 首读即 true 时 running 绑定未及触发 / slot 复用
                        //   false→true 转换被吞）→ 动画常不自启 → 白闪不显示。改走本代码库既定模式（同 deathFallAnim）：
                        //   独立 NumberAnimation（target/property 显式）+ onEntPrimedChanged 显式 start() + loops:Infinite
                        //   （duration 每循环重设即可变频率），不再依赖 running 绑定自启。
                        property real tntFlashPhase: 0.0
                        // t494 白闪亮端判定：sin(phase·π) > 0.5（相位近峰 0.5 邻域）→ 亮端（纯白，贴图 null）。
                        //   绑定 tntFlashPhase（动画推 phase → 本属性随之重算 → baseColorMap/baseColor 绑定切纯白）。
                        property bool entFlashBright: Math.sin(tntFlashPhase * Math.PI) > 0.5
                        // 循环动画推 phase 0→1；duration 随 fuseProgress 变（progress 1=刚点燃 800ms 慢周期 /
                        //   progress 0=即将引爆 120ms 快周期）。loops:Infinite + onFinished 每轮重设 duration（据当前
                        //   entFuseProg）→ 频率随 fuse 减少平滑加快。onEntPrimedChanged 显式 start/stop（primed 入→启、
                        //   slot 复用成非 primed→停），不依赖 running 绑定。
                        NumberAnimation {
                            id: tntFlashAnim
                            target: fallingBlockModel
                            property: "tntFlashPhase"
                            from: 0; to: 1
                            duration: 800
                            loops: Animation.Infinite
                            onFinished: {
                                // 每轮结束据当前 fuseProgress 重设 duration（下次循环生效，频率随 fuse 平滑加快）。
                                if (fallingBlockModel.entPrimed) {
                                    tntFlashAnim.duration = fallingBlockModel.tntFlashDuration()
                                }
                            }
                        }
                        // primed 翻转（spawn 进新槽 / slot 复用）显式启停动画：入 primed→设 duration + start（防御
                        //   running 绑定在 delegate 创建瞬间未触发）；离 primed（slot 复用成沙）→ stop + 归 0（baseColor
                        //   回原色，不留残白）。
                        onEntPrimedChanged: {
                            if (entPrimed) {
                                tntFlashAnim.duration = fallingBlockModel.tntFlashDuration()
                                tntFlashAnim.start()
                            } else {
                                tntFlashAnim.stop()
                                fallingBlockModel.tntFlashPhase = 0.0
                            }
                        }
                        // delegate 创建时若已 primed（count 增 → Repeater 新建 delegate 首读 entPrimed 即 true，无
                        //   false→true 变化事件）→ Component.onCompleted 显式 start（补 onEntPrimedChanged 覆盖不到的首建场景）。
                        Component.onCompleted: {
                            if (entPrimed) {
                                tntFlashAnim.duration = fallingBlockModel.tntFlashDuration()
                                tntFlashAnim.start()
                            }
                        }
                        // 据当前 fuseProgress 算脉冲周期（ms）：progress 1 → 800ms（慢，刚点燃）/ progress 0 → 120ms
                        //   （快，即将引爆），线性插值。fuseProgress>1（链式 fuse 略长）clamp 到 1。
                        function tntFlashDuration() {
                            const p = Math.max(0.0, Math.min(1.0, entFuseProg))
                            return 120 + (800 - 120) * p
                        }
                        // fuseProgress 变（revision bump）→ 重设动画 duration（下次循环生效，当前循环不中断）。
                        onEntFuseProgChanged: {
                            if (tntFlashAnim.running) tntFlashAnim.duration = tntFlashDuration()
                        }
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            // t494 白闪：primed 且近脉冲峰值（entFlashBright）→ baseColorMap=null + baseColor 纯白 →
                            //   **整格纯白**（遮掉 TNT 贴图，机制等价 MC primed TNT 闪白）；非峰期 → 图集 + 暗底 →
                            //   暗 TNT 贴图可见。暗↔白往复 = 白闪脉冲（明显闪烁，任何光照下都可见）。
                            baseColorMap: (fallingBlockModel.entPrimed && fallingBlockModel.entFlashBright) ? null : voxelAtlas
                            // 非 primed → 白（R19 B6：昼夜乘子已由 BlockCube dayMul 烘进顶点色天空分量；方块光时间不变）。
                            //   primed 时取消 vertexColorsEnabled（顶点色光场会让白闪暗化，pulse 视觉不纯）。
                            baseColor: {
                                const _r = entityManager.revision
                                if (_r >= 0 && !fallingBlockModel.entPrimed) return Qt.rgba(1.0, 1.0, 1.0, 1.0)
                                if (fallingBlockModel.entFlashBright) return Qt.rgba(1.0, 1.0, 1.0, 1.0) // 纯白（贴图已 null）
                                return Qt.rgba(0.25, 0.25, 0.25, 1.0) // 暗底（图集暗 TNT 贴图）
                            }
                            vertexColorsEnabled: !fallingBlockModel.entPrimed  // primed 白闪不叠顶点色光场
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
                    property int entMobType: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobTypeAt(index)) : 0 }
                    // t252/t293 碰撞箱尺寸（halfW/halfH）：C++ 按 mobType 设（t293 收紧贴合身体：pig/sheep
                    //   0.40/0.45、cow 0.40/0.50、敌对 0.30/0.90、spider 0.45/0.30、MobTest/FallingBlock 0.5）。
                    //   WireCube hitbox scale + 朝向棒长度读它们（旧版固定 1×1×1）。
                    property real mobHalfW: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.radiusAt(index)) : 0 }
                    property real mobHalfH: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.halfHeightAt(index)) : 0 }
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
                        if (entMobType === EntityManager.MobChicken) return 0.40 - mobHalfH // t398 Chicken 小型鸟（腿底 0.40）
                        if (entMobType === EntityManager.MobSquid) return 0.46 - mobHalfH // t399 Squid 触腕底 0.46（贴 collision 底面）
                        if (entMobType === EntityManager.MobWolf) return 0.42 - mobHalfH // t480 Wolf 犬科（腿底 0.42）
                        if (entMobType === EntityManager.MobOcelot) return 0.40 - mobHalfH // t481 Ocelot/Cat 猫科（腿底 0.40）
                        if (entMobType === EntityManager.MobSilverfish) return 0.15 - mobHalfH // t487 Silverfish 银鱼（腿底 0.15）
                        // t482/t483 防御造物：方块身 + 南瓜头堆叠 Model（不走 MobModel；局部原点 = 碰撞中心），
                        //   底部方块（腿/底雪块）底面须贴 collision 底面（= 地面）。底部方块 local y center = -halfH + 0.45
                        //   （0.45 = 底块半高）；mobModelYOff 把整组 Model 下移（halfH-0.45），使底块底面（-halfH-0.45...）
                        //   贴 collision 底。简化：golem 模型组自身用「position.y 已含 -halfH 偏移」（见下方 golem Model），
                        //   故 mobModelYOff = 0（模型组原点 = 碰撞中心，组内各块按碰撞中心定位）。返回 0 退化为「无偏移」。
                        if (entMobType === EntityManager.MobSnowGolem) return 0.0
                        if (entMobType === EntityManager.MobIronGolem) return 0.0
                        return 0.50 - mobHalfH                          // MobTest（UnitCube ±0.5）
                    }
                    // t400 求偶心形指示（spec 繁殖可观察反馈；机制等价 MC 1.0 love mode 心形粒子）：mob 处于求偶期
                    //   （inLoveAt=true）→ 头顶显一颗小红心（玩家喂食后即时见 → 确认求偶已触发，无此反馈则玩家不知
                    //   「喂成功了没」）。纯视觉、无碰撞；NoLighting（红线：可见 Model 必须 NoLighting）。心 = 小立方
                    //   45° Z 旋成菱形（近似心形剪影）。位置 = 碰撞顶面上方 ~0.45 格（mobHalfH + 0.45）；缩放 0.18
                    //   （小不挡视线）。仅 Mob + inLove 时 visible。静态（避免与 position 绑定冲突的动画；视觉够辨）。
                    Model {
                        visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entityManager.inLoveAt(index)) : false }
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, mobHalfH + 0.45, 0) // 头顶上方（local；Node 已在碰撞中心）
                        scale: Qt.vector3d(0.18, 0.18, 0.18)
                        eulerRotation.z: 45 // 菱形（心形近似剪影）
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#ff3a5a" // 求偶红心
                        }
                    }
                    // [perf] mob 类型特定块改为 Loader 门控：仅匹配 entMobType 的那一个 Loader 实例化其子树，
                    //   其余 mobType 的 Loader inactive（0 子节点）。旧版每槽无条件实例化全部 ~16 mob 类型块
                    //   （每槽 ~108 Model）× 64 槽(kCap) ≈ 8000 节点每帧 GUI 线程 scene-graph 同步（95% invisible
                    //   仍被遍历）→ 5 FPS 真凶。Loader active = 该块原 visible 条件；sourceComponent 包原块 verbatim
                    //   （不改块内任何代码/绑定/注释，块内 visible 行保留——Loader inactive 时本就不实例化，active 时
                    //   条件恒真亦无害）。onLoaded 领养：Loader 是 2D QQuickItem，加载出的 3D Node/Model 默认
                    //   parent=null（孤儿不渲染，lessons-learned t16）→ 显式领养进 delegate Node（mobDelegate）并入
                    //   3D 场景图（同 particleLoader 模式）。视觉/行为零变化（仅按 mobType 选择性实例化）。
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === 0
                        sourceComponent: Component {
                            Model {
                                // mobType 0：通用测试生物（t95/t239）—— UnitCube 单色立方（原创几何，§9 区隔
                                //   不照搬 MC 美术）。走 spawnMob 的 #ff5555。受击红闪：hurtFlashAt>0 → baseColor
                                //   #ff0000 覆盖。t280 燃烧：isBurningAt>0 → baseColor 偏橙（火焰色调制单色立方，
                                //   与下方 flame Model 共显「着火」感）。
                                //   t282：Shambler(4) / t287：Bones(5) 等其余 mob 已各自迁到专属 Loader（MobModel 人形 + 贴图）。
                                visible: entKind === EntityManager.Mob
                                         && entMobType === 0
                                geometry: UnitCube {}
                                position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: {
                                        const _r = entityManager.revision
                                        if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                        // t280 燃烧中 → 橙红偏色（与 flame Model 叠加显「着火」），否则走 mob 配色。
                                        if (_r >= 0 && entityManager.isBurningAt(index)) return "#ff7a3a"
                                        return _r >= 0 ? entityManager.colorAt(index) : "#000000"
                                    }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entMobType === EntityManager.MobSnowGolem) : false }
                        sourceComponent: Component {
                            // t482 雪傀儡（SnowGolem，mobType 12）：防御造物，南瓜头 + 雪块身堆叠（机制等价 MC 1.0 雪傀儡，
                            //   §9 区隔纯色原创非照搬 MC）。delegate 原点 = 碰撞中心（pos.y）；mobModelYOff=0 故组内各块按
                            //   碰撞中心定位（halfH=0.90 → feet local y=-0.90）。UnitCube + NoLighting（红线）。受击红闪 /
                            //   减速蓝调（isSlowedAt） / 昼夜灰阶（terrainLight）经 tinted() 函数统一驱动所有部件（base 色 × tint）。
                            //   t499 二轮复盘：golemSheared / tint 绑定改表达式形式（rev>=0 ? f() : fallback），lessons
                            //   t498「静态构建 + 低频 NOTIFY 用语句块 { rev; return f() } 会漏注册」的防御性写法（虽 Loader
                            //   动态创建 + revision 高频本应工作，改表达式形式绝后患 + 修 t499 一轮「受击红闪不闪」症状）。
                            Node {
                                visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entMobType === EntityManager.MobSnowGolem) : false }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                // t510 golemSheared = 是否已被剪刀剪掉南瓜头（shearSnowGolem → snowGolemShearedAt=true）。
                                //   剪后变无头 derpy 形态（机制等价 MC 1.0「剪后变无头形态带眼不死的 derpy 版」）：
                                //   南瓜头本体隐藏，眼/嘴保留贴原头位漂浮（不死，仅外观变化）。各部件 visible 据它切换。
                                //   t499 二轮复盘：表达式形式（rev>=0 ? ... : false）保 NOTIFY 依赖可靠注册（lessons t498）。
                                property bool golemSheared: entityManager.revision >= 0 ? entityManager.snowGolemShearedAt(index) : false
                                // tint = 当前调制色（红闪 / 蓝调 / 昼夜灰阶）；tinted(hex) 把部件 base 色按 tint 逐通道相乘。
                                //   t499 二轮复盘：表达式形式（rev>=0 ? 分流 : 兜底）保 hurtFlashAt（受击红闪）NOTIFY 可靠
                                //   触发 —— 修 t499 一轮「打雪傀儡无红闪」根因（语句块形式漏注册，damageEntity bump 了
                                //   revision 但 tint 绑定不重算 → 部件不转红）。hurtFlashAt>0 → 全红遮部件原色。
                                property color tint: entityManager.revision >= 0
                                    ? (entityManager.hurtFlashAt(index) > 0
                                        ? Qt.rgba(1.0, 0.0, 0.0, 1.0)
                                        : (entityManager.isSlowedAt(index)
                                            ? Qt.rgba(0.60, 0.72, 1.0, 1.0)
                                            : terrainLight(worldClock.skyLight)))
                                    : terrainLight(worldClock.skyLight)
                                function tinted(hex) {
                                    const b = Qt.color(hex)
                                    return Qt.rgba(b.r * tint.r, b.g * tint.g, b.b * tint.b, 1.0)
                                }
                                // feat 雪块身（雪傀儡身体）：MobModel mobType 12（柱身两雪块上下堆叠，几何内含底/顶雪块，
                                //   local 原点 = 碰撞中心；mobModelYOff=0 故 Model 在 (0,0,0)）。pack 命中 snow_golem.png →
                                //   packTextured=true（T 字 UV 展开进贴图）+ baseColorMap = pack 贴图；pack 关 → 全脸 UV +
                                //   纯色雪白（无程序生成贴图）。baseColor = tinted("#f0f4f8")（受击红闪 / 减速蓝调 / 昼夜灰阶调制
                                //   pack 贴图或纯色）。NoLighting（红线）。MobModel 作 Node 子节点 → 继承 bodyYaw（雪块身随
                                //   雪傀儡朝 AI 行走方向 -Z 转，机制上对称看不出，但与猪牛羊同路径一致）。
                                Model {
                                    geometry: MobModel {
                                        mobType: 12
                                        packTextured: mobSnowGolemPackTex.source.toString().length > 0
                                    }
                                    position: Qt.vector3d(0, 0, 0) // 碰撞中心（mobModelYOff=0；MobModel 局部原点同碰撞中心）
                                    scale: Qt.vector3d(1.0, 1.0, 1.0)
                                    materials: PrincipledMaterial {
                                        lighting: PrincipledMaterial.NoLighting
                                        baseColor: parent.tinted("#f0f4f8")
                                        baseColorMap: mobSnowGolemPackTex.source.toString().length > 0 ? mobSnowGolemPackTex : null
                                    }
                                }
                                // 南瓜头（机制等价 MC 1.0 雪傀儡戴刻面南瓜；§9 区隔纯色原创非照搬 MC）。
                                //   t582 修（用户「生成后头还是没有南瓜；头太大，要比中间身子小一截」）：
                                //   ① 头比身子小一截 —— t552 头宽 0.66 反而比顶雪块（0.60）宽，读作「头比身子大」；
                                //     改 **0.50**（MC 1.0 雪傀儡头 8×8×8 = 半格，比顶块 10px→0.60 小一截）→
                                //     头心 y = 顶雪块顶 0.90 + 半高 0.25 = 1.15，微沉 0.01 到 1.14（防 z-fight）。
                                //   ② 真南瓜贴图 —— 原纯色橙 UnitCube 读作「橙方块」非南瓜；改 BlockCube{blockId:100}
                                //     （南瓜方块）+ 共享图集 voxelAtlas：per-face 采 pumpkin_side/top/face 瓦片（-Z
                                //     前面 = 刻面双眼+锯齿嘴瓦片，随 bodyYaw 朝行走方向）→ pack 激用包内 HD 南瓜
                                //     三瓦片（t582 tileFilenameMap 117/118/119 接 pumpkin_side/face_off/top.png），
                                //     pack 关用程序生成 default_pumpkin_*.png —— 两种模式头都是「真南瓜」。
                                //   t510：剪南瓜头后（golemSheared）隐藏本体（无头 derpy 形态）；眼/嘴仍悬浮原头位。
                                Model {
                                    visible: !parent.golemSheared // t510 剪后隐藏南瓜头本体（无头形态）
                                    geometry: BlockCube { blockId: 100 } // 100 = BlockRegistry::Pumpkin（QML 不 import C++ 静态类故字面量，同 onMobDied 约定）
                                    position: Qt.vector3d(0, 1.14, 0)
                                    scale: Qt.vector3d(0.50, 0.50, 0.50)
                                    materials: PrincipledMaterial {
                                        lighting: PrincipledMaterial.NoLighting
                                        baseColorMap: voxelAtlas
                                        baseColor: parent.tinted("#ffffff") // 白=不额外染色，仅受击红闪/减速蓝调/昼夜灰阶调制南瓜瓦片
                                    }
                                }
                                // 南瓜头刻面双眼 + 嘴（机制等价 MC jack o'lantern 刻面：双眼 + 锯齿嘴）。
                                //   t582：南瓜头本体已带刻面贴图（BlockCube -Z 前面 = pumpkin_face 瓦片）→ 头在时
                                //   本组 overlay 隐藏（防「贴图脸 + 深色 overlay」双层脸）；仅 t510 剪后（golemSheared）
                                //   「无头 derpy 形态」显示 —— 眼/嘴脱空头位悬浮（机制等价 MC 1.0「剪后变无头形态
                                //   带眼不死的 derpy 版」）。位随 t582 新头位（头心 1.14 / xz 半 0.25）：z=-0.27 凸出
                                //   原头前面（原头前 z=-0.25）；眼位 y=1.19（头心上偏留嘴位）、嘴位 y=1.07（眼下）。
                                Model {
                                    visible: parent.golemSheared // t582：仅无头 derpy 形态显示（头在时由贴图脸承担）
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.13, 1.19, -0.27)
                                    scale: Qt.vector3d(0.10, 0.11, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                }
                                Model {
                                    visible: parent.golemSheared
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.13, 1.19, -0.27)
                                    scale: Qt.vector3d(0.10, 0.11, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                }
                                // 刻面嘴（横向长条，呈咧嘴笑剪影；机制等价 MC 南瓜嘴刻面）。仅 derpy 形态显示（同上）。
                                Model {
                                    visible: parent.golemSheared
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 1.07, -0.27)
                                    scale: Qt.vector3d(0.26, 0.06, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entMobType === EntityManager.MobIronGolem) : false }
                        sourceComponent: Component {
                            // t483 铁傀儡（IronGolem，mobType 13）：防御造物，南瓜头 + 铁块身（躯干 + 双腿 + 双臂）堆叠
                            //   （机制等价 MC 1.0 铁傀儡，§9 区隔纯色原创非照搬 MC）。halfH=1.20 → feet local y=-1.20。
                            //   UnitCube + NoLighting（红线）。受击红闪 / 减速蓝调 / 昼夜灰阶经 tinted() 统一驱动。
                            Node {
                                visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Mob && entMobType === EntityManager.MobIronGolem) : false }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                property color tint: {
                                    const _r = entityManager.revision
                                    if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return Qt.rgba(1.0, 0.0, 0.0, 1.0)
                                    if (entityManager.isSlowedAt(index)) return Qt.rgba(0.60, 0.72, 1.0, 1.0)
                                    return terrainLight(worldClock.skyLight)
                                }
                                function tinted(hex) {
                                    const b = Qt.color(hex)
                                    return Qt.rgba(b.r * tint.r, b.g * tint.g, b.b * tint.b, 1.0)
                                }
                                // feat 铁块身（铁傀儡身体）：MobModel mobType 13（铁块人形：宽躯干 + 双腿 + 双长臂，几何
                                //   内含 5 铁块盒，local 原点 = 碰撞中心；mobModelYOff=0 故 Model 在 (0,0,0)）。盒比例与原
                                //   t483 UnitCube 堆叠同（保南瓜头 / 眼 overlay 对齐）。pack 命中 iron_golem.png → packTextured=true
                                //   （T 字 UV 展开显铁纹）+ baseColorMap = pack 贴图；pack 关 → 全脸 UV + 纯色铁灰 #7d848c。
                                //   修 dev-plan C「铁傀儡全白」：程序纯色铁灰在用户视角读作「白」，pack iron_golem.png 铁纹才显
                                //   铁质。原 t483 锈斑 Model（铁灰 + 锈橙斑）在 pack 命中时由 pack 铁纹取代（锈纹已是贴图一部分），
                                //   pack 关时简化为纯色铁灰（无锈斑，纯色单材质无法表达锈斑）。baseColor = tinted("#7d848c")
                                //   （受击红闪 / 减速蓝调 / 昼夜灰阶调制 pack 贴图或纯色）。NoLighting（红线）。MobModel 作 Node
                                //   子节点 → 继承 bodyYaw（铁块身随铁傀儡朝 AI 行走方向 -Z 转）。
                                Model {
                                    geometry: MobModel {
                                        mobType: 13
                                        packTextured: mobIronGolemPackTex.source.toString().length > 0
                                    }
                                    position: Qt.vector3d(0, 0, 0) // 碰撞中心（mobModelYOff=0；MobModel 局部原点同碰撞中心）
                                    scale: Qt.vector3d(1.0, 1.0, 1.0)
                                    materials: PrincipledMaterial {
                                        lighting: PrincipledMaterial.NoLighting
                                        baseColor: parent.tinted("#7d848c")
                                        baseColorMap: mobIronGolemPackTex.source.toString().length > 0 ? mobIronGolemPackTex : null
                                    }
                                }
                                // 南瓜头（橙色）：local y center +0.95。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.95, 0)
                                    scale: Qt.vector3d(0.72, 0.66, 0.72)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: parent.tinted("#e8821e") }
                                }
                                // 南瓜头刻面双眼（深色小方块贴头前面 -Z）。t499 同 SnowGolem 修：眼 z=-0.38（凸出头前 0.02，
                                //   头 z scale 0.72 → 头前面 z=-0.36；旧 z=-0.34 在头内 0.02 → 被遮挡不可见）。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.14, 1.00, -0.38)
                                    scale: Qt.vector3d(0.09, 0.11, 0.03)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.14, 1.00, -0.38)
                                    scale: Qt.vector3d(0.09, 0.11, 0.03)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a0e04" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    // t371 燃烧火焰视觉（重做 t280/t344）：旧版「略大于 mob 的橙黄半透立方整块包覆」→ 读作
                    //   「橙色方块 / 放大火把」而非火焰。改为「贴身的动画火舌」—— 沿身体表面（collision 箱表面，
                    //   delegate 原点 = 箱中心，跨度 ±mobHalfW × ±mobHalfH）分布若干小火舌，每条复用 torch 三层焰
                    //   （外橙 #ff8a1a / 中黄 #ffd23c / 白心 #fff4c4，§9a 原创自绘），尺寸 ~0.13（贴身非包覆）。
                    //   各火舌独立闪烁（相位错开 → 火苗此起彼伏）→ 读作「身上窜动的火焰」。机制等价 MC 僵尸/骷髅
                    //   日光着火 + 动物触岩浆着火视觉。状态 = EntityManager.isBurningAt（t280 敌对日光 burning OR
                    //   t344 fireTimer>0 火烧；ALL mobs）。NoLighting（可见 Model 红线）。Repeater + 位置/相位表驱动
                    //   （复用 mobHost 同款 Repeater-for-3D，减重复；火焰随 delegate Node bodyYaw 转动贴身）。绑
                    //   revision 触碰 → 翻入/翻出 burning 时重算 visible。
                    Node {
                        id: mobBurnFlames
                        visible: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.isBurningAt(index)) : false }
                        // 火舌点表：[x, y, z, phaseIdx]，坐标为 delegate 本地框（collision 箱中心 = 原点，
                        //   身体 ±mobHalfW × ±mobHalfH）。phaseIdx 选相位（错开闪烁）。火焰贴身表面分布脚/腰/肩/顶。
                        // perf：非燃烧时 model=[] → 0 delegate（免 64 槽 × 7 = 448 火焰节点常驻 scene-graph 同步
                        //   + 448 个 loops:Infinite SequentialAnimation 每渲染帧推进 —— QML 动画 visible:false 不暂停，
                        //   revision 节流管不住它）。燃烧是稀有瞬态，toggle 时实例化/销毁 churn 可接受。
                        Repeater {
                            // t561 ② 修「白天着火火焰不显」：model 绑定原为裸 `isBurningAt(index) ? [...] : []` ——
                            //   纯 Q_INVOKABLE 方法调用不建 QML NOTIFY 依赖（lessons t498：返数组的函数调用当模型
                            //   不自动跟踪该类型 NOTIFY）→ 只在 delegate 创建瞬间求值一次、之后恒 [] → mob 翻入
                            //   燃烧后火焰永不出现（用户「火焰粒子不见了」）。修：显式触碰 entityManager.revision
                            //   （同 mobDelegate 其它绑定模式）→ 翻入/翻出 burning 时 revision bump → model 重算 →
                            //   火舌数组生成 / 清空。可见性（visible）已有 revision 依赖；model 一并补上才闭环。
                            model: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.isBurningAt(index) ? [
                                [0.0,      -mobHalfH * 0.65,  mobHalfW,        0],   // 脚前
                                [0.0,      -mobHalfH * 0.65, -mobHalfW,        1],   // 脚后
                                [0.0,       0.0,               mobHalfW,        2],   // 腰前
                                [mobHalfW,  0.0,               0.0,             3],   // 右腰
                                [-mobHalfW, 0.0,               0.0,             0],   // 左腰
                                [0.0,       mobHalfH * 0.65,  -mobHalfW,        1],   // 肩后
                                [0.0,       mobHalfH * 0.95,   0.0,             2]    // 头顶
                            ] : []) : [] }
                            delegate: Node {
                                position: Qt.vector3d(modelData[0], modelData[1], modelData[2])
                                // [lessons-learned] Repeater 创建的 3D delegate 默认 parent=null（孤儿不渲染），
                                //   onCompleted 显式 reparent 进 mobBurnFlames（同 mobHost / itemHost 模式）。
                                Component.onCompleted: if (parent === null) parent = mobBurnFlames
                                property real flickerS: 0.13
                                // 外焰（橙，最大；三层由大到小嵌套 → 渐变焰心，同 torch 焰模式）。
                                Model {
                                    geometry: UnitCube {}
                                    scale: Qt.vector3d(flickerS, flickerS * 1.20, flickerS)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff8a1a" }
                                }
                                // 中焰（黄）
                                Model {
                                    geometry: UnitCube {}
                                    scale: Qt.vector3d(flickerS * 0.62, flickerS * 0.74, flickerS * 0.62)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffd23c" }
                                }
                                // 焰心（暖白，最小最亮）
                                Model {
                                    geometry: UnitCube {}
                                    scale: Qt.vector3d(flickerS * 0.38, flickerS * 0.46, flickerS * 0.38)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#fff4c4" }
                                }
                                // 闪烁：flickerS 标量 SequentialAnimation（同 torch；本工具链无 Vector3DAnimation，
                                //   走 NumberAnimation on 标量 + scale 绑定）。duration 按 phaseIdx 偏移 → 各火舌
                                //   相位错开，读作「此起彼伏」而非整齐跳动。
                                SequentialAnimation on flickerS {
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 0.13; to: 0.17; duration: 110 + modelData[3] * 60 }
                                    NumberAnimation { from: 0.17; to: 0.11; duration: 150 + modelData[3] * 50 }
                                    NumberAnimation { from: 0.11; to: 0.15; duration: 90 }
                                    NumberAnimation { from: 0.15; to: 0.13; duration: 130 + modelData[3] * 40 }
                                }
                            }
                        }
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === 1
                        sourceComponent: Component {
                            Model {
                                // t240 猪（mobType 1）：MobModel 方块化原创模型 + mob_pig 贴图。
                                // t241 行走动画：walkPhase 绑定驱动 4 腿对角摆动（moveSpeed>0 时 EntityManager 每帧推进相位）。
                                visible: entKind === EntityManager.Mob && entMobType === 1
                                geometry: MobModel {
                                    mobType: 1
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（程序生成 mob_pig）。
                                    packTextured: mobPigPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面（halfH 变后免悬空 / 穿地）
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    // 受击红闪：hurtFlashAt>0 → baseColor=#ff0000 调制贴图全红（同 mobType 0 红闪语义）。
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // t421 pack 命中 → 切 pack entity 贴图；否则程序生成 mob_pig。
                                    baseColorMap: mobPigPackTex.source.toString().length > 0 ? mobPigPackTex : mobPigTex
                                }
                                // rv-low-batch1 眼睛恢复（回退 t555 的整删）：程序生成 mob 贴图是「全脸」身体纹（铺
                                //   每盒每面，无五官）→ pack 关时猪无眼显「怪」。恢复 t251 补的白眼底 + 深瞳子 Model，
                                //   但加 pack 感知 visible：pack 命中（mobPigPackTex.source 非空，url-guard 判空铁律）→
                                //   隐眼（pack 贴图自带猪脸五官，叠眼成「双层眼」）；pack 关 / 包内无映射 → 显眼。
                                //   NoLighting（红线）。眼作 mob Model 子节点 → 继承 bodyYaw + 父 visible。
                                //   猪 headPitch 恒 0 → 眼直接定位头前面（无需俯仰 Node）。位置 = MobModel 局部坐标：
                                //   头心 (0,0.05,-0.50) 半 (0.22,0.22,0.18) → 前面 z=-0.68，眼 y≈0.13、x=±0.10。
                                Model {
                                    visible: mobPigPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.10, 0.13, -0.68)
                                    scale: Qt.vector3d(0.08, 0.10, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                }
                                Model {
                                    visible: mobPigPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.10, 0.13, -0.68)
                                    scale: Qt.vector3d(0.08, 0.10, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                }
                                Model {
                                    visible: mobPigPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.10, 0.13, -0.69)
                                    scale: Qt.vector3d(0.04, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    visible: mobPigPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.10, 0.13, -0.69)
                                    scale: Qt.vector3d(0.04, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === 2
                        sourceComponent: Component {
                            Model {
                                // t240 牛（mobType 2）：MobModel + mob_cow 贴图（高大长身 + 头顶两小角盒）。
                                // t241 行走动画：walkPhase 绑定驱动腿摆（同猪）。
                                visible: entKind === EntityManager.Mob && entMobType === 2
                                geometry: MobModel {
                                    mobType: 2
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（程序生成 mob_cow）。
                                    packTextured: mobCowPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t252 cow halfH=0.70 → offset −0.20 腿底贴地
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // t421 pack 命中 → 切 pack entity 贴图；否则程序生成 mob_cow。
                                    baseColorMap: mobCowPackTex.source.toString().length > 0 ? mobCowPackTex : mobCowTex
                                }
                                // rv-low-batch1 眼睛恢复（同猪模式 + pack 感知 visible：pack 命中隐眼 / 关显眼）。
                                //   牛头心 (0,0.15,-0.60) 半 (0.20,0.22,0.20) → 前面 z=-0.80；眼 y≈0.22、x=±0.09。
                                Model {
                                    visible: mobCowPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.09, 0.22, -0.80)
                                    scale: Qt.vector3d(0.07, 0.09, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                }
                                Model {
                                    visible: mobCowPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.09, 0.22, -0.80)
                                    scale: Qt.vector3d(0.07, 0.09, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                                }
                                Model {
                                    visible: mobCowPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.09, 0.22, -0.81)
                                    scale: Qt.vector3d(0.035, 0.045, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    visible: mobCowPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.09, 0.22, -0.81)
                                    scale: Qt.vector3d(0.035, 0.045, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: {
                            const _r = entityManager.revision
                            return _r >= 0 && entKind === EntityManager.Mob && entMobType === 3
                                   && !entityManager.shearedAt(index)
                        }
                        sourceComponent: Component {
                            Model {
                                // t240 羊（mobType 3）毛茸态：MobModel + mob_sheep 贴图（圆胖躯干 + 小头 + 短腿）。
                                // t241 行走动画 + 吃草低头：walkPhase 驱动腿摆；headPitch 驱动头部俯仰（仅吃草周期内非零，
                                //   headPitchAt 据 eatTimer 返 sin(πp) 包络 → 低头→嚼→抬头；草丛在 C++ tick 内被消耗）。
                                // t300 剪羊毛态：shearedAt=false（未剪羊毛 / 已重新长毛）→ 显本毛茸贴图 Model；sheared=true
                                //   时切到下方裸肤色 Model（互斥 visible，由 revision 触碰刷新）。机制等价 MC 1.0 剪羊毛后
                                //   羊裸露皮肤。
                                visible: {
                                    const _r = entityManager.revision
                                    return _r >= 0 && entKind === EntityManager.Mob && entMobType === 3
                                           && !entityManager.shearedAt(index)
                                }
                                geometry: MobModel {
                                    mobType: 3
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（程序生成 mob_sheep）。
                                    packTextured: mobSheepPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                    headPitch: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.headPitchAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // t421 pack 命中 → 切 pack entity 贴图；否则程序生成 mob_sheep。
                                    baseColorMap: mobSheepPackTex.source.toString().length > 0 ? mobSheepPackTex : mobSheepTex
                                }
                                // rv-low-batch1 眼睛恢复（同猪/牛模式 + pack 感知 visible）。羊吃草时 MobModel 头绕
                                //   颈枢俯仰 → 眼放「颈枢 Node」（position=颈附着点 (0,0.10,-0.29)，eulerRotation.x 绑
                                //   headPitchAt）随头同步俯仰。眼相对颈枢：z=-0.32、y=0.06、x=±0.055（同裸态羊眼）。
                                Node {
                                    visible: mobSheepPackTex.source.toString().length === 0
                                    position: Qt.vector3d(0, 0.10, -0.29)
                                    property real headPitch: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.headPitchAt(index)) : 0 }
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
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: {
                            const _r = entityManager.revision
                            return _r >= 0 && entKind === EntityManager.Mob && entMobType === 3
                                   && entityManager.shearedAt(index)
                        }
                        sourceComponent: Component {
                            Model {
                                // t300 羊（mobType 3）裸态：剪羊毛后（shearedAt=true）的羊外观。复用 MobModel 几何（同
                                //   毛茸态四肢 + 头 + 躯干），但去贴图改裸肤色 #d6b890（机制等价 MC 1.0 剪羊毛后羊裸露
                                //   皮肤；t363 改肤色而非纯粉：贴近玩家手肤 + 略带残白羊毛，无 mob_sheep 毛茸贴图 → 直接
                                //   baseColor 实色渲染，受 terrainLight 调制保昼夜明暗 + hurtFlash 红闪仍生效）。与上方毛茸态
                                //   Model 互斥 visible（shearedAt 翻转 → 切换）。
                                //   walkPhase / headPitch 同步绑定 → 裸羊照常行走 + 吃草低头动画。
                                //   重长毛（C++ tick 内吃草方块 → sheared=false）→ 上方毛茸 Model 显、本 Model 隐。
                                visible: {
                                    const _r = entityManager.revision
                                    return _r >= 0 && entKind === EntityManager.Mob && entMobType === 3
                                           && entityManager.shearedAt(index)
                                }
                                geometry: MobModel {
                                    mobType: 3
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                    headPitch: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.headPitchAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t252 腿底贴 collision 底面（同毛茸态）
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    // 受击红闪覆盖肤色（同毛茸态红闪语义）；否则肤色 × terrainLight 调昼夜明暗。
                                    // t363 baseColor=肤色 #d6b890（玩家手肤 0.792/0.643/0.447=#caa472 略向白偏，留少量残白羊毛感，
                                    //   非猪粉 #e8b8b8）：剪羊毛后裸露的是肤色调而非纯粉，贴近玩家手肤、带一丝残白。
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : "#d6b890") : "#000000" }
                                    // 无 baseColorMap → PrincipaledMaterial 走纯 baseColor 实色路径（默认即无贴图）。
                                }
                                // 裸态眼同步（同毛茸态颈枢 Node 结构；复用 headPitchAt 绑头俯仰）。
                                Node {
                                    position: Qt.vector3d(0, 0.10, -0.29)
                                    property real headPitch: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.headPitchAt(index)) : 0 }
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
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobShambler
                        sourceComponent: Component {
                            Model {
                                // t282 蹒跚者（Shambler，mobType 4；机制等价 MC 1.0 僵尸，§9 改名 + 原创模型/贴图）：
                                //   MobModel 人形几何（躯干 + 头 + 双臂前伸僵尸姿态 + 双腿 walkPhase 摆动）+ mob_shambler 贴图。
                                //   近战 AI（detect→pathfind→attack，t281 已就绪）→ 走向玩家攻击；本任务仅交付原创模型 + 贴图。
                                //   walkPhase 绑定驱动双腿绕髋左右反相摆动（biped walk cycle，EntityManager moveSpeed>0 时推进）。
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobShambler
                                geometry: MobModel {
                                    mobType: 4
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（程序生成 mob_shambler）。
                                    packTextured: mobShamblerPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t282 halfH=0.90 → offset 0（腿底贴 collision 底面）
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    // 受击红闪：hurtFlashAt>0 → baseColor=#ff0000 调制贴图全红（同 mobType 0/1/2/3 红闪语义）。
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // t421 pack 命中 → 切 pack entity 贴图；否则程序生成 mob_shambler。
                                    baseColorMap: mobShamblerPackTex.source.toString().length > 0 ? mobShamblerPackTex : mobShamblerTex
                                }
                                // rv-low-batch1 亡灵红眼恢复（pack 感知 visible：pack 命中隐 / 关显）。不死亡灵的
                                //   赤红眼（实心红 #b01818 独立 Model，原创纯色 §9a）。MobModel 头心 (0,0.57,0) 半
                                //   (0.22,0.22,0.22) → 前面 z=-0.22；眼 y≈0.62、x=±0.09、z=-0.23（略凸防 z-fight）。
                                Model {
                                    visible: mobShamblerPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.09, 0.62, -0.23)
                                    scale: Qt.vector3d(0.07, 0.08, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#b01818" }
                                }
                                Model {
                                    visible: mobShamblerPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.09, 0.62, -0.23)
                                    scale: Qt.vector3d(0.07, 0.08, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#b01818" }
                                }
                                // t377 Shambler 随机护甲（4 部位；mobArmorAt 返护甲 id，0=无 → 隐）。作 mob Model 子节点 →
                                //   继承 bodyYaw + 父 visible。MobModel 局部坐标（头心 0.57 / 躯干心 0.05 / 腿底 -0.90）。
                                //   腿摆动烘焙在几何里 → 护腿 / 靴为静态盒（近似的视觉提示，~20% mob 偶遇可接受）。
                                //   tier 色 × terrainLight + 受击红闪（mobArmorColor）；NoLighting（红线）。
                                Model { // 头盔（piece 0）
                                    id: mobArmorHead
                                    property int armId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 0)) : 0 }
                                    visible: armId !== 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.66, 0); scale: Qt.vector3d(0.48, 0.30, 0.48)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorHead.armId) }
                                }
                                Model { // 胸甲（piece 1）
                                    id: mobArmorChest
                                    property int armId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 1)) : 0 }
                                    visible: armId !== 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.12, 0); scale: Qt.vector3d(0.48, 0.50, 0.30)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorChest.armId) }
                                }
                                // t560 护腿/靴（piece 2/3）随腿 walkPhase 摆动：旧版是静态整块盒（t377 注释「腿摆动
                                //   烘焙在几何里 → 护腿/靴为静态盒（近似的视觉提示）」）→ 用户「盔甲像固定没跟腿动画」。
                                //   修：分左/右两腿各作「髋枢 Node」子节点 —— 枢 y=−0.25 与 mobmodel.cpp mobType 4
                                //   （Shambler）腿枢 hipY 一致，eulerRotation.x = mobArmorLegSwingDeg(walkPhase, ±1) 与
                                //   MobModel 几何腿同幅同相（同一量化相位，见 mobArmorLegSwingDeg 注释）。盒位/尺寸沿用
                                //   旧静态盒（护腿整块 (0,-0.30,0)@(0.46,0.40,0.26) / 靴 (0,-0.82,0)@(0.46,0.16,0.26)）
                                //   按腿拆半到腿心 ±0.11、半宽 0.10（贴 Shambler 腿几何 half 0.11）。随枢旋转 → 腿摆时
                                //   盔甲同步摆动（不再固定）。NoLighting（红线）；tier 色×受击红闪同旧。
                                Node { // 左腿盔甲枢轴（髋 y=−0.25；腿心 x=−0.11）
                                    id: mobArmorLegPivotL
                                    property int legArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 2)) : 0 }
                                    property int bootArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 3)) : 0 }
                                    property real legSwing: { const _r = entityManager.revision; return _r >= 0 ? (mobArmorLegSwingDeg(entityManager.walkPhaseAt(index), 1)) : 0 }
                                    visible: legArmId !== 0 || bootArmId !== 0
                                    position: Qt.vector3d(-0.11, -0.25, 0)
                                    eulerRotation.x: legSwing
                                    Model { // 左护腿
                                        visible: parent.legArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.05, 0); scale: Qt.vector3d(0.20, 0.40, 0.26)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorLegPivotL.legArmId) }
                                    }
                                    Model { // 左靴
                                        visible: parent.bootArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.57, 0); scale: Qt.vector3d(0.20, 0.16, 0.26)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorLegPivotL.bootArmId) }
                                    }
                                }
                                Node { // 右腿盔甲枢轴（镜像；右腿摆角反相）
                                    id: mobArmorLegPivotR
                                    property int legArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 2)) : 0 }
                                    property int bootArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 3)) : 0 }
                                    property real legSwing: { const _r = entityManager.revision; return _r >= 0 ? (mobArmorLegSwingDeg(entityManager.walkPhaseAt(index), -1)) : 0 }
                                    visible: legArmId !== 0 || bootArmId !== 0
                                    position: Qt.vector3d(0.11, -0.25, 0)
                                    eulerRotation.x: legSwing
                                    Model { // 右护腿
                                        visible: parent.legArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.05, 0); scale: Qt.vector3d(0.20, 0.40, 0.26)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorLegPivotR.legArmId) }
                                    }
                                    Model { // 右靴
                                        visible: parent.bootArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.57, 0); scale: Qt.vector3d(0.20, 0.16, 0.26)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, mobArmorLegPivotR.bootArmId) }
                                    }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobStalker
                        sourceComponent: Component {
                            Model {
                                // t284 潜行者（Stalker，mobType 6；机制等价 MC 1.0 苦力怕，§9 改名 + 原创模型/纯色无贴图）：
                                //   MobModel 四短腿 + 高瘦躯干 + 小头（mobType 5）。近距蓄力 → 爆炸（C++ aiStalker 已就绪）。
                                //   walkPhase 绑定驱动四腿对角 walk cycle（EntityManager moveSpeed>0 时推进相位）。
                                //   蓄力膨胀：inflateAt(i) 驱动 Model scale（1+inflate·0.5，机制等价 MC 苦力怕近距蓄力膨胀）+
                                //     baseColor 蓄力发白（绿→白 lerp by inflate；机制等价 MC 苦力怕蓄力发白闪烁）。
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobStalker
                                property real inflate: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.inflateAt(index)) : 0 }
                                geometry: MobModel {
                                    mobType: 6
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（无贴图，纯色）。
                                    packTextured: mobStalkerPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0) // t284 halfH=0.90 → offset 0（腿底贴 collision 底面）
                                // 蓄力膨胀：scale 随 inflate 增长（0 → 1.0、满蓄力 → 1.5；机制等价 MC 苦力怕膨胀）。
                                scale: Qt.vector3d(1.0 + inflate * 0.5, 1.0 + inflate * 0.5, 1.0 + inflate * 0.5)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    // t421 pack 命中 → 切 pack entity 贴图（baseColor 仍作 tint 调制贴图：受击红 / 蓄力白）；
                                    //   否则 null（纯色，现状）。pack 关时 baseColor 即体色。
                                    baseColorMap: mobStalkerPackTex.source.toString().length > 0 ? mobStalkerPackTex : null
                                    // 受击红闪优先；否则青绿色（terrainLight 调昼夜暗），蓄力时 lerp 向白（蓄力发白）。
                                    baseColor: {
                                        const _r = entityManager.revision
                                        const tl = terrainLight(worldClock.skyLight)
                                        if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                        let r = 0.37, g = 0.66, b = 0.23 // Stalker 青绿色（呈现层视觉约定色，原创）
                                        const infl = entityManager.inflateAt(index)
                                        if (infl > 0) {
                                            const t = Math.min(1, infl)
                                            r = r * (1 - t) + 1.0 * t
                                            g = g * (1 - t) + 1.0 * t
                                            b = b * (1 - t) + 1.0 * t
                                        }
                                        return _r >= 0 ? Qt.rgba(r * tl.r, g * tl.g, b * tl.b, 1.0) : "#000000"
                                    }
                                }
                                // rv-low-batch1 深色眼恢复（pack 感知 visible）。MobModel 头心 (0,0.66,0) 半
                                //   (0.15,0.15,0.15) → 前面 z=-0.15；眼 y≈0.68、x=±0.06、z=-0.17（略凸防 z-fight）。
                                Model {
                                    visible: mobStalkerPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.06, 0.68, -0.17)
                                    scale: Qt.vector3d(0.05, 0.06, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    visible: mobStalkerPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.06, 0.68, -0.17)
                                    scale: Qt.vector3d(0.05, 0.06, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobBones
                        sourceComponent: Component {
                            // t287/t301/t331 Bones（骸骨/骷髅；mobType 5）：MobModel 瘦骨人形（窄躯干/细四肢/小头骨）。
                            //   灰白骨色 baseColor（无专属贴图，纯色原创 §9a）。受击红闪。远程射箭由 EntityManager 负责。
                            //   t331：弓 + 右臂移出 MobModel（单材质无法同几何双色）→ 见下方「肩枢 Node」：木色弓（MobBowGeometry，
                            //   修「弓误用骨白」）+ 右臂（骨白 UnitCube 共享 boneMat）随 drawAmount（aimTimer）抬起瞄准 + 弦后拉。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobBones
                                geometry: MobModel {
                                    mobType: 5
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（无贴图，纯色骨白）。
                                    packTextured: mobBonesPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                // boneMat 带 id：右臂（肩枢 Node 子节点）共享同一材质 → 受击红闪 + 昼夜灰阶与身体完全同步。
                                materials: PrincipledMaterial {
                                    id: boneMat
                                    lighting: PrincipledMaterial.NoLighting
                                    // t421 pack 命中 → 切 pack entity 贴图（baseColor 仍作 tint：受击红 / 昼夜灰阶）；否则 null（纯色）。
                                    baseColorMap: mobBonesPackTex.source.toString().length > 0 ? mobBonesPackTex : null
                                    baseColor: {
                                        const _r = entityManager.revision
                                        const tl = terrainLight(worldClock.skyLight)
                                        if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                        return _r >= 0 ? Qt.rgba(0.85 * tl.r, 0.84 * tl.g, 0.77 * tl.b, 1.0) : "#000000" // 灰白骨色（身体 + 右臂）
                                    }
                                }
                                // rv-low-batch1 黑色眼窝恢复（pack 感知 visible）。头骨空洞眼窝（纯黑 #1a1a1a，
                                //   §9a）。头骨心 (0,0.57,0) 半 (0.16,0.18,0.16) → 前面 z=-0.16；眼窝 y≈0.62、
                                //   x=±0.06、z=-0.17（略凸防 z-fight）。
                                Model {
                                    visible: mobBonesPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.06, 0.62, -0.17)
                                    scale: Qt.vector3d(0.06, 0.07, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    visible: mobBonesPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.06, 0.62, -0.17)
                                    scale: Qt.vector3d(0.06, 0.07, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                // t331 右臂 + 弓 肩枢 Node：drawAmount（EntityManager::drawAmountAt，aimTimer 驱动）抬起右臂瞄准。
                                //   臂与弓同处一 Node 绕肩枢刚体同转 → 抬臂时弓精确随臂移动（免错位）。肩枢 = 右臂根与躯干相接处
                                //   (0.20,0.28,-0.12)（MobModel 局部坐标；Node 继承 bodyYaw + 父 position）。drawAmount=0 → 臂/弓在
                                //   原持弓静态位（与 t301 MobModel 内建位一致）。机制等价 MC 1.0 骷髅停步抬弓瞄准。
                                Node {
                                    position: Qt.vector3d(0.20, 0.28, -0.12)
                                    eulerRotation.x: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.drawAmountAt(index) * 30) : 0 } // 度；+draw 前端（-Z）上扬
                                    // 右臂（骨白 UnitCube，共享 boneMat）：臂心相对肩枢 = (0,-0.05,-0.25)；半 (0.05,0.05,0.25)。
                                    Model {
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0.0, -0.05, -0.25)
                                        scale: Qt.vector3d(0.10, 0.10, 0.50)
                                        materials: boneMat
                                    }
                                    // 弓（木褐色 MobBowGeometry，独立于骨白体色；弦随 drawAmount 后拉 + 肢增弯）：握把相对肩枢 = (0.02,-0.06,-0.38)。
                                    Model {
                                        geometry: MobBowGeometry {
                                            drawAmount: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.drawAmountAt(index)) : 0 }
                                        }
                                        position: Qt.vector3d(0.02, -0.06, -0.38)
                                        materials: PrincipledMaterial {
                                            lighting: PrincipledMaterial.NoLighting
                                            baseColor: {
                                                const tl = terrainLight(worldClock.skyLight) // 昼夜灰阶（同身体；受击期暂持木色，短可接受）
                                                return Qt.rgba(0.42 * tl.r, 0.27 * tl.g, 0.15 * tl.b, 1.0) // 木褐色（修「弓误用骨白」）
                                            }
                                        }
                                    }
                                }
                                // t377 Bones 随机护甲（4 部位；同 Shambler，但 Bones 身形瘦 → 护甲盒按比例缩窄，贴骨身）。
                                //   作 mob Model 子节点继承 bodyYaw + 父 visible；MobModel 局部坐标（瘦躯干 half 0.14 / 细腿 0.06）。
                                //   ids 必须 main.qml 全局唯一 → Bones 段用 bonesArmor* 前缀（Shambler 段仍 mobArmor*）。
                                Model { // 头盔（piece 0）
                                    id: bonesArmorHead
                                    property int armId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 0)) : 0 }
                                    visible: armId !== 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.66, 0); scale: Qt.vector3d(0.36, 0.26, 0.36)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorHead.armId) }
                                }
                                Model { // 胸甲（piece 1）
                                    id: bonesArmorChest
                                    property int armId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 1)) : 0 }
                                    visible: armId !== 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.12, 0); scale: Qt.vector3d(0.34, 0.50, 0.24)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorChest.armId) }
                                }
                                // t560 护腿/靴（piece 2/3）随腿 walkPhase 摆动：同 Shambler 段 —— 分左/右两腿各作
                                //   髋枢 Node（枢 y=−0.25 = mobmodel.cpp mobType 5 腿枢 hipY），eulerRotation.x =
                                //   mobArmorLegSwingDeg(walkPhase, ±1) 与几何腿同幅同相。Bones 腿心 ±0.07 / 半宽 0.06
                                //   （瘦骨杆）→ 护甲盒按腿拆到 ±0.07、scale.x=0.14（旧整块 (0.30,*,0.2x) 拆半贴细腿）。
                                Node { // 左腿盔甲枢轴
                                    id: bonesArmorLegPivotL
                                    property int legArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 2)) : 0 }
                                    property int bootArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 3)) : 0 }
                                    property real legSwing: { const _r = entityManager.revision; return _r >= 0 ? (mobArmorLegSwingDeg(entityManager.walkPhaseAt(index), 1)) : 0 }
                                    visible: legArmId !== 0 || bootArmId !== 0
                                    position: Qt.vector3d(-0.07, -0.25, 0)
                                    eulerRotation.x: legSwing
                                    Model { // 左护腿
                                        visible: parent.legArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.05, 0); scale: Qt.vector3d(0.14, 0.40, 0.20)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorLegPivotL.legArmId) }
                                    }
                                    Model { // 左靴
                                        visible: parent.bootArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.57, 0); scale: Qt.vector3d(0.14, 0.16, 0.20)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorLegPivotL.bootArmId) }
                                    }
                                }
                                Node { // 右腿盔甲枢轴（镜像；摆角反相）
                                    id: bonesArmorLegPivotR
                                    property int legArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 2)) : 0 }
                                    property int bootArmId: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.mobArmorAt(index, 3)) : 0 }
                                    property real legSwing: { const _r = entityManager.revision; return _r >= 0 ? (mobArmorLegSwingDeg(entityManager.walkPhaseAt(index), -1)) : 0 }
                                    visible: legArmId !== 0 || bootArmId !== 0
                                    position: Qt.vector3d(0.07, -0.25, 0)
                                    eulerRotation.x: legSwing
                                    Model { // 右护腿
                                        visible: parent.legArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.05, 0); scale: Qt.vector3d(0.14, 0.40, 0.20)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorLegPivotR.legArmId) }
                                    }
                                    Model { // 右靴
                                        visible: parent.bootArmId !== 0
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, -0.57, 0); scale: Qt.vector3d(0.14, 0.16, 0.20)
                                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: mobArmorColor(index, bonesArmorLegPivotR.bootArmId) }
                                    }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobSpider
                        sourceComponent: Component {
                            // t285/t302 Spider（蜘蛛；mobType 7）：MobModel 宽矮躯干 + 前伸小头 + **8 腿**（原创 §9，4 对
                            //   沿躯干 Z 分布；t302 升级自 t285 简化 4 腿。爬墙留后续）。暗黑红 baseColor（纯色原创 §9a）。
                            //   受击红闪。hostile → EntityManager AI 自动追击玩家。t302 加 4 颗红眼（蜘蛛标志性，纯色子 Model）。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobSpider
                                geometry: MobModel {
                                    mobType: 7
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（无贴图，纯色暗黑红）。
                                    packTextured: mobSpiderPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    // t421 pack 命中 → 切 pack entity 贴图（baseColor 仍作 tint：受击红 / 昼夜暗）；否则 null（纯色）。
                                    baseColorMap: mobSpiderPackTex.source.toString().length > 0 ? mobSpiderPackTex : null
                                    baseColor: {
                                        const _r = entityManager.revision
                                        const tl = terrainLight(worldClock.skyLight)
                                        if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                        return _r >= 0 ? Qt.rgba(0.16 * tl.r, 0.10 * tl.g, 0.10 * tl.b, 1.0) : "#000000" // 暗黑红
                                    }
                                }
                                // rv-low-batch1 4 红眼恢复（pack 感知 visible；蜘蛛标志性 8 眼简化为 4 颗醒目红眼，
                                //   原创纯色 NoLighting §9a）。Spider 头心 (0,-0.02,-0.32) 半 (0.18,0.14,0.18) →
                                //   前面 z=-0.50；眼贴 z=-0.51（略凸防 z-fight）。4 颗分上下两对（y=+0.04 / -0.08；x=±0.07）。
                                Model {
                                    visible: mobSpiderPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.07, 0.04, -0.51)
                                    scale: Qt.vector3d(0.05, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                                }
                                Model {
                                    visible: mobSpiderPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.07, 0.04, -0.51)
                                    scale: Qt.vector3d(0.05, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                                }
                                Model {
                                    visible: mobSpiderPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.07, -0.08, -0.51)
                                    scale: Qt.vector3d(0.05, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                                }
                                Model {
                                    visible: mobSpiderPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.07, -0.08, -0.51)
                                    scale: Qt.vector3d(0.05, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff2020" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobSilverfish
                        sourceComponent: Component {
                            // t487 Silverfish（银鱼；mobType 14）：MobModel 小型虫几何（分节躯干 + 前伸小头 + 多对短腿；
                            //   机制等价 MC 1.0 银鱼，§9 原创模型 + 贴图）。hostile → EntityManager AI 自动追击玩家
                            //   （默认 aiHostile 近战追击，小体型快速）。银鱼刷怪笼（要塞，Spawner state 带
                            //   SpawnerStateSilverfishFlag）周期刷出。受击红闪。mob_silverfish 贴图（灰白甲壳 + 体节纹）。
                            //   mobModelYOff=0.15−halfH（腿底 0.15 贴 collision 底面）；halfH=0.15 → offset=0。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobSilverfish
                                geometry: MobModel {
                                    mobType: 14
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColorMap: mobSilverfishTex
                                    baseColor: {
                                        const _r = entityManager.revision
                                        const tl = terrainLight(worldClock.skyLight)
                                        if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                        return _r >= 0 ? tl : "#000000"
                                    }
                                }
                                // 银鱼眼（2 颗黑点；头前侧。MobModel 头心 (0,0.00,-0.24) 半 (0.14,0.11,0.10) → 前面 z=-0.34；
                                //   眼贴头前侧 z=-0.35（略凸出防与头面 z-fight，同 t52 贴脸）。受击红闪时身体变红 → 黑眼仍辨。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.05, 0.00, -0.35)
                                    scale: Qt.vector3d(0.03, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#101010" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.05, 0.00, -0.35)
                                    scale: Qt.vector3d(0.03, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#101010" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobChicken
                        sourceComponent: Component {
                            // t398 Chicken（鸡；mobType 8）：MobModel 小型鸟几何（圆胖躯干 + 前伸小头 + 后翘尾 + 2 细腿
                            //   biped walk cycle；机制等价 MC 1.0 鸡，§9 原创模型 + 贴图）。passive → EntityManager AI 走
                            //   aiWander（同猪/牛/羊）；周期性下蛋（chickenLaidEgg → onChickenLaidEgg 转发 spawnItem EGG）。
                            //   受击红闪。喙 / 鸡冠 / 肉垂为本 Model 子节点（纯色 NoLighting，同猪眼模式 —— 单材质无法同几何
                            //   双色，故头饰独立子节点继承 bodyYaw + visible）。MobModel 头心 (0,0.26,-0.18) 半 (0.11,0.12,0.11)。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobChicken
                                geometry: MobModel {
                                    mobType: 8
                                    // t421 pack 命中 entity 贴图 → T 字 UV 展开；否则全脸 UV（程序生成 mob_chicken）。
                                    packTextured: mobChickenPackTex.source.toString().length > 0
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // t421 pack 命中 → 切 pack entity 贴图；否则程序生成 mob_chicken。
                                    baseColorMap: mobChickenPackTex.source.toString().length > 0 ? mobChickenPackTex : mobChickenTex
                                }
                                // 喙（前伸尖嘴，橙黄；头前面 z=-0.29）。MobModel 头前面 = 头心 cz(-0.18) - hz(0.11) = -0.29。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.24, -0.32)
                                    scale: Qt.vector3d(0.04, 0.025, 0.06)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8a020" }
                                }
                                // 鸡冠（头顶红色小冠，3 颗粒状凸起；头心上方 y≈0.39）。机制等价 MC 鸡冠视觉。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0, 0.39, -0.16)
                                    scale: Qt.vector3d(0.05, 0.04, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#c83030" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.04, 0.39, -0.18)
                                    scale: Qt.vector3d(0.035, 0.035, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#c83030" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.04, 0.39, -0.18)
                                    scale: Qt.vector3d(0.035, 0.035, 0.04)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#c83030" }
                                }
                                // rv-low-batch1 黑眼恢复（pack 感知 visible；头两侧偏前 z=-0.27、y=0.27、x=±0.08）。
                                Model {
                                    visible: mobChickenPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.08, 0.27, -0.27)
                                    scale: Qt.vector3d(0.025, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    visible: mobChickenPackTex.source.toString().length === 0
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.08, 0.27, -0.27)
                                    scale: Qt.vector3d(0.025, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobSquid
                        sourceComponent: Component {
                            // t399 Squid（鱿鱼；mobType 9）：MobModel 水生软体几何（圆胖躯干 + 顶端尖 + 8 触腕；机制等价
                            //   MC 1.0 squid，§9 原创模型 + 贴图）。passive → EntityManager AI 走 aiSquid（水里喷水推进游动）；
                            //   死亡掉墨囊（onMobDied → spawnItem InkSacId）。受击红闪。眼为本 Model 子节点（纯色 NoLighting，
                            //   同鸡眼模式 —— 单材质无法同几何双色，故眼独立子节点继承 bodyYaw + visible）。MobModel 躯干心
                            //   (0,0.08,0) 半 (0.28,0.24,0.28) → 前面 z=-0.28；眼贴躯干前侧（z≈-0.29 略凸出防 z-fight）。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobSquid
                                geometry: MobModel {
                                    mobType: 9
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                position: Qt.vector3d(0, mobModelYOff, 0)
                                scale: Qt.vector3d(1.0, 1.0, 1.0)
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    baseColorMap: mobSquidTex
                                }
                                // 眼（2 颗黑点；躯干前侧偏前 z=-0.29、y=0.10、x=±0.10）。同鸡眼纯色子 Model 模式。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.10, 0.10, -0.29)
                                    scale: Qt.vector3d(0.03, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.10, 0.10, -0.29)
                                    scale: Qt.vector3d(0.03, 0.03, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobWolf
                        sourceComponent: Component {
                            // t480 Wolf（狼；mobType 10）：MobModel 犬科几何（细长躯干 + 尖头 + 立耳 + 4 腿）+ mob_wolf 贴图
                            //   （机制等价 MC 1.0 狼，§9 原创模型 + 贴图）。passive（hostile=false）→ 生命周期同被动生物；
                            //   未驯服走 aiWolf 敌对玩家（追击咬击）；驯服后跟随主人 + 防御主人目标（攻击 / 受击来源的 mob）。
                            //   受击红闪（同既有 hurtFlashAt>0 → baseColor 红模式）。尾巴为**独立子 Model**（spec「尾巴角度
                            //   示血量」）：绕尾根枢旋转 —— 满血竖起（~35°）、残血下垂（~140°），机制等价 MC 狼尾随血量升降。
                            //   坐姿（wolfSittingAt=true）→ 整个狼 Model 垂直压缩 + 后倾 + 略下沉（读作「坐地留守」，与站姿
                            //   明显区分）。眼为子节点（纯色 NoLighting，同猪眼模式）。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobWolf
                                property real wolfSit: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.wolfSittingAt(index) ? 1 : 0) : 0 }
                                geometry: MobModel {
                                    mobType: 10
                                    // 狼无 pack entity 贴图映射（同 Squid(9)，spec 未列 → 保程序生成 mob_wolf 全脸 UV）。
                                    packTextured: false
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                // t480 坐姿变换：坐 → 垂直压缩（1−0.22=0.78）+ 后倾（-18° 绕 X，鼻略抬）+ 略下沉 0.08 格 →
                                //   读作「坐地留守」；站 → 原比例 / 无倾 / 原高。wolfSit 绑 revision → toggleWolfSit 翻转即时切姿。
                                position: Qt.vector3d(0, mobModelYOff - wolfSit * 0.08, 0)
                                scale: Qt.vector3d(1.0, 1.0 - wolfSit * 0.22, 1.0)
                                eulerRotation.x: wolfSit * -18
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    baseColorMap: mobWolfTex
                                }
                                // 尾巴枢（身体后上部，绕根旋转）：尾根 = 身体后上 (0, 0.16, 0.38)（MobModel 局部坐标：躯干心
                                //   0.02 半 0.15×0.40 → 后上角）。eulerRotation.x 正 → +Y 端朝 +Z（尾向后竖）；满血 → 140−105×1=35°
                                //   （竖起）、残血 → 140−105×0=140°（下垂）。随 bodyYaw + 父 visible + 坐姿变换继承。
                                Node {
                                    id: wolfTailPivot
                                    position: Qt.vector3d(0, 0.16, 0.38)
                                    property real tailAngle: {
                                        const _r = entityManager.revision
                                        const h = entityManager.healthAt(index)
                                        const m = entityManager.maxHealthAt(index)
                                        return _r >= 0 ? ((m > 0) ? (140 - 105 * Math.max(0, Math.min(1, h / m))) : 0) : 0
                                    }
                                    eulerRotation.x: tailAngle
                                    // 尾巴本体（垂直细盒，尾根下方 0.10 中心 → 竖尾时从尾根向上伸出；灰狼毛色 × 昼夜灰阶 +
                                    //   受击红闪同身体）。
                                    Model {
                                        geometry: UnitCube {}
                                        position: Qt.vector3d(0, 0.10, 0)
                                        scale: Qt.vector3d(0.06, 0.20, 0.06)
                                        materials: PrincipledMaterial {
                                            lighting: PrincipledMaterial.NoLighting
                                            baseColor: {
                                                const _r = entityManager.revision
                                                const tl = terrainLight(worldClock.skyLight)
                                                if (_r >= 0 && entityManager.hurtFlashAt(index) > 0) return "#ff0000"
                                                return _r >= 0 ? Qt.rgba(0.55 * tl.r, 0.55 * tl.g, 0.55 * tl.b, 1.0) : "#000000"
                                            }
                                        }
                                    }
                                }
                                // 眼（2 颗深色点；头前侧。MobModel 头心 (0,0.12,-0.52) 半 (0.14,0.15,0.18) → 前面 z=-0.70；
                                //   眼 y≈0.16、x=±0.08；z 贴头前面略凸（-0.71，同 t52 贴脸防 z-fight）。同猪眼纯色子 Model 模式。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.08, 0.16, -0.71)
                                    scale: Qt.vector3d(0.04, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.08, 0.16, -0.71)
                                    scale: Qt.vector3d(0.04, 0.05, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
                    }
                    Loader {
                        active: entKind === EntityManager.Mob && entMobType === EntityManager.MobOcelot
                        sourceComponent: Component {
                            // t481 Ocelot/Cat（豹猫/猫；mobType 11）：MobModel 猫科几何（细长躯干 + 尖耳 + 长尾 + 4 细腿）
                            //   + 贴图（机制等价 MC 1.0 豹猫/猫，§9 原创模型 + 贴图）。passive（hostile=false）→ 生命周期
                            //   同被动生物；未驯服走 aiOcelot 游荡分支（丛林野豹猫被动散步），生鱼驯服 → 变猫（随机毛色
                            //   变体 0..2，ocelotVariantAt 选 mob_cat_* 贴图；未驯服用 mob_ocelot 斑点豹猫贴图）。驯服猫
                            //   跟随主人（aiOcelot follow）+ 空手右键坐/站切换。受击红闪（同既有 hurtFlashAt>0 → baseColor
                            //   红模式）。坐姿（ocelotSittingAt=true）→ 整个 Model 垂直压缩 + 后倾 + 略下沉（同狼坐姿，
                            //   读作「坐地留守」）。眼为子节点（纯色 NoLighting，同猪眼模式）。
                            Model {
                                visible: entKind === EntityManager.Mob && entMobType === EntityManager.MobOcelot
                                property real ocatSit: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.ocelotSittingAt(index) ? 1 : 0) : 0 }
                                geometry: MobModel {
                                    mobType: 11
                                    // 豹猫/猫无 pack entity 贴图映射（同 Wolf/Squid，spec 未列 → 保程序生成贴图全脸 UV）。
                                    packTextured: false
                                    walkPhase: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.walkPhaseAt(index)) : 0 }
                                }
                                // t481 坐姿变换：坐 → 垂直压缩（1−0.22=0.78）+ 后倾（-18° 绕 X，鼻略抬）+ 略下沉 0.08 格 →
                                //   读作「坐地留守」；站 → 原比例 / 无倾 / 原高。ocatSit 绑 revision → toggleOcelotSit 翻转即时切姿。
                                position: Qt.vector3d(0, mobModelYOff - ocatSit * 0.08, 0)
                                scale: Qt.vector3d(1.0, 1.0 - ocatSit * 0.22, 1.0)
                                eulerRotation.x: ocatSit * -18
                                materials: PrincipledMaterial {
                                    lighting: PrincipledMaterial.NoLighting
                                    baseColor: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.hurtFlashAt(index) > 0 ? "#ff0000" : terrainLight(worldClock.skyLight)) : "#000000" }
                                    // 驯服 → 据 ocelotVariantAt 选 3 色猫贴图；未驯服 → mob_ocelot 豹猫贴图（几何同，异贴图
                                    //   区分豹猫/猫，机制等价 MC 1.0 同模型异贴图）。
                                    baseColorMap: {
                                        const _r = entityManager.revision
                                        if (_r >= 0 && entityManager.ocelotTamedAt(index)) {
                                            const v = entityManager.ocelotVariantAt(index)
                                            if (v === 0) return mobCatBlackTex
                                            if (v === 1) return mobCatGingerTex
                                            return mobCatCreamTex
                                        }
                                        return _r >= 0 ? mobOcelotTex : null
                                    }
                                }
                                // 眼（2 颗斜挑深色点；头前侧。MobModel 头心 (0,0.12,-0.46) 半 (0.11,0.12,0.14) → 前面 z=-0.60；
                                //   眼 y≈0.15、x=±0.07；z 贴头前面略凸（-0.61，同 t52 贴脸防 z-fight）。同猪眼纯色子 Model 模式。
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(-0.07, 0.15, -0.61)
                                    scale: Qt.vector3d(0.035, 0.04, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                                Model {
                                    geometry: UnitCube {}
                                    position: Qt.vector3d(0.07, 0.15, -0.61)
                                    scale: Qt.vector3d(0.035, 0.04, 0.02)
                                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                                }
                            }
                        }
                        onLoaded: if (item) item.parent = mobDelegate
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
                        // t529：雪傀儡身纯雪白 → 白色 WireCube 框线融进白身不可见（用户报「F3+B 碰撞箱看不到」）。
                        //   雪傀儡 / 铁傀儡（淡色身）改用青色框线（与白雪 / 铁灰高对比）；其余 mob 保留白框线（对棕 / 粉
                        //   身本就高对比）。条件读 entMobType（已在 delegate 顶层 property）。
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: (entMobType === EntityManager.MobSnowGolem || entMobType === EntityManager.MobIronGolem)
                                       ? "#00e5ff" : "#ffffff"
                        }
                    }
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        // 朝向棒：从 collision 中心沿本地 -Z（mob 前 = 头朝向）延伸 facingLen（略超前壁辨识）。
                        //   UnitCube ±0.5 scale sz → 棒长 sz；position z = −sz/2 使棒从 z=0 延伸到 z=−sz（前向）。
                        //   t558 雪/铁傀儡是「高大方块身」—— 旧棒长 = mobHalfW+0.05（雪傀儡 0.40 / 铁傀儡 0.65）被
                        //   不透明的雪块 / 铁块身**完全包在体内**（用户报「F3+B 看不到朝向，红线在脑子里被挡」）。
                        //   对造物：棒延长到凸出身体前壁（+0.65）+ 抬高到上身（mobHalfH*0.6），棒从块体间伸出可见；
                        //   其余 mob（体型小 / 头前伸）保持原短棒（原长短够辨，无此问题）。
                        property real facingLen: (entMobType === EntityManager.MobSnowGolem || entMobType === EntityManager.MobIronGolem)
                                                 ? (mobHalfW + 0.65) : (mobHalfW + 0.05)
                        property real facingY: (entMobType === EntityManager.MobSnowGolem || entMobType === EntityManager.MobIronGolem)
                                               ? (mobHalfH * 0.6) : 0.0
                        position: Qt.vector3d(0, facingY, -facingLen * 0.5)
                        scale: Qt.vector3d(0.03, 0.03, facingLen)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                    }
                    // t283 箭矢（Arrow）：骷髅弓箭手远程射出的投射物。细长杆 Model 沿飞行速度定向（yaw + pitch，
                    //   arrowYawAt/arrowPitchAt 据 vel 算）。delegate Node 已摆 position（箭世界坐标）+ 不转（bodyYaw=0
                    //   非 Mob）；本子 Node 据 yaw/pitch 转杆朝飞行方向。机制等价 MC 1.0 骷髅射箭抛物 + 命中伤害；
                    //   名称 / 视觉全原创（§9 区隔，纯色自绘非 MC 美术）。NoLighting（可见 Model 红线）。
                    //   杆本地 -Z = 飞行方向（同 player/mob 模型 -Z 前）；UnitCube ±0.5 scale (0.05,0.05,0.5) → 细杆长 0.5
                    //   沿 Z；position z=-0.25 让杆从中心向前伸（箭头在前）。箭头 / 箭羽为杆子节点同向继承定向。
                    Node {
                        visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Arrow) : false }
                        property real arrYaw: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.arrowYawAt(index)) : 0 }
                        property real arrPitch: { const _r = entityManager.revision; return _r >= 0 ? (entityManager.arrowPitchAt(index)) : 0 }
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
                    // t482 雪球（Snowball）：雪傀儡抛出的远程弹丸。白色小球（雪傀儡远程攻击，机制等价 MC 1.0
                    //   snowball）。delegate Node 已摆 position（雪球世界坐标）+ 不转（雪球对称无需定向）。NoLighting
                    //   （红线：可见 Model 必须 NoLighting）。
                    Node {
                        visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Snowball) : false }
                        // 外层白球（近纯白 + 冷蓝阴影 → 读作「压实雪球」）。
                        Model {
                            geometry: UnitCube {}
                            scale: Qt.vector3d(0.20, 0.20, 0.20)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#f0f4f8" }
                        }
                        // 内层亮白高光（冰晶反光点，强化「雪球」非纯白方块）。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0.04, 0.04, 0.06)
                            scale: Qt.vector3d(0.10, 0.10, 0.10)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                        }
                    }
                    // t583 鸡蛋（Egg）：玩家投掷 / 发射器弹出的孵鸡弹丸。奶白卵形（纵向略长的双色壳 + 顶部高光
                    //   → 读作「鸡蛋」非白方块；机制等价 MC 1.0 egg 投掷物，视觉原创 §9 区隔）。delegate Node
                    //   已摆 position（鸡蛋世界坐标）+ 不转（卵形对称无需定向）。NoLighting（红线：可见 Model
                    //   必须 NoLighting）。命中（方块 / mob）碎裂（eggBreak → burstEgg 蛋壳碎屑）+ 1/8 概率
                    //   在命中处孵 1 只小鸡（Entities 层 Egg tick 分支，机制等价 MC 1.0 鸡蛋砸出小鸡）。
                    Node {
                        visible: { const _r = entityManager.revision; return _r >= 0 ? (entKind === EntityManager.Egg) : false }
                        // 卵形主体：两颗竖叠小立方（下大上小）读作「纵向略长的蛋壳」。
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0, -0.02, 0)
                            scale: Qt.vector3d(0.16, 0.13, 0.16)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#f5efdd" }
                        }
                        Model {
                            geometry: UnitCube {}
                            position: Qt.vector3d(0, 0.06, 0)
                            scale: Qt.vector3d(0.11, 0.08, 0.11)
                            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#faf5e6" }
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

        // t196 / t225 / t441 箱子盖子开合动画（场景内 3D Node，与 torchHost / itemHost 同层）。仅当前所开箱子
        //   （chestX/Y/Z）一处显盖子（一次只开一只箱子，chestOpen 单 bool）。chestLidAngle 由 openChest/
        //   closeChest 驱动（window 级 Behavior 平滑过渡 0↔全开角）。
        //
        // 朝向：箱子放置时前面（锁面）朝玩家（placeBlock 写 state=horizontalFacing^1）。盖子铰链在锁面
        //   **背侧**（开盖向远离玩家翻）。用「外层 pivot（顶面中心 + Yaw）+ 内层铰链（局部背棱 + X 旋转）」
        //   嵌套，使局部「+Z=背 / -Z=前」约定对所有朝向统一：pivot 的 Yaw 把局部 +Z 转到箱子实际背侧方向
        //   （chestLidYaw 据 chestFacing 算），铰链摆在局部 +Z=+0.5（背棱），其 X 旋转翻起局部 -Z（前缘）。
        //
        // t441 根因 + 重做盖几何（修「右键打开仍见整方块 + 额外错位件」）：旧盖子满深 1.0×0.25×1.0（仅压厚度，
        //   深仍占满 cell）。绕后棱 +X 翻 ~90° 时，盖子的「深 1.0」变成「立起高 ~1.0」= 与本体（整立方 1.0）等高，
        //   正面呈现一张满格贴图面 → 肉眼读作「箱子背后立着第二只箱子」（额外错位件）。历次修（t409 等）只改
        //   厚度数值（0.16→0.10→0.25），而**决定开盖立起高度的是「深」不是「厚」** → 改厚度对开盖观感零效果，
        //   即「反复修仍坏」的真因。重做：盖子缩为 1.0×0.30×0.50（机制等价 MC 箱子盖 8/16 深 / 5/16 厚 ——
        //   盖子只占背半，浅），原点在后顶棱（铰链）。开盖 ~95° 只立起 ~0.5 格（明显矮于本体 1.0）→ 读作「翻起
        //   的盖子」而非「第二只箱子」；合态盖子嵌在背顶（被本体遮挡，仅动画期可见）。lighting:NoLighting（红线）。
        //
        // 盖子本体 = BlockCube(Chest)（复用图集 per-face 贴图 chest_top/side/front → 与箱子本体 mesher
        //   整立方同外观，零 MC 资产）。可见性：playing 态且（chestOpen 或 角>0）→ 合盖动画播放期间
        //   （chestOpen 已 false 但角未到 0）盖子仍显，角到位 0 后自动隐（防闭态无谓渲染）。分层（PLAN §2）：
        //   纯呈现层，只读 chestX/Y/Z + chestFacing + chestOpen/chestLidAngle。
        Node {
            id: chestLidPivot
            visible: window.appState === "playing" && (window.chestOpen || window.chestLidAngle > 0.01)
            // 外层 pivot：箱子顶面中心；Yaw 把局部 +Z（背侧）转到箱子实际背侧（chestLidYaw 据朝向算）。
            position: Qt.vector3d(window.chestX + 0.5, window.chestY + 1.0, window.chestZ + 0.5)
            eulerRotation: Qt.vector3d(0, window.chestLidYaw, 0)

            // 内层铰链：局部背棱（local +Z = +0.5，即箱子背侧顶棱中点 = 铰链轴）；绕局部 X 翻开（chestLidAngle）
            //   → 局部 -Z（前缘 = 锁面侧）上扬 = 开盖向背侧翻（机制等价 MC 箱子后铰链前翻）。
            Node {
                position: Qt.vector3d(0, 0, 0.5)
                eulerRotation: Qt.vector3d(window.chestLidAngle, 0, 0)

                Model {
                    // t441 浅盖：BlockCube 顶点 ±0.5，缩 (1.0, 0.30, 0.50) → 1 宽 × 0.30 厚 × 0.50 深（仅背半）。
                    //   摆位相对铰链（铰链原点 = 背侧顶棱中点）：X 居中(0)、Y 向下(-0.15 → 中心 cy+0.85、顶面齐
                    //   cy+1.0)、Z 向前(-0.25 → 覆盖背半 [前 -0.5, 背 0])。开盖 ~95°：0.50 的「深」变立起高 ~0.5
                    //   （矮于本体 1.0）→ 读作翻起的盖子；满深 1.0 会立起满高像第二只箱子（旧 bug）。
                    geometry: BlockCube { blockId: 22 }   // 22 = BlockRegistry::Chest（与 blockregistry.h Id 枚举同源；同 torch=13 既有字面量 + 注释模式）
                    position: Qt.vector3d(0.0, -0.15, -0.25)
                    scale: Qt.vector3d(1.0, 0.30, 0.50)
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

    // t351 没入岩浆橙雾：眼位在岩浆里（player.eyeInLava）→ 全屏暖橙半透叠层（机制等价 MC 没入岩浆的橙红视野雾）。
    //   与水下蓝雾平行（同模式、同层级、同显隐条件），水/岩浆互斥（一格非既水又岩浆）。纯 Rectangle 无 MouseArea
    //   → 不拦截鼠标（同蓝雾经验）。状态驱动 = player.eyeInLava（tickImpl 每 tick 重算、翻转才发 eyeInLavaChanged）。
    //   仅 playing 态显。橙色比蓝雾略浓（岩浆近不透 → 视野受阻更强，机制等价 MC 岩浆视野差于水）。
    Rectangle {
        anchors.fill: parent
        visible: window.appState === "playing" && player.eyeInLava
        color: Qt.rgba(0.85, 0.30, 0.05, 0.55)
    }

    // t344 着火火焰叠层：玩家燃烧（player.burning，岩浆 / 火点燃）→ 屏幕底部 ~35% 火焰半透叠层（机制等价
    //   MC 着火屏边火焰；不覆盖全屏，仅底部火焰窜动感）。橙红渐变（顶透明 → 底炽热）+ opacity 抖动模拟火苗窜动。
    //   纯 Rectangle / Gradient 自绘原创（§9a，非 MC 资产）。状态驱动 = player.burning（tickImpl 算时序、翻转才
    //   emit burningChanged，无每帧抖动）。仅 playing 态显。放在 View3D 之后、HUD/背包/暂停/死亡叠层之前（同水下
    //   蓝雾经验：叠层只染 3D 场景区，HUD/背包仍清晰可读）。纯 Rectangle 无 MouseArea → 不拦截鼠标。
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.35
        visible: window.appState === "playing" && player.burning
        gradient: Gradient {
            GradientStop { position: 0.0;  color: Qt.rgba(1.0, 0.35, 0.05, 0.0) }   // 顶（透明，火焰上沿渐隐）
            GradientStop { position: 0.45; color: Qt.rgba(1.0, 0.40, 0.08, 0.35) }  // 中（橙半透）
            GradientStop { position: 1.0;  color: Qt.rgba(1.0, 0.55, 0.15, 0.78) }  // 底（炽热近不透）
        }
        property real flameFlicker: 0.85
        SequentialAnimation on flameFlicker {
            loops: Animation.Infinite
            NumberAnimation { from: 0.65; to: 1.0; duration: 120 }
            NumberAnimation { from: 1.0; to: 0.70; duration: 160 }
        }
        opacity: flameFlicker
    }

    // t386 闪电屏幕白闪叠层：雷击瞬间全屏白透叠层闪现 → 300ms 淡到透明（机制等价 MC 1.0 雷击瞬时全屏白闪）。
    //   纯 Rectangle 无 MouseArea → 不拦截鼠标（同水下蓝雾 / 岩浆橙雾经验）。opacity 由 lightningFlashAnim 驱动
    //   （World::lightningStruck → 上方 Connections.onLightningStruck → lightningFlashAnim.start()）。仅 playing 态显。
    //   放在 View3D 之后、HUD/背包/暂停叠层之前（同水下蓝雾：只染 3D 场景区，HUD 仍清晰可读）。
    Rectangle {
        id: lightningFlash
        anchors.fill: parent
        visible: window.appState === "playing"
        color: "#ffffff"
        opacity: 0.0   // 静止时透明；雷击时由下方动画闪现后淡回 0
        SequentialAnimation on opacity { id: lightningFlashAnim; running: false
            NumberAnimation { from: 0.85; to: 0.0; duration: 300 }   // 满白闪现 → 300ms 淡到透明（雷击瞬时白闪）
        }
    }

    // t465 受击红色 vignette（机制等价 MC 1.0 受伤屏边红雾；spec「屏幕红色 vignette 闪 ~200ms」）。
    //   玩家受击（PlayerState.damaged → 上方 Connections.onDamaged → damageVignetteAnim.start()）→
    //   屏幕四角/边缘红半透渐变（中心透明、边缘暗红）闪现后淡回 0（~200ms）。Canvas 一次性绘制径向渐变
    //   （中心透明 → 70% 暗红 → 边缘亮红），只在窗口尺寸变时重绘；闪烁靠 Item.opacity 动画（不每帧重绘 Canvas）。
    //   与 t67「模型变红 + 相机晃动」并存：模型变红是 3D 模型层反馈、相机晃动是相机层反馈、本 vignette 是
    //   屏幕层反馈——三层叠加完整覆盖「受击」的全方位视觉反馈（spec「红色 vignette + 轻微相机震动」）。
    //   纯 Canvas/Item 无 MouseArea → 不拦截鼠标（同水下蓝雾 / 闪电白闪经验）。仅 playing 态显。
    //   放在 View3D 之后、HUD/背包/暂停叠层之前（同水下蓝雾：只染 3D 场景区，HUD 仍清晰可读）。
    //   分层（PLAN §2）：呈现层只消费 PlayerState 语义事件（damaged），绝不反向写数值（同 hurtAnim / shakeAnim）。
    Item {
        id: damageVignette
        anchors.fill: parent
        visible: window.appState === "playing"
        opacity: 0.0   // 静止时透明；受击时由下方动画闪现后淡回 0
        Canvas {
            anchors.fill: parent
            // 一次性绘制径向渐变（中心透明、边缘红），只在窗口尺寸变时重绘。闪烁靠 damageVignette.opacity
            //   动画驱动（每帧改 Item.opacity 而非重绘 Canvas → 性能稳）。
            onPaint: {
                const ctx = getContext('2d')
                ctx.reset()
                const w = width, h = height
                const cx = w / 2, cy = h / 2
                const innerR = Math.max(1, Math.min(w, h) * 0.30)
                const outerR = Math.max(innerR + 1, Math.max(w, h) * 0.75)
                const g = ctx.createRadialGradient(cx, cy, innerR, cx, cy, outerR)
                g.addColorStop(0.0, "rgba(0,0,0,0)")             // 中心透明（不挡视野核心区）
                g.addColorStop(0.65, "rgba(120,0,0,0.30)")       // 中环暗红（渐显）
                g.addColorStop(1.0, "rgba(190,0,0,0.85)")        // 边缘亮红（四角渐变最浓）
                ctx.fillStyle = g
                ctx.fillRect(0, 0, w, h)
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
        SequentialAnimation on opacity { id: damageVignetteAnim; running: false
            // 受击瞬间满 alpha 闪现 → 200ms 衰减回透明（spec「闪 ~200ms」；连击 start() 重启 → 重新闪）
            NumberAnimation { from: 1.0; to: 0.0; duration: 200; easing.type: Easing.OutQuad }
        }
    }

    // t388/t457 睡觉过渡叠层（夜间右键床 → 躺下渐黑 → 全黑显「起床」按钮 → 跳清晨/按钮醒 fade 回显）：
    //   机制等价 MC 1.0 睡觉过渡，但**非瞬黑瞬醒**——三阶段平滑动画（Lying 渐黑 + Settled 全黑 + Waking 渐显）。
    //   状态驱动 = player.sleeping（序列进行中）+ player.sleepFade（0..1 全屏黑透明度，阶段派生）。
    //   Lying：sleepFade 0→1（~1s 渐黑，相机同步 sleepLie 降低 + 上仰转躺）；Settled：恒 1（全黑 + 起床按钮）；
    //   Waking：1→0（~0.8s 渐显清晨/当前场景，sleeping=false 时叠层隐藏，此时 fade 已 0 无跳变）。
    //   纯 Rectangle 无 MouseArea → 不拦截鼠标（同水下蓝雾 / 闪电白闪经验）。仅 playing 态显；放 View3D 之后、
    //   HUD/背包/暂停叠层之前（起床按钮在其上层单独声明，点得到）。
    Rectangle {
        anchors.fill: parent
        visible: window.appState === "playing" && player.sleeping
        color: "#000000"
        opacity: player.sleepFade * 0.98
    }

    // t457「起床」按钮（Settled 全黑入睡阶段显）：玩家可点（左/右键）立即平滑醒（不跳清晨，仍处夜晚），不点则自动跳清晨。
    //   spec「中间显起床按钮→玩家不按则度过夜晚（跳到黎明）→按则立即醒」。仅 sleepSettled 阶段显（其它阶段隐）。
    //   睡觉期间指针保持 captured（光标隐藏，屏幕渐黑不可见）→ 点击唤醒走 PlayerController eventFilter（任意左/右键
    //   点击 → wakeUpFromBed），不依赖光标位置。本按钮纯视觉提示（居中显「起床」+「点击起床」副标），不持 MouseArea
    //   （eventFilter 已消费 press，MouseArea 收不到）。放黑叠层之上（声明顺序在后 → Z 序在上），Settled 时黑叠层全黑
    //   盖背景、按钮清晰可见。
    Rectangle {
        visible: window.appState === "playing" && player.sleepSettled
        anchors.centerIn: parent
        width: 220
        height: 96
        radius: 10
        color: "#222230"
        border.color: "#9aa0b0"
        border.width: 2
        Column {
            anchors.centerIn: parent
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("起床")
                color: "#f0f0f0"
                font.pixelSize: 26
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("（点击任意处起床，否则将度过夜晚）")
                color: "#c0c0cc"
                font.pixelSize: 13
            }
        }
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
            // t465 受击红色 vignette（屏幕层反馈，与模型变红 + 相机晃动三层叠加完整覆盖受击反馈）
            damageVignetteAnim.start() // 200ms 红色 vignette 闪现淡出（连击 start() 重启 → 重新闪）
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
            progress.onDeath()  // progress 统计死亡次数
            if (window.inventoryOpen) window.inventoryOpen = false
            if (window.craftingTableOpen) window.craftingTableOpen = false
            if (window.furnaceOpen) window.furnaceOpen = false
            if (window.chestOpen) window.chestOpen = false
            if (window.chatOpen) window.chatOpen = false   // t312：死亡关聊天（死亡屏接管光标）
            // pause-menu：死亡关暂停菜单子面板（设置 / 进度 / 统计；防死亡态遗留，死亡屏 z=180 盖在其上）。
            if (window.settingsOpen) window.settingsOpen = false
            if (window.progressOpen) window.progressOpen = false
            if (window.statsOpen) window.statsOpen = false
            window.returnHeldToHotbar()
            player.dropAllItems()     // t175：死亡掉落整个背包到死亡点 + 清空背包
            // t443 死亡掉部分 XP（spec「死亡地点掉部分 XP，约 1 只怪量」）：在死亡点 spawn 1 个经验球
            //   （量约 1 只被动 mob = 1-3 XP）。XP 清零已在 PlayerState.takeDamage 致死分支完成（Game 层规则，
            //   先于本 onDied 触发）；此处仅 spawn 球（呈现层编排，需死亡位置 + XpOrbManager；PLAN §2 分层：
            //   Game 层持 XP 数值 / 死亡规则，呈现层持位置 + 实体生成）。坐标同 dropAllItems 死亡格（脚底
            //   floor），球与掉落物同处便于玩家走回拾取。机制对齐项目决策（MC 死亡掉经验，本工程简化为定量
            //   「约 1 只被动 mob」而非 level 比例，spec 明示）。
            const dp = player.feetPosition
            xpOrbs.spawnOrb(Math.floor(dp.x), Math.floor(dp.y), Math.floor(dp.z),
                            1 + Math.floor(Math.random() * 3))  // 1-3 XP（约 1 只被动 mob 量）
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
            // t549：栅格编辑版本号 +1（驱动附魔台 UI bookshelfPower 绑定重算；见 window.worldEditRev 注释）。
            window.worldEditRev++
            // t88：火把被破 → 从伪光源列表移除（id=13=BlockRegistry::Torch；C++ 侧未把枚举暴露 QML，
            // 此处用字面量 13 + 注释，与 blockregistry.h Id 枚举同源）。
            // t170：同步销毁视觉 delegate（木柄+火焰 Model）—— torchPositions 供 TorchSmoke/选中框读，
            //   torchHost.torchObjs 供本场景渲染，二者经同一信号并行增删。
            if (id === 13) { removeTorchAt(x, y, z); torchHost.removeTorchVis(x, y, z) }
            // t173/t179/t522：箱子被破 → 先把内部 27 槽内容 spawnItem 掉落世界（机制等价 MC 1.0 破箱掉落
            //   内容，修用户报「箱子装东西后挖掉不掉」），再 chestStore.clearChest 清孤儿条目。id=22=
            //   BlockRegistry::Chest（与 blockregistry.h Id 枚举同源；此处用字面量 + 注释，同 torch=13 /
            //   furnace=10 既有模式）。逐非空槽 spawnItem（每槽独立实体，便于玩家走回拾取；itemEntities.spawnItem
            //   内置就近合并，同 id 自动合）；上界取 chestStore.slotCount（恒 27，与 kSlotsPerChest 同源），
            //   同 furnace=10 的逐槽掉落模式（3 槽循环用字面量 3，本 27 槽走 VM 暴露的 slotCount 以免 magic number）。
            //   t571 标注【自然掉落：恒发（含创造）】—— 消费 blockBroken（World 语义事件，无 drop 标志）→ 创造
            //   破箱：箱子本体物品不掉（player.onSpawnItem 由 drop=false 门控）、内容物照掉（本处恒发），正确。
            if (id === 22) {
                for (let ci = 0; ci < chestStore.slotCount; ++ci) {
                    const cid = chestStore.slotIdAt(x, y, z, ci)
                    const ccount = chestStore.slotCountAt(x, y, z, ci)
                    if (cid !== 0 && ccount > 0)
                        itemEntities.spawnItem(x, y, z, cid, ccount)
                }
                chestStore.clearChest(x, y, z)
            }
            // t177 二轮复盘：熔炉被破 → 把 in/fuel/out 内容 spawnItem 掉落世界（机制等价 MC 1.0 破熔炉掉落
            //   内容，修用户报「打掉熔炉内部物品不掉」），再 furnaceStore.clearFurnace 清孤儿条目。id=10=
            //   BlockRegistry::Furnace（与 blockregistry.h Id 枚举同源；此处用字面量 + 注释，同 torch=13 /
            //   chest=22 既有模式）。逐槽 spawnItem（每槽独立实体，便于玩家走回拾取；itemEntities.spawnItem
            //   内置就近合并，同 id 自动合）。
            //   t571 标注【自然掉落：恒发（含创造）】—— 同 chest=22（内容物掉落与模式无关）。
            if (id === 10) {
                for (let fi = 0; fi < 3; ++fi) {
                    const fid = furnaceStore.slotIdAt(x, y, z, fi)
                    const fcount = furnaceStore.slotCountAt(x, y, z, fi)
                    if (fid !== 0 && fcount > 0)
                        itemEntities.spawnItem(x, y, z, fid, fcount)
                }
                furnaceStore.clearFurnace(x, y, z)
            }
            // t542：发射器被破 → 把内部 9 槽内容 spawnItem 掉落世界（机制等价 MC 1.0 破发射器掉落内容，
            //   修用户报「发射器放东西进去挖掉没掉东西」通病），再 dispenserStore.clearDispenser 清孤儿条目。
            //   id=107=BlockRegistry::Dispenser（与 blockregistry.h Id 枚举同源；此处用字面量 + 注释，同 torch=13 /
            //   chest=22 / furnace=10 既有模式）。逐非空槽 spawnItem（每槽独立实体，便于玩家走回拾取；
            //   itemEntities.spawnItem 内置就近合并，同 id 自动合）。铁砧（97-99）/ 附魔台（94）当前是 shell-mode
            //   无容器（功能后补），破掉无内部物品可掉 → 无需 dump（spec「铁砧/附魔台主要是发射器」）。
            //   t571 标注【自然掉落：恒发（含创造）】—— 同 chest=22（内容物掉落与模式无关）。
            if (id === 107) {
                for (let di = 0; di < dispenserStore.slotCount; ++di) {
                    const did = dispenserStore.slotIdAt(x, y, z, di)
                    const dcount = dispenserStore.slotCountAt(x, y, z, di)
                    if (did !== 0 && dcount > 0)
                        itemEntities.spawnItem(x, y, z, did, dcount)
                }
                dispenserStore.clearDispenser(x, y, z)
            }
            // t117：被破格上方若为沙 → 失支撑塌落（maybeTrigger 内部 setBlock(air) 递归触发更上方沙链）。
            maybeTriggerFallingBlock(x, y + 1, z)
        }
        function onBlockPlaced(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstPlace(x, y, z, id)
            // t89：放块音（按新放方块 id）。
            audio.playPlace(id)
            // t549：栅格编辑版本号 +1（驱动附魔台 UI bookshelfPower 绑定重算；见 window.worldEditRev 注释）。
            window.worldEditRev++
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
        // t445 世界侧产出的掉落物（仙人掌失撑 / 邻接方块即整柱坍落）→ 转发到 itemEntities.spawnItem 生成掉落实体。
        //   同 player.onSpawnItem / fallingBlockDropped 模式（单向事件流：World 低层发语义事件、呈现层只消费，
        //   PLAN §2 分层）。id = 方块 id（仙人掌），count = 1（每格一件，整柱坍落 = 多件散落物）。
        function onBlockDroppedAsItem(x, y, z, id) { itemEntities.spawnItem(x, y, z, id, 1) }
        // t527 积雪层整柱失撑坍落 → 转 entityManager.spawnFallingBlockState 生成携带层数 metadata 的下落实体。
        //   World 低层（checkSnowLayerOnEdit）发语义事件（柱底坐标 + 总层数 1..8），呈现层只消费（PLAN §2 分层：
        //   World 不反向依赖 Entities）。layers 1..8 → state=layers-1（0..7）保留层数；blockId=44=SnowLayer（与
        //   chunkgeometry / hotbar.cpp 字面量 + 注释同源；QML 无 BlockRegistry 枚举访问故字面量）。
        //   下落实体着地（entitymanager FallingBlock tick）走 setBlockFromEntity 带 state 写回雪层（保留层数）。
        function onSnowLayerFell(x, y, z, layers) {
            const clamped = Math.max(1, Math.min(8, layers)) // clamp 1..8（防越界；state 0..7）
            entityManager.spawnFallingBlockState(x, y, z, 44, clamped - 1)
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
        // t328：切槽（数字键 1-9 / 滚轮）→ UI click 反馈（与视觉高亮配对的音频 tick）。
        function onSelectedSlotChanged() { audio.playUIClick() }
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
                    && !window.enchantingTableOpen && !window.anvilOpen && !window.dispenserOpen   // t549：三 UI 开时 T 不开聊天（先关面板）
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
                else if (window.enchantingTableOpen) window.closeEnchantingTable()
                else if (window.anvilOpen) window.closeAnvil()
                else if (window.dispenserOpen) window.closeDispenser()
                else window.toggleInventory()
                e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.inventoryOpen) {
                window.closeInventory(); e.accepted = true; return
            }
            // pause-menu ESC 关子态面板（settingsOpen / progressOpen / statsOpen）：!captured 时 Esc 落 QML
            //   → 关回暂停菜单（不直接 unpause / 不抢 grab）。优先级与下方各背包面板并列（子态与背包互斥：
            //   任一子态开时背包必关，故分支先后无串台）。资源查看器（resourceBrowserOpen）在下方独立分支
            //   （回设置面板，不回暂停菜单）。
            if (e.key === Qt.Key_Escape && window.settingsOpen) {
                window.settingsOpen = false; e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.progressOpen) {
                window.progressOpen = false; e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.statsOpen) {
                window.statsOpen = false; e.accepted = true; return
            }
            // t458 资源查看器：Esc 关（回设置面板，仍 !captured）。资源查看器在暂停 + 设置态打开（!captured，
            //   Esc 不被 C++ 事件过滤器拦截，落到 QML → 本分支处理）。
            if (e.key === Qt.Key_Escape && window.resourceBrowserOpen) {
                window.resourceBrowserOpen = false; e.accepted = true; return
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
            // t474 附魔台面板：Esc 关（同工作台 / 熔炉 / 箱子；!captured 时 Esc 落 QML → 本分支）。
            if (e.key === Qt.Key_Escape && window.enchantingTableOpen) {
                window.closeEnchantingTable(); e.accepted = true; return
            }
            // t477 铁砧面板：Esc 关（同附魔台 / 工作台 / 熔炉 / 箱子；!captured 时 Esc 落 QML → 本分支）。
            if (e.key === Qt.Key_Escape && window.anvilOpen) {
                window.closeAnvil(); e.accepted = true; return
            }
            // t517 发射器面板：Esc 关（同铁砧 / 附魔台 / 工作台 / 熔炉 / 箱子；!captured 时 Esc 落 QML → 本分支）。
            if (e.key === Qt.Key_Escape && window.dispenserOpen) {
                window.closeDispenser(); e.accepted = true; return
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
                    || window.enchantingTableOpen || window.anvilOpen || window.dispenserOpen   // t549：三 UI 同背包语义（悬停槽丢弃）
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
            //   t549：bagOpen 扩到附魔台 / 铁砧 / 发射器 —— 三 UI 打开时 Shift 同样不透传（防玩家蹲下）。
            // Shift：始终追踪 shiftHeld（供背包槽 TapHandler 读做 Shift+左键搬运）；背包开时不透传给 player。
            const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen
                || window.enchantingTableOpen || window.anvilOpen || window.dispenserOpen   // t549：三 UI 开时 Shift 不透传 player（防蹲）
            //   shiftHeld 不论背包开关都更新 —— 闭包后若用户仍按住 Shift，下一次 TapHandler 读到的 shiftHeld
            //   仍准确（松开时 Keys.onReleased 翻回 false）。
            if (e.key === Qt.Key_Shift) {
                window.shiftHeld = true
                if (bagOpen) { e.accepted = true; return }    // 蹲下守卫：背包开时不让 Shift 进 player.m_keys
            }

            if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {            // 1–9 直选 hotbar 槽 0..8（属性赋值走 WRITE setter）
                if (bagOpen) {
                    // t512 创造调色板 hover 物品 + 1-9 → 强制替换对应 hotbar 槽（覆盖原物，不论原槽有无）。
                    //   机制等价 MC 1.0 创造模式 hotbar；优先于槽 hover 互换（指针同一时刻只在一个区域，二者互斥，
                    //   但显式优先级让「调色板 hover」路径不被「槽 hover」误覆盖，且调色板单元格无 hoveredSlotKey
                    //   → 此处不会与 swapHoveredWithHotbar 串台）。hotbar 槽 hover（hoveredSlotKey≠""）仍走 t110
                    //   整栈互换；两者皆无 → 不动 selectedSlot。
                    if (window.creativeHoveredItemId !== 0 && player.mode === PlayerController.Creative) {
                        inventoryPanel.forceReplaceHotbarFromCreative(e.key - Qt.Key_1)
                    } else if (window.hoveredSlotKey !== "") {
                        window.swapHoveredWithHotbar(e.key - Qt.Key_1)
                    }
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
                    || window.enchantingTableOpen || window.anvilOpen || window.dispenserOpen   // t549：与 press 守卫对称
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
                if (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen
                    || window.enchantingTableOpen || window.anvilOpen || window.dispenserOpen) return   // t549：三 UI 开时滚轮不切槽
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
                 && !window.enchantingTableOpen && !window.anvilOpen && !window.dispenserOpen   // t549：三 UI 开时抑制暂停叠层
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
                    if (window.resourceBrowserOpen) return   // t458：资源查看器开时不响应背景点击
                    player.grab(); keyInput.forceActiveFocus()
                }
            }
        }
        // pause-menu 6 行菜单布局（删原 PAUSED / click to play / 6 行按键帮助文字；用户诉求「重做成按钮列表」）。
        //   布局（按钮风格沿用现有 Rectangle + MouseArea + hover 模板，§9 GUI 自绘原创，无 MC GUI PNG）：
        //     行1: [返回游戏] 居中（绿色大按钮 = 点背景 grab 同效，显式按钮更直观）。
        //     行2: [进度] [统计]  两按钮对半分（中间留较大间隙）。进度=progressOpen 成就面板；统计=statsList 面板。
        //     行3: [提供反馈] [报告漏洞]  占位无功能（点显「敬请期待」）。
        //     行4: [选项] [对局域网开放]  选项=打开选项面板（settingsOpen，复用现有设置面板容器）；局域网=占位。
        //     行5: [Mod]  居中单按钮（占位无功能）。
        //     行6: [保存并退回到标题屏幕]  居中（= saveAndExitToWorldList，文字改通用词）。
        Rectangle {
            width: 420; height: 360; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.centerIn: parent; spacing: 10
                // 行1：返回游戏（绿色大按钮；点击 grab 恢复捕获，同背景点击语义，但更显式）。
                Rectangle {
                    width: 360; height: 38; radius: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: resumeArea.containsMouse ? "#2a4a2a" : "#1a3a1a"
                    border.color: "#3a6a3a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "返回游戏"
                           color: "#7fe57f"; font.pixelSize: 15; font.bold: true }
                    MouseArea {
                        id: resumeArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { player.grab(); keyInput.forceActiveFocus() }
                    }
                }
                // 行2：进度 / 统计（两按钮对半分，中间留较大间隙）。子面板 progressOpen / statsOpen 各自打开。
                Row {
                    spacing: 24
                    anchors.horizontalCenter: parent.horizontalCenter
                    // 进度 → progressOpen 成就列表面板。
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: progressBtnArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                        border.color: "#3a5a7a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "进度"
                               color: "#7fb0e5"; font.pixelSize: 14 }
                        MouseArea {
                            id: progressBtnArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.progressOpen = true
                        }
                    }
                    // 统计 → statsOpen 统计列表面板。
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: statsBtnArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                        border.color: "#3a5a7a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "统计"
                               color: "#7fb0e5"; font.pixelSize: 14 }
                        MouseArea {
                            id: statsBtnArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.statsOpen = true
                        }
                    }
                }
                // 行3：提供反馈 / 报告漏洞（占位无功能；点显「敬请期待」toast，复用右下角成就 toast 同一组件，
                //   避免另起弹窗）。
                Row {
                    spacing: 24
                    anchors.horizontalCenter: parent.horizontalCenter
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: feedbackArea.containsMouse ? "#3a3a3a" : "#2a2a2a"
                        border.color: "#555555"; border.width: 1
                        Text { anchors.centerIn: parent; text: "提供反馈"
                               color: "#bbbbbb"; font.pixelSize: 14 }
                        MouseArea {
                            id: feedbackArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.showInfoToast("敬请期待")
                        }
                    }
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: bugArea.containsMouse ? "#3a3a3a" : "#2a2a2a"
                        border.color: "#555555"; border.width: 1
                        Text { anchors.centerIn: parent; text: "报告漏洞"
                               color: "#bbbbbb"; font.pixelSize: 14 }
                        MouseArea {
                            id: bugArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.showInfoToast("敬请期待")
                        }
                    }
                }
                // 行4：选项 / 对局域网开放（选项=打开选项面板 settingsOpen；局域网=占位）。
                Row {
                    spacing: 24
                    anchors.horizontalCenter: parent.horizontalCenter
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: optionsArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                        border.color: "#3a5a7a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "选项"
                               color: "#7fb0e5"; font.pixelSize: 14 }
                        MouseArea {
                            id: optionsArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.settingsOpen = true
                        }
                    }
                    Rectangle {
                        width: 168; height: 34; radius: 6
                        color: lanArea.containsMouse ? "#3a3a3a" : "#2a2a2a"
                        border.color: "#555555"; border.width: 1
                        Text { anchors.centerIn: parent; text: "对局域网开放"
                               color: "#bbbbbb"; font.pixelSize: 14 }
                        MouseArea {
                            id: lanArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.showInfoToast("敬请期待")
                        }
                    }
                }
                // 行5：Mod（居中单按钮；占位无功能）。
                Rectangle {
                    width: 360; height: 34; radius: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: modArea.containsMouse ? "#3a3a3a" : "#2a2a2a"
                    border.color: "#555555"; border.width: 1
                    Text { anchors.centerIn: parent; text: "Mod"
                           color: "#bbbbbb"; font.pixelSize: 14 }
                    MouseArea {
                        id: modArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.showInfoToast("敬请期待")
                    }
                }
                // 行6：保存并退回到标题屏幕（绿色；= saveAndExitToWorldList，文字改通用词）。
                Rectangle {
                    width: 360; height: 38; radius: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: backMenuArea.containsMouse ? "#2a4a2a" : "#1a3a1a"
                    border.color: "#3a6a3a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "保存并退回到标题屏幕"
                           color: "#7fe57f"; font.pixelSize: 14; font.bold: true }
                    MouseArea {
                        id: backMenuArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.saveAndExitToWorldList()
                    }
                }
            }
        }

        // pause-menu 选项面板（原「设置」面板，行4「选项」按钮 → settingsOpen）。覆盖在暂停叠层之上
        //   （z=50，在 pauseOverlay 内暂停内容之上）。pause-menu 重构：原单列所有滑条重组成两组——
        //   「视频设置」（阴影 / 渲染距离 / 暗度 / 资源包）+ 「操作设置」（手臂位置 4 滑条 + 手持方块位置 3 滑条），
        //   每组前加粗体彩色分区标题（垂直排列，不切 Tab）。资源查看器入口保留在顶部。内容超面板高 → Flickable
        //   滚动（面板加高到 820）。仅 settingsOpen 显；Esc / 返回按钮关回暂停菜单。背景遮罩仅吸收点击（§9 lessons
        //   「全屏遮罩 onClicked 会误关」→ 此处无 close 语义，纯防穿透到背后暂停叠层的恢复 grab）。
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
                width: 460; height: 820; radius: 10
                anchors.centerIn: parent
                color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
                Column {
                    anchors.fill: parent; anchors.margins: 20; spacing: 10
                    Text { text: "选项"; color: "#eeeeee"; font.pixelSize: 22; font.bold: true
                           anchors.horizontalCenter: parent.horizontalCenter }
                    // t458 资源查看器入口（醒目大按钮）：用户诉求「找不到入口浏览所有方块 / 物品样貌」→
                    //   在设置面板顶部置高对比大按钮，点击显 ResourceBrowser（JEI 式网格 + 3D 旋转预览）。
                    //   pack 启用时预览显实际贴图效果（atlasSource / 图标均随 pack 切换刷新）。
                    //   消费点击，不冒泡到背景；宽按钮 + 高亮色作「醒目」处理。
                    Rectangle {
                        width: parent.width; height: 40; radius: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: resViewArea.containsMouse ? "#2a4a5a" : "#1a3a4a"
                        border.color: "#3a7a9a"; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "🔍  资源查看器（浏览全部方块 / 物品）"
                            color: "#7fe5e5"; font.pixelSize: 14; font.bold: true
                        }
                        MouseArea {
                            id: resViewArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.resourceBrowserOpen = true
                        }
                    }
                    // pause-menu 视频设置分组标题（§9 GUI 自绘原创：粗体彩色 Text 分隔）。
                    Text { text: "视频设置"
                           color: "#e5c07f"; font.pixelSize: 15; font.bold: true
                           width: parent.width }
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
                    // t470 渲染距离滑条（用户报 9 FPS 主因 = 全 100 chunk 渲染无视距；动态调可见 chunk 半径）。
                    //   机制等价 MC video display setting 的 render distance 滑条。range 1..8 chunk（= 16..128 格）：
                    //   小→省 draw-call/GPU、视野收；大→视野广、吃性能。本工程 10×10 chunk 小世界半径 ≥5 时全可见
                    //   无 culling 收益，故默认 4（中心出生 81/100 chunk 可见）。改值触发 _refreshChunkVisibility
                    //   立即重算（onValueChanged 直调，无需等玩家跨界）。
                    Text { text: "渲染距离（chunk 半径；当前 " + window.renderDistance
                                 + " → 可见 " + window.visibleChunkCount + "/" + (window.worldChunksPerSide * window.worldChunksPerSide)
                                 + " chunk）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    ArmSlider {
                        width: parent.width
                        label: "render distance"
                        from: 1; to: 8
                        value: window.renderDistance
                        // chunksBuilt 守门：ArmSlider 默认 value=0，初次绑 window.renderDistance(=3) 时 0→3
                        //   会触发 onValueChanged；此时 chunks 未建 / _playerCX 未定 → 跳过（chunkAnchor.onCompleted
                        //   末会显式 _updatePlayerChunk 取首值）。
                        onValueChanged: {
                            if (!window.chunksBuilt) return
                            window.renderDistance = Math.round(value)
                            window._refreshChunkVisibility()
                        }
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
                    // t415c 资源包（resource pack）：启用开关 + 目录选择器（FolderDialog）。持久化 settings.json；
                    //   apply() 即时重建合成图集、覆盖落盘 + 刷新 atlasSource（file:// 与 qrc:/ 间切换 → QML
                    //   Texture 重载）→ 贴图即时切换，无需重启。红线（PLAN §9）：loader 仅运行期读取本地
                    //   gitignored 包 PNG，绝不 bake MC 资产进 qrc；此处只暴露「开关 + 目录选择」，不接触纹理文件。
                    Text { text: "资源包（resource pack）"
                           color: "#7fae7f"; font.pixelSize: 12 }
                    Row {
                        spacing: 8
                        Rectangle {
                            id: rpToggleBox
                            width: 22; height: 22; radius: 4
                            color: resourcePack.enabled ? "#2a5a3a" : "#2a2a2a"
                            border.color: resourcePack.enabled ? "#5fe57f" : "#555555"; border.width: 1
                            Text { anchors.centerIn: parent; text: resourcePack.enabled ? "✓" : ""
                                   color: "#7fe57f"; font.pixelSize: 16; font.bold: true }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: { resourcePack.enabled = !resourcePack.enabled
                                             resourcePack.apply() } }
                        }
                        Text { text: resourcePack.enabled ? "启用（尝试加载资源包覆盖贴图）"
                                                          : "禁用（始终用程序生成默认贴图）"
                               color: "#cccccc"; font.pixelSize: 12
                               anchors.verticalCenter: parent.verticalCenter
                               wrapMode: Text.WordWrap; width: 350 }
                    }
                    Text { text: "状态：" + (resourcePack.active ? "已加载资源包，贴图已覆盖"
                                                                : "未加载资源包（用默认贴图）")
                           color: resourcePack.active ? "#7fe57f" : "#b08060"; font.pixelSize: 11 }
                    Text { text: "材质包目录（可选 pack 根目录，loader 会自动查找 assets/minecraft/textures/block）"
                           color: "#9aa0a6"; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                    Row {
                        spacing: 8
                        // 选择材质包目录：打开原生 FolderDialog。accepted → 写 packPath + 启用 + 重建（即时切换）。
                        Rectangle {
                            width: 160; height: 28; radius: 6
                            color: rpPickArea.containsMouse ? "#2a4a3a" : "#1a3a2a"
                            border.color: "#3a6a4a"; border.width: 1
                            Text { anchors.centerIn: parent; text: "选择材质包目录..."
                                   color: "#7fe5a0"; font.pixelSize: 12 }
                            MouseArea {
                                id: rpPickArea
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: rpFolderDialog.open()
                            }
                        }
                        // 当前已选目录（只读文本；未选时给提示）。
                        Text { text: resourcePack.packPath.length > 0
                                        ? resourcePack.packPath
                                        : "（未选；点左侧按钮挑选材质包目录）"
                               color: resourcePack.packPath.length > 0 ? "#c0d0c0" : "#8090a0"
                               font.pixelSize: 11
                               anchors.verticalCenter: parent.verticalCenter
                               wrapMode: Text.WordWrap; width: 260 }
                    }
                    // t415c 原生文件夹拾取器：accepted 取 selectedFolder（url），剥 file:/// 前缀得本地路径
                    //   （decodeURIComponent 处理空格 / 中文）→ packPath + enabled + apply（一步到位即时切换）。
                    FolderDialog {
                        id: rpFolderDialog
                        title: "选择材质包目录（pack 根目录或其下任意层级均可）"
                        onAccepted: {
                            var u = selectedFolder.toString()
                            if (u.startsWith("file:///"))
                                u = u.slice("file:///".length)
                            resourcePack.packPath = decodeURIComponent(u)
                            resourcePack.enabled = true
                            resourcePack.apply()
                        }
                    }
                    // pause-menu 操作设置分组标题（手臂位置 + 手持方块位置；第一人称 viewmodel 微调）。
                    Text { text: "操作设置"
                           color: "#e5c07f"; font.pixelSize: 15; font.bold: true
                           width: parent.width; topPadding: 6 }
                    // 手臂调试（从生存背包 t129 调试区迁入）：滑动写回 window 级属性 → viewModelHand
                    //   绑定读取 → 第一人称手臂 baseTilt / position 实时变。
                    Text { text: "手臂位置（实时）"
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
                    // 返回按钮：关选项面板回暂停菜单。
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
    // t458 资源查看器面板（JEI 式方块 / 物品浏览 + 3D 旋转预览）。设置面板「资源查看器」按钮触发
    //   （resourceBrowserOpen）。仅 playing && resourceBrowserOpen 显；覆盖在暂停 / 设置叠层之上
    //   （z=160，高于暂停 100 / 背包 150，低于死亡 180 / 主菜单 200）。Esc / 返回按钮关 → 回设置面板。
    //   复用 hotbar VM（creativeBlocks/Tools/Materials/Armor + iconSourceForBlock + isTool/isMaterial 路由）
    //   与共享图集（atlasSource = resourcePack.atlasSource → pack 切换即时刷新预览）。
    //   仅依赖 QtQuick / QtQuick3D（无特殊模块），直接实例化（同 Inventory / SurvivalInventory 模式）。
    ResourceBrowser {
        anchors.fill: parent
        hotbar: hotbarVM
        atlasSource: resourcePack.atlasSource
        packActive: resourcePack.active
        // 生物图鉴 / 生物蛋 3D 预览：注入整 ResourcePackManager 实例（调 mobTextureSource(mobType) + 读 .active）。
        resourcePack: resourcePack
        visible: window.appState === "playing" && window.resourceBrowserOpen
        z: 160
        onClosed: window.resourceBrowserOpen = false
    }
    // pause-menu 进度面板（行2「进度」按钮 → progressOpen）：成就依赖树（progress.achievements() 返回
    //   [{id,name,desc,unlocked,parentId,parentName,depth,locked}] → Repeater delegate 每成就一行）。
    //   progress-tree 三轮：树形显示——根成就（depth=0）顶格平铺；子成就按 depth 缩进 + 左缘竖脊线 +
    //   横接 tick（父→子依赖连线，机制等价 MC 1.0 advancement tree 的树形进度）。三态图标：
    //   ✓ 已解锁（绿）/ ○ 可解锁未解锁（灰）/ 🔒 locked 父未解锁（更暗 + 描述附「（需先完成：父名）」）。
    //   revision 触碰刷新：model 表达式显式读 progress.revision 且 revision 参与返回值（_r>=0 守卫恒真），
    //   防 qmlcachegen AOT 把裸触碰读当死代码消除 → 面板即时刷新（修复「获得成就却显示未解锁」）。
    //   同设置面板模式（半透遮罩 + 居中 Rectangle + 返回按钮 + Esc 关）。仅 playing && progressOpen 显；
    //   z=155（高于暂停 100，低于死亡 180）。纯呈现（PLAN §2 UI 层），§9 GUI 自绘原创。
    //   成就名 / 描述均来自 Game 层 PlayerProgress（不引 MC 专名）。
    Item {
        id: progressOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && window.progressOpen
        z: 155
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.7)
            MouseArea { anchors.fill: parent; onClicked: {} } // 吸收点击，不穿透到背后暂停叠层
        }
        Rectangle {
            width: 460; height: 540; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                Text { text: "进度（成就）"; color: "#eeeeee"; font.pixelSize: 20; font.bold: true
                       anchors.horizontalCenter: parent.horizontalCenter }
                // 成就列表（Flickable 容滚动，防成就数多超面板高）。
                Flickable {
                    width: parent.width
                    height: parent.height - 32 /*标题*/ - 50 /*返回*/ - 16 /*间距*/
                    contentHeight: achList.height
                    contentWidth: width
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: achList
                        width: parent.width
                        spacing: 6
                        // revision 触碰：progress 任一统计 / 成就变更 → revision bump → 此 Repeater 重算
                        //   achievements()。QML 绑定 model 触碰 progress.revision（property 读）才会在
                        //   progressChanged 时重算函数调用（纯函数调用不自动建依赖），故 model 表达式显式读 revision。
                        Repeater {
                            // 触碰 revision：表达式先读 progress.revision（建依赖），且 revision **参与返回值**
                            //   （_r >= 0 守卫恒真）——防 qmlcachegen AOT 把裸触碰读当死代码消除 → 依赖不注册
                            //   → revision 变后 model 永不重算、面板停留首值（「获得成就却显示未解锁」根因）。
                            //   progressChanged → revision 变 → 此绑定重算 → achievements() 取最新（含解锁态）。
                            model: { const _r = progress.revision; return _r >= 0 ? progress.achievements() : [] }
                            delegate: Rectangle {
                                width: achList.width
                                height: achRow.implicitHeight + 16
                                radius: 6
                                // 三态底色：已解锁 → 绿底；locked（父未解锁）→ 更暗底；可解锁未解锁 → 暗底。
                                color: modelData.unlocked ? "#1a2a1a" : (modelData.locked ? "#181818" : "#222222")
                                border.color: modelData.unlocked ? "#3a6a3a" : "#3a3a3a"; border.width: 1
                                Item {
                                    id: achBody
                                    anchors.fill: parent; anchors.margins: 8
                                    // 依赖树缩进 = depth × 16（根成就 depth=0 顶格；子成就逐级右移）。
                                    readonly property real indent: modelData.depth * 16
                                    // 连线层（progress-tree 三轮）：竖脊线 = 同父兄弟连成一根纵线；横接 tick =
                                    //   父 → 子指向。根成就（depth=0）不画。暗绿线色。
                                    Rectangle {
                                        visible: modelData.depth > 0
                                        x: achBody.indent - 12; y: 0
                                        width: 2; height: parent.height
                                        color: "#4a5a4a"
                                    }
                                    Rectangle {
                                        visible: modelData.depth > 0
                                        x: achBody.indent - 12
                                        y: parent.height / 2 - 1
                                        width: 12; height: 2
                                        color: "#4a5a4a"
                                    }
                                    Row {
                                        id: achRow
                                        anchors.fill: parent
                                        anchors.leftMargin: achBody.indent
                                        spacing: 10
                                        // 三态图标：✓ 已解锁（绿）/ ○ 可解锁未解锁（灰）/ 🔒 locked（父未解锁，暗灰）。
                                        Text {
                                            text: modelData.unlocked ? "✓" : (modelData.locked ? "🔒" : "○")
                                            color: modelData.unlocked ? "#7fe57f" : (modelData.locked ? "#666666" : "#8a8a8a")
                                            font.pixelSize: 18; font.bold: true
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 28
                                            spacing: 2
                                            Text {
                                                text: modelData.name
                                                color: modelData.unlocked ? "#e0e0e0" : (modelData.locked ? "#777777" : "#999999")
                                                font.pixelSize: 14; font.bold: true
                                            }
                                            Text {
                                                // locked 成就描述追加前置依赖提示（父名），指引玩家先做父成就。
                                                text: modelData.desc + (modelData.locked && modelData.parentName
                                                      ? "（需先完成：" + modelData.parentName + "）" : "")
                                                color: modelData.unlocked ? "#bbbbbb" : (modelData.locked ? "#5a5a5a" : "#777777")
                                                font.pixelSize: 11
                                                wrapMode: Text.WordWrap; width: parent.width
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // 返回按钮：关进度面板回暂停菜单。
                Rectangle {
                    width: 120; height: 32; radius: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: backProgressArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                    border.color: "#3a5a7a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "返回"
                           color: "#7fb0e5"; font.pixelSize: 13 }
                    MouseArea {
                        id: backProgressArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.progressOpen = false
                    }
                }
            }
        }
    }
    // pause-menu 统计面板（行2「统计」按钮 → statsOpen）：显统计列表（progress.statsList() 返回
    //   [{name,value}] → Repeater delegate 每行 name: value）。revision 触碰刷新。同进度面板模式。纯呈现。
    Item {
        id: statsOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && window.statsOpen
        z: 155
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.7)
            MouseArea { anchors.fill: parent; onClicked: {} } // 吸收点击，不穿透到背后暂停叠层
        }
        Rectangle {
            width: 400; height: 460; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.fill: parent; anchors.margins: 20; spacing: 8
                Text { text: "统计"; color: "#eeeeee"; font.pixelSize: 20; font.bold: true
                       anchors.horizontalCenter: parent.horizontalCenter }
                // 统计列表（Flickable 容滚动）。
                Flickable {
                    width: parent.width
                    height: parent.height - 32 /*标题*/ - 50 /*返回*/ - 16 /*间距*/
                    contentHeight: statsListCol.height
                    contentWidth: width
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: statsListCol
                        width: parent.width
                        spacing: 4
                        Repeater {
                            // 触碰 revision：model 表达式显式读 progress.revision（建依赖），progressChanged →
                            //   revision 变 → 绑定重算 statsList() 取最新。qml-touch 三轮：`_r` 必须参与返回
                            //   （`_r >= 0` 恒真守卫）防 qmlcachegen AOT 把裸读当死代码消除（进度面板 model 同坑）。
                            model: { const _r = progress.revision; return _r >= 0 ? progress.statsList() : [] }
                            delegate: Rectangle {
                                width: statsListCol.width
                                height: 28
                                radius: 4
                                color: "#262626"
                                border.color: "#333333"; border.width: 1
                                Text {
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name + "："
                                    color: "#bbbbbb"; font.pixelSize: 13
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.value
                                    color: "#7fb0e5"; font.pixelSize: 13; font.bold: true
                                }
                            }
                        }
                    }
                }
                // 返回按钮：关统计面板回暂停菜单。
                Rectangle {
                    width: 120; height: 32; radius: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: backStatsArea.containsMouse ? "#2a3a4a" : "#1a2a3a"
                    border.color: "#3a5a7a"; border.width: 1
                    Text { anchors.centerIn: parent; text: "返回"
                           color: "#7fb0e5"; font.pixelSize: 13 }
                    MouseArea {
                        id: backStatsArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.statsOpen = false
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
        // t358：容器高度 = 可见行数 × 行高。**Item 无显式 height 默认 0**，而 ListView 用 anchors.fill + clip:true
        //   → 视口恒 0 → **所有**聊天行（含死亡播报）永远不可见（t327 只修了 visible/z，但 0 高容器下无论
        //   visible/z/opacity 都画不出像素；这是死亡播报「仍只在死亡屏、不在聊天栏」的真正根因）。补 height 让
        //   下文 chatVisibleLines/chatLineHeight（此前声明却从未被引用 = 本就为定高度而设）真正生效。
        height: chatVisibleLines * chatLineHeight
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
            // t494 修「聊天消息从上往下堆叠」：旧 `verticalLayoutDirection: BottomToTop` 的 QML 语义是 **index 0 在底、
            //   列表向上长** → 新 append（最高 index）落在**顶部**，用户看到新消息从顶往下叠（与 MC 聊天相反）。
            //   改回默认 TopToBottom（index 0 在顶、新 append 在底）+ positionViewAtEnd 滚到底 → 最新在底部
            //   （贴近 input 位置 / 符合 MC 聊天新消息在底部往上堆的观感）。
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
            //   t347 Up/Down 翻命令历史（spec t347）：置 autoRepeat 守卫之前，支持长按连续翻；Enter/Esc 仍受
            //     autoRepeat 抑制（防长按重复发送 / 反复开关）。
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Up) {
                    window.browseHistory(-1)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Down) {
                    window.browseHistory(1)
                    event.accepted = true
                    return
                }
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

    // t346 `/give` 参数提示行（仅 chatOpen 且输入为 /give 命令时显）：位于输入栏正下方，灰黄小字 + 黑描边。
    //   纯呈现（PLAN §2 UI 层），文案由 giveHintText 依 chatInput.text 实时算（binding 随键入自动刷新）。
    Text {
        visible: window.appState === "playing" && window.chatOpen
                 && giveHintText(chatInput.text).length > 0
        anchors.left: chatInputBar.left
        anchors.leftMargin: 14
        anchors.top: chatInputBar.bottom
        anchors.topMargin: 3
        text: giveHintText(chatInput.text)
        color: "#e0d060"
        font.pixelSize: 12
        style: Text.Outline
        styleColor: "#000000"
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
        // perf-t520 节流：原绑定读 player.position/yaw/pitch/onGround + hotbarVM.nameAt/countAt → 60Hz 重算；
        //   现读 hudPosText 单一 string（由 f3RefreshTimer 10Hz 刷新），main thread binding 工作降 6×。
        text: window.hudPosText
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
    //
    // t464 诊断级增强（PLAN §2-F F3 + 帮验证 t437 性能修复）：
    //   - **entities 行（liveCount 三类 + t488 槽利用率）**：mobs/items/orbs 各自的**活体**计数（liveCount()）
    //     与**槽总数**（count = Repeater 高水位）。活体/槽比揭示 slot-reuse 高水位：实体爆发（TNT 爆炸 /
    //     刷怪）后 count 停在高水位不缩（t488 (a) 根因面），live/slots 接近 0 说明空槽 delegate 高占比 ——
    //     配合 t488 的 releaseSlot kind 中性化（空槽 Loader 卸载重子树）判断高水位是否还在拖累。
    //   - **game time（time HH:MM · day N · moon M）**：worldClock.dayPhase 派生 24h 制 HH:MM（phase 0=noon=12:00、
    //     0.25=dusk=18:00、0.5=midnight=00:00、0.75=dawn=06:00）+ worldClock.dayCount（完整天数）+ moonPhase（月相）。
    //   - **biome 行**：玩家**脚底所在格**的群系（theWorld.biomeIdAt(floor(feetX), floor(feetZ)) → 通用名 Plains/Hills/
    //     Desert/Forest/Snowy/Swamp）。仅消费 World 层 Q_INVOKABLE（不反向写；PLAN §2 分层：UI ← World 向下读）。
    //   不涉及 lighting / alphaMode（PLAN §2-H / t439-t442 不变量不动）。
    Text {
        visible: window.appState === "playing" && window.f3Visible
        x: 12; y: 62
        z: 50
        color: "#ffff00"                        // 单色（黄）+ 黑描边，亮/暗背景均高对比可读
        style: Text.Outline; styleColor: "#000000"
        font.pixelSize: 12; font.family: "monospace"
        // perf-t520 节流：原 text 绑定读 60Hz player.position/feetPosition/yaw/pitch/speed/onGround/hasHit/hitBlock +
        //   theWorld.biomeIdAt(...) Q_INVOKABLE + liveCount() ×3 Q_INVOKABLE + worldClock.dayPhase/skyLight（100Hz）→
        //   整块 ~30 行字符串每帧重算（每秒 ~60 次）。改读 window.f3Text 单一 string（由 f3RefreshTimer 10Hz 刷新，
        //   buildF3Text() 内读最新值），重算频率降 6×。F3 是调试叠层，100ms 延迟零影响。
        text: window.f3Text
    }

    // perf 帧时间分解叠层（FrameProfiler：C++ 各热路径 Scope 累加 → 每 ~1s flush 报告字符串）。
    //   tick 行 = 60Hz tickImpl 各阶段 ms/frame（env/item/xp/boat/mob/pickup/phys/ray/input）；
    //   win 行 = 1s 窗口内 mesh 总 ms（含 rebuild 次数）+ world tick 各总 ms；
    //   perf-t520 frame 行 = 帧时间切分桶（main_total / render_cpu，ms/frame）—— 区分 GUI 主线程 vs 渲染线程瓶颈：
    //     - main_total = frameSwapped 间隔（GUI 线程帧周期；含 sim + QML binding/scenegraph update + 同步等待）；
    //     - render_cpu = beforeRendering → afterRendering（渲染线程 CPU 侧编码 + GPU 提交阻塞；**非**真 GPU 时间，
    //       QtQuick3D 路径无公开 GPU 计时查询，render_cpu 含 GPU stall 但不等同纯 GPU 时间，已在报告中标注）。
    //     threaded render loop 下 frame ≈ max(main_total, render_cpu)：
    //       - main_total >> render_cpu → 主线程 bound（QML binding / 物理 tick / scene-graph update）；
    //       - render_cpu >> main_total → 渲染线程 bound（GPU 提交 / draw-call 多 / 渲染队列长）。
    //     max 一侧标 *（视觉提示瓶颈侧）。
    //   mob sub 行 = mob 桶拆分（ai/phys/hostile/spawn/loop）。
    //   诊断 <10 FPS 时读此叠层定位「每帧固定开销」花在哪（实体 tick / mesh 重建 / 物理 / QML binding / 渲染），
    //   不再猜。F3 关时不显；报告内容亦每秒落 logs/voxelsandbox.log（grep vo.prof）。
    Text {
        visible: window.appState === "playing" && window.f3Visible
        x: 12; y: 62 + 200   // 在主 F3 块下方（主块约 12 行 × ~16px）
        z: 50
        color: "#00ff88"
        style: Text.Outline; styleColor: "#000000"
        font.pixelSize: 11; font.family: "monospace"
        text: FrameProfiler.report   // 单例直接按类型名引用（QML_SINGLETON，不可在 QML 实例化）
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
                        visible: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.blockIdAt(index) !== 0) : false }
                        Image {
                            anchors.fill: parent
                            visible: { const _r = hotbarVM.slotRevision; const id = hotbarVM.blockIdAt(index)
                                       return _r >= 0 ? (!hotbarVM.isTool(id) && !hotbarVM.isMaterial(id)) : false }
                            source: { const _sr = hotbarVM.slotRevision; const _pa = resourcePack.active; return _sr >= 0 && _pa >= 0 ? (hotbarVM.iconSourceForBlock(hotbarVM.blockIdAt(index))) : "" }
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                        ToolIcon {
                            anchors.fill: parent
                            visible: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.isTool(hotbarVM.blockIdAt(index))) : false }
                            tier: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.toolTier(hotbarVM.blockIdAt(index))) : 0 }
                            toolType: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.toolType(hotbarVM.blockIdAt(index))) : 0 }
                        }
                        MaterialIcon {
                            anchors.fill: parent
                            visible: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.isMaterial(hotbarVM.blockIdAt(index))) : false }
                            materialId: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.blockIdAt(index)) : 0 }
                        }
                    }
                    // 栈数量（t32）：count>1 时右下角显数字（MC 风格：单件不显数）。触碰 slotRevision 刷新
                    // （countAt 是 Q_INVOKABLE，靠版本号触发）。白字黑描边保证亮/暗槽底均可读。
                    Text {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 3
                        anchors.bottomMargin: 1
                        visible: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.countAt(index) > 1) : false }
                        text: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.countAt(index)) : "" }
                        color: "#ffffff"
                        style: Text.Outline; styleColor: "#000000"
                        font.pixelSize: 14; font.bold: true
                    }

                    // t315 工具耐久条：槽底薄条，宽 ∝ remaining/max，色绿(>50%)/黄(20–50%)/红(<20%)。
                    //   仅「带耐久」物品（toolMaxDurability>0）且 remaining<max（满耐久不显条）时可见。触碰 slotRevision 令耐久
                    //   消耗后重算（durabilityAt / toolMaxDurability 是 Q_INVOKABLE，靠版本号触发）。机制等价
                    //   MC 1.0 工具耐久条（绿色随耗变黄转红、满耐久隐）；原创自绘 Rectangle，零 MC 资产（§9）。
                    //   t349：耐久条按「有无耐久」判（maxDur>0）而非 isTool 段 —— 显式含剪刀（toolType=Shears，maxDur=238，
                    //   t315 漏剪刀）；满耐久（curDur==maxDur）隐条，受损后绿/黄/红同其他工具。
                    Item {
                        id: durabilityBar
                        property int curDur: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.durabilityAt(index)) : 0 }
                        property int maxDur: { const _r = hotbarVM.slotRevision; return _r >= 0 ? (hotbarVM.toolMaxDurability(hotbarVM.blockIdAt(index))) : 0 }
                        property real ratio: maxDur > 0 ? curDur / maxDur : 0.0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 3
                        anchors.rightMargin: 3
                        anchors.bottomMargin: 2
                        height: 3
                        visible: { hotbarVM.slotRevision
                            // t349：按「有无耐久」（maxDur>0）判而非 isTool 段 —— 显式含剪刀（maxDur=238）；
                            //   未来任何带耐久物品亦自动显条。满耐久（curDur==maxDur）隐条（同 MC「满耐久不显」）。
                            return durabilityBar.maxDur > 0 && durabilityBar.curDur > 0 && durabilityBar.curDur < durabilityBar.maxDur }
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
                    const _r = hotbarVM.slotRevision
                    const id = hotbarBar.hoveredItemId
                    if (id === 0) return ""
                    const name = hotbarVM.nameForBlock(id)
                    // t349：按「有无耐久」（toolMaxDurability>0）判而非 isTool 段 —— 显式含剪刀（maxDur=238）；
                    //   非工具 / 材料段 maxDur=0 → 仅显名（无耐久行）。
                    if (hotbarVM.toolMaxDurability(id) <= 0) return _r >= 0 ? name : ""
                    const cur = hotbarBar.hoveredSlot >= 0 ? hotbarVM.durabilityAt(hotbarBar.hoveredSlot) : 0
                    const mx = hotbarVM.toolMaxDurability(id)
                    return _r >= 0 ? (name + "\n\n耐久: " + cur + "/" + mx) : ""
                }
                color: "#f2f2f2"
                font.pixelSize: 12
            }
        }
    }

    // t403 经验条（XP bar）+ 等级数：hotbar 正上方（hearts/hunger 行之下，MC 1.0 布局：hotbar → XP 条 → 心/饥饿）。
    //   经验值累积（playerState.xp，t402 经验球拾取）→ level 由总 xp 经 MC 曲线派生 → 条按 xpBarFraction
    //   （当前级进度 / 升下一级所需）填充。满 → 升级（level++ + 条分母跳到更大值，机制等价 MC 1.0）；
    //   每级所需 XP 单调递增（曲线见 PlayerState::xpNeedForLevel）。等级数仅 level>0 显（MC：0 级无数）。
    //   分层（PLAN §2）：呈现层只读 playerState.level / xpBarFraction，曲线 + level 派生全在 Game 层
    //   PlayerState（单一权威），绝不反向写。绿条 + 绿字自绘原创（§9 override (a) 非 MC GUI PNG）。
    //   t443 仅 Survival 显（spec「创造/观察者隐藏 XP 条」）：创造/观察者无生存经济，经验条无意义 →
    //     与心/饥饿条同仅在 Survival 显（player.mode === Survival 门控，同 vitalsBar）。
    Item {
        id: xpBar
        visible: window.appState === "playing"
                 && player.mode === PlayerController.Survival
        anchors.bottom: hotbarBar.top
        anchors.bottomMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: hotbarBar.width
        height: 9

        // 背景凹槽（深色底，凸显绿色填充）。
        Rectangle {
            anchors.fill: parent
            color: "#1f1f1f"
            opacity: 0.9
            border.color: "#3a3a3a"
            border.width: 1
        }
        // 绿色填充：宽度 ∝ xpBarFraction [0,1]（满 → 下一帧升级后分母变大、条「空」回小段）。
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            width: Math.max(0, parent.width - 2) * playerState.xpBarFraction
            color: "#7ee23a"
        }

        // 等级数（绿色描边，居中于条 → 半浮于条上方、半压在条上，MC 1.0 风格）。level=0 不显。
        Text {
            anchors.centerIn: parent
            visible: playerState.level > 0
            text: playerState.level
            color: "#7ee23a"
            style: Text.Outline
            styleColor: "#202020"
            font.pixelSize: 16
            font.bold: true
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
        anchors.bottom: xpBar.top
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

    // t508 骑船下船提示（spec「坐上船时物品栏上方提示按 shift 下船」）：玩家骑船时（player.boatManager.ridingIndex>=0）
    //   在 hotbar 上方（vitalsBar / xpBar 之上）居中显「按潜行键（Shift）下船」。触碰 boats.revision 令上下马瞬时刷新
    //   （tryMount / dismount / 撞毁都 bump revision）。纯 QtQuick Text 自绘（§9 override (a)），无 MC GUI PNG。
    //   Creative/Survival 均显（两种模式都骑船）；非骑乘 / 非游戏态不显。
    //   ridingBoat 先读进 property：boats.revision 写进三元表达式（非裸语句）建可靠 NOTIFY 依赖（ridingIndex 是
    //   Q_INVOKABLE 不被 NOTIFY 自动跟踪；且本节点是静态构建的 window 级属性，按 lessons-learned「静态节点绑定用
    //   表达式形式」铁律，revision 须参与值计算而非裸语句，否则漏注册 → 提示上下马不刷新）。
    property bool ridingBoat: boats.revision >= 0
                              ? (player.boatManager ? player.boatManager.ridingIndex() >= 0 : false)
                              : false
    // t565 骑矿车下车提示（同 ridingBoat 模式；Shift 按下沿 → PlayerController dismount）。
    //   revision 触碰建可靠 NOTIFY 依赖（ridingIndex 是 Q_INVOKABLE 不被 NOTIFY 自动跟踪；表达式形式铁律）。
    property bool ridingCart: carts.revision >= 0
                              ? (player.minecartManager ? player.minecartManager.ridingIndex() >= 0 : false)
                              : false
    // t566 复用船的下车提示节拍（5s 自动隐 + 重上重启）：任一骑乘（船 / 矿车）边沿驱动 dismountHintVisible /
    //   dismountHintTimer；文案统一「按潜行键（Shift）下X」（机制等价 MC 1.0 骑乘提示）。两个 property 的
    //   onChanged 各自驱动 → 船↔矿车换乘也触发显 / 计时（无缝）。
    onRidingCartChanged: {
        if (ridingCart) { dismountHintVisible = true; dismountHintTimer.restart() }
        else { dismountHintVisible = true; dismountHintTimer.stop() }
    }
    // t530 下船提示 ~5s 自动消失（机制等价 MC 1.0 骑船提示短暂出现；现常驻改为限时）：首次上船显提示 +
    //   dismountHintTimer 5s 后把 dismountHintVisible 置 false → 提示自动隐（玩家已知晓按键）。重新上船（ridingBoat
    //   false→true 边沿）→ restart 计时 + 提示再显 5s。下船（ridingBoat→false）→ 复位 true 备下次上船。
    //   模式仿 infoToastTimer（showInfoToast restart 延后 + 文案覆盖）；纯 QtQuick Timer + 属性（§9 override (a)）。
    property bool dismountHintVisible: true
    onRidingBoatChanged: {
        if (ridingBoat) { dismountHintVisible = true; dismountHintTimer.restart() }
        else { dismountHintVisible = true; dismountHintTimer.stop() }
    }
    Timer {
        id: dismountHintTimer
        interval: 5000
        repeat: false
        onTriggered: dismountHintVisible = false
    }
    Text {
        visible: window.appState === "playing" && (ridingBoat || ridingCart) && window.dismountHintVisible
        anchors.bottom: vitalsBar.top
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("按潜行键（Shift）下%1").arg(ridingCart ? "车" : "船")
        color: "#f2f2f2"
        style: Text.Outline
        styleColor: "#202020"
        font.pixelSize: 13
        font.bold: true
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

    // t345 护甲条（仅 Survival）：置 vitalsBar 上一行，**左对齐贴在生命心条正上方**（机制等价 MC 1.0：
    //   护甲条排在左侧心条上方）。spec「ARMOR BAR shows above the hearts row (icons filling with total armor)」。
    //   10 颗盾形图标，每盾 = 2 护甲点（满 20 = 钻石整套 = 10 盾全亮）；totalArmor 奇数 → 末盾半亮。
    //   显隐：仅 Survival 且 totalArmorPoints > 0（无装备时不显，机制等价 MC 无护甲不显护甲条）。
    //   复用 VitalIcon kind="armor"（自绘原创盾形 Canvas，§9 override (a) 非 MC GUI PNG）。
    //   分层（PLAN §2）：呈现层只读 hotbar.totalArmorPoints（ViewModel 据装备槽算），绝不反向写。
    Item {
        id: armorBar
        visible: window.appState === "playing"
                 && player.mode === PlayerController.Survival
                 && hotbarVM.totalArmorPoints > 0
        anchors.bottom: vitalsBar.top
        anchors.bottomMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        width: vitalsBar.width
        height: 18

        // 第 index 颗护甲盾的态：totalArmor - index*2 余额 ≥2 → full、≥1 → half、否则 empty。
        function levelForArmor(curValue, index) {
            const bal = curValue - index * 2
            return bal >= 2 ? 2 : (bal >= 1 ? 1 : 0)
        }

        // 左对齐贴心条正上方（与下方心 Row 同 anchors.left）。
        Row {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            spacing: 0
            Repeater {
                model: 10
                delegate: VitalIcon {
                    kind: "armor"
                    level: armorBar.levelForArmor(hotbarVM.totalArmorPoints, index)
                }
            }
        }
    }

    // t585 指南针/钟动画帧状态推送（4Hz 节流）：手持 HUD 表盘已删（用户否决），指南针/钟的信息改由
    //   物品图标本身表达——pack 的逐帧贴图 compass_NN/clock_NN 按状态选帧（机制等价 MC 1.0 compass/clock
    //   每帧 item 贴图：图标即仪表）。本 Timer 只在 playing 态跑，把「磁针指出生点的相对角」与「昼夜相位」
    //   换算成 0..1 环值推给 ResourcePackManager.updateAnimatedItemState：帧 index 变化才 ++animRevision
    //   → MaterialIcon packImg 绑定触碰 rp.animRevision 重查帧文件路径（全工程 hotbar/手持/掉落物/背包统一）。
    //   分层（PLAN §2）：出生点/视线/相位全在呈现层读取换算，Core 只收连续状态值（不持 Game 层引用）。
    //   帧序零位锚（指南针针指上帧 16/32、钟全昼帧 32/64=正午）在 Core 的 anchor01 单一权威，此处推原始值。
    Timer {
        interval: 250   // 4Hz 节流（帧切换无需每帧；MC compass/clock 视觉更新率也远低于 60Hz）
        running: window.appState === "playing" && resourcePack.active
        repeat: true
        onTriggered: {
            // 指南针环值：出生点方向相对玩家视线的顺时针角 / 360（0=出生点在正前 → 针指上帧）。
            //   方位角口径同 t567：atan2(x, -z)（+X 东 / -Z 北，0=北）。玩家恰在出生点上 → 0（中性位）。
            let compassF = 0
            const dx = player.spawnPoint.x - player.feetPosition.x
            const dz = player.spawnPoint.z - player.feetPosition.z
            if (Math.abs(dx) > 0.01 || Math.abs(dz) > 0.01) {
                const bearing = Math.atan2(dx, -dz)
                const lv = player.lookVector
                const facing = Math.atan2(lv.x, -lv.z)
                compassF = (bearing - facing) / (2.0 * Math.PI)
            }
            // 钟环值：昼夜相位原值（dayPhase 0=正午 / 0.5=子夜；正午帧锚在 Core anchor01）。
            resourcePack.updateAnimatedItemState(compassF, worldClock.dayPhase)
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

    // pause-menu 右下角 toast（成就解锁 + 「敬请期待」占位提示通用通道）：单一 string 缓存 infoToastText +
    //   infoToastVisible + Timer 3 秒清空。仅 playing 态显（菜单 / worldlist 不显）。z=170（高于暂停 / 背包 / 进度
    //   统计面板，低于死亡 180 / 主菜单 200 → 死亡时不挡死亡屏）。§9 GUI 自绘原创（Rectangle + Text，无 MC PNG）。
    Item {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 20
        anchors.bottomMargin: 80   // 避开 hotbar 底栏
        visible: window.appState === "playing" && window.infoToastVisible
        z: 170
        width: Math.min(360, infoToastLabel.implicitWidth + 32)
        height: infoToastLabel.implicitHeight + 20
        Rectangle {
            anchors.fill: parent
            radius: 8
            color: "#2a2a1a"
            border.color: "#6a5a2a"; border.width: 1
            opacity: 0.95
        }
        Text {
            id: infoToastLabel
            anchors.centerIn: parent
            text: window.infoToastText
            color: "#e5c07f"; font.pixelSize: 14; font.bold: true
            wrapMode: Text.WordWrap; width: 340
            horizontalAlignment: Text.AlignHCenter
        }
        // 3 秒后清空（连点不同源 → showInfoToast restart 延后 + 文案覆盖最新）。
        Timer {
            id: infoToastTimer
            interval: 3000
            onTriggered: window.infoToastVisible = false
        }
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
        player: player
        visible: window.appState === "playing" && window.inventoryOpen
                 && player.mode === PlayerController.Creative
        z: 150
        onClosed: window.closeInventory()
        // t356：创造拖出面板外丢弃（点遮罩区 / 拖出释放）→ 落地为实体（同生存 dropHeldCursor）。回归根因：
        //   t292 曾把创造 dismiss 统一改成「凭空消失」（heldBlock=0），t318 又把「调色板点原格归还」接到同一信号
        //   discardHeldRequested → 创造拖出 / 丢热键一并变虚空、丢不出物。t356 把两个意图拆开信号：拖出面板外 =
        //   丢世界实体（本处 dropHeldCursor）；调色板点原格 / 换拿归还 = 凭空消失（returnHeldToVoidRequested，见下）。
        //   分层（PLAN §2）：呈现层只发意图信号，是否落地由宿主定（此处一律 spawn 实体，与生存一致）。
        onDiscardHeldRequested: player.dropHeldCursor()
        // 右键逐个拖出 → 同生存 dropHeldCursorOne（丢 1 件到世界，余量留光标）。
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
        // t356：调色板「归还光标手持物到虚空」（点原格 t318 切换归还 / 换拿时旧物回虚空 t136）→ 凭空消失
        //   （heldBlock=0，同步清 count+durability；t292 创造无限源语义，不丢世界实体）。与拖出（=世界）区分。
        onReturnHeldToVoidRequested: hotbarVM.heldBlock = 0
        // t120：创造拿物品（调色板点击）→ 手弹跳（同生存拾取的手弹反馈，spec「创造拿物品到手也触发」）。
        onItemTaken: handPopAnim.start()
        // survival-tab 三轮：用户嫌「点生存模式物品栏 tab 就切换成生存模式」→ 该 tab 已改为创造背包内的分页
        //   （Inventory.qml currentTab===6 生存物品栏视图：护甲 + 合成占位 + 角色预览 + 3×9 主栏 + hotbar 行），
        //   不再发 switchToSurvivalRequested，宿主不再切模式：创造模式下 E 开背包也能直接看/操作生存背包
        //   （player.mode 保持不变）。Survival 模式 E 开背包仍走下方 SurvivalInventory（player.mode 绑定分流，
        //   不受本改动影响）。Inventory.qml 的 switchToSurvivalRequested 信号保留声明但已无 emit 路径，此处不再接。
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
        progress: progress
        player: player
        visible: window.appState === "playing" && window.inventoryOpen
                 && player.mode === PlayerController.Survival
        z: 150
        onClosed: window.closeInventory()
        // t49：拖出丢弃（手持物点遮罩区）→ player 把光标手持栈丢为前方实体（同 Q 丢弃）。
        onDiscardHeldRequested: player.dropHeldCursor()
        // t228：右键拖出 → 只丢 1 件（左键整栈走上面的 dropHeldCursor）。
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
        // t345 护甲装备 / 脱下 → 播装备音（spec「equip/unequip SOUND」；当前复用 playPlace 作过渡装备音，
        //   专用护甲音属后续资产任务 —— 程序合成 wav 接 build_sounds.py）。
        onArmorChanged: audio.playPlace(0)
    }

    // t50 工作台 3×3 合成面板：右键工作台方块打开（player.craftingTableOpened → openCraftingTable）。
    // 仅 playing && craftingTableOpen 时显（与背包面板互斥）。E/Esc/关闭信号关 → 宿主恢复 grab。
    // z 与背包面板一致（150），低于主菜单（200）；光标手持物浮动图标 z=300 仍在其上。合成检测 /
    // 栈操作经 hotbar VM（recipeMatch / addStack / setStack）；关包时 onVisibleChanged 归还合成栏。
    CraftingTableUI {
        id: craftingTablePanel
        anchors.fill: parent
        hotbar: hotbarVM
        progress: progress
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
        // t494 注入 PlayerController（setFurnaceLit）+ 熔炉格世界坐标（furnaceOpened 携坐标存 window.furnaceX/Y/Z）。
        player: player
        furnaceX: window.furnaceX
        furnaceY: window.furnaceY
        furnaceZ: window.furnaceZ
        // t177 二轮复盘 注入 FurnaceStore（按 furnaceX/Y/Z 寻址的 per-block 内容 + 冶炼进度）。
        furnaceStore: furnaceStore
        visible: window.appState === "playing" && window.furnaceOpen
        z: 150
        onClosed: window.closeFurnace()
        onDiscardHeldRequested: player.dropHeldCursor()
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
        // t402 冶炼取走产物 → 产经验球（spec「removing finished smelt item grants XP」）。球 spawn 在
        //   玩家脚位上方一格（玩家正在取产物 → 立刻被磁吸吸收；机制等价 MC 冶炼产经验给玩家）。铁锭 >
        //   木炭由 SmeltingRegistry 数据自然表达（FurnaceUI 据 smeltXpReward 算 amount）。单向事件流。
        onXpAwarded: {
            const fp = player.feetPosition
            xpOrbs.spawnOrb(Math.floor(fp.x), Math.floor(fp.y) + 1, Math.floor(fp.z), amount)
        }
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

    // t515 附魔台面板（工作台蓝本重做）：右键附魔台方块打开（player.enchantingTableOpened → openEnchantingTable）。
    //   仅 playing && enchantingTableOpen 时显（与背包 / 工作台 / 熔炉 / 箱子 / 铁砧面板互斥）。
    //   上方占位附魔功能区（消耗 XP/青金石沿用 t474，真附魔效果后补）+ 底部 3×9 主栏 + 9 hotbar 行（能放/取
    //   背包物品，物品移动经 InventoryOps 单一权威，同 CraftingTableUI / FurnaceUI / ChestUI 全套快捷操作）。
    //   PERF：所有显示绑定到 slotRevision / mainRevision / levelChanged（低频 NOTIFY），永不 per-frame。
    EnchantingTableUI {
        id: enchantingPanel
        anchors.fill: parent
        hotbar: hotbarVM
        playerState: playerState
        theWorld: theWorld
        enchantX: window.enchantX
        enchantY: window.enchantY
        enchantZ: window.enchantZ
        // t549：世界栅格编辑版本号（放 / 破方块自增）—— 触碰它驱动 bookshelfPower 绑定重算（书架检测）。
        worldEditRev: window.worldEditRev
        visible: window.appState === "playing" && window.enchantingTableOpen
        z: 150
        onClosed: window.closeEnchantingTable()
        onDiscardHeldRequested: player.dropHeldCursor()
        onDiscardHeldOneRequested: player.dropHeldCursorOne()
    }

    // t477 铁砧面板：右键铁砧方块打开（player.anvilOpened → openAnvil）。
    //   仅 playing && anvilOpen 时显（与背包 / 工作台 / 熔炉 / 箱子 / 附魔台面板互斥）。
    //   以选中 hotbar 槽为目标，三功能（修复 / 附魔合并 / 重命名）各消耗 XP 等级；每次成功操作调
    //   player.damageAnvil(anvilX/Y/Z) 推进铁砧损坏（~1/3 概率 +1 阶段；重损再损碎裂移除）。
    //   PERF：所有显示绑定到 slotRevision / selectedSlotChanged / levelChanged（低频 NOTIFY），永不 per-frame。
    AnvilUI {
        id: anvilPanel
        anchors.fill: parent
        hotbar: hotbarVM
        playerState: playerState
        player: player
        anvilX: window.anvilX
        anvilY: window.anvilY
        anvilZ: window.anvilZ
        visible: window.appState === "playing" && window.anvilOpen
        z: 150
        onClosed: window.closeAnvil()
    }

    // t517 发射器物品栏面板：右键发射器方块打开（player.dispenserOpened → openDispenser）。仅 playing &&
    //   dispenserOpen 时显（与背包 / 工作台 / 熔炉 / 箱子 / 附魔台 / 铁砧面板互斥）。E/Esc/关闭信号关 →
    //   宿主恢复 grab。3×3 发射器容器槽 + 主栏 / hotbar 共享 hotbar VM（t517 本轮内容暂存 QML 本地数组，
    //   per-block 存储后补）。z 与其它面板一致（150）；光标手持物浮动图标 z=300 仍在其上。
    DispenserUI {
        id: dispenserPanel
        anchors.fill: parent
        hotbar: hotbarVM
        // t542 注入 DispenserStore（按 dispenserX/Y/Z 寻址的 per-block 9 槽 3×3 内容）+ 发射器方块世界坐标
        //   （dispenserOpened 携坐标存 window.dispenserX/Y/Z）。修旧 bug（t517）：旧版 9 槽存 QML 本地数组 →
        //   全世界发射器共享一个物品栏、打掉不掉；现 per-block 按坐标寻址（同 ChestStore / FurnaceStore 模式）。
        dispenserStore: dispenserStore
        dispenserX: window.dispenserX
        dispenserY: window.dispenserY
        dispenserZ: window.dispenserZ
        visible: window.appState === "playing" && window.dispenserOpen
        z: 150
        onClosed: window.closeDispenser()
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
            progress.onPlayTimeTick(dt)           // progress 统计游戏时间 + 距离 flush
            progress.setDayCount(worldClock.dayCount)  // progress 统计天数（单调取值）
            // t185 水流蔓延 tick：WorldClock 每 100ms tick → 驱动 World.tickWaterFlow（内部节流到 ~0.3s
            //   把波前推进 1 格 → 1 格/tick 流动动画可见）。纯 QML 桥接（WorldClock 为 Game 层不 include World；
            //   QML 同时持二者向下合法，PLAN §2 分层不破）。tickWaterFlow 内部对 settled 流场（无变化）静默 → 无重建开销。
            theWorld.tickWaterFlow()
            // t343 岩浆流 tick：WorldClock 每 100ms tick → 驱动 World.tickLavaFlow（内部节流到 ~3s 把波前推进 1 格
            //   → 岩浆比水慢 ~30 倍的可见缓慢流动；更短扩散距离 3 格；无源再生）。末尾 ignite pass 焚毁邻岩浆木类。
            //   纯 QML 桥接（同 tickWaterFlow 模式）。稳态（worldgen 全源岩浆湖）静默 → 无重建开销。
            theWorld.tickLavaFlow()
            // t236 小麦作物生长 tick：WorldClock 每 100ms tick → 驱动 World.tickCropGrowth（内部节流到 ~每 2.5s
            //   做一次成长判定，作物据光强 + 耕地支撑 + 散布概率逐步升生长阶段）。纯 QML 桥接（WorldClock 为
            //   Game 层不 include World；QML 同时持二者向下合法，PLAN §2 分层不破）。tickCropGrowth 内部对稳态
            //   （全成熟 / 无作物 / 全暗）静默 → 无重建开销。
            theWorld.tickCropGrowth()
            // t406 甘蔗生长 tick：WorldClock 每 100ms tick → 驱动 World.tickSugarcaneGrowth（内部节流到 ~每 5s
            //   一窗，甘蔗柱基邻水 + 柱高<5 + 散布概率 → 在柱顶上方长一格；柱基不邻水永不长、达 5 格停长）。
            //   纯 QML 桥接（同 tickCropGrowth 模式，PLAN §2 分层不破）。稳态（无甘蔗 / 全满高 / 全不邻水）静默。
            theWorld.tickSugarcaneGrowth()
            // t406 耕地湿润复算 tick：WorldClock 每 100ms tick → 驱动 World.tickFarmlandHydration（内部节流到
            //   ~每 3s 一窗，复算各耕地格湿润等级 0..3，与存档不等才静默写 → 驱动 mesher 顶点色暗化重建，肉眼见
            //   近水耕地变深、远水渐干；亦兼容 t234 旧单 bit 存档自动迁移到 4 级编码）。纯 QML 桥接（同上）。
            theWorld.tickFarmlandHydration()
            // t305 树苗生长 tick：WorldClock 每 100ms tick → 驱动 World.tickSaplingGrowth（内部节流到 ~每 5s
            //   做一次成长判定，树苗据光强 + 草地/泥土支撑 + 主干列畅通 + 散布概率逐步长成完整橡树）。纯 QML
            //   桥接（同 tickCropGrowth 模式）。tickSaplingGrowth 内部对稳态（无树苗 / 全不满足）静默 → 无重建开销。
            theWorld.tickSaplingGrowth()
            // t514 浆果丛生长 tick：WorldClock 每 100ms tick → 驱动 World.tickSweetBerryBushGrowth（内部节流到 ~每 5s
            //   一窗，浆果丛据光强 + 透光土壤支撑 + 散布概率逐步升生长阶段 0→1→2，机制等价 MC 1.0 sweet berry bush
            //   random-tick 生长）。纯 QML 桥接（同 tickCropGrowth / tickSaplingGrowth 模式，PLAN §2 分层不破）。
            //   玩家种植落地 state=0 + 采摘降回 state=0 → 本 tick 把丛推回成熟（采→回 0→生长→成熟→可再采循环）。
            //   稳态（无丛 / 全成熟 / 全无土壤 / 全暗）静默 → 无重建开销。
            theWorld.tickSweetBerryBushGrowth()
            // t468 结冰 tick：WorldClock 每 100ms tick → 驱动 World.tickIceFreeze（内部节流到 ~每 5s 一窗，把
            //   Snowy 群系暴露天空的水源按散布概率冻结为 Ice，机制等价 MC 1.0 寒冷群系水变冰）。纯 QML 桥接
            //   （同 tickCropGrowth 模式，PLAN §2 分层不破）。稳态（无 Snowy 暴露水源 / 本窗散布落空）静默 → 无开销。
            theWorld.tickIceFreeze()
            // t495 冰融化 tick：WorldClock 每 100ms tick → 驱动 World.tickIceMelt（内部节流到 ~每 2s 一窗，把高亮邻
            //   （火把 / 燃烧熔炉 / 岩浆 / 火）旁的普通冰按散布概率融为水，机制等价 MC 1.0 冰受高方块光照射融化；
            //   仅普通冰 Ice，浮冰 / 蓝冰永不融）。纯 QML 桥接（同 tickIceFreeze 模式，PLAN §2 分层不破）。
            //   稳态（无冰 / 无高亮邻 / 本窗散布落空）静默 → 无开销。
            theWorld.tickIceMelt()
            // t325 树叶渐进消退 tick：WorldClock 每 100ms tick → 驱动 World.tickLeafDecay（内部节流到 ~每 0.4s
            //   开一窗，队列内每叶按散布概率 1%/窗独立判定是否消失 → 几何分布散布 ~30-90s 渐退，非瞬时全消；
            //   t379 在 t325 基础上放慢约 2.5×）。
            //   纯 QML 桥接（同 tickCropGrowth/tickSaplingGrowth 模式）。tickLeafDecay 内部对稳态（无失撑叶 /
            //   本窗无命中）静默 → 无写入、无重建开销。
            theWorld.tickLeafDecay()
            // t385 天气 tick：WorldClock 每 100ms tick → 驱动 World.tickWeather（按 dt 推进天气态剩余计时，
            //   归零即随机转换晴↔雨/雪/雷；态翻转 emit weatherChanged 驱动下方天空变暗 + 粒子切换）。
            //   纯 QML 桥接（同 tickWaterFlow / tickCropGrowth 模式，PLAN §2 分层不破）。计时未到零开销。
            theWorld.tickWeather(dt)
            // t384 云层漂移累积：单一时间权威（PLAN §2）—— 云缓慢漂移用世界时钟 tick 推进，不在 QML 另起 Timer。
            //   cloudDrift 在「一个 tile 世界宽」处取模回绕（贴图可无缝平铺 → 回绕点无接缝）。
            window.cloudDrift = (window.cloudDrift + dt * window.kCloudDriftSpeed) % window.kCloudTileWorld
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
        visible: (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen || window.chestOpen || window.enchantingTableOpen || window.anvilOpen || window.dispenserOpen) && hotbarVM.heldBlock !== 0
        enabled: false
        z: 300
        x: cursorTracker.point.position.x - 16
        y: cursorTracker.point.position.y - 16
        width: 32; height: 32
        Image {
            anchors.fill: parent
            visible: !hotbarVM.isTool(hotbarVM.heldBlock) && !hotbarVM.isMaterial(hotbarVM.heldBlock)
            source: { const _r = resourcePack.active; return _r >= 0 ? (hotbarVM.iconSourceForBlock(hotbarVM.heldBlock)) : "" }
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
