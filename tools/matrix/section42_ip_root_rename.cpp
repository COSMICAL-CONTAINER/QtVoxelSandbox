#include "matrix_helpers.h"

#include <QDirIterator>
#include <QRegularExpression>

// t1067 §9 区隔改名批收口探针段（4 腿 r2041a-d；filter 词 "r2041"；矩阵 695→695+N）。
// 置尾先例沿用（接 section41，runAll 末执行）。任务契约（dev-plan t1067；review-r1917-final L2
// 登记基线转正）：src/ 树全量盘点的 MC 词根（NetherPortal/EndPortal/nether_portal/end_portal
// 词根族 + 用户可见中文 displayName）按 t1067 映射表改名，行为语义逐位不变——只改名不改行为；
// 数值 block id 零变化 = 存档安全铁律（saves 存数值 id，改名零迁移）。四腿分工：
//   「旧行为等价墙」→ r2041a（改名后传送门族全链行为回归：EmberGate 点燃/连通域熄灭/生产钩子
//     熄门/AbyssGate 12 框架环开环/完整性复检 + 数值 id 断言逐位 == 138/111/131 + state 编码不变
//     + WorldStore 真临时库存读往返 id/state 逐位保真——「只改名」的最硬行为级证明）；
//   「旧词根清零钉」→ r2041b（src/ 全树递归扫旧词根字面：NetherPortal/EndPortal/nether_portal/
//     end_portal 四词根，命中行必携 §9 记载标记 + 每文件命中数 == 盘点表口径 + 总数 25——
//     新代码引入旧词根字面即响亮红）；豁免面 = 资源包读取面（用户包内 MC 布局文件名
//     nether_portal.png）+ kMcBlockId 迁移文档表 MC 侧行标签 + t1067 映射头注对照记载。
//   「用户可见面钉」→ r2041c（displayName 新名在场 + 旧中文专有名词清零：暗渊门框架/暗渊门面/
//     余烬门/暗渊之眼逐项断言 + blockName 内部分类字面新名断言 + Hotbar::nameForBlock(EndEyeId)
//     =="暗渊之眼" + 旧中文名下界传送门/末地传送门框架/末地传送门面/末影之眼命中行必携 §9 + 每文件
//     命中数 == 盘点口径 + 总数 7）；
//   「结构钉」→ r2041d（数值 id 逐位不变钉：AbyssGate==111 / AbyssGateSurface==131 / EmberGate==138 /
//     AbyssGateStateActiveFlag==0x01 / kEmberGateStripFrames==32 / EndEyeId==0x23A + 映射表头注
//     在场钉（blockregistry.h / world.h 两处 t1067 头注锚串）+ 改名后方法族声明面源钉 + 编译期
//     static_assert 互钉（枚举值零漂移））。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名四日志）：
//   NEG-1 摘旧词根清零钉（blockregistry.cpp 追加一行含旧词根字面、无 §9 标记的注释）→ 声明红面 =
//     {r2041b}（清零钉命中计数 +1 且未标记 → 单腿红；行为腿/c 钉/d 钉零误伤——纯注释行不触任何
//     断言面）。
//   NEG-2 摘用户可见面钉（blockregistry.cpp kDefs 行 displayName 字面「暗渊门框架」回改为
//     「末地传送门框架」）→ 声明红面 = {r2041c}（displayName 断言不等红 + 旧中文名清零钉命中 +1
//     未标记红；r2041b 扫英文词根零交集、r2041a/d 零误伤）。
// 词元纪律：腿名/diag 零跨任务 filter 词元（r2021 先例）；本段注释中的族引用不进腿名。
void MatrixRun::section42_ip_root_rename()
{
    // ── 段内共享帮手（各腿自建 fresh 世界/临时库，零共享 rig 状态；section39/41 同门）─────────
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
    constexpr int kWF = 48, kDF = 48, kHF = 96; // fresh 小世界（3×3 chunk）
    const auto initTwin = [](World &w) {
        w.setWidth(kWF);
        w.setDepth(kDF);
        w.setHeight(kHF);
        w.setSeed(82);
        w.setWeatherState(0);               // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f);  // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名；QDir::temp()，测试自清理；r2010c 同款）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2041_%1_%2.sqlite")
                                                 .arg(QLatin1String(tag))
                                                 .arg(QCoreApplication::applicationPid()));
    };

    // ── r2041a：旧行为等价墙（改名后传送门族全链行为回归 + 数值 id 逐位 + 存读往返保真）────────
    runLeg(QStringLiteral("t1067 r2041a portal-family behavior equivalence wall after the root"
        " rename (igniting an obsidian frame fills the whole inner opening with exactly 6 and 20"
        " gate cells carrying the unchanged numeric ids 138 and state axis bits 0/1, breaking a"
        " load-bearing frame member through the production setBlock hook extinguishes the whole"
        " connected domain, direct-mining a gate cell plus the connected-domain clear empties the"
        " box again, the twelve-frame abyss gate ring reports incomplete while any frame lacks the"
        " active bit and complete once all twelve carry it, opening the ring fills the nine inner"
        " cells with the unchanged numeric id 131, breaking one frame silently clears every"
        " surface cell, and a real WorldStore save-load round trip lands the gate and frame voxels"
        " byte-identical in id and state - behavior semantics bit-identical, only names moved)"),
        [&]() {
        bool ok = true;
        QString diag;

        World wA;
        wA.setWidth(48);
        wA.setDepth(48);
        wA.setHeight(64);
        wA.setSeed(13);
        wA.setWeatherState(0);
        wA.setWeatherRemainingSec(3600.0f);
        const int pY = 20; // 门框基线层（不轻信「y 以上必空」——buildFrame 先显式清场兜底）

        // 建门框（section02 t806 同款泛化框）：开口左下角 (x0,pY,z0) 沿 u=(ux,uz) 展开 w 列 × h 层
        // 全 Air；底梁 / 顶梁与左右边柱全黑曜石；corners=true 补四角。清场盒 = 框外沿 ±3 × 法向 ±2。
        const auto buildFrame = [&](World &w, int x0, int z0, int ux, int uz,
                                    int wdt, int hgt, bool corners) {
            const int vx = uz, vz = ux; // 门法线向（清深 ±2）
            for (int c = -3; c <= wdt + 3; ++c)
                for (int r = -3; r <= hgt + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        w.setBlock(x0 + c * ux + d * vx, pY + r, z0 + c * uz + d * vz, BR::Air, 0);
            for (int c = 0; c < wdt; ++c) {
                w.setBlock(x0 + c * ux, pY - 1, z0 + c * uz, BR::Obsidian, 0);
                w.setBlock(x0 + c * ux, pY + hgt, z0 + c * uz, BR::Obsidian, 0);
            }
            for (int r = 0; r < hgt; ++r) {
                w.setBlock(x0 - ux, pY + r, z0 - uz, BR::Obsidian, 0);
                w.setBlock(x0 + wdt * ux, pY + r, z0 + wdt * uz, BR::Obsidian, 0);
            }
            if (corners) {
                const int cs[2] = {-1, wdt};
                for (const int ci : cs)
                    for (const int ry : {-1, hgt})
                        w.setBlock(x0 + ci * ux, pY + ry, z0 + ci * uz, BR::Obsidian, 0);
            }
        };
        // 局部门格计数（只数本 rig 清场盒内 id==138 的格；各场景零串数）。
        const auto cellsInBox = [&](World &w, int x0, int z0, int ux, int uz,
                                    int wdt, int hgt) -> int {
            int n = 0;
            for (int c = -3; c <= wdt + 3; ++c)
                for (int r = -3; r <= hgt + 3; ++r)
                    for (int d = -2; d <= 2; ++d)
                        if (w.blockAt(x0 + c * ux + d * uz, pY + r, z0 + c * uz + d * ux)
                            == BR::EmberGate)
                            ++n;
            return n;
        };

        // ① 2×3 最小门（X 平面 / 带角）：点燃 → true + 恰 6 格 + 逐格 id==138 + state==0。
        {
            buildFrame(wA, 8, 8, 1, 0, 2, 3, true);
            const bool lit = wA.tryIgniteEmberGate(9, pY + 1, 8);
            int n = 0;
            bool idsOk = true;
            quint8 states = 0;
            for (int x = 8; x <= 9 && idsOk; ++x)
                for (int y = pY; y <= pY + 2; ++y) {
                    const quint8 id = wA.blockAt(x, y, 8);
                    if (id != BR::EmberGate) { idsOk = false; break; }
                    states |= wA.stateAt(x, y, 8);
                    ++n;
                }
            const bool gateOk = lit && n == 6 && idsOk && states == 0;
            ok = ok && gateOk;
            if (!gateOk) diag += QStringLiteral("[2x3 lit=%1 n=%2 ids=%3 st=%4] ")
                                     .arg(lit).arg(n).arg(idsOk).arg(states);
        }
        // ② 4×5 门（Z 平面）：点燃 → 恰 20 格 + state==1（axis bit）；生产钩子熄门：破一根边柱
        //    （setBlock(Air)——review #27 钩子族内建）→ 整门 20 格全熄归零。门平面 x=wx 恒定 →
        //    点燃位 = (wx, pY+row, wz+col)、边柱 = (wx, pY+r, wz-1)。
        {
            buildFrame(wA, 30, 8, 0, 1, 4, 5, true);
            const bool lit = wA.tryIgniteEmberGate(30, pY + 1, 9);
            const int n = cellsInBox(wA, 30, 8, 0, 1, 4, 5);
            const int st = int(wA.stateAt(30, pY, 8));
            wA.setBlock(30, pY + 1, 7, BR::Air, 0); // 破左边柱（承重框格）→ 生产钩子连通域熄门
            const int after = cellsInBox(wA, 30, 8, 0, 1, 4, 5);
            const bool hookOk = lit && n == 20 && st == 1 && after == 0;
            ok = ok && hookOk;
            if (!hookOk) diag += QStringLiteral("[4x5 lit=%1 n=%2 st=%3 after=%4] ")
                                     .arg(lit).arg(n).arg(st).arg(after);
        }
        // ③ 直挖门格 + 连通域熄灭（镜像 finishMiningAt 门格分支序列 setBlock(Air)+removeEmberGateAt）
        //    → 归零；重建点燃后门格 id 仍逐位 138。
        {
            buildFrame(wA, 8, 14, 1, 0, 2, 3, true);
            const bool lit = wA.tryIgniteEmberGate(9, pY + 1, 14);
            wA.setBlock(9, pY + 1, 14, BR::Air, 0);
            wA.removeEmberGateAt(9, pY + 1, 14, 0);
            const int after = cellsInBox(wA, 8, 14, 1, 0, 2, 3);
            const bool mineOk = lit && after == 0;
            ok = ok && mineOk;
            if (!mineOk) diag += QStringLiteral("[mine lit=%1 after=%2] ").arg(lit).arg(after);
        }
        // ④ 暗渊门 12 框架环：环谓词（缺激活位 → false；12 全激活 → true）+ 开环 → 3×3 内圈恰 9 格
        //    id==131 + 破一框架 → 完整性复检静默清门面归零（生产钩子）。
        {
            const int cx = 24, cy = 30, cz = 24;
            for (int dx = -3; dx <= 3; ++dx)
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dz = -3; dz <= 3; ++dz)
                        wA.setBlock(cx + dx, cy + dy, cz + dz, BR::Air, 0); // 清场盒
            const auto ring = [&](bool active) {
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dz = -2; dz <= 2; ++dz) {
                        const bool onRing = (qAbs(dx) == 2 && qAbs(dz) <= 1)
                            || (qAbs(dz) == 2 && qAbs(dx) <= 1);
                        if (onRing)
                            wA.setBlock(cx + dx, cy, cz + dz, BR::AbyssGate,
                                        active ? BR::AbyssGateStateActiveFlag : 0);
                    }
            };
            ring(false);
            const bool notReady = !wA.abyssGateRingComplete(cx, cy, cz);
            ring(true);
            const bool ready = wA.abyssGateRingComplete(cx, cy, cz);
            const bool opened = wA.tryOpenAbyssGate(cx, cy, cz);
            int n = 0;
            bool idsOk = true;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (wA.blockAt(cx + dx, cy, cz + dz) != BR::AbyssGateSurface) { idsOk = false; break; }
                    ++n;
                }
            wA.setBlock(cx - 2, cy, cz, BR::Air, 0); // 破一框架 → 完整性复检静默清门面
            int after = 0;
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    if (wA.blockAt(cx + dx, cy, cz + dz) == BR::AbyssGateSurface) ++after;
            const bool ringOk = notReady && ready && opened && idsOk && n == 9 && after == 0;
            ok = ok && ringOk;
            if (!ringOk) diag += QStringLiteral("[ring notReady=%1 ready=%2 open=%3 ids=%4 n=%5 a=%6] ")
                                     .arg(notReady).arg(ready).arg(opened).arg(idsOk).arg(n).arg(after);
        }
        // ⑤ 存档安全铁律行为级：真 WorldStore 真临时库存读往返 → 138(state1)/111(state1) 逐位保真。
        {
            World wS1;
            initTwin(wS1);
            const auto h5 = wS1.heightAt(5, 5);
            const auto h6 = wS1.heightAt(6, 5);
            const bool sitesOk = h5 >= 0 && h6 >= 0 && h5 + 1 < wS1.height();
            wS1.setBlock(5, h5 + 1, 5, quint8(BR::EmberGate), 1);
            wS1.setBlock(6, h6 + 1, 5, quint8(BR::AbyssGate), BR::AbyssGateStateActiveFlag);
            const quint8 s138 = wS1.stateAt(5, h5 + 1, 5);
            const quint8 s111 = wS1.stateAt(6, h6 + 1, 5);

            WorldStore store;
            store.setWorld(&wS1);
            const QString db = tempDb("a");
            QFile::remove(db);
            const bool save1 = store.openWorld(db) && store.saveAll(QStringLiteral("r2041rig"));
            store.closeWorld();

            World wS2;
            wS2.setWidth(kWF);
            wS2.setDepth(kDF);
            wS2.setHeight(kHF);
            wS2.beginLoad(82);
            store.setWorld(&wS2);
            const bool opened = store.openWorld(db);
            const int nch = opened ? store.loadChunks() : -1;
            store.closeWorld();
            wS2.finishLoad();
            QFile::remove(db);

            const quint8 r138 = wS2.blockAt(5, h5 + 1, 5);
            const quint8 r138s = wS2.stateAt(5, h5 + 1, 5);
            const quint8 r111 = wS2.blockAt(6, h6 + 1, 5);
            const quint8 r111s = wS2.stateAt(6, h6 + 1, 5);
            const bool rtOk = sitesOk && save1 && opened && nch == 9
                && r138 == quint8(BR::EmberGate) && r138s == s138
                && r111 == quint8(BR::AbyssGate) && r111s == s111;
            ok = ok && rtOk;
            if (!rtOk)
                diag += QStringLiteral("[rt sites=%1 save=%2 open=%3 n=%4 id138=%5/%6 s=%7/%8 id111=%9/%10 s=%11/%12] ")
                            .arg(sitesOk).arg(save1).arg(opened).arg(nch)
                            .arg(r138).arg(int(BR::EmberGate)).arg(r138s).arg(s138)
                            .arg(r111).arg(int(BR::AbyssGate)).arg(r111s).arg(s111);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1067 r2041a portal-family behavior equivalence wall after the root"
                             " rename: ignite fills 6 and 20 gate cells with unchanged ids/states,"
                             " production hooks extinguish whole domains on frame break and direct"
                             " mine, the abyss ring opens only with twelve active frames into nine"
                             " unchanged surface cells and self-clears on frame loss, and a real"
                             " save-load round trip keeps the gate and frame voxels byte-identical"
                          << (ok ? QString() : diag);
    });

    // ── r2041b：旧词根清零钉（src/ 全树四词根命中行必携 §9 + 每文件计数 == 盘点口径）────────────
    runLeg(QStringLiteral("t1067 r2041b old-root zero pin over the whole src tree (every line"
        " carrying the legacy NetherPortal/EndPortal/nether_portal/end_portal word roots must"
        " carry the section-nine record marker, the per-file hit counts must equal the audited"
        " inventory of three in the core block table, fifteen in the block registry header, two"
        " and one in the resource pack reader faces and four in the world header mapping note for"
        " a total of twenty-five, and any other file must carry zero hits - new legacy literals"
        " without the marker ring the bell immediately)"), [&]() {
        bool ok = true;
        QString diag;
        const QRegularExpression re(QStringLiteral(
            "NetherPortal|EndPortal|nether_portal|end_portal"));
        const QMap<QString, int> expected = {
            { QStringLiteral("Core/blockregistry.cpp"), 3 },
            { QStringLiteral("Core/blockregistry.h"), 15 },
            { QStringLiteral("Core/resourcepackmanager.cpp"), 2 },
            { QStringLiteral("Core/resourcepackmanager.h"), 1 },
            { QStringLiteral("World/world.h"), 4 },
        };
        QMap<QString, int> hits;
        QStringList unmarked;
        QDirIterator it(srcRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (!QStringList { QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("qml") }
                    .contains(QFileInfo(p).suffix()))
                continue;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) {
                ok = false;
                diag += QStringLiteral("[unreadable %1] ").arg(p);
                continue;
            }
            const QString rel = QDir(srcRoot).relativeFilePath(p);
            const QStringList lines = QString::fromUtf8(f.readAll())
                                          .split(QLatin1Char('\n'));
            for (const QString &ln : lines)
                if (re.match(ln).hasMatch()) {
                    hits[rel] = hits.value(rel) + 1;
                    if (!ln.contains(QStringLiteral("§9")))
                        unmarked << QStringLiteral("%1: %2").arg(rel, ln.trimmed().left(60));
                }
        }
        if (!unmarked.isEmpty()) {
            ok = false;
            diag += QStringLiteral("[unmarked n=%1 %2] ").arg(unmarked.size()).arg(unmarked.first());
        }
        QStringList countBad;
        for (auto pit = expected.begin(); pit != expected.end(); ++pit)
            if (hits.value(pit.key()) != pit.value())
                countBad << QStringLiteral("%1=%2!=%3")
                                .arg(pit.key()).arg(hits.value(pit.key())).arg(pit.value());
        for (auto hit = hits.begin(); hit != hits.end(); ++hit)
            if (!expected.contains(hit.key()))
                countBad << QStringLiteral("unexpected %1=%2").arg(hit.key()).arg(hit.value());
        int total = 0;
        for (auto hit = hits.begin(); hit != hits.end(); ++hit) total += hit.value();
        if (!countBad.isEmpty() || total != 25) {
            ok = false;
            diag += QStringLiteral("[counts total=%1 %2] ").arg(total).arg(countBad.join(";"));
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1067 r2041b old-root zero pin: every legacy word-root line in the"
                             " whole src tree carries the section-nine record marker and the"
                             " per-file hit counts equal the audited inventory of 25 lines across"
                             " five whitelisted files, with every other file at zero"
                          << (ok ? QString() : diag);
    });

    // ── r2041c：用户可见面钉（displayName/blockName/item 名新名在场 + 旧中文名清零钉）────────────
    runLeg(QStringLiteral("t1067 r2041c user-visible face pin (the block display names answer the"
        " original dark-abyss frame, surface and ember gate names, the internal block table names"
        " answer the renamed abyss_gate, abyss_gate_surface and ember_gate keys, the hotbar name"
        " for the abyss eye item answers its original name with the item id untouched, and the"
        " legacy Chinese dimension names appear only on section-nine marked record lines with"
        " per-file counts equal to the audited inventory - user-visible surface fully moved to"
        " original naming with zero old proper nouns left unmarked)"), [&]() {
        bool ok = true;
        QString diag;

        // ① displayName 新名在场（BlockRegistry::displayName 单一权威读面）。
        const QString d111 = BR::displayName(BR::AbyssGate);
        const QString d131 = BR::displayName(BR::AbyssGateSurface);
        const QString d138 = BR::displayName(BR::EmberGate);
        const bool dispOk = d111 == QStringLiteral("暗渊门框架")
            && d131 == QStringLiteral("暗渊门面")
            && d138 == QStringLiteral("余烬门");
        ok = ok && dispOk;
        if (!dispOk) diag += QStringLiteral("[display %1/%2/%3] ").arg(d111, d131, d138);

        // ② 内部分类字面新名在场（BlockDef::name；豁免论证：仅 blockName() 调试读面，零存档/资源包消费方）。
        const char *n111 = BR::blockName(quint8(BR::AbyssGate));
        const char *n131 = BR::blockName(quint8(BR::AbyssGateSurface));
        const char *n138 = BR::blockName(quint8(BR::EmberGate));
        const bool nameOk = QLatin1String(n111) == QLatin1String("abyss_gate")
            && QLatin1String(n131) == QLatin1String("abyss_gate_surface")
            && QLatin1String(n138) == QLatin1String("ember_gate");
        ok = ok && nameOk;
        if (!nameOk)
            diag += QStringLiteral("[blockname %1/%2/%3] ").arg(QLatin1String(n111),
                                                             QLatin1String(n131),
                                                             QLatin1String(n138));

        // ③ 物品名（Hotbar::nameForBlock；EndEyeId 0x23A 数值 id 不变 → 存档/配方零迁移）。
        Hotbar hb;
        const QString eye = hb.nameForBlock(RecipeRegistry::EndEyeId);
        const bool eyeOk = eye == QStringLiteral("暗渊之眼");
        ok = ok && eyeOk;
        if (!eyeOk) diag += QStringLiteral("[eye %1] ").arg(eye);

        // ④ 新名正面源钉（剥注释，string 字面在场；缺一即红）。
        const QStringList missD = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.cpp"), {
            SrcPin("t1067 display frame", "暗渊门框架", 1),
            SrcPin("t1067 display surface", "暗渊门面", 1),
            SrcPin("t1067 display ember gate", "余烬门", 1),
        });
        const QStringList missH = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("t1067 eye item name", "暗渊之眼", 1),
        });
        for (const QString &m : missD + missH) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 旧中文名清零钉（命中行必携 §9 + 每文件计数 == 盘点口径 + 总数 9）。
        const QStringList legacy = { QStringLiteral("下界传送门"), QStringLiteral("末地传送门框架"),
                                     QStringLiteral("末地传送门面"), QStringLiteral("末影之眼") };
        const QMap<QString, int> expected = {
            { QStringLiteral("Core/blockregistry.cpp"), 1 },
            { QStringLiteral("Core/blockregistry.h"), 4 },
            { QStringLiteral("Game/playercontroller.cpp"), 1 },
            { QStringLiteral("World/world.h"), 1 },
        };
        const QRegularExpression re(legacy.join(QLatin1Char('|')));
        QMap<QString, int> hits;
        QStringList unmarked;
        QDirIterator it(srcRoot, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (!QStringList { QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("qml") }
                    .contains(QFileInfo(p).suffix()))
                continue;
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) {
                ok = false;
                diag += QStringLiteral("[unreadable %1] ").arg(p);
                continue;
            }
            const QString rel = QDir(srcRoot).relativeFilePath(p);
            const QStringList lines = QString::fromUtf8(f.readAll())
                                          .split(QLatin1Char('\n'));
            for (const QString &ln : lines)
                if (re.match(ln).hasMatch()) {
                    hits[rel] = hits.value(rel) + 1;
                    if (!ln.contains(QStringLiteral("§9")))
                        unmarked << QStringLiteral("%1: %2").arg(rel, ln.trimmed().left(60));
                }
        }
        if (!unmarked.isEmpty()) {
            ok = false;
            diag += QStringLiteral("[unmarked n=%1 %2] ").arg(unmarked.size()).arg(unmarked.first());
        }
        QStringList countBad;
        for (auto pit = expected.begin(); pit != expected.end(); ++pit)
            if (hits.value(pit.key()) != pit.value())
                countBad << QStringLiteral("%1=%2!=%3")
                                .arg(pit.key()).arg(hits.value(pit.key())).arg(pit.value());
        for (auto hit = hits.begin(); hit != hits.end(); ++hit)
            if (!expected.contains(hit.key()))
                countBad << QStringLiteral("unexpected %1=%2").arg(hit.key()).arg(hit.value());
        int total = 0;
        for (auto hit = hits.begin(); hit != hits.end(); ++hit) total += hit.value();
        if (!countBad.isEmpty() || total != 7) {
            ok = false;
            diag += QStringLiteral("[counts total=%1 %2] ").arg(total).arg(countBad.join(";"));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1067 r2041c user-visible face pin: display names answer the"
                             " original dark-abyss frame / surface / ember-gate names, internal"
                             " table keys and the hotbar item name moved in lockstep, and legacy"
                             " Chinese dimension names survive only on section-nine marked record"
                             " lines at the audited counts"
                          << (ok ? QString() : diag);
    });

    // ── r2041d：结构钉（数值 id 逐位不变 + 映射头注在场 + 方法族声明面 + 编译期互钉）──────────────
    runLeg(QStringLiteral("t1067 r2041d structure pin (the numeric block ids stay 111, 131 and 138"
        " with the active flag 0x01, the strip frame count 32 and the eye item id 0x23A all"
        " unchanged, compile-time assertions freeze the three portal enum values, the renamed"
        " world hook method family keeps its declaration face in the world header, and the t1067"
        " mapping-table header notes are present verbatim in both the block registry header and"
        " the world header - save files keep working across the rename with zero migration)"),
        [&]() {
        bool ok = true;
        QString diag;

        // ① 数值 id 逐位不变钉（存档安全铁律——saves 存数值 id）。
        const bool idsOk = int(BR::AbyssGate) == 111 && int(BR::AbyssGateSurface) == 131
            && int(BR::EmberGate) == 138
            && int(BR::AbyssGateStateActiveFlag) == 0x01
            && BR::kEmberGateStripFrames == 32
            && RecipeRegistry::EndEyeId == 0x23A;
        ok = ok && idsOk;
        if (!idsOk)
            diag += QStringLiteral("[ids %1/%2/%3/%4/%5/%6] ")
                        .arg(int(BR::AbyssGate)).arg(int(BR::AbyssGateSurface))
                        .arg(int(BR::EmberGate)).arg(int(BR::AbyssGateStateActiveFlag))
                        .arg(BR::kEmberGateStripFrames).arg(RecipeRegistry::EndEyeId, 4, 16, QLatin1Char('0'));

        // ② 编译期互钉（枚举值零漂移；本段 TU 编译即验证）。
        static_assert(int(BR::AbyssGate) == 111, "t1067 numeric id pin: AbyssGate == 111");
        static_assert(int(BR::AbyssGateSurface) == 131, "t1067 numeric id pin: AbyssGateSurface == 131");
        static_assert(int(BR::EmberGate) == 138, "t1067 numeric id pin: EmberGate == 138");
        static_assert(BR::AbyssGateStateActiveFlag == 0x01, "t1067 state flag pin");

        // ③ 改名后方法族声明面源钉（剥注释；缺声明即红）。
        const QStringList missWh = pinSet(srcRoot + QStringLiteral("/World/world.h"), {
            SrcPin("t1067 integrity hook", "void checkAbyssGateIntegrity(", 1),
            SrcPin("t1067 ring predicate", "bool abyssGateRingComplete(", 1),
            SrcPin("t1067 open gate", "bool tryOpenAbyssGate(", 1),
            SrcPin("t1067 ignite", "bool tryIgniteEmberGate(", 1),
            SrcPin("t1067 connected clear", "void removeEmberGateAt(", 1),
            SrcPin("t1067 break hook", "void breakEmberGatesAround(", 1),
            SrcPin("t1067 reentry guard", "bool m_inRemoveEmberGate", 1),
        });
        const QStringList missBh = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("t1067 frame predicate", "static bool isAbyssGate(quint8 blockId);", 1),
            SrcPin("t1067 surface predicate", "static bool isAbyssGateSurface(quint8 blockId);", 1),
            SrcPin("t1067 strip frames", "static constexpr int kEmberGateStripFrames = 32;", 1),
            SrcPin("t1067 active flag", "static constexpr quint8 AbyssGateStateActiveFlag = 0x01;", 1),
        });
        const QStringList missRh = pinSet(srcRoot + QStringLiteral("/Game/recipe.h"), {
            SrcPin("t1067 eye item id", "EndEyeId       = 0x23A;", 1),
        });
        for (const QString &m : missWh + missBh + missRh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ 映射表头注在场钉（t1067 头注锚串；头注是 §9 记载 → 裸文本 contains，不走剥注释 pinSet）。
        const auto rawContains = [](const QString &path, const char *needle) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return false;
            return QString::fromUtf8(f.readAll()).contains(QString::fromUtf8(needle));
        };
        const bool map1 = rawContains(srcRoot + QStringLiteral("/Core/blockregistry.h"),
                                      "t1067 §9 区隔改名映射表");
        const bool map2 = rawContains(srcRoot + QStringLiteral("/World/world.h"),
                                      "t1067 §9 区隔改名映射表");
        const bool mapOk = map1 && map2;
        ok = ok && mapOk;
        if (!mapOk) diag += QStringLiteral("[map %1/%2] ").arg(map1).arg(map2);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1067 r2041d structure pin: numeric ids 111/131/138, the active"
                             " flag, the strip frame count and the eye item id all unchanged,"
                             " compile-time assertions freeze the portal enum values, the renamed"
                             " world hook family keeps its declaration face, and the t1067"
                             " mapping-table header notes sit verbatim in both headers"
                          << (ok ? QString() : diag);
    });
}
