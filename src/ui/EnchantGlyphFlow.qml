import QtQuick
import QtQuick3D
// t765 书架→附魔台「文字/符文」粒子流（呈现层；PLAN §2 分层 —— 只读 World 书架位，不反向写栅格）。
//
// 机制等价 MC 1.0 附魔台 glyph 粒子流：玩家打开附魔台 UI 期间，周围**有效书架位**向附魔台悬浮书
//   持续喷小字符颗粒（贴图字形 + 染色 + 沿弧线飞向书心 + 淡出）；UI 关闭即停。
//
// 与既有两件「符文」视觉的区别（防后人误删/误并）：
// - t649 EnchantRunes.qml：**常驻**的彩色立方符文漂流（t697 改 playing 常驻，用户明确要「非仅
//   开 UI 时」）—— 那是氛围层，粒子是纯色小立方、无字形贴图。本组件是 UI 打开期间的**文字字形**
//   密集流（glyph sprite），两套并存各司其职（氛围 vs 交互反馈），非重复实现。
// - t732 撤下的 GlyphLines（Renderer/glyphlines.cpp）：书页上的**静态**符文字迹叠层，因贴图自带
//   字迹会重影而撤。本组件是**动态粒子流**，与它两回事（dev-plan t765 行明示）。
//
// 实现（EnchantRunes t649 同族：Model 池 + Timer 弹道，不依赖 Particles3D —— t385/t390 已证该模块
//   运行期可能降级；经 Main.qml glyphFlowLoader 隔离加载 + 领养进 particlesHost，失败仅 warn，§2-E）：
// - 粒子 = "#Rectangle" 内建面片 + glyphs.png 4×4 字形图集（tools/build_glyph_sprites.py 程序原创
//   字形，零 MC 资产）按格采样（Texture.scaleU/V=0.25 + positionU/V 选格，t489 flipbook 同 API）；
//   每池元素独立材质/贴图 → 每颗粒子随机字形 + 随机染色（紫/青/白系，同 EnchantRunes 色板）。
// - 弹道 = 参数化飞行（start→书心 lerp + sinπt 弧线 + 收尾淡出）：t→1 恰落在书心（「涌入」感，
//   不会飞过头），寿命 = 距离/速度；tick 内每颗面向相机转 billboard（正对可读的文字面）。
// - 有效书架位枚举：QML 侧扫 World::countBookshelvesAround 同规则（水平切比雪夫 ==2 环带 × y/y+1
//   两层 + 半步格 Air；blockAt 只读）。不加 World 新 API —— EnchantRunes t649 同先例，分层最干净
//   （World 只读 invokable 已够用，逐位枚举纯属呈现层派生）。
// - 驱动：active = 宿主绑「playing && 附魔台 UI 开」。MC 语义 = 只有所开台的书被喂符文（远处台
//   喷了玩家也看不见）→ 台坐标绑 window.enchantX/Y/Z（当前所开台），不遍历 enchantTablePositions。
//
// 性能红线（t724 粒子风暴前例）：① UI 关闭 → spawnTimer 停（running 绑 shelfCells 非空 + active），
//   在飞粒子由 tickTimer 推进至寿终（running 绑 active || liveCount>0 → 清空即全停，零常驻开销）；
//   ② 全局发射率上限 maxPerTick（200ms 轮 ≤4 颗 = ≤20/s）+ 池硬上限 poolSize（满则静默丢，同
//   BlockParticles 模式）—— 书架再多也封顶，常量均可调。
//
// 坐标空间：经 Main.qml glyphFlowLoader.onLoaded 领养进 particlesHost 锚点（t16：否则 Loader 加载的
//   3D Node parent=null → 孤儿不渲染）。粒子坐标即世界坐标（书架格 / 台格中心）。
Node {
    id: root

    // 宿主注入（Main.qml glyphFlowLoader.onLoaded）：World（blockAt 只读查书架）+ 相机（billboard
    //   朝向）+ 所开附魔台方块坐标 + active（附魔台 UI 开）+ editRev（放/破方块版本号 —— 书架位重扫）。
    property var world: null
    property var camNode: null
    property int tableX: 0
    property int tableY: 0
    property int tableZ: 0
    property bool active: false
    property int editRev: 0

    // ---- 可调常量（性能红线：发射率上限 + 池上限防粒子风暴；集中在此便于调参） ----
    readonly property int poolSize: 36        // 池硬上限：全局 ≤20/s × 最长寿命 1.3s ≈ 26 稳态 + 余量
    readonly property real ratePerShelf: 2.4  // 每有效书架每秒符文数（低频，机制等价 MC glyph 流密度）
    readonly property int maxPerTick: 4       // 单轮（200ms）发射上限 → 全局 ≤20/s 封顶
    readonly property real flightSpeed: 2.6   // 飞行速度（格/s）：2-3 格书架 → ~1s 飞抵书心
    readonly property real flightLifeMin: 0.5 // 寿命钳制（近书架防闪瞬、远书架防拖尾过久）
    readonly property real flightLifeMax: 1.3
    readonly property real arcHeight: 0.30    // 弧线峰值（书架顶→书心的抛物拱，涌入感）
    readonly property real glyphScaleMin: 0.10 // 字形面片边长（格）：~1.5-2.5 纹素级小字符
    readonly property real glyphScaleMax: 0.16

    // 符文染色板（与 t649 EnchantRunes 同板：神秘紫系为主 + 冷青/亮白点缀；近白字形相乘染色）。
    readonly property var tintColors: ["#b06ae8", "#8a4ad8", "#6ab8e8", "#e8e8f8", "#c88ae8"]

    property var pool: []
    property int liveCount: 0   // 在飞数（UI 关后 tickTimer 据它判「清空即全停」）

    // 参与书架位缓存（[{x,y,z},...]）。editRev / active / 台坐标变化时重扫（显式触碰是唯一刷新源，
    //   同 EnchantRunes 模式 —— UI 开着放/破书架也能重算，t549 先例）。
    property var shelfCells: []

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
        console.info("[t765] EnchantGlyphFlow ready; pool=" + root.poolSize
                     + " (glyph-sprite Model+Timer pool; UI-open driven)")
    }

    // 重扫参与书架位（World::countBookshelvesAround 同规则：切比雪夫 ==2 环带 × y/y+1 两层 + 半步格
    //   Air）。与 EnchantRunes.rescanShelves 同款复制（规则单一权威在 World::countBookshelvesAround
    //   的注释契约里，两处 QML 呈现层各自内联同规则 —— 改规则须三处同步，此处显式注记）。
    function rescanShelves() {
        root.shelfCells = []
        if (!root.world || !root.active) return
        const tx = root.tableX, ty = root.tableY, tz = root.tableZ
        for (let dy = 0; dy <= 1; ++dy) {
            const yy = ty + dy
            for (let dx = -2; dx <= 2; ++dx) {
                for (let dz = -2; dz <= 2; ++dz) {
                    if (Math.max(Math.abs(dx), Math.abs(dz)) !== 2) continue
                    if (root.world.blockAt(tx + dx, yy, tz + dz) !== 95 /* Bookshelf */) continue
                    if (root.world.blockAt(tx + Math.trunc(dx / 2), yy, tz + Math.trunc(dz / 2)) !== 0) continue
                    root.shelfCells.push({ x: tx + dx, y: yy, z: tz + dz })
                }
            }
        }
    }

    onActiveChanged:   rescanShelves()
    onEditRevChanged:  rescanShelves()
    onTableXChanged:   rescanShelves()
    onTableYChanged:   rescanShelves()
    onTableZChanged:   rescanShelves()

    // 发射轮（200ms）：每轮 n = 钳制(书架数 × ratePerShelf × 0.2, 1, maxPerTick) —— 每书架低频
    //   ~2.4/s、全局 ≤20/s 封顶（性能红线②）。UI 关 / 无书架 → running=false 零开销。
    Timer {
        id: spawnTimer
        interval: 200
        repeat: true
        running: root.active && root.shelfCells.length > 0
        onTriggered: {
            const want = Math.round(root.shelfCells.length * root.ratePerShelf * 0.2)
            const n = Math.max(1, Math.min(root.maxPerTick, want))
            for (let i = 0; i < n; i++) root.spawnGlyph()
        }
    }

    // 从随机有效书架位 spawn 一颗字形粒子：起点 = 书架格中心朝台侧偏移（从书架「怀里」冒出），
    //   终点 = 台上悬浮书心（台格中心 +0.85，对齐 bookDelegate 书心 y+0.82~0.85）；寿命 = 距离/速度
    //   （钳制），弹道参数化 → t=1 恰落书心后淡尽（「涌入」不飞过头）。
    function spawnGlyph() {
        const cells = root.shelfCells
        if (cells.length === 0) return
        const c = cells[Math.floor(Math.random() * cells.length)]
        const tx = root.tableX + 0.5, tyv = root.tableY + 0.85, tz = root.tableZ + 0.5
        // 起点在书架内侧（朝台方向半格出、书架上半身高度）：读作「从书架里涌出」而非凭空出现。
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
        const life = Math.max(root.flightLifeMin, Math.min(root.flightLifeMax, dist / root.flightSpeed))
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
            return
        }
        // 池满：静默丢（同 BlockParticles / EnchantRunes 模式，不 new 不阻塞）。
    }

    // 弹道推进 Timer（~50fps）：参数化飞行 + 正弦弧 + billboard 朝相机 + 前 15% 淡入/末 25% 淡出；
    //   t≥1 落书心即回收。running 绑 active || liveCount>0 —— UI 关后在飞颗粒放完即全停（零常驻）。
    Timer {
        id: tickTimer
        interval: 20
        repeat: true
        running: root.active || root.liveCount > 0
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
                // 参数化弹道：直线 lerp + sinπt 竖向弧（书架顶→书心抛物拱）+ 末端收敛的横向轻摆。
                const k = p.t
                const swayA = Math.sin(p.swayPhase + k * 7.0) * 0.05 * (1.0 - k)
                m.px = p.sx + (p.ex - p.sx) * k + swayA
                m.py = p.sy + (p.ey - p.sy) * k + Math.sin(Math.PI * k) * p.arc
                m.pz = p.sz + (p.ez - p.sz) * k + Math.cos(p.swayPhase + k * 7.0) * 0.05 * (1.0 - k)
                // 淡入前 15% / 淡出末 25%（t→1 在书心处透明消隐，不与书页面 z-fight）。
                m.glyphOpacity = Math.min(1.0, k / 0.15, (1.0 - k) / 0.25)
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
    //   子区采样 + Blend 渐隐（透明底图集）。NoLighting（红线：可见 Model 必须 NoLighting）。
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
            property real scl: 0.12
            property real yawDeg: 0.0
            property real pitchDeg: 0.0
            property real glyphOpacity: 0.0
            property color tint: "#b06ae8"
            // 字形图集选格（alias 到本实例贴图的 UV 偏移；每池元素独立 Texture → 每颗粒子独立字形）。
            property alias texU: glyphTex.positionU
            property alias texV: glyphTex.positionV
            position: Qt.vector3d(glyph.px, glyph.py, glyph.pz)
            eulerRotation: Qt.vector3d(glyph.pitchDeg, glyph.yawDeg, 0)
            scale: Qt.vector3d(glyph.scl, glyph.scl, 1)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                cullMode: Material.NoCulling   // 双面（Main.qml 手持 billboard 图标同先例）：billboard 瞬时翻转不出消失半帧
                baseColor: glyph.tint          // 近白字形 × 染色 = 各色符文
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
