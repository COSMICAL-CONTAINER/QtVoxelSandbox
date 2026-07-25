// Phase 0 占位窗口。后续 QQuickRhiItem 体素视口会叠在此之上。
import QtQuick

Window {
    width: 1280
    height: 720
    visible: true
    title: "Voxel Sandbox — Phase 0 (Qt 6.11.1)"
    color: "#2b2b2b"

    Text {
        anchors.centerIn: parent
        text: "Phase 0 OK — Qt 6.11.1 build works"
        color: "#e0e0e0"
        font.pixelSize: 28
    }
}
