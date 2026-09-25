# 治理审计 #21（2026-09-26，只读独立 agent 执行，主控清偿批内落档）

> 审计窗口：9079f74..7353eb8 共 18 提交（含窗口首笔 9b03b88 = 审计 #20 落档件，非五闭环成员）。五闭环 = t1094（写门家族残面批收官）、t1095（红石中继器，一次 429 中断 SendMessage 续作）、t1096（经验瓶·蕴辉瓶）、t1097（酿造台+药水第一轮，含验收修正件 1b728a6/73443f4——主控 diff 亲读抓 F1 parity bug 回传同 agent 修复链首例）、t1098（残项小批合集：中继器生存配方翻案连补 smelt 链 + chunkstore 卫生 + 红石纪元标注审计 + 比较器纪元裁定）。

## 判定：GREEN

五闭环证据链全实核通过（五单三方/两方 md5 配方逐单复跑全等、十份 NEG 恰红面 + 十份还原逐日志吻合、t1097 腿文修订 md5 不变性主张机理独立复现成立、账本三方一致、腿数链 790→812 无断、五件 tail20 实文齐全、不变量五项抽查全过）；#20 预告的 YELLOW 升级条件（控制块字段再度失更）**未触发**——counter 1..5 逐单递增 + HEAD 行逐单同步终态，恢复后的纪律整个窗口守住了。发现 1 MEDIUM + 3 INFO，均不触及证据链与交付代码。

## 核查通过面（逐项证据）

- **证据链（全过）**：五单 PASS/FAIL 计数与关单主张逐一吻合——t1094 final/final2 各 797/0（pos 为 filter 跑 7 腿，命名偏差如实留痕）+ 主控复跑 797/0；t1095 pos/final 各 802/0；t1096 pos/final 各 806/0；t1097 pos/final 各 810/0；t1098 pos/final 各 812/0；orch 四件（t1095-t1098）计数同值。规范腿集合 md5 配方逐单复跑（全 32 位）：t1094 final/final2/orch 三方恒等 `7416b136d836e37027cd093c54cda5e0`；t1095 pos/final/orch `a2f3eb64f895601edc4fdad702a4bb21`；t1096 `9b493da5b66ba9874bafc0172c575cee`；t1097 `d2360c45208c60911834989f369391b1`；t1098 `6aa9a59584bc10c76a0b63a4d4c7c5bb`——与五关单条目主张值逐字全等。NEG 恰红面十份逐日志核：t1094 neg1={r2064a,e,g}/neg2={r2064b,c,d,g}、t1095 neg1={r2065a}/neg2={r2065b}、t1096 neg1={r2066a,d}/neg2={r2066a,b,d}（据实修正版）、t1097 neg1={r2067a}/neg2={r2067c}、t1098 neg1={r2068a}/neg2={r2068b}——每份与申报恰红面逐一吻合；十份 restore 全绿（0 FAIL）。t1098 附加自证面复现：pos/final/orch/tail20 四日志 "still in use" 计数全 0。
- **t1097 md5 不变性主张核（过）**：主张（dev-plan t1097 关单：「md5 对腿 ID 词元哈希，腿文更新不迁移值」）独立复现成立——配方哈希材料仅 `^PASS \| <腿ID> ` 词元（腿文不在哈希域）；实证取 73443f4 前后两版日志：修订前 matrix_t1097_filter11.log（09-26 01:49，r2067a 文为 "only decrements across completions"）与修订后 matrix_t1097_pos.log（04:00，文为 "decrements exactly once per completed operation...fresh powder burnt"）腿文实变，而 r2067 子集 md5 三处恒等 `d1b439dd`（filter11/fixfilter/pos），且 NEG 两红面（03:42-03:45）同携修订后措辞 = 73443f4「regenerate NEG red faces」主张与在盘证据一致。
- **账本三方一致（过）**：五关单哈希 ↔ git log ↔ agent-state（控制块 last_completed_task 族 + Recovery Point t1094-t1098 五条）↔ auto-backlog 摘除行逐单吻合（t1098 摘 removeDatabase 行、t1095/t1096/t1097 池底三候选划线、写门家族行补 t1094 收官注）；腿数链 790→797→802→806→810→812 无断；治理计数 5/5 触发本审计，算术自洽。
- **提交纪律（一违）**：18 提交 %B 扫描——**2 笔命中 AI 署名尾注（F1，见下）**；全英文检查命中 3 处 CJK 字面（F2，单列豁免判断）；文件面零互串（五笔 docs 提交仅触 docs/agent-state.md、auto-backlog.md、dev-plan.md[+9b03b88 审计报告本体]；代码/测试提交零触 docs/，textures+tools/build_*.py 与 src 同件=既定资产程序生成模式，CMakeLists 仅新段 TU 注册）。
- **tail20（全过）**：logs/voxelsandbox_t{1094,1095,1096,1097,1098}_tail20.log 五件齐——各含启动横幅（`=== voxelsandbox start ===` + build 戳）与 `root objects after load: 1` 实文；t1095 件 2.6KB 短会话形态与关单条目「横幅+root objects 齐——合规」注一致；t1097 build 戳 `2026-09-26 03:44 @ 0af0802` 为 build-before-commit 既知模式（#19 INFO 在册，1b728a6/73443f4 提交在 04:27）。
- **控制块全字段纪律（过——本窗判定重点）**：五次关单 docs 提交逐笔核 agent-state：counter = 1（997debc）/ 2（5ebbe79）/ 3（1458685）/ 4（1453d0c）/ 5（7353eb8）逐单递增；Workspace Guard HEAD 行逐单同步该单终态（t1094→t1098）；governance_review_due 在 7353eb8 恰翻 true（5/5 到期 + #21 已派）。#20 预告「若 #21 窗口仍失更降 YELLOW」——**未失更，不触发**。
- **不变量抽查（全过）**：①t1097 修复链与头注一致——playercontroller.cpp:6993-6994 完成循环 `--fuelOpsNow`+setFuelOps（一次操作=一批与瓶数无关）、:6958 追赶门 `fuelOpsNow > 0`、追赶轮合格瓶位复判（无映射防原料空转）、:7001 亮标读递减后现值；brewingstore.h:22-24 头注已改「燃料计每完成一次操作 -1（一次操作=一批转换全部合格瓶位，与瓶数无关）」与代码逐面一致；满口径场景③腿在 section67_brewing.cpp:150-175（播种 1→耗尽 0 粉未烧→续瓶烧第 2 粉 fuelCnt 2→1 重置 20 完成后 19）。②t1098 chunkstore 作用域收口零语义变化——机械差分（剥括号/缩进/注释后逐行 diff）：全部差异 = 11 处 `QSqlDatabase db = openStoreConnection(...)` 同文移位入内层作用域 + 携注闭括号新增 + 4 行新头注 + 一处 qWarning 串换行重排，语句集合恒等。③smelt 行兑现旧行注——ef140da 前 recipe.cpp:995（石按钮）/ :1054（石砖族）两处行注声称「石头经熔炉烧圆石产出」而 smelting.cpp 零 Stone/Cobble 行（链实断）；本单补 cobble→stone 行 + 0 XP 行（两表同接），旧行注自此为真（发现面计数见 F3）。④t1095 零侧表/整流保留——world.h 零 repeater 状态成员（状态全在方块 state bits，blockregistry.h:1217-1221 位域注）；world.cpp:5705 水平 4 向走 sourceFeedsCell 定向（整流）+ :5350/:6067 repeaterInputOn 定向读后端。⑤t1096 canonical split 链原样——xporbmanager.cpp:44-51 阈值链 247/123/63/31/15/7/3/1 + spawnOrbsForTotal 纯确定性收口。
- **留池台账（过）**：auto-backlog.md:51-58 酿造后续轮登记节六条与 t1097 关单「six follow-up rounds registered」吻合；t1099 立项划线条目（:53）与 dev-plan t1099 开工条目、agent-state current_task 三方一致（火抗/再生/中毒/虚弱/瞬间治疗五链核实先行 + 岩浆怪/恶魂/褐菇/西瓜 worldgen 在场核实清单逐字同源）；压力板标注失实更正（:33）与激活铁轨台账行（:34）两条候选在案；比较器纪元裁定停放收口（:56）。

## 发现与清偿

**F1（MEDIUM）t1096 两笔提交携带 AI 署名尾注**：9774062（fix）与 e33d257（test）提交对象 %B 尾均含 `Co-Authored-By: Claude Code <noreply@anthropic.com>`——违反本项目「提交零 AI 署名」纪律（#19/#20 两窗 30 笔零命中；本窗余 16 笔零命中）。两笔恰局限于 t1096 单任务链，指向该单派工简报未复述禁署名规则、子 agent 沿 harness 默认补注。
→ **清偿建议**：①**不做历史改写**（会破 dev-plan/agent-state/auto-backlog 三处已录哈希与本审计窗口定义本身，代价远超收益）；②禁署名规则目前只在用户记忆层、仓内 docs/autonomous-governance.md 与 Workspace Guard 纪律行均无明文——建议主控清偿批在 Workspace Guard 纪律节补一行「git 提交禁任何 AI 署名尾注（Co-Authored-By/Generated with）」+ 派工简报模板恒带此句，堵 harness 默认注入路径；③本发现落档即可，无需重跑证据（尾注不触证据链内容）。

**F2（INFO）提交信息 CJK 字面三处**：1458685（t1096 docs）两处「蕴辉瓶」（§9 登记的中文物件名引证）、5ebbe79（t1095 docs）一处「v1口径」（项目行话）。均在 docs-only 提交、引证性质，代码提交零 CJK。豁免判断：可接受，无需动作；「全英文」纪律实效域=代码/测试提交，docs 提交按引证豁免，登记备查。

**F3（INFO）t1098「三处旧行注」计数差一**：ef140da 提交信息与新补行注称「stone button / stone pressure plate / stone brick row comments」三处声称熔炼链；父修订实测仅两处显式声称（recipe.cpp:995、:1054），石压力板行注（父 :966）只写了多重集唯一性未提熔炼——但石压力板族同样因石材链断而不可生存获取，实体缺口覆盖面无误，仅叙事计数差一。无需代码动作；叙事精度登记备查。

**F4（INFO）dev-plan t1097 关单「NEG 日志生成于修订前」措辞歧义**：在盘 NEG 红面日志（03:42-03:45）实携 73443f4 修订后措辞 = 即「再生」的修订后红面（与其提交信息 "regenerate NEG red faces" 一致）；关单句若按「先于燃料口径修订」读则与在盘证据相悖、按「先于首轮 7 处 lawful 钉修订」读则成立（钉修订在 0af0802 于 03:16 已落树）。证据有效性不受影响（NEG 均 filter r2067 跑 + 恰红面已对终版主张逐一核），纯措辞歧义登记；建议后续关单句写明「修订」指代对象。

**UNVERIFIABLE**：无。

## 备查

- 治理计数已由 7353eb8 置 due=true + #21 已派；本审计落档后计数归零，下一审计窗口 = t1099 起五闭环（或至下次 5/5）。
- t1099 酿造效果链扩展在飞（r2069）；压力板纪元标注更正 + 激活铁轨台账行两条池候选待排。
- 本报告未提交，留主控清偿批落档（docs 提交）；报告本身与 git 署名无涉。
