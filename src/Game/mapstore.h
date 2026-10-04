#ifndef MAPSTORE_H
#define MAPSTORE_H

#include <QObject>
#include <QtQml/qqml.h>

#include <QByteArray>
#include <QImage>
#include <QVariantMap>

// map 地图数据存储（Game 层 ViewModel；t1114 初建 / t1132 扩展域 + 持久化首片）。机制等价 MC 1.0
// 「地图数据集」（Beta 1.8→1.0 基线内的地图机制）——**首个物品附挂绘制数据面**：激活空地图时建一份
// 1bpp（1 格 = 1 像素，单缩放）像素集，玩家持填充地图时周界列随走随绘（探索面），未到过的列保持未探索
// 底色。
//
// **数据集形态裁定（1.0 实读留痕三面——源码注 + 腿文 + 提交注）**：每会话**单份**全幅数据集（Beta
// 1.6-1.7.3「全图共享一份数据」期口径）。1.4.2+ 的 per-map 唯一数据不取——工程 ItemStack 无 id 外
// 附加面可携带 per-map 序号（Hotbar::normalizeDurability 对材料段恒归 0 的实读留痕），两形 id 拆分
// （空地图 0x290 / 填充地图 0x291，recipe.h）+ 单份数据集 = 工程激活链的最小自洽面；同栈多张填充
// 地图读同一视图，与 Beta 1.6 期共享口径一致。
//
// **范围裁定（t1132 MAP-02 修订——era 核实留痕）**：era 1.0 地图 = 128×128 固定画布、激活时以玩家
// 位置定中心、1:1 单缩放、画布绑定地图物品自身原点（首用定锚之后恒定）；per-map 多份画布需要物品侧
// per-map 身份（era 走 damage 存序号），工程材料段 damage=normalizeDurability 恒 0 纪律冲突 → per-map
// 族**裁定入候选池**（见文末裁定锚）。工程取**「世界核心域 + 四侧探索前沿带」单份有界数据集**（t1114
// 「有限世界全幅渲染」先例的延伸面）：数据集恒覆 [-kDomainMargin, worldWidth + kDomainMargin) ×
// [-kDomainMargin, worldDepth + kDomainMargin)（流式世界核心域外的负坐标 / 超界列可绘 = MAP-02 扩展
// 坐标绘制），**有界不无限**（era 128 画布封顶的工程对应面——带外恒不绘不扩）。fixed 世界带内无地形
// 数据（heightmapAt 越界 = -1）→ 探索扫描按「无地形不绘」门跳过，带保持未探索底色（不绘虚空）。
//
// **存档面裁定（t1132 MAP-03 首片——共享口径持久化，era 偏离如实登记）**：era 1.0 真值 = per-map
// 独立持久化（map_xx.dat：颜色数组 + x/z 中心 + 尺寸 + 缩放）；工程无 per-item 附加数据门（t1114
// 实读留痕在案）→ per-map 身份族候选池（文末裁定锚）；**本片交付共享口径的 per-world 持久化**——
// 每世界单行 map_dataset 表（width/depth/revision/pixels BLOB，initSchema 纯追加，旧档无行 = 空集
// 行为降级如实），进世界 loadVariant 整体替换内存（无存档行 → 清 = t1114 会话口径降级），退出经
// 统一保存链第 10 参同事务落盘（SaveRequest.mapDataset → writeWorldPart → writeMapDataset，signs
// 第 9 参同门）。存退重进探索保持 = 本片验收面；跨世界隔离 = 表随库文件（每世界一库）天然隔离。
//
// 设计：纯像素存储，不持 World / 不读栅格（PLAN §2 分层：地形顶色计算在 PlayerController（有
// m_world），本类只收 ARGB 写入 / 出 QImage；Game 层零向上依赖）。像素 = quint32 ARGB（0xAARRGGBB），
// 与 QImage::Format_ARGB32 同 layouts，renderImage 零转换包装。
//
// 显示面：main.cpp 胶水层 MapAtlasProvider（image://mapstore/<revision>，t414 rp provider 同门——
// QtQuick 依赖留 app 胶水层）把 renderImage() 交给 Main.qml 手持地图 overlay 的 Image；revision 变更
// 换 URL → QML 源缓存自动失效重取（一批一 bump，免逐列抖 QML）。overlay 玩家位点投影 = 世界坐标 +
// kDomainMargin 后除数据集尺寸（QML 经 mapMargin 属性取带宽——常量单一权威在本类，QML 零复制）。
//
// §4 法律 + §9：零 MC 专有名词（「地图」通用词先于 MC 存在）；色板 = 工程原创自然色调（§9a，
// PlayerController::mapColumnColor，零 MC 资产）。
class MapStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MapStore)
    // 数据集在库（激活后 true；clearAll 后 false）。Main.qml 手持地图 overlay 显隐门之一。
    Q_PROPERTY(bool hasMap READ hasMap NOTIFY mapChanged)
    // 数据集尺寸（含四侧前沿带：世界核心域 + 2×kDomainMargin；Main.qml overlay 尺寸行消费）。
    Q_PROPERTY(int mapWidth READ mapWidth NOTIFY mapChanged)
    Q_PROPERTY(int mapDepth READ mapDepth NOTIFY mapChanged)
    // 探索前沿带半宽（常量投影面：Main.qml 位点投影 / 矩阵腿取值——单一权威，防 QML 复制字面）。
    Q_PROPERTY(int mapMargin READ mapMargin CONSTANT)
    // 数据集版本号（任一批列写入 / 建库 / 清库自增）。QML Image 源 URL 携它换值强制重取（同
    //   ChestStore revision / chestChanged 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY mapChanged)

public:
    explicit MapStore(QObject *parent = nullptr);
    ~MapStore() override;

    // 活跃实例（main.cpp 图像 provider 拉取面：provider 建于 engine 装配期、MapStore 实例生于 QML
    //   装配期，两生命周期以静态指针桥接——ResourcePackManager 静态 compositeAtlas 同门形态）。
    //   多实例时最后构造者生效（Main.qml 单实例惯例）。
    static MapStore *active() { return s_active; }

    // 未探索底色（ARGB）：暗灰蓝黑（MC 未绘区读感的工程原创色，§9a——非取自 MC 资产）。
    static constexpr quint32 kUnexploredColor = 0xFF15151a;
    // 探索前沿带半宽（t1132 MAP-02）：数据集域 = 核心域四侧各扩此带（era 128 画布封顶的工程对应
    //   面——**有界**扩展，禁无限位图）。64 列 ≈ 4 chunk 环，覆盖核心域外正常探索半径；带外列恒
    //   不绘（MapStore 写读双门 + PlayerController 扫描域门，双保险）。
    static constexpr int kDomainMargin = 64;

    bool hasMap() const { return m_width > 0 && m_depth > 0; }
    int mapWidth() const { return m_width; }   // 数据集宽（核心域 + 2×kDomainMargin）
    int mapDepth() const { return m_depth; }   // 数据集深（同上）
    int mapMargin() const { return kDomainMargin; }
    int revision() const { return m_revision; }

    // 建库（激活面 / 惰性重建共用）：入参 = **世界核心域尺寸**（调用方传 World 尺寸，逐字节同
    //   t1114 调用形）；数据集定版 = 核心域 + 四侧前沿带 + 全图未探索底色 + revision 自增单信号。
    //   重复建库（已建再建）= 整库重置（新尺寸 / 全未探索）——跨世界 loadVariant 空行降级路径即
    //   走此形态。
    void initialize(int width, int depth);
    // 写单列色（探索写入；**世界坐标**——负坐标 / 超核心域列落前沿带内即绘，带外 / 未建库 no-op）。
    //   **不发信号**——批量写后 commitColumns() 一次 bump（一批一信号，免逐列抖 QML）。
    void writeColumn(int x, int z, quint32 argb);
    // 批次收口：revision 自增 + 单次 mapChanged（PlayerController::refreshMapAroundPlayer 盒扫后调）。
    void commitColumns();
    // 读单列色（测试腿断言面；世界坐标——带外 / 未建库 → kUnexploredColor）。
    quint32 columnColor(int x, int z) const;
    // 像素导出（main.cpp MapAtlasProvider 拉取面）：Format_ARGB32 包装副本（provider 持有独立
    //   QImage，免与 m_pixels 生命周期耦合）。未建库 → 空图。
    QImage renderImage() const;
    // 清库（会话口径收口：loadVariant 空行降级路径内部复用；Q_INVOKABLE 面保留供显式清）。
    //   未建库 → no-op（不无故发信号，SignStore::clearAll 同门口径）。
    Q_INVOKABLE void clearAll();
    // 导出数据集（t1132 MAP-03：Main.qml runExitSave 经统一保存链第 10 参落盘；世界核心域尺寸 +
    //   revision + 全幅像素 BLOB 原样）。未建库 → 空 map（caller 不写行 / 写空 = 表清空语义，与
    //   容器表 DELETE 全量口径一致）。
    Q_INVOKABLE QVariantMap exportVariant() const;
    // 装载数据集（t1132 MAP-03：Main.qml enterWorld 整体替换内存——signStore.loadAll 同门位）。
    //   空 map / 缺 present 键（旧档无行 / 无表）→ clearAll 降级（会话口径，重探索再填充面）；
    //   有行 → initialize(核心域尺寸) 后像素 BLOB 原样回填 + revision 复原（尺寸账不平 = 存档
    //   病 → 诚实降级 clearAll，不载半截数据）。
    Q_INVOKABLE void loadVariant(const QVariantMap &data);

signals:
    // 数据集变更（initialize / commitColumns / clearAll / loadVariant）。驱动 revision 自增 +
    //   overlay 重取。
    void mapChanged();

private:
    static MapStore *s_active; // 活跃实例（active() 拉取面；构造注册 / 析构注销）

    int m_worldWidth = 0; // 世界核心域宽（initialize 入参原样；导出 / 装载往返的尺寸语义域）
    int m_worldDepth = 0; // 世界核心域深（同上）
    int m_width = 0;      // 数据集宽 = m_worldWidth + 2×kDomainMargin（1 格 = 1 像素）
    int m_depth = 0;      // 数据集深 = m_worldDepth + 2×kDomainMargin
    int m_revision = 0;   // 版本号（QML Image 源 URL 失效键）
    QByteArray m_pixels;  // ARGB32 行优先像素（width*depth*4 字节；quint32 逐格；下标 = (z+带)×宽
                          //   + (x+带)——前沿带负坐标列经 +kDomainMargin 平移入格）
};

#endif // MAPSTORE_H
