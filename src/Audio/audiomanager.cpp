#include "audiomanager.h"

#include <QByteArray>
#include <QFile>
#include <QLoggingCategory>

#include <vector>

// miniaudio 单头库（vendored src/Audio/miniaudio.h，MIT-0 / public domain）。
// 仅在本私有 .cpp include（不外泄到 audiomanager.h）；MINIAUDIO_IMPLEMENTATION 定义在
// 独立的 miniaudio_impl.cpp（唯一编译实现体的 TU）。
//
// 抑制 miniaudio 头在 -Wall -Wextra 下的第三方警告（PLAN §4：第三方头警告可抑制并注明；
// 该门槛只扫项目自有代码）。实现体 TU（miniaudio_impl.cpp）整文件 -w 抑制（见 CMakeLists）。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "miniaudio.h"
#pragma GCC diagnostic pop

Q_LOGGING_CATEGORY(lcAudio, "vo.audio")  // PLAN §2-F：模块化日志（vo.audio category）

namespace {
// 从 qrc（:/sounds/xxx.wav）读 WAV 原始字节；失败返空（调用方降级）。PLAN §2-L：本任务先 qrc。
QByteArray loadWavResource(const char *qrcPath)
{
    QFile f(QString::fromLatin1(qrcPath));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcAudio) << "无法打开音频资源" << qrcPath << ":" << f.errorString()
                           << "（该音效将静默降级，§2-E）";
        return {};
    }
    return f.readAll();
}
} // namespace

// pimpl 数据：ma_engine + 3 SFX clip（解码后 PCM + 零拷贝 data_source ref + sound）。
//   PCM 缓冲（std::vector<ma_int16>）由 Clip 持有、与 ma_sound 同寿命 —— ma_audio_buffer_ref
//   零拷贝指向其内存，故 resize 须在 initSound 前完成、之后不再动（否则指针悬挂）。
struct AudioManager::Data
{
    ma_engine engine;
    bool engineOk = false;

    // 单个 SFX clip：解码后 PCM + data_source ref + sound。任一步失败 → ok=false（静默降级）。
    struct Clip
    {
        const char *qrcPath;            // qrc 资源路径
        std::vector<ma_int16> pcm;      // 解码后 PCM（s16 interleaved）；须常驻到 sound uninit
        ma_audio_buffer_ref ref;        // 零拷贝包 pcm → ma_data_source（供 ma_sound 用）
        ma_sound sound;
        bool ok = false;                // PCM 解码成功（initSound 前置）；initSound 失败会再降级
    };
    Clip breakClip{":/sounds/break.wav"};
    Clip placeClip{":/sounds/place.wav"};
    Clip stepClip{":/sounds/step.wav"};

    static constexpr ma_uint32 kChannels = 1;     // mono（合成时即 mono，省一半带宽）
    static constexpr ma_uint32 kSampleRate = 22050;

    // 解码 WAV 字节 → s16 mono PCM @ kSampleRate，包进 ref；失败 ok=false。
    void loadClip(Clip &c)
    {
        const QByteArray wav = loadWavResource(c.qrcPath);
        if (wav.isEmpty()) return;  // 资源缺失（已 warn）；ok 保持 false
        ma_decoder dec;
        const ma_decoder_config cfg =
            ma_decoder_config_init(ma_format_s16, kChannels, kSampleRate);
        if (ma_decoder_init_memory(wav.constData(), size_t(wav.size()), &cfg, &dec) != MA_SUCCESS) {
            qCWarning(lcAudio) << "decoder init 失败" << c.qrcPath << "（该音效降级）";
            return;
        }
        // 估算总帧数：未知 / 异常大 → 兜底 2s 上限（SFX 都 <0.3s，2s 足够安全）。
        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(&dec, &total);
        if (total == 0 || total > ma_uint64(kSampleRate) * 5)
            total = ma_uint64(kSampleRate) * 2;
        c.pcm.resize(size_t(total * kChannels));
        ma_uint64 read = 0;
        const ma_result rr = ma_decoder_read_pcm_frames(&dec, c.pcm.data(), total, &read);
        ma_decoder_uninit(&dec);
        if (rr != MA_SUCCESS || read == 0) {
            c.pcm.clear();
            qCWarning(lcAudio) << "decoder read 失败" << c.qrcPath << "（该音效降级）";
            return;
        }
        c.pcm.resize(size_t(read * kChannels));  // 收到实际读到的帧数（之后不再动 → ref 指针稳定）
        ma_audio_buffer_ref_init(ma_format_s16, kChannels, c.pcm.data(), read, &c.ref);
        c.ok = true;
    }

    // 引擎就绪后把 ref 包成 ma_sound（NO_SPATIALIZATION：简单 SFX，不做 3D 定位）。
    void initSound(Clip &c)
    {
        if (!engineOk || !c.ok) return;
        if (ma_sound_init_from_data_source(&engine, &c.ref,
                MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &c.sound) != MA_SUCCESS) {
            qCWarning(lcAudio) << "sound init 失败" << c.qrcPath << "（该音效降级）";
            c.ok = false;  // 标记不可播（replay 早退）
        }
    }

    // 重播：seek 回 0 + 设音量 + start。已在播则截断重发 → 单实例、不堆叠（限并发）。
    void replay(Clip &c, float vol)
    {
        if (!engineOk || !c.ok) return;
        ma_sound_seek_to_pcm_frame(&c.sound, 0);
        ma_sound_set_volume(&c.sound, vol);
        ma_sound_start(&c.sound);
    }
};

AudioManager::AudioManager(QObject *parent)
    : QObject(parent), d(std::make_unique<Data>())
{
    // miniaudio 引擎初始化：Windows 自动初始化 COM + 选 DirectSound/WASAPI 后端（spec 验收项）。
    // 失败 → engineOk=false，所有 play* 早退为静默（§2-E 保持运行而非崩溃）。
    if (ma_engine_init(nullptr, &d->engine) != MA_SUCCESS) {
        qCWarning(lcAudio) << "miniaudio engine init 失败 —— 音频已整体降级关闭（§2-E）";
        d->engineOk = false;
        return;
    }
    d->engineOk = true;
    // 3 SFX 解码 + sound 初始化：任一 clip 失败仅自身降级，不影响其余（局部降级）。
    d->loadClip(d->breakClip);
    d->loadClip(d->placeClip);
    d->loadClip(d->stepClip);
    d->initSound(d->breakClip);
    d->initSound(d->placeClip);
    d->initSound(d->stepClip);
    qCInfo(lcAudio) << "AudioManager init: engine =" << d->engineOk
                    << " break/place/step =" << d->breakClip.ok << d->placeClip.ok << d->stepClip.ok;
}

AudioManager::~AudioManager()
{
    if (!d->engineOk) return;
    // 先 uninit sounds（释放对 ref/data_source 的引用），再 uninit engine（顺序：依赖反向拆解）。
    if (d->breakClip.ok) ma_sound_uninit(&d->breakClip.sound);
    if (d->placeClip.ok) ma_sound_uninit(&d->placeClip.sound);
    if (d->stepClip.ok) ma_sound_uninit(&d->stepClip.sound);
    ma_engine_uninit(&d->engine);
}

void AudioManager::playBreak(int blockId) { d->replay(d->breakClip, m_volume); }
void AudioManager::playPlace(int blockId) { d->replay(d->placeClip, m_volume); }
// 脚步音量略低（避免连击疲劳；spec「音量合理」）。
void AudioManager::playStep() { d->replay(d->stepClip, m_volume * 0.7f); }

void AudioManager::setVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (qFuzzyCompare(v, m_volume)) return;
    m_volume = v;
    emit volumeChanged();
}
