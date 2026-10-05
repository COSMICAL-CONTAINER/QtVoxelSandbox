#ifndef SAVEBRIDGE_H
#define SAVEBRIDGE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include "savecoordinator.h" // 台账/代次/恢复状态机 + SaveFaultHook 缝（生产零挂载）
#include "worldstore.h"      // 三写执行体（现有 Q_INVOKABLE 面——worldstore 冻结域零改动）

// ── t1057 SaveCoordinator 生产接线（refactor-plan §29.6）：QML ↔ SaveCoordinator 的最小消费桥
//    （QML 例外单——StreamingBridge/W5b 同款形态；audit #9 候选表「已建未用」大件收官）────────
// r2015 组件（marker-first/代次/恢复状态机/FaultHook 缝）零接线在库；本桥把它接到生产保存/读档链：
//   · 保存链：Main.qml runExitSave 三写段（savePlayerData/saveAll/saveProgress 三调用）置换为本桥
//     一口进——内部序 = ①coordinator 写 attempt 标记（代次 g+1）②三写照旧调 WorldStore 现有
//     Q_INVOKABLE（逐字节原样；worldstore 冻结域零改动）③全成才盖 complete 戳；任一败 = 标记
//     留档（下次读档 Interrupted）且返回 false——与旧 runExitSave「三写全成才真」返回语义逐位同。
//     saveOkCount 观测面语义不变（成功 = 三部分各 +1 = +3；失败不计数，t974 契约原样）。
//   · 读档面：openWorld 后 recoveryState(file) → Clean/Fresh 静默；Interrupted → caller（Main.qml
//     enterWorld）toast 提示 + 建议立即重存（重存收敛 = Interrupted 的唯一恢复动作，r2015 语义）。
//   · #5②（review0916 #5）：recover() 开库/读库失败从静默 Fresh 改为可区分 OpenError 态（修复
//     本体在 savecoordinator，本桥按态映射字符串上报）。
//   · t1064（review0901 #36 清偿）退出存档退避重试面：exitSaveRetryBackoff(store, file)——
//     Main.qml 退出存档失败重试 wrapper（两路径共用唯一实现）在首试失败后调用：独立连接小事务
//     探锁（BEGIN EXCLUSIVE + ROLLBACK，零数据写零计数面），锁在持才同步退避一档后整链重试
//     恰一次；退避上限 ≤300ms 卡死 = 登记原文口径（防把关窗/退出阻塞成秒级「未响应」）。
//
// 形态选型（头注释立证）：
//   · **QML 单例**（StreamingBridge/BuildInfo 同款 QML_SINGLETON + instance() 模式）——QML 零元素
//     实例化即可按类型名调用（变更面集中：不新增 QML 元素、不挂 context property）。
//   · **签名 = 分参而非打包 QVariantMap**（与 QML 传递便利性权衡后立证）：八参按 SaveRequest 字段
//     序（name/chests/furnaces/dispensers/worldTime/bedSpawn/playerData/progress）+ store/file 两
//     定位参。打包 map 的键是字符串——拼错键会**静默**变成空 map，而「playerData 空 = 该部分不
//     请求」是 coordinator 协议语义，拼错键 = 静默丢该部分写（不可接受）；分参由 QML 引擎做形参
//     数量/类型检查，错传响亮。且旧 saveAll 本就是六分参形态（t188/t177/t542/t1016/t1024 逐参
//     演化），本签名与其同族。
//   · **store/file 逐调用传入**（StreamingBridge::enterWorld 同款对象传递先例）：桥自身无状态
//     （不持 WorldStore*/路径缓存）——无悬垂指针面、无跨世界陈旧绑定面；db 路径经 savesDir 三级
//     解析镜像（worldstore 一行禁触的登记镜像面，StreamingBridge::savesDir 同款；绝对路径直用 =
//     矩阵临时库先例）。
//   · **冻结机制不接线选型（§29.6 转抄）**：r2015 ①冻结-持久化分离的 freeze 缓冲在生产冗余——
//     保存链同步单线程无重入（t1055 核），世界不可能在写中途变化；生产只取其簿记面（marker/
//     代次/Interrupted/complete 权威）。freeze 面随 coordinator 协议原样保留（字节面恒等无行为
//     差），不做生产开关、不另建第二份冻结实现；freeze 面的独立行为证明留矩阵测试域（r2015b）。
//     成本登记（t1070 件三修订）：coordinator 首保存对宿主 dims 重建冻结缓冲（new World + dims
//     setter 各触一次 worldgen ≈ 4 次一次性生成成本，跨保存复用；§29.6 原文登记的「后续单优化
//     面」已由**桥内长活 coordinator**兑现——见 m_coord 注；旧逐保存栈上实例形态的每保存重付
//     面 = Review_2026-09-18 #1，随本单关闭）。
//   · **SaveFaultHook 生产零挂载**：桥内钩子缺省恒空 = 生产形态恒不注入；矩阵腿经 C++ 面
//     setFaultHook 挂注入（r2015 五段缝语义原样——本桥不复制协议，只转发钩子）。
//
// QML 暴露清单（全桥仅此三件）：
//   saveViaCoordinator(store, file, name, chests, furnaces, dispensers, worldTime, bedSpawn,
//                      playerData, progress)  runExitSave 三写段的置换入口（一口进；返回 = 三写
//                                            全成 + complete 戳盖讫，与旧返回语义逐位同）。
//   recoveryState(file)                       恢复状态读面：fresh/clean/interrupted/open-error
//                                            四态字符串（Caller：enterWorld 对 interrupted
//                                            toast；clean/fresh 静默；open-error qWarning 留痕
//                                            诚实降级不弹窗——载入数据本身照常可用）。
//   exitSaveRetryBackoff(store, file)         t1064 探锁退避：退出存档首试失败后问一次「锁还在
//                                            吗」——在持则同步退避一档（≤300ms 上限），返回实际
//                                            退避毫秒（0 = 锁已释放/无库可探 = 零退避，caller
//                                            立即重试 = 旧 0ms 行为对无锁失败保真）。

// ── t1064 退避一档（review0901 #36 清偿）──────────────────────────────────────────────
// 上限 ≤300ms = 登记原文口径（卡死上限防把关窗/退出阻塞成秒级「未响应」）；取值 150ms =
//   「外部锁窗（杀软/索引器/同步盘）常为百毫秒级」的登记观感与退出响应性的一档折中。只睡一次
//   不轮询：重试次数由 QML caller 的「恰一次重试」钉死（重试风暴面在 caller，不在桥）。
constexpr int kExitSaveBackoffMs = 150;
//
// 分层（PLAN §2）：Game 层（StreamingBridge 同域）——向下依赖 World 层 SaveCoordinator/WorldStore
// （savecoordinator 同 r2015 分层先例）+ QtQml 注册面；QML 只经本类型名调用。
class SaveBridge : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SaveBridge)
    QML_SINGLETON

public:
    // QML 单例工厂（QML_SINGLETON 要求）：返回全局唯一实例（C++ / QML 共用同一对象，同款先例）。
    static SaveBridge *create(QQmlEngine *, QJSEngine *) { return instance(); }
    // C++ 全局访问点（矩阵 real-chain 腿经它取得与 QML 同一对象）。
    static SaveBridge *instance();

    // ── QML 消费面（两件；语义见类头注暴露清单）────────────────────────────────────────
    // 统一保存一口进（runExitSave 三写段置换入口）。任一参不全 / store 未开库 → false（诚实
    //   失败——caller 重试 + toast 兜底与旧链同门）。返回 true = marker→三写→complete 全程成。
    Q_INVOKABLE bool saveViaCoordinator(WorldStore *store, const QString &worldFile,
                                        const QString &worldName, const QVariantList &chests,
                                        const QVariantList &furnaces,
                                        const QVariantList &dispensers,
                                        const QVariantMap &worldTime,
                                        const QVariantMap &bedSpawn,
                                        const QVariantMap &playerData,
                                        const QVariantMap &progress,
                                        const QVariantList &hoppers = {}, // t1080 漏斗内容（缺省空 = 旧 caller 零改动兼容）
                                        const QVariantList &brewingStands = {}, // t1113 补正：酿造内容透传（t1097 曾只加下游参未跟桥链 → 第 12 参被旧 11 参签名静默丢弃）
                                        const QVariantList &signs = {}, // t1113 牌子文本（缺省空 = 旧 caller 零改动兼容）
                                        const QVariantMap &mapDataset = {}, // t1132 地图数据集透传（缺省空 = 旧 caller 零改动兼容；MapStore::exportVariant 产物）
                                        const QVariantList &entities = {}, // t1133 生物持久化透传（第 15 参；EntityManager::exportPersistedEntities 产物；缺省空 = 表清空[全灭快照]，旧 caller 零改动兼容）
                                        const QVariantList &pistonAnims = {}); // t1137 活塞两拍动画透传（第 16 参；World::exportPistonAnims 产物；缺省空 = 表清空[无在册动画快照]，旧 caller 零改动兼容）
    // 恢复状态读面（台账只读；库缺席 = fresh；读不了 = open-error——#5② 可区分态，不谎报）。
    Q_INVOKABLE QString recoveryState(const QString &worldFile) const;
    // t1064 探锁退避（QML 消费面第三件；退出存档首试失败后调，选型立证见实现头注）：
    //   独立连接小事务探锁（BEGIN EXCLUSIVE + ROLLBACK——零数据写、零计数面），锁在持 = 同步
    //   睡 kExitSaveBackoffMs 一档；锁已释放 / 无库可探 = 立即返回。返回实际退避毫秒
    //   （0 = 未退避）——QML/矩阵观测面（零退避柱 / 退避窗断言面）。
    Q_INVOKABLE int exitSaveRetryBackoff(WorldStore *store, const QString &worldFile);

    // ── C++ 面（零 QML 暴露；矩阵腿 / 诊断消费）────────────────────────────────────────
    // 全量恢复读数（代次两键 + 态——行为腿断言面；QML 只见上面的字符串投影）。
    SaveGenerationInfo recoveryInfo(const QString &worldFile) const;
    // 故障注入缝转发（测试专用，生产零挂载——挂载点全 src 树仅矩阵腿；r2015 五段语义原样）。
    void setFaultHook(SaveFaultHook hook) { m_faultHook = std::move(hook); }
    // t1129 流式冲洗缝（生产装配点 = StreamingBridge::instance 一次性登记；矩阵腿可覆写挂
    //   本地会话——r2028 同门「C++ 面零 QML 暴露」）。flush = 保存事务内执行（false = 回滚整
    //   事务）；commit = 提交成功尾的内存账面收口。逐保存转发给桥内 coordinator（fault 钩同门）。
    void setFlushHook(SaveFlushHook hook) { m_flushHook = std::move(hook); }
    void setFlushCommitHook(SaveFlushCommitHook hook) { m_flushCommitHook = std::move(hook); }
    // t1070 件三诊断面（矩阵 r2044c 断言用；C++ only 非 QML）：桥内长活 coordinator 的冻结
    //   缓冲重建计数透传（语义与陈旧防面论证见 m_coord 注 / savecoordinator.h ensureBuffer 注）。
    int frozenBufferRebuildCount() const { return m_coord.bufferRebuildCount(); }

private:
    SaveBridge() = default;

    // saves/ 目录三级解析（worldstore::savesDir 的镜像——登记的镜像面，StreamingBridge::savesDir
    //   同款：exeDir/../saves → AppLocalDataLocation/saves → exe 同级兜底；mkpath 即写权限探针）。
    static QString savesDir();
    // 存档库全路径（相对名 → saves/ 下；绝对名直用——矩阵临时库先例，QDir::absoluteFilePath 直通）。
    static QString resolveSavePath(const QString &file);

    SaveFaultHook m_faultHook; // 生产恒空（缺省无钩 = 生产形态；逐保存转发给 coordinator）
    SaveFlushHook m_flushHook;             // t1129：生产由 StreamingBridge 登记（缺省空 = fixed 形态）
    SaveFlushCommitHook m_flushCommitHook; // t1129：提交面账面收口（与 flush 钩成对登记）
    // **t1070 件三（Review_2026-09-18 #1 清偿）：长活 coordinator**——r2015「冻结缓冲跨保存
    //   复用」的实现前提（旧形态 = 逐保存栈上实例 → 复用被打断，每次保存重付 ≈4 次一次性
    //   worldgen + 4 条统计 qInfo）。桥是进程级 QML 单例（GUI 线程单线程消费），coordinator
    //   与其冻结缓冲 World 随桥长活：首保存按 dims 重建一次，此后同 dims 保存全复用
    //   （bufferRebuildCount 诊断面 + frozenBufferRebuildCount 透传 = 行为级锚）。陈旧冻结点
    //   防面（判据读码定，宁可失效勤）：①快照**每保存重新冻结**（captureSnapshot 是对活体的
    //   唯一读点——复用的只是 World 壳非冻结点本身）；②beginLoad 每保存把缓冲网格重置全零
    //   再全量重放冻结点——复用缓冲的内容面与「每保存新建」逐字节等价，「窗内改动活体→重读
    //   = 冻结点 ≠ 活体」承重墙逐位不弱化（r2044c 行为级复验）；③dims 变化（跨世界）走
    //   ensureBuffer 既有重建分支，bind 每保存重绑 store/路径（逐调用传入语义不变，无陈旧
    //   绑定面）。只读恢复面（recoveryInfo）仍用栈上局部实例——recover 不触冻结缓冲。
    SaveCoordinator m_coord;
};

#endif // SAVEBRIDGE_H
