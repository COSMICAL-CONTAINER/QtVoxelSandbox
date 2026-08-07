import QtQuick
import QtQuick3D
import QtQuick3D.Particles3D

// 天气降水粒子（t385 雨 / 雪覆盖层）：呈现层消费 World 的天气态（weatherStateAt 群系解析）。
// 本文件经 Main.qml 的 Loader 动态加载——Particles3D 模块运行期缺失时仅该 Loader 失败、
// 显式降级（Loader.Error → console.warn 告警，§2-E「保持运行而非崩溃，且不静默吞」），
// 顶层 Main.qml 仍正常加载（同 BlockParticles.qml / TorchSmoke.qml 隔离模式）。
// 分层铁律（§2）：天气态由 World(Game 层) 算，呈现层只读消费、绝不反向写。
// 坐标空间：本 Node 经 Main.qml 的 Loader.onLoaded 领养进 particlesHost 场景锚点 Node（同 t16 修复，
// 否则 Loader 加载出的 3D Node parent=null → 孤儿 → 不渲染）；Node 跟随玩家眼位（粒子云始终笼罩玩家）。
Node {
    id: root

    // 由 Main.qml 注入（Loader.onLoaded）：World 实例（查天气态）+ PlayerController（眼位跟随）。
    property var world: null
    property var player: null

    // 粒子云跟随玩家眼位（player.position = 眼位）。降水是「头顶落下的覆盖层」，跟随玩家移动始终笼罩。
    position: player ? player.position : Qt.vector3d(0, 0, 0)

    // 局部降水类型（群系解析；0=Clear / 1=Rain / 2=Snow / 3=Thunder）。
    //   触碰 world.weatherState（NOTIFY weatherChanged）+ player.position → 天气翻转或玩家跨群系时重算。
    readonly property int precipType: {
        if (!world || !player) return 0
        const _ws = world.weatherState   // 触碰 NOTIFY weatherChanged（驱动天气翻转时重算）
        const p = player.position        // 触碰 positionChanged（驱动玩家跨群系时重算）
        return world.weatherStateAt(Math.floor(p.x), Math.floor(p.z))
    }
    readonly property bool raining: precipType === 1 || precipType === 3   // Rain / Thunder → 雨
    readonly property bool snowing: precipType === 2                       // Snow → 雪

    Component.onCompleted: console.info("[t385] WeatherParticles ready; ParticleSystem3D running =", weatherSys.running)

    ParticleSystem3D {
        id: weatherSys
        running: true

        // --- 雨（含雷态）：蓝灰小颗粒快速直落 ---
        ModelParticle3D {
            id: rainParticle
            maxAmount: 900   // 池容：emitRate 500 × lifespan 0.9s ≈ 450 并发，留余量
            color: "#9fb4d6"  // 蓝灰（呈现层视觉约定色；非用户可见命名）
            colorVariation: Qt.vector4d(0.08, 0.08, 0.10, 0.0)
            fadeInEffect: ModelParticle3D.FadeOpacity
            fadeInDuration: 150
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 150
            hasTransparency: true
            delegate: rainDelegate
        }
        ParticleEmitter3D {
            id: rainEmitter
            particle: rainParticle
            emitRate: root.raining ? 500 : 0   // 仅雨 / 雷态发射；Clear / 雪 → 0
            lifeSpan: 900
            lifeSpanVariation: 150
            particleScale: 0.035               // 小颗粒（≤ 0.07；ModelParticle3D 实例忽略 delegate scale，由此决定）
            // 发射体积：玩家头顶一片 XZ 范围（24×24）+ Y 偏上（中心 +8 → 跨 [+4,+12]）。
            position: Qt.vector3d(0, 8, 0)
            shape: ParticleShape3D {
                type: ParticleShape3D.Box
                size: Qt.vector3d(24, 8, 24)
                fill: true
            }
            // 直落（-Y 18 格/s）+ 轻微横向抖动。
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, -18, 0)
                directionVariation: Qt.vector3d(1.2, 2.0, 1.2)
            }
        }

        // --- 雪：白色小片慢漂 + 横向游移 ---
        ModelParticle3D {
            id: snowParticle
            maxAmount: 700   // 池容：emitRate 120 × lifespan 3.5s ≈ 420 并发，留余量
            color: "#f4f6fa"  // 近白
            colorVariation: Qt.vector4d(0.05, 0.05, 0.05, 0.0)
            fadeInEffect: ModelParticle3D.FadeOpacity
            fadeInDuration: 200
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 300
            hasTransparency: true
            delegate: snowDelegate
        }
        ParticleEmitter3D {
            id: snowEmitter
            particle: snowParticle
            emitRate: root.snowing ? 120 : 0   // 仅雪态发射；Clear / 雨 → 0
            lifeSpan: 3500
            lifeSpanVariation: 600
            particleScale: 0.05                // 略大于雨（雪花可见）
            position: Qt.vector3d(0, 8, 0)
            shape: ParticleShape3D {
                type: ParticleShape3D.Box
                size: Qt.vector3d(24, 8, 24)
                fill: true
            }
            // 慢落（-Y 1.5 格/s）+ 较大横向游移（雪花飘）。
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, -1.5, 0)
                directionVariation: Qt.vector3d(0.9, 0.4, 0.9)
            }
        }
    }

    // 雨滴 delegate：小立方（#Cube）+ NoLighting 白底（particle color 着色生效；lit 红线 — 默认 lit 不渲染）。
    // 尺寸由 emitter.particleScale 决定（delegate 显式 scale 被 ModelParticle3D 忽略，仅留双保险）。
    Component {
        id: rainDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.03, 0.03, 0.03)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
    }
    // 雪花 delegate：小立方 + NoLighting 白底（同上；particle color 近白）。
    Component {
        id: snowDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.05, 0.05, 0.05)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
    }
}
