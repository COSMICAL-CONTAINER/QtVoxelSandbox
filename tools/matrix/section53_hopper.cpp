#include "matrix_helpers.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>

// t1080 漏斗（Hopper=145）机制四语义探针段（4 腿；filter 词 r2052；矩阵 738→742）。置尾先例（接
// section52，runAll 末执行，rig 世界零接触——各腿自建 fresh 小世界 48×48×96 s82，section11 同门）。
//
// 现场核实（按实况定范围，t1070/t1077/t1078 先例第四现）：候选「漏斗 = 收集 + 抽取/输出 + 红石锁停 + UI」
// 四段中——① 容器族现状 = ChestStore(27) / DispenserStore(9，投掷器共用) / FurnaceStore(3) 三个 per-block
//   坐标键控存储，**无统一容器抽象**（单一权威铁律：不造第二份抽象，引擎按同形 Q_INVOKABLE 面 kind 分派）；
//   ② 掉落物权威 = EntityStore（R20.14，ItemEntityManager 过渡 Adapter）——收集 = 读栈 + insertStack 入腔 +
//   removeAt/setCountAt，禁第二份模拟；③ 红石面 = World::isReceivingPower 既有信号查询面（邻源直供 / 邻粉
//   通电）接钩即得锁停语义，**无缺口、不建第二套信号系统**；④ UI 面 = 箱子 UI 先例在（ChestUI）但漏斗开盖
//   面禁为此单新开 QML 玩法路径（零迁移铁律）→ **如实降级**（revision/读族在位留 HopperUI 候选）。
//   熔炉挂接：抽取只认 out 产物槽 / 推入按面定向（朝下 in(0) / 侧推 fuel(1)）——MC 口径；熔炉 tick 本体
//   是 QML 面板驱动的既有降级面（FurnaceUI.tick），非本单范围。
//
// 腿面：r2052a 收集承重墙（上方格 + 自身格收集 / 整栈入腔逐位 id·count·槽序 / 元数据保真 / 满仓拒绝实体
//   留场 / 部分接受余量回写 / 异列不收）；r2052b 抽取 + 输出承重墙（上抽首非空槽 / 前推排料口朝向 /
//   漏斗链传递 / 无目标挂起不销毁 / 熔炉 out 抽取与 in·fuel 定向推入 / 元数据随栈保真）；r2052c 红石锁停 +
//   边界墙（锁停三语义全停 / 无效应不消耗口径 / 解锁恢复 / 满仓拒绝 / 孤儿条目 inert）；r2052d 结构钉 +
//   持久化往返（def 行逐字段 + kMcBlockId 尾行 + 排料口解码单一权威 + 图集三瓦片 + 配方命中/负例 + 注册族
//   源钉 + QML 零触碰反探 + hoppers 表真 SQLite 往返）。
//
// 阴性面（双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证 build/ 终名日志
//   matrix_t1080_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘输出语义本体（playercontroller.cpp scanHoppers 输出段 transfer 行注释）→ 声明红面 {r2052b}
//     （前推 / 链传递 / 熔炉定向推入全断）。r2052a 不误伤（收集路径独立）；r2052c 不误伤（锁停腿全部
//     断言面走抽取/收集通道，输出仅以 Dirt 计数作被动锁停见证——输出死则计数恒定、断言仍真）；
//     r2052d 不误伤（钉面无 transfer 行）。
//   NEG-2 摘锁停语义本体（scanHoppers 红石锁停 continue 行注释）→ 声明红面 {r2052c}（powered 漏斗继续
//     转移 = 锁停断言红）。r2052a/b 不误伤（两腿无 powered 漏斗场景）；r2052d 不误伤（无该行钉面）。
//
// rig 纪律：fresh 小世界 incantation（section11 四 setter 同款，构造即 generate）；rig 位**运行期扫描空域**
//   （lessons t769/t1030：不信「某高度以上必空」——逐列扫 y-1/y/y+1 三连空）；引擎条目经 setHopperCooldown
//   显式登记（生产等价 = placeBlock 放置登记行）；驱动 = pc.scanHoppers(0.4f) 直调（每调用恰一轮，
//   scanDispenserTraps review24 #9 等价递减驱动先例——探针无 16ms 定时器）。

namespace {

// fresh 小世界 incantation（section11 同款四 setter + 恒晴零 RNG）。
inline void initHopperWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// rig 空域扫描：列 (x,z) 上首个「y-1 / y / y+1 三连空」高度（放置层 = y，容器邻格向下兼容）。
inline int findHopperRigY(World &w, int x, int z)
{
    for (int y = 4; y < w.height() - 2; ++y) {
        if (w.blockAt(x, y - 1, z) == BR::Air && w.blockAt(x, y, z) == BR::Air
            && w.blockAt(x, y + 1, z) == BR::Air)
            return y;
    }
    return -1;
}

// 腿 rig：fresh 小世界 + 四容器 store + 掉落物 Adapter + 真消费端 PlayerController（t814 直造同门——
// C++ 直造不启 16ms 定时器，componentComplete 不触发；机制节律由探针直调 scanHoppers 驱动）。
struct HopperRig
{
    World w;
    ChestStore chests;
    DispenserStore disp;
    FurnaceStore furn;
    HopperStore hoppers;
    ItemEntityManager items;
    PlayerController pc;

    explicit HopperRig()
    {
        initHopperWorld(w);
        pc.setWorld(&w);
        pc.setChestStore(&chests);
        pc.setDispenserStore(&disp);
        pc.setFurnaceStore(&furn);
        pc.setHopperStore(&hoppers);
        pc.setItemEntities(&items);
    }

    // 放漏斗（state 直写 + 引擎条目登记——生产等价 = placeBlock 放置登记行；回读钉落位）。
    void placeHopper(int x, int y, int z, quint8 state)
    {
        w.setBlock(x, y, z, BR::Hopper, state);
        hoppers.setHopperCooldown(x, y, z, 0.0);
    }

    // 驱动 n 轮（每调用恰一轮——cd 0.4 与 dt 0.4 相位对齐）。
    void drive(int cycles)
    {
        for (int i = 0; i < cycles; ++i) pc.scanHoppers(0.4f);
    }
};

} // namespace

void MatrixRun::section53_hopper()
{
    // 实体定位帮手（槽位 LIFO 复用 t256——禁按生成序断言下标，按 itemId 找活体）。
    const auto findAlive = [](ItemEntityManager &items, int itemId) -> int {
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i) && items.itemIdAt(i) == itemId) return i;
        return -1;
    };

    // ── r2052a：收集承重墙（掉落物 -> 入槽逐位）────────────────────────────────────────
    runLeg("r2052a hopper collection wall (above+self cell vacuum, whole-stack slot-exact, metadata, full/partial reject)", [&]() {
        HopperRig rig;
        bool ok = true;
        QString diag;
        const int x = 8, z = 8;
        const int y = findHopperRigY(rig.w, x, z);
        if (y <= 0) { ++totalFail; qInfo().noquote() << "FAIL | r2052a hopper collection wall (no rig space y=" << y << ")"; return; }
        rig.placeHopper(x, y, z, BlockRegistry::HopperFacingDownFlag);

        // (1) 整栈收集：上方格掉落物（泥土 x5）一轮全入 -> 实体销毁 + 槽 0 = {Dirt,5}（id/count/槽序逐位）。
        rig.items.spawnItem(x, y + 1, z, int(BR::Dirt), 5);
        rig.drive(1);
        const bool a1 = rig.hoppers.slotIdAt(x, y, z, 0) == int(BR::Dirt)
            && rig.hoppers.slotCountAt(x, y, z, 0) == 5
            && findAlive(rig.items, int(BR::Dirt)) < 0;
        ok = ok && a1;
        if (!a1) diag += QStringLiteral("[a1 id=%1 cnt=%2]")
                            .arg(rig.hoppers.slotIdAt(x, y, z, 0))
                            .arg(rig.hoppers.slotCountAt(x, y, z, 0));

        // (2) 自身格收集：掉落物落在漏斗自身格（MC 规则收集域含自身格）-> 一轮入腔。
        rig.items.spawnItem(x, y, z, int(BR::Cobble), 2);
        rig.drive(1);
        const bool a2 = rig.hoppers.slotIdAt(x, y, z, 1) == int(BR::Cobble)
            && rig.hoppers.slotCountAt(x, y, z, 1) == 2
            && findAlive(rig.items, int(BR::Cobble)) < 0;
        ok = ok && a2;
        if (!a2) diag += QStringLiteral("[a2 id=%1 cnt=%2]")
                            .arg(rig.hoppers.slotIdAt(x, y, z, 1)).arg(rig.hoppers.slotCountAt(x, y, z, 1));

        // (3) 异列不收：邻列 (x+1, y+1) 的掉落物不进腔（收集域 = 上方格 + 自身格，无斜向口径）；
        //     本漏斗朝下且下方无容器 -> 无输出目标 -> 实体留场（挂起不销毁口径的收集侧镜像）。
        rig.items.spawnItem(x + 1, y + 1, z, int(BR::Sand), 1);
        rig.drive(2);
        bool a3 = findAlive(rig.items, int(BR::Sand)) >= 0;
        for (int i = 0; i < HopperStore::kSlotsPerHopper; ++i)
            a3 = a3 && rig.hoppers.slotIdAt(x, y, z, i) != int(BR::Sand);
        ok = ok && a3;
        if (!a3) diag += QStringLiteral("[a3]");

        // (4) 元数据保真：带耐久 + 名的掉落物（cap=1 语义，木锄）入腔逐位随栈。
        rig.items.spawnItem(x, y + 1, z, int(ToolRegistry::HoeWood), 1, QVariantList(),
                            QStringLiteral("试制锄"), 42);
        rig.drive(1);
        const bool a4 = rig.hoppers.slotIdAt(x, y, z, 2) == int(ToolRegistry::HoeWood)
            && rig.hoppers.slotDurabilityAt(x, y, z, 2) == 42
            && rig.hoppers.slotNameAt(x, y, z, 2) == QStringLiteral("试制锄")
            && findAlive(rig.items, int(ToolRegistry::HoeWood)) < 0;
        ok = ok && a4;
        if (!a4) diag += QStringLiteral("[a4 id=%1 dur=%2 name=%3]")
                            .arg(rig.hoppers.slotIdAt(x, y, z, 2))
                            .arg(rig.hoppers.slotDurabilityAt(x, y, z, 2))
                            .arg(rig.hoppers.slotNameAt(x, y, z, 2));

        // (5) 满仓拒绝：五槽全满（圆石 x64）-> 上方掉落物原地保留（挂起不销毁——满仓拒收口径）。
        rig.hoppers.clearHopper(x, y, z);
        for (int i = 0; i < HopperStore::kSlotsPerHopper; ++i)
            rig.hoppers.setSlot(x, y, z, i, int(BR::Cobble), 64);
        rig.items.spawnItem(x, y + 1, z, int(BR::Stone), 3);
        rig.drive(1);
        const int st5 = findAlive(rig.items, int(BR::Stone));
        const bool a5 = st5 >= 0 && rig.items.countAt(st5) == 3
            && rig.hoppers.slotCountAt(x, y, z, 0) == 64
            && rig.hoppers.slotIdAt(x, y, z, 0) == int(BR::Cobble);
        ok = ok && a5;
        if (!a5) diag += QStringLiteral("[a5 ent=%1]")
                            .arg(rig.items.aliveAt(st5 < 0 ? 0 : st5) ? rig.items.countAt(st5) : -1);

        // (6) 部分接受（cap 边界合并）：槽 4 预置圆石 x62 -> 上方圆石 x5 合并 2 件至 cap 64、余 3 件回写
        //     实体（「接受 2 / 留 3」逐位）——cap 边界合并语义两向钉。
        rig.hoppers.setSlot(x, y, z, 4, int(BR::Cobble), 62);
        rig.items.spawnItem(x, y + 1, z, int(BR::Cobble), 5);
        rig.drive(1);
        const int st6 = findAlive(rig.items, int(BR::Cobble));
        const bool a6 = rig.hoppers.slotCountAt(x, y, z, 4) == 64
            && st6 >= 0 && rig.items.countAt(st6) == 3;
        ok = ok && a6;
        if (!a6) diag += QStringLiteral("[a6 s4=%1 ent=%2]")
                            .arg(rig.hoppers.slotCountAt(x, y, z, 4))
                            .arg(st6 >= 0 ? rig.items.countAt(st6) : -1);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2052a hopper collection wall (above+self cell vacuum, whole-stack slot-exact, metadata, full/partial reject)"
            << (ok ? QString() : diag);
    });

    // ── r2052b：抽取 + 输出承重墙（上抽 / 前推 / 漏斗链 / 挂起 / 熔炉定向）──────────────
    runLeg("r2052b hopper extract and push wall (above first-nonempty, facing target, chain, suspend, furnace routing)", [&]() {
        HopperRig rig;
        bool ok = true;
        QString diag;
        const int x = 16, z = 16;
        const int y = findHopperRigY(rig.w, x, z);
        if (y <= 0) { ++totalFail; qInfo().noquote() << "FAIL | r2052b leg (no rig space)"; return; }

        // (1) 前推：排料口朝 +X（state=0）+ 前方箱子 -> 一轮恰 1 件入箱槽 0（转移粒度 = 1 件，MC 口径）。
        rig.placeHopper(x, y, z, 0);
        rig.w.setBlock(x + 1, y, z, BR::Chest, 0);
        rig.hoppers.setSlot(x, y, z, 0, RecipeRegistry::IronIngotId, 3);
        rig.drive(1);
        const bool b1 = rig.chests.slotIdAt(x + 1, y, z, 0) == RecipeRegistry::IronIngotId
            && rig.chests.slotCountAt(x + 1, y, z, 0) == 1
            && rig.hoppers.slotCountAt(x, y, z, 0) == 2;
        ok = ok && b1;
        if (!b1) diag += QStringLiteral("[b1 c=%1/%2 h=%3]")
                            .arg(rig.chests.slotCountAt(x + 1, y, z, 0))
                            .arg(rig.chests.slotIdAt(x + 1, y, z, 0))
                            .arg(rig.hoppers.slotCountAt(x, y, z, 0));

        // (2) 无目标挂起：排料口朝 +X 但目标格 Air -> 槽内容不动不销毁（挂起语义）。
        const int x2 = x + 6;
        rig.w.setBlock(x2 + 1, y, z, BR::Air, 0); // 目标格显式清空（探针环境契约空，t769 凿净空带先例）
        rig.placeHopper(x2, y, z, 0);
        rig.hoppers.setSlot(x2, y, z, 0, int(BR::Dirt), 7);
        rig.drive(3);
        const bool b2 = rig.hoppers.slotIdAt(x2, y, z, 0) == int(BR::Dirt)
            && rig.hoppers.slotCountAt(x2, y, z, 0) == 7;
        ok = ok && b2;
        if (!b2) diag += QStringLiteral("[b2 id=%1 cnt=%2]")
                            .arg(rig.hoppers.slotIdAt(x2, y, z, 0)).arg(rig.hoppers.slotCountAt(x2, y, z, 0));

        // (3) 漏斗链传递：A 朝 +X 推入同向漏斗 B -> 一轮恰 1 件自 A 槽 0 入 B 槽 0（链传递语义）。
        const int x3 = x2 + 6;
        rig.placeHopper(x3, y, z, 0);
        rig.placeHopper(x3 + 1, y, z, 0);
        rig.hoppers.setSlot(x3, y, z, 0, int(BR::Sand), 4);
        rig.drive(1);
        const bool b3 = rig.hoppers.slotCountAt(x3, y, z, 0) == 3
            && rig.hoppers.slotIdAt(x3 + 1, y, z, 0) == int(BR::Sand)
            && rig.hoppers.slotCountAt(x3 + 1, y, z, 0) == 1;
        ok = ok && b3;
        if (!b3) diag += QStringLiteral("[b3 a=%1 b=%2]")
                            .arg(rig.hoppers.slotCountAt(x3, y, z, 0))
                            .arg(rig.hoppers.slotCountAt(x3 + 1, y, z, 0));
        // (4) 上抽首非空槽：上方箱子槽 0 空 / 槽 3 有煤 -> 抽取自槽 3（扫描序 0 起首个非空——任务书
        //     「槽 0」为其特例），一轮恰 1 件入自身；箱槽 3 余 1。
        const int y4 = y + 6;
        rig.w.setBlock(x, y4, z, BR::Chest, 0);
        rig.placeHopper(x, y4 - 1, z, BlockRegistry::HopperFacingDownFlag);
        rig.chests.setSlot(x, y4, z, 3, RecipeRegistry::CoalId, 2);
        rig.drive(1);
        const bool b4 = rig.hoppers.slotIdAt(x, y4 - 1, z, 0) == RecipeRegistry::CoalId
            && rig.hoppers.slotCountAt(x, y4 - 1, z, 0) == 1
            && rig.chests.slotCountAt(x, y4, z, 3) == 1;
        ok = ok && b4;
        if (!b4) diag += QStringLiteral("[b4 hop=%1/%2 chest=%3]")
                            .arg(rig.hoppers.slotIdAt(x, y4 - 1, z, 0))
                            .arg(rig.hoppers.slotCountAt(x, y4 - 1, z, 0))
                            .arg(rig.chests.slotCountAt(x, y4, z, 3));

        // (5) 熔炉抽取定向：上方熔炉只从 out 产物槽（2）抽取（炉料槽 0 / 燃料槽 1 不动——MC 口径不偷吃）。
        const int x5 = x + 18;
        rig.w.setBlock(x5, y4, z, BR::Furnace, 0);
        rig.placeHopper(x5, y4 - 1, z, BlockRegistry::HopperFacingDownFlag);
        rig.furn.setSlot(x5, y4, z, FurnaceStore::kSlotIn, int(BR::IronOre), 8);
        rig.furn.setSlot(x5, y4, z, FurnaceStore::kSlotFuel, RecipeRegistry::CoalId, 4);
        rig.furn.setSlot(x5, y4, z, FurnaceStore::kSlotOut, RecipeRegistry::IronIngotId, 2);
        rig.drive(1);
        const bool b5 = rig.hoppers.slotIdAt(x5, y4 - 1, z, 0) == RecipeRegistry::IronIngotId
            && rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotOut) == 1
            && rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotIn) == 8
            && rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotFuel) == 4;
        ok = ok && b5;
        if (!b5) diag += QStringLiteral("[b5 hop=%1 out=%2 in=%3 fuel=%4]")
                            .arg(rig.hoppers.slotIdAt(x5, y4 - 1, z, 0))
                            .arg(rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotOut))
                            .arg(rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotIn))
                            .arg(rig.furn.slotCountAt(x5, y4, z, FurnaceStore::kSlotFuel));

        // (6) 熔炉推入定向：朝下漏斗持铁原矿 -> 推入下方熔炉 in 槽（0）；侧向漏斗持煤 -> 推入 fuel 槽（1）。
        const int x6 = x5 + 6;
        rig.w.setBlock(x6, y4, z, BR::Furnace, 0);
        rig.placeHopper(x6, y4 + 1, z, BlockRegistry::HopperFacingDownFlag); // 炉上方，排料口直指炉
        rig.hoppers.setSlot(x6, y4 + 1, z, 0, int(BR::IronOre), 3);
        rig.w.setBlock(x6, y4, z + 3, BR::Furnace, 0); // 侧方熔炉在排料口 +X 直指格
        rig.placeHopper(x6 - 1, y4, z + 3, 0); // 排料口朝 +X 直指侧方熔炉
        rig.hoppers.setSlot(x6 - 1, y4, z + 3, 0, RecipeRegistry::CoalId, 5);
        rig.drive(1);
        const bool b6 = rig.furn.slotIdAt(x6, y4, z, FurnaceStore::kSlotIn) == int(BR::IronOre)
            && rig.furn.slotCountAt(x6, y4, z, FurnaceStore::kSlotIn) == 1
            && rig.hoppers.slotCountAt(x6, y4 + 1, z, 0) == 2
            && rig.furn.slotIdAt(x6, y4, z + 3, FurnaceStore::kSlotFuel) == RecipeRegistry::CoalId
            && rig.furn.slotCountAt(x6, y4, z + 3, FurnaceStore::kSlotFuel) == 1;
        ok = ok && b6;
        if (!b6) diag += QStringLiteral("[b6 in=%1/%2 hop=%3 fuel=%4/%5]")
                            .arg(rig.furn.slotIdAt(x6, y4, z, FurnaceStore::kSlotIn))
                            .arg(rig.furn.slotCountAt(x6, y4, z, FurnaceStore::kSlotIn))
                            .arg(rig.hoppers.slotCountAt(x6, y4 - 1, z, 0))
                            .arg(rig.furn.slotIdAt(x6, y4, z + 3, FurnaceStore::kSlotFuel))
                            .arg(rig.furn.slotCountAt(x6, y4, z + 3, FurnaceStore::kSlotFuel));

        // (7) 元数据随栈：箱槽 0 带耐久/名工具 -> 抽取入腔逐位保真（抽取链元数据保真承重）。
        const int x7 = x6 + 6;
        rig.w.setBlock(x7, y4, z, BR::Chest, 0);
        rig.placeHopper(x7, y4 - 1, z, BlockRegistry::HopperFacingDownFlag);
        rig.chests.setSlot(x7, y4, z, 0, int(ToolRegistry::HoeWood), 1, QVariantList(),
                            QStringLiteral("传箱锄"), 17);
        rig.drive(1);
        const bool b7 = rig.hoppers.slotIdAt(x7, y4 - 1, z, 0) == int(ToolRegistry::HoeWood)
            && rig.hoppers.slotDurabilityAt(x7, y4 - 1, z, 0) == 17
            && rig.hoppers.slotNameAt(x7, y4 - 1, z, 0) == QStringLiteral("传箱锄")
            && rig.chests.slotIdAt(x7, y4, z, 0) == 0;
        ok = ok && b7;
        if (!b7) diag += QStringLiteral("[b7 dur=%1 name=%2]")
                            .arg(rig.hoppers.slotDurabilityAt(x7, y4 - 1, z, 0))
                            .arg(rig.hoppers.slotNameAt(x7, y4 - 1, z, 0));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2052b hopper extract and push wall (above first-nonempty, facing target, chain, suspend, furnace routing)"
            << (ok ? QString() : diag);
    });

    // ── r2052c：红石锁停 + 边界墙（锁停三语义全停 / 无效应不消耗 / 解锁恢复 / 满仓 / 孤儿）──
    runLeg("r2052c hopper redstone lock-stop and boundary wall (powered stops all three, no-effect-no-consume, resume, full reject, orphan inert)", [&]() {
        HopperRig rig;
        bool ok = true;
        QString diag;
        const int x = 32, z = 32;
        const int y = findHopperRigY(rig.w, x, z);
        if (y <= 0) { ++totalFail; qInfo().noquote() << "FAIL | r2052c leg (no rig space)"; return; }

        // rig：漏斗朝 +X，前方箱子（输出面）；上方箱子（抽取面，槽 3 有煤）；上方格掉落物（收集面）。
        rig.placeHopper(x, y, z, 0);
        rig.w.setBlock(x + 1, y, z, BR::Chest, 0);
        rig.w.setBlock(x, y + 1, z, BR::Chest, 0);
        rig.hoppers.setSlot(x, y, z, 0, int(BR::Dirt), 4);
        rig.chests.setSlot(x, y + 1, z, 3, RecipeRegistry::CoalId, 2);

        // (1) 基线（未供电）：一轮抽取 1 件煤入腔（**抽取通道**为基线见证——本腿全部断言面不依赖输出
        //     语义，NEG-1 摘输出不连坐；输出通道由 Dirt 计数作「锁停见证」被动观测）。
        rig.drive(1);
        const int dirtAfterBaseline = rig.hoppers.slotCountAt(x, y, z, 0); // 输出通道基线快照（相对见证——
                                                                           //  锁停期计数不得再动）
        const bool c1 = rig.chests.slotCountAt(x, y + 1, z, 3) == 1
            && rig.hoppers.slotIdAt(x, y, z, 1) == RecipeRegistry::CoalId
            && rig.hoppers.slotCountAt(x, y, z, 1) == 1;
        ok = ok && c1;
        if (!c1) diag += QStringLiteral("[c1 ext=%1/%2]")
                            .arg(rig.chests.slotCountAt(x, y + 1, z, 3))
                            .arg(rig.hoppers.slotCountAt(x, y, z, 1));

        // (2) 锁停：邻格拉杆扳开（state bit0 = 电源激活）-> 三轮三语义全停、无效应不消耗口径
        //     （输出停 = 前箱不增 / 自槽不减；抽取停 = 上箱槽 3 不减；收集停 = 上方掉落物留场）。
        rig.w.setBlock(x - 1, y, z, BR::Lever, 1);
        rig.items.spawnItem(x, y, z, int(BR::Sand), 2); // 收集面待收实体（漏斗自身格——收集域含自身格；
                                                        //  上方格是抽取面箱子，收集域另一格恰被其占用）。
        rig.drive(3);
        const int stSand = findAlive(rig.items, int(BR::Sand));
        const bool c2 = rig.w.isReceivingPower(x, y, z) // rig 自证：漏斗确实被供电
            && rig.hoppers.slotCountAt(x, y, z, 0) == dirtAfterBaseline // 输出停（Dirt 三轮不动——锁停见证）
            && rig.chests.slotCountAt(x, y + 1, z, 3) == 1 // 抽取停（c1 已抽 1 -> 锁停期不再减）
            && stSand >= 0 && rig.items.countAt(stSand) == 2; // 收集停（实体留场）
        ok = ok && c2;
        if (!c2) diag += QStringLiteral("[c2 pw=%1 dirt=%2 ext=%3 sand=%4]")
                            .arg(rig.w.isReceivingPower(x, y, z))
                            .arg(rig.hoppers.slotCountAt(x, y, z, 0))
                            .arg(rig.chests.slotCountAt(x, y + 1, z, 3))
                            .arg(stSand >= 0 ? rig.items.countAt(stSand) : -1);

        // (3) 解锁恢复：拉杆扳回（state bit0=0，无源无粉 = 断电）-> 一轮输出 / 抽取恢复；
        //     断电期冷却照常推进（无追赶爆发——恰一轮一步进，节律钉）。
        rig.w.setBlock(x - 1, y, z, BR::Lever, 0);
        rig.drive(1);
        const bool c3 = !rig.w.isReceivingPower(x, y, z)
            && rig.chests.slotCountAt(x, y + 1, z, 3) == 0 // 抽取恢复恰 1 件（余 1 -> 0）
            && rig.hoppers.slotIdAt(x, y, z, 1) == RecipeRegistry::CoalId // 恢复轮煤入腔并入槽 1
            && rig.hoppers.slotCountAt(x, y, z, 1) == 2; // （同 id 未满槽合并——cap 内 1+1=2）
        ok = ok && c3;
        if (!c3) diag += QStringLiteral("[c3 push=%2 ext=%3]")
                            .arg(rig.chests.slotCountAt(x + 1, y, z, 0))
                            .arg(rig.chests.slotCountAt(x, y + 1, z, 3));

        // (4) 满仓边界：漏斗五槽全满 -> 上方箱子抽取挂起（箱槽不动）、输出挂起（自槽不动）。
        const int x4 = x + 16;
        rig.w.setBlock(x4, y + 1, z, BR::Chest, 0);
        rig.placeHopper(x4, y, z, 0);
        rig.chests.setSlot(x4, y + 1, z, 0, RecipeRegistry::CoalId, 6);
        for (int i = 0; i < HopperStore::kSlotsPerHopper; ++i)
            rig.hoppers.setSlot(x4, y, z, i, int(BR::Gravel), 64);
        rig.drive(2);
        const bool c4 = rig.chests.slotCountAt(x4, y + 1, z, 0) == 6 // 抽取挂起
            && rig.hoppers.slotCountAt(x4, y, z, 0) == 64 // 输出挂起（下方无目标且满）
            && rig.hoppers.slotIdAt(x4, y, z, 0) == int(BR::Gravel);
        ok = ok && c4;
        if (!c4) diag += QStringLiteral("[c4 chest=%5 self=%6/%7]")
                            .arg(rig.chests.slotCountAt(x4, y + 1, z, 0))
                            .arg(rig.hoppers.slotCountAt(x4, y, z, 0))
                            .arg(rig.hoppers.slotIdAt(x4, y, z, 0));

        // (5) 孤儿条目 inert：条目在场但格上方块已非漏斗（爆炸 / 岩浆破坏的降级面）-> 引擎跳过不崩不误写。
        const int x5 = x4 + 8;
        rig.placeHopper(x5, y, z, 0);
        rig.hoppers.setSlot(x5, y, z, 0, int(BR::Dirt), 2);
        rig.w.setBlock(x5, y, z, BR::Air, 0); // 摘块留条目（孤儿）
        rig.drive(2);
        const bool c5 = rig.hoppers.slotIdAt(x5, y, z, 0) == int(BR::Dirt) // 条目 inert 不消费
            && rig.hoppers.slotCountAt(x5, y, z, 0) == 2;
        ok = ok && c5;
        if (!c5) diag += QStringLiteral("[c5 orphan consumed]")
                            ;

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2052c hopper redstone lock-stop and boundary wall (powered stops all three, no-effect-no-consume, resume, full reject, orphan inert)"
            << (ok ? QString() : diag);
    });

    // ── r2052d：结构钉 + 持久化往返（def 行 / 解码权威 / 注册族 / 真库往返 / 反探）────────
    runLeg("r2052d hopper structure pins and persistence round-trip (def row, out-delta authority, registration family, real sqlite, QML zero-touch)", [&]() {
        bool ok = true;
        QString diag;

        // (1) def 行逐字段（Core 单一权威表；追加不插中间——id 145 = 存档格式契约）。
        //     t1083 lawful 修订：Jukebox=146 尾部追加后漏斗不再是最末方块（Count 146→147、图集
        //     189→191——唱片机两瓦片追加不插中间）。钉的语义 =「漏斗行契约逐位不变 + 尾部追加
        //     不重排」，非「漏斗恒最末」→ 钉值随注册表尾部增长同步前移（t1077 同门先例）。
        const auto &hd = BR::def(BR::Hopper);
        //     t1093 lawful 修订：漏斗异形翻案（solid true→false / ShapeFull→ShapeHopper 三盒，
        //     hopperShapeBoxes 单一权威）——钉的语义 =「漏斗行契约逐位不变」，非「恒整立方降级」
        //     （t1083 Count 前移同门先例：钉值随本方块属性翻案同步）。
        const bool d1 = hd.id == 145 && !hd.solid && hd.shape == BR::ShapeHopper
            && hd.hardness == 3.0f && hd.toolType == int(BR::Pickaxe)
            && hd.requiresTool && hd.minToolTier == 1
            && hd.dropId == int(BR::Hopper) && hd.dropCount == 1 && hd.maxStack == 64
            && hd.topTile == 186 && hd.bottomTile == 187 && hd.sideTile == 187 && hd.frontTile == 188
            && QLatin1String(hd.name) == QLatin1String("hopper")
            && QLatin1String(hd.display) == QLatin1String("漏斗")
            && int(BR::Jukebox) == 146 && int(BR::Count) == 151 && BR::mcBlockId(quint8(BR::Repeater)) == 93 // t1103 lawful 前移：Count 149→151（Melon=149/MelonStem=150 尾部追加；t1097 曾 148→149）；mcBlockId(Repeater)==93 行为级钉不变
            && BR::AtlasTileCount == 201; // t1103 lawful 前移：195→201（西瓜族 tile 195..200 追加；t1097 曾 193→195）
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1 id=%2 cnt=%3]")
                            .arg(hd.id).arg(int(BR::Count));

        // (2) 排料口解码单一权威（placement / scanHoppers 输出 / mesher 前贴图三方同源）。
        int dx = 9, dy = 9, dz = 9;
        BR::hopperOutDelta(quint8(BlockRegistry::HopperFacingDownFlag), dx, dy, dz);
        const bool down = dx == 0 && dy == -1 && dz == 0;
        BR::hopperOutDelta(quint8(BlockRegistry::HopperFacingDownFlag | 3), dx, dy, dz); // 朝下时低 2 位 inert
        const bool downInert = dx == 0 && dy == -1 && dz == 0;
        BR::hopperOutDelta(quint8(0), dx, dy, dz);
        const bool px = dx == 1 && dy == 0 && dz == 0;
        BR::hopperOutDelta(quint8(1), dx, dy, dz);
        const bool nx = dx == -1 && dy == 0 && dz == 0;
        BR::hopperOutDelta(quint8(2), dx, dy, dz);
        const bool pz = dx == 0 && dy == 0 && dz == 1;
        BR::hopperOutDelta(quint8(3), dx, dy, dz);
        const bool nz = dx == 0 && dy == 0 && dz == -1;
        const bool d2 = down && downInert && px && nx && pz && nz;
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2 %1%2%3%4%5%6]")
                            .arg(down).arg(downInert).arg(px).arg(nx).arg(pz).arg(nz);

        // (3) 配方命中 / 负例：5 铁锭 + 1 箱子（III / ICI，工作台有序）-> 恰漏斗 x1；
        //     平移摆位（最小包围盒规则）仍命中；无箱 5 锭 / 2x2 摆不下 -> nullptr（无效应不消耗权威面）。
        const int gHit[9] = { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
                              RecipeRegistry::IronIngotId, int(BR::Chest), RecipeRegistry::IronIngotId,
                              0, 0, 0 };
        const RecipeRegistry::Recipe *hit = RecipeRegistry::match(gHit, 3);
        const int gOff[9] = { 0, 0, 0, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
                              RecipeRegistry::IronIngotId, int(BR::Chest), RecipeRegistry::IronIngotId }; // 整体下移一行（最小包围盒平移规则）
        const RecipeRegistry::Recipe *off = RecipeRegistry::match(gOff, 3);
        const int gNo[9] = { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
                             RecipeRegistry::IronIngotId, 0, RecipeRegistry::IronIngotId, 0, 0, 0 };
        const RecipeRegistry::Recipe *noChest = RecipeRegistry::match(gNo, 3);
        const bool d3 = hit && hit->outputId == int(BR::Hopper) && hit->outputCount == 1
            && off && off->outputId == int(BR::Hopper) // 平移摆位仍命中（最小包围盒规则）
            && (noChest == nullptr || noChest->outputId != int(BR::Hopper)); // 无箱不得产漏斗
        ok = ok && d3;
        if (!d3) diag += QStringLiteral("[d3 hit=%1 nc=%2]")
                            .arg(hit ? hit->outputId : -1).arg(noChest != nullptr);

        // (4) 注册族源钉（剥注释口径 pinSet；跨层互钉落本测试 TU 合法 include 全栈）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QStringList missBr = pinSet(srcRoot + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("id row", "Hopper           = 145", 1),
            SrcPin("down flag", "HopperFacingDownFlag = 0x04", 1),
            SrcPin("out delta", "static void hopperOutDelta", 1)});
        //     t1093 lawful 修订：漏斗 tileFor 分支随 PASS2→PASS1 迁移退役（tileFor 不再触达漏斗），
        //     钉改指 PASS 1 异形收纳行（同语义「mesher 漏斗贴图路由面」新落点）。
        const QStringList missMsh = pinSet(srcRoot + QStringLiteral("/World/meshbuilder.cpp"), {
            SrcPin("partial route", "|| b == BlockRegistry::Hopper", 1)});
        const QStringList missHb = pinSet(srcRoot + QStringLiteral("/Game/hotbar.cpp"), {
            SrcPin("creative row", "int(BlockRegistry::Hopper),", 1)});
        const QStringList missRec = pinSet(srcRoot + QStringLiteral("/Game/recipe.cpp"), {
            SrcPin("recipe row", "int(BlockRegistry::Hopper), 1, 1, ", 1)});
        const QStringList missPc = pinSet(srcRoot + QStringLiteral("/Game/playercontroller.cpp"), {
            SrcPin("tick wiring", "scanHoppers(dt);", 1),
            SrcPin("engine entry", "void PlayerController::scanHoppers", 1),
            SrcPin("place register", "m_hopperStore->setHopperCooldown(tx, ty, tz, 0.0);", 1),
            SrcPin("break drop", "m_hopperStore->clearHopper(x, y, z);", 1)});
        const QStringList missSb = pinSet(srcRoot + QStringLiteral("/Game/savebridge.h"), {
            SrcPin("bridge param", "const QVariantList &hoppers = {}", 1)});
        const QStringList missSc = pinSet(srcRoot + QStringLiteral("/World/savecoordinator.h"), {
            SrcPin("request field", "QVariantList hoppers;", 1)});
        const QStringList allMiss = missBr + missMsh + missHb + missRec + missPc + missSb + missSc;
        ok = ok && allMiss.isEmpty();
        if (!allMiss.isEmpty()) diag += QStringLiteral("[pins %1] ").arg(allMiss.join(u','));

        // (5) QML 装配钉 + UI 面反探：Main.qml 四接线（实例化 / 注入 / 存 / 读）在旁。
        //     t1093 lawful 修订：开盖 UI 交付（t1080 登记的 UI 候选授权面兑现）——反探从
        //     「全 QML 树无 HopperUI 文件（UI 降级）」改为「恰一份 HopperUI.qml（授权面单文件，
        //     禁第二份 UI 复制品）」。
        const QString mainQml = srcRoot + QStringLiteral("/ui/Main.qml");
        QFile mf(mainQml);
        const QString mainTxt = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        const bool d5 = !mainTxt.isEmpty()
            && mainTxt.contains(QLatin1String("HopperStore { id: hopperStore }"))
            && mainTxt.contains(QLatin1String("hopperStore: hopperStore"))
            && mainTxt.contains(QLatin1String("hopperStore.allHoppers()"))
            && mainTxt.contains(QLatin1String("hopperStore.loadAll(worldStore.loadHoppers())"));
        ok = ok && d5;
        if (!d5) diag += QStringLiteral("[d5 main wiring] ");
        int qmlUiFiles = 0;
        QDirIterator it(srcRoot + QStringLiteral("/ui"), QStringList() << QStringLiteral("HopperUI*"),
            QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); ++qmlUiFiles; } // 反探口径 = 无新 UI **文件**（注释词元不误伤）
        ok = ok && qmlUiFiles == 1;
        if (qmlUiFiles != 1) diag += QStringLiteral("[d5 ui exists x%1] ").arg(qmlUiFiles);

        // (6) hoppers 表真 SQLite 往返（t1016 临时库先例）：allHoppers 产物 -> saveAll 末参 ->
        //     关库重开 loadHoppers() -> {x,y,z,slots:[{id,count}x5]} 逐位；旧 caller 缺省参（不传
        //     hoppers）写后读回 = 空（全量重写 DELETE 语义，无残留）。
        HopperStore storeF;
        storeF.setSlot(3, 40, 5, 0, int(BR::Dirt), 12);
        storeF.setSlot(3, 40, 5, 1, RecipeRegistry::IronIngotId, 3);
        storeF.setSlot(-7, 41, -2, 4, RecipeRegistry::CoalId, 9); // 负坐标键
        const QVariantList fixture = storeF.allHoppers();
        World wT;
        initHopperWorld(wT);
        WorldStore ws;
        ws.setWorld(&wT);
        const QString dbT = QDir::temp().absoluteFilePath(
            QStringLiteral("voxel_t1080_probe_%1.sqlite").arg(QCoreApplication::applicationPid()));
        QFile::remove(dbT);
        bool d6 = ws.openWorld(dbT)
            && ws.saveAll(QStringLiteral("t1080rig"), QVariantList(), QVariantList(), QVariantList(),
                          QVariantMap(), QVariantMap(), fixture);
        ws.closeWorld();
        QVariantList back;
        if (d6 && ws.openWorld(dbT)) back = ws.loadHoppers();
        ws.closeWorld();
        d6 = d6 && back.size() == 2;
        const auto findEntry = [&back](int ex, int ey, int ez) -> QVariantMap {
            for (const QVariant &v : back) {
                const QVariantMap m = v.toMap();
                if (m.value(QStringLiteral("x")).toInt() == ex && m.value(QStringLiteral("y")).toInt() == ey
                    && m.value(QStringLiteral("z")).toInt() == ez)
                    return m;
            }
            return QVariantMap();
        };
        const QVariantMap e1 = findEntry(3, 40, 5);
        const QVariantMap e2 = findEntry(-7, 41, -2);
        const QVariantList s1 = e1.value(QStringLiteral("slots")).toList();
        const QVariantList s2 = e2.value(QStringLiteral("slots")).toList();
        d6 = d6 && s1.size() == 5 && s2.size() == 5
            && s1.at(0).toMap().value(QStringLiteral("id")).toInt() == int(BR::Dirt)
            && s1.at(0).toMap().value(QStringLiteral("count")).toInt() == 12
            && s1.at(1).toMap().value(QStringLiteral("id")).toInt() == RecipeRegistry::IronIngotId
            && s1.at(1).toMap().value(QStringLiteral("count")).toInt() == 3
            && s2.at(4).toMap().value(QStringLiteral("id")).toInt() == RecipeRegistry::CoalId
            && s2.at(4).toMap().value(QStringLiteral("count")).toInt() == 9;
        // 旧 caller 形态（缺省 hoppers 参）-> 表清空（全量重写语义：DELETE + 空插 = 无漏斗残留）。
        if (ws.openWorld(dbT))
            ws.saveAll(QStringLiteral("t1080rig")); // 四参旧调用形态
        ws.closeWorld();
        QVariantList legacy;
        if (ws.openWorld(dbT)) legacy = ws.loadHoppers();
        ws.closeWorld();
        d6 = d6 && legacy.isEmpty();
        QFile::remove(dbT);
        ok = ok && d6;
        if (!d6) diag += QStringLiteral("[d6 n=%1] ").arg(back.size());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2052d hopper structure pins and persistence round-trip (def row, out-delta authority, registration family, real sqlite, QML zero-touch)"
            << (ok ? QString() : diag);
    });
}
