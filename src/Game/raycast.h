#ifndef RAYCAST_H
#define RAYCAST_H

#include <QtGlobal> // qint32
#include <QVector3D>

class World; // 前向声明：raycast 只读 World（PLAN §2：Physics 层只向下依赖 World 层）

// 体素射线投射（Physics/Game 层；DDA = Amanatides-Woo 快速体素遍历）。
//
// 沿单位方向 dir 从 origin 出发，逐体素步进（每步 ≤ 1 格），返回 maxDist 范围内
// 命中的**第一个实体方块**及其**命中面法线**（破/放放置方向的前置；PLAN §4"射线选体"）。
//
// 分层（PLAN §2）：本层只 include World（读 blockAt/isSolid）与 QtCore/QtGui（数学），
// **绝不**依赖 Renderer/QtQuick3D —— 射线选体是逻辑，呈现交给视图层。
//
// 返回值约定：
//   - valid=false：射程内未命中（空气一路到 maxDist），或起点已嵌在实体方块内（退化）。
//   - valid=true：bx/by/bz = 命中方块的整数格坐标；nx/ny/nz = 命中面外法线（±1 的某一轴，
//     其余为 0），即「射线从该面进入方块」。空气/非实体方块被穿过，不阻挡。
//   - dist：命中点沿**归一化方向**的参数距离（内部 dir 已归一，故 dist 即起点到命中面的欧氏
//     距离）。t40 第三人称相机距离钳制用它；选体（t04）不读它，仅为相机复用 raycast 而暴露。
struct RayHit
{
    bool valid = false;
    qint32 bx = 0, by = 0, bz = 0;   // 命中方块的格坐标（+Y 朝上，与 World::blockAt 一致）
    float  nx = 0, ny = 0, nz = 0;   // 命中面外法线（单位向量，单一非零分量）
    float  dist = 0.0f;              // 命中面距起点欧氏距离（归一化方向上的 t；valid=false 时为 0）
};

// 从 origin 沿 dir（无需归一化，内部归一）步进，maxDist 内返回首个实体方块命中。
RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist);

#endif // RAYCAST_H
