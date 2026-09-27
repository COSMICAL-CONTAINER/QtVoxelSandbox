#include "matrix_helpers.h"

#include <QElapsedTimer>

// t1084 纸 / 书 / 书架探针段（4 腿；filter 词 r2055；矩阵 750→754）。置尾先例（接
// section55，runAll 末执行，rig 世界零接触——破坏腿自建 fresh 小世界 48×48×96 s82，
// section11/53/55 同门；真链交互 = t945/t1083 rig 同门（QQuickWindow 载体 + grab/
// loadSavedState/tick 刷射线 + beginMining 泵墙钟））。
//
// 现场核实（按实况定范围，t1070/t1077/t1078/t1080/t1083 降级/盘点先例谱系第七现）：候选
//   「纸 / 书 / 书架」六面中——① 纸 = PaperId 0x237 已在位（t473：3 甘蔗横排 → 3 纸，
//   recipe.cpp paper 行 + 调色板 + 名 + 图标 + pack itemFilenameMap {0x237→paper.png}）；
//   ② 书 = BookId 0x238 已在位（t473：3 纸 + 1 皮革 2×2 → 1 书）；③ 甘蔗 = Sugarcane 53
//   已在位（worldgen placeSugarcane + dropId=自身 → 合成原料可采可入格）；④ 皮革获取链 =
//   LeatherId 0x20D 杀牛掉落（Main.qml onMobDied 牛分支字面量 0x20D）+ fishing 池（loottable
//   皮革 row，垃圾桶 10/1,1）→ 书原料双源齐备；⑤ 书架 = Bookshelf 95 已在位（t474：6 木板 + 3 书配方 /
//   def 行 / t620 per-face 贴图 tile 8·111 / 创造调色板 / kMcBlockId=47 / isBookshelf 谓词 +
//   World::countBookshelvesAround 附魔联动，t795/t823/t873 探针族已钉）；⑥ 附魔系统在位
//   （EnchantRegistry + 书架档位公式 t795 钉）→「书架→附魔强度联动」非缺口，零实现零降级。
//   **唯一缺口 = 破坏掉书**：def 行原 dropId=自身 / dropCount=1（注释自认偏离 MC 1.0
//   「破书架掉书」）→ 本段随附 fix 改 dropId=0x238 / dropCount=3（回收闭环：3 书可 3 纸+1 皮
//   重合、6 木板+3 书重合书架，零净损）。
//   **如实降级登记（零实现）**：kMcMaterialId 表补行（paper 339 / book 340）不做——表界
//   [0x200..0x22E] 单一权威与 docs/item-ids.md 钉死一致（本任务禁碰 docs/），且 pack 文件映射
//   已由 itemFilenameMap {0x237/0x238} 直挂（越界回退自绘面无实际缺口，同 RawFishId 0x231
//   先例）；新方块 id / 图集瓦片追加不做——三项均已在位（id 0x237/0x238/95 非本任务发号；
//   AtlasTileCount=191 已覆盖 tile 111），lawful 无变更面，钉面锁现状。
//
// 腿面：r2055a 合成承重墙（纸命中[中行 + 顶行纵移] / 书命中[2×2 与 3×3 双格] / 书架命中 +
//   原料错误负例：2 甘蔗 / 竖列 / 小麦行产物异分流 / 2×2 拒纳 / 缺皮革 / 镜像摆位 / 9 木板 /
//   中行错纸 + 三配方行源钉唯一）；r2055b 破坏掉书承重墙（真链生存破 → Air + 恰 1 实体
//   0x238 携 3 件 + def 掉落面逐字段[dropId/dropCount 精确钉留在本腿——NEG-1 靶行不入 d 腿，
//   t1083「NEG 靶行不入钉」先例] + 创造破不掉 + 空手也掉落[requiresTool=false 行为柱]）；
//   r2055c 物品面/调色板/源面（0x237/0x238 逐位 + 材料段判定 + isMaterial + 名表面 +
//   调色板纸书连续同列 + 书架方块列 + isBookshelf 谓词 + 甘蔗自掉源面 + 皮革 fishing 池逐位 +
//   牛掉皮革 QML 字面源钉 + pack 映射行源钉）；r2055d 结构钉（def 行逐字段[掉落面只钉相对
//   恒等 dropId ∈ 材料段 + dropCount ≥1] + kMcBlockId 47 行唯一 + 图集 191 容量 + 图标路径 +
//   书架 QML tripwire 字面 + 纸书 id 字面禁入 Main.qml 反探 + 旧掉自身口径禁出反探）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_r2055_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘破坏掉书语义本体（blockregistry.cpp 书架 def 行 dropCount 3→1）→ 声明红面
//     {r2055b}（掉落断言红：实体件数 1≠3 + def 掉落面精确钉红）。r2055a 不误伤（纯配方表
//     查询零掉落依赖）；r2055c 不误伤（调色板 / 名 / 谓词 / 源面零 dropCount 依赖）；r2055d
//     不误伤（def 掉落面只钉相对恒等 dropId=0x238 不动 + dropCount ≥1——t1083 NEG 靶行
//     不入钉先例，精确钉在 b 腿）。
//   NEG-2 摘书架配方语义本体（recipe.cpp bookshelf 行中行 3 书 → 0,0,0）→ 声明红面
//     {r2055a}（书架配方命中断言红：match 返 nullptr）。r2055b 不误伤（掉落读 def 表零配方
//     依赖）；r2055c/d 不误伤（无 bookshelf 配方 pattern 针脚——行源钉只锚输出段 needle，
//     变异不动输出段）。
//
// rig 纪律：fresh 小世界 incantation（section11 四 setter 同款）；rig 位运行期扫描空域
//   （lessons t769/t1030——逐列扫 y-1/y/y+1 三连空）；pc 构造默认 Stone → 显式
//   setSelectedBlock(Air)（t1030 阴性轮兜底旁路教训——材料段 / 空手 rig 必归 Air）；挖掘泵
//   墙钟 pumpMs（t1083 c3 同款——tickImpl dt 吃墙钟间隔，裸连调 tick 不累积）；掉落物断言
//   按 itemId 找活体（槽位 LIFO 复用 t256——禁按生成序断言下标）。

namespace {

// fresh 小世界 incantation（section11 同款四 setter + 恒晴零 RNG）。
inline void initBookshelfWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// rig 空域扫描：列 (x,z) 上首个「y-1 / y / y+1 三连空」高度（书架放 y，玩家同层站位）。
inline int findBookshelfRigY(World &w, int x, int z)
{
    for (int y = 4; y < w.height() - 2; ++y) {
        if (w.blockAt(x, y - 1, z) == BR::Air && w.blockAt(x, y, z) == BR::Air
            && w.blockAt(x, y + 1, z) == BR::Air)
            return y;
    }
    return -1;
}

// 掉落物定位帮手（槽位 LIFO 复用 t256——禁按生成序断言下标，按 itemId 找活体）。
inline int findAliveItemById(ItemEntityManager &items, int itemId)
{
    for (int i = 0; i < items.count(); ++i)
        if (items.aliveAt(i) && items.itemIdAt(i) == itemId) return i;
    return -1;
}

// 挖掘泵墙钟（t1083 pumpMs 同款：processEvents 泵墙钟——tickImpl dt = 墙钟间隔，裸连调不累积）。
inline void pumpMs(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 真链破坏 rig（t1083 JukeboxRig 缩本：fresh 世界 + 空实体族 + 热栏 + 窗载体；书架机制不依赖
//   信号面，ItemEntityManager 仅作掉落产物断言面）。坐标约定：书架在 (bx, y, bz)，玩家脚位
//   (bx+3.5, y, bz+0.5)，瞄书架 +X 侧面 (bx+0.9, y+0.5, bz+0.5)。
struct BookshelfRig
{
    World w;
    ItemEntityManager items;
    Hotbar hb;
    PlayerController pc;
    QQuickWindow win;
    int bx = 8, bz = 8, y = -1;

    BookshelfRig()
    {
        initBookshelfWorld(w);
        y = findBookshelfRigY(w, bx, bz);
        pc.setWorld(&w);
        pc.setHotbar(&hb);
        pc.setItemEntities(&items);
        pc.setSelectedBlock(int(BR::Air)); // t1030 rig 加固：构造默认 Stone → 显式归 Air（空手 /
                                           //   材料段 rig 口径），防分支旁路走通用放置偷放 Stone。
        pc.setParentItem(win.contentItem());
        // 真链掉落接线（Main.qml onSpawnItem 同款消费：PlayerController 发 spawnItem 语义事件 →
        //   ItemEntityManager 落实体——生存挖掘掉落走信号流，rig 不接线则掉落零实体 [t1084 首轮
        //   b1/b3 ents=0 病根]）。
        QObject::connect(&pc, &PlayerController::spawnItem, &pc,
                         [&](int x, int y, int z, int itemId, int count) {
                             items.spawnItem(x, y, z, itemId, count);
                         });
        pc.grab();
    }

    // 场景保真：脚下垫石台面（玩家站位行地坪——防生存 tick 重力坠落 → 视线脱靶 → 挖掘
    //   目标换格进度清零永不破块的 rig 病根，t1083 同门）+ 书架本体。
    void placeBookshelf()
    {
        for (int dx = 0; dx <= 4; ++dx)
            w.setBlock(bx + dx, y - 1, bz, BR::Stone, 0);
        w.setBlock(bx, y, bz, BR::Bookshelf, 0);
    }

    // 瞄准 + tick 刷射线（t1083 aim 同款；先 release+grab 重居中吞掉 pollMouse 残留 delta）。
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

void MatrixRun::section56_paper_book_bookshelf()
{
    // ── r2055a：合成承重墙（纸 / 书 / 书架三配方命中 + 原料错误负例 + 行源钉唯一）──────────
    runLeg("r2055a paper/book/bookshelf recipe wall (3-cane row -> 3 paper incl. row-translation, 3 paper+leather 2x2/3x3 -> 1 book, 6 planks+3 books -> 1 bookshelf; negatives: 2-cane/vertical/wheat-diverts-to-bread/2x2-reject/no-leather/mirrored/9-planks/wrong-mid-row; three recipe-row unique source pins)", [&]() {
        bool ok = true;
        QString diag;
        // (1) 纸：中行 3 甘蔗（3×3 工作台）→ PaperId 恰 3 件；顶行同命中（shapedEqual bbox 纵移等价，
        //     recipe.cpp「顶 / 中 / 底行均可命中」口径行为级双柱）。
        const auto *rp = static_cast<const RecipeRegistry::Recipe *>(nullptr);
        {
            int g[9] = {};
            g[3] = g[4] = g[5] = int(BR::Sugarcane);
            rp = RecipeRegistry::match(g, 3);
        }
        const bool a1 = rp && rp->outputId == RecipeRegistry::PaperId && rp->outputCount == 3;
        ok = ok && a1;
        if (!a1) diag += QStringLiteral("[a1 out=%1 cnt=%2]")
                             .arg(rp ? rp->outputId : -1).arg(rp ? rp->outputCount : 0);
        {
            int g[9] = {};
            g[0] = g[1] = g[2] = int(BR::Sugarcane);
            const auto *rt = RecipeRegistry::match(g, 3);
            const bool a1t = rt && rt->outputId == RecipeRegistry::PaperId && rt->outputCount == 3;
            ok = ok && a1t;
            if (!a1t) diag += QStringLiteral("[a1t]");
        }
        // (2) 纸负例：2 甘蔗（bbox 2×1 → 无配方）/ 竖列 3 甘蔗（1×3 ≠ 3×1 → 无配方）/ 小麦行
        //     （料异 → 产物异分流面包，不吞纸格——比 nullptr 更强的分流钉）/ 2×2 拒纳（一行 3 宽
        //     放不进背包栏，只能工作台合）。
        {
            int g[9] = {};
            g[3] = g[4] = int(BR::Sugarcane);
            const bool a2a = RecipeRegistry::match(g, 3) == nullptr;
            int gv[9] = {};
            gv[1] = gv[4] = gv[7] = int(BR::Sugarcane);
            const bool a2b = RecipeRegistry::match(gv, 3) == nullptr;
            int gw[9] = {};
            gw[3] = gw[4] = gw[5] = RecipeRegistry::WheatId;
            const auto *rw = RecipeRegistry::match(gw, 3);
            const bool a2c = rw && rw->outputId == RecipeRegistry::BreadId;
            int g2[4] = { int(BR::Sugarcane), int(BR::Sugarcane), int(BR::Sugarcane), 0 };
            const bool a2d = RecipeRegistry::match(g2, 2) == nullptr;
            ok = ok && a2a && a2b && a2c && a2d;
            if (!(a2a && a2b && a2c && a2d))
                diag += QStringLiteral("[a2 %1%2%3%4]").arg(a2a).arg(a2b).arg(a2c).arg(a2d);
        }
        // (3) 书：3 纸 + 1 皮革（2×2 左上四格：纸左上/右上/左下 + 皮革右下）→ BookId 1；同 pattern
        //     在 3×3 输入顶左也命中（2×2 配方在 3×3 工作台可合，recipe.h 两阶段第一轮口径）。
        {
            int g2[4] = { RecipeRegistry::PaperId, RecipeRegistry::PaperId,
                          RecipeRegistry::PaperId, RecipeRegistry::LeatherId };
            const auto *rb2 = RecipeRegistry::match(g2, 2);
            const bool a3a = rb2 && rb2->outputId == RecipeRegistry::BookId && rb2->outputCount == 1;
            int g3[9] = {};
            g3[0] = RecipeRegistry::PaperId;  g3[1] = RecipeRegistry::PaperId;
            g3[3] = RecipeRegistry::PaperId;  g3[4] = RecipeRegistry::LeatherId;
            const auto *rb3 = RecipeRegistry::match(g3, 3);
            const bool a3b = rb3 && rb3->outputId == RecipeRegistry::BookId && rb3->outputCount == 1;
            ok = ok && a3a && a3b;
            if (!(a3a && a3b))
                diag += QStringLiteral("[a3 %1%2 out23=%3]")
                            .arg(a3a).arg(a3b).arg(rb2 ? rb2->outputId : -1);
        }
        // (4) 书负例：缺皮革（3 纸 L 形 → 无配方）/ 镜像摆位（皮革左上 → 有序匹配不认，nullptr）。
        {
            int g2[4] = { RecipeRegistry::PaperId, RecipeRegistry::PaperId,
                          RecipeRegistry::PaperId, 0 };
            const bool a4a = RecipeRegistry::match(g2, 2) == nullptr;
            int gm[4] = { RecipeRegistry::PaperId,   RecipeRegistry::PaperId,
                          RecipeRegistry::LeatherId, RecipeRegistry::PaperId };
            const bool a4b = RecipeRegistry::match(gm, 2) == nullptr;
            ok = ok && a4a && a4b;
            if (!(a4a && a4b)) diag += QStringLiteral("[a4 %1%2]").arg(a4a).arg(a4b);
        }
        // (5) 书架：6 木板 + 3 书（上/下两行木板、中间一行 3 书）→ Bookshelf 1。
        {
            int g[9] = {};
            g[0] = g[1] = g[2] = int(BR::Planks);
            g[3] = g[4] = g[5] = RecipeRegistry::BookId;
            g[6] = g[7] = g[8] = int(BR::Planks);
            const auto *rbs = RecipeRegistry::match(g, 3);
            const bool a5 = rbs && rbs->outputId == int(BR::Bookshelf) && rbs->outputCount == 1;
            ok = ok && a5;
            if (!a5) diag += QStringLiteral("[a5 out=%1]")
                                 .arg(rbs ? rbs->outputId : -1);
        }
        // (6) 书架负例：中行错木板（9 木板满铺 → 无配方）/ 中行错纸（6 木板 + 3 纸 → 无配方，
        //     原料错误不吞书架格）。
        {
            int g[9] = {};
            g[0] = g[1] = g[2] = int(BR::Planks);
            g[3] = g[4] = g[5] = int(BR::Planks);
            g[6] = g[7] = g[8] = int(BR::Planks);
            const bool a6a = RecipeRegistry::match(g, 3) == nullptr;
            int gp[9] = {};
            gp[0] = gp[1] = gp[2] = int(BR::Planks);
            gp[3] = gp[4] = gp[5] = RecipeRegistry::PaperId;
            gp[6] = gp[7] = gp[8] = int(BR::Planks);
            const bool a6b = RecipeRegistry::match(gp, 3) == nullptr;
            ok = ok && a6a && a6b;
            if (!(a6a && a6b)) diag += QStringLiteral("[a6 %1%2]").arg(a6a).arg(a6b);
        }
        // (7) 三配方行源钉唯一（recipe.cpp 单一权威恰一处——行 needle 锚输出段，NEG-2 变异
        //     只动 pattern 段不误伤针脚，红面由命中断言承担）。
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile rf(root + QStringLiteral("/src/Game/recipe.cpp"));
        const QString rSrc = rf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(rf.readAll()) : QString();
        const bool a7 = rSrc.count(QStringLiteral("RecipeRegistry::PaperId, 3, 1, \"paper\" }")) == 1
            && rSrc.count(QStringLiteral("RecipeRegistry::BookId, 1, 1, \"book\" }")) == 1
            && rSrc.count(QStringLiteral("int(BlockRegistry::Bookshelf), 1, 1, \"bookshelf\" }")) == 1;
        ok = ok && a7;
        if (!a7) diag += QStringLiteral("[a7 paper=%1 book=%2 bookshelf=%3]")
                             .arg(rSrc.count(QStringLiteral("RecipeRegistry::PaperId, 3, 1, \"paper\" }")))
                             .arg(rSrc.count(QStringLiteral("RecipeRegistry::BookId, 1, 1, \"book\" }")))
                             .arg(rSrc.count(QStringLiteral("int(BlockRegistry::Bookshelf), 1, 1, \"bookshelf\" }")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2055a paper/book/bookshelf recipe wall (3-cane row -> 3 paper incl. row-translation, 3 paper+leather 2x2/3x3 -> 1 book, 6 planks+3 books -> 1 bookshelf; negatives: 2-cane/vertical/wheat-diverts-to-bread/2x2-reject/no-leather/mirrored/9-planks/wrong-mid-row; three recipe-row unique source pins)"
            << (ok ? QString() : diag);
    });

    // ── r2055b：破坏掉书承重墙（真链生存破掉 3 书 / 创造破不掉 / 空手也掉落 + def 掉落面精确钉）──
    runLeg("r2055b bookshelf break-drops-books wall (real-chain survival break -> air + exactly one 0x238 entity carrying 3, def drop-face field-exact [dropId/dropCount pins live HERE - NEG-1 target row kept out of leg d], creative break no-drop, bare-hand also drops [requiresTool=false behavior column], zero self-drop)", [&]() {
        bool ok = true;
        QString diag;
        // (0) def 掉落面逐字段**精确钉**（dropId=0x238 / dropCount=3 / maxStack=64 / requiresTool=false
        //     ——精确值钉住本腿；NEG-1 靶行即此，d 腿只钉相对恒等避免双红，t1083 先例）。
        const auto &d = BR::def(BR::Bookshelf);
        const bool b0 = d.dropId == 0x238 && d.dropCount == 3 && d.maxStack == 64
            && !d.requiresTool;
        ok = ok && b0;
        if (!b0) diag += QStringLiteral("[b0 dropId=%1 dropCount=%2]")
                             .arg(d.dropId).arg(d.dropCount);
        // (1) 真链生存破坏（铁斧加速：hardness 1.5 / speedMul 6 ≈ 0.25s，tick+泵墙钟喂真 dt）：
        //     书架 → Air + 掉落物**恰 1 实体**携 itemId=0x238（书）×3（fresh rig 无他实体 → count==1
        //     同时钉「零自掉」——不掉书架方块自身）。
        {
            BookshelfRig rig;
            const int tgt = rig.y;
            rig.placeBookshelf();
            rig.hb.setStack(0, ToolRegistry::AxeIron, 1); // 铁斧 speedMul 6 → miningTime ≈ 0.25s
            const QVector3D hit = rig.aimAt(2 /*Survival*/);
            rig.pc.beginMining();
            int guard = 0;
            while (rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Bookshelf && guard < 200) {
                rig.pc.tick();  // 挖掘累积（生存；updateMining 真 tick 路径）
                pumpMs(25);     // 泵墙钟喂真 dt（≈25ms/tick × ~10 tick 达成）
                ++guard;
            }
            const int ent = findAliveItemById(rig.items, 0x238);
            const bool b1 = hit == QVector3D(float(rig.bx), float(tgt), float(rig.bz))
                && rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Air
                && rig.items.count() == 1 && ent == 0
                && rig.items.itemIdAt(0) == 0x238 && rig.items.countAt(0) == 3;
            ok = ok && b1;
            if (!b1) diag += QStringLiteral("[b1 hit=%1,%2,%3 id=%4 ents=%5 ent=%6 entId=%7 entCnt=%8 guard=%9]")
                                 .arg(hit.x()).arg(hit.y()).arg(hit.z())
                                 .arg(rig.w.blockAt(rig.bx, tgt, rig.bz))
                                 .arg(rig.items.count()).arg(ent)
                                 .arg(ent >= 0 ? rig.items.itemIdAt(ent) : -1)
                                 .arg(ent >= 0 ? rig.items.countAt(ent) : -1).arg(guard);
        }
        // (2) 创造破坏不掉（drop=false 口径）：瞬破 → 书架 Air + 零掉落实体（同全方块创造不掉落）。
        {
            BookshelfRig rig;
            const int tgt = rig.y;
            rig.placeBookshelf();
            rig.aimAt(1 /*Creative*/);
            rig.pc.beginMining(); // 创造瞬破
            const bool b2 = rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Air
                && rig.items.count() == 0;
            ok = ok && b2;
            if (!b2) diag += QStringLiteral("[b2 id=%1 ents=%2]")
                                 .arg(rig.w.blockAt(rig.bx, tgt, rig.bz)).arg(rig.items.count());
        }
        // (3) 空手也掉落（requiresTool=false 行为柱）：无工具 → 照掉 0x238 ×3（木质空手可采，
        //     斧只给速度加成——def 面的 b0 钉在行为级复核）。
        {
            BookshelfRig rig;
            const int tgt = rig.y;
            rig.placeBookshelf();
            rig.aimAt(2 /*Survival*/); // 空手：热栏零工具
            rig.pc.beginMining();
            int guard = 0;
            while (rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Bookshelf && guard < 240) {
                rig.pc.tick();
                pumpMs(25); // 空手 speedMul 1 → ≈1.5s（~60 tick，guard 有界）
                ++guard;
            }
            const int ent = findAliveItemById(rig.items, 0x238);
            const bool b3 = rig.w.blockAt(rig.bx, tgt, rig.bz) == BR::Air
                && ent >= 0 && rig.items.countAt(ent) == 3;
            ok = ok && b3;
            if (!b3) diag += QStringLiteral("[b3 id=%1 ent=%2 guard=%3]")
                                 .arg(rig.w.blockAt(rig.bx, tgt, rig.bz)).arg(ent).arg(guard);
        }
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2055b bookshelf break-drops-books wall (real-chain survival break -> air + exactly one 0x238 entity carrying 3, def drop-face field-exact [dropId/dropCount pins live HERE - NEG-1 target row kept out of leg d], creative break no-drop, bare-hand also drops [requiresTool=false behavior column], zero self-drop)"
            << (ok ? QString() : diag);
    });

    // ── r2055c：物品面/调色板/源面（id 逐位 + 名表面 + 调色板 + 谓词 + 甘蔗/皮革双源 + pack 映射）──
    runLeg("r2055c item faces / palette / sources wall (0x237/0x238 bit-exact + material-segment + isMaterial, name faces, paper/book adjacent in creative materials + bookshelf in creative blocks, isBookshelf predicate authority, sugarcane self-drop source, leather fishing-pool bit-exact + cow-drop QML literal source pin, pack itemFilenameMap rows)", [&]() {
        bool ok = true;
        QString diag;
        Hotbar hb;
        // (1) id 逐位 + 材料段判定（单一权威常量 + Hotbar isMaterial 分流——材料段基址同源）。
        const bool c1 = RecipeRegistry::PaperId == 0x237 && RecipeRegistry::BookId == 0x238
            && RecipeRegistry::PaperId >= RecipeRegistry::MaterialIdBase
            && RecipeRegistry::BookId >= RecipeRegistry::MaterialIdBase
            && RecipeRegistry::PaperId != RecipeRegistry::BookId
            && RecipeRegistry::BookId != RecipeRegistry::EnchantedBookId // 0x227 附魔书勿混
            && hb.isMaterial(RecipeRegistry::PaperId) && hb.isMaterial(RecipeRegistry::BookId);
        ok = ok && c1;
        if (!c1) diag += QStringLiteral("[c1]");
        // (2) 名表面（漏名 = 调色板无 tooltip，t728 先例病）：纸 / 书 / 书架 / 皮革四名逐字。
        const bool c2 = hb.nameForBlock(RecipeRegistry::PaperId) == QStringLiteral("纸")
            && hb.nameForBlock(RecipeRegistry::BookId) == QStringLiteral("书")
            && hb.nameForBlock(int(BR::Bookshelf)) == QStringLiteral("书架")
            && hb.nameForBlock(RecipeRegistry::LeatherId) == QStringLiteral("皮革");
        ok = ok && c2;
        if (!c2) diag += QStringLiteral("[c2 paper=%1 book=%2 shelf=%3 leather=%4]")
                             .arg(hb.nameForBlock(RecipeRegistry::PaperId))
                             .arg(hb.nameForBlock(RecipeRegistry::BookId))
                             .arg(hb.nameForBlock(int(BR::Bookshelf)))
                             .arg(hb.nameForBlock(RecipeRegistry::LeatherId));
        // (3) 调色板：纸 / 书连续同列于 creativeMaterials（t473 相邻入列）；书架在 creativeBlocks
        //     （可取用 / 可放置）。
        const QList<QVariant> mats = hb.creativeMaterials();
        int posP = -1;
        for (int i = 0; i < mats.size(); ++i)
            if (mats[i].toInt() == RecipeRegistry::PaperId) { posP = i; break; }
        const bool c3 = posP >= 0 && posP + 1 < mats.size()
            && mats[posP + 1].toInt() == RecipeRegistry::BookId;
        bool c3b = false;
        const QList<QVariant> blocks = hb.creativeBlocks();
        for (int i = 0; i < blocks.size(); ++i)
            if (blocks[i].toInt() == int(BR::Bookshelf)) { c3b = true; break; }
        ok = ok && c3 && c3b;
        if (!(c3 && c3b)) diag += QStringLiteral("[c3 pos=%1 n=%2 shelfInBlocks=%3]")
                                      .arg(posP).arg(mats.size()).arg(c3b);
        // (4) 谓词单一权威：isBookshelf(Bookshelf)=true / 对照方块 false（World::countBookshelvesAround
        //     唯一判定口，t795/t823 消费面）。
        const bool c4 = BR::isBookshelf(BR::Bookshelf) && !BR::isBookshelf(BR::Planks)
            && !BR::isBookshelf(BR::Air);
        ok = ok && c4;
        if (!c4) diag += QStringLiteral("[c4]");
        // (5) 源面一：甘蔗自掉（dropId=自身 → 破甘蔗掉甘蔗方块可入合成格——纸配方的原料获取闭环）。
        const auto &sc = BR::def(BR::Sugarcane);
        const bool c5 = sc.dropId == int(BR::Sugarcane) && sc.dropCount >= 1;
        ok = ok && c5;
        if (!c5) diag += QStringLiteral("[c5 dropId=%1]").arg(sc.dropId);
        // (6) 源面二：皮革双源——fishing 池皮革 row 逐位（垃圾桶 weight 10 / 1,1）+ 杀牛掉皮革
        //     QML 字面源钉（Main.qml onMobDied 牛分支 0x20D，recipe.h 单一权威同源）。
        bool c6 = false;
        for (const auto &e : LootTable::fishingPool())
            if (e.itemId == RecipeRegistry::LeatherId)
                c6 = e.weight == 10 && e.minCount == 1 && e.maxCount == 1;
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString mSrc = mf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mf.readAll()) : QString();
        const bool c6b = mSrc.count(QStringLiteral("0x20D")) >= 1;
        ok = ok && c6 && c6b;
        if (!(c6 && c6b)) diag += QStringLiteral("[c6 pool=%1 qml=%2]").arg(c6).arg(c6b);
        // (7) pack 映射行源钉（resourcepackmanager itemFilenameMap 恰一行：0x237→paper.png /
        //     0x238→book.png——资源包面直挂，越界 kMcMaterialId 回退面无实际缺口）。
        QFile pf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
        const QString pSrc = pf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(pf.readAll()) : QString();
        const bool c7 = pSrc.count(QStringLiteral("{0x237, QStringLiteral(\"paper.png\")}")) == 1
            && pSrc.count(QStringLiteral("{0x238, QStringLiteral(\"book.png\")}")) == 1;
        ok = ok && c7;
        if (!c7) diag += QStringLiteral("[c7 paper=%1 book=%2]")
                             .arg(pSrc.count(QStringLiteral("{0x237, QStringLiteral(\"paper.png\")}")))
                             .arg(pSrc.count(QStringLiteral("{0x238, QStringLiteral(\"book.png\")}")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2055c item faces / palette / sources wall (0x237/0x238 bit-exact + material-segment + isMaterial, name faces, paper/book adjacent in creative materials + bookshelf in creative blocks, isBookshelf predicate authority, sugarcane self-drop source, leather fishing-pool bit-exact + cow-drop QML literal source pin, pack itemFilenameMap rows)"
            << (ok ? QString() : diag);
    });

    // ── r2055d：结构钉（def 行逐字段[掉落面相对恒等] / kMcBlockId 47 / 图集 191 / 图标 / QML
    //    tripwire 字面 / 纸书 id 禁入 Main.qml 反探 / 旧掉自身口径禁出反探）────────────────────
    runLeg("r2055d structure pins (def row field-exact except drop-face relative identity [dropId in material segment + dropCount >= 1 - NEG-1 target excluded], kMcBlockId 47 row unique, atlas 191 capacity, icon path, bookshelf QML tripwire literals, paper/book id literals barred from Main.qml anti-probe, old drop-self wording barred anti-probe)", [&]() {
        bool ok = true;
        QString diag;
        // (1) def 行逐字段（kDefs 单一权威——破坏 / 音色 / 贴图 / 谓词面全读表）。掉落面只钉相对
        //     恒等：dropId 落材料段（破书架掉**非方块**书面）+ dropCount ≥1——NEG-1 靶字段
        //     （dropCount 精确值）不入本腿，精确钉在 r2055b（t1083 NEG 靶行不入钉先例）。
        const auto &d = BR::def(BR::Bookshelf);
        const bool d1 = d.solid && d.shape == BR::ShapeFull
            && d.hardness == 1.5f && d.toolType == int(BR::Axe)
            && d.dropId >= RecipeRegistry::MaterialIdBase && d.dropCount >= 1
            && d.maxStack == 64
            && d.topTile == 8 && d.bottomTile == 8 && d.sideTile == 111 && d.frontTile == 111
            && BR::materialGroup(BR::Bookshelf) == BR::GroupWood
            && BR::isBookshelf(BR::Bookshelf);
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1]");
        // (2) kMcBlockId 行（MC 1.0 bookshelf = 47 真实存在——迁移文档面）+ 行唯一源钉 +
        //     图集容量（191 > 111 覆盖，lawful 无变更面锁现状）。
        const QString root = QDir(QCoreApplication::applicationDirPath()
                                  + QStringLiteral("/..")).absolutePath();
        QFile bf(root + QStringLiteral("/src/Core/blockregistry.cpp"));
        const QString bSrc = bf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(bf.readAll()) : QString();
        const bool d2 = BR::mcBlockId(BR::Bookshelf) == 47
            && BR::AtlasTileCount == 207 && d.frontTile < BR::AtlasTileCount // t1105 lawful 前移：201→207（南瓜族 + 炼药锅 tile 追加；t1103 曾 195→201、t1097 曾 193→195）
            && bSrc.count(QStringLiteral("/* bookshelf               */ 47")) == 1;
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2 mc=%1 atlas=%2 row=%3]")
                             .arg(BR::mcBlockId(BR::Bookshelf)).arg(BR::AtlasTileCount)
                             .arg(bSrc.count(QStringLiteral("/* bookshelf               */ 47")));
        // (3) 掉落面源钉（行内 dropId=0x238 字面恰一处——Core 不依赖 Game 字面量纪律）+ 图标
        //     路径源钉（hotbar.cpp 书架立方图标恰一处）。
        QFile hf(root + QStringLiteral("/src/Game/hotbar.cpp"));
        const QString hSrc = hf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(hf.readAll()) : QString();
        const bool d3 = bSrc.count(QStringLiteral(", false, 0x238,")) == 1
            && hSrc.count(QStringLiteral("return \"icon_bookshelf.png\";")) == 1;
        ok = ok && d3;
        if (!d3) diag += QStringLiteral("[d3 lit=%1 icon=%2]")
                             .arg(bSrc.count(QStringLiteral(", false, 0x238,")))
                             .arg(hSrc.count(QStringLiteral("return \"icon_bookshelf.png\";")));
        // (4) QML tripwire 字面（EnchantRunes / EnchantGlyphFlow 的 !==95 书架 id 契约——recipe.cpp
        //     static_assert 的 QML 侧锚，id 迁移必红的面）+ 纸 / 书 id 字面禁入 Main.qml 反探
        //     （材料段物品路由全在 C++ / 面板 QML，主场景文件零 id 字面——零状态机入呈现层）。
        QFile er(root + QStringLiteral("/src/ui/EnchantRunes.qml"));
        const QString erSrc = er.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(er.readAll()) : QString();
        QFile eg(root + QStringLiteral("/src/ui/EnchantGlyphFlow.qml"));
        const QString egSrc = eg.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(eg.readAll()) : QString();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString mSrc = mf.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mf.readAll()) : QString();
        const bool d4 = erSrc.count(QStringLiteral("!== 95")) == 1
            && egSrc.count(QStringLiteral("!== 95")) == 1
            && !mSrc.contains(QStringLiteral("0x237"))
            && !mSrc.contains(QStringLiteral("0x238"));
        ok = ok && d4;
        if (!d4) diag += QStringLiteral("[d4 er=%1 eg=%2 m237=%3 m238=%4]")
                             .arg(erSrc.count(QStringLiteral("!== 95")))
                             .arg(egSrc.count(QStringLiteral("!== 95")))
                             .arg(mSrc.count(QStringLiteral("0x237")))
                             .arg(mSrc.count(QStringLiteral("0x238")));
        // (5) 旧「掉书架自身以便回收重放」口径禁出反探（blockregistry 注释与行同步改净——
        //     双文件旧措辞残留即红，防「行为改了注释仍教人旧语义」漂移）。
        QFile bh(root + QStringLiteral("/src/Core/blockregistry.h"));
        const QString bhSrc = bh.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(bh.readAll()) : QString();
        const bool d5 = !bSrc.contains(QStringLiteral("回收重放"))
            && !bhSrc.contains(QStringLiteral("回收重放"))
            && bSrc.contains(QStringLiteral("t1084 破坏掉书"))
            && bhSrc.contains(QStringLiteral("t1084 破坏掉书"));
        ok = ok && d5;
        if (!d5) diag += QStringLiteral("[d5 oldCpp=%1 oldHdr=%2 newCpp=%3 newHdr=%4]")
                             .arg(bSrc.contains(QStringLiteral("回收重放")))
                             .arg(bhSrc.contains(QStringLiteral("回收重放")))
                             .arg(bSrc.contains(QStringLiteral("t1084 破坏掉书")))
                             .arg(bhSrc.contains(QStringLiteral("t1084 破坏掉书")));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2055d structure pins (def row field-exact except drop-face relative identity [dropId in material segment + dropCount >= 1 - NEG-1 target excluded], kMcBlockId 47 row unique, atlas 191 capacity, icon path, bookshelf QML tripwire literals, paper/book id literals barred from Main.qml anti-probe, old drop-self wording barred anti-probe)"
            << (ok ? QString() : diag);
    });
}
