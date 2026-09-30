#include "matrix_helpers.h"

// review0915 #5 顺手修：--filter 模式下「实际执行腿组数」计数（runLegMulti 命中分支自增），
//   runAll 尾部输出 `=== filtered: legFilter=<s>, ran N legs ===` —— 裸看 total FAIL:0 不再被
//   误读为全套件绿（N 与 total PASS + total SKIP 的口径差 = 循环腿静态语句计 1，登记维持不改）。
//   文件内 static：runLegMulti / runAll 同 TU，零头文件改动（段 TU 不重编面）。
static int s_filteredRanLegs = 0;

// R20.03：原 main() 前置 rig 搭建（L229-294）成员化落位。行序与语义同原：
// World 三 setter（各自 regenerate 一次）→ 信号计数器 connect（同步计数，接收者 &w 同原）
// → nextSlot / placeRigBlock 同体成员化。w / slotIdx / 计数器为成员（matrix_helpers.h），
// 原 QGuiApplication 构造留驻 matrix_main.cpp（对象构造序不变：app 先于套件）。
MatrixRun::MatrixRun()
{
    w.setWidth(96);
    w.setDepth(180); // t814：96 → 128 —— rig 位耗尽（slotIdx=125 > 96 深度的 124 位上限）后 nextSlot 落
                     //   z≥96 越界区，setBlock 被静默拒绝 → 器件根本没放上 → 消费端探针全线假 FAIL。
                     //   深度扩到 128：既有 slot 0..123 坐标不变（列距 22 / 行距 3 只向后延伸），新 probe
                     //   落新增行（z=97 起）。世界生成按新深度 regenerate 一次（秒级）。
                     //   t1005：128 → 144 —— P-t1005 四布局 rig 带 ±4 清场/弹坑足印，网格末行 z0=127 时
                     //   z0+4=131 越界（首跑实锤：layout D 的 +Z 直供 TNT 落 z=128 被静默拒 → placeRigBlock
                     //   qFatal 全程中止）。深度扩到 144 既有 slot 坐标不变，新行只向后延伸（t814 同款）。
                     //   review0906 D2：144 → 180 —— review0906 低七项清偿 +3 腿（t1013b / t1015(f) /
                     //   t1017(f)，各耗 1 slot）把基线仅剩 2 slot 的余量打穿（首跑实锤：slot 184 qFatal
                     //   于矩阵尾部）。深度扩到 180 既有 slot 坐标不变，新行只向后延伸（t814/t1005 同款）；
                     //   下方耗尽守卫与容量注释同步改口径。
    w.setHeight(48); // 3 次 setter 各 regenerate 一次（几秒内）；生成快

    // ── 信号计数器（等价 Main.qml 转发消费端；直接连接同步计数）──
    //   （tntFired 等 6 计数器已成员化——原 main 局部被 [&] 捕获、main 存活全程；成员版经 this
    //   捕获存活全程，语义等价。声明行删除，见 matrix_helpers.h。）
    QObject::connect(&w, &World::powerTntTriggered, &w, [&](int x, int y, int z) {
        ++tntFired; lastTntX = x; lastTntY = y; lastTntZ = z;
    });
    QObject::connect(&w, &World::powerDispenserTriggered, &w, [&](int x, int y, int z) {
        Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); ++dispFired;
    });
    // ── 掉落物 / 雪层坍落信号记录器（审查修 #4/#15 探针 P24/P25 消费：附着物失撑坍落断言）──
    //   同上同步计数模式；P24/P25 用前后差分（drops0 快照）隔离本探针的掉落事件。
    QObject::connect(&w, &World::blockDroppedAsItem, &w, [&](int x, int y, int z, int id) {
        ++dropItemCount; lastDropId = id; lastDropX = x; lastDropY = y; lastDropZ = z;
    });
    QObject::connect(&w, &World::snowLayerFell, &w, [&](int x, int y, int z, int layers) {
        Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z); Q_UNUSED(layers); ++snowFellCount;
    });
}

// rig 寻址（2D 网格防越界——首版 x 单排递增在 x>48 后 setBlock 全被越界拒绝 = 假 FAIL）：x 列距 22
//   （容纳 16 粉 + 源 + 接收器的最长探针 18 格）、z 行距 3；96×180 → 4 列 × 58 行 = 232 rig 位。
//   t814 教训：耗尽后 setBlock 静默拒绝（无返回值无告警）→ 器件没放上 → 下游探针全线假 FAIL 且
//   diag 指向消费端（真凶是选址）——故越界改为 qFatal 硬失败（响亮 > 静默腐烂）。
// 原 main lambda L249-261 同体成员化（调用点语法 nextSlot() 不变）。
QPair<int, int> MatrixRun::nextSlot()
{
    const int col = slotIdx % 4, row = slotIdx / 4;
    // review24 低危（探针族）：耗尽检查移到 ++ 之前——旧版先 ++ 再检查，qFatal 报的编号比真失败的
    //   slot 大 1（off-by-one，diag 误导排查）；现报真实失败位号。
    //   t1005：守卫含 **+4 足印**（P-t1005 族 9×9 清场/石台 z0+4 须在界内；旧守卫只钉 z0 本格，
    //   z0=127 行放行后 +Z 器件/清场写越界被静默拒 = rig D qFatal 的真凶）。
    if (4 + row * 3 + 4 >= 180)
        qFatal("rig grid exhausted: slot %d beyond 180-deep grid with +4 footprint "
               "(4 cols x 58 rows = 232) - out-of-bounds setBlock is silently rejected = "
               "false FAIL farm", slotIdx);
    ++slotIdx;
    return QPair<int, int>(4 + col * 22, 4 + row * 3);
}

// review24 低危（探针族）：rig 器件放置回读校验。World::setBlock 对「越界拒绝 / 同 id 无变化早退」均
//   静默（返回 false 无告警）——nextSlot 的 qFatal 只防了坐标越界这一条静默拒绝路径；器件没放上时
//   下游断言全线假 FAIL 且 diag 指向消费端（t814 教训同源）。关键器件放置（t814 真消费端探针的
//   机器 / 源铺设）走本帮手：放置后回读 blockAt 钉落位，落位失败响亮退出。同 id 早退时格子本已是
//   目标 id → 回读照过（不误伤；清理用的 Air 写不需本帮手）。
// 原 main lambda L268-274 同体成员化。
void MatrixRun::placeRigBlock(World &world, int x, int y, int z, BR::Id id, quint8 st)
{
    world.setBlock(x, y, z, id, st);
    if (world.blockAt(x, y, z) != quint8(id))
        qFatal("rig placement silently rejected at (%d,%d,%d) id=%d state=%d - device never "
               "landed, downstream assertions are a false-FAIL farm",
               x, y, z, int(id), int(st));
}

// ── R20.03 目标 B：--filter 腿门控 ──
// names = 本腿组全部静态腿名（PASS 行名，非空）；命中任一子串即整组执行
//（多腿共享作用域按块级门控：任一腿名命中，块内全部腿执行——共享 rig 前置使然）；
// 全不命中则逐名 SKIP 并计数。legFilter 空 = 全跑（与无参逐位一致）。
void MatrixRun::runLegMulti(const QStringList &names, const std::function<void()> &body)
{
    if (legFilter.isEmpty()) {
        body();
        return;
    }
    for (const QString &n : names) {
        if (n.contains(legFilter)) {
            ++s_filteredRanLegs; // review0915 #5：filter 模式实际执行腿组数（汇总行用）
            body();
            return;
        }
    }
    for (const QString &n : names) {
        ++skipCount;
        qInfo().noquote() << "SKIP |" << n;
    }
}

// ── 段调度：原 main 体执行序原样（腿间状态依赖不可变）──
void MatrixRun::runAll()
{
    section01_redstone_core();
    section02_early_probes();
    section03_mid_probes();
    section04_instancing();
    section05_render_ui();
    section06_worldgen_drag();
    section07_chests_mobs();
    section08_recent();
    section09_foundations(); // R20.05 基础类型（新段置尾：世界基线面零接触——setBlock 写即清，
                             //   且执行序在 worldgen 腿族（section06）之后，逐位恒等核对不受扰）
    section10_command_event_snapshot(); // R20.06 Command/Event/Snapshot（置尾先例沿用：纯类型/
                                        //   队列行为面，rig 世界零接触——零残留由构造保证）
    section11_gamesession(); // R20.07 GameSession（置尾先例沿用：自建 fresh 小世界，rig 零接触）
    section12_worldfacade(); // R20.08 WorldFacade（置尾先例沿用：自建 fresh 小世界，rig 零接触）
    section13_editbuffer(); // R20.09 EditBuffer（置尾先例沿用：纯类型腿 + 自建 fresh 小世界，rig 零接触）
    section14_chunklifecycle(); // R20.10 Chunk lifecycle（置尾先例沿用：纯图腿 + 自建 fresh 小世界，rig 零接触）
    section15_generationjob(); // R20.11 GenerationJob 同步版 ChunkScheduler（置尾先例沿用：纯请求模型腿 + 裸网格/fresh 小世界，rig 零接触）
    section16_backgroundgen(); // R20.12 后台 GenerationJob（置尾先例沿用：真线程 worker 腿 + 裸网格/fresh 小世界，rig 零接触）
    section17_meshbuilder(); // R20.13 MeshBuilder（置尾先例沿用：fresh 小世界快照/逐位等价/计数穿透 + 纯源码钉腿，rig 零接触）
    section18_entitystore(); // R20.14 EntityStore（置尾先例沿用：store/Adapter 直驱孪生等价 + 快照权威 + notify 沿计数回归 + 纯源码钉腿，rig 零接触）
    section19_savecoordinator(); // R20.15 SaveCoordinator（置尾先例沿用：统一 SaveGeneration/冻结-持久化分离/故障注入恢复三态 + 纯源码钉腿，rig 零接触）
    section20_generationpolicy(); // §29.4-P1 GenerationPolicy（置尾先例沿用：默认关零变化/请求面/驱逐面/确定性+结构钉，全段零世界纯函数腿，rig 零接触）
    section21_chunkstreamdriver(); // §29.4-P2 ChunkStreamDriver（置尾先例沿用：默认关惰性墙/请求跟随+取消/风暴背压/异步收割+结构钉，全段零世界编排腿，rig 零接触）
    section22_chunkevictor(); // §29.4-P3 ChunkEvictor 卸载+Edits-on-evict（置尾先例沿用：惰性承重墙/驱逐回灌往返[真 SaveCoordinator + fresh 临时库]/失败中止+null 缝 fail-safe/结构钉+kind 选择，rig 零接触）
    section23_meshworker(); // D6 worker meshing（置尾先例沿用：确定性等价承重墙/队列纪律/析构卫生/结构钉+白名单修订自证+零接线，rig 零接触）
    section24_qmldynamization(); // §29.4-P4 QML 动态化（置尾先例沿用：固定世界枚举恒等墙/模型响应性沿对账/QML 真链响应性/钉面，rig 零接触）
    section25_sparseworld(); // §29.5-W1 稀疏世界核（置尾先例沿用：fixed 零变化承重墙/未加载域 OOB 等价+加载翻转/同 seed 权威恒等/钉面，rig 零接触）
    section26_sparse_population(); // §29.5-W1b sparse population parity（置尾先例沿用：fixed 零变化墙/parity 承重墙/加载顺序无关/结构钉，rig 零接触）
    section27_streaming_wiring(); // §29.5-W2 位置源 + 驱动接线（置尾先例沿用：fixed 零活动承重墙/通电走查承重/真线程收割+#7 契约+背压/结构钉，rig 零接触）
    section28_eviction_persistence(); // §29.5-W3 驱逐 + Edits-on-evict 落盘（置尾先例沿用：fixed 零活动墙[无附加表构造]/驱逐回灌承重[真临时库]/失败中止+实体先移除/结构钉，rig 零接触）
    section29_wiring_meshworker(); // §29.5-W4 bake→worker 网格化（置尾先例沿用：fixed 零变化墙[无 worker 构造]/异步等价承重墙[真执行器+收割拍逐位恒等]/回退+卫生[满载/已停回退+最新快照胜]/结构钉[F3 worker 列+单点收口]，rig 零接触）
    section30_streaming_persistence(); // §29.5-W5 后端 流式世界持久化 + D2/D3 标志 + overlay 合并（置尾先例沿用：fixed 零活动墙[保存/读档全语义逐位不动+库面零流式表]/流式存读往返承重墙[保存含冲洗+销毁重建逐位恒等+二次幂等，真临时库]/D3 转换往返[blob 物化+代次对代次仲裁+逆翻照旧]/结构钉[worldstore 禁触反探+additive 正面钉+冲洗失败不谎报+QML 零触碰]，rig 零接触）
    section31_streaming_ui(); // §29.5-W5b 流式 UI（置尾先例沿用：默认关零变化墙[真链分流恒 false+会话零构造+三写/往返回归+库面零流式表]/开关+转换通电面[真 QQmlEngine×真桥单例：标志行+sparse 进入通电+真玩家走查生产泵收敛+D3 转换 blob 逐位+冲洗生产面+行回灌仲裁]/QML 面钉[变更面集中+Q_INVOKABLE 逐一正面钉+词元禁触+值组件零暴露]，rig 零接触）
    section32_residual_sweep(); // t1055 残余清偿合集一 + t1056 合集二（置尾先例沿用：r2029a detachWorker 销账守卫/r2029b lastDirtyChunks 值快照语义/r2029c submitAsync fail-fast 默认/r2029d weather 0 值回落 qInfo 诊断 + r2030a 软影列顶域扩 PCF 半格触达[kTopDim 22+紧覆盖推导+真实快照逐探测列对真值]/r2030b generate 首循环回填群系 memo[全列已填+纯函数直连零变化+懒填兜底]，rig 零接触）
    section33_savebridge_wiring(); // t1057 SaveCoordinator 生产接线（置尾先例沿用：r2031a Clean 路径零变化墙[真链三写段置换+计数观测+参数透传+旧档 Fresh 回归]/r2031b marker→complete 往返+中断恢复收敛[真保存走桥+两键 raw 直读+FaultHook 经桥注入+戳权威]/r2031c #5② 开库失败可区分[真锁占 open-error+拒存不重编+open 级失败]/r2031d 结构钉[worldstore 零触碰反探+additive 正面钉+QML 两处例外面钉+生产零挂载]，真 QQmlEngine real-chain × 真桥单例，rig 零接触）
    section34_observation_lines(); // t1059 P5 观测前置（置尾先例沿用：r2033a fixed 全零行+行格式钉[行恒在零值+解析钉+拼行/推送源码钉]/r2033b 流式行计数与权威面逐项恒等[四窗 flush 界定：初载 8/取消 9/网格收割/驱逐 evP1 evE4 evS4]/r2033c dt 负/NaN 分域计数[三类精确+零变化墙+读面钉]，sparse fresh 小世界族+真临时库，rig 零接触）
    section35_fixed_async_bake(); // §29.7 t1060 fixed 世界 bake 异步化（置尾先例沿用：r2034a fixed 异步≡同步逐位等价承重墙[四形态：地形/流体水段/异形 cross/世界边界 + 单实例线程身份 + F3 sub/mesh/worker 面 + 驻留零沿] / r2034b 风暴摊平[dayMul 跨门 → 提交数==非空段数 + 单拍应用恰上界 + 后续拍排干收敛 + 终态逐位==同步参照 + F3 面对账] / r2034c 回退与卫生[env 全同步门 + 容量缝 0 退化满载内联回退逐位 + 析构 join 有界 + 再构造再用] / r2034d 结构钉[env 缝正面钉 + 惰性单例源序 + 单实例/不双起线程行为柱 + 收割宿主单点收口扫略 + 真桥 enterWorld/pumpTick 生产链行为柱 + 单一权威反探兼容 + QML 零触碰]，fixed 小世界族 96×96×96 s82 + 48×48×96 s82 + 真临时库，rig 零接触）
    section36_streaming_entry(); // t1061 无限世界生产入口 livelock（置尾先例沿用：r2035a fixed 零变化墙[真链分流恒 false + 会话零构造 + 驻留沿零发射 + 通电门/泵墙/进入分流三钉] / r2035b 入口收敛承重墙[真链生产尺寸进入：物化突发恰一条驻留沿 + 沿时刻驻留集合流[减员全在 gen 半径外 + 驻留数单调不降] + 收敛到请求集尺寸零半径内驱逐 + 收敛后沿停 + 整池重建消费面钉] / r2035c 预生成中心×出生分歧[合规驱逐 + 错位域收敛 + 走回重物化逐位恒等 + 收敛后驻留稳] / r2035d 走离走回+结构钉[首沿门前惰性 + 擦槽驱逐 + Edits-on-evict 编辑存活 + 单一权威源钉族 + 物化批口五落点]，fixed 宿主 48×48×96 s82 + 核心域 160×160×96/80×80×96 + 真临时库，rig 零接触）
    section37_walkback_identity(); // t1062 走回重生成内容漂移收口（置尾先例沿用：r2036a fixed 世界零变化墙[双 generate 逐位恒等+全 pass 字面钉+物化链反探] / r2036b 物化时序无关承重墙[全驱逐→逆序重物化≡初见+×3 轮恒等+零掩蔽口径] / r2036c 预生成 vs 按需三方恒等[A 初见==A 走回重物化==B 空域按需+邻块零扰动] / r2036d 结构钉[统一物化路径单一权威+钳制/快照面不回退+fixed 反探]，80×80×96 fresh sparse 小世界族，rig 世界 w 零接触）
    section38_slot_pool_patch(); // t1063 Main.qml 差分池 patch（置尾先例沿用：r2037a fixed 世界零变化墙[真桥进入握手恒 false+驻留零沿+池零 patch 零重建+段指针逐位不动] / r2037b 增量等价承重墙[每沿后池≡整池重建参照逐项恒等+幸存组指针复用+重加组 canonical 归位新对象+规范序重排复用] / r2037c 增量性计量腿[每沿 churn=恰变化键组规模+批口单沿双键恰 10+走查模拟逐沿精确+账本闭合到池尺寸] / r2037d 结构钉[revision Connections 恰一处+增量入口+组创建单点化+步长单源三面同源+两消费端契约句零触碰+世界换代收口面函数域钉+玩法路径+生命周期禁入]，r2037a-c 真 QQmlEngine×真 World 池镜像 wrapper，r2037d 纯源码钉，rig 零接触）
    section39_exit_save_backoff(); // t1064 退出存档失败重试退避（置尾先例沿用：r2038a 无锁常态墙[首试即真零重试+零退避双证+计数 +3 口径+完成门/toast 语义面] / r2038b 退避承重[真锁 BEGIN EXCLUSIVE：桥直调窗 [100,300]ms+锁窗内释放→重试收敛恰一次] / r2038c 上限与不谎报[锁全程持有：恰一次重试即止+计数零动+台账历史原样+释放后收敛] / r2038d 结构钉[共用实现单点+两调用点+旧 0ms 重放禁入+完成门/归还序零触碰复钉+退避常量 ≤300ms 源钉]，真 QQmlEngine×真桥单例×生产 wrapper 原文抽取（brace 配平），rig 零接触）
    section40_slot_pool_fixed_switch(); // t1065 流式→固定世界切换池残留收口（置尾先例沿用：r2039a fixed-only 零变化墙[真链进入恒 false×2+驻留零沿+池零 patch 零重派生+段指针逐位不动+时代标记恒 false] / r2039b 切换承重墙[真链流式走查产负坐标驻留键→detach→进 fixed→时代门重派生恰一次→池与固定网格逐位一致+负坐标组清零+churn 账本闭合] / r2039c 往返腿[fixed→流式→fixed→流式全链池一致性+fixed 两轮键集逐位一致+流式再进入重派生不回退+差分路径仍活] / r2039d 结构钉[时代标记单点落账+fixed 收口恰一处+函数域抽体钉+两消费端体零触碰反探+生命周期/C++ 模式读面禁入 QML 反探+reinitializeAsFixed 体零沿语义钉]，r2039a-c 真 QQmlEngine×真 World 池镜像 wrapper（section38 同门镜像家族），r2039d 纯源码钉，rig 零接触）
    section41_save_busy_zero(); // t1066 保存链 BUSY_TIMEOUT=0（置尾先例沿用：r2040a 无锁零变化墙[首试即真恰 1 调用 + 墙钟 ≤300 + +3 口径 + 台账 Clean + 探锁零退避面 + 完成门/toast 语义面原样] / r2040b 首试即时性承重[真锁全程持有 + 真链零注入：首试 <1s 诚实败 + 整链[探锁→150ms 档→恰一次重试] <2s 有界 + 计数零动 + 台账历史原样 + 释放后收敛 +3 Clean 2/2] / r2040c 锁中途释放收敛[t1082：事件驱动锁窗[持锁者见退避打卡才放手] + 真链零注入：首试瞬时败→探锁→退避打卡恰一次[逻辑轮次面]→档→重试收敛真 + 恰一次重试 + 仅重试 +3 + 代次单调 2/2 + 墙钟上界] / r2040d 结构钉[逐连接放置钉[每 addDatabase 后 2 行内必跟归零设置 + 每文件设置点计数 4/2/1/1 = 新增连接漏配即红] + 剥注释 minCount 复钉 + r2038 零触碰复钉 + 探锁连接保持 0 钉]，真 QQmlEngine×真桥单例×生产 wrapper 原文抽取（brace 配平），零故障注入直面真锁，rig 零接触）
    section42_ip_root_rename(); // t1067 §9 区隔改名批收口（置尾先例沿用：r2041a 旧行为等价墙[点燃 6/20 格 id 138 逐位 + state 轴位 0/1 + 生产钩子熄门 + 直挖门格连通域熄灭 + 暗渊门 12 框架环谓词/开环 9 格 id 131/破框静默清门面 + 真临时库存读往返 id/state 逐位保真=存档安全铁律行为级] / r2041b 旧词根清零钉[src/ 全树四词根命中行必携 §9 记载标记 + 每文件计数 3/15/2/1/4 = 总 25，盘点口径漂移即红] / r2041c 用户可见面钉[displayName 暗渊门框架/暗渊门面/余烬门 + blockName abyss_gate 族 + nameForBlock(EndEyeId)=="暗渊之眼" + 新名正面源钉 + 旧中文名命中行必携 §9 + 每文件计数 1/4/1/1 = 总 7] / r2041d 结构钉[数值 id 111/131/138 + 0x01/32/0x23A + 编译期 static_assert 互钉 + 方法族声明面源钉 + t1067 映射头注锚串裸文本在场钉]，各腿自建 fresh 世界/临时库，rig 零接触）

    section43_ender_root_rename(); // t1068 §9 Ender 族改名收口（置尾先例沿用：r2042a 旧行为等价墙[眼 kind 7 + 巡航高度掷出眼位+8 + 飞行态非碎裂 / 珠 kind 8 + 非整格接触落点=自身格经改名信号 / 改名钩子传送立位 + Survival 自伤恰一次 (5, AbyssPearlTp) + Creative 零伤 / shapeless 珠+燃烬粉答不变眼 id + 内部字面 abyss_eye + 半配方对照 null / 真临时库存读往返双物品 id/count/名逐位保真=存档安全铁律行为级] / r2042b 旧词根清零钉[src/ 全树八词根命中行必携 §9 记载标记 + 每文件计数 11/6/3/4 = 总 24，盘点口径漂移即红；Enderman 已 t727 改名不入扫面] / r2042c 用户可见面钉[nameForBlock 双物品新名 + 死因文案在场 + 读取面图标路径字面 + 自绘入口 + 调色板 id 面，displayName 本批零改动] / r2042d 结构钉[数值 id 逐位 0x243/0x23A/7/8/16 + 编译期 static_assert 互钉 + 改名族声明面源钉 + QML kind/delegate/handler 面 + t1068 映射头注锚串裸文本在场钉]，各腿自建 fresh 世界/临时库，rig 零接触）

    section44_residual_pair(); // t1069 残余清偿合集（置尾先例沿用：r2043a 既有行为零变化墙[相邻族腿名计数源钉 ×3 + 池路径契约钉 + 生产 patch 体原文抽取执行初建/单键驱逐/重加全链池行为逐位] / r2043b 口径收窄承重[收窄门逐字源钉 + 固定世界 comparator 敏感面行为柱：旧列高基准盲区的块缘带体素腐败必被全逐位比对捕获 + 退役留痕锚] / r2043c 精确摘除承重（生产 patch 体原文执行——抽取 harness 强于镜像 wrapper 一档：测生产体本身非同构体）/ r2043d 结构钉[摘除谓词族源钉 + 全清写点精确计数恰两处 + 两消费端/稳态兜底契约句零触碰反探 + 段入口 comparator 锚 + PASS 如实化锚 + 退役留痕锚]，各腿自建 fresh 世界，rig 零接触）

    section45_savepath_opt(); // t1070 流式/存档路径优化合集三件（置尾先例沿用：r2044a 零变化墙 / r2044b 件一承重[双计数对账：executed 推进而 generated 恒冻 + 逐位回灌] / r2044c 件三承重[重建计数 +1/0/0 + 冻结点三面] / r2044d 结构钉[件一权威源钉族 + 件二量化注锚 + 桥顶针 + 禁触面反探]，r2044b 真会话真线程波 + r2044c 真链 × 真桥单例，rig 零接触）

    section46_skeleton_integrity(); // t1071 测试骨架完整性合集（置尾先例沿用：r2045a 件一承重 / r2045b 件二承重 / r2045c+d 结构钉——纯源码钉零世界腿，rig 零接触）

    section47_streaming_outer_content(); // t1073 流式外环内容三合一诊断修复（置尾先例沿用：r2046a fixed 零变化墙[双 generate 逐位恒等 + 单一权威钉 + 旧钳制反探] / r2046b 外环内容承重墙[生产尺寸真链走查出核：外环树/矿/carve 齐备 + 走回重物化逐位] / r2046c 刷怪域承重[外环黑夜自然刷怪 + 外环日光燃烧 + AI 钳制不拽核] / r2046d 结构钉[扩展域单点 + lattice 带族 + 域门调用面 + 反探族]，fixed 48×48×96 s82 + sparse 核心 160×160×96 ×2 + 真临时库，rig 零接触）

    section48_streaming_outer_light(); // t1074 无限世界外环挖方块全黑（置尾先例沿用：r2047a fixed 零变化墙[fixed 挖掘/火把行为柱 + Fixed 分支字面钉] / r2047b 外环全黑复现+修复承重[生产尺寸真链出核：播种柱 + 挖掘亮 + 跨 chunk 火把] / r2047c 负坐标外环腿[负侧播种柱 + 挖掘亮 + 负侧火把跨 chunk] / r2047d 结构钉[floorDiv 标脏循环 + sparse 边界读分支 + sparse 无界盒 + 旧码反探]，fixed 48×48×96 s82 + sparse 核心 160×160×96 ×2 + 真临时库，rig 零接触）

    section49_entity_blob_shadow(); // t1075 blob 软阴影（置尾先例沿用：r2048a 平地贴地承重墙[贴地 Y/alpha/半径 + 指纹差分 + 几何可数通道] / r2048b 悬崖边缘+腾空衰减[单调淡出链+超窗缺席] / r2048c 未物化缺席+两模式贴地逐位一致[sparse radius-0 同 seed 孪生] / r2048d 结构钉[采样权威门+几何契约+QML 接线/材质契约+注册族+零对地采样反探+EntityStore 零触碰反探]，fixed 48×48×96 s82 + sparse radius-0 孪生，rig 零接触）


    section50_streaming_negative_walk(); // t1076 大核流式世界负向走查零收敛（置尾先例沿用：r2049 负向走查收敛断言[生产尺寸真链逐 chunk 负向走查 (4,5)→(-3,5) 每站有界泵拍 → 终站静置 ±2 方窗 25 键 deadline 150000ms 收敛 + 六态直方/F3 流式行差分/驻留键样本/loadChunkAt 兜底探针四域失败签名 diag]，fixed 48×48×96 s82 + sparse 核心 160×160×96 s82 + 真临时库，rig 零接触）

    section51_bonemeal_flora_boneblock(); // t1077 骨粉催生草丛/花 + 骨块（置尾先例沿用：r2050a 草方块催生承重墙[世界直调：有效目标真值 + 5×5 邻域 id 带守卫 + 既有方块零覆盖 + 全图唯一 patch + 同 seed 孪生逐位 + 错峰行为差分 + 全围死仍中心长 + 上方被占/泥土负例]，r2050b 骨块承重墙[纯表：配方双向/负例/往返恒等 + def 行逐字段 + 采掘面三查询 + 世界写读回 + kMcBlockId 尾行行为级对齐]，r2050c 催生分布/边界墙[13×13 大田统计一致性 + patch 截断边缘安全 + 2×2 组合配方负例]，r2050d 结构钉[单一权威 + 交互入口单点 + id/tile/配方/图标/资源表注册族 + 派生链工具表 + 对齐补行钉 + QML 零触碰反探]，rig 世界零接触，接 section50）

    section52_dye_mixing(); // t1078 染料获取链补全（置尾先例沿用：r2051a 染料获取承重墙[源自含复核 + 墨囊→黑染料 1:1 转换 2×2/3×3 双格命中 + 全表不动点闭包：6 生存源→15/16 色可达逐位 + 棕色永不可达 + 黑必经转换]，r2051b 混色行为柱[9 条 2→2 产率逐位 + 换位摆序无关 + 混色→羊毛两跳链端到端 + 品红二级二级双跳原料自证]，r2051c 边界与覆盖墙[未知对/同色对/墨囊不替代混料/骨粉非白染料替身/三料落选形态/棕色终末 → 全 nullptr 无效应不消耗权威面]，r2051d 结构钉[10 配方行注册族在旁 + 名去重恰一次 + 染料右键分流单点反探禁第二份分发 + QML 零触碰反探 + t1078 头注锚点在场]，纯表腿 + 源码钉，rig 世界零接触，接 section51）
    section53_hopper(); // t1080 漏斗机制四语义（置尾先例沿用：r2052a 收集承重墙[上方格+自身格收集/整栈入腔逐位/元数据/满仓拒绝/部分接受余量回写/异列不收]，r2052b 输出+抽取承重墙[前推一轮恰1件/无目标挂起/漏斗链传递/上抽首非空槽/熔炉 out 抽取与 in·fuel 定向推入/元数据随栈]，r2052c 红石锁停+边界墙[powered 三语义全停+无效应不消耗+解锁恢复+满仓挂起+孤儿条目 inert]，r2052d 结构钉+持久化往返[def 行逐字段/排料口解码权威/配方命中负例/注册族源钉/QML 装配钉+零触碰反探/hoppers 表真 SQLite 往返]，fresh 48×48×96 s82 小世界族 + 真临时库，rig 世界零接触，接 section52）

    section54_spawner(); // t1081 刷怪笼条件刷怪 + 破坏语义（置尾先例沿用：r2053a 条件刷怪承重墙[节流 5+1s/距离窗 17.5>16/昼夜光照门同 rig 双亮度/火把压停+撤火把恢复/双 rig 采样确定性]，r2053b 刷出合法性+破坏承重墙[盘缘采样承重·旧窄域判别位/盘外格拒采/支撑门保留/破笼即停/def 行破坏面逐字段]，r2053c 与地牢族解耦墙[fixed 核心域手放笼 + sparse 负坐标外环笼·loadChunkAt 物化 + 零地牢路径反探]，r2053d 结构钉[常量族逐位/state 编码/签名+接线+采样表哈希源钉/QML XP 接线钉/单一权威恰一处与 QML 装配零触碰反探]，fixed 48×48×96 s82 小世界族 + sparse 构造缝 ×1，rig 世界零接触，接 section53）
    section55_jukebox(); // t1083 唱片机 + 音乐盘（置尾先例沿用：r2054a 放入/吐出承重墙[真链放入 state 逐位+生存消耗+started 沿；吐出盘物品还原 id 逐位+state 清位+stopped 沿；创造放入不消耗]，r2054b 音乐盘+战利品承重墙[三盘 id/名/调色板连续同列/不可堆叠 Game+Core 双面/映射 round-trip 唯一/配方命中+中心料负例/dungeon+mineshaft 两池挂接逐位/确定性 seed roll 命中]，r2054c 播放语义+边界墙[到期前不吐/到期自动吐盘=时长单一权威驱动+双吐守卫/载入态空手续播/生存破坏吐盘+非播放零 stopped/创造破坏不吐/非盘拒收无效应不消耗 4→4]，r2054d 结构钉[def 行逐字段/kMcBlockId 84/state 编解码 0..63 可逆+越界 clamp/音色族 GroupWood/图集 191/配方注册·tick 接线·时长契约两处同步源钉/QML 路由钉+状态机零 QML 反探]，真链 rig = t945 同门，fresh 48×48×96 s82，rig 世界零接触，接 section54）
    section56_paper_book_bookshelf(); // t1084 纸/书/书架（置尾先例沿用：r2055a 合成承重墙[纸命中·行纵移/书命中 2×2+3×3/书架命中 + 负例 2 甘蔗/竖列/小麦分流面包/2×2 拒纳/缺皮革/镜像/9 木板/中行错纸 + 三配方行源钉唯一]，r2055b 破坏掉书承重墙[真链生存破 → 恰 1 实体 0x238 携 3 件 + def 掉落面精确钉（NEG-1 靶行不入 d 腿）/创造破不掉/空手也掉落/零自掉]，r2055c 物品面/调色板/源面[id 逐位/名表面/纸书连续同列/书架方块列/isBookshelf 谓词/甘蔗自掉/皮革 fishing 池逐位+牛掉 QML 字面/pack 映射行源钉]，r2055d 结构钉[def 行逐字段·掉落面相对恒等/kMcBlockId 47 行唯一/图集 191/图标路径/QML !==95 tripwire/纸书 id 禁入 Main.qml 反探/旧掉自身口径禁出反探]，真链 rig = t1083 同门，fresh 48×48×96 s82，rig 世界零接触，接 section55）
    section57_lava_water_contact(); // t1085 岩浆×水接触规则（置尾先例沿用：r2056a 岩浆源×水黑曜石承重墙[水源/流水双触发漏斗 → 岩浆格 Obsidian + 水面存活 + 幂等 + 无水对照]，r2056b 流岩浆×水圆石承重墙[流岩浆格自身凝固 Cobble·不问水方 state + 水面存活 + 无石头产物反探 + 同水源可再生 + 源/流双漏斗 level 互斥]，r2056c 账面收敛+确定性墙[水波前 7 级展开精确 states + 接触转化不动点 + 双 seed 孪生逐位 + 双 tick 共驱平衡]，r2056d 结构钉[接触漏斗单写点源钉 + solidifyKeys 四守卫 + 增量索引零触碰反探 + tick 接线钉 + 转化 id 逐位 + t1085 头注锚]。流体窗口直调 tickWaterFlow/tickLavaFlow（section03 同款），fresh 48×48×96 双 seed 小世界族，rig 世界零接触，接 section56）
    section58_sugarcane_growth(); // t1088 甘蔗生长口径归一（置尾先例沿用：r2058a 草/土基生长命中承重墙[放宽基材集行为级 + 石基对照]，r2058b 沙基回归+邻水门保持墙[t446 行为保留 + 无水沙基/无水草基恒不长]，r2058c 上限 3 精确墙[2 高柱恰好到 3 + 3 高柱第 4 次拔高恒拒；t406「max5」lawful 翻案行为级]，r2058d 结构钉[单一权威三面源钉 + 真值表 + 上限常量 =3 源钉 & =5 反探 + t406 翻案锚注 + t418 退役锚注 + worldgen 1..3 公式钉 + kSugarcaneTallPct 残留禁出]。生长窗口直调 tickSugarcaneGrowth，fresh 48×48×96 s82，rig 世界零接触，接 section57）
    section59_write_gate_outer(); // t1089 写门五员同族清偿（置尾先例沿用：r2059a setBlockSilent 外环承重墙[负坐标踩踏回土 + 对照 + 未物化拒 + 无变化早退]，r2059b setBlockFromEntity 外环承重墙[出核远 chunk 行为级沙落 + 远/负直调 + 未物化拒 + occ 保持]，r2059c setSnowLayerMerge 外环承重墙[出核远 chunk 行为级雪层塌落合并 + 负直调 + 防御/未物化拒]，r2059d clearBlockSilent 外环承重墙[负坐标点火清原块 + 对照 + 未物化拒 + y 域门]，r2059e setWaterSilent 外环承重墙[负坐标舀水/非流体写 + 无变化早退 + 未物化/y 拒 + 对照]，r2059f fixed 世界零变化墙[五员域内真/域外恒拒行为级]，r2059g 结构钉[五员锚注 + 物化门行计数 7 + 批头锚注族 + world.h 声明注族]。sparse 构造缝 + loadChunkAt 物化（r2053c/r2047c 同门），rig 世界零接触，接 section58）
    section60_write_gate_scan_box(); // t1090 写门家族「核心域假设」同族清偿·后续批（置尾先例沿用：r2060a destroySphereSilent 外环承重墙[负坐标弹坑毁块 + 天光回灌 + 空返回面 + 对照]，r2060b 树叶腐朽外环承重墙[负坐标叶入队 + hashVoxel 确定性窗驱动渐退 + 对照叶永留 + 天光回灌]，r2060c 重力级联外环承重墙[②支撑破坏柱坍 + ③26 邻浮沙连锁 + 坍落柱天光回灌]，r2060d fixed 世界零变化墙[四员 Fixed 分支逐字原样行为级]，r2060e FallingBlock 实体负坐标列着地承重墙[级联链 entitymanager 面]，r2060f 结构钉[十站点锚注 + 无界盒/包键计数 + 退化盒防御钉 + t1089/t1074 零变化复钉 + 批头/留池锚注族]。sparse 构造缝 + loadChunkAt 物化（r2053c/r2047c 同门），rig 世界零接触，接 section59）
    section61_write_gate_residual(); // t1091 写门家族「核心域假设」同族清偿·第三批残项（置尾先例沿用：r2061a primed TNT 外环承重墙[负列重力下落 + 落位引爆毁支撑]，r2061b Mob/Item 外环物理承重墙[负列下落着地]，r2061c packGrowthCell 键域承重墙[负坐标水铺展 + 燃烧态存续到烧毁收口]，r2061d 火辅助谓词族承重墙[点燃/燃烧查询/湿燃料负向水邻/雨露判确定性窗]，r2061e 铁轨重连 + 附着复检承重墙[负坐标轨连接位 + 清格火把脱落 + 核心对照]，r2061f fixed 世界零变化墙[实体负列冻结 + 冻结位引爆 + 谓词域外恒假 + 域外放轨拒 + 核心水回归柱]，r2061g 结构钉[二十七站点锚注 + 别名/掩码/域门行计数 + r2059/r2060 复钉 + 批头/留池锚注族]。sparse 构造缝 + loadChunkAt 物化（r2053c/r2047c 同门），rig 世界零接触，接 section60）
    section62_cocoa_bean_dye(); // t1092 棕色染料生存源——可可豆（置尾先例沿用：r2062a 地牢池 roll 行为柱[确定性 seed 命中 + 同 seed 复现 + 数量区间 [1,2] 逐次 + 千 seed 宽频带防权重塌零]，r2062b 转换行为柱[2×2/3×3 单放 1:1 产棕 + 行名身份钉 + 错误原料负例[可可+白染料/羊毛/双可可/墨囊全 nullptr 无效应不消耗] + 两跳链端到端可可→棕染料→棕羊毛]，r2062c 16 色生存可达闭包钉[基源 = 四花+仙人掌绿+墨囊+可可豆(池门控) → shapeless 不动点 16/16——t1078「最后一环」兑现验证]，r2062d 结构钉[段位源钉 0x262 段尾追加序 + dungeonChestPool 行逐字段+行数 10+权重和 153 同步钉 + 合成行源钉与名去重 + maxStack 64 双层面[Hotbar/Core] + 名/调色板尾四连/图标 case 三呈现面 + 荚面降级登记注 + 零方块/零图集反探[blockregistry/resourcepackmanager 全无 cocoa]]。纯表腿 + 源码钉，rig 世界零接触，接 section61）
    section63_hopper_degraded(); // t1093 漏斗降级四件批（置尾先例沿用：r2063a UI 路由钉[hopperOpened 信号/发射行/QML open·close 函数/面板实例化/store 注入/E·Esc·互斥收口源钉] + 5 槽 store 面[全元数据往返/revision/清条目]，r2063b 碰撞行为柱[三盒逐位 + 可站顶 0.625 三权威（supportTopYAt/collisionTopY/solidTopOffset）+ 朝向嘴位 + 谓词族 + 射线 sub-AABB + 真 EntityManager tick 沙落「不完整方块分支」碎成掉落物]，r2063c 爆炸掉内容[真链 detonateTntBlock → 内容实体落地带元数据 + 条目清空 + 本体毁 + 非漏斗格广播无害]，r2063d 结构钉[def 行逐字段 + Shape 枚举值 11 + 单一权威盒逐位两态 + cap 防御 + mesher 三路由源钉 + 爆炸双发射点源钉 + 比较器反探 + fixed 零变化墙[收集/输出机制回归柱]]。各腿自建 fresh 48×48×96 s82 小世界族，rig 世界零接触，接 section62）
    section64_write_gate_residual2(); // t1094 写门家族「核心域假设」同族清偿·残面批（置尾先例沿用：r2064a 红石外环承重墙[负坐标电路点亮 + 金轨链步点亮 + 核心对照]，r2064b 柱坍族承重墙[负坐标仙人掌/甘蔗整柱失撑坍落]，r2064c 单格附着族承重墙[负坐标蘑菇/压力板/枯灌木失撑脱落]，r2064d 板/雪/轨/画/火族承重墙[负坐标活板门级联掉落 + 雪层柱坍 + 铁轨失撑掉落 + 画作整画掉落 + 火失撑即时熄]，r2064e mob 火/岩浆足印承重墙[负坐标燃块旁足印接触点燃扣血 + 核心对照]，r2064f fixed 世界零变化墙[域内红石/mob 火接触回归柱 + 域外写拒 + 负列 mob 冻结恒满血]，r2064g 结构钉[十九站点锚注 + 批头/清偿注锚族 + 词根计数 + r2059/r2060/r2061 承重复钉]。sparse 构造缝 + loadChunkAt 物化（r2053c/r2047c 同门），rig 世界零接触，接 section63）
    section65_repeater(); // t1095 红石中继器（置尾先例沿用：r2065a 延迟档位语义[同构双电路 delay=1/4 精确时序差 + 挂起计数 state 直读]，r2065b 整流单向[侧向不灌 + 背靠背反灌拒绝 + 链上正控制]，r2065c 续距 15 + 端到端[负坐标 + 核心双域 17 格电路，无中继器必暗强判据]，r2065d 结构钉[def 行逐字段 + id/图集/state 编解码 + 调档循环 + 全链源钉族]，r2065e 域门收口复钉 + 相邻族零污染[r2059-r2064 计数钉 + 既有红石族回归柱 + 负坐标墙复钉 + 惰性编辑零重算]。sparse 构造缝 + loadChunkAt 物化（r2053c/r2059-r2064 同门），rig 世界零接触，接 section64）
    section66_glimmer_bottle(); // t1096 蕴辉瓶投掷释经验（置尾先例沿用：r2066a 右键投掷行为柱[真 pc 发射链 + 初速/重力/朝向行为级 + 创造不耗/生存消耗/挥手三口径]，r2066b 触地即碎承重墙[无实体也碎 + mob 触碰 0 伤害即碎 + 命中格精确 + 零世界变更]，r2066c XP 释放面[拆球守恒 + canonical split 逐值 + 确定性 seed roll 信封 [3,11] 两端 + 负例防御]，r2066d 结构钉[0x263 段尾追加 + 名面/调色板尾邻 + 投掷入口/释放链源钉族 + QML 路由与 delegate 钉 + 零方块/零图集反探 + 相邻族零污染]。真链 pc rig（t891 同门），rig 世界零接触，接 section65）
    section67_brewing(); // t1097 酿造台 + 药水（置尾先例沿用：r2067a 酿造机制承重墙[scanBrewingStands 直编：20s 一轮 + 燃烬粉 20 次计量 + 三瓶同酿 + 瓶栈数量保留 + 无原料进度复位 + 亮标翻转]，r2067b 效果链 + 配方面承重墙[brewResult/fuelOpsFor 静态表 + 三合成配方命中 + applyStatusEffect 挂效果快照与到期]，r2067c 装水 + 饮用链行为柱[真 pc rig：瓶装水零世界写入 + 水瓶/药水长按饮用 + 空瓶返还 + 生存耗 1 创造不耗]，r2067d 结构钉[0x264..0x26A 段尾 + 方块 148/Count 149/kMcBlockId 117/Shape 13/图集 195/枚举尾追加/常量族/配方行/名面调色板/全链源钉族/锚注/相邻族零污染]。fixed 48×48×96 s82 + 真链 pc rig（t891 同门），rig 世界零接触，接 section66）；t1105 炼药锅 + 南瓜农作链（置尾先例沿用：r2075a 炼药锅水位交互行为柱[真 pc rig placeBlock 链：生存桶灌满 3 级+桶→空桶 + 瓶取 3 次水位阶梯 3→0 + 每取予 1 水瓶 + 空锅拒瓶无效应无挥臂 + 创造不耗桶 + 真链破坏掉本体]，r2075b 南瓜茎生长门+成熟结果行为柱[r2073b MelonStem 模板镜像第二实例：骰子镜像生长 + 石地/围石双门阴性 + 哈希定向落南瓜 + 茎保持成熟 + 真链破茎掉 1 南瓜种子]，r2075c 南瓜生存入口+南瓜灯合成+光照面[1:4 种子双向 + 南瓜+火把→灯 + 光 15 权威面 + 真世界邻格方块光 14 + 物品面三件]，r2075d 结构钉族[0x28B 段尾 + 方块 151..153/Count 154/图集 207 + kMc 104/91/118 唯一 + 三 def 行 + 两配方行 + worldgen 双调用点 + 接线/呈现源钉族 + 相邻族零污染]。fixed 48×48×96 s82 + 真链 pc rig（t891 同门），rig 世界零接触，接 section67）
    section68_enchanting(); // t1104 附魔台 + 附魔机制（置尾先例沿用：r2074a XP 等级阶梯 + spendLevels 结算承重墙[MC 1.0 三段曲线锚 7/9/11 · 35/37 · 112 + 总量边界 7/16/280/315/1395 + 级内条填充比 + 真付 2 级恰截 27 级内清零 + 余额不足假零副作用零信号 + 0 级防御 no-op + 扣到零基线 + addLevels 整级抬升 + 单呼 55 点跨 5 级发信号]，r2074b 台位锚定书架增幅链 + 抽签阶梯承重墙[净空 0 → 仅上层环带 6 书架计 6 → 档 2 → offered 9；补下层满环 15 封顶 → 档 3 → offered [10,20,30]；堵下层角书架专属半步恰 −1；钻石剑池 6 候选数量阶梯 offered 5/15/30 → 恒 1/2/3 抽 40 seed 全扫 + 锐锋等级阶梯 offered 30 恒 5 级 / offered 2 恒 1 级 + 同 item/offered/seed 确定性 + 桥接同源 + 产物 pack → 4 槽 → findLevel/unpack 往返恒等]，r2074c 表产物 → 消费面回归柱[扫 seed 找锐锋产物 → weaponAttackDamage == 基础 + 0.5×级 + 伴生不串扰 + 显示面 round 恒等；扫 seed 找效率产物 → selectedItemEnchantLevel 读回 + findLevel 恒等 + Stone 挖掘时间严格变快；消费接线源钉族五针]，r2074d 结构钉族 + UI 路由钉[引擎 id 94 + kMcBlockId 116 全表扫唯一行 + def 行逐字段 + collisionTopY 0.75 特例 + 谓词/图标 + 配方 1 书+2 钻+4 黑曜石命中 + 错心/2×2 负例 + maxStack 双面 + playerstate.h 声明行 + 信号/发射行 + Main.qml 路由族 + 面板五行接线 + kMc 116/石质音色双裸读锚 + 相邻族零污染 0x28A/0x287/Count 151/图集 201/BrewingStand 148 原值]。PlayerState/EnchantRegistry/World 直驱，rig 世界零接触，接 section67）
    section69_snow_golem(); // t1106 雪傀儡 + 生物名册审计合集（置尾先例沿用：r2076a 构建面真 rig 列[真 pc placeBlock 链：南瓜头位 + 雪块×2 → 恰一 MobSnowGolem 4 血朝玩家 + 3 块静默消耗 + JackOLantern 头位同门 + 缺雪块/非头位双负例零构建]，r2076b AI 行为列[雪 trail 脚下格铺 SnowLayer state=0 + 不叠层 + 入水融化 1HP/1s 慢扣 + 剪南瓜头信号恰一次幂等]，r2076c 雪球投掷伤害列[nearestHostile 发球 + 2.5s 节流 + 友好零发球 + golem 雪球 1 伤/烈焰相性 3 伤/玩家雪球烈焰相性 3 伤发射者无关/玩家 0 伤红闪/被动 0 伤 0 红闪]，r2076d 结构钉族[枚举位 12/尾 20/kMobTypeCount 21→23 lawful 修订 t1107 尾追加 + 常量族含 kSnowballBlazeDamage=3 + 雪块合成链 + 源钉族七针 + 相邻族零污染]。fresh 48×48×96 s82 + 真链 pc rig（t891 同门），rig 世界零接触，接 section68）
    section70_slime_villager(); // t1107 史莱姆 + 村民合集探针段（置尾先例沿用：r2077a 生成+分裂链[大→中×2-4→小×2-4 守恒+血量=尺寸档+盒三档+非法档回退]，r2077b 弹跳 AI+接触伤害[追击收敛+4/2/0 尺寸档伤害面]，r2077c 史莱姆块生成面[双跑逐位恒等+10% 带+深度门/骰面源钉]，r2077d 村民+结构钉族[被动人形+零攻击+死亡快照+枚举 21/22+0x28C..E 蛋表+源钉+相邻零污染]。fresh 48×48×96 s82，rig 世界零接触，接 section69）
    section71_village_worldgen(); // t1108 村庄 worldgen 探针段（置尾先例沿用：r2078a 站点表+区域投影确定性[同 seed 双世界站点/足迹恒等 + 计数绝对面]，r2078b spawn 桥契约[take/refill 恰额 + 二次 take 零重复 + 村民实数落债面]，r2078c 模板方块面[井/屋/田/路逐构件坐标定格断言 + 道路守卫负例]，r2078d 结构钉族[枚举位 + 偏移/半边常量 + 桥面源钉 + 相邻族零污染]。fresh 80×80×96 确定性 seed 表寻村，rig 世界零接触，接 section70）
    section72_t1109_closeout(); // t1109 池面收官批探针段（置尾先例沿用：r2079a 瓜茎收口柱[单果门:同型邻果不再结果 + 茎固定格 + 保持成熟；南瓜结果朝向=槽位向刻脸背茎 + 异型果不挡门；西瓜果 state 恒 0]，r2079b 矿区瓜收口柱[前提纠正留痕：1.0 任何版本矿井无野生瓜——全图甜瓜=0 负面扫描 + 野生南瓜对比锚 + 无 melon worldgen pass 源钉 + 矿井箱池瓜种行收口]，r2079c 沙漠村庄柱[Desert 准入后完好沙漠站点模板面同构平原 + 沙面地形 + 道路臂落 fBm 地表格 + 站点足迹两两不相交 + 双群系生成证据]，r2079d 结构钉族[新面源钉 + 登记注锚注 + 相邻族零污染；NEG 摘面行豁免不钉]。fresh 小世界族 48×48×96 s82 + 80×80×96 确定性 seed 表，rig 世界零接触，接 section71）
    section73_audio_t1110(); // t1110 名册审计 + 小缺口清偿合集探针段（置尾先例沿用：r2080a狼雪球零相性翻案柱[1.0 整型折半 (0+1)/2=0 = 雪球对狼 0 伤——野狼/驯服狼 hp 不变 + 击退/减速照挂 + 烈焰相性回归 + 猪被动回归]，r2080b 史莱姆弹跳着地信号柱[首跳着地沿恰 1 + 长窗 O(bounces) 双界 + 存活面]，r2080c 受击信号恰一次计数柱[真链 pc.attackMob mobAttacked(21/22) 冷却恰 1/泵越窗恰 2 + 村民恰 1]，r2080d 结构钉族[信号 decl + 翻案负面钉 kSnowballWolfDamage 不存在 + 音频三面注册铁律 + 路由别名族 + 资产链 + 相邻族零污染；NEG 摘面行豁免不钉]。fresh 48×48×96 s82，rig 世界零接触，接 section72）
    section74_roster_low_t1111(); // t1111 名册低件量批探针段（置尾先例沿用：r2081a 砂岩楼梯柱[6 砂岩阶梯合成命中 + 勘误对面 4 砂岩 2×2 恒答切制砂岩 + def 行逐字段 + 谓词路由 + 放置读回 + 石质音色 + kMc -1 + 楼梯三先例零污染]，r2081b 石/砂岩台阶双柱[双合成命中 + 四先例回归 + 双半砖合并映射两行 + def 双行 + 半遮光 7 + kMc 44 + 上下半读回 + 切制砂岩回归]，r2081c 史莱姆出生位柱[三档贴地出生位 y+halfH 精确断言 + 无嵌入 settle 面 + 缺省中档路径 + 小档 + 猪邻族零污染 + r2077a 契约复钉]，r2081d 结构钉族 + 金苹果负面钉[id/段位/kMc/def/谓词/合并/光照/音色/配方/图标/调色板/CMake 源钉族——NEG 双摘面豁免不钉 + 金苹果段位钉(lawful 修订 t1115/r2085 清偿——缺席钉退役改钉交付面) + 瓜片食面尾行幸存]。fresh 48×48×96 s82，rig 世界零接触，接 section73）
    section75_roster_mid_t1112(); // t1112 名册中件量批探针段（置尾先例沿用：r2082a 栅栏门柱[两行棒-板-棒合成命中 + def 行逐字段 + 开合碰撞分面 + 开合态读回 + 门族零污染]，r2082b 玻璃板柱[6 玻璃→16 合成 + def/谓词 + 十字条带选中/射线 + 零掉落 + kMc 102]，r2082c 蛋糕牛奶链柱[真 pc rig 分块食用状态机 + 空桶挤奶 + 饮面清效果返空桶 + 容器交换单点]，r2082d 猪骑乘柱 + 结构钉族[鞍→骑乘→Shift 下猪真链 + 推挤豁免 + 骑乘互斥 + 鞍死不掉负面钉(1.4.2+ 纪元) + 源钉族——NEG 双摘面豁免不钉]。fresh 48×48×96 s82，rig 世界零接触，接 section74）
    section76_roster_sign_t1113(); // t1113 名册大件批首单探针段（置尾先例沿用：r2083a 站牌柱[6 板 2×3 合成命中 + 木门形状孪生零污染 + def 逐字段 + 零碰撞/列顶 + 板面盒双朝向 + 放置真链(horizontalFacing^1 + signPlaced 恰一次) + 底面悬空拒放]，r2083b 挂墙牌柱[def + 贴墙满宽板面盒双朝向 + 真链侧面放置 + 失撑脱落 + 非完整立方拒放 + 相邻族零污染]，r2083c 文本链柱[SignStore 写读回/15 字符截断/全空清条目/双牌独立/round-trip + 放置→录入→读回真链 + Main.qml 接线 raw 钉(NEG-1 摘面行本腿持有)]，r2083d 结构钉族[值面 + 源钉族——NEG 双摘面豁免不钉 + 无再编辑/16 向不取负面钉]。fresh 48×48×96 s83，rig 世界零接触，接 section75）
    section77_roster_map_t1114(); // t1114 名册大件批第二单探针段（置尾先例沿用：r2084a 激活链柱[8 纸环合成命中 + 熔炉/画作环孪生零污染 + 双 id 段位/名面/调色板/maxStack 64 + 空地图右键激活真链(槽内转换 + 建库 + 首绘中心区)]，r2084b 探索填充柱[MapStore 行为面 + 持图探索 tick 真链双 rig 盒扫 + 不持图不扫描负例 + 清库惰性重建(会话口径重探索面)]，r2084c 显示面柱[Main.qml overlay 接线 raw 钉 + main.cpp provider 钉 + 注入行钉]，r2084d 结构钉族[源钉族——NEG 双摘面豁免不钉 + 罗盘芯/缩放克隆/不可堆叠/存档表四负面钉]。fresh 48×48×96 s84，rig 世界零接触，接 section76）
    section78_golden_apple_t1115(); // t1115 golden apple 完整链探针段（置尾先例沿用：r2085a 苹果掉落柱[橡树叶 1/200 直调大样本 6000 采样带断言 + 树苗/木棒族面幸存 + 云杉零苹果门 600 采样 + 消亡路径零苹果负钉(world.cpp 零 AppleId)]，r2085b 苹果食物柱[食面单一权威 +4/+4/瓜片 +2 幸存 + 非可饮面 + 真链进食 rig 饥饿 +4/消耗 1 件/foodBurped 恰一次/potionDrunk 零 + 名面]，r2085c 金苹果食物柱[真链进食 Regen I 30s 效果快照(type/level/seconds 带) + healed 2.5s 首脉冲 + 创造门面挂零不消耗]，r2085d 合成柱[8 金锭环+苹果心命中 3×3 门 + 四环孪生零污染(熔炉/地图/画作/闪烁西瓜) + 心片权威负例(瓜片心/金粒心无行)]，r2085e 结构钉族[源钉族——NEG 双摘面豁免不钉 + 无 Absorption/无 kMc/无进食时长加成/战利品零生苹果负面钉（t1116 lawful 修订——金苹果行入池，缺席钉退役改钉交付面）]。fresh 48×48×96 s85，rig 世界零接触，接 section77）
    section79_small_gaps_t1116(); // t1116 名册后小缺口批探针段（置尾先例沿用：r2086a 铁栏杆图标柱[iconFileForBlock case 源钉 + CMake 资源行钉 + 图标文件在盘非空钉(QImage alpha 扫描) + iconSourceForBlock 运行期非空钉 + 调色板行在册]，r2086b 蛋糕失撑柱[三写入口收口行为柱(4/5 参 setBlock + setWaterSilent 破支撑 → 当场破块零掉落 + 完整立方置换幸存面) + 真链放置预检同谓词(半砖顶拒/石坪顶放)]，r2086c 地牢箱柱[金苹果行逐字段+尾位钉 + 既有 9 行逐条幸存 + 总权重 118 + 千 seed roll 带 + 瓜种带回归 + 生苹果零行]，r2086d 结构钉族[值面 + 源钉族(decl/impl 头/静默挂点行/金苹果行/沿革锚/CMake 段行) + 双负面钉——NEG 双摘面豁免不钉]。fresh 48×48×96 s86，rig 世界零接触，接 section78）
    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    if (!legFilter.isEmpty()) {
        qInfo().noquote() << "=== total SKIP:" << skipCount << "===";
        // review0915 #5 顺手修：filter 模式追加实际执行腿组数（exit 0 只证明「选中腿全绿」）。
        qInfo().noquote() << "=== filtered: legFilter=" << legFilter
                          << ", ran" << s_filteredRanLegs << "legs ===";
    }
}
