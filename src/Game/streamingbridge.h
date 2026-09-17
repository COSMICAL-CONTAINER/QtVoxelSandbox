#ifndef STREAMINGBRIDGE_H
#define STREAMINGBRIDGE_H

#include <QObject>
#include <QMetaObject>
#include <QPointer> // §29.7：fixed 收割宿主的世界引用（QPointer——世界随 QML/矩阵腿存亡，悬垂自动归零）
#include <QString>
#include <QtQml/qqml.h>

#include <memory>

#include "gamesession.h"     // §29.5-W5 会话（冲洗 / 读档合并 / 流式泵的生产宿主——本桥是其在
                             //   QML 面的唯一投影；R20.07 编排壳纪律：桥零复制游戏/存档逻辑）
#include "playercontroller.h" // 位置沿喂入（playerChunkChanged → 会话 notePlayerChunk，W2 生产链）
#include "world.h"           // 模式迁移两法（reinitializeAsSparse / reinitializeAsFixed）+ isSparse
#include "worldclock.h"      // 生产泵拍源（ticked → pumpTick——与 QML 既有 tick 桥同拍同源单时钟）
#include "worldstore.h"      // 读档 overlay 的 blob 通路（只读 Q_INVOKABLE 消费——worldstore 零改动）

// ── §29.5-W5b 流式 UI（r2028）：QML ↔ W5 后端的最小消费桥（§29.5 登记的 QML 例外单）────────
// 计划原文（refactor-plan §29.5.2 W5b 半 + §29.5.3 选型 2）：新世界 UI「无限世界」开关（默认关，
// D2）；世界菜单 opt-in「转为无限世界」（D3——纯元数据转换，零数据搬迁）；保存链三写前置冲洗
// 接线（GameSession 会话面已就绪）。治理审计 #12 放行：本桥 + 两段 QML 是全弧唯一登记的新增
// QML 可达面；除此之外 QML 零字节触碰（r2028c 变更面集中钉守护）。
//
// 形态选型（头注释立证）：
//   · **QML 单例**（BuildInfo/FrameProfiler 同款 QML_SINGLETON + instance() 模式）——两段 QML 零
//     元素实例化即可按类型名调用（变更面集中的结构性前提：不新增 QML 元素、不挂 context property）。
//   · **有状态面只有会话所有权**：m_session = 流式世界进入期间存活的会话（构造即 W2/W3/W4 全量
//     通电——isSparse() 门内三缝接齐；fixed 世界连构造都不发生 = D2 零活动墙）。会话生命周期 =
//     enterWorld(true) 起至下一次 enterWorld / detachWorld——退出世界不拆（世界列表态无泵拍源，
//     worker 空闲挂起；下次进入原会话销毁重建，线程 join 有界）。
//   · **值组件零 QML 暴露**（ChunkStore/StreamWorldMeta/GameSession 类型零 Q_PROPERTY / 零值转发
//     ——存档域不进 QML，r2022d 同门；QML 只见五个布尔/动作 Q_INVOKABLE，见下方暴露清单）。
//   · **存档路径解析镜像**（登记的镜像面）：worldstore 一行禁触（W5 后端冻结域延续），savesDir 的
//     「exeDir/../saves → AppLocalData → exe 同级」三级解析与相对/绝对名路由在本桥逐行镜像
//     （chunkstore 独立命名连接开-用-关，与 worldstore kConn 互不占用）；两处一致性由 r2028b
//     真链腿行为级锚定（桥写标志行与 store 建库落同一文件）。绝对路径直用 = 矩阵腿临时库先例
//     （openWorld 绝对路径同门，saves/ 绝不触）。
//
// QML 暴露清单（全桥仅此五件，逐一矩阵正面钉 r2028c）：
//   flagNewWorldStreaming(file, coreW, coreD)  D2 创建链标志落库（新世界对话框勾选时调用；默认关
//                                              = 零调用 = fixed 零变化）。
//   saveIsStreaming(file)                      流式标志读面（转换动作显隐 + enterWorld 内部分流）。
//   convertSaveToStreaming(file, coreW, coreD) D3 opt-in 转换（纯元数据：标志 + core dims + 代次
//                                              锚；已转换 = 幂等 true；翻回不设 UI，登记）。
//   enterWorld(world, store, clock, player,
//              file, seed)                     进入分流：fixed 存档恒 false（顺带清流式残留 +
//                                              空网格归位，caller 走既有链逐字节原样）；流式存档
//                                              true = 桥内完成 sparse 重构 + overlay 读档 + 会话
//                                              通电 + 泵拍/位置沿挂钩。
//   flushForSave()                             保存链三写前置冲洗：非流式会话恒 true 零动作（fixed
//                                              保存链零变化）；流式 = 会话冲洗成败原样穿透（失败
//                                              上报不谎报——caller 门三写，r2015 complete 戳同门）。
//
// 分层（PLAN §2）：Game 层（GameSession 同域）——向下依赖 World/WorldStore/WorldClock/Player
// Controller + QtQml 注册面，不依赖 Renderer/Entities 之外的游戏子系统；QML 只经本类型名调用。
class StreamingBridge : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StreamingBridge)
    QML_SINGLETON

public:
    // QML 单例工厂（QML_SINGLETON 要求）：返回全局唯一实例（C++ / QML 共用同一对象，BuildInfo 同款）。
    static StreamingBridge *create(QQmlEngine *, QJSEngine *) { return instance(); }
    // C++ 全局访问点（redstone_matrix_test 真链腿经它取得与 QML 同一对象）。
    static StreamingBridge *instance();

    // ── QML 消费面（五件；语义见类头注暴露清单）────────────────────────────────────────
    // D2 创建链标志落库。core dims = 宿主世界栅格 dims（QML 传 store.world.width/depth——与 D3
    //   转换「被转换世界 dims」同口径；enter 时 sparse 重构即用该 core 域）。返回落库成败（QML
    //   忽略返回值亦安全：标志缺席 = 世界按 fixed 进入 = 现行语义，可重勾选重进补登记——诚实降级）。
    Q_INVOKABLE bool flagNewWorldStreaming(const QString &file, int coreWidth, int coreDepth);
    // 流式标志读面（无行 / 库缺席 / 读败 → false = fixed 语义；读败 qWarning 留痕——诚实降级）。
    Q_INVOKABLE bool saveIsStreaming(const QString &file) const;
    // D3 opt-in 转换（同一落点复用：markStreamingWorld 置位 + core dims + 代次锚；已转换 = 幂等
    //   true 零重锚——再转换锚语义属后端显式操作面，UI 不重触发）。返回转换成败。
    Q_INVOKABLE bool convertSaveToStreaming(const QString &file, int coreWidth, int coreDepth);
    // 进入分流（语义见类头注）。返回 true = 已按流式语义进入（caller 跳过 beginLoad/regenerate
    //   分流——sparse 世界七入口守卫本就早退，跳过是冗余保险）；false = 走既有 fixed 链（逐字节原样）。
    Q_INVOKABLE bool enterWorld(World *world, WorldStore *store, WorldClock *clock,
                                PlayerController *player, const QString &file, int seed);
    // 保存链三写前置冲洗（生产挂点 = Main.qml runExitSave 首行；onClosing 关窗路径同函数同门覆盖）。
    Q_INVOKABLE bool flushForSave();

    // ── C++ 面（零 QML 暴露；矩阵腿 / 诊断消费）────────────────────────────────────────
    // 会话存活读面（fixed/未进入恒 false = D2 零活动墙的桥侧投影）。
    bool sessionActive() const { return m_session != nullptr; }
    // 泵拍执行体（生产 = WorldClock::ticked 槽体；矩阵腿直调同体——与生产拍同一函数，无第二份）。
    void pumpTick();
    // 拆会话（进入链换世界 / fixed 清退路径内部使用；矩阵腿间复位缝）。线程件 join 有界（会话
    // 析构语义——worker 析构 stop + 丢弃计数 + join，绝不挂死）；幂等。
    void detachWorld();

private:
    StreamingBridge() = default;

    // saves/ 目录三级解析（worldstore::savesDir 的镜像——登记的镜像面，见类头注；语义逐行同源：
    // exeDir/../saves → AppLocalDataLocation/saves → exe 同级兜底；mkpath 即写权限探针）。
    static QString savesDir();
    // 存档库全路径（相对名 → saves/ 下；绝对名直用——矩阵临时库先例，QDir::absoluteFilePath 直通）。
    static QString resolveSavePath(const QString &file);
    // 流式标志行只读探（独立临时 ChunkStore 开-用-关；不建表不写库——纯读面）。
    static bool readMetaFlag(const QString &savePath, const QString &worldId,
                             struct StreamWorldMeta &out);
    // 泵拍 / 位置沿挂钩（幂等：已挂且对象未换 = 零动作；对象变更才重连——跨世界换 id 防御）。
    void ensurePumpHook(WorldClock *clock);
    void ensureFeedHook(PlayerController *player);

    std::unique_ptr<GameSession> m_session; // 流式会话（sparse 独占；fixed/未进入恒 null = 零活动墙）
    // ── §29.7 t1060 fixed 世界收割宿主（r2034；C1 完全体）───────────────────────────────
    // fixed 世界 bake 异步化的收割拍宿主 = 本桥既有泵拍：WorldClock::ticked 既有拍上的薄收割
    //   槽（C++ 槽挂接——fixed 进入分支 ensurePumpHook 复用与流式泵同一连接；QML 零改动、零
    //   第二计时器）。pumpTick 无会话（fixed）时调 World::harvestBuiltChunkMeshes()（单拍应用
    //   有界——黎明星空→白天 dayMul 跨门重烘风暴分帧摊平）。**宿主选型立证**：候选二
    //   「ChunkGeometry 帧驱动拉取」被否——QQuick3DGeometry 无每帧回调沿，500 段各自拉取 =
    //   500 个收割点（与 W4「收割拍单点收口」纪律相悖）；候选一胜出与 W5b 泵拍同宿（同一
    //   ticked 沿、同一连接、fixed/流式两态在 pumpTick 内分派）——不阻塞主线程（收割只灌注
    //   已产出网格）、延迟有界 ≤ 一拍（100ms）、QML 零改动三准绳全合。
    //   引用持 QPointer：世界归 QML/矩阵腿所有，跨世界/跨腿销毁后悬垂自动归零（pumpTick 静
    //   默跳过），无 dangling harvest 面。
    QPointer<World> m_fixedWorld;           // fixed 收割宿主的世界引用（未进入/流式态 = null）
    QMetaObject::Connection m_pumpConn;     // clock.ticked → pumpTick（幂等挂钩）
    QMetaObject::Connection m_feedConn;     // player.playerChunkChanged → notePlayerChunk（W2 生产链）
    WorldClock *m_pumpClock = nullptr;      // 已挂泵拍源（重入防御读面）
    PlayerController *m_feedPlayer = nullptr; // 已挂位置源（重入防御读面）
};

#endif // STREAMINGBRIDGE_H
