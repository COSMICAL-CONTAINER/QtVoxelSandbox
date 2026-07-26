#ifndef CHUNKGEOMETRY_H
#define CHUNKGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>

#include <QVector>
#include <QtQml/qqml.h>
#include <vector>

// 体素区块几何：Perlin fBm 生成地形 → 立方体面剔除（culled meshing）→
// 单一纹理图集（textures/atlas.png，5 瓦片横排）+ per-face UV。
//
// 关键优化（Minecraft 做法）：只生成「邻居为空气」的可见面，把面数从
// 每方块 6 面压到平均 ~1-2 面；整区块一个 Model / 一个材质 / 一次 draw call。
// 替代了之前「每个 #Cube 实例画满 6 面」导致 10-20fps 的方案。
//
// 方块 id：0=空气, 1=草, 2=土, 3=石, 4=沙。
// 图集瓦片顺序（须与 tools/build_atlas.py 一致）：
//   0=grass_top, 1=grass_side, 2=dirt, 3=stone, 4=sand。
class ChunkGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChunkGeometry)
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(int depth READ depth WRITE setDepth NOTIFY depthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(int seed READ seed WRITE setSeed NOTIFY seedChanged)

public:
    explicit ChunkGeometry(QQuick3DObject *parent = nullptr);

    int width() const { return m_width; }
    int depth() const { return m_depth; }
    int height() const { return m_height; }
    int seed() const { return m_seed; }
    void setWidth(int w);
    void setDepth(int d);
    void setHeight(int h);
    void setSeed(int s);

signals:
    void widthChanged();
    void depthChanged();
    void heightChanged();
    void seedChanged();

private:
    void regenerate();            // 生成置换表 + 填充体素 + 重建网格
    void buildMesh();             // 面剔除 + 写入 QQuick3DGeometry
    double noise2(double x, double z) const;
    double fbm(double x, double z) const;
    int heightAt(int x, int z) const;
    quint8 blockAt(int x, int y, int z) const; // 体素值（越界 = 0 空气）
    int tileFor(quint8 block, int face) const; // 该面用哪个图集瓦片

    // 体素栅格，索引 [x + width*(z + depth*y)]，0=空气
    QVector<quint8> m_voxels;
    int m_width = 16;
    int m_depth = 16;
    int m_height = 16;
    int m_seed = 1337;
    std::vector<int> m_perm;      // 512 置换表
};

#endif // CHUNKGEOMETRY_H
