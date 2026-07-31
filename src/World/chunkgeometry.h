#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QVector3D>

#include <QtQml/qqml.h>

#include "world.h" // Q_PROPERTY(World*) + chunks() 路由（World 层只读）

// 体素区块几何（纯视图，per-chunk，t03）：每个 ChunkGeometry 负责一个 chunk（cx,cz）的
// 局部 culled meshing。从注入的 World（经 blockAt 跨 chunk 路由）取体素 + 邻居判定，
// 只生成「邻居为空气」的可见面。顶点为 chunk 局部坐标；QML 把 Model 摆到 chunk 世界起点
// (cx*16, 0, cz*16) 完成世界定位。
//
// dirty 驱动重建（dev-spec t03 验收）：setBlock 经 ChunkManager 标目标 + 边界邻接 chunk 脏；
// worldChanged → onWorldChanged() 检 myChunk()->dirty()，**仅脏 chunk 重建并清脏**，非脏
// chunk 不重建（rebuild 次数 = dirty chunk 数）。跨 chunk 边界面剔除走 world.blockAt
//（相邻两 chunk 实体→共边面剔除无夹层；一侧空气→画出；越界=空气）→ 3×3 无缝。
//
// 分层（PLAN §2）：本类属 Renderer，**只读** World/ChunkManager（blockAt/isSolid + dirty 标记），
// 不反向写栅格。不变量 B 形：mesh 数据 own/move-only/不可变（为后续线程化留形）。
//
// 方块 id 与每面瓦片映射见 BlockRegistry（单一权威）；本类只读它做网格化。
// 图集瓦片顺序（须与 tools/build_atlas.py 一致）：
//   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
//   5=cobble 6=log_top 7=log_side 8=planks 9=leaves（N=10）
class ChunkGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChunkGeometry)
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)
    Q_PROPERTY(int cx READ cx WRITE setCx NOTIFY cxChanged)
    Q_PROPERTY(int cz READ cz WRITE setCz NOTIFY czChanged)
    // t123 动态太阳光照：太阳方向（单位向量，指向太阳）由 WorldClock 派生、经 QML 绑定注入。
    //   mesher 据此把 faceNormal·sunDir 烘进顶点 color.rgb（配合 heightmap 列投影阴影）。
    //   分层（PLAN §2）：本类只接收「裸 QVector3D」（不 include worldclock.h、不依赖 Game 层
    //   时间源），保持 Renderer→向下 依赖方向。设值即触发 buildMesh（绕过 chunk dirty：体素未变、
    //   仅光照变）。WorldClock 量化步进 → sunChanged → 本 setter → 重建（约 16s/步 正常、0.4s/步 调试）。
    Q_PROPERTY(QVector3D sunDir READ sunDir WRITE setSunDir NOTIFY sunInputChanged)
    // t148 水渲染分流：waterOnly=true → 本几何只网格化 Water 方块（独立透明段，Main.qml 用 opacity=0.7
    //   材质渲染）；waterOnly=false（默认）→ 只网格化非水方块（地形 / 异形 / ...，跳过 Water，避免与水段
    //   重复绘制 + 水被当不透明地形误渲）。两段共用同一 culled meshing 主体 + 顶点色光照管线，仅：
    //   (a) 选块（水 vs 非水）；(b) 邻居面剔除规则不同（水段额外剔 nb==Water，水-水面互剔；见 .cpp）。
    //   一个 chunk 由两个 ChunkGeometry 实例渲染（地形段 + 水段），各自绑 QML Model + 材质。
    Q_PROPERTY(bool waterOnly READ waterOnly WRITE setWaterOnly NOTIFY waterOnlyChanged)
    // 网格统计（t10 F3 调试叠层，PLAN §2-F）：buildMesh 完成后暴露本 chunk 的顶点 / 三角面数，
    // 供 F3 叠层汇总诊断 meshing 吞吐与帧抖根因（§2-F 明言「没有 F3 叠层，帧率验收无法诊断帧抖」）。
    // 仅在 buildMesh 末尾经 meshRebuilt 通知；呈现层只读、不反向写。三角面 = idx/3（实际索引计数
    // 派生，不依赖内部「每可见面 4 顶点 + 6 索引」约定，将来换贪婪网格化仍正确）。
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY meshRebuilt)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY meshRebuilt)

public:
    explicit ChunkGeometry(QQuick3DObject *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);

    int cx() const { return m_cx; }
    void setCx(int cx);
    int cz() const { return m_cz; }
    void setCz(int cz);

    // t123 太阳方向（mesher 据此烘顶点光方向调制 + 投影阴影）。
    QVector3D sunDir() const { return m_sunDir; }
    void setSunDir(const QVector3D &dir);

    // t148 水渲染分流（见 Q_PROPERTY 注释）：true=只网格化 Water 段。
    bool waterOnly() const { return m_waterOnly; }
    void setWaterOnly(bool on);

    // 网格统计（t10 F3 叠层）：上次 buildMesh 产出的顶点 / 三角面数。
    int vertexCount() const { return m_vertexCount; }
    int triangleCount() const { return m_triangleCount; }

signals:
    void worldChanged();
    void cxChanged();
    void czChanged();
    void sunInputChanged(); // t123：sunDir 变（太阳量化跨步）；驱动呈现层 / 未来光场刷新
    void waterOnlyChanged(); // t148：水段开关变（QML 改 waterOnly → 重建，水段 / 地形段重网格化）
    // buildMesh 完成（顶点 / 三角面数已更新；t10 F3 叠层据此刷新汇总）。
    void meshRebuilt();

private:
    void onWorldChanged();            // worldChanged 槽：仅 dirty chunk 才重建
    void buildMesh();                 // 局部 culled mesh + 写入 QQuick3DGeometry + 清脏
    int tileFor(quint8 block, int face) const;
    Chunk *myChunk() const;           // 本几何负责的 chunk（world/cx/cz 无效 → nullptr）
    // 世界坐标查询（跨 chunk 经 world.blockAt 路由 → 边界面剔除正确）
    quint8 blockAtWorld(int wx, int wy, int wz) const {
        return m_world ? m_world->blockAt(wx, wy, wz) : quint8(0);
    }
    // t133：世界坐标 state 查询（异形方块朝向/开合；经 world.stateAt 跨 chunk 路由）。
    //   常规方块 / 越界 → 0。PartialBlockGeometry::append 据此选朝向变体。
    quint8 stateAtWorld(int wx, int wy, int wz) const {
        return m_world ? m_world->stateAt(wx, wy, wz) : quint8(0);
    }

    World *m_world = nullptr;
    int m_cx = -1; // -1 = 未赋值（myChunk 返回 nullptr，待 QML 赋 cx/cz 后才建）
    int m_cz = -1;
    QVector3D m_sunDir{0.f, 1.f, 0.f}; // t123 太阳方向（单位向量；默认天顶正午，QML 绑 WorldClock.sunDir）
    bool m_waterOnly = false; // t148：true=只网格化 Water 段（透明水）；false=只网格化非水地形段
    int m_vertexCount = 0;   // 上次 buildMesh 的顶点数（t10 F3 叠层汇总）
    int m_triangleCount = 0; // 上次 buildMesh 的三角面数（idx.size()/3）
};

#endif // CHUNKGEOMETRY_H
