// tools/matrix/section04_instancing.cpp —— R20.03 测试分层段 TU
// 原 tools/redstone_matrix_test.cpp L18561-24789 逐字节搬移（段 md5: 3944189098ee6c5cf60b0367b719ea5c；
// 8 段拼接 == 原 main 体，md5 82ac70c7ede1a2ed647fea738734be6b，存证 build/r2003_proof/）。
#include "matrix_helpers.h"

void MatrixRun::section04_instancing()
{

    // ── P-t892 静止水位降低探针（液面单一权威 + 消费方四方同源行为级）──
    //    用户报告：「耕地比水还低」透视错乱——耕地矮盒顶 15/16 而水满格 1.0 漫过其顶。修复 = 静水表面降
    //    到 7/8（MC 1.0 语义：比方块顶低 2 像素），**单一权威 BlockRegistry::waterSurfaceFrac**（源 7/8 /
    //    流 (8−min(s,7))/8），消费方四方同读：mesher renderTop（视觉水面）/ 浮标浮定 / 掉落物浮面 / 船水线
    //    ——统一改源头，严禁消费点各自内联 1.0（否则物浮在可视水面上/下方 1/8，视觉物理分裂）。
    //    断言：(a) 单一权威数值钉（源 7/8、流分档、越界 clamp）；(b) 掉落物静水浮面 = 顶水格 + 7/8 − 0.05
    //    下沉（镜像常量 kItemFloatOffset=0.05，P18 模式）；(c) 船静水水线 = 顶水格 + 7/8 − 吃水 0（tick 浮水
    //    lerp 收敛）；(d) 眼位液面分数判：眼在 7/8..1.0 空段 → 不算水下（蓝雾与可视液面同步），液面下 → 水下，
    //    柱内格（上方仍是水 → 满块）→ 水下。
    //    阴性轮：回退 waterSurfaceFrac(0) → 1.0 则 (a) 源值钉红 + (b)(c) 高度断言红（+1/8 偏差超容差）+
    //    (d) 空段判红（恢复满格恒水下）——四方同红即「单一权威生效」的证明。
    {
        World wS;
        // 48×48×96 seed 77 = t836 已证净空带（地形 ≤81 → 82+ 全空；小世界也会自动 worldgen，rig 层须避开
        //   自然地形——首跑 seed 892 物品落在 y≈45 天然地表上红）。
        wS.setWidth(48); wS.setDepth(48); wS.setHeight(96); wS.setSeed(77);
        const int ty = 83;                       // 石底格；静水 ty+1（state=0）
        for (int x = 4; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z) {
                wS.setBlock(x, ty, z, BR::Stone, 0);
                wS.setBlock(x, ty + 1, z, BR::Water, 0);
            }
        wS.setBlock(4, ty + 2, 4, BR::Water, 0);  // (d) 柱内格：该列上方仍是水 → 满块口径
        // (a) 单一权威数值钉：源 7/8（低 2 像素）；st1 与源同高 7/8；分档 (8−s)/8；越界 clamp 到最低档。
        const bool okA = qAbs(BR::waterSurfaceFrac(0) - 0.875f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(1) - 0.875f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(2) - 0.75f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(4) - 0.5f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(7) - 0.125f) < 1e-6f
                         && qAbs(BR::waterSurfaceFrac(200) - 0.125f) < 1e-6f;
        // (b) 掉落物浮面：出生水上 → 落水浮定 restY = 顶水格 + 7/8 − 0.05（旧满格口径 +1.0 → 红）。
        ItemEntityManager items;
        items.spawnItem(6, ty + 4, 6, BR::Cobble, 1);
        for (int t = 0; t < 400; ++t) items.tick(0.05, &wS);   // 20s：落 + 浮 + 静置（寿命 300s 内）
        int it = -1;
        for (int i = 0; i < items.count(); ++i)
            if (items.aliveAt(i)) { it = i; break; }
        const bool okB = it >= 0
                         && qAbs(items.posAt(it).y() - (float(ty + 1) + 0.875f - 0.05f)) < 2e-3f;
        // (c) 船水线：spawn 于水格 → tick 浮水 lerp 收敛到 顶水格 + 7/8 − 吃水 0（旧口径 +1.0 → 红）。
        BoatManager boats;
        const bool boatSpawned = boats.spawnBoat(7, ty + 1, 7, BoatManager::Oak);
        for (int t = 0; t < 600; ++t) boats.tick(0.016, &wS);   // 9.6s（kBoatAccel 恒速钳到 |dy| → 精确收敛）
        int bt = -1;
        for (int i = 0; i < boats.count(); ++i)
            if (boats.aliveAt(i)) { bt = i; break; }
        const bool okC = boatSpawned && bt >= 0
                         && qAbs(boats.posAt(bt).y() - (float(ty + 1) + 0.875f)) < 1e-3f;
        // (d) 眼位液面分数判（PlayerController 直读 eyeInWater）：眼 y=ty+1.92（7/8..1.0 空段）→ false；
        //     眼 y=ty+1.5（液面下）→ true；柱内列 (4,4) 同眼高 → true（上方是水 → 满块）。
        PlayerController pc;
        pc.setWorld(&wS);
        pc.loadSavedState(6.5f, float(ty + 1) + 0.92f - 1.62f, 6.5f, 0.0f, 0.0f, 1);
        const bool bandDry = !pc.eyeInWater();
        pc.loadSavedState(6.5f, float(ty + 1) + 0.5f - 1.62f, 6.5f, 0.0f, 0.0f, 1);
        const bool belowWet = pc.eyeInWater();
        pc.loadSavedState(4.5f, float(ty + 1) + 0.92f - 1.62f, 4.5f, 0.0f, 0.0f, 1);
        const bool columnWet = pc.eyeInWater();
        const bool okD = bandDry && belowWet && columnWet;
        const float itemY = (it >= 0) ? items.posAt(it).y() : -99.0f;   // diag 前取值（清场后槽失效）
        const float boatY = (bt >= 0) ? boats.posAt(bt).y() : -99.0f;
        // 清场（即用即清，防串扰后续探针）。
        for (int x = 4; x <= 8; ++x)
            for (int z = 4; z <= 8; ++z)
                for (int dy = 0; dy <= 2; ++dy) wS.setBlock(x, ty + dy, z, BR::Air, 0);
        wS.setBlock(4, ty + 2, 4, BR::Air, 0);
        items.clearAll();
        boats.clearAll();
        const bool ok = okA && okB && okC && okD;
        if (!ok)
            qInfo().noquote() << "  [t892 diag] okA" << okA << "| okB" << okB << "itemY" << itemY
                              << "| okC" << okC << "boatY" << boatY
                              << "| okD" << okD << "bandDry" << bandDry
                              << "belowWet" << belowWet << "columnWet" << columnWet;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t892 still-water surface lowered to 7/8 (2px below block top, MC semantics): "
                             "single-authority waterSurfaceFrac (source 7/8 / flow (8-s)/8 / clamp) drives all "
                             "four consumers in lockstep - mesher renderTop (visual surface; farmland 15/16 now "
                             "stands above water, fixing the reported perspective clash), item float restY, boat "
                             "waterline and the eye-in-water liquid-fraction check (eye in the 1/8 air band above "
                             "a still surface is no longer underwater; column-interior cells stay full-block wet)";
    }

    // ── P-t893 流水动画流向四向匹配（源码钉；驱动 ChunkGeometry 需渲染后端，t879/t889 源码钉先例）──
    //    流水条带（右列）图案随帧沿 −v 移动（build_fluid_strips roll_y + t563 保向）→ mesher 按本格离源
    //    流向 D 旋转 UV 把「−v」映射到 D（观感顺流）。钉三面：①流向判定与掉落物随流同源算法（4 向
    //    state 梯度、低 state=近源背向）且门= !m_lavaOnly && st>0（静水左列 / 岩浆 / 孤立流格无向恒等，
    //    spec「静止面无向」）；②四向旋转路由俱在（±Y 顶面 cv≡−D 四分支 + ±X 墙 ±Z 流 / ±Z 墙 ±X 流
    //    沿墙横置、正交流保持竖直下淌 t563 语义）；③u/v 窗不越狱（colL/hxs 列窗 + stripV0 帧子区保留
    //    → positionV 翻书不受扰，零 mesh 重建语义不变）。回退（删 flowDir 旋转）→ 钉②红。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile cf(root + QStringLiteral("/src/World/chunkgeometry.cpp"));
        const QString t = cf.open(QIODevice::ReadOnly) ? QString::fromUtf8(cf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("int flowDir = 0;"));
        const int i1 = t.indexOf(QStringLiteral("if (flowDir != 0)"));
        bool okGate = false, okRot = false;
        if (i0 >= 0) {
            const QString seg = t.mid(i0, 900);
            okGate = seg.contains(QStringLiteral("!m_lavaOnly && st > 0"))
                     && seg.contains(QStringLiteral("fns < st"))
                     && seg.contains(QStringLiteral("fgx") ) && seg.contains(QStringLiteral("fgz"));
        }
        if (i1 >= 0) {
            const QString seg = t.mid(i1, 1200);
            okRot = seg.contains(QStringLiteral("cv = 1.0f - dz"))      // D=+Z（顶面 / ±X 墙）
                    && seg.contains(QStringLiteral("cv = 1.0f - dx"))   // D=+X（顶面 / ±Z 墙）
                    && seg.contains(QStringLiteral("cv = dx;        cu = dz"))   // D=−X 顶面
                    && seg.contains(QStringLiteral("cv = dz; cu = dy"))          // ±X 墙 D=−Z
                    && seg.contains(QStringLiteral("cv = dx; cu = dy"));         // ±Z 墙 D=−X
        }
        const bool ok = okGate && okRot;
        if (!ok)
            qInfo().noquote() << "  [t893 diag] gate" << i0 << okGate << "| rot" << i1 << okRot;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t893 flow animation direction matches the four flow directions: mesher derives "
                             "the away-from-source cardinal from the 4-neighbor water-state gradient (same "
                             "algorithm as item drift/player push) for flow cells only (still column, lava and "
                             "unresolvable isolated cells stay directionless) and rotates the strip UVs so the "
                             "-v pattern motion maps onto that direction on the top face, with side walls "
                             "animating horizontally only when the flow runs along the wall (waterfalls keep "
                             "flowing down); u stays locked to the column window and v to the frame-0 sub-range "
                             "so the positionV flipbook is untouched";
    }

    // ── P-t894 潜行者模型 0.85（源码钉：QML 契约——纯视觉项 t781 先例不进行为矩阵）──
    //    用户「苦力怕（潜行者）现偏大」→ 全模视觉缩 0.85。钉三面成对契约：①Main.qml scale 基 0.85
    //    （蓄力膨胀相对量 1+inflate·0.5 保持 → 满蓄力 ≈1.28）；②mobModelYOff Stalker 分支腿底补偿
    //    0.90×0.85（缺补偿 → 脚下悬空 0.135 —— 与 ① 成对，改其一须同步另一）；③碰撞盒**不缩**
    //    （radiusAt/halfHeightAt 走 mobType 表单一权威 —— halfW 0.30/halfH 0.90 保持，移动/近战/爆炸
    //    判定不随视觉变）；④图鉴预览 mobPreviewScale(6) 同源 0.85（所见即游戏内比例）。
    //    回退 scale 到 1.0（漏 Y 补偿）→ ①红（②仍绿但契约断裂面由 ① 单钉暴露，Y 补偿独立值 0.765
    //    与 0.85 基乘积钉死）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        bool okScale = m.count(QStringLiteral("0.85 * (1.0 + inflate * 0.5)")) >= 3; // 三轴同基
        const int iy = m.indexOf(QStringLiteral("0.90 * 0.85 - mobHalfH"));
        bool okY = iy >= 0 && m.mid(iy - 600, 600).contains(QStringLiteral("MobStalker"));
        QFile bf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
        const QString b = bf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bf.readAll()) : QString();
        bool okBrowser = b.contains(QStringLiteral("if (t === 6) return 0.85"));
        const bool ok = okScale && okY && okBrowser;
        if (!ok)
            qInfo().noquote() << "  [t894 diag] scale" << okScale << "yOff" << okY << "browser" << okBrowser;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t894 stalker model scaled to 0.85 (visual-only, hitbox untouched): base scale "
                             "0.85 on all three axes with the relative inflate swell kept (full charge ~1.28), "
                             "leg-bottom Y compensation 0.90*0.85 keeps the feet on the collision floor (pair "
                             "contract - changing the scale without the offset lifts the model 0.135 off the "
                             "ground), collision halfW/halfH stay at the mobType-table single authority, and the "
                             "resource browser preview mirrors 0.85";
    }

    // ── P-t895 F3 修复（源码钉：①重叠布局分列 + ②MC 1.0 行结构逐面对齐）──
    //    ① 用户「黄绿文字重叠」根因：主 F3 块（黄 #ffff00）与 FrameProfiler 报告（绿 #00ff88）各自绝对
    //      定位，绿块钉死 y=62+200（注释还写「主块约 12 行」）—— 主块逐轮增行到 ~20 行后越过 200px
    //      与绿块叠印。修 = Column 布局分列（行数增减自动排布永不重叠）。钉：两 Text 同入一个 Column
    //      （f3Text Text 与 FrameProfiler Text 之间存在 spacing 锚）+ 旧 `y: 62 + 200` 绝对定位已消失。
    //    ② 主块严格对齐 MC 1.0 F3：标题行带版本（MC "Minecraft 1.0.0" → "voxelsandbox (BuildInfo)"，
    //      t813 版本戳并入标题不再单独占行）、fps 行、x/y/z 三行（MC "x: 123.456 // 123 // 11" 坐标//
    //      所在格//格内 16 取余）、f 朝向行（基数码 MC 表 +Z→0/−X→1/−Z→2/+X→3 + 轴 + (yaw / pitch)）、
    //      biome 行、bl/ol 光照行（脚下格 blockLightAt/skyLightAt 真值）。钉六行前缀俱在 + 旧格式行
    //      （"build: " 单行 / "pos: " 合并行 / "yaw: " 独行）已删。工程诊断尾段保留（§2-F 验收铁律，
    //      MC 行在前工程扩展在后，空行分隔）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        const int iF3 = m.indexOf(QStringLiteral("text: window.f3Text"));
        const int iProf = m.indexOf(QStringLiteral("text: FrameProfiler.report"));
        const int iCol = m.lastIndexOf(QStringLiteral("Column {"), iF3);
        bool okColumn = iF3 >= 0 && iProf > iF3 && iCol >= 0
                        && m.mid(iCol, iF3 - iCol).contains(QStringLiteral("spacing: 10"))   // 同 Column 属性
                        && m.lastIndexOf(QStringLiteral("Column {"), iProf) == iCol;          // 两 Text 同 Column
        bool okOldGone = !m.contains(QStringLiteral("y: 62 + 200"));
        const int iFn = m.indexOf(QStringLiteral("function buildF3Text()"));
        // t857 起函数体加长（renderStats 真值段 + 注释）→ 切片窗 4200→5200；t934 再加 render-side 真值行
        //   （+~3.5k：gpu/prep/vmem 常量 + 行拼接 + 判读注释）→ 8736 → 窗 9600（钉的是内容 token，窗口须
        //   覆盖增长后的函数；窗口不足会把仍在函数内的钉 token 误判为消失 = 假红）。
        const QString fn = iFn >= 0 ? m.mid(iFn, 9600) : QString();
        bool okMc = fn.contains(QStringLiteral("\"voxelsandbox (\" + BuildInfo.full"))
                    && fn.contains(QStringLiteral("\\nx: \""))
                    && fn.contains(QStringLiteral(" // \""))
                    && fn.contains(QStringLiteral("\\nf: \" + fIdx"))
                    && fn.contains(QStringLiteral("\\nbiome: \""))
                    && fn.contains(QStringLiteral("\\nbl: \" + footBl + \" ol: \""))
                    && !fn.contains(QStringLiteral("\"\\nbuild: \""))
                    && !fn.contains(QStringLiteral("\"\\npos: \""));
        const bool ok = okColumn && okOldGone && okMc;
        if (!ok)
            qInfo().noquote() << "  [t895 diag] column" << okColumn << "oldGone" << okOldGone
                              << "mc" << okMc;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t895 F3 overlay fixed: (1) yellow main block and green FrameProfiler report "
                             "now live in one Column (auto-stacked, overlap from the hardcoded y=62+200 with "
                             "the grown ~20-line main block is structurally gone); (2) main block realigned "
                             "line-by-line to MC 1.0 F3 - version-carrying title, fps line, separate x/y/z "
                             "lines with MC coordinate//block//in-chunk-16 format, f facing line (MC cardinal "
                             "table +Z->0/-X->1/-Z->2/+X->3 with yaw/pitch), biome line and bl/ol feet-light "
                             "line from real World queries; project diagnostics kept as a blank-line-separated "
                             "tail (PLAN 2-F acceptance requires the mesh/perf stats)";
    }

    // ── P-t896 创造拿取/复制语义（源码钉 QML 数量契约 + Hotbar VM 行为级数量钉）──
    //    用户定稿：**中键 = 复制一整组** —— 对背包物品（hotbar / 主栏 / 合成 / 护甲槽）中键复制的是
    //    maxStackSize(id) 整组，非源槽当前数量（旧 min(count,maxStack) 复制 2 件 → 放回 = 4 的
    //    「2变4 翻倍」）。钉：Inventory.qml 中键 TapHandler maxStackSize(modelData) / copyStackToCursor
    //    maxStackSize(id)（min(count,…) 旧式必须消失）+ Hotbar::maxStackSize 行为级（方块 64 / 工具·桶 1 /
    //    附魔书 1 —— 整组语义的数量单一权威，QML 三处全读它）。
    //    **t975 合法演化**（P-t949(d)/P-t950(f) 先例：用户口径再定稿 → 旧钉随新契约演化）：t896 的
    //    「调色板左键默认 1 个」被用户 8-28 定稿翻案（左键回整组 / 右键接走单件），该键位分配钉由 P-t975
    //    承接；本探针保留 t896 仍拥有的面——中键复制整组（调色板中键 + copyStackToCursor）+ 数量单一权威
    //    + 旧式 min(count,…) 绝迹。回退复制面（中键回源槽数）→ 对应钉红。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile inf(root + QStringLiteral("/src/ui/Inventory.qml"));
        const QString s = inf.open(QIODevice::ReadOnly) ? QString::fromUtf8(inf.readAll()) : QString();
        const int iFn = s.indexOf(QStringLiteral("function copyStackToCursor"));
        const QString fn = iFn >= 0 ? s.mid(iFn, 900) : QString();
        bool okCopy = fn.contains(QStringLiteral("heldCount = root.hotbar.maxStackSize(id)"))
                      && !fn.contains(QStringLiteral("Math.min(count"));
        // 调色板中键 TapHandler：heldBlock = modelData 的第二处出现（第一处在 t975 paletteTake 共用入口内），
        //   邻近段仍直读 maxStackSize(modelData)（t896/t653① 中键复制面，t975 零触碰）。
        const int iTake1 = s.indexOf(QStringLiteral("root.hotbar.heldBlock = modelData"));
        const int iTake2 = iTake1 >= 0 ? s.indexOf(QStringLiteral("root.hotbar.heldBlock = modelData"), iTake1 + 10) : -1;
        bool okMid = iTake2 >= 0 && s.mid(iTake2, 400).contains(QStringLiteral("maxStackSize(modelData)"));
        // 行为级数量钉：整组语义的数量权威（方块/材料 64；桶·附魔书 1 —— 工具段同 1 由桶代表不可堆叠类）。
        Hotbar hbT896;
        const bool okVm = hbT896.maxStackSize(BR::Stone) == 64
                          && hbT896.maxStackSize(RecipeRegistry::RedstoneId) == 64
                          && hbT896.maxStackSize(RecipeRegistry::BucketEmptyId) == 1
                          && hbT896.maxStackSize(RecipeRegistry::EnchantedBookId) == 1;
        const bool ok = okCopy && okMid && okVm;
        if (!ok)
            qInfo().noquote() << "  [t896 diag] copy" << okCopy
                              << "mid" << okMid << "vm" << okVm;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t896 creative copy semantics (left-click quantity leg legally evolved to"
                             " P-t975 per the user's 8-28 re-finalization): middle-click on any inventory"
                             " slot clones a FULL maxStackSize stack to the cursor instead of the slot's"
                             " current count (the old min(count,max) clone let a 2-item slot become 4"
                             " when placed back - the reported 2-becomes-4 doubling); quantity authority"
                             " stays Hotbar::maxStackSize (blocks 64, tools/buckets/armor 1) read by all"
                             " QML copy sites, pinned by source pin plus behavioral VM quantity probe";
    }

    // ── P-t897 羊两修（行为级：吃草门 = 脚下草方块 + 静止 walkPhase 归零）──
    //    ① 吃草动画只在**脚下草方块**触发（t897 ①）：草方块平台（**全场零草丛**）上的羊照常开吃草周期
    //      —— 周期 apply 段把脚下 Grass 写成 Dirt（观测：平台内出现 Dirt 格）。阴性对照：石平台上围一圈
    //      TallGrass 诱饵（旧「前方草丛」逻辑的触发面）—— 新逻辑羊不吃（平台 0 Dirt + 诱饵草丛**全数
    //      完好**；旧逻辑会消耗掉一棵草丛 → 双重判别面）。② 静止走路动画归零（t897 ②）：猪游荡期
    //      walkPhase 推进（观测到非 0）后必在某次 idle 后归 0（旧「冻结于上次相位」下非 0 值永不回 0）。
    //    **flake 修复（主Agent 复跑抓红）**：旧 rig 平台裸放（7×7 无栏），羊 RNG 游走期走出平台缘坠落
    //    到自然地表（log 见「sheep ate grass block at 28 64 25」—— y64 野草地）→ 平台内 Dirt 计数 0 =
    //    经典 mob 游走几何漂移（t836/t882 同款教训）。修法 = **几何围栏**：两平台各加 2 格高石墙环
    //    （1 格会被 mob 越障跳翻过——isJumpObstacle 前方 1 格墙 + 上方空气即跳；2 格上方仍是墙 → 不跳，
    //    羊被物理钉在栏内，只能吃栏内 Grass）。窗口同步放宽 2400→3600 帧（57.6s，覆盖 RNG 掷骰节律的
    //    多轮 idle/吃草/冷却循环）。围栏后「吃到」的判定不再依赖几何停留（栏内全是 Grass，任何一次
    //    idle 吃草都落在断言面内）。
    {
        World wG;
        wG.setWidth(36); wG.setDepth(36); wG.setHeight(96); wG.setSeed(31); // 平台 rig y84/85（局部覆写）
        // 草围栏（外环 7..15 周界 2 高石墙；栏内地表 8..14² 全 Grass、零草丛；墙下垫石防浮空）。
        for (int x = 7; x <= 15; ++x)
            for (int z = 7; z <= 15; ++z) {
                const bool wall = (x == 7 || x == 15 || z == 7 || z == 15);
                wG.setBlock(x, 84, z, wall ? BR::Stone : BR::Grass, 0);
                if (wall) { wG.setBlock(x, 85, z, BR::Stone, 0); wG.setBlock(x, 86, z, BR::Stone, 0); }
            }
        // 石围栏（外环 19..27 周界 2 高石墙；栏内 20..26² 石板 + 诱饵草丛环）。
        for (int x = 19; x <= 27; ++x)
            for (int z = 19; z <= 27; ++z) {
                const bool wall = (x == 19 || x == 27 || z == 19 || z == 27);
                wG.setBlock(x, 84, z, BR::Stone, 0);
                if (wall) { wG.setBlock(x, 85, z, BR::Stone, 0); wG.setBlock(x, 86, z, BR::Stone, 0); }
            }
        int baitCount = 0;
        for (int x = 21; x <= 25; ++x)
            for (int z = 21; z <= 25; ++z)
                if (!(x == 23 && z == 23)) { wG.setBlock(x, 85, z, BR::TallGrass, 0); ++baitCount; } // 草丛诱饵环
        EntityManager em;
        const int sheepG = em.spawnMobTyped(11, 85, 11, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int sheepS = em.spawnMobTyped(23, 85, 23, EntityManager::MobSheep, QStringLiteral("#f5f0e8"), 10);
        const int pig = em.spawnMobTyped(9, 85, 13, EntityManager::MobPig, QStringLiteral("#ee9999"), 10);
        const QVector3D farListener(-1000.0f, 90.0f, -1000.0f);
        bool sawWalk = false, sawReset = false;
        for (int t = 0; t < 3600; ++t) {   // 57.6s：吃草（扫描 ≤1s + 周期 1.2s + 冷却 2s 多轮）+ 游荡走停交替
            em.tick(0.016f, &wG, farListener, 0.3f, 1.8f, false);
            if (pig >= 0 && !sawWalk) {
                if (em.walkPhaseAt(pig) != 0.0f) sawWalk = true;
            } else if (pig >= 0 && sawWalk && !sawReset && em.walkPhaseAt(pig) == 0.0f) {
                sawReset = true;
            }
        }
        // 断言面：草栏内 ≥1 格 Dirt（吃了——栏内全 Grass，任何 idle 吃草都落此面）；石栏内 0 Dirt +
        // 诱饵环 24 棵全在（没吃、也没消耗草丛）；猪走过后归零。
        int grassDirt = 0, stoneDirt = 0, baitLeft = 0;
        for (int x = 8; x <= 14; ++x)
            for (int z = 8; z <= 14; ++z)
                if (wG.blockAt(x, 84, z) == BR::Dirt) ++grassDirt;
        for (int x = 20; x <= 26; ++x)
            for (int z = 20; z <= 26; ++z)
                if (wG.blockAt(x, 84, z) == BR::Dirt) ++stoneDirt;
        for (int x = 21; x <= 25; ++x)
            for (int z = 21; z <= 25; ++z)
                if (!(x == 23 && z == 23) && wG.blockAt(x, 85, z) == BR::TallGrass) ++baitLeft;
        const bool ok = sheepG >= 0 && sheepS >= 0 && pig >= 0
                        && grassDirt >= 1 && stoneDirt == 0
                        && baitLeft == baitCount && sawWalk && sawReset;
        if (!ok)
            qInfo().noquote() << "  [t897 diag] grassDirt" << grassDirt << "stoneDirt" << stoneDirt
                              << "bait" << baitLeft << "/" << baitCount
                              << "sawWalk" << sawWalk << "sawReset" << sawReset
                              << "sheepG" << sheepG << "sheepS" << sheepS << "pig" << pig;
        if (!ok) ++totalFail;
        qInfo().noquote() << (ok ? "PASS" : "FAIL")
                          << "| t897 sheep fixes: graze animation now keys on the grass block UNDERFOOT "
                             "(the own-column support cell - sheep on a pure grass-block platform with zero "
                             "tall grass starts eating cycles and turns the block below to dirt), while the "
                             "negative control keeps every bait tall-grass plant intact on a stone platform "
                             "with zero dirt conversions (old front-column tall-grass logic would have "
                             "consumed a plant); stationary walk animation resets to zero - the pig's "
                             "walkPhase observed advancing later reads exactly 0 after an idle phase (old "
                             "freeze kept the last phase forever, legs stuck mid-stride)";
    }

    // ── P-t898 睡觉瞬移躺床 / 出床回位探针（Game 层真消费端，t814 模式）──
    //   用户 8-25 澄清：睡下时人物**直接瞬移到床上躺平、视角/相机移到床位置**（对齐 MC，非原地睡觉）；
    //   醒来瞬移回床边（MC 下床语义）。断言三组：
    //   (a) 夜间 trySleepAt：m_pos 瞬移到床脚端躺位——foot 格 (x0,z0) state D=+X（head 在 x0-1）→ 躺位
    //       (x0+0.9, y+1, z0+0.5)（foot 格心 +0.4·D：1.8 身长嵌 2.0 床长）、yaw 转床轴朝床尾（D=+X →
    //       front=+X → yaw=-90）、sleeping/sleepLying（躺姿门）真；
    //   (b) wakeUp（中断式瞬醒，public Q_INVOKABLE）：出床瞬移到床周首个可站位格（本 rig：foot +X 邻
    //       (x0+1) 地板支撑 / 头身两格净空 → feet=(x0+1.5, y, z0+0.5)，Y=床层站地面）、sleeping/sleepLying 假；
    //   (c) 白天拒绝零位移副作用：setPhase(0)=正午 → trySleepAt 被拒（瞬移必须在夜间/无怪物两道语义门之后，
    //       被拒不产生位移）。
    {
        PlayerController pc;
        WorldClock clock;
        EntityManager ents; // 空管理器：hostileNearby 恒 false（怪物拒绝路径不触发）
        pc.setWorld(&w);
        pc.setWorldClock(&clock);
        pc.setEntityManager(&ents);
        clock.setPhase(0.5f); // 子夜（skyLight<0.5 → isNight；setPhase 即时重派生）

        const auto [x0, z0] = nextSlot();
        const int y = kRigY;
        // 工作体积先清空再搭（t897/review#5 教训：高空「必空」不是生成器不变量；确定性净空带）。
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 3; ++dy)
                    w.setBlock(x0 + dx, y + dy, z0 + dz, BR::Air, 0);
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                placeRigBlock(w, x0 + dx, y - 1, z0 + dz, BR::Stone, 0); // 支撑地板（床 + 出床站位同层）
        placeRigBlock(w, x0, y, z0, BR::BedWhite, quint8(0));     // foot：D=+X（bit[1:0]=0）→ head 在 x0-1
        placeRigBlock(w, x0 - 1, y, z0, BR::BedWhite, quint8(8)); // head（bit3=1）

        pc.trySleepAt(x0, y, z0);
        const QVector3D lie = pc.feetPosition();
        // (d) review27 #1 QML 契约面：sleepLying 必须在 metaobject 属性表内（QML 属性解析走 QMetaObject，
        //     旧版缺 Q_PROPERTY 声明 → Main.qml player.sleepLying 解析 undefined（falsy）→ F5 躺姿 100%
        //     失效；C++ 直调探针测不到该面——review26 #4 同族教训）。经 property() 读回（QML 同路径）。
        const QMetaObject *mo = pc.metaObject();
        const int propIdx = mo->indexOfProperty("sleepLying");
        const bool metaLieA = propIdx >= 0 && mo->property(propIdx).read(&pc).toBool();
        const bool okA = pc.sleeping() && pc.sleepLying() && metaLieA
                         && std::abs(lie.x() - (x0 + 0.9f)) < 1e-3f
                         && std::abs(lie.y() - (y + 1.0f)) < 1e-3f
                         && std::abs(lie.z() - (z0 + 0.5f)) < 1e-3f
                         && std::abs(pc.yaw() - (-90.0f)) < 1e-3f;
        pc.wakeUp();
        const QVector3D out = pc.feetPosition();
        const bool metaLieB = propIdx >= 0 && !mo->property(propIdx).read(&pc).toBool();
        const bool okB = !pc.sleeping() && !pc.sleepLying() && metaLieB
                         && std::abs(out.x() - (x0 + 1.5f)) < 1e-3f
                         && std::abs(out.y() - float(y)) < 1e-3f
                         && std::abs(out.z() - (z0 + 0.5f)) < 1e-3f;
        clock.setPhase(0.0f); // 正午（/time 特权指令允许设相，PLAN §2-H 与睡觉单向不冲突）
        const QVector3D beforeDay = pc.feetPosition();
        pc.trySleepAt(x0, y, z0);
        const bool okC = !pc.sleeping() && pc.feetPosition() == beforeDay;
        const bool okT898 = okA && okB && okC;
        if (!okT898)
            qInfo().noquote() << "  [t898 diag] lie=" << lie.x() << lie.y() << lie.z()
                              << "yaw=" << pc.yaw() << " out=" << out.x() << out.y() << out.z()
                              << " sleeping=" << pc.sleeping() << " lying=" << pc.sleepLying()
                              << " propIdx=" << propIdx
                              << " metaLieA=" << metaLieA << " metaLieB=" << metaLieB;
        if (!okT898) ++totalFail;
        qInfo().noquote() << (okT898 ? "PASS" : "FAIL")
                          << "| t898 bed-sleep teleport: right-click bed at night teleports the player "
                             "flat onto the bed (feet pinned to the foot-cell end offset 0.4 along the "
                             "head->foot axis so the 1.8-block body nests inside the 2-block bed, Y = bed "
                             "top, yaw rotated to the bed axis looking toward the foot, lying-pose gate "
                             "on - and review27 #1: the gate is a real Q_PROPERTY read via "
                             "QMetaObject::indexOfProperty/property() on both lying and woken states, "
                             "the exact resolution path QML uses; the old bare member function resolved "
                             "to undefined in Main.qml bindings), interrupt-style wake teleports out to "
                             "the first standable cell beside "
                             "the bed at floor level (MC get-out-of-bed semantics, pose gate off), and a "
                             "daytime refusal produces zero displacement side effects (teleport strictly "
                             "after the night/monster semantic gates)";
    }

    // ── P-t900 垃圾桶语义终版（VM 行为级 + 源码钉；用户 8-25 定稿）──
    //   定稿原话：「普通左键=清光标持有（t839 语义保持）+ shift+左键=清空整个背包」。QML 点击路由不可由
    //   本 harness 直驱（Inventory.qml 需全模块场景）→ 双腿：
    //   (a) 行为级：Hotbar VM 填满（hotbar 9 + main 27 + 光标持有）→ 执行 QML shift 分支的同序清空序列
    //       （setStack 9 + mainSetStack 27 + setHeldBlock(0)）→ 全槽读空（证明该调用面足以清空整个背包，
    //       无隐藏残留槽态）；普通左键两档语义（清光标 / 选中槽单格）为 t839 既有语义，本探针不重复钉。
    //   (b) 源码钉：Inventory.qml 销毁槽块（滤注释）——MouseArea（TapHandler 不分辨修饰键，t700 教训）+
    //       ShiftModifier 分流分支含 9 槽 setStack 循环 + mainCount 槽 mainSetStack 循环 + 光标清空 +
    //       普通左键两档（heldBlock 整组 / 选中槽单格）保留。
    {
        Hotbar hb;
        for (int s = 0; s < 9; ++s) hb.setStack(s, int(BR::Cobble), 32);
        for (int m = 0; m < hb.mainCount(); ++m) hb.mainSetStack(m, int(BR::Planks), 16);
        // review27 #21①：盔甲 4 槽并入清空序列（armorSetStack 独立存储，旧版 shift 清空漏扫 = 残留）。
        //   填充用真护甲 id 且部位对槽（armorSetStack 拒非护甲 / 部位不符——Glass 会被静默拒掉成假绿）。
        for (int a = 0; a < 4; ++a)
            hb.armorSetStack(a, RecipeRegistry::ArmorIdBase + a, 1);
        hb.setHeldBlock(int(BR::Glass));
        hb.setHeldCount(8);
        // QML shift 分支同序清空序列（Inventory.qml 销毁槽 MouseArea onClicked ShiftModifier 支；
        //   review27 #21① 后含盔甲四槽——合成格是 QML 本地态，由 review27-21 源码钉看守）。
        for (int s = 0; s < 9; ++s) hb.setStack(s, 0, 0);
        for (int m = 0; m < hb.mainCount(); ++m) hb.mainSetStack(m, 0, 0);
        for (int a = 0; a < 4; ++a) hb.armorSetStack(a, 0, 0);
        hb.setHeldBlock(0);
        bool okA = hb.heldBlock() == 0 && hb.heldCount() == 0;
        for (int s = 0; s < 9 && okA; ++s)
            if (hb.blockIdAt(s) != 0 || hb.countAt(s) != 0) okA = false;
        for (int m = 0; m < hb.mainCount() && okA; ++m)
            if (hb.mainBlockIdAt(m) != 0 || hb.mainCountAt(m) != 0) okA = false;
        for (int a = 0; a < 4 && okA; ++a)
            if (hb.armorBlockIdAt(a) != 0 || hb.armorCountAt(a) != 0) okA = false;
        if (!okA)
            qInfo().noquote() << "  [t900 diag] held=" << hb.heldBlock()
                              << " h0=" << hb.blockIdAt(0) << " m0=" << hb.mainBlockIdAt(0)
                              << " a0=" << hb.armorBlockIdAt(0);

        // (b) 源码钉（t879/t893 先例：断言销毁槽块的路由文本；锚定 destroyWrap 块而非全文件首
        //     MouseArea——面板根遮罩自身也是 MouseArea）。review27 #21① 扩段后块跨度增长 → 窗口改
        //     「tier② 行 + 60」动态收尾（普通左键档②是块内 onClicked 的末行，其后即 MouseArea 收口）。
        bool okPin = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
            QFile qf(root + QStringLiteral("/src/ui/Inventory.qml"));
            const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("id: destroyWrap"));
            const int iTier2 = t.indexOf(
                QStringLiteral("root.hotbar.setStack(root.hotbar.selectedSlot, 0, 0)"), i0);
            if (i0 < 0 || iTier2 < 0) {
                qInfo().noquote() << "  [t900 pin diag] destroyWrap block miss"
                              << (i0 >= 0) << "tier2" << (iTier2 >= 0);
            } else {
                const QString seg = t.mid(i0, iTier2 - i0 + 60); // 销毁槽块（图标 Canvas + 注释 + 点击处理器；
                                                                 //   负向断言用元素声明形「TapHandler {」防注释文本误中）
                okPin = seg.contains(QStringLiteral("MouseArea"))                       // TapHandler 不分辨修饰键（t700）→ MouseArea
                        && seg.contains(QStringLiteral("mouse.modifiers & Qt.ShiftModifier")) // shift 分流分支
                        && seg.contains(QStringLiteral("root.hotbar.mainSetStack(m, 0, 0)"))   // main 27 槽清空
                        && seg.indexOf(QStringLiteral("setStack(s, 0, 0)"))
                               < seg.indexOf(QStringLiteral("mainSetStack(m, 0, 0)"))  // hotbar 9 槽先行
                        && seg.contains(QStringLiteral("root.hotbar.armorSetStack(a, 0, 0)")) // review27 #21① 盔甲 4 槽
                        && seg.contains(QStringLiteral("root.hotbar.heldBlock = 0"))   // 光标清空 + 普通左键档①
                        && seg.contains(QStringLiteral("root.hotbar.setStack(root.hotbar.selectedSlot, 0, 0)")) // 档②
                        && !seg.contains(QStringLiteral("TapHandler {"));              // 旧事件源元素形态退役（块内）
            }
            if (!okPin)
                qInfo().noquote() << "  [t900 pin diag] block found=" << (i0 >= 0)
                                  << " seg checks failed";
        }
        const bool okT900 = okA && okPin;
        if (!okT900) ++totalFail;
        qInfo().noquote() << (okT900 ? "PASS" : "FAIL")
                          << "| t900 trash-slot final semantics (user 8-25): plain left click keeps t839 tiers "
                             "(cursor-held stack destroyed / selected single slot cleared when empty-handed), "
                             "shift+left-click clears the ENTIRE inventory (behavioral leg proves the exact QML "
                             "call sequence - 9 setStack + 27 mainSetStack + 4 armorSetStack (review27 #21) + "
                             "heldBlock reset - leaves zero residue in every slot read, armor included; the "
                             "crafting-grid half is QML-local state pinned by review27-21; source pin proves "
                             "the MouseArea modifier split and retires the old TapHandler form - TapHandler "
                             "cannot see modifiers, t700 lesson)";
    }

    // ── P-t901 画作背面木板源码钉（t837 未愈返修；纯视觉项轻量源码钉，t781/t893 先例）──
    //   用户「背面仍全透明」根因：画面 BillboardQuad 默认背面剔除 → 墙后侧（玻璃墙 / 透视支撑后）看画，
    //   quad 被剔 = 无像素。修法 = 第二张反向法线 quad（绕 Y 180°）贴木板背板（MC 语义：画作背面木板）。
    //   QML delegate 渲染不可由本 harness 直驱 → 源码钉 paintingDelegate 块：背 quad 的 180° 欧拉 +
    //   default_wood.png（= 图集 tile 8 planks 同源）+ 背面略压暗 baseColor 存在。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile qf(root + QStringLiteral("/src/ui/Main.qml"));
        const QString t = qf.open(QIODevice::ReadOnly) ? QString::fromUtf8(qf.readAll()) : QString();
        const int i0 = t.indexOf(QStringLiteral("id: paintingDelegate"));
        bool okT901 = false;
        if (i0 < 0) {
            qInfo().noquote() << "  [t901 pin diag] paintingDelegate block miss";
        } else {
            const QString seg = t.mid(i0, 4300); // delegate 块（双 quad + 材质全在内，实测跨度 ~4.3k）
            const int back0 = seg.indexOf(QStringLiteral("Qt.vector3d(0, 180, 0)")); // 背 quad 反向法线欧拉
            const int wood = seg.indexOf(QStringLiteral("qrc:/textures/default_wood.png"));
            const int dim = seg.indexOf(QStringLiteral("0.72, 0.72, 0.72"));
            okT901 = back0 > 0 && wood > back0 && dim > wood;   // 三件同块依序（背 quad → 木板贴图 → 压暗）
        }
        if (!okT901) ++totalFail;
        qInfo().noquote() << (okT901 ? "PASS" : "FAIL")
                          << "| t901 painting back board: second reverse-normal quad (Y+180 euler, "
                             "backface-culled pair so no coplanar z-fight, offset 1/64 wall-ward) carries "
                             "the plank board texture default_wood.png (same source file as atlas tile 8) "
                             "slightly dimmed - MC semantics: a painting's back is a wooden board, fixing "
                             "the fully-transparent back visible through glass walls (source pin on the "
                             "paintingDelegate block; visual confirmation pending user playtest)";
    }

    // ── P-t902 耕地图标面源码钉（纯视觉项轻量源码钉；atlasIconSpecForBlock 是文件内 static，行为级不可
    //    直调 → 钉 case 存在 + def 字段契约）──
    //   用户「两面都是耕地」根因：Farmland def.frontTile 字段被 mesher 复用为**湿态顶面瓦片 27**（无 -Z
    //   前面语义），ShapeFull 泛化 addBox(topT, sideT, frontT) 把它喂给图标左前面 → 顶=干耕 + 左前=湿耕 +
    //   右=泥。修法 = 显式 case 钉 side/front=sideT（dirt 单一权威）。双腿：(a) def 契约（top=26 / side=2 /
    //   front=27——字段复用事实本身钉死，泛化路径对耕地必错）；(b) 源码钉 spec 的 Farmland case 用
    //   (topT, sideT, sideT) 且先于 ShapeFull 泛化（boxes 非空则泛化不跑）。
    {
        const BR::BlockDef &fd = BR::def(BR::Farmland);
        const bool okDef = fd.topTile == 26 && fd.sideTile == 2 && fd.frontTile == 27;
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile rf(root + QStringLiteral("/src/Core/resourcepackmanager.cpp"));
        const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
        const int iCase = t.indexOf(QStringLiteral("case BlockRegistry::Farmland:"));
        bool okPin = false;
        if (iCase < 0) {
            qInfo().noquote() << "  [t902 pin diag] Farmland case miss in atlasIconSpecForBlock";
        } else {
            const QString seg = t.mid(iCase, 1600); // case 块（注释 + addBox 全在内）
            const int box = seg.indexOf(QStringLiteral("addBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, topT, sideT, sideT)"));
            okPin = box > 0
                    && seg.indexOf(QStringLiteral("case BlockRegistry::Cactus:")) > box; // 在泛化前（② 特型段）
        }
        const bool okT902 = okDef && okPin;
        if (!okT902) {
            ++totalFail;
            qInfo().noquote() << "  [t902 diag] def" << fd.topTile << fd.sideTile << fd.frontTile
                              << "pin" << okPin;
        }
        qInfo().noquote() << (okT902 ? "PASS" : "FAIL")
                          << "| t902 farmland item icon faces: explicit spec case pins side AND front faces "
                             "to the dirt side-tile (single authority def.sideTile) with only the top "
                             "carrying the tilled texture - the generic ShapeFull path fed the mesher-reused "
                             "frontTile (wet-farmland top tile 27) into the icon's left-front face, so the "
                             "icon read as farmland on both visible faces; def field-reuse contract pinned "
                             "(top=26 dry / side=2 dirt / front=27 wet-top) and cache family bumped "
                             "icon6->icon7 so stale on-disk icons regenerate; visual confirmation pending "
                             "user playtest";
    }

    // ── P-t903 草丛支撑置换失撑探针（World 层行为级；放置面真值表已随 t847 探针收紧同步钉）──
    //   用户定稿「只能放草方块（泥土也不行）」+ 失撑链同口径：草丛唯一合法支撑 = 草方块 → 支撑被**置换**为
    //   非草面（非破 Air）也失撑掉落。三腿：
    //   (a) 羊吃草路径：setWaterSilent(Grass→Dirt)（t897 羊吃消耗走本入口）→ 正上方草丛清 Air +
    //       blockDroppedAsItem 掉 dropId(TallGrass)（种子族，运行期读表）；
    //   (b) 通用置换路径：setBlockSilent(Dirt→Farmland) 锄地语义 → 草丛同掉（任意非草面置换）；
    //   (c) 阴性对照（族口径不扩大）：花下泥土置换成耕地（花合法面含 Farmland）→ 花**不**掉（t507
    //       「置换不掉」族口径对花 / 蘑菇保留，只草丛收口）。
    {
        const auto [x0, z0] = nextSlot();
        placeRigBlock(w, x0, kRigY, z0, BR::Grass, 0);
        placeRigBlock(w, x0, kRigY + 1, z0, BR::TallGrass, 0);
        const int dropsA0 = dropItemCount;
        const quint8 lastA0 = lastDropId;
        Q_UNUSED(lastA0);
        w.setWaterSilent(x0, kRigY, z0, BR::Dirt, 0); // 羊吃草消耗路径（t897 同入口）
        const bool okA = w.blockAt(x0, kRigY + 1, z0) == quint8(BR::Air)
                     && dropItemCount == dropsA0 + 1
                     && lastDropId == BR::dropId(BR::TallGrass);

        const auto [x1, z1] = nextSlot();
        placeRigBlock(w, x1, kRigY, z1, BR::Grass, 0);
        placeRigBlock(w, x1, kRigY + 1, z1, BR::TallGrass, 0);
        const int dropsB0 = dropItemCount;
        w.setBlockSilent(x1, kRigY, z1, BR::Farmland, 0); // 任意非草面置换（锄地语义）
        const bool okB = w.blockAt(x1, kRigY + 1, z1) == quint8(BR::Air)
                     && dropItemCount == dropsB0 + 1
                     && lastDropId == BR::dropId(BR::TallGrass);

        const auto [x2, z2] = nextSlot();
        placeRigBlock(w, x2, kRigY, z2, BR::Dirt, 0);
        placeRigBlock(w, x2, kRigY + 1, z2, BR::FlowerRed, 0);
        const int dropsC0 = dropItemCount;
        w.setBlockSilent(x2, kRigY, z2, BR::Farmland, 0); // 花合法面含耕地 → 不掉（族口径保留）
        const bool okC = w.blockAt(x2, kRigY + 1, z2) == quint8(BR::FlowerRed)
                     && dropItemCount == dropsC0;

        const bool okT903 = okA && okB && okC;
        if (!okT903)
            qInfo().noquote() << "  [t903 diag] okA" << okA << "okB" << okB << "okC" << okC
                              << "a=" << int(w.blockAt(x0, kRigY + 1, z0))
                              << "b=" << int(w.blockAt(x1, kRigY + 1, z1))
                              << "c=" << int(w.blockAt(x2, kRigY + 1, z2));
        if (!okT903) ++totalFail;
        qInfo().noquote() << (okT903 ? "PASS" : "FAIL")
                          << "| t903 tallgrass support-replacement lost-support: grass block's only legal "
                             "support is another grass block (placement tightened, dirt rejected), and the "
                             "lost-support hook now drops the tall grass when its support is REPLACED by any "
                             "non-grass face - the sheep-graze path (setWaterSilent Grass->Dirt, t897 entry) "
                             "and the generic silent replacement (Dirt->Farmland hoe semantics) both clear the "
                             "plant and drop its dropId, while the flower negative control stays put on "
                             "replaced-but-still-legal farmland (family replacement-caliber kept for flowers/"
                             "mushrooms, only tallgrass tightened)";
    }

    // ── P-t857 F3 渲染统计真值源码钉（R19.14 性能起步批；源序钉先例 = review #4/#5 的 rpm 源序探针）──
    //   buildF3Text 的 draw 行自 t857 起读 view3d.renderStats 真值（drawCallCount / drawVertexCount /
    //   renderPassCount），旧 ~drawEst 估算公式（visibleSegmentCount + itemLive + mobLive + torches + 6）
    //   退役。钉三件事：① 函数体必经 renderStats 真值四读；② 估算公式 token（drawEst）在函数体内绝迹；
    //   ③ View3D 上 extendedDataCollectionEnabled 绑 f3Visible（真值收集的开关契约——漏绑则真值恒 0，
    //   F3 显示静默失真）。QML 无 static_assert 面 → 源码文本钉（滤 // 注释行后切片断言）。
    {
        QString qmlPath;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            for (const QString &c : candidates) {
                if (QFile::exists(c)) { qmlPath = c; break; }
            }
        }
        bool okT857 = true;
        if (qmlPath.isEmpty()) {
            qInfo().noquote() << "  [t857 note] Main.qml not found near exe - source-pin skipped";
        } else {
            QFile qmlF(qmlPath);
            if (!qmlF.open(QIODevice::ReadOnly)) {
                okT857 = false;
                qInfo().noquote() << "  [t857 diag] failed to open" << qmlPath;
            } else {
                QString codeText;
                const QString rawText = QString::fromUtf8(qmlF.readAll());
                for (const QString &line : rawText.split(QLatin1Char('\n'))) {
                    if (line.trimmed().startsWith(QLatin1String("//")))
                        continue; // 滤 // 注释行（探测目标是语句文本，注释里的 token 会干扰）
                    codeText += line;
                    codeText += QLatin1Char('\n');
                }
                const int fnStart = codeText.indexOf(QStringLiteral("function buildF3Text()"));
                const int fnEnd = fnStart >= 0 ? codeText.indexOf(QStringLiteral("\n    }"), fnStart) : -1;
                if (fnStart < 0 || fnEnd < 0) {
                    okT857 = false;
                    qInfo().noquote() << "  [t857 diag] buildF3Text body not found fnStart" << fnStart
                                      << "fnEnd" << fnEnd;
                } else {
                    const QString body = codeText.mid(fnStart, fnEnd - fnStart);
                    const bool hasTruth = body.contains(QStringLiteral("view3d.renderStats"))
                                          && body.contains(QStringLiteral("drawCallCount"))
                                          && body.contains(QStringLiteral("drawVertexCount"))
                                          && body.contains(QStringLiteral("renderPassCount"));
                    const bool noEstimate = !body.contains(QStringLiteral("drawEst"));
                    const bool hasEnable = codeText.contains(
                            QStringLiteral("renderStats.extendedDataCollectionEnabled: window.f3Visible"));
                    okT857 = hasTruth && noEstimate && hasEnable;
                    if (!okT857)
                        qInfo().noquote() << "  [t857 diag] truth" << hasTruth << "noEstimate" << noEstimate
                                          << "enableGate" << hasEnable;
                }
            }
        }
        if (!okT857) ++totalFail;
        qInfo().noquote() << (okT857 ? "PASS" : "FAIL")
                          << "| t857 F3 render-stats truth: buildF3Text draw line reads view3d.renderStats "
                             "real values (drawCallCount/drawVertexCount/renderPassCount via RenderStats, "
                             "extended collection gated on f3Visible at the View3D) and the legacy ~drawEst "
                             "sum formula (visibleSegmentCount+items+mobs+torches+6) is retired - estimate "
                             "drift vs backend reality (transparency pass splits, frustum culling, "
                             "instancing batches) no longer misleads perf work";
    }

    // ── P-t860 cutout 段折叠（R19.14 试验项，保留交付）：行为级 + 源码钉双探针 ──
    //   背景：t442 起 terrain 段材质已带 alphaMode:Mask + alphaCutoff:0.5（与 cutout 段材质逐字相同，
    //   leaves cutout 实证生效）→ 独立 cutout 段失去存在必要。t860 把 cross（草丛/作物/树苗）+ 门（t638
    //   窗格）+ 活板门（t723 栅格孔）顶点并入 terrain 段 mesh，QML 停建 cutout 段 Model（每 chunk 6 段 →
    //   5 段，600 Model 满配 → 500）。行为级断言：terrain 段 ChunkGeometry 在放置 TallGrass 后顶点数**增加**
    //   （折叠前该格被路由走、terrain 顶点不变；cross 是 ShapeNone 非实体 → 不影响邻居面剔除，顶点差 = 纯
    //   cross 贡献）。review28 #4：降级杠杆显式开关化（ChunkGeometry.cutoutFolded + Main.qml
    //   cutoutSegmentRestored 单开关三面联动）——行为级加两态互斥腿（false = cross 退出 terrain 段回基线 /
    //   true = 复原），源码钉改钉联动四件（开关声明 / 守卫实例化 / cutoutFolded 绑定 / segmentsPerChunk
    //   派生），任何半恢复（只改一处）即红；旧「无 createObject + 段数 5」静态钉退役（照旧注释恢复
    //   createObject 会两段同发 z-fighting——正是 review28 #4 的缺陷）。
    {
        const auto [x860, z860] = nextSlot();
        const int cx860 = x860 / 16, cz860 = z860 / 16;
        ChunkGeometry geoT;
        geoT.setWorld(&w);
        geoT.setCx(cx860);
        geoT.setCz(cz860);
        // 扫真空位（t799/t814 教训：rig 槽位地形可及 y≥42，「某高度以上必空」不成立——Stone 放进已实体格
        //   = 无变化早退不 emit，首建不触发）。找连续两格 Air：Stone 落下格（制造脏 + 同步 worldChanged
        //   重建取基线 v0），TallGrass 落上格（cross 折叠顶点差分）。
        int y860 = kRigY + 1;
        while (y860 < 46
               && (w.blockAt(x860, y860, z860) != BR::Air || w.blockAt(x860, y860 + 1, z860) != BR::Air))
            ++y860;
        w.setBlock(x860, y860, z860, BR::Stone, 0);
        const int v0 = geoT.vertexCount();
        w.setBlock(x860, y860 + 1, z860, BR::TallGrass, 0); // Air → TallGrass（ShapeNone 不动邻居剔面）
        const int v1 = geoT.vertexCount(); // setBlock 同步 emit worldChanged → 脏 chunk 即时重建
        const bool okFold = v0 > 0 && v1 > v0; // cross 顶点计入 terrain 段 mesh（折叠生效签名）
        if (!okFold)
            qInfo().noquote() << "  [t860 diag] y=" << y860 << "v0=" << v0 << "v1=" << v1
                              << "(cross must add terrain-segment vertices when folded)";
        // review28 #4 行为级（**同态基线**——两态的 chunk 内容集不同，跨态比顶点必混入世界生成 cross
        //   的路由差，v0 不能复用）：显式开关两态互斥——恢复态（false）基线下放 TallGrass 顶点**不变**
        //   （跳过清单回归，cross 让位 cutout 段）；翻回 true 顶点增长（折叠全收）；再翻 false 回基线
        //   （round-trip 无残留）。回退本开关（else 恒全收）→ 恢复态放置即增长 → 第一断言红。
        geoT.setCutoutFolded(false);
        w.setBlock(x860, y860 + 1, z860, BR::Air, 0); // 清 cross 位重立恢复态基线
        const int u0 = geoT.vertexCount();
        w.setBlock(x860, y860 + 1, z860, BR::TallGrass, 0);
        const int u1 = geoT.vertexCount();
        const bool okUnfolded = u0 > 0 && u1 == u0;
        geoT.setCutoutFolded(true);
        const int f1 = geoT.vertexCount(); // setter 触发 Dirty 即时重建
        const bool okRefold = f1 > u1;
        geoT.setCutoutFolded(false);
        const int u2 = geoT.vertexCount();
        const bool okRoundTrip = u2 == u0;
        if (!okUnfolded || !okRefold || !okRoundTrip)
            qInfo().noquote() << "  [t860 diag] switch u0=" << u0 << "u1=" << u1
                              << "f1=" << f1 << "u2=" << u2
                              << "(unfolded must ignore the cross placement; refold must absorb it)";
        geoT.setCutoutFolded(true); // 复位默认折叠态
        w.setBlock(x860, y860 + 1, z860, BR::Air, 0); // 还原（rig 清洁）
        w.setBlock(x860, y860, z860, BR::Air, 0);

        // 源码钉（review28 #4 联动钉）：恢复路径三面同源——① 总开关声明默认 false（折叠态出厂）；
        //   ② crossChunkComp 实例化受该开关守卫（守卫行缺失 = 有人恢复了无条件 createObject → 双发）；
        //   ③ terrainGeo 的 cutoutFolded 绑定到同一开关（绑定缺失 = 恢复后 terrain 仍全收 → 双发）；
        //   ④ segmentsPerChunk 由同一开关派生（5/6）。四断言互锁：任何半恢复（只改一处）即红。
        bool okPin860 = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            QString qml;
            for (const QString &c : candidates) {
                QFile f(c);
                if (f.open(QIODevice::ReadOnly)) { qml = QString::fromUtf8(f.readAll()); break; }
            }
            if (qml.isEmpty()) {
                qInfo().noquote() << "  [t860 note] Main.qml not found near exe - source-pin skipped";
                okPin860 = true; // 行为级断言仍有效（源码钉缺席不判红，同 r24 note 先例）
            } else {
                // 滤 // 注释行后判定（降级杠杆注释行被滤掉，只钉真代码）。
                QString code;
                for (const QString &line : qml.split(QLatin1Char('\n'))) {
                    const QString t = line.trimmed();
                    if (t.startsWith(QLatin1String("//")) || t.startsWith(QLatin1String("*"))
                        || t.startsWith(QLatin1String("/*")))
                        continue;
                    code += line;
                    code += QLatin1Char('\n');
                }
                const bool switchDeclared = code.contains(QStringLiteral("property bool cutoutSegmentRestored: false"));
                const bool createGuarded = code.contains(QStringLiteral("if (window.cutoutSegmentRestored)"))
                                           && code.contains(QStringLiteral("objs.push(crossChunkComp.createObject"));
                const bool foldedBound = code.contains(QStringLiteral("cutoutFolded: !window.cutoutSegmentRestored"));
                const bool segDerived = code.contains(QStringLiteral("const segmentsPerChunk = window.cutoutSegmentRestored ? 6 : 5"));
                okPin860 = switchDeclared && createGuarded && foldedBound && segDerived;
                if (!okPin860)
                    qInfo().noquote() << "  [t860 diag] switchDeclared" << switchDeclared
                                      << "createGuarded" << createGuarded
                                      << "foldedBound" << foldedBound
                                      << "segDerived" << segDerived;
            }
        }
        const bool okT860 = okFold && okUnfolded && okRefold && okRoundTrip && okPin860;
        if (!okT860) ++totalFail;
        qInfo().noquote() << (okT860 ? "PASS" : "FAIL")
                          << "| t860 cutout segment folded into terrain: terrain-segment ChunkGeometry "
                             "absorbs cross-billboard vertices (TallGrass placement grows the terrain "
                             "mesh, pre-fold routing diverted it to a separate cutout model) - sound "
                             "because both materials became literally identical after t439/t442 "
                             "(alphaMode Mask + cutoff 0.5), same vertex pipeline, same depth-writing "
                             "opaque pass; review28 #4: the documented degrade lever is now an explicit "
                             "switch - ChunkGeometry.cutoutFolded=false behaviorally sheds cross "
                             "vertices back to baseline (pre-t860 skip list restored, mutually "
                             "exclusive with a restored cutout segment, no double-emission z-fighting) "
                             "and refolding restores them, while the Main.qml source pin locks the "
                             "single-switch coupling (cutoutSegmentRestored declared false + guarded "
                             "crossChunkComp instantiation + cutoutFolded binding + segmentsPerChunk "
                             "derivation all keyed to one property - any half-restored path goes red)";
    }

    // ── P-t858 经验球 instancing 试点探针（R19.14；Game 层 feeder 直调，实例表内容级）──
    //   XpOrbInstancing（QQuick3DInstancing 子类，公开 API、无自定义 shader——RHI 囚笼合规）把整族
    //   经验球压成 1 Model / 1 draw。钉实例表内容派生契约：① 无数据源 / 无活体 → 空表；② spawn 后
    //   表条目位置 = 管理器 posAt ± bob 带（Y ∈ [pos.y, pos.y+0.12]，XZ 精确——bob 只加 Y，解析式
    //   0.06*(1-cos) ∈ [0,0.12]）；③ clearAll（切世界清场）后表清空（slot-reuse 的 alive=false 槽
    //   不进表）。掉落物各族保持逐 Model 的范围取舍在 xporbinstancing.h 头注释 + dev-plan 记录钉死。
    {
        XpOrbManager orbs858;
        XpOrbInstancing feed858;
        QVector3D p858;
        const bool okEmpty = !feed858.probeFirstInstancePosition(&p858); // 无 manager → 空表
        feed858.setManager(&orbs858);
        const bool okNoLive = !feed858.probeFirstInstancePosition(&p858); // 有 manager 无活体 → 空表
        orbs858.spawnOrb(10, 20, 30, 3);
        const bool okOne = feed858.probeFirstInstancePosition(&p858)
                           && std::abs(p858.x() - 10.5f) < 1e-4f
                           && std::abs(p858.z() - 30.5f) < 1e-4f
                           && p858.y() >= 20.5f - 1e-4f && p858.y() <= 20.5f + 0.12f + 1e-4f;
        orbs858.spawnOrb(11, 20, 30, 7);
        orbs858.clearAll();
        const bool okCleared = !feed858.probeFirstInstancePosition(&p858);
        const bool okT858 = okEmpty && okNoLive && okOne && okCleared;
        if (!okT858)
            qInfo().noquote() << "  [t858 diag] empty" << okEmpty << "noLive" << okNoLive
                              << "one" << okOne << "pos" << p858.x() << p858.y() << p858.z()
                              << "cleared" << okCleared;
        if (!okT858) ++totalFail;
        qInfo().noquote() << (okT858 ? "PASS" : "FAIL")
                          << "| t858 xp-orb instancing pilot: XpOrbInstancing feeder derives the "
                             "instance table from XpOrbManager slots (empty without manager or live "
                             "orbs, entry position = slot pos + analytic bob band on Y only, "
                             "clearAll empties the table since dead slots never enter it) - whole "
                             "family renders as one Model/one draw replacing per-orb delegates, "
                             "pickups/magnetism stay pure C++ in the manager; drop-item families "
                             "stay per-Model by scope decision (per-item textures/geometries need "
                             "per-itemId bucketed models, deferred with rationale)";
    }

    // ── P-t1027a 掉落物 instancing 治理首批探针（R19.21；Game 层 feeder 直调，t858 先例）──
    //   方块整立方族（ItemEntityManager::isPlainCubeDrop：泥土/石头/圆石/木板/砂/羊毛16色…挖掘产出主面，
    //   活体数期望最大族）按 itemId 分桶 × per-id instanced Model（族内 per-id 差异只在 BlockCube 几何
    //   UV，同 id 才能共享几何）。钉契约：① 族分桶正确性——feeder 只收纳同 id 整立方活体，异族（torch
    //   3D 族 / 材料段）与异族 id 永不进表，条目数 = 活体数；② 实例表内容——XZ 精确 = 实体世界位、
    //   Y ∈ [pos.y, pos.y+0.15]（bob 解析带，公式逐字对齐旧 delegate 两段 InOutSine）、scale 0.3 均匀、
    //   纯 Y 轴旋转（euler x/z≈0；rotY 相位 = 墙钟 + slot×0.37s 错峰，不作精确断言）；③ 拾取 / 移除 /
    //   clearAll 反射——表随活体集同步缩；④ 合并不加实例——同 id 就近 spawn（≤kMergeRadius=2）走 count
    //   累加，feeder 条数不变（1 实例 count=3 = 1 条目）；⑤ 数据链零改动——feeder 只读，拾取判定 /
    //   despawn / 物理全在 C++ 管理器（既有掉落腿覆盖）。
    {
        ItemEntityManager items1027;
        BlockDropInstancing fDirt1027, fCobble1027, fTorch1027;
        fDirt1027.setManager(&items1027);   fDirt1027.setFamilyId(int(BR::Dirt));
        fCobble1027.setManager(&items1027); fCobble1027.setFamilyId(int(BR::Cobble));
        fTorch1027.setManager(&items1027);  fTorch1027.setFamilyId(int(BR::Torch)); // 3D 族 id：任何整立方桶不收
        const bool okEmpty1027 = fDirt1027.probeInstanceCount() == 0; // 有 manager 无活体 → 空表
        // 格距 ≥3（> kMergeRadius=2，防 spawn 就近合并塌缩活体数——t490fix 合并是正确行为，rig 必须绕开）
        items1027.spawnItem(10, 40, 10, BR::Dirt, 1);
        items1027.spawnItem(14, 40, 10, BR::Dirt, 2);
        items1027.spawnItem(18, 40, 10, BR::Dirt, 1);
        items1027.spawnItem(22, 40, 10, BR::Cobble, 1);
        items1027.spawnItem(26, 40, 10, BR::Cobble, 3);
        items1027.spawnItem(30, 40, 10, BR::Torch, 1);   // 3D 形状族：feeder 不收（旧 delegate 路径）
        items1027.spawnItem(34, 40, 10, 0x200, 1);       // 材料段（木棒）：feeder 不收
        const int cntDirt = fDirt1027.probeInstanceCount();
        const int cntCobble = fCobble1027.probeInstanceCount();
        const int cntTorch = fTorch1027.probeInstanceCount();
        bool ok1027 = okEmpty1027 && cntDirt == 3 && cntCobble == 2 && cntTorch == 0;
        for (int k = 0; k < 3; ++k) { // ② 实例内容（槽序 = 表序，spawn 升序）
            QVector3D p1027, s1027; QQuaternion q1027;
            ok1027 = ok1027 && fDirt1027.probeInstanceAt(k, &p1027, &s1027, &q1027);
            const float expX = 10.5f + 4.0f * float(k);
            ok1027 = ok1027 && std::abs(p1027.x() - expX) < 1e-4f && std::abs(p1027.z() - 10.5f) < 1e-4f;
            ok1027 = ok1027 && p1027.y() >= 40.5f - 1e-4f && p1027.y() <= 40.65f + 1e-4f;
            ok1027 = ok1027 && std::abs(s1027.x() - 0.3f) < 1e-5f
                     && std::abs(s1027.y() - 0.3f) < 1e-5f && std::abs(s1027.z() - 0.3f) < 1e-5f;
            const QVector3D eu1027 = q1027.toEulerAngles();
            ok1027 = ok1027 && std::abs(std::remainder(eu1027.x(), 360.0f)) < 1e-3f
                     && std::abs(std::remainder(eu1027.z(), 360.0f)) < 1e-3f;
        }
        items1027.setCountAt(0, 0);    // ③ 拾走槽 0（t64 余数 0 = 销毁）→ 2 泥土
        const bool okPick = fDirt1027.probeInstanceCount() == 2;
        items1027.removeAt(1);         // 活体槽 1 移除 → 1 泥土
        const bool okRemove = fDirt1027.probeInstanceCount() == 1;
        items1027.clearAll();          // 切世界清场 → 全桶空
        const bool okCleared = fDirt1027.probeInstanceCount() == 0 && fCobble1027.probeInstanceCount() == 0;
        items1027.spawnItem(10, 40, 10, BR::Dirt, 1);
        items1027.spawnItem(10, 40, 10, BR::Dirt, 1);  // ④ 同格 → 就近合并（不加实例）
        items1027.spawnItem(11, 40, 10, BR::Dirt, 1);  // 邻格（距 1.0 ≤ kMergeRadius 2.0）→ 合并
        int mergedCount1027 = -1;
        for (int i = 0; i < items1027.count(); ++i)    // 槽复用 LIFO：活体槽下标不定 → 扫活体读 count
            if (items1027.aliveAt(i)) mergedCount1027 = items1027.countAt(i);
        const bool okMerge = fDirt1027.probeInstanceCount() == 1
                             && items1027.liveCount() == 1 && mergedCount1027 == 3;
        ok1027 = ok1027 && okPick && okRemove && okCleared && okMerge;
        if (!ok1027)
            qInfo().noquote() << "  [t1027 diag] empty" << okEmpty1027 << "cntDirt" << cntDirt
                              << "cntCobble" << cntCobble << "cntTorch" << cntTorch
                              << "pick" << okPick << "remove" << okRemove
                              << "cleared" << okCleared << "merge" << okMerge;
        if (!ok1027) ++totalFail;
        qInfo().noquote() << (ok1027 ? "PASS" : "FAIL")
                          << "| t1027 drop-item instancing batch 1: BlockDropInstancing feeder buckets "
                             "the plain-cube drop family per itemId (only same-id live slots enter a "
                             "bucket: dirt=3/cobble=2 while the torch item and a material item never "
                             "enter any plain-cube bucket), entries carry exact XZ slot positions + "
                             "analytic bob band on Y only + 0.3 uniform scale + pure-Y rotation, "
                             "pickup setCountAt(0) / removeAt / clearAll shrink the table in lockstep, "
                             "and same-id nearby spawns merge by count WITHOUT adding instances "
                             "(1 slot count=3 stays 1 entry) - the largest drop family now renders "
                             "as one Model/one draw per active bucket id replacing per-entity inline "
                             "geometry/material instances (t1007 governance path a), while pickup/"
                             "despawn/merge/physics data chains stay untouched (feeder is read-only)";
    }

    // ── P-t1027b 族谓词单一权威行为级 + QML 接线源码钉（t1027 首批）──
    //   QML delegate 排除侧（Main.qml plain-cube visible 链 hasBucket）与 C++ feeder 收纳侧
    //   （BlockDropInstancing 过滤 isPlainCubeDrop）必须逐位同判，否则双渲（共面 z-fight）或丢渲。
    //   谓词已自 Main.qml isItem3DFamily 字面量表收编（t880 建 / t925 扩 / t965 订正，25 id 原样）→
    //   C++ 行为级全枚举 + QML 薄委托 / 桶池接线源码钉（QML 渲染分支 headless 不可行为级断言，
    //   review27-4 源码钉先例；阴性轮敏感：摘 hasBucket 排除 / 摘 feeder 过滤各自翻红）。
    {
        bool okPred = true;
        // 整立方族收录抽点（常规挖掘产出面 + 16 色羊毛段 27 / FirstWoolVariant..LastWoolVariant）
        okPred = okPred && ItemEntityManager::isPlainCubeDrop(int(BR::Grass))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Dirt))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Stone))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Cobble))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Planks))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Sand))
                 && ItemEntityManager::isPlainCubeDrop(int(BR::Wool));
        for (int id = int(BR::FirstWoolVariant); id <= int(BR::LastWoolVariant); ++id)
            okPred = okPred && ItemEntityManager::isPlainCubeDrop(id);
        // 3D 形状家族 25 id：isItem3DFamily 全 true 且永不进整立方族（与 Main.qml 旧表逐 id 等值）
        static const int kFam1027[] = { 13, 20, 136, 15, 87, 58, 109, 44, 24, 94,
                                        43, 25, 17, 60, 88, 19, 135, 89, 115, 48,
                                        102, 129, 112, 113, 114 };
        for (int id : kFam1027)
            okPred = okPred && ItemEntityManager::isItem3DFamily(id)
                     && !ItemEntityManager::isPlainCubeDrop(id);
        // 非 3D 方块不误标 3D；木楼梯 16（t880 排除清单 = partial 非 3D）/ air / 越界 / 工具 / 材料段
        //  全不进整立方族（工具·材料段经 Count 上界排除，itementitymanager.h static_assert 钉前提）
        okPred = okPred && !ItemEntityManager::isItem3DFamily(int(BR::Dirt))
                 && !ItemEntityManager::isItem3DFamily(int(BR::Stone))
                 && !ItemEntityManager::isItem3DFamily(int(BR::Cobble))
                 && !ItemEntityManager::isItem3DFamily(int(BR::Wool))
                 && !ItemEntityManager::isItem3DFamily(0x200) && !ItemEntityManager::isItem3DFamily(-3)
                 && !ItemEntityManager::isPlainCubeDrop(16) && !ItemEntityManager::isPlainCubeDrop(0)
                 && !ItemEntityManager::isPlainCubeDrop(-1) && !ItemEntityManager::isPlainCubeDrop(0x100)
                 && !ItemEntityManager::isPlainCubeDrop(0x200);
        const QString exeDir1027 = QCoreApplication::applicationDirPath();
        const QString root1027 = QDir(exeDir1027 + QStringLiteral("/..")).absolutePath();
        QFile mf1027(root1027 + QStringLiteral("/src/ui/Main.qml"));
        const QString qml1027 = mf1027.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf1027.readAll()) : QString();
        // delegate 排除钉：plain-cube visible 链在 review27-4 家族互斥之后追加 hasBucket 桶排除
        const int v0 = qml1027.indexOf(QStringLiteral("visible: entRoot.entId !== 13 && !hotbarVM.isPartialBlock"));
        const int v1 = qml1027.indexOf(QStringLiteral("geometry: BlockCube { blockId: entRoot.entId }"), v0);
        const bool okDeleg = v0 >= 0 && v1 > v0
                             && qml1027.mid(v0, v1 - v0).contains(QStringLiteral("&& !blockDropInstHost.hasBucket(entRoot.entId)"));
        // 家族谓词薄委托钉：Main.qml isItem3DFamily 函数体只余 C++ 委托（字面量表收编后不再内联）
        const int if0 = qml1027.indexOf(QStringLiteral("function isItem3DFamily(id)"));
        const int if1 = qml1027.indexOf(QLatin1Char('}'), if0);
        const bool okQmlDeleg = if0 >= 0 && if1 > if0
                                && qml1027.mid(if0, if1 - if0).contains(QStringLiteral("return itemEntities.isItem3DFamily(id)"))
                                && !qml1027.mid(if0, if1 - if0).contains(QStringLiteral("id === "));
        // 接线 / 谓词声明 / feeder 过滤源钉（R19.21 批次 review 收口：裸 contains 迁 pinSet 剥注释——
        //   「注释掉该行」式 QML 回归在裸 contains 下不翻红（review0906 #19 comment-masked pin 同族），
        //   迁移后即红；okDeleg / okQmlDeleg 是 indexOf+mid 范围序断言 pinSet 表达不了故保留为结构面，
        //   其关键子句由本组 pinSet 针（hasbucket-exclude / thin-delegate）补注释免疫）
        QStringList miss1027 = pinSet(
            QDir(exeDir1027 + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/ui/Main.qml")), {
            {"qml-blockdrop-host-id", "id: blockDropInstHost"},
            {"qml-blockdrop-host-type", "BlockDropInstancing {"},
            {"qml-blockdrop-host-manager", "manager: itemEntities"},
            {"qml-blockdrop-host-familyid", "familyId: blockDropInstHost.buckets[index]"},
            {"qml-blockdrop-reassign-fn", "function reassignDropBuckets"},
            {"qml-blockdrop-reassign-call", "blockDropInstHost.reassignDropBuckets()"},
            {"qml-blockdrop-hasbucket-exclude", "&& !blockDropInstHost.hasBucket(entRoot.entId)"},
            {"qml-item3d-thin-delegate", "return itemEntities.isItem3DFamily(id)"},
        });
        miss1027 << pinSet(
            QDir(exeDir1027 + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/Game/itementitymanager.h")), {
            {"hdr-item3d-invokable", "Q_INVOKABLE static bool isItem3DFamily(int itemId);"},
            {"hdr-plaincube-invokable", "Q_INVOKABLE static bool isPlainCubeDrop(int itemId);"},
            {"hdr-count-upper-bound", "int(BlockRegistry::Count) <= 0x100"},
        });
        miss1027 << pinSet(
            QDir(exeDir1027 + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/Game/blockdropinstancing.cpp")), {
            {"feeder-plaincube-filter", "ItemEntityManager::isPlainCubeDrop(itemId)"},
            {"feeder-familyid-neq", "itemId != m_familyId"},
        });
        const bool okPins1027 = miss1027.isEmpty();
        const bool okB1027 = okPred && okDeleg && okQmlDeleg && okPins1027;
        if (!okB1027)
            qInfo().noquote() << "  [t1027 diag] pred" << okPred << "deleg" << okDeleg
                              << "qmlDeleg" << okQmlDeleg << "pin miss:" << miss1027.join(QLatin1Char(','));
        if (!okB1027) ++totalFail;
        qInfo().noquote() << (okB1027 ? "PASS" : "FAIL")
                          << "| t1027 family predicate single authority: ItemEntityManager::isItem3DFamily "
                             "(24-id table moved verbatim from Main.qml) and isPlainCubeDrop are "
                             "behaviorally pinned in C++ (plain mining drops incl. the 16 wool colors "
                             "in, every 3D-family id plus stairs-16/air/out-of-range/tool/material "
                             "segments out) so the QML delegate exclusion side and the C++ feeder "
                             "inclusion side judge every id identically; source pins lock the QML "
                             "wiring (hasBucket exclusion appended to the plain-cube visible chain, "
                             "BlockDropInstancing buckets with manager+familyId, reassignDropBuckets "
                             "signal handler per the AOT binding lesson, isItem3DFamily thin "
                             "delegation) and the feeder filter as the negative-round lesion sites";
    }

    // ── P-t1032a 掉落物 instancing 批 2 探针：3D 形状族 per-id 桶（R19.22；批 1 P-t1027a 同 rig）──
    //   isItem3DFamily 25 id（火把/活板门/台阶/雪层/草丛/附魔台/枯灌木/小麦/栅栏/门/蘑菇/蛛网/红石
    //   火把/拉杆/按钮；木楼梯 16 不在族——t880 billboard 语义）经 BlockDropInstancing shapeFamily
    //   模式收进形状桶（Main.qml blockShapeInstHost，批 1 blockDropInstHost 同构）。钉契约：
    //   ① 25 id 族逐 id 查询——t1038（Review_2026-09-11 #1）起 94 **整体不桶化**（isItem3DFamily(94)
    //   仍真但形状模式 feeder 恒出空表；其余 24 id 条数 = 该 id 活体数）；② 跨族互斥——楼梯 16（partial 非
    //   3D）/ 整立方（泥土）/ 工具 0x100 / 材料 0x200 永不进形状桶，火把也永不进整立方桶（批 1 侧）；
    //   ③ 实例表内容与批 1 同参——XZ 精确 = 实体世界位、Y ∈ bob 解析带（0↔0.15）、scale 0.3 均匀、
    //   纯 Y 轴旋转。
    //   阴性轮敏感：摘 feeder 形状过滤（false && 前缀，t1030/t1031 先例）→ 本腿楼梯/异族拒绝面恰红；
    //   摘 94 不桶化 skip → 本腿 94 腿恰红（期望 0 实得 1）。
    {
        ItemEntityManager items1032a;
        BlockDropInstancing fTorch1032a, fDoor1032a, fStairs1032a, fPlain1032a;
        fTorch1032a.setShapeFamily(true);
        fDoor1032a.setShapeFamily(true);
        fStairs1032a.setShapeFamily(true); // 楼梯 16 非 3D 族：任何形状桶不收纳（QML 桶池亦不指派）
        fTorch1032a.setManager(&items1032a);  fTorch1032a.setFamilyId(13); // 火把
        fDoor1032a.setManager(&items1032a);   fDoor1032a.setFamilyId(19); // 木门
        fStairs1032a.setManager(&items1032a);
        fStairs1032a.setFamilyId(int(BR::WoodStairs)); // 16：桶 id 即便被（误）指派，谓词侧恒拒
        fPlain1032a.setManager(&items1032a); // 批 1 模式（shapeFamily=false）+ 火把 id：跨族恒空
        fPlain1032a.setFamilyId(13);
        // 格距 ≥3（> kMergeRadius=2，防就近合并塌缩活体数——P-t1027a 同 rig 纪律）
        items1032a.spawnItem(10, 40, 10, 13, 1);                  // 火把（3D 族）
        items1032a.spawnItem(14, 40, 10, 19, 1);                  // 木门（3D 族）
        items1032a.spawnItem(18, 40, 10, int(BR::WoodStairs), 1); // 楼梯（partial 非 3D → billboard）
        items1032a.spawnItem(22, 40, 10, int(BR::Dirt), 1);       // 整立方（批 1 桶）
        items1032a.spawnItem(26, 40, 10, 0x100, 1);               // 工具段
        items1032a.spawnItem(30, 40, 10, 0x200, 1);               // 材料段
        const int cntTorch1032a = fTorch1032a.probeInstanceCount();
        const int cntDoor1032a = fDoor1032a.probeInstanceCount();
        const int cntStairs1032a = fStairs1032a.probeInstanceCount();
        const int cntPlainTorch1032a = fPlain1032a.probeInstanceCount();
        bool ok1032a = cntTorch1032a == 1 && cntDoor1032a == 1
                       && cntStairs1032a == 0 && cntPlainTorch1032a == 0;
        QVector3D p1032a, s1032a; QQuaternion q1032a;
        ok1032a = ok1032a && fTorch1032a.probeInstanceAt(0, &p1032a, &s1032a, &q1032a);
        ok1032a = ok1032a && std::abs(p1032a.x() - 10.5f) < 1e-4f && std::abs(p1032a.z() - 10.5f) < 1e-4f;
        ok1032a = ok1032a && p1032a.y() >= 40.5f - 1e-4f && p1032a.y() <= 40.65f + 1e-4f;
        ok1032a = ok1032a && std::abs(s1032a.x() - 0.3f) < 1e-5f
                  && std::abs(s1032a.y() - 0.3f) < 1e-5f && std::abs(s1032a.z() - 0.3f) < 1e-5f;
        const QVector3D eu1032a = q1032a.toEulerAngles();
        ok1032a = ok1032a && std::abs(std::remainder(eu1032a.x(), 360.0f)) < 1e-3f
                  && std::abs(std::remainder(eu1032a.z(), 360.0f)) < 1e-3f;
        // ① 25 id 族逐 id 查询（顺带压 setFamilyId 换桶沿）：t1038 起 94 整体不桶化——期望条数改 0
        //    （isItem3DFamily(94) 仍真但形状模式 feeder 恒出空表：QML 桶池计数循环 skip 94 + C++ 收纳
        //    侧同参排除）；其余 24 id 照旧 条数 = 该 id 活体数（ItemEntityManager::isItem3DFamily
        //    行为级已在 P-t1027b 钉）。
        static const int kFam1032[] = { 13, 20, 136, 15, 87, 58, 109, 44, 24, 94,
                                        43, 25, 17, 60, 88, 19, 135, 89, 115, 48,
                                        102, 129, 112, 113, 114 };
        BlockDropInstancing fLoop1032a;
        fLoop1032a.setShapeFamily(true);
        fLoop1032a.setManager(&items1032a);
        int famMiss1032a = -1;
        for (int k = 0; k < 25 && famMiss1032a < 0; ++k) {
            items1032a.spawnItem(34 + 3 * k, 40, 10, kFam1032[k], 1); // ≥3 格距防合并
            fLoop1032a.setFamilyId(kFam1032[k]);
            int liveOfId1032a = 0;
            for (int i = 0; i < items1032a.count(); ++i)
                if (items1032a.aliveAt(i) && items1032a.itemIdAt(i) == kFam1032[k]) ++liveOfId1032a;
            // t1038：94 期望 0（不桶化）——阴性轮摘 skip 即此处恰红（94 活体 1 实得 1 ≠ 0）
            const int expect1032a = (kFam1032[k] == 94) ? 0 : liveOfId1032a;
            if (fLoop1032a.probeInstanceCount() != expect1032a) famMiss1032a = kFam1032[k];
        }
        ok1032a = ok1032a && famMiss1032a < 0;
        // t1038：不桶化 ≠ 出族——isItem3DFamily(94) 仍真（delegate 渲染链首闸保持，台+书恒走同链）
        ok1032a = ok1032a && ItemEntityManager::isItem3DFamily(94);
        // 异族不混入（同 id 才收）：换回火把 id → 恰 2（初始 + 循环）；泥土/楼梯/工具/材料不计数。
        fLoop1032a.setFamilyId(13);
        const int torchTotal1032a = fLoop1032a.probeInstanceCount();
        ok1032a = ok1032a && torchTotal1032a == 2;
        if (!ok1032a)
            qInfo().noquote() << "  [t1032a diag] torch" << cntTorch1032a << "door" << cntDoor1032a
                              << "stairs" << cntStairs1032a << "plainTorch" << cntPlainTorch1032a
                              << "famMiss" << famMiss1032a << "torchTotal" << torchTotal1032a;
        if (!ok1032a) ++totalFail;
        qInfo().noquote() << (ok1032a ? "PASS" : "FAIL")
                          << "| t1032a drop-item instancing batch 2: the 3D shape family "
                             "(isItem3DFamily 25 ids, stairs-16 excluded per the t880 billboard "
                             "exception) feeds per-id shape buckets through the BlockDropInstancing "
                             "shapeFamily mode: every family id is probed per-id with the entry count "
                             "equal to its live count (torch=1/door=1 then each of the 25 ids), with "
                             "t1038 excluding the enchanting table 94 from shape buckets entirely "
                             "(isItem3DFamily(94) stays true - the delegate chain gate holds - while "
                             "the shape feeder serves an empty table for it, keeping the floating "
                             "book and the table on one same-chain same-phase delegate render), "
                             "stairs-16/dirt/tools-0x100/materials-0x200 never enter a "
                             "shape bucket and a torch never enters a plain-cube bucket (cross-family "
                             "mutex on both modes), and entries carry exact XZ slot positions + the "
                             "analytic bob band on Y + 0.3 uniform scale + pure-Y rotation - the "
                             "second-largest drop family now renders as one ItemShapeGeometry/one "
                             "draw per active bucket id (geometry shared by all instances of the "
                             "same id) while the bucket-full overflow path keeps the delegate "
                             "fallback rendering";
    }

    // ── P-t1038a 附魔台 94 整体不桶化行为腿（Review_2026-09-11 #1 定夺修；书-台相位失锁病灶收口）──
    //   病灶（review 实读）：t1032 把 94 上移 dropBookNode 后若 94 入形状桶，台体走 feeder 解析相位
    //   （m_clock.elapsed()/1000 + slot×0.37），小书仍走 entRoot QML 动画相位（NumberAnimation 相位
    //   = delegate 创建时刻）→ 两套时钟恒定旋转偏移 0-360° + bob 异相振幅最大 0.15 格（反相时书嵌
    //   入台体）。修法（用户推荐）：94 整体排除出形状桶——台+书恒走 delegate 同链同相。本腿钉
    //   headless 可达面：
    //   ① isItem3DFamily(94) 仍真（delegate 渲染链首闸保持——不桶化 ≠ 出族）；
    //   ② 形状模式 feeder 对 94 恒出空表（94 活体在场 →「形状桶无 94 桶」的 feeder 查询面投影 =
    //      C++ 收纳侧 getInstanceBuffer 排除）；同场对照：其余 3D 族 id（火把 13）照常入桶走钟；
    //   ③ 空转门同参：94 桶即使（误）指派也不走钟（hasLiveMember 与 getInstanceBuffer 同参排除
    //      ——无僵尸钟，review0910 #3 纪律）；
    //   ④ QML 编排面（reassignShapeBuckets / hasShapeBucket 纯 JS）headless 不可行为级断言
    //      （review0910 #2 登记盲区）→ 源码钉（P-t1032c ctor 源钉同纪律）：Main.qml
    //      reassignShapeBuckets 切片含 `if (id === 94) continue` skip + Review_2026-09-11 #1 登记
    //      注释；hasShapeBucket 切片零 94 字面（「无第二处特判」结构不变量钉）。
    //   阴性轮敏感：摘 C++ 收纳侧 skip（false && 前缀）→ ②③ 恰红；摘 Main.qml skip → ④ 源钉恰红。
    {
        ItemEntityManager items1038;
        items1038.spawnItem(10, 40, 10, 94, 1);   // 附魔台活体在场（病灶触发条件）
        items1038.spawnItem(14, 40, 10, 13, 1);   // 对照：火把（其余 24 id 桶化不受影响）
        const bool fam94True1038 = ItemEntityManager::isItem3DFamily(94); // ①
        BlockDropInstancing f941038;
        f941038.setShapeFamily(true);
        f941038.setManager(&items1038);
        f941038.setFamilyId(94); // 误指派面：即便 QML 桶池事故指派 94，feeder 也恒空（第二道防线）
        const bool no94Table1038 = f941038.probeInstanceCount() == 0;     // ②
        const bool no94Clock1038 = !f941038.probeTickerActive();          // ③ 同参空转门
        BlockDropInstancing fTorch1038;
        fTorch1038.setShapeFamily(true);
        fTorch1038.setManager(&items1038);
        fTorch1038.setFamilyId(13);
        const bool torchNormal1038 = fTorch1038.probeInstanceCount() == 1
                                     && fTorch1038.probeTickerActive();   // ② 对照
        // ④ 源码钉：QML 编排面盲区的源钉覆盖（skip 在位 + 登记注释在位 + 无第二处特判）
        const QString exeDir1038 = QCoreApplication::applicationDirPath();
        QFile qml1038(QDir(exeDir1038 + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src/ui/Main.qml")));
        const QString qmlSrc1038 = qml1038.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(qml1038.readAll()) : QString();
        const int rsb01038 = qmlSrc1038.indexOf(QStringLiteral("function reassignShapeBuckets"));
        const int rsb11038 = qmlSrc1038.indexOf(
            QStringLiteral("Component.onCompleted: reassignShapeBuckets"), rsb01038);
        const QString rsbSeg1038 = (rsb01038 >= 0 && rsb11038 > rsb01038)
            ? qmlSrc1038.mid(rsb01038, rsb11038 - rsb01038) : QString();
        const int hsb01038 = qmlSrc1038.indexOf(QStringLiteral("function hasShapeBucket"));
        const int hsb11038 = qmlSrc1038.indexOf(QStringLiteral("function reassignShapeBuckets"),
                                                hsb01038);
        const QString hsbSeg1038 = (hsb01038 >= 0 && hsb11038 > hsb01038)
            ? qmlSrc1038.mid(hsb01038, hsb11038 - hsb01038) : QString();
        const bool qmlSkipPinned1038 = !rsbSeg1038.isEmpty()
            && rsbSeg1038.contains(QStringLiteral("if (id === 94) continue")) // 摘 skip 即红
            && rsbSeg1038.contains(QStringLiteral("Review_2026-09-11 #1"));   // 登记注释在位
        const bool noSecondGate1038 = !hsbSeg1038.isEmpty()
            && !hsbSeg1038.contains(QStringLiteral("94"));                    // 无第二处特判
        const bool ok1038 = fam94True1038 && no94Table1038 && no94Clock1038
                            && torchNormal1038 && qmlSkipPinned1038 && noSecondGate1038;
        if (!ok1038)
            qInfo().noquote() << "  [t1038 diag] fam94" << fam94True1038 << "noTable"
                              << no94Table1038 << "noClock" << no94Clock1038 << "torch"
                              << torchNormal1038 << "qmlPin" << qmlSkipPinned1038
                              << "noSecond" << noSecondGate1038
                              << "segLen" << rsbSeg1038.length();
        if (!ok1038) ++totalFail;
        qInfo().noquote() << (ok1038 ? "PASS" : "FAIL")
                          << "| t1038a enchanting-table 94 excluded from shape buckets entirely "
                             "(Review_2026-09-11 #1, book-table phase-lock fix): isItem3DFamily(94) "
                             "stays true so the delegate render chain gate holds (no bucketing does "
                             "not mean eviction from the 3D family), the shape-mode feeder serves an "
                             "empty instance table for id 94 even with a live 94 item in the scene "
                             "(the feeder-query projection of the bucket pool never assigning 94 - "
                             "the C++ collection-side defense line) and its idle gate stays stopped "
                             "(hasLiveMember same-predicate exclusion, no zombie clock), a torch in "
                             "the same scene still buckets and clocks normally (the other 24 family "
                             "ids untouched), and the QML orchestration surface (reassignShapeBuckets "
                             "/ hasShapeBucket, a registered headless blind spot) is covered by "
                             "source pins: the counting loop carries the id-94 skip with the "
                             "Review_2026-09-11 #1 registration comment while hasShapeBucket holds "
                             "no second 94 special case - the floating book and the table now always "
                             "render on one delegate chain sharing the entRoot animation clock "
                             "(constant rotation offset and out-of-phase bob eliminated)";
    }

    // ── P-t1032b 桶满降级路径 headless 可达面（review0910 #2 采纳；QML 编排面盲区如实 scoped）──
    //   reassignDropBuckets / hasBucket / reassignShapeBuckets / hasShapeBucket 是纯 QML JS 编排
    //   （信号 handler 改表类），headless 探针不可行为级断言（review0910 #2 登记盲区——维持源码钉
    //   P-t1032d + 实机确认项 ≥10 种 id 定向冒烟，勿虚报）。本腿只钉 C++ feeder 对「未指派桶
    //   （familyId<=0 = 桶满溢出 / 桶释放后无桶 id 的 QML 投影）」的空表响应，与「桶释放→重指派」
    //   的实例表恢复：桶满 8 时第 9 种 id 恰无桶 → feeder familyId 保持 0 → 恒空表（delegate 旧
    //   路径渲染不双渲的 C++ 侧保证）；桶释放 id 重获指派 → 实例表恢复 = 活体数。
    {
        ItemEntityManager items1032b;
        BlockDropInstancing f9th1032b;
        f9th1032b.setManager(&items1032b); // familyId 保持 0 = 未入桶 id 的 feeder 态
        // 9 种整立方 id 同屏（羊毛基色 + 变体段 8 色；桶池 8 → 第 9 种 QML 侧无桶）
        const int ids1032b[9] = { int(BR::Wool), int(BR::FirstWoolVariant),
                                  int(BR::FirstWoolVariant) + 1, int(BR::FirstWoolVariant) + 2,
                                  int(BR::FirstWoolVariant) + 3, int(BR::FirstWoolVariant) + 4,
                                  int(BR::FirstWoolVariant) + 5, int(BR::FirstWoolVariant) + 6,
                                  int(BR::FirstWoolVariant) + 7 };
        bool distinct1032b = true; // 9 id 确互异（防注册表漂移让「9 种」名存实亡）
        for (int a = 0; a < 9; ++a)
            for (int b = a + 1; b < 9; ++b)
                if (ids1032b[a] == ids1032b[b]) distinct1032b = false;
        for (int k = 0; k < 9; ++k)
            items1032b.spawnItem(10 + 3 * k, 40, 10, ids1032b[k], 1); // ≥3 格距防合并
        const bool okOverflow1032b = distinct1032b && f9th1032b.probeInstanceCount() == 0;
        f9th1032b.setFamilyId(ids1032b[8]);   // 桶释放后该 id 重获指派（QML 桶空出 → 入桶）
        const bool okRestore1032b = f9th1032b.probeInstanceCount() == 1;
        f9th1032b.setFamilyId(0);             // 再释放 → 空表（丢桶瞬时不双渲）
        const bool okRelease1032b = f9th1032b.probeInstanceCount() == 0;
        // 批 2 形状桶同语义：familyId<=0（负值哨兵同收）→ 空表；指派 → 恢复。
        items1032b.spawnItem(50, 40, 10, 13, 1); // 火把
        items1032b.spawnItem(54, 40, 10, 19, 1); // 木门
        BlockDropInstancing fShape1032b;
        fShape1032b.setShapeFamily(true);
        fShape1032b.setManager(&items1032b);
        const bool okShapeUnassigned1032b = fShape1032b.probeInstanceCount() == 0;
        fShape1032b.setFamilyId(19);
        const bool okShapeAssigned1032b = fShape1032b.probeInstanceCount() == 1;
        fShape1032b.setFamilyId(-1);
        const bool okShapeReleased1032b = fShape1032b.probeInstanceCount() == 0;
        const bool ok1032b = okOverflow1032b && okRestore1032b && okRelease1032b
                             && okShapeUnassigned1032b && okShapeAssigned1032b
                             && okShapeReleased1032b;
        if (!ok1032b)
            qInfo().noquote() << "  [t1032b diag] overflow" << okOverflow1032b << "restore"
                              << okRestore1032b << "release" << okRelease1032b << "shapeUn"
                              << okShapeUnassigned1032b << "shapeAs" << okShapeAssigned1032b
                              << "shapeRe" << okShapeReleased1032b;
        if (!ok1032b) ++totalFail;
        qInfo().noquote() << (ok1032b ? "PASS" : "FAIL")
                          << "| t1032b bucket-full degradation headless-reachable faces (review0910 "
                             "#2 adoption, QML orchestration blind spot honestly scoped): with 9 "
                             "distinct plain-cube ids alive the unassigned feeder (familyId=0, the "
                             "C++ projection of the 9th id having no bucket) serves an empty table "
                             "and never double-renders, re-assigning the released bucket id "
                             "restores the instance table to the live count, releasing it again "
                             "(familyId=0) empties it, and the batch-2 shape feeder obeys the same "
                             "contract including the negative familyId sentinel - the QML "
                             "reassign/hasBucket orchestration itself stays a registered blind "
                             "spot covered by source pins plus an on-device smoke item";
    }

    // ── P-t1032c 8×16ms 定时器空转门行为腿（review0910 #3 采纳；批 1/批 2 两池同款门）──
    //   空转（familyId<=0 或桶内活体 0）→ 钟停：「tick 不再 markDirty」的 headless 可观测面 =
    //   probeTickerActive()==false 且事件泵后仍 false（钟停 = 零周期 markDirty 源；markDirty 本体
    //   是渲染 sync 侧态，headless 不可直观测——观测面如实登记为计时器状态机）。活跃沿
    //   （setManager / setFamilyId / setShapeFamily / manager entitiesChanged 有桶内活体）start +
    //   markDirty 兜底（markDirty 兜底为源钉面：refreshTicker 调用点全在 set* 沿 + entitiesChanged
    //   沿）。形状模式门为谓词感知：桶 id 非本族活体（门 19 在位而火把桶空）不算活跃。
    //   阴性轮敏感：摘空转门（构造启钟 + refreshTicker 早退）→ 本腿全部「空转必须 false」面恰红。
    {
        ItemEntityManager items1032c;
        BlockDropInstancing fGate1032c;
        const auto pumpFor1032 = [](int ms) {
            QElapsedTimer t1032c; t1032c.start();
            while (!t1032c.hasExpired(ms))
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const bool okCtorIdle1032c = !fGate1032c.probeTickerActive();    // 构造不启钟（旧恒走钟收口）
        fGate1032c.setManager(&items1032c);
        const bool okWireIdle1032c = !fGate1032c.probeTickerActive();    // 接线后仍空转（无指派）
        fGate1032c.setFamilyId(int(BR::Dirt)); // 批 1 模式（缺省）+ 整立方桶 id（可活跃）
        const bool okAssignedIdle1032c = !fGate1032c.probeTickerActive(); // 指派但桶内活体 0 → 不走钟
        items1032c.spawnItem(10, 40, 10, int(BR::Dirt), 1); // 活跃沿：entitiesChanged → start（+markDirty 兜底）
        const bool okActiveEdge1032c = fGate1032c.probeTickerActive()
                                       && fGate1032c.probeInstanceCount() == 1;
        pumpFor1032(50);
        const bool okActiveRun1032c = fGate1032c.probeTickerActive(); // 活跃期恒走钟（批 1 动画行为不变）
        for (int i = 0; i < items1032c.count(); ++i)
            if (items1032c.aliveAt(i)) items1032c.setCountAt(i, 0);   // 拾走 → 空转沿 stop
        const bool okIdleEdge1032c = !fGate1032c.probeTickerActive();
        pumpFor1032(50);
        const bool okIdleStay1032c = !fGate1032c.probeTickerActive(); // 空转期保持停（泵事件不复活）
        // 形状模式同门 + 谓词感知：dirt 桶切形状模式 → dirt 非 3D 族（谓词侧恒空转）；异族活体
        //   （门 19）在位而火把形状桶（id 不匹配）仍空转；本族活体入桶 → 启钟。
        fGate1032c.setShapeFamily(true);
        const bool okShapeIdle1032c = !fGate1032c.probeTickerActive();
        items1032c.spawnItem(14, 40, 10, 19, 1);
        const bool okShapePredIdle1032c = !fGate1032c.probeTickerActive()
                                          && fGate1032c.probeInstanceCount() == 0;
        fGate1032c.setFamilyId(13);               // 火把形状桶（门在位但 id 不匹配 → 恒空转）
        const bool okShapeIdle2_1032c = !fGate1032c.probeTickerActive();
        items1032c.spawnItem(18, 40, 10, 13, 1);  // 本族活体 → 启钟
        const bool okShapeActive1032c = fGate1032c.probeTickerActive()
                                        && fGate1032c.probeInstanceCount() == 1;
        // 源面：构造体不再含无条件 start（摘门 lesion 会在构造体重启钟 → 行为面 + 本结构面同步红）。
        const QString exeDir1032c = QCoreApplication::applicationDirPath();
        QFile bdi1032c(QDir(exeDir1032c + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src/Game/blockdropinstancing.cpp")));
        const QString bdiSrc1032c = bdi1032c.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(bdi1032c.readAll()) : QString();
        const int ctor0 = bdiSrc1032c.indexOf(QStringLiteral("BlockDropInstancing::BlockDropInstancing"));
        const int ctorEnd = bdiSrc1032c.indexOf(QStringLiteral("\n}"), ctor0);
        const bool okCtorSrc1032c = ctor0 >= 0 && ctorEnd > ctor0
            && !bdiSrc1032c.mid(ctor0, ctorEnd - ctor0).contains(QStringLiteral("m_ticker.start()"));
        const bool ok1032c = okCtorIdle1032c && okWireIdle1032c && okAssignedIdle1032c
                             && okActiveEdge1032c && okActiveRun1032c && okIdleEdge1032c
                             && okIdleStay1032c && okShapeIdle1032c && okShapePredIdle1032c
                             && okShapeIdle2_1032c && okShapeActive1032c && okCtorSrc1032c;
        if (!ok1032c)
            qInfo().noquote() << "  [t1032c diag] ctor" << okCtorIdle1032c << "wire" << okWireIdle1032c
                              << "assigned" << okAssignedIdle1032c << "activeEdge" << okActiveEdge1032c
                              << "activeRun" << okActiveRun1032c << "idleEdge" << okIdleEdge1032c
                              << "idleStay" << okIdleStay1032c << "shapeIdle" << okShapeIdle1032c
                              << "shapePred" << okShapePredIdle1032c << "shapeIdle2" << okShapeIdle2_1032c
                              << "shapeActive" << okShapeActive1032c
                              << "ctorSrc" << okCtorSrc1032c;
        if (!ok1032c) ++totalFail;
        qInfo().noquote() << (ok1032c ? "PASS" : "FAIL")
                          << "| t1032c 16ms feeder timer idle gate (review0910 #3 adoption, both "
                             "bucket pools): the animation timer now starts STOPPED - constructing "
                             "and wiring a feeder leaves it idle, assigning a familyId with zero "
                             "live members keeps it idle, the first live member (entitiesChanged "
                             "edge) starts it with a fresh table, the active period keeps it "
                             "running across an event pump (animation behavior identical to the "
                             "old always-on clock), picking the last member stops it and an event "
                             "pump does not revive it, the shape mode applies the same gate with "
                             "predicate awareness (a foreign-family live item keeps the bucket "
                             "idle) and the constructor source no longer contains an unconditional "
                             "start - idle feeders no longer wake ~60x/s or push empty tables to "
                             "render sync every frame (XpOrbInstancing t858 precedent stays "
                             "always-on by scope decision, registered for a follow-up)";
    }

    // ── P-t1032d 批 2 两侧谓词同源钉 + 几何共享等价断言 + 不让位登记钉（批 1 P-t1027b 同纪律）──
    //   同源三面：feeder 收纳侧（shapeFamily 模式过滤 isItem3DFamily）/ QML 重算侧
    //   （reassignShapeBuckets 内 itemEntities.isItem3DFamily 薄委托、无字面量表）/ delegate 排除侧
    //   （shape Model visible 链 hasShapeBucket）。几何共享：headless 无 QML 场景树，「同 id 同
    //   geometry 指针」以「每桶单几何」结构钉（geometry 绑桶 id 而非实体 id + Repeater 桶模型）+
    //   「同 id 重建几何逐位确定」行为级等价断言（同 id 两实例 vertexData 全等、异 id 不等）承载。
    //   review0910 #4：在用桶保持不让位 = 防几何重建抖动刻意取舍**维持现状**（两池 kept 逻辑逐字
    //   同款、无让位分支——kept 恒钉 minCount=2，让位逻辑若 future 加入即翻红）。
    {
        // 几何 per-id 确定性（桶内全实例共享同一几何的「同 id 同内容」等价面）
        ItemShapeGeometry gA1032d, gB1032d, gC1032d;
        gA1032d.setBlockId(13);  // 火把（细立柱特型）
        gB1032d.setBlockId(13);
        gC1032d.setBlockId(94);  // 附魔台（0.75 矮盒特型）
        const QByteArray vdA1032d = gA1032d.vertexData();
        const QByteArray vdB1032d = gB1032d.vertexData();
        const QByteArray vdC1032d = gC1032d.vertexData();
        const bool okGeom1032d = vdA1032d.size() > 0 && vdA1032d == vdB1032d && !(vdA1032d == vdC1032d);
        const QString exeDir1032d = QCoreApplication::applicationDirPath();
        const QString root1032d = QDir(exeDir1032d + QStringLiteral("/..")).absolutePath();
        QStringList miss1032d;
        miss1032d << pinSet(root1032d + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-blockshape-host-id", "id: blockShapeInstHost"},
            {"qml-blockshape-shapefamily", "shapeFamily: true"},
            {"qml-blockshape-reassign-fn", "function reassignShapeBuckets"},
            {"qml-blockshape-reassign-call", "blockShapeInstHost.reassignShapeBuckets()"},
            {"qml-blockshape-hasbucket-exclude", "&& !blockShapeInstHost.hasShapeBucket(entRoot.entId)"},
            {"qml-blockshape-geometry-per-id",
             "geometry: ItemShapeGeometry { blockId: blockShapeInstHost.buckets[index] }"},
            {"qml-blockshape-familyid-binding", "familyId: blockShapeInstHost.buckets[index]"},
            {"qml-dropbucket-kept-noyield", "if (id > 0 && counts[id] > 0) { next[k] = id; kept.push(id) }", 2},
        });
        miss1032d << pinSet(root1032d + QStringLiteral("/src/Game/blockdropinstancing.h"), {
            {"hdr-shapefamily-prop",
             "Q_PROPERTY(bool shapeFamily READ shapeFamily WRITE setShapeFamily NOTIFY shapeFamilyChanged)"},
            {"hdr-ticker-gate-note", "Q_INVOKABLE bool probeTickerActive() const"},
        });
        miss1032d << pinSet(root1032d + QStringLiteral("/src/Game/blockdropinstancing.cpp"), {
            {"feeder-shape-filter", "ItemEntityManager::isItem3DFamily(itemId)"},
            {"feeder-idle-gate-fn", "void BlockDropInstancing::refreshTicker()"},
            {"feeder-idle-gate-start", "m_ticker.start()"},
            {"feeder-idle-gate-stop", "m_ticker.stop()"},
            {"feeder-entities-changed-edge", "connect(m_manager, &ItemEntityManager::entitiesChanged"},
        });
        // 结构面（indexOf+mid 范围序断言，pinSet 表达不了；P-t1027b okDeleg 先例）：
        //   重算侧同源——reassignShapeBuckets 函数体内用薄委托谓词、无字面量 id 表。
        //   t1038 修订：唯一豁免 = 登记「Review_2026-09-11 #1」的 94 skip（if (id === 94) continue
        //   ——书台同相定夺修，注释须紧邻前置）；其余字面 id 比对仍禁（防旁路谓词权威）。
        QFile mf1032d(root1032d + QStringLiteral("/src/ui/Main.qml"));
        const QString qml1032d = mf1032d.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf1032d.readAll()) : QString();
        const int rs0 = qml1032d.indexOf(QStringLiteral("function reassignShapeBuckets"));
        const int rs1 = qml1032d.indexOf(QStringLiteral("blockShapeInstHost.buckets = next"), rs0);
        const QString rsWin1032d = (rs0 >= 0 && rs1 > rs0) ? qml1032d.mid(rs0, rs1 - rs0) : QString();
        const int skip941032d = rsWin1032d.indexOf(QStringLiteral("if (id === 94) continue"));
        const bool okSkipRegistered1032d = skip941032d >= 0
            && rsWin1032d.left(skip941032d).contains(QStringLiteral("Review_2026-09-11 #1"));
        QString rsStripped1032d = rsWin1032d;
        rsStripped1032d.remove(QStringLiteral("if (id === 94) continue"));
        const bool okResignSameSrc1032d = rs0 >= 0 && rs1 > rs0
            && rsWin1032d.contains(QStringLiteral("itemEntities.isItem3DFamily(id)"))
            && okSkipRegistered1032d
            && !rsStripped1032d.contains(QStringLiteral("id === "));
        // 材质逐字同参——桶 Model 材质块（geometry 钉后窗）含旧 ItemShapeGeometry 分支五参数。
        const int gi = qml1032d.indexOf(
            QStringLiteral("geometry: ItemShapeGeometry { blockId: blockShapeInstHost.buckets[index] }"));
        const QString matWin = (gi >= 0) ? qml1032d.mid(gi, 1600) : QString();
        const bool okMatParity1032d = gi >= 0
            && matWin.contains(QStringLiteral("alphaMode: PrincipledMaterial.Mask"))
            && matWin.contains(QStringLiteral("alphaCutoff: 0.5"))
            && matWin.contains(QStringLiteral("opacity: 0.99"))
            && matWin.contains(QStringLiteral("baseColorMap: voxelAtlas"))
            && matWin.contains(QStringLiteral("baseColor: terrainLight(worldClock.skyLight)"));
        const bool okPins1032d = miss1032d.isEmpty();
        const bool ok1032d = okGeom1032d && okResignSameSrc1032d && okMatParity1032d && okPins1032d;
        if (!ok1032d)
            qInfo().noquote() << "  [t1032d diag] geom" << okGeom1032d << "reassign"
                              << okResignSameSrc1032d << "mat" << okMatParity1032d
                              << "pin miss:" << miss1032d.join(QLatin1Char(','));
        if (!ok1032d) ++totalFail;
        qInfo().noquote() << (ok1032d ? "PASS" : "FAIL")
                          << "| t1032d batch-2 same-source predicate pins + geometry sharing "
                             "equivalence + no-yield registration: the shape-family admission "
                             "(feeder isItem3DFamily filter), the QML bucket reassignment (thin "
                             "delegation, no literal id table beyond the single t1038-registered "
                             "id-94 skip carrying its Review_2026-09-11 #1 registration comment) "
                             "and the delegate exclusion "
                             "(hasShapeBucket on the shape Model visible chain) all judge every "
                             "id from the single C++ authority, the per-bucket geometry is pinned "
                             "to the bucket id (not per-entity) with same-id rebuilds proven "
                             "byte-identical and cross-id distinct (the headless equivalent of "
                             "same-id same-geometry-pointer sharing), the bucket Model material "
                             "reproduces the old ItemShapeGeometry branch verbatim (Mask + 0.5 "
                             "cutoff + 0.99 opacity + atlas + skylight tint), both bucket pools "
                             "keep the stable-kept-bucket no-yield assignment (review0910 #4 "
                             "deliberate trade-off, pinned at minCount=2 so any future yield "
                             "logic turns red), and the idle-gate plumbing (refreshTicker "
                             "start/stop + entitiesChanged edge) is comment-immune pinned";
    }

    // ── P-t1039a 掉落物 instancing 批 3 探针：光晕壳族全族合批（R19.23；批 1/2 P-t1027a/t1032a 同 rig）──
    //   旧 delegate entShell 无 visible 条件 = 壳全族横切（每个活体掉落实体恒带一壳）。批 3 压成
    //   全族单 instanced Model 单实例表（GlowShellInstancing，1 draw）。钉契约：
    //   ① 全族收纳——整立方（泥土）/ 3D 形状族（火把 13）/ partial billboard 族（楼梯 16）/
    //      工具段 0x100 / 材料段 0x200 同池（壳不看 itemId，与本体族路由正交）；实例数 = 活体数；
    //   ② 实例表内容逐字对齐旧 entShell——XZ 精确 = 实体世界位、Y ∈ bob 解析带（0↔0.15）、
    //      scale 0.45 均匀、纯 Y 轴旋转；
    //   ③ per-instance color——普通灰 (176,176,176)（默认光 k=1）+ 静态 alpha 0.35；附魔紫
    //      (140,64,230) + 呼吸 alpha ∈ [0.28,0.45]（t696 解析式带；逐字公式由 P-t1039d 源钉承载）。
    //      ⚠ 断言口径：QQuick3DInstancing::calculateTableEntry 落表前经 QSSGUtils::color::sRGBToLinear
    //      （Qt 6.11 src/utils/qssgutils.cpp 多项式 C1·c³+C2·c²+C3·c，alpha 透传），getColor() 原样
    //      回读**线性**值——探针按同一多项式镜像期望线性值（= Quick3D 线性色管线的权威存储值；
    //      渲染输出端转回 sRGB ⇒ 视觉 = 旧 delegate baseColor 同色，材质路径同经 sRGB→linear）；
    //   ④ 天光乘子——k = minLight + (1-minLight)×skyLight（tintBySkyLight floor 公式；setSkyLight
    //      0.5 → 灰 sRGB r 变 qRound(176×0.7)=123；还原 1.0 → 逐位回 176）。
    //   阴性轮敏感：摘壳收纳 alive 过滤（false && 前缀）→ 本腿拾取后实例数面恰红。
    {
        // Quick3D calculateTableEntry 的 sRGB→linear 逐字镜像（QSSGUtils::color::sRGBToLinear，
        // Qt 6.11 qssgutils.cpp：rgb*(rgb*(rgb*C1+C2)+C3)；探针线性回读期望值用）
        const auto srgbToLinear1039a = [](float c) {
            return c * (c * (c * 0.305306011f + 0.682171111f) + 0.012522878f);
        };
        ItemEntityManager items1039a;
        GlowShellInstancing fShell1039a;
        fShell1039a.setManager(&items1039a); // skyLight/minLight 缺省 1.0/0.4 → k=1（headless 确定态）
        // 格距 ≥3（> kMergeRadius=2，防就近合并塌缩活体数——P-t1027a 同 rig 纪律）；附魔工具与
        //   普通工具同 id 异位（距 8）不合并——附魔随实例走（t590）正是紫/灰分色断言的前提。
        items1039a.spawnItem(10, 40, 10, int(BR::Dirt), 1);                    // 灰壳（整立方族）
        items1039a.spawnItem(14, 40, 10, 13, 1);                               // 灰壳（3D 形状族）
        items1039a.spawnItem(18, 40, 10, int(BR::WoodStairs), 1);              // 灰壳（billboard 族）
        items1039a.spawnItem(22, 40, 10, 0x100, 1);                            // 灰壳（工具段）
        items1039a.spawnItem(26, 40, 10, 0x200, 1);                            // 灰壳（材料段）
        items1039a.spawnItem(30, 40, 10, 0x100, 1, QVariantList{7, 0, 0, 0});  // 紫壳（附魔工具）
        const int cntAll1039a = fShell1039a.probeInstanceCount();
        bool ok1039a = cntAll1039a == 6; // ① 全族收纳：5 灰 + 1 紫 = 活体数
        // ② 首条（泥土灰壳）变换内容
        QVector3D p1039a, sc1039a;
        QQuaternion q1039a;
        QColor c1039a;
        ok1039a = ok1039a && fShell1039a.probeInstanceAt(0, &p1039a, &sc1039a, &q1039a, &c1039a);
        ok1039a = ok1039a && std::abs(p1039a.x() - 10.5f) < 1e-4f && std::abs(p1039a.z() - 10.5f) < 1e-4f;
        ok1039a = ok1039a && p1039a.y() >= 40.5f - 1e-4f && p1039a.y() <= 40.65f + 1e-4f;
        ok1039a = ok1039a && std::abs(sc1039a.x() - 0.45f) < 1e-5f
                  && std::abs(sc1039a.y() - 0.45f) < 1e-5f && std::abs(sc1039a.z() - 0.45f) < 1e-5f;
        const QVector3D eu1039a = q1039a.toEulerAngles();
        ok1039a = ok1039a && std::abs(std::remainder(eu1039a.x(), 360.0f)) < 1e-3f
                  && std::abs(std::remainder(eu1039a.z(), 360.0f)) < 1e-3f;
        // ③ 颜色实例表：灰 176 三通道（线性落表）+ 静态 0.35 alpha（alpha 透传不进伽马）
        const float tolC1039a = 2.0f / 255.0f;
        ok1039a = ok1039a && std::abs(c1039a.redF() - srgbToLinear1039a(176.0f / 255.0f)) <= tolC1039a
                  && std::abs(c1039a.greenF() - srgbToLinear1039a(176.0f / 255.0f)) <= tolC1039a
                  && std::abs(c1039a.blueF() - srgbToLinear1039a(176.0f / 255.0f)) <= tolC1039a;
        ok1039a = ok1039a && std::abs(c1039a.alphaF() - 0.35f) <= 1.5f / 255.0f + 1e-3f;
        // ③ 紫实例（附魔工具，槽序第 6 条）：rgb 140/64/230（线性落表）+ 呼吸带 alpha
        QColor cp1039a;
        ok1039a = ok1039a && fShell1039a.probeInstanceAt(5, nullptr, nullptr, nullptr, &cp1039a);
        ok1039a = ok1039a && std::abs(cp1039a.redF() - srgbToLinear1039a(140.0f / 255.0f)) <= tolC1039a
                  && std::abs(cp1039a.greenF() - srgbToLinear1039a(64.0f / 255.0f)) <= tolC1039a
                  && std::abs(cp1039a.blueF() - srgbToLinear1039a(230.0f / 255.0f)) <= tolC1039a;
        ok1039a = ok1039a && cp1039a.alphaF() >= 0.28f - 2.0f / 255.0f
                  && cp1039a.alphaF() <= 0.45f + 2.0f / 255.0f;
        // ④ 天光乘子沿：k = 0.4 + 0.6×0.5 = 0.7 → 灰 sRGB r = qRound(176×0.7) = 123；还原逐位回 176
        fShell1039a.setSkyLight(0.5); // minLight 缺省 0.4 = Main.qml window.minLight 同值
        QColor cd1039a;
        ok1039a = ok1039a && fShell1039a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cd1039a);
        ok1039a = ok1039a && std::abs(cd1039a.redF() - srgbToLinear1039a(123.0f / 255.0f)) <= tolC1039a;
        fShell1039a.setSkyLight(1.0);
        ok1039a = ok1039a && fShell1039a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cd1039a)
                  && std::abs(cd1039a.redF() - srgbToLinear1039a(176.0f / 255.0f)) <= tolC1039a;
        // ① 拾取沿：泥土被拾（setCountAt 0 = releaseSlot）→ 实例表塌到 5（空槽不进表）
        items1039a.setCountAt(0, 0);
        const int cntAfterPick1039a = fShell1039a.probeInstanceCount();
        ok1039a = ok1039a && cntAfterPick1039a == 5;
        if (!ok1039a)
            qInfo().noquote() << "  [t1039a diag] all" << cntAll1039a << "afterPick"
                              << cntAfterPick1039a
                              << "grayLin r/g/b/a" << c1039a.redF() << c1039a.greenF() << c1039a.blueF()
                              << c1039a.alphaF() << "grayLinExp" << srgbToLinear1039a(176.0f / 255.0f)
                              << "purpleLin r/g/b" << cp1039a.redF() << cp1039a.greenF() << cp1039a.blueF()
                              << "purpleExp" << srgbToLinear1039a(140.0f / 255.0f) << ","
                              << srgbToLinear1039a(64.0f / 255.0f) << "," << srgbToLinear1039a(230.0f / 255.0f)
                              << "purpleA" << cp1039a.alphaF()
                              << "dimLin r" << cd1039a.redF() << "dimExp" << srgbToLinear1039a(123.0f / 255.0f);
        if (!ok1039a) ++totalFail;
        qInfo().noquote() << (ok1039a ? "PASS" : "FAIL")
                          << "| t1039a drop glow-shell instancing batch 3: the old entShell delegate "
                             "carried no visible condition so EVERY live drop entity wears a shell "
                             "(family cross-cutting) - the whole shell family now feeds one instanced "
                             "Model (one draw): plain-cube dirt, 3D-family torch, billboard stairs, "
                             "tool-segment and material-segment items all enter the same table with "
                             "the entry count equal to the live count, entries carry exact XZ slot "
                             "positions + the analytic bob band on Y + 0.45 uniform scale + pure-Y "
                             "rotation (verbatim entShell parity), per-instance color holds gray "
                             "176/176/176 with static alpha 0.35 versus enchanted purple 140/64/230 "
                             "with the t696 breathing alpha band 0.28..0.45 (asserted against the "
                             "linear values Quick3D's calculateTableEntry stores via its "
                             "sRGBToLinear pipeline conversion - the renderer's authoritative table "
                             "content, visually identical to the delegate material path), and the "
                             "skylight tint "
                             "follows k = minLight + (1-minLight)*skyLight (dimming to r=123 at "
                             "skyLight 0.5 and restoring bit-exact at 1.0) while picking a drop "
                             "collapses the table - hasTransparency makes the table alpha render on "
                             "the opaque white material (negative-round sensitive: shell alive-filter "
                             "removal)";
    }

    // ── P-t1039b 批 3 两侧谓词同源 + 跨池正交腿（壳不双渲 / 壳不占本体桶 / 桶不占壳）──
    //   同源最强形式：QML delegate 排除侧（entShell visible !hasShellAt）薄委托 feeder 的
    //   Q_INVOKABLE hasShellAt —— 与 getInstanceBuffer 收纳（前 kShellCap 活体槽）互为镜像。
    //   本腿钉 C++ 可达面：① 谓词镜像——≤cap 时 hasShellAt(i)==aliveAt(i) ∀i 且双 feeder 确定性
    //   同表（count 相等）；② 谓词边界——越界 / 空槽 / 无 manager 恒 false（delegate 壳保底）；
    //   ③ 跨池正交——壳池收火把 13 而整立方本体桶（familyId=13）恒空、泥土两池并存（壳 + 本体
    //   是不同视觉件，同 id 壳不双渲 ≠ 壳体互斥）；④ 死槽翻转 + 槽复用回升（t256 slot-reuse 语义）。
    //   阴性轮敏感：摘壳收纳 alive 过滤 → ①镜像 / ④塌缩面恰红。
    {
        ItemEntityManager items1039b;
        GlowShellInstancing fShell1039b, fMirror1039b;
        fShell1039b.setManager(&items1039b);
        fMirror1039b.setManager(&items1039b); // 第二实例 = 确定性同表面（同 manager 同表）
        items1039b.spawnItem(10, 40, 10, int(BR::Dirt), 1);       // 整立方（本体桶也收）
        items1039b.spawnItem(14, 40, 10, 13, 1);                  // 火把（3D 族，本体桶不收）
        items1039b.spawnItem(18, 40, 10, int(BR::Wool), 1);       // 整立方（羊毛）
        items1039b.spawnItem(22, 40, 10, 0x100, 1);               // 工具段（本体桶不收）
        items1039b.spawnItem(26, 40, 10, 0x200, 1);               // 材料段（本体桶不收）
        bool parity1039b = true;
        for (int i = 0; i < items1039b.count(); ++i)
            parity1039b = parity1039b && fShell1039b.hasShellAt(i) == items1039b.aliveAt(i);
        const int cntA1039b = fShell1039b.probeInstanceCount();
        const int cntB1039b = fMirror1039b.probeInstanceCount();
        const bool okMirror1039b = parity1039b && cntA1039b == 5 && cntB1039b == 5; // ①
        const bool okBounds1039b = !fShell1039b.hasShellAt(-1) && !fShell1039b.hasShellAt(items1039b.count()); // ②
        GlowShellInstancing fBare1039b; // 无 manager（QML 未接线退化面）
        const bool okBare1039b = !fBare1039b.hasShellAt(0) && fBare1039b.probeInstanceCount() == 0; // ②
        // ③ 跨池正交：本体桶侧（批 1 BlockDropInstancing）——泥土入整立方桶、火把恒不进；
        //    壳池侧两 id 全收（count 5）。壳排除链挂 entShell 节点、桶排除链挂本体 Model——正交。
        BlockDropInstancing fPlainDirt1039b, fPlainTorch1039b;
        fPlainDirt1039b.setManager(&items1039b);
        fPlainDirt1039b.setFamilyId(int(BR::Dirt));
        fPlainTorch1039b.setManager(&items1039b);
        fPlainTorch1039b.setFamilyId(13);
        const bool okOrtho1039b = fPlainDirt1039b.probeInstanceCount() == 1
                                  && fPlainTorch1039b.probeInstanceCount() == 0
                                  && fShell1039b.probeInstanceCount() == 5;
        // ④ 死槽翻转 + 槽复用：拾走火把（槽 1）→ hasShellAt(1) false、壳表塌 4（本体泥土桶不动）；
        //    远位重投火把 → 复用空槽 → hasShellAt 回 true、壳表回 5。
        items1039b.setCountAt(1, 0);
        const bool okDead1039b = !fShell1039b.hasShellAt(1) && fShell1039b.probeInstanceCount() == 4
                                 && fPlainDirt1039b.probeInstanceCount() == 1;
        items1039b.spawnItem(60, 40, 10, 13, 1); // 距其余 ≥3 格防合并
        const bool okReuse1039b = fShell1039b.hasShellAt(1) && fShell1039b.probeInstanceCount() == 5;
        const bool ok1039b = okMirror1039b && okBounds1039b && okBare1039b
                             && okOrtho1039b && okDead1039b && okReuse1039b;
        if (!ok1039b)
            qInfo().noquote() << "  [t1039b diag] mirror" << okMirror1039b << "bounds" << okBounds1039b
                              << "bare" << okBare1039b << "ortho" << okOrtho1039b << "dead"
                              << okDead1039b << "reuse" << okReuse1039b
                              << "cntA" << cntA1039b << "cntB" << cntB1039b;
        if (!ok1039b) ++totalFail;
        qInfo().noquote() << (ok1039b ? "PASS" : "FAIL")
                          << "| t1039b glow-shell two-sided same-source predicate + cross-pool "
                             "orthogonality: the delegate exclusion side (entShell visible "
                             "!hasShellAt) thin-delegates the feeder's own Q_INVOKABLE so admission "
                             "(getInstanceBuffer takes the first kShellCap live slots) and exclusion "
                             "are mirror images of one C++ function - with the pool under cap "
                             "hasShellAt equals aliveAt for every slot and two feeder instances "
                             "produce identical tables, out-of-range/dead/unmanaged slots answer "
                             "false (delegate shell fallback), the shell pool admits torch-13 while "
                             "the plain-cube body bucket for id 13 stays empty and dirt lives in "
                             "both pools (shell and body are separate visual pieces - one shell per "
                             "entity, never two), and picking a torch flips its hasShellAt false "
                             "with the table collapsing while a far re-spawn reuses the slot and "
                             "restores admission (negative-round sensitive: shell alive-filter "
                             "removal)";
    }

    // ── P-t1039c 批 3 空转门行为腿 + kShellCap 溢出降级腿（t1032c 同纪律；壳族无 familyId →
    //    活跃判定 = 任一活体槽）──
    //   空转三停一启：构造停① / 接线停② / 拾走停③；首发活体启（entitiesChanged 沿）+ 事件泵后
    //   保持。skyLight/minLight 沿不启钟（色调重取无时钟语义）。溢出降级：130 活体（manager 上限
    //   200 内）→ 实例表恰 128（kShellCap）、尾 2 槽 hasShellAt false（delegate 壳保底）；拾走前段
    //   一槽 → 尾槽 128 转入池（降级→恢复连续面）。
    //   阴性轮敏感：摘空转门（构造 start + refreshTicker 早退）→ 本腿全部「空转必须 false」面恰红；
    //   摘收纳容量（上限放宽）→ 溢出面恰红。
    {
        ItemEntityManager items1039c;
        GlowShellInstancing fGate1039c;
        const auto pumpFor1039 = [](int ms) {
            QElapsedTimer t1039c; t1039c.start();
            while (!t1039c.hasExpired(ms))
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        const bool okCtorIdle1039c = !fGate1039c.probeTickerActive();   // 停①：构造不启钟
        fGate1039c.setManager(&items1039c);
        const bool okWireIdle1039c = !fGate1039c.probeTickerActive();   // 停②：接线后仍空转
        fGate1039c.setSkyLight(0.5);
        const bool okTintIdle1039c = !fGate1039c.probeTickerActive();   // 色调沿不启钟
        fGate1039c.setSkyLight(1.0);
        items1039c.spawnItem(10, 40, 10, int(BR::Dirt), 1);             // 启：entitiesChanged 活跃沿
        const bool okActiveEdge1039c = fGate1039c.probeTickerActive()
                                       && fGate1039c.probeInstanceCount() == 1;
        pumpFor1039(50);
        const bool okActiveRun1039c = fGate1039c.probeTickerActive();   // 活跃期恒走钟
        items1039c.setCountAt(0, 0);                                    // 拾走 → 停③：空转沿
        const bool okIdleEdge1039c = !fGate1039c.probeTickerActive();
        pumpFor1039(50);
        const bool okIdleStay1039c = !fGate1039c.probeTickerActive();   // 空转期保持停
        // 溢出降级腿：130 活体泥土（30 列 × 行距 3 格，格距 > kMergeRadius=2 防合并）
        for (int k = 0; k < 130; ++k)
            items1039c.spawnItem(4 + 3 * (k % 30), 40, 4 + 3 * (k / 30), int(BR::Dirt), 1);
        const int cntCap1039c = fGate1039c.probeInstanceCount();
        const bool okCapCount1039c = cntCap1039c == GlowShellInstancing::kShellCap; // 128 封顶
        const bool okCapTail1039c = !fGate1039c.hasShellAt(128) && !fGate1039c.hasShellAt(129)
                                    && fGate1039c.hasShellAt(0) && fGate1039c.hasShellAt(127);
        const bool okCapActive1039c = fGate1039c.probeTickerActive();   // 壳池活跃（动画走钟）
        items1039c.setCountAt(0, 0); // 拾走前段一槽 → 活体 129 → 尾槽 128 转入池（恢复面）
        const bool okCapRestore1039c = fGate1039c.hasShellAt(128) && !fGate1039c.hasShellAt(129)
                                       && !fGate1039c.hasShellAt(0)
                                       && fGate1039c.probeInstanceCount() == GlowShellInstancing::kShellCap;
        // 源面：构造体不含无条件 start（t1032c 同款结构钉——摘门 lesion 在构造体重启钟即翻红）
        const QString exeDir1039c = QCoreApplication::applicationDirPath();
        QFile gsi1039c(QDir(exeDir1039c + QStringLiteral("/..")).absoluteFilePath(
            QStringLiteral("src/Game/glowshellinstancing.cpp")));
        const QString gsiSrc1039c = gsi1039c.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(gsi1039c.readAll()) : QString();
        const int ctor0t1039c = gsiSrc1039c.indexOf(QStringLiteral("GlowShellInstancing::GlowShellInstancing"));
        const int ctorEnd1039c = gsiSrc1039c.indexOf(QStringLiteral("\n}"), ctor0t1039c);
        const bool okCtorSrc1039c = ctor0t1039c >= 0 && ctorEnd1039c > ctor0t1039c
            && !gsiSrc1039c.mid(ctor0t1039c, ctorEnd1039c - ctor0t1039c).contains(QStringLiteral("m_ticker.start()"));
        const bool ok1039c = okCtorIdle1039c && okWireIdle1039c && okTintIdle1039c
                             && okActiveEdge1039c && okActiveRun1039c && okIdleEdge1039c
                             && okIdleStay1039c && okCapCount1039c && okCapTail1039c
                             && okCapActive1039c && okCapRestore1039c && okCtorSrc1039c;
        if (!ok1039c)
            qInfo().noquote() << "  [t1039c diag] ctor" << okCtorIdle1039c << "wire" << okWireIdle1039c
                              << "tint" << okTintIdle1039c << "activeEdge" << okActiveEdge1039c
                              << "activeRun" << okActiveRun1039c << "idleEdge" << okIdleEdge1039c
                              << "idleStay" << okIdleStay1039c << "capCount" << okCapCount1039c
                              << "capTail" << okCapTail1039c << "capActive" << okCapActive1039c
                              << "capRestore" << okCapRestore1039c << "ctorSrc" << okCtorSrc1039c
                              << "cnt" << cntCap1039c;
        if (!ok1039c) ++totalFail;
        qInfo().noquote() << (ok1039c ? "PASS" : "FAIL")
                          << "| t1039c glow-shell idle gate + kShellCap overflow degradation: the "
                             "16ms feeder timer starts STOPPED (constructor stays start-free, "
                             "wiring and a skylight tint edge both leave it idle), the first live "
                             "drop starts it through the entitiesChanged edge with a fresh table, "
                             "the active period survives an event pump, picking the last drop stops "
                             "it and a pump does not revive it (no familyId for the cross-cutting "
                             "shell family - any live slot is activity), stuffing 130 live drops "
                             "caps the instance table at exactly kShellCap=128 with the two tail "
                             "slots answered false (delegate shell fallback, manager cap 200 keeps "
                             "the overflow state reachable) and picking one head slot promotes the "
                             "first overflow slot back into the pool (negative-round sensitive: "
                             "idle-gate removal and admission-cap removal)";
    }

    // ── P-t1039d 批 3 两侧接线源钉 + 动画解析式逐字钉（t1032d 同纪律；QML 编排面盲区的源钉覆盖）──
    //   QML 侧只有薄委托（无 reassign 表类 handler——壳族无桶池），headless 行为面由 P-t1039a/b/c
    //   直调覆盖；本腿钉接线在位（host id / instancing 绑定 / manager / 天光两绑 / 薄委托 / entShell
    //   排除〔t1047 O-1 改 revision 触碰反应式——hasShellAt 无 NOTIFY，非反应裸绑定在 >128 溢出 /
    //   槽翻转态永续双壳/丢壳；review0912 #1〕 / 呼吸 running 门 / 白基色）+ C++ 侧公式逐字（呼吸
    //   0.28+0.17·½·(1−cos) / 静态 0.35 / 紫 140 / 灰 176 / 收纳容量 / slot×0.37 错峰 / scale 0.45 /
    //   hasTransparency / 天光 floor 公式 / 空转门启停 / entitiesChanged 沿）。
    //   t1047 O-1 如实 scoped：溢出反应性行为腿 headless 不可达（QML 绑定重算无 C++ 直调面；壳池
    //   hasShellAt 前 128 语义已由 P-t1039b/c 行为腿覆盖且本单不变）→ 源钉（本行）+ 绑定触碰结构钉
    //   足够，实机确认（review0912 #1 同项）补观感面。
    {
        const QString exeDir1039d = QCoreApplication::applicationDirPath();
        const QString root1039d = QDir(exeDir1039d + QStringLiteral("/..")).absolutePath();
        QStringList miss1039d;
        miss1039d << pinSet(root1039d + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-glowshell-host-id", "id: glowShellInstHost"},
            {"qml-glowshell-instancing-bind", "instancing: GlowShellInstancing {"},
            {"qml-glowshell-manager-bind", "manager: itemEntities", 3},
            {"qml-glowshell-skylight-bind", "skyLight: worldClock.skyLight"},
            {"qml-glowshell-minlight-bind", "minLight: window.minLight"},
            {"qml-glowshell-thin-delegate",
             "function hasShellAt(slot) { return glowShellInst.hasShellAt(slot) }"},
            {"qml-glowshell-entshell-exclude",
             "return _r >= 0 ? !glowShellInstHost.hasShellAt(index) : true"}, // t1047 O-1 revision 触碰绑定
            {"qml-glowshell-breath-gate", "running: entShell.visible && entRoot.entHasEnch"},
            {"qml-glowshell-white-base", "baseColor: Qt.rgba(1.0, 1.0, 1.0, 1.0)", 2},
        });
        miss1039d << pinSet(root1039d + QStringLiteral("/src/Game/glowshellinstancing.h"), {
            {"hdr-glowshell-cap", "static constexpr int kShellCap = 128"},
            {"hdr-glowshell-hasshell", "Q_INVOKABLE bool hasShellAt(int slot) const"},
            {"hdr-glowshell-gate-probe", "Q_INVOKABLE bool probeTickerActive() const"},
        });
        miss1039d << pinSet(root1039d + QStringLiteral("/src/Game/glowshellinstancing.cpp"), {
            {"feeder-glowshell-transparency", "setHasTransparency(true)"},
            {"feeder-glowshell-breath-formula",
             "0.28 + 0.17 * 0.5 * (1.0 - std::cos(M_PI * s3))"},
            {"feeder-glowshell-static-alpha", "0.35f"},
            {"feeder-glowshell-purple", "qRound(140.0 * k)"},
            {"feeder-glowshell-gray", "qRound(176.0 * k)"},
            {"feeder-glowshell-cap-admission", "m_liveSlots.size() < kShellCap"},
            {"feeder-glowshell-stagger", "slot * 0.37"},
            {"feeder-glowshell-scale", "QVector3D(0.45f, 0.45f, 0.45f)"},
            {"feeder-glowshell-lightk", "m_minLight + (1.0 - m_minLight) * m_skyLight"},
            {"feeder-glowshell-idle-start", "m_ticker.start()"},
            {"feeder-glowshell-idle-stop", "m_ticker.stop()"},
            {"feeder-glowshell-entities-edge", "connect(m_manager, &ItemEntityManager::entitiesChanged"},
        });
        const bool ok1039d = miss1039d.isEmpty();
        if (!ok1039d)
            qInfo().noquote() << "  [t1039d diag] pin miss:" << miss1039d.join(QLatin1Char(','));
        if (!ok1039d) ++totalFail;
        qInfo().noquote() << (ok1039d ? "PASS" : "FAIL")
                          << "| t1039d glow-shell wiring source pins + verbatim analytic pins: the "
                             "QML side stays a thin delegation (no bucket reassignment handler - the "
                             "cross-cutting shell family needs no bucket pool) with the host id, the "
                             "GlowShellInstancing binding, the itemEntities manager plus the "
                             "worldClock.skyLight and window.minLight tint bindings, the "
                             "hasShellAt thin delegate, the entShell revision-touching visible "
                             "exclusion (t1047 O-1: the bare non-reactive form never re-evaluated "
                             "across over-cap and slot-flip states - the binding now touches "
                             "itemEntities.revision so the predicate re-queries on every "
                             "entity-set change), the "
                             "breathing running gate and the white material base color all pinned "
                             "comment-immune, while the C++ feeder carries the verbatim calibers - "
                             "hasTransparency(true), the t696 breathing formula "
                             "0.28 + 0.17*0.5*(1-cos(pi*s)) at 800ms per leg, static gray alpha "
                             "0.35, purple 140/64/230 vs gray 176/176/176 through the skylight "
                             "floor formula, kShellCap admission, the slot*0.37 stagger, the 0.45 "
                             "shell scale and the idle-gate plumbing (negative-round sensitive: "
                             "any pinned wiring or formula edit turns this leg red)";
    }

    // ── P-t1041a 掉落物 instancing 批 4 探针：工具 3D 族 per-id 桶 + tier 色 per-instance color ──
    //   isTool3DDrop（五类几何镐/锄/斧/铲/剑 + 弓）经 ToolDropInstancing 收进工具桶（Main.qml
    //   toolDropInstHost，批 1/2 同构）。钉契约：
    //   ① 族分桶互斥——剪刀 0x110（图标族）/ 材料 0x200 / 火把 13（3D 形状族）/ 钓鱼竿 0x111（掉落
    //      delegate 无分支——既有行为如实不入族，t1041 登记）即便被（误）指派桶也恒空表（谓词拒）；
    //   ② 实例数=活体数（木镐 2 → 2 条；拾取塌表）；③ 实例表内容与旧 delegate 逐字对齐——XZ 精确 =
    //      实体世界位、Y ∈ bob 解析带（0↔0.15）、scale 0.45 均匀、纯 Y 轴旋转（工具继承自转，无 billboard
    //      抵消）；④ tier 色 per-instance color（t1039 color 实例表先例）——木镐 tier1 = (138,90,46)、
    //      铁剑 tier3 = (216,216,230)、弓弦 stringPass = (245,245,245)（t330 不随 tier）+ alpha 1.0
    //      （不透明；⚠ 断言口径沿 P-t1039a：calculateTableEntry 落表经 sRGBToLinear，探针镜像线性值）；
    //      ⑤ 天光乘子沿——k = minLight + (1-minLight)×skyLight，setSkyLight 0.5 → 木镐 r = qRound(138×0.7)
    //      = 97，还原 1.0 逐位回 138（t144 夜间变暗契约）。
    //   阴性轮敏感：摘收纳谓词（false && 前缀）→ 本腿互斥面恰红。
    {
        // Quick3D calculateTableEntry 的 sRGB→linear 逐字镜像（P-t1039a 同款 lambda）
        const auto srgbToLinear1041a = [](float c) {
            return c * (c * (c * 0.305306011f + 0.682171111f) + 0.012522878f);
        };
        ItemEntityManager items1041a;
        ToolDropInstancing fPick1041a, fSword1041a, fBow1041a, fStr1041a, fNeg1041a;
        fPick1041a.setManager(&items1041a);
        fPick1041a.setFamilyId(int(ToolRegistry::PickaxeWood));  // 木镐 tier 1
        fSword1041a.setManager(&items1041a);
        fSword1041a.setFamilyId(int(ToolRegistry::SwordIron));   // 铁剑 tier 3
        fBow1041a.setManager(&items1041a);
        fBow1041a.setFamilyId(int(ToolRegistry::Bow));           // 弓（type 7）
        fStr1041a.setManager(&items1041a);
        fStr1041a.setStringPass(true);                           // 弓弦第二实例表（t330 白弦）
        fStr1041a.setFamilyId(int(ToolRegistry::Bow));
        fNeg1041a.setManager(&items1041a);                       // 误指派面（familyId 沿途换）
        // 格距 ≥3（> kMergeRadius=2，防就近合并塌缩活体数——P-t1027a 同 rig 纪律）
        items1041a.spawnItem(10, 40, 10, int(ToolRegistry::PickaxeWood), 1);
        items1041a.spawnItem(14, 40, 10, int(ToolRegistry::PickaxeWood), 1);
        items1041a.spawnItem(18, 40, 10, int(ToolRegistry::SwordIron), 1);
        items1041a.spawnItem(22, 40, 10, int(ToolRegistry::Bow), 1);
        items1041a.spawnItem(26, 40, 10, int(ToolRegistry::Shears), 1);  // 图标族（不入工具 3D 桶）
        items1041a.spawnItem(30, 40, 10, 0x200, 1);                      // 材料段（木棒）
        items1041a.spawnItem(34, 40, 10, 13, 1);                         // 火把（3D 形状族）
        items1041a.spawnItem(38, 40, 10, int(ToolRegistry::FishingRod), 1); // 钓鱼竿（无掉落分支，不入族）
        const int cntPick1041a = fPick1041a.probeInstanceCount();
        const int cntSword1041a = fSword1041a.probeInstanceCount();
        const int cntBow1041a = fBow1041a.probeInstanceCount();
        const int cntStr1041a = fStr1041a.probeInstanceCount();
        bool ok1041a = cntPick1041a == 2 && cntSword1041a == 1
                       && cntBow1041a == 1 && cntStr1041a == 1; // ①② 实例数=活体数（弓身/弦同表条数）
        fNeg1041a.setFamilyId(int(ToolRegistry::Shears));
        ok1041a = ok1041a && fNeg1041a.probeInstanceCount() == 0;    // ① 剪刀即便误指派也恒空
        fNeg1041a.setFamilyId(int(ToolRegistry::FishingRod));
        ok1041a = ok1041a && fNeg1041a.probeInstanceCount() == 0;    // ① 钓鱼竿不入族（登记面）
        fNeg1041a.setFamilyId(0x200);
        ok1041a = ok1041a && fNeg1041a.probeInstanceCount() == 0;    // ① 材料段拒
        fNeg1041a.setFamilyId(13);
        ok1041a = ok1041a && fNeg1041a.probeInstanceCount() == 0;    // ① 3D 形状族拒
        // ③ 变换内容（木镐首条）
        QVector3D p1041a, sc1041a; QQuaternion q1041a;
        ok1041a = ok1041a && fPick1041a.probeInstanceAt(0, &p1041a, &sc1041a, &q1041a, nullptr);
        ok1041a = ok1041a && std::abs(p1041a.x() - 10.5f) < 1e-4f && std::abs(p1041a.z() - 10.5f) < 1e-4f;
        ok1041a = ok1041a && p1041a.y() >= 40.5f - 1e-4f && p1041a.y() <= 40.65f + 1e-4f;
        ok1041a = ok1041a && std::abs(sc1041a.x() - 0.45f) < 1e-5f
                  && std::abs(sc1041a.y() - 0.45f) < 1e-5f && std::abs(sc1041a.z() - 0.45f) < 1e-5f;
        const QVector3D eu1041a = q1041a.toEulerAngles();
        ok1041a = ok1041a && std::abs(std::remainder(eu1041a.x(), 360.0f)) < 1e-3f
                  && std::abs(std::remainder(eu1041a.z(), 360.0f)) < 1e-3f;
        // ④ tier 色 per-instance color（线性落表口径，k=1：minLight 0.4 + skyLight 1 → k=1）
        const float tolC1041a = 2.0f / 255.0f;
        QColor cPick1041a, cSword1041a, cStr1041a;
        ok1041a = ok1041a && fPick1041a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cPick1041a);
        ok1041a = ok1041a && std::abs(cPick1041a.redF() - srgbToLinear1041a(138.0f / 255.0f)) <= tolC1041a
                  && std::abs(cPick1041a.greenF() - srgbToLinear1041a(90.0f / 255.0f)) <= tolC1041a
                  && std::abs(cPick1041a.blueF() - srgbToLinear1041a(46.0f / 255.0f)) <= tolC1041a
                  && std::abs(cPick1041a.alphaF() - 1.0f) <= 1e-3f; // 不透明（无 hasTransparency）
        ok1041a = ok1041a && fSword1041a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cSword1041a);
        ok1041a = ok1041a && std::abs(cSword1041a.redF() - srgbToLinear1041a(216.0f / 255.0f)) <= tolC1041a
                  && std::abs(cSword1041a.greenF() - srgbToLinear1041a(216.0f / 255.0f)) <= tolC1041a
                  && std::abs(cSword1041a.blueF() - srgbToLinear1041a(230.0f / 255.0f)) <= tolC1041a;
        ok1041a = ok1041a && fStr1041a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cStr1041a);
        ok1041a = ok1041a && std::abs(cStr1041a.redF() - srgbToLinear1041a(245.0f / 255.0f)) <= tolC1041a
                  && std::abs(cStr1041a.greenF() - srgbToLinear1041a(245.0f / 255.0f)) <= tolC1041a
                  && std::abs(cStr1041a.blueF() - srgbToLinear1041a(245.0f / 255.0f)) <= tolC1041a;
        // ⑤ 天光乘子沿：k = 0.4 + 0.6×0.5 = 0.7 → 木镐 r = qRound(138×0.7) = 97；还原逐位回 138
        fPick1041a.setSkyLight(0.5);
        QColor cDim1041a;
        ok1041a = ok1041a && fPick1041a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cDim1041a);
        ok1041a = ok1041a && std::abs(cDim1041a.redF() - srgbToLinear1041a(97.0f / 255.0f)) <= tolC1041a;
        fPick1041a.setSkyLight(1.0);
        ok1041a = ok1041a && fPick1041a.probeInstanceAt(0, nullptr, nullptr, nullptr, &cDim1041a)
                  && std::abs(cDim1041a.redF() - srgbToLinear1041a(138.0f / 255.0f)) <= tolC1041a;
        // ② 拾取沿：首条木镐被拾 → 表塌到 1
        items1041a.setCountAt(0, 0);
        const int cntAfterPick1041a = fPick1041a.probeInstanceCount();
        ok1041a = ok1041a && cntAfterPick1041a == 1;
        if (!ok1041a)
            qInfo().noquote() << "  [t1041a diag] pick" << cntPick1041a << "sword" << cntSword1041a
                              << "bow" << cntBow1041a << "str" << cntStr1041a
                              << "afterPick" << cntAfterPick1041a
                              << "pickLin r/g/b" << cPick1041a.redF() << cPick1041a.greenF()
                              << cPick1041a.blueF() << "strLin r" << cStr1041a.redF()
                              << "dimLin r" << cDim1041a.redF() << "dimExp"
                              << srgbToLinear1041a(97.0f / 255.0f);
        if (!ok1041a) ++totalFail;
        qInfo().noquote() << (ok1041a ? "PASS" : "FAIL")
                          << "| t1041a drop-item instancing batch 4: the tool 3D family "
                             "(isTool3DDrop: pickaxe/hoe/axe/shovel/sword plus bow) feeds per-id "
                             "tool buckets through ToolDropInstancing - shears/materials-0x200/the "
                             "3D-shape-family torch and the branchless fishing rod never enter a "
                             "tool bucket even when (mis)assigned one (cross-family mutex on the "
                             "single C++ predicate), entry counts equal live counts (two wooden "
                             "pickaxes -> 2, collapse to 1 after picking), entries carry exact XZ "
                             "slot positions + the analytic bob band on Y + 0.45 uniform scale + "
                             "pure-Y spin (delegate parity - tools keep the entRoot spin, no "
                             "billboard cancel), tier colors ride the per-instance color table "
                             "(t1039 precedent) with wooden 138/90/46 and iron 216/216/230 plus "
                             "the bow-string second table carrying silk white 245/245/245 "
                             "(t330, tier-independent) asserted against the linear values "
                             "Quick3D's calculateTableEntry stores, and the skylight tint follows "
                             "k = minLight + (1-minLight)*skyLight (dimming to r=97 at skyLight "
                             "0.5, restoring bit-exact at 1.0) (negative-round sensitive: feeder "
                             "predicate removal)";
    }

    // ── P-t1041b 批 4 探针：billboard 图标族双池（异形方块图标族 + 工具/材料图标族）+ 朝相机旋转 ──
    //   BillboardDropInstancing itemIconFamily 两模式（缺省 = isBlockIconBillboardDrop 异形方块图标族
    //   〔楼梯 16/花 49/床〕；true = isIconBillboardDrop 工具/材料图标族〔剪刀 0x110/打火石 0x121/材料
    //   0x200〕）。钉契约：① 双池分桶互斥——火把 13 不进异形桶（3D 形状族）、异形 id 不进图标桶、
    //   工具 3D/整立方不进图标桶（双向跨池谓词面）；② 实例数=活体数 + 拾取塌表；③ 实例表内容——
    //   XZ 精确、Y bob 带、scale 0.3、rotation euler = (camPitch, camYaw, 0)（朝相机旋转进实例表；
    //   billboard 不自转——旧分支显式抵消 rotY）+ camYaw 沿 markDirty 跟随（40→90）。
    //   阴性轮敏感：摘收纳谓词（false && 前缀）→ 本腿互斥面恰红。
    {
        ItemEntityManager items1041b;
        BillboardDropInstancing fStairs1041b, fFlower1041b, fBed1041b, fTorchNeg1041b,
            fStairsNeg1041b, fIcon1041b, fIconNeg1041b;
        fStairs1041b.setManager(&items1041b);
        fStairs1041b.setFamilyId(int(BR::WoodStairs));
        fStairs1041b.setCamPitch(25.0);
        fStairs1041b.setCamYaw(40.0);
        fFlower1041b.setManager(&items1041b);
        fFlower1041b.setFamilyId(int(BR::FlowerRed));
        fBed1041b.setManager(&items1041b);
        fBed1041b.setFamilyId(int(BR::BedRed));
        fTorchNeg1041b.setManager(&items1041b);
        fTorchNeg1041b.setFamilyId(13);              // 火把：3D 形状族 → 异形桶恒空
        fStairsNeg1041b.setManager(&items1041b);
        fStairsNeg1041b.setFamilyId(int(ToolRegistry::Shears)); // 异形模式对剪刀 id 恒空
        fIcon1041b.setManager(&items1041b);
        fIcon1041b.setItemIconFamily(true);
        fIcon1041b.setFamilyId(int(ToolRegistry::Shears));
        fIconNeg1041b.setManager(&items1041b);
        fIconNeg1041b.setItemIconFamily(true);
        fIconNeg1041b.setFamilyId(int(BR::WoodStairs)); // 图标模式对异形 id 恒空（双向互斥）
        items1041b.spawnItem(10, 40, 10, int(BR::WoodStairs), 1);
        items1041b.spawnItem(14, 40, 10, int(BR::FlowerRed), 1);
        items1041b.spawnItem(18, 40, 10, int(BR::BedRed), 1);
        items1041b.spawnItem(22, 40, 10, 13, 1);                              // 火把（3D 族）
        items1041b.spawnItem(26, 40, 10, int(ToolRegistry::Shears), 1);
        items1041b.spawnItem(30, 40, 10, int(ToolRegistry::FlintAndSteel), 1);
        items1041b.spawnItem(34, 40, 10, 0x200, 1);                           // 木棒（材料段）
        const int cntStairs1041b = fStairs1041b.probeInstanceCount();
        bool ok1041b = cntStairs1041b == 1
                       && fFlower1041b.probeInstanceCount() == 1
                       && fBed1041b.probeInstanceCount() == 1
                       && fTorchNeg1041b.probeInstanceCount() == 0
                       && fStairsNeg1041b.probeInstanceCount() == 0
                       && fIcon1041b.probeInstanceCount() == 1
                       && fIconNeg1041b.probeInstanceCount() == 0;            // ①② 双池收纳+互斥
        fIcon1041b.setFamilyId(int(ToolRegistry::FlintAndSteel));
        ok1041b = ok1041b && fIcon1041b.probeInstanceCount() == 1;            // ② 打火石同池（换桶沿）
        fIcon1041b.setFamilyId(0x200);
        ok1041b = ok1041b && fIcon1041b.probeInstanceCount() == 1;            // ② 材料段同池
        fIcon1041b.setFamilyId(int(ToolRegistry::PickaxeWood));
        ok1041b = ok1041b && fIcon1041b.probeInstanceCount() == 0;            // ① 工具 3D 不进图标池
        fIcon1041b.setFamilyId(int(BR::Dirt));
        ok1041b = ok1041b && fIcon1041b.probeInstanceCount() == 0;            // ① 整立方不进图标池
        // ③ 变换内容（楼梯首条）：XZ 精确 + Y bob 带 + scale 0.3 + 朝相机旋转 euler=(25,40,0)
        QVector3D p1041b, sc1041b; QQuaternion q1041b;
        ok1041b = ok1041b && fStairs1041b.probeInstanceAt(0, &p1041b, &sc1041b, &q1041b);
        ok1041b = ok1041b && std::abs(p1041b.x() - 10.5f) < 1e-4f && std::abs(p1041b.z() - 10.5f) < 1e-4f;
        ok1041b = ok1041b && p1041b.y() >= 40.5f - 1e-4f && p1041b.y() <= 40.65f + 1e-4f;
        ok1041b = ok1041b && std::abs(sc1041b.x() - 0.3f) < 1e-5f
                  && std::abs(sc1041b.y() - 0.3f) < 1e-5f && std::abs(sc1041b.z() - 0.3f) < 1e-5f;
        const QVector3D eu1041b = q1041b.toEulerAngles();
        ok1041b = ok1041b && std::abs(eu1041b.x() - 25.0f) < 1e-3f
                  && std::abs(std::remainder(eu1041b.y() - 40.0f, 360.0f)) < 1e-3f
                  && std::abs(std::remainder(eu1041b.z(), 360.0f)) < 1e-3f;
        // ③ 相机沿：setCamYaw(90) → 实例表 rotation 跟随（markDirty 路径重取）；还原 40
        fStairs1041b.setCamYaw(90.0);
        QQuaternion qYaw1041b;
        ok1041b = ok1041b && fStairs1041b.probeInstanceAt(0, nullptr, nullptr, &qYaw1041b);
        const QVector3D euYaw1041b = qYaw1041b.toEulerAngles();
        ok1041b = ok1041b && std::abs(std::remainder(euYaw1041b.y() - 90.0f, 360.0f)) < 1e-3f;
        fStairs1041b.setCamYaw(40.0);
        // ② 拾取沿：楼梯被拾 → 表塌到 0（空槽不进表）
        items1041b.setCountAt(0, 0);
        const int cntAfterPick1041b = fStairs1041b.probeInstanceCount();
        ok1041b = ok1041b && cntAfterPick1041b == 0;
        if (!ok1041b)
            qInfo().noquote() << "  [t1041b diag] stairs" << cntStairs1041b
                              << "afterPick" << cntAfterPick1041b
                              << "eu x/y/z" << eu1041b.x() << eu1041b.y() << eu1041b.z();
        if (!ok1041b) ++totalFail;
        qInfo().noquote() << (ok1041b ? "PASS" : "FAIL")
                          << "| t1041b drop-item instancing batch 4: the two billboard icon "
                             "families feed per-id texture buckets through one BillboardDropInstancing "
                             "class with the itemIconFamily mode switch - the block-icon family "
                             "(isBlockIconBillboardDrop: stairs-16 partial, cross flower, colored "
                             "bed) and the item icon family (isIconBillboardDrop: shears, flint "
                             "and steel, material segment) each admit exactly their own ids with "
                             "two-way cross-pool mutex (the 3D-family torch never enters a "
                             "block-icon bucket, block-icon ids never enter item-icon buckets, "
                             "tools-3D and plain cubes never enter icon buckets), entry counts "
                             "equal live counts and collapse on pickup, entries carry exact XZ + "
                             "the analytic bob band + 0.3 uniform scale, the camera-facing "
                             "rotation rides the instance table as euler (camPitch, camYaw, 0) "
                             "(the bitwise equivalent of the old Ry(camYaw)*Rx(camPitch) child "
                             "compose - billboards do not spin) and follows a camYaw edge via "
                             "markDirty, and the per-id textures stay per-id host buckets since "
                             "the atlas+UV route would need a custom shader (PLAN section 2-A "
                             "forbidden) (negative-round sensitive: feeder predicate removal)";
    }

    // ── P-t1041c 批 4 三池空转门行为腿（t1032c 同款；ToolDropInstancing + BillboardDropInstancing）──
    //   空转（familyId<=0 / 非本族 id / 桶内活体 0 / t1047 O-2：stringPass 通道非弓 id）→ 钟停
    //   （probeTickerActive()==false 且事件泵后仍 false）；活跃沿（setManager / setFamilyId /
    //   setItemIconFamily / setStringPass / manager entitiesChanged 有桶内活体）start + markDirty 兜底。
    //   谓词感知：剪刀/钓鱼竿 id 的工具桶、itemIconFamily 模式下的异形 id 桶恒空转；t1047 O-2 弓门：
    //   stringPass 表只服务弓桶——非弓 id 即便桶内有活体弦表钟恒停（review0912 #2 废钟清偿）。
    //   源面：两个新 feeder 构造体均不含无条件
    //   m_ticker.start()（摘门 lesion 会在构造体重启钟 → 行为面 + 本结构面同步红）。
    //   阴性轮敏感：摘空转门（构造启钟 + refreshTicker 早退）→ 本腿全部「空转必须 false」面恰红；
    //   t1047 O-2 摘弓门 → okStrBowGate1041c 恰红。
    {
        ItemEntityManager items1041c;
        ToolDropInstancing fGate1041c, fStrGate1041c;
        BillboardDropInstancing fBGate1041c;
        const auto pumpFor1041c = [](int ms) {
            QElapsedTimer t1041c; t1041c.start();
            while (!t1041c.hasExpired(ms))
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        // 工具池门：构造 / 接线 / 指派空桶三停
        const bool okCtorIdle1041c = !fGate1041c.probeTickerActive();
        fGate1041c.setManager(&items1041c);
        const bool okWireIdle1041c = !fGate1041c.probeTickerActive();
        fGate1041c.setFamilyId(int(ToolRegistry::PickaxeWood));
        const bool okAssignedIdle1041c = !fGate1041c.probeTickerActive();
        items1041c.spawnItem(10, 40, 10, int(ToolRegistry::PickaxeWood), 1); // 活跃沿
        const bool okActiveEdge1041c = fGate1041c.probeTickerActive()
                                       && fGate1041c.probeInstanceCount() == 1;
        pumpFor1041c(50);
        const bool okActiveRun1041c = fGate1041c.probeTickerActive();
        items1041c.setCountAt(0, 0); // 拾走 → 空转沿 stop
        const bool okIdleEdge1041c = !fGate1041c.probeTickerActive();
        pumpFor1041c(50);
        const bool okIdleStay1041c = !fGate1041c.probeTickerActive();
        // 谓词感知：剪刀/钓鱼竿 id 即便有活体也不为本族 → 恒空转恒空表
        items1041c.spawnItem(14, 40, 10, int(ToolRegistry::Shears), 1);
        fGate1041c.setFamilyId(int(ToolRegistry::Shears));
        const bool okShearsPredIdle1041c = !fGate1041c.probeTickerActive()
                                           && fGate1041c.probeInstanceCount() == 0;
        items1041c.spawnItem(18, 40, 10, int(ToolRegistry::FishingRod), 1);
        fGate1041c.setFamilyId(int(ToolRegistry::FishingRod));
        const bool okRodPredIdle1041c = !fGate1041c.probeTickerActive()
                                        && fGate1041c.probeInstanceCount() == 0;
        // 弦表门：stringPass 通道同门——非弓桶空转，弓活体入桶启钟
        fStrGate1041c.setStringPass(true);
        fStrGate1041c.setManager(&items1041c);
        fStrGate1041c.setFamilyId(int(ToolRegistry::Bow));
        const bool okStrIdle1041c = !fStrGate1041c.probeTickerActive();
        items1041c.spawnItem(22, 40, 10, int(ToolRegistry::Bow), 1);
        const bool okStrActive1041c = fStrGate1041c.probeTickerActive()
                                      && fStrGate1041c.probeInstanceCount() == 1;
        // t1047 O-2 弓门（review0912 #2）：stringPass 谓词感知弓语义——启钟的弓桶切到**非弓 3D 族工具**
        //   （PickaxeWood=type1-5 族内、且有活体）→ 钟恒停恒空表（弦表只服务弓桶；旧口径 7 废钟 ~420
        //   唤醒/秒清偿面）。敏感性注记：剪/竿不在 isTool3DDrop（族谓词先拦），切桶目标必须取族内非弓
        //   id 且有活体，否则面空转（t1047 阴轮首跑实证：剪刀面门摘仍绿）。
        items1041c.spawnItem(22, 42, 10, int(ToolRegistry::PickaxeWood), 1); // 非弓 3D 族活体（敏感性活体；占用槽 3）
        fStrGate1041c.setFamilyId(int(ToolRegistry::PickaxeWood));
        const bool okStrBowGate1041c = !fStrGate1041c.probeTickerActive(); // 只断言钟（review0912 #2 缺陷面=废钟；表条目由 QML visible 门兜底不计成本）
        // billboard 池门：同三停一启 + 模式切换谓词感知（异形 id 在 itemIconFamily 模式下恒空转）
        fBGate1041c.setManager(&items1041c);
        fBGate1041c.setFamilyId(int(BR::WoodStairs));
        const bool okBIdle1041c = !fBGate1041c.probeTickerActive();
        items1041c.spawnItem(26, 40, 10, int(BR::WoodStairs), 1);
        const bool okBActive1041c = fBGate1041c.probeTickerActive()
                                    && fBGate1041c.probeInstanceCount() == 1;
        fBGate1041c.setItemIconFamily(true);
        const bool okBModeIdle1041c = !fBGate1041c.probeTickerActive()
                                      && fBGate1041c.probeInstanceCount() == 0;
        fBGate1041c.setItemIconFamily(false);
        const bool okBModeBack1041c = fBGate1041c.probeTickerActive()
                                      && fBGate1041c.probeInstanceCount() == 1;
        items1041c.setCountAt(4, 0); // 拾走楼梯（t256 slot-reuse：镐拾走后槽 0 被剪刀复用 → 剪刀0/竿1/弓2/O-2镐3/楼梯4）→ 空转沿 stop
        const bool okBIdleEdge1041c = !fBGate1041c.probeTickerActive();
        // 源面：两个新 feeder 构造体均不再含无条件 start（t1032c 结构钉同款）
        const QString exeDir1041c = QCoreApplication::applicationDirPath();
        const auto ctorNoStart1041c = [&exeDir1041c](const QString &rel, const QString &ctorNeedle) {
            QFile f(QDir(exeDir1041c + QStringLiteral("/..")).absoluteFilePath(rel));
            const QString src = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
            const int ctorBegin = src.indexOf(ctorNeedle);
            const int ctorEnd = src.indexOf(QStringLiteral("\n}"), ctorBegin);
            return ctorBegin >= 0 && ctorEnd > ctorBegin
                && !src.mid(ctorBegin, ctorEnd - ctorBegin).contains(QStringLiteral("m_ticker.start()"));
        };
        const bool okCtorSrc1041c =
            ctorNoStart1041c(QStringLiteral("src/Game/tooldropinstancing.cpp"),
                             QStringLiteral("ToolDropInstancing::ToolDropInstancing(QQuick3DObject *parent)"))
            && ctorNoStart1041c(QStringLiteral("src/Game/billboarddropinstancing.cpp"),
                                QStringLiteral("BillboardDropInstancing::BillboardDropInstancing(QQuick3DObject *parent)"));
        const bool ok1041c = okCtorIdle1041c && okWireIdle1041c && okAssignedIdle1041c
                             && okActiveEdge1041c && okActiveRun1041c && okIdleEdge1041c
                             && okIdleStay1041c && okShearsPredIdle1041c && okRodPredIdle1041c
                             && okStrIdle1041c && okStrActive1041c && okStrBowGate1041c
                             && okBIdle1041c && okBActive1041c && okBModeIdle1041c
                             && okBModeBack1041c && okBIdleEdge1041c && okCtorSrc1041c;
        if (!ok1041c)
            qInfo().noquote() << "  [t1041c diag] ctor" << okCtorIdle1041c << "wire" << okWireIdle1041c
                              << "assigned" << okAssignedIdle1041c << "activeEdge" << okActiveEdge1041c
                              << "activeRun" << okActiveRun1041c << "idleEdge" << okIdleEdge1041c
                              << "idleStay" << okIdleStay1041c << "shearsPred" << okShearsPredIdle1041c
                              << "rodPred" << okRodPredIdle1041c << "strIdle" << okStrIdle1041c
                              << "strActive" << okStrActive1041c << "strBowGate" << okStrBowGate1041c
                              << "bIdle" << okBIdle1041c
                              << "bActive" << okBActive1041c << "bModeIdle" << okBModeIdle1041c
                              << "bModeBack" << okBModeBack1041c << "bIdleEdge" << okBIdleEdge1041c
                              << "ctorSrc" << okCtorSrc1041c;
        if (!ok1041c) ++totalFail;
        qInfo().noquote() << (ok1041c ? "PASS" : "FAIL")
                          << "| t1041c batch-4 idle gates (t1032c caliber, both new feeders): the "
                             "16ms animation timers start STOPPED - constructing, wiring and "
                             "assigning an empty bucket all leave them idle, the first live "
                             "member starts them with a fresh table, the active period survives "
                             "an event pump, picking the last member stops them and a pump does "
                             "not revive them, the gates are predicate-aware (shears and the "
                             "branchless fishing rod keep a tool bucket idle and empty, the "
                             "stringPass table only arms for a live bow bucket, switching the "
                             "stringPass feeder to a non-bow id with live members never arms "
                             "its clock (t1047 O-2 bow gate - the seven dead string clocks are "
                             "gone), and switching "
                             "the billboard feeder into itemIconFamily mode stops the clock for "
                             "a live block-icon id and back), and neither new constructor "
                             "carries an unconditional timer start - idle feeders no longer "
                             "wake ~60x/s or push empty tables to render sync (negative-round "
                             "sensitive: idle-gate removal)";
    }

    // ── P-t1041d 批 4 接线源钉 + 口径逐字钉（t1032d/t1039d 同纪律；QML 编排面盲区的源钉覆盖）──
    //   QML 编排（reassignToolBuckets / reassignItemBuckets / reassignBlockIconBuckets / has*Bucket）
    //   headless 不可行为级断言（review0910 #2 盲区）→ pinSet 剥注释钉接线在位（三 host id / 薄委托
    //   谓词 / 桶排除链 6+3+1 针 / familyId 绑定 / 几何 per-id / stringPass / itemIconFamily /
    //   camPitch/camYaw 绑定 / 图标 wrapper）+ itementitymanager 三谓词声明 + C++ 口径逐字（收纳谓词 /
    //   tier 色映射 / 弦白 245 / scale / bob 公式 / 天光 k / 空转门 / entitiesChanged 沿 / billboard
    //   朝相机 rotation）。材料/异形贴图材质逐字同参以 window 切片承载（pinSet 表达不了的顺序面）。
    {
        const QString exeDir1041d = QCoreApplication::applicationDirPath();
        const QString root1041d = QDir(exeDir1041d + QStringLiteral("/..")).absolutePath();
        QStringList miss1041d;
        miss1041d << pinSet(root1041d + QStringLiteral("/src/ui/Main.qml"), {
            {"qml-tool-host-id", "id: toolDropInstHost"},
            {"qml-tool-reassign-fn", "function reassignToolBuckets"},
            {"qml-tool-reassign-call", "toolDropInstHost.reassignToolBuckets()"},
            {"qml-tool-predicate", "itemEntities.isTool3DDrop(id)"},
            {"qml-tool-hasbucket-exclude", "&& !toolDropInstHost.hasToolBucket(entRoot.entId)", 6},
            {"qml-tool-geometry-per-id", "geometry: toolDropInstHost.geomForId(toolDropInstHost.buckets[index])"},
            {"qml-tool-familyid-binding", "familyId: toolDropInstHost.buckets[index]", 2},
            {"qml-tool-stringpass", "stringPass: true"},
            {"qml-icon-host-id", "id: itemIconInstHost"},
            {"qml-icon-reassign-fn", "function reassignItemBuckets"},
            {"qml-icon-reassign-call", "itemIconInstHost.reassignItemBuckets()"},
            {"qml-icon-predicate", "itemEntities.isIconBillboardDrop(id)"},
            {"qml-icon-hasbucket-exclude", "&& !itemIconInstHost.hasItemBucket(entRoot.entId)", 3},
            {"qml-icon-itemfamily", "itemIconFamily: true"},
            {"qml-icon-familyid-binding", "familyId: itemIconInstHost.buckets[index]"},
            {"qml-icon-texture-wrapper", "sourceItem: Item {"},
            {"qml-icon-toolicon-type", "toolType: hotbarVM.toolType(itemIconInstHost.buckets[index])"},
            {"qml-icon-maticon-id", "materialId: itemIconInstHost.buckets[index]"},
            {"qml-blockicon-host-id", "id: blockIconInstHost"},
            {"qml-blockicon-reassign-fn", "function reassignBlockIconBuckets"},
            {"qml-blockicon-reassign-call", "blockIconInstHost.reassignBlockIconBuckets()"},
            {"qml-blockicon-predicate", "itemEntities.isBlockIconBillboardDrop(id)"},
            {"qml-blockicon-hasbucket-exclude", "&& !blockIconInstHost.hasIconBucket(entRoot.entId)"},
            {"qml-blockicon-familyid-binding", "familyId: blockIconInstHost.buckets[index]"},
            {"qml-blockicon-iconsource", "hotbarVM.iconSourceForBlock(blockIconInstHost.buckets[index])"},
            {"qml-billboard-campitch", "camPitch: cam.eulerRotation.x", 2},
            {"qml-billboard-camyaw", "camYaw: cam.eulerRotation.y", 2},
        });
        miss1041d << pinSet(root1041d + QStringLiteral("/src/Game/itementitymanager.h"), {
            {"hdr-tool3d-decl", "Q_INVOKABLE static bool isTool3DDrop(int itemId);"},
            {"hdr-iconbillboard-decl", "Q_INVOKABLE static bool isIconBillboardDrop(int itemId);"},
            {"hdr-blockiconbillboard-decl", "Q_INVOKABLE static bool isBlockIconBillboardDrop(int itemId);"},
        });
        miss1041d << pinSet(root1041d + QStringLiteral("/src/Game/tooldropinstancing.cpp"), {
            {"feeder-tool-filter", "ItemEntityManager::isTool3DDrop(m_familyId)", 2},
            {"feeder-tool-tier-gold", "cr = 242; cg = 200; cb = 50;"},
            {"feeder-tool-tier-copper", "cr = 200; cg = 120; cb = 80;"},
            {"feeder-tool-tier-diamond", "cr = 79;  cg = 217; cb = 210;"},
            {"feeder-tool-tier-iron", "cr = 216; cg = 216; cb = 230;"},
            {"feeder-tool-tier-stone", "cr = 154; cg = 154; cb = 154;"},
            {"feeder-tool-string-white", "cr = cg = cb = 245;"},
            {"feeder-tool-bob-formula", "0.075 * (1.0 - std::cos(M_PI * s2))"},
            {"feeder-tool-stagger", "slot * 0.37"},
            {"feeder-tool-scale", "QVector3D(0.45f, 0.45f, 0.45f)"},
            {"feeder-tool-lightk", "m_minLight + (1.0 - m_minLight) * m_skyLight"},
            {"feeder-tool-idle-start", "m_ticker.start()"},
            {"feeder-tool-idle-stop", "m_ticker.stop()"},
            {"feeder-tool-entities-edge", "connect(m_manager, &ItemEntityManager::entitiesChanged"},
        });
        miss1041d << pinSet(root1041d + QStringLiteral("/src/Game/billboarddropinstancing.cpp"), {
            {"feeder-billboard-icon-filter", "ItemEntityManager::isIconBillboardDrop(m_familyId)"},
            {"feeder-billboard-blockicon-filter", "ItemEntityManager::isBlockIconBillboardDrop(m_familyId)"},
            {"feeder-billboard-cam-rot", "QVector3D(float(m_camPitch), float(m_camYaw), 0.0f)"},
            {"feeder-billboard-bob-formula", "0.075 * (1.0 - std::cos(M_PI * s2))"},
            {"feeder-billboard-scale", "QVector3D(0.3f, 0.3f, 0.3f)"},
            {"feeder-billboard-stagger", "slot * 0.37"},
            {"feeder-billboard-idle-start", "m_ticker.start()"},
            {"feeder-billboard-idle-stop", "m_ticker.stop()"},
            {"feeder-billboard-entities-edge", "connect(m_manager, &ItemEntityManager::entitiesChanged"},
        });
        // 材质逐字同参——异形图标桶 Model 材质块（familyId 钉后窗）含旧 billboard 分支参数。
        QFile mf1041d(root1041d + QStringLiteral("/src/ui/Main.qml"));
        const QString qml1041d = mf1041d.open(QIODevice::ReadOnly)
            ? QString::fromUtf8(mf1041d.readAll()) : QString();
        const int bi1041d = qml1041d.indexOf(
            QStringLiteral("familyId: blockIconInstHost.buckets[index]"));
        const QString matWin1041d = (bi1041d >= 0) ? qml1041d.mid(bi1041d, 1200) : QString();
        const bool okMatParity1041d = bi1041d >= 0
            && matWin1041d.contains(QStringLiteral("alphaCutoff: 0.5"))
            && matWin1041d.contains(QStringLiteral("opacity: 0.99"))
            && matWin1041d.contains(QStringLiteral("baseColor: terrainLight(worldClock.skyLight)"))
            && matWin1041d.contains(QStringLiteral("generateMipmaps: false"))
            && matWin1041d.contains(QStringLiteral("hotbarVM.iconSourceForBlock(blockIconInstHost.buckets[index])"));
        const bool okPins1041d = miss1041d.isEmpty();
        const bool ok1041d = okMatParity1041d && okPins1041d;
        if (!ok1041d)
            qInfo().noquote() << "  [t1041d diag] mat" << okMatParity1041d
                              << "pin miss:" << miss1041d.join(QLatin1Char(','));
        if (!ok1041d) ++totalFail;
        qInfo().noquote() << (ok1041d ? "PASS" : "FAIL")
                          << "| t1041d batch-4 wiring source pins + verbatim calibers: the three "
                             "new bucket hosts (toolDropInstHost / itemIconInstHost / "
                             "blockIconInstHost) carry their reassignment functions, thin "
                             "predicate delegations (isTool3DDrop / isIconBillboardDrop / "
                             "isBlockIconBillboardDrop single C++ authority), bucket exclusion "
                             "chains on all ten delegate branches (six tool-3D, three item-icon, "
                             "one block-icon), per-bucket geometry switching, the bow stringPass "
                             "second table, the itemIconFamily mode, the camera euler bindings "
                             "and the icon wrapper, the header declares the three family "
                             "predicates, and both feeders carry the verbatim calibers - the "
                             "tier color map (gold 242/200/50, copper 200/120/80, diamond "
                             "79/217/210, iron 216/216/230, stone 154/154/154, wood default), "
                             "silk-white 245 string pass, the analytic bob and slot*0.37 "
                             "stagger, 0.45/0.3 scales, the skylight floor formula, the "
                             "camera-facing rotation in the instance table, the billboard "
                             "material parity window (alphaCutoff 0.5 + opacity 0.99 + "
                             "terrainLight + generateMipmaps false + iconSourceForBlock) and "
                             "the idle-gate plumbing (negative-round sensitive: any pinned "
                             "wiring or formula edit turns this leg red)";
    }

    // ── review27-4 附魔台（94）掉落物 / 资源浏览器双渲染互斥（源码钉）──
    //   isItem3DFamily 家族成员里附魔台不在 isPartialBlock（mesher 靠 chunkgeometry 显式 case 并入）→
    //   BlockCube 分支 visible 对 94 仍 true，与 ItemShapeGeometry 分支叠加 = 满格立方 + 矮台四面共面
    //   z-fight。修法 = 两处 BlockCube 分支 visible 追加家族排除（不把 94 并入 isPartialBlock——放置 /
    //   失撑 / 碰撞链回归面大）。QML 渲染分支 headless 不可行为级断言 → 源码钉两分支互斥（t880 (b) 先例）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        bool ok4 = true;
        {
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            const QString t = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("visible: entRoot.entId !== 13 && !hotbarVM.isPartialBlock"));
            const int i1 = t.indexOf(QStringLiteral("geometry: BlockCube { blockId: entRoot.entId }"), i0);
            if (i0 < 0 || i1 <= i0) {
                ok4 = false;
                qInfo().noquote() << "  [review27-4 diag] Main.qml BlockCube slice miss";
            } else {
                ok4 = ok4 && t.mid(i0, i1 - i0).contains(QStringLiteral("&& !isItem3DFamily(entRoot.entId)"));
            }
            // 互斥另一边：ItemShapeGeometry 分支存在且以同一家族谓词开门。
            ok4 = ok4 && t.indexOf(QStringLiteral("visible: isItem3DFamily(entRoot.entId)")) > i0;
        }
        {
            QFile rf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
            const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
            const int i0 = t.indexOf(QStringLiteral("visible: root.selectedIsCube && !root.selectedIsMob && !root.selectedIsBed"));
            // t965 合法演化：BlockCube 绑定追加 blockState 形态按钮组接线（家族互斥钉意图不变——
            //   锚串同步到新绑定形态，仍锚定查看器预览的 BlockCube 本体）。
            const int i1 = t.indexOf(QStringLiteral("geometry: BlockCube { blockId: root.selectedId; blockState: root.selectedFormState }"), i0);
            if (i0 < 0 || i1 <= i0) {
                ok4 = false;
                qInfo().noquote() << "  [review27-4 diag] ResourceBrowser BlockCube slice miss";
            } else {
                ok4 = ok4 && t.mid(i0, i1 - i0).contains(QStringLiteral("&& !root.selectedIsItem3D"));
            }
        }
        if (!ok4) ++totalFail;
        qInfo().noquote() << (ok4 ? "PASS" : "FAIL")
                          << "| review27-4 enchanting-table dual-render z-fight: both BlockCube branches "
                             "(drop-item delegate in Main.qml + resource-browser preview) now exclude the "
                             "isItem3DFamily / selectedIsItem3D family so the full cube and the real "
                             "ItemShapeGeometry partial shape can never be visible at once (source pin - 94 "
                             "sits outside isPartialBlock so the family-exclusion clause is the only mutual "
                             "exclusion guard; merging 94 into isPartialBlock was rejected to keep the "
                             "place/support/collision chain untouched)";
    }

    // ── review27-5 雪球 / 鸡蛋闪避链 clearAggro=false（源码钉 + 闪避入口全枚举）──
    //   review26 #8 修复只给箭 / 浮标两链传了 clearAggro=false，雪球 / 蛋走 nightwalkerDodge 内
    //   teleportEntity 默认清仇恨 → 0 伤害 4 雪块无限复购投掷物 = 免费远程净化 + 打断前摇 exploit 原封
    //   保留。修法 = nightwalkerDodge 加 clearAggro 形参（默认 true 保近战 30% 闪避「打断激怒」原语义），
    //   雪球 / 蛋两调用点显式 false。Lessons #5：声称封闭 exploit 的修复把同族入口全部枚举进探针——
    //   本钉同时数 entitymanager.cpp 投射物命中分支 m.mobType == MobNightwalker == 4（箭 / 雪球 / 蛋 /
    //   浮标），日后新增第五条闪避入口（如火球补免疫分支）会翻数 → 强制同步更新本探针（排查结论：
    //   火球现无闪避分支——真伤害直击不清仇恨不位移；末影珍珠 / 末影眼不判 mob 命中——非闪避入口）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        bool ok5 = true;
        QFile ef(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
        const QString t = ef.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef.readAll()) : QString();
        const int cntFalse = t.count(QStringLiteral("nightwalkerDodge(mi, world, /*clearAggro=*/false)"));
        const int cntAll = t.count(QStringLiteral("nightwalkerDodge(mi, world"));
        const int cntBranch = t.count(QStringLiteral("m.mobType == MobNightwalker"));
        ok5 = ok5 && cntFalse == 2 && cntAll == cntFalse && cntBranch == 4;
        // nightwalkerDodge 本体把形参透传 teleportEntity（默认 true 走 teleportEntity 缺省——近战链不变）。
        const int i0 = t.indexOf(QStringLiteral("bool EntityManager::nightwalkerDodge"));
        const int i1 = t.indexOf(QStringLiteral("aiEmberling"), i0);
        ok5 = ok5 && i0 >= 0 && i1 > i0
              && t.mid(i0, i1 - i0).contains(QStringLiteral("kNightwalkerTeleportMax, clearAggro)"));
        QFile eh(root + QStringLiteral("/src/Entities/entitymanager.h"));
        const QString th = eh.open(QIODevice::ReadOnly) ? QString::fromUtf8(eh.readAll()) : QString();
        ok5 = ok5 && th.contains(
                  QStringLiteral("bool nightwalkerDodge(int i, World *world, bool clearAggro = true);"));
        QFile pf(root + QStringLiteral("/src/Game/playercontroller.cpp"));
        const QString tp = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
        ok5 = ok5 && tp.count(QStringLiteral("nightwalkerDodge(entityIndex, m_world)")) == 1; // 近战链保持默认清仇恨
        if (!ok5)
            qInfo().noquote() << "  [review27-5 diag] cntFalse" << cntFalse << "cntAll" << cntAll
                              << "cntBranch" << cntBranch;
        if (!ok5) ++totalFail;
        qInfo().noquote() << (ok5 ? "PASS" : "FAIL")
                          << "| review27-5 nightwalker projectile-dodge aggro preservation: snowball and "
                             "egg dodge chains now pass clearAggro=false through nightwalkerDodge (matching "
                             "the arrow/bobber caliber from review26 #8 - a zero-damage "
                             "infinitely-rebuyable projectile must not double as a free remote purge wiping "
                             "enraged/rageTimer/windupTimer and cancelling the attack windup), the melee "
                             "30%-dodge path keeps the default clearAggro=true, and the projectile "
                             "hit-branch count (m.mobType == MobNightwalker == 4: arrow/snowball/egg/"
                             "bobber) pins the complete dodge-entry enumeration so a future fifth entry "
                             "forces a conscious probe update (fireball hits with real damage and no dodge "
                             "branch; pearl/ender-eye never test mob hits)";
    }

    // ── review27-6 船降位后撞睡莲扫层（行为级：快=碎 / 慢=挡 双半边）──
    //   t892 静水降位（稳态船 Y = 顶水格 + 7/8 → floor = 顶水格 W）后，睡莲只存在于「顶水格+1」层
    //   （W+1）而撞碎扫描只向下扫 cy/cy-1 两层 → 高速船穿叶不碎（t630/t711「快=碎」半边静默失效；
    //   慢=挡半边由 boatFootprintBlocked 的 cy/cy+1 采样正常）。修法 = smashLilyPads 扫 cy-1..cy+1 三层。
    //   断言：(a) W 层顶浮船满速撞 W+1 层叶 → 叶碎（Air）+ lilyPadSmashed 信号（t805 骑乘 rig：
    //   tryMount + tickRiddenBoat 定步长驱动）；(b) 低速（wish 0.25 → 稳态 2.0 < 阈值 3.0）同景 →
    //   叶完好且船被挡在叶列前（防过度修复破「慢=挡」契约）。
    {
        World wR6;
        // 48×48×96 seed 77 = t836 已证净空带（t892 同款；地形 ≤81 → 82+ 全空）。
        wR6.setWidth(48); wR6.setDepth(48); wR6.setHeight(96); wR6.setSeed(77);
        const int ty = 83;   // 石底；静水 ty+1（state 0 → 液面 ty+1.875）；叶 ty+2（顶水格+1）
        for (int x = 4; x <= 30; ++x)
            for (int z = 6; z <= 8; ++z) {          // 快道条带（z 中格 7）
                wR6.setBlock(x, ty, z, BR::Stone, 0);
                wR6.setBlock(x, ty + 1, z, BR::Water, 0);
            }
        for (int x = 4; x <= 30; ++x)
            for (int z = 15; z <= 17; ++z) {        // 慢道条带（z 中格 16）
                wR6.setBlock(x, ty, z, BR::Stone, 0);
                wR6.setBlock(x, ty + 1, z, BR::Water, 0);
            }
        wR6.setBlock(20, ty + 2, 7, BR::LilyPad, 0);   // 快道叶
        wR6.setBlock(20, ty + 2, 16, BR::LilyPad, 0);  // 慢道叶
        BoatManager boats6;
        int smashSignals = 0;
        QObject::connect(&boats6, &BoatManager::lilyPadSmashed, &boats6,
                         [&](int, int, int) { ++smashSignals; });
        QVector3D bp6;
        bool crashed6 = false;
        // (a) 快道：spawn + 骑乘 + 满油 +X（速度 >3 后 ~1.8s 到叶列，600 tick 上限裕量）。
        bool fastSmashed = false;
        if (boats6.spawnBoat(6, ty + 1, 7, BoatManager::Oak))
            boats6.tryMount(QVector3D(6.5f, float(ty + 3), 7.5f), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
        for (int t = 0; t < 600 && !fastSmashed; ++t) {
            boats6.tickRiddenBoat(1.0 / 60.0, &wR6, 1.0f, 0.0f, bp6, crashed6);
            if (wR6.blockAt(20, ty + 2, 7) == BR::Air) fastSmashed = true;
        }
        const bool okFast = fastSmashed && smashSignals >= 1;
        // (b) 慢道：wish 0.25 → 稳态速 8×0.25=2.0 < kBoatLilySmashSpeed 3.0 → 叶不碎；船被叶挡停
        //     （footprint 前缘 x+0.5 到 20 前被拒 → 停位 <19.5，容差上界 19.6）。
        float slowMaxX = 0.0f;
        if (boats6.spawnBoat(6, ty + 1, 16, BoatManager::Oak))
            boats6.tryMount(QVector3D(6.5f, float(ty + 3), 16.5f), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
        const int bIdx6 = boats6.ridingIndex();
        for (int t = 0; t < 600; ++t) {
            boats6.tickRiddenBoat(1.0 / 60.0, &wR6, 0.25f, 0.0f, bp6, crashed6);
            if (bIdx6 >= 0) slowMaxX = qMax(slowMaxX, boats6.posAt(bIdx6).x());
        }
        const bool okSlow = wR6.blockAt(20, ty + 2, 16) == BR::LilyPad && slowMaxX < 19.6f;
        const bool okR6 = okFast && okSlow;
        if (!okR6)
            qInfo().noquote() << "  [review27-6 diag] okFast" << okFast << "smashSignals" << smashSignals
                              << "| okSlow" << okSlow << "padIntact"
                              << (wR6.blockAt(20, ty + 2, 16) == BR::LilyPad)
                              << "slowMaxX" << slowMaxX;
        if (!okR6) ++totalFail;
        qInfo().noquote() << (okR6 ? "PASS" : "FAIL")
                          << "| review27-6 boat lily-pad smash layer realigned to the lowered waterline: "
                             "with the t892 still-water surface (boat rest Y = top-water + 7/8, floor = the "
                             "top water cell W) a full-throttle ridden boat smashes the lily pad one layer "
                             "up at W+1 (smashLilyPads now scans cy-1..cy+1 - the pad-only layer is cy+1; "
                             "behavioral via tryMount + tickRiddenBoat rig, lilyPadSmashed signal observed), "
                             "while a slow boat (steady 2.0 b/s < 3.0 threshold) still gets blocked by the "
                             "intact pad before its footprint enters the pad column (fast=smash / slow=stop "
                             "both halves of the t630/t711 contract pinned)";
    }

    // ── review27-7 烧尽终局附着复检（行为级：火把 / 铁活板门 / 铁门随燃失掉落）──
    //   t891 岩浆点燃改道「点燃 → 计时烧尽」后，烧尽终局走 4 参 setBlock（钩子清单无 trapdoor/door
    //   复检、无 6 邻火把扫）→ 贴墙 / 顶立附着物悬空残留（legacy 焚毁路径有 recheckAttachmentsAfterClear）。
    //   修法 = 烧毁分支 setBlock 后补调 recheckAttachmentsAfterClear（门格除外——烧尽门的配对半扇走
    //   上方带湿/雨守卫的专用分支，通用复检无湿守卫抢跑会误清 review24#1/review25#9 保住的湿/雨半扇）。
    //   rig：木板贴岩浆（t891 (a1) 同款点火）×3——板顶立火把（TorchFloor state 0：支撑=下方）/ 板顶立
    //   铁门（下扇 bit3=0 / 上扇 bit3=1）/ 板 +X 侧贴铁活板门。铁门 / 铁活板门非 flammable → 无同态
    //   蔓延干扰（木门 / 木活板门会被燃烧板的逐窗蔓延掷骰点燃烧成另一条链，断言面被污染）。
    //   断言：三板均燃尽（非 Planks）后三附着格全 Air + 各格 blockDroppedAsItem 信号 ≥1（铁门两扇各 1）。
    //   t843 火蔓延路径共用本终局 = 既有缺口顺带收口（同断言覆盖）。
    {
        World wR7;
        wR7.setWidth(48); wR7.setDepth(48); wR7.setHeight(96); wR7.setSeed(77);
        const int fy7 = 83;
        // rig A：岩浆 (9) + 板 (10) + 板顶火把；rig B：岩浆 (15) + 板 (16) + 板顶铁门两扇；
        // rig C：岩浆 (21) + 板 (22) + 板 +X 侧铁活板门 (23)。岩浆横向邻除板侧外砌石（防流岩浆
        // 绕板改地形 / 顶掉附着格），标记格 = 各岩浆正上一格翻转 Air/Stone（t891 poke 手法）。
        const int lavaX7[3] = {9, 15, 21};
        for (int lx : lavaX7) {
            wR7.setBlock(lx, fy7, 7, BR::Stone, 0);
            wR7.setBlock(lx, fy7, 9, BR::Stone, 0);
            wR7.setBlock(lx - 1, fy7, 8, BR::Stone, 0);
            wR7.setBlock(lx, fy7, 8, BR::Lava, 0);
        }
        wR7.setBlock(10, fy7, 8, BR::Planks, 0);
        wR7.setBlock(10, fy7 + 1, 8, BR::Torch, 0);      // TorchFloor=0：支撑 = 下方板
        wR7.setBlock(16, fy7, 8, BR::Planks, 0);
        wR7.setBlock(16, fy7 + 1, 8, BR::IronDoor, 0);   // 下扇（bit3=0）
        wR7.setBlock(16, fy7 + 2, 8, BR::IronDoor, 8);   // 上扇（bit3=1）
        wR7.setBlock(22, fy7, 8, BR::Planks, 0);
        wR7.setBlock(23, fy7, 8, BR::IronTrapdoor, 0);   // 板 +X 侧（唯一侧撑 = 板）
        int dropTorch = 0, dropDoor = 0, dropTrap = 0;
        QObject::connect(&wR7, &World::blockDroppedAsItem, &wR7,
                         [&](int x, int y, int z, int) {
                             if (x == 10 && y == fy7 + 1 && z == 8) ++dropTorch;
                             else if (x == 16 && y >= fy7 + 1 && y <= fy7 + 2 && z == 8) ++dropDoor;
                             else if (x == 23 && y == fy7 && z == 8) ++dropTrap;
                         });
        // 点火（t891 (a1)：每窗 35 调 tickLavaFlow ≥ 节流 30 → 恰 1 真窗；8%/窗 → 480 窗 P(未中)≈1e-17）。
        bool litA = false, litB = false, litC = false;
        int wins7 = 0;
        for (; wins7 < 480 && !(litA && litB && litC); ++wins7) {
            for (int lx : lavaX7)
                wR7.setBlock(lx, fy7 + 1, 8, (wins7 & 1) ? BR::Air : BR::Stone, 0); // 标记翻转 poke 脏
            for (int t = 0; t < 35; ++t) wR7.tickLavaFlow();
            litA = litA || wR7.isBurningAt(10, fy7, 8);
            litB = litB || wR7.isBurningAt(16, fy7, 8);
            litC = litC || wR7.isBurningAt(22, fy7, 8);
        }
        // 燃尽驱动（燃烧计时 ~10 窗 + 余烬衔接；400 调裕量，t891 同款）。
        bool goneA = false, goneB = false, goneC = false;
        for (int t = 0; t < 400 && !(goneA && goneB && goneC); ++t) {
            wR7.tickFire();
            goneA = goneA || wR7.blockAt(10, fy7, 8) != BR::Planks;
            goneB = goneB || wR7.blockAt(16, fy7, 8) != BR::Planks;
            goneC = goneC || wR7.blockAt(22, fy7, 8) != BR::Planks;
        }
        const bool okBurn7 = litA && litB && litC && goneA && goneB && goneC;
        const bool torchGone = wR7.blockAt(10, fy7 + 1, 8) == BR::Air;
        const bool doorGone = wR7.blockAt(16, fy7 + 1, 8) == BR::Air
                              && wR7.blockAt(16, fy7 + 2, 8) == BR::Air;
        const bool trapGone = wR7.blockAt(23, fy7, 8) == BR::Air;
        const bool okR7 = okBurn7 && torchGone && doorGone && trapGone
                          && dropTorch >= 1 && dropDoor >= 2 && dropTrap >= 1;
        if (!okR7)
            qInfo().noquote() << "  [review27-7 diag] okBurn" << okBurn7
                              << "lit" << litA << litB << litC
                              << "gone" << goneA << goneB << goneC
                              << "| torchGone" << torchGone << dropTorch
                              << "doorGone" << doorGone << dropDoor
                              << "trapGone" << trapGone << dropTrap;
        if (!okR7) ++totalFail;
        qInfo().noquote() << (okR7 ? "PASS" : "FAIL")
                          << "| review27-7 burnout endgame re-checks attachments: the burn-timer endgame "
                             "(shared by the t891 lava-ignite path and the t843 fire-spread path - the "
                             "latter's pre-existing gap closes here too) now runs "
                             "recheckAttachmentsAfterClear after the burnout setBlock, so a torch standing "
                             "on the plank, an iron door mounted on it and an iron trapdoor attached to its "
                             "side all break and drop as items at those cells instead of floating "
                             "(behavioral: three attachment cells go Air with blockDroppedAsItem signals; "
                             "iron variants chosen as non-flammable carriers so same-type spread cannot "
                             "divert the door/trapdoor into their own burn chains; door cells themselves "
                             "skip the generic recheck and keep the wet/rain-guarded pair cleanup)";
    }

    // ── review27-8 Waking 渐显期相机基准（行为级）──
    //   相机躺偏移（-look×1.4 / Y−1.35）按床顶躺位标定，但 Waking 入口 leaveBedTeleport 已把 m_pos 瞬移
    //   到床边地面（Y=by）——lie 若沿 Waking 1→0 渐降，渐显期眼位 = by+1.62−1.35·lie，lie>0.44 即低于
    //   床顶（嵌床 / 嵌邻墙观感）。修法 = 出床瞬移单点清 lie + Waking 分支恒 0（躺渐变只在入睡方向
    //   Lying 0→1 使用）。断言：(a) Lying 期 lie 如常 0→1 ramp（入睡方向保留）；(b) Settled 满 lie 后
    //   wakeUpFromBed 进 Waking 瞬间 lie==0 而 fade 仍 1（旧版此刻 lie=1 渐降——正是沉床窗口）；
    //   (c) Waking 中段 fade<0.5 时 lie 仍 0；(d) Lying 中断醒（wakeUp）后 lie==0。
    //   captured 前置：updateSleep 只在 m_captured 路径跑（!captured 早 return 之前不到睡眠段）——
    //   t891 的「挂窗 + grab」载体同款（headless 无指针锁）。
    {
        World wR8;
        wR8.setWidth(48); wR8.setDepth(48); wR8.setHeight(96); wR8.setSeed(77);
        WorldClock clockR8;
        EntityManager entsR8; // 空管理器：hostileNearby 恒 false（怪物拒绝门不触发）
        Hotbar hbR8;
        PlayerController pcR8;
        pcR8.setWorld(&wR8);
        pcR8.setWorldClock(&clockR8);
        pcR8.setEntityManager(&entsR8);
        pcR8.setHotbar(&hbR8);
        QQuickWindow probeWinR8;
        pcR8.setParentItem(probeWinR8.contentItem());
        pcR8.grab(); // m_window 就绪 → setCaptured(true) 走通（t891 同款；进程退出析构配对）
        clockR8.setPhase(0.5f); // 子夜（skyLight<0.5 → isNight）
        const auto pumpR8 = [&pcR8](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            pcR8.tick();
        };
        const int bx8 = 8, by8 = 84, bz8 = 8; // 工作体积净空 + 石地板 + 床（P-t898 同构：foot D=+X）
        for (int dx = -2; dx <= 3; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 3; ++dy) wR8.setBlock(bx8 + dx, by8 + dy, bz8 + dz, BR::Air, 0);
                wR8.setBlock(bx8 + dx, by8 - 1, bz8 + dz, BR::Stone, 0);
            }
        wR8.setBlock(bx8, by8, bz8, BR::BedWhite, quint8(0));
        wR8.setBlock(bx8 - 1, by8, bz8, BR::BedWhite, quint8(8));
        // (a) Lying ramp：trySleepAt → 泵至 lie>0.5（kSleepLieDur=1s → ~35 拍 17ms 泵）。
        pcR8.trySleepAt(bx8, by8, bz8);
        for (int t = 0; t < 200 && pcR8.sleepLie() <= 0.5f; ++t) pumpR8(17);
        const bool okA = pcR8.sleeping() && pcR8.sleepLying() && pcR8.sleepLie() > 0.5f;
        // 进 Settled（满 lie=1 + 按钮窗口）再按钮醒 → Waking 入口。
        for (int t = 0; t < 200 && !pcR8.sleepSettled(); ++t) pumpR8(17);
        pcR8.wakeUpFromBed();
        // (b) Waking 入口：lie 已清 0、fade 仍满值 1、sleeping 真、躺姿门假（站位眼位渐显）。
        const bool okB = pcR8.sleeping() && !pcR8.sleepLying()
                         && pcR8.sleepLie() == 0.0f && pcR8.sleepFade() == 1.0f;
        // (c) Waking 中段：fade<0.5（kSleepWakeDur=0.8s 半程）时 lie 仍恒 0（旧版此处 lie≈0.5 沉床）。
        for (int t = 0; t < 200 && pcR8.sleepFade() >= 0.5f; ++t) pumpR8(17);
        const bool okC = pcR8.sleepFade() < 0.5f && pcR8.sleeping()
                         && pcR8.sleepLie() == 0.0f;
        // (d) 中断醒：再睡 → Lying ramp 到 lie>0.5 → wakeUp（cancelSleep 路径）→ lie==0。
        for (int t = 0; t < 200 && pcR8.sleeping(); ++t) pumpR8(17); // Waking 走完自然收尾
        clockR8.setPhase(0.5f);
        pcR8.trySleepAt(bx8, by8, bz8);
        for (int t = 0; t < 200 && pcR8.sleepLie() <= 0.5f; ++t) pumpR8(17);
        pcR8.wakeUp();
        const bool okD = !pcR8.sleeping() && !pcR8.sleepLying() && pcR8.sleepLie() == 0.0f;
        const bool okR8 = okA && okB && okC && okD;
        if (!okR8)
            qInfo().noquote() << "  [review27-8 diag] okA" << okA << "okB" << okB
                              << "okC" << okC << "okD" << okD
                              << "lie" << pcR8.sleepLie() << "fade" << pcR8.sleepFade()
                              << "sleeping" << pcR8.sleeping();
        pcR8.release();
        probeWinR8.deleteLater();
        if (!okR8) ++totalFail;
        qInfo().noquote() << (okR8 ? "PASS" : "FAIL")
                          << "| review27-8 waking camera anchor: the lie camera offset is calibrated "
                             "for the on-bed lying spot, but leaveBedTeleport has already moved m_pos "
                             "to the bedside floor when Waking starts - so the lie amount is zeroed at "
                             "the teleport (single point) and pinned to 0 through the whole Waking "
                             "phase (fade still ramps 1->0); the lying ramp now only runs in the "
                             "fall-asleep direction (Lying 0->1), so the fade-in eye never sinks into "
                             "the bed or the wall behind it (lie>0.44 was below bed-top under the old "
                             "1->0 wake ramp); interrupt-wake mid-Lying also reads lie==0";
    }

    // ── review27-9 掉落物附魔台「悬浮书」局部坐标（源码钉；纯视觉 headless 不可行为级）──
    //   旧版 dropBookNode y=0.14（疑似 0.46×0.3 误做预缩放）被父级 Model scale 0.3 再乘 → 实际 0.042
    //   < 台顶 0.1125，书整个埋进台体内部不可见。修法 = 与资源浏览器预览 / 放置态 bookDelegate 相同
    //   局部坐标（书心 y=0.46、页 ±0.176、页 scale 0.38×0.03×0.46），父级 0.3 统一缩小、不做预缩放。
    //   钉：Main.qml dropBookNode 切片含新坐标 + 不含旧坐标；ResourceBrowser etBookNode 同 y（两消费端
    //   单一口径互钉）。
    //   t1032 锚串合法演化（t965 先例）：dropBookNode 自 shape Model 子级上移为 entRoot 直属（94 入
    //   形状桶后 shape Model visible=false，书保留 delegate 逐实体渲染）——0.3 父级缩小并入本节点
    //   （position 换算 bobY + 0.46×0.3、显式 scale 0.3，世界变换逐位不变），页坐标两枚原样；查看器
    //   侧锚不动。钉随迁新公式（不放宽：仍逐字钉书心 0.46 常量与补偿式 + 页坐标 count==2）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        bool ok9 = true;
        QString tMain;
        {
            QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
            tMain = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
            const int i0 = tMain.indexOf(QStringLiteral("id: dropBookNode"));
            const int i1 = tMain.indexOf(QStringLiteral("// t219"), i0);
            if (i0 < 0 || i1 <= i0) {
                ok9 = false;
                qInfo().noquote() << "  [review27-9 diag] dropBookNode slice miss";
            } else {
                const QString slice = tMain.mid(i0, i1 - i0);
                ok9 = ok9 && slice.contains(
                          QStringLiteral("position: Qt.vector3d(0, entRoot.bobY + 0.46 * 0.3, 0)"))
                      && slice.contains(QStringLiteral("scale: Qt.vector3d(0.3, 0.3, 0.3)"))
                      && slice.count(QStringLiteral("position: Qt.vector3d(-0.176, 0.045, 0)")) == 1
                      && slice.count(QStringLiteral("position: Qt.vector3d(0.176, 0.045, 0)")) == 1
                      && slice.count(QStringLiteral("scale: Qt.vector3d(0.38, 0.03, 0.46)")) == 2;
                // 负向钉：旧预缩放坐标不得残留（y=0.14 / 页 ±0.088 / 页 scale 0.19×0.03×0.23）。
                ok9 = ok9 && !slice.contains(QStringLiteral("Qt.vector3d(0, 0.14, 0)"))
                      && !slice.contains(QStringLiteral("0.088"))
                      && !slice.contains(QStringLiteral("Qt.vector3d(0.19, 0.03, 0.23)"));
            }
        }
        {
            QFile rf(root + QStringLiteral("/src/ui/ResourceBrowser.qml"));
            const QString t = rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString();
            ok9 = ok9 && t.contains(QStringLiteral("id: etBookNode"))
                      && t.contains(QStringLiteral("position: Qt.vector3d(0, 0.46, 0)"));
        }
        if (!ok9) ++totalFail;
        qInfo().noquote() << (ok9 ? "PASS" : "FAIL")
                          << "| review27-9 drop-item enchanting-table book visible: dropBookNode "
                             "keeps the same local coordinates as the resource-browser preview and "
                             "the placed-state delegate (book center y=0.46 above the 0.375 table "
                             "top, pages at +/-0.176 scaled 0.38x0.03x0.46) - t1032 moved the node "
                             "out of the instanced shape Model into a direct entRoot child (the "
                             "enchanting table id rides the shape bucket while the book stays a "
                             "per-drop delegate render) with the 0.3 uniform shrink folded into "
                             "the node itself (bobY + 0.46*0.3 position compensation, world "
                             "transform bit-identical); the old y=0.14 pre-scale mistake stays "
                             "negatively pinned";
    }

    // ── review27-10 mob 侧站燃块顶（行为级：悬停不燃 / 落顶复燃——与玩家侧对称）──
    //   mob 侧主扫描 yy 从 footY 起（Y 严格）恒不覆盖 footY-1 支撑格：站燃块顶唯一覆盖是 t843 中心列
    //   行，且无 Y 界定 → 跳越 / 下落掠过燃块顶 <1 格误燃 8s。修法 = 站顶分支（XZ 足印覆盖列 + Y 界定
    //   脚底贴支撑面 ±0.002）+ 中心列快速路径同款 Y 界定（玩家侧 review27 #3 口径）。rig：燃板正上 1×1
    //   石井（禁 XZ 漂移，落点确定）；(a) 猪出生悬空 feet=板顶+1.0，慢 tick（dt 0.005×20）下落 <0.16 格
    //   期间（覆盖 ≥5 个 aiTick）不燃——旧中心列行在此窗必燃（阴性回归钉）；(b) 常速 tick 落定板顶后复燃。
    {
        World wR10;
        wR10.setWidth(48); wR10.setDepth(48); wR10.setHeight(96); wR10.setSeed(77);
        EntityManager ents10;
        const QVector3D farL10(-1000.0f, 10.0f, -1000.0f);
        const int cx10 = 10, cy10 = 83, cz10 = 10; // 燃板格；seed 77 地形 ≤81 → 82+ 全空（review27-6 同款）
        wR10.setBlock(cx10, cy10, cz10, BR::Planks, 0);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                for (int dy = 1; dy <= 4; ++dy) wR10.setBlock(cx10 + dx, cy10 + dy, cz10 + dz, BR::Stone, 0); // 井壁
            }
        const bool lit10 = wR10.igniteFlammableAt(cx10, cy10, cz10)
                           && wR10.isBurningAt(cx10, cy10, cz10);
        const int pig10 = ents10.spawnMobTyped(cx10, cy10 + 2, cz10, EntityManager::MobPig,
                                               QStringLiteral("#ee9999"), 20); // feet = 板顶 +1.0（悬空 1 格）
        bool ok10 = lit10 && pig10 >= 0;
        // (a) 悬停窗：慢 tick ×20（0.1s，落 <0.16 格；feet ∈ (板顶, 板顶+1) → footY-1 = 燃板但脚底未贴面）
        //   → 不燃（Y 界定生效；旧版中心列行 footY-1 无 Y 校验在此窗点燃）。
        if (pig10 >= 0) {
            for (int t = 0; t < 20; ++t)
                ents10.tick(0.005, &wR10, farL10, 0.3f, 1.8f, false);
            ok10 = ok10 && !ents10.isBurningAt(pig10);
            // (b) 落定复燃：常速 tick 至 resting（feet 贴板顶 ±snap 缝 ≤0.002）→ 站顶接触点燃。
            bool ignited10 = false;
            for (int t = 0; t < 80 && !ignited10; ++t) {
                ents10.tick(0.05, &wR10, farL10, 0.3f, 1.8f, false);
                ignited10 = ents10.isBurningAt(pig10);
            }
            ok10 = ok10 && ignited10;
            if (!(ok10 && lit10))
                qInfo().noquote() << "  [review27-10 diag] lit" << lit10
                                  << "hoverBurn" << ents10.isBurningAt(pig10)
                                  << "ignited" << ignited10
                                  << "pos" << ents10.posAt(pig10).y();
        }
        if (!ok10) ++totalFail;
        qInfo().noquote() << (ok10 ? "PASS" : "FAIL")
                          << "| review27-10 mob stand-on-burning-top parity: the mob-side scan now "
                             "carries the player-side stand-on branch (footprint columns of the "
                             "support layer with the Y touch bound - feet within kTouchSkin of the "
                             "support face) plus the same Y bound on the t843 center-column fast "
                             "path, so a pig hovering inside the 1-block window above a burning "
                             "plank top (jump-over/fall-through) stays unlit while landing back on "
                             "the top ignites it (behavior mirrors the player side; the misleading "
                             "'already redundant' comment is gone - the strict-Y main scan never "
                             "covered the support layer)";
    }

    // ── review27-11 玩家水灭 / 雨灭（行为级；MC 1.0 着火实体浸水 / 淋雨立即熄灭）──
    //   t888 拿掉随机熄灭后玩家侧无任何提前止损（注释谎称「雨灭走 mob/世界侧」）。修法 = 火段补水灭
    //   （feetInWater/eyeInWater）+ 雨灭（World::rainExtinguishesAt 单一权威，与 mob 侧同判据）。
    //   rig（seed 86 石地板，biome 扫 Plains 列——沙漠列恒 Clear 会假阴性）：(a) 站火点燃 → 撤火干燥
    //   对照仍燃（防「任意 tick 熄灭」假阳性）→ 脚位格换水 → 熄；(b) 两 pc 同景（露天 / 头顶石檐），
    //   撤火后强降雨 → 露天熄、檐下仍燃（雨灭谓词的见天半边隔离）。
    {
        World wR11;
        wR11.setWidth(48); wR11.setDepth(48); wR11.setHeight(96); wR11.setSeed(86);
        EntityManager ents11;
        Hotbar hb11a, hb11b;
        PlayerController pcA, pcB;
        const QVector3D farL11(-1000.0f, 10.0f, -1000.0f);
        const auto pump11 = [&](int ms) {
            QElapsedTimer t; t.start();
            while (t.elapsed() < ms)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        };
        pcA.setWorld(&wR11); pcA.setEntityManager(&ents11); pcA.setHotbar(&hb11a);
        pcB.setWorld(&wR11); pcB.setEntityManager(&ents11); pcB.setHotbar(&hb11b);
        const int fy11 = 83;
        // 选题：随全局天气的群系列（Plains 0 / Forest 3——跳过 Desert 恒晴 / Snowy 恒雪：后者降水恒真
        //   会破「干燥对照仍燃」半边）。两列同 z=6、间隔 ≥4（同群系带，互不掺火/水）。
        int xOpen = -1, xRoof = -1;
        QString biomes11;
        for (int x = 2; x <= 45; ++x) {
            const int b = wR11.biomeIdAt(x, 6);
            if (x < 10) biomes11 += QString::number(b);
            if (b != 0 && b != 3) continue;
            if (xOpen < 0) xOpen = x;
            else if (xRoof < 0 && x >= xOpen + 4) { xRoof = x; break; }
        }
        bool ok11 = xOpen >= 2 && xRoof >= 2;
        if (!ok11)
            qInfo().noquote() << "  [review27-11 diag] biome scan miss xOpen" << xOpen
                              << "xRoof" << xRoof << "row@z6 head" << biomes11;
        if (ok11) {
            for (int x : { xOpen, xRoof }) {
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz) {
                        wR11.setBlock(x + dx, fy11, 6 + dz, BR::Stone, 0);      // 地板
                        for (int dy = 1; dy <= 12; ++dy) wR11.setBlock(x + dx, fy11 + dy, 6 + dz, BR::Air, 0); // 净空到顶
                    }
            }
            for (int dx = -1; dx <= 1; ++dx)                   // 檐：pcB 头顶 y=88 石盖（隔天空）
                for (int dz = -1; dz <= 1; ++dz) wR11.setBlock(xRoof + dx, fy11 + 5, 6 + dz, BR::Stone, 0);
            ok11 = ok11 && wR11.skyLightAt(xOpen, fy11 + 2, 6) >= 15
                        && wR11.skyLightAt(xRoof, fy11 + 2, 6) < 15; // 见天半边 rig 自检
            wR11.setBlock(xOpen, fy11 + 1, 6, BR::Fire, 0);
            wR11.setBlock(xRoof, fy11 + 1, 6, BR::Fire, 0);
            pcA.loadSavedState(xOpen + 0.5f, float(fy11 + 2), 6.5f, -90.0f, -20.0f, 2 /* Survival */);
            pcB.loadSavedState(xRoof + 0.5f, float(fy11 + 2), 6.5f, -90.0f, -20.0f, 2);
            bool litA11 = false, litB11 = false;
            for (int t = 0; t < 40 && !(litA11 && litB11); ++t) {
                pump11(17); ents11.tick(0.05, &wR11, farL11, 0.3f, 1.8f, false);
                pcA.tick(); pcB.tick();
                litA11 = pcA.burning(); litB11 = pcB.burning();
            }
            ok11 = ok11 && litA11 && litB11;
            // 撤火 + 干燥对照：无火无水无雨 → 仍燃（防假阳性；余焰 8s 语义 t888 已钉）。
            wR11.setBlock(xOpen, fy11 + 1, 6, BR::Air, 0);
            wR11.setBlock(xRoof, fy11 + 1, 6, BR::Air, 0);
            for (int t = 0; t < 6; ++t) {
                pump11(17); ents11.tick(0.05, &wR11, farL11, 0.3f, 1.8f, false);
                pcA.tick(); pcB.tick();
            }
            ok11 = ok11 && pcA.burning() && pcB.burning();
            // (a) 水灭：pcA 脚位格置水（feetInWater）→ 数拍内熄。
            wR11.setBlock(xOpen, fy11 + 1, 6, BR::Water, 0);
            for (int t = 0; t < 6 && pcA.burning(); ++t) {
                pump11(17); ents11.tick(0.05, &wR11, farL11, 0.3f, 1.8f, false);
                pcA.tick();
            }
            ok11 = ok11 && !pcA.burning();
            // (b) 雨灭：强降雨（Clear→雨/雪必翻，Thunder 回避雷击；review-g #5 (d) 同款）→ pcB 檐下仍燃、
            //     pcA 已灭保持；再验露天新着火被雨即灭——pcB 移檐外不可（loadSavedState 清火）→ 用 pcA 复燃：
            //     pcA 已在水中（会水灭）→ 改验「檐下 pcB 仍燃」即雨灭谓词的对照组（见天半边）。
            bool precip11 = false;
            for (int i = 0; i < 60 && !precip11; ++i) {
                wR11.tickWeather(1.0e6);
                const int st = wR11.weatherState();
                precip11 = (st == 1 || st == 2);
            }
            for (int t = 0; t < 6; ++t) {
                pump11(17); ents11.tick(0.05, &wR11, farL11, 0.3f, 1.8f, false);
                pcB.tick();
            }
            ok11 = ok11 && precip11 && !pcA.burning() && pcB.burning(); // 檐下对照：雨不灭（不见天）
            if (!ok11)
                qInfo().noquote() << "  [review27-11 diag] xOpen" << xOpen << "xRoof" << xRoof
                                  << "precip" << precip11 << "A(water)" << !pcA.burning()
                                  << "B(roofed, still burning)" << pcB.burning();
        }
        pcA.clearStatusEffects();
        pcB.clearStatusEffects();
        if (!ok11) ++totalFail;
        qInfo().noquote() << (ok11 ? "PASS" : "FAIL")
                          << "| review27-11 player fire extinguish paths: the player fire segment now "
                             "douses immediately when feet or eyes are in water and when the shared "
                             "World::rainExtinguishesAt predicate hits (sky-exposed + precipitating "
                             "column - the same single authority the mob side uses, replacing the "
                             "misleading 'rain handled elsewhere' comment); dry control stays burning "
                             "after the fire source is removed, the roofed control stays burning "
                             "through the rain (sky half of the predicate isolated), and the t888 "
                             "full-8s no-early-stop gap is closed for the player";
    }

    // ── review27-12 walkPhase 骑乘 / 死亡态归零（行为级 + 源码钉）──
    //   骑乘态 / 死亡态在主循环 continue 早退，恒不达 t897 ② 归零块——行走中被放上矿车 / 被击杀的 mob
    //   腿冻结半步相位（注释 / 提交信息却声称覆盖）。修法 = 登乘写链（矿车 / 船，清 moveSpeed 同位置）
    //   与死亡翻转处顺带 walkPhase=stepAccum=0。断言：(a) 行走中（walkPhase≠0 瞬间）damageEntity 致死
    //   → walkPhase==0；(b) 行走中 spawnCart 贴身 + tickVehicleRiding → 登乘（rideCartAt≥0）且
    //   walkPhase==0；(c) 源码钉三写点。
    {
        World wR12;
        wR12.setWidth(48); wR12.setDepth(48); wR12.setHeight(96); wR12.setSeed(77);
        EntityManager ents12;
        MinecartManager carts12;
        ents12.setVehicleManagers(&carts12, nullptr);
        const QVector3D farL12(-1000.0f, 10.0f, -1000.0f);
        const int fy12 = 83;
        for (int dx = -1; dx <= 6; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                wR12.setBlock(10 + dx, fy12, 6 + dz, BR::Stone, 0);
                for (int dy = 1; dy <= 4; ++dy) wR12.setBlock(10 + dx, fy12 + dy, 6 + dz, BR::Air, 0);
            }
        const auto pumpWalk12 = [&](int slot, int maxTicks) {
            QElapsedTimer t; t.start();
            for (int i = 0; i < maxTicks; ++i) {
                while (t.elapsed() < 17 * (i + 1))
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                ents12.tick(0.05, &wR12, farL12, 0.3f, 1.8f, false);
                if (ents12.walkPhaseAt(slot) != 0.0f) return true; // 行走相位推进中
            }
            return false;
        };
        // (a) 死亡翻转：行走中一击致死 → 相位即刻归零（尸体腿中立位）。
        const int pigDead = ents12.spawnMobTyped(10, fy12 + 1, 6, EntityManager::MobPig,
                                                 QStringLiteral("#ee9999"), 20);
        bool okA12 = pigDead >= 0 && pumpWalk12(pigDead, 600);
        if (okA12) {
            ents12.damageEntity(pigDead, 9999);
            okA12 = ents12.deadAt(pigDead) && ents12.walkPhaseAt(pigDead) == 0.0f;
        }
        // (b) 矿车登乘：行走中贴身生成矿车 + tickVehicleRiding（Pass C 登乘扫描）→ 归零。
        const int pigRide = ents12.spawnMobTyped(14, fy12 + 1, 6, EntityManager::MobPig,
                                                 QStringLiteral("#ee9999"), 20);
        bool okB12 = pigRide >= 0 && pumpWalk12(pigRide, 600);
        if (okB12) {
            const QVector3D p12 = ents12.posAt(pigRide);
            carts12.spawnCart(int(std::floor(p12.x())), int(std::floor(p12.y())), int(std::floor(p12.z())));
            for (int t = 0; t < 3 && ents12.rideCartAt(pigRide) < 0; ++t)
                ents12.tickVehicleRiding();
            okB12 = ents12.rideCartAt(pigRide) >= 0 && ents12.walkPhaseAt(pigRide) == 0.0f;
        }
        // (c) 源码钉：三写点（死亡翻转 / 矿车登乘 / 船登乘）各自切片含 walkPhase=stepAccum=0。
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile ef12(root + QStringLiteral("/src/Entities/entitymanager.cpp"));
        const QString t12 = ef12.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef12.readAll()) : QString();
        auto sliceHas12 = [&t12](const QString &from, const QString &to) {
            const int i0 = t12.indexOf(from);
            const int i1 = t12.indexOf(to, i0);
            return i0 >= 0 && i1 > i0
                   && t12.mid(i0, i1 - i0).contains(QStringLiteral("e.walkPhase = 0.0f;"))
                   && t12.mid(i0, i1 - i0).contains(QStringLiteral("e.stepAccum = 0.0f;"));
        };
        const bool okC12 = sliceHas12(QStringLiteral("e.dead = true;"), QStringLiteral("e.deathBurned"))
                           && sliceHas12(QStringLiteral("m_cartMgr->seatMob(best, idx);"), QStringLiteral("continue;"))
                           && sliceHas12(QStringLiteral("m_boatMgr->seatMob(best, bestSeat, idx);"), QStringLiteral("continue;"));
        const bool okR12 = okA12 && okB12 && okC12;
        if (!okR12)
            qInfo().noquote() << "  [review27-12 diag] death" << okA12 << "ride" << okB12
                              << "srcpin" << okC12;
        if (!okR12) ++totalFail;
        qInfo().noquote() << (okR12 ? "PASS" : "FAIL")
                          << "| review27-12 walkPhase zeroing covers riding and death: both early-exit "
                             "states never reach the t897 idle-reset block, so the mount write-sites "
                             "(cart and boat boarding, next to the moveSpeed clear) and the death flip "
                             "now zero walkPhase and stepAccum in place - a walking mob put into a "
                             "minecart or killed mid-stride snaps its legs to neutral instead of "
                             "freezing mid-step (the exact visual bug t897-2 claimed to have fixed); "
                             "comments no longer claim the idle block covers them";
    }

    // ── review27-13 ParticleSystem3D 族 + 世界锚定动画暂停门（源码钉）──
    //   review26 #11「全仓纯视觉 Timer 清点」漏网同族：TorchSmoke/AmbientParticles/WeatherParticles
    //   三处 ParticleSystem3D.running 常开（ESC 硬档照发照飞）；同组件自相矛盾——附魔书 pageFlipTimer
    //   被 gate 但书 bob / 静息 flutter 照跑；刷怪笼 mini-mob 自旋+浮沉未 gate。修法 = 三组件加
    //   worldRunning 注入门（Loader.onLoaded 绑 window.worldRunning，BlockParticles 模式）+ 书 /
    //   刷怪笼动画补 && window.worldRunning（flutter 与大摆互斥改声明式——命令式 stop/restart 会夺
    //   running 绑定）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        bool ok13 = true;
        // 三粒子组件：worldRunning 属性 + running 门 + 无残留 running: true。
        for (const char *f : { "/src/ui/TorchSmoke.qml", "/src/ui/AmbientParticles.qml",
                               "/src/ui/WeatherParticles.qml" }) {
            QFile pf(root + QString::fromLatin1(f));
            const QString t = pf.open(QIODevice::ReadOnly) ? QString::fromUtf8(pf.readAll()) : QString();
            const bool thisOk = t.contains(QStringLiteral("property bool worldRunning: false"))
                                && t.contains(QStringLiteral("running: root.worldRunning"))
                                && !t.contains(QStringLiteral("running: true"));
            if (!thisOk)
                qInfo().noquote() << "  [review27-13 diag] particle file gate miss:" << f;
            ok13 = ok13 && thisOk;
        }
        QFile mf13(root + QStringLiteral("/src/ui/Main.qml"));
        const QString m13 = mf13.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf13.readAll()) : QString();
        // 三个 Loader 注入（Qt.binding → window.worldRunning）。
        ok13 = ok13
               && m13.count(QStringLiteral("smokeLoader.item.worldRunning = Qt.binding(function() { return window.worldRunning })")) == 1
               && m13.count(QStringLiteral("weatherLoader.item.worldRunning = Qt.binding(function() { return window.worldRunning })")) == 1
               && m13.count(QStringLiteral("ambientLoader.item.worldRunning = Qt.binding(function() { return window.worldRunning })")) == 1;
        // 书 bob / 刷怪笼自旋 + 浮沉三处无限循环动画 gate；flutter 声明式互斥（含暂停门）。
        //   t914 契约更新：flutter 的 running 在暂停门上**追加** bookOpen 门（书本开合返修——合拢态
        //   不翻页；worldRunning 暂停门保持在前，语义不变）。
        ok13 = ok13
               && m13.count(QStringLiteral("running: window.worldRunning; loops: Animation.Infinite")) == 3
               && m13.contains(QStringLiteral("running: window.worldRunning && bookRoot.bookOpen && !pageFlipAnim.running; loops: Animation.Infinite"));
        // faceTimer（书朝向 10Hz）同口径 gate（pageFlipTimer 旁的漏网 Timer）。
        ok13 = ok13 && m13.count(QStringLiteral("running: window.worldRunning; repeat: true")) == 1;
        if (!ok13) ++totalFail;
        qInfo().noquote() << (ok13 ? "PASS" : "FAIL")
                          << "| review27-13 particle systems and world-anchored animations under the "
                             "pause gate: the three Particles3D-isolated components (torch smoke, "
                             "ambient, weather) expose a worldRunning property bound through their "
                             "Loaders to window.worldRunning (BlockParticles pattern) so ESC hard "
                             "pause stops emission and freezes in-flight particles; the enchanting "
                             "book bob/flutter, its facing timer and the spawner mini-mob spin/bob "
                             "animations are gated the same way, with the flutter-vs-big-flip mutex "
                             "made declarative (!pageFlipAnim.running) because imperative "
                             "stop()/restart() calls would steal the running binding and re-open the "
                             "pause gate";
    }

    // ── review27-14 烈焰弹边缘对（①朝脚下直射不自燃 ②kCap 拒生成不消耗；行为级）──
    //   ① 玩家侧火球（fireballShooter==-1）直下发射撞非可燃地板：来向格 == 玩家自身格（旧版立地火把
    //     发射者自己点着——火系统按 AABB 接触点燃）。修后落火偏移到不与玩家 AABB 相交的邻格（火球水平
    //     来向一格优先、四向兜底；燃烬者火球 shooter>=0 不偏移保持敌意落火语义）。
    //   ② EntityManager 实体达 kCap：spawnFireball 返 -1（弹未生成）→ 烈焰弹不消耗 / 不挥手（旧版
    //     无条件 takeStack = 弹被吞仍扣 1 发；烈焰弹是生存合成资源 3 发/组）。
    {
        World wR14;
        wR14.setWidth(48); wR14.setDepth(48); wR14.setHeight(96); wR14.setSeed(77);
        const int fy14 = 84;
        for (int x = 1; x <= 14; ++x)
            for (int z = 4; z <= 8; ++z) {
                wR14.setBlock(x, fy14, z, BR::Stone, 0);
                for (int dy = 1; dy <= 3; ++dy) wR14.setBlock(x, fy14 + dy, z, BR::Air, 0);
            }
        // (a) 朝脚下直射：listener 站 (3.5, 85, 6.5)（halfW 0.3 → AABB x[3.2,3.8]），火球从眼位下方
        //     0.5 直落撞脚下石板 → 来向格 = 玩家格 (3,85,6)。断言：玩家格 Air + 邻格 (4,85,6) Fire
        //     （+X 候选首个不交 AABB 的 Air 格）。
        EntityManager entsA;
        const int fbA = entsA.spawnFireball(QVector3D(3.5f, float(fy14 + 1) + 1.62f - 0.5f, 6.5f),
                                            QVector3D(0.0f, -12.0f, 0.0f), 100);
        bool settleA = false;
        for (int t = 0; t < 40 && !settleA; ++t) {
            entsA.tick(0.05f, &wR14, QVector3D(3.5f, float(fy14 + 1), 6.5f), 0.3f, 1.8f, true);
            if (fbA >= 0 && !entsA.aliveAt(fbA)) settleA = true; // 消失 = 撞击结算完成
        }
        const bool okA14 = settleA
                           && wR14.blockAt(3, fy14 + 1, 6) == BR::Air   // 玩家自身格不落火（不自燃）
                           && wR14.blockAt(4, fy14 + 1, 6) == BR::Fire; // 落火偏移到 AABB 外邻格
        // (b) kCap 拒生成：填满实体槽（镜像常量 64 = EntityManager::kCap，private 不跨层读，P18 模式）→
        //     右键发射被拒 → 生存不消耗（count 3 不变）+ 不挥手（发射未发生）。
        constexpr int kMirrorEntityCap = 64;
        EntityManager entsB;
        int spawnedB = 0;
        for (int i = 0; i < kMirrorEntityCap + 2; ++i)
            if (entsB.spawnMobTyped(6 + (i % 8), fy14 + 1 + (i / 8) * 2, 6, EntityManager::MobPig,
                                    QStringLiteral("#ee9999"), 10) >= 0)
                ++spawnedB;
        Hotbar hbB;
        hbB.setStack(0, RecipeRegistry::FireChargeId, 3, 0);
        hbB.setSelectedSlot(0);
        PlayerController pcB;
        pcB.setWorld(&wR14);
        pcB.setEntityManager(&entsB);
        pcB.setHotbar(&hbB);
        QQuickWindow probeWin14;
        pcB.setParentItem(probeWin14.contentItem());
        pcB.grab(); // captured 入口门（P-t891 探针同款挂窗载体，无 show）
        pcB.setSelectedBlock(int(BR::Air)); // t1040 rig 加固（t1030 同式）：烈焰弹材料段→Air 建模（kCap 拒生成腿分支 return 不读此面，兜底通用放置）
        pcB.loadSavedState(3.5f, float(fy14 + 1), 6.5f, 0.0f, -30.0f, 2 /* Survival */);
        int swingB = 0;
        QObject::connect(&pcB, &PlayerController::swingArm, &pcB, [&]() { ++swingB; });
        pcB.placeBlock(); // 右键发射（kCap 满 → spawnFireball 返 -1）
        const bool okB14 = spawnedB == kMirrorEntityCap
                           && hbB.blockIdAt(0) == RecipeRegistry::FireChargeId
                           && hbB.countAt(0) == 3   // 不消耗（旧版 3→2 = 弹被吞仍扣）
                           && swingB == 0;          // 不挥手（发射未发生）
        pcB.release();
        const bool okR14 = okA14 && okB14;
        if (!okR14)
            qInfo().noquote() << "  [review27-14 diag] selfCellAir"
                              << (wR14.blockAt(3, fy14 + 1, 6) == BR::Air)
                              << "offsetFire" << (wR14.blockAt(4, fy14 + 1, 6) == BR::Fire)
                              << "settleA" << settleA << "| spawnedB" << spawnedB
                              << "cnt0" << hbB.countAt(0) << "swingB" << swingB;
        if (!okR14) ++totalFail;
        qInfo().noquote() << (okR14 ? "PASS" : "FAIL")
                          << "| review27-14 fire-charge edge pair: a straight-down player fireball hitting "
                             "the floor under the shooter no longer drops standing fire into the shooter's "
                             "own cell (the approach cell overlaps the player AABB so the fire offsets to "
                             "the first neighbor outside it - horizontal travel direction first, compass "
                             "fallback, owner-side only since emberling splash near the player is intended), "
                             "and a launch rejected at the entity cap (spawnFireball -1) consumes no charge "
                             "and plays no swing (crafted survival ammo must not vanish into a full entity "
                             "table; the old path took the stack unconditionally)";
    }

    // ── review27-15 水面降位外溢对（①睡莲叶高读液面·源码钉 ②两层冰墙骑船 Y 振荡·行为级）──
    //   ① t892 静水液面降 7/8 后，睡莲 quad 旧「cell 底 + 1/16 按满格水面校准」悬空 ~3/16——叶高改读
    //     下方水格 waterSurfaceFrac（partialblockgeometry LilyPad case 消费 nb.belowId/belowState，
    //     chunkgeometry 仅对 LilyPad 填）。mesher 内部不可行为直驱（t893 源码钉先例）。
    //   ② 岸边两层冰墙：首层冰顶 snap 上去后船中心层（restLayer）仍是冰（第二层）→ 下一帧 iceTop 升到
    //     第二层顶（高差 1.0 > snap）回钉水面 → 再 snap = 逐帧 ~0.325 振荡。修 = snap 前查目标层无冰
    //     （埋位守卫）；单层冰面 snap（t892/t805 契约）必须不受影响（对照半边）。
    {
        const QString exeDir15 = QCoreApplication::applicationDirPath();
        const QString root15 = QDir(exeDir15 + QStringLiteral("/..")).absolutePath();
        // (a) 源码钉：三方契约（消费 nb.belowState×waterSurfaceFrac / ctx 字段存在 / chunkgeometry 填）
        //     + 负向钉（旧固定高度行不残留）。
        QFile pg15(root15 + QStringLiteral("/src/World/partialblockgeometry.cpp"));
        const QString t15 = pg15.open(QIODevice::ReadOnly) ? QString::fromUtf8(pg15.readAll()) : QString();
        QFile ph15(root15 + QStringLiteral("/src/World/partialblockgeometry.h"));
        const QString h15 = ph15.open(QIODevice::ReadOnly) ? QString::fromUtf8(ph15.readAll()) : QString();
        QFile cg15(root15 + QStringLiteral("/src/World/chunkgeometry.cpp"));
        const QString c15 = cg15.open(QIODevice::ReadOnly) ? QString::fromUtf8(cg15.readAll()) : QString();
        const bool okA15 = t15.contains(QStringLiteral("waterSurfaceFrac(nb.belowState)"))
                           && t15.contains(QStringLiteral("nb.belowId == BlockRegistry::Water"))
                           && !t15.contains(QStringLiteral("constexpr float yp = 1.0f / 16.0f;"))
                           && h15.contains(QStringLiteral("quint8 belowId = 0;"))
                           && c15.contains(QStringLiteral("nctx.belowId = blockAtWorld(wx, ly - 1, wz)"))
                           && c15.contains(QStringLiteral("nctx.belowState = stateAtWorld(wx, ly - 1, wz)"));
        // (b) 行为级：两层冰墙（lane A z 6..8 x=20 两层）骑船逼近 → 埋位守卫拒 snap，Y 稳定钉水面
        //     84.875（surf = 84 + 7/8）不超 85.0（旧版振荡上界 85.2 = 84+1+0.2）；单层冰面（lane B
        //     z 15..17 x 20..30 一层）snap 照常 → Y 到 85.2 稳定（守卫不过度）。
        World wR15;
        wR15.setWidth(48); wR15.setDepth(48); wR15.setHeight(96); wR15.setSeed(77);
        const int ty15 = 83;
        for (int x = 4; x <= 30; ++x)
            for (int z = 6; z <= 8; ++z) wR15.setBlock(x, ty15, z, BR::Stone, 0);      // lane A 石底
        for (int x = 4; x <= 30; ++x)
            for (int z = 15; z <= 17; ++z) wR15.setBlock(x, ty15, z, BR::Stone, 0);    // lane B 石底
        for (int x = 4; x <= 30; ++x)
            for (int z = 6; z <= 8; ++z)
                if (x != 20) wR15.setBlock(x, ty15 + 1, z, BR::Water, 0);              // lane A 水（x=20 留给冰）
        for (int x = 4; x < 20; ++x)
            for (int z = 15; z <= 17; ++z) wR15.setBlock(x, ty15 + 1, z, BR::Water, 0); // lane B 水（冰前）
        for (int z = 6; z <= 8; ++z) {           // lane A 两层冰墙（x=20，y 84/85）
            wR15.setBlock(20, ty15 + 1, z, BR::Ice, 0);
            wR15.setBlock(20, ty15 + 2, z, BR::Ice, 0);
        }
        for (int x = 20; x <= 46; ++x)
            for (int z = 15; z <= 17; ++z) wR15.setBlock(x, ty15 + 1, z, BR::Ice, 0);  // lane B 单层冰面（铺到车道尽头防驶出跌落）
        BoatManager boats15;
        QVector3D bp15;
        bool crashed15 = false;
        // lane A：两层冰墙逼近（低速 0.4 档稳态 ~3.2 b/s < crash 14；速不参与判据，Y 段逐帧求值）。
        float maxY_A = 0.0f, lastY_A = 0.0f;
        if (boats15.spawnBoat(6, ty15 + 1, 7, BoatManager::Oak)) {
            boats15.tryMount(QVector3D(6.5f, float(ty15 + 3), 7.5f), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
            const int bA = boats15.ridingIndex();
            for (int t = 0; t < 900; ++t) {
                boats15.tickRiddenBoat(1.0 / 60.0, &wR15, 0.4f, 0.0f, bp15, crashed15);
                if (bA >= 0) {
                    lastY_A = boats15.posAt(bA).y();
                    maxY_A = qMax(maxY_A, lastY_A);
                }
            }
        }
        const float surf15 = float(ty15) + 1.0f + 7.0f / 8.0f; // 84.875（降位静水液面镜像）
        const bool okB15 = maxY_A < float(ty15) + 2.0f         // 从未 snap 到首层冰顶 + 船底（85.2）
                           && qAbs(lastY_A - surf15) < 0.05f;  // 终态稳定钉水面（无振荡）
        // lane B：单层冰面 snap 照常（守卫不过度）——Y 到 85.2（冰顶 + 船底 0.2）并稳定。
        float maxY_B = 0.0f, lastY_B = 0.0f;
        if (boats15.spawnBoat(6, ty15 + 1, 16, BoatManager::Oak)) {
            boats15.tryMount(QVector3D(6.5f, float(ty15 + 3), 16.5f), QVector3D(0.0f, -1.0f, 0.0f), 8.0f);
            const int bB = boats15.ridingIndex();
            for (int t = 0; t < 900; ++t) {
                boats15.tickRiddenBoat(1.0 / 60.0, &wR15, 0.4f, 0.0f, bp15, crashed15);
                if (bB >= 0) {
                    lastY_B = boats15.posAt(bB).y();
                    maxY_B = qMax(maxY_B, lastY_B);
                }
            }
        }
        const float iceRest15 = float(ty15) + 2.0f + 0.2f;     // 85.2（单层冰顶 85 + kBoatHullBottom 0.2）
        const bool okC15 = maxY_B >= iceRest15 - 0.05f         // 真的 snap 上去了（t892 契约保持）
                           && qAbs(lastY_B - iceRest15) < 0.05f; // 终态稳定贴冰面（无回落振荡）
        const bool okR15 = okA15 && okB15 && okC15;
        if (!okR15)
            qInfo().noquote() << "  [review27-15 diag] pin" << okA15 << "| laneA maxY" << maxY_A
                              << "lastY" << lastY_A << "| laneB maxY" << maxY_B << "lastY" << lastY_B;
        if (!okR15) ++totalFail;
        qInfo().noquote() << (okR15 ? "PASS" : "FAIL")
                          << "| review27-15 lowered-waterline spillover pair: the lily-pad quad height now "
                             "reads the water cell below through the mesher context (waterSurfaceFrac, "
                             "source-pinned across producer and consumer - the old cell-bottom+1/16 "
                             "calibration floated the leaf ~3/16 above the 7/8 surface), and a ridden boat "
                             "approaching a TWO-layer ice wall no longer y-oscillates ~0.325/frame (the "
                             "snap-up target layer contains the second ice layer = buried hull = wall not "
                             "surface, guard rejects the snap and the boat stays pinned to the waterline "
                             "84.875), while the single-layer ice sheet still snaps up and rests stably at "
                             "85.2 (the t892/t805 ice-road contract the guard must not over-reach)";
    }

    // ── review27-18 腾空羊不开吃（源码钉；行为回归由 P-t897 草栏探针看守）──
    //   groundY = floor(pos.y − halfH) − 1 的垂直窗口腾空时放宽 ~1 格（小跳 / 下落 / 水面缓沉都在窗内）
    //   → 旧版腾空羊可开吃并在 0.5s 后空中消耗 Grass→Dirt。修 = sheepEatGrass 入口 resting 门（检测与
    //   消耗同门）。行为级「落地照常吃」由 P-t897（草栏 grassDirt≥1）覆盖——本探针钉源码契约面。
    {
        const QString exeDir18 = QCoreApplication::applicationDirPath();
        const QString root18 = QDir(exeDir18 + QStringLiteral("/..")).absolutePath();
        QFile ef18(root18 + QStringLiteral("/src/Entities/entitymanager.cpp"));
        const QString t18 = ef18.open(QIODevice::ReadOnly) ? QString::fromUtf8(ef18.readAll()) : QString();
        const int i0 = t18.indexOf(QStringLiteral("bool EntityManager::sheepEatGrass"));
        const int i1 = t18.indexOf(QStringLiteral("const int cx = qFloor(e.pos.x());"), i0);
        const bool okR18 = i0 >= 0 && i1 > i0
                           && t18.mid(i0, i1 - i0).contains(QStringLiteral("if (!e.resting) return false;"));
        if (!okR18) ++totalFail;
        qInfo().noquote() << (okR18 ? "PASS" : "FAIL")
                          << "| review27-18 airborne sheep cannot open a graze cycle: sheepEatGrass gates "
                             "on e.resting (detection and consumption share the same gate - the "
                             "groundY=floor(pos.y-halfH)-1 window is ~1 block too generous while airborne, "
                             "so a hopping/sinking sheep used to open the cycle and consume Grass->Dirt "
                             "mid-air 0.5s later); grounded eating stays covered behaviorally by the "
                             "P-t897 grass-pen probe (grassDirt >= 1)";
    }

    // ── review27-21 清空整个背包语义收口（源码钉：盔甲 4 槽 + 2×2 合成格一并清）──
    //   旧 shift+左键只清 hotbar 9 + main 27 + 光标——盔甲（armorSetStack 独立存储）与合成格原料残留、
    //   输出槽仍显产物 =「整个背包」语义不完整。QML 不可行为直驱（review27-13 源码钉先例）。
    {
        const QString exeDir21 = QCoreApplication::applicationDirPath();
        const QString root21 = QDir(exeDir21 + QStringLiteral("/..")).absolutePath();
        QFile iv21(root21 + QStringLiteral("/src/ui/Inventory.qml"));
        const QString q21 = iv21.open(QIODevice::ReadOnly) ? QString::fromUtf8(iv21.readAll()) : QString();
        // 清空段切片：从 shift 分支标记到 heldBlock 归零（段内须含盔甲循环 + 合成格数组全清 + craftRev++）。
        const int i0 = q21.indexOf(QStringLiteral("if ((mouse.modifiers & Qt.ShiftModifier) !== 0) {"));
        const int i1 = q21.indexOf(QStringLiteral("root.hotbar.heldBlock = 0"), i0);
        const QString seg = (i0 >= 0 && i1 > i0) ? q21.mid(i0, i1 - i0) : QString();
        const bool okR21 = seg.contains(QStringLiteral("root.hotbar.armorSetStack(a, 0, 0)"))
                           && seg.contains(QStringLiteral("root.craftSlots = [0, 0, 0, 0]"))
                           && seg.contains(QStringLiteral("root.craftCounts = [0, 0, 0, 0]"))
                           && seg.contains(QStringLiteral("root.craftRev++"));
        if (!okR21) ++totalFail;
        qInfo().noquote() << (okR21 ? "PASS" : "FAIL")
                          << "| review27-21 clear-whole-inventory semantics completed: the shift+click "
                             "trash action now also clears the four armor slots (armorSetStack, separately "
                             "stored) and the 2x2 crafting grid ingredients (output slot is a craftRev "
                             "derived binding and recomputes to empty), so 'entire inventory' no longer "
                             "leaves armor and ingredients behind while the UI implies a full wipe; "
                             "no-confirmation risk registered as a product note (user-pinned 8-25 "
                             "semantics, adjacency misclick warning in the comment)";
    }

    // ── review27-23 火把拆/重放重置 burnout（行为级：计数窗继承 + 冷却锁定两半）──
    //   m_torchBurnout 唯一摘表路径原只有到期 → (a) 计数窗内拆后同格重放，新火把继承 flips（不到 8 翻
    //   即熔断）；(b) 冷却锁定期内拆后重放，锁定门 continue 跳过评估——基座已供电也错误亮到到期。
    //   修 = notePowerWrite 中 oldId==RedstoneTorch 时 erase 该格键（MC 拆火把重放即重置熔断）。
    //   rig = review26-6(a) 拉杆 NOT 门（每拨恰一翻可精确计数）。
    {
        World wR23;
        wR23.setWidth(48); wR23.setDepth(48); wR23.setHeight(96); wR23.setSeed(77);
        for (int x = 2; x <= 40; ++x)
            for (int z = 2; z <= 40; ++z) wR23.setBlock(x, 84, z, BR::Stone, 0);
        const auto torchOff23 = [&]() {
            return (wR23.stateAt(6, 86, 14) & BR::RedstoneTorchStateOffFlag) != 0;
        };
        const auto setLever23 = [&](quint8 st) {
            wR23.setBlock(6, 85, 15, BR::Lever, st);
            tickN(wR23, 6);
        };
        placeRigBlock(wR23, 6, 85, 14, BR::Stone, 0);
        placeRigBlock(wR23, 6, 85, 15, BR::Lever, 1); // ON
        tickN(wR23, 2);
        placeRigBlock(wR23, 6, 86, 14, BR::RedstoneTorch, 0);
        tickN(wR23, 6);
        // 翻 1..4（lever ON→off / OFF→on ×2）：到窗内 4 翻。
        bool seqOk = torchOff23();
        for (int half = 0; half < 3 && seqOk; ++half) {
            setLever23(quint8(half % 2 == 0 ? 0 : 1));
            seqOk = seqOk && (torchOff23() == (half % 2 == 1)); // lever on 段熄 / off 段亮（review26-6 同相序）
        }
        // (a) 计数窗中段拆 + 重放（lever OFF → 新火把亮）：窗内 flips 已 4。
        wR23.setBlock(6, 86, 14, BR::Air, 0);
        tickN(wR23, 2);
        placeRigBlock(wR23, 6, 86, 14, BR::RedstoneTorch, 0);
        tickN(wR23, 6);
        const bool relitAfterReplace = !torchOff23(); // 新火把立即评估（lever OFF → 亮）
        // 再拨 4 翻（ON,OFF,ON,OFF）：修复后 = 新窗第 1..4 翻全正常（末态亮）；未修 = 累计第 5..8 翻，
        //   第 8 翻（本应重亮）熔断 → 末态灭。
        for (int k = 0; k < 4; ++k) setLever23(quint8(k % 2 == 0 ? 1 : 0));
        const bool okCountReset = seqOk && relitAfterReplace && !torchOff23();
        // (b) 冷却锁定中段拆 + 重放：继续拨到新窗第 8 翻（OFF→本应重亮）→ 锁定灭；确认 30 tick 仍灭；
        //     然后 lever ON（基座供电）+ 拆 + 重放 → 修复后新火把立即评估为灭（offFlag 置位）；未修 =
        //     锁定门 continue 跳过评估 → 错误保持亮到冷却到期。
        for (int k = 0; k < 4; ++k) setLever23(quint8(k % 2 == 0 ? 1 : 0));
        const bool lockedDark = torchOff23();
        for (int t = 0; t < 30; ++t) wR23.tickRedstone();
        const bool stillDark = torchOff23();
        setLever23(1); // ON：锁定中不重评（保持灭）
        const bool darkUnderPower = torchOff23();
        wR23.setBlock(6, 86, 14, BR::Air, 0);
        tickN(wR23, 2);
        placeRigBlock(wR23, 6, 86, 14, BR::RedstoneTorch, 0);
        tickN(wR23, 6);
        const bool okCooldownReset = lockedDark && stillDark && darkUnderPower && torchOff23();
        // 清场（rig 槽位还原）。
        wR23.setBlock(6, 86, 14, BR::Air, 0);
        wR23.setBlock(6, 85, 15, BR::Air, 0);
        wR23.setBlock(6, 85, 14, BR::Air, 0);
        tickN(wR23, 2);
        const bool okR23 = okCountReset && okCooldownReset;
        if (!okR23)
            qInfo().noquote() << "  [review27-23 diag] countReset" << okCountReset << "seqOk" << seqOk
                              << "relit" << relitAfterReplace << "endLit" << !torchOff23()
                              << "| cooldown" << okCooldownReset << "locked" << lockedDark
                              << "still" << stillDark << "underPower" << darkUnderPower;
        if (!okR23) ++totalFail;
        qInfo().noquote() << (okR23 ? "PASS" : "FAIL")
                          << "| review27-23 torch burnout resets on break+replace: notePowerWrite erases "
                             "the cell's burnout entry when the torch is removed, so a torch replaced "
                             "mid-count-window starts from zero flips (the old inherited count fused it "
                             "before 8 fresh toggles) and a torch replaced during the cooldown lock gets "
                             "evaluated immediately - with a powered base it turns OFF at once instead of "
                             "illegally staying lit until cooldown expiry (MC semantics: replacing a torch "
                             "resets its burnout state; lever NOT-gate rig, deterministic integer counters)";
    }

    // ── t905 perf：群系 memo（World::biomeAt 列级缓存）契约探针 ──
    // 背景：tickIceFreeze 每 5s 节流窗遍历全水格索引逐格调 biomeAt（单次最多 5 条 4 阶 fBm ~20 次
    //   Perlin 采样）→ ice 桶 2.5ms/s 均摊尖峰的主源。修法 = 列级 memo（纯函数于 seed，运行期群系不变）。
    // 本探针钉三契约：① 同世界两遍全图读一致（缓存暖后回读同值，不抖动）；② 换 seed 缓存失效
    //   （regenerate 清缓存 → 新图生效，非旧缓存假阳性）；③ 同 seed 跨实例一致（memo 路径 == 纯计算
    //   路径的确定性，§2-K 无损）。时序收益另行实测（perf 报告），此处只钉语义零回归。
    {
        qInfo().noquote() << "=== t905 perf probes (biome memo contract) ===";
        World wA;
        wA.setWidth(64);
        wA.setDepth(64);
        wA.setHeight(32);
        wA.setSeed(4242); // 1337 → 4242：触发 regenerate（缓存清 + 新图）
        QVector<int> pass1, pass2;
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) pass1.push_back(wA.biomeIdAt(x, z)); // 首遍：冷缓存逐列回填
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) pass2.push_back(wA.biomeIdAt(x, z)); // 二遍：全命中
        const bool stable = pass1 == pass2;
        wA.setSeed(999); // 4242 → 999：缓存失效点（generate 清）
        QVector<int> pass3;
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) pass3.push_back(wA.biomeIdAt(x, z));
        const bool invalidated = pass3 != pass1; // 新 seed 必产新图（低频 fBm 全图重排，64×64 全同概率 ~0）
        World wB; // 跨实例同 seed：memo 路径的确定性 == 独立世界纯计算路径
        wB.setWidth(64);
        wB.setDepth(64);
        wB.setHeight(32);
        wB.setSeed(999);
        QVector<int> passB;
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) passB.push_back(wB.biomeIdAt(x, z));
        const bool crossInstance = pass3 == passB;
        const bool okT905 = stable && invalidated && crossInstance;
        if (!okT905)
            qInfo().noquote() << "  [t905 diag] stable" << stable << "invalidated" << invalidated
                              << "crossInstance" << crossInstance;
        // 诊断（非断言，防机器快慢抖动）：64×64=4096 列全图冷（fBm 计算 + 回填）vs 暖（纯缓存读）耗时。
        //   注意 generate 的逐列 worldgen 本身就会调 biomeAt 预热缓存（生产路径 tickIceFreeze 首窗即暖）——
        //   要测真冷路径须 beginLoad（清缓存且不 worldgen）后首遍。
        wB.beginLoad(999); // 网格零填 + 群系缓存清（seed 同值无妨：beginLoad 无条件清）
        QElapsedTimer tCold2; tCold2.start();
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) wB.biomeIdAt(x, z);
        const qint64 nsCold = tCold2.nsecsElapsed();
        QElapsedTimer tWarm2; tWarm2.start();
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x) wB.biomeIdAt(x, z);
        const qint64 nsWarm = tWarm2.nsecsElapsed();
        // 比值即 tickIceFreeze / 天气 / F3 等逐格调 biomeAt 路径的每格节省倍数（冷 = 修复前每窗每格成本）。
        qInfo().noquote() << "  [t905 perf diag] biomeAt 4096 cols cold" << nsCold / 1000 << "us warm"
                          << nsWarm / 1000 << "us ratio"
                          << (nsWarm > 0 ? double(nsCold) / double(nsWarm) : -1.0);
        if (!okT905) ++totalFail;
        qInfo().noquote() << (okT905 ? "PASS" : "FAIL")
                          << "| t905 biome memo: per-column cache in biomeAt returns identical values on "
                             "cold and warm passes, is cleared on seed change (no stale-map false positives), "
                             "and a seeded rebuild matches an independent same-seed world (memo path == pure "
                             "fBm path determinism, PLAN 2-K intact; motivation: tickIceFreeze scans the "
                             "water-cell index calling biomeAt per cell - 5 fbm chains x 4 noise octaves each "
                             "was the dominant cost of the ice bucket hitch)";
    }

    // ── P-t926 咬钩信号重做探针（行为级：判定窗 ~1s；源码钉：下沉幅度 / 待机缩幅 / 鱼粒子距离-方位域）──
    //    用户原话四要素：待机微飘缩幅别喧宾夺主 / 鱼粒子在浮标随机方位随机距离 ≤4 格出现游向鱼钩 /
    //    触钩大幅下沉+水花 / ~1s 判定窗右键收杆。窗口在 Entities 层（kBobberBiteWindowSec）——行为级可
    //    钉：水槽 settle → 等待期（确定性掷骰 ≤30s）→ bobberBit 沿数窗内 tick 到 bobberEscaped，断言
    //    窗长 ∈[0.9,1.1]s 且窗口内 bobberHasBiteAt 恒 true / 逃走后翻 false。视觉三面（QML）按 t887b/
    //    t924 源码钉手法锁语句面：Main.qml 下沉 0.35→0.7 / 微飘 0.035→0.018（fishingBobber 段界滤）；
    //    BlockParticles 距离域 0.7..4.0（≤4 上限）+ 距离解算游速（t884 近距涟漪域 0.9..1.32 退役）。
    {
        World wL;
        wL.setWidth(48); wL.setDepth(48); wL.setHeight(96); wL.setSeed(82);
        EntityManager ents;
        const QVector3D farL(-1000.0f, 10.0f, -1000.0f);
        const auto tickL = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wL, farL, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy, z, BR::Stone, 0);
                wL.setBlock(x, fy + 1, z, BR::Water, 0); // 3×3 水池（t884(a) 同款）
            }
        // (a) 窗长行为级：bobberBit → bobberEscaped 的模拟时长 ≈ 1.0s（dt 0.05 → 恰 20 tick）。
        int bitAt = -1, escAt = -1, t926tick = 0;
        bool biteSeen = false;
        QObject::connect(&ents, &EntityManager::bobberBit, &ents, [&](float, float, float) {
            if (!biteSeen) { biteSeen = true; bitAt = t926tick; }
        });
        QObject::connect(&ents, &EntityManager::bobberEscaped, &ents, [&](float, float, float) {
            if (bitAt >= 0 && escAt < 0) escAt = t926tick;
        });
        const int bobL = ents.spawnBobber(QVector3D(6.5f, float(fy + 4), 6.5f), QVector3D(0, 0, 0), 961);
        bool inWindow = false;
        for (int t = 0; t < 700 && bobL >= 0 && escAt < 0; ++t) { // 等待 ≤30s（600 tick）+ 裕量
            tickL(1, 0.05f);
            t926tick = t + 1;
            if (biteSeen && !inWindow) inWindow = ents.bobberHasBiteAt(bobL); // 窗口内查询恒 true
        }
        const float winSec = (bitAt >= 0 && escAt >= 0) ? float(escAt - bitAt) * 0.05f : -1.0f;
        const bool okA = bobL >= 0 && bitAt >= 0 && escAt >= 0 && inWindow
                         && winSec >= 0.9f && winSec <= 1.1f
                         && !ents.bobberHasBiteAt(bobL); // 逃走后翻 false（鱼跑了）
        ents.removeEntityAt(bobL);
        for (int x = 5; x <= 7; ++x)
            for (int z = 5; z <= 7; ++z) {
                wL.setBlock(x, fy + 1, z, BR::Air, 0);
                wL.setBlock(x, fy, z, BR::Air, 0);
            }

        // (b) 源码钉：下沉 0.7 / 微飘 0.018（fishingBobber 段界滤——段外同名数字不误伤）+ 鱼粒子
        //     距离域 0.7..4.0 / 距离解算游速（BlockParticles 全文——函数名唯一）。
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        QFile mf(root + QStringLiteral("/src/ui/Main.qml"));
        QFile bf(root + QStringLiteral("/src/ui/BlockParticles.qml"));
        const QString mt = mf.open(QIODevice::ReadOnly) ? QString::fromUtf8(mf.readAll()) : QString();
        const QString bt = bf.open(QIODevice::ReadOnly) ? QString::fromUtf8(bf.readAll()) : QString();
        const int f0 = mt.indexOf(QStringLiteral("id: fishingBobber"));
        const int f1 = f0 >= 0 ? mt.indexOf(QStringLiteral("function fishingRodTipWorld"), f0) : -1;
        bool okB = false;
        if (f0 >= 0 && f1 > f0) {
            const QString seg = mt.mid(f0, f1 - f0);
            okB = seg.contains(QStringLiteral("player.hasBite ? 0.7"))
                  && seg.contains(QStringLiteral("Math.sin(fishingBobber.bobPhase) * 0.018"))
                  && !seg.contains(QStringLiteral("* 0.035"))   // 旧待机幅度退役（段内负向）
                  && !seg.contains(QStringLiteral("? 0.35"));   // 旧下沉深度退役（段内负向）
        }
        const bool okC = bt.contains(QStringLiteral("0.7 + 3.3 * ((ph * 7) % 16) / 15"))
                         && bt.contains(QStringLiteral("const sp = 1.2 + 0.8 * rad"))
                         && !bt.contains(QStringLiteral("0.9 + 0.14 * ((ph * 7) % 4)")); // t884 近距涟漪域退役
        const bool okT926 = okA && okB && okC;
        if (!okT926) ++totalFail;
        if (!okT926)
            qInfo().noquote() << "  [t926 diag] okA" << okA << "winSec" << winSec
                              << "biteSeen" << biteSeen << "okB" << okB << "okC" << okC;
        qInfo().noquote() << (okT926 ? "PASS" : "FAIL")
                          << "| t926 bite-signal rework: judgment window widened 0.5->1.0s "
                             "(kBobberBiteWindowSec user override pinned over the MC 1.0 ~0.5s "
                             "value -- 'about one second to right-click reel', behavioral: "
                             "bobberBit->bobberEscaped measures 1.00s +-0.1 in the water rig, "
                             "bobberHasBiteAt true throughout the window and false after the "
                             "escape), bite sink deepened 0.35->0.7 blocks with a stronger "
                             "splash (16 particles, vY 4.4) as the 'pull it under hard' moment, "
                             "idle micro-bob shrunk 0.035->0.018 so the wait phase no longer "
                             "drowns the bite contrast, and the approach-fish particle domain "
                             "replaces t884's close ripple ring (0.9..1.32) with random-bearing "
                             "random-distance <=4-block spawns (0.7..4.0 deterministic golden-"
                             "angle + phase-derived distance) whose swim speed is solved from "
                             "distance (1.2+0.8xrad -> any spawn reaches the hook in <=1.1s, "
                             "arriving-and-dying at the bobber); QML halves pinned at source "
                             "level per the t887b/t924 precedent";
    }

    // ── P-t927 拉拽飞天返修探针（行为级：高台收杆峰值 ≥ 玩家高度 − 1 + 重型折扣；t882 参数返修）──
    //    用户「空中右键收杆看不到生物飞起」：旧 t882 冲量式上抛 2.8+0.18d 的峰值 = vy²/56（32 格远也只
    //    ~1.3 格）在玩家居高时够不着玩家高度。修 = 落差解算 vy = √(2·28·Δh)，目标峰 = 玩家眼位 + 0.6。
    //    rig：玩家 Y+6 石柱高台（脚位 fy+7=90），地面猪平台 fy；仰角扫描甩中（t882(b) 手法——直瞄在轻重力
    //    12 下过冲，需扫描 + 每次失败换新猪重摆压进首游荡窗 30 tick）；收杆后逐 tick 跟踪峰值 Y：
    //    (a) 猪（轻型）峰值 ≥ 玩家脚位 − 1（89.0；解算目标眼位 91.62 + 0.6 → 理想 ~92.7）；
    //    (b) 铁傀儡（重型 halfH 1.20）峰值 > 起点 + 2（仍明显拽起）且 < 猪峰值 − 1（重型折扣可观察）；
    //    (c) 两者耐久均 -5（钩住收杆口径不变）。
    //    flake 声明（t1029 起退役）：原口径「与 t882(b) 同源（mob 首游荡窗内甩钩 RNG），偶发漂移复跑清」
    //    —— wander 冻结缝接管后甩钩窗全确定，本腿不再依赖复跑清。
    {
        World wG;
        wG.setWidth(48); wG.setDepth(48); wG.setHeight(96); wG.setSeed(83);
        EntityManager ents;
        // t1029 wander 冻结缝：同 t882（甩钩窗内 mob wander RNG = 历史偶红源；铁傀儡非追击回退
        //   aiWander 同被冻结）。峰值跟踪只读拉拽抛物线物理，不受影响。
        ents.setWanderFrozen(true);
        const QVector3D farG(-1000.0f, 10.0f, -1000.0f);
        const auto tickG = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wG, farG, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        PlayerController pc;
        Hotbar hb;
        hb.setStack(0, ToolRegistry::FishingRod, 1, ToolRegistry::maxDurability(ToolRegistry::FishingRod));
        hb.setSelectedSlot(0);
        pc.setWorld(&wG);
        pc.setEntityManager(&ents);
        pc.setHotbar(&hb);
        // 玩家高台：柱 (13, fy+1..fy+6, 6) → 脚位 fy+7 = 90（眼 91.62）；目标平台 (19..21, fy, 5..7)。
        for (int y = fy + 1; y <= fy + 6; ++y) wG.setBlock(13, y, 6, BR::Stone, 0);
        for (int x = 19; x <= 21; ++x)
            for (int z = 5; z <= 7; ++z) wG.setBlock(x, fy, z, BR::Stone, 0);
        const float playerFeet = float(fy + 7);
        // 高台甩中 + 收杆 + 峰值跟踪：pitch 扫描 −18..−58（步 2），每次失败换新 mob 重摆（t882(b) 手法）。
        //   返 (峰值 − 起始 Y)，未钩中返 -1。
        const auto hookAndPeak = [&](EntityManager::MobType type, const QString &color, int hp,
                                     float *startYOut) -> float {
            for (int pi = 0; pi <= 20; ++pi) {
                const float pitch = -18.0f - 2.0f * float(pi);
                const int mob = ents.spawnMobTyped(20, fy + 1, 6, type, color, hp);
                tickG(5, 0.05f); // settle（首游荡窗内）
                pc.loadSavedState(13.5f, playerFeet, 6.5f, -90.0f, pitch, 2);
                pc.useFishingRod();
                int bob = -1;
                for (int i = 0; i < ents.count(); ++i)
                    if (ents.aliveAt(i) && ents.kindAt(i) == int(EntityManager::Bobber)) { bob = i; break; }
                bool hooked = false;
                if (bob >= 0) {
                    for (int t = 0; t < 40; ++t) {
                        if (ents.bobberHookedMobAt(bob) == mob) { hooked = true; break; }
                        tickG(1, 0.05f);
                        if (!ents.aliveAt(bob)) break;
                    }
                }
                if (!hooked) {
                    pc.useFishingRod(); // 空收浮标（重试下一仰角）
                    ents.removeEntityAt(mob);
                    continue;
                }
                const float y0 = ents.posAt(mob).y();
                const int dur0 = hb.durabilityAt(0);
                pc.useFishingRod(); // 高台收杆 → 解算式拉拽
                const bool durOk = hb.durabilityAt(0) == dur0 - 5;
                float peak = y0;
                bool landed = false;
                for (int t = 0; t < 160 && ents.aliveAt(mob); ++t) {
                    tickG(1, 0.05f);
                    if (!ents.aliveAt(mob)) break;
                    const float y = ents.posAt(mob).y();
                    if (y > peak) peak = y;
                    if (t > 20 && y < y0 + 0.05f) { landed = true; break; } // 升过又落回 = 飞完
                }
                ents.removeEntityAt(mob);
                if (startYOut) *startYOut = y0;
                return (durOk && (landed || peak > y0 + 0.5f)) ? peak : -1000.0f; // durOk / 峰值有效性门
            }
            return -1.0f; // 全仰角未钩中
        };
        float pigStart = 0.0f, golemStart = 0.0f;
        const float pigPeak = hookAndPeak(EntityManager::MobPig, QStringLiteral("#e8a0a0"), 10, &pigStart);
        const float golemPeak = hookAndPeak(EntityManager::MobIronGolem, QStringLiteral("#c8c8c8"), 40,
                                            &golemStart);
        const bool okA = pigPeak >= playerFeet - 1.0f; // 轻型至少拽到玩家高度 − 1（用户口径）
        const bool okB = golemPeak > golemStart + 2.0f && golemPeak < pigPeak - 1.0f; // 重型拽起但打折
        const bool okT927 = okA && okB;
        if (!okT927) ++totalFail;
        if (!okT927)
            qInfo().noquote() << "  [t927 diag] okA" << okA << "pigPeak" << pigPeak << "from" << pigStart
                              << "okB" << okB << "golemPeak" << golemPeak << "from" << golemStart
                              << "playerFeet" << playerFeet;
        qInfo().noquote() << (okT927 ? "PASS" : "FAIL")
                          << "| t927 hook-reel to player height: drop-solved launch replaces the t882 "
                             "impulse lift -- vy = sqrt(2 x 28 x dh) with dh targeting the player eye "
                             "+ 0.6 overhead margin (old 2.8+0.18xd peaked at vy^2/56 ~1.3 blocks, "
                             "nowhere near an elevated player, the 'reel from a height and the mob "
                             "never visibly flies' root); behavioral rig: player on a Y+6 pillar "
                             "reels a ground pig -> pig peak Y >= player feet - 1 (solved target "
                             "~eye+0.6 vs required feet-1, 3-block margin), weight-class discount "
                             "via the mobType halfH mass proxy (halfHeightAt >= 1.0 = heavy family) "
                             "-- iron golem (halfH 1.20) still yanked >2 blocks but peaks >=1 below "
                             "the pig (x0.55 dh discount, 'heavy barely lifts, light sails over the "
                             "head'); floor dh 1.3 keeps same-level reels visibly airborne; -5 "
                             "durability unchanged; elevation-sweep rig with a fresh mob per "
                             "attempt per the t882(b) wander-window discipline";
    }

    // ── P-t970 钓获生物坠伤豁免探针（行为级 + 源码钉；t927 拉拽链的落地结算面）──
    //    用户第五轮「被拉上来的生物落地有掉落伤害——免除/大幅减轻该次拉拽产生的坠伤（拉拽是玩家动作，
    //    不该顺带摔死目标）」。修 = 两件套：① mob 通用落地摔伤链（此前缺席——落差基准/结算补齐，与玩家
    //    t22 同式同阈值：落差 >3 起摔、每整格 1HP、落水豁免）；② 拉拽一次性豁免（pullMobToward 置
    //    fallExemptOnce，落地沿无条件消费）——豁免只覆盖拉拽抛物线自身那次落地，之后的自体坠落照摔。
    //    腿：(a) 高台猪被拉拽大弧落地 → HP 不变（峰值 ≥ 起点+6 先钉「弧够高」防小弧假绿）；
    //        (b) 对照腿：同落差自落（空投 61→50，无拉拽）→ 照摔（HP 10→2 精确钉 = floor(11−3)=8 伤）；
    //        (c) 豁免一次性消费：同一头猪先拉拽落地（20HP 满血穿过 ~11.8 格弧 = 第二豁免样本）再挖穿
    //            其站立柱（3×3 支撑移除 = 确定性坠落驱动）落差 50→40 → 恰摔 7（HP 20→13 精确钉：
    //            豁免随体存活则 0 伤 / 峰值滞留弧顶则 18 伤——双病同钉）；
    //        (d) 水缓冲：井内自落（>3 格、落点脚位格 Water）→ HP 不变（t200 玩家镜像）；
    //        (e) 源码钉：pullMobToward 体内豁免置位行 + tick 落地沿消费/基准复位/水豁免/伤害式 +
    //            头文件阈值常量值（t882(c) 手法；kMobFallSafeBlocks private 不跨层读 → P18 镜像 3.0）。
    //    直驱说明：(a)(c) 直调 pullMobToward（Entities 层拉拽入口本体）——竿→该入口的接线已由
    //    P-t882(c)/P-t927 源码钉 + 行为腿锁死，此处直驱消掉甩钩仰角扫描 RNG（t882/t927 已知 flake 源），
    //    不向矩阵基线引入新 flake。rig 全 setBlock 自凿先于砌筑（t933 教训：不信地形/净空带），独占局部
    //    World；落点带 = 高台石板（顶 50，8..20×18..30）+ 环地板（顶 40）。
    {
        World wF;
        wF.setWidth(48); wF.setDepth(48); wF.setHeight(96); wF.setSeed(89);
        EntityManager ents;
        const QVector3D farF(-1000.0f, 10.0f, -1000.0f);
        const auto tickF = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wF, farF, 0.3f, 1.8f, false);
        };
        constexpr float kMirrorFallSafe = 3.0f;   // EntityManager::kMobFallSafeBlocks（private 不跨层读，P18 模式）
        constexpr float kMirrorGravity  = 28.0f;  // EntityManager::kGravity（世界重力，P18 模式）
        // ── rig：先凿净空（6..32 × 40..72 × 16..34）后砌筑——地板顶 40 / 高台实心板顶 50 / 玩家柱顶 60 /
        //    井（板面 1×1 开口、两格深水，四壁实心不外流）。
        for (int x = 6; x <= 32; ++x)
            for (int y = 40; y <= 72; ++y)
                for (int z = 16; z <= 34; ++z) wF.setBlock(x, y, z, BR::Air, 0);
        for (int x = 6; x <= 32; ++x)
            for (int z = 16; z <= 34; ++z) wF.setBlock(x, 39, z, BR::Stone, 0);
        for (int x = 8; x <= 20; ++x)
            for (int y = 40; y <= 49; ++y)
                for (int z = 18; z <= 30; ++z) wF.setBlock(x, y, z, BR::Stone, 0);
        for (int y = 40; y <= 59; ++y) wF.setBlock(24, y, 24, BR::Stone, 0);
        // 井 = 板面东南角 5×5 开口（16..20 × 26..30）两格深水——5×5 让空投期游走横漂（≤0.91 格 @0.9s 空落）
        //   结构性逃不出去（出缘需 ≥2.1 格 = 2.3× 裕量，确定性不靠 RNG）；井心 (18.5,28.5) 与拉拽落点带
        //   （x ≤ 14.8，拉向 +x 玩家柱）相隔 ≥ 3.1 格 → (a)(c) 的拉拽落点永不落井。四壁实心 + 底石不外流。
        for (int wx = 16; wx <= 20; ++wx)
            for (int wz = 26; wz <= 30; ++wz) {
                wF.setBlock(wx, 49, wz, BR::Water, 0);
                wF.setBlock(wx, 48, wz, BR::Water, 0);
            }
        // 拉拽驱动（t927 解算式现算 upSpeed：目标峰 = 玩家脚 60 + 眼 1.62 + 过头 0.6 − 目标中心；
        // speed 9.0 ≈ 6+0.35×8.5 中距档；水平位移受击退拖拽 v0/4 限幅 ~2.3 格 → 落点恒在板内 = 确定性）。
        const auto reelPig = [&](int pig, float *peakOut) {
            const float startY = ents.posAt(pig).y();
            const float dh = (60.0f + 1.62f + 0.6f) - startY;
            const bool pulled = ents.pullMobToward(pig, QVector3D(24.5f, 60.0f, 24.5f), 9.0f,
                                                   std::sqrt(2.0f * kMirrorGravity * dh));
            float peak = startY;
            int stillT = 0;
            for (int t = 0; t < 300 && ents.aliveAt(pig); ++t) {
                tickF(1, 0.05f);
                const float y = ents.posAt(pig).y();
                if (y > peak) peak = y;
                if (t > 20 && y < startY + 0.05f) { ++stillT; if (stillT >= 3) break; } // 落回起高带 = 飞完
            }
            if (peakOut) *peakOut = peak;
            return pulled;
        };

        // (a) 拉拽大弧落地免摔：起脚 50 → 弧顶 ~61.3（落差 ~11.3，无豁免将摔 floor(11.3−3)=8）→ 落回板。
        bool okA = false;
        float peakA = 0.0f;
        int hpA = -1;
        {
            const int pig = ents.spawnMobTyped(12, 50, 24, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 10);
            tickF(5, 0.05f); // 贴台 settle（首游荡窗内抢拍，t836(d) 纪律）
            reelPig(pig, &peakA);
            peakA -= 0.45f;                  // 中心 → 脚位口径（diag 用）
            tickF(6, 0.05f);                 // 结算裕量（落地沿必已发生；防早读假绿/假红）
            hpA = ents.healthAt(pig);
            okA = peakA >= 56.0f             // 弧够高钉（峰值脚位 ≥ 起点 50 + 6；解算目标 ~61.3）
                  && hpA == 10;              // 落地零伤（豁免消费；阴性轮此处 = 2 红）
            ents.removeEntityAt(pig);
        }

        // (b) 对照腿：同落差自落（无拉拽空投 61→50 = 落差 11）→ 照摔 floor(11−3)=8 → HP 10−8=2 精确钉。
        bool okB = false;
        int hpB = -1;
        {
            const int pig = ents.spawnMobTyped(12, 61, 24, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 10);
            for (int t = 0; t < 120 && ents.aliveAt(pig); ++t) {
                tickF(1, 0.05f);
                if (ents.posAt(pig).y() < 51.0f) break; // 已落板带
            }
            tickF(6, 0.05f); // 结算裕量（落地沿必已发生）
            hpB = ents.healthAt(pig);
            // 精确钉：spawn 脚位恰 61.0、板顶恰 50.0 → 落差 11.0 → dmg = floor(11−3) = 8（镜像常量现算）
            const int expectB = 10 - int(std::floor(11.0f - kMirrorFallSafe));
            okB = hpB == expectB;
            ents.removeEntityAt(pig);
        }

        // (c) 豁免一次性消费：满血 20 猪先拉拽落地（满血穿过 = 第二豁免样本）→ 挖穿其站立柱（3×3 支撑
        //     移除 = 确定性坠落驱动，替代击退推挤——推挤位移会被 RNG 游走对冲，实测一轮假红）→ 落差
        //     50→40 = 10 → 7 伤 → 掉血 ≥4 = 豁免未随体存活（消费成立）。
        bool okC = false;
        int hpC = -1;
        {
            const int pig = ents.spawnMobTyped(12, 50, 24, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 20);
            tickF(5, 0.05f);
            const bool pulled = reelPig(pig, nullptr);
            tickF(6, 0.05f); // 拉拽落地结算裕量
            const int px = qFloor(ents.posAt(pig).x());
            const int pz = qFloor(ents.posAt(pig).z());
            for (int dx2 = -1; dx2 <= 1; ++dx2)          // 3×3 柱挖穿 40..49：footprint 任一列支撑
                for (int dz2 = -1; dz2 <= 1; ++dz2)      // 都被移除（t362 复探口径对偶）→ 必失撑
                    for (int y = 40; y <= 49; ++y)
                        wF.setBlock(px + dx2, y, pz + dz2, BR::Air, 0);
            for (int t = 0; t < 200 && ents.aliveAt(pig); ++t) {
                tickF(1, 0.05f);
                if (ents.posAt(pig).y() < 41.0f) break; // 已落地板带（地板顶 40）
            }
            tickF(6, 0.05f); // 结算裕量
            hpC = ents.healthAt(pig);
            // 精确钉 = 双铁证：①豁免一次性消费（豁免若随体存活 → 零伤 hpC 20）；②落地沿峰值复位
            // （峰值若滞留拉拽弧顶 ~61.7 → 落差 21.7 → 18 伤 hpC 2；恰复位到落点 50 → 落差 10 → 7 伤）。
            const int expectC = 20 - int(std::floor(10.0f - kMirrorFallSafe));
            okC = pulled && hpC == expectC;
            ents.removeEntityAt(pig);
        }

        // (d) 水缓冲：井心 (18,28) 上方空投（61→井底 48 = 落差 13，无水将摔 10）→ 落点脚位格 Water → 零伤。
        bool okD = false;
        int hpD = -1;
        float yD = 0.0f;
        {
            const int pig = ents.spawnMobTyped(18, 61, 28, EntityManager::MobPig,
                                               QStringLiteral("#e8a0a0"), 10);
            for (int t = 0; t < 400 && ents.aliveAt(pig); ++t) {
                tickF(1, 0.05f);
                if (ents.posAt(pig).y() < 48.6f) break; // 沉底带（井底支撑顶 48 + halfH）
            }
            tickF(20, 0.05f); // 沉底/结算裕量（水中缓沉 ≤3 b/s）
            yD = ents.posAt(pig).y();
            hpD = ents.healthAt(pig);
            okD = hpD == 10; // 水豁免（脚位格 Water）；无水同落差 = 10 伤必死
            if (!okD)
                qInfo().noquote() << "  [t970 d diag] finalY" << yD
                                  << "finalXZ" << ents.posAt(pig).x() << ents.posAt(pig).z()
                                  << "cells b50" << wF.blockAt(18, 50, 28) << "b49" << wF.blockAt(18, 49, 28)
                                  << "b48" << wF.blockAt(18, 48, 28) << "b47" << wF.blockAt(18, 47, 28)
                                  << "alive" << ents.aliveAt(pig);
            ents.removeEntityAt(pig);
        }

        // (e) 源码钉：豁免置位行（pullMobToward 体界内）+ 落地沿消费/复位/水豁免/伤害式 + 头文件常量值。
        bool okE = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString emPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                       QStringLiteral("src/Entities/entitymanager.cpp"));
            const QString emhPath = QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(
                                        QStringLiteral("src/Entities/entitymanager.h"));
            QFile fc(emPath), fh(emhPath);
            if (fc.open(QIODevice::ReadOnly) && fh.open(QIODevice::ReadOnly)) {
                const QString t = QString::fromUtf8(fc.readAll());
                const QString th = QString::fromUtf8(fh.readAll());
                const int b0 = t.indexOf(QStringLiteral("bool EntityManager::pullMobToward"));
                const int b1 = t.indexOf(QStringLiteral("bool EntityManager::bobberHasBiteAt"));
                QString pullBody;
                if (b0 >= 0 && b1 > b0) {
                    for (const QString &line : t.mid(b0, b1 - b0).split(QLatin1Char('\n'))) {
                        if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                        pullBody += line; pullBody += QLatin1Char('\n');
                    }
                }
                QString settleBody; // tick 落地沿（resting 翻 true 分支）邻域：豁免消费行 → 伤害式行
                const int s0 = t.indexOf(QStringLiteral("e.fallExemptOnce = false"));
                const int s1 = t.indexOf(QStringLiteral("damageEntity(idx, int(std::floor(fallDist"));
                if (s0 >= 0 && s1 > s0) {
                    for (const QString &line : t.mid(s0, s1 - s0).split(QLatin1Char('\n'))) {
                        if (line.trimmed().startsWith(QLatin1String("//"))) continue;
                        settleBody += line; settleBody += QLatin1Char('\n');
                    }
                }
                okE = pullBody.contains(QStringLiteral("e.fallExemptOnce = true"))
                      && settleBody.contains(QStringLiteral("e.fallPeakY = restTopY"))
                      && settleBody.contains(QStringLiteral("mobFeetInWater("))
                      && settleBody.contains(QStringLiteral("!pullExempt && !feetInWater"))
                      && t.contains(QStringLiteral("if (fallFeetNow > e.fallPeakY)"))
                      && t.contains(QStringLiteral("e.fallPeakY = e.pos.y() - e.halfH"))
                      && th.contains(QStringLiteral("kMobFallSafeBlocks = 3.0f"));
            }
        }

        const bool okT970 = okA && okB && okC && okD && okE;
        if (!okT970) ++totalFail;
        if (!okT970)
            qInfo().noquote() << "  [t970 diag] okA" << okA << "peakA" << peakA << "hpA" << hpA
                              << "| okB" << okB << "hpB" << hpB
                              << "| okC" << okC << "hpC" << hpC
                              << "| okD" << okD << "hpD" << hpD
                              << "| okE" << okE;
        qInfo().noquote() << (okT970 ? "PASS" : "FAIL")
                          << "| t970 reeled-mob fall-damage exemption: a generic mob landing-settlement "
                             "chain now exists (peak-feet vs landing-top, dmg = floor(fall - 3), water "
                             "landing cancels, damage via the existing damageEntity hurt chain) and the "
                             "fishing reel stamps a ONE-SHOT exemption consumed unconditionally at the "
                             "next landing edge -- (a) a pig reeled in a ~11-block solved arc lands at "
                             "full HP (peak >= start+6 pins a real arc; unexempted the same arc would "
                             "deal 8), (b) control: an unpulled drop from the same 11-block fall takes "
                             "exactly 8 (10->2 HP), (c) the SAME pig reeled at full 20 HP then dropped "
                             "through a support-dug shaft falls 50->40 and lands at exactly 13 HP "
                             "(=7 damage: zero if the exemption survived the body, 18 if the landing "
                             "had not reset the fall baseline - both faces pinned), (d) a >3-block drop into a "
                             "water well lands unharmed (t200 mirror), (e) source pins on the reel-side "
                             "stamp inside pullMobToward, the landing-edge consume/peak-reset/water/"
                             "damage lines, and the 3.0f threshold constant; reel driven directly "
                             "through pullMobToward (the rod->entry wiring is P-t882(c)/P-t927 pinned) "
                             "so no new hook-sweep RNG enters the baseline";
    }

    // ── P-t1029 wander 冻结测试缝探针（行为级 + 源码钉；t882/t927/t960 三族钓鱼 flaky 治理的缝本体）──
    //    review0907 B-P3-4：三族偶红同源 = 甩钩 / 量测窗内猪 wander RNG（t882 okFar-false / t960 dBMax
    //    擦线 / t927 同族签名）。缝 = EntityManager::setWanderFrozen(bool)（t970「直驱消 RNG」的管理器级
    //    等价物；最小侵入 = 一成员 + 一 setter + aiWander 顶部早退，不动 RNG 类结构）。腿：
    //    (a) 冻结行为：冻结中 2×800 tick（80s，≥16 个 wander 时间片）位置/朝向/腿摆零写入 + 生成列
    //        定身——断言面证「窗内状态不变」；「守卫先于 RNG 抽取 / 全局流未被消费」是实现声明：
    //        (d) 缝钉锚语句存在而非其在 aiWander 内的次序，不能升格为断言结论（R19.21 批次
    //        review B-P3 备案，注释措辞按断言面收敛）；moveSpeed 恒 0（wander 速度写入被跳过）；
    //    (b) 重力保留：冻结中空中生成的第二头猪仍落台静息（静息 Y 与 (a) 同值——restY = 支撑顶 + halfH
    //        单一权威贴面），XZ 定身——冻结 ≠ 悬停（重力 / 击退 / 拉拽物理在 aiWander 之外）；
    //    (c) 解冻恢复：冻结期 wanderTimer 未推进（保持生成初值 0）→ setWanderFrozen(false) 后首个
    //        AI tick 到期重掷 yaw——缝关即恢复原噪声路径（关缝阴性轮的历史签名复现基础）；
    //    (d) 源码钉：aiWander 早退语句本体 + setter 实现 + 头文件声明 / 缺省 false 成员（pinSet 剥注释，
    //        阴轮摘缝即红）。(c) 假红质量：重掷恰回 yaw=0 概率 1/62832，叠加 3s 行走窗 idle 概率
    //        （kIdleChance≈25%/片）→ <5e-6，远低于被治理的原噪声（套件史 13 次 t882/t960 偶红）。
    {
        World wS;
        wS.setWidth(48); wS.setDepth(48); wS.setHeight(96); wS.setSeed(97);
        EntityManager ents;
        ents.setWanderFrozen(true); // 缝存在性：C++ 直调编译期即证；后续全部腿在冻结态跑
        const QVector3D farS(-1000.0f, 10.0f, -1000.0f);
        const auto tickS = [&](int n, float dt) {
            for (int i = 0; i < n; ++i) ents.tick(qreal(dt), &wS, farS, 0.3f, 1.8f, false);
        };
        const int fy = 83;
        for (int x = 15; x <= 17; ++x) // 3×3 石台 + 生成列净空自凿（t933 教训：不赌地形/净空带）
            for (int z = 5; z <= 7; ++z) wS.setBlock(x, fy, z, BR::Stone, 0);
        for (int x = 15; x <= 17; ++x)
            for (int z = 5; z <= 7; ++z)
                for (int y = fy + 1; y <= fy + 12; ++y) wS.setBlock(x, y, z, BR::Air, 0);

        // (a) 冻结行为腿：生成（空中）→ 800 tick settle+冻结窗 → 快照 → 再 800 tick → 逐位不变。
        const int pigA = ents.spawnMobTyped(16, fy + 4, 6, EntityManager::MobPig,
                                            QStringLiteral("#e8a0a0"), 10);
        tickS(800, 0.05f);
        const QVector3D posA0 = ents.posAt(pigA);
        const float yawA0 = ents.yawAt(pigA);
        tickS(800, 0.05f);
        const bool okFrozen = ents.posAt(pigA) == posA0
                           && ents.yawAt(pigA) == yawA0
                           && ents.moveSpeedAt(pigA) == 0.0f
                           && ents.walkPhaseAt(pigA) == 0.0f
                           && posA0.x() == 16.5f && posA0.z() == 6.5f; // 生成列定身（x+0.5 spawn 约定）

        // (b) 重力保留腿：更高空中生成（冻结中）→ 落同一台面同一静息位（restY = 支撑顶 + halfH）。
        const int pigB = ents.spawnMobTyped(17, fy + 8, 6, EntityManager::MobPig,
                                            QStringLiteral("#e8a0a0"), 10);
        tickS(800, 0.05f);
        const QVector3D posB = ents.posAt(pigB);
        const bool okGravity = posB.y() == posA0.y() // 同台面贴面静息（冻结不悬停；落地沿精确贴面）
                            && posB.x() == 17.5f && posB.z() == 6.5f; // 垂直下落不带水平位移

        // (c) 解冻恢复腿：解冻后 wanderTimer(0) 到期重掷 yaw（首个 AI tick；60 tick 缓冲窗覆盖
        //     t500 错峰门），yaw/位置任一破「全等」即证 wander 恢复。
        ents.setWanderFrozen(false);
        tickS(1, 0.05f);
        bool okUnfrozen = ents.yawAt(pigA) != yawA0 || ents.posAt(pigA) != posA0;
        tickS(60, 0.05f);
        okUnfrozen = okUnfrozen || ents.yawAt(pigA) != yawA0 || ents.posAt(pigA) != posA0;

        // (d) 源码钉（缝语句本体；pinSet 剥注释——摘缝 / 改签名即红）。
        const QString exeDirS = QCoreApplication::applicationDirPath();
        QStringList missS = pinSet(
            QDir(exeDirS + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/Entities/entitymanager.cpp")), {
            {"cpp-wander-freeze-guard", "if (m_wanderFrozen) {"},
            {"cpp-wander-freeze-setter", "void EntityManager::setWanderFrozen(bool frozen)"},
        });
        missS << pinSet(
            QDir(exeDirS + QStringLiteral("/..")).absoluteFilePath(
                QStringLiteral("src/Entities/entitymanager.h")), {
            {"hdr-wander-freeze-decl", "Q_INVOKABLE void setWanderFrozen(bool frozen);"},
            {"hdr-wander-freeze-default", "bool m_wanderFrozen = false;"},
        });
        const bool okPinS = missS.isEmpty();
        if (!okPinS)
            qInfo().noquote() << "  [t1029 diag] pin miss:" << missS.join(QLatin1Char(','));

        const bool okT1029 = okFrozen && okGravity && okUnfrozen && okPinS;
        if (!okT1029) ++totalFail;
        if (!okT1029)
            qInfo().noquote() << "  [t1029 diag] okFrozen" << okFrozen << "okGravity" << okGravity
                              << "okUnfrozen" << okUnfrozen << "okPin" << okPinS;
        qInfo().noquote() << (okT1029 ? "PASS" : "FAIL")
                          << "| t1029 wander-freeze test seam: EntityManager::setWanderFrozen(bool) "
                             "short-circuits aiWander before the time-slice countdown / RNG re-roll / "
                             "speed write / displacement (global QRandomGenerator stream untouched, "
                             "moveSpeed zeroed, walk phase idle) while gravity and the knockback / "
                             "reel physics stay live -- the manager-level equivalent of the t970 "
                             "direct-drive RNG purge and the single kill switch for the shared "
                             "t882/t927/t960 wander noise; legs: an 80s frozen window keeps "
                             "position/yaw/walk-phase bit-identical, an air-spawned frozen pig still "
                             "settles onto the platform at the same rest height (freeze != hover), "
                             "unfreeze re-rolls the yaw within one AI tick (seam-off restores the "
                             "legacy noise path), and source pins lock the aiWander guard, the "
                             "setter body, the header declaration and the default-false member";
    }

    // ── P-t929 大峡谷孤立水格探针（worldgen 行为级：多 seed 生成 → 全图孤立水格 == 0）──
    //   用户「大峡谷中间还是会生成单独的水方块然后直接掉落，旁边没有别的支撑方块」——根因 = t376/t601
    //   高源瀑布源：峡心柱悬空一格 Water（下方恒 canyon air、水平四邻恒无水；水源永不蒸发但起 tick 即
    //   泄成孤立下落水柱 = 用户所见）。t929 退役整个置源 pass（连同 t376 (1a) 壁环含水检测死码），峡谷
    //   定版干涸地貌（carve 盘内排干 + 排水带 + 不补新源）。孤立水格判据 = 本格 Water 且下方 Air 且 4
    //   水平邻皆非水（无侧向水体喂养 → 起 tick 必为孤立下落柱）；worldgen 纯函数于 seed（PLAN §2-K）→
    //   同 seed 计数确定，可精确断言 0。多 seed 扫：瀑布门控按概率命中含水壁环，单 seed 可能不触发
    //   （阴性轮依赖其中至少一 seed 在退役前命中）。fresh world 无任何 rig 编辑 → 不与游玩期倒水混淆。
    {
        const int kT929Seeds[] = {1337, 42, 7, 2024, 8888, 555};
        int isoTotal = 0;
        QString perSeedDiag;
        for (int sd : kT929Seeds) {
            World wc;
            wc.setWidth(96);
            wc.setDepth(96);
            wc.setHeight(48);
            wc.setSeed(sd); // 最后设 seed → 一次全尺寸 regenerate（尺寸 setter 各自小尺寸 regen，省时）
            int iso = 0;
            int firstX = -1, firstY = -1, firstZ = -1;
            for (int x = 0; x < wc.width(); ++x) {
                for (int z = 0; z < wc.depth(); ++z) {
                    for (int y = 1; y < wc.height(); ++y) { // y=0 下无格（基岩域）
                        if (wc.blockAt(x, y, z) != quint8(BR::Water)) continue;
                        if (wc.blockAt(x, y - 1, z) != quint8(BR::Air)) continue; // 下方实体/水 → 有支撑
                        bool horiz = false;
                        if ((x > 0 && wc.blockAt(x - 1, y, z) == quint8(BR::Water))
                            || (x + 1 < wc.width() && wc.blockAt(x + 1, y, z) == quint8(BR::Water))
                            || (z > 0 && wc.blockAt(x, y, z - 1) == quint8(BR::Water))
                            || (z + 1 < wc.depth() && wc.blockAt(x, y, z + 1) == quint8(BR::Water))) {
                            horiz = true;
                        }
                        if (horiz) continue;
                        if (iso == 0) { firstX = x; firstY = y; firstZ = z; }
                        ++iso;
                    }
                }
            }
            isoTotal += iso;
            if (iso > 0)
                perSeedDiag += QStringLiteral(" seed%1=%2@(%3,%4,%5)").arg(sd).arg(iso).arg(firstX).arg(firstY).arg(firstZ);
        }
        const bool okT929 = isoTotal == 0;
        if (!okT929) ++totalFail;
        if (!okT929)
            qInfo().noquote() << "  [t929 diag] isolated water cells:" << perSeedDiag;
        qInfo().noquote() << (okT929 ? "PASS" : "FAIL")
                          << "| t929 canyon lone-water removal: the t376/t601 high-source waterfall pass is "
                             "retired wholesale -- it planted a single Water source hanging in the canyon "
                             "center column (solid air below, zero horizontal water neighbors), which on "
                             "the first fluid tick bleeds into a lone falling column exactly matching the "
                             "user report 'a single water block appears mid-canyon and drops with nothing "
                             "supporting it'; canyons are now canonically dry landforms (carve-disc "
                             "drain + kDrainRadius drain band, no new sources placed), and a full-grid "
                             "sweep over six deterministic seeds asserts worldgen emits zero unsupported "
                             "isolated water cells anywhere (water with air below and no horizontal water "
                             "neighbor), so in-world waterfalls are player-made only; wall-ring water "
                             "detection dead code removed with the feature";
    }

    // ── P-t930 沙子悬浮链 26 邻域探针（World 层行为级；t799 既有「直接上方」支线保钉 + 新 ③ 级联四腿）──
    //   用户口径：「破坏其中一个沙子应自动检测周围沙子状态，悬空就掉落；放置一个方块在它一格之内（26
    //   体素检测概念）也更新沙子悬浮状态开始掉落」。旧 checkGravityBlockOnEdit 只查直接上方 + 放置完整
    //   立方早退（编辑格邻域的既有悬空沙永不复检）。修 = ③ cascadeGravityAround：任何编辑（破坏 / 放置）
    //   → BFS 扫 26 邻域失撑重力方块整柱坍落 + 坍落格续扫连锁。悬空沙以 setBlockFromEntity 直写构造
    //   （实体着地入口无 check 钩子——真实「落地后下方被挖」等滞留悬空态的等价孤本，探针可确定性摆出）。
    //   五腿：(a) 破坏对角邻格触发邻域悬空沙坍落；(b) 放置完整立方（斜对角）触发（旧早退路径）；(c) 连锁
    //   传播——初扫不可达（距编辑格 dx=2）的第二悬空沙经第一坍落格续扫带落；(d) 阴性——有支撑沙在邻域
    //   编辑后不掉、放置支撑面救活悬空沙；(e) 破坏沙柱底格全柱坍落（既有②行为回归钉）。
    {
        int fellCount = 0;
        const QMetaObject::Connection fellConn =
            QObject::connect(&w, &World::gravityBlockFell, &w, [&fellCount](int, int, int, int) {
                ++fellCount;
            });

        // (a) 破坏对角邻格 → 邻域悬空沙坍落（旧代码：编辑格非下方支撑 → 不查 → 悬空残留）。
        //     先 setBlock 清空气袋（晚位 slot 列 y=kRigY 可能仍在山体内——setBlock 覆写无守卫、
        //     setBlockFromEntity 只肯写 air/水 → 不清袋则悬空沙构造被静默拒绝）。
        const auto clearPocket = [](World &world, int x0, int y0, int z0, int x1, int y1, int z1) {
            for (int x = x0; x <= x1; ++x)
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        world.setBlock(x, y, z, BR::Air, 0);
        };
        const auto [xa, za] = nextSlot();
        clearPocket(w, xa - 1, kRigY - 1, za - 1, xa + 2, kRigY + 2, za + 2);
        placeRigBlock(w, xa, kRigY, za, BR::Stone, 0);          // 待破对角邻格
        const bool flA = w.setBlockFromEntity(xa + 1, kRigY + 1, za + 1, quint8(BR::Sand)); // 悬空沙
        int fell0 = fellCount;
        w.setBlock(xa, kRigY, za, BR::Air, 0);                   // 破坏 → ③ 26 邻域扫中 (dx=1,dy=1,dz=1)
        const bool okA = flA
                     && w.blockAt(xa + 1, kRigY + 1, za + 1) == quint8(BR::Air)
                     && fellCount == fell0 + 1;

        // (b) 放置完整立方（斜对角）触发（旧代码 isFullCube(id) 早退 → 放置路径零复检）。
        const auto [xb, zb] = nextSlot();
        clearPocket(w, xb - 1, kRigY, zb - 1, xb + 2, kRigY + 3, zb + 2);
        const bool flB = w.setBlockFromEntity(xb, kRigY + 2, zb, quint8(BR::Sand)); // 悬空沙
        fell0 = fellCount;
        w.setBlock(xb + 1, kRigY + 1, zb + 1, BR::Stone, 0);      // 放置完整立方（对角邻）
        const bool okB = flB
                     && w.blockAt(xb, kRigY + 2, zb) == quint8(BR::Air)
                     && fellCount == fell0 + 1;

        // (c) 连锁传播：F1 距编辑格 dx=1（初扫可达），F2 距编辑格 dx=2（初扫不可达，只能经 F1 坍落格续扫）。
        const auto [xc, zc] = nextSlot();
        clearPocket(w, xc - 2, kRigY - 1, zc - 1, xc + 2, kRigY + 2, zc + 2);
        placeRigBlock(w, xc - 1, kRigY, zc, BR::Stone, 0);        // 待破格
        const bool flC1 = w.setBlockFromEntity(xc, kRigY + 1, zc, quint8(BR::Sand));         // F1
        const bool flC2 = w.setBlockFromEntity(xc + 1, kRigY + 1, zc + 1, quint8(BR::Sand)); // F2
        fell0 = fellCount;
        w.setBlock(xc - 1, kRigY, zc, BR::Air, 0);
        const bool okC = flC1 && flC2
                     && w.blockAt(xc, kRigY + 1, zc) == quint8(BR::Air)
                     && w.blockAt(xc + 1, kRigY + 1, zc + 1) == quint8(BR::Air)
                     && fellCount == fell0 + 2;

        // (d) 阴性：有支撑沙在邻域编辑后不掉；放置支撑面「救活」悬空沙（放置不误伤不迟钝）。
        const auto [xd, zd] = nextSlot();
        clearPocket(w, xd - 1, kRigY - 1, zd - 1, xd + 3, kRigY + 4, zd + 2);
        placeRigBlock(w, xd, kRigY, zd, BR::Stone, 0);
        w.setBlock(xd, kRigY + 1, zd, BR::Sand, 0);               // 合法支撑沙（②/③ 均不掉）
        const bool flD = w.setBlockFromEntity(xd + 2, kRigY + 3, zd, quint8(BR::Sand)); // 悬空沙（dy=+2 出 D1 编辑 26 邻域）
        fell0 = fellCount;
        w.setBlock(xd + 1, kRigY + 1, zd + 1, BR::Stone, 0);      // 邻域放置：支撑沙不掉（有支撑）
        const bool okD1 = flD
                     && w.blockAt(xd, kRigY + 1, zd) == quint8(BR::Sand)
                     && w.blockAt(xd + 2, kRigY + 3, zd) == quint8(BR::Sand)
                     && fellCount == fell0;
        w.setBlock(xd + 2, kRigY + 2, zd, BR::Stone, 0);          // 悬空沙正下方补支撑 → 不掉（被救活）
        const bool okD2 = w.blockAt(xd + 2, kRigY + 3, zd) == quint8(BR::Sand) && fellCount == fell0;

        // (e) 破坏沙柱底格 → 全柱坍落（既有 ② 直接上方支线回归钉）。
        const auto [xe, ze] = nextSlot();
        clearPocket(w, xe - 1, kRigY - 1, ze - 1, xe + 1, kRigY + 4, ze + 1);
        placeRigBlock(w, xe, kRigY, ze, BR::Stone, 0);
        w.setBlock(xe, kRigY + 1, ze, BR::Sand, 0);
        w.setBlock(xe, kRigY + 2, ze, BR::Sand, 0);
        w.setBlock(xe, kRigY + 3, ze, BR::Sand, 0);
        fell0 = fellCount;
        w.setBlock(xe, kRigY + 1, ze, BR::Air, 0);                // 破底格 → 上方 3 格整柱坍落
        const bool okE = w.blockAt(xe, kRigY + 2, ze) == quint8(BR::Air)
                     && w.blockAt(xe, kRigY + 3, ze) == quint8(BR::Air)
                     && fellCount == fell0 + 2;

        QObject::disconnect(fellConn);
        const bool okT930 = okA && okB && okC && okD1 && okD2 && okE;
        if (!okT930) ++totalFail;
        if (!okT930)
            qInfo().noquote() << "  [t930 diag] a" << okA << "b" << okB << "c" << okC
                              << "d1" << okD1 << "d2" << okD2 << "e" << okE
                              << "| flA" << flA << "flB" << flB << "flC" << flC1 << flC2
                              << "flD" << flD
                              << "| a.cell" << int(w.blockAt(xa + 1, kRigY + 1, za + 1))
                              << "b.cell" << int(w.blockAt(xb, kRigY + 2, zb))
                              << "c.f1" << int(w.blockAt(xc, kRigY + 1, zc))
                              << "c.f2" << int(w.blockAt(xc + 1, kRigY + 1, zc + 1))
                              << "d.f" << int(w.blockAt(xd + 2, kRigY + 3, zd));
        qInfo().noquote() << (okT930 ? "PASS" : "FAIL")
                          << "| t930 sand 26-neighborhood gravity cascade: any edit (break OR place, "
                             "including placing a full cube which used to early-return untouched) now "
                             "BFS-scans the 26-voxel neighborhood for unsupported gravity blocks "
                             "(same predicate as placement self-check: below not a full cube) and drops "
                             "them as whole columns, with cleared cells re-queued so the collapse "
                             "propagates outward (a floater two cells from the edit falls via the first "
                             "dropped column's rescan); supported sand near an edit stays put and "
                             "placing a support under a floater rescues it (no false drops, no missed "
                             "drops); breaking the bottom of a sand column still cascades the whole "
                             "column (t799 direct-above branch kept); floaters staged via "
                             "setBlockFromEntity (the entity-landing write path that carries no edit "
                             "hooks -- equivalent of landed-then-undermined stale sand, "
                             "deterministically constructible in the rig)"
                             ;
    }

    // ── P-t931 满耐久不显耐久条源码钉（纯 UI 修；t902/t857 源码文本钉先例——QML 无 static_assert 面）──
    //   用户：「刚做好的工具耐久度是满的，在背包界面就不要显示耐久条了；1~9 物品栏的显示是正确的，只有
    //   破坏了之后才会看到耐久度并且一直显示」。根因 = DurabilityBar 组件 visible 只判 `maxDur>0 && curDur>0`
    //   （t498 曾定「背包常显」口径）→ 满耐久显满绿条，与 HUD hotbar（t315/t349 `curDur < maxDur` 满耐久隐）
    //   两套口径。t931 用户口径翻案 t498：背包 / 装备槽全部对齐 hotbar——满耐久无条、受损后显且持续。
    //   钉三面：① DurabilityBar.qml visible 含 `curDur < maxDur`（组件统一判——Inventory 生存 tab 主栏 /
    //   hotbar / 装备槽 + SurvivalInventory 主栏 / hotbar 全走本组件）；② SurvivalInventory 内联 armorDurBar
    //   （唯二不迁移组件的槽）同判 + 护甲耐久数字 Text 同口径（满耐久无数字）；③ Main.qml HUD hotbar
    //   参照实现保持不变（`durabilityBar.curDur < durabilityBar.maxDur` 正锚）。
    {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        auto readSrc = [&root](const QString &rel) -> QString {
            QFile f(root + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString db = readSrc(QStringLiteral("src/ui/DurabilityBar.qml"));
        const QString sv = readSrc(QStringLiteral("src/ui/SurvivalInventory.qml"));
        const QString mn = readSrc(QStringLiteral("src/ui/Main.qml"));
        const bool okBar = db.contains(QStringLiteral(
            "visible: maxDur > 0 && curDur > 0 && curDur < maxDur"));
        const bool okArmor = sv.contains(QStringLiteral(
            "visible: armId !== 0 && maxDur > 0 && curDur > 0 && curDur < maxDur"));
        const bool okArmorNum = sv.contains(QStringLiteral(
            "root.hotbar.armorDurabilityAt(index) < root.hotbar.armorMaxDurability(armId)"));
        const bool okHotbar = mn.contains(QStringLiteral(
            "durabilityBar.curDur < durabilityBar.maxDur")); // 参照实现不变（正锚）
        const bool okT931 = okBar && okArmor && okArmorNum && okHotbar;
        if (!okT931) ++totalFail;
        if (!okT931)
            qInfo().noquote() << "  [t931 diag] bar" << okBar << "armor" << okArmor
                              << "armorNum" << okArmorNum << "hotbar" << okHotbar;
        qInfo().noquote() << (okT931 ? "PASS" : "FAIL")
                          << "| t931 full-durability hides the bar in inventory panels: DurabilityBar's "
                             "visible gains curDur < maxDur (the t498 'always show in inventory' caliber "
                             "is overruled by the user -- a freshly crafted tool or new armor shows NO "
                             "bar/number in inventory/chest-adjacent panels until first damage, then it "
                             "stays visible), which unifies every panel (Inventory survival-tab main/"
                             "hotbar/armor slots + SurvivalInventory main/hotbar all route through the "
                             "component) with the HUD hotbar reference (t315/t349, pinned unchanged as "
                             "the positive anchor); the two non-migrated spots are pinned too -- the "
                             "inline armorDurBar gets the same comparison and the armor durability "
                             "number text hides at full durability (visible only while "
                             "armorDurabilityAt < armorMaxDurability); pure UI change pinned at source "
                             "level per the t902 precedent"
                             ;
    }

    // ── P-t933 跨世界卡顿泄漏定位探针（TNT 炸沙坑 8FPS → kill @e 无效 → 换世界仍卡 → 重启恢复）──
    //   三腿：
    //   (a) 风暴量化 + 批量收口经济性：t930 级联把「逐格 recomputeLightAround（每次 = ±15×到顶全盒重
    //       flood）+ 每柱 1 次 worldChanged QML 扇出」重新引入爆炸链（t320 只批了爆炸本体）→ 沙坑一次
    //       爆炸 = 数百次全盒重 flood。修 = dropGravityColumn 柱末一次 refloodBox + cascadeGravityAround
    //       级联末一次（联合盒）。钉：128 格悬空沙经一次编辑触发全坍落，光照重 flood 总数 ≤ 4（旧逐格版
    //       ≥ 129 → 必 FAIL）+ worldChanged ≤ 3（旧逐柱版 = 1 + 柱数）。行为等价钉：坍落后原悬空格见天
    //       skyLight==15、遮挡格 < 15（批量联合盒 reflood 终态 == 逐格重 flood 终态）。
    //   (b) 同世界收敛：爆炸 + 级联后跑全部世界 tick（水/岩浆/火/生长/冰/叶衰/天气/红石）——稳态窗口内
    //       worldChanged / gravityBlockFell / blockBroken / 光照重 flood 全零。若非零 = 存在**不收敛**的
    //       重算循环（用户「光照一直重建」怀疑的判据）。
    //   (c) 跨世界残留清零：走真实换世界两路径 —— regenerate（新世界 worldgen）与 beginLoad+finishLoad
    //       （读档），之后同款 tick 电池全零。非零 = 进程级 / World 级状态跨世界存活（泄漏类）。
    //       C++ World 层审计结论（beginLoad/generate 清 m_growthCells/m_waterCells/m_lavaCells/m_iceCells/
    //       m_fireCells/m_burningCells/m_torchBurnout/m_powerDirty/m_decayingLeaves/m_biomeCache/活动盒/
    //       t933 联合盒）由本腿行为级钉死。
    //   独立世界（t759 要塞净空先例）：96×96×64 —— 本工程海平面 kWaterLevel=58，高 64 世界的水只到 58，
    //       y≥59 为干燥空气带（沙坑弹坑不进水 → 电池零写入断言不被「水流入弹坑」合法瞬态污染）。
    //       ⚠ 地形 / 山体可达世界顶（seed 1337 的 (8..16) 区山体实测到顶）→ rig 位**程序化搜索**干燥
    //       空气盒（blockAt 全 Air 判定），不假定固定坐标为空。worldgen 水（海 / 湖邻洞穴）首 tick 起
    //       有合法有限沉降 → 各断言窗前先跑**沉降循环**（tickWaterFlow 推进到连续无写入），把「自然
    //       瞬态」与「泄漏残留」分离——沉降收敛本身也是 (b)/(c)「有限收敛」前提的一部分。
    {
        World w933;
        w933.setWidth(96);
        w933.setDepth(96);
        w933.setHeight(64); // 3 次 setter 各 regenerate 一次；水至 58，y≥59 干燥带（山体除外 → 找位）
        // 事件计数器（等价 Main.qml 消费端；同步直连）。
        int wc933 = 0, fell933 = 0, broke933 = 0, dropped933 = 0;
        QObject::connect(&w933, &World::worldChanged, &w933, [&wc933]() { ++wc933; });
        QObject::connect(&w933, &World::gravityBlockFell, &w933,
                         [&fell933](int, int, int, int) { ++fell933; });
        QObject::connect(&w933, &World::blockBroken, &w933,
                         [&broke933](int, int, int, int) { ++broke933; });
        QObject::connect(&w933, &World::blockDroppedAsItem, &w933,
                         [&dropped933](int, int, int, int) { ++dropped933; });
        // 世界 tick 系统（等价 Main.qml 的 WorldClock.ticked 桥接面；逐系统分立 → 断言窗可按系统归因）。
        constexpr int kSysN933 = 13;
        const char *const kSysName933[kSysN933] = { "rs", "wat", "lav", "fire", "crop", "sug",
                                                    "farm", "sap", "berry", "frz", "melt", "leaf", "wthr" };
        const auto tickSys933 = [&w933](int i) {
            switch (i) {
            case 0: w933.tickRedstone(); break;
            case 1: w933.tickWaterFlow(); break;
            case 2: w933.tickLavaFlow(); break;
            case 3: w933.tickFire(); break;
            case 4: w933.tickCropGrowth(); break;
            case 5: w933.tickSugarcaneGrowth(); break;
            case 6: w933.tickFarmlandHydration(); break;
            case 7: w933.tickSaplingGrowth(); break;
            case 8: w933.tickSweetBerryBushGrowth(); break;
            case 9: w933.tickIceFreeze(); break;
            case 10: w933.tickIceMelt(); break;
            case 11: w933.tickLeafDecay(); break;
            default: w933.tickWeather(0.1); break;
            }
        };
        // worldgen 水沉降循环：推进 tickWaterFlow 直到连续 kQuiet 个推进零写入（世界自然流场有限收敛；
        //      cap 防病态挂死）。岩浆步进 3s/格太慢且 worldgen 岩浆湖恒封闭稳定 → 不沉降（断言窗内
        //      interval=30 不推进）。
        const auto settleWater933 = [&w933, &wc933]() -> bool {
            int quiet = 0;
            int iter = 0;
            for (; iter < 4000 && quiet < 12; ++iter) {
                const int wc0 = wc933;
                w933.tickWaterFlow();
                w933.tickWaterFlow();
                w933.tickWaterFlow(); // 3 tick = 1 波前推进（kFlowTickInterval=3）
                quiet = (wc933 == wc0) ? quiet + 1 : 0;
            }
            return quiet >= 12; // true = 已收敛（cap 4000 推进仍未收敛 = 病态世界，探针响亮报 FAIL）
        };
        // 干燥盒搜索（区域 B 用）：bx×bz 足迹在 y∈[y0,y1] 无 Water/Lava（山体实岩 / 空气皆可——
        //      placeRigBlock 覆写放置，弹坑只要不邻流体即稳态）。step 2 扫描；找不到 → qFatal。
        const auto findDryBox933 = [&w933](int bx, int bz, int y0, int y1) {
            for (int z = 2; z + bz - 1 < 96; z += 2)
                for (int x = 2; x + bx - 1 < 96; x += 2) {
                    bool ok = true;
                    for (int dz = 0; dz < bz && ok; dz += 1)
                        for (int dx = 0; dx < bx && ok; dx += 1)
                            for (int y = y0; y <= y1 && ok; y += 1) {
                                const quint8 id = w933.blockAt(x + dx, y, z + dz);
                                if (id == quint8(BR::Water) || id == quint8(BR::Lava)) ok = false;
                            }
                    if (ok) return QPair<int, int>(x, z);
                }
            qFatal("t933 rig: no dry box %dx%d y%d..%d found in 96x96x64 world", bx, bz, y0, y1);
            return QPair<int, int>(-1, -1); // qFatal noreturn（防 -Wreturn-type）
        };
        // 纯空气盒搜索（区域 A 用：悬空沙 staging 须目标格空气 + 光照断言须见天到顶）。尺寸回退
        //      （山体种子大平空气带可能不存在 → 逐级缩盒），返回 {x, z, size}。避开 excl 足迹区。
        const auto findAirBox933 = [&w933](int bxMax, int y0, int y1,
                                           int exclX0, int exclZ0, int exclX1, int exclZ1) {
            for (int bx = bxMax; bx >= 8; bx -= 2)
                for (int z = 2; z + bx - 1 < 96; z += 2)
                    for (int x = 2; x + bx - 1 < 96; x += 2) {
                        if (x + bx - 1 >= exclX0 && x <= exclX1 && z + bx - 1 >= exclZ0 && z <= exclZ1)
                            continue; // 与既有 rig 区重叠 → 跳过该候选
                        bool ok = true;
                        for (int dz = 0; dz < bx && ok; dz += 1)
                            for (int dx = 0; dx < bx && ok; dx += 1)
                                for (int y = y0; y <= y1 && ok; y += 1)
                                    if (w933.blockAt(x + dx, y, z + dz) != quint8(BR::Air)) ok = false;
                        if (ok) return QVector3D(float(x), float(z), float(bx));
                    }
            qFatal("t933 rig: no air box >=8x8 y%d..%d found in 96x96x64 world", y0, y1);
            return QVector3D(-1, -1, -1); // qFatal noreturn（防 -Wreturn-type）
        };

        // ── rig 找位 + 水沉降（断言前提：世界自然流场已收敛）──
        // 区域 B（大，先找）：29×29 足迹 y∈[59,62]（石板 y=59 + 沙丘 3 层）。区域 A（小，后找，避开 B）：
        // 12×12 足迹 y∈[59,62]（悬空沙 8×8×2 + 触发邻格）。
        // ⚠ 顺序契约：**先沉降后找位**——初始 worldgen 含未稳水系（泉 / 悬水 / 湖洞相交），首轮流
        //   扫（setter 的 generate 置 dirty + 空盒 → 全量兜底）会把水重排到新位置；若找位在沉降前，
        //   「干燥」判定量的是**沉降前**的栅格，rig 可能恰好压在沉降后才出现的泉眼 / 瀑布上（平台堵
        //   住水柱 → 电池窗内合法排水 / 重流写入污染零断言——实测如此）。沉降收敛后再找位 = 干燥判定
        //   对稳态世界成立。
        const bool settledInit933 = settleWater933();
        const QPair<int, int> boxB = findDryBox933(29, 29, 56, 63); // 弹坑 ± 活动盒无流体即可（实岩可）
        const QVector3D boxA3 = findAirBox933(12, 59, 63,
                                              boxB.first - 2, boxB.second - 2,
                                              boxB.first + 29, boxB.second + 29);
        const int aSize933 = int(boxA3.z()); // 空气盒边长（8..12 回退结果）

        // ── (a) 悬空沙 128 格一次编辑全坍落：reflood / worldChanged 经济性 + 光照终态等价 ──
        // 区域 A：boxA 内 8×8 高 60..61 悬空沙（setBlockFromEntity 实体着地入口无编辑钩子 → 静默滞留
        //   悬空态，t930 探针同款确定性构造法；找位保证目标格恒空气 → staging 必成）。
        //   触发 = 在邻格放一个完整立方（t930 (b) 放置路径）→ ③ 级联 BFS 带落全部。
        const int ax0 = int(boxA3.x()), az0 = int(boxA3.y());
        const int ae933 = aSize933 - 2; // 内缩 1 圈（触发格留在盒角外圈邻位）
        bool stagedAll = true;
        int stagedN933 = 0;
        for (int x = ax0 + 1; x <= ax0 + ae933; ++x)
            for (int z = az0 + 1; z <= az0 + ae933; ++z)
                for (int y = 60; y <= 61; ++y) {
                    stagedAll = w933.setBlockFromEntity(x, y, z, quint8(BR::Sand)) && stagedAll;
                    ++stagedN933;
                }
        const qint64 rf0 = FrameProfiler::instance()->countValue("refloodN");
        const int wc0 = wc933, fell0 = fell933;
        w933.setBlock(ax0, 60, az0, BR::Stone, 0); // 邻格放置完整立方 → ③ 26 邻域级联触发（t930 (b) 路径）
        const qint64 rfTrig = FrameProfiler::instance()->countValue("refloodN") - rf0;
        const int wcTrig = wc933 - wc0, fellTrig = fell933 - fell0;
        bool goneAll = true;
        for (int x = ax0 + 1; x <= ax0 + ae933; ++x)
            for (int z = az0 + 1; z <= az0 + ae933; ++z)
                for (int y = 60; y <= 61; ++y)
                    goneAll = goneAll && w933.blockAt(x, y, z) == quint8(BR::Air);
        // 光照终态等价：原悬空沙格现为露天空气 → 天光 15（批量联合盒 reflood 必须复出与逐格重 flood
        //   相同的见天列；若联合盒范围算错（漏 ±15 / 漏到顶）此处留下暗格）。对照：触发放的 Stone
        //   遮挡其正下方格 → 该格天光 < 15（遮光语义仍在）。
        const int skyOpen = int(w933.skyLightAt(ax0 + aSize933 / 2, 60, az0 + aSize933 / 2));
        const int skyShaded = int(w933.skyLightAt(ax0, 59, az0));
        const bool okA = stagedAll && goneAll && fellTrig == stagedN933 && rfTrig <= 4 && wcTrig <= 3
                         && skyOpen == 15 && skyShaded < 15;

        // ── (b)+(c) 沙坑爆炸风暴 → 同世界收敛 → 换世界（regenerate / beginLoad+finishLoad）残留清零 ──
        // 区域 B：石板 y=59（29×29 足迹），沙丘 y=60..62（中心 15×15=675 格，逐格 setBlock 合法支撑
        //   放置）；爆炸球心 = 沙丘中心 (bx0+14, 60, bz0+14) r=2.5 —— 球底掏穿石板 → 上方沙柱失撑
        //   级联（用户沙坑场景的最小复现）。红石粉 / 铁轨 / 火把放远离弹坑的板角（爆炸后 m_powerDirty /
        //   级联 recheck 路径有真实载荷）。
        const int bx0 = boxB.first, bz0 = boxB.second;
        for (int x = bx0; x <= bx0 + 28; ++x)
            for (int z = bz0; z <= bz0 + 28; ++z)
                placeRigBlock(w933, x, 59, z, BR::Stone, 0);
        for (int x = bx0 + 7; x <= bx0 + 21; ++x)
            for (int z = bz0 + 7; z <= bz0 + 21; ++z)
                for (int y = 60; y <= 62; ++y)
                    placeRigBlock(w933, x, y, z, BR::Sand, 0);
        placeRigBlock(w933, bx0 + 1, 60, bz0 + 1, BR::RedstoneDust, 0); // 电力族（编辑入 m_powerDirty 载荷）
        placeRigBlock(w933, bx0 + 2, 60, bz0 + 1, BR::Rail, 0);         // 铁轨（级联 recheck 路径载荷）
        placeRigBlock(w933, bx0 + 3, 60, bz0 + 1, BR::Torch, 0);        // 火把（光源 + 附着物复检载荷）
        const qint64 rfB0 = FrameProfiler::instance()->countValue("refloodN");
        const int wcB0 = wc933, fellB0 = fell933, brokeB0 = broke933, dropB0 = dropped933;
        const auto dv = w933.destroySphereSilent(bx0 + 14, 60, bz0 + 14, 2.5f); // TNT 陆地爆炸的 World 层本体
        const int explDestroyed = int(dv.size());
        const int fellExpl = fell933 - fellB0;
        const int wcExpl = wc933 - wcB0;
        const int brokeExpl = broke933 - brokeB0, dropExpl = dropped933 - dropB0;
        const qint64 rfExpl = FrameProfiler::instance()->countValue("refloodN") - rfB0;
        // 稳态电池：逐系统 5 tick × 2 窗（预热 + 断言）；每系统独立计 wc → 归因。5 次 < 各系最短
        //   概率窗距（结冰 50 / 冰融 20 —— 全程 6 窗 × 5 = 30 次超过 20 → 冰融窗会触发；但 worldgen
        //   冰的融化候选 = 邻发光源冰（无）→ 零写入，diag 按 melt 位归因核对）。
        int sysSame933[kSysN933] = { 0 };
        for (int pass = 0; pass < 2; ++pass) {
            for (int s = 0; s < kSysN933; ++s) {
                const int w0 = wc933;
                for (int i = 0; i < 5; ++i) tickSys933(s);
                if (pass == 1) sysSame933[s] = wc933 - w0; // 断言窗才记录（预热窗的一次性收尾不算）
            }
        }
        const qint64 rfS0 = FrameProfiler::instance()->countValue("refloodN");
        for (int s = 0; s < kSysN933; ++s)
            for (int i = 0; i < 5; ++i) tickSys933(s);
        const qint64 rfSame = FrameProfiler::instance()->countValue("refloodN") - rfS0;
        int wcSameTotal = 0, fellSame = fell933, brokeSame = broke933, dropSame = dropped933;
        for (int s = 0; s < kSysN933; ++s) wcSameTotal += sysSame933[s];
        // fell/broke/drop 的稳态增量在电池后再取一次差分（上面记录的是电池前值）
        const int fellS1 = fell933 - fellSame, brokeS1 = broke933 - brokeSame, dropS1 = dropped933 - dropSame;
        const bool okSame = wcSameTotal == 0 && fellS1 == 0 && brokeS1 == 0 && dropS1 == 0 && rfSame == 0;
        // 换世界路径 1：regenerate（「新建世界」）→ 沉降 → 同款电池全零。
        w933.regenerate(4242);
        const int wcRegen = wc933; // 累计值（diag 用；regenerate 自身 emit 不在断言窗内）
        const bool settledRegen933 = settleWater933();
        int sysRegen933[kSysN933] = { 0 };
        for (int pass = 0; pass < 2; ++pass) {
            for (int s = 0; s < kSysN933; ++s) {
                const int w0 = wc933;
                for (int i = 0; i < 5; ++i) tickSys933(s);
                if (pass == 1) sysRegen933[s] = wc933 - w0;
            }
        }
        int wcRegenTotal = 0;
        for (int s = 0; s < kSysN933; ++s) wcRegenTotal += sysRegen933[s];
        const bool okRegen = wcRegenTotal == 0 && (fell933 - fellS1 - fellSame) == 0;
        // 换世界路径 2：beginLoad + finishLoad（「读档」）→ 沉降（空世界即刻稳）→ 同款电池全零。
        w933.beginLoad(777);
        w933.finishLoad();
        const bool settledLoad933 = settleWater933();
        int sysLoad933[kSysN933] = { 0 };
        for (int pass = 0; pass < 2; ++pass) {
            for (int s = 0; s < kSysN933; ++s) {
                const int w0 = wc933;
                for (int i = 0; i < 5; ++i) tickSys933(s);
                if (pass == 1) sysLoad933[s] = wc933 - w0;
            }
        }
        int wcLoadTotal = 0;
        for (int s = 0; s < kSysN933; ++s) wcLoadTotal += sysLoad933[s];
        const bool okLoad = wcLoadTotal == 0;
        const bool okT933 = okA && okSame && okRegen && okLoad && settledInit933 && settledRegen933
                            && settledLoad933
                            && explDestroyed > 0 && fellExpl > 0 && wcExpl > 0 && rfExpl > 0;
        if (!okT933) ++totalFail;
        if (!okT933) {
            auto sysStr = [&](const int *v) {
                QString s;
                for (int i = 0; i < kSysN933; ++i)
                    if (v[i] != 0) s += QString(" %1:%2").arg(QLatin1String(kSysName933[i])).arg(v[i]);
                return s.isEmpty() ? QString(" all0") : s;
            };
            qInfo().noquote() << "  [t933 diag] a" << okA << "(staged" << stagedAll << "gone" << goneAll
                              << "fell" << fellTrig << "of" << stagedN933 << "rf" << rfTrig << "wc" << wcTrig
                              << "sky" << skyOpen << "shaded" << skyShaded << ")"
                              << "| same" << okSame << "(" << sysStr(sysSame933)
                              << "dfell" << fellS1 << "dbroke" << brokeS1 << "drf" << rfSame << ")"
                              << "| regen" << okRegen << "(" << sysStr(sysRegen933) << ")"
                              << "| load" << okLoad << "(" << sysStr(sysLoad933) << ")"
                              << "| expl(destroyed" << explDestroyed << "fell" << fellExpl
                              << "wc" << wcExpl << "broke" << brokeExpl << "drop" << dropExpl
                              << "rf" << rfExpl << ")"
                              << "| settled" << settledInit933 << settledRegen933 << settledLoad933
                              << "| rigA" << ax0 << az0 << "s" << aSize933 << "rigB" << bx0 << bz0
                              << "wcRegen" << wcRegen;
        }
        qInfo().noquote() << (okT933 ? "PASS" : "FAIL")
                          << "| t933 cross-world lag leak localization: the TNT-on-sand 8FPS storm is "
                             "the t930 gravity cascade re-introducing per-CELL recomputeLightAround "
                             "(each a +/-15-to-sky-top two-channel reflood plus a qInfo disk flush) "
                             "and per-COLUMN worldChanged QML fanout into the explosion chain that "
                             "t320 had batched -- dropGravityColumn now does ONE refloodBox per column "
                             "and cascadeGravityAround collapses the whole BFS into ONE union-box "
                             "reflood + ONE worldChanged (equivalence: every cell's light influence "
                             "is a subset of its +/-15 box, all boxes subset the union, boundary-seed "
                             "reflood of the union equals the per-cell terminal state, pinned by "
                             "skyLight==15 on a former floater cell and <15 under the placed shade "
                             "block); probe legs: (a) 128 staged floaters collapse via a single edit "
                             "with <=4 refloods and <=3 worldChanged (per-cell code would need >=129), "
                             "(b) after the explosion + cascade a full world tick battery "
                             "(water/lava/fire/growth/ice/leaf/weather/redstone) produces ZERO "
                             "worldChanged/blockBroken/light-reflood in steady state -- no "
                             "non-converging recompute loop, killing the 'lighting keeps rebuilding' "
                             "hypothesis at the World layer, (c) the same battery stays zero after "
                             "both real world-exit paths (regenerate worldgen AND beginLoad+"
                             "finishLoad save-load) -- no process-level World state survives a "
                             "world switch (index sets, side tables, dirty flags, activity boxes, "
                             "and the new gravity-light union box are all cleared), so the residual "
                             "cross-world cost the user measured lives in the QML scene layer "
                             "(slot high-water delegate fanout = t935; render-side waitSync = t934, "
                             "now observable via the F3 'act ct' line)"
                             ;
    }

    // ── P-t972 载入世界空白区探针（R19.17 🅶 杂项组；行为级 ChunkGeometry 直驱，t860 先例）──
    //   用户第五轮口径：进世界看到大片空白透过去（疑似回到原点计算/区块未请求），走近挖/放才刷新。
    //   根因 = t470/t472 的可见性把 Model.visible 链在 chunkInRange（r=3 重建窗口）上：窗口外 chunk
    //   即使已有 mesh 也被强制隐藏（有限 160×160 世界中心只见 49/100、角落仅 16/100），且载入只重建
    //   窗口内段——窗外区要么永久留白、要么靠「走近跨界 catch-up / 编辑 worldChanged」才点状出现。
    //   修复：①各段 Model.visible 只由 vertexCount>0 决定（有限世界全幅渲染，机制对标 MC 1.0 有限
    //   地图；t470 实测绘制剔除零 FPS 收益）；②世界换代后呈现层对窗外段 clearMesh 作废旧残 mesh +
    //   近→远入渐进同步队列（meshSyncTimer 每帧限量 refreshMesh，复用与编辑同一条 buildMesh(Dirty)
    //   链）→ 进入世界秒级填满；③窗外段错过的内容重建 / 光照重烘记欠账（deferredRebuildPending /
    //   lightStale），稳态低频排空保远处可见地形最终一致。本探针在局部 2×2 chunk 世界直驱三新入口
    //   钉行为契约 + Main.qml 源码钉编排链（kickWorldMeshSync 挂载 / 可见性解链 / 排空泵）。
    {
        World wl972;
        wl972.setWidth(32);
        wl972.setDepth(32);
        wl972.setHeight(128);
        wl972.setSeed(20250831); // 旧世界（worldgen 全量地形，几何首建基线来源）
        ChunkGeometry ga972, gb972; // ga=chunk(0,0) 窗内 / gb=chunk(1,1) 窗外（窗口 r=0 @ 玩家 chunk(0,0)）
        ga972.setWorld(&wl972);
        ga972.setCx(0);
        ga972.setCz(0);
        gb972.setWorld(&wl972);
        gb972.setCx(1);
        gb972.setCz(1);
        const int oldCntA = ga972.vertexCount(); // 旧世界全量地形 mesh（generate 首建）
        const int oldCntB = gb972.vertexCount();
        gb972.setChunkInRange(false); // true→false：不重建（t472 语义），gb 停持旧世界 mesh
        const bool okBaseline = oldCntA > 0 && oldCntB > 0;

        // 「载入存档」等价流（同 WorldStore.loadChunks：blob 直写 chunk，不经 World 写入路径）：
        //   beginLoad（零填充+全脏）→ 直写两根石柱 → finishLoad（重算 heightmap + 全脏 + worldChanged
        //   + clearAllDirty）。窗内段 a 当帧重建为载入数据（brief 验收「玩家所格视距内空 mesh 计数=0」
        //   的载入腿）；窗外段 b 被窗口门控跳过 → 记内容欠账（呈现层排空依据）。
        wl972.beginLoad(20250831);
        if (Chunk *ca = wl972.chunks().chunk(0, 0))
            for (int y = 0; y <= 40; ++y) ca->setBlock(8, y, 8, BR::Stone);
        if (Chunk *cb = wl972.chunks().chunk(1, 1))
            for (int y = 0; y <= 38; ++y) cb->setBlock(4, y, 4, BR::Stone);
        wl972.finishLoad();
        const bool okWinLoaded = ga972.vertexCount() > 0 && ga972.vertexCount() != oldCntA;
        const bool okLoadDebt = gb972.deferredRebuildPending(); // 载入变更窗外未建 → 欠账已记

        // 渐进同步契约（clearMesh 作废陈旧 → refreshMesh 按当前世界数据重建，同一条 buildMesh 链）：
        //   窗外段先仍持旧世界 mesh（未重建），clearMesh 后归零（visible 绑定自动隐 = 防陈旧错景），
        //   refreshMesh 后非空且**顶点数随新世界数据变**（fresh = 按载入数据建，非旧 mesh 保留）。
        const bool okStaleKept = gb972.vertexCount() == oldCntB;
        gb972.clearMesh();
        const bool okCleared = gb972.vertexCount() == 0;
        gb972.refreshMesh();
        const bool okRefilled = gb972.vertexCount() > 0 && gb972.vertexCount() != oldCntB;
        const bool okDebtClearedByBuild = !gb972.deferredRebuildPending();

        // 稳态欠账腿①内容：窗外编辑（载入后世界是空场，(29,41,29) 恒空气；圆石非重力族无级联）被
        //   onWorldChanged 窗口门控跳过（dirty 随 clearAllDirty 清）→ 欠账当场记账，refreshMesh 排空。
        wl972.setBlock(29, 41, 29, BR::Cobble, 0);
        const bool okEditDebt = gb972.deferredRebuildPending();
        gb972.refreshMesh();
        const bool okEditDrained = !gb972.deferredRebuildPending();

        // 稳态欠账腿②光照：窗外段 setDayMul 一步跨过 kDayMulThresh=0.03 重烘门 → lightStale 记账
        //   （t972 起窗外可见，不排空则夜晚远处仍显上烘正午亮度），refreshMesh 排空后双清。
        gb972.setDayMul(0.0f);
        const bool okLightDebt = gb972.lightStale();
        gb972.refreshMesh();
        const bool okLightDrained = !gb972.lightStale();

        // review0901 #34② 计数腿（重建面微优化，行为级直驱 + meshRebuilt 计数）：稳态欠账条目在队
        //   等待期间被 setChunkInRange(false→true) catch-up 重建过（玩家走近）→ 排空端复查谓词
        //   （vertexCount>0 && !deferredRebuildPending && !lightStale）命中 → 跳过 refreshMesh =
        //   免一次纯浪费的重复重建（计数持平）；对照：进世界全量条目（clearMesh 后 vertexCount==0）
        //   与稳态欠账条目（标记在）谓词不命中 → 必须重建（计数 +1）。谓词同式钉在 Main.qml 排空泵。
        int rebuilds972 = 0;
        bool okDedup972 = false;
        {
            const QMetaObject::Connection cntConn972 =
                    QObject::connect(&gb972, &ChunkGeometry::meshRebuilt,
                                     [&rebuilds972]() { ++rebuilds972; });
            const int baseCnt972 = rebuilds972;
            // 场景复现：条目已入稳态欠账队列（窗外编辑记账）→ 玩家走近 catch-up 重建（计数 +1、欠账双清）。
            gb972.setChunkInRange(false);
            wl972.setBlock(29, 42, 29, BR::Stone, 0);      // 远处编辑 → 排空队列挂着的内容欠账
            const bool debtWhileQueued = gb972.deferredRebuildPending();
            gb972.setChunkInRange(true);                    // 玩家走近 → catch-up buildMesh（无条件，t472）
            const int afterCatchup = rebuilds972;
            const bool catchupCleaned = debtWhileQueued && afterCatchup == baseCnt972 + 1
                                        && !gb972.deferredRebuildPending() && gb972.vertexCount() > 0;
            // 排空端谓词（Main.qml 排空循环同式）命中 → 跳过 = 零重复重建（避免计数腿）。
            const bool skipDue972 = gb972.vertexCount() > 0
                                    && !gb972.deferredRebuildPending() && !gb972.lightStale();
            if (!skipDue972) gb972.refreshMesh();
            const bool okAvoidRebuild = catchupCleaned && skipDue972
                                        && rebuilds972 == afterCatchup;
            // 对照①全量条目：clearMesh 后 vertexCount==0 → 谓词不命中 → 必须重建（进世界队列语义不变）。
            //   注：clearMesh 也发 meshRebuilt（F3 顶点汇总归零通知）→ 计数取「刷新前后 delta」口径，
            //   clearMesh 自身 +1 不参与断言（绝对计数会把归零通知误记成一次重建）。
            gb972.setChunkInRange(false);                   // 真→假不重建（本腿不依赖 catch-up）
            gb972.clearMesh();
            const bool skipAfterClear = gb972.vertexCount() > 0
                                        && !gb972.deferredRebuildPending() && !gb972.lightStale();
            const int preBootRefresh = rebuilds972;
            if (!skipAfterClear) gb972.refreshMesh();
            const bool okBootstrapRebuilds = !skipAfterClear && rebuilds972 == preBootRefresh + 1
                                             && gb972.vertexCount() > 0;
            // 对照②稳态欠账条目：窗外跨阈值 dayMul → lightStale 记账（远端 setter 不直建）→ 谓词
            //   不命中 → 必须重建（稳态排空语义不变）。
            gb972.setDayMul(1.0f);
            const bool debt2Booked = gb972.lightStale();
            const bool skipWithDebt = gb972.vertexCount() > 0
                                      && !gb972.deferredRebuildPending() && !gb972.lightStale();
            const int preDebtRefresh = rebuilds972;
            if (!skipWithDebt) gb972.refreshMesh();
            const bool okDebtRebuilds = debt2Booked && !skipWithDebt
                                        && rebuilds972 == preDebtRefresh + 1;
            okDedup972 = okAvoidRebuild && okBootstrapRebuilds && okDebtRebuilds;
            if (!okDedup972)
                qInfo().noquote() << "  [t972 diag] dedup avoid" << okAvoidRebuild << "boot"
                                  << okBootstrapRebuilds << "debt" << okDebtRebuilds
                                  << "| queued" << debtWhileQueued << "skip" << skipDue972
                                  << "cnt" << rebuilds972;
            QObject::disconnect(cntConn972);
        }

        // Main.qml 源码钉（呈现层编排链；滤注释行后判定，同 t860 先例）：可见性解链（六段模板 visible
        //   只由 vertexCount 决定，旧「chunkInRange && …vertexCount」形态清零）+ kickWorldMeshSync
        //   存在且在 enterWorld 内挂载 + 窗外段 clearMesh 作废 + 排空泵（shift + refreshMesh）。
        bool okPin972 = false;
        {
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString candidates[2] = {
                QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
                QDir(exeDir + QStringLiteral("/../..")).absoluteFilePath(QStringLiteral("src/ui/Main.qml")),
            };
            QString qml;
            for (const QString &c : candidates) {
                QFile f(c);
                if (f.open(QIODevice::ReadOnly)) { qml = QString::fromUtf8(f.readAll()); break; }
            }
            if (qml.isEmpty()) {
                qInfo().noquote() << "  [t972 note] Main.qml not found near exe - source-pin skipped";
                okPin972 = true; // 行为级断言仍有效（源码钉缺席不判红，同 r24 note 先例）
            } else {
                QString code;
                for (const QString &line : qml.split(QLatin1Char('\n'))) {
                    const QString t = line.trimmed();
                    if (t.startsWith(QLatin1String("//")) || t.startsWith(QLatin1String("*"))
                        || t.startsWith(QLatin1String("/*")))
                        continue;
                    code += line;
                    code += QLatin1Char('\n');
                }
                const bool visTerrainUnchained = code.contains(QStringLiteral("visible: terrainGeo.vertexCount > 0"))
                                                 && !code.contains(QStringLiteral("visible: chunkInRange && terrainGeo.vertexCount"));
                const bool visFamilyUnchained =
                    !code.contains(QStringLiteral("visible: chunkInRange && waterGeo"))
                    && !code.contains(QStringLiteral("visible: chunkInRange && lavaGeo"))
                    && !code.contains(QStringLiteral("visible: chunkInRange && glassGeo"))
                    && !code.contains(QStringLiteral("visible: chunkInRange && iceGeo"))
                    && !code.contains(QStringLiteral("visible: chunkInRange && crossGeo"));
                const bool kickDefined = code.contains(QStringLiteral("function kickWorldMeshSync()"));
                // 挂载点序钉：kick 调用须落在 enterWorld 内「位姿定稿（adoptSpawnColumn）」之后、
                //   跨世界持久化装载（chestStore.loadAll）之前（startGame 定义在 enterWorld 之前，
                //   文件序不可作上界锚——首版钉曾误用而恒红）。
                const int idxAdopt = code.indexOf(QStringLiteral("player.adoptSpawnColumn()"));
                const int idxKick = idxAdopt >= 0 ? code.indexOf(QStringLiteral("kickWorldMeshSync()"), idxAdopt) : -1;
                const int idxChest = code.indexOf(QStringLiteral("chestStore.loadAll("));
                const bool kickHooked = kickDefined && idxAdopt >= 0 && idxKick >= 0
                                        && idxChest >= 0 && idxKick < idxChest;
                const bool clearOnFar = code.contains(QStringLiteral("seg.geometry.clearMesh()"));
                const bool pumpDrains = code.contains(QStringLiteral("_meshSyncQueue.shift()"))
                                        && code.contains(QStringLiteral("g.refreshMesh()"));
                // review0901 #33/#34 登记（注释即契约）钉：读**原始** qml（上方 code 已滤注释行，
                //   登记注释本体必须原样在——注释被改写/删除即红）。#33 = 排空泵「UI/呈现件豁免
                //   worldRunning」显式登记（暂停期照跑是有意取舍，t889 清点按豁免登记）；
                //   #34① = 全幅绘制的 Android 验证轮登记；#34② = 排空端复查谓词同式（代码形态，
                //   原文钉防散改）+ 登记注释。
                const bool pumpExemptionPin = qml.contains(QStringLiteral("UI/呈现件豁免 worldRunning"))
                                              && qml.contains(QStringLiteral("暂停期本泵照跑"))
                                              && qml.contains(QStringLiteral(
                                                      "t889 硬档停清点时本 Timer 按「呈现件豁免」登记"));
                const bool androidDrawPin = qml.contains(QStringLiteral("性能登记（review0901 #34①，绘制面）"))
                                            && qml.contains(QStringLiteral("Android 验证轮"))
                                            && qml.contains(QStringLiteral("draw 数"))
                                            && qml.contains(QStringLiteral("恢复「绘制半径」开关"));
                const bool drainSkipPin = qml.contains(QStringLiteral(
                        "if (g.vertexCount > 0 && !g.deferredRebuildPending() && !g.lightStale()) continue"))
                        && qml.contains(QStringLiteral("review0901 #34②（重建面微优化登记）"));
                okPin972 = visTerrainUnchained && visFamilyUnchained && kickDefined && kickHooked
                           && clearOnFar && pumpDrains
                           && pumpExemptionPin && androidDrawPin && drainSkipPin;
                if (!okPin972)
                    qInfo().noquote() << "  [t972 diag] visTerrain" << visTerrainUnchained
                                      << "visFamily" << visFamilyUnchained << "kickDefined" << kickDefined
                                      << "kickHooked" << kickHooked << "clearOnFar" << clearOnFar
                                      << "pumpDrains" << pumpDrains
                                      << "pumpExempt" << pumpExemptionPin << "android" << androidDrawPin
                                      << "drainSkip" << drainSkipPin;
            }
        }

        const bool okT972 = okBaseline && okWinLoaded && okLoadDebt && okStaleKept && okCleared
                            && okRefilled && okDebtClearedByBuild && okEditDebt && okEditDrained
                            && okLightDebt && okLightDrained && okDedup972 && okPin972;
        if (!okT972)
            qInfo().noquote() << "  [t972 diag] baseline" << okBaseline << "winLoaded" << okWinLoaded
                              << "loadDebt" << okLoadDebt << "staleKept" << okStaleKept
                              << "cleared" << okCleared << "refilled" << okRefilled
                              << "debtCleared" << okDebtClearedByBuild << "editDebt" << okEditDebt
                              << "editDrained" << okEditDrained << "lightDebt" << okLightDebt
                              << "lightDrained" << okLightDrained << "dedup" << okDedup972
                              << "pin" << okPin972
                              << "| cntA" << oldCntA << "->" << ga972.vertexCount()
                              << "cntB" << oldCntB << "->" << gb972.vertexCount();
        if (!okT972) ++totalFail;
        qInfo().noquote() << (okT972 ? "PASS" : "FAIL")
                          << "| t972 world-entry blank region: the t470/t472 view culling chained "
                             "Model.visible to the chunkInRange rebuild window, so on entering a world "
                             "51-84 of the 100 finite-world chunks were force-hidden (and the window "
                             "itself only rebuilt at load) -- the user saw large see-through voids that "
                             "only filled near-dig/place. Fix: Model.visible derives from vertexCount "
                             "alone (finite world renders edge to edge, MC 1.0 finite-map semantics; "
                             "t470 measured zero FPS gain from draw culling so this is free), and the "
                             "presentation layer kicks a progressive near-to-far mesh sync on world "
                             "entry (clearMesh invalidates the previous world's out-of-window meshes "
                             "so no stale terrain shows, then one bounded refreshMesh per frame drains "
                             "the queue through the SAME buildMesh(Dirty) chain as edits -- seconds-"
                             "scale fill, no synchronous full-rebuild stall). Probe legs (local 2x2 "
                             "chunk world, direct ChunkGeometry drive per t860 precedent): the "
                             "in-window segment rebuilds synchronously from loaded data at finishLoad "
                             "(empty-mesh count in view radius = 0), the load books the out-of-window "
                             "miss as deferredRebuildPending, clearMesh zeroes it and refreshMesh "
                             "rebuilds fresh-from-current-data (vertex count tracks the new world, "
                             "not the retained old mesh), an out-of-window edit and a past-threshold "
                             "dayMul step each book their debt and refreshMesh clears both (steady-"
                             "state eventual consistency for visible far terrain); Main.qml source "
                             "pin locks the orchestration (six templates visible-unchained from "
                             "chunkInRange, kickWorldMeshSync defined and hooked inside enterWorld "
                             "after the player pose settles, clearMesh on far segments, shift+"
                             "refreshMesh drain pump). review0901 additions: drain-side dedup "
                             "counter leg (a steady-debt entry catch-up rebuilt by the player "
                             "walking near is SKIPPED by the recheck predicate vertexCount>0 && "
                             "no deferred && no lightStale = zero wasted rebuilds, while bootstrap "
                             "entries (vertexCount==0 after clearMesh) and debt entries still "
                             "rebuild, counted via meshRebuilt), plus raw-source registration "
                             "pins (the drain pump's deliberate UI-chrome exemption from the "
                             "worldRunning pause caliber, the edge-to-edge draw-cost Android "
                             "verification registration, and the drain recheck predicate form)"
                             ;
    }

    // ── P-t934 waitSync 渲染侧归因插桩探针（dev-plan R19.17 性能批二；t933 act-ct 先例的渲染线程侧续篇）──
    //   背景：用户实测 frame2 行 waitSync 76ms 一家独大而 render_cpu ~7ms / RenderStats render ~1ms——
    //   GUI 在同步屏障阻塞 76ms，渲染 pass 本身不慢。机械链（main.cpp t934 注释）：渲染线程要跑完上一帧的
    //   [渲染 pass + present/vsync 阻塞 + 帧尾清理] 才到屏障 → 时间去向只剩三汇：①GPU/present bound
    //   （真 GPU 时间此前无测量面）②渲染线程 prep/上传风暴（mesh 重建的渲染侧回声）③渲染合帧/hook 多发
    //   （测量口径，非独立开销）。矩阵 rig 无 GUI、渲染线程路径不可直达 → 交付 = 插桩 + 判读面，钉三级：
    //   (a) 行为级（Core 直达）：FrameProfiler 样本计数 roundtrip——addSampleMs 每有效样本给 "cnt:<name>"
    //       +1（ms<=0 被忽略的样本不计数）+ flush 报告把帧段拼 "ms(N)" 且含新 fPresent 段。(N) 判据的数据面：
    //   拥塞下 animation tick 每事件循环回合一拍而渲染按 vsync 合帧 → 某段 N > main 的 N = 恒等式不可加
    //   （用户实测 main 88.4 vs 四段和 150.4 的机械解释）。
    //   (b) 源码钉 main.cpp + frameprofiler.cpp：fPresent 段接线——afterRendering（渲染线程 DirectConnection）
    //       记 afterRenderDoneNs、frameSwapped（GUI 收到）结清 fPresent = present 阻塞 + queued 派发延迟；
    //       计数 bump 语句本体。
    //   (c) 源码钉 Main.qml：F3 render-side 行读 RenderStats 的 lastCompletedGpuTime（真 GPU ms，perf-t520
    //       「无 GPU 计时」诚实标注的补面）/ renderPrepareTime / frameTime / syncTime / vmemUsedBytes——①②
    //   的实机判读面（waitSync 大时 gpu 大 → ①；prep 大 + mesh reb 非 0 → ②；都小 → ③看 (N)）。
    {
        // (a) 行为级：计数 roundtrip + 报告格式（先 flush 清窗防此前 World 计数残留；tickFrame 保除数 ≥1）。
        FrameProfiler *fp934 = FrameProfiler::instance();
        fp934->tickFrame();
        fp934->flush(); // 清窗 + 基线（frames=1 全零）
        fp934->addSampleMs(QStringLiteral("t934probe"), 1.5);
        fp934->addSampleMs(QStringLiteral("t934probe"), 2.5);
        fp934->addSampleMs(QStringLiteral("t934probe"), 0.0);  // ms<=0：被忽略 → 不得计数
        fp934->addSampleMs(QStringLiteral("t934probe"), -1.0); // 同上
        const qint64 n934 = fp934->countValue("cnt:t934probe"); // == 2（只数有效样本）
        for (int i = 0; i < 2; ++i) fp934->tickFrame();         // 本窗 frames=2
        fp934->addSampleMs(QStringLiteral("fWaitSync"), 10.0);
        fp934->addSampleMs(QStringLiteral("fWaitSync"), 20.0);
        fp934->addSampleMs(QStringLiteral("fPresent"), 5.0);
        fp934->flush();
        const QString rep934 = fp934->report();
        const bool okA1 = n934 == 2;
        const bool okA2 = rep934.contains(QStringLiteral("waitSync 15.0(2)")); // (10+20)ms / 2 帧
        const bool okA3 = rep934.contains(QStringLiteral("present 2.5(1)"));  // 5ms / 2 帧
        const bool okA4 = rep934.contains(QStringLiteral("(N)=samples/win")); // 判据图例在报告内
        const bool okA = okA1 && okA2 && okA3 && okA4;
        // (b) 源码钉：fPresent 接线（代码形态字面量——注释不含这些精确串）。
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QString root = QDir(exeDir + QStringLiteral("/..")).absolutePath();
        auto readSrc934 = [&root](const QString &rel) -> QString {
            QFile f(root + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mcs934 = readSrc934(QStringLiteral("main.cpp"));
        const bool okB1 = mcs934.contains(
            QStringLiteral("addSampleMs(QStringLiteral(\"fPresent\")"));
        const bool okB2 = mcs934.contains(
            QStringLiteral("*afterRenderDoneNs = FrameProfiler::nowNs();"));
        const bool okB3 = mcs934.contains(
            QStringLiteral("double(now - *afterRenderDoneNs) / 1e6"));
        const QString fpc934 = readSrc934(QStringLiteral("src/Core/frameprofiler.cpp"));
        const bool okB4 = fpc934.contains(
            QStringLiteral("m_counts[\"cnt:\" + name.toStdString()] += 1;"));
        const bool okB = okB1 && okB2 && okB3 && okB4;
        // (c) 源码钉：F3 render-side 真值行（RenderStats 6.11 Q_PROPERTY 名逐一钉——名字写错 = 运行期
        //     TypeError 静默断行，F3 行消失；本钉让改名 / 删行在矩阵红）。
        const QString qml934 = readSrc934(QStringLiteral("src/ui/Main.qml"));
        const bool okC1 = qml934.contains(QStringLiteral("rs.lastCompletedGpuTime"))
                       && qml934.contains(QStringLiteral("rs.renderPrepareTime"))
                       && qml934.contains(QStringLiteral("rs.frameTime"))
                       && qml934.contains(QStringLiteral("rs.syncTime"))
                       && qml934.contains(QStringLiteral("rs.vmemUsedBytes"));
        const bool okC2 = qml934.contains(QStringLiteral("\\nrender-side: frame "));
        const bool okC = okC1 && okC2;
        const bool okT934 = okA && okB && okC;
        if (!okT934) ++totalFail;
        if (!okT934)
            qInfo().noquote() << "  [t934 diag] a" << okA << "(cnt" << okA1 << n934
                              << "ws" << okA2 << "pr" << okA3 << "legend" << okA4 << ")"
                              << "| b" << okB << "(wire" << okB1 << okB2 << okB3 << "bump" << okB4 << ")"
                              << "| c" << okC << "(props" << okC1 << "line" << okC2 << ")"
                              << "| srcLen main" << mcs934.size() << "fp" << fpc934.size()
                              << "qml" << qml934.size();
        qInfo().noquote() << (okT934 ? "PASS" : "FAIL")
                          << "| t934 waitSync render-side attribution instrumentation: the 76ms "
                             "GUI block at the sync barrier is mechanically [render pass + "
                             "present/vsync + post-frame cleanup] of the PREVIOUS frame on the "
                             "render thread (render_cpu small => time went to one of three "
                             "sinks); this task delivers the discriminating instrumentation: "
                             "(1) fPresent bucket (afterRendering on the render thread -> "
                             "frameSwapped receipt on GUI = present blocking + queued dispatch, "
                             "wired in main.cpp), (2) per-bucket sample counts (N) on the frame/"
                             "frame2 report lines (denominator mismatch = hook coalescing under "
                             "congestion -- the mechanical explanation for the user's "
                             "four-segment sum 150.4 vs main_total 88.4; a segment's N exceeding "
                             "main's N means the identity is not additive, not extra cost), "
                             "(3) the F3 render-side truth line from View3D.renderStats: "
                             "frameTime/syncTime/renderPrepareTime plus lastCompletedGpuTime "
                             "(TRUE GPU ms via RHI timestamp queries, closing the perf-t520 "
                             "'no GPU timing' honesty gap) and vmemUsedBytes (monotonic vmem "
                             "growth across world switches that only a process restart clears = "
                             "the process-level GPU leak signature); read-out guide: waitSync "
                             "big + gpu big => GPU/present bound (treat via renderDistance/"
                             "segment folding/overdraw), waitSync big + prep big + win-line "
                             "mesh reb nonzero => render-thread upload/rebuild storm (the "
                             "t930/t933 storm's render-side echo), both small + N mismatch => "
                             "frame coalescing (measurement caliber); probe legs: (a) behavioral "
                             "count roundtrip on FrameProfiler (ignored ms<=0 samples must not "
                             "count, report formats 'ms(N)' including the new present field), "
                             "(b) source-pin the fPresent wiring + count bump, (c) source-pin "
                             "the F3 render-side property reads (a typo'd property name would "
                             "silently kill the line at runtime -- TypeError, headless-invisible)"
                          ;
    }

    // ── P-t935 mob ltail 10.59ms 粒度化 revision 探针（R19.17 性能批二收官；t933 判决的 QML 侧残留面）──
    //   用户实测（TNT 炸沙坑后）mob 行 ltail 10.59ms —— t905 头号假设实锤：20Hz 节流后单次 emit 仍激活
    //   **全部** N 槽 delegate 的 ~50 个 revision 绑定（槽高水位 47，空槽 / 静置 mob 的重求值全是白算）。
    //   修法 = 槽位脏名单（可见态指纹差分）+ 每槽 EntitySlotMonitor（delegate 绑 mon.revision，仅本槽
    //   可见态变化时重求值）。矩阵断言（任一 FAIL = 扇出回归 / 粒度化失效）：
    //   (a) 定向 bump：3 猪 spawn + 3 监视器建立后 damage 槽 1 → 恰槽 1 revision +1、槽 0/2 不变、全局
    //       revision +1；FrameProfiler 差分 emit=1 / bump=1 / fan=3（fan = 旧口径 count×emit 全扇出）；
    //   (b) 高水位 / 跨世界残留归零：clearAll → 三槽全 bump（alive/kind 翻转可见，delegate 隐藏）；此后
    //       逐只重 spawn（复用空槽）→ 每次恰复用槽 +1，**死槽不再 bump**（这正是 t933 判决的 QML 侧
    //       残留嫌疑面 —— 槽池高水位存活但刷新成本归零）；
    //   (c) O(变化槽) 非 O(count)：第 4 只 spawn 使 count 3→4 → fan=4 而 bump=1（新槽哨兵必 bump，
    //       既有 3 槽零成本）；新槽监视器建立后 damage 它 → 恰新槽 +1；
    //   (d) 源码钉：EntitySlotMonitor 类 / slotMonitorAt / notifyEntitiesChanged 漏斗 / 指纹差分本体；
    //       Main.qml 迁移面 —— mon.revision 绑定 ≥ 100 处且 entityManager.revision 全文件残留 0（残留
    //       = 未迁绑定仍吃全局扇出 = 粒度化破洞，t934 教训：源码钉须配行为腿防注释嵌字假绿）。
    {
        EntityManager ents;
        auto monRev = [&ents](int i) -> int {
            QObject *m = ents.slotMonitorAt(i);
            return m ? m->property("revision").toInt() : -1;
        };
        const int p0 = ents.spawnMobTyped(10, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const int p1 = ents.spawnMobTyped(12, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const int p2 = ents.spawnMobTyped(14, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        bool okPre = p0 == 0 && p1 == 1 && p2 == 2; // 追加槽 0/1/2（首次占用无空槽）
        // 模拟 QML delegate 建立（mobHost Repeater 每槽 delegate 各取一次监视器）。
        QObject *m0 = ents.slotMonitorAt(0), *m1 = ents.slotMonitorAt(1), *m2 = ents.slotMonitorAt(2);
        okPre = okPre && m0 && m1 && m2 && m0 != m1 && m1 != m2
               && monRev(0) == 0 && monRev(1) == 0 && monRev(2) == 0; // 建立时 0（spawn 时监视器尚不存在，无需 bump）
        const int revBefore = ents.revision();
        const qint64 e0 = FrameProfiler::instance()->countValue("mobEmitN");
        const qint64 b0 = FrameProfiler::instance()->countValue("mobBumpN");
        const qint64 f0 = FrameProfiler::instance()->countValue("mobFanN");
        const int r0a = monRev(0), r1a = monRev(1), r2a = monRev(2);
        // (a) 定向 damage：只槽 1 可见态变（health/hurtFlash）。
        ents.damageEntity(1, 1);
        const bool okA = monRev(0) == r0a && monRev(1) == r1a + 1 && monRev(2) == r2a
                        && ents.revision() == revBefore + 1
                        && FrameProfiler::instance()->countValue("mobEmitN") - e0 == 1
                        && FrameProfiler::instance()->countValue("mobBumpN") - b0 == 1
                        && FrameProfiler::instance()->countValue("mobFanN") - f0 == 3;
        // (b) 高水位归零：clearAll 全 bump（隐藏），重 spawn 只 bump 复用槽（free list LIFO：2 → 1 → 0）。
        const int r0b = monRev(0), r1b = monRev(1), r2b = monRev(2);
        ents.clearAll();
        const bool okB1 = monRev(0) == r0b + 1 && monRev(1) == r1b + 1 && monRev(2) == r2b + 1
                        && !ents.aliveAt(0) && !ents.aliveAt(1) && !ents.aliveAt(2);
        const int p3 = ents.spawnMobTyped(16, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const int p4 = ents.spawnMobTyped(18, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const int p5 = ents.spawnMobTyped(20, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        // 复用期间死槽数：p3 占槽 2、p4 占槽 1、p5 占槽 0（LIFO）；每 spawn 恰该槽 +1，其余不变。
        const bool okB2 = p3 == 2 && p4 == 1 && p5 == 0
                        && monRev(2) == r2b + 2 && monRev(1) == r1b + 2 && monRev(0) == r0b + 2
                        && ents.aliveAt(0) && ents.aliveAt(1) && ents.aliveAt(2);
        // (c) 第 4 只：free 空尽 → count 3→4；fan=4 而 bump=1（成本随变化槽，不随 count）。
        const qint64 b1 = FrameProfiler::instance()->countValue("mobBumpN");
        const qint64 f1 = FrameProfiler::instance()->countValue("mobFanN");
        const int r0c = monRev(0), r1c = monRev(1), r2c = monRev(2);
        const int p6 = ents.spawnMobTyped(22, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        QObject *m3 = ents.slotMonitorAt(3); // 新 delegate 建立（count 增后可取）
        const bool okC1 = p6 == 3 && m3
                        && monRev(0) == r0c && monRev(1) == r1c && monRev(2) == r2c
                        && FrameProfiler::instance()->countValue("mobFanN") - f1 == 4
                        && FrameProfiler::instance()->countValue("mobBumpN") - b1 == 1;
        const int r3c = monRev(3);
        ents.damageEntity(3, 1);
        const bool okC2 = monRev(3) == r3c + 1 && monRev(0) == r0c && monRev(1) == r1c && monRev(2) == r2c;
        // (d) 源码钉：C++ 漏斗 / 指纹差分本体 + QML 迁移面（mon.revision 大规模替换 + 全局 revision 残留 0）。
        const QString exeDir935 = QCoreApplication::applicationDirPath();
        const QString root935 = QDir(exeDir935 + QStringLiteral("/..")).absolutePath();
        auto readSrc935 = [&root935](const QString &rel) -> QString {
            QFile f(root935 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString emh935 = readSrc935(QStringLiteral("src/Entities/entitymanager.h"));
        const QString emc935 = readSrc935(QStringLiteral("src/Entities/entitymanager.cpp"));
        const QString qml935 = readSrc935(QStringLiteral("src/ui/Main.qml"));
        const QString fpc935 = readSrc935(QStringLiteral("src/Core/frameprofiler.cpp"));
        const bool okD1 = emh935.contains(QStringLiteral("class EntitySlotMonitor : public QObject"))
                        && emh935.contains(QStringLiteral("Q_INVOKABLE QObject *slotMonitorAt(int i);"))
                        && emh935.contains(QStringLiteral("void notifyEntitiesChanged();"))
                        && emh935.contains(QStringLiteral("quint64 slotFingerprint(size_t i) const;"));
        const bool okD2 = emc935.contains(QStringLiteral("void EntityManager::refreshSlotMonitors()"))
                        && emc935.contains(QStringLiteral("quint64 EntityManager::slotFingerprint(size_t i) const"))
                        && emc935.contains(QStringLiteral("++m_revision;\n    refreshSlotMonitors();\n    emit entitiesChanged();"))
                        && emc935.contains(QStringLiteral("FrameProfiler::instance()->addCount(\"mobBumpN\", bumped);"));
        int monCount935 = 0;
        for (int pos935 = qml935.indexOf(QStringLiteral("mon.revision"));
             pos935 >= 0; pos935 = qml935.indexOf(QStringLiteral("mon.revision"), pos935 + 1))
            ++monCount935;
        const bool okD3 = monCount935 >= 100
                        && !qml935.contains(QStringLiteral("entityManager.revision"))
                        && qml935.contains(QStringLiteral("property var mon: entityManager.slotMonitorAt(index)"));
        const bool okD4 = fpc935.contains(QStringLiteral("mobEmitN"))
                        && fpc935.contains(QStringLiteral("mobBumpN"))
                        && fpc935.contains(QStringLiteral("mobFanN"));
        const bool okT935 = okPre && okA && okB1 && okB2 && okC1 && okC2 && okD1 && okD2 && okD3 && okD4;
        if (!okT935) ++totalFail;
        if (!okT935)
            qInfo().noquote() << "  [t935 diag] pre" << okPre << "a" << okA << "b1" << okB1 << "b2" << okB2
                              << "c1" << okC1 << "c2" << okC2 << "| d" << okD1 << okD2 << okD3 << okD4
                              << "(monCount" << monCount935 << ")"
                              << "| srcLen h" << emh935.size() << "c" << emc935.size()
                              << "qml" << qml935.size();
        qInfo().noquote() << (okT935 ? "PASS" : "FAIL")
                          << "| t935 per-slot entity revision fanout: one throttled entitiesChanged "
                             "emit used to reactivate ALL N delegates' ~50 revision bindings each "
                             "(user-measured mob ltail 10.59ms after a TNT sand-pit explosion, 47-slot "
                             "high-water; empty slots and resting mobs re-evaluating bindings that read "
                             "back identical values = pure waste, and the cross-world residue t933 "
                             "verdict placed in the QML scene layer); fix = slot-granular dirty list: "
                             "every notify funnels through notifyEntitiesChanged which diffs a "
                             "per-slot fingerprint of the QML-visible fields (slotFingerprint, the "
                             "At()-accessor field contract) and bumps ONLY changed slots' "
                             "EntitySlotMonitor -> delegate bindings moved from entityManager.revision "
                             "to mon.revision re-evaluate per changed slot only; walking mobs still "
                             "refresh (MobModel walkPhase quantization unchanged), dead/high-water "
                             "slots cost zero; quantified via the F3 mob line emit/bump/fan counters "
                             "(bump = slots actually refreshed, fan = legacy count*emit fanout); "
                             "probe legs: (a) damage slot 1 of 3 bumps exactly slot 1 + emit=1/"
                             "bump=1/fan=3, (b) clearAll bumps all (visible hide) then each re-spawn "
                             "reusing a freed slot bumps only that slot -- dead-slot fanout stays zero "
                             "across the high-water pool, (c) 4th spawn grows count 3->4 with fan=4 "
                             "bump=1 and the new slot's monitor bumps on its first damage, (d) source "
                             "pins for the funnel/fingerprint/monitor + the Main.qml migration (>=100 "
                             "mon.revision bindings, zero entityManager.revision survivors)"
                          ;
    }

    // ── P-t978 生物复制体泄漏探针（R19.18 批六首项；用户 9-01 实测「新建世界生成瞬间双影 + 地上全是静止
    //   复制生物」，F3 mobs 1/36 = C++ 1 活体 vs 36 槽高水位，复制体呈 QML delegate/槽池簿记形态）──
    //   静态复制体的冻结机制在 C++ 既有路径层证伪为「无自发路径」（P-t935 已钉槽级 bump 语义；六处
    //   releaseSlot 调用点审计各带 alive 守卫；指纹契约对全部 At() 访问器逐项核账无缺字段），但槽池簿记缺
    //   **结构不变量**防线、QML delegate 可见性缺**自愈网**——本探针钉 t978 三层修复（真 EntityManager +
    //   真监视器，非平行复写）：
    //   (a) releaseSlot 幂等守卫：同一槽双释放不得把重复索引二次入 free list（pre-fix：两次 spawn 经 LIFO
    //       弹出同一槽，第二次 std::move 无声覆盖前者 → 两 spawn 同槽号 + liveCount 失真 = 槽池簿记损坏面）；
    //   (b) clearAll 死槽纠正 bump：预死槽（有监视器 = 有 delegate 在看）在跨世界清场时恰 +1 自愈（死槽
    //       指纹早在释放 notify 已对齐死态 → 常规 notify 永不再 bump → 冻结 delegate 无常规自愈面；
    //       pre-fix 恒 r1 不动 = 冻结复现）；复用槽 LIFO 与未复用死槽零额外 bump = t935 经济学不回退；
    //   (c) 源码钉：releaseSlot 守卫行 + clearAll corrective + Main.qml visible 的 entityManager.count
    //       自愈触碰在场，且 entityManager.revision 残留恒 0、mon.revision 迁移面 ≥100 不减（P-t935(d) 同钉）。
    {
        // (a) 双释放 → 两次 spawn 必得两个不同活槽。
        EntityManager enta;
        const int pa = enta.spawnMobTyped(10, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        QObject *mona = enta.slotMonitorAt(pa); // delegate 建立（监视器在案）
        enta.removeEntityAt(pa);
        enta.removeEntityAt(pa); // 双释放（防御口径：任何 caller 重复释放都不得污染 free list）
        const int pb = enta.spawnMobTyped(12, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const int pc = enta.spawnMobTyped(14, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const bool okA = pa == 0 && mona != nullptr && pb == 0 && pc == 1
                        && enta.aliveAt(pb) && enta.aliveAt(pc) && !enta.aliveAt(2)
                        && enta.liveCount() == 2 && enta.count() == 2;
        // (b) clearAll 死槽纠正 bump：0/1/2 三槽全建监视器 → 预杀槽 1 → clearAll 全槽翻死。
        EntityManager entb;
        auto monRevB = [&entb](int i) -> int {
            QObject *m = entb.slotMonitorAt(i);
            return m ? m->property("revision").toInt() : -1;
        };
        entb.spawnMobTyped(10, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30); // 槽 0
        entb.spawnMobTyped(12, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30); // 槽 1
        entb.spawnMobTyped(14, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30); // 槽 2
        entb.slotMonitorAt(0); entb.slotMonitorAt(1); entb.slotMonitorAt(2);
        entb.removeEntityAt(1); // 预死槽 1（释放 bump 后指纹对齐死态 → 常规 notify 永不再 bump = 冻结面原型）
        const int r0 = monRevB(0), r1 = monRevB(1), r2 = monRevB(2);
        entb.clearAll();
        const bool okB = monRevB(0) == r0 + 1 && monRevB(2) == r2 + 1 // 活槽释放 bump（t935 语义不变）
                       && monRevB(1) == r1 + 1                        // t978 corrective 恰 +1（pre-fix 恒 r1 = 冻结）
                       && !entb.aliveAt(0) && !entb.aliveAt(1) && !entb.aliveAt(2);
        const int pr = entb.spawnMobTyped(16, kRigY, 10, EntityManager::MobPig, QStringLiteral("#ee9999"), 30);
        const bool okB2 = pr == 2                       // LIFO 复用槽 2（P-t935(b) 同语义）
                        && monRevB(2) == r2 + 2         // 复用恰再 +1
                        && monRevB(1) == r1 + 1         // 未复用死槽零额外 bump（死槽经济学不回退）
                        && entb.aliveAt(2) && !entb.aliveAt(1);
        // (c) 源码钉：三层修复在场 + t935 迁移面不回退。
        const QString exeDir978 = QCoreApplication::applicationDirPath();
        const QString root978 = QDir(exeDir978 + QStringLiteral("/..")).absolutePath();
        auto readSrc978 = [&root978](const QString &rel) -> QString {
            QFile f(root978 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString emh978 = readSrc978(QStringLiteral("src/Entities/entitymanager.h"));
        const QString qml978 = readSrc978(QStringLiteral("src/ui/Main.qml"));
        const bool okC = emh978.contains(QStringLiteral("if (!e.alive) return; // t978"))
                       && emh978.contains(QStringLiteral("m_slotMonitors[i]->bump(); // t978 corrective"))
                       && qml978.contains(QStringLiteral("const _c = entityManager.count"))
                       && !qml978.contains(QStringLiteral("entityManager.revision"));
        int monCount978 = 0;
        for (int pos978 = qml978.indexOf(QStringLiteral("mon.revision"));
             pos978 >= 0; pos978 = qml978.indexOf(QStringLiteral("mon.revision"), pos978 + 1))
            ++monCount978;
        const bool okC2 = monCount978 >= 100;
        const bool okT978 = okA && okB && okB2 && okC && okC2;
        if (!okT978) ++totalFail;
        if (!okT978)
            qInfo().noquote() << "  [t978 diag] a" << okA << "b" << okB << "b2" << okB2
                              << "c" << okC << "c2" << okC2
                              << "(pa" << pa << "pb" << pb << "pc" << pc << "pr" << pr
                              << "monRevB" << monRevB(0) << monRevB(1) << monRevB(2) << ")";
        qInfo().noquote() << (okT978 ? "PASS" : "FAIL")
                          << "| t978 mob-clone leak: the user's 9-01 playtest reports a duplicated "
                             "projection at spawn-instant in fresh worlds and piles of static texture-only "
                             "mob clones after long sessions (F3 mobs 1/36 = one live entity vs 36-slot "
                             "high-water, so the clones present as QML delegate / slot-bookkeeping state, "
                             "not C++ entities); static audit of the existing paths proved no spontaneous "
                             "freeze (P-t935 already pins the per-slot bump semantics, all six releaseSlot "
                             "callers alive-check first, and the fingerprint contract covers every "
                             "At() accessor field), so the fix is three defensive layers, each "
                             "probe-pinned here: (a) releaseSlot gains an idempotency guard -- a double "
                             "release must never push the same index into the free list twice (pre-fix, "
                             "two spawns then pop the same slot LIFO and the second std::move silently "
                             "overwrites the first: same slot index twice + liveCount drift = the "
                             "slot-pool corruption face); (b) clearAll gains a corrective bump for dead "
                             "slots that already carry a monitor (= a delegate is watching): their "
                             "fingerprint aligned to the dead state at the release notify, so routine "
                             "notifies never bump them again and a frozen delegate has no routine "
                             "self-heal face -- the cross-world teardown is the only mandatory "
                             "whole-pool refresh point, so it force-bumps exactly once (monitor-less "
                             "dead slots stay zero-bump, keeping the t935 economics; the LIFO reuse "
                             "and unused-dead-slot zero-bump semantics are unchanged); (c) source pins "
                             "for the guard line, the corrective line, and the Main.qml visible "
                             "self-heal net (an entityManager.count touch -- NOTIFY entitiesChanged "
                             "fires on every notify -- so a dead slot's delegate re-reads aliveAt and "
                             "hides at the next emit no matter what failed in the monitor chain; cost "
                             "is one bool Q_INVOKABLE re-eval per slot per emit against the ~50 "
                             "bindings-per-slot t935 collapsed, and the entityManager.revision string "
                             "stays extinct with the mon.revision migration surface >= 100); probe "
                             "legs: (a) double-remove then two spawns land on distinct live slots "
                             "with liveCount 2, (b) pre-killed slot 1 with a monitor advances exactly "
                             "+1 on clearAll then stays put across an LIFO reuse of slot 2, (c) the "
                             "three fix markers present with the t935 pins intact"
                          ;
    }

    // ── P-t936 动力轨传播顺序无关探针（World 直编；spec「不管先放什么，激活都沿动力铁轨链传到红石最远
    //    可达范围」—— 用户实测：先放上坡动力轨再激活一段，后放的其他上坡动力轨不被激活〔要全部摆好再激
    //    活才行〕）──
    //   不变量：激活集 = 世界布局的**纯函数**（任意放置 / 破坏序收敛到同一激活集）。五腿：
    //   (a) 用户序主腿：坡链 A..C 先摆 + 源激活段（A..C 亮）→ **后放** D..F 接链 → D..F 立即亮（主断言；
    //       旧版接收器扫描域 = 6 正交邻，坡链相邻轨是斜角 → 后放轨的 pass 内够不到链尾 ≤8 格外的直供种子
    //       → 恒判灭 = 顺序依赖）；
    //   (b) 阴性·无源：无源坡链的延伸轨保持灭（不接链 / 源未激活 → 不激活）；随后**最后**放源 → 整链 7 根
    //       全亮（第三种摆放序也收敛到同一激活集）；
    //   (c) 深度上限腿：种子 + 下坡 9 根（逐格 -1）→ 前 8 根亮、第 9 根灭（kGoldenRailChainMax 钉住——
    //       「放置即亮」不得越链上限过度点亮；下坡方向兼钉 -1 层传播）；
    //   (d) 破坏对称腿：亮坡链破中段轨 → 源侧 2 根保持亮、远翼 2 根熄灭（放置 / 破坏对称完整——旧版远翼
    //       不在任何 6 正交扫描域 = 残留通电位）；
    //   (e) 源码钉：goldenRailChainStep 单一权威（声明 + 定义）+ notePowerWrite 放置沿重算块 + 链 BFS 消费
    //       同 helper + 深度常量恰一处声明（防第二套判定 / 双深度源漂移回归）。
    {
        // rig 选址：运行期扫描空区（lessons t769：不信任「某高度以上必空」经验值）。单列 rig（链沿 X 走
        //   向，dz -1..1 隔离）；各腿独立选址、用毕清场。
        const auto scanRigArea = [&](int dxLo, int dxHi, int dyLo, int dyHi) {
            int rx = -1, rz = -1;
            for (int zz = 3; zz < 94 && rx < 0; zz += 2)
                for (int xx = 4; xx + dxHi < 96 && rx < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = dxLo; dx <= dxHi && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = dyLo; dy <= dyHi && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { rx = xx; rz = zz; }
                }
            return QPair<int, int>(rx, rz);
        };
        const auto railOn = [&](int x, int y, int z) {
            return (w.stateAt(x, y, z) & BR::GoldenRailStateOnFlag) != 0;
        };
        // (a) 用户序主腿：先摆 A..C → 激活 → 后放 D..F。
        bool okA = false;
        {
            const auto [x0, z0] = scanRigArea(-1, 6, -2, 6);
            if (x0 < 0) {
                qInfo().noquote() << "  [t936 diag] leg A: no clear rig area found";
            } else {
                for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0); // 源放种子侧邻（t910 口径：非脚下）
                tickN(w, 6);
                int litSeg = 0;
                for (int i = 0; i <= 2; ++i) litSeg += railOn(x0 + i, kRigY + i, z0);
                for (int i = 3; i <= 5; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0); // 后放
                tickN(w, 6);
                int litAll = 0;
                for (int i = 0; i <= 5; ++i) litAll += railOn(x0 + i, kRigY + i, z0);
                okA = litSeg == 3 && litAll == 6;
                if (!okA)
                    qInfo().noquote() << "  [t936 diag] leg A litSeg" << litSeg << "litAll" << litAll;
                for (int i = 0; i <= 5; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (b) 阴性·无源 + 源最后放。
        bool okB = false;
        {
            const auto [x0, z0] = scanRigArea(-1, 7, -2, 6);
            if (x0 < 0) {
                qInfo().noquote() << "  [t936 diag] leg B: no clear rig area found";
            } else {
                for (int i = 0; i <= 4; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0);
                tickN(w, 4);
                int dark5 = 0;
                for (int i = 0; i <= 4; ++i) dark5 += railOn(x0 + i, kRigY + i, z0);
                for (int i = 5; i <= 6; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0); // 延伸无源链
                tickN(w, 4);
                int dark7 = 0;
                for (int i = 0; i <= 6; ++i) dark7 += railOn(x0 + i, kRigY + i, z0);
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0); // 源**最后**放
                tickN(w, 6);
                int lit7 = 0;
                for (int i = 0; i <= 6; ++i) lit7 += railOn(x0 + i, kRigY + i, z0);
                okB = dark5 == 0 && dark7 == 0 && lit7 == 7;
                if (!okB)
                    qInfo().noquote() << "  [t936 diag] leg B dark5" << dark5 << "dark7" << dark7
                                      << "lit7" << lit7;
                for (int i = 0; i <= 6; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (c) 深度上限腿（下坡逐格 -1 共 9 根：种子 + 7 亮，第 9 根灭）。
        bool okC = false;
        {
            const auto [x0, z0] = scanRigArea(-1, 9, -10, 2);
            if (x0 < 0) {
                qInfo().noquote() << "  [t936 diag] leg C: no clear rig area found";
            } else {
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                for (int i = 0; i <= 8; ++i) w.setBlock(x0 + i, kRigY - i, z0, BR::GoldenRail, 0);
                tickN(w, 8);
                int lit08 = 0;
                for (int i = 0; i <= 7; ++i) lit08 += railOn(x0 + i, kRigY - i, z0);
                const bool ninthDark = !railOn(x0 + 8, kRigY - 8, z0);
                okC = lit08 == 8 && ninthDark;
                if (!okC)
                    qInfo().noquote() << "  [t936 diag] leg C lit08" << lit08 << "ninthDark" << ninthDark;
                for (int i = 0; i <= 8; ++i) w.setBlock(x0 + i, kRigY - i, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (d) 破坏对称腿（亮坡链破中段 → 源侧亮 / 远翼灭）。
        bool okD = false;
        {
            const auto [x0, z0] = scanRigArea(-1, 5, -2, 5);
            if (x0 < 0) {
                qInfo().noquote() << "  [t936 diag] leg D: no clear rig area found";
            } else {
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0);
                for (int i = 0; i <= 4; ++i) w.setBlock(x0 + i, kRigY + i, z0, BR::GoldenRail, 0);
                tickN(w, 6);
                int lit5 = 0;
                for (int i = 0; i <= 4; ++i) lit5 += railOn(x0 + i, kRigY + i, z0);
                w.setBlock(x0 + 2, kRigY + 2, z0, BR::Air, 0); // 破中段
                tickN(w, 6);
                const bool nearLit = railOn(x0, kRigY, z0) && railOn(x0 + 1, kRigY + 1, z0);
                const bool farDark = !railOn(x0 + 3, kRigY + 3, z0) && !railOn(x0 + 4, kRigY + 4, z0);
                okD = lit5 == 5 && nearLit && farDark;
                if (!okD)
                    qInfo().noquote() << "  [t936 diag] leg D lit5" << lit5 << "nearLit" << nearLit
                                      << "farDark" << farDark;
                for (int i = 0; i <= 4; ++i)
                    if (i != 2) w.setBlock(x0 + i, kRigY + i, z0, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
                tickN(w, 2);
            }
        }
        // (e) 源码钉（t935 模式：源文件直读字符串钉——helper 消失 / 双深度源 / BFS 自写第二套判定即红）。
        const QString exeDir936 = QCoreApplication::applicationDirPath();
        const QString root936 = QDir(exeDir936 + QStringLiteral("/..")).absolutePath();
        auto readSrc936 = [&root936](const QString &rel) -> QString {
            QFile f(root936 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString wh936 = readSrc936(QStringLiteral("src/World/world.h"));
        const QString wc936 = readSrc936(QStringLiteral("src/World/world.cpp"));
        const bool okE1 = wh936.contains(QStringLiteral(
                              "bool goldenRailChainStep(int x, int y, int z, int ax, int az, int &nx, int &ny, int &nz) const;"))
                       && wc936.contains(QStringLiteral(
                              "bool World::goldenRailChainStep(int x, int y, int z, int ax, int az, int &nx, int &ny, int &nz) const"));
        const bool okE2 = wc936.contains(QStringLiteral("t936 动力轨放置 / 破坏沿重算"))
                       && wc936.contains(QStringLiteral(
                              "goldenRailChainStep(cx, cy, cz, dir[0], dir[1], sx, sy, sz)"))
                       && wc936.contains(QStringLiteral("kGoldenRailChainMax && !frontier.empty()"));
        const bool okE3 = wc936.contains(QStringLiteral(
                              "if (!goldenRailChainStep(c.x, c.y, c.z, a[0], a[1], nx, ny, nz)) continue;"));
        int depthDecl936 = 0;
        for (int pos936 = wc936.indexOf(QStringLiteral("kGoldenRailChainMax = 8"));
             pos936 >= 0;
             pos936 = wc936.indexOf(QStringLiteral("kGoldenRailChainMax = 8"), pos936 + 1))
            ++depthDecl936;
        const bool okE4 = depthDecl936 == 1;
        const bool okT936 = okA && okB && okC && okD && okE1 && okE2 && okE3 && okE4;
        if (!okT936) ++totalFail;
        if (!okT936)
            qInfo().noquote() << "  [t936 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE1 << okE2 << okE3 << okE4
                              << "(depthDecl" << depthDecl936 << ")"
                              << "| srcLen h" << wh936.size() << "c" << wc936.size();
        qInfo().noquote() << (okT936 ? "PASS" : "FAIL")
                          << "| t936 powered-rail propagation is order-independent: activation is "
                             "a pure function of the world layout - placing a powered rail that "
                             "extends an already-energized climbing chain lights it immediately "
                             "(user report: rails placed AFTER activating a segment stayed dark "
                             "unless everything was laid out before powering; the receiver scan "
                             "only covered 6-orthogonal neighbors so a newly placed slope rail "
                             "never saw the directly-fed seed up to 8 chain cells away along the "
                             "diagonal rail geometry); fix = golden-rail edits walk the chain via "
                             "goldenRailChainStep (the same three-height-probe single authority "
                             "the chain BFS uses) for kGoldenRailChainMax steps and dirty every "
                             "rail on it, so the next tick re-seeds from the true directly-fed "
                             "rail and the t704/t910 BFS relights/extinguishes to the fixed-point "
                             "regardless of placement order; symmetric destruction face covered "
                             "(breaking a mid-chain slope rail now extinguishes the sourceless "
                             "far wing instead of leaving stale charge outside the 6-orthogonal "
                             "scan domain); probe legs: (a) user-order main leg (A..C placed, "
                             "powered, then D..F placed after -> all 6 lit), (b) negative "
                             "unpowered-chain extension stays dark + source placed LAST lights "
                             "all 7, (c) downhill 9-rail chain lights seed+7 only (chain depth "
                             "cap pinned - no over-lighting past kGoldenRailChainMax), (d) "
                             "mid-chain break keeps the fed side lit and drops the far wing, "
                             "(e) source pins: helper declaration+definition, the notePowerWrite "
                             "placement-walk block, the BFS consuming the same helper, and "
                             "exactly one kGoldenRailChainMax declaration"
                          ;
    }

    // ── P-t937 平行轨道独立激活 + 重算风暴收窄探针（World 直编；spec t937 ①②）──
    //   ① 平行独立：两条互不连接的平行动力轨线（A 沿 X 走、B 贴其 +Z 侧同沿 X 走），源只贴 A 头 →
    //      A 全亮、B 恒灭。旧版链步只看「该向三高有动力轨」的**空间存在性** → A 的 +Z 探针命中 B，
    //      信号横穿无连接的平行轨 = 用户实测「一个红石点亮两条独立平行轨道」；连接位门槛（步源轨须持
    //      该轴向 RailConn 位——贯穿轴轨不设跨向位）后 A→B 无位即断。
    //   ② 零重算：普通方块（Stone）放到亮链中段轨旁再挖掉 → powerRecomputePasses 计数不动（普通方块
    //      与空气在电力读数里同为 0——收窄面判据）且链全程保持亮（无「灭一下又亮」中间态）。
    //   ③ 真触发照常 + 先算后清：拉杆（未扳）放到亮链中段旁 → 计数增长（触发面不回缩）且链不闪
    //      （goldenRailChainHasFedSeed 域外种子兜底——旧版扫描域无种子即误熄、波前数 tick 后重亮 =
    //      两拍闪烁）；拆源 → 链全灭（降沿终态 = 布局纯函数不变，t936 不变量保持）。
    //   (d) 源码钉：链步连接位门槛 / 快路径收窄谓词 / 反向走查消费 / 链传集并入写集 / 计数器声明。
    {
        // rig 选址：运行期扫描空区（lessons t769）。双线 rig（A/B 沿 X 平行、dz -1..2 隔离）。
        const auto scanRig937 = [&](int dxLo, int dxHi) {
            int rx = -1, rz = -1;
            for (int zz = 3; zz < 94 && rx < 0; zz += 2)
                for (int xx = 4; xx + dxHi < 96 && rx < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = dxLo; dx <= dxHi && clear; ++dx)
                        for (int dz = -1; dz <= 2 && clear; ++dz)
                            for (int dy = -1; dy <= 1 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { rx = xx; rz = zz; }
                }
            return QPair<int, int>(rx, rz);
        };
        const auto railOn937 = [&](int x, int y, int z) {
            return (w.stateAt(x, y, z) & BR::GoldenRailStateOnFlag) != 0;
        };
        const auto litA = [&](int x0, int z0) {
            int n = 0;
            for (int i = 0; i < 6; ++i) n += railOn937(x0 + i, kRigY, z0);
            return n;
        };
        const auto litB = [&](int x0, int z0) {
            int n = 0;
            for (int i = 0; i < 6; ++i) n += railOn937(x0 + i, kRigY, z0 + 1);
            return n;
        };
        const auto clear937 = [&](int x0, int z0) {
            w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0);
            for (int i = 0; i < 6; ++i) {
                w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
                w.setBlock(x0 + i, kRigY, z0 + 1, BR::Air, 0);
            }
            tickN(w, 2);
        };
        // (a) ① 平行独立主腿：A 全亮 / B 恒灭（阴性即用户症状——旧空间域下 B 误亮）。
        bool okA = false, okB = false, okC = false;
        {
            const auto [x0, z0] = scanRig937(-1, 6);
            if (x0 < 0) {
                qInfo().noquote() << "  [t937 diag] no clear rig area found";
            } else {
                for (int i = 0; i < 6; ++i) w.setBlock(x0 + i, kRigY, z0, BR::GoldenRail, 0);     // 线 A（z0）
                for (int i = 0; i < 6; ++i) w.setBlock(x0 + i, kRigY, z0 + 1, BR::GoldenRail, 0); // 线 B（z0+1，平行贴邻）
                w.setBlock(x0 - 1, kRigY, z0, BR::RedstoneBlock, 0); // 源只贴 A 头（B 各格 6 邻均不含源）
                tickN(w, 8);
                const int a = litA(x0, z0), b = litB(x0, z0);
                okA = a == 6 && b == 0;
                if (!okA) qInfo().noquote() << "  [t937 diag] a litA" << a << "litB" << b;
                // (b) ② 普通方块编辑零重算 + 无闪烁：settled 后快照计数 → 放石头 / 挖石头（贴 A 中段轨旁）
                //     → 计数不动 + A 恒 6/6。旧版：任意红石族邻格触发 → 计数增 + 域内无种子先误熄再重亮。
                tickN(w, 2); // 沉降（前腿写入的波前脏集清空）
                const int c0 = w.powerRecomputePasses();
                w.setBlock(x0 + 3, kRigY, z0 - 1, BR::Stone, 0); // 贴 A3 的普通方块
                tickN(w, 3);
                const int aPlace = litA(x0, z0);
                w.setBlock(x0 + 3, kRigY, z0 - 1, BR::Air, 0);   // 挖掉
                tickN(w, 3);
                const int c1 = w.powerRecomputePasses();
                const int aBreak = litA(x0, z0);
                okB = c1 == c0 && aPlace == 6 && aBreak == 6;
                if (!okB)
                    qInfo().noquote() << "  [t937 diag] b passes" << c0 << "->" << c1
                                      << "litA(place)" << aPlace << "litA(break)" << aBreak;
                // (c) ③ 真触发照常 + 先算后清：未扳拉杆放亮链中段旁 → 计数增长且不闪（域外种子兜底）；
                //     拆源 → 全灭（降沿终态）。
                const int c2 = w.powerRecomputePasses();
                w.setBlock(x0 + 2, kRigY, z0 - 1, BR::Lever, 0); // 慢路径（拉杆属红石族）——真触发面
                tickN(w, 1);
                const int cLeverTick = w.powerRecomputePasses();
                const int aLever = litA(x0, z0);
                tickN(w, 3); // 旧版误熄后波前重亮窗——全程 6/6 才算无两拍闪烁
                const int aLeverSettled = litA(x0, z0);
                w.setBlock(x0 + 2, kRigY, z0 - 1, BR::Air, 0);
                w.setBlock(x0 - 1, kRigY, z0, BR::Air, 0); // 拆源 → 降沿
                tickN(w, 10);
                const int aDark = litA(x0, z0);
                okC = cLeverTick > c2 && aLever == 6 && aLeverSettled == 6 && aDark == 0;
                if (!okC)
                    qInfo().noquote() << "  [t937 diag] c passes" << c2 << "->" << cLeverTick
                                      << "aLever" << aLever << "aLeverSettled" << aLeverSettled
                                      << "aDark" << aDark;
                clear937(x0, z0);
            }
        }
        // (d) 源码钉（t935/t936 模式：源文件直读字符串钉——门槛 / 收窄谓词 / 走查 / 并入 / 计数器消失即红）。
        const QString exeDir937 = QCoreApplication::applicationDirPath();
        const QString root937 = QDir(exeDir937 + QStringLiteral("/..")).absolutePath();
        auto readSrc937 = [&root937](const QString &rel) -> QString {
            QFile f(root937 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString wh937 = readSrc937(QStringLiteral("src/World/world.h"));
        const QString wc937 = readSrc937(QStringLiteral("src/World/world.cpp"));
        const bool okD1 = wc937.contains(QStringLiteral("if ((con & need) == 0) return false;"))
                       && wc937.contains(QStringLiteral("const quint8 need = (ax > 0) ? BlockRegistry::RailConnPx"));
        const bool okD2 = wc937.contains(QStringLiteral("static bool isPowerEmitterBlock(quint8 id)"))
                       && wc937.contains(QStringLiteral("isPowerEmitterBlock(nb)"));
        const bool okD3 = wc937.contains(QStringLiteral("bool World::goldenRailChainHasFedSeed(int x, int y, int z) const"))
                       && wc937.contains(QStringLiteral("wantOn = goldenRailChainHasFedSeed(x, y, z);"));
        const bool okD4 = wc937.contains(QStringLiteral("for (const quint64 k : goldenPowered) receivers.insert(k);"));
        const bool okD5 = wh937.contains(QStringLiteral("int powerRecomputePasses() const"))
                       && wh937.contains(QStringLiteral("bool goldenRailChainHasFedSeed(int x, int y, int z) const;"));
        const bool okT937 = okA && okB && okC && okD1 && okD2 && okD3 && okD4 && okD5;
        if (!okT937) ++totalFail;
        if (!okT937)
            qInfo().noquote() << "  [t937 diag] a" << okA << "b" << okB << "c" << okC
                              << "| d" << okD1 << okD2 << okD3 << okD4 << okD5
                              << "| srcLen h" << wh937.size() << "c" << wc937.size();
        qInfo().noquote() << (okT937 ? "PASS" : "FAIL")
                          << "| t937 parallel tracks activate independently and the power-recompute "
                             "trigger surface is narrowed to connectivity-relevant edits: (1) chain "
                             "propagation now requires the stepping rail's own connection bit toward "
                             "the step direction (the same physical-connection authority the mesher "
                             "and minecart pickTrackStep consume), so a redstone source feeding one "
                             "track no longer leaks across to an unconnected parallel track placed "
                             "beside it (old chain step only probed spatial existence - any golden "
                             "rail in the 3-height window was chain, so power jumped the gap between "
                             "side-by-side tracks; user report: one redstone lit two independent "
                             "parallel tracks); (2) notePowerWrite's fast path only continues when a "
                             "neighbor is dust or a power SOURCE - plain blocks read as 0 in every "
                             "power reading just like air, so placing/breaking ordinary blocks beside "
                             "rails (and t930 cascade sand landings) trigger zero redstone recomputes "
                             "(old path accepted ANY power-family neighbor including receivers, "
                             "pumping a full-chain recompute whose scan domain missed the chain seed "
                             "and visibly flickered the rails dark-then-lit); (3) before writing a "
                             "golden rail dark, a bounded reverse seed-walk (same chain-step "
                             "authority, same depth) confirms no directly-fed rail within chain "
                             "distance 7 - legit triggers (lever/lamp placement, source edits) no "
                             "longer emit wrong dark intermediates, and the chain-lit set is merged "
                             "into the receiver write set so brightening completes in one pass; "
                             "final activation states are unchanged (order-independent pure function "
                             "of layout, t936 invariant kept, P-t936 legs stay green); probe legs: "
                             "(a) parallel rig A-lit-6/B-dark-0, (b) stone place+break beside the lit "
                             "chain - powerRecomputePasses counter flat and chain stays lit, (c) lever "
                             "beside mid-chain recompute counter rises with no flicker + source "
                             "removal darkens the chain, (d) source pins for the connection gate, "
                             "narrowed predicate, reverse-walk consumption, write-set merge, and the "
                             "counter accessor"
                          ;
    }

    // ── P-t938 铁轨可选中探针（选体射线直调）【t983 改版：整格命中废除，终局 = 薄板 sub-AABB】──
    //   沿革：t638③ 轨选体盒 = 2/16 贴地薄板（轨格上部空气段穿透 → t938 用户「挖轨变挖后面 / 放矿车
    //   不便」）→ t938 翻为 HitRail 整格命中（轨格全高可选）→ **t983 用户再翻**：整格口径让射线从轨格
    //   顶面进入（上部 15/16 空气）即抢命中——站轨上前向放置前向铁轨时准星所指目标格的射线在脚下轨格
    //   空气段被截停 =「指空打轨选不中目标格」。终局口径（用户铁律「选块必须指哪指哪」）：轨的真实相交
    //   盒取薄板几何（raycastAABBs ~2/16），命中距离按真实盒算——瞄轨板本体仍选中轨，瞄上部空气穿透
    //   命中后方；选体与相机（HitPartial）对轨同几何（轨无碰撞不拉近视距，t605 语义不变）。
    //   腿：(a) 浅俯角穿轨格上部空气段 → 命中后方墙（t938 整格腿翻转； = 指哪指哪主钉）；
    //       (b) 陡俯角直瞄轨板（薄板精确路径回归钉）：命中轨 + 法线 +Y；
    //       (c) 穿轨上方空域（轨格上一格）→ 命中后墙高位格（透视保留）；
    //       (d) isRail 家族三 id 同为薄板可选（动力 / 探测换格同 (b) 射线命中）；
    //       (e) 相机模式同 (a) 射线仍命中墙（相机零回归钉，与选体同几何）；
    //       (f) 放置回归：轨板命中面（+Y）邻格 = 轨上方 Air 可放块不毁轨；轨格自身不可被替换；
    //       (g) 源码钉：fullCell 链无轨特判 / updateRaycast 过滤器 / .h 沿革注 / selectionAABBs 轨薄板
    //           分支 + 矿车放置分支 + 放置预检行。
    {
        // rig 选址：运行期扫描空区（lessons t769）。footprint x0-1..x0+5 × z0-1..z0+1 × y kRigY-1..kRigY+3。
        const auto scanRig938 = [&]() {
            int rx = -1, rz = -1;
            for (int zz = 3; zz < 94 && rx < 0; zz += 2)
                for (int xx = 4; xx + 5 < 96 && rx < 0; xx += 2) {
                    bool clear = true;
                    for (int dx = -1; dx <= 5 && clear; ++dx)
                        for (int dz = -1; dz <= 1 && clear; ++dz)
                            for (int dy = -1; dy <= 3 && clear; ++dy)
                                if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                    if (clear) { rx = xx; rz = zz; }
                }
            return QPair<int, int>(rx, rz);
        };
        // 选体过滤器（镜像 updateRaycast t983 后的实参）与相机过滤器。
        const unsigned kSel938 = RayFilter::HitTorch | RayFilter::HitLadder;
        const int kSelReach = 8; // 探针射程（与 kReach 解耦——测的是几何语义非触达）
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        const auto [x0, z0] = scanRig938();
        if (x0 < 0) {
            qInfo().noquote() << "  [t938 diag] no clear rig area found";
        } else {
            const float zc = float(z0) + 0.5f; // rig 中轴（列中心）
            // 场景：轨格 (x0+2,kRigY,z0) + 后墙石柱 (x0+4, kRigY..kRigY+2)（轨后 2 格的「后格」实体）。
            for (int dy = 0; dy <= 2; ++dy) placeRigBlock(w, x0 + 4, kRigY + dy, z0, BR::Stone, 0);
            placeRigBlock(w, x0 + 2, kRigY, z0, BR::Rail, 0);
            // (a) 浅俯角穿轨格上部空气段 → 后墙（t983 主钉；t938 整格腿翻转）：眼位 (x0-0.5, kRigY+1.62)，
            //     视线过轨格中心高度 y_frac 0.60 —— 轨格内段 y_frac 0.77→0.43（全程在 2/16 薄板上方 =
            //     薄板口径必穿）→ 命中墙格 (x0+4,kRigY) -X 面。「指空打轨」不再发生：上部空气 = 指后方。
            const QVector3D oA(x0 - 0.5f, kRigY + 1.62f, zc);
            const QVector3D dA(3.0f, -1.02f, 0.0f);
            const RayHit hA = raycastVoxel(w, oA, dA, kSelReach, kSel938);
            okA = hA.valid && hA.bx == x0 + 4 && hA.by == kRigY && hA.bz == z0
                  && hA.nx == -1.0f && hA.ny == 0.0f && hA.nz == 0.0f;
            if (!okA)
                qInfo().noquote() << "  [t938 diag] a" << hA.valid << hA.bx << hA.by << hA.bz
                                  << "n" << hA.nx << hA.ny << hA.nz;
            // (b) 陡俯角直瞄轨板（薄板精确路径回归钉）：垂直下视线入轨格顶面 → 命中轨板、法线 +Y
            //     （挖轨 / 手持矿车右键上轨经此路径保持可选）。
            const RayHit hB = raycastVoxel(w, QVector3D(x0 + 2.5f, kRigY + 3.0f, zc),
                                           QVector3D(0.0f, -1.0f, 0.0f), kSelReach, kSel938);
            okB = hB.valid && hB.bx == x0 + 2 && hB.by == kRigY && hB.bz == z0
                  && hB.nx == 0.0f && hB.ny == 1.0f && hB.nz == 0.0f;
            if (!okB)
                qInfo().noquote() << "  [t938 diag] b" << hB.valid << hB.bx << hB.by << hB.bz
                                  << "n" << hB.nx << hB.ny << hB.nz;
            // (c) 穿轨上方空域（轨格上一格，y_frac ~0.3..0.9 段）→ 命中后墙高位格而非轨（透视保留）：
            //     视线全程 y>kRigY+1 直到入墙格 (x0+4,kRigY+1)。
            const QVector3D oC(x0 - 0.5f, kRigY + 2.62f, zc);
            const QVector3D dC(3.0f, -0.90f, 0.0f);
            const RayHit hC = raycastVoxel(w, oC, dC, kSelReach, kSel938);
            okC = hC.valid && hC.bx == x0 + 4 && hC.by == kRigY + 1 && hC.bz == z0;
            if (!okC)
                qInfo().noquote() << "  [t938 diag] c" << hC.valid << hC.bx << hC.by << hC.bz;
            // (d) isRail 家族：动力轨 / 探测轨替换同格，同一 (b) 射线 → 同样薄板命中（三 id 单一谓词覆盖）。
            bool famGolden = false, famDetector = false;
            placeRigBlock(w, x0 + 2, kRigY, z0, BR::GoldenRail, 0);
            const RayHit hD1 = raycastVoxel(w, QVector3D(x0 + 2.5f, kRigY + 3.0f, zc),
                                            QVector3D(0.0f, -1.0f, 0.0f), kSelReach, kSel938);
            famGolden = hD1.valid && hD1.bx == x0 + 2 && hD1.by == kRigY && hD1.bz == z0;
            placeRigBlock(w, x0 + 2, kRigY, z0, BR::DetectorRail, 0);
            const RayHit hD2 = raycastVoxel(w, QVector3D(x0 + 2.5f, kRigY + 3.0f, zc),
                                            QVector3D(0.0f, -1.0f, 0.0f), kSelReach, kSel938);
            famDetector = hD2.valid && hD2.bx == x0 + 2 && hD2.by == kRigY && hD2.bz == z0;
            placeRigBlock(w, x0 + 2, kRigY, z0, BR::Rail, 0); // 还原普通轨（后续腿用）
            okD = famGolden && famDetector;
            if (!okD)
                qInfo().noquote() << "  [t938 diag] d golden" << famGolden << "detector" << famDetector;
            // (e) 相机模式零回归：同一 (a) 射线改走 HitPartial（updateCameraDistance 过滤器）→ 轨为薄板
            //     sub-AABB（y_frac 0.77→0.43 段无实体）→ 穿轨命中后墙（与选体同几何，t605 语义不变）。
            const RayHit hE = raycastVoxel(w, oA, dA, kSelReach, RayFilter::HitPartial);
            okE = hE.valid && hE.bx == x0 + 4 && hE.by == kRigY && hE.bz == z0;
            if (!okE)
                qInfo().noquote() << "  [t938 diag] e" << hE.valid << hE.bx << hE.by << hE.bz;
            // (f) 放置回归（placeBlock 通用推导 tx=hit+normal 的行为级镜像 + 预检复现）：
            //     f1 选轨板（b 命中，法线 +Y）→ 邻格 = 轨正上方 Air → 放石不毁轨；轨格仍是 Rail；
            //     f2 轨格自身：id 非 Air/Water/Lava → 任何「目标须空气/流体」放置预检都拒绝写入轨格。
            const int uX = hB.bx + int(hB.nx), uY = hB.by + int(hB.ny), uZ = hB.bz + int(hB.nz);
            const bool f1Cell = (uX == x0 + 2 && uY == kRigY + 1 && uZ == z0)
                                && w.blockAt(uX, uY, uZ) == BR::Air;
            if (f1Cell) { w.setBlock(uX, uY, uZ, BR::Stone, 0); }
            const bool f1RailIntact = w.blockAt(x0 + 2, kRigY, z0) == BR::Rail;
            if (f1Cell) { w.setBlock(uX, uY, uZ, BR::Air, 0); } // 清理
            const quint8 railId938 = w.blockAt(x0 + 2, kRigY, z0);
            const bool f2NotReplaceable = (railId938 != BR::Air && railId938 != BR::Water
                                           && railId938 != BR::Lava);
            okF = f1Cell && f1RailIntact && f2NotReplaceable;
            if (!okF)
                qInfo().noquote() << "  [t938 diag] f" << f1Cell << f1RailIntact << f2NotReplaceable;
            // 清理（还原空区 + 沉降红石脏标记——动力/探测轨替换走慢路径重算）。
            for (int dy = 0; dy <= 2; ++dy) w.setBlock(x0 + 4, kRigY + dy, z0, BR::Air, 0);
            w.setBlock(x0 + 2, kRigY, z0, BR::Air, 0);
            tickN(w, 2);
        }
        // (g) 源码钉（t983 终局形态：整格特判移除 / 过滤器收敛 / 沿革注 / 选中框薄板分支 / 矿车放置
        //     分支 / 放置预检行，任一消失即红）。
        const QString exeDir938 = QCoreApplication::applicationDirPath();
        const QString root938 = QDir(exeDir938 + QStringLiteral("/..")).absolutePath();
        auto readSrc938 = [&root938](const QString &rel) -> QString {
            QFile f(root938 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString rh938 = readSrc938(QStringLiteral("src/Game/raycast.h"));
        const QString rc938 = readSrc938(QStringLiteral("src/Game/raycast.cpp"));
        const QString pc938 = readSrc938(QStringLiteral("src/Game/playercontroller.cpp"));
        const QString br938 = readSrc938(QStringLiteral("src/Core/blockregistry.cpp"));
        const bool okG1 = rc938.contains(QStringLiteral(
            "|| b == BlockRegistry::Lava || !preciseMode;"));
        const bool okG2 = pc938.contains(QStringLiteral(
            "RayFilter::HitTorch | RayFilter::HitLadder);"));
        const bool okG3 = rh938.contains(QStringLiteral("HitRail（t938 设 / **t983 废**）"));
        const bool okG4 = br938.contains(QStringLiteral("    if (isRail(blockId))\n        return raycastAABBs(blockId, state);"));
        const bool okG5 = pc938.contains(QStringLiteral("bool ok = BlockRegistry::isRail(m_world->blockAt(cx, cy, cz));"))
                       && pc938.contains(QStringLiteral("m_minecartManager->spawnCart(cx, cy, cz, m_world);"));
        const bool okG6 = pc938.contains(QStringLiteral("if (tgt == BlockRegistry::Air || tgt == BlockRegistry::Water || tgt == BlockRegistry::Lava) {"));
        const bool okT938 = okA && okB && okC && okD && okE && okF
                            && okG1 && okG2 && okG3 && okG4 && okG5 && okG6;
        if (!okT938) ++totalFail;
        if (!okT938)
            qInfo().noquote() << "  [t938 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "e" << okE << "f" << okF
                              << "| g" << okG1 << okG2 << okG3 << okG4 << okG5 << okG6
                              << "| srcLen h" << rh938.size() << "c" << rc938.size()
                              << "p" << pc938.size() << "b" << br938.size();
        qInfo().noquote() << (okT938 ? "PASS" : "FAIL")
                          << "| t938 rails pick semantics settled by t983 thin-plate precision: the"
                             " selection hit-box saga ended with the user's sixth-round report -"
                             " standing on a rail and aiming forward to place the next rail, the"
                             " crosshair pointed at the target cell yet the FOOT rail under the"
                             " player was picked (t938's full-cell HitRail made any ray entering the"
                             " rail cell - even through its top-face air band, 15/16 of the cell -"
                             " hit it instantly). Final rule (user iron law: the pick must hit what"
                             " it points at): the rail's real intersection box is the ~2/16 plate"
                             " (raycastAABBs), distance compared by the real box - aiming at the"
                             " plate still selects the rail (digging / minecart placement keep"
                             " working), aiming through the upper air band passes through to the"
                             " target behind; the camera (HitPartial) shares the same plate geometry"
                             " (collision-less rail never pulls the camera, t605 unchanged); the"
                             " RayFilter::HitRail bit and the fullCell special case are removed."
                             " Probe legs: (a) shallow ray through the rail cell's upper air band"
                             " hits the wall behind (t938 full-cell leg inverted - the no"
                             " phantom-foot-rail pin); (b) steep ray onto the plate still hits the"
                             " rail with +Y normal (plate path regression guard); (c) ray through"
                             " the air cell above the rail hits the wall behind (see-through"
                             " preserved); (d) golden and detector rails behave identically via the"
                             " plate path (isRail family); (e) the same (a) ray under the camera"
                             " filter HitPartial hits the wall (camera zero-regression pin, same"
                             " geometry as selection now); (f) placement regression: the rail-plate"
                             " hit + normal targets the air cell above the rail, a stone written"
                             " there leaves the rail intact, and the rail cell itself fails every"
                             " air/fluid-only placement precheck; (g) source pins for the fullCell"
                             " chain without the rail special case, the converged updateRaycast"
                             " filter, the raycast.h supersession note, the selectionAABBs thin"
                             " plate branch, the minecart on-rail placement branch, and the"
                             " placement precheck line"
                          ;
    }

    // ── P-t983 铁轨平行不吸附 + 站轨选块指哪打哪 + review0830-B #12 翻案探针（World setBlock /
    //    raycastVoxel 直调；spec「①三种铁轨平行放置吸附怪异完全不遵守规则——平行相邻轨不互连，只有
    //    端点相对才连接；②站轨上前向放置前向铁轨，准星已指目标格却选中脚底下的轨——选块必须指哪指
    //    哪；review0830-B #12 under-line parallel-track tradeoff 登记项本任务翻案清算」）──
    //   根因分账（三症同域不同源）：
    //   ① 连接判定翻轴偷连：railConnections 旧规则②③④在「既有定向轨（bit5 面向 / 既有连接）偏好轴向
    //      无邻」时翻轴去接垂直旁轨（兜底级联「任取单端救垂直 stub」）——平行双轨互相当对方是唯一旁邻
    //      → 双双被拽翻轴互连（EW 孤轨对被拽成 NS 对）= 「平行放置吸附怪异」精确机制。修 = 既有定向 +
    //      偏好轴无邻 → 保持 0 连接（轴偏守恒）；真孤新轨（state 全零）唯一邻定轴（对己端点相对）。
    //   ② 选块整格抢命中：t938 HitRail 整格口径让射线从轨格顶面进入（上部 15/16 空气）即命中——站轨上
    //      前向放置时脚下轨格抢截射线 = 选不中目标格。修 = HitRail 位与整格特判移除，轨走薄板 sub-AABB
    //      精确命中（真实相交盒 ~2/16，瞄板选中、瞄空气穿透——详见 P-t938 改版注）。
    //   ③ #12：clampShift 链可达闸拿 chainDelta 对拍宽容列扫首轨层 ryTgt——目标列同时有自层延续轨 + 头顶
    //      并行线轨时列扫先摸到上轨 → 恒拒（下层线两车分离被永久钳死）。修 = 目标层精确解（rySelf +
    //      chainDelta 层直接验轨本体 + 下一帧列扫窗顶约束），并行线轨不再劫持判定。
    //   腿：
    //   (a) 平行普通轨：EW 面向孤轨 A + 北侧平行 B → 双方 0 连接（bit5 轴偏守恒；pre-fix 双双被拽成 NS
    //       对 = 红）；端点相对延伸 C（A 东侧）→ A 持 Px、C 持 Nx（合法连线零回归）；
    //   (b) 平行动力轨：同 (a) 布局 → 双方 0 连接；端点相对延伸照连（三轨型覆盖）；
    //   (c) 真孤新轨零回归：state 全零双轨南北相邻 → 各持 Pz/Nz 朝向对方（唯一邻定轴保留）；
    //   (d) 站轨选块：石面上一格脚底轨 + 眼位站轨上朝前下方瞄前向石块顶面 → 命中前向石格（+Y 法线）
    //       而非脚底轨（pre-fix 命中脚底轨 = 红）→ 放置目标 = 前向轨格（hit+normal 推导）；
    //   (e) 源码钉：规则②既有定向 0 连接行 / 规则③显式轴闸 / clampShift 链延续精确层两行 / #12 翻案注。
    {
        // (a)(b)(c) shape 腿 rig 选址：footprint x-2..x+2 × z-2..z+2 × Y-1..Y+1。
        int xa = -1, za = -1;
        for (int zz = 4; zz < 92 && xa < 0; zz += 5)
            for (int xx = 6; xx + 2 < 96 && xa < 0; ++xx) {
                bool clear = true;
                for (int dx = -2; dx <= 2 && clear; ++dx)
                    for (int dz = -2; dz <= 2 && clear; ++dz)
                        for (int dy = -1; dy <= 1 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xa = xx; za = zz; }
            }
        bool okA = false, okB = false, okC = false;
        if (xa < 0) {
            qInfo().noquote() << "  [t983 diag] shape: no clear rig area";
        } else {
            // (a) 平行普通轨（EW 面向孤轨 A + 北侧平行 B，同 bit5）→ 双 0 连接；A 东侧 C → Px/Nx 照连。
            w.setBlock(xa, kRigY, za,     BR::Rail, BR::RailAxisEWFlag);
            w.setBlock(xa, kRigY, za + 1, BR::Rail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conA1 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conB1 = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            const bool axisKeptA = (w.stateAt(xa, kRigY, za) & BR::RailAxisEWFlag) != 0;
            const bool parallelIso = conA1 == 0 && conB1 == 0 && axisKeptA;
            w.setBlock(xa + 1, kRigY, za, BR::Rail, BR::RailAxisEWFlag); // 端点相对延伸 C
            tickN(w, 2);
            const quint8 conA2 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conC = quint8(w.stateAt(xa + 1, kRigY, za) & 0x0F);
            okA = parallelIso && conA2 == BR::RailConnPx && conC == BR::RailConnNx;
            if (!okA)
                qInfo().noquote() << "  [t983 diag] a iso" << parallelIso << "conA1" << conA1
                                  << "conB1" << conB1 << "conA2" << conA2 << "conC" << conC;
            w.setBlock(xa,     kRigY, za,     BR::Air, 0);
            w.setBlock(xa,     kRigY, za + 1, BR::Air, 0);
            w.setBlock(xa + 1, kRigY, za,     BR::Air, 0);
            tickN(w, 2);
            // (b) 平行动力轨（三轨型覆盖；GoldenRail 同 bit5 布局）。
            w.setBlock(xa, kRigY, za,     BR::GoldenRail, BR::RailAxisEWFlag);
            w.setBlock(xa, kRigY, za + 1, BR::GoldenRail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conG1 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conG2 = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            const bool goldenIso = conG1 == 0 && conG2 == 0;
            w.setBlock(xa + 1, kRigY, za, BR::GoldenRail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conG3 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conGC = quint8(w.stateAt(xa + 1, kRigY, za) & 0x0F);
            okB = goldenIso && conG3 == BR::RailConnPx && conGC == BR::RailConnNx;
            if (!okB)
                qInfo().noquote() << "  [t983 diag] b iso" << goldenIso << "conG1" << conG1
                                  << "conG2" << conG2 << "conG3" << conG3 << "conGC" << conGC;
            w.setBlock(xa,     kRigY, za,     BR::Air, 0);
            w.setBlock(xa,     kRigY, za + 1, BR::Air, 0);
            w.setBlock(xa + 1, kRigY, za,     BR::Air, 0);
            tickN(w, 2);
            // (c) 真孤新轨（state 全零）唯一邻定轴零回归：南北相邻双 fresh 轨 → 各持 Pz/Nz 朝向对方。
            w.setBlock(xa, kRigY, za,     BR::Rail, 0);
            w.setBlock(xa, kRigY, za + 1, BR::Rail, 0);
            tickN(w, 2);
            const quint8 conD = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conE = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            okC = conD == BR::RailConnPz && conE == BR::RailConnNz;
            if (!okC)
                qInfo().noquote() << "  [t983 diag] c conD" << conD << "conE" << conE;
            w.setBlock(xa, kRigY, za,     BR::Air, 0);
            w.setBlock(xa, kRigY, za + 1, BR::Air, 0);
            tickN(w, 2);
        }
        // (d) 站轨选块 rig 选址：footprint x-1..x+3 × z-1..z+1 × Y-2..Y+2。
        int xe = -1, ze = -1;
        for (int zz = 3; zz < 94 && xe < 0; zz += 4)
            for (int xx = 6; xx + 3 < 96 && xe < 0; ++xx) {
                bool clear = true;
                for (int dx = -1; dx <= 3 && clear; ++dx)
                    for (int dz = -1; dz <= 1 && clear; ++dz)
                        for (int dy = -2; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xe = xx; ze = zz; }
            }
        bool okD = false;
        if (xe < 0) {
            qInfo().noquote() << "  [t983 diag] d: no clear rig area";
        } else {
            for (int i = 0; i <= 3; ++i) w.setBlock(xe + i, kRigY - 1, ze, BR::Stone, 0); // 石面
            w.setBlock(xe, kRigY, ze, BR::Rail, 0); // 脚底轨（玩家站其上）
            tickN(w, 2);
            // 眼位 = 站轨上（轨板顶 +1.64），准星朝前下方瞄前向石块顶面（(xe+1.5, Y)）：
            //   射线在脚底轨格内段 y_frac 1.0→0.85（薄板上方的空气段）——整格口径（pre-fix）在此抢命中
            //   脚底轨；薄板口径穿透命中前向石格顶面（+Y）→ 放置目标 = hit+normal = 前向轨格。
            const QVector3D oD(float(xe) + 0.5f, float(kRigY) + 1.7f, float(ze) + 0.5f);
            const QVector3D dD(1.0f, -1.7f, 0.0f);
            const RayHit hD = raycastVoxel(w, oD, dD, 8.0f,
                                           RayFilter::HitTorch | RayFilter::HitLadder);
            const bool hitForward = hD.valid && hD.bx == xe + 1 && hD.by == kRigY - 1
                && hD.bz == ze && hD.nx == 0.0f && hD.ny == 1.0f && hD.nz == 0.0f;
            const int tX = hD.valid ? hD.bx + int(hD.nx) : -999;
            const int tY = hD.valid ? hD.by + int(hD.ny) : -999;
            const bool targetForward = tX == xe + 1 && tY == kRigY
                && w.blockAt(tX, tY, ze) == BR::Air; // 放置目标 = 前向轨格（当前 Air 可放）
            okD = hitForward && targetForward;
            if (!okD)
                qInfo().noquote() << "  [t983 diag] d valid" << hD.valid << "hit"
                                  << hD.bx << hD.by << hD.bz << "n" << hD.nx << hD.ny << hD.nz
                                  << "tgt" << tX << tY;
            w.setBlock(xe, kRigY, ze, BR::Air, 0);
            for (int i = 0; i <= 3; ++i) w.setBlock(xe + i, kRigY - 1, ze, BR::Air, 0);
            tickN(w, 2);
        }
        // (e) 源码钉（任一消失即红）。
        const QString exeDir983 = QCoreApplication::applicationDirPath();
        const QString root983 = QDir(exeDir983 + QStringLiteral("/..")).absolutePath();
        auto readSrc983 = [&root983](const QString &rel) -> QString {
            QFile f(root983 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString br983 = readSrc983(QStringLiteral("src/Core/blockregistry.cpp"));
        const QString mc983 = readSrc983(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const bool okE1 = br983.contains(QStringLiteral(
            "|| (sc == 0 && ewPref && (sideState & RailAxisEWFlag) != 0);")); // 规则①平地拐角平行拒连（收口细化：读邻连接位 + EW bit5 显式）
        const bool okE2 = br983.contains(QStringLiteral(
            "if ((curState & RailAxisEWFlag) != 0 || c != 0) {")); // 规则②/③显式轴只连轴上臂闸
        const bool okE3 = mc983.contains(QStringLiteral(
            "const int ryChain = rySelf + chainDelta;"));
        const bool okE4 = mc983.contains(QStringLiteral("review0830-B #12"));
        const bool okT983 = okA && okB && okC && okD && okE1 && okE2 && okE3 && okE4;
        if (!okT983) ++totalFail;
        if (!okT983)
            qInfo().noquote() << "  [t983 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE1 << okE2 << okE3 << okE4;
        qInfo().noquote() << (okT983 ? "PASS" : "FAIL")
                          << "| t983 parallel rails do not snap + standing-on-rail pick hits what it"
                             " points at + review0830-B #12 overturned: (1) railConnections used to"
                             " flip a directed rail's axis to grab a perpendicular side neighbor"
                             " (rules 2/3/4 catch-all cascades), so two PARALLEL rails placed side"
                             " by side each saw the other as their only neighbor and both got yanked"
                             " into a connected pair (user: parallel placement snaps weirdly and"
                             " ignores the rules entirely); now a rail with an explicit orientation"
                             " (bit5 placement facing or existing connection bits) whose preferred"
                             " axis has no neighbors keeps 0 connections (axis metadata conserved) -"
                             " only truly fresh rails orient toward a single neighbor, and only"
                             " endpoint-facing neighbors ever connect; (2) the t938 full-cell rail"
                             " pick let the ray hit the foot rail through the 15/16 air band of its"
                             " cell - standing on a rail and aiming forward at the next cell selected"
                             " the rail underfoot (user: the pick must hit what it points at); the"
                             " HitRail bit and special case are removed and rails use the thin-plate"
                             " sub-AABB (see the P-t938 rewrite for the full saga); (3) the"
                             " clampShift chain-reachability gate compared the chain delta against"
                             " the lenient column-scan first-found layer, which an overhead parallel"
                             " line hijacked - the registered review0830-B #12 tradeoff is settled"
                             " with the precise target layer (rySelf + chainDelta must BE rail and"
                             " sit within the next-frame scan window). Probe legs: (a) parallel"
                             " plain rails stay 0-connection with bit5 conserved, then an"
                             " endpoint-facing east extension connects Px/Nx; (b) same for golden"
                             " rails (three-rail-family coverage); (c) two fresh zero-state rails"
                             " still orient toward each other (single-neighbor defines the axis,"
                             " zero regression); (d) the standing-on-rail forward pick hits the"
                             " forward stone cell with +Y normal and the hit+normal placement target"
                             " is the forward rail cell (pre-fix picked the foot rail = red);"
                             " (e) source pins for the rule-2/rule-3 explicit-axis gates, the"
                             " precise chain layer lines and the #12 registration marker"
                          ;
    }

    // ── P-t1018 铁轨延伸松弛（t983 回炉）探针（World setBlock 直编；spec「沿轨线端点延伸铺设时，新轨
    //    自动接续既有轨——连接由邻轨端点拓扑决定，放置朝向无关；平行侧邻拒连禁令不回退（t983 主口径
    //    保住）」；用户原诉「现在延伸铺设不管朝向、接不上线」）──
    //   根因：t983 显式轴闸（bit5 放置面向 / c 连接位）在「偏好轴无邻」时一律 0 连接——放置面向恰与
    //   轨线轴相反时，端点延伸的新轨持错误轴偏好落 0 连接（且继续延伸仍落 0，轨线从此断在延伸点）。
    //   修 = 延伸松弛：仅 c==0（bit5 纯放置面向）轨偏好轴零臂时，垂直轴「端点相对」邻
    //   （railProbeEndpointAligned 单表判据：邻轨轴〔连接位优先 bit5 兜底；坡臂 review0906 #5 起读
    //   对应层 state，不再恒真〕含连接方向 = 轨线
    //   端点对本格）照连，bit5 随实连轴镜像（World 写回单轴镜像扩展）；平行侧邻（邻轴垂直连接方向）
    //   仍拒连 = t983 不回退；c!=0 轨既有连接即实拓扑，永不松。
    //   腿：
    //   (a) NS 轨线端点延伸双向：EW 面向（bit5，错误轴）放置 → 接续成线（新轨持 Nz + bit5 镜像清零 +
    //       线端轨 Pz|Nz 互连）；NS 面向（state 0）放置 → 照常接续（唯一邻定轴零回归）——两向全接 =
    //       朝向无关；
    //   (b) 1 格断桥补接：普通轨断桥 EW 面向补接 → Pz|Nz 双臂贯穿 + 两断端闭合；动力轨同布局（三轨型
    //       覆盖）；
    //   (c) 平行侧邻拒连不回退（t1018 自守卫）：EW 面向孤轨 + 北侧 EW 面向平行轨 → 双 0 连接 + bit5
    //       守恒（P-t983 腿 a 同构复钉，防松弛翻轴回潮）；
    //   (d) 源码钉：端点相对判定实现 / 规则②③松弛行 / axisCon 守卫 / World 单轴镜像行。
    {
        // shape 腿 rig 选址：footprint x-2..x+2 × z-2..z+6 × Y-1..Y+2。
        int xa = -1, za = -1;
        for (int zz = 3; zz < 90 && xa < 0; zz += 4)
            for (int xx = 6; xx + 2 < 96 && xa < 0; ++xx) {
                bool clear = true;
                for (int dx = -2; dx <= 2 && clear; ++dx)
                    for (int dz = -2; dz <= 6 && clear; ++dz)
                        for (int dy = -1; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { xa = xx; za = zz; }
            }
        bool okA = false, okB = false, okC = false, okE5 = false;
        if (xa < 0) {
            qInfo().noquote() << "  [t1018 diag] shape: no clear rig area";
        } else {
            // (a) NS 线（z..z+2 三格 fresh）→ EW 面向延伸 (x,z+3) 接续；再清场 NS 面向延伸零回归。
            auto buildLine = [&]() {
                for (int i = 0; i <= 2; ++i) w.setBlock(xa, kRigY, za + i, BR::Rail, 0);
                tickN(w, 2);
            };
            auto clearLine = [&]() {
                for (int i = 0; i <= 3; ++i) w.setBlock(xa, kRigY, za + i, BR::Air, 0);
                tickN(w, 2);
            };
            buildLine();
            const quint8 conEnd0 = quint8(w.stateAt(xa, kRigY, za + 2) & 0x0F);
            w.setBlock(xa, kRigY, za + 3, BR::Rail, BR::RailAxisEWFlag); // EW 面向（错误轴）延伸
            tickN(w, 2);
            const quint8 stN1 = w.stateAt(xa, kRigY, za + 3);
            const quint8 conN1 = quint8(stN1 & 0x0F);
            const bool ewKept1 = (stN1 & BR::RailAxisEWFlag) != 0;
            const quint8 conEnd1 = quint8(w.stateAt(xa, kRigY, za + 2) & 0x0F);
            const bool ewFacingConnect = conEnd0 == BR::RailConnNz && conN1 == BR::RailConnNz
                && !ewKept1 && conEnd1 == quint8(BR::RailConnNz | BR::RailConnPz);
            clearLine();
            buildLine();
            w.setBlock(xa, kRigY, za + 3, BR::Rail, 0); // NS 面向（state 0 = fresh）延伸
            tickN(w, 2);
            const quint8 conN2 = quint8(w.stateAt(xa, kRigY, za + 3) & 0x0F);
            const quint8 conEnd2 = quint8(w.stateAt(xa, kRigY, za + 2) & 0x0F);
            const bool nsFacingConnect = conN2 == BR::RailConnNz
                && conEnd2 == quint8(BR::RailConnNz | BR::RailConnPz);
            okA = ewFacingConnect && nsFacingConnect;
            if (!okA)
                qInfo().noquote() << "  [t1018 diag] a end0" << conEnd0 << "conN1" << conN1
                                  << "ewKept" << ewKept1 << "end1" << conEnd1
                                  << "conN2" << conN2 << "end2" << conEnd2;
            clearLine();
            // (b) 1 格断桥补接：普通轨 z / z+2 两断端 + EW 面向补接 (x,z+1) → Pz|Nz 贯穿闭合；
            //     动力轨同布局（三轨型覆盖）。
            w.setBlock(xa, kRigY, za,     BR::Rail, 0);
            w.setBlock(xa, kRigY, za + 2, BR::Rail, 0);
            tickN(w, 2);
            w.setBlock(xa, kRigY, za + 1, BR::Rail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conBr = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            const quint8 conS0 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conS2 = quint8(w.stateAt(xa, kRigY, za + 2) & 0x0F);
            const bool railBridge = conBr == quint8(BR::RailConnPz | BR::RailConnNz)
                && conS0 == BR::RailConnPz && conS2 == BR::RailConnNz;
            for (int i = 0; i <= 2; ++i) w.setBlock(xa, kRigY, za + i, BR::Air, 0);
            tickN(w, 2);
            w.setBlock(xa, kRigY, za,     BR::GoldenRail, 0);
            w.setBlock(xa, kRigY, za + 2, BR::GoldenRail, 0);
            tickN(w, 2);
            w.setBlock(xa, kRigY, za + 1, BR::GoldenRail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conGBr = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            const quint8 conGS0 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conGS2 = quint8(w.stateAt(xa, kRigY, za + 2) & 0x0F);
            // 金轨断端 = 单臂（规则② fresh 级联 `if (hasPZ) return RailConnPz`，与普通轨断端同构）：
            //   矩阵 run1 实证 conGS0=4(Pz)/conGS2=8(Nz)——「断端 Pz|Nz」旧预期是对向双臂拓扑误写
            //   （金轨桥本体 Pz|Nz 贯穿接续才是本腿断言面）。
            const bool goldenBridge = conGBr == quint8(BR::RailConnPz | BR::RailConnNz)
                && conGS0 == BR::RailConnPz && conGS2 == BR::RailConnNz;
            okB = railBridge && goldenBridge;
            if (!okB)
                qInfo().noquote() << "  [t1018 diag] b rail" << railBridge << conBr << conS0 << conS2
                                  << "golden" << goldenBridge << conGBr << conGS0 << conGS2;
            for (int i = 0; i <= 2; ++i) w.setBlock(xa, kRigY, za + i, BR::Air, 0);
            tickN(w, 2);
            // (c) 平行侧邻拒连不回退（t1018 自守卫，P-t983 腿 a 同构复钉）。
            w.setBlock(xa, kRigY, za,     BR::Rail, BR::RailAxisEWFlag);
            w.setBlock(xa, kRigY, za + 1, BR::Rail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conP1 = quint8(w.stateAt(xa, kRigY, za) & 0x0F);
            const quint8 conP2 = quint8(w.stateAt(xa, kRigY, za + 1) & 0x0F);
            const bool axisKept = (w.stateAt(xa, kRigY, za) & BR::RailAxisEWFlag) != 0;
            okC = conP1 == 0 && conP2 == 0 && axisKept;
            if (!okC)
                qInfo().noquote() << "  [t1018 diag] c" << conP1 << conP2 << axisKept;
            w.setBlock(xa, kRigY, za,     BR::Air, 0);
            w.setBlock(xa, kRigY, za + 1, BR::Air, 0);
            tickN(w, 2);
            // (e) review0906 #5 上层垂直定向线腿（坡臂分支行为覆盖 —— 旧腿全为同层邻居）：
            //     上层既有 EW 线（fresh 自连：中段 Px|Nx、东端 Nx）+ 东端**正南一格下层**的 EW 面向
            //     （bit5）fresh 轨 —— 该轨偏好轴（X）零臂、垂直轴（Z）唯一邻是「上层的东端」（坡臂，
            //     upState=Nx）：线轴垂直于连接方向 = 非端点相对，坡臂必须拒连（旧「坡臂恒真」让该轨持
            //     Nz 单向幽灵臂；端点自身 nArm=2 走 axisCon 存在性路径不回连 → mesher 翘头坡 / 矿车
            //     单向驶入不可返；**不取线中段下方**——中段会经规则⑤ 3 臂 T 交叉重排成转辙器，
            //     与本修复无关）。阳性对照：线上游净空区上层 fresh stub（state 0 → NS 保守读 =
            //     端点相对）坡臂照连成合法坡段（拒绝非一刀切）。
            for (int i = -1; i <= 1; ++i)
                w.setBlock(xa + i, kRigY + 1, za + 1, BR::Rail, 0); // 上层 EW 线（fresh 自连成线）
            tickN(w, 2);
            const quint8 conLineMid5 = quint8(w.stateAt(xa, kRigY + 1, za + 1) & 0x0F);
            const quint8 conLineEnd5 = quint8(w.stateAt(xa + 1, kRigY + 1, za + 1) & 0x0F);
            w.setBlock(xa + 1, kRigY, za + 2, BR::Rail, BR::RailAxisEWFlag); // 幽灵臂施害轨（东端正南下层）
            tickN(w, 2);
            const quint8 stGhost5 = w.stateAt(xa + 1, kRigY, za + 2);
            const quint8 conGhost5 = quint8(stGhost5 & 0x0F);
            const quint8 conLineMid5b = quint8(w.stateAt(xa, kRigY + 1, za + 1) & 0x0F);
            const quint8 conLineEnd5b = quint8(w.stateAt(xa + 1, kRigY + 1, za + 1) & 0x0F);
            // 阳性对照（线上游净空区，与线隔 2 格无探针耦合）：上层 fresh stub + 其 **−Z 向**下层的
            //     EW 面向轨 —— stub 对下轨是 −Z 单臂（fresh 级联 → Nz=8），下轨对 stub 是 +Z 坡臂
            //     （fresh stub upState=0 → NS 保守读 = 端点相对 → Pz=4）= 两向互连合法坡段。
            w.setBlock(xa - 1, kRigY + 1, za - 1, BR::Rail, 0); // 上层 fresh stub
            tickN(w, 2);
            w.setBlock(xa - 1, kRigY, za - 2, BR::Rail, BR::RailAxisEWFlag);
            tickN(w, 2);
            const quint8 conPos5 = quint8(w.stateAt(xa - 1, kRigY, za - 2) & 0x0F);
            const quint8 conStub5 = quint8(w.stateAt(xa - 1, kRigY + 1, za - 1) & 0x0F);
            okE5 = conLineMid5 == quint8(BR::RailConnPx | BR::RailConnNx) && conLineEnd5 == BR::RailConnNx
                && conGhost5 == 0                                        // 幽灵臂拒连（保持 stub）
                && (stGhost5 & BR::RailAxisEWFlag) != 0                  // bit5 守恒（0 连接不翻轴）
                && conLineMid5b == conLineMid5 && conLineEnd5b == conLineEnd5 // 线端不回连（前提自洽）
                && conPos5 == BR::RailConnPz && conStub5 == BR::RailConnNz; // 阳性坡段互连（+Z 下轨 / −Z stub）
            if (!okE5)
                qInfo().noquote() << "  [t1018 diag] e line" << conLineMid5 << "end" << conLineEnd5
                                  << "ghost" << conGhost5
                                  << "axisKept" << int(stGhost5 & BR::RailAxisEWFlag)
                                  << "line2" << conLineMid5b << "end2" << conLineEnd5b
                                  << "pos" << conPos5 << "stub" << conStub5;
            for (int i = -1; i <= 1; ++i)
                w.setBlock(xa + i, kRigY + 1, za + 1, BR::Air, 0);
            w.setBlock(xa + 1, kRigY, za + 2, BR::Air, 0);
            w.setBlock(xa - 1, kRigY + 1, za - 1, BR::Air, 0);
            w.setBlock(xa - 1, kRigY, za - 2, BR::Air, 0);
            tickN(w, 2);
        }
        // (d) 源码钉（任一消失即红）。B-P1-1 迁移：滤注释钉（pinSet）；原 okD4 钉串拼进尾注释
        //     「// t1018 单 Z 臂…」（尾注释措辞改动即误红的隐患钉，B-P2-2 同族核查确认）→ 改钉
        //     语句本体 minCount 2（贯穿 Z + 单 Z 臂两处镜像行全须在场）+ 单 Z 臂守卫行
        //     （A-P3-1 交叉轴零位守卫，语句本体不变仍钉）。
        const QString root1018 = QDir(QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/..")).absolutePath();
        const QStringList missDbr = pinSet(root1018 + QStringLiteral("/src/Core/blockregistry.cpp"), {
            {"fn-railProbeEndpointAligned", "bool BlockRegistry::railProbeEndpointAligned(const RailProbe &p, bool xAxis)"},
            {"rule2-endpoint-relax", "(hasPZ && railProbeEndpointAligned(pz, false) ? RailConnPz : 0)"}, // 规则②③延伸松弛行
            {"axis-arm-first-guard", "if (axisCon != 0 || c != 0) return axisCon;"}, // 轴上臂优先 / c!=0 永不松守卫
            {"slope-arm-layer-state", "const quint8 slopeState = isRail(p.up) ? p.upState : p.downState;"}, // review0906 #5 坡臂读对应层 state
            {"slope-arm-corner-guard-x", "|| railProbeEndpointAligned(hasPX ? px : nx, true)"}, // review0906 #5 规则① t982 坡优先路径坡臂端点判读（合法演化：19:34 起 xArmOk 表格中段——slopeFresh 承接行尾分号，钉缩为表达式本体）
            {"slope-arm-corner-guard-z", "|| railProbeEndpointAligned(hasPZ ? pz : nz, false)"}, // review0906 #5 规则① t982 坡优先路径坡臂端点判读（同上 z 镜像行）
        });
        const QStringList missDwd = pinSet(root1018 + QStringLiteral("/src/World/world.cpp"), {
            {"zmirror-single-guard", "(con & (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz)) != 0"}, // 单 Z 臂分支（!= 0；贯穿分支为 ==）
            {"zmirror-pref-stmt", "con = quint8(con & quint8(~BlockRegistry::RailAxisEWFlag));", 2}, // 贯穿 Z + 单 Z 臂两镜像行
            {"cross-axis-zero-guard", "&& (con & (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx)) == 0)"}, // A-P3-1 守卫
        });
        const bool okD = missDbr.isEmpty() && missDwd.isEmpty();
        const bool okT1018 = okA && okB && okC && okD;
        if (!okT1018) ++totalFail;
        if (!okT1018)
            qInfo().noquote() << "  [t1018 diag] a" << okA << "b" << okB << "c" << okC
                              << "| d br:" << missDbr.join(QLatin1Char(','))
                              << "wd:" << missDwd.join(QLatin1Char(','));
        qInfo().noquote() << (okT1018 ? "PASS" : "FAIL")
                          << "| t1018 rail extension relaxation (t983 rework): extending a line at"
                             " its endpoint used to depend on placement facing - a new rail placed"
                             " with the facing axis perpendicular to the line carried the wrong bit5"
                             " axis preference, the explicit-axis gate (t983) kept it at 0"
                             " connections and the line stayed broken at the extension point no"
                             " matter how many more rails the player laid (user: extending never"
                             " connects regardless of facing). Fix relaxes exactly that case: a rail"
                             " whose state is placement-facing only (c == 0) with zero neighbors on"
                             " its preferred axis connects along the perpendicular axis when the"
                             " neighbor there is ENDPOINT-relative (neighbor's own axis contains the"
                             " joining direction - railProbeEndpointAligned single table: connection"
                             " bits first, bit5 fallback, slope arms read the actual layer state"
                             " (review0906 #5)), and bit5 mirrors"
                             " the actually-connected axis on write-back (single-axis mirror"
                             " extension); parallel SIDE neighbors (neighbor axis perpendicular to"
                             " the joining direction) are still rejected - the t983 no-snap rule is"
                             " NOT rolled back, and rails with existing connection bits (c != 0)"
                             " never relax (their connections are the real topology). Probe legs:"
                             " (a) NS line endpoint extension connects both ways - EW-facing"
                             " (wrong-axis bit5) placement joins the line (Nz + bit5 mirrored clear,"
                             " end rail closes Pz/Nz) and fresh NS placement still joins (single-"
                             " neighbor axis, zero regression) = facing-independent; (b) a 1-gap"
                             " bridge filled with an EW-facing rail closes both ends through Pz|Nz,"
                             " same layout with golden rails (three-rail-family coverage); (c)"
                             " parallel EW-facing rails stay mutually 0-connection with bit5"
                             " conserved (t983 leg-a isomorph re-pinned against axis-flip"
                             " regression); (d) source pins for the endpoint-aligned predicate, the"
                             " rule-2/rule-3 relaxation lines, the axisCon guard and the World"
                             " single-axis bit5 mirror"
                          ;
        if (!okE5) ++totalFail;
        qInfo().noquote() << (okE5 ? "PASS" : "FAIL")
                          << "| t1018(e) slope-arm layer-state leg (review0906 #5): an up-layer"
                             " PERPENDICULAR directed line (EW mid-rail, c = Px|Nx) must not"
                             " accept a slope arm from a fresh EW-facing rail one block below -"
                             " the old slope-arm branch returned true unconditionally (RailProbe"
                             " structurally lacked up/down layer state), so the new rail kept a"
                             " one-way ghost connection the line never reciprocates (its c != 0"
                             " axisCon existence path cannot add the down link): mesher drew a"
                             " head-tilted ramp and carts drove in one direction only. The slope"
                             " branch now reads the actual layer state (upState/downState filled"
                             " by the runtime recompute) EVERYWHERE slope arms are consumed -"
                             " the rule-2/3 relaxation table AND the rule-1 t982 corner slope-"
                             " priority path (a ghost slope arm no longer hijacks the line end's"
                             " existing connection and sever the line: the legitimate arm is"
                             " kept as a single connection instead): connection bits must"
                             " contain the joining axis, else reject (ghost rail stays a"
                             " 0-connection stub"
                             " with bit5 conserved, line unchanged); a fresh up-layer stub"
                             " (state 0, conservative NS read = endpoint-relative) still forms"
                             " the legitimate two-way slope segment (positive control against"
                             " blanket rejection)"
                          << (okE5 ? QString()
                                   : QStringLiteral("diag see [t1018 diag] e"));
    }

    // ── P-t939 单格坡静置矿车下滑规则探针（MinecartManager 直编；spec「单格上/下坡静置矿车仍静止——
    //    应往下坡运动。口径（用户定稿）：未激活动力轨=减速可平衡坡上；普通轨=下滑；激活动力轨+探测轨=
    //    往下坡运动」）──
    //   根因：t909② 静置闸只读「邻轨层差」（连续坡每格 ±1）——单格坡（平轨里嵌一格凸/凹）的坡度全部
    //   落在本格面上（railRiseAt 的 fx 线性坡），本格邻轨探针读 {上坡侧 +1, 平侧 0}，两头都无 -1 → 被
    //   当平地停驻。修 = 静置闸 + 滑行坡向两处都补**本格面梯度**（cartRailGradient：与 Y 钉定/俯仰同一
    //   张面 ±kCartPitchProbe 采样）+ 轨型闸（未激活动力轨 brake 刹住坡上车）。
    //   rig：驼峰线（z=z0 行）x0-5..x0-2 平轨引道 + x0-1 上坡 incline + x0@Y+1 峰 + x0+1 下坡 incline +
    //   x0+2..x0+3 平轨引出；独立电源行（z=z0+2）x0-2 平轨 + x0-1 通电动力轨（下 RedstoneBlock 直供）。
    //   腿：(a) 单格上坡（x0-1 普通轨）静车 → 西向下坡滚 ≥1.5 格（kick-only 摩擦滑 ~0.5 格 → 阈值钉
    //           「滑行半边的本格面重力覆盖」也在：自然滚落非蠕动）；
    //       (b) 单格下坡（x0+1 普通轨）静车 → 东向 ≥1.5 格；
    //       (c) 未激活动力轨同场景（x0-1 换 GoldenRail 无源）→ 位移 ≈0（口径①「减速可平衡坡上」钉）；
    //       (d) 通电动力轨同场景（下方 RedstoneBlock 直供）→ 西向 ≥1.5 格（口径③ 动力轨半边：起步后
    //           t735④ boost 接管）；
    //       (e) 探测轨（x0+1 换 DetectorRail）→ 东向 ≥1.5 格（口径③ 探测轨半边）；
    //       (f) 平地阴性：平轨 / 通电动力轨平地静车位移 ≈0（t735④「平地静置空车不被动力轨弹射」+
    //           「下滑只发生在有下坡分量」）；滚落车 y 贴回平轨面（无悬浮）；
    //       (g) 源码钉：brake 闸 / 静置闸梯度消费 / 滑行梯度覆盖 / helper 实现 / 头文件阈值常量。
    {
        // rig 选址：运行期扫描空区（t909 模式）。footprint x0-6..x0+4 × z0-1..z0+3 × kRigY-2..kRigY+3
        //   （含 RedstoneBlock 层 Y-1 与峰 Y+1）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 6; xx + 4 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -6; dx <= 4 && clear; ++dx)
                    for (int dz = -1; dz <= 3 && clear; ++dz)
                        for (int dy = -2; dy <= 3 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        bool okA = false, okB = false, okC = false, okD = false, okE = false, okF = false;
        if (x0 < 0) {
            qInfo().noquote() << "  [t939 diag] no clear rig area found";
        } else {
            const auto clearSlopeRow = [&]() {
                for (int i = -5; i <= 3; ++i)
                    w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);          // 引道 + 两 incline（含峰前格）
                w.setBlock(x0, kRigY + 1, z0, BR::Air, 0);              // 峰
                w.setBlock(x0 - 1, kRigY - 1, z0, BR::Air, 0);          // 相位 3 直供源（若有）
            };
            // ── 相位 1：普通轨驼峰 + 平地阴性（腿 a/b/f）──
            for (int i = -5; i <= 3; ++i)
                if (i != 0) placeRigBlock(w, x0 + i, kRigY, z0, BR::Rail, 0);
            placeRigBlock(w, x0, kRigY + 1, z0, BR::Rail, 0);           // 峰（两侧低一格的单格凸）
            // 电源行：平轨 + 通电动力轨（下 RedstoneBlock 直供 —— t909 rig 同款）。
            placeRigBlock(w, x0 - 1, kRigY - 1, z0 + 2, BR::RedstoneBlock, 0);
            placeRigBlock(w, x0 - 2, kRigY, z0 + 2, BR::Rail, 0);
            placeRigBlock(w, x0 - 1, kRigY, z0 + 2, BR::GoldenRail, 0);
            tickN(w, 4);
            const bool flatPoweredOk =
                (w.stateAt(x0 - 1, kRigY, z0 + 2) & BR::GoldenRailStateOnFlag) != 0;
            {
                MinecartManager carts;
                carts.spawnCart(x0 - 1, kRigY, z0, &w);                 // A：单格上坡（槽 0）
                carts.spawnCart(x0 + 1, kRigY, z0, &w);                 // B：单格下坡（槽 1）
                carts.spawnCart(x0 - 2, kRigY, z0 + 2, &w);             // F1：平轨阴性（槽 2）
                carts.spawnCart(x0 - 1, kRigY, z0 + 2, &w);             // F2：通电动力轨平地（槽 3）
                const float ax0 = carts.posAt(0).x(), bx0 = carts.posAt(1).x();
                const float f1x0 = carts.posAt(2).x(), f2x0 = carts.posAt(3).x();
                for (int t = 0; t < 400; ++t) carts.tickPushedCarts(0.016f, &w);
                const float dA = ax0 - carts.posAt(0).x();              // 西向位移（正 = 向下坡）
                const float dB = carts.posAt(1).x() - bx0;              // 东向位移
                // (a) 单格上坡：西滚 ≥1.5（kCartSlopeKick-only 摩擦滑 ~0.5 格 → 阈值同时钉滑行半边的
                //     本格面重力覆盖）；终位贴平轨引道面（无半坡悬浮）。
                okA = dA >= 1.5f
                    && std::fabs(carts.posAt(0).y() - (float(kRigY) + 0.45f)) < 0.03f
                    && carts.posAt(0).x() > float(x0) - 5.5f;           // 留在 rig 内（未飞出死端）
                // (b) 单格下坡：东滚 ≥1.5。
                okB = dB >= 1.5f
                    && std::fabs(carts.posAt(1).y() - (float(kRigY) + 0.45f)) < 0.03f
                    && carts.posAt(1).x() < float(x0) + 4.0f;
                // (f) 平地阴性：普通平轨 + 通电动力轨平地静车都 ≈0（t735④ 弹射豁免不破 —— 下滑只发生
                //     在有下坡分量；flatPoweredOk 先钉动力轨确已通电，防腿空转）。
                okF = flatPoweredOk
                    && std::fabs(carts.posAt(2).x() - f1x0) <= 0.02f
                    && std::fabs(carts.posAt(3).x() - f2x0) <= 0.02f;
                if (!okA || !okB || !okF)
                    qInfo().noquote() << "  [t939 diag] p1 dA" << dA << "dB" << dB
                                      << "f1" << std::fabs(carts.posAt(2).x() - f1x0)
                                      << "f2" << std::fabs(carts.posAt(3).x() - f2x0)
                                      << "pow" << flatPoweredOk
                                      << "A" << carts.posAt(0) << "B" << carts.posAt(1);
                carts.clearAll();
            }
            // ── 相位 2：未激活动力轨平衡钉（腿 c；口径①「减速可平衡坡上——车停得住」）──
            placeRigBlock(w, x0 - 1, kRigY, z0, BR::GoldenRail, 0);     // 上坡 incline 换断电动力轨
            tickN(w, 2);
            {
                MinecartManager carts;
                carts.spawnCart(x0 - 1, kRigY, z0, &w);
                const float cx0 = carts.posAt(0).x(), cy0 = carts.posAt(0).y();
                for (int t = 0; t < 400; ++t) carts.tickPushedCarts(0.016f, &w);
                okC = std::fabs(carts.posAt(0).x() - cx0) <= 0.02f
                    && std::fabs(carts.posAt(0).y() - cy0) <= 0.02f;    // 停在坡面原位（不被推离 incline）
                if (!okC)
                    qInfo().noquote() << "  [t939 diag] p2 d"
                                      << std::fabs(carts.posAt(0).x() - cx0)
                                      << "dy" << std::fabs(carts.posAt(0).y() - cy0);
                carts.clearAll();
            }
            // ── 相位 3：通电动力轨同场景（腿 d；口径③ 动力轨半边 —— 有下坡分量才动）──
            placeRigBlock(w, x0 - 1, kRigY - 1, z0, BR::RedstoneBlock, 0); // 直供源（红石块是合法支撑）
            tickN(w, 4);
            const bool inclinePoweredOk =
                (w.stateAt(x0 - 1, kRigY, z0) & BR::GoldenRailStateOnFlag) != 0;
            {
                MinecartManager carts;
                carts.spawnCart(x0 - 1, kRigY, z0, &w);
                const float dx0 = carts.posAt(0).x();
                for (int t = 0; t < 400; ++t) carts.tickPushedCarts(0.016f, &w);
                const float dD = dx0 - carts.posAt(0).x();
                okD = inclinePoweredOk && dD >= 1.5f
                    && carts.posAt(0).x() > float(x0) - 5.5f;           // 加速滑入引道后摩擦停（未飞出）
                if (!okD)
                    qInfo().noquote() << "  [t939 diag] p3 dD" << dD << "pow" << inclinePoweredOk
                                      << "D" << carts.posAt(0);
                carts.clearAll();
            }
            // ── 相位 4：探测轨同场景（腿 e；口径③ 探测轨半边 —— 下坡 incline 换 DetectorRail）──
            placeRigBlock(w, x0 + 1, kRigY, z0, BR::DetectorRail, 0);
            {
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0, &w);
                const float ex0 = carts.posAt(0).x();
                for (int t = 0; t < 400; ++t) carts.tickPushedCarts(0.016f, &w);
                const float dE = carts.posAt(0).x() - ex0;
                okE = dE >= 1.5f && carts.posAt(0).x() < float(x0) + 4.0f;
                if (!okE)
                    qInfo().noquote() << "  [t939 diag] p4 dE" << dE << "E" << carts.posAt(0);
                carts.clearAll();
            }
            // 清场（先拆轨再拆源 —— 源格编辑会触发上方轨失撑坍落；全部拆完即等效）。
            clearSlopeRow();
            w.setBlock(x0 - 2, kRigY, z0 + 2, BR::Air, 0);
            w.setBlock(x0 - 1, kRigY, z0 + 2, BR::Air, 0);
            w.setBlock(x0 - 1, kRigY - 1, z0 + 2, BR::Air, 0);
            tickN(w, 2);
        }
        // (g) 源码钉（t935/t936 模式：字符串钉——brake 闸 / 静置闸梯度消费 / 滑行梯度覆盖 / helper
        //     实现 / 头文件阈值常量，任一消失即红）。
        const QString exeDir939 = QCoreApplication::applicationDirPath();
        const QString root939 = QDir(exeDir939 + QStringLiteral("/..")).absolutePath();
        auto readSrc939 = [&root939](const QString &rel) -> QString {
            QFile f(root939 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc939 = readSrc939(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const QString mh939 = readSrc939(QStringLiteral("src/Entities/minecartmanager.h"));
        const bool okG1 = mc939.contains(
            QStringLiteral("(world->stateAt(sx, ry, sz) & BlockRegistry::GoldenRailStateOnFlag) == 0)"));
        // review0830 #8 适配（依据：静置闸梯度消费行前加 4 邻 ±1 层廉价预筛 maySlope —— 采样语义
        //   与阈值不变，仅平地车早退；钉的仍是同一消费点的修后形态）。
        const bool okG2 = mc939.contains(
            QStringLiteral("if (maySlope && cartRailGradient(world, c.pos, ry, c.dirX, c.dirZ, grad)) {"));
        const bool okG3 = mc939.contains(
            QStringLiteral("if (cartRailGradient(world, c.pos, ry, c.dirX * float(gs), c.dirZ * float(gs), grad)) {"));
        const bool okG4 = mc939.contains(
            QStringLiteral("bool MinecartManager::cartRailGradient(World *world, const QVector3D &pos, int railY,"));
        const bool okG5 = mh939.contains(QStringLiteral("static constexpr float kCartSlopeGradMin = 0.1f;"));
        const bool okT939 = okA && okB && okC && okD && okE && okF
                            && okG1 && okG2 && okG3 && okG4 && okG5;
        if (!okT939) ++totalFail;
        if (!okT939)
            qInfo().noquote() << "  [t939 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "e" << okE << "f" << okF
                              << "| g" << okG1 << okG2 << okG3 << okG4 << okG5
                              << "| srcLen cpp" << mc939.size() << "h" << mh939.size();
        qInfo().noquote() << (okT939 ? "PASS" : "FAIL")
                          << "| t939 stationary carts slide down single-block slopes: the t909 static-start "
                             "gate only read NEIGHBOR rail layer deltas (continuous slopes step +-1 per "
                             "cell), so a single-block hump/dip embedded in a flat line - where the entire "
                             "gradient lives on the cart's OWN cell surface (railRiseAt's fx ramp, neighbor "
                             "probes read {uphill +1, flat 0}) - was classified flat and the cart stayed "
                             "parked (user report). Fix adds a same-surface gradient sample "
                             "(cartRailGradient, the very surface Y-pinning/pitch sampling reads, "
                             "+-kCartPitchProbe window) to BOTH the static-start gate and the sliding "
                             "slope classification (kick + slope-gravity roll, not friction creep), gated "
                             "by rail type per the user's final rules: unpowered golden rail = brake that "
                             "holds a parked cart on a slope (rule 1), plain/detector/powered rails roll "
                             "downhill (rules 2+3, powered lerp takes over after the start); flat ground "
                             "has no downhill component so all rail types stay parked and the t735(4) "
                             "no-launch exemption survives. Probe legs: (a) single up-slope plain rail "
                             "rolls west >=1.5 (kick-only coast is ~0.5, so the threshold also pins the "
                             "sliding-half gravity overlay), ends glued to the flat lead surface, stays "
                             "inside the rig; (b) single down-slope rolls east >=1.5; (c) unpowered "
                             "golden rail on the same slope: zero displacement (balance pin); (d) powered "
                             "golden rail (RedstoneBlock direct feed, flag verified) rolls west >=1.5; "
                             "(e) detector rail rolls east >=1.5; (f) flat plain rail and flat POWERED "
                             "golden rail carts stay put (t735(4) negative regression); (g) source pins "
                             "for the brake gate, both gradient consumption sites, the helper, and the "
                             "threshold constant"
                          ;
    }

    // ── P-t940 脱轨车近轨吸附探针（MinecartManager 直编；spec「玩家身体碰撞把矿车推到旁边铁轨上时，
    //    矿车自动吸附回轨恢复正常移动形态（近轨 snap）」）──
    //   根因：脱轨 / 地面车被推进轨格列后仍走 tickDerailedCart 自由物理（贴地高度穿轨板滑行、沿轨推
    //   也走 derailed 分支）——轨只「接住」了位置，没接回移动形态。修 = tickDerailedCart 收尾近轨吸附
    //   trySnapDerailedToRail（同层闸 Δ=0 + pickTrackStep 死端外向不吸 + 复用 pinCartY / updateCartPitch
    //   同一套钉定）。断言五段：
    //   (a) 横推上轨（用户场景）：轨南侧地面车被北推 → 吸回 EW 轨（轨心对齐 |x/z−0.5| + 骑乘高 rideH
    //       —— groundH 0.3875 差 0.0625 为形态签名）+ 停驻轨上不北漂；吸回后推手继续贴推（横推 =
    //       t908 no-op，不二次脱轨）；
    //   (b) 沿轨推上轨（西引道东推入轨端）：吸附保留沿轨速度（沿轨前进 ≥1.2 格贴 rideH 滑行至磨停，
    //       终态轨上形态——旧代码 groundH 穿轨滑行出线）；
    //   (c) 阴性·t908/t863④ 推离不吸回：西死端车沿轴外推 → 出轨西滑，落定轨外地面 groundH（不被吸回
    //       轨上 rideH）；续跑 100 tick 位不动（平地脱轨车远离轨不吸附）；
    //   (d) 坡轨吸附：脱轨车西推入 NS 坡格（北邻 +1）→ 吸附 Y 钉**坡面**（railY + rise(0.5) + rideH
    //       = +0.95，非平地高度 +0.45）+ X 钉轨心线 + 俯仰 45°（放置即贴坡同函数）；
    //   (e) 源码钉：snap 调用点 / helper 定义与声明（防静默移除）。
    {
        // rig 选址：运行期扫描空区（t908 模式）。footprint x0-6..x0+3 × z0-1..z0+4 × kRigY-2..kRigY+2
        //   （(c) 西滑走廊地板铺到 x0-5 —— 推离 4 blocks/s / 摩擦 ≤2/s 最远 ~4 格）。
        int x0 = -1, z0 = -1;
        for (int zz = 3; zz < 94 && x0 < 0; zz += 2)
            for (int xx = 8; xx + 3 < 96 && x0 < 0; xx += 2) {
                bool clear = true;
                for (int dx = -6; dx <= 3 && clear; ++dx)
                    for (int dz = -1; dz <= 4 && clear; ++dz)
                        for (int dy = -2; dy <= 2 && clear; ++dy)
                            if (w.blockAt(xx + dx, kRigY + dy, zz + dz) != BR::Air) clear = false;
                if (clear) { x0 = xx; z0 = zz; }
            }
        bool okA = false, okB = false, okC = false, okD = false;
        if (x0 < 0) {
            qInfo().noquote() << "  [t940 diag] no clear rig area found";
        } else {
            const float rideH = 0.45f;     // kCartRideH 镜像（轨上形态签名；groundH 0.3875 与之差 0.0625）
            const float groundH = 0.3875f; // kCartGroundH 镜像（自由体贴地落定中心偏移）
            // rig：z=z0 主线 EW 三格轨（x0..x0+2）+ 主线 / 西引道地板（x0-5..x0+2）；(a) 南腿地板
            //     (x0+1,z0+1)；z=z0+3 坡腿：坡格 (x0,Y) + 北上邻轨 (x0,Y+1,z0+2)（支撑 Stone @Y）+ 地板。
            for (int i = -5; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Stone, 0);
            w.setBlock(x0 + 1, kRigY - 1, z0 + 1, BR::Stone, 0);
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0 + 3, BR::Stone, 0);
            w.setBlock(x0, kRigY, z0 + 2, BR::Stone, 0); // 上邻轨支撑
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Rail, 0); // EW 主线
            w.setBlock(x0, kRigY + 1, z0 + 2, BR::Rail, 0); // 坡上邻（北 = -Z）
            w.setBlock(x0, kRigY, z0 + 3, BR::Rail, 0);     // 坡格（北邻 +1 → rise=1-fz）
            // ── (a) 横推上轨（用户场景）：南侧地面车北推 → 吸回轨。──
            {
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0 + 1, &w); // 非轨格 → 地面静止模式
                bool snapped = false;
                int snapTick = -1;
                for (int t = 0; t < 200 && !snapped; ++t) {
                    const QVector3D tp = carts.posAt(0);
                    carts.pushEmptyCart(&w, QVector3D(tp.x(), tp.y(), tp.z() + 0.4f), 0.0f, -1.0f); // 北推
                    carts.tickPushedCarts(0.016f, &w);
                    const QVector3D p = carts.posAt(0);
                    if (std::fabs(p.y() - (float(kRigY) + rideH)) < 0.02f
                        && std::fabs(p.z() - (float(z0) + 0.5f)) < 0.01f) { snapped = true; snapTick = t; }
                }
                // 吸回后推手继续贴推 30 tick：EW 轨上北向推 = 横推（t908 no-op）—— 不二次脱轨 / 不北漂。
                for (int t = 0; t < 30; ++t) {
                    const QVector3D tp = carts.posAt(0);
                    carts.pushEmptyCart(&w, QVector3D(tp.x(), tp.y(), tp.z() + 0.4f), 0.0f, -1.0f);
                    carts.tickPushedCarts(0.016f, &w);
                }
                const QVector3D fa = carts.posAt(0);
                okA = snapped && carts.aliveAt(0)
                    && std::fabs(fa.x() - (float(x0 + 1) + 0.5f)) < 0.02f // 沿轴坐标保留（spawn 位不动）
                    && std::fabs(fa.z() - (float(z0) + 0.5f)) < 0.01f     // 垂直轴钉轨心线（轨心对齐）
                    && std::fabs(fa.y() - (float(kRigY) + rideH)) < 0.02f // 轨上形态（rideH vs groundH）
                    && fa.z() > float(z0);                                // 停驻轨上（未被推穿北侧）
                if (!okA)
                    qInfo().noquote() << "  [t940 diag] a snapped" << snapped
                                      << "snapTick" << snapTick << "final" << fa;
                carts.clearAll();
            }
            // ── (b) 沿轨推上轨（西引道东推入轨端）：吸附保留沿轨速度 → 贴 rideH 沿轨滑行磨停。──
            {
                MinecartManager carts;
                carts.spawnCart(x0 - 1, kRigY, z0, &w); // 主线西侧一格地面车
                const QVector3D p0 = carts.posAt(0);
                carts.pushEmptyCart(&w, QVector3D(p0.x() - 0.4f, p0.y(), p0.z()), 1.0f, 0.0f); // 东推（一次性）
                bool snapped = false;
                float maxXRailed = -99.0f;
                for (int t = 0; t < 400; ++t) {
                    carts.tickPushedCarts(0.016f, &w);
                    const QVector3D p = carts.posAt(0);
                    if (p.x() > float(x0) && std::fabs(p.y() - (float(kRigY) + rideH)) < 0.02f) {
                        snapped = true;                                 // 轨上形态（吸附成立）
                        maxXRailed = std::max(maxXRailed, float(p.x()));
                    }
                }
                const QVector3D fb = carts.posAt(0);
                okB = snapped && carts.aliveAt(0)
                    && maxXRailed >= float(x0) + 1.2f                     // 沿轨速度保留：吸后沿轨前进 ≥1.2 格
                    && fb.x() > float(x0) && fb.x() < float(x0 + 3)       // 终位留在轨线（摩擦磨停，未飞出）
                    && std::fabs(fb.y() - (float(kRigY) + rideH)) < 0.02f // 终态轨上形态（旧代码 groundH 穿轨）
                    && std::fabs(fb.z() - (float(z0) + 0.5f)) < 0.02f;    // 轨心线（不侧漂）
                if (!okB)
                    qInfo().noquote() << "  [t940 diag] b snapped" << snapped
                                      << "maxXRailed" << maxXRailed << "final" << fb;
                carts.clearAll();
            }
            // ── (c) 阴性·t908/t863④ 推离不吸回：西死端车沿轴外推 → 出轨西滑落定轨外地面。──
            {
                MinecartManager carts;
                carts.spawnCart(x0, kRigY, z0, &w); // 西死端格轨上静止车（轨模式）
                for (int t = 0; t < 300; ++t) {
                    const QVector3D tp = carts.posAt(0);
                    if (tp.x() < float(x0) - 0.6f) break; // 已滑离轨格（免追推干扰）
                    carts.pushEmptyCart(&w, QVector3D(tp.x() + 0.4f, tp.y(), tp.z()), -1.0f, 0.0f);
                    carts.tickPushedCarts(0.016f, &w);
                }
                for (int t = 0; t < 200; ++t) carts.tickPushedCarts(0.016f, &w); // 滑行渐停
                const QVector3D fc0 = carts.posAt(0);
                for (int t = 0; t < 100; ++t) carts.tickPushedCarts(0.016f, &w); // 远离轨续跑
                const QVector3D fc = carts.posAt(0);
                okC = fc0.x() < float(x0) - 0.3f                             // 确已推离轨格（不被吸回）
                    && std::fabs(fc0.y() - (float(kRigY) + groundH)) < 0.02f // 落定轨外地面（自由体形态）
                    && carts.aliveAt(0)
                    && std::fabs(fc.x() - fc0.x()) < 0.01f                   // 续跑不动（无吸附 / 无漂移）
                    && std::fabs(fc.y() - fc0.y()) < 0.01f;
                if (!okC) qInfo().noquote() << "  [t940 diag] c settle" << fc0 << "after" << fc;
                carts.clearAll();
            }
            // ── (d) 坡轨吸附：东引道西推入 NS 坡格 → Y 钉坡面（rise 0.5）+ X 钉轨心线 + 45° 贴坡。──
            {
                MinecartManager carts;
                carts.spawnCart(x0 + 1, kRigY, z0 + 3, &w); // 坡格东侧一格地面车
                bool snapped = false;
                for (int t = 0; t < 200 && !snapped; ++t) {
                    const QVector3D tp = carts.posAt(0);
                    carts.pushEmptyCart(&w, QVector3D(tp.x() + 0.4f, tp.y(), tp.z()), -1.0f, 0.0f); // 西推
                    carts.tickPushedCarts(0.016f, &w);
                    const QVector3D p = carts.posAt(0);
                    if (std::fabs(p.x() - (float(x0) + 0.5f)) < 0.01f
                        && std::fabs(p.y() - (float(kRigY) + 0.95f)) < 0.02f) snapped = true;
                }
                const QVector3D fd = carts.posAt(0);
                okD = snapped && carts.aliveAt(0)
                    && std::fabs(fd.x() - (float(x0) + 0.5f)) < 0.01f     // X 钉轨心线（吸附「吸」位移）
                    && std::fabs(fd.z() - (float(z0 + 3) + 0.5f)) < 0.01f // 沿轴坐标保留（z 不动）
                    && std::fabs(fd.y() - (float(kRigY) + 0.95f)) < 0.02f // Y 钉坡面（rise 0.5；平地高 +0.45）
                    && std::fabs(carts.pitchAt(0) - 45.0f) < 1.0f;        // 贴坡俯仰（updateCartPitch 同函数）
                if (!okD)
                    qInfo().noquote() << "  [t940 diag] d snapped" << snapped
                                      << "final" << fd << "pitch" << carts.pitchAt(0);
                carts.clearAll();
            }
            // 清场。
            for (int i = -5; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0, BR::Air, 0);
            w.setBlock(x0 + 1, kRigY - 1, z0 + 1, BR::Air, 0);
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY - 1, z0 + 3, BR::Air, 0);
            w.setBlock(x0, kRigY, z0 + 2, BR::Air, 0);
            for (int i = 0; i <= 2; ++i) w.setBlock(x0 + i, kRigY, z0, BR::Air, 0);
            w.setBlock(x0, kRigY + 1, z0 + 2, BR::Air, 0);
            w.setBlock(x0, kRigY, z0 + 3, BR::Air, 0);
            tickN(w, 2);
        }
        // (e) 源码钉（t939 模式：字符串钉——snap 调用点 / helper 定义 / 头文件声明，任一消失即红）。
        const QString exeDir940 = QCoreApplication::applicationDirPath();
        const QString root940 = QDir(exeDir940 + QStringLiteral("/..")).absolutePath();
        auto readSrc940 = [&root940](const QString &rel) -> QString {
            QFile f(root940 + QStringLiteral("/") + rel);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        const QString mc940 = readSrc940(QStringLiteral("src/Entities/minecartmanager.cpp"));
        const QString mh940 = readSrc940(QStringLiteral("src/Entities/minecartmanager.h"));
        const bool okE1 = mc940.contains(QStringLiteral("    trySnapDerailedToRail(c, world);"));
        const bool okE2 = mc940.contains(
            QStringLiteral("bool MinecartManager::trySnapDerailedToRail(Cart &c, World *world)"));
        const bool okE3 = mh940.contains(QStringLiteral("bool trySnapDerailedToRail(Cart &c, World *world);"));
        const bool okT940 = okA && okB && okC && okD && okE1 && okE2 && okE3;
        if (!okT940) ++totalFail;
        if (!okT940)
            qInfo().noquote() << "  [t940 diag] a" << okA << "b" << okB << "c" << okC << "d" << okD
                              << "| e" << okE1 << okE2 << okE3
                              << "| srcLen cpp" << mc940.size() << "h" << mh940.size();
        qInfo().noquote() << (okT940 ? "PASS" : "FAIL")
                          << "| t940 body-pushed derailed cart snaps back onto a nearby rail: a cart shoved"
                             " sideways or along the ground into a rail cell was caught positionally but kept"
                             " free-body physics (gliding at ground height through the rail boards, pushes"
                             " routed through the derailed branch - never regaining rail movement form). Fix"
                             " adds an end-of-free-physics near-rail snap (trySnapDerailedToRail) with a"
                             " same-layer gate (rail layer must equal the cart-center cell, strict column"
                             " scan so solid floors block), a dead-end-outward exemption (pickTrackStep with"
                             " the current velocity - the only arm anti-parallel to an exiting push filters"
                             " out, preserving t863(4)/t908 push-off semantics; orphan 0-connection rails"
                             " never snap), and reuses the placement/riding pinning set (perpendicular axis"
                             " onto the rail center line, Y=pinCartY on the same rise surface - slope rails"
                             " pin to the sloped face not flat height, pitch via updateCartPitch, head along"
                             " the selected arm) with velocity projected onto the rail axis (lateral"
                             " component dropped per the t908 decomposition). Probe legs: (a) lateral"
                             " ground shove onto an EW line snaps within ticks - center-line aligned, riding"
                             " height, parked on the rail, continued lateral pushing stays a no-op; (b)"
                             " along-rail entry preserves along-rail speed and glides >=1.2 cells glued to"
                             " the surface; (c) dead-end along-axis push-off still exits west and settles on"
                             " the ground off-track (not re-snapped) and stays put; (d) slope-cell snap pins"
                             " Y to the slope face (+0.95 = rise 0.5 + rideH, not flat +0.45) with a 45deg"
                             " pitch; (e) source pins for the call site, helper definition and declaration"
                          ;
    }
}
