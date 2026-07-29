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

    // app 状态机（t17）：menu（启动首显主菜单）↔ playing（显 View3D/HUD + grab 指针）。
    // 初始 menu：启动不直接进游戏，先显主菜单（开始/退出）。准星/HUD 仅 playing 态显。
    property string appState: "menu"

    // 背包子态（t18）：仅 playing 态有意义。开 → 释放指针（光标可见，可点格子，类暂停）；
    // 关 → 恢复 grab。Esc/E/点遮罩均可关。开时抑制暂停叠层（二者互斥：都是 !captured 态）。
    property bool inventoryOpen: false
    // t50 工作台子态：右键工作台方块 → player.craftingTableOpened → 显本面板（3×3 合成）+ 释放指针。
    // 与 inventoryOpen 互斥（关一个再开另一个）；E/Esc 关 → 恢复 grab。开时抑制暂停叠层。
    property bool craftingTableOpen: false

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
        returnHeldToHotbar()           // t56：返回菜单前归还光标手持栈（防遗留 heldBlock）
        player.release()
        appState = "menu"
    }
    // t56：把光标手持栈（heldBlock/heldCount）归还进 hotbar。关背包 / 工作台 / 返回菜单时调。
    //   根因：旧版关包只 returnCraftToHotbar（合成格），**不归还 heldBlock** → 用户从调色板拾取到光标后
    //   关包，heldBlock 残留为隐形孤儿（浮动图标仅背包开时显）。此后按 Q → dropHeld 读选中槽（空）→
    //   早退无丢弃（spec 现象「手持物按 Q 看不到丢弃 + 打开背包还在手上」：手上=heldBlock 残留）。
    //   修法：关包时 addStack 把 heldBlock 全部塞回 hotbar（addStack 优先选中槽 → 「入手」语义 →
    //   Q 能读到），再清空 heldBlock。hotbar 满（极端）则把余量丢弃为前方实体（dropHeldCursor，
    //   同拖出丢弃；不静默吞，§2-E）。
    function returnHeldToHotbar() {
        if (!hotbarVM.heldBlock || hotbarVM.heldCount <= 0) return
        const leftover = hotbarVM.addStack(hotbarVM.heldBlock, hotbarVM.heldCount)
        if (leftover > 0) {
            // hotbar 满：余量丢弃为实体（先把 heldCount 收到余量再 dropHeldCursor，它清 heldBlock + 发 spawnItem）。
            // 注：heldBlock/heldCount 是 Q_PROPERTY（WRITE setter），不能当函数调（.setHeldBlock(0) 会 TypeError），
            //   经赋值触发 setter。
            hotbarVM.heldCount = leftover
            player.dropHeldCursor()
        } else {
            hotbarVM.heldBlock = 0 // 全入 → 清光标手持栈（setHeldBlock(0) 同步清 count）
        }
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

    // 单一体素世界（内部 3×3=9 chunk，世界 48×48×16；QML API 不变）：网格(ChunkGeometry)
    // 与物理(PlayerController)共用同一份栅格。
    World { id: theWorld; width: 48; depth: 48; height: 16; seed: 1337 }

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

    // t47：夜间全屏 tint 叠层 alpha（spec 方案 A）。地形 chunk 用 NoLighting 自发光恒定 → t09 的
    //   天光/方向光 lerp 只改了天空 clearColor，地形亮度始终不变。在此叠一层半透深蓝遮罩，alpha 随
    //   天光乘子反向 lerp：m=1(正午)→0 全透、m=0(子夜)→0.6 把地形拉向夜色 #0b1026（仍可辨识轮廓，
    //   spec t09「黑夜仍可辨识地形轮廓」）。深蓝取夜空同色 → 夜间天空已是该色、混合不偏移，仅地形亮色被拉暗。
    function nightTintAlpha(m) { return 0.6 * (1.0 - m) }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }

    // 玩家状态（生命/饥饿，t22）：满血满饥初值。心/饥饿条读其 health/hunger；掉落伤害经
    // 下面的 Connections 路由到 takeDamage（呈现层只读，绝不反向写数值；PLAN §2 分层）。
    PlayerState { id: playerState }

    // 方块掉落实体管理器（t35）：生存破可掉落方块时在该格生成 item entity（旋转 / 浮动小方块
    // 图标），等 t36 拾取。纯数据持有（pos + itemId），呈现层（下方 View3D 的 Repeater）只读。
    // 触发由 PlayerController 发 spawnItem 信号，下面 Connections 转发到 spawnItem()（单向事件流）。
    ItemEntityManager { id: itemEntities }

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
        function onMiningParticle(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstMine(x, y, z, id)
        }
        // t50：右键工作台 → player 发 craftingTableOpened → 开 3×3 合成面板（释放指针 / 关包互斥）。
        function onCraftingTableOpened() { window.openCraftingTable() }
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
                if (player.cameraMode === PlayerController.ThirdPersonFront)
                    return Qt.vector3d(-player.pitch, player.yaw + 180, 0) // 回看正面：俯仰反向 + 偏航 +180
                return Qt.vector3d(player.pitch, player.yaw, 0)             // 第一人称 & 第三人称-后：朝前看
            }
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
                // t52 手不穿模（渲染于地形之上）：旧 pivot z=-0.4 + baseTilt 65° 使手指尖伸到相机本地
                //   z=-0.49，深于玩家 AABB 半宽 0.3（实体方块最近可到 z=-0.3）→ 手被前方邻块遮挡 / 穿进墙。
                //   修法：pivot 收到 z=-0.2、baseTilt 减到 30°（少前伸）→ 手中心相机本地 z∈[-0.25(静止),
                //   -0.13(挥峰)]，始终近于 0.3 → depth 测试恒胜地形 → 手恒在所有实体方块之前（不穿模）。
                //   缩放相应收小（近则显大）：0.16→0.09 保持视感尺寸；pivot.y 上移到 0.05 使手仍落视野下中。
                position: Qt.vector3d(0.35, -0.05, -0.4)
                readonly property real baseTilt: 65.0  // 静态前抬：手臂略前伸入视野（非纯下垂），更像持物姿态
                property real swingAngle: 0.0          // 挥动增量（度）；0=静止。下挥=负（手往下/前劈），回位=0
                eulerRotation: Qt.vector3d(viewModelHand.baseTilt + viewModelHand.swingAngle, 0, 0)

                // 手臂方块：从肩枢沿局部 -Y 延伸（手在下方），随 Node 一起绕肩枢旋转。
                Model {
                    geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 改自定义 UnitCube 几何（同地形/线框的已验证路径）
                    position: Qt.vector3d(0, -0.1, 0)
                    scale: Qt.vector3d(0.16, 0.2, 0.16)
                    // NoLighting：本工程所有可见 Model（地形/线框/粒子）均用 NoLighting——默认 lit
                    // PrincipledMaterial 在本场景不渲染（手因此「完全透明」不可见）。改 NoLighting 后可见。
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#caa472" }
                }
                // 手持方块（t52）：持有方块（selectedBlock≠0）时，手前显该方块小图标。
                //   BlockCube + 共享图集 voxelAtlas → per-face 贴图（草顶 / 草侧…），复用地形贴图（零 MC 资产）。
                //   作 viewModelHand 子节点 → 随挥动同步运动（块在手中）。本地 z=+0.01（向相机略移）→ 块在手前
                //   （相机侧），depth 比手皮更近 → 不被手皮遮挡。selectedBlock=0 时 BlockCube 兜底为 Stone 但
                //   Model.visible=false 不渲染（blockId 兜底仅防空 UV，不影响显隐）。
                Model {
                    visible: player.selectedBlock !== 0
                    geometry: BlockCube { blockId: player.selectedBlock }
                    position: Qt.vector3d(0, -0.1, 0.01)
                    scale: Qt.vector3d(0.12, 0.12, 0.12)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
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
        // 第三人称挖掘挥臂（t45）：mineBlend 0→1（前 80ms 抬臂）→ 0（后 160ms 回落），总 ~240ms。
        //   双臂枢轴的 eulerRotation 绑定读 mineBlend：>0 时双臂前抬 70°（覆盖行走摆臂），=0 时正常行走/中性。
        //   仅第三人称可见时显效（第一人称 playerModel.visible=false，动画仍跑但肉眼不见——无副作用）。
        SequentialAnimation {
            id: bodySwingAnim
            loops: 1
            NumberAnimation { target: playerModel; property: "mineBlend"; to: 1.0; duration: 80; easing.type: Easing.OutQuad }
            NumberAnimation { target: playerModel; property: "mineBlend"; to: 0.0; duration: 160; easing.type: Easing.InQuad }
        }

        // t09 昼夜：brightness 随天光乘子 lerp 1.5(昼)↔0.25(夜)；**方向固定不变**（PLAN §2-H：
        // 亮度乘子 lerp，非旋转方向光）。NoLighting 材质地形不受 DirectionalLight 影响（自发光恒定），
        // 故地形不会变暗——昼夜视觉由 sky clearColor 渲染；DirectionalLight 只影响场景内 lit 元素。
        DirectionalLight {
            eulerRotation.x: -40
            eulerRotation.y: -25
            brightness: dayNightBrightness(worldClock.skyLight)
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
            geometry: ChunkGeometry { id: geo00; world: theWorld; cx: 0; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
            Component.onCompleted: console.info("[t31] chunk(0,0) UP parent=" + parent + " (对照：已知可见)")
        }
        Model { // chunk (1,0) → 世界 (16,0)
            position: Qt.vector3d(16, 0, 0)
            geometry: ChunkGeometry { id: geo10; world: theWorld; cx: 1; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,0) → 世界 (32,0)
            position: Qt.vector3d(32, 0, 0)
            geometry: ChunkGeometry { id: geo20; world: theWorld; cx: 2; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (0,1) → 世界 (0,16)
            position: Qt.vector3d(0, 0, 16)
            geometry: ChunkGeometry { id: geo01; world: theWorld; cx: 0; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (1,1) → 世界 (16,16)
            position: Qt.vector3d(16, 0, 16)
            geometry: ChunkGeometry { id: geo11; world: theWorld; cx: 1; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,1) → 世界 (32,16)
            position: Qt.vector3d(32, 0, 16)
            geometry: ChunkGeometry { id: geo21; world: theWorld; cx: 2; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (0,2) → 世界 (0,32)
            position: Qt.vector3d(0, 0, 32)
            geometry: ChunkGeometry { id: geo02; world: theWorld; cx: 0; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (1,2) → 世界 (16,32)
            position: Qt.vector3d(16, 0, 32)
            geometry: ChunkGeometry { id: geo12; world: theWorld; cx: 1; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,2) → 世界 (32,32)
            position: Qt.vector3d(32, 0, 32)
            geometry: ChunkGeometry { id: geo22; world: theWorld; cx: 2; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }

        // 选中立方体框（射线选体 t04 / t52）：从「命中面方框 (WireSquare)」改为「整个立方体框」，
        // 12 棱包住命中方块 8 角。Model 摆到命中方块中心（hitBlock + 0.5），略放大 1.02 防 z-fight；
        // 几何对称、与朝向无关 → 无需 eulerRotation（不似 WireSquare 需按命中面法线旋转）。
        // 未命中 / 暂停（未捕获）时 hasHit=false → 隐藏。
        Model {
            visible: player.hasHit
            position: Qt.vector3d(player.hitBlock.x + 0.5,
                                  player.hitBlock.y + 0.5,
                                  player.hitBlock.z + 0.5)
            scale: Qt.vector3d(1.02, 1.02, 1.02)
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
        // 身体只随水平 yaw 转、不随 pitch 倾（抬头只动相机，不动身体）。
        Node {
            id: playerModel
            visible: player.cameraMode !== PlayerController.FirstPerson
            position: player.feetPosition
            eulerRotation: Qt.vector3d(0, player.yaw, 0)

            // 半透幽灵态（各 body Part 的材质读此属性）：观察者半透 0.35，其余不透明。
            readonly property real bodyOpacity: player.mode === PlayerController.Spectator ? 0.35 : 1.0

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

            // [t31] 诊断：确认本 Node 已加载、parent=场景节点（非 null 孤儿）、feetPosition 合法、visible 状态。
            // 打印到 voxelsandbox.log。若运行后日志无此行 → Main.qml 未进二进制（stale build）。
            Component.onCompleted: console.info("[t31] playerModel UP  parent=" + playerModel.parent
                + "  feet=" + player.feetPosition + "  vis=" + visible
                + "  cam=" + player.cameraMode + "  mode=" + player.mode)

            // 头（≈0.5³，肤色）。模型以脚底为原点（y=0 贴地），总高≈1.8 对齐玩家 AABB(kHeight)。
            Model {
                geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 改自定义 UnitCube 几何（同地形/线框的已验证路径）
                position: Qt.vector3d(0, 1.55, 0)
                scale: Qt.vector3d(0.5, 0.5, 0.5)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#caa472"; opacity: playerModel.bodyOpacity }
            }
            // 眼睛（t39 / t52 贴脸修正）：头部正面（朝 -Z = 玩家朝向；t04 约定 yaw=0 时前向 = (0,0,-1)）
            // 的两个对称小方块，使第三人称能看到「脸」。白眼底 (#e8e8e8) + 深色瞳 (#1a1a1a) 两层，原创纯色
            // （§9 override (a)，无 MC 皮肤）。随 playerModel 的 yaw 一起转（身体只水平转、不随 pitch 倾）。
            //
            // t52：旧 z=-0.30 / -0.31 让眼悬浮在头前 0.05（头半厚 0.25，头前面在 z=-0.25）→ 用户反馈「眼
            // 睛悬浮在头前」。改为贴脸：白眼底 z=-0.25（厚 0.02 → 前面 -0.26，略凸出 0.01），瞳 z=-0.26
            // （前面 -0.27，在白眼底前 0.01）。|z|≈头半径 0.25 → 贴在头表面而非外飘（spec「贴近头面」）。
            // （注：z 略小于头半径会陷进不透明头内被前面遮挡 → 必须在 z≤-0.25 才可见；故取 -0.25/-0.26。）
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 1.62, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8"; opacity: playerModel.bodyOpacity }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 1.62, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8"; opacity: playerModel.bodyOpacity }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 1.62, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a"; opacity: playerModel.bodyOpacity }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 1.62, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a"; opacity: playerModel.bodyOpacity }
            }
            // 躯干（上衣色；y 0.6→1.3，宽 0.5 深 0.3）
            Model {
                geometry: UnitCube {}   // t31：静态 #Cube 不渲染 → 改自定义 UnitCube 几何（同地形/线框的已验证路径）
                position: Qt.vector3d(0, 0.95, 0)
                scale: Qt.vector3d(0.5, 0.7, 0.3)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
            }
            // 左臂枢轴（t45 走 / t52 仅右手挖）：枢轴位于左肩 (-0.375, 1.3, 0)，袖+手作子节点本地 -Y 偏移
            //   （袖中心 -0.25、手中心 -0.6）→ 绕肩旋转时手在弧线末端摆动（自然关节运动，非整体平移）。
            //   静止时几何中心与重组前完全一致（袖 y=1.05、手 y=0.7），总高不变。
            //   摆动：行走时左臂与左腿反相（左腿前=−sin → 左臂后=+sin；对侧臂腿同相）。
            //   t52：挖掘/放置只动右手——左臂仅行走摆动，不读 mineBlend（旧版双臂同挖，用户反馈「双手都动」）。
            //   +eulerRotation.x = 臂尖前摆（-Y→-Z，朝玩家前向）。
            Node {
                id: leftArmPivot
                position: Qt.vector3d(-0.375, 1.3, 0)
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
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
                }
                // 手（肤色 #caa472；臂末端 y 0.6→0.8 = 肩下 0.5..0.7）
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.6, 0)
                    scale: Qt.vector3d(0.25, 0.2, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#caa472"; opacity: playerModel.bodyOpacity }
                }
            }
            // 右臂枢轴（t45 / t52 持方块）：与左臂对称（右肩 0.375, 1.3, 0），行走与左腿同相（−sin；左腿前则右臂前）。
            //   t52：挖掘/放置只动右手——仅右臂读 mineBlend（挖掘挥臂前抬 70°）；左臂已不读（见上）。
            Node {
                id: rightArmPivot
                position: Qt.vector3d(0.375, 1.3, 0)
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
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
                }
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.6, 0)
                    scale: Qt.vector3d(0.25, 0.2, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#caa472"; opacity: playerModel.bodyOpacity }
                }
                // 手持方块（t52）：持有方块（selectedBlock≠0）时，第三人称右手上显该方块小图标。
                //   作 rightArmPivot 子节点 → 随右臂行走 / 挖掘挥臂同步运动（块在手中，自然跟随）。
                //   BlockCube + 共享图集 → per-face 贴图（草顶 / 草侧…），复用地形贴图（零 MC 资产）。
                //   仅第三人称 + 非观察者显（第一人称见 viewModelHand 的手持；观察者无动作不持物）。
                Model {
                    visible: player.selectedBlock !== 0 && player.mode !== PlayerController.Spectator
                    geometry: BlockCube { blockId: player.selectedBlock }
                    position: Qt.vector3d(0, -0.6, -0.08)
                    scale: Qt.vector3d(0.2, 0.2, 0.2)
                    materials: PrincipledMaterial {
                        lighting: PrincipledMaterial.NoLighting
                        baseColorMap: voxelAtlas
                        opacity: playerModel.bodyOpacity
                    }
                }
            }
            // 左腿枢轴（t45）：枢轴位于左髋 (-0.125, 0.6, 0)，腿段作子节点本地 -Y 偏移（中心 -0.3）。
            //   行走与右臂同相（−sin → 与右腿反相；右腿前则左腿后）。+eulerRotation.x = 腿前摆。
            //   静止归零（walkBlend=0）；仅走路模式有 walkPhase 推进（Spectator/飞=0 → 腿不摆）。
            Node {
                id: leftLegPivot
                position: Qt.vector3d(-0.125, 0.6, 0)
                // t51：摆幅 ×swingAmp（疾跑大步 / 蹲下小步）。
                eulerRotation: Qt.vector3d(-Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp, 0, 0)
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.3, 0)
                    scale: Qt.vector3d(0.25, 0.6, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#3a3a5a"; opacity: playerModel.bodyOpacity }
                }
            }
            // 右腿枢轴（t45）：与左腿对称（右髋 0.125, 0.6, 0），行走与左臂同相（+sin）。
            Node {
                id: rightLegPivot
                position: Qt.vector3d(0.125, 0.6, 0)
                // t51：摆幅 ×swingAmp（与左腿对称；疾跑大步 / 蹲下小步）。
                eulerRotation: Qt.vector3d(Math.sin(player.walkPhase) * 28 * playerModel.walkBlend * playerModel.swingAmp, 0, 0)
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.3, 0)
                    scale: Qt.vector3d(0.25, 0.6, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#3a3a5a"; opacity: playerModel.bodyOpacity }
                }
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
                    //   - 工具段（isTool）→ CrackBox（每面全幅 UV 0..1）+ Texture.sourceItem = ToolIcon Canvas
                    //     （tier 据 entId 查 hotbarVM.toolTier；tier=1 木镐 / 2 石镐 / 3 铁镐 各自配色）；
                    //   - 材料段（isMaterial）→ CrackBox + Texture.sourceItem = MaterialIcon Canvas（木棒）。
                    //   CrackBox 每面独立全幅 UV → 把 sourceItem 渲出的整张 2D 图完整铺到该面；六面同图 →
                    //   绕 Y 自转时正面恒有图标可见（不靠 billboard）。
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
                    // 工具段：CrackBox + ToolIcon sourceItem（镐形按 tier 着色）。
                    Model {
                        visible: hotbarVM.isTool(entRoot.entId)
                        geometry: CrackBox {}
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
                            baseColorMap: Texture {
                                // flipV：见上 flipV 注释（Item 左上原点 vs 3D 纹理左下原点）。
                                flipV: true
                                sourceItem: ToolIcon {
                                    tier: hotbarVM.toolTier(entRoot.entId)
                                    width: 64; height: 64
                                }
                            }
                        }
                    }
                    // 材料段：CrackBox + MaterialIcon sourceItem（木棒棕色长条）。
                    Model {
                        visible: hotbarVM.isMaterial(entRoot.entId)
                        geometry: CrackBox {}
                        scale: Qt.vector3d(0.3, 0.3, 0.3)
                        position: Qt.vector3d(0, entRoot.bobY, 0)
                        materials: PrincipledMaterial {
                            lighting: PrincipledMaterial.NoLighting
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
    }

    // t47：昼夜影响地形光照（全屏 tint 叠层，spec 方案 A）。
    // 根因：地形 chunk 用 PrincipledMaterial.NoLighting（自发光恒定）→ t09 的环境光 lerp 只改了天空
    //   clearColor、DirectionalLight.brightness 对 NoLighting 材质无效 → 地形白天/黑夜亮度不变（用户反馈）。
    //   lit 材质在本场景实测不渲染（lessons-learned），故不换材质、改走全屏叠层：在 View3D（先绘制）之上、
    //   所有 HUD/背包/暂停叠层（后绘制）之前，叠一层半透深蓝 Rectangle，alpha 随天光乘子反向 lerp。
    //   深蓝取夜空同色 #0b1026：夜间天空已是该色、与之混合无额外偏移，仅地形亮色被拉向夜色（对症根因）。
    // 不影响 GUI 亮度（spec 验收）：本 Rectangle 无显式 z（=0）、声明在 View3D 之后、所有 HUD 之前 →
    //   同 z=0 的 HUD（准星/Hotbar/HUD Text）靠声明序在后绘制、盖在 tint 之上；显式 z 的叠层（F3 z=50、
    //   暂停 z=100、背包 z=150、菜单 z=200）更在其上 → 均不被压暗。无 MouseArea → 不拦截破/放/背包点击。
    // 仅 playing 态显（menu 被 MainMenu z=200 覆盖；显式 gate 更清晰、避免菜单态多余的叠层合成）。
    Rectangle {
        visible: window.appState === "playing"
        anchors.fill: parent
        color: Qt.rgba(0.043, 0.063, 0.149, nightTintAlpha(worldClock.skyLight))
    }

    // t51 受伤红色半透全屏闪烁叠层：PlayerState.takeDamage 实扣血时发 damaged(amount) → 本 Connections
    //   立即把 opacity 设到 0.4（满红），随后 NumberAnimation 用 ~600ms 淡出到 0（spec「红色半透闪烁」）。
    //   连击受伤：再次触发时 opacity 重置到 0.4 + 重启动画（damageFadeAnim.start() 重启进行中的动画）。
    //   z=250：高于 HUD / 背包面板（z=150）/ F3（z=50）/ 暂停（z=100），低于主菜单（z=200 仅 menu 态显，
    //   与本叠层 playing 态互斥）。enabled:false → 不拦截破/放/背包点击（纯呈现层）。
    //   分层（PLAN §2）：呈现层只消费 PlayerState 语义事件，绝不反向写数值（同 fallDamageTaken→takeDamage）。
    Rectangle {
        id: damageOverlay
        visible: window.appState === "playing"
        anchors.fill: parent
        color: "#ff0000"
        opacity: 0.0
        z: 250
        enabled: false   // 不参与指针事件（防挡破/放/背包点击）
    }
    NumberAnimation {
        id: damageFadeAnim
        target: damageOverlay
        property: "opacity"
        duration: 600
        from: 0.4
        to: 0.0
        easing.type: Easing.OutQuad
    }
    Connections {
        target: playerState
        function onDamaged(amount) {
            damageOverlay.opacity = 0.4    // 立即满红
            damageFadeAnim.start()          // 600ms 淡出（重启进行中的动画 → 连击重新闪）
        }
    }

    // 破/放信号 → 粒子迸发（呈现层消费 World 语义事件）。
    // 现代函数式 Connections（Qt6）；粒子降级（Loader.item 为 null）时安全跳过（PLAN §2-E）。
    Connections {
        target: theWorld
        function onBlockBroken(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstBreak(x, y, z, id)
        }
        function onBlockPlaced(x, y, z, id) {
            if (particleLoader.item) particleLoader.item.burstPlace(x, y, z, id)
            // t32：生存放置消耗 1 件（创造=无限源不耗）。worldgen 经 m_chunks.setBlock 直写、不经
            // World::setBlock → 不会发 blockPlaced；游戏内该信号仅玩家 placeBlock 触发，故此处即
            // 「玩家放置成功」语义事件。ViewModel 观察 World 事件做栈突变（PLAN §2 分层：VM 只依赖
            // World/Game 数据，不反向写）。takeStack 取至 0 → 选中栈变 Air → player.selectedBlock
            // 经 selectedBlockId 绑定变 Air → 右键不再放置（playercontroller 守 Air）。
            if (player.mode === PlayerController.Survival)
                hotbarVM.takeStack(hotbarVM.selectedSlot, 1)
        }
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
            // t50：工作台面板同样 E/Esc 关（与背包互斥）。
            if (e.key === Qt.Key_E && window.appState === "playing") {
                if (window.craftingTableOpen) window.closeCraftingTable()
                else window.toggleInventory()
                e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.inventoryOpen) {
                window.closeInventory(); e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.craftingTableOpen) {
                window.closeCraftingTable(); e.accepted = true; return
            }
            // F3 调试叠层切换（t10，PLAN §2-F）：playing 态按 F3 显/隐左上角调试文本。
            // 仅 playing 态有意义（menu 态主菜单全屏覆盖，叠层不可见）；切换不依赖指针捕获。
            if (e.key === Qt.Key_F3 && window.appState === "playing") {
                window.f3Visible = !window.f3Visible; e.accepted = true; return
            }
            if (e.key === Qt.Key_F5) { player.cycleCamera(); e.accepted = true; return } // 相机模式循环（t27）
            if (e.key === Qt.Key_F6) { worldClock.toggleDebugFast(); e.accepted = true; return } // 昼夜调试加速（t09）
            if (e.key === Qt.Key_G) { player.cycleMode(); e.accepted = true; return }
            if (e.key === Qt.Key_Q) { player.dropHeld(); e.accepted = true; return }     // 丢弃手持 1 件（t36）
            if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {            // 1–9 直选 hotbar 槽 0..8（属性赋值走 WRITE setter）
                hotbarVM.selectedSlot = e.key - Qt.Key_1; e.accepted = true; return
            }
            player.setKey(e.key, true)
        }
        Keys.onReleased: (e) => { if (e.isAutoRepeat) return; player.setKey(e.key, false) }

        // 光标位置追踪（背包内「手持物」浮动图标跟随鼠标用）。keyInput anchors.fill 窗口 →
        // point.position 即窗口坐标，供下方 cursorHeld Item 定位。
        HoverHandler { id: cursorTracker }

        // 滚轮循环切换 hotbar。WheelHandler 按指针位置抓取，与光标显隐/锁定无关，
        // 故捕获与未捕获都生效。只消费滚轮 —— 鼠标按键仍走 C++ 窗口级事件过滤（破/放）。
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                if (event.angleDelta.y > 0)      hotbarVM.scroll(-1) // 上滚 → 左移（下标-1，环绕）
                else if (event.angleDelta.y < 0) hotbarVM.scroll(1)  // 下滚 → 右移
            }
        }
    }

    // 暂停 / 未捕获 覆盖层（仅 playing 态）：点击任意处 → 进入（锁定指针）。
    // 主菜单态（appState="menu"）不显本叠层（由 MainMenu 覆盖全屏）。
    // 背包 / 工作台打开时（同为 !captured 态）抑制本叠层 —— 三者互斥，避免面板下面透出暂停叠层。
    Item {
        id: pauseOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && !player.captured
                 && !window.inventoryOpen && !window.craftingTableOpen
        z: 100
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.55)
            MouseArea { anchors.fill: parent; onClicked: { player.grab(); keyInput.forceActiveFocus() } }
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
                Text { text: "[F3] toggle debug overlay (fps / pos / chunks / vertices)"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                // 返回主菜单（playing ↔ menu 双向切换，t17）：消费点击，不冒泡到背景 grab。
                Rectangle {
                    width: 150; height: 32; radius: 6
                    color: backMenuArea.containsMouse ? "#2a3a2a" : "#1a2a1a"
                    border.color: "#3a6a3a"; border.width: 1
                    anchors.horizontalCenter: parent.horizontalCenter
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
                //   属性（slotRevision），整绑定就挂在该信号上 → slotsChanged 后重算。这与 SurvivalInventory
                //   主栏（mainRev 触碰 + mainSlots[index] 读取）同构，是「数组突变靠版本号触发刷新」的
                //   通用稳健写法（不依赖 Repeater 对等长数组的重建行为，那在不同 Qt 版本表现不一致）。
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

    // 光标手持物浮动图标（背包点击拾取后「拿在鼠标上」的物品栈；hotbarVM.heldBlock/heldCount 驱动）。
    // 仅背包 / 工作台打开且手持非空时显，z 最高（盖过背包面板 z=150）。位置跟随 cursorTracker（窗口坐标）。
    // t32：count>1 时右下角显数量（手持整栈移动时可见剩余数）。t33：手持工具 → ToolIcon 自绘（非 Image）；
    // t50：手持材料 → MaterialIcon 自绘（木棒）。t37：enabled:false 显式声明本 Item 不参与指针事件——
    // z=300 浮在面板(z=150)之上，若参与事件捕获会抢走下方槽位 TapHandler 的点击。纯呈现层。
    Item {
        visible: (window.inventoryOpen || window.craftingTableOpen) && hotbarVM.heldBlock !== 0
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
