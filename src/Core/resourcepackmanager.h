#ifndef RESOURCEPACKMANAGER_H
#define RESOURCEPACKMANAGER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QList>
#include <QPair>
#include <QtQml/qqml.h>

#include "blockregistry.h"   // t489 流体条带帧数常量（kWaterStripFrames / kLavaStripFrames）

// Resource-pack loader core（t414，phase 1：方块贴图覆盖；t415：完整 tile→文件名映射 + 运行期开关 UI）。
//
// 启动时解析资源包路径，优先级（spec t414）：
//   1) settings.json 字段 "resourcePack"（绝对 / 相对路径，相对 exe 目录解析）；
//   2) 环境变量 VOXELSANDBOX_RESOURCEPACK；
//   3) 默认探查：resourcepacks/active/ → 再探 dev 包 docs/Default HD 128x Demo 1.8.2.2/。
// settings.json "resourcePackEnabled"（缺省 true）可整体禁用。
//
// 找到合法包（含 assets/minecraft/textures/block/）时：以 qrc 程序生成图集为底，对「引擎 tile
// → 包内标准贴图文件名」映射里存在的瓦片，把包内 PNG 缩放到 TILE=16（= 引擎瓦片尺寸，与
// tools/build_atlas.py TILE=16 / chunkgeometry N*16 UV 对齐）后覆盖该瓦片，得到一份运行期合成图集。
// 合成图集落盘到 AppLocalDataLocation/voxelsandbox_rp_atlas.png，atlasSource 返回 file:/// 该路径供
// QtQuick3D Texture 直接加载（QtQuick3D 的贴图加载器不走 QtQuick QQuickImageProvider → image:// URL
// 在 Texture 上是空贴图 = 全白方块，故必须 file://）；active=false（无包 / 被禁用）时回退
// qrc:/textures/atlas.png（程序生成默认）。缺省 enabled=false：启动用程序生成图集，用户在设置面板
// 显式选目录后才覆盖，避免无感知切换贴图。
//
// t415 运行期开关：enabled / packPath 为 QML 可写属性（持久化 settings.json）；apply() 触发重新解析 +
// 重建合成图集、覆盖落盘 + 刷新 atlasSource（file:// URL 随 active 切换在 file:/// 与 qrc:/ 间变化 →
// QML Texture 重载）→ 用户在设置面板切换 / 改路径后即时生效，无需重启。合成状态由进程全局 mutex
// 保护（atlasSource 落盘路径读与 apply() 重建 / 重写互斥，防 QImage 竞态）。
//
// 红线（PLAN §9）：本类只读取本地 / gitignored 路径的包 PNG，绝不把任何 MC 资产 bake 进 qrc 或提交进 VCS。
// 映射表（引擎 tile → 标准贴图文件名）是功能性元数据，可随代码提交；纹理文件本身不进仓库。引擎默认在
// 无包时仍以程序生成图集正常工作。
//
// 分层（PLAN §2）：Core 叶子工具，只依赖 Qt（Core/Gui），不 include World/Renderer/Game。被 Main.qml
// （呈现层）实例化（QML_NAMED_ELEMENT 门面）；合成图集仅落盘为 PNG 供 QtQuick3D Texture 经 file:// 直
// 接加载（Core 不沾 QtQuick）。compositeAtlas()/packActive() 仍保留以兼容 main.cpp 注册的 image provider
// （仅 QtQuick 路径，地形贴图不依赖该 provider）。
class ResourcePackManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ResourcePackManager)
    // 资源包是否启用且存在（启动期解析，运行期经 apply() 重建）。active=false → QML 用 qrc 程序生成图集。
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // 地形图集贴图源 URL：active → file:///<AppLocalData>/voxelsandbox_rp_atlas.png（落盘的合成图集；
    //   QtQuick3D Texture 不支持 image:// QQuickImageProvider，故必须 file://）；否则 qrc:/textures/atlas.png。
    Q_PROPERTY(QString atlasSource READ atlasSource NOTIFY activeChanged)
    // t489 流体条带贴图源（材质级 flipbook 动画）：active → file:///<AppLocalData>/voxelsandbox_<x>_strip.png
    //   （落盘的合成条带 = qrc 程序生成条带 + 包内帧覆盖）；否则 qrc:/textures/<x>_strip.png。水/岩浆段
    //   独立材质 baseColorMap 指向此条带（不走共享图集 voxelAtlas）→ 动画 positionV 只动水/岩浆，不动其它方块。
    Q_PROPERTY(QString waterStripSource READ waterStripSource NOTIFY activeChanged)
    Q_PROPERTY(QString lavaStripSource READ lavaStripSource NOTIFY activeChanged)
    // t489 条带帧数（与 BlockRegistry::kWaterStripFrames / kLavaStripFrames 同源单一权威；QML positionV 动画
    //   步长 = k/N 用此值，mesher UV 子区高 1/N 用 blockregistry 常量）。CONSTANT：值不随运行期变。
    Q_PROPERTY(int waterStripFrames READ waterStripFrames CONSTANT)
    Q_PROPERTY(int lavaStripFrames READ lavaStripFrames CONSTANT)
    // t415 资源包总开关（镜像 settings.json resourcePackEnabled，缺省 false：避免无感知切换贴图）。
    //   setter 立即持久化；配合 apply() 即时重建图集（也可仅持久化等下次重启生效）。
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // t415 资源包路径（镜像 settings.json resourcePack，空 = 走环境变量/默认探查）。setter 立即持久化。
    Q_PROPERTY(QString packPath READ packPath WRITE setPackPath NOTIFY configChanged)

public:
    explicit ResourcePackManager(QObject *parent = nullptr);
    // t420 注销实例注册表（apply 广播 activeChanged 用）；默认析构即可，显式声明以配套注册表注销。
    ~ResourcePackManager();

    bool active() const { return m_active; }
    QString atlasSource() const;
    // t489 流体条带贴图源（材质级 flipbook；详见 Q_PROPERTY 注释）。active → file:/// 落盘合成条带；否则 qrc 程序生成条带。
    QString waterStripSource() const;
    QString lavaStripSource() const;
    int waterStripFrames() const { return BlockRegistry::kWaterStripFrames; }
    int lavaStripFrames() const { return BlockRegistry::kLavaStripFrames; }

    bool enabled() const;
    void setEnabled(bool e);
    QString packPath() const;
    void setPackPath(const QString &p);
    // t415 应用当前 enabled/packPath：重新解析包 + 重建合成图集 + 刷新 atlasSource（cache-bust）。
    Q_INVOKABLE void apply();

    // t420 物品图标覆盖：pack 启用且 itemId 在「引擎物品 id → pack item 文件名」映射内、且包内对应 PNG
    //   存在时，返回 file:///<itemDir>/<file> 供 QtQuick Image 直接加载（缩到图标尺寸）；否则返空串
    //   → ToolIcon/MaterialIcon 回退自绘 Canvas。运行期读本地 gitignored pack 路径（红线 §9：PNG 不进 qrc/VCS）。
    //   itemDir 在 ensureBuiltLocked 与 block 目录一并解析缓存；active=false / itemDir 空 / 无映射 / 文件缺 → ""。
    Q_INVOKABLE QString itemIconSource(int itemId) const;

    // t497 生存背包空护甲槽图标覆盖：pack 启用且包内 item 目录有 empty_armor_slot_<piece>.png（piece 取
    //   "helmet"/"chestplate"/"leggings"/"boots" 四部位，与 ArmorRegistry::ArmorPiece 同序 0..3）时，
    //   返 file:///<itemDir>/empty_armor_slot_<piece>.png 供 QtQuick Image 加载（alpha-test 透明底，机制等价
    //   MC 1.0 survival 背包空装备槽占位图）；否则返空串 → SurvivalInventory 回退自绘 Canvas 暗灰金属剪影。
    //   运行期读本地 gitignored pack PNG（红线 §9：PNG 不进 qrc/VCS）。active=false / 无 item 目录 / 文件缺 → ""。
    Q_INVOKABLE QString emptyArmorSlotSource(int armorPiece) const;

    // t421 生物模型贴图覆盖：pack 启用且 mobType 在「引擎 mob id → pack entity 子目录/文件名」映射内、且包内
    //   对应 PNG 存在时，返回 file:///<entityDir>/<mob>/<mob>.png 供 QtQuick3D Texture 直接加载（MobModel 据
    //   packTextured 把几何 UV 按 T 字展开进该贴图）；否则返空串 → Main.qml 各 mob delegate 回退程序生成
    //   mob_*.png（pig/cow/sheep/shambler/chicken）或纯色 baseColor（stalker/bones/spider）。映射取标准 MC 1.0
    //   entity 子目录命名（pig/cow/sheep/chicken/zombie/skeleton/creeper/spider），是功能性元数据（红线 §9 可随
    //   代码提交）；贴图文件本身仅运行期读本地 gitignored pack，不 bake 进 qrc/VCS。active=false / 无 entity 目录 /
    //   无映射 / 文件缺 → ""。子目录缺时自动回退扁平 entity/<mob>.png（兼容旧 / HD 包布局）。
    Q_INVOKABLE QString mobTextureSource(int mobType) const;

    // 引擎图集瓦片尺寸（tools/build_atlas.py TILE=16 + chunkgeometry UV 的 N*16 同源；公开供 image provider 复用）。
    static constexpr int kTile = 16;

    // 合成图集（程序生成图集 + 包覆盖）。幂等首调构建并缓存；运行期经 apply() 重建。供 image provider 调用。
    static QImage compositeAtlas();
    // 包是否启用且存在（与 active() 同源；ensureBuilt 后稳定）。供 image provider / main 判定。
    static bool packActive();
    // t456 方块 item 图标覆盖（静态，供 Hotbar 等 Game 层无实例调用）：pack 启用且 blockId 在「方块→pack item/
    //   前贴图文件名」映射内（现 CraftingTable/Furnace）、且包内 PNG 存在时（item 目录优先、block 目录兜底），
    //   返 file:// URL；否则空串 → Hotbar::iconSourceForBlock 回退程序生成 icon_<block>.png。工作台/熔炉等 2D
    //   物品图标此前用程序绘制等距立方体 icon_*.png（"旧版"）；pack 有对应 item 贴图则改用 pack（机制等价 MC
    //   1.0 item icon）。仅 2D 物品图标路径消费；3D 手持立方 / 掉落物走 BlockCube+voxelAtlas 另一路径，不受影响。
    //   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。active=false / 无映射 / 文件缺 → ""。
    static QString blockItemIconSource(int blockId);

signals:
    void activeChanged();   // active 或 atlasSource（revision）变（驱动 QML Texture 重载）
    void configChanged();   // enabled / packPath 变（驱动设置 UI 刷新；不立即重建图集）

private:
    bool m_active = false;
};

#endif // RESOURCEPACKMANAGER_H
