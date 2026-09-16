#include "matrix_helpers.h"

#include "chunkevictor.h"      // §29.4-P3 被测：ChunkEvictor 驱逐编排值组件（纯缝三件套）
#include "chunkstreamdriver.h" // §29.4-P2/P3：驱动器扩展面（setEvictor + setSavedContentQuery）
#include "generationpolicy.h"  // 测试侧 twin policy：期望 decide 输出的独立计算源（r2016 已钉其本体）
#include "savecoordinator.h"   // R20.15 载体（r2019b：真 SaveCoordinator + fresh 临时库，绝触 saves/）

// §29.4-P3 卸载 + Edits-on-evict 探针段（4 腿 r2019a-d；filter 词 "r2019"；矩阵 612→616，
// band 614±2 内）。置尾先例沿用（接 section21，runAll 末执行）。任务契约
// （docs/refactor-plan-2026-09-08.md §29.4「P3 卸载 + Edits-on-evict」原文：「视距外
// Evicting→Absent，改动块经 SaveCoordinator 落盘、重载回灌（R20.10d 存档往返腿族先例）」+
// D5 决策「Evicting→Absent + Edits-on-evict 落盘」）：
//   「默认关恒惰」延续（P2 承重墙延续）→ r2019a（**本单承重墙**：默认构造 driver[无 evictor/
//     无 savedQuery] + 位置扫掠 → r2018 全语义逐位不动[提交/取消/统计与 P2 形态双生恒等]；
//     streaming off 时 evictor/savedQuery 两缝均不可达[回调点在默认关短路之后]）；
//   「Evicting→Absent + Edits-on-evict 落盘 + 重载回灌」→ r2019b（**本单承重腿**：真
//     SaveCoordinator + fresh 临时库[r2015 先例，绝触 saves/] + 真世界生命周期表 + scratch 脏缝：
//     ①编辑过的半径外 chunk：persist 先于转移[缝记录调用序柱]、经合法边⑥⑦到 Absent[步1
//     ensure-Loaded 自转移守卫拒 = 预期 no-op]、报告 persisted/evicted 计数正确；②重入半径：
//     savedQuery true → Load kind job 提交[非 Generate]、Load 完成后内容回灌逐位恒等[落盘快照
//     ↔ 回灌读面一致——r2010d rebind 教训钉在腿文：store.setWorld 先于 loadChunks，回灌面 =
//     beginLoad 零填充 fresh 世界，非空转结构性保证]；③未编辑半径外 chunk：零 persist 调用、
//     直达 Absent）；
//   「persist 失败 = 中止该 chunk 驱逐」→ r2019c（自校验 QMap 缝[chunklifecycle 合法表守卫同
//     构]：persistFn 注入失败 → 该 chunk aborted、保持原态不转移[零 accepted 转移 + 表不动]，
//     重试语义 = 下一变更沿重报候选[同 evictor 实例复跑收敛——组件零隐状态]；转移缝注入拒绝 →
//     aborted 计数、每候选转移尝试恰 2 次[有界，无重试风暴]；null 缝 fail-safe：缺 dirty 缝/
//     缺转移缝/缺 persist 缝[dirty 候选] → skipped 零调用）；
//   结构钉 + kind 选择 → r2019d（chunkevictor.h：QObjectFree 复述 + 正面钉[三缝/evict 签名/
//     合法边⑥⑦驱动行] + 反探[禁 QObject/QML/线程/World*/ChunkManager*/SaveCoordinator 记号
//     ——落盘经缝、转移经缝终点，组件零直驱] + 零接线；chunkstreamdriver.h：setEvictor/
//     setSavedContentQuery 正面钉 + Mesh 仍禁出 + Load 恰经缝门控[r2018d 同变更修订的承接面] +
//     零接线；null savedQuery → 全 Generate 双生[r2018b twin 复跑恒等] + savedQuery true →
//     kind 翻 Load[键/优先级/轨迹恒等]；null evictor → toEvict 消费面零调用[重放恒等] + 使能
//     evictor → 候选单 ≡ twin decide().toEvict 逐项恒等；确定性[同候选序双 fresh evictor 轨迹
//     逐位恒等]）。
// ── 阴性轮恰红面设计（**先于腿文定稿**，R20.11 收缩纪律；存证 matrix_r2019_neg1_red.log /
//    matrix_r2019_neg1_restore.log / matrix_r2019_neg2_red.log / matrix_r2019_neg2_restore.log
//    直落 build/ 终名，红跑/还原分文件，禁 TEMP 中转）────────────────────────────────────
//   NEG-1 摘「先落盘后转移」序——chunkevictor.h evict() 循环体内把 persist 块整体移到步1
//     ensure-Loaded 转移调用**之后**（= persist 晚于本候选首个转移调用）→ **恰红 r2019b 单腿
//     的调用序柱**（persist 索引 < 首转移索引 失配；报告计数/⑥⑦三重/回灌恒等面全不受扰：
//     r2019b 全候选均 Loaded 稳态，步1 自转移被守卫拒 = 零 accepted 转移零表动 → 回灌面/计数
//     面逐位不红）。r2019c 不红（腿文按「零 accepted 转移 + 表不动」口径断言失败中止面，对
//     「被拒 no-op 调用先于 persist」不敏感——恰红面设计使 fail-abort 柱与调用序柱正交）；
//     r2019a/d 无 persist 序敏感面 → 不红。
//   NEG-2 摘 persist 失败中止——`if (!pr.isOk())` 前置 `false &&`（= 中止分支永不可达，
//     失败仍转移）→ **恰红 r2019c 单腿**（失败中止柱：aborted 计数失配 + 表 A 被移到 Absent
//     = 「保持原态不转移」失配）。r2019b 全程 persist 零失败 → 不红；r2019a/d 无失败注入面
//     → 不红。
// 确定性口径：ChunkEvictor 纯同步编排（候选保序 + 三步驱动全确定）；r2019b 真世界路径的
//   SQLite 落盘/回灌为确定字节面（r2015/r2010d 先例同款）；缝记录全显式（腿选址纪律：结构性
//   布点勿靠缺席默认凑断言）。
void MatrixRun::section22_chunkevictor()
{
    // ── 段内共享帮手 ──────────────────────────────────────────────────────────────────────
    // 缝表键：packed(cx,cz)（ChunkKey 双射打包；负坐标同等合法——section21 同款）。
    const auto kkey = [](int cx, int cz) { return ChunkKey{ cx, cz }.packed(); };
    // 合成生命周期表缝：表缺席格默认 Absent（ChunkManager::lifecycleAt 越界同款语义）。
    const auto tableSeam = [&kkey](const QMap<quint64, ChunkLifecycle> &t) {
        return [&t, &kkey](int cx, int cz) -> ChunkLifecycle {
            return t.value(kkey(cx, cz), ChunkLifecycle::Absent);
        };
    };
    // twin policy（本体被 r2016 钉）：期望 decide 输出的独立计算源（请求 + 驱逐两单）。
    const auto twinDecide = [&](const GenerationPolicyParams &par, int px, int pz,
                                const QMap<quint64, ChunkLifecycle> &t) {
        GenerationPolicy twin{ par };
        return twin.decide(px, pz, tableSeam(t));
    };
    // 同步轨迹 worker（全成功面；execute 序 = 提交编排的最终执行轨迹——section21 同款）。
    class TraceSyncWorker : public GenerationWorker
    {
    public:
        QVector<GenerationRequest> executed;
        Result<void> execute(const GenerationRequest &req) override
        {
            executed.append(req);
            return Result<void>::ok();
        }
    };
    // 轨迹快照（双生恒等比较面：逐沿 stats + executed + outcomes 全量字段）。
    struct DriverSnap
    {
        QVector<int> statsSnap;                 // 每沿 [submitted, canceled, rejected, pending]
        QVector<GenerationRequest> executed;    // 全程执行轨迹
        QVector<GenerationJobOutcome> outcomes; // 全程交付
    };
    const auto replayDriver = [](ChunkStreamDriver &drv, TraceSyncWorker &w,
                                 const int moves[][3], int n, DriverSnap &snap) {
        for (int i = 0; i < n; ++i) {
            drv.onPlayerChunk(moves[i][0], moves[i][1]);
            if (moves[i][2])
                drv.pump();
            const auto st = drv.stats();
            snap.statsSnap.append(st.submitted);
            snap.statsSnap.append(st.canceled);
            snap.statsSnap.append(st.rejected);
            snap.statsSnap.append(drv.pendingJobCount());
        }
        GenerationJobOutcome o;
        while (drv.takeOutcome(o))
            snap.outcomes.append(o);
        snap.executed = w.executed;
    };
    const auto snapsIdentical = [](const DriverSnap &a, const DriverSnap &b) {
        if (a.statsSnap != b.statsSnap || a.executed.size() != b.executed.size()
            || a.outcomes.size() != b.outcomes.size())
            return false;
        for (int i = 0; i < a.executed.size(); ++i) {
            if (a.executed[i].requestId != b.executed[i].requestId
                || a.executed[i].key.cx != b.executed[i].key.cx
                || a.executed[i].key.cz != b.executed[i].key.cz
                || a.executed[i].priority != b.executed[i].priority
                || a.executed[i].kind != b.executed[i].kind)
                return false;
        }
        for (int i = 0; i < a.outcomes.size(); ++i) {
            if (a.outcomes[i].requestId != b.outcomes[i].requestId
                || a.outcomes[i].key.cx != b.outcomes[i].key.cx
                || a.outcomes[i].key.cz != b.outcomes[i].key.cz
                || a.outcomes[i].kind != b.outcomes[i].kind
                || a.outcomes[i].error.code != b.outcomes[i].error.code)
                return false;
        }
        return true;
    };
    // 轨迹恒等（kind 除外面）：id/key/priority/stats/outcomes 全恒等、唯 kind 可异（⑥ kind 翻转面）。
    const auto sameExceptKind = [](const DriverSnap &a, const DriverSnap &b) {
        if (a.statsSnap != b.statsSnap || a.executed.size() != b.executed.size()
            || a.outcomes.size() != b.outcomes.size())
            return false;
        for (int i = 0; i < a.executed.size(); ++i) {
            if (a.executed[i].requestId != b.executed[i].requestId
                || a.executed[i].key.cx != b.executed[i].key.cx
                || a.executed[i].key.cz != b.executed[i].key.cz
                || a.executed[i].priority != b.executed[i].priority)
                return false;
        }
        for (int i = 0; i < a.outcomes.size(); ++i) {
            if (a.outcomes[i].requestId != b.outcomes[i].requestId
                || a.outcomes[i].key.cx != b.outcomes[i].key.cx
                || a.outcomes[i].key.cz != b.outcomes[i].key.cz
                || a.outcomes[i].error.code != b.outcomes[i].error.code)
                return false;
        }
        return true;
    };
    // fresh 小世界 incantation（48×48×96 s82 + 天气双钉，section11+/section19 先例）。
    const auto freshWorld48 = [](World &w) {
        w.setWidth(48);
        w.setDepth(48);
        w.setHeight(96);
        w.setSeed(82);
        w.setWeatherState(0);              // Weather::Clear——转换掷骰不进探针窗口
        w.setWeatherRemainingSec(3600.0f); // >> 探针窗 → 恒晴零 RNG
    };
    // 临时库路径（pid 键名 + 腿标；QDir::temp()，用后即删，绝触 saves/——r2015 先例）。
    const auto tempDb = [](const char *tag) {
        return QDir::temp().absoluteFilePath(QStringLiteral("voxel_r2019_%1_%2.sqlite")
                                                 .arg(QCoreApplication::applicationPid())
                                                 .arg(QLatin1String(tag)));
    };
    // 地表标记放置（t1051 选址纪律：显式读 heightAt + 放置回读校验，静默拒绝即响亮失败）。
    const auto placeMarker = [](World &w, int x, int z) -> int {
        const int h = w.heightAt(x, z);
        if (h < 0 || h + 2 >= w.height()) return -1;
        w.setBlock(x, h + 1, z, quint8(BR::Stone), 0);
        if (w.blockAt(x, h + 1, z) != quint8(BR::Stone)) return -1;
        return h + 1;
    };
    // 单 chunk 三段 blob 快照 / 逐字节对账（r2010d 往返口径；回灌恒等的比较面）。
    const auto chunkBlob = [](World &w, int cx, int cz) -> QList<QByteArray> {
        const Chunk *c = w.chunks().chunk(cx, cz);
        const size_t n = c->voxelCount();
        return { QByteArray(reinterpret_cast<const char *>(c->voxelData()), int(n)),
            QByteArray(reinterpret_cast<const char *>(c->stateData()), int(n)),
            QByteArray(reinterpret_cast<const char *>(c->lightData()), int(n)) };
    };
    const auto blobSame = [](const QList<QByteArray> &a, World &w, int cx, int cz) -> bool {
        const Chunk *c = w.chunks().chunk(cx, cz);
        const size_t n = c->voxelCount();
        return size_t(a.at(0).size()) == n
            && std::memcmp(c->voxelData(), a.at(0).constData(), n) == 0
            && std::memcmp(c->stateData(), a.at(1).constData(), n) == 0
            && std::memcmp(c->lightData(), a.at(2).constData(), n) == 0;
    };
    // 缝调用序记录条目（r2019b/c/d 共用；persist 条目 target 恒 Absent 占位）。
    struct EvCall
    {
        bool persist;         // true = persist 缝调用；false = 转移缝调用
        int cx = 0, cz = 0;
        ChunkLifecycle target = ChunkLifecycle::Absent; // 转移目标（persist 条目占位）
        bool accepted = false;                          // 转移是否被接受（persist = 是否成功）
    };
    // 自校验 QMap 缝 rig（r2019c/d 用）：转移经 chunklifecycle 合法表守卫（ChunkManager::
    // setLifecycle 同构——非法拒 false 不改表），合法改表返回 true；全调用序显式记录。
    //   键打包内联 ChunkKey::packed（局部类成员函数不可捕获段内 lambda——kkey 是局部变量）。
    struct EvSeamRig
    {
        QMap<quint64, ChunkLifecycle> table;
        QVector<EvCall> calls;
        bool failPersist = false;       // 注入：persist 恒失败
        bool rejectTransitions = false; // 注入：转移恒拒绝（守卫前注入）
        static quint64 key(int cx, int cz) { return ChunkKey{ cx, cz }.packed(); }
        Result<void> persist(int cx, int cz)
        {
            calls.append({ true, cx, cz, ChunkLifecycle::Absent, !failPersist });
            return failPersist ? Result<void>::fail(206, "injected persist failure")
                               : Result<void>::ok();
        }
        bool transition(int cx, int cz, ChunkLifecycle to)
        {
            const ChunkLifecycle from
                = table.value(key(cx, cz), ChunkLifecycle::Absent);
            const bool acc = !rejectTransitions && chunkLifecycleTransitionLegal(from, to);
            if (acc)
                table.insert(key(cx, cz), to);
            calls.append({ false, cx, cz, to, acc });
            return acc;
        }
        // 观测帮手（腿断言用）。
        int persistCalls(int cx, int cz) const
        {
            int n = 0;
            for (const EvCall &c : calls)
                if (c.persist && c.cx == cx && c.cz == cz) ++n;
            return n;
        }
        int transCalls(int cx, int cz) const
        {
            int n = 0;
            for (const EvCall &c : calls)
                if (!c.persist && c.cx == cx && c.cz == cz) ++n;
            return n;
        }
        int acceptedTrans(int cx, int cz) const
        {
            int n = 0;
            for (const EvCall &c : calls)
                if (!c.persist && c.cx == cx && c.cz == cz && c.accepted) ++n;
            return n;
        }
        QVector<QPair<ChunkLifecycle, bool>> transSeq(int cx, int cz) const
        {
            QVector<QPair<ChunkLifecycle, bool>> v;
            for (const EvCall &c : calls)
                if (!c.persist && c.cx == cx && c.cz == cz)
                    v.append({ c.target, c.accepted });
            return v;
        }
        int firstPersistIndex(int cx, int cz) const
        {
            for (int i = 0; i < calls.size(); ++i)
                if (calls[i].persist && calls[i].cx == cx && calls[i].cz == cz) return i;
            return -1;
        }
        int firstTransIndex(int cx, int cz) const
        {
            for (int i = 0; i < calls.size(); ++i)
                if (!calls[i].persist && calls[i].cx == cx && calls[i].cz == cz) return i;
            return -1;
        }
    };

    // ── r2019a：惰性承重墙——默认关 driver 全零动作 + 扩展缝关态不可达 + P2 形态双生恒等 ──
    runLeg(QStringLiteral("r2019a default-off inert load-bearing wall (section 29.4 P3,"
        " P2 wall continuation): a default-constructed driver with no evictor and no"
        " saved-content query sweeps positions with zero submissions/cancellations/"
        " rejections, an untouched queue and a never-executed worker (P2 semantics"
        " bit-unchanged); with the evictor and saved-content query seams installed but"
        " streaming still disabled, far-position sweeps never reach either seam (zero"
        " callback invocations, zero queries, zero stats movement - the callback sits"
        " behind the default-off short-circuit); and an enabled driver carrying the"
        " recording seams replays a movement trajectory bit-identically to a P2-form"
        " driver (same stats per edge, same executed requests, same outcomes) while the"
        " recording seams demonstrably fired (queries counted, toEvict candidates"
        " delivered item-for-item equal to the twin policy decide output)"), [&]() {
        bool ok = true;
        QString diag;

        // 共享缝表（混合面：若被测短路失效即有请求/回调可观察）。
        QMap<quint64, ChunkLifecycle> tMixed;
        tMixed.insert(kkey(0, 0), ChunkLifecycle::Absent);
        tMixed.insert(kkey(1, 0), ChunkLifecycle::Generated);
        tMixed.insert(kkey(-3, 2), ChunkLifecycle::Evicting);
        tMixed.insert(kkey(4, 4), ChunkLifecycle::Active); // gen=2 窗外驻留 → toEvict 候选源

        // 相①：默认构造（无 evictor/无 savedQuery）+ 扫掠 → 全零动作（r2018 语义逐位不动）。
        const int sweep[4][2] = { { 0, 0 }, { 3, 0 }, { 3, 4 }, { 40, -30 } };
        {
            TraceSyncWorker w;
            ChunkStreamDriver drv; // 默认构造 = 默认关 + 两扩展缝 null
            drv.setSeam(tableSeam(tMixed));
            drv.setWorker(&w);
            for (const auto &pos : sweep) {
                drv.onPlayerChunk(pos[0], pos[1]);
                drv.pump(); // 关态 pump 零动作（不转发底层泵）
                const auto st = drv.stats();
                const bool zero = st.submitted == 0 && st.canceled == 0 && st.rejected == 0
                    && drv.pendingJobCount() == 0 && drv.outcomeCount() == 0
                    && w.executed.isEmpty();
                if (!zero) {
                    ok = false;
                    diag += QStringLiteral("[ph1 at(%1,%2) s=%3/c=%4/r=%5 pj=%6 ex=%7] ")
                                .arg(pos[0])
                                .arg(pos[1])
                                .arg(st.submitted)
                                .arg(st.canceled)
                                .arg(st.rejected)
                                .arg(drv.pendingJobCount())
                                .arg(w.executed.size());
                }
            }
        }

        // 相②：两扩展缝已装 + streaming off → 扫掠零回调零查询（回调点在默认关短路之后）。
        {
            TraceSyncWorker w;
            ChunkStreamDriver drv{ GenerationPolicyParams(false, 2, 1, 4) };
            drv.setSeam(tableSeam(tMixed));
            drv.setWorker(&w);
            int evictCalls = 0, queryCalls = 0, evictCandidates = 0;
            drv.setEvictor([&](const QVector<GenerationPolicyEvict> &c) {
                ++evictCalls;
                evictCandidates += c.size();
            });
            drv.setSavedContentQuery([&](int, int) {
                ++queryCalls;
                return false;
            });
            for (const auto &pos : sweep) {
                drv.onPlayerChunk(pos[0], pos[1]);
                drv.pump();
                const auto st = drv.stats();
                const bool zero = st.submitted == 0 && st.canceled == 0 && st.rejected == 0
                    && drv.pendingJobCount() == 0 && w.executed.isEmpty();
                if (!zero) {
                    ok = false;
                    diag += QStringLiteral("[ph2 at(%1,%2) s=%3/c=%4/r=%5] ")
                                .arg(pos[0])
                                .arg(pos[1])
                                .arg(st.submitted)
                                .arg(st.canceled)
                                .arg(st.rejected);
                }
            }
            // NEG-1 关联面：使能回调 + streaming off 时 evict 不可达（回调零调用零候选零查询）。
            if (evictCalls != 0 || evictCandidates != 0 || queryCalls != 0) {
                ok = false;
                diag += QStringLiteral("[ph2 seams evict=%1 cand=%2 query=%3] ")
                            .arg(evictCalls)
                            .arg(evictCandidates)
                            .arg(queryCalls);
            }
        }

        // 相③：P2 形态双生恒等——使能 driver 无扩展（A）vs 使能 driver 带记录缝（B，查询恒
        // false → 全 Generate）：轨迹逐位恒等；B 的记录面确曾触发（非空转）且候选 ≡ twin。
        {
            const GenerationPolicyParams par(true, 2, 1, 4); // gen=2 → 5×5 窗；(4,4) 距 (0,0) cheb 4 > 2
            const int moves[3][3] = { { 0, 0, 1 }, { 1, 1, 0 }, { 3, 0, 1 } };

            TraceSyncWorker wA;
            ChunkStreamDriver dA{ par }; // P2 形态（无 evictor/无 savedQuery）
            dA.setSeam(tableSeam(tMixed));
            dA.setWorker(&wA);
            DriverSnap snapA;
            replayDriver(dA, wA, moves, 3, snapA);

            int evictCallsB = 0, queryCallsB = 0;
            QVector<QVector<GenerationPolicyEvict>> edgeCands;
            TraceSyncWorker wB;
            ChunkStreamDriver dB{ par };
            dB.setSeam(tableSeam(tMixed));
            dB.setWorker(&wB);
            dB.setEvictor([&](const QVector<GenerationPolicyEvict> &c) {
                ++evictCallsB;
                edgeCands.append(c); // 记录-only：不改任何状态（P2 行为保持）
            });
            dB.setSavedContentQuery([&](int, int) {
                ++queryCallsB;
                return false; // 恒无存档 → 全 Generate（P2 twin 面）
            });
            DriverSnap snapB;
            replayDriver(dB, wB, moves, 3, snapB);

            // 非空转 + 候选面：B 的 evictor 每沿确曾收到 twin decide 的 toEvict（逐沿逐项恒等）。
            //   沿 1 @(0,0)：(4,4) cheb4>2 → 唯一候选[(-3,2) 虽窗外距离但 Evicting 非驻留恒不入]；
            //   沿 2 @(1,1)：(4,4) cheb3>2 → 候选；沿 3 @(3,0)：(4,4) cheb4>2 → 候选。
            const auto twin1 = twinDecide(par, 0, 0, tMixed);
            const auto twin2 = twinDecide(par, 1, 1, tMixed);
            const auto twin3 = twinDecide(par, 3, 0, tMixed);
            const bool candsOk = evictCallsB == 3 && edgeCands.size() == 3
                && edgeCands[0] == twin1.toEvict && edgeCands[1] == twin2.toEvict
                && edgeCands[2] == twin3.toEvict && twin1.toEvict.size() == 1;
            const bool twinOk = snapsIdentical(snapA, snapB) && candsOk
                && queryCallsB > 0 && snapA.executed.size() > 0 && snapA.outcomes.size() > 0;
            if (!twinOk) {
                ok = false;
                diag += QStringLiteral("[ph3 twin=%1 cands=%2 q=%3 ev=%4 exA=%5] ")
                            .arg(snapsIdentical(snapA, snapB))
                            .arg(candsOk)
                            .arg(queryCallsB)
                            .arg(evictCallsB)
                            .arg(snapA.executed.size());
            }
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2019a default-off inert wall: a default-constructed driver"
                             " (no evictor, no saved-content query) sweeps positions with"
                             " zero submissions/cancellations/rejections and an untouched"
                             " queue (P2 semantics bit-unchanged); with both P3 seams"
                             " installed but streaming disabled, sweeps never reach either"
                             " seam (zero callbacks, zero queries); and an enabled driver"
                             " carrying recording seams replays a movement trajectory"
                             " bit-identically to a P2-form driver while the seams"
                             " demonstrably fired with toEvict candidates equal to the"
                             " twin policy decide output"
                          << (ok ? QString() : diag);
    });

    // ── r2019b：驱逐回灌承重——真 SaveCoordinator + fresh 临时库 + 真世界表 + scratch 脏缝 ──
    runLeg(QStringLiteral("r2019b eviction round-trip load-bearing leg (section 29.4 P3"
        " 'Evicting->Absent with edits persisted through the SaveCoordinator and reloaded on"
        " re-entry'): against a real fresh world with its real lifecycle table and a real"
        " SaveCoordinator on a fresh temp database, the driver's toEvict consumption evicts"
        " the eight out-of-radius residents when the player lands mid-world - the edited"
        " chunk persists BEFORE any transition call for it (seam-recorded call order), walks"
        " the legal edge path ⑥⑦ to Absent (the ensure-Loaded self-transition is rejected by"
        " the guard as the documented no-op), the report counts exactly one persisted and"
        " eight evicted with zero aborts/skips, and the delivered candidate list equals the"
        " twin policy decide output item-for-item; the untouched out-of-radius chunk shows"
        " zero persist calls and reaches Absent directly; the coordinator ledger reads Clean"
        " at generation 1 (one unified save); re-entering the evicted chunk asks the"
        " saved-content seam (true - the whole world rode the unified save), submits a Load"
        " kind job (not Generate) and after the pump the reloaded read face (a zero-filled"
        " fresh world the store was rebound to, setWorld before loadChunks per the R20.10d"
        " lesson) is bit-identical to the pre-eviction chunk snapshot (voxels/states/light"
        " and the placed marker readable)"), [&]() {
        bool ok = true;
        QString diag;

        // ① rig：fresh 世界（3×3 chunk 全 Loaded 稳态）+ 编辑 (2,2)（scratch 脏缝登记）。
        World w1;
        freshWorld48(w1);
        const int mY = placeMarker(w1, 40, 40); // (40,40) → chunk (2,2)
        ok = ok && mY > 0;
        if (mY <= 0)
            diag += QStringLiteral("[site] ");
        QSet<quint64> dirty;
        dirty.insert(kkey(2, 2));

        // 内容快照（驱逐前）：承重比对基准（回灌逐位恒等的锚）。
        const QList<QByteArray> snapA = chunkBlob(w1, 2, 2); // 编辑过的驱逐受害者
        const QList<QByteArray> snapB = chunkBlob(w1, 2, 0); // 未编辑的驱逐受害者

        WorldStore store;
        store.setWorld(&w1);
        const QString db = tempDb("evict");
        QFile::remove(db);
        ok = ok && store.openWorld(db);
        SaveCoordinator coord;
        coord.bind(&store, db);

        // ② 三缝（真世界/真协调器载体；调用序显式记录——调用序柱的观测面）。
        QVector<EvCall> trace;
        ChunkEvictor ev;
        ev.setTransitionFn([&](int cx, int cz, ChunkLifecycle t) {
            const bool acc = w1.setChunkLifecycle(cx, cz, t); // 唯一守卫入口（forwarder）
            trace.append({ false, cx, cz, t, acc });
            return acc;
        });
        ev.setDirtyQueryFn([&](int cx, int cz) { return dirty.contains(kkey(cx, cz)); });
        ev.setPersistFn([&](int cx, int cz) -> Result<void> {
            trace.append({ true, cx, cz, ChunkLifecycle::Absent, true });
            SaveRequest rq; // 真统一保存（world 部分；整世界 9 chunk 随行落盘）
            rq.name = QStringLiteral("r2019b");
            const SaveReceipt rc = coord.saveAll(rq);
            return rc.ok() ? Result<void>::ok()
                           : Result<void>::fail(rc.error.code, "coord save failed");
        });

        // ③ driver（gen=0：半径仅玩家 chunk；scan=2 盖满 3×3）+ 扩展缝。
        const GenerationPolicyParams par(true, 0, 0, 2);
        // 回灌执行缝（测试侧生产接线替身）：Load kind → store 改绑回灌面 + loadChunks
        // （r2010d rebind 教训：setWorld 必须先于 loadChunks——写入面 = store 持有的 m_world）。
        World wR; // 回灌读面：fresh dims + beginLoad 零填充 → 回灌内容只能来自库（非空转结构性保证）
        freshWorld48(wR);
        wR.beginLoad(82);
        class ReloadWorker : public GenerationWorker
        {
        public:
            WorldStore *store = nullptr;
            World *reloadTarget = nullptr;
            QVector<GenerationRequest> executed;
            int reloads = 0;
            Result<void> execute(const GenerationRequest &req) override
            {
                executed.append(req);
                if (req.kind == GenerationJobKind::Load && store && reloadTarget) {
                    store->setWorld(reloadTarget); // r2010d rebind 教训钉（先 setWorld 后 loadChunks）
                    const int n = store->loadChunks();
                    if (n < 0)
                        return Result<void>::fail(kErrSaveStoreNotOpen, "loadChunks failed");
                    ++reloads;
                }
                return Result<void>::ok();
            }
        };
        ReloadWorker worker;
        worker.store = &store;
        worker.reloadTarget = &wR;

        ChunkStreamDriver drv{ par };
        drv.setSeam([&](int cx, int cz) { return w1.chunks().lifecycleAt(cx, cz); });
        drv.setWorker(&worker);
        int queryCalls = 0;
        drv.setSavedContentQuery([&](int, int) {
            ++queryCalls;
            return true; // 库内已有全量内容（统一保存落了 9 chunk）→ 重入即 Load
        });
        QVector<GenerationPolicyEvict> gotCands;
        QVector<ChunkEvictor::Report> reports;
        drv.setEvictor([&](const QVector<GenerationPolicyEvict> &c) {
            gotCands += c;
            reports.append(ev.evict(c));
        });

        // ④ 沿 1：玩家落 (1,1) → toEvict = 8 个半径外驻留 chunk（(2,2) 编辑过 + 7 个未编辑）。
        const auto twin1 = twinDecide(par, 1, 1,
            [&] {
                QMap<quint64, ChunkLifecycle> t;
                for (int cz = 0; cz < 3; ++cz)
                    for (int cx = 0; cx < 3; ++cx)
                        t.insert(kkey(cx, cz), w1.chunks().lifecycleAt(cx, cz));
                return t;
            }());
        drv.onPlayerChunk(1, 1); // evictor 回调在提交/取消之后同步执行驱逐

        // 候选面：交付单 ≡ twin decide().toEvict（三点互钉：policy 权威 → driver 消费 → 执行）。
        const bool candsOk = gotCands == twin1.toEvict && gotCands.size() == 8;
        // 报告面：persisted=1（恰编辑块）/ evicted=8 / aborted=0 / skipped=0。
        const ChunkEvictor::Report rep1 = reports.value(0);
        const bool rep1Ok = reports.size() == 1 && rep1.persisted == 1 && rep1.evicted == 8
            && rep1.aborted == 0 && rep1.skipped == 0;
        // 调用序柱（先落盘后转移）：(2,2) 的 persist 索引 < 其首个转移调用索引。
        int firstP22 = -1, firstT22 = -1;
        for (int i = 0; i < trace.size(); ++i) {
            if (trace[i].persist && trace[i].cx == 2 && trace[i].cz == 2 && firstP22 < 0)
                firstP22 = i;
            if (!trace[i].persist && trace[i].cx == 2 && trace[i].cz == 2 && firstT22 < 0)
                firstT22 = i;
        }
        const bool orderOk = firstP22 >= 0 && firstT22 > firstP22;
        // ⑥⑦面：(2,2) 转移序 = [(Loaded,rej=自转移 no-op), (Evicting,acc=⑥), (Absent,acc=⑦)]。
        QVector<QPair<ChunkLifecycle, bool>> want22
            = { { ChunkLifecycle::Loaded, false }, { ChunkLifecycle::Evicting, true },
                  { ChunkLifecycle::Absent, true } };
        auto seqOf = [&](int cx, int cz) {
            QVector<QPair<ChunkLifecycle, bool>> v;
            for (const EvCall &c : trace)
                if (!c.persist && c.cx == cx && c.cz == cz)
                    v.append({ c.target, c.accepted });
            return v;
        };
        const bool edgeOk22 = seqOf(2, 2) == want22 && w1.chunks().lifecycleAt(2, 2) == ChunkLifecycle::Absent;
        // ③ 未编辑 chunk：零 persist、直达 Absent（同款合法边序）。
        const bool cleanChunkOk = seqOf(2, 0) == want22
            && std::none_of(trace.cbegin(), trace.cend(), [](const EvCall &c) {
                   return c.persist && c.cx == 2 && c.cz == 0;
               })
            && w1.chunks().lifecycleAt(2, 0) == ChunkLifecycle::Absent;
        // 台账面：一次统一保存 → Clean 1/1。
        const SaveGenerationInfo led1 = coord.recover();
        const bool ledgerOk = led1.state == SaveRecoveryState::Clean && led1.generation == 1
            && led1.completeGeneration == 1;
        const bool ph1 = candsOk && rep1Ok && orderOk && edgeOk22 && cleanChunkOk && ledgerOk;
        ok = ok && ph1;
        if (!ph1)
            diag += QStringLiteral("[ph1 cands=%1 rep p=%2 e=%3 a=%4 s=%5 order=%6(%7<%8)"
                                   " edge22=%9 clean=%10 ledger=%11] ")
                        .arg(candsOk)
                        .arg(rep1.persisted)
                        .arg(rep1.evicted)
                        .arg(rep1.aborted)
                        .arg(rep1.skipped)
                        .arg(orderOk)
                        .arg(firstP22)
                        .arg(firstT22)
                        .arg(edgeOk22)
                        .arg(cleanChunkOk)
                        .arg(ledgerOk);

        // ⑤ 沿 2：重入 (2,2)（现 Absent）→ savedQuery true → Load kind 提交（非 Generate）。
        store.setWorld(&wR); // r2010d rebind 教训：回灌面改绑（此后零保存发生，w1 表照读）
        // 沿 2 期望（twin 必须沿前快照表——沿回调会把 (1,1) 驱逐，沿后快照 = 陈旧前置态反例，
        // R20.10 态独立成立纪律）。
        QMap<quint64, ChunkLifecycle> t2now;
        for (int cz = 0; cz < 3; ++cz)
            for (int cx = 0; cx < 3; ++cx)
                t2now.insert(kkey(cx, cz), w1.chunks().lifecycleAt(cx, cz));
        const auto twin2 = twinDecide(par, 2, 2, t2now);
        const bool cands2Ok = twin2.toRequest.size() == 1 && twin2.toRequest[0].cx == 2
            && twin2.toRequest[0].cz == 2 && twin2.toEvict.size() == 1
            && twin2.toEvict[0].cx == 1 && twin2.toEvict[0].cz == 1;
        drv.onPlayerChunk(2, 2); // decide：toRequest=[(2,2)]；toEvict=[(1,1)]（半径外驻留）
        const bool subOk = drv.stats().submitted == 1 && drv.pendingJobCount() == 1
            && queryCalls == 1;
        drv.pump(); // Load job 内联执行：回灌（store.loadChunks → wR）+ 交付 outcome

        // kind 面 + 交付面：恰一条 Load outcome（非 Generate）、零错误、(2,2)。
        GenerationJobOutcome oc;
        bool ocOk = false;
        if (drv.takeOutcome(oc))
            ocOk = oc.kind == GenerationJobKind::Load && !isError(oc.error)
                && oc.key.cx == 2 && oc.key.cz == 2 && oc.requestId != 0;
        const bool drained = !drv.takeOutcome(oc);
        // 回灌恒等面：wR(2,2) ≡ 驱逐前快照（voxels/states/light 三段逐字节 + 标记块可读）；
        // (2,0) 回灌面 ≡ 其快照（同库同代次字节）。回灌读面来自库（beginLoad 零填充 → 非空转）。
        wR.finishLoad();
        const bool reloadId = blobSame(snapA, wR, 2, 2) && blobSame(snapB, wR, 2, 0)
            && wR.blockAt(40, mY, 40) == quint8(BR::Stone)
            && worker.reloads == 1 && worker.executed.size() == 1
            && worker.executed[0].kind == GenerationJobKind::Load;
        const bool ph2 = subOk && cands2Ok && ocOk && drained && reloadId
            && reports.value(1).evicted == 1 && reports.value(1).persisted == 0;
        ok = ok && ph2;
        if (!ph2)
            diag += QStringLiteral("[ph2 sub=%1 cands=%2 oc=%3 drained=%4 reload=%5"
                                   " rep2 e=%6 p=%7] ")
                        .arg(subOk)
                        .arg(cands2Ok)
                        .arg(ocOk)
                        .arg(drained)
                        .arg(reloadId)
                        .arg(reports.value(1).evicted)
                        .arg(reports.value(1).persisted);

        store.closeWorld();
        QFile::remove(db); // 临时库用后即删（绝触 saves/）

        // 非空转总闸：全链确有落盘、确有转移、确有回灌。
        const bool nonVacuous = trace.size() >= 8 * 3 && reports.size() == 2
            && queryCalls == 1;
        ok = ok && nonVacuous;
        if (!nonVacuous)
            diag += QStringLiteral("[vac trace=%1 reports=%2 q=%3] ")
                        .arg(trace.size())
                        .arg(reports.size())
                        .arg(queryCalls);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2019b eviction round-trip: eight out-of-radius residents"
                             " evicted with the edited chunk persisted before any"
                             " transition (recorded call order), walking legal edges to"
                             " Absent with report 1 persisted / 8 evicted and candidates"
                             " equal to the twin decide output; the untouched chunk shows"
                             " zero persists and reaches Absent directly; the ledger reads"
                             " Clean 1/1; re-entry submits a Load kind job and the zero-"
                             " filled reload face comes back bit-identical to the pre-"
                             " eviction snapshot (marker readable)"
                          << (ok ? QString() : diag);
    });

    // ── r2019c：失败中止——persist 失败 = aborted 保持驻留 + 重报收敛 + 拒绝有界 + null 缝 ──
    runLeg(QStringLiteral("r2019c abort-on-failure and fail-safe seams (section 29.4 P3"
        " 'persist failure aborts that chunk's eviction - never drop content then chase it"
        " with a save'): with an injected persisting failure the dirty candidate is counted"
        " aborted, keeps its state with zero accepted transitions and zero table movement,"
        " while its clean sibling evicts normally; the retry semantics are the caller"
        " re-reporting candidates on the next change edge (the same stateless evictor"
        " instance converges once the injection clears); with the transition seam injected"
        " to reject everything, both candidates land aborted with exactly two bounded"
        " transition attempts each (no retry storm) and zero table movement; null seams are"
        " fail-safe: a missing dirty query or missing transition seam skips every candidate"
        " with zero seam calls, and a missing persist seam skips only dirty candidates while"
        " clean ones still evict"), [&]() {
        bool ok = true;
        QString diag;

        // 相①：persist 注入失败 → 脏候选 aborted、保持原态不转移；干净同胞照常驱逐。
        EvSeamRig r1;
        r1.table.insert(kkey(0, 0), ChunkLifecycle::Loaded); // A：dirty 受害者
        r1.table.insert(kkey(1, 0), ChunkLifecycle::Loaded); // B：非 dirty 同胞
        r1.failPersist = true;
        QSet<quint64> dirty1{ kkey(0, 0) };
        ChunkEvictor ev1;
        ev1.setTransitionFn([&r1](int cx, int cz, ChunkLifecycle t) { return r1.transition(cx, cz, t); });
        ev1.setDirtyQueryFn([&dirty1, &kkey](int cx, int cz) { return dirty1.contains(kkey(cx, cz)); });
        ev1.setPersistFn([&r1](int cx, int cz) { return r1.persist(cx, cz); });
        QVector<GenerationPolicyEvict> cands1{ { 0, 0 }, { 1, 0 } };
        const ChunkEvictor::Report rep1 = ev1.evict(cands1);
        const bool abortOk = rep1.persisted == 0 && rep1.evicted == 1 && rep1.aborted == 1
            && rep1.skipped == 0;
        const bool heldOk = r1.acceptedTrans(0, 0) == 0 // 零 accepted 转移（失败 = 不转移）
            && r1.table.value(kkey(0, 0)) == ChunkLifecycle::Loaded // 保持原态
            && r1.persistCalls(0, 0) == 1
            && r1.table.value(kkey(1, 0)) == ChunkLifecycle::Absent
            && r1.persistCalls(1, 0) == 0;
        ok = ok && abortOk && heldOk;
        if (!abortOk || !heldOk)
            diag += QStringLiteral("[ph1 abort=%1 held=%2 p/a/e/s=%3/%4/%5/%6] ")
                        .arg(abortOk)
                        .arg(heldOk)
                        .arg(rep1.persisted)
                        .arg(rep1.aborted)
                        .arg(rep1.evicted)
                        .arg(rep1.skipped);

        // 相②：重试语义 = 下一变更沿重报候选（同一 evictor 实例，注入清除 → 收敛）。
        r1.failPersist = false;
        const ChunkEvictor::Report rep2 = ev1.evict(cands1); // A 复报（B 已 Absent → 转移被拒 aborted）
        const bool retryOk = rep2.persisted == 1 && rep2.evicted == 1 // A 驱逐成功
            && rep2.aborted == 1 // B 已 Absent：ensure→Loaded 拒、→Evicting 拒 = 不在可驱逐稳态
            && r1.table.value(kkey(0, 0)) == ChunkLifecycle::Absent;
        ok = ok && retryOk;
        if (!retryOk)
            diag += QStringLiteral("[ph2 retry=%1 p=%2 e=%3 a=%4 A=%5] ")
                        .arg(retryOk)
                        .arg(rep2.persisted)
                        .arg(rep2.evicted)
                        .arg(rep2.aborted)
                        .arg(int(r1.table.value(kkey(0, 0))));

        // 相③：转移缝注入拒绝 → aborted 计数 + 每候选转移尝试恰 2 次（有界，无重试风暴）。
        EvSeamRig r3;
        r3.table.insert(kkey(0, 0), ChunkLifecycle::Loaded); // C：dirty
        r3.table.insert(kkey(1, 0), ChunkLifecycle::Loaded); // D：非 dirty
        r3.rejectTransitions = true;
        QSet<quint64> dirty3{ kkey(0, 0) };
        ChunkEvictor ev3;
        ev3.setTransitionFn([&r3](int cx, int cz, ChunkLifecycle t) { return r3.transition(cx, cz, t); });
        ev3.setDirtyQueryFn([&dirty3, &kkey](int cx, int cz) { return dirty3.contains(kkey(cx, cz)); });
        ev3.setPersistFn([&r3](int cx, int cz) { return r3.persist(cx, cz); });
        const ChunkEvictor::Report rep3 = ev3.evict(cands1);
        const bool rejOk = rep3.persisted == 1 && rep3.evicted == 0 && rep3.aborted == 2
            && rep3.skipped == 0;
        const bool boundedOk = r3.transCalls(0, 0) == 2 && r3.transCalls(1, 0) == 2
            && r3.acceptedTrans(0, 0) == 0 && r3.acceptedTrans(1, 0) == 0
            && r3.table.value(kkey(0, 0)) == ChunkLifecycle::Loaded
            && r3.table.value(kkey(1, 0)) == ChunkLifecycle::Loaded;
        ok = ok && rejOk && boundedOk;
        if (!rejOk || !boundedOk)
            diag += QStringLiteral("[ph3 rej=%1 bnd=%2 tC=%3 tD=%4] ")
                        .arg(rejOk)
                        .arg(boundedOk)
                        .arg(r3.transCalls(0, 0))
                        .arg(r3.transCalls(1, 0));

        // 相④：null 缝 fail-safe（头注「null 缝行为」节的行为面）：
        //   缺 dirty 缝 / 缺转移缝 → 全 skipped 零缝调用；
        //   缺 persist 缝 → 仅 dirty 候选 skipped（宁驻留不误删），干净候选照常驱逐。
        QSet<quint64> dirty4{ kkey(0, 0) };
        ChunkEvictor evNoDirty;
        evNoDirty.setTransitionFn(
            [&r1](int cx, int cz, ChunkLifecycle t) { return r1.transition(cx, cz, t); });
        evNoDirty.setPersistFn([&r1](int cx, int cz) { return r1.persist(cx, cz); });
        const int baseCalls = r1.calls.size();
        const ChunkEvictor::Report repN1 = evNoDirty.evict(cands1);
        const bool noDirtyOk = repN1.skipped == 2 && repN1.evicted == 0 && repN1.aborted == 0
            && repN1.persisted == 0 && r1.calls.size() == baseCalls; // 零缝调用

        ChunkEvictor evNoTrans;
        evNoTrans.setDirtyQueryFn([&dirty4, &kkey](int cx, int cz) { return dirty4.contains(kkey(cx, cz)); });
        evNoTrans.setPersistFn([&r1](int cx, int cz) { return r1.persist(cx, cz); });
        const ChunkEvictor::Report repN2 = evNoTrans.evict(cands1);
        const bool noTransOk = repN2.skipped == 2 && repN2.evicted == 0;

        EvSeamRig r4; // 独立表：E(0,0) dirty / F(1,0) 干净——缺 persist 缝分化面
        r4.table.insert(kkey(0, 0), ChunkLifecycle::Loaded);
        r4.table.insert(kkey(1, 0), ChunkLifecycle::Loaded);
        ChunkEvictor evNoPersist;
        evNoPersist.setTransitionFn(
            [&r4](int cx, int cz, ChunkLifecycle t) { return r4.transition(cx, cz, t); });
        evNoPersist.setDirtyQueryFn([&dirty4, &kkey](int cx, int cz) { return dirty4.contains(kkey(cx, cz)); });
        const ChunkEvictor::Report repN3 = evNoPersist.evict(cands1);
        const bool noPersistOk = repN3.skipped == 1 && repN3.evicted == 1 && repN3.aborted == 0
            && r4.table.value(kkey(0, 0)) == ChunkLifecycle::Loaded // dirty E 宁驻留
            && r4.table.value(kkey(1, 0)) == ChunkLifecycle::Absent; // 干净 F 照常驱逐
        ok = ok && noDirtyOk && noTransOk && noPersistOk;
        if (!noDirtyOk || !noTransOk || !noPersistOk)
            diag += QStringLiteral("[ph4 nd=%1 nt=%2 np=%3 ndS=%4 ntS=%5 npS=%6 npE=%7] ")
                        .arg(noDirtyOk)
                        .arg(noTransOk)
                        .arg(noPersistOk)
                        .arg(repN1.skipped)
                        .arg(repN2.skipped)
                        .arg(repN3.skipped)
                        .arg(repN3.evicted);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2019c abort-on-failure: an injected persist failure counts"
                             " the dirty candidate aborted with zero accepted transitions"
                             " and zero table movement while the clean sibling evicts, the"
                             " next re-report converges through the same stateless evictor,"
                             " an all-rejecting transition seam lands both candidates"
                             " aborted with exactly two bounded attempts (no retry storm),"
                             " and null seams skip candidates with zero side effects"
                             " (missing persist seam spares only dirty chunks)"
                          << (ok ? QString() : diag);
    });

    // ── r2019d：结构钉 + kind 选择——chunkevictor/driver 双头钉 + Load 门控 + 确定性 ──────
    runLeg(QStringLiteral("r2019d structure pins and kind selection (section 29.4 P3):"
        " compile-time restatement keeps ChunkEvictor and its Report QObject-free with a"
        " trivially copyable report; comment-stripped pins hold the three seams and the"
        " evict entry plus the legal-edge ⑥⑦ drive lines in chunkevictor.h while reverse"
        " probes prove no QObject/QML/thread/World/ChunkManager/SaveCoordinator/setLifecycle"
        " tokens (persistence and transitions ride the seams only) and zero production"
        " wiring; the driver header pins setEvictor/setSavedContentQuery and the toEvict"
        " callback behind its null guard with Mesh still forbidden and Load appearing"
        " exactly at the seam-gated selection (the P2-era Load-forbidden probe's"
        " counterpart); a null saved-content query keeps every submission Generate and"
        " replays bit-identically to a seam-free driver while a always-true query flips the"
        " same keys to Load kind with identical ids/priorities; a null evictor replays"
        " identically while an installed recording evictor receives the twin decide toEvict"
        " list item-for-item; and two fresh evictors replaying the same candidate order"
        " produce bit-identical call traces and reports"), [&]() {
        bool ok = true;
        QString diag;

        // ① 编译期复述（r2014d/r2015d 腿内复钉先例——头钉被删即双红）。
        static_assert(QObjectFree<ChunkEvictor>, "r2019d: evictor must not carry QObject");
        static_assert(QObjectFree<ChunkEvictor::Report>, "r2019d: report must not carry QObject");
        static_assert(std::is_trivially_copyable_v<ChunkEvictor::Report>,
            "r2019d: report stays trivially copyable");

        // ② 源码钉根（exe 相对 src/——r2011d/r2015d/r2018d 同款解析）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src"));
        const auto forbiddenAbsent = [](const QString &path, const char *needle) {
            const QStringList miss = pinSet(path, { SrcPin("forbidden-probe", needle, 1) });
            return miss.size() == 1
                && !miss.first().startsWith(QStringLiteral("<file-unreadable"));
        };

        // ③ chunkevictor.h：正面钉（三缝/evict 签名/⑥⑦驱动行/值纪律）。
        const QStringList missEv = pinSet(
            srcRoot + QStringLiteral("/World/chunkevictor.h"), {
                SrcPin("r2019 evictor class", "class ChunkEvictor", 1),
                SrcPin("r2019 evict entry", "Report evict(const QVector<GenerationPolicyEvict>", 1),
                SrcPin("r2019 transition seam type", "using LifecycleTransitionFn", 1),
                SrcPin("r2019 dirty seam type", "using DirtyQueryFn", 1),
                SrcPin("r2019 persist seam type", "using PersistFn", 1),
                SrcPin("r2019 persist seam call (persist-before-transition execution face)",
                    "m_persist(c.cx, c.cz)", 1),
                SrcPin("r2019 ensure-Loaded drive line", "ChunkLifecycle::Loaded)", 2),
                SrcPin("r2019 edge-6 drive line", "m_transition(c.cx, c.cz, ChunkLifecycle::Evicting)", 1),
                SrcPin("r2019 edge-7 drive line", "m_transition(c.cx, c.cz, ChunkLifecycle::Absent)", 1),
                SrcPin("r2019 qobjectfree pin", "static_assert(QObjectFree<ChunkEvictor>", 1),
            });
        for (const QString &m : missEv) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        // 反探（minCount=1 惯用法，miss 非空 = 合规）：零 QObject/QML/线程/World*/ChunkManager*/
        //   SaveCoordinator 记号（落盘经 persist 缝、转移经转移缝——组件零直驱零持久化直绑）。
        const QString evHdr = srcRoot + QStringLiteral("/World/chunkevictor.h");
        const bool evNegOk = forbiddenAbsent(evHdr, ": public QObject")
            && forbiddenAbsent(evHdr, "Q_OBJECT") && forbiddenAbsent(evHdr, "Q_INVOKABLE")
            && forbiddenAbsent(evHdr, "Q_PROPERTY") && forbiddenAbsent(evHdr, "QThread")
            && forbiddenAbsent(evHdr, "std::thread") && forbiddenAbsent(evHdr, "QMutex")
            && forbiddenAbsent(evHdr, "World *") && forbiddenAbsent(evHdr, "ChunkManager *")
            && forbiddenAbsent(evHdr, "SaveCoordinator") && forbiddenAbsent(evHdr, "setLifecycle")
            && forbiddenAbsent(evHdr, "QML_NAMED_ELEMENT");
        ok = ok && evNegOk;
        if (!evNegOk) diag += QStringLiteral("[ev-neg] ");

        // ④ chunkstreamdriver.h：P3 扩展正面钉 + Mesh 仍禁出 + Load 恰经缝门控 + 零直驱。
        const QString drvHdr = srcRoot + QStringLiteral("/World/chunkstreamdriver.h");
        const QStringList missDrv = pinSet(drvHdr, {
            SrcPin("r2019 evictor injection face", "void setEvictor(const EvictorFn", 1),
            SrcPin("r2019 saved-content injection face",
                "void setSavedContentQuery(const SavedContentQueryFn", 1),
            SrcPin("r2019 toEvict consume call behind null guard", "if (m_evictor)", 1),
            SrcPin("r2019 toEvict consume call site", "m_evictor(d.toEvict)", 1),
            SrcPin("r2019 seam-gated kind selection", "m_savedQuery && m_savedQuery(r.cx, r.cz)", 1),
            SrcPin("r2019 generate default branch", "GenerationJobKind::Generate", 1),
            SrcPin("r2018 driver class retained", "class ChunkStreamDriver", 1),
            SrcPin("r2018 default-off double short-circuit retained",
                "if (!m_params.streamingEnabled", 2),
        });
        for (const QString &m : missDrv) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const bool drvNegOk = forbiddenAbsent(drvHdr, "GenerationJobKind::Mesh")
            && forbiddenAbsent(drvHdr, "setLifecycle") // 零边驱动不变（r2018d 承接）
            && forbiddenAbsent(drvHdr, ": public QObject") && forbiddenAbsent(drvHdr, "Q_OBJECT")
            && forbiddenAbsent(drvHdr, "std::thread") && forbiddenAbsent(drvHdr, "QMutex")
            && forbiddenAbsent(drvHdr, "World *") && forbiddenAbsent(drvHdr, "ChunkManager *");
        ok = ok && drvNegOk;
        if (!drvNegOk) diag += QStringLiteral("[drv-neg] ");

        // ⑤ 零接线（世界/会话/输入/QML 面零 ChunkEvictor 提及——生产接线归 P4/P5）。
        const bool unwired = forbiddenAbsent(srcRoot + QStringLiteral("/World/world.h"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/World/world.cpp"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/gamesession.h"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/playercontroller.h"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "ChunkEvictor")
            && forbiddenAbsent(srcRoot + QStringLiteral("/ui/Main.qml"), "ChunkStreamDriver");
        ok = ok && unwired;
        if (!unwired) diag += QStringLiteral("[unwired] ");

        // ⑥ kind 选择：null savedQuery → 全 Generate 双生（r2018b twin 复跑恒等）；恒 true →
        //    同键 kind 翻 Load（id/priority/stats 轨迹恒等）；恒 false ≡ null（双生复跑恒等）。
        const GenerationPolicyParams par(true, 1, 1, 3); // gen=1 → 3×3 窗；scan=3
        QMap<quint64, ChunkLifecycle> td;
        td.insert(kkey(0, 1), ChunkLifecycle::Generated); // 窗内 1 格免请求
        td.insert(kkey(2, 2), ChunkLifecycle::Active);    // 窗外驻留 → toEvict 候选
        const auto twinD = twinDecide(par, 0, 0, td);
        const int moves1[2][3] = { { 0, 0, 1 }, { 1, 0, 1 } };

        TraceSyncWorker wN;
        ChunkStreamDriver dNullQ{ par };
        dNullQ.setSeam(tableSeam(td));
        dNullQ.setWorker(&wN); // savedQuery null（P2 形态）
        DriverSnap snapN;
        replayDriver(dNullQ, wN, moves1, 2, snapN);

        int qF = 0;
        TraceSyncWorker wF;
        ChunkStreamDriver dFalseQ{ par };
        dFalseQ.setSeam(tableSeam(td));
        dFalseQ.setWorker(&wF);
        dFalseQ.setSavedContentQuery([&qF](int, int) { ++qF; return false; });
        DriverSnap snapF;
        replayDriver(dFalseQ, wF, moves1, 2, snapF);

        int qT = 0;
        TraceSyncWorker wT;
        ChunkStreamDriver dTrueQ{ par };
        dTrueQ.setSeam(tableSeam(td));
        dTrueQ.setWorker(&wT);
        dTrueQ.setSavedContentQuery([&qT](int, int) { ++qT; return true; });
        DriverSnap snapT;
        replayDriver(dTrueQ, wT, moves1, 2, snapT);

        bool nullQGen = snapN.executed.size() > 0;
        for (const auto &e : snapN.executed)
            nullQGen = nullQGen && e.kind == GenerationJobKind::Generate;
        const bool falseQTwin = snapsIdentical(snapN, snapF) && qF > 0; // 恒 false ≡ null（双生）
        bool trueQLoad = sameExceptKind(snapN, snapT) && qT > 0; // 键/序/id/stats 恒等……
        for (int i = 0; i < snapT.executed.size(); ++i) // ……唯 kind 翻 Load
            trueQLoad = trueQLoad && snapT.executed[i].kind == GenerationJobKind::Load;
        ok = ok && nullQGen && falseQTwin && trueQLoad;
        if (!nullQGen || !falseQTwin || !trueQLoad)
            diag += QStringLiteral("[kind nullQ=%1 falseQ=%2 trueQ=%3 qF=%4 qT=%5] ")
                        .arg(nullQGen)
                        .arg(falseQTwin)
                        .arg(trueQLoad)
                        .arg(qF)
                        .arg(qT);

        // ⑦ null evictor → toEvict 消费面零调用（重放恒等）；使能 evictor → 候选 ≡ twin
        //    （逐沿：沿 1 @(0,0) 与沿 2 @(1,0) 的窗外驻留候选均 = [(2,2)]）。
        QVector<QVector<GenerationPolicyEvict>> recEdges;
        int recCalls = 0;
        TraceSyncWorker wR2;
        ChunkStreamDriver dRec{ par };
        dRec.setSeam(tableSeam(td));
        dRec.setWorker(&wR2);
        dRec.setEvictor([&](const QVector<GenerationPolicyEvict> &c) {
            ++recCalls;
            recEdges.append(c);
        });
        DriverSnap snapRec;
        replayDriver(dRec, wR2, moves1, 2, snapRec);

        TraceSyncWorker wNone;
        ChunkStreamDriver dNoEvictor{ par }; // evictor null（默认）
        dNoEvictor.setSeam(tableSeam(td));
        dNoEvictor.setWorker(&wNone);
        DriverSnap snapNone;
        replayDriver(dNoEvictor, wNone, moves1, 2, snapNone);

        const auto twinE2 = twinDecide(par, 1, 0, td);
        const bool recOk = recCalls == 2 && recEdges.size() == 2
            && recEdges[0] == twinD.toEvict && recEdges[1] == twinE2.toEvict
            && twinD.toEvict.size() == 1 && twinD.toEvict[0].cx == 2
            && twinD.toEvict[0].cz == 2 && twinE2.toEvict.size() == 1;
        const bool nullEvictorTwin = snapsIdentical(snapNone, snapRec);
        ok = ok && recOk && nullEvictorTwin;
        if (!recOk || !nullEvictorTwin)
            diag += QStringLiteral("[evictor rec=%1 cands0=%2 twin0=%3 twin2=%4 none=%5] ")
                        .arg(recCalls)
                        .arg(recEdges.value(0).size())
                        .arg(twinD.toEvict.size())
                        .arg(twinE2.toEvict.size())
                        .arg(nullEvictorTwin);

        // ⑧ 确定性：同候选序 × 双 fresh evictor（同 rig 初态）→ 调用轨迹逐位恒等 + 报告恒等。
        const auto buildRig = [](EvSeamRig &rig, QSet<quint64> &ds) {
            rig.table.insert(EvSeamRig::key(0, 0), ChunkLifecycle::Loaded);
            rig.table.insert(EvSeamRig::key(1, 0), ChunkLifecycle::Loaded);
            rig.table.insert(EvSeamRig::key(2, 0), ChunkLifecycle::Active);
            rig.table.insert(EvSeamRig::key(0, 1), ChunkLifecycle::Generated);
            ds.insert(EvSeamRig::key(1, 0));
        };
        EvSeamRig rA, rB;
        QSet<quint64> dA, dB;
        buildRig(rA, dA);
        buildRig(rB, dB);
        const QVector<GenerationPolicyEvict> candsD
            = { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } }; // 混合态候选序（Active/Loaded/非驻留）
        ChunkEvictor evA, evB;
        evA.setTransitionFn([&rA](int cx, int cz, ChunkLifecycle t) { return rA.transition(cx, cz, t); });
        evA.setDirtyQueryFn([&dA, &kkey](int cx, int cz) { return dA.contains(kkey(cx, cz)); });
        evA.setPersistFn([&rA](int cx, int cz) { return rA.persist(cx, cz); });
        evB.setTransitionFn([&rB](int cx, int cz, ChunkLifecycle t) { return rB.transition(cx, cz, t); });
        evB.setDirtyQueryFn([&dB, &kkey](int cx, int cz) { return dB.contains(kkey(cx, cz)); });
        evB.setPersistFn([&rB](int cx, int cz) { return rB.persist(cx, cz); });
        const ChunkEvictor::Report repA = evA.evict(candsD);
        const ChunkEvictor::Report repB = evB.evict(candsD);
        bool detTraces = rA.calls.size() == rB.calls.size() && !rA.calls.isEmpty();
        for (int i = 0; i < rA.calls.size() && detTraces; ++i) {
            detTraces = rA.calls[i].persist == rB.calls[i].persist && rA.calls[i].cx == rB.calls[i].cx
                && rA.calls[i].cz == rB.calls[i].cz && rA.calls[i].target == rB.calls[i].target
                && rA.calls[i].accepted == rB.calls[i].accepted;
        }
        const bool detReports = repA.persisted == repB.persisted && repA.evicted == repB.evicted
            && repA.aborted == repB.aborted && repA.skipped == repB.skipped;
        ok = ok && detTraces && detReports && repA.evicted > 0; // 非空转
        if (!detTraces || !detReports || repA.evicted <= 0)
            diag += QStringLiteral("[det traces=%1 reports=%2 e=%3 n=%4] ")
                        .arg(detTraces)
                        .arg(detReports)
                        .arg(repA.evicted)
                        .arg(rA.calls.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2019d structure pins and kind selection: compile-time and"
                             " comment-stripped pins hold the evictor QObject-free with"
                             " its three seams, evict entry and legal-edge drive lines"
                             " while reverse probes prove no QObject/QML/thread/World/"
                             "ChunkManager/SaveCoordinator/setLifecycle tokens and zero"
                             " production wiring; the driver header pins both P3 injection"
                             " faces with Mesh forbidden and Load only at the seam-gated"
                             " selection; a null saved-content query stays all-Generate"
                             " and bit-identical to a seam-free driver, an always-true"
                             " query flips the same keys to Load with identical ids; a"
                             " null evictor replays identically while a recording evictor"
                             " receives the twin toEvict list; two fresh evictors replay"
                             " the same candidates with bit-identical traces and reports"
                          << (ok ? QString() : diag);
    });
}
