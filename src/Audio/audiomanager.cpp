#include "audiomanager.h"

#include <QByteArray>
#include <QFile>
#include <QLoggingCategory>

#include <array>
#include <deque>
#include <string>
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
// 材质组数（不含 GroupDefault 哨兵；GroupDefault → 兜底用 GroupStone，见 groupIndex()）。
constexpr int kNumGroups = int(BlockRegistry::GroupDefault);  // 5（Stone..Leaves）

// 把 BlockRegistry::materialGroup 折成 [0, kNumGroups) 的 clip 池下标。
// GroupDefault（air / 未知 / 越界）→ 0（Stone 组兜底，spec「缺组永不静默」用最常见材质音色）。
int groupIndex(int blockId)
{
    const int g = int(BlockRegistry::materialGroup(quint8(blockId)));
    if (g >= kNumGroups) return 0; // GroupDefault / 越界 → Stone 兜底
    return g;
}

// 材质组名（仅用于日志可读 + qrc 路径拼接）。
const char *groupName(int idx)
{
    switch (idx) {
    case 0: return "stone";
    case 1: return "wood";
    case 2: return "grass";
    case 3: return "sand";
    case 4: return "leaves";
    default: return "stone"; // 兜底（理论不可达；防 -Wreturn-type）
    }
}

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

// pimpl 数据：ma_engine + 材质分组 clip 池（5 组 × {break, mining, step}）+ 单件 clip（place/pickup）。
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

    // 材质分组 clip 池：[groupIndex][kinds]，kinds = {break, mining, step}（顺序见 kKindNames）。
    // std::array<std::array<Clip, 3>, 5>：行=组、列=音类。qrcPath 在构造时按 group/kind 拼出。
    enum Kind { Break = 0, Mining = 1, Step = 2, NumKinds = 3 };
    // 路径拼接用缓冲：每 Clip 持自己的 const char*（指向 pathStore 内 string 的 .c_str()）→ 须常驻。
    // 用 std::deque<std::string>（非 vector）：deque 的 push_back **不失效**已有元素引用 / 指针，
    // 故循环内 emplace_back 拼出 15 条路径时，先前已赋进 Clip::qrcPath 的 .c_str() 不悬挂
    // （vector 扩容会迁内存、使所有先前 .c_str() 失效 → qrcPath 全悬空，是潜在 hard-bug）。
    std::deque<std::string> pathStore;
    std::array<std::array<Clip, NumKinds>, kNumGroups> groupClips{};

    // 单件 clip（放置 / 拾取不分材质）。
    Clip placeClip{":/sounds/place.wav"};
    Clip pickupClip{":/sounds/pickup.wav"};
    // t152 门 / 活版门开合单件（木质；右键 useBlock 翻开合时播，与 place 放块声区分）。
    Clip doorOpenClip{":/sounds/door_open.wav"};
    Clip doorCloseClip{":/sounds/door_close.wav"};

    static constexpr ma_uint32 kChannels = 1;     // mono（合成时即 mono，省一半带宽）
    static constexpr ma_uint32 kSampleRate = 22050;

    // 在 pathStore 内构造一条 qrc 路径（:/sounds/{kind}_{group}.wav），返回其 .c_str()（长寿命）。
    const char *makePath(const char *kind, int groupIdx)
    {
        pathStore.emplace_back(std::string(":/sounds/") + kind + "_" + groupName(groupIdx) + ".wav");
        return pathStore.back().c_str();
    }

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

    // 按 groupIndex 取某音类的 clip（Break / Mining / Step）。
    Clip &groupClip(int groupIdx, Kind kind) { return groupClips[size_t(groupIdx)][size_t(kind)]; }
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

    // 材质分组 clip 池：每组 × {break, mining, step} 的 qrc 路径在 makePath 内拼接长寿命化。
    // groupClips 默认初始化时 qrcPath=nullptr → 此处补设（Data 的 pathStore 持字符串长寿命）。
    static constexpr const char *kKindNames[Data::NumKinds] = {"break", "mining", "step"};
    for (int g = 0; g < kNumGroups; ++g) {
        for (int k = 0; k < int(Data::NumKinds); ++k) {
            d->groupClips[size_t(g)][size_t(k)].qrcPath = d->makePath(kKindNames[k], g);
        }
    }

    // 解码 + sound 初始化：任一 clip 失败仅自身降级，不影响其余（局部降级）。
    for (int g = 0; g < kNumGroups; ++g) {
        for (int k = 0; k < int(Data::NumKinds); ++k) {
            d->loadClip(d->groupClips[size_t(g)][size_t(k)]);
            d->initSound(d->groupClips[size_t(g)][size_t(k)]);
        }
    }
    d->loadClip(d->placeClip);
    d->loadClip(d->pickupClip);
    d->loadClip(d->doorOpenClip);
    d->loadClip(d->doorCloseClip);
    d->initSound(d->placeClip);
    d->initSound(d->pickupClip);
    d->initSound(d->doorOpenClip);
    d->initSound(d->doorCloseClip);

    qCInfo(lcAudio).nospace().noquote()
        << "AudioManager init: engine=" << d->engineOk
        << " groups(stone/wood/grass/sand/leaves)×(break/mining/step)="
        << d->groupClips[0][0].ok << "/" << d->groupClips[0][1].ok << "/" << d->groupClips[0][2].ok << " | "
        << d->groupClips[1][0].ok << "/" << d->groupClips[1][1].ok << "/" << d->groupClips[1][2].ok << " | "
        << d->groupClips[2][0].ok << "/" << d->groupClips[2][1].ok << "/" << d->groupClips[2][2].ok << " | "
        << d->groupClips[3][0].ok << "/" << d->groupClips[3][1].ok << "/" << d->groupClips[3][2].ok << " | "
        << d->groupClips[4][0].ok << "/" << d->groupClips[4][1].ok << "/" << d->groupClips[4][2].ok
        << " place=" << d->placeClip.ok << " pickup=" << d->pickupClip.ok
        << " door_open=" << d->doorOpenClip.ok << " door_close=" << d->doorCloseClip.ok;
}

AudioManager::~AudioManager()
{
    if (!d->engineOk) return;
    // 先 uninit sounds（释放对 ref/data_source 的引用），再 uninit engine（顺序：依赖反向拆解）。
    for (int g = 0; g < kNumGroups; ++g) {
        for (int k = 0; k < int(Data::NumKinds); ++k) {
            if (d->groupClips[size_t(g)][size_t(k)].ok)
                ma_sound_uninit(&d->groupClips[size_t(g)][size_t(k)].sound);
        }
    }
    if (d->placeClip.ok) ma_sound_uninit(&d->placeClip.sound);
    if (d->pickupClip.ok) ma_sound_uninit(&d->pickupClip.sound);
    if (d->doorOpenClip.ok) ma_sound_uninit(&d->doorOpenClip.sound);
    if (d->doorCloseClip.ok) ma_sound_uninit(&d->doorCloseClip.sound);
    ma_engine_uninit(&d->engine);
}

void AudioManager::playBreak(int blockId)
{
    d->replay(d->groupClip(groupIndex(blockId), Data::Break), m_volume);
}

void AudioManager::playPlace(int blockId)
{
    Q_UNUSED(blockId);  // 放置不分材质（单件 place clip）
    d->replay(d->placeClip, m_volume);
}

void AudioManager::playStep(int blockId)
{
    // 脚步音量略低（避免连击疲劳；spec「音量合理」）。
    d->replay(d->groupClip(groupIndex(blockId), Data::Step), m_volume * 0.7f);
}

void AudioManager::playMining(int blockId)
{
    // 挖掘音量略低于 break（每阶反馈，spec「每挥一次响」不应过响）。
    d->replay(d->groupClip(groupIndex(blockId), Data::Mining), m_volume * 0.8f);
}

void AudioManager::playPickup()
{
    d->replay(d->pickupClip, m_volume);
}

void AudioManager::playDoorOpen()
{
    d->replay(d->doorOpenClip, m_volume);
}

void AudioManager::playDoorClose()
{
    d->replay(d->doorCloseClip, m_volume);
}

void AudioManager::setVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (qFuzzyCompare(v, m_volume)) return;
    m_volume = v;
    emit volumeChanged();
}
