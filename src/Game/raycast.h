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

// 阻挡谓词（filter）—— 同一份 DDA 被多种语义复用，Torch / Water / Ladder 是否挡射线按调用方语义切换：
//   - 空气恒穿过；实体方块（非 Torch / Water / Ladder）恒挡。
//   - HitTorch（t184）：Torch 亦挡射线 —— 选体模式下准星瞄火把即**命中火把**（可显示火把边界框 +
//     直接左键挖）。t157 旧设计「射线永远穿透火把」导致火把不可直挖（须先挖支撑块），违背用户原意
//     （「火把可选可挖、空气可穿」），t184 修正：选体射线纳入 Torch。仅当光标在空气 / 不完整方块的
//     空气部分时才穿到后方实体 —— 即空气穿过、火把命中（机制等价 MC 1.0 火把可被准星选中并秒破）。
//   - HitLadder（t501）：Ladder 亦挡射线 —— 机制同 HitTorch：木梯默认穿（玩家爬梯时准星瞄后方 / 邻格
//     方块应选中方块本体而非先撸梯子，spec「爬梯时挖掘优先选中梯子 → 应像火把不优先选中、可透视穿过」）；
//     仅当准星完全落在木梯视觉面（贴墙薄 quad 的精确 sub-AABB）时才命中木梯本身（可显示框 + 左键拆梯）。
//     相机距离 / 桶射线走 Default（不设 HitLadder）→ 木梯穿（non-solid 不拉近视距 / 不挡舀水），保 t40 / t174。
//   - HitWater（t174）：Water 亦挡射线 —— 铁桶舀水专用（命中首个水格）。
//   - HitPartial（t605）：不完整方块（半砖 / 雪层 / 楼梯 / 压力板 / 栅栏 / 门 / 活版门…）按其**碰撞 sub-AABB**
//     精确命中（射线进格后须真碰实体子盒才算，空气部分穿过）—— 相机碰撞距离（t605）专用：第三人称相机沿
//     偏移方向可用空隙以「实体面」为准（与玩家碰撞 collisionAABBs 同源 shapeBoxes），修 1.5 格通道蹲行切
//     第三人称相机穿墙（眼位落在天花板半砖格空气段时旧版退化 invalid → 相机取满距 3.5 直穿）。Torch / Water /
//     Ladder 仍穿过（HitPartial 不含它们的 Hit* 标志，non-solid 不拉近视距，保 t40）。
//   - 默认（filter=0，RayFilter::Default）：Torch / Water / Ladder 均穿过 —— 基础语义（t40 相机旧用；t605 起
//     相机改传 HitPartial 以获得 sub-AABB 精度，Default 保留作 filter 缺省值 / 最简阻挡谓词）：
//     Torch / Water / Ladder 皆 non-solid（玩家可走过 / 游过 / 穿入梯格）。
namespace RayFilter {
    constexpr unsigned Default  = 0u;                  // Torch / Water / Ladder 均穿过（基础语义；filter 缺省值）
    constexpr unsigned HitTorch = 1u << 0;             // Torch 挡射线（选体 t184：火把可选中 / 直挖）
    constexpr unsigned HitWater = 1u << 1;             // Water 挡射线（铁桶舀水 t174）
    constexpr unsigned HitLava  = 1u << 2;             // Lava 挡射线（铁桶舀岩浆 t343）
    constexpr unsigned HitLadder = 1u << 3;            // Ladder 挡射线（选体 t501：木梯可选中 / 直拆；同 HitTorch 模式）
    constexpr unsigned HitPartial = 1u << 4;           // 不完整方块 sub-AABB 精确命中（相机距离 t605；同选体几何）
} // namespace RayFilter

// 从 origin 沿 dir（无需归一化，内部归一）步进，maxDist 内返回首个「该 filter 视为阻挡」的方块命中。
//   选体（updateRaycast）传 RayFilter::HitTorch|HitLadder（火把 / 木梯命中、水穿过）；相机距离（updateCameraDistance）
//   传 RayFilter::HitPartial（t605：不完整方块 sub-AABB 精确命中、火把 / 水 / 木梯均穿过）；铁桶舀水传
//   RayFilter::HitWater（命中首个水格）。详见 .cpp blocksRay / 起点退化注释。
RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist, unsigned filter = RayFilter::Default);

#endif // RAYCAST_H
