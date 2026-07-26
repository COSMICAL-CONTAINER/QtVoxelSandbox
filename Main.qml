import QtQuick
import QtQuick3D

Window {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — cube (QtQuick3D / D3D11)"
    color: "#2b2b2b"

    // 由 C++ 每秒 setProperty 更新（frameSwapped 计数）
    property int fps: 0

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#3a3a3a"
            backgroundMode: SceneEnvironment.Color
        }
        PerspectiveCamera { position: Qt.vector3d(0, 0, 500) }
        DirectionalLight { eulerRotation.x: -30; brightness: 1.5 }

        Model {
            source: "#Cube"
            NumberAnimation on eulerRotation.y {
                from: 0
                to: 360
                duration: 12000
                loops: Animation.Infinite
                running: true
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: "#ffffff"
                baseColorMap: Texture { source: "qrc:/textures/default_cobble.png" }
            }
        }
    }

    // 左上角 FPS（C++ 每秒更新 window.fps）
    Text {
        x: 12
        y: 8
        color: "#7fe57f"
        font.pixelSize: 22
        font.bold: true
        style: Text.Outline
        styleColor: "#000000"
        text: "FPS: " + window.fps
    }
}
