# R19.14 终审报告（t857-t860，窗口 88c3a32..6b1405d）
## 判定：PASS（1 中危潜伏项，建议下批修）

> **处置记录（fix(review-r1914-final) 8b1dae0）**：M1 已修——putAABB 返回计数钳到 cap（`n=qMin(n+1,cap)`），t859 cap=0 探针同步钉钳制语义（cap=0 → 返 0 且零写入）；L1 已修——xporbinstancing.cpp 显式 `<cmath>`；L2 登记不修（空表 tick 成本微小，注释口径随实现演进再润）。矩阵 309 PASS / 0 FAIL。

### Findings
**M1（中·潜伏）** 溢出路径返回计数未收敛：blockregistry.cpp:1552 putAABB 越界时 ++n 无条件递增，collisionAABBsInto/shapeBoxesInto 返回的 n 可 >cap；调用方全部以 `boxes[kMaxAABBsPerCell]` 定容数组按返回值循环（world.cpp:544、playercontroller.cpp:5925、entitymanager 四处），一旦新增多盒形状超 4 → 守卫触发的同一刻栈越界读。当前最大铁砧 3 盒，路径不可达，行为零变成立；但"响亮失败"设计点恰是引爆点。建议：两处 Into 返回前 `n=qMin(n,cap)`（或调用方判 n>cap 即 fail）。附带：真触发时每帧数百次 qWarning 刷日志。矩阵 cap=0 探针只测了写字节保护、未测溢出迭代。

**L1（低）** xporbinstancing.cpp 用 std::cos/std::fmod/M_PI 未显式 include <cmath>，当前经 Qt 头传递可用（MSVC 构建通过），可移植性小nit。
**L2（低）** m_ticker 无论 manager 空/无活体恒 16ms markDirty——空表时仍触发渲染侧 sync 重取（表空零上传，成本微小）；注释"空表零成本"稍乐观，不构成缺陷。

### 通过面（亲核）
- **RHI 囚笼 §2-A**：xporbinstancing.{h,cpp} 纯公开 API（QQuick3DInstancing 子类、无 qrhi.h/QRhi/QShader/custom shader）；全仓 RHI 命中仅 playercontroller.h/main.qml 注释提及 QRhiGpuTimer 文字，非 include。分层正确：src/Game → Entities 向下依赖，先例一致（PlayerController QQuickItem 派生）。IP 门：新标识符全原创，无 MC 词根。CMake 双目标已注册（187/879 行）。Timer 生命周期：成员 QTimer 随对象析构自停，tick/markDirty 均 GUI 线程，合法。
- **t859 等价性**：分支序/特例表逐字镜像旧版；World 偏移换算原地加 (fx,fy,fz) 与旧语义一致；by-value 薄壳仅剩冷路径（selection/raycast/矩阵），热路径全清（playercontroller 九处、entitymanager 四处含 TNT 直调 Registry 特例、pointBlockedByCollision）。qWarning 可达性核实：main.cpp:101 装 handler，WRN 落 logs/voxelsandbox.log，过滤规则仅关 qt.qpa/scenegraph debug 类，默认 category 放行；CMakeLists 无 QT_NO_WARNING_OUTPUT。
- **t860 折叠**：PASS1 双路由（cutoutOnly 回退 / 折叠全收）互斥分支；PASS2 三路径（culled/greedy/流体特例）剔除链覆盖完整——门族（WoodDoor/SpruceDoor/IronDoor/Wood/IronTrapdoor 全在 isPartialBlock）、cross 全在 isCrossBillboard → 无双重发射也无漏发。5 段创建顺序与 segmentsPerChunk=5 一致（vis 分组同步改）；降级杠杆真实可用：crossChunkComp 模板在 Main.qml:4386 未删、cutoutOnly 分支保留、一行恢复注释在场。
- **t857 真值链**：extendedDataCollectionEnabled 绑 f3Visible（Main.qml:2869）；buildF3Text 仅 F3 显时由 10Hz Timer 读取，关时零收集；~drawEst 公式退役，drawEst token 全仓仅存一处说明性注释，visibleSegmentCount 降级为诊断计数无消费悬垂。
- **t858 接缝**：拾取/磁吸纯 C++ 不经 feeder（xporbmanager 未反向依赖渲染）；动画公式与旧 QML 数字逐字对齐（bob/pulse 推导正确），per-slot 0.37s 错峰为声明过的等价替换；颜色走 per-instance color、材质白基色，NoLighting 红线保持，无昼夜 tint 冲突（旧材质本就 NoLighting）；rideRevision/entitiesChanged 无新直发；xpOrbHost 空壳保留清 delegate 链。dev-plan 四条 ✅✅ 参数记录与代码一一相符（含诚实标注的待用户目视项）。

低危 L1/L2 可随下批顺手修，不阻塞收尾。
