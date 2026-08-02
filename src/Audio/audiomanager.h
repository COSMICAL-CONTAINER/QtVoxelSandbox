#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <QObject>
#include <QtQml/qqml.h>

#include <memory>

#include "blockregistry.h"  // BlockRegistry::materialGroup（id→材质组；同 Core 层向下依赖）

// AudioManager（音频封装，Core/Platform 层，PLAN §2 分层）。
//
// 集成 miniaudio（单头库，PLAN §1 决策：miniaudio 单头 MIT；避 Qt Multimedia 后端矩阵不一致），
// 为破 / 放 / 挖 / 脚步 / 拾取 / 受伤 / 环境 7 类原创 SFX（§4 + t177 音效完善）提供播放，**按方块材质
// 分组**（t118：石/木/草/沙/叶 5 组 clip 池，spec「playBreak/playMining/playStep 按 group 选」）。
// 触发由 Game/Entities 层信号发出（World::blockBroken / blockPlaced、PlayerController::miningParticle
// / itemPickedUp / walkPhaseChanged、PlayerState::damaged），呈现/QML 层经 Connections 转发到本类的
// Q_INVOKABLE play* 方法 —— 音频层只消费、绝不反向写栅格 / 玩家态（PLAN §2 分层：依赖只向下）。
//
// 设计要点：
//   - pimpl（std::unique_ptr<Data>）：miniaudio.h 是 ~4MB 重头，**不**进 audiomanager.h，使本头轻量、
//     不污染其他 TU（更快编译 + 干净公共接口）。Data 内含 ma_engine + 材质分组 clip 池 + 单件 clip
//     + 解码后 PCM 缓冲。
//   - 资产从 qrc（:/sounds/*.wav）加载（PLAN §2-L：磁盘加载是终态，本任务先 qrc 跑通 + 留接口）。
//     WAV 经 ma_decoder 解码为 s16 mono PCM，零拷贝包成 ma_audio_buffer_ref（data_source）→
//     ma_sound_init_from_data_source。PCM 缓冲由 Clip 持有、与 sound 同寿命（ref 指向其内存）。
//   - 材质分组 clip 池：5 组 × {break, mining, step} = 15 clip，按 BlockRegistry::materialGroup(id)
//     选播；GroupDefault（air / 未知）→ 兜底复用 GroupStone（spec「按材质分组」+ 永不静默）。
//     place / pickup / door / hurt 单件（放置 / 拾取 / 门开关 / 受伤不分材质）；
//     ambient（风声床）单件 + looping（startAmbient/stopAmbient 控制循环开关，setAmbientLevel 调强度）。
//   - 重播语义：每次单件 play* 调 ma_sound_seek_to_pcm_frame(0) + start —— 截断重发，单实例不堆叠
//     （spec「快速连击不叠暴」；MC SFX 重叠会爆音，截断是最简稳健的限并发）。ambient 是循环长音，
//     走 start/stop（不 seek 重发）；setAmbientLevel 只改音量。
//   - 降级（§2-E）：引擎初始化失败 / 某 WAV 缺失或损坏 → 仅该路径静默 + qCWarning 告警，
//     其余 clip 不受影响、应用照常运行（绝不崩）。删音频文件运行 = 各 clip 各自降级、不崩。
//
// 分层（PLAN §2）：Core/Platform 层，依赖同层 BlockRegistry（材质组查询），**不**依赖
// Renderer/QtQuick3D/World/Physics/Game；miniaudio 是第三方 vendored 单头（src/Audio/，
// MIT-0 / public domain 双许可，与本项目 MIT 兼容）。
class AudioManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioManager)
    // 总音量（0..1）。乘进每次 play 的 ma_sound_set_volume；0 静音。NOTIFY 供未来 UI 音量条绑定。
    Q_PROPERTY(float volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager() override;

    // 破块音。按被破方块 id 的材质组选 break clip（spec「按方块材质分组」）。
    Q_INVOKABLE void playBreak(int blockId);
    // 放块音（放置不分材质；blockId 参数保留语义对齐 / 未来扩展）。
    Q_INVOKABLE void playPlace(int blockId);
    // 脚步音。按踩在脚下方块的材质组选 step clip（spec：playStep 按 group 选）。blockId = 脚下表面方块
    // id（air / 越界 → GroupDefault 兜底用 Stone step）。
    Q_INVOKABLE void playStep(int blockId);
    // 挖掘音（t118 spec「每挥一次响」）。按被挖方块 id 的材质组选 mining clip；由 miningParticle 信号
    // （生存累积挖掘每跨一阶）触发。创造瞬破不进累积态 → 不发 miningParticle → 不响（合理：瞬破只响
    // 一次 break 音，由 onBlockBroken → playBreak 覆盖）。
    Q_INVOKABLE void playMining(int blockId);
    // 拾取音（t118）：拾起掉落实体时响（itemPickedUp → 此处）。轻快双音不分材质。
    Q_INVOKABLE void playPickup();
    // 门 / 活版门开合音（t152）：右键 useBlock 翻开合时由 PlayerController::doorToggled(open) → Main.qml
    //   路由到本方法（open=true→开门 / false→关门）。单件音（木质，不分材质）；机制等价 MC 木门开关声
    //   （原创程序合成，§9）。由 useBlock 开合语义触发（与 place 的放块声区分：右键已放置的门是「使用」
    //   非放置）。spec「playDoorOpen/Close + doorToggled 信号」。
    Q_INVOKABLE void playDoorOpen();
    Q_INVOKABLE void playDoorClose();
    // 受伤音（t177 音效完善）：玩家自身受伤声 —— 低频闷击 + 略不和谐中频（呻吟感）+ 起始宽带冲击。
    //   不分材质（玩家自身，非方块）。机制等价 MC 玩家受伤声（原创程序合成，§9）。由 PlayerState::
    //   damaged(amount) → Main.qml 路由到本方法（掉落伤害等所有 takeDamage 路径）；连击 seek 重发
    //   不堆叠（同其他单件）。仅 Survival 走此路径（Creative 无伤 / Spectator noclip 不发 damaged）。
    Q_INVOKABLE void playHurt();
    // 环境音 / 风声床（t177 音效完善）：长循环风声（ma_sound looping=true），进入游戏（playing）启动、
    //   退出（回菜单 / 世界列表）停止。机制等价 MC 的环境 / 风声氛围床（原创程序合成，§9）。
    //   setAmbientLevel 据昼夜调强度（夜间更静谧）：level ∈ [0,1]，乘进 ambient 基础音量（base*m_volume*
    //   *level）。Main.qml 把 worldClock.skyLight（0=子夜/1=正午）映射成 level（如 0.5+0.5*skyLight）
    //   驱动 → 白天风声稍显、夜间更静谧。startAmbient 在 ambientPlaying=true 时早退（幂等）；stopAmbient
    //   反之。降级：engine 失败 / ambient_clip 加载失败 → 各方法静默早退（§2-E，不崩）。
    Q_INVOKABLE void startAmbient();
    Q_INVOKABLE void stopAmbient();
    // 设环境音强度（0..1）：仅改 ambient 声音量（若在播即时生效）；幂等（无变化不动）。
    Q_INVOKABLE void setAmbientLevel(float level);
    // t223 水流声（近流水 proximity ambience loop）：长循环水流声（ma_sound looping=true），玩家近流动水
    //   （state>0 流水格）一定范围时启动、远离停止。机制等价 MC 近流水 / 瀑布的环境水流声（原创程序合成，§9）。
    //   startWaterFlow / stopWaterFlow 控开关（幂等，同 ambient 模式）；setWaterFlowLevel 据玩家到最近流水格
    //   的距离调音量（近强远弱 → 0 时由 caller stopWaterFlow）。PlayerController.tickImpl 节流扫邻近流水格算
    //   level（Q_PROPERTY flowSoundLevel），Main.qml Connections 据此 start/stop + setLevel。
    //   降级：engine 失败 / water_flow_clip 加载失败 → 各方法静默早退（§2-E，不崩）。
    Q_INVOKABLE void startWaterFlow();
    Q_INVOKABLE void stopWaterFlow();
    // 设水流声强度（0..1）：仅改水流声音量（若在播即时生效）；幂等。由 PlayerController.flowSoundLevel 驱动。
    Q_INVOKABLE void setWaterFlowLevel(float level);

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
