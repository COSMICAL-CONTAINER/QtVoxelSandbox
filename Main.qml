import QtQuick
import QtQuick3D

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — 16×16 区块 (可见面剔除 + 图集贴图)"
    color: "#101010"

    property int fps: 0
    property var keys: ({})

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#9ec6e8"
            backgroundMode: SceneEnvironment.Color
            // 显式关闭 AA（默认即关；避免任何多余开销）
            antialiasingMode: SceneEnvironment.NoAA
        }

        PerspectiveCamera {
            id: cam
            property real yaw: 0
            property real pitch: -42
            position: Qt.vector3d(8, 16, 22) // 看向 16×16×16 区块中心 (8, 0, 8)
            eulerRotation.x: cam.pitch
            eulerRotation.y: cam.yaw
            clipNear: 0.05
            clipFar: 1000
        }

        DirectionalLight { eulerRotation.x: -40; eulerRotation.y: -25; brightness: 1.5 }

        // 整个区块一个 Model：自建几何（只生成可见面）+ 单一图集材质。
        Model {
            geometry: ChunkGeometry { width: 16; depth: 16; height: 16; seed: 1337 }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColorMap: Texture {
                    source: "qrc:/textures/atlas.png"
                    generateMipmaps: false
                }
            }
        }
    }

    // 鼠标按住拖动 → 转视角
    MouseArea {
        anchors.fill: parent
        property real lastX
        property real lastY
        onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
        onPositionChanged: (mouse) => {
            cam.yaw -= (mouse.x - lastX) * 0.3
            cam.pitch -= (mouse.y - lastY) * 0.3
            cam.pitch = Math.max(-89, Math.min(89, cam.pitch))
            lastX = mouse.x
            lastY = mouse.y
        }
    }

    Item {
        id: keyInput
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => { window.keys[event.key] = true }
        Keys.onReleased: (event) => { window.keys[event.key] = false }
    }

    // WASD/Space/Shift 飞行
    Timer {
        interval: 16
        repeat: true
        running: true
        onTriggered: {
            const yr = cam.yaw * Math.PI / 180.0
            const fx = -Math.sin(yr), fz = -Math.cos(yr)
            const rx = Math.cos(yr), rz = -Math.sin(yr)
            let dx = 0, dy = 0, dz = 0
            if (window.keys[Qt.Key_W]) { dx += fx; dz += fz }
            if (window.keys[Qt.Key_S]) { dx -= fx; dz -= fz }
            if (window.keys[Qt.Key_D]) { dx += rx; dz += rz }
            if (window.keys[Qt.Key_A]) { dx -= rx; dz -= rz }
            if (window.keys[Qt.Key_Space]) dy += 1
            if (window.keys[Qt.Key_Shift]) dy -= 1
            if (dx !== 0 || dy !== 0 || dz !== 0) {
                const len = Math.sqrt(dx * dx + dy * dy + dz * dz)
                const sp = 0.3
                cam.position = Qt.vector3d(cam.position.x + dx / len * sp,
                                           cam.position.y + dy / len * sp,
                                           cam.position.z + dz / len * sp)
            }
        }
    }

    Text {
        x: 12; y: 8
        color: "#7fe57f"; font.pixelSize: 20; font.bold: true
        style: Text.Outline; styleColor: "#000000"
        text: "FPS: " + window.fps + "   cam: " + Math.round(cam.position.x * 10) / 10 + "," + Math.round(cam.position.y * 10) / 10 + "," + Math.round(cam.position.z * 10) / 10
    }
    Text {
        x: 12; y: 38
        color: "#dddddd"; font.pixelSize: 14
        text: "16×16×16 区块 · 可见面剔除(culled meshing) + 图集贴图。WASD/鼠标/Space·Shift"
    }
}
