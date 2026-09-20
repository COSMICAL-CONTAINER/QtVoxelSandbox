# 治理审计 #15 — 2026-09-20（触发：治理计数 5/5——t1065/t1066/t1067/t1068 四完整闭环 + 待机段/解除段 + t1069 立项）

- 触发：audit #14 后四个完整闭环（矩阵 687→703）+ 窗口内两次管线状态事件（待机宣布/用户指令解除）+ t1069 立项。独立只读 agent 执行（主控停用尾注后第三窗）。
- 日期/窗口：2026-09-20；起点 2f68939（#14 报告提交 + t1064 关单 + t1066 登记 + t1065 开工）→ 终点 bbec087（HEAD，t1068 关单 + 本审计派单），共 13 提交 = 4 完整闭环 × fix/test/docs 三段 + 5640987（t1067 关单 + 待机宣布）+ 8dafe97（待机解除 + t1068 立项）+ bbec087 内 t1069 立项（agent-state 控制块）。
- 最近完成任务：t1065（流式→固定切换池残留，Main.qml poolStreamingEra 时代标记，零 C++）、t1066（保存链 8 连接面 BUSY_TIMEOUT=0，锁下退出 ~29s→≤300ms+一档）、t1067（传送门词根改名 NetherPortal/EndPortal→EmberGate/AbyssGate，4 资产 git mv）、t1068（Ender 族→Abyss 族，14 文件 ≈406 处）。当前任务：t1069 IN_PROGRESS（前置 = 本审计；残余清偿合集 r2035c 口径收窄 + _meshSyncQueue 精确摘除，filter r2043）。
- 当前批次：连续开发串行队列（用户指令「直接先继续开发，不用管我」解除待机后）；事件 2 起均账本留痕（见事实核对 6）。
- git 状态：main；工作区仅 `?? .codex/`（窗口内 0 提交触及）；本地领先 origin/main **26 笔**（origin/main = 94bed7c，t1061 之前）——推送积压自 #14 的 12 笔翻倍，t1061-t1068 全部工作仅存本地，见自动动作 3。
- binary/source 状态：redstone_matrix_test.exe 时间戳 09-20 11:30 与 t1068 fix(11:27)→test 时序吻合；窗口源码改动全部带 pos/final 双日志 + orch_verify。**冒烟 tail20 日志自 r2035 起未落盘**（logs/ 最新为 voxelsandbox_r2034_tail20.log）——本窗四单冒烟证据为账本声明级（app 重建 EXIT=0 + 冒烟 EXIT=124），与 #14「未独立重跑 app」同态，延续观察。
- 矩阵状态：**703 PASS / 0 FAIL**（t1068 pos/final 双日志实测）；全窗基线 687→691→695→699→703 零 FAIL；四单主控脱离式复跑 orch_verify 实测 691/695/699/703 全 0 FAIL。
- 闭环数：4 完整（fix/test/docs 三段配对逐单核对无错位）。
- 阴性轮异常数：0。八份红日志红面腿与账本声明**逐单精确吻合**（见事实核对 3；任务简报对 r2039 两轮顺序笔误，账本与日志自洽非异常）。
- flaky 数：0 新报。t997/t979/t830 登记类零漂移（PASS 行抽查未见）。
- 性能基线变化：无。本窗三修均为改名/连接设置/换代一次性池重派生，非性能面；锁下退出 29s→≤300ms 属可靠性修复。

## 事实核对

1. **账实相符**：t1065(fe15881/05cb42d/7502d3b)、t1066(e1a55af/e107147/30e9c88)、t1067(f7f92d7/d69f982/5640987)、t1068(ae3b2fe/b46da7b/bbec087) 三段配对完整，hash 与 dev-plan ✅ 落地段逐一一致。fix 提交触及 tools/matrix 均系改名批探针同变更修订（t1067 51 处/t1068 50 处，腿名/断言标识符随改断言语义零变）+ CMake qrc/py 路径；test 提交只触 CMakeLists + tools/matrix（新增 section40-43 各 ~700-970 行）；docs 提交五笔只触 docs/agent-state.md + docs/dev-plan.md；无 build/logs/saves 误提交，`.codex/` 零提交。
2. **硬门（QML/线程/include）**：QML 变更面三处全登记——①t1065 Main.qml 两 hunk 全文亲读（@335 poolStreamingEra 属性块 + @881 enterWorld 两分支；呈现层池账本定位注释在场；`_refreshChunkVisibility`/`kickWorldMeshSync` 两消费端零触碰；fixed-only 恒 false = 逐位旧行为）；②t1067 四 QML（Main/BlockParticles/MaterialIcon/ResourceBrowser）+ t1068 三 QML 均系改名面（节点 id/连接处理器/字面随词根族），非玩法逻辑迁移。worldstore 审计面例外：t1066 三文件 diff = **44 插入 / 0 删除**（worldstore.cpp +29 = 头注盘点表块 + 每设置点一行 setConnectOptions），协议/数据/schema/计数口径零触碰；例外登记三面一致（worldstore.cpp 头注块原文「audit face: registered exception」+ 提交注 + 关单条目）。全窗 src/ diff **零新增 #include**（上行 include 零）+ **零线程原语记号**（t1023c/r2020d 白名单零扩，t1064 第三落点 savebridge.cpp 未再触）。
3. **验证纪律（逐项实测）**：八份 pos/final `grep -ac '^PASS'` = 691/695/699/703，全 0 FAIL，日志尾 `=== total FAIL: 0 ===` 在案；四单腿集合恒等（前 110 字符键 LC_ALL=C diff = IDENTICAL ×4，sort locale 警告系 #14 已登记同现象）；阴性八红面逐腿核对——r2039 N1=3P/1F{r2039d}/N2=2P/2F{r2039b,c}（dev-plan 声明 NEG-1={d}、NEG-2={b,c}——**日志与账本精确吻合**，简报顺序笔误不成立为异常）；r2040 N1=1P/3F{b,c,d}/N2=3P/1F{d}；r2041 N1{b}/N2{c}；r2042 N1{b}/N2{c}——八红面 100% 命中声明；十六份还原日志全 4P/0F；三连跑/终查（r2039 green3、r2040 run1-3、r2041 filter_1-3+final、r2042 g1-g3+final_check）全 4P/0F。相邻 filter 零污染日志（r2041_filter_final 等）在案。
4. **主控复跑**：四单脱离式独立复跑 matrix_t106[5-8]_orch_verify.log 实测 691/695/699/703 全 0 FAIL（#14 在途的 t1064 复跑面已随关单闭环，本窗四单全完整）。
5. **账本**：dev-plan t1065-t1068 四条目 ✅ 落地段与 git 全对应（四条 🚧 开工→✅ 落地状态迁移留痕完整）；agent-state 控制块（t1069 IN_PROGRESS、前置=审计 #15）与 Recovery Point（最新闭环 t1068）一致；「全弧待实机确认汇总」未被本窗改动。**发现一处缺口：t1069 立项仅存 agent-state 控制块，dev-plan 全文 `t1069` 词元计数 = 0**——与 #14 抓过的 t1064 立项漏写同类（第二现）；t1065-t1068 惯例均为立项即落 ### 条目。→ 自动动作 1。
6. **待机段/解除段留痕**：5640987（t1067 关单 + 「headless queue empty, pipeline standby awaiting user device data」）→ 8dafe97（dev-plan t1068 立项段逐字记录用户指令「直接先继续开发，不用管我」解除待机 + agent-state 控制块同步）。两笔均 docs-only，状态迁移链完整可追——**留痕完整，非方向问题**。
7. **署名尾注**：窗口 13 笔 `grep -ci co-authored` = **0**——停用延续（#14 时 12 笔连续干净，现累计 25 笔）。异常指标：无未知工作区修改、无大范围无关文件变化、无 flaky 新报。t1067 过程留痕（误 git stash 即时 pop 全量恢复 + 首次提交漏 git mv soft-reset 重做，25 行钉复验通过）已如实入账本，属自报纠偏非违规。

## 架构方向（对照 R20 纪律，逐面）

- **单一权威只减不增**：t1065 换代收口对称 t1063 复用 `resetChunkSlotPool` 唯一实现（零新机制）；t1066 失败语义收敛既有 t1064 探锁退避单点（零第二套重试）；t1067/t1068 映射表双锚头注立证（blockregistry.h:36/:56 + world.h:1074 + recipe.h:323），行为单点（点燃/激活/投掷/合成链）零复制零第二权威；补「worldstore 零 diff」例外按登记程序执行。
- **QML 生命周期决策零新增**：poolStreamingEra 为呈现层池账本（非生命周期决策，注释立证）；改名批 QML 面零逻辑迁移。
- **上行 include / 线程白名单**：全窗零新增 include、零线程原语记号（实测 diff 扫描）。
- **数据安全**：改名批数值 id 双钉实测在场——编译期 static_assert 落于矩阵腿（section42:472-475 四条 = AbyssGate 111/AbyssGateSurface 131/EmberGate 138/StateActiveFlag 0x01；section43:359-363 五条 = AbyssPearlId 0x243/EndEyeId 0x23A/Kind 7/8/DeathCause 16）+ 运行期 r2041d/r2042d PASS 行在案；blockregistry.h 枚举 diff 逐行核对值域逐位不动（111/131/138 仅换名）。资产 5 笔 git mv 全 R100 字节同内容（仅 build_end_portal.py R081 = 换名+路径实改，登记面内）。
- **豁免面**：resourcepackmanager 读取面 + kMcBlockId + §9 记载行携标记，r2041b/r2042b 逐文件计数钉（25/24 行）PASS 实测在案，漂移即红。

## 方向收益（§4 Step 7 七问·逐单简要）

| 单 | 用户目标 | 重构阻力 | 可复用 Module | 重复权威 | 可测试性 | 后台/联机就绪 | 特殊分支 |
|---|---|---|---|---|---|---|---|
| t1065 | ✅ 流式→固定切换空显/缺失 | 负（零 C++，复用 t1063 收口件） | resetChunkSlotPool 对偶复用 | 不增（时代标记=呈现层账本） | ✅ 病灶签名腿（keys=81 model=9 neg=1 era=1） | 中性 | 否（门控恰一次，fixed-only 零调用） |
| t1066 | ✅ 锁下退出秒级未响应 | 低（每连接一行） | t1064 退避链复用 | 减少（重试语义单点收敛） | ✅ 真锁首试 <1s 腿 | ✅（worker 不触 SQL 论证在案） | 否（8 点盘点钉防漏配） |
| t1067 | ✅ §9 IP 门存量清偿 | 低（纯改名+同变更探针） | 映射表双锚模式（t1068 沿用） | 不增 | ✅ 行为等价墙+双钉 | 中性 | 否（豁免行携标记钉住） |
| t1068 | ✅ §9 最后词根族 | 低（t1067 同构复走） | 同上 | 不增 | ✅ 同上 | 中性 | 否 |

无连续特殊分支累积；四单均零新增权威，改名批把 §9 登记债清偿为钉住的豁免清单。

## 任务队列健康度

当前任务唯一（t1069）；下一任务来源明确（t1063 非目标「_meshSyncQueue 精确摘除」+ t1062 非目标「r2035c 口径收窄」双背书）；无 BLOCKED 重试、无编号冲突；任务粒度回归小单节奏（四单均 ≤1 文件核心面或纯改名）。Architecture/Gameplay 双线平衡：本窗为登记债清偿窗（§9 改名 + 可靠性），非停滞。

## 结论：**GREEN**

四完整闭环证据链零错位；八阴性红面 100% 命中声明；worldstore 例外按登记程序三面一致落地；改名批数值 id 双钉实测在场（存档安全铁律兑现）；主控四单脱离式复跑全绿。唯一缺口（t1069 立项条目）有确定纠偏路径且属第二现，按 #14 先例以自动动作清偿，不构成方向问题。

## 自动动作

1. **t1069 派单时补录 dev-plan 立项条目（强制）**：残余清偿合集定义（r2035c 腿口径收窄 + _meshSyncQueue 精确摘除、filter r2043、来源 = t1062/t1063 非目标登记）落 ### 条目，清偿本审计发现。**此为同类缺口第二现**（#14 抓过 t1064）——若第三现，按 #14 自动动作 3 先例升级 GOV 纠偏任务。
2. agent-state 卫生（下次 docs 提交顺带，非阻塞）：①Recovery Point 尾部「下一最小动作：待机等用户真机数据」残留行与顶部控制块（t1069 IN_PROGRESS）矛盾，删或改述；②「Governance Counter」节停更于 #9（#10-#14 未记），改由控制块单点承载或补记——两处均系历史快照残留，非本窗引入。
3. **推送积压提示（用户动作，不阻塞）**：本地领先 origin/main 26 笔（#14 时 12 笔），t1061-t1068 全部工作仅存本地盘——建议用户择机 `git push`，防单盘故障丢失全弧证据链。
4. 冒烟证据落盘观察项：tail20 日志自 r2035 起未再产出（本窗四单冒烟为声明级）。非违规（与 #14 同态），若持续到下一审计建议恢复 `logs/voxelsandbox_rNNNN_tail20.log` 落盘惯例。

## 纠偏任务

无独立 GOV 任务——自动动作 1 在 t1069 派单 docs 提交内清偿（下一审计首查项）。

## 人工待验

- P5 用户设备会话清单不变（主项 = §29.7 黎明风暴 recheck）+ 本窗新增四单待实机项：①t1065 真机「流式走出核心域→退菜单→进 fixed」负坐标空组消失；②t1066 锁下退出 ≤0.5s 放行 + backoff 行可见；③t1067 三个 displayName 观感 + 新图标渲染 + 旧档传送门不变；④t1068「暗渊珠/暗渊之眼」观感 + 资源包 ender_eye.png 读取面 + 旧档物品行为。
- 推送积压表态（自动动作 3）。

## 下一治理触发点

t1069 闭环后治理计数重置 0；审计 #16 于下一 5/5（或批次结束/架构阶段切换/异常指标/定时触发）。若 t1069 派单 docs 未按自动动作 1 补录条目，#16 将该事项升级为 GOV 纠偏任务。

## 重点面亲核证据补强（本轮实际执行的验证，可复追）

- 提交/文件面：`git log --oneline 2f68939..HEAD`（13 笔）+ `git log --name-status` 逐笔核文件域（fix 触 src+tools+R100 资产、test 触 CMakeLists+tools/matrix、docs 只触 docs/ 两文件 ×5）；`git status --porcelain` 仅 `?? .codex/`；`git log 2f68939..HEAD -- .codex/` = 0 笔。
- 日志计数：`grep -ac '^PASS'/'^FAIL'` 八份 pos/final（691/695/699/703，全 0F）+ 四份 orch_verify（同值全 0F）+ 八份阴性红日志逐腿读 FAIL 行（红面腿名与声明 100% 吻合）+ 十六份 restore 全 4P/0F + 三连跑/终查 12 份全 4P/0F。
- 腿集合恒等：`LC_ALL=C diff <(grep -a '^PASS' pos | cut -c1-110 | sort) <(…final…)` ×4 = IDENTICAL。
- 改名双批：`git show f7f92d7 -- src/Core/blockregistry.h` 逐行读（值域逐位不动）；`grep static_assert tools/matrix/section42/43`（4+5 条编译期钉落点定位）；r2041b/r2042b PASS 行（25/24 行逐文件计数）+ r2041d/r2042d PASS 行（数值 id/Kind/死因双钉）亲读。
- 架构抽查：`git diff 2f68939..HEAD -- src/ | grep '^+.*#include'` = 空 + 线程原语记号 = 空；t1066 三文件 diff 44 插入/0 删除 + worldstore.cpp 头注例外登记块全文亲读；t1065 Main.qml 全 diff（两 hunk）亲读；`git show 5640987/8dafe97 -- docs/dev-plan.md` 待机/解除留痕亲读。
- 账本缺口：`grep -c 't1069' docs/dev-plan.md` = 0；四条目 🚧→✅ 状态迁移 + hash 对账全过。
- 署名尾注：`git log --format='%B' 2f68939..HEAD | grep -ci co-authored` = 0（停用累计 25 笔连续）。
