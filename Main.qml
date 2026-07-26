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

    // 玩家控制器：指针锁定鼠标 + WASD/跳/飞 + 三模式物理
    PlayerController { id: player; world: theWorld }

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
    }

    // 键盘：N 切模式、1/2/3 直选、WASD/Space/Shift 传给控制器。Esc 由 C++ 事件过滤器拦截。
    Item {
        id: keyInput
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            if (e.isAutoRepeat) return                               // 忽略自动重复（否则长按空格反复触发双击→飞行闪烁）
            if (e.key === Qt.Key_N) { player.cycleMode(); e.accepted = true; return }
            if (e.key === Qt.Key_1) { player.setMode(PlayerController.Spectator); e.accepted = true; return }
            if (e.key === Qt.Key_2) { player.setMode(PlayerController.Creative); e.accepted = true; return }
            if (e.key === Qt.Key_3) { player.setMode(PlayerController.Survival); e.accepted = true; return }
            player.setKey(e.key, true)
        }
        Keys.onReleased: (e) => { if (e.isAutoRepeat) return; player.setKey(e.key, false) }
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
                Text { text: "[N] cycle mode   [1/2/3] Spectator/Creative/Survival"
                       color: "#999999"; font.pixelSize: 12
                       anchors.horizontalCenter: parent.horizontalCenter }
                Text { text: "[Esc] release   WASD move   Space jump/fly   Shift down"
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
              + (player.captured ? ("   ground: " + (player.onGround ? "yes" : "no")) : "   pointer free")
    }
}
