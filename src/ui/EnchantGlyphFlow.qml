import QtQuick
import QtQuick3D
// t765/t797 书架→附魔台「文字」粒子流（呈现层；PLAN §2 分层 —— 只读 World 书架位，不反向写栅格）。
//
// 机制等价 MC 1.0 附魔台 glyph 粒子流的**常驻版**（t797 用户定稿）：只要游玩中，附魔台旁的有效书架就
//   持续向台漂出**白色小字形**（透明底字形图集 × 纯白染色）：缓慢漂移（一程 ~2-4s）+ 途中线性渐隐 +
//   到达书心即透明回收（到达即删）—— **不依赖附魔台 UI 开关**（旧 t765 仅开 UI 才播且面片过大「像
//   爆炸」，两项均按用户报告重做）。附魔台 UI 打开时**所开台**的书架发射率加密（×uiBoost，交互反馈）。
//
// 与既有「符文」视觉的区别（防后人误删/误并）：
// - t649 EnchantRunes.qml：常驻彩色**小立方**氛围漂流（t697 起常驻）—— 纯色立方、无字形贴图、只跟
//   「最近一次打开的台」（tableX 绑 window.enchantX 遗留值）。本组件是**全图所有台**的白色**文字字形**
//   流（glyphs.png 字形图集），按台×书架对发射 —— 两套并存各司其职（立方氛围 vs 文字流向）。
// - t732 撤下的 GlyphLines（Renderer/glyphlines.cpp）：书页上的静态符文字迹叠层，与动态粒子流两回事。
//
// 实现（EnchantRunes t649 同族：Model 池 + Timer 弹道，不依赖 Particles3D —— t385/t390 已证该模块运行期
//   可能降级；经 Main.qml glyphFlowLoader 隔离加载 + 领养进 particlesHost，失败仅 warn，§2-E）：
// - 粒子 = "#Rectangle" 内建面片 + glyphs.png 4×4 字形图集（tools/build_glyph_sprites.py 程序原创字形，
//   零 MC 资产）按格采样（Texture.scaleU/V=0.25 + positionU/V 选格，t489 flipbook 同 API）；每池元素独立
//   材质/贴图 → 每颗随机字形；t797 起染色板改纯白系（用户「白色的文字就行」，透明底走 Blend）。
// - 弹道 = 参数化飞行（start→书心 lerp + sinπt 轻弧 + 末端收敛横摆）：漂速 ~0.9 格/s → 一程 2-4s
//   「缓慢漂向」；alpha = 前 12% 淡入 × (1-k) 线性渐隐 → t=1 恰落书心且已透明（到达即删，不与书页
//   z-fight）；tick 内每颗面向相机 billboard（正对可读的文字面）。
// - 台×书架对枚举：tableModel（Main.qml 注入 enchantTablePositions 全图附魔台表 —— 事件驱动 + 读档
//   重建 + 孤儿清理三重维护）逐台套 World::countBookshelvesAround 同规则（水平切比雪夫 ==2 环带 ×
//   y/y+1 两层 + 半步格 Air；blockAt 只读，不加 World API —— t649 先例）。editRev / 台表 count /
//   active 变化重扫（放书架 ≤0.5s 内起流，拆书架/台即停）。
//
// 性能红线（t724 粒子风暴前例）：① 无对（pairs 空）→ spawnTimer 停；在飞粒子由 tickTimer 推进至寿终
//   （running 绑 active || liveCount>0 → 清空即全停，零常驻开销）；② 全局发射率上限 maxPerTick + 池硬
//   上限 poolSize（满则静默丢，同 BlockParticles 模式）；③ 发射距离门 camEmitRangeSq：书架离相机
//   >16 格的对不发射（远处看不见纯浪费）；④ 每书架低频 ~0.22/s（= 3-6s 一粒的随机常驻密度）。
//
// 坐标空间：经 Main.qml glyphFlowLoader.onLoaded 领养进 particlesHost 锚点（t16：否则 Loader 加载的
//   3D Node parent=null → 孤儿不渲染）。粒子坐标即世界坐标（书架格 / 台格中心）。
Node {
    id: root

    // 宿主注入（Main.qml glyphFlowLoader.onLoaded）：World（blockAt 只读查书架）+ 相机（billboard 朝向 +
    //   发射距离门）+ tableModel（enchantTablePositions 全图附魔台表）+ active（playing 常驻，**不依赖
    //   附魔台 UI 开**，t797）+ uiOpen/openTable*（所开台 —— 发射率加密对象）+ editRev（放/破方块版本号）。
    property var world: null
    property var camNode: null
    property var tableModel: null
    // 绑 tableModel.count —— 增/删台（含读档重建的 clear+append、onWorldChanged 孤儿清理）触发重扫。
    property int tableCount: 0
    property bool active: false
    // review26 #11（t889 补漏）：硬暂停总闸——宿主经 glyphFlowLoader.onLoaded 注入 window.worldRunning
    //   （Qt.binding；同 active/world/camNode 注入先例，组件不直引跨上下文 id）。ESC 硬档（世界全停）时
    //   spawn 停 + 在飞字形冻结（恢复时自然续飞，机制等价 MC Java 单机暂停粒子冻结）；软档 GUI 开
    //   worldRunning 仍真 → 照常发射。旧版只绑 active（appState=="playing"）→ ESC 时白字持续飞（finding）。
    property bool worldRunning: false
    property bool uiOpen: false
    property int openTableX: 0
    property int openTableY: 0
    property int openTableZ: 0
    property int editRev: 0

    // ---- 可调常量（性能红线：发射率/池上限/距离门防粒子风暴；集中在此便于调参） ----
    readonly property int poolSize: 36          // 池硬上限：全局 ≤8/s × 最长寿命 4s ≈ 32 稳态 + 余量
    readonly property real ratePerShelf: 0.22   // 每书架每秒字数（用户「3-6s 一粒」→ 低频常驻密度）
    readonly property real uiBoost: 4.0         // 附魔台 UI 开时所开台书架的发射率倍率（加密反馈）
    readonly property int maxPerTick: 4         // 单轮（500ms）发射上限 → 全局 ≤8/s 封顶
    readonly property real camEmitRangeSq: 256  // 发射距离门 16²（格²）：书架离相机超此距不发射
    readonly property real driftSpeed: 0.9      // 漂移速度（格/s）：2-3 格书架 → 一程 ~2-4s 缓慢漂向
    readonly property real flightLifeMin: 1.8   // 寿命钳制（近书架防闪瞬、远书架防拖尾过久）
    readonly property real flightLifeMax: 4.0
    readonly property real arcHeight: 0.20      // 弧线峰值（慢漂下的轻拱，不夺目）
    // 字形面片边长（格）：t873 标定。#Rectangle 内建面片基尺寸是 **100×100 单位**（实测 scale 0.07 →
    //   7.005 格宽 = 0.07×100，非 1×1）—— 下方池模板 scale 已 ÷100，本组数值即真实「格」数。历史：
    //   t765 旧值 0.10-0.16 直乘 100 基 = 10-16 格宽「像爆炸」；t797 缩到 0.055-0.085 仍直乘 = 5.5-8.5
    //   格白幕（两轮都治不好的真因）。÷100 后 0.055-0.085 实测在 5-8 格视距下笔画 ~0.5px **亚像素不
    //   可见**（探针像素级实证），故按可读性重标定为 0.18-0.28（18-28px @ 7 格视距、笔画 1.5-2px，
    //   机制对标 MC 字形粒子 ~1/4 格的白字）。
    readonly property real glyphScaleMin: 0.18
    readonly property real glyphScaleMax: 0.28

    // 字形染色板：t797 用户定稿「白色的文字」—— 纯白为主 + 极轻冷调抖动（近白字形相乘仍读作白）。
    readonly property var tintColors: ["#ffffff", "#f4f6ff", "#e9eeff"]

    property var pool: []
    property int liveCount: 0   // 在飞数（active 翻假后 tickTimer 据它判「清空即全停」）

    // 台×书架对缓存（[{x,y,z, tx,ty,tz},...]：书架格 + 所属台格）。editRev / 台表 count / active 变化时
    //   重扫（显式触碰是唯一刷新源，同 EnchantRunes 模式 —— 放/破书架或台即刻重算，t549 先例）。
    property var pairs: []

    // t873 自检计数器：累计发射颗数（诊断「发射器在跑但肉眼看不见」vs「根本没在跑」——前者查渲染侧，
    //   后者查数据链；配合下方各 [t873] 日志一次运行即可读出链断在哪一跳）。
    property int emittedTotal: 0
    property int tickSample: 0   // 发射轮采样计数（每 ~8s 落一行日志）

    Component.onCompleted: {
        root.pool = []
        for (let i = 0; i < root.poolSize; i++) {
            const m = glyphComponent.createObject(root)
            m.visible = false
            // p.t ∈[0,1) 参数化飞行进度；start/end/scale 在 spawn 时定，tick 只推 t。
            root.pool.push({ obj: m, t: 0.0, life: 1.0, active: false,
                             sx: 0, sy: 0, sz: 0, ex: 0, ey: 0, ez: 0,
                             swayPhase: 0.0, arc: 0.0 })
        }
        console.info("[t797] EnchantGlyphFlow ready; pool=" + root.poolSize
                     + " (ambient white glyph flow: all tables x shelves, cam-range gated)")
    }

    // review26 #23：t873 自检日志降为调试开关门（默认关）——rescanPairs 由 onEditRevChanged 驱动，旧版
    //   每次挖/放方块必刷一行 console.info（生产路径噪声）。开关 = 启动参数 --verbose-glyphs（排障时
    //   临时打开恢复 t873 全量自检：数据侧 0 台/0 对 vs 渲染侧不可见的折损点定位语义不变）。
    readonly property bool debugSelfCheck: Qt.application.arguments.indexOf("--verbose-glyphs") >= 0

    // 重扫台×书架对：tableModel 逐台套 World::countBookshelvesAround 同规则（水平切比雪夫 ==2 环带 ×
    //   y/y+1 两层 + 半步格 Air）。规则单一权威在 World::countBookshelvesAround 的注释契约里，QML 呈现层
    //   内联同规则（EnchantRunes 同款复制 —— 改规则须多处同步，此处显式注记）。
    function rescanPairs() {
        root.pairs = []
        if (!root.world || !root.active || !root.tableModel) {
            // t873 自检：前置门未齐（读档早期 / 菜单态）—— 折损点直接落日志，不再静默早退（review26 #23
            //   起同受 --verbose-glyphs 门：常态零日志）。
            if (root.debugSelfCheck)
                console.info("[t873] rescan aborted: world=" + (root.world !== null)
                             + " active=" + root.active
                             + " tableModel=" + (root.tableModel !== null))
            return
        }
        const n = root.tableModel.count
        for (let i = 0; i < n; ++i) {
            const e = root.tableModel.get(i)
            for (let dy = 0; dy <= 1; ++dy) {
                const yy = e.y + dy
                for (let dx = -2; dx <= 2; ++dx) {
                    for (let dz = -2; dz <= 2; ++dz) {
                        if (Math.max(Math.abs(dx), Math.abs(dz)) !== 2) continue
                        if (root.world.blockAt(e.x + dx, yy, e.z + dz) !== 95 /* Bookshelf */) continue
                        if (root.world.blockAt(e.x + Math.trunc(dx / 2), yy, e.z + Math.trunc(dz / 2)) !== 0) continue
                        root.pairs.push({ x: e.x + dx, y: yy, z: e.z + dz, tx: e.x, ty: e.y, tz: e.z })
                    }
                }
            }
        }
        // t873 自检（review26 #23 起 --verbose-glyphs 门控）：每次重扫落一行「台数 × 对数」——0 台 / 0 对
        //   即链断在数据侧（表空或搭法不满足环带规则），非 0 却看不见则链断在渲染/视觉侧（发射、材质、尺寸）。
        if (root.debugSelfCheck)
            console.info("[t873] rescanPairs: tables=" + n + " pairs=" + root.pairs.length)
    }

    onActiveChanged:     rescanPairs()
    onEditRevChanged:    rescanPairs()
    onTableCountChanged: rescanPairs()

    // 发射轮（500ms）：① 距离门 —— 书架离相机 >16 格的对剔除（远处不发射）；② 期望值法 —— 每近处对
    //   ratePerShelf × 0.5s（所开台 ×uiBoost）累加出期望发射数，整数部分 + 按小数部分概率补 1（3-6s
    //   一粒的**随机**低频，非整齐节拍）；③ 全局 maxPerTick 封顶。无对 / 硬暂停 → running=false 零开销
    //   （review26 #11：worldRunning 并入——ESC 全停时不新增发射）。
    Timer {
        id: spawnTimer
        interval: 500
        repeat: true
        running: root.active && root.pairs.length > 0 && root.worldRunning
        onTriggered: {
            const camPos = root.camNode ? root.camNode.position : null   // JS 读 = 快照（不建绑定依赖）
            const near = []
            for (let i = 0; i < root.pairs.length; i++) {
                const p = root.pairs[i]
                if (camPos) {
                    const dx = p.x + 0.5 - camPos.x, dz = p.z + 0.5 - camPos.z
                    if (dx * dx + dz * dz > root.camEmitRangeSq) continue
                }
                near.push(p)
            }
            if (near.length === 0) return
            let want = 0.0
            for (let i = 0; i < near.length; i++) {
                const p = near[i]
                let r = root.ratePerShelf
                if (root.uiOpen && p.tx === root.openTableX && p.ty === root.openTableY
                        && p.tz === root.openTableZ)
                    r *= root.uiBoost
                want += r * 0.5
            }
            let n = Math.floor(want) + (Math.random() < (want % 1) ? 1 : 0)
            n = Math.min(n, root.maxPerTick)
            for (let i = 0; i < n; i++) root.spawnGlyph(near)
            // t873 自检：发射节拍采样（每 ~8s 一行，不逐轮刷屏）：近处对数 / 期望值 / 实发 / 累计 ——
            //   want>0 而 spawn=0 = 池满；want=0 = 距离门全剔或速率归零。
            root.tickSample++
            if (root.tickSample % 16 === 1)
                console.info("[t873] spawn tick: nearPairs=" + near.length + " want=" + want.toFixed(2)
                             + " spawned=" + n + " totalEmitted=" + root.emittedTotal
                             + " poolFree=" + (root.poolSize - liveCount))
        }
    }

    // 从随机近处对 spawn 一颗白色小字形：起点 = 书架格中心朝台侧偏移（从书架「怀里」冒出），终点 =
    //   所属台上悬浮书心（t796 ① 书心 0.82→0.95 抬升同步：台格中心 +0.95，对齐 bookDelegate 书心
    //   y+0.95±bob0.035）；寿命 = 距离/漂速钳制 → 一程 ~2-4s，t=1 恰落书心且 alpha 已线性归零。
    function spawnGlyph(list) {
        if (list.length === 0) return
        const c = list[Math.floor(Math.random() * list.length)]
        const tx = c.tx + 0.5, tyv = c.ty + 0.95, tz = c.tz + 0.5
        // 起点在书架内侧（朝台方向半格出、书架上半身高度）：读作「从书架里缓慢漂出」而非凭空出现。
        let dx = tx - (c.x + 0.5), dz = tz - (c.z + 0.5)
        const hd = Math.max(0.001, Math.sqrt(dx * dx + dz * dz))
        const sx = c.x + 0.5 + dx / hd * 0.45 + (Math.random() - 0.5) * 0.2
        const sy = c.y + 0.55 + Math.random() * 0.3
        const sz = c.z + 0.5 + dz / hd * 0.45 + (Math.random() - 0.5) * 0.2
        // 终点微抖（±0.05）：多颗同落点不叠成一串「珠链」，散进书缝里。
        const ex = tx + (Math.random() - 0.5) * 0.10
        const ey = tyv + (Math.random() - 0.5) * 0.06
        const ez = tz + (Math.random() - 0.5) * 0.10
        const dist = Math.sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy) + (ez - sz) * (ez - sz))
        const life = Math.max(root.flightLifeMin, Math.min(root.flightLifeMax, dist / root.driftSpeed))
        const arr = root.pool
        for (let i = 0; i < arr.length; i++) {
            const p = arr[i]
            if (p.active) continue
            p.active = true
            p.t = 0.0
            p.life = life
            p.sx = sx; p.sy = sy; p.sz = sz
            p.ex = ex; p.ey = ey; p.ez = ez
            p.swayPhase = Math.random() * Math.PI * 2
            p.arc = root.arcHeight * (0.6 + Math.random() * 0.8)   // 弧高抖动：非整齐同拱
            const m = p.obj
            m.px = sx; m.py = sy; m.pz = sz
            m.tint = root.tintColors[Math.floor(Math.random() * root.tintColors.length)]
            m.glyphOpacity = 0.0
            m.scl = root.glyphScaleMin + Math.random() * (root.glyphScaleMax - root.glyphScaleMin)
            // 字形图集 4×4 选格：列 col（U 左→右 = 图像 x 正向）；行 row 从图像顶数，而 V 约定
            //   「图像顶 ↔ v=1」（t489 实测）→ positionV = (GRID-1-row)/4。
            const col = Math.floor(Math.random() * 4), row = Math.floor(Math.random() * 4)
            m.texU = col * 0.25
            m.texV = (3 - row) * 0.25
            m.visible = true
            root.liveCount++
            root.emittedTotal++
            return
        }
        // 池满：静默丢（同 BlockParticles / EnchantRunes 模式，不 new 不阻塞）。
    }

    // 弹道推进 Timer（~50fps）：参数化飞行 + 正弦轻弧 + billboard 朝相机 + 前 12% 淡入 × (1-k) 全程
    //   线性渐隐（t797「途中缓慢变透明」）；t≥1 落书心已全透明即回收（到达即删）。running 绑
    //   (active || liveCount>0) && worldRunning —— active 翻假后在飞颗粒放完即全停（零常驻）；
    //   review26 #11：硬暂停亦冻结（在飞字形停在中途，恢复时自然续飞——比「暂停期继续飞完」更贴
    //   MC Java 单机 ESC 粒子冻结；暂停叠层遮住世界，冻结不可见无观感代价）。
    Timer {
        id: tickTimer
        interval: 20
        repeat: true
        running: (root.active || root.liveCount > 0) && root.worldRunning
        onTriggered: {
            const dt = 0.020
            const camPos = root.camNode ? root.camNode.position : null   // JS 读 = 快照（不建绑定依赖）
            const arr = root.pool
            for (let i = 0; i < arr.length; i++) {
                const p = arr[i]
                if (!p.active) continue
                p.t += dt / p.life
                const m = p.obj
                if (p.t >= 1.0) {
                    p.active = false
                    m.visible = false
                    root.liveCount--
                    continue
                }
                // 参数化弹道：直线 lerp + sinπt 轻弧（书架→书心缓拱）+ 末端收敛的横向轻摆。
                const k = p.t
                const swayA = Math.sin(p.swayPhase + k * 6.0) * 0.04 * (1.0 - k)
                m.px = p.sx + (p.ex - p.sx) * k + swayA
                m.py = p.sy + (p.ey - p.sy) * k + Math.sin(Math.PI * k) * p.arc
                m.pz = p.sz + (p.ez - p.sz) * k + Math.cos(p.swayPhase + k * 6.0) * 0.04 * (1.0 - k)
                // t797 渐隐律：前 12% 淡入 × (1-k) 线性渐隐 —— 全程缓慢变透明，t=1 在书心处恰归零
                //   （到达即删，不与书页面 z-fight）。
                m.glyphOpacity = Math.min(1.0, k / 0.12) * (1.0 - k)
                // billboard：面片 +Z 朝相机（yaw=atan2(dx,dz)；pitch=-atan2(dy,水平距)，+Z 上仰为负角）。
                //   camNode 未注入时保持 spawn 位姿（兜底：朝台飞行方向附近仍大致可读）。
                if (camPos) {
                    const cdx = camPos.x - m.px, cdy = camPos.y - m.py, cdz = camPos.z - m.pz
                    m.yawDeg = Math.atan2(cdx, cdz) * 180 / Math.PI
                    m.pitchDeg = -Math.atan2(cdy, Math.sqrt(cdx * cdx + cdz * cdz)) * 180 / Math.PI
                }
            }
        }
    }

    // 池元素模板：#Rectangle 内建面片（自带 UV，t489 验证；勿用未注册的 PlaneGeometry）+ 字形图集
    //   子区采样 + Blend 渐隐（透明底图集 → 透明背景）。NoLighting（红线：可见 Model 必须 NoLighting）。
    //   NoCulling（Material.NoCulling，Main.qml 手持图标先例）：billboard 瞬时翻转（相机掠过正上/
    //   正后方）时不出「消失半帧」。
    Component {
        id: glyphComponent
        Model {
            id: glyph
            source: "#Rectangle"
            visible: false
            property real px: 0.0
            property real py: 0.0
            property real pz: 0.0
            property real scl: 0.07
            property real yawDeg: 0.0
            property real pitchDeg: 0.0
            property real glyphOpacity: 0.0
            property color tint: "#ffffff"
            // 字形图集选格（alias 到本实例贴图的 UV 偏移；每池元素独立 Texture → 每颗粒子独立字形）。
            property alias texU: glyphTex.positionU
            property alias texV: glyphTex.positionV
            position: Qt.vector3d(glyph.px, glyph.py, glyph.pz)
            eulerRotation: Qt.vector3d(glyph.pitchDeg, glyph.yawDeg, 0)
            // t873 根因修正：#Rectangle 基尺寸 100×100 单位（实测 scale 0.07 → 7.005 格宽，非 1×1）——
            //   ÷100 恢复 scl「字形面片边长（格）」语义。旧直乘 = 5.5-8.5 格宽巨型半透明白幕，t765
            //   「像爆炸」→t797 数值缩小两轮均无效的真因（基尺寸假设错 100×，从未像素级验证）。
            scale: Qt.vector3d(glyph.scl / 100.0, glyph.scl / 100.0, 1)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                cullMode: Material.NoCulling   // 双面（Main.qml 手持 billboard 图标同先例）：billboard 瞬时翻转不出消失半帧
                baseColor: glyph.tint          // 近白字形 × 白系染色 = 白色小字（透明底）
                opacity: glyph.glyphOpacity
                alphaMode: PrincipledMaterial.Blend   // 连续渐隐走 Blend（透明底图集）
                baseColorMap: Texture {
                    id: glyphTex
                    source: "qrc:/textures/glyphs.png"   // tools/build_glyph_sprites.py 程序原创字形
                    scaleU: 0.25                          // 4×4 图集取 1 格
                    scaleV: 0.25
                    // 注：不设放大过滤属性 —— 本 Qt 6.11 Texture 无 magnificationFilter（运行期
                    //   「不存在的属性」致组件加载失败，log 实证），默认 Linear 对 ~1:1 像素比字形够用。
                }
            }
        }
    }
}
