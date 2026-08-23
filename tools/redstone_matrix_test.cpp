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
#include <QDebug>
#include <cmath>
#include <algorithm> // t795 探针 std::max（环带切比雪夫距离判定）

#include "blockregistry.h"
#include "toolregistry.h" // t762 黑曜石挖掘规则探针（miningTime / canHarvest / miningSpeedMul 纯表查询）
#include "hotbar.h"       // t763 附魔数值生效链探针（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源）
#include "recipe.h"       // t802 全配方审计探针（match 两阶段匹配 + recipeAt 全表自匹配回归）
#include "smelting.h"     // t802 云杉链熔炉补缺探针（SpruceLog→木炭 + 云杉原木/木板燃料）
#include "playerstate.h"  // t755 死亡态硬锁探针（致死落库 0 / heal 死亡免疫 / respawn 复位链）
#include "world.h"
#include "partialblockgeometry.h" // t737 拐角象限断言（mesher 同源调用）
#include "minecartmanager.h"      // t737 环线矿车绕圈断言（骑乘 / 空车两路）
#include "entitymanager.h"        // 审查 #1 末影眼巡航高度回归探针（spawnEnderEye + enderEyeCruiseYAt）
#include "resourcepackmanager.h"  // t785 生物蛋探针（生成式染色表 spawnEggTint 条目存在性直调）
#include "itementitymanager.h"    // t804 掉落物火焚探针（item 入 Fire 格 0.8s 焚毁 + itemBurned 烟信号）
#include "boatmanager.h"          // t805 船上岸回归探针（水/陆速比 + 同层湿沙挡停 + 冰面豁免保留）

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
    QCoreApplication app(argc, argv);

    World w;
    w.setWidth(96);
    w.setDepth(96);
    w.setHeight(48); // 3 次 setter 各 regenerate 一次（几秒内）；生成快

    // rig 寻址（2D 网格防越界——首版 x 单排递增在 x>48 后 setBlock 全被越界拒绝 = 假 FAIL）：x 列距 22
    //   （容纳 16 粉 + 源 + 接收器的最长探针 18 格）、z 行距 3；96×96 → 4 列 × ~31 行 = 124 rig 位。
    int slotIdx = 0;
    const auto nextSlot = [&]() {
        const int col = slotIdx % 4, row = slotIdx / 4;
        ++slotIdx;
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
            for (int t = 0; t < 400 && okB; ++t) {
                carts.tickRiddenCart(0.016, &w, 0.998f, 0.0625f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (!onTrack(cp, t, "relaunch ")) { okB = false; break; }
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
    //   (a) 五形态（立地 + 四向墙插）每 quad 底边中点 == 柄根 B、顶边中点 == B+轴×0.8 —— 贴图中央火把列
    //       （u=0.5 处）钉在两片共同火把轴上（t776 修前 S 片中点离轴 0.225 → FAIL）；
    //   (b) 墙插两片宽度恰 {0.8 整瓦采样, 0.2 子区采样} 各一：0.2 片顶点 u 归一落 [0.375,0.625]（焰头
    //       4px 列区）、0.8 片 u 归一铺满 [0,1] → 两片 texel 密度一致（0.8/1.0 == 0.2/0.25）；
    //   (c) 杆向/亮端：底边（贴图底=柄端，v=0）恒 y=0.197 且离墙最近、顶边中点沿轴伸离墙（四向各验
    //       点积符号）+ 上倾 0.866×0.8；立地态底边 y=0、顶边 y=1、中点 (0.5,·,0.5)（亮端朝上）；
    //   (d) 熄灭位（RedstoneTorchStateOffFlag）几何不变、瓦片换 170（暗红熄焰）：u 全落 tile 170 区。
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
            int wPlanes = 0, ribbons = 0;
            for (int g = 0; g + 3 < verts.size(); g += 4) {
                const Vtx &p0 = verts[g], &p1 = verts[g + 1], &p2 = verts[g + 2], &p3 = verts[g + 3];
                const float mbx = (p0.x + p1.x) / 2, mbz = (p0.z + p1.z) / 2; // 底边中点（= 贴图 u=0.5 火把列）
                const float mtx = (p2.x + p3.x) / 2, mtz = (p2.z + p3.z) / 2;
                // (a) 火把列钉共同轴：底/顶边中点 == 柄根 / 轴端（W 片与 S 带中点重合 = 两片剪影重合）。
                if (std::fabs(mbx - bx) > 1e-4f || std::fabs(p0.y - kTBaseY) > 1e-4f
                    || std::fabs(mbz - bz) > 1e-4f
                    || std::fabs(mtx - (bx + axx)) > 1e-4f || std::fabs(p2.y - (kTBaseY + ayy)) > 1e-4f
                    || std::fabs(mtz - (bz + azz)) > 1e-4f) {
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "off torch axis: base mid" << mbx << p0.y << mbz
                                      << "top mid" << mtx << p2.y << mtz;
                    ok = false;
                    continue;
                }
                // (c) 亮端离墙：底边 v=0（贴图底=柄端）且顶边中点比柄根伸离支撑（away·Δ>0）。
                const float away = -(ax * (mtx - mbx) + az * (mtz - mbz));
                if (p0.v > 1e-4f || p1.v > 1e-4f || p2.v < 1.0f - 1e-4f || away <= 0.0f
                    || std::fabs((p2.y - p0.y) - ayy) > 1e-4f) {
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "bright-end dir wrong: v0" << p0.v << "v2" << p2.v
                                      << "away" << away;
                    ok = false;
                    continue;
                }
                // (b) 两片各一：0.8 整瓦（u 归一铺满 [0,1]）/ 0.2 子区带（u 归一 [0.375,0.625]）。
                const float wdt = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y)
                                           + (p1.z - p0.z) * (p1.z - p0.z));
                const float uu0 = (p0.u - onTile * tileW) / tileW, uu1 = (p1.u - onTile * tileW) / tileW;
                const bool fullTile = std::fabs(wdt - 0.8f) < 1e-4f
                                      && std::fabs(uu0) < 1e-4f && std::fabs(uu1 - 1.0f) < 1e-4f;
                const bool ribbon = std::fabs(wdt - 0.2f) < 1e-4f
                                    && std::fabs(uu0 - 0.375f) < 1e-4f && std::fabs(uu1 - 0.625f) < 1e-4f;
                if (fullTile && !ribbon) ++wPlanes;
                else if (ribbon && !fullTile) ++ribbons;
                else {
                    qInfo().noquote() << "  wall form" << form << "quad" << g / 4
                                      << "width/uv-region wrong: w" << wdt << "uu" << uu0 << uu1;
                    ok = false;
                }
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
                          << "| t776 wall redstone torch texture alignment: all 5 attach forms pin "
                             "mid-edge torch column onto shared torch axis (base/top midpoints); wall "
                             "W-plane 0.8 full-tile + S-ribbon 0.2 sub-region [0.375,0.625] same texel "
                             "density; bright end away from wall (4 dirs signed) / up on floor; off "
                             "flag swaps tile 170 with same geometry";
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

    // ── P21 t804 点燃交互扩展探针（① 木墙点燃蔓延烧毁链 / ② Stalker 打火石短引信引爆 / ③ item 入火焚毁）──
    //   用户报告（R19.12）：「打火石对着木头制品右键点燃 + 蔓延」「打火石对苦力怕右键引爆」「往火里丢
    //   物品被烧掉（参考岩浆）」。三段断言（任一 FAIL = 用户症状在当前 HEAD 的复现点）：
    //   (a) 木墙蔓延烧毁：石台上 6 连木板墙 + 端点火格 → 驱动 tickFire（每 5 调 = 1 判定窗，0.5s/窗）600 窗
    //       （300s，逐邻独立 5%/窗 → 每块期望 ~10s，链式 ~60-120s + 余量）→ 全部木板被吞（blockAt 全非
    //       Planks）、火最终无燃料自熄（全 Air）；烧毁走 setBlock(Fire) 放置语义 → blockBroken(Planks)
    //       恒 0（烧毁无掉落，区别于破块链）；
    //   (b) Stalker 打火石引爆：远场监听（>> kDetectRange 不追踪）+ playerTargetable=false（旧 !targetable
    //       门会清 fuseTimer 并跳过 aiStalker——本断言兼证 t804 的门豁免）→ igniteStalkerFlint 返 true；
    //       猪（非 Stalker）同调用返 false（类型拒）；点燃后原地 ~1.5s 引爆（爆炸恰一次、引爆时刻 ∈
    //       [1.4, 2.3]s、期间 inflateAt 曾 >0.2 = 蓄力膨胀可见）；
    //   (c) item 入火焚毁计时：item 直落 Fire 格（t804 重力列扫 / resting 复探豁免 Fire——旧版 isSolid
    //       非 air 实存语义使 item 骑在火格顶面永不进格 = 用户「往火里丢东西烧不掉」的复现点）→ ~30 tick
    //       落地 + 0.8s（50 tick）点燃窗后焚毁（总 [65,120] tick）+ itemBurned 恰一次且坐标在火格列；
    //       对照 Lava 格内生成瞬毁（t343 首 tick 即毁 ≤3，无点燃窗，两者语义刻意不同）；抢救窗：入火
    //       55 tick（<窗）后拆火 → item 存活且其后 200 tick 不焚毁（出火熄火 fireBurn 清 0）。
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

        // (a) 木墙点燃蔓延烧毁链：石台 dx 0..7，木板墙 dx 1..6（ty 层），火 dx 0（贴首块木板）。
        //   blockBroken 计数只滤 Planks（火自熄 Fire→Air 也发 blockBroken 但 oldId==Fire，排除）。
        int plankBreaks = 0;
        QObject::connect(&w, &World::blockBroken, &w,
                         [&](int, int, int, int oldId) { if (oldId == int(BR::Planks)) ++plankBreaks; });
        for (int dx = 0; dx <= 7; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        for (int dx = 1; dx <= 6; ++dx) w.setBlock(x0 + dx, ty, z0, BR::Planks, 0);
        w.setBlock(x0, ty, z0, BR::Fire, 0);
        for (int win = 0; win < 600; ++win)
            for (int k = 0; k < 5; ++k) w.tickFire(); // 5 调 = 1 判定窗（kFireTickInterval）
        int planksLeft = 0, firesLeft = 0;
        for (int dx = 0; dx <= 7; ++dx) {
            const quint8 b = w.blockAt(x0 + dx, ty, z0);
            if (b == BR::Planks) ++planksLeft;
            if (b == BR::Fire) ++firesLeft;
        }
        const bool okA = planksLeft == 0 && firesLeft == 0 && plankBreaks == 0;
        // 清 (a) 场（石台 + 残火/灰烬；正常应为全 Air，仍防御性清）。
        for (int dx = 0; dx <= 7; ++dx) {
            w.setBlock(x0 + dx, ty - 1, z0, BR::Air, 0);
            w.setBlock(x0 + dx, ty, z0, BR::Air, 0);
        }
        tickN(w, 2);

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

        // (c) item 入火焚毁计时：火 dx 10 / 岩浆 dx 12（各石台支撑）；item 从 ty+3 直落（spawnItemAt 零初速）。
        ItemEntityManager items;
        int burnSignals = 0;
        float burnX = -1.0f, burnY = -1.0f, burnZ = -1.0f;
        QObject::connect(&items, &ItemEntityManager::itemBurned, &items,
                         [&](qreal x, qreal y, qreal z) {
                             ++burnSignals; burnX = float(x); burnY = float(y); burnZ = float(z);
                         });
        const int fx = x0 + 10, lx = x0 + 12;
        for (int dx = 10; dx <= 12; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Stone, 0);
        w.setBlock(fx, ty, z0, BR::Fire, 0);
        // (c1) 火焚毁：~30 tick 落地 + 50 tick 点燃窗 → 总 [65,120]；itemBurned 恰一次 + 坐标在火格列。
        items.spawnItemAt(QVector3D(float(fx) + 0.5f, float(ty + 3) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itFire = items.count() - 1;
        int burnTick = -1;
        for (int t = 1; t <= 400; ++t) {
            items.tick(0.016, &w);
            if (!items.aliveAt(itFire)) { burnTick = t; break; }
        }
        // (c2) 岩浆瞬毁对照：直接生成于岩浆格内（t343 消费路径 = 中心格 == Lava 即毁；直落会先停在
        //     岩浆面顶——岩浆非穿透语义，不在本任务范围）→ 首 tick 即毁（无点燃窗，与火焚语义对照）。
        w.setBlock(lx, ty, z0, BR::Lava, 0);
        items.spawnItemAt(QVector3D(float(lx) + 0.5f, float(ty) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itLava = items.count() - 1;
        int lavaTick = -1;
        for (int t = 1; t <= 200; ++t) {
            items.tick(0.016, &w);
            if (!items.aliveAt(itLava)) { lavaTick = t; break; }
        }
        // (c3) 抢救窗：新 item 入火 55 tick（落地 ~31 + 燃 0.38s < 0.8s 窗）→ 拆火 → 存活且其后 200 tick 不毁。
        items.spawnItemAt(QVector3D(float(fx) + 0.5f, float(ty + 3) + 0.5f, float(z0) + 0.5f),
                          int(BR::Planks), 1, 0.0f, 0.0f, 0.0f);
        const int itRescue = items.count() - 1;
        for (int t = 0; t < 55; ++t) items.tick(0.016, &w);
        w.setBlock(fx, ty, z0, BR::Air, 0);
        bool rescued = items.aliveAt(itRescue);
        for (int t = 0; t < 200 && rescued; ++t) {
            items.tick(0.016, &w);
            rescued = items.aliveAt(itRescue);
        }
        const bool okC = rigOk && burnTick >= 65 && burnTick <= 120
                         && burnSignals == 1
                         && int(burnX) == fx && int(burnY) == ty && int(burnZ) == z0
                         && lavaTick >= 1 && lavaTick <= 3 && lavaTick < burnTick - 15
                         && rescued;
        // 清 (c) 场（石台 + 岩浆；岩浆不驱动 tickLavaFlow 不蔓延，直接清）。
        w.setBlock(lx, ty, z0, BR::Air, 0);
        for (int dx = 10; dx <= 12; ++dx) w.setBlock(x0 + dx, ty - 1, z0, BR::Air, 0);
        tickN(w, 2);

        const bool ok = rigOk && okA && okB && okC;
        if (!ok) {
            qInfo().noquote() << "  [t804 diag] rigOk" << rigOk << "| okA" << okA
                              << "planksLeft" << planksLeft << "firesLeft" << firesLeft
                              << "plankBreaks" << plankBreaks
                              << "| okB" << okB << "ignOk" << ignOk << "explodeTick" << explodeTick
                              << "fuseSec" << QString::number(fuseSec, 'f', 2)
                              << "inflateSeen" << inflateSeen << "explosions" << explosions
                              << "| okC" << okC << "burnTick" << burnTick << "lavaTick" << lavaTick
                              << "burnSignals" << burnSignals << "burnPos" << burnX << burnY << burnZ
                              << "rescued" << rescued;
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t804 flint ignition extended: fire next to 6-plank wall burns all planks "
                             "away (no blockBroken-drop chain) and self-extinguishes; flint on stalker "
                             "detonates in-place ~1.5s uncancellable fuse (pig rejected, !targetable gate "
                             "exempt, exactly one explosion, inflate visible); item dropped into fire "
                             "burns after ~0.8s window (itemBurned smoke once at fire cell) vs lava "
                             "instant destroy, item rescued within window survives after fire removed";
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
            //   云杉板+羊毛方块床形→白床（MC 任意木板语义的族外覆盖）。
            const int gBed[9] = { SP, int(RecipeRegistry::WoolId), 0, 0 };
            expectCraft(gBed, 2, int(BR::BedRed), 1, "spruce_planks+wool->bed_red");
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

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    return totalFail == 0 ? 0 : 1;
}
