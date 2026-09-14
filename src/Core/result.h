#ifndef RESULT_H
#define RESULT_H

// R20.05 基础错误类型（refactor-plan §5.1 voxel_base「Result / Error」+ §29.3 R20.05
// 「基础错误类型」）：最小雏形，**只立类型不迁移调用点**。现状：World 写族以 bool 返回 +
// 静默拒绝表意（无错误码 / 无诊断），本类型为 R20.06（Command/Event/Snapshot 立类型）把
// 失败面从 bool 升维到 Error 提供落脚点；本单零调用点迁移，行为面零变化。
// 形态刻意最小：
//   Error       { code, message }——code==0 即无错误；message 指静态字面量（可空）。
//   Result<T>   值或错误二选一；Result<void> 特化承无值成功面。
//   两者均为平凡可拷贝聚合风格的值类型，无 QObject / 无信号 / 无堆分配，World 层规则与
//   未来跨线程事件链（plan §29.4「Event 不携带 QObject」）均可安全携带。
// 约定：T 须可默认构造（fail 路径落 T{} 哨兵值——消费方必须先 isOk() 再取值）。

#include <QObject>     // QObjectFree 约束（R20.06）：is_base_of 需完整类型
#include <type_traits> // QObjectFree：is_base_of_v / remove_cv_t / remove_reference_t
#include <utility>     // std::move

struct Error
{
    int code = 0;                  // 0 = Ok；非零 = 错误码（分域见下方百位段约定）
    const char *message = nullptr; // 静态诊断串（static 存储期；可空）
};

// 错误谓词（Result 之外也可独立判裸 Error）。
inline bool isError(const Error &e) { return e.code != 0; }

// ── 错误码分域（头注预留位自 R20.06 起收口：百位段 = 域，域内递增）──────────────────
constexpr int kErrQueueFull = 101; // 队列域：容量上界满载（push 拒绝；不覆盖 / 不静默挤出）

template <typename T>
class Result
{
public:
    static Result ok(T value) { return Result(std::move(value), Error{}); }
    static Result fail(int code, const char *message = nullptr)
    {
        return Result(T{}, Error{ code, message });
    }
    static Result fail(Error e) { return Result(T{}, std::move(e)); }

    bool isOk() const { return !isError(m_error); }
    explicit operator bool() const { return isOk(); }
    const Error &error() const { return m_error; }
    const T &value() const { return m_value; }

private:
    Result(T v, Error e) : m_value(std::move(v)), m_error(std::move(e)) {}
    T m_value{};     // fail 路径为哨兵默认值（仅 isOk() 为真时语义有效）
    Error m_error{};
};

// 无值成功面特化（动作型结果：只报成败与错误）。
template <>
class Result<void>
{
public:
    static Result ok() { return Result(Error{}); }
    static Result fail(int code, const char *message = nullptr)
    {
        return Result(Error{ code, message });
    }
    static Result fail(Error e) { return Result(std::move(e)); }

    bool isOk() const { return !isError(m_error); }
    explicit operator bool() const { return isOk(); }
    const Error &error() const { return m_error; }

private:
    explicit Result(Error e) : m_error(std::move(e)) {}
    Error m_error{};
};

// ── QObjectFree：「不携带 QObject」值纪律的编译期约束（R20.06 Command/Event/Snapshot）──
// plan §4.2「Snapshot…不携带 QObject」+ §29.3 R20.06「Event 不携带 QObject」的机制化：
// 类型一旦直接或经成员成为 QObject 派生（携带 QObject 子对象），is_base_of 即真 → 概念
// 不满足 → 各类型头内 static_assert 编译失败（「摘即红」在类型层成立：给 Event 加
// QObject 派生成员 = 构建即红，无需运行轮）。边界登记：QObject **裸指针**成员不经本约束
// 覆盖（指针不构成子对象）——值语义体系刻意全值成员，指针面由评审把关；本约束钉子对象。
template <typename T>
concept QObjectFree = !std::is_base_of_v<QObject, std::remove_cv_t<std::remove_reference_t<T>>>;

#endif // RESULT_H
