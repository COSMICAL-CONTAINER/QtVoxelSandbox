#include "partialblockgeometry.h"

// t133 基础设施：异形方块几何骨架。switch 按 blockId 分流到各形状生成器（机制等价 MC 1.0
//   (id, metadata) 方块模型：id 选形状、state/metadata 选朝向 / 开合 / 半位）。
//
// 当前所有合法 blockId < BlockRegistry::FirstPartial(=15) = Count → chunkgeometry 的
//   `if (b >= FirstPartial)` 分支永不进入、append 不会被调用。此处只搭骨架（默认不追加顶点），
//   等 t134 在 BlockRegistry 加 WoodSlab=15 ... WoodTrapdoor=20 后激活各 case 并填充具体异形顶点。
//
// 参数全集已就位（verts/idx/lx/ly/lz/blockId/state/light/UV 常量），t134 各 case 直接使用；
// 当前骨架未引用的形参标记 (void) 抑制 -Wunused-parameter（PLAN §4 零警告门）。
//   注：(void) 此处是「基础设施 stub 的未用形参」标准 C++ 处置 —— 非 lessons-learned「[[nodiscard]]
//   返回值被丢」那种错误掩盖（本函数无 [[nodiscard]]、也无 fallible 操作可失败）；t134 各 case 落地
//   后这些形参即被真实使用，(void) 行随之删除。
int PartialBlockGeometry::append(
    QVector<Vtx> &verts, QVector<quint32> &idx,
    int lx, int ly, int lz,
    quint8 blockId, quint8 state,
    const PartialLightCtx &light,
    float tileW, float hx, float hy, float v0, float v1)
{
    (void)verts; (void)idx;
    (void)lx;    (void)ly;    (void)lz;
    (void)state; (void)light;
    (void)tileW; (void)hx;    (void)hy; (void)v0; (void)v1;

    switch (blockId) {
        // t134 落地（BlockRegistry 加 id 后此处改用枚举名替换魔法数）：
        //   case BlockRegistry::WoodSlab:           return buildSlab(verts, idx, lx, ly, lz, state, light, tileW, hx, hy, v0, v1);
        //   case BlockRegistry::WoodStairs:         return buildStairs(...);
        //   case BlockRegistry::WoodFence:          return buildFence(...);
        //   case BlockRegistry::WoodPressurePlate:  return buildPressurePlate(...);
        //   case BlockRegistry::WoodDoor:           return buildDoor(...);
        //   case BlockRegistry::WoodTrapdoor:       return buildTrapdoor(...);
        // 各 build* 按「复用 kFaces 法线+uv 规则」生成异形顶点（轴向整面走 kFaces 约定、子面按
        //   (cu,cv)∈[0,1] 映射瓦片子区），顶点色用 light 上下文按面外法线算 vc（同 chunkgeometry 立方面）。
        default:
            return 0; // 未实现 / 非异形方块 → 不追加顶点（chunkgeometry 的 continue 跳过此格）
    }
}
