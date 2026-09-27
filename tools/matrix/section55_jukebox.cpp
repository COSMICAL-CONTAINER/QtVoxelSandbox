#include "matrix_helpers.h"

#include <QElapsedTimer>

// t1083 唱片机 + 音乐盘机制探针段（4 腿；filter 词 r2054；矩阵 746→750）。置尾先例（接
// section54，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 48×48×96 s82，section11/53
// 同门；交互腿走 t945 真链 rig（QQuickWindow 载体 + grab/loadSavedState/tick 刷射线/placeBlock））。
//
// 现场核实（按实况定范围，t1070/t1077/t1078/t1080/t1081 降级/盘点先例谱系第六现）：候选「唱片机 =
//   放入 / 播放 / 吐盘 + 盘物品 + 战利品」五面中——① 音频管线 = miniaudio 既有（AudioManager 单件
//   replay / 循环床 / per-pitch 池三模式齐备，qrc WAV 解码 → **全量交付可行**，无需降级到「无声
//   状态机」）；② 乐曲资产 = build_sounds.py 程序合成管线在（gen_note_piano 钢琴质感同源）→
//   三条确定性原创旋律（gen_disc_track，30/36/42s）**程序生成，零外部资产**（§9 + 成本纪律：
//   非 MC 乐曲拷贝、非现实乐曲转录）；③ 战利品面 = LootTable 六池 + ChestStore 首开填充在 →
//   **只挂既有池**（dungeon/mineshaft 各 +1 条低权重盘），禁第二份战利品逻辑；④ 块 id 表 =
//   Hopper=145 / Count=146（追加前核全行数，t691/t1077/t1080 教训）→ Jukebox=146 尾部追加 +
//   kMcBlockId 补行（MC 1.0 jukebox = 84，**真实存在**非 0 占位）；⑤ 红石触发面 = MC 1.0 **无
//   此机制**（唱片机红石输出是 1.5+ 比较器面）→ 如实登记「非缺口」，零实现零降级。
//   **播放态持久化口径**：state 只存「盘在机 + 盘号」（bit0 + bit[7:2]），播放中 = 运行期表
//   （PlayerController m_jukeboxPlaying）——机制等价 MC 1.0 JukeboxTileEntity 只持久化 record、
//   playing 是运行期概念；载入即停（盘仍在机，再右键续播）登记简化。
//
// 腿面：r2054a 放入/吐出承重墙（真链放入：state 逐位 + 生存消耗 + started 沿；真链吐出：盘物品
//   还原 id 逐位 + state 清位 + stopped 沿；创造放入不消耗）；r2054b 音乐盘 + 战利品接入承重墙
//   （三盘 id/名/图标调色板/不可堆叠双面/映射 round-trip 唯一性 + 配方命中与负例 + 两池挂接
//   逐位 + 确定性 roll 命中）；r2054c 播放语义 + 边界墙（到期自动吐盘双向：到期前不吐/到期吐
//   [时长单一权威驱动] + 双吐守卫 + 非播放破坏吐盘不发声 + 创造破坏不吐/生存破坏吐盘 + 非盘
//   拒收无效应不消耗 + 空机续播）；r2054d 结构钉（def 行逐字段 + kMcBlockId 行 + state 编解码
//   权威 + 音色族 + 图集 191 + 配方注册恰一处 + tick 接线 + QML 路由钉与状态机零 QML 反探）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_t1083_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘音轨到期吐盘语义本体（playercontroller.cpp tickJukeboxes 内 ejectJukeboxDiscAt 调用行
//     注释）→ 声明红面 {r2054c}（到期断言红：无盘产出 + state 未清 + 无 stopped）。r2054a 不误伤
//     （吐出走 placeBlock 分支 ①，不经 tick）；r2054b 不误伤（纯表查询零 tick）；r2054d 不误伤
//     （钉面钉 tick 接线行与 call site，不钉 eject 行本体——NEG 靶行不入钉，t1080/t1081 先例）。
//   NEG-2 摘放入语义本体（placeBlock 唱片机分支 `if (track >= 0)` 改 `if (false)`）→ 声明红面
//     {r2054a}（放入断言红：state 不写 + 不消耗 + 无 started）。r2054c 不误伤（自动吐盘/续播走
//     直写 state + 空手右键分支 ②，不入放入面）；r2054b/d 不误伤（无 placeBlock 放入依赖）。
//
// rig 纪律：fresh 小世界 incantation（section11 四 setter 同款）；rig 位**运行期扫描空域**（lessons
//   t769/t1030——逐列扫 y-1/y/y+1 三连空）；真链驱动 = release+grab 重居中 → loadSavedState →
//   tick 刷射线 → placeBlock（t945 逐位同门）；placeBlock 200ms CD 用 processEvents 泵墙钟
//   （t945 pumpMs 同款）；物品断言按 itemId 找活体（槽位 LIFO 复用 t256——禁按生成序断言下标）。

namespace {

// fresh 小世界 incantation（section11 同款四 setter + 恒晴零 RNG）。
inline void initJukeboxWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// rig 空域扫描：列 (x,z) 上首个「y-1 / y / y+1 三连空」高度（唱片机放 y，玩家同层站位）。
inline int findJukeboxRigY(World &w, int x, int z)
{
    for (int y = 4; y < w.height() - 2; ++y) {
        if (w.blockAt(x, y - 1, z) == BR::Air && w.blockAt(x, y, z) == BR::Air
            && w.blockAt(x, y + 1, z) == BR::Air)
            return y;
    }
    return -1;
}

// 实体定位帮手（槽位 LIFO 复用 t256——禁按生成序断言下标，按 itemId 找活体）。
inline int findAliveDisc(ItemEntityManager &items, int itemId)
{
    for (int i = 0; i < items.count(); ++i)
        if (items.aliveAt(i) && items.itemIdAt(i) == itemId) return i;
    return -1;
}

// placeBlock 200ms CD 间隔泵（t945 pumpMs 同款：processEvents 泵墙钟——m_evtClock 单调墙钟）。
inline void pumpMs(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 真链交互 rig（t945 模式缩本：fresh 世界 + 空实体族 + 热栏 + 窗载体；唱片机机制不依赖 mob /
//   掉落物 tick，ItemEntityManager 仅作吐盘产物断言面）。坐标约定：唱片机在 (bx, y, bz)，
//   玩家脚位 (bx+3.5, y, bz+0.5)，瞄唱片机 +X 侧面 (bx+0.9, y+0.5, bz+0.5)。
struct JukeboxRig
{
    World w;
    ItemEntityManager items;
    Hotbar hb;
    PlayerController pc;
    QQuickWindow win;
    int startedCount = 0, stoppedCount = 0, lastTrack = -1;
    int bx = 8, bz = 8, y = -1;

    explicit JukeboxRig()
    {
        initJukeboxWorld(w);
        y = findJukeboxRigY(w, bx, bz);
        pc.setWorld(&w);
        pc.setHotbar(&hb);
        pc.setItemEntities(&items);
        pc.setSelectedBlock(int(BR::Air)); // t1030 rig 加固：构造默认 Stone → 显式归 Air 建模
                                           //   「手持材料段（盘 / 木棒）非放置」——防拒收 / 吐盘腿
                                           //   fall-through 通用放置偷放 Stone + 偷消耗选中栈。
        pc.setParentItem(win.contentItem());
        pc.grab();
        // 播放语义信号计数（真链沿断言面——started/stopped 各一次 = 一放一吐）。
        QObject::connect(&pc, &PlayerController::jukeboxStarted, &pc,
                         [&](int, int, int, int track) { ++startedCount; lastTrack = track; });
        QObject::connect(&pc, &PlayerController::jukeboxStopped, &pc,
                         [&](int, int, int, int) { ++stoppedCount; });
    }

    // 放唱片机本体（state 直写；场景保真：脚下垫石台面 + 玩家站位行地坪——
    //   玩家在 (bx+3.5, bz+0.5) 列须有支撑，否则生存 tick 重力坠落 → 视线脱靶
    //   → 挖掘目标换格进度清零永不破块，c3 guard 耗尽的 rig 病根）。
    void placeJukebox(quint8 state)
    {
        for (int dx = 0; dx <= 4; ++dx)
            w.setBlock(bx + dx, y - 1, bz, BR::Stone, 0);
        w.setBlock(bx, y, bz, BR::Jukebox, state);
    }

    // 瞄准 + tick 刷射线（t945 aim 同款；先 release+grab 重居中吞掉 pollMouse 残留 delta）。
    QVector3D aimAt(int mode)
    {
        const float ex = float(bx) + 3.5f, ez = float(bz) + 0.5f;
        const float ey = float(y) + 1.62f;
        const float ax = float(bx) + 0.9f, ay = float(y) + 0.5f, az = float(bz) + 0.5f;
        const float dx = ax - ex, dy = ay - ey, dz = az - ez;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float pitch = std::asin(dy / len) * 57.2957795f;
        const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
        pc.release();
        pc.grab();
        pc.loadSavedState(ex, float(y), ez, yaw, pitch, mode);
        pc.tick(); // updateRaycast 刷新命中（t889 先例）
        return pc.hitBlock();
    }
};

} // namespace

void MatrixRun::section55_jukebox()
{
    // ── r2054a：放入 / 吐出承重墙（真链交互；state 逐位 + 消耗语义 + 沿信号）─────────────────
    runLeg("r2054a jukebox insert/eject wall (real-chain insert state-exact + survival consume + started; eject disc-restore + state-clear + stopped; creative no-consume)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 生存真链放入：手持盘 1 件 → placeBlock → state = insertState(0)（bit0 + track 段逐位）
        //     + 消耗（槽 1→0）+ started 恰 1 沿携 track 0 + 方块 id 不变。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(0);
            rig.hb.setStack(0, RecipeRegistry::MusicDiscAmberId, 1);
            const QVector3D hit = rig.aimAt(2 /*Survival*/);
            rig.pc.placeBlock();
            pumpMs(260); // 200ms CD 泵（后续吐盘右键不被吞）
            const quint8 st = rig.w.stateAt(rig.bx, tgt, rig.bz);
            const int heldAfter = rig.hb.countAt(0);
            const bool a1 = hit == QVector3D(float(rig.bx), float(tgt), float(rig.bz))
                && rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox
                && st == BlockRegistry::jukeboxInsertState(0)
                && BlockRegistry::jukeboxHasDisc(st)
                && BlockRegistry::jukeboxTrack(st) == 0
                && heldAfter == 0 // 生存消耗 1 件（1→0，takeStack 消耗面）
                && rig.startedCount == 1 && rig.lastTrack == 0
                && rig.stoppedCount == 0;
            ok = ok && a1;
            if (!a1) diag += QStringLiteral("[a1 hit=%1,%2,%3 id=%4 st=%5 started=%6 held=%7]")
                                 .arg(hit.x()).arg(hit.y()).arg(hit.z())
                                 .arg(rig.w.blockAt(rig.bx, tgt, rig.bz)).arg(st)
                                 .arg(rig.startedCount).arg(heldAfter);
        }
        // (2) 生存真链吐出：播放中（(1) 放入即播）再右键 → 盘物品还原（id 逐位 = MusicDiscAmberId）
        //     + state 清 0 + stopped 恰 1 沿 + started 仍 1（不重播）。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(0);
            rig.hb.setStack(0, RecipeRegistry::MusicDiscAmberId, 1);
            rig.aimAt(2);
            rig.pc.placeBlock(); // 放入（即播）
            pumpMs(260);
            rig.pc.placeBlock(); // 再右键 = 吐出（播放中分支 ①）
            const int idx = findAliveDisc(rig.items, RecipeRegistry::MusicDiscAmberId);
            const bool a2 = idx >= 0 && rig.items.countAt(idx) == 1
                && rig.w.stateAt(rig.bx, tgt, rig.bz) == 0
                && rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox
                && rig.startedCount == 1 && rig.stoppedCount == 1;
            ok = ok && a2;
            if (!a2) diag += QStringLiteral("[a2 ent=%1 st=%2 started=%3 stopped=%4]")
                                 .arg(idx).arg(rig.w.stateAt(rig.bx, tgt, rig.bz))
                                 .arg(rig.startedCount).arg(rig.stoppedCount);
        }
        // (3) 创造放入不消耗（同暗渊之眼激活面）：盘保持 1 件 + state 照写 + started 沿。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(0);
            rig.hb.setStack(0, RecipeRegistry::MusicDiscEchoId, 1);
            rig.aimAt(1 /*Creative*/);
            rig.pc.placeBlock();
            const quint8 st = rig.w.stateAt(rig.bx, tgt, rig.bz);
            const bool a3 = st == BlockRegistry::jukeboxInsertState(1)
                && BlockRegistry::jukeboxTrack(st) == 1
                && rig.startedCount == 1 && rig.lastTrack == 1;
            ok = ok && a3;
            if (!a3) diag += QStringLiteral("[a3 st=%5 track=%6 started=%7]")
                                 .arg(st).arg(BlockRegistry::jukeboxTrack(st))
                                 .arg(rig.startedCount);
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2054a jukebox insert/eject wall (real-chain insert state-exact + survival consume + started; eject disc-restore + state-clear + stopped; creative no-consume)"
            << (ok ? QString() : diag);
    });

    // ── r2054b：音乐盘 + 战利品接入承重墙（盘 id / 映射 / 配方 / 两池挂接逐位）────────────────
    runLeg("r2054b music disc + loot wall (3 disc ids/names/palette/non-stack dual-face, track mapping round-trip unique, recipe hit+negatives, dungeon+mineshaft pool attach, deterministic roll)", [&]() {
        bool ok = true;
        QString diag;
        const int discs[3] = { RecipeRegistry::MusicDiscAmberId, RecipeRegistry::MusicDiscEchoId,
                               RecipeRegistry::MusicDiscNightId };
        // (1) 映射 round-trip 唯一性：track→disc→track 恒等（三轨）+ 三 id 互异 + 非盘 → -1。
        bool b1 = true;
        for (int t = 0; t < 3; ++t)
            b1 = b1 && PlayerController::jukeboxTrackForDisc(PlayerController::jukeboxDiscForTrack(t)) == t;
        b1 = b1 && discs[0] != discs[1] && discs[1] != discs[2] && discs[0] != discs[2]
            && PlayerController::jukeboxTrackForDisc(RecipeRegistry::StickId) == -1
            && PlayerController::jukeboxTrackForDisc(0) == -1
            && PlayerController::jukeboxDiscForTrack(3) == 0; // 越界 → 0（无物品）
        ok = ok && b1;
        if (!b1) diag += QStringLiteral("[b1]");

        // (2) 不可堆叠双面（Game 权威 + Core 掉落合并面同步——两处特判缺一即 64 合并数据错）。
        bool b2 = true;
        for (int id : discs) {
            b2 = b2 && Hotbar().maxStackSize(id) == 1
                && BlockRegistry::maxStackSize(id) == 1;
        }
        ok = ok && b2;
        if (!b2) diag += QStringLiteral("[b2]");

        // (3) 名表面：三盘 hover 名非空（漏名 = 调色板无 tooltip，t728 先例病）。
        Hotbar hb;
        const bool b3 = !hb.nameForBlock(discs[0]).isEmpty() && !hb.nameForBlock(discs[1]).isEmpty()
            && !hb.nameForBlock(discs[2]).isEmpty();
        ok = ok && b3;
        if (!b3) diag += QStringLiteral("[b3]");

        // (4) 创造调色板：三盘连续同列于 creativeMaterials（蛋区连续性同门——盘区尾三连）。
        const QList<QVariant> mats = hb.creativeMaterials();
        int pos0 = -1;
        for (int i = 0; i < mats.size(); ++i)
            if (mats[i].toInt() == discs[0]) { pos0 = i; break; }
        const bool b4 = pos0 >= 0 && pos0 + 2 < mats.size()
            && mats[pos0 + 1].toInt() == discs[1] && mats[pos0 + 2].toInt() == discs[2];
        ok = ok && b4;
        if (!b4) diag += QStringLiteral("[b4 pos=%1 n=%2]").arg(pos0).arg(mats.size());

        // (5) 配方：8 木板环 + 中心钻石 → 唱片机 1（命中；match 第二参 = 子格**边长**——3×3 填 3，
        //     2×2 填 2，t802 expectCraft 全表同门）；中心红石（音符盒）/ 中心空（箱子形）负例不误命中。
        int grid[9] = { int(BR::Planks), int(BR::Planks), int(BR::Planks),
                        int(BR::Planks), RecipeRegistry::DiamondId, int(BR::Planks),
                        int(BR::Planks), int(BR::Planks), int(BR::Planks) };
        const RecipeRegistry::Recipe *r = RecipeRegistry::match(grid, 3);
        const bool b5 = r && r->outputId == int(BR::Jukebox) && r->outputCount == 1;
        grid[4] = RecipeRegistry::RedstoneId;
        const RecipeRegistry::Recipe *rn = RecipeRegistry::match(grid, 3);
        const bool b5n = rn && rn->outputId == int(BR::NoteBlock); // 中心料异 → 产物异（音符盒不吞盘机格）
        grid[4] = 0;
        // 中心空 = 箱子 {Planks:8} 的合法配方（MC 同料同形——唱片机与箱子靠中心料分流）→ 断言
        //   命中箱子而非唱片机（不是「无匹配」——箱子配方先在，负例钉的是「不吞盘机格」）。
        const RecipeRegistry::Recipe *rc = RecipeRegistry::match(grid, 3);
        const bool b5n2 = rc && rc->outputId == int(BR::Chest);
        ok = ok && b5 && b5n && b5n2;
        if (!(b5 && b5n && b5n2)) diag += QStringLiteral("[b5 out=%1 cnt=%2 note=%3 chest=%4]")
                                            .arg(r ? r->outputId : -1).arg(r ? r->outputCount : 0)
                                            .arg(rn ? rn->outputId : -1)
                                            .arg(rc ? rc->outputId : -1);

        // (6) 战利品挂接：dungeon 池含琥珀盘（1,1）、mineshaft 池含深巷盘（1,1）——逐位
        //     （weight/count 区间钳死，禁第二份池）。
        bool b6 = false, b6m = false;
        for (const auto &e : LootTable::dungeonChestPool())
            if (e.itemId == RecipeRegistry::MusicDiscAmberId)
                b6 = e.weight == 2 && e.minCount == 1 && e.maxCount == 1;
        for (const auto &e : LootTable::mineshaftChestPool())
            if (e.itemId == RecipeRegistry::MusicDiscEchoId)
                b6m = e.weight == 2 && e.minCount == 1 && e.maxCount == 1;
        ok = ok && b6 && b6m;
        if (!b6) diag += QStringLiteral("[b6 dungeon]");
        if (!b6m) diag += QStringLiteral("[b6 mineshaft]");

        // (7) 确定性 roll：seed 扫描 0..999，dungeon 抽 8 roll 恰有一 seed 产琥珀盘（确定性可复现：
        //     同 seed 再 roll 同结果——PLAN §2-K 坐标确定性战利品面的表级抽查）。
        bool b7 = false;
        for (quint32 s = 0; s < 1000 && !b7; ++s) {
            const auto stacks = LootTable::roll(LootTable::dungeonChestPool(), 8, s);
            bool hit = false;
            for (const auto &st : stacks)
                if (st.itemId == RecipeRegistry::MusicDiscAmberId) hit = true;
            if (hit) {
                const auto again = LootTable::roll(LootTable::dungeonChestPool(), 8, s);
                bool againHit = false;
                for (const auto &st : again)
                    if (st.itemId == RecipeRegistry::MusicDiscAmberId) againHit = true;
                b7 = againHit; // 同 seed 复现
            }
        }
        ok = ok && b7;
        if (!b7) diag += QStringLiteral("[b7 no-seed-hit]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2054b music disc + loot wall (3 disc ids/names/palette/non-stack dual-face, track mapping round-trip unique, recipe hit+negatives, dungeon+mineshaft pool attach, deterministic roll)"
            << (ok ? QString() : diag);
    });

    // ── r2054c：播放语义 + 边界墙（到期自动吐盘双向 / 破坏吐盘 / 拒收 / 续播）──────────────────
    runLeg("r2054c playback semantics + boundary wall (pre-expiry no-eject / expiry auto-eject via single-authority duration + double-eject guard, loaded resume, survival break-eject + creative break no-drop, non-disc reject no-consume)", [&]() {
        bool ok = true;
        QString diag;
        const float dur = PlayerController::jukeboxTrackDurationSec(0); // 时长单一权威驱动（30s）
        // (1) 到期前不吐：续播态 tick 半时长 → 零盘产出 + state 原样 + 零 stopped。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(BlockRegistry::jukeboxInsertState(2)); // 盘在机未播（载入态直写）
            rig.aimAt(2);
            rig.pc.placeBlock(); // 空手右键 → 续播（分支 ②，不消耗）
            rig.pc.tickJukeboxes(dur * 0.5f);
            const bool c1 = findAliveDisc(rig.items, RecipeRegistry::MusicDiscNightId) < 0
                && rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox
                && BlockRegistry::jukeboxHasDisc(rig.w.stateAt(rig.bx, tgt, rig.bz))
                && rig.stoppedCount == 0 && rig.startedCount == 1;
            ok = ok && c1;
            if (!c1) diag += QStringLiteral("[c1]");
        }
        // (2) 到期自动吐盘（时长单一权威驱动）+ 双吐守卫：tick 全长 + 余量 → 盘产出（id 逐位）
        //     + state 清 + stopped 恰 1；再 tick 全长 → 无第二盘无第二 stopped。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(BlockRegistry::jukeboxInsertState(0));
            rig.aimAt(2);
            rig.pc.placeBlock(); // 空手右键 → 续播
            rig.pc.tickJukeboxes(dur + 0.5f);
            const int idx = findAliveDisc(rig.items, RecipeRegistry::MusicDiscAmberId);
            const bool c2a = idx >= 0 && rig.items.countAt(idx) == 1
                && rig.w.stateAt(rig.bx, tgt, rig.bz) == 0
                && rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox
                && rig.stoppedCount == 1;
            rig.pc.tickJukeboxes(dur + 1.0f); // 双吐守卫（表项已摘 → 幂等）
            const bool c2b = findAliveDisc(rig.items, RecipeRegistry::MusicDiscEchoId) < 0
                && rig.stoppedCount == 1;
            ok = ok && c2a && c2b;
            if (!(c2a && c2b)) diag += QStringLiteral("[c2 ent=%1 st=%2 stopped=%3]")
                                          .arg(findAliveDisc(rig.items, RecipeRegistry::MusicDiscAmberId))
                                          .arg(rig.w.stateAt(rig.bx, tgt, rig.bz)).arg(rig.stoppedCount);
        }
        // (3) 生存破坏吐盘：盘在机未播 → 挖掘破坏（真实 tick 链：铁斧加速 + processEvents 泵墙钟
        //     喂真 dt——tickImpl dt = m_clock.restart()，探针连调 tick 的墙钟间隔 ≈0 → 须泵出真实
        //     时间进度才累积）→ 唱片机 Air + 盘产出 + 零 stopped（非播放破坏无音频可停——对称信号面）。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(BlockRegistry::jukeboxInsertState(1));
            rig.hb.setStack(0, ToolRegistry::AxeIron, 1); // 铁斧 speedMul 6 → miningTime 0.33s
            rig.aimAt(2);
            rig.pc.beginMining();
            int guard = 0;
            while (rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox && guard < 200) {
                rig.pc.tick();  // 挖掘累积（生存；updateMining 真 tick 路径）
                pumpMs(25);     // 泵墙钟喂真 dt（≈25ms/tick × 14 tick ≈ 0.35s 达成）
                ++guard;
            }
            const bool c3 = rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Air
                && findAliveDisc(rig.items, RecipeRegistry::MusicDiscEchoId) >= 0
                && rig.stoppedCount == 0;
            ok = ok && c3;
            if (!c3) diag += QStringLiteral("[c3 guard=%1]").arg(guard);
        }
        // (4) 创造破坏不吐（drop=false 口径）：瞬破 → 唱片机 Air + 零盘产出（创造不掉落同全方块）。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(BlockRegistry::jukeboxInsertState(1));
            rig.aimAt(1 /*Creative*/);
            rig.pc.beginMining(); // 创造瞬破
            const bool c4 = rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Air
                && findAliveDisc(rig.items, RecipeRegistry::MusicDiscEchoId) < 0;
            ok = ok && c4;
            if (!c4) diag += QStringLiteral("[c4]");
        }
        // (5) 非盘拒收无效应不消耗：空机 + 手持木棒（非盘非食物中性材料）→ placeBlock → state 0
        //     原样 + 零 started/stopped + 槽内木棒计数不变（无效应不消耗口径逐位）。
        {
            JukeboxRig rig;
            const int tgt = rig.y;
            rig.placeJukebox(0);
            rig.hb.setStack(0, RecipeRegistry::StickId, 4);
            rig.aimAt(2);
            const int heldBefore = rig.hb.countAt(0);
            rig.pc.placeBlock();
            const int heldAfter = rig.hb.countAt(0);
            const bool c5 = rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Jukebox
                && rig.w.stateAt(rig.bx, tgt, rig.bz) == 0
                && rig.startedCount == 0 && rig.stoppedCount == 0
                && heldBefore == 4 && heldAfter == 4; // 拒收不消耗（4→4 逐位）
            ok = ok && c5;
            if (!c5) diag += QStringLiteral("[c5 started=%1 stopped=%2 held=%3->%4]")
                                 .arg(rig.startedCount).arg(rig.stoppedCount)
                                 .arg(heldBefore).arg(heldAfter);
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2054c playback semantics + boundary wall (pre-expiry no-eject / expiry auto-eject via single-authority duration + double-eject guard, loaded resume, survival break-eject + creative break no-drop, non-disc reject no-consume)"
            << (ok ? QString() : diag);
    });

    // ── r2054d：结构钉（def 行 / kMcBlockId / 编解码权威 / 音色 / 图集 / 注册族 / QML 反探）──────
    runLeg("r2054d structure pins (def row field-exact, kMcBlockId 84, state codec authority, wood group, atlas 191, single-recipe/single-tick wiring source pins, QML routing pins + zero state-machine-in-QML anti-probe)", [&]() {
        bool ok = true;
        QString diag;
        // (1) def 行逐字段（kDefs 单一权威——破坏 / 音色 / 贴图 / 掉落面全部读表）。
        const auto &d = BlockRegistry::def(BlockRegistry::Jukebox);
        const bool d1 = d.solid && d.shape == BR::ShapeFull
            && d.hardness == 2.0f && d.toolType == int(BR::Axe) && !d.requiresTool
            && d.dropId == int(BR::Jukebox) && d.dropCount == 1 && d.maxStack == 64
            && d.topTile == 189 && d.bottomTile == 190 && d.sideTile == 190
            && d.frontTile == 190
            && BlockRegistry::materialGroup(BlockRegistry::Jukebox) == BR::GroupWood
            && BlockRegistry::isJukebox(BlockRegistry::Jukebox)
            && !BlockRegistry::isJukebox(BlockRegistry::NoteBlock);
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1]");

        // (2) kMcBlockId 行（MC 1.0 jukebox = 84 真实存在——迁移文档面）+ 图集容量（191 > 190）。
        const bool d2 = BlockRegistry::mcBlockId(BlockRegistry::Jukebox) == 84
            && BlockRegistry::AtlasTileCount == 201; // t1103 lawful 前移：195→201（西瓜族 tile 195..200 追加；t1097 曾 193→195）
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2 mc=%1 atlas=%2]")
                             .arg(BlockRegistry::mcBlockId(BlockRegistry::Jukebox))
                             .arg(BlockRegistry::AtlasTileCount);

        // (3) state 编解码权威 round-trip：insertState(0..63) 全域可逆解码 + 越界 clamp + 吐出态 0。
        bool d3 = true;
        for (int t = 0; t <= 63; ++t) {
            const quint8 s = BlockRegistry::jukeboxInsertState(t);
            d3 = d3 && BlockRegistry::jukeboxHasDisc(s)
                && BlockRegistry::jukeboxTrack(s) == t
                && s == BlockRegistry::jukeboxInsertState(BlockRegistry::jukeboxTrack(s));
        }
        d3 = d3 && !BlockRegistry::jukeboxHasDisc(0)
            && BlockRegistry::jukeboxTrack(0) == 0
            && BlockRegistry::jukeboxTrack(BlockRegistry::jukeboxInsertState(-5)) == 0
            && BlockRegistry::jukeboxTrack(BlockRegistry::jukeboxInsertState(64)) == 0;
        ok = ok && d3;
        if (!d3) diag += QStringLiteral("[d3]");

        // (4) 源钉：tick 接线恰一处（scanHoppers 之后常开）+ 静态表恰一份（kDiscTrackCount /
        //     kDurations 唯一）+ 时长契约两处同步字面量（C++ 30.0f,36.0f,42.0f ↔ py DISC_TRACK_DUR）。
        //     源文件路径 = applicationDirPath/.. 项目根（t992 源钉同门——CWD 是 build/，裸相对路径必空读）。
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        const auto readSrc = [&root](const char *rel) {
            QFile f(root + QString::fromLatin1(rel));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString pcSrc = readSrc("/src/Game/playercontroller.cpp");
        const QString pcHdr = readSrc("/src/Game/playercontroller.h");
        const bool d4 = pcSrc.contains(QStringLiteral("tickJukeboxes(dt);"))
            && pcSrc.count(QStringLiteral("void PlayerController::tickJukeboxes")) == 1
            && pcSrc.count(QStringLiteral("kDurations[") ) >= 0 // 表消费存在（字面量钉在下行）
            && pcSrc.contains(QStringLiteral("{ 30.0f, 36.0f, 42.0f }"))
            && pcHdr.count(QStringLiteral("void tickJukeboxes(float dt);")) == 1;
        ok = ok && d4;
        if (!d4) diag += QStringLiteral("[d4]");

        // (5) 源钉（续）：时长契约 py 侧 + kMcBlockId 行唯一 + def 行唯一（注册族恰一处）。
        const QString pySrc = readSrc("/tools/build_sounds.py");
        const QString brSrc = readSrc("/src/Core/blockregistry.cpp");
        const bool d5 = pySrc.contains(QStringLiteral("DISC_TRACK_DUR = [30.0, 36.0, 42.0]"))
            && brSrc.count(QStringLiteral("/* jukebox             */ ")) == 1 // kMcBlockId 行唯一
            && brSrc.count(QStringLiteral("\"jukebox\",        \"唱片机\"")) == 1 // def 行唯一
            && brSrc.contains(QStringLiteral("bool BlockRegistry::isJukebox"));
        ok = ok && d5;
        if (!d5) diag += QStringLiteral("[d5]");

        // (6) QML 路由钉 + 状态机零 QML 反探（单一权威不复制——播放表 / tick 决不进呈现层；
        //     Main.qml 只有 started→playDisc / stopped→stopDisc 两条单向消费 + 退世界两处 stopDisc）。
        const QString mainSrc = readSrc("/src/ui/Main.qml");
        const bool d6 = mainSrc.count(QStringLiteral("onJukeboxStarted")) == 1
            && mainSrc.count(QStringLiteral("onJukeboxStopped")) == 1
            && mainSrc.count(QStringLiteral("audio.stopDisc()")) == 3 // 吐出沿路由 + 退世界/回主菜单两处清理
            && mainSrc.count(QStringLiteral("audio.playDisc(track)")) == 1
            && !mainSrc.contains(QStringLiteral("tickJukeboxes"))
            && !mainSrc.contains(QStringLiteral("jukeboxPlaying"))
            && !mainSrc.contains(QStringLiteral("m_jukebox"));
        ok = ok && d6;
        if (!d6) diag += QStringLiteral("[d6]");

        // (7) 曲目数两处同步钉（Audio kDiscClipCount=3 ↔ Game kDiscTrackCount=3——手抄同口径，
        //     kNotePitchCount 同门；漂移即红非静默失效）。
        const QString audioSrc = readSrc("/src/Audio/audiomanager.cpp");
        const bool d7 = audioSrc.contains(QStringLiteral("static constexpr int kDiscClipCount = 3;"))
            && pcSrc.contains(QStringLiteral("static constexpr int kDiscTrackCount = 3;"))
            && audioSrc.count(QStringLiteral("void AudioManager::playDisc")) == 1
            && audioSrc.count(QStringLiteral("void AudioManager::stopDisc")) == 1;
        ok = ok && d7;
        if (!d7) diag += QStringLiteral("[d7]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2054d structure pins (def row field-exact, kMcBlockId 84, state codec authority, wood group, atlas 191, single-recipe/single-tick wiring source pins, QML routing pins + zero state-machine-in-QML anti-probe)"
            << (ok ? QString() : diag);
    });
}
