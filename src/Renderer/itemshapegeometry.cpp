#include "itemshapegeometry.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm> // std::min/max
#include <vector>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节（同 MobModel；无顶点色——昼夜明暗走 QML baseColor × terrainLight）。
namespace {
struct ShapeVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点模板（±0.5 单位盒；CCW 朝外，同 BlockCube kFaceCorners——含面内归一化 (cu,cv) 供 UV 插值）。
// 本类把模板按子盒 extent 缩放：pos = 盒心 + fc.axis · 盒半长（fc 的 ±0.5 × 半长）。
constexpr float kH = 0.5f;
struct FaceCorner {
    float x, y, z;
    float cu, cv;
};
const FaceCorner kFaceCorners[6][4] = {
    // +X（面内 z,y）
    {{ kH, -kH, -kH, 0, 0}, { kH,  kH, -kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH, -kH,  kH, 1, 0}},
    // -X（面内 z,y；u 反向）
    {{-kH, -kH,  kH, 1, 0}, {-kH,  kH,  kH, 1, 1}, {-kH,  kH, -kH, 0, 1}, {-kH, -kH, -kH, 0, 0}},
    // +Y（顶，面内 x,z）
    {{-kH,  kH,  kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH,  kH, -kH, 1, 0}, {-kH,  kH, -kH, 0, 0}},
    // -Y（底，面内 x,z）
    {{-kH, -kH, -kH, 0, 0}, { kH, -kH, -kH, 1, 0}, { kH, -kH,  kH, 1, 1}, {-kH, -kH,  kH, 0, 1}},
    // +Z（面内 x,y）
    {{-kH, -kH,  kH, 0, 0}, { kH, -kH,  kH, 1, 0}, { kH,  kH,  kH, 1, 1}, {-kH,  kH,  kH, 0, 1}},
    // -Z（面内 x,y；u 反向）
    {{ kH, -kH, -kH, 1, 0}, {-kH, -kH, -kH, 0, 0}, {-kH,  kH, -kH, 0, 1}, { kH,  kH, -kH, 1, 1}},
};

// 图集常量（同 BlockCube：AtlasTileCount / kAtlasTilePx 单一权威 + 半纹素内缩防渗色）。
constexpr int kAtlasN = BlockRegistry::AtlasTileCount;
constexpr float kTileW = 1.0f / kAtlasN;
constexpr float kHx = 0.5f / (kAtlasN * BlockRegistry::kAtlasTilePx);
constexpr float kHy = 0.5f / BlockRegistry::kAtlasTilePx;

// 追加一个轴对齐子盒（cell-local [0,1]³ 区间，**未居中**——yShift 把盒平移到形心居中坐标系）。
//   topTile/bottomTile/sideTile = 三段图集瓦片。可选 UV 窗口（wu0..wu1 / wv0..wv1 ∈ [0,1]，瓦片内子区
//   ——火把细柱采 torch 瓦片中央列带用；默认满窗）。bounds 累计实际顶点范围。
//   t925 sideAltTile/sideAltMask：按面覆写瓦片（mask 第 f 位置 1 → 第 f 面用 sideAltTile）——门族薄侧边
//   用同族基材瓦片（t674 mesher 同规则）免「压缩门贴图」观感；默认 -1/0 全不覆写（既有调用零改动）。
//   t969 capTopV0..1 / capBotV0..1：端面 v 窗覆写（f=2 顶 / f=3 底；负值哨兵 = 沿用共享 wv 窗，既有
//   调用零改动）。动机 = 火把端面垃圾窗：共享窗 v 满高（含瓦片上下透明行）铺到 2/16 见方端面上，端面
//   把整条瓦片竖条压扁成一片「悬空碎屑」——透明行被 Mask 丢弃后端面漏出背景底色（用户观感「中间悬空
//   黑色部分」），且侧脸可见内容止于内容带内、端面仍落盒底真边 → 碎屑与柄身之间隔一条透明断口（悬空
//   感本体）。覆写后端面只采内容带内与该端相邻的 2px 色带（顶=焰带 / 底=柄木带），与端面侧面内容同色
//   延续——两端色带不同（焰/木），故顶底各持一对。
void addShapeBox(std::vector<ShapeVtx> &verts, std::vector<quint32> &idx,
                 float x0, float y0, float z0, float x1, float y1, float z1,
                 int topTile, int bottomTile, int sideTile, float yShift,
                 QVector3D &bMin, QVector3D &bMax,
                 float wu0 = 0.0f, float wu1 = 1.0f, float wv0 = 0.0f, float wv1 = 1.0f,
                 int sideAltTile = -1, unsigned sideAltMask = 0u,
                 float capTopV0 = -1.0f, float capTopV1 = -1.0f,
                 float capBotV0 = -1.0f, float capBotV1 = -1.0f)
{
    // cell-local [0,1]³ → 原点居中系：X/Z 平移 −0.5（满格 footprint 即 ±0.5；BlockCube 同基准），Y 用
    //   yShift 形心居中（各形状自身高度中点，图标 y_mid 同口径）。
    const float mx = (x0 + x1) * 0.5f - 0.5f, my = (y0 + y1) * 0.5f + yShift, mz = (z0 + z1) * 0.5f - 0.5f;
    // 模板角点 ∈ ±0.5 → 乘盒**全长**（extent）落到位（乘半长会把盒缩成一半——t880 探针 bounds 红的根因）。
    const float ex = x1 - x0, ey = y1 - y0, ez = z1 - z0;
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        const int tile = (sideAltTile >= 0 && ((sideAltMask >> unsigned(f)) & 1u))
                             ? sideAltTile
                             : (f == 2) ? topTile : (f == 3) ? bottomTile : sideTile;
        const float tu0 = float(tile) * kTileW + kHx;
        const float tu1 = float(tile + 1) * kTileW - kHx;
        const float u0 = tu0 + wu0 * (tu1 - tu0);
        const float u1 = tu0 + wu1 * (tu1 - tu0);
        // t969：±Y 端面 v 窗覆写（capTopV/capBotV 哨兵 ≥0 生效；侧四脸恒用共享 wv 窗）。
        const float wva = (f == 2 && capTopV0 >= 0.0f) ? capTopV0
                        : (f == 3 && capBotV0 >= 0.0f) ? capBotV0 : wv0;
        const float wvb = (f == 2 && capTopV1 >= 0.0f) ? capTopV1
                        : (f == 3 && capBotV1 >= 0.0f) ? capBotV1 : wv1;
        const float v0 = kHy + wva * (1.0f - 2.0f * kHy);
        const float v1 = kHy + wvb * (1.0f - 2.0f * kHy);
        const quint32 b = base + quint32(f * 4);
        for (int c = 0; c < 4; ++c) {
            const FaceCorner &fc = kFaceCorners[f][c];
            ShapeVtx v;
            v.x = mx + fc.x * ex;
            v.y = my + fc.y * ey;
            v.z = mz + fc.z * ez;
            v.u = u0 + fc.cu * (u1 - u0);
            v.v = v0 + fc.cv * (v1 - v0);
            verts.push_back(v);
            bMin.setX(std::min(bMin.x(), v.x)); bMax.setX(std::max(bMax.x(), v.x));
            bMin.setY(std::min(bMin.y(), v.y)); bMax.setY(std::max(bMax.y(), v.y));
            bMin.setZ(std::min(bMin.z(), v.z)); bMax.setZ(std::max(bMax.z(), v.z));
        }
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

// 追加一片双面 quad（cross 族：对角平面 + 反向绕序副本 → 双面可见，机制等价 World pushCrossQuad
//   的双面发）。四角 cell-local（(ax,az)-(bx,bz) 对角 + 高度 h0..h1），yShift 同上；瓦片整张贴面。
void addCrossQuad(std::vector<ShapeVtx> &verts, std::vector<quint32> &idx,
                  float ax, float az, float bx, float bz, float h0, float h1,
                  int tile, float yShift, QVector3D &bMin, QVector3D &bMax)
{
    const float u0 = float(tile) * kTileW + kHx;
    const float u1 = float(tile + 1) * kTileW - kHx;
    const float v0 = kHy, v1 = 1.0f - kHy;
    const float y0 = h0 + yShift, y1 = h1 + yShift;
    // cell-local [0,1] → 原点居中系（X/Z −0.5，同 addShapeBox）。
    const ShapeVtx quad[4] = {
        { ax - 0.5f, y0, az - 0.5f, u0, v0 }, { bx - 0.5f, y0, bz - 0.5f, u1, v0 },
        { bx - 0.5f, y1, bz - 0.5f, u1, v1 }, { ax - 0.5f, y1, az - 0.5f, u0, v1 },
    };
    for (int pass = 0; pass < 2; ++pass) { // pass 0 正面 / pass 1 反向绕序（双面）
        const quint32 b = quint32(verts.size());
        for (int c = 0; c < 4; ++c) {
            const ShapeVtx &v = quad[c];
            verts.push_back(v);
            bMin.setX(std::min(bMin.x(), v.x)); bMax.setX(std::max(bMax.x(), v.x));
            bMin.setY(std::min(bMin.y(), v.y)); bMax.setY(std::max(bMax.y(), v.y));
            bMin.setZ(std::min(bMin.z(), v.z)); bMax.setZ(std::max(bMax.z(), v.z));
        }
        if (pass == 0) {
            idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
        } else {
            idx.push_back(b + 2); idx.push_back(b + 1); idx.push_back(b + 0);
            idx.push_back(b + 3); idx.push_back(b + 2); idx.push_back(b + 0);
        }
    }
}

// t965 追加一片**水平**双面 quad（贴地薄板族：动力铁轨查看器形态——mesher pushRailFlat 直线 NS 的
//   同源形态）。四角 (x0,z0)(x1,z0)(x1,z1)(x0,z1) @ 高 y，UV u→x / v→z（mesher NS 直轨同映射）；
//   yShift 形心居中同 addCrossQuad；双面发两 pass（绕序镜像）同 addCrossQuad。
void addFlatQuad(std::vector<ShapeVtx> &verts, std::vector<quint32> &idx,
                 float x0, float z0, float x1, float z1, float y,
                 int tile, float yShift, QVector3D &bMin, QVector3D &bMax)
{
    const float u0 = float(tile) * kTileW + kHx;
    const float u1 = float(tile + 1) * kTileW - kHx;
    const float v0 = kHy, v1 = 1.0f - kHy;
    const float yy = y + yShift;
    const ShapeVtx quad[4] = {
        { x0 - 0.5f, yy, z0 - 0.5f, u0, v0 }, { x1 - 0.5f, yy, z0 - 0.5f, u1, v0 },
        { x1 - 0.5f, yy, z1 - 0.5f, u1, v1 }, { x0 - 0.5f, yy, z1 - 0.5f, u0, v1 },
    };
    for (int pass = 0; pass < 2; ++pass) {
        const quint32 b = quint32(verts.size());
        for (int c = 0; c < 4; ++c) {
            const ShapeVtx &v = quad[c];
            verts.push_back(v);
            bMin.setX(std::min(bMin.x(), v.x)); bMax.setX(std::max(bMax.x(), v.x));
            bMin.setY(std::min(bMin.y(), v.y)); bMax.setY(std::max(bMax.y(), v.y));
            bMin.setZ(std::min(bMin.z(), v.z)); bMax.setZ(std::max(bMax.z(), v.z));
        }
        if (pass == 0) {
            idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
        } else {
            idx.push_back(b + 2); idx.push_back(b + 1); idx.push_back(b + 0);
            idx.push_back(b + 3); idx.push_back(b + 2); idx.push_back(b + 0);
        }
    }
}
} // namespace

ItemShapeGeometry::ItemShapeGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 WoodSlab 建；QML 设 blockId 时再 rebuild
}

void ItemShapeGeometry::setBlockId(int id)
{
    if (id <= 0 || id >= int(BlockRegistry::Count)) id = int(BlockRegistry::WoodSlab); // 兜底合法方块
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

// t965 形态按钮组 state：值变 → rebuild（态变族开合几何 / 高度 / 瓦片重算）。-1 = auto 哨兵（唯一合法
//   负值，不得参与 & 0xFF 钳制）；其余钳到 quint8 非负域。
void ItemShapeGeometry::setBlockState(int s)
{
    if (s != -1)
        s = (s < 0) ? 0 : (s & 0xFF);
    if (s == m_blockState) return;
    m_blockState = s;
    emit blockStateChanged();
    rebuild();
}

// 按 def.shape 泛化 + 特型覆盖（火把 / 附魔台 / 动力铁轨）建几何。各形状盒区与 World partialblockgeometry
//   同源；yShift = -(y0+y1)/2 形心居中（图标 y_mid 同口径）。
// t965 形态按钮组：生效 state = 显式（≥0）直用；auto（-1，掉落物 / 旧消费路径）→ 各形状旧默认
//   （小麦=成熟穗 WheatCropStageMax〔t925「图标显成熟态」口径〕/ 草丛=中草满格〔旧版唯一外观〕/ 其余 0）
//   —— 未设 state 的既有消费端行为逐位零回归。
void ItemShapeGeometry::rebuild()
{
    std::vector<ShapeVtx> verts;
    std::vector<quint32> idx;
    QVector3D bMin(1e9f, 1e9f, 1e9f), bMax(-1e9f, -1e9f, -1e9f);
    const BlockRegistry::BlockDef &d = BlockRegistry::def(quint8(m_blockId));
    const int topT = d.topTile, botT = d.bottomTile, sideT = d.sideTile;
    int effState = m_blockState;
    if (effState < 0) {
        if (m_blockId == int(BlockRegistry::WheatCrop))
            effState = int(BlockRegistry::WheatCropStageMax);
        else if (m_blockId == int(BlockRegistry::TallGrass))
            effState = int(BlockRegistry::TallGrassMedium);
        else
            effState = 0;
    }
    // 活板门族薄侧边 per-family 分流（铁=iron_block / 木=planks；World mesher t742/t879 同一语言）。
    const int trapSide = (m_blockId == int(BlockRegistry::IronTrapdoor))
        ? BlockRegistry::tileIndex(BlockRegistry::IronBlock, BlockRegistry::PosX)
        : BlockRegistry::tileIndex(BlockRegistry::Planks, BlockRegistry::PosX);

    if (m_blockId == int(BlockRegistry::Torch)) {
        // 火把细立柱：2/16 见方 × 10/16 高（世界内火把模型量级）。贴图 torch 瓦片**中央列带**子窗
        //   （u [7/16,9/16] 火把本体柱）侧/顶/底同窗（火把 def 各面 = torch(17)；整瓦片直铺会把 16px
        //   宽透明底压进 2/16 窄面成碎条——子窗只采火把像素）。形心居中 yShift=-5/16。
        //   t969 v 窗收正到**内容带**（用户第五轮「中间悬空黑色部分」）：旧侧脸 v 满高 [0,1]——瓦片
        //   row0 与 row14..15 是透明底，Mask 丢弃后侧脸上下各留一段被裁的死带，盒底真边与柄身可见
        //   内容之间出现透明断口；±Y 端面又把含透明行的整条竖带压扁成 2/16 见方碎片，悬在断口下方
        //   露背景底色 = 「悬空黑色部分」。修 = 侧脸 v 窗收正到不透明内容带（焰头+柄木，上下零死带）；
        //   ±Y 端面经 capV 覆写采与该端相邻的 2px 内容色带（顶=焰带 / 底=柄木带，机制等价 MC 火把方块
        //   模型端面采样柄截面），端面永不含透明行、不再悬空。
        //   ⚠ V 朝向契约（lessons-learned t489 像素级实测）：**图像顶 ↔ v=1**（上传翻转）——下述窗口
        //   全按「v = 1 − 图像行/16」折算（如瓦片图像 row1..2 = v [13/16,15/16]），勿按行号直写。
        //   数值随 build_torch.py 画稿锚定（内容图像 row1..13；改画稿须同步）。
        constexpr float kT0 = 7.0f / 16.0f, kT1 = 9.0f / 16.0f, kHh = 10.0f / 16.0f;
        constexpr float kWu0 = 7.0f / 16.0f, kWu1 = 9.0f / 16.0f;
        constexpr float kWv0 = 2.0f / 16.0f, kWv1 = 15.0f / 16.0f;        // 内容带（图像 row1..13）
        // 端面窗**必须全窗不透明**（焰尖 row1 只占 x7 单列、x8 透明——任何含焰行的端面窗中央必有
        //   透明孔 → Mask 丢弃 → 端面中央黑斑）。故顶/底端面都采柄木**全双列不透明**带（机制等价
        //   MC 火把方块模型 up 面采 [7,6]..[9,8] 柄顶截面）：
        constexpr float kCapV0 = 8.0f / 16.0f, kCapV1 = 10.0f / 16.0f;    // 顶端面：柄顶木带（图像 row6..7）
        constexpr float kCapV2 = 2.0f / 16.0f, kCapV3 = 4.0f / 16.0f;     // 底端面：柄底木带（图像 row12..13）
        addShapeBox(verts, idx, kT0, 0.0f, kT0, kT1, kHh, kT1,
                    topT, botT, sideT, -kHh * 0.5f, bMin, bMax, kWu0, kWu1, kWv0, kWv1,
                    -1, 0u, kCapV0, kCapV1, kCapV2, kCapV3);
    } else if (m_blockId == int(BlockRegistry::EnchantingTable)) {
        // 附魔台 0.75 矮盒（def shape=ShapeFull 但世界内走 mesher 显式 0.75 盒——本类同款；侧瓦片图集
        //   已裁顶 0.25 空白 → 整张贴 0.75 高侧面）。形心居中 -0.375。台顶悬浮书由 QML 叠 EnchantBookBox。
        addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.75f, 1.0f,
                    topT, botT, sideT, -0.375f, bMin, bMax);
    } else if (m_blockId == int(BlockRegistry::Lever)
               || m_blockId == int(BlockRegistry::WoodButton)
               || m_blockId == int(BlockRegistry::StoneButton)) {
        // t925 机关三件（拉杆 112 / 木·石按钮 113/114）：几何源 = BlockRegistry::mechBoxes(blockId, 0)
        //   ——与 World mesher t662 / raycastAABBs 选中**同一盒集**（贴地默认态 off），零第二套数值。贴图
        //   分流同 mesher：按钮单盒贴本方块瓦片（132 木 / 133 石）；拉杆盒 0 底座贴 cobble、盒 1.. 摆棍
        //   贴 planks（lever(131) 是平面立绘，贴 3D 小盒会整图拉伸错位——mesher t662 同款取舍）。形心
        //   居中 yShift -0.5（盒集占满 cell [0,1]，与楼梯同口径）。
        const bool isLever = (m_blockId == int(BlockRegistry::Lever));
        const int planksTile = BlockRegistry::tileIndex(BlockRegistry::Planks, BlockRegistry::PosX);
        const int cobbleTile = BlockRegistry::tileIndex(BlockRegistry::Cobble, BlockRegistry::PosX);
        const std::vector<BlockRegistry::BlockAABB> boxes = BlockRegistry::mechBoxes(quint8(m_blockId), 0);
        for (size_t bi = 0; bi < boxes.size(); ++bi) {
            const BlockRegistry::BlockAABB &bb = boxes[bi];
            const int boxTile = isLever ? (bi == 0 ? cobbleTile : planksTile) : sideT;
            addShapeBox(verts, idx, bb.minX, bb.minY, bb.minZ, bb.maxX, bb.maxY, bb.maxZ,
                        boxTile, boxTile, boxTile, -0.5f, bMin, bMax);
        }
    } else if (m_blockId == int(BlockRegistry::GoldenRail)) {
        // t965 动力铁轨贴地薄板（查看器形态预览专用 case）：掉落物侧保留 billboard（Main.qml
        //   isItem3DFamily t880「全部铁轨」排除清单不动——本分支仅查看器 selectedIsItem3D 新成员 127
        //   消费）。几何 = 水平双面 quad 贴 y≈1/16（同 mesher rail case 直线 NS 基准形态——物品无邻居
        //   语境取直轨）；瓦片经 stateTileOverride 态变（157 未激活 / 159 激活亮金——Core 单一权威）。
        //   形心居中 yShift -1/16。
        const int railT = BlockRegistry::stateTileOverride(quint8(m_blockId), int(BlockRegistry::PosX),
                                                           quint8(effState));
        const float ry = 1.0f / 16.0f;
        addFlatQuad(verts, idx, 0.0f, 0.0f, 1.0f, 1.0f, ry,
                    railT < 0 ? sideT : railT, -ry, bMin, bMax);
    } else {
        switch (d.shape) {
        case BlockRegistry::ShapeTrapdoor:
            // 合态薄板：全 footprint y[0,3/16]（同 mesher 合态 + 图标 trapdoor 盒）；形心居中 -3/32。
            // t965 开态（TrapdoorStateOpenFlag，朝向位取放置缺省 0=+X）：竖直薄板贴 +X 边（mesher 开态
            //   facing 0 盒区 x[0.8125,1]×y[0,1]×z[0,1]）；大面（±X）格子板、四薄边（±Y/±Z）同族基材
            //   （合态 t742/t879 per-face 语言镜像，面覆写掩码 0x3=±X）。
            if (effState & int(BlockRegistry::TrapdoorStateOpenFlag)) {
                addShapeBox(verts, idx, 0.8125f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                            trapSide, trapSide, trapSide, -0.5f, bMin, bMax,
                            0.0f, 1.0f, 0.0f, 1.0f, sideT, 0x3u);
            } else {
                addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f / 16.0f, 1.0f,
                            topT, botT, trapSide, -3.0f / 32.0f, bMin, bMax);
            }
            break;
        case BlockRegistry::ShapeSlab:
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f,
                        topT, botT, sideT, -0.25f, bMin, bMax);
            break;
        case BlockRegistry::ShapeStairs:
            // 整步（低，全 footprint）+ 背墙（远端半高上半）——同 World mesher / 图标 stairs 盒。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            addShapeBox(verts, idx, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            break;
        case BlockRegistry::ShapeSnowLayer:
            // state 0 基底观感：1/8 高薄板（同图标 snow_layer 盒）。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 8.0f, 1.0f,
                        topT, botT, sideT, -1.0f / 16.0f, bMin, bMax);
            break;
        case BlockRegistry::ShapeFence: {
            // t925 栅栏族（木 17 / 圆石墙 60 / 云杉 88）：物品摆位取「满连形态」= 中心立柱 + 四向横杆。
            //   世界内档臂由邻居连接态现场决定（partialblockgeometry fence case）；物品无邻居语境，满连展示
            //   横杆轮廓最可辨（MC 物品图标同款栅栏剪影）。t991 与世界 mesher 同源对齐 MC 形态：
            //   - 木/云杉（9 盒）：柱 [0.375,0.625]²×y[0,1.5]（4/16 见方 × 1.5 格高）、下档 y[6/16,9/16]、
            //     上档 y[12/16,15/16]、横杆截面 [7/16,9/16]（2px 见方），±X 档 x 柱面..格边 z[7/16,9/16] /
            //     ±Z 档 z 柱面..格边 x[7/16,9/16]。形心居中 -0.75（柱高 1.5 的中点）。
            //   - 圆石墙（6 盒）：墙形制分家——柱 [0.25,0.75]²×y[0,15/16] + 顶部凸缘 [3/16,13/16]²×
            //     y[15/16,1] + 四向低连接拱 y[10/16,15/16] 截面 [5/16,11/16]。形心居中 -0.5。
            if (m_blockId == int(BlockRegistry::CobbleFence)) {
                constexpr float aY0 = 0.625f, aY1 = 0.9375f;    // 低连接拱 y（10/16..15/16，mesher 同值）
                constexpr float aTh0 = 0.3125f, aTh1 = 0.6875f; // 拱截面（5/16..11/16）
                addShapeBox(verts, idx, 0.25f, 0.0f, 0.25f, 0.75f, 0.9375f, 0.75f,
                            topT, botT, sideT, -0.5f, bMin, bMax); // 中心柱
                addShapeBox(verts, idx, 0.1875f, 0.9375f, 0.1875f, 0.8125f, 1.0f, 0.8125f,
                            topT, botT, sideT, -0.5f, bMin, bMax); // 顶部凸缘
                for (int arm = 0; arm < 4; ++arm) {
                    const bool xAxis = (arm < 2);        // ±X 拱 / ±Z 拱
                    const bool posSide = (arm % 2 == 0); // +X/+Z：柱面..格边；-X/-Z：格边..柱面
                    const float lo = posSide ? 0.75f : 0.0f;
                    const float hi = posSide ? 1.0f : 0.25f;
                    const float x0 = xAxis ? lo : aTh0, x1 = xAxis ? hi : aTh1;
                    const float z0 = xAxis ? aTh0 : lo, z1 = xAxis ? aTh1 : hi;
                    addShapeBox(verts, idx, x0, aY0, z0, x1, aY1, z1, topT, botT, sideT, -0.5f, bMin, bMax);
                }
            } else {
                constexpr float yLo0 = 0.375f,  yLo1 = 0.5625f;  // 下档（MC 6/16..9/16，mesher 同值）
                constexpr float yHi0 = 0.75f,   yHi1 = 0.9375f;  // 上档（MC 12/16..15/16）
                constexpr float rTh0 = 0.4375f, rTh1 = 0.5625f;  // 横杆截面（MC 7/16..9/16）
                addShapeBox(verts, idx, 0.375f, 0.375f, 0.375f, 0.625f, 1.5f, 0.625f,
                            topT, botT, sideT, -0.75f, bMin, bMax); // 中心立柱（4/16 × 1.5 格高）
                for (int arm = 0; arm < 4; ++arm) {
                    const bool xAxis = (arm < 2); // ±X 档 / ±Z 档
                    const bool posSide = (arm % 2 == 0); // arm0/arm2 = +X/+Z：柱面..格边；arm1/arm3 反向
                    const float x0 = xAxis ? (posSide ? 0.625f : 0.0f) : rTh0;
                    const float x1 = xAxis ? (posSide ? 1.0f : 0.375f) : rTh1;
                    const float z0 = xAxis ? rTh0 : (posSide ? 0.625f : 0.0f);
                    const float z1 = xAxis ? rTh1 : (posSide ? 1.0f : 0.375f);
                    addShapeBox(verts, idx, x0, yLo0, z0, x1, yLo1, z1, topT, botT, sideT, -0.75f, bMin, bMax);
                    addShapeBox(verts, idx, x0, yHi0, z0, x1, yHi1, z1, topT, botT, sideT, -0.75f, bMin, bMax);
                }
            }
            break;
        }
        case BlockRegistry::ShapeDoor: {
            // t925 门族（木 19 / 云杉 89 / 铁 135）：合态 facing +X 满高薄板（厚 3/16 贴 +X 边，同 mesher
            //   合态 state=0 盒区 [0.8125,1]×[0,1]×[0,1]）。大面（±X）贴下格门板瓦片（def.bottomTile=lower
            //   ——手持 / 掉落物 BlockCube 的门贴图同口径，item 语境无「上格」）；薄侧边（±Y/±Z 四面）贴
            //   同族基材（t674 mesher 同规则：木→planks / 云杉→spruce_planks / 铁→iron_block，经 def 取免
            //   字面量漂移）。形心居中 -0.5。
            // t965 开态（DoorStateOpenFlag，朝向位取放置缺省 0=+X 合态基准）：薄板旋贴 +Z 边（mesher 开态
            //   facing 0 盒区 x[0,1]×y[0,1]×z[0.8125,1]）；大面（±Z）门板、四薄边（±X/±Y）同族基材
            //   （面覆写掩码 0x30=±Z）。
            const quint8 planksBlock = (m_blockId == int(BlockRegistry::SpruceDoor))
                ? BlockRegistry::SprucePlanks
                : (m_blockId == int(BlockRegistry::IronDoor))
                    ? BlockRegistry::IronBlock
                    : BlockRegistry::Planks;
            const int planksTile = BlockRegistry::def(planksBlock).sideTile;
            if (effState & int(BlockRegistry::DoorStateOpenFlag)) {
                addShapeBox(verts, idx, 0.0f, 0.0f, 0.8125f, 1.0f, 1.0f, 1.0f,
                            planksTile, planksTile, planksTile, -0.5f, bMin, bMax,
                            0.0f, 1.0f, 0.0f, 1.0f, botT, 0x30u);
            } else {
                addShapeBox(verts, idx, 0.8125f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                            planksTile, planksTile, botT, -0.5f, bMin, bMax,
                            0.0f, 1.0f, 0.0f, 1.0f, planksTile, 0x3Cu); // 面覆写：±Y+±Z（bit2..5）→ planks
            }
            break;
        }
        default:
            if (BlockRegistry::isCrossBillboard(quint8(m_blockId))) {
                // cross 族（草丛 / 枯灌木 / 蘑菇红白 / 蛛网 / 红石火把 / 小麦作物…）：两片对角交叉双面 quad
                //   （同 World pushCrossQuad X 形）。
                //   t925 旧口径「小麦物品取成熟金黄穗瓦片（基底 + StageMax，物品无生长 state 嫩芽不可辨）」
                //   由 auto 态保留（effState=WheatCropStageMax → 同瓦片）；t965 形态按钮组显式 state 走
                //   stateTileOverride（小麦阶段 29+age / 胡萝卜·马铃薯基底+age/2 / 红石火把 161/170——Core
                //   单一权威），其余 cross 无态变落 def 顶瓦片。
                int crossT = BlockRegistry::stateTileOverride(quint8(m_blockId), int(BlockRegistry::PosX),
                                                              quint8(effState));
                if (crossT < 0)
                    crossT = topT;
                // t965 草丛三变种高度（矮 0.5 / 中 1.0 / 高 2.0——tallGrassVariantHeight Core 权威；
                //   auto=中草满格 = 旧版唯一外观）。其余 cross 恒满格；yShift=-h/2 形心居中。
                const float gh = (m_blockId == int(BlockRegistry::TallGrass))
                    ? BlockRegistry::tallGrassVariantHeight(quint8(effState)) : 1.0f;
                addCrossQuad(verts, idx, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, gh, crossT, -gh * 0.5f, bMin, bMax);
                addCrossQuad(verts, idx, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, gh, crossT, -gh * 0.5f, bMin, bMax);
                break;
            }
            // 兜底：满立方（ShapeFull / 未知形；调用方本不该路由到此，防御性非空几何）。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            break;
        }
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride → setBounds
    //   → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(ShapeVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(ShapeVtx)));
    setBounds(bMin, bMax); // 实际形状 AABB（形心居中坐标系；Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(ShapeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(ShapeVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
