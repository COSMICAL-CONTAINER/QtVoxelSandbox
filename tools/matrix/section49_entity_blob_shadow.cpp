#include "matrix_helpers.h"

#include "entityshadowfield.h"  // 被测面：blob 阴影采样权威（Game 层直编——本段 CMake 附加已列）
#include "blobshadowgeometry.h" // 被测面：贴片几何专用面（Renderer 直编——同上）
#include "worldfacade.h"        // 期望值对账面：supportTopYAt 真顶（与采样权威同源同语义读）
#include "chunklifecycle.h"     // sparse 物化门读（r2023c 按需物化缝的 Loaded 判据）

#include <cmath>

// t1075 blob 软阴影探针段（4 腿；filter 词 r2048；矩阵 723→723+4）。置尾先例沿用（接
// section48，runAll 末执行，rig 世界零接触）。
//
// 任务契约（R21.1 P0 队列，用户 2026-09-20 真机症状⑤：「还有就是没有玩家的阴影」）。feature
// 单 = MC 式软阴影贴片（blob shadow）：半透明暗椭圆贴片贴地渲染，玩家与生物同覆盖。
//
// 实现面（选型/落选全录于两新文件头注释，此处只录可测面锚点）：
//   · src/Game/entityshadowfield.h —— EntityShadowField 采样权威：15Hz 空转门节拍（running
//     gate = QML 绑 worldRunning）+ 指纹差分；贴地语义 = 脚位中心列向下最近承载面
//     （WorldFacade::supportTopYAt——本单加行收窄转发，单一权威不复制）+ chunkExistsAt
//     未物化缺席门（fixed 全物化恒过门 = 两模式语义一致）；覆盖域 = 玩家 + Kind::Mob 槽
//     （掉落物/投射物族不动——EntityStore 单一权威契约勿动）。
//   · src/Renderer/blobshadowgeometry.h —— BlobShadowGeometry 专用面：八边形顶点色径向
//     渐变 disc（中心 alpha 满/缘 0），全实体合单 Model 单 draw；setQuads 指纹早退 +
//     quadCount 可数通道。QML 只接五条指针 + 一 Model，零世界查询零逐帧扫（派工不变量①）。
//
// 腿面设计（headless 可测面 = C++ 采样权威 + 几何可数通道；透明度/大小观感 = 纯视觉，如实
// 标「待实机确认」不入腿面）：
//   「平地贴地承重墙」→ r2048a（fixed 平地：玩家 + 双 mob 脚位恰在支撑顶 → quad 贴地 Y ==
//     支撑真顶 + 抬升 / alpha 满档 / 半径 = halfW×1.25 钳值 + 指纹差分零变化零 bump +
//     几何可数通道[顶点/索引字节量 == 拓扑常量 × quad 数]）——NEG-2（玩家收纳摘除）恰红；
//   「悬崖边缘 + 腾空衰减」→ r2048b（悬浮块顶贴地满档 → 同高悬空邻列窗内深支撑淡出
//     [h=6 → alpha=kBase×0.25] → 中空 h=3 → alpha=kBase×0.625 → 超窗 h=9 缺席——四段单调
//     衰减链一次走查）——NEG-1（高度衰减摘除：alpha 恒满档）恰红；
//   「未物化缺席 + 两模式贴地一致」→ r2048c（sparse radius-0 同 seed 孪生：materialize 单
//     chunk 后同坐标玩家 quad 贴地 Y/alpha/半径 fixed≡sparse 逐位 + 未物化 chunk 玩家缺席 +
//     同坐标 fixed 在场——两模式语义一致三面）——NEG-1 不误伤（同坐标面 h=0）；NEG-2 恰红；
//   「结构钉」→ r2048d（未物化门/真顶权威读/覆盖域/空转门/索引成对契约/沿收口/QML 接线面/
//     材质契约/QML_NAMED_ELEMENT 注册族源钉 + Main.qml 零对地采样反探 + EntityStore 零触碰
//     反探[不变量②]）——两 NEG 均不误伤（钉面避开被摘行）。
// 阴性面设计（双变异双还原，手工 Edit 做/手工 Edit 还原；存证 build/ 终名日志
//   matrix_r2048_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘高度衰减（entityshadowfield.cpp sampleQuad 的 alpha 行变异为恒 kBaseAlpha）→
//     声明红面 {r2048b}[淡出值断言 0.125/0.3125 红成 0.5]。r2048a 不误伤（h=0 满档同值）；
//     r2048c 不误伤（parity/缺席面 h=0 同值）；r2048d 不误伤（钉面不含 alpha 行字面）。
//   NEG-2 摘玩家收纳（update() 的 m_player 采样块注释化）→ 声明红面 {r2048a, r2048b,
//     r2048c}[玩家 quad 全灭 → quadCount/玩家 quad 断言红；mob 面仍绿 = 红面归因玩家侧]。
//     r2048d 不误伤（钉面不含 m_player 块内字面）。
void MatrixRun::section49_entity_blob_shadow()
{
    constexpr int kW = 48, kD = 48, kHh = 96, kSeed = 82;

    // 源码钉根（应用 exe 同级 src/ —— section47 先例同式派生）。
    const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
        + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
    const QString fieldCpp = srcRoot + QStringLiteral("/Game/entityshadowfield.cpp");
    const QString fieldH = srcRoot + QStringLiteral("/Game/entityshadowfield.h");
    const QString geoCpp = srcRoot + QStringLiteral("/Renderer/blobshadowgeometry.cpp");
    const QString geoH = srcRoot + QStringLiteral("/Renderer/blobshadowgeometry.h");
    const QString mainQml = srcRoot + QStringLiteral("/ui/Main.qml");
    const QString storeCpp = srcRoot + QStringLiteral("/Entities/entitystore.cpp");
    const QString storeH = srcRoot + QStringLiteral("/Entities/entitystore.h");

    // fresh fixed 小世界（section11 四 setter 同门——各 setter 触发一次 generate）。
    const auto initWorld = [](World &w) {
        w.setWidth(kW);
        w.setDepth(kD);
        w.setHeight(kHh);
        w.setSeed(kSeed);
        w.setWeatherState(0); // Weather::Clear——零 RNG 进探针窗（section11 同门）
        w.setWeatherRemainingSec(3600.0f);
    };

    // 平地柱挑选：地表 Grass + 3×3 邻域同高全 Grass + 3×3 全部柱顶 11 格实测空气（r2048b
    //   悬浮块/腾空柱面净空前提——lessons「某高度以上必空」反教训：以实测净空为准，绝不假设
    //   高度带；邻域树冠带列 = 悬空衰减腿的假支撑源，必须整域净空）。
    const auto pickFlat = [](World &w, int &ox, int &oy, int &oz, QString *diag = nullptr) {
        int rNotGrass = 0, rUneven = 0, rBlocked = 0;
        for (int z = 6; z <= kD - 7; ++z) {
            for (int x = 6; x <= kW - 7; ++x) {
                const int top = w.heightAt(x, z);
                if (top <= 2 || top >= kHh - 14) { ++rBlocked; continue; }
                bool ok = true;
                for (int dz = -1; dz <= 1 && ok; ++dz)
                    for (int dx = -1; dx <= 1 && ok; ++dx) {
                        const int t2 = w.heightAt(x + dx, z + dz);
                        if (t2 != top
                            || w.blockAt(x + dx, t2, z + dz) != BR::Grass) { ok = false; break; }
                        for (int y = t2 + 1; y <= t2 + 11 && ok; ++y)
                            if (w.blockAt(x + dx, y, z + dz) != BR::Air) ok = false;
                    }
                if (!ok) { ++rUneven; continue; }
                bool tall = true;
                for (int y = top + 1; y <= top + 11 && tall; ++y)
                    if (w.blockAt(x, y, z) != BR::Air) tall = false;
                if (!tall) { ++rBlocked; continue; }
                ox = x; oy = top; oz = z;
                if (diag)
                    *diag += QStringLiteral("[pick x=%1 z=%2 top=%3] ").arg(x).arg(z).arg(top);
                return true;
            }
        }
        if (diag)
            *diag += QStringLiteral("[pick none rNotGrass=%1 rUneven=%2 rBlocked=%3] ")
                         .arg(rNotGrass).arg(rUneven).arg(rBlocked);
        return false;
    };

    // quad 定位（按中心 x/z 匹配；±1e-3；返回下标或 -1）。
    const auto findQuad = [](const BlobShadowGeometry &geo, float x, float z) {
        for (int i = 0; i < geo.quadCount(); ++i) {
            const ShadowBlobQuad &q = geo.quadAt(i);
            if (std::fabs(q.x - x) < 1e-3f && std::fabs(q.z - z) < 1e-3f)
                return i;
        }
        return -1;
    };

    // rig（每腿 fresh；矩阵不挂 QTimer——时钟缝 = 直调 update()；QTimer 空转门语义由
    // r2048d 源钉承载。setWorld/setGeometry 构造期接，setPlayer/setMobs 玩家态就绪后接）。
    struct Rig
    {
        World w;
        PlayerController pc;
        EntityManager ents;
        BlobShadowGeometry geo;
        EntityShadowField field;
        Rig()
        {
            field.setWorld(&w);
            field.setGeometry(&geo);
            pc.setWorld(&w);
        }
        // 玩家态就绪后接线（setPlayer 沿立即 update() 采样一次——首拍确定性）。
        void attach()
        {
            field.setPlayer(&pc);
            field.setMobs(&ents);
        }
    };

    // ── r2048a：平地贴地承重墙（玩家 + 双 mob 贴地 Y/alpha/半径 + 指纹差分 + 几何可数通道）──
    runLeg(QStringLiteral("r2048a flat-ground load-bearing wall (with the player and two mobs"
        " resting exactly on a verified flat grass support the shadow field emits one quad per"
        " entity with the ground Y equal to the support true top plus the lift, full-strength"
        " alpha, the player fixed radius and the mob radius clamped from the collision half"
        " width, a second update with nothing moved bumps nothing, and the geometry channel"
        " reports the exact vertex and index byte counts for three quads)"), [&]() {
        bool ok = true;
        QString diag;

        Rig rig;
        initWorld(rig.w);
        int px = 0, py = 0, pz = 0;
        const bool picked = pickFlat(rig.w, px, py, pz, &diag);
        ok = ok && picked;
        if (!picked)
            diag += QStringLiteral("[pick none] ");

        rig.attach();
        // 玩家脚位恰在支撑顶（loadSavedState 直设 m_pos，无物理落体——确定性贴地位形）。
        rig.pc.loadSavedState(px + 0.5f, float(py + 1), pz + 0.5f, 0.0f, 0.0f, 2);
        // 双 mob：spawnMobCore 公式 pos=(x+0.5, y+halfH, z+0.5) → 脚位 = 格底 = 支撑顶（y 传
        //   支撑上方空气格）。无 tick → 无 AI 位移，全确定。
        const int s1 = rig.ents.spawnMobTyped(px + 1, py + 1, pz, EntityManager::MobPig,
                                              QStringLiteral("#ee9999"), 10);
        const int s2 = rig.ents.spawnMobTyped(px, py + 1, pz + 1, EntityManager::MobSheep,
                                              QStringLiteral("#ee9999"), 10);
        const bool spawnOk = s1 >= 0 && s2 >= 0;
        ok = ok && spawnOk;
        if (!spawnOk)
            diag += QStringLiteral("[spawn %1/%2] ").arg(s1).arg(s2);

        const int rev0 = rig.field.shadowRevision();
        rig.field.update();

        const bool countOk = rig.geo.quadCount() == 3;
        ok = ok && countOk;
        if (!countOk)
            diag += QStringLiteral("[count n=%1] ").arg(rig.geo.quadCount());

        // 玩家 quad：贴地 Y = 支撑真顶 + 抬升；alpha 满档；半径 = 玩家固定值。
        const int qi = countOk ? findQuad(rig.geo, px + 0.5f, pz + 0.5f) : -1;
        const bool playerOk = qi >= 0;
        float pY = -99, pA = -99, pR = -99;
        if (playerOk) {
            const ShadowBlobQuad &q = rig.geo.quadAt(qi);
            pY = q.y;
            pA = q.alpha;
            pR = q.radius;
        }
        const bool playerVals = playerOk
            && std::fabs(pY - float(py + 1) - EntityShadowField::kGroundLift) < 1e-4f
            && std::fabs(pA - EntityShadowField::kBaseAlpha) < 1e-6f
            && std::fabs(pR - EntityShadowField::kPlayerRadius) < 1e-6f;
        ok = ok && playerVals;
        if (!playerVals)
            diag += QStringLiteral("[player qi=%1 y=%2 a=%3 r=%4] ")
                        .arg(qi).arg(pY).arg(pA).arg(pR);

        // mob quad ×2：贴地 Y / alpha 满档 / 半径 = clamp(halfW×1.25)（期望值由同源输入派生）。
        for (const int s : { s1, s2 }) {
            const QVector3D p = rig.ents.posAt(s);
            const float halfW = rig.ents.radiusAt(s);
            const float wantR = std::clamp(halfW * EntityShadowField::kMobRadiusFactor,
                                           EntityShadowField::kMobRadiusMin,
                                           EntityShadowField::kMobRadiusMax);
            const int mi = findQuad(rig.geo, p.x(), p.z());
            bool mobOk = mi >= 0;
            if (mobOk) {
                const ShadowBlobQuad &q = rig.geo.quadAt(mi);
                mobOk = std::fabs(q.y - float(py + 1) - EntityShadowField::kGroundLift) < 1e-4f
                    && std::fabs(q.alpha - EntityShadowField::kBaseAlpha) < 1e-6f
                    && std::fabs(q.radius - wantR) < 1e-6f;
            }
            ok = ok && mobOk;
            if (!mobOk)
                diag += QStringLiteral("[mob s=%1 mi=%2] ").arg(s).arg(mi);
        }

        // 指纹差分：零变化零重传零 bump（第二拍无任何位形变化）。
        rig.field.update();
        const bool fpOk = rig.field.shadowRevision() == rev0 + 1;
        ok = ok && fpOk;
        if (!fpOk)
            diag += QStringLiteral("[fp rev=%1 want=%2] ")
                        .arg(rig.field.shadowRevision()).arg(rev0 + 1);

        // 几何可数通道：3 quads → 顶点 3×9×7×4 字节 + 索引 3×24×4 字节（拓扑常量腿面）。
        const qsizetype wantV = qsizetype(3) * BlobShadowGeometry::kVertsPerQuad
            * BlobShadowGeometry::kFloatsPerVertex * qsizetype(sizeof(float));
        const qsizetype wantI = qsizetype(3) * BlobShadowGeometry::kIndicesPerQuad
            * qsizetype(sizeof(quint32));
        const bool bytesOk = rig.geo.vertexData().size() == wantV
            && rig.geo.indexData().size() == wantI;
        ok = ok && bytesOk;
        if (!bytesOk)
            diag += QStringLiteral("[bytes v=%1 want=%2 i=%3 want=%4] ")
                        .arg(rig.geo.vertexData().size()).arg(wantV)
                        .arg(rig.geo.indexData().size()).arg(wantI);

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2048a flat-ground load-bearing wall (player and two mobs"
                             " resting on a verified flat support emit per-entity quads with"
                             " ground Y at the support true top plus lift, full alpha, radii"
                             " from the player constant and the clamped mob half width, no"
                             " change no bump on a second update, and exact vertex/index byte"
                             " counts for three quads)"
                          << (ok ? QString() : diag);
    });

    // ── r2048b：悬崖边缘 + 腾空衰减（悬浮块顶满档 → 同高悬空邻列淡出 → 中空淡出 → 超窗缺席）──
    runLeg(QStringLiteral("r2048b cliff edge and airborne decay wall (standing on a floating"
        " block projects at the block top with full alpha, hovering at the same height over"
        " the neighbor grass column decays the alpha to a quarter strength onto the lower"
        " terrain, hovering three blocks up decays to five eighths, and hovering beyond the"
        " eight block window emits no quad at all)"), [&]() {
        bool ok = true;
        QString diag;

        Rig rig;
        initWorld(rig.w);
        int px = 0, py = 0, pz = 0;
        const bool picked = pickFlat(rig.w, px, py, pz, &diag);
        ok = ok && picked;
        if (!picked)
            diag += QStringLiteral("[pick none] ");

        rig.attach();
        // 悬浮块（中心列上方 +6）：本单 rig 面（setBlock 即写即查，腿内自建世界）。
        rig.w.setBlock(px, py + 6, pz, BR::Stone);
        const bool blockOk = rig.w.blockAt(px, py + 6, pz) == BR::Stone;
        ok = ok && blockOk;

        const float kEps = 1e-6f;
        // (i) 悬浮块顶贴地：h=0 → 满档，贴地 Y = 块顶 + 抬升（中心列语义落点在脚下承载面）。
        rig.pc.loadSavedState(px + 0.5f, float(py + 7), pz + 0.5f, 0.0f, 0.0f, 2);
        rig.field.update();
        int qi = findQuad(rig.geo, px + 0.5f, pz + 0.5f);
        bool onBlock = qi >= 0
            && std::fabs(rig.geo.quadAt(qi).y - float(py + 7) - EntityShadowField::kGroundLift) < 1e-4f
            && std::fabs(rig.geo.quadAt(qi).alpha - EntityShadowField::kBaseAlpha) < kEps;
        ok = ok && onBlock;
        if (!onBlock)
            diag += QStringLiteral("[onblock qi=%1 y=%2 a=%3] ")
                        .arg(qi)
                        .arg(qi >= 0 ? rig.geo.quadAt(qi).y : -99.f)
                        .arg(qi >= 0 ? rig.geo.quadAt(qi).alpha : -99.f);

        // (ii) 同高悬空邻列（flat 3×3 内，地形同高 py）：脚位 py+7、支撑 py+1 → h=6 窗内深支撑
        // 淡出 → alpha = kBase×0.25，贴地 Y = 地形真顶 + 抬升（悬崖边缘：悬空格贴更低承载面）。
        rig.pc.loadSavedState(px + 1.5f, float(py + 7), pz + 0.5f, 0.0f, 0.0f, 2);
        rig.field.update();
        qi = findQuad(rig.geo, px + 1.5f, pz + 0.5f);
        const float wantQuarter = EntityShadowField::kBaseAlpha * (1.0f - 6.0f / 8.0f);
        bool quarter = qi >= 0
            && std::fabs(rig.geo.quadAt(qi).y - float(py + 1) - EntityShadowField::kGroundLift) < 1e-4f
            && std::fabs(rig.geo.quadAt(qi).alpha - wantQuarter) < kEps;
        ok = ok && quarter;
        if (!quarter)
            diag += QStringLiteral("[quarter qi=%1 a=%2 want=%3] ")
                        .arg(qi)
                        .arg(qi >= 0 ? rig.geo.quadAt(qi).alpha : -99.f).arg(wantQuarter);

        // (iii) 中空三格：h=3 → alpha = kBase×0.625。
        rig.pc.loadSavedState(px + 1.5f, float(py + 4), pz + 0.5f, 0.0f, 0.0f, 2);
        rig.field.update();
        qi = findQuad(rig.geo, px + 1.5f, pz + 0.5f);
        const float wantFiveEighths = EntityShadowField::kBaseAlpha * (1.0f - 3.0f / 8.0f);
        bool fiveEighths = qi >= 0
            && std::fabs(rig.geo.quadAt(qi).y - float(py + 1) - EntityShadowField::kGroundLift) < 1e-4f
            && std::fabs(rig.geo.quadAt(qi).alpha - wantFiveEighths) < kEps;
        ok = ok && fiveEighths;
        if (!fiveEighths)
            diag += QStringLiteral("[fiveEighths qi=%1 a=%2 want=%3] ")
                        .arg(qi)
                        .arg(qi >= 0 ? rig.geo.quadAt(qi).alpha : -99.f).arg(wantFiveEighths);

        // (iv) 超窗缺席：h=9 ≥ kShadowMaxHeight(8) → 玩家 quad 全灭（无 mob 场景 → 集恒空）。
        rig.pc.loadSavedState(px + 1.5f, float(py + 10), pz + 0.5f, 0.0f, 0.0f, 2);
        rig.field.update();
        const bool gone = rig.geo.quadCount() == 0;
        ok = ok && gone;
        if (!gone)
            diag += QStringLiteral("[gone n=%1] ").arg(rig.geo.quadCount());

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2048b cliff edge and airborne decay wall (floating-block top"
                             " full alpha at the block top, same-height hover over the neighbor"
                             " column decays to a quarter onto the lower terrain, three-up"
                             " hover decays to five eighths, and beyond the window no quad)"
                          << (ok ? QString() : diag);
    });

    // ── r2048c：未物化缺席 + fixed/sparse 贴地语义一致（sparse radius-0 同 seed 孪生）────────
    runLeg(QStringLiteral("r2048c unmaterialized absence and two-mode ground parity (a radius"
        " zero sparse twin sharing the fixed world seed materializes one chunk and the player"
        " quad over that chunk matches the fixed twin bit for bit in ground Y alpha and radius,"
        " the same player position over an unmaterialized chunk emits nothing in the sparse"
        " world, and the identical coordinates in the fixed world still project because every"
        " fixed chunk is materialized)"), [&]() {
        bool ok = true;
        QString diag;

        // fixed 孪生（同 seed 同 dims）+ sparse radius-0（无预生成——r2023c 按需物化缝）。
        Rig rigFixed;
        initWorld(rigFixed.w);
        World::SparseWorldParams sp;
        sp.seed = kSeed;
        sp.coreWidth = kW;
        sp.coreDepth = kD;
        sp.height = kHh;
        sp.spawnPreGenerateRadius = 0;
        World wSparse = World(sp);
        PlayerController pcSparse;
        BlobShadowGeometry geoSparse;
        EntityShadowField fieldSparse;
        pcSparse.setWorld(&wSparse);
        fieldSparse.setWorld(&wSparse);
        fieldSparse.setGeometry(&geoSparse);
        fieldSparse.setPlayer(&pcSparse);

        int px = 0, py = 0, pz = 0;
        const bool picked = pickFlat(rigFixed.w, px, py, pz, &diag);
        ok = ok && picked;
        if (!picked)
            diag += QStringLiteral("[pick none] ");

        if (picked) {
            // sparse 按需物化玩家所在单 chunk（floorDiv 负坐标安全——本腿坐标非负，正域同门）。
            const int ccx = floorDiv(px, 16), ccz = floorDiv(pz, 16);
            const bool loaded = wSparse.loadChunkAt(ccx, ccz)
                && wSparse.chunks().lifecycleAt(ccx, ccz) == ChunkLifecycle::Loaded;
            ok = ok && loaded;
            if (!loaded)
                diag += QStringLiteral("[load cx=%1 cz=%2] ").arg(ccx).arg(ccz);

            // 同坐标玩家：fixed ≡ sparse 贴地 Y/alpha/半径 逐位（两模式语义一致）。
            rigFixed.attach();
            rigFixed.pc.loadSavedState(px + 0.5f, float(py + 1), pz + 0.5f, 0.0f, 0.0f, 2);
            pcSparse.loadSavedState(px + 0.5f, float(py + 1), pz + 0.5f, 0.0f, 0.0f, 2);
            rigFixed.field.update();
            fieldSparse.update();

            const int qf = findQuad(rigFixed.geo, px + 0.5f, pz + 0.5f);
            const int qs = findQuad(geoSparse, px + 0.5f, pz + 0.5f);
            bool parity = qf >= 0 && qs >= 0;
            if (parity) {
                const ShadowBlobQuad &a = rigFixed.geo.quadAt(qf);
                const ShadowBlobQuad &b = geoSparse.quadAt(qs);
                parity = std::memcmp(&a, &b, sizeof(ShadowBlobQuad)) == 0;
            }
            ok = ok && parity;
            if (!parity)
                diag += QStringLiteral("[parity qf=%1 qs=%2] ").arg(qf).arg(qs);

            // 未物化缺席：sparse 世界远端 chunk（与玩家 chunk 异位；核内 0..2 三选一——
            //   固定孪生域内，fixedPresent 检查可达）不物化 → 零 quad。
            //   远端柱 = fixed 孪生面实选（实心顶 + 顶上两格空气——supportTopYAt 落点近地表，
            //   排除海洋列「支撑深在窗底 → 缺席」的假阳性混淆面）。
            const int fcx = (ccx == 2) ? 0 : 2;
            const int fcz = (ccz == 2) ? 1 : 2;
            int fx = -1, fy = -1, fz = -1;
            bool farPicked = false;
            for (int lz = 2; lz < 14 && !farPicked; ++lz)
                for (int lx = 2; lx < 14 && !farPicked; ++lx) {
                    const int tx = fcx * 16 + lx, tz = fcz * 16 + lz;
                    const int t = rigFixed.w.heightAt(tx, tz);
                    if (t <= 2 || t >= kHh - 4)
                        continue;
                    const quint8 b = rigFixed.w.blockAt(tx, t, tz);
                    const bool solidTop = b == BR::Grass || b == BR::Dirt || b == BR::Stone
                        || b == BR::Sand || b == BR::Sandstone;
                    if (solidTop && rigFixed.w.blockAt(tx, t + 1, tz) == BR::Air
                        && rigFixed.w.blockAt(tx, t + 2, tz) == BR::Air) {
                        fx = tx; fy = t; fz = tz;
                        farPicked = true;
                    }
                }
            ok = ok && farPicked;
            if (!farPicked)
                diag += QStringLiteral("[farPick fcx=%1 fcz=%2] ").arg(fcx).arg(fcz);

            if (farPicked) {
                pcSparse.loadSavedState(fx + 0.5f, float(fy + 1), fz + 0.5f, 0.0f, 0.0f, 2);
                fieldSparse.update();
                const bool absent = geoSparse.quadCount() == 0;
                ok = ok && absent;
                if (!absent)
                    diag += QStringLiteral("[absent n=%1] ").arg(geoSparse.quadCount());

                // 同坐标 fixed 在场：fixed 全物化恒过门（两模式语义一致第三面）。
                rigFixed.pc.loadSavedState(fx + 0.5f, float(fy + 1), fz + 0.5f, 0.0f, 0.0f, 2);
                rigFixed.field.update();
                const bool fixedPresent = rigFixed.geo.quadCount() == 1
                    && findQuad(rigFixed.geo, fx + 0.5f, fz + 0.5f) >= 0;
                ok = ok && fixedPresent;
                if (!fixedPresent)
                    diag += QStringLiteral("[fixedPresent n=%1] ").arg(rigFixed.geo.quadCount());
            }
        }

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2048c unmaterialized absence and two-mode ground parity (the"
                             " sparse player quad over a materialized chunk matches the fixed"
                             " twin bit for bit, an unmaterialized chunk emits nothing, and the"
                             " same coordinates in the fixed world still project)"
                          << (ok ? QString() : diag);
    });

    // ── r2048d：结构钉（采样权威/几何契约/QML 接线面/注册族源钉 + 零对地采样反探 +
    //    EntityStore 零触碰反探[不变量②]）──────────────────────────────────────────────
    runLeg(QStringLiteral("r2048d structure pins (the sampling authority keeps the"
        " unmaterialized gate, the true-top support read, the mob-kind coverage and the idle"
        " gate expression, the geometry keeps the index-data pairing and the change signal,"
        " the QML surface keeps the pointer wiring with the blend vertex-color no-cull"
        " material contract, both new types carry their QML registration macros, and the"
        " anti-probes confirm Main.qml never samples the ground and the entity store was"
        " never touched by the shadow feature)"), [&]() {
        bool ok = true;
        QString diag;

        // 采样权威正面钉（剥注释口径——NEG-1 摘 alpha 行 / NEG-2 注释化 m_player 块均不触下列）。
        const QStringList missField = pinSet(fieldCpp,
            { SrcPin("unmaterialized gate", "if (!facade.chunkExistsAt(cx, cz))", 1),
                SrcPin("true-top support read", "facade.supportTopYAt(", 1),
                SrcPin("mob-kind coverage", "EntityManager::Mob", 1),
                SrcPin("idle gate expression", "m_running && m_world != nullptr", 1),
                SrcPin("fingerprint dispatch", "m_geometry->setQuads(", 1) });
        const bool fieldOk = missField.isEmpty();
        ok = ok && fieldOk;
        if (!fieldOk)
            diag += QStringLiteral("[field %1] ").arg(missField.join(QLatin1Char(',')));

        // 几何契约钉：索引成对上传（lessons t35）+ 沿收口单点。
        const QStringList missGeo = pinSet(geoCpp,
            { SrcPin("index data pairing", "setIndexData(ib);", 1),
                SrcPin("change signal emission", "emit quadsChanged();", 1) });
        const bool geoOk = missGeo.isEmpty();
        ok = ok && geoOk;
        if (!geoOk)
            diag += QStringLiteral("[geo %1] ").arg(missGeo.join(QLatin1Char(',')));

        // QML 接线面 + 材质契约钉（t439/t442 透明段 + NoLighting 可见性契约；minCount 按
        // Main.qml 全文件既有出现数下限钉——新面只增不灭）。
        const QStringList missQml = pinSet(mainQml,
            { SrcPin("field wiring", "EntityShadowField {", 1),
                SrcPin("geometry pointer wiring", "geometry: blobShadowGeometry", 1),
                SrcPin("world running gate wiring", "running: window.worldRunning", 2),
                SrcPin("vertex color channel", "vertexColorsEnabled: true", 3),
                SrcPin("blend alpha mode", "alphaMode: PrincipledMaterial.Blend", 3),
                SrcPin("no-cull material", "cullMode: PrincipledMaterial.CullNoCulling", 1) });
        const bool qmlOk = missQml.isEmpty();
        ok = ok && qmlOk;
        if (!qmlOk)
            diag += QStringLiteral("[qml %1] ").arg(missQml.join(QLatin1Char(',')));

        // 注册族钉（QML_NAMED_ELEMENT 缺失 = 运行期 is not a type，t179 族病）。
        const QStringList missReg = pinSet(fieldH,
            { SrcPin("field QML registration", "QML_NAMED_ELEMENT(EntityShadowField)", 1) });
        const QStringList missGeoReg = pinSet(geoH,
            { SrcPin("geometry QML registration", "QML_NAMED_ELEMENT(BlobShadowGeometry)", 1) });
        const bool regOk = missReg.isEmpty() && missGeoReg.isEmpty();
        ok = ok && regOk;
        if (!regOk)
            diag += QStringLiteral("[reg %1/%2] ")
                        .arg(missReg.join(QLatin1Char(',')))
                        .arg(missGeoReg.join(QLatin1Char(',')));

        // 反探（裸读文件口径——t1073 反探同门）：QML 零对地采样（对地采样点属权威面，QML 禁入
        // ——派工不变量①的呈现侧形态）；EntityStore 单一权威零触碰（不变量②——hadow 大小写
        // 中拼覆盖 Shadow/shadow 两词形）。
        const auto rawRead = [](const QString &path, QString &out) {
            QFile f(path);
            out = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            return !out.isEmpty();
        };
        QString mainSrc, storeSrc, storeHSrc;
        const bool reads = rawRead(mainQml, mainSrc) && rawRead(storeCpp, storeSrc)
            && rawRead(storeH, storeHSrc);
        ok = ok && reads;
        const bool qmlClean = reads && !mainSrc.contains(QLatin1String("supportTopYAt"))
            && !mainSrc.contains(QLatin1String("chunkExistsAt"));
        ok = ok && qmlClean;
        if (!qmlClean)
            diag += QStringLiteral("[qml samples ground] ");
        const bool storeClean = reads && !storeSrc.contains(QLatin1String("hadow"))
            && !storeHSrc.contains(QLatin1String("hadow"));
        ok = ok && storeClean;
        if (!storeClean)
            diag += QStringLiteral("[store touched] ");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| r2048d structure pins (sampling authority gates, geometry"
                             " contracts, QML wiring and material contracts, QML registration"
                             " macros, and the anti-probes keep Main.qml free of ground"
                             " sampling and the entity store untouched)"
                          << (ok ? QString() : diag);
    });
}
