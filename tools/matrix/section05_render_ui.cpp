// tools/matrix/section05_render_ui.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L24790-30840 逐字节搬移（段 md5: 43ff08341d96fe706994361e1b2988c6；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section05_render_ui()
{

    // ── P-t941 矿车 3D 侧贴图四向统一源码钉（纯视觉修；t931 源码文本钉先例）──
    //   用户第五轮实测：「侧边是石头贴图（错）——前后左右统一贴图（用前后贴图即可）」。根因 =
    //   MinecartBox kPackParts（demo 包布局 1）纵帮 piece 1/2 大面分采壁带亮/暗窗（t862② 外亮/内暗），
    //   PIL 实测壁带内区平灰噪点（lum≈74 sd≈6 无结构）= 石头纹理观感；端帮 piece 3 大面 = 框栏端面窗。
    //   修 = piece 1/2 六面与 piece 3 同套采样（大面 ±X = 端面窗 (0,2)-(20,10) = 前后贴图，条带 = 端帮
    //   同款内壁行）→ 前后左右四面墙同一贴图，壁带整条不再被引用（t862② 内外明暗随统一口径废止，
    //   t931 同款用户最新口径翻案先例）。qrc 程序布局 0 不动（壁窗族有铆钉列结构、前后本就同族 =
    //   正锚）。钉：(a) 统一纵帮行 ×2 + (b) 端帮参照行 ×1 + (c) 旧壁窗坐标串清零 + (d) 布局 0 两行
    //   不变 + (e) Main.qml 五个车斗 Model 同一 baseColorMap 绑定 ×5。
    {
        const QString exeDir941 = QCoreApplication::applicationDirPath();
        const QString root941 = QDir(exeDir941 + QStringLiteral("/..")).absolutePath();
        auto readSrc941 = [&root941](const QString &rel) -> QString {
            QFile f(root941 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mb = readSrc941(QStringLiteral("src/Renderer/minecartbox.cpp"));
        const QString mq = readSrc941(QStringLiteral("src/ui/Main.qml"));
        const QString kSide941 = QStringLiteral(
            "    PartRects{{ { 0, 2,20,10}, { 0, 2,20,10}, {20,2,36, 4}, {20,8,36,10}, {20,2,24,10}, {20,2,24,10} }},");
        const bool okA = mb.count(kSide941) == 2; // piece 1/2 同行 ×2（四向统一）
        const bool okB = mb.contains(QStringLiteral(
            "    PartRects{{ {20, 2,24,10}, {20, 2,24,10}, {20, 2,36, 4}, {20, 8,36,10}, { 0, 2,20,10}, { 0, 2,20,10} }},")); // piece 3 参照不变
        const bool okC = !mb.contains(QStringLiteral("2,10,22,28"))
                      && !mb.contains(QStringLiteral("24,10,44,28")); // 旧壁带亮/暗窗坐标清零
        const bool okD = mb.contains(QStringLiteral(
            "    PartRects{{ {0, 4,20,20}, {0, 4,20,20}, {0, 4,20, 6}, {0,18,20,20}, {0, 6, 4,20}, {0, 6, 4,20} }},"))
                      && mb.contains(QStringLiteral(
            "    PartRects{{ {24, 4,44,20}, {24, 4,44,20}, {24, 4,44, 6}, {24,18,44,20}, {20, 6,24,20}, {20, 6,24,20} }},")); // 布局 0 正锚不变
        const bool okE = mq.count(QStringLiteral(
            "baseColorMap: cartPackHit ? cartPackTex : cartTex")) == 5; // 五车斗 Model 同一贴图绑定
        const bool okT941 = okA && okB && okC && okD && okE;
        if (!okT941) ++totalFail;
        if (!okT941)
            qInfo().noquote() << "  [t941 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "e" << okE << "| srcLen box" << mb.size() << "qml" << mq.size();
        qInfo().noquote() << (okT941 ? "PASS" : "FAIL")
                          << "| t941 minecart 3D side walls unified to the front/back texture: the demo-pack"
                             " layout side pieces (MinecartBox kPackParts 1/2) sampled the wall-band"
                             " bright/dark windows for their large faces (t862② outer-bright/inner-dark split)"
                             " - PIL re-measure shows that band is flat gray noise (lum~74, sd~6, no"
                             " structure) reading as the stone texture per the user's round-5 report; fix"
                             " makes pieces 1/2 sample the exact same rect set as the end piece 3 (large"
                             " faces = the framed end-face window (0,2)-(20,10) = the front/back texture,"
                             " rim strips = the end piece's light inner-wall rows), so all four walls"
                             "(front/back/left/right) share one texture and the wall band is no longer"
                             " referenced by any piece (the t862② shading distinction is overruled by the"
                             " user's unify caliber, t931-style latest-word precedent); qrc program layout 0"
                             " untouched (its wall windows carry rivet-column structure and are already the"
                             " same family front/back - pinned unchanged as the positive anchor). Source"
                             " pins: unified side rect line x2, end-piece reference line, old wall-band"
                             " window coords absent, layout-0 lines intact, and all five cart Model"
                             " materials bound to the same baseColorMap in Main.qml"
                          ;
    }

    // ── P-t942 爆炸毁能量源后动力轨激活残留探针（World 直编 destroySphereSilent；spec「TNT/苦力怕炸掉
    //    红石块/火把后部分动力轨仍激活」）──
    //   根因（HEAD=4ce51b3 实测定位）：t683 已在 destroySphereSilent 逐破坏格补 notePowerWrite、t936 已给
    //   「轨编辑」挂链走查，但**源 / 粉编辑格不走链**——炸掉红石块 / 火把后只有编辑格 + 其 6 正交邻（种子
    //   轨）入评估域，种子轨翻转后靠「翻转波前」逐 tick 把链尾拉进脏集（8 根链实测 5 tick 仍部分亮 = 用户
    //   所见激活残留窗；粉传形态源—粉—轨—链更长）。修法（t942 ②/③）：轨 / 电源 / 粉编辑格统一经
    //   dirtyGoldenRailChainFrom（t936 走查提取的单源 helper，链几何判定仍 goldenRailChainStep 一套）整链
    //   入脏集 + Phase A2 粉电平翻转回插同走查 → 爆炸批量补 note 的每格都把被毁能量源喂着的整链下一 tick
    //   一次 pass 全灭。腿：
    //   (a) 主腿：红石块直供平链 ×8 → r0.9 爆心钉源（只毁源）→ **1 tick 内**链全灭（旧版 t1 仍 7 亮）；
    //   (b) 火把形态同腿（Torch 源，1 tick 全灭）；
    //   (c) t936 破坏对称经爆炸路径：爆心钉中段轨（r0.9 只毁该轨）→ 源侧 3 根保持亮、远翼 4 根灭；
    //   (d) t937 收窄不回退（经爆炸路径）：settled 后爆掉远离轨 / 源 / 粉的孤石 → powerRecomputePasses
    //       计数不动 + 链保持 8/8（普通方块编辑零红石重算口径在 destroySphereSilent 批量路径同样成立）；
    //   (e) 坡链形态（t910 几何）：上坡链爆源 → 1 tick 全灭（三高探针从源位发现 ±1 层种子轨）；
    //   (f) 源码钉：helper 声明 + 定义恰一处（禁第二套链判定）、t942 ② 触发行（源 / 粉扩位）、Phase A2
    //       走查行、destroySphereSilent 链尾 notePowerWrite 挂点（两爆炸入口共用）。
    {
        const auto scanRig942 = [&](int dxLo, int dxHi, int dyLo, int dyHi) {
            int rx = -1, rz = -1;
            for (int zz = 3; zz < 94 && rx < 0; zz += 2)
                for (int xx = 4; xx + dxHi < 96 && rx < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = dxLo; dx <= dxHi && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = dyLo; dy <= dyHi && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { rx = xx; rz = zz; }
                }
            return QPair<int, int>(rx, rz);
        };
        const auto railOn942 = [&](int x, int y, int z) {
            return (w.stateAt(x, y, z) & BR::GoldenRailStateOnFlag) != 0;
        };
        // (a) 主腿 + (b) 火把腿：源（RedstoneBlock / RedstoneTorch）直供平链 ×8 → 爆心钉源 r0.9（只毁源
        //     格——邻轨距 1 > 0.81 幸存、支撑板距 1 幸存）→ 1 tick 全灭。
        bool okA = false, okB = false;
        for (int leg = 0; leg < 2; ++leg) {
            const quint8 srcId = (leg == 0) ? quint8(BR::RedstoneBlock) : quint8(BR::RedstoneTorch);
            const auto [x0, z0] = scanRig942(-2, 8, -2, 1);
            if (x0 < 0) {
                qInfo().noquote() << "  [t942 diag] leg" << leg << "no clear rig area found";
                break;
            }
            for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0); // 支撑板（防 t733 失撑掉落噪声）
            w.setBlock(x0 - 1, kRigY, z0, srcId, 0);
            for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::GoldenRail, 0);
            tickN(w, 8);
            int lit0 = 0;
            for (int i = 0; i < 8; ++i) lit0 += railOn942(x0 + i, kRigY, z0);
            const auto dv = w.destroySphereSilent(x0 - 1, kRigY, z0, 0.9f); // TNT/Stalker 爆炸的 World 层本体（两入口共用）
            bool srcGone = w.blockAt(x0 - 1, kRigY, z0) == quint8(BR::Air);
            int railsLeft = 0;
            for (int i = 0; i < 8; ++i) railsLeft += w.blockAt(x0 + i, kRigY, z0) == quint8(BR::GoldenRail);
            tickN(w, 1); // 「下一 tick 内」——一次 pass 全灭
            int lit1 = 0;
            for (int i = 0; i < 8; ++i) lit1 += railOn942(x0 + i, kRigY, z0);
            const bool ok = lit0 == 8 && int(dv.size()) == 1 && srcGone && railsLeft == 8 && lit1 == 0;
            if (leg == 0) okA = ok; else okB = ok;
            if (!ok)
                qInfo().noquote() << "  [t942 diag] leg" << leg << "lit0" << lit0 << "dv" << int(dv.size())
                                  << "srcGone" << srcGone << "railsLeft" << railsLeft << "lit1" << lit1;
            for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
        // (c) t936 破坏对称经爆炸路径：爆心钉中段 R4（r0.9 只毁该轨）→ 源侧 R1..R3 亮、远翼 R5..R8 灭。
        bool okC = false;
        {
            const auto [x0, z0] = scanRig942(-2, 8, -2, 1);
            if (x0 < 0) {
                qInfo().noquote() << "  [t942 diag] leg C: no clear rig area found";
            } else {
                for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::GoldenRail, 0);
                tickN(w, 8);
                int lit0 = 0;
                for (int i = 0; i < 8; ++i) lit0 += railOn942(x0 + i, kRigY, z0);
                const auto dv = w.destroySphereSilent(x0 + 3, kRigY, z0, 0.9f); // 中段轨格（i=3）
                tickN(w, 2);
                int nearLit = 0, farDark = 0;
                for (int i = 0; i <= 2; ++i) nearLit += railOn942(x0 + i, kRigY, z0);
                for (int i = 4; i <= 7; ++i) farDark += !railOn942(x0 + i, kRigY, z0);
                okC = lit0 == 8 && int(dv.size()) == 1 && nearLit == 3 && farDark == 4;
                if (!okC)
                    qInfo().noquote() << "  [t942 diag] c lit0" << lit0 << "dv" << int(dv.size())
                                      << "nearLit" << nearLit << "farDark" << farDark;
                for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (d) t937 收窄不回退（爆炸路径）：settled 亮链 → 快照计数 → 爆掉远离 rig 的孤石（6 邻全 Air——
        //     非粉 ∪ 电源）→ 计数不动 + 链保持 8/8（destroySphereSilent 的逐格 notePowerWrite 走快路径早退）。
        bool okD = false;
        {
            const auto [x0, z0] = scanRig942(-7, 8, -2, 1);
            if (x0 < 0) {
                qInfo().noquote() << "  [t942 diag] leg D: no clear rig area found";
            } else {
                for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::GoldenRail, 0);
                tickN(w, 8);
                tickN(w, 2); // 沉降（波前脏集清空 → 稳态脏集空）
                const int c0 = w.powerRecomputePasses();
                w.setBlock(x0 - 5, kRigY, z0, BR::Stone, 0); // 孤石（距源 4 / 距最近轨 5；6 邻全 Air）
                const auto dv = w.destroySphereSilent(x0 - 5, kRigY, z0, 0.9f); // 炸孤石（与轨 / 源 / 粉无关的普通方块）
                tickN(w, 3);
                const int c1 = w.powerRecomputePasses();
                int litAfter = 0;
                for (int i = 0; i < 8; ++i) litAfter += railOn942(x0 + i, kRigY, z0);
                okD = c1 == c0 && litAfter == 8 && int(dv.size()) == 1;
                if (!okD)
                    qInfo().noquote() << "  [t942 diag] d passes" << c0 << "->" << c1
                                      << "litAfter" << litAfter << "dv" << int(dv.size());
                w.setBlock(x0 - 5, kRigY, z0, BR::Air, 0);
                for (int i = -1; i <= 7; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                for (int i = 0; i < 8; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (e) 坡链形态（t910 几何）：上坡链 i=0..6（每轨 +1）源贴种子 → 爆源 → 1 tick 全灭。
        bool okE = false;
        {
            const auto [x0, z0] = scanRig942(-2, 7, -2, 8);
            if (x0 < 0) {
                qInfo().noquote() << "  [t942 diag] leg E: no clear rig area found";
            } else {
                for (int i = -1; i <= 6; ++i) w.setBlock(x0 + i, kRigY - 1 + i, z0, BR::Stone, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                for (int i = 0; i <= 6; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0);
                tickN(w, 8);
                int lit0 = 0;
                for (int i = 0; i <= 6; ++i) lit0 += railOn942(x0 + i, kRigY + i, z0);
                const auto dv = w.destroySphereSilent(x0 - 1, kRigY, z0, 0.9f);
                tickN(w, 1);
                int lit1 = 0;
                for (int i = 0; i <= 6; ++i) lit1 += railOn942(x0 + i, kRigY + i, z0);
                okE = lit0 == 7 && int(dv.size()) == 1 && lit1 == 0;
                if (!okE)
                    qInfo().noquote() << "  [t942 diag] e lit0" << lit0 << "dv" << int(dv.size())
                                      << "lit1" << lit1;
                for (int i = -1; i <= 6; ++i) w.setBlock(x0 + i, kRigY - 1 + i, z0, BR::Air, 0);
                for (int i = 0; i <= 6; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (f) 源码钉（t935/t936 模式：源文件直读字符串钉——helper / 触发行 / 链尾挂点 / Phase A2 走查消失即红）。
        const QString exeDir942 = QCoreApplication::applicationDirPath();
        const QString root942 = QDir(exeDir942 + QStringLiteral("/..")).absolutePath();
        auto readSrc942 = [&root942](const QString &rel) -> QString {
            QFile f(root942 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString wh942 = readSrc942(QStringLiteral("src/World/world.h"));
        const QString wc942 = readSrc942(QStringLiteral("src/World/world.cpp"));
        const bool okF1 = wh942.contains(QStringLiteral("void dirtyGoldenRailChainFrom(int x, int y, int z);"))
                       && wc942.count(QStringLiteral("void World::dirtyGoldenRailChainFrom(int x, int y, int z)")) == 1;
        const bool okF2 = wc942.contains(QStringLiteral(
                              "|| isPowerEmitterBlock(oldId) || isPowerEmitterBlock(newId)"))
                       && wc942.contains(QStringLiteral(
                              "|| BlockRegistry::isRedstoneDust(oldId) || BlockRegistry::isRedstoneDust(newId)"));
        const bool okF3 = wc942.contains(QStringLiteral("t942 ③ 粉电平翻转"))
                       && wc942.count(QStringLiteral("dirtyGoldenRailChainFrom(x, y, z);")) == 2;
        const bool okF4 = wc942.contains(QStringLiteral(
                              "notePowerWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);"))
                       && wc942.contains(QStringLiteral("t942：本口即爆炸链尾的红石重扫挂点"));
        const bool okF5 = wc942.contains(QStringLiteral("t936 动力轨放置 / 破坏沿重算")); // t936 正锚（注释块保留）
        const bool okT942 = okA && okB && okC && okD && okE && okF1 && okF2 && okF3 && okF4 && okF5;
        if (!okT942) ++totalFail;
        if (!okT942)
            qInfo().noquote() << "  [t942 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "e" << okE << "| f" << okF1 << okF2 << okF3 << okF4 << okF5
                              << "| srcLen h" << wh942.size() << "c" << wc942.size();
        qInfo().noquote() << (okT942 ? "PASS" : "FAIL")
                          << "| t942 explosion destroying a power source leaves no stale charge on"
                             " powered-rail chains: notePowerWrite only chain-walked RAIL edits"
                             " (t936), so when a blast (destroySphereSilent's per-voxel"
                             " notePowerWrite tail, shared by the TNT and stalker detonation"
                             " entries) destroyed the redstone block or torch FEEDING a chain,"
                             " only the edit cell and its 6-orthogonal neighbors entered the"
                             " recompute domain - the seed rail flipped dark but the rest of the"
                             " chain was only pulled in stepwise by the flip wavefront (8-rail"
                             " chain measured 5 ticks still partly lit = the user's 'some powered"
                             " rails stay activated' window; dust-relay layouts lingered longer)"
                             " - while placement/break edits through setBlock had the same"
                             " stepwise falling edge. Fix keeps ONE chain-walk authority"
                             " (dirtyGoldenRailChainFrom, extracted from the t936 block, still"
                             " stepping via goldenRailChainStep only) and fires it for rail,"
                             " power-EMITTER and dust edits alike (edit cell is not a rail so the"
                             " 3-height probe finds the seed it feeds with no connection-bit"
                             " gate), plus the Phase A2 dust-power-change reinsert walks the"
                             " same helper so surviving-dust-relayed chains converge in one pass"
                             " too - the whole chain fed by a destroyed source goes dark in the"
                             " NEXT single tick, and the bright edge keeps its one-pass semantics"
                             " (t937 goldenPowered merge); the t937 fast-path narrowing is"
                             " untouched (plain-block edits still zero-recompute through the"
                             " explosion path). Probe legs: (a) redstone-block-fed flat chain of"
                             " 8, blast pinned on the source only -> all dark within 1 tick (was"
                             " 7-lit at t1), (b) same with a redstone torch source, (c) blast"
                             " pinned on a mid-chain rail -> fed side stays lit 3, far wing dark"
                             " 4 (t936 destruction symmetry via the explosion path), (d) blasting"
                             " an unrelated lone stone -> powerRecomputePasses counter flat and"
                             " chain stays 8/8 lit (narrowing not regressed), (e) climbing chain"
                             " blast-the-source -> 1-tick full dark, (f) source pins: helper"
                             " declared once + defined exactly once, the emitter/dust trigger"
                             " lines, the Phase-A2 walk line pair, the destroySphereSilent tail"
                             " notePowerWrite hook comment, and the preserved t936 comment anchor"
                          ;
    }

    // ── P-t943 V 字载人变慢 + 多车卡出探针（MinecartManager 直编；spec「① 载人后速度变慢、最高点速度
    //    正转负时特别慢（往返换向阻尼过大？）；② 多矿车丝滑运动有概率卡出 V 字到隔壁 / 横着卡在坡上
    //    （非 45° 状态）/ 挤压颤抖卡死——t907/t909 去穿插与坡向参数返修」）──
    //   ① 根因分账：被骑路径旧版无输入走 targetV lerp —— 上坡只剩 kCartFriction(2/s) 指数衰减（渐近
    //      零、顶点前长时间 0.x b/s 爬行 = 「最高点速度正转负时特别慢」），下坡靠 slopeDownAuto 抬
    //      targetV 再 kCartAccel(3/s) 缓起；空车路径（t909③）是 kCartSlopeGravity(19.8/s²) 沿轨重力
    //      直接积分 —— 同一 V 空车丝滑、载人爬行 = 物理口径劈叉。修 = 无输入且非动力段改走空车同一套
    //      坡道积分（tickRiddenCart coasting 分支），载人曲线与空车同物理。
    //   ② 根因三面收口：clampShift 近层闸（review28 #7 |Δ层|≤1）只验「目标列有轨」不验「本链延续」→
    //      跨链立体同列的下线 / 桥下线轨被当坡面延续 = 跨链跳线（「卡出 V 字到隔壁」）→ 补链可达闸
    //      （连接位 + railProbeDelta 层差同一权威）；冲量对撞反向弹开 + 坡面 t909②/t863① kick 回灌 =
    //      弹开-回灌极限环（「挤压颤抖卡死」）→ 持续挤压对速度一致性 + 钳向清速（钳边车不得持指向
    //      钳制边界的速度 —— 段内位移无跨格校验，破之则推过格界坠轨，t907(a) 回归实证）；解析收尾
    //      cartYawFromDir 重钉（姿态恒沿轨轴，「横着卡在坡上」呈现面收口）。
    //   rig：V 形 **5 格臂**（重力捕获阈 sqrt(2·19.8·5)=14.1 > 冲量钳 12.8 → 闭合系统，任何碰撞获速车
    //      必被臂重力捕获，无山顶逃逸面）+ V 底两格通电动力轨（t909 同款）。腿：
    //      (a) 空车参照：东臂半山 spawn，1500 tick 往返 —— 反转 ≥6、臂上爬行 tick（|vApp|<0.5）≤90、
    //          含留、Y 平滑、存活（= 修后载人须对齐的「同物理」基准，兼空车零回归守卫）；
    //      (b) 载人对照：同位 spawn + tryMount，无输入同驱 1500 tick —— 同一组阈值全绿（旧代码：无
    //          坡向起步 → 半山腰朝上坡向停死；或有速度时摩擦爬顶飞出死端 —— 反转不足 / 出 rig 必居其一）；
    //      (c) 多车长跑：4 空车（谷底 ×2 + 两臂半山）+ resolveCartCollisions 同帧 1500 tick —— 全程
    //          存活 / 恒贴轨线（|z−心|<0.02，横向逃逸零容忍）/ yaw 恒轴向（fmod 90° ±0.5，「横着」零
    //          容忍）/ 含留（x 与 y 双界）/ 末 400 tick 每车路程 ≥1.0（无冻结无颤抖死锁）；
    //      (d) 源码钉：coasting 闸 / 链可达闸两行 / 速度一致性 match / 钳向清速两行 / yaw 重钉。
    {
        // rig 选址：运行期扫描空区（t909 模式）。footprint x0-5..x0+6 × z0-1..z0+1 × kRigY-2..kRigY+6。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 8; xx + 6 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -5; dx <= 6 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 6 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        bool okA = false, okB = false, okC = false, okD = false;
        if (x0 < 0) {
            qInfo().noquote() << "  [t943 diag] no clear rig area found";
        } else {
            w.setBlock(x0, kRigY - 1, z0, BR::RedstoneBlock, 0);      // 直供源（兼支撑）
            w.setBlock(x0, kRigY, z0, BR::GoldenRail, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::GoldenRail, 0);
            for (int i = 1; i <= 5; ++i) {
                w.setBlock(x0 + 1 + i, kRigY + i, z0, BR::Rail, 0);   // 东臂（顶 x0+6@Y+5）
                w.setBlock(x0 - i, kRigY + i, z0, BR::Rail, 0);       // 西臂（顶 x0-5@Y+5）
            }
            tickN(w, 8); // 电力重算：直供 + 链传第二格
            const bool poweredOk = (w.stateAt(x0, kRigY, z0) & BR::GoldenRailStateOnFlag) != 0
                                && (w.stateAt(x0 + 1, kRigY, z0) & BR::GoldenRailStateOnFlag) != 0;
            // (a)/(b) 共用驱动：单空车 / 单被骑车在 V 里自由往返 1500 tick，采反转数 / 臂上爬行 tick /
            //   含留 / Y 平滑 / 存活。臂上判定 = 离谷心 >1.3 格；爬行 = 视速 <0.5（旧载人摩擦衰减在顶点
            //   前长时间 0.x b/s = 用户「特别慢」签名；重力物理 0.5→0 仅 ~2 tick）。
            const auto runOsc = [&](bool ridden, int &rev, int &crawl, bool &inRig,
                                    bool &ySmooth, bool &alive) {
                MinecartManager carts;
                carts.spawnCart(x0 + 3, kRigY + 2, z0, &w);           // 东臂半山（t909 同位）
                const QVector3D mountOrigin(float(x0 + 3) + 0.5f, float(kRigY + 2) + 2.0f,
                                            float(z0) + 0.5f);
                const bool mounted = !ridden || carts.tryMount(mountOrigin, QVector3D(0, -1, 0), 4.0f);
                QVector3D cp;
                int state = 0;
                float accum = 0.0f;
                float prevX = carts.posAt(0).x(), prevY = carts.posAt(0).y();
                rev = 0; crawl = 0; inRig = true; ySmooth = true;
                for (int t = 0; t < 1500; ++t) {
                    if (ridden) {
                        carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp); // 骑乘分支（镜像 PlayerController 序）
                        carts.tickPushedCarts(0.016, &w);                // 被骑车在其中被跳过
                    } else {
                        carts.tickPushedCarts(0.016, &w);
                    }
                    const int ri = ridden ? carts.ridingIndex() : 0;
                    const QVector3D p = carts.posAt(ri);
                    const float dX = p.x() - prevX;
                    accum += dX;
                    if (state == 0) {
                        if (accum > 0.3f) { state = 1; accum = 0.0f; }
                        else if (accum < -0.3f) { state = -1; accum = 0.0f; }
                    } else if (state > 0 && accum < -0.3f) { ++rev; state = -1; accum = 0.0f; }
                    else if (state < 0 && accum > 0.3f) { ++rev; state = 1; accum = 0.0f; }
                    if (std::fabs(p.x() - (float(x0 + 1) + 0.5f)) > 1.3f
                        && std::fabs(dX) / 0.016f < 0.5f) ++crawl;
                    if (p.x() < float(x0) - 5.4f || p.x() > float(x0) + 6.4f) inRig = false;
                    if (std::fabs(p.y() - prevY) > 0.35f) ySmooth = false;
                    prevX = p.x(); prevY = p.y();
                    alive = carts.aliveAt(ri);
                }
                return mounted && alive;
            };
            int revA = 0, crawlA = 0, revB = 0, crawlB = 0;
            bool inRigA = false, ySA = false, aliveA = false, inRigB = false, ySB = false, aliveB = false;
            const bool droveA = runOsc(false, revA, crawlA, inRigA, ySA, aliveA);
            const bool droveB = runOsc(true, revB, crawlB, inRigB, ySB, aliveB);
            okA = poweredOk && droveA && revA >= 6 && crawlA <= 90 && inRigA && ySA && aliveA;
            okB = poweredOk && droveB && revB >= 6 && crawlB <= 90 && inRigB && ySB && aliveB;
            if (!okA || !okB)
                qInfo().noquote() << "  [t943 diag] osc empty pow" << poweredOk << "drove" << droveA
                                  << "rev" << revA << "crawl" << crawlA << "inRig" << inRigA
                                  << "yS" << ySA << "alive" << aliveA
                                  << "| ridden drove" << droveB << "rev" << revB << "crawl" << crawlB
                                  << "inRig" << inRigB << "yS" << ySB << "alive" << aliveB;
            // ── (c) 多车长跑：4 空车（谷底 ×2 + 两臂半山）+ 碰撞解析同帧 1500 tick。──
            {
                MinecartManager carts;
                carts.spawnCart(x0, kRigY, z0, &w);          // 谷底西格
                carts.spawnCart(x0 + 1, kRigY, z0, &w);      // 谷底东格
                carts.spawnCart(x0 + 3, kRigY + 2, z0, &w);  // 东臂半山（cell x0+3 轨在 Y+2）
                carts.spawnCart(x0 - 2, kRigY + 2, z0, &w);  // 西臂半山（cell x0-2 轨在 Y+2 —— 西臂
                                                             //   镜像层差 x0-i@Y+i，勿按 +3 错层落地）
                bool railLine = true, axisYaw = true, inRig = true, allAlive = true;
                float pathLen[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                float prevXs[4];
                for (int ci = 0; ci < 4; ++ci) prevXs[ci] = carts.posAt(ci).x();
                int violT = -1, violCi = -1;
                QVector3D violP;
                for (int t = 0; t < 1500; ++t) {
                    carts.tickPushedCarts(0.016f, &w);       // 镜像 PlayerController 非骑乘帧序
                    carts.resolveCartCollisions(&w);
                    for (int ci = 0; ci < 4; ++ci) {
                        if (!carts.aliveAt(ci)) { allAlive = false; continue; }
                        const QVector3D p = carts.posAt(ci);
                        const bool bad = std::fabs(p.z() - (float(z0) + 0.5f)) > 0.02f
                            || p.x() < float(x0) - 5.4f || p.x() > float(x0) + 6.4f
                            || p.y() < float(kRigY) - 0.05f || p.y() > float(kRigY) + 5.6f;
                        if (bad && violT < 0) { violT = t; violCi = ci; violP = p; }
                        if (bad) inRig = false;                                          // 逃逸 / 坠落
                        if (std::fabs(p.z() - (float(z0) + 0.5f)) > 0.02f) railLine = false; // 出轨线
                        const float yawMod = std::fmod(carts.yawAt(ci), 90.0f);
                        if (!(yawMod < 0.5f || yawMod > 89.5f)) axisYaw = false;             // 横着（非轴向）
                        if (t >= 1100) pathLen[ci] += std::fabs(p.x() - prevXs[ci]);
                        prevXs[ci] = p.x();
                    }
                }
                float minPath = 99.0f;
                for (int ci = 0; ci < 4; ++ci) minPath = std::min(minPath, pathLen[ci]);
                okC = allAlive && railLine && axisYaw && inRig && minPath >= 1.0f;
                if (!okC)
                    qInfo().noquote() << "  [t943 diag] mc alive" << allAlive << "line" << railLine
                                      << "yaw" << axisYaw << "inRig" << inRig << "minPath" << minPath
                                      << "firstViol t" << violT << "cart" << violCi << "at" << violP
                                      << "finals" << carts.posAt(0) << carts.posAt(1)
                                      << carts.posAt(2) << carts.posAt(3);
                carts.clearAll();
            }
            // 清场。
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0, kRigY, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);
            for (int i = 1; i <= 5; ++i) {
                w.setBlock(x0 + 1 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 - i, kRigY + i, z0, BR::Air, 0);
            }
            tickN(w, 2);
        }
        // (d) 源码钉（t939/t940 模式：字符串钉，任一消失即红）。
        const QString exeDir943 = QCoreApplication::applicationDirPath();
        const QString root943 = QDir(exeDir943 + QStringLiteral("/..")).absolutePath();
        auto readSrc943 = [&root943](const QString &rel) -> QString {
            QFile f(root943 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc943 = readSrc943(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const bool okD1 = mc943.contains(QStringLiteral(
            "const bool coasting = !railPowered && std::fabs(proj) <= 1e-3f;"));
        const bool okD2 = mc943.contains(QStringLiteral(
            "if ((selfCon & connBit) != 0 && chainDelta != INT_MIN"));
        const bool okD3 = mc943.contains(QStringLiteral(
            "BlockRegistry::isRail(world->blockAt(tx, ryChain, tz)))")); // t983 #12 翻案：精确层验轨取代 chainDelta == ryTgt - rySelf
        const bool okD4 = mc943.contains(QStringLiteral(
            "if (va > vb + 1e-4f)"));
        const bool okD5 = mc943.contains(QStringLiteral(
            "if (aClamped && a.speed * wantA > 0.0f) a.speed = 0.0f;"));
        const bool okD6 = mc943.contains(QStringLiteral(
            "if (bClamped && b.speed * wantB > 0.0f) b.speed = 0.0f;"));
        const bool okD7 = mc943.contains(QStringLiteral(
            "if (!c.alive || c.derailed) continue;\n"
            "            cartYawFromDir(c.dirX, c.dirZ, c.yaw);"));
        okD = okD1 && okD2 && okD3 && okD4 && okD5 && okD6 && okD7;
        const bool okT943 = okA && okB && okC && okD;
        if (!okT943) ++totalFail;
        if (!okT943)
            qInfo().noquote() << "  [t943 diag] a" << okA << "b" << okB << "c" << okC
                              << "| d" << okD1 << okD2 << okD3 << okD4 << okD5 << okD6 << okD7
                              << "| srcLen cpp" << mc943.size();
        qInfo().noquote() << (okT943 ? "PASS" : "FAIL")
                          << "| t943 V-valley ridden slowdown + multi-cart escapes: the ridden path's"
                             " no-input coasting went through the targetV lerp where an uphill leg has"
                             " no slope gravity at all - only kCartFriction exponential decay (12.8"
                             " needs ~6.4 blocks to bleed off, crawling at 0.x b/s for seconds before"
                             " the reversal = the user's 'especially slow at the top'), while the"
                             " empty-cart path integrates kCartSlopeGravity along the track - same V,"
                             " different physics. Fix routes input-free unpowered riding through the"
                             " same slope integration as the empty path (uphill 19.8 b/s^2 bleed,"
                             " downhill converge to +-10, flat friction, t863(1) stall slide-back),"
                             " leaving input-driven and powered-rail semantics untouched. Multi-cart"
                             " escapes get three closures: the depenetration near-layer gate now also"
                             " requires chain continuity (the target column's rail must be this"
                             " chain's own slope continuation - connection bit plus railProbeDelta"
                             " layer match - so stacked crossing lines can no longer hijack a pressed"
                             " cart onto a neighboring chain), a persistently squeezed pair (both"
                             " sides clamped, still overlapped) gets velocity consistency (the faster"
                             " chaser along n adopts the chased velocity, clamped to +-boost, killing"
                             " the bounce-vs-slope-kick limit cycle) with clamp-direction zeroing (a"
                             " boundary-clamped cart may not keep velocity pointing into the clamp -"
                             " segment moves have no mid-cell boundary validation, so breaking this"
                             " slides carts through walls into rail-less columns where pinCartY drops"
                             " them, the t907(a) cage regression this probe family guards), and a"
                             " post-resolution cartYawFromDir re-pin keeps every rail-locked cart's"
                             " heading exactly on its rail axis. Probe legs: (a) empty reference cart"
                             " oscillates in the 5-arm V (capture threshold 14.1 > 12.8 impulse clamp"
                             " = closed system) with >=6 reversals and <=90 arm-crawl ticks in 24s;"
                             " (b) a mounted cart driven with no input meets the same thresholds -"
                             " same physics as empty, no parked start, no friction crawl at the"
                             " reversal; (c) four carts (two in the valley, one per arm) run 1500"
                             " ticks with collision resolution: all alive, never off the rail line,"
                             " yaw always an exact axis heading, never out of the rig, and every cart"
                             " still covers >=1.0 blocks over the last 400 ticks (no freeze, no"
                             " trembling deadlock); (d) source pins for the coasting gate, the chain"
                             " continuity lines, the velocity-consistency match, the clamp-direction"
                             " zeroing pair, and the yaw re-pin"
                          ;
    }

    // ── P-t981 V 形轨谷一次停驻 + 嵌入车失联冻结清算探针（MinecartManager 直编；spec「矿车放在 V 形铁轨
    //    滑落到底部时卡住、来回振荡最后才停——谷底应一次平滑减速停驻（或按速度通过），不许往复振荡」
    //    + review0831 #28 登记项（嵌入车 × 采样失联 = 每 tick 回退清速冻结）同域清算）──
    //   根因分账（两症不同源——实证非同症，一并收口）：
    //   ① 振荡：V 形凹谷格（行进轴两侧邻轨皆 +1 → railRiseAt 的 2|axis-0.5| 谷面）物理全量保守 —— 下坡
    //      半幅 kCartSlopeGravity 加速、上坡半幅同量减速，谷内零耗散；t863① 失速反溜（-0.5）与 t909②
    //      静置闸 kick（+1.0）在两壁停驻点反复再供能 → 两壁间极限环往复（腿 (a) 空场即复现 → #28 的
    //      「嵌入冻结」非此症根因）。修 = tryValleyBottomCapture：|speed| ≤ kCartValleyEscape(=重力终端
    //      10 —— 纯重力可达速度全捕) 的车进谷格即按「制动到谷心」速度律（a0 = v²/2d 每 tick 重算自校正）
    //      一次平滑减速停驻谷心（谷心梯度 0 = 静置闸稳定不动点，停驻后所有闸门一致静止）；boost / 冲量
    //      （>10）按速度通过。**t1019 演化：逃逸阈改能量判据（v² ≥ 2g·h 对面坡升 + 余量则放行）——行为
    //      腿 (a)(b)(c) 口径不变**（(a) 谷底滑落到达能 < 阈仍捕 / (b) boost ~11.6 仍过 / (c) 与捕获无关），
    //      逃逸阈钉同步演化见腿 (d)。
    //   ② review0831 #28：stepCartAlongRail 受阻三分法 !sampled 分支无条件回退+清速、不看 embeddedAtEntry
    //      豁免 —— 嵌入车遇失联子步（前探列无轨 / 列扫容差拒）每 tick 被打回子步起点 = 永久冻结（推力 /
    //      动力喂速全被吞）。修 = 补 haveFreePos 门（同 uphill 分支口径）：非嵌入回退、嵌入车豁免延续直至
    //      脱出。
    //   腿：
    //   (a) 谷一次停驻：W(x0,Y+1) / V(x0+1,Y) / E(x0+2,Y+1) 三格轨；W 上 spawn 滑落 → x 向反转数 == 0
    //       （pre-fix 极限环 ≥2）+ 终位 V 谷心 ±0.06 / 谷面高 ±0.03 + 静止守卫（60 tick 位移 <1e-3）；
    //   (b) 按速度通过：西端 8 格通电动力轨（各自 RedstoneBlock 直供）接 V 谷；boost ~11.6 入谷 > 逃逸阈
    //       → maxX 越谷心东侧 ≥0.9（谷没留住 = 通过；被捕车恒停谷心 ±0.06 不可能越过）；
    //   (c) 嵌入车失联解冻（#28）：平轨 (x0..x0+3,Y) 骑乘东行，越过 (x0+2) 格心后将 (x0+3) 轨换 Stone
    //       （车体前半已探入该格 = 入点嵌入）→ 续骑 → 车越过 x0+3.2（豁免推进穿石至死端飞出；pre-fix 恒
    //       被回退冻结在 x0+2.8 上下）；
    //   (d) 源码钉：两处捕获调用 / haveFreePos 门 / t1019 能量余量常量（t981 逃逸阈钉合法演化）/ 捕获实现签名。
    {
        // ── (a) 谷一次停驻。rig 选址：footprint x0-1..x0+3 × z0-1..z0+1 × Y-1..Y+2。──
        int xa = -1, za = -1;
        for (int zz = 3; zz < 94 && xa < 0; zz += 2)
            for (int xx = 6; xx + 3 < 96 && xa < 0; ++xx) {
                bool clear = true;
                for (int dx = -1; dx <= 3 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xa = xx; za = zz; }
            }
        bool okA = false;
        int revA = -1;
        if (xa < 0) {
            qInfo().noquote() << "  [t981 diag] a: no clear rig area";
        } else {
            w.setBlock(xa,     kRigY + 1, za, BR::Rail, 0); // 西壁（V 西邻 +1）
            w.setBlock(xa + 1, kRigY,     za, BR::Rail, 0); // V 谷格（两侧皆 +1 → 谷面 2|fx-0.5|）
            w.setBlock(xa + 2, kRigY + 1, za, BR::Rail, 0); // 东壁
            tickN(w, 2);
            const bool valleyOk =
                BlockRegistry::railProbeDelta({ w.blockAt(xa + 2, kRigY, za),
                                                w.blockAt(xa + 2, kRigY + 1, za),
                                                w.blockAt(xa + 2, kRigY - 1, za) }) == 1;
            MinecartManager carts;
            carts.spawnCart(xa, kRigY + 1, za, &w); // 单端连接（东）→ 定向 +X 滑落向
            const float vCenter = float(xa + 1) + 0.5f;
            QVector3D prev = carts.posAt(0);
            float accum = 0.0f;
            int state = 0;
            revA = 0;
            bool inRig = true;
            for (int t = 0; t < 600 && inRig; ++t) {
                carts.tickPushedCarts(0.016, &w);
                const QVector3D p = carts.posAt(0);
                accum += p.x() - prev.x();
                if (state == 0) {
                    if (accum > 0.25f) { state = 1; accum = 0.0f; }
                    else if (accum < -0.25f) { state = -1; accum = 0.0f; }
                } else if (state > 0 && accum < -0.25f) { ++revA; state = -1; accum = 0.0f; }
                else if (state < 0 && accum > 0.25f) { ++revA; state = 1; accum = 0.0f; }
                if (std::fabs(p.y() - prev.y()) > 0.55f) inRig = false; // Y 平滑守卫
                prev = p;
            }
            const QVector3D fin = carts.posAt(0);
            bool restOk = true;
            for (int t = 0; t < 60 && restOk; ++t) { // 静止守卫：捕获停驻后钉死（无 kick / 反溜再起）
                carts.tickPushedCarts(0.016f, &w);
                if ((carts.posAt(0) - fin).length() > 1e-3f) restOk = false;
            }
            okA = valleyOk && revA == 0 && inRig && restOk
                && std::fabs(fin.x() - vCenter) <= 0.06f
                && std::fabs(fin.y() - (float(kRigY) + 0.45f)) <= 0.03f;
            if (!okA)
                qInfo().noquote() << "  [t981 diag] a valley" << valleyOk << "rev" << revA
                                  << "inRig" << inRig << "rest" << restOk << "fin" << fin;
            carts.clearAll();
            w.setBlock(xa,     kRigY + 1, za, BR::Air, 0);
            w.setBlock(xa + 1, kRigY,     za, BR::Air, 0);
            w.setBlock(xa + 2, kRigY + 1, za, BR::Air, 0);
            tickN(w, 2);
        }
        // ── (b) 按速度通过。rig 选址：footprint x0-10..x0+1 × z0-1..z0+1 × Y-1..Y+2。──
        bool okB = false;
        int xb = -1, zb = -1;
        for (int zz = 3; zz < 94 && xb < 0; zz += 2)
            for (int xx = 12; xx + 1 < 96 && xb < 0; ++xx) {
                bool clear = true;
                for (int dx = -10; dx <= 1 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xb = xx; zb = zz; }
            }
        if (xb < 0) {
            qInfo().noquote() << "  [t981 diag] b: no clear rig area";
        } else {
            // 动力平台与谷壁**同层**（R+1）—— 平台直通西壁零爬阶耗能，入谷速 = boost 全额（> 逃逸阈）。
            for (int i = -9; i <= -2; ++i) { // 8 格通电动力轨（各自 RedstoneBlock 直供兼支撑）
                w.setBlock(xb + i, kRigY,     zb, BR::RedstoneBlock, 0);
                w.setBlock(xb + i, kRigY + 1, zb, BR::GoldenRail, 0);
            }
            w.setBlock(xb - 1, kRigY + 1, zb, BR::Rail, 0); // 西壁平段（东邻 V 低一格 → 跨谷壁）
            w.setBlock(xb,     kRigY,     zb, BR::Rail, 0); // V 谷格（西邻上格 +1 / 东邻上格 +1）
            w.setBlock(xb + 1, kRigY + 1, zb, BR::Rail, 0); // 东壁（东死端）
            tickN(w, 8);
            const bool poweredOk = (w.stateAt(xb - 2, kRigY + 1, zb) & BR::GoldenRailStateOnFlag) != 0;
            MinecartManager carts;
            carts.spawnCart(xb - 9, kRigY + 1, zb, &w); // 西端格心（静置闸不弹射 —— pushEmptyCart 起步）
            const float valleyCenterB = float(xb) + 0.5f;
            bool pushed = carts.pushEmptyCart(&w, QVector3D(float(xb - 9) - 0.2f,
                                                             float(kRigY + 1) + 0.45f,
                                                             float(zb) + 0.5f), 1.0f, 0.0f);
            float maxX = carts.posAt(0).x();
            for (int t = 0; t < 400; ++t) {
                carts.tickPushedCarts(0.016f, &w);
                if (carts.aliveAt(0)) maxX = std::max(maxX, carts.posAt(0).x());
            }
            okB = poweredOk && pushed && maxX > valleyCenterB + 0.9f; // 越谷心东侧 = 未被捕（通过）
            if (!okB)
                qInfo().noquote() << "  [t981 diag] b pow" << poweredOk << "push" << pushed
                                  << "maxX" << maxX << "center" << valleyCenterB;
            carts.clearAll();
            for (int i = -9; i <= -2; ++i) {
                w.setBlock(xb + i, kRigY,     zb, BR::Air, 0);
                w.setBlock(xb + i, kRigY + 1, zb, BR::Air, 0);
            }
            w.setBlock(xb - 1, kRigY + 1, zb, BR::Air, 0);
            w.setBlock(xb,     kRigY,     zb, BR::Air, 0);
            w.setBlock(xb + 1, kRigY + 1, zb, BR::Air, 0);
            tickN(w, 2);
        }
        // ── (c) 嵌入车失联解冻（review0831 #28）。rig 选址：footprint x0-1..x0+6 × z0-1..z0+1 × Y-1..Y+2。──
        bool okC = false;
        int xc = -1, zc = -1;
        for (int zz = 3; zz < 94 && xc < 0; zz += 2)
            for (int xx = 6; xx + 6 < 96 && xc < 0; ++xx) {
                bool clear = true;
                for (int dx = -1; dx <= 6 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xc = xx; zc = zz; }
            }
        if (xc < 0) {
            qInfo().noquote() << "  [t981 diag] c: no clear rig area";
        } else {
            for (int i = 0; i <= 3; ++i) w.setBlock(xc + i, kRigY, zc, BR::Rail, 0); // 平轨 4 格（东端将嵌石）
            tickN(w, 2);
            MinecartManager carts;
            carts.spawnCart(xc, kRigY, zc, &w);
            const bool mounted = carts.tryMount(QVector3D(float(xc) + 0.5f, float(kRigY) + 2.0f,
                                                          float(zc) + 0.5f), QVector3D(0, -1, 0), 4.0f);
            bool placed = false;
            float maxX = 0.0f;
            QVector3D cp;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp); // W 持续东行
                carts.tickPushedCarts(0.016, &w);
                cp = carts.posAt(0);
                maxX = std::max(maxX, cp.x());
                // 越过 (x0+2) 格心（(x0+3) 轨在位时格心重选已过）→ 轨换 Stone：车体前半探入石格
                //   （maxX = fx+1.0 > x0+3）= 入点嵌入；下一子步前探列扫描容差拒（石下无轨）→ 失联。
                if (!placed && cp.x() >= float(xc + 2) + 0.5f) {
                    w.setBlock(xc + 3, kRigY, zc, BR::Stone, 0);
                    placed = true;
                }
            }
            // post-fix：豁免推进穿石至石格格心 → deadEnd 停驻（速度 <3 分支；前探列无轨不构成坡顶飞出）
            //   → maxX ≥ x0+3.5；pre-fix：每 tick 回退清速恒冻结在 ~x0+2.75（ == 用户「推不动」）。
            okC = mounted && placed && maxX > float(xc) + 3.2f;
            if (!okC)
                qInfo().noquote() << "  [t981 diag] c mount" << mounted << "placed" << placed
                                  << "maxX" << maxX << "fin" << carts.posAt(0);
            carts.clearAll();
            for (int i = 0; i <= 2; ++i) w.setBlock(xc + i, kRigY, zc, BR::Air, 0);
            w.setBlock(xc + 3, kRigY, zc, BR::Air, 0);
            tickN(w, 2);
        }
        // ── (d) 源码钉（任一消失即红）。──
        const QString exeDir981 = QCoreApplication::applicationDirPath();
        const QString root981 = QDir(exeDir981 + QStringLiteral("/..")).absolutePath();
        auto readSrc981 = [&root981](const QString &rel) -> QString {
            QFile f(root981 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc981 = readSrc981(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const QString mh981 = readSrc981(QStringLiteral("src/Entities/minecartmanager.h"));
        const bool okD1 = mc981.contains(QStringLiteral(
            "} else if (tryValleyBottomCapture(c, world, ry, dt)) {"));
        const bool okD2 = mc981.contains(QStringLiteral(
            "if (tryValleyBottomCapture(c, world, railY, dt)) {"));
        const bool okD3 = mh981.contains(QStringLiteral(
            "static constexpr float kCartValleyPassMarginH = 0.05f;")); // t1019 演化：逃逸阈钉 → 能量判据余量钉
        const bool okD4 = mc981.contains(QStringLiteral(
            "bool MinecartManager::tryValleyBottomCapture(Cart &c, World *world, int railY, qreal dt)"));
        const bool okD5 = mc981.contains(QStringLiteral(
            "if (haveFreePos) {\n                        c.pos.setX(preX);"));
        const bool okD6 = mc981.contains(QStringLiteral("review0831 #28"));
        const bool okT981 = okA && okB && okC && okD1 && okD2 && okD3 && okD4 && okD5 && okD6;
        if (!okT981) ++totalFail;
        if (!okT981)
            qInfo().noquote() << "  [t981 diag] a" << okA << "b" << okB << "c" << okC
                              << "| d" << okD1 << okD2 << okD3 << okD4 << okD5 << okD6;
        qInfo().noquote() << (okT981 ? "PASS" : "FAIL")
                          << "| t981 V-valley one-pass settle + embedded sampling-lost freeze"
                             " (review0831 #28): an unpowered V-valley cell is fully conservative"
                             " physics - downhill half accelerates by kCartSlopeGravity, uphill half"
                             " decelerates by the same, zero dissipation inside the valley - while the"
                             " stall slide-back (-0.5) and the static-start kick (+1.0) re-pump energy"
                             " at each wall stop, so a cart sliding to the bottom enters a wall-to-wall"
                             " limit cycle instead of settling (user report: stuck oscillating, only"
                             " stops at the end). Fix adds tryValleyBottomCapture: a cart entering"
                             " a V-valley cell (straight rail,"
                             " both axis neighbors +1, same geometry as the railRiseAt valley branch)"
                             " is taken over by a brake-to-center speed law (a = v^2/2d recomputed per"
                             " tick, self-correcting), gliding to a single smooth stop at the valley"
                             " bottom center where the gradient is zero and every gate agrees on rest;"
                             " golden rails keep their own brake/boost semantics and fast carts pass"
                             " through by speed (t981 gated capture by the escape speed threshold;"
                             " t1019 legally evolved that gate to the energy criterion v^2 >= 2g*h"
                             " opposite-climb - both behavioral legs here are unchanged under it)."
                             " Same-domain registration review0831 #28 is verified"
                             " NOT this symptom (leg (a) reproduces the oscillation with no embedded"
                             " block) but is settled here: the !sampled branch of the stepCartAlongRail"
                             " blocked-triage reverted and zeroed speed unconditionally, ignoring the"
                             " embeddedAtEntry escape exemption - an embedded cart hitting a"
                             " sampling-lost substep (probe column with no reachable rail) was reverted"
                             " every tick and frozen solid (push feed swallowed; recovery only by"
                             " breaking the overlapping block). Fix gates the revert on haveFreePos"
                             " (same shape as the uphill branch): non-embedded carts still revert,"
                             " embedded carts keep the carry-overlap exemption until they escape."
                             " Probe legs: (a) cart spawned on the west wall of a 3-cell V glides down"
                             " and settles with ZERO x-direction reversals (pre-fix limit cycle makes"
                             " >=2), parks within 0.06 of the valley center at surface height and"
                             " stays pinned for 60 ticks; (b) a boost-fed cart (8-cell powered golden"
                             " run, arrival ~11.6, energy above the opposite-climb threshold) crosses the valley center"
                             " eastward by >=0.9 - the valley does not capture fast traffic; (c)"
                             " mounted eastbound cart whose forward rail is swapped to stone after"
                             " passing a cell center (body already probing the cell = embedded at"
                             " entry) escapes past x0+3.2 post-fix (pre-fix frozen forever at ~x0+2.8"
                             " = the user's immovable cart); (d) source pins for both capture call"
                             " sites, the t1019 energy-margin constant (t981 escape-threshold pin"
                             " legally evolved), the capture implementation, the"
                             " haveFreePos-gated revert and the #28 registration marker"
                          ;
    }

    // ── P-t1019 V 谷通过物理能量判据探针（MinecartManager 直编；spec「谷底捕获改能量判据——进谷速度
    //    足以爬升对面坡（v² ≥ 2g·h 对面坡升）则通过；不足才谷心制动停驻；消灭『打转』极限环」；
    //    t981 口径再翻案）──
    //   t981 速度阈（|speed| ≤ 10 全捕 / >10 全过）改能量判据逐例二选一：g 用项目坡道运动学口径
    //   kCartSlopeGravity（世界重力 28 × sin45° 沿轨分量，与谷内加速 / 上坡减速同一常量 → 能量账自洽）；
    //   h_对面坡升 = 行进向逐格 +1 连续爬升段总高（单壁 V = 1.0 → 通过阈 v ≥ sqrt(2·19.8·1.05) ≈
    //   6.45 blocks/s）；余量 0.05 格盖静置闸 kick（+1.0）贴阈再供能（crest-stall 回谷循环带收口）。
    //   腿（采位置/速度曲线，两速度档各自「通过或单调减速」二选一、无往复）：
    //   (a) 低速进谷 → 单调减速谷心停驻：3 格 V（W@Y+1 / V@Y / E@Y+1），W 上 spawn 静置闸起步（入谷
    //       速 ~4.6，v² ~21 < 阈 ~41.6）→ 曲线断言：x 向反转数 == 0、入谷后 |v| 单调不增（容差 2e-3）、
    //       到谷心距离单调不增（= 从未越心，容差 2e-3）、终位谷心 ±0.06、静止守卫（60 tick 位移 <1e-3）；
    //   (b) 高速进谷 → 通过：8 格通电动力轨平台（与谷壁同层直通）接 V 谷，boost ~11.6 入谷（v² ~135 >
    //       阈）→ 曲线断言：x 向反转数 == 0、x 全程单调不减（容差 2e-3）、越过谷心东 ≥0.9（被捕车恒停
    //       谷心 ±0.06 不可能越过 = 谷没留住）；
    //   (b2) 临界档（review0907 B-P2-3）：运行时自搜索动力平台长 L×平段衰减格 D 共 8 档，取入谷 v²
    //       落通过阈 ±10% 窗的档，断言「通过或停驻二选一、无往复」（rev ≤ 1）——判据漂移最敏感点；
    //   (c) 源码钉：能量判据行 / 对面坡升扫描行 / 余量常量 / 扫描上限常量。
    {
        // ── (a) 低速档。rig 选址：footprint x0-1..x0+3 × z0-1..z0+1 × Y-1..Y+2。──
        int xa = -1, za = -1;
        for (int zz = 3; zz < 94 && xa < 0; zz += 2)
            for (int xx = 6; xx + 3 < 96 && xa < 0; ++xx) {
                bool clear = true;
                for (int dx = -1; dx <= 3 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xa = xx; za = zz; }
            }
        bool okA = false;
        if (xa < 0) {
            qInfo().noquote() << "  [t1019 diag] a: no clear rig area";
        } else {
            w.setBlock(xa,     kRigY + 1, za, BR::Rail, 0); // 西壁（V 西邻 +1）
            w.setBlock(xa + 1, kRigY,     za, BR::Rail, 0); // V 谷格（两侧皆 +1 → 谷面 2|fx-0.5|）
            w.setBlock(xa + 2, kRigY + 1, za, BR::Rail, 0); // 东壁
            tickN(w, 2);
            MinecartManager carts;
            carts.spawnCart(xa, kRigY + 1, za, &w); // 单端连接（东）→ 静置闸起步滑落
            const float vCenter = float(xa + 1) + 0.5f;
            const float vEnter = float(xa + 1);     // 谷格西缘（入谷判定线）
            QList<float> xs, vs;
            QVector3D prev = carts.posAt(0);
            float accum = 0.0f;
            int state = 0;
            int rev = 0;
            bool inRig = true;
            for (int t = 0; t < 600 && inRig; ++t) {
                carts.tickPushedCarts(0.016, &w);
                const QVector3D p = carts.posAt(0);
                xs.append(p.x());
                vs.append((p.x() - prev.x()) / 0.016f); // 视速（位置差分）
                accum += p.x() - prev.x();
                if (state == 0) {
                    if (accum > 0.25f) { state = 1; accum = 0.0f; }
                    else if (accum < -0.25f) { state = -1; accum = 0.0f; }
                } else if (state > 0 && accum < -0.25f) { ++rev; state = -1; accum = 0.0f; }
                else if (state < 0 && accum > 0.25f) { ++rev; state = 1; accum = 0.0f; }
                if (std::fabs(p.y() - prev.y()) > 0.55f) inRig = false; // Y 平滑守卫
                prev = p;
            }
            // 曲线断言：首个入谷 tick 起 |v| 单调不增 + 到心距离单调不增（制动律常减速 → 严格递减）。
            int i0 = -1;
            for (int i = 0; i < xs.size(); ++i)
                if (xs.at(i) >= vEnter) { i0 = i; break; }
            bool monoV = i0 >= 0, monoD = i0 >= 0;
            for (int i = i0 + 1; i < xs.size(); ++i) {
                if (std::fabs(vs.at(i)) > std::fabs(vs.at(i - 1)) + 2e-3f) monoV = false;
                if (std::fabs(xs.at(i) - vCenter) > std::fabs(xs.at(i - 1) - vCenter) + 2e-3f)
                    monoD = false;
            }
            const QVector3D fin = carts.posAt(0);
            bool restOk = true;
            for (int t = 0; t < 60 && restOk; ++t) { // 静止守卫：停驻后钉死（无 kick / 反溜再起）
                carts.tickPushedCarts(0.016f, &w);
                if ((carts.posAt(0) - fin).length() > 1e-3f) restOk = false;
            }
            okA = monoV && monoD && rev == 0 && inRig && restOk
                && std::fabs(fin.x() - vCenter) <= 0.06f
                && std::fabs(fin.y() - (float(kRigY) + 0.45f)) <= 0.03f;
            if (!okA)
                qInfo().noquote() << "  [t1019 diag] a i0" << i0 << "n" << xs.size() << "monoV"
                                  << monoV << "monoD" << monoD << "rev" << rev << "inRig" << inRig
                                  << "rest" << restOk << "fin" << fin;
            carts.clearAll();
            w.setBlock(xa,     kRigY + 1, za, BR::Air, 0);
            w.setBlock(xa + 1, kRigY,     za, BR::Air, 0);
            w.setBlock(xa + 2, kRigY + 1, za, BR::Air, 0);
            tickN(w, 2);
        }
        // ── (b) 高速档（通过）。rig 选址：footprint x0-10..x0+1 × z0-1..z0+1 × Y-1..Y+2。──
        bool okB = false;
        int xb = -1, zb = -1;
        for (int zz = 3; zz < 94 && xb < 0; zz += 2)
            for (int xx = 12; xx + 1 < 96 && xb < 0; ++xx) {
                bool clear = true;
                for (int dx = -10; dx <= 1 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xb = xx; zb = zz; }
            }
        if (xb < 0) {
            qInfo().noquote() << "  [t1019 diag] b: no clear rig area";
        } else {
            // 动力平台与谷壁同层（R+1）—— 平台直通西壁零爬阶耗能，入谷速 = boost 全额（能量 > 阈）。
            for (int i = -9; i <= -2; ++i) { // 8 格通电动力轨（各自 RedstoneBlock 直供兼支撑）
                w.setBlock(xb + i, kRigY,     zb, BR::RedstoneBlock, 0);
                w.setBlock(xb + i, kRigY + 1, zb, BR::GoldenRail, 0);
            }
            w.setBlock(xb - 1, kRigY + 1, zb, BR::Rail, 0); // 西壁平段（东邻 V 低一格）
            w.setBlock(xb,     kRigY,     zb, BR::Rail, 0); // V 谷格（两侧皆 +1）
            w.setBlock(xb + 1, kRigY + 1, zb, BR::Rail, 0); // 东壁（东死端）
            tickN(w, 8);
            const bool poweredOk = (w.stateAt(xb - 2, kRigY + 1, zb) & BR::GoldenRailStateOnFlag) != 0;
            MinecartManager carts;
            carts.spawnCart(xb - 9, kRigY + 1, zb, &w);
            const float valleyCenter = float(xb) + 0.5f;
            const bool pushed = carts.pushEmptyCart(&w, QVector3D(float(xb - 9) - 0.2f,
                                                                 float(kRigY + 1) + 0.45f,
                                                                 float(zb) + 0.5f), 1.0f, 0.0f);
            QList<float> xs;
            float maxX = carts.posAt(0).x();
            xs.append(maxX);
            QVector3D prev = carts.posAt(0);
            float accum = 0.0f;
            int state = 0;
            int rev = 0;
            for (int t = 0; t < 400; ++t) {
                carts.tickPushedCarts(0.016f, &w);
                if (!carts.aliveAt(0)) break;
                const QVector3D p = carts.posAt(0);
                xs.append(p.x());
                maxX = std::max(maxX, p.x());
                accum += p.x() - prev.x();
                if (state == 0) {
                    if (accum > 0.25f) { state = 1; accum = 0.0f; }
                    else if (accum < -0.25f) { state = -1; accum = 0.0f; }
                } else if (state > 0 && accum < -0.25f) { ++rev; state = -1; accum = 0.0f; }
                else if (state < 0 && accum > 0.25f) { ++rev; state = 1; accum = 0.0f; }
                prev = p;
            }
            bool monoFwd = true; // x 全程单调不减（无往复；飞出 / 停驻都只进不退）
            for (int i = 1; i < xs.size(); ++i)
                if (xs.at(i) < xs.at(i - 1) - 2e-3f) monoFwd = false;
            okB = poweredOk && pushed && monoFwd && rev == 0
                && maxX > valleyCenter + 0.9f; // 越谷心东侧 = 未被捕（通过）
            if (!okB)
                qInfo().noquote() << "  [t1019 diag] b pow" << poweredOk << "push" << pushed
                                  << "monoFwd" << monoFwd << "rev" << rev << "maxX" << maxX
                                  << "center" << valleyCenter;
            carts.clearAll();
            for (int i = -9; i <= -2; ++i) {
                w.setBlock(xb + i, kRigY,     zb, BR::Air, 0);
                w.setBlock(xb + i, kRigY + 1, zb, BR::Air, 0);
            }
            w.setBlock(xb - 1, kRigY + 1, zb, BR::Air, 0);
            w.setBlock(xb,     kRigY,     zb, BR::Air, 0);
            w.setBlock(xb + 1, kRigY + 1, zb, BR::Air, 0);
            tickN(w, 2);
        }
        // ── (b2) 临界档腿（review0907 B-P2-3 最小版）：调车初速使入谷 v² 贴近通过阈
        //     2g(h+margin)（±10% 窗），断言「通过或停驻二选一、无往复」（不判方向只判无极限环
        //     —— rev ≤ 1：单次爬壁回溜不算打转，反复往复才算）。动力轨 boost 是逐 tick 朝 12.8
        //     档 lerp（无解析式），初速靠**运行时自搜索**：平台长 L∈1..4 × 平段衰减格 D∈0..1 共
        //     8 档逐试，入谷帧差分测 v²，任一档落窗且行为二选一即绿；判据漂移（阈式 / 余量改版）
        //     时阈边行为翻转最先在本腿暴露。阈值同值镜像生产式（常量私有不可直引；生产常量另由
        //     (c) 腿钉与 t1019(d) 常量钉互锁，漂移两头同抓）。
        bool okB2 = false;
        {
            int x2 = -1, z2 = -1; // 选址：x 跨 8 格（平台≤4 + 平段≤1 + 谷 1 + 东壁 1 + 余量）× z±1
            for (int zz = 3; zz < 94 && x2 < 0; zz += 2)
                for (int xx = 6; xx + 8 < 96 && x2 < 0; ++xx) {
                    bool clear = true;
                    for (int dx = 0; dx <= 7 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -1; dy <= 2 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x2 = xx; z2 = zz; }
                }
            if (x2 < 0) {
                qInfo().noquote() << "  [t1019 diag] b2: no clear rig area";
            } else {
                const float gM = 19.799f;                  // = MinecartManager::kCartSlopeGravity（私有）
                const float pass2M = 2.0f * gM * (1.0f + 0.05f); // h_对面坡升 1（单壁 V）+ margin 0.05 = 41.58
                QString diagB2;
                for (int L = 1; L <= 4 && !okB2; ++L) {
                    for (int D = 0; D <= 1 && !okB2; ++D) {
                        // 建 rig：平台 L 格通电动力轨（红石块直供）| 平段 D 格普通轨（摩擦衰减细调）
                        //   | 谷格（西侧平段邻接 = 西壁 +1，与 (a) 单壁 V 同构）| 东壁（对面坡，东死端）。
                        for (int i = 0; i < L; ++i) {
                            w.setBlock(x2 + 1 + i, kRigY,     z2, BR::RedstoneBlock, 0);
                            w.setBlock(x2 + 1 + i, kRigY + 1, z2, BR::GoldenRail, 0);
                        }
                        for (int i = 0; i < D; ++i)
                            w.setBlock(x2 + 1 + L + i, kRigY + 1, z2, BR::Rail, 0);
                        const int vx = x2 + 1 + L + D;     // 谷格
                        w.setBlock(vx,     kRigY,     z2, BR::Rail, 0);
                        w.setBlock(vx + 1, kRigY + 1, z2, BR::Rail, 0);
                        tickN(w, 8);
                        MinecartManager carts;
                        carts.spawnCart(x2 + 1, kRigY + 1, z2, &w); // 平台西端起步（东向单端连接）
                        const bool pushedB2 = carts.pushEmptyCart(&w,
                            QVector3D(float(x2 + 1) - 0.2f, float(kRigY + 1) + 0.45f, float(z2) + 0.5f),
                            1.0f, 0.0f);
                        const float centerB2 = float(vx) + 0.5f;
                        float vEntry = -1.0f, maxX = carts.posAt(0).x();
                        QVector3D prev = carts.posAt(0), fin = prev;
                        float accum = 0.0f;
                        int state = 0, rev = 0;
                        bool entered = false;
                        for (int t = 0; t < 600; ++t) {
                            carts.tickPushedCarts(0.016f, &w);
                            if (!carts.aliveAt(0)) break;
                            const QVector3D p = carts.posAt(0);
                            const float step = p.x() - prev.x();
                            maxX = std::max(maxX, p.x());
                            if (!entered && p.x() >= float(vx)) { // 入谷帧：差分速度 = 进谷 v
                                entered = true;
                                vEntry = std::fabs(step) / 0.016f;
                            }
                            accum += step; // 反转计数状态机（(a)/(b) 同式：±0.25 迟滞）
                            if (state == 0) {
                                if (accum > 0.25f) { state = 1; accum = 0.0f; }
                                else if (accum < -0.25f) { state = -1; accum = 0.0f; }
                            } else if (state > 0 && accum < -0.25f) { ++rev; state = -1; accum = 0.0f; }
                            else if (state < 0 && accum > 0.25f) { ++rev; state = 1; accum = 0.0f; }
                            prev = p;
                            fin = p;
                        }
                        const float v2 = vEntry * vEntry;
                        const bool inWin = vEntry > 0.0f && std::fabs(v2 - pass2M) <= 0.10f * pass2M;
                        const bool passedB2 = maxX > centerB2 + 0.9f;      // 越谷心东侧 = 通过
                        const bool stoppedB2 = std::fabs(fin.x() - centerB2) <= 0.15f; // 谷心停驻
                        const bool trialOk = pushedB2 && entered && inWin && rev <= 1
                            && (passedB2 || stoppedB2);
                        diagB2 += QStringLiteral("L%1D%2:v2=%3%4%5%6%7 ")
                            .arg(L).arg(D).arg(v2, 0, 'f', 1)
                            .arg(inWin ? QStringLiteral("WIN") : QStringLiteral("-"))
                            .arg(pushedB2 ? QString() : QStringLiteral("!push"))
                            .arg(rev <= 1 ? QString() : QStringLiteral("!rev%1").arg(rev))
                            .arg((passedB2 || stoppedB2) ? QString() : QStringLiteral("!outc"));
                        carts.clearAll();
                        for (int i = 0; i < L; ++i) {
                            w.setBlock(x2 + 1 + i, kRigY,     z2, BR::Air, 0);
                            w.setBlock(x2 + 1 + i, kRigY + 1, z2, BR::Air, 0);
                        }
                        for (int i = 0; i < D; ++i)
                            w.setBlock(x2 + 1 + L + i, kRigY + 1, z2, BR::Air, 0);
                        w.setBlock(vx,     kRigY,     z2, BR::Air, 0);
                        w.setBlock(vx + 1, kRigY + 1, z2, BR::Air, 0);
                        tickN(w, 2);
                        if (trialOk) okB2 = true;
                    }
                }
                if (!okB2)
                    qInfo().noquote() << "  [t1019 diag] b2 none in +-10% window of" << pass2M
                                      << ":" << diagB2;
            }
        }
        // ── (d) 带内中速档腿（review0906 #6）：入格缘 v² ∈ [25,39] —— 错杀带 [21.8, 41.58) 内的可达判别
        //     域（单格 boost 档实测入谷 v² ~38：L1D0=38.5 / L1D4=37.8，更低档不可达——衰减每平格仅
        //     ~0.18 v²，坡下降增益又计入采样点之后）。判别双向裕度：旧式直比阈 38.5 < 41.58 = 捕（裕
        //     3.1）；谷底账 38.5 + 2g·d ≈ 58.3 ≥ 41.58 = 放行爬上东壁（裕 16.7）。旧式判据在下行半幅
        //     早评当前 v² → 该档全捕 = 本腿红（阴性轮敏感）。初速靠运行时自搜索（(b2) 同式 —— boost
        //     lerp 无解析式）：平台长 L∈1..4 × 平段衰减格 D∈0..4 逐试，取入谷 v² 落带内档，断言「过谷
        //     心爬上东壁」（maxX > center+0.6 = 完整翻越；被捕车恒停谷心 ±0.06 不可能）+ 无极限环
        //     （rev ≤ 1 —— 翻越东壁死端后单次回溜落谷被接住是文档化近阈形态）。
        bool okD6 = false;
        {
            int x4 = -1, z4 = -1; // 选址：x 跨 11 格（平台≤4 + 平段≤4 + 谷 1 + 东壁 1 + 余量）× z±1
            for (int zz = 3; zz < 94 && x4 < 0; zz += 2)
                for (int xx = 6; xx + 10 < 96 && x4 < 0; ++xx) {
                    bool clear = true;
                    for (int dx = 0; dx <= 10 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -1; dy <= 2 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x4 = xx; z4 = zz; }
                }
            if (x4 < 0) {
                qInfo().noquote() << "  [t1019 diag] d: no clear rig area";
            } else {
                QString diagD6;
                for (int L = 1; L <= 4 && !okD6; ++L) {
                    for (int D = 0; D <= 4 && !okD6; ++D) {
                        for (int i = 0; i < L; ++i) {
                            w.setBlock(x4 + 1 + i, kRigY,     z4, BR::RedstoneBlock, 0);
                            w.setBlock(x4 + 1 + i, kRigY + 1, z4, BR::GoldenRail, 0);
                        }
                        for (int i = 0; i < D; ++i)
                            w.setBlock(x4 + 1 + L + i, kRigY + 1, z4, BR::Rail, 0);
                        const int vx4 = x4 + 1 + L + D;     // 谷格
                        w.setBlock(vx4,     kRigY,     z4, BR::Rail, 0);
                        w.setBlock(vx4 + 1, kRigY + 1, z4, BR::Rail, 0);
                        tickN(w, 8);
                        MinecartManager carts;
                        carts.spawnCart(x4 + 1, kRigY + 1, z4, &w);
                        const bool pushedD6 = carts.pushEmptyCart(&w,
                            QVector3D(float(x4 + 1) - 0.2f, float(kRigY + 1) + 0.45f, float(z4) + 0.5f),
                            1.0f, 0.0f);
                        const float centerD6 = float(vx4) + 0.5f;
                        float vEntryD = -1.0f, maxXD = carts.posAt(0).x();
                        QVector3D prevD = carts.posAt(0);
                        float accumD = 0.0f;
                        int stateD = 0, revD = 0;
                        bool enteredD = false;
                        for (int t = 0; t < 600; ++t) {
                            carts.tickPushedCarts(0.016f, &w);
                            if (!carts.aliveAt(0)) break;
                            const QVector3D p = carts.posAt(0);
                            const float step = p.x() - prevD.x();
                            maxXD = std::max(maxXD, p.x());
                            if (!enteredD && p.x() >= float(vx4)) {
                                enteredD = true;
                                vEntryD = std::fabs(step) / 0.016f;
                            }
                            accumD += step;
                            if (stateD == 0) {
                                if (accumD > 0.25f) { stateD = 1; accumD = 0.0f; }
                                else if (accumD < -0.25f) { stateD = -1; accumD = 0.0f; }
                            } else if (stateD > 0 && accumD < -0.25f) { ++revD; stateD = -1; accumD = 0.0f; }
                            else if (stateD < 0 && accumD > 0.25f) { ++revD; stateD = 1; accumD = 0.0f; }
                            prevD = p;
                        }
                        const float v2D6 = vEntryD * vEntryD;
                        const bool inBandD = vEntryD > 0.0f && v2D6 >= 25.0f && v2D6 <= 39.0f;
                        const bool passedD6 = maxXD > centerD6 + 0.6f; // 翻上东壁（被捕车不可能）
                        const bool trialOkD = pushedD6 && enteredD && inBandD
                            && revD <= 1 && passedD6;
                        diagD6 += QStringLiteral("L%1D%2:v2=%3%4%5%6 ")
                            .arg(L).arg(D).arg(v2D6, 0, 'f', 1)
                            .arg(inBandD ? QStringLiteral("BAND") : QStringLiteral("-"))
                            .arg(revD <= 1 ? QString() : QStringLiteral("!rev%1").arg(revD))
                            .arg(passedD6 ? QString() : QStringLiteral("!pass"));
                        carts.clearAll();
                        for (int i = 0; i < L; ++i) {
                            w.setBlock(x4 + 1 + i, kRigY,     z4, BR::Air, 0);
                            w.setBlock(x4 + 1 + i, kRigY + 1, z4, BR::Air, 0);
                        }
                        for (int i = 0; i < D; ++i)
                            w.setBlock(x4 + 1 + L + i, kRigY + 1, z4, BR::Air, 0);
                        w.setBlock(vx4,     kRigY,     z4, BR::Air, 0);
                        w.setBlock(vx4 + 1, kRigY + 1, z4, BR::Air, 0);
                        tickN(w, 2);
                        if (trialOkD) okD6 = true;
                    }
                }
                if (!okD6)
                    qInfo().noquote() << "  [t1019 diag] d no in-band trial:" << diagD6;
            }
        }
        // ── (e/f) 金轨谷格腿（review0906 #7）：谷格换断电金轨（无红石块供能）—— 旧版无条件豁免把
        //     滑行车交还纯保守谷物理 + 两壁 kick 再供能 = t981 往复在断电金轨谷复发（t939③ 刹车只在
        //     静置分支，管不住滑行车）。收窄后断电金轨谷格照捕：静置闸起步（入谷 v² ~21，谷底账
        //     40.8 < 41.58）→ 捕获停驻谷心（无往复 + 60 tick 静止守卫；停驻 |speed|→0 恰落入 t939③
        //     静置刹车 = 稳定不动点）。(f) 通电对照（红石块直供谷格金轨）：boost 在谷格接管 → 通过
        //     （豁免语义仍在，防收窄误伤通电轨型）。几何与 (a) 单壁 V 同构（谷格西东两邻皆 +1）。
        bool okG7 = false;
        {
            int xg = -1, zg = -1;
            for (int zz = 3; zz < 94 && xg < 0; zz += 2)
                for (int xx = 6; xx + 3 < 96 && xg < 0; ++xx) {
                    bool clear = true;
                    for (int dx = -1; dx <= 3 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -1; dy <= 2 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { xg = xx; zg = zz; }
                }
            if (xg < 0) {
                qInfo().noquote() << "  [t1019 diag] e: no clear rig area";
            } else {
                const auto buildVG = [&](bool powered) {
                    w.setBlock(xg,     kRigY + 1, zg, BR::Rail, 0);       // 西壁
                    if (powered) w.setBlock(xg + 1, kRigY - 1, zg, BR::RedstoneBlock, 0);
                    w.setBlock(xg + 1, kRigY,     zg, BR::GoldenRail, 0); // 谷格 = 金轨
                    w.setBlock(xg + 2, kRigY + 1, zg, BR::Rail, 0);       // 东壁（死端）
                    tickN(w, 8);
                };
                const auto clearVG = [&]() {
                    w.setBlock(xg,     kRigY + 1, zg, BR::Air, 0);
                    w.setBlock(xg + 1, kRigY,     zg, BR::Air, 0);
                    w.setBlock(xg + 2, kRigY + 1, zg, BR::Air, 0);
                    w.setBlock(xg + 1, kRigY - 1, zg, BR::Air, 0);
                    tickN(w, 2);
                };
                const float centerG = float(xg + 1) + 0.5f;
                const auto runCartG = [&](int &revOut, float &maxXOut, QVector3D &finOut) {
                    MinecartManager carts;
                    carts.spawnCart(xg, kRigY + 1, zg, &w); // 西壁单端连接（东）→ 静置闸起步滑落
                    maxXOut = carts.posAt(0).x();
                    QVector3D prev = carts.posAt(0);
                    float accum = 0.0f;
                    int state = 0;
                    revOut = 0;
                    for (int t = 0; t < 600; ++t) {
                        carts.tickPushedCarts(0.016f, &w);
                        if (!carts.aliveAt(0)) break;
                        const QVector3D p = carts.posAt(0);
                        maxXOut = std::max(maxXOut, p.x());
                        accum += p.x() - prev.x();
                        if (state == 0) {
                            if (accum > 0.25f) { state = 1; accum = 0.0f; }
                            else if (accum < -0.25f) { state = -1; accum = 0.0f; }
                        } else if (state > 0 && accum < -0.25f) { ++revOut; state = -1; accum = 0.0f; }
                        else if (state < 0 && accum > 0.25f) { ++revOut; state = 1; accum = 0.0f; }
                        prev = p;
                        finOut = p;
                    }
                    carts.clearAll();
                };
                // (e) 断电：捕获停驻谷心，无往复、静止守卫。
                buildVG(false);
                const bool unpoweredOkG = (w.stateAt(xg + 1, kRigY, zg) & BR::GoldenRailStateOnFlag) == 0;
                int revE = 0;
                float maxXE = 0.0f;
                QVector3D finE;
                runCartG(revE, maxXE, finE);
                bool restOkE = true;
                {
                    // 静止守卫（(a) 同式）：整段重跑 600 tick 后从终位起测 60 tick 位移
                    //（捕获制动律末段每 tick 位移仍 >1e-3，须等完全停驻再测）。
                    MinecartManager carts;
                    carts.spawnCart(xg, kRigY + 1, zg, &w);
                    QVector3D finR;
                    for (int t = 0; t < 600; ++t) {
                        carts.tickPushedCarts(0.016f, &w);
                        if (!carts.aliveAt(0)) break;
                        finR = carts.posAt(0);
                    }
                    for (int t = 0; t < 60 && restOkE; ++t) {
                        carts.tickPushedCarts(0.016f, &w);
                        if ((carts.posAt(0) - finR).length() > 1e-3f) restOkE = false;
                    }
                    carts.clearAll();
                }
                const bool okE7 = unpoweredOkG && revE == 0
                    && maxXE <= centerG + 0.6f                    // 未通过（被捕）
                    && std::fabs(finE.x() - centerG) <= 0.15f     // 停驻谷心
                    && restOkE;                                   // 无 kick/反溜再起
                if (!okE7)
                    qInfo().noquote() << "  [t1019 diag] e pow" << unpoweredOkG << "rev" << revE
                                      << "maxX" << maxXE << "fin" << finE << "rest" << restOkE
                                      << "center" << centerG;
                clearVG();
                // (f) 通电对照：boost 谷格接管 → 通过（豁免仍在）。
                buildVG(true);
                const bool poweredOkG = (w.stateAt(xg + 1, kRigY, zg) & BR::GoldenRailStateOnFlag) != 0;
                int revF = 0;
                float maxXF = 0.0f;
                QVector3D finF;
                runCartG(revF, maxXF, finF);
                const bool okF7 = poweredOkG && maxXF > centerG + 0.6f && revF <= 1;
                if (!okF7)
                    qInfo().noquote() << "  [t1019 diag] f pow" << poweredOkG << "rev" << revF
                                      << "maxX" << maxXF << "fin" << finF << "center" << centerG;
                clearVG();
                okG7 = okE7 && okF7;
            }
        }
        // ── (c) 源码钉（任一消失即红）。──
        const QString exeDir1019 = QCoreApplication::applicationDirPath();
        const QString root1019 = QDir(exeDir1019 + QStringLiteral("/..")).absolutePath();
        auto readSrc1019 = [&root1019](const QString &rel) -> QString {
            QFile f(root1019 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc1019 = readSrc1019(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const QString mh1019 = readSrc1019(QStringLiteral("src/Entities/minecartmanager.h"));
        const bool okC1 = mc1019.contains(QStringLiteral(
            "if (v2ValleyFloor >= pass2) return false;")); // 能量足（谷底账）→ 放行通过（review0906 #6 折算）
        const bool okC2 = mc1019.contains(QStringLiteral(
            "const float pass2 = 2.0f * kCartSlopeGravity")); // 通过阈 = 2g·(h + 余量)
        const bool okC3 = mc1019.contains(QStringLiteral(
            "if (dK == INT_MIN) break;")); // 对面坡升扫描（review0906 #16 三高探针口径）
        const bool okC4 = mh1019.contains(QStringLiteral(
            "static constexpr int kCartValleyClimbScanMax = 16;"));
        const bool okT1019 = okA && okB && okC1 && okC2 && okC3 && okC4;
        if (!okT1019) ++totalFail;
        if (!okT1019)
            qInfo().noquote() << "  [t1019 diag] a" << okA << "b" << okB << "b2(critical)" << okB2
                              << "| c" << okC1 << okC2 << okC3 << okC4;
        qInfo().noquote() << (okT1019 ? "PASS" : "FAIL")
                          << "| t1019 V-valley pass physics on the energy criterion (t981 rework):"
                             " the old capture gate was a single speed threshold (|v| <= 10 captures"
                             " everything gravity can reach, > 10 passes everything powered) which"
                             " cannot ask the per-case question 'can THIS cart climb THIS opposite"
                             " wall' - near-conservative valley physics returns exactly the rim-drop"
                             " energy to a cart entering from an equal-height wall, so a threshold"
                             " either traps energetic gravity traffic in the bowl or releases"
                             " under-powered carts to fail on the far wall and re-enter (the spin"
                             " limit cycle). Fix replaces the gate with the energy criterion: pass"
                             " iff v^2 >= 2*kCartSlopeGravity*(h_opposite + margin), where g is the"
                             " project's slope kinematics constant (world gravity 28 x sin45 along-"
                             " track component, the same constant the valley integrates) and"
                             " h_opposite is the total contiguous +1-per-cell climb of the far wall"
                             " (scanned per travel direction, staircase walls fully counted - a"
                             " deep bowl must not release a cart it cannot eject); the 0.05-block"
                             " margin covers the settle-kick re-pump so a just-barely-escaping cart"
                             " is captured instead of crest-stalling into the kick-back band."
                             " A critical-energy leg (review0907 B-P2-3) is reported separately"
                             " below (its own PASS line)."
                             " Insufficient energy still brakes to a single smooth stop at the"
                             " valley center (t981 law unchanged). Probe legs (position/velocity"
                             " curves, pass-or-monotonic-decel, no oscillation in either speed"
                             " tier): (a) low tier - cart released on the west rim of a 3-cell V"
                             " enters at v^2 ~ 21 < threshold ~ 41.6, |v| non-increasing after"
                             " valley entry, distance-to-center non-increasing (never crosses),"
                             " zero x reversals, parks within 0.06 of the center, pinned for 60"
                             " ticks; (b) high tier - boost-fed cart enters at v^2 ~ 135 > threshold,"
                             " x strictly non-decreasing, zero reversals, crosses >= 0.9 east of the"
                             " center (a captured cart cannot); (c) source pins for the energy"
                             " pass line, the pass threshold, the opposite-climb scan and the scan"
                             " cap constant"
                          ;
        if (!okB2) ++totalFail;
        qInfo().noquote() << (okB2 ? "PASS" : "FAIL")
                          << "| t1019(b2) critical-energy boundary leg (review0907 B-P2-3): runtime"
                             " self-search over powered-platform lengths L in 1..4 x flat decay"
                             " cells D in 0..1 finds a trial whose measured valley-entry v^2 lands"
                             " within +-10% of the mirrored pass threshold 2*19.799*(1+0.05)"
                             " = 41.58 (boost is a per-tick lerp toward the 12.8 cap with no"
                             " closed form, so the leg calibrates itself each run instead of"
                             " hardcoding a platform length), then asserts the boundary contract:"
                             " pass (crosses 0.9 east of center) OR stop (parks within 0.15 of"
                             " center) - never a limit cycle (<= 1 x-reversal; a single climb-and-"
                             " return on the far wall is the documented near-threshold capture"
                             " shape, repeated reciprocation is the killed spin cycle). Most"
                             " sensitive spot for criterion drift: a changed threshold formula or"
                             " margin flips the behavior of the in-window trial first here"
                          << (okB2 ? QString()
                                   : QStringLiteral("diag no trial in window, see [t1019 diag] b2"));
        if (!okD6) ++totalFail;
        qInfo().noquote() << (okD6 ? "PASS" : "FAIL")
                          << "| t1019(d) in-band mid-tier leg (review0906 #6): a cart entering the"
                             " valley cell at v^2 in [25, 39] - inside the ~[21.8, 41.6)"
                             " wrong-capture band (the reachable single-cell-boost tier measures"
                             " ~38 at the rim; lower tiers are unreachable - flat decay bleeds"
                             " only ~0.2 v^2 per cell and the west-descent gain lands after the"
                             " sampling point) - must PASS through, because the criterion now"
                             " charges the energy to the valley-floor account: the downhill half"
                             " still returns ~2*g*d = ~19.8 v^2 while the cart slides to the"
                             " center, so the sampled account 38.5 + 19.8 = 58.3 clears the"
                             " pass threshold 41.58 (old-capture margin 3.1, new-pass margin"
                             " 16.7) and the cart crests the far wall (crosses"
                             " 0.6+ past the center; a captured cart parks within 0.06 of it and"
                             " can never). The old gate sampled the CURRENT v^2 every tick of the"
                             " downhill half - capture is takeover (skip slope physics, v monotone"
                             " down) so the cart never got its 'slide to the floor, grow, then be"
                             " released' chance and mid-band entries were wrongly braked to a stop"
                             " at the center (legs (a)/(b) straddle the band edges and stayed"
                             " green on both formulas; this leg is the discriminator). Runtime"
                             " self-search over platform L in 1..4 x flat decay D in 0..4 picks"
                             " the trial whose measured valley-entry v^2 lands in-band; rev <= 1"
                             " tolerates the documented single climb-and-return after the far-"
                             " wall dead end (a re-entry at low energy is then captured - no"
                             " limit cycle)"
                          << (okD6 ? QString()
                                   : QStringLiteral("diag no in-band trial, see [t1019 diag] d"));
        if (!okG7) ++totalFail;
        qInfo().noquote() << (okG7 ? "PASS" : "FAIL")
                          << "| t1019(e/f) golden-rail valley legs (review0906 #7): the valley-cell"
                             " exemption used to fire on GoldenRail regardless of power state,"
                             " while the t939 (3) unpowered-brake lives only in the |speed| < 1e-3"
                             " static branch - a COASTING cart on an unpowered golden rail in a V"
                             " valley was handed back the near-conservative valley physics plus"
                             " both wall re-pump kicks: the t981 reciprocation symptom revived on"
                             " unpowered golden valleys. The exemption now requires"
                             " GoldenRailStateOnFlag (the same bit the boost path reads): (e)"
                             " unpowered - the gravity cart (static-gate start, entry v^2 ~ 21,"
                             " valley-floor account 40.8 < 41.58) is captured, parks at the center"
                             " with zero reversals and stays pinned for 60 ticks (once stopped,"
                             " the t939 (3) static brake takes over = stable rest); (f) powered"
                             " control - the boost lerp takes over in the valley and the cart"
                             " passes the far wall, proving the narrowing did not swallow the"
                             " powered-rail semantics"
                          << (okG7 ? QString()
                                   : QStringLiteral("diag see [t1019 diag] e/f"));
    }

    // ── P-t944 上坡顶方块阻挡探针（MinecartManager 直编；spec「上坡处上方放方块 → 矿车被挡住不能穿墙
    //    过去（移动积分对坡向阻挡格的碰撞）」）──
    //   根因：轨态推进（stepCartAlongRail）是「轨道特权」通道 —— 只受轨连接位约束、从不读世界碰撞；
    //   上坡段车体随 railRiseAt 梯度面升高，坡顶正上方放方块（车体升高后将占据的格）被直接穿墙（对照：
    //   脱轨自由物理 tickDerailedCart 有撞墙清速）。修 = 每子步位移提交后对「含上坡升后 Y 钉定」的候选位
    //   做车体 AABB × World::collisionAABBsAt 探测（cartBodyBlockedAt），仅**上坡向位移**（本格面梯度沿
    //   行进向 >kCartSlopeGradMin，t939 同一张面同阈）启用：命中 → 二分回钳到最大自由前进位
    //   （clampRailMoveToFree）+ 速度清零（t943 钳向清速同口径）、不掉轨；下坡 / 平移的重叠不拦（用户
    //   口径「下坡方向不做额外阻挡」—— 下坡穿顶属既有低顶净空延续；平移重叠几何上不存在）。
    //   rig（EW 行 z0）：x0 低平（西死端）+ x0+1 坡格（东邻高一格）+ x0+2..x0+3 高平（东死端）；
    //   阻挡格 = 坡格正上方 (x0+1, R+1) Stone。腿：
    //   (a) 上坡被挡：骑乘 W(+X) → 停在坡下侧（x0+1 < x < x0+1.45，接触面 rise≈0.1）+ 轨态保持（Y 恒钉
    //       轨面）+ 持续 W 稳定不穿墙（20 tick 漂移 <0.05）；
    //   (b) 移除方块恢复通行：拆 Stone → 同车继续 W 驶到高平死端格心（Y = R+1+rideH）；
    //   (c) 阴性·无障碍通行不变：新鲜车无障碍从静止同驱 → 全程通行到死端（探测零误拦）；
    //   (d) 阴性·平轨隧道口同判：平轨 + 1 格净空石顶（含隧道口跨越）→ 照常穿行到死端（车体顶
    //       R+0.9125 < 天花板底 R+1.0 恒不相交 = 检查不误拦 P18 同款合法净空）；
    //   (e) 阴性·下坡不阻挡：阻挡格在场，峰上 spawn 车向西下坡 → 穿阻挡格列直达低平死端（用户口径
    //       「下坡方向不做额外阻挡」钉住，防上行闸被简化掉）；
    //   (f) 源码钉：入点锚 / 子步探测调用 / 上行闸梯度行 / 回钳调用 / 两函数定义 / 头文件声明。
    {
        // rig 选址：运行期扫描空区（t943 模式）。footprint x0-2..x0+4 × z0-1..z0+1 × kRigY-2..kRigY+3。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 6; xx + 5 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -2; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        if (x0 < 0) {
            qInfo().noquote() << "  [t944 diag] no clear rig area found";
        } else {
            const float rideH = 0.45f; // kCartRideH 镜像（P11/P12b 同款）
            // 行清空 + 布局重建（各腿互不残留；幂等清 x0-1..x0+4 × R..R+2）。
            const auto clearRow = [&]() {
                for (int i = -1; i <= 4; ++i)
                    for (int dy = 0; dy <= 2; ++dy)
                        w.setBlock(x0 + i, kRigY + dy, z0, BR::Air, 0);
            };
            const auto buildSlope = [&](bool withBlock) {
                clearRow();
                w.setBlock(x0,     kRigY,     z0, BR::Rail, 0); // 低平（西死端）
                w.setBlock(x0 + 1, kRigY,     z0, BR::Rail, 0); // 坡格（东邻高一格 → 坡面自西向东抬升）
                w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0); // 峰后高平
                w.setBlock(x0 + 3, kRigY + 1, z0, BR::Rail, 0); // 高平（东死端）
                if (withBlock)
                    w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0); // 阻挡格 = 坡格正上方
            };
            // 坡面期望（验收 Y 钉定）：x<x0+1 → R；x>x0+2 → R+1；坡格内 → R+(x-(x0+1))。
            const auto wantSurf = [&](float x) {
                if (x < float(x0 + 1)) return float(kRigY);
                if (x > float(x0 + 2)) return float(kRigY + 1);
                return float(kRigY) + (x - float(x0 + 1));
            };
            // (a) 上坡被挡（阻挡格在场）。
            buildSlope(true);
            MinecartManager carts;
            carts.spawnCart(x0, kRigY, z0, &w);
            bool mounted = carts.tryMount(QVector3D(float(x0) + 0.5f, float(kRigY) + 2.0f,
                                                    float(z0) + 0.5f),
                                          QVector3D(0, -1, 0), 4.0f);
            bool yOkA = true;
            QVector3D cp;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOkA = false; break; }
            }
            const QVector3D stuck = carts.posAt(0);
            QVector3D stuck2 = stuck;
            for (int t = 0; t < 20; ++t) { // 持续 W 阻挡稳定（不穿墙不漂移）
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                stuck2 = carts.posAt(0);
            }
            okA = mounted && yOkA
                && stuck.x() > float(x0 + 1) && stuck.x() < float(x0 + 1) + 0.45f // 坡下侧
                && std::fabs(stuck.y() - (wantSurf(stuck.x()) + rideH)) < 0.02f   // 轨态保持
                && (stuck2 - stuck).length() < 0.05f                              // 不穿墙
                && std::fabs(carts.pitchAt(0)) <= 45.5f;
            if (!okA)
                qInfo().noquote() << "  [t944 diag] a mounted" << mounted << "yOk" << yOkA
                                  << "stuck" << stuck << "drift" << (stuck2 - stuck).length()
                                  << "pitch" << carts.pitchAt(0);
            // (b) 移除方块 → 恢复通行（同车继续 W）。
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Air, 0);
            bool yOkB = true;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOkB = false; break; }
            }
            const QVector3D finB = carts.posAt(0);
            okB = yOkB
                && std::fabs(finB.x() - float(x0 + 3) - 0.5f) < 0.05f
                && std::fabs(finB.y() - float(kRigY + 1) - rideH) < 0.02f;
            if (!okB)
                qInfo().noquote() << "  [t944 diag] b yOk" << yOkB << "fin" << finB;
            // (c) 阴性·无障碍通行不变：新鲜车（同 rig 已无阻挡格）从静止同驱全程通行。
            carts.hitCartFromRay(QVector3D(finB.x(), finB.y() + 3.0f, finB.z()),
                                 QVector3D(0, -1, 0), 4.0f, &w, true);
            carts.spawnCart(x0, kRigY, z0, &w);
            const bool mountedC = carts.tryMount(QVector3D(float(x0) + 0.5f, float(kRigY) + 2.0f,
                                                           float(z0) + 0.5f),
                                                 QVector3D(0, -1, 0), 4.0f);
            bool yOkC = true;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOkC = false; break; }
            }
            const QVector3D finC = carts.posAt(0);
            okC = mountedC && yOkC
                && std::fabs(finC.x() - float(x0 + 3) - 0.5f) < 0.05f
                && std::fabs(finC.y() - float(kRigY + 1) - rideH) < 0.02f;
            if (!okC)
                qInfo().noquote() << "  [t944 diag] c mounted" << mountedC << "yOk" << yOkC
                                  << "fin" << finC;
            carts.clearAll();
            // (d) 阴性·平轨隧道口同判：平轨（x0-1 露天引道 + x0..x0+3 石顶下）→ 照常穿行到东死端。
            clearRow();
            w.setBlock(x0 - 1, kRigY, z0, BR::Rail, 0);     // 露天引道（隧道口西侧）
            for (int i = 0; i <= 3; ++i) {
                w.setBlock(x0 + i, kRigY,     z0, BR::Rail, 0);  // 平轨
                w.setBlock(x0 + i, kRigY + 1, z0, BR::Stone, 0); // 1 格净空石顶（P18 同款）
            }
            carts.spawnCart(x0 - 1, kRigY, z0, &w);
            const bool mountedD = carts.tryMount(QVector3D(float(x0 - 1) + 0.5f, float(kRigY) + 2.0f,
                                                           float(z0) + 0.5f),
                                                 QVector3D(0, -1, 0), 4.0f);
            bool yOkD = true;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (float(kRigY) + rideH)) > 0.02f) { yOkD = false; break; }
            }
            const QVector3D finD = carts.posAt(0);
            okD = mountedD && yOkD
                && std::fabs(finD.x() - float(x0 + 3) - 0.5f) < 0.05f // 穿过隧道口到东死端（未被拦）
                && std::fabs(finD.y() - (float(kRigY) + rideH)) < 0.02f;
            if (!okD)
                qInfo().noquote() << "  [t944 diag] d mounted" << mountedD << "yOk" << yOkD
                                  << "fin" << finD;
            carts.clearAll();
            // (e) 阴性·下坡不阻挡（阻挡格在场）：峰上 spawn 车向西下坡 → 穿阻挡格列直达低平死端。
            buildSlope(true);
            carts.spawnCart(x0 + 3, kRigY + 1, z0, &w); // 单端连接（西）→ spawn 定向 -X 下坡向
            const bool mountedE = carts.tryMount(QVector3D(float(x0 + 3) + 0.5f, float(kRigY + 1) + 2.0f,
                                                           float(z0) + 0.5f),
                                                 QVector3D(0, -1, 0), 4.0f);
            bool yOkE = true;
            for (int t = 0; t < 400; ++t) {
                carts.tickRiddenCart(0.016, &w, -1.0f, 0.0f, cp); // 持续 W 向西（下坡）
                carts.tickPushedCarts(0.016, &w);
                if (std::fabs(cp.y() - (wantSurf(cp.x()) + rideH)) > 0.02f) { yOkE = false; break; }
            }
            const QVector3D finE = carts.posAt(0);
            okE = mountedE && yOkE
                && finE.x() < float(x0 + 1)                                        // 已穿过阻挡格列
                && std::fabs(finE.x() - float(x0) - 0.5f) < 0.05f                  // 低平死端格心
                && std::fabs(finE.y() - (float(kRigY) + rideH)) < 0.02f;
            if (!okE)
                qInfo().noquote() << "  [t944 diag] e mounted" << mountedE << "yOk" << yOkE
                                  << "fin" << finE;
            carts.clearAll();
            // 清场。
            clearRow();
            tickN(w, 2);
        }
        // (f) 源码钉（t939/t940 模式：字符串钉，任一消失即红）。
        const QString exeDir944 = QCoreApplication::applicationDirPath();
        const QString root944 = QDir(exeDir944 + QStringLiteral("/..")).absolutePath();
        auto readSrc944 = [&root944](const QString &rel) -> QString {
            QFile f(root944 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc944 = readSrc944(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const QString mc944h = readSrc944(QStringLiteral("src/Entities/minecartmanager.h"));
        // review0830 #4 适配（依据：anchorFree 一职两用拆分为 embeddedAtEntry / lastFreePos，回钳 lo
        //   端改用最近自由位——原 okF1「bool anchorFree = !cartBodyBlockedAt(...)」与 okF4
        //   「clampRailMoveToFree(c, world, preX, preZ)」两行源形态随之演化；钉的是同一探测/回钳语义
        //   的修后形态，其余五钉原样保留）。
        const bool okF1 = mc944.contains(QStringLiteral(
            "const bool embeddedAtEntry = cartBodyBlockedAt(anchorProbe, world);"));
        const bool okF2 = mc944.contains(QStringLiteral(
            "const bool blocked = cartBodyBlockedAt(probe, world, &pry);"));
        const bool okF3 = mc944.contains(QStringLiteral(
            "&& cartRailGradient(world, probe.pos, pry, tx, tz, grad)"));
        const bool okF4 = mc944.contains(QStringLiteral(
            "clampRailMoveToFree(c, world, lastFreePos.x(), lastFreePos.z());"));
        const bool okF5 = mc944.contains(QStringLiteral(
            "bool MinecartManager::cartBodyBlockedAt(Cart &probe, World *world, int *outRailY)"));
        const bool okF6 = mc944.contains(QStringLiteral(
            "void MinecartManager::clampRailMoveToFree(Cart &c, World *world, float preX, float preZ)"));
        const bool okF7 = mc944h.contains(QStringLiteral(
            "bool cartBodyBlockedAt(Cart &probe, World *world, int *outRailY = nullptr);"));
        okF = okF1 && okF2 && okF3 && okF4 && okF5 && okF6 && okF7;
        const bool okT944 = okA && okB && okC && okD && okE && okF;
        if (!okT944) ++totalFail;
        if (!okT944)
            qInfo().noquote() << "  [t944 diag] a" << okA << "b" << okB << "c" << okC
                              << "d" << okD << "e" << okE
                              << "| f" << okF1 << okF2 << okF3 << okF4 << okF5 << okF6 << okF7
                              << "| srcLen cpp" << mc944.size() << "h" << mc944h.size();
        qInfo().noquote() << (okT944 ? "PASS" : "FAIL")
                          << "| t944 block above an uphill rail stops the cart (no more wall-phasing"
                             " through slope-top blocks): the rail-mode integrator was a privilege lane"
                             " constrained only by rail connections, so a cart climbing a gradient"
                             " surface phased straight through a block placed over the slope. Fix"
                             " probes the committed substep position (cart AABB vs world collision"
                             " sub-AABBs, Y pinned to the candidate's rail surface so the raised body"
                             " cell is naturally covered) and gates the block to UPHILL travel only"
                             " (same-surface gradient along travel > the t939 threshold) - downhill and"
                             " flat overlaps stay unblocked per the user's caliber (low-headroom descent"
                             " semantics kept; flat overlaps do not geometrically exist). Probe legs:"
                             " (a) climbing into a block over the slope stops the cart on the lower"
                             " flank, rail-locked with Y pinned to the surface, stable under continued W"
                             " (no wall-pass); (b) removing the block resumes the traverse to the upper"
                             " dead-end; (c) negative: an obstacle-free build traverses unchanged from a"
                             " standing start; (d) negative: a flat 1-clearance tunnel mouth (P18"
                             " geometry) is passed through - the cart top 0.9125 never meets a ceiling"
                             " bottom at 1.0; (e) negative: with the block present a crest-spawned cart"
                             " descends straight through the block column to the low dead-end (no"
                             " downhill blocking); (f) source pins for the entry anchor, the substep"
                             " probe call, the uphill gradient gate, the clamp call, both helper"
                             " definitions, and the header declaration"
                          ;
    }

    // ── P-r0830B review-2026-08-30 批 B（矿车 / 红石中 #2/#3/#4 + 低 #8/#9/#10/#11/#12）探针 ──
    //   七修一登（审查建议照单全收），每腿回退对应修法即红：
    //   (a) #2（中）t942 垂直供电种子盲区：源悬于种子轨**正上方**、链向下爬坡延伸 ×8 —— 链邻轨与被炸
    //       编辑格竖差 -2（水平 4 轴三高探针窗外，isReceivingPower 却按 6 正交邻读源 = 合法直供几何）
    //       → 炸源 → 1 tick 全灭（旧版：种子轨只经锚点 6 邻以 receivers 身份熄灭、链靠 t704 翻转波前
    //       逐 tick 收缩 = t1 仍 7 亮）。修 = dirtyGoldenRailChainFrom 非轨编辑格补探 (x, y±1, z) 垂直
    //       种子（轨链永不垂直延伸，仅发现步需要 ±Y）。几何取「源上 + 链下行」形态：源在种子**下方**
    //       的镜像形态会连带种子轨失撑坍落（t733 坍落自身触发轨编辑走查而掩盖盲区），上行镜像对称。
    //   (b) #3（中）t943 载人静置车不受断电金轨刹车闸：断电金轨坡上空车停稳（t939 闸锚）→ 上客无输入
    //       → 位移 ≈0（旧版 coasting 下坡分支从 0 积分直接开溜 ≈1 格 = 红）+ 有输入仍可推行腿（断电
    //       刹车但推得动 —— W 走 else 分支不受闸影响）。机制等价 MC 1.0 断电 powered rail 刹车。
    //   (c) #4（中）t944 anchorFree 一职两用（审查场景的可达化）：骑乘 W 爬单格坡 S（+X 邻轨高一层、
    //       S 正上方石块），车一越坡底（x≥S+0.06）即拆 S 的 -X 侧轨 —— 后向梯度采样列失轨（失联窗）
    //       → 全程车不得越过石列（旧版：失联子步无条件翻 anchorFree=false 并保留重叠提交位 → 下一 tick
    //       入点探测判嵌入 → 整 tick 逃逸豁免 → 穿墙爬上东臂 = 红）。修 = 拆 embeddedAtEntry（本 tick
    //       常量豁免开关）/ lastFreePos（回钳 lo 端），失联子步回退 pre 不保留重叠位。
    //   (d) #8（低）t939 梯度采样预筛：平地静车梯度采样计数 = 0（4 邻三高探针任一 ±1 才进采样；
    //       旧版平地车每 tick 恒 +1 = 红）。
    //   (e) #9（低）t940 吸附防御先验证后写入：源码钉 staged 副本提交形态（旧「先写真实车后 pinCartY
    //       验证」形态绝迹；防御失败零写入）。
    //   (f) #10（低）t942 Phase A2 走查收窄：拆除无源粉线端格（邻粉连接位单独翻转、电力位不变）→
    //       走查计数 delta 恰 1（仅 notePowerWrite 编辑走查；旧版邻粉 A2 再 +1 = 红）+ 对照腿：贴线端
    //       放红石块（电力位变化）→ delta ≥2（收窄不破电平沿走查）。
    //   (g) #11（低）t943 有输入分支补 t939 梯度覆盖：W 驱动西行下单格坡面（层差读 0 的口径劈叉面）
    //       → 速度吃 slopeDownAuto 供能下探（阈值钉；旧版裸邻轨层差在坡面上向平侧回 8 巡航 = 红）。
    //   (h) #12（低）t943 链可达闸「下线并行轨过拒」登记注释：源码钉（纯注释登记，注释消失即红）。
    {
        bool okA = false, okB = false, okC = false, okD = false, okE = false,
             okF = false, okG = false, okH = false;
        const QString exeDirRb = QCoreApplication::applicationDirPath();
        const QString rootRb = QDir(exeDirRb + QStringLiteral("/..")).absolutePath();
        auto readSrcRb = [&rootRb](const QString &rel) -> QString {
            QFile f(rootRb + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString wcRb = readSrcRb(QStringLiteral("src/World/world.cpp"));
        const QString mcRb = readSrcRb(QStringLiteral("src/Entities/minecartmanager.cpp"));

        // ── (a) #2 垂直供电种子：源上链下行 ×8 → 炸源 → 1 tick 全灭 ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 4; xx + 8 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -2; dx <= 8 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -9; dy <= 3 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] a: no clear rig area found";
            } else {
                const auto railOnA = [&](int x, int y, int z) {
                    return (w.stateAt(x, y, z) & BR::GoldenRailStateOnFlag) != 0;
                };
                w.setBlock(x0, kRigY - 1, z0, BR::Stone, 0);             // 种子轨支撑
                for (int i = 1; i <= 7; ++i)
                    w.setBlock(x0 + i, kRigY - i - 1, z0, BR::Stone, 0); // 链轨支撑（链向下爬坡）
                w.setBlock(x0, kRigY + 1, z0, BR::RedstoneBlock, 0);     // 悬浮源：种子轨正上方（6 正交直供）
                w.setBlock(x0, kRigY, z0, BR::GoldenRail, 0);            // 种子轨
                for (int i = 1; i <= 7; ++i)
                    w.setBlock(x0 + i, kRigY - i, z0, BR::GoldenRail, 0);
                tickN(w, 8);
                int lit0 = 0;
                for (int i = 0; i <= 7; ++i) lit0 += railOnA(x0 + i, kRigY - i, z0);
                const auto dv = w.destroySphereSilent(x0, kRigY + 1, z0, 0.9f); // 只炸源
                const bool srcGone = w.blockAt(x0, kRigY + 1, z0) == quint8(BR::Air);
                int railsLeft = 0;
                for (int i = 0; i <= 7; ++i)
                    railsLeft += w.blockAt(x0 + i, kRigY - i, z0) == quint8(BR::GoldenRail);
                tickN(w, 1); // 「下一 tick 全灭」——整链同 tick 入脏、一次 pass
                int lit1 = 0;
                for (int i = 0; i <= 7; ++i) lit1 += railOnA(x0 + i, kRigY - i, z0);
                okA = lit0 == 8 && int(dv.size()) == 1 && srcGone && railsLeft == 8 && lit1 == 0;
                if (!okA)
                    qInfo().noquote() << "  [r0830B diag] a lit0" << lit0 << "dv" << int(dv.size())
                                      << "srcGone" << srcGone << "railsLeft" << railsLeft
                                      << "lit1" << lit1;
                w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
                for (int i = 0; i <= 7; ++i) w.setBlock(x0 + i, kRigY - i, z0, BR::Air, 0);
                for (int i = 0; i <= 7; ++i) w.setBlock(x0 + i, kRigY - i - 1, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // ── (b) #3 断电金轨刹车闸载人半边 ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 6; xx + 4 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -1; dx <= 4 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -2; dy <= 3 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] b: no clear rig area found";
            } else {
                // 断电金轨坡（无任何源）：金平（西死端）+ 金坡格（东邻高一层）+ 普通轨高平 ×2。
                //   轨不垫支撑（P-t944 rig 同款）：支撑石会落在爬坡车尾的 AABB 扫掠带内（t944 上坡闸
                //   对「有垫真实坡」的中段回钳面），与本题（刹车闸 / 推行）无关的几何一并排除。
                w.setBlock(x0,     kRigY,     z0, BR::GoldenRail, 0); // 西死端金平
                w.setBlock(x0 + 1, kRigY,     z0, BR::GoldenRail, 0); // 金坡格（东邻 +1）
                w.setBlock(x0 + 2, kRigY + 1, z0, BR::Rail, 0);
                w.setBlock(x0 + 3, kRigY + 1, z0, BR::Rail, 0);
                tickN(w, 2);
                const bool poweredOff = (w.stateAt(x0 + 1, kRigY, z0) & BR::GoldenRailStateOnFlag) == 0;
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0, &w);
                const float ex0 = carts.posAt(0).x();
                for (int t = 0; t < 100; ++t) carts.tickPushedCarts(0.016f, &w); // 空车静置（t939 闸锚）
                const float emptyDx = std::fabs(carts.posAt(0).x() - ex0);
                const bool mounted = carts.tryMount(
                    QVector3D(float(x0 + 1) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f),
                    QVector3D(0, -1, 0), 4.0f);
                QVector3D cp;
                const float mx0 = carts.posAt(0).x();
                for (int t = 0; t < 300; ++t) { // 上客无输入 → 刹车闸停驻
                    carts.tickRiddenCart(0.016, &w, 0.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016f, &w);
                }
                const float rideDx = std::fabs(carts.posAt(0).x() - mx0);
                for (int t = 0; t < 400; ++t) { // 有输入仍可推行（W 东行爬坡）
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016f, &w);
                }
                const float pushDx = carts.posAt(0).x() - mx0;
                okB = poweredOff && mounted && emptyDx <= 0.02f && rideDx <= 0.05f && pushDx > 0.5f;
                if (!okB)
                    qInfo().noquote() << "  [r0830B diag] b pow" << poweredOff << "mounted" << mounted
                                      << "emptyDx" << emptyDx << "rideDx" << rideDx
                                      << "pushDx" << pushDx;
                carts.clearAll();
                for (int i = 0; i <= 3; ++i) w.setBlock(x0 + i, kRigY + (i >= 2 ? 1 : 0), z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // ── (c) #4 anchorFree 一职两用（坡底拆轨失联窗 → 不得穿墙） ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 6; xx + 4 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -4; dx <= 4 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -2; dy <= 3 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] c: no clear rig area found";
            } else {
                const float rideH = 0.45f;
                // 单格坡 rig：西引道 ×3 + 坡格 S（东邻高一层）+ 东臂高平 ×3；阻挡格 = S 正上方石块。
                for (int i = -3; i <= 0; ++i) {
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
                    w.setBlock(x0 + i, kRigY,     z0, BR::Rail, 0);
                }
                for (int i = 1; i <= 3; ++i) {
                    w.setBlock(x0 + i, kRigY,     z0, BR::Stone, 0);
                    w.setBlock(x0 + i, kRigY + 1, z0, BR::Rail, 0);
                }
                w.setBlock(x0, kRigY + 1, z0, BR::Stone, 0); // 阻挡格
                MinecartManager carts;
                carts.spawnCart(x0 - 2, kRigY, z0, &w);
                const bool mounted = carts.tryMount(
                    QVector3D(float(x0 - 2) + 0.5f, float(kRigY) + 2.0f, float(z0) + 0.5f),
                    QVector3D(0, -1, 0), 4.0f);
                QVector3D cp;
                bool replaced = false;
                float maxX = carts.posAt(0).x();
                for (int t = 0; t < 600; ++t) {
                    carts.tickRiddenCart(0.016, &w, 1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016f, &w);
                    const float px = carts.posAt(0).x();
                    if (px > maxX) maxX = px;
                    // 车一越坡底（fx ≥ 0.06）即拆 S 的 -X 侧轨 = 坡底无轨列（后向梯度探针失联窗）。
                    if (!replaced && px >= float(x0) + 0.06f) {
                        w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                        replaced = true;
                    }
                }
                // 终位贴坡面（y 随 x 走 S 的 fx 线性面 —— 车停在坡下侧低段，非平轨高）。
                const float finX = carts.posAt(0).x();
                float faceRise = finX - float(x0);
                if (faceRise < 0.0f) faceRise = 0.0f;
                if (faceRise > 1.0f) faceRise = 1.0f;
                okC = mounted && replaced && maxX < float(x0) + 0.45f && carts.aliveAt(0)
                    && std::fabs(carts.posAt(0).y()
                                 - (float(kRigY) + faceRise + rideH)) < 0.03f;
                if (!okC)
                    qInfo().noquote() << "  [r0830B diag] c mounted" << mounted << "replaced" << replaced
                                      << "maxX" << maxX << "fin" << carts.posAt(0);
                carts.clearAll();
                for (int i = -3; i <= 0; ++i) {
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                    w.setBlock(x0 + i, kRigY,     z0, BR::Air, 0);
                }
                for (int i = 1; i <= 3; ++i) {
                    w.setBlock(x0 + i, kRigY,     z0, BR::Air, 0);
                    w.setBlock(x0 + i, kRigY + 1, z0, BR::Air, 0);
                }
                w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // ── (d) #8 平地静车梯度采样计数 = 0 ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 6; xx + 3 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -1; dx <= 3 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -2; dy <= 2 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] d: no clear rig area found";
            } else {
                for (int i = 0; i <= 2; ++i) {
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
                    w.setBlock(x0 + i, kRigY,     z0, BR::Rail, 0);
                }
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0, &w);
                const int g0 = carts.gradientProbeCount();
                const float dx0 = carts.posAt(0).x();
                for (int t = 0; t < 120; ++t) carts.tickPushedCarts(0.016f, &w);
                okD = carts.gradientProbeCount() == g0
                    && std::fabs(carts.posAt(0).x() - dx0) <= 0.02f; // 静置语义不变
                if (!okD)
                    qInfo().noquote() << "  [r0830B diag] d g" << (carts.gradientProbeCount() - g0)
                                      << "dx" << std::fabs(carts.posAt(0).x() - dx0);
                carts.clearAll();
                for (int i = 0; i <= 2; ++i) {
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                    w.setBlock(x0 + i, kRigY,     z0, BR::Air, 0);
                }
                tickN(w, 2);
            }
        }
        // ── (e)(h) 源码钉：#9 staged 提交形态 + #12 链可达闸翻案清算注（t983 以目标层精确解取代登记取舍）──
        okE = mcRb.contains(QStringLiteral("Cart staged = c;"))
           && mcRb.contains(QStringLiteral("const int pinnedY = pinCartY(staged, world);"))
           && mcRb.contains(QStringLiteral("\n    c = staged;"))
           && !mcRb.contains(QStringLiteral("if (pinnedY < 0) return false; // 防御（同层闸已验轨在列，此处失败 = 吸附瞬间轨被拆 / 列扫失效）：\n                                   //   保持 derailed 自由物理"));
        okH = mcRb.contains(QStringLiteral("review0830-B #12"))
           && mcRb.contains(QStringLiteral("头顶并行"));
        // ── (f) #10 Phase A2 走查收窄（连接位单独翻转零走查） ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 6; xx + 4 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -1; dx <= 4 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -2; dy <= 2 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] f: no clear rig area found";
            } else {
                for (int i = 0; i <= 3; ++i) {
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
                    w.setBlock(x0 + i, kRigY,     z0, BR::RedstoneDust, 0); // 无源粉线 ×4
                }
                tickN(w, 8); // 沉降（初始连接位写入的走查全部发生在快照前）
                const quint8 connBefore = quint8(w.stateAt(x0 + 2, kRigY, z0) & 0xF0);
                const int w0 = w.railChainWalkCount();
                w.setBlock(x0 + 3, kRigY, z0, BR::Air, 0); // 拆东端粉：邻粉连接位单独翻转、电力不变
                tickN(w, 2);
                const int w1 = w.railChainWalkCount();
                const quint8 connAfter = quint8(w.stateAt(x0 + 2, kRigY, z0) & 0xF0);
                // 对照：电力位变化仍走查（收窄不破 t942 ③ 电平沿语义）。
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                tickN(w, 3);
                const int w2 = w.railChainWalkCount();
                okF = (w1 - w0) == 1 && connBefore != connAfter && (w2 - w1) >= 2;
                if (!okF)
                    qInfo().noquote() << "  [r0830B diag] f dw1" << (w1 - w0) << "connChg"
                                      << (connBefore != connAfter) << "dw2" << (w2 - w1);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                for (int i = 0; i <= 2; ++i) {
                    w.setBlock(x0 + i, kRigY,     z0, BR::Air, 0);
                    w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
                }
                tickN(w, 2);
            }
        }
        // ── (g) #11 有输入分支梯度覆盖（下坡单格坡面吃 slopeDownAuto 供能） ──
        {
            int x0 = -1, z0 = -1;
            for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
                for (int xx = 8; xx + 6 < 96 && x0 < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -3; dx <= 6 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -2; dy <= 3 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { x0 = xx; z0 = zz; }
                }
            if (x0 < 0) {
                qInfo().noquote() << "  [r0830B diag] g: no clear rig area found";
            } else {
                // 高平 ×6（R+1）+ 单格下坡面 B（R，西引道 R）：车从高平东端 W 西行，过坡面时层差读 0
                //   （西邻同层引道）——梯度覆盖认坡（slopeDownAuto 供能下探），裸层差不认（回 8 巡航）。
                w.setBlock(x0 - 2, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 - 1, kRigY - 1, z0, BR::Stone, 0);
                w.setBlock(x0 - 2, kRigY,     z0, BR::Rail, 0); // 西引道 A
                w.setBlock(x0 - 1, kRigY,     z0, BR::Rail, 0); // 下坡单格坡面 B（东邻 +1 → 本格面西倾）
                for (int i = 0; i <= 5; ++i) {
                    w.setBlock(x0 + i, kRigY,     z0, BR::Stone, 0);
                    w.setBlock(x0 + i, kRigY + 1, z0, BR::Rail, 0); // 高平 ×6
                }
                MinecartManager carts;
                carts.spawnCart(x0 + 5, kRigY + 1, z0, &w);
                const bool mounted = carts.tryMount(
                    QVector3D(float(x0 + 5) + 0.5f, float(kRigY + 1) + 2.0f, float(z0) + 0.5f),
                    QVector3D(0, -1, 0), 4.0f);
                QVector3D cp;
                float vMin = 0.0f;
                float prevX = carts.posAt(0).x();
                for (int t = 0; t < 500; ++t) {
                    carts.tickRiddenCart(0.016, &w, -1.0f, 0.0f, cp);
                    carts.tickPushedCarts(0.016f, &w);
                    const float p = carts.posAt(0).x();
                    const float v = (p - prevX) / 0.016f;
                    if (v < vMin) vMin = v;
                    prevX = p;
                }
                okG = mounted && vMin <= -8.65f && carts.posAt(0).x() < float(x0 - 1);
                if (!okG)
                    qInfo().noquote() << "  [r0830B diag] g mounted" << mounted << "vMin" << vMin
                                      << "fin" << carts.posAt(0);
                carts.clearAll();
                w.setBlock(x0 - 2, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY - 1, z0, BR::Air, 0);
                w.setBlock(x0 - 2, kRigY,     z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY,     z0, BR::Air, 0);
                for (int i = 0; i <= 5; ++i) {
                    w.setBlock(x0 + i, kRigY,     z0, BR::Air, 0);
                    w.setBlock(x0 + i, kRigY + 1, z0, BR::Air, 0);
                }
                tickN(w, 2);
            }
        }
        const bool okR0830B = okA && okB && okC && okD && okE && okF && okG && okH;
        if (!okR0830B) ++totalFail;
        if (!okR0830B)
            qInfo().noquote() << "  [r0830B diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE << "f" << okF << "g" << okG << "h" << okH;
        qInfo().noquote() << (okR0830B ? "PASS" : "FAIL")
                          << "| review0830 batch B (carts/redstone #2 #3 #4 #8 #9 #10 #11 #12):"
                             " (a) a source floating DIRECTLY ABOVE its seed rail with the chain"
                             " descending off it puts the first chain rail 2 layers below the"
                             " destroyed cell - outside the 4-axis three-height probe window while"
                             " isReceivingPower legally reads the 6-orthogonal feed - so the old"
                             " discovery found nothing and the chain shrank one rail per tick via"
                             " the flip wavefront (t1 still 7 lit); the vertical-seed probe (x,"
                             " y+-1, z) for non-rail edit cells now dirties the whole chain for a"
                             " one-pass shutdown (source-below mirror rejected: the t733 support"
                             " collapse of the seed rail would itself chain-walk and mask the"
                             " blind spot); (b) a parked EMPTY cart on a de-powered golden slope"
                             " holds (t939 brake anchor), but MOUNTING it used to route the"
                             " stationary ride through the coasting slope integrator which"
                             " started it rolling from zero (the t939 half-fix); the same gate"
                             " now zeroes the ridden stationary cart while W input still pushes"
                             " it (brake but pushable); (c) climbing a single-block slope under"
                             " a stone with the rail BEHIND the cart torn out mid-climb (the"
                             " review's rail-less column, made reachable) loses the backward"
                             " gradient sample: the old code flipped anchorFree and KEPT the"
                             " overlapping commit, so the next tick's embedded entry exempted"
                             " the whole tick and the cart phased through the stone onto the"
                             " upper arm; the split embeddedAtEntry/lastFreePos reverts the lost"
                             " substep instead - the cart never crosses the block column; (d) a"
                             " stationary cart on flat plain rail makes ZERO gradient samples"
                             " (cheap 4-neighbor +-1-layer prefilter; was one ~30-blockAt sweep"
                             " per tick); (e) source pins for the snap staging form (verify in a"
                             " local copy, commit once); (f) tearing the end dust off a source-"
                             " less dust line flips the neighbor's connection bits with power"
                             " unchanged and the chain-walk counter moves by exactly 1 (the"
                             " notePowerWrite edit walk only; was 2 with the wide A2 trigger),"
                             " while feeding the line a redstone block still walks (>=2); (g)"
                             " driving W west down a single-block slope face whose layer diff"
                             " reads 0: the gradient overlay now engages slopeDownAuto on the"
                             " input branch (speed dips past the -8 cruise target; was pinned at"
                             " cruise); (h) source pin for the chain-reachability gate's"
                             " registered under-line parallel-track tradeoff comment"
                          ;
    }

    // ── P-t945 仙人掌旁放铁轨（玩家放置全链）探针 ──
    //   用户第五轮实测「仙人掌旁放铁轨放不了」——t911 只钉了 World 直编层（P-t911 探针经 w.setBlock 直写，
    //   天然绕过 PlayerController::placeBlock 的放置预检链），玩家真实路径（射线 → 预检 → setBlock）此前无
    //   行为级覆盖。本探针直编 PlayerController（t814 真消费端模式 + review27-8 挂窗 grab 载体）走**完整
    //   放置链**：loadSavedState 定位/定向 → tick 刷射线 → setSelectedBlock(Rail) → placeBlock。
    //   **t984 口径翻案**（用户 9-01「我的口径是能放下来，而不是仙人掌会掉落，你之前一直都做错了」）：
    //   (a)(b)(e)(f) 的期望从「放置成功 + 整柱坍落」翻转为「放置成功 + 仙人掌无恙 + 零掉落」；(g) 石头
    //   （整立方 isFullCube）邻接仍照旧整柱坍落——既有语义不回归钉。
    //   断言八段：
    //   (a) 地面顶面瞄准（瞄仙人掌旁地面 → 目标 = 地面上方气格，贴仙人掌柱基）→ 放置成功 + 仙人掌两格原样
    //       + 零掉落 + 铁轨留存；
    //   (b) 仙人掌基座侧面瞄准（瞄 0.8 细柱选中面 → 目标 = 侧邻气格，同贴柱基）→ 同 (a)；
    //   (c) 阴性·无支撑悬空轨位照旧拒（瞄 2 高柱**上层**侧面 → 目标下方 Air → 轨预检②拒）：放置不发生、
    //       仙人掌无恙（放置被拒不触发邻接坍落——仙人掌坍落不是非法放置的免死金牌，用户定稿口径）；
    //   (d) 阴性·空场悬空放轨照旧拒（无仙人掌镜像对照，钉轨支撑语义本身）；
    //   (e) 生存模式全链（真实游玩口径）：hotbar 铁轨栈放置 + 消耗 1 件（t669）+ 仙人掌无恙；
    //   (f) 对称面·火把贴柱旁 → 放置成功 + 仙人掌无恙 + 零掉落（非实体族同口径）；
    //   (g) 对称面·石头挤占柱旁 → 放置成功 + 整柱坍落（t984 后完整实体方块邻接仍触发 ④，钉口径防漂移）；
    //   (h) 源码钉：placeBlock 铁轨预检块 + checkCactusOnEdit ④ 邻接坍落关键行（任一消失即红）。
    {
        // rig 选址：kRigY 高空全空盒扫描（同 t911 模式；dx -1..7、dz -1..1、dy -2..+4）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 7 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 7 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t945 rail-adjacent cactus player-path place: no clear rig area found";
        } else {
            WorldClock clockP945;
            EntityManager entsP945; // 空管理器（无 mob / 无掉落物；放置链不依赖）
            Hotbar hbP945;
            PlayerController pcP945; // t814 真消费端模式（review27-8 挂窗 grab 载体；headless 无指针锁）
            pcP945.setWorld(&w);
            pcP945.setWorldClock(&clockP945);
            pcP945.setEntityManager(&entsP945);
            pcP945.setHotbar(&hbP945);
            QQuickWindow probeWinP945;
            pcP945.setParentItem(probeWinP945.contentItem());
            pcP945.grab(); // m_window 就绪 → setCaptured(true) 走通（placeBlock 入口门）
            pcP945.setSelectedBlock(int(BR::Rail));
            // 掉落计数（blockDroppedAsItem 局部连接——只数本 rig 柱附近的 Cactus 掉落）。
            int dropsP945 = 0;
            const QMetaObject::Connection dropConnP945 = QObject::connect(
                &w, &World::blockDroppedAsItem, &w,
                [&dropsP945, x0, z0](int bx, int, int bz, int bid) {
                    if (bid == int(BR::Cactus) && bz == z0 && bx >= x0 && bx <= x0 + 6)
                        ++dropsP945;
                });
            // rig 搭建：石台面（kRigY-1，x0..x0+5）+ 2 高仙人掌柱（x0, kRigY/kRigY+1, z0，贴柱基可站地面）。
            const auto buildRigP945 = [&]() {
                for (int dx = 0; dx <= 5; ++dx) {
                    w.setBlock(x0 + dx, kRigY - 1, z0, BR::Stone, 0);
                    for (int dy = 0; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
                }
                w.setBlock(x0, kRigY - 1, z0, BR::Sand, 0);      // 仙人掌合法沙支撑（场景保真）
                w.setBlock(x0, kRigY,     z0, BR::Cactus, 0);
                w.setBlock(x0, kRigY + 1, z0, BR::Cactus, 0);
            };
            const auto clearRigP945 = [&]() {
                for (int dx = 0; dx <= 5; ++dx)
                    for (int dy = -1; dy <= 2; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0, BR::Air, 0);
            };
            // 瞄准 + tick 刷射线：从眼位（脚位 +1.62）指向 aim 点（格面上一点），返命中格。
            //   先 release+grab：grab 的光标居中（QCursor::setPos(windowCenterGlobal)）必须紧贴本次 tick ——
            //   pollMouse 每次捕获 tick 读 QCursor::pos−窗口中心改写 yaw/pitch（headless 下窗口中心与真实
            //   光标残留位的偏差会被一次性吞成视角踢变，跨腿累积漂移）→ 每腿重居中归零 delta，保证
            //   loadSavedState 写入的 yaw/pitch 原样进 updateRaycast。
            const auto aimP945 = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ, int mode) {
                const float ex = feetX, ey = float(kRigY) + 1.62f, ez = feetZ;
                const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float pitch = std::asin(dy / len) * 57.2957795f;         // rad → deg（俯视为负）
                const float yaw = std::atan2(-dx, -dz) * 57.2957795f;          // lookDirection 约定
                pcP945.release();
                pcP945.grab(); // 重新居中光标（pollMouse delta 归零；见上注）
                pcP945.loadSavedState(feetX, float(kRigY), feetZ, yaw, pitch, mode);
                pcP945.tick(); // updateRaycast 刷新命中（tick 公共入口；t889 先例）
                return pcP945.hitBlock();
            };
            const auto pumpMsP945 = [](int ms) { // 放置 200ms CD 间隔（t128；m_evtClock 单调墙钟）
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < ms)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            };
            buildRigP945();
            // (a) 地面顶面瞄准：瞄 (x0+1.7, kRigY, z0+0.5)（= 石台顶面）→ 目标 (x0+1, kRigY, z0)。
            const QVector3D hitA = aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                                           float(x0) + 1.7f, float(kRigY), float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okA = hitA == QVector3D(float(x0 + 1), float(kRigY - 1), float(z0))
                && w.blockAt(x0 + 1, kRigY, z0) == BR::Rail          // 放置成功（轨留存）
                && w.blockAt(x0, kRigY, z0) == BR::Cactus            // 仙人掌无恙（t984：轨邻接不破坏）
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsP945 == 0;                                       // 零掉落
            if (!okA)
                qInfo().noquote() << "  [t945 diag] a hit" << hitA << "tgt"
                                  << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945;
            pumpMsP945(260);
            // (b) 仙人掌基座侧面瞄准：瞄 0.8 细柱选中面 (x0+0.9, kRigY+0.5, z0+0.5) → 目标同 (a) 格。
            clearRigP945();
            buildRigP945();
            dropsP945 = 0;
            const QVector3D hitB = aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                                           float(x0) + 0.9f, float(kRigY) + 0.5f, float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okB = hitB == QVector3D(float(x0), float(kRigY), float(z0))
                && w.blockAt(x0 + 1, kRigY, z0) == BR::Rail
                && w.blockAt(x0, kRigY, z0) == BR::Cactus            // 仙人掌无恙（t984）
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsP945 == 0;
            if (!okB)
                qInfo().noquote() << "  [t945 diag] b hit" << hitB << "tgt"
                                  << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945;
            pumpMsP945(260);
            // (c) 阴性·上层侧面（目标下方 Air → 轨预检②拒）：放置不发生、仙人掌无恙、零掉落。
            clearRigP945();
            buildRigP945();
            dropsP945 = 0;
            const QVector3D hitC = aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                                           float(x0) + 0.9f, float(kRigY) + 1.5f, float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okC = hitC == QVector3D(float(x0), float(kRigY + 1), float(z0))
                && w.blockAt(x0 + 1, kRigY + 1, z0) == BR::Air       // 拒放（悬空轨位）
                && w.blockAt(x0, kRigY, z0) == BR::Cactus            // 仙人掌无恙（拒放不触发坍落）
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsP945 == 0;
            if (!okC)
                qInfo().noquote() << "  [t945 diag] c hit" << hitC << "tgt"
                                  << int(w.blockAt(x0 + 1, kRigY + 1, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945;
            pumpMsP945(260);
            // (d) 阴性·空场悬空放轨（无仙人掌镜像对照）：瞄石柱**侧面**（命中真实发生）→ 目标 = 柱旁气格、
            //     其下方 Air → 轨预检②拒（非「射线落空」假阳性：断言命中格本身）。
            clearRigP945();
            w.setBlock(x0 + 1, kRigY - 1, z0, BR::Stone, 0); // 石柱三格（台面 + 立柱）
            w.setBlock(x0 + 1, kRigY,     z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY + 1, z0, BR::Stone, 0);
            dropsP945 = 0;
            const QVector3D hitD = aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                                           float(x0) + 2.0f, float(kRigY) + 1.5f, float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okD = hitD == QVector3D(float(x0 + 1), float(kRigY + 1), float(z0))
                && w.blockAt(x0 + 2, kRigY + 1, z0) == BR::Air; // 悬空轨位照旧拒
            if (!okD)
                qInfo().noquote() << "  [t945 diag] d hit" << hitD << "tgt"
                                  << int(w.blockAt(x0 + 2, kRigY + 1, z0));
            // (e) 生存模式全链（真实游玩口径）：hotbar 槽 0 = 铁轨 ×16 + 仙人掌侧面瞄准 → 放置成功 +
            //     仙人掌无恙（t984）+ 槽内消耗 1 件（t669 C++ 消耗收口在放置动作本体，走通即证 Survival
            //     放置链无额外拒绝）。
            clearRigP945();
            buildRigP945();
            dropsP945 = 0;
            hbP945.setStack(0, BR::Rail, 16);
            hbP945.setSelectedSlot(0);
            const QVector3D hitE = aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                                           float(x0) + 0.9f, float(kRigY) + 0.5f, float(z0) + 0.5f, 2); // 2 = Survival
            pcP945.placeBlock();
            const bool okE = hitE == QVector3D(float(x0), float(kRigY), float(z0))
                && w.blockAt(x0 + 1, kRigY, z0) == BR::Rail
                && w.blockAt(x0, kRigY, z0) == BR::Cactus            // 仙人掌无恙（t984）
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsP945 == 0
                && hbP945.countAt(0) == 15; // 生存放置消耗 1 件（t669 收口）
            if (!okE)
                qInfo().noquote() << "  [t945 diag] e hit" << hitE << "tgt"
                                  << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945
                                  << "stack" << hbP945.countAt(0);
            pumpMsP945(260);
            // (f) 对称面·火把（薄格非实体族同口径）：火把贴柱旁地面 → 放置成功 + 仙人掌无恙 + 零掉落
            //     （t984 后非实体邻接不触发 ④；预检只看支撑不看邻仙人掌——非法化「邻仙人掌」的预检不存在，
            //     轨族如此火把族亦如此）。
            clearRigP945();
            buildRigP945();
            dropsP945 = 0;
            pcP945.setSelectedBlock(int(BR::Torch));
            aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                    float(x0) + 1.7f, float(kRigY), float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okF = w.blockAt(x0 + 1, kRigY, z0) == BR::Torch
                && w.blockAt(x0, kRigY, z0) == BR::Cactus            // 仙人掌无恙（t984）
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsP945 == 0;
            if (!okF)
                qInfo().noquote() << "  [t945 diag] f tgt"
                                  << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945;
            pumpMsP945(260);
            // (g) 对称面·石头（整立方方块邻接仍坍落——t984 口径防漂移钉）：石头放柱旁 → 放置成功 +
            //     整柱坍落（review0903 #1 后 ④ 对 isFullCube 整立方照旧触发；本腿钉「只豁免非整立方族」
            //     不是「全族豁免」）。
            clearRigP945();
            buildRigP945();
            dropsP945 = 0;
            pcP945.setSelectedBlock(int(BR::Stone));
            aimP945(float(x0) + 3.5f, float(z0) + 0.5f,
                    float(x0) + 1.7f, float(kRigY), float(z0) + 0.5f, 1);
            pcP945.placeBlock();
            const bool okG = w.blockAt(x0 + 1, kRigY, z0) == BR::Stone
                && w.blockAt(x0, kRigY, z0) == BR::Air
                && w.blockAt(x0, kRigY + 1, z0) == BR::Air
                && dropsP945 == 2;
            if (!okG)
                qInfo().noquote() << "  [t945 diag] g tgt"
                                  << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsP945;
            pumpMsP945(260);
            // (h) 源码钉：放置预检轨支撑 + World ④ 邻接坍落关键行（t939/t940 字符串钉模式）。
            const QString exeDirP945 = QCoreApplication::applicationDirPath();
            const QString rootP945 = QDir(exeDirP945 + QStringLiteral("/..")).absolutePath();
            auto readSrcP945 = [&rootP945](const QString &rel) -> QString {
                QFile f(rootP945 + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString pcSrcP945 = readSrcP945(QStringLiteral("src/Game/playercontroller.cpp"));
            const QString wSrcP945 = readSrcP945(QStringLiteral("src/World/world.cpp"));
            const bool okH = pcSrcP945.contains(QStringLiteral(
                "if (!BlockRegistry::solidSupportBlock(below)) return; // ② 下方非完整立方 / 仙人掌支撑 → 拒（不挥）"))
                && wSrcP945.contains(QStringLiteral("dropCactusColumn(nx, baseY, nz);"));
            // 清场 + 释放（grab 析构配对）。
            clearRigP945();
            w.setBlock(x0 + 1, kRigY - 1, z0, BR::Air, 0);
            QObject::disconnect(dropConnP945);
            pcP945.release();
            probeWinP945.deleteLater();
            const bool okP945 = okA && okB && okC && okD && okE && okF && okG && okH;
            if (!okP945) ++totalFail;
            if (!okP945)
                qInfo().noquote() << "  [t945 diag] a" << okA << "b" << okB << "c" << okC
                                  << "d" << okD << "e" << okE << "f" << okF << "g" << okG
                                  << "h" << okH;
            qInfo().noquote() << (okP945 ? "PASS" : "FAIL")
                              << "| t945 rail placement beside a cactus succeeds through the REAL player"
                                 " placement path (raycast -> placeBlock prechecks -> setBlock) and leaves"
                                 " the cactus standing (t984 caliber): (a) aiming at the ground top beside"
                                 " the column places the rail in the adjacent ground-level cell and both"
                                 " column cells stay cactus with zero drops; (b) aiming at the cactus"
                                 " column's own 0.8 selection face resolves to the same adjacent cell"
                                 " with the same outcome; (c) negative: aiming at the upper column face"
                                 " targets a support-less cell and the placement is rejected WITHOUT"
                                 " breaking the cactus (illegal placement gets no free pass);"
                                 " (d) negative: a floating rail spot far from any cactus stays rejected"
                                 " (rail support semantics intact); (e) Survival mode end-to-end: same"
                                 " placement through a hotbar rail stack with the stack consumed by one"
                                 " (t669) and the cactus intact; (f) symmetry: a torch aimed beside the"
                                 " column also places harmlessly (non-solid families never fell the"
                                 " cactus); (g) symmetry: a solid stone beside the column still fells it"
                                 " (t984 keeps the full-cube gate - pinned against scope drift); (h) source"
                                 " pins for the rail support precheck and the adjacency collapse call";
        }
    }

    // ── P-t984 仙人掌旁放非整立方方块（行为级口径翻案）探针 ──
    //   用户 9-01 原话「我的口径是能放下来，而不是仙人掌会掉落，你之前一直都做错了」：仙人掌破坏校验
    //   （checkCactusOnEdit ④）只应被**整立方方块**（BlockRegistry::isFullCube —— shape==ShapeFull，t213
    //   单一权威谓词；review0903 #1 由 isSolid 代理修为整立方权威，与 t503 worldgen 柱 4 邻守卫同一谓词
    //   同源）的水平邻接触发；铁轨（三变体）/ 火把 / 压力板等非整立方邻接放置 → 放置成功 + 仙人掌 id 不变 +
    //   零掉落。断言七段：
    //   (a) 2 高柱四邻逐一放 Rail / GoldenRail / DetectorRail / Torch → 全部留存 + 仙人掌两格原样 + 零掉落；
    //   (b) 木 / 石压力板贴 1 高柱两侧 → 同（薄板族同口径）；
    //   (c) 对照腿·石头（整立方）贴柱 → 照旧整柱坍落（1 格 1 掉落，既有语义不回归）；
    //   (d) 对照腿·沙（整立方实体，落沙落旁同谓词路径）贴柱 → 照旧坍落（worldgen / 放置 / 挖除口径一致）；
    //   (e) 挖除链：柱旁轨留存时仙人掌无恙；挖邻轨不伤仙人掌；挖沙支撑 → ② 失撑整柱掉落（失撑链不回归）；
    //   (f) 源码钉：④ 门槛 isFullCube 行 + t503 worldgen 守卫 isFullCube 行（任一消失即红）；
    //   (g) 对照腿·玻璃（review0903 #1：solid=false 但 ShapeFull 的整立方）贴柱 → 照旧整柱坍落（isSolid
    //       代理口径下漏放 → 本腿钉「整立方才是权威」，代理门槛回潮即红）。
    {
        // rig 选址：kRigY 高空全空盒扫描（dx -1..2、dz -1..1、dy -2..+4）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 2 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 2 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t984 non-solid neighbor beside cactus: no clear rig area found";
        } else {
            int dropsT984 = 0;
            const QMetaObject::Connection dropConnT984 = QObject::connect(
                &w, &World::blockDroppedAsItem, &w,
                [&dropsT984, x0, z0](int bx, int, int bz, int bid) {
                    if (bid == int(BR::Cactus) && bz == z0 && bx >= x0 - 1 && bx <= x0 + 1)
                        ++dropsT984;
                });
            // rig 搭建：先清 3×3 的 Y/Y+1 层（前腿遗留的实心邻格会让新柱在放置瞬间被 ④ 误毁——先清后建），
            //   再铺 3×3 石台面（kRigY-1，火把 / 轨 / 板合法支撑）+ 沙基座（x0, kRigY-1）+ 仙人掌柱（h=1/2）。
            const auto buildRigT984 = [&](int h) {
                for (int dy = 1; dy >= 0; --dy) // 自顶向下清（仙人掌 oldId=Cactus 跳过 ② 失撑，无级联坍落）
                    for (int dx = -1; dx <= 1; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                            w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz)
                        w.setBlock(x0 + dx, kRigY - 1, z0 + dz, BR::Stone, 0);
                w.setBlock(x0, kRigY - 2, z0, BR::Stone, 0);     // 沙垫石：沙是重力方块，下方悬空会被重力链收走
                w.setBlock(x0, kRigY - 1, z0, BR::Sand, 0);      // 仙人掌合法沙支撑（场景保真）
                w.setBlock(x0, kRigY,     z0, BR::Cactus, 0);
                if (h >= 2) w.setBlock(x0, kRigY + 1, z0, BR::Cactus, 0);
            };
            // (a) 轨三变体 + 火把四邻逐一贴 2 高柱基座层 → 放置成功 + 仙人掌不动 + 零掉落。
            buildRigT984(2);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);          // +X 邻：铁轨
            w.setBlock(x0 - 1, kRigY, z0, BR::GoldenRail, 0);    // -X 邻：动力铁轨
            w.setBlock(x0, kRigY, z0 + 1, BR::DetectorRail, 0);  // +Z 邻：探测铁轨
            w.setBlock(x0, kRigY, z0 - 1, BR::Torch, 0);         // -Z 邻：火把
            const bool okA = w.blockAt(x0 + 1, kRigY, z0) == BR::Rail
                && w.blockAt(x0 - 1, kRigY, z0) == BR::GoldenRail
                && w.blockAt(x0, kRigY, z0 + 1) == BR::DetectorRail
                && w.blockAt(x0, kRigY, z0 - 1) == BR::Torch
                && w.blockAt(x0, kRigY,     z0) == BR::Cactus    // 仙人掌两格原样
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsT984 == 0;
            if (!okA)
                qInfo().noquote() << "  [t984 diag] a" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << int(w.blockAt(x0 - 1, kRigY, z0))
                                  << int(w.blockAt(x0, kRigY, z0 + 1))
                                  << int(w.blockAt(x0, kRigY, z0 - 1))
                                  << "c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsT984;
            // (b) 木 / 石压力板贴 1 高柱两侧 → 同口径（薄板族）。
            buildRigT984(1);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::WoodPressurePlate, 0);
            w.setBlock(x0 - 1, kRigY, z0, BR::StonePressurePlate, 0);
            const bool okB = w.blockAt(x0 + 1, kRigY, z0) == BR::WoodPressurePlate
                && w.blockAt(x0 - 1, kRigY, z0) == BR::StonePressurePlate
                && w.blockAt(x0, kRigY, z0) == BR::Cactus
                && dropsT984 == 0;
            if (!okB)
                qInfo().noquote() << "  [t984 diag] b" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << int(w.blockAt(x0 - 1, kRigY, z0))
                                  << "c" << int(w.blockAt(x0, kRigY, z0))
                                  << "drops" << dropsT984;
            // (c) 对照腿·石头（完整实体方块）贴 1 高柱 → 照旧整柱坍落（既有语义不回归）。
            buildRigT984(1);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Stone, 0);
            const bool okC = w.blockAt(x0 + 1, kRigY, z0) == BR::Stone
                && w.blockAt(x0, kRigY, z0) == BR::Air           // 仙人掌碎
                && dropsT984 == 1;
            if (!okC)
                qInfo().noquote() << "  [t984 diag] c" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c" << int(w.blockAt(x0, kRigY, z0))
                                  << "drops" << dropsT984;
            // (d) 对照腿·沙（整立方实体——落沙落旁 / worldgen 守卫同谓词）贴 1 高柱 → 照旧坍落。
            buildRigT984(1);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Sand, 0);
            const bool okD = w.blockAt(x0 + 1, kRigY, z0) == BR::Sand
                && w.blockAt(x0, kRigY, z0) == BR::Air
                && dropsT984 == 1;
            if (!okD)
                qInfo().noquote() << "  [t984 diag] d" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c" << int(w.blockAt(x0, kRigY, z0))
                                  << "drops" << dropsT984;
            // (e) 挖除链：轨贴柱仙人掌无恙 → 挖邻轨仍无恙 → 挖沙支撑 ② 失撑整柱（2 格）掉落。
            buildRigT984(2);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Rail, 0);
            bool okE = w.blockAt(x0, kRigY, z0) == BR::Cactus
                && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                && dropsT984 == 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Air, 0);           // 挖邻轨 → 仙人掌无恙（挖除链不伤）
            okE = okE && w.blockAt(x0, kRigY, z0) == BR::Cactus
                     && w.blockAt(x0, kRigY + 1, z0) == BR::Cactus
                     && dropsT984 == 0;
            w.setBlock(x0, kRigY - 1, z0, BR::Air, 0);           // 挖沙支撑 → ② 失撑整柱掉落
            okE = okE && w.blockAt(x0, kRigY, z0) == BR::Air
                     && w.blockAt(x0, kRigY + 1, z0) == BR::Air
                     && dropsT984 == 2;
            if (!okE)
                qInfo().noquote() << "  [t984 diag] e c0" << int(w.blockAt(x0, kRigY, z0))
                                  << "c1" << int(w.blockAt(x0, kRigY + 1, z0))
                                  << "drops" << dropsT984;
            // (f) 源码钉：④ 门槛 isFullCube 行 + t503 worldgen 柱 4 邻守卫 isFullCube 行（单一谓词两路径同源）。
            const QString exeDirT984 = QCoreApplication::applicationDirPath();
            const QString rootT984 = QDir(exeDirT984 + QStringLiteral("/..")).absolutePath();
            auto readSrcT984 = [&rootT984](const QString &rel) -> QString {
                QFile f(rootT984 + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString wSrcT984 = readSrcT984(QStringLiteral("src/World/world.cpp"));
            const bool okF = wSrcT984.contains(QStringLiteral(
                "if (id != BlockRegistry::Air && BlockRegistry::isFullCube(id)) {"))
                && wSrcT984.contains(QStringLiteral(
                    "BlockRegistry::isFullCube(m_chunks.blockAt(x + d[0], yy, z + d[1]))"));
            if (!okF)
                qInfo().noquote() << "  [t984 diag] f src pin miss";
            // (g) 对照腿·玻璃（review0903 #1：solid=false 但 ShapeFull 的整立方）贴 1 高柱 → 照旧坍落
            //     （isSolid 代理口径下玻璃漏放 → 本腿钉整立方权威，代理门槛回潮即红）。
            buildRigT984(1);
            dropsT984 = 0;
            w.setBlock(x0 + 1, kRigY, z0, BR::Glass, 0);
            const bool okG = w.blockAt(x0 + 1, kRigY, z0) == BR::Glass
                && w.blockAt(x0, kRigY, z0) == BR::Air
                && dropsT984 == 1;
            if (!okG)
                qInfo().noquote() << "  [t984 diag] g" << int(w.blockAt(x0 + 1, kRigY, z0))
                                  << "c" << int(w.blockAt(x0, kRigY, z0))
                                  << "drops" << dropsT984;
            // 清场：柱位自顶向下（oldId=Cactus 跳过 ② 失撑，无级联坍落）+ 3×3 台面带 + 沙下垫石。
            w.setBlock(x0, kRigY - 2, z0, BR::Air, 0);
            for (int dy = 1; dy >= 0; --dy)
                w.setBlock(x0, kRigY + dy, z0, BR::Air, 0);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
            QObject::disconnect(dropConnT984);
            tickN(w, 2);
            const bool okT984 = okA && okB && okC && okD && okE && okF && okG;
            if (!okT984) ++totalFail;
            if (!okT984)
                qInfo().noquote() << "  [t984 diag] a" << okA << "b" << okB << "c" << okC
                                  << "d" << okD << "e" << okE << "f" << okF << "g" << okG;
            qInfo().noquote() << (okT984 ? "PASS" : "FAIL")
                              << "| t984 non-full-cube neighbors beside a cactus place successfully and"
                                 " leave the cactus standing (reversed caliber per user): (a) rail,"
                                 " golden rail, detector rail and torch on the four horizontal"
                                 " neighbors of a 2-high column all stay with the cactus intact and"
                                 " zero drops; (b) wood and stone pressure plates likewise; control"
                                 " legs keep the existing semantics: (c) a full cube (stone)"
                                 " beside the column still fells it (one drop), (d) sand (a full"
                                 " cube - same predicate as the t503 worldgen guard and falling-sand"
                                 " path) still fells it; (e) dig chain: rail-adjacent cactus survives,"
                                 " digging the rail harms nothing, digging the sand support drops the"
                                 " whole 2-high column via the support-loss chain; (f) source pins"
                                 " for the isFullCube gate in checkCactusOnEdit and the t503 worldgen"
                                 " guard (one predicate, both paths); (g) glass (full cube with"
                                 " solid=false - review0903 #1) still fells it (the old isSolid proxy"
                                 " let glass slip through - pinned against regression)";
        }
    }

    // ── P-t985 仙人掌底接触阴影探针（不满格方块的光照 opacity 语义；t985）──
    //   用户实测：仙人掌放沙子上「底部沙子与仙人掌接触的部分整片变成阴影」。根因归因（skyLight 与 AO
    //   双查）：AO/PCF 侧 t849/t850 已把仙人掌排除出 heightmap（列顶实面落到沙顶，环隙无整格黑影）——
    //   病灶在光照 lightOpacity(Cactus)=15（t445「opaque 实体植物满遮」旧口径）：天光种子列在仙人掌格
    //   截断 + BFS 进入衰减 max(1,15)=15 → 仙人掌格天光恒 0；mesher 立方面光 = 面所朝邻格 skyLightAt
    //   → 沙顶面恰朝仙人掌格 → 整面采 0 压到 kVcMin 暗部地板 = 「整片阴影」。修法 = Cactus opacity
    //   15→0（MC 语义：不满格不遮天光；同轨/板/雪层默认全透口径）。reflood 兼容性：recomputeLightField /
    //   refloodBox / recomputeLightAround / t933 批量 reflood 全链只读 lightOpacity 单一权威，改值自动生效；
    //   仙人掌放/挖 opacity 0↔0 无翻转 → 光场不变无重算需要（光学上等同空气）。
    //   断言五段：
    //   (a) 沙顶 2 高仙人掌：两节仙人掌格 skyLightAt==15（修前 0——种子截断+满遮不渗；仙人掌底格即沙顶面
    //       所朝采样格）+ 柱顶空气格 15（种子列穿透整柱）；
    //   (b) AO 侧双查：仙人掌列 columnTopSurfaceY == 沙顶（t849 排除口径——细柱不入列顶，PCF 环隙无影）；
    //   (c) 对照腿·石头贴沙照样暗（遮光/AO 不回归）：石格 skyLightAt==0（满遮照旧）+ columnTopSurfaceY ==
    //       石顶（整立方入列顶）；
    //   (d) 其它不满格方块同口径：铁轨 / 火把 / 石压力板 / 雪层贴沙 → 所占格 skyLightAt==15（支撑面不暗，
    //       防漂移钉——本族本就全透，钉住不许跟回去）；
    //   (e) 源码钉：lightOpacity Cactus 全透行 + 光 BFS 只读 lightOpacity 单一权威行（任一消失即红）。
    {
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 4; xx + 2 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -1; dx <= 1 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -3; dy <= 4 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                // 开天自检：rig 格天光满格（列上方直通世界顶）才入选——防丛林冠层/遗留结构假红（t997 rig 教训）。
                if (clear && w.skyLightAt(xx, kRigY, zz) == 15) { x0 = xx; z0 = zz; }
            }
        if (x0 < 0) {
            ++totalFail;
            qInfo().noquote() << "FAIL | t985 cactus contact shadow: no clear open-sky rig area found";
        } else {
            // 石板垫层（kRigY-2，3×3）：沙是重力方块，下方悬空会被重力链收走（t984 rig 同款垫石教训）。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    w.setBlock(x0 + dx, kRigY - 2, z0 + dz, BR::Stone, 0);
            // (a)+(b) 沙顶 2 高仙人掌：光照穿透（天光种子列过仙人掌直落沙顶）+ 列顶落沙顶。
            w.setBlock(x0, kRigY - 1, z0, BR::Sand, 0);
            w.setBlock(x0, kRigY,     z0, BR::Cactus, 0);
            w.setBlock(x0, kRigY + 1, z0, BR::Cactus, 0);
            const bool okA = w.skyLightAt(x0, kRigY,     z0) == 15   // 沙顶面所朝采样格（修前 0 = 阴影病灶）
                && w.skyLightAt(x0, kRigY + 1, z0) == 15             // 上节仙人掌格同样透光
                && w.skyLightAt(x0, kRigY + 2, z0) == 15;            // 柱顶空气格（种子列穿透）
            const bool okB = std::fabs(w.columnTopSurfaceY(x0, z0) - float(kRigY)) < 0.01f;
            if (!okA || !okB)
                qInfo().noquote() << "  [t985 diag] ab sky@" << int(w.skyLightAt(x0, kRigY, z0))
                                  << int(w.skyLightAt(x0, kRigY + 1, z0))
                                  << int(w.skyLightAt(x0, kRigY + 2, z0))
                                  << "colTop" << w.columnTopSurfaceY(x0, z0);
            // (c) 对照腿·石头贴沙照样暗：完整实体满遮 + 入列顶（修法只豁免不满格，不放松实心遮光）。
            w.setBlock(x0 + 1, kRigY - 1, z0, BR::Sand, 0);
            w.setBlock(x0 + 1, kRigY,     z0, BR::Stone, 0);
            const bool okC = w.skyLightAt(x0 + 1, kRigY, z0) == 0
                && std::fabs(w.columnTopSurfaceY(x0 + 1, z0) - float(kRigY + 1)) < 0.01f;
            if (!okC)
                qInfo().noquote() << "  [t985 diag] c sky" << int(w.skyLightAt(x0 + 1, kRigY, z0))
                                  << "colTop" << w.columnTopSurfaceY(x0 + 1, z0);
            // (d) 其它不满格方块同口径：所占格天光满格（支撑面不暗）。
            w.setBlock(x0 - 1, kRigY - 1, z0, BR::Sand, 0);
            w.setBlock(x0 - 1, kRigY,     z0, BR::Rail, 0);
            w.setBlock(x0, kRigY - 1, z0 - 1, BR::Sand, 0);
            w.setBlock(x0, kRigY,     z0 - 1, BR::Torch, 0);
            w.setBlock(x0, kRigY - 1, z0 + 1, BR::Sand, 0);
            w.setBlock(x0, kRigY,     z0 + 1, BR::StonePressurePlate, 0);
            w.setBlock(x0 - 1, kRigY - 1, z0 + 1, BR::Sand, 0);
            w.setBlock(x0 - 1, kRigY,     z0 + 1, BR::SnowLayer, 0);
            const bool okD = w.skyLightAt(x0 - 1, kRigY, z0) == 15
                && w.skyLightAt(x0, kRigY, z0 - 1) == 15
                && w.skyLightAt(x0, kRigY, z0 + 1) == 15
                && w.skyLightAt(x0 - 1, kRigY, z0 + 1) == 15;
            if (!okD)
                qInfo().noquote() << "  [t985 diag] d rail" << int(w.skyLightAt(x0 - 1, kRigY, z0))
                                  << "torch" << int(w.skyLightAt(x0, kRigY, z0 - 1))
                                  << "plate" << int(w.skyLightAt(x0, kRigY, z0 + 1))
                                  << "snow" << int(w.skyLightAt(x0 - 1, kRigY, z0 + 1));
            // (e) 源码钉：opacity 全透行 + 光 BFS 单一权威行（t933 批量 reflood 同链同源）。
            const QString exeDirT985 = QCoreApplication::applicationDirPath();
            const QString rootT985 = QDir(exeDirT985 + QStringLiteral("/..")).absolutePath();
            auto readSrcT985 = [&rootT985](const QString &rel) -> QString {
                QFile f(rootT985 + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString brSrcT985 = readSrcT985(QStringLiteral("src/Core/blockregistry.cpp"));
            const QString wSrcT985 = readSrcT985(QStringLiteral("src/World/world.cpp"));
            const bool okE = brSrcT985.contains(QStringLiteral("case Cactus:       return 0;"))
                && wSrcT985.contains(QStringLiteral(
                    "const quint8 nbOp = BlockRegistry::lightOpacity(m_chunks.blockAt(nx, ny, nz), m_chunks.stateAt(nx, ny, nz));"));
            if (!okE)
                qInfo().noquote() << "  [t985 diag] e src pin miss" << brSrcT985.isEmpty()
                                  << wSrcT985.isEmpty();
            // 清场：自顶向下拆（仙人掌 oldId=Cactus 跳过 ② 失撑级联；沙在柱拆完后拆不触发落沙）
            //   + 3×3 台面带 + 石板垫层。
            for (int dy = 1; dy >= 0; --dy)
                w.setBlock(x0, kRigY + dy, z0, BR::Air, 0);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = 0; dy >= -2; --dy)
                        w.setBlock(x0 + dx, kRigY + dy, z0 + dz, BR::Air, 0);
            tickN(w, 2);
            const bool okT985 = okA && okB && okC && okD && okE;
            if (!okT985) ++totalFail;
            if (!okT985)
                qInfo().noquote() << "  [t985 diag] a" << okA << "b" << okB << "c" << okC
                                  << "d" << okD << "e" << okE;
            qInfo().noquote() << (okT985 ? "PASS" : "FAIL")
                              << "| t985 a cactus standing on sand casts no contact shadow on the"
                                 " support face (light-opacity semantics for non-full blocks,"
                                 " reversed t445 full-shadow caliber): (a) both cactus cells of a"
                                 " 2-high column on sand read skylight 15 - the cell the sand top"
                                 " face samples used to sit at 0 because the seed column broke at"
                                 " the cactus and the 15-opacity BFS refused to leak in, pressing"
                                 " the whole contact face into the darkness floor; (b) the cactus"
                                 " column stays out of the heightmap (columnTopSurfaceY == sand"
                                 " top) so the AO/PCF side of the double check is pinned too;"
                                 " control legs keep existing semantics: (c) a full stone cube on"
                                 " sand still reads skylight 0 and still tops the column (full"
                                 " cubes keep full shadow); (d) rail, torch, stone pressure plate"
                                 " and snow layer on sand all keep skylight 15 in their cells"
                                 " (non-full families stay transparent - drift guard); (e) source"
                                 " pins for the transparent cactus opacity row and the light BFS"
                                 " reading the single lightOpacity authority (t933 batch reflood"
                                 " chain included)";
        }
    }

    // ── P-t946 坐姿变换链连续域探针（狼/豹猫；t946 断链形态回归拦截，t987 前爪根锚重推后照绿）──
    //   用户实测（第五轮）：「身体翘太高、身体与躯体分离中间透明」。根因 = t878② 坐姿把躯干绕枢 +40°
    //   上仰而头/耳/腿保持各段独立绝对坐标——变换链断开：抬起后的躯干底与臀下折叠腿顶之间悬空
    //   ~0.10-0.23（「中间透明」），胸顶 0.48 反压头心 0.30（「翘太高」）。修复 = 单根锚派生
    //   （mobmodel.cpp sitRot 链 lambda；躯干随动段全走链；t987 起根锚 = 前爪着地点 kSitPivotY/Z，
    //   臀底角解析触地，链派生纪律不变）。
    //   本探针在**真几何顶点**上做连续域断言（MobModel 直编读 vertexData，同 t880 ItemShapeGeometry
    //   行为级先例；沿 +Y 射线三角奇偶内外判定，凸盒并集上与盒区间并集等价）：
    //   (a) 着地：坐姿整体 minY = 碰撞底面（狼 -0.42 / 豹猫 -0.40 ±0.05）——断链形态臀部悬空必红；
    //   (b) 不翘太高：maxY 上界（狼 0.62 / 豹猫 0.58；t987 修复态耳顶 0.342/0.295）——防未来再抬高的回归界；
    //   (c) 臀链连续：髋带 z∈[0.16,0.36]（豹猫 [0.10,0.30]）× x=±0.10 逐列采样——列内自着地至剪影顶
    //       的内部空隙 ≤ 0.075（断链形态实测 0.110-0.234 缝必红）+ 列底触地；
    //   (d) 胸链连续：前腿带 z∈[-0.30,-0.20]（豹猫 [-0.26,-0.18]）同判（断链形态前带离地必红）；
    //   (e) 站姿零回归：sitPose=false 站姿剪影界不变（狼 y[-0.42,0.37] / 豹猫 y[-0.40,0.32] ±0.05）；
    //   (f) 源码钉：单根锚派生形态（t987 kSitPivotY/kSitPivotZ 值 + 躯干 addBoxRot 枢轴引用 + 颈附
    //       sitRot 链 + 大腿块 z 绑根锚）+ Main.qml / ResourceBrowser.qml 眼/尾 overlay 成对契约新位
    //       （t880/t902/t931 源码钉先例——QML 侧无行为级断言面）。
    {
        bool ok = true;
        QString diag;
        // 三角汤（顶点 stride 5 float = pos3+uv2，MobVtx 契约；索引 U32）。
        struct SitTri { float ax, ay, az, bx, by, bz, cx, cy, cz; };
        auto buildSitTris = [](const MobModel &g, std::vector<SitTri> &tris) {
            tris.clear();
            const QByteArray vd = g.vertexData();
            const QByteArray id = g.indexData();
            const float *vp = reinterpret_cast<const float *>(vd.constData());
            const int vCount = int(vd.size()) / 20;
            const quint32 *ip = reinterpret_cast<const quint32 *>(id.constData());
            const int iCount = int(id.size()) / int(sizeof(quint32));
            for (int i = 0; i + 2 < iCount; i += 3) {
                const quint32 ia = ip[i], ib = ip[i + 1], ic = ip[i + 2];
                if (ia >= quint32(vCount) || ib >= quint32(vCount) || ic >= quint32(vCount)) continue;
                const float *a = vp + std::size_t(ia) * 5;
                const float *b = vp + std::size_t(ib) * 5;
                const float *c = vp + std::size_t(ic) * 5;
                tris.push_back({a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
            }
        };
        // 列覆盖区间并集（法线定向深度计数）：竖直射线在 (x0,z0) 列上的全部三角交点按 y 排序，
        //   依面法线 Y 符号累计实体深度（nY<0 = 面朝下 → 上行进入 +1；nY>0 → 穿出 −1），depth ≥ 1 的
        //   运行段 = 实体并集覆盖区间。**不能用奇偶法**——本几何大腿块/前爪/前腿/躯干互相嵌接（重叠实体），
        //   奇偶在重叠段误判为外部（两实体叠 = depth 2 ≡ 偶）。nY≈0 = 竖直面（x=const 平面含射线方向）不横穿。
        auto columnRuns = [](const std::vector<SitTri> &tris, float x0, float z0,
                             std::vector<std::pair<float, float>> &runs) {
            runs.clear();
            std::vector<std::pair<float, int>> cr;
            cr.reserve(tris.size());
            for (const SitTri &t : tris) {
                const float d = (t.bz - t.cz) * (t.ax - t.cx) + (t.cx - t.bx) * (t.az - t.cz);
                if (std::abs(d) < 1e-12f) continue;
                const float w1 = ((t.bz - t.cz) * (x0 - t.cx) + (t.cx - t.bx) * (z0 - t.cz)) / d;
                const float w2 = ((t.cz - t.az) * (x0 - t.cx) + (t.ax - t.cx) * (z0 - t.cz)) / d;
                const float w0 = 1.0f - w1 - w2;
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
                const float e1x = t.bx - t.ax, e1z = t.bz - t.az;
                const float e2x = t.cx - t.ax, e2z = t.cz - t.az;
                const float nY = e1z * e2x - e1x * e2z; // (e1×e2).y（kFace 绕序 = 外法线一致）
                if (std::abs(nY) < 1e-9f) continue;
                // 权重配对：m = P−C = w1·(A−C) + w2·(B−C) → w1↔A、w2↔B、w0=1−w1−w2↔C。
                cr.push_back({ w1 * t.ay + w2 * t.by + w0 * t.cy, nY < 0.0f ? +1 : -1 });
            }
            std::sort(cr.begin(), cr.end());
            int depth = 0;
            float runStart = 0.0f;
            for (const auto &c : cr) {
                if (depth == 0) runStart = c.first;
                depth += c.second;
                if (depth == 0) runs.push_back({ runStart, c.first });
            }
            // 网格病态兜底（闭合网格不应触达；钳在末交点防越界虚高）。
            if (depth > 0 && !cr.empty()) runs.push_back({ runStart, cr.back().first });
        };
        auto probeSitSilhouette = [&](const std::vector<SitTri> &tris, float ground, float maxYBound,
                                      float hipLo, float hipHi, float frLo, float frHi, const char *tag) {
            constexpr float kStep = 0.025f, kTol = 0.03f, kMaxGap = 0.075f;
            const float xs[2] = { 0.10f, -0.10f }; // 落在 腿[0.08,0.24]/躯干/头/耳 投影内的采样平面（避开盒面坐标）
            std::vector<std::pair<float, float>> runs;
            float allMin = 9e9f, allMax = -9e9f;
            // z 网格半步偏移：落在盒面坐标的列会让射线精确命中两三角共享对角边（交点双重计入 → 深度失衡）。
            for (int xi = 0; xi < 2; ++xi)
                for (float z = -0.60f + kStep / 2; z <= 0.70f; z += kStep) {
                    columnRuns(tris, xs[xi], z, runs);
                    for (const auto &r : runs) {
                        allMin = std::min(allMin, r.first);
                        allMax = std::max(allMax, r.second);
                    }
                }
            if (std::abs(allMin - ground) > kTol) {
                ok = false;
                diag += QStringLiteral(" %1 minY %2!=%3").arg(tag).arg(allMin).arg(ground);
            }
            if (allMax > maxYBound) {
                ok = false;
                diag += QStringLiteral(" %1 maxY %2>%3").arg(tag).arg(allMax).arg(maxYBound);
            }
            const float bands[2][2] = { { hipLo, hipHi }, { frLo, frHi } };
            for (int bi = 0; bi < 2; ++bi)
                for (float z = bands[bi][0] + kStep / 2; z <= bands[bi][1]; z += kStep)
                    for (int xi = 0; xi < 2; ++xi) {
                        columnRuns(tris, xs[xi], z, runs);
                        if (runs.empty()) {
                            ok = false;
                            diag += QStringLiteral(" %1 band%2 z=%3 x=%4 empty")
                                        .arg(tag).arg(bi).arg(z, 0, 'f', 3).arg(xs[xi]);
                            continue;
                        }
                        if (std::abs(runs.front().first - ground) > kTol) {
                            ok = false;
                            diag += QStringLiteral(" %1 band%2 z=%3 x=%4 off-ground %5")
                                        .arg(tag).arg(bi).arg(z, 0, 'f', 3)
                                        .arg(xs[xi]).arg(runs.front().first, 0, 'f', 3);
                        }
                        for (std::size_t ri = 1; ri < runs.size(); ++ri) {
                            const float gap = runs[ri].first - runs[ri - 1].second;
                            if (gap > kMaxGap) {
                                ok = false;
                                diag += QStringLiteral(" %1 band%2 z=%3 x=%4 gap %5")
                                            .arg(tag).arg(bi).arg(z, 0, 'f', 3)
                                            .arg(xs[xi]).arg(gap, 0, 'f', 3);
                            }
                        }
                    }
        };
        std::vector<SitTri> sitTris;
        {
            MobModel g;
            g.setMobType(10);
            g.setSitPose(true);
            buildSitTris(g, sitTris);
            probeSitSilhouette(sitTris, -0.42f, 0.62f, 0.16f, 0.36f, -0.30f, -0.20f, "wolfSit");
        }
        {
            MobModel g;
            g.setMobType(11);
            g.setSitPose(true);
            buildSitTris(g, sitTris);
            probeSitSilhouette(sitTris, -0.40f, 0.58f, 0.10f, 0.30f, -0.26f, -0.18f, "ocelotSit");
        }
        {   // (e) 站姿零回归（x=0.10 剪影列；站姿分支本任务未动，界钉死防漂移）。
            std::vector<std::pair<float, float>> runs;
            float mn = 9e9f, mx = -9e9f;
            {
                MobModel g;
                g.setMobType(10);
                buildSitTris(g, sitTris);
                for (float z = -0.70f + 0.0125f; z <= 0.50f; z += 0.025f) {
                    columnRuns(sitTris, 0.10f, z, runs);
                    for (const auto &r : runs) {
                        mn = std::min(mn, r.first);
                        mx = std::max(mx, r.second);
                    }
                }
                if (std::abs(mn - (-0.42f)) > 0.03f || std::abs(mx - 0.37f) > 0.03f) {
                    ok = false;
                    diag += QStringLiteral(" wolfStand [%1,%2]").arg(mn).arg(mx);
                }
            }
            {
                MobModel g;
                g.setMobType(11);
                buildSitTris(g, sitTris);
                mn = 9e9f; mx = -9e9f;
                for (float z = -0.60f + 0.0125f; z <= 0.60f; z += 0.025f) {
                    columnRuns(sitTris, 0.10f, z, runs);
                    for (const auto &r : runs) {
                        mn = std::min(mn, r.first);
                        mx = std::max(mx, r.second);
                    }
                }
                // 豹猫站姿耳 x∈[0.03,0.09] 不含采样列 x=0.10 → 列顶 = 头顶 0.24（狼耳 [0.045,0.115] 含 0.10）。
                if (std::abs(mn - (-0.40f)) > 0.03f || std::abs(mx - 0.24f) > 0.03f) {
                    ok = false;
                    diag += QStringLiteral(" ocelotStand [%1,%2]").arg(mn).arg(mx);
                }
            }
        }
        {   // (f) 源码钉：单根锚派生形态（t987 前爪根锚版：kSitPivotY/kSitPivotZ 值 + 躯干 addBoxRot 枢轴
            //   引用 + 颈附 sitRot 链 + 大腿块 z 绑根锚）+ Main.qml / ResourceBrowser.qml 眼/尾 overlay
            //   成对契约新位（眼/尾；项圈 overlay 已 t986 收编进 MobModel 几何 collarVisible，其成对位钉
            //   退役 → 移交 P-t986 环带钉）。
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            auto readSrc = [&root](const QString &rel) -> QString {
                QFile f(root + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            const QString mm = readSrc(QStringLiteral("src/Renderer/mobmodel.cpp"));
            const int wSit = mm.indexOf(QStringLiteral("t987 狼坐姿返修"));
            const int oSit = mm.indexOf(QStringLiteral("t987 豹猫/猫坐姿返修"));
            const int oEnd = mm.indexOf(QStringLiteral("} else if (m_mobType == 12)"));
            bool okPin = wSit >= 0 && oSit > wSit && oEnd > oSit;
            if (okPin) {
                const QString wolf = mm.mid(wSit, oSit - wSit);
                okPin = wolf.contains(QStringLiteral("constexpr float kSitPivotY   = -0.42f"))
                     && wolf.contains(QStringLiteral("kSitPivotY, kSitPivotZ, kSitPitch"))
                     && wolf.contains(QStringLiteral("sitRotY(0.12f, -0.24f)"))
                     && wolf.contains(QStringLiteral("kSitPivotZ + 0.48f"));
            }
            if (okPin) {
                const QString oce = mm.mid(oSit, oEnd - oSit);
                okPin = oce.contains(QStringLiteral("constexpr float kSitPivotY   = -0.40f"))
                     && oce.contains(QStringLiteral("kSitPivotY, kSitPivotZ, kSitPitch"))
                     && oce.contains(QStringLiteral("sitRotY(0.12f, -0.24f)"))
                     && oce.contains(QStringLiteral("sitRotZ(0.18f, 0.36f)"))
                     && oce.contains(QStringLiteral("kSitPivotZ + 0.41f"));
            }
            const QString mn = readSrc(QStringLiteral("src/ui/Main.qml"));
            const QString rb = readSrc(QStringLiteral("src/ui/ResourceBrowser.qml"));
            const bool okMn = mn.contains(QStringLiteral("wolfSit === 1 ? Qt.vector3d(0, -0.15, 0.56)"))
                && mn.contains(QStringLiteral("wolfSit === 1 ? Qt.vector3d(-0.08, 0.16, -0.39)"))
                && mn.contains(QStringLiteral("wolfSit === 1 ? Qt.vector3d(0.08, 0.16, -0.39)"))
                && mn.contains(QStringLiteral("ocatSit === 1 ? Qt.vector3d(-0.07, 0.15, -0.28)"))
                && mn.contains(QStringLiteral("ocatSit === 1 ? Qt.vector3d(0.07, 0.15, -0.28)"));
            const bool okRb = rb.contains(QStringLiteral("? Qt.vector3d(0, -0.15, 0.56) : Qt.vector3d(0, 0.16, 0.38)"))
                && rb.contains(QStringLiteral("? Qt.vector3d(-0.08, 0.16, -0.39)"))
                && rb.contains(QStringLiteral("? Qt.vector3d(0.08, 0.16, -0.39)"))
                && rb.contains(QStringLiteral("? Qt.vector3d(-0.07, 0.15, -0.28)"))
                && rb.contains(QStringLiteral("? Qt.vector3d(0.07, 0.15, -0.28)"));
            if (!okPin || !okMn || !okRb) {
                ok = false;
                diag += QStringLiteral(" pins mm=%1 mn=%2 rb=%3").arg(okPin).arg(okMn).arg(okRb);
            }
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t946 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t946 wolf/ocelot sit pose rebuilt on a single root-anchor transform chain: the"
                             " t878 pose rotated the torso +40 deg about a hip pivot while head/ears/legs"
                             " stayed at independent absolute coordinates (broken chain -> the user-visible"
                             " 'chest reared too high with a transparent gap between body halves'). The fix"
                             " derives every torso-following segment (head via the rotated neck attach, ears"
                             " via the head offset) from ONE root anchor + pitch (mobmodel.cpp sitRot"
                             " lambdas; t987 re-rooted the chain onto the front-paw ground point"
                             " kSitPivotY/kSitPivotZ with an analytic-landing pitch, discipline unchanged),"
                             " with hind legs folded flat under the rump and standing-identical front legs."
                             " Verified on REAL mesh vertices (MobModel direct"
                             " build; per-column coverage = normal-oriented crossing-depth union -- parity is"
                             " wrong here because the joints intentionally OVERLAP as separate closed boxes):"
                             " (a) sit minY == collision bottom (wolf -0.42 /"
                             " ocelot -0.40), (b) maxY <= 0.62/0.58 (no rearing high), (c) hip band"
                             " z[0.16,0.36]/[0.10,0.30] columns ground-connected with interior gaps <= 0.075"
                             " (the broken form gaps 0.11-0.23), (d) front-leg band columns continuous,"
                             " (e) standing-pose silhouette bounds unchanged (zero regression), (f) source"
                             " pins for the shared-root derivation form and the Main.qml/ResourceBrowser.qml"
                             " eye/collar/tail overlay pair-contract positions"
                             ;
    }

    // ── P-t987 四足坐姿肢体返修探针（狼/豹猫；用户口径「完全像兔子——后脚多出好长一节接触地面、
    //    腿凭空长高一节」）──
    //    t946 形态病：根锚在臀部自身 → 18° 后仰几乎不动臀（躯干底仅沉 0.013）却把胸顶抬到 0.39——
    //    坐高全靠前腿加长（0.34→0.54 立在抬高的胸下）+ 臀下 0.42 高「大腿柱」+ 贴地长爪板填空 =
    //    视觉「腿凭空长高一节 + 后脚多出长段贴地」，读作兔子蹲。t987 修：根锚 = 前爪着地点（前腿站姿
    //    占位不变量，站/坐切换前腿零变化），躯干绕它后仰（狼 24.4° / 豹猫 26.6°，tanθ 解析解使臀底角
    //    精确触地）；前腿与站姿同盒立撑、后腿折叠 = 臀下侧埋矮块 + 贴地细爪板。
    //    真几何顶点断言（MobModel 直编读 vertexData，P-t946/P-t968 先例）：
    //    (a) 臀部着地：后臀带 z∈[0.40,0.50]（狼）/ [0.36,0.46]（豹猫）存在 y∈[ground-0.033,ground+0.01]
    //        顶点（躯干臀底角触地 = 「臀部落地」本体；旧形态臀底 -0.143 悬空、该带有着地段全无必红）；
    //    (b) 无穿地：坐姿全体顶点 min y ≥ ground − 0.005（穿地伸出段零容忍）；
    //    (c) 前腿立撑形态钉：前腿柱**外半**（|x|∈[0.185,0.25] 狼 / [0.155,0.21] 豹猫——避开躯干最大
    //        半宽 0.18/0.15 防躯干底边误触发）× z∈[-0.33,-0.15] / [-0.27,-0.13] 存在腿顶环带顶点
    //        y∈[legTop−0.03,legTop+0.03]（狼 −0.05 / 豹猫 −0.02 = 站姿顶 −0.08/−0.06 埋胸 0.03/0.04；
    //        旧形态前腿顶 0.12/0.09 该带无顶点必红 = 「腿加长」形态拦截）；
    //    (d) 紧凑坐高钉：坐姿全顶点 maxY ≤ 站姿 maxY + 0.02（狼 0.37 / 豹猫 0.32；旧形态耳顶
    //        0.568/0.50 = 「兔子直立」必红）；
    //    (e) 站态对照零回归：sitPose=false 全顶点 AABB = 站姿界（狼 [−0.42,0.37] / 豹猫 [−0.40,0.32]）。
    {
        bool ok = true;
        QString diag;
        auto sitVertScan = [](int mobType, bool sit, float &mnY, float &mxY,
                              bool &hipGround, bool &frontLegTop) {
            MobModel g;
            g.setMobType(mobType);
            if (sit) g.setSitPose(true);
            const QByteArray vd = g.vertexData();
            const float *vp = reinterpret_cast<const float *>(vd.constData());
            const int n = int(vd.size()) / 20;
            mnY = 9e9f; mxY = -9e9f;
            hipGround = false;
            frontLegTop = false;
            // 物种参数表：ground / 臀带 z 界 / 前腿柱外半 |x| 带 + z 带 / 站姿腿顶。
            const float ground   = (mobType == 10) ? -0.42f : -0.40f;
            const float hipZMin  = (mobType == 10) ?  0.40f :  0.36f;
            const float axMin    = (mobType == 10) ?  0.185f :  0.155f;
            const float axMax    = (mobType == 10) ?  0.25f :  0.21f;
            const float legZMin  = (mobType == 10) ? -0.33f : -0.27f;
            const float legZMax  = (mobType == 10) ? -0.15f : -0.13f;
            const float legTop   = (mobType == 10) ? -0.05f : -0.02f; // 坐姿前腿顶（站姿顶 -0.08/-0.06 + 埋胸 0.03/0.04 补肩窝）
            for (int i = 0; i < n; ++i) {
                const float x = vp[i * 5], y = vp[i * 5 + 1], z = vp[i * 5 + 2];
                mnY = std::min(mnY, y);
                mxY = std::max(mxY, y);
                if (!sit) continue;
                if (z >= hipZMin && z <= hipZMin + 0.10f
                    && y >= ground - 0.033f && y <= ground + 0.01f)
                    hipGround = true;
                const float ax = std::abs(x);
                if (ax >= axMin && ax <= axMax && z >= legZMin && z <= legZMax
                    && y >= legTop - 0.03f && y <= legTop + 0.03f)
                    frontLegTop = true;
            }
        };
        float wMn = 0, wMx = 0, oMn = 0, oMx = 0, sMn = 0, sMx = 0;
        bool wHip = false, wLeg = false, oHip = false, oLeg = false;
        sitVertScan(10, true, wMn, wMx, wHip, wLeg);
        sitVertScan(11, true, oMn, oMx, oHip, oLeg);
        if (!wHip || !oHip) {   // (a) 臀部着地
            ok = false;
            diag += QStringLiteral(" hip w=%1 o=%2").arg(int(wHip)).arg(int(oHip));
        }
        if (wMn < -0.42f - 0.005f || oMn < -0.40f - 0.005f) {   // (b) 无穿地
            ok = false;
            diag += QStringLiteral(" pierce wMin=%1 oMin=%2").arg(wMn, 0, 'f', 4).arg(oMn, 0, 'f', 4);
        }
        if (!wLeg || !oLeg) {   // (c) 前腿立撑形态钉
            ok = false;
            diag += QStringLiteral(" legTop w=%1 o=%2").arg(int(wLeg)).arg(int(oLeg));
        }
        sitVertScan(10, false, sMn, sMx, wHip, wLeg);   // (d) 坐高 ≤ 站高（站姿剪影 maxY 对照）+ (e) 狼站姿界
        if (wMx > sMx + 0.02f) {
            ok = false;
            diag += QStringLiteral(" sitHi w=%1>stand%2").arg(wMx, 0, 'f', 3).arg(sMx, 0, 'f', 3);
        }
        if (std::abs(sMn - (-0.42f)) > 0.02f || std::abs(sMx - 0.37f) > 0.02f) {   // (e) 站态零回归（狼）
            ok = false;
            diag += QStringLiteral(" wolfStand [%1,%2]").arg(sMn, 0, 'f', 3).arg(sMx, 0, 'f', 3);
        }
        sitVertScan(11, false, sMn, sMx, oHip, oLeg);   // (d) 豹猫 + (e) 豹猫站姿界
        if (oMx > sMx + 0.02f) {
            ok = false;
            diag += QStringLiteral(" sitHi o=%1>stand%2").arg(oMx, 0, 'f', 3).arg(sMx, 0, 'f', 3);
        }
        if (std::abs(sMn - (-0.40f)) > 0.02f || std::abs(sMx - 0.32f) > 0.02f) {
            ok = false;
            diag += QStringLiteral(" ocelotStand [%1,%2]").arg(sMn, 0, 'f', 3).arg(sMx, 0, 'f', 3);
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t987 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t987 quadruped sit-pose limb rework (wolf/ocelot): the t946 pose kept the"
                             " root anchor on the rump itself, so the 18-deg rearing barely moved the hip"
                             " (-0.013) while lifting the chest to 0.39 -- the sit height came from"
                             " LENGTHENED front legs (0.34 -> 0.54) plus a 0.42-tall thigh pillar and a"
                             " long ground slab under the haunch (user: 'the back foot grows a long extra"
                             " section touching the ground, the legs grow a taller section out of nowhere"
                             " -- looks exactly like a rabbit'). The rework roots the chain on the"
                             " FRONT-PAW GROUND POINT (the standing front-leg footprint as the structural"
                             " invariant) and pitches the torso by the analytic angle that lands the"
                             " rump-bottom corner exactly on the ground (wolf 24.4 deg / ocelot 26.6"
                             " deg): hips truly grounded, front legs in the standing footprint (tops sunk"
                             " 0.03/0.04 into the chest to fill the shoulder notch, no 0.5 lengthening),"
                             " hind legs folded as low side-buried haunch blocks plus flat forward paw"
                             " slabs. Verified on REAL mesh vertices (MobModel direct build): (a) a"
                             " grounded vertex exists in the rear-rump band z[0.40,0.50]/[0.36,0.46] (the"
                             " old form's hip bottom hangs at -0.143 -> red), (b) zero sit vertices below"
                             " the support plane (no through-ground protruding section), (c) the front-leg"
                             " column's OUTER half holds a top-ring vertex within 0.03 of the sit leg top"
                             " -0.05/-0.02 (the lengthened 0.12/0.09 tops -> red), (d) sit maxY <= standing"
                             " maxY + 0.02 (compact crouch; the old upright 0.568/0.50 -> red), (e)"
                             " standing-pose full-vertex AABB unchanged"
                             ;
    }

    // ── P-t986 项圈一圈探针（驯服狼/豹猫「脖子完整一圈项链」；旧 t831/t963 overlay 退役）──
    //    用户第五轮口径「差不多看到两个红点的样子」根因：旧单横扁盒 overlay 心 z=-0.30 埋进头盒
    //    z∈[-0.60,-0.24] 范围内，只露 x ±0.03 两侧凸块（侧视两个红点）。修法 = 项圈收编 MobModel
    //    几何（collarVisible 属性 + 独立 subset 1）：四薄板围合**裸颈段**（body 前缘与头后缘之间
    //    暴露颈区，band z[-0.26,-0.20]）环绕颈部横截面 → 任意 yaw 可辨完整一圈；三消费端（实体
    //    delegate / 图鉴预览 / 刷怪笼迷你）同一几何，QML 不再复刻 overlay 盒（t782 同源）。
    //    探针（MobModel 直编读 vertexData，P-t946/P-t968 先例；发射序契约 = 身体盒在前、环带 4 盒
    //    追加尾部 → 环带顶点 = vCount(true) − vCount(false) 差集，恰 4 盒 × 24 = 96）：
    //    (a) 狼站态：+96 顶点且绕颈轴（环带平面 = XY）8×45° 扇区 ≥7 非空（矩形环四边+四角充满全
    //        扇区；「两红点」形态只占 2 扇区必红）+ 质心 = 站姿裸颈段心 (0.02,-0.23)；
    //    (b) 豹猫站态：同 (a)（颈围镜像数值系，质心同位）；
    //    (c) 坐态随移：狼/豹猫 sitPose=true 环带顶点质心 = 同一根锚链派生位（狼 sitRot(0.02,-0.23)
    //        绕 t987 前爪根锚 (-0.42,-0.24) 旋 24.4° = (-0.015,-0.067)；豹猫绕 (-0.40,-0.20) 旋 26.6°
    //        = (-0.013,-0.033)）且扇区 ≥7（坐态仍整圈——QML overlay 时代的「随移成对契约」由几何
    //        单源派生取代）；
    //    (d) 源码钉：mobmodel.cpp 环带发射标记「t986 项圈环带」恰 4 处（狼/豹猫 × 坐/站）；
    //        Main.qml collarVisible 绑定 == 2（狼+豹猫）、ResourceBrowser.qml ≥1；双 QML 文件零
    //        旧 overlay 残留（坐姿位 vector3d(0,0.35,-0.175)/(0,0.32,-0.19) + 横扁环带 scale
    //        (0.42,0.06,0.07)/(0.36,0.05,0.06) 四串全绝迹 = 「一处几何不复制」tripwire）。
    {
        bool ok = true;
        QString diag;
        struct RingV { float x, y, z; };
        // 环带顶点收集：vCount(true) − vCount(false) 必恰 96（发射序契约；≠96 = 盒数/顶点格式漂移）。
        //   MobModel 直编（顶点 stride 5 float = pos3+uv2；本探针只数顶点/读坐标，不读索引）。
        auto ringOf = [](int mobType, bool sit, bool collar, std::vector<RingV> &out) -> int {
            MobModel g;
            g.setMobType(mobType);
            if (sit) g.setSitPose(true);
            g.setCollarVisible(collar);
            const QByteArray vd = g.vertexData();
            const float *vp = reinterpret_cast<const float *>(vd.constData());
            const int total = int(vd.size()) / 20;
            out.clear();
            if (collar) {
                for (int i = total - 96; i < total; ++i)
                    out.push_back({vp[i * 5], vp[i * 5 + 1], vp[i * 5 + 2]});
            }
            return total;
        };
        // 环带平面 = XY（颈轴沿 Z）：绕 (0,cy) 8×45° 扇区占用数（矩形环解析值 8/8，「两红点」≤2）。
        auto ringBins = [](const std::vector<RingV> &ring, float cy) {
            int mask = 0;
            for (const RingV &v : ring) {
                float deg = std::atan2(v.y - cy, v.x) * 57.2957795f + 90.0f;
                if (deg < 0.0f) deg += 360.0f;
                int b = int(deg / 45.0f);
                if (b > 7) b = 7;
                mask |= 1 << b;
            }
            int n = 0;
            for (int b = 0; b < 8; ++b) n += (mask >> b) & 1;
            return n;
        };
        auto checkRing = [&](const char *tag, int mobType, bool sit, float cyExp, float czExp) {
            std::vector<RingV> ring, none;
            const int base = ringOf(mobType, sit, false, none);
            const int with = ringOf(mobType, sit, true, ring);
            const int extra = with - base;
            if (extra != 96 || !none.empty()) {
                ok = false;
                diag += QStringLiteral(" %1 extra=%2 base0=%3").arg(tag).arg(extra).arg(int(none.empty()));
            } else {
                const int bins = ringBins(ring, cyExp);
                if (bins < 7) {
                    ok = false;
                    diag += QStringLiteral(" %1 bins=%2").arg(tag).arg(bins);
                }
                float sx = 0, sy = 0, sz = 0;
                for (const RingV &v : ring) { sx += v.x; sy += v.y; sz += v.z; }
                sx /= float(ring.size()); sy /= float(ring.size()); sz /= float(ring.size());
                if (std::abs(sx) > 0.01f || std::abs(sy - cyExp) > 0.04f || std::abs(sz - czExp) > 0.04f) {
                    ok = false;
                    diag += QStringLiteral(" %1 c=(%2,%3,%4)").arg(tag).arg(sx, 0, 'f', 3)
                                .arg(sy, 0, 'f', 3).arg(sz, 0, 'f', 3);
                }
            }
        };
        checkRing("wolf", 10, false, 0.02f, -0.23f);   // (a) 站态整圈 + 裸颈段心
        checkRing("ocelot", 11, false, 0.02f, -0.23f); // (b) 豹猫镜像
        checkRing("wolfSit", 10, true, -0.015f, -0.067f);  // (c) 坐态链派生随移（t987 前爪根锚链解析位）
        checkRing("ocelotSit", 11, true, -0.013f, -0.033f);
        {   // (d) 源码钉（t880/t902/t931 先例——QML 侧无行为级断言面）。
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            auto readSrc = [&root](const QString &rel) -> QString {
                QFile f(root + QStringLiteral("/") + rel);
                return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            };
            auto countSub = [](const QString &hay, const QString &needle) {
                int n = 0;
                for (int p = hay.indexOf(needle); p >= 0; p = hay.indexOf(needle, p + needle.size()))
                    ++n;
                return n;
            };
            const QString mm = readSrc(QStringLiteral("src/Renderer/mobmodel.cpp"));
            const QString mn = readSrc(QStringLiteral("src/ui/Main.qml"));
            const QString rb = readSrc(QStringLiteral("src/ui/ResourceBrowser.qml"));
            // 4 处发射点（狼/豹猫 × 坐/站）以环带边界赋值行为准（注释词会复用于说明段）。
            const int emit4 = countSub(mm, QStringLiteral("collarIdxStart = int(idx.size());"));
            const int mnBind = countSub(mn, QStringLiteral("collarVisible:"));
            const int rbBind = countSub(rb, QStringLiteral("collarVisible:"));
            const bool gone = !mn.contains(QStringLiteral("Qt.vector3d(0, 0.35, -0.175)"))
                && !rb.contains(QStringLiteral("Qt.vector3d(0, 0.35, -0.175)"))
                && !mn.contains(QStringLiteral("Qt.vector3d(0, 0.32, -0.19)"))
                && !rb.contains(QStringLiteral("Qt.vector3d(0, 0.32, -0.19)"))
                && !mn.contains(QStringLiteral("Qt.vector3d(0.42, 0.06, 0.07)"))
                && !rb.contains(QStringLiteral("Qt.vector3d(0.42, 0.06, 0.07)"))
                && !mn.contains(QStringLiteral("Qt.vector3d(0.36, 0.05, 0.06)"))
                && !rb.contains(QStringLiteral("Qt.vector3d(0.36, 0.05, 0.06)"));
            // (e) IP 门自证：项圈 = 程序自绘几何 + 纯色材质，零 MC 资产接线——三个实现文件对
            //     MC 原版项圈贴图名（wolf_collar/cat_collar，原版 textures/entity 下文件名）零引用；
            //     materials[1]/mobCollarMat 均无 baseColorMap → pack 命中与否项圈恒红，不消费任何包贴图
            //     （dev 包 docs/Default HD 附带 wolf_collar.png 也不读——仓库不新增任何资产文件）。
            const bool ipFree = !mm.contains(QStringLiteral("wolf_collar"))
                && !mm.contains(QStringLiteral("cat_collar"))
                && !mn.contains(QStringLiteral("wolf_collar"))
                && !mn.contains(QStringLiteral("cat_collar"))
                && !rb.contains(QStringLiteral("wolf_collar"))
                && !rb.contains(QStringLiteral("cat_collar"));
            if (emit4 != 4 || mnBind != 2 || rbBind < 1 || !gone || !ipFree) {
                ok = false;
                diag += QStringLiteral(" pins mm=%1 mn=%2 rb=%3 gone=%4 ip=%5")
                            .arg(emit4).arg(mnBind).arg(rbBind).arg(int(gone)).arg(int(ipFree));
            }
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t986 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t986 full collar ring: the old single flat-box overlay sat at z=-0.30,"
                             " INSIDE the head box z-range, so only the two x-side tabs poked out"
                             " (user: 'you can basically just see two red dots'). The collar now"
                             " lives in MobModel geometry (collarVisible + dedicated subset 1): four"
                             " thin slabs wrap the BARE neck segment (band z[-0.26,-0.20] between"
                             " body front and head rear) around the neck cross-section, visible as a"
                             " complete ring from any yaw. Verified on REAL mesh vertices (MobModel"
                             " direct build): collar adds exactly 96 verts in all four pose/species"
                             " combos (wolf/ocelot x stand/sit) spread over >=7 of 8 45-deg sectors"
                             " around the neck axis (two-dot form spans <=2), centroid at the bare-neck"
                             " anchor (0.02,-0.23) or the sit-chain-derived spot (-0.015,-0.067 /"
                             " -0.013,-0.033; t987 front-paw root chain); source pins: single-geometry emission x4 in mobmodel.cpp,"
                             " collarVisible bindings in Main.qml x2 + ResourceBrowser (t782"
                             " three-consumer sharing), zero old overlay leftovers (no duplicate box"
                             " lists), and zero MC-asset wiring: the collar is procedural geometry"
                             " with a mapless solid-color material - wolf_collar/cat_collar texture"
                             " names are referenced by no implementation file (IP gate: nothing"
                             " shipped or consumed from the vanilla entity texture set)"
                             ;
    }

    // ── P-t947 狼三修（R19.17 ①观察者不跟随 / ②咬击 4HP 口径 / ③chase 越障跳）──
    //    通用 rig：44×44×96 局部世界（seed 26）整面凿平 —— y[85,95] 清 Air + y84 全铺 Stone（lessons
    //    「地形高度是种子实测值非生成器不变量」：显式凿空不依赖任何「某高度以上必空」叙事；自带独立
    //    小世界不碰共享 nextSlot 分配器，t923 同款）。三修各用独立 World/EntityManager 免态串扰。
    //    (a1) 观察者不跟随：驯服站狼距主人 6.0 → tick(…, targetable=true, spectator=true) 1.5s → 主狼距
    //         ≥4.0（跟随会在 ~1.0s 收进 kFollowMinDist=2.5 停步带；观察者回退 aiWander 漂移上限
    //         1.5s×kWalkSpeed 1.0 = 1.5 → 距离下界 4.5，两行为带 [4.0,∞) vs ≤3.0 不相交，游向 RNG 无关判定）。
    //    (a2) 观察者瞬移停：主人距 15（> kWolfTeleportDist=12）→ spectator 2s → 距离仍 ≥10（跟随段的
    //         瞬移补位会把狼落进主人 2-5 格环 + 游荡 ≤2 → ≤7；不瞬移 ≥13 —— 阈值 10 两态硬分界）。
    //    (a3) 跟随对照（创造/生存 = spectator=false）：同 (a1) 几何 → 1.5s 内收进 ≤3.0（2.5 停步带）。
    //    (b) 咬击口径：满血 20HP Shambler setMobArmorSet(4) 穿甲 + 驯服狼 wolfRetaliateAgainst → 事件驱动
    //        逐帧 tick 至恰两口（hp 每新落一档记一口，记满即停 —— 咬击节律时序无关判定）：逐口 ==4、
    //        两口后血量 ==12（总伤 8 < 20 存活 = 「两口打死穿甲僵尸」不再成立）。==4 双向钉：kWolfAttackDamage
    //        常量（下方 static_assert 源级同钉）+ 「护甲不减伤」（t377 mob 护甲仅视觉，damageEntity 原值
    //        扣血 —— 穿甲与无甲同伤是既有口径非 bug，核对结论随本腿落档）。
    //    (c) 越障跳：x∈{22,23} 两列厚 1 格高**全深**石墙隔开狼（x17）/ 穿甲僵尸（x27）—— 全深堵死绕行，
    //        2 格厚 > kAttackRange 1.6 → 隔墙咬几何不可能（咬到必已越墙）；狼防御追击 → 越墙（中心 x >
    //        24.2 = 远侧墙沿 24.0 + 落位余量）+ 越墙后咬击掉血（「起跳越过后继续接近」）。t923 版 chase 把
    //        跳门在 `!moved`（斜向滑墙单轴恒可动 → 永不等到撞停）—— 阴性轮回退该形态本腿恒红（贴墙溜到超时）。
    {
        bool ok = true;
        QString diag;
        // ② 常量源级钉（kWolfAttackDamage 是类私有 constexpr，测试 TU 不可直读 → t923 (d) 源码钉先例）：
        //   钉「= 4」用户口径（调参越界即红）；行为级逐口 ==4 断言在 (b) 双向兜底。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile hf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            const QString h = hf.open(QIODevice::ReadOnly) ? QString::fromUtf8(hf.readAll()) : QString();
            // 钉声明形态（含名→值原始间距）：文档注释里同名词不见「= 4」——首现即声明的匹配才算数。
            const bool pin4 = h.contains(QStringLiteral("kWolfAttackDamage   = 4"));
            ok = ok && pin4;
            if (!pin4) diag += QStringLiteral("const-pin miss ");
        }
        auto flatRig = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        // 驯服站狼（~33%/骨 → 循环掷到成功；spawnMobTyped 默认站态非坐）。
        auto tamedWolfAt = [](EntityManager &em, int x, int z) -> int {
            const int wolf = em.spawnMobTyped(x, 85, z, EntityManager::MobWolf,
                                              QStringLiteral("#c8ccd4"), 10);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && wolf >= 0 && !tamed; ++attempt)
                tamed = em.tameWolf(wolf);
            return tamed ? wolf : -1;
        };
        auto distXZTo = [](const QVector3D &p, const QVector3D &q) {
            return QVector3D(p.x() - q.x(), 0.0f, p.z() - q.z()).length();
        };
        // (a1) 观察者不跟随。
        {
            World w1; flatRig(w1);
            EntityManager em1;
            const int wolf = tamedWolfAt(em1, 16, 22);
            const QVector3D owner(22.5f, 85.0f, 22.5f); // 距狼落点 (16.5,22.5) 恰 6.0
            ok = ok && wolf >= 0;
            if (wolf >= 0) {
                for (int t = 0; t < 94; ++t) // 1.504s
                    em1.tick(0.016f, &w1, owner, 0.3f, 1.8f, true, true); // targetable=true, spectator=true
                const float d = distXZTo(em1.posAt(wolf), owner);
                ok = ok && d >= 4.0f;
                if (d < 4.0f) diag += QStringLiteral("a1 spectator followed: dist=%1 ").arg(d);
            } else diag += QStringLiteral("a1 tame failed ");
        }
        // (a2) 观察者瞬移停。
        {
            World w2; flatRig(w2);
            EntityManager em2;
            const int wolf = tamedWolfAt(em2, 16, 22);
            const QVector3D owner(31.5f, 85.0f, 22.5f); // 距 15.0 > kWolfTeleportDist 12
            ok = ok && wolf >= 0;
            if (wolf >= 0) {
                for (int t = 0; t < 125; ++t) // 2.0s
                    em2.tick(0.016f, &w2, owner, 0.3f, 1.8f, true, true);
                const float d = distXZTo(em2.posAt(wolf), owner);
                ok = ok && d >= 10.0f;
                if (d < 10.0f) diag += QStringLiteral("a2 spectator teleported: dist=%1 ").arg(d);
            } else diag += QStringLiteral("a2 tame failed ");
        }
        // (a3) 跟随对照（创造/生存）。
        {
            World w3; flatRig(w3);
            EntityManager em3;
            const int wolf = tamedWolfAt(em3, 16, 22);
            const QVector3D owner(22.5f, 85.0f, 22.5f);
            ok = ok && wolf >= 0;
            if (wolf >= 0) {
                for (int t = 0; t < 94; ++t) // 1.504s
                    em3.tick(0.016f, &w3, owner, 0.3f, 1.8f, true, false); // spectator=false → 照常跟随
                const float d = distXZTo(em3.posAt(wolf), owner);
                ok = ok && d <= 3.0f;
                if (d > 3.0f) diag += QStringLiteral("a3 follow missing: dist=%1 ").arg(d);
            } else diag += QStringLiteral("a3 tame failed ");
        }
        // (b) 咬击口径（穿甲僵尸两口存活 + 逐口 ==4）。
        int bite1 = -1, bite2 = -1, bHp = 20;
        {
            World wb; flatRig(wb);
            EntityManager emb;
            const int wolf = tamedWolfAt(emb, 16, 22);
            const int zombie = emb.spawnMobTyped(28, 85, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20); // 满血 20HP
            bool dressed = zombie >= 0 && emb.setMobArmorSet(zombie, 4);         // 穿甲（tier4 全套）
            for (int pc = 0; pc < 4 && zombie >= 0; ++pc)
                dressed = dressed && emb.mobArmorAt(zombie, pc) != 0;            // 四部位皆着装（前置钉）
            ok = ok && wolf >= 0 && dressed;
            if (wolf >= 0 && dressed) {
                emb.wolfRetaliateAgainst(wolf, zombie);
                const QVector3D farOwner(-1000.0f, 90.0f, -1000.0f); // 玩家远 → 跟随/瞬移不抢戏（防御分支优先）
                int bites = 0;
                for (int t = 0; t < 2500 && bites < 2; ++t) { // 40s 事件驱动帽
                    emb.tick(0.016f, &wb, farOwner, 0.3f, 1.8f, false, false);
                    const int now = emb.healthAt(zombie);
                    if (now < bHp) { // 新一口落地（冷却 1s → 单帧至多一口）
                        if (bites == 0) bite1 = bHp - now; else bite2 = bHp - now;
                        ++bites;
                        bHp = now;
                    }
                }
                ok = ok && bites == 2 && bite1 == 4 && bite2 == 4 && bHp == 12; // 两口总伤 8 < 20 → 存活
                if (!(bites == 2 && bite1 == 4 && bite2 == 4 && bHp == 12))
                    diag += QStringLiteral("b bites=%1 d1=%2 d2=%3 hp=%4 ")
                                .arg(bites).arg(bite1).arg(bite2).arg(bHp);
            } else diag += QStringLiteral("b spawn/tame/dress failed ");
        }
        // (c) 越障跳（追击穿墙不可能的两格厚全深墙 → 越墙 + 续咬）。
        {
            World wc;
            wc.setWidth(44); wc.setDepth(44); wc.setHeight(96); wc.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) wc.setBlock(x, y, z, BR::Air, 0);
                    wc.setBlock(x, 84, z, BR::Stone, 0);
                }
            for (int z = 0; z < 44; ++z)
                for (int x = 22; x <= 23; ++x)
                    wc.setBlock(x, 85, z, BR::Stone, 0);
            EntityManager emc;
            const int wolf = tamedWolfAt(emc, 17, 22);
            const int zombie = emc.spawnMobTyped(27, 85, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20);
            const bool dressed = zombie >= 0 && emc.setMobArmorSet(zombie, 4);
            ok = ok && wolf >= 0 && dressed;
            if (wolf >= 0 && dressed) {
                emc.wolfRetaliateAgainst(wolf, zombie);
                const QVector3D farOwner(-1000.0f, 90.0f, -1000.0f);
                bool crossed = false;
                for (int t = 0; t < 3000 && !crossed; ++t) { // 48s 帽：越墙即停
                    emc.tick(0.016f, &wc, farOwner, 0.3f, 1.8f, false, false);
                    if (emc.posAt(wolf).x() > 24.2f) crossed = true; // 越过远侧墙沿 24.0 + 落位余量
                }
                int bites = 0;
                int cHp = 20;
                for (int t = 0; t < 1250 && bites < 1; ++t) { // 越墙后 20s 内咬到（继续接近的证据）
                    emc.tick(0.016f, &wc, farOwner, 0.3f, 1.8f, false, false);
                    const int now = emc.healthAt(zombie);
                    if (now < cHp) { ++bites; cHp = now; }
                }
                ok = ok && crossed && bites == 1;
                if (!(crossed && bites == 1))
                    diag += QStringLiteral("c crossed=%1 bites=%2 wx=%3 hp=%4 ")
                                .arg(int(crossed)).arg(bites)
                                .arg(emc.posAt(wolf).x()).arg(cHp);
            } else diag += QStringLiteral("c spawn/tame/dress failed ");
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t947 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t947 wolf triple-fix: (1) spectator-owner follow gate -- tamed standing"
                             " wolf holds off (>=4.0) and skips the far-teleport (>=10) while the owner"
                             " spectates, creative/survival control still closes to the 2.5 stop band"
                             " (<=3.0); (2) bite caliber pinned at 4 HP/bite (static_assert + per-bite"
                             "==4): an armored 20HP shambler survives two bites at exactly 12 HP -- total"
                             " 8 < 20, no two-bite kill (t377 mob armor is visual-only by spec, so the"
                             " constant is the real per-bite damage); (3) chase obstacle jump now probes"
                             " proactively every AI tick (aiHostile precedent) instead of only after a"
                             " full stop behind a >0.6 gate -- the wolf crosses a 2-thick full-depth"
                             " 1-high wall it cannot bite through and lands a bite beyond it"
                             ;
    }

    // ── P-t988 驯服狼战斗 AI 探针（用户口径「狼帮我打僵尸 AI 还是不会走路 + 遇到要跳跃才能上的
    //    格子不会跳、卡在那里」）──
    //    通用 rig：44×44×96 局部世界（seed 26）y[85,95] 清 Air + y84 铺 Stone（t947 同款；独立小世界）。
    //    注册链走**真实游戏路径**：僵尸侦测圈内咬中主人 → aiHostile melee 命中玩家块 m_wolfTarget=idx
    //    （entitymanager.cpp t480 注册点），不直调 setWolfTarget（与 P-t947(c)/P-t948 直调面互补——用户
    //    「指狼打僵尸」的实战序 = 打/被打任一先发生，注册面同一槽）。chase lambda（aiWolf 唯一移动路径）
    //    驱动接近 + t947③ 主动越障跳（#14 压跳门 t988 收窄为「同层可咬」：(a)(b) 目标远/异层照跳，
    //    同层贴脸压跳由 P-r0830C(d) rig 继续把守）。
    //    (a) 平地对照（奔跑接近 + 抵达撕咬）：驯服站狼 (16,22)、僵尸 (24,22)、主人 (20.5,22.5)（狼距
    //        4.5 < kWolfTeleportDist 12 防瞬移抢戏；僵尸侦测圈内）→ 断言狼 closing（min distXZ(狼,僵) ≤
    //        kAttackRange 1.6）+ 僵尸掉血（首口落地），20s 事件驱动帽；
    //    (b) 1 格台阶腿（跳上台阶 + 越台撕咬）：x∈[24,43] 全深 y85 石台（顶 86 = 1 格台阶，全深堵绕行）、
    //        主人台上 (30.5,22.5)、僵尸台上 (28,22)、狼台下面 (20,22)（距主人 10.5 < 12 防瞬移；僵尸咬主人
    //        注册目标 → 狼防御追击撞台 → 越障跳探（distXZ > 1.6 全程成立）→ 跳上台）→ 断言存在 tick
    //        「狼 x ≥ 24.5 且 y ≥ 86.3」（上台 = 平地跳跃峰值不可达域：地面点 y 85.45，墙前原地跳峰值虽
    //        过 86.3 但 x 恒 < 24.5 被台面碰撞拦回，两条件合取 = 真上台）+ 僵尸掉血（越台后续咬），
    //        30s 帽。「不会走路 / 不会跳」两症状在此腿分别为「x 恒 <20 / y 恒 <86.3」。
    //    (c) 台阶贴脸腿（**能力腿**——review0903 #2 如实化：本腿证「异层可爬可咬」的能力在位，**不是**
    //        门形判别腿——自家插桩（diag 逐位）实证本腿对 #14 门形不敏感（击退放行距离子句使门形差异
    //        不改变本腿终态）；门形回归（同层贴脸压跳）由 P-r0830C(d) 钉承担）：僵尸钉在台沿格（24,86,22，
    //        主人贴邻 1.0 咬距内 → 僵尸追主不动），狼台下面压台面后距僵尸 0.8 ≤ kAttackRange → 异层目标
    //        （|tdy|=1.0 > 0.5）带内照探跳 → 狼跳上台。判据同 (b) 合取，25s 帽。
    {
        bool ok = true;
        QString diag;
        auto flatRig988 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        auto tamedWolfAt988 = [](EntityManager &em, int x, int z) -> int {
            const int wolf = em.spawnMobTyped(x, 85, z, EntityManager::MobWolf,
                                              QStringLiteral("#c8ccd4"), 10);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && wolf >= 0 && !tamed; ++attempt)
                tamed = em.tameWolf(wolf);
            return tamed ? wolf : -1;
        };
        auto distXZ988 = [](const QVector3D &p, const QVector3D &q) {
            return QVector3D(p.x() - q.x(), 0.0f, p.z() - q.z()).length();
        };
        // (a) 平地对照：真注册链 + 奔跑接近 + 撕咬。
        {
            World wa; flatRig988(wa);
            EntityManager ema;
            const int wolf = tamedWolfAt988(ema, 16, 22);
            const int zombie = ema.spawnMobTyped(24, 85, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20);
            const QVector3D owner(20.5f, 85.0f, 22.5f);
            ok = ok && wolf >= 0 && zombie >= 0;
            if (wolf >= 0 && zombie >= 0) {
                float minD = 1e9f;
                bool bitten = false;
                for (int t = 0; t < 1250 && !bitten; ++t) { // 20s 事件驱动帽
                    ema.tick(0.016f, &wa, owner, 0.3f, 1.8f, true, false);
                    minD = std::min(minD, distXZ988(ema.posAt(wolf), ema.posAt(zombie)));
                    if (ema.healthAt(zombie) < 20) bitten = true; // 狼首口落地
                }
                ok = ok && bitten && minD <= 1.6f;
                if (!(bitten && minD <= 1.6f))
                    diag += QStringLiteral("a bitten=%1 minD=%2 ").arg(int(bitten)).arg(minD, 0, 'f', 2);
            } else diag += QStringLiteral("a spawn/tame failed ");
        }
        // (b) 1 格台阶：跳上台（x≥24.5 且 y≥86.3 合取）+ 越台撕咬。
        {
            World wb; flatRig988(wb);
            for (int z = 0; z < 44; ++z)
                for (int x = 24; x < 44; ++x)
                    wb.setBlock(x, 85, z, BR::Stone, 0); // 全深石台（顶 86 = 1 格台阶；堵死绕行）
            EntityManager emb;
            const int wolf = tamedWolfAt988(emb, 20, 22);   // 台下面 (feet 85)
            const int zombie = emb.spawnMobTyped(28, 86, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20); // 台上 (feet 86)
            const QVector3D owner(30.5f, 86.0f, 22.5f);     // 主人台上（僵尸咬主人 → 注册狼防御目标）
            ok = ok && wolf >= 0 && zombie >= 0;
            if (wolf >= 0 && zombie >= 0) {
                bool onTop = false, bitten = false;
                float maxX = 0.0f, maxY = 0.0f;
                for (int t = 0; t < 1875 && !(onTop && bitten); ++t) { // 30s 事件驱动帽
                    emb.tick(0.016f, &wb, owner, 0.3f, 1.8f, true, false);
                    const QVector3D wp = emb.posAt(wolf);
                    maxX = std::max(maxX, wp.x());
                    maxY = std::max(maxY, wp.y());
                    if (wp.x() >= 24.5f && wp.y() >= 86.3f) onTop = true; // 真上台合取判据
                    if (emb.healthAt(zombie) < 20) bitten = true;          // 越台后首口
                }
                ok = ok && onTop && bitten;
                if (!(onTop && bitten))
                    diag += QStringLiteral("b onTop=%1 bitten=%2 maxX=%3 maxY=%4 zhp=%5 ")
                                .arg(int(onTop)).arg(int(bitten))
                                .arg(maxX, 0, 'f', 2).arg(maxY, 0, 'f', 2)
                                .arg(emb.healthAt(zombie));
            } else diag += QStringLiteral("b spawn/tame failed ");
        }
        // (c) 台阶贴脸腿（能力腿：异层目标可爬可咬；review0903 #2 如实化——本腿非门形判别腿，
        //     门形回归由 P-r0830C(d) 钉；历史修面 = #14 压跳收窄为「同层可咬」，异层目标带内照探跳）。
        {
            World wc; flatRig988(wc);
            for (int z = 0; z < 44; ++z)
                for (int x = 24; x < 44; ++x)
                    wc.setBlock(x, 85, z, BR::Stone, 0); // 全深石台（顶 86 = 1 格台阶）
            EntityManager emc;
            const int wolf = tamedWolfAt988(emc, 22, 22);   // 台下面，贴台位（压台面后中心 x≈23.7）
            const int zombie = emc.spawnMobTyped(24, 86, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20); // 台上沿边格（中心 x=24.5，距狼压位点 0.8 ≤ 咬距带）
            const QVector3D owner(25.0f, 86.0f, 22.5f);     // 台上距僵尸 1.0 ≤ 咬距带 → 僵尸钉在沿边不动（追主不移动）
            ok = ok && wolf >= 0 && zombie >= 0;
            if (wolf >= 0 && zombie >= 0) {
                bool onTop = false, bitten = false;
                float maxX = 0.0f, maxY = 0.0f;
                for (int t = 0; t < 1560 && !(onTop && bitten); ++t) { // 25s 事件驱动帽
                    emc.tick(0.016f, &wc, owner, 0.3f, 1.8f, true, false);
                    const QVector3D wp = emc.posAt(wolf);
                    maxX = std::max(maxX, wp.x());
                    maxY = std::max(maxY, wp.y());
                    if (t % 45 == 0)
                        diag += QStringLiteral("[t=%1 w=(%2,%3,%4) z=(%5,%6,%7) zhp=%8 whp=%9] ")
                                    .arg(t).arg(wp.x(), 0, 'f', 2).arg(wp.y(), 0, 'f', 2).arg(wp.z(), 0, 'f', 2)
                                    .arg(emc.posAt(zombie).x(), 0, 'f', 2).arg(emc.posAt(zombie).y(), 0, 'f', 2)
                                    .arg(emc.posAt(zombie).z(), 0, 'f', 2)
                                    .arg(emc.healthAt(zombie)).arg(emc.healthAt(wolf));
                    if (wp.x() >= 24.5f && wp.y() >= 86.3f) onTop = true; // 真上台合取判据（同 (b)）
                    if (emc.healthAt(zombie) < 20) bitten = true;
                }
                ok = ok && onTop && bitten;
                diag += QStringLiteral("c onTop=%1 bitten=%2 maxX=%3 maxY=%4 zhp=%5 ")
                            .arg(int(onTop)).arg(int(bitten))
                            .arg(maxX, 0, 'f', 2).arg(maxY, 0, 'f', 2)
                            .arg(emc.healthAt(zombie));
            } else diag += QStringLiteral("c spawn/tame failed ");
        }
        if (!ok) ++totalFail;
        if (!ok) // review0903 #9：diag 门控（原每跑必打印）+ 删注释残行
            qInfo().noquote() << "  [t988 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t988 tamed-wolf combat AI: command the wolf on a zombie (the real"
                             " registration chain -- the shambler melee-hits the owner on its detect band"
                             " and the hit site registers the shared m_wolfTarget) and the wolf RUNS to"
                             " close (flat control: min wolf-zombie distance reaches the 1.6 bite band and"
                             " the first bite lands within 20s) and hops the 1-block step on the way (step"
                             " leg: a full-depth y85 stone platform tops out 1 above the floor, the wolf"
                             " below reaches x>=24.5 WITH y>=86.3 -- the conjunction only holds once it"
                             " stands on top; a ground jump peaks past 86.3 but the platform face keeps x"
                             " pinned < 24.5, a blocked wolf stays at x<20) and lands its bite beyond the"
                             " step within 30s; the #14 bite-band jump suppression stays narrowed to"
                             " SAME-FLOOR targets (|target dy| <= 0.5): leg c is a CAPABILITY leg -- the"
                             " wolf can climb to and bite a cross-level target pinned at the ledge 0.8"
                             " XZ away -- not a gate-shape discriminator (the probe's own diag showed"
                             " the leg outcome is insensitive to the gate form via the knockback"
                             " release-distance clause, review0903 #2); gate-shape regression"
                             " (no-hop-while-biting on a same-floor target) is pinned by the"
                             " review0830 C(d) rig (source pin evolved to the narrowed gate);"
                             " the chase lambda stays the single movement drive and the follow/stand"
                             " states keep their t947 bands (zero regression)"
                             ;
    }

    // ── P-t948 狼攻击仇恨转移（R19.17 🅲：狼主动咬敌对 → 被咬者转火攻击狼；t923 反击注册面核）──
    //    通用 rig：44×44×96 局部世界（seed 26）整面凿平 —— y[85,95] 清 Air + y84 全铺 Stone（t947
    //    同款；自带独立小世界不碰共享 nextSlot 分配器）。三腿各用独立 World/EntityManager 免态串扰。
    //    (a) 行为腿·近战（Shambler）：驯服站狼 + 满血 20HP Shambler + 玩家位在僵尸侦测圈（kDetectRange
    //        16）内（修复前被咬后仍追玩家的可复现口径）——setWolfTarget(僵尸) → 狼群追咬，首口落地
    //        （hp<20，注册面前置）→ 仇恨转移行为证 = 狼掉血（僵尸近战 kAttackDamage=3 是本世界狼的
    //        唯一伤害源；僵尸只有经仇恨分支才近战 mob）+ 双方贴身（dzw≤3.0 打斗带）。事件驱动双证齐
    //        即停（防长窗互殴打死任何一方扰动断言）。
    //    (b) 行为腿·远程（Bones）：驯服狼 + 满血骷髅弓手 —— 狼咬骷髅 → 骷髅转火**保持距离射击**狼：
    //        箭命中狼掉血（kArrowDamage=2，狼 hp<10）。t712 滤网语义腿照绿：狼本就在骷髅箭 mob 结算
    //        名单内（t923 扩），命中即 damageEntity + wolfRetaliateAgainst（源码钉在 t923(d) 既有）。
    //        玩家远置（-1000）：骷髅箭不进玩家命中判定，侦测不到玩家 → 目标判别纯净。
    //    (c) 阴性·被动无仇恨：setWolfTarget(猪) → 狼咬猪（猪掉血）但猪无仇恨系统不还手（狼满血恒 10
    //        ——被动型入口门 no-op，逃跑链不受影响）。
    //    (d) 源码钉：狼咬击点接单一注册入口 mobAggroAgainst(m_wolfTarget, idx) + 敌对近战消费点接
    //        wolfRetaliateAgainst(aggroIdx, idx)（t923 反击面在转火近战路径的接线完整；t923(d) 源码钉
    //        先例——逐帧弹道 / 打斗时序 headless 不稳的面锁接线文本）。
    {
        bool ok = true;
        QString diag;
        auto flatRig948 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        auto tamedWolfAt948 = [](EntityManager &em, int x, int z) -> int {
            const int wolf = em.spawnMobTyped(x, 85, z, EntityManager::MobWolf,
                                              QStringLiteral("#c8ccd4"), 10);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && wolf >= 0 && !tamed; ++attempt)
                tamed = em.tameWolf(wolf);
            return tamed ? wolf : -1;
        };
        auto distXZTo948 = [](const QVector3D &p, const QVector3D &q) {
            return QVector3D(p.x() - q.x(), 0.0f, p.z() - q.z()).length();
        };
        // (a) 近战转火（Shambler 咬回驯服狼 = 仇恨转移行为证）。
        {
            World wa; flatRig948(wa);
            EntityManager ema;
            const int wolf = tamedWolfAt948(ema, 16, 22);
            const int zombie = ema.spawnMobTyped(12, 85, 12, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20);
            // 玩家放僵尸侦测圈内（(12,12)→(22.5,22.5) 距 14.9 < 16）：修复前僵尸被咬后仍追玩家；
            //   修复后转火追狼。玩家位仅作「另一可选目标」存在，断言读狼掉血（判别充分且 RNG 无关）。
            const QVector3D player(22.5f, 85.0f, 22.5f);
            ok = ok && wolf >= 0 && zombie >= 0;
            if (wolf >= 0 && zombie >= 0) {
                ema.setWolfTarget(zombie); // 主人标记 → 狼群追咬（t480 setWolfTarget 单一入口）
                bool bitten = false, fought = false;
                for (int t = 0; t < 1500 && !(bitten && fought); ++t) { // 24s 帽，事件驱动
                    ema.tick(0.016f, &wa, player, 0.3f, 1.8f, true, false);
                    if (ema.healthAt(zombie) < 20) bitten = true; // 狼首口落地（注册面前置证据）
                    if (ema.healthAt(wolf) < 10) fought = true;   // 僵尸还手（仇恨转移 = 本任务断言）
                }
                const float dzw = distXZTo948(ema.posAt(zombie), ema.posAt(wolf));
                ok = ok && bitten && fought && dzw <= 3.0f;
                if (!(bitten && fought && dzw <= 3.0f))
                    diag += QStringLiteral("a bitten=%1 fought=%2 dzw=%3 whp=%4 zhp=%5 ")
                                .arg(int(bitten)).arg(int(fought)).arg(dzw)
                                .arg(ema.healthAt(wolf)).arg(ema.healthAt(zombie));
            } else diag += QStringLiteral("a spawn/tame failed ");
        }
        // (b) 远程转火（Bones 被咬 → 保持距离射击狼；t712 滤网语义腿照绿）。
        {
            World wb; flatRig948(wb);
            EntityManager emb;
            const int wolf = tamedWolfAt948(emb, 30, 12);
            const int bones = emb.spawnMobTyped(24, 85, 12, EntityManager::MobBones,
                                                QStringLiteral("#d8d8e0"), 20);
            const QVector3D far(-1000.0f, 90.0f, -1000.0f); // 玩家远 → 侦测不到，箭不进玩家判定
            ok = ok && wolf >= 0 && bones >= 0;
            if (wolf >= 0 && bones >= 0) {
                emb.setWolfTarget(bones); // 狼群追咬骷髅 → 首口落地注册仇恨
                bool bitten = false, shot = false;
                for (int t = 0; t < 2000 && !(bitten && shot); ++t) { // 32s 帽：拉弓 0.5s + 冷却 2.5s
                    emb.tick(0.016f, &wb, far, 0.3f, 1.8f, true, false);
                    if (emb.healthAt(bones) < 20) bitten = true;
                    if (emb.healthAt(wolf) < 10) shot = true; // 箭命中狼（kArrowDamage=2）= 转火射击证
                }
                ok = ok && bitten && shot;
                if (!(bitten && shot))
                    diag += QStringLiteral("b bitten=%1 shot=%2 whp=%3 bhp=%4 ")
                                .arg(int(bitten)).arg(int(shot))
                                .arg(emb.healthAt(wolf)).arg(emb.healthAt(bones));
            } else diag += QStringLiteral("b spawn/tame failed ");
        }
        // (c) 阴性·被动无仇恨（猪被咬不还手，狼满血恒 10）。
        {
            World wc; flatRig948(wc);
            EntityManager emc;
            const int wolf = tamedWolfAt948(emc, 16, 30);
            const int pig = emc.spawnMobTyped(20, 85, 30, EntityManager::MobPig,
                                              QStringLiteral("#e8a0a0"), 10);
            const QVector3D far(-1000.0f, 90.0f, -1000.0f);
            ok = ok && wolf >= 0 && pig >= 0;
            if (wolf >= 0 && pig >= 0) {
                emc.setWolfTarget(pig);
                bool bitten = false;
                for (int t = 0; t < 1000 && !bitten; ++t) { // 16s 帽：狼咬到猪即止
                    emc.tick(0.016f, &wc, far, 0.3f, 1.8f, true, false);
                    if (emc.healthAt(pig) < 10) bitten = true;
                }
                ok = ok && bitten && emc.healthAt(wolf) == 10; // 猪从不还手（无仇恨系统）
                if (!(bitten && emc.healthAt(wolf) == 10))
                    diag += QStringLiteral("c bitten=%1 whp=%2 ")
                                .arg(int(bitten)).arg(emc.healthAt(wolf));
            } else diag += QStringLiteral("c spawn/tame failed ");
        }
        // (d) 源码钉：咬击点单一注册入口 + 敌对近战消费点反击接线。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile ef(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            const QString t = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
            const bool pinReg = t.contains(QStringLiteral("mobAggroAgainst(m_wolfTarget, idx)"));
            const bool pinRet = t.contains(QStringLiteral("wolfRetaliateAgainst(aggroIdx, idx)"));
            ok = ok && pinReg && pinRet;
            if (!pinReg || !pinRet)
                diag += QStringLiteral("d pinReg=%1 pinRet=%2 ").arg(int(pinReg)).arg(int(pinRet));
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t948 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t948 wolf-bite aggro transfer: the bitten hostile turns on the biting"
                             " wolf -- a shambler (with the player inside its detect band) counter-"
                             "melees the wolf that bit it (wolf hp drop = zombie melee is the only"
                             " damage source and only the revenge branch melee-hits mobs), a bones"
                             " archer keeps distance and shoots the wolf (arrows settle on the wolf"
                             " via the existing t712/t923 filter, semantics intact), a bitten pig"
                             " (passive, no aggro system) never fights back (wolf stays full HP),"
                             " and both wiring sites are source-pinned (bite -> mobAggroAgainst,"
                             " melee consumer -> wolfRetaliateAgainst)"
                             ;
    }

    // ── P-t949 豹猫两修（R19.17 🅲：① 生鱼驯服 live 右键可达 / ② 图鉴驯服猫预览贴图源 × UV 模式同源）──
    //    根因①：eventFilter 右键链的食物分支（foodHungerAmount>0 → beginEating + return）先行拦截——生鱼 /
    //    狼肉既是食物又是 mob 交互材料，placeBlock 内生鱼驯服分支（t481）与狼肉分支（t480）在 live 输入下
    //    **永不可达**（矩阵旧探针直调 tameOcelot/EntityManager 测不到输入翻译缝）。修 = t514/t639①「使用优先
    //    于进食」同构分流：findMobHit 命中豹猫（生鱼）/ 已驯服狼（狼肉）→ placeBlock；未命中 → fall-through 进食。
    //    根因②：图鉴预览 packTextured（t780「pack 命中 → box-UV」）与 t920「驯服猫 → 程序全脸 mob_cat_*」
    //    两开关条件不同源 → 驯服态预览几何以 box-UV 窗采程序猫贴图任意像素 = 混入狼样灰斑（游戏内 delegate
    //    的 ocelotPackHit 自带 !ocatTamed 无此病）。修 = packTextured 门加驯服猫例外（与贴图切换同条件）。
    //    (a) 行为腿·生鱼驯服（真实输入翻译链）：PlayerController 直编挂 QQuickWindow（grab 载体，t891 先例）
    //        + 生存 64 生鱼 + 野豹猫 2.7 格前 → 合成 QMouseEvent 右键 press **直调 eventFilter**（输入第一站）
    //        → 循环至驯中（~1/3 概率 40 次帽；每次 press 泵 >200ms placeBlock CD）→ 断言驯服态位 + 变体
    //        0..2 + 生鱼消耗。阴性轮（回退 gate）本腿必红：食物分支拦截 → 永不进 placeBlock → 恒野。
    //    (b) 阴性·熟鱼不驯（t836 口径豹猫只吃生鱼）：持熟鱼瞄豹猫 press → gate 不接熟鱼 → 食物分支进食
    //        → 不驯 + 鱼不耗（无 tick 不完成进食）。
    //    (c) 行为腿·狼肉喂养回血（同缝同修面，任务行③「喂养回血面顺带核一致」）：驯服狼 damageEntity 到
    //        6HP → 持生牛肉瞄狼 press → gate（isWolfMeatItem + wolfTamed）→ placeBlock 肉分支 healTamedPet
    //        +4 → HP==10 + 肉耗 1。
    //    (d) ②映射单源 + 两消费端源码钉：mobEntityMap() 直调 11→cat/ocelot.png 且 10→wolf/wolf.png（两源
    //        互异 =「豹猫贴图非狼贴图」映射级断言）；Main.qml 钉游戏内 ocelotPackHit 判据含 !ocatTamed +
    //        baseColorMap 收口永不落狼贴图；ResourceBrowser.qml 钉 packTextured 门含驯服猫例外且与 t920
    //        贴图切换同条件（t923(d) 文件读源码钉先例——QML 消费端 headless 不可达）。
    {
        bool ok = true;
        QString diag;
        auto flatRig949 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        // 泵事件循环推墙钟（placeBlock 200ms CD 走 m_evtClock 墙钟——t891 链 B pumpFor 先例）。
        auto pump949 = [](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 合成右键 press 经 sendEvent 投递进 probeWin（pc 在 onWindowChanged 里 installEventFilter 于窗
        //   = 用户真实投递链，filter 命中 o==m_window；eventFilter 是 protected 虚不可直调。t874 合成事件
        //   走 QML TapHandler 有 harness 伪影已弃，此处直达 C++ 事件过滤链，无 QML 队列伪影面）。
        auto rightPress949 = [](PlayerController &, QQuickWindow &win) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(64.0, 64.0), QPointF(64.0, 64.0),
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            return QCoreApplication::sendEvent(&win, &press);
        };
        // 瞄准布置：pc 挂窗 grab 捕获 + 玩家脚位 (mx+0.5, 85, mz+3) 眼 86.62 → 瞄 mob 中心 (mx+0.5, 85.5, mz+0.5)
        //   （dir (0,-1.12,-2.5)，距 2.74 < kReach 5；yaw 0 / pitch -24.12°，loadSavedState 取弧度→角度换算）。
        auto aimRig949 = [](PlayerController &pc, World &w, EntityManager &em, Hotbar &hb,
                            int mx, int mz, int mobType, int itemId) -> int {
            pc.setWorld(&w);
            pc.setEntityManager(&em);
            pc.setHotbar(&hb);
            const int mob = em.spawnMobTyped(mx, 85, mz, mobType, QStringLiteral("#e8c890"), 10);
            hb.setStack(0, itemId, 64, 0);
            hb.setSelectedSlot(0);
            const QVector3D eye(float(mx) + 0.5f, 86.62f, float(mz) + 3.0f);
            const QVector3D dir = (QVector3D(float(mx) + 0.5f, 85.5f, float(mz) + 0.5f) - eye).normalized();
            pc.loadSavedState(eye.x(), 85.0f, eye.z(),
                              qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                              qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            return mob;
        };
        QQuickWindow probeWin949;
        // (a) 生鱼驯服（真实输入链，40 次帽覆盖 ~1/3 驯服 RNG：0.67^40 ≈ 1e-7 漏判率）。
        int consumedA = -1;
        {
            World wa; flatRig949(wa);
            EntityManager ema;
            Hotbar hba;
            PlayerController pca;
            pca.setParentItem(probeWin949.contentItem());
            pca.grab(); // m_window 就绪 → setCaptured(true)（placeBlock/eating 共同入口门）
            pca.setSelectedBlock(int(BR::Air)); // t1040 rig 加固（t1030 同式）：生鱼材料段→Air 建模（eventFilter 喂食分流 + 分支 return 均不读此面，兜底 fall-through 通用放置）
            const int cat = aimRig949(pca, wa, ema, hba, 20, 12,
                                      EntityManager::MobOcelot, RecipeRegistry::RawFishId);
            bool tamed = false;
            if (cat >= 0) {
                for (int attempt = 0; attempt < 40 && !tamed; ++attempt) {
                    if (attempt > 0) pump949(210); // >200ms placeBlock CD（墙钟）
                    rightPress949(pca, probeWin949);
                    tamed = ema.ocelotTamedAt(cat);
                }
            }
            consumedA = 64 - hba.countAt(0);
            const int variant = tamed ? ema.ocelotVariantAt(cat) : -1;
            const bool varOk = tamed && variant >= 0 && variant <= 2;
            ok = ok && cat >= 0 && tamed && varOk && consumedA >= 1 && consumedA <= 40;
            if (!(cat >= 0 && tamed && varOk && consumedA >= 1 && consumedA <= 40))
                diag += QStringLiteral("a cat=%1 tamed=%2 var=%3 consumed=%4 ")
                            .arg(cat).arg(int(tamed)).arg(variant).arg(consumedA);
        }
        // (b) 阴性·熟鱼不驯（gate 不接熟鱼 → 食物分支进食 → placeBlock 不可达）。
        {
            World wb; flatRig949(wb);
            EntityManager emb;
            Hotbar hbb;
            PlayerController pcb;
            pcb.setParentItem(probeWin949.contentItem());
            pcb.grab();
            pcb.setSelectedBlock(int(BR::Air)); // t1040 rig 加固（t1030 同式）：熟鱼食物分支 beginEating 不进 placeBlock，selectedBlock 归 Air 建模（兜底）
            const int cat = aimRig949(pcb, wb, emb, hbb, 20, 12,
                                      EntityManager::MobOcelot, RecipeRegistry::CookedFishId);
            bool tamed = false;
            if (cat >= 0) {
                for (int attempt = 0; attempt < 5 && !tamed; ++attempt) {
                    if (attempt > 0) pump949(210);
                    rightPress949(pcb, probeWin949);
                    tamed = emb.ocelotTamedAt(cat);
                }
            }
            const bool untouched = hbb.countAt(0) == 64; // 进食路径不完成（无 tick）→ 鱼不耗
            ok = ok && cat >= 0 && !tamed && untouched;
            if (!(cat >= 0 && !tamed && untouched))
                diag += QStringLiteral("b cat=%1 tamed=%2 count=%3 ")
                            .arg(cat).arg(int(tamed)).arg(hbb.countAt(0));
        }
        // (c) 狼肉喂养回血（同缝同修面：isWolfMeatItem + 已驯服狼 → placeBlock 肉分支 healTamedPet）。
        {
            World wc; flatRig949(wc);
            EntityManager emc;
            Hotbar hbc;
            PlayerController pcc;
            pcc.setParentItem(probeWin949.contentItem());
            pcc.grab();
            pcc.setSelectedBlock(int(BR::Air)); // t1040 rig 加固（t1030 同式）：狼肉材料段→Air 建模（喂食分流 + 肉分支 return 均不读此面，兜底）
            const int wolf = aimRig949(pcc, wc, emc, hbc, 20, 12,
                                       EntityManager::MobWolf, RecipeRegistry::RawBeefId);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && wolf >= 0 && !tamed; ++attempt)
                tamed = emc.tameWolf(wolf); // 直调驯服（t948 helper 先例；RNG 循环帽）
            emc.damageEntity(wolf, 4); // 10 → 6HP（喂肉回血面：healTamedPet +4 钳上限）
            if (wolf >= 0 && tamed) {
                pump949(210);
                rightPress949(pcc, probeWin949);
            }
            const bool healed = emc.healthAt(wolf) == 10;
            const bool consumed = hbc.countAt(0) == 63; // 喂成功耗 1 生牛肉
            ok = ok && wolf >= 0 && tamed && healed && consumed;
            if (!(wolf >= 0 && tamed && healed && consumed))
                diag += QStringLiteral("c wolf=%1 tamed=%2 hp=%3 count=%4 ")
                            .arg(wolf).arg(int(tamed)).arg(emc.healthAt(wolf)).arg(hbc.countAt(0));
        }
        // (d) ②映射单源直调 + 两消费端源码钉。
        {
            bool mapOcelot = false, mapWolf = false;
            for (const auto &m : mobEntityMap()) {
                if (m.first == 11) mapOcelot = (m.second == QStringLiteral("cat/ocelot.png"));
                if (m.first == 10) mapWolf = (m.second == QStringLiteral("wolf/wolf.png"));
            }
            const bool distinct = mapOcelot && mapWolf; // 豹猫/狼贴图源互异（「非狼贴图」映射级断言）
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile pf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            const QString psrc = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const bool pinGate = psrc.contains(QStringLiteral("if (ocelotFeed || wolfFeed) {"))
                                 && psrc.contains(QStringLiteral("t949 喂食分流优先"));
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString msrc = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            // 游戏内消费端：pack 命中判据排除驯服态（驯服猫恒程序贴图全脸 UV）+ 贴图收口永不落狼。
            const bool pinMain = msrc.contains(QStringLiteral(
                                    "!ocatTamed && mobOcelotPackTex.source.toString().length > 0"))
                                 && msrc.contains(QStringLiteral(
                                    "return ocelotPackHit ? mobOcelotPackTex : mobOcelotTex"));
            QFile rf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
            const QString rsrc = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
            // 查看器消费端：packTextured 门含驯服猫例外（t949 新门）且与 t920 贴图切换同条件并存。
            //   t963 钉合法演化（P-t950(f) 先例）：驯服猫变体 0 贴图源 mob_cat_black → mob_cat_tabby
            //   （全黑档退役，家猫花纹返修）——钉的意图不变：图鉴驯服态 = 程序家猫贴图源。
            const bool pinBrowser = rsrc.contains(QStringLiteral(
                                      "!(root.selectedMobFromSection === 11 && root.mobTamedPreview))"))
                                    && rsrc.contains(QStringLiteral(
                                      "? \"qrc:/textures/mob_cat_tabby.png\""));
            ok = ok && distinct && pinGate && pinMain && pinBrowser;
            if (!(distinct && pinGate && pinMain && pinBrowser))
                diag += QStringLiteral("d mapO=%1 mapW=%2 pinGate=%3 pinMain=%4 pinBrowser=%5 ")
                            .arg(int(mapOcelot)).arg(int(mapWolf)).arg(int(pinGate))
                            .arg(int(pinMain)).arg(int(pinBrowser));
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t949 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t949 ocelot pair-fix: raw-fish right-click on a wild ocelot tames it"
                             " through the REAL input chain (synthesized right-press driven straight"
                             " into eventFilter -- the eat branch no longer swallows the feed; tame"
                             " flag + variant 0..2 + fish consumed), a cooked fish press never tames"
                             " (t836 raw-only caliber: eat path, fish untouched), raw beef on a"
                             " damaged tamed wolf heals it 6->10 through the same gate (t480 meat"
                             " face now reachable live), and the texture source stays single-"
                             " authority: mobEntityMap 11->cat/ocelot.png distinct from 10->wolf/"
                             "wolf.png, the in-game delegate pack-hit excludes the tamed state,"
                             " and the viewer packTextured gate now carries the same tamed-cat"
                             " exception as the t920 texture switch (no more box-UV sampling of the"
                             " program cat art = the wolf-gray mottle the user reported)"
                             ;
    }

    // ── P-t950 mob 装备拾取穿着（R19.17 🅲：僵尸/骷髅**经过**装备掉落物时有概率拾取并穿上——不主动
    //    寻路纯接触判定；按槽位规则更好护甲/武器才换；换下旧装备掉回地面）──
    //    驱动方式：PlayerController 直造（t814 先例，不启 16ms tick）+ EntityManager/ItemEntityManager
    //    注入；实体物理不 tick（mob 定格在掉落物所在格 = 「经过」的接触稳态，游走 RNG 不进断言），扫描
    //    窗由探针直调 tickMobEquipmentPickup(0.5) 定步长递推（窗内 = tickImpl 每帧喂真 dt 的等价累积，
    //    逐窗行为一致；tickImpl 接线行由 (f) 源码钉覆盖——t948(d) 接线文本钉先例）。掉落物新生免拾窗
    //    （kPickupDelayMs=500 墙钟，ItemEntityManager 时钟构造即走无注入缝）用 msleep 越过。
    //    (a) 拾取穿上（概率上端钉 chance=1）：裸装 Shambler 站铁胸甲所在格 → 1 窗内胸甲位变铁胸甲 id +
    //        掉落物从地面消失 + 无旧装备回掉（原空槽）。
    //    (b) 更好规则腿（用户定稿「更好才换」）：铁套 Shambler 路过皮革胸甲（差）→ 6 窗不拾（身上不变 +
    //        掉落物留存）；路过钻石胸甲（好）→ 1 窗内换上 + 被换下铁胸甲**回掉在地面**（新活体同 id）；
    //        回掉铁甲陈化后续窗仍不被回吸（比身上钻石差——严格序天然防循环，亦证非免拾窗假绿）。
    //    (c) 武器腿（数据登记面）：裸手 Shambler 拾木剑（4 > 徒手 1 基线）→ heldItemId=木剑 + 地面消失；
    //        路过铁剑（6 > 4）→ 换持铁剑 + 木剑回掉；回掉木剑陈化后续窗不回吸（4 < 6）；骸骨同规则拾木剑。
    //        手持武器不加成攻击力（AI 常量口径——登记取舍，无行为面可断言）。
    //    (d) 概率下端钉（chance=0）：铁胸甲在场 8 窗恒不拾（护甲/地面全不动）。
    //    (e) 非装备不掉腿：生猪排（食物材料段）+ 弓（attackDamage=徒手 → 非武器口径）在场 → 6 窗恒不拾。
    //    (f) 源码钉：tickImpl 接线行 / 类型门行 / 护甲与武器的严格更大比较行 / removeAt 移除行 / 旧装备
    //        掉地行 / Entity heldItemId 字段行。
    {
        bool ok = true;
        QString diag;
        auto flatRig950 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        // 装备 id（t763/t949 同式字面组装：ArmorIdBase + tier*4 + piece；工具段用 ToolRegistry 枚举）。
        const int leatherChest = int(RecipeRegistry::ArmorIdBase) + 0 * 4 + 1; // 0x301 皮革胸甲（护甲点 3）
        const int ironChest    = int(RecipeRegistry::ArmorIdBase) + 1 * 4 + 1; // 0x305 铁胸甲（护甲点 6）
        const int diaChest     = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1; // 0x311 钻石胸甲（护甲点 8）
        const int woodSword    = int(ToolRegistry::SwordWood); // 0x10C 木剑（攻 4）
        const int ironSword    = int(ToolRegistry::SwordIron); // 0x10E 铁剑（攻 6）
        // 越过掉落物新生免拾窗（isPickupReady 读墙钟 500ms；无注入缝 → 真睡）。
        const auto ageDrop950 = []() { QThread::msleep(560); };
        // 地面上是否有 itemId 活体掉落物（有 → 槽索引，无 → -1）。
        auto findAliveItem950 = [](ItemEntityManager &iem, int itemId) -> int {
            for (int i = 0; i < iem.count(); ++i)
                if (iem.aliveAt(i) && iem.itemIdAt(i) == itemId) return i;
            return -1;
        };
        // 裸装 Shambler 站 (20,85,20)（脱 spawn 随机甲 → 断言面纯净；~20% 生成自带甲会被污染）。
        auto nakedZombie950 = [](EntityManager &em) -> int {
            const int zom = em.spawnMobTyped(20, 85, 20, EntityManager::MobShambler,
                                             QStringLiteral("#4a6a3a"), 20);
            if (zom >= 0) em.setMobArmorSet(zom, -1); // tier<0 = 清空四部位（setMobArmorSet 脱甲语义）
            return zom;
        };
        // (a) 拾取穿上（chance=1 上端钉）。
        {
            World wa; flatRig950(wa);
            EntityManager ema;
            ItemEntityManager iema;
            PlayerController pca;
            pca.setEntityManager(&ema);
            pca.setItemEntities(&iema);
            const int zom = nakedZombie950(ema);
            iema.spawnItem(20, 85, 20, ironChest); // 与 mob 同格（item 中心格心，mob 脚 85.0——竖直窗内）
            ageDrop950();
            pca.setEquipmentPickupChance(1.0);
            pca.tickMobEquipmentPickup(0.5); // 1 窗即拾（chance=1 跳掷骰恒拾）
            const bool worn = ema.mobArmorAt(zom, 1) == ironChest;
            const bool gone = findAliveItem950(iema, ironChest) == -1;
            const bool noDrop = iema.liveCount() == 0; // 原空槽 → 无旧装备回掉
            ok = ok && zom >= 0 && worn && gone && noDrop;
            if (!(zom >= 0 && worn && gone && noDrop))
                diag += QStringLiteral("a zom=%1 worn=%2 gone=%3 noDrop=%4 ")
                            .arg(zom).arg(int(worn)).arg(int(gone)).arg(int(noDrop));
        }
        // (b) 更好规则腿：皮革（差）不拾留存 → 钻石（好）换上 + 铁甲回掉 → 回掉件陈化后不回吸。
        {
            World wb; flatRig950(wb);
            EntityManager emb;
            ItemEntityManager iemb;
            PlayerController pcb;
            pcb.setEntityManager(&emb);
            pcb.setItemEntities(&iemb);
            const int zom = emb.spawnMobTyped(20, 85, 20, EntityManager::MobShambler,
                                              QStringLiteral("#4a6a3a"), 20);
            emb.setMobArmorSet(zom, 1); // 整套铁（胸甲位 = ironChest）
            iemb.spawnItem(20, 85, 20, leatherChest);
            ageDrop950();
            pcb.setEquipmentPickupChance(1.0);
            for (int wi = 0; wi < 6; ++wi) pcb.tickMobEquipmentPickup(0.5);
            const bool worseKept = emb.mobArmorAt(zom, 1) == ironChest
                                   && findAliveItem950(iemb, leatherChest) >= 0;
            iemb.spawnItem(20, 85, 20, diaChest);
            ageDrop950();
            pcb.tickMobEquipmentPickup(0.5); // 1 窗换钻石
            const bool upgraded = emb.mobArmorAt(zom, 1) == diaChest
                                  && findAliveItem950(iemb, diaChest) == -1;
            const bool ironDropped = findAliveItem950(iemb, ironChest) >= 0; // 换下的铁甲回掉在地面
            ageDrop950(); // 回掉件陈化 → 续窗判定走「更好才换」而非免拾窗（防弱断言假绿）
            for (int wi = 0; wi < 2; ++wi) pcb.tickMobEquipmentPickup(0.5);
            const bool noResuck = findAliveItem950(iemb, ironChest) >= 0
                                  && findAliveItem950(iemb, leatherChest) >= 0
                                  && emb.mobArmorAt(zom, 1) == diaChest;
            ok = ok && zom >= 0 && worseKept && upgraded && ironDropped && noResuck;
            if (!(zom >= 0 && worseKept && upgraded && ironDropped && noResuck))
                diag += QStringLiteral("b zom=%1 worse=%2 up=%3 drop=%4 resuck=%5 chest=%6 ")
                            .arg(zom).arg(int(worseKept)).arg(int(upgraded)).arg(int(ironDropped))
                            .arg(int(noResuck)).arg(emb.mobArmorAt(zom, 1));
        }
        // (c) 武器腿：木剑入槽 → 铁剑换下木剑回掉 → 木剑陈化后不回吸；骸骨同规则。
        {
            World wc; flatRig950(wc);
            EntityManager emc;
            ItemEntityManager iemc;
            PlayerController pcc;
            pcc.setEntityManager(&emc);
            pcc.setItemEntities(&iemc);
            const int zom = nakedZombie950(emc);
            iemc.spawnItem(20, 85, 20, woodSword);
            ageDrop950();
            pcc.setEquipmentPickupChance(1.0);
            pcc.tickMobEquipmentPickup(0.5);
            const bool woodHeld = emc.mobHeldItemAt(zom) == woodSword
                                  && findAliveItem950(iemc, woodSword) == -1;
            iemc.spawnItem(20, 85, 20, ironSword);
            ageDrop950();
            pcc.tickMobEquipmentPickup(0.5);
            const bool ironSwap = emc.mobHeldItemAt(zom) == ironSword
                                  && findAliveItem950(iemc, ironSword) == -1
                                  && findAliveItem950(iemc, woodSword) >= 0; // 换下木剑回掉
            ageDrop950(); // 回掉木剑陈化 → 续窗不回吸（4 < 6 严格序，非免拾窗假绿）
            for (int wi = 0; wi < 2; ++wi) pcc.tickMobEquipmentPickup(0.5);
            const bool noResuck = emc.mobHeldItemAt(zom) == ironSword
                                  && findAliveItem950(iemc, woodSword) >= 0;
            const int bones = emc.spawnMobTyped(30, 85, 30, EntityManager::MobBones,
                                                QStringLiteral("#d8d8e0"), 20);
            iemc.spawnItem(30, 85, 30, woodSword); // 骸骨侧独立格同规则（距 (20,20) 11 格零串扰）
            ageDrop950();
            pcc.tickMobEquipmentPickup(0.5);
            const bool bonesHeld = emc.mobHeldItemAt(bones) == woodSword
                                   && findAliveItem950(iemc, ironSword) == -1;
            ok = ok && zom >= 0 && woodHeld && ironSwap && noResuck && bones >= 0 && bonesHeld;
            if (!(zom >= 0 && woodHeld && ironSwap && noResuck && bones >= 0 && bonesHeld))
                diag += QStringLiteral("c zom=%1 wood=%2 swap=%3 resuck=%4 bones=%5 bHeld=%6 ")
                            .arg(zom).arg(int(woodHeld)).arg(int(ironSwap)).arg(int(noResuck))
                            .arg(bones).arg(int(bonesHeld));
        }
        // (d) 概率下端钉（chance=0 → 入口早退，窗都不跑）。
        {
            World wd; flatRig950(wd);
            EntityManager emd;
            ItemEntityManager iemd;
            PlayerController pcd;
            pcd.setEntityManager(&emd);
            pcd.setItemEntities(&iemd);
            const int zom = nakedZombie950(emd);
            iemd.spawnItem(20, 85, 20, ironChest);
            ageDrop950();
            pcd.setEquipmentPickupChance(0.0);
            for (int wi = 0; wi < 8; ++wi) pcd.tickMobEquipmentPickup(0.5);
            const bool untouched = emd.mobArmorAt(zom, 1) == 0
                                   && findAliveItem950(iemd, ironChest) >= 0;
            ok = ok && zom >= 0 && untouched;
            if (!(zom >= 0 && untouched))
                diag += QStringLiteral("d zom=%1 untouched=%2 chest=%3 ")
                            .arg(zom).arg(int(untouched)).arg(emd.mobArmorAt(zom, 1));
        }
        // (e) 非装备不掉腿：生猪排（食物）+ 弓（attackDamage=徒手非武器口径）恒不拾。
        {
            World we; flatRig950(we);
            EntityManager eme;
            ItemEntityManager ieme;
            PlayerController pce;
            pce.setEntityManager(&eme);
            pce.setItemEntities(&ieme);
            const int zom = nakedZombie950(eme);
            ieme.spawnItem(20, 85, 20, RecipeRegistry::RawPorkchopId);
            ieme.spawnItem(21, 85, 20, int(ToolRegistry::Bow)); // 邻格（同在水平 1 格带内）
            ageDrop950();
            pce.setEquipmentPickupChance(1.0);
            for (int wi = 0; wi < 6; ++wi) pce.tickMobEquipmentPickup(0.5);
            const bool kept = findAliveItem950(ieme, RecipeRegistry::RawPorkchopId) >= 0
                              && findAliveItem950(ieme, int(ToolRegistry::Bow)) >= 0
                              && eme.mobArmorAt(zom, 1) == 0 && eme.mobHeldItemAt(zom) == 0;
            ok = ok && zom >= 0 && kept;
            if (!(zom >= 0 && kept))
                diag += QStringLiteral("e zom=%1 kept=%2 ").arg(zom).arg(int(kept));
        }
        // (f) 源码钉：tickImpl 接线 / 类型门 / 严格更大比较（护甲 + 武器）/ removeAt / 旧装备掉地 / 字段。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile pf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            const QString psrc = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const bool pinWire = psrc.contains(QStringLiteral("tickMobEquipmentPickup(dt);"));
            // t952 扩表：拾取类型门加小蹒跚者（白名单两段钉——成人两型头段 + 幼体扩段，任一缺失即红）。
            const bool pinGate = psrc.contains(QStringLiteral(
                "if (mt != EntityManager::MobShambler && mt != EntityManager::MobBones"))
                && psrc.contains(QStringLiteral("&& mt != EntityManager::MobBabyShambler) continue;"));
            const bool pinBetter = psrc.contains(QStringLiteral(
                "if (ArmorRegistry::armorPoints(id) <= ArmorRegistry::armorPoints(cur)) continue;"));
            const bool pinWpn = psrc.contains(QStringLiteral(
                "if (ToolRegistry::attackDamage(id) <= ToolRegistry::attackDamage(cur)) continue;"));
            const bool pinRemove = psrc.contains(QStringLiteral("m_itemEntities->removeAt(ii);"));
            const bool pinDrop = psrc.contains(QStringLiteral(
                "m_itemEntities->spawnItem(fx, fy, fz, itemId, 1);"));
            QFile ef(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            const QString esrc = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
            const bool pinGateEnt = esrc.contains(QStringLiteral(
                "if (e.mobType != MobShambler && e.mobType != MobBones && e.mobType != MobBabyShambler) return false;"));
            QFile ehf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            const QString ehsrc = ehf.open(QIODevice::ReadOnly) ? QString::fromUtf8(ehf.readAll()) : QString();
            const bool pinField = ehsrc.contains(QStringLiteral("int heldItemId = 0;"));
            ok = ok && pinWire && pinGate && pinBetter && pinWpn && pinRemove && pinDrop
                      && pinField && pinGateEnt;
            if (!(pinWire && pinGate && pinBetter && pinWpn && pinRemove && pinDrop
                  && pinField && pinGateEnt))
                diag += QStringLiteral("f wire=%1 gate=%2 better=%3 wpn=%4 rm=%5 drop=%6 fld=%7 gEnt=%8 ")
                            .arg(int(pinWire)).arg(int(pinGate)).arg(int(pinBetter)).arg(int(pinWpn))
                            .arg(int(pinRemove)).arg(int(pinDrop)).arg(int(pinField))
                            .arg(int(pinGateEnt));
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t950 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t950 mob equipment pickup-and-wear: a shambler passing over an iron"
                             " chestplate dons it (armor slot changes, drop vanishes from the ground,"
                             " nothing re-dropped from the empty slot) under chance=1, a worse drop"
                             " (leather on an iron-set mob) stays put across six windows while a"
                             " better one (diamond) swaps in and the displaced iron chestplate falls"
                             " back to the ground and is never re-sucked once aged (strictly-better"
                             " rule, not the pickup-delay window), weapons follow the same rule as a"
                             " data-registered held slot (wood sword picked from bare hands, iron"
                             " sword swaps it out and the wood sword drops back, bones archers obey"
                             " the same rule; attack power stays the AI constant per the registered"
                             " trade-off), chance=0 never picks across eight windows, food and the"
                             " bow (fist-class attackDamage) are never picked, and the tickImpl"
                             " wiring / type gate / strictly-better comparisons / removal / drop-"
                             " back sites are source-pinned"
                             ;
    }

    // ── P-t951 白天阴影 AI（R19.17 🅲：僵尸/骷髅白天优先找阴凉保命；等玩家进阴影才发起攻击；
    //    骷髅可在阴影内射箭（走位不出阴影）；夜间全部现行行为零回归）──
    //    抽搐根因（t670 版）：寻影由 e.burning 驱动——进影即停燃 → burning 翻 false → 立即清阴凉目标回
    //    追玩家 → 一步出影复燃 → 又寻影，追击/避光向量逐 AI tick 交替占优 = 用户「来回转向走出/退回阴影」。
    //    t951 修：白天双状态机（暴晒→寻影优先 / 真遮蔽→持影等玩家入影）+ kShadeHoldSeconds 迟滞窗 +
    //    灼烧采样单源提炼（sunBurnExposureAt：燃烧扣血与避光 AI 同一谓词，禁第二套光照判定）。
    //    驱动方式：EntityManager 直造 + 直调 tick 逐 tick 推进（t948 先例）；skyBrightness 经 tick 新尾参
    //    注入（白天腿 1.0f；夜间腿缺省 0 = 夜间语义 = t951 分支整体旁路，恰为旧 7 参调用形态 = 零回归钉）。
    //    阴凉 rig = 石台整面 + 石檐（setBlock 走 recomputeLightAround → 檐下水平 flood skyLight<15 = 真遮
    //    荫，与燃烧判定同一真值源；腿内先钉 rig 光照真值防伪绿）。
    //    (a) 白天寻影腿：暴晒僵尸（玩家置侦测圈内制造追击压力）→ 走入石檐停驻（檐下格 skyLight<15）且
    //        ≥200 tick 稳定滞留（迟滞窗内不折返、不因追击压力出檐）。
    //    (b) 持影不攻 + 入影开攻腿：檐下僵尸 vs 阳光下 2.4 格玩家（近战射程外一步、追击压力满格）→
    //        ≥300 tick 零攻击且不出檐格；玩家入檐 → 数 tick 内攻击发起（mobAttackedPlayer 信号计数）。
    //    (c) 骷髅影内射击腿：檐下骸骨 vs 阳光下 3 格玩家（< kArcherKeepMin 退避压力恰朝檐外）→ 拉弓射箭
    //        命中玩家（信号计数）且骷髅全程 floor(XZ) 未出檐格（候选落点暴晒弃选闸）+ 终态檐下遮荫。
    //    (d) 夜间零回归腿：缺省 skyBrightness → 暴晒僵尸照旧直线追击并攻击（t951 分支旁路 = 旧行为）。
    //    (e) 源码钉：迟滞刷新行（两 AI ≥2）/ 灼烧谓词定义 + ≥7 处消费（单源禁令行为面）/ 白名单行 +
    //        ≥3 消费 / 攻击压门行 / 弓手候选闸行 / 头文件声明 + 常量 + Entity 字段 / tickImpl 生产接线。
    {
        bool ok = true;
        QString diag;
        auto flatRig951 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        auto roof951 = [](World &w, int x0, int z0, int n) {
            for (int dx = 0; dx < n; ++dx)
                for (int dz = 0; dz < n; ++dz) w.setBlock(x0 + dx, 88, z0 + dz, BR::Stone, 0);
        };
        auto cellIn951 = [](const QVector3D &p, int x0, int z0, int n) {
            const int cx = int(std::floor(p.x())), cz = int(std::floor(p.z()));
            return cx >= x0 && cx < x0 + n && cz >= z0 && cz < z0 + n;
        };
        // (a) 白天寻影：暴晒僵尸走入石檐停驻 + 稳定滞留。
        {
            World wa; flatRig951(wa);
            roof951(wa, 20, 20, 7); // 石檐 (20..26)² @y88 → 檐下 y85 格 skyLight≤14（边缘 14 / 内里 13）
            EntityManager ema;
            int hits = 0;
            QObject::connect(&ema, &EntityManager::mobAttackedPlayer, [&hits](int, int, float, float) { ++hits; });
            const int zom = ema.spawnMobTyped(15, 85, 20, EntityManager::MobShambler,
                                              QStringLiteral("#4a6a3a"), 20);
            // review0830 #6 适配（依据：盔免烧豁免入门后「裸装」从 RNG 巧合变显式前提）：脱 t377 随机
            //   生成甲（~12.5% 带头盔 → 戴盔僵尸不再寻影，裸装腿会偶发红）。
            ema.setMobArmorSet(zom, -1);
            // rig 光照真值钉（遮荫/露天两读数——与燃烧判定同一采样源，防檐没造出 shade 的伪绿）。
            const bool shadeTruth = wa.skyLightAt(23, 85, 23) < 15 && wa.skyLightAt(21, 85, 21) < 15;
            const bool sunTruth = wa.skyLightAt(15, 85, 20) == 15 && wa.skyLightAt(10, 85, 10) == 15;
            // 玩家在侦测圈内（dist≈10 < kDetectRange 16）制造持续追击压力 → 旧行为会朝玩家走。
            const QVector3D player(15.5f, 85.0f, 30.5f);
            bool inShade = false;
            for (int t = 0; t < 900 && !inShade; ++t) { // 14.4s 帽：6 格寻影路程 ≈ 2.5s
                ema.tick(0.016f, &wa, player, 0.3f, 1.8f, true, false, 1.0f);
                inShade = cellIn951(ema.posAt(zom), 20, 20, 7)
                          && wa.skyLightAt(int(std::floor(ema.posAt(zom).x())), 85,
                                           int(std::floor(ema.posAt(zom).z()))) < 15;
            }
            bool stable = inShade;
            for (int t = 0; t < 200 && stable; ++t) { // 3.2s 稳定窗：入影后不折返（迟滞 + 持影等待）
                ema.tick(0.016f, &wa, player, 0.3f, 1.8f, true, false, 1.0f);
                stable = cellIn951(ema.posAt(zom), 20, 20, 7);
            }
            ok = ok && zom >= 0 && shadeTruth && sunTruth && inShade && stable && hits == 0;
            if (!(zom >= 0 && shadeTruth && sunTruth && inShade && stable && hits == 0))
                diag += QStringLiteral("a zom=%1 shadeT=%2 sunT=%3 in=%4 stab=%5 hits=%6 ")
                            .arg(zom).arg(int(shadeTruth)).arg(int(sunTruth)).arg(int(inShade))
                            .arg(int(stable)).arg(hits);
        }
        // (b) 持影不攻（玩家暴晒）→ 玩家入影开攻。
        {
            World wb; flatRig951(wb);
            roof951(wb, 22, 22, 3); // 石檐 (22..24)² @y88；檐下 (23,23) 遮荫、(25,23) 露天
            EntityManager emb;
            int hits = 0;
            QObject::connect(&emb, &EntityManager::mobAttackedPlayer, [&hits](int, int, float, float) { ++hits; });
            const int zom = emb.spawnMobTyped(23, 85, 23, EntityManager::MobShambler,
                                              QStringLiteral("#4a6a3a"), 20);
            emb.setMobArmorSet(zom, -1); // review0830 #6 适配：脱 t377 随机甲（持影腿须裸装，同 (a) 注）
            const bool shadeTruth = wb.skyLightAt(23, 85, 23) < 15 && wb.skyLightAt(25, 85, 23) == 15;
            // 玩家阳光下 2.4 格（> kAttackRange 1.6 一步、< kDetectRange 16 追击压力满格）。
            const QVector3D playerSun(25.9f, 85.0f, 23.5f);
            for (int t = 0; t < 300; ++t) // 4.8s：持影等待——零攻击 + 不出檐格（旧码此窗必追出+咬）
                emb.tick(0.016f, &wb, playerSun, 0.3f, 1.8f, true, false, 1.0f);
            const QVector3D zp = emb.posAt(zom);
            const bool heldInShade = hits == 0 && cellIn951(zp, 22, 22, 3);
            // 玩家入檐（cell 23,23 遮荫 → 暴晒判定翻转）→ 攻击发起。
            const QVector3D playerShade(23.9f, 85.0f, 23.9f);
            bool attacked = false;
            for (int t = 0; t < 600 && !attacked; ++t) {
                emb.tick(0.016f, &wb, playerShade, 0.3f, 1.8f, true, false, 1.0f);
                attacked = hits > 0;
            }
            ok = ok && zom >= 0 && shadeTruth && heldInShade && attacked;
            if (!(zom >= 0 && shadeTruth && heldInShade && attacked))
                diag += QStringLiteral("b zom=%1 shadeT=%2 held=%3 atk=%4 hits=%5 ")
                            .arg(zom).arg(int(shadeTruth)).arg(int(heldInShade))
                            .arg(int(attacked)).arg(hits);
        }
        // (c) 骷髅影内射箭（走位不出阴影）。
        {
            World wc; flatRig951(wc);
            roof951(wc, 20, 20, 3); // 石檐 (20..22)² @y88；骷髅贴北缘（z=20），退避压力恰朝檐外
            EntityManager emc;
            int hits = 0;
            QObject::connect(&emc, &EntityManager::mobAttackedPlayer, [&hits](int, int, float, float) { ++hits; });
            const int bones = emc.spawnMobTyped(21, 85, 21, EntityManager::MobBones,
                                                QStringLiteral("#d8d8e0"), 20);
            emc.setMobArmorSet(bones, -1); // review0830 #6 适配：脱 t377 随机甲（候选闸腿须裸装，同 (a) 注）
            const bool shadeTruth = wc.skyLightAt(21, 85, 21) < 15 && wc.skyLightAt(21, 85, 23) == 15;
            // 玩家阳光下 dist≈2.6（< kArcherKeepMin 5 → 保持带退避方向 = -z = 檐外；
            // ≤ kArcherShootRange 12 + 视线越檐清 → 射门常开）。
            // **定窗驱动**（不因命中提前停）：退避压力全程在场 ≥19s 覆盖多轮拉弓/冷却——冷却期 draw=0
            // 全速退避，无候选闸必跨过檐缘（阴性 B 签名）；有闸则每 tick 目的地暴晒即弃选，永久滞留檐内。
            const QVector3D player(21.5f, 85.0f, 23.5f);
            bool stayedIn = true;
            for (int t = 0; t < 1200; ++t) { // 19.2s：拉弓 0.5s + 冷却 2.5s 多轮（t948 b 同式帽量级）
                emc.tick(0.016f, &wc, player, 0.3f, 1.8f, true, false, 1.0f);
                if (!cellIn951(emc.posAt(bones), 20, 20, 3)) stayedIn = false; // 候选闸失效即出檐
            }
            const bool arrowHit = hits > 0; // 箭命中玩家（抛物解算确定性 + kArrowSpread 抖动多发射窗覆盖）
            const QVector3D bp = emc.posAt(bones);
            const bool endShaded = cellIn951(bp, 20, 20, 3)
                                   && wc.skyLightAt(int(std::floor(bp.x())), 85,
                                                    int(std::floor(bp.z()))) < 15;
            ok = ok && bones >= 0 && shadeTruth && stayedIn && arrowHit && endShaded;
            if (!(bones >= 0 && shadeTruth && stayedIn && arrowHit && endShaded))
                diag += QStringLiteral("c bones=%1 shadeT=%2 stay=%3 shot=%4 end=%5 hits=%6 ")
                            .arg(bones).arg(int(shadeTruth)).arg(int(stayedIn))
                            .arg(int(arrowHit)).arg(int(endShaded)).arg(hits);
        }
        // (d) 夜间零回归：缺省 skyBrightness（=旧 7 参调用形态）→ 暴晒僵尸照旧追击并攻击。
        {
            World wd; flatRig951(wd);
            EntityManager emd;
            int hits = 0;
            QObject::connect(&emd, &EntityManager::mobAttackedPlayer, [&hits](int, int, float, float) { ++hits; });
            const int zom = emd.spawnMobTyped(12, 85, 12, EntityManager::MobShambler,
                                              QStringLiteral("#4a6a3a"), 20);
            const QVector3D player(20.5f, 85.0f, 20.5f); // dist≈12 ≤ kDetectRange 16
            bool attacked = false;
            for (int t = 0; t < 1500 && !attacked; ++t) { // 24s 帽：12 格追击 ≈ 4.3s（t948 a 同式）
                emd.tick(0.016f, &wd, player, 0.3f, 1.8f, true, false); // 缺省尾参 = 夜间语义零回归钉
                attacked = hits > 0;
            }
            const QVector3D zp = emd.posAt(zom);
            const bool closedUp = QVector3D(zp.x() - 20.5f, 0.0f, zp.z() - 20.5f).length() <= 2.0f;
            ok = ok && zom >= 0 && attacked && closedUp;
            if (!(zom >= 0 && attacked && closedUp))
                diag += QStringLiteral("d zom=%1 atk=%2 close=%3 hits=%4 ")
                            .arg(zom).arg(int(attacked)).arg(int(closedUp)).arg(hits);
        }
        // (e) 源码钉：迟滞 / 单源谓词 / 白名单 / 攻击压门 / 候选闸 / 声明常量字段 / 生产接线。
        {
            auto countSub951 = [](const QString &hay, const QString &needle) {
                int n = 0;
                for (int pos = hay.indexOf(needle); pos >= 0; pos = hay.indexOf(needle, pos + needle.size()))
                    ++n;
                return n;
            };
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile ef(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
            const QString esrc = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
            QFile ehf(root + QStringLiteral("/src/Entities/entitymanager.h"));
            const QString ehsrc = ehf.open(QIODevice::ReadOnly) ? QString::fromUtf8(ehf.readAll()) : QString();
            QFile pf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
            const QString psrc = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const bool pinHold = countSub951(esrc, QStringLiteral("e.shadeHoldTimer = kShadeHoldSeconds;")) >= 2;
            const bool pinPredDef = esrc.contains(QStringLiteral("bool EntityManager::sunBurnExposureAt(World *world"));
            const bool pinSingle = countSub951(esrc, QStringLiteral("sunBurnExposureAt(")) >= 6;
            // t952 扩表：亡灵白名单加小蹒跚者（单一权威名单的合法演化，钉演化后的整行——名单本体
            //   消失 / 消费点缺失仍红，防 B6 名单漂移的契约不变）。
            const bool pinWhite = esrc.contains(QStringLiteral(
                "return mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler;"))
                                  && countSub951(esrc, QStringLiteral("undeadBurnsInDaylight(e.mobType)")) >= 3;
            const bool pinSuppress = esrc.contains(QStringLiteral("&& !attackSuppressed"));
            const bool pinGate = esrc.contains(QStringLiteral("wantMove = false; // t951 候选落点暴晒 → 弃选"));
            const bool pinDecl = ehsrc.contains(QStringLiteral("static bool sunBurnExposureAt(World *world"));
            const bool pinConst = ehsrc.contains(QStringLiteral("static constexpr float kShadeHoldSeconds   = 1.0f;"));
            const bool pinField = ehsrc.contains(QStringLiteral("float shadeHoldTimer = 0.0f;"));
            const bool pinWire = psrc.contains(QStringLiteral("m_worldClock ? float(m_worldClock->skyLight()) : 0.0f);"));
            ok = ok && pinHold && pinPredDef && pinSingle && pinWhite && pinSuppress
                      && pinGate && pinDecl && pinConst && pinField && pinWire;
            if (!(pinHold && pinPredDef && pinSingle && pinWhite && pinSuppress
                  && pinGate && pinDecl && pinConst && pinField && pinWire))
                diag += QStringLiteral("e hold=%1 def=%2 single=%3 white=%4 sup=%5 gate=%6 decl=%7 const=%8 fld=%9 wire=%10 ")
                            .arg(int(pinHold)).arg(int(pinPredDef)).arg(int(pinSingle)).arg(int(pinWhite))
                            .arg(int(pinSuppress)).arg(int(pinGate)).arg(int(pinDecl)).arg(int(pinConst))
                            .arg(int(pinField)).arg(int(pinWire));
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t951 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t951 daytime shade AI: a sun-exposed shambler (player pressing inside"
                             " its detect band) walks into a stone overhang and settles in truly"
                             " shaded cells, holding there across a stability window; a sheltered"
                             " zombie facing a sun-lit player 2.4 blocks away neither attacks nor"
                             " leaves the overhang for 300 ticks and opens the attack the moment the"
                             " player steps into the shade; a sheltered bones archer under retreat"
                             " pressure (player 2.6 blocks, inside the keep-min band) still shoots and"
                             " lands an arrow on the exposed player while floor(XZ) never leaves the"
                             " overhang cells (movement candidates that would land in burning sunlight"
                             " are dropped); with the sky channel at its night default the legacy"
                             " behavior is bit-identical (shambler charges and attacks in the open);"
                             " and the shade-hold hysteresis lines, the single sun-exposure sampling"
                             " authority (burn + both AIs + player verdict), the undead whitelist,"
                             " the attack-suppression gate, the archer candidate gate, the header"
                             " declaration/constants/field and the production skyBrightness wiring"
                             " are source-pinned"
                             ;
    }

    // ── P-t952 小蹒跚者 + 小鸡骑士（R19.17 🅲：<1 格高、移速快、可穿盔甲的幼体僵尸；生成时概率与
    //    小鸡组合成「小鸡骑士」——小鸡驮小僵尸，骑手 AI 驱动载具位移）──
    //    驱动方式：EntityManager / PlayerController 直造直调（t950/t951 先例，不启 16ms tick）；
    //    骑士组合概率经 setChickenJockeyChance 缝写端钉（同 t950 setEquipmentPickupChance 先例）——
    //    所有非组合腿先钉 0.0 防缺省 5% 概率的随机鸡污染断言。
    //    (a) 生成表项腿：spawnMobTyped 小蹒跚者 → halfH=0.45 < 0.5（<1 格高口径）且 < 成体 0.90、
    //        hostile=true、radius < 成体。
    //    (b) 移速快腿：同距（5 格）追击同一玩家位 30 tick 位移对比——小蹒跚者 > 成体 ×1.15
    //        （kBabyShamblerChaseSpeedMul=1.4 实测投影；双方恒 chase 无 RNG 路径）。
    //    (c) 可穿盔甲腿（t950 拾取面白名单扩小蹒跚者）：裸装小蹒跚者站铁胸甲格 → 1 窗拾取穿上
    //        （chance=1 端钉；胸甲位 id 变 + 掉落物消失）。
    //    (d) 骑士组合上端钉（chance=1）：生成小蹒跚者 → 必组合：同格出现小鸡、双向链互指
    //        （rideMob/mobRider）、骑手钉载具顶（rider.y == mount.y + mount.halfH + rider.halfH）；
    //        推进 10 tick 后钉位关系保持（XZ 重合 + Y 恒载具顶）。
    //    (e) 骑士组合下端钉（chance=0）：生成小蹒跚者 → 恒独立（rideMob=-1、全场无小鸡、liveCount=1）。
    //    (f) 分离腿 ①：小鸡被杀 → 小僵尸落地独立（rideMob 清 -1、存活、Y 从载具顶落回地面 <86.0）。
    //    (g) 分离腿 ②：小僵尸被杀 → 小鸡独立存活（mobRider 清 -1、小鸡活体）。
    //    (h) 蛋表 / 刷怪笼行为腿：mobTypeForSpawnEgg(0x25D)==MobBabyShambler（单一权威表）+ spawner
    //        state 编码→解码 round-trip；成体蹒跚者蛋映射不被扩表污染。
    //    (i) 源码钉：枚举/字段/常量（Entities .h）、组合钩子与概率、黑暗刷怪幼体翻变、主循环被骑乘
    //        冻结 + aiAccum 累积、载具 AI 挂起分支、挂载 pass 接线、晒燃白名单、玩家推挤成对豁免、
    //        aiHostile 快速低伤参数分支、Renderer 分支与白名单表行、拾取门扩段、蛋表 case、创造
    //        调色板、图鉴条目 / 蛋映射 / Loader 贴图行、蛋图标 case。
    {
        bool ok = true;
        QString diag;
        auto flatRig952 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        // 骑士链一致性：rideMob/mobRider 双向互指且都有效（防单向断链假组合）。
        auto jockeyLinked952 = [](EntityManager &em, int baby, int chicken) {
            return em.rideMobAt(baby) == chicken && em.mobRiderAt(chicken) == baby;
        };
        // (a) 生成表项：<1 格高幼体盒 + 敌对语义。
        {
            World wa; flatRig952(wa);
            EntityManager ema;
            ema.setChickenJockeyChance(0.0); // 防缺省 5% 随机组合污染尺寸/速度腿
            const int baby = ema.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            const int adult = ema.spawnMobTyped(30, 85, 30, EntityManager::MobShambler,
                                                QStringLiteral("#4a6a3a"), 0);
            const bool small = ema.aliveAt(baby) && ema.halfHeightAt(baby) < 0.5f;
            const bool smallerThanAdult = ema.halfHeightAt(baby) < ema.halfHeightAt(adult);
            const bool slim = ema.radiusAt(baby) < ema.radiusAt(adult);
            const bool hostileBaby = ema.isHostileAt(baby) && ema.mobTypeAt(baby) == EntityManager::MobBabyShambler;
            ok = ok && baby >= 0 && adult >= 0 && small && smallerThanAdult && slim && hostileBaby;
            if (!(baby >= 0 && adult >= 0 && small && smallerThanAdult && slim && hostileBaby))
                diag += QStringLiteral("a baby=%1 adult=%2 small=%3 smaller=%4 slim=%5 hostile=%6 ")
                            .arg(baby).arg(adult).arg(int(small)).arg(int(smallerThanAdult))
                            .arg(int(slim)).arg(int(hostileBaby));
        }
        // (b) 移速快：同距追击 30 tick 位移 baby > adult ×1.15。
        {
            World wb; flatRig952(wb);
            EntityManager emb;
            emb.setChickenJockeyChance(0.0);
            const QVector3D player(25.0f, 85.0f, 25.0f);
            const int baby = emb.spawnMobTyped(21, 85, 25, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            const int adult = emb.spawnMobTyped(29, 85, 25, EntityManager::MobShambler,
                                                QStringLiteral("#4a6a3a"), 0);
            const QVector3D b0 = emb.posAt(baby), a0 = emb.posAt(adult);
            for (int t = 0; t < 30; ++t)
                emb.tick(0.016f, &wb, player, 0.3f, 1.8f, true, false, 0.0f); // 夜间语义（t951 旁路 → 纯追击）
            const auto xzDist = [](const QVector3D &from, const QVector3D &to) {
                const float dx = to.x() - from.x(), dz = to.z() - from.z();
                return std::sqrt(dx * dx + dz * dz);
            };
            const float babyDisp = xzDist(b0, emb.posAt(baby));
            const float adultDisp = xzDist(a0, emb.posAt(adult));
            const bool bothMoved = adultDisp > 0.3f;
            const bool babyFaster = babyDisp > adultDisp * 1.15f;
            ok = ok && baby >= 0 && adult >= 0 && bothMoved && babyFaster;
            if (!(baby >= 0 && adult >= 0 && bothMoved && babyFaster))
                diag += QStringLiteral("b baby=%1 adult=%2 bDisp=%3 aDisp=%4 ")
                            .arg(baby).arg(adult).arg(babyDisp).arg(adultDisp);
        }
        // (c) 可穿盔甲：t950 拾取链对小蹒跚者生效（门扩段行为面）。
        {
            World wc; flatRig952(wc);
            EntityManager emc;
            ItemEntityManager iemc;
            PlayerController pcc;
            pcc.setEntityManager(&emc);
            pcc.setItemEntities(&iemc);
            emc.setChickenJockeyChance(0.0);
            const int baby = emc.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            emc.setMobArmorSet(baby, -1); // 脱 spawn 随机甲（~20% 概率自带）→ 断言面纯净（t950 同式）
            const int ironChest = int(RecipeRegistry::ArmorIdBase) + 1 * 4 + 1; // 0x305 铁胸甲（t950 同式组装）
            iemc.spawnItem(20, 85, 20, ironChest);
            QThread::msleep(560); // 越过掉落物新生免拾窗（kPickupDelayMs，t950 同式）
            pcc.setEquipmentPickupChance(1.0);
            pcc.tickMobEquipmentPickup(0.5);
            const bool worn = emc.mobArmorAt(baby, 1) == ironChest;
            const bool gone = [&]() {
                for (int i = 0; i < iemc.count(); ++i)
                    if (iemc.aliveAt(i) && iemc.itemIdAt(i) == ironChest) return false;
                return true;
            }();
            ok = ok && baby >= 0 && worn && gone;
            if (!(baby >= 0 && worn && gone))
                diag += QStringLiteral("c baby=%1 worn=%2 gone=%3 chest=%4 ")
                            .arg(baby).arg(int(worn)).arg(int(gone)).arg(emc.mobArmorAt(baby, 1));
        }
        // (d) 骑士组合上端钉（chance=1 全组合）+ 钉位关系推进保持。
        {
            World wd; flatRig952(wd);
            EntityManager emd;
            emd.setChickenJockeyChance(1.0);
            const int baby = emd.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            int chicken = -1;
            for (int i = 0; i < emd.count(); ++i)
                if (emd.aliveAt(i) && emd.mobTypeAt(i) == EntityManager::MobChicken) { chicken = i; break; }
            const bool linked = baby >= 0 && chicken >= 0 && jockeyLinked952(emd, baby, chicken);
            const float mountTopY = emd.posAt(chicken).y() + emd.halfHeightAt(chicken);
            const bool pinned = baby >= 0 && chicken >= 0
                                && std::abs(emd.posAt(baby).y() - (mountTopY + emd.halfHeightAt(baby))) < 1e-3f;
            const QVector3D player(25.0f, 85.0f, 25.0f);
            bool heldAfterTicks = true;
            for (int t = 0; t < 10; ++t) {
                emd.tick(0.016f, &wd, player, 0.3f, 1.8f, true, false, 0.0f);
                const QVector3D rp = emd.posAt(baby), mp = emd.posAt(chicken);
                if (emd.rideMobAt(baby) != chicken
                    || std::abs(rp.x() - mp.x()) > 1e-4f || std::abs(rp.z() - mp.z()) > 1e-4f
                    || std::abs(rp.y() - (mp.y() + emd.halfHeightAt(chicken) + emd.halfHeightAt(baby))) > 1e-3f) {
                    heldAfterTicks = false;
                    break;
                }
            }
            ok = ok && baby >= 0 && chicken >= 0 && linked && pinned && heldAfterTicks;
            if (!(baby >= 0 && chicken >= 0 && linked && pinned && heldAfterTicks))
                diag += QStringLiteral("d baby=%1 chicken=%2 linked=%3 pinned=%4 held=%5 ")
                            .arg(baby).arg(chicken).arg(int(linked)).arg(int(pinned)).arg(int(heldAfterTicks));
        }
        // (e) 下端钉（chance=0 全独立）。
        {
            World we; flatRig952(we);
            EntityManager eme;
            eme.setChickenJockeyChance(0.0);
            const int baby = eme.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            bool anyChicken = false;
            for (int i = 0; i < eme.count(); ++i)
                if (eme.aliveAt(i) && eme.mobTypeAt(i) == EntityManager::MobChicken) anyChicken = true;
            const bool independent = baby >= 0 && eme.rideMobAt(baby) == -1 && !anyChicken && eme.liveCount() == 1;
            ok = ok && independent;
            if (!independent)
                diag += QStringLiteral("e baby=%1 rideMob=%2 chicken=%3 live=%4 ")
                            .arg(baby).arg(baby >= 0 ? eme.rideMobAt(baby) : -2)
                            .arg(int(anyChicken)).arg(eme.liveCount());
        }
        // (f) 分离腿 ①：小鸡死 → 小僵尸落地独立（存活 + 链清 + Y 落回地面）。
        {
            World wf; flatRig952(wf);
            EntityManager emf;
            emf.setChickenJockeyChance(1.0);
            const int baby = emf.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            const int chicken = emf.rideMobAt(baby);
            const QVector3D player(25.0f, 85.0f, 25.0f);
            for (int t = 0; t < 3; ++t) emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f);
            const float mountedY = emf.posAt(baby).y();
            emf.damageEntity(chicken, 999); // 小鸡被杀（dead 即时翻 → 挂载 pass 对账解除）
            for (int t = 0; t < 20; ++t) emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f);
            const bool riderAlive = emf.aliveAt(baby) && !emf.deadAt(baby);
            const bool unlinked = emf.rideMobAt(baby) == -1;
            const bool landed = emf.posAt(baby).y() < mountedY - 0.2f && emf.posAt(baby).y() < 86.0f;
            const bool mountDead = emf.deadAt(chicken);
            ok = ok && baby >= 0 && chicken >= 0 && riderAlive && unlinked && landed && mountDead;
            if (!(baby >= 0 && chicken >= 0 && riderAlive && unlinked && landed && mountDead))
                diag += QStringLiteral("f baby=%1 chicken=%2 alive=%3 unlink=%4 y=%5 mY=%6 mDead=%7 ")
                            .arg(baby).arg(chicken).arg(int(riderAlive)).arg(int(unlinked))
                            .arg(emf.posAt(baby).y()).arg(mountedY).arg(int(mountDead));
        }
        // (g) 分离腿 ②：小僵尸死 → 小鸡独立存活（链清 + 小鸡活体）。
        {
            World wg; flatRig952(wg);
            EntityManager emg;
            emg.setChickenJockeyChance(1.0);
            const int baby = emg.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 0);
            const int chicken = emg.rideMobAt(baby);
            const QVector3D player(25.0f, 85.0f, 25.0f);
            for (int t = 0; t < 3; ++t) emg.tick(0.016f, &wg, player, 0.3f, 1.8f, true, false, 0.0f);
            emg.damageEntity(baby, 999); // 小僵尸被杀
            for (int t = 0; t < 8; ++t) emg.tick(0.016f, &wg, player, 0.3f, 1.8f, true, false, 0.0f);
            const bool chickenAlive = emg.aliveAt(chicken) && !emg.deadAt(chicken);
            const bool freed = emg.mobRiderAt(chicken) == -1;
            ok = ok && baby >= 0 && chicken >= 0 && emg.deadAt(baby) && chickenAlive && freed;
            if (!(baby >= 0 && chicken >= 0 && emg.deadAt(baby) && chickenAlive && freed))
                diag += QStringLiteral("g baby=%1 chicken=%2 bDead=%3 cAlive=%4 freed=%5 ")
                            .arg(baby).arg(chicken).arg(int(emg.deadAt(baby)))
                            .arg(int(chickenAlive)).arg(int(freed));
        }
        // (h) 蛋表 / 刷怪笼 round-trip 行为腿。
        {
            EntityManager emh;
            const int eggId = RecipeRegistry::SpawnEggBabyShamblerId;
            const bool eggMap = RecipeRegistry::mobTypeForSpawnEgg(eggId) == EntityManager::MobBabyShambler;
            const bool adultUnpolluted = RecipeRegistry::mobTypeForSpawnEgg(RecipeRegistry::SpawnEggShamblerId)
                                         == EntityManager::MobShambler;
            const quint8 cageState = BlockRegistry::spawnerStateForMob(EntityManager::MobBabyShambler);
            const bool cageRoundTrip = emh.spawnerMobTypeForState(int(cageState)) == EntityManager::MobBabyShambler;
            ok = ok && eggMap && adultUnpolluted && cageRoundTrip;
            if (!(eggMap && adultUnpolluted && cageRoundTrip))
                diag += QStringLiteral("h egg=%1 adult=%2 cage=%3 ")
                            .arg(int(eggMap)).arg(int(adultUnpolluted)).arg(int(cageRoundTrip));
        }
        // (i) 源码钉（相对 exe ../ = 工程根；t950 同式）。
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            const auto readSrc = [&root](const QString &rel) {
                QFile pf(root + QLatin1Char('/') + rel);
                return pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            };
            const QString entCpp = readSrc(QStringLiteral("src/Entities/entitymanager.cpp"));
            const QString entH = readSrc(QStringLiteral("src/Entities/entitymanager.h"));
            const QString modelCpp = readSrc(QStringLiteral("src/Renderer/mobmodel.cpp"));
            const QString pcCpp = readSrc(QStringLiteral("src/Game/playercontroller.cpp"));
            const QString recipeCpp = readSrc(QStringLiteral("src/Game/recipe.cpp"));
            const QString hotbarCpp = readSrc(QStringLiteral("src/Game/hotbar.cpp"));
            const QString mainQml = readSrc(QStringLiteral("src/ui/Main.qml"));
            const QString browserQml = readSrc(QStringLiteral("src/ui/ResourceBrowser.qml"));
            const QString iconQml = readSrc(QStringLiteral("src/ui/MaterialIcon.qml"));
            // Entities：枚举 + Entity 双向链字段 + 概率常量（.h）。
            const bool pinEnum = entH.contains(QStringLiteral("MobBabyShambler = 19"));
            // review0830 #18：音频层裸 19 镜像枚举配对钉——小蹒跚者环境音改写行（idx 19 → 4 = 复用成体
            //   蹒跚者音色；Audio 层不 include entitymanager.h 故裸字面量）与 Entities 枚举值 19 配对
            //   断言（本测试 TU 合法 include 全栈）：枚举值漂移 / 行被误删任一即红（历史 MC id 71 vs
            //   内部 135 跨层字面量漂移的防线；预防性钉——现状即绿）。
            const QString audioCpp = readSrc(QStringLiteral("src/Audio/audiomanager.cpp"));
            const bool pinAudioMirror = audioCpp.contains(QStringLiteral("if (idx == 19) idx = 4;"))
                                        && EntityManager::MobBabyShambler == 19;
            const bool pinFieldRide = entH.contains(QStringLiteral("int rideMob = -1;"));
            const bool pinFieldRider = entH.contains(QStringLiteral("int mobRider = -1;"));
            const bool pinChance = entH.contains(QStringLiteral("kChickenJockeyChance"));
            const bool pinSetter = entH.contains(QStringLiteral("setChickenJockeyChance(qreal chance);"));
            // Entities：生成组合钩子 / 概率掷骰 / 黑暗刷怪幼体翻变（.cpp）。
            const bool pinCombineHook = entCpp.contains(QStringLiteral(
                "if (slot >= 0 && mobType == MobBabyShambler) tryFormChickenJockey(slot);"));
            const bool pinRoll = entCpp.contains(QStringLiteral("m_chickenJockeyChance < 1.0"));
            const bool pinNatural = entCpp.contains(QStringLiteral("finalSpawnType = MobBabyShambler;"))
                                    && entCpp.contains(QStringLiteral("kBabyShamblerSpawnChance"));
            // Entities：被骑乘冻结 + 节拍累积 / 载具 AI 挂起 / 挂载 pass 接线 / 推挤成对豁免。
            const bool pinFreeze = entCpp.contains(QStringLiteral("if (e.rideMob >= 0) {"))
                                   && entCpp.contains(QStringLiteral(
                                       "e.aiAccum += float(dt); // 骑手 AI 节拍累积"));
            const bool pinMountSuspend = entCpp.contains(QStringLiteral("} else if (e.mobRider >= 0) {"));
            const bool pinPassWire = entCpp.contains(QStringLiteral(
                "if (tickMobMounts(world, listener, worldW, worldD, playerTargetable, skyBrightness)) dirty = true;"));
            const bool pinPushSkip = entCpp.contains(QStringLiteral(
                "|| e.rideMob >= 0 || e.mobRider >= 0) continue;"));
            // Entities：晒燃白名单扩段（t951 单一权威名单）。
            const bool pinBurnWhite = entCpp.contains(QStringLiteral(
                "return mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler;"));
            // Entities：aiHostile 快速低伤参数分支。
            const bool pinBabyParams = entCpp.contains(QStringLiteral(
                "const bool isBabyShambler = (e.mobType == MobBabyShambler);"))
                && entCpp.contains(QStringLiteral("kBabyShamblerChaseSpeedMul"))
                && entCpp.contains(QStringLiteral("kBabyShamblerAttackDamage"));
            // Renderer：几何分支 + 白名单表行。
            const bool pinModelBranch = modelCpp.contains(QStringLiteral("else if (m_mobType == 19) {"));
            const bool pinModelTable = modelCpp.contains(QStringLiteral("/* 19 BabyShambler */ true"));
            // Game：拾取门扩段 / 蛋表 case / 创造调色板。
            const bool pinPickupGate = pcCpp.contains(QStringLiteral(
                "&& mt != EntityManager::MobBabyShambler) continue;"));
            const bool pinEggCase = recipeCpp.contains(QStringLiteral(
                "case SpawnEggBabyShamblerId: return EntityManager::MobBabyShambler;"));
            const bool pinPalette = hotbarCpp.contains(QStringLiteral(
                "int(RecipeRegistry::SpawnEggBabyShamblerId),"));
            // QML：图鉴条目 / 蛋映射 / delegate Loader + 程序贴图 / 蛋图标 case。
            const bool pinBrowserEntry = browserQml.contains(QStringLiteral(
                "{ mobType: 19, name: \"小蹒跚者\" }"));
            // t989 演化（P-t949(d) 先例）：原钉 QML 蛋映射行 `case 0x25D: return 19;`——该镜像行随
            //   查看器蛋分区整段退役（权威收编 RecipeRegistry::mobTypeForSpawnEgg，egg=1 钉不变）→
            //   翻转为断言代码形态绝迹（登记注释可提及 0x25D，故钉带 case 前缀的代码形态）。
            const bool pinBrowserEgg = !browserQml.contains(QStringLiteral("case 0x25D"));
            const bool pinLoader = mainQml.contains(QStringLiteral(
                "active: entKind === EntityManager.Mob && entMobType === EntityManager.MobBabyShambler"));
            const bool pinTex = mainQml.contains(QStringLiteral("mob_baby_shambler.png"));
            const bool pinIcon = iconQml.contains(QStringLiteral("drawSpawnEgg(\"babyshambler\")"));
            const bool pinsOk = pinEnum && pinFieldRide && pinFieldRider && pinChance && pinSetter
                && pinCombineHook && pinRoll && pinNatural && pinFreeze && pinMountSuspend
                && pinPassWire && pinPushSkip && pinBurnWhite && pinBabyParams
                && pinModelBranch && pinModelTable && pinPickupGate && pinEggCase && pinPalette
                && pinBrowserEntry && pinBrowserEgg && pinLoader && pinTex && pinIcon
                && pinAudioMirror;
            ok = ok && pinsOk;
            if (!pinsOk)
                diag += QStringLiteral("i enum=%1 ride=%2 rider=%3 chance=%4 setter=%5 hook=%6 roll=%7 "
                                       "nat=%8 frz=%9 susp=%10 wire=%11 push=%12 burn=%13 prm=%14 "
                                       "mbr=%15 mtb=%16 gate=%17 egg=%18 pal=%19 brE=%20 brg=%21 "
                                       "ldr=%22 tex=%23 ico=%24 aud=%25 ")
                            .arg(int(pinEnum)).arg(int(pinFieldRide)).arg(int(pinFieldRider))
                            .arg(int(pinChance)).arg(int(pinSetter)).arg(int(pinCombineHook))
                            .arg(int(pinRoll)).arg(int(pinNatural)).arg(int(pinFreeze))
                            .arg(int(pinMountSuspend)).arg(int(pinPassWire)).arg(int(pinPushSkip))
                            .arg(int(pinBurnWhite)).arg(int(pinBabyParams)).arg(int(pinModelBranch))
                            .arg(int(pinModelTable)).arg(int(pinPickupGate)).arg(int(pinEggCase))
                            .arg(int(pinPalette)).arg(int(pinBrowserEntry)).arg(int(pinBrowserEgg))
                            .arg(int(pinLoader)).arg(int(pinTex)).arg(int(pinIcon))
                            .arg(int(pinAudioMirror));
        }
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t952 diag]" << diag;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t952 baby shambler + chicken jockey: the spawned baby stands under"
                             " half a block tall (halfH 0.45, below the adult's 0.90) with a slimmer"
                             " hostile box; chasing a player from the same 5-block distance it covers"
                             " over 1.15x the adult's ground in 30 ticks (fast caliber); it picks up"
                             " and wears an iron chestplate it stands on through the equipment-"
                             " pickup whitelist (t950 face extended); with the jockey chance pinned"
                             " to 1 every baby spawn combines with a chicken at the same cell - the"
                             " two-slot pair is cross-linked and the rider stays pinned on the"
                             " mount's top across ticks while the mount's XZ follows the rider -"
                             " and pinned to 0 every baby spawns independent with no chicken in the"
                             " world; killing the chicken drops the baby back to the ground alive"
                             " and unlinked, killing the baby frees the chicken alive and unlinked;"
                             " the egg-table and spawner round-trips map the new egg to the baby"
                             " type without polluting the adult shambler egg; and the enum/fields/"
                             " chance, the combine hook and roll, the natural-spawn baby flip, the"
                             " ridden freeze + beat accumulation, the mount AI suspension, the pass"
                             " wiring, the pair push-skip, the daylight-burn whitelist, the fast-"
                             " low-damage AI parameters, the renderer branch and table row, the"
                             " pickup-gate extension, the egg-table case, the creative palette, the"
                             " encyclopedia entry/egg map, the delegate loader/texture and the egg"
                             " icon are source-pinned"
                             ;
    }

    // ── P-r0830C review-2026-08-30 批 C（生物：中 #5/#6 + 低 #13/#14/#15/#16/#17/#18/#19/#26）探针 ──
    //   十修一登（审查建议照单全收），驱动方式 = EntityManager / PlayerController / Hotbar / MinecartManager
    //   直造直调（P-t947/t950/t951/t952 先例，独立小世界免态串扰）。每腿回退对应修法即红：
    //   (a) #5（中）t947 豹猫缺观察者跟随门（同构宠物只修一侧）：驯服站猫距主人 6.0 → spectator 1.5s
    //       → 距 ≥4.0（跟随会收进 2.5 停步带 = 旧版红）；主人距 15（> kOcelotTeleportDist 12）→
    //       spectator 2s → 距 ≥10（瞬移补位会落 2-5 格环 = 旧版红）；创造/生存对照腿照常收进 ≤3.0
    //       + 对称 sync pin（狼门与猫门同一字面量门形，审查六-3「对称提交配对称腿」，count ≥2）
    //       + 分发点/签名透传钉。
    //   (b) #6（中）t951 头盔免疫未纳入避光门：裸装对照腿（寻影照旧 = 修法零回归锚）+ 戴盔近战腿
    //       （白天暴晒戴盔 Shambler 照常追到阳光下玩家并咬击 —— 旧版寻影弃追压攻击 = 红）+ 戴盔弓手腿
    //       （dayShadeAi 关 → 候选落点暴晒弃选闸随之关闭 → 走位踏出檐外；旧版滞留檐内 = 红）
    //       + 燃烧豁免行保持钉（「仍不燃烧」面的源级契约）+ 两处 dayShadeAi 盔豁免行 count==2 钉。
    //   (c) #13（低）t952 mobAggroAgainst 受害者枚举门漏幼体：狼咬小蹒跚者 → 幼体转火追咬狼（狼掉血 =
    //       幼体近战唯一伤害源）——旧版注册侧 no-op 幼体恒追玩家 = 红 + switch case 钉。
    //   (d) #14（低）t947 攻击距离内矮障碍瞬态起跳：驯服狼贴脸防御静止猪目标（distXZ 1.0 ≤ kAttackRange
    //       1.6）前方 0.6 格 1 格矮墙 → 继续咬击（hits ≥3）且全程贴地（跳起抬升 <0.15；旧版边咬边跳
    //       ≥1.0 = 红）。t1031 rig 迁移：旧「野狼咬玩家」供流转驯服狼防御分支供流（同 chase lambda 同
    //       咬带门，野狼中立收口后旧供流面不复存在）；目标静止 = setWanderFrozen 冻结。
    //       t988 门收窄演化：压跳语义收窄为「同层可咬」（|tdy| ≤ 0.5）——本腿同层（dy=0）照旧压跳保持绿，
    //       异层面由 P-t988(c) 承接。
    //   (e) #16（低）t950 骑乘态 mob 未被拾取扫描排除：乘矿车 Shambler 压着铁胸甲 6 窗恒不拾 + 同窗
    //       裸装地面对照腿照拾（扫描活证）+ #15 概率缺省单源钉（成员初始化 = kEquipPickupChance）。
    //   (f) #19（低）t952 骑乘解除后陈旧 jumpG 滑流：骑士组合东行撞墙（钉位恒 resting → aiHostile 跳
    //       分支积东向滑流）→ 玩家移师西面（组合掉头西撤、无新跳覆盖）→ 杀鸡 → 下落期逐帧位移无东向帧
    //       （旧版尾段滑流东漂 ~1 格 = 红签名）+ 钉位段清滑流行钉。
    //   (g) #17（低）t951 非追击态不走寻影登记：纯注释钉（aiHostile 早退点 + aiArcher 同位 + 头文件口径）。
    //   (h) #26（低）t952 attackMob 亡灵谓词未扩幼体（被 t961 显示面放大为显示与实战劈叉）：亡灵杀手 III
    //       钻石剑 → 幼体实伤 15 == 成体实伤 15 == 基伤 7 + 显示面 (+8)（显示==实战对拍）；蜘蛛 7
    //       （亡灵族门不外泄）+ 无附魔幼体 7（族门是使能方）+ isUndeadFamily 单一权威钉（与
    //       undeadBurnsInDaylight 语义分立：亡灵族门不含头盔免烧豁免）。
    //   #18（低）音频层裸 19 镜像配对钉落在 P-t952(i) pinEnum 组（现有 audiomanager 行 + 枚举值配对
    //       断言，防未来枚举漂移——预防性钉，现状即绿）。
    {
        bool okA = false, okB = false, okC = false, okD = false,
             okE = false, okF = false, okG = false, okH = false;
        QString diag;
        const QString exeDirC = QCoreApplication::applicationDirPath();
        const QString rootC = QDir(exeDirC + QStringLiteral("/..")).absolutePath();
        auto readSrcC = [&rootC](const QString &rel) -> QString {
            QFile f(rootC + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString entCppC = readSrcC(QStringLiteral("src/Entities/entitymanager.cpp"));
        const QString entHC = readSrcC(QStringLiteral("src/Entities/entitymanager.h"));
        const QString pcCppC = readSrcC(QStringLiteral("src/Game/playercontroller.cpp"));
        const QString pcHC = readSrcC(QStringLiteral("src/Game/playercontroller.h"));
        auto countSubC = [](const QString &hay, const QString &needle) {
            int n = 0;
            for (int pos = hay.indexOf(needle); pos >= 0; pos = hay.indexOf(needle, pos + needle.size()))
                ++n;
            return n;
        };
        auto flatRigC = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(26);
            for (int x = 0; x < 44; ++x)
                for (int z = 0; z < 44; ++z) {
                    for (int y = 85; y <= 95; ++y) w.setBlock(x, y, z, BR::Air, 0);
                    w.setBlock(x, 84, z, BR::Stone, 0);
                }
        };
        auto roofC = [](World &w, int x0, int z0, int n) {
            for (int dx = 0; dx < n; ++dx)
                for (int dz = 0; dz < n; ++dz) w.setBlock(x0 + dx, 88, z0 + dz, BR::Stone, 0);
        };
        auto cellInC = [](const QVector3D &p, int x0, int z0, int n) {
            const int cx = int(std::floor(p.x())), cz = int(std::floor(p.z()));
            return cx >= x0 && cx < x0 + n && cz >= z0 && cz < z0 + n;
        };
        auto distXZC = [](const QVector3D &p, const QVector3D &q) {
            return QVector3D(p.x() - q.x(), 0.0f, p.z() - q.z()).length();
        };
        // 驯服站猫（~1/3 → 循环掷到成功；spawnMobTyped 默认站态，t947 驯狼同式）。
        auto tamedCatAtC = [](EntityManager &em, int x, int z) -> int {
            const int cat = em.spawnMobTyped(x, 85, z, EntityManager::MobOcelot,
                                             QStringLiteral("#e8c890"), 10);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && cat >= 0 && !tamed; ++attempt)
                tamed = em.tameOcelot(cat);
            return tamed ? cat : -1;
        };
        // 驯服站狼（t947 同式）。
        auto tamedWolfAtC = [](EntityManager &em, int x, int z) -> int {
            const int wolf = em.spawnMobTyped(x, 85, z, EntityManager::MobWolf,
                                              QStringLiteral("#c8ccd4"), 10);
            bool tamed = false;
            for (int attempt = 0; attempt < 200 && wolf >= 0 && !tamed; ++attempt)
                tamed = em.tameWolf(wolf);
            return tamed ? wolf : -1;
        };

        // ── (a) #5 豹猫观察者跟随门（镜像 t947 狼三腿 + 对称 sync pin）──
        {
            bool a1 = false, a2 = false, a3 = false;
            {
                World wa; flatRigC(wa);
                EntityManager ema;
                const int cat = tamedCatAtC(ema, 16, 22);
                const QVector3D owner(22.5f, 85.0f, 22.5f); // 距猫落点 (16.5,22.5) 恰 6.0
                if (cat >= 0) {
                    for (int t = 0; t < 94; ++t) // 1.504s
                        ema.tick(0.016f, &wa, owner, 0.3f, 1.8f, true, true); // spectator=true
                    a1 = distXZC(ema.posAt(cat), owner) >= 4.0f;
                    if (!a1) diag += QStringLiteral("a1 d=%1 ").arg(distXZC(ema.posAt(cat), owner));
                } else diag += QStringLiteral("a1 tame failed ");
            }
            {
                World wb; flatRigC(wb);
                EntityManager emb;
                const int cat = tamedCatAtC(emb, 16, 22);
                const QVector3D owner(31.5f, 85.0f, 22.5f); // 距 15.0 > kOcelotTeleportDist 12
                if (cat >= 0) {
                    for (int t = 0; t < 125; ++t) // 2.0s
                        emb.tick(0.016f, &wb, owner, 0.3f, 1.8f, true, true);
                    a2 = distXZC(emb.posAt(cat), owner) >= 10.0f;
                    if (!a2) diag += QStringLiteral("a2 d=%1 ").arg(distXZC(emb.posAt(cat), owner));
                } else diag += QStringLiteral("a2 tame failed ");
            }
            {
                World wc; flatRigC(wc);
                EntityManager emc;
                const int cat = tamedCatAtC(emc, 16, 22);
                const QVector3D owner(22.5f, 85.0f, 22.5f);
                if (cat >= 0) {
                    for (int t = 0; t < 94; ++t) // 1.504s：猫速 4.0 > 距差 → 收进停步带
                        emc.tick(0.016f, &wc, owner, 0.3f, 1.8f, true, false); // 创造/生存照常跟随
                    a3 = distXZC(emc.posAt(cat), owner) <= 3.0f;
                    if (!a3) diag += QStringLiteral("a3 d=%1 ").arg(distXZC(emc.posAt(cat), owner));
                } else diag += QStringLiteral("a3 tame failed ");
            }
            // 对称 sync pin（审查六-3）：狼门与猫门同一字面量门形（同缩进两行体）——一侧回退即掉 1。
            const int gateForms = countSubC(entCppC, QStringLiteral(
                "if (playerSpectator)\n        return aiWander(e, dt, world, worldW, worldD, speedScale);"));
            // 分发点透传 + 双宠物签名钉（头文件里两声明同以 playerSpectator 参数收尾）。
            const bool pinDispatch = entCppC.contains(QStringLiteral(
                "if (aiOcelot(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale, playerSpectator))"))
                && countSubC(entHC, QStringLiteral("bool playerSpectator);")) == 2;
            okA = a1 && a2 && a3 && gateForms >= 2 && pinDispatch;
            if (!(a1 && a2 && a3 && gateForms >= 2 && pinDispatch))
                diag += QStringLiteral("a pins g=%1 dsp=%2 ").arg(gateForms).arg(int(pinDispatch));
        }

        // ── (b) #6 头盔免烧豁免入门（裸装对照 + 戴盔近战 + 戴盔弓手 + 燃烧豁免保持钉）──
        {
            // (b0) 裸装对照：白天暴晒僵尸走入石檐停驻（寻影照旧 = t951(a) 同 rig 的零回归锚）。
            bool b0 = false;
            {
                World wb0; flatRigC(wb0);
                roofC(wb0, 20, 20, 7);
                EntityManager emb0;
                const int zom = emb0.spawnMobTyped(15, 85, 20, EntityManager::MobShambler,
                                                   QStringLiteral("#4a6a3a"), 20);
                emb0.setMobArmorSet(zom, -1); // 裸装对照 = 显式裸装（脱 t377 随机甲 ~12.5% 头盔，同 t951 适配注）
                const bool shadeTruth = zom >= 0 && wb0.skyLightAt(23, 85, 23) < 15;
                const QVector3D player(15.5f, 85.0f, 30.5f); // 侦测圈内追击压力（同 t951(a)）
                bool inShade = false;
                if (zom >= 0) {
                    for (int t = 0; t < 900 && !inShade; ++t) {
                        emb0.tick(0.016f, &wb0, player, 0.3f, 1.8f, true, false, 1.0f);
                        inShade = cellInC(emb0.posAt(zom), 20, 20, 7);
                    }
                }
                b0 = zom >= 0 && shadeTruth && inShade;
                if (!b0) diag += QStringLiteral("b0 zom=%1 sT=%2 in=%3 ")
                                     .arg(zom).arg(int(shadeTruth)).arg(int(inShade));
            }
            // (b1) 戴盔近战腿：暴晒 + 盔 → 不寻影不压攻击，照常追到阳光下玩家并咬击。
            bool b1 = false;
            {
                World wb1; flatRigC(wb1);
                roofC(wb1, 20, 20, 7);
                EntityManager emb1;
                int hits = 0;
                QObject::connect(&emb1, &EntityManager::mobAttackedPlayer,
                                 [&hits](int, int, float, float) { ++hits; });
                const int zom = emb1.spawnMobTyped(15, 85, 20, EntityManager::MobShambler,
                                                   QStringLiteral("#4a6a3a"), 20);
                const bool helmet = zom >= 0 && emb1.setMobArmorSet(zom, 1)
                                    && emb1.mobArmorAt(zom, 0) != 0; // 头盔部位在位（豁免门的前置）
                const QVector3D player(15.5f, 85.0f, 30.5f); // 阳光下玩家（暴晒 = 持影等待的对象）
                bool closed = false;
                if (zom >= 0 && helmet) {
                    for (int t = 0; t < 900 && !closed; ++t) { // 14.4s：10.5 格追击 ≈ 3.8s + 咬击节流
                        emb1.tick(0.016f, &wb1, player, 0.3f, 1.8f, true, false, 1.0f);
                        closed = distXZC(emb1.posAt(zom), player) <= 2.0f && hits > 0;
                    }
                }
                b1 = zom >= 0 && helmet && closed;
                if (!b1) diag += QStringLiteral("b1 zom=%1 hel=%2 close=%3 hits=%4 ")
                                     .arg(zom).arg(int(helmet)).arg(int(closed)).arg(hits);
            }
            // (b2) 戴盔弓手腿：dayShadeAi 关 → 候选落点暴晒弃选闸随之关闭 → 退避走位踏出檐外。
            bool b2 = false;
            {
                World wb2; flatRigC(wb2);
                roofC(wb2, 20, 20, 3);
                EntityManager emb2;
                const int bones = emb2.spawnMobTyped(21, 85, 21, EntityManager::MobBones,
                                                     QStringLiteral("#d8d8e0"), 20);
                const bool helmet = bones >= 0 && emb2.setMobArmorSet(bones, 1)
                                    && emb2.mobArmorAt(bones, 0) != 0;
                const bool shadeTruth = wb2.skyLightAt(21, 85, 21) < 15 && wb2.skyLightAt(21, 85, 23) == 15;
                const QVector3D player(21.5f, 85.0f, 23.5f); // dist≈2.6 < kArcherKeepMin → 退避方向 = 檐外
                bool leftRoof = false;
                if (bones >= 0 && helmet) {
                    for (int t = 0; t < 1200 && !leftRoof; ++t) { // 19.2s：拉弓/冷却多轮窗（t951(c) 同式）
                        emb2.tick(0.016f, &wb2, player, 0.3f, 1.8f, true, false, 1.0f);
                        leftRoof = !cellInC(emb2.posAt(bones), 20, 20, 3);
                    }
                }
                b2 = bones >= 0 && helmet && shadeTruth && leftRoof;
                if (!b2) diag += QStringLiteral("b2 bones=%1 hel=%2 sT=%3 left=%4 ")
                                     .arg(bones).arg(int(helmet)).arg(int(shadeTruth)).arg(int(leftRoof));
            }
            // 燃烧豁免行保持（「仍不燃烧」面的源级契约——本 rig 不跑 tickHostileLife，燃烧面钉在燃烧
            //   调用点原文上）+ 两处 dayShadeAi 盔豁免行（修法本体，count==2）。
            const bool pinBurnKept = entCppC.contains(QStringLiteral(
                "&& undeadBurnsInDaylight(e.mobType) && e.armorHelmet == 0;"));
            const bool pinShadeHelmet = countSubC(entCppC, QStringLiteral(
                "undeadBurnsInDaylight(e.mobType)\n                            && e.armorHelmet == 0;")) == 2;
            okB = b0 && b1 && b2 && pinBurnKept && pinShadeHelmet;
            if (!(b0 && b1 && b2 && pinBurnKept && pinShadeHelmet))
                diag += QStringLiteral("b pins burn=%1 shade=%2 ").arg(int(pinBurnKept)).arg(int(pinShadeHelmet));
        }

        // ── (c) #13 狼咬幼体 → 幼体转火追咬狼（受害者枚举门补幼体）──
        {
            World wc; flatRigC(wc);
            EntityManager emc;
            emc.setChickenJockeyChance(0.0); // 防缺省 5% 随机组合污染（t952 同式）
            const int wolf = tamedWolfAtC(emc, 16, 22);
            const int baby = emc.spawnMobTyped(12, 85, 12, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 20);
            const QVector3D player(22.5f, 85.0f, 22.5f); // 幼体侦测圈内（旧版被咬后仍追玩家的对照面）
            bool bitten = false, fought = false;
            if (wolf >= 0 && baby >= 0) {
                emc.setWolfTarget(baby); // 狼群追咬幼体（t480 setWolfTarget 单一入口）
                for (int t = 0; t < 1500 && !(bitten && fought); ++t) { // 24s 帽（t948(a) 同式）
                    emc.tick(0.016f, &wc, player, 0.3f, 1.8f, true, false); // 夜间语义免日光干扰
                    if (emc.healthAt(baby) < 20) bitten = true; // 狼首口落地（注册面前置证据）
                    if (emc.healthAt(wolf) < 10) fought = true; // 幼体还手（仇恨转移 = 本任务断言）
                }
                const float dzw = distXZC(emc.posAt(baby), emc.posAt(wolf));
                okC = bitten && fought && dzw <= 3.0f;
                if (!okC) diag += QStringLiteral("c bit=%1 fight=%2 dzw=%3 ")
                                      .arg(int(bitten)).arg(int(fought)).arg(dzw);
            } else {
                okC = false;
                diag += QStringLiteral("c spawn/tame failed ");
            }
            // switch 门钉：幼体 case 在受害者门内（回退即红）。
            okC = okC && entCppC.contains(QStringLiteral(
                "case MobBabyShambler: break; // 仇恨 AI 消费面"));
        }

        // ── (d) #14 攻击距离内不起跳（跳探加 distXZ 门）──
        //   t1031 rig 迁移：旧 rig 以「野狼咬玩家」供咬击流；t1031 野狼中立收口（aiWolf 未驯服分支纯
        //   游荡）后改由**驯服狼防御分支**供流——同 chase lambda、同咬击带门（distXZ ≤ kAttackRange +
        //   |tdy| ≤ 0.5 同层压跳），#14 钉语义不变。目标 = 静止猪（spawn 在墙格内 = 原 rig「玩家点在
        //   墙格」同位；setWanderFrozen 冻结 wander 钉位；20HP 承 3 口存活，4HP/口 t947 钉）。
        {
            World wd; flatRigC(wd);
            EntityManager emd;
            emd.setWanderFrozen(true); // 静止目标（狼防御/跟随不走 aiWander，冻结不波及狼追击）
            const int pig = emd.spawnMobTyped(21, 85, 22, EntityManager::MobPig,
                                              QStringLiteral("#e8a0a0"), 20); // 咬击带内（狼 20.5 → 21.5 距 1.0）
            const int wolf = emd.spawnMobTyped(20, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            emd.setTameRollOverride(0); // t1031 缝必成（直调驯服——阴性轮摘骨头分流本腿恒绿）
            const bool tamedD = wolf >= 0 && emd.tameWolf(wolf);
            emd.setTameRollOverride(-1);
            for (int z = 21; z <= 23; ++z) wd.setBlock(21, 85, z, BR::Stone, 0); // 矮墙（狼前方 0.6 格探针位）
            const QVector3D player(21.9f, 85.0f, 22.5f); // 主人位（狼-猪带内；狼到位后 < kFollowMinDist 站定）
            int hits = 0;
            if (tamedD && pig >= 0) {
                emd.setWolfTarget(pig); // 狼群追咬目标（t480 setWolfTarget 单一入口，防御分支供流）
                int prevHp = emd.healthAt(pig);
                // 60 tick 稳定窗（实测 6 tick 时仍处落地下沉中段 85.2995 → 贴支撑顶 85.45 的 +0.15
                //   上 snap 会被误读成起跳；1s 后真值静止，起跳签名 ≥0.9 与噪声硬分界）。
                for (int t = 0; t < 60; ++t) emd.tick(0.016f, &wd, player, 0.3f, 1.8f, true, false);
                const float baseY = emd.posAt(wolf).y();
                float maxY = baseY;
                int maxAt = -1;
                for (int t = 0; t < 625; ++t) { // 10s：咬击冷却 1s → ≥3 口（边咬证据）
                    emd.tick(0.016f, &wd, player, 0.3f, 1.8f, true, false);
                    const int hpNow = emd.healthAt(pig);
                    if (hpNow < prevHp) { ++hits; prevHp = hpNow; } // 防御咬击落地面（4HP/口掉血计数）
                    const float yNow = emd.posAt(wolf).y();
                    if (yNow > maxY) { maxY = yNow; maxAt = t; }
                }
                okD = wolf >= 0 && hits >= 3 && (maxY - baseY) < 0.15f;
                diag += QStringLiteral("d wolf=%1 pig=%2 hits=%3 lift=%4 maxAt=%5 base=%6 ")
                            .arg(wolf).arg(pig).arg(hits).arg(maxY - baseY).arg(maxAt).arg(baseY);
            } else {
                okD = false;
                diag += QStringLiteral("d spawn/tame failed ");
            }
            // 跳探门钉：新门形在位（t988 收窄：异层目标咬带内仍探跳）+ 旧无距离门形绝迹（本函数内）。
            okD = okD && entCppC.contains(QStringLiteral(
                "if (e.resting && world && (distXZ > kAttackRange || std::abs(tdy) > 0.5f)) {"));
        }

        // ── (e) #16 骑乘态不拾 + 地面对照（+ #15 缺省单源钉）──
        {
            World we; flatRigC(we);
            EntityManager eme;
            eme.setChickenJockeyChance(0.0);
            MinecartManager cartsE;
            eme.setVehicleManagers(&cartsE, nullptr); // 载具注入（登乘扫描数据源）
            ItemEntityManager ieme;
            PlayerController pce;
            pce.setEntityManager(&eme);
            pce.setItemEntities(&ieme);
            const int ironChest = int(RecipeRegistry::ArmorIdBase) + 1 * 4 + 1;    // t950 同式组装
            const int leatherChest = int(RecipeRegistry::ArmorIdBase) + 0 * 4 + 1;
            const int rider = eme.spawnMobTyped(20, 85, 20, EntityManager::MobShambler,
                                                QStringLiteral("#4a6a3a"), 20);
            if (rider >= 0) eme.setMobArmorSet(rider, -1); // 脱 spawn 随机甲（断言面纯净）
            cartsE.spawnCart(20, 85, 20, &we); // 同格矿车（登乘带内）
            eme.tickVehicleRiding();           // Pass C 登乘扫描（生产接线 = PlayerController 每帧调）
            const bool boarded = rider >= 0 && eme.rideCartAt(rider) >= 0;
            const int walker = eme.spawnMobTyped(30, 85, 30, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20); // 地面对照（远离车）
            if (walker >= 0) eme.setMobArmorSet(walker, -1);
            ieme.spawnItem(20, 85, 20, ironChest);    // 骑手脚下（座位钉位格）
            ieme.spawnItem(30, 85, 30, leatherChest); // 对照脚下
            QThread::msleep(560); // 越过掉落物新生免拾窗（kPickupDelayMs 墙钟，t950 同式）
            pce.setEquipmentPickupChance(1.0);
            for (int wi = 0; wi < 6; ++wi) pce.tickMobEquipmentPickup(0.5);
            const bool riderSkipped = eme.mobArmorAt(rider, 1) == 0
                && [&]() {
                    for (int i = 0; i < ieme.count(); ++i)
                        if (ieme.aliveAt(i) && ieme.itemIdAt(i) == ironChest) return true;
                    return false;
                }(); // 骑乘态：甲未穿 + 铁胸甲留存
            const bool walkerPicked = walker >= 0 && eme.mobArmorAt(walker, 1) == leatherChest;
            // #15 概率缺省单源钉：成员初始化走常量（双字面量形态绝迹）。
            const bool pinChance = pcHC.contains(QStringLiteral("qreal m_equipPickupChance = kEquipPickupChance;"))
                && !pcHC.contains(QStringLiteral("qreal m_equipPickupChance = 0.3;"));
            okE = boarded && riderSkipped && walkerPicked && pinChance;
            if (!(boarded && riderSkipped && walkerPicked && pinChance))
                diag += QStringLiteral("e boarded=%1 skip=%2 walk=%3 pin=%4 ")
                            .arg(int(boarded)).arg(int(riderSkipped)).arg(int(walkerPicked))
                            .arg(int(pinChance));
        }

        // ── (f) #19 骑乘解除后陈旧滑流（钉位段清 jumpG）──
        {
            World wf; flatRigC(wf);
            EntityManager emf;
            emf.setChickenJockeyChance(1.0); // 必组合（t952 上端钉同式）
            const int baby = emf.spawnMobTyped(20, 85, 20, EntityManager::MobBabyShambler,
                                               QStringLiteral("#5a7a42"), 20);
            int chicken = -1;
            for (int i = 0; i < emf.count(); ++i)
                if (emf.aliveAt(i) && emf.mobTypeAt(i) == EntityManager::MobChicken) { chicken = i; break; }
            for (int z = 19; z <= 21; ++z) wf.setBlock(23, 85, z, BR::Stone, 0); // 3 宽矮墙（跳探 0.6 格窗内）
            QVector3D player(27.0f, 85.0f, 20.5f); // 东 → 骑手驱动组合东行撞墙
            bool stalled = false, movedWest = false, eastDrift = false, unlinked = false;
            if (baby >= 0 && chicken >= 0) {
                for (int t = 0; t < 190 && !stalled; ++t) { // 3s：撞墙 stall（钉位恒 resting → 跳分支积东向滑流；
                                                            //   t1008② 起跳意图转移载具 → 组合会翻过 1 格矮墙，
                                                            //   本相位只要求「抵墙带」触发，过墙不否定后续断言）
                    emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f);
                    if (emf.posAt(baby).x() >= 22.3f) stalled = true;
                }
                // 侧压持续窗：stall 阈值 22.3 处跳探前向格仍是空气（fx = floor(22.9) = 22），须再压
                //   ≥ 数个 AI tick 让骑手抵墙脸 22.55+（fx = 23 = 墙格）→ 跳分支反复点火积东向滑流
                //   （t1008② 起点火同时转移载具起跳 = 组合翻墙东行——陈旧滑流的「点火-清零」循环不变）。
                for (int t = 0; t < 90 && stalled; ++t)
                    emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f);
                player = QVector3D(13.0f, 85.0f, 20.5f); // 玩家移师西 → 骑手 AI 掉头（西向无障 → 不覆写滑流）
                const float xWestStart = emf.posAt(baby).x();
                for (int t = 0; t < 180 && emf.posAt(baby).x() > 19.0f; ++t) // 西撤至离墙带 + 离玩家
                    emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f); // 尚远（≤19；t1008② 起
                // 组合过墙翻到东侧，西撤须再翻墙一次——距离闸（非旧 40tick 定长帽）对「卡墙/过墙」两态
                // 同收敛；杀鸡点离玩家 ≥6 格 → 解除挂载后独立追击恒向西，>0.02 东向帧只剩滑流签名）。
                movedWest = emf.posAt(baby).x() <= 21.8f;
                diag += QStringLiteral("f westDx=%1 ")
                            .arg(xWestStart - emf.posAt(baby).x());
                float prevX = emf.posAt(baby).x();
                emf.damageEntity(chicken, 999); // 杀鸡 → 挂载解除 → 下落期（陈旧滑流施加窗）
                for (int t = 0; t < 40; ++t) {
                    emf.tick(0.016f, &wf, player, 0.3f, 1.8f, true, false, 0.0f);
                    const float xNow = emf.posAt(baby).x();
                    if (xNow - prevX > 0.02f) eastDrift = true; // 东向位移帧 = 陈旧滑流签名
                    prevX = xNow;
                }
                unlinked = emf.rideMobAt(baby) == -1 && emf.aliveAt(baby) && !emf.deadAt(baby);
            }
            okF = baby >= 0 && chicken >= 0 && stalled && movedWest && unlinked && !eastDrift;
            diag += QStringLiteral("f baby=%1 chk=%2 stall=%3 west=%4 east=%5 link=%6 x=%7 ")
                        .arg(baby).arg(chicken).arg(int(stalled)).arg(int(movedWest))
                        .arg(int(eastDrift)).arg(int(unlinked))
                        .arg(baby >= 0 ? emf.posAt(baby).x() : -1.0f);
            // 钉位段清滑流行钉（与清 vy 同段；行被删即红）。
            okF = okF && entCppC.contains(QStringLiteral(
                "if (e.jumpGX != 0.0f || e.jumpGZ != 0.0f) { e.jumpGX = 0.0f; e.jumpGZ = 0.0f; dirty = true; }"));
            // review0901 #31：钉位段 resting 置位语句恰一处（批 C #19 编辑曾在既有行前叠插一行同文——
            //   幂等无害但属残留；count 钉防同类手误再进，再叠/散改即红）。
            okF = okF && entCppC.count(QStringLiteral("if (!e.resting) e.resting = true;")) == 1;
        }

        // ── (g) #17 非追击态不走寻影登记（纯注释钉：两 AI 早退点 + 头文件口径）──
        okG = countSubC(entCppC, QStringLiteral("review0830 #17 登记")) >= 2
           && entHC.contains(QStringLiteral("review0830 #17 登记"));

        // ── (h) #26 亡灵杀手对幼体生效 + 显示==实战对拍 ──
        {
            World wh; flatRigC(wh);
            EntityManager emh;
            emh.setChickenJockeyChance(0.0); // 幼体独立生成（组合会改站位）
            const int diaSword = int(ToolRegistry::DiamondSword); // 基伤 7
            const int smite3 = EnchantRegistry::pack(int(EnchantRegistry::UndeadSlay), 3); // III → +7.5
            const QVariantList smiteL{smite3, 0, 0, 0};
            Hotbar hbh;
            const QString famText = hbh.displayFamilyBonusText(smiteL); // t961 显示面 "(+8)"
            const int famM = famText.startsWith(QStringLiteral("(+"))
                                 ? famText.mid(2, famText.size() - 3).toInt() : -1;
            // 单发实伤采样：attackMob 私有 → 走 Q_INVOKABLE beginMining 的真实攻击链（t242 路径 =
            //   attackMob 唯一生产入口，t866「beginMining 不可直驱」注记的公开替代面）；命中即置攻击
            //   冷却且直调不 tick 冷却不走 → 每 mob 独立 pc/hb 采样。m_hitDist 缺省 5.0（updateRaycast
            //   未跑）→ 瞄 2.7 格内 mob 恒 mobDist ≤ m_hitDist；m_vel 零 → 暴击分支天然旁路。
            QQuickWindow probeWinH;
            auto hitOnceC = [&](int x, int z, int mobType, bool enchanted) -> int {
                Hotbar hb;
                PlayerController pc;
                pc.setParentItem(probeWinH.contentItem());
                pc.grab(); // m_captured（beginMining 入口门，t949 同式）
                hb.setStack(0, diaSword, 1, -1, enchanted ? smiteL : QVariantList{0, 0, 0, 0});
                hb.setSelectedSlot(0);
                pc.setWorld(&wh);
                pc.setEntityManager(&emh);
                pc.setHotbar(&hb);
                const int mob = emh.spawnMobTyped(x, 85, z, mobType, QStringLiteral("#4a6a3a"), 20);
                if (mob < 0) return -1;
                const QVector3D eye(float(x) + 0.5f, 86.62f, float(z) + 3.0f);
                const QVector3D dir = (QVector3D(float(x) + 0.5f, 85.5f, float(z) + 0.5f) - eye).normalized();
                pc.loadSavedState(eye.x(), 85.0f, eye.z(),
                                  qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                                  qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
                pc.beginMining(); // findMobHit 命中 → attackMob（t476 附魔伤链）
                return 20 - emh.healthAt(mob);
            };
            const int babyDmg = hitOnceC(20, 20, EntityManager::MobBabyShambler, true);
            const int adultDmg = hitOnceC(30, 30, EntityManager::MobShambler, true);
            const int spiderDmg = hitOnceC(10, 30, EntityManager::MobSpider, true);
            const int plainDmg = hitOnceC(10, 10, EntityManager::MobBabyShambler, false);
            // 显示==实战对拍：实战伤 = 基伤 7 + 显示面 (+M)；幼体 == 成体（族门同式）；族门不外泄蜘蛛；
            //   无附魔 = 裸基伤（族门是使能方，非恒加成）。
            okH = famText == QStringLiteral("(+8)") && famM == 8
               && babyDmg == 7 + famM && adultDmg == babyDmg
               && spiderDmg == 7 && plainDmg == 7;
            if (!okH) diag += QStringLiteral("h fam=%1 M=%2 baby=%3 adult=%4 spider=%5 plain=%6 ")
                                  .arg(famText).arg(famM).arg(babyDmg).arg(adultDmg)
                                  .arg(spiderDmg).arg(plainDmg);
            // 单一权威钉：独立谓词（亡灵族）在位 + attackMob 消费 + 旧裸清单绝迹 + 语义分立登记在案
            //   （亡灵族门不含头盔免烧豁免——与 undeadBurnsInDaylight 的差）。
            okH = okH && entCppC.contains(QStringLiteral("bool EntityManager::isUndeadFamily(int mobType)"))
               && entHC.contains(QStringLiteral("static bool isUndeadFamily(int mobType);"))
               && entHC.contains(QStringLiteral("头盔免烧豁免不进本门"))
               && pcCppC.contains(QStringLiteral("const bool undead = EntityManager::isUndeadFamily(mobType);"))
               && !pcCppC.contains(QStringLiteral(
                   "mobType == int(EntityManager::MobShambler) || mobType == int(EntityManager::MobBones));"));
        }

        const bool okR0830C = okA && okB && okC && okD && okE && okF && okG && okH;
        if (!okR0830C) ++totalFail;
        if (!okR0830C)
            qInfo().noquote() << "  [r0830C diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE << "f" << okF << "g" << okG << "h" << okH << "|" << diag;
        qInfo().noquote() << (okR0830C ? "PASS" : "FAIL")
                          << "| review0830 batch C (mobs #5 #6 #13 #14 #15 #16 #17 #18 #19 #26):"
                             " (a) the tamed OCELOT mirrors the wolf's spectator gate - a standing"
                             " cat holds off (>=4.0) and skips the far-teleport (>=10) while its"
                             " owner spectates where following used to close/teleport, the"
                             " creative/survival control still closes to the stop band (<=3.0),"
                             " and the wolf/cat gates share one literal gate form (sync pin,"
                             " count >=2) with the dispatch/signature pass-through pinned; (b)"
                             " the helmet burn exemption now feeds the shade machine too: a"
                             " BARE sun-lit shambler still retreats into the overhang (control),"
                             " a HELMETED one charges straight to the sun-lit player and bites"
                             " (no shade-seek, no attack suppression) instead of parking in the"
                             " shade, and a helmeted bones archer walks OUT of the overhang once"
                             " the sunlit-candidate gate switches off with dayShadeAi, while the"
                             " burn-site exemption line stays pinned (still never burns); (c) a"
                             " wolf bite on a BABY shambler registers revenge - the baby turns and"
                             " melees the wolf (wolf hp drop, closed up) where the old enum gate"
                             " silently no-oped; (d) a TAMED wolf defending a frozen pig target"
                             " at bite range (1.0 <= 1.6, t1031 rig migration: the untamed"
                             " player-bite flow was retired by the wild-wolf-neutral caliber so"
                             " the same chase lambda is now exercised through the defense branch)"
                             " with a 1-high wall 0.6 ahead keeps biting (>=3 health drops) without"
                             " ever leaving the ground (lift <0.15; the transient hop used to lift"
                             " ~1.0); (e) a cart-riding shambler standing on an iron chestplate"
                             " never picks it across six windows while a bare ground control in"
                             " the same scan does (riding excluded, resolvePlayerPush caliber),"
                             " with the pickup-chance default pinned to the kEquipPickupChance"
                             " constant; (f) after the jockey pair engages the wall with the"
                             " player east -- the rider's gate cycles eastward jump intent at the"
                             " face and since t1008 the mount-hop takes the pair over the top --"
                             " and the player flips to the west side (the evolved window lets the"
                             " pair re-cross westward), killing the chicken drops the baby with"
                             " ZERO eastward per-tick drift frames (the old stale-glide streamer"
                             " drifted ~1 block east mid-fall; the pin-segment discard of the"
                             " rider's vy/glide is the regression core and stays source-pinned);"
                             " (g) the wander-state"
                             " burns-in-place trade-off is comment-registered at both AI early-"
                             " exits plus the header caliber; (h) an UndeadSlay-III diamond sword"
                             " deals 15 to the BABY shambler == 15 to the adult == base 7 + the"
                             " displayed (+8) (t961 show==combat pairing), 7 vs the spider (family"
                             " gate does not leak) and 7 unenchanted (the gate enables, not adds),"
                             " backed by the isUndeadFamily single authority (deliberately split"
                             " from undeadBurnsInDaylight: the family gate carries no helmet"
                             " exemption) with the bare t476 list extinct"
                             ;
    }

    // ── t954 书本合拢动画重做源码钉（用户第五轮口径「一瞬间+只有左边合并——应左页向右、右页向左
    //    对向合拢成有厚度的关闭书籍」；纯视觉项，动画时序/观感 headless 不可达——3D 呈现层需真窗口，
    //    t931/t941/t953 源码钉先例）──
    //    旧病灶：左右页 pageAngle 绑 bookOpen + Behavior 240ms（读作「一瞬间」）且合拢面只有左页
    //    -22°→-178° 大摆（右页仅 21° 微动读作不动）→ 合拢后两薄盒叠平零厚度。修 = 命令式双页对向
    //    ParallelAnimation（850ms，review28 #9 pageFlipAnim 同款 running:false + restart() 先例）+
    //    合拢厚度层（封面抬升 stackLift / 基页下沉内移 gather / 右页叠层 rightPageBlock / 书脊增高
    //    closeAmt）+ ESC 硬档落定（delegate 实例内 Connections——window 作用域不含 inline Component
    //    内 id，兼修 review28 #9 的运行期断链；window 级旧语句面加 typeof 守卫保持 P-review28b 契约）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile qf954(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m954 = qf954.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf954.readAll()) : QString();
        // (a) 对向双页动画存在 + 时长门：两动画各恰 5 条 850ms 驱动；合拢动画含左页 -178 与右页 +1
        //     两腿（对向 = 双页都有角度驱动，回退单页即红）；翻开动画含 -22/+22 回程；起摆入口在
        //     onBookOpenChanged（命令式 restart）。
        const int iOpen954 = m954.indexOf(QStringLiteral("id: bookOpenAnim"));
        const int iClose954 = m954.indexOf(QStringLiteral("id: bookCloseAnim"));
        const int iTrigger954 = m954.indexOf(QStringLiteral("onBookOpenChanged:"));
        const QString openSlice954 = (iOpen954 >= 0 && iClose954 > iOpen954)
                                         ? m954.mid(iOpen954, iClose954 - iOpen954) : QString();
        const QString closeSlice954 = (iClose954 >= 0 && iTrigger954 > iClose954)
                                          ? m954.mid(iClose954, iTrigger954 - iClose954) : QString();
        const bool okA954 = iOpen954 >= 0 && iClose954 > iOpen954 && iTrigger954 > iClose954
                         && openSlice954.count(QStringLiteral("duration: 850")) == 5
                         && closeSlice954.count(QStringLiteral("duration: 850")) == 5
                         && closeSlice954.contains(QStringLiteral("target: leftPageNode; property: \"pageAngle\"; to: -178"))
                         && closeSlice954.contains(QStringLiteral("target: rightPageNode; property: \"pageAngle\"; to: 1"))
                         && openSlice954.contains(QStringLiteral("target: leftPageNode; property: \"pageAngle\"; to: -22"))
                         && openSlice954.contains(QStringLiteral("target: rightPageNode; property: \"pageAngle\"; to: 22"))
                         // review0830 #21：起摆前先停对向动画（迟滞带内快速往返 → 双动画同写 5 属性，
                         // 旧形态只 restart 方向匹配动画 = 依赖「注册序后者后写获胜」的未文档化行为）。
                         && m954.contains(QStringLiteral("if (bookOpen) { bookCloseAnim.stop(); bookOpenAnim.restart() }"))
                         && m954.contains(QStringLiteral("else { bookOpenAnim.stop(); bookCloseAnim.restart() }"));
        // (b) 合拢厚度层存在：右页叠层薄片（id + 内缩尺寸）+ 封面抬升 / 基页下沉内移 / 书脊增高
        //     三系数绑定（合拢态总厚 ~0.06 ≈ 单页 2.7×，「封面+书脊厚度感」的几何面）。
        const bool okB954 = m954.contains(QStringLiteral("id: rightPageBlock"))
                         && m954.contains(QStringLiteral("scale: Qt.vector3d(0.365, 0.014, 0.44)"))
                         && m954.contains(QStringLiteral("property real stackLift: 1.0"))
                         && m954.contains(QStringLiteral("position: Qt.vector3d(0, 0.026 * stackLift, 0)"))
                         && m954.contains(QStringLiteral("property real gather: 1.0"))
                         // review0830 #1：节点 position 改纯向脊平移（-0.025·gather，无 +0.19 前导项）
                         //   ——子 Model 的 0.19 页偏移只算一次，枢轴留在书脊。旧出错形态
                         //   `0.19 - 0.025 * gather` 曾被本钉钉成正向钉（给 bug 背书），已随本修替换。
                         && m954.contains(QStringLiteral("position: Qt.vector3d(-0.025 * gather, -0.006 * gather, 0.0)"))
                         && m954.contains(QStringLiteral("property real closeAmt: 1.0"))
                         && m954.contains(QStringLiteral("scale: Qt.vector3d(0.032, 0.03 + 0.045 * closeAmt, 0.46)"))
                         && m954.contains(QStringLiteral("position: Qt.vector3d(0.0, -0.02 + 0.0225 * closeAmt, 0.0)"));
        // (c) ESC 硬档落定面：snapBookPose 全驱动落定函数 + delegate 实例内 Connections 停摆 + 复位；
        //     window 级旧语句面 typeof 守卫（review28b 语句面契约 + t954 运行期勘误双钉）。
        const bool okC954 = m954.contains(QStringLiteral("function snapBookPose()"))
                         && m954.contains(QStringLiteral("leftPageNode.pageAngle = bookOpen ? -22 : -178"))
                         && m954.contains(QStringLiteral("spineBar.closeAmt = bookOpen ? 0.0 : 1.0"))
                         && m954.contains(QStringLiteral("bookOpenAnim.stop()"))
                         && m954.contains(QStringLiteral("bookCloseAnim.stop()"))
                         && m954.contains(QStringLiteral("bookRoot.snapBookPose()"))
                         && m954.contains(QStringLiteral("typeof pageFlipAnim !== \"undefined\""));
        const bool ok954 = okA954 && okB954 && okC954;
        if (!ok954)
            qInfo().noquote() << "  t954 diag: dualAnim" << okA954 << "thickness" << okB954
                              << "escReset" << okC954;
        if (!ok954) ++totalFail;
        qInfo().noquote() << (ok954 ? "PASS" : "FAIL")
                          << "| t954 book closing rework source pin: the close transition is now a "
                             "command-driven parallel animation with BOTH pages converging toward "
                             "the spine in one 850ms ease-out beat (the left page's outer edge "
                             "sweeps right over the hinge to -178 while the right page flattens to "
                             "+1 and gathers 0.025 toward the spine - the old bound-property 240ms "
                             "Behavior read as instant and visibly moved only the left half), the "
                             "closed form gains thickness (cover lifted 0.026 arching over the page "
                             "stack, base page dropped and gathered inward, an inset paper block "
                             "layer riding the right page, spine bar growing 0.03->0.075 as the "
                             "bound edge), and the ESC hard-pause settle covers the transitions "
                             "through the delegate-instance Connections (snapBookPose writes every "
                             "driven property to the bookOpen-consistent rest pose) with the "
                             "window-level handler keeping the review28b statement surface behind "
                             "a typeof guard because inline-Component ids never resolve at window "
                             "scope";
    }

    // ── P-review0830-1 书本右页几何不变量运行期断言（Review_2026-08-30 #1 高危；审查 §六-1 结构性
    //    建议「几何/变换类改动补运行期断言」的落点）──
    //    病灶：t954 给 rightPageNode 新增节点级 position（`0.19 - 0.025 * gather`）却没清子 Model
    //    原 0.19 页偏移 → 页偏移被数两遍（变换序 = T(节点 pos)·R·T(Model pos)）：敞开态右页内缘
    //    x≈0.19 脱离书脊半个书宽、合拢态右页外缘 ≈0.545 甩出左封面（≈0.38）并排不叠合、flutter
    //    静息页片悬在裂口。旧 P-t954 全源码钉且把出错行钉成正向钉 = 几何类改动的系统性盲区。
    //    修 = 节点 position 改纯向脊平移（-0.025·gather），子 Model 0.19 不动，枢轴留书脊。
    //    本探针 = 从**真 Main.qml 源**解析节点链常量（position 表达式按「a ± b·变量」仿射解析，
    //    非逐字钉——改系数自动重算，改不出仿射形态即红 = 结构不可验证也算红）+ C++ 数学复算
    //    T(节点 pos)·R(角)·T(Model pos) 变换链取页盒内缘/外缘点（审查建议的可达落点；Quick3D
    //    序 = T·R·S 契约、Z 轴单轴旋转在 xy 平面内，t764 抬升账同款手算先例），断言五条数值不变量：
    //    (1) 敞开态（gather=0）右页内缘世界 x ≈ 0（贴书脊；出病灶 = 0.19）；
    //    (2) 合拢态（gather=1）右页外缘 x ≤ 左封面外缘 x + 容差（叠合非并排；出病灶 0.545 > 0.38）；
    //    (3) 合拢态右页内缘 x ≤ 左封面内缘 x + 容差（右页压过书脊出基座；出病灶 0.165 > 0）；
    //    (4) 敞开态 flutter 静息页片心（R(baseAngle)·(0.19, 0.004)）变换进右页盒体系仍在盒内
    //        （|x|≤半宽 0.19 且 |y|≤半厚 0.011 = 嵌入非悬空；出病灶 y≈0.075 > 0.011）；
    //    (5) 左页节点 position 无 x 分量（左右枢轴对称性——左页单偏移/右页双偏移即错位实锤）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile qfR1(root + QStringLiteral("/src/ui/Main.qml"));
        const QString mR1 = qfR1.open(QIODevice::ReadOnly) ? QString::fromUtf8(qfR1.readAll()) : QString();
        // 仿射解析：分量表达式「[a][±b·var]」→ (a, b)；eval(v) = a + b·v。不支持形态（括号/函数）
        //   一律解析失败 → 探针红（形态变了=不可验证，宁可红不可静默绿）。
        auto parseAffineR1 = [](const QString &expr, bool *ok) -> QPair<double, double> {
            *ok = false;
            QString e = expr; e.remove(' ');
            double a = 0.0, b = 0.0;
            const int gi = e.indexOf(QStringLiteral("*gather"));
            if (gi >= 0) {
                int s = gi - 1;
                while (s >= 0 && (e.at(s).isDigit() || e.at(s) == QLatin1Char('.'))) --s;
                const int start = (s >= 0 && (e.at(s) == QLatin1Char('-') || e.at(s) == QLatin1Char('+'))) ? s : s + 1;
                bool okc = false;
                b = e.mid(start, gi - start).toDouble(&okc); // 含符号位
                if (!okc) return { a, b };
                e = e.left(start);
                while (e.endsWith(QLatin1Char('+')) || e.endsWith(QLatin1Char('-'))) e.chop(1);
            }
            if (!e.isEmpty()) {
                bool oka = false;
                a = e.toDouble(&oka);
                if (!oka) return { a, b };
            }
            *ok = true;
            return { a, b };
        };
        // 仿射（变量名泛化版）：「[a][±b·var]」。
        auto parseAffineVarR1 = [&parseAffineR1](const QString &expr, const QString &var, bool *ok) -> QPair<double, double> {
            if (var == QStringLiteral("gather")) return parseAffineR1(expr, ok);
            *ok = false;
            QString e = expr; e.remove(' ');
            QString g = e; g.replace(var, QStringLiteral("gather"));
            return parseAffineR1(g, ok);
        };
        // 从 Qt.vector3d(...) 捕获三分量。
        auto vecArgsR1 = [](const QString &src, int from, bool *ok) -> QStringList {
            *ok = false;
            const int i = src.indexOf(QStringLiteral("Qt.vector3d("), from);
            if (i < 0) return {};
            const int j = src.indexOf(QLatin1Char(')'), i);
            if (j < 0) return {};
            const QStringList parts = src.mid(i + 12, j - i - 12).split(QLatin1Char(','));
            if (parts.size() != 3) return {};
            *ok = true;
            return parts;
        };
        bool parseOk = mR1.contains(QStringLiteral("id: rightPageNode"))
                    && mR1.contains(QStringLiteral("id: leftPageNode"))
                    && mR1.contains(QStringLiteral("id: flipPivot"));
        // 右页链：节点 position（仿射·gather）+ Model 局部 position/scale（页盒 ±0.5 居中 → 半宽 = scaleX/2）。
        double rNx0 = 0, rNx1 = 0, rNy0 = 0, rNy1 = 0, rMx = 0, rHalfW = 0, rHalfT = 0;
        // 左封面链：节点 position（仿射·stackLift；x 恒 0 = 对称性腿）+ Model 局部 position/scale。
        double lNx0 = 0, lNx1 = 0, lNy0 = 0, lNy1 = 0, lMx = 0, lHalfW = 0;
        // flutter 静息页片：baseAngle + Model 局部 position。
        double fBase = 0, fMx = 0, fMy = 0;
        // 角度（动画目标值即两静息位）：open 右/左、closed 右/左。
        double angROpen = 0, angRClose = 0, angLClose = 0;
        if (parseOk) {
            const int iR = mR1.indexOf(QStringLiteral("id: rightPageNode"));
            const int iL = mR1.indexOf(QStringLiteral("id: leftPageNode"));
            const int iS = mR1.indexOf(QStringLiteral("id: spineBar"));
            const QString rs = (iR >= 0 && iS > iR) ? mR1.mid(iR, iS - iR) : QString();
            const QString ls = (iL >= 0 && iR > iL) ? mR1.mid(iL, iR - iL) : QString();
            bool ok1, ok2, ok3, ok4;
            const QStringList npos = vecArgsR1(rs, 0, &ok1);           // 节点 position = 第一个 vector3d
            const int iGeo = rs.indexOf(QStringLiteral("geometry:"));
            const QStringList mpos = vecArgsR1(rs, iGeo, &ok2);        // Model 局部 position = geometry 后第一个
            const int isc = rs.indexOf(QStringLiteral("scale: Qt.vector3d("), iGeo);
            const QStringList mscl = vecArgsR1(rs, isc >= 0 ? isc : rs.size() - 1, &ok3);
            auto ap1 = parseAffineR1(npos.value(0), &ok4);
            auto ap2 = parseAffineR1(npos.value(1), &ok1);
            parseOk = parseOk && ok1 && ok2 && ok3 && ok4 && rs.size() > 0;
            rNx0 = ap1.first; rNx1 = ap1.first + ap1.second;
            rNy0 = ap2.first; rNy1 = ap2.first + ap2.second;
            rMx = mpos.value(0).toDouble();
            rHalfW = mscl.value(0).toDouble() * 0.5;
            rHalfT = mscl.value(1).toDouble() * 0.5;
            const QStringList lnpos = vecArgsR1(ls, 0, &ok2);
            const int liGeo = ls.indexOf(QStringLiteral("geometry:"));
            const QStringList lmpos = vecArgsR1(ls, liGeo, &ok3);
            const int lisc = ls.indexOf(QStringLiteral("scale: Qt.vector3d("), liGeo);
            const QStringList lmscl = vecArgsR1(ls, lisc >= 0 ? lisc : ls.size() - 1, &ok4);
            auto bp1 = parseAffineVarR1(lnpos.value(0), QStringLiteral("stackLift"), &ok1);
            auto bp2 = parseAffineVarR1(lnpos.value(1), QStringLiteral("stackLift"), &ok2);
            parseOk = parseOk && ok1 && ok2 && ok3 && ok4 && ls.size() > 0;
            lNx0 = bp1.first; lNx1 = bp1.first + bp1.second;
            lNy0 = bp2.first; lNy1 = bp2.first + bp2.second;
            lMx = lmpos.value(0).toDouble();
            lHalfW = lmscl.value(0).toDouble() * 0.5;
            const int iF = mR1.indexOf(QStringLiteral("id: flipPivot"));
            const QString fs = iF >= 0 ? mR1.mid(iF, 900) : QString();
            const int fBaseIdx = fs.indexOf(QStringLiteral("property real baseAngle:"));
            const int fiGeo = fs.indexOf(QStringLiteral("geometry:"));
            const QStringList fmpos = vecArgsR1(fs, fiGeo, &ok1);
            parseOk = parseOk && ok1 && fBaseIdx >= 0 && fiGeo >= 0;
            if (parseOk) {
                fBase = fs.mid(fBaseIdx, 40).split(QLatin1Char(':')).value(1).simplified().split(QLatin1Char(' ')).value(0).toDouble();
                fMx = fmpos.value(0).toDouble();
                fMy = fmpos.value(1).toDouble();
            }
            // 角度静息位取自动画目标（okA954 已钉其形态，此处提数）。
            const int iO = mR1.indexOf(QStringLiteral("id: bookOpenAnim"));
            const int iC = mR1.indexOf(QStringLiteral("id: bookCloseAnim"));
            const int iT = mR1.indexOf(QStringLiteral("onBookOpenChanged:"));
            if (iO >= 0 && iC > iO && iT > iC) {
                const QString os = mR1.mid(iO, iC - iO);
                const QString cs = mR1.mid(iC, iT - iC);
                const QRegularExpression reR(QStringLiteral("target: rightPageNode; property: \"pageAngle\"; to: (-?[\\d.]+)"));
                const QRegularExpression reL(QStringLiteral("target: leftPageNode; property: \"pageAngle\"; to: (-?[\\d.]+)"));
                const QRegularExpressionMatch mo = reR.match(os);
                const QRegularExpressionMatch mc = reR.match(cs);
                const QRegularExpressionMatch ml = reL.match(cs);
                parseOk = parseOk && mo.hasMatch() && mc.hasMatch() && ml.hasMatch();
                if (parseOk) {
                    angROpen = mo.captured(1).toDouble();
                    angRClose = mc.captured(1).toDouble();
                    angLClose = ml.captured(1).toDouble();
                }
            } else {
                parseOk = false;
            }
        }
        auto degCos = [](double d) { return std::cos(d * 3.14159265358979323846 / 180.0); };
        auto degSin = [](double d) { return std::sin(d * 3.14159265358979323846 / 180.0); };
        // 页盒 x 区间：T(节点 pos)·R(角)·T(Model pos) 后 ±半宽·cos(角)（min/max 兼容 ±178° 的负 cos）。
        auto spanR1 = [](double nx, double ny, double mx, double halfW, double ang, double *innerX, double *outerX, double *cy) {
            const double c = std::cos(ang * 3.14159265358979323846 / 180.0);
            const double cx = nx + mx * c;
            *cy = ny + mx * std::sin(ang * 3.14159265358979323846 / 180.0);
            const double e1 = (mx - halfW) * c, e2 = (mx + halfW) * c;
            *innerX = std::min(e1, e2) + nx;
            *outerX = std::max(e1, e2) + nx;
        };
        bool okGeo = parseOk;
        double dInnerOpen = 0, dOutR = 0, dOutL = 0, dInR = 0, dInL = 0, dFlipY = 0;
        if (okGeo) {
            // (1) 敞开态右页内缘 x ≈ 0（贴书脊）。
            double cyOpen = 0, inOpen = 0, outOpen = 0;
            spanR1(rNx0, rNy0, rMx, rHalfW, angROpen, &inOpen, &outOpen, &cyOpen);
            dInnerOpen = inOpen;
            okGeo = okGeo && std::fabs(inOpen) <= 0.005;
            // (2)(3) 合拢态：右页外缘不甩出左封面外缘（叠合）+ 右页内缘压过左封面内缘（出基座）。
            double inR = 0, outR = 0, inL = 0, outL = 0, cyL = 0;
            spanR1(rNx1, rNy1, rMx, rHalfW, angRClose, &inR, &outR, &cyOpen);
            spanR1(lNx1, lNy1, lMx, lHalfW, angLClose, &inL, &outL, &cyL);
            dOutR = outR; dOutL = outL; dInR = inR; dInL = inL;
            okGeo = okGeo && outR <= outL + 0.01 && inR <= inL + 0.01;
            // (4) flutter 静息嵌入：flipPivot 是 rightPageNode 的**兄弟**（同挂 bobNode，无自己的
            //     position，不吃右页节点位移）→ 页片心 = R(baseAngle)·T(局部 pos)（bobNode 系），
            //     右页盒心 = T(节点 pos)·R(angle)·T(局部 pos)；Δ 回页盒系须在盒内（出病灶 y≈0.075）。
            const double fcx = fMx * degCos(fBase) - fMy * degSin(fBase);
            const double fcy = fMx * degSin(fBase) + fMy * degCos(fBase);
            const double dx = fcx - (rNx0 + rMx * degCos(angROpen));
            const double dy = fcy - (rNy0 + rMx * degSin(angROpen));
            const double lx = dx * degCos(angROpen) + dy * degSin(angROpen);
            const double ly = -dx * degSin(angROpen) + dy * degCos(angROpen);
            dFlipY = ly;
            okGeo = okGeo && std::fabs(lx) <= rHalfW + 1e-6 && std::fabs(ly) <= rHalfT + 1e-6;
            // (5) 左页节点无 x 分量（枢轴对称）。
            okGeo = okGeo && std::fabs(lNx0) <= 1e-9 && std::fabs(lNx1) <= 1e-9;
        }
        const bool okR1 = okGeo;
        if (!okR1)
            qInfo().noquote() << "  review0830-1 diag: parse" << parseOk << "openInnerX" << dInnerOpen
                              << "closedOuterR" << dOutR << "vsL" << dOutL
                              << "closedInnerR" << dInR << "vsL" << dInL
                              << "flipLocalY" << dFlipY << "halfT" << rHalfT;
        if (!okR1) ++totalFail;
        qInfo().noquote() << (okR1 ? "PASS" : "FAIL")
                          << "| review0830-1 book right-page geometry invariants (Review_2026-08-30 #1 "
                             "high): t954 added a node-level position on rightPageNode while the child "
                             "Model kept its original 0.19 page offset, so the offset was counted TWICE "
                             "(transform order T(node pos)*R*T(model pos)) - the open-state inner edge "
                             "sat 0.19 off the spine (half a page width), the closed-state right page "
                             "slid out to ~0.545 beside the left cover (~0.38) instead of stacking, and "
                             "the flutter page hung in the gap; the old P-t954 probe pinned the very "
                             "buggy line as a positive pin, which is exactly the geometry-class blind "
                             "spot review section six flags; the fix makes the node position a pure "
                             "toward-spine translation (-0.025*gather) leaving the child 0.19 intact "
                             "and the pivot on the spine; this probe parses the REAL Main.qml node "
                             "chain constants (affine parse of the position expressions, not a verbatim "
                             "pin - coefficient changes re-evaluate, unparseable shapes go red) and "
                             "recomputes the T*R*T chain in C++ asserting five numeric invariants: "
                             "open-state right-page inner edge ~ 0 on the spine, closed-state right "
                             "outer edge within the left cover's outer edge (stacked not side-by-side), "
                             "closed-state right inner edge crossing the spine onto the cover (the "
                             "thickness base), the flutter rest flip center transformed into the right "
                             "page box frame still inside the box (embedded, not floating), and the "
                             "left page node carrying no x component (pivot symmetry)";
    }

    // ── t955 hover 预告格式改源码钉（用户第五轮口径「去掉『必得』字样——直接『效率......?』」；
    //    纯 UI 文案项，hover 悬浮文案 headless 不可达——t931/t941/t954 源码钉先例）──
    //    旧格式「必得 锐锋 ?」= 前置铺垫词 + 名 + 空格问号；新格式 = 附魔名 + 「......?」后缀
    //    （省略号 = 产物词条列表未揭、? = 等级未知；t917「预告与施放严格同源」语义零变化——同一条
    //    tierPreviewName 首条名，只换呈现格式）。钉三面：
    //    ① 新格式源码形态 —— previewText 的 text 行含附魔名拼接与「......?」后缀（旧式前缀拼接 +
    //       「 ?」尾巴形态全无）；
    //    ② 「必得」字样全文件不存在（含注释——旧呈现格式的指纹词，注释残留 = 旧口径漂回的温床；
    //       格式语义已由名本身承载，「必出」承诺不靠前置词）；
    //    ③ 预告管线正锚 —— tierPreviewName 定义与调用仍在（格式改不碰 t917 同源链；预览文本仍
    //       唯一出自它，防「格式改」顺手把预览源改掉）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile ef955(root + QStringLiteral("/src/ui/EnchantingTableUI.qml"));
        const QString e955 = ef955.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef955.readAll()) : QString();
        // (a) 新格式形态：名拼接 + 「......?」后缀；旧式「'必得 ' + 名 + ' ?'」形态不复存在。
        const bool okA955 = e955.contains(QStringLiteral("text: optSlot.previewName + \"......?\""))
                         && !e955.contains(QStringLiteral("\" ?\""))
                         && !e955.contains(QStringLiteral("+ optSlot.previewName + \" "));
        // (b) 「必得」全文件不存在（含注释；utf-8 字面按码点比对——源码即 utf-8 读入）。
        const bool okB955 = !e955.contains(QStringLiteral("必得"));
        // (c) 预告管线正锚：tierPreviewName 仍是预览文本的唯一来源（t917 同源链零触碰）。
        const bool okC955 = e955.contains(QStringLiteral("function tierPreviewName(slotIdx)"))
                         && e955.contains(QStringLiteral("root.tierPreviewName(optSlot.idx)"));
        const bool ok955 = okA955 && okB955 && okC955;
        if (!ok955)
            qInfo().noquote() << "  t955 diag: newFormat" << okA955 << "prefixGone" << okB955
                              << "pipeline" << okC955;
        if (!ok955) ++totalFail;
        qInfo().noquote() << (ok955 ? "PASS" : "FAIL")
                          << "| t955 enchant hover preview format: the guaranteed-prefix wording is "
                             "gone - the tooltip renders the enchant name straight into the "
                             "'......?' suffix (the name itself carries the guaranteed-appearance "
                             "promise, the ellipsis = the rest of the result list unrevealed, "
                             "? = level masked; the MC 1.0 one-enchant-level-blurred caliber is "
                             "unchanged and the t917 preview==cast single-source chain is "
                             "untouched), the old 'prefix + name + space-question' source form is "
                             "extinct anywhere in the file including comments, and "
                             "tierPreviewName remains the sole text source (pipeline positive "
                             "anchor)";
    }

    // ── t956 附魔台 / 铁砧创造中键复制补口（源码钉 ×2 + 真 QML×真 C++ Hotbar 行为腿；R19.17）──
    //    用户第五轮实测：「附魔台/铁砧 UI 创造中键复制返修（t896 链在这两个 UI 不管用了）」。根因：
    //    t653①/t896 的中键复制只落在 Inventory 面板（调色板 / 护甲 / craft / main / hotbar 五面），
    //    EnchantingTableUI / AnvilUI 的槽交互面只有左/右两个 TapHandler —— acceptedButtons 漏中键，
    //    中键分支整个缺席（不是坏了，是从来没接过）。钉四面：
    //    ① 两面板各持面板级 copyStackToCursor（口径逐字对齐 Inventory.qml 单一权威：maxStackSize(id)
    //       整组数量 + list4 序列归一（t874）+ 实例元数据保真；旧 min(count,…) 翻倍形态必须绝迹）；
    //    ② 两面板各恰 3 个中键 TapHandler（附魔台 = EnchantInputSlot 组件（覆盖 0/1 两实例）+ main 行 +
    //       hotbar 行；铁砧 = AnvilSlot 组件（覆盖左/右输入两实例）+ main 行 + hotbar 行），且每分支
    //       400 字符内同现 enabled: root.creativeMode 创造门（t288 中键 pick 仅创造）与复制调用；
    //    ③ 铁砧产物预览槽排除钉逐字（enabled: root.creativeMode && !aslot.preview）—— 预览 slotId 是
    //       投射产物非实有物品，中键复制 = 绕过 takeProduct 消耗凭空量产（同 Inventory.qml 合成
    //       结果槽无中键的先例）；
    //    ④ 行为腿（t874 真链 harness 先例；真中键事件投递在该 harness 已证不可达 —— 合成 QMouseEvent
    //       走 TapHandler 有不可消除的拖动伪影，故按其「函数链直调」定案）：源树 AnvilUI.qml 经临时
    //       目录直载 + 真 Hotbar VM，直调 copyStackToCursor —— 断言满栈复制（源槽 5 件方块 → 光标 64
    //       整组，非 min(5,64)=5）、工具整组=1、实例元数据（耐久 / 附魔 / 名）保真、旧光标手持被覆盖、
    //       空槽 no-op。（中键事件路由面与 enabled 门由 ①②③ 源码钉承载 —— QML 事件路由 C++ 矩阵全盲，
    //       同 t918 口径。）
    //    ⑤ review0830 #22 拾取反馈钉：两面板声明 signal itemTaken() 且成功复制尾部发 root.itemTaken()
    //       （no-op 早退不发）；Main.qml 三面板（Inventory + 附魔台 + 铁砧）同一消费端
    //       onItemTaken → handPopAnim.start（中键获得与拾取一致的手弹视觉反馈）；行为腿直连信号计数：
    //       两次成功复制恰 2 发、空槽 no-op 0 发（发点在守卫之后的实锤）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile ef956(root + QStringLiteral("/src/ui/EnchantingTableUI.qml"));
        const QString e956 = ef956.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef956.readAll()) : QString();
        QFile af956(root + QStringLiteral("/src/ui/AnvilUI.qml"));
        const QString a956 = af956.open(QIODevice::ReadOnly) ? QString::fromUtf8(af956.readAll()) : QString();
        // ① 面板级复制函数：数量权威 + 序列归一 + 元数据保真三要素在函数体内；旧 min(count,…) 绝迹；
        //    review0830 #22：函数尾部发 root.itemTaken()（反馈面与 Inventory.qml 同口径）。
        auto copyBodyOk = [](const QString &s) {
            const int iFn = s.indexOf(QStringLiteral("function copyStackToCursor"));
            if (iFn < 0) return false;
            const QString fn = s.mid(iFn, 700);
            return fn.contains(QStringLiteral("heldCount = root.hotbar.maxStackSize(id)"))
                    && fn.contains(QStringLiteral("InventoryOps.list4(enchants)"))
                    && fn.contains(QStringLiteral("heldDurability = (durability > 0) ? durability : 0"))
                    && fn.contains(QStringLiteral("root.itemTaken()"))
                    && !s.contains(QStringLiteral("Math.min(count"));
        };
        // ⑤ 信号面钉：signal 声明 + 宿主消费端三面板同一（Inventory / EnchantingTableUI / AnvilUI）。
        auto takenSignalOk = [](const QString &s) {
            return s.contains(QStringLiteral("signal itemTaken()"))
                    && s.count(QStringLiteral("root.itemTaken()")) >= 1;
        };
        QFile mf956(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m956 = mf956.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf956.readAll()) : QString();
        const bool okTakenE = takenSignalOk(e956);
        const bool okTakenA = takenSignalOk(a956);
        const bool okTakenMain = m956.count(QStringLiteral("onItemTaken: handPopAnim.start()")) == 3;
        // ② 逐分支钉：每个中键 TapHandler 的邻近段同现创造门与复制调用（缺门 / 缺调用的散写分支即红）。
        auto midHandlersOk = [](const QString &s) {
            int pos = -1, n = 0;
            while ((pos = s.indexOf(QStringLiteral("acceptedButtons: Qt.MiddleButton"), pos + 1)) >= 0) {
                ++n;
                const QString seg = s.mid(pos, 400);
                if (!seg.contains(QStringLiteral("enabled: root.creativeMode"))
                        || !seg.contains(QStringLiteral("root.copyStackToCursor(")))
                    return false;
            }
            return n == 3;
        };
        const bool okDefE = copyBodyOk(e956);
        const bool okDefA = copyBodyOk(a956);
        const bool okMidE = midHandlersOk(e956);
        const bool okMidA = midHandlersOk(a956);
        // ③ 铁砧预览排除（逐字；QML 事件面无 static_assert 可用，源码即契约）。
        const bool okPreviewA = a956.contains(QStringLiteral("enabled: root.creativeMode && !aslot.preview"));

        // ④ 行为腿：最小真链 harness（同 t874 装配法：临时目录逃离 qrc 重映射 + 私有 URI + wrapper 作用域）。
        static bool sT956TypesRegistered = false;
        if (!sT956TypesRegistered) {
            qmlRegisterType<Hotbar>("VoxelSandboxProbeT956", 1, 0, "Hotbar");
            qmlRegisterType<PlayerState>("VoxelSandboxProbeT956", 1, 0, "PlayerState");
            qmlRegisterType<PlayerController>("VoxelSandboxProbeT956", 1, 0, "PlayerController");
            qmlRegisterType<ResourcePackManager>("VoxelSandboxProbeT956", 1, 0, "ResourcePackManager");
            sT956TypesRegistered = true;
        }
        bool behavOk = false;
        QString behavDiag;
        const QString uiDir956 = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath())
                                     .filePath(QStringLiteral("../../src/ui"));
        const QString probeUiDir = QDir::temp().absoluteFilePath(
                QStringLiteral("t956_qml_%1").arg(QCoreApplication::applicationPid()));
        QDir().mkpath(probeUiDir);
        for (const QString f : { QStringLiteral("AnvilUI.qml"), QStringLiteral("InventoryOps.js"),
                                 QStringLiteral("InvSlot.qml"), QStringLiteral("ToolIcon.qml"),
                                 QStringLiteral("MaterialIcon.qml") }) {
            QFile::remove(probeUiDir + QLatin1Char('/') + f);
            QFile(uiDir956 + QLatin1Char('/') + f).copy(probeUiDir + QLatin1Char('/') + f);
        }
        {
            const QUrl jsUrl = QUrl::fromLocalFile(probeUiDir + QLatin1Char('/') + QStringLiteral("InventoryOps.js"));
            const QStringList qmlFiles = QDir(probeUiDir).entryList({ QStringLiteral("*.qml") }, QDir::Files);
            for (const QString &f : qmlFiles) {
                QFile p(probeUiDir + QLatin1Char('/') + f);
                if (!p.open(QIODevice::ReadOnly | QIODevice::Text))
                    continue;
                QString t = QString::fromUtf8(p.readAll());
                p.close();
                t.replace(QStringLiteral("import \"InventoryOps.js\" as InventoryOps"),
                          QStringLiteral("import \"") + jsUrl.toString() + QStringLiteral("\" as InventoryOps"));
                t.replace(QStringLiteral("import VoxelSandbox\n"),
                          QStringLiteral("import VoxelSandboxProbeT956\n"));
                if (p.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    p.write(t.toUtf8());
                    p.close();
                }
            }
        }
        QQmlEngine engine956;
        Hotbar vm956;
        PlayerState ps956;
        engine956.rootContext()->setContextProperty(QStringLiteral("t956Hotbar"), &vm956);
        QQmlComponent wrapComp956(&engine956);
        wrapComp956.setData(R"QML(import QtQuick
Item {
    id: window
    width: 800; height: 600
    property bool shiftHeld: false
    function refocusKeyInput() { }
    function closeAnvil() { }
}
)QML", QUrl());
        QQuickItem host956;   // 独立场景根（无窗口，同 t874）
        QObject *anvil956 = nullptr;
        QFile anvilSrc956(probeUiDir + QLatin1Char('/') + QStringLiteral("AnvilUI.qml"));
        if (wrapComp956.isError()) {
            behavDiag = QStringLiteral("wrapper: ") + wrapComp956.errorString();
        } else if (!anvilSrc956.open(QIODevice::ReadOnly)) {
            behavDiag = QStringLiteral("AnvilUI read failed");
        } else {
            QQuickItem *wrapItem = qobject_cast<QQuickItem *>(wrapComp956.create());
            if (!wrapItem) {
                behavDiag = QStringLiteral("wrapper create failed");
            } else {
                wrapItem->setParent(&engine956);
                wrapItem->setParentItem(&host956);
                QQmlComponent anvilComp956(&engine956);
                anvilComp956.setData(anvilSrc956.readAll(), QUrl::fromLocalFile(anvilSrc956.fileName()));
                if (anvilComp956.isError()) {
                    behavDiag = QStringLiteral("AnvilUI load: ") + anvilComp956.errorString();
                } else {
                    anvil956 = anvilComp956.create(qmlContext(wrapItem));
                    QQuickItem *ai = qobject_cast<QQuickItem *>(anvil956);
                    if (!ai) {
                        behavDiag = QStringLiteral("AnvilUI create: ") + anvilComp956.errorString();
                    } else {
                        anvil956->setProperty("hotbar", QVariant::fromValue(&vm956));
                        anvil956->setProperty("playerState", QVariant::fromValue(&ps956));
                        anvil956->setProperty("player", QVariant());
                        anvil956->setProperty("progress", QVariant());
                        ai->setWidth(800);
                        ai->setHeight(600);
                        anvil956->setParent(wrapItem);
                        ai->setParentItem(wrapItem);
                    }
                }
            }
        }
        if (!anvil956) {
            behavOk = false;
        } else {
            QCoreApplication::processEvents();
            // ⑤ review0830 #22：直连 itemTaken 计数（QML 小 tap 对象收数，经典 SIGNAL/SLOT 字串连接——
            //    信号不存在 → 连接失败计数恒 -1/0，(4) 步即红；发点在守卫后由 no-op 不计数实证）。
            QQmlComponent tapComp956(&engine956);
            tapComp956.setData(QByteArrayLiteral(
                                   "import QtQuick\n"
                                   "QtObject {\n"
                                   "    property int count: 0\n"
                                   "    function bump() { count += 1 }\n"
                                   "}\n"), QUrl());
            QObject *tap956 = tapComp956.create();
            if (tap956) tap956->setParent(anvil956);   // 生命周期挂面板
            const bool tapConn956 = tap956
                && QObject::connect(anvil956, SIGNAL(itemTaken()), tap956, SLOT(bump()));
            const auto takenCount956 = [tap956]() {
                return tap956 ? tap956->property("count").toInt() : -1;
            };
            const int stoneT = int(BR::Stone);
            const int swordT = int(ToolRegistry::DiamondSword);
            const int sharp3T = EnchantRegistry::pack(int(EnchantRegistry::Sharpness), 3);
            auto invokeCopy = [anvil956](int id, int count, int dur, const QVariantList &ench, const QString &name) {
                return QMetaObject::invokeMethod(anvil956, "copyStackToCursor",
                                                 Q_ARG(QVariant, QVariant(id)), Q_ARG(QVariant, QVariant(count)),
                                                 Q_ARG(QVariant, QVariant(dur)), Q_ARG(QVariant, QVariant(ench)),
                                                 Q_ARG(QVariant, QVariant(name)));
            };
            // 断言面（do/while(0) 顺序步进，首个失败步留 diag 短路余下步）：
            do {
                // (1) 满栈复制：源槽 5 件方块 → 光标 64 整组（t896 数量权威，非 min(5,64)=5）+ 旧光标被覆盖。
                vm956.setHeldBlock(int(RecipeRegistry::BucketEmptyId));   // 预置旧光标手持 → 应被覆盖（创造归还虚空同效）
                if (!invokeCopy(stoneT, 5, 0, { 0, 0, 0, 0 }, QString())) { behavDiag = QStringLiteral("invoke stone failed"); break; }
                if (vm956.heldBlock() != stoneT || vm956.heldCount() != vm956.maxStackSize(stoneT)) {
                    behavDiag = QStringLiteral("stone full-stack: held ") + QString::number(vm956.heldBlock())
                                 + QStringLiteral(" x") + QString::number(vm956.heldCount());
                    break;
                }
                if (vm956.heldDurability() != 0 || !vm956.heldCustomName().isEmpty()) { behavDiag = QStringLiteral("stone metadata leak"); break; }
                // (2) 工具实例保真：整组=1 + 耐久 / 附魔 / 名随实例复制（非「创造取新」的清零路径）。
                if (!invokeCopy(swordT, 1, 37, { sharp3T, 0, 0, 0 }, QStringLiteral("改名剑"))) { behavDiag = QStringLiteral("invoke sword failed"); break; }
                if (vm956.heldBlock() != swordT || vm956.heldCount() != 1) {
                    behavDiag = QStringLiteral("sword stack: ") + QString::number(vm956.heldCount());
                    break;
                }
                if (vm956.heldDurability() != 37) {
                    behavDiag = QStringLiteral("sword durability lost: ") + QString::number(vm956.heldDurability());
                    break;
                }
                const QVariantList he = vm956.heldEnchants();
                if (he.size() != 4 || he.at(0).toInt() != sharp3T) { behavDiag = QStringLiteral("sword ench lost (list4 chain)"); break; }
                if (vm956.heldCustomName() != QStringLiteral("改名剑")) {
                    behavDiag = QStringLiteral("sword name lost: ") + vm956.heldCustomName();
                    break;
                }
                // (3) 空槽 no-op：光标仍持改名剑（复制面只认实有物品格）。
                if (!invokeCopy(0, 0, 0, { 0, 0, 0, 0 }, QString())) { behavDiag = QStringLiteral("invoke empty failed"); break; }
                if (vm956.heldBlock() != swordT || vm956.heldCustomName() != QStringLiteral("改名剑")) { behavDiag = QStringLiteral("empty-slot copy clobbered cursor"); break; }
                // (4) review0830 #22 反馈发射：两次成功复制恰 2 发、空槽 no-op 0 发（发点在守卫之后的实锤；
                //     信号不存在 → 连接失败恒红）。
                if (!tapConn956 || takenCount956() != 2) {
                    behavDiag = QStringLiteral("itemTaken: conn=%1 count=%2")
                                    .arg(int(tapConn956)).arg(takenCount956());
                    break;
                }
                behavOk = true;
            } while (false);
        }
        QDir(probeUiDir).removeRecursively();

        const bool ok956 = okDefE && okDefA && okMidE && okMidA && okPreviewA
                           && okTakenE && okTakenA && okTakenMain && behavOk;
        if (!ok956)
            qInfo().noquote() << "  [t956 diag] defE" << okDefE << "defA" << okDefA << "midE" << okMidE
                              << "midA" << okMidA << "preview" << okPreviewA
                              << "takenE" << okTakenE << "takenA" << okTakenA
                              << "takenMain" << okTakenMain << "behav" << behavOk
                              << behavDiag;
        if (!ok956) ++totalFail;
        qInfo().noquote() << (ok956 ? "PASS" : "FAIL")
                          << "| t956 enchanting/anvil creative middle-click copy: the t896 chain now reaches "
                             "both block-adjacent workstations - each panel carries a panel-level "
                             "copyStackToCursor (verbatim Inventory.qml caliber: maxStackSize full-stack "
                             "quantity, list4 sequence normalization, durability/enchant/name instance "
                             "fidelity) and exactly three middle-button TapHandlers (input-slot component "
                             "+ main row + hotbar row) each creative-gated; the anvil product-preview slot "
                             "is excluded verbatim (copying a projected output would bypass takeProduct "
                             "consumption, same precedent as the crafting result slot); review0830 #22 "
                             "pickup feedback: both panels declare signal itemTaken() and emit it at the "
                             "end of a successful copy (no-op early-returns stay silent), the Main.qml "
                             "consumer is the same onItemTaken -> handPopAnim.start line across all three "
                             "panels, and the behavioral leg counts emissions over the real panel object "
                             "(exactly 2 for the two successful copies, 0 for the empty-slot no-op); "
                             "behavioral leg "
                             "drives the real AnvilUI.qml against the real Hotbar VM: 5-item stone source "
                             "copies as a 64 stack (not min(5,64)), tool copies as 1 with durability 37 / "
                             "sharpness-3 / custom name intact, stale cursor overwritten, empty slot no-op";
    }

    // ── t957 附魔台 UI 文案收口 + 青金石「空缺风格」轮廓图标（源码钉 ×2 + PNG 数据钉；R19.17）──
    //    用户第五轮口径：① 删「武器/工具」字样与「左槽放……」提示行；② 青金石轮廓图标不像 →
    //    边缘提取算法处理青金石 png（边缘变黑的空缺风格图标）。钉三面：
    //    ① 文案退场 —— 槽位类别 caption 字面 + caption 属性面（property 声明 / eslot.caption 消费点）
    //       与空槽 0 提示行字面全文件绝迹（字样回潮必先恢复属性面 → 结构双钉）；存留的右槽缺料
    //       提示行正锚（删空态行没误伤整行组件，t916 语义半保留）。
    //    ② 引用切换 —— t544 手绘 Canvas 占位（onPaint 指纹）绝迹，空槽占位改 Image 引用边缘提取
    //       离线资产 icon_lapis_outline.png（tools/build_lapis_outline.py 生成），showLapisOutline 门仍在。
    //    ③ PNG 数据钉 —— 资产本体结构断言：48×48、四角全透明、近黑不透明边缘像素成规模（「边缘
    //       变黑」）、半透冷蓝内部残色成规模（空缺/镂空感）；源图 icon_lapis_item.png 同在（提取链
    //       输入落盘，脚本重跑可复现）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile ef957(root + QStringLiteral("/src/ui/EnchantingTableUI.qml"));
        const QString e957 = ef957.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef957.readAll()) : QString();
        // ① 文案退场：caption 绑定字面 + 属性声明 + 消费点 + 空态提示行字面四重绝迹；存留提示正锚。
        const bool okA957 = !e957.contains(QStringLiteral("caption: \"武器/工具\""))
                         && !e957.contains(QStringLiteral("property string caption"))
                         && !e957.contains(QStringLiteral("eslot.caption"))
                         && !e957.contains(QStringLiteral("左槽放工具"))
                         && e957.contains(QStringLiteral("右槽放足青金石即解锁高栏"));
        // ② 引用切换：旧手绘占位（onPaint）绝迹 → 新资产引用 + showLapisOutline 门仍在。
        const bool okB957 = e957.contains(QStringLiteral("qrc:/textures/icon_lapis_outline.png"))
                         && e957.contains(QStringLiteral("showLapisOutline: true"))
                         && !e957.contains(QStringLiteral("onPaint:"));
        // ③ PNG 数据钉：48×48 / 四角全透明 / 近黑边缘 ≥120 px / 半透冷蓝内部 ≥60 px。
        QImage outline957(root + QStringLiteral("/textures/icon_lapis_outline.png"));
        bool okC957 = outline957.width() == 48 && outline957.height() == 48;
        int edgeDark957 = 0, ghostBlue957 = 0;
        for (int y = 0; okC957 && y < outline957.height(); ++y) {
            for (int x = 0; x < outline957.width(); ++x) {
                const QColor p = outline957.pixelColor(x, y);
                if (p.alpha() == 0) continue;
                if (p.alpha() == 255 && p.red() < 40 && p.green() < 40 && p.blue() < 40) ++edgeDark957;
                else if (p.alpha() < 200 && p.blue() > p.red() && p.blue() > 60) ++ghostBlue957;
            }
        }
        okC957 = okC957
                && outline957.pixelColor(2, 2).alpha() == 0 && outline957.pixelColor(45, 2).alpha() == 0
                && outline957.pixelColor(2, 45).alpha() == 0 && outline957.pixelColor(45, 45).alpha() == 0
                && edgeDark957 >= 120 && ghostBlue957 >= 60
                && QFile::exists(root + QStringLiteral("/textures/icon_lapis_item.png"));
        const bool ok957 = okA957 && okB957 && okC957;
        if (!ok957)
            qInfo().noquote() << "  t957 diag: wordingGone" << okA957 << "iconRef" << okB957
                              << "pngData" << okC957 << "edgeDark" << edgeDark957
                              << "ghostBlue" << ghostBlue957;
        if (!ok957) ++totalFail;
        qInfo().noquote() << (ok957 ? "PASS" : "FAIL")
                          << "| t957 enchanting-table wording cleanup + lapis void-style outline icon: "
                             "the slot category caption (binding literal, property declaration and "
                             "consumer) and the empty-slot-0 hint line are extinct from the panel "
                             "(the surviving lapis-shortage hint is pinned as the positive anchor - "
                             "deleting the empty-state row did not take the whole hint component "
                             "down), the t544 hand-drawn Canvas placeholder (onPaint fingerprint) is "
                             "replaced by an Image referencing the offline edge-extracted asset "
                             "icon_lapis_outline.png still gated by showLapisOutline, and the PNG "
                             "data pin asserts the void-style structure (48x48, fully transparent "
                             "corners, a solid population of near-black opaque edge pixels = edges "
                             "turned black, semi-transparent cool-blue interior residue = hollow "
                             "feel) with the extraction-chain source icon_lapis_item.png on disk "
                             "(tools/build_lapis_outline.py reruns reproducibly)";
    }

    // ── t958 铁砧面板操作内容垂直居中（源码钉；R19.17 🅳）──
    //    用户第五轮口径：「铁砧 UI 上下居中：改名栏与 A+B→C 下方空一行——面板内容垂直居中」。
    //    旧布局把操作内容（改名框 / A+B→C 槽行 / 等级·冲突提示行）整块 top 钉死在 anvilArea 顶
    //    （改名框 topMargin 2 起步），内容自然底 ~y110 落在 134 高的操作区内 → 底部恒留 ~24px 空行
    //    （上挤下空）。钉三面（QML 布局无 static_assert 面，源码即契约；t954/t955 纯视觉项先例；
    //    AnvilUI.qml 整文档可加载性由 t956 行为腿的真链直载覆盖，此处不重复 harness）：
    //    ① 居中形态钉 —— 操作内容包进 opBlock 内容块（width 随操作区 / height = 内容自然高 108 /
    //       anchors.verticalCenter 锚操作区中线），旧顶对齐形态（改名框 top 钉 + 固定 2px 上距）
    //       全文件绝迹；
    //    ② 包裹结构钉 —— id 链 opBlock → renameBox → slotRow → 两条 slotRow.bottom 锚（等级行 /
    //       冲突行）依序全落在 flash 叠层注释之前（改名框起的三段链整体收进块内）；
    //    ③ 内部刚性钉 —— 改名框贴块顶无固定上距（上下留白由居中锚承担）+ 槽行仍锚改名框下 10px
    //       （块内相对关系逐字未动，居中只平移整块、不重排内部）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile af958(root + QStringLiteral("/src/ui/AnvilUI.qml"));
        const QString a958 = af958.open(QIODevice::ReadOnly) ? QString::fromUtf8(af958.readAll()) : QString();
        // ① 居中形态：opBlock 声明邻域三要素 + 旧顶对齐形态绝迹。
        //   review0830 #7：块高 108 只容单行冲突文案（冲突行 top=96 剩 12px），3 行红字越过 134
        //   操作区底 ~11px 泼进主栏 → 块高放 120（冲突行可用高 24 = 实测最坏合并文案 2 行整）。
        const int iBlk958 = a958.indexOf(QStringLiteral("id: opBlock"));
        const QString blk958 = iBlk958 >= 0 ? a958.mid(iBlk958, 240) : QString();
        const bool okA958 = iBlk958 >= 0
                && blk958.contains(QStringLiteral("width: parent.width"))
                && blk958.contains(QStringLiteral("height: 120"))
                && blk958.contains(QStringLiteral("anchors.verticalCenter: parent.verticalCenter"))
                && !a958.contains(QStringLiteral("anchors.top: parent.top; anchors.topMargin: 2"));
        // ② 包裹结构：改名框链依序落在块声明与 flash 叠层之间。
        const int iRen958 = a958.indexOf(QStringLiteral("id: renameBox"));
        const int iRow958 = a958.indexOf(QStringLiteral("id: slotRow"));
        const int iCost958 = a958.indexOf(QStringLiteral("anchors.top: slotRow.bottom; anchors.topMargin: 2"));
        const int iConf958 = a958.indexOf(QStringLiteral("anchors.top: slotRow.bottom; anchors.topMargin: 20"));
        const int iFlash958 = a958.indexOf(QStringLiteral("「操作成功」绿色 flash 叠层"));
        const bool okB958 = iBlk958 >= 0 && iBlk958 < iRen958 && iRen958 < iRow958
                && iRow958 < iCost958 && iCost958 < iConf958 && iConf958 < iFlash958;
        // ③ 内部刚性：改名框贴块顶无上距 + 槽行锚改名框下 10px（内部关系刚性保持）。
        const QString ren958 = iRen958 >= 0 ? a958.mid(iRen958, 200) : QString();
        const QString row958 = iRow958 >= 0 ? a958.mid(iRow958, 200) : QString();
        const bool okC958 = ren958.contains(QStringLiteral("anchors.top: parent.top"))
                && !ren958.contains(QStringLiteral("topMargin"))
                && row958.contains(QStringLiteral("anchors.top: renameBox.bottom; anchors.topMargin: 10"));
        // ④ 文本余量断言（review0830 #7；QML 布局无 static_assert 面 → 常量推算 + 真引擎实测双腿）：
        //   从源解析块内几何常量（块高/区高/改名框高/槽行高/两级 topMargin），推算冲突行 top；
        //   (a) 块内可用高（块高 − 冲突行 top）≥ 实测最坏合并冲突文案高（真 QQmlEngine 量 Text
        //       implicitHeight，字体口径与运行环境一致）；
        //   (b) 3 行理论最坏（3×实测单行高）在区内不得泼进下方主物品栏行（≤ 操作区底 + Column 行距 10）。
        double blockH958 = 0, areaH958 = 0, confTop958 = 0, textH2 = 0, textH1 = 0;
        bool measOk958 = false;
        {
            // 锚点后第一个数字（跳过任意间隔空白/换行缩进；t958 块内常量逐个定位，避免全文首个误配）。
            auto numAt958 = [&a958](const QString &anchor, int from) -> double {
                const int i = a958.indexOf(anchor, from);
                if (i < 0) return -1;
                int n = i + anchor.size();
                while (n < a958.size() && !a958.at(n).isDigit()) {
                    if (!a958.at(n).isSpace() && a958.at(n) != QLatin1Char('-')) return -1;
                    ++n;
                }
                int e = n;
                while (e < a958.size() && (a958.at(e).isDigit() || a958.at(e) == QLatin1Char('.'))) ++e;
                return a958.mid(n, e - n).toDouble();
            };
            const int iArea958 = a958.indexOf(QStringLiteral("id: anvilArea"));
            const int iRenId958 = a958.indexOf(QStringLiteral("id: renameBox"));
            const double renH = numAt958(QStringLiteral("height: "), iRenId958);
            const double rowH = numAt958(QStringLiteral("height: "), iRow958);
            const double rowW = numAt958(QStringLiteral("width: "), iRow958);
            blockH958 = numAt958(QStringLiteral("height: "), iBlk958);
            areaH958 = numAt958(QStringLiteral("height: "), iArea958);
            const double rowTopM = numAt958(QStringLiteral("anchors.topMargin: "),
                                            a958.indexOf(QStringLiteral("anchors.top: renameBox.bottom")));
            const double confTopM = numAt958(QStringLiteral("anchors.topMargin: "), iConf958);
            const double colSpace = numAt958(QStringLiteral("spacing: "),
                                             a958.indexOf(QStringLiteral("anchors.margins: 12")));
            measOk958 = renH > 0 && rowH > 0 && rowW > 0 && blockH958 > 0 && areaH958 > 0
                     && rowTopM >= 0 && confTopM >= 0 && colSpace >= 0;
            if (measOk958) {
                confTop958 = renH + rowTopM + rowH + confTopM;   // 改名框高 + 槽行上距 + 槽行高 + 冲突行上距
                measOk958 = measOk958 && int(rowW + 60) == 236;  // 冲突行宽钉（文案量测宽度与真值同源）
            }
            // 真 QQmlEngine 量测（t956 真链装配法：Item 载体 + 隐藏 Text，无窗口可见性依赖——
            //   implicitHeight 在 text/width 设定后经 polish 求值，processEvents 泵足）。
            QQmlEngine eng958;
            QQmlComponent comp958(&eng958);
            comp958.setData(R"QML(import QtQuick
Item {
    width: 400; height: 200
    Text {
        objectName: "one958"
        visible: false; width: 236
        wrapMode: Text.WrapAnywhere; font.pixelSize: 9
        text: "冲突：锋利 III　不适用：耐久 I"
    }
    Text {
        objectName: "worst958"
        visible: false; width: 236
        wrapMode: Text.WrapAnywhere; font.pixelSize: 9
        text: "冲突：锋利 III、摔落缓冲 IV、节肢杀手 III　不适用：耐久 I　替换 ×2"
    }
}
)QML", QUrl());
            QQuickItem host958;
            QObject *mroot958 = comp958.isError() ? nullptr : comp958.create();
            if (!mroot958) {
                measOk958 = false;
            } else {
                mroot958->setParent(&eng958);
                QQuickItem *mi = qobject_cast<QQuickItem *>(mroot958);
                mi->setParentItem(&host958);
                for (int i = 0; i < 8; ++i)
                    QCoreApplication::processEvents();
                const auto kids = mroot958->findChildren<QObject *>();
                for (QObject *k : kids) {
                    if (k->objectName() == QStringLiteral("one958"))
                        textH1 = k->property("implicitHeight").toDouble();
                    if (k->objectName() == QStringLiteral("worst958"))
                        textH2 = k->property("implicitHeight").toDouble();
                }
                measOk958 = measOk958 && textH1 > 0 && textH2 >= textH1;
                delete mroot958;
            }
        }
        // (a) 块内冲突行可用高 ≥ 实测最坏文案高（含浮点容差）；(b) 3 行最坏不出操作区底 + 行距（不泼主栏）。
        const double blockTop958 = (areaH958 - blockH958) / 2.0;
        const bool okD958 = measOk958
                && (blockH958 - confTop958) >= textH2 - 1e-9
                && (blockTop958 + confTop958 + 3.0 * textH1) <= areaH958 + 10.0 + 1e-9;
        const bool ok958 = okA958 && okB958 && okC958 && okD958;
        if (!ok958)
            qInfo().noquote() << "  t958 diag: centeredForm" << okA958 << "wrapStructure" << okB958
                              << "rigidInner" << okC958 << "textBudget" << okD958
                              << "meas" << measOk958 << "blockH" << blockH958 << "areaH" << areaH958
                              << "confTop" << confTop958 << "blockTop" << blockTop958
                              << "textH1" << textH1 << "textH2" << textH2
                              << "blk@" << iBlk958 << "ren@" << iRen958
                              << "row@" << iRow958 << "cost@" << iCost958 << "conf@" << iConf958
                              << "flash@" << iFlash958;
        if (!ok958) ++totalFail;
        qInfo().noquote() << (ok958 ? "PASS" : "FAIL")
                          << "| t958 anvil panel operation content vertically centered: the operation "
                             "content (rename box + A+B->C slot row + level/conflict hint lines) is "
                             "wrapped in an opBlock content block (width tracking the operation area, "
                             "height = the 120px content extent, review0830 #7: 108 left the conflict "
                             "row only 12px = a single wrapped line, so a 2-3 line merge-conflict text "
                             "spilled ~11px past the 134px operation area into the main inventory row) "
                             "pinned with "
                             "anchors.verticalCenter to the operation-area midline - the old "
                             "top-pinned form (rename box anchored to parent.top with a fixed 2px top "
                             "margin, leaving a ~24px dead band below the A+B->C row inside the 134px "
                             "area) is extinct; the wrapped chain order (block -> renameBox -> slotRow "
                             "-> the two slotRow.bottom hints) all lands before the success-flash "
                             "overlay comment, and the internal relationships stay byte-identical "
                             "(rename box flush at block top without a fixed margin, slot row still "
                             "renameBox.bottom + 10) - centering only translates the block, never "
                             "re-lays-out its content; the text-budget leg parses the block geometry "
                             "constants from the source and a real QQmlEngine-measured worst-case "
                             "merge-conflict Text (same 9px/WrapAnywhere/236px caliber) asserting the "
                             "in-block available height covers the measured worst text and the "
                             "3-line theoretical worst never reaches past the operation area bottom "
                             "+ column spacing (never paints over the inventory row)";
    }

    // ── t959 附魔池随机性收窄探针（R19.17 🅳；Game 层表 + Hotbar 桥接，无 World/QML —— t824 同台先例）──
    //    用户第五轮口径：「书本附魔把很多工具+装甲附魔冲突地混在一起——书附魔池按类别（工具/武器/装甲/书）
    //    收窄 + 冲突组规则（同组互斥如保护系/锋利+截肢系）」。t959 收口：注册表每附魔加 homeCategory
    //    （EnchantCategory：weapon/tool/armor/universal；t960 扩 bow/rod 两类）+ conflictGroup
    //    （=exclusiveGroup 单值形式）；
    //    selectEnchantsForItem 对书载体先由同一 LCG roll **主类别**（武器/工具/装甲五选一均匀轮——t959
    //    起三类，t960 弓/竿系入表扩五类），候选收窄到「该类 ∪ 通用（耐久）」；同次产物内同组
    //    互斥由既有位集抽样（review M1）照旧保证。**单一权威落点**：收窄在 selectEnchantsForItem 内部 =
    //    预告（tierPreviewName → Hotbar::selectEnchantsPreviewForItem）与施放（doEnchant 同桥）共用的那
    //    一层，t917「预告==施放严格同源」契约结构性保持（review28 #3 种子快照序零触碰）。
    //    断言：
    //    (a) 注册表数据钉 —— 20 条附魔逐 id homeCategory / conflictGroup 与语义表一致（字段齐备；
    //        表行多列初始化错位在此必红）。review0830 #24：语义表按 NID 定尺寸 + static_assert 同长
    //        （EnchantCount 扩而表未扩 = 编译期红，非 OOB 静默）；注册表字段面另有 kEnchants 行内
    //        consteval 校验（漏写 homeCategory 编译期红）。
    //    (b) 书附魔行为腿（offered 2/12/30 × seed 0..399 共 1200 次施法）：
    //        ① 单次产物**无跨类混出**（非通用附魔的 homeCategory 在单次产物内全一致 = 主类别成簇；
    //           旧全池行为「锐锋(武器)+效率(工具)」式跨类产物必现 = 用户症状，阴性轮复现）；
    //        ② 单次产物内部无同组互斥对（conflictsWith 两两判假）；
    //        ③ 产物每条 isApplicableForItem(book)（书载体合法性）+ 等级 ∈ [1,maxLevel]；
    //        ④ 跨 1200 次施法全 20 附魔都出现（主类别随机轮换 → union 不收窄，长期可达性）；
    //        ⑤ 确定性：同 seed 两次施法产物逐条相等（主类别 roll 消耗同一 LCG 流，t917 同源前提）。
    //    (c) 直附面腿 —— 镐/铲 ⊆ {效率,精准,时运,耐久}（**直附工具不出护甲/武器附魔**）且四元全在；
    //        胸甲 ⊆ {保护,火焰保护,弹射物保护,耐久}（不出工具/武器系）——t824 逐物品过滤面不回归。
    //    (d) t917 同源钉 —— 书物品 Hotbar::selectEnchantsPreviewForItem == EnchantRegistry::
    //        selectEnchantsForItem 同 seed 逐条相等（收窄活在桥下共用层，QML 面无副本）。
    {
        Hotbar hb;
        const int bookId   = RecipeRegistry::BookId;
        const int diaPick  = int(ToolRegistry::PickaxeDiamond);
        const int diaShovel = int(ToolRegistry::DiamondShovel);
        const int diaChest = int(RecipeRegistry::ArmorIdBase) + 4 * 4 + 1;
        const int E  = int(EnchantRegistry::Efficiency),    ST = int(EnchantRegistry::SilkTouch);
        const int F  = int(EnchantRegistry::Fortune),       U  = int(EnchantRegistry::Unbreaking);
        const int SH = int(EnchantRegistry::Sharpness),     UD = int(EnchantRegistry::UndeadSlay);
        const int AR = int(EnchantRegistry::ArthropodSlay), KB = int(EnchantRegistry::Knockback);
        const int FA = int(EnchantRegistry::FireAspect),    P  = int(EnchantRegistry::Protection);
        const int FP = int(EnchantRegistry::FireProtection), PR = int(EnchantRegistry::ProjectileProt);
        const int FF = int(EnchantRegistry::FeatherFall),   AA = int(EnchantRegistry::AquaAffinity);
        const int WC = int(EnchantRegistry::EnchantCatWeapon), TC2 = int(EnchantRegistry::EnchantCatTool);
        const int AC = int(EnchantRegistry::EnchantCatArmor),  UC = int(EnchantRegistry::EnchantCatUniversal);
        const int BC960 = int(EnchantRegistry::EnchantCatBow), RC960 = int(EnchantRegistry::EnchantCatRod);
        // review0830 #24：constexpr（数组定尺寸用）——语义表与 NID 解耦的编译期闸见下方 static_assert。
        constexpr int NID = int(EnchantRegistry::EnchantCount); // t960 起 21（1..20；固定 15 数组会越界）

        // (a) 注册表数据钉：id → {homeCategory, conflictGroup} 语义表逐行比对（0 号占位行不在钉内；
        //     t960 弓四件 = Bow / 竿两件 = Rod、组 0）。review0830 #24：语义表按 NID（EnchantCount）定
        //     尺寸 + static_assert 同长——注册表扩条（EnchantCount 变）而本表未同步补行 → 编译期红
        //     （旧定长 21 数组 + NID 循环在第 22 条上是 OOB 读 UB，可能静默通过而非报红，防护补丁）。
        constexpr int expectCat[] = { 0, WC, WC, WC, WC, WC, TC2, TC2, TC2, UC, AC, AC, AC, AC, AC,
                                      BC960, BC960, BC960, BC960, RC960, RC960 };
        constexpr int expectGrp[] = { 0,  1,  1,  1,  0,  0,   2,   2,   2,  0,  3,  3,  3,  3,   0,
                                          0,     0,     0,     0,     0,     0 };
        static_assert(int(sizeof(expectCat) / sizeof(expectCat[0])) == NID
                      && int(sizeof(expectGrp) / sizeof(expectGrp[0])) == NID,
                      "P-t959 语义表须与 EnchantRegistry::EnchantCount 同步扩行（review0830 #24：防第 22 条 OOB 静默）");
        bool okData = true;
        for (int i = 1; i < NID; ++i)
            okData = okData && EnchantRegistry::homeCategory(i) == expectCat[i]
                             && EnchantRegistry::conflictGroup(i) == expectGrp[i];

        // (b) 书附魔行为腿：1200 次施法逐产物断言 ①②③ + 累计 ④。seenBook 按 NID 定尺寸
        //     （review0830 #24：按下标 id 写入，旧定长 21 在第 22 条上 OOB 写 UB）。
        const int offeredList[3] = { 2, 12, 30 };
        bool okCluster = true, okNoConflict = true, okApplicable = true;
        bool seenBook[NID];
        for (int i = 0; i < NID; ++i) seenBook[i] = false;
        for (int oi = 0; oi < 3; ++oi) {
            for (int seed = 0; seed < 400; ++seed) {
                const QVariantList picks = EnchantRegistry::selectEnchantsForItem(bookId, offeredList[oi], seed);
                if (picks.isEmpty()) { okCluster = false; continue; } // 书池恒非空（任一主类别池 ≥3 条）
                int catSeen = 0;                                      // 本产物已见的非通用主类别（0 = 未定）
                for (int a = 0; a < picks.size(); ++a) {
                    const QVariantMap ma = picks.at(a).toMap();
                    const int ida = ma.value(QStringLiteral("id")).toInt();
                    const int la  = ma.value(QStringLiteral("level")).toInt();
                    seenBook[ida] = true;
                    if (!EnchantRegistry::isApplicableForItem(ida, bookId)) okApplicable = false;
                    if (la < 1 || la > EnchantRegistry::maxLevel(ida)) okApplicable = false;
                    const int ca = EnchantRegistry::homeCategory(ida);
                    if (ca != UC) {                                   // 通用（耐久）任意主类别池合法
                        if (catSeen == 0) catSeen = ca;
                        else if (catSeen != ca) okCluster = false;    // ① 跨类混出 = 用户症状复现点
                    }
                    for (int b = a + 1; b < picks.size(); ++b) {
                        const int idb = picks.at(b).toMap().value(QStringLiteral("id")).toInt();
                        if (EnchantRegistry::conflictsWith(ida, idb)) okNoConflict = false; // ② 同组互斥对
                    }
                }
            }
        }
        bool okUnion = true;
        for (int i = 1; i < NID; ++i) okUnion = okUnion && seenBook[i];   // ④ 全 20 附魔长期仍都可达
        // ⑤ 同 seed 确定性（主类别 roll 在抽样前消耗同一 LCG 流 → 同 seed 恒同产物）。
        const QVariantList p1 = EnchantRegistry::selectEnchantsForItem(bookId, 21, 777);
        const QVariantList p2 = EnchantRegistry::selectEnchantsForItem(bookId, 21, 777);
        bool okDeterminism = p1.size() == p2.size() && !p1.isEmpty();
        for (int i = 0; okDeterminism && i < p1.size(); ++i)
            okDeterminism = p1.at(i).toMap().value(QStringLiteral("id")) == p2.at(i).toMap().value(QStringLiteral("id"))
                         && p1.at(i).toMap().value(QStringLiteral("level")) == p2.at(i).toMap().value(QStringLiteral("level"));

        // (c) 直附面腿：镐/铲 ⊆ 采集池、胸甲 ⊆ 护甲池（subset + requireAll 双向，防过滤过头砍空池）。
        const auto poolScan = [&](int itemId, bool *seen) {
            for (int i = 0; i < NID; ++i) seen[i] = false;
            for (int oi = 0; oi < 3; ++oi)
                for (int seed = 0; seed < 400; ++seed) {
                    const QVariantList picks = EnchantRegistry::selectEnchantsForItem(itemId, offeredList[oi], seed);
                    for (const QVariant &v : picks) seen[v.toMap().value(QStringLiteral("id")).toInt()] = true;
                }
        };
        const auto poolIs = [&](int itemId, const std::vector<int> &allowed) {
            bool seen[NID];   // review0830 #24：同 (b) seenBook——按下标 id 写入须随 EnchantCount 定尺寸
            poolScan(itemId, seen);
            for (int i = 1; i < NID; ++i) {
                const bool allowedHas = std::find(allowed.begin(), allowed.end(), i) != allowed.end();
                if (seen[i] && !allowedHas) return false;   // 出现不允许的（直附工具出护甲/武器附魔）
                if (allowedHas && !seen[i]) return false;   // 允许的没出现（池被砍空）
            }
            return true;
        };
        const std::vector<int> miningPool = { E, ST, F, U };
        const std::vector<int> chestPool  = { P, FP, PR, U };
        const bool okDirect = poolIs(diaPick, miningPool) && poolIs(diaShovel, miningPool)
                           && poolIs(diaChest, chestPool);

        // (d) t917 同源钉：书物品桥接 == 直调（同 seed 同产物——收窄在共用层，QML 面无副本）。
        const QVariantList viaBridgeB = hb.selectEnchantsPreviewForItem(bookId, 17, 4242);
        const QVariantList directB    = EnchantRegistry::selectEnchantsForItem(bookId, 17, 4242);
        bool okBridge = viaBridgeB.size() == directB.size() && !directB.isEmpty();
        for (int i = 0; okBridge && i < int(directB.size()); ++i)
            okBridge = viaBridgeB.at(i).toMap().value(QStringLiteral("id")) == directB.at(i).toMap().value(QStringLiteral("id"))
                    && viaBridgeB.at(i).toMap().value(QStringLiteral("level")) == directB.at(i).toMap().value(QStringLiteral("level"));

        const bool ok959 = okData && okCluster && okNoConflict && okApplicable && okUnion
                        && okDeterminism && okDirect && okBridge;
        if (!ok959)
            qInfo().noquote() << "  t959 diag: data" << okData << "cluster" << okCluster
                              << "noConflict" << okNoConflict << "applicable" << okApplicable
                              << "union" << okUnion << "determinism" << okDeterminism
                              << "direct" << okDirect << "bridge" << okBridge;
        if (!ok959) ++totalFail;
        qInfo().noquote() << (ok959 ? "PASS" : "FAIL")
                          << "| t959 enchant pool category narrowing: every enchant carries homeCategory"
                             " (weapon/tool/armor/bow/rod/universal - t960 added the bow/rod categories)"
                             " + conflictGroup pinned per id; a book cast"
                             " first rolls ONE main category from the same LCG stream and draws only"
                             " from that category pool plus universal (unbreaking), so a single cast"
                             " never mixes cross-category lines (the old full-pool union let"
                             " sharpness-family + efficiency + protection coalesce into one book ="
                             " the user symptom), same-group exclusives stay pairwise-absent within a"
                             " product, all 20 enchants remain reachable across casts (union"
                             " un-narrowed), same seed reproduces the identical product, direct"
                             " item enchanting keeps the t824 per-item pools (pick/shovel mining-only"
                             " - no armor lines on tools - chest armor-only), and the narrowing lives"
                             " under the Hotbar bridge so preview==cast stays single-source (t917)";
    }
}
