#include "matrix_helpers.h"

#include <QDir>
#include <QFile>

// t1093 漏斗降级四件批探针段（4 腿；filter 词 r2063；矩阵 786→790）。置尾先例（接 section62，runAll
// 末执行，rig 世界零接触——各腿自建 fresh 固定小世界 48×48×96 s82，section53 同门）。
//
// 现场核实（逐件核实先行，按实况定交付/降级）：
//   ① 开盖 UI = **交付**（t1080 关单登记的 UI 候选授权面兑现）：HopperStore 5 槽 Q_INVOKABLE 读族 /
//      写族全量在位（Q_PROPERTY slotCount/revision 面 t1080 已立）→ 右键漏斗 hopperOpened 信号
//      （chestOpened/dispenserOpened 同门）→ Main.qml openHopper/closeHopper + HopperUI（1×5 容器 +
//      主栏 + hotbar，DispenserUI 同门；InventoryOps 单一权威槽算法）。QML 玩法路径零迁移（Main.qml
//      仅单向消费增量）。遮挡判定从简（无「上方压盖不开」门——dev-spec 明示从简）。
//   ② 异形碰撞体 = **交付**：t1080 整立方降级（ShapeFull/solid=true）翻案为 ShapeHopper 三盒异形
//      （顶箅板 y[8,10]/16 满格 footprint 可站 + 漏斗颈 4..12/16 × y[4,8]/16 + 排料嘴 5..11/16 ×
//      y[0,4]/16；嘴位随 state）。几何单一权威 hopperShapeBoxes（碰撞/选中/射线/列顶/渲染五处同源，
//      anvilShapeBoxes 同门）；消费面盘点 = 全走 isCollidable/isFullCube/collisionAABBs 单一权威谓词
//      （实体/爆炸失撑三方同源谓词自动跟随：torchSupportBlock=true、solidSupportBlock=false、
//      lightOpacity default 全透=漏斗开顶、沙/铁砧落体按 isFullCube=false 走「不完整方块」分支）。
//   ③ 比较器读数 = **子降级**（零代码零新号）：比较器方块本工程不存在（MC 1.5+ 依赖红石比较器承载）
//      → 无承载面非缺口，比较器本体另列候选池（纪元 + 依赖面双重理由）；登记注落在 blockregistry.h
//      Hopper 行 + 本段反探（生产源零 Comparator 标识符）。
//   ④ 爆炸破坏掉内容 = **交付**（翻案 t1080「内容不退回」登记口径）：机制等价 MC「被爆炸摧毁的容器
//      掉落全部内容」——EntityManager 爆炸破坏沿（detonateStalker / detonateTntSphere 球形破坏循环）
//      每被毁格发 explosionVoxelDestroyed（Entities 层语义事件；分层：Entities 不持容器存储向上不可
//      依赖 → 消费面在 Game 层 PlayerController::onExplosionVoxelDestroyed 排空 HopperStore 落实体 +
//      清条目）。t1085 固化面 / t1089 写门族零牵连（本链不写栅格，只读 store + 落实体）。
//
// 腿面：r2063a UI 路由钉（信号/发射/QML 开关函数/面板实例化/store 注入五面源钉）+ 5 槽 store 面
//   （UI 寻址语义行为级）；r2063b 碰撞行为柱（三盒逐位 + 可站顶 0.625 [supportTopYAt/collisionTopY/
//   solidTopOffset 三权威] + 朝向嘴位 + 谓词族 + 射线 sub-AABB + 真 EntityManager tick 沙落漏斗
//   「不完整方块分支」碎成掉落物）；r2063c 爆炸掉内容（真链 detonateTntBlock → 内容实体落地带元数据
//   + 条目清空 + 本体 destroyed）；r2063d 结构钉（def 行逐字段 + Shape 枚举值 + 单一权威盒逐位 +
//   mesher 三路由源钉 + 爆炸双发射点源钉 + 比较器反探 + fixed 零变化墙[收集/输出机制回归柱]）。
//
// 阴性面（恰红面先于腿文设计；双变异双还原，手工 Edit 做/还原，禁 git checkout/restore；存证
//   build/ 终名日志 matrix_t1093_neg{1,2}_{red,restore}.log）：
//   NEG-1 摘碰撞语义本体（blockregistry.cpp shapeBoxesInto 的 `case BlockRegistry::ShapeHopper:` 行
//     注释 → 未知 shape 空盒兜底）→ **恰红 = {r2063b}**（三盒/supportTopYAt/collisionTopY 行为面红）。
//     r2063a 不误伤（UI/存储面零碰撞读）；r2063c 不误伤（爆炸破坏按距离不按形状）；r2063d 不误伤
//     （结构钉不含 shapeBoxesInto 的 case 行——只钉 hopperShapeBoxes 权威函数体 / collisionTopY /
//     solidTopOffset 行 / mesher 路由，均非 NEG-1 靶行）。
//   NEG-2 摘爆炸消费接线（playercontroller.cpp setEntityManager 的 explosionVoxelDestroyed connect
//     对注释）→ **恰红 = {r2063c}**（内容滞留 store / 零内容实体）。r2063a/b 不误伤（无爆炸场景）；
//     r2063d 不误伤（爆炸钉面只在 entitymanager 侧双发射点 + 信号声明——connect 行不入钉）。
//
// rig 纪律：fresh 小世界 incantation（section53 同款四 setter）；rig 位运行期扫描空域（三连空列）；
//   引擎条目经 setHopperCooldown 显式登记（生产等价）；爆炸驱动 = em.detonateTntBlock 直调（生产
//   入口 = PlayerController 踩板路径同参）；下落体驱动 = em.tick 真管线（r2060e 同款）。

namespace {

// fresh 固定小世界 incantation（section53 同款四 setter + 恒晴零 RNG）。
inline void initHopperDegWorld(World &w)
{
    w.setWidth(48);
    w.setDepth(48);
    w.setHeight(96);
    w.setSeed(82);
    w.setWeatherState(0);
    w.setWeatherRemainingSec(3600.0f);
}

// rig 空域扫描：列 (x,z) 上首个「y-1 / y / y+1 / y+2 四连空」高度（漏斗放 y，上两格留给下落体）。
inline int findHopperDegRigY(World &w, int x, int z)
{
    for (int y = 4; y < w.height() - 3; ++y) {
        if (w.blockAt(x, y - 1, z) == BR::Air && w.blockAt(x, y, z) == BR::Air
            && w.blockAt(x, y + 1, z) == BR::Air && w.blockAt(x, y + 2, z) == BR::Air)
            return y;
    }
    return -1;
}

// 爆炸/下落腿 rig：fresh 小世界 + HopperStore + 掉落物 Adapter + 真消费端 PlayerController +
//   EntityManager（setEntityManager 内完成 explosionVoxelDestroyed 直连——生产装配同链）。
struct HopperDegRig
{
    World w;
    HopperStore hoppers;
    ItemEntityManager items;
    PlayerController pc;
    EntityManager em;

    HopperDegRig()
    {
        initHopperDegWorld(w);
        pc.setWorld(&w);
        pc.setHopperStore(&hoppers);
        pc.setItemEntities(&items);
        pc.setEntityManager(&em);
    }

    // 放漏斗（state 直写 + 引擎条目登记——生产等价 = placeBlock 放置登记行）。
    void placeHopper(int x, int y, int z, quint8 state)
    {
        w.setBlock(x, y, z, BR::Hopper, state);
        hoppers.setHopperCooldown(x, y, z, 0.0);
    }

    // 实体定位帮手（槽位 LIFO 复用 t256——按 itemId 找活体）。
    int findAlive(int itemId) const
    {
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i) && items.itemIdAt(i) == itemId) return i;
        return -1;
    }
};

// 推进下落体管线（r2060e 同款 tick 形态；600 拍 ≈ 9.6s 模拟时——2 格落差充足裕量）。
inline void tickHopperDegFalling(EntityManager &em, World &w)
{
    const QVector3D farListener(4.5f, 80.5f, 4.5f);
    for (int t = 0; t < 600; ++t)
        em.tick(0.016f, &w, farListener, 0.3f, 1.8f, false);
}

inline int aliveFallingHopperDeg(const EntityManager &em)
{
    int n = 0;
    for (int i = 0; i < em.count(); ++i)
        if (em.aliveAt(i) && em.kindAt(i) == int(EntityManager::FallingBlock)) ++n;
    return n;
}

} // namespace

void MatrixRun::section63_hopper_degraded()
{
    // ── r2063a：UI 路由钉 + 5 槽 store 面（t1080 关单登记的 UI 候选授权面兑现）───────────────────
    runLeg("r2063a hopper open UI route pins (signal + emission + QML open/close handlers + panel"
        " instantiation + store injection + E/Esc and mutual-exclusion close routes) and the 5-slot"
        " HopperStore UI face (slotCount authority, per-slot metadata round-trip, revision bump,"
        " clear)", [&]() {
        bool ok = true;
        QString diag;
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        const QString projRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absolutePath();

        // (1) Game 层信号面：hopperOpened 声明 + placeBlock 发射行（chestOpened/dispenserOpened 同门）。
        QFile pcf(srcRoot + QStringLiteral("/Game/playercontroller.cpp"));
        const QString pcSrc = pcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pcf.readAll()) : QString();
        QFile pchf(srcRoot + QStringLiteral("/Game/playercontroller.h"));
        const QString phSrc = pchf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pchf.readAll()) : QString();
        const bool a1 = phSrc.contains(QStringLiteral("void hopperOpened(int x, int y, int z);"))
            && pcSrc.contains(QStringLiteral("emit hopperOpened(m_hitBx, m_hitBy, m_hitBz);"));
        ok = ok && a1;
        if (!a1) diag += QStringLiteral("[a1]");

        // (2) Main.qml 路由面：开关函数 + Connections 处理器 + 面板实例化 + store 注入 + E/Esc 关。
        QFile mqf(srcRoot + QStringLiteral("/ui/Main.qml"));
        const QString mqSrc = mqf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mqf.readAll()) : QString();
        const bool a2 = mqSrc.contains(QStringLiteral("function openHopper(x, y, z) {"))
            && mqSrc.contains(QStringLiteral("function closeHopper() {"))
            && mqSrc.contains(QStringLiteral("function onHopperOpened(x, y, z) { window.openHopper(x, y, z) }"))
            && mqSrc.contains(QStringLiteral("id: hopperPanel"))
            && mqSrc.contains(QStringLiteral("visible: window.appState === \"playing\" && window.hopperOpen"))
            && mqSrc.contains(QStringLiteral("onClosed: window.closeHopper()"))
            && mqSrc.contains(QStringLiteral("else if (window.hopperOpen) window.closeHopper()"))
            && mqSrc.contains(QStringLiteral("if (e.key === Qt.Key_Escape && window.hopperOpen) {"))
            && mqSrc.count(QStringLiteral("if (hopperOpen) closeHopper()")) >= 9; // 互斥收口面：各 open*/退出/死亡路径
        ok = ok && a2;
        if (!a2) diag += QStringLiteral("[a2 open=%1 close=%2 conn=%3 panel=%4 exc=%5]")
                            .arg(mqSrc.contains(QStringLiteral("function openHopper(x, y, z) {")))
                            .arg(mqSrc.contains(QStringLiteral("function closeHopper() {")))
                            .arg(mqSrc.contains(QStringLiteral("onHopperOpened")))
                            .arg(mqSrc.contains(QStringLiteral("id: hopperPanel")))
                            .arg(mqSrc.count(QStringLiteral("if (hopperOpen) closeHopper()")));

        // (3) HopperUI.qml 在位 + CMake 注册 + store 路由（localReadSlot 走 HopperStore per-block 读族）。
        QFile huif(srcRoot + QStringLiteral("/ui/HopperUI.qml"));
        const QString huiSrc = huif.open(QIODevice::ReadOnly) ? QString::fromUtf8(huif.readAll()) : QString();
        QFile cmf(projRoot + QStringLiteral("/CMakeLists.txt"));
        const QString cmSrc = cmf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cmf.readAll()) : QString();
        const bool a3 = huiSrc.contains(QStringLiteral("property HopperStore hopperStore"))
            && huiSrc.contains(QStringLiteral("hopperStore.slotIdAt(root.hopperX, root.hopperY, root.hopperZ, index)"))
            && huiSrc.contains(QStringLiteral("hopSlotCount: hopperStore ? hopperStore.slotCount : 5"))
            && cmSrc.contains(QStringLiteral("src/ui/HopperUI.qml"));
        ok = ok && a3;
        if (!a3) diag += QStringLiteral("[a3 qml=%1 cmake=%2]")
                            .arg(huiSrc.contains(QStringLiteral("property HopperStore hopperStore")))
                            .arg(cmSrc.contains(QStringLiteral("src/ui/HopperUI.qml")));

        // (4) store 面（UI 寻址语义行为级）：5 槽全量元数据往返（id/count/耐久/附魔/名——HopperUI
        //     localReadSlot/localWriteSlot 消费的同族 Q_INVOKABLE 面）+ slotCount 权威 + revision 沿 + 清条目。
        HopperStore hs;
        bool a4 = hs.slotCount() == HopperStore::kSlotsPerHopper && hs.slotCount() == 5;
        QVariantList ench { 3, 0, 0, 0 };
        for (int i = 0; i < HopperStore::kSlotsPerHopper; ++i)
            hs.setSlot(7, 65, -9, i, int(BR::Dirt) + i, 1 + i, ench,
                       QStringLiteral("漏斗件%1").arg(i), 40 + i);
        a4 = a4 && hs.revision() >= HopperStore::kSlotsPerHopper;
        for (int i = 0; i < HopperStore::kSlotsPerHopper && a4; ++i) {
            a4 = hs.slotIdAt(7, 65, -9, i) == int(BR::Dirt) + i
                && hs.slotCountAt(7, 65, -9, i) == 1 + i
                && hs.slotDurabilityAt(7, 65, -9, i) == 40 + i
                && hs.slotNameAt(7, 65, -9, i) == QStringLiteral("漏斗件%1").arg(i)
                && hs.slotEnchantsAt(7, 65, -9, i).at(0).toInt() == 3
                && hs.hasHopper(7, 65, -9);
        }
        hs.clearHopper(7, 65, -9);
        a4 = a4 && !hs.hasHopper(7, 65, -9);
        ok = ok && a4;
        if (!a4) diag += QStringLiteral("[a4]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2063a hopper open UI route pins (signal + emission + QML open/close handlers + panel"
               " instantiation + store injection + E/Esc and mutual-exclusion close routes) and the 5-slot"
               " HopperStore UI face (slotCount authority, per-slot metadata round-trip, revision bump,"
               " clear)"
            << (ok ? QString() : diag);
    });

    // ── r2063b：碰撞行为柱（三盒异形 + 可站顶 0.625 + 朝向嘴位 + 谓词族 + 沙「不完整方块分支」）──────
    runLeg("r2063b hopper partial collision behavior column (three sub-boxes bit-exact via the"
        " hopperShapeBoxes single authority, standable top at 10/16 through supportTopYAt and"
        " collisionTopY and solidTopOffset, facing-driven mouth placement, the predicate family"
        " [isFullCube false / isCollidable true / torchSupport true / solidSupport false / full"
        " light pass], sub-AABB raycast boxes, and on a real EntityManager tick a falling sand"
        " entity above the hopper takes the not-a-full-cube branch and breaks into a drop instead"
        " of landing)", [&]() {
        bool ok = true;
        QString diag;
        HopperDegRig rig;
        const int x = 8, z = 8;
        const int y = findHopperDegRigY(rig.w, x, z);
        if (y <= 0) { ++totalFail; qInfo().noquote() << "FAIL | r2063b (no rig space y=" << y << ")"; return; }
        rig.placeHopper(x, y, z, BlockRegistry::HopperFacingDownFlag); // 朝下

        // (1) 三盒几何（碰撞查询面 = World::collisionAABBsAt → shapeBoxesInto → hopperShapeBoxes 权威）：
        //     顶箅板满格 footprint y[0.5,0.625]（顶面 = 可站面 10/16）+ 漏斗颈 + 底心排料嘴；无满格盒。
        BlockRegistry::BlockAABB boxes[BlockRegistry::kMaxAABBsPerCell];
        const int n = rig.w.collisionAABBsAt(x, y, z, boxes, BlockRegistry::kMaxAABBsPerCell);
        // collisionAABBsAt 返回**世界坐标**盒（cell-local + 格原点）→ 先归一到 cell-local 再逐位比对。
        for (int i = 0; i < n; ++i) {
            boxes[i].minX -= float(x); boxes[i].maxX -= float(x);
            boxes[i].minY -= float(y); boxes[i].maxY -= float(y);
            boxes[i].minZ -= float(z); boxes[i].maxZ -= float(z);
        }
        bool b1 = n == 3;
        if (b1) {
            const BlockRegistry::BlockAABB &plate = boxes[0];
            const BlockRegistry::BlockAABB &neck  = boxes[1];
            const BlockRegistry::BlockAABB &mouth = boxes[2];
            b1 = plate.minY == 0.5f && plate.maxY == 0.625f
                && plate.minX == 0.0f && plate.maxX == 1.0f && plate.minZ == 0.0f && plate.maxZ == 1.0f
                && neck.minX == 0.25f && neck.maxX == 0.75f && neck.minY == 0.25f && neck.maxY == 0.5f
                && mouth.minY == 0.0f && mouth.maxY == 0.25f
                && mouth.minX == 0.3125f && mouth.maxX == 0.6875f // 朝下 → 嘴贴底心
                && !(plate.minY == 0.0f && plate.maxY == 1.0f);   // 无满格盒（非全格碰撞）
        }
        ok = ok && b1;
        if (!b1) diag += QStringLiteral("[b1 n=%1]").arg(n);

        // (2) 可站顶三权威逐位：supportTopYAt（实体承接）/ collisionTopY / solidTopOffset（PCF 列顶）= 10/16。
        const bool b2 = rig.w.supportTopYAt(x, y, z) == float(y) + 0.625f
            && BlockRegistry::collisionTopY(BR::Hopper, quint8(0)) == 0.625f
            && BlockRegistry::solidTopOffset(BR::Hopper, quint8(0)) == 0.625f;
        ok = ok && b2;
        if (!b2) diag += QStringLiteral("[b2 sup=%1 top=%2 ofs=%3]")
                            .arg(rig.w.supportTopYAt(x, y, z) - float(y))
                            .arg(BlockRegistry::collisionTopY(BR::Hopper, quint8(0)))
                            .arg(BlockRegistry::solidTopOffset(BR::Hopper, quint8(0)));

        // (3) 朝向嘴位：水平朝 +X（state=0）→ 嘴盒贴 +X 侧边（maxX=15/16）；非满格足迹侧向让空。
        rig.w.setBlock(x, y, z, BR::Hopper, quint8(0));
        const int n3 = rig.w.collisionAABBsAt(x, y, z, boxes, BlockRegistry::kMaxAABBsPerCell);
        // 世界坐标 → cell-local 归一（同 b1）。
        for (int i = 0; i < n3; ++i) {
            boxes[i].minX -= float(x); boxes[i].maxX -= float(x);
            boxes[i].minY -= float(y); boxes[i].maxY -= float(y);
            boxes[i].minZ -= float(z); boxes[i].maxZ -= float(z);
        }
        bool b3 = n3 == 3 && boxes[2].maxX == 0.9375f && boxes[2].minX == 0.5625f;
        ok = ok && b3;
        if (!b3) diag += QStringLiteral("[b3 n=%1 mx=%2]").arg(n3).arg(n3 == 3 ? boxes[2].maxX : -1.0f);

        // (4) 谓词族（实体/爆炸失撑三方同源谓词消费面）：isFullCube=false（沙/铁砧落体分支语义）/
        //     isCollidable=true（可站可挡）/ torchSupport=true（火把可附）/ solidSupport=false（粉/梯不附）/
        //     lightOpacity 全透（漏斗开顶）/ selection·raycast sub-AABB 三盒（射线瞄开口区穿透）。
        rig.w.setBlock(x, y, z, BR::Hopper, BlockRegistry::HopperFacingDownFlag);
        const bool b4 = !BlockRegistry::isFullCube(BR::Hopper)
            && BlockRegistry::isCollidable(BR::Hopper, quint8(0))
            && BlockRegistry::torchSupportBlock(BR::Hopper, quint8(0))
            && !BlockRegistry::solidSupportBlock(BR::Hopper)
            && BlockRegistry::lightOpacity(BR::Hopper, quint8(0)) == 0
            && !BlockRegistry::isSolid(BR::Hopper)
            && BlockRegistry::raycastAABBs(BR::Hopper, quint8(0)).size() == 3
            && BlockRegistry::selectionAABBs(BR::Hopper, quint8(0)).size() == 3;
        ok = ok && b4;
        if (!b4) diag += QStringLiteral("[b4]");

        // (5) 行为柱：漏斗上方掉沙（真 EntityManager tick 管线）→ isFullCube=false 判「不完整方块」→
        //     走 fallingBlockDropped 分支（沙碎成掉落物，不还原方块；随后被漏斗收集域收走——机制等价
        //     MC）。旧 ShapeFull 形态会在此着地还原沙方块（行为反差 = 本腿红载荷）。
        rig.em.spawnFallingBlock(x, y + 2, z, int(BR::Sand));
        tickHopperDegFalling(rig.em, rig.w);
        const bool b5 = aliveFallingHopperDeg(rig.em) == 0
            && rig.w.blockAt(x, y + 1, z) == BR::Air   // 未还原沙方块（不完整方块分支）
            && rig.w.blockAt(x, y, z) == BR::Hopper;   // 漏斗本体完好
        ok = ok && b5;
        if (!b5) diag += QStringLiteral("[b5 alive=%1 above=%2 hop=%3]")
                            .arg(aliveFallingHopperDeg(rig.em))
                            .arg(rig.w.blockAt(x, y + 1, z))
                            .arg(rig.w.blockAt(x, y, z));

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2063b hopper partial collision behavior column (three sub-boxes bit-exact via the"
               " hopperShapeBoxes single authority, standable top at 10/16 through supportTopYAt and"
               " collisionTopY and solidTopOffset, facing-driven mouth placement, the predicate family"
               " [isFullCube false / isCollidable true / torchSupport true / solidSupport false / full"
               " light pass], sub-AABB raycast boxes, and on a real EntityManager tick a falling sand"
               " entity above the hopper takes the not-a-full-cube branch and breaks into a drop instead"
               " of landing)"
            << (ok ? QString() : diag);
    });

    // ── r2063c：爆炸掉内容（真链 detonateTntBlock → 内容实体落地 + 条目清空 + 本体毁）──────────────
    runLeg("r2063c hopper explosion spills contents (a real-chain TNT detonation at the production"
        " entry destroys the hopper, the explosion voxel-destroyed event reaches the Game layer"
        " which empties the 5-slot store into item entities with full metadata and clears the"
        " entry, while the store entry no longer resurrects on the same cell)", [&]() {
        bool ok = true;
        QString diag;
        HopperDegRig rig;
        const int x = 8, z = 8;
        const int y = findHopperDegRigY(rig.w, x, z);
        if (y <= 0) { ++totalFail; qInfo().noquote() << "FAIL | r2063c (no rig space y=" << y << ")"; return; }
        rig.placeHopper(x, y, z, BlockRegistry::HopperFacingDownFlag);
        // 内容两栈：普通堆叠（泥土 x7）+ 全元数据单件（木锄，名 + 耐久 42——cap=1 各占一槽）。
        rig.hoppers.setSlot(x, y, z, 0, int(BR::Dirt), 7);
        rig.hoppers.setSlot(x, y, z, 2, int(ToolRegistry::HoeWood), 1, QVariantList(),
                            QStringLiteral("试制锄"), 42);

        // 真链引爆：生产入口 detonateTntBlock（PlayerController 踩板路径同参），TNT 距漏斗 2 格
        //   （kExplosionRadius=3 球内）。玩家位远离（无伤害消费面）。
        rig.w.setBlock(x + 2, y, z, BR::TntBlock, quint8(0));
        rig.em.detonateTntBlock(x + 2, y, z, &rig.w, QVector3D(-40.5f, 80.5f, -40.5f));

        // (1) 本体毁 + 条目清：漏斗格 Air；HopperStore 条目清除（同格重放不复活旧容腔）。
        const bool c1 = rig.w.blockAt(x, y, z) == BR::Air && !rig.hoppers.hasHopper(x, y, z);
        ok = ok && c1;
        if (!c1) diag += QStringLiteral("[c1 b=%1 has=%2]")
                            .arg(rig.w.blockAt(x, y, z)).arg(rig.hoppers.hasHopper(x, y, z));

        // (2) 内容实体落地：两栈各一实体（元数据全量随栈——名 / 耐久 / 数量逐位），落在漏斗格。
        const int dirt = rig.findAlive(int(BR::Dirt));
        const int hoe = rig.findAlive(int(ToolRegistry::HoeWood));
        const bool c2 = dirt >= 0 && rig.items.countAt(dirt) == 7
            && hoe >= 0 && rig.items.nameAt(hoe) == QStringLiteral("试制锄")
            && rig.items.durabilityAt(hoe) == 42
            && rig.items.posAt(dirt).x() >= float(x) && rig.items.posAt(dirt).x() < float(x + 1)
            && rig.items.posAt(dirt).z() >= float(z) && rig.items.posAt(dirt).z() < float(z + 1);
        ok = ok && c2;
        if (!c2) diag += QStringLiteral("[c2 dirt=%1 hoe=%2]")
                            .arg(dirt).arg(hoe);

        // (3) 非漏斗格广播无害：爆炸毁掉的石质邻格（无容器语义格）不产生杂散条目/实体（id 门 no-op）。
        bool c3 = true;
        for (int i = 0; i < rig.items.count(); ++i) {
            if (rig.items.aliveAt(i) && rig.items.itemIdAt(i) != int(BR::Dirt)
                && rig.items.itemIdAt(i) != int(ToolRegistry::HoeWood))
                c3 = false; // 爆炸掉落物只有本体链产物（dropId 概率面不 assert）；内容物 id 恰两类
        }
        ok = ok && c3;
        if (!c3) diag += QStringLiteral("[c3]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2063c hopper explosion spills contents (a real-chain TNT detonation at the production"
               " entry destroys the hopper, the explosion voxel-destroyed event reaches the Game layer"
               " which empties the 5-slot store into item entities with full metadata and clears the"
               " entry, while the store entry no longer resurrects on the same cell)"
            << (ok ? QString() : diag);
    });

    // ── r2063d：结构钉（def 行 / 枚举 / 单一权威盒 / mesher 路由 / 爆炸双发射点 / 比较器反探 / fixed 墙）──
    //   NEG 靶行按恰红设计收账：本腿不含 shapeBoxesInto 的 ShapeHopper case 行（NEG-1 靶）与
    //   playercontroller connect 行（NEG-2 靶）；只钉 hopperShapeBoxes 权威体 / collisionTopY /
    //   solidTopOffset 行 / mesher 三路由 / entitymanager 双发射点 + 信号声明——NEG 双轮均不触碰。
    runLeg("r2063d hopper batch structure pins (def row keeps id 145 with ShapeHopper and solid"
        " false and the three atlas tiles, the Shape enum value is 11, the hopperShapeBoxes"
        " authority emits the bit-exact three-box table for down and horizontal facing,"
        " collisionTopY and solidTopOffset both read 10/16, the mesher routes the hopper through"
        " PASS 1 partial geometry at all three sites with the append case consuming the authority,"
        " the explosion signal and both emit sites stay in place, no comparator identifiers exist"
        " in the production sources, and the fixed-world engine wall [collect + output-to-chest]"
        " stays green)", [&]() {
        bool ok = true;
        QString diag;

        // (1) def 行逐字段（数值 id 零改动 + 形状 / solid 翻案面 + 三贴图 + 掉落面）。
        const auto &d = BlockRegistry::def(BR::Hopper);
        const bool d1 = int(BR::Hopper) == 145
            && d.shape == BlockRegistry::ShapeHopper
            && d.solid == false
            && d.topTile == 186 && d.sideTile == 187 && d.frontTile == 188
            && d.dropId == int(BR::Hopper) && d.dropCount == 1
            && BR::mcBlockId(quint8(BR::Hopper)) == 0;
        ok = ok && d1;
        if (!d1) diag += QStringLiteral("[d1]");

        // (2) Shape 枚举值 + 单一权威盒表（朝下 / 朝 +X 两态逐位——与 r2063b 行为面同源异法）。
        BlockRegistry::BlockAABB hb[3];
        const bool d2 = BlockRegistry::ShapeHopper == 11
            && BlockRegistry::hopperShapeBoxes(BlockRegistry::HopperFacingDownFlag, hb, 3) == 3
            && hb[0].maxY == 0.625f && hb[0].minY == 0.5f && hb[0].minX == 0.0f && hb[0].maxX == 1.0f
            && hb[1].minX == 0.25f && hb[1].maxX == 0.75f
            && hb[2].minX == 0.3125f && hb[2].maxX == 0.6875f
            && BlockRegistry::hopperShapeBoxes(quint8(0), hb, 3) == 3
            && hb[2].maxX == 0.9375f
            && BlockRegistry::hopperShapeBoxes(quint8(0), hb, 2) == 0; // cap 防御
        ok = ok && d2;
        if (!d2) diag += QStringLiteral("[d2]");

        // (3) mesher 三路由源钉（PASS 1 收纳行 + greedy/culled 双跳过行）+ 渲染 case 消费权威 + 权威
        //     函数体 + collisionTopY/solidTopOffset 行（NEG-1 靶行 [shapeBoxesInto case] 不入钉面）。
        const QString srcRoot = QDir(QCoreApplication::applicationDirPath()
            + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src"));
        QFile mbf(srcRoot + QStringLiteral("/World/meshbuilder.cpp"));
        const QString mbSrc = mbf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mbf.readAll()) : QString();
        QFile pbgf(srcRoot + QStringLiteral("/World/partialblockgeometry.cpp"));
        const QString pbSrc = pbgf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pbgf.readAll()) : QString();
        QFile brcf(srcRoot + QStringLiteral("/Core/blockregistry.cpp"));
        const QString brSrc = brcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(brcf.readAll()) : QString();
        const bool d3 = mbSrc.count(QStringLiteral("// t1093 漏斗三盒异形已在 PASS 1")) == 2
            && mbSrc.contains(QStringLiteral("|| b == BlockRegistry::Hopper    // t1093 漏斗三盒异形（顶箅板+颈+嘴，hopperShapeBoxes 单一权威）经 PartialBlockGeometry 渲染"))
            && pbSrc.contains(QStringLiteral("BlockRegistry::hopperShapeBoxes(state, hb, 3)"))
            && brSrc.count(QStringLiteral("int BlockRegistry::hopperShapeBoxes(quint8 state, BlockAABB *out, int cap)")) == 1
            && brSrc.contains(QStringLiteral("case ShapeHopper:  return 0.625f; // t1093 漏斗顶箅板顶面 10/16"))
            && brSrc.contains(QStringLiteral("case ShapeHopper:   return 0.625f;                        // t1093 漏斗顶箅板顶面 10/16（PCF 列顶随可站面）"));
        ok = ok && d3;
        if (!d3) diag += QStringLiteral("[d3 skip2=%1 partial=%2 auth=%3]")
                            .arg(mbSrc.count(QStringLiteral("// t1093 漏斗三盒异形已在 PASS 1")))
                            .arg(pbSrc.contains(QStringLiteral("BlockRegistry::hopperShapeBoxes(state, hb, 3)")))
                            .arg(brSrc.count(QStringLiteral("int BlockRegistry::hopperShapeBoxes(quint8 state, BlockAABB *out, int cap)")));

        // (4) 爆炸链源钉（Entities 侧双发射点 + 信号声明；NEG-2 靶行 [connect] 不入钉面）。
        QFile emf(srcRoot + QStringLiteral("/Entities/entitymanager.cpp"));
        const QString emSrc = emf.open(QIODevice::ReadOnly) ? QString::fromUtf8(emf.readAll()) : QString();
        QFile emhf(srcRoot + QStringLiteral("/Entities/entitymanager.h"));
        const QString emhSrc = emhf.open(QIODevice::ReadOnly) ? QString::fromUtf8(emhf.readAll()) : QString();
        const bool d4 = emSrc.count(QStringLiteral("emit explosionVoxelDestroyed(d.x, d.y, d.z, int(d.oldId));")) == 2
            && emhSrc.count(QStringLiteral("void explosionVoxelDestroyed(int x, int y, int z, int blockId);")) == 1;
        ok = ok && d4;
        if (!d4) diag += QStringLiteral("[d4 emits=%1 sig=%2]")
                            .arg(emSrc.count(QStringLiteral("emit explosionVoxelDestroyed(d.x, d.y, d.z, int(d.oldId));")))
                            .arg(emhSrc.count(QStringLiteral("void explosionVoxelDestroyed(int x, int y, int z, int blockId);")));

        // (5) 比较器子降级反探：生产四源文件零 Comparator 标识符（零代码零新号裁定留痕）。
        QFile pcf(srcRoot + QStringLiteral("/Game/playercontroller.cpp"));
        const QString pcSrc = pcf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pcf.readAll()) : QString();
        const bool d5 = !brSrc.contains(QStringLiteral("Comparator"))
            && !pcSrc.contains(QStringLiteral("Comparator"))
            && !emSrc.contains(QStringLiteral("Comparator"))
            && !mbSrc.contains(QStringLiteral("Comparator"));
        ok = ok && d5;
        if (!d5) diag += QStringLiteral("[d5]");

        // (6) fixed 零变化墙：fresh fixed 小世界引擎机制回归柱——收集（上方格掉落物入腔）+ 输出
        //     （朝下漏斗向下箱推 1 件）两语义在异形翻案后逐位不变（t2052 机制面零回归）。
        HopperDegRig rig;
        const int x = 8, z = 8;
        const int y = findHopperDegRigY(rig.w, x, z);
        bool d6 = y > 0;
        if (d6) {
            rig.placeHopper(x, y, z, BlockRegistry::HopperFacingDownFlag);
            rig.w.setBlock(x, y - 1, z, BR::Chest, quint8(0));
            ChestStore chests;
            rig.pc.setChestStore(&chests);
            rig.items.spawnItem(x, y + 1, z, int(BR::Dirt), 4);
            rig.hoppers.setSlot(x, y, z, 3, int(BR::Cobble), 1);
            rig.pc.scanHoppers(0.4f); // 一轮：收集 dirt x4 → 空槽；输出 cobble x1 → 下箱
            d6 = rig.hoppers.slotIdAt(x, y, z, 0) == int(BR::Dirt)
                && rig.hoppers.slotCountAt(x, y, z, 0) == 4
                && chests.slotIdAt(x, y - 1, z, 0) == int(BR::Cobble)
                && rig.hoppers.slotIdAt(x, y, z, 3) == 0;
        }
        ok = ok && d6;
        if (!d6) diag += QStringLiteral("[d6]");

        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
            << "| r2063d hopper batch structure pins (def row keeps id 145 with ShapeHopper and solid"
               " false and the three atlas tiles, the Shape enum value is 11, the hopperShapeBoxes"
               " authority emits the bit-exact three-box table for down and horizontal facing,"
               " collisionTopY and solidTopOffset both read 10/16, the mesher routes the hopper through"
               " PASS 1 partial geometry at all three sites with the append case consuming the authority,"
               " the explosion signal and both emit sites stay in place, no comparator identifiers exist"
               " in the production sources, and the fixed-world engine wall [collect + output-to-chest]"
               " stays green)"
            << (ok ? QString() : diag);
    });
}
