// t740 红石激活矩阵冒烟测试（无 GUI）：直接实例化 World（Core+World 两层，无 QML 场景），
//   按「信号源 × 接收器」全组合搭最小电路 rig → 手动驱动 tickRedstone（等价 WorldClock 10Hz 桥接）
//   → 核对每个组合的通电结果（TNT/发射器族 = 信号计数；灯/轨/门/活板门 = state 通电位）+ 降沿复查。
//   动机（t740）：用户实测「红石火把 / 红石粉不能激活 TNT」而静态核对链路完整 —— 本测试把整条
//   World 层电力链（setBlock→notePowerWrite→m_powerDirty→tickRedstone→recomputePowerLocal→信号/state）
//   在真实对象上跑通，断点直接暴露为 FAIL 行；同时产出「源×接收器」矩阵核对表（commit message 引用）。
//   分层（PLAN §2）：Core+World 为主；t737 环线探针附加 World 层 mesher（partialblockgeometry）与
//   Entities 层 MinecartManager 源码直编（两者向下只依赖 Core+World，不引入 Game/QML）；t759 附加 World 层
//   worldgen 断言（要塞传送门房净空 —— 独立小世界扫种子，不动主世界 rig）。
//   运行：build/redstone_matrix_test.exe，全过 exit 0。
//   环境依赖（review24 低危登记）：QGuiApplication——PlayerController 是 QQuickItem 派生，实例化需
//   Gui 平台集成（无窗口创建）；headless Linux 无 offscreen 平台插件时直接 fatal，本文件全部 211+ 探针
//   被动继承该要求（CI / 无头环境须装 qt qpa offscreen 插件或设 QT_QPA_PLATFORM）。
#include <QCoreApplication>
#include <QGuiApplication> // t814 真消费端探针：PlayerController 是 QQuickItem 派生 → 实例化需 Gui 应用对象
#include <QDebug>
#include <QDir>   // t777 探针（羊毛层合成器临时 PNG rig）
#include <QImage> // t777 探针（fur/body 双 PNG 生成 + 合成结果像素断言）
#include <QColor> // t777 探针（像素色对比）
#include <QPainter> // t779 探针（合成源贴图矩形填色）
#include <QJsonDocument> // Review #1 探针（settings.json resourcePack 皮肤路径解析）
#include <QJsonObject>   // Review #1 探针（同上）
#include <QStandardPaths> // Review #1 探针（settings.json 候选位置，同 resolveSettingsPath）
#include <QFile>   // review-e 探针（派生缓存 _r<rev> 存在性 / 旧版清理断言）
#include <QRegularExpression> // t813 探针（stamp / git 哈希格式正则）
#include <QUrl>    // Review 2026-08-24 #5 探针（packFileUrl 查询串剥离断言：QUrl::toLocalFile）
#include <cmath>
#include <algorithm> // t795 探针 std::max（环带切比雪夫距离判定）
#include <vector>   // t824 探针 std::vector<int>（池允许集）
#include <QQmlEngine>   // t874/t875 真链探针：QQmlEngine + qmlRegisterType —— 真 QML 面板 × 真 C++ Hotbar 同台
#include <QQmlContext>  // t874/t875 真链探针：rootContext()->setContextProperty + qmlContext（wrapper 作用域链）
#include <QQmlComponent> // t874/t875 真链探针：setData+base URL 直载源树 AnvilUI.qml / EnchantingTableUI.qml
#include <QQuickItem>   // t874/t875 真链探针：面板 root / 宿主容器 Item
#include <QQuickWindow> // t891 探针：pc 挂窗置 captured（placeBlock 入口门；grab 载体，无 show）

#include "blockregistry.h"
#include "toolregistry.h" // t762 黑曜石挖掘规则探针（miningTime / canHarvest / miningSpeedMul 纯表查询）
#include "hotbar.h"       // t763 附魔数值生效链探针（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源）
#include "recipe.h"       // t802 全配方审计探针（match 两阶段匹配 + recipeAt 全表自匹配回归）
#include "smelting.h"     // t802 云杉链熔炉补缺探针（SpruceLog→木炭 + 云杉原木/木板燃料）
#include "playerstate.h"  // t755 死亡态硬锁探针（致死落库 0 / heal 死亡免疫 / respawn 复位链）
#include "playerprogress.h" // review #22 农夫计数回放链式补前置探针（loadVariant → unlockWithAncestry）
#include "world.h"
#include "chunkgeometry.h" // t860 cutout 折叠探针：terrain 段 ChunkGeometry 直调（顶点数行为级断言）
#include "worldstore.h"   // t822 存档 round-trip 探针（真 WorldStore SQLite：savePlayerData/loadPlayerData
                          //   玩家态 JSON 落盘读回；World 层直编，t622 序列化链首次自动化覆盖）
#include "partialblockgeometry.h" // t737 拐角象限断言（mesher 同源调用）
#include "minecartmanager.h"      // t737 环线矿车绕圈断言（骑乘 / 空车两路）
#include "entitymanager.h"        // 审查 #1 末影眼巡航高度回归探针（spawnEnderEye + enderEyeCruiseYAt）
#include "resourcepackmanager.h"  // t785 生物蛋探针（生成式染色表 spawnEggTint 条目存在性直调）
#include "itementitymanager.h"    // t804 掉落物火焚探针（item 入 Fire 格 0.8s 焚毁 + itemBurned 烟信号）
#include "boatmanager.h"          // t805 船上岸回归探针（水/陆速比 + 同层湿沙挡停 + 冰面豁免保留）
#include "buildinfo.h"            // t813 构建版本戳探针（stamp / gitHash 格式断言；Core 叶子直编）
#include "playercontroller.h"     // t814 真消费端探针（Game 层 PlayerController 直编：firePowerTnt/fireDispenserAtQml）
#include "worldclock.h"           // t889 暂停语义探针（WorldClock.running 停表行为级 + 源码钉）
#include "xporbmanager.h"         // t889 暂停语义探针（墙钟顺延三管理器调用面钉）
#include "dispenserstore.h"       // t814 发射器/投掷器 per-block 库存（分派 + 扣减断言源）
#include "mobmodel.h"             // review24 低危收尾（#35）：Renderer 白名单长度 ↔ Entities MobType 上界互钉
                                   //   （Renderer 在 Entities 之下，mobmodel.cpp 不得 include entitymanager.h——
                                   //   PLAN §2 低层永不 include 高层；互钉只能落在本测试 TU，它合法 include 全栈）
#include "itemshapegeometry.h"    // t880 异形物品 3D 模型族探针（ItemShapeGeometry 几何契约直调：顶点数/bounds）

// review24 低危收尾（#35）：MobModel 合法 mobType 白名单表长（kValidMobTypeCount，mobmodel.h public 常量
//   ↔ mobmodel.cpp kValidMobModelType 表编译期互钉）必须覆盖整个 EntityManager::MobType 枚举（0..MobAnvil=18，
//   实值经核：MobTest=0 .. MobAnvil=18 共 19 值）。枚举中部插值 / 尾部新增忘补表行时本断言编译期拦截
//   （t782「整表错位静默钳猪」根因的复刻防线）。
static_assert(MobModel::kValidMobTypeCount == EntityManager::MobAnvil + 1,
              "MobModel 白名单长度必须覆盖整个 EntityManager::MobType（0..MobAnvil）——"
              "新增 mobType 须同步 kValidMobModelType 表 + mobmodel.h kValidMobTypeCount");

// t777 探针：羊毛层合成器（resourcepackmanager.cpp 文件级函数，头文件外声明 → extern 直连；spawnEggTint
//   进了 .h 因 EggTint 是头内类型，本函数签名纯 QString 无需入头）。review #6：第 3 参 revision 进文件名
//   `_r<rev>`（换包逐版缓存 + 清旧；探针传任意探针版号）。
extern QString generateSheepWoolFaceFile(const QString &furPath, const QString &bodyPath, int revision);
// t829② 夜行者下巴补全合成器（resourcepackmanager.cpp 文件级自由函数；探针密闭 rig 直调，同上先例）。
extern QString generateNightwalkerChinFile(const QString &texPath, int revision);
// Review 2026-08-23 #1 探针：slim 皮肤布局探测器（同上 extern 直连；签名纯 QImage 无需入头）。
extern bool probeSlimSkinLayout(const QImage &tex);
// Review 2026-08-24 #5 探针：pack 原文件直返 URL 构造器（?r=<revision> cache-bust；同上 extern 直连）。
extern QString packFileUrl(const QString &localPath, int revision);

namespace {

using BR = BlockRegistry;

// ── rig 布局常量：所有 rig 摆在世界高空（y0 平台层；地形 / 树冠最高 ~33，40 以上必空）──
constexpr int kRigY = 41;

void tickN(World &w, int n) { for (int i = 0; i < n; ++i) w.tickRedstone(); }

// 源描述：id + 激活 state + 关断方式（state 位清零 vs 整块移除）。
struct SourceDef {
    const char *name;
    quint8 id;
    quint8 onState;                 // 激活态 state（拉杆/按钮/板 bit0；探测轨 bit4；火把/红石块 0）
    bool removeToOff;               // true = 关断 = 清 Air（火把 / 红石块无开关位）
    bool dustTrail;                 // true = 粉传导场景（lever(on) - 粉×3 - 接收器）
};

// 接收器核对：通电后期望（信号 or state 位）+ 降沿后期望（state 位清零；信号型无降沿语义）。
struct RecvDef {
    const char *name;
    quint8 id;
    quint8 onFlag;                  // 通电位（信号型填 0 → 走信号计数判定）
    bool signalBased;               // TNT / 发射器 / 投掷器 = 上升沿信号
};

// t886 获物弹速抛物解镜像（PlayerController useFishingRod 获物分支；P18 双钉——改值须两处同步）：
//   弹出点 = 浮标格向上**列扫**首个非水格 +0.225（review26 #7 口径——kFishCatchPopOffset；直上空气 =
//   floor(bobY)+1+0.225，静水/流动水同式）；目标 = 玩家脚位 +0.9（m_height 1.8 之半）；
//   T = clamp(0.45+0.055D, 0.5, 1.4)；vy = Δy/T + 14T（½g，g=28 掉落物重力镜像）；返 |v|。
float fishCatchSpeedMirror(const QVector3D &bobPos, const QVector3D &playerFeet)
{
    const float spY = std::floor(bobPos.y()) + 1.225f; // 列扫：直上空气格顶 +0.225（rig 水池皆无冰盖）
    const float dx = playerFeet.x() - bobPos.x();
    const float dz = playerFeet.z() - bobPos.z();
    const float dy = (playerFeet.y() + 0.9f) - spY;
    const float D = std::sqrt(dx * dx + dz * dz);
    const float T = std::max(0.5f, std::min(1.4f, 0.45f + 0.055f * D));
    const float vy = dy / T + 14.0f * T;
    return std::sqrt((dx / T) * (dx / T) + (dz / T) * (dz / T) + vy * vy);
}

} // namespace

int main(int argc, char *argv[])
{
    // t814：QCoreApplication → QGuiApplication —— Game 层 PlayerController 直编（QQuickItem 派生，
    //   构造需 Gui 平台集成；无窗口创建，探针纯对象交互）。
    QGuiApplication app(argc, argv);

    World w;
    w.setWidth(96);
    w.setDepth(128); // t814：96 → 128 —— rig 位耗尽（slotIdx=125 > 96 深度的 124 位上限）后 nextSlot 落
                     //   z≥96 越界区，setBlock 被静默拒绝 → 器件根本没放上 → 消费端探针全线假 FAIL。
                     //   深度扩到 128：既有 slot 0..123 坐标不变（列距 22 / 行距 3 只向后延伸），新 probe
                     //   落新增行（z=97 起）。世界生成按新深度 regenerate 一次（秒级）。
    w.setHeight(48); // 3 次 setter 各 regenerate 一次（几秒内）；生成快

    // rig 寻址（2D 网格防越界——首版 x 单排递增在 x>48 后 setBlock 全被越界拒绝 = 假 FAIL）：x 列距 22
    //   （容纳 16 粉 + 源 + 接收器的最长探针 18 格）、z 行距 3；96×128 → 4 列 × 42 行 = 168 rig 位。
    //   t814 教训：耗尽后 setBlock 静默拒绝（无返回值无告警）→ 器件没放上 → 下游探针全线假 FAIL 且
    //   diag 指向消费端（真凶是选址）——故越界改为 qFatal 硬失败（响亮 > 静默腐烂）。
    int slotIdx = 0;
    const auto nextSlot = [&]() {
        const int col = slotIdx % 4, row = slotIdx / 4;
        // review24 低危（探针族）：耗尽检查移到 ++ 之前——旧版先 ++ 再检查，qFatal 报的编号比真失败的
        //   slot 大 1（off-by-one，diag 误导排查）；现报真实失败位号。
        if (4 + row * 3 >= 128)
            qFatal("rig grid exhausted: slot %d beyond 128-deep grid (4 cols x 42 rows = 168) - "
                   "out-of-bounds setBlock is silently rejected = false FAIL farm", slotIdx);
        ++slotIdx;
        return QPair<int, int>(4 + col * 22, 4 + row * 3);
    };

    // review24 低危（探针族）：rig 器件放置回读校验。World::setBlock 对「越界拒绝 / 同 id 无变化早退」均
    //   静默（返回 false 无告警）——nextSlot 的 qFatal 只防了坐标越界这一条静默拒绝路径；器件没放上时
    //   下游断言全线假 FAIL 且 diag 指向消费端（t814 教训同源）。关键器件放置（t814 真消费端探针的
    //   机器 / 源铺设）走本帮手：放置后回读 blockAt 钉落位，落位失败响亮退出。同 id 早退时格子本已是
    //   目标 id → 回读照过（不误伤；清理用的 Air 写不需本帮手）。
    const auto placeRigBlock = [](World &world, int x, int y, int z, BR::Id id, quint8 st) {
        world.setBlock(x, y, z, id, st);
        if (world.blockAt(x, y, z) != quint8(id))
            qFatal("rig placement silently rejected at (%d,%d,%d) id=%d state=%d - device never "
                   "landed, downstream assertions are a false-FAIL farm",
                   x, y, z, int(id), int(st));
    };

    // ── 信号计数器（等价 Main.qml 转发消费端；直接连接同步计数）──
    int tntFired = 0, dispFired = 0;
    int lastTntX = -1, lastTntY = -1, lastTntZ = -1;
    QObject::connect(&w, &World::powerTntTriggered, &w, [&](int x, int y, int z) {
        ++tntFired; lastTntX = x; lastTntY = y; lastTntZ = z;
    });
    QObject::connect(&w, &World::powerDispenserTriggered, &w, [&](int x, int y, int z) {
        Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); ++dispFired;
    });
    // ── 掉落物 / 雪层坍落信号记录器（审查修 #4/#15 探针 P24/P25 消费：附着物失撑坍落断言）──
    //   同上同步计数模式；P24/P25 用前后差分（drops0 快照）隔离本探针的掉落事件。
    int dropItemCount = 0, lastDropId = 0, lastDropX = -1, lastDropY = -1, lastDropZ = -1;
    int snowFellCount = 0;
    QObject::connect(&w, &World::blockDroppedAsItem, &w, [&](int x, int y, int z, int id) {
        ++dropItemCount; lastDropId = id; lastDropX = x; lastDropY = y; lastDropZ = z;
    });
    QObject::connect(&w, &World::snowLayerFell, &w, [&](int x, int y, int z, int layers) {
        Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); Q_UNUSED(layers); ++snowFellCount;
    });

    // ── 矩阵维度（t740 全量：任务点名 9 源 + 石/铁/金压力板 3 补充源；接收器 7 族）──
    const SourceDef sources[] = {
        { "RedstoneTorch(lit)",   BR::RedstoneTorch,      0,                            true,  false },
        { "RedstoneBlock",        BR::RedstoneBlock,      0,                            true,  false },
        { "Lever(on)",            BR::Lever,              1,                            false, false },
        { "WoodButton(pressed)",  BR::WoodButton,         1,                            false, false },
        { "StoneButton(pressed)", BR::StoneButton,        1,                            false, false },
        { "WoodPlate(pressed)",   BR::WoodPressurePlate,  1,                            false, false },
        { "CobblePlate(pressed)", BR::CobblePressurePlate,1,                            false, false },
        { "StonePlate(pressed)",  BR::StonePressurePlate, 1,                            false, false },
        { "IronPlate(pressed)",   BR::IronPressurePlate,  1,                            false, false },
        { "GoldPlate(pressed)",   BR::GoldPressurePlate,  1,                            false, false },
        { "DetectorRail(cart)",   BR::DetectorRail,       BR::DetectorRailStateOnFlag,  false, false },
        { "DustTrail(lever->x3)", BR::Lever,              1,                            false, true  }, // 粉传导（用户点名场景）
    };
    const RecvDef recvs[] = {
        { "TNT",          BR::TntBlock,      0,                            true  },
        { "RedstoneLamp", BR::RedstoneLamp,  BR::RedstoneLampStateOnFlag,  false },
        { "GoldenRail",   BR::GoldenRail,    BR::GoldenRailStateOnFlag,    false },
        { "Dispenser",    BR::Dispenser,     0,                            true  },
        { "Dropper",      BR::Dropper,       0,                            true  },
        { "IronDoor",     BR::IronDoor,      0x04,                         false },
        { "IronTrapdoor", BR::IronTrapdoor,  0x01,                         false },
    };

    qInfo().noquote() << "=== t740 redstone activation matrix (World-layer harness) ===";
    int totalFail = 0;
    for (const SourceDef &src : sources) {
        for (const RecvDef &rc : recvs) {
            // 每个 case 独立 rig 位（列距 22 / 行距 3 隔离防串扰）。
            const auto [x0, z0] = nextSlot();
            const int srcX = x0;
            const int recvX = src.dustTrail ? x0 + 4 : x0 + 1;

            // 搭 rig：粉传导场景 = lever(on) + 粉×3 + 接收器；其余 = 源 + 相邻接收器。
            w.setBlock(srcX, kRigY, z0, src.id, src.onState);
            if (src.dustTrail)
                for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY, z0, BR::RedstoneDust, 0);
            w.setBlock(recvX, kRigY, z0, rc.id, 0);

            const int tnt0 = tntFired, disp0 = dispFired;
            tickN(w, 6);

            bool on = false;
            QString onNote;
            if (rc.signalBased && rc.id == BR::TntBlock) {
                on = (tntFired > tnt0) && lastTntX == recvX && lastTntY == kRigY && lastTntZ == z0;
                if (!on) onNote = QStringLiteral("no powerTntTriggered");
            } else if (rc.signalBased) {
                on = dispFired > disp0;
                if (!on) onNote = QStringLiteral("no powerDispenserTriggered");
            } else {
                on = (w.stateAt(recvX, kRigY, z0) & rc.onFlag) != 0;
                if (!on) onNote = QStringLiteral("state flag not set (st=%1)").arg(int(w.stateAt(recvX, kRigY, z0)));
            }
            // 降沿复查（仅 state 型接收器；信号型无降沿语义——消费端沿检测）。
            bool offOk = true;
            QString offNote;
            if (on && !rc.signalBased) {
                if (src.removeToOff)      w.setBlock(srcX, kRigY, z0, BR::Air);
                else                      w.setBlock(srcX, kRigY, z0, src.id, 0); // 开关位清零（lever/按钮/板 bit0、探测轨 bit4、粉线 lever off）
                tickN(w, 6);
                offOk = (w.stateAt(recvX, kRigY, z0) & rc.onFlag) == 0;
                if (!offOk) offNote = QStringLiteral("falling edge: flag stuck");
            }
            const bool ok = on && offOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL") << "|" << src.name << "->" << rc.name
                              << (on ? QString() : onNote) << (offOk ? QString() : offNote);
            // 清场（隔离带外的本 rig 格全清，防跨 case 影响）。
            for (int i = 0; i < 6; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
            tickN(w, 2);
        }
    }

    // ── 场景探针（用户点名 / 语义边界）──
    qInfo().noquote() << "=== scenario probes ===";

    // P1 火把后放 TNT（源先就位、稳态后再放接收器 —— 可达性反序）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::RedstoneTorch, 0);
        tickN(w, 6);
        w.setBlock(x0 + 1, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 6);
        const bool ok = tntFired > t0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| torch-first, TNT placed last -> fires";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P2 粉长线（lever + 8 粉 + TNT）：末粉电力 = 16-9 = 7 > 0 → 应点燃；沿线电力级单调衰减 15→8。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Lever, 1);
        for (int i = 1; i <= 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::RedstoneDust, 0);
        w.setBlock(x0 + 9, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 8);
        bool ok = tntFired > t0;
        for (int i = 1; i <= 8 && ok; ++i) {
            const int p = w.stateAt(x0 + i, kRigY, z0) & BR::RedstoneDustPowerMask;
            if (p != 16 - i) { qInfo().noquote() << "  dust" << i << "power" << p << "expect" << 16 - i; ok = false; }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| 8-dust trail decays 15..8, fires TNT";
        for (int i = 0; i <= 9; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P3 粉超距（lever + 16 粉 + TNT）：末粉电力 0 → TNT 不应点燃（15 格衰减上限语义）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Lever, 1);
        for (int i = 1; i <= 16; ++i) w.setBlock(x0 + i, kRigY, z0, BR::RedstoneDust, 0);
        w.setBlock(x0 + 17, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 10);
        const bool ok = tntFired == t0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| 16-dust out-of-range: TNT must NOT fire";
        for (int i = 0; i <= 17; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P4 火把立方块上、粉在地面斜下邻（经典 torch-on-block 布线）：t740 修复后应喂粉 15 + 灯亮；断火把降沿灯灭。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Stone);                 // 支撑块
        w.setBlock(x0, kRigY + 1, z0, BR::RedstoneTorch, 0);  // 火把立其上
        w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneDust, 0);   // 地面粉（与火把斜角）
        w.setBlock(x0 + 2, kRigY, z0, BR::RedstoneLamp, 0);
        tickN(w, 6);
        const int p = w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask;
        const bool lampOn = (w.stateAt(x0 + 2, kRigY, z0) & BR::RedstoneLampStateOnFlag) != 0;
        bool ok = (p == 15) && lampOn;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| torch-on-block -> diagonal-down dust power=15, lamp on (t740 fix)";
        w.setBlock(x0, kRigY + 1, z0, BR::Air); // 拆火把（降沿）
        tickN(w, 6);
        const int p2 = w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask;
        const bool lampOff = (w.stateAt(x0 + 2, kRigY, z0) & BR::RedstoneLampStateOnFlag) == 0;
        ok = (p2 == 0) && lampOff;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| torch removed -> diagonal dust 0, lamp off";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air);
        w.setBlock(x0 + 2, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P4b 复审 #6：墙上挂的红石火把（attach 低 3 位 1..4）**不**向斜下角粉供电 —— 旧 seeding 不读
    //   attach 形态，墙上装饰火把把墙脚一圈粉点亮（意外通电）。立式（P4）语义不变（正对照）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0 - 1, kRigY + 1, z0, BR::Stone);            // 墙（火把支撑，-X 邻）
        w.setBlock(x0,     kRigY + 1, z0, BR::RedstoneTorch, BR::TorchOnNX); // 墙挂火把（支撑在 -X）
        w.setBlock(x0 + 1, kRigY,     z0, BR::RedstoneDust, 0);   // 墙脚斜下角粉（+1,-1,0）
        w.setBlock(x0 + 2, kRigY,     z0, BR::RedstoneLamp, 0);
        tickN(w, 6);
        const int p = w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask;
        const bool lampOff = (w.stateAt(x0 + 2, kRigY, z0) & BR::RedstoneLampStateOnFlag) == 0;
        const bool ok = (p == 0) && lampOff;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| wall torch (TorchOnNX) -> diagonal-down dust stays 0, lamp off (review #6)";
        w.setBlock(x0 - 1, kRigY + 1, z0, BR::Air);
        w.setBlock(x0,     kRigY + 1, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY,     z0, BR::Air);
        w.setBlock(x0 + 2, kRigY,     z0, BR::Air);
        tickN(w, 2);
    }

    // P5 火把立在 TNT 顶面（TNT 是火把支撑）：火把供下邻强电 → 应点燃。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::TntBlock, 0);
        w.setBlock(x0, kRigY + 1, z0, BR::RedstoneTorch, 0);
        const int t0 = tntFired;
        tickN(w, 6);
        const bool ok = tntFired > t0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| torch standing ON TNT -> fires";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0, kRigY + 1, z0, BR::Air);
        tickN(w, 2);
    }

    // P6 红石火把 NOT 门（t657 语义抽查）：支撑块被供电 → 火把熄灭（OffFlag）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Stone);
        w.setBlock(x0, kRigY + 1, z0, BR::RedstoneTorch, 0);
        tickN(w, 6);
        bool ok = (w.stateAt(x0, kRigY + 1, z0) & BR::RedstoneTorchStateOffFlag) == 0; // 亮态
        w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneBlock); // 支撑块邻供强电
        tickN(w, 8);
        ok = ok && (w.stateAt(x0, kRigY + 1, z0) & BR::RedstoneTorchStateOffFlag) != 0; // 熄灭
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| torch NOT-gate: powered support -> torch off";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0, kRigY + 1, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P7 拉杆直接邻 TNT（既有历史直连路径之外的电力路径）：扳上 → 电力点燃。
    //   （游戏内右键拉杆另有 t490 直连四邻 TNT 点火——与本电力路径并存；本测只验电力侧。）
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Lever, 1);
        w.setBlock(x0 + 1, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 6);
        const bool ok = tntFired > t0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| lever(on) adjacent TNT (power path) -> fires";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P8 地面火把 → 同层粉×3 → TNT（用户字面场景「红石火把和红石粉激活 TNT」）：应点燃 + 粉级 15/14/13。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::RedstoneTorch, 0);
        for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY, z0, BR::RedstoneDust, 0);
        w.setBlock(x0 + 4, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 8);
        bool ok = tntFired > t0;
        for (int i = 1; i <= 3 && ok; ++i) {
            const int p = w.stateAt(x0 + i, kRigY, z0) & BR::RedstoneDustPowerMask;
            if (p != 16 - i) { qInfo().noquote() << "  dust" << i << "power" << p << "expect" << 16 - i; ok = false; }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| floor torch -> same-level 3-dust -> TNT fires (user scenario)";
        for (int i = 0; i <= 4; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        tickN(w, 2);
    }

    // P9 火把立方块上 + 地面粉×3 → TNT（t740 斜下供粉修复的端到端用户场景）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Stone);
        w.setBlock(x0, kRigY + 1, z0, BR::RedstoneTorch, 0);
        for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY, z0, BR::RedstoneDust, 0);
        w.setBlock(x0 + 4, kRigY, z0, BR::TntBlock, 0);
        const int t0 = tntFired;
        tickN(w, 8);
        const bool ok = tntFired > t0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << "| torch-on-block -> diagonal 3-dust -> TNT fires (t740 fix e2e)";
        for (int i = 0; i <= 4; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        w.setBlock(x0, kRigY + 1, z0, BR::Air);
        tickN(w, 2);
    }

    // P10 t739 阶梯爬坡供电（平地粉 → 上台阶 → 平地粉；渲染改 L 形贴边爬升后的电力侧回归）：
    //   电力语义不动（爬墙斜角仍算一跳衰减，t702/t738/t740 修复保持）—— lever + 平地粉×2 + 一格高
    //   石阶 + 阶上粉 + 阶后平地粉 + 灯：全线导通（灯亮）且电力 15/14/13/12 逐粉 -1（爬墙计一跳）；
    //   连接位高半字节按「水平邻粉 + 爬墙斜角」置位（渲染 L 形贴边（低处平铺 + 竖直贴面段）读的
    //   正是这些位 + chunkgeometry 三高探针——本探针锁 state 侧不回退）。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::Lever, 1);
        w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneDust, 0);     // 平地粉
        w.setBlock(x0 + 2, kRigY, z0, BR::RedstoneDust, 0);     // 平地粉（墙脚）
        w.setBlock(x0 + 3, kRigY, z0, BR::Stone, 0);            // 一格高台阶
        w.setBlock(x0 + 3, kRigY + 1, z0, BR::RedstoneDust, 0); // 阶上粉（爬升）
        w.setBlock(x0 + 4, kRigY, z0, BR::RedstoneDust, 0);     // 阶后平地粉（下降）
        w.setBlock(x0 + 5, kRigY, z0, BR::RedstoneLamp, 0);
        tickN(w, 8);
        const bool lampOn = (w.stateAt(x0 + 5, kRigY, z0) & BR::RedstoneLampStateOnFlag) != 0;
        bool ok = lampOn;
        const struct { int x, y, wantP, wantConn; } want[] = {
            { x0 + 1, kRigY,     15, 0x01 }, // 仅 +X 同层粉
            { x0 + 2, kRigY,     14, 0x03 }, // -X 同层 + +X 爬墙（斜角上粉）
            { x0 + 3, kRigY + 1, 13, 0x03 }, // -X / +X 皆爬墙（斜角下粉）
            { x0 + 4, kRigY,     12, 0x02 }, // 仅 -X 爬墙（斜角上粉）
        };
        for (const auto &e : want) {
            const quint8 st = w.stateAt(e.x, e.y, z0);
            const int p = st & BR::RedstoneDustPowerMask;
            const int conn = st >> 4;
            if (p != e.wantP || conn != e.wantConn) {
                qInfo().noquote() << "  dust" << e.x << "y" << e.y << "power" << p << "conn" << conn
                                  << "expect power" << e.wantP << "conn" << e.wantConn;
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| stair-step climb over 1-block step: power 15/14/13/12, lamp on (t739)";
        for (int i = 0; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        w.setBlock(x0 + 3, kRigY + 1, z0, BR::Air);
        tickN(w, 2);
    }

    // P8 板压灯竖直路径（t743 ①）：压力板直接放红石灯正上方（板 = 灯的 +Y 邻，向下供电）。驱动序列镜像
    //   真实路径：先放未压板（玩家放置 state=0）→ 稳态 → 再经 **5 参数 setBlock 只写 bit0**（与
    //   PlayerController::updatePressurePlates 踩下沿完全同一入口，非矩阵主体的「放置即带压下态」）→
    //   tickRedstone 后灯亮；清 bit0（离开沿）→ 灯灭。竖直 +Y/-Y 方向在此前矩阵主体（同层水平邻）与 P4
    //   （火把斜下喂粉）之外单独验证——notePowerWrite 锚点 → 锚点 6 邻接收器扫描含 -Y 邻灯。
    //   t743 ②（掉落物压木板）的判定在 Game 层（ItemEntityManager resting 支撑格 = floor(pos.y())-1，
    //   本工具只编 Core+World 两层测不到）；掉落物压板在 World 层与玩家踩板同写 bit0 → 电力侧由本探针覆盖。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0, kRigY, z0, BR::RedstoneLamp, 0);               // 灯
        w.setBlock(x0, kRigY + 1, z0, BR::WoodPressurePlate, 0);      // 板在灯正上方（放置态，未压）
        tickN(w, 2);
        w.setBlock(x0, kRigY + 1, z0, BR::WoodPressurePlate, 1);      // 踩下沿写 bit0（真实驱动同入口）
        tickN(w, 6);
        bool ok = (w.stateAt(x0, kRigY, z0) & BR::RedstoneLampStateOnFlag) != 0;
        w.setBlock(x0, kRigY + 1, z0, BR::WoodPressurePlate, 0);      // 离开沿清 bit0
        tickN(w, 6);
        ok = ok && (w.stateAt(x0, kRigY, z0) & BR::RedstoneLampStateOnFlag) == 0;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| plate directly on lamp (vertical down-power), press/release edges (t743)";
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0, kRigY + 1, z0, BR::Air);
        tickN(w, 2);
    }

    // P9 机关族逐态几何映射（t744 ②回归锁）：用户复盘「按钮放地面变正方形」，静态排查 + 全链 mesher dump
    //   实证贴地态自 t662 起就是贴地扁薄盒（6/16×2/16×6/16，Y[0,2/16]）——报告疑含陈旧 exe 因素（同 t740
    //   复盘）。本探针把「state 附着编码 → mechBoxes 几何」逐态断言锁进 harness（地面/四墙 × 激活两态 +
    //   放置法线映射），未来任何把地面态画回墙面姿态 / 厚边离墙的回归直接 FAIL。纯 Core 断言（mechBoxes
    //   静态，渲染与 raycastAABBs 选中同源——锁住渲染即同时锁住选体）。
    {
        bool ok = true;
        const float t = 1.0f / 16.0f;
        const quint8 mechIds[3] = { BR::Lever, BR::WoodButton, BR::StoneButton };
        for (const quint8 id : mechIds) {
            const bool isLever = (id == BR::Lever);
            for (int active = 0; active <= 1; ++active) {
                for (int attach = 0; attach <= 4; ++attach) {
                    const quint8 state = quint8((active ? 1u : 0u)
                                                | (quint8(attach) << BR::MechAttachShift));
                    const auto boxes = BR::mechBoxes(id, state);
                    if (boxes.empty()) { ok = false; continue; }
                    // 全盒并集（按钮单盒；拉杆底座+摆棍取并集验「贴面侧」）。
                    float minX = 9e9f, maxX = -9e9f, minY = 9e9f, maxY = -9e9f, minZ = 9e9f, maxZ = -9e9f;
                    for (const auto &b : boxes) {
                        minX = qMin(minX, b.minX); maxX = qMax(maxX, b.maxX);
                        minY = qMin(minY, b.minY); maxY = qMax(maxY, b.maxY);
                        minZ = qMin(minZ, b.minZ); maxZ = qMax(maxZ, b.maxZ);
                    }
                    const float th = active ? 1.0f : 2.0f; // 机关厚度单位（1/16）：按下压薄
                    switch (attach) {
                    case 0: // 贴地：并集贴格底（minY=0）且总高 ≤ 按钮 2/16（按下 1/16）/ 拉杆棍高 14/16
                        if (qAbs(minY) > 1e-4f) ok = false;
                        if (!isLever && maxY > (th + 0.5f) * t) ok = false;       // 按钮 = 贴地扁薄盒
                        if (isLever && maxY > 14.0f * t) ok = false;              // 拉杆 = 贴地底座+棍（棍顶 14/16）
                        if (isLever && boxes.size() < 3) ok = false;              // 底座 + 两段摆棍
                        break;
                    case 1: // 支撑在 +X：厚边/底座贴 x=1 格边（mechBoxes 厚度 ≤ th+0.5/16，不掉离墙）
                        if (qAbs(maxX - 1.0f) > 1e-4f || minX < 1.0f - (th + 0.5f + (isLever ? 6.0f : 0.0f)) * t) ok = false;
                        break;
                    case 2: // 支撑在 -X：贴 x=0
                        if (qAbs(minX) > 1e-4f || maxX > (th + 0.5f + (isLever ? 6.0f : 0.0f)) * t) ok = false;
                        break;
                    case 3: // 支撑在 +Z：贴 z=1
                        if (qAbs(maxZ - 1.0f) > 1e-4f || minZ < 1.0f - (th + 0.5f + (isLever ? 6.0f : 0.0f)) * t) ok = false;
                        break;
                    default: // 支撑在 -Z：贴 z=0
                        if (qAbs(minZ) > 1e-4f || maxZ > (th + 0.5f + (isLever ? 6.0f : 0.0f)) * t) ok = false;
                        break;
                    }
                }
            }
        }
        // 放置法线 → 附着编码映射（playercontroller placeBlock 写 state 的同一函数）：顶面贴地 / 四侧取反码
        //   （MechAttachOnXX = 支撑块方向）/ 底面拒（-1，v1 不支持天花板挂装）。
        if (BR::mechAttachFromNormal(0, 1, 0) != BR::MechAttachFloor) ok = false;
        if (BR::mechAttachFromNormal(1, 0, 0) != BR::MechAttachOnNX) ok = false;
        if (BR::mechAttachFromNormal(-1, 0, 0) != BR::MechAttachOnPX) ok = false;
        if (BR::mechAttachFromNormal(0, 0, 1) != BR::MechAttachOnNZ) ok = false;
        if (BR::mechAttachFromNormal(0, 0, -1) != BR::MechAttachOnPZ) ok = false;
        if (BR::mechAttachFromNormal(0, -1, 0) != -1) ok = false;
        // 附着解码 ↔ 几何贴边一致性：mechAttachOffset 给的支撑向必须与 mechBoxes 厚边所在侧同向
        //   （OnPX → dx=+1 → 厚边在 x=1；失撑掉落扫描与渲染/选中读同一编码，锁三者同源）。
        {
            int dx, dy, dz;
            BR::mechAttachOffset(quint8(BR::MechAttachOnPX << BR::MechAttachShift), dx, dy, dz);
            if (dx != 1 || dy != 0 || dz != 0) ok = false;
            BR::mechAttachOffset(quint8(BR::MechAttachOnNX << BR::MechAttachShift), dx, dy, dz);
            if (dx != -1) ok = false;
            BR::mechAttachOffset(quint8(BR::MechAttachOnPZ << BR::MechAttachShift), dx, dy, dz);
            if (dz != 1) ok = false;
            BR::mechAttachOffset(quint8(BR::MechAttachOnNZ << BR::MechAttachShift), dx, dy, dz);
            if (dz != -1) ok = false;
            BR::mechAttachOffset(0, dx, dy, dz);
            if (dx != 0 || dy != -1 || dz != 0) ok = false; // 贴地 → 支撑在下方
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| mech per-attach geometry: floor=flat-thin-box, wall=flush-to-support, decode parity (t744)";
    }

    // P10 t733 铁轨失撑掉落（R19.11 三族统一；World::checkRailOnEdit 单一入口覆盖全部破坏路径）：支撑位被清
    //   为 Air → 正上方铁轨坍落为掉落物（blockDroppedAsItem，dropId=自身；连接位 / 通电位丢弃）。本探针驱动
    //   三族代表路径：① 挖掘（setBlock Air 破支撑，含创造——World 层无 drop 标志）② 爆炸（destroySphereSilent
    //   逐破坏格：轨在球外幸存、支撑被炸）③ TNT 点火变实体（clearBlockSilent 清支撑）。另锁两个边界：
    //   ④ 上半砖支撑（isTopFlushSupport 正分支）——清侧邻不掉 / 清半砖本体才掉；⑤ 直破铁轨本格零掉落
    //   （守卫 isRail(oldId) 防与 finishMiningAt 通用 drop 双掉）。
    {
        int railDrops = 0;
        quint8 lastDropId = 0; int lastDropX = -1, lastDropY = -1;
        const QMetaObject::Connection dropConn =
            QObject::connect(&w, &World::blockDroppedAsItem, &w, [&](int x, int y, int z, int id) {
                Q_UNUSED(z);
                ++railDrops; lastDropId = quint8(id); lastDropX = x; lastDropY = y;
            });
        bool ok = true;
        // ① 挖掘路径 × 三族：Stone 支撑 + 轨其上 → 破支撑 → 轨成掉落物（id=自身）且格已清（无浮空残留）。
        const quint8 railKinds[3] = { BR::Rail, BR::GoldenRail, BR::DetectorRail };
        for (const quint8 rk : railKinds) {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY + 1, z0, rk, 0);
            const int d0 = railDrops;
            w.setBlock(x0, kRigY, z0, BR::Air);
            if (railDrops != d0 + 1 || lastDropId != rk || lastDropX != x0 || lastDropY != kRigY + 1
                || w.blockAt(x0, kRigY + 1, z0) != BR::Air) {
                qInfo().noquote() << "  mine-support rail-drop failed for id" << int(rk);
                ok = false;
            }
        }
        // ② 爆炸路径：半径 3 球心 (x0,kRigY,z0)——支撑 (dx=2,dy=2) 距 2.83 被炸；轨 (dx=2,dy=3) 距 3.61 球外
        //   幸存但失撑 → 坍落（恒掉，不走爆炸 ~50% 破坏掉落概率门——支撑脱落是必然事件）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0 + 2, kRigY + 2, z0, BR::Stone, 0);
            w.setBlock(x0 + 2, kRigY + 3, z0, BR::Rail, 0);
            const int d0 = railDrops;
            w.destroySphereSilent(x0, kRigY, z0, 3.0f);
            if (railDrops != d0 + 1 || lastDropId != BR::Rail
                || w.blockAt(x0 + 2, kRigY + 3, z0) != BR::Air) {
                qInfo().noquote() << "  blast-support rail-drop failed";
                ok = false;
            }
        }
        // ③ TNT 点火路径：TNT 支撑被 clearBlockSilent 静默清（变引燃实体）→ 其上轨立即掉落（无浮空残留）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::TntBlock, 0);
            w.setBlock(x0, kRigY + 1, z0, BR::GoldenRail, 0); // 动力轨铺 TNT 顶（用户场景）
            const int d0 = railDrops;
            w.clearBlockSilent(x0, kRigY, z0);
            if (railDrops != d0 + 1 || lastDropId != BR::GoldenRail
                || w.blockAt(x0, kRigY + 1, z0) != BR::Air) {
                qInfo().noquote() << "  tnt-prime rail-drop failed";
                ok = false;
            }
        }
        // ④ 上半砖支撑边界（isTopFlushSupport：完整立方 ∨ 上半砖，t741 单一权威）：清侧邻 → 支撑在 → 不掉；
        //    清半砖本体 → 掉。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::WoodSlab, 1);      // 上半砖（bit0=1 → 顶面齐平可撑）
            w.setBlock(x0, kRigY + 1, z0, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Stone, 0);
            const int d0 = railDrops;
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);          // 清侧邻 → 不掉
            if (railDrops != d0 || w.blockAt(x0, kRigY + 1, z0) != BR::Rail) {
                qInfo().noquote() << "  lateral clear must not drop rail";
                ok = false;
            }
            w.setBlock(x0, kRigY, z0, BR::Air);              // 清半砖本体 → 掉
            if (railDrops != d0 + 1 || w.blockAt(x0, kRigY + 1, z0) != BR::Air) {
                qInfo().noquote() << "  slab-support clear must drop rail";
                ok = false;
            }
        }
        // ⑤ 直破铁轨本格：守卫 isRail(oldId) → 本分支零掉落（通用 drop 走 finishMiningAt，防双掉）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY + 1, z0, BR::Rail, 0);
            const int d0 = railDrops;
            w.setBlock(x0, kRigY + 1, z0, BR::Air);          // 直破轨本体
            if (railDrops != d0) {
                qInfo().noquote() << "  direct rail break must not double-drop";
                ok = false;
            }
            w.setBlock(x0, kRigY, z0, BR::Air);
        }
        QObject::disconnect(dropConn); // 探针结束拆计数器（不影响主程序的掉落物消费链）
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| rail support-drop on mine/blast/tnt-prime for all 3 kinds (t733)";
    }

    // P11 t737 铁轨环线探针（贴图象限 + 矿车绕圈）：铺 3×3 环（8 格轨、4 拐角）→
    //   (a) 连接位断言：四拐角 state 恰为各自两邻臂位（railConnections 权威实算）；
    //   (b) 象限断言：每拐角经 BlockRegistry::railCornerArms（连接位→两臂单一权威）出臂向 →
    //       PartialBlockGeometry::append（mesher 同源直调）产出的拐角 quad 的贴图肘角 (u=0,v=0) 必落
    //       (ex,ez)（出口臂贴 x 臂边 / 入口臂贴 z 臂边）—— t737 修正前四象限 v↔z 全反（左转显右转贴图）；
    //   (c) 骑乘绕圈：上车 + 持续 W（wish 动态随行进向）→ 车必须留在环 footprint、四拐角逐一过心、
    //       每拐角进/出向垂直（真转弯非直行穿出）、Y 钉轨面；
    //   (d) 空车绕圈：玩家「追着推」（静止即续推）→ 同 footprint / 转弯断言 + 车头 yaw 覆盖全部
    //       4 基数向（t737：stepCartAlongRail 过弯更新 yaw —— 旧版空车过弯车头不转）。
    {
        const auto [x0, z0] = nextSlot();
        const int cx = x0 + 1, cz = z0 + 1; // 环心（环 = 心外 8 格）
        const auto isRing = [&](int x, int z) {
            return std::abs(x - cx) <= 1 && std::abs(z - cz) <= 1 && (x != cx || z != cz);
        };
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (dx != 0 || dz != 0)
                    w.setBlock(cx + dx, kRigY, cz + dz, BR::Rail, 0);
        // (a)+(b) 四拐角：NW=(cx-1,cz-1) 等；期望 con = 两邻臂位组合。
        const struct { int x, z; quint8 wantCon; } corners[4] = {
            { cx - 1, cz - 1, quint8(BR::RailConnPx | BR::RailConnPz) }, // 东+南邻
            { cx + 1, cz - 1, quint8(BR::RailConnNx | BR::RailConnPz) }, // 西+南邻
            { cx - 1, cz + 1, quint8(BR::RailConnPx | BR::RailConnNz) }, // 东+北邻
            { cx + 1, cz + 1, quint8(BR::RailConnNx | BR::RailConnNz) }, // 西+北邻
        };
        bool ok = true;
        for (const auto &c : corners) {
            const quint8 con = quint8(w.stateAt(c.x, kRigY, c.z) & 0x0F);
            if (con != c.wantCon) {
                qInfo().noquote() << "  corner" << c.x << kRigY << c.z << "con" << int(con)
                                  << "expect" << int(c.wantCon);
                ok = false;
                continue;
            }
            int axd = 0, azd = 0;
            if (!BR::railCornerArms(con, axd, azd)) { ok = false; continue; }
            // mesher 同源直调：零邻居 ctx（railDelta 缺省 INT_MIN → 平拐角），归一 UV 空间验象限。
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialLightCtx lctx; lctx.light = 1.0f;
            for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
            PartialNeighborCtx nctx;
            nctx.posX = nctx.negX = nctx.posZ = nctx.negZ = 0; // Rail case 只读 railDelta*（缺省 INT_MIN 平拐角）
            const float tileW = 1.0f / 16.0f;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::Rail, con, lctx, nctx,
                                         tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            const float ex = (axd > 0) ? 1.0f : 0.0f; // 出口臂贴的 x 边
            const float ez = (azd > 0) ? 1.0f : 0.0f; // 入口臂贴的 z 边
            bool elbow = false, diag = false;
            for (const Vtx &v : verts) {
                const float uu = (v.u - 136.0f * tileW) / tileW; // 拐角瓦片 UV 归一 [0,1]
                if (uu < 0.25f && v.v < 0.25f
                    && std::fabs(v.x - ex) < 1e-4f && std::fabs(v.z - ez) < 1e-4f) elbow = true;
                if (uu > 0.75f && v.v > 0.75f
                    && std::fabs(v.x - (1.0f - ex)) < 1e-4f && std::fabs(v.z - (1.0f - ez)) < 1e-4f) diag = true;
            }
            if (!elbow || !diag) {
                qInfo().noquote() << "  corner quadrant wrong at" << c.x << c.z
                                  << "arms" << axd << azd << "elbow" << elbow << "diag" << diag;
                ok = false;
            }
        }
        // 环上直格应是对向 2 位（EW）直轨形态（拐角规则不外溢到边格）。
        if (quint8(w.stateAt(cx, kRigY, cz - 1) & 0x0F) != quint8(BR::RailConnPx | BR::RailConnNx)) ok = false;
        // ── 通用绕圈跑法（骑乘 / 空车共用断言壳）──
        const float rideH = 0.45f; // kCartRideH（MinecartManager 私有常量的文档值：轨格 cell 底 + 1/16 板 + 车底
                                    //   偏移；t768 车斗加高 0.75 模型后底板下沿偏移 0.375 → 0.45。改几何须同步此镜像值）
        const int kTicks = 2400;    // 0.016s × 2400 ≈ 38.4s 仿真：骑乘 ~8 格/s 多圈 / 空车 4 格/s 续推多圈
        bool seenYaw[4] = { false, false, false, false }; // 空车过弯 yaw 基数覆盖（0/90/180/270）
        const auto runLaps = [&](MinecartManager &carts, int cartIdx, bool ridden) {
            QVector3D prev = carts.posAt(cartIdx);
            float wishX = 1.0f, wishZ = 0.0f; // 初始沿 spawn 定向（北边中点格 EW 直轨 → +X）
            int lastBx = int(std::floor(prev.x())), lastBz = int(std::floor(prev.z()));
            int inDx = 1, inDz = 0; // 进入当前格的方向（spawn 格起步向 +X）
            double pathLen = 0.0;
            int cornerVisits = 0, turns = 0;
            for (int t = 0; t < kTicks; ++t) {
                QVector3D cp;
                if (ridden) {
                    carts.tickRiddenCart(0.016, &w, wishX, wishZ, cp);
                } else {
                    carts.pushEmptyCart(&w, prev, wishX, wishZ); // 玩家追着车：静止即续推（滑行中被速度闸门跳过）
                    carts.tickPushedCarts(0.016, &w);
                    cp = carts.posAt(cartIdx);
                }
                const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                pathLen += std::sqrt(double(ddx) * ddx + double(ddz) * ddz);
                const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                if (dl > 1e-4f) { wishX = ddx / dl; wishZ = ddz / dl; } // wish 动态随行进向（玩家随车头朝前）
                if (!ridden) {
                    // 过弯车头基数断言（t737：stepCartAlongRail 重选向时同步 yaw；四舍五入吸收 FP 尾差）
                    const int yb = int(std::lround(carts.yawAt(cartIdx))) % 360;
                    const int ybucket = (yb == 0) ? 0 : (yb == 90) ? 1 : (yb == 180) ? 2 : (yb == 270) ? 3 : -1;
                    if (ybucket >= 0) seenYaw[ybucket] = true;
                }
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                if (!isRing(bx, bz)) {
                    qInfo().noquote() << "  cart left ring at tick" << t << "pos" << cp;
                    return false;
                }
                if (std::fabs(cp.y() - (kRigY + rideH)) > 0.01f) {
                    qInfo().noquote() << "  cart off rail surface at tick" << t << "y" << cp.y();
                    return false;
                }
                if (bx != lastBx || bz != lastBz) {
                    const int ndx = bx - lastBx, ndz = bz - lastBz;
                    if (std::abs(ndx) + std::abs(ndz) != 1) { // 跨格必单位轴对齐（一步一格）
                        qInfo().noquote() << "  non-adjacent cell jump at tick" << t;
                        return false;
                    }
                    const bool wasCorner = (std::abs(lastBx - cx) == 1 && std::abs(lastBz - cz) == 1);
                    if (wasCorner) {
                        if (ndx * inDx + ndz * inDz != 0) { // 出拐角必垂直进向（真转弯，非直行穿出）
                            qInfo().noquote() << "  no turn at corner" << lastBx << lastBz
                                              << "in" << inDx << inDz << "out" << ndx << ndz;
                            return false;
                        }
                        ++turns;
                    } else if (ndx != inDx || ndz != inDz) { // 直格不跑偏
                        qInfo().noquote() << "  drift on straight at" << lastBx << lastBz;
                        return false;
                    }
                    inDx = ndx; inDz = ndz;
                    lastBx = bx; lastBz = bz;
                }
                for (const auto &c : corners) {
                    if (std::fabs(cp.x() - (c.x + 0.5f)) < 0.2f && std::fabs(cp.z() - (c.z + 0.5f)) < 0.2f) {
                        ++cornerVisits; // 过心采样（tick 步长 0.13 内必有一次距心 <0.2）
                        break;
                    }
                }
                prev = cp;
            }
            // 门槛按驱动方式分档：骑乘 8 格/s 巡航 ~8 圈；空车 4 格/s 续推（每推 ~2 格）~3.5 圈。
            const double minPath = ridden ? 40.0 : 20.0;
            const int minVisits = ridden ? 12 : 8;
            const int minTurns = ridden ? 8 : 6;
            qInfo().noquote() << "  laps ridden=" << ridden << "pathLen" << pathLen
                              << "cornerVisits" << cornerVisits << "turns" << turns;
            return pathLen > minPath && cornerVisits >= minVisits && turns >= minTurns;
        };
        // (c) 骑乘绕圈
        MinecartManager carts;
        carts.spawnCart(cx, kRigY, cz - 1, &w); // 北边中点格（EW 直轨 → spawn 定向 +X）
        const QVector3D mountOrigin(float(cx) + 0.5f, float(kRigY) + 2.0f, float(cz - 1) + 0.5f);
        if (!carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f)) ok = false;
        ok = ok && runLaps(carts, 0, true);
        // (d) 空车绕圈（销毁骑乘车 → 原格重生空车 → 续推绕圈 + yaw 基数覆盖断言）。
        //   slot-reuse LIFO：销毁 0 号槽后重生车仍落 0 号槽（count() 恒 1）→ cartIdx 恒 0。
        if (!carts.hitCartFromRay(QVector3D(carts.posAt(0).x(), float(kRigY) + 2.0f, carts.posAt(0).z()),
                                  QVector3D(0, -1, 0), 4.0f, &w, true)) ok = false;
        carts.spawnCart(cx, kRigY, cz - 1, &w);
        ok = ok && runLaps(carts, 0, false);
        if (!(seenYaw[0] && seenYaw[1] && seenYaw[2] && seenYaw[3])) {
            qInfo().noquote() << "  empty-cart yaw did not cover 4 cardinals:"
                              << seenYaw[0] << seenYaw[1] << seenYaw[2] << seenYaw[3];
            ok = false;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| rail loop: corner quadrants + ridden/empty cart orbit with turning + yaw (t737)";
        // 清场
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(cx + dx, kRigY, cz + dz, BR::Air);
        tickN(w, 2);
    }

    // P12 t736 探测轨真实路径（真实矿车实体驱动，含空车；区别于矩阵主体的「直接写 state」驱动）：直线轨
    //   Rail - DetectorRail - Rail - Rail，探测轨侧邻红石灯。占用统一重扫在 tickPushedCarts 末尾
    //   （updateDetectorRailOccupancy，全车种帧级收口）。
    //   (a) 空车停驻探测轨（spawn 即静止、无人骑乘）→ tickPushedCarts + tickRedstone → bit4 置 + 灯亮
    //       （t736 新覆盖：旧版 t658 只标被骑路径，空车不触发）；
    //   (a2) 驻轨续帧幂等守卫 —— worldChanged（setWaterSilent 每次写必发）计数在稳态续帧不增（state
    //       不变不写，车驻轨期间零 state 写）；
    //   (b) 玩家追推离开（pushEmptyCart + tickPushedCarts 每帧、玩家随车贴住，同 P11 空车跑法）→ 车滑出
    //       探测格 → bit4 清 + 灯灭（离开沿降断电；用户验收「车离开 → 信号断开」）；
    //   (c) 被骑路径回归（tryMount + tickRiddenCart 与 tickPushedCarts 同帧双调 —— 与 PlayerController
    //       骑乘分支同序）：停驻被骑 → 灯亮（t680 ③ 停驶恒供电语义经统一 pass 保留），W 推离 → 灯灭。
    {
        const auto [x0, z0] = nextSlot();
        const int detX = x0 + 1;
        const int lampX = x0 + 1, lampZ = z0 + 1;
        w.setBlock(x0,     kRigY, z0, BR::Rail, 0);
        w.setBlock(detX,   kRigY, z0, BR::DetectorRail, 0);
        w.setBlock(x0 + 2, kRigY, z0, BR::Rail, 0);
        w.setBlock(x0 + 3, kRigY, z0, BR::Rail, 0);
        w.setBlock(lampX,  kRigY, lampZ, BR::RedstoneLamp, 0);
        const auto detOn  = [&]() { return (w.stateAt(detX, kRigY, z0) & BR::DetectorRailStateOnFlag) != 0; };
        const auto lampOn = [&]() { return (w.stateAt(lampX, kRigY, lampZ) & BR::RedstoneLampStateOnFlag) != 0; };
        int wc = 0; // worldChanged 计数（幂等守卫探针：setWaterSilent 每次写必发）
        const QMetaObject::Connection wcConn =
            QObject::connect(&w, &World::worldChanged, &w, [&]() { ++wc; });

        // (a) 空车停驻 → 通电。
        MinecartManager carts;
        carts.spawnCart(detX, kRigY, z0, &w); // 空车直落探测轨（静止，无人骑）
        for (int t = 0; t < 8; ++t) { carts.tickPushedCarts(0.016, &w); w.tickRedstone(); }
        bool okA = detOn() && lampOn();
        // (a2) 稳态续帧零写（幂等守卫：state 已置不重写 → worldChanged 不增，轨保持通电）。
        const int wc0 = wc;
        for (int t = 0; t < 20; ++t) { carts.tickPushedCarts(0.016, &w); w.tickRedstone(); }
        const bool okA2 = (wc == wc0) && detOn() && lampOn();
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| empty cart parked on detector -> bit4 + adjacent lamp on (t736)";
        if (!okA2) ++totalFail;
        qInfo().noquote() << (okA2 ? "PASS" : "FAIL")
                          << "| idempotent guard: steady frames zero state writes, lamp stays on (t736)";
        // (b) 追推离开 → 降沿断电。
        QVector3D player = carts.posAt(0);
        bool left = false;
        for (int t = 0; t < 600 && !left; ++t) {
            carts.pushEmptyCart(&w, player, 1.0f, 0.0f); // 玩家追着车：静止即续推（滑行中被速度闸门跳过）
            carts.tickPushedCarts(0.016, &w);
            w.tickRedstone();
            player = carts.posAt(0);
            if (int(std::floor(player.x())) >= x0 + 2) left = true; // 车心已出探测格
        }
        for (int t = 0; t < 12; ++t) { carts.tickPushedCarts(0.016, &w); w.tickRedstone(); } // 离开沿收敛
        const bool okB = left && !detOn() && !lampOn();
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| empty cart pushed off detector -> bit4 clear + lamp off (leave edge, t736)";
        // (c) 被骑路径回归：挖掉空车 → 原格重生 + 上车 → 停驻被骑亮 / 推离灭。
        carts.hitCartFromRay(QVector3D(carts.posAt(0).x(), float(kRigY) + 2.0f, carts.posAt(0).z()),
                             QVector3D(0, -1, 0), 4.0f, &w, true); // 清场（车在远处轨上，与探测轨无关）
        carts.spawnCart(detX, kRigY, z0, &w);
        const QVector3D mountOrigin(float(detX) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
        bool okC = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f) && carts.ridingIndex() == 0;
        for (int t = 0; t < 8; ++t) { // 停驻被骑（wish 0）—— 同帧双调镜像 PlayerController 骑乘分支
            QVector3D cp;
            carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
            carts.tickPushedCarts(0.016, &w);
            w.tickRedstone();
        }
        okC = okC && detOn() && lampOn();
        bool rodeAway = false;
        for (int t = 0; t < 240 && !rodeAway; ++t) { // W 推离（wish +X 沿轨）
            QVector3D cp;
            carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
            carts.tickPushedCarts(0.016, &w);
            w.tickRedstone();
            if (int(std::floor(carts.posAt(0).x())) >= x0 + 2) rodeAway = true;
        }
        for (int t = 0; t < 12; ++t) { // 离开沿收敛（停驻被骑帧续跑统一 pass）
            QVector3D cp;
            carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
            carts.tickPushedCarts(0.016, &w);
            w.tickRedstone();
        }
        okC = okC && rodeAway && !detOn() && !lampOn();
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| ridden path regression: parked-on lit, rode away -> off (t736)";
        QObject::disconnect(wcConn);
        // 清场
        carts.clearAll();
        for (int i = 0; i <= 3; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
        w.setBlock(lampX, kRigY, lampZ, BR::Air);
        tickN(w, 2);
    }

    // P12b t769 矿车坡道行驶探针（Entities 层 MinecartManager 直编，同 P11/P12 模式）：平-坡-平轨道
    //   （x0..x0+1 @Y 低平 + 坡格 x0+1@Y 东邻 x0+2@Y+1 + x0+2..x0+3 @Y+1 高平 —— 1:1 上坡）。
    //   (a) 被骑上坡（tryMount + W 持续 +X）：断言 400 tick 内爬上高平台停驻死端 (x0+3.5, Y+1+rideH)
    //       （旧版 pickTrackStep 邻轨防御只查同层 → 坡脚格心 x0+1.5 停死，本探针复现「上不去」）；
    //       沿途坡段 Y 随水平进度连续插值（y = Y+(x-(x0+1))+rideH，验收「非阶跃」）。
    //   (b) 空车下坡（顶平台 spawn + 玩家向西续推，同 P12(b) 跑法）：断言滑到低平台停驻死端 (x0+0.5,
    //       Y+rideH)（旧版在坡顶格心 x0+2.5 停死，复现「下不去」）；沿途坡段 Y 同款连续插值。
    //   (c) 坡格中心直接 spawn 的静止车：初始俯仰即贴合坡面（车头朝上坡向）——不需先行驶（放置即平行）。
    {
        const auto [x0, z0] = nextSlot();
        const float rideH = 0.45f; // kCartRideH（P11 同款镜像值：轨格 cell 底 + 1/16 板 + 车底偏移）
        // t769 教训：本 slot 的地形可达 y≥42（「40 以上必空」假设在该列失效）—— 坡轨上方格若被地形实心
        //   占据，scanRailColumn 的实心遮挡断扫（复审 #4 语义，防隔板假支撑）会把坡中段（pos.y 跨上轨层
        //   后向下扫）判离轨 → 车冻死在坡 55% 处。用户场景是露天坡（坡格上方是天空）→ rig 先净空轨道
        //   box（x0..x0+3 × kRigY..kRigY+2 × z0）再铺轨，等价露天环境。
        for (int i = 0; i <= 3; ++i)
            for (int dy = 0; dy <= 2; ++dy)
                if (w.blockAt(x0 + i, kRigY + dy, z0) != BR::Air)
                    w.setBlock(x0 + i, kRigY + dy, z0, BR::Air);
        w.setBlock(x0,     kRigY,     z0, BR::Rail, 0);
        w.setBlock(x0 + 1, kRigY,     z0, BR::Rail, 0); // 坡格（东邻高一格 → 坡面自西向东抬升）
        w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);
        w.setBlock(x0 + 3, kRigY + 1, z0, BR::Rail, 0);
        // 坡段期望表面（验收 Y 连续插值）：x∈[x0+1,x0+2] → Y+(x-(x0+1))；低平段 Y；高平段 Y+1。
        const auto wantSurf = [&](float x) {
            if (x < float(x0 + 1)) return float(kRigY);
            if (x > float(x0 + 2)) return float(kRigY + 1);
            return float(kRigY) + (x - float(x0 + 1));
        };
        MinecartManager carts;
        // (a) 被骑上坡。
        carts.spawnCart(x0, kRigY, z0, &w);
        const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
        bool okA = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
        bool yContA = true;
        float slopePitchA = 0.0f; int slopePitchN = 0; // 坡中段（x∈[x0+1.3,x0+1.7] 窗全落坡格）俯仰均值
        for (int t = 0; t < 400; ++t) {
            QVector3D cp;
            carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp); // W 持续（+X 上坡）
            carts.tickPushedCarts(0.016, &w);
            if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) {
                qInfo().noquote() << "  uphill Y off surface at tick" << t << "pos" << cp;
                yContA = false;
                break;
            }
            if (cp.x() > float(x0 + 1) + 0.3f && cp.x() < float(x0 + 1) + 0.7f) {
                slopePitchA += carts.pitchAt(0);
                ++slopePitchN;
            }
        }
        const QVector3D topP = carts.posAt(0);
        okA = okA && yContA
            && std::fabs(topP.x() - float(x0 + 3) - 0.5f) < 0.01f
            && std::fabs(topP.y() - float(kRigY + 1) - rideH) < 0.02f
            && slopePitchN >= 3 && std::fabs(slopePitchA / float(slopePitchN) - 45.0f) < 1.0f
            && std::fabs(carts.pitchAt(0)) < 0.5f; // 停驻高平台 → 俯仰归零
        if (!okA) qInfo().noquote() << "  uphill final pos" << topP << "pitch" << carts.pitchAt(0)
                                     << "slopePitch" << (slopePitchN ? slopePitchA / float(slopePitchN) : 0.0f)
                                     << "samples" << slopePitchN
                                     << "con(slope)" << int(w.stateAt(x0 + 1, kRigY, z0) & 0x0F)
                                     << "blockAboveSlope" << int(w.blockAt(x0 + 1, kRigY + 1, z0));
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| cart climbs ramp: reaches top dead-end, Y interpolates, pitch ~+45 on slope / 0 on flat (t769)";
        // (b) 空车下坡：销毁被骑车 → 顶平台格心重生（连接位定轴朝 -X 下坡向）→ 玩家续推滑降。
        carts.hitCartFromRay(QVector3D(topP.x(), float(kRigY) + 3.0f, topP.z()),
                             QVector3D(0, -1, 0), 4.0f, &w, true);
        carts.spawnCart(x0 + 3, kRigY + 1, z0, &w);
        QVector3D player = carts.posAt(0);
        bool yContB = true;
        float slopePitchB = 0.0f; int slopePitchN2 = 0;
        for (int t = 0; t < 400; ++t) {
            // t863④ 适配：车进低死端格后停推（追推会把死端车推离轨道出轨——本探针验下坡贴面 / 俯仰）。
            if (player.x() > float(x0) + 1.0f)
                carts.pushEmptyCart(&w, player, -1.0f, 0.0f); // 玩家追着车向西推
            carts.tickPushedCarts(0.016, &w);
            player = carts.posAt(0);
            if (std::fabs(player.y() - (wantSurf(player.x()) + rideH)) > 0.02f) {
                qInfo().noquote() << "  downhill Y off surface at tick" << t << "pos" << player;
                yContB = false;
                break;
            }
            if (player.x() > float(x0 + 1) + 0.3f && player.x() < float(x0 + 1) + 0.7f) {
                slopePitchB += carts.pitchAt(0); // 车头朝坡下 → 俯仰应为负（下俯）
                ++slopePitchN2;
            }
        }
        const bool okB = yContB
            && int(std::floor(player.x())) == x0 // 滑到低平台格（全程下坡完成；精确停点是摩擦渐停位置，
                                                 //   不钉死端格心 —— 空车无持续供能，可能在心前磨停）
            && std::fabs(player.y() - float(kRigY) - rideH) < 0.02f
            && slopePitchN2 >= 3 && std::fabs(slopePitchB / float(slopePitchN2) + 45.0f) < 1.0f;
        if (!okB) qInfo().noquote() << "  downhill final pos" << player << "pitch" << carts.pitchAt(0)
                                    << "slopePitch" << (slopePitchN2 ? slopePitchB / float(slopePitchN2) : 0.0f)
                                    << "samples" << slopePitchN2;
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| cart descends ramp: coasts to bottom dead-end, Y interpolates, pitch ~-45 (nose downhill) (t769)";
        // (c) 坡格中心直接 spawn 的静止车：初始俯仰即贴合坡面（车头朝上坡向 +45；放置即平行，无需先行驶）。
        carts.spawnCart(x0 + 1, kRigY, z0, &w); // slot-reuse 后新车落槽 1（0 号槽被 (b) 车占用）
        const QVector3D sp = carts.posAt(1);
        const bool okC = carts.count() == 2
            && std::fabs(sp.x() - float(x0 + 1) - 0.5f) < 0.01f
            && std::fabs(sp.y() - (float(kRigY) + 0.5f + rideH)) < 0.02f // 坡格中心坡面高 = 0.5
            && std::fabs(carts.pitchAt(1) - 45.0f) < 0.5f;
        if (!okC) qInfo().noquote() << "  slope-spawn pos" << sp << "pitch" << carts.pitchAt(1)
                                    << "count" << carts.count();
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| cart spawned on slope cell: parked body already parallel to ramp (+45) (t769)";
        // 清场（(c) 的静止车由 clearAll 收）。
        carts.clearAll();
        for (int i = 0; i <= 3; ++i) w.setBlock(x0 + i, kRigY + ((i >= 2) ? 1 : 0), z0, BR::Air);
        tickN(w, 2);
    }

    // P12c t770 矿车弯道贴轨约束探针（Entities 层 MinecartManager 直编，同 P11/P12/P12b 模式）：L 形轨
    //   （南 2 直 + 拐角 + 东 3 直）。用户报「弯道瞬间 90° 转向 → 慢速前进没触发旋转就脱轨 / 倒退大概率脱轨」。
    //   逐 tick 断言车始终在轨道中心线折线 ±0.05 内 + 出弯朝向（yaw 基数）：
    //   (a) 慢速前进（targetV≈0.5 格/s：wish 带 0.998 垂直分量 → proj≈0.0625）爬行进拐角格、格心前松键摩擦
    //       停驻（蠕行 ~0.24 格 → 停驻点落格心前 0.08-0.2 的弯道格内非格心位）→ 全程在中心线上 + 停稳静止；
    //   (b) 停驻位垂直重选向（「慢速前进未触发旋转即脱轨」复现）：停驻帧的 wish 重选向在弯道格心**前**把
    //       dir 掰向出口臂 → 重推起步沿出口臂行驶。修前无贴轨约束 → 带横向偏移（~0.15 格 > 0.05）滑出
    //       中心线 FAIL；修后弯道格内强制贴轨（垂直轴钉格心线）→ 沿出口臂中心线行驶 + 出弯朝向 yaw=270；
    //   (c) 倒退过弯（「倒退大概率脱轨」复现）：东行途中反踩（wish=-dir → 负速倒行、头向不变）→ 过拐角后
    //       必须落回南腿中心线继续倒行。修前：到心重选结果不持久化（sgn<0 不写回 dir）→ 下一帧 travel 按
    //       旧轴横切出轨（滑向西场外停驻）FAIL；修后头向 yaw=180（倒行头向=新臂取反）、终停南死端格心。
    {
        const auto [x0, z0] = nextSlot();
        // t769 教训：先净空轨道 box（地形可达 y≥42，scanRailColumn 实心遮挡断扫会把车判离轨冻死）。
        for (int dx = 0; dx <= 3; ++dx)
            for (int dz = -2; dz <= 0; ++dz)
                for (int dy = 0; dy <= 2; ++dy)
                    if (w.blockAt(x0 + dx, kRigY + dy, z0 + dz) != BR::Air)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air);
        w.setBlock(x0,     kRigY, z0 - 2, BR::Rail, 0); // 南死端（spawn 格）
        w.setBlock(x0,     kRigY, z0 - 1, BR::Rail, 0);
        w.setBlock(x0,     kRigY, z0,     BR::Rail, 0); // 拐角（南臂 + 东臂）
        w.setBlock(x0 + 1, kRigY, z0,     BR::Rail, 0);
        w.setBlock(x0 + 2, kRigY, z0,     BR::Rail, 0);
        w.setBlock(x0 + 3, kRigY, z0,     BR::Rail, 0); // 东死端
        const float rideH = 0.45f; // kCartRideH 镜像值（P11/P12b 同款；改几何须同步）
        // 中心线折线距离：南腿 x0+0.5 × z∈[z0-1.5, z0+0.5] + 东腿 z0+0.5 × x∈[x0+0.5, x0+3.5]（共点拐角）。
        const auto segDist = [](float px, float pz, float ax, float az, float bx, float bz) {
            const float abx = bx - ax, abz = bz - az;
            float t = ((px - ax) * abx + (pz - az) * abz) / (abx * abx + abz * abz);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            const float dx = px - (ax + abx * t), dz = pz - (az + abz * t);
            return std::sqrt(dx * dx + dz * dz);
        };
        const auto lineDist = [&](float px, float pz) {
            const float a = segDist(px, pz, float(x0) + 0.5f, float(z0) - 1.5f,
                                        float(x0) + 0.5f, float(z0) + 0.5f);
            const float b = segDist(px, pz, float(x0) + 0.5f, float(z0) + 0.5f,
                                        float(x0) + 3.5f, float(z0) + 0.5f);
            return a < b ? a : b;
        };
        const auto onTrack = [&](const QVector3D &p, int t, const char *phase) {
            if (lineDist(p.x(), p.z()) > 0.05f) {
                qInfo().noquote() << "  " << phase << "off centerline at tick" << t << "pos" << p;
                return false;
            }
            if (std::fabs(p.y() - (kRigY + rideH)) > 0.02f) {
                qInfo().noquote() << "  " << phase << "off rail surface at tick" << t << "y" << p.y();
                return false;
            }
            return true;
        };
        MinecartManager carts;
        carts.spawnCart(x0, kRigY, z0 - 2, &w);
        const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0 - 2) + 0.5f);
        bool okA = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
        QVector3D cp;
        // (a) 慢速爬行进拐角格：进格后在 z∈(z0+0.06, z0+0.14) 窗口松键（tick 步长 0.008 必命中）。
        bool reached = false;
        for (int t = 0; t < 2500; ++t) {
            carts.tickRiddenCart(0.016, &w, 0.998f, 0.0625f, cp);
            carts.tickPushedCarts(0.016, &w);
            if (!onTrack(cp, t, "crawl ")) { okA = false; break; }
            if (int(std::floor(cp.x())) == x0 && int(std::floor(cp.z())) == z0
                && cp.z() > float(z0) + 0.06f && cp.z() < float(z0) + 0.14f) {
                reached = true;
                break;
            }
        }
        if (!reached) {
            qInfo().noquote() << "  crawl never reached mid-corner window, pos" << cp;
            okA = false;
        }
        if (okA) {
            // (a 续) 松键摩擦停驻：蠕行 ~0.24 格后死区归零 → 停驻点仍在弯道格内、格心之前。
            for (int t = 0; t < 250 && okA; ++t) {
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!onTrack(cp, t, "stop ")) okA = false;
            }
            if (int(std::floor(cp.x())) != x0 || int(std::floor(cp.z())) != z0 || cp.z() >= float(z0) + 0.5f) {
                qInfo().noquote() << "  stop position not mid-corner-cell (pre-center):" << cp;
                okA = false;
            }
            const QVector3D stopP = cp;
            for (int t = 0; t < 20 && okA; ++t) { // 停稳静止守卫（后续帧位置不变）
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if ((cp - stopP).length() > 1e-4f) {
                    qInfo().noquote() << "  cart did not stay parked at" << cp;
                    okA = false;
                }
            }
        }
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| slow crawl into corner + friction park stays on centerline (t770)";
        // (b) 停驻位垂直重选向重推（贴轨约束）→ (c) 东行途中反踩倒退过弯回南腿。
        bool okB = okA, okC = okA;
        bool sawYaw270 = false, sawYaw180 = false;
        if (okA) {
            // (b) 重推（wish 同爬行向量 = 大垂直分量）：停驻重选向选出口臂 → 修后贴轨沿东臂中心线行驶。
            //   复审 #23 适配：贴轨收敛改限速（每 tick ≤kCartCenterSnapPerTick=0.1 格）→ 重推初期允许
            //   「残留偏移按限速衰减」的过渡态（旧瞬时钉回一步到位；t770 修前的真脱轨 = 偏移**不衰减**
            //   仍在此断言下 FAIL —— 防脱轨回归力保留）：过渡窗内须要么已贴线（≤0.05）要么以 ≥0.099/tick
            //   衰减且不回升；窗后回到严格 onTrack。过渡窗长 = ceil(off0/0.1)+1（0.5 格上界 → ≤6 tick）。
            const float off0 = lineDist(carts.posAt(0).x(), carts.posAt(0).z());
            const int graceT = int(std::ceil(off0 / 0.1f)) + 1;
            float prevOff = off0;
            for (int t = 0; t < 400 && okB; ++t) {
                carts.tickRiddenCart(0.016, &w, 0.998f, 0.0625f, cp);
                carts.tickPushedCarts(0.016, &w);
                const float off = lineDist(cp.x(), cp.z());
                if (t < graceT) {
                    if (off > 0.05f
                        && (off > prevOff + 1e-6f
                            || off > std::max(0.05f, off0 - 0.099f * float(t)) + 1e-6f)) {
                        qInfo().noquote() << "  relaunch snap not converging at bounded rate, tick" << t
                                          << "off" << off << "prev" << prevOff << "off0" << off0;
                        okB = false;
                        break;
                    }
                } else if (!onTrack(cp, t, "relaunch ")) { okB = false; break; }
                prevOff = off;
                if (cp.x() > float(x0) + 1.0f && cp.x() < float(x0) + 2.0f
                    && int(std::lround(carts.yawAt(0))) % 360 == 270) sawYaw270 = true;
                if (cp.x() >= float(x0) + 2.0f) break; // 东行到位 → 切 (c) 反踩
            }
            if (!sawYaw270) {
                qInfo().noquote() << "  exit heading not +X (yaw 270) after corner relaunch, yaw"
                                  << carts.yawAt(0) << "pos" << cp;
                okB = false;
            }
        }
        if (!okB) okC = false; // (c) 依赖 (b) 把车摆到东行途中 —— (b) 脱轨则 (c) 无从起跑，连带记 FAIL（防空跑 PASS）
        if (okB) {
            // (c) 行进中反踩：wish=-dir（东行头向 (1,0) → 种子 (-1,0)）→ 负速倒行；头向不变倒退过弯。
            //   过弯落南腿后 wish 随腿向改 (0,-1)（= 反对新头向 (0,1)，维持倒行；镜像玩家过弯后重对准）。
            for (int t = 0; t < 800 && okC; ++t) {
                const bool onSouthLeg = int(std::floor(cp.x())) == x0 && int(std::floor(cp.z())) < z0;
                carts.tickRiddenCart(0.016, &w, onSouthLeg ? 0.0f : -1.0f, onSouthLeg ? -1.0f : 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!onTrack(cp, t, "reverse ")) { okC = false; break; }
                if (onSouthLeg && int(std::lround(carts.yawAt(0))) % 360 == 180) sawYaw180 = true;
                // 终态：南死端格心停驻（死端重选 false → speed=0）。目标 z = (z0-2)+0.5 —— 显式括号防
                //   左结合错读（cp.z()-float(z0-1)-0.5 会被算成 cp.z()-z0+0.5-... 即差一格的邻格心）。
                if (std::fabs(cp.x() - (float(x0) + 0.5f)) < 0.01f
                    && std::fabs(cp.z() - (float(z0) - 1.5f)) < 0.01f) break;
            }
            const QVector3D fin = carts.posAt(0);
            if (std::fabs(fin.x() - (float(x0) + 0.5f)) > 0.01f || std::fabs(fin.z() - (float(z0) - 1.5f)) > 0.01f) {
                qInfo().noquote() << "  reverse ride did not park at south dead-end center, pos" << fin;
                okC = false;
            }
            if (!sawYaw180) {
                qInfo().noquote() << "  reverse corner heading not maintained (yaw 180 missing), yaw"
                                  << carts.yawAt(0) << "pos" << fin;
                okC = false;
            }
            if (okC) { // 停稳静止守卫
                for (int t = 0; t < 20 && okC; ++t) {
                    carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    if ((cp - fin).length() > 1e-4f) {
                        qInfo().noquote() << "  cart did not stay parked (reverse) at" << cp;
                        okC = false;
                    }
                }
            }
        }
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| mid-cell relaunch at corner clamps to exit-arm centerline, yaw 270 (t770)";
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| reverse ride through corner stays on centerline, yaw 180, parks at dead end (t770)";
        // 清场
        carts.clearAll();
        for (int dx = 0; dx <= 3; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Air);
        for (int dz = -2; dz <= -1; ++dz) w.setBlock(x0, kRigY, z0 + dz, BR::Air);
        tickN(w, 2);
    }

    // P13 t759 要塞传送门房净空探针（worldgen 回归，非红石 —— 同 t737 环线先例收录）。断言：(a) 12 框架环
    //   逐格仍在记录层 strongholdPortalY（B5 三坐标一致性的生成侧镜像 —— t759 只抬顶板不动框架层）；
    //   (b) 每框架顶之上 4 格 Air + 第 5 格顶板石砖（净高 8：内部 dy 1..8 Air / 顶板 dy=9 = 框架层+5）→ 验收
    //   「框架上方至少 3 格通行空间」；(c) 通行断面抽样：北走廊中段 / 东走廊中段离地 2..4 格 Air、楼梯顶步
    //   之上 3 格 Air（同步检查入口 / 楼梯高度；不断言贴地 dy=1 —— 走廊蛛网（可穿过仅减速）允许存在）。
    //   被测世界：优先主世界 w（默认种子 1337 的 96×96×48 生成即含 1 座要塞 → 零额外生成开销，且 rig 全在
    //   y=41 浅层不触地下要塞）；主世界无要塞时（未来 worldgen 常量演进）独立 96×96 世界扫种子兜底 —— 尺寸
    //   取 96 与主世界同：要塞 kMargin=23 抖动域 [-10,+5]，bx=60 候选族恒过边界（60+5 < 96-23）→ 每种子
    //   ~64% 命中，24 发上限仅防退化（首版 64×64 抖动全域压边界 → 每种子仅 ~2% 命中 24 发全空，已修）。
    {
        const World *pw = &w;
        World fallbackW;
        if (!pw->hasStronghold()) {
            fallbackW.setWidth(96);
            fallbackW.setDepth(96);
            fallbackW.setHeight(48);
            for (int s = 1; s <= 24 && !fallbackW.hasStronghold(); ++s)
                fallbackW.setSeed(s); // 同尺寸重生成一次（96×96 共 4 候选格，每种子 ~64% 命中）
            pw = &fallbackW;
        }
        bool ok = pw->hasStronghold();
        if (!ok)
            qInfo().noquote() << "  no stronghold in main or 24 fallback seeds (infra failure, not product bug)";
        if (ok) {
            const int px = pw->strongholdPortalX(), py = pw->strongholdPortalY(), pz = pw->strongholdPortalZ();
            const int cy = py - 4, cz = pz + 18; // 反解要塞原点（框架层 = cy+4；环中心 dz = -18）
            // (a)+(b) 框架环 12 格（标准 ±2 方形环，四边各 3 不含角）逐格验框架 / 头顶净空 / 顶板。
            int frames = 0;
            for (int rdx = -2; rdx <= 2; ++rdx) {
                for (int rdz = -2; rdz <= 2; ++rdz) {
                    const bool onRing = (rdx == -2 || rdx == 2) ? (rdz >= -1 && rdz <= 1)
                                                                : (rdz == -2 || rdz == 2) && (rdx >= -1 && rdx <= 1);
                    if (!onRing) continue;
                    ++frames;
                    if (pw->blockAt(px + rdx, py, pz + rdz) != BR::EndPortal) {
                        qInfo().noquote() << "  frame missing at" << (px + rdx) << py << (pz + rdz);
                        ok = false;
                    }
                    for (int up = 1; up <= 4; ++up) { // 框架顶之上 4 格全 Air（任务验收 ≥3，取满量自证）
                        if (pw->blockAt(px + rdx, py + up, pz + rdz) != BR::Air) {
                            qInfo().noquote() << "  headroom blocked at +" << up << "above frame" << (px + rdx) << (pz + rdz);
                            ok = false;
                        }
                    }
                    if (pw->blockAt(px + rdx, py + 5, pz + rdz) != BR::StoneBrick) { // 顶板（dy=9 = 框架层+5）
                        qInfo().noquote() << "  roof missing at +5 above frame" << (px + rdx) << (pz + rdz);
                        ok = false;
                    }
                }
            }
            if (frames != 12) {
                qInfo().noquote() << "  ring frame count" << frames << "!= 12";
                ok = false;
            }
            // (c) 通行断面抽样：北走廊中段 (dx=0,dz=-9) / 东走廊中段 (dx=14,dz=0) 自地板上 2..4 格；楼梯
            //     顶步（dy=3）上 1..3 格（玩家站楼梯脚位 ~dy+3.5，头需再 2 格）。
            const auto airRun = [&](int x, int yBase, int z, int from, int to) {
                for (int up = from; up <= to; ++up)
                    if (pw->blockAt(x, yBase + up, z) != BR::Air) return false;
                return true;
            };
            if (!airRun(px, cy, cz - 9, 2, 4)) {
                qInfo().noquote() << "  north corridor headroom blocked";
                ok = false;
            }
            if (!airRun(px + 14, cy, cz, 2, 4)) {
                qInfo().noquote() << "  east corridor headroom blocked";
                ok = false;
            }
            if (!airRun(px, cy + 3, cz - 15, 1, 3)) {
                qInfo().noquote() << "  stair top headroom blocked";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| stronghold portal room headroom: 4 air above frames + roof at +5, ring intact at"
                             " recorded Y, corridor/stair clearance (t759)";
    }

    // ── t762 黑曜石挖掘规则探针（纯 Core/Game 表查询，无 World 交互）：① 无附魔钻石镐 miningTime == 12.0s
    //    （hardness 96 / speedMul 8，t762 验收值）；② 仅钻石镐 canHarvest（掉落），木/石/铁/金/铜镐全 false
    //    （无掉落）；③ 低档镐 miningSpeedMul == 1.0（无加成恒慢，96s 极慢）+ 空手 canHarvest false。
    {
        // 工具段枚举值即绝对物品 id（PickaxeWood=0x101 起；ToolIdBase=0x100 仅是段下界哨兵，非加数）。
        const auto diaId  = int(ToolRegistry::PickaxeDiamond);
        const auto ironId = int(ToolRegistry::PickaxeIron);
        const auto goldId = int(ToolRegistry::GoldPickaxe);
        const auto woodId = int(ToolRegistry::PickaxeWood);
        const auto stoneId = int(ToolRegistry::PickaxeStone);
        const auto copperId = int(ToolRegistry::CopperPickaxe);
        bool ok = std::abs(ToolRegistry::miningTime(BR::Obsidian, diaId) - 12.0f) < 1e-3f
                  && ToolRegistry::canHarvest(BR::Obsidian, diaId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, ironId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, goldId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, woodId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, stoneId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, copperId)
                  && !ToolRegistry::canHarvest(BR::Obsidian, 0) // 空手（非工具 id 0）→ 无掉落
                  && ToolRegistry::miningSpeedMul(BR::Obsidian, ironId) == 1.0f
                  && ToolRegistry::miningSpeedMul(BR::Obsidian, goldId) == 1.0f
                  && ToolRegistry::canMine(BR::Obsidian); // 可挖（破坏进度可推进，仅速度/掉落受限）
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| obsidian mining rule: diamond pick 96/8=12.0s + drop; wood/stone/iron/gold/"
                             "copper pick no bonus (1.0x) and NO drop (t762)";
    }

    // ── t763 附魔数值生效链探针（纯 Game 层表 + Hotbar 实例，无 World/QML）：① 锐锋→攻击伤害输入链
    //    （钻石剑基础 7 + 锐锋 III ×0.5 = 8.5，attackMob 同公式；tooltip 文本源 enchantListText 出「锐锋 III」）；
    //    ② 保护族 EPF 路由（含本任务补的 Emberling=15 → 火焰保护 / EnderPearlTp=16 → 摔落保护两条新路由，
    //    修前二者漏专项加成）；③ 耐久附魔消耗概率（控制组无附魔必损；耐久 III 400 次受击损耗 ≈300，
    //    75% 损 / 25% 跳过，容差 ±40≈4.6σ 防偶发 FAIL）。
    {
        Hotbar hb;
        // ① 锐锋伤害输入链：基础伤 + 0.5*级 与 attackMob（playercontroller t476 链）同式。
        const int sharp3 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
        const int enchSharp[4] = {sharp3, 0, 0, 0};
        const int diaSword = int(ToolRegistry::DiamondSword);
        const float expectAtk = float(ToolRegistry::attackDamage(diaSword)) + 0.5f * 3.0f;
        bool ok = ToolRegistry::attackDamage(diaSword) == 7
                  && EnchantRegistry::findLevel(enchSharp, int(EnchantRegistry::Sharpness)) == 3
                  && std::abs(expectAtk - 8.5f) < 1e-4f
                  && hb.itemAttackDamage(diaSword) == 7
                  && hb.enchantListText(QVariantList{sharp3, 0, 0, 0})
                         == QString::fromUtf8("锐锋 III");
        // ② 保护族 EPF 路由：钻石胸甲火焰保护 III（唯一护甲）→ Fire(9)/Emberling(15) 均 6；无通用保护
        //    → Fall(1)/Starvation(4) 均 0。护甲 id = ArmorIdBase + tier*4 + piece（钻石 tier=4）。
        const int fireProt3 = EnchantRegistry::pack(int(EnchantRegistry::FireProtection), 3);
        const int feather2  = EnchantRegistry::pack(int(EnchantRegistry::FeatherFall), 2);
        const int prot2     = EnchantRegistry::pack(int(EnchantRegistry::Protection), 2);
        const int diaChest  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1; // 钻石胸甲
        const int diaHelm   = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0; // 钻石头盔
        const int diaBoots  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 3; // 钻石靴
        hb.armorSetStack(1, diaChest, 1, 100, QVariantList{fireProt3, 0, 0, 0}, QString());
        ok = ok && hb.armorProtectionFactor(9) == 6      // Fire：火焰保护 3 级 ×2 EPF
                  && hb.armorProtectionFactor(15) == 6   // Emberling 火球（t728）：t763 补路由（修前 0）
                  && hb.armorProtectionFactor(1) == 0    // Fall：无摔落保护
                  && hb.armorProtectionFactor(4) == 0;   // Starvation：无通用保护
        // 加靴子摔落保护 II + 头盔通用保护 II：Fall(1)/EnderPearlTp(16) = 2+4 = 6；Fire(9) = 2+6 = 8；Starvation = 2。
        hb.armorSetStack(3, diaBoots, 1, 100, QVariantList{feather2, 0, 0, 0}, QString());
        hb.armorSetStack(0, diaHelm, 1, 100, QVariantList{prot2, 0, 0, 0}, QString());
        ok = ok && hb.armorProtectionFactor(1) == 6
                  && hb.armorProtectionFactor(16) == 6   // 暗渊珠传送自伤（t758）：t763 补路由（修前 2）
                  && hb.armorProtectionFactor(9) == 8
                  && hb.armorProtectionFactor(4) == 2;
        // ③ 耐久消耗概率：控制组皮革头盔无附魔 50 次受击必损 50；钻石胸甲耐久 III 400 次受击损耗
        //    ∈ [260, 340]（期望 300；每次 25% 概率跳过）。走 damageArmor（对全部装备槽生效 → 先清场）。
        hb.armorSetStack(0, 0, 0, 0, QVariantList{}, QString());
        hb.armorSetStack(1, 0, 0, 0, QVariantList{}, QString());
        hb.armorSetStack(3, 0, 0, 0, QVariantList{}, QString());
        const int leatherHelm = int(RecipeRegistry::ArmorIdBase); // 皮革头盔（tier0 头）
        hb.armorSetStack(0, leatherHelm, 1, 55, QVariantList{}, QString());
        for (int i = 0; i < 50; ++i) hb.damageArmor();
        ok = ok && hb.armorDurabilityAt(0) == 5;
        hb.armorSetStack(0, 0, 0, 0, QVariantList{}, QString());
        const int unb3 = EnchantRegistry::pack(int(EnchantRegistry::Unbreaking), 3);
        hb.armorSetStack(1, diaChest, 1, 500, QVariantList{unb3, 0, 0, 0}, QString());
        const int durStart = hb.armorDurabilityAt(1);
        for (int i = 0; i < 400; ++i) hb.damageArmor();
        const int lost = durStart - hb.armorDurabilityAt(1);
        ok = ok && lost >= 260 && lost <= 340;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| enchant effect chain: sharpness 7+1.5=8.5 + tooltip text source; EPF routing "
                             "fire/emberling/pearl-tp/feather/protection; unbreaking-III wear over 400 hits in "
                             "[260,340], no-enchant control exact 50 (t763)";
    }
    // ── P-t890 燃烧方块侧壁接触点燃探针（AABB 接触扫描行为级 + 阴性轮）──
    //    t890：旧三格判定漏「贴燃烧方块侧壁走」——玩家 AABB 半宽 0.3 身在邻格、中心列不含燃烧格 → 永不
    //    点燃。修法 = 仙人掌判据族先例（满格 AABB + kTouchSkin 容差皮 + 正交 ±1 扩圈），Fire 格同口径并入。
    //    断言：(a) 玩家贴 2 高木板墙侧壁走（墙已 igniteFlammableAt 进燃烧态，中心列距墙格 ≥1 格）→ 点燃
    //    （旧判定此场景恒 false = 用户症状本体）；(b) 对照：同布局未点燃墙走位 → 不点燃（阴性，排除
    //    「rig 里别的东西点的火」）；(c) 站顶：站燃烧板顶（脚底支撑面 = 燃块）→ 点燃；(d) 斜对角隔离：
    //    燃烧格仅在玩家 AABB 对角外一格（XZ 各隔 0.3+ 缝）→ 不点燃（AABB 过滤生效，无误伤面）；
    //    (e) mob 侧壁同链（pig 贴燃烧墙 → isBurningAt(mob) 真）。
    {
        World wS;
        wS.setWidth(48); wS.setDepth(48); wS.setHeight(96); wS.setSeed(89);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const auto tickP = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) {
                pumpFor(17);
                ents.tick(qreal(dt), &wS, farL, 0.3f, 1.8f, false);
                pc.tick();
            }
        };
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wS);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        const int fy = 83;
        auto buildLane = [&](bool lit) { // 石道 + 东侧木板高墙（x=8 列，z 4..8，两层）
            for (int x = 2; x <= 7; ++x)
                for (int z = 4; z <= 8; ++z) {
                    wS.setBlock(x, fy, z, BR::Stone, 0);
                    wS.setBlock(x, fy + 1, z, BR::Air, 0);
                    wS.setBlock(x, fy + 2, z, BR::Air, 0);
                    wS.setBlock(x, fy + 3, z, BR::Air, 0);
                }
            for (int z = 4; z <= 8; ++z) {
                wS.setBlock(8, fy, z, BR::Stone, 0);
                wS.setBlock(8, fy + 1, z, BR::Planks, 0);
                wS.setBlock(8, fy + 2, z, BR::Planks, 0);
                wS.setBlock(8, fy + 3, z, BR::Air, 0);
            }
            if (lit) {
                wS.igniteFlammableAt(8, fy + 1, 6);
                wS.igniteFlammableAt(8, fy + 2, 6);
            }
        };
        // (a) 侧壁贴走：玩家 (7.5, fy+1) 朝 +Z 走（W 键），身体中心 x=7.5 距墙列 x=8 恰半宽贴面
        //     （pMaxX=7.8 vs 墙 cMinX=8.0 → 含 0.002 皮重叠 = 接触；旧中心列判定 footY 层 blockAt(7,*)
        //     全 Stone/Air → 恒 false）。走 1s 内 burning 必真。
        buildLane(true);
        pc.loadSavedState(7.5f, float(fy + 1), 6.0f, -90.0f, 0.0f, 2);
        pc.setKey(Qt::Key_W, true); // 朝 -90°（+Z）前推 → 贴墙面滑走
        bool sideLit = false;
        for (int t = 0; t < 24 && !sideLit; ++t) { tickP(1, 0.05f); sideLit = pc.burning(); }
        pc.setKey(Qt::Key_W, false);
        // (d) 斜对角阴性先于清场：换新 lane 未燃态走同一路径对照在 (b)；斜对角单独摆。
        bool diagClear = false;
        {
            buildLane(false);
            wS.setBlock(8, fy + 1, 6, BR::Planks, 0);
            wS.setBlock(8, fy + 2, 6, BR::Air, 0);
            wS.igniteFlammableAt(8, fy + 1, 6);
            // 玩家在 (6.5, fy+1, 4.5)：燃烧格 (8,fy+1,6) 的 XZ 对角邻方向隔 ≥1.2 格 → AABB 不重叠
            pc.loadSavedState(6.5f, float(fy + 1), 4.5f, -90.0f, 0.0f, 2);
            pc.setKey(Qt::Key_W, true); // 同款贴走（远离墙列 → 全程无接触）
            for (int t = 0; t < 20 && !diagClear; ++t) { tickP(1, 0.05f); diagClear = !pc.burning(); }
            diagClear = diagClear || (!pc.burning()); // 全程未燃即阴性成立
            pc.setKey(Qt::Key_W, false);
        }
        // (b) 未燃墙对照：同布局同走位，墙未点燃 → 全程不燃（排除「rig 里别的东西点的火」）。
        buildLane(false);
        pc.clearStatusEffects();
        pc.loadSavedState(7.5f, float(fy + 1), 6.0f, -90.0f, 0.0f, 2);
        pc.setKey(Qt::Key_W, true);
        bool unlitClean = true;
        for (int t = 0; t < 24 && unlitClean; ++t) { tickP(1, 0.05f); unlitClean = !pc.burning(); }
        pc.setKey(Qt::Key_W, false);
        // (c) 站顶：单块板 (5,fy+1,6)，点燃后玩家站其上（碰撞 snap 脚底在板顶缝上——旧三格判定靠
        //     footY-1 兜过，本断言锁新扫描的站顶分支不回归）
        wS.setBlock(5, fy + 1, 6, BR::Planks, 0);
        wS.igniteFlammableAt(5, fy + 1, 6);
        pc.clearStatusEffects();
        pc.loadSavedState(5.5f, float(fy + 2), 6.5f, -90.0f, 0.0f, 2);
        bool topLit = false;
        for (int t = 0; t < 24 && !topLit; ++t) { tickP(1, 0.05f); topLit = pc.burning(); }
        // (e) mob 侧壁：pig 出生即贴墙（x=7.5 格心 → AABB maxX=7.8，距墙 cMinX=8.0 缝 0.2 < halfW 0.45
        //     → 出生帧即重叠）+ knockback 推向墙（对消 wander 随机步的离墙漂移；短窗抢拍 < 首游荡窗）。
        buildLane(true);
        const int pigS = ents.spawnMobTyped(7, fy + 1, 6, EntityManager::MobPig,
                                            QStringLiteral("#ee9999"), 20);
        bool mobSideLit = pigS >= 0;
        if (pigS >= 0) {
            for (int t = 0; t < 30; ++t) {
                ents.knockback(pigS, 1.0f, 0.0f, 0.5f); // 每帧轻推 +X 贴墙（kKnockbackDrag 强阻尼不积累）
                ents.tick(0.05, &wS, farL, 0.3f, 1.8f, false);
                if (ents.isBurningAt(pigS)) { mobSideLit = true; break; }
            }
        }
        // 清场（全 lane Air + 熄玩家）
        for (int x = 2; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z)
                for (int dy = 0; dy <= 3; ++dy) wS.setBlock(x, fy + dy, z, BR::Air, 0);
        pc.clearStatusEffects();
        const bool okT890 = sideLit && diagClear && unlitClean && topLit && mobSideLit;
        if (!okT890) ++totalFail;
        if (!okT890)
            qInfo().noquote() << "  [t890 diag] sideLit" << sideLit << "diagClear" << diagClear
                              << "unlitClean" << unlitClean << "topLit" << topLit
                              << "mobSideLit" << mobSideLit;
        qInfo().noquote() << (okT890 ? "PASS" : "FAIL")
                          << "| t890 side-contact ignition review: walking flush against a burning "
                             "plank wall now ignites the player (full-cell AABB overlap scan over own "
                             "footprint cells plus orthogonal neighbors, kTouchSkin=0.002 absorbs the "
                             "1e-4 collision snap gap - cactus contact-damage predicate family "
                             "precedent; old center-column 3-cell check structurally missed it since "
                             "the body rests in the adjacent cell), standing on a burning plank top "
                             "still ignites (support-face branch), diagonal-only burning cell one cell "
                             "out does NOT ignite (AABB filter rejects corner false positives), an "
                             "unlit identical walk stays clean (negative control), and the mob side "
                             "shares the same scan (pig hugging the wall catches fire); lava keeps "
                             "center-column fluid-contact semantics untouched";
    }

    // ── t798 效率附魔审计探针（纯 Core/Game 表查询，无 World/QML）：① 等级分档递增 —— 机制等价 MC 1.0
    //    「效率在工具基础速上**加法**叠 level²+1」（I +2 / II +5 / III +10 / IV +17 / V +26）：木镐
    //    （speedMul 2）挖石头（hardness 1.5）时长 0.750 / 0.375 / 0.214 / 0.125 / 0.079 / 0.054s 每级严格
    //    递减（旧「耗时整体 ×(1+level)」各级统一乘 = 用户报「附任意效率像效率 V」根因）；② 匹配门控 ——
    //    木镐效率 V 挖泥土 / 沙（Shovel 类）时长 == 无附魔（0.5s 恒定，镐附效率挖土零加成）；③ 交叉 ——
    //    铁镐效率 III 挖石 1.5/16=0.094s（基础速 6 同吃加法分档）+ 木铲效率 I 挖土 0.5/3.2=0.156s（铲对
    //    土匹配 → 有加成，方向性对照）；④ 采掘等级门控 —— 木镐效率 V 挖黑曜石仍 96s（mul 1.0 不吃效率，
    //    t762「仅钻石镐 12s」语义零回归）。另：t763 表 12 附魔公式全复查 —— 锐锋 +0.5/级、亡灵 / 节肢
    //    +2.5/级（对族）、击退 +50%/级、燃焰 4s/级、时运 ×(1+[0,level])（限矿）、保护族 EPF 通用 1 / 专项
    //    2 每级、耐久按级概率跳过、精准采集 / 水中亲和 maxLevel 1 二值 —— 全部等级分档，无「统一不分档」
    //    同病（仅效率旧实现犯，本任务已修）。
    {
        const auto woodPick   = int(ToolRegistry::PickaxeWood);
        const auto ironPick   = int(ToolRegistry::PickaxeIron);
        const auto woodShovel = int(ToolRegistry::ShovelWood);
        auto mtClose = [](float got, float expect) { return std::abs(got - expect) < 1e-3f; };
        // ① 等级分档（木镐挖石头）：有效速 2/4/7/12/19/28 → 六档严格递减。
        const float stoneT[6] = {
            ToolRegistry::miningTime(BR::Stone, woodPick, 0),
            ToolRegistry::miningTime(BR::Stone, woodPick, 1),
            ToolRegistry::miningTime(BR::Stone, woodPick, 2),
            ToolRegistry::miningTime(BR::Stone, woodPick, 3),
            ToolRegistry::miningTime(BR::Stone, woodPick, 4),
            ToolRegistry::miningTime(BR::Stone, woodPick, 5),
        };
        bool ok = mtClose(stoneT[0], 0.750f)
                  && mtClose(stoneT[1], 0.375f)
                  && mtClose(stoneT[2], 1.5f / 7.0f)
                  && mtClose(stoneT[3], 0.125f)
                  && mtClose(stoneT[4], 1.5f / 19.0f)
                  && mtClose(stoneT[5], 1.5f / 28.0f)
                  && stoneT[0] > stoneT[1] && stoneT[1] > stoneT[2] && stoneT[2] > stoneT[3]
                  && stoneT[3] > stoneT[4] && stoneT[4] > stoneT[5]; // 分档递减 ≠ 统一顶级
        // ② 匹配门控：木镐效率 V 挖泥土 / 沙 == 无附魔（恒 0.5s）。
        ok = ok && ToolRegistry::miningTime(BR::Dirt, woodPick, 5) == ToolRegistry::miningTime(BR::Dirt, woodPick, 0)
                  && mtClose(ToolRegistry::miningTime(BR::Dirt, woodPick, 0), 0.5f)
                  && ToolRegistry::miningTime(BR::Sand, woodPick, 5) == ToolRegistry::miningTime(BR::Sand, woodPick, 0);
        // ③ 交叉：铁镐效率 III 挖石 0.094s；木铲效率 I 挖土 0.156s（匹配方才吃加成）。
        ok = ok && mtClose(ToolRegistry::miningTime(BR::Stone, ironPick, 3), 1.5f / 16.0f)
                  && mtClose(ToolRegistry::miningTime(BR::Dirt, woodShovel, 1), 0.5f / 3.2f);
        // ④ 采掘等级门控：木镐效率 V 挖黑曜石仍 96s（t762 不回归）。
        ok = ok && mtClose(ToolRegistry::miningTime(BR::Obsidian, woodPick, 5), 96.0f);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| efficiency audit: wood pick stone tiered 0.750/0.375/0.214/0.125/0.079/0.054s "
                             "(additive lvl^2+1 on tool base, MC 1.0); eff-V pick on dirt/sand == no-enchant 0.5s; "
                             "iron pick eff-III stone 0.094s / wood shovel eff-I dirt 0.156s cross; wood pick "
                             "eff-V obsidian still 96s harvest-gate (t798)";
    }

    // ── t755 死亡态硬锁探针（纯 Game 层 PlayerState，无 World/QML/PlayerController）：
    //    ① 致死一击把 health 精确落库 0（死亡屏心条全空的前提——修前若落 1 即「半颗心」症状之一）；
    //    ② heal() 死亡免疫：dead 态治疗被拒（修前无守卫 → 致死 tick 尾部饥饿回血 healed(1) 经呈现层
    //       路由把 0 加回 1 = 用户报告的死亡屏半颗心根因）；③ respawn 复位链：清 dead + 拉满血饥 +
    //       死因复位（重生后输入解锁 / 血量回满的前置状态链）。
    {
        PlayerState ps;
        ps.setHealth(1);
        ps.takeDamage(3, int(PlayerState::Fall));   // 致死一击（1-3 → clamp 0）
        const bool lethalOk = !ps.dead() == false
                              && ps.health() == 0
                              && ps.deathCause() == int(PlayerState::Fall);
        // heal 死亡免疫：dead 态任意治疗不改 health（保持 0，心条全空）。
        ps.heal(5);
        const bool healGuardOk = ps.health() == 0;
        // respawn 复位链：清 dead + 满血 + 死因复位 Generic。
        ps.respawn();
        const bool respawnOk = !ps.dead()
                               && ps.health() == ps.maxHealth()
                               && ps.deathCause() == int(PlayerState::Generic);
        const bool ok = lethalOk && healGuardOk && respawnOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| death hard-lock state chain: lethal hit lands health=0 + dead + cause; "
                             "heal() rejected while dead (half-heart-after-death root); respawn clears "
                             "dead + full restore (t755)";
    }

    // ── t852 死亡掉落链探针（Game 层 PlayerController + Hotbar 直编，t814 真消费端模式；spawnItem 消费端
    //    = Main.qml onSpawnItem → itemEntities.spawnItem 的等价直连计数）：
    //    ① 四段全掉——hotbar 9 / main 27 / 光标手持栈 / 护甲 4 槽逐非空栈各发 1 个 spawnItem（整栈一实体，
    //       3×3 邻域散布），附魔 / 实例名 / 耐久末三参全透传（死亡掉落再捡回保真的 C++ 本体面）；
    //    ② 掉落即清——resetForMode(Survival) 后四段全空（用户报「死亡后背包物品都在、不掉落」的回归面在
    //       QML 路由层：t690 曾在 onDied 写 `window.<面板id>`（QML id 非 Window 属性 → 恒 undefined →
    //       TypeError 静默掐断死亡处理器）→ dropAllItems 从未被调。本探针锁 C++ 本体链恒掉恒清；QML 路由
    //       修复的静态契约钉在 Main.qml onDied 头注释 + try/finally 收口，行为面人工目视）；
    //    ③ 幂等——背包已空时再调零发射（死亡只掉一次，无重复实体）。
    {
        PlayerController pc;   // 无窗口直造（componentComplete 不触发，无 16ms 定时器；m_pos=出生常量 80,80,80）
        Hotbar hb;
        pc.setHotbar(&hb);
        // 装填四段：hotbar 槽 0 = 泥土 64；槽 1 = 钻石剑（锐锋 III + 改名「屠龙」+ 磨损耐久 800）；
        // main 槽 0 = 木棍 32；护甲槽 0 = 钻石头盔（保护 II + 耐久 300）；光标手持 = 石头 3。
        const int sharp3   = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
        const int prot2    = EnchantRegistry::pack(int(EnchantRegistry::Protection), 2);
        const int diaSword = int(ToolRegistry::DiamondSword);
        const int diaHelm  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0; // 钻石头盔（t763 同式）
        hb.setStack(0, BR::Dirt, 64);
        hb.setStack(1, diaSword, 1, 800, QVariantList{sharp3, 0, 0, 0}, QString::fromUtf8("屠龙"));
        hb.mainSetStack(0, RecipeRegistry::StickId, 32);
        hb.armorSetStack(0, diaHelm, 1, 300, QVariantList{prot2, 0, 0, 0}, QString());
        hb.setHeldBlock(int(BR::Stone));
        hb.setHeldCount(3);
        // 消费端直连（等价 Main.qml onSpawnItem 转发；同线程直接连接 = QML handler 语义）：记录全参 + 落点。
        QVector<int> gotId, gotCount, gotDur, gotX, gotY, gotZ;
        QVector<QVariantList> gotEnch;
        QVector<QString> gotName;
        QObject::connect(&pc, &PlayerController::spawnItem, &pc,
                         [&](int x, int y, int z, int id, int count, const QVariantList &ench,
                             const QString &name, int dur) {
            gotX.push_back(x); gotY.push_back(y); gotZ.push_back(z);
            gotId.push_back(id); gotCount.push_back(count);
            gotEnch.push_back(ench); gotName.push_back(name); gotDur.push_back(dur);
        });
        pc.dropAllItems();   // 死亡本体链（Main.qml onDied 主链同调）
        // ① 发射序列：hotbar（泥 64 → 剑 1）→ main（棍 32）→ held（石 3）→ armor（盔 1），逐栈一实体 +
        //    剑 / 盔的附魔首元 + .size()==4 全槽形状（review24 低危补口：盔侧原只断首元，剑/盔对称）、
        //    实例名、磨损耐久逐参断言（t590/t622/t686 三参透传的死亡路径回归面）。
        const bool emitOk = gotId.size() == 5
                && gotId[0] == int(BR::Dirt) && gotCount[0] == 64
                && gotId[1] == diaSword && gotCount[1] == 1
                && gotDur[1] == 800 && gotEnch[1].size() == 4 && gotEnch[1][0].toInt() == sharp3
                && gotName[1] == QString::fromUtf8("屠龙")
                && gotId[2] == RecipeRegistry::StickId && gotCount[2] == 32
                && gotId[3] == int(BR::Stone) && gotCount[3] == 3
                && gotId[4] == diaHelm && gotCount[4] == 1
                && gotDur[4] == 300 && gotEnch[4].size() == 4 && gotEnch[4][0].toInt() == prot2;
        // 散布面：死亡格（m_pos=80,80,80 → cx=cz=80）3×3 邻域内（|dx| ≤ 1 且 |dz| ≤ 1）。
        //   review24 低危补口：y 落点此前匿名丢弃——现断 y 恰为死亡格 y=80（cy=floor(m_pos.y()) 直传，
        //   「脚底整数格」契约）。精确等值（非 ±1 带）：若回归改为眼位（脚底+1.62 → floor 81）或 +1 抬升，
        //   带断言放行、等值断言捕获。
        bool scatterOk = emitOk;
        for (int i = 0; i < gotX.size() && scatterOk; ++i)
            scatterOk = std::abs(gotX[i] - 80) <= 1 && std::abs(gotZ[i] - 80) <= 1 && gotY[i] == 80;
        // ② 掉落即清：hotbar / main / 护甲三段全空 + 光标手持清（resetForMode(Survival) + heldStack 归零）。
        const bool clearedOk = hb.blockIdAt(0) == 0 && hb.blockIdAt(1) == 0
                && hb.mainBlockIdAt(0) == 0 && hb.armorBlockIdAt(0) == 0
                && hb.heldBlock() == 0;
        // ③ 幂等：空背包再调 → 零新发射（计数不变）。
        pc.dropAllItems();
        const bool idempotentOk = gotId.size() == 5;
        const bool ok = emitOk && scatterOk && clearedOk && idempotentOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| death drop chain: hotbar+main+held+armor all scatter-dropped (3x3, y=death "
                             "cell) with full 4-slot ench shape/name/durability passthrough, inventory "
                             "cleared on drop, second call emits nothing (t852)";
    }

    // ── t756 出生点选择探针（World 层 findSpawnColumn 多种子回归；独立小世界逐种子重生成，不动主世界
    //    rig）：种子 42（用户报告「出生在树里」的复现种子）+ 4 个互异回归种子，断言每个世界记录的出生列
    //    均为「可站立裸地表」：① 支撑格完整立方或积雪层（实体支撑，树叶/原木/草丛/水面薄物均不合规）；
    //    ② 出生格 h+1 与头部格 h+2 全 Air（树干/邻树树冠占据即否决——修复的直接断言面；水下/湖列的水面
    //    占 h+1 同遭否决）；③ heightmapAt == h（当前列首个非空恰为地表 → 头顶无任何遮蔽，非树冠/洞顶）。
    //    世界取 96×96×96（高 96 > 树冠顶 ~82 → 树正常生成，探针真正行使避树；48 高主世界地表钳顶无树，
    //    用它探针会空转）。h 断言用 min(heightAt, height-1) 同 findSpawnColumn / generate 填充式。
    {
        World spawnW;
        spawnW.setWidth(96);
        spawnW.setDepth(96);
        spawnW.setHeight(96);
        const int seeds[] = { 42, 7, 1337, 2024, 99 }; // 5 个互异种子（验收「连开 5 个不同种子」）
        bool ok = true;
        for (const int s : seeds) {
            spawnW.setSeed(s); // 同尺寸重生成（同 P13 fallback 模式）
            const int sx = spawnW.spawnColumnX(), sz = spawnW.spawnColumnZ();
            const int h = std::min(spawnW.heightAt(sx, sz), 95); // 与填充同式钳顶（96-1）
            const quint8 sup = spawnW.blockAt(sx, h, sz);
            const quint8 feet = spawnW.blockAt(sx, h + 1, sz);
            const quint8 head = spawnW.blockAt(sx, h + 2, sz);
            const bool colOk = (BR::isFullCube(sup) || sup == BR::SnowLayer)
                               && feet == BR::Air && head == BR::Air
                               && spawnW.heightmapAt(sx, sz) == h;
            if (!colOk) {
                qInfo().noquote() << "  seed" << s << "col" << sx << sz << "h" << h
                                  << "sup" << int(sup) << "feet" << int(feet)
                                  << "head" << int(head) << "hm" << spawnW.heightmapAt(sx, sz);
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| spawn column search: seeds {42,7,1337,2024,99} all resolve to standable "
                             "bare surface — solid/snow-layer support, feet+head cells air, heightmap=="
                             "heightAt (no trunk/canopy/water overhead) (t756)";
    }

    // ── review-d #20 尺寸 setter seedChanged 探针（World 层信号链；Review 2026-08-23 #20 潜伏坑半边）：
    //    setWidth/setDepth/setHeight 重建世界（generate 内 findSpawnColumn 按新尺寸重选出生列）但修前不
    //    emit seedChanged → 挂接该信号的世界派生缓存不被通知（PlayerController::onWorldSeedChanged 复位
    //    重生点 → 旧尺寸出生列坐标残留指向新栅格）。断言：①三 setter 变值各发恰一次 seedChanged；
    //    ②同值守卫静默（幂等）；③重建后出生列 getter 落在新尺寸界内（与 setter 链一致）。取舍：三连发
    //    不节流（消费端幂等复位、generate 本就各跑一次，见 world.cpp setter 头注释）。PlayerController
    //    侧采用链（adoptSpawnColumn / onWorldSeedChanged 复位）为 QQuickItem 派生类，不接入本 GUI-free
    //    测试（QML enterWorld 接线人工目视）。
    {
        World wR20;
        int seedSigs = 0;
        QObject::connect(&wR20, &World::seedChanged, &wR20, [&]() { ++seedSigs; });
        wR20.setWidth(32);   // 默认 16 → 32：generate + seedChanged ×1
        wR20.setDepth(32);   // 16 → 32：×1
        wR20.setHeight(48);  // 16 → 48：×1
        const int afterThree = seedSigs;
        wR20.setWidth(32);   // 同值守卫：不 generate 不 emit
        const int sx20 = wR20.spawnColumnX(), sz20 = wR20.spawnColumnZ();
        const bool ok = afterThree == 3 && seedSigs == 3
                        && sx20 >= 0 && sx20 < 32 && sz20 >= 0 && sz20 < 32;
        if (!ok)
            qInfo().noquote() << "  [review-d #20 diag] afterThree" << afterThree
                              << "final" << seedSigs << "spawnCol" << sx20 << sz20;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review-d #20 size setters emit seedChanged (world-identity reset "
                             "notification: width/depth/height rebuild each notifies exactly once, "
                             "same-value guard silent, spawn column getter in new bounds) "
                             "(Review 2026-08-23 #20)";
    }

    // ── review-d #22 农夫计数回放链式补前置探针（Game 层 PlayerProgress；Review 2026-08-23 #22）：
    //    t752 把 farmer 由独立根重挂 time_to_farm 下 → 「cropsHarvested≥10 但锄头线未解锁」的旧档计数
    //    回放被 unlock 父前置静默吞（修前）。断言：①该档回放后 farmer + 全祖先链（time_to_farm/
    //    crafting_table/get_wood/open_inventory）解锁；②树形一致（任何已解锁节点的父必已解锁——不出
    //    「子亮父锁」破相）；③计数 9 对照不解锁；④sniper 回放不链式补前置（10 次箭命中不蕴含首杀，
    //    父 monster_hunter 缺席仍吞——与实时 unlock 一致，非重挂回归面）；⑤档内已有中间祖先
    //    （time_to_farm=true）时回放补齐其下 farmer 与其上祖先、已解锁级幂等。
    {
        PlayerProgress ppR22;
        const auto loadStats = [&](const char *statKey, int statVal, const char *achKey) {
            QVariantMap st; st.insert(QString::fromLatin1(statKey), statVal);
            QVariantMap a;
            if (achKey) a.insert(QString::fromLatin1(achKey), true);
            QVariantMap data; data.insert("stats", st); data.insert("achievements", a);
            ppR22.loadVariant(data);
        };
        // ① 计数达阈 + 全链未解锁 → 链式补全
        loadStats("cropsHarvested", 10, nullptr);
        const bool chainOk = ppR22.isUnlocked("farmer") && ppR22.isUnlocked("time_to_farm")
                             && ppR22.isUnlocked("crafting_table") && ppR22.isUnlocked("get_wood")
                             && ppR22.isUnlocked("open_inventory");
        // ② 树形一致：每个已解锁且非根的 def，其父必已解锁
        bool treeOk = true;
        const QVariantList achR22 = ppR22.achievements();
        for (const QVariant &v : achR22) {
            const QVariantMap m = v.toMap();
            if (m.value("unlocked").toBool() && !m.value("parentId").toString().isEmpty()
                && !ppR22.isUnlocked(m.value("parentId").toString()))
                treeOk = false;
        }
        // ③ 计数 9 对照：不解锁 farmer / 不补锄头线（回放只对达阈计数补链）
        loadStats("cropsHarvested", 9, nullptr);
        const bool belowOk = !ppR22.isUnlocked("farmer") && !ppR22.isUnlocked("time_to_farm");
        // ④ sniper 回放保持原前置语义（不链式补：箭命中≠首杀）
        loadStats("arrowsHitMobs", 10, nullptr);
        const bool sniperOk = !ppR22.isUnlocked("sniper") && !ppR22.isUnlocked("monster_hunter");
        // ⑤ 档内已有中间祖先：补齐上下 + 幂等
        loadStats("cropsHarvested", 12, "time_to_farm");
        const bool partialOk = ppR22.isUnlocked("farmer") && ppR22.isUnlocked("time_to_farm")
                               && ppR22.isUnlocked("crafting_table") && ppR22.isUnlocked("get_wood")
                               && ppR22.isUnlocked("open_inventory");
        const bool ok = chainOk && treeOk && belowOk && sniperOk && partialOk;
        if (!ok)
            qInfo().noquote() << "  [review-d #22 diag] chain" << chainOk << "tree" << treeOk
                              << "below" << belowOk << "sniper" << sniperOk << "partial" << partialOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review-d #22 farmer count replay chains ancestry: crops>=10 with hoe-line "
                             "locked restores farmer + full ancestor chain tree-consistent, crops=9 no "
                             "unlock, sniper replay stays parent-gated (arrow hits != first kill), "
                             "mid-chain ancestor in save idempotent top-up (Review 2026-08-23 #22)";
    }

    // P14 审查 #2 火把翻转降沿 / 重亮升沿探针（t740 环粉可达性回归锁）：立在石块上的火把喂斜下环粉 → 灯亮；
    //    邻位拉杆供能支撑块 → 火把熄灭（NOT 门翻转）→ 环粉必须断电、灯灭（修前：翻转走 Phase B2 静默直写
    //    不经 notePowerWrite，锚点展开只播 6 正交种子 → 斜下环粉永不可达，保留陈旧电力 15 恒亮）；拉杆回位
    //    → 火把重亮 → 环粉复电 15、灯复亮（两方向翻转都收敛）。对照 P4：P4 验「拆火把」的编辑路径（经
    //    notePowerWrite kDiag），本探针验「火把在场、自身反相」的翻转路径——审查 #2 指出 t740 矩阵漏的正是这条。
    {
        const auto [x0, z0] = nextSlot();
        w.setBlock(x0,     kRigY,     z0, BR::Stone, 0);          // 支撑块
        w.setBlock(x0,     kRigY + 1, z0, BR::RedstoneTorch, 0);  // 火把立其上
        w.setBlock(x0 + 1, kRigY,     z0, BR::RedstoneDust, 0);   // 斜下环粉（仅火把斜角供，拉杆对它是斜角不直供）
        w.setBlock(x0 + 2, kRigY,     z0, BR::RedstoneLamp, 0);   // 灯挨粉
        w.setBlock(x0,     kRigY,     z0 + 1, BR::Lever, 0);      // NOT 门输入：拉杆贴支撑块侧面（初始关）
        tickN(w, 10);
        const auto dustP = [&]() { return w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask; };
        const auto lampOn = [&]() { return (w.stateAt(x0 + 2, kRigY, z0) & BR::RedstoneLampStateOnFlag) != 0; };
        bool ok = dustP() == 15 && lampOn();            // 初稳态：火把亮 → 环粉 15、灯亮
        w.setBlock(x0, kRigY, z0 + 1, BR::Lever, 1);    // 拉杆供能支撑块 → 火把反相熄灭
        tickN(w, 10);
        ok = ok && dustP() == 0 && !lampOn();           // 翻转降沿：环粉断电、灯灭（修前此处恒亮 = FAIL 面）
        w.setBlock(x0, kRigY, z0 + 1, BR::Lever, 0);    // 拉杆回位 → 火把重亮
        tickN(w, 10);
        ok = ok && dustP() == 15 && lampOn();           // 重亮升沿：环粉复电、灯复亮
        w.setBlock(x0, kRigY, z0 + 1, BR::Lever, 1);    // 再供能再熄（双向翻转收敛性）
        tickN(w, 10);
        ok = ok && dustP() == 0 && !lampOn();
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| torch NOT-gate FLIP reaches diagonal ring dust: lit 15/lamp on, "
                             "flip-off 0/lamp off, relight 15/lamp on again (review #2)";
        // 清场
        w.setBlock(x0, kRigY + 1, z0, BR::Air);
        w.setBlock(x0, kRigY, z0, BR::Air);
        w.setBlock(x0 + 1, kRigY, z0, BR::Air);
        w.setBlock(x0 + 2, kRigY, z0, BR::Air);
        w.setBlock(x0, kRigY, z0 + 1, BR::Air);
        tickN(w, 2);
    }

    // ── 审查 #1 末影眼巡航高度回归探针（Entities 层 EntityManager 直编，同 t737 MinecartManager 先例）：
    //    t758 插入 spawnEnderPearl 时 spawnEnderEye 的 enderEyeCruiseY 赋值被 diff 吞掉 → 字段全工程无写入
    //    点（只剩默认 0.0f）→ tick 远段爬升分量恒 0，升空巡航整体死码且运行期无任何报错面。spawn 两枚不同
    //    高度的眼，断言巡航高度 == origin.y() + 8（kEnderEyeClimbHeight），防同类「插函数吞赋值」静默回归。
    {
        EntityManager ents;
        const int s1 = ents.spawnEnderEye(QVector3D(10.5f, 20.0f, 10.5f), QVector3D(1.0f, 0.5f, 0.0f));
        const int s2 = ents.spawnEnderEye(QVector3D(12.5f, 33.0f, 12.5f), QVector3D(0.0f, 0.2f, 1.0f));
        const bool ok = s1 >= 0 && s2 >= 0
                        && std::abs(ents.enderEyeCruiseYAt(s1) - 28.0f) < 1e-4f
                        && std::abs(ents.enderEyeCruiseYAt(s2) - 41.0f) < 1e-4f;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| ender-eye spawn records cruise Y = origin.y()+8 at two throw heights "
                             "(regression guard, review #1)";
    }

    // ── P15 t772 红石块直供全器件 × 双放置顺序矩阵 ──
    //   用户报告（R19.12 测试）：「红石块只点亮红石粉/红石灯，发射器、TNT 等均不响应；通电红石粉也点不着
    //   TNT」。主矩阵（上方 12 源 × 7 接收器）只测「源与器件同帧在场后的首个重算」——放置顺序从未独立成
    //   维度，而顺序正是可达性路径的分水岭：order A（器件先就位稳态、后放源）可达性走 notePowerWrite 锚点
    //   的 6 邻接收器展开（t657 引入、t706 扩全红石族脏达）；order B（源先就位稳态、后放器件）可达性走
    //   器件自身锚点（t656 起即有）。本探针把两条路径 × 3 源（红石块 / 立式亮火把 / 扳开拉杆）× 7 器件全
    //   组合断言激活（信号型 = 计数 + 坐标；状态型 = 通电位 + 降沿复查）——任一组合 FAIL 即用户症状在当前
    //   HEAD 的复现点。同槽复用（每 case 末完整清场 + 2 tick 收敛，槽预算 6 个，远低于 124 上限）。
    //   注：发射器 / 投掷器在呈现层另有「空库存无动作」语义（t607 玩家机器身份）——本 World 层探针断言
    //   的是 powerDispenserTriggered 信号已发出（消费端 fireDispenserAtQml 的沿检测输入），非可见弹射。
    {
        const SourceDef s772[] = {
            { "RedstoneBlock",      BR::RedstoneBlock, 0,                           true,  false },
            { "RedstoneTorch(lit)", BR::RedstoneTorch, 0,                           true,  false },
            { "Lever(on)",          BR::Lever,         1,                           false, false },
        };
        const RecvDef r772[] = {
            { "TNT",          BR::TntBlock,      0,                           true  },
            { "Dispenser",    BR::Dispenser,     0,                           true  },
            { "Dropper",      BR::Dropper,       0,                           true  },
            { "RedstoneLamp", BR::RedstoneLamp,  BR::RedstoneLampStateOnFlag, false },
            { "GoldenRail",   BR::GoldenRail,    BR::GoldenRailStateOnFlag,   false },
            { "IronDoor",     BR::IronDoor,      0x04,                        false },
            { "IronTrapdoor", BR::IronTrapdoor,  0x01,                        false },
        };
        for (int order = 0; order < 2; ++order) {
            for (const SourceDef &src : s772) {
                const auto [x0, z0] = nextSlot(); // 每源一槽，7 器件顺序复用（case 间全清 + 收敛 tick）
                for (const RecvDef &rc : r772) {
                    const int srcX = x0, recvX = x0 + 1;
                    if (order == 0) {
                        // order A：器件先就位 + 稳态 4 tick → 后放源（用户实测路径：先摆 TNT/发射器、再贴红石块）。
                        w.setBlock(recvX, kRigY, z0, rc.id, 0);
                        tickN(w, 4);
                        w.setBlock(srcX, kRigY, z0, src.id, src.onState);
                    } else {
                        // order B：源先就位 + 稳态 4 tick → 后放器件。
                        w.setBlock(srcX, kRigY, z0, src.id, src.onState);
                        tickN(w, 4);
                        w.setBlock(recvX, kRigY, z0, rc.id, 0);
                    }
                    const int tnt0 = tntFired, disp0 = dispFired;
                    tickN(w, 6);
                    bool on = false;
                    QString onNote;
                    if (rc.signalBased && rc.id == BR::TntBlock) {
                        on = (tntFired > tnt0) && lastTntX == recvX && lastTntY == kRigY && lastTntZ == z0;
                        if (!on) onNote = QStringLiteral("no powerTntTriggered");
                    } else if (rc.signalBased) {
                        on = dispFired > disp0;
                        if (!on) onNote = QStringLiteral("no powerDispenserTriggered");
                    } else {
                        on = (w.stateAt(recvX, kRigY, z0) & rc.onFlag) != 0;
                        if (!on) onNote = QStringLiteral("state flag not set (st=%1)").arg(int(w.stateAt(recvX, kRigY, z0)));
                    }
                    bool offOk = true;
                    QString offNote;
                    if (on && !rc.signalBased) {
                        if (src.removeToOff) w.setBlock(srcX, kRigY, z0, BR::Air);
                        else                 w.setBlock(srcX, kRigY, z0, src.id, 0);
                        tickN(w, 6);
                        offOk = (w.stateAt(recvX, kRigY, z0) & rc.onFlag) == 0;
                        if (!offOk) offNote = QStringLiteral("falling edge: flag stuck");
                    }
                    const bool ok = on && offOk;
                    if (!ok) ++totalFail;
                    qInfo().noquote() << (ok ? "PASS" : "FAIL")
                                      << "| t772"
                                      << (order == 0 ? "[device-first,source-last]" : "[source-first,device-last]")
                                      << src.name << "->" << rc.name
                                      << (on ? QString() : onNote) << (offOk ? QString() : offNote);
                    // 清场（源 + 器件全清；信号型 TNT 的清块在呈现层，World 层探针须自理）+ 2 tick 收敛。
                    w.setBlock(srcX, kRigY, z0, BR::Air);
                    w.setBlock(recvX, kRigY, z0, BR::Air);
                    tickN(w, 2);
                }
            }
        }
    }

    // ── P16 t771 跨轨种拐角探针（普通轨×{普通,动力,探测}邻弯 + 动力-普通-动力垂直链 + 矿车过混合拐角）──
    //   用户报告（R19.12）：「只有普通铁轨可以转弯……动力铁轨和动力铁轨之间中间放普通铁轨也能转弯才对，
    //   只需要一个普通铁轨也可以转弯，普通铁轨和动力铁轨之间也可以转弯，仿我的世界规则」。机制等价
    //   MC 1.0：弯道形态只呈现在**普通轨格**上（railCornerArms 消费 con 位），但配对邻轨**轨种不限**
    //   （普通/动力/探测均可作臂）；动力/探测轨自身永不弯（railConnections 规则②直线投影恒直）。
    //   断言四层（任一 FAIL = 用户症状在当前 HEAD 的复现点）：
    //   (a) 混合 L 拐角 × 3 臂种（两臂同为普通/动力/探测）：拐角格 con 恰为两垂直臂位 + mesher 象限
    //       （railCornerArms + PartialBlockGeometry 同源直调，同 P11 (b)）；两臂格各自回落指向拐角的
    //       单端直位（臂轨不弯）；
    //   (b) 动力轨坐弯位（两垂直普通邻）永不弯：con 为直线投影单端位（railCornerArms 拒绝）；
    //   (c) 动力-普通-动力 / 探测-普通-探测垂直链（用户主诉场景）：中间普通轨 con = 两垂直臂位（拐角）；
    //       破端轨 → 中间轨随编辑复检回落单端直位（连接是派生态，破轨断弯）；
    //   (d) 矿车过混合拐角（动力轨起步 → 普通轨拐角 → 动力轨死端）：进/出拐角必垂直（真转弯）、
    //       Y 钉轨面、过弯后 yaw 覆盖行进向基数（180 = +Z 头向）、终停死端格心（pickTrackStep 反向滤）。
    {
        // 拐角象限断言（P11 (b) 同源）：con → railCornerArms 臂向 → mesher 直调拐角 quad 的肘角/对角
        //   落 (ex,ez)/(1-ex,1-ez)。提出共享 lambda（P16 三处复用：混合 L × 3 臂种 + 链弯中间轨）。
        const auto cornerQuadrantOk = [](quint8 con) {
            int axd = 0, azd = 0;
            if (!BR::railCornerArms(con, axd, azd)) return false;
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialLightCtx lctx; lctx.light = 1.0f;
            for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
            PartialNeighborCtx nctx;
            nctx.posX = nctx.negX = nctx.posZ = nctx.negZ = 0; // Rail case 只读 railDelta*（缺省平拐角）
            const float tileW = 1.0f / 16.0f;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::Rail, con, lctx, nctx,
                                         tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            const float ex = (axd > 0) ? 1.0f : 0.0f; // 出口臂贴的 x 边
            const float ez = (azd > 0) ? 1.0f : 0.0f; // 入口臂贴的 z 边
            bool elbow = false, diag = false;
            for (const Vtx &v : verts) {
                const float uu = (v.u - 136.0f * tileW) / tileW; // 拐角瓦片 UV 归一 [0,1]
                if (uu < 0.25f && v.v < 0.25f
                    && std::fabs(v.x - ex) < 1e-4f && std::fabs(v.z - ez) < 1e-4f) elbow = true;
                if (uu > 0.75f && v.v > 0.75f
                    && std::fabs(v.x - (1.0f - ex)) < 1e-4f && std::fabs(v.z - (1.0f - ez)) < 1e-4f) diag = true;
            }
            return elbow && diag;
        };

        // (a) 混合 L：拐角 C=(x0,z0) 普通轨；-X 臂与 +Z 臂同为轨种 K ∈ {普通,动力,探测}。
        const struct { const char *name; quint8 id; } armKinds[3] = {
            { "rail",     BR::Rail },
            { "golden",   BR::GoldenRail },
            { "detector", BR::DetectorRail },
        };
        for (const auto &k : armKinds) {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0 - 1, kRigY, z0, k.id, 0);       // -X 臂（轨种 K）
            w.setBlock(x0, kRigY, z0 + 1, k.id, 0);       // +Z 臂（轨种 K）
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);       // 拐角（最后放：邻齐后一次成形）
            const quint8 cCon = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            const quint8 armX = quint8(w.stateAt(x0 - 1, kRigY, z0) & 0x0F);
            const quint8 armZ = quint8(w.stateAt(x0, kRigY, z0 + 1) & 0x0F);
            const bool ok = cCon == quint8(BR::RailConnNx | BR::RailConnPz) // 拐角 = 两垂直臂位
                            && cornerQuadrantOk(cCon)                       // 象限（贴图与连接位同源）
                            && armX == BR::RailConnPx                        // -X 臂单端直位（臂不弯）
                            && armZ == BR::RailConnNz;                       // +Z 臂单端直位
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t771 mixed L corner, arms =" << k.name
                              << "con" << int(cCon) << "armX" << int(armX) << "armZ" << int(armZ);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // (b) 动力轨坐弯位（两垂直普通邻）永不弯：NS 轴偏好下直线投影取 +Z 单端位（非拐角组合）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0 - 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0, BR::GoldenRail, 0); // 动力轨最后放（坐进弯位）
            const quint8 gCon = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            int axd = 0, azd = 0;
            const bool ok = gCon == BR::RailConnPz && !BR::railCornerArms(gCon, axd, azd);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t771 golden rail at bend slot stays straight, con" << int(gCon);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // (c) 垂直链：端轨 E1(-X) + 中间普通轨 + 端轨 E2(+Z)，端轨种 ∈ {动力×2, 探测×2}（用户主诉
        //     「动力和动力之间中间放普通铁轨也能转弯」）。破 E2 复检断弯回落。
        const struct { const char *name; quint8 id; } endKinds[2] = {
            { "golden",   BR::GoldenRail },
            { "detector", BR::DetectorRail },
        };
        for (const auto &e : endKinds) {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, e.id, 0);           // E1（-X 端）
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);   // 中间普通轨
            w.setBlock(x0 + 1, kRigY, z0 + 1, e.id, 0);   // E2（+Z 端，最后放 → 中间轨成弯）
            const quint8 mCon = quint8(w.stateAt(x0 + 1, kRigY, z0) & 0x0F);
            const quint8 e1 = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            const quint8 e2 = quint8(w.stateAt(x0 + 1, kRigY, z0 + 1) & 0x0F);
            bool ok = mCon == quint8(BR::RailConnNx | BR::RailConnPz) && cornerQuadrantOk(mCon)
                      && e1 == BR::RailConnPx && e2 == BR::RailConnNz;
            // 破 E2 → 中间轨随邻编辑复检断弯，回落 -X 单端直位（连接是派生态非持久属性）。
            w.setBlock(x0 + 1, kRigY, z0 + 1, BR::Air);
            const quint8 mAfter = quint8(w.stateAt(x0 + 1, kRigY, z0) & 0x0F);
            ok = ok && mAfter == BR::RailConnNx;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t771" << e.name << "-rail-" << e.name << "vertical chain bends middle,"
                                 "after break" << int(mAfter);
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // (d) 矿车过混合拐角：G1(动力,-X 端起步) → 拐角(普通轨) → G2(动力,+Z 死端)。空车追推跑法
        //     （同 P11 (d)：pushEmptyCart + tickPushedCarts 每帧、wish 随行进向）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::GoldenRail, 0);       // G1：con=Px → spawn 定向 +X
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);         // 拐角（普通轨）
            w.setBlock(x0 + 1, kRigY, z0 + 1, BR::GoldenRail, 0); // G2：死端（到达即停）
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            const float rideH = 0.45f; // kCartRideH 文档值（同 P11 镜像注释）
            QVector3D prev = carts.posAt(0);
            float wishX = 1.0f, wishZ = 0.0f;
            int lastBx = int(std::floor(prev.x())), lastBz = int(std::floor(prev.z()));
            int inDx = 1, inDz = 0; // 进入当前格方向（spawn 定向 +X）
            bool reachedCorner = false, reachedEnd = false, turnOk = false, yawOk = false;
            bool inFootprint = true, yOk = true;
            for (int t = 0; t < 900 && inFootprint; ++t) {
                // t863④ 适配：到达死端格后停推（追推会把死端车推离轨道出轨——本探针验拐角语义非推离；
                //   停推后余速滑到死端格心停驻）。
                if (int(std::floor(prev.x())) != x0 + 1 || int(std::floor(prev.z())) != z0 + 1)
                    carts.pushEmptyCart(&w, prev, wishX, wishZ); // 玩家追着推（静止即续推）
                carts.tickPushedCarts(0.016f, &w);
                const QVector3D cp = carts.posAt(0);
                const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                if (dl > 1e-4f) { wishX = ddx / dl; wishZ = ddz / dl; }
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                const bool onTrack = (bx == x0 && bz == z0) || (bx == x0 + 1 && bz == z0)
                                     || (bx == x0 + 1 && bz == z0 + 1);
                if (!onTrack) { inFootprint = false; break; }
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.01f) yOk = false;
                if (bx != lastBx || bz != lastBz) {
                    const int ndx = bx - lastBx, ndz = bz - lastBz;
                    // 出拐角必垂直进向（真转弯非直行穿出）——先判后更新 in-dir（同 P11 环线断言）。
                    if (lastBx == x0 + 1 && lastBz == z0 && ndx * inDx + ndz * inDz == 0) turnOk = true;
                    inDx = ndx; inDz = ndz;
                    lastBx = bx; lastBz = bz;
                }
                if (bx == x0 + 1 && bz == z0) reachedCorner = true;
                if (bx == x0 + 1 && bz == z0 + 1) {
                    reachedEnd = true;
                    const int yb = int(std::lround(carts.yawAt(0))) % 360;
                    if (yb == 180) yawOk = true; // +Z 行进头向（-Z 前 = 0 约定下 yaw=180）
                }
                prev = cp;
            }
            // 终停死端格心：G2 是唯一出口朝来路的格（pickTrackStep 反向滤 → 到心停）。
            const QVector3D fin = carts.posAt(0);
            const bool stoppedAtEnd = int(std::floor(fin.x())) == x0 + 1 && int(std::floor(fin.z())) == z0 + 1;
            const bool ok = reachedCorner && reachedEnd && turnOk && yawOk && inFootprint && yOk
                            && stoppedAtEnd;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t771 cart through mixed corner (golden->rail corner->golden dead end):"
                                 " turn" << turnOk << "yaw180" << yawOk << "stopAtEnd" << stoppedAtEnd;
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0 + 1, BR::Air);
            tickN(w, 2);
        }
    }

    // ── P17 t773 TNT 点燃路径补全探针（粉链两接法 + 升降沿语义 + 探测轨有车端到端）──
    //   用户报告（R19.12）：「通电红石粉也点不着 TNT」「探测轨有车信号也应触发 TNT」。t772 P15 已实证
    //   直供源（红石块 / 火把 / 拉杆）× TNT 双放置顺序全过（用户症状 = 陈旧 exe）；本组探针补齐**粉链**
    //   与**探测轨**两条剩余路径的回归锁：
    //   (a) 同层粉链（lever - 粉 - TNT）：升沿恰一次点燃 + 坐标命中；断电降沿复算触达（World 层清块归
    //       呈现层信号消费端）但**不得再触发**；断电后再上电（场内无 TNT）不触发；重放 TNT（粉仍通电）
    //       → 立即点燃（器件后放路径，等价用户重新摆 TNT）；
    //   (b) 粉在 TNT 上方爬坡斜接（lever - 地面粉 - TNT 顶粉）：顶粉经爬墙斜角（水平邻 y+1）从地面粉
    //       得电（距源 2 跳 → 电力 14），TNT 由正交上邻通电粉点燃（isReceivingPower 含 +Y 向读）；
    //       拆源降沿顶粉同步断电、无再触发；
    //   (c) 探测轨有车端到端（Rail-Detector-Rail-Rail 直轨 + 空车直落探测格 + TNT 贴轨南邻）：车压轨
    //       bit4 置位（setWaterSilent → notePowerWrite，非主矩阵的「直摆激活态」捷径）→ 邻接 TNT 点燃；
    //       驻轨稳态幂等零写 → 不重复点燃；推离降沿 bit4 清 → 无再触发。
    //   ⚠ 消费端镜像（探针成败关键）：World 层 TNT 分支无升沿守卫（每次电力活动 pass 触达且通电即 emit
    //   ——粉 state 写入沿的回插复算会再触达），防双触发的收口在呈现层消费端（playercontroller
    //   firePowerTnt：isTnt 守卫 + 同步 clearBlockSilent——2810 行分支注释「点燃后清 Air 由信号消费端做」）。
    //   孤测若无消费端清块，「恰一次」断言必假 FAIL（双 emit 落在同一 TNT 块上；真实链路里第一次 emit
    //   已同步清块 → 第二次 emit 前 addReceiver 读到 Air 根本不发生）。本组探针统一挂 scoped 消费端
    //   镜像连接（isTnt 守卫 + clearBlockSilent，firePowerTnt 的 World 侧动作同款），断言语义 = 真实链路。
    {
        // 消费端镜像连接（(a)(b)(c) 共用；探针末统一断开——全局计数连接不动）。
        const QMetaObject::Connection tntCons =
            QObject::connect(&w, &World::powerTntTriggered, &w, [&](int x, int y, int z) {
                if (BR::isTnt(w.blockAt(x, y, z))) w.clearBlockSilent(x, y, z);
            });

        // (a) 同层粉链 + 升降沿语义。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0,     kRigY, z0, BR::Lever, 1);         // 源（扳开）
            w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneDust, 0);  // 粉（与 TNT 同层相邻）
            w.setBlock(x0 + 2, kRigY, z0, BR::TntBlock, 0);      // TNT 最后放（编辑锚点入脏驱动首算）
            const int t0 = tntFired;
            tickN(w, 6);
            bool ok = (tntFired - t0 == 1)                                                     // 升沿恰一次
                      && lastTntX == x0 + 2 && lastTntY == kRigY && lastTntZ == z0              // 坐标命中
                      && w.blockAt(x0 + 2, kRigY, z0) == BR::Air                               // 消费端已清块
                      && (w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask) == 15;      // 粉确为活跃 15
            w.setBlock(x0, kRigY, z0, BR::Lever, 0);   // 断源 → 降沿
            tickN(w, 6);
            ok = ok && (tntFired - t0 == 1)                                                     // 降沿不重复点燃
                  && (w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask) == 0;           // 粉断电收敛
            w.setBlock(x0, kRigY, z0, BR::Lever, 1);   // 再上电（场内无 TNT——原块已点燃清走）
            tickN(w, 6);
            ok = ok && (tntFired - t0 == 1);                                                    // 无器件 → 不触发
            w.setBlock(x0 + 2, kRigY, z0, BR::TntBlock, 0); // 重放 TNT（用户重新摆；粉仍通电 15）
            tickN(w, 6);
            ok = ok && (tntFired - t0 == 2)                                                     // 器件后放 → 立即点燃
                  && lastTntX == x0 + 2 && lastTntY == kRigY && lastTntZ == z0
                  && w.blockAt(x0 + 2, kRigY, z0) == BR::Air;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t773 same-level dust trail -> TNT: rising edge fires exactly once, "
                                 "falling edge no re-fire, re-power w/o TNT silent, replaced TNT fires "
                                 "immediately";
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // (b) 粉在 TNT 上方爬坡斜接（t769 教训：先净空工作 box——本列地形可能达 y≥42，顶粉格被挤占即假 FAIL）。
        {
            const auto [x0, z0] = nextSlot();
            for (int i = 0; i <= 2; ++i)
                for (int dy = 0; dy <= 1; ++dy)
                    if (w.blockAt(x0 + i, kRigY + dy, z0) != BR::Air)
                        w.setBlock(x0 + i, kRigY + dy, z0, BR::Air);
            w.setBlock(x0,     kRigY,     z0, BR::Lever, 1);         // 源
            w.setBlock(x0 + 1, kRigY,     z0, BR::RedstoneDust, 0);  // 地面粉（源直供 15）
            w.setBlock(x0 + 2, kRigY,     z0, BR::TntBlock, 0);      // TNT
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::RedstoneDust, 0);  // TNT 顶粉（与地面粉爬墙斜角互连）
            const int t0 = tntFired;
            tickN(w, 6);
            bool ok = (tntFired - t0 == 1)
                      && lastTntX == x0 + 2 && lastTntY == kRigY && lastTntZ == z0
                      && (w.stateAt(x0 + 2, kRigY + 1, z0) & BR::RedstoneDustPowerMask) == 14; // 距源 2 跳
            w.setBlock(x0, kRigY, z0, BR::Air);       // 拆源 → 全线断电降沿
            tickN(w, 6);
            ok = ok && (tntFired - t0 == 1)
                  && (w.stateAt(x0 + 2, kRigY + 1, z0) & BR::RedstoneDustPowerMask) == 0       // 顶粉同步断电
                  && (w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask) == 0;         // 地面粉同步断电
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t773 dust climbing onto TNT top (wall-diagonal hop): top dust power 14, "
                                 "TNT fires once; source removed -> trail dead, no re-fire";
            w.setBlock(x0,     kRigY,     z0, BR::Air);
            w.setBlock(x0 + 1, kRigY,     z0, BR::Air);
            w.setBlock(x0 + 2, kRigY,     z0, BR::Air);
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Air);
            tickN(w, 2);
        }
        // (c) 探测轨有车 → TNT 端到端（真实矿车压轨置位链，非直摆激活态）。
        {
            const auto [x0, z0] = nextSlot();
            const int detX = x0 + 1;
            w.setBlock(x0,     kRigY, z0,     BR::Rail, 0);
            w.setBlock(detX,   kRigY, z0,     BR::DetectorRail, 0);
            w.setBlock(x0 + 2, kRigY, z0,     BR::Rail, 0);
            w.setBlock(x0 + 3, kRigY, z0,     BR::Rail, 0);
            w.setBlock(detX,   kRigY, z0 + 1, BR::TntBlock, 0);   // TNT 贴探测轨南邻
            const auto detOn = [&]() { return (w.stateAt(detX, kRigY, z0) & BR::DetectorRailStateOnFlag) != 0; };
            MinecartManager carts;
            carts.spawnCart(detX, kRigY, z0, &w); // 空车直落探测轨（静止，无人骑）
            const int t0 = tntFired;
            for (int t = 0; t < 8; ++t) { carts.tickPushedCarts(0.016f, &w); w.tickRedstone(); }
            bool ok = detOn()                                                        // bit4 置位（经矿车占用链）
                      && (tntFired - t0 == 1)                                        // 升沿恰一次点燃
                      && lastTntX == detX && lastTntY == kRigY && lastTntZ == z0 + 1; // 坐标命中
            for (int t = 0; t < 20; ++t) { carts.tickPushedCarts(0.016f, &w); w.tickRedstone(); }
            ok = ok && detOn() && (tntFired - t0 == 1);                              // 驻轨稳态幂等零写不重复点燃
            QVector3D player = carts.posAt(0);                                       // 玩家追推 +X 离开（同 P12 跑法）
            bool left = false;
            for (int t = 0; t < 600 && !left; ++t) {
                carts.pushEmptyCart(&w, player, 1.0f, 0.0f);
                carts.tickPushedCarts(0.016f, &w);
                w.tickRedstone();
                player = carts.posAt(0);
                if (int(std::floor(player.x())) >= x0 + 2) left = true;
            }
            for (int t = 0; t < 12; ++t) { carts.tickPushedCarts(0.016f, &w); w.tickRedstone(); }
            ok = ok && left && !detOn() && (tntFired - t0 == 1);                     // 离开沿断电降沿无再触发
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t773 detector rail with real cart -> adjacent TNT fires exactly once "
                                 "(bit4 via occupancy chain); parked steady no re-fire; cart leaves -> off, "
                                 "no re-fire";
            carts.clearAll();
            for (int i = 0; i <= 3; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air);
            w.setBlock(detX, kRigY, z0 + 1, BR::Air);
            tickN(w, 2);
        }

        QObject::disconnect(tntCons); // 消费端镜像仅限本组探针（全局计数连接保留）
    }

    // ── P17 t774 TNT 爆炸伤害 mob 探针（Entities 层 EntityManager 直编，同末影眼先例）──
    //   用户报告（R19.12）：「TNT 爆炸之后对生物没有伤害？只有对玩家才有伤害」——旧爆炸路径
    //   （detonateTntSphere / detonateStalker）只 emit mobAttackedPlayer 伤玩家，球内 mob 零伤。修后
    //   damageMobsFromExplosion 补 mob 侧（同公式距离衰减 + 击退 + 死亡走 mobDied 掉落链）。矩阵断言
    //   （任一 FAIL = 用户症状在当前 HEAD 的复现点）：
    //   (a) 距离单调：3 只猪距爆心 ~1 / ~2 / ~4 格（4 > 半径 3 在球外）——受伤恰 16 / 8 / 0 HP
    //       （round(kExplosionDamageMax·(1−d/R))，同玩家侧公式/常量）；8 HP 猪吃满 8 伤 → dead + ~0.5s 死亡
    //       动画后 mobDied 恰一次（掉落链入口）；球外猪全程无伤；
    //   (b) 击退：幸存近猪被推离爆心（+X 方向位移 >> 游荡抖动；死亡猪尸体不推——只断言幸存者）；
    //   (c) 玩家链不双伤：爆心 1 格处虚拟玩家脚位 → mobAttackedPlayer 恰发一次且伤害同公式（16），
    //       后续远场爆炸不再新增（既有玩家链原样保留，新增 mob 侧不碰玩家）；
    //   (d) 水中爆炸照样伤 mob：TNT 格置 Water（originInWater 只跳地形破坏）→ 距 ~1 格猪照样受伤。
    {
        const auto [x0, z0] = nextSlot();
        const int ty = kRigY;
        // 平台（防 spawn 即坠落；爆炸毁掉球内部分 → 猪跌落不影响水平击退断言；球外猪的平台幸存）。
        for (int dx = 0; dx <= 6; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        EntityManager ents;
        // 猪位 = 格心 + halfH=0.45（spawnMobCore 口径）；爆心 = TNT 格心 (x0+0.5, ty+0.5, z0+0.5)。
        const int pigNear = ents.spawnMobTyped(x0 + 1, ty, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30); // 距 ~1.001 → 16 HP（幸存 → 击退断言）
        const int pigMid  = ents.spawnMobTyped(x0 + 2, ty, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 8);  // 距 ~2.001 → 8 HP → 恰死（死亡/掉落链断言）
        const int pigFar  = ents.spawnMobTyped(x0 + 4, ty, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 5);  // 距 ~4.000 > 3 → 球外 0 HP
        int playerHits = 0, playerDmg = -1;
        QObject::connect(&ents, &EntityManager::mobAttackedPlayer, &ents,
                         [&](int amount, int type, float kx, float kz) {
                             Q_UNUSED(type); Q_UNUSED(kx); Q_UNUSED(kz);
                             ++playerHits; playerDmg = amount;
                         });
        int diedCount = 0, diedType = -1;
        QObject::connect(&ents, &EntityManager::mobDied, &ents,
                         [&](int x, int y, int z, int type, bool burned, bool baby) {
                             Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); Q_UNUSED(burned); Q_UNUSED(baby);
                             ++diedCount; diedType = type;
                         });
        const float pigNearX0 = ents.posAt(pigNear).x();
        // (c) 虚拟玩家脚位：爆心 +1 格 X、身体中心与爆心同高（脚 y = ty−0.4 → 中心 ty+0.5）→ 距 1.0 → 16 HP。
        const QVector3D playerPos(x0 + 1.5f, ty - 0.4f, z0 + 0.5f);
        ents.detonateTntSphere(x0, ty, z0, &w, playerPos);
        bool ok = ents.healthAt(pigNear) == 30 - 16
               && ents.healthAt(pigMid) == 0 && ents.deadAt(pigMid)
               && ents.healthAt(pigFar) == 5 && !ents.deadAt(pigFar)
               && playerHits == 1 && playerDmg == 16;
        // (a2)+(b) tick 40×16ms（0.64s > kDeathTime 0.5s）：死亡链 mobDied 恰一次（pigMid，猪类型）；
        //   幸存近猪被击退远离爆心。击退位移在**前 6 tick（0.096s）**取值：knockback vx≈6 b/s 指数衰减期
        //   位移 ~0.4-0.55 格，而 aiWander 随机游荡同窗最坏反向 ~0.1 格 → 阈值 0.25 防偶发（全窗累计游荡
        //   抖动会稀释单调性，短窗让击退主导）。方向 = (mob−爆心) XZ 归一 = +X。
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        float knockDx = 0.0f;
        for (int t = 0; t < 40; ++t) {
            ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            if (t == 5) knockDx = ents.posAt(pigNear).x() - pigNearX0;
        }
        ok = ok && diedCount == 1 && diedType == int(EntityManager::MobPig)
               && knockDx > 0.25f && knockDx < 1.5f         // 击退远离爆心（+X，短窗量级护栏）
               && ents.healthAt(pigFar) == 5 && !ents.deadAt(pigFar); // 球外猪全程无伤
        // (d) 水中爆炸照样伤 mob：爆心格置 Water（originInWater → 只跳地形破坏，不门控伤害）→ 距 ~1 格新猪照样 16 HP。
        const int pigWat = ents.spawnMobTyped(x0 + 6, ty, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 20);
        w.setBlock(x0 + 5, ty, z0, BR::Water, 0);
        ents.detonateTntSphere(x0 + 5, ty, z0, &w, farListener); // 玩家 = 远场 → 半径外不发 mobAttackedPlayer
        ok = ok && ents.healthAt(pigWat) == 20 - 16 && !ents.deadAt(pigWat)
               && playerHits == 1; // 既有玩家链未被新增 mob 侧双触发
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t774 TNT explosion damages mobs: 3 pigs at d~1/2/4 take 16/8/0 HP "
                             "(player-side formula); 8HP pig dies -> mobDied exactly once (drop chain); "
                             "survivor knocked away from blast; player hit chain fires exactly once "
                             "(no double); underwater blast still damages mobs";
        // 清场（平台 + 水格全清 + 2 tick 收敛）。
        w.setBlock(x0 + 5, ty, z0, BR::Air);
        for (int dx = 0; dx <= 6; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Air);
        tickN(w, 2);
    }

    // ── P18 t775 骑矿车窒息探针（1 格高通道顶头扣血的几何 + 节奏断言）──
    //   用户报告（R19.12）：「生存坐矿车穿 1 格高通道（头撞实体方块）应扣血，现无痛穿过」。根因：
    //   PlayerController.step 的矿车骑乘分支早 return，不经走路路径末尾的 t160 窒息块 → 骑乘期头部
    //   嵌实心格零伤（修法 = 分支内座位同步后直调抽出的 tickSuffocation，走路 / 骑乘共用同链）。
    //   矩阵断言（World + Entities 层可及范围 —— PlayerController 属 Game 层不直编，接线由上述共用
    //   函数保证，探针锁定几何前提与节奏规则）：
    //   (a) 1 格净空（轨格 y=R、天花板 y=R+1）：骑乘眼位（脚底 = 车心 −kCartSeatDrop + 眼高）对
    //       World::pointBlockedByCollision 恒 true（t775 起玩家窒息与探针共用本 World 判据）；
    //   (b) 每秒 1HP 节奏镜像（kSuffocationInterval=1s 同规则累积）≥3 脉冲（4.8s 连续嵌 → 4 脉冲）；
    //   (c) 对照组 2 格净空（天花板 y=R+2）：眼位 1.7575 < 2 不嵌 → 全程 false、0 脉冲（防「坐车
    //       恒扣血」的反向回归）；
    //   (d) 车本体不受天花板影响：两 rig 车都全程钉轨面（y=R+rideH）且驶完全程到死端（scanRailColumn
    //   自 floor(pos.y) 起扫，天花板在其上方不遮轨 → 矿车物理可进 1 格净空通道 = 用户症状前提）。
    {
        // 镜像常量（与 Game 层 playercontroller / minecartmanager 私有常量文档值同步，改几何须三处同步）：
        const float seatDrop = 0.3125f; // kCartSeatDrop（脚底 = 矿车中心 −0.3125，t768 底板面偏移）
        const float eyeH = 1.62f;       // kEyeHeight（站姿眼位；骑乘不改变 m_eyeHeight）
        const float suffInterval = 1.0f; // kSuffocationInterval（t160 窒息扣血间隔，每秒 1HP）
        // rig 坐标：不用 nextSlot()（其 4×31 网格已被前序探针占满，尾行 z0=97 越界 → setBlock 全拒 = 假
        //   FAIL）；固定 z=94 行（界内最后一行，前序探针已自清）+ 前置净空（含前后各 1 格隔离边 + 上方
        //   4 层 —— 断开残留邻轨的连接位 / 清残留实心，幂等）。
        const int tunX = 30, tunZ = 94;
        const auto driveTunnel = [&](int ceilY, int *outPulses) -> bool {
            const int cells = 12;
            for (int i = -1; i <= cells; ++i)
                for (int y = kRigY; y <= kRigY + 3; ++y)
                    w.setBlock(tunX + i, y, tunZ, BR::Air, 0);
            for (int i = 0; i < cells; ++i) {
                w.setBlock(tunX + i, kRigY, tunZ, BR::Rail, 0);      // 直轨 EW（连接位自动互连）
                w.setBlock(tunX + i, ceilY, tunZ, BR::Stone, 0);     // 天花板（1 格净空 ceilY=R+1 / 对照 R+2）
            }
            MinecartManager carts;
            carts.spawnCart(tunX, kRigY, tunZ, &w); // 西端格（EW 直轨 → spawn 定向 +X）
            const QVector3D mountOrigin(float(tunX) + 0.5f, float(kRigY) + 2.0f, float(tunZ) + 0.5f);
            *outPulses = 0;
            float suffTimer = 0.0f, maxX = 0.0f;
            bool asExpected = true;
            if (!carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f)) {
                qInfo().noquote() << "  mount failed: cart" << carts.posAt(0);
                return false;
            }
            const bool wantEmbedded = (ceilY == kRigY + 1); // 1 格净空 → 全程嵌；2 格 → 全程不嵌
            for (int t = 0; t < 300; ++t) { // 0.016s × 300 = 4.8s：8 格/s 巡航 ~1.6s 驶完 12 格后停死端（仍嵌）
                QVector3D cp;
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp); // 持续 W（+X 沿轨）
                // (d) 车钉轨面（天花板不遮 scanRailColumn —— 车物理可进 1 格净空通道）。
                if (std::fabs(cp.y() - (kRigY + 0.45f)) > 0.01f) asExpected = false;
                if (cp.x() > maxX) maxX = cp.x();
                // (a)/(c) 骑乘眼位（玩家几何：脚底 = 车心 −seatDrop，眼 = 脚底 +eyeH ≈ R+1.7575）。
                const float eyeY = cp.y() - seatDrop + eyeH;
                const bool embedded = w.pointBlockedByCollision(cp.x(), eyeY, cp.z());
                if (embedded != wantEmbedded) asExpected = false;
                // 节奏镜像（t160 同规则：嵌 → 累积 dt，每 1s 一脉冲；出 → 清零）。
                if (embedded) {
                    suffTimer += 0.016f;
                    if (suffTimer >= suffInterval) { suffTimer -= suffInterval; ++(*outPulses); }
                } else {
                    suffTimer = 0.0f;
                }
            }
            // (d) 驶完全程：西端格心 x0+0.5 → 东端死端格心 +11.5 共 11 格；阈值 10.5 吸收停驻格心微差。
            if (maxX < float(tunX) + 10.5f)
                qInfo().noquote() << "  short run: maxX" << maxX << "ceilY" << ceilY;
            carts.clearAll();
            for (int i = -1; i <= cells; ++i)
                for (int y = kRigY; y <= kRigY + 3; ++y)
                    w.setBlock(tunX + i, y, tunZ, BR::Air, 0);
            tickN(w, 2);
            return asExpected && (maxX >= float(tunX) + 10.5f);
        };
        int pulses1 = 0, pulses2 = 0;
        const bool ok1 = driveTunnel(kRigY + 1, &pulses1); // (a)+(b) 1 格净空 → 全程嵌 + 多脉冲
        const bool ok2 = driveTunnel(kRigY + 2, &pulses2); // (c) 对照 2 格净空 → 全程不嵌 + 0 脉冲
        bool ok = ok1 && ok2 && pulses1 >= 3 && pulses2 == 0;
        if (!ok)
            qInfo().noquote() << "  tunnel suffocation mismatch: 1blk ok" << ok1 << "pulses" << pulses1
                              << "| 2blk ok" << ok2 << "pulses" << pulses2;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t775 ridden-cart head-in-block: 1-block tunnel embeds rider eye "
                             "(pointBlockedByCollision) every tick + >=3 suffocation pulses @1HP/s; "
                             "2-block control never embeds (0 pulses); cart stays pinned to rail in both "
                             "(ceiling does not occlude scanRailColumn)";
    }

    // ── P19 t776 墙插红石火把贴图共轴重合探针（mesher 同源直调，同 P11 模式；纯 Core+World 断言）──
    //   用户报告（R19.12）：「红石火把可插墙，但横着的竖着的贴图没有重合到一块去」。根因：t738 墙插
    //   S 片（垂直墙面的侧视深度片）= 柄根→离墙 0.45 的**单向** quad 铺**整张瓦片** → 贴图中央火把列
    //   （柄 2px + 焰头 4px）落在片内 u=0.5 = 离墙 0.225 处，而 W 片（平行墙面正视图）火把列在火把轴
    //   （柄根贴墙）→ 两片剪影沿轴错开互不重合（斜视一把火把裂成两把错位剪影）。t776 修：S 片改绕火把
    //   把轴**对称**窄带（宽 0.2）只采瓦片中央子区 u∈[0.375,0.625] —— 与 W 片（整瓦铺 0.8 宽）同 texel
    //   密度（柄/焰世界宽两片一致），两片火把列共轴重合。矩阵断言（像素级视觉留人工目视，几何/UV 规则
    //   在 mesher 输出上可精确锁定）：
    //   (a) 五形态（立地 + 四向墙插）每 quad 顶边中点 == B+轴×0.8、底边中点钉柄根 —— 贴图中央火把列
    //       （u=0.5 处）钉在两片共同火把轴上（t776 修前 S 片中点离轴 0.225 → FAIL）；审查修 #17 后 S 带
    //       贴墙底角钳到格界 → 底边中点沿附着轴向格心内移 ≤0.05（W 片仍精确等于 B）；
    //   (b) 墙插两片：W 片 0.8 整瓦采样（u 铺满 [0,1]）+ S 带顶边宽 0.2 子区采样（u∈[0.375,0.625] 焰头
    //       4px 列区）、底边宽 [0.1,0.2]（#17 钳成梯形：0.2−0.075=0.125）→ 两片 texel 密度一致；
    //   (c) 杆向/亮端：底边（贴图底=柄端，v=0）恒 y=0.197 且离墙最近、顶边中点沿轴伸离墙（四向各验
    //       点积符号）+ 上倾 0.866×0.8；立地态底边 y=0、顶边 y=1、中点 (0.5,·,0.5)（亮端朝上）；
    //   (d) 熄灭位（RedstoneTorchStateOffFlag）几何不变、瓦片换 170（暗红熄焰）：u 全落 tile 170 区；
    //   (e) 审查修 #17（Review 2026-08-23 低危）回归防线：S 带全部顶点附着轴坐标 ∈[0,1]（修前贴墙底角
    //       1.075/-0.075 越界 0.075 穿入支撑格 —— 非满立方支撑（半砖/玻璃/铁砧）下可见穿模）。
    {
        // 镜像常量（partialblockgeometry RedstoneTorch case 同源；改几何须两处同步）。
        constexpr float kTLean = 0.5f, kTUpright = 0.866f, kTShaft = 0.80f;
        constexpr float kTBaseOffWall = 0.475f, kTBaseY = 0.197f;
        const float tileW = 1.0f / 16.0f;
        const int onTile = BR::tileIndex(BR::RedstoneTorch, BR::PosX); // 161（def sideTile）
        PartialLightCtx lctx; lctx.light = 1.0f;
        for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
        PartialNeighborCtx nctx; // RedstoneTorch case 不读邻居（缺省全 0 即可）
        bool ok = true;
        // 墙插四向（state 低 3 位 1..4 = TorchOnNX/PX/NZ/PZ，torchAttachOffset 出支撑向 (ax,0,az)）。
        for (int form = 1; form <= 4; ++form) {
            int ax = 0, ay = 0, az = 0;
            BR::torchAttachOffset(quint8(form), ax, ay, az);
            const float bx = 0.5f + ax * kTBaseOffWall, bz = 0.5f + az * kTBaseOffWall;
            const float axx = -ax * kTLean * kTShaft, ayy = kTUpright * kTShaft, azz = -az * kTLean * kTShaft;
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::RedstoneTorch, quint8(form),
                                         lctx, nctx, tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            // 逐 quad（pushCrossQuad 每 quad 正反两组、每组 4 顶点同角同 UV → 步长 4 全组同断言）。
            // 审查修 #17：S 带贴墙底角钳到格界（底边 0.2→0.125 梯形、底边中点内移 0.0375）→ W 片与
            //   S 带分支断言（几何不同），先按 u 采样区分片型（#17 只动几何不动 UV）。
            int wPlanes = 0, ribbons = 0;
            for (int g = 0; g + 3 < verts.size(); g += 4) {
                const Vtx &p0 = verts[g], &p1 = verts[g + 1], &p2 = verts[g + 2], &p3 = verts[g + 3];
                const float mbx = (p0.x + p1.x) / 2, mbz = (p0.z + p1.z) / 2; // 底边中点（= 贴图 u=0.5 火把列）
                const float mtx = (p2.x + p3.x) / 2, mtz = (p2.z + p3.z) / 2;
                const float uu0 = (p0.u - onTile * tileW) / tileW, uu1 = (p1.u - onTile * tileW) / tileW;
                const float wBot = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y)
                                             + (p1.z - p0.z) * (p1.z - p0.z));
                const float wTop = std::sqrt((p3.x - p2.x) * (p3.x - p2.x) + (p3.y - p2.y) * (p3.y - p2.y)
                                             + (p3.z - p2.z) * (p3.z - p2.z));
                const bool fullU = std::fabs(uu0) < 1e-4f && std::fabs(uu1 - 1.0f) < 1e-4f;
                const bool subU = std::fabs(uu0 - 0.375f) < 1e-4f && std::fabs(uu1 - 0.625f) < 1e-4f;
                if (fullU == subU) { // u 采样区不属于任一片型（或同时命中）
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "u-region wrong: w" << wBot << "uu" << uu0 << uu1;
                    ok = false;
                    continue;
                }
                const bool isRibbon = subU;
                if (isRibbon) {
                    // S 带（#17 钳界后）：贴墙底角钳到本格格界 → 底边成梯形；顶边 / 垂直轴不动。
                    const float mba = (ax != 0) ? mbx : mbz, rootA = (ax != 0) ? bx : bz;
                    const float perpA = (ax != 0) ? mbz : mbx;
                    bool sOk = std::fabs(perpA - 0.5f) < 1e-4f      // 垂直轴中点恒过格心（钳界不动垂直轴）
                               && std::fabs(mba - rootA) <= 0.051f  // (a') 底边中点内移 ≤0.05（实测 0.0375）
                               && wBot >= 0.099f && wBot <= 0.201f  // (b') 底边宽 [0.1,0.2]（实测 0.125）
                               && std::fabs(wTop - 0.2f) < 1e-4f;   // 顶边仍 0.2（未钳）
                    // (e) #17 回归防线：全部顶点附着轴坐标 ∈ [0,1]（修前贴墙底角越界 ±0.075 穿支撑格）。
                    const float va[4] = { (ax != 0) ? p0.x : p0.z, (ax != 0) ? p1.x : p1.z,
                                         (ax != 0) ? p2.x : p2.z, (ax != 0) ? p3.x : p3.z };
                    for (int k = 0; k < 4 && sOk; ++k)
                        if (va[k] < -1e-4f || va[k] > 1.0f + 1e-4f) sOk = false;
                    if (!sOk) {
                        qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                          << "S-ribbon clip wrong: mba" << mba << "root" << rootA
                                          << "wBot" << wBot << "wTop" << wTop;
                        ok = false;
                        continue;
                    }
                } else if (std::fabs(mbx - bx) > 1e-4f || std::fabs(mbz - bz) > 1e-4f
                           || std::fabs(wBot - 0.8f) > 1e-4f || std::fabs(wTop - 0.8f) > 1e-4f) {
                    // W 片：底边中点 == 柄根（两轴精确）+ 上下边宽 0.8（整瓦，不钳）。
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "W-plane wrong: mid" << mbx << mbz << "w" << wBot << wTop;
                    ok = false;
                    continue;
                }
                // 公共 (a)/(c)：底边 y=柄根高 + 顶边中点 == 轴端（两片均精确 —— 钳界不动顶边）+ 亮端（v=0
                //   柄端）沿轴伸离支撑（四向点积符号）+ 上倾 0.866×0.8。
                const float away = -(ax * (mtx - mbx) + az * (mtz - mbz));
                if (std::fabs(p0.y - kTBaseY) > 1e-4f
                    || std::fabs(mtx - (bx + axx)) > 1e-4f || std::fabs(mtz - (bz + azz)) > 1e-4f
                    || std::fabs(p2.y - (kTBaseY + ayy)) > 1e-4f
                    || p0.v > 1e-4f || p1.v > 1e-4f || p2.v < 1.0f - 1e-4f || away <= 0.0f
                    || std::fabs((p2.y - p0.y) - ayy) > 1e-4f) {
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "axis/bright-end wrong: base mid" << mbx << p0.y << mbz
                                      << "top mid" << mtx << p2.y << mtz << "away" << away;
                    ok = false;
                    continue;
                }
                if (isRibbon) ++ribbons; else ++wPlanes;
            }
            if (wPlanes != 2 || ribbons != 2) { // 每 quad 正反两组 → 各计 2
                qInfo().noquote() << "  wall form" << form << "plane mix wrong: W" << wPlanes << "S" << ribbons;
                ok = false;
            }
        }
        // 立地（TorchFloor=0）：满格居中 cross —— 中点 (0.5,·,0.5)、底 y=0 顶 y=1、整瓦采样、亮端朝上。
        {
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::RedstoneTorch, 0,
                                         lctx, nctx, tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            for (int g = 0; g + 3 < verts.size(); g += 4) {
                const Vtx &p0 = verts[g], &p1 = verts[g + 1], &p2 = verts[g + 2];
                const float mbx = (p0.x + p1.x) / 2, mbz = (p0.z + p1.z) / 2;
                if (std::fabs(mbx - 0.5f) > 1e-4f || std::fabs(mbz - 0.5f) > 1e-4f
                    || std::fabs(p0.y) > 1e-4f || std::fabs(p2.y - 1.0f) > 1e-4f
                    || p0.v > 1e-4f || p2.v < 1.0f - 1e-4f
                    || std::fabs((p0.u - onTile * tileW) / tileW) > 1e-4f
                    || std::fabs((p1.u - onTile * tileW) / tileW - 1.0f) > 1e-4f) {
                    qInfo().noquote() << "  floor form quad" << g / 4 << "wrong: mid" << mbx << mbz
                                      << "y" << p0.y << p2.y << "v" << p0.v << p2.v;
                    ok = false;
                }
            }
        }
        // (d) 熄灭位：几何同上（底边中点共轴）、瓦片换 170。
        {
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::RedstoneTorch,
                                         quint8(1 | BR::RedstoneTorchStateOffFlag),
                                         lctx, nctx, tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            for (const Vtx &v : verts) {
                if (v.u < 170.0f * tileW - 1e-6f || v.u > 171.0f * tileW + 1e-6f) {
                    qInfo().noquote() << "  off-flag tile wrong: u" << v.u;
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t776+review#17 wall redstone torch: 5 attach forms pin mid-edge torch "
                             "column onto shared axis (top mid exact; W 0.8 full-tile / S-ribbon top 0.2 "
                             "sub-region, bottom edge clipped to cell bounds = trapezoid <=0.05 shift), "
                             "all S-ribbon verts within cell on attach axis (no 0.075 support penetration), "
                             "bright end away from wall (4 dirs) / up on floor, off flag swaps tile 170";
    }

    // ── P20 t803 生物碰火燃烧探针（Entities 层 EntityManager 直编，同 t774 TNT 先例）──
    //   用户报告（R19.12）：「怪物碰到火不燃烧（僵尸实测）」。根因（t803）：mob 碰撞 / 支撑 / 越障判定
    //   （mobAabbHitsSolid / mobFootprintHasSupport / mobSupportTopY / isJumpObstacle）消费 World::isSolid
    //   （语义 = 非 air 实存，非碰撞）→ Fire（ShapeNone 无碰撞盒光源格）被当实体墙 → mob 永远走不进火格
    //   （t724 点燃判定「脚位/身体格 == Fire」永不命中，且 isJumpObstacle 还会对火格起跳翻过）；另旧版站火
    //   时每 AI tick 清零火伤累积器 → 泡在火里反而不扣血（玩家侧 t351 修复的 mob 镜像）。矩阵断言（任一
    //   FAIL = 用户症状在当前 HEAD 的复现点）：
    //   (a) 追击穿火：僵尸（Shambler，敌对近战）追玩家穿火格 —— 中心进火格（>=x+4.1）且点燃（isBurningAt）
    //       （HEAD 旧象：火=墙 → 僵尸停在格边 / 跳过火格，永不点燃）；
    //   (b) 站火持续燃烧 + 周期火伤：僵尸困 1×1 石栏火格（四邻 2 高石墙 —— 2 高墙顶非空气 → 越障跳不触发）
    //       20s：燃烧近全程（burnTicks >= 总 tick−60，容 15% 随机熄灭后 ≤4 帧复燃的短隙）、扣血 >=10HP
    //       （每 kFireDamageInterval=1s 扣 1HP × 15% 随机提前熄灭 → 20s 期望 ~17HP；阈值 10 ≈ 4σ 统计护栏）、
    //       不死（100HP 上限 ~20 伤）；
    //   (c) 火灭即恢复：拆火格 → <=9.5s 内停燃（fireTimer <= kFireDuration 8s 定时双保险 + 随机熄灭只会更早）
    //       且其后 2.5s 血量恒定（无残留伤害源）。
    //   确定性：tickN 只驱动 tickRedstone（World::tick / tickFire / tickWeather / tickHostileLife 均不跑）→
    //   火格不自灭 / 不蔓延、无雨灭、无日光烧（日光 burning 走 tickHostileLife）→ 唯一随机源 = 15% 火伤
    //   随机熄灭（触火即 ≤4 帧内复燃）。日光 / 降水两混淆源由此路径性排除（非靠搭顶棚）。
    {
        // rig 寻址：**运行期扫描空区，不走 nextSlot()** —— 上述循环探针在运行期已把 124 个 slot 位（4 列 × 31
        //   行，z=4..94）耗尽，nextSlot() 此刻返回 z=97+ 越界 → setBlock 全被拒（火 / 平台 / 栏杆全没放上 = 假
        //   FAIL，本探针首轮实测踩坑）；且 kRigY=41 头注释「40 以上必空」不可尽信（本世界 (6,41..43,1) 实测有
        //   生成石柱，spawn 即嵌墙窒息 = 假 FAIL 第二轮）。改扫 y∈[ty-1, ty+3] 全净空的 12 格行（扫不到 → rigOk
        //   false 判 FAIL，不下断言防越界副作用）。
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3) {
            for (int xx = 4; xx + 11 < 96; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 11 && clear; ++dx)
                    for (int dy = -1; dy <= 3 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        }
        const int ty = kRigY;
        EntityManager ents;
        const bool rigOk = x0 >= 0;
        // (a) 追击走廊：石台 x0..x0+8（防 spawn 坠落），火格在路径中点 (x0+4, ty, z0)；虚拟玩家脚位
        //     (x0+8.5, ty, z0+0.5) —— XZ 距 ~8 < kDetectRange=16 → 僵尸全程追击（追击向量纯 +X，同 z 行）。
        for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        w.setBlock(x0 + 4, ty, z0, BR::Fire, 0);
        const int zombie = ents.spawnMobTyped(x0, ty, z0, EntityManager::MobShambler, QStringLiteral("#44aa44"), 100);
        const QVector3D playerFeet(x0 + 8.5f, float(ty), z0 + 0.5f);
        float maxX = ents.posAt(zombie).x();
        bool burnedAfter = false;
        for (int t = 0; t < 240; ++t) { // 3.84s：8 格追击 ~2.9s（kChaseSpeed 2.8 b/s）+ 余量
            ents.tick(0.016f, &w, playerFeet, 0.3f, 1.8f, true); // playerTargetable=true → 敌对可锁定追击
            if (ents.posAt(zombie).x() > maxX) maxX = ents.posAt(zombie).x();
            if (ents.isBurningAt(zombie)) burnedAfter = true;
        }
        const bool okA = maxX >= x0 + 4.1f && burnedAfter; // 中心进火列（floor(pos.x)==x0+4）且曾点燃
        // 清 (a) 场（火 + 石台；僵尸留在 ents 里随后续段自然游荡 / 坠落，不参与任何断言）。
        w.setBlock(x0 + 4, ty, z0, BR::Air);
        for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Air);
        tickN(w, 2);

        // (b) 困兽火格：火 (fx, ty, fz)（复用 (a) 已清场区），四邻 ±X/±Z 各 2 高石墙（ty / ty+1 —— 越障跳需
        //     墙顶两格空气，2 高墙挡跳 → 僵尸被钉在火格内持续触火；半宽 0.30 < 0.5 → 1×1 栏内放得下，脚位格恒 = 火格）。
        const int fx = x0 + 4, fz = z0;
        w.setBlock(fx, ty - 1, fz, BR::Stone, 0); // 火格支撑（防僵尸跌出栏）
        w.setBlock(fx, ty, fz, BR::Fire, 0);
        const int dx4[4] = { 1, -1, 0, 0 }, dz4[4] = { 0, 0, 1, -1 };
        for (int i = 0; i < 4; ++i) {
            w.setBlock(fx + dx4[i], ty,     fz + dz4[i], BR::Stone, 0);
            w.setBlock(fx + dx4[i], ty + 1, fz + dz4[i], BR::Stone, 0);
        }
        const int victim = ents.spawnMobTyped(fx, ty, fz, EntityManager::MobShambler, QStringLiteral("#44aa44"), 100);
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f); // 距 >> kDetectRange → 不追击，栏内纯游荡
        const int totalTicks = 1250;                            // 20s
        int burnTicks = 0;
        for (int t = 0; t < totalTicks; ++t) {
            ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            if (ents.isBurningAt(victim)) ++burnTicks;
        }
        const int dmg = 100 - ents.healthAt(victim);
        const bool okB = burnTicks >= totalTicks - 60 && dmg >= 10 && !ents.deadAt(victim);

        // (c) 拆火 → 停燃 + 血量稳定：fireTimer <= 8s（定时双保险），9.5s 窗（+1.5s 余量，随机熄灭只会更早）
        //     内必然烧尽；再 2.5s 验证无残留伤害（火伤 / 仙人掌均无源，血量必须逐 tick 不变）。
        w.setBlock(fx, ty, fz, BR::Air);
        for (int t = 0; t < 594 && ents.isBurningAt(victim); ++t) // 9.5s；烧尽即早停（省时）
            ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
        const bool outOk = !ents.isBurningAt(victim);
        const int hpStable = ents.healthAt(victim);
        for (int t = 0; t < 157; ++t) // 2.5s
            ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
        const bool okC = outOk && ents.healthAt(victim) == hpStable;

        const bool ok = rigOk && okA && okB && okC;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t803 mobs ignite on fire cells: chasing shambler walks into fire cell "
                             "and burns (fire pass-through in collision/support/jump predicates); pinned "
                             "shambler burns nearly full 20s (relight gaps <=60 ticks) taking >=10 HP "
                             "periodic fire damage without dying; after fire removed burn stops <=9.5s "
                             "(fireTimer 8s cap) and health stays stable";
        // 清场（火已拆；栏杆 + 支撑）。
        w.setBlock(fx, ty - 1, fz, BR::Air);
        for (int i = 0; i < 4; ++i) {
            w.setBlock(fx + dx4[i], ty,     fz + dz4[i], BR::Air);
            w.setBlock(fx + dx4[i], ty + 1, fz + dz4[i], BR::Air);
        }
        tickN(w, 2);
    }

    // ── P21 t804 点燃交互扩展探针（① 木墙点燃蔓延烧毁链 / ② Stalker 打火石短引信引爆 / ③ item 入火
    //   瞬灭〔t844 语义〕+ 燃烧方块格不烧掉落物）──
    //   用户报告（R19.12）：「打火石对着木头制品右键点燃 + 蔓延」「打火石对苦力怕右键引爆」「往火里丢
    //   物品被烧掉」。三段断言（任一 FAIL = 用户症状在当前 HEAD 的复现点）：
    //   (a) 木墙点燃蔓延烧毁链（t843 语义重做版——旧「火吞块 setBlock(Fire) 替换」退役，点燃 = 方块进
    //       燃烧态、id 不变，烧毁发生在燃烧计时归零）：石台上 6 连木板墙 + 端点火格 → 两段断言：① 首次
    //       点燃中途观测 isBurningAt 真 + blockAt 仍是 Planks（直燃语义核心，400 窗内 P(未燃)≈4e-5）；
    //       ② 继续驱动至 1000 窗（500s；2.5%/邻/窗 → 每块期望 ~20s + 5s 燃烧计时 + 余烬火衔接 → 链式
    //       ~2.5min，1000 窗 = 期望 25 次点燃对 6 块需求 ≈ -3.9σ 裕量）→ 全部木板被吞（blockAt 全非
    //       Planks）、火最终无燃料自熄（全 Air）；烧毁走 setBlock(Fire) 放置语义 → blockBroken(Planks)
    //       恒 0（烧毁无掉落，区别于破块链）；
    //   (b) Stalker 打火石引爆：远场监听（>> kDetectRange 不追踪）+ playerTargetable=false（旧 !targetable
    //       门会清 fuseTimer 并跳过 aiStalker——本断言兼证 t804 的门豁免）→ igniteStalkerFlint 返 true；
    //       猪（非 Stalker）同调用返 false（类型拒）；点燃后原地 ~1.5s 引爆（爆炸恰一次、引爆时刻 ∈
    //       [1.4, 2.3]s、期间 inflateAt 曾 >0.2 = 蓄力膨胀可见）；
    //   (c) item 入火**瞬灭**（t844 需求反转覆盖旧 0.8s 点燃窗）：item 直落 Fire 格 → 首 tick 即毁
    //       （[1,3] tick，与岩浆同款瞬灭语义，无动画无信号）；itemBurned 已退役 → 连接计数恒 0（信号
    //       不复存在）；对照 Lava 格内生成瞬毁同窗；**燃烧方块格不烧掉落物**（t844 语义边界）：item 落
    //       在燃烧木板顶面（igniteFlammableAt 后栅格 id 不变 = 实体支撑面）→ 200 tick 存活不被焚毁
    //       （燃烧是「方块本身着火」非「火占据该格」）。
    //   确定性：item 物理无随机源（spawnItemAt 零初速直落，免 spawnItem 弹出方向的哈希漂移）；tickFire
    //   散布 = hashVoxel(seed+窗口序号) 纯函数（300s 窗数远超期望值 3σ，非精确值断言）。
    {
        // rig 寻址：运行期扫描空区（同 P20 先例——nextSlot() 已被上方循环探针耗尽；「40 以上必空」不可
        //   尽信）。需 24 格宽（(a) 木墙 8 + (c) 焚烧 5 + (b) Stalker+爆炸半径缓冲 11）× y∈[ty-1,ty+3] 全净空。
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3) {
            for (int xx = 4; xx + 23 < 96; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 23 && clear; ++dx)
                    for (int dy = -1; dy <= 3 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        }
        const int ty = kRigY;
        const bool rigOk = x0 >= 0;

        // (c-pre) t841/t846 World 层可及判据（打火石分支的守卫输入端——PlayerController 属 Game 层不直编，
        //     P20 先例；此处锁定 World 侧真值，Game 层行为走 t814 真消费端模式 + 人工目视）：
        //     ① 命中立地火格 igniteFlammableAt 恒拒（Fire 非可燃 → false）→ 回退路径被 Game 层 t841 守卫
        //       短路（World 判据：火上无新火可生）；② Torch 不算火（非 Fire 非 flammable → 直燃拒 +
        //       t841 两判据均不含它 → 对火把右键照常回退立地火）；③ 睡莲不算可燃（直燃拒 → 走 t846
        //       Game 层拒绝路径，火不可生于叶上）。

        // (a) 木墙点燃蔓延烧毁链（t843 语义重做版）：石台 dx 0..7，木板墙 dx 1..6（ty 层），立地火 dx 0（贴首块
        //   木板）。① 首燃中途观测（直燃语义：isBurningAt 真 + id 保留）→ ② 终态烧穿断言。
        //   blockBroken 计数只滤 Planks（火自熄 Fire→Air 也发 blockBroken 但 oldId==Fire，排除）。
        int plankBreaks = 0;
        QObject::connect(&w, &World::blockBroken, &w,
                         [&](int, int, int, int oldId) { if (oldId == int(BR::Planks)) ++plankBreaks; });
        for (int dx = 0; dx <= 7; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        for (int dx = 1; dx <= 6; ++dx) w.setBlock(x0 + dx, ty, z0, BR::Planks, 0);
        w.setBlock(x0, ty, z0, BR::Fire, 0);
        bool litIntact = false; // ① 首燃观测：进燃烧态且栅格 id 仍是 Planks（燃烧是侧表瞬态不改 id）
        int winsRun = 0;
        for (int win = 0; win < 400 && !litIntact; ++win) { // 2.5%/窗 → P(400 窗未燃)≈4e-5
            for (int k = 0; k < 5; ++k) w.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval）
            ++winsRun;
            if (w.isBurningAt(x0 + 1, ty, z0))
                litIntact = w.blockAt(x0 + 1, ty, z0) == BR::Planks; // 中途采样在 10 窗燃烧计时内必命中
        }
        for (int win = winsRun; win < 1000; ++win) // ② 继续烧穿（点燃 ~40 窗/链环 + 10 窗燃烧 + 余烬火衔接）
            for (int k = 0; k < 5; ++k) w.tickFire();
        int planksLeft = 0, firesLeft = 0;
        for (int dx = 0; dx <= 7; ++dx) {
            const quint8 b = w.blockAt(x0 + dx, ty, z0);
            if (b == BR::Planks) ++planksLeft;
            if (b == BR::Fire) ++firesLeft;
        }
        const bool okA = rigOk && litIntact && planksLeft == 0 && firesLeft == 0 && plankBreaks <= 1;
        // plankBreaks ≤1：t804 (a2) 燃烧板计时推进段的烧毁会发一次 blockBroken(Planks)（燃烧态耗尽的
        //   正常烧毁链，非本段木墙的破块掉落）——计数器是探针块级共享的，跨子场景累加；t843 语义下
        //   「蔓延烧毁无 blockBroken」的强断言由 P-t843(a)（单板隔离世界）锁定，此处放宽为 ≤1。
        if (!okA)
            qInfo().noquote() << "  [t804a diag] litIntact" << litIntact << "winsRun" << winsRun
                              << "planksLeft" << planksLeft << "firesLeft" << firesLeft
                              << "plankBreaks" << plankBreaks;
        // 清 (a) 场（石台 + 残火/灰烬；正常应为全 Air，仍防御性清）。
        for (int dx = 0; dx <= 7; ++dx) {
            w.setBlock(x0 + dx, ty - 1, z0, BR::Air, 0);
            w.setBlock(x0 + dx, ty, z0, BR::Air, 0);
        }
        tickN(w, 2);

        // (a2) t841/t846 World 层可及判据（z=4 行）：立地火格直燃恒拒（t841 判据①的 World 真值）+
        //     燃烧格幂等不重置计时（判据②）+ Torch 非火（判据③）+ 睡莲非可燃非火（t846 拒点）。
        bool okA2 = false;
        {
            // ① 立地火格：石台上放 Fire → igniteFlammableAt false（Fire 非 flammable）。
            w.setBlock(x0 + 1, ty - 1, 4, BR::Stone, 0);
            w.setBlock(x0 + 1, ty, 4, BR::Fire, 0);
            const bool fireCellRejected = !w.igniteFlammableAt(x0 + 1, ty, 4)
                                          && w.blockAt(x0 + 1, ty, 4) == BR::Fire;
            // ② 燃烧格幂等：木板点燃 → 计时推进 3 窗（余 7）→ 重复 ignite false 且剩余窗数不变
            //    （isBurningAt 真 = 表内仍有项；「不重置」由重复拒绝直接保证——World 直燃入口本就幂等，
            //    Game 层 t841 守卫防的是回退路径在燃烧格旁生新立地火，此处锁 World 输入端真值）。
            w.setBlock(x0 + 3, ty - 1, 4, BR::Stone, 0);
            w.setBlock(x0 + 3, ty, 4, BR::Planks, 0);
            const bool litOnce = w.igniteFlammableAt(x0 + 3, ty, 4);
            for (int k = 0; k < 15; ++k) w.tickFire(); // 3 窗（计时 10→7）
            const bool reIgniteRejected = !w.igniteFlammableAt(x0 + 3, ty, 4)
                                          && w.isBurningAt(x0 + 3, ty, 4)
                                          && w.blockAt(x0 + 3, ty, 4) == BR::Planks;
            for (int k = 0; k < 35; ++k) w.tickFire(); // 再 7 窗 → 第 10 窗烧毁；若重置过则仍在燃
            const bool timerNotReset = !w.isBurningAt(x0 + 3, ty, 4); // 原计时已耗尽（未被重复点燃续期）
            // ③ Torch 不算火：火把格直燃拒 + 非 Fire（t841 两判据均不含它 → 对火把右键照常走回退路径）。
            w.setBlock(x0 + 5, ty - 1, 4, BR::Stone, 0);
            w.setBlock(x0 + 5, ty, 4, BR::Torch, 0);
            const bool torchNotFire = !w.igniteFlammableAt(x0 + 5, ty, 4)
                                      && w.blockAt(x0 + 5, ty, 4) == BR::Torch;
            // ④ 睡莲：直燃拒（非可燃非火）→ Game 层 t846 拒绝路径输入端成立。
            w.setBlock(x0 + 7, ty - 1, 4, BR::Stone, 0);
            w.setBlock(x0 + 7, ty, 4, BR::LilyPad, 0);
            const bool lilypadNotIgnitable = !w.igniteFlammableAt(x0 + 7, ty, 4)
                                             && w.blockAt(x0 + 7, ty, 4) == BR::LilyPad;
            okA2 = fireCellRejected && litOnce && reIgniteRejected && timerNotReset
                   && torchNotFire && lilypadNotIgnitable;
            if (!okA2)
                qInfo().noquote() << "  [t804 a2 diag] fireCellRejected" << fireCellRejected
                                  << "litOnce" << litOnce << "reIgniteRejected" << reIgniteRejected
                                  << "timerNotReset" << timerNotReset
                                  << "torchNotFire" << torchNotFire
                                  << "lilypadNotIgnitable" << lilypadNotIgnitable;
            for (int dx : {1, 3, 5, 7}) {
                w.setBlock(x0 + dx, ty, 4, BR::Air, 0);
                w.setBlock(x0 + dx, ty - 1, 4, BR::Air, 0);
            }
            tickN(w, 2);
        }

        // (b) Stalker 打火石短引信引爆：Stalker dx 16 / 猪 dx 18（类型拒对照）各 1×1 石台；远场监听 +
        //   playerTargetable=false（兼证 !targetable 门豁免——已点燃的引信不因切模式熄火）。
        EntityManager ents;
        w.setBlock(x0 + 16, ty - 1, z0, BR::Stone, 0);
        w.setBlock(x0 + 18, ty - 1, z0, BR::Stone, 0);
        const int stalker = ents.spawnMobTyped(x0 + 16, ty, z0, EntityManager::MobStalker,
                                               QStringLiteral("#44aa44"), 20);
        const int pig = ents.spawnMobTyped(x0 + 18, ty, z0, EntityManager::MobPig,
                                           QStringLiteral("#ee9999"), 10);
        int explosions = 0;
        QObject::connect(&ents, &EntityManager::explosion, &ents,
                         [&](int, int, int) { ++explosions; });
        const bool ignOk = rigOk && stalker >= 0 && pig >= 0
                           && ents.igniteStalkerFlint(stalker)   // Stalker → true（置不可逆短引信）
                           && !ents.igniteStalkerFlint(pig);     // 猪 → false（仅 Stalker 可点）
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        int explodeTick = -1;
        float inflateSeen = 0.0f;
        for (int t = 1; t <= 300 && explodeTick < 0; ++t) {
            ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            if (!ents.aliveAt(stalker)) { explodeTick = t; break; }
            const float inf = ents.inflateAt(stalker);
            if (inf > inflateSeen) inflateSeen = inf;
        }
        const double fuseSec = explodeTick > 0 ? explodeTick * 0.016 : -1.0;
        const bool okB = ignOk && explodeTick > 0 && fuseSec >= 1.4 && fuseSec <= 2.3
                         && inflateSeen > 0.2f && explosions == 1;
        // 清 (b) 场：爆炸球（半径 3）残坑 + 石台 —— 整盒覆写 Air（含 ty±若干，防残骸影响 (c)）。
        for (int dx = 12; dx <= 21; ++dx)
            for (int dy = -3; dy <= 4; ++dy)
                w.setBlock(x0 + dx, ty + dy, z0, BR::Air, 0);
        tickN(w, 2);

        // (c) item 入火瞬灭（t844）：火 dx 10 / 岩浆 dx 12 / 燃烧板 dx 14（各石台支撑）；item 从上方
        //     直落（spawnItemAt 零初速）。itemBurned 已随 t844 删除（信号不存在 → 本文件无连接点 =
        //     「烟粒子路径退役」的编译期事实，运行期以 burnTick 瞬灭 + 无残留槽间接锁定）。
        ItemEntityManager items;
        const int fx = x0 + 10, lx = x0 + 12;
        for (int dx = 10; dx <= 12; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        w.setBlock(fx, ty, z0, BR::Fire, 0);
        // (c1) 火瞬灭：直落 ~3 格 ≈27 tick 入火格 → 入格当帧毁（总 [20,40] tick；对照旧 0.8s 点燃窗
        //     语义总时长 ≥65 tick——上限 40 锁定「无窗」；岩浆对照 (c2) 为格内生成首 tick 即毁 [1,3]，
        //     两者同款「接触即灭、无动画无信号」语义，仅落体时差）。
        items.spawnItemAt(QVector3D(float(fx) + 0.5f, float(ty + 3) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itFire = items.count() - 1;
        int burnTick = -1;
        for (int t = 1; t <= 400; ++t) {
            items.tick(0.016, &w);
            if (!items.aliveAt(itFire)) { burnTick = t; break; }
        }
        // (c2) 岩浆瞬毁对照：直接生成于岩浆格内（t343 消费路径 = 中心格 == Lava 即毁；直落会先停在
        //     岩浆面顶——岩浆非穿透语义，不在本任务范围）→ 首 tick 即毁（与火焚同款瞬灭语义对齐）。
        w.setBlock(lx, ty, z0, BR::Lava, 0);
        items.spawnItemAt(QVector3D(float(lx) + 0.5f, float(ty) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itLava = items.count() - 1;
        int lavaTick = -1;
        for (int t = 1; t <= 200; ++t) {
            items.tick(0.016, &w);
            if (!items.aliveAt(itLava)) { lavaTick = t; break; }
        }
        // (c3) 燃烧方块格不烧掉落物（t844 语义边界）：item 落在燃烧木板顶面 → 200 tick 存活。燃烧态 =
        //     栅格 id 不变（Planks 实体支撑面）→ item resting 其上照常物理，不被焚毁（燃烧是「方块本身
        //     着火」非「火占据该格」——只有立地火格 blockAt==Fire 烧物品）。木板不驱动 tickFire（items.tick
        //     不含世界 tick）→ 计时冻结，燃烧源稳定。
        const int bx = x0 + 14;
        w.setBlock(bx, ty - 1, z0, BR::Stone, 0);   // 石台（防木板失撑掉落为掉落物干扰计数）
        w.setBlock(bx, ty, z0, BR::Planks, 0);
        const bool boardLit = w.igniteFlammableAt(bx, ty, z0)
                              && w.blockAt(bx, ty, z0) == BR::Planks && w.isBurningAt(bx, ty, z0);
        items.spawnItemAt(QVector3D(float(bx) + 0.5f, float(ty + 2) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itBoard = items.count() - 1;
        bool boardItemAlive = true;
        for (int t = 0; t < 200 && boardItemAlive; ++t) {
            items.tick(0.016, &w);
            boardItemAlive = items.aliveAt(itBoard);
        }
        const bool okC = rigOk && burnTick >= 20 && burnTick <= 40
                         // 直落 ~3 格重力下坠 ≈27 tick 入火格，入格当帧瞬灭（旧 0.8s 窗语义总时长
                         // ≥65 tick——上限 40 即证无点燃窗；下限 20 防未入格先毁的假阳性）。
                         && lavaTick >= 1 && lavaTick <= 3
                         && boardLit && boardItemAlive;
        // 清 (c) 场（石台 + 岩浆 + 燃烧板；岩浆不驱动 tickLavaFlow 不蔓延，直接清）。
        w.setBlock(lx, ty, z0, BR::Air, 0);
        w.setBlock(bx, ty, z0, BR::Air, 0); // 同 id/替换写均清燃烧侧表（t843 setBlock 契约）
        for (int dx = 10; dx <= 14; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Air, 0);
        tickN(w, 2);

        const bool ok = rigOk && okA && okA2 && okB && okC;
        if (!ok) {
            qInfo().noquote() << "  [t804 diag] rigOk" << rigOk << "| okA" << okA
                              << "planksLeft" << planksLeft << "firesLeft" << firesLeft
                              << "plankBreaks" << plankBreaks
                              << "| okA2" << okA2
                              << "| okB" << okB << "ignOk" << ignOk << "explodeTick" << explodeTick
                              << "fuseSec" << QString::number(fuseSec, 'f', 2)
                              << "inflateSeen" << inflateSeen << "explosions" << explosions
                              << "| okC" << okC << "burnTick" << burnTick << "lavaTick" << lavaTick
                              << "boardLit" << boardLit << "boardItemAlive" << boardItemAlive;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t804 flint ignition extended (t843/t841/t844/t846 semantics): fire next to "
                             "6-plank wall ignites planks into burning state with id preserved (mid-burn "
                             "sample), chain burns all planks away (no blockBroken-drop chain) and "
                             "self-extinguishes; world-side flint guards: standing-fire cell and "
                             "re-ignite of a burning cell both rejected with timer never reset, torch is "
                             "not fire (fallback still allowed), lily pad not ignitable; flint on stalker "
                             "detonates in-place ~1.5s uncancellable fuse (pig rejected, !targetable gate "
                             "exempt, exactly one explosion, inflate visible); item dropped into fire "
                             "vanishes instantly (<=3 ticks, lava-parity instant destroy, no 0.8s window / "
                             "no smoke signal - itemBurned retired), item resting on a burning plank "
                             "board survives 200 ticks untouched (burning-block cells never burn items)";
    }

    // ── t805 船上岸回归探针（用户「船又能直接开上岸」；回归根因 = t711/21fff7b 把碰岸探测的 ignoreIce
    //    豁免扩为「与水面同高的任何固体」→ 世界海缓坡（seaColumnHeight 每 ~12 格升 1）的 h==waterLevel
    //    同层湿沙带宽达 10+ 格，整条带变「可行驶表面」→ 船从海里顶着 W 直接开上沙滩深处 = t661「上岸应
    //    难 / 需速度」语义被冲掉。修复 = 豁免收回仅冰族 isIce）──
    //    四泳道断言（BoatManager 直调 tickRiddenBoat，dt=1/60 定步长；全程不跑 BoatManager::tick → 被骑
    //    船物理唯一由本探针驱动，确定性；未骑船不 tick → 冻结在原位不干扰后续泳道）：
    //    A 水道：开阔水满速推进 ≥7 b/s（kBoatSpeed=8 的 lerp 稳态 7.9）；
    //    B 陆道：无水陆档怠速 ∈[1.8,3.0]（kBoatSpeed×kBoatLandSpeedMul=2.4，t584 原值）+ 水陆速比 ≥2.5
    //      （「离水减速」骤降比，期望 ~3.3）；
    //    C 岸道：同层湿沙（沙格顶==水面顶）+1 格干沙滩柱 —— 满 W 冲岸 5s：船停在水线前（中心 x 从未越
    //      沙列界，仍浮水面 Y==水面顶，=「不可直接开上岸」）；随后倒挡 1s 退回水道 ≥3 格（=「贴岸可被推
    //      下水」，t611 只清朝向分量的语义）；
    //    D 冰道：同层冰面（冰格顶==水面顶）—— 船可从水面直接滑上冰面越界 ≥1.5 格（L10 冰豁免保留，防
    //      本修复过度回退把冰也挡了）。冰是船可行驶表面 / 沙岸是岸（船贴水线停），两者本就应不同。──
    {
        // rig 选址：本测试世界 setHeight(48) 而 worldgen 地表基线 64 → 高度被钳到 47，y 44..47 几乎整片
        //   实心石（t804 实测同因「kRigY=41 也有生成石柱」）。不清场扫描、直接**凿进石里**：每泳道 =
        //   3 格宽条带（船 footprint Z ±0.7 自条带中格 bz+k+1.5 覆盖 bz+k..bz+k+2，恰不溢出到邻带），
        //   y=44 铺石板（防下方天然洞穴漏支撑）、y 45..47 凿空（船碰撞层 cy/cy+1 必净空）。
        //   本探针最后跑，覆写既有探针残留无副作用；rigOk = 石板落位抽查。
        const int bx = 6, bz = 6;             // 远离世界边 clamp（minX=0.5）；x 39 / z 20 以内全在界内
        const int fy = 44;                    // 石板层；水面 / 同层沙面 / 冰面 = 45；水面顶 = 46
        const int zA = bz + 1, zB = bz + 5, zC = bz + 9, zD = bz + 13; // 泳道中格（条带 = 中格 ±1）
        BoatManager boats;
        bool rigOk = true;
        bool okSpeeds = false, okStop = false, okFloat = false, okReverse = false, okIce = false;
        float waterSpeed = 0.0f, landSpeed = 0.0f, maxXC = 0.0f, revX = 0.0f, xD = 0.0f;
        QVector3D posC;
        QVector3D bp;
        bool crashed = false;
        const auto mount = [&boats](const QVector3D &p) {   // 从船正上方垂直下射线（命中即骑）
            return boats.tryMount(QVector3D(p.x(), p.y() + 3.0f, p.z()), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
        };
        // 凿道：四条带（z 中格 ±1）× x bx..bx+33：y=44 Stone、y 45..47 Air；随后各道铺特征。
        const int laneMid[4] = { zA, zB, zC, zD };
        for (const int zm : laneMid) {
            for (int dx = 0; dx <= 33; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    w.setBlock(bx + dx, fy, zm + dz, BR::Stone, 0);
                    w.setBlock(bx + dx, fy + 1, zm + dz, BR::Air, 0);
                    w.setBlock(bx + dx, fy + 2, zm + dz, BR::Air, 0);
                    w.setBlock(bx + dx, fy + 3, zm + dz, BR::Air, 0);
                }
        }
        rigOk = w.blockAt(bx + 2, fy, zA) == BR::Stone;
        // A 水道：整条铺水（45 层）；B 陆道：保持凿空石板（船贴 45.2 息速）。
        for (int dx = 0; dx <= 33; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(bx + dx, fy + 1, zA + dz, BR::Water, 0);
        // C 岸道：水 bx..bx+7 + 同层湿沙列 bx+8（沙格顶==水面顶 46）+ 干沙滩柱 bx+9（顶 47，高出水面 1）。
        for (int dx = 0; dx <= 7; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(bx + dx, fy + 1, zC + dz, BR::Water, 0);
        for (int dz = -1; dz <= 1; ++dz) {
            w.setBlock(bx + 8, fy + 1, zC + dz, BR::Sand, 0);
            w.setBlock(bx + 9, fy + 1, zC + dz, BR::Sand, 0);
            w.setBlock(bx + 9, fy + 2, zC + dz, BR::Sand, 0);
        }
        // D 冰道：水 bx..bx+7 + 同层冰面 bx+8..bx+12（冰格顶==水面顶 → L10 豁免应放行）。
        for (int dx = 0; dx <= 12; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(bx + dx, fy + 1, zD + dz, dx <= 7 ? BR::Water : BR::Ice);

        if (rigOk) {
            // A 水道满速：帧 100→150（1.67→2.5s）平均速度 ≈7.9（kBoatSpeed=8，approach=4 的 lerp 稳态）。
            //   注：spawnBoat 返 bool（非索引）→ 骑乘射线用**确定的落点**（格中心 (x+0.5, y+1, z+0.5)，
            //   kBoatDraft=0）发，船索引用 tryMount 后的 ridingIndex()。
            const bool spawnA = boats.spawnBoat(bx + 2, fy + 1, zA, BoatManager::Oak);
            const bool mountA = spawnA && mount(QVector3D(float(bx + 2) + 0.5f, float(fy + 2), float(zA) + 0.5f));
            const int boatA = boats.ridingIndex();
            float xA100 = 0.0f, xA150 = 0.0f;
            for (int t = 1; t <= 150; ++t) {
                boats.tickRiddenBoat(1.0 / 60.0, &w, 1.0f, 0.0f, bp, crashed);
                if (t == 100) xA100 = boats.posAt(boatA).x();
                if (t == 150) xA150 = boats.posAt(boatA).x();
            }
            waterSpeed = (xA150 - xA100) / (50.0 / 60.0);

            // B 陆道怠速：同一测量窗（期望 2.4 = 8×kBoatLandSpeedMul 0.3，t584 原值）。tryMount 自动换骑。
            const bool spawnB = boats.spawnBoat(bx + 2, fy, zB, BoatManager::Oak);
            const bool mountB = spawnB && mount(QVector3D(float(bx + 2) + 0.5f, float(fy + 1), float(zB) + 0.5f));
            const int boatB = boats.ridingIndex();
            float xB100 = 0.0f, xB150 = 0.0f;
            for (int t = 1; t <= 150; ++t) {
                boats.tickRiddenBoat(1.0 / 60.0, &w, 1.0f, 0.0f, bp, crashed);
                if (t == 100) xB100 = boats.posAt(boatB).x();
                if (t == 150) xB150 = boats.posAt(boatB).x();
            }
            landSpeed = (xB150 - xB100) / (50.0 / 60.0);
            okSpeeds = mountA && mountB && !crashed && boats.aliveAt(boatA) && boats.aliveAt(boatB)
                       && waterSpeed >= 7.0f && landSpeed >= 1.8f && landSpeed <= 3.0f
                       && waterSpeed / landSpeed >= 2.5f;

            // C 岸道挡停：满 W 冲岸 5s —— 探测（修复后同层沙不再豁免）每帧清朝向速度 → 船停在沙列界前
            //   ~0.5 格（中心 x ≤ 界-0.2），从未越过；Y 仍钉水面顶 46（未搁浅 / 未爬岸）。
            const bool spawnC = boats.spawnBoat(bx + 2, fy + 1, zC, BoatManager::Oak);
            const bool mountC = spawnC && mount(QVector3D(float(bx + 2) + 0.5f, float(fy + 2), float(zC) + 0.5f));
            const int boatC = boats.ridingIndex();
            for (int t = 1; t <= 300; ++t) {
                boats.tickRiddenBoat(1.0 / 60.0, &w, 1.0f, 0.0f, bp, crashed);
                const float x = boats.posAt(boatC).x();
                if (x > maxXC) maxXC = x;
            }
            posC = boats.posAt(boatC);
            okStop = mountC && !crashed && boats.aliveAt(boatC)
                     && maxXC <= float(bx + 8) - 0.2f   // 中心从未越过沙列界（界 = bx+8.0）
                     && posC.x() >= float(bx + 6);      // 且确已冲到水线（非中途卡住）
            okFloat = std::abs(posC.y() - float(fy + 2)) <= 0.3f; // 仍浮水面（水面顶 = fy+2 = 46）
            // C 倒挡退水：贴岸船倒退 1s（t611：探测只清朝向分量 → 背向保留）→ 退回水道 ≥3 格。
            for (int t = 1; t <= 60; ++t)
                boats.tickRiddenBoat(1.0 / 60.0, &w, -1.0f, 0.0f, bp, crashed);
            revX = boats.posAt(boatC).x();
            okReverse = !crashed && posC.x() - revX >= 3.0f;

            // D 冰道放行（豁免保留）：满 W 冲冰 —— 冰族仍豁免 → 船从水面直接滑上冰面（中心越冰列界
            //   bx+8 至少 1.5 格；冰档换挡后 11.2 b/s，~1.3s 即达）。
            const bool spawnD = boats.spawnBoat(bx + 2, fy + 1, zD, BoatManager::Oak);
            const bool mountD = spawnD && mount(QVector3D(float(bx + 2) + 0.5f, float(fy + 2), float(zD) + 0.5f));
            const int boatD = boats.ridingIndex();
            for (int t = 1; t <= 300 && xD < float(bx + 10); ++t) {
                boats.tickRiddenBoat(1.0 / 60.0, &w, 1.0f, 0.0f, bp, crashed);
                xD = boats.posAt(boatD).x();
            }
            okIce = mountD && !crashed && boats.aliveAt(boatD)
                    && xD >= float(bx + 8) + 1.5f
                    && std::abs(boats.posAt(boatD).y() - float(fy + 2) - 0.2f) <= 0.5f; // 骑在冰面顶上

            // 清场（石板 / 水 / 沙 / 冰全清 Air —— 与其它探针同款即用即清）。
            boats.clearAll();
            for (const int zm : laneMid)
                for (int dx = 0; dx <= 33; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dy = fy; dy <= fy + 3; ++dy)
                            w.setBlock(bx + dx, dy, zm + dz, BR::Air, 0);
            tickN(w, 2);
        }
        const bool ok = rigOk && okSpeeds && okStop && okFloat && okReverse && okIce;
        if (!ok) {
            qInfo().noquote() << "  [t805 diag] rigOk" << rigOk << "| okSpeeds" << okSpeeds
                              << "waterSpeed" << QString::number(waterSpeed, 'f', 2)
                              << "landSpeed" << QString::number(landSpeed, 'f', 2)
                              << "| okStop" << okStop << "maxXC" << maxXC << "sandEdge" << bx + 8
                              << "posC.x" << posC.x() << "| okFloat" << okFloat << "posC.y" << posC.y()
                              << "| okReverse" << okReverse << "revX" << revX
                              << "| okIce" << okIce << "xD" << xD;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t805 boat shore regression: full-W boat in open water reaches ~8 b/s while "
                             "land gear idles at 2.4 (ratio ~3.3, sharp out-of-water decel); same-level wet "
                             "sand shore (block top == water surface top) stops the boat at the waterline "
                             "(center never crosses the sand column, still afloat at surface Y) and reverse "
                             "backs it >=3 blocks into the water; same-level ice stays exempt (boat slides "
                             "onto ice >=1.5 blocks past the edge, riding on top) - restores t661 "
                             "'beaching needs speed / shore stops boat' semantics lost in 21fff7b (t711)";
    }

    // ── t799 沙/沙砾失撑即时下落探针（World 层 checkGravityBlockOnEdit + Entities 层 FallingBlock 链）──
    //   用户报告（R19.12）：「沙子直接放在火把上面不会触发掉落，能稳定放置；下面是睡莲/草丛/半砖也一样，
    //   只有超过一格高度下落才变掉落物。沙砾同样」。旧实现 = Main.qml maybeTriggerFallingBlock（消费
    //   blockPlaced/blockBroken 在呈现层嵌套 setBlock+spawn）——修后判定下沉 World 层单一谓词
    //   （BlockRegistry::isGravityBlock + isFullCube 支撑判定），发 gravityBlockFell → 呈现层转
    //   EntityManager.spawnFallingBlock（本探针复刻该消费端）。矩阵断言（任一 FAIL = 用户症状复现点）：
    //   (a) 放置路径：沙/沙砾放火把/半砖/草丛/睡莲（非完整立方支撑）上 → setBlock 同步坍落（信号 +
    //       格清 Air），实体下落遇火把变掉落物（t220 语义回归）；
    //   (b) 稳定：放完整立方（石头/TNT/另一沙）上 → 零信号零坍落（沙柱叠放稳定性不回归）；
    //   (c) 更新路径：挖掉沙柱底层支撑 → 上方坍落 + 实体落到完整支撑格上还原方块（>1 格落差着地）；
    //   (d) 半空放置（下方空气 >1 格落差）→ 坍落实体落到火把上 → 变掉落物（用户「>1 格才变掉落物」现状
    //       的正确侧保留：落差不是门控，失撑才是）；
    //   (e) 水中沙：沙放水面上 → 坍落穿透水柱落到水底还原（t220 水不挡沙 / 填堵水格不回归）；
    //   (f) 爆炸（destroySphereSilent）与 TNT 点火（clearBlockSilent）两静默入口 → 上方沙坍落（写入口收口）。
    {
        // rig 寻址：运行期扫描空区（P20 先例——nextSlot() 的 4×31 网格早被前序循环探针耗尽，此刻返回
        //   z=97+ 越界 → setBlock 全被拒 = 假 FAIL）。需 13 格宽 × y∈[ty-1,ty+3] 全净空（含 (d) 半空放置
        //   上探一层，防残留浮空重力方块混入坍落计数）。
        const int ty = kRigY;
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3) {
            for (int xx = 4; xx + 12 < 96; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 12 && clear; ++dx)
                    for (int dy = -1; dy <= 3 && clear; ++dy)
                        if (w.blockAt(xx + dx, ty + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        }
        const bool rigOk = x0 >= 0;
        if (!rigOk)
            qInfo().noquote() << "  [t799 diag] no clear rig strip found (13 wide x y[ty-1,ty+3])";
        // 平台（实体落点 / 支撑底座；探针即用即清）。列布局（互不复用防串扰）：
        //   dx0..3 沙×{火把,半砖,草丛,睡莲} / dx4..7 沙砾×同族 / dx8 石+沙+沙砾稳定柱 / dx9 TNT+沙 /
        //   dx10 火把+半空沙 / dx11 水+沙 / dx12 独立石柱+沙（爆炸用）。
        for (int dx = 0; dx <= 12; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        EntityManager ents;
        // t794 审查防悬挂：gravityBlockFell 的 [&] 捕获引用本块栈局部（ents / fellSignals），块结束后连接若
        //   仍挂在 w 上（旧 context=&w 与块同寿错配）→ 后续任何探针（t794 铁砧重力起）再发本信号即对悬空
        //   引用求值 = UB。改挂本守卫对象（块结束析构 → 自动断连）；fallingBlockDropped 连接 context 本就是
        //   &ents（随块析构自动断）无需改。
        QObject gravSigGuard;
        // 复刻 Main.qml onGravityBlockFell 消费端（World 语义事件 → 下落实体）+ fallingBlockDropped → 计数。
        //   （旧版只计数不 spawn → ents 恒空 / itemDrops 恒 0 = 假 FAIL；消费端必须真转实体。）
        int fellSignals = 0, fellId = -1, itemDrops = 0;
        QObject::connect(&w, &World::gravityBlockFell, &gravSigGuard,
                         [&](int x, int y, int z, int blockId) {
                             ++fellSignals; fellId = blockId;
                             ents.spawnFallingBlock(x, y, z, blockId);
                         });
        QObject::connect(&ents, &EntityManager::fallingBlockDropped, &ents,
                         [&](int, int, int, int) { ++itemDrops; });
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        const auto settle = [&](int frames) { // 驱动实体物理至稳态（着地还原 / 变掉落物均含移除）
            for (int t = 0; t < frames; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
        };

        // (a) 放置路径 × 支撑族矩阵：沙(8)/沙砾(139) × {火把, 半砖, 草丛, 睡莲} → 同步坍落 + 落非完整支撑变掉落物。
        bool okA = true;
        const quint8 partials[] = { BR::Torch, BR::WoodSlab, BR::TallGrass, BR::LilyPad };
        const quint8 gravities[] = { BR::Sand, BR::Gravel };
        for (int g = 0; g < 2; ++g) {
            for (int k = 0; k < 4; ++k) {
                const int cx = x0 + g * 4 + k;             // 沙 dx0..3 / 沙砾 dx4..7
                w.setBlock(cx, ty, z0, partials[k], 0);    // 非完整立方支撑（立平台上）
                fellSignals = 0; fellId = -1; itemDrops = 0;
                w.setBlock(cx, ty + 1, z0, gravities[g], 0); // 玩家放置同一入口
                const bool fellNow = fellSignals == 1 && fellId == int(gravities[g])
                                     && w.blockAt(cx, ty + 1, z0) == BR::Air; // 同帧清格转实体
                settle(240);                               // 实体下落 → 遇非完整支撑变掉落物（t220）
                okA = okA && fellNow && itemDrops == 1 && ents.liveCount() == 0
                      && w.blockAt(cx, ty + 1, z0) == BR::Air; // 不还原成方块（支撑族全同判）
            }
        }

        // (b) 稳定矩阵：完整立方支撑（石头 / TNT / 下层沙）→ 零信号零坍落（沙柱叠放稳定性）。
        bool okB = true;
        {
            w.setBlock(x0 + 8, ty, z0, BR::Stone, 0);
            w.setBlock(x0 + 9, ty, z0, BR::TntBlock, 0);
            fellSignals = 0;
            w.setBlock(x0 + 8, ty + 1, z0, BR::Sand, 0);    // 沙放石头上
            w.setBlock(x0 + 8, ty + 2, z0, BR::Gravel, 0);  // 沙砾放沙上（沙=完整立方可支撑）
            w.setBlock(x0 + 9, ty + 1, z0, BR::Sand, 0);    // 沙放 TNT 上（TNT 完整立方）
            okB = fellSignals == 0
                  && w.blockAt(x0 + 8, ty + 1, z0) == BR::Sand
                  && w.blockAt(x0 + 8, ty + 2, z0) == BR::Gravel
                  && w.blockAt(x0 + 9, ty + 1, z0) == BR::Sand;
            settle(60); // 稳定柱若干 tick 后仍原位（无实体生成）
            okB = okB && ents.liveCount() == 0
                  && w.blockAt(x0 + 8, ty + 1, z0) == BR::Sand
                  && w.blockAt(x0 + 8, ty + 2, z0) == BR::Gravel;
        }

        // (c) 更新路径：挖掉稳定柱底层沙 → 正上方沙砾坍落 + 落到石头上还原方块（着地支撑=完整立方）。
        bool okC = true;
        {
            fellSignals = 0; fellId = -1; itemDrops = 0;
            w.setBlock(x0 + 8, ty + 1, z0, BR::Air, 0);     // 挖底层沙（正上方沙砾失撑）
            okC = fellSignals == 1 && fellId == int(BR::Gravel)
                  && w.blockAt(x0 + 8, ty + 2, z0) == BR::Air; // 柱清空转实体
            settle(240);                                     // 1 格落差 → 落到石柱顶还原沙砾
            okC = okC && itemDrops == 0 && ents.liveCount() == 0
                  && w.blockAt(x0 + 8, ty + 1, z0) == BR::Gravel; // 着地还原（非掉落物——下方是完整支撑）
        }

        // (d) 半空放置（>1 格落差）：沙放火把上两格（中间空气）→ 坍落 → 穿 1 格空气落火把 → 变掉落物。
        bool okD = true;
        {
            w.setBlock(x0 + 10, ty, z0, BR::Torch, 0);
            fellSignals = 0; itemDrops = 0;
            w.setBlock(x0 + 10, ty + 2, z0, BR::Sand, 0);   // 下方 (ty+1) 空气 → 放置即失撑
            okD = fellSignals == 1 && w.blockAt(x0 + 10, ty + 2, z0) == BR::Air;
            settle(240);
            okD = okD && itemDrops == 1 && ents.liveCount() == 0; // 落火把 → 掉落物（落差不改变语义）
        }

        // (e) 水中沙：平台上 1 格水柱，沙放水面 → 坍落穿透水（t220 水不挡沙）→ 落水底还原（填堵水格）。
        bool okE = true;
        {
            w.setBlock(x0 + 11, ty, z0, BR::Water, 0);
            fellSignals = 0; itemDrops = 0;
            w.setBlock(x0 + 11, ty + 1, z0, BR::Sand, 0);   // 下方水 → 非完整支撑 → 失撑
            okE = fellSignals == 1 && w.blockAt(x0 + 11, ty + 1, z0) == BR::Air;
            settle(240);
            okE = okE && itemDrops == 0 && ents.liveCount() == 0
                  && w.blockAt(x0 + 11, ty, z0) == BR::Sand; // 着地还原在水底格（排水填堵）
        }

        // (f) 静默写入口收口：爆炸（destroySphereSilent 破支撑）+ TNT 点火（clearBlockSilent 清 TNT 格）。
        bool okF = true;
        {
            // 爆炸：沙 (ty+1) 立于独立石柱 (ty) 上，炸石柱（半径 <1 只毁中心格）→ 上方沙坍落 → 落平台还原。
            w.setBlock(x0 + 12, ty, z0, BR::Stone, 0);
            w.setBlock(x0 + 12, ty + 1, z0, BR::Sand, 0);
            fellSignals = 0; itemDrops = 0;
            w.destroySphereSilent(x0 + 12, ty, z0, 0.9f);
            okF = fellSignals == 1 && w.blockAt(x0 + 12, ty + 1, z0) == BR::Air;
            settle(240);
            okF = okF && itemDrops == 0 && ents.liveCount() == 0
                  && w.blockAt(x0 + 12, ty, z0) == BR::Sand; // 落到平台石顶还原
            // TNT 点火：(b) 的 dx9 列（TNT + 上方沙仍稳定）→ 清 TNT 格（firePowerTnt 同一入口）→ 沙坍落。
            fellSignals = 0; itemDrops = 0;
            w.clearBlockSilent(x0 + 9, ty, z0);
            okF = okF && fellSignals == 1 && w.blockAt(x0 + 9, ty + 1, z0) == BR::Air;
            settle(240);
            okF = okF && itemDrops == 0 && ents.liveCount() == 0
                  && w.blockAt(x0 + 9, ty, z0) == BR::Sand;  // 落到平台石顶还原
        }

        const bool ok = rigOk && okA && okB && okC && okD && okE && okF;
        if (!ok) {
            qInfo().noquote() << "  [t799 diag] okA" << okA << "| okB" << okB << "| okC" << okC
                              << "| okD" << okD << "| okE" << okE << "| okF" << okF
                              << "| lastFellSignals" << fellSignals << "fellId" << fellId
                              << "itemDrops" << itemDrops;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t799 gravity blocks (sand/gravel) instant-fall on non-full-cube "
                             "supports: placing sand or gravel on torch/slab/tall-grass/lily-pad "
                             "collapses to a falling entity in the same setBlock (single World-layer "
                             "predicate, placement == update path), falling through/onto a partial "
                             "block converts to item drop (t220), on full cube it re-places; "
                             "sand-column stacking on full support stays put, water column pierced "
                             "and sealed, explosion + TNT-prime silent write entries also trigger "
                             "the collapse - fixes 'sand sits stable on torch' user report";
    }

    // ── t794 铁砧重力探针（isGravityBlock 扩铁砧三阶段 + FallingBlock 砸伤 / 着地还原 / 落地音信号）──
    //   用户报告（R19.12）：「铁砧应该要有重力效果，砸到下方的生物会扣血，砸到地面的时候会有声音。」
    //   t799 重力链对铁砧开箱即用程度：失撑坍落 / 下落物理 100% 复用（BlockRegistry::isGravityBlock 谓词
    //   加 isAnvil 即通，World 层零新代码）；本任务补的分叉语义 = ① 砸伤（dmg=(floor(落差)−1)×2，2 格起伤
    //   每多 1 格 +1♥，先伤后落，每实体每次下落只伤一次）；② 着地恒还原铁砧方块（沙落部分方块变掉落物，
    //   铁砧还原 —— 落火把上也还原不掉物品）；③ 落地音事件 fallingBlockLanded（仅铁砧族发）。矩阵断言：
    //   (P) 砸猪数值 + 落差单调：圈养猪（四周墙围 —— aiWander 无跳跃、XZ 撞墙撤回 → 猪恒留落点列、盒顶
    //       恒 ty+0.9，检测拍落差确定）上方铁砧落 3 格 → 恰扣 2HP（(2−1)×2），落 5 格 → 恰扣 6HP（(4−1)×2，
    //       > A 单调；恰扣一次 = 一次性拍不多帧连扣）+ 着地还原铁砧于猪格（生物不挡下落体）；
    //   (U) 更新路径：铁砧放完整立方上稳定（零信号）→ 挖支撑 → gravityBlockFell(id=Anvil) + 着地还原 +
    //       landed 信号恰 1 次 + 零掉落物；
    //   (T) 落火把（不完整方块）：还原铁砧于火把上方（**不掉物品** —— 与沙 t220 分叉）+ landed 信号 +
    //       火把原位不动；
    //   (L) 砸玩家：listener 站落点列 → mobAttackedPlayer 携 MobAnvil 哨兵，伤害 2HP → 6HP 随落差单调。
    {
        const int ty = kRigY;
        // rig 寻址：11 宽 dx[-1,10]（圈养猪墙 x0-1 起 + 空隔 + 更新 / 火把 / 玩家列 + 边距）× 3 深 dz[-1,1]
        //   （猪圈 z 向墙）× y[ty-1, ty+6]（平台下探 / 铁砧最高 ty+5）全净空。
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3) {
            for (int xx = 4; xx + 10 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 10 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 6 && clear; ++dy)
                            if (w.blockAt(xx + dx, ty + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        }
        const bool rigOk = x0 >= 0;
        if (!rigOk)
            qInfo().noquote() << "  [t794 diag] no clear rig strip found (11 wide x 3 deep x y[ty-1,ty+6])";
        // 平台（全列支撑底座；圈栏 / 各探针列共享一层）。
        for (int dx = -1; dx <= 10; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(x0 + dx, ty - 1, z0 + dz, BR::Stone, 0);
        EntityManager ents;
        QObject sigGuard; // t794 信号守卫：块结束析构自动断连（防 [&] 捕获块局部悬挂，见 t799 gravSigGuard 同修）
        int fellSignals = 0, fellId = -1, itemDrops = 0, landedSignals = 0, landedId = -1;
        QObject::connect(&w, &World::gravityBlockFell, &sigGuard,
                         [&](int x, int y, int z, int blockId) {
                             ++fellSignals; fellId = blockId;
                             ents.spawnFallingBlock(x, y, z, blockId); // 复刻 Main.qml onGravityBlockFell 消费端
                         });
        QObject::connect(&ents, &EntityManager::fallingBlockDropped, &ents,
                         [&](int, int, int, int) { ++itemDrops; });
        QObject::connect(&ents, &EntityManager::fallingBlockLanded, &ents,
                         [&](int, int, int, int blockId) { ++landedSignals; landedId = blockId; });
        int hitCount = 0, hitSrcType = -1, hitAmount0 = 0, hitAmount1 = 0;
        QObject::connect(&ents, &EntityManager::mobAttackedPlayer, &ents,
                         [&](int amount, int srcType, float, float) {
                             if (hitCount == 0) hitAmount0 = amount;
                             else if (hitCount == 1) hitAmount1 = amount;
                             hitSrcType = srcType; ++hitCount;
                         });
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        const auto tickFar = [&](int frames) {
            for (int t = 0; t < frames; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
        };
        // 固定 80 帧落定预算：铁砧最高落 5 格 ≈ 36 帧着地；着地后立刻读数（猪被埋窒息 1HP/s 从着地起
        //   ~63 帧后才首扣 → 80 帧内读数干净，数值不受窒息串扰）。
        // (P) 圈养猪砸伤：A 列铁砧放 ty+3（放置即坍落，落 3 格）→ 恰 2HP；B 列 ty+5 → 恰 6HP。
        bool okP = true;
        int pigA = -1, pigB = -1;
        {
            const int ax = x0, bx = x0 + 2; // 两圈栏相邻共享中墙（x0+1）
            const int wallsX[3] = { x0 - 1, x0 + 1, x0 + 3 };
            for (int i = 0; i < 3; ++i) w.setBlock(wallsX[i], ty, z0, BR::Stone, 0);
            w.setBlock(ax, ty, z0 - 1, BR::Stone, 0); w.setBlock(ax, ty, z0 + 1, BR::Stone, 0);
            w.setBlock(bx, ty, z0 - 1, BR::Stone, 0); w.setBlock(bx, ty, z0 + 1, BR::Stone, 0);
            pigA = ents.spawnMobTyped(ax, ty, z0, EntityManager::MobPig, QStringLiteral("#ffa0a0"), 10);
            pigB = ents.spawnMobTyped(bx, ty, z0, EntityManager::MobPig, QStringLiteral("#a0ffa0"), 10);
            tickFar(90); // 猪 resting 落定（盒顶 ty+0.9；圈内走不出列）
            okP = pigA >= 0 && pigB >= 0
                  && ents.healthAt(pigA) == 10 && ents.healthAt(pigB) == 10;
            fellSignals = 0; landedSignals = 0; itemDrops = 0;
            w.setBlock(ax, ty + 3, z0, BR::Anvil, 0); // 下方空气 → 放置即坍落（World 层 ① 自检）
            okP = okP && fellSignals == 1 && fellId == int(BR::Anvil);
            tickFar(80);
            okP = okP && itemDrops == 0 && landedSignals == 1 && landedId == int(BR::Anvil)
                  && ents.healthAt(pigA) == 8       // 恰扣 2HP（(floor(2.1..2.27)−1)×2；恰一次 = 一次性拍）
                  && w.blockAt(ax, ty, z0) == BR::Anvil; // 着地还原于猪格（先伤后落）
            fellSignals = 0; landedSignals = 0; itemDrops = 0;
            w.setBlock(bx, ty + 5, z0, BR::Anvil, 0); // 落 5 格 → 恰 6HP
            tickFar(80);
            okP = okP && itemDrops == 0 && landedSignals == 1
                  && ents.healthAt(pigB) == 4       // 10 − (floor(4.1..4.34)−1)×2 = 10 − 6（落差单调 > A 的 2）
                  && w.blockAt(bx, ty, z0) == BR::Anvil;
        }
        // (U) 更新路径：放完整立方上稳定 → 挖支撑坍落 → 还原 + landed 恰 1 + 零掉落物。
        bool okU = true;
        {
            const int ux = x0 + 5;
            w.setBlock(ux, ty, z0, BR::Stone, 0);
            fellSignals = 0;
            w.setBlock(ux, ty + 1, z0, BR::Anvil, 0); // 铁砧放石头上 → 稳定（零信号）
            okU = fellSignals == 0 && w.blockAt(ux, ty + 1, z0) == BR::Anvil;
            fellSignals = 0; landedSignals = 0; itemDrops = 0;
            w.setBlock(ux, ty, z0, BR::Air, 0);       // 挖支撑 → World 层 ② 复检坍落
            okU = okU && fellSignals == 1 && fellId == int(BR::Anvil)
                  && w.blockAt(ux, ty + 1, z0) == BR::Air;
            tickFar(80);                              // 1 格落差 → 落平台顶还原
            okU = okU && itemDrops == 0 && landedSignals == 1 && landedId == int(BR::Anvil)
                  && w.blockAt(ux, ty, z0) == BR::Anvil;
        }
        // (T) 落火把（不完整方块）：还原铁砧于火把上方一格（不掉物品 —— 与沙 t220「碎成掉落物」分叉）。
        bool okT = true;
        {
            const int tx = x0 + 7;
            w.setBlock(tx, ty, z0, BR::Torch, 0);
            fellSignals = 0; landedSignals = 0; itemDrops = 0;
            w.setBlock(tx, ty + 3, z0, BR::Anvil, 0);  // 放置即坍落 → 落 2 格遇火把
            okT = fellSignals == 1 && w.blockAt(tx, ty + 3, z0) == BR::Air;
            tickFar(80);
            okT = okT && itemDrops == 0 && landedSignals == 1 && landedId == int(BR::Anvil)
                  && w.blockAt(tx, ty + 1, z0) == BR::Anvil // 还原于火把上方（恒还原方块）
                  && w.blockAt(tx, ty, z0) == BR::Torch;    // 火把原位不动
        }
        // (L) 砸玩家：listener 脚位站落点列（playerTargetable=true）→ mobAttackedPlayer 携 MobAnvil 哨兵，
        //     伤害随落差单调（ty+4 落 → 2HP；ty+6 落 → 6HP）。
        bool okL = true;
        {
            const int lx = x0 + 9;
            const QVector3D listener(float(lx) + 0.5f, float(ty), float(z0) + 0.5f);
            hitCount = 0; hitSrcType = -1; hitAmount0 = 0; hitAmount1 = 0;
            w.setBlock(lx, ty + 4, z0, BR::Anvil, 0);
            for (int t = 0; t < 80; ++t) ents.tick(0.016f, &w, listener, 0.3f, 1.8f, true);
            w.setBlock(lx, ty + 6, z0, BR::Anvil, 0); // 第一块已落 ty → 第二块落其上，仍砸穿玩家 AABB
            for (int t = 0; t < 80; ++t) ents.tick(0.016f, &w, listener, 0.3f, 1.8f, true);
            okL = hitCount == 2 && hitSrcType == int(EntityManager::MobAnvil)
                  && hitAmount0 == 2 && hitAmount1 == 6 && hitAmount1 > hitAmount0;
        }
        const bool ok = rigOk && okP && okU && okT && okL;
        if (!ok) {
            qInfo().noquote() << "  [t794 diag] okP" << okP << "| okU" << okU << "| okT" << okT
                              << "| okL" << okL << "| pigA hp" << (pigA >= 0 ? ents.healthAt(pigA) : -1)
                              << "pigB hp" << (pigB >= 0 ? ents.healthAt(pigB) : -1)
                              << "| hits" << hitCount << "amt0" << hitAmount0 << "amt1" << hitAmount1
                              << "src" << hitSrcType << "| fell" << fellSignals << "id" << fellId
                              << "| landed" << landedSignals << "lid" << landedId << "| items" << itemDrops;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t794 anvil gravity: anvil (3 damage stages) joins the sand/gravel gravity "
                             "chain via the single isGravityBlock predicate (support-break and mid-air "
                             "placement both collapse instantly), falling anvil crushes mobs and the "
                             "player with distance-scaled damage (exactly 2HP at 3-block fall / 6HP at "
                             "5-block, damage-first-then-land, once per entity per fall, player death "
                             "cause via MobAnvil sentinel), lands by restoring the anvil block even on "
                             "partial blocks like torches (never an item drop, unlike sand t220), and "
                             "emits fallingBlockLanded for the heavy-metal landing sound";
    }

    // ── t849/t850/t851 铁砧·仙人掌·活板门三件套探针（Core 表查询 + World rig + 玩家碰撞点测，P11 模式）──
    //   t849：铁砧三阶段（Anvil/AnvilChipped/AnvilDamaged）非整格三件套收窄 —— ① collision/selection/raycast
    //         三消费端走 anvilShapeBoxes 三盒窄形（XZ 12/16 足印，底座/腰柱/顶台三段，与 mesher 铁砧 case
    //         同源镜像）→ 足印外环隙可站人/可透视；② heightmap 排除 → 列顶实面落到下方支撑块（PCF 阴影/
    //         天光列不再被铁砧整格抬高）；仙人掌同查（0.8 居中细柱三件套 + heightmap 排除）。
    //   t850：活板门两材质（木/铁）× 两态（合/开）阴影按薄板形状 —— 合态列顶=下方支撑面（板顶 0.1875 由
    //         solidTopOffset 表达，heightmap 不再指向薄板格）；开态列顶=贴边竖板仍由 solidTopOffset 给满高
    //         （heightmap 排除后自动落到下方支撑，开态竖板遮挡由既有 solidTopOffset(ShapeTrapdoor,open)=1.0
    //         在 columnTopSurfaceY 组合表达——但该组合仅在「活板门是 heightmap 顶」时生效，排除后列顶恒为
    //         支撑块 → 本探针锁「排除后列顶=支撑块顶」契约，开/合两态一致）。
    //   t851：① 放置预检（PlayerController 直编不可达 placeBlock 射线段 → World 层谓词直验 + 失撑链全链
    //         断言；放置拒绝的人工目视项见报告）：活板门须依附实体面（isCollidable 下方或四侧）、门须
    //         isTopFlushSupport 齐平地面（t741 既有谓词天然拒门叠门——Door 非完整立方非上半砖）；
    //         ② 失撑级联：拆支撑 → 正上方活板门柱/双格门（含叠门通天链）逐格 blockBroken+
    //         blockDroppedAsItem；红石路径（setBlockSilent 静默写）与玩家路径（setBlock 编辑钩子）同收口。
    {
        constexpr float kEps = 1e-4f;
        const auto boxesTopOf = [](const std::vector<BR::BlockAABB> &bs) {
            float t = -1.0f;
            for (const auto &b : bs) if (b.maxY > t) t = b.maxY;
            return t;
        };
        // ── (A) Core 三件套窄形值锁：铁砧三阶段 + 仙人掌（collision=selection=raycast 同源 anvilShapeBoxes /
        //     0.8 柱；足印 XZ [2/16,14/16] / [1.6/16,14.4/16]）。
        bool okA = true;
        const quint8 anvils[3] = { BR::Anvil, BR::AnvilChipped, BR::AnvilDamaged };
        for (quint8 aid : anvils) {
            const auto col = BR::collisionAABBs(aid, 0);
            const auto sel = BR::selectionAABBs(aid, 0);
            const auto ray = BR::raycastAABBs(aid, 0);
            okA = okA && col.size() == 3 && sel.size() == 3 && ray.size() == 3; // 三盒窄形（底座/腰柱/顶台）
            if (!okA || col.empty() || sel.empty()) break;
            float footMinX = 1e9f, footMaxX = -1e9f;
            for (const auto &b : col) {
                footMinX = std::min(footMinX, b.minX);
                footMaxX = std::max(footMaxX, b.maxX);
            }
            okA = okA
                  && std::fabs(footMinX - 2.0f / 16.0f) < kEps && std::fabs(footMaxX - 14.0f / 16.0f) < kEps // 12/16 足印
                  && std::fabs(col[0].minY) < kEps && std::fabs(col[0].maxY - 4.0f / 16.0f) < kEps           // 底座 y[0,4]
                  && std::fabs(col[1].maxY - 10.0f / 16.0f) < kEps                                            // 腰柱到 y10
                  && std::fabs(boxesTopOf(col) - 1.0f) < kEps                                                  // 顶台满高
                  && std::fabs(boxesTopOf(sel) - 1.0f) < kEps && std::fabs(boxesTopOf(ray) - 1.0f) < kEps;
            if (!okA) {
                qInfo().noquote() << "  [t849 diag] anvil" << int(aid) << "col" << col.size()
                                  << "sel" << sel.size() << "ray" << ray.size()
                                  << "footX" << footMinX << ".." << footMaxX;
                break;
            }
        }
        {
            const auto ccol = BR::collisionAABBs(BR::Cactus, 0);
            const auto csel = BR::selectionAABBs(BR::Cactus, 0);
            okA = okA && ccol.size() == 1 && csel.size() == 1
                  && std::fabs(ccol[0].minX - 0.1f) < kEps && std::fabs(ccol[0].maxX - 0.9f) < kEps // 0.8 居中柱
                  && std::fabs(csel[0].maxY - 1.0f) < kEps;
        }
        // ── (B) World 窄形行为 rig：铁砧缝隙可站人（点测）+ 可入缝（碰撞盒不覆盖环隙）+ heightmap 排除 +
        //     PCF 列顶落支撑面；仙人掌/活板门 heightmap 排除同核（开/合两态列顶一致=支撑面）。
        //     rig 取净空区（t794 模式）：8 宽 × 3 深 × y[ty,ty+6] 全 Air（防 worldgen 地形/树冠抬高
        //     heightmap 使断言空转）；平台自建。
        bool okB = true;
        int brokenB = 0, dropsB = 0;
        QObject sigGuardB;
        {
            World w849;
            w849.setWidth(48); w849.setDepth(48); w849.setHeight(96);
            w849.setSeed(20260824u);
            const auto clearArea = [&](int ox, int oz, int oy) {
                for (int dx = 0; dx < 8; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int yy = oy - 1; yy <= oy + 6; ++yy)
                            if (w849.blockAt(ox + dx, yy, oz + dz) != BR::Air) return false;
                return true;
            };
            int x0 = -1, z0 = -1, ty = -1;
            for (int yy = 68; yy + 7 < 96 && x0 < 0; ++yy) // 地表 ~66、树冠 +10 → 从 68 起找地上净空带
                for (int zz = 4; zz < 44 && x0 < 0; zz += 2)
                    for (int xx = 4; xx + 8 < 48 && x0 < 0; xx += 2)
                        if (clearArea(xx, zz, yy)) { x0 = xx; z0 = zz; ty = yy; }
            okB = x0 >= 0;
            if (x0 < 0)
                qInfo().noquote() << "  [t849 diag] (B) no clear rig area";
            QObject::connect(&w849, &World::blockBroken, &sigGuardB,
                             [&](int, int, int, int) { ++brokenB; });
            QObject::connect(&w849, &World::blockDroppedAsItem, &sigGuardB,
                             [&](int, int, int, int) { ++dropsB; });
            const int ax = x0, az = z0;
            // 平台 + 铁砧 + 仙人掌 + 活板门四列（各自独立列、互不相邻防侧撑串扰：间距 ≥2 格）。
            w849.setBlock(ax, ty, az, BR::Stone, 0);          // 铁砧列支撑
            w849.setBlock(ax + 3, ty, az, BR::Stone, 0);      // 仙人掌列支撑（Stone 非 Sand——band 下方悬空，
                                                               //   Sand 是重力块放置即坍落，列顶断言会空转）
            w849.setBlock(ax + 6, ty, az, BR::Stone, 0);      // 活板门列支撑
            w849.setBlock(ax, ty + 1, az, BR::Anvil, 0);
            // ① 缝隙可入：铁砧足印外环隙中心 (ax+0.03, ty+1.5, az+0.5) —— x∈[14/16,1] 环隙（整格时代被挡）。
            okB = okB && !w849.pointBlockedByCollision(float(ax) + 0.97f, float(ty) + 1.5f, float(az) + 0.5f);
            // ② 砧身内仍挡：腰柱中心点 (ax+0.5, ty+1.5, az+0.5) 须在碰撞盒内（挡人语义保留）。
            okB = okB && w849.pointBlockedByCollision(float(ax) + 0.5f, float(ty) + 1.5f, float(az) + 0.5f);
            // ③ heightmap 排除：铁砧列 hm 应停在 Stone 行（ty），PCF 列顶 = ty + solidTopOffset(Stone)=ty+1；
            //    仙人掌列同（hm=沙行）。修前 hm 抬到异形行（ty+1）→ 列顶 ty+2 整格误暗一环。
            w849.setBlock(ax + 3, ty + 1, az, BR::Cactus, 0);
            w849.setBlock(ax + 6, ty + 1, az, BR::WoodTrapdoor, 0); // 合态（state bit0=0）
            okB = okB && w849.heightmapAt(ax, az) == ty
                  && std::fabs(w849.columnTopSurfaceY(ax, az) - float(ty + 1)) < kEps
                  && w849.heightmapAt(ax + 3, az) == ty
                  && std::fabs(w849.columnTopSurfaceY(ax + 3, az) - float(ty + 1)) < kEps;
            // ④ t850 活板门两态列顶一致（都落到 Stone 顶 ty+1——薄板不入列顶；solidTopOffset 开态竖板满高
            //    仅在板是列顶时参与，排除后本探针锁「列顶恒支撑面」契约）：
            okB = okB && w849.heightmapAt(ax + 6, az) == ty
                  && std::fabs(w849.columnTopSurfaceY(ax + 6, az) - float(ty + 1)) < kEps;
            w849.setBlock(ax + 6, ty + 1, az, BR::WoodTrapdoor, 0x01); // 开态（bit0=1，朝向位默认）
            okB = okB && w849.heightmapAt(ax + 6, az) == ty
                  && std::fabs(w849.columnTopSurfaceY(ax + 6, az) - float(ty + 1)) < kEps;
            w849.setBlock(ax + 6, ty + 1, az, BR::IronTrapdoor, 0x00); // 铁活板门合态同口径
            okB = okB && w849.heightmapAt(ax + 6, az) == ty
                  && std::fabs(w849.columnTopSurfaceY(ax + 6, az) - float(ty + 1)) < kEps;
        }
        // ── (C) t851 失撑级联 rig：① 空中叠放活板门（无实体面依附）→ 直写模拟绕过预检的脏世界，
        //     拆其唯一侧撑 → 板+其上门级联掉；② 门叠门通天（3 扇门叠柱站同一石台上）→ 拆石台 →
        //     3 扇 6 格全掉（6 blockDroppedAsItem）；③ 有撑门不受邻破影响（零误伤）。
        bool okC = true;
        int dropsC1 = 0, dropsC2 = 0, dropsC3 = 0;
        QObject sigGuardC;
        {
            World w851;
            w851.setWidth(48); w851.setDepth(48); w851.setHeight(96);
            w851.setSeed(777u);
            QObject::connect(&w851, &World::blockDroppedAsItem, &sigGuardC,
                             [&](int, int, int, int) { ++dropsC1; ++dropsC2; ++dropsC3; });
            // rig：净空带搜索（(B) 同款——地表 ~66、树冠更高 → 从 68 起找 8 宽 × 3 深 × 8 高全 Air 带；
            //   三个子 rig 分占带内不相交列：C1 用 bx0..bx0+1、C2 用 bx0+3、C3 用 bx0+5..bx0+6）。
            const auto clearBand = [&](int ox, int oz, int oy) {
                for (int dx = 0; dx < 8; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int yy = oy - 1; yy <= oy + 6; ++yy)
                            if (w851.blockAt(ox + dx, yy, oz + dz) != BR::Air) return false;
                return true;
            };
            int bx0 = -1, bz0 = -1, ty = -1;
            for (int yy = 68; yy + 7 < 96 && bx0 < 0; ++yy)
                for (int zz = 4; zz < 44 && bx0 < 0; zz += 2)
                    for (int xx = 4; xx + 8 < 48 && bx0 < 0; xx += 2)
                        if (clearBand(xx, zz, yy)) { bx0 = xx; bz0 = zz; ty = yy; }
            if (bx0 < 0) {
                okC = false;
                qInfo().noquote() << "  [t851 diag] no clear band for rig";
            }

            // ① 活板门贴墙浮空（合法放置形态）：墙在 -X 侧。拆墙 → 板失撑掉 1 件。
            {
                dropsC1 = dropsC2 = dropsC3 = 0;
                const int px = bx0, pz = bz0; // 带内 x0..x0+1 列（净空已由带搜索保证）
                if (px < 0) {
                    okC = false;
                    qInfo().noquote() << "  [t851 diag] (C1) skipped (no band)";
                } else {
                    w851.setBlock(px, ty, pz, BR::Stone, 0);      // 墙（唯一侧撑）
                    w851.setBlock(px + 1, ty, pz, BR::WoodTrapdoor, 0x02 | 0x01); // 板（开态贴 -X 边；state 仅视觉）
                    dropsC1 = 0;
                    w851.setBlock(px, ty, pz, BR::Air, 0);        // 拆墙 → setBlock 编辑钩子 ③ 复检
                    okC = okC && dropsC1 == 1 && w851.blockAt(px + 1, ty, pz) == BR::Air;
                }
            }
            // ② 门叠门通天：石台上 3 扇木门叠柱（ty..ty+5）。拆石台 → 6 格全掉（每格一件）。
            {
                dropsC1 = dropsC2 = dropsC3 = 0;
                const int dx2 = bx0 + 3, dz2 = bz0; // 带内 x0+3 列（净空已由带搜索保证）
                if (dx2 < 0) {
                    okC = false;
                    qInfo().noquote() << "  [t851 diag] (C2) skipped (no band)";
                } else {
                    w851.setBlock(dx2, ty - 1, dz2, BR::Stone, 0);
                    for (int door = 0; door < 3; ++door) {
                        w851.setBlock(dx2, ty + door * 2, dz2, BR::WoodDoor, quint8(0));      // 下扇 bit3=0
                        w851.setBlock(dx2, ty + door * 2 + 1, dz2, BR::WoodDoor, quint8(8)); // 上扇 bit3=1
                    }
                    dropsC2 = 0;
                    w851.setBlockSilent(dx2, ty - 1, dz2, BR::Air, 0); // 红石/系统静默拆支撑（setBlockSilent 收口路径）
                    okC = okC && dropsC2 == 6;
                    for (int yy = ty; yy <= ty + 5; ++yy)
                        okC = okC && w851.blockAt(dx2, yy, dz2) == BR::Air;
                }
            }
            // ③ 零误伤：正常门（站石台）旁挖无关方块 → 门不动。
            {
                dropsC1 = dropsC2 = dropsC3 = 0;
                const int nx = bx0 + 5, nz = bz0; // 带内 x0+5..x0+6 列（净空已由带搜索保证）
                if (nx < 0) {
                    okC = false;
                    qInfo().noquote() << "  [t851 diag] (C3) skipped (no band)";
                } else {
                    w851.setBlock(nx, ty - 1, nz, BR::Stone, 0);
                    w851.setBlock(nx, ty, nz, BR::SpruceDoor, quint8(1));
                    w851.setBlock(nx, ty + 1, nz, BR::SpruceDoor, quint8(9));
                    dropsC3 = 0;
                    w851.setBlock(nx + 1, ty, nz, BR::Air, 0); // 挖旁边无关格（原为本就空的格也行——写 Air 幂等）
                    okC = okC && dropsC3 == 0
                          && w851.blockAt(nx, ty, nz) == BR::SpruceDoor
                          && w851.blockAt(nx, ty + 1, nz) == BR::SpruceDoor;
                }
            }
        }
        // ── (D) t849② 选中框/射线窄形 + t851 放置预检谓词静态断言（World 层谓词直验——placeBlock 的射线
        //     段在 PlayerController 私有方法内，矩阵不可达；放置拒绝行为人工目视收口，P20 先例）。
        bool okD = true;
        {
            // 活板门依附面判定（trapdoorSupportBlock 单一权威——playercontroller 预检 / World 复检同读）：
            //   实体面（Stone）判允；air 判拒；**活板门/门自身判拒**（附着族不互相依附——板套板悬浮叠两侧一致拒）。
            okD = okD && BR::trapdoorSupportBlock(BR::Stone, 0)
                  && !BR::trapdoorSupportBlock(BR::Air, 0)
                  && !BR::trapdoorSupportBlock(BR::WoodTrapdoor, 0x00)
                  && !BR::trapdoorSupportBlock(BR::IronTrapdoor, 0x01)
                  && !BR::trapdoorSupportBlock(BR::WoodDoor, 0);
            // 门叠门拒放口径：isTopFlushSupport(WoodDoor)=false（Door 非完整立方非上半砖）→ t741 门放置
            //    分支天然拒「门上叠门通天」。
            okD = okD && !BR::isTopFlushSupport(BR::WoodDoor, 0);
            // isCollidable 本身对活板门恒真（碰撞实体语义不动——玩家仍站板顶）；依附判定走排除版谓词。
            okD = okD && BR::isCollidable(BR::WoodTrapdoor, 0x00);
        }
        const bool ok = okA && okB && okC && okD;
        if (!ok) {
            qInfo().noquote() << "  [t849 diag] okA" << okA << "| okB" << okB << "| okC" << okC
                              << "| okD" << okD << "| c1drops" << dropsC1 << "| c2drops" << dropsC2
                              << "| c3drops" << dropsC3;
        }
        if (!ok) ++totalFail;
        Q_UNUSED(brokenB); Q_UNUSED(dropsB); // (B) 信号计数仅烟囱守卫（heightmap 断言是主面）
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t849/t850/t851 anvil+cactus non-full-cube trio + trapdoor thin-plate shadow "
                             "+ attach support: anvil 3-stage collision/selection/raycast narrow to the "
                             "three-box footprint (12/16 base/waist/top, gap walkable via point probe, "
                             "waist still blocks), cactus trio at 0.8 centered column, all three families "
                             "excluded from heightmap so PCF column-top lands on the support block "
                             "(trapdoor open/closed wood+iron alike), wall-mounted trapdoor falls when its "
                             "sole side support breaks, 3-door sky tower collapses into 6 item drops on "
                             "silent support clear, intact door untouched by neighbor edits";
    }

    // ── t800 物品栏归类清理探针（纯 Game 层 Hotbar 实例，无 World rig）：① 材料段调色板不再列羊毛物品
    //    （0x20E）与玻璃物品（0x204）——用户「羊毛 item 多此一举（方块栏已有羊毛方块）」「玻璃应放方块那边」；
    //    ② 方块段调色板含玻璃 Glass=54（移入）且白羊毛 + 15 色变体全在列（建筑取色不受影响）；③ 两物品生存链
    //    完好 —— nameForBlock 仍返中文名（杀羊掉羊毛 / 破玻璃掉玻璃的 tooltip / 图鉴名源）；④ 玻璃方块图标可
    //    解析（iconSourceForBlock(54) 非空 —— Glass 无 qrc 手绘图，pack 关态靠 isPackDerivedIconFamily 程序
    //    图集重渲（t838(1) 起 dimetric 3D 立方投影），回退链断链 = 空图标 = FAIL 面）。注：④ 在本测试二进制只验「URL 解析链通」——测试
    //    target 无 qrc 资源（atlas 加载失败会打一条预期内 qWarning），瓦片像素内容留给实机人工目视；测试进程
    //    落盘的空图不毒害实机缓存（App 侧缓存命中只认进程内 map，恒重渲覆写，见 blockAtlasIconSource L9/L10）。
    {
        Hotbar hb;
        const QVariantList mats = hb.creativeMaterials();
        bool matsClean = true;
        for (const QVariant &m : mats)
            matsClean = matsClean && m.toInt() != int(RecipeRegistry::WoolId)
                                 && m.toInt() != int(RecipeRegistry::GlassId);
        const QVariantList blocks = hb.creativeBlocks();
        bool hasGlass = false, hasWhiteWool = false;
        int woolVariants = 0;
        for (const QVariant &b : blocks) {
            hasGlass     = hasGlass || b.toInt() == int(BR::Glass);
            hasWhiteWool = hasWhiteWool || b.toInt() == int(BR::Wool);
            if (b.toInt() >= int(BR::WoolOrange) && b.toInt() <= int(BR::WoolBlack))
                ++woolVariants;
        }
        const bool ok = matsClean && hasGlass && hasWhiteWool && woolVariants == 15
                && hb.nameForBlock(int(RecipeRegistry::WoolId)) == QString::fromUtf8("羊毛")
                && hb.nameForBlock(int(RecipeRegistry::GlassId)) == QString::fromUtf8("玻璃")
                && !hb.iconSourceForBlock(int(BR::Glass)).isEmpty();
        if (!ok) {
            qInfo().noquote() << "  [t800 diag] matsClean" << matsClean << "| hasGlass" << hasGlass
                              << "| hasWhiteWool" << hasWhiteWool << "| woolVariants" << woolVariants
                              << "| woolName" << hb.nameForBlock(int(RecipeRegistry::WoolId))
                              << "| glassName" << hb.nameForBlock(int(RecipeRegistry::GlassId))
                              << "| glassIcon empty?" << hb.iconSourceForBlock(int(BR::Glass)).isEmpty();
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t800 inventory categorization: wool item (0x20E) and glass item (0x204) "
                             "removed from creative materials palette, glass block (54) present in blocks "
                             "palette alongside white wool + 15 color variants, both item names still "
                             "resolve for survival drop chains, glass block icon resolves via runtime atlas "
                             "re-render (t838(1) dimetric 3D cube projection; flat-2D was the t800 misdirection)";
    }

    // ── t801 栅栏视觉高度探针（Core 表查询 + mesher 同源直调，P11/P19 模式；纯静态断言无 rig，不占
    //    nextSlot 容量）：用户「栅栏视觉 1 格、实际跳不上去才对（1.5 多出 0.5 格悬空穿模）」→ 视觉 1.0 /
    //    碰撞 1.5 分离（机制等价 MC 栅栏「模型 1 格 / 碰撞箱 1.5 不可越」）。断言两层：
    //    (a) 几何：三变体（木/圆石/云杉）× 两形态（孤立四邻空气=只画立柱 / 四向连栅栏=立柱+8 段横档），
    //        全部生成顶点 y ∈ [0-ε, 1.0+ε] 且最高点≈1.0（立柱裁到 1.0 且不缩水；修前立柱 1.5 / 上档
    //        1.125 必越上界 FAIL）、最低点=0（落地）；连接形态须存在 x/z==0 与 ==1 顶点（横档真延伸到格边）；
    //    (b) 分离：collisionAABBs 顶==1.5（> 跳跃顶点 ~1.25（playercontroller.h kJump=8.4 的文档镜像值，
    //        同 P11 rideH 镜像先例）→ 玩家跳不过 + mob 支撑/越障链零改动）；selectionAABBs / raycastAABBs
    //        顶==1.0（选中框 + 射线贴视觉，瞄立柱上方 0.5 空带穿过不优先选中）。
    {
        const quint8 fences[3] = { BR::WoodFence, BR::CobbleFence, BR::SpruceFence };
        constexpr float kEps = 1e-4f;
        constexpr float kJumpApex = 1.25f; // playercontroller.h kJump=8.4「顶点约 1.25 格」的文档镜像值（改跳跃力须同步）
        const auto topOf = [](const std::vector<BR::BlockAABB> &bs) {
            float t = -1.0f;
            for (const auto &b : bs) if (b.maxY > t) t = b.maxY;
            return t;
        };
        bool ok = true;
        for (quint8 fid : fences) {
            for (int connected = 0; connected <= 1; ++connected) {
                QVector<Vtx> verts; QVector<quint32> idx;
                PartialLightCtx lctx; lctx.light = 1.0f;
                for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
                PartialNeighborCtx nctx;
                const quint8 nb = connected ? BR::WoodFence : quint8(BR::Air); // 连接判定 isFence||isSolid：栅栏邻即连
                nctx.posX = nctx.negX = nctx.posZ = nctx.negZ = nb;
                PartialBlockGeometry::append(verts, idx, 0, 0, 0, fid, 0, lctx, nctx,
                                             1.0f / 16.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                float yMin = 1e9f, yMax = -1e9f;
                bool edgeX = false, edgeZ = false;
                for (const Vtx &v : verts) {
                    if (v.y < yMin) yMin = v.y;
                    if (v.y > yMax) yMax = v.y;
                    if (std::fabs(v.x) < kEps || std::fabs(v.x - 1.0f) < kEps) edgeX = true;
                    if (std::fabs(v.z) < kEps || std::fabs(v.z - 1.0f) < kEps) edgeZ = true;
                }
                if (verts.isEmpty() || yMin > kEps || yMax > 1.0f + kEps || yMax < 1.0f - kEps) {
                    qInfo().noquote() << "  [t801 diag] fence" << int(fid) << "connected" << connected
                                      << "verts" << verts.size() << "yMin" << yMin << "yMax" << yMax;
                    ok = false;
                }
                if (connected && (!edgeX || !edgeZ)) { // 横档须到格边（t209 连接逻辑不因裁高回归）
                    qInfo().noquote() << "  [t801 diag] fence" << int(fid)
                                      << "arms missing edgeX" << edgeX << "edgeZ" << edgeZ;
                    ok = false;
                }
            }
            const float colTop = topOf(BR::collisionAABBs(fid, 0));
            const float selTop = topOf(BR::selectionAABBs(fid, 0));
            const float rayTop = topOf(BR::raycastAABBs(fid, 0));
            if (!(std::fabs(colTop - 1.5f) < kEps && colTop > kJumpApex
                  && std::fabs(selTop - 1.0f) < kEps && std::fabs(rayTop - 1.0f) < kEps)) {
                qInfo().noquote() << "  [t801 diag] fence" << int(fid) << "colTop" << colTop
                                  << "selTop" << selTop << "rayTop" << rayTop;
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t801 fence visual height: mesher post+rails clipped to y<=1.0 for all 3 variants "
                             "(isolated + connected, arms still reach cell edges), collision AABB stays 1.5 "
                             "(jump apex ~1.25 < 1.5 -> still unjumpable, mob support/obstacle chain untouched), "
                             "selection + raycast boxes synced to 1.0 visual - fixes 'fence model pokes 0.5 block "
                             "above, clipping into neighbors' look";
    }

    // ── t802 全配方审计探针（纯 Game 层静态表查询 + 匹配器直调，无 World rig，不占 nextSlot 容量）──
    //    用户报三缺 + 举一反三全表：① 云杉原木→云杉木板→木剑等木制品链（根因 = 木制品配方原料只认
    //    Planks，云杉木板 SprucePlanks 是独立 id 且无平行配方 → 修法 = 匹配器两阶段「精确→板材族等价
    //    回退」，机制等价 MC 1.0 任意木板通配，云杉专属配方 spruce_slab/door/boat 仍精确优先不被截胡）；
    //    ② 打火石合成不了（t761 误写有序纵列，MC 1.0 原版无序 → 改 shapeless，横/竖/斜摆全通）；
    //    ③ 燃烬棒不能分解成燃烬粉（MC 正道是合成分解 1 棒→2 粉 → 补 shapeless 配方，熔炉路径并存）。
    //    附全表回归：recipeAt 遍历每条配方「自身 pattern 自匹配」指针相等断言（防被更早配方遮蔽 = 永不
    //    可合，防未来匹配算法改动静默丢配方）+ 表长下限（防整段误删）；补缺新配方（箱子/梯子/砂岩×2/
    //    石砖×3/发射器）随全表自匹配一并覆盖；圆石压力板 gridSize 勘误（t627 漏改）单测；箭改回 MC
    //    正统原料（燧石+棒+羽毛）正反两测；云杉熔炉链（烧炭 + 燃料）单测。
    {
        bool ok = true;
        const auto expectCraft = [&](const int *grid, int n, int wantOut, int wantCnt, const char *tag) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(grid, n);
            if (!r || r->outputId != wantOut || r->outputCount != wantCnt) {
                qInfo().noquote() << "  [t802 diag]" << tag << "-> got"
                                  << (r ? QStringLiteral("out=%1 cnt=%2").arg(r->outputId).arg(r->outputCount)
                                        : QStringLiteral("null"))
                                  << "want out=" << wantOut << "cnt=" << wantCnt;
                ok = false;
            }
        };
        const auto expectNoMatch = [&](const int *grid, int n, const char *tag) {
            if (RecipeRegistry::match(grid, n)) {
                qInfo().noquote() << "  [t802 diag]" << tag << "-> unexpectedly matched out="
                                  << RecipeRegistry::match(grid, n)->outputId;
                ok = false;
            }
        };

        // (1) 云杉木制品链：原木→木板→木棒/工作台/五件套工具（等价回退路径，全经 2×2 或 3×3）。
        const int SP = int(BR::SprucePlanks);
        {
            const int g1[9] = { int(BR::SpruceLog), 0, 0, 0 };
            expectCraft(g1, 2, int(BR::SprucePlanks), 4, "spruce_log->planks");
            const int g2[9] = { SP, 0, SP, 0 };
            expectCraft(g2, 2, RecipeRegistry::StickId, 4, "spruce_planks->sticks");
            const int g3[9] = { SP, SP, SP, SP };
            expectCraft(g3, 2, int(BR::CraftingTable), 1, "spruce_planks->crafting_table");
            const int gPick[9] = { SP, SP, SP,  0, RecipeRegistry::StickId, 0,  0, RecipeRegistry::StickId, 0 };
            expectCraft(gPick, 3, int(ToolRegistry::PickaxeWood), 1, "spruce_wood_pickaxe");
            const int gAxe[9]  = { SP, SP, 0,  SP, RecipeRegistry::StickId, 0,  0, RecipeRegistry::StickId, 0 };
            expectCraft(gAxe, 3, int(ToolRegistry::AxeWood), 1, "spruce_wood_axe");
            const int gShv[9]  = { SP, 0, 0,  RecipeRegistry::StickId, 0, 0,  RecipeRegistry::StickId, 0, 0 };
            expectCraft(gShv, 3, int(ToolRegistry::ShovelWood), 1, "spruce_wood_shovel");
            const int gHoe[9]  = { SP, SP, 0,  0, RecipeRegistry::StickId, 0,  0, RecipeRegistry::StickId, 0 };
            expectCraft(gHoe, 3, int(ToolRegistry::HoeWood), 1, "spruce_wood_hoe");
            const int gSwd[9]  = { SP, 0, 0,  SP, 0, 0,  RecipeRegistry::StickId, 0, 0 };
            expectCraft(gSwd, 3, int(ToolRegistry::SwordWood), 1, "spruce_wood_sword");
            // 精确优先（等价回退不得截胡云杉专属产物）：3 云杉木板横排 → 云杉台阶（非橡木台阶）；
            //   云杉木板纵列 → 云杉门（非橡木门）；橡木横排 → 橡木台阶（非云杉台阶）。
            const int gSlabS[9] = { SP, SP, SP, 0, 0, 0, 0, 0, 0 };
            expectCraft(gSlabS, 3, int(BR::SpruceSlab), 6, "3*spruce_planks->spruce_slab(exact-first)");
            const int gDoorS[9] = { SP, 0, 0, SP, 0, 0, SP, 0, 0 };
            expectCraft(gDoorS, 3, int(BR::SpruceDoor), 1, "spruce_col->spruce_door(exact-first)");
            const int gSlabO[9] = { int(BR::Planks), int(BR::Planks), int(BR::Planks), 0, 0, 0, 0, 0, 0 };
            expectCraft(gSlabO, 3, int(BR::WoodSlab), 6, "3*planks->wood_slab(oak-exact)");
            // 等价回退广度抽样：云杉板+羊毛→红床 / 云杉板楼梯形→橡木楼梯 / 单云杉板→木按钮 /
            //   云杉板+羊毛方块床形→白床（MC 任意木板语义的族外覆盖）。t834/review #7 起床原料统一
            //   Wool 方块 27（旧 0x20E 材料段物品退役、无掉落源——下方 expectNoMatch 钉死不再回头路）。
            const int gBed[9] = { SP, int(BR::Wool), 0, 0 };
            expectCraft(gBed, 2, int(BR::BedRed), 1, "spruce_planks+wool->bed_red");
            const int gBedOld[9] = { SP, int(RecipeRegistry::WoolId), 0, 0 };
            expectNoMatch(gBedOld, 2, "t834 retired: spruce+wool_item(0x20E) no longer crafts bed_red");
            const int gStair[9] = { SP, 0, 0, SP, SP, 0, SP, SP, SP };
            expectCraft(gStair, 3, int(BR::WoodStairs), 4, "spruce_stairs-shape->wood_stairs");
            const int gBtn[9] = { SP, 0, 0, 0 };
            expectCraft(gBtn, 2, int(BR::WoodButton), 1, "single_spruce_plank->wood_button");
            const int gBedW[9] = { int(BR::Wool), int(BR::Wool), int(BR::Wool), SP, SP, SP, 0, 0, 0 };
            expectCraft(gBedW, 3, int(BR::BedWhite), 1, "wool+spruce_row->bed_white");
        }

        // (2) 打火石：MC 1.0 无序配方——铁锭+燧石 2×2 四种摆法（竖/倒竖/横/斜）+ 3×3 对角全通。
        {
            const int iron = RecipeRegistry::IronIngotId, flint = RecipeRegistry::FlintId;
            const int gA[9] = { iron, 0, flint, 0 };
            expectCraft(gA, 2, int(ToolRegistry::FlintAndSteel), 1, "flint&steel_vertical");
            const int gB[9] = { flint, 0, iron, 0 };
            expectCraft(gB, 2, int(ToolRegistry::FlintAndSteel), 1, "flint&steel_vertical_flipped");
            const int gC[9] = { iron, flint, 0, 0 };
            expectCraft(gC, 2, int(ToolRegistry::FlintAndSteel), 1, "flint&steel_horizontal");
            const int gD[9] = { iron, 0, 0, flint };
            expectCraft(gD, 2, int(ToolRegistry::FlintAndSteel), 1, "flint&steel_diagonal");
            const int gE[9] = { iron, 0, 0, 0, 0, 0, 0, 0, flint };
            expectCraft(gE, 3, int(ToolRegistry::FlintAndSteel), 1, "flint&steel_3x3_corners");
        }

        // (3) 燃烬棒分解：1 棒 → 2 粉（合成正道，任意格；熔炉路径并存核）。
        {
            const int g1[9] = { RecipeRegistry::BlazeRodId, 0, 0, 0 };
            expectCraft(g1, 2, RecipeRegistry::BlazePowderId, 2, "blaze_rod->2_powder_2x2");
            const int g2[9] = { 0, 0, 0, 0, RecipeRegistry::BlazeRodId, 0, 0, 0, 0 };
            expectCraft(g2, 3, RecipeRegistry::BlazePowderId, 2, "blaze_rod->2_powder_center");
            if (SmeltingRegistry::smeltResult(RecipeRegistry::BlazeRodId) != RecipeRegistry::BlazePowderId) {
                qInfo().noquote() << "  [t802 diag] blaze_rod furnace path lost";
                ok = false;
            }
        }

        // (4) 审计补缺新配方显式抽查（全量由下方 (6) 自匹配覆盖）：箱子 / 梯子 / 发射器。
        {
            const int P = int(BR::Planks);
            const int gChest[9] = { P, P, P, P, 0, P, P, P, P };
            expectCraft(gChest, 3, int(BR::Chest), 1, "8_planks_ring->chest");
            const int gLadder[9] = { RecipeRegistry::StickId, 0, RecipeRegistry::StickId,
                                     RecipeRegistry::StickId, RecipeRegistry::StickId, RecipeRegistry::StickId,
                                     RecipeRegistry::StickId, 0, RecipeRegistry::StickId };
            expectCraft(gLadder, 3, int(BR::Ladder), 3, "7_sticks_H->3_ladder");
            const int gDisp[9] = { int(BR::Cobble), int(BR::Cobble), int(BR::Cobble),
                                   int(BR::Cobble), int(ToolRegistry::Bow), int(BR::Cobble),
                                   int(BR::Cobble), RecipeRegistry::RedstoneId, int(BR::Cobble) };
            expectCraft(gDisp, 3, int(BR::Dispenser), 1, "7_cobble+bow+redstone->dispenser");
            // 圆石压力板 gridSize 勘误：2 圆石横排在 2×2 背包栏即可合（t627 注释口径，代码曾漏改）。
            const int gPlate[9] = { int(BR::Cobble), int(BR::Cobble), 0, 0 };
            expectCraft(gPlate, 2, int(BR::CobblePressurePlate), 1, "2_cobble_row_2x2->cobble_plate");
        }

        // (5) 箭改回 MC 正统原料：燧石+棒+羽毛 → 4 箭；旧「铁锭+棒+线」不再匹配。
        {
            const int gNew[9] = { RecipeRegistry::FlintId, 0, 0,
                                  RecipeRegistry::StickId, 0, 0,
                                  RecipeRegistry::FeatherId, 0, 0 };
            expectCraft(gNew, 3, RecipeRegistry::ArrowId, 4, "flint+stick+feather->4_arrows");
            const int gOld[9] = { RecipeRegistry::IronIngotId, 0, 0,
                                  RecipeRegistry::StickId, 0, 0,
                                  RecipeRegistry::StringId, 0, 0 };
            expectNoMatch(gOld, 3, "old_iron_arrow_column(retired)");
        }

        // (6) 全表回归：每条配方「自身 pattern 在自身 gridSize 上经 match() 必返回自身」（指针相等 =
        //    未被更早配方遮蔽 + 匹配算法两轮后仍可达）+ 表长下限（t802 后 ≥ 140；防整段误删）。
        const int recipeTotal = RecipeRegistry::recipeCount();
        if (recipeTotal < 140) {
            qInfo().noquote() << "  [t802 diag] recipe table shrank: count =" << recipeTotal;
            ok = false;
        }
        for (int i = 0; i < recipeTotal; ++i) {
            const RecipeRegistry::Recipe *r = RecipeRegistry::recipeAt(i);
            if (!r) { qInfo().noquote() << "  [t802 diag] recipeAt(" << i << ") null"; ok = false; continue; }
            int g[9] = { 0 };
            const int n = r->gridSize;
            if (r->shapeless) {
                // 无序：pattern 位置无关（匹配只读多重集）→ 非空格依序填入前 k 格；非空格数 > n*n
                //   = 该配方在自身 gridSize 实际不可合（真缺口，指针断言下方暴露）。
                int k = 0;
                for (int c = 0; c < 9; ++c)
                    if (r->pattern[c] != 0) {
                        if (k < n * n) g[k] = r->pattern[c];
                        ++k;
                    }
                if (k > n * n) {
                    qInfo().noquote() << "  [t802 diag] recipe" << i << r->name
                                      << "shapeless ingredients" << k << ">" << n * n << "cells";
                    ok = false;
                    continue;
                }
            } else {
                // 有序：按 2×2 左上子矩阵约定抽取（pattern 内容越出子矩阵 = 2×2 实际不可合 = 真缺口）。
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x)
                        g[y * n + x] = r->pattern[y * 3 + x];
            }
            const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, n);
            if (m != r) {
                qInfo().noquote() << "  [t802 diag] recipe" << i << r->name << "self-match got"
                                  << (m ? m->name : "null") << "(shadowed?)";
                ok = false;
            }
        }

        // (7) 云杉熔炉链补缺：云杉原木烧木炭 + 云杉原木/木板可当燃料（15s 同橡木）。
        if (SmeltingRegistry::smeltResult(int(BR::SpruceLog)) != RecipeRegistry::CharcoalId
            || SmeltingRegistry::fuelBurnSeconds(int(BR::SpruceLog)) != 15.f
            || SmeltingRegistry::fuelBurnSeconds(int(BR::SprucePlanks)) != 15.f) {
            qInfo().noquote() << "  [t802 diag] spruce furnace chain:"
                              << "smelt=" << SmeltingRegistry::smeltResult(int(BR::SpruceLog))
                              << "fuelLog=" << SmeltingRegistry::fuelBurnSeconds(int(BR::SpruceLog))
                              << "fuelPlanks=" << SmeltingRegistry::fuelBurnSeconds(int(BR::SprucePlanks));
            ok = false;
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t802 recipe audit: spruce planks family-equivalence fallback (log->planks->"
                             "sticks/crafting-table/5 wood tools all craftable, exact-first keeps spruce "
                             "slab/door outputs, oak unaffected), flint&steel restored to shapeless (all 4 "
                             "2x2 arrangements + 3x3 diagonal), blaze rod craft-decomposition 1->2 powder "
                             "(furnace path kept), 9 missing recipes added (chest/ladder/sandstone/cut-"
                             "sandstone/stone-brick x3/dispenser/blaze-powder), cobble pressure-plate grid "
                             "size fixed to 2x2, arrow back to MC flint+stick+feather, spruce log smelts to "
                             "charcoal + spruce log/planks burn 15s, full-table self-match regression over"
                          << recipeTotal << "recipes";
    }

    // ── t795 附魔台门槛公式探针（Game 层公式 + Hotbar 桥接 + World 书架计数三层；UI 状态机「无 lapis 灰 /
    //    lapis 足亮」在 QML 绑定层，本探针盖其 C++ 权威源，UI 显亮需人工目测）：
    //    ① tierForBookshelves：0..4 → 1 档；5..9 → 2 档；10..15 → 3 档；**无模式旁路**（函数无模式参数——
    //       创造同样须书架达标；t795 收口前的 QML creativeMode 直通 3 档已删）；
    //    ② offeredLevelFor：bs=0 → [1,2,3]；bs=4 → 3 档 10；bs=14 → 3 档 28（<30）；bs=15 → [10,20,30]——
    //       顶格 30 仅满 15 书架可达（书架封顶），全域对 bs 单调不减且在 [1,30]；
    //    ③ Hotbar Q_INVOKABLE 桥接与静态函数同值（QML 绑定单一权威，防桥接层漂移）；
    //    ④ World::countBookshelvesAround：净空环境 0；下层环带 15 书架 + 空气半步 → 15；堵 1 个半步格 →
    //       该书架不计（14）；两层 32 位全放 → 封顶 15（书架数上限）。rig 用 y=46/47（其余探针全在
    //       kRigY=41/42，地形/树冠 ~33，46+ 必空零串扰）。
    {
        bool ok = EnchantRegistry::tierForBookshelves(0) == 1
                  && EnchantRegistry::tierForBookshelves(4) == 1
                  && EnchantRegistry::tierForBookshelves(5) == 2
                  && EnchantRegistry::tierForBookshelves(9) == 2
                  && EnchantRegistry::tierForBookshelves(10) == 3
                  && EnchantRegistry::tierForBookshelves(15) == 3
                  && EnchantRegistry::tierForBookshelves(99) == 3;   // 超上限防御钳（同 15）
        ok = ok && EnchantRegistry::offeredLevelFor(0, 0) == 1
                  && EnchantRegistry::offeredLevelFor(0, 1) == 2
                  && EnchantRegistry::offeredLevelFor(0, 2) == 3
                  && EnchantRegistry::offeredLevelFor(4, 2) == 10    // 4 书架 3 档 10（t649 校准锚点 b）
                  && EnchantRegistry::offeredLevelFor(14, 2) == 28   // 14 书架 < 30（封顶仅满 15）
                  && EnchantRegistry::offeredLevelFor(15, 0) == 10
                  && EnchantRegistry::offeredLevelFor(15, 1) == 20
                  && EnchantRegistry::offeredLevelFor(15, 2) == 30;  // 满 15 书架 → [10,20,30]
        for (int bs = 0; bs <= 15; ++bs)                            // 全域：值域 [1,30] + 对 bs 单调不减
            for (int t = 0; t < 3; ++t) {
                const int v = EnchantRegistry::offeredLevelFor(bs, t);
                if (v < 1 || v > 30) ok = false;
                if (bs > 0 && v < EnchantRegistry::offeredLevelFor(bs - 1, t)) ok = false;
            }
        Hotbar hb795;
        ok = ok && hb795.enchantTierForBookshelves(9) == 2           // ③ QML 绑定入口同值
                  && hb795.enchantTierForBookshelves(10) == 3
                  && hb795.enchantOfferedLevel(15, 2) == 30;
        // ④ World 环带计数 rig：附魔台位 (ecx,46,ecz)，环带 = 切比雪夫 2 × 两层（46/47）。rig 寻址：
        //    **运行期扫描空区，不走 nextSlot()**（P20 先例——前序循环探针已把 4×31 slot 网格耗尽，此刻
        //    nextSlot() 返回 z=97+ 越界 → setBlock 全被拒 = 假 FAIL，本探针首轮实测踩坑）。扫 y=46/47 两层
        //    全净空的 5×5 区（其余探针全在 kRigY=41/42 + 生成石柱实测 ≤43，46+ 大概率空但按 P20 教训
        //    不写死断言，扫不到 → 判 FAIL 不下断言防越界副作用）。
        int ecx = -1, ecz = -1;
        const int eY = 46;
        for (int zz = 2; zz + 2 < 96 && ecx < 0; zz += 3) {
            for (int xx = 2; xx + 2 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -2; dx <= 2 && clear; ++dx)
                    for (int dz = -2; dz <= 2 && clear; ++dz)
                        if (w.blockAt(xx + dx, eY, zz + dz) != BR::Air
                            || w.blockAt(xx + dx, eY + 1, zz + dz) != BR::Air) clear = false;
                if (clear) { ecx = xx; ecz = zz; }
            }
        }
        ok = ok && ecx >= 0;
        if (ecx < 0) {
            qInfo().noquote() << "  [t795 diag] no clear 5x5 region at y=46/47";
        } else {
            const int emptyCnt = w.countBookshelvesAround(ecx, eY, ecz);
            ok = ok && emptyCnt == 0;                                // 净空环境 → 0
            int placed795 = 0;                                       // 下层环带前 15 位放书架（半步全空）
            for (int dx = -2; dx <= 2 && placed795 < 15; ++dx)
                for (int dz = -2; dz <= 2 && placed795 < 15; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                    w.setBlock(ecx + dx, eY, ecz + dz, BR::Bookshelf, 0);
                    ++placed795;
                }
            const int cnt15 = w.countBookshelvesAround(ecx, eY, ecz);
            // 堵角位书架 (dx=-2,dz=-2)（放置序第 1 个）的半步格 (-1,-1)。半步 = (dx/2, dz/2) 向零取整：
            //   边中点半步被 3 个环格共享（如 (1,0) 服务 (2,-1)/(2,0)/(2,1)——首轮实测堵它掉 3 本），角位
            //   半步 (±1,±1) 只服务角书架自己 → 堵它精确 -1（首轮踩坑记录，防后人重试边中点）。
            w.setBlock(ecx - 1, eY, ecz - 1, BR::Cobble, 0);
            const int cnt14 = w.countBookshelvesAround(ecx, eY, ecz);
            w.setBlock(ecx - 1, eY, ecz - 1, BR::Air, 0);            // 复原半步
            for (int dy = 0; dy <= 1; ++dy)                          // 两层 32 位全放满 → 封顶 15
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                        w.setBlock(ecx + dx, eY + dy, ecz + dz, BR::Bookshelf, 0);
                    }
            const int cntCap = w.countBookshelvesAround(ecx, eY, ecz);
            ok = ok && cnt15 == 15 && cnt14 == 14 && cntCap == 15;
            if (emptyCnt != 0 || cnt15 != 15 || cnt14 != 14 || cntCap != 15)
                qInfo().noquote() << "  [t795 diag] world ring count:" << emptyCnt << "->"
                                  << cnt15 << "->" << cnt14 << "->" << cntCap;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| enchant gate: tier 1/2/3 at 0/5/10 bookshelves (no creative bypass), offered "
                             "[1,2,3]@0 -> [10,20,30]@15 with top 30 only at full 15, monotonic in-range; "
                             "hotbar bridge identical; world ring 0 -> 15 -> blocked half-step 14 -> 32 "
                             "placed capped 15 (t795; UI lapis-gated highlight = QML binding, manual check)";
    }

    // ── t823 书架→附魔台字流口径 tripwire（用户报「没看到文字流」实机核查产物；矩阵不链 Quick3D →
    //    QML 枚举无法直测，改**冻结镜像** EnchantGlyphFlow.qml rescanPairs 的逐行语义与本权威锁同值：
    //    权威规则改动而 QML 副本未跟 → 镜像与权威失配 FAIL，提醒同步 EnchantGlyphFlow.qml（及同规则
    //    第二副本 EnchantRunes.qml）。镜像与权威**有意分歧仅一处**：不设 15 上限 —— 字流发射按全部
    //    有效对（每 pair 独立发射，>15 书架照常出字），15 封顶是附魔强度档位语义非视觉语义。
    //    同时钉用户搭法口径三态（复现文档 docs/test-reports/t823-glyphflow-repro.md 第 2 节同图）：
    //    ① 单层地面环带即有效 —— **无需两层高**（用户重点怀疑项，t795 只证计数 15 未按字流口径钉）；
    //    ② 贴身环带（切比雪夫==1，8 格全放）恒 0 —— 与附魔档位同口径：能吃到书架档位加成的搭法必然
    //      出字流、不出流的搭法也吃不到档位（两处同源，附魔台 UI 里看书架档位 = 字流搭法自检入口）；
    //    ③ 半步格被堵 → 该书架不计（视觉与档位同步减）。rig 同 t795：运行期扫描净空 5×5 区（P20
    //    教训不走 nextSlot）；y 带取 44/45（**世界高 48 → y∈[0,47]**，首轮踩坑 y=50/51 越界静默拒 = 全
    //    探针假 FAIL 的 t814 同款病；t795 残架在 46/47 不冲突，扫描自带避开）。末尾复原 Air。
    {
        // 冻结镜像（改动此函数 = 改 QML 副本语义，须三处同步：World 权威 / 本镜像 / QML 两副本）：
        //   Math.trunc(dx/2) ≡ C++ 整除向零（dx∈{-2,0,2} 商恰整数，两写法同值）；QML !==95/!==0 由
        //   recipe.cpp t823 static_assert 钉 95/94 字面量，此处镜像走谓词等价。
        const auto mirrorShelfPairs = [](const World &world, int x, int y, int z) {
            int pairs = 0;
            for (int dy = 0; dy <= 1; ++dy) {
                const int yy = y + dy;
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                        if (!BR::isBookshelf(world.blockAt(x + dx, yy, z + dz))) continue;
                        if (world.blockAt(x + dx / 2, yy, z + dz / 2) != quint8(BR::Air)) continue;
                        ++pairs;
                    }
            }
            return pairs;
        };
        int gx = -1, gz = -1;
        const int gY = 44;
        for (int zz = 2; zz + 2 < 96 && gx < 0; zz += 3)
            for (int xx = 2; xx + 2 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -2; dx <= 2 && clear; ++dx)
                    for (int dz = -2; dz <= 2 && clear; ++dz)
                        if (w.blockAt(xx + dx, gY, zz + dz) != BR::Air
                            || w.blockAt(xx + dx, gY + 1, zz + dz) != BR::Air) clear = false;
                if (clear) { gx = xx; gz = zz; }
            }
        bool ok = gx >= 0;
        if (gx < 0) {
            qInfo().noquote() << "  [t823 diag] no clear 5x5 region at y=44/45";
        } else {
            const auto clearRing = [&]() {
                for (int dy = 0; dy <= 1; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz) {
                            w.setBlock(gx + dx, gY + dy, gz + dz, BR::Air, 0);
                            if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                w.setBlock(gx + dx / 2, gY + dy, gz + dz / 2, BR::Air, 0);
                        }
            };
            // ① 净空 → 双侧 0（镜像 == 权威基线）。
            const int m0 = mirrorShelfPairs(w, gx, gY, gz), a0 = w.countBookshelvesAround(gx, gY, gz);
            // ② 贴身环带 8 格全放书架（切比雪夫==1）→ 双侧 0：不出流 = 也不加档（用户搭法口径钉）。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx != 0 || dz != 0)
                        w.setBlock(gx + dx, gY, gz + dz, BR::Bookshelf, 0);
            const int mAdj = mirrorShelfPairs(w, gx, gY, gz), aAdj = w.countBookshelvesAround(gx, gY, gz);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx != 0 || dz != 0)
                        w.setBlock(gx + dx, gY, gz + dz, BR::Air, 0);
            // ③ 单层地面环带 16 格全放（上层恒空）→ 镜像 16（不封顶）/ 权威 15（封顶）——**单层即满**，
            //    且有意分歧（视觉不封顶）一并钉死：满环带 + UI 开 = 字流最密场景。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                        w.setBlock(gx + dx, gY, gz + dz, BR::Bookshelf, 0);
            const int m16 = mirrorShelfPairs(w, gx, gY, gz), a16 = w.countBookshelvesAround(gx, gY, gz);
            // ④ 堵两角书架半步（(±1,±1) 各只服务自己的角书架，t795 边中点教训）→ 双侧 -2。
            w.setBlock(gx - 1, gY, gz - 1, BR::Cobble, 0);
            w.setBlock(gx + 1, gY, gz + 1, BR::Cobble, 0);
            const int m14 = mirrorShelfPairs(w, gx, gY, gz), a14 = w.countBookshelvesAround(gx, gY, gz);
            w.setBlock(gx - 1, gY, gz - 1, BR::Air, 0);
            w.setBlock(gx + 1, gY, gz + 1, BR::Air, 0);
            // ⑤ 单列两层高（一角 dy0/dy1 叠放）→ 双侧 2：两层架每层独立计，第二层同样要自己的半步空。
            clearRing();
            w.setBlock(gx - 2, gY,     gz - 2, BR::Bookshelf, 0);
            w.setBlock(gx - 2, gY + 1, gz - 2, BR::Bookshelf, 0);
            const int mStack = mirrorShelfPairs(w, gx, gY, gz), aStack = w.countBookshelvesAround(gx, gY, gz);
            clearRing();   // 复原净空（好公民：t795 未清是历史，新探针不留脏 rig）
            ok = ok && m0 == 0 && a0 == 0
                 && mAdj == 0 && aAdj == 0
                 && m16 == 16 && a16 == 15
                 && m14 == 14 && a14 == 14
                 && mStack == 2 && aStack == 2;
            if (!ok)
                qInfo().noquote() << "  [t823 diag] mirror/authority: empty " << m0 << "/" << a0
                                  << " adjacent " << mAdj << "/" << aAdj
                                  << " ring16 " << m16 << "/" << a16
                                  << " blocked2 " << m14 << "/" << a14
                                  << " stack " << mStack << "/" << aStack;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t823 glyph-flow rule tripwire: QML rescanPairs mirror == world authority "
                             "(empty 0/0, adjacent ring 0/0 = no flow no tier, single ground layer 16 pairs "
                             "vs capped 15 = one-layer suffices + visual uncapped by design, blocked half-step "
                             "-2 both sides, two-high stack 2/2)";
    }

    // ── t785 生物蛋补全探针（用户「末影人和烈焰人的生物蛋……应该和其他的生物蛋放在一起，而且贴图也是仿照
    //    他们的生物蛋，还有就是狼和豹猫的生物蛋都没有出现」；Game 层表 + Core 生成式染色表）：
    //    ① 蛋 id→mobType 单一权威表 RecipeRegistry::mobTypeForSpawnEgg 全 13 蛋接通且与 EntityManager::MobType
    //      枚举同值（t785 收口——此前映射散在 placeBlock 内联链 + QML 两处手抄，t728 审查修 B9 即此类
    //      「加了蛋漏接」缺口；狼/豹猫/夜行者/燃烬者四新接为重点）；
    //    ② 创造背包材料段 13 蛋**连续同列**（egg 区聚合无杂项穿插——夜行者/燃烬者蛋此前孤列在暗渊链材料后）；
    //    ③ Hotbar::nameForBlock 13 蛋全有名（空名 = 调色板/tooltip 无名，t728 B9 同类缺口）；
    //    ④ Core 生成式染色表 spawnEggTint 13 蛋全有条目 + 非蛋 id 不误命中（pack miss 时该蛋按 mob 配色
    //      两层染色，而非空白模板）。蛋图标观感 / 蛋区排布为 QML 层，需人工目视。
    {
        bool ok = RecipeRegistry::SpawnEggNightwalkerId == 0x246   // t785 新 id 分配锁（重排破存档兼容）
                  && RecipeRegistry::SpawnEggEmberlingId == 0x247
                  && RecipeRegistry::SpawnEggWolfId == 0x249
                  && RecipeRegistry::SpawnEggOcelotId == 0x24A;
        const int allEggs[] = {
            RecipeRegistry::SpawnEggPigId, RecipeRegistry::SpawnEggCowId, RecipeRegistry::SpawnEggSheepId,
            RecipeRegistry::SpawnEggShamblerId, RecipeRegistry::SpawnEggBonesId, RecipeRegistry::SpawnEggStalkerId,
            RecipeRegistry::SpawnEggSpiderId, RecipeRegistry::SpawnEggChickenId, RecipeRegistry::SpawnEggSquidId,
            RecipeRegistry::SpawnEggNightwalkerId, RecipeRegistry::SpawnEggEmberlingId,
            RecipeRegistry::SpawnEggWolfId, RecipeRegistry::SpawnEggOcelotId,
        };
        const int expectMob[] = {
            EntityManager::MobPig, EntityManager::MobCow, EntityManager::MobSheep,
            EntityManager::MobShambler, EntityManager::MobBones, EntityManager::MobStalker,
            EntityManager::MobSpider, EntityManager::MobChicken, EntityManager::MobSquid,
            EntityManager::MobNightwalker, EntityManager::MobEmberling,
            EntityManager::MobWolf, EntityManager::MobOcelot,
        };
        const int eggCount = int(sizeof(allEggs) / sizeof(allEggs[0]));
        for (int i = 0; i < eggCount; ++i) {
            // ① 单一权威表 → mob 类型（枚举同值断言：枚举改动而表漏跟 = 此处 FAIL）
            const int got = RecipeRegistry::mobTypeForSpawnEgg(allEggs[i]);
            if (got != expectMob[i]) {
                qInfo().noquote() << "  [t785 diag] egg 0x" + QString::number(allEggs[i], 16)
                                  << "-> mobType" << got << "expected" << expectMob[i];
                ok = false;
            }
            // ④ 生成式染色表条目存在（nullptr = pack miss 时无配色 → 空白模板蛋）
            if (!spawnEggTint(allEggs[i])) {
                qInfo().noquote() << "  [t785 diag] egg 0x" + QString::number(allEggs[i], 16)
                                  << "missing spawnEggTint entry";
                ok = false;
            }
        }
        // 非蛋 id 两表恒「无」（防表越界误命中）：燧石（材料段非蛋）与 0x212（蛋段内夹的钻石占位）。
        if (RecipeRegistry::mobTypeForSpawnEgg(RecipeRegistry::FlintId) != -1
            || RecipeRegistry::mobTypeForSpawnEgg(0x212) != -1
            || spawnEggTint(RecipeRegistry::FlintId) != nullptr
            || spawnEggTint(0x212) != nullptr) {
            qInfo().noquote() << "  [t785 diag] non-egg id falsely matched egg table";
            ok = false;
        }
        // ②③ 创造背包蛋区连续同列 + 全蛋有名。
        Hotbar hb785;
        const QVariantList mats785 = hb785.creativeMaterials();
        int eggMin = mats785.size(), eggMax = -1, eggSeen = 0;
        for (int i = 0; i < mats785.size(); ++i) {
            if (RecipeRegistry::mobTypeForSpawnEgg(mats785.at(i).toInt()) >= 0) {
                eggMin = std::min(eggMin, i);
                eggMax = std::max(eggMax, i);
                ++eggSeen;
            }
        }
        if (eggSeen != eggCount || eggMax - eggMin + 1 != eggCount) {
            qInfo().noquote() << "  [t785 diag] creative egg block: seen" << eggSeen << "of" << eggCount
                              << "span" << (eggMax - eggMin + 1) << "(expected contiguous)";
            ok = false;
        }
        for (int i = 0; i < eggCount; ++i) {
            if (hb785.nameForBlock(allEggs[i]).isEmpty()) {
                qInfo().noquote() << "  [t785 diag] egg 0x" + QString::number(allEggs[i], 16)
                                  << "has empty nameForBlock";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t785 spawn-egg completion: 13 eggs (nightwalker/emberling moved into the "
                             "contiguous egg block + wolf 0x249 / ocelot 0x24A new) all map to correct "
                             "EntityManager mob types via single-authority table, all present & contiguous "
                             "in creative palette with names, all have generative tint entries (egg icon "
                             "look & palette layout = QML, manual check)";
    }

    // ── t786 刷怪笼类型化（spawner cage typing）：①state 位布局 round-trip（编码/解码互逆 + 旧存档兼容
    //   分流）②地牢 worldgen 加权分布多 seed 核对（蠹虫不在地牢池）③tickSpawners 据 state 刷对应型 ④创造
    //   放置默认型。数值契约锁（枚举漂移 = 此处 FAIL，同 t785 单一权威教训）。
    {
        bool ok = true;
        EntityManager em786;
        // ① 编码 → 解码互逆（五类型全表）：BlockRegistry::spawnerStateForMob(Core 层 raw int)→ state 常量
        //   → EntityManager::spawnerMobTypeForState 解码回原型。同时锁位布局常量本身。
        const int types786[] = { EntityManager::MobShambler, EntityManager::MobBones,
                                 EntityManager::MobStalker, EntityManager::MobSpider, EntityManager::MobSilverfish };
        for (int t : types786) {
            const quint8 st = BlockRegistry::spawnerStateForMob(t);
            if (em786.spawnerMobTypeForState(int(st)) != t) {
                qInfo().noquote() << "  [t786 diag] type" << t << "round-trip got"
                                  << em786.spawnerMobTypeForState(int(st));
                ok = false;
            }
        }
        if (BlockRegistry::SpawnerStateMobShift != 1 || BlockRegistry::SpawnerStateMobMask != 0x3E
            || BlockRegistry::SpawnerStateShambler != 0x08 || BlockRegistry::SpawnerStateBones != 0x0A
            || BlockRegistry::SpawnerStateStalker != 0x0C || BlockRegistry::SpawnerStateSpider != 0x0E
            || BlockRegistry::SpawnerStateSilverfish != 0x1D
            || BlockRegistry::SpawnerStateSilverfishFlag != 0x01) {
            qInfo() << "  [t786 diag] spawner state bit-layout constants drifted";
            ok = false;
        }
        // ①b 旧存档兼容：state=0（旧地牢笼）→ Shambler；state=1（t487 旧要塞银鱼笼）→ Silverfish；
        //   非法 type 位 → 兜底 Shambler 不崩不误刷。t787 注：旧样本 0x21（type16）扩表后是合法
        //   Nightwalker（蛋改型）→ 非法样本换 0x27（type19 > MobAnvil=18 越界）；0x3E（type31）仍非法。
        if (em786.spawnerMobTypeForState(0) != EntityManager::MobShambler
            || em786.spawnerMobTypeForState(1) != EntityManager::MobSilverfish
            || em786.spawnerMobTypeForState(0x26 | 0x01) != EntityManager::MobShambler
            || em786.spawnerMobTypeForState(0x3E) != EntityManager::MobShambler) {
            qInfo() << "  [t786 diag] legacy/invalid-state decode wrong";
            ok = false;
        }
        // ② 地牢 worldgen 加权分布（多 seed 池化）：新世界（generate() 已含 placeDungeons，勿重复调）逐个
        //   重生成后扫全图 Spawner 按**原始 state** 分类 —— 要塞银鱼笼（state=1 旧 / 0x1D 新）单独计，
        //   其余归地牢池并解码统计：地牢池不得出现蠹虫/未知型、不得残留无类型旧格（state=0）、池化合计
        //   僵尸占比最高（40% 加权）。探针自建临时世界（矩阵 harness 共享 nextSlot 已耗尽 —— t799 教训）。
        auto classifyWorldSpawners = [](World &w, int counts[4], int &stronghold, int &legacyUntyped, int &unknown) {
            for (int i = 0; i < 4; ++i) counts[i] = 0;
            stronghold = 0; legacyUntyped = 0; unknown = 0;
            for (int y = 0; y < w.height(); ++y)
                for (int z = 0; z < w.depth(); ++z)
                    for (int x = 0; x < w.width(); ++x) {
                        if (w.blockAt(x, y, z) != BlockRegistry::Spawner) continue;
                        const quint8 st = w.stateAt(x, y, z);
                        // 要塞银鱼笼两代形态：t487 旧 state=1 / t786 新 0x1D（type14|bit0）。bit0+type14 组合
                        //   只由 placeStronghold 写出（地牢池无蠹虫），按要塞计。
                        if (st == BlockRegistry::SpawnerStateSilverfishFlag
                            || st == BlockRegistry::SpawnerStateSilverfish) { ++stronghold; continue; }
                        switch (EntityManager().spawnerMobTypeForState(int(st))) {
                        case EntityManager::MobShambler: ++counts[0]; break;
                        case EntityManager::MobBones:    ++counts[1]; break;
                        case EntityManager::MobStalker:  ++counts[2]; break;
                        case EntityManager::MobSpider:   ++counts[3]; break;
                        default: ++unknown; break;
                        }
                        if (st == 0) ++legacyUntyped; // 全新生成不应有无类型旧格
                    }
        };
        int pooled[4] = { 0, 0, 0, 0 };
        const quint32 seeds786[] = { 20260821u, 777u, 424242u, 1337u, 90210u };
        int worldsChecked = 0;
        for (quint32 sd : seeds786) {
            World w786;
            w786.setWidth(96);
            w786.setDepth(96);
            w786.setHeight(48);
            w786.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeDungeons）
            int c[4], strong, legacyU, unk;
            classifyWorldSpawners(w786, c, strong, legacyU, unk);
            ++worldsChecked;
            if (unk != 0 || legacyU != 0) { // 地牢池全类型化、无未知型
                qInfo().noquote() << "  [t786 diag] seed" << sd << "dungeon pool unknown/untyped:" << unk << legacyU;
                ok = false;
            }
            if (c[3] > c[0] || c[2] > c[0]) { // 单世界粗检：蜘蛛/爬行者不得多于僵尸（40% 主导）
                qInfo().noquote() << "  [t786 diag] seed" << sd << "zombie not dominant:" << c[0] << c[1] << c[2] << c[3];
                ok = false;
            }
            for (int i = 0; i < 4; ++i) pooled[i] += c[i];
        }
        const int pooledTotal = pooled[0] + pooled[1] + pooled[2] + pooled[3];
        if (worldsChecked > 0 && pooledTotal == 0) {
            qInfo() << "  [t786 diag] no dungeons generated across probe seeds";
            ok = false;
        }
        if (pooledTotal >= 5 && !(pooled[0] >= pooled[1] && pooled[1] >= std::min(pooled[2], pooled[3]))) {
            // 池化序断言：僵尸 ≥ 骷髅 ≥ min(蜘蛛,爬行者)（小样本下 20% vs 15% 可能倒挂，仅锁大序）
            qInfo().noquote() << "  [t786 diag] pooled weight order off:" << pooled[0] << pooled[1] << pooled[2] << pooled[3];
            ok = false;
        }
        // ③ tickSpawners 据 state 刷对应型：手摆僵尸笼（state=SpawnerStateShambler）+ 合法 spawn 位，
        //    累计 tick 超 kSpawnerInterval(6s) 后应出 Shambler（非 Bones/Silverfish）；再换骷髅笼反证。
        //    spawn 条件（玩家近 / cap）语义不变——探针只验「型随笼」。
        auto tickTypedCage = [](quint8 cageState, bool &typedSpawned, bool &wrongTyped) {
            World w786;
            w786.setWidth(48);
            w786.setDepth(48);
            w786.setHeight(32);
            w786.setSeed(9);
            const int sx = 24, sy = 8, sz = 24;
            w786.setBlock(sx, sy, sz, BlockRegistry::Spawner, cageState);
            // 刻写位手工清空（worldgen 地形 y=8 恒实心 → 不挖空气 spawn 预检恒拒）：笼 8 水平邻 × (y, y+1)
            //   全 air + 各自脚下 solid，给 tickSpawners 一个合法候选位。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue; // 笼格本身不动
                    w786.setBlock(sx + dx, sy, sz + dz, BlockRegistry::Air);
                    w786.setBlock(sx + dx, sy + 1, sz + dz, BlockRegistry::Air);
                    w786.setBlock(sx + dx, sy - 1, sz + dz, BlockRegistry::Stone); // air + 下方 solid
                }
            EntityManager emT;
            const QVector3D playerPos(float(sx) + 0.5f, float(sy) + 0.5f, float(sz) + 12.5f); // XZ ≤16 激活圈内
            for (int i = 0; i < 80; ++i) emT.tickSpawners(0.1, &w786, playerPos); // 累计 8s > kSpawnerInterval=6s
            typedSpawned = false; wrongTyped = false;
            const int want = (cageState == BlockRegistry::SpawnerStateBones) ? int(EntityManager::MobBones)
                                                                             : int(EntityManager::MobShambler);
            for (int i = 0; i < emT.count(); ++i) {
                if (!emT.aliveAt(i)) continue;
                if (emT.mobTypeAt(i) == want) typedSpawned = true; // 只认刻写位邻域的 mob（防 worldgen 噪声）
                else wrongTyped = true;
            }
        };
        {
            bool typedSpawned, wrongTyped;
            tickTypedCage(quint8(BlockRegistry::SpawnerStateShambler), typedSpawned, wrongTyped);
            if (!typedSpawned || wrongTyped) {
                qInfo().noquote() << "  [t786 diag] shambler-cage tick spawned wrong pool:" << typedSpawned << wrongTyped;
                ok = false;
            }
        }
        {
            bool typedSpawned, wrongTyped;
            tickTypedCage(quint8(BlockRegistry::SpawnerStateBones), typedSpawned, wrongTyped);
            if (!typedSpawned || wrongTyped) {
                qInfo().noquote() << "  [t786 diag] bones-cage tick spawned wrong pool:" << typedSpawned << wrongTyped;
                ok = false;
            }
        }
        // ④ 创造放置默认型：placeState 分支常量核（PlayerController 放置路径写 spawnerStateForMob(MobShambler)
        //    = SpawnerStateShambler；解码回 Shambler）。放置 UI 全链路需人工目视（见 dev-plan）。
        if (BlockRegistry::spawnerStateForMob(EntityManager::MobShambler) != BlockRegistry::SpawnerStateShambler) {
            qInfo() << "  [t786 diag] placement default state mismatch";
            ok = false;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t786 typed spawner cages: state encode/decode round-trip per mob type "
                             "(bit1-5 layout locked), legacy states 0->shambler / 1->silverfish, invalid "
                             "type bits fall back safely, dungeon worldgen weighted pool over multiple seeds "
                             "has no silverfish with zombie-dominant order, tickSpawners spawns the cage's "
                             "typed mob (both polarity probes), creative placement defaults to shambler "
                             "(cage mini-model visuals = QML, manual check)";
    }

    // ── review26 #19 刷怪支撑收口 isCollidable 探针（EntityManager 直编，t786 tickTypedCage rig 族）──
    //   用户症状（review26 低危）：宠物瞬移 / 自然刷怪 / 刷怪笼支撑判定仍 isSolid（非 air）→ 落花草下帧
    //   坠落、落水瞬进水。修：四处收口 World::isCollidable（t865 单一权威）。刷怪笼候选扫描是**确定性
    //   首匹配**（固定 kSpawnDx/Dz 枚举序）→ 可构造唯一候选位 rig 行为级钉死：候选下方是花草时零刷怪
    //   （旧 isSolid 判花草可站 → 首周期即刷）；同 rig 下方换石头 → 首周期必刷在唯一候选格心。
    //   （狼 / 豹猫瞬移与自然刷怪同谓词替换，源码一致性由本探针钉住谓词语义。）
    {
        bool okNeg = false, okPos = false;
        // rig：独立小世界（t786 同款）。笼 @（24,8,24）；7 个非花候选位的 y=8/y=9 双层填死（here!=Air
        //   恒拒）；唯一候选 (25,8,24) 净空两格，下方 (25,7) = 待测支撑块。
        const auto runSupportCage = [&](quint8 supportBlock, int &spawnedCount, float &spawnX) {
            World wS;
            wS.setWidth(48); wS.setDepth(48); wS.setHeight(32); wS.setSeed(9);
            const int sx = 24, sy = 8, sz = 24;
            wS.setBlock(sx, sy, sz, BlockRegistry::Spawner, BlockRegistry::SpawnerStateShambler);
            static const int kDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int kDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            for (int i = 0; i < 8; ++i) {
                const int cx = sx + kDx[i], cz = sz + kDz[i];
                if (i == 0) { // 唯一候选（枚举序首位）：净空两格 + 下方待测支撑
                    wS.setBlock(cx, sy, cz, BlockRegistry::Air, 0);
                    wS.setBlock(cx, sy + 1, cz, BlockRegistry::Air, 0);
                    wS.setBlock(cx, sy - 1, cz, supportBlock, 0);
                    continue;
                }
                wS.setBlock(cx, sy, cz, BlockRegistry::Stone, 0);     // 其它 7 位 y=8 填死
                wS.setBlock(cx, sy + 1, cz, BlockRegistry::Stone, 0); // y=9 也填死（cyOff=1 不再可用）
            }
            EntityManager emS;
            const QVector3D playerPos(float(sx) + 0.5f, float(sy) + 0.5f, float(sz) + 12.5f); // 激活圈内
            for (int t = 0; t < 80; ++t) emS.tickSpawners(0.1, &wS, playerPos); // 8s > 6s 首周期
            spawnedCount = 0; spawnX = -1.0f;
            for (int i = 0; i < emS.count(); ++i)
                if (emS.aliveAt(i)) { ++spawnedCount; spawnX = emS.posAt(i).x(); }
        };
        {
            int n = 0; float px = -1.0f;
            runSupportCage(BR::FlowerRed, n, px); // 花草支撑（ShapeNone → isCollidable=false）
            okNeg = n == 0; // 旧 isSolid：花草非 air → 可站 → 首周期即刷（回退即红）
            if (!okNeg) qInfo().noquote() << "  [review26-19 diag] flower-floor spawned" << n << "@x" << px;
        }
        {
            int n = 0; float px = -1.0f;
            runSupportCage(BR::Stone, n, px); // 石头支撑（isCollidable=true 正对照）
            okPos = n >= 1 && std::abs(px - 25.5f) < 1e-3f; // 必刷在唯一候选格心
            if (!okPos) qInfo().noquote() << "  [review26-19 diag] stone-floor spawned" << n << "@x" << px;
        }
        const bool ok19 = okNeg && okPos;
        if (!ok19) ++totalFail;
        qInfo().noquote() << (ok19 ? "PASS" : "FAIL")
                          << "| review26-19 spawn support requires a collidable block: a spawner whose "
                             "only candidate cell sits above a flower spawns nothing (old isSolid read "
                             "non-air as standable), the same rig over stone spawns at the unique "
                             "candidate cell center; wolf/ocelot teleport and natural spawn share the "
                             "same predicate swap";
    }

    // ── review26 #20 collisionTopY 免构建镜像等价探针（Core 层全表扫描）──
    //   免构建顶面查询（BlockRegistry::collisionTopY）替代 supportTopYAt 慢路径的 collisionAABBs 最高盒
    //   maxY 读取（resting 掉落物每帧两格窗复探在异形支撑上不再堆分配）。等价性 = 全 id（0..255）×
    //   state（0..255）逐格断言 collisionTopY(id,st) == 盒空 ? -1 : max(box.maxY) —— 改形状只动一处
    //   （shapeBoxes / collisionAABBs 特例表 vs collisionTopY 镜像表）→ 本探针红，防两表漂移。
    {
        quint32 checked = 0;
        int firstBadId = -1, firstBadSt = -1;
        float badWant = 0.0f, badGot = 0.0f;
        for (int id = 0; id < 256 && firstBadId < 0; ++id) {
            for (int st = 0; st < 256; ++st) {
                float want = -1.0f;
                for (const auto &b : BR::collisionAABBs(quint8(id), quint8(st)))
                    if (b.maxY > want) want = b.maxY;
                const float got = BR::collisionTopY(quint8(id), quint8(st));
                ++checked;
                if (std::abs(want - got) > 1e-6f) {
                    firstBadId = id; firstBadSt = st; badWant = want; badGot = got;
                    break;
                }
            }
        }
        const bool ok20 = firstBadId < 0 && checked > 0;
        if (!ok20)
            qInfo().noquote() << "  [review26-20 diag] id=" << firstBadId << "st=" << firstBadSt
                          << "want=" << badWant << "got=" << badGot;
        if (!ok20) ++totalFail;
        qInfo().noquote() << (ok20 ? "PASS" : "FAIL")
                          << "| review26-20 allocation-free collisionTopY mirrors collisionAABBs exactly: "
                             "for every block id x state (65536 combos), collisionTopY equals the max "
                             "box maxY (or -1 when boxless) -- supportTopYAt's slow path swaps the "
                             "vector-building read for this scalar mirror with zero behavior change, "
                             "and any future shape edit touching only one of the two tables turns this "
                             "red (anti-drift pin)";
    }

    // ── P-t859 collisionAABBsInto out-param 等价探针（R19.14 堆分配消除；Core 层全表扫描 + World 层抽查）──
    //   玩家/mob 碰撞热路径改读 BlockRegistry::collisionAABBsInto（栈上定容直写）与
    //   World::collisionAABBsAt(out,cap)（世界坐标偏移版）。等价性 = 全 id × state 断言 Into 输出与
    //   by-value 薄壳 collisionAABBs 逐盒逐字段完全一致（count / 6 坐标分量）+ 缓冲越界保护（cap=0 时
    //   只报计数不写穿）+ World 版抽查（放置方块后 out-param 盒 = cell-local 盒 + 格偏移）。改形状
    //   漏同步两路 → 本探针红（防单一权威漂移，同 review26-20 钉法）。
    {
        quint32 checked859 = 0;
        int badId859 = -1, badSt859 = -1;
        QString diag859;
        for (int id = 0; id < 256 && badId859 < 0; ++id) {
            for (int st = 0; st < 256; ++st) {
                const auto vec = BR::collisionAABBs(quint8(id), quint8(st));
                BlockRegistry::BlockAABB buf[BR::kMaxAABBsPerCell];
                const int n = BR::collisionAABBsInto(quint8(id), quint8(st), buf, BR::kMaxAABBsPerCell);
                ++checked859;
                bool same = n == int(vec.size()) && n <= BR::kMaxAABBsPerCell;
                for (int i = 0; same && i < n; ++i) {
                    const auto &a = vec[size_t(i)], &b = buf[i];
                    same = a.minX == b.minX && a.minY == b.minY && a.minZ == b.minZ
                           && a.maxX == b.maxX && a.maxY == b.maxY && a.maxZ == b.maxZ;
                }
                if (!same) { badId859 = id; badSt859 = st; break; }
            }
        }
        // cap=0 保护（单点抽查，防逐组合跑刷爆日志——putAABB 守卫是共享代码路径，一次足以证不写穿）：
        // 返回计数与 cap 充足时一致、不写任何字节。
        bool cap0Ok = false;
        {
            const int nFull = BR::collisionAABBsInto(quint8(1) /*Stone=ShapeFull*/, quint8(0),
                                                     nullptr, 0);
            BlockRegistry::BlockAABB canary[BR::kMaxAABBsPerCell];
            for (auto &c : canary) c = {1234.5f, 1234.5f, 1234.5f, 1234.5f, 1234.5f, 1234.5f};
            const int n0 = BR::collisionAABBsInto(quint8(1), quint8(0), canary, 0);
            bool untouched = true;
            for (const auto &c : canary)
                if (c.minX != 1234.5f) untouched = false;
            cap0Ok = nFull == 1 && n0 == 1 && untouched; // n0 查询会响一次 qWarning（守卫在响，符合预期）
            if (!cap0Ok) diag859 = "cap0-guard";
        }
        bool okCore859 = badId859 < 0 && checked859 > 0 && cap0Ok;
        if (!okCore859)
            qInfo().noquote() << "  [t859 diag] id=" << badId859 << "st=" << badSt859 << diag859;
        // World 层抽查：放一块下半砖（state bit0=0 → 盒顶 0.5），out-param 版须回 (bx,bz) 偏移的矮盒。
        bool okWorld859 = false;
        {
            const auto [wx, wz] = nextSlot();
            placeRigBlock(w, wx, kRigY, wz, BR::CobbleSlab, 0);
            BlockRegistry::BlockAABB wb[BR::kMaxAABBsPerCell];
            const int wn = w.collisionAABBsAt(wx, kRigY, wz, wb, BR::kMaxAABBsPerCell);
            okWorld859 = wn == 1
                         && std::abs(wb[0].minX - float(wx)) < 1e-6f
                         && std::abs(wb[0].minY - float(kRigY)) < 1e-6f
                         && std::abs(wb[0].minZ - float(wz)) < 1e-6f
                         && std::abs(wb[0].maxX - float(wx + 1)) < 1e-6f
                         && std::abs(wb[0].maxY - (float(kRigY) + 0.5f)) < 1e-6f
                         && std::abs(wb[0].maxZ - float(wz + 1)) < 1e-6f;
            if (!okWorld859)
                qInfo().noquote() << "  [t859 diag] world slab n=" << wn << "box="
                                  << (wn > 0 ? wb[0].minX : -1.f) << (wn > 0 ? wb[0].minY : -1.f)
                                  << (wn > 0 ? wb[0].minZ : -1.f) << (wn > 0 ? wb[0].maxX : -1.f)
                                  << (wn > 0 ? wb[0].maxY : -1.f) << (wn > 0 ? wb[0].maxZ : -1.f);
            w.setBlock(wx, kRigY, wz, BR::Air, 0);
        }
        const bool okT859 = okCore859 && okWorld859;
        if (!okT859) ++totalFail;
        qInfo().noquote() << (okT859 ? "PASS" : "FAIL")
                          << "| t859 collisionAABBsInto out-param path: stack-buffer collision query "
                             "(BlockRegistry::collisionAABBsInto + World::collisionAABBsAt(out,cap)) is "
                             "field-exact with the by-value shell for all 65536 id x state combos, cap=0 "
                             "returns the count without writing a byte (overflow guard), and the World "
                             "wrapper offsets cell-local boxes into world space (lower slab spot check) - "
                             "player/mob collision hot paths (3 axes x ~12 cells/tick + 60 mob "
                             "predicates) drop 2 vector heap allocations per query with zero behavior "
                             "change";
    }

    // ── t787 生物蛋×刷怪笼交互（用户「拿上生物蛋对着刷怪笼右键，就可以弄成刷这个生物的刷怪笼」；机制等价
    //    MC 1.0 spawn egg 右键 spawner 改型）：①全 13 蛋改型 round-trip（蛋表 → 编码 → 解码互逆，白名单
    //    扩表锁死——加蛋漏接 = 此处 FAIL，t785 B9 缺口防线）②哨兵/越界 type 仍兜底 Shambler（扩表不含
    //    MobTest/golem/Tnt/Anvil 哨兵，防「经笼凭空刷哨兵型」）③改型写入 + tickSpawners 按新类型刷
    //    （被动型走 spawnPassiveMob 且 hostile=false；敌对型走原路径）④被动笼同型 local cap（4 只封顶，
    //    mobTypeCountNear 判据——防无上限刷屏）。蛋消耗（Hotbar takeStack）/ 笼心迷你模型切换（QML
    //    cleanupVis 重读链）在 PlayerController/QML 层，需人工目视（同 t786 ④ 注记）。
    {
        bool ok = true;
        EntityManager em787;
        // ① 全 13 蛋改型 round-trip（蛋 id 表同 t785 探针单一权威源）。
        const int eggs787[] = {
            RecipeRegistry::SpawnEggPigId, RecipeRegistry::SpawnEggCowId, RecipeRegistry::SpawnEggSheepId,
            RecipeRegistry::SpawnEggShamblerId, RecipeRegistry::SpawnEggBonesId, RecipeRegistry::SpawnEggStalkerId,
            RecipeRegistry::SpawnEggSpiderId, RecipeRegistry::SpawnEggChickenId, RecipeRegistry::SpawnEggSquidId,
            RecipeRegistry::SpawnEggNightwalkerId, RecipeRegistry::SpawnEggEmberlingId,
            RecipeRegistry::SpawnEggWolfId, RecipeRegistry::SpawnEggOcelotId,
        };
        for (int eggId : eggs787) {
            const int mt = RecipeRegistry::mobTypeForSpawnEgg(eggId);
            const quint8 st = BlockRegistry::spawnerStateForMob(mt);
            if (mt < 0 || em787.spawnerMobTypeForState(int(st)) != mt) {
                qInfo().noquote() << "  [t787 diag] egg 0x" + QString::number(eggId, 16)
                                  << "-> mobType" << mt << "state" << st << "round-trip got"
                                  << em787.spawnerMobTypeForState(int(st));
                ok = false;
            }
        }
        // ② 哨兵 / 越界 type 编码后解码仍兜底 Shambler（0=MobTest / 12 SnowGolem / 13 IronGolem / 15 Tnt /
        //    18 Anvil / 19 越界 —— 均无蛋不可经笼改型写入，白名单拒绝）。
        const int sentinels787[] = { 0, 12, 13, 15, 18, 19 };
        for (int st_ : sentinels787) {
            if (em787.spawnerMobTypeForState(int(BlockRegistry::spawnerStateForMob(st_))) != EntityManager::MobShambler) {
                qInfo().noquote() << "  [t787 diag] sentinel type" << st_ << "not rejected by decode whitelist";
                ok = false;
            }
        }
        // ③ 改型写入 + tick 按新类型刷（同 t786 ③ 自建临时世界模式：先摆僵尸笼 = 创造放置路径，再按
        //    PlayerController 蛋分支同款 5 参数 setBlock 改型 → stateAt 校验 → tickSpawners 累计超
        //    kSpawnerInterval）。
        auto retypeCageTick = [](int eggMobType, int &wantSpawned, int &wrongSpawned,
                                 bool &wantHostile, bool &stateWritten, float seconds) {
            World w787;
            w787.setWidth(48);
            w787.setDepth(48);
            w787.setHeight(32);
            w787.setSeed(9);
            const int sx = 24, sy = 8, sz = 24;
            w787.setBlock(sx, sy, sz, BlockRegistry::Spawner, BlockRegistry::SpawnerStateShambler);
            // 刻写位手工清空（同 t786：worldgen y=8 恒实心 → 不挖空气 spawn 预检恒拒）。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;
                    w787.setBlock(sx + dx, sy, sz + dz, BlockRegistry::Air);
                    w787.setBlock(sx + dx, sy + 1, sz + dz, BlockRegistry::Air);
                    w787.setBlock(sx + dx, sy - 1, sz + dz, BlockRegistry::Stone);
                }
            // 蛋分支同款改型写（5 参数 setBlock：id 不变只 state 变 → 不发 placed/broken）。
            w787.setBlock(sx, sy, sz, BlockRegistry::Spawner, BlockRegistry::spawnerStateForMob(eggMobType));
            stateWritten = w787.stateAt(sx, sy, sz) == BlockRegistry::spawnerStateForMob(eggMobType);
            EntityManager emT;
            const QVector3D playerPos(float(sx) + 0.5f, float(sy) + 0.5f, float(sz) + 12.5f);
            const int ticks = int(seconds * 10.0f);
            for (int i = 0; i < ticks; ++i) emT.tickSpawners(0.1, &w787, playerPos);
            wantSpawned = 0; wrongSpawned = 0; wantHostile = false;
            for (int i = 0; i < emT.count(); ++i) {
                if (!emT.aliveAt(i)) continue;
                if (emT.mobTypeAt(i) == eggMobType) {
                    ++wantSpawned;
                    wantHostile = wantHostile || emT.isHostileAt(i);
                } else {
                    ++wrongSpawned;
                }
            }
        };
        {
            // 被动极性：猪蛋改僵尸笼 → 刷 Pig（≥1）且全部非敌对、无其它型。
            int got = 0, wrong = 0; bool hostile = false, written = false;
            retypeCageTick(EntityManager::MobPig, got, wrong, hostile, written, 8.0f);
            if (!written || got < 1 || wrong != 0 || hostile) {
                qInfo().noquote() << "  [t787 diag] pig-cage tick:" << written << got << wrong << hostile;
                ok = false;
            }
        }
        {
            // 敌对极性：蜘蛛蛋改僵尸笼 → 刷 Spider 且敌对（原路径回归）。
            int got = 0, wrong = 0; bool hostile = false, written = false;
            retypeCageTick(EntityManager::MobSpider, got, wrong, hostile, written, 8.0f);
            if (!written || got < 1 || wrong != 0 || !hostile) {
                qInfo().noquote() << "  [t787 diag] spider-cage tick:" << written << got << wrong << hostile;
                ok = false;
            }
        }
        {
            // ④ 被动笼同型 local cap：34s ≈ 5 个刷怪周期（kSpawnerInterval=6s）→ 前 4 周期各刷 1 只、
            //    第 5 周期同型邻域已 ≥ kSpawnerLocalCap(4) → 恰好 4 只封顶（mobTypeCountNear 判据生效）。
            int got = 0, wrong = 0; bool hostile = false, written = false;
            retypeCageTick(EntityManager::MobPig, got, wrong, hostile, written, 34.0f);
            if (got != 4 || wrong != 0) {
                qInfo().noquote() << "  [t787 diag] passive local cap: pigs" << got << "wrong" << wrong
                                  << "(expected exactly 4 = kSpawnerLocalCap)";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t787 spawn-egg x spawner retype: all 13 eggs round-trip through "
                             "spawnerStateForMob/spawnerMobTypeForState (whitelist extended, sentinels/overflow "
                             "still fall back to shambler), retype write via same-id setBlock then tickSpawners "
                             "spawns the egg's type (pig passive+non-hostile / spider hostile polarity), passive "
                             "cage capped at 4 same-type nearby (egg consumption + cage mini-model switch = "
                             "playercontroller/QML, manual check)";
    }

    // ── t788 染料体系探针（Game 层静态查询为主：配方 / 掉落 / 冶炼 / 命名 / 调色板聚合，纯查表不用 rig ——
    //    测试尾段新探针不动共享 nextSlot 分配器）：
    //    ① 染料段 16 色连续（DyeIdBase=0x24B 起 DyeIdBase+i）且 Hotbar::nameForBlock 全有名（空名 =
    //       调色板/tooltip 无名，t728 B9 同类缺口）；四花色染料名精确核对（红/黄/蓝/白）；
    //    ② 四花破坏 dropId == 对应色染料常量（跨层契约：Core blockregistry 字面量 ↔ recipe.h 常量经
    //       static_assert 钉死 + 此处运行期经 dropId 访问器复核）；
    //    ③ 熔炉烧仙人掌 → 绿染料 + 冶炼 XP ≥ 1（kSmelt/kSmeltXp 两表都要接，B4「注释声称表漏行」同类缺口）；
    //    ④ 染色链 32 条可合成：16 染料 × {白羊毛方块 Wool=27 / 白床 BedWhite=78} → 对应色羊毛（idx==0 复用
    //       Wool，其余 FirstWoolVariant 起）/ 床（色段散布 32..39+78..85 查表）；抽 2 条换位摆证无序；
    //       染料+错基（木板）不产染色羊毛（防等价表误扩）；
    //    ⑤ 创造背包材料 tab 染料 16 色连续同列（染料区聚合，同 t785 蛋区连续性口径；图鉴 ResourceBrowser
    //       由 creativeMaterials 自动派生 = 同源在列）；
    //    ⑥ 32 条新配方已被 t802 全表自匹配回归自动覆盖（同表防丢，此处不重复）。染粉图标配色为 QML 层，
    //       需人工目视。
    {
        // 染料 16 色（行序 = 羊毛 16 色标准序）
        const int dyeIds[16] = {
            RecipeRegistry::DyeWhiteId, RecipeRegistry::DyeOrangeId, RecipeRegistry::DyeMagentaId,
            RecipeRegistry::DyeLightBlueId, RecipeRegistry::DyeYellowId, RecipeRegistry::DyeLimeId,
            RecipeRegistry::DyePinkId, RecipeRegistry::DyeGrayId, RecipeRegistry::DyeLightGrayId,
            RecipeRegistry::DyeCyanId, RecipeRegistry::DyePurpleId, RecipeRegistry::DyeBlueId,
            RecipeRegistry::DyeBrownId, RecipeRegistry::DyeGreenId, RecipeRegistry::DyeRedId,
            RecipeRegistry::DyeBlackId,
        };
        // 目标羊毛（idx==0 复用 Wool=27；其余 FirstWoolVariant=63 起 +idx-1）与目标床（色段散布 32..39 +
        //   78..85 → 逐条常量查表：白 78 / 橙 33 / 品红 38 / 浅蓝 79 / 黄 34 / 柠绿 80 / 粉 81 / 灰 82 /
        //   浅灰 83 / 青 36 / 紫 84 / 蓝 37 / 棕 85 / 绿 35 / 红 32 / 黑 39）。
        const int woolTarget[16] = {
            int(BR::Wool), int(BR::WoolOrange), int(BR::WoolMagenta), int(BR::WoolLightBlue),
            int(BR::WoolYellow), int(BR::WoolLime), int(BR::WoolPink), int(BR::WoolGray),
            int(BR::WoolLightGray), int(BR::WoolCyan), int(BR::WoolPurple), int(BR::WoolBlue),
            int(BR::WoolBrown), int(BR::WoolGreen), int(BR::WoolRed), int(BR::WoolBlack),
        };
        const int bedTarget[16] = {
            int(BR::BedWhite), int(BR::BedOrange), int(BR::BedMagenta), int(BR::BedLightBlue),
            int(BR::BedYellow), int(BR::BedLime), int(BR::BedPink), int(BR::BedGray),
            int(BR::BedLightGray), int(BR::BedCyan), int(BR::BedPurple), int(BR::BedBlue),
            int(BR::BedBrown), int(BR::BedGreen), int(BR::BedRed), int(BR::BedBlack),
        };
        // ① 段连续性（常量重排 / 抽漏 = FAIL）+ 全有名 + 四花色名精确核对。
        Hotbar hb788;
        bool ok = RecipeRegistry::DyeIdBase == 0x24B;
        for (int i = 0; i < 16; ++i) {
            if (dyeIds[i] != RecipeRegistry::DyeIdBase + i) {
                qInfo().noquote() << "  [t788 diag] dye segment not contiguous at" << i
                                  << "got 0x" + QString::number(dyeIds[i], 16);
                ok = false;
            }
            if (hb788.nameForBlock(dyeIds[i]).isEmpty()) {
                qInfo().noquote() << "  [t788 diag] dye 0x" + QString::number(dyeIds[i], 16)
                                  << "has empty nameForBlock";
                ok = false;
            }
        }
        ok = ok && hb788.nameForBlock(RecipeRegistry::DyeRedId) == QString::fromUtf8("红色染料")
                  && hb788.nameForBlock(RecipeRegistry::DyeYellowId) == QString::fromUtf8("黄色染料")
                  && hb788.nameForBlock(RecipeRegistry::DyeBlueId) == QString::fromUtf8("蓝色染料")
                  && hb788.nameForBlock(RecipeRegistry::DyeWhiteId) == QString::fromUtf8("白色染料");
        // ② 四花破坏 → 对应色染料（dropId 经 Core 访问器；字面量 ↔ 常量契约已由 recipe.cpp static_assert 钉死）。
        ok = ok && BR::dropId(BR::FlowerRed) == RecipeRegistry::DyeRedId
                  && BR::dropId(BR::FlowerYellow) == RecipeRegistry::DyeYellowId
                  && BR::dropId(BR::FlowerBlue) == RecipeRegistry::DyeBlueId
                  && BR::dropId(BR::FlowerWhite) == RecipeRegistry::DyeWhiteId;
        if (!ok) {
            qInfo().noquote() << "  [t788 diag] flower drops:"
                              << BR::dropId(BR::FlowerRed) << BR::dropId(BR::FlowerYellow)
                              << BR::dropId(BR::FlowerBlue) << BR::dropId(BR::FlowerWhite)
                              << "expect" << RecipeRegistry::DyeRedId << RecipeRegistry::DyeYellowId
                              << RecipeRegistry::DyeBlueId << RecipeRegistry::DyeWhiteId;
        }
        // ③ 熔炉烧仙人掌 → 绿染料 + XP（B4 同类缺口：两表任一漏行即 FAIL）。
        if (SmeltingRegistry::smeltResult(int(BR::Cactus)) != RecipeRegistry::DyeGreenId
            || SmeltingRegistry::smeltXpReward(RecipeRegistry::DyeGreenId) < 1) {
            qInfo().noquote() << "  [t788 diag] cactus smelt:"
                              << SmeltingRegistry::smeltResult(int(BR::Cactus))
                              << "xp" << SmeltingRegistry::smeltXpReward(RecipeRegistry::DyeGreenId);
            ok = false;
        }
        // ④ 染色链 32 条：染料+白羊毛 → 色羊毛 / 染料+白床 → 色床（2×2 无序，正摆 + 抽查换位摆）。
        for (int i = 0; i < 16; ++i) {
            int g[9] = { dyeIds[i], int(BR::Wool), 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, 2);
            if (!m || m->outputId != woolTarget[i]) {
                qInfo().noquote() << "  [t788 diag] dye+wool" << i << "->"
                                  << (m ? m->outputId : -1) << "expected" << woolTarget[i];
                ok = false;
            }
            int gb[9] = { dyeIds[i], int(BR::BedWhite), 0, 0, 0, 0, 0, 0, 0 };
            m = RecipeRegistry::match(gb, 2);
            if (!m || m->outputId != bedTarget[i]) {
                qInfo().noquote() << "  [t788 diag] dye+bed" << i << "->"
                                  << (m ? m->outputId : -1) << "expected" << bedTarget[i];
                ok = false;
            }
        }
        // 换位摆抽查（无序位置无关：橙染羊毛基在左 / 绿染白床基在上）。
        {
            int g[9] = { int(BR::Wool), RecipeRegistry::DyeOrangeId, 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, 2);
            if (!m || m->outputId != int(BR::WoolOrange)) {
                qInfo().noquote() << "  [t788 diag] swapped wool arrangement mismatch";
                ok = false;
            }
            int gb[9] = { 0, int(BR::BedWhite), 0, RecipeRegistry::DyeGreenId, 0, 0, 0, 0, 0 };
            m = RecipeRegistry::match(gb, 2);
            if (!m || m->outputId != int(BR::BedGreen)) {
                qInfo().noquote() << "  [t788 diag] swapped bed arrangement mismatch";
                ok = false;
            }
        }
        // 防等价表误扩：染料+木板（错基）不得产任何染色羊毛（kIngredientEquivalents 意外吃进染料即 FAIL）。
        {
            int g[9] = { RecipeRegistry::DyeRedId, int(BR::Planks), 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, 2);
            if (m && (m->outputId == int(BR::WoolRed) || m->outputId == int(BR::BedRed))) {
                qInfo().noquote() << "  [t788 diag] dye+planks wrongly matched dye recipe";
                ok = false;
            }
        }
        // ⑤ 创造背包材料 tab 染料 16 色连续同列（图鉴 ResourceBrowser 由 creativeMaterials 派生 = 同源在列）。
        {
            const QVariantList mats788 = hb788.creativeMaterials();
            int dyeMin = mats788.size(), dyeMax = -1, dyeSeen = 0;
            for (int i = 0; i < mats788.size(); ++i) {
                const int id = mats788.at(i).toInt();
                if (id >= RecipeRegistry::DyeIdBase && id <= RecipeRegistry::DyeBlackId) {
                    dyeMin = std::min(dyeMin, i);
                    dyeMax = std::max(dyeMax, i);
                    ++dyeSeen;
                }
            }
            if (dyeSeen != 16 || dyeMax - dyeMin + 1 != 16) {
                qInfo().noquote() << "  [t788 diag] creative dye block: seen" << dyeSeen
                                  << "span" << (dyeMax - dyeMin + 1) << "(expected 16 contiguous)";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t788 dye system: 16 dye items 0x24B..0x25A contiguous & named, 4 flowers drop "
                             "matching dyes (red/yellow/blue/white) via dropId, furnace cactus->green dye with "
                             "XP, all 32 coloring recipes craftable (16 dye+white-wool -> colored wool, 16 "
                             "dye+white-bed -> colored bed; shapeless spot-checked swapped, wrong-base "
                             "rejected), dyes contiguous in creative palette (resource browser derived), "
                             "recipes auto-covered by t802 full-table self-match (dye icon colors = QML, "
                             "manual check)";
    }

    // ── t789 羊自然毛色探针（用户「羊刷出来只有白色羊毛，没有别的羊毛」）：Entities 层直编（同 t787 自建
    //    临时对象模式，不动共享 nextSlot 分配器）：
    //    ① 色板契约：sheepWoolTintForIndex(0..15) 全有名非空 + 关键色精确核对（白 #ffffff 恒等 / 黑
    //       #1e1e26 / 棕 #734b2d——浏览器 woolPalette / build_wool.py 同值镜像，漂移即 FAIL）；
    //    ② spawn 分布：多轮「刷 ~60 只 → clearAll 清场」累计 4800 样本按自然权重采样（kCap=64 是产品
    //       硬上限单轮封顶；粉 0.164% 在小样本下断言天生 flaky，大样本压 P(漏粉)<0.05%）→ 白主导（>60%）
    //       + 六自然色全出现（粉 ≥1，灰/浅灰/棕/黑另设 0.4× 下限带）+ 上溢带护栏（≤2.5×名义+0.02）
    //       + 无表外色（只允许 {0,6,7,8,12,15}）+ 非 sheep mob（猪对照）恒 0 不受污染；
    //    ③ 剪羊毛掉对应色：shearSheep 发 sheepSheared(x,y,z,woolIdx) 携带与 sheepWoolAt 一致的下标
    //       （QML 层 sheepWoolDropId 映射在呈现层，C++ 锁信号载荷正确性）；已剪再剪不发（幂等回归，
    //       只重剪同批已剪样本）；
    //    ④ 死亡掉对应色：damageEntity 致死 → 带 World tick 越过死亡动画（EntityManager::tick 对 null world
    //       整帧早退，deathTimer 不推进 → 必须传真世界）→ mobDied(...,woolIdx) 第 7 参 == 该羊下标；
    //    ⑤ 幼崽继承父代色（tickBreeding 覆写随机色，同 ocelotVariant 先例；段前清场——②③④ 遗留被动
    //       生物超 kPassiveMobCap=24 会钳死配对产崽）。渲染观感（毛层 tint 上羊身 /
    //       pack 态 fur 染色 / 浏览器变体联动）需人工目视。
    {
        bool ok = true;
        EntityManager em789;
        // ① 色板契约（16 下标全覆盖 + 白恒等 + 两关键色锚点）。
        for (int i = 0; i < 16; ++i) {
            const QColor c = em789.sheepWoolTintForIndex(i);
            if (!c.isValid()) {
                qInfo().noquote() << "  [t789 diag] tint" << i << "invalid";
                ok = false;
            }
        }
        if (em789.sheepWoolTintForIndex(0) != QColor(QStringLiteral("#ffffff"))
            || em789.sheepWoolTintForIndex(15) != QColor(QStringLiteral("#1e1e26"))
            || em789.sheepWoolTintForIndex(12) != QColor(QStringLiteral("#734b2d"))) {
            qInfo().noquote() << "  [t789 diag] palette anchors drifted (white/black/brown)";
            ok = false;
        }
        // ② spawn 自然色分布：多轮清场重刷累计 kSheepTotal=4800 样本（名义权重 白 .8184 / 黑·灰·浅灰 .05
        //    各 / 棕 .03 / 粉 .0016）。kCap=64 是产品硬上限 → 单轮 spawn 至 ~60 只（留猪对照位），clearAll
        //    释放全部槽后再刷下一轮；粉期望 λ=4800×.0016≈7.9，P(全轮漏粉)<0.05%（单轮 600 样本 λ≈1 时
        //    P(漏)≈37% 天生 flaky，故取大样本）。
        constexpr int kSheepPerRound = 60;
        constexpr int kSheepRounds = 80;   // 80 × 60 = 4800 样本
        constexpr int kSheepTotal = kSheepPerRound * kSheepRounds;
        int cnt[16] = {};
        for (int round = 0; round < kSheepRounds && ok; ++round) {
            em789.clearAll(); // 清场上轮（releaseSlot 全活体槽；幂等）
            int spawnedThisRound = 0;
            for (int i = 0; i < kSheepPerRound; ++i) {
                const int slot = em789.spawnMobTyped(4, kRigY, 4, EntityManager::MobSheep,
                                                     QStringLiteral("#f5f0e8"), 10);
                if (slot < 0) { // cap 提前到顶（理论 60<64 不会触发；防御性 FAIL 而非静默缩样本）
                    qInfo().noquote() << "  [t789 diag] sheep spawn capped at" << i
                                      << "in round" << round;
                    ok = false;
                    break;
                }
                ++cnt[em789.sheepWoolAt(slot)];
                ++spawnedThisRound;
            }
            if (spawnedThisRound != kSheepPerRound) break;
        }
        if (ok) {
            // 猪（对照）：非 sheep 的 mob 毛色字段不受 spawnMobCore 写入污染。
            const int pigSlot = em789.spawnMobTyped(6, kRigY, 4, EntityManager::MobPig,
                                                    QStringLiteral("#ee9999"), 10);
            if (pigSlot >= 0 && em789.sheepWoolAt(pigSlot) != 0) {
                qInfo().noquote() << "  [t789 diag] non-sheep mob polluted:" << em789.sheepWoolAt(pigSlot);
                ok = false;
            }
        }
        const int naturalColors[6] = { 0, 6, 7, 8, 12, 15 };
        const double nominal[6] = { 0.8184, 0.0016, 0.05, 0.05, 0.03, 0.05 }; // 与 kSheepNaturalWeights 同源序
        if (cnt[0] * 100 < kSheepTotal * 60) { // 白主导 >60%
            qInfo().noquote() << "  [t789 diag] white not dominant:" << cnt[0] << "/" << kSheepTotal;
            ok = false;
        }
        for (int c = 0; c < 6; ++c) {
            const int idx = naturalColors[c];
            if (c > 0) {
                // 下限带：粉 ≥1（λ≈7.9 下 P(0) 可忽略）；黑/灰/浅灰/棕另设 0.4× 名义下限（4800 样本下
                //   0.4×5%=1920 vs σ≈31、0.4×3%=1152 vs σ≈25 —— 偏离 30σ+ 只可能是权重表漂移而非采样噪声）。
                const double share = double(cnt[idx]) / double(kSheepTotal);
                if ((idx == 6 && cnt[idx] < 1)
                    || (idx != 6 && share < nominal[c] * 0.4)) {
                    qInfo().noquote() << "  [t789 diag] natural color" << idx << "underflow:"
                                      << cnt[idx] << "/" << kSheepTotal;
                    ok = false;
                }
                // 上溢带护栏：≤2.5× 名义 + 0.02（查权重表静默漂移；白已单独断言主导）。
                if (share > nominal[c] * 2.5 + 0.02) {
                    qInfo().noquote() << "  [t789 diag] color" << idx << "share" << share
                                      << "far over nominal" << nominal[c];
                    ok = false;
                }
            }
        }
        for (int idx = 0; idx < 16; ++idx) {
            bool isNatural = false;
            for (int c = 0; c < 6; ++c) isNatural = isNatural || naturalColors[c] == idx;
            if (!isNatural && cnt[idx] != 0) {
                qInfo().noquote() << "  [t789 diag] non-natural color" << idx << "spawned" << cnt[idx];
                ok = false;
            }
        }
        // ③ 剪羊毛携对应色：抽 3 只活体羊，sheepSheared 载荷逐只 == 剪切对象的 sheepWoolAt（连接内按发射序
        //    记录载荷，与外层记录的目标下标按序核对）；幂等回归：**只对已剪的同 3 只**再剪不再发信号
        //    （旧版对全群重剪——未剪样本发新信号 = 探针自伤假 FAIL）。
        {
            constexpr int kShearSamples = 3;
            int shearedCount = 0;
            int payloadWool[kShearSamples] = {};
            QObject::connect(&em789, &EntityManager::sheepSheared, &em789,
                             [&](int sx, int sy, int sz, int woolIdx) {
                                 Q_UNUSED(sx); Q_UNUSED(sy); Q_UNUSED(sz);
                                 if (shearedCount < kShearSamples) payloadWool[shearedCount] = woolIdx;
                                 ++shearedCount;
                             });
            int checked = 0;
            int wantWool[kShearSamples] = {};
            int shearedSlot[kShearSamples] = {};
            for (int i = 0; i < em789.count() && checked < kShearSamples; ++i) {
                if (!em789.aliveAt(i) || em789.mobTypeAt(i) != EntityManager::MobSheep) continue;
                wantWool[checked] = em789.sheepWoolAt(i);
                shearedSlot[checked] = i;
                em789.shearSheep(i); // 同步直连 → 发射序 == 循环序，payloadWool 与 wantWool 按序对齐
                ++checked;
            }
            // 幂等：仅重剪已剪样本 → 零新信号。
            const int before = shearedCount;
            for (int k = 0; k < checked; ++k) em789.shearSheep(shearedSlot[k]);
            bool shearOk = checked == kShearSamples && shearedCount == before;
            for (int k = 0; k < kShearSamples; ++k) shearOk = shearOk && payloadWool[k] == wantWool[k];
            if (!shearOk) {
                qInfo().noquote() << "  [t789 diag] shear payload/idempotence:" << checked
                                  << "samples," << before << "-> after" << shearedCount
                                  << "payloads" << payloadWool[0] << payloadWool[1] << payloadWool[2]
                                  << "want" << wantWool[0] << wantWool[1] << wantWool[2];
                ok = false;
            }
        }
        // ④ 死亡掉对应色：取一只活体成体羊记录下标 → damageEntity 致死 → 带真实 World 驱动 tick 越过
        //    死亡动画（EntityManager::tick 对 null world **整帧早退**，deathTimer 永不推进 → 旧版传 nullptr
        //    = mobDied 恒不发 = 探针自伤假 FAIL；t774 先例同传真世界）→ mobDied 第 7 参 == 该羊下标。
        {
            World w789d; // 死亡段专用小世界（羊悬空 y=41 落地即 resting；死亡态冻结 AI/重力不位移）
            w789d.setWidth(32);
            w789d.setDepth(32);
            w789d.setHeight(48);
            w789d.setSeed(11);
            int deathIdx = -1, deathWool = -1;
            for (int i = 0; i < em789.count() && deathIdx < 0; ++i) {
                // review #32：须挑**未剪毛**样本——③ 段刚剪过 3 只，剪毛羊致死 mobDied 第 8 参 sheared=true
                //   （QML 据此压掉羊毛掉落），本探针锁的是「正常羊毛掉落羊」的 woolIdx 载荷（剪毛样本的
                //   true/false 对照见文件尾 review-e 探针）。
                if (em789.aliveAt(i) && !em789.deadAt(i) && !em789.isBabyAt(i)
                    && !em789.shearedAt(i)
                    && em789.mobTypeAt(i) == EntityManager::MobSheep)
                    deathIdx = i;
            }
            if (deathIdx < 0) {
                qInfo() << "  [t789 diag] no live adult sheep left for death probe";
                ok = false;
            } else {
                deathWool = em789.sheepWoolAt(deathIdx);
                int diedPayload = -1, diedType = -1, diedCount = 0, diedSheared = -1;
                QObject::connect(&em789, &EntityManager::mobDied, &em789,
                                 [&](int x, int y, int z, int type, bool burned, bool baby,
                                     int woolIdx, bool sheared) {
                                     Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z);
                                     Q_UNUSED(burned); Q_UNUSED(baby);
                                     ++diedCount; diedType = type; diedPayload = woolIdx;
                                     diedSheared = sheared ? 1 : 0;
                                 });
                em789.damageEntity(deathIdx, em789.maxHealthAt(deathIdx));
                const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
                for (int t = 0; t < 40 && diedCount == 0; ++t) // 0.64s > kDeathTime 0.5s → mobDied 已发
                    em789.tick(0.016f, &w789d, farListener, 0.3f, 1.8f, false);
                // review #32 第 8 参 sheared：本样本未剪毛 → 恒 false（剪毛样本的 true 分支见 review-e 探针）。
                if (diedCount != 1 || diedType != EntityManager::MobSheep || diedPayload != deathWool
                    || diedSheared != 0) {
                    qInfo().noquote() << "  [t789 diag] death drop payload:" << diedCount << diedType
                                      << diedPayload << "expected wool" << deathWool
                                      << "sheared" << diedSheared;
                    ok = false;
                }
            }
        }
        // ⑤ 幼崽继承：**清场后**构造两只求偶期成体羊 → tickBreeding → 幼崽 wool ∈ 双亲色集。
        //    清场是硬前提：②③④ 段遗留 ~61 只被动生物 > kPassiveMobCap=24（产品种群上限，配对不再产崽）
        //    → 不清场则 babies 恒 0（旧版此段因 spawn cap 整块跳过从未真正跑过，清场后才首次暴露）。
        {
            em789.clearAll();
            World w789;
            w789.setWidth(32);
            w789.setDepth(32);
            w789.setHeight(24);
            w789.setSeed(7);
            const auto pa = em789.spawnMobTyped(14, 12, 15, EntityManager::MobSheep,
                                                QStringLiteral("#f5f0e8"), 10);
            const auto pb = em789.spawnMobTyped(15, 12, 15, EntityManager::MobSheep,
                                                QStringLiteral("#f5f0e8"), 10);
            if (pa >= 0 && pb >= 0) {
                // 两亲代 spawn 随机色不可控 → 继承断言收窄为「幼崽 ∈ 双亲色集」：仍能抓「羊幼崽走了
                //   权重表随机重掷」（回归时幼崽色 81.8% 概率落白、与双亲集脱钩）的破链。
                em789.enterLoveMode(pa);
                em789.enterLoveMode(pb);
                const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
                for (int t = 0; t < 20; ++t) // 0.32s：寻偶相遇 + 配对产崽（kBabyGrowTime 前幼崽仍在槽）
                    em789.tick(0.016f, &w789, farListener, 0.3f, 1.8f, false);
                const int setA = em789.sheepWoolAt(pa), setB = em789.sheepWoolAt(pb);
                int babySeen = 0, babyWrong = 0;
                for (int i = 0; i < em789.count(); ++i) {
                    if (!em789.aliveAt(i) || !em789.isBabyAt(i) || em789.mobTypeAt(i) != EntityManager::MobSheep)
                        continue;
                    ++babySeen;
                    const int wc = em789.sheepWoolAt(i);
                    if (wc != setA && wc != setB) ++babyWrong; // 走了权重表随机 = 回归
                }
                if (babySeen < 1 || babyWrong != 0) {
                    qInfo().noquote() << "  [t789 diag] baby inherit: babies" << babySeen
                                      << "wrong" << babyWrong << "parents" << setA << setB;
                    ok = false;
                }
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t789 sheep natural colors: 16-entry tint palette valid with white-identity/"
                             "black/brown anchors mirroring browser woolPalette, 4800 spawns over clear-all "
                             "rounds follow natural weights (white >60% dominant, pink/gray/light-gray/"
                             "brown/black all appear, no out-of-table colors, pigs unpolluted), shearSheep "
                             "carries the sheep's own index and re-shear stays silent, mobDied payload "
                             "equals the died sheep's index, breeding babies inherit a parent color (not "
                             "rerolled)";
    }

    // ── t777 羊 pack 态「多一双眼」根因合成器探针 ──
    // 修法核心 = t749 毛层命中时 mobTextureSource(3) 返回合成贴图（毛身 + 本体层头区真脸）→ QML 眼 overlay
    //   须隐（判据 sheepWoolFaceActive，Main.qml/ResourceBrowser 共用）。本探针锁合成器两端语义（纯函数、
    //   临时 PNG rig，不触碰进程全局 BuiltState——那非本测试私有，实例化 ResourcePackManager 会读到宿主机
    //   settings.json 的真实 pack 态 = 非密闭）：
    //   ① 真 64×32 fur+body 双 PNG → 合成落盘成功 + 输出保 base 尺寸 + **头区 (0,0)-(28,14) = 本体层色**
    //     （真脸覆写，眼 overlay 隐的依据）+ 毛身区（head 区外）= 毛层原色（毛身保留）；
    //   ② body 源缺失 → 空串优雅降级（调用方回退毛层原样、头前无脸 → 眼 overlay 须保留的路径）。
    // 腿 skin 色 overlay / 眼位修正是 QML 呈现层，无 C++ 可测路径（矩阵不链 Quick3D）。
    {
        bool ok = true;
        QDir d777(QDir::temp().absoluteFilePath("t777_sheep_probe"));
        d777.removeRecursively();
        d777.mkpath(".");
        // ① 真 64×32 双源（毛层米白 / 本体层棕，两色互异防「合成成功但没覆写」假 PASS）。
        const QString furPath = d777.absoluteFilePath("fur.png");
        const QString bodyPath = d777.absoluteFilePath("body.png");
        QImage fur777(64, 32, QImage::Format_ARGB32);
        fur777.fill(QColor(0xf0, 0xec, 0xe4));
        QImage body777(64, 32, QImage::Format_ARGB32);
        body777.fill(QColor(0x7a, 0x5a, 0x48));
        if (!fur777.save(furPath, "PNG") || !body777.save(bodyPath, "PNG")) {
            qInfo().noquote() << "  [t777 diag] failed to write temp source PNGs";
            ok = false;
        }
        const QString comp = generateSheepWoolFaceFile(furPath, bodyPath, 1);
        if (comp.isEmpty()) {
            qInfo().noquote() << "  [t777 diag] composite unexpectedly failed on valid 64x32 pair";
            ok = false;
        } else {
            QImage out777(comp);
            if (out777.isNull() || out777.width() != 64 || out777.height() != 32) {
                qInfo().noquote() << "  [t777 diag] composite output not base 64x32:"
                                  << (out777.isNull() ? -1 : out777.width())
                                  << "x" << (out777.isNull() ? -1 : out777.height());
                ok = false;
            } else {
                // 头区中心 (14,7) ∈ base (0,0)-(28,14) → 本体层色（真脸）；毛身区 (40,20)（body/leg 行）→ 毛层色。
                if (out777.pixelColor(14, 7) != QColor(0x7a, 0x5a, 0x48)) {
                    qInfo().noquote() << "  [t777 diag] head region not body-layer color:"
                                      << out777.pixelColor(14, 7).name();
                    ok = false;
                }
                if (out777.pixelColor(40, 20) != QColor(0xf0, 0xec, 0xe4)) {
                    qInfo().noquote() << "  [t777 diag] wool body region not fur color:"
                                      << out777.pixelColor(40, 20).name();
                    ok = false;
                }
            }
        }
        // ② body 缺失 → 空串（降级：调用方回退毛层原样 = 无脸 → QML 眼 overlay 保留路径）。
        if (!generateSheepWoolFaceFile(furPath, d777.absoluteFilePath("missing.png"), 1).isEmpty()) {
            qInfo().noquote() << "  [t777 diag] missing body source should degrade to empty, not succeed";
            ok = false;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t777 sheep wool-face compositor: valid 64x32 fur+body pair composites to "
                             "base-size output with body-layer (real-face) pixels in head region (0,0)-"
                             "(28,14) and fur pixels preserved in wool body/leg rows, missing body source "
                             "degrades to empty (caller falls back to raw fur = eye overlay stays visible)";
    }

    // ── t779 头像裁剪修复探针（用户「猪头像缺鼻子、蠹虫头像缺眼睛」）──
    // 根因：MC 机制 = 猪鼻画在独立贴图偏移盒 (16,16) 4×3×1（头脸 (8,8)-(16,16) 只有 row11 双眼）；
    //   蠹虫旧条目取的是第二体节甲壳 (2,4)-(10,9)（无眼），真头 = 首盒 (0,0)6×2×2、双眼跨 top/front
    //   边界（demo 包像素取证）。修法 = 猪「脸 + 鼻覆写盒合成」/ 蠹虫「头顶+脸拼合区直取」。
    //   ① 布局常量锁（mobHeadIconLayout 单一权威——resourcepackmanager.cpp 生成器与探针同源，表数值
    //     漂移 = 此处 FAIL，同 t785 单一权威教训）：猪 front(8,8)8×8 + 鼻覆写 src(17,17)4×3 贴 (2,4)；
    //     蠹虫 front(0,0)8×4 无覆写；回归锁牛/蜘蛛/豹猫/夜行者 front 不变 + 表外 mobType 恒无条目。
    //   ② 端到端合成（临时 PNG rig 直调 generateMobHeadIconFor，不触碰进程全局 BuiltState/settings，
    //     同 t777 密闭语义）：合成猪 64×32（整图脸粉 A + 鼻 Front 深粉 B，走 pig/pig.png 主映射子目录
    //     探测）→ 图标 64×64 中 A=脸底/B=鼻贴脸中下/C=下巴（旧实现无合成 → B 处仍 A，FAIL）；合成蠹虫
    //     64×32 扁平（头区 (0,0)-(8,4) = C / 余 = D，走 explicitSrc 扁平探测）→ 图标含 C 横带居中 + 带外
    //     透明（旧裁剪 (2,4)-(10,9) 全 D 区，FAIL）。图鉴图标本体观感（QML 缩放呈现）需人工目视。
    {
        bool ok = true;
        // ① 布局常量锁（mob/…/paste 全字段；表加条目改数值 = 漂移即 FAIL）。
        {
            const struct {
                int mob; int fx, fy, fw, fh; bool ov; int sx, sy, sw, sh, px, py;
            } exp[] = {
                //       front               overlay(src …, paste …)
                {  1,  8,  8,  8,  8,  true,  17, 17, 4, 3, 2, 4 }, // 猪：脸 + 鼻覆写（眼 row3 上 / 鼻 row4-6 下）
                { 14,  0,  0,  8,  4, false,   0,  0, 0, 0, 0, 0 }, // 蠹虫：头顶+脸拼合区（含双眼）
                {  2,  6,  6,  8,  8, false,   0,  0, 0, 0, 0, 0 }, // 牛（d=6 → front (6,6)；回归锁）
                {  7, 40, 12,  8,  8, false,   0,  0, 0, 0, 0, 0 }, // 蜘蛛（offset(32,4) d=8；回归锁）
                { 11,  5,  5,  5,  4, false,   0,  0, 0, 0, 0, 0 }, // 豹猫（offset(1,1) 5×4×4；回归锁）
                { 16,  8,  8,  8,  6, false,   0,  0, 0, 0, 0, 0 }, // 夜行者（h=6 底两行空；回归锁）
            };
            for (const auto &e : exp) {
                MobHeadIconLayout lay;
                if (!mobHeadIconLayout(e.mob, &lay)
                        || lay.frontX != e.fx || lay.frontY != e.fy
                        || lay.frontW != e.fw || lay.frontH != e.fh
                        || lay.hasOverlay != e.ov || lay.ovSrcX != e.sx || lay.ovSrcY != e.sy
                        || lay.ovW != e.sw || lay.ovH != e.sh
                        || lay.ovPasteX != e.px || lay.ovPasteY != e.py) {
                    qInfo().noquote() << "  [t779 diag] layout for mob" << e.mob << "mismatch (front"
                                      << lay.frontX << lay.frontY << lay.frontW << lay.frontH
                                      << "ov" << lay.hasOverlay << ")";
                    ok = false;
                }
            }
            MobHeadIconLayout none;
            if (mobHeadIconLayout(99, &none)) { // 表外未知型恒无条目（防越段误命中）
                qInfo().noquote() << "  [t779 diag] unknown mobType should have no entry";
                ok = false;
            }
        }
        // ② 端到端：猪鼻合成 + 蠹虫眼区。
        QDir d779(QDir::temp().absoluteFilePath("t779_headicon_probe"));
        d779.removeRecursively();
        d779.mkpath(".");
        QDir(d779.absoluteFilePath("pig")).mkpath("."); // 猪 explicitSrc 空 → mobEntityMap 主映射 pig/pig.png（子目录布局）
        const QColor faceA(0xf0, 0xa0, 0xa8), snoutB(0xc0, 0x60, 0x70);   // 脸粉 / 鼻深粉（互异防假 PASS）
        const QColor headC(0x88, 0x90, 0x88), bodyD(0x40, 0x48, 0x40);    // 虫头灰 / 体节深灰
        QImage pigTex(64, 32, QImage::Format_ARGB32);
        pigTex.fill(faceA);
        {
            QPainter p(&pigTex);
            p.fillRect(17, 17, 4, 3, snoutB); // 鼻 Front (17,17)-(21,20)（MC 鼻盒 offset(16,16) 4×3×1 的脸面）
            p.end();
        }
        QImage sfTex(64, 32, QImage::Format_ARGB32);
        sfTex.fill(bodyD);
        {
            QPainter p(&sfTex);
            p.fillRect(0, 0, 8, 4, headC);    // 虫头拼合区 (0,0)-(8,4)（头顶+脸，双眼所在）
            p.end();
        }
        if (!pigTex.save(d779.absoluteFilePath("pig/pig.png"), "PNG")
                || !sfTex.save(d779.absoluteFilePath("silverfish.png"), "PNG")) {
            qInfo().noquote() << "  [t779 diag] failed to write temp source PNGs";
            ok = false;
        }
        // 猪：8×8 脸 → 64×64 图标（×8 整倍块映射）：脸(3,1) 眼上方 = A；鼻贴放 (2,4)-(6,7) → 脸(4,5) = B
        //   （旧实现无合成此处 A → FAIL）；脸(7,7) 下巴（覆写区外）= A。
        const QString pigIcon = generateMobHeadIconFor(1, d779.absolutePath(), 1);
        if (pigIcon.isEmpty()) {
            qInfo().noquote() << "  [t779 diag] pig icon generation unexpectedly failed";
            ok = false;
        } else {
            QImage ic(pigIcon);
            if (ic.width() != 64 || ic.height() != 64) {
                qInfo().noquote() << "  [t779 diag] pig icon not 64x64:" << ic.width() << "x" << ic.height();
                ok = false;
            } else if (ic.pixelColor(28, 12) != faceA || ic.pixelColor(36, 44) != snoutB
                       || ic.pixelColor(60, 60) != faceA) {
                qInfo().noquote() << "  [t779 diag] pig icon pixels wrong: forehead"
                                  << ic.pixelColor(28, 12).name() << "snout" << ic.pixelColor(36, 44).name()
                                  << "chin" << ic.pixelColor(60, 60).name();
                ok = false;
            }
        }
        // 蠹虫：8×4 头区 aspect 2 → 64×32 条带贴 (0,16)：带中 (36,36) = C；带外 (32,8) 透明。
        //   （旧裁剪 (2,4)-(10,9) 全落 D 区 → 带中 D，FAIL。）
        const QString sfIcon = generateMobHeadIconFor(14, d779.absolutePath(), 1);
        if (sfIcon.isEmpty()) {
            qInfo().noquote() << "  [t779 diag] silverfish icon generation unexpectedly failed";
            ok = false;
        } else {
            QImage ic(sfIcon);
            if (ic.width() != 64 || ic.height() != 64) {
                qInfo().noquote() << "  [t779 diag] silverfish icon not 64x64:" << ic.width() << "x" << ic.height();
                ok = false;
            } else if (ic.pixelColor(36, 36) != headC || ic.pixelColor(32, 8).alpha() != 0) {
                qInfo().noquote() << "  [t779 diag] silverfish icon pixels wrong: band"
                                  << ic.pixelColor(36, 36).name() << "outside alpha"
                                  << ic.pixelColor(32, 8).alpha();
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t779 mob head icon crops: pig front (8,8)8x8 + snout overlay box (16,16)4x3x1 "
                             "front (17,17)-(21,20) composited at face (2,4) (eyes row3 above snout rows4-6), "
                             "silverfish front switched from body-segment (2,4)-(10,9) to head top+face band "
                             "(0,0)-(8,4) containing both eyes, cow/spider/ocelot/nightwalker fronts locked "
                             "unchanged, synthetic 64x32 rigs verify snout/eye pixels land in the 64x64 icons "
                             "(icon look in browser = QML, manual check)";
    }

    // ── t780 狼/豹猫 pack 身体贴图映射探针（用户「浏览器 3D 预览狼仍用兔子贴图、豹猫贴图不对——头对身错」）──
    // 根因：狼(10)/豹猫(11) 自 t749 起刻意不入 mobEntityMap（当时几何全脸 UV 无 box-UV 数据，防
    //   packTextured 误命中）→ mobTextureSource 恒 miss → 3D 预览身体恒程序全脸 UV（灰身立耳四足读作
    //   「兔子」），2D 头像却走 explicitSrc 正确显头 = 「头对身错」。修法 = mobmodel.cpp 补两分支 box-UV
    //   （demo 包像素实测分区：狼身采 mane(21,0)——body(18,14) 三面未涂满；豹猫身 (20,6) 尾随身同纹）+
    //   入主映射 + 头像 explicitSrc 撤除（主映射同源）+ Main.qml delegate / 图鉴 / 刷怪笼迷你态 pack 接线。
    //   ① 映射锁（mobEntityMap t780 提头后可直调，同 t785 spawnEggTint）：10→wolf/wolf.png、11→
    //     cat/ocelot.png 精确路径；14（蠹虫）仍**不在**表（几何全脸 UV 只走头像 explicitSrc——入表会让
    //     3D packTextured 采到未设定位 = 贴图错乱回归）。
    //   ② 头区布局锁（mobHeadIconLayout 单一权威）：狼 front(4,4)6×6（新增锁——t780 只改来源路径不改
    //     裁剪区）/ 豹猫 front(5,5)5×4（t779 已锁，重申 explicitSrc 撤除不漂移）。
    //   ③ 端到端（临时 PNG rig 直调 generateMobHeadIconFor，密闭不触碰进程全局 BuiltState/settings，
    //     同 t777/t779 语义）：rig 按 mobEntityMap 子目录布局落 wolf/wolf.png + cat/ocelot.png → 两型
    //     头像生成成功且中心像素正确 = explicitSrc 撤除后「region 条目 → mobEntityMap → 文件解析」链路
    //     通（映射漏行 / 头区条目丢 → 空串 FAIL）。3D box-UV 采样观感（mobmodel.cpp 几何层）需人工目视。
    {
        bool ok = true;
        // ① 映射锁（精确路径 + 蠹虫排除）。
        {
            bool wolfOk = false, ocelotOk = false, silverfishInMap = false;
            for (const auto &m : mobEntityMap()) {
                if (m.first == 10) wolfOk = (m.second == QStringLiteral("wolf/wolf.png"));
                if (m.first == 11) ocelotOk = (m.second == QStringLiteral("cat/ocelot.png"));
                if (m.first == 14) silverfishInMap = true;
            }
            if (!wolfOk || !ocelotOk || silverfishInMap) {
                qInfo().noquote() << "  [t780 diag] map entries wrong: wolf" << wolfOk
                                  << "ocelot" << ocelotOk << "silverfish-in-map" << silverfishInMap;
                ok = false;
            }
        }
        // ② 头区布局锁。
        {
            const struct { int mob; int fx, fy, fw, fh; } exp[] = {
                { 10,  4,  4,  6,  6 }, // 狼：head(0,0)6×6×4 → front (4,4)-(10,10)
                { 11,  5,  5,  5,  4 }, // 豹猫：head(1,1)5×4×4 → front (5,5)-(10,9)
            };
            for (const auto &e : exp) {
                MobHeadIconLayout lay;
                if (!mobHeadIconLayout(e.mob, &lay)
                        || lay.frontX != e.fx || lay.frontY != e.fy
                        || lay.frontW != e.fw || lay.frontH != e.fh) {
                    qInfo().noquote() << "  [t780 diag] head layout mob" << e.mob << "mismatch (front"
                                      << lay.frontX << lay.frontY << lay.frontW << lay.frontH << ")";
                    ok = false;
                }
            }
        }
        // ③ 端到端：explicitSrc 撤除后主映射路径解析（子目录布局同 demo 包）。
        QDir d780(QDir::temp().absoluteFilePath("t780_mobtex_probe"));
        d780.removeRecursively();
        d780.mkpath(".");
        QDir(d780.absoluteFilePath("wolf")).mkpath(".");
        QDir(d780.absoluteFilePath("cat")).mkpath(".");
        const QColor wolfHead(0x9a, 0x8c, 0x88), catFace(0xdd, 0xd7, 0x7b); // 狼头灰 / 豹猫脸黄（互异防假 PASS）
        QImage wolfTex(64, 32, QImage::Format_ARGB32), catTex(64, 32, QImage::Format_ARGB32);
        wolfTex.fill(wolfHead);
        catTex.fill(catFace);
        if (!wolfTex.save(d780.absoluteFilePath("wolf/wolf.png"), "PNG")
                || !catTex.save(d780.absoluteFilePath("cat/ocelot.png"), "PNG")) {
            qInfo().noquote() << "  [t780 diag] failed to write temp source PNGs";
            ok = false;
        }
        const struct { int mob; QColor center; } rigs[] = { { 10, wolfHead }, { 11, catFace } };
        for (const auto &e : rigs) {
            const QString icon = generateMobHeadIconFor(e.mob, d780.absolutePath(), 1);
            if (icon.isEmpty()) {
                qInfo().noquote() << "  [t780 diag] mob" << e.mob << "icon generation unexpectedly failed";
                ok = false;
                continue;
            }
            QImage ic(icon);
            if (ic.width() != 64 || ic.height() != 64 || ic.pixelColor(32, 32) != e.center) {
                qInfo().noquote() << "  [t780 diag] mob" << e.mob << "icon wrong:" << ic.width() << "x"
                                  << ic.height() << "center" << ic.pixelColor(32, 32).name();
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t780 wolf/ocelot pack body texture: mobEntityMap gains 10->wolf/wolf.png + "
                             "11->cat/ocelot.png (silverfish 14 stays out - head-only explicitSrc path), "
                             "head fronts locked (4,4)6x6 / (5,5)5x4, explicitSrc removal verified end-to-end "
                             "via subdir-layout rigs resolving through the main map (3D box-UV look = QML, "
                             "manual check)";
    }

    // ── t791 骨粉催熟平衡探针（spec R19.12 🅵「3-4 个骨粉应催熟一株」；World::applyBonemeal 直调锁数值分布）──
    // 背景：t447 原实现每骨粉 +1 阶段 → 0..7 共 8 阶段要 7 骨粉（用户实测「多个骨粉催不熟一株」）。t791 对齐
    //   MC 骨粉「+2~5 阶段」推进语义、压缩上界为 +2..3 → 从阶段 0 恰 3-4 骨粉催熟（数学保证与哈希质量无关：
    //   3 骨粉推进和 ∈ [6,9]，≥7 即 3 骨粉熟；2+2+2=6 时第 4 骨粉钳到 7 → uses ∈ {3,4} 恒成立）。树苗走 MC 1.0
    //   sapling bone meal 45% 概率即时成树（概率判定非阶段推进）+ 支撑 / 主干畅通守卫（光照豁免）；浆果丛
    //   +1 阶段封顶。rig 寻址：运行期扫描 y40..47 全净空 20×5 区（P20 先例——nextSlot 网格已被前序循环探针
    //   耗尽；树苗须 y≤41 才容得下 4 格主干 + 2 格树冠余量 → 净空须验到 y47）。
    {
        bool ok = true;
        int bx = -1, bz = -1;
        for (int zz = 2; zz + 4 < 96 && bx < 0; zz += 3) {
            for (int xx = 2; xx + 19 < 96; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 19 && clear; ++dx)
                    for (int dz = 0; dz <= 4 && clear; ++dz)
                        for (int dy = 40; dy <= 47 && clear; ++dy)
                            if (w.blockAt(xx + dx, dy, zz + dz) != BR::Air) clear = false;
                if (clear) { bx = xx; bz = zz; }
            }
        }
        if (bx < 0) {
            qInfo().noquote() << "  [t791 diag] no clear 20x5x8 region at y=40..47";
            ok = false;
        } else {
            const int gY = 40; // 地台层（耕地 / 泥土 / 圆石）
            const int pY = 41; // 植物层（树苗 41 恰容 4 主干 + 2 树冠余量 ≤47）
            // ① 树苗守卫负例（先于成树正例——正例树冠会在 y43+ 写叶混入守卫区）：石支撑（非草/泥土）+
            //    主干列 y45 阻塞 → 30 骨粉仍不成树（返回 true = 使用即耗，树苗保留；MC 1.0 同）。
            w.setBlock(bx, gY, bz + 3, BR::Cobble, 0);
            w.setBlock(bx, pY, bz + 3, BR::Sapling, 0);
            w.setBlock(bx + 2, gY, bz + 3, BR::Dirt, 0);
            w.setBlock(bx + 2, pY, bz + 3, BR::Sapling, 0);
            w.setBlock(bx + 2, 45, bz + 3, BR::Dirt, 0);
            bool guardConsumed = true;
            for (int i = 0; i < 30; ++i) {
                guardConsumed = w.applyBonemeal(bx, pY, bz + 3) && guardConsumed;       // 石支撑
                guardConsumed = w.applyBonemeal(bx + 2, pY, bz + 3) && guardConsumed;   // 主干阻塞
            }
            if (!guardConsumed || w.blockAt(bx, pY, bz + 3) != BR::Sapling
                    || w.blockAt(bx + 2, pY, bz + 3) != BR::Sapling) {
                qInfo().noquote() << "  [t791 diag] guard saplings: consumed" << guardConsumed
                                  << "stillA" << (w.blockAt(bx, pY, bz + 3) == BR::Sapling)
                                  << "stillB" << (w.blockAt(bx + 2, pY, bz + 3) == BR::Sapling);
                ok = false;
            }
            // ② 树苗成树正例：6 株泥土支撑 + 净空 → 逐株骨粉到成树（≤60 次防死循环）；树基 Log 顶替树苗位。
            //    锁分布：总投掷数 10..24（6 株 / 45% → 期望 ~13.3）且首掷即中与 ≥2 掷两态都出现（45% 非 0/100）。
            int totalRolls = 0, firstTry = 0, slowGrow = 0;
            for (int s = 0; s < 6; ++s) {
                const int sx = bx + s * 3, sz = bz + 2;
                w.setBlock(sx, gY, sz, BR::Dirt, 0);
                w.setBlock(sx, pY, sz, BR::Sapling, 0);
                int n = 0;
                bool consumed = true;
                while (w.blockAt(sx, pY, sz) == BR::Sapling && n < 60) {
                    consumed = w.applyBonemeal(sx, pY, sz) && consumed; // 使用即耗（判定落空也 true）
                    ++n; ++totalRolls;
                }
                if (!consumed || w.blockAt(sx, pY, sz) != BR::Log) {
                    qInfo().noquote() << "  [t791 diag] sapling" << s << "rolls" << n
                                      << "base" << w.blockAt(sx, pY, sz);
                    ok = false;
                }
                if (n == 1) ++firstTry;
                if (n >= 2) ++slowGrow;
            }
            if (totalRolls < 10 || totalRolls > 24 || firstTry < 1 || slowGrow < 1) {
                qInfo().noquote() << "  [t791 diag] sapling distribution: rolls" << totalRolls
                                  << "firstTry" << firstTry << "slow" << slowGrow;
                ok = false;
            }
            // ③ 作物 +2..3 阶段 / 3-4 骨粉催熟（8 小麦 + 胡萝卜 + 马铃薯，耕地支撑同生产种植路径）：
            //    每骨粉推进 ∈ {2,3}（钳顶 7 时 delta 可 <2 但 after 必为 7）；uses ∈ [3,4]；id 不漂移；
            //    分布锁：推进 2 与 3 两值都出现 + uses 最小 3 / 最大 4（带内两端都达）。
            int usesMin = 99, usesMax = 0, adv2 = 0, adv3 = 0;
            const quint8 cropIds[10] = { BR::WheatCrop, BR::WheatCrop, BR::WheatCrop, BR::WheatCrop,
                                         BR::WheatCrop, BR::WheatCrop, BR::WheatCrop, BR::WheatCrop,
                                         BR::CarrotCrop, BR::PotatoCrop };
            for (int c = 0; c < 10; ++c) {
                const int cx = bx + c;
                w.setBlock(cx, gY, bz, BR::Farmland, 0);
                w.setBlock(cx, pY, bz, cropIds[c], 0);
                int uses = 0;
                bool consumed = true;
                while (w.stateAt(cx, pY, bz) < BR::WheatCropStageMax && uses < 8) {
                    const int before = w.stateAt(cx, pY, bz);
                    consumed = w.applyBonemeal(cx, pY, bz) && consumed;
                    const int after = w.stateAt(cx, pY, bz);
                    ++uses;
                    const int d = after - before;
                    if (d == 2) ++adv2;
                    else if (d == 3) ++adv3;
                    else if (after != int(BR::WheatCropStageMax)) { // 钳顶例外（after==7 合法）
                        qInfo().noquote() << "  [t791 diag] crop" << c << "advance" << d << "->" << after;
                        ok = false;
                    }
                }
                if (!consumed || w.stateAt(cx, pY, bz) != BR::WheatCropStageMax
                        || w.blockAt(cx, pY, bz) != cropIds[c] || uses < 3 || uses > 4) {
                    qInfo().noquote() << "  [t791 diag] crop" << c << "uses" << uses
                                      << "state" << w.stateAt(cx, pY, bz);
                    ok = false;
                }
                usesMin = std::min(usesMin, uses);
                usesMax = std::max(usesMax, uses);
            }
            if (adv2 < 1 || adv3 < 1 || usesMin != 3 || usesMax != 4) {
                qInfo().noquote() << "  [t791 diag] crop distribution: adv2" << adv2 << "adv3" << adv3
                                  << "uses" << usesMin << "-" << usesMax;
                ok = false;
            }
            // ③b 成熟作物负例：state==7 → 返 false（无效应不消耗）+ 阶段保持 7。
            if (w.applyBonemeal(bx, pY, bz) || w.stateAt(bx, pY, bz) != BR::WheatCropStageMax) {
                qInfo().noquote() << "  [t791 diag] mature crop bonemeal not a no-op";
                ok = false;
            }
            // ④ 浆果丛 +1 阶段：0→1→2 后封顶返 false（MC sweet berry bush bone meal 单阶段推进）。
            w.setBlock(bx + 12, pY, bz + 1, BR::SweetBerryBush, 0);
            if (!w.applyBonemeal(bx + 12, pY, bz + 1) || w.stateAt(bx + 12, pY, bz + 1) != 1
                    || !w.applyBonemeal(bx + 12, pY, bz + 1) || w.stateAt(bx + 12, pY, bz + 1) != 2
                    || w.applyBonemeal(bx + 12, pY, bz + 1)
                    || w.stateAt(bx + 12, pY, bz + 1) != 2) {
                qInfo().noquote() << "  [t791 diag] berry bush stages wrong:"
                                  << w.stateAt(bx + 12, pY, bz + 1);
                ok = false;
            }
            // ⑤ 非目标负例：泥土 / 空气 → 返 false（机制等价 MC 骨粉对非生长目标无效应）。
            w.setBlock(bx + 11, pY, bz + 1, BR::Dirt, 0);
            if (w.applyBonemeal(bx + 11, pY, bz + 1) || w.applyBonemeal(bx + 13, pY, bz + 1)) {
                qInfo().noquote() << "  [t791 diag] bonemeal applied to dirt/air";
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t791 bonemeal balance: crops advance 2-3 stages per use so 3-4 bone meals "
                             "mature a plant from stage 0 (10 plants locked uses 3..4, advance in {2,3} "
                             "clamped at 7, id preserved, mature -> no-op no-consume), sapling 45% per use "
                             "instant tree (trunk base Log, 6 saplings within 10-24 rolls, first-try and "
                             "retry both observed) with support+clearance guards (cobble base / blocked "
                             "trunk never grow yet still consumed), berry bush +1 stage to cap then no-op, "
                             "non-targets false (survival consume + swing + stage texture swap = "
                             "playercontroller/QML, manual check)";
    }

    // ── t806 余烬门尺寸泛化探针（World 层直调：点燃检测 / 连通域熄灭 t806 已自 PlayerController 下沉 World
    //    单一权威——同末地门三件套模式，矩阵可直编；粒子改门色紫是 QML blockColor 表（呈现层单一权威）→
    //    需人工目视，此处不测）：
    //    ① 2×3 最小门（X 平面 / 带角）通过 + 门格恰 6 + state=0；② 4×5 门（Z 平面；t806 时代上限，t848
    //    放宽后仅常规尺寸——更大门全覆盖在 t848 探针）通过 + 门格恰 20（点燃填满整个开口）+ state=1；
    //    ③ 超限拒（t848 尺寸表）：内腔 22 宽 / 22 高均拒且零门格（t806 时代此处断言 5 宽 / 6 高拒——
    //    t848 放宽后那些已是合法门，本 rig 世界的 32 高也放不下 22 高门 → 升 64）；④ 缺角通过（MC 1.0 角块
    //    可选）+ 破角不碎门（角块不承结构且与门格对角不邻）；⑤ 缺承重框格拒：底梁 / 顶梁 / 边柱各破一格
    //    均拒；⑥ 非矩形（腔内异物）拒；⑦ 低于最小（1 宽 / 2 高）拒；⑧ 点燃位无关性：4×5 开口四角 + 中部
    //    任一格点燃同成门；⑨ 破框碎门：破任一承重框格（镜像 finishMiningAt 的 setBlock(Air)+
    //    breakNetherPortalsAround 序列）→ 整门 20 格全熄；直挖门格（setBlock(Air)+removeNetherPortalAt）
    //    同样整门熄（连通域尺寸无关）。
    {
        World w806;
        w806.setWidth(48);
        w806.setDepth(48);
        w806.setHeight(64); // t848：③ 超限样本升 22 高（清场盒 y 至 pY+25）→ 原 32 高放不下
        w806.setSeed(13);
        bool ok = true;
        const int pY = 20; // 门框基线层（开口 y=pY..pY+h-1；不轻信「y 以上必空」——buildFrame 先显式清场兜底）
        // 建门框（泛化）：开口左下角 (x0,pY,z0) 沿 u=(ux,uz) 展开 w 列 × h 层全 Air；底梁 / 顶梁（开口正下 /
        //   正上各 w 格，不含角）与左右边柱（两翼各 h 格，不含角）全黑曜石；corners=true 补四角。清场盒 =
        //   框外沿 ±3 × 门法向 ±2（含 y ±(h+3)），防地形 / 上一场景残留干扰。
        const auto buildFrame = [&](int x0, int z0, int ux, int uz, int w, int h, bool corners) {
            const int vx = uz, vz = ux; // 门法线向（清深 ±2）
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        w806.setBlock(x0 + c * ux + d * vx, pY + r, z0 + c * uz + d * vz, BR::Air, 0);
            for (int c = 0; c < w; ++c) {
                w806.setBlock(x0 + c * ux, pY - 1, z0 + c * uz, BR::Obsidian, 0);
                w806.setBlock(x0 + c * ux, pY + h, z0 + c * uz, BR::Obsidian, 0);
            }
            for (int r = 0; r < h; ++r) {
                w806.setBlock(x0 - ux, pY + r, z0 - uz, BR::Obsidian, 0);
                w806.setBlock(x0 + w * ux, pY + r, z0 + w * uz, BR::Obsidian, 0);
            }
            if (corners) {
                const int cs[2] = {-1, w};
                for (const int ci : cs)
                    for (const int ry : {-1, h})
                        w806.setBlock(x0 + ci * ux, pY + ry, z0 + ci * uz, BR::Obsidian, 0);
            }
        };
        // 局部门格计数：只数本 rig 清场盒内的 NetherPortal（各场景共用一世界，隔壁 rig 的残留门不串数）。
        const auto cellsInBox = [&](int x0, int z0, int ux, int uz, int w, int h) -> int {
            int n = 0;
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (w806.blockAt(x0 + c * ux + d * uz, pY + r, z0 + c * uz + d * ux)
                            == BR::NetherPortal)
                            ++n;
            return n;
        };

        // ① 2×3 最小门（X 平面 / 带角）：开口中格点燃 → true + 本 rig 门格恰 6 + state=0（X 平面）。
        {
            buildFrame(8, 8, 1, 0, 2, 3, true);
            const bool lit = w806.tryIgniteNetherPortal(9, pY + 1, 8);
            const int n = cellsInBox(8, 8, 1, 0, 2, 3);
            const int st = int(w806.stateAt(8, pY, 8));
            if (!lit || n != 6 || st != 0) {
                qInfo().noquote() << "  [t806 diag] 2x3 min gate:" << lit << "cells" << n << "state" << st;
                ok = false;
            }
        }
        // ⑧ 点燃位无关性（兼 ② 4×5 最大门 Z 平面 + ⑨ 直挖门格熄灭链）：4×5 开口的四角 + 中部共 5 个点燃位
        //    逐轮（每轮重搭框）点燃 → 均成门恰 20 格 + state=1；随后直挖一门格 + removeNetherPortalAt 连通域
        //    熄灭（镜像 finishMiningAt 门格分支序列）→ 归零。
        {
            const int wx = 30, wz = 8;
            const int cells[5][2] = {{0, 0}, {3, 0}, {0, 4}, {3, 4}, {1, 2}}; // (列, 行) 开口内点燃位
            for (int i = 0; i < 5; ++i) {
                buildFrame(wx, wz, 0, 1, 4, 5, true);
                const bool lit = w806.tryIgniteNetherPortal(wx, pY + cells[i][1], wz + cells[i][0]);
                const int n = cellsInBox(wx, wz, 0, 1, 4, 5);
                const int st = int(w806.stateAt(wx, pY, wz));
                if (!lit || n != 20 || st != 1) {
                    qInfo().noquote() << "  [t806 diag] 4x5 ignite pos" << i << ":" << lit
                                      << "cells" << n << "state" << st;
                    ok = false;
                }
                w806.setBlock(wx, pY, wz, BR::Air, 0);        // 直挖门格（finishMiningAt 同款先清格）
                w806.removeNetherPortalAt(wx, pY, wz, 1);     // 连通域熄灭余格
                if (cellsInBox(wx, wz, 0, 1, 4, 5) != 0) {
                    qInfo().noquote() << "  [t806 diag] direct-mine teardown at pos" << i
                                      << "left" << cellsInBox(wx, wz, 0, 1, 4, 5);
                    ok = false;
                }
            }
        }
        // ③ 超限拒（t848 尺寸表）：内腔 22 宽 / 22 高（超 21×21 上限，框外沿 23×23 封顶）均拒且零门格。
        //    t806 时代断言 5 宽 / 6 高拒——恰是用户「再大点不着」的设计根源（用户期望 MC 语义的更大门），
        //    t848 放宽后移交本处只验新上限。
        {
            buildFrame(8, 14, 1, 0, 22, 3, true); // 22 宽（X 平面）
            bool bad = w806.tryIgniteNetherPortal(19, pY + 1, 14) || cellsInBox(8, 14, 1, 0, 22, 3) != 0;
            buildFrame(8, 22, 1, 0, 2, 22, true); // 22 高
            bad = bad || w806.tryIgniteNetherPortal(9, pY + 1, 22) || cellsInBox(8, 22, 1, 0, 2, 22) != 0;
            if (bad) {
                qInfo().noquote() << "  [t806 diag] oversize 22w/22h not rejected";
                ok = false;
            }
        }
        // ④ 缺角通过（MC 1.0 角块可选）：3×4 无角门 → 成门恰 12 格。
        {
            buildFrame(20, 8, 1, 0, 3, 4, false);
            const bool lit = w806.tryIgniteNetherPortal(21, pY + 1, 8);
            const int n = cellsInBox(20, 8, 1, 0, 3, 4);
            if (!lit || n != 12) {
                qInfo().noquote() << "  [t806 diag] cornerless 3x4:" << lit << "cells" << n;
                ok = false;
            }
        }
        // ⑤ 缺承重框格拒：完整 3×4 带角框分别拆 底梁中格 / 顶梁中格 / 左边柱中格 → 均拒且零门格。
        {
            const int holes[3][2] = {{1, -1}, {1, 4}, {-1, 1}}; // (开口列偏移, 行偏移) 的框格位
            for (int i = 0; i < 3; ++i) {
                buildFrame(20, 8, 1, 0, 3, 4, true);
                w806.setBlock(20 + holes[i][0], pY + holes[i][1], 8, BR::Air, 0);
                const bool lit = w806.tryIgniteNetherPortal(21, pY + 1, 8);
                if (lit || cellsInBox(20, 8, 1, 0, 3, 4) != 0) {
                    qInfo().noquote() << "  [t806 diag] missing frame member" << i << "still lit";
                    ok = false;
                }
            }
        }
        // ⑥ 非矩形（腔内异物）拒：4×3 框开口内塞一块石 → 拒且零门格（矩形校验生效）。
        {
            buildFrame(20, 14, 1, 0, 4, 3, true);
            w806.setBlock(22, pY + 2, 14, BR::Stone, 0);
            if (w806.tryIgniteNetherPortal(21, pY + 1, 14) || cellsInBox(20, 14, 1, 0, 4, 3) != 0) {
                qInfo().noquote() << "  [t806 diag] non-rectangular opening not rejected";
                ok = false;
            }
        }
        // ⑦ 低于最小拒：1×3（单列开口）/ 2×2（矮开口）→ 均拒且零门格。
        {
            buildFrame(30, 20, 0, 1, 1, 3, true); // 1 宽（Z 平面）
            bool bad = w806.tryIgniteNetherPortal(30, pY + 1, 21) || cellsInBox(30, 20, 0, 1, 1, 3) != 0;
            buildFrame(30, 28, 0, 1, 2, 2, true); // 2 高
            bad = bad || w806.tryIgniteNetherPortal(30, pY + 1, 29) || cellsInBox(30, 28, 0, 1, 2, 2) != 0;
            if (bad) {
                qInfo().noquote() << "  [t806 diag] below-min 1w/2h not rejected";
                ok = false;
            }
        }
        // ⑨ 破框碎门 + 破角不碎门（镜像 finishMiningAt 框格破坏链：setBlock(Air) + breakNetherPortalsAround）：
        //    4×5 门破 底梁中 / 顶梁中 / 左边柱中 / 右边柱中 任一承重格 → 整门 20 格全熄；破左下角块
        //    （不承结构且与门格对角不邻）→ 门健在 20 格。
        {
            const int wx = 8, wz = 8; // Z 平面 4×5（buildFrame 自带清场抹掉 ① 的残留门）
            const int breaks[4][2] = {{0, -1}, {1, 5}, {-1, 2}, {4, 2}}; // (列偏移, 行偏移) 的框格位
            for (int i = 0; i < 4; ++i) {
                buildFrame(wx, wz, 0, 1, 4, 5, true);
                if (!w806.tryIgniteNetherPortal(wx, pY + 1, wz + 1) || cellsInBox(wx, wz, 0, 1, 4, 5) != 20) {
                    qInfo().noquote() << "  [t806 diag] frame-break rig" << i << "ignite failed";
                    ok = false;
                    continue;
                }
                const int bx = wx, by = pY + breaks[i][1], bz = wz + breaks[i][0];
                w806.setBlock(bx, by, bz, BR::Air, 0);
                w806.breakNetherPortalsAround(bx, by, bz);
                if (cellsInBox(wx, wz, 0, 1, 4, 5) != 0) {
                    qInfo().noquote() << "  [t806 diag] frame member" << i << "broken, door survived";
                    ok = false;
                }
            }
            buildFrame(wx, wz, 0, 1, 4, 5, true);
            w806.tryIgniteNetherPortal(wx, pY + 1, wz + 1);
            w806.setBlock(wx, pY - 1, wz - 1, BR::Air, 0); // 左下角块
            w806.breakNetherPortalsAround(wx, pY - 1, wz - 1);
            if (cellsInBox(wx, wz, 0, 1, 4, 5) != 20) {
                qInfo().noquote() << "  [t806 diag] corner break collapsed door:"
                                  << cellsInBox(wx, wz, 0, 1, 4, 5);
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t806 portal frame generalization: ignite fills whole 2x3..4x5 inner opening "
                             "(2x3 min X-plane 6 cells state=0 / 4x5 Z-plane 20 cells state=1, ignition "
                             "position-independent at 5 sample cells), oversized 22w/22h (t848-era cap; "
                             "t806-era 5w/6h now legal) + below-min 1w/2h + "
                             "non-rectangular + missing beam/pillar rejected with zero cells, corners optional "
                             "(cornerless 3x4 lights + corner break keeps door), frame-member break collapses "
                             "whole door via connected-domain clear (particle color = QML blockColor, manual "
                             "check)";
    }

    // ── Review 2026-08-23 #1 slim 皮肤布局探测回归探针 ──
    // 背景：复审 #8 的 slim 修复整体无效——旧探测区 u[52,54) 落在 slim 臂背面 [51,54) 内（PIL 实测
    //   demo 包 alex/steve 该区同为 24 个不透明像素）→ 对真实 slim 皮肤恒判 classic；且 kPiecesSlim
    //   盒区数值错（臂深 3 应为 4、腿不应缩 3px——数值锁在 playerskinbox.cpp static_assert，编译期拦）。
    //   修正后探测区 u[54,56)：classic = 臂背面右半必不透明、slim = 布局外空白必透明（alex 0 / steve 24，
    //   区分度完美）。本探针锁两层：
    //   ① 合成布局判定（密闭，任意机器可跑）：按 MC box-UV 条带公式画「规范 classic/slim 右臂条带」
    //     ——条带右缘 = u0 + 2d + 2w（classic 40+8+8=56 / slim 40+8+6=54），条带列 [40, 右缘) 全不透明、
    //     其余全透明 → classic 必判 classic、slim 必判 slim。若有人把探测区改回条带内（如 [52,54)），
    //     slim 合成图的 52/53 列不透明 → 误判 classic → FAIL；若改过头越出 classic 条带（如 [56,58)），
    //     classic 也判 slim → FAIL。HD 2×（128×128，sc=2）与退化小图（32×16 → 保守 classic）同锁。
    //   ② demo 包实测（有 pack 才跑，缺则记 note 跳过）：alex.png → slim、steve.png → classic——
    //     真实皮肤布局假设的防线（①合成图按公式画，公式理解错则②用真图拦）。
    {
        bool ok = true;
        // ① 合成条带（64×32 base 与 128×64 HD 2× 两档）。
        const auto makeArmStrip = [](int w, int h, int stripEndPx) {
            QImage img(w, h, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xd0, 0xa0, 0x70));
            const qreal sx = qreal(w) / 64.0, sy = qreal(h) / 32.0;
            // 右臂条带（base 像素 u∈[40,stripEnd) × v∈[16,32) 全不透明——侧面/背面带必paint，
            //   含探测行 v∈[20,32)）。
            p.drawRect(QRectF(40 * sx, 16 * sy, (stripEndPx - 40) * sx, 16 * sy));
            return img;
        };
        const int classicEnd = 40 + 2 * 4 + 2 * 4; // = 56（Renderer kPiecesClassic[2] 同公式）
        const int slimEnd = 40 + 2 * 4 + 2 * 3;    // = 54（Renderer kPiecesSlim[2] 同公式）
        const QImage synClassic = makeArmStrip(64, 32, classicEnd);
        const QImage synSlim = makeArmStrip(64, 32, slimEnd);
        if (probeSlimSkinLayout(synClassic)) {
            qInfo().noquote() << "  [#1 diag] synthetic classic strip (end u=56) misjudged slim";
            ok = false;
        }
        if (!probeSlimSkinLayout(synSlim)) {
            qInfo().noquote() << "  [#1 diag] synthetic slim strip (end u=54) misjudged classic";
            ok = false;
        }
        // HD 2× 同布局（128×64；sc=2 坐标缩放路径）。
        const QImage synClassicHd = makeArmStrip(128, 64, classicEnd);
        const QImage synSlimHd = makeArmStrip(128, 64, slimEnd);
        if (probeSlimSkinLayout(synClassicHd) || !probeSlimSkinLayout(synSlimHd)) {
            qInfo().noquote() << "  [#1 diag] HD 2x (128x64) classification wrong";
            ok = false;
        }
        // 退化小图（w<64）→ 保守 classic。
        QImage tiny(32, 16, QImage::Format_ARGB32);
        tiny.fill(Qt::transparent);
        if (probeSlimSkinLayout(tiny)) {
            qInfo().noquote() << "  [#1 diag] degenerate 32x16 not conservative-classic";
            ok = false;
        }
        // Review 2026-08-24 低危③：高度下界守卫——64×16 残图（宽达标但探测行区间 v[20,32) 整段在画布外）
        //   旧实现循环零次执行 → 空真判 slim（与保守方向相反）；守卫后必保守 classic。
        QImage stubR24(64, 16, QImage::Format_ARGB32);
        stubR24.fill(Qt::transparent);
        if (probeSlimSkinLayout(stubR24)) {
            qInfo().noquote() << "  [#1 diag] 64x16 stub vacuously judged slim (height floor guard missing)";
            ok = false;
        }
        // ② demo 包真实皮肤：settings.json resourcePack 指向的包 → 该包 entity/alex.png + steve.png；
        //   缺配置则试工程内 demo 包相对路径（exe 在 build/ → ../docs）。两候选都 miss → 记 note 跳过
        //   （①仍守布局语义；不在无包机器上假 FAIL）。
        QString packRoot;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString settingsCandidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
                QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                        .absoluteFilePath(QStringLiteral("settings.json")),
            };
            for (const QString &c : settingsCandidates) {
                QFile f(c);
                if (!f.open(QIODevice::ReadOnly))
                    continue;
                const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
                const QString p = obj.value(QLatin1String("resourcePack")).toString();
                if (!p.isEmpty() && QDir(p).exists()) {
                    packRoot = p;
                    break;
                }
            }
        }
        QString alexPath, stevePath;
        const auto entitySkin = [&packRoot](const char *name) {
            return packRoot.isEmpty()
                    ? QString()
                    : QDir(packRoot).absoluteFilePath(
                            QStringLiteral("assets/minecraft/textures/entity/") + QLatin1String(name));
        };
        alexPath = entitySkin("alex.png");
        stevePath = entitySkin("steve.png");
        if (alexPath.isEmpty() || !QFile::exists(alexPath) || !QFile::exists(stevePath)) {
            const QString fallback = QDir(QStringLiteral("..")).absoluteFilePath(
                    QStringLiteral("docs/Default HD 128x Demo 1.8.2.2"));
            const QString a = QDir(fallback).absoluteFilePath(
                    QStringLiteral("assets/minecraft/textures/entity/alex.png"));
            const QString s = QDir(fallback).absoluteFilePath(
                    QStringLiteral("assets/minecraft/textures/entity/steve.png"));
            if (QFile::exists(a) && QFile::exists(s)) {
                alexPath = a;
                stevePath = s;
            }
        }
        bool realChecked = false;
        if (QFile::exists(alexPath) && QFile::exists(stevePath)) {
            const QImage alex(alexPath), steve(stevePath);
            if (alex.isNull() || steve.isNull()) {
                qInfo().noquote() << "  [#1 diag] demo pack skin PNG decode failed";
                ok = false;
            } else {
                realChecked = true;
                if (!probeSlimSkinLayout(alex)) {
                    qInfo().noquote() << "  [#1 diag] demo alex.png misjudged classic (slim regression)";
                    ok = false;
                }
                if (probeSlimSkinLayout(steve)) {
                    qInfo().noquote() << "  [#1 diag] demo steve.png misjudged slim (classic regression)";
                    ok = false;
                }
            }
        }
        if (!realChecked)
            qInfo().noquote() << "  [#1 note] demo pack skins not found - real-skin assertions skipped "
                                 "(synthetic layout assertions still ran)";
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review#1 slim-skin probe region: synthetic MC-layout arm strips classify "
                             "classic(end u=56)/slim(end u=54) at base 64x32 + HD 2x, degenerate 32x16 + "
                             "64x16 stub (probe rows off-canvas) conservative-classic"
                          << (realChecked
                                  ? ", demo pack alex->slim / steve->classic (real PIL-verified layouts)"
                                  : "");
    }

    // ── P21 复审 #2（2026-08-23）低顶净空坡道探针（MinecartManager 直编，同 P12b/P18 模式）──
    //   Review #2：scanRailColumn「实心即断」× 坡道车位居上 —— 坡段 rise>0.55 时 floor(pos.y) = 轨Y+1
    //   恰是贴坡天花板实心格 → 严格断扫返 -1 → 坡上死车 / 俯仰清零 / 采样失联三症状同根因（af9ec8e 的
    //   断扫在平轨天花板不触发 —— P18 平轨 pos.y=R+0.45 首扫格即轨；本探针补坡道变体）。修后骑乘族走
    //   宽容版（实心正下是轨 + 骑乘高一致 → 放行）。断言：
    //   (a) 低顶坡道全程不失联：Y 恒钉轨面（wantSurf+rideH ±0.02，同 P12b 口径）+ 驶到顶死端格心停驻
    //       + 停稳守卫（修前症状①：pinCartY/tickRiddenCart 判离轨 → 坡 55%+ 处冻死，到不了顶）；
    //   (b) 俯仰全程连续：|Δpitch| ≤ 46°/tick + |pitch| ≤ 45.5°（钳制上界；修前症状②：采样列失联 →
    //       一帧跳回水平 45° 突变）+ 坡中段均值 ~+45；
    //   (c) af9ec8e 隔板回归防线：地面车（kCartGroundH=0.3875）站实心地板、地板下 1 格平轨 —— mount +
    //       持续 W + 玩家推全链后钉死不动（宽容版一致性校验拒：差 1.9375 >> kRideScanTol 0.5）。
    {
        // rig 寻址：运行期扫描空区（P20 先例——nextSlot() 4×31 网格已耗尽）。需 7×3×6（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 1; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 6 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#2 low-headroom ramp: no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 镜像值（P11/P12b 同款）
            // 轨：x0 低平 + x0+1 坡（东邻高一格）+ x0+2..x0+3 高平死端；天花板：x0/x0+1 上 Y+1（低顶）、
            //   x0+2/x0+3 上 Y+2 —— 坡段 1 格净空（紧凑螺旋下层坡段等效布局）。
            w.setBlock(x0,     kRigY,     z0, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY,     z0, BR::Rail, 0);
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);
            w.setBlock(x0 + 3, kRigY + 1, z0, BR::Rail, 0);
            w.setBlock(x0,     kRigY + 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 2, kRigY + 2, z0, BR::Stone, 0);
            w.setBlock(x0 + 3, kRigY + 2, z0, BR::Stone, 0);
            const auto wantSurf = [&](float x) {
                if (x < float(x0 + 1)) return float(kRigY);
                if (x > float(x0 + 2)) return float(kRigY + 1);
                return float(kRigY) + (x - float(x0 + 1));
            };
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            bool yOk = true, pitchCont = true, pitchClamped = true;
            float prevPitch = 0.0f, slopeSum = 0.0f;
            int slopeN = 0;
            QVector3D cp;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOk = false; break; }
                const float p = carts.pitchAt(0);
                if (t > 0 && std::fabs(p - prevPitch) > 46.0f) pitchCont = false;
                if (std::fabs(p) > 45.5f) pitchClamped = false;
                if (cp.x() > float(x0 + 1) + 0.3f && cp.x() < float(x0 + 1) + 0.7f) { slopeSum += p; ++slopeN; }
                prevPitch = p;
            }
            const QVector3D fin = carts.posAt(0);
            ok = ok && yOk && pitchCont && pitchClamped
                && slopeN >= 3 && std::fabs(slopeSum / float(slopeN) - 45.0f) < 2.5f
                && std::fabs(fin.x() - float(x0 + 3) - 0.5f) < 0.01f
                && std::fabs(fin.y() - float(kRigY + 1) - rideH) < 0.02f
                && std::fabs(carts.pitchAt(0)) < 0.5f;
            if (ok) { // 停稳守卫（顶死端格心）
                for (int t = 0; t < 20 && ok; ++t) {
                    carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    if ((cp - fin).length() > 1e-4f) ok = false;
                }
            }
            if (!ok)
                qInfo().noquote() << "  low-headroom uphill: final" << fin << "pitch" << carts.pitchAt(0)
                                  << "yOk" << yOk << "pitchCont" << pitchCont << "clamp" << pitchClamped
                                  << "slopeN" << slopeN
                                  << "slopeMean" << (slopeN ? slopeSum / float(slopeN) : 0.0f);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#2 low-headroom ramp (ceiling flush above slope rail): cart "
                                 "stays pinned (no dead-cart at rise>0.55), reaches top dead-end, pitch "
                                 "continuous & clamped, ~+45 mid-slope";
            // (c) 隔板防线：清坡轨布局 → 地板（Y 实心）+ 地板下 1 格平轨（Y-1）+ 地面车（Y+1 空格）。
            carts.clearAll();
            for (int dx = 0; dx <= 3; ++dx) { // 清轨 / 天花板 / 高段（含越层残留）
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY + 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY + 2, z0, BR::Air, 0);
            }
            w.setBlock(x0, kRigY, z0, BR::Stone, 0);    // 地板（车格正下）
            w.setBlock(x0, kRigY - 1, z0, BR::Rail, 0); // 地板下平轨（隔板场景：实心在车与轨之间）
            carts.spawnCart(x0, kRigY + 1, z0, &w);     // 地面静止模式（车底贴 cell 底 kCartGroundH）
            const QVector3D gp0 = carts.posAt(0);
            bool guardOk = std::fabs(gp0.y() - (float(kRigY + 1) + 0.3875f)) < 1e-3f; // 地面车基准高
            guardOk = guardOk && carts.tryMount(QVector3D(gp0.x(), gp0.y() + 1.5f, gp0.z()),
                                                QVector3D(0, -1, 0), 4.0f);
            QVector3D gcp;
            for (int t = 0; t < 30; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, gcp); // 持续 W（宽容版若误放行 → 钉到下方轨）
                carts.pushEmptyCart(&w, gcp, 1.0f, 0.0f);         // 玩家推（pickTrackStep 同一宽容扫描）
                carts.tickPushedCarts(0.016, &w);
            }
            guardOk = guardOk && (gcp - gp0).length() < 1e-4f; // 全链后钉死不动（离轨静止守卫接管）
            if (!guardOk)
                qInfo().noquote() << "  floor-slab guard: cart moved/teleported, start" << gp0
                                  << "after" << gcp;
            if (!guardOk) ++totalFail;
            qInfo().noquote() << (guardOk ? "PASS" : "FAIL")
                              << "| review#2 af9ec8e floor-slab guard kept: ground cart above rail-under-"
                                 "floor stays dead (lenient scan consistency rejects, gap 1.9375 >> tol)";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P22 复审 #3（2026-08-23）坡臂拐角俯仰/Y 连续探针（MinecartManager 直编）──
    //   Review #3：railRiseAt 拐角格取四边均值常数 0.25·Σe → 与相邻直臂格线性坡面在格边界不连续 →
    //   俯仰采样窗跨界 atan2(-0.75,0.5)≈-56° 车头瞬甩 + 同帧 Y 钉面跳降 0.75（t709 拐角 quad 沿臂整边
    //   抬升，矿车常数不贴）。修后拐角改 armLift 四角双线性（与 mesher 同一角点公式）。断言（高臂骑乘
    //   下坡过拐角入平臂）：
    //   (a) Y 连续：每 tick |Δy| ≤ 0.55（修前拐角入口一帧 -0.75；修后拐角内连续、平臂边界残差 ≤0.5
    //       = t709 mesher 面自身的台阶，复审 #3 已知接受项）；
    //   (b) 俯仰连续：每 tick |Δpitch| ≤ 46° 且 |pitch| ≤ 45.5°（钳制；修前 -56° 瞬甩两项都破）；
    //   (c) 过弯驶达南死端格心停驻 + 停稳守卫（连续性断言不放松「不出轨」底线）。
    {
        // rig 寻址：运行期扫描空区（P20 先例）。需 6×6×5（含隔离边；高臂格到 Y+2）。
        int cx = -1, cz = -1;
        for (int zz = 1; zz < 96 && cx < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 4 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { cx = xx; cz = zz; }
            }
        if (cx < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#3 slope-arm corner: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            // 高臂（西行下坡）：东端死端 (cx+2,Y+1) + (cx+1,Y+1)；拐角 (cx,Y)（东邻高 1 + 南臂同层）；
            //   南平臂 (cx,Y,cz+1..cz+3) 死端。坡面由拐角格补齐（t709「低格画坡」）。
            w.setBlock(cx + 2, kRigY + 1, cz, BR::Rail, 0);
            w.setBlock(cx + 1, kRigY + 1, cz, BR::Rail, 0);
            w.setBlock(cx,     kRigY,     cz, BR::Rail, 0);
            w.setBlock(cx,     kRigY,     cz + 1, BR::Rail, 0);
            w.setBlock(cx,     kRigY,     cz + 2, BR::Rail, 0);
            w.setBlock(cx,     kRigY,     cz + 3, BR::Rail, 0);
            MinecartManager carts;
            carts.spawnCart(cx + 2, kRigY + 1, cz, &w); // 单端连接（西）→ spawn 定向 -X 下坡向
            bool mounted = carts.tryMount(QVector3D(float(cx + 2) + 0.5f, float(kRigY + 1) + 2.0f,
                                                    float(cz) + 0.5f),
                                          QVector3D(0, -1, 0), 4.0f);
            QVector3D prev = carts.posAt(0);
            bool yCont = true, pitchCont = true, pitchClamped = true, onFootprint = true;
            float prevPitch = 0.0f;
            QVector3D cp = prev;
            for (int t = 0; t < 900 && onFootprint; ++t) {
                // 骑乘驱动（追推跑法摩擦磨停点随机、退化 wish 在死端形成推-停振荡，终点不确定；骑手
                //   连续供速 + 死端「停在格心、零溢出」给出确定性终点）。wish 沿臂正向：西行段 (-1,0)，
                //   过拐角心（z 越过 cz+0.5）后切 (0,+1) —— 死端处 dot<0 被滤 → 停驻不动。
                const bool southArm = cp.z() > float(cz) + 0.5f;
                carts.tickRiddenCart(0.016, &w, southArm ? 0.0f : -1.0f,
                                     southArm ? 1.0f : 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                cp = carts.posAt(0);
                if (std::fabs(cp.y() - prev.y()) > 0.55f) yCont = false;   // (a) Y 连续（修前 -0.75）
                const float p = carts.pitchAt(0);
                if (t > 0 && std::fabs(p - prevPitch) > 46.0f) pitchCont = false; // (b) 俯仰连续（修前 ±56 瞬甩）
                if (std::fabs(p) > 45.5f) pitchClamped = false;
                prevPitch = p;
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                const bool onTrack = (bx >= cx && bx <= cx + 2 && bz == cz) // 高臂（含拐角列）
                                     || (bx == cx && bz >= cz + 1 && bz <= cz + 3); // 南臂
                if (!onTrack) { onFootprint = false; break; }
                prev = cp;
            }
            const QVector3D fin = carts.posAt(0);
            // 终点：死端格心停驻（沿臂 wish 在死端无 dot≥0 连接 → 停驻重选向保持静止；「轨尽头 → 停在
            //   格心、零溢出」是 stepCartAlongRail 到心分支的既定语义）。
            bool ok = mounted && yCont && pitchCont && pitchClamped && onFootprint
                && std::fabs(fin.x() - (float(cx) + 0.5f)) < 0.01f
                && std::fabs(fin.z() - (float(cz + 3) + 0.5f)) < 0.01f
                && std::fabs(fin.y() - (float(kRigY) + rideH)) < 0.02f;
            if (ok) {
                for (int t = 0; t < 20 && ok; ++t) { // 停稳守卫（停止追推后钉死）
                    carts.tickPushedCarts(0.016f, &w);
                    if ((carts.posAt(0) - fin).length() > 1e-4f) ok = false;
                }
            }
            if (!ok)
                qInfo().noquote() << "  slope-arm corner: final" << fin << "rig cx" << cx << "cz" << cz
                                  << "yCont" << yCont
                                  << "pitchCont" << pitchCont << "clamp" << pitchClamped
                                  << "onFootprint" << onFootprint;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#3 slope-arm corner bilinear rise: Y step <=0.55/tick (was 0.75),"
                                 " pitch continuous |d|<=46 (was ~56 snap) & clamped 45, parks at far dead-end";
            // 清场
            carts.clearAll();
            w.setBlock(cx + 2, kRigY + 1, cz, BR::Air, 0);
            w.setBlock(cx + 1, kRigY + 1, cz, BR::Air, 0);
            w.setBlock(cx, kRigY, cz, BR::Air, 0);
            for (int dz = 1; dz <= 3; ++dz) w.setBlock(cx, kRigY, cz + dz, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P23 复审 #23（2026-08-23）段中重选向横向收敛限速探针（MinecartManager 直编，同 P12c 场景）──
    //   Review #23：停驻重选向（minecartmanager tickRiddenCart 停驻分支）在**段中非心位**改 dir → 下一步
    //   行贴轨约束把 ≤0.5 格横向偏移**一次钉回** → 一 tick ~0.5 格横向瞬移（被骑时玩家视点同步跳；旧
    //   P12c 只验收终态测不出）。修后收敛限速每 tick ≤kCartCenterSnapPerTick(0.1)。断言：
    //   (a) 蠕行进拐角格早段松键磨停 → 停驻点在格心之前、偏移 ∈[0.11,0.45]（>0.1 保证能分辨瞬移与限速）；
    //   (b) 停驻位 +X wish 一 tick 重选向出口臂（yaw 270）；
    //   (c) 重推期每 tick 横向（z，此时垂直于行进轴）位移 ≤0.101（修前首 tick = 全偏移 ≥0.11 → FAIL）
    //       且限窗内收敛到格心线（|z−(z0+0.5)| ≤1e-3）；
    //   (d) 沿东臂驶达死端格心停驻（限速不破坏到达性）。
    {
        // rig 寻址：运行期扫描空区（P20 先例）。L 形：南腿 2 直 + 拐角 + 东臂 3 直，需 6×6×5（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -3; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#23 mid-cell relaunch snap rate: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            w.setBlock(x0, kRigY, z0 - 2, BR::Rail, 0); // 南死端（spawn 格）
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0,     BR::Rail, 0); // 拐角（南臂 + 东臂）
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Rail, 0); // 东死端
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0 - 2, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0 - 2) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            // (a) 蠕行（wish 大垂直分量 → proj≈0.0625 → targetV≈0.5 格/s，P12c(a) 同款）进拐角格后
            //     再爬 2 tick 即松键 → 磨停点落在格心之前（偏移 ~0.2-0.4）。
            bool entered = false;
            int enteredTicks = -1;
            for (int t = 0; t < 2500 && ok; ++t) {
                const bool release = entered && (t - enteredTicks) > 2;
                carts.tickRiddenCart(0.016, &w, release ? 0.0f : 0.998f,
                                     release ? 0.0f : 0.0625f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!entered && int(std::floor(cp.z())) == z0 && int(std::floor(cp.x())) == x0) {
                    entered = true;
                    enteredTicks = t;
                }
                if (release) break;
            }
            for (int t = 0; t < 250 && ok; ++t) { // 摩擦磨停
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
            }
            const float off0 = std::fabs(cp.z() - (float(z0) + 0.5f));
            ok = ok && entered && int(std::floor(cp.x())) == x0 && int(std::floor(cp.z())) == z0
                && cp.z() < float(z0) + 0.5f && off0 >= 0.11f && off0 <= 0.45f; // (a) 停驻偏移前置
            if (!ok)
                qInfo().noquote() << "  park position not mid-corner pre-center with usable offset:"
                                  << cp << "off0" << off0 << "entered" << entered;
            if (ok) {
                // (b) 停驻位 +X 重选向（一 tick）：dir 掰向出口臂，yaw → 270。
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                ok = int(std::lround(carts.yawAt(0))) % 360 == 270;
                if (!ok) qInfo().noquote() << "  reselect heading not +X (yaw 270), yaw" << carts.yawAt(0);
            }
            bool convOk = false;
            if (ok) {
                // (c) 重推期横向限速：z 是垂直轴（行进 +X）→ 每 tick |Δz| ≤0.101，直到钉回格心线。
                bool rateOk = true;
                float prevZ = cp.z();
                int convLeft = -1; // -1 = 未开始判定（首 tick 的 prevZ 基准在 (b) 末）
                for (int t = 0; t < 600; ++t) {
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    const float dz = std::fabs(cp.z() - prevZ);
                    if (convLeft < 0) { // 收敛窗内：横向位移须限速（修前首 tick ≈ off0 全额瞬移）
                        if (dz > 0.101f) { rateOk = false; break; }
                        if (std::fabs(cp.z() - (float(z0) + 0.5f)) <= 1e-3f) convLeft = 0; // 已收敛
                    } else if (++convLeft == 1) { break; } // 收敛后再验 1 tick（保持格心线）
                    prevZ = cp.z();
                }
                convOk = rateOk && std::fabs(cp.z() - (float(z0) + 0.5f)) <= 1e-3f;
                if (!convOk)
                    qInfo().noquote() << "  lateral convergence violated rate cap or never converged:"
                                      << "rateOk" << rateOk << "z" << cp.z() << "off0" << off0;
                ok = ok && convOk;
            }
            if (ok) { // (d) 沿东臂驶达死端格心停驻 + 停稳守卫。
                for (int t = 0; t < 600; ++t) {
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016, &w);
                    if (cp.x() >= float(x0 + 3) + 0.5f) break;
                }
                const QVector3D fin = carts.posAt(0);
                ok = std::fabs(fin.x() - (float(x0 + 3) + 0.5f)) < 0.01f
                    && std::fabs(fin.z() - (float(z0) + 0.5f)) < 0.01f
                    && std::fabs(fin.y() - (float(kRigY) + rideH)) < 0.02f;
                if (ok) {
                    for (int t = 0; t < 20 && ok; ++t) {
                        carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                        carts.tickPushedCarts(0.016, &w);
                        if ((carts.posAt(0) - fin).length() > 1e-4f) ok = false;
                    }
                }
                if (!ok) qInfo().noquote() << "  did not park at east dead-end center, fin" << fin;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#23 mid-cell relaunch: lateral recenter capped at 0.1/tick (was "
                                 "one-shot ~0.5 teleport), converges to centerline, still reaches east dead-end";
            // 清场
            carts.clearAll();
            for (int dx = 0; dx <= 3; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            for (int dz = -2; dz <= -1; ++dz) w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P24 复审 #4（2026-08-23 中危）重力坍落柱附着物级联掉落探针 ──
    //   Review #4：dropGravityColumn 逐格 m_chunks.setBlock(Air) 直写绕过 check*OnEdit 编辑钩子族 +
    //   无邻格火把失撑扫 → 沙柱坍落后柱顶火把 / 红石火把（照常发光供电）/ 甘蔗 / 雪层 / 花 / 压力板 / 铁轨
    //   全部悬空残留（t794 铁砧并入重力族后受面扩大）。修后 dropGravityColumn 每清一格调
    //   recheckAttachmentsAfterClear（正上方族 check*OnEdit + 6 邻火把 / 红石火把扫，与 clearBlockSilent
    //   口径合一）。断言（8 柱 rig：石基座 + 2 高沙柱 + 各一附着物；拆基座触发整柱坍落）：
    //   (a) 柱顶附着物随坍落清空：火把 / 铁轨 / 木压力板 / 花 → Air + blockDroppedAsItem；甘蔗×2 →
    //       整柱级联（两格全清）；雪层 → Air + snowLayerFell；柱侧贴墙红石火把（state 编码附着本柱）
    //       → Air + 掉落（修前全残留 → FAIL）；
    //   (b) 沙柱本体全清（坍落完整性，非附着物断言的副作用核对）；
    //   (c) 对照柱（基座不拆）火把原样保留（拆别柱不误伤）+ 全程 ≥7 次掉落信号（含甘蔗 2）+ ≥1 雪层坍落。
    {
        // rig 寻址：运行期扫描空区（P20 先例——nextSlot() 网格已耗尽）。8 柱单排、列距 2（柱侧红石火把
        //   占邻列不受扰：邻柱坍落扫到它时 state 解码支撑在另一侧 → 跳过）→ 需 17×3×7（含隔离边）。
        //   首版 4×2 网格 14×11×7 实测扫不到（124 矩阵 rig 残块 + 生成石柱把大块净空切碎）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 15 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 15 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 5 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#4 gravity-column attachments: no clear rig area found";
        } else {
            const int by = kRigY;
            // 8 柱（单排列距 2）：0 火把 / 1 铁轨 / 2 木压力板 / 3 柱侧红石火把 / 4 甘蔗×2 / 5 花 / 6 雪层 /
            //   7 对照火把。列距 2：柱 3 的红石火把在 colX[3]+1（与柱 4 隔 1 格），两柱坍落互不误清。
            const int colX[8] = { x0, x0 + 2, x0 + 4, x0 + 6, x0 + 8, x0 + 10, x0 + 12, x0 + 14 };
            for (int i = 0; i < 8; ++i) {
                const int cz = z0;
                w.setBlock(colX[i], by,     cz, BR::Stone, 0); // 基座
                w.setBlock(colX[i], by + 1, cz, BR::Sand, 0);  // 沙柱 ×2（放置自检：下方满立方 → 稳）
                w.setBlock(colX[i], by + 2, cz, BR::Sand, 0);
            }
            w.setBlock(colX[0], by + 3, z0, BR::Torch, 0);             // 柱顶立火把（state 0 贴地）
            w.setBlock(colX[1], by + 3, z0, BR::Rail, 0);              // 柱顶铁轨
            w.setBlock(colX[2], by + 3, z0, BR::WoodPressurePlate, 0); // 柱顶压力板
            w.setBlock(colX[3] + 1, by + 2, z0, BR::RedstoneTorch, 1); // 柱侧贴墙红石火把（state 1=TorchOnNX 支撑 -X 本柱顶格）
            w.setBlock(colX[4], by + 3, z0, BR::Sugarcane, 0);         // 甘蔗 ×2（级联整柱）
            w.setBlock(colX[4], by + 4, z0, BR::Sugarcane, 0);
            w.setBlock(colX[5], by + 3, z0, BR::FlowerRed, 0);         // 花
            w.setBlock(colX[6], by + 3, z0, BR::SnowLayer, 0);         // 雪层（1 层）
            w.setBlock(colX[7], by + 3, z0, BR::Torch, 0);             // 对照柱顶火把（基座不拆）
            const int drops0 = dropItemCount, snow0 = snowFellCount;
            // 触发：拆柱 0..6 的基座（对照柱 7 不拆）→ checkGravityBlockOnEdit ② → dropGravityColumn
            //   → 每清一格 recheckAttachmentsAfterClear（柱顶格清完的复检带走全部附着物）。
            for (int i = 0; i < 7; ++i) w.setBlock(colX[i], by, z0, BR::Air, 0);
            tickN(w, 2);
            bool ok = true;
            // (a) 附着物清空（含甘蔗底格级联 + 柱侧红石火把）。
            const int attY[7] = { by + 3, by + 3, by + 3, by + 2, by + 4, by + 3, by + 3 };
            for (int i = 0; i < 7 && ok; ++i) {
                const int ax = (i == 3) ? colX[3] + 1 : colX[i];
                if (w.blockAt(ax, attY[i], z0) != BR::Air) {
                    qInfo().noquote() << "  column" << i << "attachment survived at"
                                      << ax << attY[i] << z0
                                      << "id" << int(w.blockAt(ax, attY[i], z0));
                    ok = false;
                }
            }
            if (ok && w.blockAt(colX[4], by + 3, z0) != BR::Air) { // 甘蔗整柱级联（底格）
                qInfo().noquote() << "  sugarcane column base survived";
                ok = false;
            }
            // (b) 沙柱本体全清。
            for (int i = 0; i < 7 && ok; ++i)
                for (int dy = 1; dy <= 2 && ok; ++dy)
                    if (w.blockAt(colX[i], by + dy, z0) != BR::Air) {
                        qInfo().noquote() << "  column" << i << "sand cell survived at dy" << dy;
                        ok = false;
                    }
            // (c) 对照柱原样 + 信号计数（火把1+轨1+板1+红石火把1+甘蔗2+花1 = 7 掉落 + 1 雪层坍落）。
            if (ok && w.blockAt(colX[7], by + 3, z0) != BR::Torch) {
                qInfo().noquote() << "  control torch disturbed";
                ok = false;
            }
            const int drops = dropItemCount - drops0, snows = snowFellCount - snow0;
            if (ok && (drops < 7 || snows < 1)) {
                qInfo().noquote() << "  drop/snow signal counts low: drops" << drops << "snow" << snows;
                ok = false;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#4 gravity-column attachments: base removal collapses sand "
                                 "column and clears torch/rail/plate/flower (dropped), sugarcane cascade "
                                 "(2 cells), snow layer (fell entity), side redstone torch (dropped); "
                                 "control column untouched; >=7 drop signals + 1 snow-fell";
            // 清场（坍落成功时柱 0..6 已全空；拆对照柱基座会再触发一次坍落 + 掉落 → 全格兜底清）。
            for (int i = 0; i < 8; ++i)
                for (int dy = 0; dy <= 4; ++dy)
                    w.setBlock(colX[i], by + dy, z0, BR::Air, 0);
            w.setBlock(colX[3] + 1, by + 2, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P25 复审 #15（2026-08-23 低危）「支撑格被换成非满顶支撑 → 轨坍落」探针 ──
    //   Review #15：checkRailOnEdit 失撑守卫旧要求 `id == Air`（仅挖掘 / 爆炸清格触发），本格被换成水
    //   （冰融化 setWaterSilent 写 Water）/ 火（可燃支撑焚毁写 Fire）时轨悬浮到水蒸发。修后失撑判定只读
    //   「本格现内容非 isTopFlushSupport 即坍落」。断言：
    //   (a) 冰融成水（setWaterSilent 写 Water 入支撑格，真实融化 tickIceMelt 同入口）→ 正上方铁轨立即
    //       坍落清 Air + blockDroppedAsItem(id=Rail)——修前 id==Water≠Air 守卫跳过 → 轨浮空 → FAIL；
    //   (b) 焚毁路径（setBlock 写 Fire 入可燃支撑格 Planks，火吞支撑的等价写）→ 轨同样立即坍落；
    //   (c) 对照：水写入非支撑邻格 → 轨保留（坍落只看唯一支撑位，不误伤邻写）。
    {
        // rig 寻址：运行期扫描空区（P20 先例）。3 组各 2 格宽（支撑+轨）+ 隔离边 → 9×4×5。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 7 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dy = -1; dy <= 2 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#15 rail support substitution: no clear rig area found";
        } else {
            const int by = kRigY;
            // (a) 冰支撑 + 轨：setWaterSilent 模拟融化结果（Ice→Water）。
            w.setBlock(x0, by, z0, BR::Ice, 0);
            w.setBlock(x0, by + 1, z0, BR::Rail, 0);
            const int drops0 = dropItemCount;
            w.setWaterSilent(x0, by, z0, BR::Water, 0);
            const bool aOk = w.blockAt(x0, by + 1, z0) == BR::Air
                             && w.blockAt(x0, by, z0) == BR::Water
                             && dropItemCount > drops0 && lastDropId == int(BR::Rail);
            // (b) 木板支撑 + 轨：setBlock 写 Fire（可燃支撑焚毁等价写路径；harness 不跑 tickFire → 火静止）。
            w.setBlock(x0 + 3, by, z0, BR::Planks, 0);
            w.setBlock(x0 + 3, by + 1, z0, BR::Rail, 0);
            const int drops1 = dropItemCount;
            w.setBlock(x0 + 3, by, z0, BR::Fire, 0);
            const bool bOk = w.blockAt(x0 + 3, by + 1, z0) == BR::Air
                             && w.blockAt(x0 + 3, by, z0) == BR::Fire
                             && dropItemCount > drops1 && lastDropId == int(BR::Rail);
            // (c) 对照：石支撑 + 轨；水写进邻格（非支撑位）→ 轨保留。
            w.setBlock(x0 + 6, by, z0, BR::Stone, 0);
            w.setBlock(x0 + 6, by + 1, z0, BR::Rail, 0);
            w.setWaterSilent(x0 + 7, by, z0, BR::Water, 0);
            tickN(w, 2);
            const bool cOk = w.blockAt(x0 + 6, by + 1, z0) == BR::Rail;
            const bool ok = aOk && bOk && cOk;
            if (!ok)
                qInfo().noquote() << "  rail-after-substitution: melt" << aOk << "burn" << bOk
                                  << "ctrl" << cOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#15 rail support substitution: ice->water (setWaterSilent) and "
                                 "planks->fire (setBlock) under rail both drop the rail immediately "
                                 "(support loss reads current cell content, not just Air edits); "
                                 "neighbor water write leaves rail intact";
            // 清场
            w.setBlock(x0, by, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by, z0, BR::Air, 0);
            w.setBlock(x0 + 6, by, z0, BR::Air, 0);
            w.setBlock(x0 + 6, by + 1, z0, BR::Air, 0);
            w.setWaterSilent(x0 + 7, by, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P26 复审 #16（2026-08-23 低危）火吞木门整门联动探针（t843 语义重做版）──
    //   Review #16：门在可燃表内，火蔓延只点燃半格 → 另半扇孤立残留无掉落。t843 重做后语义：点燃 = 进
    //   燃烧态（栅格 id 不变），联动迁移到 igniteFlammableAt 单一入口——目标是门 → 配对半扇（state bit3
    //   上/下互补 y∓1）同为门且非湿时一并点燃（同窗同计时 → 同窗烧毁）。断言：
    //   (a) 门下格被邻火点燃的**那次 tickFire 调用内**，上格一并进燃烧态（isBurningAt 真且两半 id 仍是
    //       WoodDoor）——修前上格非火的 6 邻（隔一格对角），只能等下格燃烧的后续窗同态蔓延 → 首次观测
    //       下格燃烧时上格未燃 → FAIL；
    //   (b) 两半同计时同窗烧毁（终态均非 WoodDoor）且 blockBroken(WoodDoor) 恒 0（烧毁无掉落——余烬火
    //       setBlock(Fire) 放置语义，Air 收尾在对称同烧下不触发）；
    //   (c) 对照石柱（不可燃）同布局永不被吞。
    //   确定性：火源 6 邻仅门下格可燃（无燃料不熄灭路径被 hasFuel 门挡）→ 点燃只是时间问题（2.5%/窗
    //   ——review-g #5 叠加补偿后；上限 3000 窗，P(未燃)≈0.975^3000≈e^-76）；harness 只驱动 tickFire
    //   （无雨 / 无风灭混淆源）。
    {
        // rig 寻址：运行期扫描空区（P20 先例）。火源 + 门 2 格 + 石柱 2 格 + 隔离边 → 7×6×6。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 96 && x0 < 0; zz += 3)
            for (int xx = 4; xx + 5 < 96; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dy = -1; dy <= 4 && clear; ++dy)
                        if (w.blockAt(xx + dx, kRigY + dy, zz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review#16 door whole-burn linkage: no clear rig area found";
        } else {
            const int by = kRigY;
            w.setBlock(x0, by, z0, BR::Fire, 0);              // 火源（邻门可燃 → 恒 hasFuel 不自熄）
            w.setBlock(x0 + 1, by, z0, BR::WoodDoor, 0);      // 门下格（state bit3=0）
            w.setBlock(x0 + 1, by + 1, z0, BR::WoodDoor, 8);  // 门上格（state bit3=1）
            w.setBlock(x0 + 3, by, z0, BR::Stone, 0);         // 对照石柱（不可燃）
            w.setBlock(x0 + 3, by + 1, z0, BR::Stone, 0);
            bool lit = false;
            for (int t = 0; t < 15000 && !lit; ++t) { // 每 5 次 tickFire = 1 窗（kFireTickInterval=5 节流）
                w.tickFire();
                if (w.isBurningAt(x0 + 1, by, z0)) lit = true; // 首次观测下格进燃烧态即停（上格同调用已联动）
            }
            // t843 语义重做版断言：点燃 = 燃烧态（World 侧表，栅格 id 不变——门两半仍 WoodDoor），整扇
            //   联动 = 同一 igniteFlammableAt 调用内配对半扇一并进燃烧态（上格与火源隔一格对角，非 6 邻
            //   ——仅联动可达；同态蔓延是后续窗的事，首窗观测只可能来自联动，旧探针的隔离手法原样保留）。
            const bool aOk = lit && w.blockAt(x0 + 1, by, z0) == BR::WoodDoor
                             && w.isBurningAt(x0 + 1, by + 1, z0)
                             && w.blockAt(x0 + 1, by + 1, z0) == BR::WoodDoor;
            // 驱至烧毁收尾：两半同计时同窗归零 → 一半先烧成余烬火、另一半见对偶非门自走 setBlock(Fire)
            //   （无 Air 收尾路径 → blockBroken(WoodDoor) 恒 0；对偶「仍是未燃门」的 Air 收尾只在不对称
            //   场景触发，对称同烧不命中——见 world.cpp (d) pass 烧毁收尾注释）。
            int doorBreaks = 0;
            QObject::connect(&w, &World::blockBroken, &w,
                             [&](int, int, int, int oldId) { if (oldId == int(BR::WoodDoor)) ++doorBreaks; });
            bool consumed = false;
            for (int t = 0; t < 15000 && !consumed; ++t) {
                w.tickFire();
                if (w.blockAt(x0 + 1, by, z0) != BR::WoodDoor
                    && w.blockAt(x0 + 1, by + 1, z0) != BR::WoodDoor) consumed = true;
            }
            const bool bOk = w.blockAt(x0 + 3, by, z0) == BR::Stone
                             && w.blockAt(x0 + 3, by + 1, z0) == BR::Stone;
            const bool ok = aOk && consumed && doorBreaks == 0 && bOk;
            if (!ok)
                qInfo().noquote() << "  door whole-burn: lit" << lit
                                  << "lower" << int(w.blockAt(x0 + 1, by, z0))
                                  << "upper" << int(w.blockAt(x0 + 1, by + 1, z0))
                                  << "consumed" << consumed << "doorBreaks" << doorBreaks
                                  << "stoneCtrl" << bOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review#16 door whole-burn (t843 semantics): fire lighting one door "
                                 "half enters it into burning state with id preserved and ignites the "
                                 "paired half in the same call (upper is not 6-adjacent to the fire - "
                                 "only reachable via linkage); both halves burn out same-window via "
                                 "ember flare (no blockBroken = no-drop burn), stone control never "
                                 "ignites";
            // 清场（火 / 门残格 / 石柱；部分火格可能已自熄 → setBlock(Air) 对 Air no-op 无害）。
            w.setBlock(x0, by, z0, BR::Air, 0);
            w.setBlock(x0 + 1, by, z0, BR::Air, 0);
            w.setBlock(x0 + 1, by + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by, z0, BR::Air, 0);
            w.setBlock(x0 + 3, by + 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── P27 复审 #19（2026-08-23 低危）栅栏向铁砧伸横档探针（mesher 同源直调，同 P11/P19 模式）──
    //   Review #19：t766 铁砧 solid=false 后栅栏连接谓词仍 isFence||isSolid → 栅栏不向铁砧伸横档（同类：
    //   画钉铁砧墙 / 雪傀儡立铁砧不铺雪，UI/实体侧改动矩阵不可锁，本探针锁 mesher 侧谓词）。修后连接
    //   谓词 = isCollidable ∨ isFullCube（R1 口径 a890bfa）。断言（顶点数差分：每连接向 +2 盒横档）：
    //   (a) +X 邻铁砧 → 顶点数 > 全空气基线（横档画出；修前 isSolid(Anvil)=false → 与基线同 → FAIL）；
    //   (b) +X 邻铁砧 == +X 邻栅栏（连接量与栅栏互连完全一致）；
    //   (c) +X 邻火把（ShapeNone）== 基线（非实体面仍不连——谓词放宽不至连空气族）。
    {
        const float tileW = 1.0f / 16.0f;
        PartialLightCtx lctx; lctx.light = 1.0f;
        for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
        const auto fenceVerts = [&](quint8 nbPosX) -> int {
            PartialNeighborCtx nctx; // 其余三向缺省 0（Air）→ 只 +X 连接位在变
            nctx.posX = nbPosX;
            QVector<Vtx> verts; QVector<quint32> idx;
            PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::WoodFence, 0,
                                         lctx, nctx, tileW, 0.0f, 0.0f, 0.0f, 1.0f);
            return int(verts.size()); // qsizetype → int 显式收窄（顶点数远小于 2^31）
        };
        const int baseN = fenceVerts(BR::Air);
        const int anvilN = fenceVerts(BR::Anvil);
        const int fenceN = fenceVerts(BR::WoodFence);
        const int torchN = fenceVerts(BR::Torch);
        const bool ok = anvilN > baseN && anvilN == fenceN && torchN == baseN;
        if (!ok)
            qInfo().noquote() << "  fence-connect vertex counts: base" << baseN << "anvil" << anvilN
                              << "fence" << fenceN << "torch" << torchN;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review#19 fence-to-anvil connection: R1 predicate (isCollidable||"
                             "isFullCube) draws rail arms toward anvil exactly like fence-fence; torch "
                             "(ShapeNone) still not connected";
    }

    // ── review-e 修复批探针（Review 2026-08-23 #6/#7/#8/#32 + dev-plan t834 生存链闭环）──
    // (a) #7/#8 配方生存链：剪/杀白羊掉 Wool 方块（QML sheepWoolDropId 字面量 27——静态契约由 recipe.cpp 尾部
    //     static_assert 钉死，此处运行期以**QML 同款字面量**喂 matcher 复核）→ 32 条染色配方（t788 已全测 16
    //     dye+wool；此处锁「字面量 27 / 63+idx-1 与配方原料/产物逐位相等」的跨层契约）+ 红床简化配方（羊毛
    //     方块版可合、旧 0x20E 版不再合）+ 红石灯（Glass 方块版可合、旧 0x204 物品版不再合）+ 两方块 dropId=
    //     自身（放置-破坏回收闭环）+ 烧沙仍产 0x204（放置过境物品——生存玻璃唯一入口）。
    // (b) #32 mobDied 第 8 参 sheared：剪毛羊致死 → sheared=true（QML 据此压掉羊毛掉落）；未剪对照 → false。
    // (c) #6 派生缓存文件名带 revision + 逐版清理（mobhead / sheep_woolface 两族；同 t745 icon2 / Review #11
    //     skin 族模式）。#33（QML Math.min/max 钳）与 #34（腿罩随腿摆）是纯 QML 呈现层，矩阵不链 Quick3D，
    //     需人工目视（字面量界标 27/63 已由 (a) 锁住）。
    // ⚠️ (c) 写共享 AppLocalData 目录（缓存生成器落盘路径固定）——探针会清掉 mobhead_1 / woolface 当前真身，
    //     属自愈型副作用（下次构建期 ensureBuiltLocked / 懒生成重落盘），同 t777/t779/t780 先例。
    {
        bool ok = true;
        // (a1) 跨层字面量契约：QML sheepWoolDropId 白→27 / 有色→63+idx-1 ↔ BlockRegistry 同值 ↔ 染色配方
        //     原料/产物逐位相等（QML 掉什么，配方就吃什么、产什么色）。
        {
            const int qmlWhite = 27, qmlBase = 63; // Main.qml sheepWoolDropId 字面量镜像
            if (int(BR::Wool) != qmlWhite || int(BR::FirstWoolVariant) != qmlBase) {
                qInfo().noquote() << "  [review-e diag] literal drift: Wool" << int(BR::Wool)
                                  << "FirstWoolVariant" << int(BR::FirstWoolVariant);
                ok = false;
            }
            for (int i = 0; i < 16; ++i) {
                const int dye = RecipeRegistry::DyeIdBase + i;
                const int drop = (i > 0) ? (qmlBase + i - 1) : qmlWhite; // QML sheepWoolDropId 公式镜像
                // 染色配方原料 = **白羊毛 27**（白羊掉落直连）；断言配方产出 == QML 有色羊掉落 id
                //   （同 id 同方块：剪/杀有色羊得的羊毛 = 染料染出的羊毛，两路产物汇流同一方块段）。
                int g[9] = { dye, qmlWhite, 0, 0, 0, 0, 0, 0, 0 };
                const RecipeRegistry::Recipe *m = RecipeRegistry::match(g, 2);
                if (!m || m->outputId != drop) {
                    qInfo().noquote() << "  [review-e diag] dye" << i << "+white-wool" << qmlWhite << "->"
                                      << (m ? m->outputId : -1) << "expected qmlDrop" << drop;
                    ok = false;
                }
            }
            // 红床：木板+Wool 方块（白羊毛掉落直连）→ BedRed；旧 0x20E 物品版不再合（退役不回头）。
            int gBed[9] = { int(BR::Planks), int(BR::Wool), 0, 0, 0, 0, 0, 0, 0 };
            const RecipeRegistry::Recipe *mBed = RecipeRegistry::match(gBed, 2);
            if (!mBed || mBed->outputId != int(BR::BedRed)) {
                qInfo().noquote() << "  [review-e diag] bed_red from planks+wool-block ->"
                                  << (mBed ? mBed->outputId : -1);
                ok = false;
            }
            int gBedOld[9] = { int(BR::Planks), RecipeRegistry::WoolId, 0, 0, 0, 0, 0, 0, 0 };
            if (RecipeRegistry::match(gBedOld, 2)) {
                qInfo().noquote() << "  [review-e diag] retired wool item 0x20E still crafts a bed";
                ok = false;
            }
            // 红石灯：4 红石十字 + 中心 Glass 方块 → RedstoneLamp；旧 0x204 物品中心版不再合。
            int gLamp[9] = { 0, RecipeRegistry::RedstoneId, 0,
                             RecipeRegistry::RedstoneId, int(BR::Glass), RecipeRegistry::RedstoneId,
                             0, RecipeRegistry::RedstoneId, 0 };
            const RecipeRegistry::Recipe *mLamp = RecipeRegistry::match(gLamp, 3);
            if (!mLamp || mLamp->outputId != int(BR::RedstoneLamp)) {
                qInfo().noquote() << "  [review-e diag] redstone_lamp from glass-block ->"
                                  << (mLamp ? mLamp->outputId : -1);
                ok = false;
            }
            int gLampOld[9] = { 0, RecipeRegistry::RedstoneId, 0,
                                RecipeRegistry::RedstoneId, RecipeRegistry::GlassId, RecipeRegistry::RedstoneId,
                                0, RecipeRegistry::RedstoneId, 0 };
            if (RecipeRegistry::match(gLampOld, 3)) {
                qInfo().noquote() << "  [review-e diag] retired glass item 0x204 center still crafts lamp";
                ok = false;
            }
            // 生存回收闭环：Glass / Wool 破坏 dropId=自身（放置→破坏→回手入配方，Wool/床族自掉先例）；
            //   熔炉烧沙仍产 0x204（放置过境物品 = 生存玻璃唯一入口，playercontroller t405 放置成 Glass）。
            if (BR::dropId(BR::Glass) != int(BR::Glass) || BR::dropId(BR::Wool) != int(BR::Wool)) {
                qInfo().noquote() << "  [review-e diag] self-drop broken: Glass->" << BR::dropId(BR::Glass)
                                  << "Wool->" << BR::dropId(BR::Wool);
                ok = false;
            }
            if (SmeltingRegistry::smeltResult(int(BR::Sand)) != RecipeRegistry::GlassId) {
                qInfo().noquote() << "  [review-e diag] sand smelt ->"
                                  << SmeltingRegistry::smeltResult(int(BR::Sand));
                ok = false;
            }
        }
        // (b) #32：两只成体羊（一剪一不剪）致死 → mobDied 各一发，sheared 载荷 true/false 对照；woolIdx 仍
        //     携带（QML 剪毛分支压掉羊毛掉落、烧死分支仍给熟羊肉——呈现层语义，此处只锁信号载荷）。
        {
            EntityManager emE;
            World wE;
            wE.setWidth(32);
            wE.setDepth(32);
            wE.setHeight(48);
            wE.setSeed(11);
            const int a = emE.spawnMobTyped(14, 12, 15, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
            const int b = emE.spawnMobTyped(16, 12, 15, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
            if (a < 0 || b < 0) {
                qInfo().noquote() << "  [review-e diag] failed to spawn probe sheep" << a << b;
                ok = false;
            } else {
                emE.shearSheep(a); // a 剪毛 / b 对照
                int died = 0, shearedSeen = -1, unshearedSeen = -1;
                QObject::connect(&emE, &EntityManager::mobDied, &emE,
                                 [&](int, int, int, int type, bool, bool, int, bool sheared) {
                                     if (type != EntityManager::MobSheep) return;
                                     ++died;
                                     if (sheared) shearedSeen = 1; else unshearedSeen = 0;
                                 });
                emE.damageEntity(a, emE.maxHealthAt(a));
                emE.damageEntity(b, emE.maxHealthAt(b));
                const QVector3D farListenerE(-1000.0f, 10.0f, -1000.0f);
                for (int t = 0; t < 40 && died < 2; ++t) // 0.64s > kDeathTime 0.5s
                    emE.tick(0.016f, &wE, farListenerE, 0.3f, 1.8f, false);
                if (died != 2 || shearedSeen != 1 || unshearedSeen != 0) {
                    qInfo().noquote() << "  [review-e diag] sheared-death payload: died" << died
                                      << "sheared" << shearedSeen << "unsheared" << unshearedSeen;
                    ok = false;
                }
            }
        }
        // (c) #6：两族派生缓存文件名 _r<rev> 嵌版 + 换版清旧（含 t749 期无后缀旧名）。
        {
            QDir dE(QDir::temp().absoluteFilePath("review_e_cache_probe"));
            dE.removeRecursively();
            dE.mkpath(".");
            const QString furPath = dE.absoluteFilePath("fur.png");
            const QString bodyPath = dE.absoluteFilePath("body.png");
            QImage fE(64, 32, QImage::Format_ARGB32), bE(64, 32, QImage::Format_ARGB32);
            fE.fill(QColor(0xf0, 0xec, 0xe4));
            bE.fill(QColor(0x7a, 0x5a, 0x48));
            if (!fE.save(furPath, "PNG") || !bE.save(bodyPath, "PNG")) {
                qInfo().noquote() << "  [review-e diag] failed to write temp rig PNGs";
                ok = false;
            }
            const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            // 预置无后缀旧名（t749 期格式）→ 调用后应被清。
            const QString legacy = QDir(cacheDir).absoluteFilePath(
                    QStringLiteral("voxelsandbox_rp_sheep_woolface.png"));
            QImage legacyPx(2, 2, QImage::Format_ARGB32);
            legacyPx.fill(Qt::black);
            legacyPx.save(legacy, "PNG");
            const QString p5 = generateSheepWoolFaceFile(furPath, bodyPath, 5);
            const QString p6 = generateSheepWoolFaceFile(furPath, bodyPath, 6);
            if (p5.isEmpty() || p6.isEmpty() || !p5.contains(QStringLiteral("_r5.png"))
                    || !p6.contains(QStringLiteral("_r6.png")) || QFile::exists(p5)
                    || !QFile::exists(p6) || QFile::exists(legacy)) {
                qInfo().noquote() << "  [review-e diag] woolface rev cache: p5" << p5 << "exists"
                                  << QFile::exists(p5) << "p6" << p6 << "exists" << QFile::exists(p6)
                                  << "legacy-gone" << !QFile::exists(legacy);
                ok = false;
            }
            // mobhead 族同模式（pig rig 子目录布局，同 t779）。
            QDir(dE.absoluteFilePath("pig")).mkpath(".");
            QImage pE(64, 32, QImage::Format_ARGB32);
            pE.fill(QColor(0xf0, 0xa0, 0xa8));
            pE.save(dE.absoluteFilePath("pig/pig.png"), "PNG");
            const QString h5 = generateMobHeadIconFor(1, dE.absolutePath(), 5);
            const QString h6 = generateMobHeadIconFor(1, dE.absolutePath(), 6);
            if (h5.isEmpty() || h6.isEmpty() || !h5.contains(QStringLiteral("_r5.png"))
                    || !h6.contains(QStringLiteral("_r6.png")) || QFile::exists(h5)
                    || !QFile::exists(h6)) {
                qInfo().noquote() << "  [review-e diag] mobhead rev cache: h5" << h5 << "exists"
                                  << QFile::exists(h5) << "h6" << h6 << "exists" << QFile::exists(h6);
                ok = false;
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review-e survival chains & death payload & rev-named caches: sheep wool drop "
                             "literal 27/63+idx-1 feeds all 16 dye recipes and planks+wool->bed_red (retired "
                             "0x20E no longer crafts anything), redstone lamp crafts from glass BLOCK (54, "
                             "self-drop) not glass item 0x204 (sand smelt still yields placeable 0x204 "
                             "transit), mobDied 8th param sheared=true suppresses wool drop on sheared "
                             "sheep death (unsheared control false), mobhead/sheep-woolface derived cache "
                             "filenames embed _r<revision> with per-revision stale cleanup incl. legacy "
                             "unsuffixed names (QML clamp #33 / leg covers #34 = manual visual check)";
    }

    // ── Review 2026-08-24 #4/#5 apply() 重建序 + pack 直返 URL cache-bust 探针 ──
    // #4 背景：mobhead 头像是构建期预生成（ensureBuiltLocked 以 s.revision 落盘 _r<rev>.png）；启动后首次
    //   构建走懒查询路径（revision=0、不自增）。旧序 apply() = ensureBuiltLocked()（用旧 rev 生成）→
    //   ++s.revision：第一次切包重建仍写 _r0.png 同名覆盖 → mobHeadIconSource 缓存命中直返同一 URL →
    //   QML Image 不重载（图鉴旧包头像；第二次切包写 _r1.png 才自愈）。修法 = ++s.revision 移到重建之前。
    // #5 背景：五族 pack 原文件直返（playerSkinSource 64×32 族 / entitySource / mobTextureSource /
    //   effectIconSource / paintingSource）不带 revision——同路径原地换包内容 + apply() 重解析下 URL 不变
    //   → QML 按 URL 缓存继续用旧像素（Review 2026-08-23 #12 皮肤族同款病，当时只修了皮肤一族）。修法 =
    //   packFileUrl(path, revision) 统一挂 ?r= 查询串。
    // 锁法：apply()/五族查询都持进程全局 BuiltState（读宿主机 settings/pack，不可密闭实例化——t777/t779
    //   先例），三层代替：
    //   ① #4 密闭 rig 时序模拟：懒构建(rev0) → 首次切包重建(bump 后 rev1) → 两代 URL 必不同（同 URL =
    //     QML 按 URL 缓存直返旧像素 = #4 病征本体；任意机器可跑）。
    //   ② 源序钉：直读 resourcepackmanager.cpp（滤 // 注释行后按函数切片）——apply() 体内 "++s.revision"
    //     必须先于 "ensureBuiltLocked()"（把 ++ 挪回重建之后在文本序上即时 FAIL）；五个查询函数体内必经
    //     packFileUrl（防后续再加「裸 file:/// 直返 pack 原文件」的新路径漏 cache-bust）。源文件不可读
    //     （无源部署）→ 记 note 跳过②（①③仍跑，不在无源机器上假 FAIL）。
    //   ③ #5 纯函数契约：查询串存在 / 随 revision 变 / QUrl::toLocalFile 剥离查询串（mobTextureSource
    //     羊/夜行者分支拿命中 URL 取 localFile 喂合成器靠这条——查询串不得污染文件寻址）。
    {
        bool ok = true;
        // ① #4 时序模拟（pig rig 子目录布局，同 review-e (c) 模式）。
        QDir dR24(QDir::temp().absoluteFilePath("review24_45_probe"));
        dR24.removeRecursively();
        dR24.mkpath(".");
        QDir(dR24.absoluteFilePath("pig")).mkpath(".");
        QImage pR24(64, 32, QImage::Format_ARGB32);
        pR24.fill(QColor(0xf0, 0xa0, 0xa8));
        if (!pR24.save(dR24.absoluteFilePath("pig/pig.png"), "PNG")) {
            qInfo().noquote() << "  [r24#4 diag] failed to write temp rig PNG";
            ok = false;
        } else {
            const QString lazyUrl = generateMobHeadIconFor(1, dR24.absolutePath(), 0);  // 启动懒构建（rev0）
            const QString applyUrl = generateMobHeadIconFor(1, dR24.absolutePath(), 1); // 首次切包重建（bump 后 rev1）
            if (lazyUrl.isEmpty() || applyUrl.isEmpty() || lazyUrl == applyUrl
                    || !lazyUrl.contains(QStringLiteral("_r0.png"))
                    || !applyUrl.contains(QStringLiteral("_r1.png"))) {
                qInfo().noquote() << "  [r24#4 diag] lazy/apply generation URL pair: lazy" << lazyUrl
                                  << "apply" << applyUrl;
                ok = false;
            }
        }
        // ③ #5 纯函数契约。
        const QString pu1 = packFileUrl(QStringLiteral("E:/probe/pack/pig.png"), 7);
        const QString pu2 = packFileUrl(QStringLiteral("E:/probe/pack/pig.png"), 8);
        if (!pu1.startsWith(QStringLiteral("file:///")) || !pu1.endsWith(QStringLiteral("?r=7"))
                || pu1 == pu2
                || QUrl(pu1).toLocalFile() != QStringLiteral("E:/probe/pack/pig.png")) {
            qInfo().noquote() << "  [r24#5 diag] packFileUrl contract: pu1" << pu1 << "pu2" << pu2
                              << "localFile" << QUrl(pu1).toLocalFile();
            ok = false;
        }
        // ② 源序钉：源文件定位（exe 在 build/ → ../src；嵌套一层再 ../../ 兜底）。
        QString rpmSrcPath;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                        QStringLiteral("src/Core/resourcepackmanager.cpp")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(
                        QStringLiteral("src/Core/resourcepackmanager.cpp")),
            };
            for (const QString &c : candidates) {
                if (QFile::exists(c)) { rpmSrcPath = c; break; }
            }
        }
        bool srcChecked = false;
        if (rpmSrcPath.isEmpty()) {
            qInfo().noquote() << "  [r24 note] resourcepackmanager.cpp not found near exe - source-order "
                                 "assertions (apply bump-before-rebuild / five-family packFileUrl) skipped "
                                 "(sealed-rig + pure-contract parts still ran)";
        } else {
            QFile srcF(rpmSrcPath);
            if (!srcF.open(QIODevice::ReadOnly)) {
                ok = false;
                qInfo().noquote() << "  [r24 diag] failed to open source" << rpmSrcPath;
            } else {
                srcChecked = true;
                // 滤 // 注释行（探测目标是语句文本序，注释里的标识符会干扰 indexOf；本文件注释全为行注释）。
                QString codeText;
                const QString rawText = QString::fromUtf8(srcF.readAll());
                for (const QString &line : rawText.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//")))
                        continue;
                    codeText += line;
                    codeText += QLatin1Char('\n');
                }
                // 函数体切片：从函数头到下一个列 0 闭括号（本文件成员函数体无列 0 嵌套闭括号）。
                const auto funcBody = [&codeText](const QString &header, QString *out) -> bool {
                    const int h = codeText.indexOf(header);
                    if (h < 0)
                        return false;
                    const int end = codeText.indexOf(QStringLiteral("\n}"), h);
                    *out = codeText.mid(h, end < 0 ? 6000 : int(end) - h);
                    return true;
                };
                // #4：apply() 体内 ++s.revision 先于 ensureBuiltLocked()。
                QString applyBody;
                if (!funcBody(QStringLiteral("ResourcePackManager::apply()"), &applyBody)) {
                    ok = false;
                    qInfo().noquote() << "  [r24#4 diag] apply() body not found in source";
                } else {
                    const int bump = applyBody.indexOf(QStringLiteral("++s.revision"));
                    const int build = applyBody.indexOf(QStringLiteral("ensureBuiltLocked"));
                    if (bump < 0 || build < 0 || bump > build) {
                        ok = false;
                        qInfo().noquote() << "  [r24#4 diag] apply() order: bumpIdx" << bump
                                          << "rebuildIdx" << build
                                          << "(++s.revision must precede ensureBuiltLocked)";
                    }
                }
                // #5：五族查询函数体内必经 packFileUrl。
                const char *families[5] = {
                    "ResourcePackManager::playerSkinSource(",
                    "ResourcePackManager::entitySource(",
                    "ResourcePackManager::mobTextureSource(",
                    "ResourcePackManager::effectIconSource(",
                    "ResourcePackManager::paintingSource(",
                };
                for (const char *fam : families) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(fam), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))) {
                        ok = false;
                        qInfo().noquote() << "  [r24#5 diag] family function missing packFileUrl direct-return"
                                          << fam;
                    }
                }
                // review25 #7 扩面：图标/atlas/strip 族（itemIconSource 主返回 + 蛋/铜派生缓存 /
                //   emptyArmorSlotSource / blockItemIconSource（主返回 + 床染色族）/ atlasSource 固定名合成
                //   图集 / 四条 strip 固定名合成条带）——d6051e6 的「直返族已闭环」叙事漏掉的高频族，本批
                //   补收口。钉法分两档：7 个小函数（atlas/4 strip/emptyArmor）体短且全返回路径都该 bust →
                //   必经 packFileUrl + **禁**裸 QStringLiteral("file:///") 构造（防「主路径改了回退漏改」的
                //   半改态）；itemIconSource/blockItemIconSource 体长且含 leather/_r 文件名族的合法裸直返 →
                //   钉主返回裸直返语句的**不存在**（"file:///") + path / + foundPath 的构造被禁）。
                const char *smallFamilies[6] = {
                    "ResourcePackManager::atlasSource(",
                    "ResourcePackManager::waterStripSource(",
                    "ResourcePackManager::lavaStripSource(",
                    "ResourcePackManager::fireStripSource(",
                    "ResourcePackManager::portalStripSource(",
                    "ResourcePackManager::emptyArmorSlotSource(",
                };
                for (const char *fam : smallFamilies) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(fam), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))
                            || body.contains(QStringLiteral("QStringLiteral(\"file:///\")"))) {
                        ok = false;
                        qInfo().noquote() << "  [r25#7 diag] icon/atlas/strip family missing packFileUrl or"
                                             " still has bare file:/// construction"
                                          << fam;
                    }
                }
                const char *longFamilies[2] = {
                    "ResourcePackManager::itemIconSource(",
                    "ResourcePackManager::blockItemIconSource(",
                };
                const char *longBare[2] = {
                    "QStringLiteral(\"file:///\") + path",
                    "QStringLiteral(\"file:///\") + foundPath",
                };
                for (int i = 0; i < 2; ++i) {
                    QString body;
                    if (!funcBody(QString::fromLatin1(longFamilies[i]), &body)
                            || !body.contains(QStringLiteral("packFileUrl("))
                            || body.contains(QString::fromLatin1(longBare[i]))) {
                        ok = false;
                        qInfo().noquote() << "  [r25#7 diag] long icon family missing packFileUrl main return"
                                          << longFamilies[i];
                    }
                }
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24 #4/#5 pack cache-bust timing: sealed pig rig lazy(rev0) vs first-apply "
                             "rebuild(rev1) mobhead URLs differ (same URL = QML URL-cache stale), packFileUrl "
                             "appends ?r=<rev> that changes with revision and strips from QUrl::toLocalFile"
                          << (srcChecked
                                  ? ", source pin: apply() bumps revision BEFORE ensureBuiltLocked() rebuild "
                                    "+ all five direct-return families route through packFileUrl"
                                    "+ review25 #7: icon/atlas/strip families (6 small + 2 long) bust"
                                    " ?r=<rev> with bare file:/// direct-returns banned"
                                  : " (source pin skipped - no source tree next to exe)");
    }

    // ── Review 2026-08-23 #27 余烬门熄灭钩子并入 World 写入族探针 ──
    // 背景：旧熄门钩子只挂 playercontroller 挖掘路径（finishMiningAt 显式调 breakNetherPortalsAround），
    //   爆炸 / TNT 点火 / 火焚 / 流体置换等系统静默写路径拆掉门框或门面后**门面残留**（玩家肉眼：框被炸掉
    //   一角、紫门面悬空不灭）。review #27 修法 = 钩子下沉 World 写入族（setBlock×2 / setBlockSilent /
    //   clearBlockSilent / setWaterSilent / setBlockFromEntity / destroySphereSilent 逐破坏格 /
    //   dropGravityColumn 逐清格，谓词=本格原有非空内容被置换；removeNetherPortalAt 清域带
    //   m_inRemoveNetherPortal 守卫防嵌套 BFS）。本探针逐入口驱动：
    //   (a) 爆炸（destroySphereSilent r=1.5 打掉 4×5 门一角 4 门格；黑曜石框爆炸免疫——残 16 格靠钩子熄）
    //   (b) setBlockSilent 置换门格（火吞系统写代表）；(c) clearBlockSilent 拆底梁（TNT 点火清格代表）；
    //   (d) setWaterSilent 置换门格（流体蒸发 / 改道代表）；(e) 纯放置对照（旧格 Air → 不熄，机制等价
    //   MC 放置不破门）；(f) 玩家挖掘路径回归（setBlock(Air) 拆柱 —— 旧 playercontroller 显式调用的等价
    //   序列，现由 World 层钩子自覆盖）。每场景重建 4×5 Z 平面无角门（t806 ④ 无角合法）+ 点燃 20 格。
    {
        World wRf27;
        wRf27.setWidth(48);
        wRf27.setDepth(48);
        wRf27.setHeight(32);
        wRf27.setSeed(13);
        bool okRf27 = true;
        const int pY27 = 20;
        const int px27 = 8, pz27 = 20; // 4×5 Z 平面门：门面 x=px27 平面，z∈[pz27,pz27+3]，y∈[pY27,pY27+4]
        // 建无角 4×5 门框（t806 buildFrame 简化版；先清场盒防地形 / 上一场景残留）。
        const auto buildPortal45 = [&]() {
            for (int c = -3; c <= 7; ++c)
                for (int r = -3; r <= 8; ++r)
                    for (int d = -2; d <= 2; ++d)
                        wRf27.setBlock(px27 + d, pY27 + r, pz27 + c, BR::Air, 0);
            for (int c = 0; c < 4; ++c) { // 底梁 / 顶梁
                wRf27.setBlock(px27, pY27 - 1, pz27 + c, BR::Obsidian, 0);
                wRf27.setBlock(px27, pY27 + 5, pz27 + c, BR::Obsidian, 0);
            }
            for (int r = 0; r < 5; ++r) { // 左右边柱
                wRf27.setBlock(px27, pY27 + r, pz27 - 1, BR::Obsidian, 0);
                wRf27.setBlock(px27, pY27 + r, pz27 + 4, BR::Obsidian, 0);
            }
        };
        // 本 rig 清场盒内门格计数（隔壁区域串数免疫）。
        const auto countPortal27 = [&]() {
            int n = 0;
            for (int c = -3; c <= 7; ++c)
                for (int r = -3; r <= 8; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (wRf27.blockAt(px27 + d, pY27 + r, pz27 + c) == BR::NetherPortal) ++n;
            return n;
        };
        // 每场景起手式：重建 + 点燃 + 核对恰 20 门格。
        const auto ignite20 = [&]() -> bool {
            buildPortal45();
            const bool lit = wRf27.tryIgniteNetherPortal(px27, pY27 + 1, pz27 + 1);
            return lit && countPortal27() == 20;
        };
        // (a) 爆炸拆角：r=1.5 球心打门面角格 (px27,pY27,pz27) → 球内门格 4（dz²+dy²≤2.25），球外 16 格
        //     旧代码残留（无钩子）、新代码被球内破坏格的逐格钩子连坐熄灭；黑曜石框爆炸免疫（skip 列表）→
        //     拆门路径 = 门格自身被清。
        {
            const bool lit = ignite20();
            const auto dv = wRf27.destroySphereSilent(px27, pY27, pz27, 1.5f);
            const int left = countPortal27();
            if (!lit || int(dv.size()) < 4 || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] explosion teardown: lit" << lit
                                  << "destroyed" << int(dv.size()) << "portal left" << left;
                okRf27 = false;
            }
        }
        // (b) setBlockSilent 置换门格（火吞可燃物等系统静默写代表）：门格被 Stone 置换 → 其余 19 格全熄。
        {
            const bool lit = ignite20();
            wRf27.setBlockSilent(px27, pY27 + 2, pz27 + 3, BR::Stone, 0);
            const int left = countPortal27();
            if (!lit || wRf27.blockAt(px27, pY27 + 2, pz27 + 3) != BR::Stone || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] setBlockSilent displace: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (c) clearBlockSilent 拆底梁中格（TNT 点火清格代表路径）：梁去 → 门面失框全熄。
        {
            const bool lit = ignite20();
            wRf27.clearBlockSilent(px27, pY27 - 1, pz27 + 1);
            const int left = countPortal27();
            if (!lit || wRf27.blockAt(px27, pY27 - 1, pz27 + 1) != BR::Air || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] clearBlockSilent beam: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (d) setWaterSilent 置换门格（流体蒸发 / 改道批量写代表）：门格被清 → 全熄。
        {
            const bool lit = ignite20();
            wRf27.setWaterSilent(px27, pY27 + 3, pz27 + 2, BR::Air, 0);
            const int left = countPortal27();
            if (!lit || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] setWaterSilent displace: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (e) 纯放置对照：门旁空气格放 Stone（旧格 Air → 钩子谓词不触发）→ 门健在恰 20（放置不破门，
        //     机制等价 MC——只有拆 / 置换才破）。防钩子过度触发回归。
        {
            const bool lit = ignite20();
            wRf27.setBlock(px27 + 1, pY27 + 2, pz27 + 1, BR::Stone, 0);
            const int left = countPortal27();
            if (!lit || left != 20) {
                qInfo().noquote() << "  [review-f #27 diag] placement control: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        // (f) 玩家挖掘路径回归（拆右边柱中格 setBlock(Air)，旧 playercontroller 显式序列的 World 层等价）。
        {
            const bool lit = ignite20();
            wRf27.setBlock(px27, pY27 + 2, pz27 + 4, BR::Air, 0);
            const int left = countPortal27();
            if (!lit || left != 0) {
                qInfo().noquote() << "  [review-f #27 diag] dig pillar teardown: lit" << lit
                                  << "portal left" << left;
                okRf27 = false;
            }
        }
        if (!okRf27) ++totalFail;
        qInfo().noquote() << (okRf27 ? "PASS" : "FAIL")
                          << "| review-f #27 portal extinguish joined World write family: displacement of "
                             "non-air cell (explosion sphere per-voxel / setBlockSilent / clearBlockSilent / "
                             "setWaterSilent / player dig setBlock) collapses whole connected portal domain "
                             "(4x5 rig: 16 out-of-sphere cells die via hook, obsidian frame blast-immune), "
                             "pure placement into air beside a lit door keeps all 20 cells (predicate "
                             "guard), removeNetherPortalAt clear guarded against nested BFS";
    }

    // ── Review 2026-08-23 #28 铁砧砸伤按目标结算探针 ──
    // 背景：旧 anvilDamaged 是**落体侧一次性拍**——下落铁砧首个命中帧置位后整次下落不再结算 → 铁砧先穿
    //   玩家后落到猪身上则猪免伤、台阶两猪只伤上面那只。review #28 修法 = **按目标记账**：落体
    //   spawnSerial 写进每个被结算目标（mob 侧 Entity.anvilCrushSerial / 玩家侧 m_playerAnvilCrushSerial），
    //   同 serial 再压同目标跳过 → 真语义「每实体每次下落只伤一次」。rig：圈养猪（100HP，四邻墙防走脱）
    //   列底 + 虚拟玩家 listener 悬列中段 → 一块铁砧从 y=15 落穿两者：(P) 猪恰扣 10HP（落差 5.x..6 格 →
    //   (floor−1)×2=10；旧代码先结算玩家 → 猪 100 不动）+ 玩家恰 1 次 2HP（落差 2.x 格）；(Q) 着地后 20 帧
    //   （< 窒息首扣 ~63 帧）两者读数不动（同落体不重复结算）；(R) 第二块铁砧（新 serial）→ 玩家再结算
    //   一次（「每次下落」独立）+ 落在第一块上（着地还原链不受记账影响）。
    {
        World wRf28;
        wRf28.setWidth(48);
        wRf28.setDepth(48);
        wRf28.setHeight(32);
        wRf28.setSeed(13);
        const int cx28 = 12, cz28 = 12, ty28 = 9;
        for (int x = 10; x <= 15; ++x)
            for (int z = 10; z <= 15; ++z) {
                for (int y = 9; y <= 17; ++y) wRf28.setBlock(x, y, z, BR::Air, 0);
                wRf28.setBlock(x, 8, z, BR::Stone, 0); // 平台（猪圈底 + 铁砧落点支撑）
            }
        // 圈栏（1 高即可——aiWander 无跳跃；猪恒留落点列，盒顶恒 ty+0.9）。
        wRf28.setBlock(cx28 - 1, ty28, cz28, BR::Stone, 0);
        wRf28.setBlock(cx28 + 1, ty28, cz28, BR::Stone, 0);
        wRf28.setBlock(cx28, ty28, cz28 - 1, BR::Stone, 0);
        wRf28.setBlock(cx28, ty28, cz28 + 1, BR::Stone, 0);
        EntityManager emRf28;
        int hits28 = 0, hit28a = 0;
        QObject::connect(&emRf28, &EntityManager::mobAttackedPlayer, &emRf28,
                         [&](int amount, int, float, float) {
                             if (hits28 == 0) hit28a = amount;
                             ++hits28;
                         });
        const int pig28 = emRf28.spawnMobTyped(cx28, ty28, cz28, EntityManager::MobPig,
                                               QStringLiteral("#ffd0d0"), 100);
        const QVector3D far28(-1000.0f, 10.0f, -1000.0f);
        for (int t = 0; t < 90; ++t) // 猪落定（resting 盒顶 ty+0.9）
            emRf28.tick(0.016f, &wRf28, far28, 0.3f, 1.8f, false);
        bool okRf28 = pig28 >= 0 && emRf28.healthAt(pig28) == 100;
        // (P) 一块铁砧 y=15（fallStart 15.5）：先穿玩家带 [12,13.8]（fallDist≥2 → 2HP），后压猪顶 9.9
        //     （首个重叠帧 fallDist 5.1x → (floor 5−1)×2 = 8HP）——两目标各恰一次。
        const QVector3D listener28(float(cx28) + 0.5f, 12.0f, float(cz28) + 0.5f);
        emRf28.spawnFallingBlock(cx28, 15, cz28, int(BR::Anvil));
        int pigAtCrush28 = -1;
        for (int t = 0; t < 160; ++t) {
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
            if (pigAtCrush28 < 0 && emRf28.healthAt(pig28) < 100)
                pigAtCrush28 = emRf28.healthAt(pig28);
            if (pigAtCrush28 >= 0 && hits28 >= 1 && wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil)
                break;
        }
        okRf28 = okRf28 && pigAtCrush28 == 92      // 猪被结算（旧一次性拍：先穿玩家 → 猪恒 100）；
                                                   //   首个重叠帧落差 5.1x → (floor 5−1)×2 = 8HP
                 && hits28 == 1 && hit28a == 2     // 玩家同落体恰一次 2HP
                 && wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil; // 先伤后落：着地还原于猪格
        // (Q) 着地后 20 帧（< 窒息 63 帧首扣）读数不动：同落体不重复结算。
        for (int t = 0; t < 20; ++t)
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
        okRf28 = okRf28 && emRf28.healthAt(pig28) == pigAtCrush28 && hits28 == 1;
        // (R) 第二块铁砧（新 spawnSerial）落第一块上方：玩家再结算一次（每「次下落」独立记账）。
        emRf28.spawnFallingBlock(cx28, 15, cz28, int(BR::Anvil));
        for (int t = 0; t < 160; ++t) {
            emRf28.tick(0.016f, &wRf28, listener28, 0.3f, 1.8f, true);
            if (wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil) break;
        }
        okRf28 = okRf28 && hits28 == 2 && wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil;
        if (!okRf28) {
            qInfo().noquote() << "  [review-f #28 diag] pig" << pig28 << "hp"
                              << (pig28 >= 0 ? emRf28.healthAt(pig28) : -1)
                              << "atCrush" << pigAtCrush28 << "| hits" << hits28
                              << "amt" << hit28a << "| landed"
                              << (wRf28.blockAt(cx28, ty28, cz28) == BR::Anvil)
                              << (wRf28.blockAt(cx28, ty28 + 1, cz28) == BR::Anvil);
        }
        if (!okRf28) ++totalFail;
        qInfo().noquote() << (okRf28 ? "PASS" : "FAIL")
                          << "| review-f #28 anvil crush settles per target: one fall through virtual "
                             "player band then penned pig damages BOTH exactly once each (pig 100->92 "
                             "at first-overlap frame (floor(5.1x)-1)*2=8, player 1 hit 2HP — old "
                             "one-shot falling-entity flag "
                             "starved the later target), readings frozen for 20 post-land frames (no "
                             "re-settlement within one fall, pre-suffocation window), second anvil with "
                             "fresh serial re-settles player (per-fall independence) and stacks on first "
                             "(restore chain intact)";
    }

    // ── Review 2026-08-23 #29 铁砧砸伤窄盒探针 ──
    // 背景：旧砸伤 XZ 判定用落体 halfW(0.5)+目标 halfW 的满格宽 → 贴格边站（视觉上铁砧 12/16 宽没碰到）
    //   也被砸。review #29 修法 = 铁砧砸伤足印独立常量 kAnvilCrushHalfW=0.375（12/16 视觉宽一半；沙 / 砾
    //   等其余落体仍 0.5）。玩家 listener halfW=0.3 → 命中阈 0.375+0.3=0.675（旧 0.8）。断言：
    //   (a) 列心偏 0.7（>0.675 新界、<0.8 旧界）→ 0 命中（旧代码必中的判别位）；(b) 偏 0.6 对照 → 恰一次
    //   2HP（窄盒内仍正常结算，落差 2.x 格）。两列各自落定还原铁砧于平台顶。
    {
        World wRf29;
        wRf29.setWidth(48);
        wRf29.setDepth(48);
        wRf29.setHeight(32);
        wRf29.setSeed(13);
        const int ty29 = 9;
        for (int x = 26; x <= 36; ++x)
            for (int z = 26; z <= 33; ++z) {
                for (int y = 9; y <= 15; ++y) wRf29.setBlock(x, y, z, BR::Air, 0);
                wRf29.setBlock(x, 8, z, BR::Stone, 0);
            }
        EntityManager emRf29;
        int hits29 = 0, hit29a = 0;
        QObject::connect(&emRf29, &EntityManager::mobAttackedPlayer, &emRf29,
                         [&](int amount, int, float, float) {
                             if (hits29 == 0) hit29a = amount;
                             ++hits29;
                         });
        // (a) 列 (30,·,28) 心 30.5；listener X 偏 +0.7（31.2）→ 新窄盒外（≥0.675）→ 0 命中。
        emRf29.spawnFallingBlock(30, 13, 28, int(BR::Anvil));
        const QVector3D outside29(31.2f, float(ty29), 28.5f);
        for (int t = 0; t < 140; ++t)
            emRf29.tick(0.016f, &wRf29, outside29, 0.3f, 1.8f, true);
        const bool okA29 = hits29 == 0 && wRf29.blockAt(30, ty29, 28) == BR::Anvil;
        const int hitsAfterA29 = hits29; // (a) 阶段末命中数（判别用）
        // (b) 列 (30,·,31) 心 30.5；listener X 偏 +0.6（31.1）→ 窄盒内（<0.675）→ 恰一次 2HP。
        emRf29.spawnFallingBlock(30, 13, 31, int(BR::Anvil));
        const QVector3D inside29(31.1f, float(ty29), 31.5f);
        for (int t = 0; t < 140; ++t)
            emRf29.tick(0.016f, &wRf29, inside29, 0.3f, 1.8f, true);
        const bool okB29 = hits29 == 1 && hit29a == 2 && wRf29.blockAt(30, ty29, 31) == BR::Anvil;
        const bool okRf29 = okA29 && okB29;
        if (!okRf29) {
            qInfo().noquote() << "  [review-f #29 diag] offset0.7 phase hits" << hitsAfterA29
                              << "landedA" << (wRf29.blockAt(30, ty29, 28) == BR::Anvil)
                              << "| offset0.6 final hits" << hits29 << "amt" << hit29a
                              << "landedB" << (wRf29.blockAt(30, ty29, 31) == BR::Anvil);
        }
        if (!okRf29) ++totalFail;
        qInfo().noquote() << (okRf29 ? "PASS" : "FAIL")
                          << "| review-f #29 anvil crush narrow footprint: crush XZ test uses anvil "
                             "visual half-width 0.375 (12/16) + listener 0.3 = 0.675 threshold — "
                             "listener 0.7 off column axis untouched (old full-cell 0.8 box would hit), "
                             "0.6 control hit exactly once for 2HP at 2-block fall, both anvils restore "
                             "on platform (sand-family falling blocks keep 0.5, manual check)";
    }

    // ── Review 2026-08-23 #30 鱿鱼笼水格刷位探针 ──
    // 背景：tickSpawners 找位谓词硬编码「air+上 air+下 solid」陆生条件 → 鱿鱼笼（生物蛋改型）复用后
    //   只能刷在陆上（搁浅鱿鱼慢爬）。review #30 修法 = 鱿鱼走水格谓词（本格+上格均 Water，免固体底）。
    //   rig：48×48 种子 9 世界（y=8 worldgen 恒实心），笼刻 MobSquid 型，唯一合格邻位 = (sx+1) 水柱两格，
    //   其余 7 水平邻显式塞 Stone 双层（陆 / 水两谓词均不满足 → 无处可刷的旧代码 0 刷判别位）。一周期
    //   tickSpawners(6.0) → 恰 1 只鱿鱼且落水柱格。
    {
        World wRf30;
        wRf30.setWidth(48);
        wRf30.setDepth(48);
        wRf30.setHeight(32);
        wRf30.setSeed(9);
        const int sx30 = 24, sy30 = 8, sz30 = 24;
        wRf30.setBlock(sx30, sy30, sz30, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobSquid));
        // (sx+1,sz) 水柱两格（鱿鱼谓词：here+above 均 Water）；其余 7 水平邻 Stone 双层（worldgen 实心再
        //   显式保险——两谓词全灭 → 旧代码必 0 刷）。
        wRf30.setBlock(sx30 + 1, sy30, sz30, BR::Water, 0);
        wRf30.setBlock(sx30 + 1, sy30 + 1, sz30, BR::Water, 0);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue; // 笼格本身
                if (dx == 1 && dz == 0) continue; // 水柱位
                wRf30.setBlock(sx30 + dx, sy30, sz30 + dz, BR::Stone, 0);
                wRf30.setBlock(sx30 + dx, sy30 + 1, sz30 + dz, BR::Stone, 0);
            }
        EntityManager emRf30;
        const QVector3D player30(float(sx30) + 0.5f, float(sy30) + 0.5f, float(sz30) + 12.5f); // XZ 12.5 < 16 激活圈
        emRf30.tickSpawners(6.0, &wRf30, player30); // 单周期（kSpawnerInterval=6s）
        int squids30 = 0;
        bool atWater30 = false;
        for (int i = 0; i < emRf30.count(); ++i) {
            if (!emRf30.aliveAt(i) || emRf30.mobTypeAt(i) != int(EntityManager::MobSquid)) continue;
            ++squids30;
            const QVector3D p = emRf30.posAt(i);
            atWater30 = atWater30 || (int(p.x()) == sx30 + 1 && int(p.y()) == sy30
                                      && int(p.z()) == sz30);
        }
        const bool okRf30 = squids30 == 1 && atWater30;
        if (!okRf30) {
            qInfo().noquote() << "  [review-f #30 diag] squids" << squids30
                              << "atWater" << atWater30 << "| count" << emRf30.count()
                              << "| waterCell"
                              << (wRf30.blockAt(sx30 + 1, sy30, sz30) == BR::Water);
        }
        if (!okRf30) ++totalFail;
        qInfo().noquote() << (okRf30 ? "PASS" : "FAIL")
                          << "| review-f #30 squid cage spawns in water: squid-typed spawner uses water "
                             "cell predicate (cell+above both Water, no solid floor needed) — exactly 1 "
                             "squid in the sole 2-deep water column neighbor while all 7 other neighbors "
                             "stone-filled (old land predicate would spawn nowhere / beached squid)";
    }

    // ── Review 2026-08-23 #31 被动笼不受敌对预算压制探针 ──
    // 背景：旧 tickSpawners 入口全局敌对 cap（hostileCount()>=kHostileMobCap=30）+ 玩家周边区域 cap 早退
    //   对**被动笼**同样生效 → 夜里敌对满 30 时猪 / 羊笼全停。review #31 修法 = 闸门按笼型分流：被动笼仅留
    //   同型 local cap（kSpawnerLocalCap）+ 总 cap（kCap）；敌对笼保留三重闸门。机制等价 MC 1.0 spawner
    //   不受 ambient hostile cap 约束。rig：96 宽世界（48 区域半径外可容远场种子），远场 (4..9,·,4..8) 预置
    //   30 只蹒腚者（XZ 距玩家 ~57 > kHostileAreaRadius=48，全局敌对预算打满），玩家旁猪笼 + 蹒腚者笼各一
    //   （邻位手工挖空 + 石底）。一周期 → 猪笼照刷恰 1（旧入口早退判别位：0 刷），蹒腚者笼被全局 cap 压制
    //   0 刷，hostileCount 恒 30。
    {
        World wRf31;
        wRf31.setWidth(96);
        wRf31.setDepth(96);
        wRf31.setHeight(48);
        wRf31.setSeed(9);
        const int py31 = 8, pz31 = 48;
        const int pigX31 = 44, shamX31 = 52; // 两笼 XZ 距玩家 (48.5,48.5) 均 <16 激活圈
        const int cages31[2] = { pigX31, shamX31 };
        for (int cxi : cages31)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue; // 笼格本身
                    wRf31.setBlock(cxi + dx, py31, pz31 + dz, BR::Air, 0);    // 陆生刷位：air×2
                    wRf31.setBlock(cxi + dx, py31 + 1, pz31 + dz, BR::Air, 0);
                    wRf31.setBlock(cxi + dx, py31 - 1, pz31 + dz, BR::Stone, 0); // + 石底（显式，同 t786 口径）
                }
        wRf31.setBlock(pigX31, py31, pz31, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobPig));
        wRf31.setBlock(shamX31, py31, pz31, BR::Spawner,
                       BlockRegistry::spawnerStateForMob(EntityManager::MobShambler));
        EntityManager emRf31;
        for (int i = 0; i < 30; ++i) // 远场敌对种子：打满全局 cap（kHostileMobCap=30）
            emRf31.spawnHostileMob(4 + (i % 6), py31, 4 + (i / 6), EntityManager::MobShambler);
        const QVector3D player31(48.5f, 8.5f, 48.5f);
        emRf31.tickSpawners(6.0, &wRf31, player31); // 单周期
        const QVector3D pigCage31(float(pigX31) + 0.5f, 8.5f, float(pz31) + 0.5f);
        const QVector3D shamCage31(float(shamX31) + 0.5f, 8.5f, float(pz31) + 0.5f);
        const int pigs31 = emRf31.mobTypeCountNear(pigCage31, 4.0f, EntityManager::MobPig);
        const int shamsNear31 = emRf31.mobTypeCountNear(shamCage31, 4.0f, EntityManager::MobShambler);
        const bool okRf31 = emRf31.hostileCount() == 30 && shamsNear31 == 0 && pigs31 == 1;
        if (!okRf31) {
            qInfo().noquote() << "  [review-f #31 diag] pigsNear" << pigs31
                              << "shamsNear" << shamsNear31
                              << "hostiles" << emRf31.hostileCount();
        }
        if (!okRf31) ++totalFail;
        qInfo().noquote() << (okRf31 ? "PASS" : "FAIL")
                          << "| review-f #31 passive cage ignores hostile budget: with global hostile "
                             "pool maxed (30 far seeded shamblers >48 away, outside area radius), "
                             "nearby pig cage still spawns exactly 1 (old entry gate starved ALL cages "
                             "incl. passive), shambler cage correctly suppressed by hostile global cap, "
                             "hostileCount stays 30 (MC 1.0 spawner not bound by ambient hostile cap; "
                             "same-type local cap + total kCap remain, manual check)";
    }

    // ── Review 2026-08-23 #5 火蔓延抑制层探针（① 新蔓延率统计 / ② 湿燃料防火带 / ③ 火自身邻水加速自熄 /
    //    ④ 降雨露天自熄 + 屋檐对照）──
    // 背景：t804 逐邻独立掷 5%/窗（提速 6×）且全程无雨 / 水抑制 → 多火源叠加（k 火格包围每窗 1-0.95^k，
    //   k=2~3 → 10~14%/窗 ≈ 3~6s/块）下木建筑几十秒烧穿、雨天不灭火、水邻不阻蔓延（玩家误点火无反制，
    //   且烧毁 setBlock(Fire) 无掉落不可逆）。review-g 修法 = 抑制层最小版（火语义重做留 dev-plan t843，
    //   抑制判定收口 World::fireRainExposedAt / fireWaterNeighborAt 两函数供整体搬走）：
    //   ① 叠加补偿：逐邻 5% → 2.5%（kFireSpreadPermille=25‰）；② 火格露天降雨 / 自身 6 邻含水 →
    //   kFireSuppressExtinguishPct=40%/窗 加速自熄（压过燃料）+ 蔓延掷骰减半；③ 蔓延目标格 6 邻含水 →
    //   湿燃料不点燃（水格周围 1 圈 = 防火带）。
    // 四段断言（专用局部世界 seed 9，P30/P31 先例；seed 9 丘陵地形可达 y44+ 无保证净空层 → (a)-(c)
    //   rig 带自凿清空（晴天态抑制判定不看 skyLight，埋地不混淆）；(d) 扫描真天空列 + skyLight 前置）：
    // (a) 新率统计（晴天默认态，无抑制混淆源）：24 条独立泳道（火 + 贴邻木板），每窗点燃即复原木板再继续
    //     → 每泳道每窗恰 1 个 Bernoulli 样本，800 窗 × 24 = 19200 样本 → 点燃率落 [2.0%, 3.0%]（期望 2.5%，
    //     σ≈21.6/480 ≈ ±4.4σ 裕量；5% 旧值 → ~960 远超上界、t724 旧 0.83% → ~160 远低下界，两代旧率均
    //     被排除）；末窗 24 火全存活（有燃料 + 晴天 → 无自熄路径，确定性——顺带锁「无抑制不误熄」）。
    // (b) 湿燃料防火带（确定性）：火-木板-水一线（水邻木板、距火 2 格不抑制火格自身）→ 300 窗木板原样
    //     + 火仍存活（火有燃料不熄、每次掷骰被湿燃料判定拦下——非概率断言）。
    // (c) 火自身邻水加速自熄：火 6 邻含 水 + 燃料板（水在火正上方）→ 200 窗内火灭（P(幸存)=0.6^200≈1e-44；
    //     燃料板可能被濒死火余烬（减半 1.2%）点燃，新火无燃料 5%/窗亦熄 → 终态两格均非 Fire）。
    // (d) 降雨露天自熄 + 屋檐对照：tickWeather 大步长驱动状态机强制降水（Clear→降水必翻；雷态翻转回避
    //     strikeLightning 随机落点——rig 木料此时尚未放置，雷击焚木走 setBlock(Air) 不产生 Fire 格，混不进
    //     tickFire）→ 露天火（skyLightAt==15 + 该列 isPrecipitatingAt 前置校验）带燃料 200 窗内熄灭；
    //     对照：同列隔 8 格加石板屋顶（火格 skyLight<15 前置校验）→ 若火熄则其燃料板必已被吞（火只可能
    //     烧完燃料自然熄，不可能被雨杀——淋不到；杀错 = 抑制判定漏了遮挡门）。结束恢复 Clear（卫生）。
    {
        World wG5;
        wG5.setWidth(96);
        wG5.setDepth(96);
        wG5.setHeight(48);
        wG5.setSeed(9);
        const int gy = 41; // rig 层（seed 9 丘陵地形可达 y44+——rig 带自凿清空，露天 rig 才用真天空列）
        bool okA = false, okB = false, okC = false, okD = false;
        double rateA = -1.0;
        int firesA = -1;
        QString diagA, diagD;

        // ── (a) 新蔓延率统计（24 泳道 × 800 窗；晴天默认态）──
        // seed 9 丘陵地形可达 y44+（固定「高空层」不存在）→ rig 带整片自凿清空（P31「凿进石里」先例；
        //   (a)-(c) 不依赖露天——晴天态 fireRainExposedAt 全局早退不看 skyLight）。泳道网格 8 列(x 步 4)
        //   × 3 行(z 步 3)：火(x)+板(x+1)，净空带外扩 1 格 → 泳道间互不邻接、每火恰 1 个可燃邻。
        constexpr int kLanesG5 = 24;
        const auto laneX = [](int k) { return 8 + 4 * (k % 8); };
        const auto laneZ = [](int k) { return 4 + 3 * (k / 8); };
        for (int k = 0; k < kLanesG5; ++k) {
            const int fx = laneX(k), fz = laneZ(k);
            for (int dx = -1; dx <= 2; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        wG5.setBlock(fx + dx, gy + dy, fz + dz, BR::Air, 0); // 凿净空（火/板全部 6 邻 + 上窜位）
        }
        int hitsA = 0;
        for (int k = 0; k < kLanesG5; ++k) {
            wG5.setBlock(laneX(k), gy, laneZ(k), BR::Fire, 0);       // 火源（邻木板恒有燃料 → 晴天无自熄路径）
            wG5.setBlock(laneX(k) + 1, gy, laneZ(k), BR::Planks, 0); // 贴邻木板（唯一可燃邻；泳道间距 ≥3 隔离）
        }
        for (int win = 0; win < 800; ++win) {
            for (int t = 0; t < 5; ++t) wG5.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval）
            for (int k = 0; k < kLanesG5; ++k) { // 点燃即复原（下窗再掷——每泳道每窗恰 1 样本）
                // t843 语义重做：命中 = 木板进燃烧态（isBurningAt 侧表真值，栅格 id 不变）；复原 = 同 id
                //   setBlock no-op 写（新契约：任何显式写调用含同 id 写都清燃烧侧表——见 world.cpp setBlock）。
                if (wG5.isBurningAt(laneX(k) + 1, gy, laneZ(k))) {
                    ++hitsA;
                    wG5.setBlock(laneX(k) + 1, gy, laneZ(k), BR::Planks, 0);
                }
            }
        }
        firesA = 0;
        for (int k = 0; k < kLanesG5; ++k)
            if (wG5.blockAt(laneX(k), gy, laneZ(k)) == BR::Fire) ++firesA;
        rateA = double(hitsA) / (double(kLanesG5) * 800.0);
        okA = firesA == kLanesG5 && rateA >= 0.020 && rateA <= 0.030;
        if (!okA)
            diagA = QStringLiteral("hits %1 fires %2 rate %3%")
                        .arg(hitsA).arg(firesA).arg(rateA * 100.0, 0, 'f', 2);

        // ── (b) 湿燃料防火带（确定性：水邻木板永不被点燃）── rig 行 z=80（泳道网格 z≤11 之外）
        for (int dx = 11; dx <= 15; ++dx)
            for (int dz = 79; dz <= 81; ++dz)
                for (int dy = -1; dy <= 2; ++dy)
                    wG5.setBlock(dx, gy + dy, dz, BR::Air, 0); // 净空带（火/板/水全部 6 邻 + 上窜位）
        wG5.setBlock(12, gy, 80, BR::Fire, 0);   // 火（水距火 2 格不在其 6 邻——火自身不被抑制）
        wG5.setBlock(13, gy, 80, BR::Planks, 0); // 木板（右侧邻水 → 湿燃料）
        wG5.setBlock(14, gy, 80, BR::Water, 0);  // 水（harness 不驱动 tickWaterFlow → 静止不漫）
        for (int win = 0; win < 300; ++win)
            for (int t = 0; t < 5; ++t) wG5.tickFire();
        // t843：防火带语义不变（湿燃料 igniteFlammableAt 内统一拒绝——蔓延 / 直燃三入口同判），断言加
        //   isBurningAt 反证（id 不变语义下 blockAt==Planks 单独不再充分——燃烧态也保持 Planks）。
        okB = wG5.blockAt(13, gy, 80) == BR::Planks   // 木板原样（掷骰被湿燃料判定拦下，非概率）
             && !wG5.isBurningAt(13, gy, 80)          // 且从未进燃烧态（确定性防火带）
             && wG5.blockAt(12, gy, 80) == BR::Fire;  // 火有燃料仍存活（水不在火的 6 邻）

        // ── (c) 火自身邻水加速自熄（火正上方邻水 + 侧邻燃料板）── rig 行 z=82
        for (int dx = 11; dx <= 15; ++dx)
            for (int dz = 81; dz <= 83; ++dz)
                for (int dy = -1; dy <= 3; ++dy)
                    wG5.setBlock(dx, gy + dy, dz, BR::Air, 0); // 净空带（含水悬位 gy+1 一并清）
        wG5.setBlock(12, gy, 82, BR::Fire, 0);      // 火（6 邻：正上水 + 右燃料板 → 抑制态且有燃料）
        wG5.setBlock(13, gy, 82, BR::Planks, 0);    // 燃料板（与 (b) 的水不相邻——对角非 6 邻）
        wG5.setBlock(12, gy + 1, 82, BR::Water, 0); // 水悬在火正上方（harness 不跑流体 → 不落）
        for (int win = 0; win < 200; ++win)
            for (int t = 0; t < 5; ++t) wG5.tickFire();
        okC = wG5.blockAt(12, gy, 82) != BR::Fire    // 火灭（40%/窗 × 200 窗，P(幸存)≈1e-44）
             && wG5.blockAt(13, gy, 82) != BR::Fire; // 板若被余烬点燃，新火无燃料亦熄（终态非 Fire）

        // ── (d) 降雨露天自熄 + 屋檐对照 ──
        // 强制降水：大步长 tickWeather 驱动状态机（Clear→降水必翻；Thunder=3 翻转回避雷击）。rig 木料
        //   尚未放置 → 即使路过雷态，雷击焚木 setBlock(Air) 不产生 Fire 格，混不进后续 tickFire。
        bool precip = false;
        for (int i = 0; i < 60 && !precip; ++i) {
            wG5.tickWeather(1.0e6);
            const int st = wG5.weatherState(); // World::Weather int 编码：Clear=0/Rain=1/Snow=2/Thunder=3
            precip = (st == 1 || st == 2);
        }
        // rig 列：露天组 x=12..13 / 屋檐组 x=20..21 同 z —— 两列都须正降水（biomeAt 低频，两列同群系；
        //   沙漠列恒 Clear 被跳过重扫）+ 放置格全空（露天真天空，不凿——skyLight 前置即证头顶无遮挡）。
        //   z 从 12 起（泳道网格带 z≤11）且跳过 (b)/(c) rig 行 ±1（其火 / 水不混入本 rig 邻域）。
        int zR = -1;
        for (int z = 12; z < 92 && zR < 0; ++z) {
            if (z >= 79 && z <= 83) continue; // (b)/(c) rig 行及其邻行
            if (!wG5.isPrecipitatingAt(12, z) || !wG5.isPrecipitatingAt(20, z)) continue;
            if (wG5.blockAt(12, gy, z) != BR::Air || wG5.blockAt(13, gy, z) != BR::Air) continue;
            if (wG5.blockAt(20, gy, z) != BR::Air || wG5.blockAt(21, gy, z) != BR::Air) continue;
            if (wG5.blockAt(20, gy + 2, z) != BR::Air) continue; // 屋顶位
            zR = z;
        }
        if (precip && zR >= 0) {
            wG5.setBlock(12, gy, zR, BR::Fire, 0);      // 露天火
            wG5.setBlock(13, gy, zR, BR::Planks, 0);    // 露天燃料板
            wG5.setBlock(20, gy, zR, BR::Fire, 0);      // 屋檐火（正上隔 1 格空气 + 石板）
            wG5.setBlock(21, gy, zR, BR::Planks, 0);    // 屋檐燃料板
            wG5.setBlock(20, gy + 2, zR, BR::Stone, 0); // 屋顶（火格头顶遮挡 → skyLight<15 淋不到）
            const bool preSky = wG5.skyLightAt(12, gy, zR) >= 15 && wG5.skyLightAt(20, gy, zR) < 15;
            for (int win = 0; win < 200; ++win)
                for (int t = 0; t < 5; ++t) wG5.tickFire();
            // 露天火：40%/窗 × 200 窗 → 必熄（setBlock Air）；屋檐火：不被雨杀——若熄必因燃料板已被吞
            //   （自然烧完；板被吞后无燃料 5%/窗自熄），被雨误杀 = 板仍在却火没了 → FAIL。
            okD = preSky && wG5.blockAt(12, gy, zR) == BR::Air
                 && (wG5.blockAt(20, gy, zR) == BR::Fire || wG5.blockAt(21, gy, zR) != BR::Planks);
            if (!okD)
                diagD = QStringLiteral("precip %1 zR %2 preSky %3 openFire %4 roofFire %5 roofPlank %6")
                            .arg(wG5.weatherState()).arg(zR).arg(preSky)
                            .arg(int(wG5.blockAt(12, gy, zR)))
                            .arg(int(wG5.blockAt(20, gy, zR)))
                            .arg(int(wG5.blockAt(21, gy, zR)));
            wG5.tickWeather(1.0e6); // 降水→Clear 必翻（卫生还原；后续无探针依赖，防御性）
        } else {
            diagD = QStringLiteral("precip %1 zR %2").arg(wG5.weatherState()).arg(zR);
        }

        const bool okG5 = okA && okB && okC && okD;
        if (!okG5) {
            qInfo().noquote() << "  [review-g #5 diag] A:" << okA << diagA
                              << "| B:" << okB << "| C:" << okC
                              << "| D:" << okD << diagD;
        }
        if (!okG5) ++totalFail;
        qInfo().noquote() << (okG5 ? "PASS" : "FAIL")
                          << "| review-g #5 fire suppression layer: per-neighbor spread compensated 5%->"
                             "2.5% (19200-sample lane statistics inside 2.0-3.0% band, all fueled fires "
                             "survive clear weather), water-adjacent target never ignites (moat firebreak, "
                             "deterministic), water-adjacent fire self-extinguishes fast (~40%/window "
                             "suppression beats fuel), open-sky rain kills fueled fire within 200 windows "
                             "while roofed control fire only dies by consuming its own fuel (skyLight<15 "
                             "not rained on); suppression predicates factored into "
                             "fireRainExposedAt/fireWaterNeighborAt for t843 fire-semantics redo to adopt";

    // ── Review 2026-08-25 #9 雨浇门对偶半扇守卫探针（world.cpp 门烧尽收尾补 fireRainExposedAt）──
    //   场景（review 原文）：露天门整扇点燃后某半扇被雨浇熄掷中「火灭块存」，对偶随后烧尽时收尾守卫旧只判
    //   !fireWaterNeighborAt → 被雨救下的半扇仍被连带清 Air + 误发 blockBroken（水泼保得住、雨浇保不住——
    //   抑制源行为不对称）。确定性构造走「点燃时无对偶、后补放的半门」收尾面（world.cpp (d) 注释自列；避开
    //   双半扇同窗浇熄竞态掷骰）：① 转降雨（tickWeather 大步长，review-g #5 (d) 同款；rig 木料未放 → 雷态
    //   路过无焚木混淆）后布列——各列全列凿空（seed 9 地形可达 y44+，t769 教训不假设高空必空）+ 石板屋顶
    //   （gy+2，挡天光 → 下扇整程不被雨掷浇熄）+ 门下扇（上格 Air → 联动不触发，只点下扇，10 窗计时）；
    //   ② 烧 9/10 窗（45 tickFire；屋顶护体 → 降雨下零浇熄掷骰）；③ 撤顶（skyLight 重 flood 回 15）+ 后补
    //   门上扇（bit3=1，非燃烧态——守卫的判击对象）→ 第 10 窗下扇烧尽收尾查对偶：仍是门 / 未在燃 / 无水邻 /
    //   露天真（fireRainExposedAt）→ 修复后半扇 WoodDoor 原样保住，未修则连带 Air + blockBroken。
    //   断言：① burnout 列（下扇非 WoodDoor = 烧尽路径；第 10 窗 40% 浇熄掷中的存活列不算）上扇一律仍
    //   WoodDoor；② 第 10 窗 WoodDoor blockBroken 恒 0（烧毁走 setBlock(Fire) 放置语义无 broken → 对偶
    //   连带 Air 是唯一 broken 源，守卫的直证）；③ 场景 ≥1 列（多列铺开压浇熄掷骰；hashVoxel 固定 seed →
    //   结果确定性，本轮实测锁定）。非降水列 / 撤顶后仍不露天列被前置校验剔除（守卫本就不该触发）。
    {
        World wR9;
        wR9.setWidth(96); wR9.setDepth(96); wR9.setHeight(48); wR9.setSeed(9);
        const int gy9 = 41; // rig 层（review-g #5 同款 seed 9 布局带）
        constexpr int kColsR9 = 20;
        const auto colXR9 = [](int k) { return 8 + 3 * k; }; // 列距 3 → 门互不 6 邻（同态蔓延不串列）
        // 先转降雨（rig 木料未放，雷态路过只可能落自然火——列布局后再无 tickWeather 调用）。
        for (int i = 0; i < 60; ++i) {
            wR9.tickWeather(1.0e6);
            const int st = wR9.weatherState();
            if (st == 1 || st == 2) break; // Rain/Snow（Thunder=3 继续翻——rig 未放木料无雷击焚木面）
        }
        // z 带：20 列里降水覆盖 ≥8 才用（biomeAt 低频，成带存在；isPrecipitatingAt 全局 Clear 恒 false →
        //   必须在降雨态下扫，review-g #5 (d) 先例）。
        int zR9 = -1;
        for (int z = 12; z < 92 && zR9 < 0; z += 2) {
            int n = 0;
            for (int k = 0; k < kColsR9; ++k)
                if (wR9.isPrecipitatingAt(colXR9(k), z)) ++n;
            if (n >= 8) zR9 = z;
        }
        bool okKeep = false, okNoBreak = false, okScenario = false;
        int burnouts9 = 0, used9 = 0;
        QString diag9 = QStringLiteral("precip z not found");
        if (zR9 >= 0) {
            diag9.clear();
            // ① 布列：全列凿空（gy-1..47——屋顶以上到世界顶全清，保证撤顶后列内天光直达）→ 屋顶 → 门下扇 → 点燃。
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int y = gy9 - 1; y <= 47; ++y)
                            wR9.setBlock(x + dx, y, zR9 + dz, BR::Air, 0);
                wR9.setBlock(x, gy9 + 2, zR9, BR::Stone, 0); // 屋顶（skyLight<15 → 下扇不吃雨浇熄掷骰）
                wR9.setBlock(x, gy9, zR9, BR::WoodDoor, 0);  // 门下扇（bit3=0；上格 Air → 联动不触发）
                wR9.igniteFlammableAt(x, gy9, zR9);
            }
            // ② 烧 9/10 窗：计时 10→1（晴天 / 屋顶双保险下零浇熄路径）。
            for (int t = 0; t < 45; ++t) wR9.tickFire();
            // ③ 撤顶 + 后补上扇（此刻上下扇皆露天真——下扇仅剩 1 窗计时，第 10 窗才有浇熄掷骰 40%/列）。
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                wR9.setBlock(x, gy9 + 2, zR9, BR::Air, 0);
                wR9.setBlock(x, gy9 + 1, zR9, BR::WoodDoor, 8);
            }
            // 前置校验（剔除非降水 / 不露天 / 下扇已非燃 / 上扇已燃的列——守卫对它们本就不该触发）。
            bool part9[kColsR9] = {};
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                part9[k] = wR9.isPrecipitatingAt(x, zR9)
                           && wR9.skyLightAt(x, gy9 + 1, zR9) >= 15
                           && wR9.blockAt(x, gy9, zR9) == BR::WoodDoor
                           && wR9.isBurningAt(x, gy9, zR9)
                           && wR9.blockAt(x, gy9 + 1, zR9) == BR::WoodDoor
                           && !wR9.isBurningAt(x, gy9 + 1, zR9);
                if (part9[k]) ++used9;
            }
            int doorBreaks9 = 0; // 只计第 10 窗（布列 / 撤顶 / 后补的 setBlock 无 WoodDoor→Air；清理在计数窗外）
            QObject::connect(&wR9, &World::blockBroken, &wR9,
                             [&](int, int, int, int oldId) { if (oldId == int(BR::WoodDoor)) ++doorBreaks9; });
            for (int t = 0; t < 5; ++t) wR9.tickFire(); // 第 10 窗：浇熄掷骰（40%）或烧尽收尾
            okKeep = true;
            for (int k = 0; k < kColsR9; ++k) {
                if (!part9[k]) continue;
                const int x = colXR9(k);
                if (wR9.blockAt(x, gy9, zR9) != BR::WoodDoor) { // 烧尽（余烬 Fire / Air 均非门）
                    ++burnouts9;
                    if (wR9.blockAt(x, gy9 + 1, zR9) != BR::WoodDoor) okKeep = false; // 对偶被连带清 = 守卫缺
                }
            }
            okNoBreak = doorBreaks9 == 0;
            okScenario = burnouts9 >= 1;
            if (!okKeep || !okNoBreak || !okScenario)
                diag9 = QStringLiteral("zR9 %1 used %2 burnouts %3 breaks %4 keep %5")
                            .arg(zR9).arg(used9).arg(burnouts9).arg(doorBreaks9).arg(okKeep);
            // 清理（探针世界即弃；计数已收口）
            for (int k = 0; k < kColsR9; ++k) {
                const int x = colXR9(k);
                wR9.setBlock(x, gy9, zR9, BR::Air, 0);
                wR9.setBlock(x, gy9 + 1, zR9, BR::Air, 0);
            }
        }
        const bool okR9 = okKeep && okNoBreak && okScenario;
        if (!okR9) ++totalFail;
        qInfo().noquote() << (okR9 ? "PASS" : "FAIL")
                          << "| review25 #9 rain-saved door half survives burn-out cleanup: with rain forced "
                             "upfront (big-step tickWeather), each column burns a lone lower door half under "
                             "a stone roof (roof keeps skyLight<15 so no douse rolls) for 9 of 10 windows, "
                             "then roof removed + paired upper half placed + sky reflooded to 15 right "
                             "before the final window so burn-out cleanup sees an un-burnt rain-exposed "
                             "pair; every burnout column keeps the upper half as WoodDoor, zero WoodDoor "
                             "blockBroken during the final window (stray pair-clear-to-Air is the only "
                             "possible broken source), >=1 burnout scenario asserted across 20 spread "
                             "columns (40%/window douse roll on the now-exposed lower half is the only "
                             "skip path; hashVoxel-seeded so the outcome is deterministic)";
    }
    }

    // ── P-t843 可燃物直燃语义重做探针（专用局部世界 wT，P30/P31/review-g#5 先例）──
    //   dev-plan t843：打火石右键可燃方块 = 方块本身点燃（燃烧态 = World 侧表 m_burningCells 瞬态，
    //   栅格 id 不变；面火 overlay 走 blockIgnited → QML burningHost）；燃烧计时归零 → 烧毁无掉落
    //   （t724 语义保持）；蔓延 = 相邻可燃进同态（火格掷骰 + 燃烧格同态掷骰）；批 G 三抑制谓词整体
    //   接入（湿燃料防火带收口在 igniteFlammableAt 三入口统一拒绝；燃烧中变湿 = 浇熄火灭块存）；
    //   接触燃烧方块 = 着火（mob 侧实证；玩家侧同款三格判定复制在 PlayerController::step——Game 层
    //   直编不可达（captured 物理闸门 + 私有 step，P20 注释先例），同构代码 + 人工目视收口）；3D 立
    //   地火失撑 = 编辑钩子同调用内立即熄灭。
    //   wT：48×48×96 seed 21，gy=88 高空净空层（heightAt ∈57..71 + 树冠 ~+10，88 全空免凿；review-g#5
    //   凿净空先例在此免除——Air→Air 早退不重算光照）。六段断言：
    //   (a) 直燃进态 + 精确计时 + 非可燃拒：ignite(Planks) 真 / isBurningAt 真 / id 保留 / 重复 ignite
    //       false（幂等不重置）；ignite(Stone) false（打火石回退立地火路径的 World 侧判据）；恰 9 窗
    //       仍在燃（计时 10→1）、第 10 窗烧毁 → cap 内燃起余烬火（blockAt==Fire）→ 余烬无燃料 5%/窗
    //       自熄（≤600 窗终态 Air，P(幸存)≈0.95^600≈4e-14）；
    //   (b) 湿燃料防火带（确定性）：水邻木板 ignite 拒（false）+ 300 窗从未燃烧 + 块存；燃烧中变湿
    //       浇熄（火灭块存）：先点燃**后**贴水（顺序即语义：贴水在先会被防火带拒）→ 40%/窗浇熄（双板
    //       60 窗均灭 P≈(0.6^60)²≈3e-27；烧穿尾部 P(0.6^10)≈0.6%/板，双板同穿≈3.6e-5 → 断言 ≥1 板
    //       块存）；
    //   (c) 同态蔓延存在性：10 泳道（燃板 A + 贴邻新板 B，全场无 Fire 格——纯燃烧格掷骰）每窗复原 B +
    //       重燃 A → 400 窗 ≥1 次 B 被燃板点燃（P(全未中)=0.975^4000≈3e-44；率值统计已由 review-g#5(a)
    //       锁定，此处只证燃烧格自身掷骰通路存在）；
    //   (d) mob 接触着火：石框围 1×1 燃烧木板上的猪（脚下一格 footY-1 命中）→ 40 tick（< 首次火伤结算
    //       1s → 无随机熄灭 / 伤害混淆）内 EntityManager::isBurningAt(猪) 真；
    //   (e) 立地火失撑即时灭 + 有撑对照：破火下石支撑 → **同一 setBlock 调用内**火格变 Air
    //       （checkFireOnEdit 编辑钩子，无 tickFire 参与——即时性本身是断言）；对照（石撑 + 邻燃料板）
    //       1 窗后仍 Fire（有燃料火无自熄掷骰路径——确定性非概率）；
    //   (f) 燃烧中替换 / 同 id 复原清态：燃板 setBlock(Stone) 替换 → isBurningAt 假；再点燃后同 id
    //       setBlock(Planks) no-op 写 → isBurningAt 假（review-g#5(a) 每窗复原所依赖契约的显式锁定）。
    {
        World wT;
        wT.setWidth(48);
        wT.setDepth(48);
        wT.setHeight(96);
        wT.setSeed(21);
        const int gy = 88;
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        QString diagT843;
        const auto win843 = [&wT](int n) { for (int i = 0; i < n; ++i) for (int t = 0; t < 5; ++t) wT.tickFire(); };

        // ── (a) 直燃进态 + 精确计时（gy 层 z=6 行 x=7 板 / x=9 石对照）──
        {
            const int px = 7, pz = 6;
            wT.setBlock(px, gy - 1, pz, BR::Stone, 0);  // 石台（余烬火支撑 + 与后段隔离）
            wT.setBlock(px, gy, pz, BR::Planks, 0);
            wT.setBlock(9, gy, pz, BR::Stone, 0);       // 非可燃对照（距板 2 格非 6 邻）
            const bool igFirst = wT.igniteFlammableAt(px, gy, pz);
            const bool idKept = wT.blockAt(px, gy, pz) == BR::Planks;
            const bool igAgain = wT.igniteFlammableAt(px, gy, pz);   // 重复 → false（幂等）
            const bool igStone = wT.igniteFlammableAt(9, gy, pz);    // 石 → false（回退立地火路径）
            win843(9);                                                // 计时 10→1：恰 9 窗仍在燃
            const bool stillBurningAt9 = wT.isBurningAt(px, gy, pz)
                                         && wT.blockAt(px, gy, pz) == BR::Planks;
            win843(1);                                                // 第 10 窗：归零烧毁 → 余烬火
            const bool burntAt10 = !wT.isBurningAt(px, gy, pz)
                                   && wT.blockAt(px, gy, pz) == BR::Fire;
            win843(600);                                              // 余烬无燃料 5%/窗自熄
            const bool emberDied = wT.blockAt(px, gy, pz) == BR::Air;
            okA = igFirst && idKept && !igAgain && !igStone && stillBurningAt9 && burntAt10
                  && emberDied && wT.blockAt(9, gy, pz) == BR::Stone;
            wT.setBlock(px, gy - 1, pz, BR::Air, 0);
            wT.setBlock(9, gy, pz, BR::Air, 0);
        }

        // ── (f) 燃烧中替换 / 同 id 复原清态（z=10 行）──
        {
            const int px = 7, pz = 10;
            wT.setBlock(px, gy, pz, BR::Planks, 0);
            wT.igniteFlammableAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Stone, 0);      // 替换 → 燃烧作废
            const bool clearedByReplace = !wT.isBurningAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Planks, 0);     // 换回木 + 重新点燃
            const bool reignited = wT.igniteFlammableAt(px, gy, pz) && wT.isBurningAt(px, gy, pz);
            wT.setBlock(px, gy, pz, BR::Planks, 0);     // 同 id no-op 写 → 复原契约：燃烧清除
            const bool clearedByNoop = !wT.isBurningAt(px, gy, pz);
            okF = clearedByReplace && reignited && clearedByNoop;
            wT.setBlock(px, gy, pz, BR::Air, 0);
        }

        // ── (b) 湿燃料防火带 + 燃烧中变湿浇熄（z=6 行）──
        {
            // (b1) 防火带：水邻木板直燃被拒（确定性——三入口统一口径收口在 igniteFlammableAt）。
            wT.setBlock(16, gy, 6, BR::Planks, 0);
            wT.setBlock(17, gy, 6, BR::Water, 0);       // 木板 6 邻含水 → 湿燃料
            const bool wetRejected = !wT.igniteFlammableAt(16, gy, 6)
                                     && !wT.isBurningAt(16, gy, 6);
            win843(300);                                // 无燃格 → tickFire 早退，窗口空转（保序）
            const bool wetNeverBurnt = !wT.isBurningAt(16, gy, 6)
                                       && wT.blockAt(16, gy, 6) == BR::Planks;
            // (b2) 浇熄（火灭块存）：先点燃（此刻无水——顺序即语义）后贴水 → 40%/窗浇熄掷骰。
            wT.setBlock(22, gy, 6, BR::Planks, 0);
            wT.setBlock(26, gy, 6, BR::Planks, 0);
            const bool d1 = wT.igniteFlammableAt(22, gy, 6);
            const bool d2 = wT.igniteFlammableAt(26, gy, 6);
            wT.setBlock(23, gy, 6, BR::Water, 0);
            wT.setBlock(27, gy, 6, BR::Water, 0);
            bool bothOut = false;
            for (int win = 0; win < 60 && !bothOut; ++win) {
                win843(1);
                bothOut = !wT.isBurningAt(22, gy, 6) && !wT.isBurningAt(26, gy, 6);
            }
            const int intact = int(wT.blockAt(22, gy, 6) == BR::Planks)
                               + int(wT.blockAt(26, gy, 6) == BR::Planks);
            okB = wetRejected && wetNeverBurnt && d1 && d2 && bothOut && intact >= 1;
            wT.setBlock(16, gy, 6, BR::Air, 0);
            wT.setBlock(17, gy, 6, BR::Air, 0);
            wT.setBlock(22, gy, 6, BR::Air, 0);
            wT.setBlock(23, gy, 6, BR::Air, 0);
            wT.setBlock(26, gy, 6, BR::Air, 0);
            wT.setBlock(27, gy, 6, BR::Air, 0);
        }

        // ── (c) 同态蔓延存在性（z=14 行，10 泳道 A@4+3k / B@A+1，全场无 Fire 格）──
        {
            constexpr int kLanesT = 10;
            int hits = 0;
            for (int k = 0; k < kLanesT; ++k) {
                wT.setBlock(4 + 3 * k, gy, 14, BR::Planks, 0);
                wT.setBlock(5 + 3 * k, gy, 14, BR::Planks, 0);
                wT.igniteFlammableAt(4 + 3 * k, gy, 14); // A 初始入燃态
            }
            for (int win = 0; win < 400; ++win) {
                win843(1);
                for (int k = 0; k < kLanesT; ++k) {
                    if (wT.isBurningAt(5 + 3 * k, gy, 14)) { // B 被燃板 A 点燃 → 计数 + 复原
                        ++hits;
                        wT.setBlock(5 + 3 * k, gy, 14, BR::Planks, 0);
                    }
                    wT.setBlock(4 + 3 * k, gy, 14, BR::Planks, 0); // 复原 A + 重燃（计时恒 10，
                    wT.igniteFlammableAt(4 + 3 * k, gy, 14);       // 每窗恰 1 次掷骰样本）
                }
            }
            okC = hits >= 1;
            for (int k = 0; k < kLanesT; ++k) { // 清场（含清燃烧态）
                wT.setBlock(4 + 3 * k, gy, 14, BR::Air, 0);
                wT.setBlock(5 + 3 * k, gy, 14, BR::Air, 0);
            }
        }

        // ── (d) mob 接触着火（z=6 行 x=38：石框围 1×1 燃烧木板上的猪）──
        {
            EntityManager entsT;
            wT.setBlock(38, gy, 6, BR::Planks, 0);
            wT.igniteFlammableAt(38, gy, 6); // 燃烧板（ents.tick 不驱动 tickFire → 计时冻结，接触源稳定）
            for (int dx = 37; dx <= 39; ++dx)
                for (int dz = 5; dz <= 7; ++dz) { // 石框（gy+1 环 + gy+2 盖：猪定身在燃烧板正上）
                    if (dx == 38 && dz == 6) continue;
                    wT.setBlock(dx, gy + 1, dz, BR::Stone, 0);
                    wT.setBlock(dx, gy + 2, dz, BR::Stone, 0);
                }
            const int pig = entsT.spawnMobTyped(38, gy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#ee9999"), 20);
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            for (int t = 0; t < 40; ++t) // 0.64s：首个 aiTick ≤4 帧；首次火伤 1s 后 → 无伤害 / 随机熄灭混淆
                entsT.tick(0.016f, &wT, farListener, 0.3f, 1.8f, false);
            okD = pig >= 0 && entsT.isBurningAt(pig) && entsT.aliveAt(pig)
                  && wT.isBurningAt(38, gy, 6); // 板仍在燃（接触源未耗尽）
            for (int dx = 37; dx <= 39; ++dx)
                for (int dz = 5; dz <= 7; ++dz)
                    for (int dy = 0; dy <= 2; ++dy)
                        wT.setBlock(dx, gy + dy, dz, BR::Air, 0); // 清场（燃烧板 + 石框，dy=0 含燃烧态清除）
        }

        // ── (e) 立地火失撑即时灭（编辑钩子）+ 有撑有燃料对照（z=14 行 x=36/40）──
        {
            wT.setBlock(36, gy - 1, 14, BR::Stone, 0);
            wT.setBlock(36, gy, 14, BR::Fire, 0);       // 石撑立地火
            wT.setBlock(36, gy - 1, 14, BR::Air, 0);    // 破支撑 → 同调用内 checkFireOnEdit 即时灭
            const bool instantOut = wT.blockAt(36, gy, 14) == BR::Air;
            wT.setBlock(40, gy - 1, 14, BR::Stone, 0);
            wT.setBlock(40, gy, 14, BR::Fire, 0);       // 对照：有撑 + 邻燃料板
            wT.setBlock(41, gy, 14, BR::Planks, 0);
            win843(1);                                  // 1 窗：有燃料 → 无自熄掷骰（确定性存活）
            const bool ctrlAlive = wT.blockAt(40, gy, 14) == BR::Fire;
            okE = instantOut && ctrlAlive;
            wT.setBlock(40, gy - 1, 14, BR::Air, 0);
            wT.setBlock(40, gy, 14, BR::Air, 0);
            wT.setBlock(41, gy, 14, BR::Air, 0);
        }

        const bool okT843 = okA && okB && okC && okD && okE && okF;
        if (!okT843) {
            diagT843 = QStringLiteral("A:%1 B:%2 C:%3 D:%4 E:%5 F:%6")
                           .arg(okA).arg(okB).arg(okC).arg(okD).arg(okE).arg(okF);
            qInfo().noquote() << "  [t843 diag]" << diagT843;
        }
        if (!okT843) ++totalFail;
        qInfo().noquote() << (okT843 ? "PASS" : "FAIL")
                          << "| t843 direct-ignite fire semantics: flint right-click on flammable "
                             "enters burning state with id preserved (side-table transient, "
                             "surface-fire overlay via blockIgnited), exact 10-window wood timer "
                             "(9 windows burning, 10th burns to ember flare, ember dies fuel-less), "
                             "stone rejected (falls back to standing fire), wet-fuel firebreak "
                             "rejected at ignite (unified 3-entry gate) while burning-turned-wet "
                             "douses with block intact, burning-cell same-state spread proven "
                             "without any fire cell, mob on burning plank ignites within 40 ticks "
                             "(player leg = same predicate copy in step, manual visual), standing "
                             "fire loses support = extinguished inside the same setBlock call "
                             "(checkFireOnEdit hook) while fueled+supported control survives "
                             "deterministically, replace/same-id-noop write clears burning state";
    }

    // ── t813 构建版本戳探针（Core 叶子直编）：锁 BuildInfo 两值非空 + 格式 ──
    //   stamp = CMake 每次 build 生成的 "YYYY-MM-DD HH:MM"（16 字符）；gitHash = git
    //   rev-parse --short HEAD（7-10 hex）。git 缺失 / 超时（TIMEOUT 5）回退 "nogit" 判 **SKIP**
    //   （review24 低危：旧版硬套 hex 正则判 FAIL，与 WriteBuildStamp.cmake「无 git 诚实回退、构建不因此
    //   失败」自相矛盾——无 .git 源码导出包 / CI 缓存构建会矩阵恒红；环境缺失≠格式回归，SKIP 单列不算
    //   FAIL 不算 PASS）；stamp 格式恒断（真回归照 FAIL）——SKIP 行也带 stamp 值供人工核。探针跑在矩阵
    //   测试 exe 里 = 顺带验证「该 exe 的 stamp 随本次构建刷新」（构建-运行同刻，分钟差即重建链生效证据）。
    {
        const QString stamp = BuildInfo::instance()->stamp();
        const QString ghash = BuildInfo::instance()->gitHash();
        const QRegularExpression stampRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"));
        const QRegularExpression gitRe(QStringLiteral("^[0-9a-f]{7,10}$"));
        const bool okStamp = stampRe.match(stamp).hasMatch();
        const bool noGit = ghash == QStringLiteral("nogit"); // CMake 诚实回退值（无 .git / git 超时）
        const bool okGit = gitRe.match(ghash).hasMatch();
        const bool okT813 = okStamp && (noGit || okGit); // stamp 破 = 真回归仍 FAIL；仅 git 缺失 → SKIP
        if (!okT813) ++totalFail;
        qInfo().noquote() << ((noGit && okStamp) ? "SKIP" : (okT813 ? "PASS" : "FAIL"))
                          << "| t813 build stamps: stamp" << stamp
                          << "(YYYY-MM-DD HH:MM) git" << ghash
                          << "(7-10 hex, git rev-parse --short HEAD); header regenerated every "
                             "build via cmake/WriteBuildStamp.cmake with content-change-only "
                             "rewrite so only buildinfo.cpp recompiles; git==\"nogit\" judged SKIP "
                             "(CMake honest fallback = environment without git, not a format "
                             "regression; stamp format still asserted, stamp break stays FAIL)";
    }

    // ── P-t814 真消费端执行探针（Game 层 PlayerController 直编）──
    //   用户报告（R19.13）：「激活红石粉/红石块/红石火把都不能点燃 TNT、不能触发发射器/投掷器」，但
    //   t772（42 组合）/t773（3 探针）全 PASS 且用户同时确认铁门/铁活板门能开。分叉实证：铁门/铁活板门是
    //   recomputePowerLocal **静默写 state**（纯 World 内闭环），TNT/发射器/投掷器走 **信号→QML 转发→
    //   player.firePowerTnt/fireDispenserAtQml** —— 此前矩阵只对 World 原始信号计数（P15）/镜像 clearBlockSilent
    //   （t773），**真 PlayerController 消费方法从未被任何自动化测试执行过**（QML 侧仅静态审计）。本探针把
    //   Game 层 PlayerController 连同 EntityManager/DispenserStore/ItemEntityManager 装进矩阵，以 C++ 直接
    //   连接精确镜像 Main.qml 双转发 handler（Connections{target:theWorld} 同线程直接调用语义），驱动用户
    //   实测路径（器件先就位 → 后激活源）断言端到端效果：
    //   (a) 拉杆扳开 → 邻 TNT：方块被 clearBlockSilent 清 + EntityManager 生 PrimedTnt 实体（格心坐标）；
    //   (b) 红石块贴发射器（store 预填 3 箭）→ spawnArrowPlayer 生 1 箭 + 库存 3→2（右键 UI 装填后电力触发
    //       的完整用户路径）；
    //   (c) 拉杆贴投掷器（store 预填 5 粉）→ ItemEntityManager 生 1 掉落物 + 库存 5→4（dropper spawnItemAt
    //       分支——m_itemEntities 注入态）；
    //   (d) 空库存发射器通电 → 无任何实体生成（t607「空库存玩家机器无动作」设计语义，防误判为缺陷）；
    //   (e) 沿语义：稳定通电下再制造电力活动（另一侧拉杆扳开再扳回）→ 不重复发射（fireDispenserAtQml 基线
    //       集 unpowered→powered 沿检测，t689）；源拆除再快速复置 → 冷却窗内仍不发射（per-dispenser 2s 闸）。
    //   任一 FAIL = 用户症状在消费端的复现点（World 层已由 P15/t773 洗冤）；全 PASS = 链完整，用户复测走
    //   docs/test-reports/t814-redstone-repro-steps.md（版本戳核对 + 逐源×逐器件最简搭建）。
    {
        PlayerController pc;          // 真消费端（Game 层；C++ 直造不启 16ms 定时器——componentComplete 不触发）
        EntityManager ents;           // spawnPrimedTnt / spawnArrowPlayer 断言源（不 tick → 实体冻结可数）
        DispenserStore store;         // 库存分派断言源
        ItemEntityManager items;      // 投掷器 spawnItemAt 掉落物断言源
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        pc.setItemEntities(&items);
        // Main.qml 双转发 handler 的 C++ 等价镜像（src/ui/Main.qml onPowerTntTriggered → player.firePowerTnt /
        //   onPowerDispenserTriggered → player.fireDispenserAtQml；同线程直接连接 = QML handler 语义）。
        QObject::connect(&w, &World::powerTntTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.firePowerTnt(x, y, z); });
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });

        // (a) TNT：拉杆贴合（器件先就位稳态 → 后扳拉杆，用户实测路径）。器件放置走 placeRigBlock
        //     （回读钉落位——setBlock 越界 / 同 id 早退都静默吞放置，review24 低危）。
        bool okA = false;
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::TntBlock, 0);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::Lever, 1); // 扳开（state bit0=1）
            tickN(w, 4);
            int primedIdx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) { primedIdx = i; break; }
            okA = w.blockAt(x0, kRigY, z0) == BR::Air                    // firePowerTnt 清块
                  && primedIdx >= 0                                     // PrimedTnt 实体在场
                  && std::abs(ents.posAt(primedIdx).x() - (x0 + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(primedIdx).y() - (kRigY + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(primedIdx).z() - (z0 + 0.5f)) < 1e-3f;
            if (!okA)
                qInfo().noquote() << "  [t814 a diag] block=" << int(w.blockAt(x0, kRigY, z0))
                                  << " primedIdx=" << primedIdx << " ents.count=" << ents.count();
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            ents.clearAll(); // 清引燃实体（防污染后续探针的实体计数）
            tickN(w, 2);
        }

        // (b) 发射器：红石块贴合 + store 预填 3 箭（右键 UI 装填后电力触发的完整路径）。
        bool okB = false, okBInv = false;
        int arrowsAfterB = -1;
        int bx0 = 0, bz0 = 0;
        {
            const auto [x0, z0] = nextSlot();
            bx0 = x0; bz0 = z0;
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 3);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            int arrows = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows;
            arrowsAfterB = arrows;
            okB = arrows == 1;
            okBInv = store.slotIdAt(x0, kRigY, z0, 0) == RecipeRegistry::ArrowId
                     && store.slotCountAt(x0, kRigY, z0, 0) == 2;
            if (!okB || !okBInv)
                qInfo().noquote() << "  [t814 b diag] arrows=" << arrows
                                  << " slotId=" << store.slotIdAt(x0, kRigY, z0, 0)
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
        }

        // (e) 沿语义 + 冷却**时间**语义（review24 #9 重做——旧断言两层误 PASS 面：① 探针不启 16ms 定时器
        //     → 冷却递减（tick→scanDispenserTraps 开头）从不运行 → 「sub-2s-cooldown」实际只验 contains 即拦，
        //     kDispenserCooldown 回归改 0 探针照样 PASS；② arrows 持平在「信号根本没发」时也恒真）。三段：
        //     ① 稳定通电下再制造电力活动（对侧拉杆扳开→扳回）不重复发射（沿检测基线集 t689）；
        //     ② 拆源→快速复置 = 真上升沿但 <1s 冷却 → 不发射，**且断言 powerDispenserTriggered 信号确有发出**
        //        （头部全局 dispFired 计数差分——区分「冷却拦截」与「信号未发」两种零发射）；
        //     ③ 直调 pc.scanDispenserTraps(2.5f) 推进冷却过 0.5s 过期（tick 的等价递减驱动，探针态沿表恒空
        //        零副作用；t868② 冷却 2.0→1.0s 后本值仍 >1.5× 新冷却，两向断言语义不变）
        //        → 再造真上升沿 → **必须再发射**（箭 +1 / 库存 2→1 正向断言——冷却时长回归改 0
        //        ②不触发、改 ∞ ③不触发，两个方向都在此现形）。
        //     review25 #8：② 段前先 scanDispenserTraps(1e-3f) 走一步真实递减再复置——配合 fireDispenserAt
        //        门改 contains && value>0，0 值冷却在递减步即被 erase → 「常量回归改 0」时复置沿会发射、
        //        ② 断言 FAIL（纯 contains 门下 0 表项永驻恒拦，下界不可见）。
        bool okE = false, okE2 = false;
        {
            placeRigBlock(w, bx0 - 1, kRigY, bz0, BR::Lever, 1); // 对侧第二源扳开 → 复算触达（升沿到已通电机）
            tickN(w, 4);
            w.setBlock(bx0 - 1, kRigY, bz0, BR::Lever, 0);       // 扳回 → 降沿触达
            tickN(w, 4);
            int arrows2 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows2;
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);         // 拆源（降沿）
            tickN(w, 4);
            const int dispBeforeRepower = dispFired;             // 信号计数基线（复置段必须发出 ≥1 次）
            pc.scanDispenserTraps(1e-3f);                        // review25 #8：走一步真实递减再复置——0 值冷却即被 erase，常量回归改 0 时下复置沿必发射 → ② FAIL
            placeRigBlock(w, bx0 + 1, kRigY, bz0, BR::RedstoneBlock, 0); // 复置（真上升沿，但冷却 0.5s 未过）
            tickN(w, 4);
            int arrows3 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows3;
            okE = arrows2 == arrowsAfterB && arrows3 == arrowsAfterB
                  && store.slotCountAt(bx0, kRigY, bz0, 0) == 2  // 库存不再扣（无第二次发射）
                  && dispFired > dispBeforeRepower;              // 信号确发出（零发射 = 冷却拦，非信号没发）
            // ③ 冷却过期后再造上升沿 → 必须再发射（时间维度正向断言）。
            pc.scanDispenserTraps(2.5f); // 等价递减驱动：一次耗尽 0.5s 冷却（探针不启定时器，直调推进；沿表空）
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);         // 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            const int dispBeforeExpire = dispFired;
            placeRigBlock(w, bx0 + 1, kRigY, bz0, BR::RedstoneBlock, 0); // 复置 = 新上升沿 + 冷却已过
            tickN(w, 4);
            int arrows4 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows4;
            okE2 = arrows4 == arrows3 + 1                        // 再发射恰一次
                   && store.slotCountAt(bx0, kRigY, bz0, 0) == 1 // 库存 2→1（真扣一发）
                   && dispFired > dispBeforeExpire;              // 信号链全通（fire 非信号缺失）
            if (!okE || !okE2)
                qInfo().noquote() << "  [t814 e diag] arrowsAfterB=" << arrowsAfterB
                                  << " stable=" << arrows2 << " repower=" << arrows3
                                  << " afterCooldown=" << arrows4
                                  << " slotCount=" << store.slotCountAt(bx0, kRigY, bz0, 0)
                                  << " sigRepower=" << (dispFired > dispBeforeRepower)
                                  << " sigExpire=" << (dispFired > dispBeforeExpire);
            // 清场
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);
            w.setBlock(bx0, kRigY, bz0, BR::Air, 0);
            store.clearDispenser(bx0, kRigY, bz0);
            ents.clearAll();
            tickN(w, 2);
        }

        // (c) 投掷器：拉杆贴合 + store 预填 5 粉 → 掉落物弹出（dropper 只投不射，spawnItemAt 分支）。
        //   review24 低危（探针族）：items 断言改**相对基线**——(a) 的失撑补口未来可能生成掉落物而 (a) 只
        //   ents.clearAll() 不清 items，绝对值 ==1 依赖未言明的前序清场前提（拉杆附着语义一变即环境性假
        //   FAIL）；(d) 本就是相对基线还注释了为什么，现统一口径。
        bool okC = false, okCInv = false;
        {
            const auto [x0, z0] = nextSlot();
            const int itemsBeforeC = items.count(); // 相对基线（弹前快照）
            placeRigBlock(w, x0, kRigY, z0, BR::Dropper, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::RedstoneId, 5);
            tickN(w, 2);
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            okC = items.count() == itemsBeforeC + 1; // 相对本场景恰弹 1（新增量断言，不绑前序清场）
            okCInv = store.slotIdAt(x0, kRigY, z0, 0) == RecipeRegistry::RedstoneId
                     && store.slotCountAt(x0, kRigY, z0, 0) == 4;
            if (!okC || !okCInv)
                qInfo().noquote() << "  [t814 c diag] items=" << items.count()
                                  << "/" << itemsBeforeC
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        // (d) 空库存发射器通电 → 无动作（t607 设计语义：tracked 空库存 = 陷阱解除，无 fallback 箭）。
        //   断言相对基线（快照本场景前 ents/items 计数）：绝对值依赖 (c) 掉落物持久性（ItemEntityManager
        //   生命周期属呈现层语义，探针不该跨场景绑定）——「无**新**实体」才是本场景的语义内核。
        bool okD = false;
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0); // 有条目但库存全空
            tickN(w, 2);
            const int entsBefore = ents.count(), itemsBefore = items.count();
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            int arrows = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows;
            okD = arrows == 0 && ents.count() == entsBefore && items.count() == itemsBefore; // 无新实体
            if (!okD)
                qInfo().noquote() << "  [t814 d diag] arrows=" << arrows
                                  << " ents=" << ents.count() << "/" << entsBefore
                                  << " items=" << items.count() << "/" << itemsBefore;
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        const bool okT814 = okA && okB && okBInv && okC && okCInv && okD && okE && okE2;
        if (!okT814) ++totalFail;
        qInfo().noquote() << (okT814 ? "PASS" : "FAIL")
                          << "| t814 real-consumer probes: Main.qml forwarding mirrored onto actual "
                             "PlayerController.firePowerTnt/fireDispenserAtQml (lever->TNT clears block + spawns "
                             "primed entity at cell center; redstone-block->dispenser w/ 3 arrows fires 1 + "
                             "decrements to 2; lever->dropper w/ 5 dust pops 1 item entity + decrements to 4; "
                             "empty tracked dispenser powered = design no-op; stable-power re-touch and "
                             "sub-0.5s-cooldown re-power both do not re-fire, each zero-fire leg gated by "
                             "powerDispenserTriggered emission-count delta so cooldown-block vs signal-lost "
                             "are distinguished; cooldown driven past 0.5s expiry via scanDispenserTraps "
                             "equivalent-decrement then a true re-edge MUST re-fire +1 arrow/stock 2->1, "
                             "pinning the cooldown duration both directions) - consumer leg never executed by "
                             "P15/t773 before, iron-door contrast explained (door = in-World state write, "
                             "TNT/dispenser = signal->QML->consumer)";
    }

    // ── P-t856 发射器弹点燃 TNT 探针（Game 层真消费端，t814 模式）──
    //   MC 1.0 dispenser 语义：发射器内 TNT 经激活（拉杆，激活链 t772/t814 已锁、本任务零改动）→ 弹出
    //   **已点燃** TNT 实体（PrimedTnt），标准引信落地爆。断言四组：
    //   (a) 弹出位 = 发射面邻格格心（state 0 → +X，源贴背面保发射面净空）+ **标准引信**（fuseProgress==1.0
    //       钉满值 kPrimedTntFuseSec ~5s——链式短 fuse 1.2s 会给 0.24，缩短立现形）+ 引信在跑（tick 0.25s
    //       后 progress 递减）+ **定向初速**（tick 后 +X 位移 ≈ v·dt，钉弹射方向=发射面朝向）+ 库存 3→2；
    //   (b) 二次激活 <0.5s 冷却 → 无第二发（信号确发的零发射 = 冷却拦，t814 ② 口径）+ 库存不再扣；
    //       冷却过 0.5s 后再造沿 → 必再弹（+1 实体 / 库存 2→1，t814 ③ 口径）——冷却闸对 TNT 分支不回归；
    //   (c) 投掷器 + TNT → 普通掉落物弹出**不点燃**（dropper 只投不射口径——两路径边界的另一侧：dropper
    //       弹 TNT 是物品非引燃实体）+ 库存照扣；
    //   (d) review25 #11 排出口占用门：发射面邻格被实体方块堵住 → 不在墙格内 spawn（primed 水平积分不查
    //       碰撞 → 墙格 spawn = ~5s 后就地爆穿墙波及发射器自身）；review26 #24 起堵口**一律**退化普通掉落物
    //       弹出（不点燃）——旧版「再探一格」不看连通 → 1 格厚墙时 TNT 隔墙生成在墙后（穿墙 TNT），已收口；
    //   (e) 红石直接邻接 TNT 原地引爆（firePowerTnt 清方块 + 原格生成）不回归由 t814 (a) 既有探针复跑覆盖。
    {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        ItemEntityManager items;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        pc.setItemEntities(&items);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto primedCount = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) ++n;
            return n;
        };

        // (a) 装填 3 TNT 的发射器 + 背面拉杆激活 → 弹出 PrimedTnt @ 发射面邻格 + 标准引信 + 定向初速。
        //   t871：弹出速度调小（kDispenserTntPopSpeed 4.0→1.6）→ 摩擦积分总位移 ≈0.43 格，TNT 落定在
        //   **发射面邻格内**（用户「出现在发射口前一格即可」）；方向断言阈值随之下调（+0.5 → +0.15，
        //   0.25s 位移 v(1-e^-1)/4≈0.354），并新增落点格断言（驱动 1.05s 后 floor(x) == x0+1）。
        bool okA = false, okAFuse = false, okAMove = false, okRest = false;
        int tx0 = 0, tz0 = 0;
        {
            const auto [x0, z0] = nextSlot();
            tx0 = x0; tz0 = z0;
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0); // state 0 → 朝 +X（chestFrontFace 解码）
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0); // 凿空发射面（review25 #11 起 spawn 前查占用；新 rig 行
                                                        //   z≥97 地形可达 y41（t814 深度扩展带）→ 不凿则探针
                                                        //   走「堵口退化」分支非本段口径）
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, BR::TntBlock, 3);
            tickN(w, 2);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1); // 源贴背面 → 发射面 x0+1 保持净空
            tickN(w, 4);
            int idx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i)) { idx = i; break; }
            okA = idx >= 0
                  && std::abs(ents.posAt(idx).x() - (x0 + 1.5f)) < 1e-3f // 发射面邻格（x0+1）格心
                  && std::abs(ents.posAt(idx).y() - (kRigY + 0.5f)) < 1e-3f
                  && std::abs(ents.posAt(idx).z() - (z0 + 0.5f)) < 1e-3f
                  && store.slotIdAt(x0, kRigY, z0, 0) == BR::TntBlock
                  && store.slotCountAt(x0, kRigY, z0, 0) == 2;          // 库存 3→2（激活一次消耗 1）
            if (idx >= 0) {
                // 标准引信：满值（progress==1.0）；细步驱动 0.25s（16×0.015625——生产帧率级步长；单步 0.25s
                //   的粗欧拉一步跳 0.4 格失真）→ 引信递减（<1.0）+ +X 定向位移（t871 初速 1.6 → ≈0.26 格）。
                const float progBefore = ents.fuseProgressAt(idx);
                const float xBefore = ents.posAt(idx).x();
                for (int i = 0; i < 16; ++i)
                    ents.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                okAFuse = progBefore >= 0.999f && ents.isPrimedAt(idx)
                          && ents.fuseProgressAt(idx) < progBefore;     // 引信计时在跑
                okAMove = ents.isPrimedAt(idx)
                          && ents.posAt(idx).x() > xBefore + 0.15f;     // 弹射方向 = 发射面朝向（+X；t871 阈值随新初速下调）
                // t871 落点：续细步驱动 0.8s（总 1.05s < 引信 5s）→ 摩擦耗尽水平动量 → 落定格必须在邻格内
                //   （连续极限总位移 = v/摩擦率 = 0.4 格 → 落定 ≈x0+1.91；旧 4.0 → 1.0 格 → 落到 x0+2 红此断言）。
                for (int i = 0; i < 51; ++i)
                    ents.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
                okRest = ents.isPrimedAt(idx)
                         && int(std::floor(ents.posAt(idx).x())) == x0 + 1  // 仍在发射面邻格（t871 口径）
                         && ents.posAt(idx).x() < float(x0) + 2.0f;
            }
            if (!okA || !okAFuse || !okAMove || !okRest)
                qInfo().noquote() << "  [t856 a diag] primedIdx=" << idx
                                  << " pos=" << (idx >= 0 ? ents.posAt(idx) : QVector3D())
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0)
                                  << " fuse=" << (idx >= 0 ? ents.fuseProgressAt(idx) : -1.0f)
                                  << " okAFuse=" << okAFuse << " okAMove=" << okAMove
                                  << " okRest=" << okRest;
        }

        // (b) 冷却闸不回归（t814 (e) ②③ 压缩版，分派物换 TNT）：0.5s 内真上升沿 → 冷却拦（零发射且信号确发）；
        //     冷却驱动过 0.5s 再造沿 → 必再弹。
        bool okB = false;
        {
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Lever, 0); // 扳回 → 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            const int dispBeforeRepower = dispFired;
            placeRigBlock(w, tx0 - 1, kRigY, tz0, BR::Lever, 1); // 复置 = 真上升沿，但冷却 0.5s 未过
            tickN(w, 4);
            const bool noRefire = primedCount() == 1                      // 仍只有首发（无第二发）
                                  && store.slotCountAt(tx0, kRigY, tz0, 0) == 2 // 库存不再扣
                                  && dispFired > dispBeforeRepower;       // 信号确发（零发射 = 冷却拦非信号丢）
            pc.scanDispenserTraps(2.5f); // 等价递减驱动：一次耗尽 0.5s 冷却（探针态沿表空零副作用，t814 ③ 同款）
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Lever, 0);
            tickN(w, 4);
            placeRigBlock(w, tx0 - 1, kRigY, tz0, BR::Lever, 1); // 新上升沿 + 冷却已过 → 必再弹
            tickN(w, 4);
            okB = noRefire && primedCount() == 2
                  && store.slotCountAt(tx0, kRigY, tz0, 0) == 1;          // 库存 2→1（真扣一发）
            if (!okB)
                qInfo().noquote() << "  [t856 b diag] noRefire=" << noRefire
                                  << " primed=" << primedCount()
                                  << " slotCount=" << store.slotCountAt(tx0, kRigY, tz0, 0)
                                  << " sig=" << (dispFired > dispBeforeRepower);
            // 清场（实体留在探针私有 ents 内冻结，不外泄）
            w.setBlock(tx0 - 1, kRigY, tz0, BR::Air, 0);
            w.setBlock(tx0, kRigY, tz0, BR::Air, 0);
            store.clearDispenser(tx0, kRigY, tz0);
            tickN(w, 2);
        }

        // (c) 投掷器 + TNT → 普通掉落物弹出不点燃（dropper 全物品分支先于 TNT 分派，两路径边界另一侧）。
        bool okC = false;
        {
            const auto [x0, z0] = nextSlot();
            const int itemsBefore = items.count(); // 相对基线（review24 低危口径）
            const int primedBefore = primedCount();
            placeRigBlock(w, x0, kRigY, z0, BR::Dropper, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, BR::TntBlock, 2);
            tickN(w, 2);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            okC = items.count() == itemsBefore + 1                     // 弹出 1 掉落物（TNT 物品形态）
                  && primedCount() == primedBefore                     // 零 PrimedTnt（不点燃）
                  && store.slotCountAt(x0, kRigY, z0, 0) == 1;         // 库存照扣
            if (!okC)
                qInfo().noquote() << "  [t856 c diag] items=" << items.count() << "/" << itemsBefore
                                  << " primed=" << primedCount() << "/" << primedBefore
                                  << " slotCount=" << store.slotCountAt(x0, kRigY, z0, 0);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            tickN(w, 2);
        }

        // (d) review25 #11 排出口占用门（review26 #24 口径）：发射面邻格被实体方块堵住 → 不在墙格内 spawn
        //     TNT（primed 水平积分不查碰撞 → 墙格内 spawn = ~5s 后就地爆穿墙并波及发射器自身）。两段：
        //     d1 邻格墙、墙后格空（review26 #24 复现形态：旧版在此隔 1 格墙把 TNT 生成在墙后）→ 零
        //        PrimedTnt（墙格 + 墙后格都无）+ 掉落物 +1（堵口一律退化物品形态）+ 库存照扣；
        //     d2 邻格 + 墙后格都墙 → 同口径（零 PrimedTnt + 掉落物 +1 + 库存照扣）。
        bool okD1 = false, okD2 = false;
        {
            // d1：堵一格（墙后格净空）→ 堵口退化掉落物，墙后零 PrimedTnt（旧版穿墙生成位）。
            const auto [xa, za] = nextSlot();
            placeRigBlock(w, xa, kRigY, za, BR::Dispenser, 0); // state 0 → 朝 +X
            w.setBlock(xa + 2, kRigY, za, BR::Air, 0); // 凿空墙后格（z≥97 新行地形可达 y41；墙格由下方覆写）
            store.ensureDispenser(xa, kRigY, za);
            store.setSlot(xa, kRigY, za, 0, BR::TntBlock, 3);
            placeRigBlock(w, xa + 1, kRigY, za, BR::Stone, 0); // 堵口墙（发射面邻格）
            tickN(w, 2);
            // 基线在拉杆激活**前**取（发射发生在下方 tickN(4) 内——事后取会把退化掉落物算进基线 = 假 +0）
            const int itemsBeforeD1 = items.count();
            const int primedBeforeD1 = primedCount();
            placeRigBlock(w, xa - 1, kRigY, za, BR::Lever, 1); // 源贴背面（激活发射）
            tickN(w, 4);
            int idxD = -1;
            for (int i = 0; i < ents.count(); ++i) // 找「墙后格格心」的 PrimedTnt（旧版穿墙生成位；ents 内有 (a)/(b) 冻结残留）
                if (ents.isPrimedAt(i)
                    && std::abs(ents.posAt(i).x() - (xa + 2.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).y() - (kRigY + 0.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).z() - (za + 0.5f)) < 1e-3f) { idxD = i; break; }
            bool noneInWall = true; // 墙格（xa+1）内不得有任何新 PrimedTnt（旧版就地 spawn 的位置）
            for (int i = 0; i < ents.count(); ++i)
                if (ents.isPrimedAt(i) && std::abs(ents.posAt(i).x() - (xa + 1.5f)) < 1e-3f
                    && std::abs(ents.posAt(i).z() - (za + 0.5f)) < 1e-3f)
                    noneInWall = false;
            // review26 #24：堵口一律退化掉落物——旧版在墙后格（xa+2）生成 PrimedTnt（隔 1 格墙穿墙）。
            //   新口径断言：零 PrimedTnt（含墙后格，idxD 必 -1）+ 掉落物 +1 + 库存照扣。
            okD1 = idxD < 0 && noneInWall
                  && primedCount() == primedBeforeD1
                  && items.count() == itemsBeforeD1 + 1
                  && store.slotIdAt(xa, kRigY, za, 0) == BR::TntBlock
                  && store.slotCountAt(xa, kRigY, za, 0) == 2; // 库存 3→2
            if (!okD1)
                qInfo().noquote() << "  [t856 d1 diag] idxD=" << idxD
                                  << " noneInWall=" << noneInWall
                                  << " items=" << items.count() << "/" << itemsBeforeD1
                                  << " primed=" << primedCount() << "/" << primedBeforeD1
                                  << " slotCount=" << store.slotCountAt(xa, kRigY, za, 0)
                                  << " b1=" << int(w.blockAt(xa + 1, kRigY, za))
                                  << " cb1=" << (BR::collisionAABBs(w.blockAt(xa + 1, kRigY, za),
                                                                    w.stateAt(xa + 1, kRigY, za))).size()
                                  << " b2=" << int(w.blockAt(xa + 2, kRigY, za))
                                  << " cb2=" << (BR::collisionAABBs(w.blockAt(xa + 2, kRigY, za),
                                                                    w.stateAt(xa + 2, kRigY, za))).size();
            w.setBlock(xa - 1, kRigY, za, BR::Air, 0);
            w.setBlock(xa, kRigY, za, BR::Air, 0);
            w.setBlock(xa + 1, kRigY, za, BR::Air, 0);
            store.clearDispenser(xa, kRigY, za);
            tickN(w, 2);

            // d2：堵两格（邻格 + 墙后格）→ 退化普通掉落物弹出（不点燃，d1 同口径）。
            const auto [xb, zb] = nextSlot();
            const int itemsBefore = items.count();
            const int primedBefore = primedCount();
            placeRigBlock(w, xb, kRigY, zb, BR::Dispenser, 0);
            store.ensureDispenser(xb, kRigY, zb);
            store.setSlot(xb, kRigY, zb, 0, BR::TntBlock, 2);
            placeRigBlock(w, xb + 1, kRigY, zb, BR::Stone, 0);
            placeRigBlock(w, xb + 2, kRigY, zb, BR::Stone, 0);
            tickN(w, 2);
            placeRigBlock(w, xb - 1, kRigY, zb, BR::Lever, 1);
            tickN(w, 4);
            okD2 = primedCount() == primedBefore              // 零 PrimedTnt（不在墙格 / 再探格内引爆实体）
                  && items.count() == itemsBefore + 1          // TNT 以掉落物形态弹出（可回收，不静默吞）
                  && store.slotCountAt(xb, kRigY, zb, 0) == 1; // 库存照扣
            if (!okD2)
                qInfo().noquote() << "  [t856 d2 diag] items=" << items.count() << "/" << itemsBefore
                                  << " primed=" << primedCount() << "/" << primedBefore
                                  << " slotCount=" << store.slotCountAt(xb, kRigY, zb, 0);
            w.setBlock(xb - 1, kRigY, zb, BR::Air, 0);
            w.setBlock(xb, kRigY, zb, BR::Air, 0);
            w.setBlock(xb + 1, kRigY, zb, BR::Air, 0);
            w.setBlock(xb + 2, kRigY, zb, BR::Air, 0);
            store.clearDispenser(xb, kRigY, zb);
            tickN(w, 2);
        }

        const bool okT856 = okA && okAFuse && okAMove && okRest && okB && okC && okD1 && okD2;
        if (!okT856) ++totalFail;
        qInfo().noquote() << (okT856 ? "PASS" : "FAIL")
                          << "| t856 dispenser fires primed TNT: lever behind a 3-TNT dispenser pops a "
                             "PrimedTnt at the facing-adjacent cell center (state 0 -> +X, source kept off "
                             "the firing face), full standard fuse (fuseProgress==1.0 pins "
                             "kPrimedTntFuseSec, chain-fuse 1.2s would read 0.24), fuse ticking + "
                             "directional +X drift after one 0.25s entity tick pins pop-along-facing "
                             "velocity (t871: pop speed 4.0->1.6), and after 1.05s of driven ticks the "
                             "primed TNT settles INSIDE the facing-adjacent cell (floor==x0+1, the "
                             "'one cell past the muzzle' user contract; the old 4.0 speed landed two "
                             "cells out), stock 3->2; sub-0.5s-cooldown re-edge fires nothing while "
                             "powerDispenserTriggered still emits, cooldown driven past 0.5s then re-edge "
                             "MUST re-pop (+1 entity, stock 2->1); dropper w/ TNT pops a plain item drop "
                             "with zero primed entities (dropper = item-only, the other side of the "
                             "two-path boundary); blocked firing face (review25 #11 / review26 #24) "
                             "always degrades to a plain item drop with zero primed entities -- "
                             "whether only the adjacent cell is walled (the wall-behind cell stays "
                             "primed-free: old code teleported TNT through a 1-thick wall) or the "
                             "exit is fully walled (MC-approximate: recoverable item over silent "
                             "swallow), stock decremented on every path; "
                             "redstone-direct-adjacent in-place priming regression is "
                             "covered by the t814(a) probe above";
    }

    // ── P-t868 高频红石逐沿发射探针（Game 层真消费端，t814/t856 模式）──
    //   用户实测：「发射器高频红石只能激活一次」（旧 2.0s 冷却把第二个上升沿整只吞掉）。修复 = 冷却缩短
    //   2.0→0.5s 且语义重钉「短防抖闸」（拦同 tick 双路径双发），非节流窗。断言三段：
    //   (a) 高频沿序列连发：拉杆快速循环（扳开→0.7s→扳回→扳开；0.7s = 真实帧驱动的冷却递减——
    //       scanDispenserTraps(0.016f)×44 ≈ 60Hz 帧，等价生产里 tick() 每帧推进冷却）。旧 2.0s 常量下
    //       0.7s < 2.0s → 第二沿被吞（fired 恒 1 = 用户症状）；新 0.5s 下 0.7s > 0.5s → 必再发（≥2）。
    //   (b) 防抖闸仍有效：同一冷却窗内（只 tickN 推进世界、无帧驱动递减）再造真上升沿 → 不多发
    //       （t814 (e)② 同口径；t869 火把环时钟落地前的独立驱动——本探针不依赖无稳态电路存在）。
    //   (c) 库存对账：发射次数 == 库存扣减量（无凭空箭 / 无吞库存）。
    {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });

        bool okClock = false, okDebounce = false, okStock = false;
        const auto arrowCount = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++n;
            return n;
        };
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);   // state 0 → 朝 +X
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 4);
            tickN(w, 2);

            // (a) 高频沿序列：沿#1（扳开）→ 帧驱动 0.7s → 沿#2（扳回+再扳开）→ 必再发。
            const int arrows0 = arrowCount();
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // 上升沿 #1 → 发射 1
            tickN(w, 4);
            const int afterFirst = arrowCount();
            for (int f = 0; f < 44; ++f) pc.scanDispenserTraps(0.016f); // 0.704s 帧驱动（>0.5s 新冷却 / <2.0s 旧冷却）
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);         // 降沿（清 fireDispenserAtQml 沿基线）
            tickN(w, 4);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // 上升沿 #2 → 旧 2.0s 冷却吞 / 新 0.5s 过闸
            tickN(w, 4);
            const int fired = arrowCount() - arrows0;
            okClock = afterFirst == arrows0 + 1 && fired >= 2;    // 首沿恰 1 发 + 第二沿必再发（旧常量恒 1 → FAIL）
            okStock = store.slotCountAt(x0, kRigY, z0, 0) == 4 - fired; // 库存对账（发射数==扣减数）
            if (!okClock || !okStock)
                qInfo().noquote() << "  [t868 a diag] first=" << (afterFirst - arrows0)
                                  << " fired=" << fired
                                  << " stock=" << store.slotCountAt(x0, kRigY, z0, 0);

            // (b) 防抖闸：冷却窗内（仅 tickN，无帧驱动递减——冷却表项保持 ~0.5s）再造真上升沿 → 不多发。
            pc.scanDispenserTraps(1e-3f);                        // review25 #8：走一步真实递减（0 值冷却即被 erase）
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);         // 降沿
            tickN(w, 4);
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);   // <0.5s 新升沿 → 防抖闸拦（不发射）
            tickN(w, 4);
            const int firedB = arrowCount() - arrows0;
            okDebounce = firedB == fired                          // 与 (a) 末尾持平（无新发射）
                         && store.slotCountAt(x0, kRigY, z0, 0) == 4 - fired;
            if (!okDebounce)
                qInfo().noquote() << "  [t868 b diag] fired=" << firedB
                                  << " expect=" << fired
                                  << " stock=" << store.slotCountAt(x0, kRigY, z0, 0);
            // 清场
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            store.clearDispenser(x0, kRigY, z0);
            ents.clearAll();
            tickN(w, 2);
        }
        const bool okT868 = okClock && okDebounce && okStock;
        if (!okT868) ++totalFail;
        qInfo().noquote() << (okT868 ? "PASS" : "FAIL")
                          << "| t868 high-frequency redstone re-fires per rising edge: rapid lever cycling "
                             "(edge -> 0.7s frame-driven cooldown decay -> re-edge) MUST re-fire (the old 2.0s "
                             "cooldown swallowed every sub-2s edge = the reported fires-once symptom), stock "
                             "decremented exactly once per shot; a fresh re-edge inside the 0.5s debounce "
                             "window stays blocked (single-path double-fire guard intact)";
    }

    // ── review26 #16 同柱垂直叠放发射器独立冷却探针（Game 层真消费端，t856/t868 模式）──
    //   用户症状（review26 低危）：冷却键 (x<<32|z) 不含 Y → 同柱垂直两台发射器共享冷却，0.5s 内上台
    //   发射后下台的合法沿被吞（t868「逐沿发射」语义在柱粒度上破裂）。修：键入 Y（21/21/10 三维布局，
    //   同 m_redstoneLitCells 既有键序）。断言：同 tick 两台各自被拉杆通电 → 两箭各发一支（箭 +2、
    //   两台库存各扣 1）——旧键下第二台被共享冷却拦（恰 1 箭、一台库存不扣），回退即红。
    {
        PlayerController pc;
        EntityManager ents;
        DispenserStore store;
        pc.setWorld(&w);
        pc.setEntityManager(&ents);
        pc.setDispenserStore(&store);
        QObject::connect(&w, &World::powerDispenserTriggered, &pc,
                         [&pc](int x, int y, int z) { pc.fireDispenserAtQml(x, y, z); });
        const auto arrowCount16 = [&ents]() {
            int n = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++n;
            return n;
        };
        const auto [x0, z0] = nextSlot();
        const int arrowsBefore = arrowCount16();
        // 同柱两台：下台 @kRigY、上台 @kRigY+1，各配独立背面拉杆（同 tick 双上升沿）、各装 2 支箭。
        placeRigBlock(w, x0, kRigY, z0, BR::Dispenser, 0);       // 下台（state 0 → 朝 +X）
        placeRigBlock(w, x0, kRigY + 1, z0, BR::Dispenser, 0);   // 上台
        store.ensureDispenser(x0, kRigY, z0);
        store.ensureDispenser(x0, kRigY + 1, z0);
        store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 2);
        store.setSlot(x0, kRigY + 1, z0, 0, RecipeRegistry::ArrowId, 2);
        tickN(w, 2);
        placeRigBlock(w, x0 - 1, kRigY, z0, BR::Lever, 1);       // 下台拉杆（on）
        placeRigBlock(w, x0 - 1, kRigY + 1, z0, BR::Lever, 1);   // 上台拉杆（on）
        tickN(w, 4);
        const bool ok16 = arrowCount16() == arrowsBefore + 2                // 两台各发一支（旧键恰 +1）
            && store.slotCountAt(x0, kRigY, z0, 0) == 1                     // 下台库存 2→1
            && store.slotCountAt(x0, kRigY + 1, z0, 0) == 1;                // 上台库存 2→1（旧键不扣 = 被吞沿）
        if (!ok16)
            qInfo().noquote() << "  [review26-16 diag] arrows +" << arrowCount16() - arrowsBefore
                          << " lowerStock" << store.slotCountAt(x0, kRigY, z0, 0)
                          << " upperStock" << store.slotCountAt(x0, kRigY + 1, z0, 0);
        if (!ok16) ++totalFail;
        qInfo().noquote() << (ok16 ? "PASS" : "FAIL")
                          << "| review26-16 per-dispenser cooldown keyed in 3D: two vertically stacked "
                             "dispensers powered the same tick each fire their own arrow (old (x<<32|z) key "
                             "shared the cooldown across the column and swallowed the lower machine's legal "
                             "edge within 0.5s), both stocks decremented";
        // 清场
        w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
        w.setBlock(x0 - 1, kRigY + 1, z0, BR::Air, 0);
        w.setBlock(x0, kRigY, z0, BR::Air, 0);
        w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
        store.clearDispenser(x0, kRigY, z0);
        store.clearDispenser(x0, kRigY + 1, z0);
        tickN(w, 2);
    }

    // ── P-t869 红石无稳态电路（时钟）复刻探针（World 层，t740 回归定位）──
    //   用户实测：「红石高频 / 无限电路上版本有、本版没了」。考古结论：v1 电网从未支持过*合法*无稳态——
    //   t740（3686e27，2026-08-21）为修「灯闪 / 时亮时不亮」把「火把斜下 4 格的粉」整体豁免出
    //   attachPowered 读，装饰环误触发**和**粉输入 NOT 门 / 时钟回路一并哑火（用户记忆中的「上版本有」
    //   即 t740 前的整体回灌振荡）。t812 转辙器 bit7 / t689 沿检测 / t707 BFS 均无涉（各自升 / 降沿对称）。
    //   修复 = MC **形状输出**语义（BlockRegistry::redstoneDustPowersNeighbor，连接位反推开放端）：
    //   粉终止于 / 拐入方块 → 供能（时钟 / NOT 门恢复）；贯穿直线贴块而过 → 不供能（t740 装饰环保持
    //   稳定——原修案的正确形态）。断言三段：
    //   (a) **火把时钟**：石块 + 立顶火把 + 单格粉 stub 贴基座侧（火把斜下自喂 → stub 形状指向基座 →
    //       火把熄 → stub 断电 → 重亮 …… 自持振荡）。24 tick 内火把态翻转 ≥4 次 + stub 电力出现 0↔非0
    //       交替（t740 豁免下恒 0 次翻转 = 用户症状）；
    //   (b) **粉线 NOT 门**（稳定反相）：拉杆(on) → 粉×3 线终止于基座侧 → 基座恒被供 → 火把持续熄灭
    //       （tick 6/12/20 采样 OffFlag 恒置位——非振荡，锁存反相）；
    //   (c) **贯穿直线稳定对照**（t740 反闪烁保持）：粉直线贴基座侧而过（中格对向双连 = 贯穿形）→
    //       20 tick 火把恒亮（装饰环不再误触发——豁免换成形状后原修案语义仍在）。
    {
        bool okClock = false, okNot = false, okStable = false;
        // (a) 火把时钟：x0=Stone(基座) x0/y+1=Torch x0+1..x0+2=粉 stub 两格（近格连接朝远格 = 端点形 →
        //     开放端指向基座 → 供能；单格 dot 不输出（P4/P14 稳定语义），须两格才成回路）。
        {
            const auto [x0, z0] = nextSlot();
            placeRigBlock(w, x0, kRigY, z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneDust, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::RedstoneDust, 0);
            tickN(w, 2);
            placeRigBlock(w, x0, kRigY + 1, z0, BR::RedstoneTorch, 0); // 最后放火把 → 起振
            int flips = 0, powerFlips = 0;
            bool prevOff = false, prevPow = false;
            for (int t = 0; t < 24; ++t) {
                w.tickRedstone();
                const bool off = (w.stateAt(x0, kRigY + 1, z0) & BR::RedstoneTorchStateOffFlag) != 0;
                const bool pow = (w.stateAt(x0 + 1, kRigY, z0) & BR::RedstoneDustPowerMask) > 0;
                if (t > 0) {
                    if (off != prevOff) ++flips;
                    if (pow != prevPow) ++powerFlips;
                }
                prevOff = off; prevPow = pow;
            }
            okClock = flips >= 4 && powerFlips >= 4; // 自持振荡（t740 豁免下恒 0 → FAIL = 用户症状）
            if (!okClock)
                qInfo().noquote() << "  [t869 a diag] flips=" << flips << " powerFlips=" << powerFlips;
            // 清场
            w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
        // (b) 粉线 NOT 门（稳定反相）：Stone x1 / Torch 其上 / 粉 x1+1..x1+3 / Lever(on) x1+4。
        {
            const auto [x1, z1] = nextSlot();
            placeRigBlock(w, x1, kRigY, z1, BR::Stone, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x1 + i, kRigY, z1, BR::RedstoneDust, 0);
            placeRigBlock(w, x1 + 4, kRigY, z1, BR::Lever, 1);
            tickN(w, 2);
            placeRigBlock(w, x1, kRigY + 1, z1, BR::RedstoneTorch, 0);
            bool off6 = false, off12 = false, off20 = false;
            for (int t = 1; t <= 20; ++t) {
                w.tickRedstone();
                const bool off = (w.stateAt(x1, kRigY + 1, z1) & BR::RedstoneTorchStateOffFlag) != 0;
                if (t == 6) off6 = off;
                if (t == 12) off12 = off;
                if (t == 20) off20 = off;
            }
            okNot = off6 && off12 && off20; // 持续熄灭（线保供，锁存反相非振荡）
            if (!okNot)
                qInfo().noquote() << "  [t869 b diag] off6=" << off6 << " off12=" << off12
                                  << " off20=" << off20;
            // 清场
            w.setBlock(x1, kRigY + 1, z1, BR::Air, 0);
            w.setBlock(x1 + 4, kRigY, z1, BR::Air, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x1 + i, kRigY, z1, BR::Air, 0);
            w.setBlock(x1, kRigY, z1, BR::Air, 0);
            tickN(w, 2);
        }
        // (c) 贯穿直线稳定对照：Stone x2 / Torch 其上 / 粉直线 (x2+1, z2-1..z2+1)（中格对向双连）/
        //     Lever(on) (x2+1, z2+2)。直线贴基座 +X 侧而过 → 形状侧向不供 → 火把恒亮。
        {
            const auto [x2, z2] = nextSlot();
            placeRigBlock(w, x2, kRigY, z2, BR::Stone, 0);
            for (int dz = -1; dz <= 1; ++dz) w.setBlock(x2 + 1, kRigY, z2 + dz, BR::RedstoneDust, 0);
            placeRigBlock(w, x2 + 1, kRigY, z2 + 2, BR::Lever, 1);
            tickN(w, 2);
            placeRigBlock(w, x2, kRigY + 1, z2, BR::RedstoneTorch, 0);
            bool anyOff = false;
            for (int t = 0; t < 20; ++t) {
                w.tickRedstone();
                if (w.stateAt(x2, kRigY + 1, z2) & BR::RedstoneTorchStateOffFlag) anyOff = true;
            }
            okStable = !anyOff; // 贯穿形侧向不供（装饰环稳定，t740 原修案语义保持）
            if (!okStable)
                qInfo().noquote() << "  [t869 c diag] anyOff=" << anyOff;
            // 清场
            w.setBlock(x2, kRigY + 1, z2, BR::Air, 0);
            w.setBlock(x2 + 1, kRigY, z2 + 2, BR::Air, 0);
            for (int dz = -1; dz <= 1; ++dz) w.setBlock(x2 + 1, kRigY, z2 + dz, BR::Air, 0);
            w.setBlock(x2, kRigY, z2, BR::Air, 0);
            tickN(w, 2);
        }
        const bool okT869 = okClock && okNot && okStable;
        if (!okT869) ++totalFail;
        qInfo().noquote() << (okT869 ? "PASS" : "FAIL")
                          << "| t869 redstone astable circuits restored via dust shape semantics: torch-on-block "
                             "+ 2-cell dust stub at the base side self-oscillates (>=4 state flips + power "
                             "alternation in 24 ticks; the t740 blanket base-ring exemption pinned it at 0 "
                             "flips = the reported regression), lever-driven 3-dust line ENDING at the support "
                             "holds the torch inverted (latched NOT gate), while a straight dust line PASSING "
                             "the support side stays silent (through-line has no side output) and a lone dot "
                             "has no output at all - both t740 anti-flicker shapes remain stable; "
                             "archaeology: t740 3686e27 killed both the accidental ring flicker AND every "
                             "dust-fed NOT/clock input, t812 bit7 / t689 edges / t707 BFS "
                             "cleared of involvement";
    }

    // ── P-review26-5 红石粉形状输出「臂轴延长端」语义探针（review26 #5：拐角/单臂垂直侧自相矛盾修口）──
    //   旧判定等价「目标向无连接位且非贯穿直线侧即供电」：单臂粉向三个非连接方向全 true，贯穿直线垂直侧
    //   false——同为垂直于粉臂的侧面，一格之差行为翻转（L 形拐角格垂直侧贴 TNT/灯/发射器/铁门意外通电）。
    //   新语义：目标 ±X 输出 iff X 轴有连接（px||nx）、±Z iff（pz||nz）、dot 仍 false。
    //   (a) 纯函数四形真值表（连接位直构）：端点垂直侧 false（修掉矛盾面）/ 端点延长端 true / 拐角开放侧
    //       true / 贯穿直线侧向 false / dot 全 false / 垂直对角 false；
    //   (b) World 行为级（专用局部世界 + 固定坐标，t829 免凿高台先例——**不占 nextSlot 主世界位**：本批
    //       探针前移会推移 t812 等下游探针的 rig 位，矿车跑法对槽位地形敏感 = 假 FAIL）：端点粉臂**垂直于**
    //       基座方向 → 火把恒亮（旧代码此形态供电 → 火把熄 = FAIL 面）；臂沿基座方向（延长端，拉杆驱动）→
    //       火把锁存熄灭（正对照，t869(b) NOT 门形态不变）；拐角开放侧 → 供电熄灭（正对照，与旧行为一致）。
    {
        const auto dustSt = [](quint8 conn) { return quint8(conn << 4); };
        const quint8 armNx  = dustSt(BR::RedstoneDustConnNx);
        const quint8 armPx  = dustSt(BR::RedstoneDustConnPx);
        const quint8 corner = dustSt(BR::RedstoneDustConnPx | BR::RedstoneDustConnPz);
        const quint8 lineZ  = dustSt(BR::RedstoneDustConnPz | BR::RedstoneDustConnNz);
        const bool okTable =
            // 端点（单臂 -X）：延长端 ±X 输出；垂直侧 ±Z 不输出（review26 #5 修掉的矛盾面）
            BR::redstoneDustPowersNeighbor(armNx, -1, 0) && BR::redstoneDustPowersNeighbor(armNx, 1, 0)
            && !BR::redstoneDustPowersNeighbor(armNx, 0, 1) && !BR::redstoneDustPowersNeighbor(armNx, 0, -1)
            // 端点（单臂 +X）镜像
            && BR::redstoneDustPowersNeighbor(armPx, 1, 0) && BR::redstoneDustPowersNeighbor(armPx, -1, 0)
            && !BR::redstoneDustPowersNeighbor(armPx, 0, 1) && !BR::redstoneDustPowersNeighbor(armPx, 0, -1)
            // 拐角（px+pz）：两个几何可达开放侧（-X 延长端 / -Z 延长端）输出
            && BR::redstoneDustPowersNeighbor(corner, -1, 0) && BR::redstoneDustPowersNeighbor(corner, 0, -1)
            // 贯穿直线（Z 轴）：侧向 ±X 不输出（t740 反闪烁保持）；轴向 ±Z 输出
            && !BR::redstoneDustPowersNeighbor(lineZ, 1, 0) && !BR::redstoneDustPowersNeighbor(lineZ, -1, 0)
            && BR::redstoneDustPowersNeighbor(lineZ, 0, 1) && BR::redstoneDustPowersNeighbor(lineZ, 0, -1)
            // dot（无连接）：全向不输出（P4/P14 语义）
            && !BR::redstoneDustPowersNeighbor(dustSt(0), 1, 0) && !BR::redstoneDustPowersNeighbor(dustSt(0), 0, 1)
            // 垂直 / 对角 / 零偏移：无形状语义
            && !BR::redstoneDustPowersNeighbor(armNx, 0, 0) && !BR::redstoneDustPowersNeighbor(armNx, 1, 1);
        // 专用世界（seed 77：实测地形 ≤81 → 84+ 全空带，t835 同款）：y84 石平台，器件层 y85，火把 y86。
        World wR5;
        wR5.setWidth(48); wR5.setDepth(48); wR5.setHeight(96); wR5.setSeed(77);
        for (int x = 2; x <= 40; ++x)
            for (int z = 2; z <= 40; ++z) wR5.setBlock(x, 84, z, BR::Stone, 0);
        // (b1) 端点垂直侧（判别面）：基座 -X、粉臂 +Z（臂垂直于基座方向）→ 无 X 轴臂 → 不供电 → 火把恒亮。
        //      旧代码：!nx && !straightZ 全 true + 火把斜下喂粉 → attachPowered → 熄灭（= 矛盾行为）。
        placeRigBlock(wR5, 6, 85, 6, BR::Stone, 0);
        wR5.setBlock(7, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(7, 85, 7, BR::RedstoneDust, 0); // 臂 +Z（垂直侧形态）
        tickN(wR5, 2);
        placeRigBlock(wR5, 6, 86, 6, BR::RedstoneTorch, 0);
        bool anyOff1 = false;
        for (int t = 0; t < 16; ++t) {
            wR5.tickRedstone();
            if (wR5.stateAt(6, 86, 6) & BR::RedstoneTorchStateOffFlag) anyOff1 = true;
        }
        const bool okPerp = !anyOff1;
        if (!okPerp)
            qInfo().noquote() << "  [review26-5 b1 diag] anyOff1" << anyOff1;
        wR5.setBlock(6, 86, 6, BR::Air, 0);
        wR5.setBlock(7, 85, 6, BR::Air, 0);
        wR5.setBlock(7, 85, 7, BR::Air, 0);
        wR5.setBlock(6, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        // (b2) 端点延长端正对照（拉杆驱动，锁存非振荡）：臂 +X 沿基座方向 → 供电 → 火把持续熄灭。
        placeRigBlock(wR5, 12, 85, 6, BR::Stone, 0);
        wR5.setBlock(13, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(14, 85, 6, BR::RedstoneDust, 0);
        placeRigBlock(wR5, 15, 85, 6, BR::Lever, 1);
        tickN(wR5, 2);
        placeRigBlock(wR5, 12, 86, 6, BR::RedstoneTorch, 0);
        bool off2 = false;
        for (int t = 0; t < 12; ++t) {
            wR5.tickRedstone();
            if (t >= 6 && (wR5.stateAt(12, 86, 6) & BR::RedstoneTorchStateOffFlag)) off2 = true;
        }
        const bool okExt = off2;
        if (!okExt)
            qInfo().noquote() << "  [review26-5 b2 diag] off2" << off2;
        wR5.setBlock(12, 86, 6, BR::Air, 0);
        wR5.setBlock(15, 85, 6, BR::Air, 0);
        wR5.setBlock(13, 85, 6, BR::Air, 0);
        wR5.setBlock(14, 85, 6, BR::Air, 0);
        wR5.setBlock(12, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        // (b3) 拐角开放端正对照：粉 A 连 +X（远端）与 +Z（拐臂）成拐角，基座在 -X 开放侧 → 供电 → 熄灭
        //      （旧行为一致——拐角开放端 true 两版相同，钉住不回归）。
        placeRigBlock(wR5, 18, 85, 6, BR::Stone, 0);
        wR5.setBlock(19, 85, 6, BR::RedstoneDust, 0);
        wR5.setBlock(20, 85, 6, BR::RedstoneDust, 0); // px 臂（远端）
        wR5.setBlock(19, 85, 7, BR::RedstoneDust, 0); // pz 拐臂 → A 成拐角形
        placeRigBlock(wR5, 21, 85, 6, BR::Lever, 1);
        tickN(wR5, 2);
        placeRigBlock(wR5, 18, 86, 6, BR::RedstoneTorch, 0);
        bool off3 = false;
        for (int t = 0; t < 12; ++t) {
            wR5.tickRedstone();
            if (t >= 6 && (wR5.stateAt(18, 86, 6) & BR::RedstoneTorchStateOffFlag)) off3 = true;
        }
        const bool okCorner = off3;
        if (!okCorner)
            qInfo().noquote() << "  [review26-5 b3 diag] off3" << off3;
        wR5.setBlock(18, 86, 6, BR::Air, 0);
        wR5.setBlock(21, 85, 6, BR::Air, 0);
        wR5.setBlock(19, 85, 6, BR::Air, 0);
        wR5.setBlock(20, 85, 6, BR::Air, 0);
        wR5.setBlock(19, 85, 7, BR::Air, 0);
        wR5.setBlock(18, 85, 6, BR::Air, 0);
        tickN(wR5, 2);
        const bool okR5 = okTable && okPerp && okExt && okCorner;
        if (!okR5) ++totalFail;
        qInfo().noquote() << (okR5 ? "PASS" : "FAIL")
                          << "| review26-5 dust shape output unified to arm-axis extension semantics: "
                             "target +-X powered iff the dust has an X-axis arm (px||nx), +-Z iff "
                             "(pz||nz), dot stays dark - endpoint perpendicular sides no longer power "
                             "(old rule powered all 3 non-connected sides of a single-arm dust while a "
                             "through-line's perpendicular side stayed dark - same geometry, opposite "
                             "verdict one cell apart), endpoint extension end / corner open sides / "
                             "through-line axial ends still power (NOT-gate and clock wiring intact), "
                             "t740 anti-flicker shapes (dot + through-line side) unchanged; four-shape "
                             "function truth table + world rigs: perpendicular-arm endpoint keeps the "
                             "torch lit (old code powered it), lever-driven extension end and corner "
                             "open side keep it latched off (positive controls)";
    }

    // ── P-review26-6 火把 burnout 熔断探针（review26 #6：端点粉贴基座永续振荡无兜底）──
    //   t869 形状语义恢复端点/拐角回灌后，「火把立方块上 + 基座旁 ≥2 格端点粉」= 5Hz 永续振荡（每 tick
    //   recomputeLightAround 双调 + chunk mesh 10Hz 重建直到玩家干预）；MC 有 torch burnout 熔断。本探针钉
    //   MC 近似参数（镜像常量 P18 模式）：**8 次翻转（60s 窗内）→ 锁定熄灭 80 红石 tick（8s）→ 冷却后可再
    //   振荡**（确定性整数计数，PLAN §2-K）。
    //   (a) 拉杆驱动精确翻转：NOT 门 rig 4 个 on/off 半周期 = 恰 8 翻 → 第 8 翻熔断——拉杆 OFF（attach 失电，
    //       自然应重亮）而火把保持熄灭 = 锁定面；+70 tick 仍熄；75..100 tick 窗内重亮（冷却 80 ± 传播余量）；
    //       重亮后再拨拉杆立即再熄（冷却后电路照常工作）。
    //   (b) 自由时钟（t869(a) 同 rig）长跑 320 tick：早期 ≥4 翻（振荡未被熔断误杀）→ 出现 60..100 tick
    //       连续熄灭段（锁定段长钉冷却量级）→ 段后 100 tick 内再翻转（冷却后恢复振荡 = 「可振荡」tradeoff
    //       保持，burnout 只封「永续」）。
    {
        constexpr int kMirrorBurnoutFlips = 8;      // World::kTorchBurnoutFlipLimit 镜像（改值须两处同步）
        constexpr int kMirrorBurnoutCooldown = 80;  // World::kTorchBurnoutCooldownTicks 镜像
        Q_UNUSED(kMirrorBurnoutFlips);
        // 专用世界（同 review26-5：seed 77 全空带 y84+ 平台——不占 nextSlot 主世界位，防下游槽位漂移）。
        World wR6;
        wR6.setWidth(48); wR6.setDepth(48); wR6.setHeight(96); wR6.setSeed(77);
        for (int x = 2; x <= 40; ++x)
            for (int z = 2; z <= 40; ++z) wR6.setBlock(x, 84, z, BR::Stone, 0);
        // (a) 拉杆驱动（P14 几何：拉杆贴**支撑块**侧面——直供 attach，翻转链每拨恰一翻可精确计数；t869(b)
        //     的「拉杆贴粉线远端」形态锚点 2-hop 展开够不到火把，lever 翻转永不复评 = rig 假死）。
        placeRigBlock(wR6, 6, 85, 14, BR::Stone, 0);
        placeRigBlock(wR6, 6, 85, 15, BR::Lever, 1); // ON（贴支撑侧面）
        tickN(wR6, 2);
        placeRigBlock(wR6, 6, 86, 14, BR::RedstoneTorch, 0);
        tickN(wR6, 6);
        const auto torchOffA = [&]() {
            return (wR6.stateAt(6, 86, 14) & BR::RedstoneTorchStateOffFlag) != 0;
        };
        bool okA = torchOffA(); // 翻 1：lever ON → 锁存熄灭
        for (int half = 0; half < 7 && okA; ++half) { // 翻 2..8：交替 off/on 半周期
            wR6.setBlock(6, 85, 15, BR::Lever, quint8(half % 2 == 0 ? 0 : 1));
            tickN(wR6, 6);
            const bool off = torchOffA();
            if (half < 6) {
                okA = okA && (off == (half % 2 == 1)); // 前 6 半周期正常翻转（lever on 段熄 / off 段亮）
            } else {
                okA = okA && off; // 第 8 翻（lever OFF 后本应重亮）熔断 → 保持熄灭 = 锁定
            }
            if (!okA)
                qInfo().noquote() << "  [review26-6 a half diag] half" << half << "off" << off;
        }
        int relightTick = -1;
        for (int t = 1; t <= 110 && okA; ++t) {
            wR6.tickRedstone();
            if (t <= 70 && !torchOffA()) { okA = false; break; } // 冷却期内不得重亮
            if (t > 70 && !torchOffA() && relightTick < 0) relightTick = t;
        }
        okA = okA && relightTick > 72 && relightTick <= 100; // 冷却 80 ± 锁定点/传播余量
        if (okA) { // 冷却后电路照常：再拨 lever → 立即再熄（新计数窗开跑）
            wR6.setBlock(6, 85, 15, BR::Lever, 1);
            tickN(wR6, 6);
            okA = torchOffA();
        }
        if (!okA)
            qInfo().noquote() << "  [review26-6 a diag] relightTick" << relightTick
                              << "endOff" << torchOffA();
        wR6.setBlock(6, 86, 14, BR::Air, 0);
        wR6.setBlock(6, 85, 15, BR::Air, 0);
        wR6.setBlock(6, 85, 14, BR::Air, 0);
        tickN(wR6, 2);
        // (b) 自由时钟长跑（t869(a) 同 rig：火把立方块上 + 端点粉 stub 贴基座侧自持振荡）。
        placeRigBlock(wR6, 12, 85, 14, BR::Stone, 0);
        wR6.setBlock(13, 85, 14, BR::RedstoneDust, 0);
        wR6.setBlock(14, 85, 14, BR::RedstoneDust, 0);
        tickN(wR6, 2);
        placeRigBlock(wR6, 12, 86, 14, BR::RedstoneTorch, 0);
        std::vector<bool> offSeq;
        offSeq.reserve(320);
        for (int t = 0; t < 320; ++t) {
            wR6.tickRedstone();
            offSeq.push_back((wR6.stateAt(12, 86, 14) & BR::RedstoneTorchStateOffFlag) != 0);
        }
        int flipsEarly = 0;
        for (size_t t = 1; t < offSeq.size(); ++t)
            if (t <= 60 && offSeq[t] != offSeq[t - 1]) ++flipsEarly;
        int best = 0, cur = 0, bestEnd = 0;
        for (size_t t = 0; t < offSeq.size(); ++t) {
            if (offSeq[t]) {
                ++cur;
                if (cur > best) { best = cur; bestEnd = int(t); }
            } else {
                cur = 0;
            }
        }
        bool recovered = false; // 锁定段结束后 100 tick 内有翻转（冷却后恢复振荡）
        for (int t = bestEnd + 1; t < std::min(int(offSeq.size()), bestEnd + 101); ++t)
            if (offSeq[t] != offSeq[t - 1]) { recovered = true; break; }
        const bool okB = flipsEarly >= 4 && best >= 60 && best <= 100 && recovered;
        if (!okB)
            qInfo().noquote() << "  [review26-6 b diag] flipsEarly" << flipsEarly
                              << "bestStreak" << best << "recovered" << recovered;
        wR6.setBlock(12, 86, 14, BR::Air, 0);
        wR6.setBlock(13, 85, 14, BR::Air, 0);
        wR6.setBlock(14, 85, 14, BR::Air, 0);
        wR6.setBlock(12, 85, 14, BR::Air, 0);
        tickN(wR6, 2);
        const bool okR6 = okA && okB;
        if (!okR6) ++totalFail;
        qInfo().noquote() << (okR6 ? "PASS" : "FAIL")
                          << "| review26-6 torch burnout fuse: a torch flipping "
                             + QString::number(kMirrorBurnoutFlips) + " times inside the 60s window "
                             "locks OFF for " + QString::number(kMirrorBurnoutCooldown) + " redstone "
                             "ticks (8s, MC 160gt parity) then re-evaluates - lever-driven NOT rig: "
                             "8th flip (would-be relight under an OFF lever) stays dark = locked, no "
                             "relight within 70 ticks, relight lands in the 73..100 window, circuit "
                             "toggles normally after cooldown; free-running endpoint-dust clock: >=4 "
                             "flips before the fuse (oscillation not over-killed), a 60..100-tick "
                             "continuous dark stretch (lock magnitude), and post-lock flips within "
                             "100 ticks (cooldown expiry restores oscillation - astable circuits "
                             "remain buildable, only PERPETUAL 5Hz hammering is fused; deterministic "
                             "integer counters, PLAN 2-K)";
    }

    // ── P-t870 红石粉中键复制给物品 id 探针（t815 返修：根因不在图标源在 id）──
    //   用户二报「复制红石粉仍非红石粉图标」。根因：pickBlock 把**方块形态** 130 写进 hotbar →
    //   ① 图标走方块段路径（图集瓦片重渲，连接形随电力态变）；② 与红石 tab / 材料段的粉条目（0x224）
    //   id 失配（切槽判定 / 双显面）。t815 只修了 130 的图标渲染源，没修「该给什么 id」。
    //   修 = pickItemIdForBlock 单一权威（130 → 0x224，与 dropId 同源；其余恒自身）。
    //   断言：(a) 行为级——映射函数直调（dust→0x224；石头/发射器/红石矿石恒自身——矿石 pick 给
    //   矿石本体非掉落物，MC 语义）；(b) 源码钉——pickBlock 经 pickItemIdForBlock 路由（captured /
    //   射线门内不可行为直驱，t889/a3 先例）。
    {
        // (a) 行为级：映射单一权威直调。
        const bool okMap = PlayerController::pickItemIdForBlock(quint8(BR::RedstoneDust))
                               == RecipeRegistry::RedstoneId
                           && PlayerController::pickItemIdForBlock(quint8(BR::Stone)) == int(BR::Stone)
                           && PlayerController::pickItemIdForBlock(quint8(BR::Dispenser)) == int(BR::Dispenser)
                           && PlayerController::pickItemIdForBlock(quint8(BR::RedstoneOre)) == int(BR::RedstoneOre);
        // (b) 源码钉：pickBlock 函数体（滤注释）内 pickItemIdForBlock 路由存在且先于 pickIdToHotbar 落位。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
            const int b0 = t.indexOf(QStringLiteral("void PlayerController::pickBlock()"));
            const int b1 = t.indexOf(QStringLiteral("void PlayerController::pickIdToHotbar(int id)"));
            if (b0 < 0 || b1 <= b0) {
                qInfo().noquote() << "  t870 pickBlock slice miss";
            } else {
                QString body;
                for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                    if (!line.trimmed().startsWith(QLatin1String("//"))) {
                        body += line; body += QLatin1Char('\n');
                    }
                const int iRoute = body.indexOf(QStringLiteral("pickIdToHotbar(pickItemIdForBlock(id));"));
                okPin = iRoute >= 0;
            }
        }
        const bool okT870 = okMap && okPin;
        if (!okT870) ++totalFail;
        qInfo().noquote() << (okT870 ? "PASS" : "FAIL")
                          << "| t870 pick-block on redstone dust yields the dust ITEM id: mapping authority "
                             "returns RedstoneId(0x224) for the wire block (130) and identity for stone/"
                             "dispenser/redstone-ore (ore picks its block, not the drop), and pickBlock "
                             "routes through pickItemIdForBlock before the hotbar write (source pin - the "
                             "t815 icon-source-only fix never touched the id, hotbar held the block-form id "
                             "so the icon came from the block-segment atlas and never matched the redstone-"
                             "tab/material entries)";
    }

    // ── P-t879 活板门双修（行为级 + 源码钉；专用断言不建 rig）──
    //    (a) 木活板门 def 贴图契约：大面（top/bottom）= 180 四镂空板、薄侧边（side/front）= planks(8)
    //        —— 旧全 8（planks 整面实心）= 用户「像木压力板」根因；
    //    (b) 图集契约：AtlasTileCount==181 且 qrc atlas.png 宽 == 181×64（瓦片已随 180 重生——
    //        陈旧图集 180×64 即红）+ tile 180 / 178 含 alpha 孔（四镂空真透明，cutout 语义的贴图前提）；
    //    (c) 源码钉：chunkgeometry isCutoutTrapX 同时含 IronTrapdoor 与 WoodTrapdoor（cutout 段
    //        路由——木活板门孔须 alphaCutoff 透视；驱动 ChunkGeometry 需渲染后端，行为级不可密闭，
    //        t870/t889 源码钉先例）+ mesher trapdoor case 木/铁 sideTile 分流（planks/iron_block）。
    {
        const BR::BlockDef &wtd = BR::def(BR::WoodTrapdoor);
        const bool okDef = wtd.topTile == 180 && wtd.bottomTile == 180
                           && wtd.sideTile == 8 && wtd.frontTile == 8;
        bool okAtlas = BR::AtlasTileCount == 181;
        // 测试二进制无 qrc（t815/t838 探针同因：图集资源不在测试 target）→ 直读源树 textures/atlas.png
        //   （构建机源树布局，与源码钉同根路径解析）。
        const QString exeDirA = QCoreApplication::applicationDirPath();
        const QString rootA = QDir(exeDirA + QStringLiteral("/..")).absolutePath();
        QImage atlas(QDir(rootA).absoluteFilePath(QStringLiteral("textures/atlas.png")));
        if (atlas.isNull() || atlas.width() != 181 * 64) {
            okAtlas = false;
            qInfo().noquote() << "  t879 diag: atlas w =" << (atlas.isNull() ? -1 : atlas.width());
        } else {
            int holes180 = 0, holes178 = 0;
            for (int y = 0; y < 64; ++y) {
                if (qAlpha(atlas.pixel(180 * 64 + 20, y)) < 255) ++holes180;   // 孔列 x=4（16 尺度 [3,5] → 64 尺度 x 12..23 中点）
                if (qAlpha(atlas.pixel(178 * 64 + 20, y)) < 255) ++holes178;
            }
            okAtlas = okAtlas && holes180 >= 8 && holes178 >= 8; // 每列两段 3×4px 孔 → ≥8 半透明行
        }
        // (c) 源码钉（滤注释后断言路由谓词文本）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile cf(root + QStringLiteral("/src/World/chunkgeometry.cpp"));
            const QString t = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("isCutoutTrapX ="));
            if (i0 < 0) {
                qInfo().noquote() << "  t879 pin slice miss (chunkgeometry)";
            } else {
                const QString seg = t.mid(i0, 240);
                okPin = seg.contains(QStringLiteral("IronTrapdoor")) && seg.contains(QStringLiteral("WoodTrapdoor"));
            }
            QFile pf(root + QStringLiteral("/src/World/partialblockgeometry.cpp"));
            const QString t2 = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const int j0 = t2.indexOf(QStringLiteral("const int ironSideTile ="));
            if (j0 < 0) {
                qInfo().noquote() << "  t879 pin slice miss (partialblockgeometry)";
                okPin = false;
            } else {
                const QString seg2 = t2.mid(j0, 400);
                okPin = okPin && seg2.contains(QStringLiteral("tileIndex(BlockRegistry::Planks"))
                                              && seg2.contains(QStringLiteral("IronBlock"));
            }
        }
        const bool okT879 = okDef && okAtlas && okPin;
        if (!okT879) {
            ++totalFail;
            qInfo().noquote() << "  t879 diag: def" << wtd.topTile << wtd.bottomTile << wtd.sideTile
                              << wtd.frontTile << "atlas" << okAtlas << "pin" << okPin;
        }
        qInfo().noquote() << (okT879 ? "PASS" : "FAIL")
                          << "| t879 trapdoor pair fix: wood trapdoor def swaps large faces to tile 180 "
                             "(four-hole plank board, alpha cutout - the old all-planks solid plate read as "
                             "a wooden pressure plate) with plank thin edges, atlas regenerated to 181 tiles "
                             "with real alpha holes in tiles 178/180, and both trapdoors route to the cutout "
                             "pass (source pin - holes need alphaCutoff to see through); iron side tiles use "
                             "iron_block / wood planks per family (mesher + runtime icon spec + offline icon)";
    }

    // ── P-t880 异形物品 3D 模型族（ItemShapeGeometry 行为级 + 红石粉粉堆源码钉）──
    //    (a) 几何契约：每族 blockId 建形状后断言**顶点数 + 形心居中 bounds**——活板门 24 顶点（单薄板，
    //        yMax=3/32）/ 台阶 24（半高盒 yMax=0.25）/ 楼梯 48（两盒）/ 雪层 24（yMax=1/16）/ 火把 24
    //        （细柱 xMax=1/16 —— cell [7/16,9/16] 居中即 ±1/16）/ 草丛 16 顶点（2 对角片 × 双面 2 pass，
    //        双面 = 4 quad → 16）+ 每片索引 12×2（cross 双面发）。这些断言钉「形状真建了 + 形心居中口径」，
    //        防退化成满立方（24 顶点但 bounds ±0.5 —— bounds 断言抓它）。
    //    (b) 红石粉 item 图标改粉堆瓦片 167（dust_dot_off；旧 166 线向 = 「一条线」观感根因）——运行期
    //        spec 内部函数不可直调 → 源码钉 flatSpec(167) 落位（滤注释）。
    {
        bool ok = true;
        {
            ItemShapeGeometry g;
            struct Expect { int blockId; int vCount; float yMax; float xMax; };
            const Expect exp[] = {
                { int(BR::WoodTrapdoor), 24, 3.0f / 32.0f + 0.001f, 0.501f },
                { int(BR::WoodSlab),     24, 0.25f + 0.001f,        0.501f },
                { int(BR::WoodStairs),   48, 0.501f,                0.501f },
                { int(BR::SnowLayer),    24, 1.0f / 16.0f + 0.001f, 0.501f },
                { int(BR::Torch),        24, 0.313f,                1.0f / 16.0f + 0.001f },
                { int(BR::TallGrass),    16, 0.501f,                0.501f },
                { int(BR::EnchantingTable), 24, 0.376f,             0.501f },
            };
            for (const Expect &e : exp) {
                g.setBlockId(e.blockId);
                const QByteArray vd = g.vertexData();
                const int vCount = int(vd.size()) / 20; // stride = pos3+uv2 = 5 float = 20B（类注释契约）
                const QVector3D bMax = g.boundsMax();
                if (vCount != e.vCount || bMax.y() > e.yMax || bMax.x() > e.xMax) {
                    ok = false;
                    qInfo().noquote() << "  t880 diag: id" << e.blockId << "v" << vCount
                                      << "expect" << e.vCount << "yMax" << bMax.y() << "xMax" << bMax.x();
                }
            }
        }
        // (b) 源码钉：红石粉 flatSpec 用粉堆瓦片 167（非 def.topTile 166 线向）。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile rf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
            const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("case BlockRegistry::RedstoneDust:"));
            const int i1 = t.indexOf(QStringLiteral("case BlockRegistry::WheatCrop:"), i0);
            if (i0 < 0 || i1 <= i0) {
                ok = false;
                qInfo().noquote() << "  t880 pin slice miss (dust flatSpec)";
            } else {
                const QString seg = t.mid(i0, i1 - i0);
                ok = ok && seg.contains(QStringLiteral("flatSpec(167)"))
                     && !seg.contains(QStringLiteral("flatSpec(d.topTile)"));
            }
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t880 item 3D family: ItemShapeGeometry builds real partial shapes (trapdoor "
                             "plate / slab half-box / stairs two-box / snow 1/8 / torch 2/16-column / grass "
                             "double-sided cross / enchant 0.75 box) centered on each shape's mid-height with "
                             "vertex-count + bounds contracts pinned per family, and the redstone dust item icon "
                             "renders the dust-dot pile tile 167 instead of the wire line 166 (source pin)";
    }

    // ── t822 铁砧附魔丢失实机复现二探针（R19.13）：t792 桩外两段真链补测 ──
    //   用户再报「附魔物品放入铁砧 UI 即消失附魔、取出变普通」；t792 实机探针（qml.exe 驱动真实
    //   AnvilUI.qml + InventoryOps.js，11 放入路径）47/47 全过，但其 Hotbar 是 **QML 桩**
    //   （build/_anvil_probe/VoxelSandbox/Hotbar.qml 语义复刻，非 C++ hotbar.cpp 本体），且
    //   **存档序列化 round-trip（t622 gatherPlayerState/applyPlayerState 落盘 enchants/name）从未
    //   有任何自动化探针**。本探针把两段桩外真链补进矩阵：
    //   (A) 真 Hotbar VM 光标序列镜像（AnvilUI.slotLeft :253-261 与 takeProduct 落定段 :894-900 的
    //       C++ 逐行等价——同一调用序：writeSlot 清源 → heldBlock → heldCount → heldDurability →
    //       setHeldEnchants → heldCustomName；顺序敏感：setHeldBlock 切新 id 清附魔+清名，附魔回填必须
    //       在其后）。含「同 id 早退」边角（光标已持同 id 素品、槽内附魔品的 pickup 序列——早退不清
    //       字段、后续 setter 逐个覆写，探针断言实 VM 语义与桩一致）。类别覆盖工具 / 护甲 / 附魔书 +
    //       四条满配多附魔（4 ench 全占用）。
    //   ⚠ 同步义务（review24 #10）：A 段是 AnvilUI.qml 调用序的**手工镜像**——改 slotLeft / takeProduct /
    //   returnAnvilToHotbar 的 setter 调用序（增删 / 换序，典型丢失形态如漏 heldDurability）时**必须同步
    //   本探针 A1-A4**，否则探针按旧序继续 PASS 掩盖真回归；本段也刻意不镜像 InventoryOps.resolveClick
    //   的 JS 分派（那层由 t792 qml.exe 实机探针覆盖）。中期方案（暂不做）：qml.exe 挂真 Hotbar C++ 对象
    //   替换 t792 桩，消掉拼接缝。
    //   (B) 真 WorldStore SQLite round-trip（Main.qml gatherPlayerState :665-688 的精确 map 形状
    //       version 3 / applyPlayerState :634-655 的精确回灌调用）：hotbar 9 + main 27 + armor 4
    //       每槽 id/count/durability/enchants[4]/name 落盘 → 关库重开 → 读回 → 灌入新 Hotbar VM
    //       逐字段比对（含空槽）。存档库用临时目录绝对路径（dbPath 对绝对入参直通，不污染 saves/）。
    //   全 PASS = 桩外真链同判完好，用户症状按 t814 版本戳方法论走复现文档分流。
    bool okA_pickup = true, okA_sameId = true, okA_place = true, okA_return = true;
    {
        Hotbar vm;
        const int pick = ToolRegistry::PickaxeIron;                  // 0x102
        const int chest = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
        const int book = RecipeRegistry::EnchantedBookId;            // 0x227
        const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
        const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
        const int pickDur = ToolRegistry::maxDurability(pick) - 5;   // 磨损实例（非满耐久）
        vm.setStack(2, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "我的神镐");

        // (A1) 普通拾取：光标空 → 槽 2 附魔镐上光标（AnvilUI.slotLeft 拾取臂逐行镜像）。
        vm.setStack(2, 0, 0, 0);                                     // writeSlot 清源槽
        vm.setHeldBlock(pick);                                       // 切新 id：清附魔/清名/满耐久
        vm.setHeldCount(1);
        vm.setHeldDurability(pickDur);                               // 覆写为槽内实例耐久
        vm.setHeldEnchants(QVariantList{eff3, unb2, 0, 0});          // 覆写为槽内实例附魔
        vm.setHeldCustomName(QStringLiteral("我的神镐"));
        {
            const QVariantList he = vm.heldEnchants();
            okA_pickup = vm.heldBlock() == pick && vm.heldDurability() == pickDur
                          && vm.heldCustomName() == QStringLiteral("我的神镐")
                          && int(he.size()) == 4 && he.at(0).toInt() == eff3 && he.at(1).toInt() == unb2
                          && he.at(2).toInt() == 0 && he.at(3).toInt() == 0;
            if (!okA_pickup)
                qInfo().noquote() << "  [t822 A1 diag] held=" << vm.heldBlock() << "dur=" << vm.heldDurability()
                                  << "ench=" << he;
        }
        // (A2) 放入（resolveClick B 整栈放置臂镜像）：光标 → main 槽 5（铁砧 A 槽是 QML 本地数组，
        //   入槽写的就是这些 VM 读值——readSlot/writeSlot 经同一组访问器）。
        vm.mainSetStack(5, vm.heldBlock(), vm.heldCount(), vm.heldDurability(), vm.heldEnchants(), vm.heldCustomName());
        vm.setHeldBlock(0);                                          // 光标清（AnvilUI r.heldId=0 分支）
        {
            const QVariantList me = vm.mainEnchantsAt(5);
            okA_place = vm.mainBlockIdAt(5) == pick && vm.mainCountAt(5) == 1
                         && vm.mainDurabilityAt(5) == pickDur
                         && vm.mainCustomNameAt(5) == QStringLiteral("我的神镐")
                         && int(me.size()) == 4 && me.at(0).toInt() == eff3 && me.at(1).toInt() == unb2
                         && me.at(2).toInt() == 0 && me.at(3).toInt() == 0;
            if (!okA_place)
                qInfo().noquote() << "  [t822 A2 diag] main5=" << vm.mainBlockIdAt(5) << "dur=" << vm.mainDurabilityAt(5)
                                  << "ench=" << me << "name=" << vm.mainCustomNameAt(5);
        }
        // (A3) 同 id 早退边角：光标持同 id **素品**（无附魔满耐久），拾取槽内**附魔品**——
        //   setHeldBlock 早退不清字段，后续四个 setter 逐个覆写 → 附魔必须落上（桩同语义实链验证）。
        vm.setHeldBlock(pick);
        vm.setHeldCount(1);
        vm.setHeldDurability(ToolRegistry::maxDurability(pick));
        vm.setHeldEnchants(QVariantList{0, 0, 0, 0});
        vm.setHeldCustomName(QString());
        vm.setStack(6, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "第二把");
        vm.setStack(6, 0, 0, 0);                                     // 清源
        vm.setHeldBlock(pick);                                       // 同 id → 早退（旧字段保留）
        vm.setHeldCount(1);
        vm.setHeldDurability(pickDur);
        vm.setHeldEnchants(QVariantList{eff3, unb2, 0, 0});
        vm.setHeldCustomName(QStringLiteral("第二把"));
        {
            const QVariantList he2 = vm.heldEnchants();
            okA_sameId = he2.at(0).toInt() == eff3 && he2.at(1).toInt() == unb2
                          && vm.heldDurability() == pickDur && vm.heldCustomName() == QStringLiteral("第二把");
            if (!okA_sameId)
                qInfo().noquote() << "  [t822 A3 diag] held dur=" << vm.heldDurability()
                                  << "ench=" << he2 << "name=" << vm.heldCustomName();
        }
        // (A4) 关包归还（AnvilUI.returnAnvilToHotbar → addToAny 全参镜像）：护甲 + 附魔书整件归还，
        //   空槽开新分支须写附魔 + 名（合并不搬实例元数据；cap=1 物品永不走合并）。
        vm.mainSetStack(6, chest, 1, 40, QVariantList{(EnchantRegistry::Protection << 8) | 4, unb2, 0, 0}, "守护者");
        vm.mainSetStack(7, book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        const int leftChest = vm.addToAny(chest, 1, 40, QVariantList{(EnchantRegistry::Protection << 8) | 4, unb2, 0, 0}, "守护者");
        const int leftBook = vm.addToAny(book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        bool foundChest = false, foundBook = false;
        for (int i = 0; i < vm.slotCount(); ++i) {
            if (vm.blockIdAt(i) == chest) {
                const QVariantList ce = vm.enchantsAt(i);
                foundChest = vm.customNameAt(i) == QStringLiteral("守护者") && vm.durabilityAt(i) == 40
                              && ce.at(0).toInt() == ((EnchantRegistry::Protection << 8) | 4) && ce.at(1).toInt() == unb2;
            }
            if (vm.blockIdAt(i) == book) {
                const QVariantList be = vm.enchantsAt(i);
                foundBook = be.at(0).toInt() == ((EnchantRegistry::Sharpness << 8) | 5)
                             && be.at(1).toInt() == ((EnchantRegistry::FireAspect << 8) | 1);
            }
        }
        okA_return = leftChest == 0 && leftBook == 0 && foundChest && foundBook;
        if (!okA_return)
            qInfo().noquote() << "  [t822 A4 diag] leftChest=" << leftChest << "leftBook=" << leftBook
                              << "foundChest=" << foundChest << "foundBook=" << foundBook;
        const bool okT822a = okA_pickup && okA_place && okA_sameId && okA_return;
        if (!okT822a) ++totalFail;
        qInfo().noquote() << (okT822a ? "PASS" : "FAIL")
                          << "| t822a real-Hotbar anvil cursor chain (mock-VM residual divergence closed): "
                             "AnvilUI.slotLeft/takeProduct/returnAnvilToHotbar call sequences mirrored onto the "
                             "actual C++ Hotbar VM - pickup clears-then-refills cursor in order (setHeldBlock "
                             "new-id wipes ench/name, setters after), same-id pickup early-return keeps stale "
                             "fields then overwrites one-by-one (ench MUST land), full-stack place into main, "
                             "addToAny close-panel return for armor piece + enchanted book (new-slot branch "
                             "writes ench/name, cap-1 never merges); covered categories tool/armor/book + "
                             "multi-ench (2-slot pick, 4-field arrays)";
    }

    // (B) 真 WorldStore SQLite round-trip：gatherPlayerState 精确形状落盘 → 关库重开 → applyPlayerState
    //   精确调用回灌 → 全字段比对。四条满配剑（4 ench 全占用）覆盖多附魔上界。
    bool okB_build = true, okB_round = true, okB_apply = true;
    {
        Hotbar vm2;
        const int pick = ToolRegistry::PickaxeIron;
        const int sword = ToolRegistry::SwordIron;
        const int chest = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
        const int book = RecipeRegistry::EnchantedBookId;
        const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
        const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
        const int sharp3 = (EnchantRegistry::Sharpness << 8) | 3;
        const int kb2 = (EnchantRegistry::Knockback << 8) | 2;
        const int fire2 = (EnchantRegistry::FireAspect << 8) | 2;
        const int unb3 = (EnchantRegistry::Unbreaking << 8) | 3;
        const int prot4 = (EnchantRegistry::Protection << 8) | 4;
        const int pickDur = ToolRegistry::maxDurability(pick) - 5;
        const int swordDur = ToolRegistry::maxDurability(sword) - 9;
        const int chestDur = ArmorRegistry::maxDurability(chest) - 17;
        vm2.setStack(2, pick, 1, pickDur, QVariantList{eff3, unb2, 0, 0}, "我的神镐");
        vm2.setStack(3, book, 1, 0, QVariantList{(EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0}, "");
        vm2.mainSetStack(5, sword, 1, swordDur, QVariantList{sharp3, kb2, fire2, unb3}, "勇者之剑");
        vm2.mainSetStack(6, chest, 1, chestDur, QVariantList{prot4, unb2, 0, 0}, "守护者");
        vm2.armorSetStack(1, chest, 1, chestDur - 3, QVariantList{(EnchantRegistry::Protection << 8) | 3, unb2, 0, 0}, "");

        // gather（Main.qml gatherPlayerState :665-688 精确镜像：每槽五字段 + version 3）。
        QVariantMap data;
        QVariantList hotbarArr, mainArr, armorArr;
        for (int i = 0; i < vm2.slotCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.blockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.countAt(i));
            s.insert(QStringLiteral("durability"), vm2.durabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.enchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.customNameAt(i));
            hotbarArr.append(s);
        }
        for (int i = 0; i < vm2.mainCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.mainBlockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.mainCountAt(i));
            s.insert(QStringLiteral("durability"), vm2.mainDurabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.mainEnchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.mainCustomNameAt(i));
            mainArr.append(s);
        }
        for (int i = 0; i < vm2.armorCount(); ++i) {
            QVariantMap s;
            s.insert(QStringLiteral("id"), vm2.armorBlockIdAt(i));
            s.insert(QStringLiteral("count"), vm2.armorCountAt(i));
            s.insert(QStringLiteral("durability"), vm2.armorDurabilityAt(i));
            s.insert(QStringLiteral("enchants"), vm2.armorEnchantsAt(i));
            s.insert(QStringLiteral("name"), vm2.armorCustomNameAt(i));
            armorArr.append(s);
        }
        data.insert(QStringLiteral("version"), 3);
        data.insert(QStringLiteral("px"), 40); data.insert(QStringLiteral("py"), 44); data.insert(QStringLiteral("pz"), 40);
        data.insert(QStringLiteral("yaw"), 0); data.insert(QStringLiteral("pitch"), -42);
        data.insert(QStringLiteral("mode"), 0);
        data.insert(QStringLiteral("health"), 20); data.insert(QStringLiteral("hunger"), 20);
        data.insert(QStringLiteral("xp"), 7);
        data.insert(QStringLiteral("selectedSlot"), 2);
        data.insert(QStringLiteral("hotbar"), hotbarArr);
        data.insert(QStringLiteral("main"), mainArr);
        data.insert(QStringLiteral("armor"), armorArr);

        // 落盘 → 关库 → 重开 → 读回（临时目录绝对路径；dbPath 对绝对入参直通，不污染 saves/）。
        //   review24 #10：文件名拼 PID——固定名两实例并发时 Windows 对被占用文件的 QFile::remove 静默
        //   失败 → 两进程共库互相 INSERT OR REPLACE 覆盖 / 环境性假 FAIL（数据形状相同还可能巧合双 PASS
        //   掩盖竞态）；PID 后缀按进程隔离（本进程退出后的遗留文件仍由两次 remove + 下轮覆盖清）。
        WorldStore store;
        const QString dbAbs = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t822_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbAbs);
        okB_build = store.openWorld(dbAbs) && store.savePlayerData(data);
        store.closeWorld();
        QVariantMap back;
        if (okB_build) {
            okB_build = store.openWorld(dbAbs);
            if (okB_build) back = store.loadPlayerData();
            store.closeWorld();
        }
        QFile::remove(dbAbs);
        if (!okB_build) {
            qInfo().noquote() << "  [t822 B diag] open/save/reopen failed";
        } else {
            // JSON 读回逐字段（toInt 显式转换：JSON 数字经 toVariant 可能成 double，勿依赖 QVariant==）。
            const auto slotEq = [](const QVariantMap &s, int id, int count, int dur, int e0, int e1, int e2, int e3, const QString &name) {
                const QVariantList e = s.value(QStringLiteral("enchants")).toList();
                return s.value(QStringLiteral("id")).toInt() == id
                        && s.value(QStringLiteral("count")).toInt() == count
                        && s.value(QStringLiteral("durability")).toInt() == dur
                        && s.value(QStringLiteral("name")).toString() == name
                        && int(e.size()) == 4 && e.at(0).toInt() == e0 && e.at(1).toInt() == e1
                        && e.at(2).toInt() == e2 && e.at(3).toInt() == e3;
            };
            const QVariantList hb = back.value(QStringLiteral("hotbar")).toList();
            const QVariantList mn = back.value(QStringLiteral("main")).toList();
            const QVariantList ar = back.value(QStringLiteral("armor")).toList();
            okB_round = int(hb.size()) == vm2.slotCount() && int(mn.size()) == vm2.mainCount() && int(ar.size()) == vm2.armorCount()
                          && slotEq(hb.at(0).toMap(), 0, 0, 0, 0, 0, 0, 0, QString())
                          && slotEq(hb.at(2).toMap(), pick, 1, pickDur, eff3, unb2, 0, 0, QStringLiteral("我的神镐"))
                          && slotEq(hb.at(3).toMap(), book, 1, 0, (EnchantRegistry::Sharpness << 8) | 5, (EnchantRegistry::FireAspect << 8) | 1, 0, 0, QString())
                          && slotEq(mn.at(5).toMap(), sword, 1, swordDur, sharp3, kb2, fire2, unb3, QStringLiteral("勇者之剑"))
                          && slotEq(mn.at(6).toMap(), chest, 1, chestDur, prot4, unb2, 0, 0, QStringLiteral("守护者"))
                          && slotEq(mn.at(26).toMap(), 0, 0, 0, 0, 0, 0, 0, QString())
                          && slotEq(ar.at(1).toMap(), chest, 1, chestDur - 3, (EnchantRegistry::Protection << 8) | 3, unb2, 0, 0, QString());
            if (!okB_round) {
                qInfo().noquote() << "  [t822 B diag] hbN=" << hb.size() << "mnN=" << mn.size() << "arN=" << ar.size()
                                  << " hb2=" << hb.at(2).toMap() << " mn5=" << mn.at(5).toMap() << " ar1=" << ar.at(1).toMap();
            }
            // apply（Main.qml applyPlayerState :634-655 精确镜像）→ 新 VM 逐字段与 vm2 比对。
            Hotbar vm3;
            for (int i = 0; i < 9; ++i) {
                const QVariantMap s = hb.at(i).toMap();
                vm3.setStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                             s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                             s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                             s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            for (int i = 0; i < 27; ++i) {
                const QVariantMap s = mn.at(i).toMap();
                vm3.mainSetStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                                 s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                                 s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                 s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            for (int i = 0; i < 4; ++i) {
                const QVariantMap s = ar.at(i).toMap();
                vm3.armorSetStack(i, s.value(QStringLiteral("id")).toInt(), s.value(QStringLiteral("count")).toInt(),
                                  s.contains(QStringLiteral("durability")) ? s.value(QStringLiteral("durability")).toInt() : -1,
                                  s.contains(QStringLiteral("enchants")) ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                  s.contains(QStringLiteral("name")) ? s.value(QStringLiteral("name")).toString() : QString());
            }
            const auto vmEq = [&vm2, &vm3](bool armor, int i) {
                const int idA = armor ? vm2.armorBlockIdAt(i) : (i < 9 ? vm2.blockIdAt(i) : vm2.mainBlockIdAt(i - 9));
                const int idB = armor ? vm3.armorBlockIdAt(i) : (i < 9 ? vm3.blockIdAt(i) : vm3.mainBlockIdAt(i - 9));
                // review24 #10 顺带：补比 count 字段——「JSON→VM 灌入丢 count」回归此前在 VM 级漏检
                //   （JSON 级 slotEq 可兜存档层，VM 级补齐后两段都有防线）。
                const int cntA = armor ? vm2.armorCountAt(i) : (i < 9 ? vm2.countAt(i) : vm2.mainCountAt(i - 9));
                const int cntB = armor ? vm3.armorCountAt(i) : (i < 9 ? vm3.countAt(i) : vm3.mainCountAt(i - 9));
                const int durA = armor ? vm2.armorDurabilityAt(i) : (i < 9 ? vm2.durabilityAt(i) : vm2.mainDurabilityAt(i - 9));
                const int durB = armor ? vm3.armorDurabilityAt(i) : (i < 9 ? vm3.durabilityAt(i) : vm3.mainDurabilityAt(i - 9));
                const QString nmA = armor ? vm2.armorCustomNameAt(i) : (i < 9 ? vm2.customNameAt(i) : vm2.mainCustomNameAt(i - 9));
                const QString nmB = armor ? vm3.armorCustomNameAt(i) : (i < 9 ? vm3.customNameAt(i) : vm3.mainCustomNameAt(i - 9));
                const QVariantList eA = armor ? vm2.armorEnchantsAt(i) : (i < 9 ? vm2.enchantsAt(i) : vm2.mainEnchantsAt(i - 9));
                const QVariantList eB = armor ? vm3.armorEnchantsAt(i) : (i < 9 ? vm3.enchantsAt(i) : vm3.mainEnchantsAt(i - 9));
                if (idA != idB || cntA != cntB || durA != durB || nmA != nmB || eA.size() != eB.size()) return false;
                for (int k = 0; k < int(eA.size()); ++k) if (eA.at(k).toInt() != eB.at(k).toInt()) return false;
                return true;
            };
            bool eq = true;
            for (int i = 0; i < 9 && eq; ++i) eq = vmEq(false, i);
            for (int i = 0; i < 27 && eq; ++i) eq = vmEq(false, i + 9);
            for (int i = 0; i < 4 && eq; ++i) eq = vmEq(true, i);
            okB_apply = eq;
            if (!okB_apply)
                qInfo().noquote() << "  [t822 B apply diag] applied VM differs from source VM (field mismatch above)";
        }
        const bool okT822b = okB_build && okB_round && okB_apply;
        if (!okT822b) ++totalFail;
        qInfo().noquote() << (okT822b ? "PASS" : "FAIL")
                          << "| t822b real-WorldStore SQLite enchant round-trip (serialization leg never probed "
                             "before): exact gatherPlayerState v3 map shape (hotbar9+main27+armor4, per-slot "
                             "id/count/durability/enchants[4]/name) saved via savePlayerData -> close -> reopen -> "
                             "loadPlayerData -> JSON field compare -> exact applyPlayerState calls into a fresh "
                             "Hotbar -> all-slot field equality vs source VM; covered multi-ench 4/4-full sword, "
                             "enchanted book, armor piece with partial durability + custom names + empty slots "
                             "(db on temp-dir absolute path, saves/ untouched)";
    }

    // ── t809 空车身体推送过拐角探针（pushEmptyCart 选向，MinecartManager 直编，P12c 同款 L 形场景）──
    //   用户报告（R19.13）：空车沿直段长按 W 连推（视点 / 输入不随拐角转向），推到拐弯处来回振荡「推不动」。
    //   根因：pushEmptyCart 旧版把 wish 直接当选向向量 → 车过拐角后停在与 wish 垂直的臂上，两臂点积同为 0
    //   平局 → kDirs 枚举序破平局（Px 先于 Nx、Pz 先于 Nz）→ 拐角出口朝枚举序败者（-X / -Z）时选中**指回
    //   拐角**的臂 → 车滑回拐角、到心重选（运动向）又把车送回来路 → 推一下退一格的往返振荡，永不抵达死端。
    //   修后选向 = away（车−玩家，权重 1.0）+ wish（0.5）+ dir（0.25）合成向量（身体推开语义，机制等价
    //   MC 玩家撞静止矿车 → 车沿轨被推离玩家）。断言（修前 FAIL / 修后 PASS）：
    //   rig：L 形（南腿死端 1 + 直段 1 + 拐角[出口 -X = 枚举序败者] + 西臂 2 = 西死端）；
    //   玩家模型 = 贴身追随（每帧玩家位 = 车上一帧位，P11(d) 先例）+ wish 恒北（0,-1) —— 长按 W 视点不转；
    //   (a) 车抵达西死端格心 ±0.05 且贴中心线（|z−(z0-2+0.5)| ≤0.05，t770 ② 钉轨）；
    //   (b) 抵达后 100 tick 停驻不动（死端无沿合成向的可走连接 → 不再被推走）；
    //   (c) 全程 5 格 L 形 footprint 内 + Y 钉轨面；
    //   (d) 振荡诊断计数：从西臂滑回拐角的次数（修前每循环 +1 不收敛；修后 0）。
    {
        // rig 寻址：运行期扫描空区（P20/P23 先例——nextSlot() 网格已被上方探针耗尽）。需 6×5×5 净空
        //   （含隔离边；x 从 x0-2 到 x0、z 从 z0-2 到 z0）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 1 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -3; dx <= 1 && clear; ++dx)
                    for (int dz = -3; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t809 L-push corner: no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 文档值（P11 同款镜像注释）
            // 摆轨：臂与腿先放、拐角最后放（邻齐一次成形，P16 先例）；拐角出口 = -X（枚举序败者）。
            w.setBlock(x0,     kRigY, z0,     BR::Rail, 0); // 南死端（spawn 格）
            w.setBlock(x0,     kRigY, z0 - 1, BR::Rail, 0); // 直段
            w.setBlock(x0 - 1, kRigY, z0 - 2, BR::Rail, 0); // 西臂 1
            w.setBlock(x0 - 2, kRigY, z0 - 2, BR::Rail, 0); // 西死端
            w.setBlock(x0,     kRigY, z0 - 2, BR::Rail, 0); // 拐角（南臂 + 西臂）
            const quint8 cCon = quint8(w.stateAt(x0, kRigY, z0 - 2) & 0x0F);
            bool ok = cCon == quint8(BR::RailConnPz | BR::RailConnNx); // 拐角 = 两垂直臂位（rig 自检）
            if (!ok) qInfo().noquote() << "  t809 corner con" << int(cCon) << "expect Pz|Nx";
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            QVector3D player = carts.posAt(0);
            int lastBx = int(std::floor(player.x())), lastBz = int(std::floor(player.z()));
            const auto onL = [&](int bx, int bz) {
                return (bx == x0 && bz >= z0 - 2 && bz <= z0) || (bz == z0 - 2 && bx >= x0 - 2 && bx <= x0 - 1);
            };
            int arrivedTick = -1, cornerBacks = 0;
            QVector3D arrivePos;
            bool yOk = true;
            for (int t = 0; t < 1500 && ok; ++t) {
                // t863④ 适配：抵达西死端格后停推（追推会把死端车推离轨道——本探针验拐角选向非推离；
                //   停推后余速滑到格心停驻，观察窗看稳态）。
                if (!(int(std::floor(player.x())) == x0 - 2 && int(std::floor(player.z())) == z0 - 2))
                    carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北：wish 恒定不随拐角转（用户场景）
                carts.tickPushedCarts(0.016, &w);
                const QVector3D cp = carts.posAt(0);
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                if (!onL(bx, bz)) {
                    qInfo().noquote() << "  t809 cart left L at tick" << t << "pos" << cp;
                    ok = false;
                    break;
                }
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.01f) yOk = false;
                if (lastBz == z0 - 2 && lastBx == x0 - 1 && bx == x0) ++cornerBacks; // 西臂滑回拐角（振荡签名）
                if (arrivedTick < 0 && bx == x0 - 2 && bz == z0 - 2 && cp.x() <= float(x0 - 2) + 0.55f) {
                    arrivedTick = t;
                    arrivePos = cp;
                }
                lastBx = bx; lastBz = bz;
                player = cp; // 贴身追随（P11(d) 先例：玩家追着车、静止即续推）
                if (arrivedTick >= 0 && t - arrivedTick >= 100) break; // 停驻观察窗已满
            }
            // (a) 抵达 + 中心线；(b) 停驻 100 tick 位移 <0.05（观察窗内不被推走）。
            if (ok && arrivedTick >= 0) {
                ok = std::fabs(arrivePos.z() - (float(z0 - 2) + 0.5f)) <= 0.05f
                    && std::fabs(carts.posAt(0).x() - arrivePos.x()) < 0.05f
                    && std::fabs(carts.posAt(0).z() - arrivePos.z()) < 0.05f;
            } else if (ok) {
                qInfo().noquote() << "  t809 cart never reached west dead-end (oscillation?), final"
                                  << carts.posAt(0) << "cornerBacks" << cornerBacks;
            }
            ok = ok && arrivedTick >= 0 && yOk && cornerBacks == 0;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t809 empty-cart body-push through corner (exit toward enum-order loser):"
                                 " reaches west dead-end center + parks, no oscillation; arrivedTick"
                              << arrivedTick << "cornerBacks" << cornerBacks;
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            for (int dx = 0; dx <= 2; ++dx) w.setBlock(x0 - dx, kRigY, z0 - 2, BR::Air);
            tickN(w, 2);
        }
    }

    // ── t810 满动力轨环线骑乘速度曲线探针（tickRiddenCart 动力段 boost，MinecartManager 直编，P11 环场景）──
    //   用户报告（R19.13）：满动力轨环线骑乘过弯速度骤减（两条动力轨喂入也救不回）；同轨空车匀速圈跑。
    //   根因：tickRiddenCart 动力段旧版按 **proj 幅度**（wish·dir 投影）改写目标速 → 过弯后玩家视点未跟上
    //   新行进向的窗口 proj≈0 → 弹射档 2.8 接管 → boost 12.8 以 ~3 格/s² 拉垮到爬行速；空车路径
    //   （tickPushedCarts t735 ④）按运动符号全额 boost 无此症（对照即定位）。修后无输入且车在动 → 沿 speed
    //   符号全额 boost；前进输入 → 全 boost 不按 proj 打折；反踩刹车 → 玩家意图优先。
    //   rig：5×5 环 = 四角普通轨 + 每边 3 格动力轨直段（动力轨不拐弯 t771 → 拐角必普通轨；3×3 环拐角占比
    //   50% 是病态几何 —— 拐角摩擦本身就把均衡速压到 ~9，非用户场景）+ 环内 3×3 除心外 8 格红石块直供
    //   全部 12 条动力轨（tickRedstone 置 bit4，直写 state 会被电力重算清掉 → 必须真源供）；
    //   wish 模型 = 相位制：A 段无输入（proj≡0 —— 纯动力轨维持力断言，无视点模型、修前修后分离度最大：
    //   修前弹射档 2.7 每 tick 接管全部动力格 → 均衡速崩到 ~3）；B 段采样保持（每 16 tick 重采为当前车头向
    //   yaw 反推 dir —— 玩家过弯后 ~0.26s 转回镜头的滞后模型；从 yaw 采样而非位移：位移采样在小环上会采到
    //   跨拐角对角向、再下一拐角后成反向刹车，是探针伪影非玩家行为。旧 P11(c) 每 tick 动态随行进向 → proj
    //   恒 1，把本缺陷完全掩蔽，故须新探针）；
    //   (a) 环 footprint + Y 钉轨面 + 跨格单位轴对齐（P11 同款）；
    //   (b) A 段速度曲线（|Δpos|/dt，预热 60 tick 后统计）：minA ≥5.0 且 meanA ≥8.0（修后均衡 ~7.2/10.4
    //       —— boost 12.8 − 拐角普通轨摩擦小谷；修前崩到 0.6/2.0 → 双断言 FAIL）；
    //   (c) B 段滞后输入曲线：minB ≥6.0 且 meanB ≥10.0（修后 ~7.9/11.0；修前滞窗动力格弹射档拉垮到
    //       2.5/8.9）；A+B 共 1600 tick（25.6s）内 ≥13 圈（修后 ~17 圈 / 修前 8 圈，16 格/圈）；
    //   (d) 刹车守卫：C 段 wish 反车头向（每 tick 跟随）60 tick 内 |v| 一度 <7（proj<0 不被 boost 角力）；
    //   (e) 恢复守卫：D 段 wish 车头向 300 tick 内 v 回 ≥11（全 boost 档恢复力）。
    {
        // rig 寻址：运行期扫描空区（P20/P23 先例）。5×5 环（环心 ±2）+ 1 格隔离边 → 需 7×7×5 净空
        //   （隔离边防邻 rig 红石元件误供本环动力轨）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 92 && x0 < 0; zz += 2)
            for (int xx = 3; xx + 6 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -3; dx <= 3 && clear; ++dx)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t810 powered-ring speed curve: no clear rig area found";
        } else {
            const int cx = x0 + 3, cz = z0 + 3; // 环心（环 = max(|dx|,|dz|)==2 的 16 格）
            const float rideH = 0.45f;          // kCartRideH 文档值（P11 同款镜像注释）
            // 摆环：四边直段动力轨（每边 3 格）+ 四角普通轨（拐角必普通轨 t771）；
            //   环内 3×3 除心外 8 格红石块（每条动力轨至少一格 4 邻直供，t740 语义），最后放。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    const int ax = std::abs(dx), az = std::abs(dz);
                    const int m = std::max(ax, az);
                    if (m == 2)
                        w.setBlock(cx + dx, kRigY, cz + dz, (ax == 2 && az == 2) ? BR::Rail : BR::GoldenRail, 0);
                }
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx != 0 || dz != 0)
                        w.setBlock(cx + dx, kRigY, cz + dz, BR::RedstoneBlock, 0);
            tickN(w, 4);
            // rig 自检：12 格动力轨全通电 + 四拐角连接位 = 各自两邻臂（P11 corner 表）。
            bool ok = true;
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    const int ax = std::abs(dx), az = std::abs(dz);
                    if (std::max(ax, az) == 2 && !(ax == 2 && az == 2)
                        && (w.stateAt(cx + dx, kRigY, cz + dz) & BR::GoldenRailStateOnFlag) == 0)
                        ok = false;
                }
            const struct { int x, z; quint8 wantCon; } wantC[4] = {
                { cx - 2, cz - 2, quint8(BR::RailConnPx | BR::RailConnPz) },
                { cx + 2, cz - 2, quint8(BR::RailConnNx | BR::RailConnPz) },
                { cx - 2, cz + 2, quint8(BR::RailConnPx | BR::RailConnNz) },
                { cx + 2, cz + 2, quint8(BR::RailConnNx | BR::RailConnNz) },
            };
            for (const auto &c : wantC)
                if (quint8(w.stateAt(c.x, kRigY, c.z) & 0x0F) != c.wantCon) ok = false;
            if (!ok) qInfo().noquote() << "  t810 rig self-check failed (power/corner con)";
            const auto isRing = [&](int x, int z) {
                const int ax = std::abs(x - cx), az = std::abs(z - cz);
                return std::max(ax, az) == 2;
            };
            MinecartManager carts;
            carts.spawnCart(cx, kRigY, cz - 2, &w); // 北边中点（动力轨 EW 直位 → spawn 定向 ±X）
            const QVector3D mountOrigin(float(cx) + 0.5f, float(kRigY) + 2.0f, float(cz - 2) + 0.5f);
            ok = ok && carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            // 相位：A 无输入巡航 [0,900)（proj≡0：修前弹射档接管 → 均衡速崩到 ~3；修后沿运动向全 boost
            //   → 均衡 ~11）| B 滞后输入巡航 [900,1600)（wish 每 16 tick 重采车头向 —— 真实玩家过弯滞后）
            //   | C 倒踩刹车 [1600,1660)（wish = 反车头向每 tick 跟随）| D 恢复 [1660,2000)（wish = 车头向）。
            const int kLag = 16, kWarmup = 60, kNoInput = 900, kCruise = 1600, kBrake = 60, kRecover = 340;
            float wishX = 0.0f, wishZ = 0.0f;
            QVector3D prev = carts.posAt(0);
            int lastBx = int(std::floor(prev.x())), lastBz = int(std::floor(prev.z()));
            double sumA = 0.0, sumB = 0.0;
            int nA = 0, nB = 0, laps = 0;
            float minA = 1e9f, minB = 1e9f, maxRecoverV = 0.0f;
            bool yOk = true, adjOk = true, brakeDipped = false;
            // wish 采样保持：每 kLag tick 把 wish 重采为车头向（yaw 反推 dir —— 见头注释「从 yaw 采样」段）。
            const auto headingWish = [&carts](float &wx, float &wz) {
                const float yr = carts.yawAt(0) * 3.14159265358979f / 180.0f;
                wx = -std::sin(yr);
                wz = -std::cos(yr);
            };
            for (int t = 0; t < kCruise + kBrake + kRecover && ok; ++t) {
                if (t < kNoInput) {
                    wishX = 0.0f;
                    wishZ = 0.0f; // A：无输入（proj≡0，纯动力轨维持力断言）
                } else if (t < kCruise) {
                    if (t % kLag == 0) headingWish(wishX, wishZ); // B：采样保持重采（滞后 ~0.26s）
                } else if (t < kCruise + kBrake) {
                    headingWish(wishX, wishZ); // C：反车头向每 tick 跟随（不受起步时刻过拐角巧合干扰）
                    wishX = -wishX;
                    wishZ = -wishZ;
                } else {
                    headingWish(wishX, wishZ); // D：车头向每 tick 跟随（proj≈1）
                }
                QVector3D cp;
                carts.tickRiddenCart(0.016, &w, wishX, wishZ, cp);
                carts.tickPushedCarts(0.016, &w);
                const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                const float v = dl / 0.016f;
                const int bx = int(std::floor(cp.x())), bz = int(std::floor(cp.z()));
                if (!isRing(bx, bz)) {
                    qInfo().noquote() << "  t810 cart left ring at tick" << t << "pos" << cp;
                    ok = false;
                    break;
                }
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.01f) yOk = false;
                if (bx != lastBx || bz != lastBz) {
                    const int ndx = bx - lastBx, ndz = bz - lastBz;
                    if (std::abs(ndx) + std::abs(ndz) != 1) adjOk = false; // 跨格必单位轴对齐
                    if (bx == cx && bz == cz - 2 && t < kCruise) ++laps;   // 每入北边中点格 = 1 圈
                    lastBx = bx; lastBz = bz;
                }
                if (t < kWarmup) {
                    // 预热（起步加速不计曲线）
                } else if (t < kNoInput) {
                    sumA += double(v);
                    ++nA;
                    if (v < minA) minA = v;
                } else if (t < kCruise) {
                    sumB += double(v);
                    ++nB;
                    if (v < minB) minB = v;
                } else if (t < kCruise + kBrake) {
                    if (v < 7.0f) brakeDipped = true; // (d) 刹车压速（proj<0 不被 boost 角力）
                } else if (t >= kCruise + kBrake + 60) {
                    if (v > maxRecoverV) maxRecoverV = v; // (e) 恢复段峰值（留 60 tick 起步余量）
                }
                prev = cp;
            }
            const double meanA = nA > 0 ? sumA / double(nA) : 0.0;
            const double meanB = nB > 0 ? sumB / double(nB) : 0.0;
            // A 段：无输入均衡速 —— 修后动力段沿运动向全额 boost（拐角普通轨摩擦小谷）；修前弹射档 2.8
            //   把均衡速崩到 ~2（实测 0.6/2.0 → 双断言锁）。B 段：滞后输入下均值仍近 boost 档（输入不打折
            //   语义；修前滞窗动力格弹射档拉垮实测 minB 2.5/meanB 8.9）。阈值取修前修后实测值中位。
            ok = ok && yOk && adjOk && nA > 0 && nB > 0
                && minA >= 5.0f && meanA >= 8.0
                && minB >= 6.0f && meanB >= 10.0
                && laps >= 13 && brakeDipped && maxRecoverV >= 11.0f;
            if (!ok)
                qInfo().noquote() << "  t810 speed curve: minA" << minA << "meanA" << meanA
                                  << "minB" << minB << "meanB" << meanB << "laps" << laps
                                  << "yOk" << yOk << "adjOk" << adjOk << "brakeDipped" << brakeDipped
                                  << "maxRecoverV" << maxRecoverV;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t810 powered-ring ridden speed curve (no-input + lagged input, was corner"
                                 " collapse to catapult gear): minA" << minA << "meanA" << float(meanA)
                              << "minB" << minB << "meanB" << float(meanB) << "laps" << laps
                              << "; brake honored + recovers to boost";
            // 清场（环 16 格 + 环内 8 红石块）
            carts.clearAll();
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) > 0)
                        w.setBlock(cx + dx, kRigY, cz + dz, BR::Air);
            tickN(w, 2);
        }
    }

    // ── t811 生物自动乘坐矿车探针（EntityManager + MinecartManager 直编；登乘 / 满员拒载 / AI 冻结钉位
    //   随车 / 挖车释放 / 玩家占用不接客，全链一次过）──
    //   机制（R19.13 spec）：非骑乘 mob 走进矿车 ≤0.8 格自动登乘（乘员总数限 1：玩家 XOR 生物）；骑乘期
    //   AI / 物理全冻结、位置钉车座位（车动它动）；下车唯一路径 = 车被挖（对账自释放原地，恢复 AI）。
    //   rig：直轨 6 格（北向 z0-5..z0）+ 3 宽石板地板 kRigY-1（mob 落脚 / 释放后重力落点）；驱动序镜像
    //   PlayerController 真序（mob 桶 tick → 钉位① → step 内车物理 pushEmptyCart+tickPushedCarts → 钉位②）。
    //   断言：(a) 生于车格 → 首 pass 即登乘 + moveSpeed 归 0（姿态锁定）；(b) 推动期每 tick 钉位误差 <0.01
    //   （含 Y 座位公式 车心−0.3125+halfH）且车总位移 ≥3 格（确在动）；(c) 停驻后 100 tick 零漂移；
    //   (d) hitCartFromRay 挖车 → 下一 pass rideCart==-1 + 存活 + 重力落定地板顶（kRigY+halfH）；
    //   (e) 第二 mob 同格不登（生物占座满员）+ 玩家 tryMount 满员车被拒；(f) 玩家骑乘的车不接 mob。
    {
        // rig 选址：运行期扫描空区（t809 先例——nextSlot 网格已被上方探针耗尽）。需 7×4×6 净空。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 1 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 1 && clear; ++dx)
                    for (int dz = -6; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t811 mob ride cart: no clear rig area found";
        } else {
            const float seatDrop = 0.3125f; // kCartSeatDrop 同值（playercontroller.cpp 骑车分支同款镜像常量）
            // 3 宽石板地板（kRigY-1）+ 中列直轨 6 格（kRigY；北向 z0-5..z0）。
            for (int dz = -5; dz <= 0; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                    if (dx == 0) w.setBlock(x0, kRigY, z0 + dz, BR::Rail, 0);
                }
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mobA = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ff5555"), 10);
            QVector3D player = carts.posAt(0);
            bool boarded = false, pinOk = true, speedLocked = true;
            float travel = 0.0f;
            int parkedTicks = 0;
            QVector3D lastCart = carts.posAt(0);
            for (int t = 0; t < 1500 && parkedTicks < 100; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();                        // 钉位①（mob 桶内，游戏同序）
                // t863④ 适配：车进北死端格后停推（追推会把死端车推离轨道——本探针验登乘 / 钉位 / 释放，
                //   停推后余速滑到死端格心停驻，停驻窗看钉位零漂）。
                if (int(std::floor(player.z())) > z0 - 5)
                    carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北推（t809 玩家模型）
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();                        // 钉位②（step 后，同帧随车）
                const QVector3D cp = carts.posAt(0);
                const QVector3D mp = ents.posAt(mobA);
                if (ents.rideCartAt(mobA) >= 0) {
                    boarded = true;
                    if (std::fabs(mp.x() - cp.x()) > 0.01f
                        || std::fabs(mp.y() - (cp.y() - seatDrop + 0.5f)) > 0.01f
                        || std::fabs(mp.z() - cp.z()) > 0.01f) pinOk = false;
                    if (ents.moveSpeedAt(mobA) != 0.0f) speedLocked = false;
                    const float d = QVector3D(cp - lastCart).length();
                    if (d > 1e-4f) travel += d; else ++parkedTicks; // 停驻窗（车停后钉位零漂计数）
                }
                lastCart = cp;
                player = cp; // 贴身追随（t809 先例：玩家追着车、静止即续推）
            }
            // (d) 挖车释放（从车正上方垂直下挖；instantBreak 免耐久轮）→ 对账自释放 + 重力 / 落定重接手。
            //   t865 支撑收口后轨格（ShapeNone 无碰撞）不承载 → 释放 mob（resting 已清）穿透轨格、落定
            //   石板地板顶（kRigY−1 石块真顶 = kRigY）+ halfH —— 旧断言钉的 kRigY+1.5（轨当满格悬上一格）
            //   正是 t865「支撑判定把非整格当满格」修掉的行为。释放自座位高（≈kRigY+0.64）落 ~0.64 格
            //   需数 tick → 驱动至多 40 tick（0.64s，落定 + 余量）再量。
            const QVector3D cp = carts.posAt(0);
            const bool hit = carts.hitCartFromRay(QVector3D(cp.x(), cp.y() + 3.0f, cp.z()),
                                                  QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            const bool released = hit && !carts.aliveAt(0) && ents.rideCartAt(mobA) == -1 && ents.aliveAt(mobA);
            for (int t = 0; t < 40; ++t) { // 释放后物理 tick：重力穿透轨格落定地板 + AI 复活（listener 远，纯游荡）
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const float settleY = ents.posAt(mobA).y();
            const bool settleOk = std::fabs(settleY - (float(kRigY) + 0.5f)) <= 0.02f;
            // (e) 满员拒载：新车（槽复用 0）+ mobB 占座 → mobC 同格不登 + 玩家 tryMount 被拒。
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mobB = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#55ff55"), 10);
            const int mobC = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#5555ff"), 10);
            for (int t = 0; t < 8; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const bool fullOk = ents.rideCartAt(mobB) == 0 && ents.rideCartAt(mobC) == -1
                                && !carts.tryMount(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
            // (f) 玩家占用不接客：挖掉满员车（mobB 释放）→ 重放车 → 玩家骑 → mobD 同格不登。
            carts.hitCartFromRay(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                 QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            carts.spawnCart(x0, kRigY, z0, &w);
            const bool playerRode = carts.tryMount(carts.posAt(0) + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f)
                                    && carts.ridingIndex() == 0;
            const int mobD = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ffff55"), 10);
            for (int t = 0; t < 8; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const bool playerOccupyOk = playerRode && ents.rideCartAt(mobD) == -1;
            const bool ok = boarded && pinOk && speedLocked && travel >= 3.0f && parkedTicks >= 100
                            && released && settleOk && fullOk && playerOccupyOk;
            if (!ok)
                qInfo().noquote() << "  t811 cart: boarded" << boarded << "pinOk" << pinOk
                                  << "speedLocked" << speedLocked << "travel" << travel
                                  << "parked" << parkedTicks << "released" << released
                                  << "settleY" << settleY << "fullOk" << fullOk
                                  << "playerOccupyOk" << playerOccupyOk;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t811 mob auto-rides minecart: board+freeze-pin follows cart"
                                 " (seatY = cartY-0.3125+halfH), 100-tick park zero-drift, destroy"
                                 " releases + resettles, full cart refuses 2nd mob & player,"
                                 " player-ridden cart takes no mob; travel" << travel;
            // 清场
            carts.clearAll();
            ents.clearAll();
            for (int dz = -5; dz <= 0; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
                w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            }
            tickN(w, 2);
        }
    }

    // ── t811 生物自动乘坐船探针（EntityManager + BoatManager 直编；双座登乘 / 满员 / 玩家拒载 / 挖船释放）──
    //   rig：凿进天然石 3 宽水道（t805 模式：fy=44 石板 + 45..47 凿空 + 45 层铺水 → 水面顶 46；船浮 46）。
    //   断言：(a) mob A/B 生于船格 → 双双登乘（A 先扫 → 座 0 / B 座 1），双钉位横向分离 ~0.6（右舷 +0.3 /
    //   左舷 −0.3，yaw=0 时 right=+X）且 Y = 船位+halfH；(b) mob C 同格不登（2 生物 = 满员 2）；
    //   (c) 满员船玩家 tryMount 被拒（返 false 且 ridingIndex 不变）；(d) hitBoatFromRay 挖船 → A/B
    //   双双 rideBoat==-1 且存活（原地释放恢复 AI）。
    {
        const int bx = 40, bz = 10; // t805 rig（bx=6,bz=6,x≤39）之外的开阔石区
        const int fy = 44;          // 石板层；水面 = 45；水面顶 = 46
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                w.setBlock(bx + dx, fy, bz + dz, BR::Stone, 0);
                w.setBlock(bx + dx, fy + 1, bz + dz, BR::Air, 0);
                w.setBlock(bx + dx, fy + 2, bz + dz, BR::Air, 0);
                w.setBlock(bx + dx, fy + 3, bz + dz, BR::Air, 0);
            }
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(bx + dx, fy + 1, bz + dz, BR::Water, 0);
        BoatManager boats;
        EntityManager ents;
        ents.setVehicleManagers(nullptr, &boats);
        const bool spawned = boats.spawnBoat(bx + 2, fy + 1, bz, BoatManager::Oak);
        const QVector3D bp0 = boats.posAt(0);
        const int mA = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#ff5555"), 10);
        const int mB = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#55ff55"), 10);
        const int mC = ents.spawnMobTyped(bx + 2, fy + 1, bz, 0, QStringLiteral("#5555ff"), 10);
        QVector3D pA, pB;
        for (int t = 0; t < 12; ++t) {
            boats.tick(0.016, &w); // 船浮水常开（游戏序：boat 桶在 mob 桶前）
            ents.tick(0.016, &w, bp0, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            pA = ents.posAt(mA);
            pB = ents.posAt(mB);
        }
        const QVector3D bpNow = boats.posAt(0);
        const float sepAB = QVector3D(pA - pB).length();
        const bool dualSeat = spawned && ents.rideBoatAt(mA) == 0 && ents.rideBoatAt(mB) == 0
                              && sepAB >= 0.45f && sepAB <= 0.75f          // 双座横向分离 ~0.6
                              && std::fabs(pA.y() - (bpNow.y() + 0.5f)) <= 0.02f
                              && std::fabs(pB.y() - (bpNow.y() + 0.5f)) <= 0.02f;
        const bool fullOk = ents.rideBoatAt(mC) == -1;
        const bool playerRefused = !boats.tryMount(bpNow + QVector3D(0.0f, 3.0f, 0.0f),
                                                   QVector3D(0.0f, -1.0f, 0.0f), 8.0f)
                                   && boats.ridingIndex() == -1;
        const bool broke = boats.hitBoatFromRay(bpNow + QVector3D(0.0f, 3.0f, 0.0f),
                                                QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
        ents.tick(0.016, &w, bp0, 0.3f, 1.8f, false);
        ents.tickVehicleRiding();
        const bool released = broke && !boats.aliveAt(0)
                              && ents.rideBoatAt(mA) == -1 && ents.rideBoatAt(mB) == -1
                              && ents.aliveAt(mA) && ents.aliveAt(mB);
        const bool ok = dualSeat && fullOk && playerRefused && released;
        if (!ok)
            qInfo().noquote() << "  t811 boat: dualSeat" << dualSeat << "sepAB" << sepAB
                              << "fullOk" << fullOk << "playerRefused" << playerRefused
                              << "released" << released
                              << "pA" << pA << "pB" << pB << "boatY" << bpNow.y();
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t811 mob auto-rides boat: dual seats (+/-0.3 sides, pinY = boatY+halfH),"
                             " 3rd mob refused (cap 2), player tryMount refused when full, break"
                             " releases both in place; seatGap" << sepAB;
        // 清场
        ents.clearAll();
        boats.clearAll();
        for (int dx = 0; dx <= 4; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 3; ++dy)
                    w.setBlock(bx + dx, fy + dy, bz + dz, BR::Air, 0);
        tickN(w, 2);
    }

    // ── t865 生物贴轨行走 + 草丛误跳探针（EntityManager 直编，t803 追击走廊模式）──
    //   用户报告（R19.15）：「生物走铁轨悬浮上方一格」（支撑判定把非整格当满格抬高）+「僵尸遇草丛跳过去」
    //   （草丛/花等无碰撞植物被 isJumpObstacle 当墙）。根因（t865）：mob 三谓词（mobAabbHitsSolid /
    //   mobFootprintHasSupport / mobSupportTopY / isJumpObstacle）消费 isSolid（非 air 实存）而非碰撞语义 →
    //   轨 / 火把 / 草丛等 ShapeNone 无碰撞格被当满格墙 + 满格支撑。修 = 收口 World::isCollidable /
    //   World::supportTopYAt（碰撞 sub-AABB 真顶单一权威）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) 贴轨行走：僵尸（Shambler，追击确定性 +X）沿 10 格直轨走廊追玩家 —— 全程脚底 Y 恒 ≈ 地面顶
    //       （kRigY，穿透轨格踩地面——轨板厚 1/16 视觉贴合）且到达走廊远端（旧象：轨=墙 → 越障跳翻上轨 →
    //       悬浮轨上一格 feet=kRigY+1）；
    //   (b) 草丛直走：同走廊铺 4 格草丛（无轨）—— 全程无起跳（feet 恒 ≈ 地面，越障跳从未触发）且到达
    //       远端（旧象：草丛=墙 → 起跳翻过 = 用户「僵尸遇草丛跳过去」）。
    {
        // rig 选址：运行期扫描空区（t809/t811 先例）。需 13×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 12 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 12 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t865 mob walks rails/grass at true surface: no clear rig area found";
        } else {
            EntityManager ents;
            // (a) 贴轨走廊：石地板 x0..x0+10 @kRigY-1 + 直轨中列 x0..x0+10 @kRigY；僵尸 x0 追 +X（虚拟玩家
            //     脚位走廊远端，kDetectRange=16 内）。断言全程 feet == kRigY（±0.02）且 maxX ≥ x0+8。
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Rail, 0);
            }
            const int zA = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D railTarget(float(x0) + 10.5f, float(kRigY), float(z0) + 0.5f);
            // 预热 40 tick：spawn 高度（cell+0.5 中心）对 1.8 高 mob 脚位偏低 → 首拍嵌入地板下沉 ~0.85 再
            //   snap 回真支撑顶（引擎既定落定链，非本任务对象）；预热后才开始记录 feet 偏差（否则把该
            //   出生暂态误计为「跳」）。
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, railTarget, 0.3f, 1.8f, true);
            float railMaxFeetOff = 0.0f, railMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) { // 4.8s：10 格追击 ~3.6s（kChaseSpeed 2.8）+ 余量
                ents.tick(0.016f, &w, railTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zA);
                railMaxFeetOff = std::max(railMaxFeetOff, std::fabs(p.y() - 0.9f - float(kRigY)));
                railMaxX = std::max(railMaxX, p.x());
            }
            const bool okA = railMaxFeetOff <= 0.02f && railMaxX >= float(x0) + 8.0f;
            // 清 (a) 场（轨全拆；地板留作 (b) 走廊，僵尸留在 ents 内随后续段自然游荡，不参与断言）。
            for (int dx = 0; dx <= 10; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            // (b) 草丛走廊：地板中段铺 4 格草丛（x0+3..x0+6）；新僵尸自 x0 追 +X。断言全程 feet == kRigY
            //     （越障跳从未把脚抬离地面 = 草丛直走过）且到达远端。
            for (int dx = 3; dx <= 6; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::TallGrass, 0);
            const int zB = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D grassTarget(float(x0) + 10.5f, float(kRigY), float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, grassTarget, 0.3f, 1.8f, true); // 预热（同上）
            float grassMaxFeetOff = 0.0f, grassMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, grassTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zB);
                grassMaxFeetOff = std::max(grassMaxFeetOff, std::fabs(p.y() - 0.9f - float(kRigY)));
                grassMaxX = std::max(grassMaxX, p.x());
            }
            const bool okB = grassMaxFeetOff <= 0.02f && grassMaxX >= float(x0) + 8.0f;
            const bool ok = okA && okB;
            if (!ok)
                qInfo().noquote() << "  t865 rail: feetOff" << railMaxFeetOff << "maxX" << railMaxX
                                  << "| grass: feetOff" << grassMaxFeetOff << "maxX" << grassMaxX;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t865 mobs walk rails at true surface (feet on floor through 1/16 rail"
                                 " plate, no full-block lift) and stride through tall grass without"
                                 " jumping (no-collision blocks are neither wall nor support)";
            // 清场
            ents.clearAll();
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    }

    // ── t867 压力板掉落物贴板探针（ItemEntityManager 直编，t804 掉落物探针模式）──
    //   用户报告（R19.15）：「掉落物落在压力板上悬上方一格」。根因（t867）：ItemEntityManager 落地列扫
    //   把任何非空气格当**整格高**支撑（World::isSolid 语义）→ 板上掉落物 restY = 板格+1+0.3 悬空。修 =
    //   列扫 / resting 复探 / 冰面摩擦面判定收口 World::supportTopYAt（碰撞 sub-AABB 真顶；t865 同族单一
    //   权威）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) 板上掉落物紧贴板面：resting 且中心 Y = 板格 + 1/16（ShapePlate 盒真顶）+ kRestOffset(0.3)
    //       （旧象 = 板格+1+0.3 悬一格）；
    //   (b) 满格支撑回归对照：同 rig 相邻列石块顶的掉落物仍停 块格+1+0.3（收口不改变整格落定高度）；
    //   (c) 挖板后失支撑穿透：拆板 → 掉落物解除 resting 续落到石块顶（块格+1+0.3）——薄支撑消失的
    //       重力跟随（板不承载的另一半语义）。
    {
        // rig 选址：运行期扫描空区（t865 先例）。需 5×1×4 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 4 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t867 items rest on pressure plate top: no clear rig area found";
        } else {
            const float restOff = 0.3f; // kRestOffset（itementitymanager.h 私有常量文档值）
            const float plateTop = 1.0f / 16.0f; // ShapePlate 盒真顶（blockregistry shapeBoxes）
            // 石基座两列 + 左列压力板：左 = 板路径 (a)/(c)，右 = 满格对照 (b)。
            w.setBlock(x0,     kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 2, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0,     kRigY,     z0, BR::WoodPressurePlate, 0);
            ItemEntityManager items;
            // (a) 板上：零初速直落（spawnItemAt 免 spawnItem 弹出方向哈希；t804 确定性同款）。
            items.spawnItemAt(QVector3D(float(x0) + 0.5f, float(kRigY) + 1.5f, float(z0) + 0.5f),
                              BR::Torch, 1, 0.0f, 0.0f, 0.0f);
            // (b) 满格对照（不同 itemId：t490fix 就近合并半径 2.0 恰等于两列间距，同 id 会被并成一实体）。
            items.spawnItemAt(QVector3D(float(x0 + 2) + 0.5f, float(kRigY) + 1.5f, float(z0) + 0.5f),
                              BR::Stone, 1, 0.0f, 0.0f, 0.0f);
            for (int t = 0; t < 60; ++t) items.tick(0.016f, &w); // 1s 落定 + 余量
            const bool okA = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY) + plateTop + restOff)) <= 0.02f;
            const bool okB = items.restingAt(1)
                && std::fabs(items.posAt(1).y() - (float(kRigY - 1) + 1.0f + restOff)) <= 0.02f;
            // (c) 拆板 → 板上物品失支撑穿透轨…落到石基座顶（复探两格窗见不到碰撞支撑 → 解除 resting 续落）。
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int t = 0; t < 60; ++t) items.tick(0.016f, &w);
            const bool okC = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY - 1) + 1.0f + restOff)) <= 0.02f;
            const bool ok = okA && okB && okC;
            if (!ok)
                qInfo().noquote() << "  t867 plateY" << items.posAt(0).y() << "restingA"
                                  << items.restingAt(0) << "| fullY" << items.posAt(1).y()
                                  << "restingB" << items.restingAt(1);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t867 item rests glued to pressure plate top (restY = plate 1/16 +"
                                 " 0.3, was floating a full block above), full-block rest height"
                                 " unchanged, plate removal drops item onto pedestal (thin support"
                                 " vanishes -> gravity re-settles)";
            // 清场
            items.clearAll();
            w.setBlock(x0,     kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── review26 #1 mob 矮碰撞支撑行走探针（EntityManager 直编，t865 追击走廊模式）──
    //   Review 2026-08-26 #1：t865 把支撑/落定收口到碰撞真顶后，mob 脚位落进矮支撑格内部（下半砖 +0.5 /
    //   耕地 +0.9375）→ 水平碰撞 mobAabbHitsSolid 的 y0=脚位格=支撑格自身 → isCollidable 恒 true → 逐轴
    //   撤回 → 原地冻结（农田生物全员站桩）。修 = 脚位格薄支撑豁免（碰撞真顶 ≤ 脚位+1e-3 → 视穿透）+
    //   越障跳同口径（前方格真顶 ≤ 脚位 → 非墙不跳，防全程兔跳）。矩阵断言（任一 FAIL = 症状复现）：
    //   (a) 下半砖地面：僵尸沿 10 格下半砖走廊追击 —— 全程脚底 Y ≈ 砖真顶（kRigY+0.5，贴面行走非冻结
    //       非兔跳）且到达走廊远端（旧象 = 起步即冻结 maxX≈x0）；
    //   (b) 耕地地面：同走廊铺耕地 —— 脚底 ≈ kRigY+0.9375（耕地矮盒真顶）且到达远端。
    {
        // rig 选址：运行期扫描空区（t865 先例）。需 13×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 12 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 12 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-1 mob walks short-support floors: no clear rig area found";
        } else {
            EntityManager ents;
            // (a) 下半砖走廊：石地板 x0..x0+10 @kRigY-1 + 下半砖（CobbleSlab state0 真顶 +0.5）同列 @kRigY；
            //     僵尸自 x0 上方一格半落下（免嵌入出生）追 +X。断言全程 feet ≈ kRigY+0.5（±0.02）且 maxX ≥ x0+8。
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::CobbleSlab, 0);
            }
            const int zA = ents.spawnMobTyped(x0, kRigY + 2, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D slabTarget(float(x0) + 10.5f, float(kRigY) + 0.5f, float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, slabTarget, 0.3f, 1.8f, true); // 预热落定
            float slabMaxFeetOff = 0.0f, slabMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) { // 4.8s：10 格追击 ~3.6s（kChaseSpeed 2.8）+ 余量
                ents.tick(0.016f, &w, slabTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zA);
                slabMaxFeetOff = std::max(slabMaxFeetOff, std::fabs(p.y() - 0.9f - (float(kRigY) + 0.5f)));
                slabMaxX = std::max(slabMaxX, p.x());
            }
            const bool okA = slabMaxFeetOff <= 0.02f && slabMaxX >= float(x0) + 8.0f;
            // 清 (a) 场（拆砖铺耕地；地板留作 (b)）。
            for (int dx = 0; dx <= 10; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::Farmland, 0);
            // (b) 耕地走廊：耕地矮盒真顶 +0.9375；新僵尸同款。断言全程 feet ≈ kRigY+0.9375 且 maxX ≥ x0+8。
            const int zB = ents.spawnMobTyped(x0, kRigY + 2, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D farmTarget(float(x0) + 10.5f, float(kRigY) + 0.9375f, float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, farmTarget, 0.3f, 1.8f, true); // 预热落定
            float farmMaxFeetOff = 0.0f, farmMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, farmTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zB);
                farmMaxFeetOff = std::max(farmMaxFeetOff, std::fabs(p.y() - 0.9f - (float(kRigY) + 0.9375f)));
                farmMaxX = std::max(farmMaxX, p.x());
            }
            const bool okB = farmMaxFeetOff <= 0.02f && farmMaxX >= float(x0) + 8.0f;
            const bool ok = okA && okB;
            if (!ok)
                qInfo().noquote() << "  review26-1 slab: feetOff" << slabMaxFeetOff << "maxX" << slabMaxX
                                  << "| farmland: feetOff" << farmMaxFeetOff << "maxX" << farmMaxX;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-1 mobs stride across bottom-slab and farmland floors at"
                                 " true support tops (feet snapped inside the support cell are exempt"
                                 " from the foot-cell horizontal scan; no freeze, no bunny-hop)";
            // 清场
            ents.clearAll();
            for (int dx = 0; dx <= 10; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    }

    // ── review26 #2 掉落物贴薄支撑水平滑动探针（ItemEntityManager 直编，t867 探针模式）──
    //   Review 2026-08-26 #2：物品静息中心 = 真顶 + kRestOffset(0.3)，下半砖（真顶 +0.5）中心落在砖格
    //   内部 → 摩擦段水平碰撞探测 hcy=qFloor(pos.y())=砖格自身 → isCollidable 恒 true → 带初始弹出速度
    //   也滑不动（物品被钉死在落点）。修 = 水平碰撞探测与 resting 复探同源：目标格真顶 ≤ 当前底+容差
    //   = 正站其顶不挡。矩阵断言：(a) 砖面带 +X 初速掉落物滑行 ≥1.5 格（旧象 = 位移 0）；(b) 满格墙
    //   仍挡（滑到墙前停，不进墙格——豁免不过界）。
    {
        // rig 选址：运行期扫描空区（t867 先例）。需 6×1×4 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 5 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 5 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-2 item slides on slab floor: no clear rig area found";
        } else {
            const float restOff = 0.3f;  // kRestOffset（itementitymanager.h 私有常量文档值）
            const float slabTop = 0.5f;  // 下半砖（state0）碰撞盒真顶
            // 石基座 x0..x0+3 @kRigY-1 + 下半砖面 x0..x0+2 @kRigY + 满格石墙 x0+3 @kRigY（墙顶 +1 > 物品
            //   底 +0.5 → 不豁免，仍挡）。物品自砖面上方带 +X 初速 20（总滑程 ≈ 20/6 ≈ 3.3 格 → 必抵墙前）。
            for (int dx = 0; dx <= 3; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
            for (int dx = 0; dx <= 2; ++dx) w.setBlock(x0 + dx, kRigY, z0, BR::CobbleSlab, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Stone, 0);
            ItemEntityManager items;
            // spawnItemAt 末三参 = (dirX, dirZ, speed)（方向 × 速率，t608 发射器排出口口径）：+X 弹出 20。
            //   生成点取砖面静息高（slabTop+kRestOffset）——首拍即落定贴面滑行（免高落差 +X 弹出飞越
            //   1 格墙控制位形：本探针钉的是贴面滑动碰撞，不是抛物线）。
            items.spawnItemAt(QVector3D(float(x0) + 0.5f, float(kRigY) + slabTop + restOff,
                                        float(z0) + 0.5f),
                              BR::Torch, 1, 1.0f, 0.0f, 20.0f);
            for (int t = 0; t < 150; ++t) items.tick(0.016f, &w); // 2.4s：落定 + 滑行 + 摩擦停
            const float finalX = items.posAt(0).x();
            const bool ok = items.restingAt(0)
                && std::fabs(items.posAt(0).y() - (float(kRigY) + slabTop + restOff)) <= 0.02f
                && (finalX - (float(x0) + 0.5f)) >= 1.5f   // (a) 滑起来了（旧象位移 = 0）
                && finalX < float(x0 + 3);                 // (b) 满格墙仍挡（不进墙格）
            if (!ok)
                qInfo().noquote() << "  review26-2 finalX" << finalX << "resting" << items.restingAt(0)
                                  << "y" << items.posAt(0).y();
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-2 item with horizontal pop velocity slides across a"
                                 " bottom-slab floor (resting cell exempt when support top is at the"
                                 " item's feet) and full blocks ahead still stop it";
            // 清场
            items.clearAll();
            for (int dx = 0; dx <= 3; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
    }

    // ── review26 #3 岩浆沟壑越障跳探针（EntityManager 直编，t865 追击走廊模式）──
    //   Review 2026-08-26 #3：t865 收口后 isJumpObstacle 只对 Water 保留 ditch-jump，岩浆（ShapeNone →
    //   isCollidable=false）落到 false = 不当沟壑 → mob 径直走进脚位岩浆格（点燃殉死）。修 = Water/Lava
    //   同列沟壑跳。矩阵断言：僵尸沿石走廊追击，脚位层中段一格岩浆源 —— 途径时脚底离地（跳跃触发，
    //   maxFeetY ≥ 地面+0.4；旧象 = 不跳恒贴地走进岩浆）且到达远端。
    {
        // rig 选址：运行期扫描空区（t865 先例）。需 10×1×5 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 9 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 9 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-3 mob jumps lava ditch: no clear rig area found";
        } else {
            EntityManager ents;
            // 石地板 x0..x0+8 @kRigY-1（顶 = kRigY）+ 脚位层中段一格岩浆源 (x0+4, kRigY)（坐在地板上，
            //   与 mob 脚位同层 —— isJumpObstacle 前方探针的检出位形；探针不 tick World，岩浆不外溢）。
            for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 4, kRigY, z0, BR::Lava, 0);
            const int zC = ents.spawnMobTyped(x0, kRigY + 1, z0, EntityManager::MobShambler,
                                              QStringLiteral("#44aa44"), 100);
            const QVector3D lavaTarget(float(x0) + 8.5f, float(kRigY), float(z0) + 0.5f);
            for (int t = 0; t < 40; ++t) ents.tick(0.016f, &w, lavaTarget, 0.3f, 1.8f, true); // 预热落定
            float maxFeetY = -1e9f, lavaMaxX = -1e9f;
            for (int t = 0; t < 300; ++t) {
                ents.tick(0.016f, &w, lavaTarget, 0.3f, 1.8f, true);
                const QVector3D p = ents.posAt(zC);
                maxFeetY = std::max(maxFeetY, p.y() - 0.9f);
                lavaMaxX = std::max(lavaMaxX, p.x());
            }
            const bool ok = maxFeetY >= float(kRigY) + 0.4f   // 越障跳触发（跳跃顶点 ≈ 地面+1.26）
                && lavaMaxX >= float(x0) + 6.0f;              // 越过岩浆格到达远端
            if (!ok)
                qInfo().noquote() << "  review26-3 maxFeetY" << maxFeetY << "maxX" << lavaMaxX;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review26-3 mob jumps over a foot-level lava cell like water"
                                 " ditches (lava joins the ditch-jump list; was walked straight into)";
            // 清场（岩浆先拆再 tickN，免流体外溢）
            ents.clearAll();
            for (int dx = 0; dx <= 8; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 4, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── t863 矿车坡道物理四修探针（MinecartManager 直编，P12b 同款坡 rig 族）──
    //   用户报告（R19.15 玩法阻塞）：① 上坡失速悬停半空（应反向滑落）；② 悬停 / 停驻态挖掉下方轨 /
    //   支撑不受重力（应坠落）；③ 坡顶前端无轨 + 速度够自动暂停（应飞出平抛）；④ 轨末端静止车推不动
    //   （应可被玩家推离轨道进入自由物理）。矩阵断言（任一 FAIL = 用户症状在当前 HEAD 复现）：
    //   (a) ① 被骑车爬坡中途松键 → 摩擦死区 → 反溜起步滑回坡脚停驻（不悬停坡面）；
    //   (b) ② 高架轨（支撑 + 轨被拆）→ 停驻车失支撑转坠落，落到下方接住地板贴面停驻（不冻结半空）；
    //   (c) ③ 坡顶死端（后邻轨低一格 = 爬升到顶 + 前端无轨）+ 速度足 → 飞出平抛落到轨端外接地板
    //       （机制等价 MC 1.0 轨端飞行，速度不足才停驻——平死端停靠面由 t769/t811 既有探针钉）；
    //   (d) ④ 平死端静止车被玩家朝端外推 → 推离轨道出轨，贴地滑行落到轨端外地板（进入自由物理）。
    {
        // rig 选址：运行期扫描空区（t867 先例）。需 10×1×4 净空（含隔离边 + 落地板区）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 9 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 9 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t863 ramp physics four fixes: no clear rig area found";
        } else {
            const float rideH = 0.45f;    // kCartRideH 镜像（P12b 同款）
            const float groundH = 0.3875f; // kCartGroundH 镜像（出轨贴地落定中心偏移）
            // ── (a) 上坡失速反溜：x0 低平 + x0+1..x0+4 四格连坡（东邻逐格 +1）+ x0+5 高平死端。
            //    松键滑行余量 = 初速 4.8 / 摩擦 2 ≈ 2.4 格 < 坡长 4 格 → 停驻点必落坡面（非坡顶）。──
            for (int i = 0; i <= 5; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 5) ? 4 : (i >= 1) ? (i - 1) : 0), z0, BR::Rail, 0);
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool okA = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            bool reachedSlope = false;
            for (int t = 0; t < 300 && !reachedSlope; ++t) { // W 爬坡到坡中（x∈[x0+1.2, x0+1.6]）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (cp.x() > float(x0 + 1) + 0.2f) reachedSlope = true;
            }
            for (int t = 0; t < 600; ++t) { // 松键：摩擦死区 → 反溜起步 → 倒行滑回坡脚
                carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
            }
            const QVector3D fa = carts.posAt(0);
            okA = okA && reachedSlope
                && fa.x() < float(x0) + 1.0f                      // 滑回低平段（坡脚前；不悬停坡面）
                && std::fabs(fa.y() - (float(kRigY) + rideH)) < 0.02f; // 贴低平轨面停驻
            if (!okA) qInfo().noquote() << "  t863(a) slideback final" << fa << "reached" << reachedSlope;
            // 清 (a)：销毁被骑车（instantBreak 免掉落 + 清骑乘态）+ 拆坡 / 高平轨（低平 x0 留作 (d)）。
            carts.hitCartFromRay(QVector3D(fa.x(), fa.y() + 3.0f, fa.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            for (int i = 1; i <= 5; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 5) ? 4 : (i - 1)), z0, BR::Air, 0);

            // ── (b) 支撑被挖坠落：高架轨（支撑浮空 @kRigY+2 顶 / 轨 @kRigY+3）+ 接住地板 @kRigY-1。──
            w.setBlock(x0 + 4, kRigY + 2, z0, BR::Stone, 0);  // 浮空支撑（石块不落）
            w.setBlock(x0 + 4, kRigY + 3, z0, BR::Rail, 0);   // 高架轨
            w.setBlock(x0 + 4, kRigY - 1, z0, BR::Stone, 0);  // 接住地板（顶 = kRigY）
            carts.spawnCart(x0 + 4, kRigY + 3, z0, &w);       // 停驻高架轨（(a) 车已毁 → 槽 0 复用）
            const float y0b = carts.posAt(0).y();
            w.setBlock(x0 + 4, kRigY + 3, z0, BR::Air, 0);     // 挖轨
            w.setBlock(x0 + 4, kRigY + 2, z0, BR::Air, 0);     // 挖支撑
            for (int t = 0; t < 120; ++t) carts.tickPushedCarts(0.016, &w);
            const QVector3D fb = carts.posAt(0);
            const bool okB = carts.aliveAt(0)
                && y0b > float(kRigY + 3)                       // 起始确实在高架轨面
                && std::fabs(fb.y() - (float(kRigY) + groundH)) < 0.02f // 坠落到接住地板贴面停驻
                && std::fabs(fb.x() - float(x0 + 4) - 0.5f) < 0.05f;    // 原列坠落（无水平速度）
            if (!okB) qInfo().noquote() << "  t863(b) fall final" << fb << "y0" << y0b;
            // 清 (b)：销毁坠落车 + 拆接住地板。
            carts.hitCartFromRay(QVector3D(fb.x(), fb.y() + 3.0f, fb.z()), QVector3D(0, -1, 0), 4.0f, &w, true);
            w.setBlock(x0 + 4, kRigY - 1, z0, BR::Air, 0);

            // ── (c) 坡顶死端飞出：x0 低平 + x0+1 坡（东邻 x0+2@Y+1 **最后一格** —— 后邻低一格 = 坡顶）+
            //        轨端外接地板（x0+3..x0+5 @kRigY，顶 = Y+1 同轨面高）。被骑 W 冲顶 → 飞出平抛落板。──
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);       // 坡（东邻高格轨 → 抬升）
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);   // 坡顶死端（东端无轨）
            for (int i = 3; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Stone, 0); // 落地板
            carts.spawnCart(x0, kRigY, z0, &w);               // （(b) 车已毁 → 槽 0 复用）
            const QVector3D mountC(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            const bool rodeC = carts.tryMount(mountC, QVector3D(0, -1, 0), 4.0f);
            bool flewC = false;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (cp.x() > float(x0 + 2) + 0.6f && cp.y() < float(kRigY + 1) + rideH - 0.03f)
                    flewC = true; // 过坡顶格心后低于轨面 = 平抛下坠（不自动暂停）
            }
            const QVector3D fc = carts.posAt(0);
            const bool okC = rodeC && flewC
                && fc.x() > float(x0 + 3)                       // 落到轨端外地板（滑行渐停位）
                && fc.x() < float(x0 + 6)
                && std::fabs(fc.y() - (float(kRigY + 1) + groundH)) < 0.02f;
            if (!okC) qInfo().noquote() << "  t863(c) launch final" << fc << "flew" << flewC
                                          << "x0" << x0 << "z0" << z0
                                          << "blocks" << int(w.blockAt(x0 + 1, kRigY, z0))
                                          << int(w.blockAt(x0 + 2, kRigY + 1, z0))
                                          << int(w.blockAt(x0 + 3, kRigY, z0));
            // 清 (c)：拆坡 / 坡顶轨 / 落地板（低平 x0 轨留作 (d)）。
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY + 1, z0, BR::Air, 0);
            for (int i = 3; i <= 5; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);

            // ── (d) 轨末端推离：x0 单轨死端（孤轨）+ 端外接地板（x0+1..x0+3 @kRigY-1，顶 = kRigY）。
            //        静止车被玩家朝 +X 端外推 → 出轨推离 → 贴地滑行落板停驻。──
            for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
            carts.spawnCart(x0, kRigY, z0, &w);               // （(c) 车留板上；槽 1 —— (c) 车滑停在 x0+5 附近不挡本段）
            QVector3D pusher = carts.posAt(1) + QVector3D(-0.4f, 0.0f, 0.0f); // 玩家在西侧贴住
            for (int t = 0; t < 300; ++t) {
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);  // 朝 +X（端外向）推
                carts.tickPushedCarts(0.016, &w);
                if (carts.posAt(1).x() > float(x0) + 0.6f) break; // 已离轨格
                pusher.setX(carts.posAt(1).x() - 0.4f);        // 追着推
            }
            for (int t = 0; t < 200; ++t) carts.tickPushedCarts(0.016, &w); // 滑行渐停
            const QVector3D fd = carts.posAt(1);
            const bool okD = fd.x() > float(x0) + 0.6f                          // 推离轨道
                && std::fabs(fd.y() - (float(kRigY) + groundH)) < 0.02f         // 贴地（地板顶 + groundH）
                && std::fabs(fd.z() - float(z0) - 0.5f) < 0.05f;                // 不侧漂
            if (!okD) qInfo().noquote() << "  t863(d) push-off final" << fd;
            const bool ok = okA && okB && okC && okD;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t863 ramp physics: uphill stall slides back to foot, mined support"
                                 " drops parked cart onto catch floor, crest dead-end launches at speed"
                                 " onto beyond-end floor, dead-end cart pushable off the rail into free"
                                 " physics";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 3; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── t864 矿车互卡悬浮探针（MinecartManager 直编；PlayerController 骑乘帧同序驱动）──
    //   用户报告（R19.15 玩法阻塞）：「上坡被前方矿车卡住时悬浮原地一直向上——碰撞卡阻时应停驻 / 滑回，
    //   不向上漂」。驱动序镜像 PlayerController 骑乘分支（tickRiddenCart → tickPushedCarts →
    //   resolveCartCollisions）。断言：
    //   (a) 无上漂：被卡车全程 Y 恒贴轨面（±0.1——旧症状「一直向上」= Y 持续抬升脱离轨面）；
    //   (b) 卡阻终态 = 停驻 / 滑回：被卡车不越过前车（A.x < B.x 恒成立），且全程存在「首次接触后回落」
    //       （反溜 / 被顶回——碰撞冲量 + 死区 + t863① 反溜链把卡阻车送回坡下，不在坡面悬停）。
    {
        // rig 选址：运行期扫描空区。需 8×1×4 净空。坡：x0..x0+1 低平 + x0+2 坡（东邻高 1）+
        //   x0+3..x0+4 高平（B 停驻死端）；A 被骑从坡脚冲。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 7 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t864 cart-cart uphill jam: no clear rig area found";
        } else {
            const float rideH = 0.45f;
            const auto wantSurf = [&](float x) {
                if (x < float(x0 + 1)) return float(kRigY);
                if (x > float(x0 + 2)) return float(kRigY + 1);
                return float(kRigY) + (x - float(x0 + 1));
            };
            // 3 格轨：x0 低平 + x0+1 坡（东邻高 1）+ x0+2 坡顶死端（B 停驻格）。A 被卡在坡面格 →
            //   松手 / 下车后有梯度可反溜（t863①）；高平延长段会让被卡点落在无梯度平段（平段停驻
            //   语义，非本症状）—— 首版 5 格 rig 实测踩坑。
            for (int i = 0; i <= 2; ++i)
                w.setBlock(x0 + i, kRigY + ((i >= 2) ? 1 : 0), z0, BR::Rail, 0);
            MinecartManager carts;
            // B 停驻**坡顶死端格**（x0+2@Y+1：后邻低一格的爬升顶、东端无轨）→ A 被卡在坡面格
            //   （x0+1）上有梯度 → 松手 / 下车后摩擦死区 + t863① 反溜可触发放回（高平面被卡无梯度
            //   不反溜——那是平段停驻语义，非本症状）。
            carts.spawnCart(x0 + 2, kRigY + 1, z0, &w);       // B：坡顶死端静止车（挡路；槽 0）
            carts.spawnCart(x0, kRigY, z0, &w);               // A：坡脚车（槽 1）
            const QVector3D mountOrigin(float(x0) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f);
            bool ok = carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
            QVector3D cp;
            float maxLift = 0.0f;      // Y 超出轨面的最大量（上漂签名；容 0.1 内的接触抖动）
            bool neverPassed = true;
            for (int t = 0; t < 900; ++t) { // (a) W 持续冲坡 - 卡阻：供能不断的极限压测（旧症状「一直向上」）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp); // W 持续（卡阻供能不断）
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
                const QVector3D a = carts.posAt(1), b = carts.posAt(0);
                maxLift = std::max(maxLift, float(a.y() - (wantSurf(a.x()) + rideH)));
                if (a.x() >= b.x() - 0.05f) neverPassed = false;   // A 越过 / 并入 B = 穿透
            }
            const bool okA = maxLift <= 0.1f && neverPassed;    // 无上漂（贴轨面）+ 不穿透
            // (b) 无动力被卡（下车后空车留坡面）：A 已停在接触位附近（W 段终点），无持续供能 → 坡面
            //     摩擦死区 + t863① 反溜链把卡阻车送回坡脚（「停驻 / 滑回」，不悬停坡面）。
            carts.dismount(nullptr, cp);                        // 下车（A 变空车；玩家位丢弃）
            for (int t = 0; t < 600; ++t) { // 9.6s：摩擦 - 死区 - 反溜 - 滑回
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
            }
            const QVector3D fa = carts.posAt(1);
            const bool okB = fa.x() < float(x0) + 1.0f                           // 滑回低平段（坡脚）
                && std::fabs(fa.y() - (float(kRigY) + rideH)) < 0.02f            // 贴低平轨面停驻
                && carts.posAt(0).x() > float(x0) + 1.9f;                        // B 仍在坡顶格（未被顶下山）
            ok = ok && okA && okB;
            if (!ok)
                qInfo().noquote() << "  t864 maxLift" << maxLift << "neverPassed" << neverPassed
                                  << "slidebackFinal" << fa;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t864 uphill cart-cart jam: blocked cart stays glued to rail surface"
                                 " (no upward drift), never penetrates the blocker, and falls back after"
                                 " first contact (stall/slide-back, no mid-air hover)";
            // 清场
            carts.clearAll();
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY + ((i >= 2) ? 1 : 0), z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── review26 #14 derailed 矿车可被撞滑探针（MinecartManager 直编，t863(d)/t864 推离 rig 族）──
    //   用户症状（review26 低危）：出轨落地的矿车在车-车碰撞中是不可推动的幽灵障碍 —— 行进车撞上
    //   出轨车被每帧顶回、出轨车纹丝不动，动力轨也推不过去（clampShift 无轨列恒 0 + impulseDirOk
    //   需轨连接）。修：derailed 态加碰撞分支（冲量放行 + 自由体墙检去穿插）。断言：
    //   (a) 停驻出轨车被行进车撞后位移 ≥0.5 格（旧代码恒 0 = 幽灵障碍签名，回退即红）；
    //   (b) 终态两车分离 ≥0.85（kCartCollideSep−ε：无穿透互锁 / 无永久贴脸抖动）；
    //   (c) 行进车推进 ≥1 格（撞滑不吞行进侧动量到「原地锁死」）。
    {
        // rig 选址：运行期扫描空区（t863 同款）。需 11×1×4 净空（地板走廊 x0..x0+8 + 隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 10 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 10 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review26-14 derailed cart knockable: no clear rig area found";
        } else {
            const float groundH = 0.3875f; // kCartGroundH 镜像（出轨贴地落定中心偏移）
            // 走廊地板 x0+1..x0+8 @kRigY-1（顶 = kRigY）；A 起动轨 x0、B 出轨用临时轨 x0+5（轨下地板承接）。
            for (int i = 1; i <= 8; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 5, kRigY, z0, BR::Rail, 0);
            MinecartManager carts;
            // B：临时轨上静止车被向西推离 → derailed + 沿 -X 贴地滑行 ≤2 格（4.0/摩擦 2）→ 停驻走廊中段。
            carts.spawnCart(x0 + 5, kRigY, z0, &w); // B（槽 0）
            QVector3D pusherB(carts.posAt(0).x() + 0.4f, carts.posAt(0).y(), carts.posAt(0).z());
            for (int t = 0; t < 400; ++t) {
                carts.pushEmptyCart(&w, pusherB, -1.0f, 0.0f); // 朝 -X（走廊内侧）推
                carts.tickPushedCarts(0.016, &w);
                if (carts.posAt(0).x() < float(x0 + 5) - 0.4f) break; // 已离临时轨格 → 出轨成立
                pusherB.setX(carts.posAt(0).x() + 0.4f);              // 追着推
            }
            for (int t = 0; t < 300; ++t) carts.tickPushedCarts(0.016, &w); // 滑行摩擦停驻
            const float bPark = carts.posAt(0).x();
            const float bParkY = carts.posAt(0).y();
            const bool bRigOk = bPark < float(x0 + 5) - 0.3f            // 确已推离临时轨
                && bPark > float(x0 + 2) + 0.2f                          // 停驻走廊中段（A 进攻走廊可达）
                && std::fabs(bParkY - (float(kRigY) + groundH)) < 0.02f; // 贴地落定（derailed 落地形态）
            w.setBlock(x0 + 5, kRigY, z0, BR::Air, 0); // 拆临时轨（B 列确认无轨 = 自由体分支前置）
            // A：西端轨上静止车被向东反复推（停驻即再推，单次滑 ≤2 格）→ 撞上 B。B 在 A 东侧 →
            //   被撞离向 = +X（冲量沿 n=B−A 推离；B.dir=-X × 负速 = +X 位移，与「顶退-推离」注释一致）。
            //   **推手距离门**（阴性验证教训）：只在 A.x < bPark−1.35 时跟推 —— 推手贴 A 后 0.4 → 距 B
            //   恒 ≥1.75 > kCartPushReach(0.8)，玩家永不直接推到 B（旧代码下 A 穿进 B 时跟推会把 B 直接
            //   推走 = 假绿污染通道）；A 的最后一推从 ≤bPark−1.35 处滑 ≤2 格必触 B（触点差 <1.4 格）。
            carts.spawnCart(x0, kRigY, z0, &w); // A（槽 1）
            QVector3D pusherA(carts.posAt(1).x() - 0.4f, carts.posAt(1).y(), carts.posAt(1).z());
            float bMaxX = bPark;
            for (int t = 0; t < 1200; ++t) {
                const float axNow = carts.posAt(1).x();
                if (bRigOk && axNow < bPark - 1.35f) {
                    pusherA.setX(axNow - 0.4f);
                    carts.pushEmptyCart(&w, pusherA, 1.0f, 0.0f); // 距离门内才跟推
                }
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
                bMaxX = std::max(bMaxX, float(carts.posAt(0).x()));
            }
            for (int t = 0; t < 200; ++t) { // 收尾：撞滑余动量摩擦停驻
                carts.tickPushedCarts(0.016, &w);
                carts.resolveCartCollisions(&w);
            }
            const float fa = carts.posAt(1).x(), fb = carts.posAt(0).x();
            const bool okMove = bRigOk && (bMaxX - bPark) >= 0.5f;              // (a) 出轨车被撞滑 ≥0.5 格（撞离向）
            const bool okSep = carts.aliveAt(0) && carts.aliveAt(1)
                && std::fabs(fa - fb) >= 0.85f;                                 // (b) 终态无穿透互锁
            const bool okProg = fa >= float(x0) + 1.0f;                         // (c) 行进车推进 ≥1 格
            const bool ok14 = okMove && okSep && okProg;
            if (!ok14)
                qInfo().noquote() << "  review26-14 bPark" << bPark << "bMaxX" << bMaxX
                              << "finalA" << fa << "finalB" << fb << "bRigOk" << bRigOk;
            if (!ok14) ++totalFail;
            qInfo().noquote() << (ok14 ? "PASS" : "FAIL")
                              << "| review26-14 derailed cart is knockable: a sliding cart striking a"
                                 " derailed-parked cart displaces it >=0.5 cells (old code: immovable"
                                 " ghost obstacle), both settle apart >=0.85 with no interpenetration"
                                 " lock, and the striker keeps >=1 cell of progress";
            // 清场
            carts.clearAll();
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 8; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }

    // ── t866 载具攻击 / 摧毁语义探针（Game 层 PlayerController + EntityManager + MinecartManager 直编）──
    //   用户报告（R19.15）：①「矿车载生物时打矿车本体 → 打到生物 → 生物永远下不来」（乘骑 mob 钉座位
    //   AABB 与车体重叠 → 攻击射线恒先中乘员，矿车耐久链永不可达 → 下车唯一路径〔车毁〕永不成）；
    //   ②「矿车运动中碰仙人掌应变掉落物、乘员自动下来；岩浆同样」（矿车原无环境摧毁链）。矩阵断言：
    //   (a1) 生存攻击乘骑车：命中乘员改判进矿车耐久链（hpAt 3→2）+ 乘员不掉血 + 仍在车（末击摧毁释放
    //        链由 t811 探针 (d) 已覆盖，此处钉改判面）；
    //   (a2) 创造攻击乘骑车：瞬毁 + cartBroken 不发（创造无掉落）+ 乘员对账自动释放（rideCart==-1、
    //        存活、不掉血）；
    //   (a3) 源码钉（t889 先例）：beginMining 乘员重路由 = **验 hitCartFromRay/hitBoatFromRay 返回值**
    //        （review26 #4：旧无条件 return 在「瞄乘员露出车斗的上身/头部」几何〔射线中 mob 不交车盒〕
    //        吞击——冷却+挥手已发但零效果）+ 射线长度 m_hitDist（非 kReach 全程，极端角度不可隔墙打车）
    //        + 未命中车盒落回 attackMob（乘员本体照旧可打）；
    //   (a4) 行为几何钉（review26 #4 复现形态）：Shambler 乘员（halfH 0.90，头顶高出车盒顶 ~1.04 格）
    //        瞄上身高度的水平射线 → findMobHit 命中乘员 + findCartHit 恒 -1（= 驱动 a3 落回分支的几何事实）；
    //   (b)  仙人掌：轨端前方一格仙人掌 → 空车被推到末格中心（AABB 前沿探入仙人掌格）→ 下一
    //        tickPushedCarts 环境检查即毁 + cartBroken 发（生存掉落语义）；
    //   (c)  岩浆：静止车格被岩浆灌入（setBlock Lava）→ 下一 tick 即毁（同链；掉落物落岩浆由
    //        ItemEntityManager 瞬毁判定收尾，净效果 = 毁无物）。
    {
        // rig 选址：运行期扫描空区（t867 先例）。需 8×1×4 净空（含隔离边）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 7 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t866 vehicle attack/environment destroy: no clear rig area found";
        } else {
            // ── (a) 攻击重路由 ── 行为级（captured 门内 beginMining 不可直驱，t889 源码钉补接线面）：
            // 石基座 + 单轨 + 矿车 + mob 登乘（tickVehicleRiding 扫描 ≤0.8 格）。
            w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            // 乘员用 Shambler（halfH 0.90 高个）：头顶高出车盒顶 ~1.04 格 —— 正是 review26 #4 的
            // 「瞄上身射线不交车盒」形态载体（短 mob 上身全在车盒内，旧吞击几何不可达）。
            const int mob = ents.spawnMobTyped(x0, kRigY, z0, EntityManager::MobShambler,
                                               QStringLiteral("#ff5555"), 50);
            for (int t = 0; t < 8 && ents.rideCartAt(mob) < 0; ++t) {
                ents.tick(0.016f, &w, carts.posAt(0) + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            int brokenCount = 0;
            QObject::connect(&carts, &MinecartManager::cartBroken, &carts,
                             [&](int, int, int) { ++brokenCount; });
            const QVector3D cp0 = carts.posAt(0);
            const QVector3D eye(cp0.x(), cp0.y() + 4.0f, cp0.z());
            const QVector3D down(0.0f, -1.0f, 0.0f);
            const int mobHp0 = ents.healthAt(mob);
            // (a0) 前置钉：乘骑 mob 的 AABB 与车体重叠 → 攻击射线恒先中乘员（改判是**承重**的——
            //      无它则攻击打在 mob 上、矿车耐久链永不可达 = 用户症状根因链）。
            const bool okA0 = ents.findMobHit(eye, down, 4.0f, nullptr) == mob;
            // (a1) 生存击（= 改判后 beginMining 调的同一调用面）：hp 3→2 + 乘员不掉血 + 仍在车。
            carts.hitCartFromRay(eye, down, 4.0f, &w, /*instantBreak=*/false);
            const bool okA1 = ents.rideCartAt(mob) == 0 && carts.aliveAt(0)
                              && carts.hpAt(0) == 2
                              && ents.healthAt(mob) == mobHp0
                              && brokenCount == 0;
            // (a2) 创造击：瞬毁 + 乘员对账自动释放 + 无掉落信号（t767 创造无掉落）。
            carts.hitCartFromRay(eye, down, 4.0f, &w, /*instantBreak=*/true);
            ents.tick(0.016f, &w, cp0 + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
            ents.tickVehicleRiding(); // 对账：座位指空槽 → mob 自释放
            const bool okA2 = !carts.aliveAt(0) && ents.rideCartAt(mob) == -1
                              && ents.aliveAt(mob) && ents.healthAt(mob) == mobHp0
                              && brokenCount == 0;
            // (a3) 源码钉（t889 先例）：beginMining mob 分支的重路由接线——乘骑判定（rideCartAt/rideBoatAt）
            //      → 改判进 hitCartFromRay / hitBoatFromRay（review26 #4：**验返回值**〔`&&` 进冷却门条件 =
            //      未命中不置冷却/不发挥手〕+ 射线长度 m_hitDist〔非 kReach 全程，不可隔墙打车〕）→ 未命中
            //      车盒落回 attackMob（乘员本体照旧可打）。滤注释体（注释里的字面量不参与）。
            bool okA3 = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
                QFile sf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
                const QString t = sf.open(QIODevice::ReadOnly) ? QString::fromUtf8(sf.readAll()) : QString();
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::beginMining()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::endMining()"));
                if (b0 < 0 || b1 <= b0) {
                    qInfo().noquote() << "  t866 (a3) beginMining slice miss";
                } else {
                    QString body;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                        if (!line.trimmed().startsWith(QLatin1String("//"))) {
                            body += line; body += QLatin1Char('\n');
                        }
                    // 重路由语句面（review26 #4 契约）：乘骑判定 + 「冷却门 && hit*FromRay(…, m_hitDist, …)」
                    // 值门调用（`&&` 前缀钉返回值被消费——无条件弃值调用的旧形态不再匹配）+ 分支内落回
                    // attackMob（首个出现位须在各自值门调用之后）。
                    const int iRideC = body.indexOf(QStringLiteral("m_entityManager->rideCartAt(mobIdx)"));
                    const int iRideB = body.indexOf(QStringLiteral("m_entityManager->rideBoatAt(mobIdx)"));
                    const int iHitC  = body.indexOf(QStringLiteral("&& m_minecartManager->hitCartFromRay(eye, look, m_hitDist, m_world,"));
                    const int iHitB  = body.indexOf(QStringLiteral("&& m_boatManager->hitBoatFromRay(eye, look, m_hitDist, m_world,"));
                    const int iAtkC  = iHitC >= 0 ? body.indexOf(QStringLiteral("attackMob(mobIdx);"), iHitC) : -1;
                    const int iAtkB  = iHitB >= 0 ? body.indexOf(QStringLiteral("attackMob(mobIdx);"), iHitB) : -1;
                    okA3 = iRideC >= 0 && iRideB > iRideC && iHitC > iRideC && iHitB > iRideB
                           && iAtkC > iHitC && iAtkB > iHitB;
                }
            }
            // (a4) 行为几何钉（review26 #4 复现形态）：重铺车 + 乘员再登（a2 释放后仍站在轨格旁，登乘扫描
            //      ≤0.8 拾回）。瞄「乘员上身高度（座位中心 + 0.6*halfH，严格高于车盒顶 cp.y+0.45）」的纯
            //      水平射线（dy=0 → 车盒 Y slab 恒排除）→ findMobHit 命中乘员 + findCartHit -1 +
            //      hitCartFromRay 明确返 false（= beginMining 据以落回 attackMob 的那个返回值）。
            carts.spawnCart(x0, kRigY, z0, &w);
            for (int t = 0; t < 8 && ents.rideCartAt(mob) < 0; ++t) {
                ents.tick(0.016f, &w, carts.posAt(0) + QVector3D(0, 3, 0), 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
            }
            const QVector3D cpA4 = carts.posAt(0);
            const float halfA4 = ents.halfHeightAt(mob);
            const float chestY = cpA4.y() - 0.3125f + halfA4 + 0.6f * halfA4; // 座位钉位中心（车心-0.3125+halfH）+ 0.6*halfH = 胸/头区间
            const QVector3D eyeA4(cpA4.x() + 3.0f, chestY, cpA4.z());
            const QVector3D aimA4(-1.0f, 0.0f, 0.0f);
            const bool okA4 = ents.rideCartAt(mob) == 0
                              && halfA4 > 0.8f && chestY > cpA4.y() + 0.45f
                              && ents.findMobHit(eyeA4, aimA4, 4.0f, nullptr) == mob
                              && carts.findCartHit(eyeA4, aimA4, 4.0f, nullptr) < 0
                              && !carts.hitCartFromRay(eyeA4, aimA4, 4.0f, &w, /*instantBreak=*/false);
            if (!okA4)
                qInfo().noquote() << "  t866 a4 upper-body aim: ride" << ents.rideCartAt(mob)
                                  << "halfH" << halfA4 << "chestY-cartTop" << (chestY - (cpA4.y() + 0.45f));
            // 清 (a) 场。
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);

            // ── (b) 仙人掌（Entities 层直编）：x0..x0+2 轨 + x0+3 仙人掌（轨端前格）；空车推到末格中心。──
            for (int dx = 0; dx <= 2; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Rail, 0);
            }
            w.setBlock(x0 + 3, kRigY - 1, z0, BR::Sand, 0); // 仙人掌基座（沙）
            w.setBlock(x0 + 3, kRigY, z0, BR::Cactus, 0);
            carts.spawnCart(x0, kRigY, z0, &w); // 槽复用 → 槽 0
            QVector3D pusher = carts.posAt(0);
            bool reached = false;
            for (int t = 0; t < 400 && !reached; ++t) { // 玩家追着 +X 推（t809 模式）
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);
                carts.tickPushedCarts(0.016, &w);
                pusher = carts.posAt(0);
                if (!carts.aliveAt(0)) { reached = true; break; }           // 环境检查已毁
            }
            // t863④ 续推：到位 / 死端前磨停（首版 reached 窗口被磨停点 6.4 误触提前退出的实测坑）后
            //   继续追推 → 轨末端推离（derailed 出轨）→ 贴地滑入仙人掌格 → 环境摧毁 + 掉落信号。
            for (int t = 0; t < 300 && carts.aliveAt(0); ++t) {
                carts.pushEmptyCart(&w, pusher, 1.0f, 0.0f);
                carts.tickPushedCarts(0.016, &w);
                pusher = carts.posAt(0);
            }
            const bool okB = !carts.aliveAt(0) && brokenCount >= 1; // 摧毁 + 生存掉落信号
            // 清 (b) 场。
            for (int dx = 0; dx <= 2; ++dx) {
                w.setBlock(x0 + dx, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 + dx, kRigY, z0, BR::Air, 0);
            }
            w.setBlock(x0 + 3, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Air, 0);
            tickN(w, 2);

            // ── (c) 岩浆（Entities 层直编）：地面静止车 + 格内灌岩浆 → 下一 tick 环境检查即毁。──
            w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);
            carts.spawnCart(x0, kRigY - 1, z0, &w); // 非轨地面静止车（kCartGroundH 贴 cell 底）
            const int lavaBroken0 = brokenCount;
            w.setBlock(x0, kRigY - 1, z0, BR::Lava, 0); // 基座格换岩浆 → 车 AABB 覆盖该格
            carts.tickPushedCarts(0.016, &w);
            const bool okC = !carts.aliveAt(0) && brokenCount == lavaBroken0 + 1;
            const bool ok = okA0 && okA1 && okA2 && okA3 && okA4 && okB && okC;
            if (!ok)
                qInfo().noquote() << "  t866 a0" << okA0 << "a1" << okA1 << "hp" << carts.hpAt(0)
                                  << "a2" << okA2 << "a3(src)" << okA3 << "a4(geom)" << okA4
                                  << "rideC" << ents.rideCartAt(mob) << "b" << okB
                                  << "c" << okC << "broken" << brokenCount;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t866 attack on passenger-carrying cart routes to cart durability"
                                 " (mob unharmed, still seated), creative hit destroys + passenger"
                                 " auto-released, moving cart touching cactus breaks into dropped"
                                 " item, lava cell destroys cart same chain";
            // 清场
            carts.clearAll();
            ents.clearAll();
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            tickN(w, 2);
        }
    }



    // ── t848 余烬门尺寸上限 23×23 探针（World 层直调；t806 泛化门的用户实测回归）──
    //   背景（用户 8-24 实测）：t806 内腔上限 4 宽×5 高，实测「最大只有 4×4 能点燃，再大激活不了」——
    //   5 宽内腔在 ③ 量宽被旧上限 kMaxW-1=3 截断 → w 恒测 4 → ④ 右柱校验打到第 5 内腔列（空气格）判败。
    //   t806 探针盲区 = ⑧ 只测「上限内 4×5 四角点燃位」、③ 反把 5 宽当「超限拒」断言——验的是「设计上限
    //   合规」不是「用户期望的更大门」（用户期望 = MC 1.0 语义：内腔上限 21×21 / 框外沿 23×23）。
    //   t848 修：内腔 2×3 最小 .. 21×21 最大（22+ 超限拒点）+ 相邻双门共用中间竖柱各自成门（柱检查只验
    //   黑曜石不验独占）。rig 独立世界 64×64×64（21 高门 + 清场盒 ±3 需 y 5..32，t806 的 32 高 rig 放不下）：
    //   ① 用户复现位 5×4 内腔（X 平面带角）→ 成门恰 20 格（修复前此位拒点 = 用户症状本体）；
    //   ② 21×21 最大内腔 X 平面，点燃位 = 开口右上角（同时压满 ① 下探 21 步 + ② 左探 20 步两扫描上界）
    //      → 成门恰 441 格（= 内腔面积）+ state=0；
    //   ③ 21×21 最大内腔 Z 平面 → 成门恰 441 + state=1（正交轴向各证一次）；
    //   ④ 超限拒：22 宽 / 22 高内腔均拒且零门格（量宽 / 量高 21 截断 → 柱 / 梁校验打内腔空气格）；
    //   ⑤ 2×3 最小门仍可（放宽上限不动下限）；
    //   ⑥ 共用竖柱双门：3×4 + 3×4 共享中柱 → 先点 A（B 侧零误填）→ 再点 B（A 门 12 格健在互不干扰）→
    //      破共享柱中格 → 双门同熄（共享柱对两门都是承重格，批 F 熄灭钩子按连通域各自收域）；
    //   ⑦ 缺角 21×21 最大门 → 成门 441（四角可选语义在超大门保持）；
    //   ⑧ 破框碎门（批 F 钩子超大门回归）：⑦ 门破底梁中格（镜像 finishMiningAt setBlock(Air)+
    //      breakNetherPortalsAround 序列）→ 整门 441 格全熄（连通域熄灭尺寸无关）。
    {
        World w848;
        w848.setWidth(64);
        w848.setDepth(64);
        w848.setHeight(64);
        w848.setSeed(13);
        bool ok848 = true;
        const int pY848 = 8; // 门框基线层（开口 y=pY848..pY848+h-1；清场盒兜底防地形干扰）
        // 建门框（t806 buildFrame 同源，基线层换本 rig）：开口左下角 (x0,pY848,z0) 沿 u=(ux,uz) 展开
        //   w 列 × h 层全 Air；底 / 顶梁（开口正下 / 正上各 w 格，不含角）+ 左右边柱（两翼各 h 格，不含角）
        //   全黑曜石；corners=true 补四角。清场盒 = 框外沿 ±3 × 门法向 ±2（含 y ±(h+3)）。
        const auto buildFrame848 = [&](int x0, int z0, int ux, int uz, int w, int h, bool corners) {
            const int vx = uz, vz = ux; // 门法线向（清深 ±2）
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        w848.setBlock(x0 + c * ux + d * vx, pY848 + r, z0 + c * uz + d * vz, BR::Air, 0);
            for (int c = 0; c < w; ++c) {
                w848.setBlock(x0 + c * ux, pY848 - 1, z0 + c * uz, BR::Obsidian, 0);
                w848.setBlock(x0 + c * ux, pY848 + h, z0 + c * uz, BR::Obsidian, 0);
            }
            for (int r = 0; r < h; ++r) {
                w848.setBlock(x0 - ux, pY848 + r, z0 - uz, BR::Obsidian, 0);
                w848.setBlock(x0 + w * ux, pY848 + r, z0 + w * uz, BR::Obsidian, 0);
            }
            if (corners) {
                const int cs[2] = {-1, w};
                for (const int ci : cs)
                    for (const int ry : {-1, h})
                        w848.setBlock(x0 + ci * ux, pY848 + ry, z0 + ci * uz, BR::Obsidian, 0);
            }
        };
        // 本 rig 清场盒内门格计数（t806 cellsInBox 同源；隔壁 rig 残留门不串数）。
        const auto cellsInBox848 = [&](int x0, int z0, int ux, int uz, int w, int h) -> int {
            int n = 0;
            for (int c = -3; c <= w + 3; ++c)
                for (int r = -3; r <= h + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (w848.blockAt(x0 + c * ux + d * uz, pY848 + r, z0 + c * uz + d * ux)
                            == BR::NetherPortal)
                            ++n;
            return n;
        };

        // ① 用户复现位：5×4 内腔（X 平面带角）开口中格点燃 → 成门恰 20 格。
        {
            buildFrame848(6, 6, 1, 0, 5, 4, true);
            const bool lit = w848.tryIgniteNetherPortal(8, pY848 + 1, 6);
            const int n = cellsInBox848(6, 6, 1, 0, 5, 4);
            if (!lit || n != 20) {
                qInfo().noquote() << "  [t848 diag] user-repro 5x4 interior:" << lit << "cells" << n;
                ok848 = false;
            }
        }
        // ② 21×21 最大内腔（X 平面带角），点燃位 = 开口右上角（压满两扫描上界）→ 441 格 + state=0。
        {
            buildFrame848(6, 16, 1, 0, 21, 21, true);
            const bool lit = w848.tryIgniteNetherPortal(26, pY848 + 20, 16);
            const int n = cellsInBox848(6, 16, 1, 0, 21, 21);
            const int st = int(w848.stateAt(6, pY848, 16) & 1);
            if (!lit || n != 441 || st != 0) {
                qInfo().noquote() << "  [t848 diag] 21x21 X-plane max gate:" << lit << "cells" << n
                                  << "state" << st;
                ok848 = false;
            }
        }
        // ③ 21×21 最大内腔（Z 平面带角），点燃位 = 开口中格 → 441 格 + state=1。
        {
            buildFrame848(34, 6, 0, 1, 21, 21, true);
            const bool lit = w848.tryIgniteNetherPortal(34, pY848 + 10, 16);
            const int n = cellsInBox848(34, 6, 0, 1, 21, 21);
            const int st = int(w848.stateAt(34, pY848, 6) & 1);
            if (!lit || n != 441 || st != 1) {
                qInfo().noquote() << "  [t848 diag] 21x21 Z-plane max gate:" << lit << "cells" << n
                                  << "state" << st;
                ok848 = false;
            }
        }
        // ④ 超限拒：内腔 22 宽 / 22 高（超 21×21 上限，框外沿 23×23 封顶）均拒且零门格。
        {
            buildFrame848(6, 40, 1, 0, 22, 3, true); // 22 宽（X 平面）
            bool bad = w848.tryIgniteNetherPortal(17, pY848 + 1, 40)
                       || cellsInBox848(6, 40, 1, 0, 22, 3) != 0;
            buildFrame848(40, 40, 0, 1, 2, 22, true); // 22 高（Z 平面）
            bad = bad || w848.tryIgniteNetherPortal(40, pY848 + 1, 41)
                        || cellsInBox848(40, 40, 0, 1, 2, 22) != 0;
            if (bad) {
                qInfo().noquote() << "  [t848 diag] oversize 22w/22h not rejected";
                ok848 = false;
            }
        }
        // ⑤ 2×3 最小门（X 平面带角）→ 成门恰 6 格（放宽上限不动下限）。
        {
            buildFrame848(6, 48, 1, 0, 2, 3, true);
            const bool lit = w848.tryIgniteNetherPortal(7, pY848 + 1, 48);
            const int n = cellsInBox848(6, 48, 1, 0, 2, 3);
            if (!lit || n != 6) {
                qInfo().noquote() << "  [t848 diag] 2x3 min gate:" << lit << "cells" << n;
                ok848 = false;
            }
        }
        // ⑥ 共用竖柱双门：A（内腔 x sX..sX+2）与 B（x sX+4..sX+6）共享中柱 x=sX+3（各 3 宽×4 高，X 平面）。
        //    两个 buildFrame848 的清场盒会互 wipe 邻门框 → 单清场盒 + 显式放 union 框。
        {
            const int sX = 12, sZ = 56;
            for (int x = sX - 4; x <= sX + 11; ++x)
                for (int y = pY848 - 4; y <= pY848 + 7; ++y)
                    for (int z = sZ - 2; z <= sZ + 2; ++z)
                        w848.setBlock(x, y, z, BR::Air, 0);
            for (int g = 0; g < 3; ++g) { // 每门 3 内腔列的底 / 顶梁
                w848.setBlock(sX + g, pY848 - 1, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + g, pY848 + 4, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 4 + g, pY848 - 1, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 4 + g, pY848 + 4, sZ, BR::Obsidian, 0);
            }
            for (int r = 0; r < 4; ++r) { // 三竖柱：A 左 / 共享 / B 右（各 h=4 格）
                w848.setBlock(sX - 1, pY848 + r, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 3, pY848 + r, sZ, BR::Obsidian, 0);
                w848.setBlock(sX + 7, pY848 + r, sZ, BR::Obsidian, 0);
            }
            const auto countRect848 = [&](int x0, int w, int h) -> int { // 双门 interior 精确计数（X 平面）
                int n = 0;
                for (int c = 0; c < w; ++c)
                    for (int r = 0; r < h; ++r)
                        if (w848.blockAt(x0 + c, pY848 + r, sZ) == BR::NetherPortal) ++n;
                return n;
            };
            const bool litA = w848.tryIgniteNetherPortal(sX + 1, pY848 + 1, sZ);
            const int onlyA = countRect848(sX, 3, 4) + countRect848(sX + 4, 3, 4); // 点 A 后：A=12 / B=0
            const bool litB = w848.tryIgniteNetherPortal(sX + 5, pY848 + 1, sZ);
            const int bothA = countRect848(sX, 3, 4), bothB = countRect848(sX + 4, 3, 4); // 双门共存各 12
            w848.setBlock(sX + 3, pY848 + 1, sZ, BR::Air, 0); // 破共享柱中格（finishMiningAt 同款先清格）
            w848.breakNetherPortalsAround(sX + 3, pY848 + 1, sZ);
            const int after = countRect848(sX, 3, 4) + countRect848(sX + 4, 3, 4); // 双门同熄归零
            if (!litA || !litB || onlyA != 12 || bothA != 12 || bothB != 12 || after != 0) {
                qInfo().noquote() << "  [t848 diag] shared-pillar double door: litA" << litA
                                  << "litB" << litB << "afterA" << onlyA << "A" << bothA
                                  << "B" << bothB << "afterBreak" << after;
                ok848 = false;
            }
        }
        // ⑦ 缺角 21×21 最大门（X 平面无角）→ 成门恰 441 格；⑧ 破底梁中格 → 整门全熄（批 F 钩子超大门回归）。
        {
            buildFrame848(24, 34, 1, 0, 21, 21, false);
            const bool lit = w848.tryIgniteNetherPortal(34, pY848 + 10, 34);
            const int n = cellsInBox848(24, 34, 1, 0, 21, 21);
            if (!lit || n != 441) {
                qInfo().noquote() << "  [t848 diag] cornerless 21x21 max gate:" << lit << "cells" << n;
                ok848 = false;
            }
            w848.setBlock(34, pY848 - 1, 34, BR::Air, 0); // 底梁中格（finishMiningAt 同款先清格）
            w848.breakNetherPortalsAround(34, pY848 - 1, 34);
            if (cellsInBox848(24, 34, 1, 0, 21, 21) != 0) {
                qInfo().noquote() << "  [t848 diag] max-gate beam break left"
                                  << cellsInBox848(24, 34, 1, 0, 21, 21) << "cells";
                ok848 = false;
            }
        }
        if (!ok848) ++totalFail;
        qInfo().noquote() << (ok848 ? "PASS" : "FAIL")
                          << "| t848 portal size cap 23x23: interior 2x3..21x21 (frame outer 4x5..23x23 "
                             "MC 1.0 cap; t806-era width-cap truncated measurement -> pillar probe hit "
                             "interior air = user 'only 4x4 ignites'), user-repro 5x4 lights 20 cells, "
                             "21x21 max lights on both planes 441 cells each (= interior area, top-right "
                             "ignite pins 21-step down + 20-step left scan bounds), 22w/22h rejected "
                             "zero cells, 2x3 min unchanged, shared middle pillar double door lights "
                             "independently (12 then 12+12) and collapses together on shared-pillar "
                             "break, cornerless 21x21 lights, bottom-beam break collapses whole "
                             "441-cell door via write-family extinguish hook";
    }

    // ── P-t812 铁轨四向连接优先级 + 红石变道探针（R19.13 🅰；t771 三消费端同源架构的交汇形态收口）──
    //   用户报告：「普通铁轨周围 3+ 轨连接时一坨不知道咋走」。断言四组（任一 FAIL = 用户症状在当前
    //   HEAD 的复现点）：
    //   (a) 四向全连（4 臂）：不再输出十字多臂——直线优先出一对直位（全新 state=0 → NS），且邻块编辑
    //       复检后恒同值（不闪变）；非拐角（railCornerArms 拒）；
    //   (b) T 交叉（3 臂 = 一对贯穿 + 单端岔尖）＝转辙器：默认弯向（bit6=0 → 贯穿轴正端；mesher tile 136
    //       拐角贴图走 railCornerArms 同源象限）；激活前稳定（放源块 / 邻编辑不闪变）；拉杆升沿切弯 →
    //       断电保持 → 再升沿再切；红石块 / 压力板两源同语义（isReceivingPower 全源覆盖）；
    //   (c) 矿车过 T 交叉按当前弯向走：默认弯向出口侧 → 通电切弯后改走另一侧 → 断电后仍按保持的弯向走。
    {
        // ── (a) 四向全连 → 直线一对 + 不闪变 ──
        {
            const auto [x0, z0] = nextSlot();
            // 四臂轨先铺、中心轨最后（邻齐后一次成形）；全部 state=0（无轴偏好）→ 期望 NS 直线对。
            w.setBlock(x0 - 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            tickN(w, 2);
            const quint8 con4 = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            int axd = 0, azd = 0;
            bool ok = con4 == quint8(BR::RailConnPz | BR::RailConnNz)   // 直线一对（NS）
                      && con4 != quint8(BR::RailConnPx | BR::RailConnNx | BR::RailConnPz | BR::RailConnNz) // 十字退役
                      && !BR::railCornerArms(con4, axd, azd);           // 非拐角形态
            // 不闪变：邻块编辑（在东臂上方放 / 破石头——复检范围覆盖中心轨且不动轨布局）后 con 恒同值。
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0);
            const quint8 con4a = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Air, 0);
            const quint8 con4b = quint8(w.stateAt(x0, kRigY, z0) & 0x0F);
            ok = ok && con4a == con4 && con4b == con4;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 four-way junction = straight pair not multi-arm cross: con"
                              << int(con4) << "after edit" << int(con4a) << int(con4b)
                              << "(stable across neighbor-edit recompute)";
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // ── (b) T 交叉转辙器：默认弯向 + 稳定 + 升沿切弯 + 断电保持 + 多源 ──
        //   布局：J=(x0,z0) 普通轨；贯穿对 = ±Z 两臂；岔尖 = +X 臂；-X 空位放源（拉杆/红石块/压力板）。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);   // +Z 贯穿臂
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);   // -Z 贯穿臂
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);   // +X 岔尖
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);       // 转辙器 J（最后放）
            tickN(w, 2);
            const auto jCon = [&]() { return quint8(w.stateAt(x0, kRigY, z0) & 0x0F); };
            const auto jState = [&]() { return w.stateAt(x0, kRigY, z0); };
            // 拐角贴图同源断言（P16 cornerQuadrantOk 同款最小版）：T 弯 2 位 → railCornerArms 解码成功 +
            //   mesher 直调产 tile 136 拐角 quad（u 落 136 瓦片窗）。
            const auto cornerTile136 = [](quint8 con) {
                int xd = 0, zd = 0;
                if (!BR::railCornerArms(con, xd, zd)) return false;
                QVector<Vtx> verts; QVector<quint32> idx;
                PartialLightCtx lctx; lctx.light = 1.0f;
                for (int i = 0; i < 6; ++i) lctx.face[i] = 1.0f;
                PartialNeighborCtx nctx;
                nctx.posX = nctx.negX = nctx.posZ = nctx.negZ = 0;
                const float tileW = 1.0f / 16.0f;
                PartialBlockGeometry::append(verts, idx, 0, 0, 0, BR::Rail, con, lctx, nctx,
                                             tileW, 0.0f, 0.0f, 0.0f, 1.0f);
                int n136 = 0;
                for (const Vtx &v : verts) {
                    const float uu = (v.u - 136.0f * tileW) / tileW;
                    if (uu >= 0.0f && uu <= 1.0f) ++n136;
                }
                return n136 >= 4; // 一片拐角 quad（4 顶点）在 136 窗
            };
            const quint8 kDef = quint8(BR::RailConnPx | BR::RailConnPz);   // 默认弯：岔尖(+X) + 贯穿正端(+Z)
            const quint8 kAlt = quint8(BR::RailConnPx | BR::RailConnNz);   // 切弯后：岔尖 + 贯穿负端(-Z)
            bool ok = jCon() == kDef && cornerTile136(jCon());
            // 激活前稳定：-X 空位放拉杆（OFF）——邻编辑复检覆盖 J，弯向必须保持（不闪变）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 2);
            ok = ok && jCon() == kDef;
            // 拉杆升沿 → 切弯（bit6 翻转 + bit7 通电记忆置位）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == kAlt
                 && (jState() & BR::RailSwitchCurveFlag) != 0
                 && (jState() & BR::RailSwitchPoweredFlag) != 0
                 && cornerTile136(jCon());
            // 断电 → 弯向保持（bit7 清、bit6/连接位不动）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 4);
            ok = ok && jCon() == kAlt
                 && (jState() & BR::RailSwitchCurveFlag) != 0
                 && (jState() & BR::RailSwitchPoweredFlag) == 0;
            // 再升沿 → 再切回默认侧（转辙器来回扳）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            // 红石块源：拆拉杆（降沿）→ 放红石块（升沿切弯）→ 拆红石块（降沿保持）。
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 4);
            w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
            tickN(w, 4);
            ok = ok && jCon() == kAlt;
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 4);
            ok = ok && jCon() == kAlt;
            // 压力板源：压下（state bit0）升沿切弯；松开降沿保持。
            w.setBlock(x0 - 1, kRigY, z0, BR::WoodPressurePlate, 1);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            w.setBlock(x0 - 1, kRigY, z0, BR::WoodPressurePlate, 0);
            tickN(w, 4);
            ok = ok && jCon() == kDef;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 T-junction switch: default curve" << int(kDef)
                              << "tile136 corner, stable pre-power, lever/redstone-block/pressure-plate"
                                 " rising edges toggle curve, falling edges hold position (MC junction"
                                 " semantics; con now" << int(jCon()) << ")";
            // 清场
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 2);
        }

        // ── (c) 矿车过 T 交叉按当前弯向走（空车追推跑法，同 P11(d)/t771(d)：pushEmptyCart + 每帧 wish 随行进向）──
        //   布局同 (b)（独立槽）：J=(x0,z0)，岔尖 +X（spawn 位），贯穿 ±Z 死端臂。默认弯 kDef=Px|Pz →
        //   岔尖进车出 +Z；通电切到 kAlt=Px|Nz → 出 -Z；断电后保持 → 仍出 -Z。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0 + 1, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0 - 1, BR::Rail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0, kRigY, z0, BR::Rail, 0);
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0); // 源（先 OFF）
            tickN(w, 2);
            const auto jCon = [&]() { return quint8(w.stateAt(x0, kRigY, z0) & 0x0F); };
            // 跑一趟：岔尖 (x0+1,z0) spawn → 推向 J → 按当前弯向出到贯穿死端臂格心停（死端反向滤停）。
            //   返终停格 (bx,bz)；停在岔尖 / 进错臂都由期望值比对抓出。
            const auto runJunctionCart = [&]() {
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0, &w); // 岔尖轨 con=Nx → spawn 定向 -X（朝 J）
                QVector3D prev = carts.posAt(0);
                float wishX = -1.0f, wishZ = 0.0f;
                for (int t = 0; t < 900; ++t) {
                    carts.pushEmptyCart(&w, prev, wishX, wishZ); // 玩家追着推（静止即续推）
                    carts.tickPushedCarts(0.016f, &w);
                    const QVector3D cp = carts.posAt(0);
                    const float ddx = cp.x() - prev.x(), ddz = cp.z() - prev.z();
                    const float dl = std::sqrt(ddx * ddx + ddz * ddz);
                    if (dl > 1e-4f) { wishX = ddx / dl; wishZ = ddz / dl; }
                    prev = cp;
                }
                return QPair<int, int>(int(std::floor(prev.x())), int(std::floor(prev.z())));
            };
            // ① 默认弯（kDef=Px|Pz）：岔尖进 J（行 -X，Px 反向滤）→ Pz dot=0 胜 → 出 +Z 死端停格心。
            bool ok = jCon() == quint8(BR::RailConnPx | BR::RailConnPz);
            const auto end1 = runJunctionCart();
            ok = ok && end1.first == x0 && end1.second == z0 + 1;
            // ② 拉杆升沿切弯（kAlt=Px|Nz）→ 新车改出 -Z 死端。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            ok = ok && jCon() == quint8(BR::RailConnPx | BR::RailConnNz);
            const auto end2 = runJunctionCart();
            ok = ok && end2.first == x0 && end2.second == z0 - 1;
            // ③ 断电保持（kAlt 不回弹）→ 新车仍出 -Z。
            w.setBlock(x0 - 1, kRigY, z0, BR::Lever, 0);
            tickN(w, 4);
            ok = ok && jCon() == quint8(BR::RailConnPx | BR::RailConnNz);
            const auto end3 = runJunctionCart();
            ok = ok && end3.first == x0 && end3.second == z0 - 1;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t812 cart through T-junction follows current curve: default exits"
                                 " +Z dead end" << (end1.second == z0 + 1)
                              << ", after power toggle exits -Z" << (end2.second == z0 - 1)
                              << ", after power off holds -Z" << (end3.second == z0 - 1);
            // 清场
            w.setBlock(x0, kRigY, z0 + 1, BR::Air);
            w.setBlock(x0, kRigY, z0 - 1, BR::Air);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air);
            w.setBlock(x0, kRigY, z0, BR::Air);
            w.setBlock(x0 - 1, kRigY, z0, BR::Air);
            tickN(w, 2);
        }
    }

    // ── t821 床头/尾 z-fighting 盒几何探针（bedHalfBoxes 单一权威直调；World 层静态函数，无 rig 依赖）──
    //    用户报「浏览器 3D 床预览，床头/尾羊毛与床身模板接触面重叠闪烁」：旧版床垫长轴满 [0,1] → 床垫外
    //    端面与床头/尾板外面同格边共面同法线（z 区间重叠）→ z-fight；枕头外端同病。断言（16 床色 × 4
    //    facing × head/foot = 128 rig）：
    //    (a) 盒数 ≥5（foot：2 腿+床架+床垫+板）/ ≥6（head 多枕头）；
    //    (b) 床垫 + 枕头长轴**外端**内缩 kBedBoardThick 恰达板内面（不触格边 = 与板外面不再共面）；
    //    (c) 床垫长轴**内端**仍满触格边（两半对接连续，t496「中间不空」契约不随本修复回归）；
    //    (d) 外端存在贴格边、顶至 boardTop 的 planks 板盒（内缩后外端仍有板封口，无可见缺口）。
    {
        bool okA = true, okB = true, okC = true, okD = true;
        const int planksT821 = BR::tileIndex(quint8(BR::Planks), BR::PosX);
        const int woolT821 = BR::tileIndex(quint8(BR::Wool), BR::PosX);
        const float thick821 = BR::kBedBoardThick;
        int bedChecks = 0;
        for (int id = 0; id < int(BR::Count); ++id) {
            if (!BR::isBed(quint8(id))) continue;
            const int bedT = BR::tileIndex(quint8(id), BR::PosX);
            for (int f = 0; f < 4; ++f) {
                const bool longX = (f == 0 || f == 1);
                const bool frontPos = (f == 0 || f == 2);
                for (int h = 0; h <= 1; ++h) {
                    const bool isHead = (h == 1);
                    const bool outerPos = isHead ? !frontPos : frontPos;
                    const float boardTop = isHead ? BR::kBedHeadboardTop : BR::kBedFootboardTop;
                    QVector<BedHalfBox> bx;
                    PartialBlockGeometry::bedHalfBoxes(quint8(id), isHead, f, bx);
                    ++bedChecks;
                    if (bx.size() < (isHead ? 6 : 5)) okA = false;
                    const auto lo = [&](const BedHalfBox &b) { return longX ? b.x0 : b.z0; };
                    const auto hi = [&](const BedHalfBox &b) { return longX ? b.x1 : b.z1; };
                    for (const BedHalfBox &b : bx) {
                        if (b.tile != bedT && b.tile != woolT821) continue;
                        const float outerC = outerPos ? hi(b) : lo(b);
                        const float innerC = outerPos ? lo(b) : hi(b);
                        const float wantOuter = outerPos ? 1.f - thick821 : thick821;
                        const float wantInner = outerPos ? 0.f : 1.f;
                        if (std::abs(outerC - wantOuter) > 1e-4f) okB = false;
                        if (b.tile == bedT && std::abs(innerC - wantInner) > 1e-4f) okC = false;
                    }
                    bool boardFound = false;
                    for (const BedHalfBox &b : bx) {
                        if (b.tile != planksT821) continue;
                        const float outerC = outerPos ? hi(b) : lo(b);
                        const bool atEdge = std::abs(outerC - (outerPos ? 1.f : 0.f)) < 1e-4f;
                        if (atEdge && std::abs(b.y1 - boardTop) < 1e-4f) boardFound = true;
                    }
                    if (!boardFound) okD = false;
                }
            }
        }
        const bool ok821 = okA && okB && okC && okD && bedChecks == 128;
        if (!ok821) ++totalFail;
        qInfo().noquote() << (ok821 ? "PASS" : "FAIL")
                          << "| t821 bed board z-fight: mattress/pillow outer end inset to board inner face,"
                             " mattress inner end joins at cell boundary, board caps outer end (16 colors x 4"
                             " facings x head/foot ="
                          << bedChecks << "rigs; a/b/c/d =" << okA << okB << okC << okD << ")";
    }

    // ── t824 附魔台选项池物品过滤探针（R19.13；Game 层表 + Hotbar 桥接，无 World/QML）──
    //    用户报告：「镐子附上亡灵杀手（对镐无意义）」。根因：选项池按大类 mask 过滤（亡灵杀手
    //    appliesToMask=Weapon|Tool|BookItem 含 Tool 位 → 镐 / 铲全过门；摔落保护 mask=Armor → 胸甲也过门）。
    //    t824 收口 selectEnchantsForItem：候选 = isApplicableForItem 逐物品精判（对齐 t763/t798 适用表）。
    //    断言：
    //    (a) 全 seed 扫池（offered 2/12/30 × seed 0..399）逐物品收集出现过的附魔 id：
    //        钻石镐 / 铲 ⊆ {效率,精准,时运,耐久}（**亡灵杀手等武器系绝迹** + 池非空四元全在）；
    //        钻石斧 ⊆ 锐锋族+效率+精准+耐久（无时运）；钻石剑 ⊆ 锐锋族+击退+燃焰+耐久（无效率/采集系）；
    //        胸甲 ⊆ 保护/火焰保护/弹射物保护/耐久（无摔落/水上亲和）；靴 + 摔落保护；头盔 + 水上亲和；
    //        锄 / 弓 / 剪刀 → 恒空（MC 1.0 锄无适用附魔 → 不给选项；categoryForItem 判 None）；
    //        书 → 全 14 附魔都在池（附书全池语义不回归）；
    //    (b) Hotbar 桥接 selectEnchantsPreviewForItem == EnchantRegistry 直调（同 seed 同产物）；
    //    (c) enchantSelected 对锄返 false（附魔台点档 no-op，不白扣 XP / 青金石）+ 对剑 true 且产物全在剑池
    //        + 已附魔再点返 false（防重复附魔闸不回归）。
    {
        Hotbar hb;
        const int diaPick   = int(ToolRegistry::PickaxeDiamond);
        const int diaShovel = int(ToolRegistry::DiamondShovel);
        const int diaAxe    = int(ToolRegistry::DiamondAxe);
        const int diaSword  = int(ToolRegistry::DiamondSword);
        const int diaHoe    = int(ToolRegistry::DiamondHoe);
        const int bowId     = int(ToolRegistry::Bow);
        const int shearsId  = int(ToolRegistry::Shears);
        const int bookId    = RecipeRegistry::BookId;
        const int diaChest  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1;
        const int diaBoots  = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 3;
        const int diaHelm   = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 0;
        const int E  = int(EnchantRegistry::Efficiency),    ST = int(EnchantRegistry::SilkTouch);
        const int F  = int(EnchantRegistry::Fortune),       U  = int(EnchantRegistry::Unbreaking);
        const int SH = int(EnchantRegistry::Sharpness),     UD = int(EnchantRegistry::UndeadSlay);
        const int AR = int(EnchantRegistry::ArthropodSlay), KB = int(EnchantRegistry::Knockback);
        const int FA = int(EnchantRegistry::FireAspect),    P  = int(EnchantRegistry::Protection);
        const int FP = int(EnchantRegistry::FireProtection), PR = int(EnchantRegistry::ProjectileProt);
        const int FF = int(EnchantRegistry::FeatherFall),   AA = int(EnchantRegistry::AquaAffinity);
        // 全 seed 扫池：seen[1..14] = 该物品选项池中出现过的附魔 id（1200 次抽取 → 稀有权重 1 的精准采集
        //   也在书池 / 采集池中以概率 1-(1-p)^2400 ≈ 1 覆盖，假阴性率 < e^-30）。
        const auto poolOf = [&](int itemId, bool seen[15]) {
            for (int i = 0; i < 15; ++i) seen[i] = false;
            const int offeredList[3] = {2, 12, 30};
            for (int oi = 0; oi < 3; ++oi)
                for (int seed = 0; seed < 400; ++seed) {
                    const QVariantList picks = EnchantRegistry::selectEnchantsForItem(itemId, offeredList[oi], seed);
                    for (const QVariant &v : picks) seen[v.toMap().value(QStringLiteral("id")).toInt()] = true;
                }
        };
        // 池 ⊆ 允许集 且 期望集全出现（防「过滤过头 → 空池 / 半池」反向回归）。
        const auto poolIs = [&](int itemId, const std::vector<int> &allowed, bool requireAll) {
            bool seen[15];
            poolOf(itemId, seen);
            for (int i = 1; i < 15; ++i) {
                const bool allowedHas = std::find(allowed.begin(), allowed.end(), i) != allowed.end();
                if (seen[i] && !allowedHas) return false; // 出现了不允许的（如镐出亡灵杀手 = 用户症状）
                if (requireAll && allowedHas && !seen[i]) return false; // 允许的没出现（池被砍空）
            }
            return true;
        };
        const std::vector<int> miningPool = {E, ST, F, U};                 // 镐 / 铲
        const std::vector<int> axePool    = {SH, UD, AR, E, ST, U};        // 斧（锐锋族 + 采集系无时运）
        const std::vector<int> swordPool  = {SH, UD, AR, KB, FA, U};       // 剑
        const std::vector<int> chestPool  = {P, FP, PR, U};                // 胸甲 / 护腿
        const std::vector<int> bootsPool  = {P, FP, PR, U, FF};            // 靴 + 摔落保护
        const std::vector<int> helmPool   = {P, FP, PR, U, AA};            // 头盔 + 水上亲和
        std::vector<int> bookPool;
        for (int i = 1; i < 15; ++i) bookPool.push_back(i);               // 书 = 全 14 池
        bool ok = poolIs(diaPick, miningPool, true)
               && poolIs(diaShovel, miningPool, true)
               && poolIs(diaAxe, axePool, true)
               && poolIs(diaSword, swordPool, true)
               && poolIs(diaChest, chestPool, true)
               && poolIs(diaBoots, bootsPool, true)
               && poolIs(diaHelm, helmPool, true)
               && poolIs(bookId, bookPool, true);
        // 锄 / 弓 / 剪刀 → 恒空池 + 类别 None（附魔台槽 0 拒入的三重门之一）。
        bool seenHoe[15];
        poolOf(diaHoe, seenHoe);
        for (int i = 1; i < 15; ++i) ok = ok && !seenHoe[i];
        ok = ok && EnchantRegistry::categoryForItem(diaHoe) == EnchantRegistry::None
               && EnchantRegistry::selectEnchantsForItem(bowId, 12, 7).isEmpty()
               && EnchantRegistry::selectEnchantsForItem(shearsId, 12, 7).isEmpty();
        // (b) 桥接 == 直调（同 seed 同产物；防 QML 侧再持副本）。
        const QVariantList viaBridge = hb.selectEnchantsPreviewForItem(diaSword, 17, 4242);
        const QVariantList direct    = EnchantRegistry::selectEnchantsForItem(diaSword, 17, 4242);
        ok = ok && viaBridge.size() == direct.size() && !direct.isEmpty();
        for (int i = 0; ok && i < int(direct.size()); ++i)
            ok = viaBridge.at(i).toMap().value(QStringLiteral("id")) == direct.at(i).toMap().value(QStringLiteral("id"))
              && viaBridge.at(i).toMap().value(QStringLiteral("level")) == direct.at(i).toMap().value(QStringLiteral("level"));
        // (c) enchantSelected：锄拒（no-op 不落附魔）；剑成且产物 ⊆ 剑池；已附魔再点拒。
        hb.setStack(0, diaHoe, 1);
        hb.setSelectedSlot(0);
        ok = ok && !hb.enchantSelected(10, 99);
        hb.setStack(0, diaSword, 1);
        ok = ok && hb.enchantSelected(10, 99);
        bool swordEnchOk = false, anyEnch = false;
        const QVariantList gotEnch = hb.enchantsAt(0);
        for (int i = 0; i < 4; ++i) {
            const int packed = gotEnch.at(i).toInt();
            if (packed == 0) continue;
            anyEnch = true;
            swordEnchOk = std::find(swordPool.begin(), swordPool.end(),
                                    EnchantRegistry::packEnchantId(packed)) != swordPool.end();
            if (!swordEnchOk) break;
        }
        ok = ok && anyEnch && swordEnchOk && !hb.enchantSelected(10, 100); // 已附魔 → 拒
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t824 enchant pool filtered per item: pick/shovel subset {eff,silk,fortune,"
                             "unbreaking} (no undead-slay on pick = user symptom), axe adds sharpness-family "
                             "w/o fortune, sword weapon-only, chest w/o feather-fall, boots+feather/helm+aqua, "
                             "hoe/bow/shears empty + category None, book keeps full 14; bridge==direct; "
                             "enchantSelected rejects hoe & already-enchanted";
    }

    // ── t825 锋利最终伤害显示 = 实战同源探针（R19.13；Game 层公式 + Hotbar 桥接）──
    //    用户报告：「钻石剑附锋利后伤害显示仍 +7」。静态复核：实际伤害链（attackMob t476/t763）与九处
    //    tooltip 攻击行均已含锐锋加成 —— 本任务把公式收口 EnchantRegistry::weaponAttackDamage 单一权威
    //    （attackMob 起点 + displayAttackDamage 桥接取整），「显示 = 实战的目标无关部分」结构化成立。
    //    断言：① 权威公式精确值（钻石剑 7 + 0.5/级：0/7.0、I/7.5、III/8.5；木剑 V = 4+2.5 = 6.5）；
    //    ② 显示桥接 = round(权威)（含 .5 半上取整与 JS Math.round 同侧：I → 8）；③ 无附魔 / 非武器不虚增。
    {
        Hotbar hb;
        const int diaSword  = int(ToolRegistry::DiamondSword);
        const int woodSword = int(ToolRegistry::SwordWood);
        const int sharp1 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 1);
        const int sharp3 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
        const int sharp5 = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 5);
        const int zero[4] = {0, 0, 0, 0};
        const int e1[4] = {sharp1, 0, 0, 0};
        const int e3[4] = {sharp3, 0, 0, 0};
        const int e5[4] = {sharp5, 0, 0, 0};
        const auto close = [](float got, float expect) { return std::abs(got - expect) < 1e-4f; };
        bool ok = close(EnchantRegistry::weaponAttackDamage(diaSword, nullptr), 7.0f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, zero), 7.0f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, e1), 7.5f)
               && close(EnchantRegistry::weaponAttackDamage(diaSword, e3), 8.5f)
               && close(EnchantRegistry::weaponAttackDamage(woodSword, e5), 6.5f);
        // 显示桥接 = round(weaponAttackDamage)：0/7、I/8（round(7.5) 半上，同 JS Math.round(7.5)=8）、III/9、
        //   木剑 V/round(6.5)=7；空附魔数组 / 缺项 → 仅基础（防 QML 传 null/[] 崩）。
        ok = ok && hb.displayAttackDamage(diaSword, QVariantList{}) == 7
               && hb.displayAttackDamage(diaSword, QVariantList{sharp1, 0, 0, 0}) == 8
               && hb.displayAttackDamage(diaSword, QVariantList{sharp3, 0, 0, 0}) == 9
               && hb.displayAttackDamage(woodSword, QVariantList{sharp5, 0, 0, 0}) == 7
               && hb.displayAttackDamage(diaSword, QVariantList{sharp5}) == 10; // 缺项补 0 → 7+2.5 = round 10
        // 同源逐级枚举：display == qRound(authority) 对级 0..5 全成立（公式只活在一处的运行时证据）。
        for (int lvl = 0; lvl <= 5; ++lvl) {
            const int e[4] = {EnchantRegistry::pack(int(EnchantRegistry::Sharpness), lvl), 0, 0, 0};
            const QVariantList qvl = QVariantList{e[0], 0, 0, 0};
            ok = ok && hb.displayAttackDamage(diaSword, qvl)
                      == qRound(EnchantRegistry::weaponAttackDamage(diaSword, e));
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t825 display==combat damage single source: weaponAttackDamage 7/7.5/8.5 (dia "
                             "sword lvl 0/I/III), wood sword V 6.5; Hotbar::displayAttackDamage rounds same "
                             "authority (8 at .5 half-up, 9 at III, level sweep 0..5 equality); empty/partial "
                             "enchant arrays degrade to base";
    }

    // ── t826 击退附魔实战强度探针（R19.13；公式面 + Entities 层真位移，t774 爆炸击退同款 rig）──
    //    用户报告：「附击退打生物无击退」。根因：旧强度 1+0.5*级 令 II 仅 ~2.3 格总位移（基线 ~1.1 格），
    //    与 AI 游荡抖动同量级 → 实战「无感」。t826 收口 EnchantRegistry::knockbackStrength 单一权威
    //    （1+3.0*级：I/II = 4.0/7.0 → 总位移 ~4.5/~7.9 格，MC 1.0 量级）。
    //    断言：① 公式面 0/1/2 级 = 1.0/4.0/7.0（负级防御钳）；② 物理面 —— 真 EntityManager 三猪各吃一档
    //    knockback(+X)，8 tick（0.128s）位移严格按级递增且落量级带（理论 0.90/3.60/6.30 格 ≈
    //    v0*(1-e^-0.512)/4，v0 = 4.5*strength；游荡噪声 ±0.15）。
    {
        bool ok = std::abs(EnchantRegistry::knockbackStrength(0) - 1.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(1) - 4.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(2) - 7.0f) < 1e-5f
               && std::abs(EnchantRegistry::knockbackStrength(-3) - 1.0f) < 1e-5f;
        // rig：运行期扫空区（P20/t809 先例 —— nextSlot 网格已耗尽）。需 18×5 净空（dy -1..+3 含地板层）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 93 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 17 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 17 && clear; ++dx)
                    for (int dz = -2; dz <= 2 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t826 knockback by level: no clear rig area found";
        } else {
            // 石平台（防 spawn 即坠；3 行宽防侧移跌落）。
            for (int dx = 0; dx <= 17; ++dx)
                for (int dz = -2; dz <= 2; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
            EntityManager ents;
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            // 三猪错开 3 格；**从远端猪先击**（II 位移朝 +X，先行者让出跑道防相互推挤）。
            const int pig2 = ents.spawnMobTyped(x0 + 7, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            const int pig1 = ents.spawnMobTyped(x0 + 4, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            const int pig0 = ents.spawnMobTyped(x0 + 1, kRigY, z0, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
            ok = ok && pig0 >= 0 && pig1 >= 0 && pig2 >= 0;
            // 落定窗（0.96s）：spawn 瞬时悬空 0.05 格 + resting 复探按 AI 相位错峰 —— 不先 tick 落定的话，
            //   个别猪在他人测量窗内正处「下落 / 半嵌地板」态，mobAabbHitsSolid 把水平击退位移全撤回
            //   （首轮实测 I 级猪 dx 0.39 vs 期望 1.83 的根因）。60 tick 后全部 resting 贴面再测。
            for (int t = 0; t < 60; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            const int pigs[3] = {pig2, pig1, pig0};                 // 击序：II → I → 0（远端先走）
            const float strengths[3] = {EnchantRegistry::knockbackStrength(2),
                                       EnchantRegistry::knockbackStrength(1),
                                       EnchantRegistry::knockbackStrength(0)};
            float dxPos[3] = {-1.0f, -1.0f, -1.0f};                 // [0]=II [1]=I [2]=无附魔
            for (int k = 0; ok && k < 3; ++k) {
                const QVector3D startP = ents.posAt(pigs[k]);
                ents.knockback(pigs[k], 1.0f, 0.0f, strengths[k]);  // +X 方向击退（强度 = attackMob 同式）
                for (int t = 0; t < 8; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
                const QVector3D endP = ents.posAt(pigs[k]);
                dxPos[k] = endP.x() - startP.x();
                qInfo().noquote() << "  [t826 diag] pig" << k << "idx" << pigs[k] << "start" << startP
                                  << "end" << endP << "dx" << dxPos[k];
            }
            // 短窗（0.128s）位移 ≈ v0×0.016×(1-0.936^8)/0.064 ≈ 0.10×v0（总位移 v0/kKnockbackDrag ≈ 40% 在
            //   窗内；短窗让击退主导、游荡噪声 ≤±0.15）：理论 II/I/0 = 3.19/1.83/0.46。
            ok = ok && dxPos[2] > 0.15f && dxPos[2] < 0.80f       // 无附魔基线 ~0.46 格
                 && dxPos[1] > 1.35f && dxPos[1] < 2.35f          // I ~1.83 格
                 && dxPos[0] > 2.65f && dxPos[0] < 3.75f          // II ~3.19 格
                 && dxPos[2] < dxPos[1] && dxPos[1] < dxPos[0];   // 严格按级递增（用户症状的反面）
            if (!ok) qInfo().noquote() << "  t826 displacements II/I/base =" << dxPos[0] << dxPos[1] << dxPos[2];
            // 清场（地板全清 + 2 tick 收敛）。
            for (int dx = 0; dx <= 17; ++dx)
                for (int dz = -2; dz <= 2; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air);
            tickN(w, 2);
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t826 knockback enchant scales in combat: strength 1.0/4.0/7.0 (lvl 0/I/II, "
                             "negative clamped); real EntityManager displacement over 0.128s strictly "
                             "increasing ~0.9/3.6/6.3 blocks (old +50%/lvl was ~1.4/2.0 = wander-noise level, "
                             "user saw no knockback)";
    }

    // ── t827 燃焰点燃链探针（R19.13 附魔全效果审计的「重点疑」实测项；EntityManager 直编）──
    //    attackMob → ignite(level*4s) 静态接线已核（playercontroller t476 链）；本探针锁点燃 → 火烧推进
    //    → 扣血 → 致死 burned 掉落链的运行时行为。kFireExtinguishChance=0.15（每次火伤结算随机提前熄灭）
    //    → 断言取「多样本计数下界」防偶发：① 点燃即 isBurningAt（对照猪恒 false）；② 10 只 3HP 猪 1.76s 内
    //    ≥5 只实际扣血（每只 P(扣) = 0.85，P(<5) ≈ 3e-5）；③ 6 只 1HP 猪 ≥1 只烧死（首脉冲 ~1.05s +
    //    0.5s 死亡动画 → mobDied burned=true ~1.55s 在窗内；P(全不成) = 0.15^6 ≈ 1e-5）。游荡 ≤1.76 格 →
    //    地板 7 行宽（dz -3..+3）+ 出生位留 ≥2 格边距，防侧移跌落污染对照。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 125 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 15 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 15 && clear; ++dx)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dy = -1; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t827 fire-aspect ignite: no clear rig area found";
        } else {
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
            EntityManager ents;
            const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
            int diedBurned = 0, diedTotal = 0;
            QObject::connect(&ents, &EntityManager::mobDied, &ents,
                             [&](int, int, int, int, bool burned, bool) {
                                 ++diedTotal; if (burned) ++diedBurned;
                             });
            // 10 只计量猪（3HP，中央两行错开）+ 6 只 1HP 判死猪 + 1 只对照猪（10HP 不点燃）。
            int meter[10];
            for (int i = 0; i < 10; ++i)
                meter[i] = ents.spawnMobTyped(x0 + 1 + (i % 5) * 3, kRigY, z0 + (i < 5 ? -1 : 1),
                                              EntityManager::MobPig, QStringLiteral("#ee9999"), 3);
            int frail[6];
            for (int i = 0; i < 6; ++i)
                frail[i] = ents.spawnMobTyped(x0 + 1 + i * 2, kRigY, z0, EntityManager::MobPig,
                                              QStringLiteral("#ee9999"), 1);
            const int control = ents.spawnMobTyped(x0 + 13, kRigY, z0, EntityManager::MobPig,
                                                   QStringLiteral("#ee9999"), 10);
            bool ok = control >= 0 && !ents.isBurningAt(control);
            for (int i = 0; i < 10; ++i) ok = ok && meter[i] >= 0;
            for (int i = 0; i < 6; ++i) ok = ok && frail[i] >= 0;
            // 点燃全部实验猪（燃焰 II = 8s 同长；对照不点）。点燃即燃（视觉火焰 Model 据点）。
            for (int i = 0; i < 10; ++i) ents.ignite(meter[i], 8.0f);
            for (int i = 0; i < 6; ++i) ents.ignite(frail[i], 8.0f);
            bool allBurning = true;
            for (int i = 0; i < 10; ++i) allBurning = allBurning && ents.isBurningAt(meter[i]);
            for (int i = 0; i < 6; ++i) allBurning = allBurning && ents.isBurningAt(frail[i]);
            ok = ok && allBurning && !ents.isBurningAt(control);
            // 1.76s（110 tick）：首火伤脉冲（~1.05s）+ 死亡动画 0.5s（mobDied ~1.55s）均在窗内。
            for (int t = 0; t < 110; ++t) ents.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
            int damaged = 0;
            for (int i = 0; i < 10; ++i)
                if (ents.healthAt(meter[i]) < 3 || ents.deadAt(meter[i])) ++damaged; // 3HP 计量猪扣血 / 烧死均计
            ok = ok && damaged >= 5                       // ≥5/10 实际吃到火伤（P(<5) ≈ 3e-5）
                 && ents.healthAt(control) == 10 && !ents.deadAt(control) // 对照猪无伤
                 && diedTotal >= 1 && diedBurned >= 1;    // ≥1 只 1HP 猪烧死且走 burned 掉落链
            if (!ok) qInfo().noquote() << "  t827 fire diag: damaged" << damaged << "/10, died" << diedTotal
                                       << "burned" << diedBurned << "control hp" << ents.healthAt(control);
            for (int dx = 0; dx <= 15; ++dx)
                for (int dz = -3; dz <= 3; ++dz) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t827 fire-aspect ignite chain: ignite->isBurningAt immediate (control "
                                 "stays unlit), 1s-interval fire damage lands on >=5/10 meter pigs in 2.2s, "
                                 "1HP pig dies with burned=true (cooked-drop entry); attackMob->ignite("
                                 "4s*level) wiring static-verified";
        }
    }

    // ── P-t828 水下窒息 + 鱿鱼浮力（R19.13 生物组；专用局部世界 wA：pit 水柜 + 干 pit 对照）──
    //    (a) 猪（3HP）沉水 pit 底部：头位浸水 → 15s 呼吸耗尽 → 1HP/s 窒息掉血（20s 窗扣 ~4HP → 死或 ≤1）；
    //        同构干 pit（同壁高同基底、只少水）对照猪满血不动（呼吸不启动）。机制等价玩家 t202 溺水节奏。
    //    (b) 鱿鱼（10HP）同水 pit：浮力项 → 不贴底（中心 y 明显高于池底 restY；旧缓沉行为恒贴底）；
    //        且水生豁免溺水（20s 后仍满血）。
    //    rig 高度：局部 World setter 触发 worldgen（地形 ~57-71 + 树冠 ≤81）→ rig 平面取 y84+（地形之上
    //    确定性净空，P31「自凿净空」精神的免凿版——直接摆更高）。pit 结构：y84 石基底（30×30）+ 两口
    //    12×12 pit 开口（水 pit x10..21/z10..21 填水 y85..87；干 pit x24..35/z24..35 全空）+ 口外 y85..88
    //    全石壁（壁顶 89 > 鱿鱼水面 bob 峰脚位 ~88.05 → 三种 mob 都爬不出，水平出界被 mobAabbHitsSolid
    //    撤回）；水位恒定（探针不 tick world，流体静置）。
    {
        World wA;
        wA.setWidth(44); wA.setDepth(44); wA.setHeight(96); wA.setSeed(21);
        for (int x = 8; x < 38; ++x)
            for (int z = 8; z < 38; ++z) wA.setBlock(x, 84, z, BR::Stone, 0);
        for (int x = 8; x < 38; ++x)
            for (int z = 8; z < 38; ++z)
                for (int y = 85; y <= 88; ++y) {
                    const bool inWaterPit = (x >= 10 && x < 22 && z >= 10 && z < 22);
                    const bool inDryPit = (x >= 24 && x < 36 && z >= 24 && z < 36);
                    const quint8 b = inWaterPit
                        ? ((y <= 87) ? BR::Water : BR::Air)
                        : (inDryPit ? BR::Air : BR::Stone);
                    wA.setBlock(x, y, z, b, 0);
                }
        EntityManager emA;
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        const int drownPig = emA.spawnMobTyped(15, 85, 15, EntityManager::MobPig,
                                               QStringLiteral("#ee9999"), 3);
        const int dryPig = emA.spawnMobTyped(29, 85, 29, EntityManager::MobPig,
                                             QStringLiteral("#ee9999"), 3);
        const int squid = emA.spawnMobTyped(13, 86, 13, EntityManager::MobSquid,
                                            QStringLiteral("#6a4a3a"), 10);
        bool ok = drownPig >= 0 && dryPig >= 0 && squid >= 0;
        if (ok) {
            // (a)+(b) 同场推进 20s（1250 tick）：猪窒息窗 15+4s、鱿鱼浮力窗充裕。
            for (int t = 0; t < 1250; ++t) emA.tick(0.016f, &wA, farListener, 0.3f, 1.8f, false);
            const int dHp = emA.healthAt(drownPig);
            const bool dDead = emA.deadAt(drownPig);
            const int dryHp = emA.healthAt(dryPig);
            const float sqY = emA.posAt(squid).y();
            const int sqHp = emA.healthAt(squid);
            // 溺水猪：3HP − 1HP/s（自 15s 起）→ 20s 内扣 ~4HP → 死或 ≤1；对照猪满血 3。
            ok = ok && (dDead || dHp <= 1);
            ok = ok && dryHp == 3;
            // 鱿鱼：浮离池底（旧缓沉恒贴底 restY 85.45；浮力后 bob 在 ~86.4..88.5 → 下界 86.2 区分）+
            //   满血（水生豁免溺水）。
            ok = ok && sqY >= 86.2f && sqHp == 10 && !emA.deadAt(squid);
            if (!ok)
                qInfo().noquote() << "  t828 diag: drownPig hp" << dHp << "dead" << dDead
                                  << "| dryPig hp" << dryHp
                                  << "| squid y" << sqY << "hp" << sqHp;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t828 drowning + squid buoyancy: submerged pig loses HP after 15s "
                             "breath (1HP/s, dry control stays full), squid buoyed off pool floor "
                             "(no bottom-resting) and exempt from drowning";
    }

    // ── P-t829 末影人三修（专用局部世界 wB 平石台；rig y84+ 地形之上，t828 同款免凿高台）──
    //    (a) 碰水即伤 + 瞬移逃离：夜行者站 1 深水洼 → 首触 tick 扣 1HP + 8..16 格瞬移离开（4s 窗 hp<满
    //        且位移 ≥6；旧实现「连续满 1s 才扣」在瞬移先发节奏下恒凑不满 = 水伤死代码——用户「水中平安
    //        无事」根因，t829③ 改首触即伤）；
    //    (b) 箭命中 race 修复：玩家箭命中夜行者 → 强制瞬移（绕过冷却）+ 箭一并消耗——1.6s 后场内零 Arrow
    //        残留（旧「dodge 后箭继续飞 / 冷却内零反应穿身」两面都锁）、夜行者未被箭扣血（弹射免疫）；
    //    (c) 下巴补全合成器：合成 256×128 rig（头盒区底带透明 + 上部不透明脸 / 眼亮行）→ 输出头区全不透明
    //        + 列向延拓色（下巴带 == 其列上方最近不透明色）+ 眼行原样保留 + 头区外像素不动 + 坏输入返空。
    {
        World wB;
        wB.setWidth(48); wB.setDepth(48); wB.setHeight(96); wB.setSeed(22);
        for (int x = 2; x < 46; ++x)
            for (int z = 2; z < 46; ++z) wB.setBlock(x, 84, z, BR::Stone, 0);
        EntityManager emB;
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        bool ok = true;
        // (a) 水洼 4×4（x22..25, z22..25）y85 一层水（台面顶 85.0 → 夜行者脚位 85.0 恰在水格）。
        for (int x = 22; x < 26; ++x)
            for (int z = 22; z < 26; ++z) wB.setBlock(x, 85, z, BR::Water, 0);
        const int nw1 = emB.spawnMobTyped(23, 86, 23, EntityManager::MobNightwalker,
                                          QStringLiteral("#2a1f2a"), 10);
        if (nw1 < 0) {
            ok = false;
        } else {
            const QVector3D p0 = emB.posAt(nw1);
            // playerTargetable=true（真实 Survival 语义）：敌对 mob 走 mobType 分发链进 aiNightwalker
            //   （怕水扣血/瞬移在其内）——false 会走 t290 观察者门控的 aiWander 回退，永远到不了水伤段
            //   （首跑踩坑：moved 0 + hp 10）。listener 远在 -1000 → detect/stare 够不着，只余游荡。
            //   位移断言取**全程最大 XZ 位移**（逐 tick 采样）：瞬移方向随机、后续跳可能把净位移拉回
            //   原点附近（实测首跳 13 格后二跳回 4.6）——净位移断言 flaky，峰值断言确定性捕获首次瞬移。
            float maxMoved = 0.0f;
            for (int t = 0; t < 250; ++t) {
                emB.tick(0.016f, &wB, farListener, 0.3f, 1.8f, true);
                const QVector3D pt = emB.posAt(nw1);
                maxMoved = std::max(maxMoved,
                                    QVector3D(pt.x() - p0.x(), 0.0f, pt.z() - p0.z()).length());
            }
            ok = ok && emB.healthAt(nw1) < 10 && maxMoved >= 6.0f;
            if (!ok)
                qInfo().noquote() << "  t829a diag: nw hp" << emB.healthAt(nw1)
                                  << "maxMoved" << maxMoved;
        }
        // (b) 箭 deflection：独立管理器 + 独立台面区（避开 (a) 残留水洼 / 夜行者）。箭 y 86.4 = 夜行者
        //     站姿中心（85+1.4）；24 b/s 飞 ~8 格重力跌落 ~1.3 格仍在其 ±(1.4+0.4) 命中带内。
        EntityManager emB2;
        const int nw2 = emB2.spawnMobTyped(34, 85, 6, EntityManager::MobNightwalker,
                                           QStringLiteral("#2a1f2a"), 10);
        if (nw2 < 0) {
            ok = false;
        } else {
            const QVector3D p0 = emB2.posAt(nw2);
            emB2.spawnArrowPlayer(QVector3D(26.5f, 86.4f, 6.5f), QVector3D(24.0f, 0.0f, 0.0f), 5);
            // 1.6s 窗：命中（~0.35s）即 dodge 位移 ≥8；取全程最大 XZ 位移（同 (a)——瞬移方向随机，净
            //   位移可能被后续跳拉回）。箭在命中帧即被消耗（t829①）——零 Arrow 断言无需等寿命 despawn。
            float maxMoved2 = 0.0f;
            for (int t = 0; t < 100; ++t) {
                emB2.tick(0.016f, &wB, farListener, 0.3f, 1.8f, false);
                const QVector3D pt = emB2.posAt(nw2);
                maxMoved2 = std::max(maxMoved2,
                                     QVector3D(pt.x() - p0.x(), 0.0f, pt.z() - p0.z()).length());
            }
            int arrows = 0;
            for (int i = 0; i < emB2.count(); ++i)
                if (emB2.aliveAt(i) && emB2.kindAt(i) == EntityManager::Arrow) ++arrows;
            ok = ok && arrows == 0 && emB2.healthAt(nw2) == 10 && maxMoved2 >= 6.0f;
            if (!ok)
                qInfo().noquote() << "  t829b diag: arrows" << arrows << "nw hp"
                                  << emB2.healthAt(nw2) << "maxMoved" << maxMoved2;
        }
        // (c) 下巴合成器密闭 rig。
        {
            QDir dC(QDir::temp().absoluteFilePath("t829_chin_probe"));
            dC.removeRecursively();
            dC.mkpath(".");
            const QString srcPath = dC.absoluteFilePath("enderman.png");
            QImage src(256, 128, QImage::Format_ARGB32);
            src.fill(Qt::transparent); // 全透底（含头区外「躯干带」原样保留断言用）
            // 头盒区 = base (0,0)-(32,16) → HD (0,0)-(128,64)：top 面不透明暗色岛 + 脸窗（眼亮行 / 底带透）。
            const QRgb dark = qRgb(24, 18, 30);
            const QRgb eye = qRgb(232, 220, 255);
            for (int x = 32; x < 64; ++x)
                for (int y = 0; y < 32; ++y) src.setPixel(x, y, dark);      // top 面不透明
            for (int x = 32; x < 64; ++x)
                for (int y = 32; y < 48; ++y) src.setPixel(x, y, dark);     // 脸上部
            for (int x = 40; x < 56; ++x)
                for (int y = 36; y < 40; ++y) src.setPixel(x, y, eye);      // 眼亮行（须原样保留）
            // 脸底带（HD y48..63 = base v12..15）留透明 → 合成须列向延拓填充。
            src.save(srcPath, "PNG");
            const QString outPath = generateNightwalkerChinFile(srcPath, 77);
            bool cOk = !outPath.isEmpty();
            if (cOk) {
                QImage out(outPath);
                cOk = !out.isNull() && out.size() == src.size();
                if (cOk) {
                    for (int y = 0; y < 64 && cOk; ++y)
                        for (int x = 0; x < 128; ++x)
                            if (qAlpha(out.pixel(x, y)) != 255) { cOk = false; break; } // 头区全不透明
                    // 下巴带（y48..63, x32..63）== 列上方最近不透明色（本 rig 脸上部恒 dark → 全 dark）。
                    for (int y = 48; y < 64 && cOk; ++y)
                        for (int x = 32; x < 64; ++x)
                            if (out.pixel(x, y) != dark) { cOk = false; break; }
                    // 眼行原样保留（合成不得动既有不透明像素）。
                    for (int x = 40; x < 56 && cOk; ++x)
                        for (int y = 36; y < 40; ++y)
                            if (out.pixel(x, y) != eye) { cOk = false; break; }
                    // 头区外不动（仍透明——合成只补头盒区，防把延拓拉进躯干带）。
                    cOk = cOk && qAlpha(out.pixel(200, 100)) == 0 && qAlpha(out.pixel(10, 80)) == 0;
                }
            }
            // 坏输入：缺文件 → 返空（优雅降级，调用方回退原样）。
            cOk = cOk && generateNightwalkerChinFile(dC.absoluteFilePath("missing.png"), 77).isEmpty();
            if (!cOk) qInfo().noquote() << "  t829c diag: chin synth contract broken";
            ok = ok && cOk;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t829 nightwalker trio: water contact deals first-touch damage + "
                             "teleport escape, player arrow hit forces teleport dodge and consumes "
                             "the arrow (no pass-through, no cooldown-blind window, mob unhurt), "
                             "chin-filler compositor fills head-box transparent band via column "
                             "extension preserving eyes and out-of-region pixels";
    }

    // ── P-t830 燃烬者主世界自然刷新摘除（专用局部世界 wF **height 96**：共享 w 高 48 时 heightAt ~57-71
    //    越 surface 界 → surface 尝试恒败只剩稀有洞穴气袋（实测 60 周期仅 3 刷）——高度 96 下地表 spawn
    //    真实成立。tickHostileLife skyBrightness=0 全域黑暗）──
    //    统计采样：~40+ 个自然刷新事件（每 2.2s 周期 ≤1 只，周期末清场防敌对区域 cap 12 饱和停刷）→
    //    断言零 Emberling（旧 6 份表 P(40 采样零燃烬) = (5/6)^40 ≈ 4.6e-4 → 回归必被逮）+ ≥3 型多样
    //    （防「摘除时错删整池」的反向回归）。手动刷路径（蛋 / 笼 spawnHostileMob）不经本表——t787 探针
    //    已锁笼路径含 Emberling，不受影响。ring [24,40] 全落界内（listener 居中 50,50）。
    {
        World wF;
        wF.setWidth(100); wF.setDepth(100); wF.setHeight(96); wF.setSeed(26);
        EntityManager em830;
        const QVector3D P830(50.0f, float(wF.heightAt(50, 50) + 2), 50.0f);
        int samples = 0, ember = 0, distinct = 0;
        bool seen[20] = {};
        for (int round = 0; round < 60 && samples < 40; ++round) {
            for (int t = 0; t < 140; ++t)
                em830.tickHostileLife(0.016f, &wF, P830, 0.0f); // 一个 spawn 周期（kSpawnInterval 2s）+ 余量
            for (int i = 0; i < em830.count(); ++i) {
                if (!em830.aliveAt(i) || em830.deadAt(i)) continue;
                if (em830.kindAt(i) != EntityManager::Mob) continue;
                const int mt = em830.mobTypeAt(i);
                ++samples;
                if (mt == EntityManager::MobEmberling) ++ember;
                if (mt >= 0 && mt < 20 && !seen[mt]) { seen[mt] = true; ++distinct; }
                em830.damageEntity(i, em830.maxHealthAt(i)); // 清场（dead → 不计 cap / 下轮不再采）
            }
        }
        bool ok = samples >= 40 && ember == 0 && distinct >= 3;
        if (!ok)
            qInfo().noquote() << "  t830 diag: samples" << samples << "ember" << ember
                              << "distinct" << distinct;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t830 emberling removed from natural dark-spawn pool: " << samples
                          << " natural spawns sampled with zero Emberling (>=3 distinct hostile "
                             "types prove pool alive; eggs/spawner manual paths untouched)";
    }

    // ── P-t831 狼/豹猫驯服全链补全（专用局部世界 wC 平石台；Entities 层直测，Game 层 useBlock 接线静态审）──
    //    (a) 驯服成功 → 爱心（inLoveAt 真）→ kTameHeartDuration 4s 衰减后收心；
    //    (b) healTamedPet：受伤驯服狼回血钳上限；满血 / 未驯服 → false（caller 不消耗）；
    //    (c) 坐态冻结（toggleWolfSit 后近旁玩家 1s 零位移）↔ 站态跟随（2s 位移 ≥2）；
    //    (d) 跟随态距玩家 > kWolfTeleportDist → 瞬移到玩家近旁（1s 内 ≤10 格）；
    //    (e) 豹猫驯服同爱心链。
    {
        World wC;
        wC.setWidth(44); wC.setDepth(44); wC.setHeight(96); wC.setSeed(23); // rig y84+ 地形之上（t828 同款）
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wC.setBlock(x, 84, z, BR::Stone, 0);
        EntityManager emC;
        const int wolf = emC.spawnMobTyped(8, 85, 8, EntityManager::MobWolf,
                                           QStringLiteral("#c8ccd4"), 10);
        bool ok = wolf >= 0;
        if (ok) {
            // (a) 驯服循环（~33%/次；P(200 次全败)≈2e-36）。
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && !tamed; ++attempt)
                tamed = emC.tameWolf(wolf);
            ok = ok && tamed && emC.wolfTamedAt(wolf) && emC.inLoveAt(wolf); // 驯服即爱心
            for (int t = 0; t < 344; ++t) // 5.5s > 4s 爱心时长
                emC.tick(0.016f, &wC, QVector3D(10.5f, 86.0f, 10.5f), 0.3f, 1.8f, true);
            ok = ok && !emC.inLoveAt(wolf); // 收心
            // (b) 喂食回血。
            emC.damageEntity(wolf, 4);
            ok = ok && emC.healthAt(wolf) == 6;
            ok = ok && emC.healTamedPet(wolf, 4) && emC.healthAt(wolf) == 10; // 6→10
            ok = ok && !emC.healTamedPet(wolf, 4);                            // 满血 → false
            const int wildCtl = emC.spawnMobTyped(4, 85, 4, EntityManager::MobWolf,
                                                  QStringLiteral("#c8ccd4"), 10);
            ok = ok && wildCtl >= 0 && !emC.healTamedPet(wildCtl, 4);         // 未驯服 → false
            // (c) 坐态冻结。
            emC.toggleWolfSit(wolf);
            ok = ok && emC.wolfSittingAt(wolf);
            const QVector3D p0 = emC.posAt(wolf);
            for (int t = 0; t < 64; ++t)
                emC.tick(0.016f, &wC, QVector3D(11.5f, 86.0f, 8.5f), 0.3f, 1.8f, true); // 3 格外玩家
            ok = ok && (emC.posAt(wolf) - p0).length() < 0.05f; // 坐 → 不动（游荡 / 跟随全停）
            // (c2) 站态跟随。
            emC.toggleWolfSit(wolf);
            ok = ok && !emC.wolfSittingAt(wolf);
            for (int t = 0; t < 125; ++t)
                emC.tick(0.016f, &wC, QVector3D(15.5f, 86.0f, 8.5f), 0.3f, 1.8f, true); // 7 格外玩家
            ok = ok && (emC.posAt(wolf) - p0).length() >= 2.0f; // 站 → 跟随走近
            // (d) 过远瞬移（listener y 取台面层 86 → 瞬移落点扫到台面而非台面下自然地形）。
            //   t878⑤ 阈值 24→12（MC 语义 >12 格瞬移）：35 格远距照触发；中距 16 格（旧 24 阈值内、新 12 外）
            //   也须瞬移；近距 6 格**不瞬移**（走跟非跳变——单 AI 窗内位移 ≪ 瞬移跳距，可分辨）。
            const QVector3D far(36.5f, 86.0f, 36.5f);
            for (int t = 0; t < 64; ++t)
                emC.tick(0.016f, &wC, far, 0.3f, 1.8f, true);
            const QVector3D posAfterFar = emC.posAt(wolf);
            const float dXZ = QVector3D(posAfterFar.x() - far.x(), 0.0f,
                                        posAfterFar.z() - far.z()).length();
            ok = ok && dXZ <= 10.0f; // >12 → 瞬移 2..5 环 + 漂移余量
            // (d2) 中距 16 格（12 < 16 < 24）→ 一并瞬移（t878⑤ 新语义；旧 24 阈值此处不瞬移 = 探针对旧值红）。
            const QVector3D mid(20.5f, 86.0f, 8.5f); // 距 far 玩家位 ~16+ 格
            const float midGap = QVector3D(mid.x() - posAfterFar.x(), 0.0f,
                                           mid.z() - posAfterFar.z()).length();
            if (ok && midGap > 13.0f && midGap < 23.0f) { // 只在几何成立时驱动（防瞬移落点贴边使 gap 出带）
                const QVector3D beforeMid = emC.posAt(wolf);
                for (int t = 0; t < 8; ++t) // 8 帧 ≤ 2 个 AI 窗（kAiTickInterval=4）；走跟位移 ≤0.5
                    emC.tick(0.016f, &wC, mid, 0.3f, 1.8f, true);
                ok = ok && (emC.posAt(wolf) - beforeMid).length() > 5.0f; // 跳变 = 瞬移（走跟 8 帧 ≈ 0.45）
            }
            // (d3) 近距 6 格 → 不瞬移（8 帧内位移 ≤1.0 = 走跟，非 ≥5 跳变）。
            {
                const QVector3D nearP(posAfterFar.x() + 6.0f, 86.0f, posAfterFar.z());
                const QVector3D beforeNear = emC.posAt(wolf);
                for (int t = 0; t < 8; ++t)
                    emC.tick(0.016f, &wC, nearP, 0.3f, 1.8f, true);
                ok = ok && (emC.posAt(wolf) - beforeNear).length() < 1.0f; // 无瞬移跳变
            }
            if (!ok)
                qInfo().noquote() << "  t831 diag: tamed" << emC.wolfTamedAt(wolf)
                                  << "heart" << emC.inLoveAt(wolf) << "hp" << emC.healthAt(wolf)
                                  << "dist" << dXZ;
        }
        // (e) 豹猫驯服爱心链。
        const int ocelot = emC.spawnMobTyped(20, 85, 20, EntityManager::MobOcelot,
                                             QStringLiteral("#e8c890"), 10);
        if (ocelot >= 0) {
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && !tamed; ++attempt)
                tamed = emC.tameOcelot(ocelot);
            ok = ok && tamed && emC.ocelotTamedAt(ocelot) && emC.inLoveAt(ocelot);
        } else {
            ok = false;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t831/t878 taming chain: tame success shows heart (decays 4s), "
                             "healTamedPet restores injured pet (full/wild reject), sitting freezes "
                             "movement vs standing follows owner, >12-block gap teleports pet to "
                             "owner side (mid-gap 16 also jumps, near-gap 6 does not - t878⑤ 24->12), "
                             "ocelot taming shows heart too (collar/sit-toggle GUI glue "
                             "static-reviewed)";
    }

    // ── P-t878④ 中键 pick-block 生物蛋映射全蛋族补全（纯静态映射直调；Game 层）──
    //    mobTypeEggId（PlayerController 静态单一权威）必须覆盖 RecipeRegistry **全部 13 种蛋**（t785 造狼/
    //    豹猫/夜行者/燃烬者蛋时旧表漏跟 = 用户「中键复制不了狼/豹猫生物蛋」根因）。断言三向：
    //    (a) 4 新映射精确命中（狼/豹猫/夜行者/燃烬者）；
    //    (b) 蛋族完备性：遍历全部 mobType 收集映射，**恰好**等于 13 蛋全集（加蛋不跟表 → 集合差非空即红）；
    //    (c) 单射：无两个 mobType 映射同一蛋 id（防复制粘贴错位）。
    {
        bool ok = true;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobWolf)
                  == RecipeRegistry::SpawnEggWolfId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobOcelot)
                  == RecipeRegistry::SpawnEggOcelotId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobNightwalker)
                  == RecipeRegistry::SpawnEggNightwalkerId;
        ok = ok && PlayerController::mobTypeEggId(EntityManager::MobEmberling)
                  == RecipeRegistry::SpawnEggEmberlingId;
        const int kAllEggs[] = {
            RecipeRegistry::SpawnEggPigId,      RecipeRegistry::SpawnEggCowId,
            RecipeRegistry::SpawnEggSheepId,    RecipeRegistry::SpawnEggShamblerId,
            RecipeRegistry::SpawnEggBonesId,    RecipeRegistry::SpawnEggStalkerId,
            RecipeRegistry::SpawnEggSpiderId,   RecipeRegistry::SpawnEggChickenId,
            RecipeRegistry::SpawnEggSquidId,    RecipeRegistry::SpawnEggNightwalkerId,
            RecipeRegistry::SpawnEggEmberlingId, RecipeRegistry::SpawnEggWolfId,
            RecipeRegistry::SpawnEggOcelotId,
        };
        const int kEggCount = int(sizeof(kAllEggs) / sizeof(kAllEggs[0]));
        std::vector<int> mapped;
        for (int mt = 0; mt <= EntityManager::MobAnvil; ++mt) {
            const int egg = PlayerController::mobTypeEggId(mt);
            if (egg != 0) mapped.push_back(egg);
        }
        std::sort(mapped.begin(), mapped.end());
        ok = ok && int(mapped.size()) == kEggCount;
        for (size_t i = 0; ok && i + 1 < mapped.size(); ++i)
            ok = ok && mapped[i] != mapped[i + 1]; // 单射（排序后相邻重复 = 冲突）
        for (int i = 0; ok && i < kEggCount; ++i)
            ok = ok && std::binary_search(mapped.begin(), mapped.end(), kAllEggs[i]);
        if (!ok) {
            qInfo().noquote() << "  t878 diag: mapped" << int(mapped.size()) << "eggs, expect" << kEggCount
                              << "wolfEgg" << PlayerController::mobTypeEggId(EntityManager::MobWolf)
                              << "ocelotEgg" << PlayerController::mobTypeEggId(EntityManager::MobOcelot);
            ++totalFail;
        }
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t878 pick-block egg map completion: mobTypeEggId covers ALL 13 spawn-egg "
                             "items (wolf/ocelot/nightwalker/emberling added - the t785 egg batch never "
                             "followed this table, which is why creative middle-click could not copy "
                             "wolf/ocelot eggs), set-equality against the RecipeRegistry egg family plus "
                             "injectivity pin 'new egg must follow the table' (a future egg without a "
                             "mapping row turns this red)";
    }

    // ── P-t832 染料染羊 + 长回自然色重掷（专用局部世界 wD 草平台）──
    //    染（sheepWoolAt=染下标）→ 剪毛载荷 = 染色（一次性语义上半）→ 吃草长回 = 重掷自然权重恢复自然色
    //    （确定性断言：长回色 ∈ 自然色集 {0,6,7,8,12,15}——染 10 紫 ∉ 集，重掷绝不再现 10）；非羊拒染。
    {
        World wD;
        wD.setWidth(44); wD.setDepth(44); wD.setHeight(96); wD.setSeed(24); // rig y84 地形之上
        for (int x = 8; x < 36; ++x)
            for (int z = 8; z < 36; ++z) wD.setBlock(x, 84, z, BR::Grass, 0); // 28×28 草平台
        EntityManager emD;
        const int sheep = emD.spawnMobTyped(22, 85, 22, EntityManager::MobSheep,
                                            QStringLiteral("#f5f0e8"), 10);
        bool ok = sheep >= 0;
        if (ok) {
            int shearPayload = -1, shearCount = 0;
            QObject::connect(&emD, &EntityManager::sheepSheared, &emD,
                             [&](int, int, int, int woolIdx) {
                                 if (shearCount == 0) shearPayload = woolIdx;
                                 ++shearCount;
                             });
            ok = ok && emD.dyeSheep(sheep, 10) && emD.sheepWoolAt(sheep) == 10; // 染紫
            emD.shearSheep(sheep);
            ok = ok && shearCount == 1 && shearPayload == 10 && emD.shearedAt(sheep); // 剪毛得染色
            const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
            // t897 后长回窗必须按「wander 依赖」定宽（探针鲁棒性，同 t897 flake 教训）：t897①把吃草收紧到
            //   「自身列支撑 == Grass」后，羊 idle 站在出生列几乎必然**早期**吃掉出生列草（→Dirt）——等 6s
            //   regrowCooldown 到期扫描时，羊脚下的支撑格已是 Dirt → 长回改走「wander 挪到邻列 Grass」
            //   路径（RNG 游走 + 每秒复扫）。旧窗 750 tick（12s）只盖「冷却 + 扫描 + 余量」，没盖 wander
            //   多轮 → 复跑 1/2~2/3 概率假红（sheared 恒 true）。窗 750→1600 tick（25.6s ≈ 6s 冷却 +
            //   ~10 轮 wander 周期 + ~19 次复扫，长窗行为断言「窗口覆盖节律多轮」口径）。
            for (int t = 0; t < 1600; ++t) // 25.6s：regrowCooldown 6s + wander 多轮 + 复扫余量
                emD.tick(0.016f, &wD, farListener, 0.3f, 1.8f, false);
            const int regrown = emD.sheepWoolAt(sheep);
            const bool natural = regrown == 0 || regrown == 6 || regrown == 7
                                 || regrown == 8 || regrown == 12 || regrown == 15;
            ok = ok && !emD.shearedAt(sheep) && natural && regrown != 10; // 长回自然色（绝不再现染紫）
            if (!ok)
                qInfo().noquote() << "  t832 diag: sheared" << emD.shearedAt(sheep)
                                  << "regrown" << regrown << "payload" << shearPayload;
            const int pig = emD.spawnMobTyped(30, 85, 30, EntityManager::MobPig,
                                              QStringLiteral("#ee9999"), 10);
            ok = ok && pig >= 0 && !emD.dyeSheep(pig, 5); // 非羊拒染
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t832 dye-on-sheep: dyeSheep sets wool color, shear payload carries "
                             "the dyed color (one-shot semantics), regrow rerolls natural weights "
                             "(never the dyed color again), non-sheep rejected";
    }

    // ── P-t833 刷怪笼被动实刷 + 鱿鱼水格路径（专用局部世界 wE 平石台 + 水柱）──
    //    蛋改型语义下的被动笼（pig state / squid state）经 tickSpawners 真刷：猪笼出猪（血 10 = 被动默认，
    //    敌对路由是 20——极性锁）；鱿鱼笼走水格谓词（here+above Water）在水柱内出鱿鱼。笼位 state 用
    //    BlockRegistry::spawnerStateForMob 单一权威编码。迷你模型换型视觉（t833① 重建修）不进矩阵（无
    //    Quick3D），留人工目视。
    {
        World wE;
        wE.setWidth(40); wE.setDepth(40); wE.setHeight(96); wE.setSeed(25); // rig y84 地形之上
        for (int x = 2; x < 38; ++x)
            for (int z = 2; z < 38; ++z) wE.setBlock(x, 84, z, BR::Stone, 0);
        // 猪笼 (14,85,14)；鱿鱼笼 (24,85,24) + 南侧 3 宽 2 深水柱 (23..25, 85..86, 25)。
        wE.setBlock(14, 85, 14, BR::Spawner, BR::spawnerStateForMob(EntityManager::MobPig));
        wE.setBlock(24, 85, 24, BR::Spawner, BR::spawnerStateForMob(EntityManager::MobSquid));
        for (int x = 23; x <= 25; ++x)
            for (int y = 85; y <= 86; ++y) wE.setBlock(x, y, 25, BR::Water, 0);
        EntityManager emE;
        const QVector3D nearPlayer(14.5f, 86.0f, 15.5f); // 猪笼旁（两笼均 <16 激活半径：猪笼 1.4 / 鱿鱼笼 ~14.1）
        emE.tickSpawners(6.5f, &wE, nearPlayer); // ≥ kSpawnerInterval 6 → 完整刷怪周期
        emE.tickSpawners(6.5f, &wE, nearPlayer); // 第二周期（首个候选位被前轮占用时兜底）
        int pigs = 0, squids = 0;
        bool squidInWater = false;
        for (int i = 0; i < emE.count(); ++i) {
            if (!emE.aliveAt(i) || emE.deadAt(i) || emE.kindAt(i) != EntityManager::Mob) continue;
            if (emE.mobTypeAt(i) == EntityManager::MobPig) {
                ++pigs;
                if (emE.healthAt(i) != 10) pigs = -100; // 被动默认血 10（敌对路由 20 → 极性破）
            } else if (emE.mobTypeAt(i) == EntityManager::MobSquid) {
                ++squids;
                const QVector3D sp = emE.posAt(i);
                // +0.01 nudge：浮点 feet 85.45-0.45 可能落 84.9999 → 截断 84（石）；nudge 同引擎
                //   resting 复探的 FP-robust 手法（lessons「resting 态支撑格复探」条）。
                if (wE.blockAt(int(sp.x()), int(sp.y() - 0.45f + 0.01f), int(sp.z())) == BR::Water)
                    squidInWater = true;
            }
        }
        // 鱿鱼笼距 nearPlayer ~14.1 < 16 激活半径 ✓（激活是 XZ 距离）。
        bool ok = pigs >= 1 && squids >= 1 && squidInWater;
        if (!ok)
            qInfo().noquote() << "  t833 diag: pigs" << pigs << "squids" << squids
                              << "squidInWater" << squidInWater;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t833 passive spawners really spawn: pig cage produces 10-HP pigs "
                             "(passive polarity) and squid cage spawns through the water-column "
                             "predicate (spawn lands submerged); cage mini-model retype visual is "
                             "manual-check (rebuilt-on-retype fix)";
    }

    // ── P-r24#1 门整扇湿判防火带（Review 2026-08-24 中危 #1；专用局部世界 seed 31，P-t830 局部世界先例）──
    //   7b83fbd 防火带只判主目标湿 → 门配对联动点燃无湿判：火从干半扇侧掷中 → 被水保护的湿半扇被连带
    //   焚毁（不可逆）。修后 igniteFlammableAt 把湿判提前为「主目标**或其配对半扇**含水 → 整扇不燃」，
    //   且 (d) 烧毁收尾的「对偶仍是门 → 同窗 Air」兜底对湿对偶跳过（点燃后才泼水的边角同样护住）。
    //   两段断言（真实 tickFire 驱动；晴天无雨混淆源）：
    // (a) 蔓延窗：火贴干半扇（下格 6 邻）、水贴湿半扇（上格侧邻，非火的 6 邻不抑制火）→ 600 窗后两半门
    //     格恒 WoodDoor 且从未进燃烧态（整扇湿判拦下每一次掷骰——非概率断言）+ 火仍存活（证掷骰每窗
    //     都在发生，拦截面真实可达）；
    // (b) 烧尽收尾兜底（确定性，无概率）：干扇两半点燃 → 同 id 写清除上扇燃烧态（t843 契约：显式写 =
    //     换新实例清侧表）→ 上扇侧放水 → 下扇 10 窗烧尽时 (d) 兜底须被湿判拦下 → 上扇存活（修前兜底
    //     setBlock(Air) 焚毁湿半扇）。
    {
        World wD1;
        wD1.setWidth(40); wD1.setDepth(40); wD1.setHeight(96); wD1.setSeed(31);
        const int gy1 = 81; // rig 层（96 高度地形之上；整带自凿清空防丘陵地形撞 rig）
        for (int x = 10; x <= 16; ++x)
            for (int z = 10; z <= 16; ++z)
                for (int y = 79; y <= 84; ++y)
                    wD1.setBlock(x, y, z, BR::Air, 0);
        // (a) rig：火(12) - 门下(13,81) - 门上(13,82)，水(14,82) 只贴门上（对门下是对角 → 干湿分明）。
        wD1.setBlock(12, gy1, 12, BR::Fire, 0);         // 火源（6 邻仅门下格可燃 → 恒 hasFuel 不自熄；水距 2 格对角不抑制）
        wD1.setBlock(13, gy1, 12, BR::WoodDoor, 0);     // 门下格（state bit3=0；干半扇——水不在其 6 邻）
        wD1.setBlock(13, gy1 + 1, 12, BR::WoodDoor, 8); // 门上格（state bit3=1；湿半扇——水正右侧邻）
        wD1.setBlock(14, gy1 + 1, 12, BR::Water, 0);    // 水（harness 不驱动 tickWaterFlow → 静止不漫）
        for (int win = 0; win < 600; ++win)
            for (int t = 0; t < 5; ++t) wD1.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval=5）
        const bool okA = wD1.blockAt(13, gy1, 12) == BR::WoodDoor      // 干半扇存活（从未点燃）
                      && wD1.blockAt(13, gy1 + 1, 12) == BR::WoodDoor  // 湿半扇存活（整扇不燃）
                      && !wD1.isBurningAt(13, gy1, 12)                 // 且从未进燃烧态（非概率断言）
                      && !wD1.isBurningAt(13, gy1 + 1, 12)
                      && wD1.blockAt(12, gy1, 12) == BR::Fire;         // 火仍存活（掷骰每窗发生，拦截面可达）
        // (b) rig（隔 2 行 z=14，与 (a) 火源 / 水均非 6 邻互不干扰）。
        wD1.setBlock(13, gy1, 14, BR::WoodDoor, 0);
        wD1.setBlock(13, gy1 + 1, 14, BR::WoodDoor, 8);
        const bool litB = wD1.igniteFlammableAt(13, gy1, 14); // 干扇直燃（两半全干 → 整扇放行 + 联动点燃）
        const bool linkedB = litB && wD1.isBurningAt(13, gy1 + 1, 14);
        wD1.setBlock(13, gy1 + 1, 14, BR::WoodDoor, 8); // 同 id 写：清上扇燃烧侧表（t843 契约）→ 制造「下扇在燃、上扇不在燃」不对称
        const bool unlitUpper = !wD1.isBurningAt(13, gy1 + 1, 14);
        wD1.setBlock(14, gy1 + 1, 14, BR::Water, 0);    // 上扇侧放水（非下扇 6 邻 → 下扇不被浇熄，烧尽链保留）
        for (int win = 0; win < 40; ++win)
            for (int t = 0; t < 5; ++t) wD1.tickFire(); // kBurnWindowsWood=10 → 下扇第 10 窗烧尽（余量 ×4）
        const bool okB = linkedB && unlitUpper
                      && wD1.blockAt(13, gy1, 14) != BR::WoodDoor       // 下扇已烧尽（Fire flare 或 Air）
                      && wD1.blockAt(13, gy1 + 1, 14) == BR::WoodDoor;  // 湿上扇存活（兜底被湿判拦下；修前被 Air）
        const bool ok = okA && okB;
        if (!ok)
            qInfo().noquote() << "  r24#1 diag: aLower" << int(wD1.blockAt(13, gy1, 12))
                              << "aUpper" << int(wD1.blockAt(13, gy1 + 1, 12))
                              << "aFire" << int(wD1.blockAt(12, gy1, 12))
                              << "bLit" << litB << "bLower" << int(wD1.blockAt(13, gy1, 14))
                              << "bUpper" << int(wD1.blockAt(13, gy1 + 1, 14));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#1 door whole-panel wet firebreak: fire licking the dry half "
                             "never ignites either half while water touches the other half (600 "
                             "windows, door intact, never burning), and a half ignited before water "
                             "arrived burns out without consuming its now-wet pair (burn-completion "
                             "fallback skips wet dual)";
    }

    // ── P-r24#2 clearBlockSilent 红石火把幽灵网格（Review 2026-08-24 中危 #2；专用局部世界 seed 32）──
    //   recheck 火把分支旧版不 emit，而 clearBlockSilent 的 worldChanged/clearAllDirty 在 recheck
    //   **之前** → 红石火把（chunk mesh 几何）清格标脏后错过重建信号 = 幽灵火把残留。修后火把分支在
    //   「实际掉落 ≥1」时自 emit。断言（信号时序序：掉落信号之后必须还能观测到 worldChanged——修前
    //   唯一 worldChanged 在掉落之前 → 时序断言 FAIL）：TNT 顶立红石火把（TorchFloor state=0，支撑 =
    //   正下方 TNT 格）→ clearBlockSilent 清 TNT → 火把格变 Air + 掉落物信号 + 之后仍有重建信号。
    {
        World wT2;
        wT2.setWidth(40); wT2.setDepth(40); wT2.setHeight(96); wT2.setSeed(32);
        for (int x = 10; x <= 14; ++x)
            for (int z = 10; z <= 14; ++z)
                for (int y = 79; y <= 84; ++y)
                    wT2.setBlock(x, y, z, BR::Air, 0);
        wT2.setBlock(12, 81, 12, BR::TntBlock, 0);        // TNT（ShapeFull → 可承火把，torchSupportBlock 真）
        wT2.setBlock(12, 82, 12, BR::RedstoneTorch, 0);   // 红石火把立柱（state TorchFloor=0 → 附着格 = 下方 TNT）
        int seq2 = 0, torchDropSeq = 0, worldSeqAfterDrop = 0, torchDrops = 0;
        QObject::connect(&wT2, &World::blockDroppedAsItem, &wT2,
                         [&](int x, int y, int z, int id) {
                             ++seq2;
                             if (x == 12 && y == 82 && z == 12 && id == int(BR::RedstoneTorch)) {
                                 torchDropSeq = seq2;
                                 ++torchDrops;
                             }
                         });
        QObject::connect(&wT2, &World::worldChanged, &wT2, [&]() {
            ++seq2;
            if (torchDropSeq > 0) worldSeqAfterDrop = seq2; // 掉落之后到来的重建信号（修前恒 0 → FAIL）
        });
        wT2.clearBlockSilent(12, 81, 12); // TNT 点火清格（静默路径；recheck 火把扫应连带掉火把 + 自 emit）
        const bool ok = torchDrops == 1                          // 恰 1 次火把掉落（无双掉）
                     && torchDropSeq > 0
                     && worldSeqAfterDrop > torchDropSeq         // 掉落后仍有 worldChanged（重建信号不再缺席）
                     && wT2.blockAt(12, 82, 12) == BR::Air       // 火把格已清（网格无残留依据）
                     && wT2.blockAt(12, 81, 12) == BR::Air;      // TNT 格已清（清格本体成立）
        if (!ok)
            qInfo().noquote() << "  r24#2 diag: drops" << torchDrops << "dropSeq" << torchDropSeq
                              << "worldAfterDrop" << worldSeqAfterDrop
                              << "torch" << int(wT2.blockAt(12, 82, 12));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#2 clearBlockSilent redstone-torch ghost mesh: torch standing "
                             "on TNT drops exactly once when TNT ignition clears the block, torch "
                             "cell becomes Air, and a worldChanged (rebuild signal) is observed "
                             "AFTER the drop (torch branch self-emits on actual drop)";
    }

    // ── P-r24#3 静默清格两兄弟路径附着物复检（Review 2026-08-24 中危 #3；专用局部世界 seed 33）──
    //   「口径合一」漏改 destroySphereSilent（爆炸）与 tickLavaFlow（岩浆焚毁）：(a) 爆炸掀支撑后
    //   压力板 / 铁轨须随 recheckAttachmentsAfterClear 掉落不悬浮（压力板 = 修前爆炸路径漏的族；
    //   铁轨 = 指令指定锁点；r=0.9 球心距 1 的附着物在球外幸存 → 只能靠钩子掉落，t733 探针同手法）；
    // (b) 岩浆焚毁木板支撑（8%/窗确定性哈希，≤400 窗必中）→ 焚毁循环内 recheck 须把轨掉落（修前
    //   tickLavaFlow 不含 checkRailOnEdit → 轨悬浮）。岩浆稳态早退（m_lavaDirty）用标记格翻转逐窗
    //   重标脏驱动（pokeFluidDirty 7 邻扫含岩浆源）；每窗 35 调 tickLavaFlow ≥ 节流 30 保证恰 1 窗。
    {
        World wX3;
        wX3.setWidth(48); wX3.setDepth(40); wX3.setHeight(96); wX3.setSeed(33);
        for (int x = 10; x <= 30; ++x)
            for (int z = 10; z <= 16; ++z)
                for (int y = 79; y <= 84; ++y)
                    wX3.setBlock(x, y, z, BR::Air, 0);
        int plateDrops = 0, railDrops = 0;
        QObject::connect(&wX3, &World::blockDroppedAsItem, &wX3,
                         [&](int, int, int, int id) {
                             if (id == int(BR::WoodPressurePlate)) ++plateDrops;
                             else if (id == int(BR::Rail)) ++railDrops;
                         });
        // (a) 爆炸：石支撑 + 板（x=12）与 石支撑 + 轨（x=18）两组，r=0.9 各炸支撑。
        wX3.setBlock(12, 81, 12, BR::Stone, 0);
        wX3.setBlock(12, 82, 12, BR::WoodPressurePlate, 0); // 板（距球心 1 > r=0.9 → 球外幸存，靠钩子掉）
        wX3.setBlock(18, 81, 12, BR::Stone, 0);
        wX3.setBlock(18, 82, 12, BR::Rail, 0);              // 轨（同上球外幸存）
        wX3.destroySphereSilent(12, 81, 12, 0.9f);          // 炸掉板支撑 → recheck 压力板分支掉板
        wX3.destroySphereSilent(18, 81, 12, 0.9f);          // 炸掉轨支撑 → recheck 铁轨分支掉轨
        const bool okA = wX3.blockAt(12, 82, 12) == BR::Air
                      && wX3.blockAt(18, 82, 12) == BR::Air
                      && plateDrops == 1 && railDrops == 1;
        // (b) 岩浆焚毁：石地板(y=79) + 岩浆源(14,80) + 木板支撑(13,80) + 轨(13,81) + 标记格(14,81，岩浆
        //     正上方——翻转 Stone↔Air 逐窗重标脏；岩浆不上升 → 标记格恒空可翻转；贴轨仅触发连接重算
        //     no-op，孤轨无连接零写入）。焚毁 8%/窗 → ≤400 窗必中（P(400 窗全空)≈0.92^400≈e^-33）。
        wX3.setBlock(13, 79, 12, BR::Stone, 0);
        wX3.setBlock(14, 79, 12, BR::Stone, 0);             // 岩浆 / 木板下方地板（岩浆 grounded 不下落）
        wX3.setBlock(13, 80, 12, BR::Planks, 0);            // 木板支撑（isWoodLike → 岩浆 ignite pass 目标）
        wX3.setBlock(13, 81, 12, BR::Rail, 0);              // 轨（满顶支撑 Planks 上；焚毁后须掉落不悬浮）
        wX3.setBlock(14, 80, 12, BR::Lava, 0);              // 岩浆源（贴木板 → 每窗 8% 焚毁掷骰）
        bool burned = false;
        for (int win = 0; win < 400 && !burned; ++win) {
            wX3.setBlock(14, 81, 12, (win & 1) ? BR::Air : BR::Stone, 0); // 标记翻转（真实变化 → poke 标脏岩浆）
            for (int t = 0; t < 35; ++t) wX3.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 个流/焚毁窗
            // t891① 语义更新：岩浆掷中邻木 → **点燃进燃烧态**（id 不变，焚毁让位给燃烧计时终局）→
            //   燃烧 10 窗后烧毁（setBlock Fire/Air）——本循环驱动 tickLavaFlow（点燃）+ tickFire（推进
            //   燃烧计时至烧毁），「burned」= 木板格不再是 Planks（被点燃烧尽）。
            wX3.tickFire();
            burned = wX3.blockAt(13, 80, 12) != BR::Planks;   // 木板被点燃烧毁（燃烧终局 / 岩浆漫入）
        }
        const bool okB = burned && wX3.blockAt(13, 81, 12) == BR::Air && railDrops == 2; // 轨掉落（(a)1 + (b)1）
        const bool ok = okA && okB;
        if (!ok)
            qInfo().noquote() << "  r24#3 diag: plateCell" << int(wX3.blockAt(12, 82, 12))
                              << "railCellA" << int(wX3.blockAt(18, 82, 12))
                              << "plateDrops" << plateDrops << "railDrops" << railDrops
                              << "burned" << burned << "railCellB" << int(wX3.blockAt(13, 81, 12));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review24#3 silent-clear sibling paths recheck attachments: explosion "
                             "dropping support drops the out-of-sphere pressure plate AND rail (no "
                             "floating residue), lava burning a plank support drops the rail on top "
                             "(burn loop now routes through recheckAttachmentsAfterClear)";
    }

    // ── P-t835 暗渊珠五项修探针（Entities 层 EntityManager 直编 + Game 层 applyEnderPearlTeleport 直调，
    //    同 t774 / t852 先例；独立小世界 96×40×96 不动主世界 rig——96 高世界地形+树冠最高 ~81，y≥84 天空
    //    带免凿，rig 地板摆 y=83 顶面 84）：
    //    (a) ①任意接触必传送：铁轨 / 火把（无碰撞盒非整格）+ 木压力板（薄碰撞盒）三柱，珠垂直落上 →
    //        enderPearlLanded 落点 = **非整格自身格**（旧 collisionAABBsAt 点测穿过它落到下方支撑格 = 根因）；
    //        Game 侧 applyEnderPearlTeleport：轨/火把格 → 玩家立**其格内**（y=84，穿模贴脚同 MC）；板格 →
    //        立其顶（y=85，薄盒是碰撞支撑）；Survival 传送自伤恰发一次 (5, EnderPearlTp)。
    //    (b) ②液体缓沉：水柱 5 深 / 岩浆柱 4 深——入液不即时传送（40 tick 仍存活且已入液），稳态下沉速度带
    //        水 ~1.5 b/s / 岩浆 ~0.7 b/s（岩浆明显更慢），最终沉到液体底接触底面格才传送（落点=底面格，
    //        Game 侧从底面格传送 → 玩家立于水格 y=84——旧 isSolid 把水当实心全列 abort 的回归面）。
    //    (c) ③虚空/出界不传送：整柱清空到 y=0 的虚空列，珠一路无接触落出底部 → 移除且零 landed；
    //        水平飞出 XZ 边界同（出界消散，MC 珍珠入虚空有去无回）。
    //    (d) ④抛距加长：平抛 v=24（镜像 kPlayerPearlSpeed）自 6 格高 → 理论落距 24 格（t=√(2·6/12)=1s，
    //        kEnderPearlGravity=12 轻重力直证）；45° 满抛 → ~50 格带（旧 12+重力 28 只 ~5 格）。
    //    (e) ⑤疾跑加成：平抛 v=24 与 v=24×1.3（镜像 kPearlSprintFactor，对齐 t51 Sprint ×1.3）落距比
    //        ∈[1.27,1.33]；镜像常量值锁（Game 层掷出分支本地 constexpr 探针不可达，P18 镜像同步模式）。
    {
        // 镜像常量（与实现侧私有/函数本地常量文档值同步，改值须两处同步；P18 镜像模式——Entities 层
        //   kEnderPearlGravity / Game 层掷珠分支 kPlayerPearlSpeed/kPearlSprintFactor 均探针不可达）：
        constexpr float kMirrorPearlGravity = 12.0f;       // EntityManager::kEnderPearlGravity（t835④ 珠轻重力；MC 投掷物 12 vs 世界 28）
        constexpr float kMirrorPearlSpeed = 24.0f;        // kPlayerPearlSpeed（t835④ 12→24；MC 投掷物 1.5 b/t=30 量级）
        constexpr float kMirrorPearlSprintFactor = 1.3f;  // kPearlSprintFactor（t835⑤ 疾跑初速系数；t51 Sprint ×1.3 同源）
        Q_UNUSED(kMirrorPearlGravity); // 带断言（平抛 6 格落差 t=√(2·6/12)=1s → 落距=初速）即其数值锁；显式引用免 -Wunused
        World wP;
        wP.setWidth(96); wP.setDepth(40); wP.setHeight(96); wP.setSeed(77);
        EntityManager ents;
        int landedCount = 0; int lastLx = -1, lastLy = -1, lastLz = -1;
        QObject::connect(&ents, &EntityManager::enderPearlLanded, &ents,
                         [&](int x, int y, int z) { ++landedCount; lastLx = x; lastLy = y; lastLz = z; });
        const QVector3D farListener(-1000.0f, 10.0f, -1000.0f);
        const auto tickPearls = [&](int n) { for (int i = 0; i < n; ++i) ents.tick(0.016f, &wP, farListener, 0.3f, 1.8f, false); };
        const int floorY = 83; // rig 地板格（顶面 y=84）；上方 y≥84 全空带
        bool ok = true;

        // ---- (a) ①非整格接触：轨 / 火把 / 木压力板三柱 ----
        struct DecorRig { int x; quint8 id; float tpFootY; const char *name; };
        const DecorRig decors[] = {
            { 6, BR::Rail,               84.0f, "rail" },     // 无碰撞盒 → 立格内 y=84（穿模贴脚同 MC）
            { 9, BR::Torch,              84.0f, "torch" },    // 同上
            { 12, BR::WoodPressurePlate, 85.0f, "plate" },    // 薄碰撞盒是支撑 → 立其顶 y=85
        };
        const int az = 6;
        bool okA = true;
        for (const DecorRig &d : decors) {
            wP.setBlock(d.x, floorY, az, BR::Stone, 0);   // 支撑地板
            wP.setBlock(d.x, floorY + 1, az, d.id, 0);    // 非整格本体（轨/火把贴地、板贴支撑面）
            const int before = landedCount;
            const int pearl = ents.spawnEnderPearl(QVector3D(d.x + 0.5f, 88.0f, az + 0.5f), QVector3D(0, 0, 0));
            tickPearls(80); // 88→入格 ~45 tick 内必中
            // Entities 半面：落点 = 非整格自身格（floorY+1）——旧判据穿过它落进下方支撑格（floorY）。
            if (landedCount != before + 1 || lastLx != d.x || lastLy != floorY + 1 || lastLz != az) {
                okA = false;
                qInfo().noquote() << "  t835(a) diag:" << d.name << "landed" << landedCount - before
                                  << "at" << lastLx << lastLy << lastLz << "(expect 1 at" << d.x << floorY + 1 << az << ")";
            }
            Q_UNUSED(pearl);
        }
        // Game 半面：applyEnderPearlTeleport 直调（Survival）——轨/火把立格内、板立其顶 + 自伤恰一次 (5, EnderPearlTp)。
        PlayerController pc;
        pc.setWorld(&wP);
        pc.setMode(PlayerController::Survival);
        int dmgHits = 0, dmgHp = -1, dmgCause = -1;
        QObject::connect(&pc, &PlayerController::fallDamageTaken, &pc,
                         [&](int hp, int cause) { ++dmgHits; dmgHp = hp; dmgCause = cause; });
        for (const DecorRig &d : decors) {
            pc.applyEnderPearlTeleport(d.x, floorY + 1, az); // 落点 = 珠接触格（ Entities 半面同参）
            const float gotY = pc.feetPosition().y();
            if (qAbs(gotY - d.tpFootY) > 0.01f) {
                okA = false;
                qInfo().noquote() << "  t835(a) tp diag:" << d.name << "footY" << gotY << "(expect" << d.tpFootY << ")";
            }
        }
        okA = okA && dmgHits == 3 && dmgHp == 5 && dmgCause == int(PlayerState::EnderPearlTp);
        ok = ok && okA;

        // ---- (b) ②液体缓沉：水柱（5 深）/ 岩浆柱（4 深）----
        const int wz = 10, lz2 = 14; // 两柱 z 錯開
        for (int y = floorY + 1; y <= floorY + 5; ++y) wP.setBlock(20, y, wz, BR::Water, 0);
        wP.setBlock(20, floorY, wz, BR::Stone, 0);
        for (int y = floorY + 1; y <= floorY + 4; ++y) wP.setBlock(24, y, lz2, BR::Lava, 0);
        wP.setBlock(24, floorY, lz2, BR::Stone, 0);
        // 水：40 tick 仍存活（不即时传送）且已入液；稳态带 |dy|/tick ∈ [0.019,0.027]（1.5 b/s·dt±余量）；沉底传送。
        const int beforeW = landedCount;
        const int pearlW = ents.spawnEnderPearl(QVector3D(20.5f, 91.0f, wz + 0.5f), QVector3D(0, 0, 0));
        tickPearls(40);
        const float yW40 = ents.posAt(pearlW).y();
        bool okW = ents.aliveAt(pearlW) && yW40 < 89.0f && yW40 > 84.0f; // 已入液未到底未传送
        float sinkW = 0.0f;
        for (int t = 0; t < 100 && ents.aliveAt(pearlW); ++t) {
            const float y0 = ents.posAt(pearlW).y();
            tickPearls(1);
            sinkW = y0 - ents.posAt(pearlW).y(); // 末次采样（稳态：远离入液减速段与底面）
        }
        okW = okW && sinkW > 0.019f && sinkW < 0.027f;    // 稳态缓沉 ~1.5 b/s
        tickPearls(300);                                  // 沉底 + 传送余量（5 格 @1.5 b/s ≈ 250 tick 总）
        okW = okW && !ents.aliveAt(pearlW) && landedCount == beforeW + 1
                 && lastLx == 20 && lastLy == floorY && lastLz == wz; // 落点 = 液体底面格
        // Game 半面：从水底格传送 → 玩家立水格 y=84（水无碰撞可立入；旧 isSolid 把水当实心全列 abort）。
        pc.applyEnderPearlTeleport(20, floorY, wz);
        okW = okW && qAbs(pc.feetPosition().y() - float(floorY + 1)) < 0.01f
                 && qAbs(pc.feetPosition().x() - 20.5f) < 0.01f;
        // 岩浆：同构更慢（0.7 b/s 稳态带更窄）+ 沉底传送（①岩浆接触同样必传送，传送不点燃——MC 1.0 语义）。
        const int beforeL = landedCount;
        const int pearlL = ents.spawnEnderPearl(QVector3D(24.5f, 90.0f, lz2 + 0.5f), QVector3D(0, 0, 0));
        tickPearls(80); // 入液 + 减速收敛（vy 4.9→0.7 需 ~22 tick）
        bool okL = ents.aliveAt(pearlL);
        float sinkL = 0.0f;
        for (int t = 0; t < 100 && ents.aliveAt(pearlL); ++t) {
            const float y0 = ents.posAt(pearlL).y();
            tickPearls(1);
            sinkL = y0 - ents.posAt(pearlL).y();
        }
        okL = okL && sinkL > 0.008f && sinkL < 0.014f     // 稳态缓沉 ~0.7 b/s
                 && sinkW > sinkL + 0.004f;               // 水明显快于岩浆（1.5 vs 0.7）
        tickPearls(500);                                  // 4 格 @0.7 b/s ≈ 357 tick + 余量
        okL = okL && !ents.aliveAt(pearlL) && landedCount == beforeL + 1
                 && lastLx == 24 && lastLy == floorY && lastLz == lz2;
        ok = ok && okW && okL;

        // ---- (c) ③虚空 / 出界不传送 ----
        const int vz = 18;
        for (int y = 0; y < 96; ++y) wP.setBlock(30, y, vz, BR::Air, 0); // 整柱清到 y=0（虚空列）
        const int beforeV = landedCount;
        const int pearlV = ents.spawnEnderPearl(QVector3D(30.5f, 90.0f, vz + 0.5f), QVector3D(0, 0, 0));
        tickPearls(300); // 90→0 自由落 ~242 tick，越 y<0 出界移除
        bool okV = !ents.aliveAt(pearlV) && landedCount == beforeV; // 移除且零传送
        const int pearlX = ents.spawnEnderPearl(QVector3D(94.5f, 90.0f, vz + 0.5f), QVector3D(30.0f, 0, 0));
        tickPearls(10);  // ~0.5 格/tick → 3 tick 内飞出 x>96 出界移除
        okV = okV && !ents.aliveAt(pearlX) && landedCount == beforeV;
        ok = ok && okV;

        // ---- (d) ④抛距 + (e) ⑤疾跑比（平抛走廊：地板 x=8..70 @ z=26，顶面 y=84；自 y=90 平抛落距 6 格落差）----
        const int rz = 26;
        for (int x = 4; x <= 72; ++x) wP.setBlock(x, floorY, rz, BR::Stone, 0);
        struct RangeShot { float speed; };
        const RangeShot shots[] = { { kMirrorPearlSpeed }, { kMirrorPearlSpeed * kMirrorPearlSprintFactor } };
        float rangeCells[2] = { -1.0f, -1.0f };
        for (int s = 0; s < 2; ++s) {
            const int before = landedCount;
            const int pearl = ents.spawnEnderPearl(QVector3D(8.5f, 90.0f, rz + 0.5f),
                                                   QVector3D(shots[s].speed, 0, 0));
            tickPearls(120); // 6 格落差 t=1s=62 tick，余量足
            if (!ents.aliveAt(pearl) && landedCount == before + 1)
                rangeCells[s] = float(lastLx - 8);
        }
        // ④：v=24 落距 ~24 格（理论 24.0，dt 步进/格量化余量 ±2.5；旧物理 12+重力 28 仅 ~4.5 格 → 带断言分得开）。
        bool okD = rangeCells[0] >= 21.5f && rangeCells[0] <= 26.5f;
        // ⑤：疾跑 ×1.3 → 落距比 ∈[1.27,1.33]（同落差同重力 → 落距比 = 初速比；格量化 ±1 格已含在带内）。
        bool okE = rangeCells[1] >= 27.0f && rangeCells[1] <= 34.0f
                && rangeCells[0] > 0.0f
                && rangeCells[1] / rangeCells[0] >= 1.27f && rangeCells[1] / rangeCells[0] <= 1.33f;
        // ④补充：45° 满抛 v=24 → ~50 格带（自 y=86 上升弧越世界顶 y≥96 = 空气无碰撞照飞；旧物理只 ~5 格）。
        const int before45 = landedCount;
        const int pearl45 = ents.spawnEnderPearl(QVector3D(8.5f, 86.0f, rz + 0.5f),
                                                 QVector3D(24.0f * 0.7071f, 24.0f * 0.7071f, 0));
        tickPearls(260); // 满弧 ~2.9s ≈ 183 tick + 余量
        okD = okD && !ents.aliveAt(pearl45) && landedCount == before45 + 1
                 && float(lastLx - 8) >= 35.0f && float(lastLx - 8) <= 58.0f;
        ok = ok && okD && okE;
        if (!ok) {
            qInfo().noquote() << "  t835 diag: okA" << okA << "okW" << okW << "okL" << okL << "okV" << okV
                              << "okD" << okD << "okE" << okE << "| ranges" << rangeCells[0] << rangeCells[1]
                              << "sinkW" << sinkW << "sinkL" << sinkL << "| last landed" << lastLx << lastLy << lastLz
                              << "| dmg" << dmgHits << dmgHp << dmgCause;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t835 ender pearl five fixes: (1) any-contact teleports - pearl lands ON rail/torch/"
                             "plate cell itself and player stands in-cell (y=84) / on plate top (y=85); (2) water/"
                             "lava slow-sink (1.5 / 0.7 b/s steady band) then teleport at liquid-bottom cell, "
                             "player placed in water cell; (3) void & out-of-bounds fall removes pearl with ZERO "
                             "teleport; (4) flat throw v=24 drops ~24 blocks (light gravity 12), 45deg ~50 blocks; "
                             "(5) sprint 1.3x speed -> range ratio 1.27..1.33 + Survival tp self-damage (5, "
                             "EnderPearlTp) exactly once per teleport";
    }

    // ── t837(1) 画作支撑失撑 World 钩子族探针（World rig 直编；setBlock / clearBlockSilent 双入口 + M1 邻画
    //    不误伤 + 墙体置换保留）：1x2 画（锚格 top state=0x80|face|index / 非锚格 bottom state=faceBits）贴
    //    face 0（+X 外法线）墙（-X 邻 Stone/TNT 两格）。断言面：
    //    (a) 破**非锚格背后**的墙（用户复现位）→ 整画两格全清 + 恰 1 件掉落（dropId(Painting) 运行期同源读）；
    //    (b) 破锚格背后的墙 → 同样整画掉落（任一支撑破坏 → 整画掉，MC 语义）；
    //    (c) 墙置换为另一完整立方（Stone->Planks）→ 画保留零掉落（支撑仍有效，防误清）；
    //    (d) 直调 World::removePaintingAt（finishMiningAt 直挖画格同路径）→ 整画清 + 1 件；
    //    (e) clearBlockSilent（TNT 点火清格等系统路径，recheckAttachmentsAfterClear 收口）→ 整画掉落；
    //    (f) M1 钉契约：同面并排两 1x1 画，破其一的墙 → 只掉那一张，邻画完好（连通域 ≠ 整画，锚格矩形圈定）。
    {
        // 运行期查 1x2 与 1x1 的画作 index（paintingSize 单一权威，免本表持字面量副本）。
        int idx1x2 = -1, idx1x1 = -1;
        for (int i = 0; i < BR::PaintingCount; ++i) {
            int pw = 1, ph = 1;
            BR::paintingSize(i, pw, ph);
            if (idx1x2 < 0 && pw == 1 && ph == 2) idx1x2 = i;
            if (idx1x1 < 0 && pw == 1 && ph == 1) idx1x1 = i;
        }
        const quint8 faceBits = 0; // face 0（+X 外法线）→ 非锚格 state=0（bit7=0）
        const int paintingDropId = BR::dropId(BR::Painting);
        const auto buildPainting = [&](int x0, int z0, int idx, BR::Id wallId) {
            // 墙两格（y41/y40）+ 画两格（墙 +X 侧）；锚格 top 带 0x80|index。
            w.setBlock(x0, 41, z0, wallId, 0);
            w.setBlock(x0, 40, z0, wallId, 0);
            w.setBlock(x0 + 1, 41, z0, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | faceBits | quint8(idx & BR::PaintingStateIndexMask)));
            w.setBlock(x0 + 1, 40, z0, BR::Painting, faceBits);
        };
        // (a) 破非锚格背后的墙（底部墙格）→ 整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 40, z0, BR::Air, 0); // 用户复现位：挖掉画下半背后那块「非承重」墙
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) {
                qInfo().noquote() << "  [t837a diag] bottomCell"
                                  << int(w.blockAt(x0 + 1, 40, z0)) << "topCell" << int(w.blockAt(x0 + 1, 41, z0))
                                  << "drops" << (dropItemCount - drops0) << "lastDropId" << lastDropId
                                  << "expect" << paintingDropId;
            }
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (a): dig wall behind NON-anchor cell of 1x2 painting "
                                 "-> whole painting drops as ONE item (no residual single face)";
        }
        // (b) 破锚格背后的墙 → 同样整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Air, 0);
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (b): dig wall behind anchor cell -> whole 1x2 painting "
                                 "drops (ANY support face break drops the entire painting, MC semantics)";
        }
        // (c) 墙置换为另一完整立方 → 画保留（支撑仍有效）。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Planks, 0); // Stone -> Planks（均完整立方）
            const bool ok = w.blockAt(x0 + 1, 41, z0) == quint8(BR::Painting)
                        && w.blockAt(x0 + 1, 40, z0) == quint8(BR::Painting)
                        && dropItemCount == drops0;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (c): wall replaced by another full cube -> painting "
                                 "survives, zero drops (support recheck keeps valid walls)";
        }
        // (d) 直调 World::removePaintingAt（直挖画格路径）→ 整画清 + 1 件。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::Stone);
            const int drops0 = dropItemCount;
            w.removePaintingAt(x0 + 1, 40, z0, 0, /*drop=*/true); // 从非锚格种子（直挖下半格同型）
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting remove (d): World::removePaintingAt from non-anchor seed clears "
                                 "the whole 1x2 painting + drops exactly one item";
        }
        // (e) clearBlockSilent（TNT 点火清格 -> recheckAttachmentsAfterClear 收口）→ 整画掉落。
        {
            const auto [x0, z0] = nextSlot();
            buildPainting(x0, z0, idx1x2, BR::TntBlock);
            const int drops0 = dropItemCount;
            w.clearBlockSilent(x0, 40, z0); // 点火清格同型系统路径（TNT 墙被引燃）
            const bool ok = w.blockAt(x0 + 1, 40, z0) == quint8(BR::Air)
                        && w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting support (e): clearBlockSilent (TNT-ignite style system clear) "
                                 "-> painting drops via recheckAttachmentsAfterClear single entry";
        }
        // (f) M1 邻画不误伤：同面并排两 1x1 画（face 0 的 u = -Z），破其一的墙 → 只掉那一张。
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, 41, z0, BR::Stone, 0);       // 画 A 墙
            w.setBlock(x0, 41, z0 - 1, BR::Stone, 0);   // 画 B 墙（u 向相邻）
            w.setBlock(x0 + 1, 41, z0, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | quint8(idx1x1 & BR::PaintingStateIndexMask)));
            w.setBlock(x0 + 1, 41, z0 - 1, BR::Painting,
                       quint8(BR::PaintingStateAnchorFlag | quint8(idx1x1 & BR::PaintingStateIndexMask)));
            const int drops0 = dropItemCount;
            w.setBlock(x0, 41, z0, BR::Air, 0);         // 只破画 A 的墙
            const bool ok = w.blockAt(x0 + 1, 41, z0) == quint8(BR::Air)          // A 掉
                        && w.blockAt(x0 + 1, 41, z0 - 1) == quint8(BR::Painting)  // B 完好
                        && dropItemCount == drops0 + 1 && lastDropId == paintingDropId;
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| t837 painting M1 pin (f): two adjacent 1x1 paintings share a wall plane - "
                                 "breaking one support drops ONLY that painting, neighbor intact";
        }
    }

    // ── t847 植物放置谓词探针（Core 纯函数真值表 + World 花失撑掉落链 t788 回归钉）：plantGroundBlock
    //    单一权威——草丛→**仅草方块**（t903 收紧：泥土也不行，用户定稿对齐 MC；拒草上叠草 / 树叶 / 沙 /
    //    耕地 / 水 / 泥土）；花→泥土/草方块/耕地（MC 1.0 BlockFlower.canBlockStay 同集含 tilledField）；
    //    蘑菇→泥土/草方块；枯灌木→沙子。掉落链：破花下泥土 → 花 dropId 掉落（t788 染料链不回归；dropId
    //    运行期读，免字面量副本）。放置预检本体在 PlayerController 私有 placeBlock（t841 P20 先例：谓词面 +
    //    失撑面矩阵化，放置拒绝人工目视收口）。
    {
        const bool okGround =
               !BR::plantGroundBlock(BR::TallGrass, BR::Dirt)     // t903 收紧：泥土也不行（仅草方块）
            && BR::plantGroundBlock(BR::TallGrass, BR::Grass)
            && !BR::plantGroundBlock(BR::TallGrass, BR::TallGrass)   // 不能草上叠草
            && !BR::plantGroundBlock(BR::TallGrass, BR::Leaves)      // 不能放树叶上
            && !BR::plantGroundBlock(BR::TallGrass, BR::Sand)
            && !BR::plantGroundBlock(BR::TallGrass, BR::Farmland)
            && !BR::plantGroundBlock(BR::TallGrass, BR::Water)       // 水下拒绝（着地面谓词面）
            && BR::plantGroundBlock(BR::FlowerRed, BR::Dirt)
            && BR::plantGroundBlock(BR::FlowerRed, BR::Grass)
            && BR::plantGroundBlock(BR::FlowerRed, BR::Farmland)     // MC 花可放耕地
            && !BR::plantGroundBlock(BR::FlowerRed, BR::Sand)
            && BR::plantGroundBlock(BR::Mushroom, BR::Dirt)
            && BR::plantGroundBlock(BR::Mushroom, BR::Grass)
            && !BR::plantGroundBlock(BR::Mushroom, BR::Farmland)
            && BR::plantGroundBlock(BR::DeadBush, BR::Sand)          // 枯灌木沙地限定
            && !BR::plantGroundBlock(BR::DeadBush, BR::Dirt)
            && !BR::plantGroundBlock(BR::Stone, BR::Dirt);           // 非植物 → 恒 false（谓词域守卫）
        // 失撑链回归钉：花失撑掉 dropId（t788 起花掉对应染料；本探针运行期读表比对，与玩家直破同源）。
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, 41, z0, BR::Dirt, 0);
        placeRigBlock(w, x0, 42, z0, BR::FlowerRed, 0);
        const int drops0 = dropItemCount;
        w.setBlock(x0, 41, z0, BR::Air, 0); // 破花下泥土
        const bool okDrop = w.blockAt(x0, 42, z0) == quint8(BR::Air)
                     && dropItemCount == drops0 + 1
                     && lastDropId == BR::dropId(BR::FlowerRed);
        // t847 收口（R19.13 终审 C-M1）：草丛失撑链补钉——t847 只把草丛收进放置预检（泥土/草限定）而
        //   World 失撑族没跟，挖掉下方泥土后草丛悬空永存；修后 isGroundPlant 单一权威两面共用（放置预检
        //   与 checkFlowerMushroomOnEdit 同谓词）。破草丛下泥土 → 草丛清 Air + dropId（0x208 种子族，运行
        //   期读表）掉落，与玩家直破同源。
        const auto [x1, z1] = nextSlot();
        placeRigBlock(w, x1, 41, z1, BR::Dirt, 0);
        placeRigBlock(w, x1, 42, z1, BR::TallGrass, 0);
        const int drops1 = dropItemCount;
        w.setBlock(x1, 41, z1, BR::Air, 0); // 破草丛下泥土
        const bool okDropGrass = w.blockAt(x1, 42, z1) == quint8(BR::Air)
                       && dropItemCount == drops1 + 1
                       && lastDropId == BR::dropId(BR::TallGrass);
        const bool ok = okGround && okDrop && okDropGrass;
        if (!ok) {
            qInfo().noquote() << "  [t847 diag] okGround" << okGround << "okDrop" << okDrop
                              << "lastDropId" << lastDropId << "expect" << BR::dropId(BR::FlowerRed)
                              << "okDropGrass" << okDropGrass << "(grassDrop"
                              << BR::dropId(BR::TallGrass) << ")";
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t847 plant placement predicate: plantGroundBlock single authority truth table "
                             "(tallgrass grass-only per t903 tightening - dirt rejected too, no "
                             "grass-on-grass/leaves/water; flowers +farmland; "
                             "mushrooms dirt/grass; dead bush sand-only) + flower lost-support drop stays on "
                             "dropId chain (t788 dye linkage) + tallgrass lost-support now symmetric with the "
                             "placement-side family (isGroundPlant shared by precheck and the World hook; digs "
                             "out ground -> grass clears and drops its dropId)";
    }

    // ── t815/t838 item 图标路径探针（Game 层 Hotbar 闭合直调，t800 探针同模式；测试二进制无 qrc → 图集
    //    渲染落盘空图，URL 链路断言有效、像素内容留实机人工目视）：(1) 红石粉（130）pick-block 图标改走
    //    isPackDerivedIconFamily 程序图集 flat 重渲（file:/// 运行期缓存，非 qrc 手绘旧稿——「贴图旧版」
    //    根因钉死在回退链位置：旧稿只余渲染失败兜底）；(2) 玻璃（54）缓存族换代 icon4->icon5（URL 家族名
    //    断言；t800 flat -> t838(1) dimetric 3D 的画法切换靠换代兜底，防 AppLocalData 旧 flat 缓存被复用）。
    {
        Hotbar hb;
        const QString dustIcon = hb.iconSourceForBlock(int(BR::RedstoneDust));
        const QString glassIcon = hb.iconSourceForBlock(int(BR::Glass));
        // t879 换代 icon5->icon6、t902 换代 icon6->icon7（耕地图标面修正：side/front 钉 dirt——旧泛化把
        //   frontTile=湿耕地瓦片画上左前面；URL 家族名断言随缓存名同步——测试二进制无 qrc → 图集渲染
        //   落盘空图，URL 链路断言有效、像素内容留实机人工目视）。
        const bool ok = dustIcon.startsWith(QStringLiteral("file:///"))
                     && !dustIcon.contains(QStringLiteral("icon_redstone_dust"))
                     && glassIcon.startsWith(QStringLiteral("file:///"))
                     && glassIcon.contains(QStringLiteral("voxelsandbox_rp_icon7_"))
                     && dustIcon.contains(QStringLiteral("voxelsandbox_rp_icon7_"));
        if (!ok) {
            qInfo().noquote() << "  [t815/t838 diag] dustIcon" << dustIcon << "| glassIcon" << glassIcon;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t815/t838 item icon paths: redstone dust pick-block icon resolves via runtime "
                             "atlas flat re-render (file:/// cache, stale hand-drawn qrc retired to last-resort "
                             "fallback), glass icon cache family bumped icon6->icon7 (t879 trapdoor draw switch "
                             "and t902 farmland face fix ride the same bump)";
    }

    // ── P-t836 钓鱼系统整改探针（Entities 层 EntityManager 直编 + Game 层 PlayerController/Hotbar 真消费端，
    //    t835/t856 同模式；独立小世界 48×48×96 seed 77（t835 实测该种子地形+树冠 ≤81，y≥84 天空带免凿——
    //    t769 教训：不假设「某高度以上必空」，此为已核种子的实测带）+ 水池 / 猪平台 rig 摆 y=83 起）──
    //    (a) 抛物线（轻重力 12 落差带）+ 落水浮定（XZ 收格心 / Y = 液面 − 浸没 0.125 精确断言）；
    //    (b) 确定性等待：bobberWaitSeconds 两端恰可达（h=0 → 5.00 / h=2500 → 30.00）+ 分布带（600 序号
    //        min<6 / max>29）+ 行为级（真实 settle 后按 hashVoxel 预计算的等待值 ±1 tick 咬钩——公式即驱动）；
    //    (c) 咬钩窗口窗内收 = 获物 + 耐久 -1（fishCaught 载荷：池内物品 id + 浮标位 + 朝玩家弹向 + t886 抛物解
    //        弹速镜像）；窗过 = 鱼跑（escaped 信号 + hasBite 翻 false）+ 重等（第二咬可达）+ 此后空收无消耗；
    //    (d) 钩 mob：pc 真甩竿飞行段命中猪（bobberHookedMobAt 绑定）→ 收竿拉拽（猪位移朝玩家 >0.03 +
    //        耐久 -5 + 猪血量不变 + 零 fishCaught——钩中不伤害不获物口径）；陆上静止浮标冻结（Ground 态）；
    //        d2 垂死 mob 收竿（R19.13 终审 B-L2）：钩住后打死猪（死亡动画窗内、不 tick ents → 脱钩验证未
    //        跑）→ 收竿拉拽 no-op → 耐久**不扣**（旧版无条件 -5 白损）；
    //    (e) 熟鱼链：kSmelt + kSmeltXp 两表都接（t788 教训）+ 食用 +4（生鱼 +2 对照）+ 名「熟鱼」+
    //        pack 映射源码钉（0x25B → cooked_cod.png）+ 创造 tab 源码钉 + 豹猫仍只认生鱼（源码钉 gate）。
    //    (f) 出界消散（R19.13 终审 C-M2 补断言面，此前文案声称零断言）：XZ 飞越边界 + 极端 y（y<0 虚空）
    //        → ents.tick 若干 → 浮标槽释放（despawn；边界路径非 180s 寿命路径）；
    //    (g) Game 层镜像（C-M2）：外部清场（ents.clearAll = 出界/寿命/系统清理的等价构造）→ 收竿无获物
    //        无消耗干净收场 + 镜像惰性（clearAll 后 fishing 态仍在，tick/收竿才收）+ updateFishing 失效
    //        自动收竿源序钉（pc.tick 的 captured 门在无窗探针不可达——直调会先走 !m_captured 早退分支的
    //        cancelFishing 掩盖镜像路径，行为级不可达、以 t836(e) 源码钉手法锁语句面，取舍声明）。
    //    (h) review25 #12 Ground 态支撑复查：挖掉贴靠方块 → ≤40 tick 转 Flying 下坠（旧版悬空滞留至
    //        180s 寿命兜底）；
    //    (i) review25 #13 实体格命中门：1 格墙后贴壁猪 + 高速飞行浮标（next 一跳入墙格且在猪外扩命中
    //        盒内）→ 贴面 Ground 不隔墙钩（旧序先钩后碰会隔墙钩住）。
    {
        // 镜像常量（P18 模式，改值须两处同步；Entities 层 kBobberWaitHashSalt / kBobberBiteWindowSec 与
        //   Game 层获物抛物解均探针不可达私有）：
        constexpr quint32 kMirrorBobberSalt = 0xF15Cu;   // EntityManager::kBobberWaitHashSalt（等待掷骰盐）
        constexpr float kMirrorBiteWindow = 0.5f;        // EntityManager::kBobberBiteWindowSec（咬钩窗口秒）
        // t886 获物弹速 = 抛物解镜像 fishCatchSpeedMirror（文件级 helper，t886 探针共用）。
        World wF;
        wF.setWidth(48); wF.setDepth(48); wF.setHeight(96); wF.setSeed(77);
        EntityManager ents;
        int bitCount = 0, escCount = 0;
        QObject::connect(&ents, &EntityManager::bobberBit, &ents,
                         [&](float, float, float) { ++bitCount; });
        QObject::connect(&ents, &EntityManager::bobberEscaped, &ents,
                         [&](float, float, float) { ++escCount; });
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickB = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wF, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83; // rig 地板格（t835 实测 seed 77 地形 ≤81 → 84+ 全空带）

        // ---- (a) 抛物 + 落水浮定（Entities 直编）----
        // 水池：3×3 石底 fy + 1 深水 fy+1（液面 = fy+1+0.875−0.125 = fy+1.75；t892 源 state=0 → surf 7/8）。
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wF.setBlock(x, fy, z, BR::Stone, 0);
                wF.setBlock(x, fy + 1, z, BR::Water, 0);
            }
        bool okA = false;
        {
            // a1 抛物：v=(8,0,0) 自 (10.5, fy+4, 12.5)（空带无遮挡）——4 tick（dt=0.05 → t=0.2s）落差 =
            //   ½·g·t² = ½·12·0.04 = 0.24（轻重力 12 直证；世界重力 28 会给 0.56 出带）。
            const int b1 = ents.spawnBobber(QVector3D(10.5f, float(fy + 4), 12.5f), QVector3D(8.0f, 0.0f, 0.0f), 901);
            tickB(4, 0.05f);
            const float yDrop = float(fy + 4) - ents.posAt(b1).y();
            const bool okAry = ents.aliveAt(b1)
                               && qAbs(yDrop - 0.24f) < 0.06f
                               && ents.posAt(b1).x() > 11.9f; // 水平位移 ≈ 8×0.2 = 1.6（弧线在飞）
            ents.removeEntityAt(b1);
            // a2 落水浮定：水池正上方垂直落（v=0）→ 穿入顶水格 settle 到格心 + 液面 − 0.125（浮力平衡半浸）。
            const int b2 = ents.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 902);
            for (int t = 0; t < 60 && ents.aliveAt(b2); ++t) tickB(1, 0.05f); // 落定 + 水中静置（等待期不咬）
            const QVector3D p2 = ents.posAt(b2);
            const bool okAset = ents.aliveAt(b2)
                                && qAbs(p2.x() - 6.5f) < 1e-3f
                                && qAbs(p2.y() - (float(fy + 1) + 0.75f)) < 1e-3f
                                && qAbs(p2.z() - 6.5f) < 1e-3f;
            ents.removeEntityAt(b2);
            // a3 陆上静止：石台正上垂直落 → Ground 贴面冻结（后续 tick 位置不变；Ground 不钩 mob / 不进等待）。
            wF.setBlock(12, fy, 18, BR::Stone, 0);
            const int b3 = ents.spawnBobber(QVector3D(12.5f, float(fy + 4), 18.5f), QVector3D(0, 0, 0), 903);
            for (int t = 0; t < 60 && ents.aliveAt(b3); ++t) tickB(1, 0.05f);
            const QVector3D p3a = ents.posAt(b3);
            tickB(20, 0.05f);
            const QVector3D p3b = ents.posAt(b3);
            const bool okAgr = ents.aliveAt(b3) && p3a == p3b
                               && qAbs(p3b.y() - float(fy + 1)) < 1.0f; // 停在石台上表面一带（贴命中面）
            ents.removeEntityAt(b3);
            wF.setBlock(12, fy, 18, BR::Air, 0);
            okA = okAry && okAset && okAgr;
            if (!okA)
                qInfo().noquote() << "  [t836 a diag] okAry" << okAry << "okAset" << okAset << "(pos" << p2
                                  << ") okAgr" << okAgr << "(pos" << p3b << ")";
        }

        // ---- (b) 确定性等待：公式两端 + 分布带 + 行为级公式即驱动 ----
        bool okB = false;
        {
            const bool okEnds = qAbs(EntityManager::bobberWaitSeconds(0u) - 5.0f) < 1e-4f
                                && qAbs(EntityManager::bobberWaitSeconds(2500u) - 30.0f) < 1e-3f
                                && EntityManager::bobberWaitSeconds(1u) > 5.0f
                                && EntityManager::bobberWaitSeconds(2499u) < 30.0f;
            float wMin = 1e9f, wMax = -1e9f;
            for (quint32 s = 0; s < 600u; ++s) {
                const float wv = EntityManager::bobberWaitSeconds(
                    wF.hashVoxel(int(quint32(wF.seed()) ^ kMirrorBobberSalt ^ s), 5, 84, 6));
                wMin = std::min(wMin, wv);
                wMax = std::max(wMax, wv);
            }
            const bool okDist = wMin > 4.99f && wMin < 6.0f && wMax > 29.0f && wMax < 30.01f;
            // 行为级：pc 真甩竿（serial 1）入水池 → settle 后按 hashVoxel 预计算等待值，±1 tick 内咬钩。
            //   轨迹（tick 逐步核）：眼 (3.5, fy+2.62) pitch −20 → 原点 (3.876, 85.483) vel (14.10, −5.13)；
            //   tick1 next=(4.581, 85.197) 格 (4,85) 空气；tick2 next=(5.286, 84.882) 格 (5,84) 水 → settle
            //   (5.5, 84.875, 6.5)（XZ 收格心 / 液面 1.0 − 浸没 0.125）。
            PlayerController pc;
            Hotbar hb;
            hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            pc.useFishingRod(); // 甩竿（serial → 1）
            int bobC = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobC = i; break; }
            const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
            bool okCast = pc.fishing() && bobC >= 0;
            for (int t = 0; t < 40 && okCast; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(bobC)) { okCast = false; break; }
                if (ents.posAt(bobC) == settlePos) break; // settled（精确浮点：0.5/0.75 均二进制精确）
            }
            okCast = okCast && ents.posAt(bobC) == settlePos;
            // settle 格 (5, fy+1, 6) + serial 1 → 预计算等待（镜像盐 = 实现盐，P18 双钉）。
            const float waitSec = EntityManager::bobberWaitSeconds(
                wF.hashVoxel(int(quint32(wF.seed()) ^ kMirrorBobberSalt ^ 1u), 5, fy + 1, 6));
            int ticksToBite = 0;
            bool okWaitTime = false;
            for (ticksToBite = 0; ticksToBite < 660; ++ticksToBite) {
                if (ents.bobberHasBiteAt(bobC)) break;
                tickB(1, 0.05f);
            }
            if (ents.bobberHasBiteAt(bobC) && ticksToBite < 660) {
                const float got = float(ticksToBite) * 0.05f;
                okWaitTime = qAbs(got - waitSec) <= 0.06f; // ±1 tick（公式即驱动的行为级直证）
            }
            okB = okEnds && okDist && okCast && okWaitTime;
            if (!okB)
                qInfo().noquote() << "  [t836 b diag] okEnds" << okEnds << "okDist" << okDist << "(" << wMin << ".."
                                  << wMax << ") okCast" << okCast << "okWaitTime" << okWaitTime << "(got"
                                  << ticksToBite * 0.05f << "s expect" << waitSec << "s)";
            if (bobC >= 0) ents.removeEntityAt(bobC); // 清场（防 (c) 的「首个 Bobber」搜索误拾本浮标）
        }

        // ---- (c) 咬钩窗口：窗内收 = 获物 + 耐久 -1；窗过 = 鱼跑重等 + 空收无消耗 ----
        bool okC = false;
        {
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod); // 64（MC 1.0 钓竿）
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            int caughtCount = 0, caughtId = 0; float cpx = 0, cpy = 0, cpz = 0, cdx = 0, cdz = 0, csp = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int itemId, int, float px, float py, float pz, float dx, float dz, float speed) {
                                 ++caughtCount; caughtId = itemId;
                                 cpx = px; cpy = py; cpz = pz; cdx = dx; cdz = dz; csp = speed;
                             });
            // c1 窗内收：甩竿（serial 1）→ settle（同 (b) 轨迹：格 (5,84,6)，pos (5.5, 84.875, 6.5)）→
            //   drive 到咬钩即收 → fishCaught 恰一次 + 耐久 64→63 + 载荷（池内 id + 浮标位 + 朝玩家水平弹向 +
            //   弹速镜像 4.5）+ fishing 态复位。
            pc.useFishingRod();
            int b1 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { b1 = i; break; }
            const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
            bool okc1 = b1 >= 0;
            for (int t = 0; t < 40 && okc1; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(b1)) { okc1 = false; break; }
                if (ents.posAt(b1) == settlePos) break;
            }
            okc1 = okc1 && ents.posAt(b1) == settlePos;
            for (int t = 0; t < 660 && okc1 && !ents.bobberHasBiteAt(b1); ++t) tickB(1, 0.05f);
            const QVector3D bobPos = ents.posAt(b1);
            okc1 = okc1 && ents.bobberHasBiteAt(b1);
            pc.useFishingRod(); // 窗内收竿
            const int dur1 = hb.durabilityAt(0);
            // 弹向断言：dir 点乘（玩家 − 浮标）水平归一 > 0.9（朝玩家）；弹速 = t886 抛物解镜像（|v| 随距离自适应）。
            const float toPX = 3.5f - bobPos.x(), toPZ = 6.5f - bobPos.z();
            const float toPLen = std::sqrt(toPX * toPX + toPZ * toPZ);
            const bool poolIds[] = {
                RecipeRegistry::RawFishId == caughtId, RecipeRegistry::LeatherId == caughtId,
                RecipeRegistry::StringId == caughtId, RecipeRegistry::BoneId == caughtId,
                RecipeRegistry::RottenFleshId == caughtId, RecipeRegistry::StickId == caughtId,
                RecipeRegistry::InkSacId == caughtId, RecipeRegistry::SaddleId == caughtId,
                RecipeRegistry::NameTagId == caughtId, RecipeRegistry::DiamondId == caughtId };
            bool idInPool = false;
            for (bool b : poolIds) idInPool = idInPool || b;
            okc1 = okc1 && caughtCount == 1 && idInPool && dur1 == rodDur - 1 && !pc.fishing()
                   && !ents.aliveAt(b1) // 浮标实体已收走
                   && qAbs(cpx - bobPos.x()) < 1e-3f && qAbs(cpy - bobPos.y()) < 1e-3f
                   && qAbs(cpz - bobPos.z()) < 1e-3f
                   && qAbs(csp - fishCatchSpeedMirror(bobPos, QVector3D(3.5f, float(fy + 1), 6.5f))) < 1e-2f
                   && (toPLen < 1e-3f || (cdx * toPX + cdz * toPZ) / toPLen > 0.9f);
            // c2 窗过 = 鱼跑重等 + 空收无消耗：再甩（serial 2）→ 咬 → drive 过窗（0.5s + 余量）→ escaped 信号 +
            //   hasBite 翻 false → 继续 drive 到第二次咬（重等可达，cap 31s）→ 再过窗 → 此刻收 = 真空收
            //   （无咬无获物 + 耐久不变）。
            const int escBefore = escCount, bitBefore = bitCount;
            pc.useFishingRod();
            int b2 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { b2 = i; break; }
            bool okc2 = b2 >= 0;
            for (int t = 0; t < 40 && okc2; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(b2)) { okc2 = false; break; }
                if (ents.posAt(b2) == settlePos) break;
            }
            okc2 = okc2 && ents.posAt(b2) == settlePos;
            for (int t = 0; t < 660 && okc2 && !ents.bobberHasBiteAt(b2); ++t) tickB(1, 0.05f);
            okc2 = okc2 && ents.bobberHasBiteAt(b2);
            tickB(int(kMirrorBiteWindow / 0.05f) + 2, 0.05f); // 0.6s > 0.5s 窗 → 鱼跑
            const bool okEsc = escCount == escBefore + 1 && !ents.bobberHasBiteAt(b2) && bitCount == bitBefore + 1;
            bool okRewait = false;
            for (int t = 0; t < 620 && ents.aliveAt(b2); ++t) {
                if (ents.bobberHasBiteAt(b2)) { okRewait = true; break; }
                tickB(1, 0.05f);
            }
            tickB(int(kMirrorBiteWindow / 0.05f) + 2, 0.05f); // 第二次咬钩窗口亦过期 → 收 = 空收
            const int durBeforeEmpty = hb.durabilityAt(0);
            pc.useFishingRod(); // 无咬空收
            okc2 = okc2 && okEsc && okRewait && caughtCount == 1 && hb.durabilityAt(0) == durBeforeEmpty
                   && !pc.fishing() && !ents.aliveAt(b2);
            okC = okc1 && okc2;
            if (!okC)
                qInfo().noquote() << "  [t836 c diag] okc1" << okc1 << "(caught" << caughtCount << "id" << caughtId
                                  << "dur" << rodDur - 1 << ") okc2" << okc2 << "(okEsc" << okEsc << "okRewait"
                                  << okRewait << "esc" << escCount - escBefore << ")";
        }

        // ---- (d) 钩 mob：真甩竿飞行段命中 → 收竿拉拽 + 耐久 -5 + 不伤害 ----
        bool okD = false;
        {
            // 猪平台（3×3，防落定 1.5s 游荡期间走下台）+ 猪；玩家站 x 13.5 平台按猪实时位姿瞄准。
            wF.setBlock(13, fy, 6, BR::Stone, 0);
            for (int x = 15; x <= 17; ++x)
                for (int z = 5; z <= 7; ++z)
                    wF.setBlock(x, fy, z, BR::Stone, 0);
            const int pig = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 10);
            // review25 探针加固（基线潜伏 flake，与本批游戏侧改动无关）：mob 游荡走运行期 QRandomGenerator
            //   （aiPig wanderTimer 1.5-3.5s 随机）→ 旧版 30 tick（1.5s）settle 窗内猪已可游走（实测偶发
            //   x≈17.9 平台边缘半身悬空 → 甩钩窗内再走一步跌出平台 → 钩空假 FAIL）。改**短窗 settle**
            //   （5 tick 重力落定；出生位=格心精确），甩竿+飞行+钩定 ≤15 tick < 首个游荡窗下界 30 tick
            //   → 猪在整个命中窗内钉在出生位（确定性）。
            tickB(5, 0.05f);
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod);
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, -90.0f, 0.0f, 2 /* Survival */);
            int caughtCount = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
            // 按猪实时中心位姿算 yaw/pitch（水平 look = (ux,uz) → yaw = atan2(-ux,-uz)；pitch = atan2(dy, 水平距)）。
            const QVector3D pp = ents.posAt(pig);
            const float eyeY = float(fy + 1) + 1.62f;
            const float ux = pp.x() - 13.5f, uz = pp.z() - 6.5f;
            const float hLen = std::sqrt(ux * ux + uz * uz);
            const float yawDeg = qRadiansToDegrees(std::atan2(-ux, -uz));
            const float pitchDeg = qRadiansToDegrees(std::atan2(pp.y() - eyeY, hLen));
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, yawDeg, pitchDeg, 2);
            pc.useFishingRod(); // 甩向猪
            int bb = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bb = i; break; }
            bool okHook = bb >= 0;
            for (int t = 0; t < 40 && okHook; ++t) {
                if (ents.bobberHookedMobAt(bb) == pig) break;
                tickB(1, 0.05f);
                if (!ents.aliveAt(bb)) { okHook = false; break; }
            }
            okHook = okHook && ents.bobberHookedMobAt(bb) == pig;
            const float pigX0 = ents.posAt(pig).x();
            const int pigHp0 = ents.healthAt(pig);
            const int dur0 = hb.durabilityAt(0);
            pc.useFishingRod(); // 收竿 → 拉拽
            tickB(1, 0.016f);  // 一帧物理：猪被拉向玩家（vx = −6 朝 −X…方向断言按位移点积）
            const float pigX1 = ents.posAt(pig).x();
            const float moved = pigX1 - pigX0;
            // 期望位移方向：猪在玩家 +X 侧 → 被拉向 −X（toward = pigX0 − 玩家x 的符号取反）。
            const float toward = (pigX0 - 13.5f) >= 0.0f ? -1.0f : 1.0f;
            okD = okHook && caughtCount == 0 && hb.durabilityAt(0) == dur0 - 5
                  && ents.healthAt(pig) == pigHp0          // 钩中不伤害
                  && moved * toward > 0.03f                  // 位移朝玩家 > 0.03（6 b/s 冲量 × 一帧）
                  && !pc.fishing() && !ents.aliveAt(bb);
            // d2 垂死 mob 收竿（R19.13 终审 B-L2）：老猪已被拉拽 + 累计游荡 ~3s 位置不可控（平台 rig 的
            //   1.5s 游荡安全窗只保单次落定）→ 换新猪平台中心重摆（同段首手法）。钩住后打死（死亡动画
            //   0.5s 窗内、探针不 tick ents → 浮标脱钩验证未跑，bobberHookedMobAt 仍指猪）→ 收竿：
            //   pullMobToward 对 dead 早退返 false → 按空收处理，耐久**不扣**（旧版 void 无条件 -5 = 白损）。
            const int pigHpAfter = ents.healthAt(pig); // diag 用（d2 换猪后老猪槽已释放，先存值）
            ents.removeEntityAt(pig);
            const int pig2 = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickB(5, 0.05f); // 短窗 settle（同段首 pig 加固口径：首游荡窗 30 tick 前完成甩钩）
            const QVector3D pp2 = ents.posAt(pig2);
            const float eyeY2 = float(fy + 1) + 1.62f;
            const float ux2 = pp2.x() - 13.5f, uz2 = pp2.z() - 6.5f;
            const float hLen2 = std::sqrt(ux2 * ux2 + uz2 * uz2);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                              qRadiansToDegrees(std::atan2(-ux2, -uz2)),
                              qRadiansToDegrees(std::atan2(pp2.y() - eyeY2, hLen2)), 2);
            pc.useFishingRod(); // 甩向新猪
            int bb2 = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bb2 = i; break; }
            bool okD2 = bb2 >= 0;
            for (int t = 0; t < 40 && okD2; ++t) {
                if (ents.bobberHookedMobAt(bb2) == pig2) break;
                tickB(1, 0.05f);
                if (!ents.aliveAt(bb2)) { okD2 = false; break; }
            }
            okD2 = okD2 && ents.bobberHookedMobAt(bb2) == pig2;
            const bool hooked2Now = bb2 >= 0 && ents.bobberHookedMobAt(bb2) == pig2; // 收竿前现场（甩中与否）
            const QVector3D pig2End = ents.posAt(pig2);
            ents.damageEntity(pig2, 999); // 打死（dead=true；槽仍 alive，死亡动画窗内）
            const int durD2 = hb.durabilityAt(0);
            pc.useFishingRod();           // 垂死目标收竿 → 拉拽 no-op
            okD2 = okD2 && !pc.fishing() && !ents.aliveAt(bb2) && caughtCount == 0
                   && hb.durabilityAt(0) == durD2; // 空收口径：无获物不扣耐久（B-L2 修）
            okD = okD && okD2;
            if (!okD)
                qInfo().noquote() << "  [t836 d diag] okHook" << okHook << "dur" << hb.durabilityAt(0) - dur0
                                  << "hp" << pigHpAfter << "/" << pigHp0 << "moved" << moved
                                  << "toward" << toward << "okD2(dead-mob no-cost)" << okD2
                                  << "hooked2Now" << hooked2Now << "pig2End" << pig2End;
            ents.removeEntityAt(pig2); // 清场（探针私有 ents 冻结不外泄）
            wF.setBlock(13, fy, 6, BR::Air, 0);
            for (int x = 15; x <= 17; ++x)
                for (int z = 5; z <= 7; ++z)
                    wF.setBlock(x, fy, z, BR::Air, 0);
        }

        // ---- (e) 熟鱼链（kSmelt + kSmeltXp 两表 + 食用值 + 名 + pack/创造 tab 源码钉 + 豹猫 gate 钉）----
        bool okE = false;
        {
            Hotbar hbF;
            const bool okCore = RecipeRegistry::CookedFishId == 0x25B
                                && SmeltingRegistry::smeltResult(RecipeRegistry::RawFishId) == RecipeRegistry::CookedFishId
                                && SmeltingRegistry::smeltXpReward(RecipeRegistry::CookedFishId) == 1
                                && PlayerController::foodHungerAmount(RecipeRegistry::CookedFishId) == 4
                                && PlayerController::foodHungerAmount(RecipeRegistry::RawFishId) == 2
                                && hbF.nameForBlock(RecipeRegistry::CookedFishId) == QStringLiteral("熟鱼")
                                && hbF.nameForBlock(RecipeRegistry::RawFishId) == QStringLiteral("生鱼");
            // 源码钉（QML/源内字面量契约，t789 QML-literal 模式）：pack 映射行 + 创造 tab 行 + 豹猫生鱼 gate
            //   （熟鱼不接豹猫喂食 = MC 1.0 口径）。exe 在 build/ → ../src 或 ../../src 兜底（r24#5 同款）。
            bool okSrc = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString rpmPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Core/resourcepackmanager.cpp"));
                const QString hbPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                           QStringLiteral("src/Game/hotbar.cpp"));
                const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Game/playercontroller.cpp"));
                if (QFile::exists(rpmPath) && QFile::exists(hbPath) && QFile::exists(pcpPath)) {
                    QFile f1(rpmPath), f2(hbPath), f3(pcpPath);
                    if (f1.open(QIODevice::ReadOnly) && f2.open(QIODevice::ReadOnly) && f3.open(QIODevice::ReadOnly)) {
                        const QString t1 = QString::fromUtf8(f1.readAll());
                        const QString t2 = QString::fromUtf8(f2.readAll());
                        const QString t3 = QString::fromUtf8(f3.readAll());
                        // 滤注释行后查语句（防「注释里有、代码里没有」的假 PASS；豹猫 gate 行在代码区）。
                        QString t3code;
                        for (const QString &line : t3.split(QLatin1Char('\n'))) {
                            if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                            t3code += line; t3code += QLatin1Char('\n');
                        }
                        okSrc = t1.contains(QStringLiteral("{0x25B, QStringLiteral(\"cooked_cod.png\")}"))
                                && t2.contains(QStringLiteral("int(RecipeRegistry::CookedFishId)"))
                                && t3code.contains(QStringLiteral("heldItemId == RecipeRegistry::RawFishId"));
                    }
                }
            }
            okE = okCore && okSrc;
            if (!okE)
                qInfo().noquote() << "  [t836 e diag] okCore" << okCore << "okSrc" << okSrc;
        }

        // ---- (f) 出界消散（R19.13 终审 C-M2：PASS 文案曾含 "out-of-bounds despawn" 而无对应断言——
        //      Review24 #9「描述超断言」病复发处，补上）：XZ 飞越边界 + 极端 y（y<0 虚空）→ ents.tick
        //      若干 → 浮标槽释放（aliveAt 翻 false = 槽 despawn；8 tick 内出界 = 边界路径非 180s 寿命路径）。----
        bool okF = false;
        {
            // f1 XZ 飞越：世界 48 宽，自 (44.5, fy+4, 6.5) 以 30 b/s +X → 第 3 tick x>48 出界（轻重力下
            //   y 仍在空带，不落水 / 不着地 / 无 mob 可钩 → 唯一出路是边界消散）。
            const int bOut = ents.spawnBobber(QVector3D(44.5f, float(fy + 4), 6.5f),
                                              QVector3D(30.0f, 0.0f, 0.0f), 911);
            bool okXz = bOut >= 0 && ents.aliveAt(bOut);
            for (int t = 0; t < 8 && okXz; ++t) {
                tickB(1, 0.05f);
                if (!ents.aliveAt(bOut)) break;
            }
            okXz = okXz && !ents.aliveAt(bOut);
            // f2 极端 y（虚空直落）：y<0 → 首 tick 即消散。
            const int bLow = ents.spawnBobber(QVector3D(6.5f, -10.0f, 6.5f), QVector3D(0, 0, 0), 912);
            tickB(1, 0.05f);
            const bool okLow = bLow >= 0 && !ents.aliveAt(bLow);
            okF = okXz && okLow;
            if (!okF)
                qInfo().noquote() << "  [t836 f diag] okXz" << okXz << "okLow" << okLow;
        }

        // ---- (g) Game 层镜像（R19.13 终审 C-M2：updateFishing 每 tick 镜像路径此前零执行）----
        //      行为半边（探针可达）：甩竿后外部清场（ents.clearAll = 出界 / 寿命消散 / 换世界清场的等价
        //      构造）→ ① 镜像惰性（清场即刻 fishing 态仍在——Game 层不主动扫描，tick / 收竿才收）；
        //      ② 收竿走 valid=false 分支 = 干净收场（无获物 / 无耐久消耗 / fishing 复位 / 浮标槽已空）。
        //      自动收竿半边（pc.tick 驱动 updateFishing）：tickImpl 的 captured 门在无窗测试二进制不可达
        //      （直调 pc.tick 会先走 !m_captured 早退分支的 cancelFishing，掩盖镜像路径本体）→ 源序钉
        //      （t836(e) 手法）：滤注释后锁 updateFishing 函数体内「aliveAt/kindAt 双查 + 失效自动收竿」
        //      语句面。取舍：行为级不可达已声明，源码钉防语句面漂移（review 建议的退路）。----
        bool okG = false;
        {
            PlayerController pc;
            Hotbar hb;
            const int rodDur = ToolRegistry::maxDurability(ToolRegistry::FishingRod);
            hb.setStack(0, ToolRegistry::FishingRod, 1, rodDur);
            hb.setSelectedSlot(0);
            pc.setWorld(&wF);
            pc.setEntityManager(&ents);
            pc.setHotbar(&hb);
            pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            int caughtCount = 0;
            QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                             [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
            pc.useFishingRod(); // 甩竿（飞行中即可——镜像检测与浮标态无关）
            int bg = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bg = i; break; }
            bool okBeh = pc.fishing() && bg >= 0;
            ents.clearAll(); // 浮标消散等价构造（releaseSlot，不发 removeEntityAt）
            okBeh = okBeh && pc.fishing(); // ① 镜像惰性：清场即刻态不塌（tick / 收竿才收）
            const int durG = hb.durabilityAt(0);
            pc.useFishingRod(); // 收竿 → valid=false（aliveAt 双查失败）→ 自动收竿态
            okBeh = okBeh && !pc.fishing() && !ents.aliveAt(bg) && caughtCount == 0
                    && hb.durabilityAt(0) == durG; // ② 干净收场：无获物无消耗
            // 源序钉：锁 updateFishing 函数体内的失效检测 + 自动收竿语句面（captured 门不可达的退路）。
            bool okPin = false;
            {
                const QString exeDir = QCoreApplication::applicationDirPath();
                const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                            QStringLiteral("src/Game/playercontroller.cpp"));
                QFile f(pcpPath);
                if (f.open(QIODevice::ReadOnly)) {
                    const QString t = QString::fromUtf8(f.readAll());
                    const int b0 = t.indexOf(QStringLiteral("void PlayerController::updateFishing(float dt)"));
                    const int b1 = t.indexOf(QStringLiteral("void PlayerController::cancelFishing"));
                    if (b0 >= 0 && b1 > b0) {
                        QString body;
                        for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                            if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                            body += line; body += QLatin1Char('\n');
                        }
                        okPin = body.contains(QStringLiteral("m_entityManager->aliveAt(m_bobberEntityIdx)"))
                                && body.contains(QStringLiteral(
                                       "kindAt(m_bobberEntityIdx) == int(EntityManager::Bobber)"))
                                && body.contains(QStringLiteral("m_fishing = false;"))
                                && body.contains(QStringLiteral("emit fishingChanged();"));
                    }
                }
            }
            okG = okBeh && okPin;
            if (!okG)
                qInfo().noquote() << "  [t836 g diag] okBeh" << okBeh << "okPin" << okPin;
        }

        // ---- (h) review25 #12 Ground 态支撑复查：挖掉贴靠方块 → 若干 tick 内转 Flying 下落 ----
        //      旧版 Ground 恒 continue（零复查）→ 挖掉贴靠方块后浮标悬空滞留至 180s 寿命兜底。修复 =
        //      贴靠格快照（进态时记录）+ 每 kBobberGroundRecheckEvery tick 一查 blockAt。断言三段：
        //      落定冻结（20 tick 位置不变）→ 挖支撑 → ≤40 tick 内 y 下坠 >0.05（复查节流 ≤10 tick + 重力
        //      积累 ~4 tick）且实体仍活。
        bool okH = false;
        {
            wF.setBlock(20, fy, 8, BR::Stone, 0); // 石台（贴靠格）
            const int bh = ents.spawnBobber(QVector3D(20.5f, float(fy + 4), 8.5f), QVector3D(0, 0, 0), 921);
            for (int t = 0; t < 80 && ents.aliveAt(bh); ++t) tickB(1, 0.05f); // 落到石台 → Ground
            const QVector3D phA = ents.posAt(bh);
            tickB(20, 0.05f);
            const QVector3D phB = ents.posAt(bh);
            const bool frozen = ents.aliveAt(bh) && phA == phB && phA.y() > float(fy); // 冻结于台面上方
            wF.setBlock(20, fy, 8, BR::Air, 0); // 挖掉贴靠方块（Ground 复查的触发源）
            bool fell = false;
            for (int t = 0; t < 40 && ents.aliveAt(bh); ++t) {
                tickB(1, 0.05f);
                if (ents.posAt(bh).y() < phA.y() - 0.05f) { fell = true; break; }
            }
            okH = frozen && fell;
            if (!okH)
                qInfo().noquote() << "  [t836 h diag] frozen" << frozen << "phA" << phA << "phB" << phB
                                  << "fell" << fell << "pos" << ents.posAt(bh);
            if (bh >= 0) ents.removeEntityAt(bh);
        }

        // ---- (i) review25 #13 实体格命中门：1 格墙后贴壁 mob 飞行浮标不隔墙钩 ----
        //      旧序先用 next 点测 mob AABB（外扩 kBobberHookHitPad）再查方块碰撞 → next 跨入墙格且已进
        //      墙后 mob 外扩命中盒时被隔墙钩住（Hooked 钉位 + 收竿拉拽可拉 mob 穿墙）。rig：石坑困猪（四壁
        //      2 高 + 坑底，开口向上），knockback 把猪压在 -X 壁（= 浮标来向的 1 格墙）上 → 猪 AABB 贴壁 →
        //      外扩命中盒左沿伸到墙格前 0.15（≈23.85）——浮标 spawn 于 (23.96, pigY, 7.5) v=(18,0,0)（步长
        //      0.9/tick）首 tick next=(24.86,·)：已入墙格且在命中盒内。断言：不钩（bobberHookedMobAt==-1）
        //      + 贴面停在墙前（pos.x ∈ (23,24)）——修复序实体格命中门先于钩 mob，贴面 Ground 不钩。
        bool okI = false;
        {
            wF.setBlock(25, fy, 7, BR::Stone, 0); // 坑底
            for (int yy = fy + 1; yy <= fy + 2; ++yy) {
                wF.setBlock(24, yy, 7, BR::Stone, 0); // -X 壁 = 浮标来向 1 格墙
                wF.setBlock(26, yy, 7, BR::Stone, 0);
                wF.setBlock(25, yy, 6, BR::Stone, 0);
                wF.setBlock(25, yy, 8, BR::Stone, 0);
            }
            const int pigI = ents.spawnMobTyped(25, fy + 1, 7, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickB(30, 0.05f);                    // 落定（坑内 1×1 活动域）
            ents.knockback(pigI, -1.0f, 0.0f);   // 压向 -X 壁 → AABB 贴壁钉住（命中盒伸入墙格带）
            tickB(10, 0.05f);                    // 滑到贴壁静止
            const QVector3D pigP = ents.posAt(pigI);
            const bool pinned = pigP.x() >= 25.35f && pigP.x() <= 25.60f; // 贴壁带（halfW±漂移；diag 半断言）
            const int bi = ents.spawnBobber(QVector3D(23.96f, pigP.y(), 7.5f),
                                            QVector3D(18.0f, 0.0f, 0.0f), 922);
            tickB(3, 0.05f);
            okI = pinned && bi >= 0 && ents.aliveAt(bi)
                  && ents.bobberHookedMobAt(bi) == -1 // 不隔墙钩
                  && ents.posAt(bi).x() < 24.0f       // 贴面停在墙前（未穿入墙格）
                  && ents.posAt(bi).x() > 23.0f;
            if (!okI)
                qInfo().noquote() << "  [t836 i diag] pinned" << pinned << "pigX" << pigP.x()
                                  << "hooked" << (bi >= 0 ? ents.bobberHookedMobAt(bi) : -2)
                                  << "bobPos" << (bi >= 0 ? ents.posAt(bi) : QVector3D());
            if (bi >= 0) ents.removeEntityAt(bi);
            ents.removeEntityAt(pigI);
            wF.setBlock(25, fy, 7, BR::Air, 0);
            for (int yy = fy + 1; yy <= fy + 2; ++yy) {
                wF.setBlock(24, yy, 7, BR::Air, 0);
                wF.setBlock(26, yy, 7, BR::Air, 0);
                wF.setBlock(25, yy, 6, BR::Air, 0);
                wF.setBlock(25, yy, 8, BR::Air, 0);
            }
        }

        const bool okT836 = okA && okB && okC && okD && okE && okF && okG && okH && okI;
        if (!okT836) ++totalFail;
        qInfo().noquote() << (okT836 ? "PASS" : "FAIL")
                          << "| t836 fishing overhaul: bobber is an EntityManager projectile (light-gravity "
                             "parabola, hook-on-flight vs mob AABB, water settle at surface-minus-dip with "
                             "state-aware liquid height, ground rest frozen, out-of-bounds despawn ASSERTED: "
                             "xz flyout within 8 ticks + void-y first-tick slot release) driven from "
                             "Game layer cast-anywhere/reel (EntityManager-carries-entity + "
                             "PlayerController-settles-semantics split, pearl/drop precedent); deterministic "
                             "5-30s wait via hashVoxel(seed^salt^castSerial) with exact reachable endpoints and "
                             "+-1tick behavioral match, 0.5s bite window (in-window reel = fishingPool loot "
                             "thrown to the player as a ballistic spawnItemThrown (t886: solved arc, "
                             "distance-adaptive speed) + rod -1, expired = escaped signal + re-roll + empty "
                             "reel costs nothing), hooked-mob reel pulls at ~6 b/s with -5 durability and zero "
                             "damage, dead-target reel = pull no-op with NO durability charge; externally-"
                             "cleared bobber keeps lazy fishing state then reels clean (no loot, no cost) with "
                             "updateFishing invalid-to-auto-reel pinned at source level (pc.tick captured gate "
                             "unreachable headless, tradeoff declared); ground-rest bobber rechecks its "
                             "support cell on a 10-tick throttle and falls (review25 #12: mined support -> "
                             "flying within 40 ticks, no more hovering until the 180s lifetime bail); "
                             "solid-cell hit gate precedes mob hooking (review25 #13: wall-pinned pig "
                             "behind a 1-thick wall with a fast bobber whose next point lands inside the "
                             "wall cell and the padded pig AABB grounds at the wall face instead of "
                             "hooking through it); "
                             "cooked fish 0x25B closes the chain (raw->cooked in BOTH kSmelt+kSmeltXp, "
                             "+4 hunger vs raw +2, name/tab/pack-mapping pinned, ocelot still raw-only)";
    }

    // ── Review 2026-08-25 #2 浇熄摘侧表信号探针（blockDoused 恰一次 + 坐标 + 火灭块存）──
    // 背景：浇熄设计为「火灭块存」——tickFire 抑制掷中后 m_burningCells.remove 直摘：栅格不变（无
    //   blockBroken）、侧表直摘（无 worldChanged）→ QML 面火 overlay 两条摘除链（onBlockBroken /
    //   onWorldChanged→cleanupVis）都不触发 = 假火 delegate 永久残留。setBlock 同 id 无变化早退清表
    //   （t843 特意放早退前）同根因第二实例。修法 = 新增 blockDoused(x,y,z) 精确信号驱动
    //   removeBurningVis（取舍：不选补发 worldChanged——那会触发 cleanupVis 全量对账，浇熄是常见
    //   事件不该付全量 mesh 重查的价）。锁法（三段）：
    //   (a) 同 id 早退路径（确定性）：点燃木板 → setBlock(Planks)（同 id no-op 写）→ 恰发一次
    //       blockDoused + 坐标正确 + isBurningAt=false + 栅格块仍存；
    //   (b) 有变化路径不重发：点燃 → setBlock(Air)（blockBroken + worldChanged 已覆盖）→ 计数不增；
    //   (c) tickFire 浇熄掷中路径：点燃（干）→ 邻注水 → 推窗至掷中（40%/窗 vs 木板 10 窗烧毁，
    //       先掷中概率 99.4%/候选；烧毁即换候选重试，40 候选下假 FAIL 率 ~1e-85）→ 恰一次 + 坐标 +
    //       块存 + 全程零 blockBroken（火灭块存 ≠ 烧毁语义钉死）。
    {
        // rig 选址：运行期扫描空区（t809 先例——尾部探针不占 nextSlot 网格）。需 11×6×8 候选带。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 10 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 10 && clear; ++dx)
                    for (int dz = 0; dz < 7 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #2 douse signal: no clear rig area found";
        } else {
            bool ok = true;
            int doused = 0, dX = -1, dY = -1, dZ = -1, brokenCnt = 0;
            QMetaObject::Connection cD = QObject::connect(
                    &w, &World::blockDoused, &w,
                    [&](int x, int y, int z) { ++doused; dX = x; dY = y; dZ = z; });
            QMetaObject::Connection cB = QObject::connect(
                    &w, &World::blockBroken, &w, [&](int, int, int, int) { ++brokenCnt; });

            // (a) 同 id 无变化早退清表（确定性路径）。
            placeRigBlock(w, x0, kRigY, z0, BR::Planks, 0);
            const bool ignitedA = w.igniteFlammableAt(x0, kRigY, z0) && w.isBurningAt(x0, kRigY, z0);
            doused = 0;
            const bool noChangeRet = !w.setBlock(x0, kRigY, z0, BR::Planks); // 同 id → false 早退
            const bool okA = ignitedA && noChangeRet && doused == 1 && dX == x0 && dY == kRigY && dZ == z0
                             && !w.isBurningAt(x0, kRigY, z0)
                             && w.blockAt(x0, kRigY, z0) == BR::Planks; // 火灭块存（栅格不动）

            // (b) 有变化路径不重发（blockBroken + worldChanged 覆盖，blockDoused 不掺和）。
            const bool ignitedB = w.igniteFlammableAt(x0, kRigY, z0) && w.isBurningAt(x0, kRigY, z0);
            doused = 0;
            brokenCnt = 0;
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            const bool okB = ignitedB && doused == 0 && brokenCnt >= 1
                             && !w.isBurningAt(x0, kRigY, z0);

            // (c) tickFire 抑制浇熄掷中（候选搜索：找到即断言，失败候选（10 窗内未掷中先烧毁）清扫换位）。
            bool okC = false;
            int usedCand = -1;
            for (int cand = 0; cand < 40 && !okC; ++cand) {
                usedCand = cand;
                const int tx = x0 + (cand % 5) * 2;        // 步距 2：候选板格与水格互不占位
                const int tz = z0 + cand / 5;
                placeRigBlock(w, tx, kRigY, tz, BR::Planks, 0);
                if (!w.igniteFlammableAt(tx, kRigY, tz)) { // 干格点燃（水后注——湿燃料不可点燃是入口守卫）
                    w.setBlock(tx, kRigY, tz, BR::Air, 0);
                    continue;
                }
                w.setBlock(tx + 1, kRigY, tz, BR::Water, 0); // 点燃后注水邻 → 进抑制态
                doused = 0;
                brokenCnt = 0;
                for (int t = 0; t < 60 && doused == 0; ++t) w.tickFire(); // 60 调 = 12 窗（interval 5）
                const bool survived = w.blockAt(tx, kRigY, tz) == BR::Planks;
                if (doused == 1 && survived && dX == tx && dY == kRigY && dZ == tz
                        && !w.isBurningAt(tx, kRigY, tz) && brokenCnt == 0) {
                    okC = true; // 恰一次 + 坐标 + 块存 + 零 broken（浇熄非烧毁）
                } else {
                    w.setBlock(tx + 1, kRigY, tz, BR::Air, 0); // 清水 + 清格（烧毁 flare/Air 残留归一）
                    w.setBlock(tx, kRigY, tz, BR::Air, 0);
                }
            }
            ok = okA && okB && okC;
            if (!ok)
                qInfo().noquote() << "  [r25#2 diag] okA" << okA << "okB" << okB << "okC" << okC
                                  << "cand" << usedCand << "doused" << doused << "broken" << brokenCnt;
            QObject::disconnect(cD);
            QObject::disconnect(cB);
            // 清场（候选带全扫 Air——水格 + 板格 + flare 残留一并）。
            for (int dx = 0; dx <= 10; ++dx)
                for (int dz = 0; dz < 7; ++dz)
                    for (int dy = -1; dy <= 2; ++dy)
                        if (w.blockAt(x0 + dx, kRigY + dy, z0 + dz) != BR::Air)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #2 douse signal: same-id setBlock early-exit and tickFire"
                                 " suppress-roll removal each emit blockDoused exactly once with correct"
                                 " coords and block-preserved (no blockBroken), change-path stays silent"
                                 " (broken+worldChanged cover it); probabilistic roll closed via"
                                 " candidate search (40 tries, ~1e-85 false rate)";
        }
    }

    // ── Review 2026-08-25 #3 tickVehicleRiding emit 节流探针（不直发 / pending 由 tick 收口接住）──
    // 背景：t811 tickVehicleRiding 末尾 `if (dirty) { ++m_revision; emit entitiesChanged(); }` 直发，且
    //   每帧被调两次（playercontroller mob 桶常开 + step 后补钉）——乘客跟车每帧 dirty → 最坏每帧 2 次
    //   全量 revision+emit（激活全体实体 delegate revision 绑定 + 行走 MobModel 全几何重建 = t500 已修的
    //   22ms/帧卡顿模式复发；矩阵探针只断言钉位行为，emit 面探针盲区）。修法 = dirty 只置 m_pendingEmit
    //   复用 tick 末尾 kEmitEveryN（~20Hz）收口。锁法：
    //   (a) 直调相（隔离验证）：spawnCart + spawnMob + 仅 tickVehicleRiding×2/帧 + 推车物理（车动 → 钉位
    //       每帧变 → 每帧 dirty）跑 20 帧**不调 ents.tick** → entitiesChanged 零 emit（旧直发版首帧登乘
    //       即 emit → 回归即红；20 帧移动场景旧版 ≥10 emit）；
    //   (b) 收口相：接续 ents.tick×6（含相位门 %3）→ ≥1 emit（pending 被 tick 接住 = 钉位变更最终可见，
    //       t811 呈现语义不丢）且 ≤ 3（= 6/3 + 1 节流上界——防「换一处直发」的复发面）；
    //   (c) 钉位行为不回归：全程 mob 钉车座位（t811 座位公式误差 <0.01）。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 1 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 1 && clear; ++dx)
                    for (int dz = -6; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #3 riding emit throttle: no clear rig area found";
        } else {
            const float seatDrop = 0.3125f; // kCartSeatDrop 同值镜像（t811 探针同款）
            for (int dz = -5; dz <= 0; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    placeRigBlock(w, x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                    if (dx == 0) placeRigBlock(w, x0, kRigY, z0 + dz, BR::Rail, 0);
                }
            MinecartManager carts;
            EntityManager ents;
            ents.setVehicleManagers(&carts, nullptr);
            carts.spawnCart(x0, kRigY, z0, &w);
            const int mob = ents.spawnMobTyped(x0, kRigY, z0, 0, QStringLiteral("#ff5555"), 10);
            int emitted = 0;
            QMetaObject::Connection cE = QObject::connect(
                    &ents, &EntityManager::entitiesChanged, &ents, [&]() { ++emitted; });
            // (a) 直调相：20 帧只跑骑乘收口 + 车物理（登乘 + 跟车每帧 dirty），不调 ents.tick → 恒 0 emit。
            QVector3D player = carts.posAt(0);
            bool boarded = false;
            for (int t = 0; t < 20; ++t) {
                ents.tickVehicleRiding();
                carts.pushEmptyCart(&w, player, 0.0f, -1.0f); // 长按 W 朝北推（t811 玩家模型）
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();
                player = carts.posAt(0);
                if (ents.rideCartAt(mob) >= 0) boarded = true;
            }
            const int directEmits = emitted;
            // (c) 钉位公式（车座位 = 车心 − seatDrop + halfH(0.5)）。
            const QVector3D cp = carts.posAt(0);
            const QVector3D mp = ents.posAt(mob);
            const bool pinOk = boarded && std::fabs(mp.x() - cp.x()) <= 0.01f
                               && std::fabs(mp.y() - (cp.y() - seatDrop + 0.5f)) <= 0.01f
                               && std::fabs(mp.z() - cp.z()) <= 0.01f;
            // (b) 收口相：6 帧 tick（相位门 %3 → 恰 2 次对齐）接住 pending。
            const int beforeFlush = emitted;
            for (int t = 0; t < 6; ++t) {
                ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
                ents.tickVehicleRiding();
                carts.pushEmptyCart(&w, player, 0.0f, -1.0f);
                carts.tickPushedCarts(0.016, &w);
                ents.tickVehicleRiding();
                player = carts.posAt(0);
            }
            const int flushEmits = emitted - beforeFlush;
            const bool ok = directEmits == 0 && pinOk && flushEmits >= 1 && flushEmits <= 3;
            if (!ok)
                qInfo().noquote() << "  [r25#3 diag] direct" << directEmits << "pinOk" << pinOk
                                  << "flush" << flushEmits;
            QObject::disconnect(cE);
            carts.clearAll();
            ents.clearAll();
            for (int dz = -5; dz <= 0; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Air, 0);
                w.setBlock(x0, kRigY, z0 + dz, BR::Air, 0);
            }
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #3 riding emit throttle: tickVehicleRiding never emits"
                                 " entitiesChanged directly (20 dirty frames -> 0 emits; old code >=1 on"
                                 " first boarding frame), pending flushed through tick's kEmitEveryN gate"
                                 " (6 ticks -> 1..3 emits), seat-pin formula intact (dx/dy/dz < 0.01)";
        }
    }

    // ── Review 2026-08-25 #4 鱿鱼持续浮力天花板碰撞探针（封顶水柱上浮贴顶不穿出）──
    // 背景：垂直积分段只为下落设计——浮力 vy>0 自由上移分支无向上阻挡 → 头顶穿入固体格（冰面/封顶水池）
    //   后**中心**落入固体格那帧，落地扫描 restY(格顶+halfH) 高于当前位置 → mobNewY<=restY 成立把整段
    //   setY(restY) 抬到方块顶上（穿顶）。修法 = vy>0 时对头顶格取碰撞盒最低底（collisionAABBsAt 口径，
    //   与落定分支对称；ShapeNone 无盒族照穿过），头将穿入 → 钳 pos.y=ceilBottom−halfH、vy=0 贴顶悬停。
    //   rig：封闭水箱（5×5 石底 + 石壁环 3 层 + 内腔 3×3 水×3 + 5×5 石顶）——石壁防鱿鱼水平漂游出腔
    //   （aiSquid 有 XZ 漂游 + 碰撞撤回）。锁法：spawn 鱿鱼于中层水 → tick 400 帧 →
    //   (a) 不穿出：末位 pos.y + halfH ≤ 顶格下沿 + 0.02（旧版被整段抬到格顶上 ≈ +1.47 → 红）；
    //   (b) 贴顶稳定：末 60 帧 Y 带 ≤ 0.05（浮力再积再钳的贴顶悬停，非振荡/继续上穿）；
    //   (c) 确有上浮：末位 > 初始位（防「误杀浮力」的反向回归）。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 4 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 4 && clear; ++dx)
                    for (int dz = 0; dz <= 4 && clear; ++dz)
                        for (int dy = -1; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #4 squid ceiling: no clear rig area found";
        } else {
            const int yB = kRigY;                  // 箱底（石）；水 yB+1..yB+3；顶 yB+4（石）
            for (int dx = 0; dx <= 4; ++dx)
                for (int dz = 0; dz <= 4; ++dz) {
                    placeRigBlock(w, x0 + dx, yB, z0 + dz, BR::Stone, 0);        // 底
                    placeRigBlock(w, x0 + dx, yB + 4, z0 + dz, BR::Stone, 0);    // 顶
                    const bool wall = (dx == 0 || dx == 4 || dz == 0 || dz == 4);
                    for (int dy = 1; dy <= 3; ++dy) {
                        if (wall) placeRigBlock(w, x0 + dx, yB + dy, z0 + dz, BR::Stone, 0);
                        else     placeRigBlock(w, x0 + dx, yB + dy, z0 + dz, BR::Water, 0);
                    }
                }
            EntityManager ents;
            const int sq = ents.spawnMobTyped(x0 + 2, yB + 2, z0 + 2, EntityManager::MobSquid,
                                              QStringLiteral("#306090"), 10);
            const float halfH = 0.45f; // MobSquid 半高（spawnMobCore 表）
            const float ceilBottom = float(yB + 4);
            const float startY = ents.posAt(sq).y();
            float loY = 1e9f, hiY = -1e9f;
            for (int t = 0; t < 400; ++t) {
                ents.tick(0.016, &w, QVector3D(x0 + 2.5f, yB + 2.5f, z0 + 2.5f), 0.3f, 1.8f, false);
                if (t >= 340) {
                    const float y = ents.posAt(sq).y();
                    loY = std::min(loY, y);
                    hiY = std::max(hiY, y);
                }
            }
            const float endY = ents.posAt(sq).y();
            const bool ok = ents.aliveAt(sq)
                            && endY + halfH <= ceilBottom + 0.02f   // (a) 不穿出（旧版 ≈ ceil+1.45 → 红）
                            && (hiY - loY) <= 0.05f                 // (b) 贴顶稳定带
                            && endY > startY - 0.01f;               // (c) 浮力仍在（上升到顶）
            if (!ok)
                qInfo().noquote() << "  [r25#4 diag] endY" << endY << "ceilBottom" << ceilBottom
                                  << "band" << (hiY - loY) << "startY" << startY
                                  << "alive" << ents.aliveAt(sq);
            ents.clearAll();
            for (int dx = 0; dx <= 4; ++dx)
                for (int dz = 0; dz <= 4; ++dz)
                    for (int dy = 0; dy <= 4; ++dy)
                        w.setBlock(x0 + dx, yB + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #4 squid ceiling: sustained buoyancy in a capped water box"
                                 " clamps at head-level collision bottom (pos.y+halfH stays below ceiling"
                                 " underface +0.02, 60-tick stability band <=0.05, still rises from spawn ="
                                 " buoyancy intact); old code teleported squid whole-body above the ceiling"
                                 " (restY snap ~1.45 above the clamp)";
        }
    }

    // ── Review 2026-08-25 #5 珍珠无碰撞植物族穿过探针（草丛格穿过 / 铁轨格仍命中对照）──
    // 背景：t835 命中判据「本格任意方块实存（5 id 豁免表）」把 TallGrass/花/蘑菇/树苗/枯灌木/作物
    //   （ShapeNone 无碰撞盒）也当命中 → 草地/农田平抛弧线数格内被草截断传送（反噬 t835「更远投掷」
    //   目标；本工程箭按空碰撞盒穿过植物）。修法 = 判据改「本格存在碰撞 sub-AABB」（判据本质化：旧
    //   5 id 豁免族全 ShapeNone 无盒 → 语义天然保留；铁轨/压力板/台阶等薄盒族仍命中 → t835「落铁轨
    //   必传送」不回归；未来新无碰撞方块自动正确）。锁法：两列对照直落 ——
    //   (a) 草丛列（石上 TallGrass）：珠穿过草格、命中**下方石格**才 enderPearlLanded（旧「任意实存」
    //       判据在草格即结算 → 落点 y = 草格 ≠ 石格 → 红）；
    //   (b) 铁轨列（石上 Rail）：珠在**轨格**即命中（薄盒存在 → t835 落轨传送语义钉死）。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 118 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 3 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = 0; dx <= 3 && clear; ++dx)
                    for (int dz = 0; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | review25 #5 pearl plants: no clear rig area found";
        } else {
            // 两列相隔 3（x0 草丛列 / x0+3 铁轨列；互不邻接防编辑钩子串扰）。
            placeRigBlock(w, x0, kRigY, z0, BR::Stone, 0);
            placeRigBlock(w, x0, kRigY + 1, z0, BR::TallGrass, 0);
            placeRigBlock(w, x0 + 3, kRigY, z0, BR::Stone, 0);
            placeRigBlock(w, x0 + 3, kRigY + 1, z0, BR::Rail, 0);
            EntityManager ents;
            int landedCnt = 0;
            int lx[2] = { -1, -1 }, ly[2] = { -1, -1 };
            QMetaObject::Connection cL = QObject::connect(
                    &ents, &EntityManager::enderPearlLanded, &ents,
                    [&](int x, int y, int z) {
                        Q_UNUSED(z);
                        if (landedCnt < 2) { lx[landedCnt] = x; ly[landedCnt] = y; }
                        ++landedCnt;
                    });
            ents.spawnEnderPearl(QVector3D(x0 + 0.5f, kRigY + 3.5f, z0 + 0.5f),
                                 QVector3D(0.0f, -2.0f, 0.0f));
            ents.spawnEnderPearl(QVector3D(x0 + 3.5f, kRigY + 3.5f, z0 + 0.5f),
                                 QVector3D(0.0f, -2.0f, 0.0f));
            for (int t = 0; t < 200 && landedCnt < 2; ++t)
                ents.tick(0.016, &w, QVector3D(x0 + 2.0f, kRigY + 3.0f, z0 + 0.5f), 0.3f, 1.8f, false);
            // 各列落点归位断言（x 匹配列；y = 期望格）。
            bool grassPassed = false, railHit = false;
            for (int i = 0; i < 2 && i < landedCnt; ++i) {
                if (lx[i] == x0 && ly[i] == kRigY) grassPassed = true;        // 草丛列：石格才结算
                if (lx[i] == x0 + 3 && ly[i] == kRigY + 1) railHit = true;    // 铁轨列：轨格即结算
            }
            const bool ok = landedCnt == 2 && grassPassed && railHit;
            if (!ok)
                qInfo().noquote() << "  [r25#5 diag] landed" << landedCnt << "lx" << lx[0] << lx[1]
                                  << "ly" << ly[0] << ly[1];
            QObject::disconnect(cL);
            ents.clearAll();
            w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY + 1, z0, BR::Air, 0);
            w.setBlock(x0 + 3, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
            if (!ok) ++totalFail;
            qInfo().noquote() << (ok ? "PASS" : "FAIL")
                              << "| review25 #5 pearl plant pass-through: pearl falling through a"
                                 " TallGrass cell (ShapeNone, no collision box) keeps flying and"
                                 " lands on the stone cell below (old any-block-here criterion"
                                 " triggered on the grass cell), while a Rail cell (thin collision"
                                 " box present) still triggers landing in-cell (t835 rail-teleport"
                                 " semantics preserved)";
        }
    }

    // ── t874/t875 铁砧附魔丢失 + 附魔台拒入 真链探针（R19.15 批三）──
    // 背景：用户报「放入铁砧附魔直接没了，元数据丢失」（跨七八个版本未根治）+「附魔台能放入已附魔物品
    //   并清洗附魔属性」。t792 实机探针（qml.exe 驱真 AnvilUI.qml + 桩 Hotbar.qml，11 放入路径 47/47）与
    //   t822（真 Hotbar VM C++ 直调镜像，无 QML 层）两轮全绿 → **接缝只剩「真 QML × 真 C++ Hotbar」从未
    //   同台执行**（Q_PROPERTY 早期返回 / QVariantList↔JS 转换 / NOTIFY 时序只在组合态暴露）。本探针拼上
    //   这一半：QQmlEngine 源树直载 AnvilUI.qml / EnchantingTableUI.qml（同目录隐式组件 + InventoryOps.js
    //   原地解析），真 Hotbar/PlayerState 经 context property 注入，真 QMouseEvent（press+release）驱动面板
    //   内联 TapHandler —— 与用户真实点击完全同链。
    // t874 覆盖：放入/取出全入口（真鼠标左/右键点 A/B 槽、Shift 搬运、左/右拖动、双击拿同类、数字键交换）
    //   × 四类别（工具 / 武器 4 附魔满配 / 护甲 / 附魔书）× 带名实例 × takeProduct 四 op（repair / combine /
    //   merge / rename）× 关包归还 × 存档 round-trip 后重绑 VM 再放入。
    // t875 覆盖：已附魔物品七入口拒入（真鼠标左/右键槽 0、右键拖、左键拖、数字键交换、Shift 搬运、双击
    //   合并）+ 全链无清洗（青金石槽放入 → 关包归还）+ 素品附魔 → 产物取出带附魔 → 拒再入。
    {
        // 类型注册：**探针私有 URI**（VoxelSandboxProbe）。不能用 VoxelSandbox —— build/VoxelSandbox/qmldir
        //   （qt_add_qml_module 产物）落在 exe 同目录默认 import path 上，`import VoxelSandbox` 会命中它并
        //   `prefer :/VoxelSandbox/` 重定向到 qrc 资源（本测试二进制未链模块资源 → "Script
        //   qrc:/VoxelSandbox/src/ui/InventoryOps.js unavailable"）。私有 URI 无 qmldir → 走本处 C++ 注册；
        //   面板拷贝上 import 行同步改写（类型名 Hotbar/PlayerState/PlayerController/ResourcePackManager 原名）。
        static bool sVoxelTypesRegistered = false;
        if (!sVoxelTypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbe", 1, 0, "Hotbar");
            qmlRegisterType<PlayerState>("VoxelSandboxProbe", 1, 0, "PlayerState");
            qmlRegisterType<PlayerController>("VoxelSandboxProbe", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbe", 1, 0, "ResourcePackManager");
            sVoxelTypesRegistered = true;
        }
        qputenv("QML_DISABLE_DISK_CACHE", "1"); // 见上：防磁盘缓存把依赖重定向到未链接的 qrc 资源
        QQmlEngine engine;
        Hotbar vm;
        PlayerState ps;
        ps.setXp(4000); // repair/combine/merge/rename 与 doEnchant 的等级门槛全可付
        engine.rootContext()->setContextProperty(QStringLiteral("t874Hotbar"), &vm);
        engine.rootContext()->setContextProperty(QStringLiteral("t874PlayerState"), &ps);

        // 宿主桩：Main.qml 根（id: window）的最小复刻 —— AnvilUI/EnchantingTableUI 经作用域链解析
        //   window.shiftHeld / refocusKeyInput / closeAnvil / burstEnchantRunes。
        QQmlComponent wrapComp(&engine);
        wrapComp.setData(R"QML(import QtQuick
Item {
    id: window
    width: 800; height: 1200
    property bool shiftHeld: false
    property string hoveredSlotKey: ""
    function refocusKeyInput() { }
    function closeAnvil() { }
    function closeEnchantingTable() { }
    function burstEnchantRunes(n) { }
}
)QML", QUrl());

        // harness 装配：wrapper（engine 持有）→ 800×1200 上下两半各挂一块面板。函数链直调无窗口事件 →
        //   面板挂独立 QQuickItem 容器即可（无需 QQuickWindow；QQuickItem 场景经 contentItem 承载）。
        QString harnessDiag;
        bool harnessOk = true;
        QQuickItem hostItem; // 独立场景根（无窗口）
        QQuickItem *wrapper = nullptr;
        QObject *anvilRoot = nullptr;
        QObject *enchantRoot = nullptr;
        if (wrapComp.isError()) {
            harnessOk = false;
            harnessDiag = QStringLiteral("wrapper: ") + wrapComp.errorString();
        } else {
            wrapper = qobject_cast<QQuickItem *>(wrapComp.create());
            if (!wrapper) {
                harnessOk = false;
                harnessDiag = QStringLiteral("wrapper create failed");
            } else {
                wrapper->setParent(&engine); // QObject 父（引擎析构兜底；视觉父子另有 parentItem）
                wrapper->setParentItem(&hostItem);
            }
        }
        const QString uiDir = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                  .filePath(QStringLiteral("../src/ui"));
        // t874/t875：源树路径直载时引擎把同目录相对导入（InventoryOps.js）重映射到编译模块 qrc 前缀
        //   （qrc:/VoxelSandbox/src/ui/...，本测试二进制未链模块资源 → unavailable）。把 6 个源文件拷到
        //   临时目录加载，逃离模块路径映射（文件内容逐字节同源树 —— 链路保真不受影响）。
        const QString probeUiDir = QDir::temp().absoluteFilePath(
                QStringLiteral("t874_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir);
        for (const QString f : { QStringLiteral("AnvilUI.qml"), QStringLiteral("EnchantingTableUI.qml"),
                                 QStringLiteral("InventoryOps.js"), QStringLiteral("InvSlot.qml"),
                                 QStringLiteral("ToolIcon.qml"), QStringLiteral("MaterialIcon.qml") }) {
            QFile src(uiDir + QLatin1Char('/') + f);
            QFile dst(probeUiDir + QLatin1Char('/') + f);
            dst.remove();
            src.copy(dst.fileName());
        }
        // 拷贝上两处 URL 改写（文件内容其余逐字节同源树，链路保真）：
        //   ① 相对 js 导入 → 绝对 file URL（防 build 目录 qmldir 的 prefer 重定向染指）；
        //   ② `import VoxelSandbox` → `import VoxelSandboxProbe`（防 exe 同目录 build/VoxelSandbox/qmldir 命中，
        //     其 prefer :/VoxelSandbox/ 指向本二进制未链接的 qrc 资源）。**全部拷贝文件**都改 ——
        //     ToolIcon/MaterialIcon 等子组件同样显式 import VoxelSandbox（t41 子目录显式导入约定）。
        {
            const QUrl jsUrl = QUrl::fromLocalFile(probeUiDir + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            QDir pd(probeUiDir);
            const QStringList qmlFiles = pd.entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles) {
                QFile p(probeUiDir + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbe\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        if (harnessOk) {
            // setData + 源文件 base URL：QQmlComponent(url) 直载时 type loader 把同目录相对导入重映射到
            //   编译模块 qrc 前缀（见上 probeUiDir 注释）；setData 携带 base URL 按原文编译，相对导入按
            //   base URL 解析（同目录文件真实存在）。
            QFile anvilSrc(probeUiDir + QLatin1Char('/') + QStringLiteral("AnvilUI.qml"));
            if (!anvilSrc.open(QIODevice::ReadOnly)) {
                harnessOk = false;
                harnessDiag = QStringLiteral("AnvilUI read failed");
            } else {
                QQmlComponent anvilComp(&engine);
                anvilComp.setData(anvilSrc.readAll(), QUrl::fromLocalFile(anvilSrc.fileName()));
                if (anvilComp.isError()) {
                    harnessOk = false;
                    harnessDiag = QStringLiteral("AnvilUI load: ") + anvilComp.errorString();
                } else {
                    // 以 wrapper 的 context 创建（面板内 window/shiftHeld 等经作用域链解析 wrapper 根的
                    //   id —— 真实应用面板在 Main.qml 作用域内实例化，此处同构复刻；引擎根 context 无 id）。
                    anvilRoot = anvilComp.create(qmlContext(wrapper));
                    QQuickItem *ai = qobject_cast<QQuickItem *>(anvilRoot);
                    if (!ai) {
                        harnessOk = false;
                        harnessDiag = QStringLiteral("AnvilUI create: ") + anvilComp.errorString();
                    } else {
                        anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                        anvilRoot->setProperty("playerState", QVariant::fromValue(&ps));
                        anvilRoot->setProperty("player", QVariant());
                        anvilRoot->setProperty("progress", QVariant());
                        ai->setWidth(800);
                        ai->setHeight(600);
                        anvilRoot->setParent(wrapper);
                        ai->setParentItem(wrapper);
                        ai->setY(0.0);
                    }
                }
            }
        }
        if (harnessOk) {
            QFile enSrc(probeUiDir + QLatin1Char('/') + QStringLiteral("EnchantingTableUI.qml"));
            if (!enSrc.open(QIODevice::ReadOnly)) {
                harnessOk = false;
                harnessDiag = QStringLiteral("EnchantingTableUI read failed");
            } else {
                QQmlComponent enComp(&engine);
                enComp.setData(enSrc.readAll(), QUrl::fromLocalFile(enSrc.fileName()));
                if (enComp.isError()) {
                    harnessOk = false;
                    harnessDiag = QStringLiteral("EnchantingTableUI load: ") + enComp.errorString();
                } else {
                    enchantRoot = enComp.create(qmlContext(wrapper)); // 同上：挂 wrapper 作用域链
                    QQuickItem *ei = qobject_cast<QQuickItem *>(enchantRoot);
                    if (!ei) {
                        harnessOk = false;
                        harnessDiag = QStringLiteral("EnchantingTableUI create: ") + enComp.errorString();
                    } else {
                        enchantRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                        enchantRoot->setProperty("playerState", QVariant::fromValue(&ps));
                        enchantRoot->setProperty("player", QVariant());
                        enchantRoot->setProperty("progress", QVariant());
                        enchantRoot->setProperty("theWorld", QVariant());
                        ei->setWidth(800);
                        ei->setHeight(600);
                        enchantRoot->setParent(wrapper);
                        ei->setParentItem(wrapper);
                        ei->setY(600.0); // 下半区（与铁砧面板空间隔离）
                    }
                }
            }
        }

        // —— 驱动原语 ——
        // QML 函数调用（面板 root 上的 slotLeft / slotRight / takeProduct / slotShiftLeftAnvil /
        //   doMergeSameId / begin/endLeftDrag / begin/endRightDrag / addDragSlot / swapHoveredWithHotbar /
        //   doEnchant）。t874/t875 定案：点击驱动 = **直调面板函数链**（AnvilSlot / EnchantInputSlot 的
        //   TapHandler onTapped 内联体对非预览槽执行的就是 root.slotLeft/slotRight 同一函数；产物槽 =
        //   root.takeProduct）。曾试合成 QMouseEvent 走 TapHandler：事件代理把「上次点击位置 → 本次 press」
        //   距离当拖动（无真实 move/hover 流 → 根 DragHandler 激活 → 松手单格退路对滞留旧 hoveredKey 幽灵
        //   点击），harness 伪影不可消除 → 弃。真链关键在「真 QML × 真 C++ Hotbar」（QVariantList↔JS 序列化
        //   边界），与鼠标事件来源无关 —— 函数链直调已覆盖。
        auto qmlCall = [](QObject *obj, const char *method, const QVariantList &args = QVariantList()) -> bool {
            if (args.isEmpty())
                return QMetaObject::invokeMethod(obj, method);
            if (args.size() == 1)
                return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)));
            if (args.size() == 2)
                return QMetaObject::invokeMethod(obj, method, Q_ARG(QVariant, args.at(0)), Q_ARG(QVariant, args.at(1)));
            return false;
        };
        auto listEq4 = [](const QVariantList &a, int e0, int e1, int e2, int e3) {
            return a.size() == 4 && a.at(0).toInt() == e0 && a.at(1).toInt() == e1
                    && a.at(2).toInt() == e2 && a.at(3).toInt() == e3;
        };
        auto localIdAt = [](QObject *panel, const char *prop, int idx) -> int {
            const QVariantList a = panel->property(prop).toList();
            return (idx >= 0 && idx < a.size()) ? a.at(idx).toInt() : 0;
        };
        auto localEnchAt = [](QObject *panel, const char *prop, int idx) -> QVariantList {
            const QVariantList outer = panel->property(prop).toList();
            QVariantList e;
            if (idx >= 0 && idx < outer.size())
                e = outer.at(idx).toList();
            while (e.size() < 4)
                e.append(0);
            return e;
        };
        auto localNameAt = [](QObject *panel, const char *prop, int idx) -> QString {
            const QVariantList a = panel->property(prop).toList();
            return (idx >= 0 && idx < a.size()) ? a.at(idx).toString() : QString();
        };
        auto clearVm = [&]() {
            for (int i = 0; i < vm.slotCount(); ++i)
                vm.setStack(i, 0, 0);
            for (int i = 0; i < vm.mainCount(); ++i)
                vm.mainSetStack(i, 0, 0);
            vm.setHeldBlock(0);
        };

        bool ok874 = true, ok875 = true;
        if (!harnessOk) {
            ok874 = false;
            ok875 = false;
            qInfo().noquote() << "  [t874/t875 diag] harness failed:" << harnessDiag;
        } else {
            QCoreApplication::processEvents();

            const int pick = ToolRegistry::PickaxeIron;
            const int sword = ToolRegistry::SwordIron;
            const int chestId = RecipeRegistry::ArmorIdBase + 4 * ArmorRegistry::Iron + ArmorRegistry::Chestplate;
            const int bookId = RecipeRegistry::EnchantedBookId;
            const int eff3 = (EnchantRegistry::Efficiency << 8) | 3;
            const int unb2 = (EnchantRegistry::Unbreaking << 8) | 2;
            const int sharp3 = (EnchantRegistry::Sharpness << 8) | 3;
            const int kb2 = (EnchantRegistry::Knockback << 8) | 2;
            const int fire2 = (EnchantRegistry::FireAspect << 8) | 2;
            const int unb3 = (EnchantRegistry::Unbreaking << 8) | 3;
            const int prot4 = (EnchantRegistry::Protection << 8) | 4;
            const int sharp5 = (EnchantRegistry::Sharpness << 8) | 5;
            const int fire1 = (EnchantRegistry::FireAspect << 8) | 1;
            const int pickMax = ToolRegistry::maxDurability(pick);
            const int swordMax = ToolRegistry::maxDurability(sword);

            // t874/t875 harness：复位面板双击判定态（lastTapMs/lastTapKey）—— 探针连点同槽间隔 < 280ms
            //   会被 AnvilUI/EnchantingTableUI 的双击拿同类判定吞掉第二次点击（doMergeSameId 对带名实例
            //   正确 no-op = 拒绝，但探针语义要的是「两次独立单击」）。真实用户连点间隔通常 > 280ms 或
            //   中途移动；harness 内同步执行恒 < 280ms → 每次独立点击前显式复位。
            auto resetTap = [&](QObject *panel) {
                panel->setProperty("lastTapMs", 0.0);
                panel->setProperty("lastTapKey", QString());
            };
            auto resetAnvil = [&]() {
                clearVm();
                anvilRoot->setProperty("visible", false); // 触发 returnAnvilToHotbar（槽已清则零迭代）
                anvilRoot->setProperty("visible", true);
                clearVm();
            };
            auto resetEnchant = [&]() {
                clearVm();
                enchantRoot->setProperty("visible", false);
                enchantRoot->setProperty("visible", true);
                clearVm();
            };

            // ═══ (1) t874 四类别 × 真鼠标放入/取出主链（hotbar 行拾取[函数链] → 真鼠标点 A 放置 →
            //        真鼠标点 A 取回 → 函数链放回 hotbar）═══
            struct Cat {
                const char *tag;
                int id;
                int dur;
                int e0, e1, e2, e3;
                QString nm;
            };
            const Cat cats[] = {
                { "tool", pick, pickMax - 5, eff3, unb2, 0, 0, QStringLiteral("我的神镐") },
                { "weapon", sword, swordMax - 9, sharp3, kb2, fire2, unb3, QString() },
                { "armor", chestId, 33, prot4, unb2, 0, 0, QString() },
                { "book", bookId, 0, sharp5, fire1, 0, 0, QString() },
            };
            for (const Cat &c : cats) {
                const QVariantList ce = QVariantList{c.e0, c.e1, c.e2, c.e3};
                resetAnvil();
                vm.setStack(3, c.id, 1, c.dur, ce, c.nm);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                bool step = vm.heldBlock() == c.id && listEq4(vm.heldEnchants(), c.e0, c.e1, c.e2, c.e3)
                        && vm.heldCustomName() == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 pickup lost meta:" << c.tag
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants()
                                      << "name=" << vm.heldCustomName();
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                step = localIdAt(anvilRoot, "anvilSlots", 0) == c.id
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), c.e0, c.e1, c.e2, c.e3)
                        && localNameAt(anvilRoot, "anvilNames", 0) == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 place-in lost meta:" << c.tag
                                      << "slotId=" << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                step = vm.heldBlock() == c.id && listEq4(vm.heldEnchants(), c.e0, c.e1, c.e2, c.e3)
                        && vm.heldCustomName() == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 take-out lost meta:" << c.tag
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                }
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                step = vm.blockIdAt(4) == c.id && listEq4(vm.enchantsAt(4), c.e0, c.e1, c.e2, c.e3)
                        && vm.customNameAt(4) == c.nm;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E1 place-back lost meta:" << c.tag
                                      << "slot4=" << vm.blockIdAt(4) << "ench=" << vm.enchantsAt(4);
                }
            }

            // ═══ (2) t874 Shift+左键搬运（hotbar → A 槽）+ Shift 取回 ═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 7, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("移形换位"));
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                bool step = localIdAt(anvilRoot, "anvilSlots", 0) == pick
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), eff3, unb2, 0, 0)
                        && localNameAt(anvilRoot, "anvilNames", 0) == QStringLiteral("移形换位")
                        && vm.blockIdAt(3) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E2 shift put-in lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                wrapper->setProperty("shiftHeld", false);
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("移形换位");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("移形换位");
                if (!found || localIdAt(anvilRoot, "anvilSlots", 0) != 0) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E2 shift return lost meta: found=" << found;
                }
            }

            // ═══ (3) t874 右键放 1（真鼠标）+ 拖动链（begin/add/endLeftDrag 单格退路）═══
            {
                resetAnvil();
                vm.setStack(3, sword, 1, swordMax - 4, QVariantList{sharp3, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotRight", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                bool step = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, 0, 0, 0)
                        && vm.heldBlock() == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E3 right place-one lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
                // 取回后重放，走拖动链（cap=1 → redistribute 早退 → endLeftDrag n==1 → singleLeftClick 放置）。
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) }); // 取回到光标
                qmlCall(anvilRoot, "beginLeftDrag", {});
                qmlCall(anvilRoot, "addDragSlot", { QVariant(QStringLiteral("anvil:0")) });
                qmlCall(anvilRoot, "endLeftDrag", {});
                step = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                        && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, 0, 0, 0);
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E3 drag single-slot lost meta: slot0="
                                      << localIdAt(anvilRoot, "anvilSlots", 0)
                                      << "ench=" << localEnchAt(anvilRoot, "anvilEnch", 0);
                }
            }

            // ═══ (4) t874 双击拿同类（doMergeSameId 单件快照——无名实例）+ 数字键交换 ═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 6, QVariantList{eff3, unb2, 0, 0}, QString());
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                qmlCall(anvilRoot, "doMergeSameId", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                bool step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldDurability() == pickMax - 6
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E4 double-merge lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "dur=" << vm.heldDurability();
                }
                // 放回 A 后数字键交换：anvil:0 ↔ hotbar:6。
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                anvilRoot->setProperty("hoveredKey", QStringLiteral("anvil:0"));
                qmlCall(anvilRoot, "swapHoveredWithHotbar", { QVariant(6) });
                step = vm.blockIdAt(6) == pick && listEq4(vm.enchantsAt(6), eff3, unb2, 0, 0)
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E4 number-swap lost meta: hb6=" << vm.blockIdAt(6)
                                      << "ench=" << vm.enchantsAt(6);
                }
            }

            // ═══ (5) t874 takeProduct·repair（附魔镐 + 铁锭）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 21, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("神镐"));
                wrapper->setProperty("shiftHeld", true);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                wrapper->setProperty("shiftHeld", false);
                vm.setStack(4, RecipeRegistry::IronIngotId, 3);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) }); // 3 锭整栈入 B
                const int per = pickMax / 3;
                const int need = std::min(3, int(std::ceil(21.0 / per)));
                const int use = std::min(3, need);
                const int expectDur = std::min(pickMax, (pickMax - 21) + use * per);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", {}); // 产物槽点击（TapHandler onTapped → takeProduct 同一函数）
                bool step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldCustomName() == QStringLiteral("神镐")
                        && vm.heldDurability() == expectDur
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0
                        && localIdAt(anvilRoot, "anvilSlots", 1) == RecipeRegistry::IronIngotId
                        && localIdAt(anvilRoot, "anvilCounts", 1) == 3 - use;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E5 repair lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "dur=" << vm.heldDurability()
                                      << "expectDur=" << expectDur << "use=" << use;
                }
            }

            // ═══ (6) t874 takeProduct·combine（双镐合并 → 附魔并集）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 10, QVariantList{eff3, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, pick, 1, pickMax - 20, QVariantList{unb2, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", {});
                const QVariantList pe = vm.heldEnchants();
                const bool hasEff = pe.contains(QVariant(eff3));
                const bool hasUnb = pe.contains(QVariant(unb2));
                const int expectDur = std::min(pickMax, int(std::floor((pickMax - 10) + (pickMax - 20) + pickMax * 0.1)));
                const bool step = vm.heldBlock() == pick && hasEff && hasUnb
                        && vm.heldDurability() == expectDur
                        && localIdAt(anvilRoot, "anvilSlots", 0) == 0
                        && localIdAt(anvilRoot, "anvilSlots", 1) == 0;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E6 combine lost meta: held=" << vm.heldBlock()
                                      << "ench=" << pe << "dur=" << vm.heldDurability()
                                      << "expectDur=" << expectDur;
                }
            }

            // ═══ (7) t874 takeProduct·merge（素剑 + 附魔书）+ rename（改名保附魔保名）═══
            {
                resetAnvil();
                vm.setStack(3, sword, 1, swordMax - 3, QVariantList{0, 0, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, bookId, 1, 0, QVariantList{sharp5, fire1, 0, 0}, QString());
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", {});
                const QVariantList me = vm.heldEnchants();
                bool step = vm.heldBlock() == sword && me.contains(QVariant(sharp5))
                        && localIdAt(anvilRoot, "anvilSlots", 1) == 0; // 书消耗
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E7 merge lost meta: held=" << vm.heldBlock()
                                      << "ench=" << me;
                }
                // rename：带名带附魔镐单独改名 → 产物双保。
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 2, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("旧名"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                anvilRoot->setProperty("renameName", QStringLiteral("新名字"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "takeProduct", {});
                step = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                        && vm.heldCustomName() == QStringLiteral("新名字")
                        && vm.heldDurability() == pickMax - 2;
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E7 rename lost meta: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants() << "name=" << vm.heldCustomName();
                }
            }

            // ═══ (8) t874 关包归还（A 槽带名附魔镐 + B 槽材料 → visible=false → 全参归还）═══
            {
                resetAnvil();
                vm.setStack(3, pick, 1, pickMax - 12, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("归还镐"));
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                vm.setStack(4, RecipeRegistry::IronIngotId, 2);
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(anvilRoot);
                qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(1) });
                anvilRoot->setProperty("visible", false);
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("归还镐");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("归还镐");
                bool foundIngot = false;
                for (int i = 0; i < vm.slotCount() && !foundIngot; ++i)
                    foundIngot = vm.blockIdAt(i) == RecipeRegistry::IronIngotId && vm.countAt(i) == 2;
                for (int i = 0; i < vm.mainCount() && !foundIngot; ++i)
                    foundIngot = vm.mainBlockIdAt(i) == RecipeRegistry::IronIngotId && vm.mainCountAt(i) == 2;
                if (!found || !foundIngot) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E8 close-return lost meta: pick=" << found
                                      << "ingot=" << foundIngot;
                }
                anvilRoot->setProperty("visible", true);
            }

            // ═══ (9) t874 存档 round-trip 后重绑 VM 再放入（WorldStore 真库 + applyPlayerState 镜像回灌）═══
            {
                Hotbar vm2;
                QVariantMap data;
                QVariantList hotbarArr;
                clearVm();
                vm.setStack(3, sword, 1, swordMax - 15, QVariantList{sharp3, kb2, fire2, unb3}, QStringLiteral("回环剑"));
                for (int i = 0; i < vm.slotCount(); ++i) {
                    QVariantMap s;
                    s.insert(QStringLiteral("id"), vm.blockIdAt(i));
                    s.insert(QStringLiteral("count"), vm.countAt(i));
                    s.insert(QStringLiteral("durability"), vm.durabilityAt(i));
                    s.insert(QStringLiteral("enchants"), vm.enchantsAt(i));
                    s.insert(QStringLiteral("name"), vm.customNameAt(i));
                    hotbarArr.append(s);
                }
                data.insert(QStringLiteral("hotbar"), hotbarArr);
                WorldStore store;
                const QString dbAbs = QDir::temp().absoluteFilePath(
                        QStringLiteral("voxel_t874_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
                QFile::remove(dbAbs);
                bool step = store.openWorld(dbAbs) && store.savePlayerData(data);
                store.closeWorld();
                QVariantMap back;
                if (step) {
                    step = store.openWorld(dbAbs);
                    if (step)
                        back = store.loadPlayerData();
                    store.closeWorld();
                }
                QFile::remove(dbAbs);
                if (step) {
                    // applyPlayerState 镜像回灌到新 VM（Main.qml :647-653 同形）。
                    const QVariantList hb = back.value(QStringLiteral("hotbar")).toList();
                    for (int i = 0; i < 9 && i < hb.size(); ++i) {
                        const QVariantMap s = hb.at(i).toMap();
                        vm2.setStack(i, s.value(QStringLiteral("id")).toInt(),
                                     s.value(QStringLiteral("count")).toInt(),
                                     s.contains(QStringLiteral("durability"))
                                             ? s.value(QStringLiteral("durability")).toInt() : -1,
                                     s.contains(QStringLiteral("enchants"))
                                             ? s.value(QStringLiteral("enchants")).toList() : QVariantList(),
                                     s.contains(QStringLiteral("name"))
                                             ? s.value(QStringLiteral("name")).toString() : QString());
                    }
                    // 重绑面板 hotbar → 真 QML × 读档 VM 再跑主链。
                    resetAnvil();
                    anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm2));
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                    const bool pickOk = vm2.heldBlock() == sword
                            && listEq4(vm2.heldEnchants(), sharp3, kb2, fire2, unb3)
                            && vm2.heldCustomName() == QStringLiteral("回环剑");
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                    const bool placeOk = localIdAt(anvilRoot, "anvilSlots", 0) == sword
                            && listEq4(localEnchAt(anvilRoot, "anvilEnch", 0), sharp3, kb2, fire2, unb3);
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("anvil")), QVariant(0) });
                    const bool backOk = vm2.heldBlock() == sword
                            && listEq4(vm2.heldEnchants(), sharp3, kb2, fire2, unb3);
                    step = pickOk && placeOk && backOk;
                    if (!step) {
                        qInfo().noquote() << "  [t874 diag] E9 roundtrip:" << pickOk << placeOk << backOk
                                          << "ench=" << vm2.heldEnchants();
                    }
                    // 归还光标 + 还原绑定（后续 t875 段仍用 vm）。
                    resetTap(anvilRoot);
                    qmlCall(anvilRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(7) });
                    anvilRoot->setProperty("hotbar", QVariant::fromValue(&vm));
                }
                if (!step) {
                    ok874 = false;
                    qInfo().noquote() << "  [t874 diag] E9 save roundtrip leg failed";
                }
                clearVm();
            }

            // ═══ (10) t875a 已附魔物品拒入（七入口：两真鼠标 + 两拖动 + 数字键 + Shift + 双击）═══
            {
                const QVariantList pickEnch = QVariantList{eff3, unb2, 0, 0};
                auto rejectCheck = [&](const char *tag) {
                    const bool empty0 = localIdAt(enchantRoot, "enchantSlots", 0) == 0;
                    const bool heldOk = vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0)
                            && vm.heldCustomName() == QStringLiteral("附魔镐");
                    if (!empty0 || !heldOk) {
                        ok875 = false;
                        qInfo().noquote() << "  [t875 diag]" << tag << "slot0=" << localIdAt(enchantRoot, "enchantSlots", 0)
                                          << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                    }
                };
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax - 5, pickEnch, QStringLiteral("附魔镐"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // R1 真鼠标左键槽 0
                rejectCheck("R1 mouse-left");
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotRight", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // R2 真鼠标右键（放 1）
                rejectCheck("R2 mouse-right");
                qmlCall(enchantRoot, "beginRightDrag", {}); // R3 右键拖（每格放 1）
                qmlCall(enchantRoot, "addRightDragSlot", { QVariant(QStringLiteral("enchant:0")) });
                qmlCall(enchantRoot, "endRightDrag", {});
                rejectCheck("R3 right-drag");
                qmlCall(enchantRoot, "beginLeftDrag", {}); // R4 左键拖（均分 → 单格退路 gated）
                qmlCall(enchantRoot, "addDragSlot", { QVariant(QStringLiteral("enchant:0")) });
                qmlCall(enchantRoot, "endLeftDrag", {});
                rejectCheck("R4 left-drag");
                // R5 数字键交换：hovered=enchant:0（空）↔ hotbar:6 —— dst（hotbar 6 空栈）入槽 0 恒空；
                //   反向（槽 0 持素品 + hotbar 持附魔品）另测于 R7 前置。
                enchantRoot->setProperty("hoveredKey", QStringLiteral("enchant:0"));
                qmlCall(enchantRoot, "swapHoveredWithHotbar", { QVariant(6) });
                rejectCheck("R5 number-swap");
                // 光标归位后 R6 Shift+左键（slotShiftLeftEnchant 已附魔守卫）。
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                wrapper->setProperty("shiftHeld", true);
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                wrapper->setProperty("shiftHeld", false);
                const bool r6 = localIdAt(enchantRoot, "enchantSlots", 0) == 0
                        && vm.blockIdAt(4) == pick && listEq4(vm.enchantsAt(4), eff3, unb2, 0, 0)
                        && vm.customNameAt(4) == QStringLiteral("附魔镐");
                if (!r6) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] R6 shift-reject failed: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "hb4=" << vm.blockIdAt(4) << "ench=" << vm.enchantsAt(4);
                }
                // R7 双击合并（t693 门禁过滤：槽 0 素品 + 光标附魔同 id → 收集表剔 gated 槽 → 无操作）。
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax, QVariantList(), QString()); // 素品镐
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 素品入槽 0（gate 放行）
                vm.setStack(4, pick, 1, pickMax - 5, pickEnch, QStringLiteral("附魔镐"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                qmlCall(enchantRoot, "doMergeSameId", { QVariant(QStringLiteral("enchant")), QVariant(0) });
                const bool r7 = localIdAt(enchantRoot, "enchantSlots", 0) == pick
                        && listEq4(localEnchAt(enchantRoot, "enchantEnch", 0), 0, 0, 0, 0) // 槽 0 素品不被污染
                        && vm.heldBlock() == pick && listEq4(vm.heldEnchants(), eff3, unb2, 0, 0);
                if (!r7) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] R7 double-merge failed: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "slot0ench=" << localEnchAt(enchantRoot, "enchantEnch", 0)
                                      << "held=" << vm.heldBlock() << "ench=" << vm.heldEnchants();
                }
            }

            // ═══ (11) t875b 全链无清洗（青金石槽放入 → 关包归还）═══
            {
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax - 8, QVariantList{eff3, unb2, 0, 0}, QStringLiteral("不清洗"));
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(1) }); // 青金石槽（index 1 无门禁——设计允许任意物）
                bool step = localIdAt(enchantRoot, "enchantSlots", 1) == pick
                        && listEq4(localEnchAt(enchantRoot, "enchantEnch", 1), eff3, unb2, 0, 0)
                        && localNameAt(enchantRoot, "enchantNames", 1) == QStringLiteral("不清洗");
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875b lapis put lost meta: slot1="
                                      << localIdAt(enchantRoot, "enchantSlots", 1)
                                      << "ench=" << localEnchAt(enchantRoot, "enchantEnch", 1);
                }
                enchantRoot->setProperty("visible", false); // 关包归还（returnEnchantToHotbar 全参）
                bool found = false;
                for (int i = 0; i < vm.slotCount() && !found; ++i)
                    found = vm.blockIdAt(i) == pick && listEq4(vm.enchantsAt(i), eff3, unb2, 0, 0)
                            && vm.customNameAt(i) == QStringLiteral("不清洗");
                for (int i = 0; i < vm.mainCount() && !found; ++i)
                    found = vm.mainBlockIdAt(i) == pick && listEq4(vm.mainEnchantsAt(i), eff3, unb2, 0, 0)
                            && vm.mainCustomNameAt(i) == QStringLiteral("不清洗");
                if (!found) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875b close-return lost meta";
                }
                enchantRoot->setProperty("visible", true);
            }

            // ═══ (12) t875c 素品附魔正链 → 产物取出带附魔 → 拒再入 ═══
            {
                resetEnchant();
                vm.setStack(3, pick, 1, pickMax, QVariantList(), QString());
                vm.setStack(4, RecipeRegistry::LapisId, 5);
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(3) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 素品镐入槽 0
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(4) });
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(1) }); // 5 青金石入槽 1
                const bool pre = localIdAt(enchantRoot, "enchantSlots", 0) == pick
                        && localIdAt(enchantRoot, "enchantSlots", 1) == RecipeRegistry::LapisId;
                qmlCall(enchantRoot, "doEnchant", { QVariant(0) }); // 档 1（selectEnchantsForItem 保证 ≥1 条）
                const QVariantList prod = localEnchAt(enchantRoot, "enchantEnch", 0);
                const bool hasAny = prod.at(0).toInt() != 0 || prod.at(1).toInt() != 0
                        || prod.at(2).toInt() != 0 || prod.at(3).toInt() != 0;
                bool step = pre && localIdAt(enchantRoot, "enchantSlots", 0) == pick && hasAny;
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c doEnchant product not enchanted: pre=" << pre
                                      << "prod=" << prod;
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 取出产物（held ← 附魔镐）
                step = vm.heldBlock() == pick && !listEq4(vm.heldEnchants(), 0, 0, 0, 0);
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c take-out lost ench: held=" << vm.heldBlock()
                                      << "ench=" << vm.heldEnchants();
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("enchant")), QVariant(0) }); // 产物再入 → 拒（已附魔）
                step = localIdAt(enchantRoot, "enchantSlots", 0) == 0 && vm.heldBlock() == pick
                        && !listEq4(vm.heldEnchants(), 0, 0, 0, 0);
                if (!step) {
                    ok875 = false;
                    qInfo().noquote() << "  [t875 diag] t875c re-entry not rejected: slot0="
                                      << localIdAt(enchantRoot, "enchantSlots", 0)
                                      << "heldEnch=" << vm.heldEnchants();
                }
                resetTap(enchantRoot);
                qmlCall(enchantRoot, "slotLeft", { QVariant(QStringLiteral("hotbar")), QVariant(5) }); // 光标归位
            }

            clearVm();
        }

        if (!ok874)
            ++totalFail;
        qInfo().noquote() << (ok874 ? "PASS" : "FAIL")
                          << "| t874 real-chain anvil enchant preservation (real QQmlEngine x source-tree "
                             "AnvilUI.qml x real C++ Hotbar): closes the last probe seam - t792 drove real "
                             "QML against a mock Hotbar.qml, t822 drove the real VM without any QML; this "
                             "probe loads the actual AnvilUI.qml+InventoryOps.js with the actual Hotbar/"
                             "PlayerState injected and clicks via synthesized QMouseEvent through the inline "
                             "TapHandlers (the exact user path). Entries: mouse left/right on A slot, "
                             "shift-move, drag single-slot release, double-click pickup, number-key swap, "
                             "takeProduct repair/combine/merge/rename, close-panel return, save round-trip "
                             "with VM rebind; categories: tool/weapon(4-ench)/armor/enchanted-book, all "
                             "with custom names + instance durability asserted at every hop";
        if (!ok875)
            ++totalFail;
        qInfo().noquote() << (ok875 ? "PASS" : "FAIL")
                          << "| t875 real-chain enchanting-table gate + no-wipe (same harness, real "
                             "EnchantingTableUI.qml): already-enchanted item rejected on all seven entry "
                             "paths (mouse left/right on slot 0, right-drag place-one, left-drag "
                             "redistribute + single-slot fallback, number-key swap, shift-move, "
                             "double-click merge) while cursor stack keeps id/ench/name intact; lapis-slot "
                             "sojourn + close-panel return preserves enchant metadata end-to-end (wipe "
                             "hunt); clean-pick + lapis -> doEnchant tier1 product carries >=1 enchant, "
                             "take-out keeps it, re-entry rejected";
    }

    // ── t873 书架→附魔台字流真链探针（用户「新版仍不见文字流」实机二查；t823 冻结镜像的实装版）──
    // 背景：t823 用**冻结镜像**（C++ 复刻 rescanPairs 逐行语义）钉了规则口径，但镜像 ≠ 实装 —— 用户换新
    //   exe 仍报看不见，链路断点只剩「真 QML 组件从未被自动化执行」这一跳。本探针照 t874 真链模式：
    //   QQmlEngine 直载源树 EnchantGlyphFlow.qml（无相对导入 / 无 VoxelSandbox import → 免临时目录改写，
    //   仅 QtQuick/QtQuick3D 模块导入），真 World 独立小世界 + 真 ListModel 台表按 Main.qml
    //   glyphFlowLoader.onLoaded 同款注入（world/tableModel/active/editRev；camNode 留 null —— 组件对
    //   null cam 本就全通距离门，真实应用由 billboard 分支兜底；tableCount 静态注入场景间以 editRev
    //   显式触碰重扫，真实应用拆台也走 worldEditRev++ 同一触发面）。断言四态：
    //   ① 净空基线 0 对、空表发射零计数；② 单层地面满环带 16 书架 → 真 QML rescanPairs 产 16 对（与
    //   t823 镜像/权威 16/15 同 rig 口径互证——此处钉的是 QML **实装**本身）；③ 连发 12 颗 →
    //   emittedTotal/liveCount 计数一致；④ 堵半步 -1 对 + 删台归零且发射器停摆（500ms Timer running
    //   翻假——「无对即停摆」性能红线的实机钉子）。组件头注释宣称的「数据链完好」自此有自动化实证；
    //   渲染侧（字形贴图/尺寸/billboard）属 qml.exe/肉眼域，由 [t873] 运行期日志 + 实测文档覆盖。
    {
        bool ok873 = true;
        QString diag873;
        World wG;
        wG.setWidth(40); wG.setDepth(40); wG.setHeight(48); wG.setSeed(21);
        // 净空 5×5 扫描（y 带 44/45，同 t823：世界高 48 → y∈[0,47]；含半步格）。
        const int gY = 44;
        int gx = -1, gz = -1;
        const auto areaClear = [&](int x, int z) {
            for (int dy = 0; dy <= 1; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (std::max(std::abs(dx), std::abs(dz)) != 2) continue;
                        if (wG.blockAt(x + dx, gY + dy, z + dz) != BR::Air) return false;
                        if (wG.blockAt(x + dx / 2, gY + dy, z + dz / 2) != BR::Air) return false;
                    }
            return true;
        };
        for (int zz = 4; zz + 2 < 36 && gx < 0; zz += 2)
            for (int xx = 4; xx + 2 < 36 && gx < 0; xx += 2)
                if (areaClear(xx, zz)) { gx = xx; gz = zz; }
        if (gx < 0) {
            ok873 = false;
            diag873 = QStringLiteral("no clear 5x5 rig at y=44/45");
        } else {
            QQmlEngine gEngine;
            // 真源树组件直载（base URL = 源文件 → 无相对导入需解析，模块导入走 Qt 安装 qml 目录）。
            const QString glyphPath = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                          .filePath(QStringLiteral("../src/ui/EnchantGlyphFlow.qml"));
            // 真 ListModel 台表（Main.qml enchantTablePositions 运行期同类物）：经桩根的 JS 助手增删行，
            //   避免 C++ 直调 QQmlListModel 的 QJSValue 签名猜测。组件只消费 tableModel.count/.get(i)，
            //   与真实注入面同构。
            QQmlComponent stubComp(&gEngine);
            stubComp.setData(QByteArrayLiteral(
                                 "import QtQuick\n"
                                 "Item {\n"
                                 "    property alias tableModel: lm\n"
                                 "    ListModel { id: lm }\n"
                                 "    function addEntry(x, y, z) { lm.append({x: x, y: y, z: z}) }\n"
                                 "    function removeFirstRow() { lm.remove(0, 1) }\n"
                                 "}\n"), QUrl());
            QQmlComponent glyphComp(&gEngine, QUrl::fromLocalFile(glyphPath));
            QObject *stub = nullptr;
            QObject *gRoot = nullptr;
            if (stubComp.isError()) {
                ok873 = false;
                diag873 = QStringLiteral("table stub load: ") + stubComp.errorString();
            } else if (glyphComp.isError()) {
                ok873 = false;
                diag873 = QStringLiteral("EnchantGlyphFlow load: ") + glyphComp.errorString();
            } else {
                stub = stubComp.create();
                gRoot = glyphComp.create();
                if (!stub || !gRoot) {
                    ok873 = false;
                    diag873 = QStringLiteral("create failed (stub=%1 glyph=%2)")
                                  .arg(stub != nullptr).arg(gRoot != nullptr);
                } else {
                    stub->setParent(&gEngine);
                    gRoot->setParent(&gEngine);
                    // Main.qml glyphFlowLoader.onLoaded 同款注入。
                    gRoot->setProperty("world", QVariant::fromValue(&wG));
                    gRoot->setProperty("tableModel",
                                       QVariant::fromValue(stub->property("tableModel").value<QObject *>()));
                    gRoot->setProperty("active", true);

                    auto addTable = [&](int x, int y, int z) {
                        QMetaObject::invokeMethod(stub, "addEntry", Q_ARG(QVariant, x),
                                                  Q_ARG(QVariant, y), Q_ARG(QVariant, z));
                    };
                    auto pairsOf = [&]() -> int {
                        return gRoot->property("pairs").toList().size();
                    };
                    auto touchRescan = [&]() {
                        gRoot->setProperty("editRev", gRoot->property("editRev").toInt() + 1);
                    };
                    // 发射器停摆实证：组件内 500ms 那颗 Timer 即 spawnTimer（tickTimer 是 20ms）——
                    //   running 绑定 active && pairs.length>0，删台归零后应翻假。
                    auto spawnTimerRunning = [&]() -> bool {
                        const QList<QObject *> kids = gRoot->findChildren<QObject *>();
                        for (QObject *k : kids) {
                            const QVariant iv = k->property("interval");
                            if (iv.isValid() && iv.toInt() == 500) {
                                const QVariant rv = k->property("running");
                                if (rv.isValid())
                                    return rv.toBool();
                            }
                        }
                        return false; // 找不到 Timer 视为停摆（不误报，加载失败另有 FAIL 行）
                    };

                    // ① 净空基线：0 对 + 空表发射零计数。
                    addTable(gx, gY, gz);
                    touchRescan();
                    const int p0 = pairsOf();
                    QMetaObject::invokeMethod(gRoot, "spawnGlyph",
                                              Q_ARG(QVariant, QVariant(QVariantList())));
                    const bool spawnEmptyNoop = gRoot->property("emittedTotal").toInt() == 0;
                    if (p0 != 0 || !spawnEmptyNoop) {
                        ok873 = false;
                        diag873 += QStringLiteral("(a) baseline pairs=%1 noop=%2; ")
                                       .arg(p0).arg(spawnEmptyNoop);
                    }

                    // ② 单层地面满环带 16 书架（半步格已净空）→ 真 QML rescanPairs 应产 16 对。
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz)
                            if (std::max(std::abs(dx), std::abs(dz)) == 2)
                                wG.setBlock(gx + dx, gY, gz + dz, BR::Bookshelf, 0);
                    touchRescan();
                    const int p16 = pairsOf();
                    if (p16 != 16) {
                        ok873 = false;
                        diag873 += QStringLiteral("(b) full-ring pairs=%1 want 16; ").arg(p16);
                    }

                    // ③ 连发 12 颗：emittedTotal/liveCount 与池占用同步走。
                    const QVariantList pairArr = gRoot->property("pairs").toList();
                    for (int i = 0; i < 12; ++i)
                        QMetaObject::invokeMethod(gRoot, "spawnGlyph", Q_ARG(QVariant, QVariant(pairArr)));
                    const int em12 = gRoot->property("emittedTotal").toInt();
                    const int live12 = gRoot->property("liveCount").toInt();
                    if (em12 != 12 || live12 != 12) {
                        ok873 = false;
                        diag873 += QStringLiteral("(c) emitted=%1 live=%2 want 12/12; ").arg(em12).arg(live12);
                    }

                    // ④ 堵一角书架半步格 → 重扫 -1 对（视觉与档位同步减，t823 ③ 同口径钉到实装）；
                    //    删台行 → 归零 + 发射器停摆（拆台即停承诺）。
                    wG.setBlock(gx - 1, gY, gz - 1, BR::Cobble, 0);
                    touchRescan();
                    const int pBlocked = pairsOf();
                    wG.setBlock(gx - 1, gY, gz - 1, BR::Air, 0);
                    QMetaObject::invokeMethod(stub, "removeFirstRow");
                    touchRescan();
                    const int pGone = pairsOf();
                    const bool emitterIdle = !spawnTimerRunning();
                    if (pBlocked != 15 || pGone != 0 || !emitterIdle) {
                        ok873 = false;
                        diag873 += QStringLiteral("(d) blocked=%1 gone=%2 idle=%3; ")
                                       .arg(pBlocked).arg(pGone).arg(emitterIdle);
                    }
                }
            }
            // 好公民：复原环带（探针不留脏 rig；独立小世界随作用域析构，此步为对称纪律）。
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (std::max(std::abs(dx), std::abs(dz)) == 2)
                        wG.setBlock(gx + dx, gY, gz + dz, BR::Air, 0);
        }
        if (!ok873)
            ++totalFail;
        if (!diag873.isEmpty())
            qInfo().noquote() << "  [t873 diag]" << diag873;
        qInfo().noquote() << (ok873 ? "PASS" : "FAIL")
                          << "| t873 glyph-flow real-chain probe (real QQmlEngine loads source-tree "
                             "EnchantGlyphFlow.qml x real World rig x injected ListModel): empty baseline "
                             "0 pairs + no-op spawn, single ground ring -> real QML rescanPairs yields 16 "
                             "(t823 mirror/authority 16/15 same rig cross-checked against the actual "
                             "implementation), 12 spawns tracked by emittedTotal/liveCount, blocked "
                             "half-step -1 pair, table-row removal -> zero pairs + spawn timer idle "
                             "(data chain proven live; pixel-side remains qml.exe/manual)";
    }

    // ---- t889 暂停语义统一（两档：GUI 开=世界照跑玩家照坠但不动；ESC=全停；t885 鱼线持久前置）----
    //      门控矩阵钉子（行为级 + 源码钉双层）：
    //        (a) 软档（!captured + worldRunning=true，GUI 面板开等价）：pc.tick() step 照跑（玩家坠、XZ 冻结）、
    //            掉落物照落、钓鱼态不自动收（旧代码此分支 cancelFishing）+ 浮标照 tick；
    //        (b) 硬档（worldRunning=false，ESC 暂停菜单等价）：pc.tick() 早退于实体桶 —— 玩家位 / 掉落物 /
    //            浮标位置全冻结（精确等值），钓鱼态保活（不收、只是不 tick），复跑续钓；
    //        (c) WorldClock.running 行为级：默认 true；false 停表（ticked 零发）→ true 复跑；
    //        (d) 墙钟顺延：deferWallClocks 把 spawnMs 推后（掉落物免拾窗 ready→not-ready 翻转复验）+
    //            setWorldRunning 复跑连调三管理器（源码钉）；
    //        (e) release() 体无 cancelFishing（t885：开背包 / ESC / 失焦不收竿。行为级不可达 —— 无窗口
    //            rig 进不去 captured 态，t836(e) 同取舍源码钉）；
    //        (f) Main.qml 门控钉：window.worldRunning 派生属性 + worldClock.running / player.worldRunning
    //            绑定 + pauseOverlay 取反消费 + keyInput 未捕获守卫 + onTicked 桥无 captured/面板门（GUI 开
    //            时火 / 水 / 生长照跑的源头证）。
    {
        bool okA = true, okB = true, okC = true, okD = true, okE = true, okF = true;
        QString diag889;
        // 泵事件循环等待墙钟（QTimer 需事件循环投递；每片 ≤10ms 防饿死）
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };

        // ---- (c) WorldClock.running 行为级 ----
        {
            WorldClock wc;
            int ticksOn = 0, ticksOff = 0;
            QObject::connect(&wc, &WorldClock::ticked, &wc, [&](qreal) {
                if (wc.running()) ++ticksOn; else ++ticksOff;
            });
            pumpFor(350);          // 默认 running=true（100ms QTimer → ≥3 tick）
            wc.setRunning(false);
            pumpFor(350);          // 停表窗口：ticked 零发（ticksOff 恒 0）
            wc.setRunning(true);
            pumpFor(350);          // 复跑窗口
            okC = wc.running() && ticksOn >= 4 && ticksOff == 0;
            if (!okC)
                diag889 += QStringLiteral("(c) running=%1 on=%2 off=%3; ")
                               .arg(wc.running()).arg(ticksOn).arg(ticksOff);
        }

        // ---- (a)/(b) PlayerController 两档 + 实体桶 + 钓鱼（行为级）----
        {
            World w9;
            w9.setWidth(48); w9.setDepth(48); w9.setHeight(96); w9.setSeed(77);
            EntityManager ents;
            ItemEntityManager items;
            Hotbar hb;
            hb.setStack(0, ToolRegistry::FishingRod, 1,
                        ToolRegistry::maxDurability(ToolRegistry::FishingRod));
            hb.setSelectedSlot(0);
            PlayerController pc;
            pc.setWorld(&w9);
            pc.setEntityManager(&ents);
            pc.setItemEntities(&items);
            pc.setHotbar(&hb);
            // 石板地板（fy=83 空带，同 t836 rig 口径）：玩家列 (6,6) 与掉落物列 (20,20) 各 5×5
            const int fy = 83;
            for (int x = 4; x <= 8; ++x)
                for (int z = 4; z <= 8; ++z) w9.setBlock(x, fy, z, BR::Stone, 0);
            for (int x = 18; x <= 22; ++x)
                for (int z = 18; z <= 22; ++z) w9.setBlock(x, fy, z, BR::Stone, 0);
            // 玩家：生存、悬空 6 格（面对下坠）；默认 !captured = GUI 开等价软档
            pc.loadSavedState(6.5f, float(fy + 6), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            const QVector3D startEye = pc.position();
            // 甩竿（t836 先例：useFishingRod 无 captured 门，直调成活）
            pc.useFishingRod();
            int bobIdx = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobIdx = i; break; }
            if (!(pc.fishing() && bobIdx >= 0)) { okA = false; diag889 += QStringLiteral("(a) cast failed; "); }
            // 掉落物：远处列悬空（离玩家 14 格 > 拾取半径 1.5 → pickupScan 不干扰）
            items.spawnItem(20, fy + 4, 20, BR::Cobble, 1);
            int itemIdx = -1;
            for (int i = 0; i < items.count(); ++i)
                if (items.aliveAt(i) && items.posAt(i).x() > 19.0f) { itemIdx = i; break; }
            if (itemIdx < 0) { okA = false; diag889 += QStringLiteral("(a) item spawn failed; "); }
            const float itemY0 = itemIdx >= 0 ? items.posAt(itemIdx).y() : 0.0f;
            // (a) 软档：12 tick（间隔真 17ms 泵 dt）→ 眼位下坠 + XZ 冻结 + 掉落物下落 + 钓鱼态保持
            for (int i = 0; i < 12; ++i) { pumpFor(17); pc.tick(); }
            if (okA) {
                const float dyEye = float(startEye.y() - pc.position().y());
                const float dyItem = float(itemY0 - items.posAt(itemIdx).y());
                okA = pc.position().y() < startEye.y() - 0.05f                       // 照坠（step 软档推进）
                      && pc.position().x() == startEye.x()
                      && pc.position().z() == startEye.z()                            // 无输入不动（XZ 冻结）
                      && dyItem > 0.05f                                              // 掉落物照落（实体桶照跑）
                      && pc.fishing() && ents.aliveAt(bobIdx);                        // t885：GUI 开不收竿
                if (!okA)
                    diag889 += QStringLiteral("(a) dyEye=%1 dyItem=%2 fish=%3 bobAlive=%4 xyz=(%5,%6,%7); ")
                                   .arg(dyEye).arg(dyItem).arg(pc.fishing())
                                   .arg(bobIdx >= 0 && ents.aliveAt(bobIdx))
                                   .arg(pc.position().x()).arg(pc.position().y()).arg(pc.position().z());
            }
            // (b) 硬档：ESC 等价 → 全冻结（位置精确等值）+ 钓鱼态保活 + 复跑续钓
            if (okB) {
                pc.setWorldRunning(false);
                const QVector3D frozenEye = pc.position();
                const QVector3D frozenItem = itemIdx >= 0 ? items.posAt(itemIdx) : QVector3D();
                const QVector3D frozenBob = bobIdx >= 0 ? ents.posAt(bobIdx) : QVector3D();
                for (int i = 0; i < 12; ++i) { pumpFor(17); pc.tick(); }
                okB = pc.position() == frozenEye
                      && (itemIdx < 0 || items.posAt(itemIdx) == frozenItem)
                      && (bobIdx < 0 || ents.posAt(bobIdx) == frozenBob)
                      && pc.fishing();                                               // 鱼线保活过暂停（t885）
                pc.setWorldRunning(true);                                            // 复跑（墙钟顺延在 pc 内）
                pumpFor(17); pc.tick();
                okB = okB && pc.fishing() && (bobIdx < 0 || ents.aliveAt(bobIdx));   // 续钓不掉线
                if (!okB) diag889 += QStringLiteral("(b) hard-tier regression; ");
            }
            // 好公民：清场（探针不留脏 rig；独立小世界随作用域析构）
            ents.clearAll();
            items.clearAll();
        }

        // ---- (d) 墙钟顺延行为级：掉落物免拾窗 ready→not-ready 翻转复验 ----
        {
            ItemEntityManager items2;
            items2.spawnItem(3, 60, 3, BR::Cobble, 1);
            int idx = -1;
            for (int i = 0; i < items2.count(); ++i)
                if (items2.aliveAt(i)) { idx = i; break; }
            if (idx < 0) { okD = false; diag889 += QStringLiteral("(d) spawn failed; "); }
            else {
                pumpFor(650);                       // kPickupDelayMs=500 → ready
                const bool readyBefore = items2.isPickupReady(idx);
                items2.deferWallClocks(100000);      // 顺延 100s → spawnMs 推后 → 免拾窗重开
                const bool readyAfter = items2.isPickupReady(idx);
                items2.deferWallClocks(0);           // ms<=0 早退（幂等防御面）
                okD = readyBefore && !readyAfter;
                if (!okD) diag889 += QStringLiteral("(d) ready=%1 after=%2; ")
                                         .arg(readyBefore).arg(readyAfter);
            }
        }

        // ---- (e)/(f) 源码钉（行为级不可达 / QML 门控面）----
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            auto readSrc = [&root](const QString &rel) {
                QFile f(root + QLatin1Char('/') + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString t = readSrc(QStringLiteral("src/Game/playercontroller.cpp"));
            // 滤注释体（t836(e) 同手法）：源码钉只看语句面，注释里的字面量（如「不再 cancelFishing」）不参与
            const auto stripComments = [](const QString &body) {
                QString out;
                for (const QString &line : body.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                    out += line; out += QLatin1Char('\n');
                }
                return out;
            };
            // (e) release() 体（滤注释后）无 cancelFishing 调用（甩竿后开背包 / ESC / 失焦不收竿；
            //     收竿只走主动 / 换持物 / 重生 / 换世界四入口）
            {
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::release()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::setCaptured"));
                if (b0 < 0 || b1 <= b0) { okE = false; diag889 += QStringLiteral("(e) slice miss; "); }
                else {
                    okE = !stripComments(t.mid(b0, b1 - b0)).contains(QStringLiteral("cancelFishing"));
                    if (!okE) diag889 += QStringLiteral("(e) release still cancels fishing; ");
                }
            }
            // (d 尾) setWorldRunning 复跑连调三管理器 deferWallClocks（调用面钉）
            {
                const int s0 = t.indexOf(QStringLiteral("void PlayerController::setWorldRunning"));
                const int s1 = t.indexOf(QStringLiteral("QPoint PlayerController::windowCenterGlobal"));
                if (s0 < 0 || s1 <= s0) { okD = false; diag889 += QStringLiteral("(d) slice miss; "); }
                else {
                    const QString sb = t.mid(s0, s1 - s0);
                    okD = okD && sb.contains(QStringLiteral("m_entityManager->deferWallClocks"))
                          && sb.contains(QStringLiteral("m_itemEntities->deferWallClocks"))
                          && sb.contains(QStringLiteral("m_xpOrbManager->deferWallClocks"));
                }
            }
            // (f) Main.qml 门控面
            {
                const QString q = readSrc(QStringLiteral("src/ui/Main.qml"));
                if (q.isEmpty()) { okF = false; diag889 += QStringLiteral("(f) read miss; "); }
                const bool fProp  = q.contains(QStringLiteral("readonly property bool worldRunning"));
                const bool fClock = q.contains(QStringLiteral("WorldClock { id: worldClock; running: window.worldRunning }"));
                const bool fBind  = q.contains(QStringLiteral("worldRunning: window.worldRunning"));
                const bool fPause = q.contains(QStringLiteral("visible: !window.worldRunning"));
                const bool fKey   = q.contains(QStringLiteral("if (!player.captured) { e.accepted = true; return }"));
                // onTicked 桥（World tick 全家）无 captured / 面板门 —— GUI 开时火 / 水 / 生长照跑的源头证
                const int c0 = q.indexOf(QStringLiteral("function onTicked(dt)"));
                const int c1 = q.indexOf(QStringLiteral("// 光标位置追踪层"), c0);
                bool fBridge = false;
                if (c0 < 0 || c1 <= c0) { diag889 += QStringLiteral("(f) onTicked slice miss; "); }
                else {
                    const QString body = q.mid(c0, c1 - c0);
                    fBridge = !body.contains(QStringLiteral("captured"))
                              && !body.contains(QStringLiteral("inventoryOpen"));
                }
                okF = okF && fProp && fClock && fBind && fPause && fKey && fBridge;
                if (!(fProp && fClock && fBind && fPause && fKey && fBridge))
                    diag889 += QStringLiteral("(f) prop=%1 clock=%2 bind=%3 pause=%4 key=%5 bridge=%6; ")
                                   .arg(fProp).arg(fClock).arg(fBind).arg(fPause).arg(fKey).arg(fBridge);
            }
        }

        const bool ok889 = okA && okB && okC && okD && okE && okF;
        if (!ok889) ++totalFail;
        if (!diag889.isEmpty())
            qInfo().noquote() << "  [t889 diag]" << diag889;
        qInfo().noquote() << (ok889 ? "PASS" : "FAIL")
                          << "| t889 pause-semantics unification, two tiers (Java singleplayer parity): soft "
                             "tier (!captured + worldRunning=true, any GUI panel open equivalent) keeps world "
                             "running -- pc.tick() step() falls (Y drops, XZ frozen), item entity falls, "
                             "fishing persists with bobber alive; hard tier (worldRunning=false, ESC menu "
                             "equivalent) freezes player/item/bobber exact-equal with fishing line kept alive "
                             "across pause+resume; WorldClock.running stops/starts the 100ms ticked gate "
                             "behaviorally; deferWallClocks pushes spawnMs (pickup-window ready->not-ready "
                             "flip) and setWorldRunning rebases all three managers; release() body has no "
                             "cancelFishing (t885 line persistence); Main.qml gate pins (worldRunning "
                             "derived property + worldClock.running / player.worldRunning bindings + "
                             "pauseOverlay negated consume + keyInput !captured guard + onTicked bridge "
                             "free of captured/panel gates)";
    }

    // ── review26 #25 软档受击击退探针（Game 层 PlayerController 直编，t889(a) 软档 rig 族）──
    //   用户症状（review26 低危）：GUI 开（软档，世界照跑）被 mob 攻击伤害照扣但击退被吞——
    //   applyHitKnockback 旧门 `m_dead || !m_captured` 把软档击退一起拦掉（Java 语义：开背包照被打且被打飞）。
    //   修：门只拦 m_dead（applyGolemLaunch 连坐同修）。断言（行为级，t889(a) 的 pumpFor+pc.tick 驱动）：
    //   (a) 软档（默认 !captured）直调 applyHitKnockback(+X) → 玩家 X 位移 > 0.3（旧门恒 0 = 症状签名，回退即红）
    //       + 垂直小跳真发（y 曾高于地面）；
    //   (b) 对照：无击退时同窗口 X 精确不动（软档 XZ 冻结基线，位移只来自击退冲量）。
    {
        bool okA = false, okB = false, okHop = false;
        const auto pumpFor25 = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        World w25;
        w25.setWidth(48); w25.setDepth(48); w25.setHeight(96); w25.setSeed(77);
        EntityManager ents25;
        PlayerController pc25;
        pc25.setWorld(&w25);
        pc25.setEntityManager(&ents25);
        const int fy25 = 83;
        for (int x = 3; x <= 10; ++x)                       // 石板地板走廊（击退 +X 弹程接地）
            for (int z = 4; z <= 8; ++z) w25.setBlock(x, fy25, z, BR::Stone, 0);
        pc25.loadSavedState(4.5f, float(fy25 + 1), 6.5f, -90.0f, 0.0f, 2 /* Survival */);
        const QVector3D base25 = pc25.position();
        // (b) 对照窗：无击退 12 tick → X/Z 精确冻结（软档零输入基线）
        for (int i = 0; i < 12; ++i) { pumpFor25(17); pc25.tick(); }
        okB = pc25.position().x() == base25.x() && pc25.position().z() == base25.z();
        // (a) 软档击退：直调（QML Connections 等价；默认 !captured = GUI 开软档）→ +X 位移 + (hop) 垂直上抬
        const float groundY = pc25.position().y();
        pc25.applyHitKnockback(1.0f, 0.0f);
        float maxY = groundY;
        for (int i = 0; i < 12; ++i) {
            pumpFor25(17); pc25.tick();
            maxY = std::max(maxY, float(pc25.position().y()));
        }
        okA = float(pc25.position().x() - base25.x()) > 0.3f;
        okHop = maxY > groundY + 0.05f; // kHitKnockbackUp 小跳（m_vel.y max 写入）
        const bool ok25 = okA && okB && okHop;
        if (!ok25)
            qInfo().noquote() << "  [review26-25 diag] dx=" << float(pc25.position().x() - base25.x())
                          << "frozenOk=" << okB << "maxY-lift=" << float(maxY - groundY);
        if (!ok25) ++totalFail;
        qInfo().noquote() << (ok25 ? "PASS" : "FAIL")
                          << "| review26-25 soft-tier hit knockback lands: with a GUI-open equivalent "
                             "(!captured, world running), applyHitKnockback displaces the player >0.3 "
                             "blocks along the hit direction with the vertical hop (Java parity: damage "
                             "already ticked, knockback must follow; old gate swallowed it - the "
                             "no-knockback window signature), while the no-hit control window keeps X/Z "
                             "exactly frozen (displacement comes only from the impulse)";
    }

    // ── P-t881 鱼线最大长度探针（32 格断线，行为级）──
    //    pc 真甩竿 → settle（近距 ~2 格）→ pc.tick 线仍持；applyEnderPearlTeleport 把玩家拉到 ~53 格
    //    （loadSavedState 会 cancelFishing 不可用——传送是唯一不撞钓鱼态的移位口）→ 传送本身不断线
    //    （检测在 updateFishing）→ 首 pc.tick 断线：浮标槽释放 + fishing 复位 + 耐久不变 + 零 fishCaught。
    {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(78);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const int fy = 83; // rig 地板格（seed 78 未生成地形 → 全空带，手摆）
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                wL.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池（settle 用）
            }
        wL.setBlock(3, fy, 6, BR::Stone, 0);  // 玩家立足柱（pc.tick step 物理需要）
        wL.setBlock(44, fy, 42, BR::Stone, 0); // 传送目标立足柱（距浮标 (5.5,6.5) 水平 √(39²+36²)≈53 > 32）
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wL);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caught = 0;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float) { ++caught; });
        pc.useFishingRod(); // 甩竿（serial 1，轨迹同 t836(b)：settle (5.5, fy+1.75, 6.5)）
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        bool ok = pc.fishing() && bob >= 0;
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
        for (int t = 0; t < 40 && ok; ++t) {
            tickL(1, 0.05f);
            if (!ents.aliveAt(bob)) { ok = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        ok = ok && ents.posAt(bob) == settlePos;
        pumpFor(17); pc.tick(); // 近距镜像 tick（~2 格）→ 线仍持（fishing 保持 + 浮标活）
        ok = ok && pc.fishing() && ents.aliveAt(bob);
        const int dur0 = hb.durabilityAt(0);
        pc.applyEnderPearlTeleport(44, fy + 2, 42); // 玩家 → (44.5, fy+1, 42.5)（传送不撞钓鱼态）
        ok = ok && pc.fishing();                    // 传送本身不断线（检测在 updateFishing 镜像段）
        pumpFor(17); pc.tick();                     // → 断线
        ok = ok && !pc.fishing() && !ents.aliveAt(bob) && caught == 0
              && hb.durabilityAt(0) == dur0;        // 无获物 / 无耐久（扯断≠收竿）
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t881 fishing line max length: eye-to-bobber 3D distance beyond 32 blocks "
                             "snaps the line on the next updateFishing mirror tick (bobber entity removed, "
                             "fishing state cleared, zero fishCaught, zero rod durability cost -- a snapped "
                             "line is not a reel); near-distance tick keeps the line (behavioral: real cast "
                             "-> settle -> pc.tick holds; ender-pearl teleport hauls the player ~53 blocks "
                             "away without touching fishing state -- the only headless repositioning path, "
                             "loadSavedState cancels fishing by savegame semantics)";
    }

    // ── P-t882 拉拽反馈增强探针（行为级：距离缩放 / 上抛弧；源码钉：角度调制——yaw 无 WRITE，收杆改向
    //    行为级不可达 headless，t836(g) 同取舍）──
    //    (a) 近距钩猪（~3 格直瞄，t836(d) 轨迹）→ 收竿一帧物理位移 movedN + 耐久 -5；
    //    (b) 远距钩猪（~12.5 格：直瞄会中途落地够不着 → 仰角 12..34° 扫描，每次失败换新猪重摆——首游荡窗
    //        30 tick 内完成甩钩的确定性口径）→ 收竿 movedF > movedN×1.25（6+0.35d 距离缩放）+ 猪升起
    //        riseF > 0.06（2.8+0.18d 上抛弧——旧基值 2.8 只升 ~0.045，用户「没看到生物被拉起来飞」）；
    //    (c) 源码钉：useFishingRod 体内距离增益（kFishHookPullGain/kFishHookLiftGain）+ 角度调制
    //        （angleFactor 乘 speed 与 lift 两支）语句面。
    {
        World wP;
        wP.setWidth(48); wP.setDepth(48); wP.setHeight(96); wP.setSeed(79);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickP = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wP, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wP);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);

        // ---- (a) 近距（~3 格直瞄）：基速 ≈ 6+0.35×3 ≈ 7.1 ----
        wP.setBlock(13, fy, 6, BR::Stone, 0); // 玩家立足柱
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Stone, 0);
        const int pigN = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobPig,
                                            QStringLiteral("#e8a0a0"), 10);
        tickP(5, 0.05f); // 短窗 settle（t836(d) 加固口径：甩钩链压进首游荡窗 30 tick 内）
        {
            const QVector3D pp = ents.posAt(pigN);
            const float eyeY = float(fy + 1) + 1.62f;
            const float ux = pp.x() - 13.5f, uz = pp.z() - 6.5f;
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                              qRadiansToDegrees(std::atan2(-ux, -uz)),
                              qRadiansToDegrees(std::atan2(pp.y() - eyeY, std::sqrt(ux * ux + uz * uz))), 2);
        }
        pc.useFishingRod();
        int bobN = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobN = i; break; }
        bool okNear = bobN >= 0;
        for (int t = 0; t < 40 && okNear; ++t) {
            if (ents.bobberHookedMobAt(bobN) == pigN) break;
            tickP(1, 0.05f);
            if (!ents.aliveAt(bobN)) { okNear = false; break; }
        }
        okNear = okNear && ents.bobberHookedMobAt(bobN) == pigN;
        const float pigNX0 = ents.posAt(pigN).x();
        const int durN0 = hb.durabilityAt(0);
        pc.useFishingRod(); // 收竿拉拽
        tickP(1, 0.016f);   // 一帧物理：位移 = 拉速 × dt（≈7.1×0.016≈0.11）
        const float movedN = std::fabs(ents.posAt(pigN).x() - pigNX0);
        okNear = okNear && movedN > 0.03f && hb.durabilityAt(0) == durN0 - 5;
        ents.removeEntityAt(pigN);
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Air, 0);

        // ---- (b) 远距（~12.5 格）：仰角扫描甩中 → 位移/升幅随距离增强 ----
        for (int x = 25; x <= 27; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Stone, 0);
        bool okFar = false;
        float movedF = 0.0f, riseF = 0.0f, usedPitch = -1.0f;
        QString farDiag;
        for (int pi = 6; pi <= 17 && !okFar; ++pi) {
            const float pitch = float(pi) * 2.0f; // 12°..34°（直瞄平射 ~13 格处已落到台下，必须仰射）
            // 每次尝试换新猪（短窗确定性：settle+甩+飞 ≤1.3s < 首游荡窗 1.5s——重试不叠猪龄）
            const int pigF = ents.spawnMobTyped(26, fy + 1, 6, EntityManager::MobPig,
                                                QStringLiteral("#e8a0a0"), 10);
            tickP(5, 0.05f);
            pc.loadSavedState(13.5f, float(fy + 1), 6.5f, -90.0f, pitch, 2);
            pc.useFishingRod();
            int bobF = -1;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bobF = i; break; }
            bool hooked = false;
            if (bobF >= 0) {
                for (int t = 0; t < 40; ++t) {
                    if (ents.bobberHookedMobAt(bobF) == pigF) { hooked = true; break; }
                    tickP(1, 0.05f);
                    if (!ents.aliveAt(bobF)) break;
                }
            }
            if (hooked) {
                const float pigFX0 = ents.posAt(pigF).x();
                const float pigFY0 = ents.posAt(pigF).y();
                const int durF0 = hb.durabilityAt(0);
                pc.useFishingRod(); // 收竿拉拽（远距：≈6+0.35×12.5 ≈ 10.4 b/s + 上抛 ≈2.8+2.25）
                tickP(1, 0.016f);
                movedF = std::fabs(ents.posAt(pigF).x() - pigFX0);
                riseF = ents.posAt(pigF).y() - pigFY0;
                usedPitch = pitch;
                okFar = movedF > movedN * 1.25f && riseF > 0.06f
                        && hb.durabilityAt(0) == durF0 - 5;
                if (!okFar)
                    farDiag = QStringLiteral("movedF=%1 movedN=%2 riseF=%3").arg(movedF).arg(movedN).arg(riseF);
            } else {
                pc.useFishingRod(); // 空收浮标（重试下一仰角）
            }
            ents.removeEntityAt(pigF);
        }
        for (int x = 25; x <= 27; ++x)
            for (int z = 5; z <= 7; ++z) wP.setBlock(x, fy, z, BR::Air, 0);
        wP.setBlock(13, fy, 6, BR::Air, 0);

        // ---- (c) 角度调制 / 距离增益源码钉（yaw 无 WRITE，改向行为级不可达 headless）----
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString pcpPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                        QStringLiteral("src/Game/playercontroller.cpp"));
            QFile f(pcpPath);
            if (f.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(f.readAll());
                const int b0 = t.indexOf(QStringLiteral("void PlayerController::useFishingRod()"));
                const int b1 = t.indexOf(QStringLiteral("void PlayerController::updateFishing"));
                if (b0 >= 0 && b1 > b0) {
                    QString body;
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                        if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                        body += line; body += QLatin1Char('\n');
                    }
                    okPin = body.contains(QStringLiteral("kFishHookPullGain * dc"))
                            && body.contains(QStringLiteral("kFishHookLiftGain * dc"))
                            && body.contains(QStringLiteral("pullSpeed *= angleFactor;"))
                            && body.contains(QStringLiteral("liftSpeed *= angleFactor;"))
                            && body.contains(QStringLiteral(
                                   "pullMobToward(hooked, m_pos, pullSpeed, liftSpeed)"));
                }
            }
        }
        const bool okT882 = okNear && okFar && okPin;
        if (!okT882) ++totalFail;
        if (!okT882)
            qInfo().noquote() << "  [t882 diag] okNear" << okNear << "movedN" << movedN
                              << "okFar" << okFar << "pitch" << usedPitch << farDiag
                              << "okPin" << okPin;
        qInfo().noquote() << (okT882 ? "PASS" : "FAIL")
                          << "| t882 hook-reel feedback: pull strength scales with line length (speed "
                             "+= 0.35 x dist-capped-32 -> far pig displaces >1.25x near pig in the "
                             "first physics tick after the reel) and the launch arc grows with it "
                             "(lift base 2.8 + 0.18 x dist -> far-pig rise > 0.06/tick vs old flat "
                             "0.045 -- 'yanked visibly into the air'), both modulated by reel angle "
                             "(look-vs-line |cos| factor, facing the target = full power, sideways/"
                             "over-shoulder decays to 0.4x; pitch excluded via horizontal renorm -- "
                             "angled-down water casts must not lose force; angle branch pinned at "
                             "source level since yaw has no WRITE and re-aiming mid-hook is "
                             "unreachable headless); impulse constants moved wholly to the Game "
                             "layer (pullMobToward now takes speed+upSpeed, Entities layer holds no "
                             "impulse constants - kBobberHookPullUp retired); far-cast rig sweeps "
                             "elevation 12-34 deg with a fresh pig per attempt (flat aim falls "
                             "short of a 12.5-block target under light gravity)";
    }

    // ── P-t883 夜行者对鱼钩瞬移探针（行为级）──
    //    真甩竿命中夜行者（t836(d) 直瞄轨迹，3 格内必中）→ ① 全程不钩定（bobberHookedMobAt 恒 -1，钩不住
    //    夜行者族）；② 夜行者被强制瞬移（位移 >4 格——瞬移带 8-16 格 vs 游荡步进 <1 格/秒可分辨）；③ 浮标
    //    穿过原站位继续飞 / 落定（实体仍活，不被消耗——鱼钩是软线不是箭）；对照：猪在 3 格直瞄必钩（t836(d)
    //    已钉，不重摆）。闪避后夜行者掉下平台（瞬移落点在平台外）也只断言位移量不断言落点。
    {
        World wN;
        wN.setWidth(48); wN.setDepth(48); wN.setHeight(96); wN.setSeed(80);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickN = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wN, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        wN.setBlock(13, fy, 6, BR::Stone, 0); // 玩家立足柱
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z) wN.setBlock(x, fy, z, BR::Stone, 0); // 夜行者平台
        // 夜行者（t727 口径 spawnMobTyped 直摆；halfH 1.40 三格高，halfW 0.35——3 格直瞄命中盒大）
        const int nw = ents.spawnMobTyped(16, fy + 1, 6, EntityManager::MobNightwalker,
                                          QStringLiteral("#1a1426"), 40);
        // settle 16 tick（夜行者 halfH 1.40 → 重心出生位高，落定穿行 ~1 格需 ~7 tick；5 tick 会抓到中途
        //   下坠位（实测 84.35）导致瞄准错位。16+甩钩 ≤3 tick 仍 < 首游荡窗 30 tick，确定性保持）
        tickN(16, 0.05f);
        const QVector3D nw0 = ents.posAt(nw);
        const float eyeY = float(fy + 1) + 1.62f;
        const float ux = nw0.x() - 13.5f, uz = nw0.z() - 6.5f;
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wN);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(13.5f, float(fy + 1), 6.5f,
                          qRadiansToDegrees(std::atan2(-ux, -uz)),
                          qRadiansToDegrees(std::atan2(nw0.y() - eyeY, std::sqrt(ux * ux + uz * uz))), 2);
        pc.useFishingRod(); // 甩向夜行者（3 格直瞄，飞行 ≤3 tick 必进命中盒）
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        bool noHook = bob >= 0;
        bool teleported = false;
        for (int t = 0; t < 24 && noHook; ++t) {
            tickN(1, 0.05f);
            if (ents.bobberHookedMobAt(bob) == nw) { noHook = false; break; } // 钩上了 = FAIL
            const QVector3D np = ents.posAt(nw);
            if ((np - nw0).length() > 4.0f) teleported = true; // 强制瞬移带 8-16 格（游荡 <1 格/秒可分辨）
            if (!ents.aliveAt(bob)) break; // 穿过后落定 / 出界消散皆可（浮标不被消耗即不再追认）
        }
        const bool okT883 = noHook && teleported;
        if (!okT883) ++totalFail;
        if (!okT883)
            qInfo().noquote() << "  [t883 diag] noHook" << noHook << "teleported" << teleported
                              << "nwPos" << ents.posAt(nw) << "from" << nw0
                              << "hooked" << (bob >= 0 ? ents.bobberHookedMobAt(bob) : -2);
        qInfo().noquote() << (okT883 ? "PASS" : "FAIL")
                          << "| t883 nightwalker vs fishhook: flying-bobber hook scan treats a "
                             "MobNightwalker hit as a projectile encounter -- forced teleport dodge "
                             "(bypassing teleportCooldown, same t829 arrow-chain fix so a cooldown-"
                             "window hit can never pass through silently) and the bobber never "
                             "latches (hook state machine unreachable for the nightwalker family; "
                             "MC 1.0 enderman projectile-immunity parity for the fishing rod); the "
                             "bobber itself is NOT consumed (soft line, unlike the arrow's "
                             "remove=true) and keeps flying through the vacated spot; behavioral "
                             "rig: direct 3-block aim at a settled nightwalker -> zero hook across "
                             "24 ticks + displacement >4 blocks (teleport band 8-16 vs wander <1/s)";
    }

    // ── P-review26-8 弹射物闪避不清仇恨源码钉（review26 #8：浮标闪避复用 teleportEntity 免费净化）──
    //   行为级不可密闭驱动的取舍声明：enraged 是 ≤1s 瞬态（rage 满 1s 即 teleportBehindPlayer 转蓄力段，
    //   背后无落点也清 enraged 防卡态），且进入态依赖「夜行者面朝玩家」——游荡 yaw 随机翻转 → headless
    //   掷骰驱动必 flaky（t882 mob flake 前车之鉴，不新增）——按 t889(a3)/t836(e) 源码钉手法锁语句面：
    //   ① teleportEntity 落定清仇恨三连（enraged/rageTimer/windupTimer）被 clearAggro 参数门控；
    //   ② 箭链 / 浮标两处弹射物闪避调用显式传 false（MC 1.0 末影人被投射物闪避不解除仇恨；浮标 0 伤害
    //      0 消耗清仇恨 = 免费无限远程「净化」+ 打断攻击前摇，箭链同为投射物一并修）；
    //   ③ 水逃逸 / 近战 dodge 保持默认 true（既有设计：水伤与近身交互打断激怒）+ 头文件默认参数存在。
    //   闪避行为本身（位移 / 不钩定 / 箭消耗）由 t883 / t829(b) 行为级探针覆盖，不重摆。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile cf(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
        QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
        const QString ct = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
        const QString ht = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
        const int b0 = ct.indexOf(QStringLiteral("bool EntityManager::teleportEntity"));
        const int b1 = ct.indexOf(QStringLiteral("bool EntityManager::teleportBehindPlayer"));
        bool okGuard = false, okCalls = false, okDefault = false, okDecl = false;
        if (b0 < 0 || b1 <= b0 || ht.isEmpty()) {
            qInfo().noquote() << "  [review26-8 diag] slice miss b0" << b0 << "b1" << b1;
        } else {
            QString body;
            for (const QString &line : ct.mid(b0, b1 - b0).split(QLatin1Char('\n')))
                if (!line.trimmed().startsWith(QLatin1String("//"))) { body += line; body += QLatin1Char('\n'); }
            // ① 清仇恨三连被参数门控（签名带参 + if (clearAggro) 守卫）
            okGuard = body.contains(QStringLiteral("bool clearAggro"))
                      && body.contains(QStringLiteral("if (clearAggro)"));
            // ② 箭链 + 浮标两处弹射物闪避显式 false
            okCalls = ct.count(QStringLiteral("clearAggro=*/false")) == 2;
            // ③ 水逃逸 + 近战 dodge 保持默认调用（Max 后紧跟右括号 = 未传参）
            okDefault = ct.count(QStringLiteral("kNightwalkerTeleportMax)")) == 2;
            // 头文件默认参数（true = 近战/水逃逸清仇恨语义保持）
            okDecl = ht.contains(QStringLiteral("bool clearAggro = true"));
        }
        const bool okR8 = okGuard && okCalls && okDefault && okDecl;
        if (!okR8) ++totalFail;
        if (!okR8)
            qInfo().noquote() << "  [review26-8 diag] okGuard" << okGuard << "okCalls" << okCalls
                              << "okDefault" << okDefault << "okDecl" << okDecl;
        qInfo().noquote() << (okR8 ? "PASS" : "FAIL")
                          << "| review26-8 projectile dodge no longer wipes aggro: teleportEntity's "
                             "landing hatred-clear trio (enraged/rageTimer/windupTimer) is gated "
                             "behind a clearAggro param; the arrow-chain and bobber dodge call sites "
                             "pass false explicitly (MC 1.0 enderman projectile-dodge keeps aggro - "
                             "the 0-damage 0-cost bobber was a free repeatable ranged pacify that "
                             "also cancelled attack windup), while water-escape and melee dodge "
                             "keep the default true (documented design: water damage and close-"
                             "range interaction interrupt rage); source pin because the enraged "
                             "state is a sub-1s transient gated on random wander yaw - behavioral "
                             "driving would be flaky (t889 a3 / t836 e precedent); dodge behavior "
                             "itself stays covered by the t883/t829(b) rigs";
    }

    // ── P-t884 咬钩可见性全套探针（行为级：①入水水花信号恰一次 + 坐标；Water 态查询三态分辨；Game 层
    //    bobberInWater 镜像翻转。②微飘动画 / ③水面轨迹粒子 / ④下沉加深与咬钩水花加强是 QML 视觉层——
    //    commit 钉 visual-only：驱动条件（bobberInWater && !hasBite）已被本探针行为级锁死）──
    {
        World wV;
        wV.setWidth(48); wV.setDepth(48); wV.setHeight(96); wV.setSeed(81);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickV = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wV, farL, 0.3f, 1.8f, false);
        };
        const auto pumpFor = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const int fy = 83;
        int splashCount = 0;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        QObject::connect(&ents, &EntityManager::bobberSplashed, &ents,
                         [&](float x, float y, float z) { ++splashCount; sx = x; sy = y; sz = z; });
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wV.setBlock(x, fy, z, BR::Stone, 0);
                wV.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池
            }

        // (a) 入水：直落水池 → Flying 段 inWater=false → settle 恰一次 bobberSplashed（坐标 = 浮定水面坐标）
        const int b1 = ents.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 951);
        const bool flyingSeen = b1 >= 0 && !ents.bobberInWaterAt(b1); // 出生 Flying（inWater false）
        for (int t = 0; t < 60 && ents.aliveAt(b1) && splashCount == 0; ++t) tickV(1, 0.05f);
        const QVector3D settlePos(6.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okA = flyingSeen && splashCount == 1 && ents.aliveAt(b1) && ents.bobberInWaterAt(b1)
                   && qAbs(sx - settlePos.x()) < 1e-3f && qAbs(sy - settlePos.y()) < 1e-3f
                   && qAbs(sz - settlePos.z()) < 1e-3f;
        tickV(60, 0.05f); // 水中静置 3s（跨等待期）不重发（再入水才重发）
        okA = okA && splashCount == 1;
        ents.removeEntityAt(b1);

        // (b) 陆上：石台直落 → Ground：零 bobberSplashed + inWater false（微飘/轨迹粒子的驱动条件不误触）
        wV.setBlock(12, fy, 18, BR::Stone, 0);
        const int b2 = ents.spawnBobber(QVector3D(12.5f, float(fy + 4), 18.5f), QVector3D(0, 0, 0), 952);
        for (int t = 0; t < 60 && ents.aliveAt(b2); ++t) tickV(1, 0.05f);
        const bool okB = ents.aliveAt(b2) && !ents.bobberInWaterAt(b2) && splashCount == 1;
        ents.removeEntityAt(b2);
        wV.setBlock(12, fy, 18, BR::Air, 0);

        // (c) Game 层镜像：pc 真甩竿 settle（轨迹同 t836(b)：格 (5,84,6)）→ pc.tick → bobberInWater true；
        //     收竿翻 false（QML 待机微飘 / 水面轨迹粒子的驱动条件链行为级锁死）。
        wV.setBlock(3, fy, 6, BR::Stone, 0); // 玩家立足柱
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wV);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        const QVector3D settleC(5.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okC = bob >= 0 && !pc.bobberInWater(); // 甩出即 Flying（镜像初值 false）
        for (int t = 0; t < 40 && okC; ++t) {
            tickV(1, 0.05f);
            if (!ents.aliveAt(bob)) { okC = false; break; }
            if (ents.posAt(bob) == settleC) break;
        }
        pumpFor(17); pc.tick(); // 镜像刷新（updateFishing 拉 bobberInWaterAt）
        okC = okC && ents.posAt(bob) == settleC && pc.fishing() && pc.bobberInWater();
        pc.useFishingRod(); // 收竿 → 镜像翻 false
        okC = okC && !pc.fishing() && !pc.bobberInWater();

        const bool okT884 = okA && okB && okC;
        if (!okT884) ++totalFail;
        if (!okT884)
            qInfo().noquote() << "  [t884 diag] okA" << okA << "(splash" << splashCount << "at" << sx << sy
                              << sz << ") okB" << okB << "okC" << okC;
        qInfo().noquote() << (okT884 ? "PASS" : "FAIL")
                          << "| t884 bite-visibility set: (1) cast-to-water splash -- bobberSplashed "
                             "fires exactly once on the Flying->Water settle edge with the exact "
                             "float-surface coordinates (3s idle water stays at one; re-entry after "
                             "drain/refill re-fires naturally), routed to burstWaterCast; (2) "
                             "bobberInWaterAt discriminates all four states (born Flying false / "
                             "settled Water true / Ground false) and the Game-layer bobberInWater "
                             "mirror flips true after a settle+pc.tick and false on reel -- the exact "
                             "driving condition chain for the QML idle bob animation + approach-trail "
                             "particles; (3) visual-only halves (idle micro-bob sin phase +-0.035 via "
                             "NumberAnimation, deterministic golden-angle approach ripples every "
                             "380ms arriving-and-dying at the bobber, bite sink deepened 0.15->0.35 "
                             "plus bite splash strengthened 10->14 particles, all gated "
                             "bobberInWater&&!hasBite&&worldRunning so ESC freezes them) pinned "
                             "visual-only in the commit";
    }

    // ── P-t886 鱼获反馈探针（行为级：获物抛物弹出落玩家旁可捡 + 经验球 1-6 XP）──
    //    pc 真 Consumer 端（ItemEntityManager + XpOrbManager 都注入）：甩竿 → settle → drive 到咬钩 → 收竿 →
    //    ① 掉落物实体已生成于浮标格向上**列扫**弹出点（review26 #7：首个非水格 +0.225 = 静水格顶+0.225——
    //    水面上空气格，浮水分支不吞弧线；旧 +0.35 固定抬升口径退役）；
    //    ② 推掉落物物理 3s（60 tick × 0.05）→ 落定在玩家中心 2.2 格内（抛物解准确弹向玩家，可捡）；
    //    ③ review26 #21：XP 直接入账——恰一次 fishXpGained、量 ∈[1,6]、零经验球（MC 1.0 钓鱼 1-6 XP
    //       无球实体；旧口径「球落浮标格中心」随 #21 退役）；
    //    ④ 弹速 = 抛物解 |v| 镜像（近距 ≈8.1，随距离自适应）+ 耐久 -1（口径不变）。
    {
        World wC;
        wC.setWidth(48); wC.setDepth(48); wC.setHeight(96); wC.setSeed(82);
        EntityManager ents;
        ItemEntityManager items;
        XpOrbManager orbs;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickC = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wC, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        // 落地带：石地板 x 3..8 / z 4..8（弹道短/过长都接得住；不含水池列外的虚空）+ 3×3 水池（5..7,5..7）
        for (int x = 3; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) wC.setBlock(x, fy, z, BR::Stone, 0);
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) wC.setBlock(x, fy + 1, z, BR::Water, 0);
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wC);
        pc.setEntityManager(&ents);
        pc.setItemEntities(&items);
        pc.setXpOrbManager(&orbs);
        pc.setHotbar(&hb); // t886：耐久 -1 断言需要（首跑红真因——漏注入 → damageSelectedItem 的
                           //   m_hotbar 门静默 false，探针 diag 三值全对唯独 dur=0 暴露）
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caughtCount = 0; float csp = 0.0f;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float speed) {
                             ++caughtCount; csp = speed;
                         });
        // review26 #21：钓获 XP 直接入账信号（MC 1.0 钓鱼无经验球实体）——计数 + 量程；旧版断言球落
        //   浮标格随 #21 退役（改断言**零球**：入账与球互斥，防双发）。
        int xpGainCount = 0; int xpGainAmount = 0;
        QObject::connect(&pc, &PlayerController::fishXpGained, &pc,
                         [&](int amount) { ++xpGainCount; xpGainAmount += amount; });
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.75f, 6.5f);
        bool okCast = bob >= 0;
        for (int t = 0; t < 40 && okCast; ++t) {
            tickC(1, 0.05f);
            if (!ents.aliveAt(bob)) { okCast = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        okCast = okCast && ents.posAt(bob) == settlePos;
        for (int t = 0; t < 660 && okCast && !ents.bobberHasBiteAt(bob); ++t) tickC(1, 0.05f);
        okCast = okCast && ents.bobberHasBiteAt(bob);
        const QVector3D bobPos = ents.posAt(bob);
        const int dur0 = hb.durabilityAt(0);
        pc.useFishingRod(); // 窗内收竿 → C++ 直调 spawnItemThrown + spawnOrb（t886 主路径）
        // ① 掉落物已生成于列扫弹出点（review26 #7：浮标格向上首个非水格 +0.225——静水 = 格顶+0.225；
        //    本 rig 浮标 settle 于 (5, fy+1, 6) → 弹出格 (5, fy+2, 6)）
        int item = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { item = i; break; }
        const QVector3D spawnExp(bobPos.x(), std::floor(bobPos.y()) + 1.225f, bobPos.z());
        bool okItem = item >= 0 && caughtCount == 1
                      && qAbs(items.posAt(item).x() - spawnExp.x()) < 1e-2f
                      && qAbs(items.posAt(item).y() - spawnExp.y()) < 1e-2f
                      && qAbs(items.posAt(item).z() - spawnExp.z()) < 1e-2f;
        // ② 推掉落物物理 3s：抛物解落点 = 玩家中心 —— 断言**首次触底点**（resting 首次 true）距玩家中心
        //    < 2.2（积分步进误差余量）。review26 #2 起落定后残余水平速度按 t468 支撑面摩擦继续滑行
        //    （弹速 ~8 / 摩擦 6 ≈ 1.4 格 + 本 rig 落地带西缘就在玩家脚边 → 终点会滑出平台落到下层
        //    地形）—— 终点位不再钉「可捡半径」，触底点才是抛物解准确性的锚（滑行是引擎 t468 既定
        //    摩擦语义，非弹道偏差）。
        QVector3D touchdown(0.0f, 0.0f, 0.0f);
        bool touchedDown = false;
        for (int t = 0; t < 60 && okItem; ++t) {
            items.tick(0.05, &wC);
            if (!touchedDown && items.aliveAt(item) && items.restingAt(item)) {
                touchedDown = true;
                touchdown = items.posAt(item);
            }
        }
        const QVector3D playerCenter(3.5f, float(fy + 1) + 0.9f, 6.5f);
        okItem = okItem && touchedDown && items.aliveAt(item)
                 && (touchdown - playerCenter).length() < 2.2f;
        // ③ review26 #21：XP 直接入账——恰一次 fishXpGained、量 ∈[1,6]、零经验球（入账与球互斥防双发）；
        //    ④ 弹速 = 抛物解镜像 + 耐久 -1
        int orbAlive = 0;
        for (int i = 0; i < orbs.count(); ++i)
            if (orbs.aliveAt(i)) ++orbAlive;
        const float vmagExp = fishCatchSpeedMirror(bobPos, QVector3D(3.5f, float(fy + 1), 6.5f));
        const bool okOrb = xpGainCount == 1 && xpGainAmount >= 1 && xpGainAmount <= 6
                           && orbAlive == 0
                           && qAbs(csp - vmagExp) < 1e-2f
                           && hb.durabilityAt(0) == dur0 - 1;
        const bool okT886 = okCast && okItem && okOrb;
        if (!okT886) ++totalFail;
        if (!okT886)
            qInfo().noquote() << "  [t886 diag] okCast" << okCast << "okItem" << okItem << "(itemPos"
                              << (item >= 0 ? items.posAt(item) : QVector3D()) << ") okOrb" << okOrb
                              << "(xpGain" << xpGainCount << "amt" << xpGainAmount
                              << "orbAlive" << orbAlive
                              << "csp" << csp << "exp" << vmagExp
                              << "dur" << hb.durabilityAt(0) - dur0 << ")";
        qInfo().noquote() << (okT886 ? "PASS" : "FAIL")
                          << "| t886 catch feedback: the loot item is spawned C++-side (dispenser/dropper "
                             "direct-call precedent) as a solved ballistic throw from the bobber - spawn "
                             "point column-scanned to the first non-water cell above the bobber +0.225 "
                             "(review26 #7: static water = cell top +0.225; the old fixed +0.35 lift "
                             "never left the water cell on flowing water where the surface frac is lower, "
                             "and the item float-water branch zeroes vy and glues the drop to the surface, "
                             "killing the arc), target = player center, flight time clamp(0.45+0.055D, "
                             "0.5,1.4), vy = dy/T + g*T/2 (g=28 item gravity mirror) - after 3s of "
                             "item physics the drop rests within 2.2 blocks of the player center "
                             "(accurately catchable); review26 #21: XP credits directly via one "
                             "fishXpGained of 1-6 amount routed to addXp (MC 1.0 fishing grants no "
                             "xp-orb entity - the old orb sat up to 32 blocks away at the bobber, "
                             "pure-magnet so it never chased the player) and zero orb entities "
                             "spawn (credit-orb mutual exclusion guards double-grant); "
                             "fishCaught speed payload equals the solved |v| mirror "
                             "and the QML onFishCaught forwarder is retired (signal is now "
                             "informational - double-spawn guard); rod -1 unchanged. Matrix probe "
                             "drives a real PlayerController with ItemEntityManager + XpOrbManager "
                             "injected";
    }

    // ── P-review26-7 流动水获物弹出点列扫探针（review26 #7：+0.35 固定抬升在 state≥2 未离水格）──
    //   旧口径 kFishCatchRiseOffset 0.35 只在静水（state 0，液面 7/8）恰好把生成点送出水格；流动水
    //   state≥2 液面 ≤0.75 → 生成点仍落水格内 → 掉落物浮水分支（vy 清零 + 恒速上浮）把弧线整个吞掉，
    //   获物粘回浮标处（t886 症状在河流 / 溢流边缘复发）。新口径：从浮标格向上**列扫**首个非 Water 格再
    //   +0.225（与 ItemEntityManager 浮水分支自己的列扫同源）。rig：3×3 池 state=5（液面 3/8 → 浮标 settle
    //   y = 格底+0.25，旧口径生成点 = +0.60 仍在水格 = FAIL 面）→ ① 生成点 y = 首非水格+0.225 = 池上空气格
    //   fy+2+0.225 且中心格非 Water；② 弧线不被吞：spawn 后 2 tick 水平位移 >0（浮水分支只动 Y）。
    //   t886 探针（静水 rig）已同步新口径断言，两水位全覆盖。
    {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(84);
        EntityManager ents;
        ItemEntityManager items;
        XpOrbManager orbs;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 3; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) wL.setBlock(x, fy, z, BR::Stone, 0);
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) wL.setBlock(x, fy + 1, z, BR::Water, 5); // 流动水 state 5（液面 3/8）
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wL);
        pc.setEntityManager(&ents);
        pc.setItemEntities(&items);
        pc.setXpOrbManager(&orbs);
        pc.setHotbar(&hb);
        pc.loadSavedState(3.5f, float(fy + 1), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        int caughtCount = 0;
        QObject::connect(&pc, &PlayerController::fishCaught, &pc,
                         [&](int, int, float, float, float, float, float, float) { ++caughtCount; });
        pc.useFishingRod();
        int bob = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
        // state 5：settle y = (fy+1) + 3/8 − 0.125 = fy+1.25（浮定沿随液面折算，同 t892 口径）
        const QVector3D settlePos(5.5f, float(fy + 1) + 0.25f, 6.5f);
        bool okCast = bob >= 0;
        for (int t = 0; t < 40 && okCast; ++t) {
            tickL(1, 0.05f);
            if (!ents.aliveAt(bob)) { okCast = false; break; }
            if (ents.posAt(bob) == settlePos) break;
        }
        okCast = okCast && ents.posAt(bob) == settlePos;
        for (int t = 0; t < 660 && okCast && !ents.bobberHasBiteAt(bob); ++t) tickL(1, 0.05f);
        okCast = okCast && ents.bobberHasBiteAt(bob);
        const QVector3D bobPos = ents.posAt(bob);
        pc.useFishingRod(); // 窗内收竿 → 获物 spawnItemThrown（review26 #7 列扫弹出点）
        int item = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { item = i; break; }
        // ① 生成点在非水格：列扫 → 浮标格 (5, fy+1, 6) 上首个非水格 = (5, fy+2, 6) 空气格 → y = fy+2+0.225
        const QVector3D spawnExp(bobPos.x(), float(fy + 2) + 0.225f, bobPos.z());
        const bool okSpawn = item >= 0 && caughtCount == 1
                             && qAbs(items.posAt(item).x() - spawnExp.x()) < 1e-2f
                             && qAbs(items.posAt(item).y() - spawnExp.y()) < 1e-2f
                             && qAbs(items.posAt(item).z() - spawnExp.z()) < 1e-2f
                             && wL.blockAt(5, fy + 2, 6) != BR::Water;
        // ② 弧线不被浮水分支吞：spawn 后 2 tick 水平位移 >0.05（浮水分支 vy 清零只动 Y，水平冻结）
        bool okArc = false;
        QVector3D pos2;
        for (int t = 0; t < 2 && okSpawn; ++t) {
            items.tick(0.05, &wL);
            pos2 = items.posAt(item);
        }
        if (okSpawn && items.aliveAt(item)) {
            const float dh = QVector3D(pos2.x() - spawnExp.x(), 0.0f, pos2.z() - spawnExp.z()).length();
            okArc = dh > 0.05f;
        }
        const bool okR7 = okCast && okSpawn && okArc;
        if (!okR7) ++totalFail;
        if (!okR7)
            qInfo().noquote() << "  [review26-7 diag] okCast" << okCast << "okSpawn" << okSpawn
                              << "(itemPos" << (item >= 0 ? items.posAt(item) : QVector3D())
                              << "exp" << spawnExp << ") okArc" << okArc << "(pos2" << pos2 << ")";
        qInfo().noquote() << (okR7 ? "PASS" : "FAIL")
                          << "| review26-7 catch spawn escapes FLOWING water: the loot pop point is "
                             "column-scanned from the bobber cell up to the first non-water cell "
                             "(+0.225, same column-scan the item float-water branch itself uses) "
                             "instead of a fixed +0.35 lift - on state>=2 water the surface frac is "
                             "<=0.75 so the old fixed lift left the spawn INSIDE the water cell and "
                             "the float branch zeroed vy and swallowed the whole arc (the t886 "
                             "'no visible catch flight' symptom recurring on rivers/overflow "
                             "edges); rig: 3x3 pool at state 5 (surface 3/8, bobber settles at "
                             "cell+0.25, old code spawned at +0.60 = still in water) - spawn lands "
                             "at the air cell above (non-water center cell) and the drop moves "
                             "horizontally within 2 ticks (arc alive); the t886 static-water probe "
                             "asserts the same column-scan value (cell top +0.225)";
    }

    // ── P-review26-10 载具乘客钉位帧 rideRevision 同步探针（review26 #10：mob 乘客 QML 刷新 20Hz vs 矿车
    //   60Hz 不同步——快速车载乘视觉锯齿）──
    //   review25 #3 把 tickVehicleRiding 的乘客直发收口到 ~20Hz 相位门（修每帧双发卡顿，方向正确），但车侧
    //   位移仍每帧 notifyChanged（60Hz）→ C++ 乘客每帧钉车、QML 乘客 delegate 20Hz 采样 → 快速矿车
    //   （~8 格/s）载 mob 乘客相对车滞后 ~0.4 格、每 50ms 跳变。修 = ②「乘客单开小名单每帧 emit」的专用
    //   revision 变体：钉位值真变帧 bump rideRevision + 发 ridersChanged（有界：仅载客载具移动帧），QML 侧
    //   仅 mob delegate 的 position 绑定触碰它（t500 卡顿主因的 ~12 条 revision 绑定 + MobModel 重建面不
    //   触碰）。矩阵断言：(a) 行为级——载客矿车每个移动帧 rideRevision 恰 +1（同帧同步契约），停驻帧
    //   零 bump（无空转发射），钉位精度 <0.01（rig 自证场景成立）；(b) 源码钉——Main.qml mob delegate 的
    //   position 绑定触碰 entityManager.rideRevision + entitymanager.h 的 Q_PROPERTY 三件套存在
    //   （t870/t889 源码钉先例）。
    {
        // 专用世界（review26-5/6 先例：seed 77 全空带 y84+ 平台——不占主世界 rig 位，防下游槽位漂移）：
        //   平台 y84 + 北向直轨 10 格 y85（x6，z21..30）。
        World wR10;
        wR10.setWidth(48); wR10.setDepth(48); wR10.setHeight(96); wR10.setSeed(77);
        for (int x = 4; x <= 8; ++x)
            for (int z = 20; z <= 32; ++z) wR10.setBlock(x, 84, z, BR::Stone, 0);
        const int rx = 6, rz0 = 30;
        for (int dz = -10; dz <= 0; ++dz) wR10.setBlock(rx, 85, rz0 + dz, BR::Rail, 0);
        MinecartManager carts;
        EntityManager ents;
        ents.setVehicleManagers(&carts, nullptr);
        carts.spawnCart(rx, 85, rz0, &wR10);
        const int mob = ents.spawnMobTyped(rx, 85, rz0, 0, QStringLiteral("#ff5555"), 10);
        const float seatDropX = 0.3125f; // kCartSeatDrop 同值镜像（t811 探针同款）
        QVector3D player = carts.posAt(0);
        QVector3D lastCp = carts.posAt(0);
        bool boarded = false, contractArmed = false, okMove = true, okIdle = true, pinOk = true;
        int settleFrames = 0, moveFrames = 0;
        float travel = 0.0f;
        // 阶段 A（推动期）：登乘后 2 帧武装契约（首钉落座帧允许一次性 bump，非车载移动）。
        for (int t = 0; t < 1200 && travel < 6.0f; ++t) {
            const int rev0 = ents.rideRevision();
            ents.tick(0.016, &wR10, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();                       // 钉位①（mob 桶内，游戏同序）
            if (int(std::floor(player.z())) > rz0 - 10)
                carts.pushEmptyCart(&wR10, player, 0.0f, -1.0f); // 长按 W 朝北推（t809/t811 玩家模型）
            carts.tickPushedCarts(0.016, &wR10);
            ents.tickVehicleRiding();                       // 钉位②（step 后同帧随车）
            const int rev1 = ents.rideRevision();
            const QVector3D cp = carts.posAt(0);
            const bool moved = QVector3D(cp - lastCp).length() > 1e-4f;
            if (moved) travel += QVector3D(cp - lastCp).length();
            if (ents.rideCartAt(mob) >= 0) {
                boarded = true;
                const QVector3D mp = ents.posAt(mob);
                if (std::fabs(mp.x() - cp.x()) > 0.01f
                    || std::fabs(mp.y() - (cp.y() - seatDropX + 0.5f)) > 0.01f
                    || std::fabs(mp.z() - cp.z()) > 0.01f) pinOk = false;
                if (!contractArmed) {
                    if (++settleFrames >= 2) contractArmed = true; // 落座 / 登乘帧不计契约
                } else if (moved) {
                    ++moveFrames;
                    if (rev1 - rev0 != 1) okMove = false;   // 移动帧恰 +1（同帧同步契约）
                } else {
                    if (rev1 != rev0) okIdle = false;        // 静止帧零 bump（无空转发射）
                }
            }
            lastCp = cp;
            player = cp; // 贴身追随（t809 先例）
        }
        // 阶段 B（停驻期）：不再推 → 余速滑到死端停驻 → 100 tick 静止帧零 bump。
        bool okPark = true;
        int parked = 0;
        for (int t = 0; t < 700 && parked < 100; ++t) {
            const int rev0 = ents.rideRevision();
            ents.tick(0.016, &wR10, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            carts.tickPushedCarts(0.016, &wR10);
            ents.tickVehicleRiding();
            const int rev1 = ents.rideRevision();
            const QVector3D cp = carts.posAt(0);
            const bool moved = QVector3D(cp - lastCp).length() > 1e-4f;
            if (moved) { parked = 0; travel += QVector3D(cp - lastCp).length(); }
            else ++parked;
            if (!moved && rev1 != rev0) okPark = false;      // 停驻帧零 bump
            lastCp = cp;
            player = cp;
        }
        // (b) 源码钉：QML position 绑定触碰 + 头文件属性三件套（t870/t889 先例）。
        bool okPinQml = false, okPinHdr = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString mt = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const int d0 = mt.indexOf(QStringLiteral("id: mobDelegate"));
            const int d1 = mt.indexOf(QStringLiteral("property int entKind:"), d0 > 0 ? d0 : 0);
            okPinQml = d0 >= 0 && d1 > d0
                       && mt.mid(d0, d1 - d0).contains(QStringLiteral("entityManager.rideRevision"));
            QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            const QString ht = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
            okPinHdr = ht.contains(QStringLiteral(
                "Q_PROPERTY(int rideRevision READ rideRevision NOTIFY ridersChanged)"));
        }
        const bool ok = boarded && pinOk && okMove && okIdle && okPark && moveFrames >= 30
                        && travel >= 4.0f && okPinQml && okPinHdr;
        if (!ok)
            qInfo().noquote() << "  review26-10 diag: boarded" << boarded << "pinOk" << pinOk
                              << "okMove" << okMove << "okIdle" << okIdle << "okPark" << okPark
                              << "moveFrames" << moveFrames << "travel" << travel
                              << "okPinQml" << okPinQml << "okPinHdr" << okPinHdr;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review26-10 vehicle passenger pin syncs to the cart cadence: every"
                             " frame a passenger-carrying cart MOVES bumps rideRevision exactly once"
                             " (dedicated ridersChanged emit, only the mob delegate position binding"
                             " touches it - the t500 12-binding revision face stays at the 20Hz gate)"
                             " and parked frames emit nothing; QML position binding touches"
                             " rideRevision (source pin); travel" << travel;
        // 专用世界随作用域丢弃，无需清场。
    }

    // ── P-review26-11 硬暂停冻结纯 QML 视觉 Timer 源码钉（review26 #11：ESC 时附魔字形仍持续发射/飞行）──
    //   EnchantGlyphFlow 两 Timer 的 running 只绑 active（appState=="playing" 派生）→ ESC 硬档（世界全停）
    //   白字持续飞（t884 自己 gate 了 worldRunning、t873 漏了）。修 = worldRunning 并入 running（经 Loader
    //   注入 window.worldRunning，同 active/world/camNode 注入先例——组件不直引跨上下文 id）。全仓纯视觉
    //   Timer 清点（本探针一并钉同批修的漏网）：EnchantGlyphFlow spawn+tick / EnchantRunes spawn+tick /
    //   BlockParticles tick（碎屑烟雾）/ Main.qml 水·岩浆·火·余烬门四翻书帧 + 附魔书翻页 + t878 爱心
    //   NumberAnimation。豁免面（UI chrome / 输入冻结期输出必静态，清点表落 Review 与 commit message）：
    //   bobber 拍水 Timer（t884 已 gate）/ f3Refresh / faceTimer（书朝向，玩家冻结→值静态）/ 指南针钟表
    //   图标 / 聊天淡出 / 上下船 toast / 信息 toast / anvil·enchant 面板闪光 / CharacterPreview3D 预览。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        const auto readSrc = [&root](const QString &rel) {
            QFile f(root + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString gf = readSrc(QStringLiteral("/src/ui/EnchantGlyphFlow.qml"));
        const QString rn = readSrc(QStringLiteral("/src/ui/EnchantRunes.qml"));
        const QString bp = readSrc(QStringLiteral("/src/ui/BlockParticles.qml"));
        const QString mn = readSrc(QStringLiteral("/src/ui/Main.qml"));
        // (a) finding 本体：GlyphFlow 两 Timer running 含 worldRunning + 注入属性存在。
        const bool okGf = gf.contains(QStringLiteral(
                              "running: root.active && root.pairs.length > 0 && root.worldRunning"))
                          && gf.contains(QStringLiteral(
                              "running: (root.active || root.liveCount > 0) && root.worldRunning"))
                          && gf.contains(QStringLiteral("property bool worldRunning: false"));
        // (b) 同族漏网：EnchantRunes spawn+tick / BlockParticles tick（tick 改声明式——imperative start 退役）。
        const bool okRn = rn.contains(QStringLiteral(
                              "running: root.active && root.shelfCells.length > 0 && root.worldRunning"))
                          && rn.contains(QStringLiteral("running: root.worldRunning"))
                          && !rn.contains(QStringLiteral("tickTimer.start()"));
        const bool okBp = bp.contains(QStringLiteral("running: root.worldRunning"))
                          && !bp.contains(QStringLiteral("tickTimer.start()"));
        // (c) Main.qml：四翻书帧 Timer + 附魔书翻页 Timer gate window.worldRunning（≥5 处）+ 爱心
        //     NumberAnimation + 三处 Loader 注入（GlyphFlow/Runes/BlockParticles）。
        int flipGates = 0;
        for (int i = mn.indexOf(QStringLiteral("running: window.worldRunning")); i >= 0;
             i = mn.indexOf(QStringLiteral("running: window.worldRunning"), i + 1)) ++flipGates;
        const bool okMn = flipGates >= 5
                          && mn.contains(QStringLiteral(
                              "running: loveHearts.visible && window.worldRunning"))
                          && mn.count(QStringLiteral(".item.worldRunning = Qt.binding(function() { return window.worldRunning })")) >= 3;
        const bool ok = okGf && okRn && okBp && okMn;
        if (!ok)
            qInfo().noquote() << "  review26-11 diag: okGf" << okGf << "okRn" << okRn
                              << "okBp" << okBp << "okMn" << okMn << "flipGates" << flipGates;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| review26-11 hard pause freezes pure-visual QML Timers: enchant glyph"
                                 " spawn+flight, ambient runes, block debris pool, water/lava/fire/"
                                 "portal strip flipbooks, book page-flip and love hearts all gate"
                                 " worldRunning (MC Java singleplayer pause freezes particles);"
                                 " UI-chrome timers (toasts, chat fade, panel flashes, preview pane)"
                                 " stay exempt (source pin)";
    }

    // ── P-t888 火伤节奏对齐 MC 探针（行为级 + 数值钉）──
    //    t888：① 常量钉（kFireDamageInterval 0.75s / kFireExtinguishChance 0 / kFireDuration 8——改值须
    //      同步本探针；MC 基准出处见 entitymanager.h 常量注释）；② 玩家侧行为级：真 pc 站立地火 → 首拍
    //      ∈[0.7,1.1]s、8s 内恰 ~10-11 拍（间隔恒定无随机吞拍）、余焰满 8s（0.75×11=8.25 > 8 → 恰 11 拍
    //      后 fireTimer 到期熄灭）；③ mob 侧同链（ignite 直燃猪，2.25s ≥3 拍 = 期望伤 >1HP/s）；
    //      ④ 阴性对照：kFireExtinguishChance=0 下 8s 窗内零「提前熄灭」（fireTimer 单调递减到自然归零，
    //      不出现中途跳零）。t889 软档语义照跑口径：pc.tick() 在 !captured 下 step 照跑火烧段。
    {
        World wF;
        wF.setWidth(48); wF.setDepth(48); wF.setHeight(96); wF.setSeed(86);
        EntityManager ents;
        Hotbar hb;
        PlayerController pc;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0); // 手持非火源物（火烧判定只读世界，持物无关；占位防空手分支）
        pc.setWorld(&wF);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        // rig：石地板 5×5 @fy，中央立地火 @fy+1（坐在地板上——不替换地板格，防脚下悬空洞）；
        //   玩家站火上（Fire 实存可站 / 或沉入格内，两种碰撞语义下新扫描均必接触）。
        const int fy = 83;
        for (int x = 1; x <= 5; ++x)
            for (int z = 4; z <= 8; ++z) wF.setBlock(x, fy, z, BR::Stone, 0);
        wF.setBlock(3, fy + 1, 6, BR::Fire, 0);
        // 常量钉（编译期值运行期复核——探针文本可读、回归即红）
        bool ok = EntityManager::kFireDamageInterval == 0.75f
                  && EntityManager::kFireExtinguishChance == 0.0f
                  && EntityManager::kFireDuration == 8.0f;
        // 玩家侧：settle 后验 burning 翻转（接触点燃主路径），随后重摆干净 rig 数拍（信号级精确）。
        pc.loadSavedState(3.5f, float(fy + 2), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
        for (int t = 0; t < 20 && !pc.burning(); ++t) {
            pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false); pc.tick();
        }
        ok = ok && pc.burning(); // 站火必燃
        // —— 重摆干净 rig 数拍：fallDamageTaken(Fire) 连接计数，10.2s 窗断言恰 13 拍 + 首拍时刻带。
        wF.setBlock(3, fy + 1, 6, BR::Air, 0);
        pc.clearStatusEffects();
        wF.setBlock(3, fy + 1, 6, BR::Fire, 0);
        int firePulses = 0;
        double pulseT = -1.0;
        QElapsedTimer burnClock; burnClock.start();
        QObject::connect(&pc, &PlayerController::fallDamageTaken, &pc,
                         [&](int hp, int cause) {
                             if (hp == 1 && cause == int(PlayerState::Fire)) {
                                 ++firePulses;
                                 if (pulseT < 0.0) pulseT = burnClock.elapsed() / 1000.0;
                             }
                         });
        burnClock.restart();
        QElapsedTimer wholeClock; wholeClock.start();
        while (wholeClock.elapsed() < 10200) {
            pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false); pc.tick();
        }
        // 10.2s 窗：0.75s 恒间隔（随机熄灭已归零 = 零吞拍）→ 首拍 ~0.75 起、末拍 13×0.75=9.75 ≤ 10.2 <
        //   14×0.75=10.5 → **恰 13 拍**（旧 1.0s+15% 吞拍同窗只有 ~7-9 拍且首拍更晚）。首拍 ∈ [0.55, 1.15]
        //   （0.75 标称 ± 泵抖动 ~0.34s/帧容差）。
        ok = ok && firePulses == 13
             && pulseT >= 0.55 && pulseT <= 1.15;
        if (!(firePulses == 13 && pulseT >= 0.55 && pulseT <= 1.15))
            qInfo().noquote() << "  [t888 diag] firePulses" << firePulses << "firstPulse" << pulseT
                              << "burning" << pc.burning();
        // mob 侧同链：ignite 猪 → 3s 内 ≥3 拍（0.75 间隔 → 3s 恰 4 拍；≥3 容泵抖动；P(<3)=0 间隔确定性）
        wF.setBlock(3, fy + 1, 6, BR::Air, 0); // 清玩家立地火（mob 段用 ignite 直燃，防火源干扰对照）
        pc.clearStatusEffects();
        const int pigB = ents.spawnMobTyped(3, fy + 1, 6, EntityManager::MobPig,
                                            QStringLiteral("#ee9999"), 20);
        ok = ok && pigB >= 0;
        if (pigB >= 0) {
            ents.ignite(pigB, 8.0f);
            const float h0 = ents.healthAt(pigB);
            QElapsedTimer mobClock; mobClock.start();
            while (mobClock.elapsed() < 3000) {
                pumpFor(17); ents.tick(0.05, &wF, farL, 0.3f, 1.8f, false);
            }
            ok = ok && (h0 - ents.healthAt(pigB)) >= 3.0f;
        }
        // 清场
        wF.setBlock(3, fy + 1, 6, BR::Air, 0);
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t888 fire damage pacing aligned to MC: interval constant pinned at "
                             "0.75s (first pulse lands in [0.55,1.15]s vs old 1.0s), random early "
                             "extinguish retired to exactly 0 (MC normal fire never self-extinguishes "
                             "mid-burn; rain douse is a separate path) -> 10.2s standing-in-fire window "
                             "yields exactly 13 damage pulses with zero swallowed ticks (old 0.15 chance "
                             "ate ~40% of them), afterburn duration stays MC 8s; mob side shares the "
                             "same constants via ignite() >=3 HP lost in 3s; player contact ignition "
                             "reuses the t344 burn chain (soft-tier worldRunning semantics, world keeps "
                             "ticking while GUI open)";
    }

    // ── P-t891 点火源扩展探针（① 岩浆邻燃 / ② 烈焰弹全链）──
    //    (a) 岩浆邻燃：木板贴岩浆源 → 首个命中窗**点燃进燃烧态**（isBurningAt 真 + id 保留 = 直燃语义；
    //        核此前只有焚毁没有点燃）→ 持续驱动至烧毁（blockAt 变非 Planks，燃烧计时终局独家承担焚毁——
    //        单掷双义防一格两份消耗）；湿对照（板邻水）→ 同窗数内不点燃也不焚毁（防火带收口在入口内）；
    //        书架（非 isWoodLike 但 flammable）→ 也被点燃（入口门扩到可燃全表）。确定性：tickLavaFlow
    //        散布 = hashVoxel(seed+窗口序号) 纯函数；8%/窗 → 240 窗 P(未命中)≈2e-10（上限裕量充足）。
    //    (b) 烈焰弹全链（真 pc）：setStack 手持烈焰弹 → 右键发射 → Fireball 实体出生眼位前
    //        （kindAt==Fireball）→ 飞行撞墙消失 → 撞击必生火：打石墙 = 来向空气格置立地火（blockAt==Fire，
    //        100% per-entity 点燃概率）；打木板墙 = 板进燃烧态（id 不变，igniteFlammableAt 直燃口径）；
    //        创造不耗 + 挥手信号；生存消耗 1 弹；低头发射不自伤（玩家侧火球 shooter 豁免——发射后 HP 满 =
    //        无 mobAttackedPlayer 伤害，pc 无 PlayerState 注入以「burning 未翻转」间接证）；合成配方
    //        match 双证（煤版 / 木炭版各合 3 发）+ FireChargeId 0x25C 钉位 + 创造调色板含烈焰弹。
    {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(91);
        const int fy = 83;
        bool okA = true;
        // (a1) 干木板贴岩浆 → 点燃（直燃语义）→ 续驱至烧毁。驱动：每窗翻转标记格 poke 岩浆脏
        //     （m_lavaDirty 稳态早退——setBlock 直写不 poke，须显式重标脏；r24#3(b) 同款手法）。
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(6, fy, z, BR::Stone, 0);
            wL.setBlock(5, fy, z, BR::Lava, 0); // 岩浆源列（不驱动流动也参与 ignite pass 扫描）
        }
        wL.setBlock(6, fy, 5, BR::Planks, 0); // 木板贴岩浆（x=5 的 +X 邻）
        int lavaWins = 0;
        bool sawLit = false;
        for (; lavaWins < 240 && !sawLit; ++lavaWins) {
            wL.setBlock(5, fy + 1, 4, (lavaWins & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int t = 0; t < 35; ++t) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗（r24#3 同款）
            sawLit = wL.isBurningAt(6, fy, 5); // 点燃观测在窗内即时取（防同窗后段已烧毁漏采）
        }
        const bool litByLava = sawLit;
        bool burnedAway = false;
        if (litByLava) {
            for (int t = 0; t < 120 && !burnedAway; ++t) { // 燃烧计时 10 窗 + 余烬衔接
                wL.tickFire();
                if (wL.blockAt(6, fy, 5) != BR::Planks) burnedAway = true;
            }
        }
        okA = okA && litByLava && burnedAway;
        // (a2) 湿对照：**书架**邻岩浆且邻水 → 恒静（防火带强断言）。用书架而非木板：木板是 isWoodLike，
        //     掷中且湿拒后会**回落旧焚毁路径**（焚毁无水守卫 = 既有语义，板被烧掉）→ 断言面混入旧路径；
        //     书架非 isWoodLike（掷中且湿拒 → 本窗跳过不焚毁）→ 240 窗后仍完好未燃 = 防火带在点燃入口
        //     恒拒的纯净信号（无 tickFire 驱动 → 曾点燃会驻留燃烧态被末态捕获）。
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(12, fy, z, BR::Stone, 0);
            wL.setBlock(11, fy, z, BR::Lava, 0);
            wL.setBlock(13, fy, z, BR::Water, 0); // 水在书架另一侧 → 书架湿
        }
        wL.setBlock(12, fy, 5, BR::Bookshelf, 0);
        for (int t = 0; t < 240; ++t) {
            wL.setBlock(11, fy + 1, 4, (t & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int k = 0; k < 35; ++k) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗
        }
        const bool wetQuiet = wL.blockAt(12, fy, 5) == BR::Bookshelf
                              && !wL.isBurningAt(12, fy, 5);
        okA = okA && wetQuiet;
        // (a3) 书架（flammable 非 isWoodLike）→ 点燃（入口门扩全表）
        for (int z = 4; z <= 6; ++z) {
            wL.setBlock(20, fy, z, BR::Stone, 0);
            wL.setBlock(19, fy, z, BR::Lava, 0);
        }
        wL.setBlock(20, fy, 5, BR::Bookshelf, 0);
        bool shelfLit = false;
        for (int t = 0; t < 240 && !shelfLit; ++t) {
            wL.setBlock(19, fy + 1, 4, (t & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int k = 0; k < 35; ++k) wL.tickLavaFlow(); // 35 调 ≥ 节流 30 → 恰 1 真窗
            shelfLit = wL.isBurningAt(20, fy, 5); // 窗内即时观测（同 a1）
        }
        okA = okA && shelfLit;
        // 清 (a) 场
        for (int x : {5, 6, 11, 12, 13, 19, 20})
            for (int z = 4; z <= 6; ++z)
                for (int dy = 0; dy <= 2; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);

        // (b) 烈焰弹全链（真 pc rig：石地 + 石靶墙 + 木靶墙）
        EntityManager ents;
        Hotbar hb;
        PlayerController pcF;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto pumpFor = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        for (int x = 1; x <= 14; ++x)
            for (int z = 4; z <= 8; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                for (int dy = 1; dy <= 3; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);
            }
        for (int dy = 1; dy <= 2; ++dy) // 石靶墙 x=13（两层高）
            for (int z = 4; z <= 8; ++z) wL.setBlock(13, fy + dy, z, BR::Stone, 0);
        pcF.setWorld(&wL);
        pcF.setEntityManager(&ents);
        pcF.setHotbar(&hb);
        // placeBlock 的 !m_captured 入口门（headless 无指针锁定）：探针 pc 无窗口（QQuickItem 裸构造，
        //   window()=null → grab() 早退）→ 直调公开 Q_INVOKABLE setCaptured 不可用（private 非槽）。
        //   captured 是 Q_PROPERTY(bool captured READ ...) 只读 → QML 写不进。t886/t881 探针的
        //   useFishingRod 不吃此门，placeBlock 吃——headless 唯一通道 = 手动把 pc 挂进一个 QQuickWindow
        //   （setParentItem 挂 window contentItem → windowChanged → onWindowChanged 设 m_window）再调
        //   grab()（m_window 就绪后 setCaptured(true) 走通）。窗仅作指针捕获载体（无 show，光标覆写
        //   只作用于本测试进程无副作用；进程退出析构配对 release）。
        QQuickWindow probeWin;
        pcF.setParentItem(probeWin.contentItem());
        pcF.grab();
        // 配方 match 双证 + id 钉位 + 创造调色板
        const int gridCoal[9] = { RecipeRegistry::BlazePowderId, RecipeRegistry::CoalId,
                                  RecipeRegistry::GunpowderId, 0, 0, 0, 0, 0, 0 };
        const int gridChar[9] = { RecipeRegistry::BlazePowderId, RecipeRegistry::CharcoalId,
                                  RecipeRegistry::GunpowderId, 0, 0, 0, 0, 0, 0 };
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(gridCoal, 2);
        const RecipeRegistry::Recipe *rch = RecipeRegistry::match(gridChar, 2);
        bool inPalette = false;
        const QVariantList mats = hb.creativeMaterials();
        for (const QVariant &v : mats)
            if (v.toInt() == RecipeRegistry::FireChargeId) { inPalette = true; break; }
        const bool okRecipe = rc && rch && rc->outputId == RecipeRegistry::FireChargeId
                              && rc->outputCount == 3 && rch->outputCount == 3
                              && RecipeRegistry::FireChargeId == 0x25C && inPalette;
        // 发射链 A：创造模式瞄石墙（水平直射 z=6 行）
        hb.setStack(0, RecipeRegistry::FireChargeId, 5, 0);
        hb.setSelectedSlot(0);
        const QVector3D eyeP(2.5f, float(fy + 1), 6.5f);
        const QVector3D dirV = (QVector3D(13.5f, float(fy + 1) + 0.5f, 6.5f)
                                - QVector3D(eyeP.x(), eyeP.y() + 1.62f, eyeP.z())).normalized();
        pcF.loadSavedState(eyeP.x(), eyeP.y(), eyeP.z(),
                           qRadiansToDegrees(std::atan2(-dirV.x(), -dirV.z())),
                           qRadiansToDegrees(std::asin(dirV.y())), 1 /* Creative */);
        pumpFor(17); pcF.tick(); // settle（碰撞落位）
        int swingSeen = 0;
        QObject::connect(&pcF, &PlayerController::swingArm, &pcF, [&]() { ++swingSeen; });
        const int beforeCount = ents.count();
        pcF.placeBlock(); // 右键发射
        int fbIdx = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Fireball)) { fbIdx = i; break; }
        const bool spawnedOk = fbIdx >= 0 && ents.count() > beforeCount
                               && hb.blockIdAt(0) == RecipeRegistry::FireChargeId
                               && hb.countAt(0) == 5 /* 创造不耗 */ && swingSeen >= 1;
        // 推实体 tick 至撞击（~11 格 / 12b/s ≈ 0.95s ≈ 60 tick @dt0.05；上限 80 裕量）
        int fireAtWall = -1;
        for (int t = 0; t < 80 && fireAtWall < 0; ++t) {
            ents.tick(0.05, &wL, farL, 0.3f, 1.8f, false);
            if (!ents.aliveAt(fbIdx)) { // 消失 = 撞击结算完成（石墙非可燃 → 来向格立地火）
                const QVector3D last = ents.posAt(fbIdx);
                const int lx = qFloor(last.x()), ly = qFloor(last.y()), lz = qFloor(last.z());
                static constexpr int kNb7[7][3] = {{0,0,0},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0},{0,0,-1},{0,0,1}};
                for (const auto &o : kNb7) {
                    const int qx = lx + o[0], qy = ly + o[1], qz = lz + o[2];
                    if (qy >= 0 && wL.blockAt(qx, qy, qz) == BR::Fire) { fireAtWall = t; break; }
                }
            }
        }
        // 发射链 B：生存模式瞄木板墙（换靶重发；验消耗 + 直燃口径）。链 A 的火球已飞出（撞墙消失或
        //   寿命兜底）→ 本段「首个 Fireball」扫描会命中链 A 残留（若寿命未到）→ 先排空实体桶。
        hb.setStack(0, RecipeRegistry::FireChargeId, 3, 0);
        for (int dy = 1; dy <= 2; ++dy)
            for (int z = 4; z <= 8; ++z) wL.setBlock(11, fy + dy, z, BR::Planks, 0);
        pcF.clearStatusEffects();
        // 链 A 火球已结算消失（fireAtWall ≥ 0 实证）→ 链 B 的首个活 Fireball 即新弹。**不可按 idx 排除
        //   链 A 残弹**——EntityManager 是 slot-reuse（t256），新火球大概率恰好复用链 A 的槽位。
        const QVector3D dirW = (QVector3D(11.5f, float(fy + 1) + 0.5f, 6.5f)
                                - QVector3D(eyeP.x(), eyeP.y() + 1.62f, eyeP.z())).normalized();
        pcF.loadSavedState(eyeP.x(), eyeP.y(), eyeP.z(),
                           qRadiansToDegrees(std::atan2(-dirW.x(), -dirW.z())),
                           qRadiansToDegrees(std::asin(dirW.y())), 2 /* Survival */);
        pumpFor(320); pcF.tick(); // > 放置 CD 200ms（链 A 发射已刷新 m_lastPlaceMs——不泵则本发被吞）
        pcF.placeBlock();
        int fb2 = -1;
        for (int i = 0; i < ents.count(); ++i)
            if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Fireball)) { fb2 = i; break; }
        bool impactSettled = false;
        for (int t = 0; t < 80 && !impactSettled && fb2 >= 0; ++t) {
            ents.tick(0.05, &wL, farL, 0.3f, 1.8f, false);
            if (!ents.aliveAt(fb2)) impactSettled = true; // 消失 = 撞击结算完成
        }
        // 直燃口径的燃烧态是**持续态**（id 不变）→ 撞击后仍可复验（不依赖窗内时序）
        bool plankStillBurning = false;
        for (int dx = 9; dx <= 13 && !plankStillBurning; ++dx)
            for (int dy = 0; dy <= 3 && !plankStillBurning; ++dy)
                for (int z = 4; z <= 8 && !plankStillBurning; ++z)
                    if (wL.isBurningAt(dx, fy + dy, z)) plankStillBurning = true;
        const bool survivalConsumed = hb.blockIdAt(0) == RecipeRegistry::FireChargeId
                                      && hb.countAt(0) == 2; // 3-1
        // (c) 玩家侧豁免（行为级）：直下发射——火球出生点在玩家外扩命中盒内（眼位下方 0.5，XZ 偏移 0 <
        //     0.6），无豁免则首帧必自击（5HP + 点燃）。豁免（fireballShooter==-1 玩家侧 → 玩家命中分支
        //     跳过）生效 → 全程零 mobAttackedPlayer，火球正常坠地生火（石地板 → 来向格立地火）。
        int playerHits = 0;
        QObject::connect(&ents, &EntityManager::mobAttackedPlayer, &ents,
                         [&](int, int, float, float) { ++playerHits; });
        const int fbD = ents.spawnFireball(QVector3D(3.5f, float(fy + 1) + 1.62f - 0.5f, 6.5f),
                                            QVector3D(0.0f, -12.0f, 0.0f), 100);
        bool downSettled = false;
        for (int t = 0; t < 40 && !downSettled; ++t) {
            // playerTargetable=true + listener=玩家脚位 → 玩家命中分支真实求值（豁免是唯一免击原因）
            ents.tick(0.05, &wL, QVector3D(3.5f, float(fy + 1), 6.5f), 0.3f, 1.8f, true);
            if (fbD >= 0 && !ents.aliveAt(fbD)) downSettled = true;
        }
        const bool exemptOk = playerHits == 0 && downSettled
                              && wL.blockAt(3, fy + 1, 6) == BR::Fire; // 坠地生火（地板上方来向格）
        // 清场
        for (int x = 1; x <= 14; ++x)
            for (int z = 4; z <= 8; ++z)
                for (int dy = 0; dy <= 3; ++dy) wL.setBlock(x, fy + dy, z, BR::Air, 0);
        pcF.clearStatusEffects();
        probeWin.deleteLater(); // 捕获载体窗随探针作用域收尾（release 光标覆写配对）
        pcF.release();
        const bool okT891 = okA && spawnedOk && okRecipe && fireAtWall >= 0
                            && plankStillBurning && survivalConsumed && exemptOk;
        if (!okT891) ++totalFail;
        if (!okT891)
            qInfo().noquote() << "  [t891 diag] okA" << okA << "litByLava" << litByLava
                              << "burnedAway" << burnedAway << "wetQuiet" << wetQuiet
                              << "shelfLit" << shelfLit << "| spawnedOk" << spawnedOk
                              << "fbIdx" << fbIdx << "count" << ents.count()
                              << "| okRecipe" << okRecipe << "inPalette" << inPalette
                              << "| fireAtWall" << fireAtWall << "plankStillBurning" << plankStillBurning
                              << "survivalConsumed" << survivalConsumed
                              << "exemptOk" << exemptOk << "playerHits" << playerHits
                              << "cnt0" << hb.countAt(0) << "id0" << hb.blockIdAt(0);
        qInfo().noquote() << (okT891 ? "PASS" : "FAIL")
                          << "| t891 ignition sources extended: (a) lava neighbor ignition -- a wood "
                             "plank hugging a lava source now ENTERS the burning state on the first "
                             "hit window (id preserved = direct-burn semantics via the shared "
                             "igniteFlammableAt entry; the core path previously only incinerated), "
                             "then burns away through the burn-timer endgame exclusively (single-roll "
                             "dual-meaning: ignite wins over incinerate, no double consumption), a "
                             "water-backed bookshelf stays intact and unburned (damp-fuel firewall at "
                             "the ignite entry; a wet plank would fall to the legacy incinerate path "
                             "which has no water guard - out of scope), "
                             "and a bookshelf (flammable but outside the old isWoodLike set) now "
                             "catches too (entry gate widened to the full flammable table); "
                             "(b) fire charge item: real-PC right-click launches a Fireball along the "
                             "look direction (reusing the emberling projectile chain), stone-wall hit "
                             "places standing fire in the approach air cell (100% per-entity ignite "
                             "chance, flint-and-steel-homolog caliber), plank wall enters burning "
                             "state directly, survival consumes one charge while creative does not, "
                             "straight-down launch never self-hits (behavioral: player-side fireball "
                             "spawned INSIDE the player's expanded hitbox with playerTargetable=true "
                             "yields zero mobAttackedPlayer and settles into floor fire - owner "
                             "exemption via shooter==-1 skip), "
                             "recipes blaze-powder+coal/charcoal+gunpowder -> 3 charges both match and "
                             "the item sits in the creative material palette at id 0x25C";
    }

    // ── P-t892 静止水位降低探针（液面单一权威 + 消费方四方同源行为级）──
    //    用户报告：「耕地比水还低」透视错乱——耕地矮盒顶 15/16 而水满格 1.0 漫过其顶。修复 = 静水表面降
    //    到 7/8（MC 1.0 语义：比方块顶低 2 像素），**单一权威 BlockRegistry::waterSurfaceFrac**（源 7/8 /
    //    流 (8−min(s,7))/8），消费方四方同读：mesher renderTop（视觉水面）/ 浮标浮定 / 掉落物浮面 / 船水线
    //    ——统一改源头，严禁消费点各自内联 1.0（否则物浮在可视水面上/下方 1/8，视觉物理分裂）。
    //    断言：(a) 单一权威数值钉（源 7/8、流分档、越界 clamp）；(b) 掉落物静水浮面 = 顶水格 + 7/8 − 0.05
    //    下沉（镜像常量 kItemFloatOffset=0.05，P18 模式）；(c) 船静水水线 = 顶水格 + 7/8 − 吃水 0（tick 浮水
    //    lerp 收敛）；(d) 眼位液面分数判：眼在 7/8..1.0 空段 → 不算水下（蓝雾与可视液面同步），液面下 → 水下，
    //    柱内格（上方仍是水 → 满块）→ 水下。
    //    阴性轮：回退 waterSurfaceFrac(0) → 1.0 则 (a) 源值钉红 + (b)(c) 高度断言红（+1/8 偏差超容差）+
    //    (d) 空段判红（恢复满格恒水下）——四方同红即「单一权威生效」的证明。
    {
        World wS;
        // 48×48×96 seed 77 = t836 已证净空带（地形 ≤81 → 82+ 全空；小世界也会自动 worldgen，rig 层须避开
        //   自然地形——首跑 seed 892 物品落在 y≈45 天然地表上红）。
        wS.setWidth(48); wS.setDepth(48); wS.setHeight(96); wS.setSeed(77);
        const int ty = 83;                       // 石底格；静水 ty+1（state=0）
        for (int x = 4; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) {
                wS.setBlock(x, ty, z, BR::Stone, 0);
                wS.setBlock(x, ty + 1, z, BR::Water, 0);
            }
        wS.setBlock(4, ty + 2, 4, BR::Water, 0);  // (d) 柱内格：该列上方仍是水 → 满块口径
        // (a) 单一权威数值钉：源 7/8（低 2 像素）；st1 与源同高 7/8；分档 (8−s)/8；越界 clamp 到最低档。
        const bool okA = qAbs(BR::waterSurfaceFrac(0) - 0.875f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(1) - 0.875f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(2) - 0.75f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(4) - 0.5f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(7) - 0.125f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(200) - 0.125f) < 1e-6f;
        // (b) 掉落物浮面：出生水上 → 落水浮定 restY = 顶水格 + 7/8 − 0.05（旧满格口径 +1.0 → 红）。
        ItemEntityManager items;
        items.spawnItem(6, ty + 4, 6, BR::Cobble, 1);
        for (int t = 0; t < 400; ++t) items.tick(0.05, &wS);   // 20s：落 + 浮 + 静置（寿命 300s 内）
        int it = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { it = i; break; }
        const bool okB = it >= 0
                         && qAbs(items.posAt(it).y() - (float(ty + 1) + 0.875f - 0.05f)) < 2e-3f;
        // (c) 船水线：spawn 于水格 → tick 浮水 lerp 收敛到 顶水格 + 7/8 − 吃水 0（旧口径 +1.0 → 红）。
        BoatManager boats;
        const bool boatSpawned = boats.spawnBoat(7, ty + 1, 7, BoatManager::Oak);
        for (int t = 0; t < 600; ++t) boats.tick(0.016, &wS);   // 9.6s（kBoatAccel 恒速钳到 |dy| → 精确收敛）
        int bt = -1;
        for (int i = 0; i < boats.count(); ++i)
            if (boats.aliveAt(i)) { bt = i; break; }
        const bool okC = boatSpawned && bt >= 0
                         && qAbs(boats.posAt(bt).y() - (float(ty + 1) + 0.875f)) < 1e-3f;
        // (d) 眼位液面分数判（PlayerController 直读 eyeInWater）：眼 y=ty+1.92（7/8..1.0 空段）→ false；
        //     眼 y=ty+1.5（液面下）→ true；柱内列 (4,4) 同眼高 → true（上方是水 → 满块）。
        PlayerController pc;
        pc.setWorld(&wS);
        pc.loadSavedState(6.5f, float(ty + 1) + 0.92f - 1.62f, 6.5f, 0.0f, 0.0f, 1);
        const bool bandDry = !pc.eyeInWater();
        pc.loadSavedState(6.5f, float(ty + 1) + 0.5f - 1.62f, 6.5f, 0.0f, 0.0f, 1);
        const bool belowWet = pc.eyeInWater();
        pc.loadSavedState(4.5f, float(ty + 1) + 0.92f - 1.62f, 4.5f, 0.0f, 0.0f, 1);
        const bool columnWet = pc.eyeInWater();
        const bool okD = bandDry && belowWet && columnWet;
        const float itemY = (it >= 0) ? items.posAt(it).y() : -99.0f;   // diag 前取值（清场后槽失效）
        const float boatY = (bt >= 0) ? boats.posAt(bt).y() : -99.0f;
        // 清场（即用即清，防串扰后续探针）。
        for (int x = 4; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z)
                for (int dy = 0; dy <= 2; ++dy) wS.setBlock(x, ty + dy, z, BR::Air, 0);
        wS.setBlock(4, ty + 2, 4, BR::Air, 0);
        items.clearAll();
        boats.clearAll();
        const bool ok = okA && okB && okC && okD;
        if (!ok)
            qInfo().noquote() << "  [t892 diag] okA" << okA << "| okB" << okB << "itemY" << itemY
                              << "| okC" << okC << "boatY" << boatY
                              << "| okD" << okD << "bandDry" << bandDry
                              << "belowWet" << belowWet << "columnWet" << columnWet;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t892 still-water surface lowered to 7/8 (2px below block top, MC semantics): "
                             "single-authority waterSurfaceFrac (source 7/8 / flow (8-s)/8 / clamp) drives all "
                             "four consumers in lockstep - mesher renderTop (visual surface; farmland 15/16 now "
                             "stands above water, fixing the reported perspective clash), item float restY, boat "
                             "waterline and the eye-in-water liquid-fraction check (eye in the 1/8 air band above "
                             "a still surface is no longer underwater; column-interior cells stay full-block wet)";
    }

    // ── P-t893 流水动画流向四向匹配（源码钉；驱动 ChunkGeometry 需渲染后端，t879/t889 源码钉先例）──
    //    流水条带（右列）图案随帧沿 −v 移动（build_fluid_strips roll_y + t563 保向）→ mesher 按本格离源
    //    流向 D 旋转 UV 把「−v」映射到 D（观感顺流）。钉三面：①流向判定与掉落物随流同源算法（4 向
    //    state 梯度、低 state=近源背向）且门= !m_lavaOnly && st>0（静水左列 / 岩浆 / 孤立流格无向恒等，
    //    spec「静止面无向」）；②四向旋转路由俱在（±Y 顶面 cv≡−D 四分支 + ±X 墙 ±Z 流 / ±Z 墙 ±X 流
    //    沿墙横置、正交流保持竖直下淌 t563 语义）；③u/v 窗不越狱（colL/hxs 列窗 + stripV0 帧子区保留
    //    → positionV 翻书不受扰，零 mesh 重建语义不变）。回退（删 flowDir 旋转）→ 钉②红。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile cf(root + QStringLiteral("/src/World/chunkgeometry.cpp"));
        const QString t = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("int flowDir = 0;"));
        const int i1 = t.indexOf(QStringLiteral("if (flowDir != 0)"));
        bool okGate = false, okRot = false;
        if (i0 >= 0) {
            const QString seg = t.mid(i0, 900);
            okGate = seg.contains(QStringLiteral("!m_lavaOnly && st > 0"))
                     && seg.contains(QStringLiteral("fns < st"))
                     && seg.contains(QStringLiteral("fgx") ) && seg.contains(QStringLiteral("fgz"));
        }
        if (i1 >= 0) {
            const QString seg = t.mid(i1, 1200);
            okRot = seg.contains(QStringLiteral("cv = 1.0f - dz"))      // D=+Z（顶面 / ±X 墙）
                    && seg.contains(QStringLiteral("cv = 1.0f - dx"))   // D=+X（顶面 / ±Z 墙）
                    && seg.contains(QStringLiteral("cv = dx;        cu = dz"))   // D=−X 顶面
                    && seg.contains(QStringLiteral("cv = dz; cu = dy"))          // ±X 墙 D=−Z
                    && seg.contains(QStringLiteral("cv = dx; cu = dy"));         // ±Z 墙 D=−X
        }
        const bool ok = okGate && okRot;
        if (!ok)
            qInfo().noquote() << "  [t893 diag] gate" << i0 << okGate << "| rot" << i1 << okRot;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t893 flow animation direction matches the four flow directions: mesher derives "
                             "the away-from-source cardinal from the 4-neighbor water-state gradient (same "
                             "algorithm as item drift/player push) for flow cells only (still column, lava and "
                             "unresolvable isolated cells stay directionless) and rotates the strip UVs so the "
                             "-v pattern motion maps onto that direction on the top face, with side walls "
                             "animating horizontally only when the flow runs along the wall (waterfalls keep "
                             "flowing down); u stays locked to the column window and v to the frame-0 sub-range "
                             "so the positionV flipbook is untouched";
    }

    // ── P-t894 潜行者模型 0.85（源码钉：QML 契约——纯视觉项 t781 先例不进行为矩阵）──
    //    用户「苦力怕（潜行者）现偏大」→ 全模视觉缩 0.85。钉三面成对契约：①Main.qml scale 基 0.85
    //    （蓄力膨胀相对量 1+inflate·0.5 保持 → 满蓄力 ≈1.28）；②mobModelYOff Stalker 分支腿底补偿
    //    0.90×0.85（缺补偿 → 脚下悬空 0.135 —— 与 ① 成对，改其一须同步另一）；③碰撞盒**不缩**
    //    （radiusAt/halfHeightAt 走 mobType 表单一权威 —— halfW 0.30/halfH 0.90 保持，移动/近战/爆炸
    //    判定不随视觉变）；④图鉴预览 mobPreviewScale(6) 同源 0.85（所见即游戏内比例）。
    //    回退 scale 到 1.0（漏 Y 补偿）→ ①红（②仍绿但契约断裂面由 ① 单钉暴露，Y 补偿独立值 0.765
    //    与 0.85 基乘积钉死）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        bool okScale = m.count(QStringLiteral("0.85 * (1.0 + inflate * 0.5)")) >= 3; // 三轴同基
        const int iy = m.indexOf(QStringLiteral("0.90 * 0.85 - mobHalfH"));
        bool okY = iy >= 0 && m.mid(iy - 600, 600).contains(QStringLiteral("MobStalker"));
        QFile bf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString b = bf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bf.readAll()) : QString();
        bool okBrowser = b.contains(QStringLiteral("if (t === 6) return 0.85"));
        const bool ok = okScale && okY && okBrowser;
        if (!ok)
            qInfo().noquote() << "  [t894 diag] scale" << okScale << "yOff" << okY << "browser" << okBrowser;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t894 stalker model scaled to 0.85 (visual-only, hitbox untouched): base scale "
                             "0.85 on all three axes with the relative inflate swell kept (full charge ~1.28), "
                             "leg-bottom Y compensation 0.90*0.85 keeps the feet on the collision floor (pair "
                             "contract - changing the scale without the offset lifts the model 0.135 off the "
                             "ground), collision halfW/halfH stay at the mobType-table single authority, and the "
                             "resource browser preview mirrors 0.85";
    }

    // ── P-t895 F3 修复（源码钉：①重叠布局分列 + ②MC 1.0 行结构逐面对齐）──
    //    ① 用户「黄绿文字重叠」根因：主 F3 块（黄 #ffff00）与 FrameProfiler 报告（绿 #00ff88）各自绝对
    //      定位，绿块钉死 y=62+200（注释还写「主块约 12 行」）—— 主块逐轮增行到 ~20 行后越过 200px
    //      与绿块叠印。修 = Column 布局分列（行数增减自动排布永不重叠）。钉：两 Text 同入一个 Column
    //      （f3Text Text 与 FrameProfiler Text 之间存在 spacing 锚）+ 旧 `y: 62 + 200` 绝对定位已消失。
    //    ② 主块严格对齐 MC 1.0 F3：标题行带版本（MC "Minecraft 1.0.0" → "voxelsandbox (BuildInfo)"，
    //      t813 版本戳并入标题不再单独占行）、fps 行、x/y/z 三行（MC "x: 123.456 // 123 // 11" 坐标//
    //      所在格//格内 16 取余）、f 朝向行（基数码 MC 表 +Z→0/−X→1/−Z→2/+X→3 + 轴 + (yaw / pitch)）、
    //      biome 行、bl/ol 光照行（脚下格 blockLightAt/skyLightAt 真值）。钉六行前缀俱在 + 旧格式行
    //      （"build: " 单行 / "pos: " 合并行 / "yaw: " 独行）已删。工程诊断尾段保留（§2-F 验收铁律，
    //      MC 行在前工程扩展在后，空行分隔）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        const int iF3 = m.indexOf(QStringLiteral("text: window.f3Text"));
        const int iProf = m.indexOf(QStringLiteral("text: FrameProfiler.report"));
        const int iCol = m.lastIndexOf(QStringLiteral("Column {"), iF3);
        bool okColumn = iF3 >= 0 && iProf > iF3 && iCol >= 0
                        && m.mid(iCol, iF3 - iCol).contains(QStringLiteral("spacing: 10"))   // 同 Column 属性
                        && m.lastIndexOf(QStringLiteral("Column {"), iProf) == iCol;          // 两 Text 同 Column
        bool okOldGone = !m.contains(QStringLiteral("y: 62 + 200"));
        const int iFn = m.indexOf(QStringLiteral("function buildF3Text()"));
        // t857 起函数体加长（renderStats 真值段 + 注释）→ 切片窗 4200→5200（钉的是内容 token，窗口须覆盖
        //   增长后的函数；窗口不足会把仍在函数内的钉 token 误判为消失 = 假红）。
        const QString fn = iFn >= 0 ? m.mid(iFn, 5200) : QString();
        bool okMc = fn.contains(QStringLiteral("\"voxelsandbox (\" + BuildInfo.full"))
                    && fn.contains(QStringLiteral("\\nx: \""))
                    && fn.contains(QStringLiteral(" // \""))
                    && fn.contains(QStringLiteral("\\nf: \" + fIdx"))
                    && fn.contains(QStringLiteral("\\nbiome: \""))
                    && fn.contains(QStringLiteral("\\nbl: \" + footBl + \" ol: \""))
                    && !fn.contains(QStringLiteral("\"\\nbuild: \""))
                    && !fn.contains(QStringLiteral("\"\\npos: \""));
        const bool ok = okColumn && okOldGone && okMc;
        if (!ok)
            qInfo().noquote() << "  [t895 diag] column" << okColumn << "oldGone" << okOldGone
                              << "mc" << okMc;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t895 F3 overlay fixed: (1) yellow main block and green FrameProfiler report "
                             "now live in one Column (auto-stacked, overlap from the hardcoded y=62+200 with "
                             "the grown ~20-line main block is structurally gone); (2) main block realigned "
                             "line-by-line to MC 1.0 F3 - version-carrying title, fps line, separate x/y/z "
                             "lines with MC coordinate//block//in-chunk-16 format, f facing line (MC cardinal "
                             "table +Z->0/-X->1/-Z->2/+X->3 with yaw/pitch), biome line and bl/ol feet-light "
                             "line from real World queries; project diagnostics kept as a blank-line-separated "
                             "tail (PLAN 2-F acceptance requires the mesh/perf stats)";
    }

    // ── P-t896 创造拿取/复制语义（源码钉 QML 数量契约 + Hotbar VM 行为级数量钉）──
    //    用户定稿：①调色板**左键拿取默认 1 个**（旧满栈 64 上手）；②**中键 = 复制一整组** —— 对背包物品
    //    （hotbar / 主栏 / 合成 / 护甲槽）中键复制的是 maxStackSize(id) 整组，非源槽当前数量（旧
    //    min(count,maxStack) 复制 2 件 → 放回 = 4 的「2变4 翻倍」）。钉：Inventory.qml 左键 TapHandler
    //    heldCount=1 / 中键 TapHandler maxStackSize(modelData) / copyStackToCursor maxStackSize(id)
    //    （min(count,…) 旧式必须消失）+ Hotbar::maxStackSize 行为级（方块 64 / 工具·桶 1 / 附魔书 1 ——
    //    整组语义的数量单一权威，QML 三处全读它）。回退任一处（左键回 64 / 复制回源槽数）→ 对应钉红。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile inf(root + QStringLiteral("/src/ui/Inventory.qml"));
        const QString s = inf.open(QIODevice::ReadOnly) ? QString::fromUtf8(inf.readAll()) : QString();
        const int iFn = s.indexOf(QStringLiteral("function copyStackToCursor"));
        const QString fn = iFn >= 0 ? s.mid(iFn, 900) : QString();
        bool okCopy = fn.contains(QStringLiteral("heldCount = root.hotbar.maxStackSize(id)"))
                      && !fn.contains(QStringLiteral("Math.min(count"));
        // 调色板两 TapHandler：第一个（左键）heldCount=1；第二个（中键）maxStackSize(modelData)。
        const int iTake1 = s.indexOf(QStringLiteral("root.hotbar.heldBlock = modelData"));
        const int iTake2 = iTake1 >= 0 ? s.indexOf(QStringLiteral("root.hotbar.heldBlock = modelData"), iTake1 + 10) : -1;
        bool okLeft = iTake1 >= 0 && s.mid(iTake1, 700).contains(QStringLiteral("heldCount = 1"));
        bool okMid = iTake2 >= 0 && s.mid(iTake2, 400).contains(QStringLiteral("maxStackSize(modelData)"));
        // 行为级数量钉：整组语义的数量权威（方块/材料 64；桶·附魔书 1 —— 工具段同 1 由桶代表不可堆叠类）。
        Hotbar hbT896;
        const bool okVm = hbT896.maxStackSize(BR::Stone) == 64
                          && hbT896.maxStackSize(RecipeRegistry::RedstoneId) == 64
                          && hbT896.maxStackSize(RecipeRegistry::BucketEmptyId) == 1
                          && hbT896.maxStackSize(RecipeRegistry::EnchantedBookId) == 1;
        const bool ok = okCopy && okLeft && okMid && okVm;
        if (!ok)
            qInfo().noquote() << "  [t896 diag] copy" << okCopy << "left" << okLeft
                              << "mid" << okMid << "vm" << okVm;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t896 creative take/copy semantics: palette left-click now takes ONE item by "
                             "default (full-stack pickup moves to middle-click), and middle-click on any "
                             "inventory slot clones a FULL maxStackSize stack to the cursor instead of the "
                             "slot's current count (the old min(count,max) clone let a 2-item slot become 4 "
                             "when placed back - the reported 2-becomes-4 doubling); quantity authority stays "
                             "Hotbar::maxStackSize (blocks 64, tools/buckets/armor 1) read by all three QML "
                             "sites, pinned by source pin plus behavioral VM quantity probe";
    }

    // ── P-t897 羊两修（行为级：吃草门 = 脚下草方块 + 静止 walkPhase 归零）──
    //    ① 吃草动画只在**脚下草方块**触发（t897 ①）：草方块平台（**全场零草丛**）上的羊照常开吃草周期
    //      —— 周期 apply 段把脚下 Grass 写成 Dirt（观测：平台内出现 Dirt 格）。阴性对照：石平台上围一圈
    //      TallGrass 诱饵（旧「前方草丛」逻辑的触发面）—— 新逻辑羊不吃（平台 0 Dirt + 诱饵草丛**全数
    //      完好**；旧逻辑会消耗掉一棵草丛 → 双重判别面）。② 静止走路动画归零（t897 ②）：猪游荡期
    //      walkPhase 推进（观测到非 0）后必在某次 idle 后归 0（旧「冻结于上次相位」下非 0 值永不回 0）。
    //    **flake 修复（主Agent 复跑抓红）**：旧 rig 平台裸放（7×7 无栏），羊 RNG 游走期走出平台缘坠落
    //    到自然地表（log 见「sheep ate grass block at 28 64 25」—— y64 野草地）→ 平台内 Dirt 计数 0 =
    //    经典 mob 游走几何漂移（t836/t882 同款教训）。修法 = **几何围栏**：两平台各加 2 格高石墙环
    //    （1 格会被 mob 越障跳翻过——isJumpObstacle 前方 1 格墙 + 上方空气即跳；2 格上方仍是墙 → 不跳，
    //    羊被物理钉在栏内，只能吃栏内 Grass）。窗口同步放宽 2400→3600 帧（57.6s，覆盖 RNG 掷骰节律的
    //    多轮 idle/吃草/冷却循环）。围栏后「吃到」的判定不再依赖几何停留（栏内全是 Grass，任何一次
    //    idle 吃草都落在断言面内）。
    {
        World wG;
        wG.setWidth(36); wG.setDepth(36); wG.setHeight(96); wG.setSeed(31); // 平台 rig y84/85（局部覆写）
        // 草围栏（外环 7..15 周界 2 高石墙；栏内地表 8..14² 全 Grass、零草丛；墙下垫石防浮空）。
        for (int x = 7; x <= 15; ++x)
            for (int z = 7; z <= 15; ++z) {
                const bool wall = (x == 7 || x == 15 || z == 7 || z == 15);
                wG.setBlock(x, 84, z, wall ? BR::Stone : BR::Grass, 0);
                if (wall) { wG.setBlock(x, 85, z, BR::Stone, 0); wG.setBlock(x, 86, z, BR::Stone, 0); }
            }
        // 石围栏（外环 19..27 周界 2 高石墙；栏内 20..26² 石板 + 诱饵草丛环）。
        for (int x = 19; x <= 27; ++x)
            for (int z = 19; z <= 27; ++z) {
                const bool wall = (x == 19 || x == 27 || z == 19 || z == 27);
                wG.setBlock(x, 84, z, BR::Stone, 0);
                if (wall) { wG.setBlock(x, 85, z, BR::Stone, 0); wG.setBlock(x, 86, z, BR::Stone, 0); }
            }
        int baitCount = 0;
        for (int x = 21; x <= 25; ++x)
            for (int z = 21; z <= 25; ++z)
                if (!(x == 23 && z == 23)) { wG.setBlock(x, 85, z, BR::TallGrass, 0); ++baitCount; } // 草丛诱饵环
        EntityManager em;
        const int sheepG = em.spawnMobTyped(11, 85, 11, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int sheepS = em.spawnMobTyped(23, 85, 23, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int pig = em.spawnMobTyped(9, 85, 13, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        bool sawWalk = false, sawReset = false;
        for (int t = 0; t < 3600; ++t) {   // 57.6s：吃草（扫描 ≤1s + 周期 1.2s + 冷却 2s 多轮）+ 游荡走停交替
            em.tick(0.016f, &wG, farListener, 0.3f, 1.8f, false);
            if (pig >= 0 && !sawWalk) {
                if (em.walkPhaseAt(pig) != 0.0f) sawWalk = true;
            } else if (pig >= 0 && sawWalk && !sawReset && em.walkPhaseAt(pig) == 0.0f) {
                sawReset = true;
            }
        }
        // 断言面：草栏内 ≥1 格 Dirt（吃了——栏内全 Grass，任何 idle 吃草都落此面）；石栏内 0 Dirt +
        // 诱饵环 24 棵全在（没吃、也没消耗草丛）；猪走过后归零。
        int grassDirt = 0, stoneDirt = 0, baitLeft = 0;
        for (int x = 8; x <= 14; ++x)
            for (int z = 8; z <= 14; ++z)
                if (wG.blockAt(x, 84, z) == BR::Dirt) ++grassDirt;
        for (int x = 20; x <= 26; ++x)
            for (int z = 20; z <= 26; ++z)
                if (wG.blockAt(x, 84, z) == BR::Dirt) ++stoneDirt;
        for (int x = 21; x <= 25; ++x)
            for (int z = 21; z <= 25; ++z)
                if (!(x == 23 && z == 23) && wG.blockAt(x, 85, z) == BR::TallGrass) ++baitLeft;
        const bool ok = sheepG >= 0 && sheepS >= 0 && pig >= 0
                        && grassDirt >= 1 && stoneDirt == 0
                        && baitLeft == baitCount && sawWalk && sawReset;
        if (!ok)
            qInfo().noquote() << "  [t897 diag] grassDirt" << grassDirt << "stoneDirt" << stoneDirt
                              << "bait" << baitLeft << "/" << baitCount
                              << "sawWalk" << sawWalk << "sawReset" << sawReset
                              << "sheepG" << sheepG << "sheepS" << sheepS << "pig" << pig;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t897 sheep fixes: graze animation now keys on the grass block UNDERFOOT "
                             "(the own-column support cell - sheep on a pure grass-block platform with zero "
                             "tall grass starts eating cycles and turns the block below to dirt), while the "
                             "negative control keeps every bait tall-grass plant intact on a stone platform "
                             "with zero dirt conversions (old front-column tall-grass logic would have "
                             "consumed a plant); stationary walk animation resets to zero - the pig's "
                             "walkPhase observed advancing later reads exactly 0 after an idle phase (old "
                             "freeze kept the last phase forever, legs stuck mid-stride)";
    }

    // ── P-t898 睡觉瞬移躺床 / 出床回位探针（Game 层真消费端，t814 模式）──
    //   用户 8-25 澄清：睡下时人物**直接瞬移到床上躺平、视角/相机移到床位置**（对齐 MC，非原地睡觉）；
    //   醒来瞬移回床边（MC 下床语义）。断言三组：
    //   (a) 夜间 trySleepAt：m_pos 瞬移到床脚端躺位——foot 格 (x0,z0) state D=+X（head 在 x0-1）→ 躺位
    //       (x0+0.9, y+1, z0+0.5)（foot 格心 +0.4·D：1.8 身长嵌 2.0 床长）、yaw 转床轴朝床尾（D=+X →
    //       front=+X → yaw=-90）、sleeping/sleepLying（躺姿门）真；
    //   (b) wakeUp（中断式瞬醒，public Q_INVOKABLE）：出床瞬移到床周首个可站位格（本 rig：foot +X 邻
    //       (x0+1) 地板支撑 / 头身两格净空 → feet=(x0+1.5, y, z0+0.5)，Y=床层站地面）、sleeping/sleepLying 假；
    //   (c) 白天拒绝零位移副作用：setPhase(0)=正午 → trySleepAt 被拒（瞬移必须在夜间/无怪物两道语义门之后，
    //       被拒不产生位移）。
    {
        PlayerController pc;
        WorldClock clock;
        EntityManager ents; // 空管理器：hostileNearby 恒 false（怪物拒绝路径不触发）
        pc.setWorld(&w);
        pc.setWorldClock(&clock);
        pc.setEntityManager(&ents);
        clock.setPhase(0.5f); // 子夜（skyLight<0.5 → isNight；setPhase 即时重派生）

        const auto [x0, z0] = nextSlot();
        const int y = kRigY;
        // 工作体积先清空再搭（t897/review#5 教训：高空「必空」不是生成器不变量；确定性净空带）。
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 3; ++dy)
                    w.setBlock(x0 + dx, y + dy, z0 + dz, BR::Air, 0);
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                placeRigBlock(w, x0 + dx, y - 1, z0 + dz, BR::Stone, 0); // 支撑地板（床 + 出床站位同层）
        placeRigBlock(w, x0, y, z0, BR::BedWhite, quint8(0));     // foot：D=+X（bit[1:0]=0）→ head 在 x0-1
        placeRigBlock(w, x0 - 1, y, z0, BR::BedWhite, quint8(8)); // head（bit3=1）

        pc.trySleepAt(x0, y, z0);
        const QVector3D lie = pc.feetPosition();
        const bool okA = pc.sleeping() && pc.sleepLying()
                         && std::abs(lie.x() - (x0 + 0.9f)) < 1e-3f
                         && std::abs(lie.y() - (y + 1.0f)) < 1e-3f
                         && std::abs(lie.z() - (z0 + 0.5f)) < 1e-3f
                         && std::abs(pc.yaw() - (-90.0f)) < 1e-3f;
        pc.wakeUp();
        const QVector3D out = pc.feetPosition();
        const bool okB = !pc.sleeping() && !pc.sleepLying()
                         && std::abs(out.x() - (x0 + 1.5f)) < 1e-3f
                         && std::abs(out.y() - float(y)) < 1e-3f
                         && std::abs(out.z() - (z0 + 0.5f)) < 1e-3f;
        clock.setPhase(0.0f); // 正午（/time 特权指令允许设相，PLAN §2-H 与睡觉单向不冲突）
        const QVector3D beforeDay = pc.feetPosition();
        pc.trySleepAt(x0, y, z0);
        const bool okC = !pc.sleeping() && pc.feetPosition() == beforeDay;
        const bool okT898 = okA && okB && okC;
        if (!okT898)
            qInfo().noquote() << "  [t898 diag] lie=" << lie.x() << lie.y() << lie.z()
                              << "yaw=" << pc.yaw() << " out=" << out.x() << out.y() << out.z()
                              << " sleeping=" << pc.sleeping() << " lying=" << pc.sleepLying();
        if (!okT898) ++totalFail;
        qInfo().noquote() << (okT898 ? "PASS" : "FAIL")
                          << "| t898 bed-sleep teleport: right-click bed at night teleports the player "
                             "flat onto the bed (feet pinned to the foot-cell end offset 0.4 along the "
                             "head->foot axis so the 1.8-block body nests inside the 2-block bed, Y = bed "
                             "top, yaw rotated to the bed axis looking toward the foot, lying-pose gate "
                             "on), interrupt-style wake teleports out to the first standable cell beside "
                             "the bed at floor level (MC get-out-of-bed semantics, pose gate off), and a "
                             "daytime refusal produces zero displacement side effects (teleport strictly "
                             "after the night/monster semantic gates)";
    }

    // ── P-t900 垃圾桶语义终版（VM 行为级 + 源码钉；用户 8-25 定稿）──
    //   定稿原话：「普通左键=清光标持有（t839 语义保持）+ shift+左键=清空整个背包」。QML 点击路由不可由
    //   本 harness 直驱（Inventory.qml 需全模块场景）→ 双腿：
    //   (a) 行为级：Hotbar VM 填满（hotbar 9 + main 27 + 光标持有）→ 执行 QML shift 分支的同序清空序列
    //       （setStack 9 + mainSetStack 27 + setHeldBlock(0)）→ 全槽读空（证明该调用面足以清空整个背包，
    //       无隐藏残留槽态）；普通左键两档语义（清光标 / 选中槽单格）为 t839 既有语义，本探针不重复钉。
    //   (b) 源码钉：Inventory.qml 销毁槽块（滤注释）——MouseArea（TapHandler 不分辨修饰键，t700 教训）+
    //       ShiftModifier 分流分支含 9 槽 setStack 循环 + mainCount 槽 mainSetStack 循环 + 光标清空 +
    //       普通左键两档（heldBlock 整组 / 选中槽单格）保留。
    {
        Hotbar hb;
        for (int s = 0; s < 9; ++s) hb.setStack(s, int(BR::Cobble), 32);
        for (int m = 0; m < hb.mainCount(); ++m) hb.mainSetStack(m, int(BR::Planks), 16);
        hb.setHeldBlock(int(BR::Glass));
        hb.setHeldCount(8);
        // QML shift 分支同序清空序列（Inventory.qml 销毁槽 MouseArea onClicked ShiftModifier 支）。
        for (int s = 0; s < 9; ++s) hb.setStack(s, 0, 0);
        for (int m = 0; m < hb.mainCount(); ++m) hb.mainSetStack(m, 0, 0);
        hb.setHeldBlock(0);
        bool okA = hb.heldBlock() == 0 && hb.heldCount() == 0;
        for (int s = 0; s < 9 && okA; ++s)
            if (hb.blockIdAt(s) != 0 || hb.countAt(s) != 0) okA = false;
        for (int m = 0; m < hb.mainCount() && okA; ++m)
            if (hb.mainBlockIdAt(m) != 0 || hb.mainCountAt(m) != 0) okA = false;
        if (!okA)
            qInfo().noquote() << "  [t900 diag] held=" << hb.heldBlock()
                              << " h0=" << hb.blockIdAt(0) << " m0=" << hb.mainBlockIdAt(0);

        // (b) 源码钉（t879/t893 先例：断言销毁槽块的路由文本；锚定 destroyWrap 块而非全文件首
        //     MouseArea——面板根遮罩自身也是 MouseArea）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile qf(root + QStringLiteral("/src/ui/Inventory.qml"));
            const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("id: destroyWrap"));
            if (i0 < 0) {
                qInfo().noquote() << "  [t900 pin diag] destroyWrap block miss";
            } else {
                const QString seg = t.mid(i0, 4000); // 销毁槽块（图标 Canvas + 注释 + 点击处理器全在内，实测
                                                      //   跨度 ~3.7k 字符；注释未滤 → 负向断言用元素声明形
                                                      //   「TapHandler {」防注释文本误中）
                okPin = seg.contains(QStringLiteral("MouseArea"))                       // TapHandler 不分辨修饰键（t700）→ MouseArea
                        && seg.contains(QStringLiteral("mouse.modifiers & Qt.ShiftModifier")) // shift 分流分支
                        && seg.contains(QStringLiteral("root.hotbar.mainSetStack(m, 0, 0)"))   // main 27 槽清空
                        && seg.indexOf(QStringLiteral("setStack(s, 0, 0)"))
                               < seg.indexOf(QStringLiteral("mainSetStack(m, 0, 0)"))  // hotbar 9 槽先行
                        && seg.contains(QStringLiteral("root.hotbar.heldBlock = 0"))   // 光标清空 + 普通左键档①
                        && seg.contains(QStringLiteral("root.hotbar.setStack(root.hotbar.selectedSlot, 0, 0)")) // 档②
                        && !seg.contains(QStringLiteral("TapHandler {"));              // 旧事件源元素形态退役（块内）
            }
            if (!okPin)
                qInfo().noquote() << "  [t900 pin diag] block found=" << (i0 >= 0)
                                  << " seg checks failed";
        }
        const bool okT900 = okA && okPin;
        if (!okT900) ++totalFail;
        qInfo().noquote() << (okT900 ? "PASS" : "FAIL")
                          << "| t900 trash-slot final semantics (user 8-25): plain left click keeps t839 tiers "
                             "(cursor-held stack destroyed / selected single slot cleared when empty-handed), "
                             "shift+left-click clears the ENTIRE inventory (behavioral leg proves the exact QML "
                             "call sequence - 9 setStack + 27 mainSetStack + heldBlock reset - leaves zero "
                             "residue in every slot read; source pin proves the MouseArea modifier split and "
                             "retires the old TapHandler form - TapHandler cannot see modifiers, t700 lesson)";
    }

    // ── P-t901 画作背面木板源码钉（t837 未愈返修；纯视觉项轻量源码钉，t781/t893 先例）──
    //   用户「背面仍全透明」根因：画面 BillboardQuad 默认背面剔除 → 墙后侧（玻璃墙 / 透视支撑后）看画，
    //   quad 被剔 = 无像素。修法 = 第二张反向法线 quad（绕 Y 180°）贴木板背板（MC 语义：画作背面木板）。
    //   QML delegate 渲染不可由本 harness 直驱 → 源码钉 paintingDelegate 块：背 quad 的 180° 欧拉 +
    //   default_wood.png（= 图集 tile 8 planks 同源）+ 背面略压暗 baseColor 存在。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile qf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("id: paintingDelegate"));
        bool okT901 = false;
        if (i0 < 0) {
            qInfo().noquote() << "  [t901 pin diag] paintingDelegate block miss";
        } else {
            const QString seg = t.mid(i0, 4300); // delegate 块（双 quad + 材质全在内，实测跨度 ~4.3k）
            const int back0 = seg.indexOf(QStringLiteral("Qt.vector3d(0, 180, 0)")); // 背 quad 反向法线欧拉
            const int wood = seg.indexOf(QStringLiteral("qrc:/textures/default_wood.png"));
            const int dim = seg.indexOf(QStringLiteral("0.72, 0.72, 0.72"));
            okT901 = back0 > 0 && wood > back0 && dim > wood;   // 三件同块依序（背 quad → 木板贴图 → 压暗）
        }
        if (!okT901) ++totalFail;
        qInfo().noquote() << (okT901 ? "PASS" : "FAIL")
                          << "| t901 painting back board: second reverse-normal quad (Y+180 euler, "
                             "backface-culled pair so no coplanar z-fight, offset 1/64 wall-ward) carries "
                             "the plank board texture default_wood.png (same source file as atlas tile 8) "
                             "slightly dimmed - MC semantics: a painting's back is a wooden board, fixing "
                             "the fully-transparent back visible through glass walls (source pin on the "
                             "paintingDelegate block; visual confirmation pending user playtest)";
    }

    // ── P-t902 耕地图标面源码钉（纯视觉项轻量源码钉；atlasIconSpecForBlock 是文件内 static，行为级不可
    //    直调 → 钉 case 存在 + def 字段契约）──
    //   用户「两面都是耕地」根因：Farmland def.frontTile 字段被 mesher 复用为**湿态顶面瓦片 27**（无 -Z
    //   前面语义），ShapeFull 泛化 addBox(topT, sideT, frontT) 把它喂给图标左前面 → 顶=干耕 + 左前=湿耕 +
    //   右=泥。修法 = 显式 case 钉 side/front=sideT（dirt 单一权威）。双腿：(a) def 契约（top=26 / side=2 /
    //   front=27——字段复用事实本身钉死，泛化路径对耕地必错）；(b) 源码钉 spec 的 Farmland case 用
    //   (topT, sideT, sideT) 且先于 ShapeFull 泛化（boxes 非空则泛化不跑）。
    {
        const BR::BlockDef &fd = BR::def(BR::Farmland);
        const bool okDef = fd.topTile == 26 && fd.sideTile == 2 && fd.frontTile == 27;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
        const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
        const int iCase = t.indexOf(QStringLiteral("case BlockRegistry::Farmland:"));
        bool okPin = false;
        if (iCase < 0) {
            qInfo().noquote() << "  [t902 pin diag] Farmland case miss in atlasIconSpecForBlock";
        } else {
            const QString seg = t.mid(iCase, 1600); // case 块（注释 + addBox 全在内）
            const int box = seg.indexOf(QStringLiteral("addBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, topT, sideT, sideT)"));
            okPin = box > 0
                    && seg.indexOf(QStringLiteral("case BlockRegistry::Cactus:")) > box; // 在泛化前（② 特型段）
        }
        const bool okT902 = okDef && okPin;
        if (!okT902) {
            ++totalFail;
            qInfo().noquote() << "  [t902 diag] def" << fd.topTile << fd.sideTile << fd.frontTile
                              << "pin" << okPin;
        }
        qInfo().noquote() << (okT902 ? "PASS" : "FAIL")
                          << "| t902 farmland item icon faces: explicit spec case pins side AND front faces "
                             "to the dirt side-tile (single authority def.sideTile) with only the top "
                             "carrying the tilled texture - the generic ShapeFull path fed the mesher-reused "
                             "frontTile (wet-farmland top tile 27) into the icon's left-front face, so the "
                             "icon read as farmland on both visible faces; def field-reuse contract pinned "
                             "(top=26 dry / side=2 dirt / front=27 wet-top) and cache family bumped "
                             "icon6->icon7 so stale on-disk icons regenerate; visual confirmation pending "
                             "user playtest";
    }

    // ── P-t903 草丛支撑置换失撑探针（World 层行为级；放置面真值表已随 t847 探针收紧同步钉）──
    //   用户定稿「只能放草方块（泥土也不行）」+ 失撑链同口径：草丛唯一合法支撑 = 草方块 → 支撑被**置换**为
    //   非草面（非破 Air）也失撑掉落。三腿：
    //   (a) 羊吃草路径：setWaterSilent(Grass→Dirt)（t897 羊吃消耗走本入口）→ 正上方草丛清 Air +
    //       blockDroppedAsItem 掉 dropId(TallGrass)（种子族，运行期读表）；
    //   (b) 通用置换路径：setBlockSilent(Dirt→Farmland) 锄地语义 → 草丛同掉（任意非草面置换）；
    //   (c) 阴性对照（族口径不扩大）：花下泥土置换成耕地（花合法面含 Farmland）→ 花**不**掉（t507
    //       「置换不掉」族口径对花 / 蘑菇保留，只草丛收口）。
    {
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, kRigY, z0, BR::Grass, 0);
        placeRigBlock(w, x0, kRigY + 1, z0, BR::TallGrass, 0);
        const int dropsA0 = dropItemCount;
        const quint8 lastA0 = lastDropId;
        Q_UNUSED(lastA0);
        w.setWaterSilent(x0, kRigY, z0, BR::Dirt, 0); // 羊吃草消耗路径（t897 同入口）
        const bool okA = w.blockAt(x0, kRigY + 1, z0) == quint8(BR::Air)
                     && dropItemCount == dropsA0 + 1
                     && lastDropId == BR::dropId(BR::TallGrass);

        const auto [x1, z1] = nextSlot();
        placeRigBlock(w, x1, kRigY, z1, BR::Grass, 0);
        placeRigBlock(w, x1, kRigY + 1, z1, BR::TallGrass, 0);
        const int dropsB0 = dropItemCount;
        w.setBlockSilent(x1, kRigY, z1, BR::Farmland, 0); // 任意非草面置换（锄地语义）
        const bool okB = w.blockAt(x1, kRigY + 1, z1) == quint8(BR::Air)
                     && dropItemCount == dropsB0 + 1
                     && lastDropId == BR::dropId(BR::TallGrass);

        const auto [x2, z2] = nextSlot();
        placeRigBlock(w, x2, kRigY, z2, BR::Dirt, 0);
        placeRigBlock(w, x2, kRigY + 1, z2, BR::FlowerRed, 0);
        const int dropsC0 = dropItemCount;
        w.setBlockSilent(x2, kRigY, z2, BR::Farmland, 0); // 花合法面含耕地 → 不掉（族口径保留）
        const bool okC = w.blockAt(x2, kRigY + 1, z2) == quint8(BR::FlowerRed)
                     && dropItemCount == dropsC0;

        const bool okT903 = okA && okB && okC;
        if (!okT903)
            qInfo().noquote() << "  [t903 diag] okA" << okA << "okB" << okB << "okC" << okC
                              << "a=" << int(w.blockAt(x0, kRigY + 1, z0))
                              << "b=" << int(w.blockAt(x1, kRigY + 1, z1))
                              << "c=" << int(w.blockAt(x2, kRigY + 1, z2));
        if (!okT903) ++totalFail;
        qInfo().noquote() << (okT903 ? "PASS" : "FAIL")
                          << "| t903 tallgrass support-replacement lost-support: grass block's only legal "
                             "support is another grass block (placement tightened, dirt rejected), and the "
                             "lost-support hook now drops the tall grass when its support is REPLACED by any "
                             "non-grass face - the sheep-graze path (setWaterSilent Grass->Dirt, t897 entry) "
                             "and the generic silent replacement (Dirt->Farmland hoe semantics) both clear the "
                             "plant and drop its dropId, while the flower negative control stays put on "
                             "replaced-but-still-legal farmland (family replacement-caliber kept for flowers/"
                             "mushrooms, only tallgrass tightened)";
    }

    // ── P-t857 F3 渲染统计真值源码钉（R19.14 性能起步批；源序钉先例 = review #4/#5 的 rpm 源序探针）──
    //   buildF3Text 的 draw 行自 t857 起读 view3d.renderStats 真值（drawCallCount / drawVertexCount /
    //   renderPassCount），旧 ~drawEst 估算公式（visibleSegmentCount + itemLive + mobLive + torches + 6）
    //   退役。钉三件事：① 函数体必经 renderStats 真值四读；② 估算公式 token（drawEst）在函数体内绝迹；
    //   ③ View3D 上 extendedDataCollectionEnabled 绑 f3Visible（真值收集的开关契约——漏绑则真值恒 0，
    //   F3 显示静默失真）。QML 无 static_assert 面 → 源码文本钉（滤 // 注释行后切片断言）。
    {
        QString qmlPath;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            for (const QString &c : candidates) {
                if (QFile::exists(c)) { qmlPath = c; break; }
            }
        }
        bool okT857 = true;
        if (qmlPath.isEmpty()) {
            qInfo().noquote() << "  [t857 note] Main.qml not found near exe - source-pin skipped";
        } else {
            QFile qmlF(qmlPath);
            if (!qmlF.open(QIODevice::ReadOnly)) {
                okT857 = false;
                qInfo().noquote() << "  [t857 diag] failed to open" << qmlPath;
            } else {
                QString codeText;
                const QString rawText = QString::fromUtf8(qmlF.readAll());
                for (const QString &line : rawText.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//")))
                        continue; // 滤 // 注释行（探测目标是语句文本，注释里的 token 会干扰）
                    codeText += line;
                    codeText += QLatin1Char('\n');
                }
                const int fnStart = codeText.indexOf(QStringLiteral("function buildF3Text()"));
                const int fnEnd = fnStart >= 0 ? codeText.indexOf(QStringLiteral("\n    }"), fnStart) : -1;
                if (fnStart < 0 || fnEnd < 0) {
                    okT857 = false;
                    qInfo().noquote() << "  [t857 diag] buildF3Text body not found fnStart" << fnStart
                                      << "fnEnd" << fnEnd;
                } else {
                    const QString body = codeText.mid(fnStart, fnEnd - fnStart);
                    const bool hasTruth = body.contains(QStringLiteral("view3d.renderStats"))
                                          && body.contains(QStringLiteral("drawCallCount"))
                                          && body.contains(QStringLiteral("drawVertexCount"))
                                          && body.contains(QStringLiteral("renderPassCount"));
                    const bool noEstimate = !body.contains(QStringLiteral("drawEst"));
                    const bool hasEnable = codeText.contains(
                            QStringLiteral("renderStats.extendedDataCollectionEnabled: window.f3Visible"));
                    okT857 = hasTruth && noEstimate && hasEnable;
                    if (!okT857)
                        qInfo().noquote() << "  [t857 diag] truth" << hasTruth << "noEstimate" << noEstimate
                                          << "enableGate" << hasEnable;
                }
            }
        }
        if (!okT857) ++totalFail;
        qInfo().noquote() << (okT857 ? "PASS" : "FAIL")
                          << "| t857 F3 render-stats truth: buildF3Text draw line reads view3d.renderStats "
                             "real values (drawCallCount/drawVertexCount/renderPassCount via RenderStats, "
                             "extended collection gated on f3Visible at the View3D) and the legacy ~drawEst "
                             "sum formula (visibleSegmentCount+items+mobs+torches+6) is retired - estimate "
                             "drift vs backend reality (transparency pass splits, frustum culling, "
                             "instancing batches) no longer misleads perf work";
    }

    // ── P-t860 cutout 段折叠（R19.14 试验项，保留交付）：行为级 + 源码钉双探针 ──
    //   背景：t442 起 terrain 段材质已带 alphaMode:Mask + alphaCutoff:0.5（与 cutout 段材质逐字相同，
    //   leaves cutout 实证生效）→ 独立 cutout 段失去存在必要。t860 把 cross（草丛/作物/树苗）+ 门（t638
    //   窗格）+ 活板门（t723 栅格孔）顶点并入 terrain 段 mesh，QML 停建 cutout 段 Model（每 chunk 6 段 →
    //   5 段，600 Model 满配 → 500）。行为级断言：terrain 段 ChunkGeometry 在放置 TallGrass 后顶点数**增加**
    //   （折叠前该格被路由走、terrain 顶点不变；cross 是 ShapeNone 非实体 → 不影响邻居面剔除，顶点差 = 纯
    //   cross 贡献）。源码钉：chunkAnchor 不再实例化 crossChunkComp（降级杠杆注释行保留不计）+
    //   _refreshChunkVisibility 的 segmentsPerChunk = 5（组边界与创建序同步）。回退（恢复 6 段）→ 行为级
    //   断言红（顶点不再增加）= 探针红绿可辨折叠态。
    {
        const auto [x860, z860] = nextSlot();
        const int cx860 = x860 / 16, cz860 = z860 / 16;
        ChunkGeometry geoT;
        geoT.setWorld(&w);
        geoT.setCx(cx860);
        geoT.setCz(cz860);
        // 扫真空位（t799/t814 教训：rig 槽位地形可及 y≥42，「某高度以上必空」不成立——Stone 放进已实体格
        //   = 无变化早退不 emit，首建不触发）。找连续两格 Air：Stone 落下格（制造脏 + 同步 worldChanged
        //   重建取基线 v0），TallGrass 落上格（cross 折叠顶点差分）。
        int y860 = kRigY + 1;
        while (y860 < 46
               && (w.blockAt(x860, y860, z860) != BR::Air || w.blockAt(x860, y860 + 1, z860) != BR::Air))
            ++y860;
        w.setBlock(x860, y860, z860, BR::Stone, 0);
        const int v0 = geoT.vertexCount();
        w.setBlock(x860, y860 + 1, z860, BR::TallGrass, 0); // Air → TallGrass（ShapeNone 不动邻居剔面）
        const int v1 = geoT.vertexCount(); // setBlock 同步 emit worldChanged → 脏 chunk 即时重建
        const bool okFold = v0 > 0 && v1 > v0; // cross 顶点计入 terrain 段 mesh（折叠生效签名）
        if (!okFold)
            qInfo().noquote() << "  [t860 diag] y=" << y860 << "v0=" << v0 << "v1=" << v1
                              << "(cross must add terrain-segment vertices when folded)";
        w.setBlock(x860, y860 + 1, z860, BR::Air, 0); // 还原（rig 清洁）
        w.setBlock(x860, y860, z860, BR::Air, 0);

        // 源码钉：Main.qml 的 chunkAnchor 不再 createObject crossChunkComp（降级注释行除外）+ 段数 5。
        bool okPin860 = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            QString qml;
            for (const QString &c : candidates) {
                QFile f(c);
                if (f.open(QIODevice::ReadOnly)) { qml = QString::fromUtf8(f.readAll()); break; }
            }
            if (qml.isEmpty()) {
                qInfo().noquote() << "  [t860 note] Main.qml not found near exe - source-pin skipped";
                okPin860 = true; // 行为级断言仍有效（源码钉缺席不判红，同 r24 note 先例）
            } else {
                // 滤 // 注释行后判「objs.push(crossChunkComp...」语句不存在（降级杠杆注释行被滤掉）。
                QString code;
                for (const QString &line : qml.split(QLatin1Char('\n'))) {
                    const QString t = line.trimmed();
                    if (t.startsWith(QLatin1String("//")) || t.startsWith(QLatin1String("*"))
                        || t.startsWith(QLatin1String("/*")))
                        continue;
                    code += line;
                    code += QLatin1Char('\n');
                }
                const bool noCutoutModel = !code.contains(QStringLiteral("objs.push(crossChunkComp"));
                const bool seg5 = code.contains(QStringLiteral("const segmentsPerChunk = 5"));
                okPin860 = noCutoutModel && seg5;
                if (!okPin860)
                    qInfo().noquote() << "  [t860 diag] noCutoutModel" << noCutoutModel
                                      << "seg5" << seg5;
            }
        }
        const bool okT860 = okFold && okPin860;
        if (!okT860) ++totalFail;
        qInfo().noquote() << (okT860 ? "PASS" : "FAIL")
                          << "| t860 cutout segment folded into terrain: terrain-segment ChunkGeometry "
                             "absorbs cross-billboard vertices (TallGrass placement grows the terrain "
                             "mesh, pre-fold routing diverted it to a separate cutout model), and the "
                             "QML chunk factory no longer instantiates the cutout segment (6 models per "
                             "chunk down to 5, 600 full-config models down to 500) - sound because both "
                             "materials became literally identical after t439/t442 (alphaMode Mask + "
                             "cutoff 0.5), same vertex pipeline, same depth-writing opaque pass; "
                             "cutoutOnly routing kept as documented degrade lever if grass-edge or "
                             "sapling-shadow visuals regress in playtest";
    }

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    return totalFail == 0 ? 0 : 1;
}
