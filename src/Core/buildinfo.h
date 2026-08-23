#ifndef BUILDINFO_H
#define BUILDINFO_H

#include <QObject>
#include <QString>
#include <QtQml/qqml.h>

// 构建版本戳（t813，R19.13 🅱 组全批最高优先）：把「本 exe 是何时、从哪个 commit 构建」
// 常驻可见 —— 主菜单角落小字 + F3 调试屏一行 + 启动日志一行。用户复测 bug 前先核版本，
// 终结「陈旧 exe 报已修 bug」的争议分流（历史 t740/t744/t792/t772/t805 六连疑）。
//
// 值来源：CMake 每次 build 经 cmake/WriteBuildStamp.cmake 生成 build/generated/build_stamps.h
//   （VOXEL_BUILD_STAMP = 构建时间 YYYY-MM-DD HH:MM；VOXEL_BUILD_GIT = git rev-parse --short HEAD）。
//   两个宏**只**被 src/Core/buildinfo.cpp 一个 TU include（其它代码只见本头的 QString API）→
//   stamp 变化仅重编该单 TU，不触发全量重编。
//
// 暴露（QML 单例，FrameProfiler 同款 QML_SINGLETON 模式）：
//   BuildInfo.stamp    — "2026-08-24 15:30"（构建时间）
//   BuildInfo.gitHash  — "c9119c3"（git 短哈希；git 缺失时 "nogit"）
//   BuildInfo.full     — "2026-08-24 15:30 @ c9119c3"（两处 UI 单行显示共用的组合串）
//
// 值是编译期常量 → 属性标 CONSTANT（无 NOTIFY；进程生命周期内恒不变，QML 绑定首求值即终值）。
//
// 分层（PLAN §2）：Core 叶子（只依赖 Qt Core/Qml，同 FrameProfiler），无向上依赖；
//   放最底层公共处，任意上层 / QML / 冒烟测试（redstone_matrix_test 直编源码）均可消费。
class BuildInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BuildInfo)
    QML_SINGLETON
    Q_PROPERTY(QString stamp READ stamp CONSTANT)
    Q_PROPERTY(QString gitHash READ gitHash CONSTANT)
    Q_PROPERTY(QString full READ full CONSTANT)
public:
    // QML 单例工厂（QML_SINGLETON 要求）：返回全局唯一实例（C++ / QML 共用同一对象，FrameProfiler 同款）。
    static BuildInfo *create(QQmlEngine *, QJSEngine *) { return instance(); }
    // C++ 全局访问点（main.cpp 启动日志 / redstone_matrix_test 探针经它读取）。
    static BuildInfo *instance();

    // 构建时间（YYYY-MM-DD HH:MM）。生成宏在 buildinfo.cpp 内消化，本头不暴露宏。
    QString stamp() const;
    // git 短哈希（git rev-parse --short HEAD；git 缺失回退 "nogit"）。
    QString gitHash() const;
    // 组合串 "构建时间 @ 短哈希"（主菜单角落 / F3 一行显示直接用）。
    QString full() const;

private:
    BuildInfo();
};

#endif // BUILDINFO_H
