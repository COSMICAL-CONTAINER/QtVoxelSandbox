#ifndef RESOURCEPACKMANAGER_H
#define RESOURCEPACKMANAGER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QList>
#include <QPair>
#include <QtQml/qqml.h>

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
// 合成图集经 QQuickImageProvider（main.cpp 注册 "rp" provider）以 image://rp/atlas 暴露给 QML；
// active=false（无包 / 被禁用）时 atlasSource 回退 qrc:/textures/atlas.png（程序生成默认）。
//
// t415 运行期开关：enabled / packPath 为 QML 可写属性（持久化 settings.json）；apply() 触发重新解析 +
// 重建合成图集并刷新 atlasSource（带 revision 查询串 bust QML image cache）→ 用户在设置面板切换 /
// 改路径后即时生效，无需重启。合成状态由进程全局 mutex 保护（image provider 可能从渲染线程拉图，
// apply 从 GUI 线程重建 → 互斥防 QImage 竞态）。
//
// 红线（PLAN §9）：本类只读取本地 / gitignored 路径的包 PNG，绝不把任何 MC 资产 bake 进 qrc 或提交进 VCS。
// 映射表（引擎 tile → 标准贴图文件名）是功能性元数据，可随代码提交；纹理文件本身不进仓库。引擎默认在
// 无包时仍以程序生成图集正常工作。
//
// 分层（PLAN §2）：Core 叶子工具，只依赖 Qt（Core/Gui），不 include World/Renderer/Game。被 Main.qml
// （呈现层）实例化（QML_NAMED_ELEMENT 门面），合成图集经静态方法 compositeAtlas() 被 main.cpp 注册的
// image provider 访问（QtQuick 依赖留在 main.cpp 的 app 胶水层，Core 不沾 QtQuick）。
class ResourcePackManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ResourcePackManager)
    // 资源包是否启用且存在（启动期解析，运行期经 apply() 重建）。active=false → QML 用 qrc 程序生成图集。
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // 地形图集贴图源 URL：active → image://rp/atlas?<rev>（合成图集，rev 变即 bust QML 缓存重载）；
    // 否则 qrc:/textures/atlas.png（程序生成默认）。
    Q_PROPERTY(QString atlasSource READ atlasSource NOTIFY activeChanged)
    // t415 资源包总开关（镜像 settings.json resourcePackEnabled，缺省 true）。setter 立即持久化；
    // 配合 apply() 即时重建图集（也可仅持久化等下次重启生效）。
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configChanged)
    // t415 资源包路径（镜像 settings.json resourcePack，空 = 走环境变量/默认探查）。setter 立即持久化。
    Q_PROPERTY(QString packPath READ packPath WRITE setPackPath NOTIFY configChanged)

public:
    explicit ResourcePackManager(QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString atlasSource() const;

    bool enabled() const;
    void setEnabled(bool e);
    QString packPath() const;
    void setPackPath(const QString &p);
    // t415 应用当前 enabled/packPath：重新解析包 + 重建合成图集 + 刷新 atlasSource（cache-bust）。
    Q_INVOKABLE void apply();

    // 引擎图集瓦片尺寸（tools/build_atlas.py TILE=16 + chunkgeometry UV 的 N*16 同源；公开供 image provider 复用）。
    static constexpr int kTile = 16;

    // 合成图集（程序生成图集 + 包覆盖）。幂等首调构建并缓存；运行期经 apply() 重建。供 image provider 调用。
    static QImage compositeAtlas();
    // 包是否启用且存在（与 active() 同源；ensureBuilt 后稳定）。供 image provider / main 判定。
    static bool packActive();

signals:
    void activeChanged();   // active 或 atlasSource（revision）变（驱动 QML Texture 重载）
    void configChanged();   // enabled / packPath 变（驱动设置 UI 刷新；不立即重建图集）

private:
    bool m_active = false;
};

#endif // RESOURCEPACKMANAGER_H
