import QtQuick
import QtQuick3D

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — 主菜单 · 创造沙盒"
    color: "#101010"

    property int fps: 0 // main.cpp 经 frameSwapped 回填

    // app 状态机（t17）：menu（启动首显主菜单）↔ playing（显 View3D/HUD + grab 指针）。
    // 初始 menu：启动不直接进游戏，先显主菜单（开始/退出）。准星/HUD 仅 playing 态显。
    property string appState: "menu"

    // 背包子态（t18）：仅 playing 态有意义。开 → 释放指针（光标可见，可点格子，类暂停）；
    // 关 → 恢复 grab。Esc/E/点遮罩均可关。开时抑制暂停叠层（二者互斥：都是 !captured 态）。
    property bool inventoryOpen: false

    // 进入游戏：切 playing 态 + 锁定指针（隐藏光标）+ 焦点回键位层。
    function startGame() {
        appState = "playing"
        player.grab()
        keyInput.forceActiveFocus()
    }
    // 返回主菜单：先释放指针（恢复光标 + 清按住的按键），再切 menu 态。
    function returnToMenu() {
        inventoryOpen = false
        player.release()
        appState = "menu"
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
        player.grab()                  // 恢复指针锁定（隐藏光标 + 居中）
        keyInput.forceActiveFocus()
    }

    // 单一体素世界（内部 3×3=9 chunk，世界 48×48×16；QML API 不变）：网格(ChunkGeometry)
    // 与物理(PlayerController)共用同一份栅格。
    World { id: theWorld; width: 48; depth: 48; height: 16; seed: 1337 }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }

    // 玩家状态（生命/饥饿，t22）：满血满饥初值。心/饥饿条读其 health/hunger；掉落伤害经
    // 下面的 Connections 路由到 takeDamage（呈现层只读，绝不反向写数值；PLAN §2 分层）。
    PlayerState { id: playerState }

    // 玩家控制器：指针锁定鼠标 + WASD/跳/飞 + 三模式物理。
    // 右键放置用 hotbar 当前选中槽的方块 id（绑定 hotbarVM.selectedBlockId）。
    PlayerController { id: player; world: theWorld; selectedBlock: hotbarVM.selectedBlockId }

    // 玩家掉落伤害 → 生命（t22）：PlayerController 在 Survival 着地结算时发 fallDamageTaken(hp)，
    // 呈现层经此 Connections 路由到 PlayerState.takeDamage（与破/放信号→粒子同模式：Game 层发
    // 语义事件，呈现层只消费）。PlayerController 不持有 PlayerState，保持单向事件流、分层干净。
    Connections {
        target: player
        function onFallDamageTaken(hp) { playerState.takeDamage(hp) }
        // t23/t24：背包打开时按 G 循环切模式 —— 切到观察者（无背包）则关闭；Creative↔Survival 间切换
        // 则保留背包打开，面板由各组件 visible 绑定 player.mode 自动换（创造背包↔生存背包）。避免任一
        // 背包在不兼容模式下滞留（Spectator 无背包/破放，t21）。
        function onModeChanged() {
            if (window.inventoryOpen && player.mode === PlayerController.Spectator)
                window.closeInventory()
        }
    }

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#9ec6e8"
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
            position: {
                const eye = player.position
                const look = player.lookVector
                const d = 3.5
                const m = player.cameraMode
                if (m === PlayerController.FirstPerson) return eye
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
        }

        DirectionalLight { eulerRotation.x: -40; eulerRotation.y: -25; brightness: 1.5 }

        // 共享图集纹理：3×3=9 个 per-chunk Model 共用一份 atlas（声明一次、按 id 引用）。
        Texture { id: voxelAtlas; source: "qrc:/textures/atlas.png"; generateMipmaps: false }

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
            geometry: ChunkGeometry { world: theWorld; cx: 0; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (1,0) → 世界 (16,0)
            position: Qt.vector3d(16, 0, 0)
            geometry: ChunkGeometry { world: theWorld; cx: 1; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,0) → 世界 (32,0)
            position: Qt.vector3d(32, 0, 0)
            geometry: ChunkGeometry { world: theWorld; cx: 2; cz: 0 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (0,1) → 世界 (0,16)
            position: Qt.vector3d(0, 0, 16)
            geometry: ChunkGeometry { world: theWorld; cx: 0; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (1,1) → 世界 (16,16)
            position: Qt.vector3d(16, 0, 16)
            geometry: ChunkGeometry { world: theWorld; cx: 1; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,1) → 世界 (32,16)
            position: Qt.vector3d(32, 0, 16)
            geometry: ChunkGeometry { world: theWorld; cx: 2; cz: 1 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (0,2) → 世界 (0,32)
            position: Qt.vector3d(0, 0, 32)
            geometry: ChunkGeometry { world: theWorld; cx: 0; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (1,2) → 世界 (16,32)
            position: Qt.vector3d(16, 0, 32)
            geometry: ChunkGeometry { world: theWorld; cx: 1; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }
        Model { // chunk (2,2) → 世界 (32,32)
            position: Qt.vector3d(32, 0, 32)
            geometry: ChunkGeometry { world: theWorld; cx: 2; cz: 2 }
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColorMap: voxelAtlas }
        }

        // 命中面线框（射线选体 t04）：贴在视线命中的方块面上，随准星实时更新。
        // 未命中 / 暂停（未捕获）时 hasHit=false → 隐藏。
        Model {
            visible: player.hasHit
            position: player.hitFaceCenter
            eulerRotation: player.hitFaceEuler
            geometry: WireSquare {}
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#101010"
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

            // 头（≈0.5³，肤色）。模型以脚底为原点（y=0 贴地），总高≈1.8 对齐玩家 AABB(kHeight)。
            Model {
                source: "#Cube"
                position: Qt.vector3d(0, 1.55, 0)
                scale: Qt.vector3d(0.5, 0.5, 0.5)
                materials: PrincipledMaterial { baseColor: "#caa472"; opacity: playerModel.bodyOpacity }
            }
            // 躯干（上衣色；y 0.6→1.3，宽 0.5 深 0.3）
            Model {
                source: "#Cube"
                position: Qt.vector3d(0, 0.95, 0)
                scale: Qt.vector3d(0.5, 0.7, 0.3)
                materials: PrincipledMaterial { baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
            }
            // 左臂（上衣色；挂躯干两侧，与躯干同高）
            Model {
                source: "#Cube"
                position: Qt.vector3d(-0.375, 0.95, 0)
                scale: Qt.vector3d(0.25, 0.7, 0.25)
                materials: PrincipledMaterial { baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
            }
            // 右臂
            Model {
                source: "#Cube"
                position: Qt.vector3d(0.375, 0.95, 0)
                scale: Qt.vector3d(0.25, 0.7, 0.25)
                materials: PrincipledMaterial { baseColor: "#3a6a9a"; opacity: playerModel.bodyOpacity }
            }
            // 左腿（裤色；y 0→0.6）
            Model {
                source: "#Cube"
                position: Qt.vector3d(-0.125, 0.3, 0)
                scale: Qt.vector3d(0.25, 0.6, 0.25)
                materials: PrincipledMaterial { baseColor: "#3a3a5a"; opacity: playerModel.bodyOpacity }
            }
            // 右腿
            Model {
                source: "#Cube"
                position: Qt.vector3d(0.125, 0.3, 0)
                scale: Qt.vector3d(0.25, 0.6, 0.25)
                materials: PrincipledMaterial { baseColor: "#3a3a5a"; opacity: playerModel.bodyOpacity }
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
            if (e.key === Qt.Key_E && window.appState === "playing") {
                window.toggleInventory(); e.accepted = true; return
            }
            if (e.key === Qt.Key_Escape && window.inventoryOpen) {
                window.closeInventory(); e.accepted = true; return
            }
            if (e.key === Qt.Key_F5) { player.cycleCamera(); e.accepted = true; return } // 相机模式循环（t27）
            if (e.key === Qt.Key_G) { player.cycleMode(); e.accepted = true; return }
            if (e.key >= Qt.Key_1 && e.key <= Qt.Key_9) {            // 1–9 直选 hotbar 槽 0..8（属性赋值走 WRITE setter）
                hotbarVM.selectedSlot = e.key - Qt.Key_1; e.accepted = true; return
            }
            player.setKey(e.key, true)
        }
        Keys.onReleased: (e) => { if (e.isAutoRepeat) return; player.setKey(e.key, false) }

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
    // 背包打开时（同为 !captured 态）抑制本叠层 —— 二者互斥，避免背包下面透出暂停叠层（t18）。
    Item {
        id: pauseOverlay
        anchors.fill: parent
        visible: window.appState === "playing" && !player.captured && !window.inventoryOpen
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
                Text { text: "[Esc] release   WASD move   Space jump/fly   Shift down"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[LMB] break block   [RMB] place block"
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
                                   + " (#" + player.selectedBlock + ")") : "   pointer free")
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
                // model = 槽内容（QVariantList<方块id>）。触碰 slotRevision 建立 NOTIFY 依赖：setSlotBlock
                // 改槽内容 → slotsChanged → slotRevision 自增 → 本绑定重算返回新数组 → Repeater 整列重建
                // （invokable 返回值不被 NOTIFY 跟踪，故用版本号触发；modelData = 该槽方块 id，air=0 空槽）。
                // 为何不直接 Q_PROPERTY(QVariantList slots)：本工具链 moc 拒绝 Q_PROPERTY 里的 QVariantList，
                // 故 slotList() 走 Q_INVOKABLE + slotRevision 做 NOTIFY。
                model: { hotbarVM.slotRevision; return hotbarVM.slotList() }
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

                    // 方块图标（等距立方体，统一源 → PreserveAspectFit 强制等比缩放进固定框，
                    // 无论源贴图原始尺寸如何，槽内显示尺寸完全一致；空槽 modelData=0 → 不显示）。
                    Image {
                        anchors.centerIn: parent
                        width: 38; height: 38
                        visible: modelData !== 0
                        source: hotbarVM.iconSourceForBlock(modelData)
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
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
            // 凸起边框：顶/左 亮
            Rectangle { color: "#e8e8e8"; width: parent.width; height: 2; anchors.top: parent.top }
            Rectangle { color: "#e8e8e8"; width: 2; height: parent.height; anchors.left: parent.left }
            // 凸起边框：底/右 暗
            Rectangle { color: "#3a3a3a"; width: parent.width; height: 2; anchors.bottom: parent.bottom }
            Rectangle { color: "#3a3a3a"; width: 2; height: parent.height; anchors.right: parent.right }
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
    }
}
