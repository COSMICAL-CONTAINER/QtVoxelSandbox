# 治理审计 #29 报告（2026-10-06）

- **审计 agent**：独立只读治理审计（audit #29）
- **窗口**：`675759d3..a8fa0a9c`（现数 **16 提交**；派工简报"17 提交"为含基线 675759d3=audit #28 落档件的含端点口径差，INFO-1）
- **窗口内容**：t1135/t1136/t1137/t1138/t1139 五闭环（fix+test+docs 各三笔）+ 第三轮外审交接入档（846756ed）+ audit #28 承前
- **方法**：全部结论以在盘日志/工件的只读复算为准（grep/md5sum/md5sum -c/awk/date/git log 只读命令 + 源码与反汇编工件亲读）；零构建、零矩阵 exe 运行、零 git 写操作；src/ tests/ 零触碰；本报告为本次会话唯一写入件
- **规范形口径**：`grep -a -oE "^PASS \| [a-z0-9]+ " <log> | sort | md5sum`（唯一 canonical 公式，逐单四面现算）

---

## 〇、总评

**窗口结论：GREEN（with findings）——无 HIGH/MED 实质证据缺陷；4 LOW + 1 MED(已缓释) + 5 INFO。**

13 项核查全部执行完毕：五单规范形四面 md5 复算逐字命中账面；五单四方恒等程序化 == 全 YES；主控复跑件（t1135/t1137/t1138 verify + t1136 verify2）并入恒等全 MATCH；十轮 NEG 红卷 FAIL 行逐行比对申报恰红集零偏差；六件 flake 轮 FAIL 员全在册；filter 件数与总 FAIL 全符；冒烟三件+戳代次+墙钟全符；t1138 三勘误与 t1139 断点台账如实性坐实；era 翻案抽核三件两件字节级坐实、一件方向坐实但替换枚举有误（F-1）；账本三方一致（两处记法 slip）；AI 署名全窗干净。

---

## 一、逐项核查（13 项规程）

### 1. 规范形 canonical md5 五单复算逐字对表 —— **PASS**

现算（`grep -a -oE "^PASS \| [a-z0-9]+ " <log> | sort | md5sum`，pos/neg1_restore/neg2_restore/final 四面）：

| 单 | 现算 canonical（四面恒等） | 账面（dev-plan 条目） | 对表 |
|---|---|---|---|
| t1135 | `af91992e0e02a5cc54127a237c3d1054` | af91992e…（dev-plan:4751） | MATCH |
| t1136 | `4f59a4e334171e3f15bc9c5b85c4814a` | 4f59a4e3…（dev-plan:4759） | MATCH |
| t1137 | `e4c7efd783a551f2d33ce4bbe6e9ffa1` | e4c7efd7…（dev-plan:4769） | MATCH |
| t1138 | `8cbae6dbbcfcf138ff4a1f433ddefe8d` | 8cbae6db…（dev-plan:4779） | MATCH |
| t1139 | `9142805e511f1ef97840b871963a74ef` | 9142805e…（dev-plan:4790） | MATCH |

面计数（pass/fail）同步现算全符账面：t1135 966/0 ×4 面、NEG 964/2 + 964/2；t1136 971/0 ×4、NEG 968/3 + 969/2；t1137 976/0 ×4、NEG 967/9 + 974/2；t1138 981/0 ×4、NEG 975/6 + 978/3；t1139 985/0 ×4、NEG 983/2 + 984/1。

**双变体勘正面（如实，非缺陷）**：
- t1135：test 提交消息与 `build/t1135_canonical_md5.txt` 持 `321d080dc522acc143140203ef90f79b`（报告自创"flag+leg-id 流"口径）；close 提交消息与 dev-plan 明示勘正并以规范形 `af91992e…` 入账。close 消息仅携 8 位前缀 `af91992e`（全 32 位仅 dev-plan 在案）——INFO-4。
- t1136：test 提交消息持 `59003b76`（报告 booked、十四形式试解均不中）；close 提交+dev-plan 以规范形勘正入账。现算坐实规范形。

### 2. 三方/四方恒等复算（主控复跑件并入）—— **PASS**

程序化 == 硬判（现算）：

| 单 | pos==n1restore==n2restore==final | 主控复跑件 | 并入结果 |
|---|---|---|---|
| t1135 | YES | matrix_orch_t1135_verify.log（966/0） | MATCH |
| t1136 | YES | matrix_orch_t1136_verify2.log（971/0） | MATCH |
| t1137 | YES | matrix_orch_t1137_verify.log（976/0） | MATCH |
| t1138 | YES | matrix_orch_t1138_verify.log（981/0） | MATCH |
| t1139 | YES | **无 orch verify 件**（final 即主控亲跑，见第 12 项） | （合并面） |

双 designed red 流互异且异于绿流：五单全 OK（现算）。t1139 双红 canonical 与台账逐字命中：neg1_red=`278f3b2a7024769a7ce30797f4ea556c`、neg2_red=`e9870323cc05f8b95fd2e4730f9ed4da`。

整文件 md5 复算：五单六跑链（30 件）两两互异 ✓；`md5sum -c` 对在盘工件 t1137_runlog_md5.txt（6 行）与 t1138_runlog_md5.txt（7 行）全 OK；t1139_runlog_md5.txt（2 行：neg1_restore/neg2_red）全 OK；t1136 的"七跑+主控两轮"九值零重复现算坐实（无 t1136 runlog md5 工件，主张由本审计现算补证——INFO-5）。

### 3. NEG 恰红逐日志核对 —— **PASS**

十轮红卷 FAIL 行逐行采集比对（`grep -a -E "^FAIL \| "`）：

| 单/轮 | 现采 FAIL 集 | 申报恰红集 | 判 |
|---|---|---|---|
| t1135 NEG-1 | r2105c, r2105d | {r2105c, r2105d} | 恰符 |
| t1135 NEG-2 | r2105a, r2105d | {r2105a, r2105d} | 恰符 |
| t1136 NEG-1 | r2097c(flake), r2106a, r2106e | {r2106a, r2106e}+flake 员 | 恰符 |
| t1136 NEG-2 | r2106c, r2106e | {r2106c, r2106e} | 恰符 |
| t1137 NEG-1 | r2105a,b,c, r2106a,b, r2107a,b,d,e（恰九） | 九腿={r2107a,b,d,e}∪{r2105a,b,c,r2106a,b} | 恰符 |
| t1137 NEG-2 | r2107d, r2107e | {r2107d, r2107e} | 恰符 |
| t1138 NEG-1 | r2097c(flake), r2105c, r2106a, r2107b, r2108b, r2108e | 五设计腿{r2108b,e,r2105c,r2106a,r2107b}+flake 员 | 恰符 |
| t1138 NEG-2 | r2108a, r2108c, r2108e | {r2108a,c,e} | 恰符 |
| t1139 NEG-1 | r2067c, r2109c | {r2109c}+{r2067c} | 恰符 |
| t1139 NEG-2 | r2109b | {r2109b} | 恰符 |

豁免对照集全绿核：t1135（neg1 豁免 r2105a/b、neg2 豁免 r2105b/c）、t1136（r2106b/c/d 等不在任何红集）、t1137（未修订 r2105d/r2106c,d,e 全绿）、t1138（r2105a/b、r2106b/c/d、r2107c/d 全不在红集）、t1139（末推行幸存=r2109a 无红）——全部成立。

### 4. flake 裁定盘面 —— **PASS**

六件 flake 轮在盘、FAIL 员逐件现采：

| flake 件 | FAIL 员 | 在册名 |
|---|---|---|
| matrix_t1135_neg2restore_flake_round1 | r2097c（恰一） | ✓ |
| matrix_t1136_final_flake_r1 | r2097c（恰一） | ✓ |
| matrix_orch_t1136_verify_flake_r1 | r2024b（恰一） | ✓ |
| matrix_t1138_neg1_red_flake_r1 | r2097c（六行红卷整卷拷贝，见第 9 项） | ✓ |
| matrix_t1139_neg1_restore_flake_r1 | r2097c（恰一） | ✓ |
| matrix_t1139_neg2_restore_flake_r1 | red=r2097c（恰一） | ✓ |

**r2067c 新员裁定现证**：matrix_t1139_{pos,neg1_restore,neg2_restore,final} 四件 r2067c 全 PASS（三方相邻绿+final=四窗模式）✓；红轮唯一红行 `[fill got=1 left=5 n0=5]` 与"摘预检行不断其成功路径=非设计面"申报一致。观察名单扩四员（t897/r2097c/r2024b/r2067c）如实。五单 final 卷 r2097c/r2024b 全 PASS 在案。

### 5. 时序核 + 构建戳代次 —— **PASS**

- restore/final mtime 先后：五单全符（t1135 06:34/07:57<08:22；t1136 14:07/15:09<16:10；t1137 22:14/23:28<23:55；t1138 04:55/05:47<06:13；t1139 11:30/13:36<14:22）。
- 冒烟 build 戳（head14 grep "build:"）：
  - t1135：`2026-10-05 07:34 @ 675759d3`（**pre-commit 冒烟模式**，戳=audit #28 落档提交≠test 提交 4bcc5da2；dev-plan 如实申报"build 戳 675759d3"——INFO-2）
  - t1136：`2026-10-05 16:29 @ 32da1c67` = test 哈希 ✓
  - t1137：`2026-10-06 00:24 @ 8f096216` = test 哈希 ✓
  - t1138：`2026-10-06 03:37 @ 938ca286` = test 哈希 ✓
  - t1139：`2026-10-06 15:18 @ a610d904` = test 哈希 ✓（提交后重编，ledger 申报一致）
- 墙钟：t1135 "08:38:55"（与 head14 08:38:55.642 同窗）、t1136 1791189136=16:32:16 ✓、t1137 1791217760=00:29:20 ✓、t1138 1791229153=03:39:13 ✓、t1139 1791271315=15:21:55 ✓——全部与 banner/head14 同窗。
- 提交时序链（git log %ci）：五单均 fix→test→docs 顺序；t1138 整链提交后（pos 重跑 04:00>提交 03:32）；t1139 final 轮（14:22）先于提交（15:17/15:18）、冒烟后置（15:20-21）=ledger 申报一致。

### 6. 位面机械化 —— **PASS**

`ls build/matrix_t113{5,6,7,8,9}_*.log | grep -cv build` = **0**（从仓库根现跑）。t1135 的 stale/prerestructure 前身件亦全在 build/ 内。

### 7. 冒烟三件 —— **PASS（1 FINDING）**

五单三件全在盘非空，尺寸与账面逐字吻合，root objects 行独立 grep 采集全为 "root objects after load: 1"：

| 单 | full / head14 / rootobjects (B) | 账面 | 位置 |
|---| --- | --- | --- |
| t1135 | 137965 / 1070 / 54 | 54/1070/137965（dev-plan:4751） | build/ ✓ |
| t1136 | 174698 / 1070 / 54 | 54/1070/174698（dev-plan:4759） | build/ ✓ |
| t1137 | 157201 / 1084 / 54 | 54/1084/157201（dev-plan:4769） | build/ ✓ |
| t1138 | 1461 / 1084 / 54 | 1461/1084/54（t1138 台账） | build/ ✓ |
| t1139 | 200974 / 1070 / 54 | 200974/1070/54（t1139 台账） | **仓库根** ✗ → F-2 |

### 8. filtered 盘证纪律 —— **PASS**

| 单 | 现数 | 账面 | 总 FAIL 现扫 |
|---|---|---|---|
| t1135 | 21 | 21（保护门 3+r2087..r2104） | 0 |
| t1136（orch 补跑） | 22 | 22（filter_*_t1136_orch.log） | 0 |
| t1137 | 23 | 23（+r2106） | 0 |
| t1138 | 24 | 24（+r2107） | 0 |
| t1139 | 25 | 25（+r2108） | 0 |

t1136 验收 gap 的主控补跑件 22 件全在盘（dev-plan:4760 申报一致）。

### 9. 申报命题逐句对盘 —— **PASS（1 FINDING）**

**t1138 三勘误：**
1. "flake 轮归档实为 neg1_red 字节拷贝非 mv"——**坐实**：`md5sum matrix_t1138_neg1_red.log matrix_t1138_neg1_red_flake_r1.log` = 同值 `0726482d5271b0dbc1afd49c267a6e40`（现算）。
2. "七行两两互异为假命题"——**盘面自洽**：t1138_runlog_md5.txt 七行=六互异值+第七行与 neg1_red 同值（md5sum -c 全 OK）；"七行"假命题本身仅存于临时报告（盘上 t1138 test 消息写 "six whole run file md5s pairwise distinct"，为真）——勘误指向盘外命题，如实登记，INFO-3。
3. "amend 被 GateGuard 拦 → 消息保持预申报形、台账为实测权威"——**坐实**：test 消息（预申报"exactly red five legs"）与实测 975/6 的差异由台账第 16-22 行如实对账（第六行=flake 员已披露）；close 消息携勘误全文。

**t1139 断点台账：**
- 两次 429（12:39/13:25）为进程事件，无盘上直接证据（进程日志不在盘）；mtime 链间接一致（neg2_red 12:05→neg2_restore_flake_r1 12:45→neg2_restore 13:36=13:25 杀后由孤儿 exe 续完；final 14:22=主控）——INFO-6。
- "主控一次误读（neg1_red_flake_r1 系误读实为 restore 侧）"——**盘面一致**：matrix_t1139_neg1_red_flake_r1.log 不存在，neg1_restore_flake_r1.log 存在 ✓。
- 前身轮两件自申报在盘（pos_superseded_rig1 985/0 绿、neg1_red_superseded_negface1 985/0 绿）——现算双绿坐实（见 F-4 命名面）。
- 还原 md5 双录（playercontroller 1c841a03… / mapstore 97e9c75c…）工件在盘（t1139_neg1_pc_restore_md5.txt / t1139_neg2_mapcpp_restore_md5.txt）。

### 10. era 翻案抽核（三件）—— **PASS（1 FINDING）**

① **t1137 本体杆占位翻案——字节级坐实**：t1137 工件"abr.txt:155-190"为六参入口 `a(ry,int,int,int,int,int)`（abr.txt:412 起）内**字节码偏移**；现读 build/t1125_jar_disasm/abr.txt:497-517 逐字节命中工件引录：`155: aload_1`(本体格) → `160/163: yy.ac / qz.bM=36` → `168: ry.b:(IIIII)Z`(setBlock 本体格 36,dir) → `178: getfield abr.bM`(本体 id) → `187: qz.a:(IIIZZ)`(storedId=本体, extending=false, headFlag=true) → `190: ry.a:(IIILbq;)V`(setBlockTileEntity)。t1136"era 缩回无动程"确系不完整读、翻案成立（头格当拍清空半边 495-578 偏移段同在=维持）✓。引证记法混用面 → INFO-7。
② **t1138 acu.d 勘正——字节级坐实**：现读 acu.txt:39-136：字节 101-106 `abr.f:(I)Z`(bit8 检测) `ifeq 136` = **位清才 return**；109-125 `yy.b:(Lry;IIIII)V`(dropBlockAsItem) + 126-132 `ry.g(…,0)`（本体格清空）。恢复条件=背活塞**已伸** ✓；t1136"未伸孤儿态"系反读 ✓。
③ **F03/F04 仲裁——方向坐实、替换枚举有误 → F-1**：现读 build/t1125_jar_disasm/p.txt static{} 全块（190-377）：p.p=`new mw(aav.b).m()`=J=1 ✓（285-286）；p.D 链尾 `.n()`=J=2 ✓（474-475）；t1135"sn 恰二实例/p.p 无 m()"翻案成立 ✓。但**J=1 计数现算=14 例**（g,h,i,j,k,n,p,s,u,w,y,z,B,C——子类 m()/n() 无覆写，`grep "^\s+(protected\|public) p (m\|n)\(\);"` 全反汇编仅 p.txt:174/182 定义），仲裁"12 实例"清单漏 **n**（br.m()，p.txt:218-221）与 **u**（mw.h().o().e().m()，p.txt:312-342）；另 A=`bk.n()`=J=2（355-356）为仲裁未提及面。该 12 例口径已随 t1140 派工传播（auto-backlog:175、agent-state:26）。

### 11. 账本三方一致 —— **PASS（2 FINDINGS）**

dev-plan 五条目 vs agent-state 控制块 vs auto-backlog 划线注：五单哈希/腿数/矩阵口径（962→966→971→976→981→985）/canonical 逐字互核一致 ✓；t1139 清账表述（A04/A06/A07+R01）与外审移交件队列映射一致 ✓。
- **F-3**：agent-state 控制块窗口锚记 "a625490b..a610d904"——该区间实仅含 t1139 fix+test 两笔，与实审计窗 675759d3..a8fa0a9c 不符（记法 slip）。
- F-1 传播面：auto-backlog t1140 行与 agent-state next_task 均承"J=1 十二实例"错误计数。

### 12. producer-verifier 合并披露 —— **PASS（MED 已缓释）**

如实入档核：三层一致（a8fa0a9c 提交消息 "INTERRUPTION LEDGER…producer verifier merge honestly recorded"、build/t1139_evidence_ledger.txt 第 1/37-44 行、dev-plan:4793）。接尾段产出面 mtime 佐证：final 14:22（主控）、filter_*_t1139.log 15:23-15:40（提交 15:17/15:18 后=主控）、冒烟 15:18-15:21。
**评估意见**：本次合并使 t1139 的 final 轮+冒烟+filter+台账由"验收者"自产，本窗其余四单的"主控独立脱离式复跑"规范在 t1139 缺位（无 orch verify 件）；但四面恒等中三面为 agent/孤儿 exe 产出、仅 final 为主控产出=跨生产者恒等结构仍在，且本审计已独立复算全部恒等/恰红/filter/冒烟主张（全部成立），合并风险**有界且如实披露**。缓释措施=本次审计；建议下轮恢复"接尾主控产出后仍须一次独立复跑"惯例（可由 audit 或下轮主控补跑 orch verify 件）。

### 13. 派工简报纪律面（零 AI 署名尾注）—— **PASS**

`git log --format='%H|%B|END' 675759d3..a8fa0a9c` 全窗 16 提交正文扫描 `co-authored-by|generated with|claude|anthropic|noreply|assisted`：唯一命中=t1135 close 消息 "originally **generated with** the atlas rebaked to 212 tiles"（叙述句假阳性）；零 Co-Authored-By/零 AI 署名尾注/零 AI 元语。作者/提交者全为 CosmicalContainter。

---

## 二、发现台账

| # | 级 | 发现 | 证据 |
|---|---|---|---|
| F-1 | LOW | F03/F04 仲裁替换枚举错误：J=1 实为 **14** 例非 12（漏 n、u）；A=bk.n()=J=2 亦漏；已传播至 t1140 派工（auto-backlog:175 / agent-state:26）。方向性结论（p.p=J=1、p.D=J=2、t1135 两定谳被推翻）不受影响 | p.txt static{} 现读（本报告 §一.10③） |
| F-2 | LOW | t1139 冒烟三件落仓库根非 build/（五单唯一例外）；台账只写文件名未写路径。尺寸/内容/戳全符账面 | §一.7 表 |
| F-3 | LOW | agent-state 控制块窗口锚 "a625490b..a610d904" 仅覆盖 t1139 两笔，与实窗 675759d3..a8fa0a9c 不符 | §一.11 |
| F-4 | LOW | t1139_runlog_md5_pos.txt（53b96687…）持**替退轮** md5（=pos_superseded_rig1 现算同值），文件名未申报替退身份；现 pos（47e6c063…）无盘上工件承载 | md5sum 现算（§一.2） |
| F-5 | MED（已缓释） | t1139 producer-verifier 合并且无接尾后独立复跑件；三层如实披露+本审计独立复算全过=缓释 | §一.12 |
| INFO-1 | INFO | 简报"17 提交"为含基线含端点口径；实窗 16 提交 | git log 现数 |
| INFO-2 | INFO | t1135 冒烟=pre-commit 模式（戳 675759d3），dev-plan 如实申报；t1136 起改提交后重编 | §一.5 |
| INFO-3 | INFO | t1138 勘误#2 指向盘外命题（消息本写 six，为真）；盘面自洽 | §一.9 |
| INFO-4 | INFO | t1135 规范形全 32 位仅 dev-plan 承载（close 消息 8 位前缀）；t1135_canonical_md5.txt 持已勘正的自创口径 321d080d 未再生成 | §一.1 |
| INFO-5 | INFO | t1136 无 runlog md5 工件（七跑互异主张由本审计现算补证） | §一.2 |
| INFO-6 | INFO | t1139 两次 429 无盘上直接证据；mtime 链一致 | §一.9 |
| INFO-7 | INFO | "abr.txt:155-190"=字节码偏移记法与文件行记法混用（实文件行 497-517）；建议统一标注"bc 155-190 @ abr.txt:412-743" | §一.10① |

---

## 三、如实记录的复现失败/证据缺失面

1. 审计简报所记 t1135 账面值拼写在首查中与我方 grep 模式先后各有一次不匹配——以 dev-plan:4751 亲读为准定谳账面=`af91992e0e02a5cc54127a237c3d1054`，现算命中（非仓库缺陷，审计过程记录）。
2. t1139 两次 429 时点无盘上直接证据（进程日志不在盘），仅 mtime 链间接一致（INFO-6）。
3. t1136 无 runlog md5 工件、t1139 pos 现值无盘上工件承载（INFO-5/F-4）——均以现算补证后闭环，无申报矛盾。

## 四、窗口结论

**GREEN（with findings）**：五闭环证据链在盘、可复算、逐字对表通过；发现均为 LOW/INFO 级与一项已缓释的 MED；无 HIGH，无实质证据缺陷，无申报矛盾。t1139 接尾合并如实入档且经本审计独立复算兜底。t1140 派工简报的"J=1 十二实例"计数面应在实现时以现读（14 例全枚举）为准。

—— audit #29，2026-10-06。全部现算命令与数值已内联于各节。
