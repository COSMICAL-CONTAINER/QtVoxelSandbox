#ifndef ENTITYSTAGESTORE_H
#define ENTITYSTAGESTORE_H

// ── t1142 SAVE-02 / E1 实体卸载生命周期——暂存域 + 掉落物已提交快照域载体 ─────────────────
// 契约全文见 build/t1142_save02_design.txt（贰章四态协议）；本头只立数据面。三面：
//   · entity_staging 表（会话暂存域）：驱逐沿把所属 chunk 活体（mob + 掉落物两族）序列化行
//     upsert 入表（键 = eid 稳定身份——同体重驱逐替换不重复）；同会话回访 take（SELECT+DELETE
//     同拍）恰一次恢复；会话开启（loadStreamingWorld 头部）整体截断——进程死亡 → 暂存域灭 =
//     未提交的卸载实体随世界一并回退到上一完整保存代（与 chunk_staging 同一保存点语义）。
//   · item_entities 表（掉落物已提交快照域，新 additive 面）：完整保存事务内 DELETE 全量 +
//     INSERT（LIVE + STAGED 全体 = 全档快照——「死亡/被拾取后保存不复活」同门：空快照必须
//     清掉上一档旧行，禁按空跳过）。mob 已提交面 = entities 表（t1133 既有，经 SaveBridge
//     载荷合并 provider 进 t1129 事务——本类零涉）。
//   · 提交后暂存行**保留**：会话期回访仍以暂存行为恢复源（恰一次由 take 语义承载）；跨进程
//     由会话开启截断收敛（已提交面接手）。
//
// ── 行形状 ──────────────────────────────────────────────────────────────────────────────
//   行 = (kind, eid, cx, cz, data)：data = 序列化侧（Entities 层两管理器的 export 行）
//   QVariantMap 原样 JSON（键集由序列化层定义——本类只存取裸载荷不解析实体语义，worldstore
//   「裸列不解析」先例同门）；kind = 行族判别（mob / 掉落物——恢复路由按 kind 分派，车辆族
//   = 接口扩展位，审查边界不入本单）；eid = 稳定身份十进制文本（acquireSlot 单调配发）；
//   **暂存键 = (kind, eid) 复合主键**——eid 按族分配（两族独立游标都从 1 数起，跨族必然交叠
//   ），族前缀进键 = upsert 全局唯一（单列 eid 键会让后写族覆盖先写族 = 资产丢失面）。
//   cx/cz = 所有权（派生式：中心格 floorDiv16，事件点现算，无持久属主链）。
//
// ── 分层 / 卫生 ─────────────────────────────────────────────────────────────────────────
//   World 层（chunkstore 同门）：依赖仅 Qt Sql/Json/Core；Entities 层零 SQL（序列化归两管理
//   器，本类只见 QVariantMap/QString）；Game 层（GameSession/StreamingBridge）只经
//   QVariantList/std::function 消费（gamesession「零 Entities 类型依赖」分层不变）。
//   非 QObject、零 QML 暴露；独立命名连接开-用-关（busy timeout 归零 + t1098 句柄收内层
//   作用域同门）+ 外部事务连接变体（*_On——保存事务持有方 kConn 直用，不开不管连接）。
//   幂等建表纯追加零 bump 零破坏性 SQL（r2025d 反探纪律对新文件同守）。

#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// 错误码分域（result.h 百位段约定；2xx 段续号，chunkstore 210..212 后接 215）。
constexpr int kErrEntityStageNotBound = 215; // 未 bind（无库路径）——驱逐暂存缝 fail-safe 面
constexpr int kErrEntityStageSql      = 216; // 建表 / upsert / take / 快照写 SQL 失败（含外部锁）

// 行族判别常量（kind 列域；与 EntityManager::Kind 枚举同值——枚举前两位零扰动契约的矩阵
//   结构钉互锁，World 层零 Entities include 的等值声明面； Vehicles 族扩展 = 尾追加位）。
constexpr int kStageKindMob = 0;  // EntityManager::Kind::Mob
constexpr int kStageKindItem = 1; // EntityManager::Kind::Item

// ── EntityStageStore：实体卸载暂存 + 掉落物已提交快照（非 QObject 值语义组件）────────────
class EntityStageStore
{
public:
    EntityStageStore() = default;

    // 绑定存档库路径（ChunkStore::bind 同门——生产 = 流式会话进入链；矩阵 = fresh 临时库）。
    void bind(const QString &dbFilePath) { m_dbPath = dbFilePath; }
    bool isBound() const { return !m_dbPath.isEmpty(); }
    // stream_worlds 同门：世界标识（本表暂不键世界——一库一世界现实下自洽；预留与 chunkstore
    //   元数据域对齐的显式标识位，世界标识缺省空串 = 匿名键）。
    void setStreamWorldId(const QString &worldId) { m_worldId = worldId; }
    QString streamWorldId() const { return m_worldId; }

    // ── 暂存域写（驱逐沿 sink 生产消费面）────────────────────────────────────────────────
    // 单行 upsert（键 = eid）：cx/cz = 派生所有权，kind = 行族，row = 序列化行原样（含 eid）。
    //   SQL 病 → false（fail-safe 方向 = 调用方驱逐缝早退不释放活体——宁驻留不误删同门）。
    bool stageEntity(int cx, int cz, const QVariantMap &row);
    // 行数（诊断/矩阵断言面）。
    int stagedCount() const;
    // 全量读回（**非消费**——保存载荷合并 provider / 已提交快照装配面；takeChunk 消费面对偶）。
    //   行 = 序列化行原样（含 eid/kind 键；损坏载荷跳过）。SQL 病 → 空列表（诚实降级同门）。
    QVariantList stagedRows() const;
    // 回访恢复 take（SELECT + DELETE 同拍——恰一次语义本体；读败 = 空表不堵）。
    QVariantList takeChunk(int cx, int cz);
    // 会话开启截断（loadStreamingWorld 头部；DELETE 全表单语句）。SQL 病 → false（调用方
    //   qWarning 继续——上界守卫同门的诚实降级面）。
    bool clearStaging();

    // ── 已提交快照域（掉落物 item_entities；mob 面归 t1133 entities 表经载荷合并）────────
    // 完整保存事务内全档快照写（外部连接壳；调用方已 BEGIN）：DELETE 全量 + INSERT rows
    //   （LIVE + STAGED 全体——头注快照语义）。失败 → false（调用方回滚整事务）。
    //   rows = 序列化行原样（本类不解析）。
    bool commitItemSnapshotOn(QSqlDatabase &db, const QVariantList &rows);
    // 已提交快照读回（进入链恢复面唯一读点；表缺席（旧档）→ 空列表不崩）。
    QVariantList loadItemSnapshot() const;

private:
    QString m_dbPath;  // 存档库路径（独立连接开-用-关；不持连接状态）
    QString m_worldId; // 世界标识（预留位；缺省空串）
};

#endif // ENTITYSTAGESTORE_H
