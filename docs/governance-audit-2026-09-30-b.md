# 治理审计 #25（2026-09-30，独立只读 agent 执行，主控代落档）

> 审计窗口：**d470123..d30aa405 共 15 提交**（窗口锚两处 git 现抄亲核：d470123 = #24 审计落档件、d30aa405 = t1119 关单 docs 提交，HEAD 同点位；`git log --oneline` 实数 **15** 笔——简报口径「17 提交：3+3+3+3+3+2」系误计，实构 = 五闭环各 fix+test+docs 三笔整 15 笔，构成详见 F2）。参照件 `docs/governance-audit-2026-09-30.md`（#24）ls docs/ 现核在盘，体例对齐。**窗口实况**：t1115（golden apple 完整链）、t1116（IronBars 图标+蛋糕失撑+矿井箱金苹果行）、t1117（三空白图标修复）、t1118（sign 板面 3D 字面）、t1119（骑猪第三人称坐姿）五闭环，矩阵腿数链 876→881→885→887→890→893 无断；本窗为 #24 清偿纪律（F1 现算贴入/F2 mv 保字节/F3 restore 禁复制/F4 落档名以实日）首个完整履行窗。

## 判定：GREEN

五单交付代码与测试**实质面与证据链账面双双全绿**：23 件全量矩阵日志（pos/final/orch+restore）在盘亲数全 0 FAIL（881×5/885×5/887×4/890×5/893×5），**raw 规范配方 md5 复算与权威值五单全部逐字命中**（#24 F1 清偿履行成立——上窗幻影值事故零复发）；九份 NEG 红面恰红全吻合（全量 N−1+1 形态、FAIL 腿与申报逐一全等、红件 raw 腿集全互异）；33 件整文件 md5 两两互异（cmp 四对抽核全互异 + mtime 20-30 分钟物理跑形态）= #24 F3 清偿履行成立；账本三方一致（dev-plan 五关单 ↔ git log 提交对 ↔ auto-backlog 摘除行 ↔ agent-state counter 0→5 逐单 diff 实文）；15 笔提交纪律全过（署名零命中+全 ASCII+文件面零互串）；t1117 提交重写处置四方对表全中（原违规件实文+双树哈希逐位等+链位连续+留痕一致）；r2089c 机制面零触碰实证 + r2082d 钉族实文幸存。本窗仅三项低阶发现（tail20 两单工件缺行/窗口构成简报误计/字模散文计数漂移），均不触证据实质，判 GREEN。

## 核查通过面（逐项证据）

- **#24 F1 清偿履行（过，本窗核心准据）**：五单 23 件全量日志以权威命令式（`grep -a -oE "^PASS \| [a-z0-9]+ " | sort | md5sum`）逐单复算，与主控亲算权威值 **32 位逐字比对**：t1115 `b10e05445a3794dee7d5ccf4e14a249f` 五方恒等（pos/final/orch+双 restore）✓、t1116 `784831615882eff17ff409dedf25407d` 五方 ✓、t1117 `e50341287ebfa990ad0442d39e8cc52b` 四方（pos/final/orch+单 restore）✓、t1118 `6e5728a348f547e0e52d173fe25d47d4` 五方 ✓、t1119 `4c21d57583404d1df5c89eb3301cd753` 五方 ✓。各单 agent 自报「数字归一变体」（368c52d4/8ed77d30/fd2c4df7 等）确系多余动作（第四连单留痕），raw 规范式即恒等——与本审计复算口径一致。
- **#24 F3 清偿履行（过）**：33 件全量日志（7+7+5+7+7）整文件 md5 现算**零重值**（`uniq -d` = 0）；cmp 抽核四对全互异（t1115 pos≠neg1_restore 差于 byte 79062 / t1119 pos≠neg1_restore / t1118 final≠neg2_restore / t1116 pos≠neg1_restore）；28 件申报整文件 md5 与在盘值**逐字全中**（t1115 六件 4745aa51/9dd021c1/03c647fe/5eaad217/5ba77ff7/d7507c79、t1116 六件 face9b6e/188c4875/78cc3097/aa9450fc/35718dbd/f5d90508、t1117 四件 4cb2cc79/fb3fd427/74f41d94/e64c1c37、t1118 六件 bab9e57f/f1f7c1f5/b682cc9e/ca725d3d/5acd8203/b47c4cfa、t1119 六件 f996ba27/8057e07b/3adbd2ba/6f11d6a7/5f53571c/87d0cc84）；mtime 间距物理（t1115 全轮 02:21→04:38 每 29-30 分钟一跑、t1119 16:15→18:01 每 20-22 分钟一跑）——#24 F3 型「1 分钟副本对」形态零出现；物理跑数申报与盘面工件数逐单对表吻合（含 orch 五件均在 final 之后独立复跑：05:08/09:08/11:32/15:24/18:31）。
- **#24 F4 教训履行（过）**：ls docs/ 现核——governance-audit-2026-09-30-b.md 不存在、无同名冲突，本报告按实际落档日（2026-09-30 第二份）取 -b 后缀。
- **#24 其余清偿落地（过）**：dev-plan:4508 t1111 勘正注（幻影值 d3ae5194→实值 b6ae823d+NEG 4 腿 filter 形态+主控验收漏洞记录）、dev-plan:4546 t1114 披露注（2 跑 2 副本）、agent-state:63 同款勘正、Workspace Guard 四句纪律（现算贴入/mv 保字节/restore 实跑禁复制+cmp 抽核/落档名以实日）+ t1117 soft-reset 教训 + t1119 checkout 事故复归教训全部入档（agent-state :36-37 亲读）。
- **NEG 证据（红面 9/9 吻合）**：九份红日志逐份亲数 FAIL 腿与申报敏感腿逐一全等——r2085 neg1={r2085a}/neg2={r2085c}、r2086 neg1={r2086a}/neg2={r2086b}、r2087 neg1={r2087b}、r2088 neg1={r2088a}/neg2={r2088b}、r2089 neg1={r2089b}/neg2={r2089c}；九件均为**全量 N−1+1 形态**（880+1/884+1/886+1/889+1/892+1，无 #24 F2 型 filter 形态失实）；九件 raw 腿集 md5 两两互异且各别于本单恒等值；十件 restore 全绿（配方 md5 归入五单恒等，上面已核）。
- **账本三方一致（过）**：dev-plan 五关单条目（:4550/:4561/:4574/:4584/:4596）↔ git log 提交对逐单吻合（1b0eed4+94bf9f6 / 716ce1c7+eff38764 / dabc0f7a+17ddb4fd / 0911e3c5+373145c7 / 8ddeffc7+f066bad4，哈希逐字对表）↔ auto-backlog:72 t1115/t1116/t1117 摘除行（携提交对+腿数+勘误留痕；t1118/t1119 系 dev-plan 池面清偿非 auto-backlog 条目，grep 无陈旧残留，账面自洽）↔ agent-state counter 0→1→2→3→4→5 **逐单递增 diff 实文亲核**（4122c267/9486aee6/62e1ab40/10219787/d30aa405 五笔各有 −0→+1 式递增行）+ governance_review_due 于 d30aa405 恰翻 true（5/5 触发+审计 #25 在飞）+ HEAD 行五笔同步 + Recovery Point 五条在案。
- **提交纪律（全过，第四窗连续）**：15 笔 %B 聚合 `co-authored-by|generated with|claude|anthropic|🤖` 大小写不敏感**零命中**（对照词 15/15 命中=检测器活）；逐笔 LC_ALL=C tr 删 ASCII 法 15×0=**全窗纯 ASCII**（含 t1117 重写双件）；文件面零互串——十笔 fix/test 提交零 docs/saves/.codex 触碰，五笔 docs 提交台账面合法（t1118/t1119 关单只触 agent-state+dev-plan 两件=无 auto-backlog 摘除项，与池面清偿叙事自洽）；targeted add 吻合——8ddeffc7 恰单文件 Main.qml +18/−1（与「纯呈现层」申报逐字吻合）、dabc0f7a 恰 4 件（三 PNG+生成器）。
- **t1117 提交重写处置（四方对表全中）**：①reflog 实证重写链 4f520bcb（原 fix）→1cc3f09b（原 test）→`reset: moving to HEAD~2`→dabc0f7a→17ddb4fd，与申报「reset+双段重提交」逐字吻合；②违规本体实证——原 fix 4f520bcb 消息实含两个 CJK 字符「截面」（6 非 ASCII 字节亲测）；③树哈希连续性——dabc0f7a tree `b33e5e9e` = 4f520bcb tree 逐位等、17ddb4fd tree `132c2d6f` = 1cc3f09b tree 逐位等（dev-plan:4580「HEAD 树 132c2d6f 与原 test 树逐位吻合=内容零漂移」精确成立）；④链位连续——dabc0f7a parent = 9486aee6（t1116 关单，d30aa405^ 链上在位），重写先于 62e1ab40 关单与任何 push；t1116 同门重写同法核实（e3387709 同含 6 字节 CJK→reset→716ce1c7，tree 32c234bd 逐位等）=「t1116 同门先例」申报属实。
- **t1119 过程注核查（相容）**：窗内 HEAD reflog 零 rebase/filter/amend 强制重写痕迹（仅前述两笔申报过的 reset）；`git checkout --` 事故复归天然不入 HEAD reflog，与申报「非还原通路」相容；终还原件字节核验——现盘 src/ui/Main.qml md5 = `8cc780b53411f5b4093cac49ba8d1081` 与申报「8cc780b5 双方」一致，且 8ddeffc7 后 Main.qml 零后续触碰（终态=还原态）；restore 行为实证——双 restore 日志配方 md5 与 pos 恒等（4c21d575 五方已核）。
- **r2089c 机制面零触碰（过）**：`git log --name-only 8ddeffc7^..f066bad4 -- src/Game/playercontroller.cpp` **空输出**=t1119 窗内零触碰实证；全窗内该文件仅 t1115（1b0eed45 苹果掉落门）/t1116（716ce1c7 蛋糕预检注）两笔合法交付触碰；r2082d 钉族实文幸存——骑乘钉位 :4608-4612（m_ridingPig+serial+setRideExclusion）、推挤豁免 :4578 注+:4612 挂+:8942 清、骑乘互斥双守卫 :5596（船）/:5744（矿车，各携 t1112 注）、权威实现 :8917-8921、下猪链 :8929-8944。
- **tail20（3/5 形态完整，2 单见 F1）**：logs/voxelsandbox_t111{5,6,7}_tail20.log 三件在盘各含来源头注+`=== voxelsandbox start ===` 横幅+`root objects after load: 1` 实文；t1118/t1119 两件横幅在而 root objects 行缺席（F1）。build 戳指向前一 docs 提交（d470123/716ce1c7/9486aee6/62e1ab40/10219787）=build-before-commit 既知模式。
- **不变量抽查（3/3 过，file:line 实文亲读）**：①**t1118**——src/Game/signboardfont.cpp:13-15 kMissingGlyph 九行框（0x0E,0x11,0x0A,0x11,0x0A,0x11,0x0E,0,0 携「行序与任何在册字模都不同——hasGlyph 据此反判」注）+ :128-129 覆盖面外回填消费点（行为面 85 字模覆盖+互异由 r2088a 腿矩阵承载）；②**t1119**——src/ui/Main.qml:5809 `readonly property bool isRidingPig: player.feetPosition.y > -1000.0 ? player.isRidingPig() : false`——feetPosition 参与值计算的表达式形依赖载体（t498 铁律）+ :5801-5808 三转变经 positionChanged 触达的机理注 + :5810 sitBlend 三骑乘扩展；③**t1116**——src/World/world.cpp:1816（4 参 setBlock）/:1972（5 参）/:2274（setWaterSilent）三写入口同位同序挂 checkCakeSupportOnEdit + :5547 `if (BlockRegistry::solidSupportBlock(id)) return;` 与放置预检同谓词（携「改谓词只改一处」单一权威注）。
- **push 实数（过，算术自洽）**：`git rev-list --count origin/main..HEAD` 实测 **32**（origin/main = 473b318 = t1109 关单）。#24 落档时积压 16 + 本窗 15 笔 + #24 落档件 d470123 = 32——逐笔自洽；agent-state next_task「push 16+ 笔待推」提醒仍有效，积压已翻倍建议择机推送。

## 发现与清偿

**F1（LOW）t1118/t1119 tail20 工件缺 root objects 行——申报与工件形态失配**：logs/voxelsandbox_t1118_tail20.log 与 t1119 件头注均声称「start banner + root objects」、dev-plan :4592/:4603 关单注亦申报「冒烟 tail20（…+root objects）」，但两件工件 head 窗（9 行）被 worldgen 新增的 `caves carved` 行挤出，`root objects after load` 行**均不在工件内**（t1115-t1117 三件在，因彼时 worldgen 少一行恰容 9 行窗）。实质面：t1119 有证——live logs/voxelsandbox.log:1280 `18:04:03.760 root objects after load: 1` 亲读在案（该跑本体）；t1118 的冒烟日志已被 t1119 冒烟覆盖（live log 单横幅 18:04 实证），root objects 行现**无任何盘上工件**（UNVERIFIABLE）。与 #24 F2 同族（申报形态 vs 盘面工件）但低两阶：非证据链核心位、run 存活性本身由横幅+EXIT=124+tail 段承载。
→ **清偿建议**：①tail20 采集脚本 head 行数 9→14（或 head -9 + grep root objects 兜底行双段式），防 worldgen 行数漂移再挤出；②主控随本审计落档批在 dev-plan t1118/t1119 两关单注顺带补一句「root objects 行因 head 窗漂移未入工件，t1118 面以 run 存活横幅+EXIT 为证」勘正注（不单独占提交）。

**F2（INFO）窗口构成简报误计（#24 F5 同族第三窗）**：简报口径「t1115-t1119 五闭环 17 提交：3+3+3+3+3+2」vs 实数 15（五闭环各恰 3 笔，六项加总式对五闭环本身即不成立）。#24 已立「简报窗口构成以 git log 实数为准」建议，第三窗复发。
→ **清偿建议**：无需代码动作；后续审计派工简报窗口构成一律 `git log --oneline` 现抄后填写。

**F3（INFO）字模计数三处散文漂移（t1118）**：src/Game/signboardfont.cpp:19 注「83 字模在册 = 大写 26+小写 26+数字 10+基础标点 21」漏数 :92/:93 两个十六进制字面 case（`char32_t(0x22)` 双引号/`char32_t(0x27)` 撇号）——实数 85 case（26+26+10+21 单字符含空格+2 hex）；62e1ab40 关单注组成加总「26+26+10+22」=84 亦与 85 差一（引号对或按「一种」计）。行为面无影响（r2088a「85 字模全回答」腿矩阵绿，实数 85 与行为申报吻合），纯注释/散文计数漂移。
→ **清偿建议**：下次触该文件顺带把 :19 注释勘正为 85（26+26+10+23）；关单散文随下次触条目时勘正。

**UNVERIFIABLE**：①t1119「NEG-1 首摘致 Main.qml 截断→checkout 事故复归→重走双手工 Edit 还原」事件本体——checkout 不入 HEAD reflog、中间态无独立工件；申报三面自洽（close 注/dev-plan 过程注/Workspace Guard 教训行）且终态 md5（8cc780b5 现盘吻合）+双 restore 配方恒等+NEG 红面恰红为实质承载。②t1117「生成器 md5 NEG 前后同值=逐字节还原」同理（中间态无工件，终态内容由树哈希+行为面承载）。两者均属「过程事件无盘上工件、账面自述与终态相容」类，与 #23/#24 UNVERIFIABLE 同判。

## 备查

- 判定准据：#23/#24 核心准据「md5 逐单复跑三方恒等且与关单主张逐字全等」本窗 **5/5 成立**且申报值即权威值（主控亲算三方恒等后下发，本审计独立复算逐字命中——上窗 F1「主控复算未对表」漏洞的流程修补已见效）；NEG 形态申报（全量/filter）本窗 9/9 与盘面一致；restore 实跑 10/10 有 cmp 级互异证据。三项发现均 LOW/INFO 级、不触证据链核心位，故 GREEN。
- 治理计数：d30aa405 已置 due=true + 审计 #25（本报告）在飞；落档后计数归零。池面真底空申报与 auto-backlog/dev-plan 盘面相符（剩余全部用户门控项），自治循环将如实待机——下窗审计触发条件 = 再积 5 闭环或 P0 插队后累积。
- t1116/t1117 两笔 soft-reset 重写均在 push 前完成（origin/main=473b318 不含两窗），无历史篡改风险；reflog 保留原件可溯（4f520bcb/1cc3f09b/e3387709 在案）。
- 本窗「材料段尾追加」契约连续履行（0x292/0x293 尾追加、方块段零新增、atlas 零扩、Count 162 不变，diff 面核）；矩阵腿数链 876→893 单调无回退。
- 本报告由审计 agent 落档 docs/governance-audit-2026-09-30-b.md（主控授权单次写入）；报告本身与 git 署名无涉。除本次落档写入外全程只读：零 git 写操作、零构建。
