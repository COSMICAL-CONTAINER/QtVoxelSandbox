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

    // app 状态机（t17）：menu（启动首显主菜单）↔ playing（显 View3D/HUD + grab 指针）。
    // 初始 menu：启动不直接进游戏，先显主菜单（开始/退出）。准星/HUD 仅 playing 态显。
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
    // t139 ESC 设置菜单子态：仅在暂停叠层（playing 且 !captured）下有意义。暂停叠层「设置」按钮置
    //   true → 显设置面板（手臂调试 ArmSlider 等）覆盖在暂停叠层之上；「返回」按钮置 false 回暂停菜单。
    //   回主菜单 / 点击恢复游戏时一并复位。属纯呈现态，PLAN §2 分层（UI 层）。
    property bool settingsOpen: false

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
        if (inventoryPanel.visible)      return inventoryPanel.hoveredKey
        if (survivalPanel.visible)       return survivalPanel.hoveredKey
        return ""
    }

    // t129 临时调试：第一人称手臂 viewmodel 的角度 / 位置 window 级中转属性。t139 起 ESC
    //   暂停叠层「设置」面板的 ArmSlider 写这些属性 → viewModelHand（下方 PerspectiveCamera 子节点）绑定
    //   读取 → 手臂 baseTilt / position 实时变。默认值 = t122/t73 不穿模几何定下来的值
    //   （baseTilt 100°、position (0.20, 0.05, -0.15)）。
    //   ⚠️ position.z 拉到 < -0.3 会让手伸进前方实体方块（穿模，见 viewModelHand 注释 t52/t73）；
    //      baseTilt 大幅偏离 ±100 会破坏袖 / 手前后顺序。仅临时调试用，确定值后回填进 viewModelHand 默认。
    property real handBaseTilt: 100.0
    property real handPosX: 0.20
    property real handPosY: 0.05
    property real handPosZ: -0.15

    // 进入游戏：切 playing 态 + 锁定指针（隐藏光标）+ 焦点回键位层。
    function startGame() {
        appState = "playing"
        player.grab()
        keyInput.forceActiveFocus()
    }
    // 返回主菜单：先释放指针（恢复光标 + 清按住的按键），再切 menu 态。
    function returnToMenu() {
        inventoryOpen = false
        craftingTableOpen = false
        furnaceOpen = false
        settingsOpen = false           // t139：回菜单时关设置面板（防遗留）
        returnHeldToHotbar()           // t56：返回菜单前归还光标手持栈（防遗留 heldBlock）
        player.release()
        appState = "menu"
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
    function deathReturnToMenu() {
        playerState.respawn()   // 清 dead（visible 绑自动隐）+ 满血（防死亡态遗留到下次进游戏）
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
        const leftover = hotbarVM.addToAny(hotbarVM.heldBlock, hotbarVM.heldCount)
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
        else if (inventoryPanel.visible)     inventoryPanel.swapHoveredWithHotbar(hotbarIdx)
        else if (survivalPanel.visible)      survivalPanel.swapHoveredWithHotbar(hotbarIdx)
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

    // 单一体素世界（内部 3×3=9 chunk，世界 48×48×16；QML API 不变）：网格(ChunkGeometry)
    // 与物理(PlayerController)共用同一份栅格。
    World { id: theWorld; width: 48; depth: 48; height: 64; seed: 1337 } // t119：高度 16→64（地表 16..40 + 基岩底层）

    // 昼夜时钟（t09，PLAN §2-H）：~20 分钟周期的天光亮度乘子 lerp（**非**旋转方向光）。
    // dayPhase 0..1 循环（0=正午 / 0.5=子夜）；skyLight [0,1] 是纯函数派生的天光乘子，供下面
    // SceneEnvironment.clearColor 与 DirectionalLight.brightness lerp 昼(#9ec6e8/1.5)↔夜(#0b1026/0.25)。
    // 呈现层只读消费、绝不反向写时间（PLAN §2 分层）。F6 切调试加速（~30s 一周期）便于肉眼验收。
    WorldClock { id: worldClock }

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
        const b = 0.4 + (1.0 - 0.4) * m
        return Qt.rgba(b, b, b, 1.0)
    }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }

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

    // t89 / t118 音效（Core/Platform 层，miniaudio 封装）：破 / 放 / 挖 / 脚步 / 拾取 SFX，
    //   按方块材质分组（石/木/草/沙/叶）clip 池（spec「playBreak/playMining/playStep 按 group 选」）。
    //   触发由 Game 层信号发出（World::blockBroken/blockPlaced、PlayerController::miningParticle /
    //   itemPickedUp / walkPhaseChanged），呈现层经 Connections 转发到 playBreak/playPlace/playMining /
    //   playPickup/playStep（音频层只消费，PLAN §2 分层）。
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
        selectedBlock: hotbarVM.selectedBlockId
        selectedItem: hotbarVM.selectedItemId
    }

    // 玩家掉落伤害 → 生命（t22）：PlayerController 在 Survival 着地结算时发 fallDamageTaken(hp)，
    // 呈现层经此 Connections 路由到 PlayerState.takeDamage（与破/放信号→粒子同模式：Game 层发
    // 语义事件，呈现层只消费）。PlayerController 不持有 PlayerState，保持单向事件流、分层干净。
    Connections {
        target: player
        function onFallDamageTaken(hp) { playerState.takeDamage(hp) }
        // t35：生存破可掉落方块（drop=true）→ player 发 spawnItem → 转发到 manager 生成实体。
        // 创造 / 不可采掘时 player 不发本信号（无实体产出）。ViewModel 不持有 PlayerController，
        // 经 Connections 解耦（同 fallDamageTaken→PlayerState 模式；PLAN §2 分层）。
        // t64：spawnItem 信号带 count 参数（整栈丢弃为 1 实体；破块掉落走 BlockDef.dropCount）。
        function onSpawnItem(x, y, z, id, count) { itemEntities.spawnItem(x, y, z, id, count) }
        // t61：挖掘过程粒子 —— 生存累积挖掘时每跨一阶，player 发 miningParticle（被挖方块坐标+id），
        // 转发到 BlockParticles.burstMine（复用破块碎屑 emitter / 色逻辑 / 重力，少量迸发，进度反馈）。
        // 破块完成时的 +30% 大迸发仍由 onBlockBroken → burstBreak 驱动（burstBreak 已在此任务内 +30%）。
        // t118：miningParticle 同时驱动 playMining（每挥一次响 —— 信号本就是「stage 跨阶 = 一次挥击」
        // 的语义点，spec「miningParticle 每 stage 接音」）；id 给 AudioManager 按材质组选 mining clip。
        function onMiningParticle(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstMine(x, y, z, id)
            audio.playMining(id)
        }
        // t118：拾取掉落实体 → player 发 itemPickedUp(id, count) → 拾取音（pickup clip，不分材质）。
        // 信号在 pickupScan 实际入栈时（全 / 部分）才发；全满装不下不发（无伪触发）。机制等价 MC
        // 「拾起物品啵一声」。t120：同时启动 handPopAnim（手 Y 弹跳，音 + 手弹双反馈）。
        function onItemPickedUp(id, count) { audio.playPickup(); handPopAnim.start() }
        // t50：右键工作台 → player 发 craftingTableOpened → 开 3×3 合成面板（释放指针 / 关包互斥）。
        function onCraftingTableOpened() { window.openCraftingTable() }
        // t87：右键熔炉 → player 发 furnaceOpened → 开 FurnaceUI 冶炼面板（释放指针 / 关包互斥）。
        function onFurnaceOpened() { window.openFurnace() }
        // t23/t24：背包打开时按 G 循环切模式 —— 切到观察者（无背包）则关闭；Creative↔Survival 间切换
        // 则保留背包打开，面板由各组件 visible 绑定 player.mode 自动换（创造背包↔生存背包）。避免任一
        // 背包在不兼容模式下滞留（Spectator 无背包/破放，t21）。
        function onModeChanged() {
            if (window.inventoryOpen && player.mode === PlayerController.Spectator)
                window.closeInventory()
            // t32：按模式重置 hotbar 栈内容（创造=8 方块满栈 / 生存=全空；观察者不动）。spec 验收
            // 「创造初始满、生存初始全空」。无持久化前（t36+ 存档/拾取），切模式即重置为新模式的默认态。
            hotbarVM.resetForMode(player.mode)
        }
    }

    // t89 脚步音节律（PLAN §2 分层：Game 层 walkPhase 信号驱动，音频层只消费）。
    //   walkPhaseChanged 仅在走时（moveSpeed>0.1）发 —— Survival / Creative-未飞 按住 WASD 才推进；
    //   飞行 / Spectator moveSpeed=0 不发（playercontroller.cpp:805 守），故天然无脚步音（spec 验收项）。
    //   半步节律：一个完整 stride = 2π 含两次脚落地（左+右），故每累积 Δphase≥π 播一次脚步音。
    //   2π 回绕感知：phase 从 ~2π 跳回 0 时 raw delta<0 → 补 2π（playercontroller 在 >=2π 时减 2π 回绕）。
    //   t118：脚步音按脚下表面方块材质分流（spec「playStep 按 group 选」）。表面 = 脚底 -0.1 那一格
    //   （脚底 m_pos.y 减一点防恰在整数边界踩空 → 越界返 air → GroupDefault 兜底 Stone step，仍响）。
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
                // 脚下表面方块 id（材质组判定用）；越界 / air → 0 → GroupDefault 兜底 Stone step。
                const feet = player.feetPosition
                const sid = theWorld.blockAt(Math.floor(feet.x),
                                             Math.floor(feet.y - 0.1),
                                             Math.floor(feet.z))
                audio.playStep(sid)
            }
        }
    }

    View3D {
        anchors.fill: parent
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
                // t120 popY：拾取/拿取时整手 Y 弹跳（0→-0.08→0，~200ms；下方 handPopAnim 驱动）。
                //   叠加进 position.y → 手下沉一点再回位（「拿到东西手一沉」反馈）。与 swingAngle 正交：
                //   swing 改 eulerRotation.x（绕肩挥动）、pop 改 position.y（位移），互不干扰、可叠加
                //   （拾取时手不挥、破/放时手挥不弹）。
                position: Qt.vector3d(window.handPosX, window.handPosY + viewModelHand.popY, window.handPosZ)
                readonly property real baseTilt: window.handBaseTilt  // t129: 读 window 级（t139 起由 ESC 设置面板 ArmSlider 实时调）；默认 100.0 见 window.handBaseTilt 注释（t122 几何依据）
                property real swingAngle: 0.0          // 挥动增量（度）；0=静止。下挥=负（手往下/前劈），回位=0
                property real popY: 0.0                 // t120：拾取/拿取弹跳位移（Y）；0=静止，负=下沉
                eulerRotation: Qt.vector3d(viewModelHand.baseTilt + viewModelHand.swingAngle, 0, 0)

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
                // 手持方块（t73 可见性修复）：持有方块（selectedBlock≠0）时，手前显该方块。
                //   根因：旧 position(0,-0.1,0.01) scale0.12 被手臂(scale0.16/0.2)几何完全包住（手臂 z 范围
                //     [-0.16,0.16] 吞掉方块 z=0.01）+ 65°tilt 使 z=0.01 的前移失效 → 方块被手皮遮挡不可见。
                //   修：方块前移到 z=-0.11（脱离手段 z 包围 [-0.0425,0.0425]，方块 z 范围 [-0.17,-0.05] 全在手前）、
                //     scale 0.12（大于手段 0.085，更显眼）→ 方块整段在手前方，从相机侧可见、不被手皮遮挡。
                //   BlockCube + 共享图集 voxelAtlas → per-face 贴图（草顶/草侧…），复用地形贴图（零 MC 资产）。
                //   作 viewModelHand 子节点 → 随挥动同步运动（块在手中）。selectedBlock=0 时 BlockCube 兜底为
                //   Stone 但 Model.visible=false 不渲染（blockId 兜底仅防空 UV，不影响显隐）。
                Model {
                    visible: player.selectedBlock !== 0
                    geometry: BlockCube { blockId: player.selectedBlock }
                    position: Qt.vector3d(0, 0.02, -0.11)    // t91：Y 跟手上移（旧 -0.10→+0.02）；z=-0.11 仍全在手前
                    scale: Qt.vector3d(0.12, 0.12, 0.12)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
                    }
                }
                // 手持工具（t75 木镐 3D）：选中工具槽（isTool(selectedItem)）时，手前显镐形 3D。
                //   根因：旧分支只看 selectedBlock（工具槽→Air→selectedBlock=0）→ 选工具时无渲染分支 → 木镐不可见。
                //   修：加本分支，可见性读 isTool(player.selectedItem)（selectedItem 含工具段，selectedBlockId 不再
                //   把工具「归零隐藏」——放置语义仍走 selectedBlock=Air 不放置，互不干扰）。
                //   PickaxeGeometry 是纯实色体素几何（无贴图 / 无 alpha）→ 永不黑（修「工具贴图黑」根因）。
                //   baseColor 按 tier 着色（木镐褐 / 石镐灰 / 铁镐银白，同 2D ToolIcon 配色）。
                //   作 viewModelHand 子节点 → 随挥动同步运动（镐在手中）；eulerRotation 给对角手持姿态。
                Model {
                    visible: hotbarVM.isTool(player.selectedItem)
                    geometry: PickaxeGeometry {}
                    position: Qt.vector3d(0.02, 0.04, -0.22)    // t91：Y 跟手同 delta 上移（旧 -0.08→+0.04，保 +0.02 高于手段）；z=-0.22 脱离手臂 z 包围
                    scale: Qt.vector3d(0.42, 0.42, 0.42)
                    eulerRotation: Qt.vector3d(15, -20, -15)    // 对角手持（柄下右、镐头上左，类 MC 手持）
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColor: hotbarVM.toolTier(player.selectedItem) === 3 ? "#d8d8e6"   // 铁镐银白
                                 : hotbarVM.toolTier(player.selectedItem) === 2 ? "#9a9a9a"   // 石镐中灰
                                 : "#8a5a2e"                                                 // 木镐褐（默认 / tier 1）
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
        Model {
            visible: worldClock.sunDir.y > 0.0   // 太阳在地平线上才显（夜间隐藏）
            source: "#Sphere"
            position: {
                const eye = player.position
                const s = worldClock.sunDir
                return Qt.vector3d(eye.x + s.x * 40, eye.y + s.y * 40, eye.z + s.z * 40)
            }
            scale: Qt.vector3d(3.0, 3.0, 3.0)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#fff2cc"   // 暖白（淡黄白日色，原创纯色，非 MC 资产 §9(a)）
            }
        }

        // 共享图集纹理：3×3=9 个 per-chunk Model 共用一份 atlas（声明一次、按 id 引用）。
        Texture { id: voxelAtlas; source: "qrc:/textures/atlas.png"; generateMipmaps: false }

        // 挖掘裂纹 6 阶贴图（t34）：tools/build_cracks.py 程序生成（透明底 + 黑裂纹，§9a 自绘）。
        // 裂纹叠层 Model 据 player.miningStage（0..5）取对应 Texture 作 baseColorMap。
        // 索引即 stage：0=0% / 1=20% / 2=40% / 3=60% / 4=80% / 5=100%（progress 满 = 破）。
        Texture { id: crack0; source: "qrc:/textures/crack_0.png"; generateMipmaps: false }
        Texture { id: crack1; source: "qrc:/textures/crack_1.png"; generateMipmaps: false }
        Texture { id: crack2; source: "qrc:/textures/crack_2.png"; generateMipmaps: false }
        Texture { id: crack3; source: "qrc:/textures/crack_3.png"; generateMipmaps: false }
        Texture { id: crack4; source: "qrc:/textures/crack_4.png"; generateMipmaps: false }
        Texture { id: crack5; source: "qrc:/textures/crack_5.png"; generateMipmaps: false }

        // 每 chunk culled mesh（t03）：3×3=9 个 Model/ChunkGeometry，**直接作为 View3D 的 3D 场景
        // 子节点**（与原单 Model 同路径，渲染已验证可靠）。各 Model 摆到其 chunk 世界起点
        // (cx*16, 0, cz*16)；ChunkGeometry 产出该 chunk 的局部 culled mesh（顶点=chunk 局部坐标）。
        // 跨 chunk 边界面剔除经 world.blockAt 路由：相邻两 chunk 实体→共边面剔除（无夹层黑缝）、
        // 一侧空气→画出、世界越界=空气 → 3×3 肉眼无缝。
        // dirty 驱动：setBlock 经 ChunkManager 标目标 + 边界邻接 chunk dirty；worldChanged → 各
        // ChunkGeometry::onWorldChanged() 检 myChunk().dirty()，仅脏的重建并清脏（rebuild 次数 =
        // dirty chunk 数），非脏跳过。
        //
        // 注：不在此用 Repeater 创建 3D Model delegate——Repeater 是 QQuickItem，将其 3D Model
        // delegate 领养到非纯 3D 场景 parent 时会触发「Delegate must not be of Item type」告警且
        // 有成孤儿不渲染之虞（类 t16 Loader 3D 领养坑）。固定 3×3 用显式 9 Model 最稳；t07 放大到
        // 16×16 时再换 C++ 侧批量管理（ChunkMeshManager）或经场景 Node 领养的 Repeater 方案。
        Model { // chunk (0,0) → 世界 (0,0)
            position: Qt.vector3d(0, 0, 0)
            geometry: ChunkGeometry { id: geo00; world: theWorld; cx: 0; cz: 0; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
            Component.onCompleted: console.info("[t31] chunk(0,0) UP parent=" + parent + " (对照：已知可见)")
        }
        Model { // chunk (1,0) → 世界 (16,0)
            position: Qt.vector3d(16, 0, 0)
            geometry: ChunkGeometry { id: geo10; world: theWorld; cx: 1; cz: 0; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (2,0) → 世界 (32,0)
            position: Qt.vector3d(32, 0, 0)
            geometry: ChunkGeometry { id: geo20; world: theWorld; cx: 2; cz: 0; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (0,1) → 世界 (0,16)
            position: Qt.vector3d(0, 0, 16)
            geometry: ChunkGeometry { id: geo01; world: theWorld; cx: 0; cz: 1; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (1,1) → 世界 (16,16)
            position: Qt.vector3d(16, 0, 16)
            geometry: ChunkGeometry { id: geo11; world: theWorld; cx: 1; cz: 1; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (2,1) → 世界 (32,16)
            position: Qt.vector3d(32, 0, 16)
            geometry: ChunkGeometry { id: geo21; world: theWorld; cx: 2; cz: 1; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (0,2) → 世界 (0,32)
            position: Qt.vector3d(0, 0, 32)
            geometry: ChunkGeometry { id: geo02; world: theWorld; cx: 0; cz: 2; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (1,2) → 世界 (16,32)
            position: Qt.vector3d(16, 0, 32)
            geometry: ChunkGeometry { id: geo12; world: theWorld; cx: 1; cz: 2; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }
        Model { // chunk (2,2) → 世界 (32,32)
            position: Qt.vector3d(32, 0, 32)
            geometry: ChunkGeometry { id: geo22; world: theWorld; cx: 2; cz: 2; sunDir: worldClock.sunDir }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas; vertexColorsEnabled: true; baseColor: terrainLight(worldClock.skyLight) }
        }

        // 选中立方体框（射线选体 t04 / t52）：从「命中面方框 (WireSquare)」改为「整个立方体框」，
        // 12 棱包住命中方块 8 角。Model 摆到命中方块中心（hitBlock + 0.5）；几何本身 ±0.5 居中、
        // 几何对称、与朝向无关 → 无需 eulerRotation（不似 WireSquare 需按命中面法线旋转）。
        // 未命中 / 暂停（未捕获）时 hasHit=false → 隐藏。
        //
        // 放大系数 1.005（t76 收紧，原 1.02）：WireCube 几何 ±0.5，scale 1.02 → ±0.51 超出方块 0.01，
        // 既在近面与方块之间留出可见空隙、又使远侧棱线落入邻接方块单元内被其 depth 吞噬（邻块不透明面
        // 挡住棱）。1.005 → ±0.5025，仅微凸 0.0025（与 CrackBox 叠层同防 z-fight 量级），紧贴方块边、
        // 邻块不再吞噬远侧棱。
        Model {
            id: selectionBox
            visible: player.hasHit
            // t126：选中框按命中方块实际形状分流。Torch(id13) → 小立柱（scale 0.12/0.6/0.12 + 木柄
            //   位姿），其余 → 全格 1.005（原行为，见上方 1.005 放大系数注释）。Torch 分支镜像 torchHost
            //   delegate 的木柄 Model（同一份 computeTorchOrient + torchHandleLocalPos/Euler），故选中框
            //   贴合火把外缘、非全格。13 = BlockRegistry::Torch（魔法数与下方 onWorldChanged 火把校验同源）。
            //   分层（PLAN §2）：呈现层只读 World（blockAt/isCollidable），不写栅格。
            property int worldRev: 0   // 邻居破/放会改火把有效朝向 → worldChanged ++ 触发 torchOrient 重算
            Connections { target: theWorld; function onWorldChanged() { ++selectionBox.worldRev } }
            readonly property int hitId: player.hasHit ? theWorld.blockAt(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z) : 0
            readonly property bool isTorch: hitId === 13
            readonly property string torchOrient: {
                // Q_INVOKABLE（isCollidable/blockAt）无自带 QML 绑定依赖 → 显式读 worldRev，使
                //   worldChanged 后本绑定重算（邻居破/放会改火把有效朝向）。
                selectionBox.worldRev;
                return isTorch ? computeTorchOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z,
                                                    findTorchPrefOrient(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z))
                               : "up"
            }
            position: isTorch
                ? torchHandleWorldPos(player.hitBlock.x, player.hitBlock.y, player.hitBlock.z, torchOrient)
                : Qt.vector3d(player.hitBlock.x + 0.5, player.hitBlock.y + 0.5, player.hitBlock.z + 0.5)
            scale: isTorch ? Qt.vector3d(0.12, 0.6, 0.12) : Qt.vector3d(1.005, 1.005, 1.005)
            eulerRotation: isTorch ? torchHandleEuler(torchOrient) : Qt.vector3d(0, 0, 0)
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
                    Model {
                        visible: player.selectedBlock !== 0 && player.mode !== PlayerController.Spectator
                        geometry: BlockCube { blockId: player.selectedBlock }
                        position: Qt.vector3d(0, -0.55, -0.30)   // t72：移到手前方（手心前缘 z≈-0.125 前），不嵌进手里
                        scale: Qt.vector3d(0.22, 0.22, 0.22)
                        eulerRotation: Qt.vector3d(-12, 42, 0)   // t72：绕 Y ~42° 倾斜 + 微 pitch，像 MC 手持姿态
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                            opacity: playerModel.bodyOpacity
                        }
                    }
                    // 手持工具（t75 木镐 3D）：选中工具槽时，第三人称右手上显镐形 3D（同第一人称分支，
                    //   读 isTool(selectedItem)；不再 CrackBox 兜底）。PickaxeGeometry 纯实色体素 → 永不黑。
                    //   作 rightArmPivot 子节点 → 随右臂行走 / 挖掘挥臂同步（镐在手中）；握把（几何 y≈-0.45）
                    //   落在手位（rightArmPivot 本地 y≈-0.6），故 position.y=-0.55 使握把贴手心。
                    Model {
                        visible: hotbarVM.isTool(player.selectedItem) && player.mode !== PlayerController.Spectator
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
                    // 基准位置 + 物品 id + count：触碰 itemEntities.revision（Q_PROPERTY NOTIFY=entitiesChanged）
                    // 建立依赖。t36 removeAt 用 erase-shift（前移后续实体），revision 自增 → 本绑定重算
                    // → shift 后 delegate[k] 对齐新 entity[k] 的 pos/itemId/count（否则 posAt/itemIdAt/countAt
                    // 是 Q_INVOKABLE 不被 NOTIFY 跟踪、shift 后 delegate 显示陈旧数据）。外层 Node 持基准
                    // pos + 绕 Y 旋转。t64 加 count 触碰：部分拾取后 setCountAt bump revision → 数量重算。
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
                    //   Texture.flipV=true：sourceItem 的 QtQuick Item 以左上为原点，3D 纹理以左下为原点 →
                    //   不翻转会把「木棒从左下到右上」渲染成「从左上到右下」（镐头朝下），故 flipV。
                    //   分层（PLAN §2）：呈现层只消费 ItemEntityManager 数据；ToolIcon/MaterialIcon 是纯呈现
                    //   层 QML 自绘（§9a 原创），无 MC 资产 / 反向写栅格。

                    // 方块段：BlockCube 走图集 per-face UV（草顶 / 草侧 …）。
                    Model {
                        visible: !hotbarVM.isTool(entRoot.entId) && !hotbarVM.isMaterial(entRoot.entId)
                        geometry: BlockCube { blockId: entRoot.entId }
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                        }
                    }
                    // 工具段（t75 改用 PickaxeGeometry 3D 镐形，不再 CrackBox 兜底）：
                    //   旧 CrackBox + ToolIcon(透明底 RGB0) 贴图无 alphaCutoff → 透明底被当不透明黑 → 6 面黑立方体
                    //   （「工具贴图黑」根因）。PickaxeGeometry 是纯实色体素几何（无贴图 / 无 alpha）→ 永不黑。
                    //   baseColor 按 tier 着色（木镐褐 / 石镐灰 / 铁镐银白）；绕 Y 自转时正面恒有镐形可见。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId)
                        geometry: PickaxeGeometry {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: hotbarVM.toolTier(entRoot.entId) === 3 ? "#d8d8e6"
                                     : hotbarVM.toolTier(entRoot.entId) === 2 ? "#9a9a9a"
                                     : "#8a5a2e"
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
                    //   flipV 同上方（2D 左上原点 → 3D 左下原点）。
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
                            baseColorMap: Texture {
                                flipV: true
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
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(0.45, 0.45, 0.45)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#b0b0b0"   // 浅灰
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
                    // 触碰 revision 建立依赖（push 位移 / 重力下落 / FallingBlock 移除 bump revision → 位置 /
                    // 配色 / kind 重算）。t117 FallingBlock 着地 erase-shift 后 revision 自增 → delegate 对齐
                    // 新 entity 数据（同 itemEntities delegate 模式）。
                    position: { entityManager.revision; return entityManager.posAt(index) }
                    property int entKind: { entityManager.revision; return entityManager.kindAt(index) }
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
                    Model {
                        visible: entKind === EntityManager.FallingBlock
                        geometry: BlockCube { blockId: { entityManager.revision; return entityManager.blockIdAt(index) } }
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: voxelAtlas
                        }
                    }
                    // Mob（原 t95 测试生物）：UnitCube = ±0.5 居中单位立方体（与地形 / 线框 / 玩家模型同基准）。
                    //   scale=1（1×1×1）；NoLighting；baseColor = 实体配色（#ff5555 醒目纯色，spec「纯色突出」）。
                    Model {
                        visible: entKind === EntityManager.Mob
                        geometry: UnitCube {}
                        scale: Qt.vector3d(1.0, 1.0, 1.0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: { entityManager.revision; return entityManager.colorAt(index) }
                        }
                    }
                    // t116 F3+B mob 碰撞箱（spec「mob scale 1.0」+ 朝向箭头）：mob AABB = 1×1×1 立方（与 mob
                    //   Model scale 1.0 一致）。WireCube ±0.5 scale 1.01（+1% 外扩避与 mob 立方表面 z-fight——
                    //   共面时线被面遮挡看不见）。mob delegate Node 无旋转（仅 position）→ 子节点本地旋转 0 即
                    //   世界轴对齐。mob 无 yaw 朝向态（EntityManager 仅 pos/radius，无 orientation）→ 朝向箭头
                    //   固定指向 -Z（北，yaw=0）；spec「细 Model 绑 yaw」对无朝向实体退化为固定向。
                    //   分层（PLAN §2）：纯呈现层调试叠层，只读 delegate 位置（entityManager 数据）。
                    Model {
                        visible: window.showHitboxes
                        geometry: WireCube {}
                        scale: Qt.vector3d(1.01, 1.01, 1.01)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
                    }
                    Model {
                        visible: window.showHitboxes
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0, -0.25)     // 棒中心前移 0.25（半长）→ -Z 方向延伸 0.5
                        scale: Qt.vector3d(0.03, 0.03, 0.5)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                    }
                }
            }
        }

        // t88/t114 火把伪光源 + 异形模型：在每个火把方块位置渲染「木柄 + 火焰 + 光晕」三个 Model
        // （NoLighting 高 baseColor 暖色，**非** PointLight）。根因：lit 材质 / PointLight 在本场景不可行
        // （lessons-learned 红线「lit 材质不渲染」），故火把动态光源走伪光源——纯色自发光小立方 + 半透
        // 光晕，视觉读作「发光点」，不参与场景实际光照计算（真 flood-fill 方块光留 PLAN §M）。
        //
        // t114 异形：mesher 不再为火把画 1×1×1 立方面（chunkgeometry.cpp Torch 特例 continue）→ 火把外观
        // 全部由此 delegate 负责：木柄（细长棕立方）+ 火焰（暖白小立方 + 闪烁动画）+ 光晕（保留 t88 半透）。
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

            Repeater {
                model: torchPositions
                delegate: Node {
                    id: torchGlow
                    // 火把格底面中心（cell [x,x+1]×[y,y+1]×[z,z+1] 的底面中心）；子 Model 在此局部坐标内摆位。
                    position: Qt.vector3d(model.x + 0.5, model.y, model.z + 0.5)

                    // t114 朝向态：up=垂直插地；px/nx/pz/nz=横插 ±X/±Z 向（贴对应墙、柄伸向 cell 中央）。
                    //   t125：定向权威改为「玩家点击面」（prefOrient，由 placeBlock 经 torchPlaced 传入的命中面
                    //   外法线推导），recomputeOrient 优先采用之；旧固定优先级（下>-X>+X>-Z>+Z）仅作退化兜底。
                    property string orient: "up"

                    // t126 朝向逻辑抽出为顶层 computeTorchOrient（与选中框共用同一份判定，确保两者
                    //   orient 永远一致 → 选中框贴合火把实际形状）。语义不变（t125）：优先 prefOrient
                    //   （玩家点击面）、其支撑邻居仍实体即采用；否则按下 / 4 侧顺序首个实体邻居兜底；
                    //   全无实体则保留玩家意图方向（无 pop-off 机制，宁可按原朝向画也不突兀翻转）。
                    function recomputeOrient() {
                        torchGlow.orient = computeTorchOrient(model.x, model.y, model.z, model.prefOrient || "up")
                    }

                    // [lessons-learned] Repeater 创建的 3D delegate 默认 parent=null（孤儿不渲染），
                    // onCompleted 显式 reparent 进 torchHost + 首次算朝向（同 itemHost delegate 模式）。
                    Component.onCompleted: {
                        if (parent === null) parent = torchHost
                        torchGlow.recomputeOrient()
                    }

                    // t114：邻居破/放后火把朝向重算（worldChanged 信号）。多重 Connections 可同 target
                    // （主 onWorldChanged 在文件下方做火把列表清理，本处只刷朝向）。
                    Connections {
                        target: theWorld
                        function onWorldChanged() { torchGlow.recomputeOrient() }
                    }

                    // 木柄：细长棕立方（UnitCube scale 0.12×0.6×0.12；原木暗棕 #6b4f24，与木棒
                    // MaterialIcon 同色系）。竖直时贴 cell 底部上伸（柄中心 0.3、柄顶 0.6）；墙火把
                    // 旋转 60° 倾斜上伸（柄中心 Y=0.5、沿墙法线偏移 ±0.2 让柄根贴墙面；t132 自 90° 水平改）。
                    Model {
                        geometry: UnitCube {}
                        scale: Qt.vector3d(0.12, 0.6, 0.12)
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

                    // 火焰：暖白小立方（UnitCube scale ~0.18 + 闪烁动画；spec「scale 0.18 黄 + 闪」）。
                    // 摆在柄顶端（竖直时柄顶 Y=0.65；墙火把 60° 倾斜后柄末端，见 torchFlameLocalPos）。
                    Model {
                        id: torchFlame
                        geometry: UnitCube {}
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#fff4cc"   // 焰心暖白（spec「高 baseColor 暖色 #ffcc66」的更亮内核）
                        }
                        // t132：焰位改读 torchFlameLocalPos（柄 60° 倾斜后末端，非旧水平偏移 0.10/y=0.50）。
                        position: torchFlameLocalPos(torchGlow.orient)
                        // 闪烁：自定义 flickerS（标量）由 SequentialAnimation 循环驱动；scale 绑它派生
                        // （Y 轴略加长 = 火苗上窜感）。本工具链 Vector3DAnimation 未注册（运行期「is not a
                        // type」），故走「NumberAnimation on 标量属性 + scale 绑定」等价路径（与 cam.shakeYaw
                        // / itemHost bobY 同 NumberAnimation 模式）。
                        property real flickerS: 0.18
                        scale: Qt.vector3d(flickerS, flickerS * 1.08, flickerS)
                        SequentialAnimation on flickerS {
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.18; to: 0.21; duration: 110 }
                            NumberAnimation { from: 0.21; to: 0.16; duration: 150 }
                            NumberAnimation { from: 0.16; to: 0.19; duration: 90 }
                            NumberAnimation { from: 0.19; to: 0.18; duration: 130 }
                        }
                    }

                    // 光晕（保留 t88）：半透暖色立方包覆火焰（opacity<1 自动走透明混合 → 视觉发光晕染，
                    // 非 PointLight）。跟随火焰位置（绑 torchFlame.position）。
                    Model {
                        geometry: UnitCube {}
                        position: torchFlame.position
                        scale: Qt.vector3d(0.42, 0.42, 0.42)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColor: "#ffcc66"   // spec 暖色光晕
                            opacity: 0.32          // 半透（<1 触发透明混合，同观察者幽灵半透模式）
                        }
                    }
                }
            }
        }
    }

    // t88 火把位置列表（火把伪光源 Repeater 的 model；blockPlaced/blockBroken/worldChanged 维护）。
    ListModel { id: torchPositions }
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
        }
        // t78：死亡 → 释放指针（光标可见点死亡界面按钮）+ 关背包 / 工作台（防面板卡在死亡遮罩下）+
        //   归还光标手持栈（防遗留 heldBlock）。died 由 takeDamage 扣血到 ≤0 时发（仅 Survival 走此路径）。
        //   死亡界面 visible 绑 playerState.dead（deadChanged NOTIFY 自动显），无需在此手动切显隐。
        function onDied() {
            if (window.inventoryOpen) window.inventoryOpen = false
            if (window.craftingTableOpen) window.craftingTableOpen = false
            if (window.furnaceOpen) window.furnaceOpen = false
            window.returnHeldToHotbar()
            player.release()           // 释放指针 → 光标可见（点「立即重生 / 回主菜单」按钮）
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
            if (id === 13) removeTorchAt(x, y, z)
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
        }
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
    //   水平时沿墙法线偏移 ±0.20（柄端贴墙面、柄身伸向 cell 中央），Y=0.5。
    function torchHandleLocalPos(o) {
        switch (o) {
        case "up": return Qt.vector3d(0.0, 0.30, 0.0)
        case "px": return Qt.vector3d(-0.20, 0.50, 0.0) // 贴 -X 墙、柄伸 +X
        case "nx": return Qt.vector3d( 0.20, 0.50, 0.0) // 贴 +X 墙、柄伸 -X
        case "pz": return Qt.vector3d(0.0, 0.50, -0.20) // 贴 -Z 墙、柄伸 +Z
        case "nz": return Qt.vector3d(0.0, 0.50,  0.20) // 贴 +Z 墙、柄伸 -Z
        }
        return Qt.vector3d(0.0, 0.30, 0.0)
    }

    // 火把木柄世界位置 = cell 底面中心 + 局部位姿。供选中框 Model.position 直接绑定。
    function torchHandleWorldPos(x, y, z, o) {
        const lp = torchHandleLocalPos(o)
        return Qt.vector3d(x + 0.5 + lp.x, y + lp.y, z + 0.5 + lp.z)
    }

    // 火把木柄 euler 旋转：墙火把把竖柄（默认沿 +Y）自竖直倾 ~60°（**非** 90° 水平贴墙）——
    //   柄自墙根斜向上伸（柄端高于柄根，机制等价 MC 墙火把上倾），不再是水平贴墙。
    //   ±X 向：绕 Z 轴 ±60°；±Z 向：绕 X 轴 ±60°；up 不转。t132：原 ±90°（水平）改 ±60°（倾斜）。
    function torchHandleEuler(o) {
        switch (o) {
        case "px": return Qt.vector3d(0, 0, -60)
        case "nx": return Qt.vector3d(0, 0,  60)
        case "pz": return Qt.vector3d( 60, 0, 0)
        case "nz": return Qt.vector3d(-60, 0, 0)
        }
        return Qt.vector3d(0, 0, 0)
    }

    // 火把火焰局部位置（相对 cell 底面中心）= 木柄中心 + 沿「旋转后柄轴」半柄长 0.30 到柄末端。
    //   t132：柄改 60° 倾斜后，柄末端不再位于水平偏移 (±0.10, y=0.50)，而是上抬到 y=0.65、墙法线方向
    //   收敛到 ±0.06（柄根 ±0.20 + 0.30·sin60°≈0.26 沿墙法线分量 − 0.20 = 0.06；0.30·cos60°=0.15 抬升）。
    //   up：柄竖直，焰心略高于柄顶（0.65 vs 柄顶 0.60）让焰立方叠在柄顶端（不变）。
    function torchFlameLocalPos(o) {
        switch (o) {
        case "up": return Qt.vector3d(0.0, 0.65, 0.0)
        case "px": return Qt.vector3d( 0.06, 0.65, 0.0)
        case "nx": return Qt.vector3d(-0.06, 0.65, 0.0)
        case "pz": return Qt.vector3d(0.0, 0.65,  0.06)
        case "nz": return Qt.vector3d(0.0, 0.65, -0.06)
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
            for (let i = 0; i < torchPositions.count; ++i) {
                const e = torchPositions.get(i)
                if (e.x === x && e.y === y && e.z === z) return
            }
            torchPositions.append({x: x, y: y, z: z, prefOrient: orientFromNormal(nx, ny, nz)})
        }
    }

    // t117 沙子重力触发：查 (x,y,z) 是否为沙且下方空气 → 先把沙格置 air（经 World::setBlock 发 blockBroken
    //   递归触发上方沙链）再 spawn 下落方块实体。仅 id=8（BlockRegistry::Sand）参与；其余方块无重力。
    //   「先置 air 再 spawn」使链式塌落自然：setBlock(air) → blockBroken(x,y,z,Sand) → onBlockBroken 再查
    //   (x,y+1,z) 沙并递归 trigger（沙柱一次塌完，机制等价 MC 沙链）。
    //   分层（PLAN §2）：呈现层（Main.qml）消费 World 语义事件（blockPlaced/broken）→ EntityManager 生成
    //   实体；实体物理（重力 / 着地）由 Game/Entities 层 tick 自治（同 spawnItem→掉落物 模式），呈现层不反向写。
    function maybeTriggerFallingBlock(x, y, z) {
        if (y < 0 || y >= theWorld.height) return
        if (x < 0 || z < 0 || x >= theWorld.width || z >= theWorld.depth) return
        if (theWorld.blockAt(x, y, z) !== 8) return // 仅沙（BlockRegistry::Sand=8）
        if (y > 0 && theWorld.blockAt(x, y - 1, z) !== 0) return // 下方非空气 → 有支撑，不落
        // 下方空气 → 触发：先置 air（递归触发上方沙链），再 spawn 下落实体。
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
            // 背包（t18）：E 开关。Esc 在背包打开时关闭（captured=false 时 Esc 不被 C++ 事件过滤器
            // 拦截，落到 QML；captured=true 时 Esc 仍走 C++ → release → 暂停叠层，原行为不变）。
            // t50：工作台面板同样 E/Esc 关（与背包互斥）。t87：熔炉面板亦同（E / Esc 关）。
            if (e.key === Qt.Key_E && window.appState === "playing") {
                if (window.craftingTableOpen) window.closeCraftingTable()
                else if (window.furnaceOpen) window.closeFurnace()
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
            if (e.key === Qt.Key_F5) { player.cycleCamera(); e.accepted = true; return } // 相机模式循环（t27）
            if (e.key === Qt.Key_F6) { worldClock.toggleDebugFast(); e.accepted = true; return } // 昼夜调试加速（t09）
            if (e.key === Qt.Key_G) { player.cycleMode(); e.accepted = true; return }
            if (e.key === Qt.Key_Q) { player.dropHeld(); e.accepted = true; return }     // 丢弃手持 1 件（t36）

            // t110 背包开时的 Shift / 数字键守卫（spec「背包开时 Shift/数字键不透传到 player」+「数字键交换」）。
            //   根因：原代码无差别地把 Shift 与数字键透传给 player —— Shift 进 m_keys → 关包后 release 已清，
            //   但若「关包瞬间 Shift 仍按住」会重新捕获进 m_keys → 蹲下；数字键在背包开时仍改 selectedSlot，
            //   与背包内整理物品的语义冲突（MC 1.0 背包开时数字键 = 与该 hotbar 槽交换 hover 物品，非切选中）。
            const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen

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
                const bagOpen = window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen
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
                if (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen) return
                if (event.angleDelta.y > 0)      hotbarVM.scroll(-1) // 上滚 → 左移（下标-1，环绕）
                else if (event.angleDelta.y < 0) hotbarVM.scroll(1)  // 下滚 → 右移
            }
        }
    }

    // 暂停 / 未捕获 覆盖层（仅 playing 态）：点击任意处 → 进入（锁定指针）。
    // 主菜单态（appState="menu"）不显本叠层（由 MainMenu 覆盖全屏）。
    // 背包 / 工作台打开时（同为 !captured 态）抑制本叠层 —— 三者互斥，避免面板下面透出暂停叠层。
    // t78：死亡态（playerState.dead）也抑制本叠层 —— 死亡时同样 !captured，但应由死亡界面（z=180）接管，
    //   不让「点击恢复」的暂停叠层透出（死亡必须走按钮，不可点击恢复）。
    Item {
        id: pauseOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && !player.captured
                 && !window.inventoryOpen && !window.craftingTableOpen && !window.furnaceOpen
                 && !playerState.dead
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
                Text { text: "[F6] toggle fast day/night (" + (worldClock.debugFast ? "ON · ~30s" : "OFF · ~20min") + ")"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[F3] toggle debug overlay (fps / pos / chunks / vertices)   [F3+B] toggle hitboxes"
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
                    // 返回主菜单（playing ↔ menu 双向切换，t17）：消费点击，不冒泡到背景 grab。
                    Rectangle {
                        width: 150; height: 32; radius: 6
                        color: backMenuArea.containsMouse ? "#2a3a2a" : "#1a2a1a"
                        border.color: "#3a6a3a"; border.width: 1
                        Text { anchors.centerIn: parent; text: "Main Menu"
                               color: "#7fe57f"; font.pixelSize: 13 }
                        MouseArea {
                            id: backMenuArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.returnToMenu()
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
                width: 440; height: 320; radius: 10
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
                    Text { text: "⚠ position.z < -0.3 会穿模 / baseTilt 大幅偏离 ±100 破坏袖手顺序（仅临时调试）"
                           color: "#b08060"; font.pixelSize: 10; wrapMode: Text.WordWrap; width: parent.width }
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
    // t78 死亡界面（仅 Survival 血量 0 触发）：半透黑遮罩 + 「你死了」+ [立即重生] / [回主菜单]。
    //   触发链：PlayerState.takeDamage 扣血到 ≤0 → dead=true + emit died → 上方 Connections(onDied) 释放指针
    //   + 关背包/工作台；本叠层 visible 绑 playerState.dead（deadChanged NOTIFY 自动显隐）。
    //   z=180（高于暂停 100 / 背包 150，低于主菜单 200 → 「回主菜单」后 MainMenu 覆盖）。
    //   立即重生 = window.respawnPlayer()（满血 + 传回出生点 + 重新 grab）；回主菜单 = window.deathReturnToMenu()
    //   （清死亡态 + returnToMenu）。遮罩 MouseArea 仅吸收点击（死亡态不可点恢复，必须走按钮；§9 lessons
    //   「全屏遮罩 onClicked 会让点外部误关」 → 这里无 close 语义，纯吸收防穿透到背后游戏层）。
    //   分层（PLAN §2）：死亡态属 PlayerState（Game 层），呈现层只读消费 + 按钮调 Q_INVOKABLE（不反向写数值）。
    //   GUI 自绘原创（Rectangle 组合，无 MC GUI PNG；§9 override (a)）。
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
            width: 360; height: 210; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.centerIn: parent; spacing: 22
                Text {
                    text: "你死了"
                    color: "#ff5555"; font.pixelSize: 30; font.bold: true
                    anchors.horizontalCenter: parent.horizontalCenter
                    style: Text.Outline; styleColor: "#000000"
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
    // 占位字段（spec：不得伪造数字）：draw-call 当前 n/a —— QtQuick3D 路径不暴露逐帧 draw-call 计数，
    // 取值接口留给 t13 性能 benchmark（届时走 QSGRendererInterface / RHI stats）；工作线程 0/0 —— 当前
    // meshing 同步在 GUI 线程（onWorldChanged 内），无 worker 池，t13 线程化后填实数。
    Text {
        visible: window.appState === "playing" && window.f3Visible
        x: 12; y: 62
        z: 50
        color: "#ffff00"                        // 单色（黄）+ 黑描边，亮/暗背景均高对比可读
        style: Text.Outline; styleColor: "#000000"
        font.pixelSize: 12; font.family: "monospace"
        text: {
            // 9 chunk 的顶点 / 三角面汇总（触碰各 geo_NN.vertexCount 建立 meshRebuilt 依赖）。
            const vx = geo00.vertexCount + geo10.vertexCount + geo20.vertexCount
                     + geo01.vertexCount + geo11.vertexCount + geo21.vertexCount
                     + geo02.vertexCount + geo12.vertexCount + geo22.vertexCount
            const tr = geo00.triangleCount + geo10.triangleCount + geo20.triangleCount
                     + geo01.triangleCount + geo11.triangleCount + geo21.triangleCount
                     + geo02.triangleCount + geo12.triangleCount + geo22.triangleCount
            const modeName = player.mode === PlayerController.Spectator ? "SPECTATOR"
                           : player.mode === PlayerController.Creative ? "CREATIVE" : "SURVIVAL"
            const camName = player.cameraMode === PlayerController.FirstPerson ? "1st"
                          : player.cameraMode === PlayerController.ThirdPersonBack ? "3rd-back" : "3rd-front"
            // t51：移动态（walk/sprint/crouch）入 F3 叠层，便于核对疾跑 / 蹲下触发。
            const moveName = player.moveState === PlayerController.Sprint ? "sprint"
                           : player.moveState === PlayerController.Crouch ? "crouch" : "walk"
            const ncx = theWorld.chunksX, ncz = theWorld.chunksZ
            return "voxelsandbox  [F3 debug]"
                 + "\nfps: " + window.fps
                 + "\npos: " + player.position.x.toFixed(2) + "  " + player.position.y.toFixed(2) + "  " + player.position.z.toFixed(2)
                 + "  (feet " + player.feetPosition.x.toFixed(1) + "," + player.feetPosition.y.toFixed(1) + "," + player.feetPosition.z.toFixed(1) + ")"
                 + "\nyaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch) + "  look " + camName
                 + "\nmode: " + modeName + (player.flying ? " (fly)" : "") + "  move: " + moveName + "  ground: " + (player.onGround ? "yes" : "no")
                 + (player.hasHit ? "  hit: " + player.hitBlock.x + "," + player.hitBlock.y + "," + player.hitBlock.z : "  hit: -")
                 + "\nworld: " + theWorld.width + "×" + theWorld.depth + "×" + theWorld.height
                 + "  chunks: " + ncx + "×" + ncz + " = " + (ncx * ncz) + " (all meshed)"
                 + "\nvertices: " + vx + "  triangles: " + tr
                 + "\ndraw-calls: n/a (QtQuick3D path, see t13)  threads: 0/0 (sync meshing)"
                 + "\nday: phase " + worldClock.dayPhase.toFixed(2) + "  sky " + worldClock.skyLight.toFixed(2)
                 + (worldClock.debugFast ? "  (fast)" : "")
                 + "\n[B] hitboxes: " + (window.showHitboxes ? "ON" : "off")
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

    // 主菜单（t17）：启动首显（appState="menu"）；Start/Quit 信号连到 startGame / Qt.quit。
    // 仅 menu 态可见，z 高于暂停叠层（100）与所有 HUD，全屏覆盖。「退出」→ Qt.quit() 直接退进程。
    // 仅依赖 QtQuick（无特殊模块），直接实例化（非 Loader 隔离）——加载失败即 app 致命，故无需降级。
    MainMenu {
        id: mainMenu
        anchors.fill: parent
        visible: window.appState === "menu"
        z: 200
        onStartRequested: window.startGame()
        onQuitRequested: Qt.quit()
    }

    // 创造背包 1.0（t23，t18 升级）：可滚动全方块调色板 + 底部 9 槽 hotbar 栏（同步游戏内）+ 销毁槽。
    // 仅 playing && inventoryOpen && Creative 时显（t23/t24 E 键分流：Creative→本面板，Survival→生存背包，
    // Spectator 无反应）。z 高于暂停叠层(100)/HUD，低于主菜单(200)。方块集 / 图标 / 中文名 / 槽位改写
    // 全部经 hotbar VM（ViewModel 读 BlockRegistry）；本组件只做呈现 + 输入转发。关闭（closed 信号）→ 宿主恢复 grab。
    // 仅依赖 QtQuick（无特殊模块），直接实例化（非 Loader 隔离）。
    Inventory {
        id: inventoryPanel
        anchors.fill: parent
        hotbar: hotbarVM
        visible: window.appState === "playing" && window.inventoryOpen
                 && player.mode === PlayerController.Creative
        z: 150
        onClosed: window.closeInventory()
        // t49：拖出丢弃（手持物点遮罩区）→ player 把光标手持栈丢为前方实体（同 Q 丢弃）。
        onDiscardHeldRequested: player.dropHeldCursor()
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
    }

    // t87 冶炼 tick：WorldClock 每 100ms 发 ticked(0.1) → 转发到 furnacePanel.tick 推进冶炼。
    // 单一时间权威（PLAN §2）：所有按时间推进的子系统都消费 WorldClock，不在 QML 各自起 Timer。
    // tick 内自检无活干（无燃料 / 无输入）即静默 return，故常驻连接无开销。
    Connections {
        target: worldClock
        function onTicked(dt) { furnacePanel.tick(dt) }
    }

    // 光标位置追踪层（t107）：独立全屏层，z=250 高于所有背包/工作台/熔炉面板（z=150）。
    // 原 cursorTracker HoverHandler 放在 keyInput（z=0，anchors.fill 窗口）内 —— 面板 z=150 在其
    // 之上截断 hover：按下 TapHandler/DragHandler 期间 point.position 停在旧位 → 浮动手持图标偏离
    // 鼠标（拿起卡顿）。提到面板之上后 hover 不再被截断，光标实时跟鼠标。
    // HoverHandler 为被动追踪（passive grab），**不消费 press/click** —— 不抢下方槽 TapHandler/DragHandler
    // 的 grab（本 Item 无任何 press handler，点击穿透到面板）。注意：enabled:false 会连 HoverHandler 一起
    // 禁用，故本层保持默认 enabled；纯被动 hover 不阻断下方点击（与 z=300 浮动光标 enabled:false 同理）。
    Item {
        id: cursorTrackLayer
        anchors.fill: parent
        z: 250
        HoverHandler { id: cursorTracker }
    }

    // 光标手持物浮动图标（背包点击拾取后「拿在鼠标上」的物品栈；hotbarVM.heldBlock/heldCount 驱动）。
    // 仅背包 / 工作台打开且手持非空时显，z 最高（盖过背包面板 z=150）。位置跟随 cursorTracker（窗口坐标）。
    // t32：count>1 时右下角显数量（手持整栈移动时可见剩余数）。t33：手持工具 → ToolIcon 自绘（非 Image）；
    // t50：手持材料 → MaterialIcon 自绘（木棒）。t37：enabled:false 显式声明本 Item 不参与指针事件——
    // z=300 浮在面板(z=150)之上，若参与事件捕获会抢走下方槽位 TapHandler 的点击。纯呈现层。
    Item {
        visible: (window.inventoryOpen || window.craftingTableOpen || window.furnaceOpen) && hotbarVM.heldBlock !== 0
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
}
