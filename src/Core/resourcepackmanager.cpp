#include "resourcepackmanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>

// 资源包解析 + 图集合成。详见 resourcepackmanager.h 的总体设计 / 红线（PLAN §9）。
// 引擎瓦片尺寸（与 build_atlas.py TILE=16 + chunkgeometry UV 同源）。
constexpr int kTile = ResourcePackManager::kTile;

namespace {

// 合成结果 + active 态的进程全局缓存（资源包配置是 process-global，启动期解析一次）。
struct BuiltState {
    bool built = false;
    bool active = false;
    QImage atlas;
};
BuiltState &state()
{
    static BuiltState s;
    return s;
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

// 读 settings.json：resourcePackEnabled（缺省 true）+ resourcePack（可空）。
struct Settings {
    bool enabled = true;
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
        s.enabled = obj.value(QStringLiteral("resourcePackEnabled")).toBool(true);
    if (obj.contains(QStringLiteral("resourcePack")))
        s.packPath = obj.value(QStringLiteral("resourcePack")).toString();
    return s;
}
} // namespace

// 「引擎 tile 索引 → 包内标准贴图文件名」映射（与 tools/build_atlas.py TILES 顺序对齐）。
//   缺失的文件名由 buildComposite 安全跳过（不覆盖 = 保留程序生成瓦片），故映射可慷慨、零渗色风险：
//   唯一要保证的是 tile↔文件名配对正确（配错会渗色），而非文件名都存在。
const QList<QPair<int, QString>> &ResourcePackManager::tileFilenameMap()
{
    static const QList<QPair<int, QString>> kMap = {
        {0, QStringLiteral("grass_block_top.png")},
        {1, QStringLiteral("grass_block_side.png")},
        {2, QStringLiteral("dirt.png")},
        {3, QStringLiteral("stone.png")},
        {4, QStringLiteral("sand.png")},
        {5, QStringLiteral("cobblestone.png")},
        {6, QStringLiteral("oak_log_top.png")},
        {7, QStringLiteral("oak_log.png")},
        {8, QStringLiteral("oak_planks.png")},
        {9, QStringLiteral("oak_leaves.png")},
        {10, QStringLiteral("crafting_table_top.png")},
        {11, QStringLiteral("crafting_table_side.png")},
        {12, QStringLiteral("furnace_top.png")},
        {13, QStringLiteral("furnace_side.png")},
        {14, QStringLiteral("furnace_front.png")},
        {15, QStringLiteral("coal_ore.png")},
        {16, QStringLiteral("iron_ore.png")},
        {18, QStringLiteral("bedrock.png")},
        {37, QStringLiteral("diamond_ore.png")},
        {40, QStringLiteral("copper_ore.png")},
        {41, QStringLiteral("gold_ore.png")},
        {68, QStringLiteral("glass.png")},
    };
    return kMap;
}

void ResourcePackManager::ensureBuilt()
{
    BuiltState &s = state();
    if (s.built)
        return;
    s.built = true;

    // 底图 = qrc 程序生成图集（零 MC 资产进 qrc）。即便无包，合成图集也 = 默认（provider 不被门控时用）。
    QImage base(QStringLiteral(":/textures/atlas.png"));
    if (base.isNull()) {
        qWarning("ResourcePack: 无法加载默认图集 qrc:/textures/atlas.png；跳过包覆盖。");
        return;
    }
    base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s.atlas = base;
    const int tileCount = base.width() / kTile; // 从图集宽度推导瓦片数（避免硬编码 79 魔数）。

    // 配置解析：settings.json → 环境变量 → 默认探查。
    const Settings cfg = readSettings();
    if (!cfg.enabled) {
        qInfo("ResourcePack: 已在 settings.json 禁用（resourcePackEnabled=false）；用程序生成图集。");
        return;
    }

    QString packPath;
    // 1) settings.json "resourcePack"
    if (!cfg.packPath.isEmpty()) {
        const QString abs = absolutePackPath(cfg.packPath);
        if (isValidPack(abs))
            packPath = abs;
        else
            qWarning("ResourcePack: settings.json resourcePack=%s 非合法包路径，跳过。",
                     qPrintable(cfg.packPath));
    }
    // 2) 环境变量 VOXELSANDBOX_RESOURCEPACK
    if (packPath.isEmpty()) {
        const QByteArray env = qgetenv("VOXELSANDBOX_RESOURCEPACK");
        if (!env.isEmpty()) {
            const QString abs = absolutePackPath(QString::fromLocal8Bit(env));
            if (isValidPack(abs))
                packPath = abs;
            else
                qWarning("ResourcePack: 环境变量 VOXELSANDBOX_RESOURCEPACK=%s 非合法包路径，跳过。",
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
    qInfo("ResourcePack: 启用包 %s（覆盖 %d 瓦片）。",
          qPrintable(packPath), overridden);
}

ResourcePackManager::ResourcePackManager(QObject *parent)
    : QObject(parent)
{
    ensureBuilt();
    m_active = packActive();
}

QString ResourcePackManager::atlasSource() const
{
    return m_active ? QStringLiteral("image://rp/atlas")
                    : QStringLiteral("qrc:/textures/atlas.png");
}

QImage ResourcePackManager::compositeAtlas()
{
    ensureBuilt();
    return state().atlas;
}

bool ResourcePackManager::packActive()
{
    ensureBuilt();
    return state().active;
}
