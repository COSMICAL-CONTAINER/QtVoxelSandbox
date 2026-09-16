#ifndef GENERATIONPOLICY_H
#define GENERATIONPOLICY_H

// §29.4-P1 流式激活·策略层骨架 / GenerationPolicy（refactor-plan §29.4「P1 策略层骨架：
// GenerationPolicy 组件（Absent-only 决策：哪些 chunk 该请求/该卸载）——固定世界默认关
// （零变化不变量延续），参数开启才激活；纯决策无头可测」）。header-only 非 QObject 纯值
// 组件（chunklifecycle.h/generationjob.h 同形态，World 层无 AUTOMOC）。
//
// ── 语义依据（§29.4 决策表，用户定向 2026-09-16「D1-D6 均取建议项」）──────────────────
//   D1 无限世界：本组件参数化承载——固定世界 = streamingEnabled=false 的结构性特例（D2
//     「新世界参数开关」的参数面），开启才产出决策；
//   D4 生成/渲染分离双参数：generationRadiusChunks ≥ renderRadiusChunks（不变量，构造处
//     归一化强制，见下方「归一化选型」）——toEvict 只看生成半径（渲染半径单独调小不改变
//     驱逐候选），[render, gen] 环带驻留格**不驱逐**（渲染余量语义）；
//   D5 卸载策略（Evicting→Absent + Edits-on-evict 落盘）：本组件只产出**驱逐候选单**
//     （toEvict），不执行任何状态转移/落盘——执行器与落盘回灌是 P3 接线（登记非目标）。
//
// ── 决策面（decide：纯函数，同输入逐位同输出，零隐状态）──────────────────────────────
//   decide(playerCx, playerCz, querySeam) → GenerationPolicyDecision 值：
//   · querySeam = std::function<ChunkLifecycle(int cx, int cz)>：**纯缝**——零 World*/
//     Chunk*/QObject* 指针（R20.08 facade 纪律）、零线程原语（R20.12 t1023c 白名单纪律，
//     本组件纯同步无锁）。缝不可达的坐标由缝实现自答（ChunkManager::lifecycleAt 越界即
//     Absent 的同款语义——「无可用内容」）；null 缝 = 无信息 → 零决策（fail-safe，恒空）。
//   · 距离度量 = **切比雪夫距离** max(|dcx|,|dcz|)（头注释立此存照选型）：chunk 流式请求
//     域是方窗（(2r+1)² 网格），方格距离纯整数比较——全序确定零浮点，与 ChunkManager 方形
//     网格域同构；欧氏需 sqrt/浮点，确定性比较面劣化，不取。
//   · toRequest：生成半径**内（含边界，距离 ≤ generationRadius）且状态 == Absent** 的
//     chunk，逐项 {cx, cz, priority = 距离秩 = 切比雪夫距离值}——priority 升序（0 = 玩家
//     所在格最紧急，与 GenerationRequest::priority「值小紧急」同门）、同距按 (cx, cz) 字
//     典序——输出全确定性。非 Absent 五态（Generated/Loaded/Active/Loading/Evicting）一律
//     不请求（Absent-only；r2011 头注释登记的「真实流式世界里 submit 侧必须只对 Absent
//     chunk 提交」策略面即本单收口）。
//   · toEvict：**已驻留三态**（Generated/Loaded/Active）且距离 **> generationRadius** 的
//     chunk，逐项 {cx, cz}，按距离**降序**（最远先卸）、同距 (cx, cz) 字典序。
//     Absent/Loading/Evicting 恒不入 toEvict（不在驻留稳态 / 已在离场途中，重复驱逐无义）。
//   · streamingEnabled == false → **两列表恒空**（任何玩家位、任何缝、任何参数）——§29.4
//     「固定世界默认关」：固定 48×48 世界全 Loaded 稳态零变化的结构性事实由本短路 + P1
//     零调用点迁移（r2011 先例：零接线 = 零变化最强形态）双保险承重。
//   · 缝返回六态域外的值（≥ ChunkLifecycle::Count）按「不请求不驱逐」忽略（防御性：六态
//     权威外无语义，不参与任何列表）。
//
// ── 扫描域契约（scanExtentChunks——纯点查缝的枚举边界，头注释立此存照）────────────────
//   缝只能点查、不能枚举，toRequest/toEvict 的候选域必须显式有界：decide 在以玩家为中心、
//   半宽 scanExtentChunks 的方窗内逐格点查（(2·scanExtent+1)² 次）。不变量 scanExtent ≥
//   generationRadius（归一化强制）——请求域天然在窗内；驱逐域（距离 > gen）由窗宽给出上界，
//   窗外即策略盲区（D1 无限世界下 P3 驱逐执行器换缝/换窗是接线期决策，本组件只定契约）。
//   scanExtent 是 P1 为纯函数可测性设的第四参数：前三参数（streamingEnabled + D4 双半径）
//   是 §29.4 决策表的产品面，scanExtent 只影响候选枚举边界、不改变任何阈值语义。
//
// ── 归一化选型（构造处强制，越界取值归一化——不抛错不钳断言，立此存照）────────────────
//   负半径 → 0；generationRadius > 255 → 255（priority quint8 域上界，与
//   GenerationRequest::priority 同域——全部半径类量统一 0..255 域）；renderRadius >
//   generationRadius → 收敛到 generationRadius（D4 不变量 gen ≥ render）；scanExtent 规整到
//   [generationRadius, 255]（下界盖住请求域，上界同域钳制防无界扫描窗）。静默规整的选型理
//   由：策略参数来自调用方配置面（P2 起接线），越界取值属配置病而非运行时错——规整 + 上游
//   可读回 params() 自检，好于抛错打断流式主循环。
//
// ── 接线登记（后续单，本单零做）───────────────────────────────────────────────────────
//   P2 玩家移动驱动：位置沿 → decide(...) 的 toRequest 逐项 submit（GenerationJob 三 kind
//     已含优先级；请求模型即 r2011 面）；P3 卸载 + Edits-on-evict：toEvict 逐项驱动
//     Loaded→Evicting→Absent（⑥⑦）+ 改动块经 SaveCoordinator 落盘回灌（R20.15 载体）。
//   P1 本单**零调用点迁移**：World/GameSession/QML 零触碰（r2011 先例：生产路径零接线 =
//     固定世界行为零变化的最强形态）。
//
// ── QML / 线程 / 分层面 ───────────────────────────────────────────────────────────────
//   非 QObject、无 Q_INVOKABLE / Q_PROPERTY（生命周期与调度决策不进 QML，R20 主线「QML 零
//   迁移」不变量）；零线程原语（t1023c 白名单纪律——src/ 受认可落点仅 backgroundgeneration.h）；
//   分层：World 层 header-only 值组件，向下只依赖 Core 叶子（result）+ chunklifecycle.h，
//   不依赖 Renderer/Game/Entities/QML。
//
// ── 值纪律（R20.05/06/13 先例）──────────────────────────────────────────────────────
//   Params / 条目 / Decision 为纯值类型：QObjectFree 编译期钉；条目类型另钉 trivially
//   copyable（安全值传）；Decision 持 QVector（owning 容器，非 trivially copyable——
//   ChunkMeshData 先例的同款分化登记：消息面小值钉平凡性，输出面容器只钉 QObjectFree）。

#include <QtGlobal> // quint8

#include <QVector>
#include <algorithm>     // std::max / std::min / std::sort
#include <functional>    // std::function（纯缝类型）
#include <type_traits>   // std::is_trivially_copyable_v（值纪律钉）

#include "chunklifecycle.h" // ChunkLifecycle 六态 + 转移权威（决策谓词消费）
#include "result.h"         // QObjectFree（值纪律编译期钉）

// ── GenerationPolicyParams：策略参数（D2 参数开关 + D4 双半径 + 扫描域契约）─────────────
struct GenerationPolicyParams
{
    // §29.4「固定世界默认关」：默认 false 时 decide() 恒空（零变化不变量的参数面）。
    bool streamingEnabled = false;
    int generationRadiusChunks = 4; // 生成半径（D4；≥ 渲染半径；含边界闭域）
    int renderRadiusChunks = 4;     // 渲染半径（D4；≤ 生成半径；只作环带下界语义，不改 toEvict）
    int scanExtentChunks = 6;       // 缝扫描方窗半宽（≥ 生成半径；候选枚举边界，见头注契约）

    GenerationPolicyParams() = default;
    // 规整构造（不变量在构造处强制——负值取 0、gen 钳 255、render ≤ gen、scan ∈ [gen, 255]）。
    // r2017（agent-review-2026-09-16 Info「setParams 不再规整」）起规整唯一路径 = 本类型
    //   public static normalized(...)：构造与 GenerationPolicy::setParams 同走一路（此前规
    //   整只在构造、setParams 裸赋值可破不变量——gen>255 时 decide 的 priority=quint8(cheb)
    //   回绕）。r2017d 双生钉。
    GenerationPolicyParams(bool streaming, int genR, int renderR, int scanR)
    {
        *this = normalized(streaming, genR, renderR, scanR);
    }

    // r2017 规整唯一路径（头注「归一化选型」的代码收口，构造与 setParams 共享；落位在
    // Params 本体 = 规整即该值类型的构造契约）：负半径→0、gen 钳 255（priority quint8 域
    // 上界，与 GenerationRequest::priority 同域）、render ≤ gen 收敛（D4 不变量）、
    // scan ∈ [gen, 255]。静默规整选型依据见文件头注（配置病非运行时错）。
    static GenerationPolicyParams
    normalized(bool streaming, int genR, int renderR, int scanR)
    {
        GenerationPolicyParams p;
        p.streamingEnabled = streaming;
        p.generationRadiusChunks = std::min(255, std::max(0, genR));
        p.renderRadiusChunks = std::min(p.generationRadiusChunks, std::max(0, renderR));
        p.scanExtentChunks = std::min(255, std::max(p.generationRadiusChunks, std::max(0, scanR)));
        return p;
    }
};

// ── 请求/驱逐条目（纯值；{cx, cz} 网格坐标，负坐标同等合法）────────────────────────────
struct GenerationPolicyRequest
{
    int cx = 0;
    int cz = 0;
    quint8 priority = 0; // 距离秩 = 切比雪夫距离值（0 最紧急，升序执行；quint8 同 GenerationRequest 域）

    friend bool operator==(const GenerationPolicyRequest &a, const GenerationPolicyRequest &b)
    {
        return a.cx == b.cx && a.cz == b.cz && a.priority == b.priority;
    }
    friend bool operator!=(const GenerationPolicyRequest &a, const GenerationPolicyRequest &b)
    {
        return !(a == b);
    }
};
struct GenerationPolicyEvict
{
    int cx = 0;
    int cz = 0;

    friend bool operator==(const GenerationPolicyEvict &a, const GenerationPolicyEvict &b)
    {
        return a.cx == b.cx && a.cz == b.cz;
    }
    friend bool operator!=(const GenerationPolicyEvict &a, const GenerationPolicyEvict &b)
    {
        return !(a == b);
    }
};

// ── GenerationPolicyDecision：一次决策的两张单（纯值；空 = 无事可做）────────────────────
struct GenerationPolicyDecision
{
    QVector<GenerationPolicyRequest> toRequest; // 优先级升序、同距 (cx,cz) 字典序
    QVector<GenerationPolicyEvict> toEvict;     // 距离降序（最远先卸）、同距 (cx,cz) 字典序

    friend bool operator==(const GenerationPolicyDecision &a, const GenerationPolicyDecision &b)
    {
        return a.toRequest == b.toRequest && a.toEvict == b.toEvict;
    }
    friend bool operator!=(const GenerationPolicyDecision &a, const GenerationPolicyDecision &b)
    {
        return !(a == b);
    }
};

// 值纪律编译期钉（R20.05/06/13 先例——result.h QObjectFree 概念 + 条目平凡性）：
static_assert(QObjectFree<GenerationPolicyParams>, "GenerationPolicyParams must be QObject-free (plan §29.4 P1)");
static_assert(QObjectFree<GenerationPolicyRequest>, "GenerationPolicyRequest must be QObject-free (plan §29.4 P1)");
static_assert(QObjectFree<GenerationPolicyEvict>, "GenerationPolicyEvict must be QObject-free (plan §29.4 P1)");
static_assert(QObjectFree<GenerationPolicyDecision>, "GenerationPolicyDecision must be QObject-free (plan §29.4 P1)");
static_assert(std::is_trivially_copyable_v<GenerationPolicyRequest>,
              "GenerationPolicyRequest stays trivially copyable (value-pass discipline)");
static_assert(std::is_trivially_copyable_v<GenerationPolicyEvict>,
              "GenerationPolicyEvict stays trivially copyable (value-pass discipline)");

// ── GenerationPolicy：Absent-only 决策器（纯函数面；同参数同输入 → 逐位同输出）───────────
class GenerationPolicy
{
public:
    // 纯缝类型：给定 chunk 网格坐标回六态（零 World*/Chunk*/QObject* 指针，见头注）。
    using ChunkSeamFn = std::function<ChunkLifecycle(int cx, int cz)>;

    GenerationPolicy() = default;
    explicit GenerationPolicy(const GenerationPolicyParams &params) : m_params(params) {}

    const GenerationPolicyParams &params() const { return m_params; }
    // r2017：与构造同走规整唯一路径 GenerationPolicyParams::normalized(...)（合规入参逐位
    // 恒等——r2017d 合规面回归柱；越界入参规整 ≡ 构造规整，杜绝「手改字段破不变量」面）。
    void setParams(const GenerationPolicyParams &p)
    {
        m_params = GenerationPolicyParams::normalized(p.streamingEnabled,
            p.generationRadiusChunks, p.renderRadiusChunks, p.scanExtentChunks);
    }

    // 一次流式决策（见类头注释「决策面」——排序契约 / 默认关短路 / null 缝 fail-safe）。
    GenerationPolicyDecision decide(int playerCx, int playerCz, const ChunkSeamFn &querySeam) const
    {
        GenerationPolicyDecision d;
        // 默认关短路（§29.4「固定世界默认关」）：disabled 恒空——固定世界零变化的参数面。
        if (!m_params.streamingEnabled)
            return d;
        if (!querySeam)
            return d; // null 缝 = 无信息 → 零决策（fail-safe，头注「纯缝」节）

        const int gen = m_params.generationRadiusChunks;
        const int scan = m_params.scanExtentChunks;
        // 切比雪夫距离（头注「决策面」选型）——纯整数全序，零浮点。
        const auto cheb = [](int dx, int dz) {
            return std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
        };

        // 请求面：切比雪夫距离 ≤ gen（含边界）且 Absent。
        for (int dz = -gen; dz <= gen; ++dz) {
            for (int dx = -gen; dx <= gen; ++dx) {
                const int cx = playerCx + dx, cz = playerCz + dz;
                if (querySeam(cx, cz) != ChunkLifecycle::Absent)
                    continue; // Absent-only：非 Absent 五态一律不请求
                GenerationPolicyRequest r;
                r.cx = cx;
                r.cz = cz;
                r.priority = quint8(cheb(dx, dz)); // 距离秩
                d.toRequest.append(r);
            }
        }
        // 驱逐面：扫描窗内、距离 > gen、驻留三态。
        for (int dz = -scan; dz <= scan; ++dz) {
            for (int dx = -scan; dx <= scan; ++dx) {
                if (cheb(dx, dz) <= gen)
                    continue; // 半径守卫：gen 半径内与 [render, gen] 环带永不驱逐（D4）
                const ChunkLifecycle s = querySeam(playerCx + dx, playerCz + dz);
                const bool resident = s == ChunkLifecycle::Generated || s == ChunkLifecycle::Loaded
                    || s == ChunkLifecycle::Active;
                if (!resident)
                    continue; // Absent/Loading/Evicting（及域外值）恒不入 toEvict
                GenerationPolicyEvict e;
                e.cx = playerCx + dx;
                e.cz = playerCz + dz;
                d.toEvict.append(e);
            }
        }
        // 排序：请求 (priority, cx, cz) 升序；驱逐 (距离降序 = 最远先卸, cx, cz 字典序)——
        // 距离在比较器内由坐标重算（纯函数：排序键不入条目，全序确定性）。
        std::sort(d.toRequest.begin(), d.toRequest.end(),
                  [](const GenerationPolicyRequest &a, const GenerationPolicyRequest &b) {
                      if (a.priority != b.priority)
                          return a.priority < b.priority;
                      if (a.cx != b.cx)
                          return a.cx < b.cx;
                      return a.cz < b.cz;
                  });
        const int px = playerCx, pz = playerCz;
        std::sort(d.toEvict.begin(), d.toEvict.end(),
                  [px, pz, &cheb](const GenerationPolicyEvict &a, const GenerationPolicyEvict &b) {
                      const int da = cheb(a.cx - px, a.cz - pz);
                      const int db = cheb(b.cx - px, b.cz - pz);
                      if (da != db)
                          return da > db; // 最远先卸
                      if (a.cx != b.cx)
                          return a.cx < b.cx;
                      return a.cz < b.cz;
                  });
        return d;
    }

private:
    GenerationPolicyParams m_params; // 唯一状态（规整唯一路径保证不变量）；decide 不改它
};

#endif // GENERATIONPOLICY_H
