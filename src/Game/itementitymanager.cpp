#include "itementitymanager.h"
#include "toolregistry.h" // t1041 isTool3DDrop / isIconBillboardDrop 查 ToolRegistry::tool(type/tier)——Game 层同层纯表查询
#include "recipe.h"       // t1041 isIconBillboardDrop 材料段界 RecipeRegistry::MaterialIdBase（Hotbar::isMaterial 同源常量）

// R20.14 EntityStore 过渡 Adapter 实现（plan §29.3 验收④）：模拟体（spawn 三入口 / 合并 /
// LRU / 拾取 / despawn / 焚毁 / tick 物理 / 批量收口 / 槽位机件）已整体迁至
// src/Entities/entitystore.cpp（单一权威）——本文件只剩 ① 构造（notifySink → entitiesChanged
// 信号上行缝）与 ② 渲染家族谓词静态权威（isItem3DFamily / isPlainCubeDrop / isTool3DDrop /
// isIconBillboardDrop / isBlockIconBillboardDrop；QML delegate 排除侧与 C++ feeder 收纳侧
// 同源判定，t1027b/t1041d 源钉钉在本文件——族表体逐字保留）。全部 Q_INVOKABLE / 直调面
// 为头内一行委托（m_store.*），零逻辑复制；「模拟本体禁回流」由矩阵 r2014d 反探钉承担。

ItemEntityManager::ItemEntityManager(QObject *parent) : QObject(parent)
{
    // 通知缝：store 变更收口（非批 notify 沿 / endBatch 收口 / clearAll 无条件直发）上行
    //   为本类 entitiesChanged 信号——QML / instancing feeder 消费面零变化（本单承重墙）。
    m_store.setNotifySink([this]() { emit entitiesChanged(); });
}

// ── t1027 掉落物渲染家族谓词（单一权威；族表体逐字保留——t925 源钉钉 case 字面量）──
bool ItemEntityManager::isItem3DFamily(int itemId)
{
    // 家族表自 Main.qml isItem3DFamily（t880 建 / t925 扩 / t965 订正铁门 135）逐 id 收编，值不变：
    //   火把 13；活板门 20/136；台阶 15/87/58/109；雪层 44；草丛 24；附魔台 94；枯灌木 43；小麦 25；
    //   栅栏 17/60/88；门 19/135/89；蘑菇 115/48；蛛网 102；红石火把 129；拉杆 112；按钮 113/114。
    switch (itemId) {
    case 13: case 20: case 136: case 15: case 87:
    case 58: case 109: case 44: case 24: case 94:
    case 43: case 25:
    case 17: case 60: case 88:
    case 19: case 135: case 89:
    case 115: case 48:
    case 102: case 129:
    case 112: case 113: case 114:
        return true;
    default:
        return false;
    }
}

bool ItemEntityManager::isPlainCubeDrop(int itemId)
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false; // air / 越界 / 工具·材料段（见 static_assert）
    if (BlockRegistry::isPartialBlock(quint8(itemId))) return false;      // 异形段（台阶 16 / 半砖…）→ billboard / 3D 族
    if (BlockRegistry::isCrossBillboard(quint8(itemId))) return false;    // cross 段（花 / 树苗…）→ flat billboard
    if (BlockRegistry::isBed(quint8(itemId))) return false;               // 床段（含 8 色扩展床）→ bed 图标 billboard
    return !isItem3DFamily(itemId);                                       // 3D 形状族（火把 / 门 / 栅栏…）排除
}

// ── t1041 批 4 收官三谓词（单一权威，QML 侧薄委托）──
bool ItemEntityManager::isTool3DDrop(int itemId)
{
    const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
    if (!t) return false; // 非工具段（方块 / 材料 / 越界）→ 恒假
    switch (t->type) {
    case BlockRegistry::Pickaxe:   // 五类几何分支（Main.qml toolType===1..5 七分支中的五支）
    case BlockRegistry::Hoe:
    case BlockRegistry::Axe:
    case BlockRegistry::Shovel:
    case BlockRegistry::Sword:
    case BlockRegistry::Bow:       // 弓（type 7 分支；BowStringGeometry 弦随 stringPass 实例表）
        return true;
    default:                       // Shears 6 / FishingRod 8 / FlintSteel 9 / 其余不入 3D 族
        return false;
    }
}

bool ItemEntityManager::isIconBillboardDrop(int itemId)
{
    const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
    if (t)
        return t->type == BlockRegistry::Shears      // 剪刀（t329 billboard ToolIcon 分支）
            || t->type == BlockRegistry::FlintSteel; // 打火石（t803 billboard ToolIcon 分支）
    return itemId >= RecipeRegistry::MaterialIdBase; // 材料段（Hotbar::isMaterial 逐字同判：单边 >= 含护甲段）
}

bool ItemEntityManager::isBlockIconBillboardDrop(int itemId)
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false;
    if (isItem3DFamily(itemId)) return false;                            // 3D 族走形状桶（delegate 链首闸同判）
    return BlockRegistry::isPartialBlock(quint8(itemId))                 // t219 异形段（台阶/楼梯/栅栏/门…）
        || BlockRegistry::isCrossBillboard(quint8(itemId))               // t440 cross 段（花/树苗…）
        || BlockRegistry::isBed(quint8(itemId));                         // t496 床段
}
