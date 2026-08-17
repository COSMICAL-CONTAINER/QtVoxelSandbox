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
// t551 复原 + 增强（用户「t546 做反了」）：
//   (1) 空装备槽回归 2D 占位图（empty_armor_slot_* pack PNG / Canvas 剪影 / MaterialIcon，宿主面板改回），
//       本组件保留为**完整 3D 角色预览**（唯一 3D 人物）。
//   (2) 3D 人物**跟随玩家实际动作**（绑定 PlayerController）：玩家走 → walkPhase 驱动四肢摆动；
//       玩家蹲（moveState=Crouch）→ 上半身前倾 + 腿弯 + 髋下沉；玩家跳/离地 → 按实际离地高度抬升 +
//       收腿（Timer 16ms 采样 feetPosition.y 积分离地高度，着地归零）。
//   (3) **看鼠标指针**：宿主面板 root 级 HoverHandler 记录光标场景坐标 → mouseScene 注入 → 人物转身
//       （bodyYaw）+ 转头（headYawLead）+ 抬头/低头（headPitch）朝鼠标方向（MC 角色预览类 UI 交互）。
//   (4) 3D 人物右移（宿主面板 x，t573 定格 slotSize*2-10；t551 曾移 slotSize*2+6 被用户反馈偏过头）。
//
// 全部 UnitCube + NoLighting 纯色（§9a 原创，非 MC 皮肤资产）。坐标以脚底 y=0 为原点（同 Main.qml
// playerModel / 玩家 AABB 约定）：头心 1.55、躯干 0.95、肩 1.3、髋 0.6、脚 0。
//
// F3+B（showHitboxes）：叠加玩家 AABB 线框（0.62×1.82×0.62，同 Main.qml F3+B 玩家碰撞箱 0.6×1.8×0.6
// 微扩防线融于体）。
//
// 分层（PLAN §2）：纯呈现层（QtQuick3D 场景），只读 hotbar VM 护甲数据 + PlayerController 姿态属性
// （walkPhase/moveState/feetPosition/onGround），绝不反向写。与 ArmorSlot3D 共用同一套部位几何 / 配色
// （ArmorSlot3D 已在 t551 移除宿主用法；本组件仍为两面板共用）。供 SurvivalInventory + Inventory(tab6)
// 共用（同 hotbar VM + 同 PlayerController = 「两个共用一个 UI」）。
Item {
    id: root

    // 宿主注入：护甲 VM（读 armorBlockIdAt / armorTier）。
    property Hotbar hotbar
    // F3+B 门控（宿主经 window.showHitboxes 绑定传入）。
    property bool showHitboxes: false
    // t551 宿主注入 PlayerController（Main.qml `player: player`，同 FurnaceUI/AnvilUI 模式）。走 / 蹲 / 跳
    //   姿态只读 player.walkPhase / moveState / feetPosition / onGround（Game 层 Q_PROPERTY），绝不反向写。
    property var player: null
    // t551 宿主注入光标**屏幕坐标**（面板 root 级 HoverHandler 记录 point.globalPosition；screen → 本地
    //   用 mapFromGlobal，Qt 6.11 Item 无 mapFromScene，勿用）。未跟踪时 (0,0) 之外哨兵 → 人物回中性位。
    property point mouseScene: Qt.point(-100000, -100000)

    // [t546/t551] 诊断：确认 3D 角色预览组件已实例化 + 4 装备槽初始护甲 id + 玩家控制器已注入。
    // 落 logs/voxelsandbox.log。
    Component.onCompleted: console.info("[t551] CharacterPreview3D up head=" + root.headArmor + " chest=" + root.chestArmor
        + " legs=" + root.legsArmor + " boot=" + root.bootArmor + " showHitboxes=" + root.showHitboxes
        + " playerInjected=" + (root.player !== null))

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

    // ── t551 行走 / 蹲下姿态（同 Main.qml playerModel 的 t45/t51/t65/t71 动画逻辑，仅读不写）──
    // walkBlend：moveSpeed>0.1 → 1（四肢摆动），否则 0（中性位）。0/1 二值切换（spec：静止归零）。
    readonly property real walkBlend: root.player && root.player.moveSpeed > 0.1 ? 1.0 : 0.0
    // swingAmp：疾跑 ×1.4（大步）/ 蹲下 ×0.5（拘谨小步）/ 走 ×1.0。
    readonly property real swingAmp: root.player
        ? (root.player.moveState === PlayerController.Crouch ? 0.5
           : root.player.moveState === PlayerController.Sprint ? 1.4 : 1.0)
        : 0.0
    // 蹲下姿态：crouchBlend = 1（Crouch）/ 0（Walk/Sprint）。上半身绕髋前倾 + 大腿前抬 + 膝盖回折
    // （脚仍贴地），髋下沉 crouchDrop。符号同 Main.qml：鞠躬 = -crouchBow（+x 会后仰）。
    readonly property real crouchBlend: root.player && root.player.moveState === PlayerController.Crouch ? 1.0 : 0.0
    readonly property real crouchDrop: 0.18 * root.crouchBlend
    readonly property real crouchBow: 35.0 * root.crouchBlend
    readonly property real crouchThigh: 60.0 * root.crouchBlend
    readonly property real crouchKnee: -60.0 * root.crouchBlend

    // ── t551 跳 / 离地（跟随玩家实际动作）：Timer 16ms 采样 feetPosition.y，积分「离地高度」──
    //   onGround=true → 离地高归零（着地回落）；离地且向上（vy>0.3）→ 累加 vy*dt（积分出真实弹跳高度）。
    //   人物抬升量 jumpLift = 离地高 ×0.2（MC 1.0 跳 ~1.25 格 → 预览升 ~0.25 单位）；jumpTuck 驱动收腿。
    //   走台阶 / 爬坡 onGround 恒 true → 不误抬（只对真正离地生效）。面板不可见时 Timer 停跑（省 tick）。
    property real _prevY: 0
    property real _airH: 0
    Timer {
        interval: 16
        repeat: true
        running: root.player !== null && root.visible
        onTriggered: {
            if (!root.player) return
            const y = root.player.feetPosition.y
            const vy = (y - root._prevY) * 60.0      // dt ≈ 1/60（位置每 tick 更新）
            root._prevY = y
            if (root.player.onGround) {
                root._airH = 0                        // 着地回落
            } else if (vy > 0.3) {
                root._airH = Math.min(1.25, root._airH + vy * 0.0166) // 积分离地高度，钳 1.25 格
            }
        }
    }
    onVisibleChanged: {
        // 面板可见时重置采样基准（防构造期基准 0 / 上次会话残留 → 首帧误算大 vy 误抬升）。
        if (root.visible && root.player) root._prevY = root.player.feetPosition.y
        if (root.visible) root._airH = 0
    }
    readonly property real jumpLift: Math.min(0.25, root._airH * 0.2)   // 离地高 → 预览抬升量
    readonly property real jumpTuck: Math.max(0, Math.min(1, root.jumpLift / 0.25)) // 完全离地 → 1
    readonly property real jumpThigh: 35.0 * root.jumpTuck               // 收腿（大腿前抬）

    // ── t551 看鼠标指针：mouseScene（宿主窗口坐标）→ 预览本地偏移 → 转身 + 转头 + 抬头/低头 ──
    //   review-12 修：mouseScene 现为窗口坐标（point.position；原 globalPosition 不存在绑 undefined）。
    //   mapFromItem(null, x, y)：item 传 null = 从 scene（窗口 contentItem）坐标映射到本 Item 本地
    //   （Qt 6.11 Item 无 mapFromScene，mapFromGlobal 已不适配新坐标系）。
    //   鼠标在预览中心左侧（dx<0）→ 人物左转；上方（dy<0）→ 抬头。
    //   全身 yaw 转 65% + 头 yaw 再转 35%（头领转、身随转 = 自然「看」的姿态）；垂直全由头 pitch 承担。
    //   未跟踪（mouseScene 为哨兵）→ 回中性位（lookYaw=0/lookPitch=0）。
    // t573 修左右反转（用户「鼠标在左边，人物看向右边；上下是对的」）：t551 的 -mouseDx 使 dx<0（鼠标左）
    //   → lookYaw>0 → +y 旋把脸从 +Z 转向 +X（屏幕右）——看反。取反符号为 +mouseDx（pitch 不动，上下本对）。
    readonly property bool mouseTracked: root.mouseScene.x > -9999
    readonly property point mouseLocal: root.mapFromItem(null, root.mouseScene.x, root.mouseScene.y)
    readonly property real mouseDx: root.mouseLocal.x - root.width / 2
    readonly property real mouseDy: root.mouseLocal.y - root.height / 2
    readonly property real lookYaw: root.mouseTracked ? Math.max(-60, Math.min(60, root.mouseDx * 0.6)) : 0
    readonly property real lookPitch: root.mouseTracked ? Math.max(-45, Math.min(45, -root.mouseDy * 0.5)) : 0
    readonly property real bodyYaw: 0.65 * root.lookYaw        // 全身转身（身随）
    readonly property real headYawLead: 0.35 * root.lookYaw    // 头再转（头领）

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
        // t551：position.y 随离地抬升（跳）；eulerRotation.y = 基角 + bodyYaw（看鼠标转身）。
        Node {
            id: modelRoot
            position: Qt.vector3d(0, root.jumpLift, 0)
            eulerRotation: Qt.vector3d(0, 180 + 22 + root.bodyYaw, 0)

            // F3+B 玩家 AABB（white，同 Main.qml F3+B 玩家碰撞箱）。
            Model {
                visible: root.showHitboxes
                geometry: WireCube {}
                position: Qt.vector3d(0, 0.9, 0)
                scale: Qt.vector3d(0.62, 1.82, 0.62)
                materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
            }
            // t602 F3+B 朝向箭头（用户「看到了实体框，但没看到朝向的箭头」——本组件此前只画 AABB 框、漏画
            //   朝向线）。红色细棒沿本地 -Z（模型脸/眼所朝 = 玩家前向，眼睛 z=-0.25 同证）延伸
            //   facingLen = AABB 半对角线 + 0.3 ≈ 1.31 → 棒必凸出框外 ≥0.3（t558 教训：棒长仅略超半宽时被
            //   身体/框内空间遮挡，视觉「没箭头」）。随 modelRoot 继承 180+22+bodyYaw 旋转 → 箭头反映「看鼠标
            //   转身」的实际朝向（同 mob facing line 语义）。
            // t618 修（用户「朝向线在脚底下，应在头上」）：旧棒 y=0（modelRoot 原点 = 脚底）→ 棒从脚部伸出、
            //   贴地平走，配合相机俯角透视读作「从脚底往上翻出的颠倒箭头」。提到**眼高 y=1.62**（同 Main.qml
            //   F3+B 玩家朝向线 = feet+1.62 眼位高度；MC F3+B 实体朝向线即从眼线伸出）→ 棒从头部沿视线水平
            //   前向伸出框外，二三人称视角观感一致。方向核验：本地 -Z 经 modelRoot (180+22+bodyYaw) 旋转后
            //   = 玩家水平前向（与 Main.qml 第三人称玩家箭头 yaw-only 同约定），无需取反（「上下颠倒」观感
            //   实为脚底高度 + 俯角透视的假象，提线到头高即消除）。
            //   分层（PLAN §2）：纯呈现层调试叠层，只读 showHitboxes，绝不反向写。
            // t636 修（用户「朝向线应跟鼠标上下移动（垂直于面部）」）：旧棒恒水平（只随 modelRoot yaw 转）→ 鼠标
            //   上下移动时头（lookPitch）转了、棒不动 → 线与脸不垂直。改：棒挂进**俯仰枢轴 Node**（眼高 y=1.62，
            //   eulerRotation.x = lookPitch + eulerRotation.y = headYawLead —— 与 headNode 同一姿态角），棒沿枢轴
            //   本地 -Z 伸出 → 线随头 yaw + pitch 全姿态跟随（+x = 抬头 → 棒端上翘，与 headNode「+pitch=抬头」
            //   同源约定）；枢轴原点 = 眼位 → pitch 旋转绕眼摆而非绕棒心（棒心旋转会平移出眼高）。
            Node {
                visible: root.showHitboxes
                position: Qt.vector3d(0, 1.62, 0)   // 眼位高度（同下方第三人称玩家朝向线 = feet+1.62）
                eulerRotation: Qt.vector3d(root.lookPitch, root.headYawLead, 0)   // t636：跟头 pitch+yaw 姿态
                Model {
                    geometry: UnitCube {}
                    property real facingLen: Math.sqrt(0.31 * 0.31 + 0.91 * 0.91 + 0.31 * 0.31) + 0.3
                    position: Qt.vector3d(0, 0, -facingLen * 0.5)   // 棒从枢轴（眼）沿视线延伸到 -facingLen
                    scale: Qt.vector3d(0.05, 0.05, facingLen)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ff3030" }
                }
            }

            // 上半身枢轴 Node（t71 结构）：包 head/躯干/双臂，枢轴在髋（y=0.6）。蹲下绕髋 pitch 前倾
            //   鞠躬（-crouchBow；+x 会后仰），髋随 crouchDrop 下沉（与双腿枢轴同高 → 无断身缝隙）。
            Node {
                id: upperBody
                position: Qt.vector3d(0, 0.6 - root.crouchDrop, 0)
                eulerRotation: Qt.vector3d(-root.crouchBow, 0, 0)

                // 头部枢轴 Node（t66 结构）：头 + 双眼 + 头盔，枢轴在颈（y=0.7，世界 1.3）。
                //   t551 看鼠标：x = lookPitch（抬头/低头，同 Main.qml +pitch 约定：+x=抬头）；
                //   y = headYawLead（头领先转身的一小段 → 头领身随的「看」姿态）。
                Node {
                    id: headNode
                    position: Qt.vector3d(0, 0.7, 0)
                    eulerRotation: Qt.vector3d(root.lookPitch, root.headYawLead, 0)

                    // 头（≈0.5³，肤色）。相对颈枢：头心在颈上方 0.25（世界 y=1.55）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.25, 0)
                        scale: Qt.vector3d(0.5, 0.5, 0.5)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
                    }
                    // 眼白（贴脸 z=-0.25，同 Main.qml t52/t66；相对颈 y=0.32 = 世界 1.62）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.1, 0.32, -0.25)
                        scale: Qt.vector3d(0.1, 0.12, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.1, 0.32, -0.25)
                        scale: Qt.vector3d(0.1, 0.12, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#e8e8e8" }
                    }
                    // 瞳（z=-0.26 略凸出白底前）。
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(-0.1, 0.32, -0.26)
                        scale: Qt.vector3d(0.05, 0.06, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0.1, 0.32, -0.26)
                        scale: Qt.vector3d(0.05, 0.06, 0.02)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#1a1a1a" }
                    }
                    // 头盔（装备槽 0 有护甲时叠头；z 探出 +0.06，眼仍露，同 Main.qml playerArmorHead）。
                    Model {
                        visible: root.headArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, 0.30, 0.06)
                        scale: Qt.vector3d(0.60, 0.58, 0.56)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.headArmor) }
                    }
                }

                // ── 躯干 + 胸甲（心 0.95，相对髋 y=0.35）──
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, 0.35, 0)
                    scale: Qt.vector3d(0.5, 0.7, 0.3)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
                }
                Model {
                    visible: root.chestArmor !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, 0.35, 0)
                    scale: Qt.vector3d(0.58, 0.74, 0.44)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
                }

                // ── 双臂（肩枢 ±0.375, 0.7 = 世界 1.3）+ 胸甲袖覆盖 + t551 行走摆臂 ──
                //   摆动符号同 Main.qml：左臂 +sin（与右腿同相）/ 右臂 -sin；×walkBlend×swingAmp；
                //   静止 walkBlend=0 → 归零。+eulerRotation.x = 臂尖前摆（-Y→-Z，朝玩家前向）。
                Node {
                    position: Qt.vector3d(-0.375, 0.7, 0)
                    eulerRotation: Qt.vector3d(Math.sin(root.player ? root.player.walkPhase : 0) * 22 * root.walkBlend * root.swingAmp, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.25, 0.5, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
                    }
                    Model {
                        visible: root.chestArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.30, 0.52, 0.30)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.6, 0)
                        scale: Qt.vector3d(0.25, 0.2, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
                    }
                }
                Node {
                    position: Qt.vector3d(0.375, 0.7, 0)
                    eulerRotation: Qt.vector3d(-Math.sin(root.player ? root.player.walkPhase : 0) * 22 * root.walkBlend * root.swingAmp, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.25, 0.5, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.shirtC }
                    }
                    Model {
                        visible: root.chestArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.25, 0)
                        scale: Qt.vector3d(0.30, 0.52, 0.30)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.chestArmor) }
                    }
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.6, 0)
                        scale: Qt.vector3d(0.25, 0.2, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.skinC }
                    }
                }
            }

            // ── 双腿（髋枢 ±0.125, 0.6 - crouchDrop）+ t551 行走摆腿 / 蹲腿 / 跳收腿 ──
            //   行走摆动符号同 Main.qml：左腿 -sin / 右腿 +sin；×walkBlend×swingAmp；叠加 crouchThigh
            //   （蹲大腿前抬）+ jumpThigh（跳收腿）。膝盖枢轴回折 crouchKnee（蹲）/ 站立 0°（腿直立）。
            Node {
                id: leftLegPivot
                position: Qt.vector3d(-0.125, 0.6 - root.crouchDrop, 0)
                eulerRotation: Qt.vector3d(-Math.sin(root.player ? root.player.walkPhase : 0) * 28 * root.walkBlend * root.swingAmp
                                           + root.crouchThigh + root.jumpThigh, 0, 0)
                // 大腿段（裤色 #3a3a5a；髋下 0..0.3，中心 -0.15、scale.y=0.3）
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
                }
                Model {
                    visible: root.legsArmor !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.34, 0.34, 0.34)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
                }
                // 膝盖关节：位于大腿末端（髋下 0.3）。站立 0°（小腿续大腿成直线）；蹲下回折 crouchKnee。
                Node {
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(root.crouchKnee, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
                    }
                    Model {
                        visible: root.legsArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.34, 0.34, 0.34)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
                    }
                    Model {
                        visible: root.bootArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.24, -0.03)
                        scale: Qt.vector3d(0.34, 0.14, 0.38)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.bootArmor) }
                    }
                }
            }
            Node {
                id: rightLegPivot
                position: Qt.vector3d(0.125, 0.6 - root.crouchDrop, 0)
                eulerRotation: Qt.vector3d(Math.sin(root.player ? root.player.walkPhase : 0) * 28 * root.walkBlend * root.swingAmp
                                           + root.crouchThigh + root.jumpThigh, 0, 0)
                Model {
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.25, 0.3, 0.25)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
                }
                Model {
                    visible: root.legsArmor !== 0
                    geometry: UnitCube {}
                    position: Qt.vector3d(0, -0.15, 0)
                    scale: Qt.vector3d(0.34, 0.34, 0.34)
                    materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
                }
                Node {
                    position: Qt.vector3d(0, -0.3, 0)
                    eulerRotation: Qt.vector3d(root.crouchKnee, 0, 0)
                    Model {
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.25, 0.3, 0.25)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.pantsC }
                    }
                    Model {
                        visible: root.legsArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.15, 0)
                        scale: Qt.vector3d(0.34, 0.34, 0.34)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.legsArmor) }
                    }
                    Model {
                        visible: root.bootArmor !== 0
                        geometry: UnitCube {}
                        position: Qt.vector3d(0, -0.24, -0.03)
                        scale: Qt.vector3d(0.34, 0.14, 0.38)
                        materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: root.armorColor(root.bootArmor) }
                    }
                }
            }
        }
    }
}
