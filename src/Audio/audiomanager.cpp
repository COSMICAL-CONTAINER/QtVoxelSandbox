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
    // t177 受伤单件（玩家自身受伤声；不分材质）。PlayerState::damaged → playHurt 触发。
    Clip hurtClip{":/sounds/hurt.wav"};
    // t248 mob 受击单件（生物受击专属声；与玩家 hurt 区分）。PlayerController::mobAttacked → playMobHurt 触发。
    Clip mobHurtClip{":/sounds/mob_hurt.wav"};
    // t284 爆炸单件（Stalker/苦力怕自爆）。EntityManager::explosion → playExplosion 触发。短爆裂音（~0.5s），
    //   默认 2s maxFrames 远大于其长度、安全。
    Clip explosionClip{":/sounds/explosion.wav"};
    // t250 mob 环境 idle 叫声 clip 池，按 mobType 索引（0=通用测试生物 / 1=猪 / 2=牛 / 3=羊）。EntityManager
    //   tick 内 ambientTimer 周期倒计时 → emit mobAmbient(mobType) → Main.qml 路由到 playMobAmbient 据
    //   mobType 选播。机制等价 MC 1.0 被动生物偶发 idle call（原创程序合成，§9；零 MC 资产）。mobIdleClips
    //   长度须 ≥ 最高 mobType+1（当前 4）；playMobAmbient 对越界 mobType 兜底用下标 0（generic）。
    Clip mobIdleClips[4] = { {":/sounds/mob_idle.wav"}, {":/sounds/mob_idle_pig.wav"},
                             {":/sounds/mob_idle_cow.wav"}, {":/sounds/mob_idle_sheep.wav"} };
    // t177 环境音 / 风声床单件（长循环风声；构造后置 looping=true，start/stop 控制开关，
    //   setAmbientLevel 调强度）。进入 playing 启动、退菜单停止。
    Clip ambientClip{":/sounds/ambient_wind.wav"};
    // 环境音运行态：是否在播（幂等 start/stop）+ 强度（0..1，由昼夜 skyLight 映射，夜间更静谧）。
    bool ambientPlaying = false;
    float ambientLevel = 1.0f;
    // 环境音基础音量系数（风声偏低，背景氛围不抢前景；乘 m_volume 与 ambientLevel 得最终音量）。
    static constexpr float kAmbientBaseVol = 0.22f;
    // t223 水流声单件（长循环水流声；looping=true，startWaterFlow/stopWaterFlow 控开关，
    //   setWaterFlowLevel 据 PlayerController.flowSoundLevel 调强度）。近流动水启动、远离停止。
    //   t269：water_flow.wav 重合成潺潺流水声（旧版像海浪 → 改潺潺流水；build_sounds.py 三层混合 + 密集气泡）。
    Clip waterFlowClip{":/sounds/water_flow.wav"};
    bool waterFlowPlaying = false;
    float waterFlowLevel = 1.0f;
    // 水流声基础音量系数（背景氛围级；乘 m_volume 与 waterFlowLevel 得最终音量）。
    static constexpr float kWaterFlowBaseVol = 0.30f;
    // t269 水中走路声单件（玩家脚位在水中迈步时播；不分材质，水下听感统一闷浊）。短 SFX（~0.16s），
    //   默认 2s maxFrames 远大于其长度、安全。
    Clip waterStepClip{":/sounds/water_step.wav"};

    static constexpr ma_uint32 kChannels = 1;     // mono（合成时即 mono，省一半带宽）
    static constexpr ma_uint32 kSampleRate = 22050;

    // 在 pathStore 内构造一条 qrc 路径（:/sounds/{kind}_{group}.wav），返回其 .c_str()（长寿命）。
    const char *makePath(const char *kind, int groupIdx)
    {
        pathStore.emplace_back(std::string(":/sounds/") + kind + "_" + groupName(groupIdx) + ".wav");
        return pathStore.back().c_str();
    }

    // 解码 WAV 字节 → s16 mono PCM @ kSampleRate，包进 ref；失败 ok=false。
    // maxFrames：total ≤ 此值则信任并按 total 读；未知（==0）/ 超出则截到此值（防异常大值占满内存）。
    //   短 SFX（<0.3s）默认 kSampleRate*2（2s）远大于其长度、安全；长循环环境音（≥8s）须传更大值
    //   （如 16s）以保完整解码 + 首末淡化无缝循环（修复：旧硬编码 5s 上限把 8s 环境音截到 2s →
    //   循环点处满幅波形回绕到淡化起点 ≈0 → 每 2s 一次咔哒爆音，正是淡化设计要消除的）。
    void loadClip(Clip &c, ma_uint64 maxFrames = ma_uint64(kSampleRate) * 2)
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
        // 估算总帧数：未知 / 超出 maxFrames → 截到 maxFrames（防异常大值占满内存）。
        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(&dec, &total);
        if (total == 0 || total > maxFrames)
            total = maxFrames;
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

    // 环境音最终音量 = master × base × level（level 由昼夜映射，夜间 < 1）。
    float ambientVol(float masterVolume) const
    {
        return masterVolume * kAmbientBaseVol * ambientLevel;
    }
    // t223 水流声最终音量 = master × base × level（level 由玩家到最近流水格的距离映射，近=1/远→0）。
    float waterFlowVol(float masterVolume) const
    {
        return masterVolume * kWaterFlowBaseVol * waterFlowLevel;
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
    d->loadClip(d->hurtClip);
    d->loadClip(d->mobHurtClip);
    d->loadClip(d->explosionClip);
    // t250 mob idle 叫声（猪哼 / 牛哞 / 羊咩 / 通用）—— 短 SFX（≤0.62s），默认 2s maxFrames 远大于其长度。
    for (int i = 0; i < 4; ++i)
        d->loadClip(d->mobIdleClips[i]);
    // 环境音是 8.0s 长循环（build_sounds.py 首末 50ms 三角窗淡化保无缝），maxFrames 放宽到 16s
    // 保完整解码 —— 默认 2s 上限会把 8s 截到 2s，使循环点落在满幅中波、回绕到淡化起点 ≈0 →
    // 每 2s 一次咔哒爆音（淡化设计被废弃）。
    d->loadClip(d->ambientClip, ma_uint64(Data::kSampleRate) * 16);
    // t223 水流声同为 8.0s 长循环（首末淡化无缝），maxFrames 放宽到 16s 保完整解码（同 ambient_wind 教训）。
    d->loadClip(d->waterFlowClip, ma_uint64(Data::kSampleRate) * 16);
    // t269 水中走路声短 SFX（~0.16s），默认 2s maxFrames 远大于其长度。
    d->loadClip(d->waterStepClip);
    d->initSound(d->placeClip);
    d->initSound(d->pickupClip);
    d->initSound(d->doorOpenClip);
    d->initSound(d->doorCloseClip);
    d->initSound(d->hurtClip);
    d->initSound(d->mobHurtClip);
    d->initSound(d->explosionClip);
    for (int i = 0; i < 4; ++i)
        d->initSound(d->mobIdleClips[i]);
    d->initSound(d->ambientClip);
    d->initSound(d->waterFlowClip);
    d->initSound(d->waterStepClip);
    // t177 环境音：sound init 成功后置循环 + 初始音量（startAmbient 才 start；不在此自动开）。
    if (d->engineOk && d->ambientClip.ok) {
        ma_sound_set_looping(&d->ambientClip.sound, MA_TRUE);
        ma_sound_set_volume(&d->ambientClip.sound, d->ambientVol(m_volume));
    }
    // t223 水流声：sound init 成功后置循环 + 初始音量（startWaterFlow 才 start；由 proximity 扫描驱动）。
    if (d->engineOk && d->waterFlowClip.ok) {
        ma_sound_set_looping(&d->waterFlowClip.sound, MA_TRUE);
        ma_sound_set_volume(&d->waterFlowClip.sound, d->waterFlowVol(m_volume));
    }

    qCInfo(lcAudio).nospace().noquote()
        << "AudioManager init: engine=" << d->engineOk
        << " groups(stone/wood/grass/sand/leaves)×(break/mining/step)="
        << d->groupClips[0][0].ok << "/" << d->groupClips[0][1].ok << "/" << d->groupClips[0][2].ok << " | "
        << d->groupClips[1][0].ok << "/" << d->groupClips[1][1].ok << "/" << d->groupClips[1][2].ok << " | "
        << d->groupClips[2][0].ok << "/" << d->groupClips[2][1].ok << "/" << d->groupClips[2][2].ok << " | "
        << d->groupClips[3][0].ok << "/" << d->groupClips[3][1].ok << "/" << d->groupClips[3][2].ok << " | "
        << d->groupClips[4][0].ok << "/" << d->groupClips[4][1].ok << "/" << d->groupClips[4][2].ok
        << " place=" << d->placeClip.ok << " pickup=" << d->pickupClip.ok
        << " door_open=" << d->doorOpenClip.ok << " door_close=" << d->doorCloseClip.ok
        << " hurt=" << d->hurtClip.ok << " mob_hurt=" << d->mobHurtClip.ok
        << " explosion=" << d->explosionClip.ok
        << " mob_idle(gen/pig/cow/sheep)=" << d->mobIdleClips[0].ok << "/" << d->mobIdleClips[1].ok
        << "/" << d->mobIdleClips[2].ok << "/" << d->mobIdleClips[3].ok
        << " ambient_wind=" << d->ambientClip.ok
        << " water_flow=" << d->waterFlowClip.ok
        << " water_step=" << d->waterStepClip.ok;
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
    if (d->hurtClip.ok) ma_sound_uninit(&d->hurtClip.sound);
    if (d->mobHurtClip.ok) ma_sound_uninit(&d->mobHurtClip.sound);
    if (d->explosionClip.ok) ma_sound_uninit(&d->explosionClip.sound);
    for (int i = 0; i < 4; ++i)
        if (d->mobIdleClips[i].ok) ma_sound_uninit(&d->mobIdleClips[i].sound);
    if (d->ambientClip.ok) ma_sound_uninit(&d->ambientClip.sound);
    if (d->waterFlowClip.ok) ma_sound_uninit(&d->waterFlowClip.sound);
    if (d->waterStepClip.ok) ma_sound_uninit(&d->waterStepClip.sound);
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

// t269 水中走路声：玩家脚位在水中迈步时播（不分材质；水下听感统一闷浊，单件 clip）。音量低于普通
//   playStep（水下传播衰减 + 不抢水流声前景）；seek 重发不堆叠；engine/clip 失败静默降级（§2-E）。
void AudioManager::playWaterStep()
{
    d->replay(d->waterStepClip, m_volume * 0.55f);
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

void AudioManager::playHurt()
{
    // 受伤音略低于 break（受伤反馈不宜过响；与 door 同量级）。
    d->replay(d->hurtClip, m_volume * 0.9f);
}

void AudioManager::playMobHurt()
{
    // mob 受击音与 hurt 同量级（生物受击反馈；机制等价 MC 生物受击声，§9 原创合成，与玩家 hurt 区分）。
    d->replay(d->mobHurtClip, m_volume * 0.9f);
}

// t284 爆炸音（Stalker/苦力怕自爆）：略响于 hurt/mob_hurt（爆炸是高冲击事件，前景反馈）。机制等价 MC 爆炸声
//   （§9 原创程序合成，零 MC 资产）。由 EntityManager::explosion → Main.qml Connections 路由触发。单件 seek 重发
//   不堆叠（同其他单件）；engine/clip 失败静默降级（§2-E，不崩）。
void AudioManager::playExplosion()
{
    d->replay(d->explosionClip, m_volume);
}

// t250 mob 环境 idle 叫声（牛叫/羊叫/猪叫）：按 mobType 选 mob_idle clip。mobType 越界（防御 caller 误传 /
//   将来新增子类未补 clip）→ 兜底用下标 0（generic）。音量略低于 break（环境偶发音，不抢前景）。
void AudioManager::playMobAmbient(int mobType)
{
    int idx = mobType;
    if (idx < 0 || idx >= 4) idx = 0; // 越界 → generic 兜底（永不静默：spec 缺组用最常见音色）
    d->replay(d->mobIdleClips[size_t(idx)], m_volume * 0.85f);
}

// t250 mob 走路声：复用 step 材质分组 clip 池（按脚下方块 id 的材质组选），音量低于玩家 playStep
//   （mob 是环境音、非玩家前景；spec「音量合理」）。mobType 当前保留语义对齐 / 未来扩展（步声按材质）。
void AudioManager::playMobStep(int mobType, int blockId)
{
    Q_UNUSED(mobType);
    d->replay(d->groupClip(groupIndex(blockId), Data::Step), m_volume * 0.45f);
}

void AudioManager::startAmbient()
{
    // 幂等：已在播早退（避免重复 start 把同一 looping 声叠成多路）。降级（engine/clip 失败）静默早退。
    if (!d->engineOk || !d->ambientClip.ok || d->ambientPlaying) return;
    ma_sound_set_volume(&d->ambientClip.sound, d->ambientVol(m_volume));
    if (ma_sound_start(&d->ambientClip.sound) != MA_SUCCESS) {
        qCWarning(lcAudio) << "ambient sound start 失败（环境音降级）";
        return;
    }
    d->ambientPlaying = true;
}

void AudioManager::stopAmbient()
{
    // 幂等：未在播早退。stop 不 seek（looping 声下次 start 从当前位置续播 → 无伤；seek 回 0 更稳）。
    if (!d->engineOk || !d->ambientClip.ok || !d->ambientPlaying) return;
    ma_sound_stop(&d->ambientClip.sound);
    ma_sound_seek_to_pcm_frame(&d->ambientClip.sound, 0);  // 下次 start 从头（避免中途续播突兀）
    d->ambientPlaying = false;
}

void AudioManager::setAmbientLevel(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    if (qFuzzyCompare(level, d->ambientLevel)) return;
    d->ambientLevel = level;
    // 在播则即时改音量；未播（如菜单态 dayPhase 推进）仅记 level，下次 startAmbient 用新值。
    if (d->engineOk && d->ambientClip.ok && d->ambientPlaying)
        ma_sound_set_volume(&d->ambientClip.sound, d->ambientVol(m_volume));
}

// t223 水流声：与 ambient_wind 同模式（looping 长音 + start/stop/setLevel），由 PlayerController 近流水
//   proximity 扫描驱动（flowSoundLevel）。
void AudioManager::startWaterFlow()
{
    // 幂等：已在播早退。降级（engine / clip 失败）静默早退（§2-E）。
    if (!d->engineOk || !d->waterFlowClip.ok || d->waterFlowPlaying) return;
    ma_sound_set_volume(&d->waterFlowClip.sound, d->waterFlowVol(m_volume));
    if (ma_sound_start(&d->waterFlowClip.sound) != MA_SUCCESS) {
        qCWarning(lcAudio) << "water flow sound start 失败（水流声降级）";
        return;
    }
    d->waterFlowPlaying = true;
}

void AudioManager::stopWaterFlow()
{
    // 幂等：未在播早退。stop + seek 回 0（下次 start 从头，避免中途续播突兀）。
    if (!d->engineOk || !d->waterFlowClip.ok || !d->waterFlowPlaying) return;
    ma_sound_stop(&d->waterFlowClip.sound);
    ma_sound_seek_to_pcm_frame(&d->waterFlowClip.sound, 0);
    d->waterFlowPlaying = false;
}

void AudioManager::setWaterFlowLevel(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    if (qFuzzyCompare(level, d->waterFlowLevel)) return;
    d->waterFlowLevel = level;
    // 在播则即时改音量；未播仅记 level，下次 startWaterFlow 用新值。
    if (d->engineOk && d->waterFlowClip.ok && d->waterFlowPlaying)
        ma_sound_set_volume(&d->waterFlowClip.sound, d->waterFlowVol(m_volume));
}

void AudioManager::setVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (qFuzzyCompare(v, m_volume)) return;
    m_volume = v;
    emit volumeChanged();
    // t177：环境音是持续 looping 声，master 音量变后须即时同步其音量（其他单件每次 replay 重设无需）。
    if (d->engineOk && d->ambientClip.ok && d->ambientPlaying)
        ma_sound_set_volume(&d->ambientClip.sound, d->ambientVol(m_volume));
    // t223：水流声同为持续 looping 声，master 音量变后须即时同步。
    if (d->engineOk && d->waterFlowClip.ok && d->waterFlowPlaying)
        ma_sound_set_volume(&d->waterFlowClip.sound, d->waterFlowVol(m_volume));
}
