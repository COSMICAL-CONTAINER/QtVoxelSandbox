import QtQuick
import QtQuick3D

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — FPS 视角 · 暂停菜单 · 三模式"
    color: "#101010"

    property int fps: 0 // main.cpp 经 frameSwapped 回填

    // 单一体素世界：网格(ChunkGeometry)与物理(PlayerController)共用同一份栅格
    World { id: theWorld; width: 16; depth: 16; height: 16; seed: 1337 }

    // Hotbar 视图模型（9 槽选择态 + 槽位内容）。选中方块 id 经绑定驱动玩家右键放置（t05）。
    Hotbar { id: hotbarVM }

    // 玩家控制器：指针锁定鼠标 + WASD/跳/飞 + 三模式物理。
    // 右键放置用 hotbar 当前选中槽的方块 id（绑定 hotbarVM.selectedBlockId）。
    PlayerController { id: player; world: theWorld; selectedBlock: hotbarVM.selectedBlockId }

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#9ec6e8"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            id: cam
            position: player.position                                // 眼睛位置
            eulerRotation: Qt.vector3d(player.pitch, player.yaw, 0)  // pitch→X, yaw→Y
            clipNear: 0.05
            clipFar: 1000
        }

        DirectionalLight { eulerRotation.x: -40; eulerRotation.y: -25; brightness: 1.5 }

        Model {
            geometry: ChunkGeometry { world: theWorld }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColorMap: Texture { source: "qrc:/textures/atlas.png"; generateMipmaps: false }
            }
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

        // 破/放粒子（t14）：粒子节点经 Loader 动态加载独立 BlockParticles.qml（内含 Particles3D import）。
        // 分层（PLAN §2）：触发由 World(Game 层) 发，呈现层只消费、绝不反向写栅格。
        // Particles3D 运行期缺失时仅该 Loader 失败、静默降级（§2-E），顶层 Main.qml 仍正常加载
        // （不命中 objectCreationFailed→exit(-1)）。块内 maxAmount 池化 → 连点不堆积爆量。
        Loader {
            id: particleLoader
            active: true
            source: "BlockParticles.qml"
            onStatusChanged: if (status === Loader.Error)
                console.warn("[t14] Particles3D 运行期不可用，粒子已降级关闭")
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

    // 键盘：N 切模式、1–9 直选 hotbar 槽、WASD/Space/Shift 传给控制器。Esc 由 C++ 事件过滤器拦截。
    // 注：原 1/2/3 用于直选模式，现让位给 hotbar（t06 验收要求 1–9 选槽）；模式切换统一由 N 循环。
    // 切换在指针捕获与未捕获时都可用 —— keyInput 始终持焦点（未捕获时也可预选槽）。
    Item {
        id: keyInput
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if (e.isAutoRepeat) return                               // 忽略自动重复（否则长按空格反复触发双击→飞行闪烁）
            if (e.key === Qt.Key_N) { player.cycleMode(); e.accepted = true; return }
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

    // 暂停 / 未捕获 覆盖层：点击任意处 → 进入（锁定指针）
    Item {
        id: pauseOverlay
        anchors.fill: parent
        visible: !player.captured
        z: 100
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.55)
            MouseArea { anchors.fill: parent; onClicked: { player.grab(); keyInput.forceActiveFocus() } }
        }
        Rectangle {
            width: 360; height: 210; radius: 10
            anchors.centerIn: parent
            color: "#1e1e1e"; border.color: "#3a3a3a"; border.width: 1
            Column {
                anchors.centerIn: parent; spacing: 12
                Text { text: "PAUSED"; color: "#eeeeee"; font.pixelSize: 26; font.bold: true
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "click to play"; color: "#bbbbbb"; font.pixelSize: 15
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[N] cycle mode   [1-9] select block   wheel cycle"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[Esc] release   WASD move   Space jump/fly   Shift down"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[LMB] break block   [RMB] place block"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
            }
        }
    }

    // 准星（仅捕获时）
    Item {
        visible: player.captured
        anchors.centerIn: parent
        width: 22; height: 22
        Rectangle { color: "#ffffff"; anchors.centerIn: parent; width: 20; height: 2 }
        Rectangle { color: "#ffffff"; anchors.centerIn: parent; width: 2; height: 20 }
    }

    // HUD：模式 + 地面状态 + FPS
    Text {
        x: 12; y: 8
        color: "#7fe57f"; font.pixelSize: 20; font.bold: true
        style: Text.Outline; styleColor: "#000000"
        text: "MODE: " + (player.mode === PlayerController.Spectator ? "SPECTATOR (noclip fly)" :
               player.mode === PlayerController.Creative ? ("CREATIVE " + (player.flying ? "(flying)" : "(walk · dbl-tap Space to fly)")) : "SURVIVAL (gravity + jump)")
              + "    FPS: " + window.fps
    }
    Text {
        x: 12; y: 36
        color: "#cccccc"; font.pixelSize: 13
        text: "pos: " + player.position.x.toFixed(1) + ", " + player.position.y.toFixed(1) + ", " + player.position.z.toFixed(1)
              + "   yaw: " + Math.round(player.yaw) + "  pitch: " + Math.round(player.pitch)
              + (player.captured ? ("   ground: " + (player.onGround ? "yes" : "no")
                                   + "   held: " + hotbarVM.nameAt(hotbarVM.selectedSlot)
                                   + " (#" + player.selectedBlock + ")") : "   pointer free")
    }

    // Hotbar（9 槽）：底部居中，1–9 直选 + 滚轮循环 + 选中槽高亮。
    // 布局/高亮风格与 MC 差异化（PLAN §9）：圆角槽、HUD 绿系强调色（#7fe57f，对齐既有 HUD）、
    // 选中态用缩放 + 描边 + 键位角标，而非白框；仅做 hotbar 本身（其余 HUD 已差异化）。
    Row {
        id: hotbarRow
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5

        Repeater {
            model: hotbarVM.slotCount
            delegate: Item {
                width: 50; height: 50
                // 选中槽放大强调
                scale: hotbarVM.selectedSlot === index ? 1.18 : 1.0
                Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }

                Rectangle {
                    anchors.fill: parent
                    radius: 7
                    color: Qt.rgba(0, 0, 0, 0.45)
                    border.color: hotbarVM.selectedSlot === index ? "#7fe57f" : "#3a3a3a"
                    border.width: hotbarVM.selectedSlot === index ? 3 : 1
                    // 选中槽外发光层（更柔的强调）
                    Rectangle {
                        visible: hotbarVM.selectedSlot === index
                        z: -1
                        anchors.fill: parent; anchors.margins: -3
                        radius: 10; color: "transparent"
                        border.color: Qt.rgba(0.5, 0.9, 0.5, 0.35); border.width: 2
                    }
                }

                // 方块图标（空槽 source="" → 不显示）
                Image {
                    anchors.centerIn: parent
                    width: 36; height: 36
                    visible: hotbarVM.iconSourceAt(index) !== ""
                    source: hotbarVM.iconSourceAt(index)
                    fillMode: Image.Pad
                    smooth: false
                }

                // 键位角标 1–9
                Text {
                    anchors.left: parent.left; anchors.top: parent.top
                    anchors.leftMargin: 4; anchors.topMargin: 1
                    color: hotbarVM.selectedSlot === index ? "#7fe57f" : "#888888"
                    font.pixelSize: 11; font.bold: true
                    style: Text.Outline; styleColor: "#000000"
                    text: (index + 1)
                }
            }
        }
    }
}
