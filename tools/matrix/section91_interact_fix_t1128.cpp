#include "matrix_helpers.h"

#include "brewingstore.h" // t1128 件四酿造身份门探针（Entry.activeIngredient 直读源）

// t1128 交互正确性批探针段(6 腿;filter 词 r2098;矩阵 924→930)。置尾先例沿用(接 section90,
//   runAll 末执行,rig 世界零接触——行为腿自建 fresh 小世界 + 真链 pc / 合成表 / 酿造台 rig,
//   结构钉族纯源钉)。
//
// ── 交付面(用户第三方只读审查五件逐一清偿;era 定谳工件 build/t1128_jar_crafting_map_sign.txt
//    〔sl=CraftingManager 地图+牌子两配方原文〕/ build/t1128_jar_brewing_tile.txt〔tt=TileEntity
//    BrewingStand 全文 + abk=Item.potion maxStack + bq mappings〕留痕)─────────────────────────
//   件一 MAP-01(空地图激活吞栈 P1):旧 setStack(选中槽, FilledMap, 1) 整栈替换——持 N>1 张激活丢
//     N-1 张。新面 = **只耗 1 张**(takeStack 选中槽 1 件,归 0 清槽) + 产物**入背包**(addStack 智能
//     放置权威:同 id 合并 → 空槽)+ canFitStack 容量探针守卫(腾手 credit 仅生存持 1 张时计入)——
//     背包满 → **保守拒绝激活**(era 无空地图物品场景不可考,保守面留痕:禁物品凭空消失)。创造面 =
//     不耗(takeStack 不执行,创造资源免扣同全物品族)+ 产物照给(era「持手即绘」的两形态工程承载)。
//     预期总量独立计算(1/2/64 三档字面量 + 恰耗 1 行为规格,零实现参数推导)。
//   件二 CRAFT-01(地图配方勘误):era 定谳 sl 偏移 3187..3248 = 样板 "###/#X#/###" + #=acy.aJ
//     (paper 339) + X=acy.aP(compass 345) → 1× acy.bc(map 358)——**罗盘芯 1.0.0 在册**,工程罗盘
//     在场(CompassId 0x23F)→ 交付 era 全形;旧 6 纸两列形(注释/腿文/账面三方打架源)勘误撤销。
//     腿期望网格独立抄自 jar 工件非旧实现;负例族(缺纸/缺心/错心/多料全拒)+ 2×2 不命中 3×3 行
//     + 消耗断言(consumeCount 恰 1)。
//   件三 CRAFT-02(牌子配方补回):era 定谳 sl 偏移 1215..1276 = 样板 "###/###/ X" + #=yy.x
//     (wood 木板) + X=acy.C(stick 280) → 1× acy.at(sign 323,产量 iconst_1——historic「产 3」=
//     1.3/12w18a 越基线)——**底行中列 1 木棍在册**,与活板门(6 板 2×3 无棍)不同形,t1113「模式
//     全等→禁遮蔽不可共存」旧论证撤回,配方段尾追加补回。全回路腿 = 合成→放置→写四行→存→
//     重进→破坏掉落一条真链。
//   件四 BREW-01(酿造原料身份):era tt.b() 更新循环 = 开酿点 k=原料 item id 缓存 + b>0 中
//     k≠id → b=0(偏移 54..78)——era 有「本轮原料 id 比对、换 id 即进度清零重开」面,工程旧态
//     无比对 → 换料继承旧进度出错误产物。新面 = Entry.activeIngredient(会话态不入存档——era k
//     不在 NBT,重载首拍复位字节等价)+ 扫描身份门(同 id 增减数量不清;移除/无效料走既有 eligible
//     复位;插回同 id 恒新酿)。
//   件五 BREW-02(酿造燃料 era 定谳,零代码收口):tt 字段全集零燃料面(零字段/零槽/零消耗路径/
//     NBT 仅 BrewTime+Items;tt 存档名借用 "Cauldron" era 怪癖)→ 1.0.0 酿造**无燃料**——工程
//     燃烬粉燃料面「机制等价 MC 1.0」标注失真(fuel = 1.2 期后加面)+ 同读发现 BrewTime=600
//     ticks(30s,工程 20s = 400 ticks 后期值)→ **纪元勘正候选登记本单不修**(证据注留档
//     brewingstore.h 头 + playercontroller.cpp 扫描头)。附带核(瓶槽堆叠):era 药水 maxStack=1
//     (abk 构造体 iconst_1 h(I))→ era 槽内恒 1 件、1 原料 ≤3 瓶;工程 64 栈 + 整栈转换 = 放大
//     偏差 → 候选池登记(注释勘正)。
//
// ── NEG 面与豁免设计(恰红归因先于腿文;双摘面互不重叠;双 NEG 均编译仍绿形)──────────────────
//   NEG-1 = 摘 playercontroller.cpp scanBrewingStands 原料身份门**代码块**(「{ const int
//     recordedIng = ... }」braced 块整块移除——块外 progBase 声明行原地幸存 = progBase 退化 prog0
//     旧行为,编译仍绿零告警;块上注释体幸存)→ 恰红 = 单元 {r2098e}(换料柱近满换料场景断言失
//     [错误产物 Speed + 秒完] + 重载复位场景断言失[旧进度扛过重载];同 id 数量/移除/插回/无效料
//     四场景走既有路径在 NEG-1 下行为不变——归因唯一)。f 豁免(f 所钉 = 块外注释 raw 锚 / store
//     成员行 / era 工件锚——均不在摘块内)。
//   NEG-2 = 摘 playercontroller.cpp placeBlock 空地图分支的激活消耗行(m_hotbar->takeStack(
//     选中槽, 1) 单语句行删除,编译仍绿——分支体余行[探针守卫 / addStack / 建库]完整)→ 恰红 =
//     单元 {r2098a}(激活四场景柱生存消耗断言全失:持 1 张场景槽 0 恒 EmptyMapId ≠ FilledMapId、
//     持 2/64 张余量断言多 1、连续激活 filled 计数缺 1、拒绝场景等——创造断言不受触达[消耗行
//     创造本就不执行]但腿级 FAIL 归属 a)。f 豁免(f 不含该语句行任何形态的源钉;分支头行与
//     注释 raw 锚幸存)。
//   (双摘面互不重叠:NEG-1 在酿造扫描、NEG-2 在地图激活,文件同域函数异域;e/f 对 NEG-2 幸存
//     [零激活断言],a/b/c/d 对 NEG-1 幸存[零酿造断言]。)

namespace {

// fixed 宿主小世界 incantation(section90 同款四 setter;seed 95..97 与既族解耦)。
inline void initInteractWorld(World &w, int seed)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(seed);
}

// 石坪铺装 + 上空清空(坪 y=80 闭区间,上空 y 81..92 清 Air;section90 同款)。
inline void layInteractPlatform(World &w, int px0, int px1, int pz0, int pz1)
{
    for (int x = px0; x <= px1; ++x)
        for (int z = pz0; z <= pz1; ++z) {
            w.setBlock(x, 80, z, BR::Stone, 0);
            for (int y = 81; y <= 92; ++y)
                w.setBlock(x, y, z, BR::Air, 0);
        }
}

// 源钉根路径(section90 同款:applicationDirPath/../src)。
inline QString srcRootForInteractPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 原始读含(注释体锚 / 摘面行——pinSet 剥注释会失配,section90 同款)。
inline bool rawContainsInteract(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

// 激活放置 CD 泵(section77 同款 processEvents 墙钟泵)。
inline void pumpInteract(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// 背包九槽总量独立计数(期望侧自算口径:按 id 数件,零实现参数)。
inline int countAcrossSlots(const Hotbar &hb, int id)
{
    int n = 0;
    for (int i = 0; i < 9; ++i)
        if (hb.blockIdAt(i) == id)
            n += hb.countAt(i);
    return n;
}

// 保存边界快照(Main.qml gatherPlayerState hotbar 段同键:id/count 九槽逐格)。
inline QVariantList snapshotInventory(const Hotbar &hb)
{
    QVariantList out;
    for (int i = 0; i < 9; ++i) {
        QVariantMap s;
        s.insert(QStringLiteral("id"), hb.blockIdAt(i));
        s.insert(QStringLiteral("count"), hb.countAt(i));
        out.append(s);
    }
    return out;
}

// 保存边界回填(Main.qml applyPlayerState setStack 同键逐格写回)。
inline void restoreInventory(Hotbar &hb, const QVariantList &snap)
{
    for (int i = 0; i < 9 && i < snap.size(); ++i) {
        const QVariantMap s = snap.at(i).toMap();
        hb.setStack(i, s.value(QStringLiteral("id")).toInt(),
                    s.value(QStringLiteral("count")).toInt());
    }
}

} // namespace

void MatrixRun::section91_interact_fix_t1128()
{
    // ── r2098a:激活四场景柱(NEG-2 敏感面 = 激活消耗行)──────────────────────────────────────
    //   数量三档(1/2/64 独立字面预期)+ 产物放置三态(腾手入原槽 / 空槽开新 / 满背包拒绝)+
    //   连续激活(恰耗 1/次)+ 切槽守卫(空槽零激活)+ 创造面(不耗 + 产物照给)+ 保存边界
    //   round-trip 数量一致(gatherPlayerState/applyPlayerState 同键快照回填)。
    runLeg("r2098a activation scenario column (survival activation of one held empty map"
        " consumes exactly that one and the filled map lands in the freed hand slot, two or"
        " sixty four held empties keep the remainder in hand with one filled map opening an"
        " empty slot, a full inventory with two held empties refuses the activation leaving"
        " every stack untouched, a full inventory with one held empty still converts through"
        " the freed hand, a creative holder keeps the palette empty map unconsumed while one"
        " filled map lands in the inventory, consecutive activations consume one each, an"
        " empty selected slot never activates, and the save boundary round trip restores the"
        " exact slot ids and counts)", [&]() {
        bool ok = true;
        QString diag;
        const int EM = int(RecipeRegistry::EmptyMapId);
        const int FM = int(RecipeRegistry::FilledMapId);
        // 场景 ①:生存持 1 张 + 背包余空 → 恰耗 1,腾手槽收产物。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 1, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            // 独立预期:恰耗 1 → 空地图余 0;产物恰 1;腾手槽(槽 0)收产物。
            const bool s1 = countAcrossSlots(hb, EM) == 0 && countAcrossSlots(hb, FM) == 1
                && hb.blockIdAt(0) == FM && hb.countAt(0) == 1 && ms.hasMap();
            ok = ok && s1;
            if (!s1) diag += QStringLiteral("[s1 e=%1 f=%2 id0=%3 n0=%4 map=%5]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM))
                .arg(hb.blockIdAt(0)).arg(hb.countAt(0)).arg(ms.hasMap());
        }
        // 场景 ②:生存持 2 张 → 余 1 张在手持槽 + 产物 1 张入空槽。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 2, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool s2 = countAcrossSlots(hb, EM) == 1 && countAcrossSlots(hb, FM) == 1
                && hb.blockIdAt(0) == EM && hb.countAt(0) == 1
                && hb.blockIdAt(1) == FM && hb.countAt(1) == 1;
            ok = ok && s2;
            if (!s2) diag += QStringLiteral("[s2 e=%1 f=%2]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM));
        }
        // 场景 ③:生存持 64 张 → 余 63 + 产物 1。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 64, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool s3 = countAcrossSlots(hb, EM) == 63 && countAcrossSlots(hb, FM) == 1
                && hb.countAt(0) == 63;
            ok = ok && s3;
            if (!s3) diag += QStringLiteral("[s3 e=%1 f=%2 n0=%3]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM)).arg(hb.countAt(0));
        }
        // 场景 ④:背包满(槽 1..8 石 ×64)+ 持 2 张 → 保守拒绝,全背包零动。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 2, 0);
            for (int i = 1; i < 9; ++i)
                hb.setStack(i, int(BR::Stone), 64, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool s4 = countAcrossSlots(hb, EM) == 2 && countAcrossSlots(hb, FM) == 0
                && hb.countAt(0) == 2 && hb.blockIdAt(1) == int(BR::Stone);
            ok = ok && s4;
            if (!s4) diag += QStringLiteral("[s4 e=%1 f=%2]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM));
        }
        // 场景 ⑤:背包满 + 持恰 1 张 → 腾手 credit 收产物(槽 0 转换)。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 1, 0);
            for (int i = 1; i < 9; ++i)
                hb.setStack(i, int(BR::Stone), 64, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool s5 = countAcrossSlots(hb, EM) == 0 && countAcrossSlots(hb, FM) == 1
                && hb.blockIdAt(0) == FM && hb.countAt(0) == 1
                && hb.blockIdAt(1) == int(BR::Stone);
            ok = ok && s5;
            if (!s5) diag += QStringLiteral("[s5 e=%1 f=%2 id0=%3]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM)).arg(hb.blockIdAt(0));
        }
        // 场景 ⑥:创造持 1 张(调色板口径) → 不耗 + 产物照给(手持空地图原样 + 空槽收填充图)。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 1, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 1 /* Creative */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool s6 = hb.blockIdAt(0) == EM && hb.countAt(0) == 1
                && countAcrossSlots(hb, FM) == 1 && hb.blockIdAt(1) == FM && hb.countAt(1) == 1
                && ms.hasMap();
            ok = ok && s6;
            if (!s6) diag += QStringLiteral("[s6 id0=%1 n0=%2 f=%3]")
                .arg(hb.blockIdAt(0)).arg(hb.countAt(0)).arg(countAcrossSlots(hb, FM));
        }
        // 场景 ⑦:连续激活恰耗 1/次(3 张 → 两次激活 → 余 1 + 填充图 2[同 id 并栈])。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 3, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            pumpInteract(320); // 越放置 CD 二次激活
            pc.placeBlock();
            const bool s7 = countAcrossSlots(hb, EM) == 1 && countAcrossSlots(hb, FM) == 2
                && hb.countAt(1) == 2; // 产物同 id 并栈一槽
            ok = ok && s7;
            if (!s7) diag += QStringLiteral("[s7 e=%1 f=%2 n1=%3]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM)).arg(hb.countAt(1));
        }
        // 场景 ⑧:切空槽零激活(选中槽非空地图 → 守卫短路零消耗零产物)。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 2, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            hb.setSelectedSlot(5); // 切到空槽(非空地图手持)
            pc.placeBlock();
            const bool s8 = countAcrossSlots(hb, EM) == 2 && countAcrossSlots(hb, FM) == 0;
            ok = ok && s8;
            if (!s8) diag += QStringLiteral("[s8 e=%1 f=%2]")
                .arg(countAcrossSlots(hb, EM)).arg(countAcrossSlots(hb, FM));
        }
        // 场景 ⑨:保存边界 round-trip(场景 ② 终态快照 → 新 Hotbar 回填 → 逐格一致)。
        {
            World w; initInteractWorld(w, 95);
            layInteractPlatform(w, 4, 44, 4, 44);
            EntityManager ents; Hotbar hb; MapStore ms; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb); pc.setMapStore(&ms);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            hb.setStack(0, EM, 2, 0);
            pc.setSelectedBlock(int(BR::Air));
            pc.loadSavedState(24.5, 81.0, 24.5, 0.0, -45.0, 2 /* Survival */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const QVariantList snap = snapshotInventory(hb);
            Hotbar hb2;
            restoreInventory(hb2, snap);
            bool same = true;
            for (int i = 0; i < 9; ++i)
                same = same && hb2.blockIdAt(i) == hb.blockIdAt(i) && hb2.countAt(i) == hb.countAt(i);
            const bool s9 = same && countAcrossSlots(hb2, EM) == 1 && countAcrossSlots(hb2, FM) == 1;
            ok = ok && s9;
            if (!s9) diag += QStringLiteral("[s9]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098a activation scenario column (survival activation of one held empty map"
               " consumes exactly that one and the filled map lands in the freed hand slot, two"
               " or sixty four held empties keep the remainder in hand with one filled map"
               " opening an empty slot, a full inventory with two held empties refuses the"
               " activation leaving every stack untouched, a full inventory with one held empty"
               " still converts through the freed hand, a creative holder keeps the palette"
               " empty map unconsumed while one filled map lands in the inventory, consecutive"
               " activations consume one each, an empty selected slot never activates, and the"
               " save boundary round trip restores the exact slot ids and counts)"
            << (ok ? QString() : diag);
    });

    // ── r2098b:配方勘误柱(件二;期望网格独立抄自 jar 工件)──────────────────────────────────
    //   era 网格命中(罗盘居中 8 纸环 → 1 空地图)+ 旧 6 纸两列形勘误撤销钉(恒不匹配)+ 负例族
    //   (缺纸/缺心/错心/多料全拒)+ 2×2 背包网格不命中 3×3 行(双口径:静态 match + recipeMatch
    //   桥)+ 消耗断言(consumeCount 恰 1)+ 环孪生零污染回归。
    runLeg("r2098b map recipe erratum column (the compass centered eight paper grid answers one"
        " empty map per the jar read while the withdrawn six paper two column shape answers"
        " nothing, missing paper or missing center or wrong center or an extra material all"
        " answer nothing, the two by two inventory grid never reaches the three by three row"
        " through both the static matcher and the bridge, the per cell consumption answers"
        " exactly one, and the furnace and painting ring twins keep their own answers)", [&]() {
        bool ok = true;
        QString diag;
        const int PA = int(RecipeRegistry::PaperId);
        const int CP = int(RecipeRegistry::CompassId);
        const int CO = int(BR::Cobble);
        const int ST = int(RecipeRegistry::StickId);
        const int WO = int(BR::Wool);
        // (1) era 网格命中(jar 工件样板 "###/#X#/###" #=paper X=compass → 1 空地图)。
        const int gEra[9] = { PA, PA, PA, PA, CP, PA, PA, PA, PA };
        const auto *rEra = RecipeRegistry::match(gEra, 3);
        const bool eraOk = rEra && rEra->outputId == int(RecipeRegistry::EmptyMapId)
            && rEra->outputCount == 1 && rEra->consumeCount == 1;
        ok = ok && eraOk;
        if (!eraOk) diag += QStringLiteral("[era %1]").arg(rEra ? rEra->outputId : -1);
        // (2) 勘误撤销钉:旧 6 纸两列形恒不匹配。
        const int gOld[9] = { PA, 0,  PA, PA, 0, PA, PA, 0,  PA };
        const bool oldGone = RecipeRegistry::match(gOld, 3) == nullptr;
        ok = ok && oldGone;
        if (!oldGone) diag += QStringLiteral("[old]");
        // (3) 负例族:缺纸(角缺)/ 缺心(纯 8 纸环)/ 错心(石心)/ 多料(环内混棒)全拒。
        const int gNoPaper[9]  = { 0,  PA, PA, PA, CP, PA, PA, PA, PA };
        const int gNoCenter[9] = { PA, PA, PA, PA, 0,  PA, PA, PA, PA };
        const int gWrongCore[9] = { PA, PA, PA, PA, CO, PA, PA, PA, PA };
        const int gExtra[9]    = { PA, PA, ST, PA, CP, PA, PA, PA, PA };
        const bool negOk = RecipeRegistry::match(gNoPaper, 3) == nullptr
            && RecipeRegistry::match(gNoCenter, 3) == nullptr
            && RecipeRegistry::match(gWrongCore, 3) == nullptr
            && RecipeRegistry::match(gExtra, 3) == nullptr;
        ok = ok && negOk;
        if (!negOk) diag += QStringLiteral("[neg]");
        // (3b) 石心负例不误伤:8 圆石环仍答熔炉(同形异料各行其答)。
        const int gFurn[9] = { CO, CO, CO, CO, 0, CO, CO, CO, CO };
        const auto *rFurn = RecipeRegistry::match(gFurn, 3);
        const bool furnOk = rFurn && rFurn->outputId == int(BR::Furnace);
        ok = ok && furnOk;
        if (!furnOk) diag += QStringLiteral("[furn]");
        // (4) 2×2 背包网格不命中 3×3 行(静态 match 口径:gridSize 2 只看四格,罗盘+8 纸需九格)。
        const int g2Full[4] = { PA, PA, PA, PA };
        const bool twoOk = RecipeRegistry::match(g2Full, 2) == nullptr;
        ok = ok && twoOk;
        if (!twoOk) diag += QStringLiteral("[two]");
        // (5) recipeMatch 桥同面(QML 合成入口透传权威):era 网格命中 + 2×2 空答 + 消耗数。
        Hotbar hb;
        QVariantList bridgeEra;
        for (int i = 0; i < 9; ++i) bridgeEra.append(QVariant(gEra[i]));
        const QVariantMap mEra = hb.recipeMatch(bridgeEra, 3);
        QVariantList bridgeTwo;
        for (int i = 0; i < 4; ++i) bridgeTwo.append(QVariant(g2Full[i]));
        const QVariantMap mTwo = hb.recipeMatch(bridgeTwo, 2);
        const bool bridgeOk = mEra.value(QStringLiteral("outputId")).toInt() == int(RecipeRegistry::EmptyMapId)
            && mEra.value(QStringLiteral("outputCount")).toInt() == 1
            && mEra.value(QStringLiteral("consumeCount")).toInt() == 1
            && mTwo.isEmpty();
        ok = ok && bridgeOk;
        if (!bridgeOk) diag += QStringLiteral("[bridge]");
        // (6) 环孪生零污染回归(画作环各行其答;罗盘原料在场钉 + 调色板在册)。
        const int gPaint[9] = { ST, ST, ST, ST, WO, ST, ST, ST, ST };
        const auto *rPaint = RecipeRegistry::match(gPaint, 3);
        const QVariantList pal = hb.creativeMaterials();
        const bool twinOk = rPaint && rPaint->outputId == int(RecipeRegistry::PaintingId)
            && int(RecipeRegistry::CompassId) == 0x23F
            && pal.contains(QVariant(int(RecipeRegistry::CompassId)));
        ok = ok && twinOk;
        if (!twinOk) diag += QStringLiteral("[twin]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098b map recipe erratum column (the compass centered eight paper grid answers"
               " one empty map per the jar read while the withdrawn six paper two column shape"
               " answers nothing, missing paper or missing center or wrong center or an extra"
               " material all answer nothing, the two by two inventory grid never reaches the"
               " three by three row through both the static matcher and the bridge, the per cell"
               " consumption answers exactly one, and the furnace and painting ring twins keep"
               " their own answers)"
            << (ok ? QString() : diag);
    });

    // ── r2098c:牌子合成柱(件三配方面;era 网格独立抄自 jar 工件)─────────────────────────────
    //   era 网格命中(顶两行 6 板 + 底行中列棍 → 1 站牌)+ 活板门回归(6 板 2×3 仍答活板门 ×2
    //   ——勘误不扰邻,r2083a 反证钉语义复核)+ 门孪生各行其答 + 错位棍负例(底左棍不匹配)+
    //   全表在册钉(recipeAt 扫描 standing_sign 行恰一 + 自身 pattern 自答回指针 = 遮蔽审计点)+
    //   板材族回退(云杉板同合橡木牌)。
    runLeg("r2098c sign recipe column (the six planks over a center bottom stick answers one"
        " standing sign per the jar read while the six plank two by three grid still answers"
        " the trapdoor pair and the wood door twin keeps its own answer, a misplaced bottom"
        " left stick answers nothing, the sign row is registered exactly once and answers its"
        " own pattern back, and the spruce plank family fallback crafts the same sign)", [&]() {
        bool ok = true;
        QString diag;
        const int PL = int(BR::Planks);
        const int SP = int(BR::SprucePlanks);
        const int STI = int(RecipeRegistry::StickId);
        // (1) era 网格命中:6 板(顶两行)+ 底行中列棍 → 1 站牌。
        const int gSign[9] = { PL, PL, PL, PL, PL, PL, 0, STI, 0 };
        const auto *rSign = RecipeRegistry::match(gSign, 3);
        const bool signOk = rSign && rSign->outputId == int(BR::StandingSign)
            && rSign->outputCount == 1 && rSign->consumeCount == 1;
        ok = ok && signOk;
        if (!signOk) diag += QStringLiteral("[sign %1]").arg(rSign ? rSign->outputId : -1);
        // (2) 活板门回归:6 板 2×3 仍答活板门 ×2(勘误不扰邻)。
        const int gTrap[9] = { PL, PL, PL, PL, PL, PL, 0, 0, 0 };
        const auto *rTrap = RecipeRegistry::match(gTrap, 3);
        const bool trapOk = rTrap && rTrap->outputId == int(BR::WoodTrapdoor) && rTrap->outputCount == 2;
        ok = ok && trapOk;
        if (!trapOk) diag += QStringLiteral("[trap %1]").arg(rTrap ? rTrap->outputId : -1);
        // (3) 门孪生各行其答。
        const int gDoor[9] = { PL, 0, 0, PL, 0, 0, PL, 0, 0 };
        const auto *rDoor = RecipeRegistry::match(gDoor, 3);
        const bool doorOk = rDoor && rDoor->outputId == int(BR::WoodDoor);
        ok = ok && doorOk;
        if (!doorOk) diag += QStringLiteral("[door]");
        // (4) 错位棍负例:底左棍(非中列)不匹配(包围盒 3×3 内容异)。
        const int gMisplaced[9] = { PL, PL, PL, PL, PL, PL, STI, 0, 0 };
        const bool misOk = RecipeRegistry::match(gMisplaced, 3) == nullptr;
        ok = ok && misOk;
        if (!misOk) diag += QStringLiteral("[mis]");
        // (5) 全表在册钉:standing_sign 行恰一 + 自身 pattern 自答回自身(t802 遮蔽审计点式)。
        int found = 0;
        bool selfMatch = false;
        for (int i = 0; i < RecipeRegistry::recipeCount(); ++i) {
            const auto *r = RecipeRegistry::recipeAt(i);
            if (r && r->outputId == int(BR::StandingSign) && r->pattern[6] == 0
                && r->pattern[7] == RecipeRegistry::StickId && r->pattern[8] == 0) {
                ++found;
                selfMatch = selfMatch || (RecipeRegistry::match(r->pattern, r->gridSize) == r);
            }
        }
        const bool regOk = found == 1 && selfMatch;
        ok = ok && regOk;
        if (!regOk) diag += QStringLiteral("[reg n=%1 self=%2]").arg(found).arg(selfMatch);
        // (6) 板材族回退:云杉板网格(等价规范化 → 橡木行)同合牌子。
        const int gSpruce[9] = { SP, SP, SP, SP, SP, SP, 0, STI, 0 };
        const auto *rSpruce = RecipeRegistry::match(gSpruce, 3);
        const bool spruceOk = rSpruce && rSpruce->outputId == int(BR::StandingSign);
        ok = ok && spruceOk;
        if (!spruceOk) diag += QStringLiteral("[spruce]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098c sign recipe column (the six planks over a center bottom stick answers one"
               " standing sign per the jar read while the six plank two by three grid still"
               " answers the trapdoor pair and the wood door twin keeps its own answer, a"
               " misplaced bottom left stick answers nothing, the sign row is registered exactly"
               " once and answers its own pattern back, and the spruce plank family fallback"
               " crafts the same sign)"
            << (ok ? QString() : diag);
    });

    // ── r2098d:牌子全回路柱(件三真链;合成→放置→写四行→存→重进→破坏掉落)────────────────────
    //   合成面(match 命中站牌)→ 热栏持牌放置真链(signPlaced 恰一 + 落格朝向)→ 写四行文本
    //   (SignStore::setText)+ 读回 → 存档快照 allSigns → 新 store loadAll 重进读回一致 →
    //   生存真链挖掘泵(beginMining + 墙钟 dt tick 泵至格 Air,section08 t1026a 同款)→
    //   spawnItem 掉站牌 ×1 + 格归 Air。
    runLeg("r2098d sign full loop column (the crafted sign places through the real placement"
        " chain exactly once facing away from the player, four written lines read back, the"
        " save snapshot reloads into a fresh store with the same four lines, and a real"
        " survival mine of the sign drops the sign item exactly once and clears the"
        " cell)", [&]() {
        bool ok = true;
        QString diag;
        const int PL = int(BR::Planks);
        const int STI = int(RecipeRegistry::StickId);
        // (1) 合成面:era 网格 → 站牌(真链起点)。
        const int gSign[9] = { PL, PL, PL, PL, PL, PL, 0, STI, 0 };
        const auto *rSign = RecipeRegistry::match(gSign, 3);
        const bool craftOk = rSign && rSign->outputId == int(BR::StandingSign) && rSign->outputCount == 1;
        ok = ok && craftOk;
        if (!craftOk) { diag += QStringLiteral("[craft]"); }
        // (2) 放置真链 + 写四行 + 存读回 + 生存挖掘破坏掉落(单 rig 串链)。
        if (craftOk) {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 14, 40, 14, 40);
            EntityManager ents; Hotbar hb; SignStore store; PlayerController pc;
            pc.setWorld(&w); pc.setEntityManager(&ents); pc.setHotbar(&hb);
            QQuickWindow probeWin; pc.setParentItem(probeWin.contentItem()); pc.grab();
            int placed = 0, dropped = 0, dropId = -1, dropCnt = -1;
            int px = -1, py = -1, pz = -1;
            QObject::connect(&pc, &PlayerController::signPlaced, &pc,
                             [&](int x, int y, int z) { ++placed; px = x; py = y; pz = z; });
            QObject::connect(&pc, &PlayerController::spawnItem, &pc,
                             [&](int, int, int, int id, int cnt) { ++dropped; dropId = id; dropCnt = cnt; });
            hb.setStack(0, int(BR::StandingSign), 1, 0); // 合成产物上手(等价取件)
            pc.setSelectedBlock(int(BR::StandingSign));
            // 眼位 (24.5,81,26.5) yaw0 pitch-45 → 底面命中 (24,80,24) 顶面 → 落格 (24,81,24)
            //   (r2083a 放置真链同眼位几何)。
            pc.loadSavedState(24.5, 81.0, 26.5, 0.0, -45.0, 1 /* Creative */);
            pc.tick();
            pumpInteract(320);
            pc.placeBlock();
            const bool placeOk = placed == 1 && px == 24 && py == 81 && pz == 24
                && w.blockAt(24, 81, 24) == quint8(BR::StandingSign) && w.stateAt(24, 81, 24) == 2;
            ok = ok && placeOk;
            if (!placeOk) diag += QStringLiteral("[place n=%1 @%2,%3,%4 id=%5 st=%6]")
                .arg(placed).arg(px).arg(py).arg(pz)
                .arg(w.blockAt(24, 81, 24)).arg(w.stateAt(24, 81, 24));
            // (3) 写四行(Main.qml 编辑面板确认同入口 setText)+ 读回。
            store.setText(24, 81, 24, QStringLiteral("alpha"), QStringLiteral("beta"),
                          QStringLiteral("gamma"), QStringLiteral("delta"));
            const bool textOk = store.lineAt(24, 81, 24, 0) == QStringLiteral("alpha")
                && store.lineAt(24, 81, 24, 3) == QStringLiteral("delta");
            ok = ok && textOk;
            if (!textOk) diag += QStringLiteral("[text]");
            // (4) 存档快照 → 新 store 重进(loadAll 替换语义)+ 四行读回一致。
            const QVariantList snap = store.allSigns();
            SignStore store2;
            store2.loadAll(snap);
            const bool reloadOk = store2.lineAt(24, 81, 24, 0) == QStringLiteral("alpha")
                && store2.lineAt(24, 81, 24, 1) == QStringLiteral("beta")
                && store2.lineAt(24, 81, 24, 2) == QStringLiteral("gamma")
                && store2.lineAt(24, 81, 24, 3) == QStringLiteral("delta")
                && store2.entryCount() == 1;
            ok = ok && reloadOk;
            if (!reloadOk) diag += QStringLiteral("[reload n=%1]").arg(store2.entryCount());
            // (5) 生存真链破坏掉落:重定位生存态 + 瞄牌面(板 +Z 面 24.5625 中点)→ 持木斧
            //   beginMining + 墙钟 dt tick 泵至格 Air(t1026a 教训:每 tick busy-wait ≥17ms 保
            //   updateMining 墙钟 dt>0)→ 破格 finishMiningAt(canHarvest) 发 spawnItem。
            pc.release();
            pc.grab();
            {
                const float ex = 24.5f, ey = 82.62f, ez = 26.5f;
                const float ax = 24.5f, ay = 81.8f, az = 24.5625f;
                const float dx = ax - ex, dy = ay - ey, dz = az - ez;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float pitch = std::asin(dy / len) * 57.2957795f;
                const float yaw = std::atan2(-dx, -dz) * 57.2957795f;
                pc.loadSavedState(ex, 81.0f, ez, yaw, pitch, 2 /* Survival */);
            }
            hb.setStack(1, int(ToolRegistry::AxeWood), 1); // 木斧加速(硬木 1.0 → 0.75s 级)
            hb.setSelectedSlot(1);
            pc.tick(); // 刷射线(命中牌板)
            const bool aimedOk = pc.hasHit() && pc.hitBlock() == QVector3D(24, 81, 24);
            pc.beginMining();
            for (int i = 0; i < 3000 && w.blockAt(24, 81, 24) != quint8(BR::Air); ++i) {
                QElapsedTimer dtw;
                dtw.start();
                while (dtw.elapsed() < 17)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
                pc.tick();
                QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            }
            pc.endMining();
            const bool breakOk = aimedOk && dropped == 1 && dropId == int(BR::StandingSign)
                && dropCnt == 1 && w.blockAt(24, 81, 24) == quint8(BR::Air);
            ok = ok && breakOk;
            if (!breakOk) diag += QStringLiteral("[break aimed=%1 n=%2 id=%3 cnt=%4 cell=%5]")
                .arg(aimedOk).arg(dropped).arg(dropId).arg(dropCnt).arg(w.blockAt(24, 81, 24));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098d sign full loop column (the crafted sign places through the real placement"
               " chain exactly once facing away from the player, four written lines read back, the"
               " save snapshot reloads into a fresh store with the same four lines, breaking the"
               " sign drops the sign item exactly once and clears the cell)"
            << (ok ? QString() : diag);
    });

    // ── r2098e:换料柱(件四身份门;NEG-1 敏感面)─────────────────────────────────────────────
    //   近满换料(19s 换有效料 → 新配方不得沿用旧进度秒完:首拍进度 < 1 + 原料未耗 + 瓶未转,
    //   续 20s 酿出新产物)+ 同 id 增减数量不清进度(10s 改 count 续酿)+ 移除 → 复位 → 插回
    //   同 id 新酿完成 + 换无效料复位 + 保存重进(身份会话态缺省 → 首拍复位,era k 缺省 0 字节
    //   等价)→ 完整时长重酿。全程逻辑 tick 直驱(scanBrewingStands 定步长,零 sleep)。
    runLeg("r2098e brewing ingredient swap column (swapping the ingredient near completion"
        " cancels the carried progress so the new recipe never rides the old bar to an instant"
        " or wrong product, a same id count change keeps the bar running, removing the"
        " ingredient resets the bar and reinserting starts a fresh full brew that completes,"
        " swapping to a non brewing ingredient resets the bar, and a save reload restarts the"
        " carried bar per the session identity caliber before completing the correct"
        " product)", [&]() {
        bool ok = true;
        QString diag;
        const int WB = int(RecipeRegistry::WaterBottleId);
        const int AW = int(RecipeRegistry::AwkwardPotionId);
        const int SUG = int(RecipeRegistry::SugarId);
        const int BLZ = int(RecipeRegistry::BlazePowderId);
        const int SPD = int(RecipeRegistry::SpeedPotionId);
        const int STR = int(RecipeRegistry::StrengthPotionId);
        const int WART = int(RecipeRegistry::AshWartId);
        const int STK = int(RecipeRegistry::StickId);
        const int II = BrewingStore::kSlotIngredient; // 原料槽位
        const int FI = BrewingStore::kSlotFuel;
        // 场景 ①:近满换料(19s 逻辑时换糖→燃烬粉,粗制瓶底两料皆有效)——重开不秒完,新配方正确。
        {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 20, 30, 20, 30);
            PlayerController pc; pc.setWorld(&w);
            BrewingStore store; pc.setBrewingStore(&store);
            const int bx = 24, by = 81, bz = 24;
            w.setBlock(bx, by, bz, BR::BrewingStand, 0);
            store.setSlot(bx, by, bz, 0, AW, 1);
            store.setSlot(bx, by, bz, II, SUG, 2);
            store.setSlot(bx, by, bz, FI, BLZ, 1);
            for (int i = 0; i < 380; ++i) pc.scanBrewingStands(0.05f); // 19.0s 逻辑时
            const bool midOk = store.brewProgressAt(bx, by, bz) > 15.0
                && store.brewProgressAt(bx, by, bz) < 20.0;
            store.setSlot(bx, by, bz, II, BLZ, 2); // 换料(糖→燃烬粉,对粗制瓶有效)
            pc.scanBrewingStands(0.05f);           // 首拍:身份门复位 + 自零重进
            const qreal p1 = store.brewProgressAt(bx, by, bz);
            const bool swapOk = p1 < 1.0                                   // 禁旧进度扛过(NEG-1 面:恒 19.05 红)
                && store.slotIdAt(bx, by, bz, 0) == AW                     // 瓶未转(禁秒完)
                && store.slotCountAt(bx, by, bz, II) == 2;                 // 原料未耗
            for (int i = 0; i < 400; ++i) pc.scanBrewingStands(0.05f); // 20.0s 完整重酿
            const bool doneOk = store.slotIdAt(bx, by, bz, 0) == STR        // 新配方产物(禁 Speed)
                && store.slotCountAt(bx, by, bz, II) == 1;                  // 恰耗 1
            ok = ok && midOk && swapOk && doneOk;
            if (!(midOk && swapOk && doneOk))
                diag += QStringLiteral("[s1 mid=%1 p1=%2 id=%3 ing=%4]")
                    .arg(midOk).arg(p1).arg(store.slotIdAt(bx, by, bz, 0))
                    .arg(store.slotCountAt(bx, by, bz, II));
        }
        // 场景 ②:同 id 数量增减不清进度(10s 改 count 续酿至完成)。
        {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 20, 30, 20, 30);
            PlayerController pc; pc.setWorld(&w);
            BrewingStore store; pc.setBrewingStore(&store);
            const int bx = 24, by = 81, bz = 24;
            w.setBlock(bx, by, bz, BR::BrewingStand, 0);
            store.setSlot(bx, by, bz, 0, AW, 1);
            store.setSlot(bx, by, bz, II, SUG, 3);
            store.setSlot(bx, by, bz, FI, BLZ, 1);
            for (int i = 0; i < 200; ++i) pc.scanBrewingStands(0.05f); // 10.0s
            store.setSlot(bx, by, bz, II, SUG, 5); // 同 id 加量(不清进度)
            pc.scanBrewingStands(0.05f);
            const bool keepOk = store.brewProgressAt(bx, by, bz) > 5.0; // 进度延续(≈10.05)
            for (int i = 0; i < 400; ++i) pc.scanBrewingStands(0.05f);
            const bool doneOk = store.slotIdAt(bx, by, bz, 0) == SPD
                && store.slotCountAt(bx, by, bz, II) == 4; // 恰耗 1
            ok = ok && keepOk && doneOk;
            if (!ok || !(keepOk && doneOk))
                diag += QStringLiteral("[s2 keep=%1 id=%2 ing=%3]")
                    .arg(keepOk).arg(store.slotIdAt(bx, by, bz, 0))
                    .arg(store.slotCountAt(bx, by, bz, II));
        }
        // 场景 ③:移除 → 复位 → 插回同 id 恒新酿(完整时长完成)。
        {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 20, 30, 20, 30);
            PlayerController pc; pc.setWorld(&w);
            BrewingStore store; pc.setBrewingStore(&store);
            const int bx = 24, by = 81, bz = 24;
            w.setBlock(bx, by, bz, BR::BrewingStand, 0);
            store.setSlot(bx, by, bz, 0, WB, 1);
            store.setSlot(bx, by, bz, II, WART, 1);
            store.setSlot(bx, by, bz, FI, BLZ, 2);
            for (int i = 0; i < 100; ++i) pc.scanBrewingStands(0.05f); // 5.0s
            store.setSlot(bx, by, bz, II, 0, 0); // 移除原料
            pc.scanBrewingStands(0.05f);
            const bool resetOk = store.brewProgressAt(bx, by, bz) == 0.0; // 既有 idle 复位面
            store.setSlot(bx, by, bz, II, WART, 1); // 插回同 id
            pc.scanBrewingStands(0.05f);
            const bool freshOk = store.brewProgressAt(bx, by, bz) > 0.0
                && store.brewProgressAt(bx, by, bz) < 1.0; // 新酿(非续 5s)
            for (int i = 0; i < 400; ++i) pc.scanBrewingStands(0.05f);
            const bool doneOk = store.slotIdAt(bx, by, bz, 0) == AW;
            ok = ok && resetOk && freshOk && doneOk;
            if (!ok || !(resetOk && freshOk && doneOk))
                diag += QStringLiteral("[s3 reset=%1 fresh=%2 id=%3]")
                    .arg(resetOk).arg(freshOk).arg(store.slotIdAt(bx, by, bz, 0));
        }
        // 场景 ④:换无效料(水瓶+疣酿中换木棒)→ 既有 eligible 复位面兜底(进度清零)。
        {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 20, 30, 20, 30);
            PlayerController pc; pc.setWorld(&w);
            BrewingStore store; pc.setBrewingStore(&store);
            const int bx = 24, by = 81, bz = 24;
            w.setBlock(bx, by, bz, BR::BrewingStand, 0);
            store.setSlot(bx, by, bz, 0, WB, 1);
            store.setSlot(bx, by, bz, II, WART, 1);
            store.setSlot(bx, by, bz, FI, BLZ, 1);
            for (int i = 0; i < 100; ++i) pc.scanBrewingStands(0.05f);
            store.setSlot(bx, by, bz, II, STK, 1); // 无效料(水瓶底无映射)
            pc.scanBrewingStands(0.05f);
            const bool invalidOk = store.brewProgressAt(bx, by, bz) == 0.0;
            ok = ok && invalidOk;
            if (!invalidOk) diag += QStringLiteral("[s4 p=%1]").arg(store.brewProgressAt(bx, by, bz));
        }
        // 场景 ⑤:保存重进(身份会话态缺省 → 首拍复位 = era k 缺省 0 字节等价)→ 完整时长重酿。
        {
            World w; initInteractWorld(w, 96);
            layInteractPlatform(w, 20, 30, 20, 30);
            PlayerController pc; pc.setWorld(&w);
            BrewingStore store; pc.setBrewingStore(&store);
            const int bx = 24, by = 81, bz = 24;
            w.setBlock(bx, by, bz, BR::BrewingStand, 0);
            store.setSlot(bx, by, bz, 0, AW, 1);
            store.setSlot(bx, by, bz, II, SUG, 1);
            store.setSlot(bx, by, bz, FI, BLZ, 1);
            for (int i = 0; i < 200; ++i) pc.scanBrewingStands(0.05f); // 10.0s
            const QVariantList snap = store.allBrewingStands(); // 存档边界(进度持久/身份不持久)
            BrewingStore store2;
            store2.loadAll(snap);
            PlayerController pc2; pc2.setWorld(&w); pc2.setBrewingStore(&store2);
            const bool restoredOk = store2.brewProgressAt(bx, by, bz) > 5.0; // 进度值如实落盘回填
            pc2.scanBrewingStands(0.05f); // 重进首拍:身份缺省 ≠ 当前原料 → 复位(era 同式)
            const bool restartOk = store2.brewProgressAt(bx, by, bz) < 1.0; // 禁旧进度扛过重载
            for (int i = 0; i < 400; ++i) pc2.scanBrewingStands(0.05f);
            const bool doneOk = store2.slotIdAt(bx, by, bz, 0) == SPD;
            ok = ok && restoredOk && restartOk && doneOk;
            if (!(restoredOk && restartOk && doneOk))
                diag += QStringLiteral("[s5 rest=%1 p=%2 id=%3]")
                    .arg(restoredOk).arg(store2.brewProgressAt(bx, by, bz))
                    .arg(store2.slotIdAt(bx, by, bz, 0));
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098e brewing ingredient swap column (swapping the ingredient near completion"
               " cancels the carried progress so the new recipe never rides the old bar to an"
               " instant or wrong product, a same id count change keeps the bar running, removing"
               " the ingredient resets the bar and reinserting starts a fresh full brew that"
               " completes, swapping to a non brewing ingredient resets the bar, and a save"
               " reload restarts the carried bar per the session identity caliber before"
               " completing the correct product)"
            << (ok ? QString() : diag);
    });

    // ── r2098f:结构钉族 + 件五裁定锚柱(NEG 双摘面全豁免 = 块外锚 / 成员行 / 工件锚)────────────
    //   era 工件锚 raw(两工件路径在 game 侧注体在场)+ 件四成员面(activeIngredient 字段/读写
    //   定义行——NEG-1 摘块外幸存)+ 容量探针面(canFitStack 声明/定义——NEG-2 摘消耗行外幸存)
    //   + 燃料定谳注 raw + 瓶槽口径钉 raw + 配方勘误锚 raw(地图罗盘芯工件路径 + 牌子行在册)+
    //   零行为面钉(燃料面原值不动:燃烬粉 20/纸 0)+ CMake 段行 + QML 五禁词门。
    runLeg("r2098f structure pin family (the two era evidence artifacts anchor raw on the game"
        " side notes, the brewing identity member keeps its field and accessor rows beside the"
        " removable gate block, the fit probe keeps its declaration and definition rows, the"
        " fuel verdict and bottle caliber notes survive raw, the map and sign erratum anchors"
        " survive raw, the untouched fuel face still answers twenty for the powder and zero"
        " otherwise, the cmake section row holds, and the five banned qml tokens answer"
        " zero)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForInteractPins();
        const QString pcC = srcDir + QStringLiteral("/Game/playercontroller.cpp");
        const QString bsH = srcDir + QStringLiteral("/Game/brewingstore.h");
        const QString bsC = srcDir + QStringLiteral("/Game/brewingstore.cpp");
        const QString hbH = srcDir + QStringLiteral("/Game/hotbar.h");
        const QString hbC = srcDir + QStringLiteral("/Game/hotbar.cpp");
        const QString rcC = srcDir + QStringLiteral("/Game/recipe.cpp");
        const QString brH = srcDir + QStringLiteral("/Core/blockregistry.h");
        const QString qml = srcDir + QStringLiteral("/ui/Main.qml");
        const QString cmk = QCoreApplication::applicationDirPath()
                            + QStringLiteral("/../CMakeLists.txt");
        // (1) era 工件锚 raw(件二/件三/件四/件五四问同源两工件——注释体锚,pinSet 剥注释失配)。
        const bool eraCraft = rawContainsInteract(rcC, QStringLiteral("t1128_jar_crafting_map_sign.txt"));
        const bool eraBrewH = rawContainsInteract(bsH, QStringLiteral("t1128_jar_brewing_tile.txt"));
        const bool eraBrewC = rawContainsInteract(pcC, QStringLiteral("t1128_jar_brewing_tile.txt"));
        ok = ok && eraCraft && eraBrewH && eraBrewC;
        if (!eraCraft || !eraBrewH || !eraBrewC)
            diag += QStringLiteral("[era c=%1 h=%2 pc=%3]").arg(eraCraft).arg(eraBrewH).arg(eraBrewC);
        // (2) 件四成员面(NEG-1 摘块外):字段行 + 引擎读写声明/定义行。
        const QStringList missBsH = pinSet(bsH, {
            SrcPin("identity field row", "int activeIngredient = 0;", 1),
            SrcPin("identity read decl", "int activeIngredientAt(int x, int y, int z) const;", 1),
            SrcPin("identity write decl", "void setActiveIngredient(int x, int y, int z, int ingredientId);", 1)});
        const QStringList missBsC = pinSet(bsC, {
            SrcPin("identity read def", "int BrewingStore::activeIngredientAt(int x, int y, int z) const", 1),
            SrcPin("identity write def", "void BrewingStore::setActiveIngredient(int x, int y, int z, int ingredientId)", 1)});
        ok = ok && missBsH.isEmpty() && missBsC.isEmpty();
        if (!missBsH.isEmpty() || !missBsC.isEmpty())
            diag += QStringLiteral("[bs %1|%2]").arg(missBsH.join(QLatin1Char(',')))
                        .arg(missBsC.join(QLatin1Char(',')));
        // (3) 容量探针面(NEG-2 摘消耗行外):声明 + 定义行。
        const QStringList missHbH = pinSet(hbH, {
            SrcPin("fit probe decl", "bool canFitStack(int id, int n, bool countFreedSelectedSlot) const;", 1)});
        const QStringList missHbC = pinSet(hbC, {
            SrcPin("fit probe def", "bool Hotbar::canFitStack(int id, int n, bool countFreedSelectedSlot) const", 1)});
        ok = ok && missHbH.isEmpty() && missHbC.isEmpty();
        if (!missHbH.isEmpty() || !missHbC.isEmpty())
            diag += QStringLiteral("[hb %1|%2]").arg(missHbH.join(QLatin1Char(',')))
                        .arg(missHbC.join(QLatin1Char(',')));
        // (4) 件五定谳注 raw(燃料面勘正候选 + 瓶槽口径候选——两注体在场钉)。
        const bool fuelNote = rawContainsInteract(bsH, QStringLiteral("t1128 era 定谳注"))
            && rawContainsInteract(pcC, QStringLiteral("t1128 件五附带核"));
        ok = ok && fuelNote;
        if (!fuelNote) diag += QStringLiteral("[fuelNote]");
        // (5) 配方勘误锚 raw(牌子行在册 + t1113 翻案重写段 + 站牌行尾锚)。
        const bool signAnchors = rawContainsInteract(rcC, QStringLiteral("t1128 牌子合成一行"))
            && rawContainsInteract(rcC, QStringLiteral("\"standing_sign\""))
            && rawContainsInteract(brH, QStringLiteral("t1128 勘误"));
        ok = ok && signAnchors;
        if (!signAnchors) diag += QStringLiteral("[signAnchors]");
        // (6) 零行为面钉(件五只核不修:燃料面原值不动——燃烬粉 20 / 纸 0 / 常量 20)。
        const bool fuelFace = BrewingStore::fuelOpsFor(int(RecipeRegistry::BlazePowderId)) == 20
            && BrewingStore::fuelOpsFor(int(RecipeRegistry::PaperId)) == 0
            && BrewingStore::kPowderFuelOps == 20
            && BrewingStore::fuelOpsFor(0) == 0;
        ok = ok && fuelFace;
        if (!fuelFace) diag += QStringLiteral("[fuelFace]");
        // (7) CMake 段行 + QML 五禁词零命中(r2090d 家族复钉)。
        const QStringList missCmk = pinSet(cmk, {
            SrcPin("cmake section row", "tools/matrix/section91_interact_fix_t1128.cpp", 1)});
        const bool banGameSession = rawContainsInteract(qml, QStringLiteral("GameSession"));
        const bool banMeshWorker = rawContainsInteract(qml, QStringLiteral("MeshWorker"));
        const bool banLifecycle = rawContainsInteract(qml, QStringLiteral("setChunkLifecycle"));
        const bool banEvictor = rawContainsInteract(qml, QStringLiteral("ChunkEvictor"));
        const bool banDriver = rawContainsInteract(qml, QStringLiteral("ChunkStreamDriver"));
        ok = ok && missCmk.isEmpty()
             && !banGameSession && !banMeshWorker && !banLifecycle && !banEvictor && !banDriver;
        if (!missCmk.isEmpty() || banGameSession || banMeshWorker || banLifecycle
            || banEvictor || banDriver)
            diag += QStringLiteral("[doors cmk=%1 ban=%2%3%4%5%6]")
                        .arg(missCmk.join(QLatin1Char(','))).arg(banGameSession).arg(banMeshWorker)
                        .arg(banLifecycle).arg(banEvictor).arg(banDriver);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2098f structure pin family (the two era evidence artifacts anchor raw on the"
               " game side notes, the brewing identity member keeps its field and accessor rows"
               " beside the removable gate block, the fit probe keeps its declaration and"
               " definition rows, the fuel verdict and bottle caliber notes survive raw, the map"
               " and sign erratum anchors survive raw, the untouched fuel face still answers"
               " twenty for the powder and zero otherwise, the cmake section row holds, and the"
               " five banned qml tokens answer zero)"
            << (ok ? QString() : diag);
    });
}
