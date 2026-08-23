#include "buildinfo.h"

// 构建版本戳单一权威出口（t813）：两个 VOXEL_BUILD_* 宏在本 TU 内消化为 QString，
// 全工程其余代码 / QML / 冒烟测试一律经 BuildInfo API 读取（宏永不外泄 → stamp 每次
// build 刷新时仅本 TU 重编，见 cmake/WriteBuildStamp.cmake 的「内容变才覆写」契约）。
#include "build_stamps.h" // CMake 生成（build/generated/；gitignored，不入库）

BuildInfo::BuildInfo() = default;

BuildInfo *BuildInfo::instance()
{
    static BuildInfo inst; // 进程全局唯一（值是编译期常量，无状态；QML create 同对象）
    return &inst;
}

QString BuildInfo::stamp() const
{
    return QStringLiteral(VOXEL_BUILD_STAMP);
}

QString BuildInfo::gitHash() const
{
    return QStringLiteral(VOXEL_BUILD_GIT);
}

QString BuildInfo::full() const
{
    return stamp() + QStringLiteral(" @ ") + gitHash();
}
