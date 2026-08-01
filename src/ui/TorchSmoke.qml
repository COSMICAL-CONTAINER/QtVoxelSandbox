import QtQuick
import QtQuick3D
import QtQuick3D.Particles3D

// t157 火把顶部少量烟雾粒子（呈现层；PLAN §2 分层 — 只消费火把位置，不反向写栅格）。
//
// 经 Main.qml 的 smokeLoader 动态加载，**同 BlockParticles.qml 的 Particles3D 隔离模式**：
// 模块运行期缺失时仅本 Loader 失败 + 显式告警（Loader.Error → console.warn，§2-E「保持运行
// 而非崩溃，且不静默吞」），顶层 Main.qml 仍正常加载（不命中 objectCreationFailed → exit(-1)）。
//
// 设计：单 ParticleSystem3D + Repeater（model = torchModel，由 Main.qml 经 Loader.onLoaded 注入）。
//   每火把一 ParticleEmitter3D，慢速上飘（emitRate ~2/s、lifeSpan ~1.5s）+ 横向轻扰 + 淡入淡出。
//   一个共享 ModelParticle3D 池（maxAmount 240）服务全部火把 emitter → N 火把只一份粒子系统开销，
//   不会随火把数线性涨 GPU 资源（仅 emitter 数与粒子在飞数）。
//
// 坐标空间：本 Node 经 Main.qml 的 smokeLoader.onLoaded 领养进 particlesHost 锚点 Node（同 t16
//   BlockParticles 领养模式；否则 Loader 加载出的 3D Node parent=null → 孤儿 → 不渲染）。emitter
//   position 即世界坐标（火把格底面中心 + 焰顶上方 0.85 y）。
Node {
    id: root

    // 由 Main.qml 经 smokeLoader.onLoaded 注入（ListModel 引用稳定，Repeater 反应式跟随增删）。
    property var torchModel: null

    Component.onCompleted: console.info("[t157] TorchSmoke ready; torches=", root.torchModel ? root.torchModel.count : 0)

    ParticleSystem3D {
        id: smokeSystem
        running: true

        // 烟雾粒子（共享池）：小灰半透立方，淡入淡出（非 FadeScale — 烟不需要缩放出现）。
        ModelParticle3D {
            id: smokeParticle
            // 池容上限：约 60 火把 × 4 在飞 ≈ 240（少量烟雾天花板；超出按 age 回收最老）。
            maxAmount: 240
            color: "#9a9a9a"
            // 中等灰度 variation + 较大 alpha variation → 颗粒浓度自然不均（非整齐一团）。
            colorVariation: Qt.vector4d(0.12, 0.12, 0.12, 0.30)
            fadeInEffect: ModelParticle3D.FadeOpacity
            fadeInDuration: 350
            fadeOutEffect: ModelParticle3D.FadeOpacity
            fadeOutDuration: 700
            hasTransparency: true
            delegate: smokeDelegate
        }

        // 每火把一个 emitter（Repeater 据 torchModel 增删）。慢速上飘 + 横向轻扰（无 Gravity → 烟不落）。
        //   position = 火把格底面中心 (x+0.5, y, z+0.5) + 0.85 y（焰心 local y ≈ 0.65/0.80，烟从焰顶上方
        //   一点起飘）。up / 墙火把都用同一 y=0.85（墙火把焰 local y=0.80，差 0.05 仍合理；不为 5 朝向
        //   各算偏移——烟源 ±0.13 横向偏差在 0.04 小粒子下不可辨）。
        Repeater {
            model: root.torchModel
            delegate: ParticleEmitter3D {
                particle: smokeParticle
                position: Qt.vector3d(model.x + 0.5, model.y + 0.85, model.z + 0.5)
                emitRate: 1.8           // 每秒 ~2 颗（spec「少量」；MC 火把烟雾稀疏）
                lifeSpan: 1500
                lifeSpanVariation: 400
                // 尺寸由 particleScale 决定（delegate 显式 scale 被 ModelParticle3D 实例化忽略，
                //   lessons-learned「ModelParticle3D 忽略 delegate scale」红线）。0.045 < 焰心 0.18，
                //   烟颗粒明显小于火焰、轻飘不抢戏。
                particleScale: 0.045
                velocity: VectorDirection3D {
                    // 主上飘（y=0.55，慢于碎屑的 4.0）+ 横向轻扰（variation 0.14）→ 烟柱直上轻摆，
                    //   非四散炸开（与 BlockParticles 碎屑的「收束上抛」同设计语言）。
                    direction: Qt.vector3d(0, 0.55, 0)
                    directionVariation: Qt.vector3d(0.14, 0.08, 0.14)
                }
            }
        }
    }

    // 烟雾 delegate：小立方 + NoLighting（与地形 / 线框 / 粒子同已验证可见路径，
    //   lessons-learned「所有可见 Model 用 NoLighting」）。color 由 ModelParticle3D.color 着色（灰）。
    //   scale 在此仅作双保险，真实大小由 emitter.particleScale 决定（见上注释）。
    Component {
        id: smokeDelegate
        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.05, 0.05, 0.05)
            materials: PrincipledMaterial { lighting: PrincipledMaterial.NoLighting }
        }
    }
}
