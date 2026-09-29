#include "matrix_helpers.h"

// t1108 村庄 worldgen 探针段(4 腿;filter 词 r2078;矩阵 848→852)。置尾先例沿用(接 section70,
//   runAll 末执行,rig 世界零接触——各腿自建 fresh 固定小世界,World 直驱 + 源钉族)。
//
// ── 现状核实裁定表(派工面五件逐一全仓 grep 实读)──
//   ① 村庄生成器:worldgen 结构生成器族盘点 = placeDungeons / placeMineshaft / placeDesertTemple /
//     placeJungleTemple / placeStronghold,无村庄 → 真缺(t1107 已现场核实,本单复核一致)。交付 =
//     placeVillages(固定 generate() 内 placeStronghold 之后;机制等价 MC Beta 1.8/1.0 村庄)。
//   ② 生成时机核实:本工程无「逐 chunk population」结构生成——结构族在 fixed generate() 一次性
//     全域落位,sparse 流式世界经 sparsePopulateChunk 处置表**全部 (c) 豁免不重放**(村庄=第六员
//     同门登记)。派工单「chunk population 点位」预设不成立 → 以现场为准:placeVillages 调用点 =
//     generate() 内 placeStronghold 之后(神殿同门时序),sparse 豁免如实登记。
//   ③ 桥面选型(技术难点核实:World 层无 Entity 依赖,PLAN §2):spawn 请求快照面(take 语义队列)
//     胜出——①信号面在 generate 栈内发、消费方(EntityManager 归 ui 层)未必就绪且读档路径需二发;
//     ②take 队列(EditBuffer「登记→收口消费」同门)World 只持纯值三元组,generate/finishLoad 两
//     路径同缝 refill,take 恒空幂等 = 会话内恰一次 = 村民重复生成防护面(村庄已生成标记的实体侧
//     对偶:方块侧标记 = 结构性 generate-once/存档 blob;实体侧 = take-once 队列)。桥面 = World
//     纯值请求 [x,y,z]×N,takeVillageSpawnRequests() 由 Main.qml enterWorld 消费(splashBottleBreak
//     同门:ui 层路由),村民语义归消费端。
//   ④ 模板集(1.0 口径实读裁定,单轮容量分层):水井(5×5 圆石台 + 中芯水柱深 4 + 圆石底;**开顶
//     无棚**——带棚井为 1.2+ 形态不取)+ 小屋×2..4(5×5 木板地/墙两层/平顶 + 朝井门洞 + 室内火把
//     ——1.0 原型实有木板/圆木/火把,door 块免用[门=1×2 空气门洞,1.0 小屋木门形态简化登记])+ 农田
//     (7×7 中行水道 + 湿耕地 + 小麦,t406/t445 耕地族复用)+ 道路(十字四臂**砂砾**——1.0 村庄道路=
//     gravel,grass path 1.9+ 越纪元不取;Gravel=139 在场核实)。沙漠砂岩变体:1.0 实有(Beta 1.8 起
//     村庄平原+沙漠双群系)但单轮超容 → 候选池登记,平原先行(派工明示的分层合法路径)。
//   ⑤ 村民数量口径:1.0 村民随村庄生成、无繁殖(1.0.0 无 breeding)→ 人口≈房数的工程化裁定 =
//     每小屋恰一名(非 1.0 实读定值——1.0 村庄人口未在 wiki 表格式定值,如实登记为工程裁定);
//     持久化语义裁定:方块走 chunk 存量自然持久(generate-once + blob round-trip);村民实体本身不
//     进存档(t1013 口径)→ **每次进世界重 population 一次**(generate/finishLoad 末 refill 重推导,
//     B5「重推导优于序列化」同门),会话内村民死亡不补(take-once 队列不再产请求;严格 1.0 的「村民
//     永久死亡」语义在实体不存档的工程下不可表达,如实登记)。拆井 / 小屋被切毁后:井锚 + 地板完整
//     门失败 → 不重生村民(井毁村空 / 毁屋不住,如实)。
//   ⑥ carve 风险面(实测留痕):carveCanyon 先于村后运行且无结构守卫(神殿同门既登记风险面)——
//     seed 207 实测村被峡谷切毁(井/水道/路/小屋 1 → Air)。本段探针按 section06 intactT20 同门
//     **扫完好站点**(seed 表 × 站点逐个过完好面检查,首个全绿站点为钉定站),生产行为不做峡谷豁免
//     (与神殿同口径如实保留;refill 侧的地板/井锚门保证被切村不出「幽灵村民」)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 摘小屋墙体写入行(placeVillages B) 步 `if (edge)` 改 `if (false)`,编译仍绿)→ 恰红 =
//     {r2078c}(完好面检查含槽位 0 墙体断言 → 探针所有站点过不了完好门 → [probe] fail-visible,
//     即模板面断言失;地板/屋顶/门洞/室内/火把照写 = 非 NEG-1 敏感面全幸存)。a 腿(站点表纯算术,
//     不触落块面)、b 腿(桥面判据 = 井锚+地板+air 门,墙体不在判据内)、d 腿(源钉族**不钉墙体行**
//     = t1107 豁免设计同门)均不误伤。
//   NEG-2 = 摘 spawn 桥(refillVillageSpawnRequests 函数体首行后插 `if (true) return;`,编译仍绿)
//     → 恰红 = {r2078b}(pending 恒 0 / take 恒空 = 桥面恒空断言全失)。a 腿(区域表,不触队列)、
//     c 腿(方块面,不触队列)、d 腿(钉 refill **调用行** ×2——generate/finishLoad 尾,均在本函数体
//     外,摘面不扰钉)均不误伤。
namespace {

constexpr int kW = 80, kD = 80, kH = 96;

inline void initVillageWorld(World &w, int seed)
{
    w.setWidth(kW);
    w.setDepth(kD);
    w.setHeight(kH);
    w.setSeed(seed); // 末位 setter = 全尺寸 generate(前三 setter 在小尺寸上各跑一次,末次为准)
}

// 完好站点面检查(探针选择判据,与 r2078c 断言同门同源):井(台面四沿中圆石 + 水柱 S..S-3 +
//   井底圆石)+ 槽 0 小屋(地板/三面墙中格/门洞/室内/平顶/火把)+ 农田中心水道/湿耕地 + 道路臂样格
//   (道路随地形跟随面 → 带域 S-4..S+3 内扫砂砾)。
inline bool villageSiteIntact(const World &w, int cx, int cz, int S)
{
    const int h0x = cx + World::kVillageHutSlots[0][0];
    const int h0z = cz + World::kVillageHutSlots[0][1];
    const int dirX = World::kVillageHutSlots[0][0] > 0 ? -1 : 1; // 槽 0 门洞向心侧
    // 道路列跟随面判定:带域内恰一砂砾(putRoad 每列只铺一格)。
    const auto roadAt = [&w](int px, int pz, int sy) {
        for (int yy = sy + 3; yy >= sy - 4; --yy)
            if (w.blockAt(px, yy, pz) == BR::Gravel)
                return true;
        return false;
    };
    if (w.blockAt(cx, S, cz) != BR::Water)
        return false; // 井锚(水柱顶)
    for (const int d : { -2, 2 })
        if (w.blockAt(cx + d, S, cz) != BR::Cobble || w.blockAt(cx, S, cz + d) != BR::Cobble)
            return false;
    for (int dy = 1; dy <= 3; ++dy)
        if (w.blockAt(cx, S - dy, cz) != BR::Water)
            return false;
    if (w.blockAt(cx, S - 4, cz) != BR::Cobble)
        return false;
    if (w.blockAt(h0x, S, h0z) != BR::Planks)
        return false;
    for (int dy = 1; dy <= 2; ++dy)
        if (w.blockAt(h0x + dirX * 2, S + dy, h0z) != BR::Air)
            return false; // 门洞向心侧两面 air（探针面**不含墙体 Planks 断言**——NEG-1 摘面与探针
                          //   解耦：墙行被摘时探针不受扰，恰红收敛单腿 r2078c）
    if (w.blockAt(h0x, S + 3, h0z) != BR::Planks
        || w.blockAt(h0x, S + 1, h0z + 1) != BR::Torch)
        return false;
    if (w.blockAt(cx + 11, S, cz) != BR::Water
        || w.blockAt(cx + 11, S, cz + 1) != BR::Farmland
        || !roadAt(cx + 3, cz, S) || !roadAt(cx, cz + 3, S))
        return false;
    return true;
}

// 确定性探针(seed 表 × 站点逐个过完好门 → 首个完好站点;静态一次,四腿共用;intactT20 同门)。
struct VillageProbe
{
    int seed = -1;   // 命中 seed(-1 = 表内无完好村庄 = 腿内 fail-visible)
    int sites = 0;   // 该 seed 的村庄站点总数(区域表全量,含被切站点——区域表 = 纯算术投影)
    int intact = -1; // 完好站点序号
    int cx = 0, cz = 0, y = 0; // 完好站点中心与站心地表格
};

inline VillageProbe villageProbe()
{
    static const VillageProbe p = [] {
        static const int kSeeds[] = { 82, 166, 207, 42, 1337, 7, 99, 1234, 2026, 555,
            31, 77, 314, 2718, 161, 902 };
        for (const int seed : kSeeds) {
            World w;
            initVillageWorld(w, seed);
            const int n = w.structureRegionCount(World::StructureVillage);
            if (n < 1)
                continue;
            for (int i = 0; i < n; ++i) {
                const QVariantList r = w.structureRegion(World::StructureVillage, i);
                if (r.size() != 6)
                    continue;
                const int cx = (r[0].toInt() + r[3].toInt()) / 2; // minX..maxX 中点(布局对站心对称)
                const int cz = (r[2].toInt() + r[5].toInt()) / 2;
                const int y = r[4].toInt() - 4;                   // maxY = y + kVillageHutRoofY + 1
                if (!villageSiteIntact(w, cx, cz, y))
                    continue;
                VillageProbe out;
                out.seed = seed;
                out.sites = n;
                out.intact = i;
                out.cx = cx;
                out.cz = cz;
                out.y = y;
                return out;
            }
        }
        return VillageProbe{};
    }();
    return p;
}

// 源钉根路径(section70 同门:applicationDirPath/../src)。
inline QString srcRootForVillagePins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 全活体村民计数(桥面落债面断言用)。
inline int countVillagers(const EntityManager &ents)
{
    int n = 0;
    for (int i = 0; i < ents.count(); ++i)
        if (ents.aliveAt(i) && ents.mobTypeAt(i) == EntityManager::MobVillager)
            ++n;
    return n;
}

} // namespace

void MatrixRun::section71_village_worldgen()
{
    // ── r2078a:站点表 + 区域投影确定性柱(双 NEG 均不触达 = 对照腿)─────────────────────────
    //   同 seed 双世界站点数/足迹逐项恒等(纯函数承重)+ 探针一致性 + bbox 口径(29×29 / y-6..y+4)
    //   + 区域读取防御面(越界 kind/index)。
    runLeg("r2078a village site table and region projection determinism column (two same seed"
        " worlds answer identical village site counts and identical footprints region by region,"
        " the shared probe center and surface row agree with the first site bounding box, the"
        " bounding box holds the twenty nine by twenty nine horizontal footprint and the well"
        " bottom to hut roof vertical band, and the region read face defends out of range kind"
        " and index)", [&]() {
        bool ok = true;
        QString diag;
        const VillageProbe pr = villageProbe();
        ok = ok && pr.seed >= 0 && pr.sites >= 1;
        if (pr.seed < 0) diag += QStringLiteral("[probe]");

        World w1;
        initVillageWorld(w1, pr.seed);
        World w2;
        initVillageWorld(w2, pr.seed);

        const int n1 = w1.structureRegionCount(World::StructureVillage);
        const int n2 = w2.structureRegionCount(World::StructureVillage);
        const bool twinOk = n1 == n2 && n1 == pr.sites && n1 >= 1;
        ok = ok && twinOk;
        if (!twinOk) diag += QStringLiteral("[twin n1=%1 n2=%2 sites=%3]")
            .arg(n1).arg(n2).arg(pr.sites);

        bool regionsEq = true;
        for (int i = 0; i < n1 && regionsEq; ++i)
            regionsEq = w1.structureRegion(World::StructureVillage, i)
                == w2.structureRegion(World::StructureVillage, i);
        ok = ok && regionsEq;
        if (!regionsEq) diag += QStringLiteral("[regions]");

        // 探针一致性 + bbox 口径(完好站点)。
        const QVariantList r0 = w1.structureRegion(World::StructureVillage, pr.intact);
        const bool bboxOk = r0.size() == 6
            && (r0[0].toInt() + r0[3].toInt()) / 2 == pr.cx
            && (r0[2].toInt() + r0[5].toInt()) / 2 == pr.cz
            && r0[4].toInt() - 4 == pr.y
            && r0[3].toInt() - r0[0].toInt() == 2 * int(World::kVillageHalf)
            && r0[5].toInt() - r0[2].toInt() == 2 * int(World::kVillageHalf)
            && r0[4].toInt() - r0[1].toInt() == 10; // (y+4)-(y-6) = 10(井底到小屋顶竖带)
        ok = ok && bboxOk;
        if (!bboxOk) diag += QStringLiteral("[bbox %1]").arg(r0.size());

        // 防御面:越界 kind / index。
        const bool defOk = w1.structureRegionCount(-1) == -1
            && w1.structureRegionCount(World::StructureKindCount) == -1
            && w1.structureRegion(World::StructureVillage, -1).isEmpty()
            && w1.structureRegion(World::StructureVillage, n1).isEmpty();
        ok = ok && defOk;
        if (!defOk) diag += QStringLiteral("[def]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2078a village site table and region projection determinism column (two same"
               " seed worlds answer identical village site counts and identical footprints"
               " region by region, the shared probe center and surface row agree with the first"
               " site bounding box, the bounding box holds the twenty nine by twenty nine"
               " horizontal footprint and the well bottom to hut roof vertical band, and the"
               " region read face defends out of range kind and index)"
            << (ok ? QString() : diag);
    });

    // ── r2078b:spawn 桥契约柱(NEG-2 敏感面)─────────────────────────────────────────────────
    //   refill 判据(井锚 + 地板完整 + 内格 air)的体素同构普查 → pending 恰额 + take 平铺恰额 +
    //   点集成员(小屋内格中心)+ 二次 take 零重复(防护面)+ 村民实数落债(spawn 恰额 + 1s 全活)。
    runLeg("r2078b village spawn bridge contract column (the refilled request count equals the"
        " voxel census of intact huts judged by the same well anchor floor and interior air"
        " gates across all village sites, taking the queue yields exactly that many x y z"
        " triples matching the hut interior centers, a second take answers empty with zero"
        " pending as the once per session guard, and spawning a wandering villager per request"
        " lands exactly that many alive MobVillager entities that all survive one second of"
        " simulation)", [&]() {
        bool ok = true;
        QString diag;
        const VillageProbe pr = villageProbe();
        ok = ok && pr.seed >= 0;
        if (pr.seed < 0) diag += QStringLiteral("[probe]");

        World w;
        initVillageWorld(w, pr.seed);

        // 体素普查(refill 三门同构):全站点 × 全 4 槽,井锚在场 + 地板 Planks + 内格双 Air → 计入。
        int census = 0;
        QSet<qint64> expect; // (x<<40)|(y<<20)|z 打包的期望点集
        const int nSites = w.structureRegionCount(World::StructureVillage);
        for (int si = 0; si < nSites; ++si) {
            const QVariantList r = w.structureRegion(World::StructureVillage, si);
            const int scx = (r[0].toInt() + r[3].toInt()) / 2;
            const int scz = (r[2].toInt() + r[5].toInt()) / 2;
            const int sy = r[4].toInt() - 4;
            if (w.blockAt(scx, sy, scz) != BR::Water)
                continue; // 井锚缺席(被切 / 拆井)→ 全站不出请求(refill 同门)
            for (int i = 0; i < 4; ++i) {
                const int hx = scx + World::kVillageHutSlots[i][0];
                const int hz = scz + World::kVillageHutSlots[i][1];
                if (w.blockAt(hx, sy, hz) != BR::Planks)
                    continue; // 地板门(未建 / 被切毁)
                if (w.blockAt(hx, sy + 1, hz) != BR::Air
                    || w.blockAt(hx, sy + 2, hz) != BR::Air)
                    continue; // 内格 air 门(被埋 / 被改建)
                ++census;
                expect.insert((qint64(hx) << 40) | (qint64(sy + 1) << 20) | qint64(hz));
            }
        }
        ok = ok && census >= 2; // 完好站点 ≥2 小屋(villageHutCount 下界)
        if (census < 2) diag += QStringLiteral("[census %1]").arg(census);

        // refill 恰额(pending 对账)。
        const int pend = w.villageSpawnPending();
        const bool pendOk = pend == census;
        ok = ok && pendOk;
        if (!pendOk) diag += QStringLiteral("[pend %1 census %2]").arg(pend).arg(census);

        // take 恰额 + 点集成员(小屋内格中心)。
        const QVariantList taken = w.takeVillageSpawnRequests();
        const bool takeOk = taken.size() == census * 3;
        ok = ok && takeOk;
        if (!takeOk) diag += QStringLiteral("[take %1 want %2]").arg(taken.size()).arg(census * 3);
        bool membersOk = takeOk;
        for (int i = 0; i + 2 < taken.size(); i += 3) {
            const qint64 key = (qint64(taken[i].toInt()) << 40)
                | (qint64(taken[i + 1].toInt()) << 20) | qint64(taken[i + 2].toInt());
            if (!expect.contains(key)) {
                membersOk = false;
                break;
            }
        }
        ok = ok && membersOk;
        if (!membersOk) diag += QStringLiteral("[members]");

        // 二次 take 零重复(会话内恰一次防护面)。
        const bool onceOk = w.takeVillageSpawnRequests().isEmpty()
            && w.villageSpawnPending() == 0;
        ok = ok && onceOk;
        if (!onceOk) diag += QStringLiteral("[once]");

        // 村民实数落债:每请求 spawn 一名 → 恰额 + 1s 全活(被动游荡,无伤害源)。
        {
            EntityManager ents;
            for (int i = 0; i + 2 < taken.size(); i += 3)
                ents.spawnMobTyped(taken[i].toInt(), taken[i + 1].toInt(), taken[i + 2].toInt(),
                                   EntityManager::MobVillager, QStringLiteral("#8a6a4a"), 10);
            const int spawned = countVillagers(ents);
            ok = ok && spawned == census;
            if (spawned != census)
                diag += QStringLiteral("[spawn %1 want %2]").arg(spawned).arg(census);
            for (int i = 0; i < 20; ++i) // 1.0s
                ents.tick(0.05, &w, QVector3D(float(kW + 10), 90.0f, float(kD + 10)),
                          0.3f, 1.8f, false); // 玩家远置(不可及)→ 纯游荡
            const int alive = countVillagers(ents);
            const bool surviveOk = alive == census;
            ok = ok && surviveOk;
            if (!surviveOk)
                diag += QStringLiteral("[survive %1 want %2]").arg(alive).arg(census);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2078b village spawn bridge contract column (the refilled request count equals"
               " the voxel census of intact huts judged by the same well anchor floor and"
               " interior air gates across all village sites, taking the queue yields exactly"
               " that many x y z triples matching the hut interior centers, a second take"
               " answers empty with zero pending as the once per session guard, and spawning a"
               " wandering villager per request lands exactly that many alive MobVillager"
               " entities that all survive one second of simulation)"
            << (ok ? QString() : diag);
    });

    // ── r2078c:模板方块面柱(NEG-1 敏感面)──────────────────────────────────────────────────
    //   完好站点逐构件坐标定格:井(台面四沿中圆石 + 中芯水柱 S..S-3 + 井底圆石)/小屋槽 0(地板/
    //   三面墙中格 + 门洞/室内/平顶/火把)+ 小屋前缀普查界[2,4] + 槽位后继负例 + 农田(水道/湿耕地
    //   state=3/小麦阶段=hashVoxel 公式回算)+ 道路四臂砂砾 + 道路守卫负例(农田水道格不被覆盖)。
    runLeg("r2078c village template block face column (the well answers a cobble platform ring"
        " with a water shaft three deep over a cobble bottom, the first hut answers a plank"
        " floor with three plank walls a center facing doorway an air interior a flat plank"
        " roof and an interior torch, the hut census is a prefix of two to four slots with the"
        " next slot hut free, the farm answers a water channel row with wet farmland state"
        " three and wheat crops whose stage equals the deterministic voxel hash, the four road"
        " arms answer gravel, and the road guard keeps the farm channel water unoverwritten)",
        [&]() {
        bool ok = true;
        QString diag;
        const VillageProbe pr = villageProbe();
        ok = ok && pr.seed >= 0 && pr.intact >= 0;
        if (pr.seed < 0) diag += QStringLiteral("[probe]");

        World w;
        initVillageWorld(w, pr.seed);
        const int cx = pr.cx, cz = pr.cz, S = pr.y;

        // (C1) 水井:台面四沿中圆石;中芯水柱 S..S-3 全 Water;井底 S-4 圆石。
        bool wellOk = true;
        for (const int d : { -2, 2 }) {
            wellOk = wellOk
                && w.blockAt(cx + d, S, cz) == BR::Cobble
                && w.blockAt(cx, S, cz + d) == BR::Cobble;
        }
        for (int dy = 0; dy < 4; ++dy)
            wellOk = wellOk && w.blockAt(cx, S - dy, cz) == BR::Water;
        wellOk = wellOk && w.blockAt(cx, S - 4, cz) == BR::Cobble;
        ok = ok && wellOk;
        if (!wellOk) diag += QStringLiteral("[well]");

        // (C2) 小屋槽 0(-8,-8):地板 Planks;-x/-z/+z 三面墙中格 S+1/S+2 Planks;向心 +x 墙中格
        //      = 门洞 1×2 Air;室内中心上下 Air;平顶 S+3 中心 Planks;室内火把(中心 +z)Torch。
        const int h0x = cx + World::kVillageHutSlots[0][0];
        const int h0z = cz + World::kVillageHutSlots[0][1];
        const int dirX = World::kVillageHutSlots[0][0] > 0 ? -1 : 1; // 槽 0 x=-8 → 门朝 +x(向心)
        bool hutOk = w.blockAt(h0x, S, h0z) == BR::Planks;
        for (int dy = 1; dy <= 2; ++dy) {
            hutOk = hutOk
                && w.blockAt(h0x - 2, S + dy, h0z) == BR::Planks  // -x 墙中格(背心侧)
                && w.blockAt(h0x, S + dy, h0z - 2) == BR::Planks  // -z 墙中格
                && w.blockAt(h0x, S + dy, h0z + 2) == BR::Planks  // +z 墙中格
                && w.blockAt(h0x + dirX * 2, S + dy, h0z) == BR::Air; // +x 墙中格 = 门洞(朝站心)
        }
        hutOk = hutOk
            && w.blockAt(h0x, S + 1, h0z) == BR::Air
            && w.blockAt(h0x, S + 2, h0z) == BR::Air
            && w.blockAt(h0x, S + 3, h0z) == BR::Planks
            && w.blockAt(h0x, S + 1, h0z + 1) == BR::Torch;
        ok = ok && hutOk;
        if (!hutOk) diag += QStringLiteral("[hut]");

        // (C3) 小屋前缀普查 [2,4](完好站 = 地板在前 → 前缀即小屋集)+ 槽位后继负例(未被占用的
        //      下一槽无地板)。
        int huts = 0;
        for (int i = 0; i < 4; ++i) {
            const int hx = cx + World::kVillageHutSlots[i][0];
            const int hz = cz + World::kVillageHutSlots[i][1];
            if (w.blockAt(hx, S, hz) != BR::Planks)
                break;
            ++huts;
        }
        const bool prefixOk = huts >= 2 && huts <= 4;
        ok = ok && prefixOk;
        if (!prefixOk) diag += QStringLiteral("[prefix %1]").arg(huts);
        if (huts < 4) {
            const int hx = cx + World::kVillageHutSlots[huts][0];
            const int hz = cz + World::kVillageHutSlots[huts][1];
            const bool nextFree = w.blockAt(hx, S, hz) != BR::Planks;
            ok = ok && nextFree;
            if (!nextFree) diag += QStringLiteral("[nextSlot]");
        }

        // (C4) 农田(中心 (cx+11, cz)):中行水道 Water;耕地 Farmland state=3;小麦阶段=hashVoxel 公式。
        bool farmOk = true;
        for (int dz = -3; dz <= 3; dz += 2) { // 耕地行抽样(dz=-3,-1,1,3)
            const int fx = cx + 11, fz = cz + dz;
            farmOk = farmOk && w.blockAt(fx, S, fz) == BR::Farmland
                && w.stateAt(fx, S, fz) == BR::FarmlandHydrationMax
                && w.blockAt(fx, S + 1, fz) == BR::WheatCrop
                && w.stateAt(fx, S + 1, fz) == int(w.hashVoxel(w.seed() + World::kVillageSeedOff,
                                                               fx, S + 1, fz) % 8u);
        }
        farmOk = farmOk && w.blockAt(cx + 11, S, cz) == BR::Water         // 中行水道
            && w.blockAt(cx + 8, S, cz) == BR::Water                      // 水道西端
            && w.blockAt(cx + 14, S, cz) == BR::Water;                    // 水道东端
        ok = ok && farmOk;
        if (!farmOk) diag += QStringLiteral("[farm]");

        // (C5) 道路四臂砂砾(随地形跟随面:抽样臂格带域 S-4..S+3 内恰一砂砾;样 o=3..5)+ 守卫负例
        //      (+x 臂穿农田段:水道格保持 Water 不被覆盖)。
        const auto roadAt = [&w](int px, int pz, int sy) {
            for (int yy = sy + 3; yy >= sy - 4; --yy)
                if (w.blockAt(px, yy, pz) == BR::Gravel)
                    return true;
            return false;
        };
        bool roadOk = true;
        for (int o = 3; o <= 5; ++o) {
            roadOk = roadOk
                && roadAt(cx + o, cz, S)
                && roadAt(cx - o, cz, S)
                && roadAt(cx, cz + o, S)
                && roadAt(cx, cz - o, S);
        }
        roadOk = roadOk && w.blockAt(cx + 11, S, cz) == BR::Water;        // 农田水道不被道路覆盖
        ok = ok && roadOk;
        if (!roadOk) diag += QStringLiteral("[road]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2078c village template block face column (the well answers a cobble platform"
               " ring with a water shaft three deep over a cobble bottom, the first hut answers"
               " a plank floor with three plank walls a center facing doorway an air interior a"
               " flat plank roof and an interior torch, the hut census is a prefix of two to"
               " four slots with the next slot hut free, the farm answers a water channel row"
               " with wet farmland state three and wheat crops whose stage equals the"
               " deterministic voxel hash, the four road arms answer gravel, and the road guard"
               " keeps the farm channel water unoverwritten)"
            << (ok ? QString() : diag);
    });

    // ── r2078d:结构钉族 + 相邻族零污染(双 NEG 摘面全豁免 = 结构钉对照腿)───────────────────
    //   枚举位(Village=4 尾追加 + 前四员原值 + Count=5)+ 常量族(偏移 26089/半边 14/井深 4/顶高 3
    //   /槽位表/数量骰声明行)+ 布局/桥面源钉族(generate 调用行/refill 双调用行/区域表行/概率门行/
    //   井水柱行/耕地行/道路守卫行/take Q_INVOKABLE/Main.qml 消费两行)+ 相邻族零污染(Count 154/
    //   图集 207/蛋 0x28E/粘液球 0x28C/四结构偏移原值/Pumpkin 100)。墙体写入行不钉(NEG-1 豁免面)
    //   + refill 函数体行不钉(NEG-2 豁免面,钉的是函数体外两调用行)。
    runLeg("r2078d village structure pin family column (the structure kind enum holds village at"
        " four with the count at five and the four earlier kinds untouched, the seed offset"
        " village half well depth hut roof and hut slot and hut count constants keep their"
        " declaration rows, the source pin family holds the generate call row and the two refill"
        " call rows and the region table row and the probability gate row and the well water and"
        " farmland and road guard template rows plus the take invokable and the two qml"
        " consumption rows, and the neighbouring block count atlas slimeball villager egg and"
        " the four earlier structure seed offsets keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        // (D1) 枚举位(t1108 尾追加 4/5;前四员原值零扰动)。
        const bool enumOk = int(World::StructureVillage) == 4
            && int(World::StructureKindCount) == 5
            && int(World::StructureDungeon) == 0
            && int(World::StructureMineshaft) == 1
            && int(World::StructureDesertTemple) == 2
            && int(World::StructureJungleTemple) == 3;
        ok = ok && enumOk;
        if (!enumOk) diag += QStringLiteral("[enum]");

        // (D2) 常量族值面 + 声明行源钉。
        const bool constOk = World::kVillageSeedOff == 26089
            && World::kVillageHalf == 14
            && World::kVillageWellDepth == 4
            && World::kVillageHutRoofY == 3
            && World::villageHutCount(0u) == 2
            && World::villageHutCount(2u) == 4;
        ok = ok && constOk;
        if (!constOk) diag += QStringLiteral("[const]");
        const QStringList missHdr = pinSet(srcRootForVillagePins() + QStringLiteral("/World/world.h"), {
            SrcPin("seed off decl", "static constexpr int kVillageSeedOff = 26089;", 1),
            SrcPin("half decl", "static constexpr int kVillageHalf = 14;", 1),
            SrcPin("well depth decl", "static constexpr int kVillageWellDepth = 4;", 1),
            SrcPin("hut roof decl", "static constexpr int kVillageHutRoofY = 3;", 1),
            SrcPin("hut slots decl", "static constexpr int kVillageHutSlots[4][2] = { { -8, -8 }, { 8, -8 }, { -8, 8 }, { 8, 8 } };", 1),
            SrcPin("hut count decl", "static int villageHutCount(quint32 r) { return 2 + int(r % 3u); }", 1),
            SrcPin("take invokable", "Q_INVOKABLE QVariantList takeVillageSpawnRequests();", 1),
            SrcPin("refill decl", "void refillVillageSpawnRequests();", 1)});
        ok = ok && missHdr.isEmpty();
        if (!missHdr.isEmpty()) diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));

        // (D3) world.cpp 布局/桥面源钉族(NEG 豁免面不钉:墙体写入行/refill 函数体行)。
        const QStringList missWc = pinSet(srcRootForVillagePins() + QStringLiteral("/World/world.cpp"), {
            SrcPin("generate call", "placeVillages();", 1),
            SrcPin("refill calls", "refillVillageSpawnRequests();", 2),
            SrcPin("region table row", "m_structureRegions[StructureVillage] = villageSites();", 1),
            SrcPin("probability gate", "if ((r % 100u) >= kVillagePct) continue;", 1),
            SrcPin("well water shaft", "putSolid(cx, S - dy, cz, BlockRegistry::Water);", 1),
            SrcPin("farmland row", "putSolid(fx, S, fz, BlockRegistry::Farmland,", 1),
            // [lawful 修订 t1109/r2079] road guard 扩 Sand（沙漠列真地表承接面——t1109 沙漠村庄变体）：
            //   原钉行 `if (cur != BlockRegistry::Grass && cur != BlockRegistry::Dirt) continue;` 沿革
            //   见 section72 头注；拆两针钉新形态（首行 + Sand 扩展行），沙漠站点行为不触本钉。
            SrcPin("road guard", "if (cur != BlockRegistry::Grass && cur != BlockRegistry::Dirt", 1),
            SrcPin("road guard sand ext", "&& cur != BlockRegistry::Sand)", 1)});
        ok = ok && missWc.isEmpty();
        if (!missWc.isEmpty()) diag += QStringLiteral("[wc %1]").arg(missWc.join(QLatin1Char(',')));

        // (D4) Main.qml 桥消费两行。
        const QStringList missMq = pinSet(srcRootForVillagePins() + QStringLiteral("/ui/Main.qml"), {
            SrcPin("take row", "theWorld.takeVillageSpawnRequests()", 1),
            SrcPin("spawn row", "entityManager.spawnMobTyped(vreqs[vi], vreqs[vi + 1], vreqs[vi + 2],", 1)});
        ok = ok && missMq.isEmpty();
        if (!missMq.isEmpty()) diag += QStringLiteral("[mq %1]").arg(missMq.join(QLatin1Char(',')));

        // (D5) 相邻族零污染。
        const bool neighOk = int(BR::Count) == 162 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）；t1112 曾 157→160（栅栏门/玻璃板/蛋糕尾部追加）；t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && int(BR::AtlasTileCount) == 210 // t1113 前移：Count 160→162 / Atlas 209→210（牌子双 id + 牌板 tile 段尾追加）
            && RecipeRegistry::SlimeBallId == 0x28C
            && RecipeRegistry::SpawnEggVillagerId == 0x28E
            && int(EntityManager::MobVillager) == 22
            && int(EntityManager::MobSlime) == 21
            && World::kDungeonSeedOff == 12037
            && World::kMineshaftSeedOff == 15047
            && World::kDesertTempleSeedOff == 19487
            && World::kJungleTempleSeedOff == 22617
            && int(BR::Pumpkin) == 100;
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2078d village structure pin family column (the structure kind enum holds"
               " village at four with the count at five and the four earlier kinds untouched,"
               " the seed offset village half well depth hut roof and hut slot and hut count"
               " constants keep their declaration rows, the source pin family holds the generate"
               " call row and the two refill call rows and the region table row and the"
               " probability gate row and the well water and farmland and road guard template"
               " rows plus the take invokable and the two qml consumption rows, and the"
               " neighbouring block count atlas slimeball villager egg and the four earlier"
               " structure seed offsets keep their original values)"
            << (ok ? QString() : diag);
    });
}
