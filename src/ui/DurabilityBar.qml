// 耐久条（t640② 背包 / 装备槽工具·护甲耐久条）：槽底薄条，宽 ∝ remaining/max，色 绿(>50%)/黄(20–50%)/红(<20%)。
//   与 HUD hotbar（Main.qml t315）同款观感：槽底凹槽底色（空段背景）+ 彩色已耗段。原始自绘 Rectangle，
//   零 MC 资产（§9）。被 SurvivalInventory / Inventory 生存 tab 的 main·hotbar·craft 槽 delegate 复用
//   （t183 hotbar 耐久条模式推广到背包内全部槽位；护甲槽沿用既有内联 armorDurBar 不迁移）。
//
//   用法：`DurabilityBar { anchors.leftMargin: 3; anchors.rightMargin: 3; anchors.bottomMargin: 2;
//   property 绑定 curDur / maxDur（触碰父 delegate 的 revision 属性令绑定重算）}`。
//   curDur<=0 / maxDur<=0（空槽 / 非耐久物）→ 隐藏。满耐久（curDur==maxDur）**仍显满绿条**（对齐
//   背包 / 装备槽常显语义 —— t498 二轮复盘：满耐久隐藏会让新装备 / 新工具槽内无条，用户误以为缺失；
//   MC 1.0 背包耐久条满耐久也显；HUD hotbar 侧仍按 t315 满耐久隐）。
import QtQuick

Item {
    // 槽内物品当前耐久（Q_INVOKABLE 直读 + revision 触碰；<=0 = 无耐久物品 / 空槽 → 隐）。
    property int curDur: 0
    // 槽内物品最大耐久（toolMaxDurability / armorMaxDurability；<=0 = 非耐久物 → 隐）。
    property int maxDur: 0
    property real ratio: maxDur > 0 ? curDur / maxDur : 0.0

    // 非耐久物（maxDur<=0）或空槽（curDur<=0）→ 整条隐藏（方块 / 材料 / 空槽不显黑色空段）。
    visible: maxDur > 0 && curDur > 0

    // 凹槽底色（耐久条「空段」背景，凸显已耗部分）。
    Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.55 }
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width * (parent.maxDur > 0 ? parent.curDur / parent.maxDur : 0)
        // 绿 >50% / 黄 20–50% / 红 <20%（MC 1.0 耐久条配色量级）。
        color: parent.ratio > 0.5 ? "#5fd35f" : (parent.ratio >= 0.2 ? "#e8e85a" : "#e05050")
    }
}
