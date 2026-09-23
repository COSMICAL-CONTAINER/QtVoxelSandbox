#include "matrix_helpers.h"

#include <QDir>
#include <QFile>

// t1081 刷怪笼 Spawner（id 40）条件刷怪 + 破坏语义探针段（4 腿；filter 词 r2053；矩阵 742→746）。置尾
// 先例（接 section53，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 48×48×96 s82，section11/53
// 同门；sparse 腿用 r2022 构造缝 World(SparseWorldParams) + loadChunkAt 按需物化，r2046/r2047 族同门）。
//
// 现场核实（按实况定范围，t1070/t1077/t1078/t1080 降级/盘点先例谱系第五现）：刷怪笼族**大半已交付**
//   ——① 方块面 = id 40（t392）hardness 5.0 pickaxe / dropId 0 / 多类型 state 编码（t786 六型）齐备；
//   ② 地牢族 = t392 placeDungeons 中央笼加权（t999）+ 矿井蛛笼（t1012③）+ 要塞银鱼笼（t786）全在；
//   ③ 渲染面 = t760 spawnerHost（cutout 笼壳 + 自旋迷你 mob delegate）齐备，本单零渲染路径改动；
//   ④ 条件刷怪 = tickSpawners 既有（t392 玩家距离窗 / t787 同型 cap / review#31 闸门分流 / review#30
//   水格谓词）齐备。**真缺口 = 三面**：(G1) 点亮暗门缺失——旧注释明载「刷怪笼无视光照（地牢天然黑暗）」
//   为已记录设计决策，t1081 dev-spec 明确要求「同既有敌对刷怪光照门」→ 同变更 lawful 修订（敌对型笼
//   刷出格 max(sky×skyBrightness, block) < 7；被动笼不受门）；(G2) 刷出位置采样域窄化——t392 初版只扫
//   「笼 8 水平邻 + y/y+1」，任务口径 = MC 1.0 ±4 水平邻域采样 → kSpawnOffsets 圆盘表（49 格）× y±1 +
//   spawnerScanSeed 确定性哈希轮转（MC 随机采样如实映射，PLAN §2-K 禁运行期随机源）；(G3) sparse 外环
//   扫描域钳制——旧扫描盒 max(0,·)/min(W-1,·) 把外环笼钳出扫描盒（t1073 病灶同族）→ sparse 分支无界化
//   （笼机制挂方块本体、与地牢生成路径解耦的头注释语义落地）。
//
// 破坏语义：镐可破 + 破笼即停（t392 既有，行为腿钉）；物品无掉落（dropId=0 = MC 口径）；**掉经验**
//   （MC 1.0 15-43，本工程经验族在——XpOrbManager t402 → Main.qml blockBroken 消费端如实实现，仅生存
//   门 + 恒发口径同 t571 破箱先例）。
//
// 腿面：r2053a 条件刷怪承重墙（节流 5+1s / 距离窗 17.5>16 / 昼夜光照门同 rig 双亮度 / 火把压停+撤火把
//   恢复 / 双 rig 采样确定性）；r2053b 刷出合法性 + 破坏承重墙（圆盘缘格采样承重 [旧窄域判别位] + 盘外
//   格拒采 + 支撑门保留 [无支撑候选跳过] + 破笼即停 + def 行破坏面逐字段）；r2053c 与地牢族解耦墙
//   （fixed 核心域手放笼工作 + sparse 负坐标外环笼工作 [旧钳制判别位，loadChunkAt 物化] + 机制零地牢
//   生成路径依赖反探）；r2053d 结构钉（def 行 / 常量族 / state 编码 / 签名 + 接线 + 采样表哈希源钉 /
//   QML XP 接线钉 + QML 装配零触碰反探 + 单一权威 tickSpawners 恰一处反探）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_t1081_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘点亮暗门本体（entitymanager.cpp tickSpawners 内
//     `if (spawnerEffLight >= kSpawnLightThreshold) continue;` 行注释）→ 声明红面 {r2053a}（昼夜光照门
//     + 火把压停断言红）。r2053b/c 不误伤（两腿全部亮度传 0.0 = 门恒过，摘门零行为差）；r2053d 不误伤
//     （钉面避开该行——NEG 靶行不入钉，t1080 先例）。
//   NEG-2 摘 sparse 扫描域门（entitymanager.cpp 扫描盒 isSparse 三元分支变异回固定核心盒钳制）→ 声明
//     红面 {r2053c}（外环笼被钳出扫描盒 → 零刷怪）。r2053a/b 不误伤（fixed 世界两分支逐位等价）；
//     r2053d 不误伤（三元行不入钉，同上）。
//
// rig 纪律：fresh 小世界 incantation（48×48×96 s82 恒晴）；暗室 rig = 9×9×5 石盒全填再雕（lessons
//   t769/t1030「不信必空」——rig 环境确定性等价理想环境，worldgen 噪声清零）；驱动 = em.tickSpawners
//   直调（直调不推进 m_tickPhase → 采样起点恒定 = 确定性钉面；生产 tick() 每帧推进 = 变奏面，头注释）；
//   签参亮度全腿显式传值（0.0 夜 / 1.0 昼，r2046c 显式参数同门）。

namespace {

// fresh fixed 小世界 incantation（section11/53 同款四 setter + 恒晴零 RNG）。
inline void initSpawnerWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// 地牢式暗室 rig：(sx±4, sy-2..sy+2, sz±4) 石盒全填（消除 worldgen 噪声——「不信必空」t769/t1030），
//   再居中放笼。石盒覆盖全部 49 格圆盘 × y∈{-1,0,+1} 候选域 + 支撑层，后续按需雕刻。
inline void carveCageChamber(World &w, int sx, int sy, int sz)
{
    for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -4; dz <= 4; ++dz)
            for (int dx = -4; dx <= 4; ++dx)
                w.setBlock(sx + dx, sy + dy, sz + dz, BR::Stone, 0);
    w.setBlock(sx, sy, sz, BR::Spawner, BlockRegistry::SpawnerStateShambler); // 居中放笼（地牢最常见型）
}

// 雕刻一个 dy=0 合法候选（here=Air / above=Air / support=Stone——陆生谓词全过 + 天然暗格）。
inline void carveCandidate(World &w, int x, int y, int z)
{
    w.setBlock(x, y - 1, z, BR::Stone, 0);
    w.setBlock(x, y, z, BR::Air, 0);
    w.setBlock(x, y + 1, z, BR::Air, 0);
}

} // namespace

void MatrixRun::section54_spawner()
{
    // 活体 mob 计数帮手（槽位复用 t256——按 aliveAt 计活体）。
    const auto aliveMobCount = [](EntityManager &em) -> int {
        int n = 0;
        for (int i = 0; i < em.count(); ++i)
            if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::Mob)) ++n;
        return n;
    };
    // 活体 mob 定位帮手（返首个活体下标；无 → -1）。
    const auto firstMob = [](EntityManager &em) -> int {
        for (int i = 0; i < em.count(); ++i)
            if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::Mob)) return i;
        return -1;
    };

    // ── r2053a：条件刷怪承重墙（节流 / 距离窗 / 光照门昼夜 + 火把 / 采样确定性）────────────
    runLeg("r2053a spawner conditional spawn wall (throttle 5+1s, distance window 17.5>16, light gate day/night "
           "same rig, torch suppression + resume, dual-rig deterministic sampling)", [&]() {
        bool ok = true;
        QString diag;

        // (1) 节流确定性：5×1.0s 累计 < kSpawnerInterval(6) → 0 刷；第 6 秒过界 → 首周期刷 1 Shambler。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, 24 + dx, 80, 24 + dz); // 3×3 环全雕（多候选 = 非平凡断言面）
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 37.0f); // XZ 12.5 ≤ 16 激活圈
            for (int i = 0; i < 5; ++i) em.tickSpawners(1.0, &w, playerPos, 0.0f);
            bool a1 = aliveMobCount(em) == 0;
            em.tickSpawners(1.0, &w, playerPos, 0.0f); // 累计过 6s 界 → 恰一周期
            a1 = a1 && aliveMobCount(em) == 1
                && em.mobTypeAt(firstMob(em)) == int(EntityManager::MobShambler);
            ok = ok && a1;
            if (!a1) diag += QStringLiteral("[a1 n=%1 ty=%2]")
                                .arg(aliveMobCount(em))
                                .arg(firstMob(em) >= 0 ? em.mobTypeAt(firstMob(em)) : -1);
        }

        // (2) 距离窗：玩家 XZ 17.5 > kSpawnerPlayerRange(16) → 三周期零刷（扫描盒 ±18 仍覆盖笼——
        //     笼被「找到」但被距离闸门拒绝，隔离的是激活窗本体非扫描面）。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, 24 + dx, 80, 24 + dz);
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 42.0f); // XZ 17.5 > 16
            for (int i = 0; i < 3; ++i) em.tickSpawners(6.0, &w, playerPos, 0.0f);
            const bool a2 = aliveMobCount(em) == 0;
            ok = ok && a2;
            if (!a2) diag += QStringLiteral("[a2 n=%1]").arg(aliveMobCount(em));
        }

        // (3) 昼夜光照门（同 rig 双亮度 = 门本体判别位）：地表笼（露天 sky=15）→ 昼（brightness=1.0）
        //     两周期零刷；同位置切夜（brightness=0.0）→ 一周期即刷。除亮度外 rig 全同。
        {
            World w;
            initSpawnerWorld(w);
            // 地表列扫描：露天（h+1/h+2 空气，无树冠投影）+ 草地。
            int rx = -1, rh = -1;
            for (int x = 18; x <= 34; ++x) {
                const int h = w.heightAt(x, 24);
                if (h < 2) continue;
                if (w.blockAt(x, h + 1, 24) == BR::Air && w.blockAt(x, h + 2, 24) == BR::Air) {
                    rx = x; rh = h;
                    break;
                }
            }
            bool a3 = rx > 0;
            if (a3) {
                w.setBlock(rx, rh + 1, 24, BR::Spawner, BlockRegistry::SpawnerStateShambler); // 笼坐地表
                EntityManager em;
                const QVector3D playerPos(float(rx) + 0.5f, float(rh + 1) + 0.5f, 24.5f); // 贴笼（XZ≈0）
                em.tickSpawners(6.0, &w, playerPos, 1.0f); // 昼：effLight=15 ≥ 7 → 门拒
                em.tickSpawners(6.0, &w, playerPos, 1.0f);
                a3 = aliveMobCount(em) == 0;
                em.tickSpawners(6.0, &w, playerPos, 0.0f); // 夜：effLight=0 < 7 → 过门
                a3 = a3 && aliveMobCount(em) == 1;
            }
            ok = ok && a3;
            if (!a3) diag += QStringLiteral("[a3 rx=%1]").arg(rx);
        }

        // (4) 方块光门（火把压停地牢笼 = MC 口径）：全石暗室只雕圆盘缘格 (sx+4)（旧窄域判别位）→
        //     缘格邻位火把（blockLight 13 ≥ 7）→ 零刷；撤火把（光回落 0）→ 恢复刷在缘格。
        //     三周期全夜亮度（0.0）：浮空 rig 缘格贴露天有天光侧渗，昼亮度会掩蔽「方块光门」的
        //     隔离面；夜亮度把门收窄到纯方块光 = 火把压停的隔离判别（sky 项被乘子清零）。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            carveCandidate(w, 28, 80, 24); // 唯一候选 = 圆盘缘 (sx+4, sy, sz)
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 24.5f); // 贴笼（XZ≈0 ≤ 16）
            w.setBlock(27, 80, 24, BR::Torch, 0);          // 缘格邻位火把（(sx+3) 光 14 → 缘格 13 ≥ 7）
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            bool a4 = aliveMobCount(em) == 0
                && w.blockLightAt(28, 80, 24) >= 7; // rig 自证：门读的方块光确实在（非「门误拒」假绿）
            const int lightDuring = w.blockLightAt(28, 80, 24);
            w.setBlock(27, 80, 24, BR::Air, 0);     // 撤火把
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            const QVector3D mp = em.posAt(firstMob(em));
            // XZ 恰落缘格中心；Y = 格底 + halfH（spawnMobCore 贴地口径，Shambler halfH=0.9 → 80.9）。
            a4 = a4 && w.blockLightAt(28, 80, 24) == 0 // 撤火把回零自证（rig 无残余光源）
                && aliveMobCount(em) == 1
                && mp.x() == 28.5f && mp.z() == 24.5f && mp.y() >= 80.0f && mp.y() < 81.0f;
            ok = ok && a4;
            if (!a4)
                diag += QStringLiteral("[a4 n=%1 ld=%2 la=%3 torchCell=%4/%5]")
                            .arg(aliveMobCount(em)).arg(lightDuring).arg(w.blockLightAt(28, 80, 24))
                            .arg(w.blockAt(27, 80, 24)).arg(w.blockLightAt(27, 80, 24));
        }

        // (5) 采样确定性（双 rig 同坐标同序列 → 恒同格）：两个 fresh 世界同 seed 同 rig（3×3 环 8 候选
        //     非平凡多候选面）→ 轮转起点同 → 刷出格逐位相同（m_tickPhase 直调恒 0 = 确定性钉面）。
        QVector3D posA, posB;
        bool a5 = false;
        {
            World wA;
            initSpawnerWorld(wA);
            carveCageChamber(wA, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(wA, 24 + dx, 80, 24 + dz);
            EntityManager emA;
            const QVector3D playerPos(24.5f, 80.5f, 37.0f);
            emA.tickSpawners(6.0, &wA, playerPos, 0.0f);
            if (firstMob(emA) >= 0) posA = emA.posAt(firstMob(emA));

            World wB;
            initSpawnerWorld(wB);
            carveCageChamber(wB, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(wB, 24 + dx, 80, 24 + dz);
            EntityManager emB;
            emB.tickSpawners(6.0, &wB, playerPos, 0.0f);
            if (firstMob(emB) >= 0) posB = emB.posAt(firstMob(emB));
            a5 = firstMob(emA) >= 0 && firstMob(emB) >= 0 && posA == posB;
        }
        ok = ok && a5;
        if (!a5) diag += QStringLiteral("[a5 %1 vs %2]").arg(QString::number(posA.x())).arg(QString::number(posB.x()));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2053a spawner conditional spawn wall (throttle 5+1s, distance window 17.5>16, light gate day/night "
               "same rig, torch suppression + resume, dual-rig deterministic sampling)"
            << (ok ? QString() : diag);
    });

    // ── r2053b：刷出合法性 + 破坏承重墙（缘格采样 / 盘外拒采 / 支撑门 / 破笼即停 / def 破坏面）──
    runLeg("r2053b spawner placement and break wall (disc rim sampling load-bearing, out-of-disc reject, support "
           "gate kept, break stops spawning, def row break face)", [&]() {
        bool ok = true;
        QString diag;

        // (1) 采样域承重：全石暗室只雕圆盘缘格 (sx+4)（XZ 距离恰 4 = 采样圆盘边界）→ 刷在缘格
        //     （旧「8 水平邻」窄域判别位：旧代码此 rig 零刷 = 本断言对旧实现红 = 承重）。同时雕
        //     盘外格 (sx+5)（XZ 距离 5 > 4 = 圆盘外）→ 必须零刷于该格（采样域上界钉）。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            carveCandidate(w, 28, 80, 24); // 盘缘格（dist 4.0，表内）
            carveCandidate(w, 29, 80, 24); // 盘外格（dist 5.0，表外——合法性全过也拒采）
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 24.5f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            int n29 = 0, n28 = 0;
            for (int i = 0; i < em.count(); ++i) {
                if (!em.aliveAt(i) || em.kindAt(i) != int(EntityManager::Mob)) continue;
                const QVector3D p = em.posAt(i);
                if (int(p.x()) == 29) ++n29;
                if (int(p.x()) == 28) ++n28;
            }
            const bool b1 = n29 == 0 && n28 == 1;
            ok = ok && b1;
            if (!b1) diag += QStringLiteral("[b1 n29=%1 n28=%2]").arg(n29).arg(n28);
        }

        // (2) 支撑门保留（域放宽不放宽合法性门）：雕 (sx+2) 双层空气但**无支撑**（底也掏空）+
        //     (sx+1) 常规候选（带石底）→ 轮转无论从谁起，(sx+2) 被支撑门拒 → 恒落 (sx+1)。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            carveCandidate(w, 25, 80, 24);       // (sx+1) 带支撑合法候选
            w.setBlock(26, 79, 24, BR::Air, 0);  // (sx+2) 掏支撑
            w.setBlock(26, 80, 24, BR::Air, 0);  // (sx+2) 双层空气（谓词 here/above 过、支撑门拒）
            w.setBlock(26, 81, 24, BR::Air, 0);
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 24.5f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            int n25 = 0, n26 = 0;
            for (int i = 0; i < em.count(); ++i) {
                if (!em.aliveAt(i) || em.kindAt(i) != int(EntityManager::Mob)) continue;
                const QVector3D p = em.posAt(i);
                if (int(p.x()) == 25) ++n25;
                if (int(p.x()) == 26) ++n26;
            }
            const bool b2 = n25 == 1 && n26 == 0;
            ok = ok && b2;
            if (!b2) diag += QStringLiteral("[b2 n25=%1 n26=%2]").arg(n25).arg(n26);
        }

        // (3) 破笼即停（spec「can be broken to stop」行为级）：首周期刷 1 → 破笼（setBlock Air）→
        //     两周期零新增（无 setBlock 钩子，扫描面 blockAt != Spawner 自然跳过——t392 语义保持）。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, 24 + dx, 80, 24 + dz);
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 37.0f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            const int first = aliveMobCount(em);
            w.setBlock(24, 80, 24, BR::Air, 0); // 破笼
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            em.tickSpawners(6.0, nullptr, playerPos, 0.0f); // world=null 早退语义顺手钉（防御面）
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            const bool b3 = first == 1 && aliveMobCount(em) == 1;
            ok = ok && b3;
            if (!b3) diag += QStringLiteral("[b3 first=%1 after=%2]").arg(first).arg(aliveMobCount(em));
        }

        // (4) 破坏面 def 行逐字段（Core 单一权威表）：镐可破（toolType=Pickaxe + minToolTier=1 +
        //     requiresTool）+ 物品零掉落（dropId=0 = MC 口径：破坏刷怪笼不掉自身）+ hardness 5.0。
        const auto &sd = BR::def(BR::Spawner);
        const bool b4 = sd.id == int(BR::Spawner) && sd.id == 40 && !sd.solid && sd.shape == BR::ShapeFull
            && sd.hardness == 5.0f && sd.toolType == int(BR::Pickaxe) && sd.requiresTool
            && sd.minToolTier == 1 && sd.dropId == 0 && sd.dropCount == 0 && sd.maxStack == 64
            && sd.topTile == 51 && sd.sideTile == 51 && sd.bottomTile == 51
            && QLatin1String(sd.name) == QLatin1String("spawner")
            && QLatin1String(sd.display) == QLatin1String("刷怪笼")
            && BR::mcBlockId(quint8(BR::Spawner)) == 52; // MC 1.0 mob spawner id 52（kMcBlockId 单一权威表）
        ok = ok && b4;
        if (!b4) diag += QStringLiteral("[b4 id=%1 drop=%2]").arg(sd.id).arg(sd.dropId);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2053b spawner placement and break wall (disc rim sampling load-bearing, out-of-disc reject, support "
               "gate kept, break stops spawning, def row break face)"
            << (ok ? QString() : diag);
    });

    // ── r2053c：与地牢族解耦墙（fixed 核心域手放笼 + sparse 外环笼 + 零地牢路径反探）────────
    runLeg("r2053c spawner decoupled from dungeon family wall (fixed core manual cage works, sparse negative outer "
           "ring cage works [old clamp discrimination point], zero dungeon-path dependency reverse probe)", [&]() {
        bool ok = true;
        QString diag;

        // (1) fixed 核心域：手放笼（fresh 世界零结构生成依赖——机制只读 blockAt）+ 暗室环雕 → 刷 1。
        {
            World w;
            initSpawnerWorld(w);
            carveCageChamber(w, 24, 80, 24);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, 24 + dx, 80, 24 + dz);
            EntityManager em;
            const QVector3D playerPos(24.5f, 80.5f, 37.0f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            const bool c1 = aliveMobCount(em) == 1
                && em.mobTypeAt(firstMob(em)) == int(EntityManager::MobShambler);
            ok = ok && c1;
            if (!c1) diag += QStringLiteral("[c1 n=%1]").arg(aliveMobCount(em));
        }

        // (2) sparse 负坐标外环（t1073 病灶同族判别位——旧钳制把外环笼钳出扫描盒 = 零刷怪）：sparse
        //     构造缝 + loadChunkAt(-1,-1) 物化 + 负坐标暗室手放笼 + 玩家在笼旁 → 刷 1 且落负坐标域。
        //     机制等价 MC：笼机制挂方块本体（blockAt 扫描面），方块到哪机制到哪（外环/地牢/手放同一面）。
        {
            World w{ { 82, 48, 48, 96, 0 } }; // SparseWorldParams：核心 48×48 h96 s82，零预生成
            bool c2 = w.isSparse();
            c2 = c2 && w.loadChunkAt(-1, -1);          // 按需物化负坐标 chunk（r2047 负侧先例同门）
            carveCageChamber(w, -8, 80, -8);            // 负坐标浮空暗室 + 手放笼（chunk (-1,-1) 内，y=80 零 worldgen 接触）
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, -8 + dx, 80, -8 + dz);
            EntityManager em;
            const QVector3D playerPos(-8.5f, 80.5f, 4.5f); // 笼旁（XZ 12.5 ≤ 16）
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            em.tickSpawners(6.0, &w, playerPos, 0.0f);
            int negMobs = 0;
            for (int i = 0; i < em.count(); ++i)
                if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::Mob) && em.posAt(i).x() < 0) ++negMobs;
            const bool caged = w.blockAt(-8, 80, -8) == BR::Spawner; // rig 自证：笼确实在负坐标格
            const bool chunkOk = w.loadChunkAt(-1, -1); // 幂等重查（物化失败诊断面）
            // 对照组：同世界正坐标笼（chunk (0,0)）——分离「sparse 扫描面」（对照组 = 扫描+候选全链
            //     在 sparse 域工作）与「负坐标候选/哨兵面」（本腿主断言）。
            w.loadChunkAt(0, 0);
            carveCageChamber(w, 4, 80, 4);
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dz) carveCandidate(w, 4 + dx, 80, 4 + dz);
            em.tickSpawners(6.0, &w, QVector3D(4.5f, 80.5f, 4.5f), 0.0f);
            em.tickSpawners(6.0, &w, QVector3D(4.5f, 80.5f, 4.5f), 0.0f);
            int posMobs = 0, boxCages = 0;
            for (int i = 0; i < em.count(); ++i)
                if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::Mob) && em.posAt(i).x() > 0) ++posMobs;
            for (int yy = 62; yy <= 95; ++yy)
                for (int zz = -14; zz <= 22; ++zz)
                    for (int xx = -27; xx <= 22; ++xx)
                        if (w.blockAt(xx, yy, zz) == BR::Spawner) ++boxCages; // 手工扫盒（读面自证）
            c2 = c2 && negMobs >= 1 && posMobs >= 1 && caged;
            ok = ok && c2;
            if (!c2)
                diag += QStringLiteral("[c2 sparse=%1 neg=%2 caged=%3 chunk=%4 total=%5 pos=%6 cages=%7 ring=%8/%9 light=%10]")
                            .arg(w.isSparse()).arg(negMobs).arg(caged).arg(chunkOk)
                            .arg(aliveMobCount(em)).arg(posMobs).arg(boxCages)
                            .arg(w.blockAt(-9, 80, -9)).arg(w.blockAt(-9, 81, -9))
                            .arg(w.blockLightAt(-9, 80, -9));
        }

        // (3) 机制零地牢生成路径依赖（结构反探）：entitymanager.cpp 剥行注释后不引用 placeDungeons /
        //     地牢 piece 族任何标识符（笼机制唯一输入 = blockAt 扫描 + state 解码，无结构注册表查询面；
        //     注释里的机制记载不算——剥注释口径与 pinSet 同门）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile entFile(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
        const QString entRaw = entFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(entFile.readAll()) : QString();
        QString entCode; // 剥 // 行注释（token 探针足够；同 pinSet 行注释语义）
        const auto lines = entRaw.split(QLatin1Char(u'\n'));
        for (const QString &ln : lines)
            entCode += ln.section(QLatin1String("//"), 0, 0);
        const bool c3 = !entCode.isEmpty()
            && !entCode.contains(QLatin1String("placeDungeons"))
            && !entCode.contains(QLatin1String("pieceSpiderRoom"))
            && !entCode.contains(QLatin1String("pieceCorridor"));
        ok = ok && c3;
        if (!c3) diag += QStringLiteral("[c3] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2053c spawner decoupled from dungeon family wall (fixed core manual cage works, sparse negative outer "
               "ring cage works [old clamp discrimination point], zero dungeon-path dependency reverse probe)"
            << (ok ? QString() : diag);
    });

    // ── r2053d：结构钉（def/常量/state 编码/签名接线/采样表哈希/QML XP 线/反探族）──────────
    runLeg("r2053d spawner structure pins (def row, constants family, state encoding, signature and wiring, offset "
           "table and hash source pins, QML xp wiring, single-authority and zero-touch reverse probes)", [&]() {
        bool ok = true;
        QString diag;

        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));

        // (1) 常量族源钉（EntityManager 常量为 private → 值钉走 .h 源文本，r2041d 数值源钉先例）。
        const QStringList missH = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("interval", "kSpawnerInterval      = 6.0f", 1),
            SrcPin("range", "kSpawnerPlayerRange   = 16.0f", 1),
            SrcPin("scan", "kSpawnerScanRange      = 18", 1),
            SrcPin("sample", "kSpawnerSampleRadius   = 4;", 1),
            SrcPin("check", "kSpawnerMobCheckRadius = 4.5f", 1),
            SrcPin("cap", "kSpawnerLocalCap       = 4;", 1),
            SrcPin("light", "kSpawnLightThreshold  = 7.0f", 1)});

        // (2) state 编码单一权威（t786 契约保持 + 解码往返）。
        const bool d2 = BlockRegistry::SpawnerStateShambler == 0x08
            && BlockRegistry::SpawnerStateBones == 0x0A
            && BlockRegistry::SpawnerStateCaveSpider == 0x28
            && BlockRegistry::spawnerStateForMob(int(EntityManager::MobSpider)) == 0x0E
            && EntityManager{}.spawnerMobTypeForState(int(BlockRegistry::SpawnerStateShambler))
                == int(EntityManager::MobShambler);
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2] ");

        // (3) 源钉族（pinSet 剥注释口径；NEG 靶行[点亮暗门行 / sparse 三元行]刻意不入钉——
        //     NEG-1/NEG-2 红面分离判据，t1080 先例）。
        const QStringList missEnt = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"), {
            SrcPin("def+call", "void EntityManager::tickSpawners", 1),
            SrcPin("offset table", "kSpawnOffsets", 2),
            SrcPin("seed fn", "spawnerScanSeed", 2),
            SrcPin("domain gate", "columnInPlayableDomain(world, cx, cz)", 1)});
        const QStringList missEntH = pinSet(srcRoot + QStringLiteral("/Entities/entitymanager.h"), {
            SrcPin("signature", "void tickSpawners(qreal dt, World *world, const QVector3D &playerPos, float skyBrightness);", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("tick wiring", "tickSpawners(dt, m_world, m_pos, m_worldClock ? float(m_worldClock->skyLight()) : 0.0f);", 1)});
        const QStringList missQml = pinSet(srcRoot + QStringLiteral("/ui/Main.qml"), {
            SrcPin("xp wiring", "xpOrbs.spawnOrb(x, y, z, 15 + Math.floor(Math.random() * 29))", 1),
            SrcPin("survival gate", "if (id === 40 && player.mode === PlayerController.Survival)", 1)});
        const QStringList allMiss = missH + missEnt + missEntH + missPc + missQml;
        ok = ok && allMiss.isEmpty();
        if (!allMiss.isEmpty()) diag += QStringLiteral("[pins %1] ").arg(allMiss.join(u','));

        // (4) 反探族：① tickSpawners 定义恰一处（单一权威——第二份刷怪模拟即红）；② QML 装配零触碰
        //     （JS 调用形态 entityManager.tickSpawners( 在 Main.qml 零出现——机制全 C++ 侧）；
        //     ③ 渲染面零改动（spawnerHost 重建行原样在场 = t760 delegate 链未被本单触碰）。
        QFile entFile2(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
        const QString entTxt2 = entFile2.open(QIODevice::ReadOnly) ? QString::fromUtf8(entFile2.readAll()) : QString();
        const int defCount = entTxt2.count(QLatin1String("void EntityManager::tickSpawners"));
        QFile mainFile(srcRoot + QStringLiteral("/ui/Main.qml"));
        const QString mainTxt = mainFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(mainFile.readAll()) : QString();
        const bool d4 = defCount == 1
            && !mainTxt.contains(QLatin1String("entityManager.tickSpawners("))
            && mainTxt.contains(QLatin1String("spawnerHost.addSpawnerVis(scells[si], scells[si + 1], scells[si + 2])"))
            && mainTxt.contains(QLatin1String("if (id === 40) spawnerHost.removeSpawnerVis(x, y, z)"));
        ok = ok && d4;
        if (!d4) diag += QStringLiteral("[d4 defs=%1] ").arg(defCount);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2053d spawner structure pins (def row, constants family, state encoding, signature and wiring, offset "
               "table and hash source pins, QML xp wiring, single-authority and zero-touch reverse probes)"
            << (ok ? QString() : diag);
    });
}
