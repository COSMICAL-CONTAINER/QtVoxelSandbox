#ifndef LOOTTABLE_H
#define LOOTTABLE_H

#include <QtGlobal> // quint32

#include <array>
#include <vector>

// 战利品表（LootTable，t393）。Game 层纯静态数据 + 纯函数（无 Q_OBJECT / 无实例 / 无 World 依赖）。
//
// 机制等价 MC 1.0 loot table：一张「条目池（itemId + 权重 + 数量区间）」+ 一个「按权重随机抽取 N 次」的
// 抽取算法。同一张表可被多个内容源复用 —— 本任务供地牢箱子首开填充（ChestStore::populateDungeonLoot），
// 并预留给 t401 钓鱼（渔获 = 另一张表 / 或复用本表）。**单一权威**（PLAN §2）：物品 id 引用 recipe.h 材料段
// 常量（CoalId / RedstoneId / ...），不另存物品表；条目定义只在此处（改一处全工程生效）。
//
// 抽取语义（机制对齐 MC 1.0 dungeon chest）：
//   - 每次 roll 从池中按 weight **有放回**加权抽一条；该条产出 count = [minCount, maxCount] 均匀随机。
//   - 连续 rolls 次 → 最多 rolls 个 stack（同 id 多次命中保持独立 stack，由 caller 决定是否合并入同槽；
//     MC dungeon chest 也是分散入不同槽，故本表不做合并）。
//   - RNG 由 caller 注入（quint32 seed 重载用 QRandomGenerator(seed) 确定性；PLAN §2-K 精神：同坐标箱子 →
//     同 seed → 同战利品，世界重生成可复现）。钓鱼可注入运行期 RNG（每次抛竿不同）。
//
// 分层（PLAN §2）：本层属 Game，只依赖 QtCore（QRandomGenerator）+ 同层 recipe.h（材料段 id 常量），
// **不**依赖 Renderer/Physics/World/QtQuick3D。依赖只向下。纯函数无副作用；实际「写入箱子槽」由
// ChestStore（同层 ViewModel）在 caller 处执行。
//
// §4 法律 + §9：条目物品名用通用词（煤 / 红石 / 面包 / 线 / 铁锭 / 马鞍 / 命名牌 / 附魔书）；零 MC 专名。
class LootTable
{
public:
    // 一条战利品条目：物品 id + 权重 + 单次命中产出数量区间 [minCount, maxCount]（含两端）。
    struct Entry {
        int itemId;
        int weight;
        int minCount;
        int maxCount;
    };
    // 抽取产物：一个物品栈（id + count）。同 id 多次命中产出多个独立 Stack（caller 决定合并 / 分槽）。
    struct Stack {
        int itemId;
        int count;
    };

    // 地牢箱子战利品池（t393 主交付）。8 条：煤 / 红石 / 面包 / 线 / 铁锭（常见，高权重）+
    //   马鞍 / 命名牌（稀有，低权重）+ 附魔书占位（极稀有）。机制对齐 MC 1.0 dungeon chest loot 的
    //   「常见材料多 / 稀有件少」分布。static 存储（返回 const 引用，调用方不持副本）。
    static const std::vector<Entry> &dungeonChestPool();

    // t401 钓鱼获物池（生鱼 / 垃圾 / 宝藏三档；机制对齐 MC 1.0 fishing loot「鱼常见 / 垃圾次之 / 宝藏稀有」）。
    //   生鱼（RawFishId，高权重）+ 垃圾（皮革 / 线 / 骨头 / 腐肉 / 木棒 / 墨囊，中权重）+ 宝藏（马鞍 / 命名牌 /
    //   钻石，低权重）。复用 roll（按 weight 抽一次 → 一件获物）；钓竿拉起时 PlayerController 注入运行期 RNG
    //   （每次抛竿 seed 不同 → 获物随机）。static 存储（返回 const 引用，调用方不持副本）。
    static const std::vector<Entry> &fishingPool();

    // 地牢箱子单次开启的抽取次数（机制等价 MC 1.0 dungeon chest ~8 stacks；27 槽绰绰有余）。
    static constexpr int kDungeonRolls = 8;

    // 按 weight 有放回加权抽 rolls 次，返回最多 rolls 个 Stack。RNG 由 caller 注入（确定性 seed 重载见下）。
    //   pool 为空 / rolls<=0 → 空 vector。weight<=0 的条目跳过（不入总权重）。maxCount<minCount → 取 minCount。
    static std::vector<Stack> roll(const std::vector<Entry> &pool, int rolls, quint32 seed);
};

#endif // LOOTTABLE_H
