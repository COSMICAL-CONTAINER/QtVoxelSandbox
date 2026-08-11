#include "resourcepackmanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QStandardPaths>

// 资源包解析 + 图集合成。详见 resourcepackmanager.h 的总体设计 / 红线（PLAN §9）。
// 引擎瓦片尺寸（与 build_atlas.py TILE=16 + chunkgeometry UV 同源）。
constexpr int kTile = ResourcePackManager::kTile;

namespace {

// 合成结果 + active 态 + t415 运行期可改配置的进程全局缓存（资源包配置是 process-global）。
// 由 stateMutex() 保护：apply()（GUI 线程）重建 / 落盘 atlasFile 与 atlasSource()（GUI 线程）读互斥，
// 防 QImage 在被 painter 修改时被另一线程采样（PLAN §2-E：保持运行而非崩溃；线程安全属 Core 叶子职责）。
struct BuiltState {
    bool built = false;
    bool active = false;
    QImage atlas;
    QString atlasFile;            // t415c 落盘的合成图集绝对路径（atlasSource 返回 file:///<atlasFile>）
    bool enabled = false;         // t415 镜像 settings.json resourcePackEnabled（缺省 false：避免无感切换）
    QString packPath;             // t415 镜像 settings.json resourcePack（空 = 走环境变量/默认探查）
    bool configLoaded = false;    // t415 config 是否已从 settings.json 加载（之后只信内存 + setter 持久化）
    int revision = 0;             // t415 apply() 重建计数（保留；file:// 不挂查询串，仅作历史/调试用）
    QString itemDir;              // t420 包内物品图标目录（assets/minecraft/textures/item）绝对路径；空 = 无 item 覆盖
    QString entityDir;            // t421 包内生物贴图目录（assets/minecraft/textures/entity）绝对路径；空 = 无 entity 覆盖
    QString blockDir;             // t456 包内方块贴图目录（assets/minecraft/textures/block）绝对路径；blockItemIconSource 兜底探测（pack 把前贴图放 block/ 时）
    // t489 流体条带落盘路径（active 时 file:///）；waterStrip = 2 列×32 帧（静水|流水），lavaStrip = 1 列×16 帧。
    QString waterStripFile;
    QString lavaStripFile;
};
BuiltState &state()
{
    static BuiltState s;
    return s;
}
QMutex &stateMutex()
{
    static QMutex m;
    return m;
}

// t420 ResourcePackManager 实例注册表：ToolIcon/MaterialIcon 各持一个实例查 itemIconSource，但 apply()
//   （pack 切换）只在一个实例（Main.qml 的 resourcePack）上调用 → 仅该实例 emit activeChanged。注册表让
//   apply() 向全部实例广播（同步 m_active + emit），使各 icon 的 active/itemIconSource 绑定随 pack 切换刷新。
//   GUI 线程内 QML 对象生命周期，注册表本身无需加锁（BuiltState 仍由 stateMutex 保护）。
QList<ResourcePackManager *> &rpInstances()
{
    static QList<ResourcePackManager *> l;
    return l;
}

// t419 浅层有界 DFS：在 root 子树内寻找路径以 assets/minecraft/textures/<leaf> 结尾的目录
//   （leaf = "block" 方块贴图目录 / "item" 物品图标目录，t420）。命中即返（DFS，首个即取，足够唯一）。
//   maxDepth 限制递归深度——pack 标准布局 <leaf> 在 root 下 4 层（assets/minecraft/textures/<leaf>），
//   留 2 层余量给 wrapper / 子包目录，避免扫遍巨大包树。
QString findTexturesSubDirBounded(const QDir &dir, const QString &leaf, int depth, int maxDepth)
{
    if (depth > maxDepth)
        return {};
    // 本层目录自身即目标子目录（root 自身命中：用户直选 .../textures/<leaf>；或递归过程中命中）。
    if (dir.dirName() == leaf) {
        const QString path = QDir::cleanPath(dir.absolutePath());
        const QString suffix = QStringLiteral("/assets/minecraft/textures/") + leaf;
        const QString bare = QStringLiteral("assets/minecraft/textures/") + leaf;
        if (path.endsWith(suffix) || path == bare)
            return path;
    }
    const QStringList subs =
            dir.entryList(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (const QString &sub : subs) {
        const QString r = findTexturesSubDirBounded(QDir(dir.filePath(sub)), leaf, depth + 1, maxDepth);
        if (!r.isEmpty())
            return r;
    }
    return {};
}

// t419/t420 在任意层级 packPath 上定位 textures 子目录（leaf = "block" / "item"）：
//   1) <packPath>/assets/minecraft/textures/<leaf>（pack 根标准布局，最常见 → 快速直命中，避免递归开销）
//   2)/(3) packPath 即该子目录，或 packPath 是 pack 根 / 中间层（assets/minecraft/textures）：
//         浅层有界 DFS 在子树内找路径以 assets/minecraft/textures/<leaf> 结尾的目录（root 自身也在范围内）。
//   → 用户选 pack 根、子目录、或中间任意层，均可加载。返回该子目录绝对路径（cleanPath）；找不到为空。
QString resolveTexturesSubDir(const QString &absPath, const QString &leaf)
{
    if (absPath.isEmpty())
        return {};
    const QDir root(absPath);
    if (!root.exists())
        return {};
    const QString direct = root.filePath(QStringLiteral("assets/minecraft/textures/") + leaf);
    if (QFileInfo(direct).isDir())
        return QDir::cleanPath(direct);
    return findTexturesSubDirBounded(root, leaf, 0, 6);
}

// 方块贴图目录（t419，leaf="block"）。
QString resolveBlockDir(const QString &absPath)
{
    return resolveTexturesSubDir(absPath, QStringLiteral("block"));
}

// t420 物品图标目录（leaf="item"；同 packPath 解析，与 block 并列）。
QString resolveItemDir(const QString &absPath)
{
    return resolveTexturesSubDir(absPath, QStringLiteral("item"));
}

// t421 生物贴图目录（leaf="entity"；同 packPath 解析，与 block/item 并列）。
QString resolveEntityDir(const QString &absPath)
{
    return resolveTexturesSubDir(absPath, QStringLiteral("entity"));
}

// 合法包判定：能在 packPath（任意层级）上定位到 block 贴图目录（spec t419）。
bool isValidPack(const QString &absPath)
{
    return !resolveBlockDir(absPath).isEmpty();
}

// 相对路径 → 绝对（相对 exe 目录；exe 在 build/ → 解析到 <工程根>/...）。
QString absolutePackPath(const QString &p)
{
    if (p.isEmpty())
        return {};
    const QFileInfo fi(p);
    if (fi.isAbsolute())
        return QDir::cleanPath(p);
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(p));
}

// settings.json 候选位置：工程根（exe/..）→ 系统 AppLocalData。
QString resolveSettingsPath()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .absoluteFilePath(QStringLiteral("settings.json")),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c))
            return c;
    }
    return {};
}

// t415 settings.json 写入路径：优先已存在文件（沿用读路径），否则首个候选（exe/../settings.json，
//   dev 期可写；不可写则 open 失败降级告警）。
QString resolveSettingsWritePath()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .absoluteFilePath(QStringLiteral("settings.json")),
    };
    for (const QString &c : candidates)
        if (QFile::exists(c))
            return c;
    return candidates.first();
}

// 默认探查：resourcepacks/active/ → dev 包 docs/Default HD 128x Demo 1.8.2.2/（spec t414）。
//   路径相对 exe 目录与工程根各试一次（dev 期 exe 在 build/ → ../ 指工程根；部署期 exe 直立 → 无 ../）。
QString discoverDefault()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList rels = {
        QStringLiteral("resourcepacks/active"),
        QStringLiteral("../resourcepacks/active"),
        QStringLiteral("docs/Default HD 128x Demo 1.8.2.2"),
        QStringLiteral("../docs/Default HD 128x Demo 1.8.2.2"),
    };
    for (const QString &r : rels) {
        const QString abs = absolutePackPath(r);
        // absolutePackPath 已按 exe 目录解析；再补工程根视角（exeDir/.. 下）。
        if (isValidPack(abs))
            return abs;
        const QString rootAbs = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(r);
        if (isValidPack(QDir::cleanPath(rootAbs)))
            return QDir::cleanPath(rootAbs);
    }
    return {};
}

// 读 settings.json：resourcePackEnabled（缺省 false）+ resourcePack（可空）。
struct Settings {
    bool enabled = false;
    QString packPath;
};
Settings readSettings()
{
    Settings s;
    const QString path = resolveSettingsPath();
    if (path.isEmpty())
        return s;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return s;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("resourcePackEnabled")))
        s.enabled = obj.value(QStringLiteral("resourcePackEnabled")).toBool(false);
    if (obj.contains(QStringLiteral("resourcePack")))
        s.packPath = obj.value(QStringLiteral("resourcePack")).toString();
    return s;
}

// t415 写 settings.json（enabled + packPath），保留其它已有字段。返回是否成功（失败已告警，调用方降级）。
bool writeSettings(bool enabled, const QString &packPath)
{
    const QString path = resolveSettingsWritePath();
    if (path.isEmpty()) {
        qWarning("ResourcePack: 无法解析 settings.json 写入路径；配置仅存内存。");
        return false;
    }
    // 读现有（保留其它字段）。
    QJsonObject obj;
    QFile fin(path);
    if (fin.open(QIODevice::ReadOnly)) {
        const QJsonDocument d = QJsonDocument::fromJson(fin.readAll());
        obj = d.object();
        fin.close();
    }
    obj.insert(QStringLiteral("resourcePackEnabled"), enabled);
    obj.insert(QStringLiteral("resourcePack"), packPath);
    QFile fout(path);
    if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("ResourcePack: 无法写入 settings.json %s（%s）；配置仅存内存。",
                 qPrintable(path), qPrintable(fout.errorString()));
        return false;
    }
    fout.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    fout.close();
    return true;
}

// 「引擎 tile 索引 → 包内标准贴图文件名」映射（与 tools/build_atlas.py TILES 顺序对齐，t415 补全全部
//   有 MC 等价物的瓦片）。缺失的文件名由 ensureBuiltLocked 安全跳过（不覆盖 = 保留程序生成瓦片），
//   故映射可慷慨、零渗色风险：唯一要保证的是 tile↔文件名配对正确（配错会渗色），而非文件名都存在。
//   文件名用现代（1.13+ flattening）标准 block 贴图命名（如 grass_block_top、oak_log、white_wool），
//   与现网大多数资源包一致；旧版包缺名时安全跳过（保程序生成瓦片），不崩。
const QList<QPair<int, QString>> &tileFilenameMap()
{
    static const QList<QPair<int, QString>> kMap = {
        {0, QStringLiteral("grass_block_top.png")},      // grass_top
        {1, QStringLiteral("grass_block_side.png")},     // grass_side
        {2, QStringLiteral("dirt.png")},                 // dirt
        {3, QStringLiteral("stone.png")},                // stone
        {4, QStringLiteral("sand.png")},                 // sand
        {5, QStringLiteral("cobblestone.png")},          // cobble
        {6, QStringLiteral("oak_log_top.png")},          // log_top
        {7, QStringLiteral("oak_log.png")},              // log_side（MC oak_log = 侧面贴图）
        {8, QStringLiteral("oak_planks.png")},           // planks
        {9, QStringLiteral("oak_leaves.png")},           // leaves
        {10, QStringLiteral("crafting_table_top.png")},  // crafting_table_top
        {11, QStringLiteral("crafting_table_side.png")}, // crafting_table_side
        {12, QStringLiteral("furnace_top.png")},         // furnace_top
        {13, QStringLiteral("furnace_side.png")},        // furnace_side
        {14, QStringLiteral("furnace_front.png")},       // furnace_front（未点燃态）
        {15, QStringLiteral("coal_ore.png")},            // coal_ore
        {16, QStringLiteral("iron_ore.png")},            // iron_ore
        {17, QStringLiteral("torch.png")},               // torch
        {18, QStringLiteral("bedrock.png")},             // bedrock
        {19, QStringLiteral("water_still.png")},         // water 静水
        {20, QStringLiteral("chest_top.png")},           // chest_top（多数包此贴图在 entity/chest/，缺则跳过）
        {21, QStringLiteral("chest_side.png")},          // chest_side
        {22, QStringLiteral("chest_front.png")},         // chest_front
        {23, QStringLiteral("water_flow.png")},          // water_flow 流水
        {24, QStringLiteral("water_still.png")},         // water 静水动画第二帧（flipbook；包内单帧 → 静止）
        {25, QStringLiteral("water_flow.png")},          // water_flow 流水动画第二帧（flipbook）
        {26, QStringLiteral("farmland.png")},            // farmland_dry 干耕地
        {27, QStringLiteral("farmland_moist.png")},      // farmland_wet 湿耕地
        {28, QStringLiteral("tall_grass.png")},          // tall_grass 草丛 cross
        {29, QStringLiteral("wheat_stage_0.png")},       // wheat_stage_0
        {30, QStringLiteral("wheat_stage_1.png")},       // wheat_stage_1
        {31, QStringLiteral("wheat_stage_2.png")},       // wheat_stage_2
        {32, QStringLiteral("wheat_stage_3.png")},       // wheat_stage_3
        {33, QStringLiteral("wheat_stage_4.png")},       // wheat_stage_4
        {34, QStringLiteral("wheat_stage_5.png")},       // wheat_stage_5
        {35, QStringLiteral("wheat_stage_6.png")},       // wheat_stage_6
        {36, QStringLiteral("wheat_stage_7.png")},       // wheat_stage_7
        {37, QStringLiteral("diamond_ore.png")},         // diamond_ore
        {38, QStringLiteral("white_wool.png")},          // wool 羊毛（奶白基底色）
        {39, QStringLiteral("oak_sapling.png")},         // sapling 树苗 cross
        {40, QStringLiteral("copper_ore.png")},          // copper_ore
        {41, QStringLiteral("gold_ore.png")},            // gold_ore
        {42, QStringLiteral("lava_still.png")},          // lava 岩浆
        {43, QStringLiteral("red_bed.png")},             // bed_red
        {44, QStringLiteral("orange_bed.png")},          // bed_orange
        {45, QStringLiteral("yellow_bed.png")},          // bed_yellow
        {46, QStringLiteral("green_bed.png")},           // bed_green
        {47, QStringLiteral("cyan_bed.png")},            // bed_cyan
        {48, QStringLiteral("blue_bed.png")},            // bed_blue
        {49, QStringLiteral("magenta_bed.png")},         // bed_magenta
        {50, QStringLiteral("black_bed.png")},           // bed_black
        {51, QStringLiteral("spawner.png")},             // spawner 刷怪笼
        {52, QStringLiteral("sandstone_top.png")},       // sandstone_top
        {53, QStringLiteral("sandstone_side.png")},      // sandstone_side
        {54, QStringLiteral("cactus_top.png")},          // cactus_top
        {55, QStringLiteral("cactus_side.png")},         // cactus_side
        {56, QStringLiteral("dead_bush.png")},           // dead_bush 枯死灌木 cross
        {57, QStringLiteral("snow.png")},                // snow 积雪
        {58, QStringLiteral("ice.png")},                 // ice 冰
        {59, QStringLiteral("spruce_log_top.png")},      // spruce_log_top
        {60, QStringLiteral("spruce_log.png")},          // spruce_log_side（MC spruce_log = 侧面贴图）
        {61, QStringLiteral("lily_pad.png")},            // lily_pad 睡莲
        {62, QStringLiteral("red_mushroom.png")},        // mushroom 蘑菇（红底白斑）
        {63, QStringLiteral("poppy.png")},               // flower_red（MC 罂粟 poppy）
        {64, QStringLiteral("dandelion.png")},           // flower_yellow（MC 蒲公英）
        {65, QStringLiteral("blue_orchid.png")},         // flower_blue（MC 蓝花兰；最接近的蓝色花）
        {66, QStringLiteral("oxeye_daisy.png")},         // flower_white（MC 雏菊）
        {67, QStringLiteral("sugar_cane.png")},          // sugarcane 甘蔗 cross
        {68, QStringLiteral("glass.png")},               // glass 玻璃
        {69, QStringLiteral("carrots_stage_0.png")},     // carrot_crop_0
        {70, QStringLiteral("carrots_stage_1.png")},     // carrot_crop_1
        {71, QStringLiteral("carrots_stage_2.png")},     // carrot_crop_2
        {72, QStringLiteral("carrots_stage_3.png")},     // carrot_crop_3
        {73, QStringLiteral("potatoes_stage_0.png")},    // potato_crop_0
        {74, QStringLiteral("potatoes_stage_1.png")},    // potato_crop_1
        {75, QStringLiteral("potatoes_stage_2.png")},    // potato_crop_2
        {76, QStringLiteral("potatoes_stage_3.png")},    // potato_crop_3
        {77, QStringLiteral("obsidian.png")},            // obsidian 黑曜石
        {78, QStringLiteral("ladder.png")},              // ladder 木梯 cross
        // t455 16 色 wool 其余 15 色变体（white 复用 tile 38 white_wool；缺则跳过保程序生成瓦片）。
        {79, QStringLiteral("orange_wool.png")},         // wool_orange
        {80, QStringLiteral("magenta_wool.png")},        // wool_magenta
        {81, QStringLiteral("light_blue_wool.png")},     // wool_light_blue
        {82, QStringLiteral("yellow_wool.png")},         // wool_yellow
        {83, QStringLiteral("lime_wool.png")},           // wool_lime
        {84, QStringLiteral("pink_wool.png")},           // wool_pink
        {85, QStringLiteral("gray_wool.png")},           // wool_gray
        {86, QStringLiteral("light_gray_wool.png")},     // wool_light_gray
        {87, QStringLiteral("cyan_wool.png")},           // wool_cyan
        {88, QStringLiteral("purple_wool.png")},         // wool_purple
        {89, QStringLiteral("blue_wool.png")},           // wool_blue
        {90, QStringLiteral("brown_wool.png")},          // wool_brown
        {91, QStringLiteral("green_wool.png")},          // wool_green
        {92, QStringLiteral("red_wool.png")},            // wool_red
        {93, QStringLiteral("black_wool.png")},          // wool_black
        // t455 16 色床补齐 8 色新变体（既存 8 色床 tile 43..50；本段为新色）。
        {94, QStringLiteral("white_bed.png")},           // bed_white
        {95, QStringLiteral("light_blue_bed.png")},      // bed_light_blue
        {96, QStringLiteral("lime_bed.png")},            // bed_lime
        {97, QStringLiteral("pink_bed.png")},            // bed_pink
        {98, QStringLiteral("gray_bed.png")},            // bed_gray
        {99, QStringLiteral("light_gray_bed.png")},      // bed_light_gray
        {100, QStringLiteral("purple_bed.png")},         // bed_purple
        {101, QStringLiteral("brown_bed.png")},          // bed_brown
    };
    return kMap;
}

// t420「引擎物品 id → 包内 item 标准贴图文件名」映射（item-ids.md 单一权威；id 取 toolregistry.h ToolId /
//   recipe.h MaterialId / ArmorId 段）。文件名用现代（1.13+ flattening）标准 item 命名（wooden_pickaxe /
//   iron_ingot / cooked_beef ...），与现网大多数资源包 assets/minecraft/textures/item/ 一致。包内缺该 PNG
//   时 itemIconSource 安全跳过（回退自绘），故映射可慷慨：唯一要保证的是 id↔文件名配对正确，而非文件名都存在。
//   工具段 0x100..0x112（镐/锄/斧/铲/剑×木/石/铁 + 弓/剪刀/钓竿 + t472 钻石镐）；材料段 0x200..0x231（合成材料 / 食物 / 桶 /
//   mob 掉落 / 生物蛋 / 战利品 / 鸡鱿鱼族 / 胡萝卜马铃薯 / 生鱼）；护甲段 0x300..0x313（皮革/铁/金/钻石×4 部位；
//   铜护甲无 vanilla 贴图 → 不映射，回退自绘）。raw_*（铜/金/铁原矿物品 1.17+）/spawn_egg_*/oak_sapling 等
//   旧版 / HD 包常缺 → 缺则跳过回退自绘，不崩。
const QList<QPair<int, QString>> &itemFilenameMap()
{
    static const QList<QPair<int, QString>> kMap = {
        // —— 工具段（ToolId；item-ids.md §2）——
        {0x100, QStringLiteral("wooden_pickaxe.png")},  // 木镐
        {0x101, QStringLiteral("stone_pickaxe.png")},   // 石镐
        {0x102, QStringLiteral("iron_pickaxe.png")},    // 铁镐
        {0x103, QStringLiteral("wooden_hoe.png")},      // 木锄
        {0x104, QStringLiteral("stone_hoe.png")},       // 石锄
        {0x105, QStringLiteral("iron_hoe.png")},        // 铁锄
        {0x106, QStringLiteral("wooden_axe.png")},      // 木斧
        {0x107, QStringLiteral("stone_axe.png")},       // 石斧
        {0x108, QStringLiteral("iron_axe.png")},        // 铁斧
        {0x109, QStringLiteral("wooden_shovel.png")},   // 木铲
        {0x10A, QStringLiteral("stone_shovel.png")},    // 石铲
        {0x10B, QStringLiteral("iron_shovel.png")},     // 铁铲
        {0x10C, QStringLiteral("wooden_sword.png")},    // 木剑
        {0x10D, QStringLiteral("stone_sword.png")},     // 石剑
        {0x10E, QStringLiteral("iron_sword.png")},      // 铁剑
        {0x10F, QStringLiteral("bow.png")},             // 弓
        {0x110, QStringLiteral("shears.png")},          // 剪刀
        {0x111, QStringLiteral("fishing_rod.png")},     // 钓鱼竿
        {0x112, QStringLiteral("diamond_pickaxe.png")}, // t472 钻石镐
        // —— 材料段（MaterialId；item-ids.md §3-5）——
        {0x200, QStringLiteral("stick.png")},           // 木棒
        {0x201, QStringLiteral("coal.png")},            // 煤炭
        {0x202, QStringLiteral("raw_iron.png")},        // 铁原矿（1.17+ raw_iron；缺则跳过）
        {0x203, QStringLiteral("iron_ingot.png")},      // 铁锭
        {0x204, QStringLiteral("glass.png")},           // 玻璃（多数包在 block/；缺则跳过）
        {0x205, QStringLiteral("charcoal.png")},        // 木炭
        {0x206, QStringLiteral("bucket.png")},          // 铁桶（空）
        {0x207, QStringLiteral("water_bucket.png")},    // 装水铁桶
        {0x208, QStringLiteral("wheat_seeds.png")},     // 小麦种子
        {0x209, QStringLiteral("wheat.png")},           // 小麦物品
        {0x20A, QStringLiteral("bread.png")},           // 面包
        {0x20B, QStringLiteral("porkchop.png")},        // 生猪排
        {0x20C, QStringLiteral("beef.png")},            // 生牛肉
        {0x20D, QStringLiteral("leather.png")},         // 皮革
        {0x20E, QStringLiteral("white_wool.png")},      // 羊毛（多数包在 block/；缺则跳过）
        {0x20F, QStringLiteral("pig_spawn_egg.png")},   // 生物蛋（猪）
        {0x210, QStringLiteral("cow_spawn_egg.png")},   // 生物蛋（牛）
        {0x211, QStringLiteral("sheep_spawn_egg.png")}, // 生物蛋（羊）
        {0x212, QStringLiteral("diamond.png")},         // 钻石
        {0x213, QStringLiteral("zombie_spawn_egg.png")},// 生物蛋（蹒跚者；机制等价 zombie）
        {0x214, QStringLiteral("skeleton_spawn_egg.png")},// 生物蛋（骸骨；机制等价 skeleton）
        {0x215, QStringLiteral("creeper_spawn_egg.png")},// 生物蛋（潜行者；机制等价 creeper）
        {0x216, QStringLiteral("spider_spawn_egg.png")},// 生物蛋（蜘蛛）
        {0x217, QStringLiteral("bone.png")},            // 骨头
        {0x218, QStringLiteral("rotten_flesh.png")},    // 腐肉
        {0x219, QStringLiteral("string.png")},          // 线
        {0x21A, QStringLiteral("arrow.png")},           // 箭
        {0x21B, QStringLiteral("oak_sapling.png")},     // 树苗物品（多数包在 block/；缺则跳过）
        {0x21C, QStringLiteral("raw_copper.png")},      // 铜原矿（1.17+；缺则跳过）
        {0x21D, QStringLiteral("copper_ingot.png")},    // 铜锭（缺则跳过）
        {0x21E, QStringLiteral("raw_gold.png")},        // 金原矿（1.17+；缺则跳过）
        {0x21F, QStringLiteral("gold_ingot.png")},      // 金锭
        {0x220, QStringLiteral("lava_bucket.png")},     // 装岩浆铁桶
        {0x221, QStringLiteral("cooked_porkchop.png")}, // 熟猪排
        {0x222, QStringLiteral("cooked_beef.png")},     // 熟牛肉
        {0x223, QStringLiteral("cooked_mutton.png")},   // 熟羊肉
        {0x224, QStringLiteral("redstone.png")},        // 红石粉
        {0x225, QStringLiteral("saddle.png")},          // 马鞍
        {0x226, QStringLiteral("name_tag.png")},        // 命名牌
        {0x227, QStringLiteral("enchanted_book.png")},  // 附魔书占位
        {0x228, QStringLiteral("feather.png")},         // 羽毛
        {0x229, QStringLiteral("chicken.png")},         // 生鸡肉
        {0x22A, QStringLiteral("cooked_chicken.png")},  // 熟鸡肉
        {0x22B, QStringLiteral("egg.png")},             // 蛋
        {0x22C, QStringLiteral("chicken_spawn_egg.png")},// 生物蛋（鸡）
        {0x22D, QStringLiteral("ink_sac.png")},         // 墨囊
        {0x22E, QStringLiteral("squid_spawn_egg.png")}, // 生物蛋（鱿鱼）
        {0x22F, QStringLiteral("carrot.png")},          // 胡萝卜
        {0x230, QStringLiteral("potato.png")},          // 马铃薯
        {0x231, QStringLiteral("cod.png")},             // 生鱼（MC 1.0 raw fish = modern cod）
        // —— 护甲段（ArmorId；皮革/铁/金/钻石×头盔/胸甲/护腿/靴子。铜护甲无 vanilla 贴图 → 不映射）——
        {0x300, QStringLiteral("leather_helmet.png")},
        {0x301, QStringLiteral("leather_chestplate.png")},
        {0x302, QStringLiteral("leather_leggings.png")},
        {0x303, QStringLiteral("leather_boots.png")},
        {0x304, QStringLiteral("iron_helmet.png")},
        {0x305, QStringLiteral("iron_chestplate.png")},
        {0x306, QStringLiteral("iron_leggings.png")},
        {0x307, QStringLiteral("iron_boots.png")},
        {0x30C, QStringLiteral("golden_helmet.png")},
        {0x30D, QStringLiteral("golden_chestplate.png")},
        {0x30E, QStringLiteral("golden_leggings.png")},
        {0x30F, QStringLiteral("golden_boots.png")},
        {0x310, QStringLiteral("diamond_helmet.png")},
        {0x311, QStringLiteral("diamond_chestplate.png")},
        {0x312, QStringLiteral("diamond_leggings.png")},
        {0x313, QStringLiteral("diamond_boots.png")},
    };
    return kMap;
}

// t421「引擎 mob id（EntityManager::MobType）→ pack entity 子目录 + 标准贴图文件名」映射（功能性元数据，
//   红线 §9 可随代码提交；贴图文件本身不进仓库）。mob id 取 EntityManager::MobType（pig=1/cow=2/sheep=3/
//   shambler=4/bones=5/stalker=6/spider=7/chicken=8）。文件名用 MC 1.0 entity 子目录命名（entity/<mob>/<mob>.png，
//   现网大多数包此布局；mobTextureSource 在子目录缺时自动回退扁平 entity/<mob>.png 兼容旧 / HD 包）。机制等价
//   MC 1.0 mob 外观，标识符 / 名称全原创（§9 区隔：Shambler↔zombie / Bones↔skeleton / Stalker↔creeper）。
//   包内缺该 PNG 时 mobTextureSource 安全跳过（回退程序生成 / 纯色），故映射可慷慨：唯一要保证的是 id↔目录配对正确。
//   Squid(9) 不映射（spec t421 未列；保留程序生成 mob_squid 贴图，pack 启用也不变）。
const QList<QPair<int, QString>> &mobEntityMap()
{
    static const QList<QPair<int, QString>> kMap = {
        {1, QStringLiteral("pig/pig.png")},         // MobPig → entity/pig/pig.png
        {2, QStringLiteral("cow/cow.png")},         // MobCow → entity/cow/cow.png
        {3, QStringLiteral("sheep/sheep.png")},     // MobSheep → entity/sheep/sheep.png（羊毛态；剪羊毛态走程序生成）
        {4, QStringLiteral("zombie/zombie.png")},   // MobShambler → entity/zombie/zombie.png（机制等价 zombie，§9 改名）
        {5, QStringLiteral("skeleton/skeleton.png")},// MobBones → entity/skeleton/skeleton.png（机制等价 skeleton，§9 改名）
        {6, QStringLiteral("creeper/creeper.png")}, // MobStalker → entity/creeper/creeper.png（机制等价 creeper，§9 改名）
        {7, QStringLiteral("spider/spider.png")},   // MobSpider → entity/spider/spider.png
        {8, QStringLiteral("chicken/chicken.png")}, // MobChicken → entity/chicken/chicken.png
    };
    return kMap;
}

// t456「引擎方块 id → pack item/前贴图文件名候选」映射（功能性元数据，红线 §9 可随代码提交；贴图文件不进仓库）。
//   方块段 id（与 BlockRegistry::Id 同源；Core 不依赖 Game 故用字面量 + 注释钉死，同 itemFilenameMap 不引
//   toolregistry 之例）：9=CraftingTable 工作台 / 10=Furnace 熔炉。这俩方块的 2D 物品图标此前用程序绘制的
//   等距立方体 icon_*.png（"旧版"）；pack 启用且包内有对应 item / 前贴图时改用 pack（机制等价 MC 1.0 item icon：
//   工作台 / 熔炉在物品栏显示其 item 贴图）。候选顺序 = 探测优先级：item 目录的 vanilla 风格 item/<name>.png 优先
//   （多数包有），block 目录的 <name>_front.png 兜底（pack 把前贴图放 block/ 的布局，如 furnace_front.png 在 block/）。
//   blockItemIconSource 逐候选 itemDir→blockDir 探测，首个命中即返；全缺返空（Hotbar 回退程序生成图标）。
const QList<QPair<int, QStringList>> &blockItemIconMap()
{
    static const QList<QPair<int, QStringList>> kMap = {
        { 9,  { QStringLiteral("crafting_table.png"), QStringLiteral("crafting_table_front.png") } }, // BlockRegistry::CraftingTable 工作台
        { 10, { QStringLiteral("furnace.png"),        QStringLiteral("furnace_front.png") } },         // BlockRegistry::Furnace 熔炉
    };
    return kMap;
}

// t416/t444 MC「灰度可着色」瓦片 → 着色 tint 查表（单一权威）。这些 tile 的包内贴图本体是灰度（机制等价 MC
//   foliageColor/grassColor / lily pad fixed tint），loader 直接用包内灰度原色会渲染成苔石色 / 灰白（用户「睡莲
//   现灰」），故合成时乘上对应群系 tint。非着色瓦片（stone/dirt/...）原样，不受影响（返 nullptr）。
//   注：grass_side 不在此列——它 = dirt 基底 + 顶部绿 overlay（仅顶部绿条着色），整张乘绿会把下方泥土也染绿
//   （t422 修：改走 composeGrassSide 走 overlay 合成路径）。
//   - grass_top(0) / oak_leaves(9) / tall_grass(28)：plains 叶绿素 #5a8a3a（t416）。
//   - lily_pad(61)（t444）：沼泽水生绿 #4aa852。MC lily_pad.png 是灰度可着色贴图（demo pack 实测灰 133,133,133），
//     MC 用硬编码水生绿着色；本引擎无 BlockColors → 合成时固定乘本 tint（机制等价 MC lily pad fixed tint）。
//     不着色则 pack 睡莲渲染成灰白方块（用户「现灰」）。
const int *tileTint(int tileIndex)
{
    static constexpr int kFoliage[3] = {0x5a, 0x8a, 0x3a}; // plains 叶绿素 #5a8a3a
    static constexpr int kLily[3]    = {0x4a, 0xa8, 0x52}; // t444 睡莲沼泽水生绿 #4aa852
    if (tileIndex == 0 || tileIndex == 9 || tileIndex == 28) return kFoliage; // grass_top / oak_leaves / tall_grass
    if (tileIndex == 61) return kLily;                                       // lily_pad
    return nullptr;
}

// 乘色着色：tile 已是 Format_ARGB32_Premultiplied——直接乘预乘后的 RGB = 正确保持预乘关系
//   （newR = tintR * R * A，alpha 不动）。灰度部分乘 tint → 着色（灰度×绿 → 绿）。
void applyTint(QImage &tile, int tintR, int tintG, int tintB)
{
    const int w = tile.width(), h = tile.height();
    for (int y = 0; y < h; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(tile.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = scan[x];
            scan[x] = qRgba((qRed(c) * tintR) / 255,
                            (qGreen(c) * tintG) / 255,
                            (qBlue(c) * tintB) / 255,
                            qAlpha(c));
        }
    }
}

// t422 grass_side 正确合成：dirt 基底 + 顶部绿色 overlay。t416 误把整张 grass_block_side.png
//   乘叶绿素 → 下方泥土也被染绿（错误）。MC 实际语义 = dirt.png（彩色泥土）打底，
//   grass_block_side_overlay.png（仅顶部 alpha 绿条的灰度蒙版）乘叶绿素后 SourceOver 叠在
//   dirt 上 → 仅顶部绿条变绿、下方泥土保泥土色。overlay / dirt 任一缺失 → 回退
//   grass_block_side.png 原样不着色（HD 包常已带色；缺 overlay 的旧包保原色，绝不染绿泥土）。
//   返回已缩放到 kTile×kTile 的 Format_ARGB32_Premultiplied；全缺则返回空 QImage（调用方跳过）。
QImage composeGrassSide(const QDir &blockDir)
{
    const QString overlayPath = blockDir.absoluteFilePath(QStringLiteral("grass_block_side_overlay.png"));
    const QString dirtPath = blockDir.absoluteFilePath(QStringLiteral("dirt.png"));
    if (QFile::exists(overlayPath) && QFile::exists(dirtPath)) {
        QImage dirt(dirtPath);
        QImage overlay(overlayPath);
        if (!dirt.isNull() && !overlay.isNull()) {
            dirt = dirt.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            overlay = overlay.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (dirt.size() != QSize(kTile, kTile))
                dirt = dirt.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (overlay.size() != QSize(kTile, kTile))
                overlay = overlay.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            applyTint(overlay, 0x5a, 0x8a, 0x3a); // 仅 alpha>0 处（顶部绿条）乘 plains 绿 → 绿；alpha=0 处保留透明
            QPainter op(&dirt);
            op.drawImage(0, 0, overlay); // SourceOver：绿条叠在泥土上，透明处保泥土色
            op.end();
            return dirt;
        }
    }
    // 回退：overlay / dirt 缺失 → grass_block_side.png 原样不着色（绝不染绿泥土）。
    QImage side(blockDir.absoluteFilePath(QStringLiteral("grass_block_side.png")));
    if (side.isNull())
        return {};
    side = side.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (side.size() != QSize(kTile, kTile))
        side = side.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return side;
}

// t489 从包内动画贴图（water_still / water_flow / lava_still）抽帧：MC 动画贴图是单列竖排 strip ——
//   宽 = 帧像素边长、高 = 帧数 × 帧边长。抽第 i 帧 = 行 [i*framePx, (i+1)*framePx)。包内帧边长 = image.width
//   （demo 包 water_still 16 宽 → 16×16 帧；water_flow 32 宽 → 32×32 帧；lava_still 16 宽 → 16×16 帧）。
//   返回缩放到 kFluidStripFramePx(=16)×16 的帧列表（最多 maxFrames 帧；不足 maxFrames 不补齐——调用方按需补）。
//   解码失败 / 帧数为 0 → 返回空列表（调用方回退程序生成帧）。
QList<QImage> extractAnimFrames(const QString &pngPath, int maxFrames)
{
    QList<QImage> frames;
    QImage src(pngPath);
    if (src.isNull())
        return frames;
    const int framePx = src.width();                 // 单列 strip：宽 = 帧边长
    if (framePx <= 0 || src.height() < framePx)
        return frames;
    const int count = src.height() / framePx;        // 帧数 = 高 / 帧边长
    const int take = qMin(count, maxFrames);
    for (int i = 0; i < take; ++i) {
        QImage f = src.copy(0, i * framePx, framePx, framePx);
        if (f.size() != QSize(BlockRegistry::kFluidStripFramePx, BlockRegistry::kFluidStripFramePx))
            f = f.scaled(BlockRegistry::kFluidStripFramePx, BlockRegistry::kFluidStripFramePx,
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        frames.append(f.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    return frames;
}

// t489 把某列（col=0..1）的前 N 帧覆盖进条带。strip 是 (2 列 × N 行) 合成图，每帧 16×16。
//   frames 不足 N 时：末尾用最后一帧循环补齐（保条带恒为 N 帧 → mesher UV 1/N 子区与 QML positionV 步长不错配）。
//   frames 为空（包缺该贴图）→ 该列保留程序生成底（不覆盖）。
void paintColumnFrames(QImage &strip, int col, int cols, int framePx, int frameCount, const QList<QImage> &frames)
{
    if (frames.isEmpty())
        return; // 包缺该贴图 → 保留程序生成底
    const int x0 = col * framePx;
    for (int k = 0; k < frameCount; ++k) {
        // 帧 k 占 PIL 行 [H-(k+1)*16, H-k*16)（帧 0 在图像底）。
        const int yTop = strip.height() - (k + 1) * framePx;
        const int fi = (k < frames.size()) ? k : (frames.size() - 1); // 不足末尾循环
        QPainter p(&strip);
        p.drawImage(x0, yTop, frames.at(fi));
    }
}

// t489 构建流体条带（水 / 岩浆）：以 qrc 程序生成条带为底，包内帧覆盖对应列/行 → 落盘 AppLocalData。
//   返回落盘绝对路径（file:/// 前缀由调用方加）；构建失败返空串（调用方回退 qrc）。
//   - 水条带（cols=2）：左列 = water_still 帧、右列 = water_flow 帧。两列各 32 帧（kWaterStripFrames）。
//   - 岩浆条带（cols=1）：单列 = lava_still 帧，16 帧（kLavaStripFrames）。
QString buildFluidStrip(const QDir &blockDir, const QString &baseStripResource,
                        int cols, int frameCount, const QList<QPair<int, QString>> &sourceFiles)
{
    QImage strip(baseStripResource);
    if (strip.isNull()) {
        qWarning("ResourcePack: 无法加载程序生成流体条带底 %s。", qPrintable(baseStripResource));
        return {};
    }
    strip = strip.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int framePx = BlockRegistry::kFluidStripFramePx;
    // cols 列各取包内帧覆盖；sourceFiles[i].first = 列号、.second = 包内文件名。
    for (const auto &sf : sourceFiles) {
        const QString png = blockDir.absoluteFilePath(sf.second);
        if (QFile::exists(png)) {
            const QList<QImage> frames = extractAnimFrames(png, frameCount);
            if (!frames.isEmpty())
                paintColumnFrames(strip, sf.first, cols, framePx, frameCount, frames);
        }
    }
    // 落盘（同图集落盘路径规则）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) {
        qWarning("ResourcePack: AppLocalDataLocation 为空，无法落盘流体条带；回退程序生成条带。");
        return {};
    }
    QDir().mkpath(dir);
    const QString name = (cols == 2) ? QStringLiteral("voxelsandbox_water_strip.png")
                                     : QStringLiteral("voxelsandbox_lava_strip.png");
    const QString path = QDir(dir).absoluteFilePath(name);
    if (!strip.save(path, "PNG")) {
        qWarning("ResourcePack: 无法写入流体条带 %s；回退程序生成条带。", qPrintable(path));
        return {};
    }
    return path;
}

// 合成构建（调用者须已持 stateMutex()）。幂等（built 标志）。运行期经 apply() 置 built=false 强制重建。
//   config 首次从 settings.json 加载；之后只信 BuiltState 内存值（setter 已持久化保持同步）。
void ensureBuiltLocked()
{
    BuiltState &s = state();
    if (s.built)
        return;
    s.built = true;
    s.active = false; // reset；仅当包合法 + 覆盖成功才置 true
    s.itemDir.clear(); // t420 reset 物品图标目录（仅当包合法时重填）
    s.entityDir.clear(); // t421 reset 生物贴图目录（仅当包合法时重填）
    s.blockDir.clear(); // t456 reset 方块贴图目录（仅当包合法时重填）
    s.waterStripFile.clear(); // t489 reset 流体条带落盘路径（仅当包合法时重填）
    s.lavaStripFile.clear();

    // 底图 = qrc 程序生成图集（零 MC 资产进 qrc）。即便无包，合成图集也 = 默认。
    QImage base(QStringLiteral(":/textures/atlas.png"));
    if (base.isNull()) {
        qWarning("ResourcePack: 无法加载默认图集 qrc:/textures/atlas.png；跳过包覆盖。");
        return;
    }
    base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s.atlas = base;
    const int tileCount = base.width() / kTile; // 从图集宽度推导瓦片数（避免硬编码魔数）。

    // t415 config 首次从 settings.json 加载；之后只信内存。
    if (!s.configLoaded) {
        const Settings cfg = readSettings();
        s.enabled = cfg.enabled;
        s.packPath = cfg.packPath;
        s.configLoaded = true;
    }

    if (!s.enabled) {
        qInfo("ResourcePack: 已禁用（resourcePackEnabled=false）；用程序生成图集。");
        return;
    }

    QString packPath;
    // 1) 配置指定的包路径（settings.json / UI 输入）
    if (!s.packPath.isEmpty()) {
        const QString abs = absolutePackPath(s.packPath);
        if (isValidPack(abs))
            packPath = abs;
        else
            qWarning("ResourcePack: 包路径 %s 非合法包，跳过。",
                     qPrintable(s.packPath));
    }
    // 2) 环境变量 VOXELSANDBOX_RESOURCEPACK
    if (packPath.isEmpty()) {
        const QByteArray env = qgetenv("VOXELSANDBOX_RESOURCEPACK");
        if (!env.isEmpty()) {
            const QString abs = absolutePackPath(QString::fromLocal8Bit(env));
            if (isValidPack(abs))
                packPath = abs;
            else
                qWarning("ResourcePack: 环境变量 VOXELSANDBOX_RESOURCEPACK=%s 非合法包，跳过。",
                         env.constData());
        }
    }
    // 3) 默认探查
    if (packPath.isEmpty())
        packPath = discoverDefault();

    if (packPath.isEmpty()) {
        qInfo("ResourcePack: 未找到资源包；用程序生成图集。");
        return;
    }

    // 合成：对映射里存在的瓦片，把包内 PNG 缩放到 TILE=16 后覆盖。
    // t419 packPath 可为 pack 根 / 中间层 / block 目录任意层级，统一经 resolveBlockDir 定位贴图目录。
    const QString blockDirPath = resolveBlockDir(packPath);
    if (blockDirPath.isEmpty()) {
        // isValidPack 已保证非空；此处守卫仅防竞态（包在解析后、合成前被删 / 移）。
        qWarning("ResourcePack: 包 %s 的 block 贴图目录解析失败，跳过覆盖。", qPrintable(packPath));
        return;
    }
    const QDir blockDir(blockDirPath);

    // t420 物品图标目录（assets/minecraft/textures/item；与 block 同 packPath 并列解析）。包内无 item 目录
    //   时为空 → itemIconSource 恒返空串 → ToolIcon/MaterialIcon 回退自绘（不阻塞 block 图集合成）。
    s.itemDir = resolveItemDir(packPath);
    // t421 生物贴图目录（assets/minecraft/textures/entity；同 packPath 并列解析）。包内无 entity 目录时为空
    //   → mobTextureSource 恒返空串 → Main.qml 各 mob delegate 回退程序生成贴图 / 纯色（不阻塞 block 图集合成）。
    s.entityDir = resolveEntityDir(packPath);
    // t456 方块贴图目录（已由 resolveBlockDir 解析为 blockDirPath；缓存供 blockItemIconSource 兜底探测 block/<name>.png）。
    s.blockDir = blockDirPath;
    QPainter p(&s.atlas);
    int overridden = 0;
    for (const auto &m : tileFilenameMap()) {
        if (m.first < 0 || m.first >= tileCount)
            continue; // 越界守卫（防图集宽度 < 映射索引 → 画到图集外）。
        QImage tile;
        if (m.first == 1) {
            // t422 grass_side 走专用合成（dirt 基底 + 顶部绿 overlay，不整张染绿）；全缺 → 跳过。
            tile = composeGrassSide(blockDir);
            if (tile.isNull())
                continue;
        } else {
            const QString png = blockDir.absoluteFilePath(m.second);
            if (!QFile::exists(png))
                continue; // 包内无该贴图 → 不覆盖（保留程序生成瓦片）。
            tile = QImage(png);
            if (tile.isNull()) {
                qWarning("ResourcePack: 无法解码 %s，跳过。", qPrintable(png));
                continue;
            }
            tile = tile.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (tile.size() != QSize(kTile, kTile))
                tile = tile.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (const int *tint = tileTint(m.first))
                applyTint(tile, tint[0], tint[1], tint[2]); // t416/t444：灰度可着色瓦片乘群系 tint（叶/草顶/草丛 plains 绿；睡莲沼泽水生绿）
        }
        p.drawImage(m.first * kTile, 0, tile);
        ++overridden;
    }
    p.end();

    s.active = true;

    // 落盘合成图集到 AppLocalDataLocation（QtQuick3D Texture 不支持 image:// QQuickImageProvider →
    //   image://rp/atlas 在 Texture 上是空贴图 = 全白方块；file:// 才会被 Texture 加载）。每次 apply()
    //   重建都覆盖此文件。落盘失败则回退程序生成图集（active=false），宁可不变白也不渲染空贴图。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty()) {
        qWarning("ResourcePack: AppLocalDataLocation 为空，无法落盘图集；回退程序生成图集。");
        s.active = false;
        s.atlasFile.clear();
        return;
    }
    QDir().mkpath(dir); // 缺目录先建（首次落盘或新装机器）
    s.atlasFile = QDir(dir).absoluteFilePath(QStringLiteral("voxelsandbox_rp_atlas.png"));
    if (!s.atlas.save(s.atlasFile, "PNG")) {
        qWarning("ResourcePack: 无法写入图集文件 %s；回退程序生成图集。", qPrintable(s.atlasFile));
        s.active = false;
        s.atlasFile.clear();
        return;
    }
    qInfo("ResourcePack: 启用包 %s（覆盖 %d 瓦片，图集 → %s）。",
          qPrintable(packPath), overridden, qPrintable(s.atlasFile));

    // t489 流体条带（材质级 flipbook）：以 qrc 程序生成条带为底，包内帧覆盖对应列/行 → 落盘。
    //   水条带 2 列（water_still | water_flow）× kWaterStripFrames 帧；岩浆条带 1 列（lava_still）×
    //   kLavaStripFrames 帧。构建失败（落盘目录不可写 / 底图缺）→ 路径留空 → QML 回退 qrc 程序生成条带
    //   （仍动画，只是无包内高清帧）。包缺某源文件 → 该列保留程序生成底（不覆盖），条带仍 N 帧（动画不破）。
    s.waterStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/water_strip.png"),
            2, BlockRegistry::kWaterStripFrames,
            { {0, QStringLiteral("water_still.png")}, {1, QStringLiteral("water_flow.png")} });
    s.lavaStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/lava_strip.png"),
            1, BlockRegistry::kLavaStripFrames,
            { {0, QStringLiteral("lava_still.png")} });
}
} // namespace

ResourcePackManager::ResourcePackManager(QObject *parent)
    : QObject(parent)
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    m_active = state().active;
    rpInstances().append(this); // t420 注册（供 apply 广播 activeChanged 到全部实例）
}

ResourcePackManager::~ResourcePackManager()
{
    rpInstances().removeAll(this); // t420 注销
}

bool ResourcePackManager::enabled() const
{
    QMutexLocker lock(&stateMutex());
    return state().enabled;
}

void ResourcePackManager::setEnabled(bool e)
{
    QString curPath;
    {
        QMutexLocker lock(&stateMutex());
        BuiltState &s = state();
        s.enabled = e;
        s.configLoaded = true;
        curPath = s.packPath;
    }
    writeSettings(e, curPath); // 持久化（文件 IO 在锁外，少占 image provider）
    emit configChanged();
}

QString ResourcePackManager::packPath() const
{
    QMutexLocker lock(&stateMutex());
    return state().packPath;
}

void ResourcePackManager::setPackPath(const QString &p)
{
    bool curEnabled;
    {
        QMutexLocker lock(&stateMutex());
        BuiltState &s = state();
        s.packPath = p;
        s.configLoaded = true;
        curEnabled = s.enabled;
    }
    writeSettings(curEnabled, p);
    emit configChanged();
}

void ResourcePackManager::apply()
{
    bool newActive;
    {
        QMutexLocker lock(&stateMutex());
        BuiltState &s = state();
        s.built = false;        // 强制重建（用当前 s.enabled/s.packPath，setter 已持久化）
        ensureBuiltLocked();
        ++s.revision;           // cache-bust：atlasSource 查询串变 → QML Texture 重载
        newActive = s.active;
    }
    // t420 广播到全部实例（含 ToolIcon/MaterialIcon 内持有的实例）：同步 m_active + emit activeChanged，
    //   使全工程所有 active / atlasSource / itemIconSource 绑定随 pack 切换刷新（不止触发调用 apply 的本实例）。
    //   实例态在锁外更新 + emit（避免持锁 emit 连到再加锁的槽）。
    for (ResourcePackManager *inst : rpInstances()) {
        inst->m_active = newActive;
        emit inst->activeChanged();
    }
}

QString ResourcePackManager::atlasSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/atlas.png");
    QMutexLocker lock(&stateMutex());
    return QStringLiteral("file:///") + state().atlasFile;
}

// t489 流体条带贴图源（材质级 flipbook；详见 .h Q_PROPERTY 注释）。
//   active 且条带落盘成功 → file:///<AppLocalData>/voxelsandbox_<x>_strip.png（包内帧覆盖的合成条带）；
//   否则 qrc:/textures/<x>_strip.png（程序生成条带）。条带落盘路径为空（包缺 / 落盘失败）→ 回退 qrc。
QString ResourcePackManager::waterStripSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/water_strip.png");
    QMutexLocker lock(&stateMutex());
    const QString &f = state().waterStripFile;
    return f.isEmpty() ? QStringLiteral("qrc:/textures/water_strip.png")
                       : QStringLiteral("file:///") + f;
}

QString ResourcePackManager::lavaStripSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/lava_strip.png");
    QMutexLocker lock(&stateMutex());
    const QString &f = state().lavaStripFile;
    return f.isEmpty() ? QStringLiteral("qrc:/textures/lava_strip.png")
                       : QStringLiteral("file:///") + f;
}

QImage ResourcePackManager::compositeAtlas()
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    return state().atlas; // 隐式共享副本；返回后 apply() 若重建会换新 QImage，本副本数据不受影响
}

bool ResourcePackManager::packActive()
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    return state().active;
}

QString ResourcePackManager::itemIconSource(int itemId) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.itemDir.isEmpty())
        return {};
    // 引擎物品 id → 包内标准 item 文件名（itemFilenameMap 单一权威）。无映射 / 包内缺 PNG → 空串（回退自绘）。
    QString filename;
    for (const auto &m : itemFilenameMap()) {
        if (m.first == itemId) {
            filename = m.second;
            break;
        }
    }
    if (filename.isEmpty())
        return {};
    const QString path = QDir(s.itemDir).absoluteFilePath(filename);
    if (!QFile::exists(path))
        return {}; // 包内无该 item 贴图 → 不覆盖（保留自绘 Canvas）；红线 §9：仅运行期读本地 pack PNG。
    return QStringLiteral("file:///") + path;
}

QString ResourcePackManager::blockItemIconSource(int blockId)
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active)
        return {};
    // 该方块的候选文件名列表（无映射 → 空串，调用方回退程序生成图标）。
    QStringList candidates;
    for (const auto &m : blockItemIconMap()) {
        if (m.first == blockId) {
            candidates = m.second;
            break;
        }
    }
    if (candidates.isEmpty())
        return {};
    // 逐候选：item 目录优先（vanilla item icon），block 目录兜底（pack 把前贴图放 block/）。首个命中即返。
    for (const QString &name : candidates) {
        if (!s.itemDir.isEmpty()) {
            const QString p = QDir(s.itemDir).absoluteFilePath(name);
            if (QFile::exists(p))
                return QStringLiteral("file:///") + p;
        }
        if (!s.blockDir.isEmpty()) {
            const QString p = QDir(s.blockDir).absoluteFilePath(name);
            if (QFile::exists(p))
                return QStringLiteral("file:///") + p;
        }
    }
    return {}; // 包内无该方块 item / 前贴图 → 不覆盖（保留程序生成 icon_<block>.png）。
}

QString ResourcePackManager::mobTextureSource(int mobType) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.entityDir.isEmpty())
        return {};
    // 引擎 mob id → pack entity 子目录/文件名（mobEntityMap 单一权威）。无映射 → 空串（回退程序生成 / 纯色）。
    QString relPath;
    for (const auto &m : mobEntityMap()) {
        if (m.first == mobType) {
            relPath = m.second;
            break;
        }
    }
    if (relPath.isEmpty())
        return {};
    const QDir entityDir(s.entityDir);
    // 1) 子目录布局（entity/<mob>/<mob>.png，现网大多数包）：MC 1.0 标准。
    const QString sub = entityDir.absoluteFilePath(relPath);
    if (QFile::exists(sub))
        return QStringLiteral("file:///") + sub;
    // 2) 扁平回退（entity/<mob>.png，旧 / HD 包常省略子目录）：取文件名（去子目录）。
    const QFileInfo fi(relPath);
    const QString flat = entityDir.absoluteFilePath(fi.fileName());
    if (QFile::exists(flat))
        return QStringLiteral("file:///") + flat;
    return {}; // 包内无该 entity 贴图 → 不覆盖（保留程序生成 / 纯色）；红线 §9：仅运行期读本地 pack PNG。
}
