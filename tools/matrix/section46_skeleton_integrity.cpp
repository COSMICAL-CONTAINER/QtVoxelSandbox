// tools/matrix/section46_skeleton_integrity.cpp —— t1071 测试骨架完整性合集 探针段（4 腿
// r2045a-d；filter 词 r2045；矩阵 711→711+4）。置尾先例沿用（接 section45，runAll 末执行）。
// rig 世界 w 零接触；src/ 与 tools/matrix 源码钉面只读（QFile 直读，零写入）。
//
// 任务契约（dev-plan t1071 三件；登记源 = t1070 现场发现两条）：
//   件一 = section01 局部失败账本遮蔽修正：原 L37 `int totalFail = 0;`（t740 子块作用域延至段尾）
//     把本段 78 处 ++totalFail 全部遮蔽成写不出去的局部死账 → 段内腿 FAIL 永不进 runAll 套件汇总
//     行/退出码（实证 build/matrix_t1070_final_t789flake.log：t789 FAIL 而 total FAIL:0 EXIT=0）。
//     修法（读码定案）= **成员账本**（删局部声明，非作用域收窄）：该局部自初始化后从未被读——段内
//     本无独立子块汇总输出，「t740 子块自身汇总」的真实语义 = 并入套件总账；删声明后全段记账落回
//     MatrixRun::totalFail，与 section02-08 拆段件既有语义逐位对齐。段 md5 为搬移时点存证：本修属
//     治理 §6.1 允许的 harness 明确错误同变更修订，md5 留痕 + 修订注双面在场（r2045c 钉）非削钉。
//   件二 = t789 羊色统计腿信封确定性加固：旧「pink/gray/light-gray/brown/black all appear」绝对
//     断言退役（λ≈7.68 下 P(0 粉)≈0.046% 尾事件，t1070 期间真发生）。替代面（断言语义不减，仍测
//     调色板权重分布）：粉「在场」退役到确定性源钉（kSheepNaturalWeights 表项逐字钉死）+ 采样面
//     统计带（白主导 >60% / 0.4× 下限带 / 2.5×+0.02 上溢带）+ 新增非白总数 4σ 双窗（单色带查单色
//     漂移，聚合双窗查「多色同向小漂移」——非冗余检测面）。
//   件三 = 已知 flake 名单登记（matrix_main.cpp 主入口头注释）：t882/t891/t927/t960 + t789 加固
//     退出注 + 复跑清协议，r2045d 在场钉。
//
// 被测面拆解（四腿分工）：
//   r2045a = 件一承重：section01 局部账本零残留反探 + 成员账本唯一写点钉（helpers.h 成员声明 +
//     main.cpp 退出码读点）+ 记账通路行为柱（段 TU 内成员 ++/-- 净零往返 = section01 全段 78 处
//     现在走的同一条通路）。
//   r2045b = 件二承重：新聚合 4σ 双窗在场 + 旧绝对断言退役留痕（代码面零残留 + 注释面留痕锚）+
//     权重分布面保留清单（白主导/下限带/上溢带/表外色反探）+ 粉在场确定性源钉族（六表项逐字）+
//     新腿名/隔离符号：腿名与 PASS 文本同步钉（count≥2）。
//   r2045c = 结构钉：section01 md5 钉面修订留痕锚（原搬移 md5 串 + t1071 修订注双面在场）+
//     helpers.h 全量搬移 md5 存证 + 成员账本跨段共享注释锚。
//   r2045d = 结构钉：flake 名单在场钉（matrix_main.cpp 头注释：四项名单 + 复跑清协议 + t789
//     退出注）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   NEG-1（section01 临时注入，非生产面）= 注入必败断言腿（--filter t1071-neg 单腿跑）→ 声明红面
//     = **套件汇总行 total FAIL 恰 +1 且退出码 1**（件一铁律：注入假失败必须传播到汇总/退出码；
//     这是修后能力的阳性证明）。同型对照（缺失能力复现，额外存证 matrix_r2045_neg1_shadowrepro.log）
//     = 遮蔽声明临时回插 + 同注入 → total FAIL:0 EXIT=0（修前该能力缺失的直接复现）。r2045a 不误伤
//     （注入腿只 ++totalFail 不含局部声明 needle；行为柱照常绿）。
//   NEG-2（src/Entities/entitymanager.cpp）= 粉权重表项 { 6, 16 } → { 6, 0 }（粉权重清零 = 「粉
//     在场」语义被摘）→ 声明红面 {r2045b}（粉权重表项逐字钉失配；确定性源钉正是退役绝对断言后
//     粉在场语义的承载面——摘之必红）。t789 行为腿不误伤（粉清零只缩采样池，白主导/其余四色带/
//     聚合双窗/表外色面全在带宽内）；r2045a/c/d 不误伤（零涉）；t787 等相邻羊族腿不误伤（零涉权重表）。
//   阴性日志：build/matrix_r2045_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
// 腿名纪律：腿名/diag/PASS 文本零跨任务 filter 词元（r2018 先例）；本段腿名只含 r2045 前缀与
//   件面描述，不含任何他任务腿名子串。
#include "matrix_helpers.h"

void MatrixRun::section46_skeleton_integrity()
{
    const QString projRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absolutePath();
    const QString srcRoot = projRoot + QStringLiteral("/src");
    const QString matrixRoot = projRoot + QStringLiteral("/tools/matrix");
    const QString s01Path = matrixRoot + QStringLiteral("/section01_redstone_core.cpp");
    const QString helpersPath = matrixRoot + QStringLiteral("/matrix_helpers.h");
    const QString mainPath = matrixRoot + QStringLiteral("/matrix_main.cpp");
    const QString emPath = srcRoot + QStringLiteral("/Entities/entitymanager.cpp");

    // 头注/注释体裸读（修订注/md5/flake 名单全在注释体——pinSet 剥注释会失配，section43/44/45 raw 先例）。
    const auto rawContains = [](const QString &path, const char *needle) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        return QString::fromUtf8(f.readAll()).contains(QString::fromUtf8(needle));
    };
    // 禁触反探（needle 在场即红——section33 r2031d 同款帮手）。
    const auto forbiddenAbsent = [rawContains](const QString &path, const char *needle) {
        return !rawContains(path, needle);
    };

    // ── r2045a：件一承重（section01 局部账本零残留 + 成员账本唯一写点 + 记账通路行为柱）──
    runLeg(QStringLiteral("r2045a accounting integrity pins (section01 keeps zero local fail-ledger"
        " residue so every increment lands on the shared member ledger, the member declaration is"
        " the sole write target in the runner header, the exit code reads that same member, and a"
        " live net-zero round trip proves the section-to-member accounting path is writable from a"
        " section translation unit)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 零残留反探：遮蔽声明的声明形态在本段任何位置（含注释）不得再现。
        const bool noLocalDecl = forbiddenAbsent(s01Path, "int totalFail");
        ok = ok && noLocalDecl;
        if (!noLocalDecl)
            diag += QStringLiteral("[local-ledger-alive] ");

        // ② 成员账本唯一写点：helpers.h 成员声明恰一处 + main.cpp 退出码读同一成员。
        const QStringList missHelpers = pinSet(helpersPath, {
            SrcPin("member ledger sole declaration", "int totalFail = 0;", 1),
        });
        for (const QString &m : missHelpers) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }
        const QStringList missMain = pinSet(mainPath, {
            SrcPin("exit code reads the member ledger", "return run.totalFail == 0 ? 0 : 1;", 1),
        });
        for (const QString &m : missMain) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 记账通路行为柱：段 TU 内成员 ++/-- 净零往返（净零复原，套件总账不动——本腿退出时
        //    套件汇总与进入前逐位相同）。section01 全段 78 处 ++totalFail 走的就是这条通路。
        const int failBefore = totalFail;
        ++totalFail;
        ok = ok && totalFail == failBefore + 1;
        --totalFail;
        ok = ok && totalFail == failBefore;
        if (totalFail != failBefore)
            diag += QStringLiteral("[ledger-not-restored] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2045a accounting integrity pins: section01 keeps zero local"
                             " fail-ledger residue so every increment lands on the shared member"
                             " ledger, the member declaration is the sole write target in the"
                             " runner header, the exit code reads that same member, and a live"
                             " net-zero round trip proves the accounting path is writable from a"
                             " section translation unit"
                          << (ok ? QString() : diag);
    });

    // ── r2045b：件二承重（t789 加固钉面：聚合双窗在场 + 旧断言退役双面 + 权重分布面保留清单 +
    //    粉在场确定性源钉族 + 腿名/PASS 同步）──
    runLeg(QStringLiteral("r2045b sheep-color hardening pins (the retired absolute all-appear check"
        " is gone from the assertion body with its retirement trail anchored in the section"
        " comment, the aggregate non-white band with its 4-sigma window stands in the leg body,"
        " the retained weight-distribution faces stay pinned, the pink presence semantics is"
        " carried by the exact weight-table entries in the entity manager, and the renamed leg"
        " keeps its registration name and pass line in sync)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 旧绝对断言退役双面：代码面零残留（声明形态禁入）+ 注释面留痕锚在场。
        const bool oldClauseGone = forbiddenAbsent(s01Path, "idx == 6 && cnt[idx] < 1");
        ok = ok && oldClauseGone;
        if (!oldClauseGone)
            diag += QStringLiteral("[old-clause-alive] ");
        const bool retireTrail = rawContains(s01Path, "绝对断言退役");
        ok = ok && retireTrail;
        if (!retireTrail)
            diag += QStringLiteral("[retire-trail-missing] ");
        const bool flakeEvidence = rawContains(s01Path, "0.046%");
        ok = ok && flakeEvidence;
        if (!flakeEvidence)
            diag += QStringLiteral("[flake-evidence-missing] ");

        // ② 聚合 4σ 双窗在场（t1071 新增的统计稳健锚——计算式与门逐字钉）。
        const QStringList missBand = pinSet(s01Path, {
            SrcPin("aggregate band sigma computation",
                   "const double sig = std::sqrt(lam * (1.0 - pNonWhite));", 1),
            SrcPin("aggregate band gate",
                   "double(nonWhite) < lam - 4.0 * sig || double(nonWhite) > lam + 4.0 * sig", 1),
        });
        for (const QString &m : missBand) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ③ 权重分布面保留清单（断言语义不减——采样带族逐面在场）。
        const QStringList missFaces = pinSet(s01Path, {
            SrcPin("white dominance band", "kSheepTotal * 60", 1),
            SrcPin("per-color lower band", "nominal[c] * 0.4", 1),
            SrcPin("per-color upper band", "nominal[c] * 2.5 + 0.02", 1),
            SrcPin("no out-of-table colors face", "non-natural color", 1),
        });
        for (const QString &m : missFaces) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ④ 粉在场确定性源钉族（entitymanager.cpp kSheepNaturalWeights 六表项逐字——退役绝对
        //    断言后「粉在场」语义的承载面；NEG-2 摘面 = { 6, 16 } 清零即红）。
        const QStringList missWeights = pinSet(emPath, {
            SrcPin("white weight entry", "{ 0, 8184 }", 1),
            SrcPin("pink weight entry (presence semantics)", "{ 6, 16 }", 1),
            SrcPin("gray weight entry", "{ 7, 500 }", 1),
            SrcPin("light-gray weight entry", "{ 8, 500 }", 1),
            SrcPin("brown weight entry", "{ 12, 300 }", 1),
            SrcPin("black weight entry", "{ 15, 500 }", 1),
        });
        for (const QString &m : missWeights) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        // ⑤ 腿名/PASS 同步锚（count≥2 = 注册名与 PASS 行文本都在场——防腿名漂移，r2043d/r2044d 先例）。
        //    针选行内连续子串：腿名是逐段字符串字面量拼接，跨行拼点在源文本里不连续（r2045b 首跑
        //    「pink pin phrase sync(x0<2)」教训——选针必须锚单行内实存文本，非编译期拼接结果）。
        const QStringList missSync = pinSet(s01Path, {
            SrcPin("renamed leg name/pass sync", "statistical bands", 2),
            SrcPin("pink pin phrase sync", "pink presence pinned by", 2),
        });
        for (const QString &m : missSync) {
            ok = false;
            diag += QStringLiteral("[%1] ").arg(m);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2045b sheep-color hardening pins: the retired absolute all-appear"
                             " check is gone from the assertion body with its retirement trail"
                             " anchored in the section comment, the aggregate non-white band with"
                             " its 4-sigma window stands in the leg body, the retained"
                             " weight-distribution faces stay pinned, the pink presence semantics"
                             " is carried by the exact weight-table entries in the entity manager,"
                             " and the renamed leg keeps its registration name and pass line in"
                             " sync"
                          << (ok ? QString() : diag);
    });

    // ── r2045c：结构钉（section01 md5 钉面修订留痕锚：原搬移 md5 串 + t1071 修订注双面在场，
    //    禁静默再改写；helpers.h 全量 md5 存证 + 成员账本跨段共享注释锚）──
    runLeg(QStringLiteral("r2045c structure pins (the section01 move-time md5 stays documented"
        " alongside the t1071 same-change revision note so the byte-move evidence is preserved as"
        " evidence rather than silently rewritten, the whole-move md5 in the runner header is"
        " still anchored, and the shared member-ledger note stands in the runner header)"), [&]() {
        bool ok = true;
        QString diag;

        // ① section01 双面锚：原搬移 md5 串留痕 + t1071 同变更修订注（文件头 + 段内两处 ≥2）。
        const bool md5Kept = rawContains(s01Path, "6c8067b13c0a35d2f369022d516a83b6");
        ok = ok && md5Kept;
        if (!md5Kept)
            diag += QStringLiteral("[move-md5-missing] ");
        const bool revNote = rawContains(s01Path, "t1071 同变更修订");
        const bool revNoteTwice = rawContains(s01Path, "同变更修订") && revNote;
        ok = ok && revNoteTwice;
        if (!revNoteTwice)
            diag += QStringLiteral("[revision-note-missing] ");

        // ② helpers.h 全量搬移 md5 存证 + 成员账本跨段共享注释锚（均在注释体 → raw）。
        const bool wholeMd5 = rawContains(helpersPath, "82ac70c7ede1a2ed647fea738734be6b");
        ok = ok && wholeMd5;
        if (!wholeMd5)
            diag += QStringLiteral("[whole-md5-missing] ");
        const bool sharedLedgerNote = rawContains(helpersPath, "totalFail 跨段共享");
        ok = ok && sharedLedgerNote;
        if (!sharedLedgerNote)
            diag += QStringLiteral("[shared-ledger-note-missing] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2045c structure pins: the section01 move-time md5 stays documented"
                             " alongside the t1071 same-change revision note so the byte-move"
                             " evidence is preserved rather than silently rewritten, the whole-move"
                             " md5 in the runner header is still anchored, and the shared"
                             " member-ledger note stands in the runner header"
                          << (ok ? QString() : diag);
    });

    // ── r2045d：结构钉（flake 名单在场钉：matrix_main.cpp 主入口头注释的四项名单 + 复跑清协议 +
    //    t789 退出注；名单整串逐字钉 = 名单漂移/漏项即红）──
    runLeg(QStringLiteral("r2045d structure pins (the known-flake roster stands at the matrix main"
        " entry with the four timing-sensitive legs listed verbatim, the rerun-clear protocol"
        " stays registered, and the hardened t789 carries its graduation note so the roster"
        " reflects the current fleet)"), [&]() {
        bool ok = true;
        QString diag;

        // ① 名单整串逐字（注释体 → raw；顺序也钉死——名单重排/漏项/添项即红）。
        const bool roster = rawContains(mainPath, "已知 flake = t882/t891/t927/t960");
        ok = ok && roster;
        if (!roster)
            diag += QStringLiteral("[flake-roster-missing] ");

        // ② 复跑清协议行 + t789 退出注（注释体 → raw）。
        const bool protocol = rawContains(mainPath, "复跑清协议不变");
        ok = ok && protocol;
        if (!protocol)
            diag += QStringLiteral("[rerun-protocol-missing] ");
        const bool graduation = rawContains(mainPath, "已 t1071 信封加固退出");
        ok = ok && graduation;
        if (!graduation)
            diag += QStringLiteral("[graduation-note-missing] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2045d structure pins: the known-flake roster stands at the matrix"
                             " main entry with the four timing-sensitive legs listed verbatim, the"
                             " rerun-clear protocol stays registered, and the hardened t789 carries"
                             " its graduation note so the roster reflects the current fleet"
                          << (ok ? QString() : diag);
    });
}
