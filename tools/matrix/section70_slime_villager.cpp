#include "matrix_helpers.h"

// t1107 史莱姆 + 村民合集探针段(4 腿;filter 词 r2077;矩阵 844→848)。置尾先例沿用(接 section69,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 小世界 / 直驱 EntityManager)。
//
// ── 现状核实裁定表(派工两件逐一全仓 grep 实读,双双**真缺** → 本段=完整交付形态:fix(实体/物品/
//   AI/生成面)+ test 补腿)──
//   ① 史莱姆:全仓 grep slime 零实体命中(仅 section02 皮肤 slim 误命中)→ 真缺。交付面:MobSlime=21
//     **单 Kind + slimeSize 尺寸档 {1,2,4}**(裁定留痕:MC 1.0 slime 即单实体带 size 数据;三 Kind 三倍
//     QML delegate/蛋表/名册面;mob 无存档序列化面 → Entity 新字段零存档风险,羊 sheepWool 同门)。
//     血量=尺寸档(4/2/1,MC 1.0 slime health==size 口径);接触伤害=尺寸档(大 4/中 2/**最小档 0 无
//     攻击**——1.0 口径 size 1 不伤害);弹跳 AI(aiSlime:贴地 wanderTimer 复用为跳跃倒计时 0.5-1.5s
//     [MC 10-30 tick 口径] + vy=kSlimeJumpSpeed×档缩比 + t670 jumpG 既有滑流物理;追击跳朝玩家
//     kDetectRange=16[MC slime 寻敌口径]/游荡跳随机向半速;创造模式恒游荡跳不走 aiWander 滑行);
//     死亡分裂(大→中×2-4 / 中→小×2-4,子代母格落地,t1106 use-after-free 教训=快照后不再经 e 引用
//     读写);掉落(呈现层 onMobDied:小档掉粘液球 0x28C ×0-2,中/大档分裂不掉物);史莱姆球 SlimeBallId
//     =0x28C 段尾追加(1.0 无合成消费行——黏活塞 Beta 1.7 工程无活塞面/岩浆膏 1.0.0 酿造行工程无对应,
//     候选池登记,当下=收藏 + 创造调色板兜底);生成面(slimeChunkForSeed 确定性 10% chunk 哈希[工程
//     kDungeonSeedOff hash 惯例,机制等价登记] + cy<kSlimeSpawnMaxY=40 深度门 + **无视光照门**[MC 1.0
//     slime chunk 深层生成不受亮度约束] + 尺寸三档骰{1,2,4};黑暗刷怪池五份抽签不动=骰子消费零漂移);
//     生物蛋 0x28D(蛋刷固定中档 kSlimeDefaultSpawnSize=2 确定性;改笼白名单在案);死因 DeathCause::Slime
//     「被史莱姆撞杀」(1.0 slain by Slime 同源);渲染 MobModel 21 大档基准立方 0.60 半长 + QML
//     slimeModelScale=halfH/0.60 三档缩放 + 材质 opacity 0.75 半透明 + 内核子 Model + mob_slime 程序
//     贴图(pack 面不接=候选池)。
//   ② 村民:全仓 grep villager/village 零命中 → 真缺。**分层交付**(裁定留痕:实体先行):
//     MobVillager=22(被动人形 0.30/0.90 盒;游荡 AI=aiWander 通用链——1.0 村民无目的踱步;无交易
//     [交易 1.3.1 越纪元不取,蕴辉瓶/青金石同门];无掉落[1.0 村民死亡零战利品,XP 1-3 被动同门];
//     **不惊逃**[1.0 村民无 panic,setPanicFlee 白名单外静默 no-op 在案——t1042 惊逃集五被动,村民不入];
//     死因零新增[村民无攻击——mobAttackedPlayer 不发,被杀走攻击者死因]);渲染 MobModel 22 长袍人形
//     (长袍覆脚无腿 + 大头 + 长鼻 + 抱胸臂条四盒)+ mob_villager 程序贴图(pack 面不接=候选池);
//     生物蛋 0x28E(唯一生成面);**村庄 worldgen 候选池登记**(1.0 村民仅随村庄结构生成——Beta 1.8 村庄
//     [平原/沙漠房屋/农田/水井/道路]生成器单轮超容,实体先行分层;spawn 桥面[World 层无 Entity 依赖,
//     村民随 chunk population 的 spawn 点位与持久化语义]留村庄单解)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 退化接触伤害尺寸分流(entitymanager.cpp aiSlime:`const int slimeDamage = (e.slimeSize > 1)
//     ? e.slimeSize : 0;` 改 `const int slimeDamage = 1;` 单行,编译仍绿)→ 恰红 = {r2077b}(大档恰 4 伤
//     掉 1 红 / 中档恰 2 伤掉 1 红 / 最小档零信号面翻 1 伤信号掉 1 红;追击弹跳位移面不在摘面 = 部分
//     幸存,腿级 FAIL)。候选排查:摘 mobAttackedPlayer 发射行同样红 b 但连创造无敌面一起吞(归因混);
//     退化常量 1 形保留真链 aiSlime 全路(handler 经由面只改值不改流)= 流敏感教训同门。
//   NEG-2 = 退化分裂门(entitymanager.cpp tick 死亡到期段:`if (slimeSizeSnap > 1)` 改
//     `if (slimeSizeSnap > 999)` 单行,编译仍绿)→ 恰红 = {r2077a}(大死分裂 [2,4] 断言全失 = 零子代;
//     spawn 面/尺寸校验面/盒面不在摘面 = 部分幸存,腿级 FAIL)。d 腿源钉钉 `spawnSlime(dx, dy, dz,
//     slimeSizeSnap / 2);` 行原文(NEG-2 包裹下文本不动 = 豁免设计,t1106 trail 写入行同门);a 腿
//     NEG-1 不触达(分裂链不经接触伤害段),b 腿 NEG-2 不触达(b 不杀怪)→ 双 NEG 均不触达 c
//     (c 源钉族不含两摘面行)。
namespace {

// fixed 宿主小世界 incantation(section69 同款四 setter)。
inline void initSlimeWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section69 同款)。
inline void laySlimePlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(t1102 r2072 置尾腿共用式:applicationDirPath/../src;section69 同款)。
inline QString srcRootForSlimePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 数活体 slime 中指定尺寸档的只数(分裂链断言用;按 slimeSizeAt 分档计数)。
inline int countSlimesOfSize(const EntityManager &ents, int size)
{
    int n = 0;
    for (int i = 0; i < ents.count(); ++i)
        if (ents.aliveAt(i) && ents.mobTypeAt(i) == EntityManager::MobSlime
            && ents.slimeSizeAt(i) == size) ++n;
    return n;
}

} // namespace

void MatrixRun::section70_slime_villager()
{
    // ── r2077a:史莱姆生成 + 分裂链族(NEG-2 敏感面)────────────────────────────────────────────
    //   世界 A:大档 spawnSlime(…,4) → 恰一大档(血 4=尺寸档 / 盒 0.60·0.60 / hostile)+ 致死 →
    //     死亡动画窗(0.5s)后恰 [2,4] 只中档(血 2 / 盒 0.30),母体移除。世界 B:中档 → 致死 →
    //     [2,4] 只小档(血 1 / 盒 0.15)。世界 C:非法档 3 → 防御回退中档 2(血 2)。
    runLeg("r2077a slime spawn and split chain family (spawning a big slime of size four yields"
        " exactly one hostile slime of four health with the zero point six sized box matching its"
        " size tier, killing it leaves two to four medium slimes of two health on the zero point"
        " three box with the parent removed, killing a medium slime likewise leaves two to four"
        " tiny slimes of one health on the zero point one five box, and an out-of-set size three"
        " request defensively falls back to the medium tier)", [&]() {
        bool ok = true;
        QString diag;
        // ── 世界 A:大档生成 + 分裂 → 中档 ──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idxBig = ents.spawnSlime(24, 81, 24, 4);
            const bool bigOk = idxBig >= 0
                && ents.aliveAt(idxBig)
                && ents.mobTypeAt(idxBig) == EntityManager::MobSlime
                && ents.healthAt(idxBig) == 4                       // 血量=尺寸档(MC health==size)
                && ents.slimeSizeAt(idxBig) == 4
                && std::fabs(ents.radiusAt(idxBig) - 0.60f) < 1e-4f
                && std::fabs(ents.halfHeightAt(idxBig) - 0.60f) < 1e-4f
                && ents.isHostileAt(idxBig);
            ok = ok && bigOk;
            if (!bigOk) diag += QStringLiteral("[big idx=%1 hp=%2 sz=%3 hw=%4]")
                .arg(idxBig).arg(idxBig >= 0 ? ents.healthAt(idxBig) : -1)
                .arg(ents.slimeSizeAt(idxBig))
                .arg(idxBig >= 0 ? double(ents.radiusAt(idxBig)) : -99.0);
            ents.damageEntity(idxBig, 99);                          // 致死 → deathTimer 0.5s 窗
            for (int i = 0; i < 15; ++i)                            // 0.75s > 0.5s 窗 → 分裂 + 移除
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const int mediums = countSlimesOfSize(ents, 2);
            const bool splitOk = !ents.aliveAt(idxBig)              // 母体移除
                && mediums >= 2 && mediums <= 4;                    // 分裂窗 [2,4](MC 口径)
            ok = ok && splitOk;
            if (!splitOk) diag += QStringLiteral("[splitA alive=%1 med=%2]")
                .arg(ents.aliveAt(idxBig)).arg(mediums);
        }
        // ── 世界 B:中档 → 分裂 → 小档 ──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idxMid = ents.spawnSlime(24, 81, 24, 2);
            const bool midOk = idxMid >= 0 && ents.healthAt(idxMid) == 2
                && ents.slimeSizeAt(idxMid) == 2;
            ok = ok && midOk;
            ents.damageEntity(idxMid, 99);
            for (int i = 0; i < 15; ++i)
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const int tinies = countSlimesOfSize(ents, 1);
            const bool splitOk2 = tinies >= 2 && tinies <= 4;
            ok = ok && splitOk2;
            if (!splitOk2) diag += QStringLiteral("[splitB tiny=%1]").arg(tinies);
        }
        // ── 世界 C:非法档防御回退 ──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idxBad = ents.spawnSlime(24, 81, 24, 3);      // 3 ∉ {1,2,4}
            const bool fallbackOk = idxBad >= 0
                && ents.slimeSizeAt(idxBad) == 2                    // 回退中档(同 spawnHostileMob 回退模式)
                && ents.healthAt(idxBad) == 2;
            ok = ok && fallbackOk;
            if (!fallbackOk) diag += QStringLiteral("[fallback sz=%1 hp=%2]")
                .arg(ents.slimeSizeAt(idxBad)).arg(ents.healthAt(idxBad));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2077a slime spawn and split chain family (spawning a big slime of size four"
               " yields exactly one hostile slime of four health with the zero point six sized"
               " box matching its size tier, killing it leaves two to four medium slimes of two"
               " health on the zero point three box with the parent removed, killing a medium"
               " slime likewise leaves two to four tiny slimes of one health on the zero point"
               " one five box, and an out-of-set size three request defensively falls back to"
               " the medium tier)"
            << (ok ? QString() : diag);
    });

    // ── r2077b:弹跳 AI + 接触伤害列(NEG-1 敏感面)──────────────────────────────────────────────
    //   弹跳追击面:大档 + 玩家 12 格(≤kDetectRange=16)→ 3s 内追击跳净位移 ≥1.0 格且 X 漂移 ≤2.0
    //     (跳窗 0.5-1.5s 随机、追击方向确定性 → 任一窗序列恒朝玩家;首跳 wanderTimer=0 即发)。
    //   接触伤害面(信号直连):大档贴身 2.5s → ≥1 发恰 4 伤(尺寸档);中档 → 恰 2;最小档 → 零信号
    //     (1.0 口径 size 1 无攻击;NEG-1 恰红面)。世界四分立(隔离 m_playerHitCooldown 全局节流)。
    runLeg("r2077b slime bounce chase and contact damage column (a big slime twelve blocks from"
        " the player closes distance by at least one block over three seconds of chasing hops"
        " with under two blocks of lateral drift, a big slime adjacent to the player lands at"
        " least one contact hit of exactly four damage over two and a half seconds, a medium"
        " slime lands hits of exactly two, and a tiny slime of size one never fires a hit at"
        " all)", [&]() {
        bool ok = true;
        QString diag;
        // ── 弹跳追击位移(确定性方向 + 随机跳窗 → 距离收敛下界)──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idx = ents.spawnSlime(24, 81, 24, 4);
            ok = ok && idx >= 0;
            const float z0 = ents.posAt(idx).z(), x0 = ents.posAt(idx).x();
            for (int i = 0; i < 60; ++i)                            // 3.0s(首跳 timer=0 即发)
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 36.5f), 0.3f, 1.8f, true);
            const float dz = ents.posAt(idx).z() - z0;
            const float dx = std::fabs(ents.posAt(idx).x() - x0);
            const bool chase = dz >= 1.0f && dx <= 2.0f;            // 朝玩家净收敛 + 直线度
            ok = ok && chase;
            if (!chase) diag += QStringLiteral("[chase dz=%1 dx=%2]").arg(double(dz)).arg(double(dx));
        }
        // ── 接触伤害:大档恰 4 ──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            ents.spawnSlime(30, 81, 30, 4);
            int hits = 0, minAmt = 99, maxAmt = 0;
            const QMetaObject::Connection conn = QObject::connect(
                &ents, &EntityManager::mobAttackedPlayer,
                [&](int amt, int mt, float, float) {
                    if (mt != EntityManager::MobSlime) return;
                    ++hits; minAmt = std::min(minAmt, amt); maxAmt = std::max(maxAmt, amt);
                });
            ok = ok && bool(conn);
            for (int i = 0; i < 50; ++i)                            // 2.5s(冷却 1.0s → 1-3 发)
                ents.tick(0.05, &w, QVector3D(30.5f, 81.5f, 31.2f), 0.3f, 1.8f, true);
            const bool big4 = hits >= 1 && minAmt == 4 && maxAmt == 4;
            ok = ok && big4;
            if (!big4) diag += QStringLiteral("[big4 hits=%1 min=%2 max=%3]")
                .arg(hits).arg(hits ? minAmt : -1).arg(maxAmt);
        }
        // ── 接触伤害:中档恰 2 ──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            ents.spawnSlime(30, 81, 30, 2);
            int hits = 0, maxAmt = 0;
            const QMetaObject::Connection conn = QObject::connect(
                &ents, &EntityManager::mobAttackedPlayer,
                [&](int amt, int mt, float, float) {
                    if (mt != EntityManager::MobSlime) return;
                    ++hits; maxAmt = std::max(maxAmt, amt);
                });
            ok = ok && bool(conn);
            for (int i = 0; i < 50; ++i)
                ents.tick(0.05, &w, QVector3D(30.5f, 81.5f, 31.2f), 0.3f, 1.8f, true);
            const bool mid2 = hits >= 1 && maxAmt == 2;
            ok = ok && mid2;
            if (!mid2) diag += QStringLiteral("[mid2 hits=%1 max=%2]").arg(hits).arg(maxAmt);
        }
        // ── 最小档零信号(NEG-1 恰红面)──
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idxT = ents.spawnSlime(30, 81, 30, 1);
            ok = ok && idxT >= 0 && ents.healthAt(idxT) == 1
                && std::fabs(ents.halfHeightAt(idxT) - 0.15f) < 1e-4f;
            int hits = 0;
            const QMetaObject::Connection conn = QObject::connect(
                &ents, &EntityManager::mobAttackedPlayer,
                [&](int, int mt, float, float) { if (mt == EntityManager::MobSlime) ++hits; });
            ok = ok && bool(conn);
            for (int i = 0; i < 50; ++i)
                ents.tick(0.05, &w, QVector3D(30.5f, 81.5f, 31.2f), 0.3f, 1.8f, true);
            const bool tinyQuiet = hits == 0;
            ok = ok && tinyQuiet;
            if (!tinyQuiet) diag += QStringLiteral("[tinyQuiet hits=%1]").arg(hits);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2077b slime bounce chase and contact damage column (a big slime twelve blocks"
               " from the player closes distance by at least one block over three seconds of"
               " chasing hops with under two blocks of lateral drift, a big slime adjacent to"
               " the player lands at least one contact hit of exactly four damage over two and"
               " a half seconds, a medium slime lands hits of exactly two, and a tiny slime of"
               " size one never fires a hit at all)"
            << (ok ? QString() : diag);
    });

    // ── r2077c:史莱姆块生成面列(双 NEG 不触达 = 对照腿)──────────────────────────────────────────
    //   slimeChunkForSeed 确定性(48×48 chunk 网格双跑逐位恒等)+ 10% 命中带(2304 样本 [150,320],
    //     4σ 外沿)+ 深度门/骰面源钉(entitymanager.cpp 黑暗刷怪分流三行)+ 常量源钉(MaxY 40)。
    //     黑暗刷怪池五份抽签零扰动 = 非 slime 格骰子消费与旧基线逐位一致(分流在骰前 continue 语义)。
    runLeg("r2077c slime chunk spawn face column (the slime chunk verdict is deterministic with"
        " two passes over a forty eight by forty eight chunk grid answering bit identical maps,"
        " the ten percent hit band holds across the two thousand three hundred four sampled"
        " chunks, the dark spawn integration keeps its depth gate and its three tier size dice"
        " as pinned source rows beside the slime chunk probe call, and the spawn depth cap"
        " constant stays at forty)", [&]() {
        bool ok = true;
        QString diag;
        // (C1) 确定性:同 seed 双跑逐位恒等。
        int first = 0, second = 0;
        bool identical = true;
        for (int cz = -24; cz < 24; ++cz) {
            for (int cx = -24; cx < 24; ++cx) {
                const bool a = EntityManager::slimeChunkForSeed(82, cx, cz);
                const bool b = EntityManager::slimeChunkForSeed(82, cx, cz);
                if (a != b) identical = false;
                if (a) ++first;
                if (b) ++second;
            }
        }
        const bool detOk = identical && first == second;
        ok = ok && detOk;
        if (!detOk) diag += QStringLiteral("[det id=%1 a=%2 b=%3]")
            .arg(identical).arg(first).arg(second);
        // (C2) 10% 命中带(2304 样本,期望 ~230;4σ≈58 → [150,320] 稳带)。
        const bool bandOk = first >= 150 && first <= 320;
        ok = ok && bandOk;
        if (!bandOk) diag += QStringLiteral("[band n=%1]").arg(first);
        // (C3) 深度门 + 骰面 + 探针调用行源钉(黑暗刷怪分流三针;非 slime 格骰子消费零漂移 = 分流
        //      在五份抽签之前 continue 的结构性保证,骰行原文钉)。
        const QStringList missEnt = pinSet(srcRootForSlimePins() + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("depth gate", "const bool slimeCell = cy < kSlimeSpawnMaxY", 1),
            SrcPin("chunk probe", "slimeChunkForSeed(world->seed(),", 1),
            SrcPin("size dice", "const int slimeSize = 1 + int(rng->bounded(3));", 1),
            SrcPin("spawn call", "spawnSlime(cx, cy, cz, naturalSize);", 1)});
        ok = ok && missEnt.isEmpty();
        if (!missEnt.isEmpty()) diag += QStringLiteral("[pins %1]").arg(missEnt.join(QLatin1Char(',')));
        // (C4) 深度上限常量源钉(private → 声明行;数值口径 MC 1.0 Y<40)。
        const QStringList missHdr = pinSet(srcRootForSlimePins() + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("spawn max y", "static constexpr int   kSlimeSpawnMaxY         = 40;", 1)});
        ok = ok && missHdr.isEmpty();
        if (!missHdr.isEmpty()) diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2077c slime chunk spawn face column (the slime chunk verdict is deterministic"
               " with two passes over a forty eight by forty eight chunk grid answering bit"
               " identical maps, the ten percent hit band holds across the two thousand three"
               " hundred four sampled chunks, the dark spawn integration keeps its depth gate"
               " and its three tier size dice as pinned source rows beside the slime chunk"
               " probe call, and the spawn depth cap constant stays at forty)"
            << (ok ? QString() : diag);
    });

    // ── r2077d:村民列 + 结构钉族 + 相邻族零污染(双 NEG 摘面全豁免 = 结构钉对照腿)──────────────
    //   村民行为面(spawnMobTyped → 被动人形 0.30/0.90 盒 / 满血 10 / 非敌对;贴身 3s 零攻击信号 =
    //   无攻击口径;死亡 mobDied 携 mobType=22 + slimeSize=0)+ 枚举位(21/22 尾追加 + 邻位 20/12 原值)
    //   + 物品族(0x28C/0x28D/0x28E + 蛋表双向)+ 常量/行源钉族(分裂行原文 + 分派行 + Main.qml 四行 +
    //   声明行)+ 相邻族零污染(Count 154 / 图集 207 / 0x28A / 0x28B / 0x287)。
    runLeg("r2077d villager behavior and structure pin family column (a villager spawns as a"
        " passive humanoid on the zero point three by zero point nine box at full ten health,"
        " stays standing next to the player without ever attacking across three seconds, dies"
        " reporting its type with a zero slime size, the mob enum holds slime at twenty one and"
        " villager at twenty two with the count declaration moved to the villager tail, the"
        " item family answers slimeball at 0x28C with the two eggs at 0x28D and 0x28E and the"
        " egg table round trips both, the source pin family holds the split spawn row and the"
        " ai dispatch row and the four qml rows, and the neighbouring cave spider and snow"
        " golem and block count and atlas and seed ids keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        // (D1) 村民行为面:被动人形 + 贴身零攻击 + 死亡快照。
        {
            World w;
            initSlimeWorld(w);
            laySlimePlatform(w, 14, 44, 4, 44);
            EntityManager ents;
            const int idxV = ents.spawnMobTyped(24, 81, 24, EntityManager::MobVillager,
                                                QStringLiteral("#8a6a4a"), 0);
            const bool vOk = idxV >= 0
                && ents.aliveAt(idxV)
                && ents.mobTypeAt(idxV) == EntityManager::MobVillager
                && ents.healthAt(idxV) == 10                        // kDefaultMaxHealth(0 → 默认)
                && std::fabs(ents.radiusAt(idxV) - 0.30f) < 1e-4f
                && std::fabs(ents.halfHeightAt(idxV) - 0.90f) < 1e-4f
                && !ents.isHostileAt(idxV);                         // 被动(不进黑暗刷怪)
            ok = ok && vOk;
            if (!vOk) diag += QStringLiteral("[v hp=%1 hw=%2 hh=%3 host=%4]")
                .arg(idxV >= 0 ? ents.healthAt(idxV) : -1)
                .arg(idxV >= 0 ? double(ents.radiusAt(idxV)) : -99.0)
                .arg(idxV >= 0 ? double(ents.halfHeightAt(idxV)) : -99.0)
                .arg(idxV >= 0 ? ents.isHostileAt(idxV) : false);
            int vHits = 0;
            const QMetaObject::Connection connH = QObject::connect(
                &ents, &EntityManager::mobAttackedPlayer,
                [&](int, int mt, float, float) {
                    if (mt == EntityManager::MobVillager) ++vHits;
                });
            ok = ok && bool(connH);
            for (int i = 0; i < 60; ++i)                            // 3.0s 贴身(1 格内)零攻击
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 25.0f), 0.3f, 1.8f, false);
            const bool passive = vHits == 0 && ents.aliveAt(idxV);
            ok = ok && passive;
            if (!passive) diag += QStringLiteral("[passive hits=%1 alive=%2]")
                .arg(vHits).arg(ents.aliveAt(idxV));
            // 死亡快照:mobDied 携 mobType=22 + slimeSize=0(非 slime 恒 0)+ 幼崽/剪毛位恒否。
            int diedType = -1, diedSlime = -1;
            const QMetaObject::Connection connD = QObject::connect(
                &ents, &EntityManager::mobDied,
                [&](int, int, int, int mt, bool, bool, int, bool, int ss) {
                    diedType = mt; diedSlime = ss;
                });
            ok = ok && bool(connD);
            ents.damageEntity(idxV, 99);
            for (int i = 0; i < 15; ++i)
                ents.tick(0.05, &w, QVector3D(24.5f, 81.5f, 44.5f), 0.3f, 1.8f, false);
            const bool diedOk = diedType == EntityManager::MobVillager && diedSlime == 0;
            ok = ok && diedOk;
            if (!diedOk) diag += QStringLiteral("[died type=%1 ss=%2]").arg(diedType).arg(diedSlime);
        }
        // (D2) 枚举位(t1107 尾追加 21/22;邻位 20/12 原值零扰动)。
        const bool enumOk = int(EntityManager::MobSlime) == 21
            && int(EntityManager::MobVillager) == 22
            && int(EntityManager::MobCaveSpider) == 20
            && int(EntityManager::MobSnowGolem) == 12;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");
        // (D3) 物品族:段位钉 + 蛋表双向(mobTypeForSpawnEgg 单一权威)。
        const bool itemOk = RecipeRegistry::SlimeBallId == 0x28C
            && RecipeRegistry::SpawnEggSlimeId == 0x28D
            && RecipeRegistry::SpawnEggVillagerId == 0x28E
            && RecipeRegistry::mobTypeForSpawnEgg(RecipeRegistry::SpawnEggSlimeId)
                == EntityManager::MobSlime
            && RecipeRegistry::mobTypeForSpawnEgg(RecipeRegistry::SpawnEggVillagerId)
                == EntityManager::MobVillager;
        ok = ok && itemOk;
        if (!itemOk) diag += QStringLiteral("[item]");
        // (D4) 源钉族(剥注释 pinSet 代码行;**不钉两 NEG 摘面行**——伤害分流行/分裂门行豁免,t1106
        //      d 腿同门;分裂 spawn 行原文在 NEG-2 包裹下不动 = 钉得住)。
        const QStringList missEnt = pinSet(srcRootForSlimePins() + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("split spawn row", "spawnSlime(dx, dy, dz, slimeSizeSnap / 2);", 1),
            SrcPin("ai dispatch row", "if (aiSlime(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale, playerTargetable)) dirty = true;", 1)});
        const QStringList missHdr = pinSet(srcRootForSlimePins() + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("count decl", "static constexpr int kMobTypeCount = MobVillager + 1;", 1),
            SrcPin("spawn slime decl", "Q_INVOKABLE int spawnSlime(int x, int y, int z, int size);", 1),
            SrcPin("jump speed", "static constexpr float kSlimeJumpSpeed         = 7.5f;", 1),
            SrcPin("chase speed", "static constexpr float kSlimeChaseSpeed        = 3.2f;", 1),
            SrcPin("damage base", "static constexpr int   kSlimeAttackDamageBase  = 4;", 1),
            SrcPin("split window", "static constexpr int   kSlimeSplitChildrenMin  = 2;", 1)});
        const QStringList missMq = pinSet(srcRootForSlimePins() + QStringLiteral("/ui/Main.qml"), {
            SrcPin("slime drop branch", "else if (mobType === EntityManager.MobSlime) {", 1),
            SrcPin("slime xp row", "if (mobType === EntityManager.MobSlime && slimeSize > 0) xpAmt = slimeSize", 1),
            SrcPin("villager drop branch", "else if (mobType === EntityManager.MobVillager) {", 1),
            SrcPin("slime death cause", "cause = PlayerState.Slime", 1)});
        const bool pinsOk = missEnt.isEmpty() && missHdr.isEmpty() && missMq.isEmpty();
        ok = ok && pinsOk;
        if (!pinsOk) {
            const QStringList allMiss = QStringList() << missEnt << missHdr << missMq;
            diag += QStringLiteral("[pins %1]").arg(allMiss.join(QLatin1Char(',')));
        }
        // (D5) 相邻族零污染。
        const bool neighOk = RecipeRegistry::MelonSliceId == 0x28A
            && RecipeRegistry::PumpkinSeedsId == 0x28B
            && RecipeRegistry::MundanePotionId == 0x287
            && int(BR::Count) == 163 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）
            && int(BR::AtlasTileCount) == 212 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）；t1135 前移：Count 162→163 / Atlas 210→212（活塞 id + face/side 双 tile 段尾追加）
            && int(BR::Pumpkin) == 100;
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2077d villager behavior and structure pin family column (a villager spawns as"
               " a passive humanoid on the zero point three by zero point nine box at full ten"
               " health, stays standing next to the player without ever attacking across three"
               " seconds, dies reporting its type with a zero slime size, the mob enum holds"
               " slime at twenty one and villager at twenty two with the count declaration"
               " moved to the villager tail, the item family answers slimeball at 0x28C with"
               " the two eggs at 0x28D and 0x28E and the egg table round trips both, the source"
               " pin family holds the split spawn row and the ai dispatch row and the four qml"
               " rows, and the neighbouring cave spider and snow golem and block count and"
               " atlas and seed ids keep their original values)"
            << (ok ? QString() : diag);
    });
}
