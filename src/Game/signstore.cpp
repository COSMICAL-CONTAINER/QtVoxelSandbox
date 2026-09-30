#include "signstore.h"

#include <algorithm> // std::all_of
#include <utility>   // std::move

// 牌子文本存储实现（声明见 .h 注——首个方块附挂文本面；Game 层纯存储 + QML 面向面）。

SignStore *SignStore::s_active = nullptr;

SignStore::SignStore(QObject *parent)
    : QObject(parent)
{
    s_active = this; // 活跃实例注册（最后构造者生效；provider 拉取面，MapStore 同门）。
}

// 析构注销（QML 单实例长寿——注销防悬垂；多实例栈对象析构不误清后继活跃实例）。
SignStore::~SignStore()
{
    if (s_active == this)
        s_active = nullptr;
}

// 某牌子某行文本。line 越界 / 无此牌 → 空串（与 ChestStore::slotIdAt 越界返 0 同款兜底口径）。
QString SignStore::lineAt(int x, int y, int z, int line) const
{
    if (line < 0 || line >= kLinesPerSign) return QString();
    const auto it = m_signs.find(key(x, y, z));
    if (it == m_signs.cend()) return QString();
    return it->second[size_t(line)];
}

// 写某牌子 4 行文本。逐行截断到 kCharsPerLine（QML 侧 TextInput maximumLength 已挡 15，本截断是 C++
//   侧双保险）；4 行全空 → 清条目（不落孤儿空键，ChestStore「全空跳过落盘」同门口径）。
void SignStore::setText(int x, int y, int z,
                        const QString &line0, const QString &line1,
                        const QString &line2, const QString &line3)
{
    const QString k = key(x, y, z);
    const Sign lines = { line0.left(kCharsPerLine), line1.left(kCharsPerLine),
                         line2.left(kCharsPerLine), line3.left(kCharsPerLine) };
    const bool allEmpty = std::all_of(lines.cbegin(), lines.cend(),
                                      [](const QString &s) { return s.isEmpty(); });
    if (allEmpty) {
        // 全空 4 行 = 未打字的牌子：不落条目（lineAt 兜底空串行为等价；跨存档 round-trip 空列表 =
        //   空 27 槽箱子同款「缺失条目 = 行为等价」口径）。已有条目（曾打过字再清空全部行）→ 移除。
        if (m_signs.erase(k) > 0) {
            ++m_revision;
            emit signChanged();
        }
        return;
    }
    m_signs[k] = lines;
    ++m_revision;
    emit signChanged();
}

// 移除某牌子条目（破块清孤儿；不存在则 no-op，clearChest 同模式）。
void SignStore::clearSign(int x, int y, int z)
{
    if (m_signs.erase(key(x, y, z)) > 0) {
        ++m_revision;
        emit signChanged();
    }
}

// 清空全部牌子（跨世界切换时 Main.qml.enterWorld 经 loadAll 间接调）。空 → no-op（不无故发信号）。
void SignStore::clearAll()
{
    if (m_signs.empty()) return;
    m_signs.clear();
    ++m_revision;
    emit signChanged();
}

// 收集全部牌子为 QVariantList（每项 {x,y,z, lines:[l0..l3]}）。含全空文本牌子不适用——setText 全空
//   已不落条目，故此处条目恒有 ≥1 非空行（落盘省行，同 chests 全空跳过口径）。
QVariantList SignStore::allSigns() const
{
    QVariantList out;
    for (const auto &kv : m_signs) {
        int x = 0, y = 0, z = 0;
        // 键面内部自产自销（key() 仅本类写入）→ 反解失败理论不可达；防御跳过不崩（§2-E 静默降级）。
        if (!parseKey(kv.first, x, y, z)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("x"), x);
        m.insert(QStringLiteral("y"), y);
        m.insert(QStringLiteral("z"), z);
        QVariantList lines;
        for (const QString &s : kv.second) lines.append(s);
        m.insert(QStringLiteral("lines"), lines);
        out.append(m);
    }
    return out;
}

// 用存档 QVariantList（同 allSigns 形状）整体替换内存内容（先清空再填充；单次 emit signChanged）。
void SignStore::loadAll(const QVariantList &signs)
{
    m_signs.clear();
    for (const QVariant &v : signs) {
        const QVariantMap m = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = m.value(QStringLiteral("x")).toInt(&okx);
        const int y = m.value(QStringLiteral("y")).toInt(&oky);
        const int z = m.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue; // 缺坐标 → 跳过（不写残条目）
        Sign lines;
        const QVariantList in = m.value(QStringLiteral("lines")).toList();
        for (int i = 0; i < kLinesPerSign; ++i) {
            const QString s = (i < in.size()) ? in.at(i).toString()
                                              : QString(); // 旧行缺 → 空串（向前兼容）
            lines[size_t(i)] = s.left(kCharsPerLine);
        }
        m_signs[key(x, y, z)] = std::move(lines);
    }
    ++m_revision;
    emit signChanged();
}

// 坐标键 "x,y,z"（ChestStore::key 同款——QString 键简单可读、无位打包范围限制；牌子数少性能非热点）。
QString SignStore::key(int x, int y, int z)
{
    return QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z);
}

// 反解 "x,y,z" → x,y,z（坐标可负；格式不符 → false。ChestStore::parseKey 同款逗号三段式）。
bool SignStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool okx = false, oky = false, okz = false;
    x = parts[0].toInt(&okx);
    y = parts[1].toInt(&oky);
    z = parts[2].toInt(&okz);
    return okx && oky && okz;
}
