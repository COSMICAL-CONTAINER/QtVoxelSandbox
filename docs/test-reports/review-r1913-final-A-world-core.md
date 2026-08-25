# 架构测试报告 — review-r1913-final-A（World/Core 提交组审查）

> 审查窗口：08ffc71..6f78e7f。行号以 HEAD 为准；方法为逐项读 HEAD 源码交叉核对 `docs/Review_2026-08-24.md` ✅ 标记 + 分层/约定/IP 门。

## 高危：0

## 中危（1）

### M1 armorLayerSource 皮革族与 itemIconSource 皮革图标段落盘缓存无 revision、直返裸 `file:///` —— #5 同构 cache-bust 病残留
> **已修（R19.13 收尾批 fix(review-r1913-final)）**：两族文件名挂 `_r%1(s.revision)`（`voxelsandbox_rp_leather_<id>_r<rev>.png` / `voxelsandbox_rp_leather_layer_<n>_r<rev>.png`，skin/mobhead 先例）；皮革段全部回退直返（解码失败 / 无可写目录 / 落盘失败）改走 `packFileUrl(path, s.revision)` 挂查询串；缓存命中路径随文件名世代变化自然 cache-bust。reset 段清扫扩皮革族（见 L3，一并修）。
- **提交**：20a0efa（收尾只改了非皮革直返族）
- **位置**：`src/Core/resourcepackmanager.cpp:3163-3168`（`voxelsandbox_rp_leather_layer_<layer>.png` 文件名无 `_r`；3153/3156/3161/3166/3168 五处裸 `file:///` 返回）、`:2398`（皮革图标 `voxelsandbox_rp_leather_<id>.png` 同病）
- **问题**：packFileUrl 头注释（`:2066-2067`）宣称"落盘派生缓存族……revision 进文件名，同效更稳"，但皮革两族文件名均不含 revision。apply() 清 `leatherIconFiles`（`:1715`）后重染**同名覆盖**，URL 不变 → QML Texture 按 URL 缓存继续用旧像素。
- **触发场景**：pack A → 原地换 pack B（armor/物品皮革模板路径不变）→ apply → 皮革护甲图标与皮革甲层贴图陈旧直到重启（Review #12/#5 同款病）。Review ✅ 标记如实声明"只修非皮革直返族"，非虚标，但此缺口客观存活。
- **建议**：文件名挂 `_r%1(s.revision)`（同 skin/mobhead 先例），直返改走 packFileUrl 或带 rev 文件名；ensureBuiltLocked reset 段顺带加皮革族 stale 清扫。

## 低危（3）

### L1 门配对判定缺 bit3 互补/同材质校验（Review 08-24 低危已登记、本次未修——遗留非回归）
> **登记不修（R19.13 收尾批）**：异常存档态防御项，无正常游玩触发路径；留待后续 `doorPartnerAt()` 谓词收口批。
- **位置**：`src/World/world.cpp:1710-1717`（整扇湿判）、`:1725-1737`（联动点燃）、`:1625-1632`（(d) 烧尽收尾）三处均只验 `isDoor(partner)`。
- **触发**：损坏档/第三方写档产生异常 state（两半扇同 bit3、同列异门相邻）→ 整扇联动/湿判/烧尽收尾可能波及无关门半扇。
- **建议**：抽 `doorPartnerAt()` 谓词三处共用，补 `(partnerState & 8)` 互补校验。

### L2 recheck 注释「至多 1 次叠加 emit」口径对火把族不精确（代码行为正确）
> **已修（R19.13 收尾批）**：注释改「每实际掉落格至多 1 次自 emit——check 族柱顶 ≤1 趟 + 火把族逐侧挂格各 1 次」，一行顺手修。
- **位置**：`src/World/world.cpp:2716-2718` 注释。
- **问题**：dropGravityColumn 循环内每清一格调 recheck；柱中段格**侧挂**火把逐格掉落时每格各一次自 emit（N 侧挂火把 = N 次中间 emit + 1 收口）。注释只论证了正上方 check 族"柱顶至多 1 趟"。
- **影响**：每次 emit 前标脏已落、clearAllDirty 即时收口，无幽灵终态；坍落稀有性能可接受——纯注释精度。
- **建议**：注释改「每实际掉落格至多 1 次自 emit（check 族柱顶 ≤1 + 火把族逐侧挂格）」。

### L3 mobhead/sheep/nightwalker 族磁盘残留缺 reset 段兜底（与 skin 族不对称）
> **已修（R19.13 收尾批，随 M1 同段代码）**：ensureBuiltLocked reset 段清扫从 skin 单前缀扩为五前缀无差别清（skin / leather（两族合一前缀）/ mobhead / sheep_woolface / nightwalker_chin），逐前缀各清各的互不误伤；生成失败 mobType 的旧 `_r` 代由此兜底清除。
- **位置**：`resourcepackmanager.cpp:1721-1735`（reset 段只无差别清 skin 族）；`:3638-3650`（mobhead 逐版清理挂在"生成成功后"）。
- **触发**：apply 重建时某 mobType 生成失败（包缺贴图）→ 该条目旧 `_r0` 磁盘残留（无 URL 引用，纯缓存膨胀，无正确性影响）。
- **建议**：reset 段清扫扩至 `voxelsandbox_rp_mobhead_*` / `sheep_woolface_*` / `nightwalker_chin_*` 前缀。

## 通过项（按 8 个审查点）

1. **#1 整扇湿判**：`igniteFlammableAt`（world.cpp:1698）三入口统一收口——主目标湿判 :1703、配对半扇湿判 :1710-1717（任一半含水 → 整扇 return false）；(b) 蔓延 :1568、(d) 同态 :1647 均经此函数；打火石直燃 `playercontroller.cpp:3661` 同入口。(d) 烧尽收尾 :1626-1632 补 `!fireWaterNeighborAt(对偶)`（:1628），"点燃后才泼水"边角同护。✅ 标记与代码一致。
2. **#2 自 emit 契约**：`torchDropped`（:2687）仅实际掉落 ≥1 时自 emit（:2719-2722），无掉落零 emit。clearBlockSilent 主 emit（:686-687）在 recheck（:693）**之前** → 自 emit 恰补红石火把清格的重建信号；dropGravityColumn 收口 emit（:2656-2657）在循环后 → 叠加幂等。无双副作用：火把附着格唯一命中（:2698），blockDroppedAsItem 每把恰一次。
3. **#3 两兄弟路径**：destroySphereSilent :2275-2279、tickLavaFlow :1468-1471 替换为 recheck 单一入口 + 显式 checkGravityBlockOnEdit + breakNetherPortalsAround（岩浆焚毁路径补 #27 熄门钩）。**无双掉**：check 族守卫"oldId 非本族"（:2676；checkCactusOnEdit :2317 实证）防直破双掉；爆炸球内格由 caller 按 destroyed 列表处理。**无递归**：recheck 刻意不含 gravity 钩子；dropCactusColumn/dropSnowLayerColumn/dropSugarcaneColumn/dropUnsupportedDoorsAbove（:2292/:2580s/:2539/:2479）全静默直写不回调 recheck。tickLavaFlow 批量段中间 emit 同 setWaterSilent 挂 checkRailOnEdit 既有先例，终态不受破坏。
4. **#4 revision 前移**：apply() :2202-2204 先 `++s.revision` 再 `s.built=false` + `ensureBuiltLocked()`——构建期 mobhead 预生成（:2050）以新世代号落盘；首建 rev0 → 首次切包即 `_r1.png`（URL 变 → QML 重载）。旧 `_r0` 由生成清理段（:3643-3649）删除；`mobHeadIconFiles` 随 reset（:1718）清空重填新路径，消费端无旧 `_r0` URL 引用。其余 ensureBuiltLocked 调用者（构造 :2138 + 各查询懒构建）不 bump，无兼容破绽。磁盘残留面见 L3。
5. **#5 packFileUrl 五族**：packFileUrl（:2068-2071，全局供探针 extern）六处收口——playerSkin 64×32 直返 :3299、entitySource probe :3084/:3088、mobTextureSource probe :3389/:3393、effectIconSource :3004、paintingSource :3027、armorLayer 非皮革 :3148。**toLocalFile 剥查询串契约**：全仓消费端仅 2 处（:3414 羊 / :3427 夜行者），QUrl::toLocalFile() 只取 path、query 独立剥离，合成器寻址不受影响；QML file URL 带查询串模式已经 8886325 皮肤族实证。缺口见 M1。
6. **recipe static_assert 逐位核对**：94/95（recipe.cpp:1213-1214 ↔ blockregistry.h:552/:563 ↔ Main.qml:488/10092/10192 的 94 ↔ EnchantGlyphFlow.qml:105 / EnchantRunes.qml:73 的 !==95）；63/77/27/连续（:1199-1205 ↔ blockregistry.h:175/:450-464/:1076-1077 ↔ Main.qml:1997 `idx>0 ? 63+idx-1 : 27`，idx=15 → 77 恰等 WoolBlack）。15 色无洞断言与 16 色（白 27 + 15 变体）结构吻合。全部一致。
7. **hashVoxel 单一权威**：world.h:59 升 public（const 纯函数向下零依赖；private 段 :1177 留指路）；唯一定义 world.cpp:4626；跨层消费 entitymanager.cpp:5632/:5701（Entities→World 依赖向下合法）。无副本漂移：cheststore.cpp:227-228、itementitymanager.cpp:103 为自带盐独立散列（注释如实声明"同模式"非复用；PLAN §2-K 只约束确定性+禁随机源）。
8. **✅ 标记交叉核对**：#1/#2/#3（d97f465）、#4/#5（d6051e6）、armorLayer 非皮革 + 羊毛上界（20a0efa）、94/95 钉（65636e4）全部与 HEAD 一致无虚标；WriteBuildStamp TIMEOUT 5（cmake/WriteBuildStamp.cmake:38，超时 → nogit 回退）与探针 nogit SKIP（redstone_matrix_test.cpp:7152-7172）口径对齐；r24#1/#2/#3 探针块在位（:9431/:9491/:9537）。
9. **架构门**：改动文件 include 方向无越界；零 Renderer 触碰；IP 门——新增标识符（packFileUrl / recheckAttachmentsAfterClear / torchDropped / bobberWaitSeconds / kBobberWaitHashSalt 等）全原创，无新专有名词字面量或资产入库。

## 总体判定

五个提交的修复本体全部成立且非死码，Review_2026-08-24.md 的 ✅ 标记与 HEAD 代码逐条一致、无虚报；分层铁律与 IP 门干净。遗留 1 中 3 低：M1 是 #5 修复自己声明"留后续"的皮革落盘族未竟部分（非新回归，修法同 skin/mobhead 先例，半小时级），L1 为上轮已登记遗留，L2/L3 为注释精度与缓存卫生。**判定：通过（附 M1 一项中危跟进，建议列入下一收尾批）。**

---

## R19.13 收尾批处置记录（fix(review-r1913-final)）

- **M1 已修**：皮革两族文件名挂 `_r<revision>` + 回退直返走 packFileUrl；同 L3 一并扩 reset 段五前缀清扫。
- **L1 登记不修**：门配对 bit3 互补/同材质校验（异常存档态防御，留 `doorPartnerAt()` 收口批）。
- **L2 已修**：recheck 注释口径一行改（check 族柱顶 ≤1 + 火把族逐侧挂格）。
- **L3 已修**：随 M1 reset 段扩展（mobhead/sheep_woolface/nightwalker_chin 前缀兜底清扫）。
- 验证：全量重编零警告（dxcompiler.dll 提示豁免）；矩阵 252 PASS / 0 FAIL；exe 冒烟 14s 双实例存活零 QML 错误。
