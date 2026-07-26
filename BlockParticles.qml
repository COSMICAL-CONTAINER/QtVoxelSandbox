import QtQuick
import QtQuick3D
import QtQuick3D.Particles3D

// 破/放粒子节点（t14/t16）：呈现层消费 World 的 blockBroken/blockPlaced 语义事件。
// 本文件经 Main.qml 的 Loader 动态加载——Particles3D 模块运行期缺失时仅该 Loader 失败、
// 显式降级（Loader.Error → console.warn 告警，§2-E「保持运行而非崩溃，且不静默吞」），
// 顶层 Main.qml 仍正常加载。
// 分层铁律（§2）：触发由 World(Game 层) 发，呈现层只消费、绝不反向写栅格。
// 坐标空间：本 Node 经 Main.qml 的 Loader.onLoaded 领养进场景锚点 Node（t16 修复：
// 否则 Loader 加载出的 3D Node parent=null → 孤儿 → 不渲染），burst 的 position 即世界坐标，
// 方块 [x,x+1]×[y,y+1]×[z,z+1] 的中心 = (x+0.5, y+0.5, z+0.5)。
Node {
    id: root

    // 破/放信号 → 粒子迸发（由 Main.qml 的 Connections 转发到此）。先按方块 id 设主色，再 burst。
    function burstBreak(x, y, z, id) {
        breakParticle.color = blockColor(id)
        breakEmitter.burst(16, 80, Qt.vector3d(x + 0.5, y + 0.5, z + 0.5))
    }
    function burstPlace(x, y, z, id) {
        placeParticle.color = blockColor(id)
        placeEmitter.burst(10, 60, Qt.vector3d(x + 0.5, y + 0.5, z + 0.5))
    }

    // 运行期就绪日志（t16）：落 voxelsandbox.log，使「粒子节点加载状态在 console 可见且非 Error」
    // 可被运行期核验（Particles3D 不可用时本文件根本不会被加载，故能进到此处即代表就绪）。
    Component.onCompleted: console.info("[t16] BlockParticles ready; ParticleSystem3D running =", blockParticles.running)

    // 方块 → 粒子主色（呈现层视觉约定色；非用户可见命名，对齐 BlockRegistry id 0..8）。
    // 纯呈现决策，故随粒子节点放一起（不污染 World 数据层）。
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

    ParticleSystem3D {
        id: blockParticles
        running: true

        // 碎屑方块（破块）：小体素块向上/外散、受重力下落、短命后淡出。
        ModelParticle3D {
            id: breakParticle
            maxAmount: 80 // 池容上限：单次迸发 16 → 最多约 5 组并存即回收最老
            color: "#ffffff"
            colorVariation: Qt.vector4d(0.15, 0.15, 0.15, 0.0)
            fadeInEffect: ModelParticle3D.FadeScale
            fadeInDuration: 60
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 300
            hasTransparency: true
            delegate: debrisDelegate
        }

        // 放置扬尘（放块）：更小、更柔、轻落。
        ModelParticle3D {
            id: placeParticle
            maxAmount: 60
            color: "#e8e0c8"
            colorVariation: Qt.vector4d(0.1, 0.1, 0.1, 0.0)
            fadeInEffect: ModelParticle3D.FadeScale
            fadeInDuration: 50
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 250
            hasTransparency: true
            delegate: debrisDelegate
        }

        // 重力：碎屑强落、扬尘轻落（各只 affect 自己的粒子，互不干扰）。
        Gravity3D {
            particles: [breakParticle]
            direction: Qt.vector3d(0, -1, 0)
            magnitude: 14
        }
        Gravity3D {
            particles: [placeParticle]
            direction: Qt.vector3d(0, -1, 0)
            magnitude: 6
        }

        // 破块迸发器：emitRate=0（仅按信号 burst）；velocity 向上+外散。
        ParticleEmitter3D {
            id: breakEmitter
            particle: breakParticle
            emitRate: 0
            lifeSpan: 900
            lifeSpanVariation: 200
            particleScale: 0.14
            particleScaleVariation: 0.05
            particleRotationVelocityVariation: Qt.vector3d(200, 200, 200)
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, 3.5, 0)
                directionVariation: Qt.vector3d(3, 2, 3)
            }
        }

        // 放置扬尘迸发器：更慢、更轻，整体偏外散（无强上抛）。
        ParticleEmitter3D {
            id: placeEmitter
            particle: placeParticle
            emitRate: 0
            lifeSpan: 700
            lifeSpanVariation: 150
            particleScale: 0.08
            particleScaleVariation: 0.03
            velocity: VectorDirection3D {
                direction: Qt.vector3d(0, 1.0, 0)
                directionVariation: Qt.vector3d(2.5, 1.5, 2.5)
            }
        }
    }

    // 小体素碎屑：白色 PrincipledMaterial 使粒子 color 着色生效；NoLighting 保证亮度恒定
    // （与地形 Model 同策略，不受方向光/昼夜影响），delegate 用单位 Cube、尺寸由 emitter
    // 的 particleScale 统一控（破=0.14、放=0.08，约一成方块大小的碎屑/扬尘）。
    Component {
        id: debrisDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(1, 1, 1)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting }
        }
    }
}
