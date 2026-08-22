#include "resourcepackmanager.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>

// 资源包解析 + 图集合成。详见 resourcepackmanager.h 的总体设计 / 红线（PLAN §9）。
// 引擎瓦片尺寸（与 build_atlas.py TILE=16 + chunkgeometry UV 同源）。
constexpr int kTile = ResourcePackManager::kTile;

// t633 图鉴生物头像：mobType → 头部 box-UV 数据（u0, v0, w, h, d, 贴图 base 宽, 贴图 base 高）。
//   与 mobmodel.cpp 各 mob 分支的 setMobTex 头部值同源（单一权威在 Renderer；此处 Core 不能 include Renderer，
//   以注释互指 + 数值镜像——同 mobEntityMap 与 blockItemIconMap 的字面量模式）。裁剪区 = MC +Z Front 面
//   （u0+d, v0+d)-(u0+d+w, v0+d+h)（脸所在面；mobmodel.cpp mobFaceQtUV case 4 同公式）。
//   羊条目带 sheepBody=true 标记：主贴图 sheep_fur.png 毛层头前无脸 → 改从 sheep/sheep.png 本体层裁。
//   review D3-b：结构体 + 两函数声明前置到文件顶部 —— ensureBuiltLocked（下方匿名命名空间内）构建期
//   预生成头像要用（range-for 变量 + 成员访问须完整类型），而其定义在前。数据表与裁剪实现仍在文件后段。
struct MobHeadRegion {
    int mobType;
    int u0, v0, w, h, d;
    int texW, texH;
    bool sheepBody; // true = 用 sheep/sheep.png（本体层）而非 mobEntityMap 主映射（毛层）
    // t749 头像专用显式 pack 源（相对 entity/ 的路径；缺省 nullptr = 走 mobEntityMap 主映射）。
    //   狼/豹猫/蠹虫**不进 mobEntityMap**——它们 MobModel 几何分支无 setMobTex box-UV 数据（全脸 UV 模型），
    //   若 mobTextureSource 命中会让 delegate 把 packTextured 几何 UV 采到未设定位 → 3D 贴图错乱；
    //   头像只读像素裁剪不涉几何 → 显式源只喂头像路径，3D 路径零改动（回归面最小）。
    const char *explicitSrc;
};
const QList<MobHeadRegion> &mobHeadRegions();
QString generateMobHeadIconFile(const MobHeadRegion &region, const QString &entityDirPath);
// t749 羊 3D 预览合成贴图：sheep_fur.png 毛身 + sheep.png 本体层头区（真脸）→ 落盘绝对路径（空 = 失败回退）。
QString generateSheepWoolFaceFile(const QString &furPath, const QString &bodyPath);

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
    QString effectDir;            // t715 包内状态效果图标目录（assets/minecraft/textures/mob_effect）绝对路径；effectIconSource 探测（HUD 效果栏 pack 覆盖）
    QString paintingDir;          // t717 包内画作目录（assets/minecraft/textures/painting）绝对路径；paintingSource 逐 index 探测（t720 画作方块 pack 覆盖）
    QString armorDir;             // t717 包内盔甲 layer 目录（assets/minecraft/textures/models/armor）绝对路径；armorLayerSource 探测（t718 盔甲 3D 显示 pack 覆盖）
    // t489 流体条带落盘路径（active 时 file:///）；waterStrip = 2 列×32 帧（静水|流水），lavaStrip = 1 列×16 帧，
    //   fireStrip = 1 列×32 帧（t724 火焰翻书），portalStrip = 1 列×32 帧（t725 余烬门翻书）。
    QString waterStripFile;
    QString lavaStripFile;
    QString fireStripFile;
    QString portalStripFile;
    // t496 二轮复盘 床 16 色 item 图标染色缓存：bedId→落盘的染色后 bed 图标 file:// 路径。bed.png 是红床模板，
    //   每床色首次查询时按目标色重染（retintBedTemplate）落盘 voxelsandbox_rp_bed_<id>.png，后续命中直接返。
    //   apply() 重建时清空（pack 切换 / 重解析）；随 atlasFile 同目录写（AppLocalDataLocation，已 mkpath）。
    QHash<int, QString> bedIconFiles;
    // R19 B1 皮革护甲 item 图标染色缓存：leatherId(0x300..0x303)→落盘的染色后皮革图标 file:// 路径。
    //   pack 的 leather_*.png 是白底可染色 base，每件首次查询时按皮革棕梯度重染（retintLeatherTemplate）
    //   落盘 voxelsandbox_rp_leather_<id>.png，后续命中直接返。apply() 重建时清空（pack 切换/重解析）。
    //   仅 0x300..0x303 四件（皮革 tier）；铁/金/钻石原样用 pack 图不染色；铜（t613）走 copperIconFiles
    //   的 iron_* 染铜回退（同铜工具机制），不经本缓存。
    QHash<int, QString> leatherIconFiles;
    // t731 玩家皮肤名（"default"/"alex"；settings.json playerSkin 镜像，缺省 default）。/skin 命令经
    //   setPlayerSkin 改写 + 持久化；Main.qml skinName 属性启动期从 playerSkin() 读。
    QString playerSkin;
    // t731 pack 皮肤 64×64→64×32 裁切缓存：kind（"skin_default"/"skin_alex"）→落盘的裁上半图 file://
    //   路径（voxelsandbox_rp_skin_<kind>_r<revision>.png）。apply() 重建时清空（pack 切换/重解析 → 重裁）。
    QHash<QString, QString> skinPackFiles;
    // t588/t613 铜物品（铜工具 0x118..0x11C / 铜锭 0x21D / 铜护甲 0x308..0x30B）染色图标缓存：pack 无
    //   copper_*（1.8 等老包）→ 用铁对应贴图染铜橙（retintCopperTemplate）落盘 voxelsandbox_rp_copper_<id>.png，
    //   后续命中直接返。apply() 重建时清空（pack 切换 / 重解析 → 重染）。
    QHash<int, QString> copperIconFiles;
    // t745 统一贴图原则：方块 item 图标运行期 pack 渲染缓存。blockId→落盘的 dimetric/flat 图标 file:// 路径
    //   （voxelsandbox_rp_icon_<id>_r<rev>.png，文件名带 revision —— apply() 重建后 URL 变 → QML Image 重载，
    //   规避「同路径落盘新图但 Image 不重读」的陈旧缓存）。首次查询渲染 + 落盘，后续命中 O(1)。
    //   apply() 重建时清空（pack 切换 / 重解析 → 重渲）。红线 §9：渲染产物源自启用 pack 的贴图（运行期读
    //   本地 gitignored 包 + 程序图集），是派生缓存非提交资产，不进 qrc/VCS。
    QHash<int, QString> blockIconFiles;
    // t585 指南针/钟动画帧序列状态：帧文件 stem（"compass"/"clock"）→（帧数，上次帧 index）。帧数在
    //   ensureBuiltLocked 探测 item 目录实际存在的 <stem>_NN.png 数（demo 包实测 compass 32 / clock 64；
    //   用 QHash 因未来可扩别的逐帧物品）。lastIndex 供 updateAnimatedItemState 检出「帧真变」才递增
    //   animRevision（无变化零信号开销）。apply() 重建时清空（pack 切换重探测）。
    struct AnimFrames {
        QString stem;        // 帧文件名前缀（compass / clock）
        int count = 0;       // 探测到的帧文件数（0 = 无帧序列，回落静态图）
        // t585/t612 环值锚点（0..1，加在原始状态值上）：compass 帧 16/32 = 红针尖正上 → 状态 0（出生点
        //   在正前）对应帧 N/2 → 锚 0.5；clock 帧 0 = 太阳居中（正午）→ dayPhase 0 对应帧 0 → 锚 0.0
        //   （t612 逐帧像素取证修正 t585 误读：clock_32 是月亮居中 = 子夜非正午）。锚属帧序
        //   语义（哪个帧是零位），归 Core 单一权威；QML 只推原始状态（相对角/2π、dayPhase）。
        qreal anchor01 = 0.0;
        int lastIndex = -1;  // 上次推送的帧 index（-1 = 尚未推过）
        qreal lastState01 = 0.0; // t585 最近一次 updateAnimatedItemState 推送的原始状态值（查询侧换帧用）
    };
    QHash<int, AnimFrames> animItems;   // itemId(0x23F/0x240) → 帧序列态
    // t633 图鉴生物头像缓存：mobType→落盘的头部裁剪图标 file:// 路径（mobHeadIconSource 首次裁剪落盘后记）。
    //   apply() 重建时清空（pack 切换 / 重解析 → 重裁）。随 atlasFile 同目录写（AppLocalDataLocation，已 mkpath）。
    QHash<int, QString> mobHeadIconFiles;
    // t749 羊「毛身+真脸」合成贴图缓存（mobTextureSource(3) 首次合成落盘后记；apply() 重建时清空重合成）。
    QString sheepWoolFaceFile;
    // t645 生成式生物蛋 item 图标缓存：spawnEggId（0x20F..0x216/0x22C/0x22E）→落盘的两层染色蛋图标
    //   file:// 路径。pack 无 pig_spawn_egg.png 等独立文件（demo 包实测 9 蛋全 miss）→ 生成式路径：
    //   item/spawn_egg.png（灰度蛋形 base）染 mob 主色 + item/spawn_egg_overlay.png（斑点叠层）染
    //   mob 副色 → SourceOver 合成 → 落盘 voxelsandbox_rp_egg_<id>.png（retintCopperTemplate 同机制，
    //   运行期派生缓存非提交资产）。apply() 重建时清空（pack 切换 → 重染）。模板任一缺 → 返空回退
    //   MaterialIcon drawSpawnEgg 自绘。
    QHash<int, QString> spawnEggIconFiles;
    // t585 animRevision 进程级修订号（实例成员 m_animRevision 仅镜像 + 广播）。
    int animRevision = 0;
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

// t717 画作贴图目录（leaf="painting"；MC 1.0 布局 assets/minecraft/textures/painting/<name>.png 扁平）。
QString resolvePaintingDir(const QString &absPath)
{
    return resolveTexturesSubDir(absPath, QStringLiteral("painting"));
}

// t717 盔甲 layer 贴图目录（leaf="models/armor"——armor 贴图在 textures/models/armor/ 子树，两层级；
//   与其它 leaf 不同，须递归到 models/armor 两层。用 bounded DFS 的 leaf 参数带相对段）。
QString resolveArmorDir(const QString &absPath)
{
    if (absPath.isEmpty())
        return {};
    // 直接命中（pack 根标准布局 assets/minecraft/textures/models/armor）。
    const QDir root(absPath);
    if (!root.exists())
        return {};
    const QString direct = root.filePath(QStringLiteral("assets/minecraft/textures/models/armor"));
    if (QFileInfo(direct).isDir())
        return QDir::cleanPath(direct);
    // packPath 即 armor 目录自身 / 中间层：有界 DFS 找以 models/armor 结尾的目录。
    std::function<QString(const QDir &, int, int)> dfs =
            [&dfs](const QDir &dir, int depth, int maxDepth) -> QString {
        if (depth > maxDepth)
            return {};
        if (dir.dirName() == QStringLiteral("armor")) {
            const QString path = QDir::cleanPath(dir.absolutePath());
            if (path.endsWith(QStringLiteral("/models/armor"))
                    || path == QStringLiteral("models/armor"))
                return path;
        }
        const QStringList subs =
                dir.entryList(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
        for (const QString &sub : subs) {
            const QString r = dfs(QDir(dir.filePath(sub)), depth + 1, maxDepth);
            if (!r.isEmpty())
                return r;
        }
        return {};
    };
    return dfs(root, 0, 6);
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

// 读 settings.json：resourcePackEnabled（缺省 false）+ resourcePack（可空）+ playerSkin（t731，
//   "default"/"alex"，缺省 default）。
struct Settings {
    bool enabled = false;
    QString packPath;
    QString playerSkin;
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
    if (obj.contains(QStringLiteral("playerSkin")))
        s.playerSkin = obj.value(QStringLiteral("playerSkin")).toString();
    return s;
}

// t415 写 settings.json（enabled + packPath + playerSkin），保留其它已有字段。返回是否成功（失败已
//   告警，调用方降级——皮肤名/包路径仅存内存）。
bool writeSettings(bool enabled, const QString &packPath, const QString &playerSkin)
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
    obj.insert(QStringLiteral("playerSkin"), playerSkin);
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
        // t493 青金矿背景用包石头：tile 108 lapis_ore → pack 内 lapis_ore.png（包内 stone 底纹 + 青金斑，
        //   与普通 stone 风格一致 → 矿脉不再一眼可见）。非 pack 时回落程序生成 default_lapis_ore.png
        //   （自绘石头底 + 群青斑簇 + 黄铁矿金点）。包内缺该 PNG 时安全跳过（保留程序生成瓦片）。
        {108, QStringLiteral("lapis_ore.png")},          // lapis_ore（t493：pack 激活用包内贴图，背景与包 stone 一致）
        // t494 熔炉点燃态正面：tile 134 furnace_front_on → pack 内 furnace_front_on.png（拱洞内带火，机制等价
        //   MC 1.0 熔炉燃烧时正面发光）。mesher 据 Furnace state 的 FurnaceStateLitFlag 选 14(灭)/134(点燃)；
        //   非 pack 时回落程序生成 default_furnace_front_on.png（圆石底 + 拱框 + 亮黄橙火焰）。包内缺该 PNG
        //   时安全跳过（保留程序生成瓦片）。注：demo 包（1.8.2.2）有 furnace_front_on.png（带火正面）。
        {134, QStringLiteral("furnace_front_on.png")},   // furnace_front_on（t494：熔炉燃烧正面带火炉口）
        // t514 浆果丛 3 视觉阶段贴图（tile 103..105 → pack sweet_berry_bush_stage{0,1,2}.png）。mesher 在
        //   PartialBlockGeometry::append 的 SweetBerryBush case 据 state 选 tile = 103 + stage（0 无果嫩丛 / 1 小果 /
        //   2 成熟红浆果簇）。SweetBerryBushStageMax=2（项目用 3 阶段 0..2，MC 虽有 stage 3 但本工程不取 → stage3.png
        //   不映射）。非 pack 时回落程序生成 default_sweet_berry_bush_{0,1,2}.png（tools/build_sweet_berry.py 原创自绘）。
        //   包内缺某阶段 PNG 时安全跳过（保留程序生成瓦片，不崩）。
        {103, QStringLiteral("sweet_berry_bush_stage0.png")}, // sweet_berry_bush_0（阶段 0 无果嫩丛）
        {104, QStringLiteral("sweet_berry_bush_stage1.png")}, // sweet_berry_bush_1（阶段 1 小果）
        {105, QStringLiteral("sweet_berry_bush_stage2.png")}, // sweet_berry_bush_2（阶段 2 成熟红浆果簇）
        // t569 红石矿石：tile 138 redstone_ore → pack 内 redstone_ore.png（包内 stone 底纹 + 红石斑，与普通
        //   stone 风格一致，同 t493 lapis_ore 模式）。非 pack 时回落程序生成 default_redstone_ore.png（自绘
        //   石头底 + 鲜红菱斑矿粒）。包内缺该 PNG 时安全跳过（保留程序生成瓦片）。
        {138, QStringLiteral("redstone_ore.png")},           // redstone_ore（t569：pack 激活用包内贴图）
        // t582 南瓜三面（机制等价 MC 1.0 刻面南瓜）：tile 117/118/119 → pack block/pumpkin_side.png（侧=瓜棱）/
        //   pumpkin_face_off.png（前面=刻面双眼+锯齿嘴，未点燃态；_on 是南瓜灯点燃态非本方块）/ pumpkin_top.png
        //   （顶=瓜顶带茎）。非 pack 时回落程序生成 pumpkin_*.png（tools/build_pumpkin.py 自绘）。snow_golem.png
        //   实体贴图头部区只是雪 + 深色 derpy 脸（MC 1.8+ 南瓜不是 entity 贴图的一部分）→ 雪傀儡南瓜头 Model 走
        //   BlockCube{blockId:100} + 共享图集采样本三瓦片（pack 激活即 HD 南瓜头，修「头没有南瓜」）。
        //   包内缺 PNG 时安全跳过（保留程序生成瓦片）。
        {117, QStringLiteral("pumpkin_side.png")},           // pumpkin_side（南瓜侧面瓜棱）
        {118, QStringLiteral("pumpkin_face_off.png")},       // pumpkin_face（南瓜前面刻面双眼+锯齿嘴）
        {119, QStringLiteral("pumpkin_top.png")},            // pumpkin_top（南瓜顶/底瓜顶带茎）
        // t620 南瓜核实（blockId 100 六面映射全对：top/bottom=119 / side=117 / front(-Z)=118，t582 已接 +
        //   t610 已修 face_off 懒拷贝退化链）。pumpkin_face_on.png（点亮态）**不接**——本工程无南瓜灯方块 /
        //   点亮机制（Pumpkin 无 lit state，雪傀儡头也不发光），接了无消费方；仅作 t610 退化回退链的末位
        //   候选存在（ensureBuiltLocked 内 tile 118 分支），机制等价 MC jack o'lantern 留后续若加南瓜灯再接。
        // t609 投掷器：tile 139 dropper_front → pack block/dropper_front_horizontal.png（水平朝向正面小排出口）。
        //   顶/底/侧复用熔炉 tile 12/13（既存 {12→furnace_top.png}/{13→furnace_side.png} 映射自动覆盖）。竖直
        //   朝向版（dropper_front_vertical.png）待投掷器支持上下朝向时接入（本工程放置朝向恒水平 4 向 →
        //   先接 horizontal 一张不白块）。非 pack 时回落程序生成 default_dropper_front.png（tools/build_dropper.py
        //   原创自绘）。包内缺该 PNG 时安全跳过（保留程序生成瓦片）。
        {139, QStringLiteral("dropper_front_horizontal.png")}, // dropper_front（投掷器前面排出口；t609）
        // t620 发射器三面（tile 125/126/127）：MC 1.0 发射器顶/底=熔炉顶面、侧=熔炉侧面（demo 包无 dispenser_top/
        //   side 专属文件，vanilla 本就复用 furnace 系）；前面=dispenser_front_horizontal.png（水平朝向大暗腔
        //   排出口——本工程放置朝向恒水平 4 向，按文件名约定接 horizontal；竖直朝向版
        //   dispenser_front_vertical.png 待上下朝向支持时接入，留注释）。非 pack 回落程序生成
        //   default_dispenser_*.png（tools/build_dispenser.py 原创自绘）。
        {125, QStringLiteral("furnace_top.png")},               // dispenser_top（发射器顶/底=熔炉顶面；MC 复用 furnace_top）
        {126, QStringLiteral("furnace_side.png")},              // dispenser_side（发射器三侧=熔炉侧面；MC 复用 furnace_side）
        {127, QStringLiteral("dispenser_front_horizontal.png")}, // dispenser_front（发射器前面大暗腔排出口）
        // t620 附魔台两 tile：顶=enchanting_table_top.png；侧 tile 110 走专用合成（enchanting_table_side.png 裁掉
        //   顶部 0.25 空白 → 有效 0.75 部分整张贴 0.75 高侧面，见 ensureBuiltLocked 特判）。底=obsidian(77)（demo
        //   包实测 enchanting_table_bottom.png 与 obsidian.png 逐像素相同 → 复用既存 {77→obsidian.png} 不另立 tile）。
        {109, QStringLiteral("enchanting_table_top.png")},      // enchanting_table_top（附魔台顶面；t620）
        // t620 书架侧面：tile 111 → pack block/bookshelf.png（木板边框 + 中央书脊书列）。顶/底=planks(8)
        //   经既存 {8→oak_planks.png} 自动覆盖（per-face 见 BlockDef）。非 pack 回落 default_bookshelf.png。
        {111, QStringLiteral("bookshelf.png")},                 // bookshelf（书架侧/前面；t620 per-face）
        // t620 末影祭坛三 tile（EndPortal 方块的 endframe 化）：side(140)/top(141) 走专用合成（endframe_side.png
        //   裁顶部 3/16 空白；eye(142) = endframe_top.png + endframe_eye.png overlay 叠加——MC eye 贴图是中央局部
        //   图非整面），见 ensureBuiltLocked 特判。此处的直映射仅作「文件名存在性声明」，实际覆盖由特判完成。
        {140, QStringLiteral("endframe_side.png")},             // endframe_side（祭坛侧/底=灰白细孔框身；t620 裁剪合成）
        {141, QStringLiteral("endframe_top.png")},              // endframe_top（祭坛顶面（未放之眼）；t620）
        {142, QStringLiteral("endframe_eye.png")},              // endframe_eye（祭坛顶面（已放之眼）；t620 overlay 合成）
        // t620 门上下半 per-face 四 tile（PartialBlockGeometry door case 据 state bit3 选；kDefs topTile=upper/
        //   bottomTile=lower）。demo 包另有 oak_door_top/bottom.png + spruce_door_top/bottom.png（1.13+ flattening
        //   现代命名）——门类取 door_wood_*（1.8 命名）优先、缺则现代命名兜底须候选链，但 tileFilenameMap 是单值
        //   映射；实测 demo 包两组都在，取 door_wood_* / door_spruce_*（与任务给定路径一致）。
        {143, QStringLiteral("door_wood_upper.png")},           // door_wood_upper（橡木门上半：门板+格栅窗；t620）
        {144, QStringLiteral("door_wood_lower.png")},           // door_wood_lower（橡木门下半：门板+锁孔板；t620）
        {145, QStringLiteral("door_spruce_upper.png")},         // door_spruce_upper（云杉门上半；t620）
        {146, QStringLiteral("door_spruce_lower.png")},         // door_spruce_lower（云杉门下半；t620）
        // t620 铁轨直轨：tile 121 rail → pack block/rail_normal.png（NS 直轨：两根纵轨 + 周期枕木横带；demo 包
        //   实测两根纵轨在 x 16..31/96..111 带 = 纵向轨条，与程序 default_rail.png 同语义）。EW 直轨由 mesher
        //   UV 旋转 90° 复用本瓦片（一张两用，不另接 rail_horizontal）。拐角 tile 136 走专用合成（demo 包
        //   rail_normal_turned.png 是**右转**（南进东出），程序贴图 136 基准是**左转**（南进西出）→ 合成时水平
        //   镜像后覆盖，mesher 四象限 UV 映射零改动）；十字 tile 137 无 pack 等价（vanilla 交叉轨走模型层叠放
        //   非独立贴图）→ 不映射保程序生成。动力轨 rail_golden(_powered)/探测轨 rail_detector(_powered) 本工程
        //   无对应方块（无红石系统）→ 不接，留注释。非 pack 回落 default_rail*.png（tools/build_rail.py）。
        {121, QStringLiteral("rail_normal.png")},               // rail（直轨 NS；EW 由 mesher UV 旋转复用；t620）
        {136, QStringLiteral("rail_normal_turned.png")},        // rail_corner（90° 拐角；右转→水平镜像合成，t620）
        // t620 矿物存储块六面同贴图（机制等价 MC 1.0 coal/lapis/diamond/gold/redstone block；六面一张图）。
        //   112 iron_block 是 t477 遗漏（grep 实证 tileFilenameMap 无 112 条目 → pack 激活时铁块仍是程序贴图）；
        //   t620 补齐六件全量。非 pack 回落 default_*_block.png（tools/build_mineral_blocks.py 原创自绘）。
        //   包内缺某 PNG 时安全跳过（保留程序生成瓦片）。
        {112, QStringLiteral("iron_block.png")},                // iron_block（t477 铁块；t620 补 pack 映射漏项）
        {147, QStringLiteral("coal_block.png")},                // coal_block（煤炭块；六面同；t620）
        {148, QStringLiteral("lapis_block.png")},               // lapis_block（青金石块；六面同；t620）
        {149, QStringLiteral("diamond_block.png")},             // diamond_block（钻石块；六面同；t620）
        {150, QStringLiteral("gold_block.png")},                // gold_block（金块；六面同；t620）
        {151, QStringLiteral("redstone_block.png")},            // redstone_block（红石块；六面同；t620）
        // t620 红石灯两态贴图（机制等价 MC 1.0 redstone lamp off/on 两张）：152=off（灰暗壳）/ 153=on（暖黄
        //   亮芯）。mesher tileFor 据 RedstoneLamp state bit0（右键开关）选 152/153 全六面换。非 pack 回落
        //   default_redstone_lamp_off/on.png（tools/build_mineral_blocks.py 原创自绘）。
        {152, QStringLiteral("redstone_lamp_off.png")},         // redstone_lamp_off（红石灯 off 态灰暗壳；t620）
        {153, QStringLiteral("redstone_lamp_on.png")},          // redstone_lamp_on（红石灯 on 态暖黄亮芯；t620）
        // t627 压力板家族扩展三 tile：MC 现代命名 stone_pressure_plate / heavy(铁)_weighted_pressure_plate /
        //   light(金)_weighted_pressure_plate（demo 包 1.8.2.2 实测**无**这三个文件——1.8 的铁/金板是
        //   weighted_pressure_plate 命名族，demo 包未收录；缺则安全跳过保程序生成瓦片，映射慷慨无害）。
        //   非 pack 回落 default_*_pressure_plate.png（tools/build_pressure_plates.py 原创自绘）。
        {154, QStringLiteral("stone_pressure_plate.png")},          // stone_pressure_plate（石压力板；t627）
        {155, QStringLiteral("heavy_weighted_pressure_plate.png")}, // iron_pressure_plate（铁/重质压力板；t627）
        {156, QStringLiteral("light_weighted_pressure_plate.png")}, // gold_pressure_plate（金/轻质压力板；t627）
        // t620 仙人掌底面：Cactus def 底面 tile=54（cactus_top 复用——程序贴图时代顶底同图）；pack 内
        //   cactus_bottom.png 与 cactus_top.png 像素实测不同（251/256 像素差，底面更暗、无中央凹陷）。但
        //   本工程仙人掌是 0.8 细柱（PartialBlockGeometry Cactus case）—— pushBox 侧·底统一用 sideTile(55)，
        //   仅 +Y 顶面用 topTile(54)；**底面瓦片无消费方**（mesher 不读 bottomTile、掉落物 BlockCube 走
        //   tileIndex(Bottom) 但柱底永贴沙 / 下段仙人掌不可见）→ 不接 cactus_bottom 映射（接了无渲染路径
        //   读它；如未来仙人掌改 per-face 再补）。
        // t620 红石火把不做：redstone_torch_on/off.png 在包内，但红石火把方块未实现（依赖红石系统的恒亮 /
        //   熄灭态语义，PLAN 无红石系统）→ 不接映射（无消费方）；留待红石系统任务再接。
        // t638 翻案：红石火把方块已建（RedstoneTorch，常亮 on 装饰光源 光 7）→ 接 tile 161 映射（on 常亮态；
        //   off 熄灭态不接——本方块恒亮）。非 pack 回落 default_redstone_torch.png（tools/build_rail_family.py
        //   原创自绘）。包内缺 PNG 时安全跳过。
        {161, QStringLiteral("redstone_torch_on.png")},        // redstone_torch（t638 红石火把常亮态；cross cutout）
        // t620 铁轨动力 / 探测轨不做：rail_golden(_powered) / rail_detector(_powered) 在包内，但本工程无
        //   对应方块（无红石系统 → 无激活态语义）→ 不接；同 t620 第 1 部分直轨 / 拐角的注释口径。
        // t638 翻案：动力 / 探测轨方块已建（GoldenRail / DetectorRail）→ 接断常态两映射 + 探测轨通电变体。
        // t703 修「动力 / 探测轨贴图模糊」：demo 包内**现代命名**（powered_rail.png / detector_rail.png /
        //   detector_rail_on.png / powered_rail_on.png）实测全是 16×16 缩略副本，而**老命名**（rail_golden.png
        //   / rail_golden_powered.png / rail_detector.png / rail_detector_powered.png）才是 128×128 HD 原图
        //   （与普通轨 rail_normal.png 同级）。t638 注释「两组逐像素等价」只在降采样后成立——原生尺寸不等价。
        //   旧映射取现代名 → 16px 被 SmoothTransformation 拉到 64px = 糊；tile 121 普通轨恰好映射老名
        //   rail_normal.png（128px）所以清晰（用户实测对照）。修 = 四瓦片全改老命名 HD 图。
        //   另补 159（动力轨通变态）映射：t658 起 GoldenRailStateOnFlag 已消费 tile 159（电力驱动换贴图），
        //   不映射则通电金轨仍是程序 16→64 贴图（与断常 HD 瓦片混排突兀）。非 pack 回落
        //   default_rail_{golden,detector}*.png（tools/build_rail_family.py 原创自绘）。包内缺 PNG 安全跳过。
        {157, QStringLiteral("rail_golden.png")},           // golden_rail（t638 动力铁轨断常态；t703 老名 HD 128px）
        {158, QStringLiteral("rail_detector.png")},         // detector_rail（t638 探测铁轨断常态；t703 老名 HD 128px）
        {159, QStringLiteral("rail_golden_powered.png")},   // rail_golden_on（t703 动力轨通变态——老名 HD；t658 电力驱动消费）
        {160, QStringLiteral("rail_detector_powered.png")}, // detector_rail_on（t638 探测轨通电视觉；t703 老名 HD）
        // t638 仙人掌底面翻案（t620「不接」被观察者视角观察推翻）：tile 163 = cactus_bottom.png（包内实测
        //   与 cactus_top 像素不同——底面更暗、无中央凹陷）。Cactus def bottomTile=163；mesher pushBox
        //   -Y 底面读 bottomTile（partialblockgeometry t638 加 bottomTile 参数）。非 pack 回落
        //   default_cactus_bottom.png（tools/build_cactus.py 程序生成）。包内缺 PNG 安全跳过。
        {163, QStringLiteral("cactus_bottom.png")},            // cactus_bottom（t638 仙人掌底面；观察者视角可见）
        // t638 附魔台顶摊开书（tile 162=enchant_book）：**无 pack 等价**（MC 的书是独立实体模型非方块贴图；
        //   包内无 enchant_book.png）→ 不映射，程序贴图恒用（tools/build_book.py：白纸底 + 灰字线 + 中央
        //   书脊暗线——PartialBlockGeometry EnchantingTable case 顶书页盒专用）。留注释防未来误接。
        // t646 补映射批（2026-08-17 用户 pack block/ 目录实测审计：以下贴图包内全存在但未接 → pack 启用
        //   时世界贴图仍走程序瓦片）。机制同既有直映射（包缺文件安全跳过保程序瓦片；接上后创造 3D 图标
        //   采共享图集自动吃到 pack 贴图）。**取现代（1.13+ flattening）命名**——旧名（web.png /
        //   cobblestone_mossy.png / stonebrick.png / chipped_anvil_top.png / damaged_anvil_top.png）不碰。
        {102, QStringLiteral("spruce_planks.png")},    // spruce_planks（云杉木板/台阶/栅栏共用 tile 102；包内残留
                                                       //   `spruce_planks (2).png` 是垃圾文件不引用）
        {106, QStringLiteral("packed_ice.png")},       // packed_ice（浮冰；程序名 default_pack_ice）
        {107, QStringLiteral("blue_ice.png")},         // blue_ice（蓝冰）
        {113, QStringLiteral("anvil_top.png")},        // anvil_top（完好铁砧顶面；Anvil per-face topTile）
        {114, QStringLiteral("anvil_base.png")},       // anvil_base（铁砧三阶段共享底·侧·前面）
        {115, QStringLiteral("anvil_top_damaged_1.png")}, // anvil_top_damaged_1（微损铁砧顶；勿用 chipped_anvil_top
                                                       //   .png = 现代 1.19+ 命名）
        {116, QStringLiteral("anvil_top_damaged_2.png")}, // anvil_top_damaged_2（重损铁砧顶；勿用 damaged_anvil_top.png）
        {120, QStringLiteral("cobweb.png")},           // cobweb（蜘蛛网 cross cutout；web.png 是 1.8 旧名同图，取现代名）
        {123, QStringLiteral("cut_sandstone.png")},    // cut_sandstone（切制砂岩）
        {124, QStringLiteral("mossy_cobblestone.png")}, // mossy_cobble（苔石；cobblestone_mossy.png 旧命名同图，取现代名）
        {128, QStringLiteral("stone_bricks.png")},     // stone_brick（石砖/台阶/楼梯共用 tile 128；stonebrick.png 是
                                                       //   1.8 旧名，取现代名）
        {131, QStringLiteral("lever.png")},            // lever（拉杆贴地扳手）
        {135, QStringLiteral("brown_mushroom.png")},   // brown_mushroom（白蘑菇 cross cutout；tile 62 红蘑菇已接
                                                       //   red_mushroom.png，不同瓦片勿混）
        // t646 TNT per-face（t638 铁轨家族同套流程）：包内 tnt_side/tnt_top/tnt_bottom 三贴图实测存在。
        //   TntBlock def t646 改 topTile=164/bottomTile=165/sideTile=frontTile=122（此前四槽全 122 顶底也用
        //   侧图）→ 此处接 122→tnt_side（侧·前面：捆带+中央标识）/ 164→tnt_top（顶：引线接口俯视）/
        //   165→tnt_bottom（底：纯药柱底板无标记）。非 pack 回落 tools/build_tnt.py 程序三图（tile 122 与
        //   t485 版本字节一致）。包内缺 PNG 安全跳过。
        {122, QStringLiteral("tnt_side.png")},         // tnt（TNT 侧·前面：深红药柱+捆带+中央标识；t646 per-face）
        {164, QStringLiteral("tnt_top.png")},          // tnt_top（TNT 顶面：药柱截面+中央引线接口俯视；t646）
        {165, QStringLiteral("tnt_bottom.png")},       // tnt_bottom（TNT 底面：纯药柱底板+暗捆带；t646）
        // t646 按钮 vanilla 复用（机制等价 MC 1.0 wooden/stone button 本就复用 planks/cobblestone 贴图——
        //   无独立按钮方块贴图）：tile 132→oak_planks.png / 133→cobblestone.png 两行。非 pack 回落程序
        //   default_wood_button.png / default_stone_button.png（tools/build_lever_button.py 自绘按钮钮面）。
        {132, QStringLiteral("oak_planks.png")},       // wood_button（木按钮=木板贴图；MC vanilla 复用 planks）
        {133, QStringLiteral("cobblestone.png")},      // stone_button（石按钮=圆石贴图；MC vanilla 复用 cobblestone）
        // t656 红石粉导线不接 pack：vanilla redstone_dust_line.png 是**灰度可着色**瓦片（非红粉色，需 tint
        //   合成才显红）——直接接上会渲染成灰白粉线（同睡莲 t444「现灰」根因）。断 / 通四瓦片（166..169）
        //   暂走程序红粉观感；pack tint 合成留后续任务（tileTint 睡莲模式），留注释防未来误接。
        // t657 红石火把熄灭态：mesher 据 state 的 RedstoneTorchStateOffFlag 换 tile 170（redstone_torch_off，
        //   机制等价 MC 1.0 redstone torch off 双态贴图；on 态恒 tile 161 已接）。非 pack 回落
        //   default_redstone_torch_off.png（tools/build_rail_family.py 姊妹脚本自绘）。包内缺 PNG 安全跳过。
        {170, QStringLiteral("redstone_torch_off.png")}, // redstone_torch_off（t657 熄灭态；反相器 NOT 门视觉）
        // t714 云杉树叶：tile 175 spruce_leaves → pack 内 spruce_leaves.png（demo 包实存；**灰度可着色**瓦片
        //   —— 实测三通道均 130 灰，需乘 tint 合成才显深蓝绿针叶，同 oak_leaves t416 模式）。非 pack 回落
        //   程序生成 default_spruce_leaves.png（tools/build_spruce.py 自绘深蓝绿针叶 + 透明孔）。包内缺 PNG
        //   时安全跳过（保留程序生成瓦片）。
        {175, QStringLiteral("spruce_leaves.png")},   // spruce_leaves（t714 云杉树叶；灰度 + tint 合成）
        // t717 铁门 / 铁活板门三 tile（R19.10 t722/t723 贴图前置；IronDoor/IronTrapdoor 方块后建）。
        //   与木门 143..146 同族直映射（demo 包 block/ 实测 door_iron_upper.png / door_iron_lower.png
        //   （HD 128px，1.8 老命名）与 iron_trapdoor.png 都在；iron_door_top/bottom.png 是 16px 缩略
        //   副本，取老命名 HD 版，同 t703 铁轨老名优先先例）。门上半退化检测（143/145 纯板无窗跳过）
        //   不覆盖 176 —— 铁门窗语义由 t722 接几何时按需加。非 pack 回落 default_door_iron_* /
        //   default_iron_trapdoor.png（tools/build_doors_iron.py 原创自绘）。包内缺 PNG 安全跳过。
        {176, QStringLiteral("door_iron_upper.png")},  // door_iron_upper（t717 铁门上半：门板+格栅窗）
        {177, QStringLiteral("door_iron_lower.png")},  // door_iron_lower（t717 铁门下半：门板+锁孔板）
        {178, QStringLiteral("iron_trapdoor.png")},    // iron_trapdoor（t717 铁活板门：格子板+栅格孔）
        // t761 沙砾：tile 179 gravel → pack 内 gravel.png（demo 包实存）。非 pack 回落程序生成
        //   default_gravel.png（tools/build_gravel.py 自绘灰砾石 + 卵石碎砾斑）。包内缺 PNG 时安全跳过。
        {179, QStringLiteral("gravel.png")},           // gravel（t761 沙砾：灰砾石+卵石碎砾斑）
    };
    return kMap;
}

// t420「引擎物品 id → 包内 item 标准贴图文件名」映射（item-ids.md 单一权威；id 取 toolregistry.h ToolId /
//   recipe.h MaterialId / ArmorId 段）。文件名用现代（1.13+ flattening）标准 item 命名（wooden_pickaxe /
//   iron_ingot / cooked_beef ...），与现网大多数资源包 assets/minecraft/textures/item/ 一致。包内缺该 PNG
//   时 itemIconSource 安全跳过（回退自绘），故映射可慷慨：唯一要保证的是 id↔文件名配对正确，而非文件名都存在。
//   工具段 0x100..0x112（镐/锄/斧/铲/剑×木/石/铁 + 弓/剪刀/钓竿 + t472 钻石镐）；材料段 0x200..0x231（合成材料 / 食物 / 桶 /
//   mob 掉落 / 生物蛋 / 战利品 / 鸡鱿鱼族 / 胡萝卜马铃薯 / 生鱼）；护甲段 0x300..0x313（皮革/铁/铜/金/钻石×4 部位；
//   铜护甲 t613 入映射 copper_*，老包缺 → copperIronFallback 用 iron_* 染铜）。raw_*（铜/金/铁原矿物品 1.17+）/
//   spawn_egg_*/oak_sapling 等旧版 / HD 包常缺 → 缺则跳过回退自绘，不崩。
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
        // t557 金工具（MC 1.0 存在 → 现代命名 golden_*，现网资源包普遍有）；铜工具（1.17+ → copper_*，缺则跳过回退自绘）。
        {0x113, QStringLiteral("golden_pickaxe.png")},  // 金镐
        {0x114, QStringLiteral("golden_axe.png")},      // 金斧
        {0x115, QStringLiteral("golden_shovel.png")},   // 金铲
        {0x116, QStringLiteral("golden_sword.png")},    // 金剑
        {0x117, QStringLiteral("golden_hoe.png")},      // 金锄
        {0x118, QStringLiteral("copper_pickaxe.png")},  // 铜镐（1.17+；缺则跳过）
        {0x119, QStringLiteral("copper_axe.png")},      // 铜斧（缺则跳过）
        {0x11A, QStringLiteral("copper_shovel.png")},   // 铜铲（缺则跳过）
        {0x11B, QStringLiteral("copper_sword.png")},    // 铜剑（缺则跳过）
        {0x11C, QStringLiteral("copper_hoe.png")},      // 铜锄（缺则跳过）
        // t589 钻石工具补全（斧 / 铲 / 剑 / 锄；demo 包 1.8.2.2 item 目录实测四图全有）。
        {0x11D, QStringLiteral("diamond_axe.png")},     // 钻石斧
        {0x11E, QStringLiteral("diamond_shovel.png")},  // 钻石铲
        {0x11F, QStringLiteral("diamond_sword.png")},   // 钻石剑
        {0x120, QStringLiteral("diamond_hoe.png")},     // 钻石锄
        {0x121, QStringLiteral("flint_and_steel.png")}, // t724 打火石（MC 1.0 flint and steel）
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
        {0x227, QStringLiteral("enchanted_book.png")},  // t615 附魔书（真附魔：附魔台附书产 / 地牢战利品；demo 包实测存在）
        {0x228, QStringLiteral("feather.png")},         // 羽毛
        {0x229, QStringLiteral("chicken.png")},         // 生鸡肉
        {0x22A, QStringLiteral("cooked_chicken.png")},  // 熟鸡肉
        {0x22B, QStringLiteral("egg.png")},             // 蛋
        {0x22C, QStringLiteral("chicken_spawn_egg.png")},// 生物蛋（鸡）
        {0x22D, QStringLiteral("ink_sac.png")},         // 墨囊
        {0x22E, QStringLiteral("squid_spawn_egg.png")}, // 生物蛋（鱿鱼）
        {0x22F, QStringLiteral("carrot.png")},          // 胡萝卜
        {0x230, QStringLiteral("potato.png")},          // 马铃薯
        // t669 毒马铃薯（0x241，机制等价 MC 1.0 poisonous potato）：pack item 目录通常有 poisonous_potato.png
        //   （demo 包 1.8 系含 textures/item/poisonous_potato.png）。包内缺则安全跳过（保留自绘 drawPoisonPotato）。
        {0x241, QStringLiteral("poisonous_potato.png")}, // 毒马铃薯（t669：绿皮毒薯；食后 60% 中毒）
        {0x231, QStringLiteral("cod.png")},             // 生鱼（MC 1.0 raw fish = modern cod）
        // t645 用户审计补映射（pack item/ 目录 435 文件与既有映射差集；这些物品已实现但映射漏 → pack 启用仍走
        //   MaterialIcon 自绘）。demo 包实测 9 文件全在；包内缺则安全跳过回退自绘（机制同既有段）。
        //   0x238 book.png 是普通书（书配方产物 / 附魔台材料）——勿与 0x227 enchanted_book.png 混淆（已接）。
        {0x232, QStringLiteral("bone_meal.png")},      // 骨粉（t447：骨头合成产物；右键作物催熟）
        {0x233, QStringLiteral("sweet_berries.png")},  // 甜浆果（t467：雪原浆果丛采摘；可食 +2 饥饿）
        {0x234, QStringLiteral("oak_boat.png")},       // 橡木船 item（t469：5 橡木木板合成；右键水面放船）
        {0x235, QStringLiteral("spruce_boat.png")},    // 云杉船 item（t469：5 云杉木板合成）
        {0x236, QStringLiteral("lapis_lazuli.png")},   // 青金石（t471：青金矿石挖掘掉落；附魔台消耗材料）
        {0x237, QStringLiteral("paper.png")},          // 纸（t473：3 甘蔗横排合成）
        {0x238, QStringLiteral("book.png")},           // 书（t473：3 纸 + 1 皮革合成；附魔台/书架材料；非 enchanted_book）
        {0x239, QStringLiteral("gunpowder.png")},      // 火药（t485：杀潜行者掉落；TNT 合成原料）
        // t497 末影之眼（EndEyeId=0x23A）：机制等价 MC 1.0 ender eye（要塞宝藏箱战利品；右键末地传送门激活）。
        //   t487 引入物品但 itemFilenameMap 漏映射 → pack 启用时仍走自绘 Canvas（drawEndEye）。补映射 → pack 有
        //   ender_eye.png 时改用包内贴图（alpha-test 透明底，机制等价 MC item icon）；包缺 → 安全跳过保自绘。
        //   注：MC「末影珍珠 ender_pearl」是另一物品（合成末影之眼的原料），本工程无独立物品 id 故不映射；
        //   本工程的「末影之眼」即机制等价物，故 ender_eye.png 是其正确 pack 图标。
        {0x23A, QStringLiteral("ender_eye.png")},        // 末影之眼（t497：pack 启用用包内贴图，回落 drawEndEye 自绘）
        // t507 木碗 / 蘑菇汤（bowl / mushroom_stew）：pack item 目录通常有 bowl.png / mushroom_stew.png。包内缺则
        //   安全跳过（保留自绘 MaterialIcon）。
        {0x23B, QStringLiteral("bowl.png")},              // 木碗（t507）
        {0x23C, QStringLiteral("mushroom_stew.png")},     // 蘑菇汤（t507）
        // t505 雪球（snowball）：pack item 目录通常有 snowball.png（demo 包 1.8.2.2 含 textures/item/snowball.png）。
        //   包内缺则安全跳过（保留自绘 MaterialIcon drawSnowball）。机制等价 MC 1.0 snowball item icon。
        {0x23D, QStringLiteral("snowball.png")},         // 雪球（t505：pack 启用用包内贴图，回落 drawSnowball 自绘）
        // t645 矿车（MinecartId=0x23E，t565 引入物品但本映射漏 + MaterialIcon 连 case 都没有 → pack 关时空白）。
        //   补映射（demo 包实测 minecart.png 在）+ MaterialIcon 补 drawMinecart 自绘回退分支（pack 关时不空白）。
        {0x23E, QStringLiteral("minecart.png")},         // 矿车（t645：5 铁锭合成；右键铁轨放置 + 骑乘）
        // t585 指南针/钟静态回落映射：pack 关闭动画帧序列（无 <stem>_NN.png 帧文件）但 item 目录有静态
        //   compass.png / clock.png 时，itemIconSource 返静态图（不动，机制等价 MC 无动画时的静态 item 贴图）；
        //   有帧序列时 animatedItemFrameSource 优先（按状态选帧）。两文件 demo 包实测存在。
        {0x23F, QStringLiteral("compass.png")},          // 指南针静态图（t585 回落）
        {0x240, QStringLiteral("clock.png")},            // 钟静态图（t585 回落）
        // t720 画作（PaintingId=0x242；机制等价 MC 1.0 painting item 321）：pack item 目录通常有 painting.png
        //   （MC 1.8 系实测 textures/item/painting.png 是空 item 贴图——MC 画作实体画面在 painting/ 目录；
        //   此处接 item 图标映射，包内缺则安全跳过回退 MaterialIcon drawPainting 自绘）。
        {0x242, QStringLiteral("painting.png")},         // 画作（t720：8 木棒+1 羊毛合成；右键墙贴画）
        // t726 暗渊链路（机制等价 MC 1.0 ender pearl / blaze powder / blaze rod；结成暗渊之眼端剂）：
        //   pack item 目录有 ender_pearl.png / blaze_powder.png / blaze_rod.png 则接（alpha-test 透明底，
        //   机制等价 MC item icon）；包缺 → 安全跳过保自绘（drawEnderPearl / drawBlazePowder / drawBlazeRod）。
        //   生物蛋（夜行者）无独立 pack 贴图（走引擎 MaterialIcon drawSpawnEgg 自绘；夜行者本体贴图经
        //   entityKindMap "nightwalker" 单独映射到 enderman/enderman.png，见 entitySource —— 生物蛋 items
        //   不占 itemFileNameMap）。
        {0x243, QStringLiteral("ender_pearl.png")},      // 暗渊珠（t726：杀夜行者掉落；暗渊之眼原料）
        {0x244, QStringLiteral("blaze_powder.png")},     // 燃烬粉（t726：燃烬棒冶炼产物；暗渊之眼原料）
        {0x245, QStringLiteral("blaze_rod.png")},        // 燃烬棒（t726：怒焰人死亡掉落；烧燃烬粉）
        // t761 燧石（材料段 0x248；机制等价 MC 1.0 flint item 318）：pack item 目录有 flint.png 则接
        //   （demo 包实存）；包缺 → 安全跳过回退 MaterialIcon drawFlint 自绘。来源 = 挖沙砾小概率掉落，
        //   打火石配方原料（t724 占位「圆石+铁锭」→ t761 改回正统「燧石+铁锭」）。
        {0x248, QStringLiteral("flint.png")},            // 燧石（t761：挖沙砾概率掉落；打火石配方原料）
        // —— 护甲段（ArmorId；皮革/铁/铜/金/钻石×4 部位。铜护甲 t613 入映射：现代包 copper_* 直用；老包
        //   缺 copper_* → itemIconSource 走 copperIronFallback 用 iron_* 染铜（描边带 + 铜橙梯度））——
        {0x300, QStringLiteral("leather_helmet.png")},
        {0x301, QStringLiteral("leather_chestplate.png")},
        {0x302, QStringLiteral("leather_leggings.png")},
        {0x303, QStringLiteral("leather_boots.png")},
        {0x304, QStringLiteral("iron_helmet.png")},
        {0x305, QStringLiteral("iron_chestplate.png")},
        {0x306, QStringLiteral("iron_leggings.png")},
        {0x307, QStringLiteral("iron_boots.png")},
        // t613 铜护甲四件（0x308..0x30B；1.17+ 命名 copper_*。现代包有则直用；demo 包等老包缺 →
        //   回退 iron_* 染铜，同铜工具 0x118.. 的 t588 机制）。
        {0x308, QStringLiteral("copper_helmet.png")},
        {0x309, QStringLiteral("copper_chestplate.png")},
        {0x30A, QStringLiteral("copper_leggings.png")},
        {0x30B, QStringLiteral("copper_boots.png")},
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
//   shambler=4/bones=5/stalker=6/spider=7/chicken=8/squid=9/snow_golem=12/iron_golem=13）。文件名用 MC 1.0 entity 子目录命名
//   （entity/<mob>/<mob>.png，现网大多数包此布局；mobTextureSource 在子目录缺时自动回退扁平 entity/<mob>.png 兼容
//   旧 / HD 包）。机制等价 MC 1.0 mob 外观，标识符 / 名称全原创（§9 区隔：Shambler↔zombie / Bones↔skeleton /
//   Stalker↔creeper）。包内缺该 PNG 时 mobTextureSource 安全跳过（回退程序生成 / 纯色），故映射可慷慨：唯一要
//   保证的是 id↔目录配对正确。t730 起鱿鱼 Squid(9) 已映射（squid/squid.png，扁平回退 entity/squid.png；
//   旧注「不映射保留程序生成」作废——miss 时仍自动回退程序生成 mob_squid 贴图，语义不变）。
//   feat（雪/铁傀儡）：SnowGolem(12) → 扁平 entity/snow_golem.png（demo 包 1.8.2.2 实测扁平布局，无子目录；
//   mobTextureSource 子目录探测 miss 后自动回退扁平命中）；IronGolem(13) → 子目录 entity/iron_golem/iron_golem.png
//   （demo 包子目录布局，含 iron_golem.png + crackiness 系列）。两傀儡 pack 命中后 Main.qml delegate 把几何切到
//   MobModel + T 字 UV 展开进该贴图（雪块身 / 铁块身显 pack 纹理）；pack 关 → 纯色雪白 / 铁灰（修 dev-plan C
//   「铁傀儡全白」：pack iron_golem.png 铁纹才显铁质，程序纯色铁灰读作「白」）。
const QList<QPair<int, QString>> &mobEntityMap()
{
    static const QList<QPair<int, QString>> kMap = {
        {1, QStringLiteral("pig/pig.png")},         // MobPig → entity/pig/pig.png
        {2, QStringLiteral("cow/cow.png")},         // MobCow → entity/cow/cow.png
        {3, QStringLiteral("sheep/sheep_fur.png")}, // MobSheep → entity/sheep/sheep_fur.png（t593：羊毛层贴图。sheep.png 本体 = 无毛粉肉身（用户「羊是无羊毛版本，怪怪的，应长满羊毛」）；MC 1.8 羊 = 本体 + 羊毛两层模型，本工程单贴图 MobModel 取毛层即「长满羊毛」。剪羊毛态（shearedAt）走 Main.qml 裸肤色 Model，不走本贴图）
        {4, QStringLiteral("zombie/zombie.png")},   // MobShambler → entity/zombie/zombie.png（机制等价 zombie，§9 改名）
        {5, QStringLiteral("skeleton/skeleton.png")},// MobBones → entity/skeleton/skeleton.png（机制等价 skeleton，§9 改名）
        {6, QStringLiteral("creeper/creeper.png")}, // MobStalker → entity/creeper/creeper.png（机制等价 creeper，§9 改名）
        // t596 核验（用户「蜘蛛是不是没找到贴图？」）：demo 包实存 entity/spider/spider.png（256×128 = base 64×32 的 4×），
        //   映射路径 / 大小写均正确；mobmodel.cpp 蜘蛛三组 box-UV（head(32,4)8³ / body1(0,12)10×8×12 / leg(18,0)16×2×2）
        //   六面像素区逐一实测 100% 不透明 → pack 路径无缺口。用户观感「无贴图」实为 t597：delegate baseColor 暗色
        //   (0.16,0.10,0.10) 乘 pack 贴图致近乎全黑（修于 Main.qml，非本映射）。
        {7, QStringLiteral("spider/spider.png")},   // MobSpider → entity/spider/spider.png
        {8, QStringLiteral("chicken/chicken.png")}, // MobChicken → entity/chicken/chicken.png
        // t730 鱿鱼（MobSquid，机制等价 MC 1.0 squid，§9 改名原创）：demo 包实存**扁平** entity/squid.png
        //   （512×256 = base 64×32 的 8×，无子目录）——主候选 squid/squid.png 子目录 miss 后自动回退扁平命中
        //   （mobTextureSource 两级探测，同雪傀儡）。像素实测布局：mantle 12×16×12 @ (0,0)（六面区 100% 不透明）
        //   + 8 触腕共用 2×12×2 @ (48,0) 单区（mobmodel.cpp setMobTex 镜像，单一权威在 Renderer）。
        {9, QStringLiteral("squid/squid.png")},  // MobSquid → entity/squid/squid.png（扁平回退 entity/squid.png）
        {12, QStringLiteral("snow_golem.png")},     // MobSnowGolem → entity/snow_golem.png（扁平；demo 包无子目录，mobTextureSource 子目录 miss 后回退扁平命中）
        {13, QStringLiteral("iron_golem/iron_golem.png")}, // MobIronGolem → entity/iron_golem/iron_golem.png（子目录；mobTextureSource 命中子目录）
        {16, QStringLiteral("enderman/enderman.png")}, // MobNightwalker → entity/enderman/enderman.png（t727 夜行者，机制等价 MC enderman，§9 改名；眼睛发光层走 entitySource("nightwalker_eyes") 独立取，不占本 body 表项）
        {17, QStringLiteral("blaze/blaze.png")}, // MobEmberling → entity/blaze/blaze.png（t728 燃烬者，机制等价 MC blaze，§9 改名；单头悬浮 - 头盒 UV 从该贴图采样）
    };
    return kMap;
}

// t715「引擎状态效果枚举（PlayerState::StatusEffect 序：1=Poison/2=Slowness/3=Fire）→ pack mob_effect 文件名」
//   映射（功能性元数据，红线 §9 可随代码提交；贴图文件本身不进仓库）。effectIconSource 逐枚举探测
//   <effectDir>/<name>.png，命中返 file:// URL；全缺返空（Main.qml 效果栏回退 qrc icon_effect_*.png 自绘）。
//   v1 三效果：poison / slowness / fire（mob_effect 标准文件名；fire 在 mob_effect 目录无独立文件（MC 用 HUD
//   火焰而非图标）→ miss 回退自绘属预期）。Core 不依赖 Game 故用字面量 + 注释钉死序号（同 itemFilenameMap 例）。
QStringList effectIconFiles(int effectType)
{
    switch (effectType) {
    case 1: return { QStringLiteral("poison.png") };    // EffectPoison 中毒
    case 2: return { QStringLiteral("slowness.png") };  // EffectSlowness 缓慢
    case 3: return { QStringLiteral("fire.png") };      // EffectFire 着火（多数包无此文件 → 回退自绘）
    default: return {};
    }
}

// t717「引擎画作 index → pack painting 文件名」表（27 张，demo 包 painting/ 目录逐名镜像；功能性元数据，
//   红线 §9 可随代码提交；贴图文件本身不进仓库）。name 是内部 key（非 UI 专名——玩家可见面只显画）。
//   index 与 tools/build_paintings.py PAINTINGS 表序一致（单一权威在生成器；Core 不依赖 tools 故注释互指
//   + 字面量镜像，同 itemFilenameMap 模式）。paintingSource 逐 index 探测 paintingDir/<name>.png，
//   miss 回退 qrc 程序贴图 default_painting_<name>.png。表顺序变更会致 index↔名字错位（存档 state 编码
//   index）→ 加条目只允许 push_back 到尾部。
const QStringList &paintingNames()
{
    static const QStringList kNames = {
        QStringLiteral("kebab"),            // 0  16×16
        QStringLiteral("aztec"),            // 1  16×16
        QStringLiteral("aztec2"),           // 2  16×16
        QStringLiteral("bomb"),             // 3  16×16
        QStringLiteral("plant"),            // 4  16×16
        QStringLiteral("wasteland"),        // 5  16×16
        QStringLiteral("back"),             // 6  16×16
        QStringLiteral("alban"),            // 7  16×16
        QStringLiteral("courbet"),          // 8  32×16
        QStringLiteral("sea"),              // 9  32×16
        QStringLiteral("creebet"),          // 10 32×16
        QStringLiteral("sunset"),           // 11 32×16
        QStringLiteral("pool"),             // 12 32×16
        QStringLiteral("graham"),           // 13 16×32
        QStringLiteral("wanderer"),         // 14 16×32
        QStringLiteral("match"),            // 15 32×32
        QStringLiteral("skull_and_roses"),  // 16 32×32
        QStringLiteral("stage"),            // 17 32×32
        QStringLiteral("void"),             // 18 32×32
        QStringLiteral("bust"),             // 19 32×32
        QStringLiteral("wither"),           // 20 32×32
        QStringLiteral("donkey_kong"),      // 21 64×48
        QStringLiteral("skeleton"),         // 22 64×48
        QStringLiteral("burning_skull"),    // 23 64×64
        QStringLiteral("pigscene"),         // 24 64×64
        QStringLiteral("pointer"),          // 25 64×64
        QStringLiteral("fighters"),         // 26 64×32
    };
    return kNames;
}

// t717「实体贴图 kind →（pack entity 相对路径候选， 程序回退 qrc 文件名）」表（R19.10 夜行者 / 燃烬者 /
//   鱿鱼 / 矿车 / 附魔书 / 玩家皮肤 t727/t728/t730/t731/t732 接入前置）。kind 取值经 entitySource(kind)
//   字符串参数传入（QML 字面量，非枚举——实体贴图消费方分散在 Main.qml 各 delegate / bookHost / cartHost，
//   字符串 key 同 effectIconSource(effectType int) 的解耦思路但更贴呈现层既有惯例）。两级候选：子目录布局
//   （entity/enderman/enderman.png）优先、扁平（entity/enderman.png）兜底（mobTextureSource probe 同机制）。
//   miss / 非 active 回退 qrc 程序贴图（tools/build_entities_pack.py 原创自绘；§9 改名：Enderman→夜行者
//   Nightwalker、Blaze→燃烬者 Emberling——程序文件名用原创名，pack 文件名用 MC 名（映射元数据红线 §9 允许））。
struct EntityTexEntry {
    const char *kind;      // 呈现层字符串 key
    const char *packPath;  // entity/ 下相对路径（子目录布局；扁平回退由探测取 fileName）
    const char *fallback;  // qrc 程序贴图文件名（textures/ 下；空 = 无程序回退）
};
const QList<EntityTexEntry> &entityKindMap()
{
    static const QList<EntityTexEntry> kMap = {
        { "nightwalker",      "enderman/enderman.png",   "entity_nightwalker" },      // t727 夜行者（末影人）
        { "nightwalker_eyes", "enderman/enderman_eyes.png", "entity_nightwalker_eyes" }, // t727 眼睛发光层
        { "emberling",        "blaze.png",               "entity_emberling" },        // t728 燃烬者（烈焰人）
        { "squid",            "squid.png",               "entity_squid" },            // t730 鱿鱼
        { "minecart",         "minecart.png",            "entity_minecart" },         // t732 矿车
        { "enchant_book",     "enchanting_table_book.png", "entity_enchant_book" },   // t732 附魔台悬浮书
        { "skin_default",     "steve.png",               "entity_skin_default" },     // t731 玩家默认皮肤
        { "skin_alex",        "alex.png",                "entity_skin_alex" },        // t731 皮肤变体
        // t749 剪毛羊本体层（mobType 3 shearedAt）：pack 命中 sheep/sheep.png（裸身 + 真脸 box-UV 布局，
        //   与 MobModel 羊 setMobTex 同布局 → packTextured 直采）；pack 关回退程序 mob_sheep_sheared.png
        //   （裸肤 + 残羊毛块全脸 UV，build_mob.py t749 新增）。区别于 mobEntityMap 的 sheep_fur.png 毛层
        //   （毛茸态用）——剪毛态要的是裸身观感，不与 3D 毛层路径共用映射。
        { "sheep_body",       "sheep/sheep.png",         "mob_sheep_sheared" },
    };
    return kMap;
}

// t717「盔甲 tier → pack models/armor 文件名前缀」表（t718/t719 盔甲 3D 显示 pack 覆盖前置）。
//   tier 序与 Hotbar::armorTier / playerModel.armorBaseColor 同源（0 皮革 / 1 铁 / 2 铜 / 3 金 / 4 钻石；
//   铜（t613 本工程自创档）无 pack 等价 → 不进本表，armorLayerSource miss 回退程序贴图（t718 已产程序
//   铜层 armor_copper_layer_*.png，不再需铁层染铜）。layer = 1（头盔+胸甲+护腿）/ 2（靴）。
//   皮革 pack 层是灰白可染色 base → armorLayerSource 皮革命中走 retintLeatherTemplate 同族染棕（t718 接）。
QString armorLayerPackName(int tier, int layer)
{
    const char *prefix = nullptr;
    switch (tier) {
    case 0: prefix = "leather"; break;    // 皮革棕
    case 1: prefix = "iron"; break;       // 浅灰
    case 3: prefix = "gold"; break;       // 金黄
    case 4: prefix = "diamond"; break;    // 钻石青
    case 5: prefix = "chainmail"; break;  // 链甲深灰（tier 5 = 本工程链甲档（若建）；miss 无害）
    default: return {};                   // tier 2 铜：无 pack 等价（1.17 前无铜甲）→ 空 → 回退程序层
    }
    if (layer != 1 && layer != 2)
        return {};
    return QStringLiteral("%1_layer_%2.png").arg(QString::fromLatin1(prefix)).arg(layer);
}

// t456「引擎方块 id → pack item/前贴图文件名候选」映射（功能性元数据，红线 §9 可随代码提交；贴图文件不进仓库）。
//   方块段 id（与 BlockRegistry::Id 同源；Core 不依赖 Game 故用字面量 + 注释钉死，同 itemFilenameMap 不引
//   toolregistry 之例）。blockItemIconSource 逐候选 itemDir→blockDir 探测，首个命中即返；全缺返空（Hotbar 回退
//   程序生成图标）。
// t493（R18s 复盘二轮）：LapisOre(93) 刻意不进本映射 —— 用户要它与其他矿石一致走程序绘制 3D 立方体图标
//   （第一轮映射到 lapis_ore.png 2D 平铺被否决：「创造背包里矿石都是方块的形式」）。
//   候选顺序 = 探测优先级：item 目录的 vanilla 风格 item/<name>.png 优先（多数包有），block 目录的 <name>.png 兜底。
// t537（R19.2 回退，2026-08-14）：恢复 CraftingTable(9) / Furnace(10) 映射 —— 用户否决 t518 的 3D 立方体图标
//   （「做得一坨」），要求换回 2D pack 图（t492 二轮的状态）。候选列表双兜底：item 目录的 <name>.png 优先（多数
//   包有；用户后续会提供 item/crafting_table.png / furnace.png 直接替代）、block 目录的 <name>_front.png 兜底
//   （demo 包 1.8.2.2 实测无 item/crafting_table.png 但有 block/crafting_table_front.png / furnace_front.png → 落到
//   该前贴图 = 用户要的 2D 平面 icon）。加回映射 → blockItemIconSource 命中返 pack 的 file:// URL →
//   Hotbar::iconSourceForBlock 优先返 pack 2D 图，pack 启用即覆盖 3D 立方体图标。
// t493 恢复：青金石矿不再映射 → 回落程序绘制 3D 立方体 icon（与其它矿石一致；第一轮误加，二轮复盘撤销）。
// t413 木梯（Ladder=62，cross 形）：pack 启用时 item 图标用 pack 的 ladder.png（2D 梯子图）覆盖程序
//   icon_ladder.png（cross 方块 iconFileForBlock 走程序 cross 图 → pack 未覆盖）；放下贴图 tile 78 已由
//   tileFilenameMap 覆盖（三轮实测）。pack 关闭 → 回退程序 icon_ladder.png。
const QList<QPair<int, QStringList>> &blockItemIconMap()
{
    static const QList<QPair<int, QStringList>> kMap = {
        // t676 工作台 / 熔炉移出本映射（t537 曾恢复 2D pack front 图、t676 撤出）：用户点名「front 方案太
        //   扁平，全部升 cube per-face」→ 程序生成 icon_<block>.png 已是满立方 dimetric（顶 + 右侧 + 前面
        //   三面独立贴图，tools/build_cube_icons.py --from-pack cube_front 方案）。保留本映射会在 pack 激活时
        //   持续用 2D front 图覆盖 → 3D 立方体图标永不显。删条目 → 命中落空 → Hotbar 回落程序 3D 图标
        //   （pack 关 / 开都显 3D，观感统一）。
        // t413 木梯（Ladder=62，cross 形）：pack 启用时 item 图标用 pack 的 ladder.png（2D 梯子图）覆盖程序 icon_ladder.png。
        { 62, { QStringLiteral("ladder.png") } }, // Ladder 木梯（pack item 图标覆盖）
        // t496 床 16 色变体（BedRed=32..BedBlack=39 既存 8 色 + BedWhite=78..BedBrown=85 t455 新增 8 色）。
        //   pack 仅有一张 item/bed.png（红床模板）→ 16 床色全映射到 bed.png；blockItemIconSource 命中床段时按目标
        //   床色重染模板（retintBedTemplate）落盘 voxelsandbox_rp_bed_<id>.png，返染色图路径 → 16 床色 item 图标
        //   各显各色（机制等价 MC 1.0 床 item icon 各色不同 / 各色羊毛染色；用户复盘「16 色床图标全是红床」修复）。
        //   pack 关闭时本映射候选落空 → 回退程序生成 icon_bed_<color>.png（hotbar.cpp，本身已是各色）。
        { 32, { QStringLiteral("bed.png") } }, // BedRed
        { 33, { QStringLiteral("bed.png") } }, // BedOrange
        { 34, { QStringLiteral("bed.png") } }, // BedYellow
        { 35, { QStringLiteral("bed.png") } }, // BedGreen
        { 36, { QStringLiteral("bed.png") } }, // BedCyan
        { 37, { QStringLiteral("bed.png") } }, // BedBlue
        { 38, { QStringLiteral("bed.png") } }, // BedMagenta
        { 39, { QStringLiteral("bed.png") } }, // BedBlack
        { 78, { QStringLiteral("bed.png") } }, // BedWhite
        { 79, { QStringLiteral("bed.png") } }, // BedLightBlue
        { 80, { QStringLiteral("bed.png") } }, // BedLime
        { 81, { QStringLiteral("bed.png") } }, // BedPink
        { 82, { QStringLiteral("bed.png") } }, // BedGray
        { 83, { QStringLiteral("bed.png") } }, // BedLightGray
        { 84, { QStringLiteral("bed.png") } }, // BedPurple
        { 85, { QStringLiteral("bed.png") } }, // BedBrown
        // t620 门 / 铁轨 pack 2D item 图标（t537 工作台/熔炉同模式：item 目录优先、block 目录兜底）。
        //   门：item/oak_door.png + item/spruce_door.png（demo 包 item 目录实测都在；MC 1.0 门 item 图标即
        //   2D 门板立绘）。铁轨：item 目录无 rail 图标（demo 包实测）→ block/rail_normal.png 兜底（直轨
        //   2D 图，机制等价 MC rail item icon）。
        //   t676 发射器(107) / 投掷器(117) 移出本映射（用户点名升 cube per-face 3D 图标；同上方工作台 /
        //   熔炉撤出理由 —— 保留 2D front 覆盖会让程序 3D 立方体图标永不显）。
        { 19,  { QStringLiteral("oak_door.png"),                       QStringLiteral("door_wood_lower.png") } },  // WoodDoor 木板门
        { 89,  { QStringLiteral("spruce_door.png"),                    QStringLiteral("door_spruce_lower.png") } }, // SpruceDoor 云杉门
        // t745 铁门补 2D pack 立绘（同木门/云杉门模式：item 目录优先、block 老命名 HD 兜底；此前铁门
        //   只能恒显 FROM_PACK 烘焙 3D 薄板，pack 关态无程序回退 —— 现纳入双态：pack 关走程序图集重渲）。
        { 135, { QStringLiteral("iron_door.png"),                      QStringLiteral("door_iron_lower.png") } }, // IronDoor 铁门
        { 103, { QStringLiteral("rail_normal.png") } },               // Rail 铁轨（block 兜底：item 目录无 rail 图标）
        // t638 铁轨家族 + 红石火把 pack 2D item 图标（item 目录无 rail 族图标（demo 包实测）→ block 直轨 2D
        //   图兜底；红石火把 item 目录同样无 → block/redstone_torch_on.png 兜底（常亮态 2D 火把立绘））。
        //   t703：动力 / 探测轨候选序反转——老名（rail_golden / rail_detector）是 128px HD、现代名是 16px
        //   缩略副本（tileFilenameMap 157-160 同因已改老名），图标同取 HD 版免糊。
        { 127, { QStringLiteral("rail_golden.png"),   QStringLiteral("powered_rail.png") } },   // GoldenRail 动力铁轨（t703 老名 HD 优先）
        { 128, { QStringLiteral("rail_detector.png"), QStringLiteral("detector_rail.png") } }, // DetectorRail 探测铁轨（t703 老名 HD 优先）
        { 129, { QStringLiteral("redstone_torch_on.png") } },                                  // RedstoneTorch 红石火把（常亮态 2D）
        // t746 叶（Leaves/SpruceLeaves）移出本 2D 立绘映射：t714 曾让叶走 pack 灰度原图 + retint 落盘
        //   voxelsandbox_rp_leaf_<id>.png（平面 2D 观感）。t746 叶图标 3D 化 —— pack 态改由
        //   blockAtlasIconSource(id,true) 从合成图集（pack 叶瓦片 × tileTint）运行期 dimetric 立方投影；
        //   pack 关走 isPackDerivedIconFamily → 程序图集重渲程序原生 3D（hotbar.cpp 四层回退链 ②③）。
    };
    return kMap;
}

// t416/t444/t714 MC「灰度可着色」瓦片 → 着色 tint 查表（单一权威）。这些 tile 的包内贴图本体是灰度（机制等价 MC
//   foliageColor/grassColor / lily pad fixed tint），loader 直接用包内灰度原色会渲染成苔石色 / 灰白（用户「睡莲
//   现灰」），故合成时乘上对应群系 tint。非着色瓦片（stone/dirt/...）原样，不受影响（返 nullptr）。
//   注：grass_side 不在此列——它 = dirt 基底 + 顶部绿 overlay（仅顶部绿条着色），整张乘绿会把下方泥土也染绿
//   （t422 修：改走 composeGrassSide 走 overlay 合成路径）。
//   - grass_top(0) / oak_leaves(9) / tall_grass(28)：plains 叶绿素 #5a8a3a（t416）。
//   - lily_pad(61)（t444）：沼泽水生绿 #4aa852。MC lily_pad.png 是灰度可着色贴图（demo pack 实测灰 133,133,133），
//     MC 用硬编码水生绿着色；本引擎无 BlockColors → 合成时固定乘本 tint（机制等价 MC lily pad fixed tint）。
//     不着色则 pack 睡莲渲染成灰白方块（用户「现灰」）。
//   - spruce_leaves(175)（t714）：MC 云杉叶固定深蓝绿（spruce foliage 不随群系变化，机制等价 MC spruce leaves
//     常量色 #489087 量级；demo pack spruce_leaves.png 实测灰 130,130,130 需 tint）。不着色则 pack 云杉冠灰白。
const int *tileTint(int tileIndex)
{
    static constexpr int kFoliage[3]  = {0x5a, 0x8a, 0x3a}; // plains 叶绿素 #5a8a3a
    static constexpr int kLily[3]     = {0x4a, 0xa8, 0x52}; // t444 睡莲沼泽水生绿 #4aa852
    static constexpr int kSpruce[3]   = {0x3a, 0x6e, 0x55}; // t714 云杉针叶深蓝绿 #3a6e55
    if (tileIndex == 0 || tileIndex == 9 || tileIndex == 28) return kFoliage; // grass_top / oak_leaves / tall_grass
    if (tileIndex == 61) return kLily;                                       // lily_pad
    if (tileIndex == 175) return kSpruce;                                    // spruce_leaves（t714）
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

// t496 二轮复盘 床 16 色染色表（单一权威，与 tools/build_bed.py BED_COLORS 同色板：羊毛↔床同色一致，
//   机制等价 MC 1.0 床 16 色变体）。返 nullptr = 非床段。作为 retintBedTemplate 的「目标色」——红床模板 bed.png
//   按本表重染出 16 色被面（红床行 (160,45,45) 仅是 16 色之一，无基准色语义，重染算法按 R 亮度调制而非通道比）。
struct BedTint { int r, g, b; };
const BedTint *bedTintForBlock(int blockId)
{
    // 顺序与 BlockRegistry 床段 id 对齐（BedRed=32..BedBlack=39 既存 8 色 + BedWhite=78..BedBrown=85 t455 新 8 色）。
    static const BedTint kTints[] = {
        {160,  45,  45}, // 32 BedRed
        {200,  95,  30}, // 33 BedOrange
        {190, 170,  40}, // 34 BedYellow
        { 60, 130,  50}, // 35 BedGreen
        { 55, 130, 140}, // 36 BedCyan
        { 55,  70, 165}, // 37 BedBlue
        {170,  70, 150}, // 38 BedMagenta
        { 38,  38,  44}, // 39 BedBlack
        {240, 240, 238}, // 78 BedWhite
        { 70, 150, 210}, // 79 BedLightBlue
        { 95, 175,  45}, // 80 BedLime
        {225, 145, 175}, // 81 BedPink
        { 70,  70,  80}, // 82 BedGray
        {155, 155, 160}, // 83 BedLightGray
        {130,  60, 165}, // 84 BedPurple
        {115,  75,  45}, // 85 BedBrown
    };
    if (blockId >= 32 && blockId <= 39) return &kTints[blockId - 32];
    if (blockId >= 78 && blockId <= 85) return &kTints[8 + (blockId - 78)];
    return nullptr;
}

// t496 二轮复盘 红床模板 bed.png 重染成目标床色。bed.png 被面以红为主色（实测被面像素 R 主导、G≈B≈0，
//   纯红调），按「目标/红基准通道比」缩放会因 G/B 通道为 0 而失败（蓝床 G/B 恒 0 → 仍红/黑，非蓝）。故改用
//   「红被面区域识别 + 亮度调制重染」：
//   - 红被面判据：不透明 + R > (G+B)*1.5 且 R > 40（demo 包实测此条件精确圈中被面，排开枕垫白 / 床头板木色 /
//     床沿暗灰）。非红区域（枕垫 / 木色 / 暗灰）原样保留 → 染色后被面色随目标色变、枕头仍白、床腿仍木色，
//     整张图标辨识度与红床模板一致。
//   - 亮度调制：被面像素的 R 强度（40..224）归一为 f = R/224（最亮红被面 = 1.0），略提 1.15 保高光 → 目标色 × f
//     → 被面随折边亮带 / 绗缝暗线保持明暗层次，仅色相迁移（红→蓝 / 绿 / 白 …）。机制等价 MC「同一床模板按染料
//     染色出 16 色」。
//   img 为 Format_ARGB32_Premultiplied；输出亦保持预乘。透明像素（alpha=0，边框外）不动。
void retintBedTemplate(QImage &img, int tgtR, int tgtG, int tgtB)
{
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = scan[x];
            const int a = qAlpha(c);
            if (a == 0) continue; // 透明像素不动（bed.png 边框外 alpha=0）
            // 反预乘取线性 RGB（保通用正确；demo 包不透明像素近似非预乘，反解值 = 原值）。
            const int r = qBound(0, qRed(c) * 255 / qMax(1, a), 255);
            const int g = qBound(0, qGreen(c) * 255 / qMax(1, a), 255);
            const int b = qBound(0, qBlue(c) * 255 / qMax(1, a), 255);
            // 非红被面（枕垫白 / 床头板木色 / 床沿暗灰）→ 原样保留（仅重写预乘，保 a 与线性 RGB 关系一致）。
            if (!(r > (g + b) * 3 / 2 && r > 40)) {
                continue; // 线性 RGB 与原像素一致 → 不改写（保留原预乘值，无失真）
            }
            // 红被面：按 R 强度亮度调制到目标色（保折边 / 绗缝明暗层次）。
            // f = min(1.0, R/224*1.15) 归一（被面最亮 ~224，提 1.15 保高光，饱和到 1.0 = 目标色满色）。
            // Q8.8 风格：f256 = min(256, R*256*1.15/224)；暗处 f→0 近黑目标色，亮处 f=1 满目标色。
            int f256 = (r * 256 * 23 / 20) / 224; // = r*256*1.15/224（值域 0..256+）
            if (f256 > 256) f256 = 256;           // 饱和到满目标色（min(1.0,...)）
            const int nr = qBound(0, (tgtR * f256) / 256, 255);
            const int ng = qBound(0, (tgtG * f256) / 256, 255);
            const int nb = qBound(0, (tgtB * f256) / 256, 255);
            scan[x] = qRgba((nr * a) / 255, (ng * a) / 255, (nb * a) / 255, a);
        }
    }
}

// R19 B1 皮革护甲 retint：pack 的 leather_helmet/chestplate/leggings/boots.png 是白底「可染色 base」
//   （MC 皮革染色机制 = 灰白 base 贴图 × 染料颜色；base 本身未叠皮革棕 overlay），图标直接用即显白底。
//   故把灰白底按亮度映射到皮革棕三色梯度：暗→#5e3d1c / 中→#8a5a2b / 亮→#a87340（与 MaterialIcon.qml
//   drawArmor 皮革 palettes[0] = ["#8a5a2b","#a87340","#5e3d1c"] 同色板），保留护甲折边/高光/阴影的明暗
//   层次，仅把灰白色相迁移到皮革棕。机制等价 MC「皮革 base + 默认皮革棕染料」（不同于床按 R 通道比染色，
//   皮革底是去色灰 → 用 luma 作映射键，单通道即可表征明暗）。
//   映射：luma L∈[0,255]，L<128 → dark..base（f=L/128）；L≥128 → base..light（f=(L-128)/127），线性插值。
//   透明像素（alpha=0）不动；img 为 Format_ARGB32_Premultiplied，输出亦保持预乘。
void retintLeatherTemplate(QImage &img)
{
    // 皮革棕三色锚点（与 MaterialIcon.qml palettes[0] leather 同源单一权威）。
    const int darkR = 0x5e, darkG = 0x3d, darkB = 0x1c; // #5e3d1c 暗（鞍桥/底阴影）
    const int midR  = 0x8a, midG  = 0x5a, midB  = 0x2b; // #8a5a2b 中（鞍体主色）
    const int liteR = 0xa8, liteG = 0x73, liteB = 0x40; // #a87340 亮（受光高光）
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = scan[x];
            const int a = qAlpha(c);
            if (a == 0) continue; // 透明像素不动（护甲轮廓外 alpha=0）
            // 反预乘取线性 RGB（保通用正确；demo 包不透明像素近似非预乘，反解值 = 原值）。
            const int r = qBound(0, qRed(c) * 255 / qMax(1, a), 255);
            const int g = qBound(0, qGreen(c) * 255 / qMax(1, a), 255);
            const int b = qBound(0, qBlue(c) * 255 / qMax(1, a), 255);
            // 亮度（Rec.601 luma）：灰白底的明暗即护甲 3D 层次载体（高光亮 / 折边暗）。
            const int l = (r * 299 + g * 587 + b * 114) / 1000; // 0..255
            int nr, ng, nb;
            if (l < 128) {
                // dark..base：Q8 分数 f = L*256/128（L=0→0, L=127→~254）；暗像素→皮革暗棕。
                const int f = (l * 256) / 128;
                nr = darkR + ((midR - darkR) * f) / 256;
                ng = darkG + ((midG - darkG) * f) / 256;
                nb = darkB + ((midB - darkB) * f) / 256;
            } else {
                // base..light：Q8 分数 f = (L-128)*256/127（L=128→0, L=255→~256）；亮像素→皮革亮棕。
                int f = ((l - 128) * 256) / 127;
                if (f > 256) f = 256; // 饱和到皮革亮棕（最亮高光）
                nr = midR + ((liteR - midR) * f) / 256;
                ng = midG + ((liteG - midG) * f) / 256;
                nb = midB + ((liteB - midB) * f) / 256;
            }
            scan[x] = qRgba((nr * a) / 255, (ng * a) / 255, (nb * a) / 255, a);
        }
    }
}

// t588 铜物品贴图 retint（同皮革 / 床 retint 机制）：pack（1.8.2 等 1.13 前老包）无 copper_* 物品贴图
//   （铜 1.17+ 才进 MC）→ 铜物品图标从铁对应贴图运行期染色成铜（用户「铜的物品没有贴图，还在用老贴图；
//   能不能用铁的染色成铜的，这样所有贴图就统一」）。铁工具贴图 = 灰白铁头 + 棕木柄两区域：
//   - 铁头（灰阶像素 |r-g|、|g-b| 均小）→ 亮度映射到铜橙三色梯度（luma 保持 → 工具头折边 / 高光 /
//     阴影的明暗层次保留，仅色相从铁灰迁到铜橙；锚点色与 ToolIcon tier 6 铜配色 / MaterialIcon 铜锭
//     同色板：#e8a088 亮 / #c87850 中 / #8a4818 暗）。
//   - 木柄（棕像素 r>g>b）→ 原样保留（机制等价 MC 工具柄恒木，铜工具柄也是木柄）。
//   - 铁锭（整张灰白）→ 全图映射到铜锭梯度（MaterialIcon drawCopperIngot 同色板）。
//   透明像素（alpha=0）不动；img 为 Format_ARGB32_Premultiplied，输出亦保持预乘。
//   判据阈值：铁头灰阶 |r-g|<14 && |g-b|<14（demo 包实测铁头像素精确命中、木柄棕像素排除）。
// t613 描边带压暗（用户「铜的工具都凸出来了一个像素」）：像素取证（铁镐 vs 金镐 alpha 蒙版逐像素比对
//   完全一致、染色不动 alpha）→「凸出 1px」非轮廓外扩，而是**贴图最外圈近黑描边被旧梯度染成中亮铜橙**：
//   铁头外圈描边像素 luma≈60（近黑；金镐同位描边 (54,54,32) 同样近黑——pack 的 item 贴图惯例 = 外圈
//   1px 近黑描边线），旧梯度映射到 #a75e32（luma≈110）→ 亮橙外圈对比度高，读作「工具本体胖一圈」。
//   修：luma < kCopperOutlineLuma 的暗像素走**描边带梯度**（#3a2212 近黑铜棕起、带顶衔接主梯度 luma=90
//   的映射值连续无台阶），描边读作「线」而非本体 → 与金/铁工具同观感。铁护甲外圈描边 luma 均值≈29-35
//   （更黑）同受此带保护（t613 铜护甲染铜复用本函数）。alpha 腐蚀 / 收缩 1px 不可行——蒙版本无半透明
//   像素（0<a<250 为 0），腐蚀只会切掉正确的像素，治不了亮橙描边。
void retintCopperTemplate(QImage &img)
{
    // 铜橙三色锚点（与 ToolIcon.qml tier 6 head/headDark/headLight 同源单一权威；锭暗锚取铜锭 edge 系）。
    const int darkR = 0x8a, darkG = 0x48, darkB = 0x18; // #8a4818 暗（铜锭 dark）
    const int midR  = 0xc8, midG  = 0x78, midB  = 0x50; // #c87850 中（铜工具头主色）
    const int liteR = 0xe8, liteG = 0xa0, liteB = 0x88; // #e8a088 亮（铜头受光高光）
    // t613 描边带：luma 阈值（铁镐外圈 mean≈70 / 铁护甲外圈 mean≈29-35 / 本体内部阴影多 ≥96 → 90 取界）
    //   与描边暗端锚 #3a2212（近黑铜棕——描边是线不是本体，不与铜橙本体争亮）。
    constexpr int kOutlineLuma = 90;
    const int outR = 0x3a, outG = 0x22, outB = 0x12; // 描边暗端锚 #3a2212
    // 带顶 = 主梯度在 luma=kOutlineLuma 处的映射值（预先算死，衔接连续无台阶）。
    const int topF = (kOutlineLuma * 256) / 128;
    const int topR = darkR + ((midR - darkR) * topF) / 256;
    const int topG = darkG + ((midG - darkG) * topF) / 256;
    const int topB = darkB + ((midB - darkB) * topF) / 256;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = scan[x];
            const int a = qAlpha(c);
            if (a == 0) continue; // 透明像素不动（图标轮廓外 alpha=0）
            // 反预乘取线性 RGB（保通用正确；demo 包不透明像素近似非预乘，反解值 = 原值）。
            const int r = qBound(0, qRed(c) * 255 / qMax(1, a), 255);
            const int g = qBound(0, qGreen(c) * 255 / qMax(1, a), 255);
            const int b = qBound(0, qBlue(c) * 255 / qMax(1, a), 255);
            // 铁头判据：灰阶（铁工具头 / 铁锭本体 / 铁护甲都是 r≈g≈b 的灰白）。棕木柄（r>g>b）不命中 → 原样保留。
            if (qAbs(r - g) >= 14 || qAbs(g - b) >= 14)
                continue;
            // 亮度（Rec.601 luma）→ 铜橙梯度：暗 → #8a4818、中 → #c87850、亮 → #e8a088（Q8 分数线性插值）。
            const int l = (r * 299 + g * 587 + b * 114) / 1000; // 0..255
            int nr, ng, nb;
            if (l < kOutlineLuma) {
                // t613 描边带：近黑描边像素 → 近黑铜棕线（0 → #3a2212、kOutlineLuma → 接主梯度），不进中亮铜橙。
                const int f = (l * 256) / kOutlineLuma;
                nr = outR + ((topR - outR) * f) / 256;
                ng = outG + ((topG - outG) * f) / 256;
                nb = outB + ((topB - outB) * f) / 256;
            } else if (l < 128) {
                const int f = (l * 256) / 128;
                nr = darkR + ((midR - darkR) * f) / 256;
                ng = darkG + ((midG - darkG) * f) / 256;
                nb = darkB + ((midB - darkB) * f) / 256;
            } else {
                int f = ((l - 128) * 256) / 127;
                if (f > 256) f = 256;
                nr = midR + ((liteR - midR) * f) / 256;
                ng = midG + ((liteG - midG) * f) / 256;
                nb = midB + ((liteB - midB) * f) / 256;
            }
            scan[x] = qRgba((nr * a) / 255, (ng * a) / 255, (nb * a) / 255, a);
        }
    }
}

// t645 生成式生物蛋染色表（单一权威）：spawnEggId →（主色 base / 副色 overlay）。与 playercontroller.cpp
//   生物蛋→mob 渲染色 + MaterialIcon.qml drawSpawnEgg 各 kind 主色同色板（猪粉 / 牛棕 / 羊白 / 蹒跚者绿 /
//   骸骨骨白 / 潜行者暗绿 / 蜘蛛黑红 / 鸡白红 / 鱿鱼蓝灰）。pack 无 pig_spawn_egg.png 等独立文件（demo 包
//   实测 9 蛋全 miss）→ 用两张两层模板（item/spawn_egg.png 灰度蛋形 + item/spawn_egg_overlay.png 斑点层）
//   各染一色后 SourceOver 合成（机制等价 MC 1.0 spawn egg「base 色 + spot 色」两层模型）。返 nullptr = 非
//   生物蛋段（生成式路径不介入）。
struct EggTint { int base[3]; int spot[3]; };
const EggTint *spawnEggTint(int itemId)
{
    static const EggTint kTints[] = {
        // 0x20F 猪：粉壳 + 深粉斑（drawSpawnEgg pig shell #f0a8b0）
        { { 0xf0, 0xa8, 0xb0 }, { 0xc8, 0x78, 0x88 } },
        // 0x210 牛：棕壳 + 白斑（牛皮纹 cow shell #5a4030 / 白花斑 #f0e8d8）
        { { 0x5a, 0x40, 0x30 }, { 0xf0, 0xe8, 0xd8 } },
        // 0x211 羊：奶白壳 + 灰卷绒斑（sheep shell #f5f0e8 / curl #c8c0b8）
        { { 0xf5, 0xf0, 0xe8 }, { 0xc8, 0xc0, 0xb8 } },
        // 0x212（占位非蛋——id 表按段索引，须保持与蛋 id 对齐：见下方判段，本行不参与）
        { { 0, 0, 0 }, { 0, 0, 0 } },
        // 0x213 蹒跚者：暗绿腐肉壳 + 棕褐斑（shambler shell #4a6a3a / rot #6a4a2a）
        { { 0x4a, 0x6a, 0x3a }, { 0x6a, 0x4a, 0x2a } },
        // 0x214 骸骨：灰白骨壳 + 暗骨斑（bones shell #d8d8d0 / rib #989890）
        { { 0xd8, 0xd8, 0xd0 }, { 0x98, 0x98, 0x90 } },
        // 0x215 潜行者：深绿壳 + 浅绿迷彩斑（stalker shell #3a5a3a / speckle #5a7a4a）
        { { 0x3a, 0x5a, 0x3a }, { 0x5a, 0x7a, 0x4a } },
        // 0x216 蜘蛛：近黑壳 + 红眼斑（spider shell #2a1a1a / eye #c81818）
        { { 0x2a, 0x1a, 0x1a }, { 0xc8, 0x18, 0x18 } },
    };
    if (itemId >= 0x20F && itemId <= 0x216 && itemId != 0x212)
        return &kTints[itemId - 0x20F];
    if (itemId == 0x22C) { // 鸡：白羽壳 + 红鸡冠斑（chicken shell #f5f0e4 / comb #c83030）
        static const EggTint kChicken = { { 0xf5, 0xf0, 0xe4 }, { 0xc8, 0x30, 0x30 } };
        return &kChicken;
    }
    if (itemId == 0x22E) { // 鱿鱼：深褐壳 + 暗触腕斑（squid shell #6a4a3a / dark #3a2a1a）
        static const EggTint kSquid = { { 0x6a, 0x4a, 0x3a }, { 0x3a, 0x2a, 0x1a } };
        return &kSquid;
    }
    return nullptr;
}

// t645 生成式生物蛋合成：spawn_egg.png（灰度蛋形 base）按亮度映射到主色梯度 + spawn_egg_overlay.png
//   （灰度斑点层）按亮度映射到副色梯度，SourceOver 叠加（机制等价 MC spawn egg 两层「base + spot」模型；
//   亮度映射保模板的明暗层次 / 高光 / 阴影，仅迁移色相 —— 同 retintLeatherTemplate 的 luma 键机制）。
//   两模板 alpha>0 处染色、透明处不动；输出 Format_ARGB32_Premultiplied。任一模板空尺寸不符 → false。
bool composeSpawnEgg(QImage &base, const QImage &overlayRaw, int br, int bg, int bb,
                     int sr, int sg, int sb)
{
    if (base.isNull() || overlayRaw.isNull() || base.size() != overlayRaw.size())
        return false;
    QImage overlay = overlayRaw.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // 亮度 → 颜色梯度映射（亮→满色、暗→1/2 色；保模板明暗层次）。Q8 分数 f = luma*2（0..255 → 0..510）。
    const auto mapLuma = [](int l, int c) -> int { return c * (128 + l / 2) / 256; };
    const int w = base.width(), h = base.height();
    for (int y = 0; y < h; ++y) {
        QRgb *bScan = reinterpret_cast<QRgb *>(base.scanLine(y));
        QRgb *oScan = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int ba = qAlpha(bScan[x]);
            if (ba != 0) {
                const int r = qBound(0, qRed(bScan[x]) * 255 / qMax(1, ba), 255);
                const int g = qBound(0, qGreen(bScan[x]) * 255 / qMax(1, ba), 255);
                const int b = qBound(0, qBlue(bScan[x]) * 255 / qMax(1, ba), 255);
                const int l = (r * 299 + g * 587 + b * 114) / 1000;
                bScan[x] = qRgba((mapLuma(l, br) * ba) / 255, (mapLuma(l, bg) * ba) / 255,
                                 (mapLuma(l, bb) * ba) / 255, ba);
            }
            const int oa = qAlpha(oScan[x]);
            if (oa != 0) {
                const int r = qBound(0, qRed(oScan[x]) * 255 / qMax(1, oa), 255);
                const int g = qBound(0, qGreen(oScan[x]) * 255 / qMax(1, oa), 255);
                const int b = qBound(0, qBlue(oScan[x]) * 255 / qMax(1, oa), 255);
                const int l = (r * 299 + g * 587 + b * 114) / 1000;
                oScan[x] = qRgba((mapLuma(l, sr) * oa) / 255, (mapLuma(l, sg) * oa) / 255,
                                 (mapLuma(l, sb) * oa) / 255, oa);
            }
        }
    }
    QPainter op(&base);
    op.drawImage(0, 0, overlay); // SourceOver：斑点叠在染好的蛋壳上，透明处保蛋壳色
    op.end();
    return true;
}

// t588/t613 铜物品「引擎物品 id → 铁对应 pack item 贴图文件名」回退表（copper_* 缺失时用 iron_* 染铜）。//   铜工具 0x118..0x11C 五件 + 铜锭 0x21D + 铜护甲 0x308..0x30B 四件（t613：用户「护甲里面的铜盔甲
//   还是没有更换，需要从铁套那边换个颜色弄成铜制的」——铁护甲贴图整张灰白（无木柄两区域问题），
//   retintCopperTemplate 全图染铜 + 描边带保外圈线，机制同铜工具）。铜原矿（0x21C）不进本表 ——
//   pack 无 raw_iron.png 可染，自绘 MaterialIcon drawCopperOre 本就是铜配色（石头底 + 橙铜斑 +
//   孔雀绿锈），无「老铁贴图」问题。
const char *copperIronFallback(int itemId)
{
    switch (itemId) {
    case 0x118: return "iron_pickaxe.png";     // 铜镐 ← 铁镐染铜
    case 0x119: return "iron_axe.png";         // 铜斧 ← 铁斧
    case 0x11A: return "iron_shovel.png";      // 铜铲 ← 铁铲
    case 0x11B: return "iron_sword.png";       // 铜剑 ← 铁剑
    case 0x11C: return "iron_hoe.png";         // 铜锄 ← 铁锄
    case 0x21D: return "iron_ingot.png";       // 铜锭 ← 铁锭
    case 0x308: return "iron_helmet.png";      // 铜头盔 ← 铁头盔（t613）
    case 0x309: return "iron_chestplate.png";  // 铜胸甲 ← 铁胸甲（t613）
    case 0x30A: return "iron_leggings.png";    // 铜护腿 ← 铁护腿（t613）
    case 0x30B: return "iron_boots.png";       // 铜靴子 ← 铁靴子（t613）
    default:    return nullptr;
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

// t620 裁掉贴图顶部空白带（MC 矮模型元素的侧贴图自带顶部空白——附魔台 0.25、末影祭坛 0.1875）。
//   本引擎 pushBox 整张 UV 无子区采样 → 裁掉顶部 blankFrac 比例的行、余下有效部分整张返回（调用方再缩
//   kTile×kTile）。仅当顶部确实存在「全透明行带」时才裁（防误裁无空白的自定义包：从顶向下找首个不透明
//   行，若其行号 < blankFrac*height 则从该行起裁到底；否则原样返回不裁）。源空 / 解码失败 → 空 QImage。
QImage cropTopBlank(const QImage &src, float blankFrac)
{
    if (src.isNull() || src.height() <= 0)
        return {};
    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int expect = qRound(float(img.height()) * blankFrac); // 预期空白行数
    // 从顶向下找首个含不透明像素的行（alpha > 0）。
    int firstOpaque = img.height();
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        bool opaque = false;
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(scan[x]) > 0) { opaque = true; break; }
        }
        if (opaque) { firstOpaque = y; break; }
    }
    if (firstOpaque >= img.height())
        return {}; // 全透明 → 无有效内容
    // 仅当实际空白带 ≈ 预期（允许 ±1/32 误差）才裁；自定义包顶行就有内容（firstOpaque=0）→ 不裁原样用。
    const int tol = qMax(1, img.height() / 32);
    const int cropY = (firstOpaque > 0 && qAbs(firstOpaque - expect) <= tol) ? firstOpaque : 0;
    if (cropY <= 0)
        return img; // 无空白带 → 原样
    return img.copy(0, cropY, img.width(), img.height() - cropY);
}

// t620 末影祭坛之眼 overlay 合成：endframe_top.png（框面基底）+ endframe_eye.png 叠加（SourceOver：眼图
//   alpha>0 处覆眼、透明处保框面）。MC eye 贴图是中央局部图（非整面）故必须叠基底。返回已缩放 kTile 的
//   ARGB32_Premultiplied；任一源缺 / 解码失败 → 空 QImage（调用方跳过，保程序生成眼瓦片）。
QImage overlayEyeOnTop(const QDir &blockDir)
{
    QImage top(blockDir.absoluteFilePath(QStringLiteral("endframe_top.png")));
    QImage eye(blockDir.absoluteFilePath(QStringLiteral("endframe_eye.png")));
    if (top.isNull() || eye.isNull())
        return {};
    top = top.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    eye = eye.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // 尺寸对齐（同包同分辨率，防御异尺寸包：眼缩放到顶同尺寸再叠）。
    if (eye.size() != top.size())
        eye = eye.scaled(top.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter op(&top);
    op.drawImage(0, 0, eye);
    op.end();
    if (top.size() != QSize(kTile, kTile))
        top = top.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return top;
}

// t620 水平镜像（u 翻转）：demo 包 rail_normal_turned.png 是右转（南进东出），程序贴图 136 基准是左转
//   （南进西出，mesher 四象限 UV 映射按此编码）→ 镜像后右转变左转，mesher 零改动。返回 ARGB32_Premultiplied；
//   源空 / 解码失败 → 空 QImage。
QImage mirrorHorizontally(const QImage &src)
{
    if (src.isNull())
        return {};
    QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return img.flipped(Qt::Horizontal); // Qt 6.11：mirrored() 已弃用，flipped 同语义
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
void paintColumnFrames(QImage &strip, int col, int /*cols*/, int framePx, int frameCount, const QList<QImage> &frames)
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
//   t724：落盘文件名改为显式参数 outName（旧版按 cols 推断——水 2 列 / 其余当岩浆，cols=1 的火条带会
//   覆盖岩浆落盘文件）。三条带（水 / 岩浆 / 火）各自独立命名。
QString buildFluidStrip(const QDir &blockDir, const QString &baseStripResource,
                        int cols, int frameCount, const QList<QPair<int, QString>> &sourceFiles,
                        const QString &outName)
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
    const QString path = QDir(dir).absoluteFilePath(outName);
    if (!strip.save(path, "PNG")) {
        qWarning("ResourcePack: 无法写入流体条带 %s；回退程序生成条带。", qPrintable(path));
        return {};
    }
    return path;
}

// t585 指南针/钟逐帧物品动画（机制等价 MC 1.0 compass/clock 每帧 item 贴图）：itemId（QML 字面量 0x23F/
//   0x240 与 recipe.h CompassId/ClockId 同源，Core 不 include Game 层，同 blockItemIconMap 字面量先例）→
//   帧文件 stem（compass_00.png .. compass_NN.png 环）。返空串 = 非动画物品。
QString animItemStem(int itemId)
{
    if (itemId == 0x23F) return QStringLiteral("compass");
    if (itemId == 0x240) return QStringLiteral("clock");
    return {};
}

// t585 探测 item 目录内 <stem>_NN.png 帧文件数（从 00 起逐个验存在，中断即止——帧序必须连续，缺号断环）。
//   demo 包实测：compass 32 帧（00..31）、clock 64 帧（00..63）；.mcmeta 为 {"animation":{}} = 均匀默认
//   帧序（无自定义 frames 数组 / frametime，逐帧等时长、按 index 线性环）。
int detectAnimFrameCount(const QString &itemDir, const QString &stem)
{
    int n = 0;
    while (QFile::exists(QDir(itemDir).absoluteFilePath(
            QStringLiteral("%1_%2.png").arg(stem).arg(n, 2, 10, QLatin1Char('0')))))
        ++n;
    return n;
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
    s.effectDir.clear(); // t715 reset 状态效果图标目录（仅当包合法时重填）
    s.paintingDir.clear(); // t717 reset 画作目录（仅当包合法时重填）
    s.armorDir.clear(); // t717 reset 盔甲 layer 目录（仅当包合法时重填）
    s.waterStripFile.clear(); // t489 reset 流体条带落盘路径（仅当包合法时重填）
    s.lavaStripFile.clear();
    s.fireStripFile.clear(); // t724 reset 火焰条带落盘路径（仅当包合法时重填）
    s.portalStripFile.clear(); // t725 reset 余烬门条带落盘路径（仅当包合法时重填）
    s.bedIconFiles.clear(); // t496 reset 床染色图标缓存（pack 切换 / 重解析 → 重染）
    s.leatherIconFiles.clear(); // R19 B1 reset 皮革护甲染色图标缓存（pack 切换 / 重解析 → 重染）
    s.copperIconFiles.clear();  // t588 reset 铜物品染色图标缓存（pack 切换 / 重解析 → 重染）
    s.blockIconFiles.clear();   // t745 reset 方块 item 图标运行期渲染缓存（pack 切换 / 重解析 → 重渲）
    s.mobHeadIconFiles.clear(); // t633 reset 生物头像裁剪缓存（pack 切换 / 重解析 → 重裁）
    s.spawnEggIconFiles.clear(); // t645 reset 生成式生物蛋图标缓存（pack 切换 / 重解析 → 重染）
    s.animItems.clear(); // t585 reset 动画帧序列态（pack 切换 / 重解析 → 重探测帧数）
    s.skinPackFiles.clear(); // t731 reset pack 皮肤裁切缓存（pack 切换 / 重解析 → 重裁）
    s.sheepWoolFaceFile.clear(); // t749 reset 羊合成贴图缓存（pack 切换 / 重解析 → 重合成）

    // 底图 = qrc 程序生成图集（零 MC 资产进 qrc）。即便无包，合成图集也 = 默认。
    QImage base(QStringLiteral(":/textures/atlas.png"));
    if (base.isNull()) {
        qWarning("ResourcePack: 无法加载默认图集 qrc:/textures/atlas.png；跳过包覆盖。");
        return;
    }
    base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s.atlas = base;
    const int tileCount = base.width() / kTile; // 从图集宽度推导瓦片数（避免硬编码魔数）。

    // t415 config 首次从 settings.json 加载；之后只信内存。t731 playerSkin 同批（仅 namespaced 字段，
    //   非包路径——不参与 enabled 门控，空/非法回退 default）。
    if (!s.configLoaded) {
        const Settings cfg = readSettings();
        s.enabled = cfg.enabled;
        s.packPath = cfg.packPath;
        s.playerSkin = (cfg.playerSkin == QLatin1String("alex")) ? cfg.playerSkin
                                                                : QStringLiteral("default");
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
    // t715 状态效果图标目录（assets/minecraft/textures/mob_effect；同 packPath 并列解析）。包内无 mob_effect
    //   目录时为空 → effectIconSource 恒返空串 → HUD 效果栏回退 qrc 程序自绘 icon_effect_*.png（不阻塞图集合成）。
    //   MC 1.0 无 mob_effect 目录（1.6 前状态无图标 / 1.9 才引入该目录），老包 miss 属预期常态。
    s.effectDir = resolveTexturesSubDir(packPath, QStringLiteral("mob_effect"));
    // t717 画作目录（assets/minecraft/textures/painting；同 packPath 并列解析）。包内无 painting 目录时为空
    //   → paintingSource 恒返空串 → t720 画作方块回退 qrc 程序贴图 default_painting_*.png（不阻塞图集合成）。
    s.paintingDir = resolvePaintingDir(packPath);
    // t717 盔甲 layer 目录（assets/minecraft/textures/models/armor；两层子树）。包内无该目录时为空 →
    //   armorLayerSource 恒返空串 → t718 盔甲 3D 回退程序层贴图 armor_*_layer_*.png（不阻塞图集合成）。
    s.armorDir = resolveArmorDir(packPath);
    // t585 指南针/钟逐帧动画：探测 item 目录帧文件数（compass_00.. / clock_00.. 连续环；demo 包实测 32/64）。
    //   无 item 目录 / 无帧文件 → count=0 → animatedItemFrameSource 返空 → itemIconSource 回落静态
    //   compass.png/clock.png（再缺则自绘）。探测在构建期一次完成（构建后帧文件不再增删）。
    for (int animId : { 0x23F, 0x240 }) {
        const QString stem = animItemStem(animId);
        BuiltState::AnimFrames af;
        af.stem = stem;
        // t612 修「钟动画反了」（用户「设时间 0 显示晚上、设 midnight 显示正午大白天」）：t585 的
        //   0.5 锚基于「clock_32 = 全昼 = 正午」的误读。逐帧像素取证（demo 包 clock_00..63）：表盘中
        //   心窗（昼夜符号旋转经过的窗口）clock_00 暖色像素最多（太阳居中 = 正午）、clock_32 蓝色像素
        //   最多（月亮居中 = 子夜）→ 帧号与 dayPhase 同向同零（dayPhase 0=正午 → 帧 0；0.5=子夜 → 帧
        //   N/2）。钟锚改 0.0；指南针锚保持 0.5（compass_16/32 = 红针尖正上 = 状态 0，t585 目测无误，
        //   用户仅报钟反 —— 两物品帧序零位各自独立，不能共用一个锚）。
        af.anchor01 = (animId == 0x240) ? 0.0 : 0.5;
        if (!s.itemDir.isEmpty())
            af.count = detectAnimFrameCount(s.itemDir, stem);
        if (af.count > 0)
            qInfo("ResourcePack: 物品动画帧序列 %s：%d 帧。", qPrintable(stem), af.count);
        s.animItems.insert(animId, af);
    }
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
        } else if (m.first == 110 || m.first == 140) {
            // t620 附魔台侧（110）/ 末影祭坛侧（140）走裁剪合成：MC 侧贴图顶部自带空白（附魔台 4/16、
            //   祭坛 3/16 —— 模型元素矮于整格、贴图按 16px 满格 UV 绘制故顶部留空）。本引擎 pushBox 是
            //   整张 UV 无子区采样 → 合成时裁掉顶部空白行、余下有效部分整张缩放 → 贴到矮盒侧面（附魔台
            //   0.75 / 祭坛满格拉伸）无缝且无黑边（opaque 段透明像素会显黑，裁剪是唯一正解）。源缺 / 全
            //   空 → 跳过（保程序生成瓦片）。
            tile = cropTopBlank(QImage(blockDir.absoluteFilePath(m.second)),
                                m.first == 110 ? 0.25 : 0.1875);
            if (tile.isNull())
                continue;
        } else if (m.first == 142) {
            // t620 末影祭坛之眼态（142）走 overlay 合成：MC endframe_eye.png 是**中央局部图**（demo 包实测
            //   仅中央 64×96/128 不透明，非整面贴图）→ 不能直接当顶面。合成 = endframe_top.png（框面基底）
            //   + endframe_eye.png SourceOver 叠加（眼图 alpha>0 处覆眼、透明处保框面）。任一缺 → 跳过。
            tile = overlayEyeOnTop(blockDir);
            if (tile.isNull())
                continue;
        } else if (m.first == 136) {
            // t620 铁轨拐角（136）走镜像合成：demo 包 rail_normal_turned.png 是**右转**（南 v=1 进 → 东 u=1
            //   出；边缘不透明带实测 bottom+right），而程序贴图 136 基准是**左转**（南进西出）且 mesher 的
            //   四象限 UV 映射按左转基准编码 → 水平镜像（u 翻转）后右转变左转，与程序贴图同向，mesher 零改动。
            tile = mirrorHorizontally(QImage(blockDir.absoluteFilePath(m.second)));
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
            // t610 修「雪傀儡南瓜头没脸」（pack 路径）：部分包（demo 包实测）pumpkin_face_off.png 是**侧面贴图的
            //   原样拷贝**（与 pumpkin_side.png 逐字节相同 —— 懒包复用文件），采样后 118 瓦片 == 117 侧面 →
            //   南瓜四面全是瓜棱、无刻面。检测到该退化态时回退候选链 carved_pumpkin.png（经典刻脸）→
            //   pumpkin_face_on.png（发光刻脸）—— 两者任一存在且与侧面不同即采用。无候选（全缺 / 全退化）→
            //   保原样跳过（回退程序生成 default_pumpkin_face.png，其自带刻面）。判据「与 side 逐像素相同」而非
            //   「文件哈希」：包可能重压缩（哈希变）但像素仍复用；且只在 tile 118（face）这一格做，无全局开销。
            if (m.first == 118) {
                // 退化判据：face_off 与 side 转同一格式（IgnoreAspectRatio 不缩放 —— 都按原像素比）后逐像素相同。
                //   QImage::operator== 要求同 size + 同 format 才逐像素比（不同 format 恒 false），双转 ARGB32_Premultiplied。
                const QImage sideRaw = QImage(blockDir.absoluteFilePath(QStringLiteral("pumpkin_side.png")));
                const QImage faceRaw = QImage(png);
                const QImage sideRef = sideRaw.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (!sideRaw.isNull() && !faceRaw.isNull()
                    && faceRaw.convertToFormat(QImage::Format_ARGB32_Premultiplied) == sideRef) {
                    const QString fallbacks[2] = {
                        QStringLiteral("carved_pumpkin.png"),
                        QStringLiteral("pumpkin_face_on.png"),
                    };
                    QImage faceTile;
                    for (const QString &fb : fallbacks) {
                        const QString fbPath = blockDir.absoluteFilePath(fb);
                        if (!QFile::exists(fbPath)) continue;
                        QImage cand(fbPath);
                        if (cand.isNull()) continue;
                        if (cand.convertToFormat(QImage::Format_ARGB32_Premultiplied) == sideRef) continue; // 同样退化 → 试下一个
                        faceTile = cand;
                        qInfo("ResourcePack: %s 是侧贴图拷贝（无刻脸），南瓜前面回退 %s。", qPrintable(m.second), qPrintable(fb));
                        break;
                    }
                    if (faceTile.isNull())
                        continue; // 无可用候选 → 保程序生成刻脸瓦片
                    tile = faceTile;
                }
            }
            // t716 ② 云杉门上半退化检测（demo 包实测 door_spruce_upper.png 是**无窗纯板**——懒包把上半也
            //   填成实心门板，与 MC 门上半必有格栅窗不符；现代命名 spruce_door_top.png 同病，无候选可回退）。
            //   oak_door（door_wood_upper 有真 4 孔窗）正常覆盖。判据：门上半瓦片（143/145）预乘后**无任何
            //   透明像素**（alpha<8）→ 退化 → 跳过覆盖（保程序生成 4 孔窗贴图；pack 高清换正确性，观感降级
            //   但门窗语义正确——机制等价 MC 门上半带窗）。同 t610 南瓜懒拷贝检测模式（判据 = 像素级退化态）。
            if (m.first == 143 || m.first == 145) {
                bool anyTransparent = false;
                const int tw = tile.width(), th = tile.height();
                for (int y = 0; y < th && !anyTransparent; ++y)
                    for (int x = 0; x < tw; ++x)
                        if (qAlpha(tile.pixel(x, y)) < 8) { anyTransparent = true; break; }
                if (!anyTransparent)
                    continue; // 纯板无窗（懒包退化）→ 不覆盖，保程序 4 孔窗
            }
        }
        // review H2：格式归一 + 缩放 kTile×kTile 后移到 if/else 之外 —— 此前仅直映射分支缩放，四条复合
        //   路径里 110/140（cropTopBlank）与 136（mirror）不缩放：HD 128px 包返回 128×96 / 128×128，
        //   drawImage 1:1 画入 16px 高图集 → 纵向被裁只显顶部 16 行、横向溢出覆写右侧 7 个瓦片位（附魔台侧 /
        //   铁砧三态 / 南瓜侧 / 铁轨拐角 / 红石矿 / 发射器前 / 祭坛三面 / 门上半 ≈14 格被污染）。现所有路径
        //   统一归一：转 ARGB32_Premultiplied + 非 kTile 尺寸缩放拉满 kTile²（IgnoreAspectRatio = 裁后有效
        //   部分整张拉伸，正是「贴到矮盒侧面」的原始意图；16px 包 16×12 裁剪结果也拉满 → 槽底无残留旧行）。
        //   已 kTile 的复合（142 overlay / grass_side 内部已缩放）二次归一幂等无害。tint 随之后移：applyTint
        //   假定预乘格式，须在 convert 之后（tint 仅 0/9/28/61 直映射瓦片，行为不变）。
        tile = tile.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (tile.size() != QSize(kTile, kTile))
            tile = tile.scaled(kTile, kTile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (const int *tint = tileTint(m.first))
            applyTint(tile, tint[0], tint[1], tint[2]); // t416/t444：灰度可着色瓦片乘群系 tint（叶/草顶/草丛 plains 绿；睡莲沼泽水生绿）
        // t716 ② 门窗 4 孔回归（橡木门只剩上 2 孔）根因修复：drawImage 默认 SourceOver 把瓦片**叠**在程序
        //   生成底图上 —— pack 瓦片的透明窗洞像素叠在底图上**露出底图门板/窗棂**而非真透明（机制：SourceOver
        //   透明源像素 = 保留目标）。实测：door_wood_upper 两窗带中带 1 恰落在底图程序窗洞 1 上（透出 → 显 2 孔）、
        //   带 2 落在底图门板区（被盖 → 不透）→ 橡木门上半只见 2 孔（用户复盘「4 孔只剩上 2 孔」）。修：
        //   合成模式改 **CompositionMode_Source**（替换语义：目标像素整体被源替换，含 alpha —— pack 瓦片
        //   透明处把底图一并擦成透明）。不透明瓦片（石头 / 木板 / …绝大多数）替换 == 叠加，零行为差；唯
        //   带 alpha 的瓦片（门窗 / cross）才显出差别 —— 而「pack 透明窗洞应真透」本就是正确语义（机制等价
        //   MC pack 门/草丛贴图 alpha 通道替换式应用）。
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(m.first * kTile, 0, tile);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver); // 复位（防影响后续非瓦片绘制）
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
    //   kLavaStripFrames 帧；火焰条带 1 列（fire_0）× kFireStripFrames 帧（t724）。构建失败（落盘目录不可写 /
    //   底图缺）→ 路径留空 → QML 回退 qrc 程序生成条带（仍动画，只是无包内高清帧）。包缺某源文件 → 该列
    //   保留程序生成底（不覆盖），条带仍 N 帧（动画不破）。落盘名显式传参（t724：旧 cols 推断会让火覆盖岩浆）。
    s.waterStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/water_strip.png"),
            2, BlockRegistry::kWaterStripFrames,
            { {0, QStringLiteral("water_still.png")}, {1, QStringLiteral("water_flow.png")} },
            QStringLiteral("voxelsandbox_water_strip.png"));
    s.lavaStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/lava_strip.png"),
            1, BlockRegistry::kLavaStripFrames,
            { {0, QStringLiteral("lava_still.png")} },
            QStringLiteral("voxelsandbox_lava_strip.png"));
    // t724 火焰条带：包内 fire_0.png（MC 动画贴图单列竖排 strip；demo 包实测 16×512 = 32 帧现成）→
    //   以 qrc 程序生成火焰条带为底、包内帧覆盖 → 落盘 voxelsandbox_fire_strip.png。
    s.fireStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/fire_strip.png"),
            1, BlockRegistry::kFireStripFrames,
            { {0, QStringLiteral("fire_0.png")} },
            QStringLiteral("voxelsandbox_fire_strip.png"));
    // t725 余烬门条带：包内 nether_portal.png（MC 动画贴图单列竖排 strip；demo 包实测 16×512 = 32 帧现成）→
    //   以 qrc 程序生成余烬门条带为底、包内帧覆盖 → 落盘 voxelsandbox_portal_strip.png。
    s.portalStripFile = buildFluidStrip(
            blockDir, QStringLiteral(":/textures/portal_strip.png"),
            1, BlockRegistry::kNetherPortalStripFrames,
            { {0, QStringLiteral("nether_portal.png")} },
            QStringLiteral("voxelsandbox_portal_strip.png"));

    // review D3-b 图鉴生物头像预生成（t633 修 GUI 卡顿）：mobHeadIconSource 此前把「PNG 解码 + 裁剪 +
    //   落盘」留在 QML 绑定求值里（ResourceBrowser 生物格 delegate 的 headSrc 属性 —— GUI 线程同步磁盘
    //   IO，首次开图鉴时 N 个 mob 头像逐个裁剪 = 可感知卡帧）。构建期（apply / 首查询的 ensureBuiltLocked，
    //   本就在做整图集合成 + 流体条带落盘的重 IO 段）一次遍历 mobHeadRegions 预生成全部头像 → 查询侧
    //   恒 O(1) 缓存命中（QFile::exists stat，同床 / 皮革 / 铜染色图标模式）。包缺源贴图 → 该条目跳过
    //   （查询侧回退体色方块，降级语义不变）。
    if (!s.entityDir.isEmpty()) {
        for (const MobHeadRegion &r : mobHeadRegions()) {
            const QString out = generateMobHeadIconFile(r, s.entityDir);
            if (!out.isEmpty())
                s.mobHeadIconFiles.insert(r.mobType, out);
        }
    }
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
    QString curPath, curSkin;
    {
        QMutexLocker lock(&stateMutex());
        BuiltState &s = state();
        s.enabled = e;
        s.configLoaded = true;
        curPath = s.packPath;
        curSkin = s.playerSkin;
    }
    writeSettings(e, curPath, curSkin); // 持久化（文件 IO 在锁外，少占 image provider）
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
    QString curSkin;
    {
        QMutexLocker lock(&stateMutex());
        BuiltState &s = state();
        s.packPath = p;
        s.configLoaded = true;
        curEnabled = s.enabled;
        curSkin = s.playerSkin;
    }
    writeSettings(curEnabled, p, curSkin);
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

// t724 火焰条带贴图源（同 water/lava 模式）：fireHost delegate 的两片交叉 quad 共享此 Texture 做
//   scaleV/positionV 翻书。active 且落盘成功 → file:///；否则 qrc 程序生成条带。
QString ResourcePackManager::fireStripSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/fire_strip.png");
    QMutexLocker lock(&stateMutex());
    const QString &f = state().fireStripFile;
    return f.isEmpty() ? QStringLiteral("qrc:/textures/fire_strip.png")
                       : QStringLiteral("file:///") + f;
}

// t725 余烬门条带贴图源（同 fire 模式）：portalHost delegate 的竖直平面 quad 共享此 Texture 做
//   scaleV/positionV 翻书。active 且落盘成功 → file:///；否则 qrc 程序生成条带。
QString ResourcePackManager::portalStripSource() const
{
    if (!m_active)
        return QStringLiteral("qrc:/textures/portal_strip.png");
    QMutexLocker lock(&stateMutex());
    const QString &f = state().portalStripFile;
    return f.isEmpty() ? QStringLiteral("qrc:/textures/portal_strip.png")
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
    // t645 itemDir→blockDir 双探测（参照 blockItemIconSource 既有双探测机制）：部分映射（glass 0x204 /
    //   white_wool 0x20E / oak_sapling 0x21B）的目标文件在包内 **block/** 目录（demo 包把方块类物品放
    //   block/），旧版只探测 itemDir → 恒 miss。item 目录优先（vanilla item icon 布局），block 目录兜底。
    //   不拷贝 PNG（pack 只读）；block/ 残留副本（如 oak_sapling (2).png）不触碰。
    QString path;
    const QString itemPath = QDir(s.itemDir).absoluteFilePath(filename);
    if (QFile::exists(itemPath)) {
        path = itemPath;
    } else if (!s.blockDir.isEmpty()) {
        const QString blockPath = QDir(s.blockDir).absoluteFilePath(filename);
        if (QFile::exists(blockPath))
            path = blockPath;
    }
    if (path.isEmpty()) {
        // t645 生成式生物蛋（在铜回退之前 —— 蛋 id 不在 copperIronFallback 表，插此处语义同层：映射目标
        //   pack 文件 miss 时的「模板派生」回退）：pack 无 pig_spawn_egg.png 等独立文件（demo 包 9 蛋全
        //   miss）→ 用两张两层模板合成（item/spawn_egg.png 灰度蛋形染 mob 主色 + spawn_egg_overlay.png
        //   斑点层染副色 → SourceOver）落盘 voxelsandbox_rp_egg_<id>.png，返 file:// 路径（机制等价 MC
        //   spawn egg base+spot 两层配色；retintCopperTemplate 同「运行期派生缓存」管线，不进 qrc/VCS）。
        //   模板任一缺 / 尺寸不符 / 解码 / 落盘失败 → 空串（回退 MaterialIcon drawSpawnEgg 自绘，现状不变）。
        if (const EggTint *tint = spawnEggTint(itemId)) {
            const auto cached = s.spawnEggIconFiles.constFind(itemId);
            if (cached != s.spawnEggIconFiles.constEnd() && QFile::exists(cached.value()))
                return QStringLiteral("file:///") + cached.value();
            QImage base(s.itemDir + QStringLiteral("/spawn_egg.png"));
            QImage overlay(s.itemDir + QStringLiteral("/spawn_egg_overlay.png"));
            if (base.isNull() || overlay.isNull())
                return {}; // 模板缺（旧包）→ 回退自绘
            base = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (!composeSpawnEgg(base, overlay, tint->base[0], tint->base[1], tint->base[2],
                                 tint->spot[0], tint->spot[1], tint->spot[2]))
                return {}; // 尺寸不符等 → 回退自绘
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
            if (dir.isEmpty())
                return {}; // 无可写目录 → 回退自绘（降级）
            QDir().mkpath(dir);
            const QString out = QDir(dir).absoluteFilePath(
                    QStringLiteral("voxelsandbox_rp_egg_%1.png").arg(itemId, 0, 16));
            if (!base.save(out, "PNG"))
                return {}; // 落盘失败 → 回退自绘（降级）
            state().spawnEggIconFiles.insert(itemId, out); // stateMutex 已持锁，安全
            return QStringLiteral("file:///") + out;
        }
        // t588/t613 铜物品回退：映射的 copper_* 不存在（1.8 等老包无铜）→ 用铁对应贴图染铜（同皮革 / 床
        //   retint 机制；t613 起含铜护甲四件 + 描边带压暗）。首次命中：加载 iron_* → retintCopperTemplate
        //   （铁头灰阶→铜橙梯度、木柄保留、近黑描边保暗）→ 落盘 voxelsandbox_rp_copper_<id>.png → 记缓存；
        //   后续 O(1) 命中缓存直接返。铁贴图也缺 / 解码 / 落盘失败 → 空串（回退自绘，现状不变）。
        //   红线 §9：仅运行期读本地 pack PNG，不进 qrc/VCS。
        const char *ironName = copperIronFallback(itemId);
        if (!ironName)
            return {}; // 包内无该 item 贴图 → 不覆盖（保留自绘 Canvas）；红线 §9：仅运行期读本地 pack PNG。
        const auto cached = s.copperIconFiles.constFind(itemId);
        if (cached != s.copperIconFiles.constEnd() && QFile::exists(cached.value()))
            return QStringLiteral("file:///") + cached.value();
        const QString ironPath = QDir(s.itemDir).absoluteFilePath(QString::fromLatin1(ironName));
        if (!QFile::exists(ironPath))
            return {}; // 铁贴图也缺（极端老包）→ 回退自绘
        QImage iron(ironPath);
        if (iron.isNull())
            return {}; // 解码失败 → 回退自绘
        iron = iron.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        retintCopperTemplate(iron);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dir.isEmpty())
            return {}; // 无可写目录 → 回退自绘（降级）
        QDir().mkpath(dir);
        const QString out = QDir(dir).absoluteFilePath(
                QStringLiteral("voxelsandbox_rp_copper_%1.png").arg(itemId, 0, 16));
        if (!iron.save(out, "PNG"))
            return {}; // 落盘失败 → 回退自绘（降级）
        state().copperIconFiles.insert(itemId, out); // stateMutex 已持锁，安全
        return QStringLiteral("file:///") + out;
    }

    // R19 B1 皮革护甲 retint（同床 retint 机制）：pack 的 leather_helmet/chestplate/leggings/boots.png
    //   （0x300..0x303，皮革 tier 四件）是白底可染色 base（MC 皮革染色 = 灰白 base × 染料；未叠皮革棕 overlay）
    //   → 直接用即显白底。命中皮革 tier 时重染成皮革棕梯度（retintLeatherTemplate，与 MaterialIcon drawArmor
    //   皮革 palettes[0] 同色板）落盘 voxelsandbox_rp_leather_<id>.png，返染色图 file:// 路径 → 皮革护甲图标显皮革棕。
    //   命中缓存直接返（首次染色后落盘，后续 O(1)）。非皮革 tier（铁/金/钻石/铜）原样返 pack 图，不 retint。
    if (itemId >= 0x300 && itemId <= 0x303) {
        // 命中缓存（pack 未重解析期间稳定）→ 直接返。
        const auto it = s.leatherIconFiles.constFind(itemId);
        if (it != s.leatherIconFiles.constEnd() && QFile::exists(it.value()))
            return QStringLiteral("file:///") + it.value();
        // 加载皮革 base 贴图并重染成皮革棕。解码失败 → 回退返未染色的白底 base（可接受降级，仍能辨识护甲形状）。
        QImage leather(path);
        if (leather.isNull())
            return QStringLiteral("file:///") + path;
        leather = leather.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        retintLeatherTemplate(leather);
        // 落盘到 AppLocalDataLocation（与 atlasFile 同目录，ensureBuiltLocked 已 mkpath；此处再保底）。
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dir.isEmpty())
            return QStringLiteral("file:///") + path; // 无可写目录 → 回退白底（不染色，降级）
        QDir().mkpath(dir);
        const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_leather_%1.png").arg(itemId));
        if (!leather.save(out, "PNG"))
            return QStringLiteral("file:///") + path; // 落盘失败 → 回退白底（降级）
        // 记缓存（mutable：s 是 state() 引用但 leatherIconFiles 需写；stateMutex 已持锁，安全）。
        state().leatherIconFiles.insert(itemId, out);
        return QStringLiteral("file:///") + out;
    }

    return QStringLiteral("file:///") + path;
}

// t497 生存背包空护甲槽图标源（pack 内 empty_armor_slot_<piece>.png）。piece = ArmorRegistry::ArmorPiece
//   序（0 头盔 / 1 胸甲 / 2 护腿 / 3 靴子，与 SurvivalInventory 装备槽 Repeater index 同源）；越界 → 空串。
//   active=false / 无 item 目录 / 包内缺该 PNG → 空串（SurvivalInventory 回退自绘 Canvas 暗灰剪影，§9a）。
QString ResourcePackManager::emptyArmorSlotSource(int armorPiece) const
{
    // 部位 → 标准 MC item 文件名（empty_armor_slot_helmet.png 等，现网多数包此 4 文件在 item/ 下）。
    //   empty_armor_slot_shield.png（副手槽）本工程无副手槽故不映射；仅 4 护甲部位。
    const char *filename = nullptr;
    switch (armorPiece) {
    case 0: filename = "empty_armor_slot_helmet.png";     break;     // Helmet
    case 1: filename = "empty_armor_slot_chestplate.png"; break;     // Chestplate
    case 2: filename = "empty_armor_slot_leggings.png";   break;     // Leggings
    case 3: filename = "empty_armor_slot_boots.png";      break;     // Boots
    default: return {};
    }
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.itemDir.isEmpty())
        return {};
    const QString path = QDir(s.itemDir).absoluteFilePath(QString::fromLatin1(filename));
    if (!QFile::exists(path))
        return {};
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
    QString foundPath;
    for (const QString &name : candidates) {
        if (!s.itemDir.isEmpty()) {
            const QString p = QDir(s.itemDir).absoluteFilePath(name);
            if (QFile::exists(p)) { foundPath = p; break; }
        }
        if (!s.blockDir.isEmpty()) {
            const QString p = QDir(s.blockDir).absoluteFilePath(name);
            if (QFile::exists(p)) { foundPath = p; break; }
        }
    }
    if (foundPath.isEmpty())
        return {}; // 包内无该方块 item / 前贴图 → 不覆盖（保留程序生成 icon_<block>.png）。

    // t496 二轮复盘 床 16 色区分：pack 只有红床模板 bed.png（blockItemIconMap 把 16 床色全映射到 bed.png）。
    //   用户复盘「16 色床图标全是红床」。修：bed 段命中时，按目标床色重染模板（retintBedTemplate）落盘
    //   voxelsandbox_rp_bed_<id>.png，返该染色图 file:// 路径 → 16 床色 item 图标各显各色（机制等价 MC 1.0
    //   床 item icon 各色不同 / 各色羊毛染色）。命中缓存直接返（首次染色后落盘，后续 O(1)）。非床段直接返
    //   foundPath（工作台 / 熔炉等原样用 pack 2D 图标，不染色）。bedIconFiles 随 atlasFile 同目录，已 mkpath。
    if (const BedTint *tint = bedTintForBlock(blockId)) {
        // 命中缓存（pack 未重解析期间稳定）→ 直接返。
        const auto it = s.bedIconFiles.constFind(blockId);
        if (it != s.bedIconFiles.constEnd() && QFile::exists(it.value()))
            return QStringLiteral("file:///") + it.value();
        // 加载红床模板 bed.png 并按目标色重染。解码失败 → 回退返未染色的 foundPath（红床，可接受降级）。
        QImage bed(foundPath);
        if (bed.isNull())
            return QStringLiteral("file:///") + foundPath;
        bed = bed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        retintBedTemplate(bed, tint->r, tint->g, tint->b);
        // 落盘到 AppLocalDataLocation（与 atlasFile 同目录，ensureBuiltLocked 已 mkpath；此处再保底）。
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (dir.isEmpty())
            return QStringLiteral("file:///") + foundPath; // 无可写目录 → 回退红床（不染色，降级）
        QDir().mkpath(dir);
        const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_bed_%1.png").arg(blockId));
        if (!bed.save(out, "PNG"))
            return QStringLiteral("file:///") + foundPath; // 落盘失败 → 回退红床（降级）
        // 记缓存（mutable：s 是 state() 引用但 bedIconFiles 需写；stateMutex 已持锁，安全）。
        state().bedIconFiles.insert(blockId, out);
        return QStringLiteral("file:///") + out;
    }

    // t746 叶段整块删除：叶（Leaves/SpruceLeaves）已移出 blockItemIconMap → 本函数对叶恒返空串，
    //   调用方（Hotbar::iconSourceForBlock）落到 t745 回退链 ②/③ 的 blockAtlasIconSource 3D 立方投影。

    return QStringLiteral("file:///") + foundPath;
}

// ───────────────────────── t745 方块 item 图标运行期 pack 渲染（统一贴图原则总纲机制） ─────────────────────────
// 机制：从**当前合成图集**运行期渲染方块 item 图标（dimetric 立方 / partial 子盒投影 / flat 2D）。
//   合成图集 = 程序图集 + 任意启用 pack 的贴图覆盖 + tint / 特判合成（ensureBuiltLocked 单一产物）→ 本
//   渲染器天然「任意 pack 通用」（不依赖本地参考包），pack 关闭时图集退纯程序瓦片 → 渲染结果即程序原生。
//   产物落盘 AppLocalData 缓存（playerSkinSource / 皮革染色落盘模式），文件名带 apply() revision →
//   pack 切换后 URL 变化驱动 QML Image 重载。
// 红线 §9：图标是运行期派生缓存（pack PNG + 程序图集 → 渲染产物），**不进 qrc / VCS**（与 t644 提交的
//   FROM_PACK 派生图标不同——那批是「离线 3D 渲染派生图入 VCS」先例；本机制运行期生成，盘上文件即缓存）。
namespace {

// t745 dimetric 投影常量（128×128 画布）。X 轴 → 屏幕 (+a,+b)（右下）/ Z 轴 → (-a,+b)（左下）/ Y 轴 →
//   (0,-2b)（上）；a:b = 2:1 等距。观察者在 (+X,+Y,+Z) 方向 → 可见面 = +Y 顶面（全亮）/ +Z 左前面（0.80）/
//   +X 右面（0.62）—— 与既有 icon_*.png 顶亮 / 左中 / 右暗的观感约定一致。
constexpr qreal kIconSize = 128.0;
constexpr qreal kIsoA = 58.0;
constexpr qreal kIsoB = 29.0;
constexpr qreal kIsoCx = 64.0;
constexpr qreal kIsoCy = 64.0; // 顶点 (0,1,0) 投影到 y = 64 - 58 = 6；底前角 (1,0,1) 到 y = 64 + 58 = 122

QPointF isoProject(qreal x, qreal y, qreal z)
{
    return QPointF(kIsoCx + kIsoA * (x - z), kIsoCy + kIsoB * (x + z) - 2.0 * kIsoB * y);
}

// 图标子盒：方块内轴对齐盒（cell-local [0,1]³）+ 三个可见面各用的图集瓦片。
struct AtlasIconBox {
    qreal x0, y0, z0, x1, y1, z1;
    int topTile, sideTile, frontTile;
};

// 图标规格：flat（cross / 贴地薄片立绘，瓦片原样放大保留 alpha）或 boxes（dimetric 子盒投影）。
//   t746 solidify：瓦片实心化开关（叶族专用）——瓦片含 alpha<255 孔/半透像素时，投影前用不透明像素
//   平均色填孔并强制不透明（对齐离线 build_cube_icons.py load_face「保证实心立方图标」约定）。
struct AtlasIconSpec {
    bool valid = false;
    bool flat = false;
    bool solidify = false; // t746 叶族 cutout 瓦片 → 实心立方图标（铁活板门 t742 刻意保孔不在此列）
    // t764 ① 附魔台专用：盒投影画完后叠画「悬浮敞开书」两页 V 形（drawEnchantBookOverlay；贴图两态
    //   pack/程序与放置态 bookDelegate 同源）。图标与放置观感对齐（用户「item 图标缺悬浮书」）。
    bool bookOverlay = false;
    int flatTile = -1;
    QList<AtlasIconBox> boxes; // 已按「远 → 近」深度序（painter 直接依序绘制）
};

// 方块 id → 图标形状规格。瓦片一律取 BlockRegistry::def 单一权威（topTile/sideTile/frontTile/bottomTile），
//   不另持映射副本；几何按 def.shape 泛化（整立方 / 台阶 / 楼梯 / 栅栏 / 压力板 / 门 / 活板门 / 积雪层）+
//   少数放置态特型方块（仙人掌细柱 / 附魔台矮盒 / 祭坛框 / 铁砧三段）显式覆盖。床（ShapeBed）与
//   RedstoneDust 不进（床走 blockItemIconMap 2D pack 床图 / 手绘；红石粉瓦片未映射 pack 且已有手绘 flat 图）。
AtlasIconSpec atlasIconSpecForBlock(int blockId)
{
    AtlasIconSpec spec;
    if (blockId <= 0 || blockId >= int(BlockRegistry::Count))
        return spec;
    const BlockRegistry::BlockDef &d = BlockRegistry::def(quint8(blockId));

    // ① flat 2D：cross 族 / 贴地薄片 / 火把立绘（icon 观感 = 世界内 cross 立绘）。cross 族 def 六面同
    //    瓦片 → topTile 即立绘；小麦作物 / 浆果丛 def 存阶段 0 基底瓦片 → 显成熟阶段瓦片（金黄麦穗 /
    //    成熟红浆果一眼可辨；瓦片号与 tileFilenameMap 36/105 同源）。
    // t746 叶族走 ③ ShapeFull 满立方盒 + solidify 实心化（瓦片带孔直投会显半透筛子，读不出体积）。
    if (blockId == BlockRegistry::Leaves || blockId == BlockRegistry::SpruceLeaves)
        spec.solidify = true;
    const auto flatSpec = [&spec](int tile) {
        spec.valid = true;
        spec.flat = true;
        spec.flatTile = tile;
    };
    switch (blockId) {
    case BlockRegistry::TallGrass:
    case BlockRegistry::DeadBush:
    case BlockRegistry::Sapling:
    case BlockRegistry::Cobweb:
    case BlockRegistry::LilyPad:
    case BlockRegistry::Mushroom:
    case BlockRegistry::BrownMushroom:
    case BlockRegistry::FlowerRed:
    case BlockRegistry::FlowerYellow:
    case BlockRegistry::FlowerBlue:
    case BlockRegistry::FlowerWhite:
    case BlockRegistry::Sugarcane:
    case BlockRegistry::Torch:
    case BlockRegistry::Lever:
    case BlockRegistry::RedstoneTorch:
    case BlockRegistry::Rail:
    case BlockRegistry::GoldenRail:
    case BlockRegistry::DetectorRail:
        flatSpec(d.topTile);
        return spec;
    case BlockRegistry::WheatCrop:
        flatSpec(36); // wheat_stage_7 成熟阶段瓦片（tileFilenameMap 36 同源）
        return spec;
    case BlockRegistry::SweetBerryBush:
        flatSpec(105); // sweet_berry_bush_stage2 成熟阶段瓦片（103..105 = 阶段 0..2）
        return spec;
    default:
        break;
    }

    // ② 盒模式特型覆盖（放置态异形几何，PartialBlockGeometry 同款形状）。
    const auto addBox = [&spec](qreal x0, qreal y0, qreal z0, qreal x1, qreal y1, qreal z1,
                                int top, int side, int front) {
        spec.boxes.append(AtlasIconBox{x0, y0, z0, x1, y1, z1, top, side, front});
    };
    const int topT = d.topTile, sideT = d.sideTile, frontT = d.frontTile;
    switch (blockId) {
    case BlockRegistry::Cactus: // 放置态 14/16 细柱（PartialBlockGeometry Cactus case；顶面 topTile 余 side）
        addBox(0.0625, 0.0, 0.0625, 0.9375, 1.0, 0.9375, topT, sideT, sideT);
        break;
    case BlockRegistry::EnchantingTable: // 0.75 矮盒（侧瓦片已由图集合成裁顶部 0.25 空白）
        addBox(0.0, 0.0, 0.0, 1.0, 0.75, 1.0, topT, sideT, sideT);
        spec.bookOverlay = true; // t764 ① 盒顶叠画悬浮敞开书（见 drawEnchantBookOverlay）
        break;
    case BlockRegistry::EndPortal: // 祭坛框 13/16 高（endframe 化；顶瓦片含未放眼态合成）
        addBox(0.0, 0.0, 0.0, 1.0, 0.8125, 1.0, topT, sideT, sideT);
        break;
    case BlockRegistry::Anvil: // 铁砧三段：底座 + 束腰 + 砧面台（顶瓦片 = 各阶段砧面 anvil_top*）
    case BlockRegistry::AnvilChipped:
    case BlockRegistry::AnvilDamaged:
        addBox(0.125, 0.0, 0.3125, 0.875, 0.25, 0.6875, sideT, sideT, sideT);  // 底座
        addBox(0.375, 0.25, 0.4375, 0.625, 0.625, 0.5625, sideT, sideT, sideT); // 束腰
        addBox(0.1875, 0.625, 0.25, 0.8125, 0.8125, 0.75, topT, sideT, sideT);  // 砧面台（顶面 anvil_top）
        break;
    default:
        break;
    }

    // ③ def.shape 泛化（特型未命中时）。子盒坐标与 collisionAABBs / PartialBlockGeometry 的形状语义一致。
    if (spec.boxes.isEmpty()) {
        switch (d.shape) {
        case BlockRegistry::ShapeFull:
            addBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapeSlab:
            addBox(0.0, 0.0, 0.0, 1.0, 0.5, 1.0, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapeStairs: // 整步（低，全 footprint）+ 背墙（远端半高 z[0,0.5] 上半）
            addBox(0.0, 0.0, 0.0, 1.0, 0.5, 1.0, topT, sideT, frontT);
            addBox(0.0, 0.5, 0.0, 1.0, 1.0, 0.5, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapeFence: // 立柱 + 上下横档（build_cube_icons.py _partial_shape_boxes 同款）
            addBox(6.0 / 16.0, 0.0, 6.0 / 16.0, 10.0 / 16.0, 1.0, 10.0 / 16.0, topT, sideT, frontT);
            addBox(0.0, 12.0 / 16.0, 6.0 / 16.0, 1.0, 15.0 / 16.0, 10.0 / 16.0, topT, sideT, frontT);
            addBox(0.0, 5.0 / 16.0, 6.0 / 16.0, 1.0, 8.0 / 16.0, 10.0 / 16.0, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapePlate:
            addBox(1.0 / 16.0, 0.0, 1.0 / 16.0, 15.0 / 16.0, 1.0 / 16.0, 15.0 / 16.0, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapeDoor: // 两格高 3/16 薄板：下半贴 bottomTile（lower）、上半贴 topTile（upper）
            addBox(0.0, 0.0, 0.0, 1.0, 0.5, 3.0 / 16.0, d.bottomTile, d.bottomTile, d.bottomTile);
            addBox(0.0, 0.5, 0.0, 1.0, 1.0, 3.0 / 16.0, topT, topT, topT);
            break;
        case BlockRegistry::ShapeTrapdoor:
            addBox(0.0, 0.0, 0.0, 1.0, 3.0 / 16.0, 1.0, topT, sideT, frontT);
            break;
        case BlockRegistry::ShapeSnowLayer: // 1 层雪 1/8 高（state 0 基底观感）
            addBox(0.0, 0.0, 0.0, 1.0, 0.125, 1.0, topT, sideT, frontT);
            break;
        default: // ShapeNone（非立绘类）/ ShapeBed：无盒渲染路径（调用方回落 2D pack 图 / 手绘 qrc 图标）
            return spec;
        }
    }
    spec.valid = true;

    // 深度排序（远 → 近，painter 依序覆盖）：观察方向 (+1,+1,+1) → 中心 (x+y+z) 越小越远。
    std::sort(spec.boxes.begin(), spec.boxes.end(), [](const AtlasIconBox &a, const AtlasIconBox &b) {
        const qreal da = (a.x0 + a.x1) + (a.y0 + a.y1) + (a.z0 + a.z1);
        const qreal db = (b.x0 + b.x1) + (b.y0 + b.y1) + (b.z0 + b.z1);
        return da < db;
    });
    return spec;
}

// 图集瓦片提取（水平条带图集：tile i 位于 x = i*kTile，高 = kTile）。
QImage atlasTile(const QImage &atlas, int tile)
{
    if (tile < 0)
        return {};
    const int x = tile * kTile;
    if (x + kTile > atlas.width())
        return {};
    return atlas.copy(x, 0, kTile, atlas.height());
}

// 瓦片明暗（预乘 RGB 乘系数，保持预乘关系 —— applyTint 同法）：顶 1.0 / 左前 0.80 / 右 0.62。
QImage shadedTile(const QImage &tile, qreal f)
{
    if (tile.isNull() || f >= 0.999)
        return tile;
    QImage img = tile.copy();
    const int num = qRound(f * 256.0);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = scan[x];
            scan[x] = qRgba((qRed(c) * num) >> 8, (qGreen(c) * num) >> 8, (qBlue(c) * num) >> 8, qAlpha(c));
        }
    }
    return img;
}

// t746 瓦片实心化（叶族图标专用，机制同离线 build_cube_icons.py load_face）：alpha<128 的孔用不透明像素
//   平均色填掉；半透（128..254）像素反预乘取原色后强制不透明（世界内 Mask cutout 下它们本就近似实心显示）。
//   兜底：全透瓦片用程序叶绿常量。img 为 Format_ARGB32_Premultiplied，输出保持同格式（alpha=255 时
//   预乘值 = 直行值，直接写直行色安全）。
QImage solidifyTile(const QImage &tile)
{
    if (tile.isNull())
        return tile;
    QImage img = tile.copy();
    qint64 sr = 0, sg = 0, sb = 0;
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(scan[x]);
            if (a < 128)
                continue; // 孔不参与代表色统计（预乘色会偏暗）
            sr += qBound(0, qRed(scan[x]) * 255 / a, 255);
            sg += qBound(0, qGreen(scan[x]) * 255 / a, 255);
            sb += qBound(0, qBlue(scan[x]) * 255 / a, 255);
            ++n;
        }
    }
    const int fr = n > 0 ? int(sr / n) : 90;  // 兜底叶绿 90/130/50（load_face 同款常量）
    const int fg = n > 0 ? int(sg / n) : 130;
    const int fb = n > 0 ? int(sb / n) : 50;
    for (int y = 0; y < img.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = scan[x];
            const int a = qAlpha(c);
            if (a >= 128) {
                // 半透像素：保留原色相/明暗，升满不透明（预乘 → 直行）。
                scan[x] = qRgb(qBound(0, qRed(c) * 255 / a, 255),
                               qBound(0, qGreen(c) * 255 / a, 255),
                               qBound(0, qBlue(c) * 255 / a, 255));
            } else {
                scan[x] = qRgb(fr, fg, fb); // 孔 → 代表色填
            }
        }
    }
    return img;
}

// 单个 dimetric 平行四边形面：仿射变换（单位正方形 → 平行四边形 o / o+e1 / o+e1+e2 / o+e2）贴图。
//   先用瓦片中心色实填多边形（抗相邻面抗锯齿缝透出透明底 —— 仅瓦片全不透明时；含透明孔瓦片如
//   铁活板门跳过底填保留孔洞），再 SmoothPixmapTransform 贴图。明暗已烘进瓦片（shadedTile）。
void drawIsoFace(QPainter &p, const QImage &tile, const QPointF &o,
                 const QPointF &e1, const QPointF &e2)
{
    if (tile.isNull())
        return;
    bool opaque = true;
    for (int y = 0; y < tile.height() && opaque; ++y) {
        const QRgb *scan = reinterpret_cast<const QRgb *>(tile.constScanLine(y));
        for (int x = 0; x < tile.width(); ++x) {
            if (qAlpha(scan[x]) < 255) {
                opaque = false;
                break;
            }
        }
    }
    QPolygonF poly;
    poly << o << (o + e1) << (o + e1 + e2) << (o + e2);
    if (opaque) {
        const QRgb c = tile.pixel(tile.width() / 2, tile.height() / 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(qRed(c), qGreen(c), qBlue(c)));
        p.drawPolygon(poly);
    }
    p.save();
    p.setTransform(QTransform(e1.x(), e1.y(), e2.x(), e2.y(), o.x(), o.y()));
    p.drawImage(QRectF(0.0, 0.0, 1.0, 1.0), tile);
    p.restore();
}

QImage renderAtlasIcon(const QImage &atlas, const AtlasIconSpec &spec)
{
    QImage img(int(kIconSize), int(kIconSize), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (spec.flat) {
        p.drawImage(QRectF(8.0, 8.0, kIconSize - 16.0, kIconSize - 16.0), atlasTile(atlas, spec.flatTile));
        return img;
    }
    for (const AtlasIconBox &b : spec.boxes) {
        // t746 瓦片预取 lambda：solidify（叶族）先实心化再明暗（与离线 load_face→render 顺序一致）。
        const auto faceTile = [&atlas, &spec](int tileIdx, qreal shade) {
            QImage t = atlasTile(atlas, tileIdx);
            if (spec.solidify)
                t = solidifyTile(t);
            return shadedTile(t, shade);
        };
        // 顶面 (+Y)：原点 = 远角 (x0,y1,z0)，e1 = +X，e2 = +Z（贴图 u→X / v→Z）。
        {
            const QPointF o = isoProject(b.x0, b.y1, b.z0);
            drawIsoFace(p, faceTile(b.topTile, 1.0), o,
                        isoProject(b.x1, b.y1, b.z0) - o, isoProject(b.x0, b.y1, b.z1) - o);
        }
        // 右面 (+X)：原点 = 顶后角 (x1,y1,z0)（e2 向下 = 贴图 v 上缘对齐盒顶），e1 = +Z。明暗 0.62。
        {
            const QPointF o = isoProject(b.x1, b.y1, b.z0);
            drawIsoFace(p, faceTile(b.sideTile, 0.62), o,
                        isoProject(b.x1, b.y1, b.z1) - o, isoProject(b.x1, b.y0, b.z0) - o);
        }
        // 左前面 (+Z)：原点 = 顶左角 (x0,y1,z1)，e1 = +X，e2 向下。明暗 0.80。
        {
            const QPointF o = isoProject(b.x0, b.y1, b.z1);
            drawIsoFace(p, faceTile(b.frontTile, 0.80), o,
                        isoProject(b.x1, b.y1, b.z1) - o, isoProject(b.x0, b.y0, b.z1) - o);
        }
    }
    return img;
}

// 纯程序图集（qrc；进程内单次加载）。作「pack 是否实际覆盖了该瓦片」比对基准。
const QImage &programAtlas()
{
    static const QImage img = QImage(QStringLiteral(":/textures/atlas.png"))
                                  .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return img;
}

// t764 ① 附魔台图标悬浮书叠层贴图（两态与 Main.qml bookDelegate / EnchantBookBox 同源）：pack 启用且
//   entity 目录命中 enchant_book（entityKindMap 同一表，两级探测同 entitySource）→ 包书贴图（*packLayout
//   置真，分区按 EnchantBookBox 布局 1 的 base 64×32 实测等比）；miss / 关 → qrc 程序 entity_enchant_book
//   （布局 0 左右对半）。解码失败也回退程序贴图（图标永不因包图异常而空书）。
QImage enchantBookOverlayTexture(bool packActive, const QString &entityDir, bool *packLayout)
{
    if (packLayout)
        *packLayout = false;
    if (packActive && !entityDir.isEmpty()) {
        QString relPath;
        for (const EntityTexEntry &e : entityKindMap()) {
            if (QLatin1String(e.kind) == QLatin1String("enchant_book")) {
                relPath = QString::fromLatin1(e.packPath);
                break;
            }
        }
        if (!relPath.isEmpty()) {
            const QDir dir(entityDir);
            QString p = dir.absoluteFilePath(relPath);
            if (!QFile::exists(p))
                p = dir.absoluteFilePath(QFileInfo(relPath).fileName());
            if (QFile::exists(p)) {
                const QImage pack(p);
                if (!pack.isNull()) {
                    if (packLayout)
                        *packLayout = true;
                    return pack.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                }
            }
        }
    }
    return QImage(QStringLiteral(":/textures/entity_enchant_book.png"))
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// t764 ① 附魔台图标叠画「悬浮敞开书」两页 V 形：与放置态 bookDelegate 同造型 —— 书脊沿 Z 过
//   (0.5, 0.80, z)（书心 ~0.82 的静息近似），两页各绕 Z 外倾 22°（半展 0.38·cos22°≈0.352、外缘抬升
//   0.38·sin22°≈0.142 → 页尖 y≈0.94），页深 z[0.27,0.73]（0.46 深）。斜置矩形在 dimetric 仿射投影下仍是
//   平行四边形 → drawIsoFace 直接贴图（u 自书脊向页外缘 = 阅读行向；v=0（图顶行）落 −Z 远端，与
//   EnchantBookBox t764 的 v 修正同口径）。分区（base 64×32 像素，任意分辨率按图尺寸等比）：
//   pack 封面带左封 {0,0,11,10} / 纸页叠 {1,10,12,19}——两区书脊都在区内**右缘** → 水平镜像令 u=0 在
//   书脊；程序左半封面 {0,0,32,32} / 右半纸页 {32,0,64,32}——书脊在**左缘**不镜像。明暗 左 0.86 / 右
//   1.0（同顶面族微差，读出 V 形两页）。分层（PLAN §2）：Core 图标渲染器自带 2D 画法，不依赖 Renderer
//   层几何（数值镜像互指，同 t633 mob 头像模式）。
void drawEnchantBookOverlay(QImage &img, const QImage &bookTex, bool packLayout)
{
    if (bookTex.isNull() || bookTex.width() < 4 || bookTex.height() < 4)
        return;
    const qreal sx = qreal(bookTex.width()) / 64.0;
    const qreal sy = qreal(bookTex.height()) / 32.0;
    const auto baseRect = [&bookTex, sx, sy](int u0, int v0, int u1, int v1) {
        return QRect(qRound(u0 * sx), qRound(v0 * sy),
                     qRound((u1 - u0) * sx), qRound((v1 - v0) * sy)).intersected(bookTex.rect());
    };
    QImage cover, paper;
    if (packLayout) {
        cover = bookTex.copy(baseRect(0, 0, 11, 10)).flipped(Qt::Horizontal);
        paper = bookTex.copy(baseRect(1, 10, 12, 19)).flipped(Qt::Horizontal);
    } else {
        cover = bookTex.copy(baseRect(0, 0, 32, 32));
        paper = bookTex.copy(baseRect(32, 0, 64, 32));
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QPointF spineFar = isoProject(0.5, 0.80, 0.27);   // 书脊 × 远端（−Z，图顶行落此端）
    const QPointF spineNear = isoProject(0.5, 0.80, 0.73);  // 书脊 × 近端（+Z 观察者侧）
    const QPointF e2 = spineNear - spineFar;                // v：远 → 近（页深向）
    // 左页（封面）：u 自书脊（0.5）向 −X 外缘（0.148, y+0.142）；先画（屏上位更远）。
    drawIsoFace(p, shadedTile(cover, 0.86), spineFar,
                isoProject(0.148, 0.942, 0.27) - spineFar, e2);
    // 右页（纸页）：u 自书脊向 +X 外缘（0.852, y+0.142）。
    drawIsoFace(p, shadedTile(paper, 1.0), spineFar,
                isoProject(0.852, 0.942, 0.27) - spineFar, e2);
}

// 合成图集瓦片 vs 程序图集瓦片逐像素比对（64×64 小图，开销可忽略）。尺寸/格式异常按「有差异」处理
//   （宁可多渲染不误杀 pack 覆盖）。
bool tileDiffersFromProgram(const QImage &composite, const QImage &program, int tile)
{
    const QImage a = atlasTile(composite, tile);
    const QImage b = atlasTile(program, tile);
    if (a.isNull() || b.isNull() || a.size() != b.size() || a.format() != b.format())
        return true;
    return a != b;
}

} // namespace

// t745 统一贴图原则：方块 item 图标运行期渲染（任意 pack 通用）。详见 resourcepackmanager.h 声明处注释。
QString ResourcePackManager::blockAtlasIconSource(int blockId, bool requirePackContribution)
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    BuiltState &s = state();
    if (requirePackContribution && !s.active)
        return {};
    const AtlasIconSpec spec = atlasIconSpecForBlock(blockId);
    if (!spec.valid)
        return {};
    // 「pack 实际覆盖了该块可见面吗」：合成图集瓦片与纯程序图集逐像素比对 —— 全相同 = pack 无产出 →
    //   返空串（调用方回落手绘程序图标 / FROM_PACK 家族的程序重渲；此时渲染结果只会复刻程序观感，
    //   覆盖手绘图标反而损失美术质量）。
    if (requirePackContribution) {
        const QImage &prog = programAtlas();
        bool contributed = false;
        if (spec.flat) {
            contributed = tileDiffersFromProgram(s.atlas, prog, spec.flatTile);
        } else {
            for (const AtlasIconBox &b : spec.boxes) {
                if (tileDiffersFromProgram(s.atlas, prog, b.topTile)
                    || tileDiffersFromProgram(s.atlas, prog, b.sideTile)
                    || tileDiffersFromProgram(s.atlas, prog, b.frontTile)) {
                    contributed = true;
                    break;
                }
            }
        }
        if (!contributed)
            return {};
    }
    // 缓存命中（revision 内稳定；apply() 重建清缓存 + 文件名带 revision → 无陈旧路径）。
    const auto it = s.blockIconFiles.constFind(blockId);
    if (it != s.blockIconFiles.constEnd() && QFile::exists(it.value()))
        return QStringLiteral("file:///") + it.value();
    QImage img = renderAtlasIcon(s.atlas, spec);
    if (img.isNull())
        return {};
    // t764 ① 附魔台叠画悬浮书（pack 命中包书贴图 / 否则程序书贴图；与放置态 bookDelegate 两态同源）。
    //   注意须在「缓存命中」检查之后、落盘之前 —— 叠层是渲染的一部分，不能只叠在内存像不落缓存。
    if (spec.bookOverlay) {
        bool packBook = false;
        const QImage bookTex = enchantBookOverlayTexture(s.active, s.entityDir, &packBook);
        drawEnchantBookOverlay(img, bookTex, packBook);
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        return {};
    QDir().mkpath(dir);
    // t764 文件族 icon → icon2：画法版本变更（附魔台加悬浮书）须换缓存名，否则老缓存（无书图标）在
    //   pack revision 未变时被永久复用（AppLocalData 里的旧 icon_*.png 成了无失效机制的陈旧派生物）。
    const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_icon2_%1_r%2.png").arg(blockId).arg(s.revision));
    if (!img.save(out, "PNG"))
        return {};
    s.blockIconFiles.insert(blockId, out);
    return QStringLiteral("file:///") + out;
}

// t715 状态效果 HUD 图标覆盖（实例 Q_INVOKABLE；Main.qml effectBar delegate 调）：pack 启用且 effectDir
//   （assets/minecraft/textures/mob_effect）有对应枚举的 PNG 时，返 file:///<effectDir>/<name>.png；否则空串
//   → 调用方回退 qrc:/textures/icon_effect_*.png 程序自绘。红线 §9：仅运行期读本地 gitignored pack PNG，
//   不 bake 进 qrc/VCS。active=false / 无 mob_effect 目录 / 无映射 / 文件缺 → ""。
QString ResourcePackManager::effectIconSource(int effectType) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.effectDir.isEmpty())
        return {};
    const QStringList names = effectIconFiles(effectType);
    for (const QString &name : names) {
        const QString p = QDir(s.effectDir).absoluteFilePath(name);
        if (QFile::exists(p))
            return QStringLiteral("file:///") + p;
    }
    return {};
}

// t717 画作贴图源（t720 Painting 方块 pack 覆盖前置）：index（0..26，与 paintingNames 表序一致）→
//   pack 启用且 paintingDir 有 <name>.png 时返 file:///<paintingDir>/<name>.png；否则空串 → 调用方回退
//   qrc:/textures/default_painting_<name>.png 程序自绘。不走 tileFilenameMap（27 张太多且画作是独立 Texture
//   非图集瓦片；effectIconSource 批量解析先例）。索引越界返空（防御存档异常 state）。
//   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
QString ResourcePackManager::paintingSource(int index) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.paintingDir.isEmpty())
        return {};
    const QStringList &names = paintingNames();
    if (index < 0 || index >= names.size())
        return {};
    const QString p = QDir(s.paintingDir).absoluteFilePath(names.at(index) + QStringLiteral(".png"));
    if (!QFile::exists(p))
        return {};
    return QStringLiteral("file:///") + p;
}

// t717 画作程序回退贴图名（index → default_painting_<name>.png；与 paintingNames 单一权威同表）。
//   t720 呈现层用本函数拿 qrc 程序贴图名（非 pack 态 / pack miss 时），免呈现层自持名字表副本。
//   索引越界返空（调用方回退纯色占位）。
QString ResourcePackManager::paintingFallbackName(int index) const
{
    const QStringList &names = paintingNames();
    if (index < 0 || index >= names.size())
        return {};
    return QStringLiteral("default_painting_") + names.at(index);
}

// t720 画作格尺寸（呈现层 paintingHost delegate 摆 quad 用）：委托 BlockRegistry::paintingSize 单一
//   权威（与 paintingNames 表序同源；越界 → 1 兜底）。
int ResourcePackManager::paintingWidth(int index) const
{
    int w = 1, h = 1;
    BlockRegistry::paintingSize(index, w, h);
    return w;
}
int ResourcePackManager::paintingHeight(int index) const
{
    int w = 1, h = 1;
    BlockRegistry::paintingSize(index, w, h);
    return h;
}

// t717 实体贴图源（t727/t728/t730/t731/t732 实体批 pack 覆盖前置）：kind（字符串 key，entityKindMap 表）
//   → pack 启用且 entityDir 命中（子目录布局 entity/<sub>/<name>.png 优先、扁平 entity/<name>.png 兜底，
//   同 mobTextureSource probe 两级探测）时返 file:/// URL；否则空串 → 调用方回退 qrc:/textures/<fallback>.png
//   程序自绘（tools/build_entities_pack.py；§9 改名夜行者 / 燃烬者）。无映射 kind 返空。
//   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
QString ResourcePackManager::entitySource(const QString &kind) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.entityDir.isEmpty())
        return {};
    QString relPath;
    for (const EntityTexEntry &e : entityKindMap()) {
        if (kind == QLatin1String(e.kind)) {
            relPath = QString::fromLatin1(e.packPath);
            break;
        }
    }
    if (relPath.isEmpty())
        return {};
    const QDir entityDir(s.entityDir);
    const auto probe = [&entityDir](const QString &rp) -> QString {
        const QString sub = entityDir.absoluteFilePath(rp);
        if (QFile::exists(sub))
            return QStringLiteral("file:///") + sub;
        const QFileInfo fi(rp);
        const QString flat = entityDir.absoluteFilePath(fi.fileName());
        if (QFile::exists(flat))
            return QStringLiteral("file:///") + flat;
        return {};
    };
    return probe(relPath); // 两级探测（子目录 → 扁平）；miss 返空回退程序贴图
}

// t717/t718 盔甲 layer 贴图源（玩家 + 人形 mob 护甲 3D 显示的 pack 覆盖）：tier（0 皮/1 铁/2 铜/3 金/4 钻/5 链）+
//   layer（1=头盔+胸甲+护腿 / 2=靴）→ pack 启用且 armorDir（models/armor）有 <prefix>_layer_<n>.png 时返
//   file:/// URL；否则空串 → 调用方回退 qrc:/textures/armor_<kind>_layer_<n>.png 程序层（tier 2 铜无 pack
//   等价恒走回退，t718 已产程序铜层 armor_copper_*）。
//   t718 接 TODO：皮革 pack 层是灰白可染色 base（demo 包实测 leather_layer_*.png 主体 (202,202,202) 灰白，
//   1.8.2 包未叠棕 overlay）→ 3D 直接用即显白。命中皮革 tier（0）时按 retintLeatherTemplate 染皮革棕梯度
//   （同 itemIconSource 皮革图标路径 R19 B1），落盘 voxelsandbox_rp_leather_layer_<n>.png 缓存（apply() 重建时
//   随 leatherIconFiles 一并清空重染）。解码/落盘失败 → 回退原样返回白底 base（可辨识护甲形状的降级）。
//   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
QString ResourcePackManager::armorLayerSource(int tier, int layer) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    BuiltState &s = state();
    if (!s.active || s.armorDir.isEmpty())
        return {};
    const QString name = armorLayerPackName(tier, layer);
    if (name.isEmpty())
        return {};
    const QString p = QDir(s.armorDir).absoluteFilePath(name);
    if (!QFile::exists(p))
        return {};
    // 非皮革（铁/金/钻/链）→ pack 层自带满色，原样返回。
    if (tier != 0)
        return QStringLiteral("file:///") + p;
    // 皮革：灰白 base 染棕。缓存键 = tier*10+layer（0x0 段；与 leatherIconFiles 的 0x300..0x303 段不冲突）。
    const int key = tier * 10 + layer;
    const auto cached = s.leatherIconFiles.constFind(key);
    if (cached != s.leatherIconFiles.constEnd() && QFile::exists(cached.value()))
        return QStringLiteral("file:///") + cached.value();
    QImage leather(p);
    if (leather.isNull())
        return QStringLiteral("file:///") + p; // 解码失败 → 回退白底（降级）
    leather = leather.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    retintLeatherTemplate(leather);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        return QStringLiteral("file:///") + p; // 无可写目录 → 回退白底（降级）
    QDir().mkpath(dir);
    const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_leather_layer_%1.png").arg(layer));
    if (!leather.save(out, "PNG"))
        return QStringLiteral("file:///") + p; // 落盘失败 → 回退白底（降级）
    s.leatherIconFiles.insert(key, out); // stateMutex 已持锁，安全
    return QStringLiteral("file:///") + out;
}

// t731 玩家皮肤名（"default"/"alex"；settings.json playerSkin 镜像，缺省 default）。/skin 命令经
//   setPlayerSkin 切换 + 持久化；Main.qml skinName 属性启动期从这里读初值。
QString ResourcePackManager::playerSkin() const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    return s.playerSkin == QLatin1String("alex") ? QStringLiteral("alex") : QStringLiteral("default");
}

// t731 切换玩家皮肤并持久化 settings.json（setEnabled/setPackPath 同 writeSettings 管线）。非法名拒收
//   （返 false，调用方回显用法）；合法则内存态立即生效（QML skinName 同步 → Texture 换绑）。
bool ResourcePackManager::setPlayerSkin(const QString &name)
{
    if (name != QLatin1String("default") && name != QLatin1String("alex"))
        return false;
    bool curEnabled;
    QString curPath;
    {
        QMutexLocker lock(&stateMutex());
        ensureBuiltLocked();
        BuiltState &s = state();
        s.playerSkin = name;
        s.configLoaded = true;
        curEnabled = s.enabled;
        curPath = s.packPath;
    }
    writeSettings(curEnabled, curPath, name); // 持久化（文件 IO 在锁外，同 setEnabled）
    return true;
}

// t731 玩家皮肤 pack 源（含 64×64 老式布局 → 64×32 裁切重排）：skin（"default"/"alex"）→ entityKindMap
//   的 skin_default（steve.png）/ skin_alex（alex.png）两级探测（子目录 → 扁平，同 entitySource）。
//   命中后按贴图实际宽高判型：h == w/2（64×32 族）→ 原样返回 file:///；更高（64×64 老式布局——上半 32
//   行是 base 头/身/臂/腿区，与 64×32 兼容；下半是 1.8+ overlay）→ QImage 裁上半 w×(w/2) 落盘
//   voxelsandbox_rp_skin_<kind>_r<revision>.png 缓存（apply() 重建随 skinPackFiles 清空重裁；revision
//   后缀保证换包后 URL 变 → QML 重读，同皮革染色落盘模式）。miss / 解码 / 落盘失败 → 空串 → 调用方回退程序皮肤 qrc:/textures/entity_skin_<default|alex>.png。
//   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
QString ResourcePackManager::playerSkinSource(const QString &skin) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    BuiltState &s = state();
    if (!s.active || s.entityDir.isEmpty())
        return {};
    const QString kind = (skin.compare(QLatin1String("alex"), Qt::CaseInsensitive) == 0)
            ? QStringLiteral("skin_alex") : QStringLiteral("skin_default");
    // entityKindMap 探测（两级：子目录 → 扁平）拿 pack 原图绝对路径。
    QString relPath;
    for (const EntityTexEntry &e : entityKindMap()) {
        if (kind == QLatin1String(e.kind)) {
            relPath = QString::fromLatin1(e.packPath);
            break;
        }
    }
    if (relPath.isEmpty())
        return {};
    const QDir entityDir(s.entityDir);
    QString src;
    const QString sub = entityDir.absoluteFilePath(relPath);
    if (QFile::exists(sub)) {
        src = sub;
    } else {
        const QFileInfo fi(relPath);
        const QString flat = entityDir.absoluteFilePath(fi.fileName());
        if (QFile::exists(flat))
            src = flat;
    }
    if (src.isEmpty())
        return {};
    // 命中缓存（pack 未重解析期间稳定）→ 直接返。
    const auto cached = s.skinPackFiles.constFind(kind);
    if (cached != s.skinPackFiles.constEnd() && QFile::exists(cached.value()))
        return QStringLiteral("file:///") + cached.value();
    // 判型 + 裁切：64×32 族（h == w/2）原样；64×64 老式布局裁上半 32 行（base 区，与 64×32 UV 分数
    //   兼容——UV 分母按 base 64×32，采样行 = 分数 × 实图高，裁后图高恰为宽一半 → 分数对齐）。
    QImage tex(src);
    if (tex.isNull())
        return {}; // 解码失败 → 回退程序皮肤（降级）
    const int w = tex.width(), h = tex.height();
    if (w <= 0 || h < w / 2)
        return {}; // 异常尺寸（非 2:1/1:1 族）→ 回退（降级）
    if (h == w / 2)
        return QStringLiteral("file:///") + src; // 已是 64×32 族，无需裁切
    const QImage cropped = tex.copy(0, 0, w, w / 2);
    if (cropped.isNull())
        return {};
    // 落盘缓存（AppLocalDataLocation，同 atlasFile / 皮革染色目录）。文件名带 revision（审查 #7：同
    //   t745 icon2 族先例——apply() 每次 ++s.revision → 换包重裁后落盘路径随版本变，QML Texture 按
    //   file:/// URL 重读新图；无 revision 时同路径落新图但 URL 不变 → 换包后皮肤陈旧直到重启）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        return {}; // 无可写目录 → 回退程序皮肤（降级）
    QDir().mkpath(dir);
    const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_skin_%1_r%2.png").arg(kind).arg(s.revision));
    if (!cropped.save(out, "PNG"))
        return {}; // 落盘失败 → 回退（降级）
    s.skinPackFiles.insert(kind, out); // stateMutex 已持锁，安全
    return QStringLiteral("file:///") + out;
}

QString ResourcePackManager::mobTextureSource(int mobType) const
{    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    // t749 非 const：羊合成贴图缓存写入（sheepWoolFaceFile，同 armorLayerSource 的 leatherIconFiles 写法）。
    BuiltState &s = state();
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
    // 单候选两级探测：1) 子目录布局（entity/<mob>/<mob>.png，现网大多数包）：MC 1.0 标准；
    //   2) 扁平回退（entity/<mob>.png，旧 / HD 包常省略子目录）：取文件名（去子目录）。miss 返空串。
    const auto probe = [&entityDir](const QString &rp) -> QString {
        const QString sub = entityDir.absoluteFilePath(rp);
        if (QFile::exists(sub))
            return QStringLiteral("file:///") + sub;
        const QFileInfo fi(rp);
        const QString flat = entityDir.absoluteFilePath(fi.fileName());
        if (QFile::exists(flat))
            return QStringLiteral("file:///") + flat;
        return {};
    };
    // review L15：主候选 miss 后的兜底候选 —— 羊。t593 主映射改 sheep/sheep_fur.png（毛层），但扁平布局
    //   老包只有 entity/sheep.png（本体肉身，无 sheep_fur 也无扁平 sheep_fur.png）→ 主候选两级都 miss
    //   → 整羊静默回退程序贴图。兜底 sheep/sheep.png 走同款两级探测：老包命中扁平 sheep.png（肉身贴图
    //   优于无贴图——长毛观感降级但仍显 pack 质感，且剪毛态语义不变）。仅羊有此兜底（其余 mob 的映射
    //   文件名即唯一候选）；前缀判定随映射同生共死（羊移出映射则兜底自动失效）。
    QString hit = probe(relPath);
    if (!hit.isEmpty()) {
        // t749 羊合成贴图（仅 mobType 3 且命中的是毛层主映射）：fur 毛身 + 本体层头区（真脸）→ 单贴图几何
        //   两态兼顾（见 generateSheepWoolFaceFile 布局取证注释）。缓存命中 O(1)；miss 合成落盘一次。
        //   本体层缺 / 合成失败 → 毛层原样返回（降级 = t593 现状，不劣化）。
        if (mobType == 3) {
            if (!s.sheepWoolFaceFile.isEmpty() && QFile::exists(s.sheepWoolFaceFile))
                return QStringLiteral("file:///") + s.sheepWoolFaceFile;
            const QString bodySub = entityDir.absoluteFilePath(QStringLiteral("sheep/sheep.png"));
            const QString bodyFlat = entityDir.absoluteFilePath(QStringLiteral("sheep.png"));
            const QString bodyPath = QFile::exists(bodySub) ? bodySub
                                  : (QFile::exists(bodyFlat) ? bodyFlat : QString());
            if (!bodyPath.isEmpty()) {
                const QString out = generateSheepWoolFaceFile(QUrl(hit).toLocalFile(), bodyPath);
                if (!out.isEmpty()) {
                    s.sheepWoolFaceFile = out; // stateMutex 已持锁，安全
                    return QStringLiteral("file:///") + out;
                }
            }
        }
        return hit;
    }
    if (relPath.startsWith(QStringLiteral("sheep/"))) {
        hit = probe(QStringLiteral("sheep/sheep.png"));
        if (!hit.isEmpty())
            return hit;
    }
    return {}; // 包内无该 entity 贴图 → 不覆盖（保留程序生成 / 纯色）；红线 §9：仅运行期读本地 pack PNG。
}

// t633 图鉴生物头像：mobType → 头部 box-UV 数据（u0, v0, w, h, d, 贴图 base 宽, 贴图 base 高）。
//   与 mobmodel.cpp 各 mob 分支的 setMobTex 头部值同源（单一权威在 Renderer；此处 Core 不能 include Renderer，
//   以注释互指 + 数值镜像——同 mobEntityMap 与 blockItemIconMap 的字面量模式）。裁剪区 = MC +Z Front 面
//   （u0+d, v0+d)-(u0+d+w, v0+d+h)（脸所在面；mobmodel.cpp mobFaceQtUV case 4 同公式）。
//   羊条目带 sheepBody=true 标记：主贴图 sheep_fur.png 毛层头前无脸 → 改从 sheep/sheep.png 本体层裁。
//   （结构体定义 + 声明已前置到文件顶部 —— 见该处 review D3-b 注释；此处只持数据表 + 实现。）
const QList<MobHeadRegion> &mobHeadRegions()
{
    static const QList<MobHeadRegion> kRegions = {
        //          mob  u0  v0   w   h   d  texW texH body
        /* 猪     */ {  1,  0,  0,  8,  8,  8,  64,  32, false },
        /* 牛     */ {  2,  0,  0,  8,  8,  6,  64,  32, false },
        /* 羊     */ {  3,  0,  0,  6,  6,  8,  64,  32, true  }, // t633：毛层头前无脸 → 本体层（有脸）
        /* 蹒跚者 */ {  4,  0,  0,  8,  8,  8,  64,  64, false },
        /* 骸骨   */ {  5,  0,  0,  8,  8,  8,  64,  32, false },
        /* 潜行者 */ {  6,  0,  0,  8,  8,  8,  64,  32, false },
        /* 蜘蛛   */ {  7, 32,  4,  8,  8,  8,  64,  32, false },
        /* 鸡     */ {  8,  0,  0,  4,  6,  3,  64,  32, false },
        /* 铁傀儡 */ { 13,  0,  0,  8, 10,  8, 128, 128, false },
        // ── t749 补齐七张空白头像（用户「鱿鱼/狼/豹猫/雪傀儡/蠹虫/夜行者/燃烬者头像空白」根因 = 本表缺条目，
        //    mobHeadIconSource 返空串 → QML 回退体色块）。头区数据 = demo 包像素取证（同 t598 铁傀儡实测法：
        //    枚举候选 box 的面矩形，取含五官特征的全不透明区），非 vanilla 源码值照抄（demo 包 HD 重绘有
        //    微偏移，如夜行者头前仅 6 行不透明）。
        /* 鱿鱼   */ {  9,  0,  0, 12, 16, 12,  64,  32, false, nullptr },
        //   鱿鱼无「头部盒」——mantle 前面 (12,12)-(24,28) 带双眼（demo 包 rows 18-19 亮斑实测）当头像；
        //   映射走 mobEntityMap t730 既有序（squid/squid.png 扁平）。
        /* 狼     */ { 10,  0,  0,  6,  6,  4,  64,  32, false, "wolf/wolf.png" },
        //   头盒 (0,0)6×6×4 → 前 (4,4)-(10,10)（demo 包 row6 双黑瞳 + row9 浅鼻吻实测）；显式源 = 不进
        //   mobEntityMap（见 MobHeadRegion::explicitSrc 注释——狼几何全脸 UV 无 box-UV 数据）。
        /* 豹猫   */ { 11,  1,  1,  5,  4,  4,  64,  32, false, "cat/ocelot.png" },
        //   头盒 (1,1)5×4×4 → 前 (5,5)-(10,9)（row6 双黑点眼实测）；demo 包路径 entity/cat/ocelot.png（1.8+
        //   猫科合并目录，非 ocelot/ 子目录）。
        /* 雪傀儡 */ { 12,  0,  0,  8,  8,  8,  64,  64, false, nullptr },
        //   旧注「头是南瓜方块非 entity 贴图」对 demo 包不成立——snow_golem.png 头盒 (0,0)8×8×8 前面
        //   (8,8)-(16,16) 画有深色 derpy 脸（rows 13-14 竖排双眼实测）；映射走 mobEntityMap（snow_golem.png 扁平）。
        /* 蠹虫   */ { 14,  0,  2,  8,  5,  2,  64,  32, false, "silverfish.png" },
        //   蠹虫无标准头部盒（vanilla 多节虫模型碎盒）→ 取实证「头段」区前 (2,4)-(10,9)（甲壳 + 暗斑，
        //   读作虫头）；显式源（同狼——mobEntityMap 不含 14，防 3D packTextured 误命中）。
        /* 夜行者 */ { 16,  0,  0,  8,  6,  8,  64,  32, false, nullptr },
        //   头盒 (0,0)8×8×8 但 demo 包头前仅 rows 8-13 不透明（底部 2 行空）→ h=6 取 (8,8)-(16,14)（row12
        //   左右对称亮眼实测：x8-10 / x13-15）；映射走 mobEntityMap（enderman/enderman.png t727 既有序）。
        /* 燃烬者 */ { 17,  0,  0,  8,  8,  8,  64,  32, false, nullptr },
        //   头盒 (0,0)8×8×8 前 (8,8)-(16,16)（黄焰纹实测；demo 包 blaze.png 实为 64×32 base 非 vanilla 64×64，
        //   texH 按**包内实底**取 32——scale 公式按 min(w/texW,h/texH) 裁剪不受模型侧 g_texH=64 影响）；
        //   映射走 mobEntityMap（blaze/blaze.png t728 既有序）。
    };
    return kRegions;
}

// review D3-b 头像裁剪核心（从 mobHeadIconSource 抽出，供构建期预生成 + 查询期懒生成两路共用）：
//   给定头部区数据 + entity 目录 → 解析源贴图（羊走本体层 sheep/sheep.png；其余走 mobEntityMap 主映射，
//   子目录 / 扁平两级探测）→ 按 base 比例裁 Front 像素区 → 放大 64×64 透明底 → 落盘
//   AppLocalData/voxelsandbox_rp_mobhead_<mobType>.png → 返落盘绝对路径（空串 = 任一步失败，调用方回退）。
//   纯函数（只读 entityDir + 写落盘文件；不触碰 BuiltState —— 缓存插入由调用方做，锁语义归 caller）。
QString generateMobHeadIconFile(const MobHeadRegion &region, const QString &entityDirPath)
{
    const QDir entityDir(entityDirPath);
    QString srcPath;
    if (region.sheepBody) {
        const QString sub = entityDir.absoluteFilePath(QStringLiteral("sheep/sheep.png"));
        const QString flat = entityDir.absoluteFilePath(QStringLiteral("sheep.png"));
        srcPath = QFile::exists(sub) ? sub : (QFile::exists(flat) ? flat : QString());
    } else if (region.explicitSrc) {
        // t749 头像专用显式源（狼 wolf/wolf.png / 豹猫 cat/ocelot.png / 蠹虫 silverfish.png）：两级探测同
        //   主映射（子目录布局优先、扁平文件名兜底）。只喂头像——mobTextureSource 不读它（3D 路径零改动）。
        const QString rel = QString::fromLatin1(region.explicitSrc);
        const QString sub = entityDir.absoluteFilePath(rel);
        if (QFile::exists(sub)) {
            srcPath = sub;
        } else {
            const QFileInfo fi(rel);
            const QString flat = entityDir.absoluteFilePath(fi.fileName());
            if (QFile::exists(flat)) srcPath = flat;
        }
    } else {
        QString relPath;
        for (const auto &m : mobEntityMap()) {
            if (m.first == region.mobType) { relPath = m.second; break; }
        }
        if (relPath.isEmpty())
            return {};
        const QString sub = entityDir.absoluteFilePath(relPath);
        if (QFile::exists(sub)) {
            srcPath = sub;
        } else {
            const QFileInfo fi(relPath);
            const QString flat = entityDir.absoluteFilePath(fi.fileName());
            if (QFile::exists(flat)) srcPath = flat;
        }
    }
    if (srcPath.isEmpty())
        return {};
    QImage tex(srcPath);
    if (tex.isNull())
        return {}; // 解码失败 → 回退
    // base 像素矩形 → 实际像素（HD 包是 base 整数倍）→ 裁剪（边界内钳防越界读）。
    const float scale = std::min(float(tex.width()) / float(region.texW),
                                 float(tex.height()) / float(region.texH));
    const int fx0 = qRound(float(region.u0 + region.d) * scale);
    const int fy0 = qRound(float(region.v0 + region.d) * scale);
    const int fw  = qMax(1, qRound(float(region.w) * scale));
    const int fh  = qMax(1, qRound(float(region.h) * scale));
    if (fx0 < 0 || fy0 < 0 || fx0 + fw > tex.width() || fy0 + fh > tex.height())
        return {}; // 越界（非整数倍贴图 / 数据错）→ 回退（降级）
    QImage head = tex.copy(fx0, fy0, fw, fh);
    if (head.isNull())
        return {};
    // 放大到 64×64 透明底（FastTransformation 保像素锐利；非 MC 资产——是 pack PNG 的运行期裁剪产物）。
    QImage icon(64, 64, QImage::Format_ARGB32_Premultiplied);
    icon.fill(Qt::transparent);
    const float kAspect = float(head.width()) / float(head.height());
    QImage scaled = (kAspect >= 1.0f)
            ? head.scaled(64, qMax(1, qRound(64.0f / kAspect)), Qt::IgnoreAspectRatio, Qt::FastTransformation)
            : head.scaled(qMax(1, qRound(64.0f * kAspect)), 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPainter p(&icon);
    p.drawImage((64 - scaled.width()) / 2, (64 - scaled.height()) / 2, scaled);
    p.end();
    // 落盘缓存（AppLocalDataLocation，同 atlasFile 目录）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        return {}; // 无可写目录 → 回退（降级）
    QDir().mkpath(dir);
    const QString out = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_mobhead_%1.png").arg(region.mobType));
    if (!icon.save(out, "PNG"))
        return {}; // 落盘失败 → 回退（降级）
    return out;
}

// t749 羊「毛身 + 真脸」合成贴图生成（见文件顶 generateSheepWoolFaceFile 声明处设计注释；mobTextureSource(3)
//   调用，stateMutex 已持锁——纯函数只读两源 PNG + 写落盘文件，缓存插入归 caller）。
//   布局依据（demo 包 512×256 = 64×32 base ×8 像素取证）：fur 层与本体层的 body/legs 行（base rows 14-31 +
//   body 顶面 rows 8-13 x≥28）布局逐区一致（毛盒与身盒同框同位）；唯头部区（base (0,0)-(28,14)）不同——
//   fur 是**膨胀毛盒**（偏移 + 无脸），本体层头盒 (0,0)6×6×8 与 MobModel 羊 setMobTex 同布局（有脸）。
//   单贴图 MobModel 只有一套 box-UV（本体布局）→ 直用 sheep_fur.png 时头六面全采偏移区 = 用户
//   「3D 预览完全不像羊」根因。合成 = fur 整张（毛身白）+ 本体层头区覆写 → 长毛羊身 + 真脸，两态兼顾
//   （t593「无毛粉肉身」与本次「毛层无脸」两轮诉求一次满足；机制等价 MC 本体+毛层双层模型，单层几何近似）。
QString generateSheepWoolFaceFile(const QString &furPath, const QString &bodyPath)
{
    QImage fur(furPath), body(bodyPath);
    if (fur.isNull() || body.isNull())
        return {}; // 任一解码失败 → 回退（降级：调用方仍返毛层原样）
    // 各文件自身 HD 倍率（两文件通常同倍；异倍包各算各的，头区裁剪按各自 base 坐标换算）。
    const float furS = std::min(float(fur.width()) / 64.0f, float(fur.height()) / 32.0f);
    const float bodyS = std::min(float(body.width()) / 64.0f, float(body.height()) / 32.0f);
    if (furS <= 0.0f || bodyS <= 0.0f)
        return {}; // 非 base 整数倍（异形包）→ 回退
    QImage out = fur.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // 本体层头区（base (0,0)-(28,14)：头 top/bottom + 侧面四连含脸）。
    QImage headCrop = body.copy(0, 0, qRound(28 * bodyS), qRound(14 * bodyS));
    if (headCrop.isNull())
        return {};
    if (std::abs(furS - bodyS) > 0.01f)
        headCrop = headCrop.scaled(qRound(28 * furS), qRound(14 * furS),
                                   Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPainter p(&out);
    // Source 模式连 alpha 一起覆写（SourceOver 会把本体层头区外的透明像素留成毛层底 → 残毛边）；
    //   头区像素 = 本体层原值（含头区边缘的透明 → 干净截断）。
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(0, 0, headCrop);
    p.end();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        return {}; // 无可写目录 → 回退（降级）
    QDir().mkpath(dir);
    const QString file = QDir(dir).absoluteFilePath(
            QStringLiteral("voxelsandbox_rp_sheep_woolface.png"));
    if (!out.save(file, "PNG"))
        return {}; // 落盘失败 → 回退（降级）
    return file;
}

// t633 图鉴生物头像裁剪（见头文件 mobHeadIconSource 注释）。首查缓存（pack 未重解析期间稳定）→ miss 走
//   generateMobHeadIconFile（裁剪 + 落盘）→ 记缓存返 file:/// 路径。构建期已预生成（ensureBuiltLocked 末尾，
//   review D3-b）→ 本路径的懒生成仅兜底「构建期失败但运行期文件恢复」等罕见态；常规调用 O(1) 命中缓存。
//   任意失败 → 空串（调用方回退体色方块，降级不阻塞）。
QString ResourcePackManager::mobHeadIconSource(int mobType) const
{
    const MobHeadRegion *region = nullptr;
    for (const MobHeadRegion &r : mobHeadRegions()) {
        if (r.mobType == mobType) { region = &r; break; }
    }
    if (!region)
        return {}; // 无头部区数据（雪傀儡 / 无映射 mob）→ 回退
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    BuiltState &s = state();
    if (!s.active || s.entityDir.isEmpty())
        return {};
    // 命中缓存（pack 未重解析期间稳定）→ 直接返。
    const auto cached = s.mobHeadIconFiles.constFind(mobType);
    if (cached != s.mobHeadIconFiles.constEnd() && QFile::exists(cached.value()))
        return QStringLiteral("file:///") + cached.value();
    const QString out = generateMobHeadIconFile(*region, s.entityDir);
    if (out.isEmpty())
        return {};
    s.mobHeadIconFiles.insert(mobType, out);
    return QStringLiteral("file:///") + out;
}

// t585 帧环 index：frame01（0..1 环值）→ 线性均匀帧序（mcmeta {"animation":{}} 默认均匀帧）的 index。
//   round 而非 floor：frame01 环回 1.0 时 round(N)=N → mod N = 0 正确归零（floor 在恰 1.0 时也 0，但 round
//   让帧边界对称居中，视觉上磁针/盘过中点才换帧）。
int animFrameIndex(qreal frame01, int count)
{
    const qreal wrapped = frame01 - std::floor(frame01);      // fmod 归一到 [0,1)（负角输入也安全）
    const int idx = qRound(wrapped * count) % count;
    return idx < 0 ? idx + count : idx;
}

// t585 原始状态值（指南针相对角/2π、钟 dayPhase）+ 帧序零位锚 → 帧 index（锚属帧序语义，Core 单一权威）。
int animFrameIndexForState(const BuiltState::AnimFrames &af, qreal state01)
{
    return animFrameIndex(state01 + af.anchor01, af.count);
}

QString ResourcePackManager::animatedItemFrameSource(int itemId) const
{
    QMutexLocker lock(&stateMutex());
    ensureBuiltLocked();
    const BuiltState &s = state();
    if (!s.active || s.itemDir.isEmpty())
        return {};
    // 帧序列态（构建期探测；无该物品 / 无帧文件 → 空串）。帧 index 由最近推送的原始状态值 + 帧序锚算出
    //   （未推过 → 状态 0 = 零位帧：指南针针指上 / 钟正午，随后 4Hz 内校正到真值）。
    const auto it = s.animItems.constFind(itemId);
    if (it == s.animItems.constEnd() || it->count <= 0)
        return {};
    const int idx = animFrameIndexForState(*it, it->lastState01);
    const QString path = QDir(s.itemDir).absoluteFilePath(
            QStringLiteral("%1_%2.png").arg(it->stem).arg(idx, 2, 10, QLatin1Char('0')));
    if (!QFile::exists(path))
        return {}; // 防御：探测后帧文件被删（不覆盖，回落 itemIconSource 静态图 / 自绘）
    return QStringLiteral("file:///") + path;
}

void ResourcePackManager::updateAnimatedItemState(qreal compassFrame01, qreal clockFrame01)
{
    // 锁内算新帧 index 并比较（无变化零开销）；递增 + 广播在锁外（避免持锁 emit 连到再加锁的槽）。
    bool changed = false;
    int newRevision = 0;
    {
        QMutexLocker lock(&stateMutex());
        ensureBuiltLocked();
        BuiltState &s = state();
        const struct { int id; qreal v; } inputs[] = {
            { 0x23F, compassFrame01 },
            { 0x240, clockFrame01 },
        };
        for (const auto &in : inputs) {
            const auto it = s.animItems.find(in.id);
            if (it == s.animItems.end() || it->count <= 0)
                continue; // pack 关 / 无帧序列 → 无帧可切（itemIconSource 静态图自会随 activeChanged 刷）
            const int idx = animFrameIndexForState(*it, in.v);
            if (idx != it->lastIndex) {
                it->lastIndex = idx;
                changed = true;
            }
            it->lastState01 = in.v; // 查询侧（animatedItemFrameSource）用最新状态（同帧 index 不变则不广播）
        }
        if (changed) {
            ++s.animRevision;
            newRevision = s.animRevision;
        }
    }
    if (!changed)
        return;
    // 广播到全部实例（同 apply() 的 activeChanged 模式）：同步 m_animRevision + emit animRevisionChanged
    //   → MaterialIcon packImg.source 绑定触碰 rp.animRevision（AOT 安全守卫 `_r >= 0`）→ 重查帧文件路径。
    for (ResourcePackManager *inst : rpInstances()) {
        inst->m_animRevision = newRevision;
        emit inst->animRevisionChanged();
    }
}
