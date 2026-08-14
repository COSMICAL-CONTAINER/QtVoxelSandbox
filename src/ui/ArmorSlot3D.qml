import QtQuick
import QtQuick3D
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型
//   + UnitCube / WireCube 几何（QML_NAMED_ELEMENT）。
import VoxelSandbox

// t546 装备槽 3D 预览（第三人称视角）：mini View3D 渲染「玩家身体部位 + 该部位护甲」的第三人称小模型，
// 替代 2D MaterialIcon / Canvas 占位图标。完全复用 Main.qml playerModel 的几何约定（UnitCube ±0.5 居中 +
// scale 方体堆叠）与配色（肤色 #caa472 / 上衣 #3a6a9a / 裤 #3a3a5a；护甲材质档色同 playerModel.armorBaseColor 表）。
//
// 部位（slotIndex 与 ArmorRegistry::ArmorPiece 同序）：
//   0 头：头方（肤色）+ 双眼（白底 + 瞳，第三人称脸）→ 有护甲叠头盔（护甲色）；
//   1 胸：躯干方（上衣色）→ 有护甲叠胸甲；
//   2 腿：大腿 + 小腿（裤色）→ 有护甲叠护腿（大腿/小腿两段 wrap）；
//   3 脚：小腿 + 靴位 → 有护甲叠靴子。
// 空槽 = 灰体（占位，原 Canvas 金属灰 #9aa0a6 语义）；装备 = 玩家本色 + 护甲色 overlay。
//
// F3+B（showHitboxes）：叠加该部位的 AABB 线框（white，同 Main.qml F3+B 玩家碰撞箱），满足
// 「开 F3+B 物品栏也显示 F3+B 状态（碰撞框等）」。
//
// 分层（PLAN §2）：纯呈现层（QtQuick3D 场景），只读 hotbar VM 的护甲数据（armorBlockIdAt / armorTier），
// 绝不反向写；§9a 纯色自绘原创（非 MC 皮肤 / GUI PNG）。与 CharacterPreview3D 共用同一套部位几何 / 配色，
// 供 SurvivalInventory + Inventory(tab6) 共用（同 hotbar VM = 「两个共用一个 UI」）。
Item {
    id: root

    // 宿主注入：护甲 VM（读 armorBlockIdAt / armorTier）。
    property Hotbar hotbar
    // 部位索引：0 头 / 1 胸 / 2 腿 / 3 脚（ArmorRegistry::ArmorPiece 同序）。
    property int slotIndex: 0
    // 当前装备槽护甲 id（面板 delegate 触碰 armorRevision 传入；0 = 空槽）。
    property int armorId: 0
    // F3+B 门控（宿主经 window.showHitboxes 绑定传入）。
    property bool showHitboxes: false

    // [t546] 诊断：确认 3D 预览组件已实例化（构造期 4 槽各打一行，armorId 初始 0 = 空槽；装备后看
    // onArmorIdChanged 补充日志可交叉验证）。落 logs/voxelsandbox.log。若运行后无此行 = QML 未进二进制。
    Component.onCompleted: console.info("[t546] ArmorSlot3D up slot=" + root.slotIndex + " armorId=" + root.armorId + " showHitboxes=" + root.showHitboxes)
    onArmorIdChanged: console.info("[t546] ArmorSlot3D slot=" + root.slotIndex + " armorId=" + root.armorId)

    // 体色：装备 = 玩家本色；空槽 = 灰（占位，原 Canvas 金属灰）。§9a 纯色原创，非 MC 皮肤资产。
    readonly property bool hasArmor: root.armorId !== 0
    readonly property color skinC:  root.hasArmor ? "#caa472" : "#8f959c"
    readonly property color shirtC: root.hasArmor ? "#3a6a9a" : "#8f959c"
    readonly property color pantsC: root.hasArmor ? "#3a3a5a" : "#7f858c"
    // 护甲材质档色（同 Main.qml playerModel.armorBaseColor：皮革棕 / 铁银 / 铜橙 / 金黄 / 钻石青）。
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
    readonly property color armorC: root.armorColor(root.armorId)

    // 模型缩放（按部位 bounding 高度定，使 40px 槽内 ~60% 占比）：头 2.8 / 胸 2.0 / 腿 2.3 / 脚 2.8。
    readonly property real modelScale: [2.8, 2.0, 2.3, 2.8][Math.max(0, Math.min(3, root.slotIndex))]
    // F3+B AABB（模型局部单位，部位中心为原点；被 modelRoot 缩放/旋转继承）。
    readonly property vector3d hitboxScale: {
        switch (root.slotIndex) {
        case 0: return Qt.vector3d(0.62, 0.62, 0.62)   // 头（含头盔探出）
        case 1: return Qt.vector3d(0.60, 0.80, 0.50)   // 躯干（含胸甲）
        case 2: return Qt.vector3d(0.62, 0.62, 0.40)   // 双腿（大腿+小腿）
        default: return Qt.vector3d(0.62, 0.42, 0.48)  // 小腿+靴
        }
    }

    View3D {
        anchors.fill: parent
        // 透明背景 → 面板深色井底透出；NoLighting 材质无需光照。
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.NoAA
        }
        PerspectiveCamera {
            id: cam
            position: Qt.vector3d(0, 0, 3.0)
            fieldOfView: 40
            clipNear: 0.1
            clipFar: 100
        }
        // 部件根：正面（-Z，含眼/头盔探出）旋向 +Z 相机 + 22° 3/4 侧角（同 MC 角色预览 3/4 视角）；
        // 缩放按部位。部件几何以「部位中心为原点」定位（缩放不偏位）。
        Node {
            id: modelRoot
            scale: Qt.vector3d(root.modelScale, root.modelScale, root.modelScale)
            eulerRotation: Qt.vector3d(0, 180 + 22, 0)

            // F3+B AABB（white，同 Main.qml 玩家碰撞箱）。
            Model {
                visible: root.showHitboxes
                geometry: WireCube {}
                position: Qt.vector3d(0, 0, 0)
                scale: root.hitboxScale
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
            }

            // ── 头（slotIndex 0）：头方 + 双眼 + 头盔 overlay ──
            Model {
                visible: root.slotIndex === 0
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.5, 0.5, 0.5)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
            }
            // 眼白（同 Main.qml 贴脸 z=-0.25；头面 -Z = 玩家前向 → 旋 180° 后朝相机）。
            Model {
                visible: root.slotIndex === 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 0.07, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
            }
            Model {
                visible: root.slotIndex === 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 0.07, -0.25)
                scale: Qt.vector3d(0.1, 0.12, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
            }
            // 瞳（z=-0.26 略凸出白底前）。
            Model {
                visible: root.slotIndex === 0
                geometry: UnitCube {}
                position: Qt.vector3d(-0.1, 0.07, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
            }
            Model {
                visible: root.slotIndex === 0
                geometry: UnitCube {}
                position: Qt.vector3d(0.1, 0.07, -0.26)
                scale: Qt.vector3d(0.05, 0.06, 0.02)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
            }
            // 头盔（装备时叠头；z 探出 +0.06 前向，眼在 -0.26 仍露）。
            Model {
                visible: root.slotIndex === 0 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0.05, 0.06)
                scale: Qt.vector3d(0.60, 0.58, 0.56)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }

            // ── 胸（slotIndex 1）：躯干方 + 胸甲 overlay ──
            Model {
                visible: root.slotIndex === 1
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.5, 0.7, 0.3)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
            }
            Model {
                visible: root.slotIndex === 1 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.58, 0.74, 0.44)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }

            // ── 腿（slotIndex 2）：大腿 + 小腿（裤色）+ 护腿 overlay（两段 wrap）──
            Model {
                visible: root.slotIndex === 2
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 2
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 2
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, -0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 2
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, -0.15, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 2 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }
            Model {
                visible: root.slotIndex === 2 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }
            Model {
                visible: root.slotIndex === 2 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, -0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }
            Model {
                visible: root.slotIndex === 2 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, -0.15, 0)
                scale: Qt.vector3d(0.34, 0.34, 0.34)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }

            // ── 脚（slotIndex 3）：小腿（裤色）+ 靴 overlay ──
            Model {
                visible: root.slotIndex === 3
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, 0, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 3
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, 0, 0)
                scale: Qt.vector3d(0.25, 0.3, 0.25)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
            }
            Model {
                visible: root.slotIndex === 3 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(-0.125, -0.09, -0.03)
                scale: Qt.vector3d(0.34, 0.14, 0.38)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }
            Model {
                visible: root.slotIndex === 3 && root.hasArmor
                geometry: UnitCube {}
                position: Qt.vector3d(0.125, -0.09, -0.03)
                scale: Qt.vector3d(0.34, 0.14, 0.38)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorC }
            }
        }
    }
}
