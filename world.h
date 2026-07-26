#ifndef WORLD_H
#define WORLD_H

#include <QObject>
#include <QVector>
#include <QtQml/qqml.h>

#include <vector>

// 体素世界（单一数据源）：Perlin fBm 生成地形，被网格(ChunkGeometry)与
// 物理(PlayerController)共同查询 —— 二者读同一份栅格，保证「看得见的方块=碰得到的方块」。
//
// 方块 id（定义见 BlockRegistry）：0=air 1=grass 2=dirt 3=stone 4=cobble
// 5=log 6=planks 7=leaves 8=sand。+Y 朝上。
// 线性索引 x + width*(z + depth*y)（y 最慢），越界 blockAt 返回 0(空气)。
class World : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(World)
    Q_PROPERTY(int width  READ width  WRITE setWidth  NOTIFY widthChanged)
    Q_PROPERTY(int depth  READ depth  WRITE setDepth  NOTIFY depthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(int seed   READ seed   WRITE setSeed   NOTIFY seedChanged)

public:
    explicit World(QObject *parent = nullptr);

    int width() const  { return m_width; }
    int depth() const  { return m_depth; }
    int height() const { return m_height; }
    int seed() const   { return m_seed; }
    void setWidth(int w);
    void setDepth(int d);
    void setHeight(int h);
    void setSeed(int s);

    // 越界返回 0（空气）。网格与物理都用它。
    Q_INVOKABLE quint8 blockAt(int x, int y, int z) const;
    Q_INVOKABLE bool isSolid(int x, int y, int z) const { return blockAt(x, y, z) != 0; }

    // 写入栅格并标记脏（当前单 chunk = 整个 mesh 视为脏）。越界 / 无变化返回 false。
    // 成功改动后发 blockBroken/blockPlaced（语义事件，供 t14 粒子 / t11 音效消费）
    // 与 worldChanged（驱动 ChunkGeometry 重建整个单 mesh）。
    Q_INVOKABLE bool setBlock(int x, int y, int z, quint8 id);

signals:
    void widthChanged();
    void depthChanged();
    void heightChanged();
    void seedChanged();
    void worldChanged(); // 生成/编辑后发出 → 网格重建
    // 编辑语义事件（id：broken 带被破的原方块 id；placed 带新放方块 id）。
    void blockBroken(int x, int y, int z, int id);
    void blockPlaced(int x, int y, int z, int id);

private:
    void generate(); // 填充 m_voxels（静默，不 emit）
    double noise2(double x, double z) const;
    double fbm(double x, double z) const;
    int heightAt(int x, int z) const;

    QVector<quint8> m_voxels;
    std::vector<int> m_perm; // 512 置换表
    int m_width = 16, m_depth = 16, m_height = 16, m_seed = 1337;
};

#endif // WORLD_H
