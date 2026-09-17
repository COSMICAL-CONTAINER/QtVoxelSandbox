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
//     差；首次保存的缓冲惰性重建成本 = r2015 已登记的一次性面），不做生产开关、不另建第二份冻结
//     实现；freeze 面的独立行为证明留矩阵测试域（r2015b）。成本登记：coordinator 首保存对宿主
//     dims 重建冻结缓冲（new World + dims setter 各触一次 worldgen ≈ 4 次一次性生成成本，跨保存
//     复用；生产接线若在意该成本属后续单优化面，§29.6 原文）。
//   · **SaveFaultHook 生产零挂载**：桥内钩子缺省恒空 = 生产形态恒不注入；矩阵腿经 C++ 面
//     setFaultHook 挂注入（r2015 五段缝语义原样——本桥不复制协议，只转发钩子）。
//
// QML 暴露清单（全桥仅此两件）：
//   saveViaCoordinator(store, file, name, chests, furnaces, dispensers, worldTime, bedSpawn,
//                      playerData, progress)  runExitSave 三写段的置换入口（一口进；返回 = 三写
//                                            全成 + complete 戳盖讫，与旧返回语义逐位同）。
//   recoveryState(file)                       恢复状态读面：fresh/clean/interrupted/open-error
//                                            四态字符串（Caller：enterWorld 对 interrupted
//                                            toast；clean/fresh 静默；open-error qWarning 留痕
//                                            诚实降级不弹窗——载入数据本身照常可用）。
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
                                        const QVariantMap &progress);
    // 恢复状态读面（台账只读；库缺席 = fresh；读不了 = open-error——#5② 可区分态，不谎报）。
    Q_INVOKABLE QString recoveryState(const QString &worldFile) const;

    // ── C++ 面（零 QML 暴露；矩阵腿 / 诊断消费）────────────────────────────────────────
    // 全量恢复读数（代次两键 + 态——行为腿断言面；QML 只见上面的字符串投影）。
    SaveGenerationInfo recoveryInfo(const QString &worldFile) const;
    // 故障注入缝转发（测试专用，生产零挂载——挂载点全 src 树仅矩阵腿；r2015 五段语义原样）。
    void setFaultHook(SaveFaultHook hook) { m_faultHook = std::move(hook); }

private:
    SaveBridge() = default;

    // saves/ 目录三级解析（worldstore::savesDir 的镜像——登记的镜像面，StreamingBridge::savesDir
    //   同款：exeDir/../saves → AppLocalDataLocation/saves → exe 同级兜底；mkpath 即写权限探针）。
    static QString savesDir();
    // 存档库全路径（相对名 → saves/ 下；绝对名直用——矩阵临时库先例，QDir::absoluteFilePath 直通）。
    static QString resolveSavePath(const QString &file);

    SaveFaultHook m_faultHook; // 生产恒空（缺省无钩 = 生产形态；逐保存转发给 coordinator）
};

#endif // SAVEBRIDGE_H
