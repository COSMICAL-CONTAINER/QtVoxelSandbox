import QtQuick
import QtQuick3D
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型
//   + UnitCube / WireCube 几何（QML_NAMED_ELEMENT）。
import VoxelSandbox

// t546 角色预览 3D（第三人称视角）：mini View3D 渲染**完整玩家模型**（复用 Main.qml playerModel 的
// 部件几何 / 配色：头 + 双眼 + 躯干 + 双臂 + 双腿，总高 1.8）+ 4 装备槽护甲 overlay（头盔 / 胸甲 /
// 护腿 / 靴，按 hotbar.armor* VM），替代 2D Canvas 人形剪影占位。「玩家穿装备的第三人称样子」——
// 满足「装备/物品图标都要 3D（像玩家第三人称那样）」的核心诉求（装备槽逐格 3D 见 ArmorSlot3D）。
//
// 全部 UnitCube + NoLighting 纯色（§9a 原创，非 MC 皮肤资产）。坐标以脚底 y=0 为原点（同 Main.qml
// playerModel / 玩家 AABB 约定）：头心 1.55、躯干 0.95、肩 1.3、髋 0.6、脚 0。
//
// F3+B（showHitboxes）：叠加玩家 AABB 线框（0.62×1.82×0.62，同 Main.qml F3+B 玩家碰撞箱 0.6×1.8×0.6
// 微扩防线融于体）。
//
// 分层（PLAN §2）：纯呈现层（QtQuick3D 场景），只读 hotbar VM 护甲数据，绝不反向写。
// 与 ArmorSlot3D 共用同一套部位几何 / 配色；供 SurvivalInventory + Inventory(tab6) 共用（同 hotbar VM）。
Item {
    id: root

    // 宿主注入：护甲 VM（读 armorBlockIdAt / armorTier）。
    property Hotbar hotbar
    // F3+B 门控（宿主经 window.showHitboxes 绑定传入）。
    property bool showHitboxes: false

    // [t546] 诊断：确认 3D 角色预览组件已实例化 + 4 装备槽初始护甲 id。落 logs/voxelsandbox.log。
    Component.onCompleted: console.info("[t546] CharacterPreview3D up head=" + root.headArmor + " chest=" + root.chestArmor
        + " legs=" + root.legsArmor + " boot=" + root.bootArmor + " showHitboxes=" + root.showHitboxes)

    // 4 装备槽护甲 id（表达式形式触碰 armorRevision → 装备/脱下/破损后重算；t498 模式防 AOT 裸触碰漏注册）。
    property int headArmor: root.hotbar ? (root.hotbar.armorRevision >= 0 ? root.hotbar.armorBlockIdAt(0) : 0) : 0
    property int chestArmor: root.hotbar ? (root.hotbar.armorRevision >= 0 ? root.hotbar.armorBlockIdAt(1) : 0) : 0
    property int legsArmor: root.hotbar ? (root.hotbar.armorRevision >= 0 ? root.hotbar.armorBlockIdAt(2) : 0) : 0
    property int bootArmor: root.hotbar ? (root.hotbar.armorRevision >= 0 ? root.hotbar.armorBlockIdAt(3) : 0) : 0

    // 玩家本色（§9a 原创纯色，同 Main.qml playerModel 配色 / SurvivalInventory 预览配色）。
    readonly property color skinC: "#caa472"
    readonly property color shirtC: "#3a6a9a"
    readonly property color pantsC: "#3a3a5a"
    // 护甲材质档色（同 Main.qml playerModel.armorBaseColor 表）。
    function armorColor(aid) {
        const t = root.hotbar ? root.hotbar.armorTier(aid) : -1
        switch (t) {
        case 0: return "#8a5a2b"
        case 1: return "#d8d8d8"
        case 2: return "#c87850"
        case 3: return "#fad840"
        case 4: return "#4ee0c8"
        default: return "#d8d8d8"
        }
    }

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.NoAA
        }
        PerspectiveCamera {
            id: cam
            position: Qt.vector3d(0, 0.9, 3.8)   // 眼位高度 0.9 = 身体中段；正前 3.8
            fieldOfView: 40
            clipNear: 0.1
            clipFar: 100
        }
        // 模型根：正面（-Z，含脸/眼）旋向 +Z 相机 + 22° 3/4 侧角（同 MC 角色预览视角）。脚底 y=0。
        Node {
            id: modelRoot
            eulerRotation: Qt.vector3d(0, 180 + 22, 0)

            // F3+B 玩家 AABB（white，同 Main.qml F3+B 玩家碰撞箱）。
            Model {
                visible: root.showHitboxes
                geometry: WireCube {}
                position: Qt.vector3d(0, 0.9, 0)
                scale: Qt.vector3d(0.62, 1.82, 0.62)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
            }

            // ── 头 + 双眼 + 头盔（颈枢 1.3，头心 1.55）──
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0, 1.55, 0)
                scale: Qt.vector3d(0.5, 0.5, 0.5)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
            }
            // 眼白（贴脸 z=-0.25，同 Main.qml t52/t66）。
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 1.62, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 1.62, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
            }
            // 瞳（z=-0.26 略凸出白底前）。
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 1.62, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 1.62, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
            }
            // 头盔（装备槽 0 有护甲时叠头；z 探出 +0.06，眼仍露，同 Main.qml playerArmorHead）。
            Model {
                visible: root.headArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0, 1.60, 0.06)
                scale: Qt.vector3d(0.60, 0.58, 0.56)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.headArmor) }
            }

            // ── 躯干 + 胸甲（心 0.95）──
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0.95, 0)
                scale: Qt.vector3d(0.5, 0.7, 0.3)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
            }
            Model {
                visible: root.chestArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0.95, 0)
                scale: Qt.vector3d(0.58, 0.74, 0.44)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
            }

            // ── 双臂（肩枢 ±0.375, 1.3：袖 1.05 / 手 0.7）+ 胸甲袖覆盖（MC 胸甲覆盖手臂）──
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.375, 1.05, 0)
                scale: Qt.vector3d(0.25, 0.5, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
            }
            Model {
                visible: root.chestArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.375, 1.05, 0)
                scale: Qt.vector3d(0.30, 0.52, 0.30)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.375, 0.7, 0)
                scale: Qt.vector3d(0.25, 0.2, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.375, 1.05, 0)
                scale: Qt.vector3d(0.25, 0.5, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
            }
            Model {
                visible: root.chestArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.375, 1.05, 0)
                scale: Qt.vector3d(0.30, 0.52, 0.30)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.375, 0.7, 0)
                scale: Qt.vector3d(0.25, 0.2, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
            }

            // ── 双腿（髋枢 0.6：大腿 0.45 / 小腿 0.15）+ 护腿（两段 wrap）+ 靴 ──
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.45, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.legsArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.45, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.legsArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
            }
            Model {
                visible: root.bootArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.06, -0.03)
                scale: Qt.vector3d(0.34, 0.14, 0.38)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.bootArmor) }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.45, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.legsArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.45, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
            }
            Model {
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.legsArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
            }
            Model {
                visible: root.bootArmor !== 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.06, -0.03)
                scale: Qt.vector3d(0.34, 0.14, 0.38)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.bootArmor) }
            }
        }
    }
}
