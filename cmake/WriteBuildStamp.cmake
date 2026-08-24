# t813 构建版本戳生成器（CMake script mode；configure bootstrap + 每次 build 各跑一遍）。
#
# 产出 build_stamps.h（两个宏）：
#   VOXEL_BUILD_STAMP = 本次构建时间 "YYYY-MM-DD HH:MM"（string(TIMESTAMP) 取脚本运行时刻）
#   VOXEL_BUILD_GIT   = git rev-parse --short HEAD（仓库短哈希；git 不可用回退 "nogit"）
#
# 关键契约（防每 build 全量重编）：内容**有变化才覆写**目标文件 —— 先读旧文件比对，
# 内容相同则一个字节不写（mtime 不动 → ninja/gcc depfile 不触发消费 TU 重编）。
# 于是「每次 build 刷新」由恒跑的 custom target 承担，「重编」只发生在分钟边界跨过
# （或 git 哈希变）时，且仅重编唯一 include 该头的 src/Core/buildinfo.cpp 单 TU（其它
# 代码只见 buildinfo.h 的 QString API，宏永不外泄 → 改动不波及别的 TU）。
#
# 输入（-D 传入）：
#   STAMP_FILE = 生成的头文件绝对路径（build/generated/build_stamps.h，gitignored）
#   WORKDIR    = git 仓库根（execute_process 的 WORKING_DIRECTORY）
#
# 生成文件不入库（落在 build/ 下已被 .gitignore 的 build/ 覆盖）；绝不手改。

if(NOT DEFINED STAMP_FILE OR NOT DEFINED WORKDIR)
    message(FATAL_ERROR "WriteBuildStamp.cmake 需 -DSTAMP_FILE=<头文件路径> -DWORKDIR=<仓库根>")
endif()

# 构建时间：分钟精度（YYYY-MM-DD HH:MM）。与 dev-spec t813 显示格式一致；
# 同一分钟内的重复构建内容不变 → 不覆写 → 不重编（Exe 未变，stamp 亦无需变）。
string(TIMESTAMP STAMP_VALUE "%Y-%m-%d %H:%M")

# git 短哈希：构建时的 HEAD。git 缺失 / 非仓库 / 超时（TIMEOUT 5——仓库锁 / 杀软挂起时防 configure 与
#   build target 两次执行无限卡死；超时 RESULT_VARIABLE 非 0 → 与缺 git 同走 nogit 回退）时回退 "nogit"
#   （构建不因此失败；矩阵探针 t813 对 nogit 判 SKIP——环境缺失非格式回归，review24 低危对齐两层语义）。
set(GIT_VALUE "nogit")
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE git_out
    RESULT_VARIABLE git_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    TIMEOUT 5
)
if(git_rc EQUAL 0 AND NOT git_out STREQUAL "")
    set(GIT_VALUE "${git_out}")
endif()

set(stamp_content
"// build_stamps.h — t813 由 cmake/WriteBuildStamp.cmake 生成（每次构建刷新；内容变才覆写）。
// 生成文件：不入库（build/ 已 gitignore）、不手改。唯一消费 TU = src/Core/buildinfo.cpp；
// 其它代码经 BuildInfo::stamp()/gitHash()（QString API）间接读取，宏永不外泄。
#define VOXEL_BUILD_STAMP \"${STAMP_VALUE}\"
#define VOXEL_BUILD_GIT \"${GIT_VALUE}\"
")

# 内容比对：不同才写（保 mtime 稳定 → 仅内容真变时重编消费 TU）。
if(EXISTS "${STAMP_FILE}")
    file(READ "${STAMP_FILE}" stamp_old)
else()
    set(stamp_old "")
endif()
if(NOT stamp_old STREQUAL stamp_content)
    file(WRITE "${STAMP_FILE}" "${stamp_content}")
endif()
