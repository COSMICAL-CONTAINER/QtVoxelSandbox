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
    //   t500 perf：包测本轮弹道推进耗时，经 FrameProfiler.addSampleMs("bp", ms) 推进 bp 桶（窗口总 ms）。
    //     用户「碎屑运动慢 + 更卡」需区分「被 mob tick 吃满帧 → 粒子掉帧」vs「池本身扫描开销」——本桶给数。
    Timer {
        id: tickTimer
        interval: 20
        repeat: true
        running: false
        onTriggered: {
            // t500 perf：测本轮 onTriggered 总耗时（含 Date.now 调用，本身 ~0）。
            const t0 = (typeof performance !== "undefined") ? performance.now() : Date.now()
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
            // t500 perf：推 ms 样本到 bp 桶（FrameProfiler 单例，QML_GLOBAL 进程唯一）。
            const t1 = (typeof performance !== "undefined") ? performance.now() : Date.now()
            FrameProfiler.addSampleMs("bp", t1 - t0)
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

    // 方块 → 粒子主色（呈现层视觉约定色；非用户可见命名，对齐 BlockRegistry 全枚举）。
    //   纯呈现决策，故随粒子节点放一起（不污染 World 数据层）。色取各方块贴图主色（草 / 叶绿系、
    //   木棕系、石灰系、沙黄系、矿石按矿斑色），仅取一格近拟——碎屑是抛落小颗粒、肉眼辨方块材质即可，
    //   无需逐像素精确。
    //
    // **t491 关键**：旧版 switch 只覆盖 id 1..8，id > 8 全落 default → 白。用户破「草」（最常见是 worldgen
    //   在地表散布的草丛 TallGrass=24，中文「草」的日常指代）迸发白粒子，正是 24 落 default 的症状（机制
    //   上 Grass=1 本应显绿，但用户实测的「草」实际是 TallGrass=24）。本任务把表扩到全枚举，使常见方块
    //   破块都迸材质色而非白；仍保留 default 作未来新方块的兜底（新方块先显白，肉眼即可察觉缺色并补表）。
    //
    // **跨块掉落路径同源**：dropUnsupportedCropsAround / dropCactusColumn / tickLavaFlow 焚毁等系统事件
    //   也发 blockBroken → 同走本表取色。故本表是全工程「破块粒子色」单一权威（PLAN §2 单一权威；避免
    //   多处各持色表漂移）。
    function blockColor(id) {
        switch (id) {
            // ── 地表 / 土沙族（id 1..8）── 既有色（视觉零回归）
            case 1: return "#6aaa3f" // grass（草方块；顶面草绿）
            case 2: return "#7a5a3c" // dirt（泥土）
            case 3: return "#8a8a8a" // stone（石头）
            case 4: return "#6e6e6e" // cobble（圆石）
            case 5: return "#6b4f2a" // log（橡木原木）
            case 6: return "#b08a4f" // planks（橡木木板）
            case 7: return "#4f7f33" // leaves（橡树叶）
            case 8: return "#d8c896" // sand（沙子）
            // ── 功能 / 基础族（id 9..14）──
            case 9: return "#9a7a4a"  // crafting_table（工作台，木板棕）
            case 10: return "#8a8a8a" // furnace（熔炉，石灰）
            case 11: return "#3a3a3a" // coal_ore（煤矿石，深灰底）
            case 12: return "#b8a89a" // iron_ore（铁矿石，浅褐斑）
            case 13: return "#e8c060" // torch（火把，火焰暖黄）
            case 14: return "#3a3a3a" // bedrock（基岩，深灰）
            // ── 异形木半方块族（id 15..20）── 同 planks 木板棕
            case 15: return "#b08a4f" // wood_slab
            case 16: return "#b08a4f" // wood_stairs
            case 17: return "#b08a4f" // wood_fence
            case 18: return "#b08a4f" // wood_pressure_plate
            case 19: return "#b08a4f" // wood_door
            case 20: return "#b08a4f" // wood_trapdoor
            // ── 流体 / 容器 / 耕地（id 21..23）──
            case 21: return "#3f6fd8" // water（水，蓝）
            case 22: return "#9a7a4a" // chest（箱子，木板棕）
            case 23: return "#6a4a2c" // farmland（耕地，湿土棕）
            // ── cross 草本族（id 24..25）── t491 核心：草丛迸绿、作物迸麦黄
            case 24: return "#5a8a35" // tall_grass（草丛；t491 核心修复——用户「草」白粒子真因）
            case 25: return "#b8a040" // wheat_crop（小麦作物，麦穗黄绿）
            // ── 矿石 / 羊毛 / 树苗 / 流体续（id 26..31）──
            case 26: return "#4ab8b8" // diamond_ore（钻石矿石，青白斑）
            case 27: return "#e8e8e0" // wool（白色羊毛）
            case 28: return "#5a8a35" // sapling（树苗，嫩叶绿）
            case 29: return "#c87a3a" // copper_ore（铜矿石，橙铜斑）
            case 30: return "#e8c850" // gold_ore（金矿石，金黄斑）
            case 31: return "#d85020" // lava（岩浆，红橙）
            // ── 床 8 色变体（id 32..39）── 被面色
            case 32: return "#c83030" // bed_red
            case 33: return "#e88040"  // bed_orange（近似）
            case 34: return "#e8d040" // bed_yellow
            case 35: return "#4aaa3a" // bed_green
            case 36: return "#3a9a9a" // bed_cyan
            case 37: return "#3a5ad8" // bed_blue
            case 38: return "#c83ac8" // bed_magenta
            case 39: return "#3a3a3a" // bed_black
            case 40: return "#4a6a7a" // spawner（刷怪笼，暗蓝灰）
            // ── 沙漠族（id 41..43）──
            case 41: return "#d8c896" // sandstone（砂岩，沙黄）
            case 42: return "#3a7a3a" // cactus（仙人掌，深绿）
            case 43: return "#7a5a3a" // dead_bush（枯灌木，枯褐）
            // ── 雪原族（id 44..46）──
            case 44: return "#f0f0f0" // snow_layer（积雪，冷白）
            case 45: return "#a8c8e8" // ice（冰，浅蓝）
            case 46: return "#4a3a28" // spruce_log（云杉原木，深棕）
            // ── 沼泽植物（id 47..48）──
            case 47: return "#3a8a4a" // lily_pad（睡莲，绿）
            case 48: return "#c83030" // mushroom（蘑菇，红菌盖）
            // ── 花 + 甘蔗（id 49..53）── 按花头色
            case 49: return "#c83030" // flower_red
            case 50: return "#e8d040" // flower_yellow
            case 51: return "#3a5ad8" // flower_blue
            case 52: return "#e8e8e0" // flower_white
            case 53: return "#6a9a3a" // sugarcane（甘蔗，绿茎）
            case 54: return "#c8d8d8" // glass（玻璃，近白青）
            // ── 作物 cross（id 55..56）── 同小麦作物黄绿
            case 55: return "#7aaa3a" // carrot_crop（胡萝卜作物，绿叶）
            case 56: return "#6a8a3a" // potato_crop（马铃薯作物，绿叶）
            case 57: return "#2a1838" // obsidian（黑曜石，深紫黑）
            // ── 圆石变体（id 58..61）── 同 cobble 灰
            case 58: return "#6e6e6e" // cobble_slab
            case 59: return "#6e6e6e" // cobble_stairs
            case 60: return "#6e6e6e" // cobble_fence
            case 61: return "#6e6e6e" // cobble_pressure_plate
            case 62: return "#6b4f2a" // ladder（木梯，棕）
            // ── 15 色羊毛变体（id 63..77）── 按色名
            case 63: return "#e88040"  // wool_orange（近似）
            case 64: return "#c83ac8" // wool_magenta
            case 65: return "#3a8ad8" // wool_light_blue
            case 66: return "#e8d040" // wool_yellow
            case 67: return "#5ad84a" // wool_lime
            case 68: return "#e88aa8" // wool_pink
            case 69: return "#5a5a5a" // wool_gray
            case 70: return "#8a8a8a" // wool_light_gray
            case 71: return "#3a8a8a" // wool_cyan
            case 72: return "#7a3a9a" // wool_purple
            case 73: return "#3a3ad8" // wool_blue
            case 74: return "#6a4a2a" // wool_brown
            case 75: return "#4aaa3a" // wool_green
            case 76: return "#c83030" // wool_red
            case 77: return "#3a3a3a" // wool_black
            // ── 8 色床补齐（id 78..85）── 同色羊毛色
            case 78: return "#e8e8e0" // bed_white
            case 79: return "#3a8ad8" // bed_light_blue
            case 80: return "#5ad84a" // bed_lime
            case 81: return "#e88aa8" // bed_pink
            case 82: return "#5a5a5a" // bed_gray
            case 83: return "#8a8a8a" // bed_light_gray
            case 84: return "#7a3a9a" // bed_purple
            case 85: return "#6a4a2a" // bed_brown
            // ── 云杉木制品（id 86..89）── 深棕（同 spruce_log）
            case 86: return "#4a3a28" // spruce_planks
            case 87: return "#4a3a28" // spruce_slab
            case 88: return "#4a3a28" // spruce_fence
            case 89: return "#4a3a28" // spruce_door
            case 90: return "#8a2a2a" // sweet_berry_bush（浆果灌木，红果绿叶 → 取红果色）
            // ── 冰续（id 91..92）── 同 ice
            case 91: return "#b8d8e8" // pack_ice（浮冰，白蓝）
            case 92: return "#8ab8d8" // blue_ice（蓝冰，更蓝）
            case 93: return "#2a4ad8" // lapis_ore（青金矿石，群青蓝）
            case 94: return "#3a2a4a" // enchanting_table（附魔台，黑曜石底深紫黑）
            case 95: return "#9a7a4a" // bookshelf（书架，木板棕）
            case 96: return "#d8d8d8" // iron_block（铁块，金属灰）
            // ── 铁砧 3 阶段（id 97..99）── 同铁深灰
            case 97: return "#4a4a4a" // anvil（铁砧完好）
            case 98: return "#4a4a4a" // anvil_chipped
            case 99: return "#4a4a4a" // anvil_damaged
            // ── 防御造物方块（id 100..101）──
            case 100: return "#e88040" // pumpkin（南瓜，橙）
            case 101: return "#f0f0f0" // snow（雪块，冷白）
            // ── 矿井结构（id 102..103）──
            case 102: return "#e8e8e0" // cobweb（蛛网，灰白）
            case 103: return "#6a4a2a" // rail（铁轨，棕枕木 + 灰铁 → 取棕）
            // ── 沙漠神殿（id 104..105）──
            case 104: return "#c83020" // tnt（TNT，深红药柱）
            case 105: return "#d8c896" // cut_sandstone（切制砂岩，沙黄）
            // ── 丛林神殿（id 106..107）──
            case 106: return "#6a6a5a" // mossy_cobble（苔石，圆石灰 + 苔绿 → 取苔灰绿）
            case 107: return "#8a8a8a" // dispenser（发射器，石灰）
            // ── 要塞结构（id 108..111）──
            case 108: return "#7a7a7a" // stone_brick（石砖，石灰）
            case 109: return "#7a7a7a" // stone_brick_slab
            case 110: return "#7a7a7a" // stone_brick_stairs
            case 111: return "#1a0a2a" // end_portal（末地传送门，深紫黑星空）
            // ── 手动点火机关（id 112..114）──
            case 112: return "#6b4f2a" // lever（杠杆，木质底座棕）
            case 113: return "#6b4f2a" // wood_button（木按钮，棕）
            case 114: return "#8a8a8a" // stone_button（石按钮，灰）
            default: return "#ffffff" // 未来新方块兜底（显白便于察觉缺色并补表）
        }
    }
}
