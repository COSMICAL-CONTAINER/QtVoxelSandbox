#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>

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

public:
    explicit ChunkGeometry(QQuick3DObject *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);

    int cx() const { return m_cx; }
    void setCx(int cx);
    int cz() const { return m_cz; }
    void setCz(int cz);

signals:
    void worldChanged();
    void cxChanged();
    void czChanged();

private:
    void onWorldChanged();            // worldChanged 槽：仅 dirty chunk 才重建
    void buildMesh();                 // 局部 culled mesh + 写入 QQuick3DGeometry + 清脏
    int tileFor(quint8 block, int face) const;
    Chunk *myChunk() const;           // 本几何负责的 chunk（world/cx/cz 无效 → nullptr）
    // 世界坐标查询（跨 chunk 经 world.blockAt 路由 → 边界面剔除正确）
    quint8 blockAtWorld(int wx, int wy, int wz) const {
        return m_world ? m_world->blockAt(wx, wy, wz) : quint8(0);
    }

    World *m_world = nullptr;
    int m_cx = -1; // -1 = 未赋值（myChunk 返回 nullptr，待 QML 赋 cx/cz 后才建）
    int m_cz = -1;
};

#endif // CHUNKGEOMETRY_H
