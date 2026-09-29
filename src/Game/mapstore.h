#ifndef MAPSTORE_H
#define MAPSTORE_H

#include <QObject>
#include <QtQml/qqml.h>

#include <QByteArray>
#include <QImage>

// map 地图数据存储（Game 层 ViewModel；t1114）。机制等价 MC 1.0「地图数据集」（Beta 1.8→1.0 基线内
// 的地图机制）——**首个物品附挂绘制数据面**：激活空地图时建一份全幅 1bpp（1 格 = 1 像素，单缩放）
// 像素集，玩家持填充地图时周界列随走随绘（探索面），未到过的列保持未探索底色。
//
// **数据集形态裁定（1.0 实读留痕三面——源码注 + 腿文 + 提交注）**：每会话**单份**全幅数据集（Beta
// 1.6-1.7.3「全图共享一份数据」期口径）。1.4.2+ 的 per-map 唯一数据不取——工程 ItemStack 无 id 外
// 附加面可携带 per-map 序号（Hotbar::normalizeDurability 对材料段恒归 0 的实读留痕），两形 id 拆分
// （空地图 0x290 / 填充地图 0x291，recipe.h）+ 单份数据集 = 工程激活链的最小自洽面；同栈多张填充
// 地图读同一视图，与 Beta 1.6 期共享口径一致。
//
// **范围裁定**：数据集恒覆**全幅世界**（width × depth，1 格 = 1 像素）——MC 1.0 是 128×128 激活锚点
// 制，工程取全幅 = 「有限世界全幅渲染」先例（chunkgeometry 同门）的地图面延伸，单缩放 1bpp 口径
// 保真；无激活锚点即无「存档丢锚重锚」面（会话口径的天然简化，登记见下）。
//
// **存档面裁定（会话口径如实登记，t1013 箱车 / t1112 鞍面 / t1113 牌面先例族）**：地图数据是物品
// 附挂数据（非方块附挂），工程背包物品栈仅 {itemId, count, durability} 三面（durability 材料段恒 0）
// → 无 per-item 附加数据门可依；worldstore 表族亦无坐标键控面可依（地图非方块）→ **零存档门**。
// 进世界 clearAll（Main.qml.enterWorld，signStore.loadAll 同门位置——跨世界泄漏收口）+ 持图惰性
// 重建（PlayerController::tickMapExploration，重探索再填充面如实登记）。
//
// 设计：纯像素存储，不持 World / 不读栅格（PLAN §2 分层：地形顶色计算在 PlayerController（有
// m_world），本类只收 ARGB 写入 / 出 QImage；Game 层零向上依赖）。像素 = quint32 ARGB（0xAARRGGBB），
// 与 QImage::Format_ARGB32 同 layouts，renderImage 零转换包装。
//
// 显示面：main.cpp 胶水层 MapAtlasProvider（image://mapstore/<revision>，t414 rp provider 同门——
// QtQuick 依赖留 app 胶水层）把 renderImage() 交给 Main.qml 手持地图 overlay 的 Image；revision 变更
// 换 URL → QML 源缓存自动失效重取（一批一 bump，免逐列抖 QML）。
//
// §4 法律 + §9：零 MC 专有名词（「地图」通用词先于 MC 存在）；色板 = 工程原创自然色调（§9a，
// PlayerController::mapColumnColor，零 MC 资产）。
class MapStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MapStore)
    // 数据集在库（激活后 true；clearAll 后 false）。Main.qml 手持地图 overlay 显隐门之一。
    Q_PROPERTY(bool hasMap READ hasMap NOTIFY mapChanged)
    Q_PROPERTY(int mapWidth READ mapWidth NOTIFY mapChanged)
    Q_PROPERTY(int mapDepth READ mapDepth NOTIFY mapChanged)
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

    bool hasMap() const { return m_width > 0 && m_depth > 0; }
    int mapWidth() const { return m_width; }
    int mapDepth() const { return m_depth; }
    int revision() const { return m_revision; }

    // 建库（激活面 / 惰性重建共用）：尺寸定版 + 全图未探索底色 + revision 自增单信号。重复建库
    //   （已建再建）= 整库重置（新尺寸 / 全未探索）——跨世界 clearAll 后惰性重建即走此路径。
    void initialize(int width, int depth);
    // 写单列色（探索写入；越界 / 未建库 no-op）。**不发信号**——批量写后 commitColumns() 一次
    //   bump（一批一信号，免逐列抖 QML）。
    void writeColumn(int x, int z, quint32 argb);
    // 批次收口：revision 自增 + 单次 mapChanged（PlayerController::refreshMapAroundPlayer 盒扫后调）。
    void commitColumns();
    // 读单列色（测试腿断言面；越界 / 未建库 → kUnexploredColor）。
    quint32 columnColor(int x, int z) const;
    // 像素导出（main.cpp MapAtlasProvider 拉取面）：Format_ARGB32 包装副本（provider 持有独立
    //   QImage，免与 m_pixels 生命周期耦合）。未建库 → 空图。
    QImage renderImage() const;
    // 清库（跨世界泄漏收口：Main.qml.enterWorld 调）。未建库 → no-op（不无故发信号，clearChest
    //   同门口径）。
    Q_INVOKABLE void clearAll();

signals:
    // 数据集变更（initialize / commitColumns / clearAll）。驱动 revision 自增 + overlay 重取。
    void mapChanged();

private:
    static MapStore *s_active; // 活跃实例（active() 拉取面；构造注册 / 析构注销）

    int m_width = 0;      // 数据集宽（= 世界 width，1 格 = 1 像素）
    int m_depth = 0;      // 数据集深（= 世界 depth）
    int m_revision = 0;   // 版本号（QML Image 源 URL 失效键）
    QByteArray m_pixels;  // ARGB32 行优先像素（width*depth*4 字节；quint32 逐格）
};

#endif // MAPSTORE_H
