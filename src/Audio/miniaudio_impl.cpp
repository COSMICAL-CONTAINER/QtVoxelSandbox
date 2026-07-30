// miniaudio 实现体编译单元（唯一 TU 定义 MINIAUDIO_IMPLEMENTATION）。
//
// miniaudio 是单头库（src/Audio/miniaudio.h）：在「恰好一个」.cpp 里 #define MINIAUDIO_IMPLEMENTATION
// 后 include，把实现编进来。放在独立 TU（而非 audiomanager.cpp）有两个好处：
//   1) miniaudio.h 体积大（~4MB）、需 <windows.h> 等系统头，独立 TU 避免与 Qt 头的宏冲突
//      （miniaudio 文档明确建议如此隔离）；
//   2) 整 TU 关警告（-w，见 CMakeLists） suppressing 第三方头在 -Wall -Wextra 下的海量警告，
//      不影响项目自有代码的零警告门（PLAN §4：第三方头警告可抑制并注明）。
//
// 许可：miniaudio = public domain 或 MIT-0（与本项目 MIT 兼容；来源见 src/Audio/miniaudio.h 头注释）。
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
