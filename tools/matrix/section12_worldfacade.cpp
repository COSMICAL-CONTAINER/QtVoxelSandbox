#include "matrix_helpers.h"

#include "worldfacade.h" // R20.08 被测：WorldFacade（World 收窄查询/写入视图）
#include "gamesession.h" // r2008d 回归腿：迁移后 GameSession 命令面端到端

// R20.08 WorldFacade 探针段（4 腿 r2008a-d；filter 词 "r2008"；矩阵 556→560，band 558±2 内）。
// 置尾先例沿用（接 section11，runAll 末执行）；本段自建 48×48×96 seed 82 fresh 小世界
//（section06 t973 同款 incantation）×4（wQ 查询等价 / wA+wB 写入等价双生 / wF mesher 门 +
// 会话回归共享），对 rig 世界 w 零接触。任务契约（docs/refactor-plan-2026-09-08.md §29.3
// R20.08 原文四验收）：
//   「World 规则使用统一查询 / 收窄面与直取等价」→ r2008a（facade 查询 vs World 同名方法
//     逐位一致：block/state/光照/heightmap/地表/群系/天气/谓词 × 抽样网格 + OOB 语义）；
//   「新代码不再直接获取 Chunk 内部指针（写入面收口示范）」→ r2008b（同一编辑序列 A=直调
//     World::setBlock 权威 vs B=Facade 包装：全栅格 id+state 逐位一致 + 返回值逐笔一致 +
//     三信号计数一致）；r2008c 结构钉（worldfacade.h 委托本体 + gamesession.h/chunkgeometry
//     消费面「Chunk* 不回流」阴性钉）+ mesher 脏门端到端（Facade 门查询驱动重建/跳过）；
//   「Renderer 不需要持有可变 World」→ r2008c 行为腿实证渲染面只读消费仍工作（ChunkGeometry
//     经 Facade 门查询即时重建，本批渲染行为零变化——只读形态盘点登记于 docs 关单）；
//   「旧 World 暂时仍可作为 Implementation」→ 全段前提：Facade 零逻辑复制、World 类零改动
//     （r2007a 旧路径腿继续全绿即 World 未被替换的常驻证据）。
// 阴性轮（按性价比登记）：r2008c/r2008d 源码钉承担「摘收口即红」——chunkgeometry 出现
//   myChunk()/chunks().chunk()/Chunk* 即红、gamesession.h 出现 m_world.setBlock 即红
//   （禁出钉走 minCount=1 反探：pinSet 的 cnt<minCount 语义下 0 计数钉恒不红——首版误用
//   空转钉的教训登记；反探复用 pinSet 剥注释器防注释误伤，r2007b 手工 contains 同型先例；
//   命令旁路另有 m_facade.setBlock ×2 正面钉兜底）。**变异构建式豁免登记**：facade 转发
//   短路（如 blockAt 改返常量）属纯编译期行为，常驻等价腿 r2008a 必红（r2007a 事件面连带
//   红）——阴性轮以 matrix_r2008_neg.log 实跑存证。
// 确定性口径：同 section11——fresh 世界同 seed 同尺寸（worldgen 纯函数）+ 天气双钉
//   setWeatherState(0)+setWeatherRemainingSec(3600)（tickWeather 转换掷骰不进探针窗口）。
void MatrixRun::section12_worldfacade()
{
    // ── 共享 rig：fresh 小世界构造 + 确定性天气钉（section11 initTwin 同款）────────────
    const auto initTwin = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 放置列选址：heightAt 顶 +1 恒空的第一列（确定性扫描；同 section11 placeCol）。
    const auto placeCol = [](World &w, int x0, int z0) -> QPair<int, int> {
        for (int dz = 0; dz < 8; ++dz) {
            const int z = z0 + dz;
            const int h = w.heightAt(x0, z);
            if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h + 1, z) == 0)
                return QPair<int, int>(x0, z);
        }
        qFatal("r2008 rig: no placeable column near (%d,%d)", x0, z0);
        return QPair<int, int>(-1, -1);
    };

    World wQ; // r2008a 查询等价承载
    initTwin(wQ);
    World wA; // r2008b 双生 A（直调 World 路径）
    initTwin(wA);
    World wB; // r2008b 双生 B（Facade 路径）
    initTwin(wB);
    World wF; // r2008c/d 共享腿世界（mesher 门 + 会话回归）
    initTwin(wF);

    // ── r2008a：查询面等价——Facade 逐方法 vs World 同名权威（抽样网格 + OOB 语义）─────
    runLeg(QStringLiteral("r2008a query-face equivalence WorldFacade vs World authority (R20.08"
        " WorldFacade): over a fresh 48x48x96 s82 world the narrowed const face answers bit-"
        "identical to the World methods it wraps - a full 48x48 column sweep (stride 1)"
        " compares blockAt and stateAt at 6 depths per column (~13.8k cells) plus"
        " isSolidAt/isCollidableAt/isFullCubeAt per cell and skyLightAt/blockLightAt per"
        " cell, and heightmapAt/heightAt/biomeIdAt/weatherStateAt/isPrecipitatingAt per"
        " column, all equal; out-of-bounds"
        " semantics match the authority exactly (x/z beyond the grid read Air via both faces,"
        " y above the world reads skyLight 15 via both, below reads 0), and the facade exposes"
        " no chunk internals (typed C++ surface only - no Chunk*/ChunkManager accessor exists"
        " on it by construction)"), [&]() {
        bool ok = true;
        QString diag;

        WorldFacade f(wQ);
        long cells = 0, cols = 0;

        // 全网格列扫（stride 1 → 48×48 列）× 深度采样（顶带 + 半空 + 底）：
        for (int x = 0; x < wQ.width() && ok; x += 1) {
            for (int z = 0; z < wQ.depth() && ok; z += 1) {
                // 列级查询族：
                const bool colOk = f.heightmapAt(x, z) == wQ.heightmapAt(x, z)
                    && f.heightAt(x, z) == wQ.heightAt(x, z)
                    && f.biomeIdAt(x, z) == wQ.biomeIdAt(x, z)
                    && f.weatherStateAt(x, z) == wQ.weatherStateAt(x, z)
                    && f.isPrecipitatingAt(x, z) == wQ.isPrecipitatingAt(x, z);
                if (!colOk) {
                    ok = false;
                    diag += QStringLiteral("[col(%1,%2)] ").arg(x).arg(z);
                    break;
                }
                ++cols;
                // 深度采样：底部 / 1/3 / 半空 / 顶带三格（h-1,h,h+1 夹取界内）。
                const int h = f.heightAt(x, z);
                const int ys[6] = { 0, wQ.height() / 3, wQ.height() / 2,
                    qBound(0, h - 1, wQ.height() - 1),
                    qBound(0, h, wQ.height() - 1),
                    qBound(0, h + 1, wQ.height() - 1) };
                for (int y : ys) {
                    const BlockPos p{ x, y, z };
                    const bool cellOk = f.blockAt(p) == wQ.blockAt(x, y, z)
                        && f.blockAt(x, y, z) == wQ.blockAt(x, y, z) // int 重载同面
                        && f.stateAt(p) == wQ.stateAt(x, y, z)
                        && f.isSolidAt(p) == wQ.isSolid(x, y, z)
                        && f.isCollidableAt(p) == wQ.isCollidable(x, y, z)
                        && f.isFullCubeAt(p) == wQ.isFullCubeAt(x, y, z)
                        && f.skyLightAt(x, y, z) == wQ.skyLightAt(x, y, z)
                        && f.blockLightAt(x, y, z) == wQ.blockLightAt(x, y, z);
                    if (!cellOk) {
                        ok = false;
                        diag += QStringLiteral("[cell(%1,%2,%3)] ").arg(x).arg(y).arg(z);
                        break;
                    }
                    ++cells;
                }
            }
        }
        const bool sampleOk = ok && cells >= 9000 && cols >= 2000; // 非空扫（等价性有样本量承重；2304 列 ×6 = 13824 格）
        ok = ok && sampleOk;
        if (!sampleOk)
            diag += QStringLiteral("[samples cells=%1 cols=%2] ").arg(cells).arg(cols);

        // OOB 语义与权威逐位一致（Facade 不自带边界规则——规则权威唯一在 World）：
        struct Oob { int x, y, z; };
        const Oob oobs[] = { { -1, 5, -1 }, { 48, 5, 48 }, { -100, 200, 100 },
            { 10, wQ.height() + 3, 10 }, { 10, -1, 10 } };
        bool oobOk = true;
        for (const Oob &o : oobs) {
            oobOk = oobOk && f.blockAt(o.x, o.y, o.z) == wQ.blockAt(o.x, o.y, o.z)
                && f.skyLightAt(o.x, o.y, o.z) == wQ.skyLightAt(o.x, o.y, o.z)
                && f.stateAt(o.x, o.y, o.z) == wQ.stateAt(o.x, o.y, o.z);
        }
        // y 超顶 = 开阔天空（权威语义 15）双面一致；x/z 越界 blockAt = Air 双面一致。
        oobOk = oobOk && f.skyLightAt(10, wQ.height() + 3, 10) == 15
            && f.blockAt(-1, 5, -1) == 0;
        ok = ok && oobOk;
        if (!oobOk) diag += QStringLiteral("[oob] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2008a query-face equivalence: the narrowed const face answers"
                             " bit-identical to World authority across a full 48x48 column"
                             " sweep (block/state/light/predicates per cell ~13.8k,"
                             " heightmap/height/biome/weather per column), out-of-bounds"
                             " semantics match exactly (Air beyond grid, skyLight 15 above"
                             " world), and the facade exposes no chunk internals by"
                             " construction"
                          << (ok ? QString() : diag);
    });

    // ── r2008b：写入面等价——同一编辑序列 直调 World vs 经 Facade（栅格逐位 + 返回值 + 信号）─
    runLeg(QStringLiteral("r2008b write-face equivalence facade wraps the single setBlock"
        " authority (R20.08 WorldFacade): twin 48x48x96 s82 worlds run an identical edit"
        " sequence - path A calls World::setBlock directly, path B calls the same writes"
        " through WorldFacade (place Stone above a column, break a surface top, write"
        " Farmland with hydration state 3 via the id+state entry, a no-op rewrite rejected,"
        " an out-of-bounds write rejected) - and per-step return values match, the full"
        " id+state grids stay bit-equal afterwards, and the blockBroken/blockPlaced/"
        " worldChanged signal counters are equal on both worlds (the facade is pure"
        " delegation: same authority, same post-write hooks, same semantic events, zero"
        " logic copy)"), [&]() {
        bool ok = true;
        QString diag;

        // 双生基线恒等（worldgen 纯函数）：
        QString why;
        const auto gridEqual = [&](World &a, World &b) {
            for (int x = 0; x < a.width(); ++x)
                for (int z = 0; z < a.depth(); ++z)
                    for (int y = 0; y < a.height(); ++y) {
                        if (a.blockAt(x, y, z) != b.blockAt(x, y, z)
                            || a.stateAt(x, y, z) != b.stateAt(x, y, z)) {
                            why = QStringLiteral("delta@(%1,%2,%3) %4/%5 vs %6/%7")
                                      .arg(x).arg(y).arg(z)
                                      .arg(a.blockAt(x, y, z)).arg(a.stateAt(x, y, z))
                                      .arg(b.blockAt(x, y, z)).arg(b.stateAt(x, y, z));
                            return false;
                        }
                    }
            return true;
        };
        const bool baseOk = gridEqual(wA, wB);
        ok = ok && baseOk;
        if (!baseOk) diag += QStringLiteral("[twin-base %1] ").arg(why);

        // 信号计数（init 后连接——生成期 worldChanged 不计入；只比对编辑面）：
        int aBroken = 0, aPlaced = 0, aChanged = 0, bBroken = 0, bPlaced = 0, bChanged = 0;
        QObject::connect(&wA, &World::blockBroken, &wA, [&](int, int, int, int) { ++aBroken; });
        QObject::connect(&wA, &World::blockPlaced, &wA, [&](int, int, int, int) { ++aPlaced; });
        QObject::connect(&wA, &World::worldChanged, &wA, [&]() { ++aChanged; });
        QObject::connect(&wB, &World::blockBroken, &wB, [&](int, int, int, int) { ++bBroken; });
        QObject::connect(&wB, &World::blockPlaced, &wB, [&](int, int, int, int) { ++bPlaced; });
        QObject::connect(&wB, &World::worldChanged, &wB, [&]() { ++bChanged; });

        // 选址（双生同结果；A 算 B 复用同一坐标）：
        const int hbA = wA.heightAt(10, 10);
        const QPair<int, int> pp = placeCol(wA, 20, 20);
        const int hpA = wA.heightAt(pp.first, pp.second);
        const QPair<int, int> pf = placeCol(wA, 30, 30);
        const int hfA = wA.heightAt(pf.first, pf.second);
        const bool sitesOk = hbA >= 0 && hpA >= 0 && hfA >= 0 && pp.first >= 0 && pf.first >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");

        if (ok) {
            WorldFacade fB(wB); // 路径 B 的收窄面
            // 同一编辑序列（逐笔断言返回值一致）：
            const bool s1 = wA.setBlock(pp.first, hpA + 1, pp.second, quint8(BR::Stone))
                == fB.setBlock(BlockPos{ pp.first, hpA + 1, pp.second }, quint8(BR::Stone));
            const bool s2 = wA.setBlock(10, hbA, 10, quint8(BR::Air))
                == fB.setBlock(BlockPos{ 10, hbA, 10 }, quint8(BR::Air));
            // id+state 写入口（Farmland 湿度 state=3——两路同走 5 参权威）：
            const bool s3 = wA.setBlock(pf.first, hfA + 1, pf.second, quint8(BR::Farmland), 3)
                == fB.setBlockWithState(BlockPos{ pf.first, hfA + 1, pf.second },
                       quint8(BR::Farmland), 3);
            // no-op 重写（同 id 同 state → 权威拒 false）：
            const bool s4 = wA.setBlock(pf.first, hfA + 1, pf.second, quint8(BR::Farmland), 3)
                == fB.setBlockWithState(BlockPos{ pf.first, hfA + 1, pf.second },
                       quint8(BR::Farmland), 3)
                && !fB.setBlockWithState(BlockPos{ pf.first, hfA + 1, pf.second },
                       quint8(BR::Farmland), 3);
            // OOB 写（双面恒拒）：
            const bool s5 = !wA.setBlock(-1, 5, -1, quint8(BR::Stone))
                && !fB.setBlock(BlockPos{ -1, 5, -1 }, quint8(BR::Stone));
            const bool seqOk = s1 && s2 && s3 && s4 && s5;
            ok = ok && seqOk;
            if (!seqOk) diag += QStringLiteral("[seq s1=%1 s2=%2 s3=%3 s4=%4 s5=%5] ")
                                    .arg(s1).arg(s2).arg(s3).arg(s4).arg(s5);

            // 关键格核验（B 面回读 = 权威栅格）：
            const bool cellsOk
                = fB.blockAt(pp.first, hpA + 1, pp.second) == quint8(BR::Stone)
                && fB.blockAt(10, hbA, 10) == quint8(BR::Air)
                && fB.stateAt(pf.first, hfA + 1, pf.second) == 3
                && wB.stateAt(pf.first, hfA + 1, pf.second) == 3;
            ok = ok && cellsOk;
            if (!cellsOk) diag += QStringLiteral("[cells] ");

            // 全栅格逐位（id+state）：
            why.clear();
            const bool gridOk = gridEqual(wA, wB);
            ok = ok && gridOk;
            if (!gridOk) diag += QStringLiteral("[grid %1] ").arg(why);

            // 三信号计数一致（同权威 → 同事件面）：
            const bool sigOk = aBroken == bBroken && aPlaced == bPlaced && aChanged == bChanged
                && aBroken >= 1 && aPlaced >= 2; // 序列确有破/放（非空扫）
            ok = ok && sigOk;
            if (!sigOk)
                diag += QStringLiteral("[sig broken %1/%2 placed %3/%4 changed %5/%6] ")
                            .arg(aBroken).arg(bBroken).arg(aPlaced).arg(bPlaced)
                            .arg(aChanged).arg(bChanged);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2008b write-face equivalence: identical edit sequence via"
                             " direct World::setBlock vs WorldFacade lands bit-equal grids"
                             " (full id+state), per-step accepts/rejects match (place, break,"
                             " id+state write, no-op reject, OOB reject), and the broken/"
                             " placed/worldChanged counters are equal - pure delegation to"
                             " the single write authority"
                          << (ok ? QString() : diag);
    });

    // ── r2008c：结构钉（委托本体 / Chunk* 不回流 / 命令零旁路）+ mesher 脏门端到端 ──────
    runLeg(QStringLiteral("r2008c structure pins + mesher dirty-gate end-to-end (R20.08"
        " WorldFacade): comment-stripped source pins hold the facade as pure delegation"
        " (four m_world->setBlock forwards, light/height/biome/weather forwarders, chunk"
        " gate queries via chunks().chunk) with no logic copy, hold the migrated consumers"
        " Chunk free - chunkgeometry.cpp/h contain no myChunk / chunks().chunk / Chunk*"
        " (the render-side raw chunk fetch is gone and must not flow back), gamesession.h"
        " keeps zero m_world.setBlock bypass (commands go through m_facade.setBlock x2;"
        " bypassing the narrowed face goes red) - and behaviorally the migrated mesher"
        " gate still rebuilds immediately on an in-chunk edit (vertex grows when TallGrass"
        " lands) and still skips untouched chunks (an out-of-chunk edit leaves the vertex"
        " count unchanged: the facade dirty query is chunk-scoped, rendering behavior"
        " unchanged)"), [&]() {
        bool ok = true;
        QString diag;

        // 源码钉根（exe 相对 src/——r2007b 同款解析）：
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // ① worldfacade.h 委托本体钉（纯转发、零逻辑复制——收窄面的权威接线）：
        const QStringList missF = pinSet(srcRoot + QStringLiteral("/World/worldfacade.h"), {
            SrcPin("r2008 facade class present", "class WorldFacade", 1),
            SrcPin("r2008 write delegation x4 (BlockPos/int x id, id+state x2)",
                "m_world->setBlock(", 4),
            SrcPin("r2008 query delegation: blockAt", "m_world->blockAt(", 2),
            SrcPin("r2008 query delegation: light pair", "m_world->skyLightAt(", 1),
            SrcPin("r2008 query delegation: blockLight", "m_world->blockLightAt(", 1),
            SrcPin("r2008 query delegation: heightmap", "m_world->heightmapAt(", 1),
            SrcPin("r2008 query delegation: biome", "m_world->biomeIdAt(", 1),
            SrcPin("r2008 query delegation: weather", "m_world->weatherStateAt(", 1),
            SrcPin("r2008 query delegation: rule predicate isCollidable", "m_world->isCollidable(", 1),
            SrcPin("r2008 query delegation: rule predicate isFullCube", "m_world->isFullCubeAt(", 1),
            SrcPin("r2008 chunk gate queries route via the World-layer bridge",
                "chunks().chunk(", 3),
        });
        for (const QString &m : missF) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ② 消费面「Chunk* 不回流」阳性钉（迁移点新门在位）+ 阴性禁出钉走「反探」惯用法：
        //    pinSet 语义 cnt<minCount 才红 → minCount=0 恒不红（空转钉——首版误用 0 计数
        //    SrcPin，阴性轮红因仅正面钉实证，本版登记教训）；禁出串检查 = minCount=1 反探
        //    （miss 非空 = 零命中 = 合规；复用 pinSet 剥注释器——裸 contains 会被注释里提及的
        //    myChunk 误伤，r2007b 手工 contains 同型先例）。
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };
        const QString cgPath = srcRoot + QStringLiteral("/World/chunkgeometry.cpp");
        const QString cghPath = srcRoot + QStringLiteral("/World/chunkgeometry.h");
        const QStringList missCg = pinSet(cgPath, {
            SrcPin("r2008 mesher dirty gate via facade", "chunkDirty(m_cx, m_cz)", 2),
            SrcPin("r2008 mesher fluid-only gate via facade", "chunkFluidOnlyDirty(m_cx, m_cz)", 1),
            SrcPin("r2008 mesher existence gate via facade", "chunkExists(m_cx, m_cz)", 1),
        });
        const QStringList missCgh = pinSet(cghPath, {
            SrcPin("r2008 gate helpers delegate to WorldFacade", "WorldFacade(*m_world)", 3),
        });
        for (const QString &m : missCg + missCgh) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool chunkNegOk = forbiddenAbsent(cgPath, "myChunk")
            && forbiddenAbsent(cgPath, "chunks().chunk")
            && forbiddenAbsent(cgPath, "Chunk *")
            && forbiddenAbsent(cghPath, "Chunk *");
        ok = ok && chunkNegOk;
        if (!chunkNegOk) diag += QStringLiteral("[chunk-flowback] ");

        // ③ 命令零旁路钉（GameSession 经 Facade；r2007b 委托钉的收口面续行）：R20.09 起写
        //    面双入口——setBlock（破）+ setBlockWithState（放，Review #3① state 落地）；
        //    前缀式计数钉双入口 ≥2，setBlockWithState 入口单独钉防退化回 4 参丢 state。
        const QString gsPath = srcRoot + QStringLiteral("/Game/gamesession.h");
        const QStringList missGs = pinSet(gsPath, {
            SrcPin("r2008 command writes go through the facade", "m_facade.setBlock", 2),
            SrcPin("r2008 place write lands id+state via facade", "m_facade.setBlockWithState(", 1),
            SrcPin("r2008 edit after-read goes through the facade", "m_facade.blockAt(", 1),
        });
        for (const QString &m : missGs) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool bypassOk = forbiddenAbsent(gsPath, "m_world.setBlock");
        ok = ok && bypassOk;
        if (!bypassOk) diag += QStringLiteral("[command-bypass] ");

        // ④ mesher 脏门端到端（Facade 门查询驱动重建/跳过——渲染行为零变化的实证）：
        //    chunk(1,1) 内部柱（x=24,z=24，离 chunk 边 ≥2 免邻接标脏干扰）：放 Stone 建基线 →
        //    放 TallGrass 顶点增长（脏门即时重建）→ chunk(0,0) 编辑顶点不变（非脏跳过）。
        ChunkGeometry geo;
        geo.setWorld(&wF);
        geo.setCx(1);
        geo.setCz(1);
        int ye = -1;
        for (int y = 40; y < wF.height() - 2; ++y) {
            if (wF.blockAt(24, y, 24) == 0 && wF.blockAt(24, y + 1, 24) == 0) {
                ye = y;
                break;
            }
        }
        const bool siteOk = ye > 0;
        ok = ok && siteOk;
        if (!siteOk) diag += QStringLiteral("[mesher-site y=%1] ").arg(ye);
        if (siteOk) {
            wF.setBlock(24, ye, 24, quint8(BR::Stone), 0); // 落石：脏 chunk(1,1) → 同步首建
            const int v0 = geo.vertexCount();
            wF.setBlock(24, ye + 1, 24, quint8(BR::TallGrass), 0); // cross 折叠顶点差分
            const int v1 = geo.vertexCount();
            wF.setBlock(4, ye, 4, quint8(BR::Stone), 0); // chunk(0,0) 编辑：本 chunk 非脏
            const int v2 = geo.vertexCount();            // → 脏门拒重建（Facade 门查询语义）
            const bool gateOk = v0 > 0 && v1 > v0 && v2 == v1;
            ok = ok && gateOk;
            if (!gateOk)
                diag += QStringLiteral("[gate v0=%1 v1=%2 v2=%3] ").arg(v0).arg(v1).arg(v2);
            // 还原（共享世界 wF 清洁——r2008d 复用）：
            wF.setBlock(24, ye + 1, 24, quint8(BR::Air), 0);
            wF.setBlock(24, ye, 24, quint8(BR::Air), 0);
            wF.setBlock(4, ye, 4, quint8(BR::Air), 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2008c structure pins + mesher gate: facade held as pure"
                             " delegation (setBlock x4 + query forwarders + chunk gates),"
                             " migrated consumers Chunk-free (no myChunk/chunks().chunk/"
                             " Chunk* in the mesher, no m_world.setBlock bypass in the"
                             " session), and the migrated dirty gate rebuilds in-chunk"
                             " edits immediately while out-of-chunk edits skip - render"
                             " behavior unchanged"
                          << (ok ? QString() : diag);
    });

    // ── r2008d：阴性常驻 + 迁移后会话回归（命令面端到端仍与 R20.07 行为一致）────────────
    runLeg(QStringLiteral("r2008d negative standing + migrated-session regression (R20.08"
        " WorldFacade): the standing remove-to-red probe - GameSession command execution"
        " must keep flowing through the narrowed face (the r2008c pin already reds on any"
        " m_world.setBlock bypass; this leg holds the behavioral half: a break+place pair"
        " enqueued and pumped in one tick lands exactly as before the migration - cells"
        " updated, BlockChanged events carry the edit positions with after-ids, the delta"
        " covers exactly the two edited chunks); the facade write face rejects out-of-"
        " bounds like the authority (false, grid untouched); and the old direct path"
        " (World::setBlock) stays available and equivalent on a control column - the old"
        " World is still the Implementation behind the facade"), [&]() {
        bool ok = true;
        QString diag;

        GameSession gs(wF);
        // 选址（r2008c 已还原其柱；本腿用 24/40 双柱——cx 1 vs 2 **恒异 chunk**，破/放各占
        //   一个受影响 chunk）。破位另须「表面块非空气」：heightAt 顶是真方块才发 blockBroken
        //   （空操破不发信号 → 事件/delta 计数塌成 1——首版腿缺此过滤 + 双柱同腔 chunk(1,1)
        //   两坑实证，本版双修）。放位沿用 placeCol（h+1 实证空气，真放）。
        const auto breakCol = [](World &w, int x0, int z0) -> QPair<int, int> {
            for (int dz = 0; dz < 8; ++dz) {
                const int z = z0 + dz;
                const int h = w.heightAt(x0, z);
                if (h >= 0 && h + 1 < w.height() && w.blockAt(x0, h, z) != 0)
                    return QPair<int, int>(x0, z);
            }
            return QPair<int, int>(-1, -1);
        };
        const QPair<int, int> p1 = breakCol(wF, 24, 25); // chunk(1,1)（z 32 尾列也只到 (1,2)）
        const int h1 = wF.heightAt(p1.first, p1.second);
        const QPair<int, int> p2 = placeCol(wF, 40, 24); // chunk(2,1)——cx 恒异于 p1
        const int h2 = wF.heightAt(p2.first, p2.second);
        const bool sitesOk = h1 >= 0 && h2 >= 0 && p1.first >= 0 && p2.first >= 0;
        ok = ok && sitesOk;
        if (!sitesOk) diag += QStringLiteral("[sites] ");

        if (ok) {
            QVector<WorldDelta> deltas;
            QObject::connect(&gs, &GameSession::tickCompleted, &gs,
                [&](int, const WorldDelta &d) { deltas.push_back(d); });
            const Result<void> r1 = gs.enqueueCommand(
                Command::breakBlock(BlockPos{ p1.first, h1, p1.second }, 7u, 1u, 0));
            const Result<void> r2 = gs.enqueueCommand(Command::placeBlock(
                BlockPos{ p2.first, h2 + 1, p2.second }, quint8(BR::Stone), 7u, 1u, 0));
            const int stepped = gs.stepTick(0.1); // 一 tick：两命令整边界执行（经 Facade 写）

            const bool cmdOk = r1.isOk() && r2.isOk() && stepped == 1 && gs.tick() == 1
                && wF.blockAt(p1.first, h1, p1.second) == quint8(BR::Air)
                && wF.blockAt(p2.first, h2 + 1, p2.second) == quint8(BR::Stone);
            ok = ok && cmdOk;
            if (!cmdOk)
                diag += QStringLiteral("[cmd r1=%1 r2=%2 stepped=%3 afterBreak=%4 afterPlace=%5] ")
                            .arg(r1.isOk()).arg(r2.isOk()).arg(stepped)
                            .arg(wF.blockAt(p1.first, h1, p1.second))
                            .arg(wF.blockAt(p2.first, h2 + 1, p2.second));

            // 事件面（改动后 id 经 Facade 回读——破 Air / 放 Stone）+ delta chunk 集恰两 chunk：
            Event e1{}, e2{};
            const bool pop1 = gs.events().pop(e1);
            const bool pop2 = gs.events().pop(e2);
            const bool evOk = pop1 && pop2
                && gs.events().isEmpty() && gs.droppedEventCount() == 0
                && e1.kind == EventKind::BlockChanged && e1.blockId == quint8(BR::Air)
                && e2.kind == EventKind::BlockChanged && e2.blockId == quint8(BR::Stone);
            ok = ok && evOk;
            if (!evOk)
                diag += QStringLiteral("[events pop1=%1 pop2=%2 empty=%3 dropped=%4"
                                       " e1=%5/%6 e2=%7/%8] ")
                            .arg(pop1).arg(pop2).arg(gs.events().isEmpty())
                            .arg(gs.droppedEventCount())
                            .arg(int(e1.kind)).arg(e1.blockId)
                            .arg(int(e2.kind)).arg(e2.blockId);

            const bool deltaOk = deltas.size() == 1 && deltas[0].affectedCount == 2
                && deltas[0].changedBlocks == 2
                && deltas[0].affects(ChunkKey::fromWorld(p1.first, p1.second, Chunk::kSize))
                && deltas[0].affects(ChunkKey::fromWorld(p2.first, p2.second, Chunk::kSize));
            ok = ok && deltaOk;
            if (!deltaOk)
                diag += QStringLiteral("[delta n=%1 sz=%2 chg=%3 p1c=(%4,%5) p2c=(%6,%7)] ")
                            .arg(deltas.size())
                            .arg(deltas.value(0).affectedCount)
                            .arg(deltas.value(0).changedBlocks)
                            .arg(ChunkKey::fromWorld(p1.first, p1.second, Chunk::kSize).cx)
                            .arg(ChunkKey::fromWorld(p1.first, p1.second, Chunk::kSize).cz)
                            .arg(ChunkKey::fromWorld(p2.first, p2.second, Chunk::kSize).cx)
                            .arg(ChunkKey::fromWorld(p2.first, p2.second, Chunk::kSize).cz);

            // Facade 写面 OOB 拒绝（权威语义）：
            WorldFacade fCtrl(wF);
            const bool oobOk = !fCtrl.setBlock(-5, 5, -5, quint8(BR::Stone))
                && fCtrl.blockAt(-5, 5, -5) == 0;
            ok = ok && oobOk;
            if (!oobOk) diag += QStringLiteral("[oob] ");

            // 对照柱：旧直调路径仍可用且同面（旧 World 仍为 Implementation）：
            const QPair<int, int> pc = placeCol(wF, 26, 26);
            const int hc = wF.heightAt(pc.first, pc.second);
            const bool ctrlOk = hc >= 0
                && wF.setBlock(pc.first, hc + 1, pc.second, quint8(BR::Cobble))
                && wF.blockAt(pc.first, hc + 1, pc.second) == quint8(BR::Cobble)
                && fCtrl.blockAt(BlockPos{ pc.first, hc + 1, pc.second })
                    == quint8(BR::Cobble);
            ok = ok && ctrlOk;
            if (!ctrlOk) diag += QStringLiteral("[ctrl] ");

            // 还原（共享世界清洁）：
            wF.setBlock(pc.first, hc + 1, pc.second, quint8(BR::Air), 0);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2008d negative standing + regression: session commands keep"
                             " flowing through the narrowed face with identical end-to-end"
                             " behavior (cells, BlockChanged after-ids, two-chunk delta),"
                             " the facade rejects OOB writes like the authority, and the"
                             " direct World path stays available and equivalent on a"
                             " control column - old World remains the Implementation"
                          << (ok ? QString() : diag);
    });
}
