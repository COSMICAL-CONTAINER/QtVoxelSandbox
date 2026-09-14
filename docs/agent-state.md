# QtMinecraft Agent State

状态文件版本：1
更新时间：2026-09-14 23:25（t1049 闭环）
用途：为断链恢复、定时治理和连续开发 Agent 提供短状态入口。长历史进入 dev-plan，架构决策进入 refactor-plan，治理规则进入 autonomous-governance。

## Current Control Block

```yaml
project: QtMinecraft
state: READY
current_task: R20.03（测试分层 = 测试大 TU 拆分 + 矩阵 --filter 两项提速投资）
current_task_status: READY
last_completed_task: t1049 GOV-20260914-1 纠偏闭环（review26-1 翻红根因 = 腿跨段 mob 泄漏 + t1045 踩踏 RNG，非 UB；48c2f09 fix + 3351a91 test + 本 docs）
last_task_closure_commit: 3351a91（代码终态 = 48c2f09 + 3351a91）
last_verified_commit: 3351a91（矩阵 545 PASS / 0 FAIL ×3，matrix_t1049_final1/2/3.log 权威，binary 22:34 与源对齐）
last_governance_review: 2026-09-14（audit #6 YELLOW → GOV-20260914-1=t1049 已闭环，R20 前置门解除）
governance_review_due: false
completed_tasks_since_governance_review: 1
next_task: R20.03（R20 重构主线首单；t1049 登记的「支撑抬高嵌入 mob 自格跳+级联踩踏」引擎 quirk 作 walk 重构输入）
next_task_source: docs/dev-plan.md R19.23 段「顺序（终）」+ refactor-plan R20 序
active_write_lease: main_orchestrator_serial_queue
single_writer_policy: one project, one workspace, one writing agent, one serial task
retry_count: 0
needs_human: false
```

## Workspace Guard

- HEAD = t1048 docs 提交（代码终态 a72ca74），工作区干净；`.codex/` 豁免不删不提交。
- 纪律①-⑧全在案（⑦工作区一致性硬门 / ⑧SPEC_CHANGE 门）；**大 TU 编译一律 -j 1**（09-13 蓝屏教训；本单大 TU 单编译 cc1plus 峰值 ~10GB / 34GB 总量，-j 1 下安全通过）。

## Recovery Point

- 最近闭环：**t1049 GOV-20260914-1 纠偏**（2026-09-14，48c2f09 fix + 3351a91 test + 本 docs）：review26-1 农田走廊腿翻红根因 = **非 UB**——腿 (a) 下半砖僵尸 zA 跨段泄漏进腿 (b)，slab→farmland 换地把 zA 脚位留在耕地格内部（支撑真顶高于脚位）→ aiHostile 玩家路径越障跳探（无攻击距离门）把自格判墙 → 虚假起跳+滑流回走廊 → 落地触发 t1045 踩踏掷骰（全局 RNG，P=fall−0.5）→ 掷中耕地变 Dirt 满格台阶 → zB 合规越障跳 feetOff=离散弧顶 1.19347 翻红；同 binary 红绿翻转=踩踏 RNG（pos/pos2 构建戳同为 15:43 取证；「RNG 无关」原判勘误），跨 binary 逐位一致=跳跃物理常数（恰证非 UB）。**UBSAN 不可行已登记**（两 MinGW 工具链均无 libubsan/libasan）。修复 = 腿内 removeEntityAt(zA)（(b) 净实体态起、全腿零 RNG 确定化，断言未放宽）+ pinSet 自文件结构钉（tools/ 自钉首例）；引擎侧「支撑抬高嵌入 mob 自格跳+级联踩踏」quirk 登记 R20 walk 重构输入（本单不动行为面）。矩阵 545→545（3× 全绿 matrix_t1049_final1/2/3.log；修复前 5 连绿 matrix_t1049_pos_r1-r5.log 留证）；app 重建 EXIT=0 + 冒烟 EXIT=124 存活 + logs/voxelsandbox_t1049_tail20.log。**audit #6 YELLOW 纠偏完成，R20 前置门解除。**
- t1048 批次 review 收口（2026-09-14，183782b fix + a72ca74 test）：On A Rail 勘误 500m 径向制 + 批次 review P3/台账全量清偿（详见 dev-plan t1048 段）；矩阵 545（matrix_t1048_final.log）。
- R19.23 批次全貌：t1038→t1040→t1039→t1042→t1041→t1043→t1047→t1044→t1045→t1046→**t1048（批次 review 收口）**，矩阵 521→545。双只读批次 review（A 代码正确性 / B 探针+台账）P2-1/P3/台账项全部清偿或登记。
- **新发现待办（t1048 阳性轮暴露，移交主控立项）**：review26-1 农田走廊腿环境敏感翻红（新 TU 布局下 3/4、失败轨迹逐位一致、同 binary 有全绿反例 pos2、与 RNG 不相关）——疑似 walk/support 路径潜伏 UB 被重编译布局暴露，非 t1048 行为面；证据链 matrix_t1048_pos.log（544/1）/pos2（545/0）/neg/neg2（co-red）/final（545/0）留存根目录。
- 下一最小动作：主控做批次统计 + audit #6 → R20 主线（R20.03 测试分层起；含测试大 TU 拆分与矩阵 --filter 两项提速投资）。
- 实机确认累计清单：R19.22 16 项 + R19.21 12 项 + parity 波各项观感 + t1048 骑车 500m 节奏（径向制下绕圈无效）待用户数据。
- 若 API 限额、断链或进程退出：只更新本文件的 Current Control Block 和 Recovery Point，不扩大任务范围。断链恢复按纪律⑦硬门执行。

## Governance Counter

- 锚点：2026-09-08 初始化 GREEN；#2/#3（09-09）、#4（09-11）GREEN、#5（09-12）YELLOW→GOV-20260912-1 已闭环。
- 触发规则：每 5 个完整闭环任务、批次结束、架构阶段切换、异常指标、定时触发 → 读 autonomous-governance 相关章节写结论。
- 任务计数只统计完整 fix/test/docs 闭环。
