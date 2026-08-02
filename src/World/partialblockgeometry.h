#ifndef PARTIALBLOCKGEOMETRY_H
#define PARTIALBLOCKGEOMETRY_H

#include <QtGlobal> // quint8 / quint32
#include <QVector>  // append() 的输出容器

#include "blockregistry.h" // FirstPartial 哨兵 + tileIndex（mesher 只读 World/Core 数据）

// chunk 顶点格式（pos3 + normal3 + uv2 + color4 rgba = 12 float = 48 字节）。
// 由 chunkgeometry.cpp（1×1×1 立方面）与 PartialBlockGeometry::append（异形方块）共用，二者合批进
// 同一 chunk mesh 的同一顶点缓冲。color.rgb 承载 t151 真光场（per-voxel flood-fill 天光 + 火把方光，
// max(sky,block)/15），a 恒 1.0。
//
// 注：QtQuick3D 的 ColorSemantic 按官方示例（quick3d/custommorphing/morphgeometry.cpp）按 vec4（RGBA）
//   读取——Attribute 无 componentCount 字段（语义→分量数由运行期固定表派生），若只写 3 float，第 4 分量
//   会越界读到下一顶点 / 缓冲尾外。故 color 用 4 float（a=1.0），stride = 48 字节。最终色 =
//   baseColor × vertexColor × 贴图（见 Main.qml）。
struct Vtx {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

// 逐块光场上下文（t151 真光场；由 chunkgeometry::buildMesh 按本块算好后传入，append 把它直接作各面顶点色）。
//   light = 本格光场值 max(sky,block)/15（已 clamp 到 [kVcMin, 1]）；异形方块各面共用此值（近似：异形
//   小体面所朝向的「邻格」在子格尺度上歧义，故取本格光场；异形格非遮光 → 光场已 flood 反映周围）。
//   替代 t123 方向太阳 faceVc（faceNormal·sunDir + heightmap 列投影）—— 方向阴影留 t153 PCF 软影。
struct PartialLightCtx {
    float light;  // 本格光场值 [0,1]（max(sky,block)/15，已 clamp）；异形方块各面顶点色共用
};

// 逐块水平邻居上下文（t209 栅栏连接）：4 向水平邻居的方块 id（由 chunkgeometry::buildMesh 查好后传入）。
//   仅 fence 用（栅栏横档是否画取决于邻格是否为栅栏 / 实体方块）；其余异形方块忽略本结构。
//   纯数据 POD；越界邻居（chunk 边界外）由 chunkgeometry 经 world.blockAt 跨 chunk 路由取真值（同面剔除
//   邻居查询路径），故栅栏连接跨 chunk 边界亦正确（边界格破/放已标邻 chunk 脏，触发邻居栅栏重网格化）。
struct PartialNeighborCtx {
    quint8 posX, negX, posZ, negZ;  // +X / -X / +Z / -Z 水平邻居方块 id（air=0；越界=0=空气）
};

// 不完整方块异形几何（t133 基础设施）：为 slab/stairs/fence/door/trapdoor/pressure plate 等「非
// 1×1×1 整立方」的方块生成异形顶点，**合批进同一 chunk mesh**（复用 chunkgeometry 顶点色光照管线 +
// 单 draw call），而非为每个异形方块另起 Model（lessons-learned t03「大网格不要用 QML Repeater 重复
// 声明；走 C++ 侧 mesh/节点管理」）。
//
// 接口：append(verts, idx, lx, ly, lz, blockId, state, light, nb, tileW, hx, hy, v0, v1) —— 把 (blockId, state)
//   对应的异形方块顶点 push 进 verts/idx（顶点位置 = (lx,ly,lz) + 局部偏移，与 chunkgeometry 立方面
//   同坐标系：block-local [0,1]^3 + 格原点）。light 携带逐块光照上下文，append 按各面外法线算 vc
//   （与 chunkgeometry 立方面同公式，复用顶点色光照）。nb 携带 4 向水平邻居 id（t209 栅栏连接用）。
//   tileW/hx/hy/v0/v1 是图集 UV 常量（与 chunkgeometry 同 N=19 图集对齐；瓦片按 BlockRegistry::tileIndex 查得）。
//
// switch(blockId) 分流（机制等价 MC 1.0 (id, metadata) 方块模型：id 选形状、state/metadata 选朝向 / 开合 /
// 半位）：t133 基础设施**只搭骨架** —— 所有 case 走 default 返回 0（不追加顶点）。t134 落地 6 方块
// （WoodSlab=15 / WoodStairs=16 / WoodFence=17 / WoodPressurePlate=18 / WoodDoor=19 / WoodTrapdoor=20）
// 的具体异形顶点。当前 BlockRegistry::Count=15 = FirstPartial → 任何合法 id 都 < FirstPartial →
// chunkgeometry 的 `b >= FirstPartial` 分支永不进入、append 不会被调用；基础设施就绪、等 t134 激活。
//
// 「复用 kFaces 法线 + uv 规则」（spec）：chunkgeometry.cpp 的 kFaces 定义了 6 轴向面的外法线 + 4 角
//   位置 + per-face UV 映射（±X 面 cu,cv=(z,y)；±Z 面 (x,y)；±Y 面 (x,z)）。异形方块的轴向整面（如
//   slab 顶 / 底）遵循同一规则；非整格子面（如 slab 侧的 1×0.5 矩形）按相同法线方向、UV 随角点
//   (cu,cv)∈[0,1] 映射到瓦片 [u0,u1]×[v0,v1] 子区（与 chunkgeometry 立方面 UV 公式同源）。各 shape
//   生成器（t134）据此自写 face 数据，无需 import chunkgeometry 的私有 kFaces。
//
// 文件位置说明（分层 PLAN §2）：dev-spec / dev-plan 建议 src/Renderer/，但本类**唯一调用者**
//   chunkgeometry 当前在 src/World/（历史摆放，其本身是 mesher 应属 Renderer 却被置 World）。
//   若本类放 Renderer → chunkgeometry(World) include partialblockgeometry(Renderer) = 低层 include
//   高层 = 破 PLAN §2「依赖只向下」铁律（t41 同族：raycast 按建议进 Core 会违铁律、改放 Game）。
//   故与 chunkgeometry 同放 src/World/（mesher 子系统同层），待未来 chunkgeometry 整体迁 Renderer 时
//   一并搬迁。本类自身只依赖 Core（BlockRegistry）+ Qt 容器，向下合规。
class PartialBlockGeometry
{
public:
    // 追加异形方块的顶点 / 索引到 verts/idx。返回追加的顶点数（0 = 该 id 未实现 / 非异形方块）。
    //   lx/ly/lz = chunk 局部格坐标（顶点位置 = 格原点 (lx,ly,lz) + 局部偏移 [0,1]^3）。
    //   blockId/state = 体素 id + 朝向/开合状态（door 两格 / trapdoor 开合 / slab 上下半 / stairs 朝向）。
    //   light = 逐块光照上下文（surface/shade/sun 量；append 按各面外法线算 vc，同 chunkgeometry 立方面）。
    //   nb = 逐块水平邻居上下文（t209 栅栏连接；仅 fence 用，其余异形方块忽略）。
    //   tileW/hx/hy/v0/v1 = 图集 UV 常量（与 chunkgeometry 同 N=19 图集对齐，半纹素内缩防渗色）。
    static int append(QVector<Vtx> &verts, QVector<quint32> &idx,
                      int lx, int ly, int lz,
                      quint8 blockId, quint8 state,
                      const PartialLightCtx &light,
                      const PartialNeighborCtx &nb,
                      float tileW, float hx, float hy, float v0, float v1);

private:
    PartialBlockGeometry() = delete; // 纯静态工具，无实例。
};

#endif // PARTIALBLOCKGEOMETRY_H
