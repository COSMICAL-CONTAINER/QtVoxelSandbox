#ifndef RAYCAST_H
#define RAYCAST_H

#include <QtGlobal> // quint8, qint32
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
//   - valid=false：射程内未命中（空气一路到 maxDist），或起点已嵌在「该模式视为阻挡」的实体方块内（退化）。
//   - valid=true：bx/by/bz = 命中方块的整数格坐标；nx/ny/nz = 命中面外法线（±1 的某一轴，
//     其余为 0），即「射线从该面进入方块」。空气/被该模式视为「穿过」的方块不阻挡。
//   - dist：命中点沿**归一化方向**的参数距离（内部 dir 已归一，故 dist 即起点到命中面的欧氏
//     距离）。t40 第三人称相机距离钳制用它；选体（t04）不读它，仅为相机复用 raycast 而暴露。
struct RayHit
{
    bool valid = false;
    qint32 bx = 0, by = 0, bz = 0;   // 命中方块的格坐标（+Y 朝上，与 World::blockAt 一致）
    float  nx = 0, ny = 0, nz = 0;   // 命中面外法线（单位向量，单一非零分量）
    float  dist = 0.0f;              // 命中面距起点欧氏距离（归一化方向上的 t；valid=false 时为 0）
};

// 阻挡谓词（filter）—— 同一份 DDA 被多种语义复用，Torch / Water 是否挡射线按调用方语义切换：
//   - 空气恒穿过；实体方块（非 Torch / Water）恒挡。
//   - HitTorch（t184）：Torch 亦挡射线 —— 选体模式下准星瞄火把即**命中火把**（可显示火把边界框 +
//     直接左键挖）。t157 旧设计「射线永远穿透火把」导致火把不可直挖（须先挖支撑块），违背用户原意
//     （「火把可选可挖、空气可穿」），t184 修正：选体射线纳入 Torch。仅当光标在空气 / 不完整方块的
//     空气部分时才穿到后方实体 —— 即空气穿过、火把命中（机制等价 MC 1.0 火把可被准星选中并秒破）。
//   - HitWater（t174）：Water 亦挡射线 —— 铁桶舀水专用（命中首个水格）。
//   - 默认（filter=0，RayFilter::Default）：Torch / Water 均穿过 —— 用于相机碰撞距离（t40）：
//     Torch / Water 皆 non-solid（玩家可走过 / 游过），相机不应被其拉近视距（保 t40 行为不回归）。
namespace RayFilter {
    constexpr unsigned Default  = 0u;                  // Torch / Water 均穿过（相机距离 t40）
    constexpr unsigned HitTorch = 1u << 0;             // Torch 挡射线（选体 t184：火把可选中 / 直挖）
    constexpr unsigned HitWater = 1u << 1;             // Water 挡射线（铁桶舀水 t174）
} // namespace RayFilter

// 从 origin 沿 dir（无需归一化，内部归一）步进，maxDist 内返回首个「该 filter 视为阻挡」的方块命中。
//   选体（updateRaycast）传 RayFilter::HitTorch（火把命中、水穿过）；相机距离（updateCameraDistance）
//   传 RayFilter::Default（火把 / 水均穿过，保 t40）；铁桶舀水传 RayFilter::HitWater（命中首个水格）。
//   详见 .cpp blocksRay / 起点退化注释。
RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist, unsigned filter = RayFilter::Default);

#endif // RAYCAST_H
