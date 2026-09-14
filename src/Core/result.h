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

#include <utility> // std::move

struct Error
{
    int code = 0;                  // 0 = Ok；非零 = 错误码（分域编码 R20.06+ 再定，全域通用暂存）
    const char *message = nullptr; // 静态诊断串（static 存储期；可空）
};

// 错误谓词（Result 之外也可独立判裸 Error）。
inline bool isError(const Error &e) { return e.code != 0; }

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

#endif // RESULT_H
