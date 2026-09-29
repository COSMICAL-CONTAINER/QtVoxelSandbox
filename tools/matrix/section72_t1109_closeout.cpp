#include "matrix_helpers.h"

// t1109 残项小批合集三——池面收官批探针段(4 腿;filter 词 r2079;矩阵 852→856)。置尾先例沿用(接
//   section71,runAll 末执行,rig 世界零接触——各腿自建 fresh 固定小世界,World 直驱 + 源钉族)。
//
// ── 现状核实裁定表(派工三件逐一全仓 grep + MC 实读,先核实后交付)──
//   件一 瓜茎茎蔓原型(t1103/t1105 候选池两面向):实读裁定——
//     ①「stem 爬蔓」面: MC 1.0 茎为**固定格** age 0-7 原位生长、果结水平邻格,任何版本无爬蔓行为
//       → 工程简化形态已等价 MC,收口(t1103 形态即终态,不为增强而增强)。
//     ②实读确有差异面,最小交付两件(均已核实不破 r2073b/r2075b——两腿只断言环内恰 1 果+茎保持成熟,
//       且泵程均止于首果窗):
//       (a)单果门: Beta 1.8.1 BlockStem 成熟分支先四向早退——任一水平邻格已是本茎**同型果块**即整窗
//         不结果(wiki:「stem 产出果后不再结果直到果被采收」);1.0 无 attached-stem 变体(1.4.2+),邻格
//         果门即 1.0 单果机制。t1103 旧注「茎保留可反复结果——MC 茎多果口径」系误读,本单纠正。
//       (b)结果朝向 state: MC 1.0 茎结果果块带朝向 metadata(南瓜刻脸),本工程南瓜原恒 state 0 →
//         改落槽位向刻脸(chestFrontFace 同源编码 0=+X 1=-X 2=+Z 3=-Z——扫描序与编码天然同构,脸背
//         茎);西瓜无刻面 state 恒 0。
//   件二 矿井野生瓜 patch: **派工前提纠正留痕**(t1105 矿井箱无南瓜种子行 / t1108 chunk-population
//     前提纠正同门先例)——实读证实原版**任何版本**废弃矿井均无野生瓜块:Java 甜瓜自然生成面=丛林
//     (1.7.2 起),1.0 全世界野生瓜=零(丛林群系 1.2 才入版且当时无瓜 patch);矿井只有箱子瓜种行
//     (Beta 1.8 起实有,r2073a 已交付并钉「melon seed pool row」)。1.0 口径的「矿区瓜」收口 =
//     负面世界扫描钉 + 箱池行收口 + 南瓜/甜瓜 worldgen 不对称源钉(本柱),不加非原版 patch(禁为增强
//     而增强)。
//   件三 沙漠村庄变体: 实读裁定 = Beta 1.8 村庄实有平原+沙漠双群系(villageSiteOk 扩 Desert 准入
//     =1.0 真),但沙漠**砂岩块模板**是 12w21a/1.3.1「changed desert villages to have desert-specific
//     blocks」(越纪元不取)——t1108 登记「1.0 沙漠砂岩变体」与实读有出入,本单纠正。沙漠站点与平原
//     同构(同模板:圆石井/木板屋/农田/砂砾路),唯二工程差异面 = 沙面地表 + 道路守卫扩 Sand(原
//     Grass/Dirt 门下沙漠列带域无草/泥格 → 道路恒灭;扩 Sand 后平原站点无 Sand 面行为逐位不变)。
//   lawful 钉修订(逐处申报): r2078d road guard 行 `...(Grass && Dirt) continue;` 扩 Sand → 拆两针
//     (首行 + Sand 扩展行)携沿革注,已在 section71 内修订;非静默改钉。
//
// ── NEG 面与豁免设计(恰红归因先于腿文)──
//   NEG-1 = 单果门退化(world.cpp tickCropGrowth 3b `if (hasFruit) continue;` 改 `if (false)
//     continue;`,编译仍绿)→ 恰红 = {r2079a}(场景 A:预置同型邻果 + 泵至首个命中窗 → 环内 2 枚红)。
//     场景 B(异型果不挡,预期仍结果)/场景 C(空环正常结果, NEG-1 摘门不改变其行为)/r2073b、r2075b
//     (泵程均止于首果窗,门在场与否不可区分)/r2079b、c、d(不触茎结果面)全不误伤。
//   NEG-2 = 沙漠准入退化(world.cpp villageSiteOk 群系门退回仅 Plains 形态,编译仍绿)→ 恰红 =
//     {r2079c}(探针顺序 seed 扫描全域无 Desert 站点 → fail-visible [probe/desert])。平原站点表/
//     模板/桥面零变化 → r2078a-d 全绿;r2079a(茎面)/r2079b(瓜负面)/r2079d(钉族)不触村庄选择面。
//   豁免面: r2079d **不钉**单果门行(`if (hasFruit) continue;`)与群系准入行(两处 NEG 敏感行);
//     road guard 两针已由 r2078d 修订后钉住,本段不重复记账。
namespace {

constexpr int kW = 80, kD = 80, kH = 96;      // r2079b/c: 80×80×96(worldgen 全 pass 含矿井/patch)
constexpr int kSmW = 48, kSmD = 48, kSmH = 96; // r2079a: 48×48×96 s82(section11+ 同门)

inline void initCloseoutWorld(World &w, int seed)
{
    w.setWidth(kW);
    w.setDepth(kD);
    w.setHeight(kH);
    w.setSeed(seed); // 末位 setter = 全尺寸 generate(前三 setter 在小尺寸上各跑一次,末次为准)
}

inline void initSmallWorld(World &w, int seed)
{
    w.setWidth(kSmW);
    w.setDepth(kSmD);
    w.setHeight(kSmH);
    w.setSeed(seed);
}

// 散布骰子镜像(world.cpp tickCropGrowth 同式:窗口 k,阶段 s,格 c)。
inline bool fruitDiceHit(const World &w, int k, int stage, int x, int y, int z)
{
    const int mixedSeed = int(quint32(w.seed()) ^ (quint32(k) * 0x9E3779B9u));
    return int(w.hashVoxel(mixedSeed, x, y * 7 + stage, z) & 0xFFFFu) % 100 < 6;
}

// 站点完好面检查(section71 villageSiteIntact 同门,群系无关:井[台面四沿中圆石 + 水柱 S..S-3 +
//   井底圆石]+ 槽 0 小屋[地板/门洞向心双 Air/平顶/火把;**不含墙体行**——探针面与模板断言解耦,
//   同 section71 NEG 豁免设计]+ 农田[水道/湿耕地]+ 道路臂样格带域扫砂砾)。
inline bool closeoutSiteIntact(const World &w, int cx, int cz, int S)
{
    const int h0x = cx + World::kVillageHutSlots[0][0];
    const int h0z = cz + World::kVillageHutSlots[0][1];
    const int dirX = World::kVillageHutSlots[0][0] > 0 ? -1 : 1;
    const auto roadAt = [&w](int px, int pz, int sy) {
        for (int yy = sy + 3; yy >= sy - 4; --yy)
            if (w.blockAt(px, yy, pz) == BR::Gravel)
                return true;
        return false;
    };
    if (w.blockAt(cx, S, cz) != BR::Water)
        return false; // 井锚(水柱顶)
    for (const int d : { -2, 2 })
        if (w.blockAt(cx + d, S, cz) != BR::Cobble || w.blockAt(cx, S, cz + d) != BR::Cobble)
            return false;
    for (int dy = 1; dy <= 3; ++dy)
        if (w.blockAt(cx, S - dy, cz) != BR::Water)
            return false;
    if (w.blockAt(cx, S - 4, cz) != BR::Cobble)
        return false;
    if (w.blockAt(h0x, S, h0z) != BR::Planks)
        return false;
    for (int dy = 1; dy <= 2; ++dy)
        if (w.blockAt(h0x + dirX * 2, S + dy, h0z) != BR::Air)
            return false;
    if (w.blockAt(h0x, S + 3, h0z) != BR::Planks
        || w.blockAt(h0x, S + 1, h0z + 1) != BR::Torch)
        return false;
    if (w.blockAt(cx + 11, S, cz) != BR::Water
        || w.blockAt(cx + 11, S, cz + 1) != BR::Farmland)
        return false;
    return roadAt(cx + 3, cz, S) && roadAt(cx, cz + 3, S);
}

// r2079c 探针(顺序 seed 1..120 扫描 + 双证据齐即早退,一次静态):找首个**完好沙漠站点**(站心
//   群系 = Desert + 完好门),同时收集:任一已扫 seed 是否存在 Plains 站点(双群系证据)/任一已扫
//   seed 站点足迹两两相交(零容忍)。seed 13 实测命中完好沙漠站点 (58,59)(探针日志 matrix_t1109_
//   probe2.log 在案:seed 3/11 平原站点先行入证据,seed 13 沙漠完好站点早退)。
struct DesertProbe
{
    int seed = -1;    // 命中 seed(-1 = 表内无完好沙漠站点 = 腿内 fail-visible)
    int scanned = 0;  // 实际扫描 seed 数(诊断)
    bool plainsSeen = false; // 全表任一 Plains 站点(双群系生成证据)
    bool desertSeen = false; // 全表任一 Desert 站点(允许未过完好门,仅统计群系准入生效面)
    bool overlap = false;    // 任一 seed 出现站点足迹相交(零容忍)
    int cx = 0, cz = 0, y = 0; // 完好沙漠站点中心与站心地表格
};

inline DesertProbe desertProbe()
{
    static const DesertProbe p = [] {
        DesertProbe out;
        for (int seed = 1; seed <= 120; ++seed) {
            out.scanned++;
            World w;
            initCloseoutWorld(w, seed);
            // 站点足迹两两不相交(同 seed 内;含 Plains/Desert 混排——交叠即破 t1108 jitter 界口径)。
            const int n = w.structureRegionCount(World::StructureVillage);
            QVector<QPair<int, int>> centers;
            centers.reserve(n);
            for (int i = 0; i < n; ++i) {
                const QVariantList r = w.structureRegion(World::StructureVillage, i);
                if (r.size() != 6)
                    continue;
                centers.append({ (r[0].toInt() + r[3].toInt()) / 2,
                                 (r[2].toInt() + r[5].toInt()) / 2 });
                const int cx = centers.last().first, cz = centers.last().second;
                // 群系编码经由公开 biomeIdAt()（World::Biome 枚举本体私有；int 编码单一权威
                //   world.h: 0=Plains 1=Hills 2=Desert 3=Forest 4=Snowy 5=Swamp 6=Jungle）。
                const int b = w.biomeIdAt(cx, cz);
                if (b == 2) // Desert
                    out.desertSeen = true;
                if (b == 0) // Plains
                    out.plainsSeen = true;
                if (b == 2 && out.seed < 0
                    && closeoutSiteIntact(w, cx, cz, r[4].toInt() - 4)) {
                    out.seed = seed;
                    out.cx = cx;
                    out.cz = cz;
                    out.y = r[4].toInt() - 4;
                }
            }
            for (int i = 0; i < centers.size() && !out.overlap; ++i)
                for (int j = i + 1; j < centers.size() && !out.overlap; ++j) {
                    const int ddx = qAbs(centers[i].first - centers[j].first);
                    const int ddz = qAbs(centers[i].second - centers[j].second);
                    if (ddx <= 2 * World::kVillageHalf && ddz <= 2 * World::kVillageHalf)
                        out.overlap = true;
                }
            if (out.seed >= 0 && out.plainsSeen)
                break; // 早退:双群系证据齐 + 完好沙漠站已锁定
        }
        return out;
    }();
    return p;
}

// r2079b 探针(早退):首个满足「矿井 ≥1 + 全图甜瓜 = 0 + 野生南瓜 ≥1」的 seed(甜瓜负面 + 南瓜
//   patch 对比锚同世界)。返回 false = 表内无合格 seed(fail-visible)。
struct MelonProbe
{
    int seed = -1;
    int mineshafts = 0;
    int pumpkins = 0;
};

inline MelonProbe melonProbe()
{
    static const MelonProbe p = [] {
        static const int kSeeds[] = { 82, 166, 42, 1337, 7, 99 };
        for (const int seed : kSeeds) {
            World w;
            initCloseoutWorld(w, seed);
            MelonProbe out;
            out.mineshafts = w.structureRegionCount(World::StructureMineshaft);
            if (out.mineshafts < 1)
                continue;
            int melons = 0, pumpkins = 0;
            for (int x = 0; x < kW; ++x)
                for (int z = 0; z < kD; ++z)
                    for (int y = 0; y < kH; ++y) {
                        const quint8 b = w.blockAt(x, y, z);
                        if (b == BR::Melon)
                            ++melons;
                        else if (b == BR::Pumpkin)
                            ++pumpkins;
                    }
            if (melons != 0)
                continue; // 结构性不可能,防御面(出现即本单前提被打破,如实红)
            out.pumpkins = pumpkins;
            if (pumpkins < 1)
                continue;
            out.seed = seed;
            return out;
        }
        return MelonProbe{};
    }();
    return p;
}

// 源钉根路径(section70/71 同门:applicationDirPath/../src)。
inline QString srcRootForCloseoutPins()
{
    return QDir(QCoreApplication::applicationDirPath()
                + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
}

// 裸文本锚注在场检查(注释体锚注用——pinSet 剥注释会失配,故走原始读;仅作登记注在场断言)。
inline bool rawContains(const QString &path, const QString &needle)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return QString::fromUtf8(f.readAll()).contains(needle);
}

} // namespace

void MatrixRun::section72_t1109_closeout()
{
    // ── r2079a:瓜茎收口柱(NEG-1 敏感面)────────────────────────────────────────────────────
    //   场景 A(单果门):成熟瓜茎 + 预置同型邻果 → 泵至首个命中窗,环内恒 1 果(NEG-1 摘门 → 门位
    //   四向扫描落第二果 → 恰红);茎固定格 + 保持成熟(爬蔓面收口行为级)。场景 B(门只认同型果):
    //   南瓜茎 + 预置**异型**西瓜邻果 → 照常结果 @期望槽,南瓜带槽位向刻脸 state(朝向面),西瓜原位。
    //   场景 C(西瓜果 state 面):空环瓜茎 → 恰 1 瓜 state=0。
    runLeg("r2079a melon-stem close-out column (a mature melon stem with an adjacent melon"
        " never fruits again across its reachable dice-hit windows keeping the stem fixed on"
        " its own cell and mature, a pumpkin stem with a foreign melon neighbour still drops"
        " exactly one pumpkin onto the deterministic hash-picked free slot with the carved"
        " face state pointing along that slot direction away from the stem, and a free melon"
        " stem answers exactly one melon with face state zero)", [&]() {
        bool ok = true;
        QString diag;

        // ── 场景 A:单果门(同型邻果 → 不再结果)──
        int gateWin = -1;
        bool gateOk = false;
        {
            World w;
            initSmallWorld(w, 82);
            for (int x = 22; x <= 26; ++x)
                for (int z = 22; z <= 26; ++z)
                    w.setBlock(x, 80, z, BR::Farmland, 0);
            w.setBlock(24, 81, 24, BR::MelonStem, 7);           // 成熟瓜茎
            w.setBlock(25, 81, 24, BR::Melon, 0);               // 预置同型邻果(+X)
            for (int k = 0; k < 3000 && gateWin < 0; ++k)
                if (fruitDiceHit(w, k, 7, 24, 81, 24))
                    gateWin = k;
            ok = ok && gateWin >= 0;
            if (gateWin < 0)
                diag += QStringLiteral("[gate-win]");
            if (gateWin >= 0) {
                for (int k = 0; k <= gateWin; ++k)
                    for (int c = 0; c < 25; ++c)
                        w.tickCropGrowth(); // 25 tick = 1 窗(r2073b 同门)
                int mx = -1, mz = -1, n = 0;
                static constexpr int kR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (const auto &o : kR)
                    if (w.blockAt(24 + o[0], 81, 24 + o[1]) == BR::Melon) {
                        ++n;
                        mx = 24 + o[0];
                        mz = 24 + o[1];
                    }
                gateOk = n == 1 && mx == 25 && mz == 24                       // 预置果原位,无第二果
                    && w.blockAt(24, 81, 24) == BR::MelonStem                 // 茎固定格不爬蔓
                    && w.stateAt(24, 81, 24) == BR::WheatCropStageMax;        // 保持成熟
                ok = ok && gateOk;
                if (!gateOk)
                    diag += QStringLiteral("[gate n=%1@(x%2,z%3)]").arg(n).arg(mx).arg(mz);
            }
        }

        // ── 场景 B:门只认同型果 + 南瓜结果朝向 state(脸背茎)──
        bool faceOk = false;
        {
            World w;
            initSmallWorld(w, 82);
            for (int x = 36; x <= 40; ++x)
                for (int z = 36; z <= 40; ++z)
                    w.setBlock(x, 80, z, BR::Farmland, 0);
            w.setBlock(38, 81, 38, BR::PumpkinStem, 7);          // 成熟南瓜茎
            w.setBlock(37, 81, 38, BR::Melon, 0);                // 预置**异型**邻果(-X,不挡南瓜茎)
            int win = -1;
            for (int k = 0; k < 3000 && win < 0; ++k)
                if (fruitDiceHit(w, k, 7, 38, 81, 38))
                    win = k;
            ok = ok && win >= 0;
            if (win < 0)
                diag += QStringLiteral("[face-win]");
            if (win >= 0) {
                // 期望槽位/朝向镜像(与 world.cpp 3b 同式):start=(h>>16)&3,序 +X/-X/+Z/-Z 环绕,
                //   首 Air + 落地面 Farmland 者;期望 face = 命中槽方向序号。
                const int mixedSeed = int(quint32(w.seed()) ^ (quint32(win) * 0x9E3779B9u));
                const quint32 h = w.hashVoxel(mixedSeed, 38, 81 * 7 + 7, 38);
                const int start = int((h >> 16) & 3u);
                static constexpr int kDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                int ex = -1, ez = -1, eDir = -1;
                for (int d = 0; d < 4; ++d) {
                    const int dir = (start + d) & 3;
                    const int nx = 38 + kDir[dir][0], nz = 38 + kDir[dir][1];
                    if (w.blockAt(nx, 81, nz) == BR::Air
                        && w.blockAt(nx, 80, nz) == BR::Farmland) {
                        ex = nx;
                        ez = nz;
                        eDir = dir;
                        break;
                    }
                }
                for (int k = 0; k <= win; ++k)
                    for (int c = 0; c < 25; ++c)
                        w.tickCropGrowth();
                int px2 = -1, pz2 = -1, np = 0, nm = 0;
                for (const auto &o : kDir) {
                    const quint8 b = w.blockAt(38 + o[0], 81, 38 + o[1]);
                    if (b == BR::Pumpkin) {
                        ++np;
                        px2 = 38 + o[0];
                        pz2 = 38 + o[1];
                    } else if (b == BR::Melon) {
                        ++nm;
                    }
                }
                faceOk = win >= 0 && np == 1 && nm == 1            // 恰 1 南瓜 + 预置西瓜原位
                    && px2 == ex && pz2 == ez                       // @期望槽位
                    && eDir >= 0
                    && w.stateAt(px2, 81, pz2) == eDir              // 刻脸 state = 槽位向(脸背茎)
                    && w.blockAt(38, 81, 38) == BR::PumpkinStem
                    && w.stateAt(38, 81, 38) == BR::WheatCropStageMax;
                ok = ok && faceOk;
                if (!faceOk)
                    diag += QStringLiteral("[face np=%1 nm=%2@(x%3,z%4) want(x%5,z%6,dir%7) st=%8]")
                                .arg(np).arg(nm).arg(px2).arg(pz2).arg(ex).arg(ez).arg(eDir)
                                .arg(px2 > 0 ? w.stateAt(px2 > 0 ? px2 : 0, 81, pz2 > 0 ? pz2 : 0) : -1);
            }
        }

        // ── 场景 C:空环瓜茎 → 恰 1 瓜 state=0(西瓜无刻面)──
        bool melonFaceOk = false;
        {
            World w;
            initSmallWorld(w, 82);
            for (int x = 22; x <= 26; ++x)
                for (int z = 22; z <= 26; ++z)
                    w.setBlock(x, 80, z, BR::Farmland, 0);
            w.setBlock(24, 81, 24, BR::MelonStem, 7);
            int win = -1;
            for (int k = 0; k < 3000 && win < 0; ++k)
                if (fruitDiceHit(w, k, 7, 24, 81, 24))
                    win = k;
            ok = ok && win >= 0;
            if (win < 0)
                diag += QStringLiteral("[m-win]");
            if (win >= 0) {
                const int mixedSeed = int(quint32(w.seed()) ^ (quint32(win) * 0x9E3779B9u));
                const quint32 h = w.hashVoxel(mixedSeed, 24, 81 * 7 + 7, 24);
                const int start = int((h >> 16) & 3u);
                static constexpr int kDir[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                int ex = -1, ez = -1;
                for (int d = 0; d < 4; ++d) {
                    const int dir = (start + d) & 3;
                    const int nx = 24 + kDir[dir][0], nz = 24 + kDir[dir][1];
                    if (w.blockAt(nx, 81, nz) == BR::Air
                        && w.blockAt(nx, 80, nz) == BR::Farmland) {
                        ex = nx;
                        ez = nz;
                        break;
                    }
                }
                for (int k = 0; k <= win; ++k)
                    for (int c = 0; c < 25; ++c)
                        w.tickCropGrowth();
                int mx2 = -1, mz2 = -1, n = 0;
                for (const auto &o : kDir)
                    if (w.blockAt(24 + o[0], 81, 24 + o[1]) == BR::Melon) {
                        ++n;
                        mx2 = 24 + o[0];
                        mz2 = 24 + o[1];
                    }
                melonFaceOk = n == 1 && mx2 == ex && mz2 == ez
                    && w.stateAt(mx2, 81, mz2) == 0                 // 西瓜无刻面 → state 恒 0
                    && w.blockAt(24, 81, 24) == BR::MelonStem;
                ok = ok && melonFaceOk;
                if (!melonFaceOk)
                    diag += QStringLiteral("[mface n=%1@(x%2,z%3) want(x%4,z%5)]")
                                .arg(n).arg(mx2).arg(mz2).arg(ex).arg(ez);
            }
        }

        ok = ok && gateOk && faceOk && melonFaceOk;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2079a melon-stem close-out column (a mature melon stem with an adjacent"
               " melon never fruits again across its reachable dice-hit windows keeping the"
               " stem fixed on its own cell and mature, a pumpkin stem with a foreign melon"
               " neighbour still drops exactly one pumpkin onto the deterministic hash-picked"
               " free slot with the carved face state pointing along that slot direction away"
               " from the stem, and a free melon stem answers exactly one melon with face"
               " state zero)"
            << (ok ? QString() : diag);
    });

    // ── r2079b:矿区瓜收口柱(双 NEG 均不触达 = 对照腿)────────────────────────────────────────
    //   件二派工前提纠正留痕的负面世界钉:探针世界(矿井 ≥1)全图 614k 格甜瓜计数 = 0(1.0 无矿区
    //   野生瓜,任何结构/群系均无)同图野生南瓜 ≥1(patch 对比锚);world.h/.cpp 无任何 melon 生成
    //   pass(不对称源钉);loottable.cpp 矿井池瓜种行在案(1.0 唯一西瓜生存源收口)。
    runLeg("r2079b mineshaft melon close-out column (a generated world carrying at least one"
        " mineshaft answers zero melon blocks across every world cell while wild pumpkins do"
        " exist in the same world from the grassland patch pass, the world headers answer a"
        " pumpkin patch generation pass with no melon counterpart anywhere in the source, and"
        " the mineshaft chest pool keeps the melon seed row as the sole survival source of"
        " melons)", [&]() {
        bool ok = true;
        QString diag;
        const MelonProbe pr = melonProbe();
        ok = ok && pr.seed >= 0;
        if (pr.seed < 0)
            diag += QStringLiteral("[probe]");

        // 源钉:world.h 有南瓜 patch pass 声明、world.h/world.cpp 全文无 placeMelon 词根。
        const QString srcDir = srcRootForCloseoutPins();
        const QStringList missHdr = pinSet(srcDir + QStringLiteral("/World/world.h"), {
            SrcPin("pumpkin patch decl", "void placePumpkinPatches(int wx0 = 0, int wx1 = 0, int wz0 = 0, int wz1 = 0);", 1)});
        ok = ok && missHdr.isEmpty();
        if (!missHdr.isEmpty())
            diag += QStringLiteral("[hdr %1]").arg(missHdr.join(QLatin1Char(',')));
        const bool noMelonPass = !rawContains(srcDir + QStringLiteral("/World/world.h"),
                                                  QStringLiteral("placeMelon"))
            && !rawContains(srcDir + QStringLiteral("/World/world.cpp"),
                              QStringLiteral("placeMelon"));
        ok = ok && noMelonPass;
        if (!noMelonPass)
            diag += QStringLiteral("[noMelonPass]");

        // 矿井箱池瓜种行(loottable.cpp,1.0 唯一西瓜战利品行位;t1103 已交付,本柱收口复钉)。
        const QStringList missLt = pinSet(srcDir + QStringLiteral("/Game/loottable.cpp"), {
            SrcPin("mineshaft melon seed row", "{ RecipeRegistry::MelonSeedsId,   10, 2, 4 },", 1)});
        ok = ok && missLt.isEmpty();
        if (!missLt.isEmpty())
            diag += QStringLiteral("[lt %1]").arg(missLt.join(QLatin1Char(',')));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2079b mineshaft melon close-out column (a generated world carrying at least"
               " one mineshaft answers zero melon blocks across every world cell while wild"
               " pumpkins do exist in the same world from the grassland patch pass, the world"
               " headers answer a pumpkin patch generation pass with no melon counterpart"
               " anywhere in the source, and the mineshaft chest pool keeps the melon seed row"
               " as the sole survival source of melons)"
            << (ok ? QString() : diag);
    });

    // ── r2079c:沙漠村庄柱(NEG-2 敏感面)──────────────────────────────────────────────────────
    //   完好沙漠站点(站心 biome=Desert)逐构件坐标定格:井/屋/田/路全同平原模板(1.0 同构裁定行为级)
    //   + 沙面差异面(站点周边列真地表 = Sand;道路臂格恰落在 fBm 地表格 = 砂砾替换沙顶)+ 全表站点
    //   足迹两两不相交(含混排 seed)+ 双群系生成证据(Plains 与 Desert 站点均有)。
    runLeg("r2079c desert village column (a desert biome village site answers the same plains"
        " template faces with a cobble well holding its water shaft and cobble bottom, a plank"
        " hut with walls floor doorway roof and interior torch, a farm channel with wet"
        " farmland and hash staged wheat, and gravel roads on the four arms, the sand top"
        " terrain shows around the site with the road arms landing gravel exactly on the fbm"
        " surface cells, every scanned seed answers pairwise disjoint village footprints"
        " across plains and desert kinds, and both biome kinds generate sites across the"
        " probe table)", [&]() {
        bool ok = true;
        QString diag;
        const DesertProbe pr = desertProbe();
        ok = ok && pr.seed >= 0 && !pr.overlap && pr.plainsSeen && pr.desertSeen;
        if (pr.seed < 0)
            diag += QStringLiteral("[probe/desert]");
        if (pr.overlap)
            diag += QStringLiteral("[overlap]");
        if (!pr.plainsSeen)
            diag += QStringLiteral("[no-plains]");
        if (!pr.desertSeen)
            diag += QStringLiteral("[no-desert]");
        if (pr.seed < 0)
            diag += QStringLiteral("[scanned=%1]").arg(pr.scanned);

        if (pr.seed >= 0) {
            World w;
            initCloseoutWorld(w, pr.seed);
            const int cx = pr.cx, cz = pr.cz, S = pr.y;
            const auto roadAt = [&w](int px, int pz, int sy) {
                for (int yy = sy + 3; yy >= sy - 4; --yy)
                    if (w.blockAt(px, yy, pz) == BR::Gravel)
                        return true;
                return false;
            };

            // (C1) 水井(与平原同构):台面四沿中圆石;中芯水柱 S..S-3;井底 S-4 圆石。
            bool wellOk = true;
            for (const int d : { -2, 2 })
                wellOk = wellOk
                    && w.blockAt(cx + d, S, cz) == BR::Cobble
                    && w.blockAt(cx, S, cz + d) == BR::Cobble;
            for (int dy = 0; dy < 4; ++dy)
                wellOk = wellOk && w.blockAt(cx, S - dy, cz) == BR::Water;
            wellOk = wellOk && w.blockAt(cx, S - 4, cz) == BR::Cobble;
            ok = ok && wellOk;
            if (!wellOk) diag += QStringLiteral("[well]");

            // (C2) 小屋槽 0:地板/三面墙中格/门洞/室内/平顶/火把(全同平原模板——同构裁定行为级)。
            const int h0x = cx + World::kVillageHutSlots[0][0];
            const int h0z = cz + World::kVillageHutSlots[0][1];
            const int dirX = World::kVillageHutSlots[0][0] > 0 ? -1 : 1;
            bool hutOk = w.blockAt(h0x, S, h0z) == BR::Planks;
            for (int dy = 1; dy <= 2; ++dy)
                hutOk = hutOk
                    && w.blockAt(h0x - 2, S + dy, h0z) == BR::Planks
                    && w.blockAt(h0x, S + dy, h0z - 2) == BR::Planks
                    && w.blockAt(h0x, S + dy, h0z + 2) == BR::Planks
                    && w.blockAt(h0x + dirX * 2, S + dy, h0z) == BR::Air;
            hutOk = hutOk
                && w.blockAt(h0x, S + 1, h0z) == BR::Air
                && w.blockAt(h0x, S + 2, h0z) == BR::Air
                && w.blockAt(h0x, S + 3, h0z) == BR::Planks
                && w.blockAt(h0x, S + 1, h0z + 1) == BR::Torch;
            ok = ok && hutOk;
            if (!hutOk) diag += QStringLiteral("[hut]");

            // (C3) 农田:中行水道 + 湿耕地 state=3 + 小麦阶段 = hashVoxel 公式回算。
            bool farmOk = true;
            for (int dz = -3; dz <= 3; dz += 2) {
                const int fx = cx + 11, fz = cz + dz;
                farmOk = farmOk && w.blockAt(fx, S, fz) == BR::Farmland
                    && w.stateAt(fx, S, fz) == BR::FarmlandHydrationMax
                    && w.blockAt(fx, S + 1, fz) == BR::WheatCrop
                    && w.stateAt(fx, S + 1, fz) == int(w.hashVoxel(w.seed() + World::kVillageSeedOff,
                                                                   fx, S + 1, fz) % 8u);
            }
            farmOk = farmOk && w.blockAt(cx + 11, S, cz) == BR::Water;
            ok = ok && farmOk;
            if (!farmOk) diag += QStringLiteral("[farm]");

            // (C4) 道路四臂 + 沙面承接面(本单差异面):道路臂格砂砾恰落在 fBm 地表格(heightAt 处 =
            //      被替换的沙顶);站点周边无结构样列真地表 = Sand(证明沙漠沙面地形 + 道路扩 Sand 生效)。
            bool roadOk = true;
            for (int o = 3; o <= 5; ++o) {
                roadOk = roadOk
                    && roadAt(cx + o, cz, S) && roadAt(cx - o, cz, S)
                    && roadAt(cx, cz + o, S) && roadAt(cx, cz - o, S)
                    && w.blockAt(cx + o, w.heightAt(cx + o, cz), cz) == BR::Gravel
                    && w.blockAt(cx - o, w.heightAt(cx - o, cz), cz) == BR::Gravel
                    && w.blockAt(cx, w.heightAt(cx, cz + o), cz + o) == BR::Gravel
                    && w.blockAt(cx, w.heightAt(cx, cz - o), cz - o) == BR::Gravel;
            }
            ok = ok && roadOk;
            if (!roadOk) diag += QStringLiteral("[road]");

            bool sandOk = true;
            static constexpr int kOff[4][2] = { { 6, 3 }, { -6, -3 }, { 6, -3 }, { -6, 3 } };
            for (const auto &o : kOff) {
                const int px = cx + o[0], pz = cz + o[1];
                sandOk = sandOk
                    && w.blockAt(px, w.heightAt(px, pz), pz) == BR::Sand;
            }
            ok = ok && sandOk;
            if (!sandOk) diag += QStringLiteral("[sand]");

            // (C5) 农田水道守卫负例(水道格保持 Water 不被道路覆盖——平原同门)。
            const bool guardOk = w.blockAt(cx + 11, S, cz) == BR::Water;
            ok = ok && guardOk;
            if (!guardOk) diag += QStringLiteral("[guard]");
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2079c desert village column (a desert biome village site answers the same"
               " plains template faces with a cobble well holding its water shaft and cobble"
               " bottom, a plank hut with walls floor doorway roof and interior torch, a farm"
               " channel with wet farmland and hash staged wheat, and gravel roads on the four"
               " arms, the sand top terrain shows around the site with the road arms landing"
               " gravel exactly on the fbm surface cells, every scanned seed answers pairwise"
               " disjoint village footprints across plains and desert kinds, and both biome"
               " kinds generate sites across the probe table)"
            << (ok ? QString() : diag);
    });

    // ── r2079d:结构钉族 + 相邻族零污染(双 NEG 摘面全豁免 = 结构钉对照腿)───────────────────────
    //   新面源钉:果 id 分流行/朝向 state 行/结果写行(+ Sandstone 在场声明行;**单果门行与群系准入行
    //   不钉 = NEG 豁免面**;road guard 两针在 r2078d 修订后已钉不重复记账)。登记注裸文本锚(注释体,
    //   走 raw 读)。相邻族零污染值面(Count/图集/物品/mob/结构偏移/瓜族枚举全原值)。
    runLeg("r2079d structure pin family (the stem fruiting source pins hold the fruit id branch"
        " the facing state line and the fruit write row with the sandstone declaration row"
        " pinned present, the registry close-out note anchors and the gate and facing and road"
        " lineage notes are on file, and the neighbouring block count atlas slimeball villager"
        " egg mob ids melon family enum village seed offset and the four earlier structure"
        " offsets keep their original values)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcDir = srcRootForCloseoutPins();

        // (D1) 代码行源钉(NEG 豁免面不钉:单果门行/群系准入行/road guard 行[r2078d 已钉])。
        const QStringList missWc = pinSet(srcDir + QStringLiteral("/World/world.cpp"), {
            SrcPin("fruit id branch", "const quint8 fruitId = (f.id == BlockRegistry::PumpkinStem)", 1),
            SrcPin("facing state line", "const quint8 fruitState = (fruitId == BlockRegistry::Pumpkin)", 1),
            SrcPin("fruit write row", "anyChange |= setWaterSilent(nx, f.y, nz, fruitId, fruitState);", 1)});
        ok = ok && missWc.isEmpty();
        if (!missWc.isEmpty())
            diag += QStringLiteral("[wc %1]").arg(missWc.join(QLatin1Char(',')));
        const QStringList missHdr = pinSet(srcDir + QStringLiteral("/Core/blockregistry.h"), {
            SrcPin("sandstone decl", "Sandstone      = 41,", 1)});
        ok = ok && missHdr.isEmpty();
        if (!missHdr.isEmpty())
            diag += QStringLiteral("[br %1]").arg(missHdr.join(QLatin1Char(',')));
        const QStringList missHdr2 = pinSet(srcDir + QStringLiteral("/World/world.h"), {
            SrcPin("village site ok decl", "bool villageSiteOk(int cx, int cz) const;", 1)});
        ok = ok && missHdr2.isEmpty();
        if (!missHdr2.isEmpty())
            diag += QStringLiteral("[hdr %1]").arg(missHdr2.join(QLatin1Char(',')));

        // (D2) 登记注裸文本锚(注释体,pinSet 剥注释会失配 → 原始读)。
        const bool anchors = rawContains(srcDir + QStringLiteral("/Core/blockregistry.h"),
                                             QStringLiteral("茎蔓完整原型收口（t1109"))
            && rawContains(srcDir + QStringLiteral("/World/world.cpp"),
                              QStringLiteral("t1109 单果门"))
            && rawContains(srcDir + QStringLiteral("/World/world.cpp"),
                              QStringLiteral("t1109 结果朝向 state"))
            && rawContains(srcDir + QStringLiteral("/World/world.cpp"),
                              QStringLiteral("t1109 lawful 钉修订 r2078d"));
        ok = ok && anchors;
        if (!anchors) diag += QStringLiteral("[anchors]");

        // (D3) 相邻族零污染(值面)。
        const bool neighOk = int(BR::Count) == 157 // t1111 lawful 前移：154→157（砂岩楼梯/石·砂岩台阶尾部追加）
            && int(BR::AtlasTileCount) == 207
            && int(BR::Melon) == 149
            && int(BR::MelonStem) == 150
            && int(BR::PumpkinStem) == 151
            && int(BR::JackOLantern) == 152
            && int(BR::Cauldron) == 153
            && int(BR::Pumpkin) == 100
            && int(BR::Sandstone) == 41
            && int(BR::CutSandstone) == 105
            && int(BR::WheatCropStageMax) == 7
            && RecipeRegistry::SlimeBallId == 0x28C
            && RecipeRegistry::SpawnEggVillagerId == 0x28E
            && int(EntityManager::MobVillager) == 22
            && int(EntityManager::MobSlime) == 21
            && World::kVillageSeedOff == 26089
            && World::kVillageHalf == 14
            && int(World::StructureVillage) == 4
            && int(World::StructureKindCount) == 5
            && World::kDungeonSeedOff == 12037
            && World::kMineshaftSeedOff == 15047
            && World::kDesertTempleSeedOff == 19487
            && World::kJungleTempleSeedOff == 22617;
        ok = ok && neighOk;
        if (!neighOk) diag += QStringLiteral("[neigh]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2079d structure pin family (the stem fruiting source pins hold the fruit id"
               " branch the facing state line and the fruit write row with the sandstone"
               " declaration row pinned present, the registry close-out note anchors and the"
               " gate and facing and road lineage notes are on file, and the neighbouring"
               " block count atlas slimeball villager egg mob ids melon family enum village"
               " seed offset and the four earlier structure offsets keep their original"
               " values)"
            << (ok ? QString() : diag);
    });
}
