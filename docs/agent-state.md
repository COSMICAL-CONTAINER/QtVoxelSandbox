# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 23:50
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1042
current_task_status: READY
last_completed_task: t1039 掉落物 instancing 批 3（光晕壳族全族单 instanced Model，GlowShellInstancing 第三池；kShellCap=128 溢出 delegate 保底；hasTransparency 实例 alpha；空转门同款）
last_task_closure_commit: （docs 本提交；代码终态 1fccb3b）
last_verified_commit: 1fccb3b（矩阵 526 PASS / 0 FAIL，binary 与源对齐）
last_governance_review: 2026-09-11（audit #4 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 3
next_task: t1042（R19.23 执行顺序 t1038✅→t1040✅→t1039✅→t1042→t1041）
next_task_source: docs/dev-plan.md R19.23 段
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交（代码终态 1fccb3b），工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）。

## Recovery Point

- 最近闭环：**t1039**（2026-09-11 23:50）：光晕壳族全族单 instanced Model（GlowShellInstancing 第三池，收纳=槽序前 kShellCap=128 活体，delegate 排除侧薄委托同源谓词；hasTransparency 使实例表 alpha 生效；空转门同款；per-instance color 承载紫/灰×天光+呼吸/静态 alpha），矩阵 **522→526 PASS / 0 FAIL**（matrix_t1039_final.log 权威），阴性两轮各恰红（neg1=摘空转门 525/1 P-t1039c、neg2=摘收纳 alive 过滤 524/2 P-t1039a+b），voxelsandbox EXIT=0 + 冒烟 EXIT=124 + tail20 留存。探针口径教训：calculateTableEntry 落表为 **线性色**（QSSG sRGBToLinear），getColor() 回读线性值——颜色实例表断言须镜像该多项式。
- 下一最小动作：派 t1042 中立生物激怒反击（被打中立生物敌对反击：野狼/豹猫/牛羊猪鸡；复用 t1015 attack-target 体系与 t1031 野狼中立收口面；探针=攻击牛→牛反目标玩家一次、狼被打→敌对驯服豁免；矩阵基线 526）。
- 实机确认：R19.22 总表 16 项（含两池 ≥10 种 id 定向冒烟）+ t1038 附魔台书台同相观感 + t1039 壳观感四项（壳显/夜间变暗、附魔紫晕呼吸、壳-体 bob 脱锁接受口径、>128 溢出保底）+ R19.21 12 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
