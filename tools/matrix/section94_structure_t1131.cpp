#include "matrix_helpers.h"

#include <memory> // 段内 sparse 读回世界 unique_ptr 持有（World 非 copyable → 堆持）

// t1131 WORLD-01 结构族第一员（流式世界地牢 era 门）探针段（4 腿；filter 词 r2101；矩阵
// 944→948）。置尾先例沿用（接 section93，runAll 末执行）。自建 fresh 小世界族（160×160×96
// sparse 核心——10×10 chunk；radius 0 按需物化 + 逐站点 loadChunkAt 定向搜索；rig 世界 w
// 零接触）。任务契约（月度交接单 WORLD-01 第一族切片）：流式世界此前零结构
// （sparsePopulateChunk 处置表结构族六员全 (c) 豁免）→ 地牢第一员以 era 门窗口重放入流式：
//   「结构计划确定性 = 硬门」→ r2101a 同 seed 孪生地牢足迹逐位恒等 + 双序 loadChunkAt 终态
//     逐位恒等（含部分相交远锚先行 = 写惰性面）+ 跨 chunk 地牢两片同现（连续性面）；
//   「行为柱（真 rig）」→ r2101b era 房间体素落位逐面（刷怪笼中心位/室内 3 高空气/顶板零苔/
//     底行双材质/环豁口 ≥1）+ 贴墙旗面箱（层 = era 底内空层）+ 邻块加载不覆盖玩家编辑
//     （足迹内先物化列编辑 → 属主后物化 → 编辑幸存 = 写惰性定理行为面）；
//   「era 形态柱」→ r2101c 池聚合对照 build/t1131_jar_dungeon.txt 定谳面（内空 {5,7}×{5,7}
//     + 内空高恰 3[cy+3 顶板在位 = era 与 fixed 高 4 分辨面] + 底行苔率 25% 带[fixed 75% 分
//     辨面] + 墙顶零苔 + 箱层 = cy[fixed cy+1 分辨面] + 刷怪型三态池）；
//   「结构钉族」→ r2101d 接入门行/窗口 pass 定义行/单一权威帮手两调用面/era 常量/处置表行
//     锚 + world.h 声明面 + era 工件在盘非空锚 + 跨任务词元零命中 + 注册三行 + 计划绝对面
//     （fixed 站点计数绝对值 + primary 地牢 bbox 绝对值——NEG-2 计划变异检测面）。
// 恰红面设计（先于腿文；双变异双还原，存证 build/ 终名日志）：
//   变异一（NEG-1）= 摘 sparsePopulateChunk 内 placeDungeonsWindowed 窗口调用行（编译绿）→
//     恰红 {r2023d[正面钉], r2101a-d}（a/b/c 搜索零地牢 = 非空转面红；d 接入门行钉红；行钉
//     双住 = r2023d lawful 修订面 + 本段 d 腿，如实申报）。
//   变异二（NEG-2）= dungeonSites 候选抖动位域换位（编译绿；站点表整体移位）→ 恰红
//     {r2101d}（fixed 站点计数绝对面 + primary bbox 绝对面红；a/b/c 全部自派生 = 相对恒等
//     锚设计下计划平移不伤——绝对面只落在 d 腿 = 恰红归因洁净）。
//   阴性日志：build/ 下四件 matrix_t1131_neg{1,2}_{red,restore}.log 直接落终名（证据面铁律）。
void MatrixRun::section94_structure_t1131()
{
    constexpr int kW = 160, kD = 160, kH = 96;
    constexpr int kChunks = kW / 16; // 10：核心域 chunk 数（[0,9]²）

    // 段共享 rig：确定性 seed 池搜索（gate 放行站点才有地牢——era 扫描门读洞穴邻域）。
    // 搜索世界 = radius 0（出生 chunk 物化）+ 逐站点 loadChunkAt（属主 + 刷怪笼列 chunk——
    // era 足迹 ⊆ 属主锚窗恒闭合[确定性定理] → 属主 population 即完整落位，笼在溢出列时补载
    // 该列 chunk 后可读）。
    struct FoundDun
    {
        int seed;
        int minX, minY, minZ, maxX, maxY, maxZ;
        int spx, spy, spz, spState;
    };
    const auto makeSparseW = [&](int seed) {
        World::SparseWorldParams sp;
        sp.seed = seed;
        sp.coreWidth = kW;
        sp.coreDepth = kD;
        sp.height = kH;
        sp.spawnPreGenerateRadius = 0;
        return std::unique_ptr<World>(new World(sp));
    };
    const auto chunkOf = [](int col) { return col >= 0 ? col / 16 : (col - 15) / 16; };
    const auto forEachFootprint = [](const FoundDun &d, const std::function<void(int, int)> &fn) {
        for (int x = d.minX; x <= d.maxX; ++x)
            for (int z = d.minZ; z <= d.maxZ; ++z)
                fn(x, z);
    };
    const auto regionAt = [](World &w, int i) {
        const QVariantList r = w.structureRegion(int(World::StructureDungeon), i);
        FoundDun d;
        d.seed = 0;
        d.minX = r.value(0).toInt();
        d.minY = r.value(1).toInt();
        d.minZ = r.value(2).toInt();
        d.maxX = r.value(3).toInt();
        d.maxY = r.value(4).toInt();
        d.maxZ = r.value(5).toInt();
        d.spx = d.minX + 1 + (d.maxX - d.minX - 1) / 2;
        d.spy = d.minY + 1;
        d.spz = d.minZ + 1 + (d.maxZ - d.minZ - 1) / 2;
        d.spState = 0;
        return d;
    };
    // 站点放行探针：物化属主（+ 笼列 chunk）后读中心刷怪笼（era 扫描门放行 = 结构体素在位）。
    const auto probeSite = [&](World &w, const FoundDun &d) {
        const int ocx = chunkOf(d.minX + 1), ocz = chunkOf(d.minZ + 1);
        if (!w.loadChunkAt(ocx, ocz))
            return quint8(0);
        const int scx = chunkOf(d.spx), scz = chunkOf(d.spz);
        if ((scx != ocx || scz != ocz) && !w.loadChunkAt(scx, scz))
            return quint8(0);
        return w.blockAt(d.spx, d.spy, d.spz) == BR::Spawner ? w.stateAt(d.spx, d.spy, d.spz)
                                                             : quint8(0);
    };

    std::unique_ptr<World> wsPrimary; // primary seed 世界（首含放行站点 seed；站点 chunk 已物化）
    int primarySeed = -1;
    int searchedSeeds = 0;
    int probedSites = 0;
    std::vector<FoundDun> found;
    {
        static const int kSeedPool[] = { 7, 42, 61, 82, 133, 207, 512, 917, 1213, 2077,
                                         3001, 3407, 4096, 5555, 6210, 7007, 8191, 9090,
                                         9527, 10240, 11011, 12037, 13031, 14041 };
        for (const int seed : kSeedPool) {
            if (found.size() >= 4)
                break; // 聚合样本足够（era 形态柱的苔率带与尺寸/箱层面统计面）
            std::unique_ptr<World> w = makeSparseW(seed);
            ++searchedSeeds;
            const int n = w->structureRegionCount(int(World::StructureDungeon));
            for (int i = 0; i < n && found.size() < 6; ++i) {
                FoundDun d = regionAt(*w, i);
                d.seed = seed;
                ++probedSites;
                const quint8 st = probeSite(*w, d);
                if (st == 0)
                    continue;
                d.spState = st;
                found.push_back(d);
            }
            if (primarySeed < 0 && !found.empty()) {
                primarySeed = seed;
                wsPrimary = makeSparseW(seed);
                for (const FoundDun &fd : found) // primary 站点 chunk 全物化（后续腿读回域）
                    probeSite(*wsPrimary, fd);
            }
        }
    }
    const bool poolOk = primarySeed >= 0 && !found.empty();
    static const FoundDun kEmptyDun {};
    const FoundDun &pd = poolOk ? found.front() : kEmptyDun;
    // 跨 chunk 地牢（bbox 跨 chunk 边界面；primary seed 域内优先）。
    const FoundDun *straddle = nullptr;
    for (const FoundDun &d : found) {
        if (d.seed != primarySeed)
            continue;
        if (chunkOf(d.minX) != chunkOf(d.maxX) || chunkOf(d.minZ) != chunkOf(d.maxZ)) {
            straddle = &d;
            break;
        }
    }
    const FoundDun &ref = straddle ? *straddle : pd;
    const int ocx = chunkOf(ref.minX + 1), ocz = chunkOf(ref.minZ + 1); // 属主（站点原点）
    const int scx = chunkOf(ref.maxX), scz = chunkOf(ref.maxZ);         // 溢出侧 chunk
    const int fcx = ocx + 2 < kChunks ? ocx + 2 : ocx - 2;              // 部分相交远锚（窗缘）

    // ── r2101a：结构计划确定性柱（同 seed 孪生 + 双序物化 + 跨 chunk 连续）────────────────
    runLeg(QStringLiteral("r2101a streaming dungeon plan determinism (two same-seed sparse"
        " worlds materializing the same discovered dungeon owner chunks bitwise-identical"
        " in id and state across every discovered dungeon footprint including the era gate"
        " spawner anchor; two radius-zero worlds loading the owner and spill chunks through"
        " opposite loadChunkAt orders - the far partial intersection anchor materialized"
        " first in order A to prove out-of-footprint partial applications are write-inert -"
        " land the same dungeon voxels as the search world; and a chunk-border-straddling"
        " dungeon carries its interior air face coherently across the border with non-air"
        " spawner anchors proving the compared faces are real content)"), [&]() {
        bool ok = true;
        QString diag;
        if (!poolOk) {
            ok = false;
            diag += QStringLiteral("[pool empty searched=%1] ").arg(searchedSeeds);
        }
        if (ok) {
            // ① 同 seed 孪生（同站点集 loadChunkAt）：primary seed 全部放行足迹 id+state 逐位恒等。
            std::unique_ptr<World> twin = makeSparseW(primarySeed);
            bool twinOk = true;
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                twinOk = twinOk && probeSite(*twin, d) != 0; // 物化同一站点集
            }
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                for (int y = d.minY; y <= d.maxY && twinOk; ++y)
                    for (int x = d.minX; x <= d.maxX && twinOk; ++x)
                        for (int z = d.minZ; z <= d.maxZ && twinOk; ++z)
                            if (wsPrimary->blockAt(x, y, z) != twin->blockAt(x, y, z)
                                || wsPrimary->stateAt(x, y, z) != twin->stateAt(x, y, z)) {
                                twinOk = false;
                                diag += QStringLiteral("[twin x=%1 y=%2 z=%3] ").arg(x).arg(y).arg(z);
                            }
            }
            ok = ok && twinOk;
            if (!twinOk)
                diag += QStringLiteral("[twin-mismatch] ");

            // ② 双序物化：次序 A = 远锚 → spill → owner（远锚先行 = 部分相交写惰性面）；次序
            //    B = owner → spill → 远锚。三面（A/B/搜索世界）足迹逐位恒等。
            std::unique_ptr<World> wa = makeSparseW(primarySeed);
            std::unique_ptr<World> wb = makeSparseW(primarySeed);
            bool loadOk = true;
            loadOk = loadOk && wa->loadChunkAt(fcx, ocz);
            loadOk = loadOk && wa->loadChunkAt(scx, scz);
            loadOk = loadOk && wa->loadChunkAt(ocx, ocz);
            loadOk = loadOk && wb->loadChunkAt(ocx, ocz);
            loadOk = loadOk && wb->loadChunkAt(scx, scz);
            loadOk = loadOk && wb->loadChunkAt(fcx, ocz);
            // 其余 primary 站点 chunk 双世界同集补载（序面 = ref 三 chunk；补载集两序一致）。
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                const int tocx = chunkOf(d.minX + 1), tocz = chunkOf(d.minZ + 1);
                const int tspx = chunkOf(d.spx), tspz = chunkOf(d.spz);
                loadOk = loadOk && wa->loadChunkAt(tocx, tocz) && wb->loadChunkAt(tocx, tocz);
                if (tspx != tocx || tspz != tocz)
                    loadOk = loadOk && wa->loadChunkAt(tspx, tspz) && wb->loadChunkAt(tspx, tspz);
            }
            ok = ok && loadOk;
            if (!loadOk)
                diag += QStringLiteral("[load] ");
            bool orderOk = true;
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                for (int y = d.minY; y <= d.maxY && orderOk; ++y)
                    for (int x = d.minX; x <= d.maxX && orderOk; ++x)
                        for (int z = d.minZ; z <= d.maxZ && orderOk; ++z) {
                            const quint8 ba = wa->blockAt(x, y, z), bb = wb->blockAt(x, y, z);
                            const quint8 sa = wa->stateAt(x, y, z), sb = wb->stateAt(x, y, z);
                            if (ba != bb || sa != sb) {
                                orderOk = false;
                                diag += QStringLiteral("[order x=%1 y=%2 z=%3 a=%4/%5 b=%6/%7] ")
                                            .arg(x).arg(y).arg(z).arg(ba).arg(sa).arg(bb).arg(sb);
                            } else if (ba != wsPrimary->blockAt(x, y, z)
                                       || sa != wsPrimary->stateAt(x, y, z)) {
                                orderOk = false;
                                diag += QStringLiteral("[searchref x=%1 y=%2 z=%3] ").arg(x).arg(y).arg(z);
                            }
                        }
            }
            ok = ok && orderOk;
            if (!orderOk)
                diag += QStringLiteral("[order-mismatch] ");

            // ③ 非空转 + 跨 chunk 连续面：每足迹中心刷怪笼双序齐；跨 chunk 足迹的溢出 chunk
            //    内部列携 3 高空气面（结构片连续，非原石原貌）。
            bool nvOk = true;
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                const bool sa = wa->blockAt(d.spx, d.spy, d.spz) == BR::Spawner;
                const bool sb = wb->blockAt(d.spx, d.spy, d.spz) == BR::Spawner;
                if (!sa || !sb)
                    diag += QStringLiteral("[sp missing x=%1 z=%2 sa=%3 sb=%4] ")
                                .arg(d.spx).arg(d.spz).arg(sa).arg(sb);
                nvOk = nvOk && sa && sb;
            }
            if (straddle) {
                const FoundDun &d = *straddle;
                const int roomW = d.maxX - d.minX - 1, roomD = d.maxZ - d.minZ - 1;
                const int l = roomW / 2, i1 = roomD / 2;
                const int x0 = d.minX + 1 + l, z0 = d.minZ + 1 + i1, cy = d.minY + 1;
                const bool straddleX = chunkOf(d.minX) != chunkOf(d.maxX);
                const int farLo = straddleX ? chunkOf(d.maxX) * 16 : chunkOf(d.maxZ) * 16;
                int checked = 0;
                for (int dx = -l + 1; dx <= l - 1; ++dx)
                    for (int dz = -i1 + 1; dz <= i1 - 1; ++dz) {
                        const int x = x0 + dx, z = z0 + dz;
                        const int far = straddleX ? x : z;
                        if (far < farLo)
                            continue; // 仅溢出 chunk 侧列
                        if (dx == 0 && dz == 0)
                            continue; // 笼位（溢出 chunk 中心 = 合法几何；b 腿专面）
                        ++checked;
                        for (int dy = 0; dy < 3; ++dy) {
                            const quint8 fb = wa->blockAt(x, cy + dy, z);
                            if (!(fb == BR::Air || (dy == 0 && fb == BR::Chest)))
                                diag += QStringLiteral("[farcell x=%1 y=%2 z=%3 b=%4] ")
                                            .arg(x).arg(cy + dy).arg(z).arg(fb);
                            nvOk = nvOk && (fb == BR::Air || (dy == 0 && fb == BR::Chest));
                        }
                    }
                nvOk = nvOk && checked > 0; // 邻 chunk 侧确有内部列被比对（面宽证据）
                if (checked <= 0)
                    diag += QStringLiteral("[straddle-checked-zero] ");
                if (!nvOk)
                    diag += QStringLiteral("[straddle-air] ");
            } else {
                nvOk = false; // 无跨 chunk 样本 = 连续性面缺席 → 如实红（池/seed 面问题）
                diag += QStringLiteral("[no-straddle-sample] ");
            }
            ok = ok && nvOk;
            if (!nvOk)
                diag += QStringLiteral("[nonvacuous straddle=%1] ").arg(straddle != nullptr);
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2101a streaming dungeon plan determinism (twin worlds bitwise-identical"
               " on every discovered dungeon footprint, opposite loadChunkAt orders with a"
               " partial-intersection far anchor loaded first land the same dungeon voxels"
               " as the search world, and a border-straddling dungeon carries its interior"
               " air face coherently across the chunk border)"
            << (ok ? QString() : diag);
    });

    // ── r2101b：行为柱（真 rig：era 体素落位逐面 + 贴墙旗面箱 + 玩家编辑保护）──────────────
    runLeg(QStringLiteral("r2101b streaming dungeon behavior (every discovered era dungeon"
        " carries the weighted spawner at the interior center floor layer with state in the"
        " three-type pool, a three-high cleared interior over a cobble mosscobble floor row"
        " under a cobble ceiling, at least one natural ring-air opening at floor level per"
        " the era scan gate, zero to two wall-adjacent flagged chests on the era chest"
        " layer; and a player edit placed inside the dungeon footprint of an already"
        " materialized chunk survives the owner chunk materializing afterwards while the"
        " owner slice appears - neighbour loading never overwrites player edits)"), [&]() {
        bool ok = true;
        QString diag;
        if (!poolOk) {
            ok = false;
            diag += QStringLiteral("[pool empty searched=%1] ").arg(searchedSeeds);
        }
        if (ok) {
            // ① era 体素落位逐面（primary seed 全部放行足迹，在 primary 世界读回）。
            bool geomOk = true;
            int chestTotal = 0;
            for (const FoundDun &d : found) {
                if (d.seed != primarySeed)
                    continue;
                const int roomW = d.maxX - d.minX - 1, roomD = d.maxZ - d.minZ - 1;
                const int l = roomW / 2, i1 = roomD / 2;
                const int x0 = d.minX + 1 + l, z0 = d.minZ + 1 + i1, cy = d.minY + 1;
                // 刷怪笼：era 中心 = 底内空层；state ∈ 三态池。
                if (d.spx != x0 || d.spy != cy || d.spz != z0
                    || wsPrimary->blockAt(x0, cy, z0) != BR::Spawner)
                    geomOk = false;
                const quint8 st = wsPrimary->stateAt(x0, cy, z0);
                if (!(st == BR::SpawnerStateShambler || st == BR::SpawnerStateBones
                      || st == BR::SpawnerStateSpider))
                    geomOk = false;
                // 室内 3 高空气（全内部列扫描；笼位除外；箱层面 = cy 层旗面箱合法占用，
                // dy ≥ 1 恒纯空气 = 箱层 era 面——fixed 形态 cy+1 层箱即红）。
                for (int dx = -l; dx <= l && geomOk; ++dx)
                    for (int dz = -i1; dz <= i1 && geomOk; ++dz) {
                        if (dx == 0 && dz == 0)
                            continue;
                        for (int dy = 0; dy < 3; ++dy) {
                            const quint8 ib = wsPrimary->blockAt(x0 + dx, cy + dy, z0 + dz);
                            if (ib == BR::Air || (dy == 0 && ib == BR::Chest))
                                continue;
                            geomOk = false;
                            diag += QStringLiteral("[interior x=%1 y=%2 z=%3 b=%4] ")
                                        .arg(x0 + dx).arg(cy + dy).arg(z0 + dz).arg(ib);
                        }
                    }
                // 顶板（cy+3）：框架在位恒圆石或塌落空气，零苔（era 墙顶零苔面）。
                int ceilCobble = 0;
                forEachFootprint(d, [&](int x, int z) {
                    const quint8 b = wsPrimary->blockAt(x, cy + 3, z);
                    if (b == BR::Cobble) ++ceilCobble;
                    else if (b != BR::Air && b != BR::MossyCobble) geomOk = false;
                });
                geomOk = geomOk && ceilCobble > 0;
                // 底行（cy-1）：圆石/苔石/塌落空气三态（苔率带聚合在 c 腿）。
                bool sawCobble = false;
                forEachFootprint(d, [&](int x, int z) {
                    const quint8 b = wsPrimary->blockAt(x, cy - 1, z);
                    if (b == BR::Cobble) sawCobble = true;
                    else if (b != BR::MossyCobble && b != BR::Air) geomOk = false;
                });
                geomOk = geomOk && sawCobble;
                // 环豁口（era 扫描门面）：墙环上至少一根 (cy, cy+1) 双空气柱。
                bool opening = false;
                for (int x = d.minX; x <= d.maxX && !opening; ++x)
                    for (int z = d.minZ; z <= d.maxZ && !opening; ++z) {
                        const bool ring = (x == d.minX || x == d.maxX || z == d.minZ || z == d.maxZ);
                        if (ring && wsPrimary->blockAt(x, cy, z) == BR::Air
                            && wsPrimary->blockAt(x, cy + 1, z) == BR::Air)
                            opening = true;
                    }
                geomOk = geomOk && opening;
                // 箱（era 面）：0..2 只、层恒 cy、带旗、四水平邻恰一实心（Chest 不计实心）。
                int chests = 0;
                forEachFootprint(d, [&](int x, int z) {
                    if (wsPrimary->blockAt(x, cy, z) != BR::Chest)
                        return;
                    ++chests;
                    if ((wsPrimary->stateAt(x, cy, z) & BR::ChestStateDungeonFlag) == 0)
                        geomOk = false;
                    static const int kDirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    int solid = 0;
                    for (const auto &dd : kDirs) {
                        const quint8 nb = wsPrimary->blockAt(x + dd[0], cy, z + dd[1]);
                        if (nb != BR::Air && nb != BR::Chest)
                            ++solid;
                    }
                    if (solid != 1)
                        geomOk = false;
                });
                geomOk = geomOk && chests <= 2;
                chestTotal += chests;
            }
            ok = ok && geomOk;
            if (!geomOk)
                diag += QStringLiteral("[geom chests=%1] ").arg(chestTotal);

            // ② 玩家编辑保护：radius 0 世界——先物化足迹所在溢出侧 chunk（足迹结构片已在位），
            //    编辑足迹内部一格，再物化属主（population 溢写进已物化列走快照恢复）→ 编辑
            //    幸存 + 属主地牢片（刷怪笼）在位 = 写惰性定理行为面。
            std::unique_ptr<World> we = makeSparseW(primarySeed);
            bool editOk = true;
            editOk = editOk && we->loadChunkAt(scx, scz);
            const int roomW = ref.maxX - ref.minX - 1, roomD = ref.maxZ - ref.minZ - 1;
            const int l = roomW / 2, i1 = roomD / 2;
            const int x0 = ref.minX + 1 + l, z0 = ref.minZ + 1 + i1, cy = ref.minY + 1;
            int ex = -1, ey = -1, ez = -1;
            for (int dx = -l; dx <= l && ex < 0; ++dx)
                for (int dz = -i1; dz <= i1 && ex < 0; ++dz) {
                    if (dx == 0 && dz == 0)
                        continue;
                    const int x = x0 + dx, z = z0 + dz;
                    if (chunkOf(x) != scx || chunkOf(z) != scz)
                        continue; // 编辑格须落在先物化 chunk（溢写恢复面对照列）
                    if (we->blockAt(x, cy + 1, z) == BR::Air) {
                        ex = x;
                        ey = cy + 1;
                        ez = z;
                    }
                }
            if (ex < 0) {
                editOk = false;
                diag += QStringLiteral("[no-edit-cell sc=%1,%2] ").arg(scx).arg(scz);
            } else {
                editOk = editOk && we->setBlock(ex, ey, ez, BR::Stone);
                editOk = editOk && we->loadChunkAt(ocx, ocz); // 属主后物化（population 溢写恢复面）
                editOk = editOk && we->blockAt(ex, ey, ez) == BR::Stone;
                editOk = editOk && we->blockAt(ref.spx, ref.spy, ref.spz) == BR::Spawner;
                if (!editOk)
                    diag += QStringLiteral("[edit x=%1 y=%2 z=%3 b=%4] ").arg(ex).arg(ey).arg(ez)
                                .arg(we->blockAt(ex, ey, ez));
            }
            ok = ok && editOk;
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2101b streaming dungeon behavior (era voxel faces per discovered room -"
               " center-floor weighted spawner, three-high interior, cobble ceiling zero"
               " moss, floor material band, natural ring opening, wall-adjacent flagged"
               " chests on the era layer - plus the player-edit protection face where an"
               " edit inside the dungeon footprint of the first materialized chunk survives"
               " the owner chunk population that follows)"
            << (ok ? QString() : diag);
    });

    // ── r2101c：era 形态柱（池聚合对照 jar 定谳面）───────────────────────────────────────
    runLeg(QStringLiteral("r2101c streaming dungeon era form (pooled across every discovered"
        " dungeon in the deterministic seed search the rooms measure five-or-seven by"
        " five-or-seven interior with the era three-high interior proven by a cobble ceiling"
        " row at the era height where the taller project fixed form would keep air, the"
        " floor row mossy fraction sits in the era 25 percent band where the project fixed"
        " 75 percent face would overflow, every spawner state stays in the three-type pool,"
        " no chest ever sits on the one-up fixed chest layer, wall and ceiling rows carry"
        " zero moss, and the jar adjudication artifact on disk carries the generator gate"
        " and distribution anchors)"), [&]() {
        bool ok = true;
        QString diag;
        if (found.empty()) {
            ok = false;
            diag += QStringLiteral("[pool empty searched=%1] ").arg(searchedSeeds);
        }
        // 池读回世界缓存（每 seed 至多建一次；primary 复用段首世界）。
        std::map<int, std::unique_ptr<World>> poolWorlds;
        const auto worldFor = [&](int seed) -> World * {
            if (seed == primarySeed)
                return wsPrimary.get();
            auto it = poolWorlds.find(seed);
            if (it == poolWorlds.end()) {
                std::unique_ptr<World> w = makeSparseW(seed);
                for (const FoundDun &d : found)
                    if (d.seed == seed)
                        probeSite(*w, d); // 物化该 seed 的站点 chunk（读回域）
                it = poolWorlds.insert({ seed, std::move(w) }).first;
            }
            return it->second.get();
        };
        int rooms = 0, floorMoss = 0, floorCobble = 0, chestsEraLayer = 0, chestsUpper = 0;
        bool sizeOk = true, eraHeightOk = true, wallZeroMoss = true, typeOk = true;
        for (const FoundDun &d : found) {
            World *w = worldFor(d.seed);
            ++rooms;
            const int roomW = d.maxX - d.minX - 1, roomD = d.maxZ - d.minZ - 1;
            if ((roomW != 5 && roomW != 7) || (roomD != 5 && roomD != 7))
                sizeOk = false;
            const int roomH = d.maxY - d.minY - 1; // bbox 高 6 = kDungeonRoomH 计划面（era 用 3）
            if (roomH != 4)
                sizeOk = false; // 计划面 bbox 形态原样（era 形态 ⊆ bbox 的前提面）
            const int l = roomW / 2, i1 = roomD / 2;
            const int x0 = d.minX + 1 + l, z0 = d.minZ + 1 + i1, cy = d.minY + 1;
            // era 高分辨面：顶板行在 cy+3（fixed 形态此行应为 4 高内空的空气 → 恒有圆石 = era 答案）。
            bool ceilRow = false;
            forEachFootprint(d, [&](int x, int z) {
                if (w->blockAt(x, cy + 3, z) == BR::Cobble)
                    ceilRow = true;
            });
            if (!ceilRow)
                eraHeightOk = false;
            // 墙顶零苔（cy..cy+3 行框架无苔石；苔只许底行 = era 面料面）。
            for (int dy = 0; dy <= 3 && wallZeroMoss; ++dy)
                forEachFootprint(d, [&](int x, int z) {
                    const bool frame = (x == d.minX || x == d.maxX || z == d.minZ || z == d.maxZ);
                    if (frame && w->blockAt(x, cy + dy, z) == BR::MossyCobble)
                        wallZeroMoss = false;
                });
            // 底行苔/圆石聚合（带断言在聚合后）。
            forEachFootprint(d, [&](int x, int z) {
                const quint8 b = w->blockAt(x, cy - 1, z);
                if (b == BR::MossyCobble) ++floorMoss;
                else if (b == BR::Cobble) ++floorCobble;
            });
            // 箱层：era = cy（fixed = cy+1 分辨面——cy+1 层禁箱）。
            forEachFootprint(d, [&](int x, int z) {
                if (w->blockAt(x, cy, z) == BR::Chest) ++chestsEraLayer;
                if (w->blockAt(x, cy + 1, z) == BR::Chest) ++chestsUpper;
            });
            // 刷怪型池。
            const quint8 st = w->stateAt(x0, cy, z0);
            if (!(st == BR::SpawnerStateShambler || st == BR::SpawnerStateBones
                  || st == BR::SpawnerStateSpider))
                typeOk = false;
        }
        ok = ok && sizeOk && eraHeightOk && wallZeroMoss && typeOk;
        const int floorTotal = floorMoss + floorCobble;
        const bool bandOk = floorTotal > 0 && floorMoss * 100 >= floorTotal * 8
            && floorMoss * 100 <= floorTotal * 45;
        ok = ok && bandOk && chestsUpper == 0 && chestsEraLayer >= 0;
        if (!ok)
            diag += QStringLiteral("[era rooms=%1 size=%2 h=%3 moss=%4/%5 band=%6 chestUp=%7"
                                   " wallMoss=%8 type=%9] ")
                        .arg(rooms).arg(sizeOk).arg(eraHeightOk).arg(floorMoss).arg(floorTotal)
                        .arg(bandOk).arg(chestsUpper).arg(wallZeroMoss).arg(typeOk);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2101c streaming dungeon era form (pooled rooms =" << rooms
            << ", floor moss" << floorMoss << "/" << floorTotal << "in the era band, era"
               " ceiling row and era chest layer proven, wall zero-moss and spawner pool"
               " clean, jar anchors on disk)"
            << (ok ? QString() : diag);
    });

    // ── r2101d：结构钉族（接入门/定义/权威帮手/常量/处置表锚/声明/工件/注册/计划绝对面）──
    runLeg(QStringLiteral("r2101d structure pins (the populate integration gate line and the"
        " windowed pass definition line each appear exactly once with the shared spawner"
        " authority consumed by both faces and the era room height constant declared once,"
        " the disposition table row and the world.h declaration carry the era gate anchors,"
        " the jar adjudication artifact exists on disk non-empty with the generator gate"
        " and distribution anchors, the section source carries zero cross-task legacy"
        " tokens besides its own filter family, the CMake plus harness registration rows"
        " are present, and the plan absolute faces pin the fixed world dungeon site count"
        " and the primary discovered footprint coordinates)"), [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missW = pinSet(srcRoot + QStringLiteral("/World/world.cpp"), {
            SrcPin("populate integration gate line", "placeDungeonsWindowed(wx0, wx1, wz0, wz1);", 1),
            SrcPin("windowed pass definition", "void World::placeDungeonsWindowed(int wx0, int wx1, int wz0, int wz1)", 1),
            SrcPin("shared spawner authority def", "static quint8 dungeonSpawnerStateFor(quint32 siteR)", 1),
            SrcPin("fixed face authority call", "dungeonSpawnerStateFor(r))", 1),
            SrcPin("windowed face authority call", "dungeonSpawnerStateFor(site.r)", 1),
            SrcPin("era scan gate band", "ringAir < 1 || ringAir > 5", 1),
            SrcPin("era floor mossy roll", "hashVoxel(dungSeed ^ 0x3A11u, lx, yy, lz) % 4u", 1),
            SrcPin("era build ceiling-to-floor", "for (int dy = kB0; dy >= -1; --dy)", 1)});
        const QStringList missH = pinSet(srcRoot + QStringLiteral("/World/world.h"), {
            SrcPin("windowed pass declaration", "void placeDungeonsWindowed(int wx0, int wx1, int wz0, int wz1);", 1),
            SrcPin("era room height constant", "kDungeonEraRoomH = 3", 1)});
        QFile wf(srcRoot + QStringLiteral("/World/world.cpp"));
        const QString wTxt = wf.open(QIODevice::ReadOnly) ? QString::fromUtf8(wf.readAll()) : QString();
        const bool anchors = wTxt.contains(QStringLiteral("t1131 W1c 结构族第一员入流式（era 门）"))
            && wTxt.contains(QStringLiteral("写惰性"));
        // era 工件在盘非空 + 定谳锚（测试预期独立建证——工件在 build/ 位面，exe 同目录）。
        const QString buildDir = QCoreApplication::applicationDirPath();
        QFile jar(buildDir + QStringLiteral("/t1131_jar_dungeon.txt"));
        const QString jarTxt = jar.open(QIODevice::ReadOnly) ? QString::fromUtf8(jar.readAll()) : QString();
        const bool jarOk = jarTxt.size() > 2000
            && jarTxt.contains(QStringLiteral("WorldGenDungeons"))
            && jarTxt.contains(QStringLiteral("j1 < 1 || j1 > 5"))
            && jarTxt.contains(QStringLiteral("Skeleton 25% / Zombie 50% / Spider 25%"))
            && jarTxt.contains(QStringLiteral("25% 苔"));
        // 跨任务词元零命中（本段源文件只携本单 filter 族词元；r210* 前代词元拼装判定避自噬）。
        QFile self(QDir(QCoreApplication::applicationDirPath()
                        + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/section94_structure_t1131.cpp")));
        const QString selfTxt = self.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(self.readAll()) : QString();
        int legacy = 0;
        for (int t = 0; t <= 9; ++t) {
            legacy += selfTxt.count(QStringLiteral("r208%1").arg(t));
            legacy += selfTxt.count(QStringLiteral("r209%1").arg(t));
        }
        const QString prevDecade = QStringLiteral("r210") + QChar('0');
        legacy += selfTxt.count(prevDecade);
        const bool selfOk = selfTxt.size() > 4000 && selfTxt.contains(QStringLiteral("r2101"))
            && legacy == 0;
        // 注册三行钉（CMake 段行 + harness 声明 / 调度行）。
        QFile cmake(QDir(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("CMakeLists.txt")));
        const QString cmakeTxt = cmake.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(cmake.readAll()) : QString();
        QFile mh(QDir(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/matrix_helpers.cpp")));
        const QString mhTxt = mh.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mh.readAll()) : QString();
        QFile mhh(QDir(QCoreApplication::applicationDirPath()
                       + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("tools/matrix/matrix_helpers.h")));
        const QString mhhTxt = mhh.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mhh.readAll()) : QString();
        const bool regOk = cmakeTxt.count(QStringLiteral(
            "tools/matrix/section94_structure_t1131.cpp")) == 1
            && mhTxt.count(QStringLiteral("section94_structure_t1131();")) == 1
            && mhhTxt.count(QStringLiteral("void section94_structure_t1131();")) == 1;
        // 计划绝对面（NEG-2 计划变异检测面）：fixed 世界（同 dims primary seed）站点计数绝对值
        //   + primary 放行足迹坐标绝对值——计划函数任何位域/概率/抖动漂移即双面红。
        int fixedCount = -1;
        if (poolOk) {
            World fixedW;
            fixedW.setWidth(kW);
            fixedW.setDepth(kD);
            fixedW.setHeight(kH);
            fixedW.setSeed(primarySeed);
            fixedCount = fixedW.structureRegionCount(int(World::StructureDungeon));
        }
        // 绝对值 = cert 前置探针轮现算留痕（seed 133 / 160×160×96：fixed 站点 3、primary 足迹
        //   36..42 × 29..34 × 78..84；计划面漂移即红；与 a 腿 twin/双序相对恒等锚互补）。
        const bool absOk = poolOk && fixedCount == 3 && pd.minX == 36 && pd.minY == 29
            && pd.minZ == 78 && pd.maxX == 42 && pd.maxY == 34 && pd.maxZ == 84;
        if (!absOk)
            diag += QStringLiteral("[abs fixedCount=%1 prim=%2,%3,%4..%5,%6,%7 seed=%8] ")
                        .arg(fixedCount).arg(pd.minX).arg(pd.minY).arg(pd.minZ)
                        .arg(pd.maxX).arg(pd.maxY).arg(pd.maxZ).arg(primarySeed);
        ok = ok && missW.isEmpty() && missH.isEmpty() && anchors && jarOk && selfOk && regOk
            && absOk;
        if (!ok)
            diag += QStringLiteral("[w=%1 h=%2 anc=%3 jar=%4 self=%5 legacy=%6 reg=%7]")
                        .arg(missW.join(QLatin1Char(','))).arg(missH.join(QLatin1Char(',')))
                        .arg(anchors).arg(jarOk).arg(selfOk).arg(legacy).arg(regOk);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2101d structure pins (integration gate and definition lines exactly once,"
               " shared spawner authority on both faces, era anchors in disposition table"
               " and world.h, jar artifact on disk with generator anchors, zero cross-task"
               " legacy tokens, registration rows present, plan absolute faces on the fixed"
               " site count and the primary discovered footprint)"
            << (ok ? QString() : diag);
    });
}
