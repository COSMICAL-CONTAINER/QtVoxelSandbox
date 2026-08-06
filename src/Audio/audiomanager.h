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
    // mob 受击音（t248 专属受击音 + t295 敌对专属音）：玩家攻击生物时该生物的受击声 —— 与玩家 hurt 区分。
    //   mobType = 被攻击 mob 的子类 id（0=MobTest 通用 / 1=Pig / 2=Cow / 3=Sheep 被动；4=Shambler / 5=Bones /
    //   6=Stalker / 7=Spider 敌对）。路由：
    //     - 被动（0-3）/ 通用：mob_hurt.wav（更短促、带「 creature yelp 」下扫中频 + 较轻的软冲击；
    //       机制等价 MC 生物受击声，原创程序合成 §9）。
    //     - 敌对（4-7）：复用其 ambient idle clip 作受击专属音（spec t295「敌对各:骨头敲击/蜘蛛嘶(近)/
    //       僵尸哀嚎/苦力怕爆炸声」——Shambler 哀嚎 / Bones 骨头敲击 / Spider 蜘蛛嘶嗡 / Stalker 嘶嘶；
    //       机制等价 MC 敌对受击声与其 idle 叫声同族）。Stalker 爆炸专属音（苦力怕爆炸声）走 playExplosion
    //       的 detonation 路径（detonateStalker → explosion 信号），其受击（被剑砍未引爆）用嘶嘶 idle。
    //   由 PlayerController::mobAttacked(mobType,crit) → Main.qml 路由到本方法（替代旧复用 playHurt 的路径）。
    //   seek 重发不堆叠（同 hurt 单件模式）；降级（engine / clip 失败）静默早退（§2-E，不崩）。
    Q_INVOKABLE void playMobHurt(int mobType);
    // t284 爆炸音（Stalker/苦力怕自爆）：低频闷击 + 宽带爆裂瞬态 + 较长尾音（机制等价 MC 爆炸声，原创程序
    //   合成 §9；零 MC 资产）。由 EntityManager::explosion → Main.qml 路由到本方法触发（爆炸的单一音/视入口）。
    //   单件 clip；seek 重发不堆叠（同其他单件）；engine/clip 失败静默降级（§2-E，不崩）。
    Q_INVOKABLE void playExplosion();
    // t315 工具破损音（工具耐久归零瞬间）：干脆的「啪嗒」断裂声（高频 crack 瞬态 + 中频 snap body + 末尾碎屑
    //   沙沙）。机制等价 MC 工具耐久耗尽破损声（原创程序合成，§9；零 MC 资产）。由 Hotbar::toolBroken(itemId)
    //   → Main.qml 路由到本方法（damageSelectedItem 归零分支 emit，itemId 保留语义对齐 / 未来按工具材质分流）。
    //   单件 clip；seek 重发不堆叠（同其他单件）；engine/clip 失败静默降级（§2-E，不崩）。
    Q_INVOKABLE void playToolBreak();
    // t328 UI 反馈 click（热键 / 滚轮切槽 tick）：轻 tick（~0.05s 高通噪声爆 + 微小高谐）。机制等价 MC 物品栏
    //   切换 tick 反馈（原创程序合成 §9；零 MC 资产）。由 Hotbar::selectedSlotChanged → Main.qml 路由到本方法
    //   触发（与视觉高亮配对的音频反馈）。单件 clip；seek 重发不堆叠（同其他单件）；engine/clip 失败静默
    //   降级（§2-E，不崩）。
    Q_INVOKABLE void playUIClick();
    // t250 mob 环境音（被动牛叫/羊叫/猪叫 + 敌对 idle 叫声 + 走路声）；t294 扩敌对 idle（补全「怪物叫声 idle」）：
    //   - playMobAmbient(mobType)：生物周期 idle 叫声 —— 按 mobType 选 mob_idle clip（0=通用 / 1=猪哼 /
    //     2=牛哞 / 3=羊咩 / 4=Shambler 哀嚎 / 5=Bones 骨咔哒 / 6=Stalker 嘶嘶 / 7=Spider 嘶嗡）。EntityManager
    //     tick 内 ambientTimer 周期倒计时（随机 8-16s）+ 玩家听者范围内 → emit mobAmbient(mobType) → Main.qml
    //     路由到本方法。机制等价 MC 1.0 生物偶发 idle call（原创程序合成，§9；零 MC 资产；PLAN §9 区隔改名
    //     shambler/bones/stalker/spider，非 MC 专名）。t294：mobType 4-7 旧兜底通用 → 现各敌对子类独立音色
    //     （ambient idle，与 t295 受击 / 近距 / 爆炸专属触发音不同）。seek 重发不堆叠（同单件模式）；
    //     engine/clip 失败静默降级（§2-E）。
    Q_INVOKABLE void playMobAmbient(int mobType);
    //   - playMobStep(mobType, blockId)：生物走路声 —— 复用 step 材质分组 clip 池（按脚下方块 id 的材质组选），
    //     mobType 当前保留语义对齐 / 未来扩展（步声按材质而非 mob 子类）。mob 是环境音（非玩家前景）→
    //     音量低于玩家 playStep。EntityManager tick 内 walkPhase 每累积半步（π=一次脚落）+ 听者范围内 →
    //     emit mobStep(mobType, 脚下方块 id) → Main.qml 路由到本方法。机制等价 MC 生物走路脚步声（§9 原创）。
    Q_INVOKABLE void playMobStep(int mobType, int blockId);
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
    //   t269 重做：water_flow.wav 重合成潺潺流水声（旧版像海浪 → 改潺潺流水 / 溪流；详见 build_sounds.py）。
    //   startWaterFlow / stopWaterFlow 控开关（幂等，同 ambient 模式）；setWaterFlowLevel 据玩家到最近流水格
    //   的距离调音量（近强远弱 → 0 时由 caller stopWaterFlow）。PlayerController.tickImpl 节流扫邻近流水格算
    //   level（Q_PROPERTY flowSoundLevel），Main.qml Connections 据此 start/stop + setLevel。
    //   降级：engine 失败 / water_flow_clip 加载失败 → 各方法静默早退（§2-E，不崩）。
    Q_INVOKABLE void startWaterFlow();
    Q_INVOKABLE void stopWaterFlow();
    // 设水流声强度（0..1）：仅改水流声音量（若在播即时生效）；幂等。由 PlayerController.flowSoundLevel 驱动。
    Q_INVOKABLE void setWaterFlowLevel(float level);
    // t343 岩浆声（机制同水流声，由 PlayerController.lavaSoundLevel proximity 驱动）：近岩浆启动、远离停止。
    //   长循环低频 rumble + 气泡（lava.wav，build_sounds.py 程序合成，§9 原创）；幂等；降级静默早退（§2-E）。
    Q_INVOKABLE void startLavaFlow();
    Q_INVOKABLE void stopLavaFlow();
    // 设岩浆声强度（0..1）：仅改岩浆声音量（若在播即时生效）；幂等。由 PlayerController.lavaSoundLevel 驱动。
    Q_INVOKABLE void setLavaFlowLevel(float level);
    // t269 水中走路声：玩家脚位在水中迈步时播（替代按材质的 playStep）。机制等价 MC 水中走路声（原创程序
    //   合成，§9；零 MC 资产）。不分材质（水中听感统一闷浊）单件 clip；Main.qml onWalkPhaseChanged 据玩家
    //   feetInWater 分流：水中 → playWaterStep，陆地 → playStep(blockId)。音量略低于普通 step（水下传播衰减
    //   + 不抢水流声前景）。seek 重发不堆叠（同其他单件模式）；engine/clip 失败静默降级（§2-E，不崩）。
    Q_INVOKABLE void playWaterStep();

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
