import QtQuick
import QtQuick3D
import QtQuick3D.Particles3D

// t390 环境点缀粒子（呈现层；PLAN §2 分层 — 只读消费 World 天气 / 群系 + torchPositions，绝不反向写栅格）。
// 经 Main.qml 的 ambientLoader 动态加载（同 BlockParticles / WeatherParticles / TorchSmoke 的 Particles3D
//   隔离模式 — 模块运行期缺失时仅本 Loader 失败 + 显式告警，Main.qml 仍正常加载，§2-E「保持运行而非崩溃，
//   且不静默吞」）。Particles3D 已由既有粒子 Loader 部署，无需新增模块部署。
//
// 三套点缀 emitter（spec「各场景有点缀粒子，不抢戏」；量克制）：
//   (1) 雨溅：降水态（Rain/Thunder）玩家脚边偶发小水花（联动 t385 weatherStateAt）。
//   (2) 叶飘：森林群系（biomeIdAt==Forest）非雨态时玩家头顶慢落飘叶。
//   (3) 火把火星：每火把顶部偶发明亮短命火星（Repeater 据 torchPositions；与 TorchSmoke 烟雾互补）。
//
// 坐标空间：根 Node 留世界原点（同 TorchSmoke）—— 火把火星 emitter 用世界坐标（火把格固定位置）；
//   雨溅 / 叶飘 emitter 的 position 绑定 player.position（眼位）跟随玩家（emitter 移动 = 粒子云随玩家；
//   等价 WeatherParticles 把 Node 绑 player.position 的做法，区别只是此处绑 emitter 而非 Node，
//   因 Node 需留原点让火星 emitter 用绝对世界坐标）。
Node {
    id: root

    // 由 Main.qml 经 ambientLoader.onLoaded 注入：World（查天气 / 群系）+ PlayerController（眼位跟随）
    //   + torchPositions ListModel（引用稳定，Repeater 反应式跟随增删，同 TorchSmoke）。
    property var world: null
    property var player: null
    property var torchModel: null

    // 局部降水类型（0=Clear / 1=Rain / 2=Snow / 3=Thunder；推导同 WeatherParticles.precipType）。
    //   触碰 world.weatherState（NOTIFY weatherChanged）+ player.position → 天气翻转 / 玩家跨群系时重算。
    readonly property int precipType: {
        if (!world || !player) return 0
        const _ws = world.weatherState   // 触碰 NOTIFY weatherChanged（驱动天气翻转时重算）
        const p = player.position        // 触碰 positionChanged（驱动玩家跨群系时重算）
        return world.weatherStateAt(Math.floor(p.x), Math.floor(p.z))
    }
    readonly property bool raining: precipType === 1 || precipType === 3   // Rain / Thunder → 雨溅
    // 玩家所在群系是否森林（编码 3=Forest，见 World::biomeIdAt）。叶飘仅森林；触碰 player.position 反应式。
    readonly property bool inForest: {
        if (!world || !player) return false
        const p = player.position
        return world.biomeIdAt(Math.floor(p.x), Math.floor(p.z)) === 3
    }

    Component.onCompleted: console.info("[t390] AmbientParticles ready; rain=" + root.raining + " forest=" + root.inForest + " torches=" + (root.torchModel ? root.torchModel.count : 0))

    ParticleSystem3D {
        id: ambientSys
        running: true

        // === (1) 雨溅：玩家脚边扁平盘小水花，降水态发射（联动 t385）===
        ModelParticle3D {
            id: splashParticle
            maxAmount: 60   // 池容：emitRate 30 × lifespan 0.35s ≈ 10 并发，留余量
            color: "#a8c0e0"  // 蓝灰水花（呈现层视觉约定色；非用户可见命名）
            colorVariation: Qt.vector4d(0.10, 0.10, 0.12, 0.20)
            fadeInEffect: ModelParticle3D.FadeScale
            fadeInDuration: 40
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 200
            hasTransparency: true
            delegate: splashDelegate
        }
        ParticleEmitter3D {
            id: splashEmitter
            particle: splashParticle
            // 跟随玩家眼位，y -1.6 近脚 / 地表（眼位 ≈ 脚 +1.62；近似地表，地下场景亦不抢戏）。
            position: root.player ? Qt.vector3d(root.player.position.x,
                                                 root.player.position.y - 1.6,
                                                 root.player.position.z)
                                  : Qt.vector3d(0, 0, 0)
            emitRate: root.raining ? 30 : 0   // 仅雨 / 雷态；Clear / 雪 → 0
            lifeSpan: 350
            lifeSpanVariation: 80
            particleScale: 0.04               // 小颗粒（大小由 emitter.particleScale 决定，见 debris 红线）
            // 扁平盘（XZ 6×6、Y 薄）→ 水花散在玩家四周近地表。
            shape: ParticleShape3D {
                type: ParticleShape3D.Box
                size: Qt.vector3d(6, 0.4, 6)
                fill: true
            }
            // 水花轻溅：略向上 + 横向外散（受 Gravity3D 落回地表）。
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, 1.2, 0)
                directionVariation: Qt.vector3d(1.6, 0.5, 1.6)
            }
        }
        Gravity3D {
            particles: [splashParticle]
            direction: Qt.vector3d(0, -1, 0)
            magnitude: 10   // 水花溅起后落回（非漂浮）
        }

        // === (2) 叶飘：森林群系玩家头顶慢落飘叶（非雨态；雨态让位于降水）===
        ModelParticle3D {
            id: leafParticle
            maxAmount: 120  // 池容：emitRate 10 × lifespan 8s ≈ 80 并发，留余量
            color: "#5a8a3a"   // 叶绿（呈现层约定；近 BlockParticles blockColor leaves #4f7f33）
            colorVariation: Qt.vector4d(0.18, 0.18, 0.10, 0.0)
            fadeInEffect: ModelParticle3D.FadeOpacity
            fadeInDuration: 600
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 800
            hasTransparency: true
            delegate: leafDelegate
        }
        ParticleEmitter3D {
            id: leafEmitter
            particle: leafParticle
            // 跟随玩家眼位，y +8（头顶上方落下来；发射体积跨 [+4,+12]）。
            position: root.player ? Qt.vector3d(root.player.position.x,
                                                 root.player.position.y + 8,
                                                 root.player.position.z)
                                  : Qt.vector3d(0, 0, 0)
            emitRate: (root.inForest && !root.raining) ? 10 : 0   // 森林 + 非雨态（雨态让位于雨 / 雨溅）
            lifeSpan: 8000
            lifeSpanVariation: 2000
            particleScale: 0.07                // 叶片可见（略大于碎屑）
            particleRotationVelocityVariation: Qt.vector3d(80, 80, 80)   // 飘落旋转（叶面翻飞）
            shape: ParticleShape3D {
                type: ParticleShape3D.Box
                size: Qt.vector3d(18, 8, 18)
                fill: true
            }
            // 慢落 + 较大横向游移（叶片随风飘；无 Gravity → 用速度直落，游移由 variation 给）。
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, -1.2, 0)
                directionVariation: Qt.vector3d(1.0, 0.3, 1.0)
            }
        }

        // === (3) 火把火星：每火把顶部偶发明亮短命火星（Repeater 据 torchPositions；与 TorchSmoke 烟互补）===
        ModelParticle3D {
            id: sparkParticle
            // 池容上限：约 60 火把 × ~2 在飞 ≈ 120（少量火星天花板；超出按 age 回收最老）。
            maxAmount: 120
            color: "#ffb24a"   // 暖橙黄（火星呈现层约定色；非用户可见命名）
            colorVariation: Qt.vector4d(0.15, 0.10, 0.0, 0.20)
            fadeInEffect: ModelParticle3D.FadeScale
            fadeInDuration: 30
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 150
            hasTransparency: true
            delegate: sparkDelegate
        }
        Gravity3D {
            particles: [sparkParticle]
            direction: Qt.vector3d(0, -1, 0)
            magnitude: 6   // 火星上飘后受弱重力回落（火苗余烬感）
        }
        // 每火把一个 emitter（Repeater 据 torchModel 增删；同 TorchSmoke 模式）。慢速上飘 + 横向轻扰。
        Repeater {
            model: root.torchModel
            delegate: ParticleEmitter3D {
                particle: sparkParticle
                // 世界坐标（同 TorchSmoke emitter 位置：火把格底面中心 + 焰顶上方 0.85 y）。
                position: Qt.vector3d(model.x + 0.5, model.y + 0.85, model.z + 0.5)
                emitRate: 2.5          // 每秒 ~2-3 颗（spec「少量点缀」；与 TorchSmoke 烟雾 1.8 互补不抢戏）
                lifeSpan: 450
                lifeSpanVariation: 120
                particleScale: 0.018    // 小亮点（< 烟雾 0.045；火星颗粒感，大小由 particleScale 决定）
                velocity: VectorDirection3D {
                    // 上飘（快于烟雾 0.55；火星是活跃火苗）+ 横向轻扰。
                    direction: Qt.vector3d(0, 1.4, 0)
                    directionVariation: Qt.vector3d(0.5, 0.3, 0.5)
                }
            }
        }
    }

    // --- delegates：小立方 + NoLighting 白底（particle color 着色生效；lessons-learned「所有可见
    //     Model 用 NoLighting」+「ModelParticle3D 忽略 delegate scale，真实大小由 emitter.particleScale 决定」，
    //     故 delegate 显式 scale 仅作双保险）---
    Component {
        id: splashDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.04, 0.04, 0.04)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
    }
    Component {
        id: leafDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.07, 0.07, 0.07)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
    }
    Component {
        id: sparkDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.02, 0.02, 0.02)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#ffffff" }
        }
    }
}
