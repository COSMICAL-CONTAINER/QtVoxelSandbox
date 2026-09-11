# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-12 03:25
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: t1043
current_task_status: READY
last_completed_task: t1041 instancing 批 4 收官——工具 3D 族（五几何+弓 per-id 桶 + tier 色 per-instance color + 弓弦 stringPass 双表）+ 工具/材料 billboard 族 + 异形 billboard 族（per-id 贴图桶池选型，禁自定义 shader）全合批（530→534，阳性一次过；阴性轮1 恰红 c、轮2 恰红 a+b+c；t1007 治理路径 a 终点）
last_task_closure_commit: （docs 本提交；代码终态见 test(t1041) 提交）
last_verified_commit: test(t1041)（矩阵 534 PASS / 0 FAIL，matrix_t1041_final.log 权威，binary 与源对齐）
last_governance_review: 2026-09-11（audit #4 GREEN）
governance_review_due: false
completed_tasks_since_governance_review: 5
next_task: t1043 铁轨水蚀 + 矿井防水（parity bug 波首单，裁-1；之后 t1044 蜘蛛爬墙 → t1045 耕地退化 → t1046 parity 小修合集 → R19.23 批次 review + audit #5）
next_task_source: docs/dev-plan.md R19.23 段 + docs/parity-ledger.md 用户裁决
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = 收口 docs 提交，工作区干净；`.codex/` 豁免不删不提交。
- 纪律：①每任务 docs 闭环同步本文件；②验收日志带任务号；③阴性轮 restore 一律 Edit 反向（严禁 git checkout/reset）；④长构建 nohup 起手；⑤用户修正文件（docs/Review_*.md）开工前必读，事实优先级高于 dev-plan 旧口径；⑥**多轮阴性轮间摘 C++ 病灶后必须先重建再跑下一轮**（t1038 教训：未重建旧 binary 混入上一病灶签名）。

## Recovery Point

- 最近闭环：**t1041**（2026-09-12）：掉落物 instancing 批 4 收官（t1007 治理路径 a 终点）——剩余三族全合批：①工具 3D 族（isTool3DDrop 五几何+弓 per-id 桶 × ToolDropInstancing，tier 色×天光 per-instance color〔t1039 先例〕+ 弓弦 stringPass 第二实例表〔t1038 不承载子树先例〕）；②工具/材料 billboard 族 + ③异形 billboard 族（BillboardDropInstancing itemIconFamily 双池单类，per-id 贴图=per-id host 桶池选型〔atlas+UV 需自定义 shader=PLAN §2-A 禁区〕，camPitch/camYaw 进实例表 rotation 朝相机，billboard 不自转）；Main.qml 十分支排除链 + 三新 host（各 8 桶，溢出 delegate 保底）；空转门 t1032 同款两池。矩阵 **530→534 PASS / 0 FAIL**（matrix_t1041_final.log 权威），阴性轮两轮（①摘空转门恰红 533/1=P-t1041c；②摘双 feeder 收纳谓词恰红 531/3=P-t1041a+b+c 谓词感知面），voxelsandbox EXIT=0 + 冒烟 EXIT=124 + tail20 留存。**登记**：钓鱼竿 type8 掉落 delegate 无分支（t803 只补打火石）如实不入族；弓弦-弓身 ms 级相位脱锁=批 1/2/3 已登记接受口径；QML 三 reassign/has* 编排面=headless 盲区，源码钉 P-t1041d 覆盖；rig slot-reuse 槽号教训（拾取沿断言须按 LIFO 复用算槽）。
- 下一最小动作：派 **t1043 铁轨水蚀 + 矿井防水**（parity bug 波首单，裁-1：Rail 入水毁族 isAttachableBlock 单一权威 + placeMineshaft 生成期防水「水下段不放轨/轨下垫不刷层」最小面盘点；探针=流水毁轨腿+矿井轨 worldgen 防水腿，阴性轮摘生成期防水恰红；矩阵基线 534）——之后 t1044 蜘蛛爬墙 → t1045 耕地退化 → t1046 parity 小修合集 → R19.23 批次 review + audit #5（t1041 收官已达「批次结束」触发条件的一半：还差 parity 波 4 单闭环）。
- 实机确认：R19.23 已闭环 5 项（t1038 书台同相 / t1039 壳观感四项 / t1042 惊逃与狼反击观感 / **t1041 F3 draw call 对比 + 工具 tier 色/弓弦/billboard 观感六项**）+ R19.22 总表 16 项 + 回归三单（t1005/t1006/t1007）待用户实机数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。
- 若任务完成：更新当前任务、状态、最新 commit、验证结果、任务计数和下一触发点，并与 dev-plan 同一 docs 闭环提交。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11 R19.22 批次）GREEN。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。（R19.23 批次 t1038/t1039/t1040/t1042 四闭环 + t1041 收官后达「批次结束」触发 → audit #5。）
- 任务计数只统计完整 fix/test/docs 闭环。
