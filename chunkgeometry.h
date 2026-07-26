#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>

#include <QtQml/qqml.h>

#include "world.h" // Q_PROPERTY(World*) 需要 World 完整定义（moc 指针元类型要求）

// 体素区块几何（纯视图）：从注入的 World 取体素，做立方体面剔除（culled meshing）
// + 单一纹理图集 + per-face UV。只生成「邻居为空气」的可见面；不再自己持有体素/Perlin。
//
// 方块 id 与每面瓦片映射见 BlockRegistry（单一权威）；本类只读它做网格化。
// 图集瓦片顺序（须与 tools/build_atlas.py 一致）：
//   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
//   5=cobble 6=log_top 7=log_side 8=planks 9=leaves（t12 重建图集后生效）
class ChunkGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChunkGeometry)
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)

public:
    explicit ChunkGeometry(QQuick3DObject *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);

signals:
    void worldChanged();

private:
    void buildMesh();                       // 面剔除 + 写入 QQuick3DGeometry
    int tileFor(quint8 block, int face) const;
    quint8 blockAt(int x, int y, int z) const { return m_world ? m_world->blockAt(x, y, z) : quint8(0); }

    World *m_world = nullptr;
};

#endif // CHUNKGEOMETRY_H
