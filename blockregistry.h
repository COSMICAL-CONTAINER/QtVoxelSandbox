#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString（用户可见中文名）

// 方块注册表（单一权威数据源；World 层）。
//
// 把「方块 id → 每面图集瓦片 / 是否实体 / 内部名」收敛到此处，取代散落在
// chunkgeometry::tileFor 与 worldgen 字面量里的硬编码。mesher(Renderer) 与
// worldgen(World) 都**只读**本表，不得各持副本（PLAN §2：世界数据单一）。
//
// 分层（PLAN §2）：本层只依赖 Core（QtGlobal），**不得**依赖 Renderer/QtQuick3D/
// Physics/QtQuick。依赖只向下。
//
// 方块 id（稳定可引用；worldgen/网格/存档都按 id 引用，勿随意改顺序/插值）：
//   0=air 1=grass 2=dirt 3=stone 4=cobble 5=log 6=planks 7=leaves 8=sand
// air 恒 solid=false。方块名用通用词，零 MC 专有名词（PLAN §9）。
class BlockRegistry
{
public:
    // 方块 id（与体素栅格的 quint8 存储对齐；底层类型 quint8 便于直接赋给栅格）。
    enum Id : quint8 {
        Air    = 0,
        Grass  = 1,
        Dirt   = 2,
        Stone  = 3,
        Cobble = 4,
        Log    = 5,
        Planks = 6,
        Leaves = 7,
        Sand   = 8,
        Count  = 9, // 哨兵：已定义方块数（含 air），也是合法 id 的上界（id < Count）。
    };

    // 面索引（与 Renderer 的 kFaces 顺序一致，是 World/Renderer 共享的轴向约定）：
    //   0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z
    enum Face : int {
        PosX   = 0,
        NegX   = 1,
        Top    = 2, // +Y
        Bottom = 3, // -Y
        PosZ   = 4,
        NegZ   = 5,
    };

    // 给定方块 id 与面，返回**图集瓦片序号**（须与 tools/build_atlas.py 打包顺序一致）。
    // 越界/未知 id 返回 0（兜底，与旧 tileFor 同语义）。
    //
    // 瓦片顺序（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    // 图集由 tools/build_atlas.py 打包全部 10 瓦片；mesher N=10 与之严格对齐。
    static int tileIndex(quint8 blockId, Face face);

    // 方块是否实体（参与碰撞 / culled 面剔除）。air 恒 false；其余均 true。
    // 越界/未知 id 返回 false。
    static bool isSolid(quint8 blockId);

    // 内部/调试用方块名（**非**面向用户字串；通用词）。越界/未知 id 返回 "unknown"。
    static const char *blockName(quint8 blockId);

    // 用户可见的**中文**显示名（PLAN §9 override (b)：通用描述词）。
    //   air → 空串；越界/未知 id → 空串（兜底）。
    // 与 blockName()（内部英文标识符）分离：本方法供 HUD/背包等面向用户文本消费；
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释同源）。
    //   grass=草方块 dirt=泥土 stone=石头 cobble=圆石 log=橡木原木
    //   planks=橡木木板 leaves=橡树树叶 sand=沙子
    static QString displayName(quint8 blockId);

private:
    BlockRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // BLOCKREGISTRY_H
