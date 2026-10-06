#include "mapstore.h"

#include <limits>

// map 地图数据存储实现（t1114 初建；t1132 扩展域 + 持久化首片——设计裁定与存档口径见 mapstore.h
// 头注，本文件只做像素簿记 + 数据集出入库）。
MapStore *MapStore::s_active = nullptr;

MapStore::MapStore(QObject *parent)
    : QObject(parent)
{
    s_active = this; // 图像 provider 拉取面注册（析构对称注销，防悬垂）
}

MapStore::~MapStore()
{
    if (s_active == this)
        s_active = nullptr;
}

// [t1139 R01 → 本单算式收口] 数据集字节数单一权威算式（除法形式预算检查；头注契约见
//   mapstore.h 同名段）。(核心域边长 + 2×前沿带)² × 4 字节/格——loadVariant 预检与 initialize
//   上界守卫共用，禁任何第二份 int 域同式算术（溢出面只许存在这一处）。旧形 = qsizetype 域
//   先乘后比：双轴近 INT_MAX 时乘积数学值 ≈1.85e19 越过有符号 64 位上限，溢出（UB）发生在与
//   预算线比较**之前**；且存在乘积回绕为负 / 零的入参带（数据集边长恰 2³¹：w·d = 2⁶²，×4 恰
//   回绕 0）——回绕值会被误判「低于预算」放行，把溢出负值当安全字号直送 resize。新形收口：
//   ① 非正域 = 有界拒绝哨兵（有效性语义并入返回值：返回值 ≤ kMaxDatasetBytes 即有界可建库
//      ——「bool 有效性 + 字节数」的等价单返回值编码，禁零 / 负值混入预算比较域）；
//   ② 除法判式逐步检查（禁先乘后比）：数据集边长须落 int 正域（initialize 的 m_width/m_depth
//      面）且 w ≤ ⌊预算格数/d⌋ ⇔ w·d ≤ 预算格数（整除向下取整保真；d ≥ 257 恒非零除）——
//      乘法只发生在下方已验证有界域（≤ 128MB/4 格 × 4 字节 = ≤ 预算线），任何 int 入参组合
//      零溢出面，负值 / 回绕值在调用侧不可达。
qsizetype MapStore::datasetBytesFor(int worldWidth, int worldDepth)
{
    if (worldWidth <= 0 || worldDepth <= 0)
        return kMaxDatasetBytes + 1; // ① 非正域 = 有界拒绝哨兵（禁零/负混入预算比较）
    const qsizetype w = qsizetype(worldWidth) + 2 * kDomainMargin;
    const qsizetype d = qsizetype(worldDepth) + 2 * kDomainMargin;
    constexpr qsizetype kCellBudget = kMaxDatasetBytes / 4;
    const bool bounded = w <= qsizetype(std::numeric_limits<int>::max())
        && d <= qsizetype(std::numeric_limits<int>::max())
        && w <= kCellBudget / d; // ② 除法判式（w·d ≤ 预算/4 格——禁先乘后比；d ≥ 257 恒非零除）
    if (!bounded) {
        return kMaxDatasetBytes + 1; // 非法/越界哨兵（恒真超线——调用侧比较形零改动）
    }
    return w * d * 4; // 有界乘法（≤ kMaxDatasetBytes——负值/回绕值在此不可达）
}

void MapStore::initialize(int width, int depth)
{
    if (width <= 0 || depth <= 0)
        return; // 非法定版 no-op（调用侧传 World 尺寸，恒正；防御负例）
    // [t1139 R01] 域上界守卫（有界拒绝）：w/d 自盘面元数据路径可达本算术（loadVariant 入参
    //   自存档行）——超预算 = 有界拒绝进 clearAll 降级面（与账不平同门口径），禁 int 溢出
    //   分配。预检一律经 datasetBytesFor（除法形式预算检查）比对，禁回退 int 域算术。
    if (datasetBytesFor(width, depth) > kMaxDatasetBytes) {
        clearAll();
        return;
    }
    m_worldWidth = width;
    m_worldDepth = depth;
    // 数据集 = 世界核心域 + 四侧探索前沿带（t1132 MAP-02——era 128 画布封顶的有界对应面，禁无限
    //   位图）。全图未探索底色（quint32 逐格填充——QByteArray 单字节 fill 无法表达 4 字节色；resize
    //   后逐格覆写，初值无关紧要）。
    m_width = width + 2 * kDomainMargin;
    m_depth = depth + 2 * kDomainMargin;
    // 字节数从已验证单一权威直推（预算线已过 → w·d ≤ 预算/4 格——int 域 m_width·m_depth·4
    //   乘法退役）；格数从已验证缓冲尺寸直推 = 零二次同式算术（溢出面只许存在 datasetBytesFor
    //   一处）。下界钳 0 = 异常返回值防御面（守卫已过的正常路径恒正有界，字面零行为差）。
    m_pixels.resize(qBound<qsizetype>(0, datasetBytesFor(width, depth), kMaxDatasetBytes));
    auto *px = reinterpret_cast<quint32 *>(m_pixels.data());
    const qsizetype cells = m_pixels.size() / 4;
    for (qsizetype i = 0; i < cells; ++i)
        px[i] = kUnexploredColor;
    ++m_revision;
    emit mapChanged();
}

void MapStore::writeColumn(int x, int z, quint32 argb)
{
    if (!hasMap())
        return; // 未建库 no-op（探索盒过界列由 caller 跳过，本侧双保险）
    // 世界坐标 → 数据集下标（+kDomainMargin 平移；带外列 no-op——负向越带 / 正向越带同拒）。
    const int cx = x + kDomainMargin;
    const int cz = z + kDomainMargin;
    if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_depth)
        return;
    reinterpret_cast<quint32 *>(m_pixels.data())[size_t(cz) * size_t(m_width) + size_t(cx)] = argb;
}

void MapStore::commitColumns()
{
    if (!hasMap())
        return; // 未建库 no-op（无数据可提交）
    ++m_revision;
    emit mapChanged();
}

quint32 MapStore::columnColor(int x, int z) const
{
    if (!hasMap())
        return kUnexploredColor;
    const int cx = x + kDomainMargin;
    const int cz = z + kDomainMargin;
    if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_depth)
        return kUnexploredColor; // 带外读兜底（未探索底色语义一致）
    return reinterpret_cast<const quint32 *>(m_pixels.constData())[size_t(cz) * size_t(m_width) + size_t(cx)];
}

QImage MapStore::renderImage() const
{
    if (!hasMap())
        return QImage();
    // 常量数据包装（共享不拷）→ copy() 出独立副本（provider 侧生命周期解耦）。Format_ARGB32 与
    // m_pixels 的 quint32 ARGB 逐格同 layout，零转换。
    const QImage wrapped(reinterpret_cast<const uchar *>(m_pixels.constData()),
                         m_width, m_depth, qsizetype(m_width) * 4, QImage::Format_ARGB32);
    return wrapped.copy();
}

void MapStore::clearAll()
{
    if (!hasMap())
        return; // 未建库 no-op（不无故发信号，SignStore::clearAll 同门口径）
    m_worldWidth = 0;
    m_worldDepth = 0;
    m_width = 0;
    m_depth = 0;
    m_pixels.clear();
    ++m_revision;
    emit mapChanged();
}

// t1132 MAP-03 导出（形状 = loadVariant 入参 / map_dataset 表行；世界核心域尺寸 + revision +
//   全幅像素原样——表行无 id 无序号 = 共享口径单份，era per-map 偏离登记见 mapstore.h 头注）。
QVariantMap MapStore::exportVariant() const
{
    if (!hasMap())
        return {}; // 未建库 → 空 map（caller 表清空语义；旧 caller 零改动兼容面）
    QVariantMap v;
    v.insert(QStringLiteral("present"), true);
    v.insert(QStringLiteral("width"), m_worldWidth);
    v.insert(QStringLiteral("depth"), m_worldDepth);
    v.insert(QStringLiteral("revision"), m_revision);
    v.insert(QStringLiteral("pixels"), m_pixels);
    return v;
}

// t1132 MAP-03 装载（整体替换内存——signStore.loadAll 同门口径）：空 map（旧档无行 / 无表）→
//   clearAll 降级（会话口径重探索面）；有行 → initialize 后像素 BLOB 回填 + revision 复原。
//   尺寸账不平（pixels 字节数 ≠ 数据集字节数）= 存档病 → 诚实降级 clearAll（不载半截数据）。
// [t1139 F06] 坏元数据键（尺寸 / 版本键缺失或非法）旧面 = 裸 return **不清前世界内容**——同
//   实例先载 A 再载坏行 → A 像素滞留 + 下次保存把 A 探索写进 B（跨世界泄漏面，审查 A06）。
//   新面 = 与账不平路径同门口径：clearAll 降级 + **先校验后 initialize 原子替换**（初始化前
//   不发布半态）——尺寸 / 版本 / 上界三检全过后才建库，禁半初始化发布。
void MapStore::loadVariant(const QVariantMap &data)
{
    if (data.isEmpty() || !data.value(QStringLiteral("present")).toBool()) {
        clearAll(); // 旧档降级（未建库 no-op——不无故发信号）
        return;
    }
    bool okW = false, okD = false, okR = false;
    const int w = data.value(QStringLiteral("width")).toInt(&okW);
    const int d = data.value(QStringLiteral("depth")).toInt(&okD);
    const int rev = data.value(QStringLiteral("revision")).toInt(&okR);
    const QByteArray pixels = data.value(QStringLiteral("pixels")).toByteArray();
    const qsizetype want = datasetBytesFor(w, d);
    if (!okW || !okD || !okR || w <= 0 || d <= 0 || want > kMaxDatasetBytes) {
        clearAll(); // [t1139 F06] 尺寸/版本键病 + R01 上界越界 → 同账不平门口径降级（禁保留前世界）
        return;
    }
    // 本单收口：账不平先拒（先验账后建库）——像素 BLOB 尺寸核对全过才 initialize。旧序 =
    //   initialize 先发布 mapChanged（半初始化中间态已出）再验账、不平才 clearAll 补救——账不平
    //   路径由「建库 + 两信号补救面」收口为「零建库零半态面」；账平路径与 initialize 本体的
    //   mapChanged 语义零迁移。
    if (pixels.size() != want) { clearAll(); return; } // 账不平 = 存档病（尺寸换代/截断）→ 诚实降级，不载半截
    initialize(w, d); // 校验全过后才建库（原子替换——全图未探索底色 + revision 自增 + 单信号）
    m_pixels = pixels;
    m_revision = rev; // 复原存档版本号（下次 commitColumns 自增仍单调）
    emit mapChanged();
}
