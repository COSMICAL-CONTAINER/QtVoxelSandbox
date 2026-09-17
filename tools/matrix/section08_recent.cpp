// tools/matrix/section08_recent.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L43625-47995 逐字节搬移（段 md5: 66ccd46caccf57e8a178f485a5747606；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section08_recent()
{

    // (b) AO 行为级：确定性 rig 场景（3×3 石台 @y=42 + L 形双墙 @y=43）→ 中心格顶面四角点的
    //     遮挡档位精确可算。遮挡判据 = occludesNeighborFace（与邻面剔除同谓词）；因子曲线 =
    //     VoxelLight::kAoFactor {1.0, 0.8, 0.6, 0.5}，双侧同遮钳 3。断言以「无遮挡角点色 = flat」
    //     为基准（对光场值稳健），AO 角点 = flat × 曲线值精确钉。
    runLegMulti({ "t1023b ambient-occlusion toggle (smooth-lighting research first small step): terrain culled path"
        " gains per-corner classic MC AO - three occlusion probes per vertex (side/side/diagonal around t"
        "he face's neighbor cell, occluder predicate = occludesNeighborFace, the same authority as face c"
        "ulling), factor curve kAoFactor {1.0,0.8,0.6,0.5} with the both-sides-clamp-to-3 rule, multiplie"
        "d after the light-field clamp (contact shadows may dip below kVcMin); default OFF keeps vertex c"
        "olors bit-identical (zero-cost bypass), ON pins the exact corner ladder {0.5, 0.8, 0.8, 1.0} x f"
        "lat on an L-wall rig (diagonal corner double-clamped, two single-side, one open), and OFF again "
        "restores the vertex buffer byte-for-byte (revert lever is the one window.aoEnabled switch); gree"
        "dy/fluid/partial segments deliberately not sampled (merge-key/view contract registered in the t1"
        "023 report)diag flat=%1 v=%2/%3 okFlat=%4 okRt=%5 %6" }, [&]() {
        const auto [xb, zb] = nextSlot();
        const int x1 = xb + 1, z1 = zb; // 中心格（足印 ±2；行距 3 内不蹭邻行器件）
        const int cy = 42;              // 平台层（kRigY+1）
        // 清场盒 5×5×7（几何未附前置 Air 便宜——无重建风暴）：扫净 rig 残余/上界到 46。
        for (int x = x1 - 2; x <= x1 + 2; ++x)
            for (int z = z1 - 2; z <= z1 + 2; ++z)
                for (int y = 40; y <= 46; ++y)
                    w.setBlock(x, y, z, BR::Air, 0);
        ChunkGeometry geo;
        geo.setWorld(&w);
        geo.setCx(x1 / 16);
        geo.setCz(z1 / 16);
        // 平台 3×3 + L 形双墙（-X 侧 + -Z 侧）：中心格顶面 (-x,-z) 角双侧同遮钳 3 → 0.5，
        //   (+x,-z)/(-x,+z) 单侧 → 0.8，(+x,+z) 无遮挡 → 1.0。
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(x1 + dx, cy, z1 + dz, BR::Stone, 0);
        w.setBlock(x1 - 1, cy + 1, z1, BR::Stone, 0);
        w.setBlock(x1, cy + 1, z1 - 1, BR::Stone, 0);

        const int lx = x1 - (x1 / 16) * 16, lz = z1 - (z1 / 16) * 16;
        // 顶点直读（stride 48B = 12 float：pos3+normal3+uv2+color4；Vtx 布局 x,y,z,nx,ny,nz,u,v,r,g,b,a
        //   → ny = float[4]、r = float[8]——t1023 首跑教训：角点色断言最初误取 float[5]（nz），滤出的是
        //   +Z 侧面顶点而非顶面 → 全线假值 + (1,1) 角 n=0 假红）。ultra 面隔离：同角位常有多张共角顶面
        //   （邻格平台块顶面 / 树冠叶顶），各面光场与 AO 探针集不同 → 「按坐标收集全部顶点色断言同值」
        //   在真实地形（树冠遮天天光有梯度）不成立。改为 **四连顶点四边形隔离**：culled 路径每面 4 角点
        //   连续 append（base..base+3），找出「4 个连续顶点全为 y=43 顶面且位置集恰为中心格顶面四角」
        //   的四元组——单位方格四角仅中心面唯一覆盖（邻面只共边），与邻块/树冠顶点天然隔离。
        const auto findCenterQuad = [&](const QByteArray &buf) -> std::vector<float> {
            // 返回按 {(-1,-1),(+1,-1),(+1,+1),(-1,+1)} 角序（= F.c[2] 的 (x,z) 序）排列的 4 个 r 值；
            // 找不到四元组返回空。
            const float *vf = reinterpret_cast<const float *>(buf.constData());
            const int floats = int(buf.size()) / int(sizeof(float));
            const int vTotal = floats / 12;
            for (int v = 0; v + 4 <= vTotal; ++v) {
                const float *q = vf + v * 12;
                bool allTop = true;
                for (int k = 0; k < 4 && allTop; ++k) {
                    allTop = q[k * 12 + 4] > 0.99f                     // ny（float[4]，非 nz[5]）
                             && qFuzzyCompare(q[k * 12 + 1], float(cy + 1));
                }
                if (!allTop) continue;
                // 四角位置集恰为 (lx..lx+1) × (lz..lz+1)（每角恰好出现一次）。
                bool match[2][2] = { { false, false }, { false, false } };
                for (int k = 0; k < 4; ++k) {
                    const float vx = q[k * 12 + 0], vz = q[k * 12 + 2];
                    const int ix = qFuzzyCompare(vx, float(lx)) ? 0 : (qFuzzyCompare(vx, float(lx + 1)) ? 1 : -1);
                    const int iz = qFuzzyCompare(vz, float(lz)) ? 0 : (qFuzzyCompare(vz, float(lz + 1)) ? 1 : -1);
                    if (ix < 0 || iz < 0 || match[ix][iz]) { allTop = false; break; }
                    match[ix][iz] = true;
                }
                if (!allTop) continue;
                // F.c[2]（+Y 面）cc 序：(0,1),(1,1),(1,0),(0,0) → r 序 = (0,1),(1,1),(1,0),(0,0) 角。
                return { q[8], q[20], q[32], q[44] };
            }
            return {};
        };

        const QByteArray flatBuf = geo.vertexData(); // aoEnabled 默认 false（出厂关）
        const int vCount = geo.vertexCount();
        // 平坦基线：AO 关 → 顶点色与 t1023 前逐位同公式 → 四角同色（平坦语义；值随光场，树冠遮天天光梯度下 < 1 合法）。
        const std::vector<float> flatQ = findCenterQuad(flatBuf);
        bool okFlat = vCount > 0 && flatQ.size() == 4;
        float flat = -1.0f;
        if (okFlat) {
            flat = flatQ[2]; // (1,1) 无遮挡角
            okFlat = flat > 0.0f;
            for (float r : flatQ)
                okFlat = okFlat && std::fabs(r - flat) < 1e-5f;
        }

        geo.setAoEnabled(true); // Dirty 重建（同 setGreedyMeshing 路径）
        const QByteArray aoBuf = geo.vertexData();
        const int vCountAo = geo.vertexCount();
        bool okAo = vCountAo == vCount; // AO 不改拓扑，只改顶点色
        const std::vector<float> aoQ = findCenterQuad(aoBuf);
        okAo = okAo && aoQ.size() == 4;
        QString diagAo;
        if (aoQ.size() == 4) {
            const struct { int idx; float ao; } ladder[4] = {
                { 3, 0.5f }, { 2, 0.8f }, { 0, 0.8f }, { 1, 1.0f }, // F.c[2] cc 序 (x,z)：cc3=(0,0) 双侧钳 3 / cc2=(1,0)+cc0=(0,1) 单侧 / cc1=(1,1) 无遮挡
            };
            for (const auto &c : ladder) {
                const float expect = flat * c.ao; // 乘在光场钳制后（接触阴影暗角语义）
                const bool good = std::fabs(aoQ[c.idx] - expect) < 1e-5f;
                okAo = okAo && good;
                if (!good)
                    diagAo += QStringLiteral("[corner%1 r=%2 expect=%3] ")
                                  .arg(c.idx).arg(aoQ[c.idx], 0, 'f', 4).arg(expect, 0, 'f', 4);
            }
        } else {
            diagAo += QStringLiteral("[quad not found n=%1] ").arg(int(aoQ.size()));
        }

        geo.setAoEnabled(false); // round-trip：关回后顶点缓冲逐字节复原（可回退行为级）
        const bool okRt = geo.vertexData() == flatBuf;

        // rig 清洁（t860 先例）：拆场景。
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                w.setBlock(x1 + dx, cy, z1 + dz, BR::Air, 0);
        w.setBlock(x1 - 1, cy + 1, z1, BR::Air, 0);
        w.setBlock(x1, cy + 1, z1 - 1, BR::Air, 0);

        const bool okT1023b = okFlat && okAo && okRt;
        if (!okT1023b) ++totalFail;
        qInfo().noquote() << (okT1023b ? "PASS" : "FAIL")
                          << "| t1023b ambient-occlusion toggle (smooth-lighting research first small"
                             " step): terrain culled path gains per-corner classic MC AO - three"
                             " occlusion probes per vertex (side/side/diagonal around the face's"
                             " neighbor cell, occluder predicate = occludesNeighborFace, the same"
                             " authority as face culling), factor curve kAoFactor {1.0,0.8,0.6,0.5}"
                             " with the both-sides-clamp-to-3 rule, multiplied after the light-field"
                             " clamp (contact shadows may dip below kVcMin); default OFF keeps"
                             " vertex colors bit-identical (zero-cost bypass), ON pins the exact"
                             " corner ladder {0.5, 0.8, 0.8, 1.0} x flat on an L-wall rig (diagonal"
                             " corner double-clamped, two single-side, one open), and OFF again"
                             " restores the vertex buffer byte-for-byte (revert lever is the one"
                             " window.aoEnabled switch); greedy/fluid/partial segments deliberately"
                             " not sampled (merge-key/view contract registered in the t1023 report)"
                          << (okT1023b ? QString()
                                       : QStringLiteral("diag flat=%1 v=%2/%3 okFlat=%4 okRt=%5 %6")
                                             .arg(flat, 0, 'f', 4).arg(vCount).arg(vCountAo)
                                             .arg(okFlat).arg(okRt).arg(diagAo));
    });

    // (c) meshing 线程模式事实钉（t906 复核；R20.12 同变更修订；r2020/D6 再修订；r2026/W4 三修
    //     = 纠偏留痕非放宽——本单起网格执行器经 W4 接线宿主（gamesession.h，sparse 流式会话）
    //     生产构造，F3 `threads: 0/0 (sync meshing)` 行如实表意为 **fixed 世界**（app 现行唯一
    //     世界模式）GUI 线程同步 meshing 事实；sparse 流式世界的异步 meshing 可见面 = win 行
    //     mesh 段的 worker 列[chunkgeometry 交付回调计数 meshNworker]。白名单扫描零变化：仍
    //     仅 std::thread、仍仅双文件白名单——W4 零新增线程原语落点[复用 meshworker.h]；任何
    //     新增线程原语文件/记号仍必须同变更更新本探针，防「F3 谎报 / 野线程潜入」。
    runLegMulti({ "t1023c sync-meshing fact pin (t906 recheck; R20.12 amended, dual-site; W4"
        " amended): src tree threading-primitive hits (QThreadPool/QThread/QtConcurrent/QF"
        "uture/moveToThread/std::thread/std::async) are sanctioned only in the dual-file wh"
        "itelist World/backgroundgeneration.h (background generation worker) + World/meshw"
        "orker.h (worker meshing executor, now production-wired by the W4 sparse-session ho"
        "st gamesession.h -- zero new primitive sites) with std::thread (both workers ban Q"
        "Object, see their headers), and the F3 line 'threads: 0/0 (sync meshing)' stays p"
        "inned as the FIXED-world fact (the app's only world mode until W5: meshing is sti"
        "ll synchronous on the GUI thread via ChunkGeometry direct-connected slots; sparse"
        " streaming worlds report their async meshing via the win-line worker column counte"
        "d at harvest delivery); any new primitive site (incl. mesher threading per t1023 r"
        "eport section 1.3) must update both the F3 line and this probe in the same changediag"
        " files=%1 hits=%2 f3=%3 %4" }, [&]() {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString srcRoot = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        if (!QDir(srcRoot).exists()) {
            // B-P2-1：src/ 找不到 = 布局破坏 —— 事实钉失去锚点即失察面，同 (a) 改响红。
            ++totalFail;
            qInfo().noquote() << "FAIL"
                              << "| t1023c sync-meshing fact pin: src/ tree not found near exe"
                                 " (exeDir/../src) - layout broken, fact pin loud-miss"
                                 " (review0907 B-P2-1: was a silent note that skipped the pin)";
        } else {
            const QStringList tokens = {
                QStringLiteral("QThreadPool"), QStringLiteral("QThread"), QStringLiteral("QtConcurrent"),
                QStringLiteral("QFuture"), QStringLiteral("moveToThread"), QStringLiteral("std::thread"),
                QStringLiteral("std::async"),
            };
            // r2020 双文件白名单（纠偏留痕非放宽）：仍仅 std::thread，落点从单文件扩为两 worker 头。
            const QString kSanctionedBg = QStringLiteral("World/backgroundgeneration.h"); // R20.12 后台 GenerationJob worker
            const QString kSanctionedMw = QStringLiteral("World/meshworker.h"); // D6 MeshWorker（MeshBuilder 线程化执行器，生产零接线）
            const QString kSanctionedTok = QStringLiteral("std::thread");
            int files = 0, hits = 0, sanctionedBgHits = 0, sanctionedMwHits = 0;
            QString hitDetail;
            QDirIterator it(srcRoot, { QStringLiteral("*.cpp"), QStringLiteral("*.h") },
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QFile f(it.next());
                if (!f.open(QIODevice::ReadOnly)) continue;
                ++files;
                const QString content = QString::fromUtf8(f.readAll());
                const QString rel = QDir(srcRoot).relativeFilePath(it.filePath());
                for (const QString &tok : tokens) {
                    if (content.contains(tok)) {
                        ++hits;
                        hitDetail += it.filePath() + QStringLiteral(":") + tok + QStringLiteral(" ");
                        if (tok == kSanctionedTok) {
                            if (rel == kSanctionedBg) ++sanctionedBgHits; // 受认可落点①
                            else if (rel == kSanctionedMw) ++sanctionedMwHits; // 受认可落点②
                        }
                    }
                }
            }
            bool f3Present = false;
            QFile mf(QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")));
            if (mf.open(QIODevice::ReadOnly))
                f3Present = QString::fromUtf8(mf.readAll())
                                .contains(QStringLiteral("threads: 0/0 (sync meshing)"));
            // 命中面 = 全部命中都在受认可落点（且**每个**白名单落点在场 ≥1 命中——防「worker
            // 文件被挪走后事实钉空转」）；F3 行照常钉（mesher 生产路径仍同步）。
            const int sanctionedHits = sanctionedBgHits + sanctionedMwHits;
            const bool everySanctionedSitePresent = sanctionedBgHits >= 1 && sanctionedMwHits >= 1;
            const bool okThreadPin = files > 0 && hits > 0 && hits == sanctionedHits
                && everySanctionedSitePresent && f3Present;
            if (!okThreadPin) ++totalFail;
            qInfo().noquote() << (okThreadPin ? "PASS" : "FAIL")
                              << "| t1023c sync-meshing fact pin (t906 recheck; R20.12 amended,"
                                 " dual-site; W4 amended): src tree threading-primitive hits"
                                 " are sanctioned only in the dual-file whitelist"
                                 " World/backgroundgeneration.h (background generation"
                                 " worker) + World/meshworker.h (worker meshing executor,"
                                 " production-wired by the W4 sparse-session host"
                                 " gamesession.h - zero new primitive sites) with std::thread"
                                 " (both workers ban QObject), across"
                              << files
                              << "files, and the F3 line 'threads: 0/0 (sync meshing)' stays"
                                 " pinned as the FIXED-world fact (meshing still synchronous"
                                 " on the GUI thread there; sparse streaming worlds report"
                                 " async meshing via the win-line worker column); any new"
                                 " primitive site (incl. mesher threading, t1023 report"
                                 " section 1.3) must update both the F3 line and this probe"
                                 " in the same change"
                              << (okThreadPin ? QString()
                                              : QStringLiteral("diag files=%1 hits=%2 f3=%3 every=%4 %5")
                                                    .arg(files).arg(hits).arg(f3Present)
                                                    .arg(everySanctionedSitePresent).arg(hitDetail));
        }
    });

    // ── P-t1024a 睡觉跳夜 + 床位重生锚 + 拒睡门 + 雷暴可睡 + 播报来源契约（R19.21 t1024；review0909
    //    #1/#5 清偿；t388/t457 状态机之上的完整床语义）──
    //   (a) 夜间入睡即设锚（MC「睡上即设重生点」口径）+ 跳夜时间面：Settled 满 2s 自动 sleepAdvanceToDawn
    //       → skipToDawn 精确跳清晨 phase 0.75（子夜 0.5 起跳 = +0.25 周期，float 短往返逐位还原）；
    //   (b) 重生回床位：respawn() → m_pos = m_spawnPos（床位）→ snapSpawnToGround 贴床顶（heightAt
    //       返床层 → +1 = 床顶，feet == 床位）；
    //   (c) 敌对拒睡门（阴性轮敏感：摘门 → 本腿红）：床周 8 格内 MobShambler（hostile）→ sleepRefused
    //       「你不能休息，附近有怪物」（spec 文案）+ 零位移 + 不清锚；
    //   (d) 雷暴可睡 + 醒来清雷暴（review0909 #1 wiki 考据翻转：MC「You can sleep only at night or
    //       during thunderstorms」，雷暴 = 合法入睡窗口；阴性轮敏感：摘 sleepAdvanceToDawn 清雷暴行
    //       → okThunderClear 腿红）：夜 + Thunder → 入睡成功；Settled 满 2s 自动跳晨 → weather 回
    //       Clear（睡醒清雷暴）+ 时间面照常落 0.75；白天 + Clear → 拒「只能在夜晚或雷暴中睡觉」；
    //       白天 + Thunder → 亦可睡但 wakeUp 按钮早退**不清**天气（清只挂跳晨完成沿）；夜 + Clear →
    //       可睡（正控制：(c)(d) 门是唯一拦截者）；
    //   (e) 播报来源契约（review0909 #5；t1036 信号职责分离随迁；阴性轮敏感：摘 bedSpawnAnnounce
    //       发射 → announce 断言腿红）：bedSpawnValidChanged() 无参（纯属性 NOTIFY，valid 沿时序
    //       不变——置假沿 → 回填沿 → 入睡设锚沿三连发，valid 值 false/true/true）；bedSpawnAnnounce
    //       (bool restored) 只在入睡设锚沿恰发一次（restored=false，QML 播「重生点已设置」）——
    //       读档回填沿（setBedSpawn）与置假沿（clearBedSpawn）零 announce（QML 静默）。
    runLegMulti({ "t1024a bed semantics: right-clicking a bed at night starts sleep AND anchors the respawn point a"
        "t the bed the moment sleep begins (MC sleep-sets-spawn semantics; bedSpawnValid is a real Q_PROP"
        "ERTY resolved via QMetaObject like QML does); the settled timer auto-skips the night with the da"
        "y clock landing exactly on dawn phase 0.75 (the skip-night time face), respawn() then places the"
        " player back on the bed top; an hostile shambler within the 8-block bed radius refuses sleep wit"
        "h the spec message (zero displacement, anchor kept); a thunderstorm is a LEGAL sleep window (rev"
        "iew0909 #1 wiki caliber: sleeping at night or during thunderstorms) - night+thunder sleeps, fini"
        "shing the sleep clears the storm back to Clear alongside the dawn skip, day+clear refuses with t"
        "he MC wording, day+thunder sleeps but an early wakeUp keeps the storm (only a completed night re"
        "sets weather), and a clear night sleeps (positive control); the announce-source contract (t1036 "
        "signal separation) carries the parameterless validChanged edge sequence false/true/true across c"
        "lear/restore/sleep-set while bedSpawnAnnounce fires exactly once with restored=false on the slee"
        "p-set edge (QML announces) and zero times on the save-restore and clear edges (QML silent) - neg"
        "ative-round sensitivediag entry=%1 dawn=%2 respawn=%3 hostile=%4 thunder=%5 thunderClear=%6 dayR"
        "efuse=%7 dayThunder=%8 wakeKeepsStorm=%9 control=%10 restoreQuiet=%11 sleepAnnounce=%12" }, [&]() {
        World wT24;
        wT24.setWidth(48); wT24.setDepth(48); wT24.setHeight(96); wT24.setSeed(1024);
        // regenerate（worldgen 地形；heightAt 纯函数与栅格同源）：respawn 的 snapSpawnToGround 贴
        //   heightAt 地表 → 床必须贴地表放置，重生落点才恰为床顶（浮空 rig 下 heightAt 返 fBm 地表
        //   而非床层，断言按 rig 可达域纸面推演）。在 pc.setWorld 之前跑（seedChanged 无监听者）。
        wT24.regenerate(1024);
        int bx24 = 8, bz24 = 8, by24 = wT24.heightAt(8, 8);
        for (int cx = 9; cx <= 20; ++cx) { // 取扫描段最高地表列（必为陆地；水位 58，plains ≥ ~60）
            const int h = wT24.heightAt(cx, bz24);
            if (h > by24) { by24 = h; bx24 = cx; }
        }
        WorldClock clockT24;
        EntityManager entsT24;
        PlayerController pcT24;
        pcT24.setWorld(&wT24);
        pcT24.setWorldClock(&clockT24);
        pcT24.setEntityManager(&entsT24);
        QQuickWindow probeWinT24;
        pcT24.setParentItem(probeWinT24.contentItem());
        pcT24.grab(); // m_captured（updateSleep 只在 captured 路径跑；t891/R8 同款）
        const auto pumpT24 = [&pcT24](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            pcT24.tick();
        };
        // 工作体积清空（床层 h..h+4 净空；坡地补支撑）→ 床贴地表（in-game 床位格语义：床顶 = h+1
        // = heightAt+1 = respawn 贴地表落点）。
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 4; ++dy)
                    wT24.setBlock(bx24 + dx, by24 + dy, bz24 + dz, BR::Air, 0);
        wT24.setBlock(bx24, by24 - 1, bz24, BR::Stone, 0);     // 坡地兜底支撑（地形缺口防御）
        wT24.setBlock(bx24 - 1, by24 - 1, bz24, BR::Stone, 0);
        wT24.setBlock(bx24, by24, bz24, BR::BedWhite, quint8(0));     // foot（D=+X）
        wT24.setBlock(bx24 - 1, by24, bz24, BR::BedWhite, quint8(8)); // head
        QString refusedT24;
        int refusedCountT24 = 0;
        const QMetaObject::Connection connR24 = QObject::connect(
            &pcT24, &PlayerController::sleepRefused, &pcT24,
            [&refusedT24, &refusedCountT24](const QString &r) { refusedT24 = r; ++refusedCountT24; });
        // review0909 #5 播报来源契约（t1036 双信号各自记录）：bedSpawnValidChanged() 无参——每次
        //   发射记录发射时的属性值（valid 沿时序不变）；bedSpawnAnnounce(bool restored) 单独记录
        //   restored 携带值（QML 播报面 = 入睡设锚沿恰一次 restored=false；回填 / 置假沿零 announce）。
        QVector<bool> emitValidT24, emitAnnounceRestoredT24;
        const QMetaObject::Connection connE24 = QObject::connect(
            &pcT24, &PlayerController::bedSpawnValidChanged, &pcT24,
            [&pcT24, &emitValidT24]() { emitValidT24.push_back(pcT24.bedSpawnValid()); });
        const QMetaObject::Connection connA24 = QObject::connect(
            &pcT24, &PlayerController::bedSpawnAnnounce, &pcT24,
            [&emitAnnounceRestoredT24](bool restored) { emitAnnounceRestoredT24.push_back(restored); });

        // (a) 夜间入睡即设锚 + 跳清晨（setPhase(0.5)=子夜；sleep Lying 1s + Settled 2s + Waking 0.8s）。
        clockT24.setPhase(0.5f);
        pcT24.trySleepAt(bx24, by24, bz24);
        // QML 契约面（review27 #1 同族）：bedSpawnValid 必须在 metaobject 属性表内（QML 属性语法
        // player.bedSpawnValid 走 QMetaObject 解析；C++ 直调探针测不到该面）。
        const QMetaObject *moT24 = pcT24.metaObject();
        const int propIdxT24 = moT24->indexOfProperty("bedSpawnValid");
        const bool metaAnchor = propIdxT24 >= 0 && moT24->property(propIdxT24).read(&pcT24).toBool();
        const bool okEntry = pcT24.sleeping() && pcT24.bedSpawnValid() && metaAnchor
            && pcT24.spawnPoint() == QVector3D(float(bx24) + 0.5f, float(by24) + 1.0f, float(bz24) + 0.5f);
        for (int t = 0; t < 400 && pcT24.sleeping(); ++t) pumpT24(17);
        const bool okDawn = !pcT24.sleeping()
            && std::abs(clockT24.dayPhase() - 0.75f) < 1e-3f; // 跳夜时间面：昼夜钟被置到清晨
        // (b) 重生回床位：respawn → m_spawnPos（床位）→ snapSpawnToGround 贴 heightAt 地表
        //     （床贴地表放置 → 落点恰为床顶 = 床位）。
        pcT24.respawn();
        const QVector3D bedSpawnT24(float(bx24) + 0.5f, float(by24) + 1.0f, float(bz24) + 0.5f);
        const bool okRespawn = pcT24.bedSpawnValid() && pcT24.feetPosition() == bedSpawnT24;
        // (c) 敌对拒睡门：床周 3 格 MobShambler（hostile，床位同层净空格）→ 拒 + 零位移 + 锚保留。
        entsT24.spawnMobTyped(bx24 + 3, by24, bz24, EntityManager::MobShambler, QStringLiteral("#3f7f3f"), 10);
        clockT24.setPhase(0.5f); // 夜（setPhase 特权指令，与睡觉单向不冲突）
        const QVector3D beforeHostile = pcT24.feetPosition();
        refusedT24.clear();
        pcT24.trySleepAt(bx24, by24, bz24);
        const bool okHostile = !pcT24.sleeping() && refusedT24 == QStringLiteral("你不能休息，附近有怪物")
            && pcT24.feetPosition() == beforeHostile && pcT24.bedSpawnValid(); // 拒睡不清锚
        // (d) 雷暴可睡 + 醒来清雷暴 + 白天拒睡（review0909 #1 考据翻转：MC wiki Bed「You can sleep
        //     only at night or during thunderstorms」——旧「雷暴拒睡」反 MC，已翻为可睡窗口）。
        entsT24.clearAll();
        wT24.setWeatherState(3); // Thunder
        refusedT24.clear();
        pcT24.trySleepAt(bx24, by24, bz24);
        const bool okThunder = pcT24.sleeping(); // 夜 + 雷暴 → 入睡成功（不再拒）
        for (int t = 0; t < 400 && pcT24.sleeping(); ++t) pumpT24(17);
        // 跳晨完成沿 → 雷暴被清回 Clear + 时间面照常落清晨 0.75（阴性轮敏感：摘 sleepAdvanceToDawn
        // 清雷暴行 → 本腿红）。
        const bool okThunderClear = !pcT24.sleeping() && wT24.weatherState() == 0
            && std::abs(clockT24.dayPhase() - 0.75f) < 1e-3f;
        // 白天 + Clear → 拒（新口径文案：MC 拒睡语「夜 / 雷暴二选一」窗口面）。
        clockT24.setPhase(0.2f); // 白天
        refusedT24.clear();
        pcT24.trySleepAt(bx24, by24, bz24);
        const bool okDayRefuse = !pcT24.sleeping()
            && refusedT24 == QStringLiteral("只能在夜晚或雷暴中睡觉");
        // 白天 + Thunder → 亦可睡（MC 白天雷暴窗口）；wakeUp 按钮早退**不清**天气（清只挂跳晨完成沿，
        // MC「真正睡过夜才重置天气」语义）。
        wT24.setWeatherState(3);
        pcT24.trySleepAt(bx24, by24, bz24);
        const bool okDayThunder = pcT24.sleeping();
        pcT24.wakeUp();
        const bool okWakeKeepsStorm = !pcT24.sleeping() && wT24.weatherState() == 3;
        // 正控制：无敌对 + 无雷暴的夜可睡（证明 (c)(d) 门是唯一拦截者）。
        wT24.setWeatherState(0); // Clear
        clockT24.setPhase(0.5f); // 夜（setPhase 特权指令，与睡觉单向不冲突）
        pcT24.trySleepAt(bx24, by24, bz24);
        const bool okControl = pcT24.sleeping();
        pcT24.wakeUp();
        // (e) 播报来源契约（review0909 #5；t1036 随迁）：validChanged 无参沿时序不变（置假沿 →
        //     回填沿 → 入睡设锚沿，valid 值 false/true/true）；announce 只在入睡设锚沿恰一次
        //     restored=false——回填沿与置假沿零 announce（QML 静默）。
        const int nValidT24 = emitValidT24.size();
        const int nAnnounceT24 = emitAnnounceRestoredT24.size();
        pcT24.clearBedSpawn(); // 置假沿（QML 只播置真沿，此向静默；无 announce）
        pcT24.setBedSpawn(float(bx24) + 0.5f, float(by24) + 1.0f, float(bz24) + 0.5f); // 回填沿（enterWorld 同款；无 announce）
        const bool okRestoreQuiet = emitValidT24.size() == nValidT24 + 2
            && !emitValidT24[nValidT24]                                        // 置假沿 valid=false
            && emitValidT24[nValidT24 + 1]                                     // 回填沿 valid=true
            && emitAnnounceRestoredT24.size() == nAnnounceT24;                 // 两沿零 announce
        pcT24.trySleepAt(bx24, by24, bz24); // 入睡设锚沿（validChanged + announce(false) 双发）
        const bool okSleepAnnounce = pcT24.sleeping()
            && emitValidT24.size() == nValidT24 + 3 && emitValidT24[nValidT24 + 2]
            && emitAnnounceRestoredT24.size() == nAnnounceT24 + 1
            && !emitAnnounceRestoredT24[nAnnounceT24];                         // announce 恰一次 restored=false
        pcT24.wakeUp();
        QObject::disconnect(connR24);
        QObject::disconnect(connE24);
        QObject::disconnect(connA24);
        pcT24.release();
        probeWinT24.deleteLater();
        const bool okA = okEntry && okDawn && okRespawn && okHostile && okThunder && okThunderClear
            && okDayRefuse && okDayThunder && okWakeKeepsStorm && okControl && okRestoreQuiet
            && okSleepAnnounce;
        if (!okA)
            qInfo().noquote() << "  [t1024a diag] entry" << okEntry << "dawn" << okDawn
                              << "respawn" << okRespawn << "hostile" << okHostile
                              << "thunder" << okThunder << "thunderClear" << okThunderClear
                              << "dayRefuse" << okDayRefuse << "dayThunder" << okDayThunder
                              << "wakeKeepsStorm" << okWakeKeepsStorm << "control" << okControl
                              << "restoreQuiet" << okRestoreQuiet << "sleepAnnounce" << okSleepAnnounce
                              << "phase" << clockT24.dayPhase() << "weather" << wT24.weatherState()
                              << "refused" << refusedT24 << "n" << refusedCountT24
                              << "emits" << emitValidT24.size() << "announces" << emitAnnounceRestoredT24.size()
                              << "feet" << pcT24.feetPosition().x() << pcT24.feetPosition().y()
                              << pcT24.feetPosition().z()
                              << "spawn" << pcT24.spawnPoint().x() << pcT24.spawnPoint().y()
                              << pcT24.spawnPoint().z();
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1024a bed semantics: right-clicking a bed at night starts sleep AND "
                             "anchors the respawn point at the bed the moment sleep begins (MC "
                             "sleep-sets-spawn semantics; bedSpawnValid is a real Q_PROPERTY resolved "
                             "via QMetaObject like QML does); the settled timer auto-skips the night "
                             "with the day clock landing exactly on dawn phase 0.75 (the skip-night "
                             "time face), respawn() then places the player back on the bed top; an "
                             "hostile shambler within the 8-block bed radius refuses sleep with the "
                             "spec message (zero displacement, anchor kept); a thunderstorm is a "
                             "LEGAL sleep window (review0909 #1 wiki caliber: sleeping at night or "
                             "during thunderstorms) - night+thunder sleeps, finishing the sleep "
                             "clears the storm back to Clear alongside the dawn skip, day+clear "
                             "refuses with the MC wording, day+thunder sleeps but an early wakeUp "
                             "keeps the storm (only a completed night resets weather), and a clear "
                             "night sleeps (positive control); the announce-source contract (t1036 "
                             "signal separation) carries the parameterless validChanged edge sequence "
                             "false/true/true across clear/restore/sleep-set while bedSpawnAnnounce "
                             "fires exactly once with restored=false on the sleep-set edge (QML "
                             "announces) and zero times on the save-restore and clear edges (QML "
                             "silent) - negative-round sensitive"
                          << (okA ? QString()
                                  : QStringLiteral("diag entry=%1 dawn=%2 respawn=%3 hostile=%4 "
                                                   "thunder=%5 thunderClear=%6 dayRefuse=%7 "
                                                   "dayThunder=%8 wakeKeepsStorm=%9 control=%10 "
                                                   "restoreQuiet=%11 sleepAnnounce=%12")
                                        .arg(okEntry).arg(okDawn).arg(okRespawn)
                                        .arg(okHostile).arg(okThunder).arg(okThunderClear)
                                        .arg(okDayRefuse).arg(okDayThunder).arg(okWakeKeepsStorm)
                                        .arg(okControl).arg(okRestoreQuiet).arg(okSleepAnnounce));
    });

    // ── P-t1024b 挖锚床失效链（真实注视挖掘链驱动；阴性轮敏感：摘 finishMiningAt 清锚钩子 → 本腿红）──
    //   床锚经 setBedSpawn（存档恢复入口，enterWorld 同款）设位 → 玩家站床顶 pitch -90（真实捕获 +
    //   updateRaycast 选体）→ beginMining 创造瞬破 → finishMiningAt 床分支：配对格联动清（t428）+
    //   锚床判定 → clearBedSpawn（重生点回世界出生点 kSpawn pristine）+ bedSpawnLost 恰发一次。
    runLegMulti({ "t1024b respawn-anchor invalidation: mining the anchor bed through the real gaze chain (captured "
        "controller, straight-down raycast, creative beginMining) breaks both bed halves via the pair-cle"
        "ar and invalidates the bed spawn in the same finishMiningAt pass - bedSpawnValid flips false, be"
        "dSpawnLost fires exactly once and the spawn point snaps back to the world spawn constant (80,80,"
        "80), so a later death respawns at world spawn instead of a floating bed coordinate (negative-rou"
        "nd sensitive)diag set=%1 broken=%2 invalidated=%3 lost=%4" }, [&]() {
        World wT24b;
        wT24b.setWidth(48); wT24b.setDepth(48); wT24b.setHeight(96); wT24b.setSeed(1025);
        PlayerController pcT24b;
        pcT24b.setWorld(&wT24b);
        QQuickWindow probeWinT24b;
        pcT24b.setParentItem(probeWinT24b.contentItem());
        pcT24b.grab();
        const int bx24b = 8, by24b = 40, bz24b = 8;
        for (int dx = 5; dx <= 10; ++dx)
            for (int dz = 6; dz <= 10; ++dz) {
                for (int dy = 0; dy <= 3; ++dy) wT24b.setBlock(dx, by24b + dy, dz, BR::Air, 0);
                wT24b.setBlock(dx, by24b - 1, dz, BR::Stone, 0);
            }
        wT24b.setBlock(bx24b, by24b, bz24b, BR::BedWhite, quint8(0));
        wT24b.setBlock(bx24b - 1, by24b, bz24b, BR::BedWhite, quint8(8));
        int lostT24b = 0;
        const QMetaObject::Connection connL24 = QObject::connect(
            &pcT24b, &PlayerController::bedSpawnLost, &pcT24b, [&lostT24b]() { ++lostT24b; });
        pcT24b.setBedSpawn(float(bx24b) + 0.5f, float(by24b) + 1.0f, float(bz24b) + 0.5f); // 读档恢复入口
        const bool okSet = pcT24b.bedSpawnValid()
            && pcT24b.spawnPoint() == QVector3D(float(bx24b) + 0.5f, float(by24b) + 1.0f, float(bz24b) + 0.5f);
        // 真实注视挖掘链：站床顶 + pitch -90 → tick 刷选体 → 创造 beginMining 瞬破命中格（锚床 foot）。
        pcT24b.loadSavedState(float(bx24b) + 0.5f, float(by24b) + 1.0f, float(bz24b) + 0.5f,
                              0.0f, -90.0f, 1 /* Creative */);
        pcT24b.tick(); // updateRaycast：垂直向下射线命中脚下床格
        pcT24b.beginMining();
        const bool okBroken = wT24b.blockAt(bx24b, by24b, bz24b) == BR::Air
            && wT24b.blockAt(bx24b - 1, by24b, bz24b) == BR::Air; // 配对格联动清
        const bool okInvalidated = !pcT24b.bedSpawnValid() && lostT24b == 1
            && pcT24b.spawnPoint() == QVector3D(80.0f, 80.0f, 80.0f); // 回世界出生点 kSpawn pristine
        QObject::disconnect(connL24);
        pcT24b.release();
        probeWinT24b.deleteLater();
        const bool okB = okSet && okBroken && okInvalidated;
        if (!okB)
            qInfo().noquote() << "  [t1024b diag] set" << okSet << "broken" << okBroken
                              << "invalidated" << okInvalidated << "lost" << lostT24b
                              << "valid" << pcT24b.bedSpawnValid()
                              << "spawn" << pcT24b.spawnPoint().x() << pcT24b.spawnPoint().y()
                              << pcT24b.spawnPoint().z()
                              << "footId" << wT24b.blockAt(bx24b, by24b, bz24b)
                              << "headId" << wT24b.blockAt(bx24b - 1, by24b, bz24b);
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1024b respawn-anchor invalidation: mining the anchor bed through "
                             "the real gaze chain (captured controller, straight-down raycast, "
                             "creative beginMining) breaks both bed halves via the pair-clear and "
                             "invalidates the bed spawn in the same finishMiningAt pass - "
                             "bedSpawnValid flips false, bedSpawnLost fires exactly once and the "
                             "spawn point snaps back to the world spawn constant (80,80,80), so a "
                             "later death respawns at world spawn instead of a floating bed "
                             "coordinate (negative-round sensitive)"
                          << (okB ? QString()
                                  : QStringLiteral("diag set=%1 broken=%2 invalidated=%3 lost=%4")
                                        .arg(okSet).arg(okBroken).arg(okInvalidated).arg(lostT24b));
    });

    // ── P-t1033a 爆炸清锚床失效链（真实引信链驱动；review0909 #4 遗留清偿；阴性轮敏感：摘
    //    PlayerController::setWorld 内 blockDestroyedBed connect（语义信号断链）→ 本腿红）──
    //   锚床经 setBedSpawn（存档恢复入口，t1024b 同款）设位 → 床旁 TNT 引燃（clearBlockSilent +
    //   spawnPrimedTnt = 踩板 / 机关 / 电力三条引燃链单一尾）→ ents.tick 细步 6.25s（> kPrimedTntFuseSec
    //   5s；固定 dt 无墙钟依赖，t996(b) 同款）→ detonatePrimedTnt → World::destroySphereSilent 球形破坏
    //   （TNT / 苦力怕两爆炸入口共用 = 一处覆盖）→ 床两半消失 + blockDestroyedBed 语义信号逐半双发 →
    //   PlayerController 收口（seedChanged 同款 setWorld 直连）：bedSpawnValid 置假 + bedSpawnLost 恰一次
    //   （两半双信号被 m_bedSpawnValid 门幂等塌缩成一次播报，与玩家挖掘链同汇 clearBedSpawn 单点）+
    //   spawnPoint 回 kSpawn pristine (80,80,80)（重生回世界出生点，不再悬空指已消失床）。
    runLegMulti({ "t1033a explosion invalidates bed anchor: an ignited TNT beside the anchor bed runs the real fuse"
        " chain (PrimedTnt entity, fixed-dt 6.25s > 5s fuse) and detonates through World::destroySphereSi"
        "lent (the single chokepoint shared by TNT and creeper explosions), destroying both bed halves; t"
        "he new blockDestroyedBed semantic signal (one per destroyed half, fired after the world write bu"
        "rst) reaches PlayerController via the seedChanged-style setWorld connect, collapses the double h"
        "it through the m_bedSpawnValid gate into exactly one bedSpawnLost announcement and snaps the spa"
        "wn point back to the pristine world spawn (80,80,80) instead of a floating bed coordinate (negat"
        "ive-round sensitive: removing the connect turns this leg red)diag set=%1 ignited=%2 broken=%3 in"
        "validated=%4 lost=%5" }, [&]() {
        World wT33a;
        wT33a.setWidth(48); wT33a.setDepth(48); wT33a.setHeight(96); wT33a.setSeed(1033);
        EntityManager entsT33a; // PrimedTnt 断言源 + 引信驱动（t996 同款，不挂 PC——爆炸链与玩家物理正交）
        PlayerController pcT33a;
        pcT33a.setWorld(&wT33a); // blockDestroyedBed → onWorldBedBlockDestroyed 直连在此建立
        const int bx33 = 20, by33 = 40, bz33 = 20;
        const auto buildBedRigT33 = [&](World &w, int bx, int by, int bz) {
            for (int dx = -3; dx <= 4; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    w.setBlock(bx + dx, by - 1, bz + dz, BR::Stone, 0); // 石台（爆炸波及无妨，仅承载床）
                    for (int dy = 0; dy <= 4; ++dy)
                        if (w.blockAt(bx + dx, by + dy, bz + dz) != BR::Air)
                            w.setBlock(bx + dx, by + dy, bz + dz, BR::Air, 0);
                }
            w.setBlock(bx, by, bz, BR::BedWhite, quint8(0));     // foot（配对 head 在 -X，t1024b 同款）
            w.setBlock(bx - 1, by, bz, BR::BedWhite, quint8(8)); // head
        };
        buildBedRigT33(wT33a, bx33, by33, bz33);
        int lostT33a = 0;
        const QMetaObject::Connection connL33a = QObject::connect(
            &pcT33a, &PlayerController::bedSpawnLost, &pcT33a, [&lostT33a]() { ++lostT33a; });
        pcT33a.setBedSpawn(float(bx33) + 0.5f, float(by33) + 1.0f, float(bz33) + 0.5f);
        const bool okSet = pcT33a.bedSpawnValid()
            && pcT33a.spawnPoint() == QVector3D(float(bx33) + 0.5f, float(by33) + 1.0f, float(bz33) + 0.5f);
        // 引燃：TNT 格静默清 + PrimedTnt 实体接管（引燃链统一尾，防格内 TntBlock 残留被链式二次引燃）。
        wT33a.setBlock(bx33 + 2, by33, bz33, BR::TntBlock, 0);
        const bool okIgnited = wT33a.clearBlockSilent(bx33 + 2, by33, bz33);
        entsT33a.spawnPrimedTnt(bx33 + 2, by33, bz33);
        for (int i = 0; i < 400; ++i) // 6.25s > 5s 引信（t996(b) 固定 dt 细步同款，无墙钟依赖）
            entsT33a.tick(0.015625, &wT33a, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
        const bool okBroken = wT33a.blockAt(bx33, by33, bz33) == BR::Air
            && wT33a.blockAt(bx33 - 1, by33, bz33) == BR::Air; // 床两半都在球内（foot 距 2 / head 距 3 ≤ r3）被清
        const bool okInvalidated = !pcT33a.bedSpawnValid() && lostT33a == 1
            && pcT33a.spawnPoint() == QVector3D(80.0f, 80.0f, 80.0f); // 恰一次播报 + 回 pristine 世界出生点
        QObject::disconnect(connL33a);
        const bool okA = okSet && okIgnited && okBroken && okInvalidated;
        if (!okA)
            qInfo().noquote() << "  [t1033a diag] set" << okSet << "ignited" << okIgnited
                              << "broken" << okBroken << "invalidated" << okInvalidated
                              << "lost" << lostT33a << "valid" << pcT33a.bedSpawnValid()
                              << "spawn" << pcT33a.spawnPoint().x() << pcT33a.spawnPoint().y()
                              << pcT33a.spawnPoint().z()
                              << "footId" << int(wT33a.blockAt(bx33, by33, bz33))
                              << "headId" << int(wT33a.blockAt(bx33 - 1, by33, bz33));
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1033a explosion invalidates bed anchor: an ignited TNT beside the "
                             "anchor bed runs the real fuse chain (PrimedTnt entity, fixed-dt 6.25s > "
                             "5s fuse) and detonates through World::destroySphereSilent (the single "
                             "chokepoint shared by TNT and creeper explosions), destroying both bed "
                             "halves; the new blockDestroyedBed semantic signal (one per destroyed "
                             "half, fired after the world write burst) reaches PlayerController via "
                             "the seedChanged-style setWorld connect, collapses the double hit "
                             "through the m_bedSpawnValid gate into exactly one bedSpawnLost "
                             "announcement and snaps the spawn point back to the pristine world "
                             "spawn (80,80,80) instead of a floating bed coordinate (negative-round "
                             "sensitive: removing the connect turns this leg red)"
                          << (okA ? QString()
                                  : QStringLiteral("diag set=%1 ignited=%2 broken=%3 invalidated=%4 lost=%5")
                                        .arg(okSet).arg(okIgnited).arg(okBroken)
                                        .arg(okInvalidated).arg(lostT33a));
    });

    // ── P-t1033b 爆炸锚失效幂等面（阴性轮敏感：摘槽内 m_bedSpawnValid 门 / 锚判等 → 对应腿红）──
    //   (i) 未设锚：床上 TNT 照炸（床两半消失）→ bedSpawnLost / bedSpawnValidChanged 均**零**发（无效锚
    //       的用户面零 emit——爆炸链不得给从未睡过床的玩家播「重生点已失效」）；
    //   (ii) 非锚床：锚设在 B 床（同层异位 10 格）→ A 位复置床被炸（同 y 过 Y 门）→ 零 lost、B 锚与
    //       spawnPoint 纹丝不动（判等谓词 x/z 面有判别力；clearBedSpawn 幂等不被无谓触发）。
    runLegMulti({ "t1033b explosion anchor-clear idempotence faces: blasting a bed while no spawn anchor is set des"
        "troys the bed but emits zero user-facing edges (no bedSpawnLost, no bedSpawnValidChanged - playe"
        "rs who never slept keep a silent world); with the anchor set on a different bed ten cells away o"
        "n the same Y layer, re-detonating the first bed still emits nothing and the anchor plus spawn po"
        "int stay untouched (the y-gate + x/z anchor-match predicate discriminates same-layer neighbor be"
        "ds; negative-round sensitive: dropping the m_bedSpawnValid gate or the anchor match turns the ma"
        "tching leg red)diag noAnchor=%1 anchorSet=%2 nonAnchor=%3 lost=%4 fires=%5" }, [&]() {
        World wT33b;
        wT33b.setWidth(48); wT33b.setDepth(48); wT33b.setHeight(96); wT33b.setSeed(1034);
        EntityManager entsT33b;
        PlayerController pcT33b;
        pcT33b.setWorld(&wT33b);
        const int ax33 = 20, ay33 = 40, az33 = 20; // A 位（爆炸靶）
        const int bx33b = 30, by33b = 40, bz33b = 30; // B 位（锚床）
        const auto buildBedRigT33b = [&](World &w, int bx, int by, int bz) {
            for (int dx = -3; dx <= 4; ++dx)
                for (int dz = -2; dz <= 2; ++dz) {
                    w.setBlock(bx + dx, by - 1, bz + dz, BR::Stone, 0);
                    for (int dy = 0; dy <= 4; ++dy)
                        if (w.blockAt(bx + dx, by + dy, bz + dz) != BR::Air)
                            w.setBlock(bx + dx, by + dy, bz + dz, BR::Air, 0);
                }
            w.setBlock(bx, by, bz, BR::BedWhite, quint8(0));
            w.setBlock(bx - 1, by, bz, BR::BedWhite, quint8(8));
        };
        const auto detonateAtT33b = [&](World &w, int tx, int ty, int tz) {
            w.setBlock(tx, ty, tz, BR::TntBlock, 0);
            w.clearBlockSilent(tx, ty, tz);
            EntityManager ents; // 局部管理器：爆完即弃（实体清零，防跨腿串扰）
            ents.spawnPrimedTnt(tx, ty, tz);
            for (int i = 0; i < 400; ++i)
                ents.tick(0.015625, &w, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
        };
        buildBedRigT33b(wT33b, ax33, ay33, az33);
        int lostT33b = 0, validFiresT33b = 0;
        const QMetaObject::Connection connL33b = QObject::connect(
            &pcT33b, &PlayerController::bedSpawnLost, &pcT33b, [&lostT33b]() { ++lostT33b; });
        const QMetaObject::Connection connV33b = QObject::connect(
            &pcT33b, &PlayerController::bedSpawnValidChanged, &pcT33b,
            [&validFiresT33b]() { ++validFiresT33b; });
        // (i) 未设锚炸床。
        detonateAtT33b(wT33b, ax33 + 2, ay33, az33);
        const bool okNoAnchor = wT33b.blockAt(ax33, ay33, az33) == BR::Air // 照炸（爆炸本体不受锚态影响）
            && wT33b.blockAt(ax33 - 1, ay33, az33) == BR::Air
            && lostT33b == 0 && validFiresT33b == 0 && !pcT33b.bedSpawnValid(); // 用户面零 emit
        // (ii) 非锚床炸毁，锚床（B 位）保留。
        buildBedRigT33b(wT33b, bx33b, by33b, bz33b);
        pcT33b.setBedSpawn(float(bx33b) + 0.5f, float(by33b) + 1.0f, float(bz33b) + 0.5f);
        const bool okAnchorSet = pcT33b.bedSpawnValid() && validFiresT33b == 1; // 仅设锚沿 1 发（true）
        buildBedRigT33b(wT33b, ax33, ay33, az33);
        detonateAtT33b(wT33b, ax33 + 2, ay33, az33);
        const bool okNonAnchor = wT33b.blockAt(ax33, ay33, az33) == BR::Air
            && wT33b.blockAt(ax33 - 1, ay33, az33) == BR::Air
            && lostT33b == 0 && validFiresT33b == 1
            && pcT33b.bedSpawnValid()
            && pcT33b.spawnPoint() == QVector3D(float(bx33b) + 0.5f, float(by33b) + 1.0f, float(bz33b) + 0.5f);
        QObject::disconnect(connL33b);
        QObject::disconnect(connV33b);
        const bool okB = okNoAnchor && okAnchorSet && okNonAnchor;
        if (!okB)
            qInfo().noquote() << "  [t1033b diag] noAnchor" << okNoAnchor << "anchorSet" << okAnchorSet
                              << "nonAnchor" << okNonAnchor << "lost" << lostT33b
                              << "validFires" << validFiresT33b << "valid" << pcT33b.bedSpawnValid();
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1033b explosion anchor-clear idempotence faces: blasting a bed "
                             "while no spawn anchor is set destroys the bed but emits zero user-"
                             "facing edges (no bedSpawnLost, no bedSpawnValidChanged - players who "
                             "never slept keep a silent world); with the anchor set on a different "
                             "bed ten cells away on the same Y layer, re-detonating the first bed "
                             "still emits nothing and the anchor plus spawn point stay untouched "
                             "(the y-gate + x/z anchor-match predicate discriminates same-layer "
                             "neighbor beds; negative-round sensitive: dropping the m_bedSpawnValid "
                             "gate or the anchor match turns the matching leg red)"
                          << (okB ? QString()
                                  : QStringLiteral("diag noAnchor=%1 anchorSet=%2 nonAnchor=%3 lost=%4 fires=%5")
                                        .arg(okNoAnchor).arg(okAnchorSet).arg(okNonAnchor)
                                        .arg(lostT33b).arg(validFiresT33b));
    });

    // ── P-t1033c 非玩家口径登记钉（水冲不触床）+ 玩家挖掘既有链回归（双链不双播报）──
    //   (i) 水冲：锚床贴邻落水源 → tickWaterFlow 推进 → 流水工作面成立（邻格成流）但床格仍 Bed——
    //       isAttachableBlock 冲刷清单（火把 / 红石火把 / 蛛网 / 木梯 / 铁轨三族，t1012④ 单一权威；
    //       t1043 裁-1 后 rail 族入清单——r2032c 文案对账同步，review0913 #3）不含床 → 床非
    //       附着块水冲不触（口径登记探针钉）→ 锚保留零 lost；
    //   (ii) 挖掘回归：同一锚床经真实注视挖掘链（t1024b 同款：站床顶 pitch -90 → 创造瞬破）挖除 →
    //       lost 恰 1（先水后挖累计恰一次 = 爆炸新链 + 玩家挖掘既有链同汇 clearBedSpawn 单点，不双播报；
    //       t1024b 存量腿继续独立钉挖掘链本体）。
    runLegMulti({ "t1033c non-player caliber pins: flowing water right up against the anchored bed proves the flow "
        "works (neighbor cell turns to flowing water) yet never touches the bed - the t1012④ wash list ("
        "isAttachableBlock: torches, redstone torch, cobweb, ladder, rail family) deliberately excludes b"
        "eds, so the anchor survives with zero announcements (caliber registration probe); afterwards mini"
        "ng the same anchor bed throug"
        "h the real gaze chain (t1024b pattern) invalidates it with bedSpawnLost firing exactly once acro"
        "ss the water + mine sequence - the new explosion relay and the legacy mining path converge on th"
        "e single clearBedSpawn chokepoint without double announcementsdiag washed=%1 mined=%2 singleLost"
        "=%3 lost=%4" }, [&]() {
        World wT33c;
        wT33c.setWidth(48); wT33c.setDepth(48); wT33c.setHeight(96); wT33c.setSeed(1035);
        PlayerController pcT33c;
        pcT33c.setWorld(&wT33c);
        QQuickWindow probeWinT33c;
        pcT33c.setParentItem(probeWinT33c.contentItem());
        pcT33c.grab(); // m_captured → 真实注视链（t1024b 同款）
        const int bx33c = 20, by33c = 40, bz33c = 20;
        for (int dx = -3; dx <= 6; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                wT33c.setBlock(bx33c + dx, by33c - 1, bz33c + dz, BR::Stone, 0);
                for (int dy = 0; dy <= 4; ++dy)
                    if (wT33c.blockAt(bx33c + dx, by33c + dy, bz33c + dz) != BR::Air)
                        wT33c.setBlock(bx33c + dx, by33c + dy, bz33c + dz, BR::Air, 0);
            }
        wT33c.setBlock(bx33c, by33c, bz33c, BR::BedWhite, quint8(0));
        wT33c.setBlock(bx33c - 1, by33c, bz33c, BR::BedWhite, quint8(8));
        int lostT33c = 0;
        const QMetaObject::Connection connL33c = QObject::connect(
            &pcT33c, &PlayerController::bedSpawnLost, &pcT33c, [&lostT33c]() { ++lostT33c; });
        pcT33c.setBedSpawn(float(bx33c) + 0.5f, float(by33c) + 1.0f, float(bz33c) + 0.5f);
        // (i) 水冲：源在 foot +3 格（t34w 水源同款 setBlock 源写入）→ 定步推进水流。
        wT33c.setBlock(bx33c + 3, by33c, bz33c, BR::Water, 0);
        for (int i = 0; i < 40; ++i) wT33c.tickWaterFlow();
        const bool okWashed = wT33c.blockAt(bx33c + 2, by33c, bz33c) == BR::Water // 流水工作面（真流到床旁）
            && wT33c.blockAt(bx33c, by33c, bz33c) == BR::BedWhite                 // 床 foot 不被冲
            && wT33c.blockAt(bx33c - 1, by33c, bz33c) == BR::BedWhite             // 床 head 不被冲
            && pcT33c.bedSpawnValid() && lostT33c == 0;                           // 锚保留零播报
        // (ii) 真实注视挖掘链挖锚床 foot（t1024b 同款）。
        pcT33c.loadSavedState(float(bx33c) + 0.5f, float(by33c) + 1.0f, float(bz33c) + 0.5f,
                              0.0f, -90.0f, 1 /* Creative */);
        pcT33c.tick(); // updateRaycast：垂直向下射线命中脚下床格
        pcT33c.beginMining();
        const bool okMined = wT33c.blockAt(bx33c, by33c, bz33c) == BR::Air
            && wT33c.blockAt(bx33c - 1, by33c, bz33c) == BR::Air;
        const bool okSingleLost = !pcT33c.bedSpawnValid() && lostT33c == 1
            && pcT33c.spawnPoint() == QVector3D(80.0f, 80.0f, 80.0f); // 先水后挖累计恰一次
        QObject::disconnect(connL33c);
        pcT33c.release();
        probeWinT33c.deleteLater();
        const bool okC = okWashed && okMined && okSingleLost;
        if (!okC)
            qInfo().noquote() << "  [t1033c diag] washed" << okWashed << "mined" << okMined
                              << "singleLost" << okSingleLost << "lost" << lostT33c
                              << "valid" << pcT33c.bedSpawnValid()
                              << "flowCell" << int(wT33c.blockAt(bx33c + 2, by33c, bz33c))
                              << "footId" << int(wT33c.blockAt(bx33c, by33c, bz33c))
                              << "headId" << int(wT33c.blockAt(bx33c - 1, by33c, bz33c));
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| t1033c non-player caliber pins: flowing water right up against the "
                             "anchored bed proves the flow works (neighbor cell turns to flowing "
                             "water) yet never touches the bed - the t1012④ wash list "
                             "(isAttachableBlock: torches, redstone torch, cobweb, ladder, rail family) "
                             "deliberately excludes beds, so the anchor survives with zero "
                             "announcements (caliber "
                             "registration probe); afterwards mining the same anchor bed through "
                             "the real gaze chain (t1024b pattern) invalidates it with bedSpawnLost "
                             "firing exactly once across the water + mine sequence - the new "
                             "explosion relay and the legacy mining path converge on the single "
                             "clearBedSpawn chokepoint without double announcements"
                          << (okC ? QString()
                                  : QStringLiteral("diag washed=%1 mined=%2 singleLost=%3 lost=%4")
                                        .arg(okWashed).arg(okMined).arg(okSingleLost).arg(lostT33c));
    });

    // ── P-t1037 睡眠中锚床被炸 → MC「床毁即醒」（行为级；阴性轮敏感：摘 clearBedSpawn 头部睡眠中断
    //    `if (m_sleeping) cancelSleep();` → false && 前缀 → 本腿红）──
    //   病灶（review A P3-2，行号 HEAD=e673860）：睡眠 Settled 计时中锚床被爆炸摧毁（t1033 新路径
    //   blockDestroyedBed → clearBedSpawn）：clearBedSpawn 已播「重生点已失效」并清锚，但 m_sleeping
    //   仍真 → 随后的 sleepAdvanceToDawn（旧 :3153-3155）无条件重写 m_spawnPos / m_bedAnchor 回已毁
    //   床位并把 m_bedSpawnValid 翻回 true（守卫沿静默）→ 重生点在已不存在床位上重新武装。
    //   驱动序：锚床 setBedSpawn（t1024b 同款读档恢复入口）→ 夜（setPhase 0.5 + setRunning(false) 冻结
    //   时钟——「醒，非跳晨」的相位判别器：skipToDawn 直写 elapsed 不经 QTimer，暂停态照样跳，故冻结
    //   下相位从 0.5 挪走 = 唯一来源是跳晨）→ trySleepAt 真实入睡（t898 同款，无 EntityManager 挂接 →
    //   敌对门自过）→ 泵至 Settled（captured 路径 updateSleep 才跑，t1024/t891 同款 17ms 泵）→ TNT 引燃
    //   （t1033a 同款引燃链单一尾）→ ents 固定 dt 细步 6.25s（> 5s 引信；**不泵 PC**——Settled 计时窗
    //   2s < 引信 5s，泵 PC 会让跳晨抢在爆炸前发生，t1033a「爆炸链与玩家物理正交」同款）→ 断言①醒
    //   （sleeping 翻假 + 相位仍在 0.5 = 未跳晨）+ bedSpawnValid=false + bedSpawnLost 恰一次 + spawn 回
    //   pristine (80,80,80) → 再泵 PC 冲过原 Settling 计时窗（≈5s > 2s Settled + 0.8s Waking）→ 断言②
    //   不重新武装（valid 仍假 / spawn 仍 pristine / lost 仍 1 / 相位仍 0.5）。
    runLegMulti({ "t1037 destroy-anchor during sleep wakes the player (MC bed-break wake): a player settled asleep "
        "on the anchor bed (real trySleepAt entry, pumped into Settled inside the 2s window) has the bed "
        "blasted by the real TNT fuse chain (PrimedTnt, fixed-dt 6.25s > 5s fuse, entity ticks only - ort"
        "hogonal to the player sleep timer, t1033a pattern); the blockDestroyedBed relay converges on cle"
        "arBedSpawn which now interrupts the sleep sequence through the existing startle-wake path (cance"
        "lSleep) BEFORE the anchor clear - the player wakes (sleeping false) WITHOUT the dawn jump (world"
        " clock phase stays frozen at 0.5), bedSpawnValid flips false with bedSpawnLost firing exactly on"
        "ce, the spawn point snaps back to the pristine world spawn (80,80,80) and pumping past the origi"
        "nal Settling window never re-arms the respawn point on the destroyed bed (the old lesion let sle"
        "epAdvanceToDawn rewrite m_spawnPos/m_bedAnchor onto the gone bed and flip validity back true; ne"
        "gative-round sensitive: false &&-ing out the sleep interrupt in clearBedSpawn turns this leg red"
        ")diag sleeping=%1 ignited=%2 broken=%3 woke=%4 noRearm=%5 lost=%6" }, [&]() {
        World wT37;
        wT37.setWidth(48); wT37.setDepth(48); wT37.setHeight(96); wT37.setSeed(1037);
        WorldClock clockT37;
        clockT37.setRunning(false); // 冻结 100ms QTimer：相位不漂，「相位不变」断言才对跳晨单一判别
        clockT37.setPhase(0.5f);    // 子夜（isNight；setPhase 特权指令，t1024 同款）
        PlayerController pcT37;
        pcT37.setWorld(&wT37); // blockDestroyedBed → onWorldBedBlockDestroyed 直连（t1033a 同款）
        pcT37.setWorldClock(&clockT37);
        QQuickWindow probeWinT37;
        pcT37.setParentItem(probeWinT37.contentItem());
        pcT37.grab(); // m_captured → captured 路径 updateSleep（t1024 同款；不 grab 则睡眠机不推进）
        const auto pumpT37 = [&pcT37](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            pcT37.tick();
        };
        const int bx37 = 20, by37 = 40, bz37 = 20;
        for (int dx = -3; dx <= 4; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                wT37.setBlock(bx37 + dx, by37 - 1, bz37 + dz, BR::Stone, 0); // 石台（同 t1033a rig）
                for (int dy = 0; dy <= 4; ++dy)
                    if (wT37.blockAt(bx37 + dx, by37 + dy, bz37 + dz) != BR::Air)
                        wT37.setBlock(bx37 + dx, by37 + dy, bz37 + dz, BR::Air, 0);
            }
        wT37.setBlock(bx37, by37, bz37, BR::BedWhite, quint8(0));     // foot（head 在 -X）
        wT37.setBlock(bx37 - 1, by37, bz37, BR::BedWhite, quint8(8)); // head
        int lostT37 = 0, announceT37 = 0;
        const QMetaObject::Connection connL37 = QObject::connect(
            &pcT37, &PlayerController::bedSpawnLost, &pcT37, [&lostT37]() { ++lostT37; });
        const QMetaObject::Connection connA37 = QObject::connect(
            &pcT37, &PlayerController::bedSpawnAnnounce, &pcT37, [&announceT37](bool) { ++announceT37; });
        pcT37.setBedSpawn(float(bx37) + 0.5f, float(by37) + 1.0f, float(bz37) + 0.5f);
        // 真实入睡（t898 同款入口）→ 泵至 Settled（计时窗内：入 Settled 即停，留足 2s 未耗）。
        pcT37.trySleepAt(bx37, by37, bz37);
        for (int t = 0; t < 400 && !pcT37.sleepSettled(); ++t) pumpT37(17);
        const bool okSleeping = pcT37.sleeping() && pcT37.sleepSettled()
            && pcT37.bedSpawnValid()
            && pcT37.spawnPoint() == QVector3D(float(bx37) + 0.5f, float(by37) + 1.0f, float(bz37) + 0.5f);
        const int announceEntryT37 = announceT37; // 入睡设锚沿恰 1 次 announce（review0909 #5 既有口径）
        // 引燃（t1033a 同款：格静默清 + PrimedTnt 实体接管）→ 固定 dt 细步 6.25s > 5s 引信。
        wT37.setBlock(bx37 + 2, by37, bz37, BR::TntBlock, 0);
        const bool okIgnited = wT37.clearBlockSilent(bx37 + 2, by37, bz37);
        EntityManager entsT37;
        entsT37.spawnPrimedTnt(bx37 + 2, by37, bz37);
        for (int i = 0; i < 400; ++i) // 只泵实体：爆炸链与玩家睡眠计时正交（t1033a 同款）
            entsT37.tick(0.015625, &wT37, QVector3D(-1000.0f, 80.0f, -1000.0f), 0.3f, 1.8f, true);
        const bool okBroken = wT37.blockAt(bx37, by37, bz37) == BR::Air
            && wT37.blockAt(bx37 - 1, by37, bz37) == BR::Air; // 锚床两半都在球内被清
        // ①床毁即醒：睡态中断 + 相位不变（非跳晨）+ 锚失效播报恰一次 + spawn 回 pristine。
        const bool okWoke = !pcT37.sleeping()
            && std::abs(clockT37.dayPhase() - 0.5f) < 1e-3f
            && !pcT37.bedSpawnValid() && lostT37 == 1
            && pcT37.spawnPoint() == QVector3D(80.0f, 80.0f, 80.0f)
            && announceT37 == announceEntryT37; // 置假沿零 announce（QML 静默，t1036 口径）
        // ②跳晨窗过后不重新武装：再泵 PC ≈5s > 2s Settled + 0.8s Waking——病灶里 sleepAdvanceToDawn
        //   在此把重生点重写回已毁床位并翻回 valid；修复后 m_sleeping 已假 → updateSleep 早退不可达。
        for (int t = 0; t < 300; ++t) pumpT37(17);
        const bool okNoRearm = !pcT37.bedSpawnValid() && lostT37 == 1
            && pcT37.spawnPoint() == QVector3D(80.0f, 80.0f, 80.0f)
            && std::abs(clockT37.dayPhase() - 0.5f) < 1e-3f;
        QObject::disconnect(connL37);
        QObject::disconnect(connA37);
        pcT37.release();
        probeWinT37.deleteLater();
        const bool okT37 = okSleeping && okIgnited && okBroken && okWoke && okNoRearm;
        if (!okT37)
            qInfo().noquote() << "  [t1037 diag] sleeping" << okSleeping << "ignited" << okIgnited
                              << "broken" << okBroken << "woke" << okWoke << "noRearm" << okNoRearm
                              << "| lost" << lostT37 << "announce" << announceT37
                              << "valid" << pcT37.bedSpawnValid()
                              << "spawn" << pcT37.spawnPoint().x() << pcT37.spawnPoint().y()
                              << pcT37.spawnPoint().z()
                              << "phase" << clockT37.dayPhase()
                              << "footId" << int(wT37.blockAt(bx37, by37, bz37))
                              << "headId" << int(wT37.blockAt(bx37 - 1, by37, bz37));
        if (!okT37) ++totalFail;
        qInfo().noquote() << (okT37 ? "PASS" : "FAIL")
                          << "| t1037 destroy-anchor during sleep wakes the player (MC bed-break "
                             "wake): a player settled asleep on the anchor bed (real trySleepAt "
                             "entry, pumped into Settled inside the 2s window) has the bed blasted "
                             "by the real TNT fuse chain (PrimedTnt, fixed-dt 6.25s > 5s fuse, "
                             "entity ticks only - orthogonal to the player sleep timer, t1033a "
                             "pattern); the blockDestroyedBed relay converges on clearBedSpawn "
                             "which now interrupts the sleep sequence through the existing "
                             "startle-wake path (cancelSleep) BEFORE the anchor clear - the player "
                             "wakes (sleeping false) WITHOUT the dawn jump (world clock phase "
                             "stays frozen at 0.5), bedSpawnValid flips false with bedSpawnLost "
                             "firing exactly once, the spawn point snaps back to the pristine "
                             "world spawn (80,80,80) and pumping past the original Settling "
                             "window never re-arms the respawn point on the destroyed bed (the "
                             "old lesion let sleepAdvanceToDawn rewrite m_spawnPos/m_bedAnchor "
                             "onto the gone bed and flip validity back true; negative-round "
                             "sensitive: false &&-ing out the sleep interrupt in clearBedSpawn "
                             "turns this leg red)"
                          << (okT37 ? QString()
                                  : QStringLiteral("diag sleeping=%1 ignited=%2 broken=%3 woke=%4 "
                                                   "noRearm=%5 lost=%6")
                                        .arg(okSleeping).arg(okIgnited).arg(okBroken)
                                        .arg(okWoke).arg(okNoRearm).arg(lostT37));
    });

    // ── P-t1024c 床位重生锚持久化 round-trip（真 SQLite；t1016 模式）+ 源码钉 ──
    //   (a) 有效锚：saveAll 第 6 参 {valid,x,y,z} → bed_x('g'9)/bed_y/bed_z/bed_valid=1 四键与 chunks/
    //       meta 同事务 → 关库重开 loadBedSpawn 逐键还原；
    //   (b) 失效锚：{valid:false} → bed_valid=0 门（挖锚床后退出 = 下次进世界不回填，即使坐标键残留）；
    //   (c) 旧档缺键：五参旧调用 → hasBed=false（从未睡过床 → 世界出生点重生，t388 起既有语义）；
    //   (d) 源码钉：QML 进/出世界编排 + C++ 入口签名 + 拒睡/雷暴门与文案（阴性轮敏感）。
    runLegMulti({ "t1024c bed-spawn persistence rig: the exit save writes the respawn anchor {valid,x,y,z} through "
        "saveAll's 6th arg into world_meta inside the SAME transaction as chunks/meta (bed_x 'g'9 short r"
        "ound-trip + bed_valid gate); a close/reopen round-trips the bed spawn exactly; the invalid form "
        "writes bed_valid=0 which gates the stale coordinate keys off (mined-bed-then-exit must not resto"
        "re the bed); a legacy five-arg save has no bed keys and loads hasBed=false (world-spawn respawn,"
        " pre-t1024 semantics); source pins lock the QML restore wiring, both exit-chain forms, the respa"
        "wn/lost toasts, the restore-quiet announce gate (review0909 #5), all four C++ contract surfaces,"
        " the hostile refusal and the night-or-thunder sleep window with its wording plus the storm-clear"
        " on dawn (review0909 #1) (negative-round sensitive)diag a=%1 b=%2 c=%3 d=%4" }, [&]() {
        World wT24c;
        wT24c.setWidth(48); wT24c.setDepth(48); wT24c.setHeight(96); wT24c.setSeed(1026);
        WorldStore storeT24c;
        storeT24c.setWorld(&wT24c);
        bool okA = false, okB = false, okC = false, okD = false;
        const QString dbT24 = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t1024_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT24);
        // (a) 有效锚 round-trip。
        {
            QVariantMap bs;
            bs.insert(QStringLiteral("valid"), true);
            bs.insert(QStringLiteral("x"), 8.5);
            bs.insert(QStringLiteral("y"), 41.0);
            bs.insert(QStringLiteral("z"), 8.5);
            okA = storeT24c.openWorld(dbT24)
                && storeT24c.saveAll(QStringLiteral("t1024rig"), QVariantList(), QVariantList(), QVariantList(),
                                     QVariantMap(), bs);
            storeT24c.closeWorld();
            QVariantMap back;
            if (okA && storeT24c.openWorld(dbT24)) back = storeT24c.loadBedSpawn();
            storeT24c.closeWorld();
            okA = okA && back.value(QStringLiteral("hasBed")).toBool() == true
                && back.value(QStringLiteral("x")).toDouble() == 8.5
                && back.value(QStringLiteral("y")).toDouble() == 41.0
                && back.value(QStringLiteral("z")).toDouble() == 8.5;
            if (!okA)
                qInfo().noquote() << "  [t1024c diag a] back =" << back;
        }
        // (b) 失效锚：bed_valid=0 门压过残留坐标键。
        {
            QVariantMap bsNo;
            bsNo.insert(QStringLiteral("valid"), false);
            const bool wrote = storeT24c.openWorld(dbT24)
                && storeT24c.saveAll(QStringLiteral("t1024rig"), QVariantList(), QVariantList(), QVariantList(),
                                     QVariantMap(), bsNo);
            storeT24c.closeWorld();
            QVariantMap back;
            if (wrote && storeT24c.openWorld(dbT24)) back = storeT24c.loadBedSpawn();
            storeT24c.closeWorld();
            okB = wrote && back.value(QStringLiteral("hasBed")).toBool() == false;
            if (!okB)
                qInfo().noquote() << "  [t1024c diag b] wrote" << wrote << "back =" << back;
        }
        // (c) 旧档形态：五参 saveAll（无床锚键）→ hasBed=false + coords 0。独立库（防 (b) 的
        //     bed_valid=0 残留污染旧档形态面）。
        {
            const QString dbOld24 = QDir::temp().absoluteFilePath(
                    QStringLiteral("voxel_t1024old_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbOld24);
            const bool wrote = storeT24c.openWorld(dbOld24)
                && storeT24c.saveAll(QStringLiteral("t1024old"));
            storeT24c.closeWorld();
            QVariantMap back;
            if (wrote && storeT24c.openWorld(dbOld24)) back = storeT24c.loadBedSpawn();
            storeT24c.closeWorld();
            okC = wrote && back.value(QStringLiteral("hasBed")).toBool() == false
                && back.value(QStringLiteral("x")).toDouble() == 0.0;
            if (!okC)
                qInfo().noquote() << "  [t1024c diag c] wrote" << wrote << "back =" << back;
            QFile::remove(dbOld24);
        }
        QFile::remove(dbT24);
        // (d) 源码钉：QML 编排（恢复 / 退出第 6 参 / 两处用户面文案）+ C++ 契约面（签名 / 门 / 文案）。
        //     B-P1-1 迁移：滤注释钉（pinSet）。中文文案钉为 copy 钉（t1022B 先例，单独列账）。
        {
            const QString root = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
            QStringList missD;
            missD << pinSet(root + QStringLiteral("/src/ui/Main.qml"), {
                {"qml-restore-bedspawn", "if (bs && bs.hasBed) player.setBedSpawn(bs.x, bs.y, bs.z)"},
                {"qml-exit-chain-6th-valid", "{ valid: true, x: player.spawnPoint.x, y: player.spawnPoint.y, z: player.spawnPoint.z }"},
                {"qml-exit-chain-6th-invalid", "{ valid: false }"},
                {"qml-respawn-at-bed-toast", "if (player.bedSpawnValid) window.appendChatMessage(\"\", \"你已回到床边重生\", true)"},
                {"qml-bedspawn-set-toast-copy", "重生点已设置"},
                // review0909 #5：读档回填沿（restored=true）静默——QML 播报只挂入睡设锚沿。
                {"qml-bedspawn-restore-quiet", "if (player.bedSpawnValid && !restored) window.appendChatMessage(\"\", \"重生点已设置\", true)"},
                // t1036：QML 播报 handler 改挂 bedSpawnAnnounce（bedSpawnValidChanged 无参化后不再
                //   有 onBedSpawnValidChanged(restored) 处理器）。
                {"qml-bedspawn-announce-handler", "function onBedSpawnAnnounce(restored)"},
                {"qml-bedspawn-lost-handler", "function onBedSpawnLost()"},
                {"qml-bedspawn-lost-toast-copy", "床被破坏，重生点已失效"},
            });
            missD << pinSet(root + QStringLiteral("/src/Game/playercontroller.h"), {
                {"hdr-bedSpawnValid-prop", "Q_PROPERTY(bool bedSpawnValid READ bedSpawnValid NOTIFY bedSpawnValidChanged)"},
                // review0909 #5 + t1036：播报源独立信号 bedSpawnAnnounce（只挂入睡设锚沿，restored
                //   恒 false；回填 / 置假沿零发）——validChanged 无参化（纯属性 NOTIFY，Qt 6.11 约定
                //   NOTIFY 参数=属性新值）。
                {"hdr-bedSpawnValid-signal-src", "void bedSpawnValidChanged();"},
                {"hdr-bedSpawnAnnounce-signal", "void bedSpawnAnnounce(bool restored);"},
                {"hdr-setBedSpawn", "Q_INVOKABLE void setBedSpawn(float x, float y, float z);"},
                {"hdr-clearBedSpawn", "void clearBedSpawn();"},
                {"hdr-bedSpawnLost-signal", "void bedSpawnLost();"},
            });
            missD << pinSet(root + QStringLiteral("/src/Game/playercontroller.cpp"), {
                {"cpp-thunder-gate", "m_world->weatherState() == kWeatherThunder"},
                {"cpp-hostile-copy", "你不能休息，附近有怪物"},
                // review0909 #1：雷暴改为合法入睡窗口（wiki 考据），白天拒睡文案对齐 MC「夜 / 雷暴」窗口面；
                // 跳晨完成沿清雷暴回 Clear（醒来清雷暴）。
                {"cpp-day-copy", "只能在夜晚或雷暴中睡觉"},
                {"cpp-thunder-clear-dawn", "m_world->setWeatherState(kWeatherClear);"},
                {"cpp-anchor-clear-hook", "emit bedSpawnLost();"},
            });
            missD << pinSet(root + QStringLiteral("/src/World/worldstore.h"), {
                {"hdr-loadBedSpawn", "Q_INVOKABLE QVariantMap loadBedSpawn() const;"},
            });
            okD = missD.isEmpty();
            if (!okD)
                qInfo().noquote() << "  [t1024c diag d] pin miss:" << missD.join(QLatin1Char(','));
        }
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        if (!okD) ++totalFail;
        qInfo().noquote() << (okA && okB && okC && okD ? "PASS" : "FAIL")
                          << "| t1024c bed-spawn persistence rig: the exit save writes the respawn "
                             "anchor {valid,x,y,z} through saveAll's 6th arg into world_meta "
                             "inside the SAME transaction as chunks/meta (bed_x 'g'9 short "
                             "round-trip + bed_valid gate); a close/reopen round-trips the bed "
                             "spawn exactly; the invalid form writes bed_valid=0 which gates the "
                             "stale coordinate keys off (mined-bed-then-exit must not restore the "
                             "bed); a legacy five-arg save has no bed keys and loads hasBed=false "
                             "(world-spawn respawn, pre-t1024 semantics); source pins lock the QML "
                             "restore wiring, both exit-chain forms, the respawn/lost toasts, the "
                             "restore-quiet announce gate (review0909 #5), all "
                             "four C++ contract surfaces, the hostile refusal and the night-or-"
                             "thunder sleep window with its wording plus the storm-clear on dawn "
                             "(review0909 #1) (negative-round sensitive)"
                          << (okA && okB && okC && okD
                                  ? QString()
                                  : QStringLiteral("diag a=%1 b=%2 c=%3 d=%4")
                                        .arg(okA).arg(okB).arg(okC).arg(okD));
    });

    // ── P-t1025a 繁殖链 MC 口径：喂食恋爱 → 双满产崽 → 幼崽缩放字段/血量减半 → 冷却门 → 幼崽不可繁殖 →
    //    缝调短冷却开合 → 成长还原成体（R19.21 t1025；t400/t479 繁殖链之上的口径回标 + t952 幼体基建对齐）──
    //   (a) 常量口径钉（MC 对标登记面，现实秒折算：恋爱窗 30s / 繁殖冷却 5min=300s / 成长 20min=1200s /
    //       幼崽 0.5× / 配对距 3 / 喂幼减 10%≈120s）——漂移即红 = 口径登记本体；
    //   (b) 喂食恋爱 + 双满产崽 + 幼崽字段（默认 MC 计时；0.38s 窗内幼崽远未长大、冷却远未到期）：
    //       inLoveAt 双真 → 配对产崽 + 双亲退恋进冷却（breedCooldownAt≈300）+ 幼崽 babyScaleAt=0.5 /
    //       halfHeightAt=成体×0.5（物理盒同倍缩，t952 小蹒跚者同款机制）/ maxHealth 10 = 成体上限满血
    //       （t1046 parity 台账低-1「幼崽血量=成体」，SPEC：t1025 旧减半断言随裁决「一切按原版」改写）/
    //       growTimer 挂满 1200s；
    //   (c) 冷却门（阴性轮敏感：摘 enterLoveMode 冷却行 → 本腿红）：冷却中再喂 → false（爱心不再触发）；
    //   (d) 幼崽不可繁殖门（阴性轮敏感：摘 enterLoveMode 幼崽行 → 本腿红）：幼崽求偶 false / 幼崽可喂
    //       feedBaby（growTimer 精确减 kBabyFeedGrow）/ 成体喂幼 false；
    //   (e) 缝调短冷却开合（setBreedTimings 测试缝，产品默认恒 MC 值）：1s 冷却内 false → 1.6s 后归零可再求偶
    //       （门「关→开」双向实证，300s 真值跑不动的折算口径）；
    //   (f) 成长还原：0.8s 成长缝 → 幼崽到点 baby=false + babyScaleAt 1.0 + halfHeightAt 还原成体盒 +
    //       血量上限/当前恒 10（t1046 低-1：幼崽本就满血，长大无血量还原面——t1025 ×2 还原退役）+
    //       growTimer 归零。
    runLegMulti({ "t1025a breeding MC-parity: constants locked to the registered conversion (love 30s / breed coold"
        "own 5min=300s / baby growth 20min=1200s / baby 0.5x / pair range 3 / baby-feed -10%); feeding tw"
        "o adult sheep puts both in love, pairing spawns one baby while both parents drop out of love int"
        "o the breed cooldown; the baby has scale 0.5 with a physically halved collision box (t952 baby m"
        "echanism, halfHeight 0.225), halved max health 5 and a full 1200s growth timer; feeding during t"
        "he cooldown is refused (hearts stay off); a baby refuses love mode but accepts feedBaby which sh"
        "aves exactly kBabyFeedGrow off the growth timer while an adult refuses feedBaby; the setBreedTim"
        "ings seam shortens the cooldown to 1s which closes then reopens the gate on real ticks; a 0.8s g"
        "rowth-seam cow baby grows up in-place restoring the adult collision box, scale 1.0 and doubled h"
        "ealth to 10 (negative-round sensitive: cooldown gate + baby no-love gate)diag paired=%1 fields=%"
        "2 cdGate=%3 babyGate=%4 closed=%5 reopened=%6 grown=%7" }, [&]() {
        bool ok = true;
        // (a) 常量口径：t400 常量段为 private（勿为探针动可见性）→ 数值面由两处锁定：
        //     ① t1025b (d) 的 pinSet 头文件钉（= 300.0f / = 1200.0f / 30.0f / 0.5f / 3.0f / 120.0f 字面量行）；
        //     ② 本探针行为腿（breedCooldownAt≈300 / growTimer>1199 / babyScaleAt=0.5 / 喂幼减恰 120）。
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        auto mkRig = [](World &w, quint32 seed) {
            w.setWidth(32); w.setDepth(32); w.setHeight(24); w.setSeed(seed);
            for (int x = 8; x <= 24; ++x)     // 石板地板：kGravity=28 下长窗自由落体会跌出世界被移除
                for (int z = 8; z <= 24; ++z)
                    w.setBlock(x, 10, z, BR::Stone, 0);
            // t1025 首跑教训：未 regenerate 的 World 也带 fBm 程序地形（blockAtWorld 纯函数回填，t789 rig
            //   「空世界」实为地形世界）——seed 1025-1028 的地表高于 y=10 → 石板被掩埋、mob 生成即嵌入
            //   （stuck-escape + 窒息扣血 = 引诱/跟随位移腿与成长血量腿全红的根因）。工作体积显式净空：
            //   地板上方 11..16 清 Air（同 t1024a「工作体积清空」纪律：探针选址自带保障，禁赌 worldgen）。
            for (int x = 8; x <= 24; ++x)
                for (int z = 8; z <= 24; ++z)
                    for (int y = 11; y <= 16; ++y)
                        w.setBlock(x, y, z, BR::Air, 0);
        };
        // (b) 喂食恋爱 + 双满产崽 + 幼崽字段（sheep；默认 MC 计时）。
        EntityManager emA;
        World wA; mkRig(wA, 1025);
        const int pa = emA.spawnMobTyped(14, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int pb = emA.spawnMobTyped(15, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const bool fed = pa >= 0 && pb >= 0 && emA.enterLoveMode(pa) && emA.enterLoveMode(pb)
            && emA.inLoveAt(pa) && emA.inLoveAt(pb);
        for (int t = 0; t < 24; ++t) emA.tick(0.016f, &wA, farL, 0.3f, 1.8f, false);
        int babyA = -1;
        for (int i = 0; i < emA.count(); ++i)
            if (emA.aliveAt(i) && emA.isBabyAt(i) && emA.mobTypeAt(i) == EntityManager::MobSheep) babyA = i;
        const bool paired = fed && babyA >= 0 && !emA.inLoveAt(pa) && !emA.inLoveAt(pb)
            && emA.breedCooldownAt(pa) > 299.0f && emA.breedCooldownAt(pb) > 299.0f; // 挂满 300（窗内衰减 ≤0.4s）
        const bool babyFields = babyA >= 0
            && std::abs(emA.babyScaleAt(babyA) - 0.5f) < 1e-4f
            && std::abs(emA.halfHeightAt(babyA) - 0.225f) < 1e-4f  // sheep 成体 halfH 0.45 × 0.5（物理盒同倍缩）
            && emA.maxHealthAt(babyA) == 10 && emA.healthAt(babyA) == 10 // t1046 低-1：满血=成体上限（阴性轮：重插减半行即红）
            && emA.growTimerAt(babyA) > 1199.0f;   // 产崽即挂满 1200s（窗内衰减 ≤0.4s）
        // (c) 冷却门（阴性轮敏感）。
        const bool cooldownGate = babyA >= 0 && !emA.enterLoveMode(pa) && !emA.enterLoveMode(pb);
        // (d) 幼崽不可繁殖门（阴性轮敏感）+ 喂幼加速成长。
        const float growBefore = babyA >= 0 ? emA.growTimerAt(babyA) : -1.0f;
        const bool babyGate = babyA >= 0 && !emA.enterLoveMode(babyA)
            && emA.feedBaby(babyA)
            && emA.growTimerAt(babyA) <= growBefore - 120.0f + 0.01f  // 精确减 kBabyFeedGrow=120（行为锁）
            && !emA.feedBaby(pa);
        // (e) 缝调短冷却开合（pig；1s 冷却缝）。
        EntityManager emB;
        emB.setBreedTimings(1.0f, 1200.0f);
        World wB; mkRig(wB, 1026);
        const int paB = emB.spawnMobTyped(14, 11, 15, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const int pbB = emB.spawnMobTyped(15, 11, 15, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const bool fedB = paB >= 0 && pbB >= 0 && emB.enterLoveMode(paB) && emB.enterLoveMode(pbB);
        for (int t = 0; t < 24; ++t) emB.tick(0.016f, &wB, farL, 0.3f, 1.8f, false);
        const bool gateClosed = fedB && emB.breedCooldownAt(paB) > 0.0f && !emB.enterLoveMode(paB);
        for (int t = 0; t < 100; ++t) emB.tick(0.016f, &wB, farL, 0.3f, 1.8f, false); // 1.6s > 1s 缝冷却
        const bool gateReopened = emB.breedCooldownAt(paB) == 0.0f && emB.enterLoveMode(paB)
            && emB.inLoveAt(paB);
        // (f) 成长还原（cow；0.8s 成长缝）。
        EntityManager emC;
        emC.setBreedTimings(300.0f, 0.8f);
        World wC; mkRig(wC, 1027);
        const int paC = emC.spawnMobTyped(14, 11, 15, EntityManager::MobCow, QStringLiteral("#a52a2a"), 10);
        const int pbC = emC.spawnMobTyped(15, 11, 15, EntityManager::MobCow, QStringLiteral("#a52a2a"), 10);
        const bool fedC = paC >= 0 && pbC >= 0 && emC.enterLoveMode(paC) && emC.enterLoveMode(pbC);
        for (int t = 0; t < 24; ++t) emC.tick(0.016f, &wC, farL, 0.3f, 1.8f, false);
        int babyC = -1;
        for (int i = 0; i < emC.count(); ++i)
            if (emC.aliveAt(i) && emC.isBabyAt(i) && emC.mobTypeAt(i) == EntityManager::MobCow) babyC = i;
        for (int t = 0; t < 100; ++t) emC.tick(0.016f, &wC, farL, 0.3f, 1.8f, false); // 1.6s > 0.8s 缝成长
        const bool grown = fedC && babyC >= 0 && !emC.isBabyAt(babyC)
            && std::abs(emC.babyScaleAt(babyC) - 1.0f) < 1e-4f
            && std::abs(emC.halfHeightAt(babyC) - 0.50f) < 1e-4f   // cow 成体盒还原
            && emC.maxHealthAt(babyC) == 10 && emC.healthAt(babyC) == 10  // t1046 低-1：幼崽满血，长大恒 10/10
            && emC.growTimerAt(babyC) == 0.0f;
        // (d) 源码钉（滤注释 pinSet；两道求偶门钉钉在 P-t1025a——与被护行为腿同行，阴性轮摘门时
        //     钉随行红 = 恰红面收敛在 P-t1025a，P-t1025b 保绿证其余系统零回归）。
        const QString exeDirA = QCoreApplication::applicationDirPath();
        const QString rootA25 = QDir(exeDirA + QStringLiteral("/..")).absolutePath();
        QStringList missA;
        missA << pinSet(rootA25 + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-love-cooldown-gate", "if (e.breedCooldown > 0.0f) return false;"},
            {"cpp-love-baby-gate", "if (e.baby) return false;"},
        });
        const bool pinsA = missA.isEmpty();
        if (!pinsA)
            qInfo().noquote() << "  [t1025a diag] pin miss:" << missA.join(QLatin1Char(','));
        if (!grown)
            qInfo().noquote() << "  [t1025a diag2] cow babyC" << babyC << "isBaby" << emC.isBabyAt(babyC)
                              << "alive" << emC.aliveAt(babyC) << "dead" << emC.deadAt(babyC)
                              << "type" << emC.mobTypeAt(babyC) << "scale" << emC.babyScaleAt(babyC)
                              << "halfH" << emC.halfHeightAt(babyC) << "maxHp" << emC.maxHealthAt(babyC)
                              << "hp" << emC.healthAt(babyC) << "growT" << emC.growTimerAt(babyC)
                              << "y" << emC.posAt(babyC).y();
        ok = ok && paired && babyFields && cooldownGate && babyGate && gateClosed && gateReopened && grown
            && pinsA;
        if (!ok)
            qInfo().noquote() << "  [t1025a diag] paired" << paired << "babyFields" << babyFields
                              << "cooldownGate" << cooldownGate << "babyGate" << babyGate
                              << "gateClosed" << gateClosed << "gateReopened" << gateReopened
                              << "grown" << grown << "babyA" << babyA << "babyC" << babyC
                              << "pinsA" << pinsA;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1025a breeding MC-parity: constants locked to the registered conversion "
                             "(love 30s / breed cooldown 5min=300s / baby growth 20min=1200s / baby "
                             "0.5x / pair range 3 / baby-feed -10%); feeding two adult sheep puts both "
                             "in love, pairing spawns one baby while both parents drop out of love into "
                             "the breed cooldown; the baby has scale 0.5 with a physically halved "
                             "collision box (t952 baby mechanism, halfHeight 0.225), halved max health "
                             "5 and a full 1200s growth timer; feeding during the cooldown is refused "
                             "(hearts stay off); a baby refuses love mode but accepts feedBaby which "
                             "shaves exactly kBabyFeedGrow off the growth timer while an adult refuses "
                             "feedBaby; the setBreedTimings seam shortens the cooldown to 1s which "
                             "closes then reopens the gate on real ticks; a 0.8s growth-seam cow baby "
                             "grows up in-place restoring the adult collision box, scale 1.0 and "
                             "doubled health to 10 (negative-round sensitive: cooldown gate + baby "
                             "no-love gate)"
                          << (ok ? QString()
                                  : QStringLiteral("diag paired=%1 fields=%2 cdGate=%3 babyGate=%4 "
                                                   "closed=%5 reopened=%6 grown=%7")
                                        .arg(paired).arg(babyFields).arg(cooldownGate)
                                        .arg(babyGate).arg(gateClosed).arg(gateReopened).arg(grown));
    });

    // ── P-t1025b 幼崽跟随父母 + 食物引诱 + Game 层门控接线（R19.21 t1025 AI 行为腿）──
    //   (a) 幼崽跟随最近成年同种：产崽 → 杀双亲（dead 不作认亲目标）→ 远端放同种成年羊 + 异种成年牛对照 →
    //       幼崽净位移朝羊 + yaw 精确钉向羊（非牛）——认亲「同种 + 最近 + 成年」三口径；
    //   (b) 食物引诱：setFoodLure(pig,true) → 半径内猪 yaw 钉向玩家 + 净位移朝玩家；半径外猪不受扰（游走
    //       距离有界 + yaw 不钉）；关门 → 解钉回 wander；
    //   (c) Game 层接线（真 Hotbar 信号链）： WheatId → 牛/羊门控真 / SeedId → 鸡真 / CarrotId → 猪真 /
    //       空手 → 全清（breedFoodMatches 单一权威，喂食分流与引诱同源）；
    //   (d) 源码钉（滤注释 pinSet；阴性轮行为腿在 P-t1025a，钉只锁接线存在性）。
    runLegMulti({ "t1025b baby-follow + food-lure: a baby orphaned by killing both parents locks yaw onto and walks"
        " toward the nearest adult of the SAME species while ignoring the adult cow control (follow-neare"
        "st-adult semantics); with the pig food-lure gate on, a pig inside the 10-block radius pins yaw o"
        "n and walks toward the player, a pig beyond the radius keeps bounded wander drift with unpinned "
        "yaw, and turning the gate off unpins; the Game-layer wiring drives the gate table through the re"
        "al Hotbar slotsChanged chain (wheat->cow+sheep, seeds->chicken, carrot->pig, empty hand clears a"
        "ll - one breedFoodMatches authority shared with the feeding path); source pins lock the seam/lur"
        "e contract surfaces, both love gates and the baby box/HP halvingdiag follow=%1 lureState=%2 lure"
        "In=%3 lureFar=%4 lureOff=%5 wired=%6 pins=%7" }, [&]() {
        bool ok = true;
        EntityManager em;
        World w;
        w.setWidth(32); w.setDepth(32); w.setHeight(24); w.setSeed(1028);
        for (int x = 8; x <= 24; ++x)
            for (int z = 8; z <= 24; ++z)
                w.setBlock(x, 10, z, BR::Stone, 0);
        // t1025 首跑教训（同 P-t1025a mkRig）：未 regenerate 的 World 仍带 fBm 程序地形 → 显式净空工作体积。
        for (int x = 8; x <= 24; ++x)
            for (int z = 8; z <= 24; ++z)
                for (int y = 11; y <= 16; ++y)
                    w.setBlock(x, y, z, BR::Air, 0);
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        // (a) 幼崽跟随。
        const int pa = em.spawnMobTyped(14, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int pb = em.spawnMobTyped(15, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        em.enterLoveMode(pa);
        em.enterLoveMode(pb);
        for (int t = 0; t < 24; ++t) em.tick(0.016f, &w, farL, 0.3f, 1.8f, false);
        int baby = -1;
        for (int i = 0; i < em.count(); ++i)
            if (em.aliveAt(i) && em.isBabyAt(i) && em.mobTypeAt(i) == EntityManager::MobSheep) baby = i;
        em.damageEntity(pa, 999); // 双亲死亡 → dead 不作认亲目标（0.5s 死亡动画后释放槽）
        em.damageEntity(pb, 999);
        const int cowC = em.spawnMobTyped(10, 11, 15, EntityManager::MobCow, QStringLiteral("#a52a2a"), 10);
        const int shD = em.spawnMobTyped(22, 11, 15, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const QVector3D b0 = baby >= 0 ? em.posAt(baby) : QVector3D();
        for (int t = 0; t < 48; ++t) em.tick(0.016f, &w, farL, 0.3f, 1.8f, false); // 12 AI 步 ≈ 0.77s
        auto wrapAngle = [](float a) {
            while (a > float(M_PI)) a -= float(2 * M_PI);
            while (a < -float(M_PI)) a += float(2 * M_PI);
            return a;
        };
        // t1025 二跑教训：yawAt 返回**度**（t239 QML eulerRotation.y 面约定，qRadiansToDegrees）——
        //   与 atan2 推导的目标方位（弧度）直比恒差 57.3 倍（首跑 diagF yaw=-87.68 vs toSheep=-1.5236
        //   即 -1.5303 rad：钉向本身已成立，纯单位面假红）。比较位统一转弧度（10480 行矿车探针同款换算）。
        auto yawRadAt = [&em](int i) { return em.yawAt(i) * float(M_PI) / 180.0f; };
        bool follow = false;
        if (baby >= 0 && shD >= 0 && cowC >= 0) {
            const QVector3D b1 = em.posAt(baby);
            const QVector3D sh1 = em.posAt(shD);
            const QVector3D cow1 = em.posAt(cowC);
            const float toSheep = std::atan2(-(sh1.x() - b1.x()), -(sh1.z() - b1.z()));
            const float toCow = std::atan2(-(cow1.x() - b1.x()), -(cow1.z() - b1.z()));
            const QVector3D disp = b1 - b0;
            const QVector3D want = sh1 - b0;
            follow = QVector3D::dotProduct(disp, want) > 0.05f                    // 净位移朝羊
                && std::abs(wrapAngle(yawRadAt(baby) - toSheep)) < 0.15f          // yaw 钉向羊（最后 AI 步钉）
                && std::abs(wrapAngle(yawRadAt(baby) - toCow)) > 0.3f;            // 且非朝牛（异种不认）
            qInfo().noquote() << "  [t1025b diagF] baby" << baby << "shD" << shD << "cowC" << cowC
                              << "b0" << b0.x() << b0.y() << b0.z() << "b1" << b1.x() << b1.y() << b1.z()
                              << "yawRad" << yawRadAt(baby) << "toSheep" << toSheep << "toCow" << toCow
                              << "dot" << QVector3D::dotProduct(disp, want)
                              << "isBaby" << em.isBabyAt(baby) << "shType" << em.mobTypeAt(shD);
        }
        // (b) 食物引诱（pig；玩家 listener 静置于 (4.5, 11, 15)）。
        em.clearAll();
        em.setFoodLure(EntityManager::MobPig, true);
        const bool lureState = em.foodLureAt(EntityManager::MobPig) && !em.foodLureAt(EntityManager::MobCow);
        const int pigIn = em.spawnMobTyped(10, 11, 15, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const int pigFar = em.spawnMobTyped(24, 11, 15, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const QVector3D playerL(4.5f, 11.0f, 15.0f);
        const QVector3D p0 = pigIn >= 0 ? em.posAt(pigIn) : QVector3D();
        const float farDist0 = pigFar >= 0
            ? std::sqrt(std::pow(em.posAt(pigFar).x() - playerL.x(), 2)
                        + std::pow(em.posAt(pigFar).z() - playerL.z(), 2)) : 0.0f;
        for (int t = 0; t < 48; ++t) em.tick(0.016f, &w, playerL, 0.3f, 1.8f, false);
        bool lureIn = false, lureFar = false;
        if (pigIn >= 0 && pigFar >= 0) {
            const QVector3D p1 = em.posAt(pigIn);
            const float toPlayer = std::atan2(-(playerL.x() - p1.x()), -(playerL.z() - p1.z()));
            lureIn = QVector3D::dotProduct(p1 - p0, playerL - p0) > 0.05f
                && std::abs(wrapAngle(yawRadAt(pigIn) - toPlayer)) < 0.05f;
            const QVector3D f1 = em.posAt(pigFar);
            const float toPlayerFar = std::atan2(-(playerL.x() - f1.x()), -(playerL.z() - f1.z()));
            const float farDist1 = std::sqrt(std::pow(f1.x() - playerL.x(), 2)
                                             + std::pow(f1.z() - playerL.z(), 2));
            lureFar = farDist1 > farDist0 - 1.0f   // 半径外不被拽近（游走漂移 ≤ 0.8 格）
                && std::abs(wrapAngle(yawRadAt(pigFar) - toPlayerFar)) > 1e-3f; // yaw 不钉
            qInfo().noquote() << "  [t1025b diagL] pigIn" << pigIn << "p0" << p0.x() << p0.z()
                              << "p1" << p1.x() << p1.z() << "yawRad" << yawRadAt(pigIn)
                              << "toPlayer" << toPlayer << "dot"
                              << QVector3D::dotProduct(p1 - p0, playerL - p0)
                              << "lure" << em.foodLureAt(EntityManager::MobPig);
        }
        em.setFoodLure(EntityManager::MobPig, false);
        for (int t = 0; t < 48; ++t) em.tick(0.016f, &w, playerL, 0.3f, 1.8f, false);
        bool lureOff = false;
        if (pigIn >= 0) {
            const QVector3D p2 = em.posAt(pigIn);
            const float toPlayer = std::atan2(-(playerL.x() - p2.x()), -(playerL.z() - p2.z()));
            lureOff = std::abs(wrapAngle(yawRadAt(pigIn) - toPlayer)) > 1e-3f; // 解钉（wander 重随机，恰合概率 ~0）
        }
        // (c) Game 层接线（真 Hotbar 信号链 slotsChanged → updateFoodLure → setFoodLure）。
        EntityManager emW;
        Hotbar hbW;
        PlayerController pcW;
        pcW.setEntityManager(&emW);
        pcW.setHotbar(&hbW);
        hbW.setStack(hbW.selectedSlot(), RecipeRegistry::WheatId, 1);
        const bool wireWheat = emW.foodLureAt(EntityManager::MobCow) && emW.foodLureAt(EntityManager::MobSheep)
            && !emW.foodLureAt(EntityManager::MobPig) && !emW.foodLureAt(EntityManager::MobChicken);
        hbW.setStack(hbW.selectedSlot(), RecipeRegistry::SeedId, 1);
        const bool wireSeed = emW.foodLureAt(EntityManager::MobChicken) && !emW.foodLureAt(EntityManager::MobCow)
            && !emW.foodLureAt(EntityManager::MobSheep) && !emW.foodLureAt(EntityManager::MobPig);
        hbW.setStack(hbW.selectedSlot(), RecipeRegistry::CarrotId, 1);
        const bool wireCarrot = emW.foodLureAt(EntityManager::MobPig) && !emW.foodLureAt(EntityManager::MobCow)
            && !emW.foodLureAt(EntityManager::MobChicken);
        hbW.setStack(hbW.selectedSlot(), 0, 0);
        const bool wireClear = !emW.foodLureAt(EntityManager::MobCow) && !emW.foodLureAt(EntityManager::MobSheep)
            && !emW.foodLureAt(EntityManager::MobPig) && !emW.foodLureAt(EntityManager::MobChicken);
        const bool wired = wireWheat && wireSeed && wireCarrot && wireClear;
        // (d) 源码钉（滤注释；锁接线/门/缝存在性——阴性轮红腿是 P-t1025a 的行为面）。
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QStringList miss;
        miss << pinSet(root + QStringLiteral("/src/Entities/entitymanager.h"), {
            {"hdr-love-duration-mc", "static constexpr float kLoveDuration    = 30.0f;"},
            {"hdr-breed-cooldown-mc", "static constexpr float kBreedCooldown   = 300.0f;"},
            {"hdr-baby-grow-mc", "static constexpr float kBabyGrowTime    = 1200.0f;"},
            {"hdr-baby-scale-mc", "static constexpr float kBabyScale       = 0.5f;"},
            {"hdr-breed-range-mc", "static constexpr float kBreedRange      = 3.0f;"},
            {"hdr-baby-feed-grow-mc", "static constexpr float kBabyFeedGrow    = 120.0f;"},
            {"hdr-follow-range-mc", "static constexpr float kBabyFollowRange    = 16.0f;"},
            {"hdr-lure-range-mc", "static constexpr float kFoodLureRange      = 10.0f;"},
            {"hdr-seam-decl", "Q_INVOKABLE void setBreedTimings(float breedCooldownSec, float babyGrowSec);"},
            {"hdr-lure-decl", "Q_INVOKABLE void setFoodLure(int mobType, bool active);"},
            {"hdr-lure-table", "bool m_foodLure[kMobTypeCount] = {};"},
        });
        miss << pinSet(root + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-baby-box-halve", "baby.halfW *= kBabyScale;"},            // t1046 SPEC：旧 cpp-baby-hp-halve（减半行）随低-1 清偿移除，改钉满血写
            {"cpp-baby-hp-full", "baby.health = baby.maxHealth;"},
            {"cpp-grow-restore-box", "applyMobCollisionBox(e.mobType, e);"},
            {"cpp-baby-follow-hook", "const int parent = findNearestAdultSameType(idx);"},
            {"cpp-lure-gate", "m_foodLure[e.mobType]"},
        });
        miss << pinSet(root + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-food-map-single", "const bool match = breedFoodMatches(mt, heldItemId);"},
            {"cpp-lure-refresh", "m_entityManager->setFoodLure(EntityManager::MobCow, breedFoodMatches(EntityManager::MobCow, held));"},
        });
        miss << pinSet(root + QStringLiteral("/src/Game/playercontroller.h"), {
            {"hdr-lure-refresh-decl", "void updateFoodLure();"},
        });
        const bool pinsOk = miss.isEmpty();
        if (!pinsOk)
            qInfo().noquote() << "  [t1025b diag] pin miss:" << miss.join(QLatin1Char(','));
        ok = ok && follow && lureState && lureIn && lureFar && lureOff && wired && pinsOk;
        if (!ok)
            qInfo().noquote() << "  [t1025b diag] follow" << follow << "lureState" << lureState
                              << "lureIn" << lureIn << "lureFar" << lureFar << "lureOff" << lureOff
                              << "wired" << wired;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1025b baby-follow + food-lure: a baby orphaned by killing both parents "
                             "locks yaw onto and walks toward the nearest adult of the SAME species "
                             "while ignoring the adult cow control (follow-nearest-adult semantics); "
                             "with the pig food-lure gate on, a pig inside the 10-block radius pins "
                             "yaw on and walks toward the player, a pig beyond the radius keeps "
                             "bounded wander drift with unpinned yaw, and turning the gate off "
                             "unpins; the Game-layer wiring drives the gate table through the real "
                             "Hotbar slotsChanged chain (wheat->cow+sheep, seeds->chicken, "
                             "carrot->pig, empty hand clears all - one breedFoodMatches authority "
                             "shared with the feeding path); source pins lock the seam/lure contract "
                             "surfaces, both love gates and the baby box/HP halving"
                          << (ok ? QString()
                                  : QStringLiteral("diag follow=%1 lureState=%2 lureIn=%3 lureFar=%4 "
                                                   "lureOff=%5 wired=%6 pins=%7")
                                        .arg(follow).arg(lureState).arg(lureIn).arg(lureFar)
                                        .arg(lureOff).arg(wired).arg(pinsOk));
    });

    // ── P-t1026a 小麦农业闭环行为腿（R19.21 t1026；真实玩家路径：placeBlock 锄/种 + beginMining 收割 +
    //    World::tickCropGrowth 直泵）──
    //   链基建自 t234（锄→耕地）/ t236（种子→作物 + 生长 tick）/ t237（收割掉落）/ t246（草丛掉种）已
    //   落地，本探针为验收面（dev-plan t1026 探针清单：锄地转换 / 播种落作物 / 阶段随机推进 / 成熟收获
    //   掉落表 / 未熟掉种子）+ 口径对齐（t1026：成熟种子 1-2 → 1-3）：
    //   (a) 锄地转换（真 placeBlock 链）：持木锄生存瞄泥土/草方块顶面 → 该格转 Farmland（湿润 state=0，
    //       无水源 → 干）+ 耐久 -1（槽不空）；瞄石头 → 不转换（锄对非可耕地无效应）；
    //   (b) 播种（真 placeBlock 链）：持 8 种子瞄耕地 → 耕地正上方落 WheatCrop state=0 + 消耗 1 种子；
    //       瞄非耕地（泥土）→ 不种不耗；再瞄已种作物（命中格=作物非耕地）→ 不覆盖不耗；
    //   (c) 阶段随机推进（tick 泵 + 确定性骰子复刻）：tickCropGrowth 的散布骰子是纯函数
    //       hashVoxel(seed ^ 窗口×φ, x, y*7+stage, z) & 0xFFFF % 100 < 6（干耕地 1× 倍率、无雨）→ 探针
    //       外部复刻骰子精确预测开露作物逐阶段命中窗口序号；25 次 tick = 1 窗（kCropTickInterval 节流），
    //       泵到预测窗数后断言：① 作物到 WheatCropStageMax=7；② 实际升阶段窗口序列 ≡ 模拟序列（确定性
    //       契约行为级钉死）；③ 每窗至多 +1 且单调（random-tick 逐阶语义）。三道生长门各一阴性对照株：
    //       遮黑株（6 邻全石包罩 → skyLight 0 < kCropMinLight=9 → 泵毕仍 stage 0；摘光照门时该株骰子流
    //       在泵域内必命中——阴性轮可达域守卫）；非耕地支撑株（下方泥土 → 不长）；已熟株（stage 7 → 不再动）；
    //   (d) 成熟收获掉落表（真 beginMining 生存链）：收割 (b) 长熟的作物 → 恰 2 次 spawnItem：
    //       1× WheatId(0x209) + 1× SeedId(0x208) count ∈ [1,3]（t1026 口径；阴性轮摘小麦行/改掉落 → 本腿红）；
    //   (e) 未熟收获：stage 3 作物 → 恰 1 次 spawnItem：仅 1× SeedId（无小麦）。
    //   rig：独立 40×40×32 世界 seed 10261（t1025 净空纪律：显式石板地板 + 上方全清 → 天光 15；骰子依赖
    //   seed+窗口序号 → 独立世界保 m_cropIntervalIndex 从 0 起算）。
    runLegMulti({ "t1026a wheat farming loop (real player path): hoe right-click converts dirt AND grass tops to dr"
        "y farmland in survival (hydration state 0, one durability tick, stone refuses); 8 seeds plant a "
        "stage-0 wheat crop above the farmland consuming exactly one seed while plain dirt and an already"
        "-planted crop refuse; tick-pumped growth reproduces the deterministic scatter dice exactly (actu"
        "al advance windows == simulated windows, +1 monotonic per window) maturing the open crop to stag"
        "e 7 while the stone-enclosed dark crop (skyLight 0 < 9), the no-farmland-support crop and the al"
        "ready-mature anchor all hold stage; harvesting the mature crop bare-handed yields exactly 1 whea"
        "t plus 0-3 seeds (t1046 Beta/1.0 seed caliber: zero seeds legally emits no seed item) and harves"
        "ting the stage-3 crop yields exactly 1 seed (negative-round sensitive: crop light gate + harvest"
        " drop table)diag hoe=%1 seed=%2 growth=%3 mature=%4 imm=%5" }, [&]() {
        bool ok = true;
        World wF;
        wF.setWidth(40);
        wF.setDepth(40);
        wF.setHeight(32); // 3 次 setter 各 regenerate；y≥16 工作带显式净空（t1025 fBm 地形教训）
        wF.setSeed(10261);
        for (int x = 4; x <= 35; ++x) {
            for (int z = 10; z <= 22; ++z)
                wF.setBlock(x, 15, z, BR::Stone, 0); // 石板地板（站立面）
            for (int z = 14; z <= 18; ++z)           // 农田带（z=16 ± 2）上方显式净空 → 开露列天光 15
                for (int y = 16; y <= 31; ++y)
                    wF.setBlock(x, y, z, BR::Air, 0); // t1025 fBm 地形教训：工作带清空，禁赌 worldgen
        }
        Hotbar hbF;
        PlayerController pcF; // t814 真消费端模式（无窗口直造；挂窗 grab 载体同 P-t945）
        pcF.setWorld(&wF);
        pcF.setHotbar(&hbF);
        QQuickWindow winF;
        pcF.setParentItem(winF.contentItem());
        pcF.grab();
        // t1040 rig 加固（t1030 同式）：锄/种子皆经 hotbar heldItemId 分流且分支无条件 return（含
        //   阴性腿「石头不耕 / 泥土不种」）→ m_selectedBlock 从未被读；显式归 Air 建模 QML 材料/
        //   工具段→Air 接线，堵 fall-through 通用放置对默认 Stone 的潜在误放误耗（隐性消耗）。
        pcF.setSelectedBlock(int(BR::Air));
        // 掉落收集（等价 Main.qml onSpawnItem 直连计数，t852 先例）。
        QVector<int> dropIdF, dropCntF;
        const QMetaObject::Connection dropConnF = QObject::connect(
            &pcF, &PlayerController::spawnItem, &pcF,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIdF.push_back(id);
                dropCntF.push_back(count);
            });
        // 瞄准帮手（P-t945 同款：re-grab 光标归零 delta → loadSavedState 定位定向 → tick 刷射线）。
        const auto aimF = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ, int mode) {
            const float ex = feetX, ey = 16.0f + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcF.release();
            pcF.grab();
            pcF.loadSavedState(feetX, 16.0f, feetZ, yaw, pitch, mode);
            pcF.tick();
            return pcF.hitBlock();
        };
        const auto pumpMsF = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 挖掘帮手：beginMining（m_leftDown=true）→ tick 泵（processEvents 喂真实 dt → updateMining 墙钟
        //   积分进度；hardness 0 → miningTime 0.05s 地板）至目标格 Air → endMining（防续挖下一目标）。
        //   t1026a 二跑教训（t1022 同款）：updateMining 的 dt = m_clock.restart() 墙钟差（:927）——连 tick
        //   时 dt≈0 → progress += 0*speed 恒 0 → 6000 tick 挖不满瞬破门槛（postBlock 25 / progTicks 6002 /
        //   prog 0 三联签名）。每 tick 前 busy-wait ≥17ms 保 dt>0（t889 pumpFor / t1022 面板同先例）。
        const auto mineBlockF = [&](int bx, int by, int bz) {
            pcF.beginMining();
            for (int i = 0; i < 6000 && wF.blockAt(bx, by, bz) != BR::Air; ++i) {
                QElapsedTimer dtw;
                dtw.start();
                while (dtw.elapsed() < 17)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                pcF.tick();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            }
            pcF.endMining();
        };
        // 农田布局（z=16 一排；间距 4 防瞄准串扰）：A 锄+种 / B 遮黑株 / C 非耕地株 / D 已熟株 /
        //   E 草方块锄转 / F 石头阴性 / G 泥土播种阴性 / H 未熟收割。
        struct Plot { int x; int id; };
        const Plot plots[8] = { { 12, BR::Dirt },  { 16, BR::Farmland }, { 20, BR::Dirt },
                                { 24, BR::Farmland }, { 8, BR::Grass }, { 28, BR::Stone },
                                { 30, BR::Dirt },  { 32, BR::Farmland } };
        for (const Plot &p : plots) wF.setBlock(p.x, 15, 16, quint8(p.id), 0);
        // (a) 锄地转换：持木锄（生存）瞄 A 泥土顶面 → Farmland + 干态 state；同链瞄 E 草方块 → Farmland。
        hbF.setStack(0, int(ToolRegistry::HoeWood), 1);
        hbF.setSelectedSlot(0);
        const QVector3D hitHoeA = aimF(15.5f, 16.5f, 12.5f, 15.90f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        const QVector3D hitHoeE = aimF(11.5f, 16.5f, 8.5f, 15.90f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        // 阴性：瞄 F 石头 → 锄无效应（不转换）。
        const QVector3D hitHoeF = aimF(31.5f, 16.5f, 28.5f, 15.90f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        const bool hoeOk = hitHoeA == QVector3D(12, 15, 16)
            && wF.blockAt(12, 15, 16) == BR::Farmland
            && (wF.stateAt(12, 15, 16) & BR::FarmlandHydrationMask) == 0 // 无水源 → 干
            && wF.blockAt(8, 15, 16) == BR::Farmland                     // 草方块同链可锄
            && hitHoeF == QVector3D(28, 15, 16)
            && wF.blockAt(28, 15, 16) == BR::Stone                       // 石头照旧
            && hbF.blockIdAt(0) == int(ToolRegistry::HoeWood);           // 耐久 -1 未破损（t263 链走通）
        if (!hoeOk)
            qInfo().noquote() << "  [t1026a diag] hoe hitA" << hitHoeA << "idA"
                              << int(wF.blockAt(12, 15, 16)) << "idE" << int(wF.blockAt(8, 15, 16))
                              << "hitF" << hitHoeF << "idF" << int(wF.blockAt(28, 15, 16))
                              << "hoeSlot" << hbF.blockIdAt(0);
        // (b) 播种：8 种子瞄 A 耕地 → 上方落 WheatCrop stage0 + 消耗 1；瞄 G 泥土 → 不种不耗；
        //     再瞄 A 作物本体（命中格=作物）→ 不覆盖不耗。
        hbF.setStack(0, RecipeRegistry::SeedId, 8);
        hbF.setSelectedSlot(0);
        const QVector3D hitSeedA = aimF(15.5f, 16.5f, 12.5f, 15.90f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        const QVector3D hitSeedG = aimF(33.5f, 16.5f, 30.5f, 15.90f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        const QVector3D hitSeedRe = aimF(15.5f, 16.5f, 12.5f, 16.5f, 16.5f, 2);
        pcF.placeBlock();
        pumpMsF(260);
        const bool seedOk = hitSeedA == QVector3D(12, 15, 16)
            && wF.blockAt(12, 16, 16) == BR::WheatCrop && wF.stateAt(12, 16, 16) == 0
            && hbF.countAt(0) == 7                                       // 生存消耗 1 种子
            && hitSeedG == QVector3D(30, 15, 16)
            && wF.blockAt(30, 16, 16) == BR::Air && hbF.countAt(0) == 7  // 非耕地拒种不耗
            && hitSeedRe == QVector3D(12, 16, 16)
            && wF.blockAt(12, 17, 16) == BR::Air && hbF.countAt(0) == 7; // 已种格不覆盖不耗
        if (!seedOk)
            qInfo().noquote() << "  [t1026a diag] seed hitA" << hitSeedA << "crop"
                              << int(wF.blockAt(12, 16, 16)) << "cnt" << hbF.countAt(0)
                              << "hitG" << hitSeedG << "gAbove" << int(wF.blockAt(30, 16, 16))
                              << "hitRe" << hitSeedRe << "reAbove" << int(wF.blockAt(12, 17, 16));
        // (c) 生长门对照株 rig：B 遮黑（6 邻石罩 → 天光 0）/ C 非耕地支撑 / D 已熟锚（stage 7）。
        wF.setBlock(16, 16, 16, BR::WheatCrop, 0);
        wF.setBlock(16, 17, 16, BR::Stone, 0); // 罩顶
        wF.setBlock(15, 16, 16, BR::Stone, 0); // 四侧罩 → B 格 6 邻全不透明 → skyLight 0
        wF.setBlock(17, 16, 16, BR::Stone, 0);
        wF.setBlock(16, 16, 15, BR::Stone, 0);
        wF.setBlock(16, 16, 17, BR::Stone, 0);
        wF.setBlock(20, 16, 16, BR::WheatCrop, 0);   // C：下方泥土（非耕地）→ 支撑门拒长
        wF.setBlock(24, 16, 16, BR::WheatCrop, 7);   // D：已熟锚 → 阶段不再动
        const bool lightPre = wF.skyLightAt(12, 16, 16) >= 9 && wF.skyLightAt(16, 16, 16) == 0
            && wF.skyLightAt(20, 16, 16) >= 9;
        // 确定性骰子复刻（world.cpp tickCropGrowth 同式：干耕地 1× 倍率 growPct=6、无雨；窗口序号自 0）。
        const auto diceHitsF = [&](int k, int stage, int cx, int cy, int cz) -> bool {
            const int mixedSeed = int(quint32(wF.seed()) ^ (quint32(k) * 0x9E3779B9u));
            return int(wF.hashVoxel(mixedSeed, cx, cy * 7 + stage, cz) & 0xFFFFu) % 100 < 6;
        };
        QVector<int> simAdvF; // 开露作物 A 的模拟升阶段窗口序列
        {
            int stage = 0;
            for (int k = 0; k < 3000 && stage < 7; ++k)
                if (diceHitsF(k, stage, 12, 16, 16)) { ++stage; simAdvF.push_back(k); }
            if (stage < 7) simAdvF.clear(); // 骰子流 3000 窗未熟 = 病态 seed → 守卫红（确定性，复跑恒定）
        }
        int darkHitF = -1; // 遮黑株 B「无光照门时会长的首窗」——阴性轮可达域守卫（摘门 → 必在泵域内生长）
        for (int k = 0; k < 3000 && darkHitF < 0; ++k)
            if (diceHitsF(k, 0, 16, 16, 16)) darkHitF = k;
        const int pumpW = simAdvF.isEmpty() ? -1 : std::max(simAdvF.last(), darkHitF);
        QVector<int> actAdvF; // 实际升阶段窗口序列（逐窗采样：25 tick = 1 窗）
        int prevStageF = 0;
        bool monoF = true;
        for (int k = 0; k <= pumpW; ++k) {
            for (int c = 0; c < 25; ++c) wF.tickCropGrowth(); // kCropTickInterval=25 tick = 1 窗
            const int st = wF.stateAt(12, 16, 16);
            if (st - prevStageF > 1 || st - prevStageF < 0 || st > 7) monoF = false;
            if (st - prevStageF == 1) actAdvF.push_back(k);
            prevStageF = st;
        }
        const bool growthOk = lightPre && !simAdvF.isEmpty() && darkHitF >= 0
            && actAdvF == simAdvF                                        // 实际 ≡ 模拟（确定性契约）
            && monoF                                                     // 每窗至多 +1 单调
            && wF.blockAt(12, 16, 16) == BR::WheatCrop && prevStageF == BR::WheatCropStageMax
            && wF.stateAt(16, 16, 16) == 0                               // 遮黑株不长（阴性轮敏感）
            && wF.stateAt(20, 16, 16) == 0                               // 非耕地支撑株不长
            && wF.stateAt(24, 16, 16) == BR::WheatCropStageMax;          // 已熟株恒 7
        if (!growthOk)
            qInfo().noquote() << "  [t1026a diag] growth lightPre" << lightPre << "simN" << simAdvF.size()
                              << "darkHit" << darkHitF << "actN" << actAdvF.size() << "mono" << monoF
                              << "stageA" << prevStageF << "stageB" << wF.stateAt(16, 16, 16)
                              << "stageC" << wF.stateAt(20, 16, 16) << "stageD" << wF.stateAt(24, 16, 16);
        // (d) 成熟收获（A 株已熟）：空手生存挖 → 恰 1 小麦 + 0-3× 种子（t1046 低-4 口径：0 种子合法 →
        //     只有种子数 >0 才弹第二件 → 掉落件数 1 或 2）。
        hbF.setStack(0, 0, 0);
        dropIdF.clear();
        dropCntF.clear();
        int progCntD = 0;
        const QMetaObject::Connection progConnD = QObject::connect(
            &pcF, &PlayerController::miningProgressChanged, &pcF, [&progCntD]() { ++progCntD; });
        const QVector3D hitHarvA = aimF(15.5f, 16.5f, 12.5f, 16.5f, 16.5f, 2);
        mineBlockF(12, 16, 16);
        QObject::disconnect(progConnD);
        qInfo().noquote() << "  [t1026a TDIAG d] postBlock" << int(wF.blockAt(12, 16, 16))
                          << "captured" << pcF.captured() << "progTicks" << progCntD
                          << "mining" << pcF.mining() << "prog" << pcF.miningProgress()
                          << "mode" << int(pcF.mode()) << "worldRunning" << pcF.worldRunning();
        bool matOk = hitHarvA == QVector3D(12, 16, 16) && wF.blockAt(12, 16, 16) == BR::Air
            && (dropIdF.size() == 1 || dropIdF.size() == 2);
        int wheatN = 0, seedTotal = 0;
        for (int i = 0; i < dropIdF.size(); ++i) {
            if (dropIdF[i] == RecipeRegistry::WheatId && dropCntF[i] == 1) ++wheatN;
            if (dropIdF[i] == RecipeRegistry::SeedId) seedTotal += dropCntF[i];
        }
        matOk = matOk && wheatN == 1 && seedTotal >= 0 && seedTotal <= 3
            && (seedTotal == 0 ? dropIdF.size() == 1 : dropIdF.size() == 2); // 0 种 → 单件（t1046 弹出门）
        if (!matOk)
            qInfo().noquote() << "  [t1026a diag] mature drops n" << dropIdF.size() << "wheat" << wheatN
                              << "seeds" << seedTotal << "hit" << hitHarvA;
        // (e) 未熟收获（H 株 stage 3，泵后种下防泵中生长漂移）：恰 1 件：仅 1× 种子。
        wF.setBlock(32, 16, 16, BR::WheatCrop, 3);
        dropIdF.clear();
        dropCntF.clear();
        const QVector3D hitHarvH = aimF(35.5f, 16.5f, 32.5f, 16.5f, 16.5f, 2);
        mineBlockF(32, 16, 16);
        const bool immOk = hitHarvH == QVector3D(32, 16, 16) && wF.blockAt(32, 16, 16) == BR::Air
            && dropIdF.size() == 1 && dropIdF[0] == RecipeRegistry::SeedId && dropCntF[0] == 1;
        qInfo().noquote() << "  [t1026a TDIAG e] postBlock" << int(wF.blockAt(32, 16, 16))
                          << "captured" << pcF.captured()
                          << "mining" << pcF.mining() << "prog" << pcF.miningProgress()
                          << "mode" << int(pcF.mode()) << "worldRunning" << pcF.worldRunning()
                          << "hit" << hitHarvH;
        if (!immOk)
            qInfo().noquote() << "  [t1026a diag] immature drops n" << dropIdF.size()
                              << "id0" << (dropIdF.isEmpty() ? -1 : dropIdF[0])
                              << "cnt0" << (dropCntF.isEmpty() ? -1 : dropCntF[0]) << "hit" << hitHarvH;
        QObject::disconnect(dropConnF);
        ok = hoeOk && seedOk && growthOk && matOk && immOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1026a wheat farming loop (real player path): hoe right-click converts "
                             "dirt AND grass tops to dry farmland in survival (hydration state 0, one "
                             "durability tick, stone refuses); 8 seeds plant a stage-0 wheat crop above "
                             "the farmland consuming exactly one seed while plain dirt and an already-"
                             "planted crop refuse; tick-pumped growth reproduces the deterministic "
                             "scatter dice exactly (actual advance windows == simulated windows, +1 "
                             "monotonic per window) maturing the open crop to stage 7 while the "
                             "stone-enclosed dark crop (skyLight 0 < 9), the no-farmland-support crop "
                             "and the already-mature anchor all hold stage; harvesting the mature crop "
                             "bare-handed yields exactly 1 wheat plus 0-3 seeds (t1046 Beta/1.0 seed "
                             "caliber: zero seeds legally emits no seed item) and harvesting the "
                             "stage-3 crop yields exactly 1 seed (negative-round sensitive: crop light "
                             "gate + harvest drop table)"
                          << (ok ? QString()
                                  : QStringLiteral("diag hoe=%1 seed=%2 growth=%3 mature=%4 imm=%5")
                                        .arg(hoeOk).arg(seedOk).arg(growthOk).arg(matOk).arg(immOk));
    });

    // ── P-t1026b 面包配方（3 小麦一行三格）+ 农业链源码钉（R19.21 t1026）──
    //   (a) 有序 3×3 顶/中/底行平移全通（shapedEqual 最小包围盒对齐 = MC 一行三格可在台内任意行摆放）→
    //       BreadId(0x20A) × 1；
    //   (b) 口径阴性：竖列不合（MC 面包仅横排；非镜像非旋转）/ 2×2 放不下 3 宽 / 3 种子串不顶小麦用；
    //   (c) 源码钉（滤注释 pinSet；阴性轮红腿：摘 world.cpp 光照门 → P-t1026a 遮黑株腿红 + cpp-crop-light-gate
    //       钉红；改 playercontroller.cpp 收割掉落表 → P-t1026a 收获腿红 + cpp-crop-drop 钉红）：
    //       锄转换 / 播种 / 生长门与写入 / 掉落表 / 草丛掉种分母 / 面包配方行 / 存档契约 id（Farmland=23、
    //       WheatCrop=25 枚举尾段既有位，漂移即红）。
    runLegMulti({ "t1026b bread recipe + farming source pins: three wheat in a row crafts 1 bread from the top, mid"
        "dle and bottom rows of a 3x3 table (shaped bounding-box translation = MC one-row-of-three calibe"
        "r), while a vertical column, a 2x2 grid and a row of seeds all refuse; source pins lock the hoe-"
        ">farmland and seed->crop wiring, the crop growth light/support gates and stage write, the harves"
        "t drop table (1 wheat + 0-3 seeds mature per t1046 Beta/1.0 caliber, 1 seed immature), the 1/8 t"
        "all-grass seed denominator, the bread recipe row and the save-contract block ids (Farmland=23, W"
        "heatCrop=25)diag rows=%1 negs=%2 pins=%3" }, [&]() {
        bool ok = true;
        const int Wf = RecipeRegistry::WheatId;
        const int gTop[9] = { Wf, Wf, Wf, 0, 0, 0, 0, 0, 0 };
        const int gMid[9] = { 0, 0, 0, Wf, Wf, Wf, 0, 0, 0 };
        const int gBot[9] = { 0, 0, 0, 0, 0, 0, Wf, Wf, Wf };
        const int gCol[9] = { 0, Wf, 0, 0, Wf, 0, 0, Wf, 0 };
        const int gTwo[4] = { Wf, Wf, Wf, 0 };
        const int gSeedRow[9] = { RecipeRegistry::SeedId, RecipeRegistry::SeedId, RecipeRegistry::SeedId,
                                  0, 0, 0, 0, 0, 0 };
        const auto breadAt = [](const int *g, int n) -> const RecipeRegistry::Recipe * {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, n);
            return (r && r->outputId == RecipeRegistry::BreadId && r->outputCount == 1) ? r : nullptr;
        };
        const bool rowsOk = breadAt(gTop, 3) && breadAt(gMid, 3) && breadAt(gBot, 3);
        const bool negsOk = !RecipeRegistry::match(gCol, 3)   // 竖列非面包（横排口径）
            && !RecipeRegistry::match(gTwo, 2)                // 2×2 容不下 3 宽（需工作台）
            && !RecipeRegistry::match(gSeedRow, 3);           // 种子不顶小麦原料
        if (!rowsOk || !negsOk)
            qInfo().noquote() << "  [t1026b diag] rows" << rowsOk << "negs" << negsOk;
        const QString exeDirB = QCoreApplication::applicationDirPath();
        const QString rootB = QDir(exeDirB + QStringLiteral("/..")).absolutePath();
        QStringList missB;
        missB << pinSet(rootB + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-hoe-convert", "m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::Farmland, quint8(hydr));"},
            {"cpp-seed-plant", "m_world->setBlock(wx, wy, wz, cs.cropBlockId, 0);"},
            {"cpp-crop-drop-wheat", "emit spawnItem(x, y, z, RecipeRegistry::WheatId, wheatCount);"},
            // t1046 低-4：小麦种子基准钉死 ~Beta/1.0 = 0-3（bounded(0,4) 上界开区间；旧 1-3 退役）。
            {"cpp-crop-drop-seed", "const int seedCount  = mature ? QRandomGenerator::global()->bounded(0, 4) : 1;"},
            {"cpp-crop-drop-seed-gate", "if (seedCount > 0)"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/Game/playercontroller.h"), {
            {"hdr-grass-seed-denom", "static constexpr int kTallGrassSeedDropDenom = 8;"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-crop-light-gate", "if (m_chunks.skyLightAt(c.x, c.y, c.z) < kCropMinLight) continue;"},
            {"cpp-crop-support-gate", "if (m_chunks.blockAt(c.x, c.y - 1, c.z) != BlockRegistry::Farmland)"},
            {"cpp-crop-stage-write", "anyChange |= setWaterSilent(g.x, g.y, g.z, g.id, quint8(g.stage + 1));"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/World/world.h"), {
            {"hdr-crop-min-light", "static constexpr int kCropMinLight     = 9;"},
            {"hdr-crop-grow-pct", "static constexpr int kCropGrowPct      = 6;"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/Game/recipe.cpp"), {
            {"cpp-bread-row", "{ RecipeRegistry::WheatId, RecipeRegistry::WheatId, RecipeRegistry::WheatId,"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/Core/blockregistry.h"), {
            {"hdr-farmland-id-contract", "Farmland       = 23,"},
            {"hdr-wheat-id-contract", "WheatCrop      = 25,"},
        });
        const bool pinsOkB = missB.isEmpty();
        if (!pinsOkB)
            qInfo().noquote() << "  [t1026b diag] pin miss:" << missB.join(QLatin1Char(','));
        ok = rowsOk && negsOk && pinsOkB;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1026b bread recipe + farming source pins: three wheat in a row crafts "
                             "1 bread from the top, middle and bottom rows of a 3x3 table (shaped "
                             "bounding-box translation = MC one-row-of-three caliber), while a vertical "
                             "column, a 2x2 grid and a row of seeds all refuse; source pins lock the "
                             "hoe->farmland and seed->crop wiring, the crop growth light/support gates "
                             "and stage write, the harvest drop table (1 wheat + 0-3 seeds mature "
                             "per t1046 Beta/1.0 caliber, 1 seed immature), the 1/8 tall-grass seed "
                             "denominator, the bread recipe row "
                             "and the save-contract block ids (Farmland=23, WheatCrop=25)"
                          << (ok ? QString()
                                  : QStringLiteral("diag rows=%1 negs=%2 pins=%3")
                                        .arg(rowsOk).arg(negsOk).arg(pinsOkB));
    });

    // ── P-t1028a 音符盒红石触发链（R19.21 t1028；World 接收器真消费端探针，powerTntTriggered 计数模式）──
    //   (a) 初始 off：tick 泵零误触发（无源静默）；
    //   (b) 通电上升沿：恰一响（noteBlockPlayed 恰 +1），pitch 参数 = 预调音 9（A4）、family 参数 =
    //       下方方块材质投影（planks→bass(1)），bit5 记忆位置位且音高段原样保留；
    //   (c) 稳定通电续泵：不复响（bit5 记忆位做真沿——摘沿判定的阴性轮此处红）；
    //   (d) 断电下降沿：静音 + bit5 清位（重臂就绪）；
    //   (e) 再通电：再响（重臂闭环，恰 2 次）；
    //   (f) 音色族四族 + 悬空兜底参数断言：stone→kick(2) / sand→snare(3) / glass→hat(4)（t1046 低-2
    //       第四族）/ air→piano(0)。
    runLegMulti({ "t1028a note-block redstone chain: an idle rig never fires; the lever rising edge fires noteBlock"
        "Played exactly once carrying the tuned pitch (9=A4) and the below-block timbre family (planks=ba"
        "ss) and latches the powered memory bit without disturbing the pitch field; sustained power never"
        " re-fires (true edge via the state memory bit); the falling edge is silent and clears the memory"
        " bit; re-powering fires again (re-arm); family projection asserts stone=kick, sand=snare, glass="
        "hat (t1046 fourth family) and floating=piano (negative-round sensitive: edge-judgment removal, g"
        "lass-hat mapping removal)diag idle=%1 rise=%2 hold=%3 fall=%4 rearm=%5 fam=%6 played=%7 pitch=%8"
        " fam=%9 famFailLeg=%10 exp=%11 got=%12 gotX=%13 gotPlayed=%14 gotPitch=%15" }, [&]() {
        bool ok = true;
        World wT28a;
        wT28a.setWidth(48); wT28a.setDepth(64); wT28a.setHeight(96); wT28a.setSeed(10281);
        // 五 rig（列距 4）：各坐不同下方材质（planks/stone/sand/glass/悬空）；拉杆各贴 -X 邻格独立供电。
        //   工作带 y=20 显式净空 + y=19 石板地板（t1025 fBm 地形教训：禁赌 worldgen）；
        //   悬空 rig 的地板格挖空（下方真 Air → piano 兜底腿）。
        struct NoteRig { int x; quint8 below; int family; };
        const NoteRig rigs[5] = { { 8, BR::Planks, 1 }, { 12, BR::Stone, 2 },
                                  { 16, BR::Sand, 3 }, { 20, BR::Glass, 4 },
                                  { 24, BR::Air, 0 } };
        for (int x = 4; x <= 26; ++x)
            for (int z = 20; z <= 24; ++z) {
                for (int y = 20; y <= 24; ++y) wT28a.setBlock(x, y, z, BR::Air, 0);
                wT28a.setBlock(x, 19, z, BR::Stone, 0);
            }
        for (const NoteRig &r : rigs) {
            if (r.below != BR::Air) wT28a.setBlock(r.x, 19, 22, r.below, 0);
            else                    wT28a.setBlock(r.x, 19, 22, BR::Air, 0); // 悬空：挖空地板
            wT28a.setBlock(r.x, 20, 22, BR::NoteBlock, quint8(9)); // 预调音 A4（pitch=9，探针直写跳过调音链）
            wT28a.setBlock(r.x - 1, 20, 22, BR::Lever, 0);         // 拉杆贴 -X（初始 off）
        }
        int played = 0, lastPitch = -1, lastFamily = -1, lastX = -1;
        QObject::connect(&wT28a, &World::noteBlockPlayed, &wT28a,
                         [&](int x, int, int, int pitch, int family) {
                             ++played; lastPitch = pitch; lastFamily = family; lastX = x;
                         });
        const auto leverSet = [&](const NoteRig &r, int on) {
            wT28a.setBlock(r.x - 1, 20, 22, BR::Lever, quint8(on));
        };
        const auto rigState = [&](const NoteRig &r) { return wT28a.stateAt(r.x, 20, 22); };
        // (a) 初始 off：零误触发。(b)-(e) 全边沿语义走 rig 0（planks→bass）。(f) 族参数走 rig 1..4。
        const NoteRig &r0 = rigs[0];
        tickN(wT28a, 4);
        const bool okIdle = played == 0;
        leverSet(r0, 1);
        tickN(wT28a, 4);
        const bool okRise = played == 1 && lastPitch == 9 && lastFamily == 1 && lastX == r0.x
            && (rigState(r0) & BR::NoteBlockStatePoweredFlag) != 0   // 记忆位置位
            && (rigState(r0) & BR::NoteBlockStatePitchMask) == 9;    // 音高段写位不扰动
        tickN(wT28a, 8);
        const bool okHold = played == 1;                              // 稳定通电不复响
        leverSet(r0, 0);
        tickN(wT28a, 4);
        const bool okFall = played == 1                               // 下降沿静音
            && (rigState(r0) & BR::NoteBlockStatePoweredFlag) == 0;   // 记忆位清（重臂就绪）
        leverSet(r0, 1);
        tickN(wT28a, 4);
        const bool okRearm = played == 2 && lastPitch == 9;           // 再通再响
        bool okFam = true;
        int famFail = -1, famExp = -1, famGot = -1, famGotX = -1, famGotPlayed = -1, famGotPitch = -1;
        for (int i = 1; i < 5; ++i) {
            const NoteRig &r = rigs[i];
            const int played0 = played;
            leverSet(r, 1);
            tickN(wT28a, 4);
            if (played != played0 + 1 || lastPitch != 9 || lastFamily != r.family || lastX != r.x) {
                okFam = false;
                if (famFail < 0) {
                    famFail = i; famExp = r.family; famGot = lastFamily; famGotX = lastX;
                    famGotPlayed = played; famGotPitch = lastPitch;
                }
            }
            leverSet(r, 0);
            tickN(wT28a, 4); // 复位（防跨 rig 记忆串扰）
        }
        QObject::disconnect(&wT28a, nullptr, nullptr, nullptr);
        ok = okIdle && okRise && okHold && okFall && okRearm && okFam;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1028a note-block redstone chain: an idle rig never fires; the lever "
                             "rising edge fires noteBlockPlayed exactly once carrying the tuned pitch "
                             "(9=A4) and the below-block timbre family (planks=bass) and latches the "
                             "powered memory bit without disturbing the pitch field; sustained power "
                             "never re-fires (true edge via the state memory bit); the falling edge "
                             "is silent and clears the memory bit; re-powering fires again (re-arm); "
                             "family projection asserts stone=kick, sand=snare, glass=hat (t1046 "
                             "fourth family) and floating=piano "
                             "(negative-round sensitive: edge-judgment removal, glass-hat mapping removal)"
                          << (ok ? QString()
                                  : QStringLiteral("diag idle=%1 rise=%2 hold=%3 fall=%4 rearm=%5 fam=%6 "
                                                   "played=%7 pitch=%8 fam=%9 famFailLeg=%10 exp=%11 "
                                                   "got=%12 gotX=%13 gotPlayed=%14 gotPitch=%15")
                                        .arg(okIdle).arg(okRise).arg(okHold).arg(okFall)
                                        .arg(okRearm).arg(okFam).arg(played)
                                        .arg(lastPitch).arg(lastFamily)
                                        .arg(famFail).arg(famExp).arg(famGot)
                                        .arg(famGotX).arg(famGotPlayed).arg(famGotPitch));
    });

    // ── P-t1028b 真实玩家路径：右键调音 round-trip（25 次≡回 0，MC 口径 25 档）+ 调音发声链
    //    （noteBlockTuned 携新音高 + 音名「A4」单一权威）+ 潜行旁路门（review0909 #2）+ 攻击发声
    //    （左键按下沿，pitch=当前调音，挖掘照常破掉掉自身）──阴性轮敏感：调音回绕摘 mod → round-trip
    //    腿红；摘音符盒 !sneakPlace 门 → 潜行腿红（潜行右键仍调音 + 木板未落地）。
    runLegMulti({ "t1028b note-block player path: 25 survival right-clicks on the note block cycle the pitch throug"
        "h 1..24 and wrap back to 0 exactly (25-slot round trip, MC caliber) emitting noteBlockTuned each"
        " time with the ninth carrying note name A4; sneaking with a held block and right-clicking bypass"
        "es tuning and places the block on the adjacent face instead (review0909 #2 sneakPlace gate, MC s"
        "neak-use bypass); the left-click attack edge fires noteBlockAttackPlayed exactly once with the c"
        "urrent pitch (0) and below-block family (planks=bass) while survival mining still progresses and"
        " drops the note block itself (negative-round sensitive: tuning wrap removal, sneakPlace gate rem"
        "oval)diag tun=%1 sneak=%2 atkOnce=%3 mined=%4 atkP=%5 atkF=%6 drops=%7" }, [&]() {
        World wT28b;
        wT28b.setWidth(48); wT28b.setDepth(48); wT28b.setHeight(96); wT28b.setSeed(10282);
        Hotbar hbT28b;
        PlayerController pcT28b; // t814 真消费端模式（无窗口直造；挂窗 grab 载体同 P-t945/t1026a）
        pcT28b.setWorld(&wT28b);
        pcT28b.setHotbar(&hbT28b);
        QQuickWindow winT28b;
        pcT28b.setParentItem(winT28b.contentItem());
        pcT28b.grab();
        // rig：y=15 工作层；planks 地台 + 音符盒（下方 planks → 攻击发声 family=bass(1)）；上方净空。
        const int nx28b = 12, ny28b = 15, nz28b = 16;
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 20; ++z) {
                for (int y = 15; y <= 20; ++y) wT28b.setBlock(x, y, z, BR::Air, 0);
                wT28b.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT28b.setBlock(nx28b, ny28b, nz28b, BR::NoteBlock, 0); // 初始调音 0（C4）
        // 调音 / 攻击信号记录（等价 Main.qml 路由直连计数）。
        QVector<int> tunedPitches;
        QVector<QString> tunedNames;
        int attackPlayed = 0, attackPitch = -1, attackFamily = -1;
        const QMetaObject::Connection cTun = QObject::connect(
            &pcT28b, &PlayerController::noteBlockTuned, &pcT28b,
            [&](int, int, int, int pitch, int, const QString &name) {
                tunedPitches.push_back(pitch); tunedNames.push_back(name);
            });
        const QMetaObject::Connection cAtk = QObject::connect(
            &pcT28b, &PlayerController::noteBlockAttackPlayed, &pcT28b,
            [&](int, int, int, int pitch, int family) {
                ++attackPlayed; attackPitch = pitch; attackFamily = family;
            });
        // 瞄准帮手（t1026a 同款：re-grab 光标归零 → loadSavedState 定向 → tick 刷射线）。
        const auto aimT28b = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ, int mode) {
            const float ex = feetX, ey = 15.0f + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT28b.release();
            pcT28b.grab();
            pcT28b.loadSavedState(feetX, 15.0f, feetZ, yaw, pit, mode);
            pcT28b.tick();
            return pcT28b.hitBlock();
        };
        const auto pumpMsT28b = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 调音 round-trip：空手生存瞄音符盒顶面，右键 25 次 → 音高序列 1..24,0（(p+1)%25 回绕）
        //     恰 25 次 noteBlockTuned；第 9 次音名断言「A4」（noteBlockNoteName 单一权威，n=9=440Hz）；
        //     终态 state 音高段 == 0（round-trip 契约：25 次≡回 0）。
        // t1040 rig 加固（t1030 同式）：空手调音腿在 QML 绑定下 selectedBlock=Air（空手分支 miss 时
        //   fall-through 到 m_selectedBlock==Air 通用放置守卫）——显式归 Air 建模，防 aim 落空踩构造
        //   默认 Stone 误放误耗（音符盒分支拦截不受影响）。
        pcT28b.setSelectedBlock(int(BR::Air));
        const QVector3D hitTun = aimT28b(14.5f, 16.5f, float(nx28b) + 0.5f, float(ny28b) + 0.9f,
                                         float(nz28b) + 0.5f, 2 /* Survival */);
        for (int i = 0; i < 25; ++i) {
            pcT28b.placeBlock();
            pumpMsT28b(260);
        }
        bool okTun = hitTun == QVector3D(nx28b, ny28b, nz28b)
            && tunedPitches.size() == 25
            && tunedPitches.front() == 1 && tunedPitches.back() == 0
            && tunedPitches[8] == 9 && tunedNames[8] == QStringLiteral("A4")
            && (wT28b.stateAt(nx28b, ny28b, nz28b) & BR::NoteBlockStatePitchMask) == 0;
        if (!okTun)
            qInfo().noquote() << "  [t1028b diag] tune hit" << hitTun << "n" << tunedPitches.size()
                              << "front" << (tunedPitches.isEmpty() ? -1 : tunedPitches.front())
                              << "back" << (tunedPitches.isEmpty() ? -1 : tunedPitches.back())
                              << "p9" << (tunedPitches.size() > 8 ? tunedPitches[8] : -1)
                              << "name9" << (tunedNames.size() > 8 ? tunedNames[8] : QString())
                              << "st" << wT28b.stateAt(nx28b, ny28b, nz28b);
        // (1b) 潜行旁路门（review0909 #2）：潜行持方块右键音符盒 → 旁路调音走放置（MC 潜行右键旁路
        //      useBlock 口径，同工作台/箱子等分支；非潜行右键 → 调音已由 (1) 的 25 次空手右键证明——
        //      门只拦潜行路径）。瞄准 +X 侧脸（放置落侧邻格，顶面留给 (2) 攻击腿的瞄准惯例）。
    //    阴性轮敏感：摘 playercontroller.cpp 音符盒分支 sneakPlaceBlock 门（t1050 起旁路判据名）
    //    → 潜行右键仍调音（tunedPitches 增长、音高位翻、侧邻格无木板）→ 本腿红。
        const int nTunedBeforeSneak = tunedPitches.size();
        const QVector3D hitSneak = aimT28b(14.5f, 16.5f, float(nx28b) + 0.9f, float(ny28b) + 0.5f,
                                          float(nz28b) + 0.5f, 2);
        pcT28b.setKey(Qt::Key_Shift, true);  // 潜行（placeBlock 的 sneakPlace = m_keys 原始键态，t523 口径）
        // 手持木板：placeBlock 的放置路径读 player.selectedBlock（Q_PROPERTY）——Hotbar 光标栈
        //   setHeldBlock 不喂此面（生产由 QML 绑定 player.selectedBlock 承担，探针无 QML 引擎），
        //   C++ 直调是 t945/t973 同款惯例。缺此行 m_selectedBlock 落默认 Stone（playercontroller.h
        //   t06 默认）→ 潜行旁路腿放置照常发生但材质错成 Stone（修前红根因：非门断线）。
        pcT28b.setSelectedBlock(int(BR::Planks));
        pcT28b.placeBlock();
        pumpMsT28b(260);
        pcT28b.setKey(Qt::Key_Shift, false); // 松潜行（站起复位，(2) 攻击腿站立眼位瞄准惯例不变）
        pcT28b.setSelectedBlock(int(BR::Air)); // 清手持回空手（(2) 左键攻击语义与持物隔离）
        const bool okSneakPlace = hitSneak == QVector3D(nx28b, ny28b, nz28b)
            && tunedPitches.size() == nTunedBeforeSneak // 调音零次（潜行旁路 useBlock）
            && (wT28b.stateAt(nx28b, ny28b, nz28b) & BR::NoteBlockStatePitchMask) == 0 // 音高位不变
            && wT28b.blockAt(nx28b + 1, ny28b, nz28b) == BR::Planks; // 木板落侧脸邻格（放置成功）
        // (2) 攻击发声：生存左键按下沿 → 恰一响（pitch=当前调音 0，family=下方 planks→bass 1）；
        //     发声不占挖掘链——照常累积破块（MC 攻击响 + 持续挖可破口径）→ 破后 Air + 掉自身 ×1。
        QVector<int> dropIds28b;
        const QMetaObject::Connection cDrop = QObject::connect(
            &pcT28b, &PlayerController::spawnItem, &pcT28b,
            [&](int, int, int, int id, int, const QVariantList &, const QString &, int) {
                dropIds28b.push_back(id);
            });
        const QVector3D hitAtk = aimT28b(14.5f, 16.5f, float(nx28b) + 0.5f, float(ny28b) + 0.9f,
                                         float(nz28b) + 0.5f, 2);
        pcT28b.beginMining();
        const bool okAtkOnce = attackPlayed == 1 && attackPitch == 0 && attackFamily == 1
            && hitAtk == QVector3D(nx28b, ny28b, nz28b);
        // 挖掘泵（t1026a 同款：每 tick busy-wait ≥17ms 保 updateMining 墙钟 dt>0）。
        for (int i = 0; i < 6000 && wT28b.blockAt(nx28b, ny28b, nz28b) != BR::Air; ++i) {
            QElapsedTimer dtw;
            dtw.start();
            while (dtw.elapsed() < 17)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pcT28b.tick();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        }
        pcT28b.endMining();
        const bool okMined = wT28b.blockAt(nx28b, ny28b, nz28b) == BR::Air
            && dropIds28b.size() == 1 && dropIds28b[0] == int(BR::NoteBlock)
            && attackPlayed == 1; // 挖掘过程不补响（按下沿恰一次）
        QObject::disconnect(cDrop);
        QObject::disconnect(cTun);
        QObject::disconnect(cAtk);
        pcT28b.release();
        winT28b.deleteLater();
        const bool okB = okTun && okSneakPlace && okAtkOnce && okMined;
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1028b note-block player path: 25 survival right-clicks on the note "
                             "block cycle the pitch through 1..24 and wrap back to 0 exactly "
                             "(25-slot round trip, MC caliber) emitting noteBlockTuned each time "
                             "with the ninth carrying note name A4; sneaking with a held block and "
                             "right-clicking bypasses tuning and places the block on the adjacent "
                             "face instead (review0909 #2 sneakPlace gate, MC sneak-use bypass); "
                             "the left-click attack edge "
                             "fires noteBlockAttackPlayed exactly once with the current pitch (0) "
                             "and below-block family (planks=bass) while survival mining still "
                             "progresses and drops the note block itself (negative-round "
                             "sensitive: tuning wrap removal, sneakPlace gate removal)"
                          << (okB ? QString()
                                  : QStringLiteral("diag tun=%1 sneak=%2 atkOnce=%3 mined=%4 atkP=%5 atkF=%6 drops=%7")
                                        .arg(okTun).arg(okSneakPlace).arg(okAtkOnce).arg(okMined)
                                        .arg(attackPitch).arg(attackFamily).arg(dropIds28b.size()));
    });

    // ── P-t1028c 音符盒配方（8 木板环 + 红石粉芯，MC 1.0 同料）+ 全链源码钉 ──
    //   (a) 配方：环 + 芯 → NoteBlock ×1（最小包围盒 3×3）；环 + 空芯 = 箱子（环本身不是音符盒，
    //       防形状混recipes）；2×2 放不下。
    //   (b) 源码钉（滤注释 pinSet；阴性轮红腿：摘 world.cpp 沿判定 → P-t1028a 腿红 + cpp-note-edge 钉红；
    //       摘调音回绕 → P-t1028b round-trip 腿红 + hdr-note-tuned 钉红）。
    runLegMulti({ "t1028c note-block recipe + full-chain source pins: eight planks ringed around one redstone dust "
        "crafts exactly 1 note block (MC 1.0 caliber, 3x3 table) while the plain empty-centered ring (a c"
        "hest) refuses; source pins lock the rising-edge memory-bit judgment and the World semantic signa"
        "l, the tuning and attack emissions in PlayerController, the save-contract id (NoteBlock=143) and"
        " the 25-slot pitch state helpers with the note-name and timbre-family single authorities, the Au"
        "dioManager playNote playback entry and the QML redstone/tuned routing, the recipe row and the ge"
        "n_note_piano synthesis generatordiag rec=%1 neg=%2 pins=%3" }, [&]() {
        bool ok = true;
        const int P = int(BlockRegistry::Planks);
        const int gNote[9] = { P, P, P, P, RecipeRegistry::RedstoneId, P, P, P, P };
        const int gRing[9] = { P, P, P, P, 0, P, P, P, P };
        // 注：match(grid, n) 的 n = 网格**维度**（2=背包 2×2 / 3=工作台 3×3），非元素数（t1026b 同口径）。
        const auto noteAt = [](const int *g, int n) -> const RecipeRegistry::Recipe * {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, n);
            return (r && r->outputId == int(BlockRegistry::NoteBlock) && r->outputCount == 1) ? r : nullptr;
        };
        const bool recOk = noteAt(gNote, 3) != nullptr;
        const RecipeRegistry::Recipe *ring = RecipeRegistry::match(gRing, 3);
        const bool negOk = ring == nullptr || ring->outputId != int(BlockRegistry::NoteBlock); // 空芯环非音符盒（= 箱子，防形状混同）
        if (!recOk || !negOk)
            qInfo().noquote() << "  [t1028c diag] rec" << recOk << "neg" << negOk;
        const QString exeDirC = QCoreApplication::applicationDirPath();
        const QString rootC = QDir(exeDirC + QStringLiteral("/..")).absolutePath();
        QStringList missC;
        missC << pinSet(rootC + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-note-edge", "const bool was = (st & BlockRegistry::NoteBlockStatePoweredFlag) != 0;"},
            {"cpp-note-emit", "emit noteBlockPlayed(x, y, z, BlockRegistry::noteBlockPitch(st),"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/World/world.h"), {
            {"hdr-note-signal", "void noteBlockPlayed(int x, int y, int z, int pitch, int family);"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-note-tune-emit", "emit noteBlockTuned(m_hitBx, m_hitBy, m_hitBz, pitch, family,"},
            {"cpp-note-atk-emit", "emit noteBlockAttackPlayed(m_hitBx, m_hitBy, m_hitBz, pitch, family);"},
            // review0909 #2：调音分支补潜行旁路门（对齐同函数容器/机关件分支模式）。
            {"cpp-note-sneak-gate", "if (!sneakPlaceBlock && hitId == BlockRegistry::NoteBlock) {"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/Core/blockregistry.h"), {
            {"hdr-note-id-contract", "NoteBlock         = 143,"},
            {"hdr-note-tuned", "static quint8 noteBlockTunedState(quint8 state)"},
            {"hdr-note-pitchcount", "static constexpr int NoteBlockPitchCount = 25;"},
            {"hdr-note-pitchmask", "static constexpr quint8 NoteBlockStatePitchMask = 0x1F;"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/Core/blockregistry.cpp"), {
            {"hdr-note-family", "BlockRegistry::NoteTimbreFamily BlockRegistry::noteTimbreFamily(quint8 belowId)"},
            {"hdr-note-notename", "QString BlockRegistry::noteBlockNoteName(int pitch)"},
            // t1046 低-2：玻璃=hat 第四族（id 直判，不动 GroupStone——脱组代价登记面）。
            {"cpp-note-glass-hat", "if (belowId == Glass) return NoteTimbreHat;"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/Audio/audiomanager.cpp"), {
            {"aud-note-play", "void AudioManager::playNote(int pitch, int family)"},
            {"aud-note-rate", "d->replayNote(c, m_volume * 0.9f, rate);"},
            // review0909 #3b：Audio 层手抄镜像钉（与 hdr-note-pitchcount 成对——两侧漂移即红，
            // 替代跨层 include 的分层保留同步方案）。
            {"aud-note-pitchcount", "static constexpr int kNotePitchCount = 25;"},
            // t1046 低-2 hat 倍移速率 + 评审 #4 noteClips 析构补齐（app 目标独有编译单元：
            // 矩阵不编 audiomanager.cpp → 源码钉即该面唯一探针，编译验证 = voxelsandbox 重建）。
            {"aud-note-hat-rate", "case 4: rate = 3.0f; break;"},
            {"aud-note-deinit", "ma_sound_uninit(&d->noteClips[size_t(n)].sound);"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-note-redstone", "function onNoteBlockPlayed(x, y, z, pitch, family) { audio.playNote(pitch, family) }"},
            {"qml-note-tuned", "function onNoteBlockTuned(x, y, z, pitch, family, noteName) {"},
        });
        missC << pinSet(rootC + QStringLiteral("/src/Game/recipe.cpp"), {
            {"cpp-note-recipe", "int(BlockRegistry::Planks), RecipeRegistry::RedstoneId,  int(BlockRegistry::Planks),"},
        });
        missC << pinSet(rootC + QStringLiteral("/tools/build_sounds.py"), {
            {"py-note-gen", "def gen_note_piano(n):"},
        });
        const bool pinsOkC = missC.isEmpty();
        if (!pinsOkC)
            qInfo().noquote() << "  [t1028c diag] pin miss:" << missC.join(QLatin1Char(','));
        ok = recOk && negOk && pinsOkC;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1028c note-block recipe + full-chain source pins: eight planks "
                             "ringed around one redstone dust crafts exactly 1 note block (MC 1.0 "
                             "caliber, 3x3 table) while the plain empty-centered ring (a chest) "
                             "refuses; source pins lock the rising-edge memory-bit judgment and "
                             "the World semantic signal, the tuning and attack emissions in "
                             "PlayerController, the save-contract id (NoteBlock=143) and the "
                             "25-slot pitch state helpers with the note-name and timbre-family "
                             "single authorities, the AudioManager playNote playback entry and "
                             "the QML redstone/tuned routing, the recipe row and the "
                             "gen_note_piano synthesis generator"
                          << (ok ? QString()
                                  : QStringLiteral("diag rec=%1 neg=%2 pins=%3")
                                        .arg(recOk).arg(negOk).arg(pinsOkC));
    });

    // ── P-t1030a 骨粉合成数量 + 右键催熟行为链（R19.22 t1030；真玩家路径 placeBlock + Hotbar 消耗）──
    //   盘点回标：功能本体 t447（BonemealId 0x232 注册 / 1 骨头→3 骨粉 shapeless 配方）/ t791
    //   （World::applyBonemeal 统一入口 +2..3 阶段）既已交付，t791 探针为 World 层直调锁数值分布；
    //   本探针补 Game 层行为链（合成数量契约 / placeBlock 分流消耗 / 创造豁免 / 封顶与成熟口径）：
    //   (a) 合成：1 骨头 shapeless 单放 → 3 骨粉（2×2 背包栏与 3×3 工作台同命中，MC bone→3 bone meal
    //       1:3 产出比）；2 骨头多重集无配方（阴性）。
    //   (b) 生存催熟（真 placeBlock 链）：持骨粉右键 WheatCrop（方块 25，state=阶段 0..7）→ 每次推进
    //       恰在 +2..+3 带内（钳顶步 after==7 例外）+ 槽内恰 -1 + id 不漂移；从 stage 2 连施到封顶
    //       用次 ∈ {2,3}（数学保证与骰子分布无关：每步 ≥2 → 最多 3 步，存在 +3 → 最少 2 步）；封顶后
    //       再施 = 登记口径「无效应不消耗」（world.cpp applyBonemeal 对 st>=WheatCropStageMax 返
    //       false → playercontroller 不耗不挥；MC 亦可为「消耗无生长」，本工程选不消耗并钉死）。
    //   (c) 创造催熟：同链 Creative 模式 → 阶段照常推进而槽内恒定（创造不耗，本工程创造口径先例：
    //       同种子 / 蛋 / 桶消耗豁免模式）。
    //   骰子确定性分布（hashVoxel(seed⊕使用序号×φ)）已由 t791 探针锁定；催熟为瞬时 use，无挖掘 /
    //   进度 tick 链 → 无 busy-wait 依赖（t1022/t1026 的 dt 坑不适用）。阴性轮敏感：playercontroller.cpp
    //   骨粉分流判据恒假化（false && 前缀，Edit 反向 restore）→ (b)(c) 全红（阶段不推、count 不动），
    //   (a) 配方腿与 P-t1030b 的 pins 不受影响（world.cpp / recipe 层未动）→ 恰 P-t1030a 红。
    runLegMulti({ "t1030a bonemeal craft count + right-click growth chain (real player path): one bone crafts 3 bon"
        "e meal shapeless in both the 2x2 inventory grid and a 3x3 table slot while two bones match nothi"
        "ng; survival right-clicks on an immature wheat crop advance it exactly +2..+3 stages per use (cl"
        "amp step to stage 7 allowed) consuming exactly one bone meal each time with the id preserved, ma"
        "turing from stage 2 in 2-3 uses; applying to the mature crop is the registered no-effect no-cons"
        "ume caliber (stage held, count held); in creative mode two uses advance the crop identically whi"
        "le the stack stays untouched (negative-round sensitive: playercontroller bonemeal branch deactiv"
        "ation)diag rec=%1 grow=%2 mature=%3 creative=%4" }, [&]() {
        bool ok = true;
        const int B = RecipeRegistry::BoneId, M = RecipeRegistry::BonemealId;
        // (a) 配方数量：2×2 背包栏 + 3×3 工作台单放均 1 骨头 → 3 骨粉（shapeless）；2 骨头无配方。
        const int g2A[4] = { B, 0, 0, 0 };
        const int g3A[9] = { B, 0, 0, 0, 0, 0, 0, 0, 0 };
        const int g2twoA[4] = { B, B, 0, 0 };
        const auto mealAtA = [](const int *g, int n) -> const RecipeRegistry::Recipe * {
            const RecipeRegistry::Recipe *r = RecipeRegistry::match(g, n);
            return (r && r->outputId == RecipeRegistry::BonemealId && r->outputCount == 3 && r->shapeless)
                       ? r : nullptr;
        };
        const bool recOkA = mealAtA(g2A, 2) != nullptr && mealAtA(g3A, 3) != nullptr
            && RecipeRegistry::match(g2twoA, 2) == nullptr;
        if (!recOkA)
            qInfo().noquote() << "  [t1030a diag] recipe g2" << (mealAtA(g2A, 2) != nullptr)
                              << "g3" << (mealAtA(g3A, 3) != nullptr)
                              << "twoBoneNeg" << (RecipeRegistry::match(g2twoA, 2) == nullptr);
        // rig：40×40×32 seed 10301（t1026a 净空纪律：石板地板 + 工作带显式清空 → 天光满）。
        World wA;
        wA.setWidth(40);
        wA.setDepth(40);
        wA.setHeight(32);
        wA.setSeed(10301);
        for (int x = 4; x <= 35; ++x)
            for (int z = 10; z <= 22; ++z) {
                wA.setBlock(x, 15, z, BR::Stone, 0);
                for (int y = 16; y <= 31; ++y) wA.setBlock(x, y, z, BR::Air, 0);
            }
        Hotbar hbA;
        PlayerController pcA; // t814 真消费端模式（无窗口直造；挂窗 grab 载体同 P-t945/t1026a/t1028b）
        pcA.setWorld(&wA);
        pcA.setHotbar(&hbA);
        // 显式建模真实游戏的 selectedBlock 接线（Main.qml `selectedBlock: hotbarVM.selectedBlockId` →
        //   Hotbar::selectedBlockId 材料段→Air）：探针无 QML 绑定，m_selectedBlock 会保持构造默认 Stone
        //   ——阴性轮摘骨粉分流时 fall-through 走通用放置路径（放默认方块 + 扣选中栈）污染计数断言。
        //   本探针全部腿持骨粉（材料段 → 恒 Air）→ 构造后设一次即可。
        pcA.setSelectedBlock(BR::Air);
        QQuickWindow winA;
        pcA.setParentItem(winA.contentItem());
        pcA.grab();
        // 瞄准帮手（t1026a 同款：re-grab 光标归零 → loadSavedState 定向 + 模式 → tick 刷射线）。
        const auto aimA = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ, int mode) {
            const float ex = feetX, ey = 16.0f + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcA.release();
            pcA.grab();
            pcA.loadSavedState(feetX, 16.0f, feetZ, yaw, pitch, mode);
            pcA.tick();
            return pcA.hitBlock();
        };
        const auto pumpMsA = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (b) 生存催熟：W 株 x=12（耕地支撑 + stage2 起始）；持 5 骨粉右键连施到封顶。
        wA.setBlock(12, 15, 16, BR::Farmland, 0);
        wA.setBlock(12, 16, 16, BR::WheatCrop, 2);
        hbA.setStack(0, M, 5);
        hbA.setSelectedSlot(0);
        const QVector3D hitWA = aimA(15.5f, 16.5f, 12.5f, 16.5f, 16.5f, 2 /* Survival */);
        int usedA = 0, badStepA = -1, badDeltaA = 0, badCntA = 0;
        while (wA.stateAt(12, 16, 16) < BR::WheatCropStageMax && usedA < 5) {
            const int before = wA.stateAt(12, 16, 16);
            const int cntBefore = hbA.countAt(0);
            pcA.placeBlock();
            pumpMsA(260);
            ++usedA;
            const int after = wA.stateAt(12, 16, 16);
            const int d = after - before;
            // 推进带 +2..+3（钳顶步 after==7 合法）+ 生存恰耗 1 + id 不漂移。
            if ((d != 2 && d != 3 && after != BR::WheatCropStageMax)
                || hbA.countAt(0) != cntBefore - 1
                || wA.blockAt(12, 16, 16) != BR::WheatCrop) {
                badStepA = usedA;
                badDeltaA = d;
                badCntA = cntBefore - hbA.countAt(0);
                break;
            }
        }
        const bool growOkA = hitWA == QVector3D(12, 16, 16)
            && wA.stateAt(12, 16, 16) == BR::WheatCropStageMax
            && usedA >= 2 && usedA <= 3 && badStepA < 0;
        if (!growOkA)
            qInfo().noquote() << "  [t1030a diag] grow hit" << hitWA << "stage"
                              << wA.stateAt(12, 16, 16) << "used" << usedA
                              << "badStep" << badStepA << "badDelta" << badDeltaA
                              << "badCnt" << badCntA << "cnt" << hbA.countAt(0);
        // 成熟施用（登记口径钉死）：stage==7 再施 → 无效应不消耗（阶段不动、count 不减）。
        pumpMsA(260);
        const int cntMatA = hbA.countAt(0);
        pcA.placeBlock();
        pumpMsA(260);
        const bool matureOkA = hbA.countAt(0) == cntMatA
            && wA.stateAt(12, 16, 16) == BR::WheatCropStageMax
            && wA.blockAt(12, 16, 16) == BR::WheatCrop;
        if (!matureOkA)
            qInfo().noquote() << "  [t1030a diag] mature cnt" << hbA.countAt(0) << "was" << cntMatA
                              << "stage" << wA.stateAt(12, 16, 16);
        // (c) 创造催熟：C 株 x=20（stage0 起始）Creative 模式连施两次 → 推进照常 + count 恒 3。
        wA.setBlock(20, 15, 16, BR::Farmland, 0);
        wA.setBlock(20, 16, 16, BR::WheatCrop, 0);
        hbA.setStack(0, M, 3);
        hbA.setSelectedSlot(0);
        const QVector3D hitCA = aimA(15.5f, 16.5f, 20.5f, 16.5f, 16.5f, 1 /* Creative */);
        pcA.placeBlock();
        pumpMsA(260);
        const int stC1 = wA.stateAt(20, 16, 16);
        pcA.placeBlock();
        pumpMsA(260);
        const int stC2 = wA.stateAt(20, 16, 16);
        const bool creativeOkA = hitCA == QVector3D(20, 16, 16)
            && (stC1 == 2 || stC1 == 3)
            && (stC2 - stC1 == 2 || stC2 - stC1 == 3) // 0 起步两施不可达钳顶（≤3+3=6）
            && hbA.countAt(0) == 3                    // 创造不消耗
            && wA.blockAt(20, 16, 16) == BR::WheatCrop;
        if (!creativeOkA)
            qInfo().noquote() << "  [t1030a diag] creative hit" << hitCA << "st1" << stC1
                              << "st2" << stC2 << "cnt" << hbA.countAt(0);
        pcA.release();
        winA.deleteLater();
        ok = recOkA && growOkA && matureOkA && creativeOkA;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1030a bonemeal craft count + right-click growth chain (real player "
                             "path): one bone crafts 3 bone meal shapeless in both the 2x2 inventory "
                             "grid and a 3x3 table slot while two bones match nothing; survival "
                             "right-clicks on an immature wheat crop advance it exactly +2..+3 "
                             "stages per use (clamp step to stage 7 allowed) consuming exactly one "
                             "bone meal each time with the id preserved, maturing from stage 2 in "
                             "2-3 uses; applying to the mature crop is the registered no-effect "
                             "no-consume caliber (stage held, count held); in creative mode two "
                             "uses advance the crop identically while the stack stays untouched "
                             "(negative-round sensitive: playercontroller bonemeal branch "
                             "deactivation)"
                          << (ok ? QString()
                                  : QStringLiteral("diag rec=%1 grow=%2 mature=%3 creative=%4")
                                        .arg(recOkA).arg(growOkA).arg(matureOkA).arg(creativeOkA));
    });

    // ── P-t1030b 骨粉边界（右键非作物不消耗不响）+ 催熟链源码钉（R19.22 t1030）──
    //   (a) 边界（真 placeBlock 链）：持骨粉生存右键石头（非作物非生长目标）→ 不消耗 + 目标照旧。
    //       applyBonemeal 对非三类目标返 false → 不耗不挥（机制等价 MC 骨粉对非生长目标无效应）。
    //       瞄 +x 侧面进入（眼位 15.62 高于墩顶：瞄 y14.5 才保射线在 x=11 交越时 y≈14.69 已落回
    //       y14 带、恰命中 y14 石墩侧面；瞄 14.9 会先撞 y15 预置石的侧面）→ y15 预置石封死顶面
    //       放置格；即使阴性轮摘骨粉分流 fall-through，材料段物品 selectedBlock 归 Air（rig 构造后
    //       显式建模 QML 绑定，见 pcB.setSelectedBlock）→ 通用放置被 m_selectedBlock==Air 守卫
    //       （playercontroller.cpp placeBlock 末段）挡下 → 本腿对阴性轮恒绿（恰 a 红律）。
    //   (b) 源码钉（pinSet 剥注释，套件纪律勿裸 contains）：右键分流判据 + applyBonemeal 统一入口
    //       调用语句本体（playercontroller.cpp——阴性轮 false && 恒假化后 needle 仍字面在位）、
    //       applyBonemeal 已熟早退 + 推进语句本体（world.cpp）、推进带常量（world.h）、物品 id 存档
    //       契约 BoneId=0x217 / BonemealId=0x232（recipe.h；BonemealId 为 t447 时点材料段尾追加，
    //       后续物品仍只许尾追加不重排）、配方 pattern / 产物行（recipe.cpp）。
    runLegMulti({ "t1030b bonemeal edge + growth-chain source pins: right-clicking a stone with bone meal held cons"
        "umes nothing and leaves the target untouched (non-growth target no-effect; the top placement slo"
        "t is pre-stoned so the leg stays green even with the branch deactivated); source pins lock the b"
        "onemeal branch predicate and the applyBonemeal entry call in playercontroller, the mature-earlyo"
        "ut and the +2..3 advance statement in world.cpp, the advance-band constants in world.h, the save"
        "-contract item ids BoneId=0x217 and BonemealId=0x232 and the shapeless 1-bone-to-3-meal recipe r"
        "ows in recipe.cppdiag edge=%1 pins=%2" }, [&]() {
        bool ok = true;
        World wB;
        wB.setWidth(24);
        wB.setDepth(24);
        wB.setHeight(24);
        wB.setSeed(10302);
        for (int x = 2; x <= 21; ++x)
            for (int z = 8; z <= 18; ++z) {
                wB.setBlock(x, 13, z, BR::Stone, 0);
                for (int y = 14; y <= 22; ++y) wB.setBlock(x, y, z, BR::Air, 0);
            }
        // 石墩 y14（边界目标）+ y15 预置石（堵顶面放置格 → 通用放置路径也无格可落）。
        wB.setBlock(10, 14, 13, BR::Stone, 0);
        wB.setBlock(10, 15, 13, BR::Stone, 0);
        Hotbar hbB;
        PlayerController pcB; // t814 真消费端模式（同 t1030a）
        pcB.setWorld(&wB);
        pcB.setHotbar(&hbB);
        pcB.setSelectedBlock(BR::Air); // 同 t1030a：建模 QML 材料段→Air 接线（阴性轮 fall-through 兜底）
        QQuickWindow winB;
        pcB.setParentItem(winB.contentItem());
        pcB.grab();
        const auto aimB = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = feetX, ey = 14.0f + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pitch = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcB.release();
            pcB.grab();
            pcB.loadSavedState(feetX, 14.0f, feetZ, yaw, pitch, 2 /* Survival */);
            pcB.tick();
            return pcB.hitBlock();
        };
        const auto pumpMsB = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        hbB.setStack(0, RecipeRegistry::BonemealId, 4);
        hbB.setSelectedSlot(0);
        const QVector3D hitB = aimB(13.5f, 13.5f, 10.5f, 14.5f, 13.5f); // 瞄 y14.5：+x 侧面入（14.9 会先撞 y15 预置石）
        pcB.placeBlock();
        pumpMsB(260);
        const bool edgeOkB = hitB == QVector3D(10, 14, 13)
            && hbB.countAt(0) == 4 // 非作物不消耗
            && wB.blockAt(10, 14, 13) == BR::Stone
            && wB.blockAt(10, 15, 13) == BR::Stone; // 目标与放置格照旧
        if (!edgeOkB)
            qInfo().noquote() << "  [t1030b diag] edge hit" << hitB << "cnt" << hbB.countAt(0)
                              << "id14" << int(wB.blockAt(10, 14, 13))
                              << "id15" << int(wB.blockAt(10, 15, 13));
        pcB.release();
        winB.deleteLater();
        // (b) 源码钉（pinSet 剥注释；阴性轮摘 playercontroller 骨粉分流判据后各 needle 仍字面在位）。
        const QString exeDirB = QCoreApplication::applicationDirPath();
        const QString rootB = QDir(exeDirB + QStringLiteral("/..")).absolutePath();
        QStringList missB;
        missB << pinSet(rootB + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-bonemeal-branch", "heldItemId == RecipeRegistry::BonemealId"},
            {"cpp-bonemeal-entry", "m_world->applyBonemeal(m_hitBx, m_hitBy, m_hitBz)"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-bonemeal-mature-earlyout", "if (st >= BlockRegistry::WheatCropStageMax) return false;"},
            {"cpp-bonemeal-advance", "const int advance = kBonemealCropAdvanceMin"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/World/world.h"), {
            {"hdr-bonemeal-adv-min", "static constexpr int kBonemealCropAdvanceMin = 2;"},
            {"hdr-bonemeal-adv-max", "static constexpr int kBonemealCropAdvanceMax = 3;"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/Game/recipe.h"), {
            {"hdr-bone-id-contract", "BoneId       = 0x217;"},
            {"hdr-bonemeal-id-contract", "BonemealId        = 0x232;"},
        });
        missB << pinSet(rootB + QStringLiteral("/src/Game/recipe.cpp"), {
            {"cpp-bonemeal-pattern", "{ RecipeRegistry::BoneId, 0, 0, 0, 0, 0, 0, 0, 0 },"},
            {"cpp-bonemeal-output", "RecipeRegistry::BonemealId, 3, 1, \"bone_meal\" },"},
        });
        const bool pinsOkB = missB.isEmpty();
        if (!pinsOkB)
            qInfo().noquote() << "  [t1030b diag] pin miss:" << missB.join(QLatin1Char(','));
        ok = edgeOkB && pinsOkB;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1030b bonemeal edge + growth-chain source pins: right-clicking a "
                             "stone with bone meal held consumes nothing and leaves the target "
                             "untouched (non-growth target no-effect; the top placement slot is "
                             "pre-stoned so the leg stays green even with the branch deactivated); "
                             "source pins lock the bonemeal branch predicate and the applyBonemeal "
                             "entry call in playercontroller, the mature-earlyout and the +2..3 "
                             "advance statement in world.cpp, the advance-band constants in "
                             "world.h, the save-contract item ids BoneId=0x217 and "
                             "BonemealId=0x232 and the shapeless 1-bone-to-3-meal recipe rows in "
                             "recipe.cpp"
                          << (ok ? QString()
                                  : QStringLiteral("diag edge=%1 pins=%2").arg(edgeOkB).arg(pinsOkB));
    });

    // ── P-t1031a 狼驯服真链行为探针（R19.22 t1031；真输入链 + t1031 驯服概率缝确定性化）──
    //    全链主体系 t480/t831/t878/t986-988 遗产（骨头分流 / 爱心沿 / 项圈 MobModel collarVisible /
    //    传送跟随）；本单补驯服概率缝 + 野狼中立收口，探针钉行为状态面：
    //    (a1) 必败腿（缝 999 → 样本 0.999 ≥ 0.33）：野狼 + 骨头右键 press → 不驯 + 骨头恰耗 1 +
    //         无爱心沿（失败反馈登记简化为日志——MC 失败冒烟粒子不做，探针只钉状态面）；
    //    (a2) 必成腿（缝 0 → 样本 0.0 < 0.33）：同狼再 press → 驯服态位 + 爱心沿（inLoveAt，t878③
    //         心形 delegate 状态面）+ 骨头再耗 1；tick 5.5s > kTameHeartDuration 4s → 收心；
    //    (a3) 坐/站两向切换（空手右键；t878① 口径骨头对驯服狼 no-op、坐站只走空手分支）：press →
    //         坐（wolfSittingAt 真）→ press → 站（假）；空手切换零消耗；
    //    (a4) 参战腿（attackMob→setWolfTarget t480 接线，t242/t1015 attack-target 真链）：驯服狼 +
    //         静止僵尸（wander 冻结）→ 玩家 beginMining 命中僵尸（attackMob 唯一生产入口）→ 狼追击
    //         咬击（僵尸掉血超过玩家空手一击 + 狼-僵尸最小 XZ 距 ≤ kAttackRange+0.1）；
    //    (a5) 野狼中立腿（t1031 收口面）：未驯服狼 + 生存玩家 3 格（targetable=true）+ wander 冻结
    //         钉位 6s → 零 mobAttackedPlayer + 狼 XZ 逐位钉在生成格（旧敌对分支 3 格 < 旧侦测 12 必
    //         追咬 → 本腿对收口敏感；冻结只关 wander，旧 chase 不受影响 = 敏感性保留）。
    //    缺省零调用与接线语句本体源钉在 P-t1031b（阴性轮摘驯服分流时 b 须恒绿 = 恰 a 红律）。
    runLegMulti({ "t1031a wolf taming real-chain behavior: seam-pinned MUST-FAIL bone attempt consumes exactly one "
        "bone and stays wild with no heart, seam-pinned MUST-TAME attempt flips wolfTamed with the heart "
        "edge (decayed after the 4s window), the empty-hand right-click toggles sit then stand both ways "
        "with zero consumption (bone is a no-op on a tamed wolf, t878 caliber), the player's real attack "
        "chain hands the struck mob to the tamed wolf (assist bite lands past the player's own hit, min w"
        "olf-zombie gap within the bite band), and a wild wolf near a survival player stays put with ZERO"
        " mobAttackedPlayer over 6s (t1031 neutral caliber; wander frozen for determinism, the old hostil"
        "e chase ignored the freeze so the leg is regression-sensitive)diag a=%1 assist=%2 neutral=%3" }, [&]() {
        auto flatRig1031 = [](World &w) {
            w.setWidth(44); w.setDepth(44); w.setHeight(96); w.setSeed(31);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) w.setBlock(x, 84, z, BR::Stone, 0);
        };
        auto pump1031 = [](int ms) { // placeBlock 200ms 冷却（墙钟；t949/t1030b 同式）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        auto rightPress1031 = [](QQuickWindow &win) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(64.0, 64.0), QPointF(64.0, 64.0),
                              Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            return QCoreApplication::sendEvent(&win, &press);
        };
        // 瞄 (mx,mz) 格 mob 中心：眼 (mx+0.5, 86.62, mz+3) → 距 2.74 < kReach 5（t949 aimRig 同式）。
        //   itemId>0 时置持物槽；空手传 0（驯服坐站 / 空拳攻击路径）。
        auto aimMob1031 = [](PlayerController &pc, World &w, EntityManager &em, Hotbar &hb,
                             int mx, int mz, int itemId, int itemCount) {
            pc.setWorld(&w);
            pc.setEntityManager(&em);
            pc.setHotbar(&hb);
            pc.setSelectedBlock(BR::Air); // 材料段/空手 selectedBlock 归 Air 建模（t1030 教训：阴性轮兜底）
            if (itemId > 0) hb.setStack(0, itemId, itemCount, 0);
            hb.setSelectedSlot(0);
            const QVector3D eye(float(mx) + 0.5f, 86.62f, float(mz) + 3.0f);
            const QVector3D dir = (QVector3D(float(mx) + 0.5f, 85.5f, float(mz) + 0.5f) - eye).normalized();
            pc.loadSavedState(eye.x(), 85.0f, eye.z(),
                              qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                              qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
        };
        QQuickWindow win1031;

        // (a1)(a2)(a3) 同狼递进 rig。
        bool okA = true;
        int wolfA = -1;
        {
            bool failedTame = false, tamed = false, heartGone = false;
            bool sitOn = false, sitOff = false, bonesHeld = false;
            World wa;
            flatRig1031(wa);
            EntityManager ema;
            Hotbar hba;
            PlayerController pca;
            pca.setParentItem(win1031.contentItem());
            pca.grab();
            wolfA = ema.spawnMobTyped(20, 85, 22, EntityManager::MobWolf,
                                      QStringLiteral("#c8ccd4"), 10);
            const int wolf = wolfA;
            if (wolf >= 0) {
                // (a1) 必败：缝 999 → 骨头恰耗 1、不驯、无爱心沿。
                ema.setTameRollOverride(999);
                aimMob1031(pca, wa, ema, hba, 20, 22, RecipeRegistry::BoneId, 64);
                rightPress1031(win1031);
                failedTame = !ema.wolfTamedAt(wolf) && hba.countAt(0) == 63 && !ema.inLoveAt(wolf);
                // (a2) 必成：缝 0 → 驯服 + 爱心沿 + 再耗 1；5.5s 后收心（kTameHeartDuration 4s）。
                ema.setTameRollOverride(0);
                pump1031(210);
                rightPress1031(win1031);
                tamed = ema.wolfTamedAt(wolf) && ema.inLoveAt(wolf) && hba.countAt(0) == 62;
                for (int t = 0; t < 344; ++t) // 5.5s > 4s（t831 同式衰减窗）
                    ema.tick(0.016f, &wa, QVector3D(22.5f, 86.0f, 22.5f), 0.3f, 1.8f, true);
                heartGone = !ema.inLoveAt(wolf);
                ema.setTameRollOverride(-1); // 缝复位（后续腿走生产缺省路径）
                // (a3) 坐/站两向（空手右键；骨头已耗尽无关——分支判据 heldItemId==0）。
                hba.setStack(0, 0, 0, 0); // 清持物（空手）
                pump1031(210);
                rightPress1031(win1031);
                sitOn = ema.wolfSittingAt(wolf);
                pump1031(210);
                rightPress1031(win1031);
                sitOff = !ema.wolfSittingAt(wolf);
                bonesHeld = hba.countAt(0) == 0; // 空手切换零消耗（槽已空，不被误扣）
            } else {
                failedTame = tamed = heartGone = sitOn = sitOff = false;
            }
            pca.release();
            okA = wolfA >= 0 && failedTame && tamed && heartGone && sitOn && sitOff && bonesHeld;
            if (!okA)
                qInfo().noquote() << "  [t1031a diag] fail=" << failedTame << "tamed=" << tamed
                                  << "heartGone=" << heartGone << "sitOn=" << sitOn
                                  << "sitOff=" << sitOff << "bonesHeld=" << bonesHeld;
        }

        // (a4) 参战腿（真攻击链 → t480 接线；驯服走缝直调——阴性轮摘骨头分流本腿恒绿）。
        bool assisted = false;
        {
            World wc;
            flatRig1031(wc);
            EntityManager emc;
            emc.setWanderFrozen(true); // 静止僵尸（狼防御/跟随不走 aiWander，冻结不波及狼追击）
            Hotbar hbc;
            PlayerController pcc;
            pcc.setParentItem(win1031.contentItem());
            pcc.grab();
            const int wolf = emc.spawnMobTyped(16, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            emc.setTameRollOverride(0);
            const bool pre = wolf >= 0 && emc.tameWolf(wolf);
            emc.setTameRollOverride(-1);
            const int zombie = emc.spawnMobTyped(20, 85, 22, EntityManager::MobShambler,
                                                 QStringLiteral("#4a6a3a"), 20);
            aimMob1031(pcc, wc, emc, hbc, 20, 22, 0, 0); // 空手瞄僵尸（眼距 2.74；狼 (16.5,22.5) 离射线）
            pcc.beginMining(); // attackMob 唯一生产入口（t866/t242）→ damageEntity(1) + setWolfTarget
            const int hpAfterPlayer = emc.healthAt(zombie);
            float minDz = 1e9f;
            for (int t = 0; t < 1250 && !assisted; ++t) { // 20s 事件帽（t988 同式）
                emc.tick(0.016f, &wc, QVector3D(20.5f, 86.0f, 25.5f), 0.3f, 1.8f, false);
                const QVector3D pw = emc.posAt(wolf), pz = emc.posAt(zombie);
                minDz = std::min(minDz, QVector3D(pw.x() - pz.x(), 0.0f, pw.z() - pz.z()).length());
                if (emc.healthAt(zombie) < hpAfterPlayer && minDz <= 1.7f)
                    assisted = pre && hpAfterPlayer < 20; // 玩家空手一击先落地，狼咬再掉血 = 参战
            }
            pcc.release();
            if (!assisted)
                qInfo().noquote() << "  [t1031a diag] pre=" << pre << "hpPlayer=" << hpAfterPlayer
                                  << "hpEnd=" << emc.healthAt(zombie) << "minDz=" << minDz;
        }

        // (a5) 野狼中立腿（t1031 收口面）。
        bool neutral = false;
        {
            World wd;
            flatRig1031(wd);
            EntityManager emd;
            emd.setWanderFrozen(true); // 钉位（游荡噪声归零；旧敌对 chase 不受冻结影响 = 敏感性保留）
            const int wolf = emd.spawnMobTyped(22, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            int bites = 0;
            QObject::connect(&emd, &EntityManager::mobAttackedPlayer,
                             [&bites](int, int, float, float) { ++bites; });
            for (int t = 0; t < 375; ++t) // 6s（旧口径咬击冷却 1s → ≥5 口签名，充分可辨）
                emd.tick(0.016f, &wd, QVector3D(19.5f, 86.0f, 22.5f), 0.3f, 1.8f, true, false);
            const QVector3D pw = emd.posAt(wolf);
            neutral = wolf >= 0 && bites == 0 && pw.x() == 22.5f && pw.z() == 22.5f
                   && !emd.wolfTamedAt(wolf);
            if (!neutral)
                qInfo().noquote() << "  [t1031a diag] bites=" << bites << "pos=" << pw;
        }

        bool ok = okA && assisted && neutral;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1031a wolf taming real-chain behavior: seam-pinned MUST-FAIL bone "
                             "attempt consumes exactly one bone and stays wild with no heart, "
                             "seam-pinned MUST-TAME attempt flips wolfTamed with the heart edge "
                             "(decayed after the 4s window), the empty-hand right-click toggles "
                             "sit then stand both ways with zero consumption (bone is a no-op on "
                             "a tamed wolf, t878 caliber), the player's real attack chain hands "
                             "the struck mob to the tamed wolf (assist bite lands past the "
                             "player's own hit, min wolf-zombie gap within the bite band), and "
                             "a wild wolf near a survival player stays put with ZERO "
                             "mobAttackedPlayer over 6s (t1031 neutral caliber; wander frozen "
                             "for determinism, the old hostile chase ignored the freeze so the "
                             "leg is regression-sensitive)"
                          << (ok ? QString()
                                 : QStringLiteral("diag a=%1 assist=%2 neutral=%3")
                                       .arg(okA).arg(assisted).arg(neutral));
    });

    // ── P-t1031b 狼驯服边界 + 接线源钉（R19.22 t1031；pinSet 剥注释，套件纪律勿裸 contains）──
    //    (b1) 骨头右键非狼不耗（真 placeBlock 链）：持骨瞄猪 press → 骨头 64 不变（骨头分支对非狼
    //         不消耗不放置；selectedBlock 显式建模 Air = 阴性轮 fall-through 兜底，t1030b 同式）；
    //    (b2) 空手右键野狼不驯不坐：press → wolfTamedAt/wolfSittingAt 恒假（驯服只走骨头、坐站只走
    //         驯服后空手；野狼右键无反应，机制等价 MC 只有驯服狼可命令）；
    //    (b3) 缝缺省零调用（源面）：setter / 驯服样本接管语句 / 头文件声明 / 缺省 -1 成员 pinSet 在位 +
    //         playercontroller.cpp 全文不含 setTameRollOverride（生产路径零调用——阴性轮不触缝，本组恒绿）；
    //    (b4) 接线语句本体（阴性轮只摘**驯服尝试段**，以下各钉字面在位 = 恰 P-t1031a 红律）：骨头分支
    //         判据、空手坐站切换语句、attackMob 参战接线语句、aiWolf 中立门（t1042 重钉为分支头——
    //         反击只走受击沿挑逗，不见人就咬）。
    runLegMulti({ "t1031b wolf taming boundaries + wiring source pins: a bone right-click on a non-wolf (pig) consu"
        "mes nothing through the real placeBlock chain, an empty-hand right-click on a wild wolf neither "
        "tames nor sits it (taming is bone-only, commands are tame-only, MC caliber), the tame-roll seam "
        "is default-off with zero production callers (setter + sample-takeover + header decl + default -1"
        " pinned, playercontroller contains no reference), and the wiring statements are comment-immune p"
        "inned: the bone branch predicate, the empty-hand sit-toggle call, the attackMob assist wire and "
        "the aiWolf wild-wolf branch gate (t1042 re-pin: retaliation fires only via the hit-path provocat"
        "ion, never sight-based)diag pig=%1 wild=%2 pins=%3" }, [&]() {
        bool ok = true;
        // (b1) 骨头 + 猪（非狼）→ 不消耗。
        bool pigUntouched = false;
        {
            World wb;
            auto flatRigB = [&]() {
                wb.setWidth(44); wb.setDepth(44); wb.setHeight(96); wb.setSeed(32);
                for (int x = 2; x < 42; ++x)
                    for (int z = 2; z < 42; ++z) wb.setBlock(x, 84, z, BR::Stone, 0);
            };
            flatRigB();
            EntityManager emb;
            Hotbar hbb;
            PlayerController pcb;
            pcb.setSelectedBlock(BR::Air); // 材料段物品 selectedBlock 归 Air 建模（阴性轮兜底，t1030b 同式）
            QQuickWindow winB;
            pcb.setParentItem(winB.contentItem());
            const auto aimB = [&](int mx, int mz, int itemId, int itemCount) {
                pcb.setWorld(&wb);
                pcb.setEntityManager(&emb);
                pcb.setHotbar(&hbb);
                if (itemId > 0) hbb.setStack(0, itemId, itemCount, 0);
                hbb.setSelectedSlot(0);
                const QVector3D eye(float(mx) + 0.5f, 86.62f, float(mz) + 3.0f);
                const QVector3D dir = (QVector3D(float(mx) + 0.5f, 85.5f, float(mz) + 0.5f) - eye).normalized();
                pcb.loadSavedState(eye.x(), 85.0f, eye.z(),
                                   qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                                   qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            };
            pcb.grab();
            const int pig = emb.spawnMobTyped(20, 85, 22, EntityManager::MobPig,
                                              QStringLiteral("#e8a0a0"), 10);
            aimB(20, 22, RecipeRegistry::BoneId, 64);
            QMouseEvent pressB(QEvent::MouseButtonPress, QPointF(64.0, 64.0), QPointF(64.0, 64.0),
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&winB, &pressB);
            pigUntouched = pig >= 0 && hbb.countAt(0) == 64 && !emb.wolfTamedAt(pig);
            pcb.release();
            winB.deleteLater();
        }
        // (b2) 空手 + 野狼 → 不驯不坐。
        bool wildUntouched = false;
        {
            World wc;
            wc.setWidth(44); wc.setDepth(44); wc.setHeight(96); wc.setSeed(33);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wc.setBlock(x, 84, z, BR::Stone, 0);
            EntityManager emc;
            Hotbar hbc;
            PlayerController pcc;
            pcc.setSelectedBlock(BR::Air);
            QQuickWindow winC;
            pcc.setParentItem(winC.contentItem());
            pcc.setWorld(&wc);
            pcc.setEntityManager(&emc);
            pcc.setHotbar(&hbc); // 槽保持空（heldItemId==0）
            hbc.setSelectedSlot(0);
            const QVector3D eye(20.5f, 86.62f, 25.0f);
            const QVector3D dir = (QVector3D(20.5f, 85.5f, 22.5f) - eye).normalized();
            pcc.loadSavedState(eye.x(), 85.0f, eye.z(),
                               qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                               qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            pcc.grab();
            const int wolf = emc.spawnMobTyped(20, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            QMouseEvent pressC(QEvent::MouseButtonPress, QPointF(64.0, 64.0), QPointF(64.0, 64.0),
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&winC, &pressC);
            wildUntouched = wolf >= 0 && !emc.wolfTamedAt(wolf) && !emc.wolfSittingAt(wolf);
            pcc.release();
            winC.deleteLater();
        }
        // (b3)(b4) 源钉（pinSet 剥注释）。
        const QString exeDirB1031 = QCoreApplication::applicationDirPath();
        const QString rootB1031 = QDir(exeDirB1031 + QStringLiteral("/..")).absolutePath();
        QStringList missB1031;
        missB1031 << pinSet(rootB1031 + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-tame-roll-setter", "void EntityManager::setTameRollOverride(int roll)"},
            {"cpp-tame-roll-consume", "const double roll = m_tameRollOverride >= 0"},
            // t1042 重钉：aiWolf 未驯服分支门（旧 Q_UNUSED(playerTargetable) 随 t1042 反击面退役——
            //   playerTargetable 现由野狼反击锁定门消费，本钉改锚分支头，中立收口语义不变）。
            {"cpp-wolf-neutral-gate", "if (!e.wolfTamed) {"},
        });
        missB1031 << pinSet(rootB1031 + QStringLiteral("/src/Entities/entitymanager.h"), {
            {"hdr-tame-roll-decl", "Q_INVOKABLE void setTameRollOverride(int roll);"},
            {"hdr-tame-roll-default", "int m_tameRollOverride = -1;"},
        });
        missB1031 << pinSet(rootB1031 + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-bone-branch-pred", "heldItemId == RecipeRegistry::BoneId"},
            {"cpp-wolf-sit-toggle", "m_entityManager->toggleWolfSit(mobIdx);"},
            {"cpp-wolf-assist-wire", "m_entityManager->setWolfTarget(entityIndex);"},
        });
        QFile pcB1031(rootB1031 + QStringLiteral("/src/Game/playercontroller.cpp"));
        const bool noGameCaller = !pcB1031.open(QIODevice::ReadOnly)
            || !QString::fromUtf8(pcB1031.readAll()).contains(QStringLiteral("setTameRollOverride"));
        const bool pinsOkB1031 = missB1031.isEmpty() && noGameCaller;
        if (!pinsOkB1031)
            qInfo().noquote() << "  [t1031b diag] pig=" << pigUntouched << "wild=" << wildUntouched
                              << "noCaller=" << noGameCaller
                              << "miss=" << missB1031.join(QLatin1Char(','));
        ok = pigUntouched && wildUntouched && pinsOkB1031;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1031b wolf taming boundaries + wiring source pins: a bone "
                             "right-click on a non-wolf (pig) consumes nothing through the real "
                             "placeBlock chain, an empty-hand right-click on a wild wolf neither "
                             "tames nor sits it (taming is bone-only, commands are tame-only, MC "
                             "caliber), the tame-roll seam is default-off with zero production "
                             "callers (setter + sample-takeover + header decl + default -1 pinned, "
                             "playercontroller contains no reference), and the wiring statements "
                             "are comment-immune pinned: the bone branch predicate, the empty-hand "
                             "sit-toggle call, the attackMob assist wire and the aiWolf wild-wolf "
                             "branch gate (t1042 re-pin: retaliation fires only via the hit-path "
                             "provocation, never sight-based)"
                          << (ok ? QString()
                                 : QStringLiteral("diag pig=%1 wild=%2 pins=%3")
                                       .arg(pigUntouched).arg(wildUntouched).arg(pinsOkB1031));
    });

    // ── P-t1034a 门 sneakPlace 旁路（review0909 #2 存量登记项清偿，t1034）──
    //    两向：(1) 潜行持方块右键木门 = 放置落命中面邻格（门不开：两半 state bit2 保持 0、doorToggled
    //    零发——use 被旁路）；(2) 非潜行持方块右键 = 开合照常（use 优先于放置吃掉右键：两半同翻
    //    bit2=4、doorToggled 恰 1 次、邻格无放置）。阴性轮敏感（单构建三摘一轮）：摘门分支
    //    sneakPlaceBlock 门（t1050 起判据名）→ 潜行腿红（潜行右键仍开门 + 门面无放置）+ cpp-door-sneak-gate 钉红；
    //    非潜行对照腿不受门影响保绿。
    runLegMulti({ "t1034a door sneakPlace bypass: sneaking with a held block and right-clicking a wooden door place"
        "s the held block on the clicked face's neighbor cell while the door stays shut on both halves wi"
        "th zero doorToggled emissions (use bypassed, MC sneak-use caliber, review0909 #2 legacy cleared)"
        "; without sneak the right-click still opens the door (both halves flip bit2, exactly one doorTog"
        "gled(true)) and consumes the click so nothing is placed (negative-round sensitive: door-branch s"
        "neakPlace gate removal)diag sneak=%1 use=%2 pins=%3 toggles=%4" }, [&]() {
        World wT34a;
        wT34a.setWidth(48); wT34a.setDepth(48); wT34a.setHeight(96); wT34a.setSeed(10341);
        Hotbar hbT34a;
        PlayerController pcT34a;
        pcT34a.setWorld(&wT34a);
        pcT34a.setHotbar(&hbT34a);
        QQuickWindow winT34a;
        pcT34a.setParentItem(winT34a.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空；门 A（z=16，潜行腿）与门 B（z=20，非潜行对照腿）各两格
        //（下格 state0 + 上格 state8，合态 bit2=0；state bit[1:0]=0 朝 +X → 门板贴 +X 边）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT34a.setBlock(x, y, z, BR::Air, 0);
                wT34a.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT34a.setBlock(12, 15, 16, BR::WoodDoor, quint8(0)); // 门 A 下格（合）
        wT34a.setBlock(12, 16, 16, BR::WoodDoor, quint8(8)); // 门 A 上格
        wT34a.setBlock(12, 15, 20, BR::WoodDoor, quint8(0)); // 门 B 下格（合）
        wT34a.setBlock(12, 16, 20, BR::WoodDoor, quint8(8)); // 门 B 上格
        int togglesT34a = 0;
        bool lastOpenT34a = false;
        const QMetaObject::Connection cTglA = QObject::connect(
            &pcT34a, &PlayerController::doorToggled, &pcT34a,
            [&](bool open) { ++togglesT34a; lastOpenT34a = open; });
        // 瞄准帮手（t1028b 同款：re-grab 光标归零 → loadSavedState 定向 → tick 刷射线）；玩家站
        //   地台 y=15（眼 16.62），自 +X 侧瞄门格 +X 面（state0 门板贴 +X 边 → 命中即门板面）。
        const auto aimT34a = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT34a.release();
            pcT34a.grab();
            pcT34a.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT34a.tick();
            return pcT34a.hitBlock();
        };
        const auto pumpT34a = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟，t1028b 同款）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 潜行腿：潜行 + 持木板右键门 A 的 +X 面 → 木板落邻格 (13,15,16)；门 A 两半合态不动 +
        //     doorToggled 零发（潜行旁路 useBlock，t1034 门）。
        const QVector3D hitSneakA = aimT34a(16.5f, 12.95f, 15.5f, 16.5f);
        pcT34a.setKey(Qt::Key_Shift, true);        // 潜行（sneakPlace = m_keys 原始键态，t523 口径）
        pcT34a.setSelectedBlock(int(BR::Planks));  // 持方块（无 QML 引擎，C++ 直喂 selectedBlock，t1028b 同款）
        pcT34a.placeBlock();
        pumpT34a(260);
        pcT34a.setKey(Qt::Key_Shift, false);
        const bool okSneakA = hitSneakA == QVector3D(12, 15, 16)
            && wT34a.blockAt(13, 15, 16) == BR::Planks   // 放置成功（命中面邻格）
            && wT34a.blockAt(12, 15, 16) == BR::WoodDoor  // 门本体原样
            && (wT34a.stateAt(12, 15, 16) & 4) == 0       // 下半未开
            && wT34a.blockAt(12, 16, 16) == BR::WoodDoor
            && (wT34a.stateAt(12, 16, 16) & 4) == 0       // 上半联动位未翻
            && togglesT34a == 0;                          // 开合零发（use 未发生）
        // (2) 非潜行对照腿：仍持木板（不潜行）右键门 B 的 +X 面 → 开合照常（use 分支优先于放置吃掉
        //     右键）：两半同翻 bit2=4、doorToggled 恰 1 次携 open=true、邻格 (13,15,20) 无放置。
        pumpT34a(260);
        const QVector3D hitUseA = aimT34a(20.5f, 12.95f, 15.5f, 20.5f);
        pcT34a.placeBlock(); // 不潜行（Shift 已松）→ use 照常
        pumpT34a(260);
        const bool okUseA = hitUseA == QVector3D(12, 15, 20)
            && togglesT34a == 1 && lastOpenT34a           // 开合恰一次（门两格同翻只发一次）
            && (wT34a.stateAt(12, 15, 20) & 4) == 4       // 下半开
            && (wT34a.stateAt(12, 16, 20) & 4) == 4       // 上半联动开
            && wT34a.blockAt(13, 15, 20) == BR::Air;      // 无放置（右键被 use 消费）
        // (3) 源钉（pinSet 剥注释；阴性轮摘门即红）。
        const QString rootT34a = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT34a = pinSet(rootT34a + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-door-sneak-gate", "if (!sneakPlaceBlock && BlockRegistry::isDoor(hitId) && hitId != BlockRegistry::IronDoor) {"},
        });
        if (!missT34a.isEmpty())
            qInfo().noquote() << "  [t1034a diag] pins" << missT34a.join(QLatin1Char(','));
        QObject::disconnect(cTglA);
        pcT34a.release();
        winT34a.deleteLater();
        const bool okA = okSneakA && okUseA && missT34a.isEmpty();
        if (!okA)
            qInfo().noquote() << "  [t1034a diag] hitSneak" << hitSneakA << "hitUse" << hitUseA;
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1034a door sneakPlace bypass: sneaking with a held block and "
                             "right-clicking a wooden door places the held block on the clicked "
                             "face's neighbor cell while the door stays shut on both halves with "
                             "zero doorToggled emissions (use bypassed, MC sneak-use caliber, "
                             "review0909 #2 legacy cleared); without sneak the right-click still "
                             "opens the door (both halves flip bit2, exactly one doorToggled(true)) "
                             "and consumes the click so nothing is placed (negative-round "
                             "sensitive: door-branch sneakPlace gate removal)"
                          << (okA ? QString()
                                  : QStringLiteral("diag sneak=%1 use=%2 pins=%3 toggles=%4")
                                        .arg(okSneakA).arg(okUseA).arg(missT34a.isEmpty())
                                        .arg(togglesT34a));
    });

    // ── P-t1034b 床 sneakPlace 旁路（review0909 #2 存量登记项清偿，t1034）──
    //    两向：(1) 非潜行空手右键床（白天非雷暴）= 入睡链照常触达 → 拒睡文案「只能在夜晚或雷暴中
    //    睡觉」（trySleepAt 夜/雷暴窗口门的白天分支——契约口径：拒绝文案不算放置失败对照）；
    //    (2) 潜行持方块右键床 = 放置落命中面邻格（睡链整链不触达：sleepRefused 计数不增长、不入睡、
    //    床两半原样——MC：潜行右键床=放置，不睡）。门加在 placeBlock 床分支头（先于 trySleepAt 调用），
    //    夜门/雷暴门/怪物门序一律不被触达。阴性轮敏感（单构建三摘一轮）：摘床分支 sneakPlaceBlock 门
    //    （t1050 起判据名）→
    //    潜行腿红（潜行右键仍走拒睡链：refused 计 +1 且无放置）+ cpp-bed-sneak-gate 钉红；非潜行
    //    拒睡对照腿不受门影响保绿。
    runLegMulti({ "t1034b bed sneakPlace bypass: a plain empty-hand right-click on the bed by day still reaches the"
        " sleep chain and refuses with the exact night-or-thunder message (sleep window intact, MC calibe"
        "r); sneaking with a held block and right-clicking the bed instead places the block on the clicke"
        "d face's neighbor cell with the whole sleep chain untouched (refusal count frozen, not sleeping,"
        " both bed halves pristine - MC: sneak-use on a bed places, never sleeps; review0909 #2 legacy cl"
        "eared; negative-round sensitive: bed-branch sneakPlace gate removal re-routes the sneak click in"
        "to the day refusal)diag use=%1 sneak=%2 pins=%3 refused=%4" }, [&]() {
        World wT34b;
        wT34b.setWidth(48); wT34b.setDepth(48); wT34b.setHeight(96); wT34b.setSeed(10342);
        wT34b.setWeatherState(0); // Clear（白天拒睡对照的确定性前提；review0909 #1 口径）
        Hotbar hbT34b;
        WorldClock clockT34b;
        clockT34b.setPhase(0.2f); // 白天（isNight=false；setPhase 特权指令，t1024a 同款）
        PlayerController pcT34b;
        pcT34b.setWorld(&wT34b);
        pcT34b.setWorldClock(&clockT34b);
        pcT34b.setHotbar(&hbT34b);
        QQuickWindow winT34b;
        pcT34b.setParentItem(winT34b.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空；床 foot (12,15,16) state0 + head (11,15,16) state8
        //（D=+X：head 在 foot -X 侧，t1024a 同款摆位）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 20; ++z) {
                for (int y = 15; y <= 20; ++y) wT34b.setBlock(x, y, z, BR::Air, 0);
                wT34b.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT34b.setBlock(12, 15, 16, BR::BedWhite, quint8(0)); // foot
        wT34b.setBlock(11, 15, 16, BR::BedWhite, quint8(8)); // head
        int refusedT34b = 0;
        QString lastRefuseT34b;
        const QMetaObject::Connection cRefB = QObject::connect(
            &pcT34b, &PlayerController::sleepRefused, &pcT34b,
            [&refusedT34b, &lastRefuseT34b](const QString &r) { ++refusedT34b; lastRefuseT34b = r; });
        const auto aimT34b = [&](float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = 16.5f;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT34b.release();
            pcT34b.grab();
            pcT34b.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT34b.tick();
            return pcT34b.hitBlock();
        };
        const auto pumpT34b = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 非潜行对照腿：空手不潜行右键床 foot 的 +X 侧面（aim y 15.15 在床垫低盒 y[0,~0.31] 带内
        //     —— 首跑教训：aim y 15.5 掠过低盒侧沿命中顶面 +Y，放置目标格变床顶格）→ 入睡链照常触达：
        //     拒睡文案「只能在夜晚或雷暴中睡觉」恰 1 次 + 不入睡 + 邻格无放置（use 语义照常，t388 链回归）。
        const QVector3D hitUseB = aimT34b(12.95f, 15.15f, 16.5f);
        pcT34b.setSelectedBlock(int(BR::Air)); // 空手（睡是「使用」语义，与手持何物无关）
        pcT34b.placeBlock();
        pumpT34b(260);
        const bool okUseB = hitUseB == QVector3D(12, 15, 16)
            && refusedT34b == 1
            && lastRefuseT34b == QStringLiteral("只能在夜晚或雷暴中睡觉")
            && !pcT34b.sleeping()
            && wT34b.blockAt(13, 15, 16) == BR::Air; // 无放置（右键被睡链消费）
        // (2) 潜行腿：潜行 + 持木板右键床 foot 的 +X 侧面（同 (1) 带内 aim）→ 木板落邻格 (13,15,16)；
        //     睡链整链不触达：refused 计数保持 1（潜行这一下零新增拒睡）+ 不入睡 + 床两半 id/state 原样。
        const QVector3D hitSneakB = aimT34b(12.95f, 15.15f, 16.5f);
        pcT34b.setKey(Qt::Key_Shift, true);        // 潜行（sneakPlace = m_keys 原始键态，t523 口径）
        pcT34b.setSelectedBlock(int(BR::Planks));  // 持方块（C++ 直喂 selectedBlock，t1028b 同款）
        pcT34b.placeBlock();
        pumpT34b(260);
        pcT34b.setKey(Qt::Key_Shift, false);
        const bool okSneakB = hitSneakB == QVector3D(12, 15, 16)
            && refusedT34b == 1                            // 睡链零触达（潜行这一下不拒睡不入睡）
            && !pcT34b.sleeping()
            && wT34b.blockAt(13, 15, 16) == BR::Planks     // 放置成功（命中面邻格）
            && wT34b.blockAt(12, 15, 16) == BR::BedWhite && wT34b.stateAt(12, 15, 16) == 0 // foot 原样
            && wT34b.blockAt(11, 15, 16) == BR::BedWhite && wT34b.stateAt(11, 15, 16) == 8; // head 原样
        // (3) 源钉（pinSet 剥注释；阴性轮摘门即红）。
        const QString rootT34b = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT34b = pinSet(rootT34b + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-bed-sneak-gate", "if (!sneakPlaceBlock && BlockRegistry::isBed(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {"},
        });
        if (!missT34b.isEmpty())
            qInfo().noquote() << "  [t1034b diag] pins" << missT34b.join(QLatin1Char(','));
        QObject::disconnect(cRefB);
        pcT34b.release();
        winT34b.deleteLater();
        const bool okB = okUseB && okSneakB && missT34b.isEmpty();
        if (!okB)
            qInfo().noquote() << "  [t1034b diag] hitUse" << hitUseB << "hitSneak" << hitSneakB
                              << "refused" << refusedT34b << "msg" << lastRefuseT34b;
        if (!okB) ++totalFail;
        qInfo().noquote() << (okB ? "PASS" : "FAIL")
                          << "| t1034b bed sneakPlace bypass: a plain empty-hand right-click on the "
                             "bed by day still reaches the sleep chain and refuses with the exact "
                             "night-or-thunder message (sleep window intact, MC caliber); "
                             "sneaking with a held block and right-clicking the bed instead places "
                             "the block on the clicked face's neighbor cell with the whole sleep "
                             "chain untouched (refusal count frozen, not sleeping, both bed halves "
                             "pristine - MC: sneak-use on a bed places, never sleeps; review0909 #2 "
                             "legacy cleared; negative-round sensitive: bed-branch sneakPlace gate "
                             "removal re-routes the sneak click into the day refusal)"
                          << (okB ? QString()
                                  : QStringLiteral("diag use=%1 sneak=%2 pins=%3 refused=%4")
                                        .arg(okUseB).arg(okSneakB).arg(missT34b.isEmpty())
                                        .arg(refusedT34b));
    });

    // ── P-t1034c 活板门 sneakPlace 旁路（review0909 #2 存量登记项清偿，t1034）──
    //    两向：(1) 潜行持方块右键合态活板门 = 放置落命中面邻格（板不翻：state bit0 保持 0、
    //    doorToggled 零发——use 被旁路）；(2) 非潜行持方块右键 = 翻板照常（bit0→1、doorToggled
    //    恰 1 次、邻格无放置）。阴性轮敏感（单构建三摘一轮）：摘活板门分支 sneakPlaceBlock 门（t1050 起判据名）→ 潜行腿红
    //    （潜行右键仍翻板 + 板面无放置）+ cpp-trapdoor-sneak-gate 钉红；非潜行对照腿不受门影响保绿。
    runLegMulti({ "t1034c trapdoor sneakPlace bypass: sneaking with a held block and right-clicking a closed trapdo"
        "or places the held block on the clicked face's neighbor cell while the trapdoor stays closed (st"
        "ate bit0 untouched, zero doorToggled emissions - use bypassed, MC sneak-use caliber, review0909 "
        "#2 legacy cleared); without sneak the right-click still flips the trapdoor open (bit0 set, exact"
        "ly one doorToggled(true)) and consumes the click so nothing is placed (negative-round sensitive:"
        " trapdoor-branch sneakPlace gate removal)diag sneak=%1 use=%2 pins=%3 toggles=%4" }, [&]() {
        World wT34c;
        wT34c.setWidth(48); wT34c.setDepth(48); wT34c.setHeight(96); wT34c.setSeed(10343);
        Hotbar hbT34c;
        PlayerController pcT34c;
        pcT34c.setWorld(&wT34c);
        pcT34c.setHotbar(&hbT34c);
        QQuickWindow winT34c;
        pcT34c.setParentItem(winT34c.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空；活板门 A（z=16，潜行腿）与 B（z=20，非潜行对照腿）
        // 各一格合态（state0：贴地水平薄板 y 0..3/16）。自 +X 侧瞄薄板 +X 面（aim y 在薄板带内）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT34c.setBlock(x, y, z, BR::Air, 0);
                wT34c.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT34c.setBlock(12, 15, 16, BR::WoodTrapdoor, quint8(0)); // A（合）
        wT34c.setBlock(12, 15, 20, BR::WoodTrapdoor, quint8(0)); // B（合）
        int togglesT34c = 0;
        bool lastOpenT34c = false;
        const QMetaObject::Connection cTglC = QObject::connect(
            &pcT34c, &PlayerController::doorToggled, &pcT34c,
            [&](bool open) { ++togglesT34c; lastOpenT34c = open; });
        const auto aimT34c = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT34c.release();
            pcT34c.grab();
            pcT34c.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT34c.tick();
            return pcT34c.hitBlock();
        };
        const auto pumpT34c = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 潜行腿：潜行 + 持木板右键活板门 A 的 +X 面（薄板带内）→ 木板落邻格 (13,15,16)；
        //     板合态不动（bit0 保持 0）+ doorToggled 零发（潜行旁路 useBlock，t1034 门）。
        const QVector3D hitSneakC = aimT34c(16.5f, 12.95f, 15.10f, 16.5f);
        pcT34c.setKey(Qt::Key_Shift, true);        // 潜行（sneakPlace = m_keys 原始键态，t523 口径）
        pcT34c.setSelectedBlock(int(BR::Planks));  // 持方块（C++ 直喂 selectedBlock，t1028b 同款）
        pcT34c.placeBlock();
        pumpT34c(260);
        pcT34c.setKey(Qt::Key_Shift, false);
        const bool okSneakC = hitSneakC == QVector3D(12, 15, 16)
            && wT34c.blockAt(13, 15, 16) == BR::Planks   // 放置成功（命中面邻格）
            && wT34c.blockAt(12, 15, 16) == BR::WoodTrapdoor
            && (wT34c.stateAt(12, 15, 16) & 1) == 0      // 板未翻（合态）
            && togglesT34c == 0;                          // 翻板零发（use 未发生）
        // (2) 非潜行对照腿：仍持木板（不潜行）右键活板门 B 的 +X 面 → 翻板照常（use 分支优先于放置）：
        //     bit0→1、doorToggled 恰 1 次携 open=true、邻格 (13,15,20) 无放置。
        pumpT34c(260);
        const QVector3D hitUseC = aimT34c(20.5f, 12.95f, 15.10f, 20.5f);
        pcT34c.placeBlock(); // 不潜行（Shift 已松）→ use 照常
        pumpT34c(260);
        const bool okUseC = hitUseC == QVector3D(12, 15, 20)
            && togglesT34c == 1 && lastOpenT34c           // 翻板恰一次（开）
            && wT34c.blockAt(12, 15, 20) == BR::WoodTrapdoor
            && (wT34c.stateAt(12, 15, 20) & 1) == 1       // 板已翻（开态）
            && wT34c.blockAt(13, 15, 20) == BR::Air;      // 无放置（右键被 use 消费）
        // (3) 源钉（pinSet 剥注释；阴性轮摘门即红）。
        const QString rootT34c = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT34c = pinSet(rootT34c + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-trapdoor-sneak-gate", "if (!sneakPlaceBlock && hitId == BlockRegistry::WoodTrapdoor) {"},
        });
        if (!missT34c.isEmpty())
            qInfo().noquote() << "  [t1034c diag] pins" << missT34c.join(QLatin1Char(','));
        QObject::disconnect(cTglC);
        pcT34c.release();
        winT34c.deleteLater();
        const bool okC = okSneakC && okUseC && missT34c.isEmpty();
        if (!okC)
            qInfo().noquote() << "  [t1034c diag] hitSneak" << hitSneakC << "hitUse" << hitUseC;
        if (!okC) ++totalFail;
        qInfo().noquote() << (okC ? "PASS" : "FAIL")
                          << "| t1034c trapdoor sneakPlace bypass: sneaking with a held block and "
                             "right-clicking a closed trapdoor places the held block on the "
                             "clicked face's neighbor cell while the trapdoor stays closed (state "
                             "bit0 untouched, zero doorToggled emissions - use bypassed, MC "
                             "sneak-use caliber, review0909 #2 legacy cleared); without sneak the "
                             "right-click still flips the trapdoor open (bit0 set, exactly one "
                             "doorToggled(true)) and consumes the click so nothing is placed "
                             "(negative-round sensitive: trapdoor-branch sneakPlace gate removal)"
                          << (okC ? QString()
                                  : QStringLiteral("diag sneak=%1 use=%2 pins=%3 toggles=%4")
                                        .arg(okSneakC).arg(okUseC).arg(missT34c.isEmpty())
                                        .arg(togglesT34c));
    });

    // ── P-t1035 豹猫驯服分化口径钉（R19.22 末项；盘点回标 + 现状行为腿）──
    //    盘点结论（生产代码现状 > dev-plan 设想，本探针按现状钉）：t481 生鱼驯服链（~1/3 概率 / 失败仍
    //    耗鱼 / 毛色变体 0..2）+ t949 实机右键输入缝（P-t949(a) 真输入腿持续覆盖）+ t963 项圈镜像补齐
    //    全在库 → 驯服链无 src 缺口。驯服猫现库口径 = 现代 MC：站态**跟随**（kOcelotFollowSpeed 走近 /
    //    kFollowMinDist 停步 / >kOcelotTeleportDist=12 瞬移，t878⑤）+ 坐/站命令（t481 空手分支）+ 红项圈
    //    （t963 双落点）+ 不防御不反击（t923c2）—— dev-plan t1035「信任态不跟随不项圈」设想已被
    //    t878（跟随+瞬移）/ t963（项圈）先行演化掉，过时口径在 docs 登记、行为零改动。
    //    (a) 驯服猫坐态留守（镜像 t831(c) 狼腿）：toggleOcelotSit → 玩家 3 格外 1s 零位移；
    //    (b) 站态跟随走近（镜像 t831(c2)）：玩家 7.5 格外 2s 位移 ≥2（走向主人，kOcelotFollowSpeed=4.0）；
    //    (c) 过远瞬移（镜像 t831(d/d3)）：>12 格 1s 内跳至玩家 ≤10 环；近距 6 走跟不跳变（8 帧 ≤1.0）；
    //    (d) 跟随态字段分化：狼/猫状态 accessor 跨型互查恒 false（wolfTamedAt/wolfSittingAt 对猫槽、
    //        ocelotTamedAt/ocelotSittingAt 对狼槽——同槽真值只在本型 accessor 面），且两链独立成证；
    //    (e) 生鱼来源钉（盘点问 3）：LootTable::fishingPool() 直调——RawFishId 在池且为最高权重条目
    //        （钓鱼产驯服道具，t401 池单一权威；熔炼生→熟与 +2 饥饿口径由 t836 系探针既有在库）。
    //    阴性轮敏感：摘 aiOcelot 站态跟随 chase 段 → (b) 红（猫不再走向主人）；(a)(c)(d)(e) 不受影响
    //    保绿（坐态冻结分支更早、瞬移分支独立于 chase、字段钉在 accessor、池钉在 LootTable）。
    runLegMulti({ "t1035 ocelot taming divergence caliber (dev-plan trust-state vision registered outdated - produc"
        "tion is modern-MC follow caliber): the tamed cat SITS put with a nearby player (1s zero drift, w"
        "olf t831(c) mirror) and FOLLOWS a 7.5-block owner >=2 blocks in 2s (kOcelotFollowSpeed 4.0), tel"
        "eports to the owner's 2..5 ring when >12 blocks (t878⑤ kOcelotTeleportDist) while a 6-block gap "
        "keeps walking (no jump), wolf/ocelot tamed+sitting state accessors cross-query each other's slot"
        " as false (per-type follow-state fields, both chains independently true), and LootTable::fishing"
        "Pool() carries RawFishId as its top-weight entry (raw fish = the taming item source; smelting/hu"
        "nger caliber already pinned by the t836 probes) (negative-round sensitive: aiOcelot stand-follow"
        " chase removal)" }, [&]() {
        World wT35;
        wT35.setWidth(44); wT35.setDepth(44); wT35.setHeight(96); wT35.setSeed(1035);
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) {
                for (int y = 85; y <= 90; ++y) wT35.setBlock(x, y, z, BR::Air, 0); // 瞬移落点扫描带净空
                wT35.setBlock(x, 84, z, BR::Stone, 0); // 平石台（t831 同款免凿高台）
            }
        EntityManager emT35;
        bool ok = true;
        // 驯服入口（P-t831(e) 同款循环；实机输入缝已由 P-t949(a) 持续覆盖，此处 EntityManager 层直驯）。
        const int wolfT35 = emT35.spawnMobTyped(8, 85, 8, EntityManager::MobWolf,
                                                QStringLiteral("#c8ccd4"), 10);
        const int catT35 = emT35.spawnMobTyped(20, 85, 20, EntityManager::MobOcelot,
                                               QStringLiteral("#e8c890"), 10);
        ok = ok && wolfT35 >= 0 && catT35 >= 0;
        bool diagTamedW = false, diagTamedC = false;
        float diagTpDXZ = 99.0f, diagFol = 0.0f, diagSit = 99.0f;
        bool okA = false, okB = false, okC1 = false, okC2 = false, okD = false, okE = false;
        if (ok) {
            bool wolfTamed = false, catTamed = false;
            for (int a = 0; a < 200 && !wolfTamed; ++a)
                wolfTamed = emT35.tameWolf(wolfT35);
            for (int a = 0; a < 200 && !catTamed; ++a)
                catTamed = emT35.tameOcelot(catT35);
            diagTamedW = wolfTamed;
            diagTamedC = catTamed;
            ok = ok && wolfTamed && catTamed;
            // (d) 跟随态字段分化：跨型互查恒 false + 本型真值各自成证（驯服产物 = 各型独立驯服态字段）。
            okD = emT35.wolfTamedAt(wolfT35) && !emT35.ocelotTamedAt(wolfT35)
                && !emT35.ocelotSittingAt(wolfT35)
                && emT35.ocelotTamedAt(catT35) && !emT35.wolfTamedAt(catT35)
                && !emT35.wolfSittingAt(catT35);
            // (a) 坐态留守：猫坐下 → 玩家 3 格外 1s 零位移（MC 坐猫留守，镜像 t831(c)）。
            emT35.toggleOcelotSit(catT35);
            const bool sittingT35 = emT35.ocelotSittingAt(catT35);
            const QVector3D sitP0 = emT35.posAt(catT35);
            for (int t = 0; t < 64; ++t)
                emT35.tick(0.016f, &wT35, QVector3D(23.5f, 86.0f, 20.5f), 0.3f, 1.8f, true);
            diagSit = (emT35.posAt(catT35) - sitP0).length();
            okA = sittingT35 && diagSit < 0.05f;
            // (b) 站态跟随：起立 → 玩家 7.5 格外 2s 位移 ≥2（走向主人；kFollowMinDist=2.5 内停步）。
            emT35.toggleOcelotSit(catT35);
            const bool standingT35 = !emT35.ocelotSittingAt(catT35);
            const QVector3D folP0 = emT35.posAt(catT35);
            for (int t = 0; t < 125; ++t)
                emT35.tick(0.016f, &wT35, QVector3D(27.5f, 86.0f, 20.5f), 0.3f, 1.8f, true);
            diagFol = (emT35.posAt(catT35) - folP0).length();
            okB = standingT35 && diagFol >= 2.0f;
            // (c1) 过远瞬移（>kOcelotTeleportDist=12）：35 格外玩家 1s 内跳至 ≤10 环
            //      （listener y=86 台面层 → 落点扫描带 87..82 覆盖台面 85 格，t831 同注）。
            const QVector3D farT35(36.5f, 86.0f, 36.5f);
            for (int t = 0; t < 64; ++t)
                emT35.tick(0.016f, &wT35, farT35, 0.3f, 1.8f, true);
            const QVector3D tpPos = emT35.posAt(catT35);
            diagTpDXZ = QVector3D(tpPos.x() - farT35.x(), 0.0f, tpPos.z() - farT35.z()).length();
            okC1 = diagTpDXZ <= 10.0f;
            // (c2) 近距 6 走跟不跳变：8 帧（≤2 AI 窗）位移 ≤1.0（瞬移跳变 ≥5 可分辨）。
            const QVector3D nearT35(tpPos.x() + 6.0f, 86.0f, tpPos.z());
            const QVector3D beforeNear = emT35.posAt(catT35);
            for (int t = 0; t < 8; ++t)
                emT35.tick(0.016f, &wT35, nearT35, 0.3f, 1.8f, true);
            okC2 = (emT35.posAt(catT35) - beforeNear).length() < 1.0f;
        }
        // (e) 生鱼来源钉（直调 LootTable 单一权威，随机池无关）：RawFishId 在钓鱼池且为最高权重条目
        //     （~55% 常见获物 = MC raw fish 口径；钓竿拉起链行为面由 t836 系探针既有在库）。
        {
            int fishW = 0, maxW = 0;
            bool fishIn = false;
            for (const LootTable::Entry &e : LootTable::fishingPool()) {
                if (e.itemId == RecipeRegistry::RawFishId) { fishIn = true; fishW = e.weight; }
                if (e.weight > maxW) maxW = e.weight;
            }
            okE = fishIn && fishW == maxW && fishW > 0;
        }
        ok = ok && okA && okB && okC1 && okC2 && okD && okE;
        if (!ok)
            qInfo().noquote() << "  [t1035 diag] tamedW" << diagTamedW << "tamedC" << diagTamedC
                              << "a-sit" << okA << diagSit << "b-follow" << okB << diagFol
                              << "c1-tp" << okC1 << diagTpDXZ << "c2-near" << okC2
                              << "d-fields" << okD << "e-pool" << okE;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1035 ocelot taming divergence caliber (dev-plan trust-state vision "
                             "registered outdated - production is modern-MC follow caliber): the "
                             "tamed cat SITS put with a nearby player (1s zero drift, wolf t831(c) "
                             "mirror) and FOLLOWS a 7.5-block owner >=2 blocks in 2s (kOcelotFollowSpeed "
                             "4.0), teleports to the owner's 2..5 ring when >12 blocks (t878⑤ "
                             "kOcelotTeleportDist) while a 6-block gap keeps walking (no jump), "
                             "wolf/ocelot tamed+sitting state accessors cross-query each other's slot "
                             "as false (per-type follow-state fields, both chains independently true), "
                             "and LootTable::fishingPool() carries RawFishId as its top-weight entry "
                             "(raw fish = the taming item source; smelting/hunger caliber already "
                             "pinned by the t836 probes) (negative-round sensitive: aiOcelot "
                             "stand-follow chase removal)";
    });

    // ── P-t1042a 被动型受击惊逃（R19.23 t1042；MC 原版口径：牛受击只惊逃永不反击）──
    //    (a1) 受击沿真链：玩家空手 beginMining 命中牛（attackMob 唯一生产入口）→ 扣 1HP 存活 +
    //         惊逃态置值（panicTimerAt == 8.0 = kPanicDuration「~8s 量级」登记面）；
    //    (a2) 惊逃位移：3s 内离玩家距离逐 0.5s 窗增益 ≥0.4 且总增益 ≥3.5（panic 2.0 b/s 恒向背离；
    //         wander 上限 1.0 b/s → 3s 总增益 ≤3.0 不可混同，wander 噪声免疫判据）；
    //    (a3) 速度带：疾走期 moveSpeed ∈ [1.4,2.6]（陆地 =kPanicSpeed 2.0；>kWalkSpeed=加速游离，
    //         <kChaseSpeed 2.8=非敌对追击）；
    //    (a4) 零反击：全程 mobAttackedPlayer 计数 == 0（牛从不成为攻击者）；
    //    (a5) 超时回落：8.5s 后 panicTimerAt == 0（时长 8s 量级）+ 再 0.7s 后 moveSpeed ≤1.05
    //         （wander 重掷 idle 0 / kWalkSpeed 1.0；panic 残速 2.0 被选向重掷冲销 = 回落正常游走）。
    //    阴性轮敏感（单构建三摘一轮：aiPanicFlee 三消费点 false && 前缀）：(a2)/(a3) 恰红 + (a5) 恰红
    //         （timer 不衰减恒 8.0、零惊逃位移）；(a1)/(a4) 保绿。
    runLegMulti({ "t1042a passive panic-flee on hit (MC caliber): a bare-fist hit on a cow through the real attackM"
        "ob chain drops exactly 1 HP and arms the panic state at 8.0s (kPanicDuration registration), the "
        "cow then gains >=3.5 blocks of player-distance over 3s with every 0.5s window >=0.4 (monotone aw"
        "ay-drift, wander-noise immune since wander caps at 3.0) at sprint speed within [1.4,2.6] (2x wal"
        "k = accelerated flee, below hostile chase band), fires ZERO mobAttackedPlayer (cows never retali"
        "ate), and after the 8s window the panic timer is exactly 0 with moveSpeed resettled <=1.05 (wand"
        "er re-roll: idle or walk; the panic residual speed is consumed) (negative-round sensitive: the t"
        "hree aiPanicFlee consumer gates false&&-prefixed in one build)diag a1=%1 a2=%2(%3) a3=%4 a4=%5 a"
        "5=%6(%7)" }, [&]() {
        World wa;
        wa.setWidth(44); wa.setDepth(44); wa.setHeight(96); wa.setSeed(1042);
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wa.setBlock(x, 84, z, BR::Stone, 0);
        EntityManager ema;
        Hotbar hba;
        PlayerController pca;
        QQuickWindow winA1042;
        pca.setParentItem(winA1042.contentItem());
        pca.grab(); // m_captured（beginMining 入口门，t949 同式）
        pca.setWorld(&wa);
        pca.setEntityManager(&ema);
        pca.setHotbar(&hba); // 槽保持空（heldItemId==0 → 空手 kFistDamage 1）
        pca.setSelectedBlock(BR::Air); // 材料段/空手 selectedBlock 归 Air 建模（t1030 教训）
        const int cow = ema.spawnMobTyped(20, 85, 22, EntityManager::MobCow,
                                          QStringLiteral("#5a4030"), 10);
        const QVector3D pp(20.5f, 85.0f, 25.0f); // 玩家脚位（牛南方 2.5 格）
        const QVector3D eye(20.5f, 86.62f, 25.0f);
        const QVector3D dir = (QVector3D(20.5f, 85.5f, 22.5f) - eye).normalized();
        pca.loadSavedState(eye.x(), 85.0f, eye.z(),
                           qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                           qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
        int bites = 0;
        QObject::connect(&ema, &EntityManager::mobAttackedPlayer,
                         [&bites](int, int, float, float) { ++bites; });
        pca.beginMining(); // 受击沿真链（t242/t866）→ damageEntity(1) + knockback + setPanicFlee
        const bool a1 = cow >= 0 && ema.healthAt(cow) == 9
                        && std::abs(ema.panicTimerAt(cow) - 8.0f) < 1e-3f;
        bool a2 = true, a3 = true;
        float totalGain = 0.0f;
        const auto dxz = [&pp](const QVector3D &p) {
            return QVector3D(p.x() - pp.x(), 0.0f, p.z() - pp.z()).length();
        };
        float prevD = dxz(ema.posAt(cow));
        for (int w = 0; w < 6; ++w) { // 3s 惊逃窗（6 × 0.5s）
            for (int t = 0; t < 31; ++t) ema.tick(0.016f, &wa, pp, 0.3f, 1.8f, true);
            const float d = dxz(ema.posAt(cow));
            const float gain = d - prevD;
            if (gain < 0.4f) a2 = false; // 逐窗背离（panic 1.0/窗；击退窗更高）
            totalGain += gain;
            const float ms = ema.moveSpeedAt(cow);
            if (ms < 1.4f || ms > 2.6f) a3 = false; // 疾走带（陆地 = 2.0）
            prevD = d;
        }
        a2 = a2 && totalGain >= 3.5f; // wander 3s 上限 3.0 → 判据噪声免疫
        const bool a4 = bites == 0;
        for (int t = 0; t < 344; ++t) ema.tick(0.016f, &wa, pp, 0.3f, 1.8f, true); // 累计 ~8.5s
        const bool a5timer = ema.panicTimerAt(cow) == 0.0f;
        for (int t = 0; t < 44; ++t) ema.tick(0.016f, &wa, pp, 0.3f, 1.8f, true); // 0.7s 回落窗
        const bool a5 = a5timer && ema.moveSpeedAt(cow) <= 1.05f;
        pca.release();
        winA1042.deleteLater();
        const bool ok = a1 && a2 && a3 && a4 && a5;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1042a passive panic-flee on hit (MC caliber): a bare-fist hit on "
                             "a cow through the real attackMob chain drops exactly 1 HP and arms "
                             "the panic state at 8.0s (kPanicDuration registration), the cow then "
                             "gains >=3.5 blocks of player-distance over 3s with every 0.5s window "
                             ">=0.4 (monotone away-drift, wander-noise immune since wander caps at "
                             "3.0) at sprint speed within [1.4,2.6] (2x walk = accelerated flee, "
                             "below hostile chase band), fires ZERO mobAttackedPlayer (cows never "
                             "retaliate), and after the 8s window the panic timer is exactly 0 "
                             "with moveSpeed resettled <=1.05 (wander re-roll: idle or walk; the "
                             "panic residual speed is consumed) (negative-round sensitive: the "
                             "three aiPanicFlee consumer gates false&&-prefixed in one build)"
                          << (ok ? QString()
                                 : QStringLiteral("diag a1=%1 a2=%2(%3) a3=%4 a4=%5 a5=%6(%7)")
                                       .arg(a1).arg(a2).arg(totalGain).arg(a3).arg(a4)
                                       .arg(a5).arg(a5timer));
    });

    // ── P-t1042b 豹猫被打逃逸 + 驯服狼被打零反击对照腿（R19.23 t1042）──
    //    (b1) 未驯服豹猫受击沿真链命中 → 惊逃态置值 + 3s 离玩家总增益 ≥3.5 + 零 mobAttackedPlayer
    //         （MC 原版口径：豹猫被打只逃不反击）；
    //    (b2) 驯服狼（t1031 驯服缝直调）被玩家真链命中 → 零 mobAttackedPlayer（6s）+ 无惊逃态置值 +
    //         掉血恰 1（驯服狼豁免维持——被打不反击主人，对照腿）。
    //    阴性轮敏感：摘惊逃消费点 → (b1) 恰红（豹猫零位移）；(b2) 对照腿保绿（不依赖惊逃分支）。
    runLegMulti({ "t1042b ocelot flees and tamed wolf never retaliates (MC caliber): a bare-fist hit on an untamed "
        "ocelot arms the 8s panic state and yields >=3.5 blocks of player-distance over 3s with ZERO mobA"
        "ttackedPlayer (ocelots flee and never fight back), while the control leg shows a tamed wolf stru"
        "ck by its owner drops exactly 1 HP with zero panic state and zero bites over 6s (t1031 exemption"
        " maintained - the tamed wolf never retaliates against the player) (negative-round sensitive: onl"
        "y the ocelot leg)diag b1=%1(%2) b2=%3" }, [&]() {
        // (b1) 豹猫逃逸。
        bool okB1 = false;
        float gainB1 = -1.0f;
        {
            World wb;
            wb.setWidth(44); wb.setDepth(44); wb.setHeight(96); wb.setSeed(1043);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wb.setBlock(x, 84, z, BR::Stone, 0);
            EntityManager emb;
            Hotbar hbb;
            PlayerController pcb;
            QQuickWindow winB1042;
            pcb.setParentItem(winB1042.contentItem());
            pcb.grab();
            pcb.setWorld(&wb);
            pcb.setEntityManager(&emb);
            pcb.setHotbar(&hbb);
            pcb.setSelectedBlock(BR::Air);
            const int cat = emb.spawnMobTyped(26, 85, 22, EntityManager::MobOcelot,
                                              QStringLiteral("#e8c890"), 10);
            const QVector3D pp(26.5f, 85.0f, 25.0f);
            const QVector3D eye(26.5f, 86.62f, 25.0f);
            const QVector3D dir = (QVector3D(26.5f, 85.5f, 22.5f) - eye).normalized();
            pcb.loadSavedState(eye.x(), 85.0f, eye.z(),
                               qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                               qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            int bites = 0;
            QObject::connect(&emb, &EntityManager::mobAttackedPlayer,
                             [&bites](int, int, float, float) { ++bites; });
            pcb.beginMining();
            const bool hitOk = cat >= 0 && emb.healthAt(cat) == 9
                               && std::abs(emb.panicTimerAt(cat) - 8.0f) < 1e-3f;
            const auto dxz = [&pp](const QVector3D &p) {
                return QVector3D(p.x() - pp.x(), 0.0f, p.z() - pp.z()).length();
            };
            const float d0 = dxz(emb.posAt(cat));
            for (int t = 0; t < 188; ++t) emb.tick(0.016f, &wb, pp, 0.3f, 1.8f, true); // 3s
            gainB1 = dxz(emb.posAt(cat)) - d0;
            pcb.release();
            winB1042.deleteLater();
            okB1 = hitOk && gainB1 >= 3.5f && bites == 0;
        }
        // (b2) 驯服狼零反击对照腿。
        bool okB2 = false;
        {
            World wc;
            wc.setWidth(44); wc.setDepth(44); wc.setHeight(96); wc.setSeed(1044);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wc.setBlock(x, 84, z, BR::Stone, 0);
            EntityManager emc;
            Hotbar hbc;
            PlayerController pcc;
            QQuickWindow winC1042;
            pcc.setParentItem(winC1042.contentItem());
            pcc.grab();
            pcc.setWorld(&wc);
            pcc.setEntityManager(&emc);
            pcc.setHotbar(&hbc);
            pcc.setSelectedBlock(BR::Air);
            const int wolf = emc.spawnMobTyped(20, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            emc.setTameRollOverride(0); // t1031 驯服缝必成（直调驯服）
            const bool tamed = wolf >= 0 && emc.tameWolf(wolf);
            emc.setTameRollOverride(-1);
            const QVector3D pp(20.5f, 85.0f, 25.0f);
            const QVector3D eye(20.5f, 86.62f, 25.0f);
            const QVector3D dir = (QVector3D(20.5f, 85.5f, 22.5f) - eye).normalized();
            pcc.loadSavedState(eye.x(), 85.0f, eye.z(),
                               qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                               qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            int bites = 0;
            QObject::connect(&emc, &EntityManager::mobAttackedPlayer,
                             [&bites](int, int, float, float) { ++bites; });
            pcc.beginMining(); // 打自己的驯服狼
            const bool hitOk = tamed && emc.healthAt(wolf) == 9;
            for (int t = 0; t < 375; ++t) emc.tick(0.016f, &wc, pp, 0.3f, 1.8f, true); // 6s
            pcc.release();
            winC1042.deleteLater();
            okB2 = hitOk && bites == 0 && emc.panicTimerAt(wolf) == 0.0f;
        }
        const bool ok = okB1 && okB2;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1042b ocelot flees and tamed wolf never retaliates (MC caliber): "
                             "a bare-fist hit on an untamed ocelot arms the 8s panic state and "
                             "yields >=3.5 blocks of player-distance over 3s with ZERO "
                             "mobAttackedPlayer (ocelots flee and never fight back), while the "
                             "control leg shows a tamed wolf struck by its owner drops exactly 1 "
                             "HP with zero panic state and zero bites over 6s (t1031 exemption "
                             "maintained - the tamed wolf never retaliates against the player) "
                             "(negative-round sensitive: only the ocelot leg)"
                          << (ok ? QString()
                                 : QStringLiteral("diag b1=%1(%2) b2=%3").arg(okB1).arg(gainB1).arg(okB2));
    });

    // ── P-t1042c 野狼被打敌对反击 + 狼无 panic（R19.23 t1042；t1047 O-3 改写：MC 原版——狼无
    //    PanicGoal）──
    //    (c1) 野狼受击沿真链命中（扣 1HP 存活）→ 6s 内 mobAttackedPlayer ≥1 次、伤害 ≥1（狼咬击
    //         kWolfAttackDamage=4 面经信号下传）+ 狼-玩家最小 XZ 距 ≤1.7（咬击带，t1031a 参战腿判据）；
    //    (c2) 幼狼被打 → 零 panic 零反击（t1047 O-3：setPanicFlee 狼全族 no-op + setWolfProvoked baby
    //         门维持）：驯服双亲繁殖产幼崽（t400 enterLoveMode ×2 真链）→ 摘除双亲（防命中幼崽的
    //         setWolfTarget 泼到驯服双亲身上触发防御追咬污染位移）→ 玩家命中幼崽 → panicTimerAt 恒 0
    //         （命中沿 + 2s 后双查）+ 2s 离玩家增益 <2.5（无惊逃加速；panic 4.0 vs wander 上限 2.0
    //         可分辨带沿用）+ 零 bites。
    //    阴性轮敏感（t1047）：临时回插 setPanicFlee 的 wolfBaby 分支 → (c2) 恰红（panicTimer==8 +
    //         惊逃增益 ≥2.5 双面红）；(c1) 反击腿保绿（独立分支）。
    runLegMulti({ "t1042c wild wolf retaliates and wolves have no panic (MC caliber): a bare-fist hit on a wild adu"
        "lt wolf through the real attackMob chain drops 1 HP and the wolf closes into the bite band (min "
        "gap <=1.7) landing at least one mobAttackedPlayer of >=1 damage as MobWolf within 6s (hostile-st"
        "yle retaliation with the same chase/memory clearing caliber), while a bred wolf puppy struck by "
        "the player stays at zero panic - the panic timer never arms (checked right after the hit and aga"
        "in after 2s), the puppy gains no flee acceleration (2s distance gain stays below the 2.5 panic-v"
        "s-wander discrimination band) and zero bites land (wolves have no panic (MC caliber): no PanicGo"
        "al, adults retaliate via setWolfProvoked instead, babies zero reaction; parents removed pre-hit "
        "so the t480 defense chain cannot pollute the path) (negative-round sensitive: only the puppy leg"
        " - re-inserting the withdrawn setPanicFlee wolf-baby branch reds it)diag c1=%1 c2=%2(%3)" }, [&]() {
        // (c1) 野狼反击。
        bool okC1 = false;
        {
            World wa;
            wa.setWidth(44); wa.setDepth(44); wa.setHeight(96); wa.setSeed(1045);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wa.setBlock(x, 84, z, BR::Stone, 0);
            EntityManager ema;
            Hotbar hba;
            PlayerController pca;
            QQuickWindow winD1042;
            pca.setParentItem(winD1042.contentItem());
            pca.grab();
            pca.setWorld(&wa);
            pca.setEntityManager(&ema);
            pca.setHotbar(&hba);
            pca.setSelectedBlock(BR::Air);
            const int wolf = ema.spawnMobTyped(20, 85, 22, EntityManager::MobWolf,
                                               QStringLiteral("#c8ccd4"), 10);
            const QVector3D pp(20.5f, 85.0f, 25.0f);
            const QVector3D eye(20.5f, 86.62f, 25.0f);
            const QVector3D dir = (QVector3D(20.5f, 85.5f, 22.5f) - eye).normalized();
            pca.loadSavedState(eye.x(), 85.0f, eye.z(),
                               qRadiansToDegrees(std::atan2(-dir.x(), -dir.z())),
                               qRadiansToDegrees(std::asin(dir.y())), 2 /* Survival */);
            int bites = 0, biteDmg = -1, biteType = -1;
            QObject::connect(&ema, &EntityManager::mobAttackedPlayer,
                             [&](int amount, int type, float, float) {
                                 ++bites; biteDmg = amount; biteType = type;
                             });
            pca.beginMining();
            const bool hitOk = wolf >= 0 && ema.healthAt(wolf) == 9;
            float minD = 1e9f;
            for (int t = 0; t < 375; ++t) { // 6s
                ema.tick(0.016f, &wa, pp, 0.3f, 1.8f, true);
                const QVector3D pw = ema.posAt(wolf);
                minD = std::min(minD, QVector3D(pw.x() - pp.x(), 0.0f, pw.z() - pp.z()).length());
            }
            pca.release();
            winD1042.deleteLater();
            okC1 = hitOk && bites >= 1 && biteDmg >= 1 && biteType == EntityManager::MobWolf
                   && minD <= 1.7f;
            if (!okC1)
                qInfo().noquote() << "  [t1042c diag] hitOk=" << hitOk << "bites=" << bites
                                  << "dmg=" << biteDmg << "type=" << biteType << "minD=" << minD;
        }
        // (c2) 幼狼只惊逃。
        bool okC2 = false;
        float gainC2 = -1.0f;
        {
            World wb;
            wb.setWidth(44); wb.setDepth(44); wb.setHeight(96); wb.setSeed(1046);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wb.setBlock(x, 84, z, BR::Stone, 0);
            EntityManager emb;
            const QVector3D farL(22.5f, 86.0f, 22.5f); // 世界中心（幼崽跟随瞬移落 2..5 环 → 四向留足惊逃跑位）
            emb.setTameRollOverride(0); // t1031 驯服缝必成
            const int pa = emb.spawnMobTyped(14, 85, 15, EntityManager::MobWolf,
                                             QStringLiteral("#c8ccd4"), 10);
            const int pb = emb.spawnMobTyped(15, 85, 15, EntityManager::MobWolf,
                                             QStringLiteral("#c8ccd4"), 10);
            const bool parents = pa >= 0 && pb >= 0 && emb.tameWolf(pa) && emb.tameWolf(pb);
            emb.setTameRollOverride(-1);
            const bool love = parents && emb.enterLoveMode(pa) && emb.enterLoveMode(pb);
            for (int t = 0; t < 24; ++t) emb.tick(0.016f, &wb, farL, 0.3f, 1.8f, false); // 配对产崽
            int baby = -1;
            for (int i = 0; i < emb.count(); ++i)
                if (emb.aliveAt(i) && emb.isBabyAt(i)
                    && emb.mobTypeAt(i) == EntityManager::MobWolf) baby = i;
            // 摘除双亲（命中幼崽 setWolfTarget=幼崽 会让驯服双亲防御追咬它 → 污染惊逃位移面；
            //   999 致死走既有死亡链，0.64s 后尸体移除完毕）。
            emb.damageEntity(pa, 999);
            emb.damageEntity(pb, 999);
            for (int t = 0; t < 40; ++t) emb.tick(0.016f, &wb, farL, 0.3f, 1.8f, false);
            const bool babyOk = love && baby >= 0 && emb.aliveAt(baby);
            Hotbar hbb;
            PlayerController pcb;
            QQuickWindow winE1042;
            pcb.setParentItem(winE1042.contentItem());
            pcb.grab();
            pcb.setWorld(&wb);
            pcb.setEntityManager(&emb);
            pcb.setHotbar(&hbb);
            pcb.setSelectedBlock(BR::Air);
            const QVector3D bp = emb.posAt(baby);
            const QVector3D ppB(bp.x(), 85.0f, bp.z() + 3.0f);
            const QVector3D eyeB(bp.x(), 86.62f, bp.z() + 3.0f);
            const QVector3D dirB = (QVector3D(bp.x(), bp.y(), bp.z()) - eyeB).normalized();
            pcb.loadSavedState(eyeB.x(), 85.0f, eyeB.z(),
                               qRadiansToDegrees(std::atan2(-dirB.x(), -dirB.z())),
                               qRadiansToDegrees(std::asin(dirB.y())), 2 /* Survival */);
            int bites = 0;
            QObject::connect(&emb, &EntityManager::mobAttackedPlayer,
                             [&bites](int, int, float, float) { ++bites; });
            const int hp0 = emb.healthAt(baby);
            pcb.beginMining();
            const bool hitOk = babyOk && emb.healthAt(baby) == hp0 - 1
                               && emb.panicTimerAt(baby) == 0.0f; // t1047 O-3：命中沿零 panic
            const auto dxz = [&ppB](const QVector3D &p) {
                return QVector3D(p.x() - ppB.x(), 0.0f, p.z() - ppB.z()).length();
            };
            const float d0 = dxz(emb.posAt(baby));
            for (int t = 0; t < 125; ++t) emb.tick(0.016f, &wb, ppB, 0.3f, 1.8f, true); // 2s
            gainC2 = dxz(emb.posAt(baby)) - d0;
            pcb.release();
            winE1042.deleteLater();
            okC2 = hitOk && gainC2 < 2.5f && emb.panicTimerAt(baby) == 0.0f && bites == 0; // 无惊逃加速 + 2s 后仍零 panic
            if (!okC2)
                qInfo().noquote() << "  [t1042c diag] babyOk=" << babyOk << "hitOk=" << hitOk
                                  << "gain=" << gainC2 << "bites=" << bites;
        }
        const bool ok = okC1 && okC2;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1042c wild wolf retaliates and wolves have no panic (MC "
                             "caliber): a bare-fist hit on a wild adult wolf through the real "
                             "attackMob chain drops 1 HP and the wolf closes into the bite band "
                             "(min gap <=1.7) landing at least one mobAttackedPlayer of >=1 "
                             "damage as MobWolf within 6s (hostile-style retaliation with the "
                             "same chase/memory clearing caliber), while a bred wolf puppy "
                             "struck by the player stays at zero panic - the panic timer never "
                             "arms (checked right after the hit and again after 2s), the puppy "
                             "gains no flee acceleration (2s distance gain stays below the 2.5 "
                             "panic-vs-wander discrimination band) and zero bites land (wolves "
                             "have no panic (MC caliber): no PanicGoal, adults retaliate via "
                             "setWolfProvoked instead, babies zero reaction; parents removed "
                             "pre-hit so the t480 defense chain cannot pollute the path) "
                             "(negative-round sensitive: only the puppy leg - re-inserting the "
                             "withdrawn setPanicFlee wolf-baby branch reds it)"
                          << (ok ? QString()
                                 : QStringLiteral("diag c1=%1 c2=%2(%3)").arg(okC1).arg(okC2).arg(gainC2));
    });

    // ── P-t1042d t1042 接线源钉（pinSet 剥注释；阴性轮摘 aiPanicFlee 三消费点（false && 前缀）本组
    //     恒绿——钉面全为定义 / 调用 / 门 / 消费谓词本体，false && 前缀不摘语句 → 恰 P-t1042a/b/c 红律）
    //     t1047 O-4 追加：headPitchAt 惊逃门针（headless 无吃草态缝 → 行为腿不可达，结构钉 + 实机确认
    //     补面，review0912 #4 如实 scoped）──
    runLegMulti({ "t1042d panic-flee + wolf-retaliation wiring source pins: the attackMob hit-path carries both new"
        " write-points (setPanicFlee + setWolfProvoked) next to the t480/t635 precedent wires, EntityMana"
        "ger defines the panic registrar with its MC-caliber log face, the wolf provocation gate (tamed-o"
        "r-baby exempt, t1031 kept), the shared aiPanicFlee mover consumed by exactly three gates (generi"
        "c passive chain, untamed aiOcelot, aiWolf puppy), and the header carries the panicTimer field + "
        "panicTimerAt accessor + aiPanicFlee declaration + kPanicDuration constant, and headPitchAt carri"
        "es the t1047 O-4 panic gate zeroing the graze pose while panicTimer runs (comment-immune pinSet;"
        " the consumer-gate pin is false&&-mutation-immune so the negative round reds exactly the behavio"
        "r legs)diag pins=%1" }, [&]() {
        bool ok = true;
        const QString exeDir1042 = QCoreApplication::applicationDirPath();
        const QString root1042 = QDir(exeDir1042 + QStringLiteral("/..")).absolutePath();
        QStringList miss1042;
        miss1042 << pinSet(root1042 + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-panic-wire", "m_entityManager->setPanicFlee(entityIndex);"},
            {"cpp-wolf-provoke-wire", "m_entityManager->setWolfProvoked(entityIndex);"},
        });
        miss1042 << pinSet(root1042 + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-panic-set-def", "void EntityManager::setPanicFlee(int i)"},
            {"cpp-panic-set-log", "panic-flees for"},
            {"cpp-wolf-provoke-def", "void EntityManager::setWolfProvoked(int i)"},
            {"cpp-wolf-provoke-gate", "if (e.wolfTamed || e.baby) return;"},
            {"cpp-wolf-provoke-log", "provoked: retaliates against player"},
            {"cpp-panic-flee-def", "bool EntityManager::aiPanicFlee(Entity &e, float dt, World *world, float worldW, float worldD,"},
            {"cpp-panic-consumers", "e.panicTimer > 0.0f", 3}, // 通用链 / aiOcelot / aiWolf 幼崽三消费点
            {"cpp-headpitch-panic-gate", "if (e.panicTimer > 0.0f) return 0.0f;"}, // t1047 O-4 惊逃期头回正
        });
        miss1042 << pinSet(root1042 + QStringLiteral("/src/Entities/entitymanager.h"), {
            {"hdr-panic-field", "float panicTimer = 0.0f;"},
            {"hdr-panic-accessor", "Q_INVOKABLE float panicTimerAt(int i) const;"},
            {"hdr-panic-flee-decl", "bool aiPanicFlee(Entity &e, float dt, World *world, float worldW, float worldD,"},
            {"hdr-panic-const", "static constexpr float kPanicDuration = 8.0f;"},
        });
        ok = miss1042.isEmpty();
        if (!ok)
            qInfo().noquote() << "  [t1042d diag] miss=" << miss1042.join(QLatin1Char(','));
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1042d panic-flee + wolf-retaliation wiring source pins: the "
                             "attackMob hit-path carries both new write-points (setPanicFlee + "
                             "setWolfProvoked) next to the t480/t635 precedent wires, "
                             "EntityManager defines the panic registrar with its MC-caliber log "
                             "face, the wolf provocation gate (tamed-or-baby exempt, t1031 kept), "
                             "the shared aiPanicFlee mover consumed by exactly three gates "
                             "(generic passive chain, untamed aiOcelot, aiWolf puppy), and the "
                             "header carries the panicTimer field + panicTimerAt accessor + "
                             "aiPanicFlee declaration + kPanicDuration constant, and "
                             "headPitchAt carries the t1047 O-4 panic gate zeroing the graze "
                             "pose while panicTimer runs (comment-immune "
                             "pinSet; the consumer-gate pin is false&&-mutation-immune so the "
                             "negative round reds exactly the behavior legs)"
                          << (ok ? QString()
                                 : QStringLiteral("diag pins=%1").arg(miss1042.join(QLatin1Char(','))));
    });

    // ── P-t1043a 流水冲毁铁轨腿（R19.23 t1043 裁-1 清偿；MC 原版口径：流水冲毁全部三种铁轨）──
    //    (a) 谓词腿：isAttachableBlock 单一权威含轨族三 id（Rail 103 / GoldenRail 127 / DetectorRail
    //        128，经 isRail 单一权威）+ 轨族 dropId=自身（掉落链免费成立——与玩家挖除同链）；
    //    (b) 行为腿：地下石平台一排 + 三种轨并排 + 上游水源（同源 grounded 扩散，level 4/5/6 依次
    //        到达）→ 三种轨全部被冲毁（格 Air ∨ Water）+ 各发一次 blockDroppedAsItem（按精确坐标
    //        计数，worldgen 矿井轨 / 其它 wash 不入账）；阴性轮敏感：摘 blockregistry.h 的
    //        `|| isRail(id)` 入族项 → 谓词腿 + 三 wash 腿 + P-t1012c 恰红。
    runLegMulti({ "t1043a flowing water washes rails (MC caliber, parity ruling cai-1):the attachable-block water-d"
        "estroy single authority includes all threerail ids (rail 103 / golden 127 / detector 128 via the"
        " isRail familypredicate) with dropId = itself for the whole family so the drop chainis free (sam"
        "e chain as player mining) - behaviorally one grounded watersource spreading along a stone platfo"
        "rm washes all three rail kinds inits path (cells go Air/Water) and each emits exactly oneblockDr"
        "oppedAsItem at its own cell with its own id (counted by exactcoordinates so worldgen mineshaft r"
        "ails stay out of the tally)diag a=%1 b see [t1043a diag b]" }, [&]() {
        bool okA = BlockRegistry::isAttachableBlock(quint8(BR::Rail))
            && BlockRegistry::isAttachableBlock(quint8(BR::GoldenRail))
            && BlockRegistry::isAttachableBlock(quint8(BR::DetectorRail))
            && BlockRegistry::dropId(quint8(BR::Rail)) == int(BR::Rail)
            && BlockRegistry::dropId(quint8(BR::GoldenRail)) == int(BR::GoldenRail)
            && BlockRegistry::dropId(quint8(BR::DetectorRail)) == int(BR::DetectorRail);
        bool okB = false;
        {
            World wT43a;
            wT43a.setWidth(48); wT43a.setDepth(48); wT43a.setHeight(64); wT43a.setSeed(1043);
            // worldgen 水沉降（t1012c 同口径：推进到连续静默，免地形水体扩散扰 rig）。
            int wc43a = 0;
            QObject::connect(&wT43a, &World::worldChanged, &wT43a, [&]() { ++wc43a; });
            const auto settle43a = [&]() {
                int quiet = 0;
                for (int i = 0; i < 2000 && quiet < 10; ++i) {
                    const int wc0 = wc43a;
                    wT43a.tickWaterFlow(); wT43a.tickWaterFlow(); wT43a.tickWaterFlow();
                    quiet = (wc43a == wc0) ? quiet + 1 : 0;
                }
            };
            settle43a();
            // 地下石平台 y=40（x 18..28，z=24）+ 净空 y41..43；三种轨并排 x=22/23/24（y=41），
            //   水源 x=18（grounded 扩散：x19=l1 .. x22=l4 Rail → x23=l5 GoldenRail → x24=l6
            //   DetectorRail 全冲；x25=l7 平台收尾）。
            constexpr int PY = 40, PZ = 24;
            for (int x = 18; x <= 28; ++x)
                for (int dy = 1; dy <= 3; ++dy)
                    if (wT43a.blockAt(x, PY + dy, PZ) != BR::Air)
                        wT43a.setWaterSilent(x, PY + dy, PZ, BR::Air, 0);
            for (int x = 18; x <= 28; ++x) wT43a.setBlock(x, PY, PZ, BR::Stone, 0);
            const int railX[3] = { 22, 23, 24 };
            const quint8 railId[3] = { BR::Rail, BR::GoldenRail, BR::DetectorRail };
            for (int i = 0; i < 3; ++i)
                wT43a.setBlock(railX[i], PY + 1, PZ, railId[i], 0);
            // 掉落按精确坐标计数（worldgen 矿井轨 / 其它 wash 不入账）。
            int railDrops43a = 0;
            QObject::connect(&wT43a, &World::blockDroppedAsItem, &wT43a,
                             [&](int x, int y, int z, int id) {
                                 if (y != PY + 1 || z != PZ) return;
                                 for (int i = 0; i < 3; ++i)
                                     if (x == railX[i] && id == int(railId[i])) ++railDrops43a;
                             });
            wT43a.setBlock(18, PY + 1, PZ, BR::Water, 0); // 源（桶倒路径同款源写入）
            settle43a();
            bool allWashed = true;
            for (int i = 0; i < 3; ++i) {
                const quint8 after = wT43a.blockAt(railX[i], PY + 1, PZ);
                allWashed = allWashed && (after == BR::Air || after == BR::Water);
            }
            okB = allWashed && railDrops43a == 3;
            if (!okB) {
                QString cells43a;
                for (int i = 0; i < 3; ++i)
                    cells43a += QString(" %1").arg(int(wT43a.blockAt(railX[i], PY + 1, PZ)));
                qInfo().noquote() << "  [t1043a diag b] cells" << cells43a
                                  << "drops" << railDrops43a;
            }
        }
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        qInfo().noquote() << (okA && okB ? "PASS" : "FAIL")
                          << "| t1043a flowing water washes rails (MC caliber, parity ruling cai-1):"
                             "the attachable-block water-destroy single authority includes all three"
                             "rail ids (rail 103 / golden 127 / detector 128 via the isRail family"
                             "predicate) with dropId = itself for the whole family so the drop chain"
                             "is free (same chain as player mining) - behaviorally one grounded water"
                             "source spreading along a stone platform washes all three rail kinds in"
                             "its path (cells go Air/Water) and each emits exactly one"
                             "blockDroppedAsItem at its own cell with its own id (counted by exact"
                             "coordinates so worldgen mineshaft rails stay out of the tally)"
                          << (okA && okB ? QString()
                                         : QStringLiteral("diag a=%1 b see [t1043a diag b]").arg(okA));
    });

    // ── P-t1043b 矿井 worldgen 防水腿（t1043 伴随义务；轨只在干燥格放置）+ 双机制源钉 ──
    //    (a) 行为/静态腿：12 seed × 128×128×64 真世界全量 worldgen → 全图扫 Rail（worldgen 只铺
    //        Rail），每轨核查「干燥格」= 切比雪夫距 2 的 5×5×5 邻域无水（= placeMineshaft 干燥门
    //        的放置契约，生成态复核）→ 违例恒 0 且轨总数 > 0（非空转：12 世界几千轨在扫描）。
    //        几何依据（world.cpp t1043 注释同源）：placeUndergroundWaterPools 先于矿井 → 池水存活
    //        层与巷道 breach 的接触面必在最近候选轨的切比雪夫 2 内 → 摘干燥门（false&& 前缀）后
    //        12 世界必有违例 → 本腿恰红。
    //    (b) 源钉：blockregistry.h 入族行（`|| isRail(id);`）+ world.cpp 候选登记 / 干燥门扫描 /
    //        跳过 / 落块四语句本体（剥注释 pinSet；阴性轮 1 摘入族行 → hdr 钉红；阴性轮 2 摘干燥门
    //        → cpp 钉红）。
    runLegMulti({ "t1043b mineshaft worldgen waterproofing + dual-mechanism source pins(parity ruling cai-1 compani"
        "on duty, not an exemption): across 12 freshfully-generated 128x128x64 worlds every worldgen rail"
        " sits in a drycell (no water within Chebyshev distance 2 - the exact placementcontract of the pl"
        "aceMineshaft dry-cell gate, which defers rail blocksuntil the corridor walk finishes and then dr"
        "ops only candidates whose5x5x5 neighborhood is water-free) with thousands of rails scanned sothe"
        " leg cannot pass vacuously; the pinSet half anchors the family joinline in blockregistry.h and t"
        "he candidate-row / dry-scan / skip / placestatement bodies in world.cpp (comment-immune), so the"
        " negative roundsred exactly this leg plus the mutated mechanism's own pinsdiag scan=%1 pins=%2" }, [&]() {
        bool ok = true;
        int worldsT1043b = 0;
        long railTotalT1043b = 0;
        int violT1043b = 0;
        const quint32 seedsT1043b[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 4242u,
                                        2024u, 31337u, 7u, 99u, 12345u, 5150u };
        for (quint32 sd : seedsT1043b) {
            World wT1043b;
            wT1043b.setWidth(128);
            wT1043b.setDepth(128);
            wT1043b.setHeight(64);
            wT1043b.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含 placeMineshaft + 干燥门）
            ++worldsT1043b;
            for (int x = 0; x < 128; ++x)
                for (int z = 0; z < 128; ++z)
                    for (int y = 0; y < 64; ++y)
                        if (wT1043b.blockAt(x, y, z) == BR::Rail) {
                            ++railTotalT1043b;
                            bool wet = false;
                            for (int dx = -2; dx <= 2 && !wet; ++dx)
                                for (int dy = -2; dy <= 2 && !wet; ++dy)
                                    for (int dz = -2; dz <= 2 && !wet; ++dz)
                                        if (wT1043b.blockAt(x + dx, y + dy, z + dz) == BR::Water)
                                            wet = true;
                            if (wet) ++violT1043b; // 干燥门契约：生成态任何轨 5×5×5 邻域不得有水
                        }
        }
        const bool okScan = worldsT1043b == 12 && railTotalT1043b > 0 && violT1043b == 0;
        if (!okScan)
            qInfo().noquote() << "  [t1043b diag] worlds" << worldsT1043b << "rails"
                              << railTotalT1043b << "violations" << violT1043b;
        // (b) 双机制源钉（剥注释；t1042d 同款 root 解析）。
        const QString exeDir1043b = QCoreApplication::applicationDirPath();
        const QString root1043b = QDir(exeDir1043b + QStringLiteral("/..")).absolutePath();
        QStringList miss1043b;
        miss1043b << pinSet(root1043b + QStringLiteral("/src/Core/blockregistry.h"), {
            {"hdr-wash-family-base", "id == Torch || id == RedstoneTorch || id == Cobweb || id == Ladder"},
            {"hdr-rail-family-join", "|| isRail(id);"},
        });
        miss1043b << pinSet(root1043b + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-rail-candidate-row", "railCandidates.push_back({ax, ry, az});"},
            {"cpp-dry-gate-scan", "for (int ddx = -2; ddx <= 2 && !wet; ++ddx)"},
            {"cpp-dry-gate-skip", "if (wet) continue;"},
            {"cpp-dry-gate-place", "m_chunks.setBlock(rx, ry, rz, BlockRegistry::Rail, 0);"},
        });
        const bool okPins = miss1043b.isEmpty();
        if (!okPins)
            qInfo().noquote() << "  [t1043b diag pins] miss=" << miss1043b.join(QLatin1Char(','));
        ok = okScan && okPins;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1043b mineshaft worldgen waterproofing + dual-mechanism source pins"
                             "(parity ruling cai-1 companion duty, not an exemption): across 12 fresh"
                             "fully-generated 128x128x64 worlds every worldgen rail sits in a dry"
                             "cell (no water within Chebyshev distance 2 - the exact placement"
                             "contract of the placeMineshaft dry-cell gate, which defers rail blocks"
                             "until the corridor walk finishes and then drops only candidates whose"
                             "5x5x5 neighborhood is water-free) with thousands of rails scanned so"
                             "the leg cannot pass vacuously; the pinSet half anchors the family join"
                             "line in blockregistry.h and the candidate-row / dry-scan / skip / place"
                             "statement bodies in world.cpp (comment-immune), so the negative rounds"
                             "red exactly this leg plus the mutated mechanism's own pins"
                          << (ok ? QString()
                                 : QStringLiteral("diag scan=%1 pins=%2")
                                       .arg(okScan).arg(miss1043b.join(QLatin1Char(','))));
    });

    // ── P-t1051a 矿井轨支撑守卫腿（review0913 #1：悬空轨 == 0；t1043 干燥门的支撑伴随义务）──
    //    (a) 行为腿：27 seed × 128×128×64 真世界全量 worldgen → 全图扫 Rail，每轨核查「正下方格
    //        isTopFlushSupport」（完整立方或上半砖顶面齐平；与 t733 失撑坍落 / 门族放置同一单一
    //        权威谓词）。MC 口径：铁轨需下方支撑、无支撑不放置（悬空轨不存在）→ 违例恒 0 且轨
    //        总数 > 0（非空转）。机制归因（400 世界 sweep + worldgen 逐 pass 计数桩实测）：悬空轨
    //        实际产生者 = carveCanyon（后于 placeMineshaft 运行）掏空已铺轨地板格——seed 42/166/207
    //        各 3/2/2 根、修前红跑存证，三复现体 seed 已入本腿语料（发现力保障，非空转）；review0913
    //        #1 假设的同矿延迟落块形态在该语料未触发（门内过滤数 = 0）。修复双面 = placeMineshaft
    //        落块循环支撑门（处方式契约面 + 延迟落块防御）+ pruneUnsupportedWorldgenRails（t716 ③
    //        同款 carve 类后置守卫，摘无撑轨回「少一段轨」无害终态）。
    //    (b) 源钉：world.cpp 支撑门语句本体 + prune 守卫语句本体（剥注释 pinSet；t1043b 同款 root
    //        解析）。阴性轮摘 prune（守卫体 false&& 前缀）→ 悬空轨重现 → 行为半红 + 钉红 = 本腿独红。
    runLegMulti({ "t1051a mineshaft rail support guard (review0913 #1, companion duty of the t1043"
        " dry gate): across 27 freshfully-generated 128x128x64 worlds every worldgen rail rests on"
        " a top-flush support block (isTopFlushSupport on the cell directly below - the same"
        " single-authority predicate as the t733 unsupported-rail-fall and door placement"
        " semantics; MC caliber: rails require support beneath, a floating rail is never placed)"
        " with thousands of rails scanned so the leg cannot pass vacuously; the corpus embeds"
        " three reproducer seeds (42/166/207, attribution sweep measured 3/2/2 floating rails"
        " pre-fix) so the behavior half is mutation-sensitive; attribution pinned the producer on"
        " carveCanyon hollowing floor cells beneath already-placed rails (the deferred-placement"
        " form never fired on this corpus), so the fix is two-fold: the support gate in the"
        " placeMineshaft drop loop (prescribed contract face + deferred-placement defense) and"
        " the pruneUnsupportedWorldgenRails carve-following guard (t716 precedent) removing"
        " unsupported rails back to the harmless missing-segment end state; the pinSet half"
        " anchors both statement bodies in world.cpp (comment-immune) so the negative round"
        " (prune guard false&&-prefixed) reds this leg on both halvesdiag worlds=%1 rails=%2"
        " floating=%3 pins=%4" }, [&]() {
        bool ok = true;
        int worldsT1051a = 0;
        long railTotalT1051a = 0;
        int floatT1051a = 0;
        // 12 t1043b 同源 seed + 3 复现体（42/166/207，归因 sweep 实测修前 3/2/2 根悬空轨）+ 12 扩面 seed。
        const quint32 seedsT1051a[] = { 20260821u, 777u, 424242u, 1337u, 90210u, 4242u,
                                        2024u, 31337u, 7u, 99u, 12345u, 5150u,
                                        42u, 166u, 207u,
                                        606u, 8080u, 1999u, 20260915u, 555u, 31415u,
                                        2718u, 161u, 903u, 717425u, 8675309u, 404u };
        for (quint32 sd : seedsT1051a) {
            World wT1051a;
            wT1051a.setWidth(128);
            wT1051a.setDepth(128);
            wT1051a.setHeight(64);
            wT1051a.setSeed(int(sd)); // setter 内 generate() 全量 worldgen（含支撑门 + prune 守卫）
            ++worldsT1051a;
            for (int x = 0; x < 128; ++x)
                for (int z = 0; z < 128; ++z)
                    for (int y = 0; y < 64; ++y)
                        if (wT1051a.blockAt(x, y, z) == BR::Rail) {
                            ++railTotalT1051a;
                            if (!BR::isTopFlushSupport(wT1051a.blockAt(x, y - 1, z),
                                                       wT1051a.stateAt(x, y - 1, z)))
                                ++floatT1051a; // 悬空轨：正下方非齐平支撑（review0913 #1 形态）
                        }
        }
        const bool okScan = worldsT1051a == 27 && railTotalT1051a > 0 && floatT1051a == 0;
        if (!okScan)
            qInfo().noquote() << "  [t1051a diag] worlds" << worldsT1051a << "rails"
                              << railTotalT1051a << "floating" << floatT1051a;
        // (b) 双机制源钉（剥注释；t1043b 同款 root 解析）。
        const QString exeDirT1051a = QCoreApplication::applicationDirPath();
        const QString rootT1051a = QDir(exeDirT1051a + QStringLiteral("/..")).absolutePath();
        QStringList missT1051a;
        missT1051a << pinSet(rootT1051a + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-support-gate",
             "if (!BlockRegistry::isTopFlushSupport(m_chunks.blockAt(rx, ry - 1, rz),"
             " m_chunks.stateAt(rx, ry - 1, rz))) continue;"},
            {"cpp-support-prune",
             "if (BlockRegistry::isTopFlushSupport(m_chunks.blockAt(x, y - 1, z),"
             " m_chunks.stateAt(x, y - 1, z))) continue;"},
            {"cpp-support-prune-call", "pruneUnsupportedWorldgenRails();"},
        });
        const bool okPins = missT1051a.isEmpty();
        if (!okPins)
            qInfo().noquote() << "  [t1051a diag pins] miss=" << missT1051a.join(QLatin1Char(','));
        ok = okScan && okPins;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1051a mineshaft rail support guard (review0913 #1, companion duty"
                             " of the t1043 dry gate): across 27 freshfully-generated 128x128x64"
                             " worlds every worldgen rail rests on a top-flush support block"
                             " (isTopFlushSupport on the cell directly below - the same"
                             " single-authority predicate as the t733 unsupported-rail-fall and"
                             " door placement semantics; MC caliber: rails require support"
                             " beneath, a floating rail is never placed) with thousands of rails"
                             " scanned so the leg cannot pass vacuously; the corpus embeds three"
                             " reproducer seeds (42/166/207, attribution sweep measured 3/2/2"
                             " floating rails pre-fix) so the behavior half is mutation-sensitive;"
                             " attribution pinned the producer on carveCanyon hollowing floor"
                             " cells beneath already-placed rails (the deferred-placement form"
                             " never fired on this corpus), so the fix is two-fold: the support"
                             " gate in the placeMineshaft drop loop (prescribed contract face +"
                             " deferred-placement defense) and the pruneUnsupportedWorldgenRails"
                             " carve-following guard (t716 precedent) removing unsupported rails"
                             " back to the harmless missing-segment end state; the pinSet half"
                             " anchors both statement bodies in world.cpp (comment-immune) so the"
                             " negative round (prune guard false&&-prefixed) reds this leg on"
                             " both halves"
                          << (ok ? QString()
                                 : QStringLiteral("diag scan=%1 pins=%2")
                                       .arg(okScan).arg(missT1051a.join(QLatin1Char(','))));
    });

    // ── P-t1044a 蜘蛛爬墙（R19.23 t1044；MC 原版口径：蜘蛛沿实体方块面垂直爬墙，parity-ledger 裁-2）──
    //    场景：平地平台 + 2 高 1 宽墙（(20,85..86,23) 顶面 87.0）阻在蜘蛛(20.5,85.3,21.5)与玩家
    //    (20.5,85,26) 之间（纯 +Z 追击线）。蜘蛛追击水平位移被墙挡死（逐轴撤回）→ aiSpiderWallClimb
    //    贴面攀爬脉冲（vy=kSpiderClimbSpeed 名义 2.4 b/s，AI tick 4 帧窗内重力衰减后 vy 恒正不触发
    //    落地扫描回弹 → 净爬升 ≈1.2 b/s；固定 dt=0.016 细步泵，t1033/t1037 dt 语义先例）。
    //    (a1) 净爬升：全程 maxY ≥ 87.0（中心过墙顶带 = feet ≥ 86.7；修复前蜘蛛封顶 85.3——2 高墙
    //         越障跳不触发（墙顶两格非净空）→ 无爬墙机制则永卡墙根）；
    //    (a2) 爬升先于越檐：首次中心 ≥ 86.6（爬升中段）严格早于首次 z > 22.9（越檐；墙根卡位上限
    //         ≈22.74）→ 垂直位移是越檐的前因，非水平绕过；
    //    (a3) 越檐抵达：终点与玩家 XZ 距 ≤ 1.2（修复前卡墙根距离 ≈3.45）→ MC 蜘蛛爬到顶后沿檐
    //         平移（登记简化：走既有追击水平移动）直达目标。
    //    阴性轮敏感（摘 aiSpiderWallClimb isClimber 门 false && 前缀）：(a1)(a2)(a3) 恰红（蜘蛛零
    //    爬升、永卡墙根）；蜘蛛家族能力门见 P-t1044b 对照腿。
    runLegMulti({ "t1044a spider climbs a wall to reach its target (MC caliber, parity adjudication cai-2): chasing"
        " a player separated by a 2-high 1-wide wall the blocked chase horizontal move latches a face cli"
        "mb (vy pulse at the registered kSpiderClimbSpeed caliber, gravity-decay kept positive so the lan"
        "ding scan never snaps back), the spider rises past the wall top band (max center Y >= 87.0 from "
        "a rest of 85.3 - a 2-high wall cannot be jump-cleared so a pre-fix spider is wall-bound forever)"
        ", the mid-climb band precedes the top crossing (climb is the cause of the traversal, not a horiz"
        "ontal detour), and it then walks over the edge (registered simplification: edge traverse is the "
        "plain chase move, no separate mantling) to end within 1.2 blocks of the player (negative-round s"
        "ensitive: the isClimber gate false&&-prefixed)diag a1=%1 a2=%2 a3=%3 spider=%4" }, [&]() {
        World wa;
        wa.setWidth(44); wa.setDepth(44); wa.setHeight(96); wa.setSeed(1044);
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wa.setBlock(x, 84, z, BR::Stone, 0);
        wa.setBlock(20, 85, 23, BR::Stone, 0); // 2 高 1 宽墙（顶面 87.0；越障跳对 2 高墙不触发）
        wa.setBlock(20, 86, 23, BR::Stone, 0);
        EntityManager ema;
        const int spider = ema.spawnMobTyped(20, 85, 21, EntityManager::MobSpider,
                                             QStringLiteral("#1e1e1e"), 10);
        const QVector3D pp(20.5f, 85.0f, 26.0f); // 玩家脚位（墙另一侧，纯 +Z 追击线）
        bool a1 = false, a2 = false, a3 = false;
        if (spider >= 0) {
            float maxY = -1e9f;
            int firstY = -1, firstCross = -1;
            QVector3D p;
            for (int t = 0; t < 480; ++t) { // 7.68s（爬升 ≈1.6s + 逼近 + 越檐 + 抵达余量）
                ema.tick(0.016f, &wa, pp, 0.3f, 1.8f, true);
                p = ema.posAt(spider);
                if (p.y() > maxY) maxY = p.y();
                if (firstY < 0 && p.y() >= 86.6f) firstY = t;       // 爬升中段带（墙 85..86 上部）
                if (firstCross < 0 && p.z() > 22.9f) firstCross = t; // 越檐（墙根卡位上限 ≈22.74）
            }
            const float endDist = QVector3D(p.x() - pp.x(), 0.0f, p.z() - pp.z()).length();
            a1 = maxY >= 87.0f;
            a2 = firstY >= 0 && firstCross >= 0 && firstY < firstCross;
            a3 = endDist <= 1.2f;
            if (!(a1 && a2 && a3))
                qInfo().noquote() << "  [t1044a diag] maxY=" << maxY << "firstY=" << firstY
                                  << "firstCross=" << firstCross << "endDist=" << endDist
                                  << "end=(" << p.x() << "," << p.y() << "," << p.z() << ")";
        }
        const bool ok = spider >= 0 && a1 && a2 && a3;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1044a spider climbs a wall to reach its target (MC caliber, "
                             "parity adjudication cai-2): chasing a player separated by a 2-high "
                             "1-wide wall the blocked chase horizontal move latches a face climb "
                             "(vy pulse at the registered kSpiderClimbSpeed caliber, gravity-decay "
                             "kept positive so the landing scan never snaps back), the spider rises "
                             "past the wall top band (max center Y >= 87.0 from a rest of 85.3 - a "
                             "2-high wall cannot be jump-cleared so a pre-fix spider is wall-bound "
                             "forever), the mid-climb band precedes the top crossing (climb is the "
                             "cause of the traversal, not a horizontal detour), and it then walks "
                             "over the edge (registered simplification: edge traverse is the plain "
                             "chase move, no separate mantling) to end within 1.2 blocks of the "
                             "player (negative-round sensitive: the isClimber gate false&&-prefixed)"
                          << (ok ? QString()
                                 : QStringLiteral("diag a1=%1 a2=%2 a3=%3 spider=%4")
                                       .arg(a1).arg(a2).arg(a3).arg(spider));
    });

    // ── P-t1044b 能力门对照 + 接线源钉（R19.23 t1044）──
    //    (b1) 洞穴蜘蛛（MobCaveSpider，t1012③ 同族）同场景爬墙越檐：MC cave spider 同样爬墙 →
    //         纳入同批（登记）。maxY ≥ 87.0 + 终点距玩家 ≤ 1.2（同 a1/a3 判据）。
    //    (b2) 非蜘蛛敌对对照（MobShambler = 僵尸位）：同场景（2 高墙阻路）不爬升——maxY ≤ 86.0
    //         （rest 中心 85.9；2 高墙不可越障跳 → 修复前后都封顶墙根）且 z < 23.0（永不越檐，
    //         卡位 ≈22.44）→ 能力门把爬墙限定在 Spider 家族。
    //    (b3) 接线源钉（剥注释 pinSet，t1043b 同款；追击链三条分支中仇恨狼 / 铁傀儡两分支 headless
    //         无对照腿 → 结构钉补位，t1047 O-4 先例）：爬墙调用 ×3（三条追击分支）+ isClimber 门 +
    //         攀爬脉冲语句 + header 声明。
    //    阴性轮敏感：摘 isClimber 门 → (b1) 恰红；(b2) 保绿（Shambler 不依赖爬墙分支）；(b3) 钉
    //    needle 仍在（钉接线非钉门真值）。
    runLegMulti({ "t1044b climb capability gate and wiring pins (MC caliber): the cave spider (same spider family, "
        "t1012) climbs the same test wall and reaches its target exactly like the spider (MC cave spiders"
        " climb too - included in this batch, registered), while a non-spider hostile (the shambler, our "
        "zombie) in the identical blocked-chase scenario never leaves the wall-foot band (max Y <= 86.0 a"
        "t its 85.9 rest, never crosses the wall plane) - the climber gate limits wall climbing to the sp"
        "ider family; the pinSet half anchors the climb call in all three chase branches (aggro wolf / ir"
        "on golem / player, count>=3), the climber gate, the climb pulse statement and the header declara"
        "tion (comment-immune, t1047 O-4 precedent for headless-unreachable branches)diag b1=%1(maxY=%2) "
        "b2=%3(maxY=%4 endZ=%5) pins=%6" }, [&]() {
        bool okB1 = false, okB2 = false;
        QVector3D endB1;
        float maxYB1 = -1e9f, maxYB2 = -1e9f, endZB2 = -1e9f;
        { // (b1) 洞穴蜘蛛爬墙。
            World wb1;
            wb1.setWidth(44); wb1.setDepth(44); wb1.setHeight(96); wb1.setSeed(1045);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wb1.setBlock(x, 84, z, BR::Stone, 0);
            wb1.setBlock(24, 85, 23, BR::Stone, 0);
            wb1.setBlock(24, 86, 23, BR::Stone, 0);
            EntityManager emb1;
            const int cspider = emb1.spawnMobTyped(24, 85, 21, EntityManager::MobCaveSpider,
                                                   QStringLiteral("#223344"), 10);
            const QVector3D pp(24.5f, 85.0f, 26.0f);
            if (cspider >= 0) {
                for (int t = 0; t < 480; ++t) {
                    emb1.tick(0.016f, &wb1, pp, 0.3f, 1.8f, true);
                    endB1 = emb1.posAt(cspider);
                    if (endB1.y() > maxYB1) maxYB1 = endB1.y();
                }
                const float endDist = QVector3D(endB1.x() - pp.x(), 0.0f, endB1.z() - pp.z()).length();
                okB1 = maxYB1 >= 87.0f && endDist <= 1.2f;
            }
        }
        { // (b2) 僵尸位对照：同场景不爬升。
            World wb2;
            wb2.setWidth(44); wb2.setDepth(44); wb2.setHeight(96); wb2.setSeed(1046);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wb2.setBlock(x, 84, z, BR::Stone, 0);
            wb2.setBlock(20, 85, 23, BR::Stone, 0);
            wb2.setBlock(20, 86, 23, BR::Stone, 0);
            EntityManager emb2;
            const int zombie = emb2.spawnMobTyped(20, 85, 21, EntityManager::MobShambler,
                                                  QStringLiteral("#2e5a2e"), 10);
            const QVector3D pp(20.5f, 85.0f, 26.0f);
            if (zombie >= 0) {
                QVector3D p;
                for (int t = 0; t < 300; ++t) { // 4.8s（走到墙根 ~1.5s 后长卡）
                    emb2.tick(0.016f, &wb2, pp, 0.3f, 1.8f, true);
                    p = emb2.posAt(zombie);
                    if (p.y() > maxYB2) maxYB2 = p.y();
                }
                endZB2 = p.z();
                okB2 = maxYB2 <= 86.0f && endZB2 < 23.0f;
            }
        }
        // (b3) 接线源钉（剥注释；调用 needle 出现恰 3 处 = 三条追击分支全接线）。
        const QString exeDir1044 = QCoreApplication::applicationDirPath();
        const QString root1044 = QDir(exeDir1044 + QStringLiteral("/..")).absolutePath();
        QStringList miss1044;
        miss1044 << pinSet(root1044 + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-climb-call-x3", "aiSpiderWallClimb(e, world, blockedX, nx, blockedZ, nz)", 3},
            {"cpp-climber-gate", "(e.mobType == MobSpider) || (e.mobType == MobCaveSpider)"},
            {"cpp-climb-pulse", "e.vy = kSpiderClimbSpeed;"},
        });
        miss1044 << pinSet(root1044 + QStringLiteral("/src/Entities/entitymanager.h"), {
            {"hdr-climb-decl",
             "bool aiSpiderWallClimb(Entity &e, World *world, bool blockedX, float nx, bool blockedZ, float nz);"},
        });
        const bool okPins = miss1044.isEmpty();
        if (!okPins)
            qInfo().noquote() << "  [t1044b diag pins] miss=" << miss1044.join(QLatin1Char(','));
        if (!okB1)
            qInfo().noquote() << "  [t1044b diag b1] maxY=" << maxYB1 << "end=("
                              << endB1.x() << "," << endB1.y() << "," << endB1.z() << ")";
        if (!okB2)
            qInfo().noquote() << "  [t1044b diag b2] maxY=" << maxYB2 << "endZ=" << endZB2;
        const bool ok = okB1 && okB2 && okPins;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1044b climb capability gate and wiring pins (MC caliber): the "
                             "cave spider (same spider family, t1012) climbs the same test wall "
                             "and reaches its target exactly like the spider (MC cave spiders "
                             "climb too - included in this batch, registered), while a non-spider "
                             "hostile (the shambler, our zombie) in the identical blocked-chase "
                             "scenario never leaves the wall-foot band (max Y <= 86.0 at its "
                             "85.9 rest, never crosses the wall plane) - the climber gate limits "
                             "wall climbing to the spider family; the pinSet half anchors the "
                             "climb call in all three chase branches (aggro wolf / iron golem / "
                             "player, count>=3), the climber gate, the climb pulse statement and "
                             "the header declaration (comment-immune, t1047 O-4 precedent for "
                             "headless-unreachable branches)"
                          << (ok ? QString()
                                 : QStringLiteral("diag b1=%1(maxY=%2) b2=%3(maxY=%4 endZ=%5) pins=%6")
                                       .arg(okB1).arg(maxYB1).arg(okB2).arg(maxYB2).arg(endZB2)
                                       .arg(miss1044.join(QLatin1Char(','))));
    });

    // ── P-t1045a 玩家踩踏耕地概率化 + 弹苗（R19.23 t1045；parity-ledger 裁-3，MC 原版口径）──
    //    MC Java onFallenUpon 公式 P = clamp(fall − 0.5, 0, 1)（wiki Farmland/Trampling 引证：
    //    「jumps/falls on the block (with chance equal to distance fallen - 0.5)」）。真玩家物理链
    //    （loadSavedState 置空 → tick 重力下落 → 着地沿），落差 ~1.26（跳高带，P≈0.76 < 1：
    //    缝两端都可判）；World::setTrampleRollOverride 千分比缝钉两端（t1031 setTameRollOverride
    //    先例，生产零调用、钉住时不消费 RNG → 同值恒同果零漂移）：
    //    (a1) 缝=0（必踩）：湿耕地（state=FarmlandHydrationMax）+ 上方 state0 小麦苗 → 回 Dirt
    //         且 state==0（湿润态随 id 消失）+ 苗被清（Air）+ 弹落恰 1×SeedId×1（dropCropDrops
    //         t1026 单一权威，未熟恒 1 种子=确定性）；
    //    (a2) 缝=999（概率带内必不踩，须低落差带）：干耕地原样 Farmland、零掉落。
    //    阴性轮敏感：摘 World::farmlandTrampleRoll 本体（false&& 前缀）→ (a1) 恰红（踩踏腿+弹苗腿）。
    runLegMulti({ "t1045a player trampling farmland is probabilistic with crop pop (MC caliber, parity adjudication"
        " cai-3): a real-physics landing from the jump-height band (fall ~1.26, P = fall - 0.5 ~ 0.76) wi"
        "th the roll seam pinned low reverts the hydrated farmland to dirt with the moisture state gone a"
        "nd pops the young wheat crop as exactly one seed via the dropCropDrops single authority, while t"
        "he same fall with the seam pinned high keeps the dry farmland intact with zero drops (negative-r"
        "ound sensitive: the trample roll body false&&-prefixed)diag hit=%1 crop=%2 no=%3" }, [&]() {
        World wa;
        wa.setWidth(44); wa.setDepth(44); wa.setHeight(96); wa.setSeed(1045);
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wa.setBlock(x, 84, z, BR::Stone, 0);
        wa.setBlock(10, 84, 10, BR::Farmland, BR::FarmlandHydrationMax); // (a1) 湿耕地
        wa.setBlock(10, 85, 10, BR::WheatCrop, 0);                       // state0 苗（弹落恒 1 种子）
        wa.setBlock(20, 84, 10, BR::Farmland, 0);                        // (a2) 干耕地
        PlayerController pcF45;
        pcF45.setWorld(&wa);
        QVector<int> dropId45, dropCnt45;
        const QMetaObject::Connection dropConn45 = QObject::connect(
            &pcF45, &PlayerController::spawnItem, &pcF45,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropId45.push_back(id);
                dropCnt45.push_back(count);
            });
        const auto pumpFor45 = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const auto tickP45 = [&](int n) {
            for (int i = 0; i < n; ++i) { pumpFor45(17); pcF45.tick(); }
        };
        // (a1) 缝=0 必踩：落差 ≈1.26（86.2 → 耕地顶 84.9375）。
        wa.setTrampleRollOverride(0);
        pcF45.loadSavedState(10.5f, 86.2f, 10.5f, -90.0f, 0.0f, 2);
        for (int t = 0; t < 60 && wa.blockAt(10, 84, 10) == BR::Farmland; ++t) tickP45(1);
        bool hitOk = wa.blockAt(10, 84, 10) == BR::Dirt
            && wa.stateAt(10, 84, 10) == 0 // 湿润 state 随 id 消失（t1045 探针④踩踏半面）
            && wa.blockAt(10, 85, 10) == BR::Air;
        bool cropOk = dropId45.size() == 1 && dropId45[0] == RecipeRegistry::SeedId
            && dropCnt45[0] == 1;
        // (a2) 缝=999 必不踩：同落差带（P≈0.76，样本 0.999 ≥ P）。
        wa.setTrampleRollOverride(999);
        dropId45.clear();
        dropCnt45.clear();
        pcF45.loadSavedState(20.5f, 86.2f, 10.5f, -90.0f, 0.0f, 2);
        tickP45(30);
        bool noOk = wa.blockAt(20, 84, 10) == BR::Farmland && dropId45.isEmpty();
        wa.setTrampleRollOverride(-1); // 缝复位（生产零残留）
        QObject::disconnect(dropConn45);
        const bool ok = hitOk && cropOk && noOk;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t1045a diag] hitOk=" << hitOk << "cropOk=" << cropOk
                              << "noOk=" << noOk
                              << " a=" << int(wa.blockAt(10, 84, 10)) << "stateA"
                              << wa.stateAt(10, 84, 10) << "crop" << int(wa.blockAt(10, 85, 10))
                              << "b=" << int(wa.blockAt(20, 84, 10)) << "drops" << dropId45.size();
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1045a player trampling farmland is probabilistic with crop pop "
                             "(MC caliber, parity adjudication cai-3): a real-physics landing from "
                             "the jump-height band (fall ~1.26, P = fall - 0.5 ~ 0.76) with the "
                             "roll seam pinned low reverts the hydrated farmland to dirt with the "
                             "moisture state gone and pops the young wheat crop as exactly one "
                             "seed via the dropCropDrops single authority, while the same fall "
                             "with the seam pinned high keeps the dry farmland intact with zero "
                             "drops (negative-round sensitive: the trample roll body false&&-prefixed)"
                          << (ok ? QString()
                                 : QStringLiteral("diag hit=%1 crop=%2 no=%3")
                                       .arg(hitOk).arg(cropOk).arg(noOk));
    });

    // ── P-t1045b mob 落地踩踏 + 接线源钉（R19.23 t1045）──
    //    (b1) 蜘蛛落到湿耕地（落差 ≈1.06，P≈0.56，缝=0 必踩）→ 回 Dirt + 经
    //         farmlandTrampledByMob → PlayerController::onMobTrampledFarmland 清苗 + dropCropDrops
    //         弹落恰 1×SeedId（信号直连 Game 层，headless 可行为级断言）。
    //    (b2) 概率地板对照：缝仍=0，蜘蛛仅 0.06 微落差落地 → fall ≤ 0.5 地板内恒不踩（resting
    //         行走面同口径短路）→ 干耕地原样。
    //    (b3) 接线源钉（剥注释 pinSet，t1044b 同款）：掷骰本体/缝消费/玩家分支/mob 分支/回土/发信/
    //         信号与槽声明/水冲两调用点/转换应用/静水源门。
    //    阴性轮敏感：摘掷骰本体 → (b1) 恰红（(b2) 地板对照保绿语义不变）；钉 needle 变体注——掷骰
    //    本体被摘时该 needle 同步失配（病灶自身 pin 面，t1043 先例），与 (b1) 同腿计红不另增。
    runLegMulti({ "t1045b mob landing tramples farmland with crop pop and wiring pins (MC caliber, parity adjudicat"
        "ion cai-3): a spider landing on hydrated farmland from the P~0.56 band with the shared roll seam"
        " pinned low reverts it to dirt and pops the young crop as exactly one seed through the farmlandT"
        "rampledByMob relay into the dropCropDrops single authority (MC onFallenUpon applies to all entit"
        "ies; the modern 0.512 size exemption and mobGriefing gate are not carried - Beta/1.0 baseline ha"
        "s no size gate and the project has no gamerule system, registered), while a 0.06-block step land"
        "ing stays below the formula floor and never tramples even with the seam pinned low; the pinSet h"
        "alf anchors the roll body, seam consumption, the player branch, the mob branch, the silent dirt "
        "revert, the signal emit and declarations, plus both wash call sites, the conversion apply and th"
        "e still-source gate (comment-immune)diag mob=%1 floor=%2 pins=%3" }, [&]() {
        World wb;
        wb.setWidth(44); wb.setDepth(44); wb.setHeight(96); wb.setSeed(1046);
        for (int x = 2; x < 42; ++x)
            for (int z = 2; z < 42; ++z) wb.setBlock(x, 84, z, BR::Stone, 0);
        wb.setBlock(10, 84, 12, BR::Farmland, BR::FarmlandHydrationMax); // (b1) 湿耕地
        wb.setBlock(10, 85, 12, BR::WheatCrop, 0);                       // state0 苗
        wb.setBlock(16, 84, 12, BR::Farmland, 0);                        // (b2) 干耕地
        EntityManager emb;
        PlayerController pcb45;
        pcb45.setWorld(&wb);
        pcb45.setEntityManager(&emb); // t1045 直连在 setEntityManager 内建立（生产同路）
        QVector<int> dropIdB, dropCntB;
        const QMetaObject::Connection dropConnB = QObject::connect(
            &pcb45, &PlayerController::spawnItem, &pcb45,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIdB.push_back(id);
                dropCntB.push_back(count);
            });
        const QVector3D farB(-1000.0f, 10.0f, -1000.0f);
        emb.setWanderFrozen(true); // t1029 缝：冻结游荡（下落期零水平漂移，落点列恒定）
        wb.setTrampleRollOverride(0);
        // (b1) 蜘蛛自 86.3 中心落到耕地顶 84.9375（落差 ≈1.06 → P≈0.56，缝 0 → 必踩）。
        const int spiderB = emb.spawnMobTyped(10, 86, 12, EntityManager::MobSpider,
                                              QStringLiteral("#1e1e1e"), 10);
        bool mobOk = spiderB >= 0;
        if (spiderB >= 0) {
            for (int t = 0; t < 120 && wb.blockAt(10, 84, 12) == BR::Farmland; ++t)
                emb.tick(0.016f, &wb, farB, 0.3f, 1.8f, true);
            mobOk = mobOk && wb.blockAt(10, 84, 12) == BR::Dirt
                && wb.blockAt(10, 85, 12) == BR::Air
                && dropIdB.size() == 1 && dropIdB[0] == RecipeRegistry::SeedId && dropCntB[0] == 1;
        }
        // (b2) 概率地板对照：缝仍 0，0.06 微落差（85.3 中心 → 顶 84.9375）→ 恒不踩。
        const int spiderB2 = emb.spawnMobTyped(16, 85, 12, EntityManager::MobSpider,
                                               QStringLiteral("#222222"), 10);
        bool floorOk = spiderB2 >= 0;
        if (spiderB2 >= 0) {
            for (int t = 0; t < 90; ++t) emb.tick(0.016f, &wb, farB, 0.3f, 1.8f, true);
            floorOk = floorOk && wb.blockAt(16, 84, 12) == BR::Farmland;
        }
        emb.setWanderFrozen(false);
        wb.setTrampleRollOverride(-1);
        QObject::disconnect(dropConnB);
        // (b3) 接线源钉（剥注释；两 call 点 / 应用 / 静水源门 / 掷骰与缝 / 两分支 / 回土 / 发信 / 声明）。
        const QString exeDir1045 = QCoreApplication::applicationDirPath();
        const QString root1045 = QDir(exeDir1045 + QStringLiteral("/..")).absolutePath();
        QStringList miss1045;
        miss1045 << pinSet(root1045 + QStringLiteral("/src/World/world.cpp"), {
            {"cpp-roll-body", "return sample < p;"},
            {"cpp-roll-seam", "double(m_trampleRollOverride % 1000) / 1000.0"},
            {"cpp-wash-fall-call", "tryWashFarmland(c.x, c.y - 1, c.z, c.level)"},
            {"cpp-wash-side-call", "tryWashFarmland(nx, c.y, nz, c.level)"},
            {"cpp-wash-apply", "setWaterSilent(fw.x, fw.y, fw.z, BlockRegistry::Dirt, 0)"},
            {"cpp-wash-src-gate", "if (srcLevel == 0) return false;"},
        });
        miss1045 << pinSet(root1045 + QStringLiteral("/src/World/world.h"), {
            {"hdr-roll-decl", "bool farmlandTrampleRoll(float fallDistance);"},
            {"hdr-seam-decl", "Q_INVOKABLE void setTrampleRollOverride(int roll);"},
        });
        miss1045 << pinSet(root1045 + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-player-trample", "m_world->farmlandTrampleRoll(fall)"},
            {"cpp-relay-connect", "&EntityManager::farmlandTrampledByMob", 2},
        });
        miss1045 << pinSet(root1045 + QStringLiteral("/src/Game/playercontroller.h"), {
            {"hdr-slot-decl", "void onMobTrampledFarmland(int x, int y, int z);"},
        });
        miss1045 << pinSet(root1045 + QStringLiteral("/src/Entities/entitymanager.cpp"), {
            {"cpp-mob-trample", "world->farmlandTrampleRoll(fallDist)"},
            {"cpp-mob-revert", "world->setBlockSilent(cx, restCellY, cz, BlockRegistry::Dirt, 0);"},
            {"cpp-mob-signal", "emit farmlandTrampledByMob(cx, restCellY, cz);"},
        });
        miss1045 << pinSet(root1045 + QStringLiteral("/src/Entities/entitymanager.h"), {
            {"hdr-signal-decl", "void farmlandTrampledByMob(int x, int y, int z);"},
        });
        const bool okPins45 = miss1045.isEmpty();
        if (!okPins45)
            qInfo().noquote() << "  [t1045b diag pins] miss=" << miss1045.join(QLatin1Char(','));
        const bool ok = mobOk && floorOk && okPins45;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t1045b diag] mobOk=" << mobOk << "floorOk=" << floorOk
                              << " a=" << int(wb.blockAt(10, 84, 12)) << "crop"
                              << int(wb.blockAt(10, 85, 12)) << "b=" << int(wb.blockAt(16, 84, 12))
                              << "drops" << dropIdB.size();
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1045b mob landing tramples farmland with crop pop and wiring "
                             "pins (MC caliber, parity adjudication cai-3): a spider landing on "
                             "hydrated farmland from the P~0.56 band with the shared roll seam "
                             "pinned low reverts it to dirt and pops the young crop as exactly "
                             "one seed through the farmlandTrampledByMob relay into the "
                             "dropCropDrops single authority (MC onFallenUpon applies to all "
                             "entities; the modern 0.512 size exemption and mobGriefing gate are "
                             "not carried - Beta/1.0 baseline has no size gate and the project "
                             "has no gamerule system, registered), while a 0.06-block step "
                             "landing stays below the formula floor and never tramples even "
                             "with the seam pinned low; the pinSet half anchors the roll body, "
                             "seam consumption, the player branch, the mob branch, the silent "
                             "dirt revert, the signal emit and declarations, plus both wash "
                             "call sites, the conversion apply and the still-source gate "
                             "(comment-immune)"
                          << (ok ? QString()
                                 : QStringLiteral("diag mob=%1 floor=%2 pins=%3")
                                       .arg(mobOk).arg(floorOk)
                                       .arg(miss1045.join(QLatin1Char(','))));
    });

    // ── P-t1045c 流水冲耕转换（R19.23 t1045；裁-3 定案：流水冲耕地=变回泥土，方块转换非掉落物）──
    //    (c1) 流水（state>0）grounded 蔓延落点=湿耕地 → 转 Dirt（state 清零、水**不**入格——Dirt
    //         挡水）+ 全程零 blockDroppedAsItem（与附着块冲毁掉落分型）；
    //    (c2) 干耕地同退化（t1045 探针④：干/湿都退化）；
    //    (c3) 静水源（level 0）直接邻接 → 不冲（hydration 基建面选型登记：耕地依水而建不受静水
    //         接触破坏；维基 Java/Bedrock 均无流水毁耕条目，本腿按裁-3 定案口径实现并台账注记）。
    //    阴性轮敏感：摘水冲两调用点（false&& 前缀）→ (c1)(c2) 恰红（(c3) 保绿——门反摘会误伤）。
    runLegMulti({ "t1045c flowing water washes farmland back to dirt as a block conversion (parity adjudication cai"
        "-3): flowing spread onto a hydrated farmland reverts it to dirt with the moisture state cleared "
        "and the water never enters the cell (dirt blocks flow - a conversion, not a drop: zero blockDrop"
        "pedAsItem across the whole run, distinct from the attachable wash family), the dry farmland lane"
        " degrades identically (both hydration states degrade), and a still source (level 0) directly adj"
        "acent never washes its farmland (hydration infrastructure choice: farms are built against still "
        "water; flowing-only contact, registered; negative-round sensitive: both wash call sites false&&-"
        "prefixed)diag wet=%1 dry=%2 noDrop=%3 src=%4" }, [&]() {
        bool wetOk = false, dryOk = false, noDrop = true, srcOk = false;
        {
            World wc;
            wc.setWidth(44); wc.setDepth(44); wc.setHeight(96); wc.setSeed(1047);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wc.setBlock(x, 83, z, BR::Stone, 0); // 地板 y=83（水面/耕地同道 y=84 grounded）
            wc.setBlock(12, 84, 8, BR::Farmland, BR::FarmlandHydrationMax);  // (c1) 湿耕地
            wc.setBlock(12, 84, 30, BR::Farmland, 0);                        // (c2) 干耕地（相距 22 > 扩散 7）
            int dropCnt45c = 0;
            const QMetaObject::Connection dropConnC = QObject::connect(
                &wc, &World::blockDroppedAsItem, &wc,
                [&dropCnt45c](int, int, int, int) { ++dropCnt45c; });
            wc.setBlock(10, 84, 8, BR::Water, 0);   // 桶倒源（west 2 格；下方地板 83 → grounded 蔓延）
            wc.setBlock(10, 84, 30, BR::Water, 0);  // 干耕地同道
            for (int t = 0; t < 15; ++t) wc.tickWaterFlow(); // 节流 3:1 → ~5 波前步（2 步需 + 余量）
            wetOk = wc.blockAt(12, 84, 8) == BR::Dirt && wc.stateAt(12, 84, 8) == 0
                && wc.blockAt(12, 84, 8) != BR::Water; // 转换非掉落：水不入格
            dryOk = wc.blockAt(12, 84, 30) == BR::Dirt;
            noDrop = dropCnt45c == 0; // 非掉落物：全程零 blockDroppedAsItem
            QObject::disconnect(dropConnC);
        }
        {
            World wd; // (c3) 静水源邻接豁免（土堤围死唯一接触面）
            wd.setWidth(44); wd.setDepth(44); wd.setHeight(96); wd.setSeed(1048);
            for (int x = 2; x < 42; ++x)
                for (int z = 2; z < 42; ++z) wd.setBlock(x, 83, z, BR::Stone, 0); // 地板 y=83
            wd.setBlock(12, 84, 18, BR::Farmland, 0); // 耕地（东邻静源）
            wd.setBlock(13, 84, 18, BR::Water, 0);    // 静水源直接邻接
            for (int x = 11; x <= 14; ++x) {          // 土堤：z=17/19 两行 + 西/东柱
                wd.setBlock(x, 84, 17, BR::Stone, 0);
                wd.setBlock(x, 84, 19, BR::Stone, 0);
            }
            wd.setBlock(11, 84, 18, BR::Stone, 0);
            wd.setBlock(14, 84, 18, BR::Stone, 0);
            for (int t = 0; t < 15; ++t) wd.tickWaterFlow();
            srcOk = wd.blockAt(12, 84, 18) == BR::Farmland; // 静水接触不冲
        }
        const bool ok = wetOk && dryOk && noDrop && srcOk;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t1045c diag] wetOk=" << wetOk << "dryOk=" << dryOk
                              << "noDrop=" << noDrop << "srcOk=" << srcOk;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1045c flowing water washes farmland back to dirt as a block "
                             "conversion (parity adjudication cai-3): flowing spread onto a "
                             "hydrated farmland reverts it to dirt with the moisture state "
                             "cleared and the water never enters the cell (dirt blocks flow - "
                             "a conversion, not a drop: zero blockDroppedAsItem across the "
                             "whole run, distinct from the attachable wash family), the dry "
                             "farmland lane degrades identically (both hydration states "
                             "degrade), and a still source (level 0) directly adjacent never "
                             "washes its farmland (hydration infrastructure choice: farms are "
                             "built against still water; flowing-only contact, registered; "
                             "negative-round sensitive: both wash call sites false&&-prefixed)"
                          << (ok ? QString()
                                 : QStringLiteral("diag wet=%1 dry=%2 noDrop=%3 src=%4")
                                       .arg(wetOk).arg(dryOk).arg(noDrop).arg(srcOk));
    });

    // ── P-t1046a 拉杆 / 按钮 sneakPlace 旁路（R19.23 t1046 低-3；t1034 门同式）──
    //    两向四腿：(1) 潜行持方块右键拉杆 = 放置落命中面邻格（拉杆 bit0 保持 0——use 被旁路）；
    //    (2) 非潜行持方块右键拉杆 = 激活照常（bit0 翻 1、无放置）；(3)(4) 木按钮同两向（按下 bit0=1）。
    //    阴性轮敏感：摘机关分支 sneakPlaceBlock 门（t1050 起判据名）→ 潜行两腿红（潜行右键仍扳动 + 无放置）+ cpp-mech-sneak-gate
    //    钉红；非潜行对照腿不受门影响保绿。
    runLegMulti({ "t1046a lever/button sneakPlace bypass: sneaking with a held block and right-clicking a floor lev"
        "er (or a wood button) places the held block on the hit face's neighbor cell while the mechanism "
        "state bit stays clear (activation bypassed, MC sneak-use caliber, parity low-3); without sneak t"
        "he right-click still activates the lever (bit0 set) and presses the button (bit0 set, recovery a"
        "rmed) and consumes the click so nothing is placed (negative-round sensitive: mech-branch sneakPl"
        "ace gate removal)diag sneakLv=%1 useLv=%2 sneakBtn=%3 useBtn=%4 pins=%5" }, [&]() {
        World wT46a;
        wT46a.setWidth(48); wT46a.setDepth(48); wT46a.setHeight(96); wT46a.setSeed(10461);
        Hotbar hbT46a;
        PlayerController pcT46a;
        pcT46a.setWorld(&wT46a);
        pcT46a.setHotbar(&hbT46a);
        QQuickWindow winT46a;
        pcT46a.setParentItem(winT46a.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空；拉杆 A/B（z=16/20，贴地 state0）与按钮 A/B（z=24/28，
        //     贴地扁薄盒 state0）各四件——潜行腿与 use 腿分件，免放置遮挡串扰。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 30; ++z) {
                for (int y = 15; y <= 20; ++y) wT46a.setBlock(x, y, z, BR::Air, 0);
                wT46a.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT46a.setBlock(12, 15, 16, BR::Lever, quint8(0));       // 拉杆 A（潜行腿）
        wT46a.setBlock(12, 15, 20, BR::Lever, quint8(0));       // 拉杆 B（use 腿）
        wT46a.setBlock(12, 15, 24, BR::WoodButton, quint8(0));  // 按钮 A（潜行腿）
        wT46a.setBlock(12, 15, 28, BR::WoodButton, quint8(0));  // 按钮 B（use 腿）
        // 瞄准帮手（t1034a 同款）：自 +X 侧瞄准机关格（拉杆瞄贴地基座顶面 y15.09 / 按钮瞄钮板顶面
        //     y15.06，射线恒 z=16.6（基座 z 内域，避开摆棍 ±Z 摆段 z≤16.5）→ 恰中基座/钮板 +Y 面，
        //     几何按 mechBoxes 精确推演）。
        const auto aimT46a = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT46a.release();
            pcT46a.grab();
            pcT46a.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT46a.tick();
            return pcT46a.hitBlock();
        };
        const auto pumpT46a = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟，t1034a 同款）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 潜行腿·拉杆：潜行持木板右键拉杆 A 基座顶面 → 木板落上方邻格 (12,16,16)；拉杆 A bit0 保持 0。
        const QVector3D hitSneakLv = aimT46a(16.6f, 12.5f, 15.09f, 16.6f);
        pcT46a.setKey(Qt::Key_Shift, true);        // 潜行（sneakPlace = m_keys 原始键态，t523 口径）
        pcT46a.setSelectedBlock(int(BR::Planks));  // 持方块（C++ 直喂 selectedBlock，t1034a 同款）
        pcT46a.placeBlock();
        pumpT46a(260);
        pcT46a.setKey(Qt::Key_Shift, false);
        const bool okSneakLv = hitSneakLv == QVector3D(12, 15, 16)
            && wT46a.blockAt(12, 16, 16) == BR::Planks    // 命中面邻格放置成功（+Y 面）
            && wT46a.blockAt(12, 15, 16) == BR::Lever
            && (wT46a.stateAt(12, 15, 16) & 1) == 0;      // 拉杆未扳动（use 被旁路）
        // (2) use 腿·拉杆：不潜行右键拉杆 B → 激活翻 bit0=1、无放置。
        pumpT46a(260);
        const QVector3D hitUseLv = aimT46a(20.6f, 12.5f, 15.09f, 20.6f);
        pcT46a.placeBlock(); // 不潜行（Shift 已松）→ 机关激活照常
        pumpT46a(260);
        const bool okUseLv = hitUseLv == QVector3D(12, 15, 20)
            && wT46a.blockAt(12, 15, 20) == BR::Lever
            && (wT46a.stateAt(12, 15, 20) & 1) == 1       // 拉杆扳开
            && wT46a.blockAt(12, 16, 20) == BR::Air;      // 无放置（右键被 use 消费）
        // (3) 潜行腿·按钮：潜行持木板右键按钮 A 钮板顶面 → 木板落 (12,16,24)；按钮 A bit0 保持 0。
        pumpT46a(260);
        const QVector3D hitSneakBtn = aimT46a(24.6f, 12.5f, 15.06f, 24.6f);
        pcT46a.setKey(Qt::Key_Shift, true);
        pcT46a.placeBlock();
        pumpT46a(260);
        pcT46a.setKey(Qt::Key_Shift, false);
        const bool okSneakBtn = hitSneakBtn == QVector3D(12, 15, 24)
            && wT46a.blockAt(12, 16, 24) == BR::Planks
            && wT46a.blockAt(12, 15, 24) == BR::WoodButton
            && (wT46a.stateAt(12, 15, 24) & 1) == 0;      // 按钮未按下（use 被旁路）
        // (4) use 腿·按钮：不潜行右键按钮 B → 按下 bit0=1、无放置。
        pumpT46a(260);
        const QVector3D hitUseBtn = aimT46a(28.6f, 12.5f, 15.06f, 28.6f);
        pcT46a.placeBlock();
        pumpT46a(260);
        const bool okUseBtn = hitUseBtn == QVector3D(12, 15, 28)
            && wT46a.blockAt(12, 15, 28) == BR::WoodButton
            && (wT46a.stateAt(12, 15, 28) & 1) == 1       // 按钮按下（弹回计时入表）
            && wT46a.blockAt(12, 16, 28) == BR::Air;      // 无放置
        // (5) 源钉（pinSet 剥注释；阴性轮摘门即红）。
        const QString rootT46a = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT46a = pinSet(rootT46a + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"cpp-mech-sneak-gate", "if (!sneakPlaceBlock && BlockRegistry::isManualIgniter(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {"},
        });
        if (!missT46a.isEmpty())
            qInfo().noquote() << "  [t1046a diag] pins" << missT46a.join(QLatin1Char(','));
        pcT46a.release();
        winT46a.deleteLater();
        const bool okA = okSneakLv && okUseLv && okSneakBtn && okUseBtn && missT46a.isEmpty();
        if (!okA)
            qInfo().noquote() << "  [t1046a diag] sneakLv=" << okSneakLv << "useLv=" << okUseLv
                              << "sneakBtn=" << okSneakBtn << "useBtn=" << okUseBtn
                              << "hitSneakLv" << hitSneakLv << "hitUseLv" << hitUseLv
                              << "hitSneakBtn" << hitSneakBtn << "hitUseBtn" << hitUseBtn;
        if (!okA) ++totalFail;
        qInfo().noquote() << (okA ? "PASS" : "FAIL")
                          << "| t1046a lever/button sneakPlace bypass: sneaking with a held block and "
                             "right-clicking a floor lever (or a wood button) places the held block "
                             "on the hit face's neighbor cell while the mechanism state bit stays "
                             "clear (activation bypassed, MC sneak-use caliber, parity low-3); "
                             "without sneak the right-click still activates the lever (bit0 set) "
                             "and presses the button (bit0 set, recovery armed) and consumes the "
                             "click so nothing is placed (negative-round sensitive: mech-branch "
                             "sneakPlace gate removal)"
                          << (okA ? QString()
                                  : QStringLiteral("diag sneakLv=%1 useLv=%2 sneakBtn=%3 useBtn=%4 pins=%5")
                                        .arg(okSneakLv).arg(okUseLv).arg(okSneakBtn).arg(okUseBtn)
                                        .arg(missT46a.isEmpty()));
    });

    // ── P-t1050a 空手潜行右键机关照常扳动（t1050 修2，Review_2026-09-15 #2 MC 口径纠偏）──
    //    两向：(1) 空手潜行右键拉杆 = 扳动照常（bit0 0→1 + 挥手 + 无放置）——潜行只让手持方块的
    //    放置优先（minecraft.wiki/w/Sneaking Effects："Pressing use prioritizes using a held item
    //    over interacting with a targeted block"；空手无手持物品 → 交互照常），51cc43c③ 裸门回归
    //    （空手 sneak 右键机关=无效应）就此收口；(2) 持方块潜行对照 = 放置优先不回归（t1046a 同
    //    语义再钉：bit0 保持 0 + 邻格木板）。
    //    阴性轮敏感：sneakPlaceBlock 判据摘 m_selectedBlock!=Air 合取项（退回裸 sneakPlace）→
    //    (1) 红（空手 sneak 旁路到放置，拉杆不扳）+ t1050b/c/d 空手腿同红 + 判据钉红；(2) 与既有
    //    持方块腿（t1034a/b/c、t1046a、t1028b(1b)）判据取值不变 → 保绿（恰红面 = 本单四新腿）。
    runLegMulti({ "t1050a empty-hand sneak right-click still activates mechanisms (t1050, Review_"
        "2026-09-15 #2 MC caliber fix): sneaking with an empty hand and right-clicking a floor "
        "lever toggles it on (state bit0 set, exactly one swingArm, no block placed on the hit "
        "face's neighbor cell - sneak only prioritizes placing a held block, an empty hand falls "
        "through to normal interaction per MC wiki Sneaking use-prioritizes-held-item caliber); "
        "sneaking with a held block on the control lever still bypasses to placement (bit0 stays "
        "clear, planks land above - t1046a caliber regression-pinned)"
        "diag emptyHand=%1 heldCtl=%2 pins=%3 swings=%4" }, [&]() {
        World wT50a;
        wT50a.setWidth(48); wT50a.setDepth(48); wT50a.setHeight(96); wT50a.setSeed(10501);
        Hotbar hbT50a;
        PlayerController pcT50a;
        pcT50a.setWorld(&wT50a);
        pcT50a.setHotbar(&hbT50a);
        QQuickWindow winT50a;
        pcT50a.setParentItem(winT50a.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空；拉杆 A（z=16，空手腿）与 B（z=20，持方块对照腿）
        //     贴地 state0（t1046a 同式摆位）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT50a.setBlock(x, y, z, BR::Air, 0);
                wT50a.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT50a.setBlock(12, 15, 16, BR::Lever, quint8(0)); // A（空手腿）
        wT50a.setBlock(12, 15, 20, BR::Lever, quint8(0)); // B（对照腿）
        int swingsT50a = 0;
        const QMetaObject::Connection cSwA = QObject::connect(
            &pcT50a, &PlayerController::swingArm, &pcT50a,
            [&swingsT50a]() { ++swingsT50a; });
        // 瞄准帮手（t1046a 同款）：自 +X 侧瞄拉杆基座顶面 y15.09，射线 z=基座 z 内域（恰中 +Y 面）。
        const auto aimT50a = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT50a.release();
            pcT50a.grab();
            pcT50a.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT50a.tick();
            return pcT50a.hitBlock();
        };
        const auto pumpT50a = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 空手腿：显式归 Air（t1040 rig 加固同式）+ 潜行右键拉杆 A → 扳动（bit0=1）+ 恰一次
        //     挥手（使用动作，t29）+ 无放置（右键被 use 消费）。
        const QVector3D hitEmptyA = aimT50a(16.6f, 12.5f, 15.09f, 16.6f);
        pcT50a.setSelectedBlock(int(BR::Air));    // 空手
        pcT50a.setKey(Qt::Key_Shift, true);       // 潜行（sneakPlace = m_keys 原始键态，t523 口径）
        pcT50a.placeBlock();
        pumpT50a(260);
        pcT50a.setKey(Qt::Key_Shift, false);
        const bool okEmptyA = hitEmptyA == QVector3D(12, 15, 16)
            && wT50a.blockAt(12, 15, 16) == BR::Lever
            && (wT50a.stateAt(12, 15, 16) & 1) == 1       // 扳动（激活沿——t1050 纠偏核心断言）
            && swingsT50a == 1                             // 一次「使用」动作挥手
            && wT50a.blockAt(12, 16, 16) == BR::Air;       // 无放置
        // (2) 持方块对照腿：潜行持木板右键拉杆 B → 放置优先（bit0 保持 0 + 邻格木板，t1046a 口径）。
        pumpT50a(260);
        const QVector3D hitHeldA = aimT50a(20.6f, 12.5f, 15.09f, 20.6f);
        pcT50a.setKey(Qt::Key_Shift, true);
        pcT50a.setSelectedBlock(int(BR::Planks)); // 持方块（C++ 直喂 selectedBlock，t1028b 同款）
        pcT50a.placeBlock();
        pumpT50a(260);
        pcT50a.setKey(Qt::Key_Shift, false);
        const bool okHeldA = hitHeldA == QVector3D(12, 15, 20)
            && wT50a.blockAt(12, 16, 20) == BR::Planks     // 命中面邻格放置
            && wT50a.blockAt(12, 15, 20) == BR::Lever
            && (wT50a.stateAt(12, 15, 20) & 1) == 0;       // 未扳动（use 被旁路）
        // (3) 源钉：旁路判据合取形态（阴性轮摘合取项即红；pinSet 剥注释）。
        //     t1054 同变更修订钉面落点（t1023c 先例，纠偏非削钉）：合取收进私有 helper
        //     heldPlaceableSneak()（review0916 #11），原 12 门判据行改读 helper、合取本体迁至
        //     helper return 行（唯一权威）——钉随语义落点改指新行；阴性轮摘合取项（退回裸
        //     sneakPlace / &&→||）→ 本钉同红（t1054b 与本钉双钉同一行，恰红面不变）。
        const QString rootT50a = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT50a = pinSet(rootT50a + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"t1050 sneak bypass holds-block conjunction",
             "return m_keys.value(Qt::Key_Shift) && m_selectedBlock != BlockRegistry::Air;"},
        });
        if (!missT50a.isEmpty())
            qInfo().noquote() << "  [t1050a diag] pins" << missT50a.join(QLatin1Char(','));
        QObject::disconnect(cSwA);
        pcT50a.release();
        winT50a.deleteLater();
        const bool okA50 = okEmptyA && okHeldA && missT50a.isEmpty();
        if (!okA50)
            qInfo().noquote() << "  [t1050a diag] hitEmpty" << hitEmptyA << "hitHeld" << hitHeldA
                              << "stA" << wT50a.stateAt(12, 15, 16) << "stB" << wT50a.stateAt(12, 15, 20);
        if (!okA50) ++totalFail;
        qInfo().noquote() << (okA50 ? "PASS" : "FAIL")
                          << "| t1050a empty-hand sneak right-click still activates mechanisms: "
                             "empty-hand sneak on a lever toggles bit0 on with one swingArm and "
                             "no placement (MC wiki use-prioritizes-held-item caliber, 51cc43c "
                             "empty-hand regression closed); held-block sneak still bypasses to "
                             "placement on the control lever (t1046a caliber re-pinned)"
                          << (okA50 ? QString()
                                    : QStringLiteral("diag emptyHand=%1 heldCtl=%2 pins=%3 swings=%4")
                                          .arg(okEmptyA).arg(okHeldA).arg(missT50a.isEmpty())
                                          .arg(swingsT50a));
    });

    // ── P-t1050b 空手潜行右键门照常开合（t1050 修2；t1034a 同 rig 反向面）──
    //    (1) 空手潜行右键木门 = 开合照常（两半同翻 bit2=4、doorToggled 恰 1 次、无放置）；(2) 持方块
    //    潜行对照 = 放置优先不回归（门两半合态不动 + toggles 不增长 + 邻格木板）。阴性轮敏感同 t1050a。
    runLegMulti({ "t1050b empty-hand sneak right-click still opens doors (t1050, Review_2026-09-1"
        "5 #2 MC caliber fix): sneaking with an empty hand and right-clicking a wooden door opens"
        " it (both halves flip bit2, exactly one doorToggled(true), no block placed - empty hand"
        " interacts normally, MC wiki use-prioritizes-held-item caliber); sneaking with a held bl"
        "ock on the control door still bypasses to placement (door stays shut, toggle count froze"
        "n, planks land on the clicked face's neighbor cell)diag emptyHand=%1 heldCtl=%2 toggles"
        "=%3" }, [&]() {
        World wT50b;
        wT50b.setWidth(48); wT50b.setDepth(48); wT50b.setHeight(96); wT50b.setSeed(10502);
        Hotbar hbT50b;
        PlayerController pcT50b;
        pcT50b.setWorld(&wT50b);
        pcT50b.setHotbar(&hbT50b);
        QQuickWindow winT50b;
        pcT50b.setParentItem(winT50b.contentItem());
        // rig：y=14 Planks 地台；门 A（z=16，空手腿）与 B（z=20，对照腿）各两格（下 0 + 上 8，合态）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT50b.setBlock(x, y, z, BR::Air, 0);
                wT50b.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT50b.setBlock(12, 15, 16, BR::WoodDoor, quint8(0)); // 门 A 下格（合）
        wT50b.setBlock(12, 16, 16, BR::WoodDoor, quint8(8)); // 门 A 上格
        wT50b.setBlock(12, 15, 20, BR::WoodDoor, quint8(0)); // 门 B 下格（合）
        wT50b.setBlock(12, 16, 20, BR::WoodDoor, quint8(8)); // 门 B 上格
        int togglesT50b = 0;
        bool lastOpenT50b = false;
        const QMetaObject::Connection cTglB = QObject::connect(
            &pcT50b, &PlayerController::doorToggled, &pcT50b,
            [&](bool open) { ++togglesT50b; lastOpenT50b = open; });
        const auto aimT50b = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT50b.release();
            pcT50b.grab();
            pcT50b.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT50b.tick();
            return pcT50b.hitBlock();
        };
        const auto pumpT50b = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 空手腿：空手 + 潜行右键门 A 的 +X 面（t1034a 同 aim）→ 开合照常。
        const QVector3D hitEmptyB = aimT50b(16.5f, 12.95f, 15.5f, 16.5f);
        pcT50b.setSelectedBlock(int(BR::Air));    // 空手
        pcT50b.setKey(Qt::Key_Shift, true);       // 潜行
        pcT50b.placeBlock();
        pumpT50b(260);
        pcT50b.setKey(Qt::Key_Shift, false);
        const bool okEmptyB = hitEmptyB == QVector3D(12, 15, 16)
            && togglesT50b == 1 && lastOpenT50b            // 开合恰一次（两格同翻只发一次）
            && (wT50b.stateAt(12, 15, 16) & 4) == 4        // 下半开
            && (wT50b.stateAt(12, 16, 16) & 4) == 4        // 上半联动开
            && wT50b.blockAt(13, 15, 16) == BR::Air;       // 无放置（右键被 use 消费）
        // (2) 持方块对照腿：潜行持木板右键门 B → 放置优先（门两半合态不动 + toggles 不增长）。
        pumpT50b(260);
        const QVector3D hitHeldB = aimT50b(20.5f, 12.95f, 15.5f, 20.5f);
        pcT50b.setKey(Qt::Key_Shift, true);
        pcT50b.setSelectedBlock(int(BR::Planks));
        pcT50b.placeBlock();
        pumpT50b(260);
        pcT50b.setKey(Qt::Key_Shift, false);
        const bool okHeldB = hitHeldB == QVector3D(12, 15, 20)
            && togglesT50b == 1                            // 对照腿零新增开合（use 被旁路）
            && (wT50b.stateAt(12, 15, 20) & 4) == 0
            && (wT50b.stateAt(12, 16, 20) & 4) == 0        // 门 B 两半合态不动
            && wT50b.blockAt(13, 15, 20) == BR::Planks;    // 命中面邻格放置
        QObject::disconnect(cTglB);
        pcT50b.release();
        winT50b.deleteLater();
        const bool okB50 = okEmptyB && okHeldB;
        if (!okB50)
            qInfo().noquote() << "  [t1050b diag] hitEmpty" << hitEmptyB << "hitHeld" << hitHeldB
                              << "toggles" << togglesT50b;
        if (!okB50) ++totalFail;
        qInfo().noquote() << (okB50 ? "PASS" : "FAIL")
                          << "| t1050b empty-hand sneak right-click still opens doors: empty-hand "
                             "sneak opens the door (both halves flip bit2, one doorToggled(true), "
                             "no placement - MC wiki use-prioritizes-held-item caliber); held-block "
                             "sneak still bypasses to placement (door shut, toggle count frozen)"
                          << (okB50 ? QString()
                                    : QStringLiteral("diag emptyHand=%1 heldCtl=%2 toggles=%3")
                                          .arg(okEmptyB).arg(okHeldB).arg(togglesT50b));
    });

    // ── P-t1050c 空手潜行右键床照常触达入睡链（t1050 修2；t1034b 同 rig 反向面）──
    //    (1) 空手潜行右键床（白天非雷暴）= 入睡链照常触达 → 拒睡文案恰 1 次（同 t1034b 非潜行对照
    //    面——差异仅在潜行键态，证明门不再吞空手 sneak）；(2) 持方块潜行对照 = 放置优先不回归
    //    （refused 不增长 + 邻格木板）。阴性轮敏感同 t1050a。
    runLegMulti({ "t1050c empty-hand sneak right-click still reaches the sleep chain (t1050, Revie"
        "w_2026-09-15 #2 MC caliber fix): sneaking with an empty hand and right-clicking the bed b"
        "y day still refuses with the exact night-or-thunder message (sleep chain reached, not sle"
        "eping, no placement - MC wiki use-prioritizes-held-item caliber); sneaking with a held b"
        "lock on the control bed still bypasses to placement (refusal count frozen, planks land o"
        "n the clicked face's neighbor cell)diag emptyHand=%1 heldCtl=%2 refused=%3" }, [&]() {
        World wT50c;
        wT50c.setWidth(48); wT50c.setDepth(48); wT50c.setHeight(96); wT50c.setSeed(10503);
        wT50c.setWeatherState(0); // Clear（白天拒睡的确定性前提，t1034b 同款）
        Hotbar hbT50c;
        WorldClock clockT50c;
        clockT50c.setPhase(0.2f); // 白天（isNight=false；setPhase 特权指令，t1024a 同款）
        PlayerController pcT50c;
        pcT50c.setWorld(&wT50c);
        pcT50c.setWorldClock(&clockT50c);
        pcT50c.setHotbar(&hbT50c);
        QQuickWindow winT50c;
        pcT50c.setParentItem(winT50c.contentItem());
        // rig：床 A foot (12,15,16)/head (11,15,16)（空手腿）+ 床 B foot (12,15,20)/head (11,15,20)
        //（对照腿），D=+X：head 在 foot -X 侧（t1034b 同式摆位）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT50c.setBlock(x, y, z, BR::Air, 0);
                wT50c.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT50c.setBlock(12, 15, 16, BR::BedWhite, quint8(0)); // 床 A foot
        wT50c.setBlock(11, 15, 16, BR::BedWhite, quint8(8)); // 床 A head
        wT50c.setBlock(12, 15, 20, BR::BedWhite, quint8(0)); // 床 B foot
        wT50c.setBlock(11, 15, 20, BR::BedWhite, quint8(8)); // 床 B head
        int refusedT50c = 0;
        QString lastRefuseT50c;
        const QMetaObject::Connection cRefC = QObject::connect(
            &pcT50c, &PlayerController::sleepRefused, &pcT50c,
            [&refusedT50c, &lastRefuseT50c](const QString &r) { ++refusedT50c; lastRefuseT50c = r; });
        const auto aimT50c = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT50c.release();
            pcT50c.grab();
            pcT50c.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT50c.tick();
            return pcT50c.hitBlock();
        };
        const auto pumpT50c = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 空手腿：空手 + 潜行右键床 A foot 侧面（aim y 15.15 床垫低盒带内，t1034b 首跑教训同式）
        //     → 入睡链照常触达：拒睡文案恰 1 次 + 不入睡 + 无放置。
        const QVector3D hitEmptyC = aimT50c(16.5f, 12.95f, 15.15f, 16.5f);
        pcT50c.setSelectedBlock(int(BR::Air));    // 空手
        pcT50c.setKey(Qt::Key_Shift, true);       // 潜行（t1050 纠偏：不再吞空手交互）
        pcT50c.placeBlock();
        pumpT50c(260);
        pcT50c.setKey(Qt::Key_Shift, false);
        const bool okEmptyC = hitEmptyC == QVector3D(12, 15, 16)
            && refusedT50c == 1
            && lastRefuseT50c == QStringLiteral("只能在夜晚或雷暴中睡觉")
            && !pcT50c.sleeping()
            && wT50c.blockAt(13, 15, 16) == BR::Air        // 无放置（右键被睡链消费）
            && wT50c.blockAt(12, 15, 16) == BR::BedWhite;  // 床本体原样
        // (2) 持方块对照腿：潜行持木板右键床 B → 放置优先（refused 不增长 + 邻格木板）。
        pumpT50c(260);
        const QVector3D hitHeldC = aimT50c(20.5f, 12.95f, 15.15f, 20.5f);
        pcT50c.setKey(Qt::Key_Shift, true);
        pcT50c.setSelectedBlock(int(BR::Planks));
        pcT50c.placeBlock();
        pumpT50c(260);
        pcT50c.setKey(Qt::Key_Shift, false);
        const bool okHeldC = hitHeldC == QVector3D(12, 15, 20)
            && refusedT50c == 1                            // 睡链零触达（潜行持方块 = 放置优先）
            && !pcT50c.sleeping()
            && wT50c.blockAt(13, 15, 20) == BR::Planks     // 命中面邻格放置
            && wT50c.blockAt(12, 15, 20) == BR::BedWhite;  // 床本体原样
        QObject::disconnect(cRefC);
        pcT50c.release();
        winT50c.deleteLater();
        const bool okC50 = okEmptyC && okHeldC;
        if (!okC50)
            qInfo().noquote() << "  [t1050c diag] hitEmpty" << hitEmptyC << "hitHeld" << hitHeldC
                              << "refused" << refusedT50c << "msg" << lastRefuseT50c;
        if (!okC50) ++totalFail;
        qInfo().noquote() << (okC50 ? "PASS" : "FAIL")
                          << "| t1050c empty-hand sneak right-click still reaches the sleep chain: "
                             "empty-hand sneak on the bed by day refuses with the exact "
                             "night-or-thunder message and places nothing (MC wiki "
                             "use-prioritizes-held-item caliber); held-block sneak still bypasses "
                             "to placement with the refusal count frozen"
                          << (okC50 ? QString()
                                    : QStringLiteral("diag emptyHand=%1 heldCtl=%2 refused=%3")
                                          .arg(okEmptyC).arg(okHeldC).arg(refusedT50c));
    });

    // ── P-t1050d 空手潜行右键活板门照常翻板（t1050 修2；t1034c 同 rig 反向面）──
    //    (1) 空手潜行右键合态活板门 = 翻板照常（bit0→1、doorToggled 恰 1 次、无放置）；(2) 持方块
    //    潜行对照 = 放置优先不回归（bit0 保持 0 + toggles 不增长 + 邻格木板）。阴性轮敏感同 t1050a。
    runLegMulti({ "t1050d empty-hand sneak right-click still flips trapdoors (t1050, Review_2026-"
        "09-15 #2 MC caliber fix): sneaking with an empty hand and right-clicking a closed trapdo"
        "or flips it open (state bit0 set, exactly one doorToggled(true), no block placed - MC wi"
        "ki use-prioritizes-held-item caliber); sneaking with a held block on the control trapdoo"
        "r still bypasses to placement (bit0 stays clear, toggle count frozen, planks land on the"
        " clicked face's neighbor cell)diag emptyHand=%1 heldCtl=%2 toggles=%3" }, [&]() {
        World wT50d;
        wT50d.setWidth(48); wT50d.setDepth(48); wT50d.setHeight(96); wT50d.setSeed(10504);
        Hotbar hbT50d;
        PlayerController pcT50d;
        pcT50d.setWorld(&wT50d);
        pcT50d.setHotbar(&hbT50d);
        QQuickWindow winT50d;
        pcT50d.setParentItem(winT50d.contentItem());
        // rig：活板门 A（z=16，空手腿）与 B（z=20，对照腿）各一格合态 state0（t1034c 同式摆位）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT50d.setBlock(x, y, z, BR::Air, 0);
                wT50d.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT50d.setBlock(12, 15, 16, BR::WoodTrapdoor, quint8(0)); // A（合）
        wT50d.setBlock(12, 15, 20, BR::WoodTrapdoor, quint8(0)); // B（合）
        int togglesT50d = 0;
        bool lastOpenT50d = false;
        const QMetaObject::Connection cTglD = QObject::connect(
            &pcT50d, &PlayerController::doorToggled, &pcT50d,
            [&](bool open) { ++togglesT50d; lastOpenT50d = open; });
        const auto aimT50d = [&](float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = 14.5f, ey = 16.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT50d.release();
            pcT50d.grab();
            pcT50d.loadSavedState(ex, 15.0f, ez, yaw, pit, 2 /* Survival */);
            pcT50d.tick();
            return pcT50d.hitBlock();
        };
        const auto pumpT50d = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 空手腿：空手 + 潜行右键活板门 A 的 +X 面（薄板带内 aim，t1034c 同式）→ 翻板照常。
        const QVector3D hitEmptyD = aimT50d(16.5f, 12.95f, 15.10f, 16.5f);
        pcT50d.setSelectedBlock(int(BR::Air));    // 空手
        pcT50d.setKey(Qt::Key_Shift, true);       // 潜行
        pcT50d.placeBlock();
        pumpT50d(260);
        pcT50d.setKey(Qt::Key_Shift, false);
        const bool okEmptyD = hitEmptyD == QVector3D(12, 15, 16)
            && togglesT50d == 1 && lastOpenT50d            // 翻板恰一次（开）
            && wT50d.blockAt(12, 15, 16) == BR::WoodTrapdoor
            && (wT50d.stateAt(12, 15, 16) & 1) == 1        // 板已翻（开态）
            && wT50d.blockAt(13, 15, 16) == BR::Air;       // 无放置（右键被 use 消费）
        // (2) 持方块对照腿：潜行持木板右键活板门 B → 放置优先（bit0 保持 0 + toggles 不增长）。
        pumpT50d(260);
        const QVector3D hitHeldD = aimT50d(20.5f, 12.95f, 15.10f, 20.5f);
        pcT50d.setKey(Qt::Key_Shift, true);
        pcT50d.setSelectedBlock(int(BR::Planks));
        pcT50d.placeBlock();
        pumpT50d(260);
        pcT50d.setKey(Qt::Key_Shift, false);
        const bool okHeldD = hitHeldD == QVector3D(12, 15, 20)
            && togglesT50d == 1                            // 对照腿零新增翻板（use 被旁路）
            && wT50d.blockAt(12, 15, 20) == BR::WoodTrapdoor
            && (wT50d.stateAt(12, 15, 20) & 1) == 0        // 板合态不动
            && wT50d.blockAt(13, 15, 20) == BR::Planks;    // 命中面邻格放置
        QObject::disconnect(cTglD);
        pcT50d.release();
        winT50d.deleteLater();
        const bool okD50 = okEmptyD && okHeldD;
        if (!okD50)
            qInfo().noquote() << "  [t1050d diag] hitEmpty" << hitEmptyD << "hitHeld" << hitHeldD
                              << "toggles" << togglesT50d;
        if (!okD50) ++totalFail;
        qInfo().noquote() << (okD50 ? "PASS" : "FAIL")
                          << "| t1050d empty-hand sneak right-click still flips trapdoors: "
                             "empty-hand sneak flips the closed trapdoor open (bit0 set, one "
                             "doorToggled(true), no placement - MC wiki use-prioritizes-held-item "
                             "caliber); held-block sneak still bypasses to placement (bit0 clear, "
                             "toggle count frozen)"
                          << (okD50 ? QString()
                                    : QStringLiteral("diag emptyHand=%1 heldCtl=%2 toggles=%3")
                                          .arg(okEmptyD).arg(okHeldD).arg(togglesT50d));
    });

    // ── P-t1052a 箱车裸键门清偿：空手 sneak 右键箱车照常开箱（t1050 残余登记清偿，Review_2026-09-15 #2 同门收官）──
    //    三相：(1) 基线 = 无 shift 空手右键箱车 → 开箱恰 1 携内容键（t1013b(b) 同族回归钉，同 rig 内
    //    自证 findCartHit 命中 + 遮挡守卫不过杀）；(2) t1052 核心 = 空手 + shift → **照常开箱**（旧裸键
    //    门 !m_keys.value(Key_Shift) 把此形态旁路到骑乘/放置路径——箱车不可骑守卫 + m_selectedBlock==Air
    //    守卫连环拦成「无效应」，t1050 十二门同疾第 13 门；修后门 = sneak ∧ 持可放置方块合取，与
    //    m_hasHit 块内 12 门单一判据同构）；(3) 持方块 + shift 对照 = 放置优先（chestOpened 不再增长 +
    //    命中面邻格落木板，t1050 四腿同款对照柱；MC 口径 = 手持物品的使用优先于目标交互）。
    //    阴性轮敏感：矿车开箱门摘本单合取（退回裸 !Key_Shift）→ (2) 语义柱红（空手 sneak 不开箱，
    //    opens 相对 +1 落空）+ 源钉红；(1) 基线与 (3) 对照柱全走相对恒等/快照锚（R20.11）→ 保绿；
    //    既有全部箱车腿（t1013/t1013b 均无 shift 形态）判据取值不变 → 保绿（恰红面 = 本单新腿单腿）。
    runLegMulti({ "t1052a chest-minecart raw-sneak-bypass residual closed (t1050 residual, Review_2026-09-1"
        "5 #2 same-gate finale): right-clicking a chest cart with an empty hand opens it exactly once c"
        "arrying the content key with or without sneaking - sneaking without a placeable block falls t"
        "hrough to normal container use (the old raw shift gate routed empty-hand sneak past the open "
        "branch into mount/placement where the chest-cart-cannot-be-ridden guard and the Air-selectio"
        "n guard turned it into a no-effect, the 13th gate with the t1050 disease); sneaking with a h"
        "eld block still bypasses the open to placement priority (chestOpened frozen, planks land on "
        "the hit face's neighbor cell - the t1050 four-leg control column)diag base=%1 emptyShift=%2 "
        "heldShift=%3 pins=%4 opens=%5" }, [&]() {
        World wT52;
        wT52.setWidth(48); wT52.setDepth(48); wT52.setHeight(96); wT52.setSeed(10521);
        PlayerController pcT52;
        pcT52.setWorld(&wT52);
        MinecartManager cartsT52;
        pcT52.setMinecartManager(&cartsT52);
        QQuickWindow winT52;
        pcT52.setParentItem(winT52.contentItem());
        // rig：y=14 Planks 地台，y15..20 净空（t1050a 同式）；箱车键格 (12,15,16) 四邻无轨 → 地面静止
        //     姿态（车心 y=15.3875，体盒 x[12.05,12.95] z[16,17] y[14.94,15.84]）；玩家 (12.5,15,18.5)
        //     瞄地台 (12,14,15) 顶面点 (12.5,15.0,15.5)——射线穿车体盒（findCartHit 命中）且穿键格 Air
        //     （箱车是实体不挡体素射线）落主选 (12,14,15)；对照写格 (12,15,15) 在车后不蹭车盒/玩家 AABB。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT52.setBlock(x, y, z, BR::Air, 0);
                wT52.setBlock(x, 14, z, BR::Planks, 0);
            }
        cartsT52.spawnChestCart(12, 15, 16, &wT52, 12, 15, 16);
        int opensT52 = 0;
        int keyT52[3] = { -1, -1, -1 };
        const QMetaObject::Connection cOpenT52 = QObject::connect(
            &pcT52, &PlayerController::chestOpened, &pcT52,
            [&](int kx, int ky, int kz) { ++opensT52; keyT52[0] = kx; keyT52[1] = ky; keyT52[2] = kz; });
        const auto aimT52 = [&]() { // 瞄地台 (12,14,15) 顶面（t1050a aim 同式：grab+loadSavedState+tick 刷主选）
            const float ex = 12.5f, ey = 16.62f, ez = 18.5f;
            const float dx = 12.5f - ex, dy = 15.0f - ey, dz = 15.5f - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            pcT52.release();
            pcT52.grab();
            pcT52.loadSavedState(ex, 15.0f, ez,
                                 std::atan2(-dx, -dz) * 57.2957795f,
                                 std::asin(dy / len) * 57.2957795f, 1 /* Creative（无消耗，t1013b 同款）*/);
            pcT52.tick(); // updateRaycast 刷新命中
            return pcT52.hitBlock();
        };
        const auto pumpT52 = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // (1) 基线：无潜行空手右键箱车 → 开箱恰 1 携键 + 无放置（use 被消费；放置零 = 幽灵放置不回归）。
        const QVector3D hitT52 = aimT52();
        pcT52.setSelectedBlock(int(BR::Air));    // 空手（C++ 直喂；防默认 selectedBlock 幽灵放置，t1013b 教训）
        pcT52.placeBlock();
        pumpT52(260);
        const bool okBase = opensT52 == 1
            && keyT52[0] == 12 && keyT52[1] == 15 && keyT52[2] == 16 // 内容键 = 生成格（键寻址契约）
            && hitT52 == QVector3D(12, 14, 15)     // 主选命中地台顶面（箱车不挡体素射线）
            && wT52.blockAt(12, 15, 15) == BR::Air; // 无放置
        const int opensAfterBaseT52 = opensT52; // 相对计数锚（R20.11：辅助/对照柱钉相对恒等，绝对计数归语义柱专钉——阴性变异红面不得扩散进对照柱）
        // (2) t1052 核心：空手 + 潜行右键箱车 → 照常开箱（恰 +1 携键；旧裸键门此形态 = 无效应）。
        pumpT52(260);
        aimT52();
        pcT52.setKey(Qt::Key_Shift, true);       // 潜行（m_keys 原始键态，§2-D 单一输入路径）
        pcT52.placeBlock();
        pumpT52(260);
        pcT52.setKey(Qt::Key_Shift, false);
        const bool okEmptyShift = opensT52 == opensAfterBaseT52 + 1
            && keyT52[0] == 12 && keyT52[1] == 15 && keyT52[2] == 16
            && wT52.blockAt(12, 15, 15) == BR::Air; // 空手无放置（use 被消费）
        const int opensBeforeHeldT52 = opensT52; // 相对恒等锚（对照柱：开箱计数须冻结）
        // (3) 持方块潜行对照 = 放置优先（t1050 四腿同款对照柱）：chestOpened 冻结 + 命中面邻格木板。
        pumpT52(260);
        aimT52();
        pcT52.setKey(Qt::Key_Shift, true);
        pcT52.setSelectedBlock(int(BR::Planks)); // 持方块（C++ 直喂，t1028b 同款）
        pcT52.placeBlock();
        pumpT52(260);
        pcT52.setKey(Qt::Key_Shift, false);
        const bool okHeldShift = opensT52 == opensBeforeHeldT52   // 开箱被旁路（相对恒等：计数冻结，放置优先不回归）
            && wT52.blockAt(12, 15, 15) == BR::Planks             // 命中面邻格放置
            && wT52.blockAt(12, 14, 15) == BR::Planks;            // 地台本体不动
        // 源钉：矿车开箱旁路合取形态（阴性轮摘合取退回裸键门即红；pinSet 剥注释；t1050a 判据钉同式）。
        //   t1054 同变更修订钉面落点（t1023c 先例，纠偏非放宽）：合取收进私有 helper
        //   heldPlaceableSneak()（review0916 #11），开箱门改读之——钉「两调用点（含本开箱门）改读
        //   helper」minCount=2；合取本体唯一权威的语义钉由 t1054b 承接（helper 定义 + return 合取行
        //   + 旧字面副本反探）。
        const QString rootT52 = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT52 = pinSet(rootT52 + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"t1052 chest-cart open bypass reads held-placeable-sneak helper",
             "const bool sneakPlaceBlock = heldPlaceableSneak();", 2},
        });
        if (!missT52.isEmpty())
            qInfo().noquote() << "  [t1052a diag] pins" << missT52.join(QLatin1Char(','));
        QObject::disconnect(cOpenT52);
        cartsT52.clearAll();
        pcT52.release();
        winT52.deleteLater();
        const bool okT52 = okBase && okEmptyShift && okHeldShift && missT52.isEmpty();
        if (!okT52)
            qInfo().noquote() << "  [t1052a diag] hit" << hitT52 << "opens" << opensT52
                              << "key" << keyT52[0] << keyT52[1] << keyT52[2]
                              << "cell" << int(wT52.blockAt(12, 15, 15));
        if (!okT52) ++totalFail;
        qInfo().noquote() << (okT52 ? "PASS" : "FAIL")
                          << "| t1052a chest-minecart raw-sneak-bypass residual closed: empty-hand "
                             "right-click opens the chest cart exactly once with its content key "
                             "with or without sneak (the old raw shift gate made empty-hand sneak "
                             "a no-effect - 13th gate with the t1050 disease); held-block sneak "
                             "still bypasses the open to placement (t1050 control column)"
                          << (okT52 ? QString()
                                    : QStringLiteral("diag base=%1 emptyShift=%2 heldShift=%3 pins=%4 opens=%5")
                                          .arg(okBase).arg(okEmptyShift).arg(okHeldShift)
                                          .arg(missT52.isEmpty()).arg(opensT52));
    });

    // ── P-t1054a 骑乘门 sneak 抑制（t1054，review0916 #4，同族第 14 门 = 交互抑制面）──
    //    三相 + 回归柱（rig 照 t1052a 先例 seed 10521 地台；普通矿车静止轨上——孤轨 (12,15,16)
    //    state0 平贴 cell 底薄板，选体射线走 t983 薄板 sub-AABB 口径穿轨板上部空气命中地台，车体盒
    //    on-rail 姿态 y[15.0,15.9]（pos.y=15+kCartRideH≈15.45）仍被实体射线 s∈[0.5,0.83] 结构化
    //    相交——t1052a 同几何）：(1) t1054 核心 = 持可放置方块 + shift + 右键普通矿车 → 放置发生
    //    （命中面邻格木板，t1052a(3) 同格先例；(a) shift 裸门抑制 → (b) 非矿车物品跳过 → 通用放置）
    //    且零 mount；(2) 空手 + shift + 右键普通矿车 → 无效应（零 mount、零放置、零开箱、零挥臂——
    //    潜行一律抑制实体交互，shift 裸门与手持无关；旧版此形态 tryMount 先于放置吞成上车 = 病灶
    //    本体，t1052 提交注自证矛盾点）；(3) 无 shift 右键 → mount 照旧（回归柱，骑乘门只对 sneak
    //    关）。diag 带 mount 布尔 + 放置事件（格 id）+ heldId（selectedBlock）+ 挥臂/开箱计数。
    //    阴性轮敏感：NEG-1 摘 (a) 段 sneak 裸门 → (1)(2) 红（mount 吞放置 / 空手 sneak 上车），
    //    (3) 回归柱不红（无 shift 行为未动）、t1052a 三相不红（箱车走开箱/拒载路径不在 (a)）；
    //    NEG-2 破 helper 合取（&&→||）→ 本腿不红（(a) 门读 m_keys 裸键不经 helper——t1054b 源钉
    //    + t1050/t1052 行为腿承红）。
    runLegMulti({ "t1054a minecart mount sneak suppression (parity gate 14, interaction-suppression face"
        "): right-clicking a stationary rail cart while sneaking with a held placeable block places on "
        "the hit face's neighbor cell and never mounts (the shift bare gate suppresses mounting outright"
        ", placement priority wins); sneaking with an empty hand is a no-effect (no mount, no placement"
        ", no open, no swing - sneaking suppresses entity interaction regardless of what is held, the o"
        "ld code let tryMount swallow the use into mounting); without sneak the mount still happens (re"
        "gression column - the gate only closes for sneak)diag held=%1 empty=%2 regress=%3 mountIdx=%4 "
        "cell=%5 sel=%6 swings=%7 opens=%8 cartY=%9" }, [&]() {
        World wT54;
        wT54.setWidth(48); wT54.setDepth(48); wT54.setHeight(96); wT54.setSeed(10521);
        PlayerController pcT54;
        pcT54.setWorld(&wT54);
        MinecartManager cartsT54;
        pcT54.setMinecartManager(&cartsT54);
        QQuickWindow winT54;
        pcT54.setParentItem(winT54.contentItem());
        // rig：y=14 Planks 地台（t1052a 同式）+ 孤轨 (12,15,16) + 普通矿车 on-rail 静止；玩家
        //     (12.5,15,18.5) 瞄地台 (12,14,15) 顶面点 (12.5,15.0,15.5)——实体射线穿车体盒
        //     s∈[0.5,0.83]（t1052a 同几何），选体射线穿轨板上部空气（t983 薄板口径）命中地台；
        //     对照写格 (12,15,15) 不蹭车盒/玩家 AABB（t1052a(3) 先例）。
        for (int x = 6; x <= 18; ++x)
            for (int z = 12; z <= 24; ++z) {
                for (int y = 15; y <= 20; ++y) wT54.setBlock(x, y, z, BR::Air, 0);
                wT54.setBlock(x, 14, z, BR::Planks, 0);
            }
        wT54.setBlock(12, 15, 16, BR::Rail, 0);
        cartsT54.spawnCart(12, 15, 16, &wT54);
        const float cartY54 = cartsT54.count() == 1 ? cartsT54.posAt(0).y() : -1.0f;
        int opensT54 = 0, swingsT54 = 0;
        const QMetaObject::Connection cOpenT54 = QObject::connect(
            &pcT54, &PlayerController::chestOpened, &pcT54, [&](int, int, int) { ++opensT54; });
        const QMetaObject::Connection cSwingT54 = QObject::connect(
            &pcT54, &PlayerController::swingArm, &pcT54, [&]() { ++swingsT54; });
        const auto aimT54 = [&]() { // 瞄地台 (12,14,15) 顶面（t1052a aim 同式：grab+loadSavedState+tick 刷主选）
            const float ex = 12.5f, ey = 16.62f, ez = 18.5f;
            const float dx = 12.5f - ex, dy = 15.0f - ey, dz = 15.5f - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            pcT54.release();
            pcT54.grab();
            pcT54.loadSavedState(ex, 15.0f, ez,
                                 std::atan2(-dx, -dz) * 57.2957795f,
                                 std::asin(dy / len) * 57.2957795f, 1 /* Creative（无消耗，t1052a 同款）*/);
            pcT54.tick(); // updateRaycast 刷新命中
            return pcT54.hitBlock();
        };
        const auto pumpT54 = [](int ms) { // placeBlock 200ms 冷却间隔（t128；墙钟）
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const bool rigOkT54 = cartsT54.count() == 1
            && std::fabs(cartY54 - 15.45f) < 0.01f        // on-rail 姿态（cell 底 + 薄板 1/16 + kCartRideH）
            && cartsT54.ridingIndex() == -1
            && aimT54() == QVector3D(12, 14, 15);         // 选体射线穿轨板空气命中地台顶面（结构化选址自证）
        // (1) t1054 核心：持方块 + sneak 右键普通矿车 → 放置优先 + 零 mount（旧版 tryMount 吞放置）。
        pcT54.setSelectedBlock(int(BR::Planks)); // 持方块（C++ 直喂，t1052a(3) 同款；diag heldId 载体）
        pcT54.setKey(Qt::Key_Shift, true);       // 潜行（m_keys 原始键态，§2-D 单一输入路径）
        pcT54.placeBlock();
        pumpT54(260);
        pcT54.setKey(Qt::Key_Shift, false);
        const bool okHeldT54 = cartsT54.ridingIndex() == -1      // 零 mount（(a) shift 裸门抑制）
            && wT54.blockAt(12, 15, 15) == BR::Planks            // 命中面邻格放置（(b) 非矿车物品 → 通用放置）
            && wT54.blockAt(12, 14, 15) == BR::Planks            // 地台本体不动
            && cartsT54.count() == 1 && swingsT54 == 1;          // 零 spawn；放置恰 1 次挥臂
        // (2) 空手 + sneak 右键普通矿车 → 无效应（潜行一律抑制实体交互；旧版 = tryMount 吞成上车）。
        pumpT54(260);
        aimT54();
        pcT54.setSelectedBlock(int(BR::Air));    // 空手（C++ 直喂；防默认 selectedBlock 幽灵放置，t1013b 教训）
        pcT54.setKey(Qt::Key_Shift, true);
        pcT54.placeBlock();
        pumpT54(260);
        pcT54.setKey(Qt::Key_Shift, false);
        const bool okEmptyT54 = cartsT54.ridingIndex() == -1     // 零 mount
            && wT54.blockAt(12, 15, 15) == BR::Planks            // 零放置（(1) 落的木板不动）
            && wT54.blockAt(12, 15, 14) == BR::Air               // 邻格零写入（无任何放置）
            && cartsT54.count() == 1 && opensT54 == 0 && swingsT54 == 1; // 零事件（无新挥臂 / 无开箱）
        // (3) 回归柱：无 shift 右键 → mount 照旧（骑乘门只对 sneak 关；上车路径挥臂恰 +1）。
        pumpT54(260);
        aimT54();
        pcT54.placeBlock();
        pumpT54(260);
        const bool okRegressT54 = cartsT54.ridingIndex() == 0    // 恰上车（唯一车 = 槽 0）
            && cartsT54.count() == 1 && swingsT54 == 2
            && wT54.blockAt(12, 15, 15) == BR::Planks;           // 骑乘不落块
        QObject::disconnect(cOpenT54);
        QObject::disconnect(cSwingT54);
        cartsT54.clearAll();
        pcT54.release();
        winT54.deleteLater();
        const bool okT54 = rigOkT54 && okHeldT54 && okEmptyT54 && okRegressT54;
        if (!okT54)
            qInfo().noquote() << "  [t1054a diag] rig" << rigOkT54 << "held" << okHeldT54
                              << "empty" << okEmptyT54 << "regress" << okRegressT54
                              << "mountIdx" << cartsT54.ridingIndex()
                              << "cell" << int(wT54.blockAt(12, 15, 15))
                              << "sel" << pcT54.selectedBlock()
                              << "swings" << swingsT54 << "opens" << opensT54
                              << "cartY" << cartY54;
        if (!okT54) ++totalFail;
        qInfo().noquote() << (okT54 ? "PASS" : "FAIL")
                          << "| t1054a minecart mount sneak suppression (parity gate 14): held-block "
                             "sneak right-click places instead of mounting, empty-hand sneak is a "
                             "no-effect (sneaking suppresses entity interaction outright - the old "
                             "code let tryMount swallow the use), no-sneak still mounts (regression "
                             "column)"
                          << (okT54 ? QString()
                                    : QStringLiteral("diag held=%1 empty=%2 regress=%3 mountIdx=%4 "
                                                     "cell=%5 sel=%6 swings=%7 opens=%8 cartY=%9")
                                          .arg(okHeldT54).arg(okEmptyT54).arg(okRegressT54)
                                          .arg(cartsT54.ridingIndex())
                                          .arg(int(wT54.blockAt(12, 15, 15)))
                                          .arg(pcT54.selectedBlock())
                                          .arg(swingsT54).arg(opensT54).arg(cartY54));
    });

    // ── P-t1054b sneakPlaceBlock helper 化承重钉（t1054，review0916 #11）──
    //    helper 提取后两调用点语义恒等的源码面：①合取唯一权威 = helper 定义 + return 合取行
    //    （NEG-2 破合取 &&→|| 即红）；②两调用点改读 helper（minCount=2——m_hasHit 块 12 门判据 +
    //    矿车段 (a0) 开箱门；t1052a 源钉同变更修订后与本钉互恰）；③反探：旧手写合取调用行（两处
    //    副本）必须消失（miss 非空 = 合规，r2007b 反探先例；pinSet 剥注释器防注释提及误伤）。
    //    行为恒等的实证面 = t1050 家族（12 门）+ t1052a 三相全绿（全矩阵在案）。
    runLegMulti({ "t1054b sneakPlaceBlock conjunction deduped into heldPlaceableSneak helper (review0916"
        " #11): the shift-and-held-placeable-block conjunction has exactly one authority (the helper de"
        "finition returning m_keys.value(Key_Shift) && m_selectedBlock != Air), both former handwritten"
        " copies (the m_hasHit 12-gate criterion and the chest-cart open bypass) read the helper, and t"
        "he old literal call-site conjunction lines are gone (inverted pin: their presence fails)diag "
        "pins=%1 legacyGone=%2" }, [&]() {
        const QString rootT54b = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        const QStringList missT54b = pinSet(rootT54b + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"t1054 helper definition",
             "bool PlayerController::heldPlaceableSneak() const"},
            {"t1054 helper conjunction authority",
             "return m_keys.value(Qt::Key_Shift) && m_selectedBlock != BlockRegistry::Air;"},
            {"t1054 both call sites read helper",
             "const bool sneakPlaceBlock = heldPlaceableSneak();", 2},
        });
        // 反探：旧手写合取调用行必须消失（helper 化后两处副本退役；miss 非空 = needle 不存在 = 合规）。
        const QStringList legacyT54b = pinSet(rootT54b + QStringLiteral("/src/Game/playercontroller.cpp"), {
            {"t1054 legacy handwritten conjunction must be gone",
             "const bool sneakPlaceBlock = m_keys.value(Qt::Key_Shift)"},
        });
        const bool legacyGoneT54b = !legacyT54b.isEmpty();
        if (!missT54b.isEmpty())
            qInfo().noquote() << "  [t1054b diag] pins" << missT54b.join(QLatin1Char(','));
        if (!legacyGoneT54b)
            qInfo().noquote() << "  [t1054b diag] legacy conjunction line still present";
        const bool okT54b = missT54b.isEmpty() && legacyGoneT54b;
        if (!okT54b) ++totalFail;
        qInfo().noquote() << (okT54b ? "PASS" : "FAIL")
                          << "| t1054b sneakPlaceBlock conjunction deduped into heldPlaceableSneak "
                             "helper: one conjunction authority, both call sites read the helper, "
                             "legacy handwritten copies gone (behavioral identity evidenced by the "
                             "t1050 family + t1052a staying green)"
                          << (okT54b ? QString()
                                     : QStringLiteral("diag pins=%1 legacyGone=%2")
                                           .arg(missT54b.isEmpty()).arg(legacyGoneT54b));
    });

    // ── P-t1046c 天气剩余时长持久化 + 精确续跑（R19.23 t1046 低-5；MC level.dat RainTime/ThunderTime 口径）──
    //    (a) 真 WorldStore：快照携 weatherTimerMs=77777 落 world_meta（weather_timer_ms）→ 关库重开逐键
    //        相等；(b) 旧档形态（四参 saveAll）→ hasWeatherTimer=false / weatherTimerMs=0 缺省；
    //    (c) World 续跑：setWeatherState 设态 + setWeatherRemainingSec 覆盖剩余窗 → tickWeather 部分
    //        推进剩余精确递减 → 到点翻 Clear 重抽新窗；sec<=0 拒（tickWeather 前置不变量；
    //        t1055 D 起拒面带 qInfo 诊断——计时不变行为面本腿照钉）；
    //    (d) 源钉：两 C++ 入口声明 + 存/读两侧键字面量（阴性轮敏感）。
    runLegMulti({ "t1046c weather remaining-window persistence (MC RainTime/ThunderTime caliber, parity low-5): a s"
        "ave carrying weatherTimerMs stores it as world_meta weather_timer_ms inside the same transaction"
        " and a close/reopen round-trips it exactly while a legacy save without the key defaults hasWeath"
        "erTimer=false; the restore path setWeatherState+setWeatherRemainingSec resumes the archived wind"
        "ow precisely (0.8s minus a 0.3s tick leaves 0.5s), the expiry flips to Clear with a fresh random"
        " window, and non-positive seconds are silently rejected (tickWeather positive-timer invariant); "
        "source pins lock both C++ entries and the store/load key literals (negative-round sensitive)diag"
        " a=%1 b=%2 c=%3 d=%4" }, [&]() {
        World wT46c;
        wT46c.setWidth(48); wT46c.setDepth(48); wT46c.setHeight(96); wT46c.setSeed(1046);
        WorldStore storeT46c;
        storeT46c.setWorld(&wT46c);
        bool okA = false, okB = false, okC = false, okD = false;
        // (a) 带键 round-trip。
        const QString dbT46c = QDir::temp().absoluteFilePath(
                QStringLiteral("voxel_t1046c_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT46c);
        {
            QVariantMap wt;
            wt.insert(QStringLiteral("weather"), 1); // Rain
            wt.insert(QStringLiteral("weatherTimerMs"), qlonglong(77777));
            okA = storeT46c.openWorld(dbT46c)
                && storeT46c.saveAll(QStringLiteral("t1046c"), QVariantList(), QVariantList(), QVariantList(), wt);
            storeT46c.closeWorld();
            QVariantMap back;
            if (okA && storeT46c.openWorld(dbT46c)) back = storeT46c.loadWorldTime();
            storeT46c.closeWorld();
            okA = okA && back.value(QStringLiteral("weather")).toInt() == 1
                && back.value(QStringLiteral("hasWeatherTimer")).toBool() == true
                && back.value(QStringLiteral("weatherTimerMs")).toLongLong() == 77777;
            if (!okA)
                qInfo().noquote() << "  [t1046c diag a] back =" << back;
        }
        // (b) 旧档缺键 → 缺省（hasWeatherTimer false / weatherTimerMs 0）。
        {
            const QString dbOld = QDir::temp().absoluteFilePath(
                    QStringLiteral("voxel_t1046cold_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
            QFile::remove(dbOld);
            const bool built = storeT46c.openWorld(dbOld)
                && storeT46c.saveAll(QStringLiteral("t1046cold")); // 四参旧调用形态：不写时间键
            storeT46c.closeWorld();
            QVariantMap back;
            if (built && storeT46c.openWorld(dbOld)) back = storeT46c.loadWorldTime();
            storeT46c.closeWorld();
            okB = built && back.value(QStringLiteral("hasWeatherTimer")).toBool() == false
                && back.value(QStringLiteral("weatherTimerMs")).toLongLong() == 0;
            if (!okB)
                qInfo().noquote() << "  [t1046c diag b] built" << built << "back =" << back;
            QFile::remove(dbOld);
        }
        // (c) World 精确续跑 + 非法拒。
        {
            wT46c.setWeatherState(1);                    // Rain（随机窗重抽）
            wT46c.setWeatherRemainingSec(0.8f);          // 覆盖为存档剩余窗
            const bool setOk = wT46c.weatherState() == 1
                && std::abs(wT46c.weatherRemainingSec() - 0.8f) < 1e-4f;
            wT46c.setWeatherRemainingSec(0.0f);          // 非法：拒（计时不变；t1055 D 起带 qInfo 诊断）
            wT46c.setWeatherRemainingSec(-2.0f);         // 非法：拒（计时不变；t1055 D 起带 qInfo 诊断）
            const bool gateOk = std::abs(wT46c.weatherRemainingSec() - 0.8f) < 1e-4f;
            wT46c.tickWeather(0.3);                      // 部分推进 → 剩余 0.5 精确递减
            const bool stepOk = wT46c.weatherState() == 1
                && std::abs(wT46c.weatherRemainingSec() - 0.5f) < 1e-3f;
            wT46c.tickWeather(0.5);                      // 到点 → 翻 Clear + 重抽新窗
            const bool flipOk = wT46c.weatherState() == 0
                && wT46c.weatherRemainingSec() > 0.0f
                && wT46c.weatherRemainingSec() <= 120.0f;
            okC = setOk && gateOk && stepOk && flipOk;
            if (!okC)
                qInfo().noquote() << "  [t1046c diag c] setOk" << setOk << "gateOk" << gateOk
                                  << "stepOk" << stepOk << "flipOk" << flipOk
                                  << "state" << wT46c.weatherState()
                                  << "remain" << wT46c.weatherRemainingSec();
        }
        // (d) 源钉（两 C++ 入口 + 存/读两侧键）。
        {
            const QString rootT46c = QDir(QCoreApplication::applicationDirPath()
                                          + QStringLiteral("/..")).absolutePath();
            QStringList missT46c;
            missT46c << pinSet(rootT46c + QStringLiteral("/src/World/world.h"), {
                {"hdr-remain-get", "Q_INVOKABLE float weatherRemainingSec() const { return m_weatherTimer; }"},
                {"hdr-remain-set", "Q_INVOKABLE void setWeatherRemainingSec(float seconds);"},
            });
            missT46c << pinSet(rootT46c + QStringLiteral("/src/World/worldstore.cpp"), {
                {"cpp-store-timer-key", "metas.append({QStringLiteral(\"weather_timer_ms\"),"},
                {"cpp-load-timer-key", "meta.contains(QStringLiteral(\"weather_timer_ms\"))"},
            });
            okD = missT46c.isEmpty();
            if (!okD)
                qInfo().noquote() << "  [t1046c diag d] pin miss:" << missT46c.join(QLatin1Char(','));
        }
        QFile::remove(dbT46c);
        if (!okA) ++totalFail;
        if (!okB) ++totalFail;
        if (!okC) ++totalFail;
        if (!okD) ++totalFail;
        qInfo().noquote() << (okA && okB && okC && okD ? "PASS" : "FAIL")
                          << "| t1046c weather remaining-window persistence (MC RainTime/ThunderTime"
                             " caliber, parity low-5): a save carrying weatherTimerMs stores it as"
                             " world_meta weather_timer_ms inside the same transaction and a"
                             " close/reopen round-trips it exactly while a legacy save without"
                             " the key defaults hasWeatherTimer=false; the restore path"
                             " setWeatherState+setWeatherRemainingSec resumes the archived window"
                             " precisely (0.8s minus a 0.3s tick leaves 0.5s), the expiry flips"
                             " to Clear with a fresh random window, and non-positive seconds are"
                             " silently rejected (tickWeather positive-timer invariant); source"
                             " pins lock both C++ entries and the store/load key literals"
                             " (negative-round sensitive)"
                          << (okA && okB && okC && okD
                                  ? QString()
                                  : QStringLiteral("diag a=%1 b=%2 c=%3 d=%4")
                                        .arg(okA).arg(okB).arg(okC).arg(okD));
    });

    // ── P-t1046d 小麦种子基准钉死 0-3（R19.23 t1046 低-4；~Beta/1.0 口径，多株实测逐株带内）──
    //    真玩家路径逐株收割 12 株成熟作物：每株恰 1 小麦 + 0-3 种子（0 种合法 → 该株单件弹落）。
    //    逐株断言全带内（确定性）；0-3 精确口径由 t1026b 的 cpp-crop-drop-seed 源钉锁（阴性轮
    //    改回 bounded(1,4) → 钉红恰面）。
    runLegMulti({ "t1046d mature wheat seed caliber pinned 0-3 (Beta/1.0 baseline, parity low-4): harvesting twelve"
        " mature crops through the real player path yields exactly one wheat each plus a per-crop seed co"
        "unt strictly inside {0,1,2,3} where a zero-seed crop legally emits no seed item (drop set is 1 o"
        "r 2 items accordingly); the exact bounded(0,4) caliber and the emit gate are source-pinned in t1"
        "026b (negative-round sensitive: reverting to the retired 1-3 range flips the t1026b seed pin)dia"
        "g band=%1 wheat=%2 shape=%3 plots=%4 badSeeds=%5 badPlot=%6" }, [&]() {
        World wT46d;
        wT46d.setWidth(64); wT46d.setDepth(48); wT46d.setHeight(96); wT46d.setSeed(10462);
        Hotbar hbT46d;
        PlayerController pcT46d;
        pcT46d.setWorld(&wT46d);
        pcT46d.setHotbar(&hbT46d);
        QQuickWindow winT46d;
        pcT46d.setParentItem(winT46d.contentItem());
        pcT46d.grab();
        pcT46d.setSelectedBlock(int(BR::Air)); // 空手（t1040 rig 加固同式）
        // rig：y=14 Planks 地台 + y=15 耕地行（间距 4）+ y=16 成熟作物（stage 7 直写，绕生长链）。
        QVector<int> plotXs;
        for (int x = 8; x <= 52 && plotXs.size() < 12; x += 4) plotXs.push_back(x);
        for (int x = 6; x <= 56; ++x)
            for (int z = 12; z <= 20; ++z) {
                for (int y = 15; y <= 20; ++y) wT46d.setBlock(x, y, z, BR::Air, 0);
                wT46d.setBlock(x, 14, z, BR::Planks, 0);
            }
        for (int px : plotXs) {
            wT46d.setBlock(px, 15, 16, BR::Farmland, 0);
            wT46d.setBlock(px, 16, 16, BR::WheatCrop, BR::WheatCropStageMax);
        }
        QVector<int> dropIdD, dropCntD;
        const QMetaObject::Connection dropConnD = QObject::connect(
            &pcT46d, &PlayerController::spawnItem, &pcT46d,
            [&](int, int, int, int id, int count, const QVariantList &, const QString &, int) {
                dropIdD.push_back(id);
                dropCntD.push_back(count);
            });
        // 瞄准 / 挖掘帮手（t1026a 同款：墙钟 dt 喂 updateMining，瞬破门槛 0.05s）。
        const auto aimT46d = [&](float feetX, float feetZ, float aimX, float aimY, float aimZ) {
            const float ex = feetX, ey = 16.0f + 1.62f, ez = feetZ;
            const float dx = aimX - ex, dy = aimY - ey, dz = aimZ - ez;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float pit = std::asin(dy / len) * 57.2957795f;
            const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
            pcT46d.release();
            pcT46d.grab();
            pcT46d.loadSavedState(feetX, 16.0f, feetZ, yaw, pit, 2 /* Survival */);
            pcT46d.tick();
            return pcT46d.hitBlock();
        };
        const auto mineBlockT46d = [&](int bx, int by, int bz) {
            pcT46d.beginMining();
            for (int i = 0; i < 6000 && wT46d.blockAt(bx, by, bz) != BR::Air; ++i) {
                QElapsedTimer dtw;
                dtw.start();
                while (dtw.elapsed() < 17)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                pcT46d.tick();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            }
            pcT46d.endMining();
        };
        bool allInBand = true, allWheatOne = true, dropShapeOk = true;
        int badSeeds = -1, badPlot = -1;
        for (int px : plotXs) {
            const QVector3D hit = aimT46d(float(px) + 3.0f, 16.5f, float(px) + 0.5f, 16.5f, 16.5f);
            dropIdD.clear();
            dropCntD.clear();
            mineBlockT46d(px, 16, 16);
            int wheatN = 0, seeds = 0;
            for (int i = 0; i < dropIdD.size(); ++i) {
                if (dropIdD[i] == RecipeRegistry::WheatId && dropCntD[i] == 1) ++wheatN;
                if (dropIdD[i] == RecipeRegistry::SeedId) seeds += dropCntD[i];
            }
            const bool shapeOk = hit == QVector3D(px, 16, 16)
                && wT46d.blockAt(px, 16, 16) == BR::Air
                && (dropIdD.size() == 1 || dropIdD.size() == 2)
                && (seeds == 0 ? dropIdD.size() == 1 : dropIdD.size() == 2);
            if (!shapeOk || wheatN != 1) { dropShapeOk = dropShapeOk && shapeOk; allWheatOne = allWheatOne && wheatN == 1; badPlot = px; }
            if (seeds < 0 || seeds > 3) { allInBand = false; badSeeds = seeds; badPlot = px; }
        }
        QObject::disconnect(dropConnD);
        pcT46d.release();
        winT46d.deleteLater();
        const bool ok = allInBand && allWheatOne && dropShapeOk && plotXs.size() == 12;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t1046d diag] allInBand" << allInBand << "allWheatOne" << allWheatOne
                              << "dropShape" << dropShapeOk << "plots" << plotXs.size()
                              << "badSeeds" << badSeeds << "badPlot" << badPlot;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1046d mature wheat seed caliber pinned 0-3 (Beta/1.0 baseline, parity"
                             " low-4): harvesting twelve mature crops through the real player path"
                             " yields exactly one wheat each plus a per-crop seed count strictly"
                             " inside {0,1,2,3} where a zero-seed crop legally emits no seed item"
                             " (drop set is 1 or 2 items accordingly); the exact bounded(0,4)"
                             " caliber and the emit gate are source-pinned in t1026b"
                             " (negative-round sensitive: reverting to the retired 1-3 range flips"
                             " the t1026b seed pin)"
                          << (ok ? QString()
                                  : QStringLiteral("diag band=%1 wheat=%2 shape=%3 plots=%4 badSeeds=%5 badPlot=%6")
                                        .arg(allInBand).arg(allWheatOne).arg(dropShapeOk)
                                        .arg(plotXs.size()).arg(badSeeds).arg(badPlot));
    });

    // ── P-t1046e 轨道骑士 500m 单方向径向位移达阈 + 发明成就原创标注（R19.23 t1048 勘误；MC On A Rail
    //    真口径 = 乘矿车到达距乘车起点单方向 ≥500 米的一点，review0913-A P2-1；t1046 旧「累计 1km」退役）──
    //    (a) 单方向口径钉死：起点记录后掉头折返——累计里程 600 ≥ 500 而径向位移 0 → 仍锁（非累计制）；
    //        径向位移恰 500 达阈解锁（≥ 缝；缝值全程半精度可表示：300/0/99.5/400.5/500 全精确无 FP 噪声）；
    //        起点未记录喂远点不伪解锁（判定窗门）；负增量对里程统计防御忽略、径向采样照走；flush 后统计精确；
    //    (b) 存档 round-trip：里程统计 + 解锁态恢复、不重发 toast（径向起点不入档，解锁态入档）；
    //    (c) 描述钉：ride_minecart = 500m 单方向口径不带原创；发明九项（首杀四 + 进结构四 + 箱车）抽查
    //        三项尾「（原创）」+ MC 同型两反例不带（余六项由 achievements() 描述同源生成面覆盖——UI 单一
    //        来源，如实 scoped，review0913-B P3-2）；
    //    (d) 源钉（定义行 + 埋点声明 ×2 + 阈值常量 + QML 起点捕获 / 位置路由行）。
    runLegMulti({ "t1046e rail-knight 500m single-direction radial ride + originality tags (MC On A Rail caliber, t"
        "1048 erratum of the retired 1km cumulative reading): the ride origin is captured at the mount ed"
        "ge and each onMinecartMoved sample judges radial displacement from it - turning back after 600 c"
        "umulative blocks leaves radial 0 and stays locked (proving non-cumulative), unlocking fires exac"
        "tly at radial 500 (>= seam on half-precision-exact values), repeats past threshold and negative "
        "deltas stay idempotent/ignored for the stat while radial sampling continues, an unrecorded origi"
        "n never unlocks spuriously, the flushed statistic reads back 1600 and survives a save->load roun"
        "d-trip without re-toasting; the ride_minecart description states the 500m single-direction calib"
        "er without the originality tag while the nine project-invented achievements (per-species first k"
        "ills, structure entries, chest cart) carry the (original) suffix (three spot-checked) and MC-cou"
        "nterpart ones do not; source pins lock the def rows, both invokables, the threshold and the QML "
        "origin/route lines (negative-round sensitive)diag under=%1 notCum=%2 near=%3 unlock=%4 idem=%5 g"
        "uard=%6 travel=%7 load=%8 desc=%9 pins=%10" }, [&]() {
        PlayerProgress progT46e;
        int toastT46e = 0;
        QObject::connect(&progT46e, &PlayerProgress::achievementUnlocked, &progT46e,
                         [&](const QString &, const QString &, const QString &) { ++toastT46e; });
        progT46e.onMinecartRideStarted(0.0, 0.0);
        progT46e.onMinecartMoved(300.0f, 300.0, 0.0);   // 径向 300，累计 300
        const bool underOk = !progT46e.isUnlocked(QStringLiteral("ride_minecart")) && toastT46e == 0;
        progT46e.onMinecartMoved(300.0f, 0.0, 0.0);     // 掉头回起点：累计 600 ≥ 500 但径向 0 → 非累计制钉死
        const bool notCumulativeOk = !progT46e.isUnlocked(QStringLiteral("ride_minecart")) && toastT46e == 0;
        progT46e.onMinecartMoved(99.5f, 99.5, 0.0);     // 径向 99.5，累计 699.5 → 仍锁
        const bool nearOk = !progT46e.isUnlocked(QStringLiteral("ride_minecart")) && toastT46e == 0;
        progT46e.onMinecartMoved(400.5f, 500.0, 0.0);   // 径向恰 500 → 达阈（≥ 缝）
        const bool unlockOk = progT46e.isUnlocked(QStringLiteral("ride_minecart")) && toastT46e == 1;
        progT46e.onMinecartMoved(500.0f, 0.0, 0.0);     // 回起点：径向 0、累计 1100——unlock 幂等不再 toast
        progT46e.onMinecartMoved(-5.0f, -5.0, 0.0);     // 负增量：统计不计，径向采样照走
        const bool idemOk = progT46e.isUnlocked(QStringLiteral("ride_minecart")) && toastT46e == 1;
        progT46e.onPlayTimeTick(0.6f);    // > kFlushInterval → flush 累积入统计
        const double travelE = progT46e.minecartTravelBlocks();
        const bool travelOk = std::abs(travelE - 1600.0) < 1e-6;
        // (a') 起点未记录（fresh 实例直接喂远点）→ 不得对 (0,0) 起算伪解锁（判定窗门敏感）。
        PlayerProgress progT46eGuard;
        progT46eGuard.onMinecartMoved(1000.0f, 100000.0, 0.0);
        const bool guardOk = !progT46eGuard.isUnlocked(QStringLiteral("ride_minecart"));
        // (b) 持久化 round-trip。
        PlayerProgress progT46eLoad;
        int toastT46eLoad = 0;
        QObject::connect(&progT46eLoad, &PlayerProgress::achievementUnlocked, &progT46eLoad,
                         [&](const QString &, const QString &, const QString &) { ++toastT46eLoad; });
        progT46eLoad.loadVariant(progT46e.toVariant());
        const bool loadOk = progT46eLoad.isUnlocked(QStringLiteral("ride_minecart"))
            && std::abs(progT46eLoad.minecartTravelBlocks() - 1600.0) < 1e-6
            && toastT46eLoad == 0;
        // (c) 描述钉（achievements() 单源 = toast / UI 同源）。
        QString rideDesc, woodDesc, fishDesc, spiderDesc, dungeonDesc, cartDesc;
        int defsT46e = 0;
        for (const QVariant &v : progT46e.achievements()) {
            const QVariantMap m = v.toMap();
            ++defsT46e;
            const QString id = m.value(QStringLiteral("id")).toString();
            if (id == QLatin1String("ride_minecart")) rideDesc = m.value(QStringLiteral("desc")).toString();
            if (id == QLatin1String("get_wood")) woodDesc = m.value(QStringLiteral("desc")).toString();
            if (id == QLatin1String("first_catch")) fishDesc = m.value(QStringLiteral("desc")).toString();
            if (id == QLatin1String("kill_spider")) spiderDesc = m.value(QStringLiteral("desc")).toString();
            if (id == QLatin1String("entered_dungeon")) dungeonDesc = m.value(QStringLiteral("desc")).toString();
            if (id == QLatin1String("chest_cart_loot")) cartDesc = m.value(QStringLiteral("desc")).toString();
        }
        const QString tag = QString::fromUtf8("（原创）");
        const bool descOk = defsT46e == 31
            && rideDesc == QString::fromUtf8("乘矿车到达距乘车点 500 米外的位置") // MC On A Rail 同型 → 不标原创
            && !woodDesc.endsWith(tag) && !fishDesc.endsWith(tag)          // MC 同型（Delicious Fish）不标
            && spiderDesc.endsWith(tag) && dungeonDesc.endsWith(tag) && cartDesc.endsWith(tag); // 发明项标
        // (d) 源钉（定义行 + 埋点声明 ×2 + 阈值常量 + 标注字面量 + QML 起点/位置路由行）。
        const QString rootT46e = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/..")).absolutePath();
        QStringList missT46e;
        missT46e << pinSet(rootT46e + QStringLiteral("/src/Game/playerprogress.cpp"), {
            {"def-ride-500m", "乘矿车到达距乘车点 500 米外的位置"},
            {"def-tag-spider", "首次击杀蜘蛛（原创）"},
            {"def-tag-dungeon", "发现了藏在地底的怪物房间（原创）"},
            {"def-tag-cart", "打开装货的矿车取走物品（原创）"},
        });
        missT46e << pinSet(rootT46e + QStringLiteral("/src/Game/playerprogress.h"), {
            {"hdr-invokable-minecartMoved", "Q_INVOKABLE void onMinecartMoved(float deltaBlocks, qreal x, qreal z);"},
            {"hdr-invokable-rideStarted", "Q_INVOKABLE void onMinecartRideStarted(qreal x, qreal z);"},
            {"hdr-minecart-goal", "static constexpr qreal kMinecartRideGoal = 500.0;"},
        });
        missT46e << pinSet(rootT46e + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-route-rideStarted", "progress.onMinecartRideStarted(player.position.x, player.position.z)"},
            {"qml-route-minecartMoved", "if (ridingCart) progress.onMinecartMoved(deltaBlocks, player.position.x, player.position.z)"},
        });
        const bool pinsE = missT46e.isEmpty();
        if (!pinsE)
            qInfo().noquote() << "  [t1046e diag] pin miss:" << missT46e.join(QLatin1Char(','));
        const bool ok = underOk && notCumulativeOk && nearOk && unlockOk && idemOk && guardOk
            && travelOk && loadOk && descOk && pinsE;
        if (!ok) ++totalFail;
        if (!ok)
            qInfo().noquote() << "  [t1046e diag] under" << underOk << "notCum" << notCumulativeOk
                              << "near" << nearOk << "unlock" << unlockOk << "idem" << idemOk
                              << "guard" << guardOk << "travel" << travelOk << travelE
                              << "load" << loadOk << "desc" << descOk << "pins" << pinsE
                              << "ride=" << rideDesc << "spider=" << spiderDesc;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t1046e rail-knight 500m single-direction radial ride + originality tags"
                             " (MC On A Rail caliber, t1048 erratum of the retired 1km cumulative"
                             " reading): the ride origin is captured at the mount edge and each"
                             " onMinecartMoved sample judges radial displacement from it - turning"
                             " back after 600 cumulative blocks leaves radial 0 and stays locked"
                             " (proving non-cumulative), unlocking fires exactly at radial 500"
                             " (>= seam on half-precision-exact values), repeats past threshold"
                             " and negative deltas stay idempotent/ignored for the stat while"
                             " radial sampling continues, an unrecorded origin never unlocks"
                             " spuriously, the flushed statistic reads back 1600 and survives a"
                             " save->load round-trip without re-toasting; the ride_minecart"
                             " description states the 500m single-direction caliber without the"
                             " originality tag while the nine project-invented achievements"
                             " (per-species first kills, structure entries, chest cart) carry the"
                             " (original) suffix (three spot-checked) and MC-counterpart ones do"
                             " not; source pins lock the def rows, both invokables, the threshold"
                             " and the QML origin/route lines (negative-round sensitive)"
                          << (ok ? QString()
                                  : QStringLiteral("diag under=%1 notCum=%2 near=%3 unlock=%4 idem=%5"
                                                   " guard=%6 travel=%7 load=%8 desc=%9 pins=%10")
                                        .arg(underOk).arg(notCumulativeOk).arg(nearOk).arg(unlockOk)
                                        .arg(idemOk).arg(guardOk).arg(travelOk).arg(loadOk)
                                        .arg(descOk).arg(pinsE));
    });
}
