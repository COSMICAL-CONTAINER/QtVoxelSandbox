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

// 合法包判定：含 assets/minecraft/textures/block/ 子树（spec t414 的 pack 结构）。
bool isValidPack(const QString &absPath)
{
    if (absPath.isEmpty())
        return false;
    return QDir(absPath + QStringLiteral("/assets/minecraft/textures/block")).exists();
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
    };
    return kMap;
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
    const QDir blockDir(packPath + QStringLiteral("/assets/minecraft/textures/block"));
    QPainter p(&s.atlas);
    int overridden = 0;
    for (const auto &m : tileFilenameMap()) {
        if (m.first < 0 || m.first >= tileCount)
            continue; // 越界守卫（防图集宽度 < 映射索引 → 画到图集外）。
        const QString png = blockDir.absoluteFilePath(m.second);
        if (!QFile::exists(png))
            continue; // 包内无该贴图 → 不覆盖（保留程序生成瓦片）。
        QImage tile(png);
        if (tile.isNull()) {
            qWarning("ResourcePack: 无法解码 %s，跳过。", qPrintable(png));
            continue;
        }
        tile = tile.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (tile.size() != QSize(kTile, kTile))
            tile = tile.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
}
} // namespace

ResourcePackManager::ResourcePackManager(QObject *parent)
    : QObject(parent)
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    m_active = state().active;
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
    m_active = newActive;       // 实例态在锁外更新 + emit（避免持锁 emit 连到再加锁的槽）
    emit activeChanged();
}

QString ResourcePackManager::atlasSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/atlas.png");
    QMutexLocker lock(&stateMutex());
    return QStringLiteral("file:///") + state().atlasFile;
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
