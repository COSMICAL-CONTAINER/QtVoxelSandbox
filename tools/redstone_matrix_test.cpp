// t740 红石激活矩阵冒烟测试（无 GUI）：直接实例化 World（Core+World 两层，无 QML 场景），
//   按「信号源 × 接收器」全组合搭最小电路 rig → 手动驱动 tickRedstone（等价 WorldClock 10Hz 桥接）
//   → 核对每个组合的通电结果（TNT/发射器族 = 信号计数；灯/轨/门/活板门 = state 通电位）+ 降沿复查。
//   动机（t740）：用户实测「红石火把 / 红石粉不能激活 TNT」而静态核对链路完整 —— 本测试把整条
//   World 层电力链（setBlock→notePowerWrite→m_powerDirty→tickRedstone→recomputePowerLocal→信号/state）
//   在真实对象上跑通，断点直接暴露为 FAIL 行；同时产出「源×接收器」矩阵核对表（commit message 引用）。
//   分层（PLAN §2）：仅 Core+World，不依赖 Game/Entities/QML —— 消费端（firePowerTnt 等）不在本测试
//   范围（呈现层桥接由 Main.qml 静态核对）。运行：build/redstone_matrix_test.exe，全过 exit 0。
#include <QCoreApplication>
#include <QDebug>

#include "blockregistry.h"
#include "world.h"

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

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    return totalFail == 0 ? 0 : 1;
}
