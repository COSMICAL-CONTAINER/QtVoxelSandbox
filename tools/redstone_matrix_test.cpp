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

#include "blockregistry.h"
#include "toolregistry.h" // t762 黑曜石挖掘规则探针（miningTime / canHarvest / miningSpeedMul 纯表查询）
#include "hotbar.h"       // t763 附魔数值生效链探针（EPF 路由 / 耐久消耗概率 / 攻击伤 tooltip 源）
#include "playerstate.h"  // t755 死亡态硬锁探针（致死落库 0 / heal 死亡免疫 / respawn 复位链）
#include "world.h"
#include "partialblockgeometry.h" // t737 拐角象限断言（mesher 同源调用）
#include "minecartmanager.h"      // t737 环线矿车绕圈断言（骑乘 / 空车两路）
#include "entitymanager.h"        // 审查 #1 末影眼巡航高度回归探针（spawnEnderEye + enderEyeCruiseYAt）

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
        const float rideH = 0.225f; // kCartRideH（MinecartManager 私有常量的文档值：轨格 cell 底 + 1/16 板 + 车底偏移）
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

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    return totalFail == 0 ? 0 : 1;
}
