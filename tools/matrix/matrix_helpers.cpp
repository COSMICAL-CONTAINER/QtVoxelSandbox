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

    qInfo().noquote() << "=== total FAIL:" << totalFail << "===";
    if (!legFilter.isEmpty()) {
        qInfo().noquote() << "=== total SKIP:" << skipCount << "===";
        // review0915 #5 顺手修：filter 模式追加实际执行腿组数（exit 0 只证明「选中腿全绿」）。
        qInfo().noquote() << "=== filtered: legFilter=" << legFilter
                          << ", ran" << s_filteredRanLegs << "legs ===";
    }
}
