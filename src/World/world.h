#ifndef WORLD_H
#define WORLD_H

#include <QObject>
#include <QtGlobal> // quint32（hashColumn 确定性哈希返回类型）
#include <QtQml/qqml.h>

#include <vector>

#include "chunkmanager.h" // 内部多 chunk 存储（World 层，不外泄到 QML）

// 体素世界（QML façade + 单一数据源）：内部由 ChunkManager 持一片 chunk 列网格路由（本回合
// 3×3=9 chunk，世界 48×48×16）；Perlin fBm 生成地形，被网格(ChunkGeometry)与物理
// (PlayerController)共同查询 —— 二者读同一份栅格，保证「看得见的方块=碰得到的方块」。
//
// 对外 QML API：blockAt/isSolid/setBlock/width/depth/height/seed/worldChanged/blockBroken/
// blockPlaced（与单 chunk 版完全一致；Main.qml 仅改 width/depth=48）。多 chunk 化是实现细节，
// 不外泄（PLAN §2 不变量 J：单层 chunk 抽象，不引入 chunklet）。
//
// 方块 id（定义见 BlockRegistry）：0=air 1=grass 2=dirt 3=stone 4=cobble
// 5=log 6=planks 7=leaves 8=sand。+Y 朝上。世界坐标越界 blockAt 返回 0(空气)。
class World : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(World)
    Q_PROPERTY(int width  READ width  WRITE setWidth  NOTIFY widthChanged)
    Q_PROPERTY(int depth  READ depth  WRITE setDepth  NOTIFY depthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(int seed   READ seed   WRITE setSeed   NOTIFY seedChanged)
    // chunk 列网格尺寸（= ceil(width/16)、ceil(depth/16)）；暴露给 Renderer/QML，供 t03 每
    // chunk mesher 的 Repeater 决定 Model 数量、t10 F3 计数。仅随 width/depth 变化。
    Q_PROPERTY(int chunksX READ chunksX NOTIFY widthChanged)
    Q_PROPERTY(int chunksZ READ chunksZ NOTIFY depthChanged)

public:
    explicit World(QObject *parent = nullptr);

    int width() const  { return m_width; }
    int depth() const  { return m_depth; }
    int height() const { return m_height; }
    int seed() const   { return m_seed; }
    int chunksX() const { return m_chunks.chunksX(); }
    int chunksZ() const { return m_chunks.chunksZ(); }
    void setWidth(int w);
    void setDepth(int d);
    void setHeight(int h);
    void setSeed(int s);

    // 越界返回 0（空气）。跨 chunk 由 ChunkManager 路由；网格与物理都用它。
    Q_INVOKABLE quint8 blockAt(int x, int y, int z) const;
    Q_INVOKABLE bool isSolid(int x, int y, int z) const { return blockAt(x, y, z) != 0; }

    // 写栅格的唯一入口（PLAN §2-C 精神：当前 GUI 线程单写者）。经 ChunkManager 跨 chunk 写入 +
    // 标目标 chunk 脏；该格贴 chunk 边沿（x/z 在 16 边）→ 同标邻接 chunk 脏（为 t03 跨边界剔除）。
    // 越界 / 无变化返回 false。成功改动后发 blockBroken/blockPlaced（语义事件，供 t14 粒子 / t11 音效）
    // 与 worldChanged（驱动 ChunkGeometry 重建；本回合仍重建整个单 mesh，mesher 拆分归 t03）。
    Q_INVOKABLE bool setBlock(int x, int y, int z, quint8 id);

    // 暴露内部 chunk 网格给 Renderer/Game 层（只读引用；t03 per-chunk mesher、t10 F3 计数用）。
    const ChunkManager &chunks() const { return m_chunks; }

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
    void generate();          // 重建置换表 + ChunkManager + 填充地形（静默，不 emit）
    void buildPermutation();  // 由 seed 填 512 置换表（线性同余，可复现）
    double noise2(double x, double z) const;
    double fbm(double x, double z) const;
    int heightAt(int x, int z) const;

    // 确定性树木生成（PLAN §2-K）：在 generate() 末段于 grass 表层种橡树（原木主干+树叶球冠）。
    // 位置/形状纯由 seed 决定；禁用任何运行期随机源（QTime/时钟/全局 RNG）。
    void placeTrees();                                  // 遍历列、密度+间距筛选后散布
    // 单棵树：主干 trunkH 格 + 树冠。leafRand = 该列哈希的高位，驱动树冠四角叶的有无 → 每棵树冠轮廓
    // 各异（贴近 MC 橡树自然参差）。纯由 seed 派生（确定性，PLAN §2-K）。
    void placeTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand);
    // 确定性矿石散布（t84，PLAN §2-K）：地形填充后遍历 stone 区段，按 hashVoxel(seed,x,y,z)
    // 决定是否替换为煤矿/铁矿。仅替换 Stone；同 seed → 同矿脉分布；禁用任何运行期随机源。
    void scatterOres();
    void setVoxelIfAir(int x, int y, int z, quint8 id);       // 仅写空气格（树冠不覆盖主干/地形）
    quint32 hashColumn(int seed, int x, int z) const;         // 整数哈希（列级 seed/x/z）→ 确定性伪随机
    quint32 hashVoxel(int seed, int x, int y, int z) const;   // 整数哈希（体素级 seed/x/y/z）→ 矿石散布用

    std::vector<int> m_perm;  // 512 置换表（Perlin）
    int m_width = 16, m_depth = 16, m_height = 16, m_seed = 1337;
    ChunkManager m_chunks;    // 多 chunk 存储 + 跨 chunk 路由（World 层；默认空，generate 重建）
};

#endif // WORLD_H
