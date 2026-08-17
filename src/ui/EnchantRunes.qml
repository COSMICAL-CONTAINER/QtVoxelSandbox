import QtQuick
import QtQuick3D
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `geometry: UnitCube {}` 等 C++ 类型
//   （同 BlockParticles.qml 头部模式）。
import VoxelSandbox
// t649 附魔台符文粒子（呈现层；PLAN §2 分层 —— 只读 World 书架位，不反向写栅格）。
//
// 机制等价 MC 1.0 附魔台符文粒子：附魔台界面打开时，参与加成的书架向附魔台方向漂浮小字符颗粒
//   （小紫/青/白立方，慢速飘行 + 轻微横向摆动 + 渐隐）；附魔成功时迸发一小簇。
//
// 实现（BlockParticles.qml t465 同模式）：**不依赖 Particles3D**（t385/t390 已证运行期降级 Loader），
//   预分配 Model 池（UnitCube + NoLighting + Blend 渐隐）+ 单 Timer（~50fps）推进弹道：
//   - 书架位计算复用 World::countBookshelvesAround 的同款规则（水平切比雪夫 ==2 环带 × y/y+1 两层 +
//     半步格空气），在 JS 只读 world.blockAt 重算（呈现层自治，不新增 World API）。
//   - active（宿主绑 window.enchantingTableOpen）时每 500ms 从随机参与书架顶面 spawn 1-2 颗符文，
//     初速指向附魔台中心（慢速漂向 + 轻浮力上飘 → 「书架 → 附魔台」的流向感）；寿命 ~1.8s 渐隐。
//   - burstRunes(n)（附魔成功时 Main.qml 转发）一次迸发 n 颗（同 spawn 路径）。
//   - 池满静默丢（不阻塞不 new，同 BlockParticles）；面板关 → active=false → Timer 停、在飞符文自然
//     寿终（渐隐完归位）。
//
// 坐标空间：本 Node 经 Main.qml 的 runeLoader.onLoaded 领养进 particlesHost 锚点（t16：否则 Loader
//   加载到的 3D Node parent=null → 孤儿不渲染）。emitter 坐标即世界坐标（书架格中心 / 附魔台格中心）。
Node {
    id: root

    // 宿主注入（Main.qml runeLoader.onLoaded）：World（blockAt 只读查书架）+ 附魔台方块坐标 +
    //   active（附魔台 UI 开）+ editRev（放 / 破方块版本号 —— 书架位重扫触发）。
    property var world: null
    property int tableX: 0
    property int tableY: 0
    property int tableZ: 0
    property bool active: false
    property int editRev: 0

    // ---- 池配置 ----
    // 池容：常驻漂浮 ~8-16 + 附魔迸发余量。24 足以吸收连点不丢粒。
    readonly property int poolSize: 24
    property var pool: []

    // 参与书架位缓存（[{x,y,z}, ...]）。editRev / active 变化时重扫（QML 绑定经 JS 函数直读 world，
    //   不建 NOTIFY 依赖 —— editRev 显式触碰是唯一刷新源，同 EnchantingTableUI bookshelfPower 模式）。
    property var shelfCells: []

    // 符文色板（呈现层视觉约定：神秘紫系为主 + 冷青 / 亮白点缀；通用配色，非任何专有色）。
    readonly property var runeColors: ["#b06ae8", "#8a4ad8", "#6ab8e8", "#e8e8f8", "#c88ae8"]

    Component.onCompleted: {
        root.pool = []
        for (let i = 0; i < root.poolSize; i++) {
            const m = runeComponent.createObject(root)
            m.visible = false
            root.pool.push({ obj: m, vel: null, swayPhase: 0.0, swayAmp: 0.0,
                             life: 0.0, maxLife: 1.0, active: false })
        }
        tickTimer.start()
        console.info("[t649] EnchantRunes ready; pool=" + root.poolSize
                     + " (Model+Timer pool; no Particles3D dependency)")
    }

    // 重扫参与书架位（World::countBookshelvesAround 同规则：切比雪夫 ==2 环带 × y/y+1 两层 + 半步格 Air）。
    //   editRev 变（放 / 破书架）或 active 翻真时重算；world 空 / 无书架 → 空表（无粒子，静默）。
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

    onActiveChanged:        rescanShelves()
    onEditRevChanged:      rescanShelves()
    onTableXChanged:       rescanShelves()
    onTableYChanged:       rescanShelves()
    onTableZChanged:       rescanShelves()

    // 常驻漂浮 Timer：active 且有书架 → 每 500ms spawn 1-2 颗（随机书架位）。停摆条件在 onTriggered 内
    //   早退（restart 语义简单：active=false 后不再 spawn，在飞符文仍由 tickTimer 推进至寿终）。
    Timer {
        id: spawnTimer
        interval: 500
        repeat: true
        running: root.active
        onTriggered: {
            if (root.shelfCells.length === 0) return
            const n = 1 + Math.floor(Math.random() * 2)
            for (let i = 0; i < n; ++i) root.spawnRune()
        }
    }

    // 附魔成功迸发（Main.qml burstEnchantRunes 转发；同 spawn 路径，量稍密 = 施放瞬间的符文涌动）。
    function burstRunes(n) {
        if (root.shelfCells.length === 0) rescanShelves()
        if (root.shelfCells.length === 0) return
        for (let i = 0; i < n; ++i) root.spawnRune()
    }

    // 从随机参与书架顶面向附魔台方向 spawn 一颗符文。速度 = (台心 - 书架心) 归一 × 慢速 + 轻上飘分量
    //   （漂向台心 + 略微上飘的「流向感」）；横向摆动（swayPhase/swayAmp）在 tickTimer 内正弦扰动。
    function spawnRune() {
        const cells = root.shelfCells
        if (cells.length === 0) return
        const c = cells[Math.floor(Math.random() * cells.length)]
        const sx = c.x + 0.5, sy = c.y + 0.6, sz = c.z + 0.5
        // 目标 = 附魔台中心略上方（0.75 高的台面书盒附近）。
        const tx = root.tableX + 0.5, tyy = root.tableY + 0.55, tz = root.tableZ + 0.5
        let dx = tx - sx, dyv = tyy - sy, dz = tz - sz
        const dist = Math.sqrt(dx * dx + dyv * dyv + dz * dz)
        if (dist < 0.01) return
        const speed = 0.55 + Math.random() * 0.35
        const life = dist / speed + 0.35     // 恰好飘到台心附近 + 少量余量渐隐
        const arr = root.pool
        for (let i = 0; i < arr.length; i++) {
            const p = arr[i]
            if (p.active) continue
            p.active = true
            p.life = life
            p.maxLife = life
            p.vel = Qt.vector3d(dx / dist * speed, dyv / dist * speed + 0.12, dz / dist * speed)
            p.swayPhase = Math.random() * Math.PI * 2
            p.swayAmp = 0.15 + Math.random() * 0.2
            const m = p.obj
            m.px = sx; m.py = sy; m.pz = sz
            m.runeColor = root.runeColors[Math.floor(Math.random() * root.runeColors.length)]
            m.runeOpacity = 1.0
            m.scl = 0.05 + Math.random() * 0.03
            m.visible = true
            return
        }
        // 池满：静默丢（同 BlockParticles）
    }

    // 弹道推进 Timer（~50fps）：直线漂向台心 + 正弦横向摆动 + 渐隐；到期归位入池。
    Timer {
        id: tickTimer
        interval: 20
        repeat: true
        running: false
        onTriggered: {
            const dt = 0.020
            const arr = root.pool
            for (let i = 0; i < arr.length; i++) {
                const p = arr[i]
                if (!p.active) continue
                p.life -= dt
                if (p.life <= 0) {
                    p.active = false
                    p.obj.visible = false
                    continue
                }
                p.swayPhase += dt * 3.0
                const m = p.obj
                const sway = Math.sin(p.swayPhase) * p.swayAmp * dt
                m.px = m.px + p.vel.x * dt + sway
                m.py = m.py + p.vel.y * dt
                m.pz = m.pz + p.vel.z * dt + Math.cos(p.swayPhase) * p.swayAmp * dt
                m.runeOpacity = Math.min(1.0, p.life / p.maxLife * 1.6)
            }
        }
    }

    // 池元素模板（BlockParticles 同模式）：UnitCube + NoLighting + Blend 渐隐。
    Component {
        id: runeComponent
        Model {
            id: rune
            geometry: UnitCube {}
            visible: false
            property real px: 0.0
            property real py: 0.0
            property real pz: 0.0
            property real scl: 0.06
            property color runeColor: "#b06ae8"
            property real runeOpacity: 1.0
            position: Qt.vector3d(rune.px, rune.py, rune.pz)
            scale: Qt.vector3d(rune.scl, rune.scl, rune.scl)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: rune.runeColor
                opacity: rune.runeOpacity
                alphaMode: PrincipledMaterial.Blend   // 渐隐（连续 alpha）走 Blend
            }
        }
    }
}
