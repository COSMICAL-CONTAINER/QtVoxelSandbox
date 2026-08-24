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
#include <cmath>
#include <algorithm> // t795 探针 std::max（环带切比雪夫距离判定）

#include "blockregistry.h"
#include "toolregistry.h" // t762 黑曜石挖掘规则探针（miningTime / canHarvest / miningSpeedMul 纯表查询）
#include "hotbar.h"       // t763 附魔数值生效链探针（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源）
#include "recipe.h"       // t802 全配方审计探针（match 两阶段匹配 + recipeAt 全表自匹配回归）
#include "smelting.h"     // t802 云杉链熔炉补缺探针（SpruceLog→木炭 + 云杉原木/木板燃料）
#include "playerstate.h"  // t755 死亡态硬锁探针（致死落库 0 / heal 死亡免疫 / respawn 复位链）
#include "playerprogress.h" // review #22 农夫计数回放链式补前置探针（loadVariant → unlockWithAncestry）
#include "world.h"
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
#include "dispenserstore.h"       // t814 发射器/投掷器 per-block 库存（分派 + 扣减断言源）

// t777 探针：羊毛层合成器（resourcepackmanager.cpp 文件级函数，头文件外声明 → extern 直连；spawnEggTint
//   进了 .h 因 EggTint 是头内类型，本函数签名纯 QString 无需入头）。review #6：第 3 参 revision 进文件名
//   `_r<rev>`（换包逐版缓存 + 清旧；探针传任意探针版号）。
extern QString generateSheepWoolFaceFile(const QString &furPath, const QString &bodyPath, int revision);
// Review 2026-08-23 #1 探针：slim 皮肤布局探测器（同上 extern 直连；签名纯 QImage 无需入头）。
extern bool probeSlimSkinLayout(const QImage &tex);

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
        ++slotIdx;
        if (4 + row * 3 >= 128)
            qFatal("rig grid exhausted: slot %d beyond 128-deep grid (4 cols x 42 rows = 168) - "
                   "out-of-bounds setBlock is silently rejected = false FAIL farm", slotIdx);
        return QPair<int, int>(4 + col * 22, 4 + row * 3);
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
        QVector<int> gotId, gotCount, gotDur, gotX, gotZ;
        QVector<QVariantList> gotEnch;
        QVector<QString> gotName;
        QObject::connect(&pc, &PlayerController::spawnItem, &pc,
                         [&](int x, int, int z, int id, int count, const QVariantList &ench,
                             const QString &name, int dur) {
            gotX.push_back(x); gotZ.push_back(z);
            gotId.push_back(id); gotCount.push_back(count);
            gotEnch.push_back(ench); gotName.push_back(name); gotDur.push_back(dur);
        });
        pc.dropAllItems();   // 死亡本体链（Main.qml onDied 主链同调）
        // ① 发射序列：hotbar（泥 64 → 剑 1）→ main（棍 32）→ held（石 3）→ armor（盔 1），逐栈一实体 +
        //    剑 / 盔的附魔首元、实例名、磨损耐久逐参断言（t590/t622/t686 三参透传的死亡路径回归面）。
        const bool emitOk = gotId.size() == 5
                && gotId[0] == int(BR::Dirt) && gotCount[0] == 64
                && gotId[1] == diaSword && gotCount[1] == 1
                && gotDur[1] == 800 && gotEnch[1].size() == 4 && gotEnch[1][0].toInt() == sharp3
                && gotName[1] == QString::fromUtf8("屠龙")
                && gotId[2] == RecipeRegistry::StickId && gotCount[2] == 32
                && gotId[3] == int(BR::Stone) && gotCount[3] == 3
                && gotId[4] == diaHelm && gotCount[4] == 1
                && gotDur[4] == 300 && gotEnch[4][0].toInt() == prot2;
        // 散布面：死亡格（m_pos=80,80,80 → cx=cz=80）3×3 邻域内（|dx| ≤ 1 且 |dz| ≤ 1）。
        bool scatterOk = emitOk;
        for (int i = 0; i < gotX.size() && scatterOk; ++i)
            scatterOk = std::abs(gotX[i] - 80) <= 1 && std::abs(gotZ[i] - 80) <= 1;
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
                          << "| death drop chain: hotbar+main+held+armor all scatter-dropped (3x3) with "
                             "ench/name/durability passthrough, inventory cleared on drop, second call "
                             "emits nothing (t852)";
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
    //    图集重渲 flat 2D，回退链断链 = 空图标 = FAIL 面）。注：④ 在本测试二进制只验「URL 解析链通」——测试
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
                             "resolve for survival drop chains, glass block icon resolves via flat-2D "
                             "runtime re-render";
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
                             "classic(end u=56)/slim(end u=54) at base 64x32 + HD 2x, degenerate 32x16 "
                             "conservative-classic"
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
    //   rev-parse --short HEAD（7-10 hex；git 缺失回退 "nogit" 会在此 FAIL —— 开发机 git
    //   必在，缺 git 属环境异常，诚实暴露优于静默糊弄）。探针跑在矩阵测试 exe 里 = 顺带
    //   验证「该 exe 的 stamp 随本次构建刷新」（构建-运行同刻，分钟差即重建链生效证据）。
    {
        const QString stamp = BuildInfo::instance()->stamp();
        const QString ghash = BuildInfo::instance()->gitHash();
        const QRegularExpression stampRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"));
        const QRegularExpression gitRe(QStringLiteral("^[0-9a-f]{7,10}$"));
        const bool okStamp = stampRe.match(stamp).hasMatch();
        const bool okGit = gitRe.match(ghash).hasMatch();
        const bool okT813 = okStamp && okGit;
        if (!okT813) ++totalFail;
        qInfo().noquote() << (okT813 ? "PASS" : "FAIL")
                          << "| t813 build stamps: stamp" << stamp
                          << "(YYYY-MM-DD HH:MM) git" << ghash
                          << "(7-10 hex, git rev-parse --short HEAD); header regenerated every "
                             "build via cmake/WriteBuildStamp.cmake with content-change-only "
                             "rewrite so only buildinfo.cpp recompiles";
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

        // (a) TNT：拉杆贴合（器件先就位稳态 → 后扳拉杆，用户实测路径）。
        bool okA = false;
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::TntBlock, 0);
            tickN(w, 2);
            w.setBlock(x0 + 1, kRigY, z0, BR::Lever, 1); // 扳开（state bit0=1）
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
            w.setBlock(x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::ArrowId, 3);
            tickN(w, 2);
            w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
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

        // (e) 沿语义（复用 (b) 机器——同 (x,z) 键同基线/冷却）：稳定通电下再制造电力活动（对侧拉杆扳开→
        //     扳回两次触达）不重复发射；源拆→快速复置（真上升沿但 <2s 冷却）也不发射。
        bool okE = false;
        {
            w.setBlock(bx0 - 1, kRigY, bz0, BR::Lever, 1); // 对侧第二源扳开 → 复算触达（升沿到已通电机）
            tickN(w, 4);
            w.setBlock(bx0 - 1, kRigY, bz0, BR::Lever, 0); // 扳回 → 降沿触达
            tickN(w, 4);
            int arrows2 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows2;
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);   // 拆源（降沿）
            tickN(w, 4);
            w.setBlock(bx0 + 1, kRigY, bz0, BR::RedstoneBlock, 0); // 复置（真上升沿，但冷却 2s 未过）
            tickN(w, 4);
            int arrows3 = 0;
            for (int i = 0; i < ents.count(); ++i)
                if (ents.kindAt(i) == EntityManager::Arrow) ++arrows3;
            okE = arrows2 == arrowsAfterB && arrows3 == arrowsAfterB
                  && store.slotCountAt(bx0, kRigY, bz0, 0) == 2; // 库存不再扣（无第二次发射）
            if (!okE)
                qInfo().noquote() << "  [t814 e diag] arrowsAfterB=" << arrowsAfterB
                                  << " arrowsAfterStable=" << arrows2 << " arrowsAfterRepower=" << arrows3
                                  << " slotCount=" << store.slotCountAt(bx0, kRigY, bz0, 0);
            // 清场
            w.setBlock(bx0 + 1, kRigY, bz0, BR::Air, 0);
            w.setBlock(bx0, kRigY, bz0, BR::Air, 0);
            store.clearDispenser(bx0, kRigY, bz0);
            ents.clearAll();
            tickN(w, 2);
        }

        // (c) 投掷器：拉杆贴合 + store 预填 5 粉 → 掉落物弹出（dropper 只投不射，spawnItemAt 分支）。
        bool okC = false, okCInv = false;
        {
            const auto [x0, z0] = nextSlot();
            w.setBlock(x0, kRigY, z0, BR::Dropper, 0);
            store.ensureDispenser(x0, kRigY, z0);
            store.setSlot(x0, kRigY, z0, 0, RecipeRegistry::RedstoneId, 5);
            tickN(w, 2);
            w.setBlock(x0 + 1, kRigY, z0, BR::Lever, 1);
            tickN(w, 4);
            okC = items.count() == 1;
            okCInv = store.slotIdAt(x0, kRigY, z0, 0) == RecipeRegistry::RedstoneId
                     && store.slotCountAt(x0, kRigY, z0, 0) == 4;
            if (!okC || !okCInv)
                qInfo().noquote() << "  [t814 c diag] items.count=" << items.count()
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
            w.setBlock(x0, kRigY, z0, BR::Dispenser, 0);
            store.ensureDispenser(x0, kRigY, z0); // 有条目但库存全空
            tickN(w, 2);
            const int entsBefore = ents.count(), itemsBefore = items.count();
            w.setBlock(x0 + 1, kRigY, z0, BR::RedstoneBlock, 0);
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

        const bool okT814 = okA && okB && okBInv && okC && okCInv && okD && okE;
        if (!okT814) ++totalFail;
        qInfo().noquote() << (okT814 ? "PASS" : "FAIL")
                          << "| t814 real-consumer probes: Main.qml forwarding mirrored onto actual "
                             "PlayerController.firePowerTnt/fireDispenserAtQml (lever->TNT clears block + spawns "
                             "primed entity at cell center; redstone-block->dispenser w/ 3 arrows fires 1 + "
                             "decrements to 2; lever->dropper w/ 5 dust pops 1 item entity + decrements to 4; "
                             "empty tracked dispenser powered = design no-op; stable-power re-touch and "
                             "sub-2s-cooldown re-power both do not re-fire) - consumer leg never executed by "
                             "P15/t773 before, iron-door contrast explained (door = in-World state write, "
                             "TNT/dispenser = signal->QML->consumer)";
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
        WorldStore store;
        const QString dbAbs = QDir::temp().absoluteFilePath(QStringLiteral("voxel_t822_probe.sqlite"));
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
                const int durA = armor ? vm2.armorDurabilityAt(i) : (i < 9 ? vm2.durabilityAt(i) : vm2.mainDurabilityAt(i - 9));
                const int durB = armor ? vm3.armorDurabilityAt(i) : (i < 9 ? vm3.durabilityAt(i) : vm3.mainDurabilityAt(i - 9));
                const QString nmA = armor ? vm2.armorCustomNameAt(i) : (i < 9 ? vm2.customNameAt(i) : vm2.mainCustomNameAt(i - 9));
                const QString nmB = armor ? vm3.armorCustomNameAt(i) : (i < 9 ? vm3.customNameAt(i) : vm3.mainCustomNameAt(i - 9));
                const QVariantList eA = armor ? vm2.armorEnchantsAt(i) : (i < 9 ? vm2.enchantsAt(i) : vm2.mainEnchantsAt(i - 9));
                const QVariantList eB = armor ? vm3.armorEnchantsAt(i) : (i < 9 ? vm3.enchantsAt(i) : vm3.mainEnchantsAt(i - 9));
                if (idA != idB || durA != durB || nmA != nmB || eA.size() != eB.size()) return false;
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
                carts.pushEmptyCart(&w, player, 0.0f, -1.0f);     // 长按 W 朝北推（t809 玩家模型）
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
            //   isSolid 语义 = 非 air 实存（world.h 注）→ 轨格本身是「实体支撑」：释放 mob（resting 已清）
            //   下一物理 tick 落定扫描命中轨格 → 贴其 cell 顶（kRigY+1）+ halfH —— 与引擎既有落定公式一致
            //   （非本任务新增行为），断言按它写死。
            const QVector3D cp = carts.posAt(0);
            const bool hit = carts.hitCartFromRay(QVector3D(cp.x(), cp.y() + 3.0f, cp.z()),
                                                  QVector3D(0.0f, -1.0f, 0.0f), 8.0f, nullptr, true);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false);
            ents.tickVehicleRiding();
            const bool released = hit && !carts.aliveAt(0) && ents.rideCartAt(mobA) == -1 && ents.aliveAt(mobA);
            ents.tick(0.016, &w, player, 0.3f, 1.8f, false); // 释放后首个物理 tick：重力落定重接手
            ents.tickVehicleRiding();
            const float settleY = ents.posAt(mobA).y();
            const bool settleOk = std::fabs(settleY - (float(kRigY) + 1.5f)) <= 0.06f;
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

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    return totalFail == 0 ? 0 : 1;
}
