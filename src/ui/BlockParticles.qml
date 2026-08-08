import QtQuick
import QtQuick3D
import VoxelSandbox // UnitCube（自定义 QQuick3DGeometry；内置 #Cube 静态 Model 在本工程不渲染）

// 破/放/挖/食/爆 粒子节点（t14/t16 → t465 改造）。
// 呈现层消费 World / PlayerController 的语义事件（blockBroken/blockPlaced/miningParticle/
//   eatingParticle/explosion），在动作位置 spawn 小立方体碎屑（向上/外初速度 + 重力下落 + 渐隐）。
//
// **t465 关键改造：不再依赖 Particles3D**（t14/t385 已知 Particles3D 运行期降级 Loader；本任务 spec
//   明令「绝不依赖 Particles3D —— 用 Model 弹道 + Timer」）。改用预分配 Model 池 + 单 Timer 推进弹道：
//   - 启动时一次性 createObject 预分配 poolSize 个 Model（UnitCube + NoLighting），全部 visible=false。
//   - 每次迸发从池中找空闲槽激活（设位置/速度/色/寿命）；池满即丢（无爆量）。
//   - 单 Timer (~50fps) 推进每个激活粒子的弹道（重力下落 + 寿命递减 + 渐隐 opacity），到期归位
//     （visible=false 重新入池等下次复用）。
//   - 池常驻、不增不减 → 无每帧 new Model → 无泄漏（t437 同族坑：孤儿 delegate；本池预分配 + 复用）。
//
// 分层铁律（§2）：触发由 Game 层发，呈现层只消费、绝不反向写栅格。
// 坐标空间：本 Node 经 Main.qml 的 Loader.onLoaded 领养进 particlesHost（t16：否则 Loader 加载到的
//   3D Node parent=null → 孤儿 → 不渲染）。burst 的 position 即世界坐标，方块中心 = (x+0.5, y+0.5, z+0.5)。
// 入口签名（burstBreak/burstMine/burstPlace/burstEat/burstExplosion）保留与旧 Particles3D 版一致 →
//   Main.qml 的 Connections 转发逻辑零改动。
Node {
    id: root

    // ---- 池配置 ----
    // 池容：≥ 单次最大迸发（爆炸 20）+ 并发余量（多次破/挖/食同时触发）。96 足以吸收常规连击不丢粒。
    readonly property int poolSize: 96
    // 池表：[{obj, vel, life, maxLife, active}, ...]（启动时 Component.onCompleted 填满）。
    //   obj = Model 实例（由 particleComponent.createObject 创建）；vel/life 为弹道态；active 复用标志。
    property var pool: []

    // ---- 入口（保留旧 Particles3D 版本签名，Main.qml 不改） ----
    // 破块完成迸发（t61 +30%：原 6 → 8）：碎屑色 = 被破方块主色，主上抛 + 横向收束、强重力下落。
    function burstBreak(x, y, z, id) {
        burst(x, y, z, 8, blockColor(id), 0.07, /*vYBase*/3.0, /*vYVar*/2.0, /*hScale*/1.6, /*life*/0.7)
    }
    // 挖的过程碎屑（t61）：生存累积挖掘时每跨一阶段（player 发 miningParticle）迸发少量碎屑，少量（3 < 破块 8）。
    function burstMine(x, y, z, id) {
        burst(x, y, z, 3, blockColor(id), 0.07, 3.0, 1.5, 1.3, 0.55)
    }
    // 放置扬尘：更少更弱（4 颗、轻起轻落，spec「轻撒非迸裂」）。
    function burstPlace(x, y, z, id) {
        burst(x, y, z, 4, blockColor(id), 0.06, 1.8, 1.0, 1.0, 0.45)
    }
    // 进食屑粒（t267）：面包色，从嘴部（float 世界坐标）迸发；偏上向前（嘴嚼吐出）。
    //   与 burstBreak/burstMine 的差异：原点是 float 世界坐标（玩家眼位）非方块格中心，故不加 +0.5。
    function burstEat(x, y, z) {
        burstFloat(x, y, z, 3, "#d8a838", 0.07, 1.5, 0.8, 1.0, 0.5)
    }
    // 爆炸迸发（Stalker/苦力怕自爆；EntityManager::explosion → Main.qml 路由）：大迸发 + 横向四散 +
    //   强上抛（爆炸冲击波 → 非碎屑的横向炸开）。色 = 白/灰烟光（呈现层视觉约定色），数量多（20 > 破块 8）。
    function burstExplosion(x, y, z) {
        burst(x, y, z, 20, "#d8d8d8", 0.08, 4.5, 2.5, 3.0, 0.9)
    }
    // t449 mob 死亡白烟（机制等价 MC 1.0 mob 倒地消散的白烟 puff；delegate 检测 deadAt 翻 true 时调）。
    //   白色 + 上飘 + **轻浮力（gravity<0 → 持续上飘而非回落）** + 较长寿命渐隐 = 消散感。数量适中（10）
    //   覆盖 mob 体。重力 -1.5（向上浮力；区别碎屑 / 爆炸的下落 14 / 横飞 14）。
    function burstDeathSmoke(x, y, z) {
        burstFloat(x, y, z, 10, "#f0f0f0", 0.09, 1.2, 1.0, 1.0, 0.7, -1.5)
    }

    // 通用方块中心迸发（坐标先 +0.5 到方块中心）。gravity 缺省 14（碎屑强落；t449 加可选参数供烟雾上飘）。
    function burst(x, y, z, count, color, scale, vYBase, vYVar, hScale, life) {
        burstFloat(x + 0.5, y + 0.5, z + 0.5, count, color, scale, vYBase, vYVar, hScale, life, 14.0)
    }
    // 通用 float 坐标迸发：在 (px,py,pz) 周围水平随机角度 + 上抛 + 横向初速度散出。
    //   vYBase + [0,vYVar) 随机上抛分量；hScale 控制横向散开幅度；life + [0,0.25) 寿命抖动。
    //   gravity = 该粒子重力加速度（>0 下落如碎屑 / <0 上飘如烟雾 / 0 惯性）；缺省 14（碎屑手感）。
    function burstFloat(px, py, pz, count, color, scale, vYBase, vYVar, hScale, life, gravity) {
        const g = (gravity === undefined) ? 14.0 : gravity
        for (let i = 0; i < count; i++) {
            const angle = Math.random() * Math.PI * 2
            const hSpeed = (0.4 + Math.random() * 0.8) * hScale
            const vx = Math.cos(angle) * hSpeed
            const vz = Math.sin(angle) * hSpeed
            const vy = vYBase + Math.random() * vYVar
            spawnParticle(px, py, pz, vx, vy, vz, color, scale, life + Math.random() * 0.25, g)
        }
    }

    // 从池中找空闲槽激活一颗粒子；池满则丢（防爆量、防 new）。gravity = 该粒子重力（默认碎屑 14）。
    function spawnParticle(x, y, z, vx, vy, vz, color, scale, life, gravity) {
        const arr = root.pool
        for (let i = 0; i < arr.length; i++) {
            const p = arr[i]
            if (p.active) continue
            p.active = true
            p.life = life
            p.maxLife = life
            p.gravity = (gravity === undefined) ? 14.0 : gravity
            p.vel = Qt.vector3d(vx, vy, vz)
            const m = p.obj
            m.x = x; m.y = y; m.z = z      // 直写 position 三轴（避免每帧 new vector3d）
            m.particleColor = color
            m.scl = scale                  // 统一三轴缩放（per-axis 等值）
            m.particleOpacity = 1.0
            m.visible = true
            return
        }
        // 池满：丢弃这颗（不阻塞、不 new、不报警——爆量上限即此静默丢）
    }

    // 启动时预分配池（一次性 createObject，运行期不 new）。Loader.onLoaded 把本 root 领养进
    //   particlesHost 后，本 onCompleted 触发 → 池填满 → tickTimer.start()。
    Component.onCompleted: {
        root.pool = []
        for (let i = 0; i < root.poolSize; i++) {
            const m = particleComponent.createObject(root)
            m.visible = false
            root.pool.push({ obj: m, vel: null, life: 0.0, maxLife: 1.0, active: false, gravity: 14.0 })
        }
        tickTimer.start()
        console.info("[t465] BlockParticles ready; pool=" + root.poolSize
                     + " (Model+Timer pool; no Particles3D dependency)")
    }

    // 弹道推进 Timer：~50fps（20ms）。每帧推进激活粒子的弹道 + 渐隐，到期归位（visible=false 入池等复用）。
    //   重力 = per-particle p.gravity（碎屑 14 强落 / 烟雾 -1.5 上飘 / 惯性 0）；t449 加 per-particle 以支持
    //   死亡白烟上飘（旧版常量 14 会让烟弹起即落，不像「消散」）。
    //   渐隐：opacity = clamp(life / maxLife * 1.5)，使后 2/3 寿命平滑淡出（前 1/3 保满 alpha 显眼）。
    //   dt 取 interval/1000 标称值（不读实际墙钟——视觉弹道对微小帧率波动不敏感，且免每帧读 clock 开销）。
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
                // 弹道：Y 速度按 p.gravity 递变（>0 减速下落如碎屑 / <0 加速上飘如烟）。
                const v = p.vel
                const nvx = v.x
                const nvy = v.y - p.gravity * dt
                const nvz = v.z
                p.vel = Qt.vector3d(nvx, nvy, nvz)
                const m = p.obj
                m.x = m.x + nvx * dt
                m.y = m.y + nvy * dt
                m.z = m.z + nvz * dt
                m.particleOpacity = Math.min(1.0, p.life / p.maxLife * 1.5)
            }
        }
    }

    // 池元素模板：UnitCube + NoLighting（已验证可见路径，PLAN §lighting 不变量）。
    //   x/y/z/scl/particleColor/particleOpacity 为 per-instance 属性，由 spawnParticle 设值；
    //   内层 Model 的 position/scale 与 PrincipledMaterial 的 baseColor/opacity 经绑定读它们 →
    //   属性变 → 绑定重算 → 视觉跟随（同 viewModelHand 的 playerModel.hurtTint 模式）。
    //   alphaMode:Blend + opacity（t439/t442 契约：透明段用 Blend，非 opacity hack）。
    Component {
        id: particleComponent
        Model {
            id: particle
            geometry: UnitCube {}
            visible: false
            // per-instance 弹道态（spawn 时一次性设 + Timer 每帧改 x/y/z 与 particleOpacity）
            property real x: 0.0
            property real y: 0.0
            property real z: 0.0
            property real scl: 0.07
            property color particleColor: "#ffffff"
            property real particleOpacity: 1.0
            position: Qt.vector3d(particle.x, particle.y, particle.z)
            scale: Qt.vector3d(particle.scl, particle.scl, particle.scl)
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: particle.particleColor
                opacity: particle.particleOpacity
                alphaMode: PrincipledMaterial.Blend   // t439/t442：渐隐（连续 alpha）走 Blend
            }
        }
    }

    // 方块 → 粒子主色（呈现层视觉约定色；非用户可见命名，对齐 BlockRegistry id 0..8）。
    //   纯呈现决策，故随粒子节点放一起（不污染 World 数据层）。与旧 Particles3D 版完全一致 → 视觉零回归。
    function blockColor(id) {
        switch (id) {
            case 1: return "#6aaa3f" // grass
            case 2: return "#7a5a3c" // dirt
            case 3: return "#8a8a8a" // stone
            case 4: return "#6e6e6e" // cobble
            case 5: return "#6b4f2a" // log
            case 6: return "#b08a4f" // planks
            case 7: return "#4f7f33" // leaves
            case 8: return "#d8c896" // sand
            default: return "#ffffff"
        }
    }
}
