#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QtQml/qqml.h>

#include <memory>

// AudioManager（音频封装，Core/Platform 层，PLAN §2 分层）。
//
// 集成 miniaudio（单头库，PLAN §1 决策：miniaudio 单头 MIT；避 Qt Multimedia 后端矩阵不一致），
// 为破/放/脚步 3 个原创 SFX（§4）提供播放。触发由 Game/Entities 层信号发出（World::blockBroken /
// blockPlaced、PlayerController::walkPhaseChanged），呈现/QML 层经 Connections 转发到本类的
// Q_INVOKABLE play* 方法 —— 音频层只消费、绝不反向写栅格 / 玩家态（PLAN §2 分层：依赖只向下）。
//
// 设计要点：
//   - pimpl（std::unique_ptr<Data>）：miniaudio.h 是 ~4MB 重头，**不**进 audiomanager.h，使本头轻量、
//     不污染其他 TU（更快编译 + 干净公共接口）。Data 内含 ma_engine + 3 ma_sound + 解码后 PCM 缓冲。
//   - 资产从 qrc（:/sounds/*.wav）加载（PLAN §2-L：磁盘加载是终态，本任务先 qrc 跑通 + 留接口）。
//     WAV 经 ma_decoder 解码为 s16 mono PCM，零拷贝包成 ma_audio_buffer_ref（data_source）→
//     ma_sound_init_from_data_source。PCM 缓冲由 Clip 持有、与 sound 同寿命（ref 指向其内存）。
//   - 重播语义：每次 play* 调 ma_sound_seek_to_pcm_frame(0) + start —— 截断重发，单实例不堆叠
//     （spec「快速连击不叠暴」；MC SFX 重叠会爆音，截断是最简稳健的限并发）。
//   - 降级（§2-E）：引擎初始化失败 / 某 WAV 缺失或损坏 → 仅该路径静默 + qCWarning 告警，
//     其余 clip 不受影响、应用照常运行（绝不崩）。删音频文件运行 = 三 clip 各自降级、不崩。
//
// 分层（PLAN §2）：Core/Platform 层，**不**依赖 Renderer/QtQuick3D/World/Physics；miniaudio 是
// 第三方 vendored 单头（src/Audio/，MIT-0 / public domain 双许可，与本项目 MIT 兼容）。
class AudioManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioManager)
    // 总音量（0..1）。乘进每次 play 的 ma_sound_set_volume；0 静音。NOTIFY 供未来 UI 音量条绑定。
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager() override;

    // 破块音。blockId 预留给「按方块材质分流音色」（spec：按 id 取声 / 或统一）；当前统一一份。
    Q_INVOKABLE void playBreak(int blockId);
    // 放块音（同上，blockId 预留分流）。
    Q_INVOKABLE void playPlace(int blockId);
    // 脚步音。调用方在 walkPhase 半步（Δphase≥π）时触发（Main.qml Connections 累加相位差判定）。
    Q_INVOKABLE void playStep();

    float volume() const { return m_volume; }
    void setVolume(float v);

signals:
    void volumeChanged();

private:
    // miniaudio 类型（ma_engine/ma_sound/...）藏在 .cpp，避免重头外泄到本 .h。
    struct Data;
    std::unique_ptr<Data> d;

    float m_volume = 0.8f;  // 默认音量（避免满音量爆音；spec「音量合理、不爆音」）
};

#endif // AUDIOMANAGER_H
