#ifndef CHUNKLIFECYCLE_H
#define CHUNKLIFECYCLE_H

#include <QtGlobal> // quint8

// R20.10 Chunk lifecycle（refactor-plan §29.3 R20.10「先实现 Absent、Loading、Generated、
// Active、Loaded、Evicting」）：chunk 生命周期的**六态类型 + 转移表单一权威**（header-only，
// 非 QObject 无 AUTOMOC——同 editbuffer/mathtypes 的 Core 叶子形态，但属 World 层）。
//
// ── 六态转移图（合法边恰 8 条；其余 28 个 from→to 组合一律非法）────────────────────────
//
//       requestLoad           generateDone            promoteToResident
//  Absent ────────────▶ Loading ────────────▶ Generated ──────────────▶ Loaded
//     ▲                                                                  ▲  │
//     │ finishEvict                                    cancelEvict ──────┘  │ activate /
//     │                                                                    │ deactivate
//  Evicting ◀──────────────── requestEvict ──────────────────────────────── Loaded
//                 (Loaded 是驻留枢纽：激活/驱逐/取消驱逐都经它)
//
//   边清单（编号即本头 switch 的分派序）：
//     ① Absent    → Loading    （请求加载/生成——R20.11 GenerationJob 的将来挂点）
//     ② Loading   → Generated  （内容生成完毕）
//     ③ Generated → Loaded     （晋升驻留：可查询、可建 mesh）
//     ④ Loaded    → Active     （激活进玩家活跃域使用）
//     ⑤ Active    → Loaded     （去激活，仍驻留）
//     ⑥ Loaded    → Evicting   （选中驱逐）
//     ⑦ Evicting  → Absent     （驱逐完成）
//     ⑧ Evicting  → Loaded     （驱逐取消——驱逐途中又被需要）
//
// ── 态语义 ──────────────────────────────────────────────────────────────────────
//   Absent    槽位无可用内容（流式世界里未生成/未读回；本单最小解释下 Chunk 对象仍在内存，
//             见 chunkmanager.h 选型注释——「不落盘驱逐」是登记的后续单）。
//   Loading   加载/生成进行中（内容尚不可查询）。
//   Generated 内容已生成但未晋升驻留（mesh 未建、门查询不可见）。
//   Active    驻留 + 活跃使用（门查询可见；将来 scheduler 的活跃域态）。
//   Loaded    驻留已加载（门查询可见；**默认稳态**——等价 R20.10 之前「全部 chunk 常驻已加载」
//             的现行行为，plan 验收第一条「当前固定 10×10 世界仍可运行」的零变化基线）。
//   Evicting  已选中驱逐、尚未离开（可被取消回 Loaded）。
//
// ── 门查询谓词（WorldFacade 三门的存在面消费）──────────────────────────────────
//   chunkLifecycleQueryable(s) = s ∈ {Loaded, Active}：只有驻留两态对 mesher 门查询可见；
//   Loading/Generated（内容在途未晋升）与 Absent/Evicting（离开中/已离开）一律不可见。
//   默认稳态全 chunk = Loaded → 三门与 R20.08 行为逐位一致（r2008 全绿 + r2010b 实证）；
//   卸载（⑥⑦）后三门恒 false、重载（①②③）后恢复（r2010c 端到端）。
enum class ChunkLifecycle : quint8
{
    Absent = 0,
    Loading,
    Generated,
    Active,
    Loaded,
    Evicting,
    Count // 哨兵（非状态）：枚举域上界，探针遍历用；插值/重排 = 序列化无关但转移表契约，勿动
};

// 态名（腿 diag 判别信息用；与枚举声明序逐一对应）。
inline const char *chunkLifecycleName(ChunkLifecycle s)
{
    switch (s) {
    case ChunkLifecycle::Absent: return "Absent";
    case ChunkLifecycle::Loading: return "Loading";
    case ChunkLifecycle::Generated: return "Generated";
    case ChunkLifecycle::Active: return "Active";
    case ChunkLifecycle::Loaded: return "Loaded";
    case ChunkLifecycle::Evicting: return "Evicting";
    case ChunkLifecycle::Count: break;
    }
    return "Count";
}

// 转移合法性判定（单一权威——ChunkManager::setLifecycle 的守卫谓词 + r2010a 全图腿的被测面）。
// 自转移（X→X）非法：转移必须经显式事件驱动，no-op 由调用方自行短路。
inline bool chunkLifecycleTransitionLegal(ChunkLifecycle from, ChunkLifecycle to)
{
    switch (from) {
    case ChunkLifecycle::Absent: return to == ChunkLifecycle::Loading; // ①
    case ChunkLifecycle::Loading: return to == ChunkLifecycle::Generated; // ②
    case ChunkLifecycle::Generated: return to == ChunkLifecycle::Loaded; // ③
    case ChunkLifecycle::Loaded:
        return to == ChunkLifecycle::Active // ④
            || to == ChunkLifecycle::Evicting; // ⑥
    case ChunkLifecycle::Active: return to == ChunkLifecycle::Loaded; // ⑤
    case ChunkLifecycle::Evicting:
        return to == ChunkLifecycle::Absent // ⑦
            || to == ChunkLifecycle::Loaded; // ⑧
    case ChunkLifecycle::Count: break; // 哨兵非状态：任何转移都非法
    }
    return false;
}

// 门查询谓词（见类头注释「门查询谓词」节）：驻留两态对 mesher 三门可见。
inline bool chunkLifecycleQueryable(ChunkLifecycle s)
{
    return s == ChunkLifecycle::Loaded || s == ChunkLifecycle::Active;
}

#endif // CHUNKLIFECYCLE_H
