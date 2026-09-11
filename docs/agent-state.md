# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-11 18:05
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1039
current_task_status: READY
last_completed_task: t1040 rig 稳健性补丁（8 站点显式持物加固，src 零改动，矩阵恒 522/0；实现棒限额中断由主控亲自收尾 docs）
last_task_closure_commit: （docs 本提交；代码终态 e9eec34）
last_verified_commit: e9eec34（矩阵 522 PASS / 0 FAIL，binary 与源对齐）
last_governance_review: 2026-09-11（audit #4 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 2
next_task: t1039（R19.23 执行顺序 t1038✅→t1040✅→t1039→t1042→t1041）
next_task_source: docs/dev-plan.md R19.23 段
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交（代码终态 f7bfe2b），工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）。

## Recovery Point

- 最近闭环：**t1038**（2026-09-11 18:05）：94 整体不桶化两道（Main.qml reassignShapeBuckets skip + blockdropinstancing getInstanceBuffer/hasLiveMember 同参排除），P-t1032a 改腿（94 断言不桶化）+ 新腿 P-t1038a（feeder 空表/同参停钟/火把对照/QML 源钉）+ P-t1032d 结构钉修订（94 skip 唯一豁免），矩阵 **521→522 PASS / 0 FAIL**（matrix_t1038_final.log 权威），阴性两轮各恰红 2 腿（neg=C++ 病灶、neg2=QML 病灶），voxelsandbox EXIT=0 + 冒烟 EXIT=124 + tail20 留存。
- 下一最小动作：派 t1040 rig 稳健补丁（t1026a/t945 等真消费端探针 m_selectedBlock fall-through 盲区，受影响腿显式 setSelectedBlock 或收窄断言；盘点全窗 fall-through 面；不加行为；矩阵基线 522）。
- 实机确认：R19.22 总表 16 项（含两池 ≥10 种 id 定向冒烟）+ t1038 附魔台书台同相观感 + R19.21 12 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
