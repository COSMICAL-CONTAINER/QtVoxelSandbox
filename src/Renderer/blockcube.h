#ifndef BLOCKCUBE_H
#define BLOCKCUBE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QVector3D>
#include <QtQml/qqml.h>

#include "blockregistry.h" // tileIndex → per-face 图集 UV（Renderer 读 World 数据，同 chunkgeometry）

class World; // Q_PROPERTY World*（forward；实现 .cpp include world.h，Renderer→World 向下依赖合规）

// 带贴图的单位立方体几何（1×1×1，居中 ±0.5，Triangles，per-face 图集 UV）。
//
// 用途（t35 方块掉落实体）：item entity 的小方块图标 —— 用图集贴图还原被破方块的外观
// （草顶 / 草侧 / 圆石各面 …）。复用 BlockRegistry::tileIndex 的 per-face 瓦片映射（与
// chunkgeometry 同一权威），故掉落实体看上去就是「缩小的方块」。
//
// 与既有几何的区别：
//   - UnitCube：仅 pos（玩家模型 / 手用纯色材，不采样贴图）；
//   - CrackBox：每面全幅 UV（0..1，铺整张裂纹贴图）；
//   - 本类 BlockCube：每面 UV 按方块 + 面查图集瓦片序号取子区（半纹素内缩防渗色，同
//     chunkgeometry），故 6 面可显示不同 tile（草顶 vs 草侧）。
// 顶点 24（每面 4 角独立，便于 per-face UV），索引 36（12 三角形）；CCW 朝外（默认 backface
// 剔除下，外法线面可见）。
//
// t257 掉落沙光影：顶点色光照接入。设 world + worldPos 后，rebuild 据方块世界位采样 World 的
//   天光 / 方块光（每面取其外侧邻格）+ PCF 软影（每顶点沿 sunDir 步进 heightmap）—— 与 chunkgeometry
//   立方面顶点色公式逐字同源（共用 voxellight.h 的 VoxelLight::vertexLight）。QML 材质开
//   vertexColorsEnabled:true 即让掉落沙呈现与地形一致的明暗（洞穴暗 / 阴影处暗 / 日中亮），修
//   「沙掉落时变亮 / 暗处挖底沙→掉落沙明显变亮」根因（原 BlockCube 无顶点色通道，恒 vertexColor=1.0）。
//   不设 world（worldPos 无效）→ 顶点色恒白 1.0（保 item entity / 第一·三人称手持 / 热栏 HUD 图标
//   全亮既有行为，lessons-learned t144「掉落物浮空无天光遮蔽」）。
//
// blockId 是 Q_PROPERTY：QML 据 entity 的 itemId 设它；setter 触发 rebuild（重算每面 UV 后
// 整几何重传 GPU）。顶点位置恒定（±0.5 立方体），只 UV / 顶点色随 blockId / worldPos / sunDir 变。
//
// 为何不用内置 #Cube（spec 字面建议）：本工程实测**静态 source:"#Cube" Model 不渲染**
// （t31 诊断结论；粒子 instanced #Cube 可见、静态 #Cube 不可见），故带贴图的静态方块
// 走自定义 QQuick3DGeometry + NoLighting 这条已验证可见路径（同 UnitCube / CrackBox）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只读 BlockRegistry + World（World 层），
// 不反向写。依赖只向下。
class BlockCube : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BlockCube)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)
    // t257 掉落沙光影（可选）：设 world + worldPos 后，rebuild 据世界位采样光场 + PCF 软影烘顶点色。
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)
    Q_PROPERTY(QVector3D worldPos READ worldPos WRITE setWorldPos NOTIFY worldPosChanged)
    Q_PROPERTY(QVector3D sunDir READ sunDir WRITE setSunDir NOTIFY sunDirChanged)
    Q_PROPERTY(bool shadowsEnabled READ shadowsEnabled WRITE setShadowsEnabled NOTIFY shadowsEnabledChanged)
    // PLAN §2-H 夜间火把发光修复：昼夜天光乘子（仅乘天光分量；QML 绑 terrainLight(skyLight)）。掉落沙 BlockCube
    //   设 world 后采 flood-fill 光场（含 block 分量）→ 必须同步 chunkgeometry 把 dayMul 烘进天空分量、保持
    //   「掉落沙与地形同亮度曲线」成对契约（lessons-learned t257）。block 项保留时间不变 → 掉在火把旁的沙夜间不变暗。
    Q_PROPERTY(float dayMul READ dayMul WRITE setDayMul NOTIFY dayMulChanged)

public:
    explicit BlockCube(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);

    // t257 光照采样上下文（同 chunkgeometry 语义；不设 world → 顶点色恒白）。
    World *world() const { return m_world; }
    void setWorld(World *w);
    QVector3D worldPos() const { return m_worldPos; }
    void setWorldPos(const QVector3D &p);
    QVector3D sunDir() const { return m_sunDir; }
    void setSunDir(const QVector3D &dir);
    bool shadowsEnabled() const { return m_shadowsEnabled; }
    void setShadowsEnabled(bool on);
    // PLAN §2-H dayMul（昼夜天光乘子，仅乘天光分量；见 Q_PROPERTY 注释）。
    float dayMul() const { return m_dayMul; }
    void setDayMul(float m);

signals:
    void blockIdChanged();
    void worldChanged();
    void worldPosChanged();
    void sunDirChanged();
    void shadowsEnabledChanged();
    void dayMulChanged();

private:
    void rebuild(); // 顶点位置恒定；按 m_blockId 重算每面 UV；据 world+worldPos 烘顶点色后整几何重传。

    int m_blockId = int(BlockRegistry::Stone); // 默认石头（合法非空，防未设 blockId 时空 UV）
    World *m_world = nullptr;        // t257：null → 顶点色恒白 1.0（item entity / 手持 / HUD 既有全亮行为）
    QVector3D m_worldPos;             // t257：方块世界中心（posAt 给的 (x+0.5,y+0.5,z+0.5)；占格 = floor(worldPos)）
    QVector3D m_sunDir{0.f, 1.f, 0.f};// t257：太阳方向单位向量（同 chunkgeometry 默认天顶正午）
    bool m_shadowsEnabled = true;     // t257：PCF 软影开关（false → 跳过软影，仅光场基底）
    float m_dayMul = 1.0f;            // PLAN §2-H：昼夜天光乘子（仅乘天光分量；默认 1.0=正午全日照）
};

#endif // BLOCKCUBE_H
