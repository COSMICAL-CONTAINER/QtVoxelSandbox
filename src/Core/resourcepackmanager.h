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
    // t585 指南针/钟动画帧修订号：帧切换（updateAnimatedItemState 检出帧环 index 变化）时 ++ 并向全部
    //   实例广播 → MaterialIcon 的 packImg.source 绑定触碰本值（qmlcachegen AOT 安全守卫 `_r >= 0`）→
    //   重查 itemIconSource(0x23F/0x240) 拿到新帧文件 → 全工程（hotbar/手持/掉落物/背包槽）图标统一刷帧。
    //   CONSTANT 不适用（随帧变化）；QML 侧 4Hz Timer 节流推送状态值，本值只在帧真变时递增。
    Q_PROPERTY(int animRevision READ animRevision NOTIFY animRevisionChanged)

public:
    explicit ResourcePackManager(QObject *parent = nullptr);
    // t585 指南针/钟动画帧序列查询：pack 启用且 itemId 是指南针(0x23F)/钟(0x240)（QML 字面量同源 recipe.h
    //   CompassId/ClockId）、包内 item 目录有 <stem>_%1.png 帧文件时，返回 file:///<itemDir>/<stem>_NN.png
    //   （NN = 按 updateAnimatedItemState 已推送的 0..1 环值选出的帧 index，两位零填充）。帧文件在构建期
    //   探测实际存在数（compass 32 / clock 64，demo 包实测；用户口述 34/66 系误差——以盘上文件为准，
    //   帧步长 = 360/实际帧数）。pack 关 / 无帧文件 → 空串（调用方回落 itemIconSource 静态图或自绘）。
    //   红线 §9：帧 PNG 仅运行期读本地 gitignored pack，不 bake 进 qrc/VCS。
    // 帧映射（机制等价 MC 1.0 compass/clock 每帧物品贴图）：帧 index = round((原始状态 + anchor01) * N) mod N，
    //   anchor01 = 帧序零位锚（Core 单一权威；两物品各自独立定锚，非共用）：
    //   - 指南针（锚 0.5）：原始状态 = 磁针指向出生点方向相对玩家视线的顺时针角 / 360（0=正前）。实测帧序：
    //     compass_16 = 红针尖正上（= 出生点在正前）→ 状态 0 对应帧 N/2。
    //   - 钟（锚 0.0；t612 修正）：原始状态 = WorldClock.dayPhase（0=正午 / 0.5=子夜）。逐帧像素取证
    //     （demo 包 clock_00..63）：clock_00 = 太阳居中窗（正午）、clock_32 = 月亮居中窗（子夜）→
    //     帧号与 dayPhase 同向同零。t585 曾误读 clock_32=全昼而设锚 0.5 →「设 0 显晚上」。
    Q_INVOKABLE QString animatedItemFrameSource(int itemId) const;
    // t585 帧 GUI 线程节流推进（~4Hz）：QML Timer 调用，携带当前指南针/钟状态环值。内部算出新帧 index 与
    //   当前不同时才 ++animRevision + 广播（无变化零开销）。Core 层不持 Game 层引用（出生点/相位由 QML
    //   呈现层算好传入 → 不破 PLAN §2 分层）。环值存 BuiltState 供 animatedItemFrameSource 查询。
    Q_INVOKABLE void updateAnimatedItemState(qreal compassFrame01, qreal clockFrame01);

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
    int animRevision() const { return m_animRevision; }
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

    // t715 状态效果 HUD 图标覆盖：pack 启用且包内 mob_effect 目录（assets/minecraft/textures/mob_effect）有
    //   对应效果枚举（PlayerState::StatusEffect 序：1=Poison/2=Slowness/3=Fire）的 PNG（poison.png 等）时，
    //   返 file:///<effectDir>/<name>.png 供 QtQuick Image 加载；否则空串 → Main.qml 效果栏回退 qrc 程序自绘
    //   icon_effect_*.png。MC 1.0 无 mob_effect 目录（老包 miss 属常态，安全跳过）。红线 §9：仅运行期读本地
    //   gitignored pack PNG，不 bake 进 qrc/VCS。active=false / 无目录 / 无映射 / 文件缺 → ""。
    Q_INVOKABLE QString effectIconSource(int effectType) const;

    // t717 画作贴图覆盖（t720 Painting 方块的 pack 映射前置）：index（0..26，与 tools/build_paintings.py
    //   PAINTINGS 表序一致——paintingNames() 字面量镜像单一权威）→ pack 启用且包内 painting 目录
    //   （assets/minecraft/textures/painting）有 <name>.png 时返 file:///<paintingDir>/<name>.png 供
    //   QtQuick3D Texture 直接加载；否则空串 → 调用方回退 qrc:/textures/default_painting_<name>.png 程序
    //   自绘（paintingFallbackName(index) 拿名字）。27 张不走 tileFilenameMap（画作是独立 Texture 非图集
    //   瓦片；effectIconSource 批量解析先例）。索引越界 / active=false / 无目录 / 文件缺 → ""。
    //   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
    Q_INVOKABLE QString paintingSource(int index) const;
    // t717 画作程序回退贴图名（index → "default_painting_<name>"，不含扩展名 / qrc 前缀——呈现层拼
    //   "qrc:/textures/" + name + ".png"）。与 paintingNames 单一权威同表；越界返空。
    Q_INVOKABLE QString paintingFallbackName(int index) const;
    // t720 画作格尺寸查询（呈现层 paintingHost delegate 摆 w×h quad 用）：index（0..26）→ BlockRegistry::
    //   paintingSize 单一权威（paintingNames 表序同源）。越界 → 1（QML 兜底 1×1）。CONSTANT 语义
    //   （纯静态表）——函数式查询不设 NOTIFY。
    Q_INVOKABLE int paintingWidth(int index) const;
    Q_INVOKABLE int paintingHeight(int index) const;

    // t717 实体贴图覆盖（t727 夜行者 / t728 燃烬者 / t730 鱿鱼 / t731 皮肤 / t732 矿车·书的 pack 映射
    //   前置）：kind 字符串 key（"nightwalker"/"nightwalker_eyes"/"emberling"/"squid"/"minecart"/
    //   "enchant_book"/"skin_default"/"skin_alex"，entityKindMap 表）→ pack 启用且包内 entity 目录命中
    //   （子目录布局优先、扁平兜底，同 mobTextureSource 两级探测）时返 file:/// URL 供 QtQuick3D Texture
    //   加载；否则空串 → 调用方回退 qrc:/textures/entity_<kind>.png 程序自绘（tools/build_entities_pack.py；
    //   §9 改名：Enderman→Nightwalker 夜行者、Blaze→Emberling 燃烬者）。无映射 kind / active=false /
    //   无目录 / 文件缺 → ""。红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
    Q_INVOKABLE QString entitySource(const QString &kind) const;

    // t717/t718 盔甲 layer 贴图覆盖（玩家 + 人形 mob 护甲 3D 显示的 pack 映射）：tier（0 皮革/1 铁/2 铜/3 金/
    //   4 钻石/5 链甲——与 Hotbar::armorTier 同源序）+ layer（1=头盔+胸甲+护腿 / 2=靴，MC armor 两层）→
    //   pack 启用且包内 models/armor 目录有 <prefix>_layer_<n>.png 时返 file:/// URL（皮革 tier 命中时按
    //   retintLeatherTemplate 染棕后落盘返回——pack 皮革层是灰白可染色 base，t718 接）；否则空串 → 调用方
    //   回退 qrc:/textures/armor_<kind>_layer_<n>.png 程序层（tools/build_armor_layers.py 六档含铜（t718 补，
    //   铜无 pack 等价恒回退））。无映射 tier / layer 越界 / active=false / 无目录 / 文件缺 → ""。
    //   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
    Q_INVOKABLE QString armorLayerSource(int tier, int layer) const;

    // t421 生物模型贴图覆盖：pack 启用且 mobType 在「引擎 mob id → pack entity 子目录/文件名」映射内、且包内
    //   对应 PNG 存在时，返回 file:///<entityDir>/<mob>/<mob>.png 供 QtQuick3D Texture 直接加载（MobModel 据
    //   packTextured 把几何 UV 按 T 字展开进该贴图）；否则返空串 → Main.qml 各 mob delegate 回退程序生成
    //   mob_*.png（pig/cow/sheep/shambler/chicken）或纯色 baseColor（stalker/bones/spider）。映射取标准 MC 1.0
    //   entity 子目录命名（pig/cow/sheep/chicken/zombie/skeleton/creeper/spider），是功能性元数据（红线 §9 可随
    //   代码提交）；贴图文件本身仅运行期读本地 gitignored pack，不 bake 进 qrc/VCS。active=false / 无 entity 目录 /
    //   无映射 / 文件缺 → ""。子目录缺时自动回退扁平 entity/<mob>.png（兼容旧 / HD 包布局）。
    Q_INVOKABLE QString mobTextureSource(int mobType) const;

    // t633 图鉴生物列表 2D 头像图标：pack 启用且 mobType 有头部 box-UV 区（mobmodel.cpp 同源 texOffs/size 表）
    //   时，加载 pack entity 贴图、裁「头正面」（MC +Z Front 面 = (u0+d, v0+d)-(u0+d+w, v0+d+h) 像素矩形）
    //   放大到 64×64 透明底 PNG，落盘 AppLocalData/voxelsandbox_rp_mobhead_<mobType>.png（缓存；apply() 重建时
    //   随图集重生成）并返回 file:/// 路径。无 pack / 无映射 / 裁剪解码失败 → 空串（调用方回退体色方块）。
    //   羊特例：主贴图是 sheep_fur.png（毛层，头前是纯白羊毛无脸）→ 头像改裁 sheep/sheep.png 本体层的头区
    //   （有真脸）。雪傀儡头是南瓜方块（非 entity 贴图）/ 鱿鱼·狼·豹猫·银鱼无映射 → 空串回退。
    //   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。
    Q_INVOKABLE QString mobHeadIconSource(int mobType) const;

    // 引擎图集瓦片尺寸（HD 图集 t668：16 → 64）。读 BlockRegistry::kAtlasTilePx **单一权威**（与 build_atlas.py
    //   TILE / chunkgeometry hx,hy / BlockCube kHx,kHy 四方同源——消除「瓦片尺寸魔数多份、改一处漏一份」回归类；
    //   blockregistry.h kAtlasTilePx 注释钉死四方）。包内贴图（常 128px）→ 64 降采样平滑；程序 16px → 64 近邻
    //   上采样无损失感。UV 数学按瓦片数分数（模型 UV 1/AtlasTileCount）不随像素尺寸变；只有半纹素内缩随之变。
    //   公开供 image provider / main.cpp 复用。
    static constexpr int kTile = BlockRegistry::kAtlasTilePx;

    // 合成图集（程序生成图集 + 包覆盖）。幂等首调构建并缓存；运行期经 apply() 重建。供 image provider 调用。
    static QImage compositeAtlas();
    // 包是否启用且存在（与 active() 同源；ensureBuilt 后稳定）。供 image provider / main 判定。
    static bool packActive();
    // t456 方块 item 图标覆盖（静态，供 Hotbar 等 Game 层无实例调用）：pack 启用且 blockId 在「方块→pack item/
    //   前贴图文件名」映射内（现 CraftingTable/Furnace/16 色床/木梯）、且包内 PNG 存在时（item 目录优先、block 目录兜底），
    //   返 file:// URL；否则空串 → Hotbar::iconSourceForBlock 回退程序生成 icon_<block>.png。工作台/熔炉此前三轮
    //   反复（t456 加 → 一轮撤 → t492 恢复 → t518 撤要 3D），t537 回退到 t492 的 2D pack 图（用户否决 3D「一坨」），
    //   候选 item/<name>.png 优先、block/<name>_front.png 兜底（用户后续提供 item PNG 直接替代）。仅 2D 物品图标
    //   路径消费；3D 手持立方 / 掉落物走 BlockCube+voxelAtlas 另一路径，不受影响。
    //   红线 §9：仅运行期读本地 gitignored pack PNG，不 bake 进 qrc/VCS。active=false / 无映射 / 文件缺 → ""。
    static QString blockItemIconSource(int blockId);

signals:
    void activeChanged();   // active 或 atlasSource（revision）变（驱动 QML Texture 重载）
    void configChanged();   // enabled / packPath 变（驱动设置 UI 刷新；不立即重建图集）
    // t585 指南针/钟动画帧切换（updateAnimatedItemState 检出帧 index 变化；4Hz 节流下有效变化 ≤4Hz）。
    void animRevisionChanged();

private:
    bool m_active = false;
    // t585 动画帧修订号（进程级态存 BuiltState，实例成员仅镜像 + 广播，同 m_active/apply 模式）。
    int m_animRevision = 0;
};

#endif // RESOURCEPACKMANAGER_H
