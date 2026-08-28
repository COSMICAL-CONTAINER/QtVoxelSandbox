#include "frameprofiler.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutexLocker>

#include <cmath>
#include <vector>

namespace {
Q_LOGGING_CATEGORY(lcProf, "vo.prof") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤 → 落 log 可见
}

// 逐帧桶名表（report 时按序输出，÷ m_frameCount 得 ms/frame）。须与 PlayerController tickImpl
//   插桩用的 Scope 名严格一致（一处拼错 → 该桶 report 恒 0，无副作用但不报数）。
const char *const FrameProfiler::kFramePhases[9] = {
    "env", "item", "xp", "boat", "mob", "pickup", "phys", "ray", "input"
};

FrameProfiler::FrameProfiler()
{
    // QMutex 默认非递归 —— 本类无递归加锁路径（flush 不在持锁时调 add / addSampleMs）。
}

FrameProfiler *FrameProfiler::instance()
{
    static FrameProfiler inst;
    // QQmlEngine 对单例有所有权管理；本进程静态实例寿命长于任何 QML engine，安全。
    // 多次 create() 调用返回同一指针，QML 单例机制允许（jd 注册一次）。
    return &inst;
}

// 单调纳秒（进程启动来累）。RAII Scope 与手动子桶计时共用同一静态 QElapsedTimer → 时间基一致可交叉对照。
//   实现同原 Scope::nowNs（静态 QElapsedTimer 启动即 start），提到 FrameProfiler 级以便手动计时调用。
//   亦被 main.cpp 跨线程读取（render_cpu 计时取 before/afterRendering 时间戳）—— QElapsedTimer
//   ::nsecsElapsed 本身线程安全（只读静态计时器），无需加锁。
qint64 FrameProfiler::nowNs()
{
    static QElapsedTimer t;
    if (!t.isValid()) t.start();
    return t.nsecsElapsed();
}

void FrameProfiler::add(const char *name, qint64 ns)
{
    QMutexLocker lock(&m_mutex);
    m_ns[name] += ns;
}

void FrameProfiler::count(const char *name)
{
    QMutexLocker lock(&m_mutex);
    m_counts[name] += 1;
}

// t935：count 的批量版（+n）。与 count() 共用 m_counts / m_mutex（跨线程安全口径一致）。
void FrameProfiler::addCount(const char *name, qint64 n)
{
    if (n <= 0) return; // 零增量不加键（保持「无事件 = 表无键」的读侧口径）
    QMutexLocker lock(&m_mutex);
    m_counts[name] += n;
}

// t933：读当前窗口计数（不清窗）。锁内查表；不存在 → 0。探针在事件前后各取一次快照做差分，
//   不受 flush 清窗影响的前提是探针运行期间无 60-tick flush（矩阵探针无 PlayerController tick →
//   flush 不触发；GUI 下探针短窗口内 flush 至多把差分切到新窗，行为退化而非错报，可接受）。
qint64 FrameProfiler::countValue(const char *name) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_counts.find(name);
    return it == m_counts.end() ? 0 : it->second;
}

// t500 QML 侧样本入口 / perf-t520 C++ 侧 frameSwapped + beforeRendering/afterRendering 推样本入口：
//   name → ns 累加进 m_ns（同 add 路径，但 name 非 const char* 字面量而是 QString → 转 std::string 做
//   unordered_map 键；QML 50Hz / frameSwapped 60Hz / before-afterRendering 60Hz 调用，开销可忽略）。
//   跨线程安全：加 QMutex —— render_cpu 由渲染线程发射（main.cpp 用 DirectConnection）、main_total 由
//   GUI 线程发射 → 两个线程并行写 m_ns 不加锁会 hash race 崩 / 数字乱跳。
void FrameProfiler::addSampleMs(const QString &name, double ms)
{
    if (ms <= 0.0) return; // 防 0 / 负值（Date.now 精度噪声 / 首帧边界）；被忽略样本不计数（见 .h t934 注释）
    QMutexLocker lock(&m_mutex);
    m_ns[name.toStdString()] += qint64(ms * 1e6);
    m_counts["cnt:" + name.toStdString()] += 1; // t934 样本计数（每窗各桶被推样本次数；报告拼 (N)）
}

qint64 FrameProfiler::bucketLocked(const char *name) const
{
    auto it = m_ns.find(name);
    return it == m_ns.end() ? 0 : it->second;
}

// 格式化窗口报告：逐帧桶报 ms/frame、窗口桶报总 ms、mesh 附 rebuild 次数、world 各 tick 拆分。
//   perf-t520 加 frame 行（main_total / render_cpu，按 ms/frame 报）—— 区分 GUI 主线程 vs 渲染线程瓶颈。
//   发完清窗口桶 / 帧数，保 m_report + m_lastFrames 给 F3 绑定读（直到下次 flush）。
void FrameProfiler::flush()
{
    QMutexLocker lock(&m_mutex);

    const int frames = m_frameCount > 0 ? m_frameCount : 1; // 防 0 除（窗口内无 tick 帧 → 用 1 兜底）
    const double f = frames;

    // 逐帧桶 ms/frame：tickImpl 内各阶段平均每帧耗时。
    std::vector<double> fpMs;
    fpMs.reserve(9);
    double simMs = 0.0;
    for (const char *p : kFramePhases) {
        const double ms = double(bucketLocked(p)) / 1e6 / f;
        fpMs.push_back(ms);
        simMs += ms;
    }

    // 窗口桶（事件 / 10Hz，与帧数无关）：mesh 总 ms + rebuild 次数；world tick 各总 ms + 汇总。
    const double meshMs = double(bucketLocked("mesh")) / 1e6;
    const qint64 meshN = [this]() { auto it = m_counts.find("meshN"); return it == m_counts.end() ? 0 : it->second; }();
    // perf：meshN 按重建原因细分（chunkgeometry.cpp buildMesh 按 RebuildReason 计数）—— dirty=编辑/标脏驱动、
    //   sun=太阳步进、water=水翻页。占比最大者即「mesh 风暴」主源（定位脏标记泄漏 / 无谓重建用）。
    auto meshReasonN = [this](const char *key) {
        auto it = m_counts.find(key); return it == m_counts.end() ? 0 : it->second;
    };
    const qint64 meshND = meshReasonN("meshNdirty");
    const qint64 meshNS = meshReasonN("meshNsun");
    const qint64 meshNW = meshReasonN("meshNwater");
    // w 前缀桶：World 10 个 tick 函数（wWater/wLava/wCrop/wSug/wFarm/wSap/wIce/wLeaf/wWeath + t495 wIceMelt）。
    struct WEnt { const char *key; const char *label; };
    static const WEnt wEntries[] = {
        {"wWater", "water"}, {"wLava", "lava"}, {"wCrop", "crop"},
        {"wSug", "sug"}, {"wFarm", "farm"}, {"wSap", "sap"},
        {"wIce", "ice"}, {"wLeaf", "leaf"}, {"wWeath", "wthr"},
        {"wIceMelt", "imelt"}
    };
    double worldMs = 0.0;
    std::vector<double> wMs;
    wMs.reserve(10);
    for (const WEnt &e : wEntries) {
        const double ms = double(bucketLocked(e.key)) / 1e6;
        wMs.push_back(ms);
        worldMs += ms;
    }

    // 格式化（多行，monospace 友好；NaN/极大保护由各 ms 自身 clamp 隐式）。
    QString tickLine = QStringLiteral("tick ms/f: ")
        + "env " + QString::number(fpMs[0], 'f', 2)
        + "  item " + QString::number(fpMs[1], 'f', 2)
        + "  xp " + QString::number(fpMs[2], 'f', 2)
        + "  boat " + QString::number(fpMs[3], 'f', 2)
        + "  mob " + QString::number(fpMs[4], 'f', 2)
        + "  pick " + QString::number(fpMs[5], 'f', 2)
        + "  phys " + QString::number(fpMs[6], 'f', 2)
        + "  ray " + QString::number(fpMs[7], 'f', 2)
        + "  in " + QString::number(fpMs[8], 'f', 2);
    QString winLine = QStringLiteral("win ms: ")
        + "sim " + QString::number(simMs, 'f', 2)
        + "  mesh " + QString::number(meshMs, 'f', 2) + "(" + QString::number(meshN) + "reb"
        + " [" + QString::number(meshND) + "d " + QString::number(meshNS) + "s " + QString::number(meshNW) + "w])"
        + "  world " + QString::number(worldMs, 'f', 2)
        + "  bp " + QString::number(double(bucketLocked("bp")) / 1e6, 'f', 2)
        + "  [wat " + QString::number(wMs[0], 'f', 1)
        + " lav " + QString::number(wMs[1], 'f', 1)
        + " crop " + QString::number(wMs[2], 'f', 1)
        + " sug " + QString::number(wMs[3], 'f', 1)
        + " farm " + QString::number(wMs[4], 'f', 1)
        + " sap " + QString::number(wMs[5], 'f', 1)
        + " ice " + QString::number(wMs[6], 'f', 1)
        + " leaf " + QString::number(wMs[7], 'f', 1)
        + " wthr " + QString::number(wMs[8], 'f', 1)
        + " imelt " + QString::number(wMs[9], 'f', 1) + "]";

    // t933 perf 世界写入 / 重算活动计数行（1s 窗聚合；dev-plan t933「量化每帧 recompute/mesh 重建数」）：
    //   - reflood：光照重 flood 次数（refloodBox 单一漏斗 = 编辑增量 + 爆炸 / 级联批量 + 流体延迟 + 叶衰）。
    //     稳态应恒 0；**换世界后仍非 0 = 有跨世界存活的写入源**（t933 跨世界泄漏的直接判据）。
    //   - ledit：recomputeLightAround 编辑路径调用数（reflood 的编辑分量）。
    //   - casc / gcol / gcell：重力 26 邻域级联扫描数 / 坍落柱数 / 坍落格数（t930 级联风暴强度）。
    //   诊断口径：爆炸帧窗口 casc/gcol/gcell 应一次性尖峰后归零；此后每窗恒 0 = 收敛。若持续非 0 =
    //   非收敛重算循环（用户怀疑「光照一直重建」的定量答案）。
    auto cntN = [this](const char *key) {
        auto it = m_counts.find(key); return it == m_counts.end() ? 0 : it->second;
    };
    QString cntLine = QStringLiteral("act ct: ")
        + "reflood " + QString::number(cntN("refloodN"))
        + "  ledit " + QString::number(cntN("lightEditN"))
        + "  casc " + QString::number(cntN("cascN"))
        + "  gcol " + QString::number(cntN("gravColN"))
        + "  gcell " + QString::number(cntN("gravCellN"))
        + "  (steady=0; nonzero after world switch = cross-world writer)";

    // perf-t520 帧时间分解桶：main_total（frameSwapped 间隔）+ render_cpu（beforeRendering→afterRendering）。
    //   按 ms/frame 报告（÷ frames），与 tick 各阶段同口径。诊断公式：threaded render loop 下
    //   frame ≈ max(main_total, render_cpu)。两者并标注「render_cpu 含 GPU stall 但非真 GPU 时间」。
    auto frameMs = [this, f](const char *key) { return double(bucketLocked(key)) / 1e6 / f; };
    const double mainMs = frameMs("main_total");
    const double renderMs = frameMs("render_cpu");
    const double syncMs = frameMs("qmlSync"); // GUI 线程 QML scene-graph 同步期（Node 树 commit；mob delegate 节点扇出成本藏此）
    // 瓶颈标注：max 一侧标 *（视觉提示「这一侧是瓶颈」），近相等标 ≈。
    const QLatin1String mainTag = (mainMs >= renderMs && mainMs > 0.0) ? QLatin1String("*") : QLatin1String(" ");
    const QLatin1String renderTag = (renderMs > mainMs && renderMs > 0.0) ? QLatin1String("*") : QLatin1String(" ");
    // t934 段格式化："ms(N)" —— N = 本窗该桶被推样本次数（cnt:<name> 计数桶）。动机：四段恒等式
    //   （main ≈ idleA+waitSync+qmlSync+idleB）只在「各段样本数 == main_total 样本数」时可加；拥塞下
    //   animation tick 每事件循环回合一拍而渲染按 vsync 合帧 → waitSync / idleB 可能每渲染帧多样本，
    //   ÷ 同一 frames 的均值不可加（用户实测 main 88.4 vs 四段和 150.4 的机械解释）。N 不齐本身即判据。
    const auto segN = [this, f](const char *key) {
        const double ms = double(bucketLocked(key)) / 1e6 / f;
        auto it = m_counts.find(std::string("cnt:") + key);
        const qint64 n = it == m_counts.end() ? 0 : it->second;
        return QString::number(ms, 'f', 1) + QLatin1Char('(') + QString::number(n) + QLatin1Char(')');
    };
    // t488 perf residual 残留桶：main_total 与「已知桶（sim 逐帧和 + qmlSync）」之差，显式量化主线程帧周期内
    //   没被 sim/qmlSync 覆盖的部分（事件循环 / QML binding 同步外开销 / mesh 重建 / 等渲染线程）。诊断公式：
    //   main ≈ sim + qmlSync + residual。residual 大时对照 render_cpu：
    //     - render_cpu 同量级大 → 主线程在等渲染线程（frameSwapped 被渲染节奏拖晚）→ 瓶颈在渲染侧（draw-call /
    //       GPU 提交），应查渲染开销而非主线程；
    //     - render_cpu 小、residual 仍大 → 主线程有未插桩重活（QML 绑定扇出 / chunk mesh 重建 / 实体 delegate
    //       高水位）→ 去那侧查。residual 可为负（main_total 样本与 tick 帧数不对齐的测量噪声，负值即噪声标志）。
    const double residualMs = mainMs - simMs - syncMs;
    QString frameLine = QStringLiteral("frame ms/f: ")
        + "main" + mainTag + segN("main_total")
        + "  render" + renderTag + segN("render_cpu")
        + "  qmlSync " + segN("qmlSync")
        + "  residual " + QString::number(residualMs, 'f', 1)
        + "  (frame≈max(main,render); main≈sim+qmlSync+residual; residual=未插桩/等渲染; (N)=samples/win)";
    // t904 perf residual 四段归因行：main.cpp 的渲染管线 hook（frameSwapped/afterAnimating/beforeSynchronizing/
    //   afterSynchronizing）把 GUI 线程帧周期切成 idleA / waitSync / qmlSync / idleB 四段（构造上 main_total ≈
    //   四段之和）→ residual ≈ evA + waitSync + idleB（evA = idleA − sim：idleA 内含 16ms 游戏 tick，sim 桶已计，
    //   差值 = QML 绑定求值 / 其它 QML Timer / 事件派发 / 纯空闲）。诊断：
    //     - evA 大 → 主线程有 sim 外未插桩重活（QML 绑定扇出 / delegate 高水位 / 其它 Timer）→ 去 QML 侧查；
    //     - waitSync 大 → GUI 阻塞等渲染线程同步屏障（渲染 / present-vsync 拖慢帧节奏）→ 渲染侧 bound；
    //     - idleB 大 → afterSync 后到 swap 的等待：threaded 循环 = 渲染线程渲染+present（正常 ≈ render_cpu+vsync），
    //       basic 单线程循环 = 渲染本体在 GUI 线程跑（idleB ≈ render_cpu + present）；
    //     - 某段恒 0 且其它段非 0 = 该 hook 未发（basic 循环不发 afterAnimating → evA/waitSync 恒 0 本身即判据）。
    const double idleAMs = frameMs("fIdleA");
    const double evAMs = idleAMs - simMs; // idleA 内 sim 已单列 → 差值 = 非 sim 事件段（可为负 = 测量噪声标志）
    // t934 waitSync 归因行（dev-plan：渲染线程同步等待 76ms 一家独大的解剖）。waitSync = GUI 阻塞等渲染
    //   线程抵达同步屏障；渲染线程要跑完上一帧的 [渲染 pass（render_cpu / RenderStats renderTime）+
    //   present/vsync 阻塞（fPresent 桶）+ 帧尾清理] 才到屏障。render_cpu 小而 waitSync 大时的三个候选汇
    //   （实机判读 = F3 render-side 行 + 本行）：
    //     ① GPU 过载 / present 排队 → F3 render-side 行 gpu（RenderStats.lastCompletedGpuTime，真 GPU ms）
    //       同量级大 + 本行 present 大（fPresent = afterRendering[渲染线程]→frameSwapped[GUI 收到] =
    //       present 阻塞 + 队列派发延迟，main.cpp t934 新增）；
    //     ② 渲染线程 prep / 上传风暴（mesh 重建 → 顶点缓冲重传 / 渲染列表重建，t933 风暴的渲染侧回声）
    //       → F3 render-side 行 prep（RenderStats.renderPrepareTime）大 + win 行 mesh reb 计数非 0；
    //     ③ 渲染合帧 / hook 多发 → 某段 N > main 的 N（恒等式不可加，非独立开销）。
    QString frame2Line = QStringLiteral("frame2 ms/f: ")
        + "evA " + QString::number(evAMs, 'f', 1)
        + "  waitSync " + segN("fWaitSync")
        + "  idleB " + segN("fIdleB")
        + "  present " + segN("fPresent")
        + "  (residual≈evA+waitSync+idleB; evA=idleA−sim=QML绑定/其它Timer/空闲; idleA "
        + QString::number(idleAMs, 'f', 1)
        + "; N不齐=段不可加(合帧/hook多发); waitSync大→看F3 render-side行 gpu/prep + present)";

    // t500 perf mob 子分解（逐帧 ms/f，÷ frames）：mob 桶（PlayerController tickImpl 整段）拆成 mobLoop
    //   （EntityManager::tick）/ mobHostile（tickHostileLife）/ mobSpawn（tickSpawners）三函数，mobLoop 再拆
    //   mobAI（aiTick 节流段：火烧/仙人掌/AI 决策移动）vs mobPhys（= mobLoop − mobAI：每帧段 重力/resting/
    //   flow/knockback/音频/walkPhase）。mobAI 手动 nowNs 计时（跨 continue）；mobPhys 派生免再计时。
    //   诊断 mob 桶瓶颈：mob≈27ms 时看这行即可知「27ms 在 tick 还是 hostileLife、AI-pass 还是物理-pass」。
    auto mobSubMs = [this, f](const char *key) { return double(bucketLocked(key)) / 1e6 / f; };
    const double mobLoopMs = mobSubMs("mobLoop");
    const double mobAiMs = mobSubMs("mobAI");
    // t905 perf mob 段细分：phys（= loop − ai）再拆 head / tail ——
    //   - head = 非 Mob kind 分支（箭 / 雪球 / 铁砧落体 …）+ Mob 入块到 aiT0（dead 倒计时 / 骑乘冻结 / 免疫清零）；
    //   - tail = ai 后每帧段（流推 / 红闪 / 环境音 / 走相 / 击退 / 滑流 / 窒息节流帧）+ resting 复探 / 重力 /
    //     落地扫描尾段；
    //   - ltail = 循环尾（releaseSlot / flushPendingShots / tickBreeding / emit entitiesChanged 的 QML delegate
    //     扇出 —— t935 起粒度化：指纹差分只 bump 可见态真变的槽（emit/bump/fan 三计数见 mob 行尾），扇出
    //     从「47 槽 × ~50 revision 绑定」收口到「bump 槽 × ~50」；行走 mob MobModel 几何重建仅发生在
    //     bump 槽（setWalkPhase 量化 12 腿姿/cycle 不变），与逐实体物理不同成本中心）。
    //   诊断：phys 大时看本行即知「投射物 / 尸体 / 骑乘头段」「活体每帧物理尾段」还是「emit 扇出循环尾」；
    //   尾段大 + stF（下落态 mob-帧数）高 = resting↔下落振荡（每帧重力 + 落地扫描 + dirty bump）。
    const double mobHeadMs = mobSubMs("mobHead");
    const double mobTailMs = mobSubMs("mobTail");
    const double mobLTailMs = mobSubMs("mobLoopTail");
    // t905 perf mob 状态直方图（窗口内 mob-帧 数：每 tick 每活体按当前态 +1）—— stR=resting 静置 / stF=非
    //   resting（重力+落地扫描每帧跑）/ stV=骑乘冻结 / stD=dead 尸体。判「14 活体为何 10ms」先看分布：
    //   stF 高位 = 振荡 / 悬空 mob 主吃尾段；全 stR 而 tail 仍大 = 循环尾（emit/繁殖）或索引态泄漏。
    auto mobCnt = [this](const char *key) {
        auto it = m_counts.find(key); return it == m_counts.end() ? 0 : it->second;
    };
    QString mobLine = QStringLiteral("mob sub ms/f: ")
        + "ai " + QString::number(mobAiMs, 'f', 2)
        + "  phys " + QString::number(mobLoopMs - mobAiMs, 'f', 2)
        + "  [head " + QString::number(mobHeadMs, 'f', 2)
        + " tail " + QString::number(mobTailMs, 'f', 2)
        + " ltail " + QString::number(mobLTailMs, 'f', 2) + "]"
        + "  st[R " + QString::number(mobCnt("mobStRest"))
        + " F " + QString::number(mobCnt("mobStFall"))
        + " V " + QString::number(mobCnt("mobStRide"))
        + " D " + QString::number(mobCnt("mobStDead")) + "]"
        // t935 粒度化 revision 量化读数（1s 窗）：emit = entitiesChanged 漏斗次数；bump = 指纹差分实际
        //   bump 的槽数（= 真被刷新的 delegate 数 —— 每槽 ~50 个 revision 绑定的重求值只发生在这些槽上）；
        //   fan = 旧口径「count 槽 × 每 emit 全扇出」（修复前的实付成本口径）。bump << fan = 收口生效，
        //   差值全部来自静置 mob / 空槽 / 跨世界高水位槽（t933 判决的 QML 侧残留嫌疑人，至此归零）。
        //   bump ≈ fan = 本窗大量槽真在变（正常：全场 mob 行走中）——对照 stR 静置数判读是否异常。
        + "  emit " + QString::number(mobCnt("mobEmitN"))
        + " bump " + QString::number(mobCnt("mobBumpN"))
        + " fan " + QString::number(mobCnt("mobFanN"))
        + "  hostile " + QString::number(mobSubMs("mobHostile"), 'f', 2)
        + "  spawn " + QString::number(mobSubMs("mobSpawn"), 'f', 2)
        + "  loop " + QString::number(mobLoopMs, 'f', 2);

    m_report = QStringLiteral("prof[1s] %1fr\n  %2\n  %3\n  %4\n  %5\n  %6\n  %7")
                   .arg(frames).arg(tickLine, winLine, cntLine, frameLine, frame2Line, mobLine);
    m_lastFrames = frames;

    // 重置窗口。
    m_ns.clear();
    m_counts.clear();
    m_frameCount = 0;

    // 释锁后再 emit + qInfo（避免锁内回调入函数再持锁 / qInfo 内部加锁交叉）。
    lock.unlock();
    emit reportChanged();
    // 落日志：用户进游戏跑几秒即可在 logs/voxelsandbox.log grep "vo.prof" 看每秒一行分解。
    qCInfo(lcProf).noquote() << "frame breakdown\n" << m_report;
}
