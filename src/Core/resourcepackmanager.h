#ifndef RESOURCEPACKMANAGER_H
#define RESOURCEPACKMANAGER_H

#include <QImage>
#include <QObject>
#include <QString>
#include <QList>
#include <QPair>
#include <QtQml/qqml.h>

// Resource-pack loader core（t414，phase 1：方块贴图覆盖）。
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
    // 资源包是否启用且存在（启动期解析，phase 1 不在运行期切换）。active=false → QML 用 qrc 程序生成图集。
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // 地形图集贴图源 URL：active → image://rp/atlas（合成图集）；否则 qrc:/textures/atlas.png（程序生成默认）。
    Q_PROPERTY(QString atlasSource READ atlasSource NOTIFY activeChanged)

public:
    explicit ResourcePackManager(QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString atlasSource() const;

    // 引擎图集瓦片尺寸（tools/build_atlas.py TILE=16 + chunkgeometry UV 的 N*16 同源；公开供 image provider 复用）。
    static constexpr int kTile = 16;

    // 合成图集（程序生成图集 + 包覆盖）。ensureBuilt 幂等（首调构建并缓存）。供 image provider 调用。
    static QImage compositeAtlas();
    // 包是否启用且存在（与 active() 同源；ensureBuilt 后稳定）。供 image provider / main 判定。
    static bool packActive();

signals:
    void activeChanged();

private:
    // 幂等构建：解析包路径 + 合成图集，结果存文件局部缓存。仅 GUI 线程启动期调用。
    static void ensureBuilt();
    // 「引擎 tile 索引 → 包内标准贴图文件名」映射（主地形方块）。缺失文件名由 loader 安全跳过 = 不覆盖。
    static const QList<QPair<int, QString>> &tileFilenameMap();

    bool m_active = false;
};

#endif // RESOURCEPACKMANAGER_H
