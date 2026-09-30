#ifndef SIGNSTORE_H
#define SIGNSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QString>
#include <QStringList>

#include <array>
#include <unordered_map>

// 牌子文本存储（Game 层 ViewModel；t1113）。机制等价 MC 1.0「牌子文本存于方块」——**首个方块附挂
// 文本面**：每块牌子（按世界方块坐标键控）持一份 4 行 × 15 字符文本（1.0 口径 4 lines × 15 chars），
// 跨 UI 开关 / 跨会话持久（非 QML 本地态）；多块牌子各自独立 4 行。方块附挂数据**不放 state 面**
// （工程 chunk state 仅 8 bit 放不下 4 行文本），存储门 = 容器族同门（ChestStore/HopperStore 坐标键控
// 模式），落盘 = WorldStore sign_texts 表（saveAll 第 9 参 / loadSigns，hoppers/brewing 同门第 N 参
// 追加先例）。
//
// 生命周期（1.0 口径实读留痕）：放置时编辑一次（placeBlock 成功 → signPlaced 信号 → 呈现层开编辑面板，
// 关面板即存文本含空文本——1.0 无独立取消语义，牌子无论是否打字都保持放置）；**已放牌子无再编辑**
// （右键再编辑是 1.8+ 面实读留痕不取——useBlock 无牌子分支）；破坏 → 呈现层 clearSign 清条目（文本随
// 破丢失——1.0 口径掉落物无文本面，掉的是全新牌子物品）；失撑脱落（World::checkSignSupportOnEdit）→
// 呈现层 onWorldChanged 孤儿清扫回收条目（painting host cleanupVis 同门）。
//
// 设计：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属 Game/ViewModel，由 Main.qml 装配
// / 编辑面板读写 / 持久化桥接；零向上依赖）。文本是 QString（UTF-8，中文可输入——用户输入内容不涉
// MC 专有名词纪律面）。
//
// §4 法律 + §9：零 MC 专有名词（「牌子」「Sign」为通用描述词——告示牌先于 MC 存在的通用物件）。
class SignStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SignStore)
    Q_PROPERTY(int lineCount READ lineCount CONSTANT)
    // 文本版本号（任一牌子任一行写入自增）。Main.qml 编辑面板 delegate 触碰它取最新文本（同 ChestStore
    //   revision / chestChanged 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY signChanged)

public:
    explicit SignStore(QObject *parent = nullptr);
    // 活跃实例桥接（t1118）：main.cpp 牌面图像 provider（image://signboard）建于 engine 装配期、
    //   本类实例生于 QML 装配期（更晚）——两生命周期以静态指针衔接（MapStore::active 同门形态）。
    //   多实例时最后构造者生效（Main.qml 单实例惯例）。析构注销（见下）。
    static SignStore *active() { return s_active; }
    ~SignStore() override;

    static constexpr int kLinesPerSign = 4;  // MC 1.0 牌子 4 行
    static constexpr int kCharsPerLine = 15; // MC 1.0 牌子每行 15 字符

    int lineCount() const { return kLinesPerSign; }
    int revision() const { return m_revision; }
    int entryCount() const { return int(m_signs.size()); }

    // 某牌子某行文本（line 0..3；越界 / 无此牌 → 空串）。
    Q_INVOKABLE QString lineAt(int x, int y, int z, int line) const;
    // 写某牌子 4 行文本（编辑面板确认 / 关闭时调）。逐行截断到 kCharsPerLine（QString::left——QML 侧
    //   TextInput maximumLength 已挡 15，本截断是 C++ 侧双保险）；4 行全空 → 清条目（不落孤儿空键，
    //   ChestStore allChests「全空跳过落盘」同门口径）。
    Q_INVOKABLE void setText(int x, int y, int z,
                             const QString &line0, const QString &line1,
                             const QString &line2, const QString &line3);
    // 移除某牌子条目（破块清孤儿；不存在则 no-op，clearChest 同模式）。
    Q_INVOKABLE void clearSign(int x, int y, int z);
    // 清空全部牌子（跨世界切换时 Main.qml.enterWorld 经 loadAll 间接调）。空 → no-op（不无故发信号）。
    Q_INVOKABLE void clearAll();
    // 收集全部牌子为 QVariantList（每项 {x,y,z, lines:[l0..l3]}），供 Main.qml 传 worldStore.saveAll
    //   落盘（含全空文本牌子——放置未打字也是合法牌子，条目保留 = 存档 round-trip 后仍可读回空文本）。
    Q_INVOKABLE QVariantList allSigns() const;
    // 用存档 QVariantList（同 allSigns 形状）整体替换内存内容（先清空再填充；单次 emit signChanged）。
    //   Main.qml.enterWorld 调：signStore.loadAll(worldStore.loadSigns()) —— 替换语义即「清旧世界残留 +
    //   填本世界牌子」（同 chestStore.loadAll 模式）。空列表 → 仅清空。
    Q_INVOKABLE void loadAll(const QVariantList &signs);

signals:
    // 任一牌子文本变更（setText）/ 条目移除（clearSign / clearAll）。驱动 revision 自增 + 编辑面板刷新。
    void signChanged();

private:
    static SignStore *s_active; // 活跃实例（active() 拉取面；构造注册 / 析构注销，MapStore 同门）
    // 单牌 4 行文本。全空 4 行 = 合法条目（放置未打字），仅 clearSign/clearAll/loadAll 清条目。
    using Sign = std::array<QString, kLinesPerSign>;

    // 坐标 → 文本。QString 键（"x,y,z"）—— ChestStore key() 同款简单可读键面。
    std::unordered_map<QString, Sign> m_signs;
    int m_revision = 0;

    static QString key(int x, int y, int z); // "x,y,z"
    // 反解 key() 产物（"x,y,z" → x,y,z；坐标可负）。格式不符 → false（ChestStore::parseKey 同款）。
    static bool parseKey(const QString &k, int &x, int &y, int &z);
};

#endif // SIGNSTORE_H
