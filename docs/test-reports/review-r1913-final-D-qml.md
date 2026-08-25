# 架构测试报告 — review-r1913-final-D（QML 呈现层提交组审查）

> 审查窗口：08ffc71..6f78e7f（HEAD）。负责提交：5f9a40c（t828-t833 生物组呈现）/ 19e5577（review24 #7/#8 死亡链）/ e6b2681（t854,t855 装备覆盖+皮肤混搭）/ 7437d5a（杂项组 QML 侧：垃圾桶/画作/t838）/ 0a1acb5（t836 鱼线渲染）。
> 方法：本审查环境无 git diff 工具 → 按提交说明定位改动块，**HEAD 源码全量逐行核对**（Main.qml 巨大，grep 定位 + 上下文全读），逐式核验数学（四元数/盒体包络/face 映射）与 C++ 契约（dropAllItems/m_dead、NOTIFY 链）。行号以 HEAD 为准。

## 高危：0

## 中危（1）

### M1 死亡链 finally 面板标志复位漏了 t650 三面板 —— 与本批自我声明「任何辅助异常都拦不住标志复位」不符
> **已修（R19.13 收尾批 fix(review-r1913-final) 6f49f77，review 建议二选一 → 做更彻底的 both）**：① finally 标志复位段补 `enchantingTableOpen/anvilOpen/dispenserOpen` 三行幂等复位（置于既有 8 标志同段）；② `progress.onDeath()` 包独立 `try/catch`（与三行 returnCraftToHotbar 同款对称）——单行异常不再跳过 try 段的三个 close*()（附魔/铁砧槽物归还链保住）。
- **提交**：19e5577（review24 #7/#8）
- **位置**：`src/ui/Main.qml:10016-10050`（onDied try/finally）；对照 `respawnPlayer()` `:958-963`（不关任何面板）
- **问题**：finally 首部只复位 8 个标志（inventory/crafting/furnace/chest/chat/settings/progress/stats），**不含 enchantingTableOpen / anvilOpen / dispenserOpen**。这三个标志的清零只存在于 try 段的 `close*()` 调用里（`:10025-10027`）。而 try **首行** `progress.onDeath()`（`:10017`）没有独立 try 包裹——它一旦抛 TypeError（t603「window.progress 恒 undefined」/ t690「window.面板id 恒 undefined」同款笔误形态，本工程已两度实发），异常直接跳过 try 段剩余全部行：三个 close* 被跳过 → ① 三面板标志残留 true（`respawnPlayer` 不清理 → 重生后附魔/铁砧/发射器面板叠显，且 respawnPlayer 的 `grab()` 与面板开态混合）；② `returnEnchantToHotbar()/returnAnvilToHotbar()` 不执行 → 附魔/铁砧输入槽物品**不在 hotbar/main/held 里，dropAllItems 不覆盖 → 随尸体永久消失**（注释自评「归还被跳过即随尸体永久消失」）。finally 的死亡契约本体（dropAllItems/release/XP/播报）不受影响——review24 #7 的主目标仍达成。
- **触发场景**：`progress.onDeath()` 抛错（API 改名/返回值形态变化类笔误）时死亡；纯防御性缺口，当前代码无实发路径（`Q_INVOKABLE void onDeath()` 存在于 playerprogress.h:106）。
- **建议**（二选一，均三行内）：finally 标志复位段补 `if (window.enchantingTableOpen) window.enchantingTableOpen = false` 等三行（幂等，visible 绑定的延迟二次归还已有 t690(c) 幂等判）；或给 `progress.onDeath()` 包独立 `try/catch`（与三行 returnCraftToHotbar 同款对称）。

## 低危（4）

### L1 鱼线竿尖世界锚为单锚近似：不含 pitch、F5/第一人称共用（注释已声明、目视清单项未销）
- **提交**：0a1acb5（t836）
- **位置**：`src/ui/Main.qml:4432-4444`（fishingRodTipWorld：feet + 1.40 + 右 0.36 + 水平视线前 0.18）
- **问题**：锚只读 yaw 不读 pitch。第一人称手持钓竿是 viewModelHand billboard（相机本地 (0.36,−0.12,−0.39)，前伸 0.39 vs 世界锚前 0.18），强俯/仰时图上杆尖随 pitch 移动而线起点不动 → 线起点与杆尖视觉脱离；F5 第三人称手臂挂点同样共用此锚。注释自己声明「两 cameraMode 共用一手部锚……精确逐模式锚点留人工目视调，需目视清单项」。
- **建议**：目视验证清单落实后按模式分锚（或锚入 pitch 一阶修正）；销项前记录在案即可。

### L2 垃圾桶第二档「清当前选中槽」= 整槽清空，与用户原话「单件」存在口径歧义
- **提交**：7437d5a（t839）
- **位置**：`src/ui/Inventory.qml:690-706`
- **问题**：光标空手点击 → `setStack(selectedSlot, 0, 0)` 清空**整个选中槽栈**（非只销毁 1 件）。注释引用用户原话「只应销毁当前鼠标持有/单件」并把「单件」解读为「单格=整槽」。若用户本意是「单件=只销毁 1 个」（保留余量），现实现超范围。光标持半组时点垃圾桶 → 销毁的是**光标上那半组的全部**（heldBlock=0 清整个光标栈），与「清整组」语义自洽、无拖拽交互破绽（DragHandler 不在销毁槽上、右键归 root 半/均分手势独占、Shift=普通左键由 TapHandler 天然不分辨修饰键 → 修饰语义已完全退役）。
- **建议**：与用户钉死口径；若维持整槽，注释把「单件」字样改为「单格（整槽清空）」防后人误读。

### L3 皮肤 alpha 契约双轨并存：皮肤件 cutoff 实际依赖 opacity<1 混合路径，非 alphaMode:Mask（自定义皮肤半透像素边缘观感风险）
- **提交**：e6b2681（t855）
- **位置**：`src/ui/CharacterPreview3D.qml:232/250/272/292/319/337/365/382`（8 处新增）、`src/ui/Main.qml:2903-2918`（第一人称手臂）及既有皮肤件
- **问题**：terrain/cutout 段已迁 `alphaMode:Mask + alphaCutoff`（Main.qml:4128/4301，注释钉过「Qt 6.8+ 下 alphaCutoff 仅在 alphaMode 非 Opaque 时生效」），皮肤段沿用旧约 `alphaCutoff:0.5 + opacity:0.99`（不设 Mask）。后果：(a) 修复 alex 鬼影列起作用的是「opacity<1 → 透明通道尊重贴图 alpha」而非 cutoff 本身——若将来有人删掉 opacity 0.99「顺手简化」，cutoff 惰性 → 鬼影列整批回归；(b) pack 关态程序皮肤（无 alpha 通道，采样 alpha 恒 1）零影响 ✓；(c) 自定义皮肤 base 层若有 0.5<alpha<1 半透像素，混合路径下呈半透（MC 基准是 cutout 硬边）——仅非标皮肤触发。
- **建议**：皮肤件注释统一补一句「cutoff 生效前提 = opacity<1（6.8 前契约）；勿删 opacity 0.99」；长期可并轨 alphaMode:Mask（一次批改，视觉等价）。

### L4 CharacterPreview3D 护甲 overlay 仍是纯色档色 UnitCube，t854 仅同步了袖的几何高度（预览 vs 游戏内观感档差，注释声明有意）
- **提交**：e6b2681（t854）
- **位置**：`src/ui/CharacterPreview3D.qml:61-74`（armorColor 档色表）、`:278/:298`（袖 scale 已改全臂高 0.74 ✓）
- **问题**：游戏内（Main.qml）护甲已 t718 迁 ArmorLayerBox + layer 贴图，预览面板仍纯色档色盒 → 同一套装备在背包预览与 F5 下颜色/纹理不一致。注释声明「预览面板小、档色可辨」为有意保留，非回归。
- **建议**：维持现状可接受；若后续用户报「预览甲不像」再迁 ArmorLayerBox（几何/摆位已同步，只差贴图源）。

## 通过项（按 8 个审查点）

1. **死亡链 try 结构（19e5577）**：
   - `finally` 首部 8 标志复位 → `player.dropAllItems()` → `player.release()` → XP 球 → 播报，顺序与「契约本体最先、花絮殿后」声明一致（`:10042-10069`）。**dropAllItems 置 m_dead 已核**：playercontroller.cpp:4780-4783（`m_dead = true` → 抑制 pickupScan / t655 闸门）。
   - **close* 内 grab 时序验证（agent 报告复核项）**：closeEnchantingTable/closeAnvil（`:1202-1253`）= `returnXxxToHotbar()` → `returnHeldToHotbar()` → `player.grab()`；onDied 内序列 grab（try）→ dropAllItems 置 m_dead（finally）→ release（finally），全程同一 JS 栈无事件循环重入 → 无「死亡瞬间指针又锁回」窗口，**顺序安全结论成立**。
   - **三行归还独立 try**：`:10033-10035` 各自 `try{...}catch(e){}`，任一行抛不吞其余两行与 `returnHeldToHotbar` ✓；held 栈有 dropAllItems 兜底（hotbar+main+held 全掉）无需包 ✓。
   - `progress.onDeath()` 已入 try 首行（review24 #7 落实，`:10017`）✓；`deathCauseText` 属性访问不带括号（`:10069`）✓。缺口见 M1（三面板标志 + 附魔/铁砧槽归还挂在无保护的首行之后）。
2. **皮肤 alphaCutoff 全线（t855）**：CharacterPreview3D 8 处皮肤件全部补齐 `alphaCutoff:0.5 + opacity:0.99`（头/躯干/双臂/双腿四段）；第一人称手臂 PlayerSkinBox{piece:2} 化 + `eulerRotation.x=180`（local +Y=袖端翻向下 → 手纹素朝上，`:2893-2918`），几何对齐旧两段包络（中心 −0.045 长 0.29、粗 0.12 → t73 不穿模契约不变）；同贴图源 playerSkinTex/skinIsSlim → /skin 切肤、pack 开关、slim/classic 三态与第三人称同步。程序皮肤零影响（无 alpha 通道采样恒 1）✓。边缘风险见 L3。
3. **t854 护甲袖壳联动**：
   - 玩家：playerArmorSleeveL/R 挂 leftArmPivot/rightArmPivot 子节点（`:4785-4802`/`:4825+`）→ 走路 ±22° 摆、右臂挖掘 70° 挥臂全随动（F5 可见）；全臂高 (0,−0.35)@(0.30,0.74,0.30) vs 臂 0.7/0.25 → 两端探 0.02、侧面探 0.025，包络数核对无误。
   - Shambler：双臂前伸横置**静态**（mobmodel 臂盒心 (±0.33,0.23,−0.37) 半 (0.10,0.10,0.25) → 臂长 0.50/截面 0.20），袖同位 (0.24,0.54,0.24) → 沿臂长探 0.02×2、截面探 0.02×2 ✓；`eulerRotation.x=90°` 使 local +Y（12px 袖条带轴/肩端）→ 世界 +Z（躯干侧），旋向与纹素方向核对正确（Ry 不涉及、Rx +90° (0,1,0)→(0,0,1)）。静态臂无摆动动画 → 无脱位问题。躯干壳全高 0.60+探 0.04 修肚段裸露（`:7400-7403`）✓。
   - Bones：右袖挂**弓肩枢 Node**（`:7866-7901`，枢 (0.20,0.28,−0.02)、`eulerRotation.x = drawAmount×75` 与 MobModel aimPitch 同值同枢）→ 拉弓抬臂刚体随动不脱位；臂心相对枢 (0,−0.325,0) 算术核对正确；左袖竖臂同位全高无旋转（`:7942-7954`）✓。
   - 护腿/靴枢轴（t560）walkPhase 同幅同相不受本批影响 ✓。预览侧档差见 L4。
4. **鱼线四元数（t836）逐式核**：目标 = 本地 +Y 旋到 d̂。① axis = up×d̂ = (dz,0,−dx)/len（`:4453` 叉积展开逐项核对无误）；② cosθ = clamp(dy/len,−1,1)（acos 定义域防 NaN）✓；③ q = (cos(θ/2), sin(θ/2)·n)，`Qt.quaternion(标量,x,y,z)` 参数序与 QQuaternion(w,x,y,z) 一致 ✓；④ 最短弧旋向验证（up 绕 up×d̂ 右手 θ 恰落 d̂，取 d̂=+X 手算复核）；⑤ 零向量 → identity + lineLen 下限 0.05（线缩点不可见，防御）；⑥ 平行/反平行 alen<1e-5 → 任选正交轴 (1,0,0)：θ=0 时 q=identity（正确），θ=π 时 q=(0,1,0,0) 绕 X 翻 180°（+Y→−Y，正确）——浮标正头顶/脚下两极端角均无退化。UnitCube ±0.5 居中（unitcube.h:7）→ 中点定位 + scale.y=连线长摆位正确 ✓。`bobberPosition/hasBite` NOTIFY 均为 fishingChanged（playercontroller.h:188-190，值变才 emit）→ 线/浮标每 tick 重算链成立 ✓。浮标红顶 #d83838 + 白杆 #e8e4dc 双件、咬钩下沉 0.15（hasBite 门控）✓。锚近似见 L1。
5. **垃圾桶两档语义（t839）**：① 光标持有（含半组）→ `heldBlock=0` 清**整个光标栈**（setHeldBlock(0) 连 count/耐久/附魔/名同步清）；② 光标空 → 仅清当前选中槽单格（`setStack(selectedSlot,0,0)`），绝不波及其余 8 槽；③ 仅左键（右键归 root 拿半/均分 TapHandler 独占）；④ Shift=普通左键（TapHandler 不分辨修饰键 → t700 旧修饰语义完全退役，注释明示）✓。半组交互无破绽（销毁槽不在调色板网格、无 DragHandler）。口径歧义见 L2。
6. **画作朝向（t837②）**：取值源 = `lookDirection()` 水平分量**主导轴比较**（|x|≥|z| 对角 45° 归 X 向，playercontroller.cpp:3711-3714），非 yaw 取模 → 无负数取模/环绕边界风险；反方向映射核对：看 +X→face1(−X)、看 −X→face0、看 +Z→face3(−Z)、看 −Z→face2，与「画面恒面向玩家」声明一致。QML delegate face→yaw 映射（0→90°/1→270°/2→0°/3→180°，Main.qml:9062-9071）逐向手算核对（Ry(90°) 把 local +Z 法线转到各墙面外法线）✓，+X 观察者右向不镜像成立。斜角/掠射命中由 tryPlacePainting cellOk 逐格复检墙格实体性兜底、放不下 no-op 不消耗物品 ✓。「horizontalFacing 同源」为编码族约定（0=+X 1=−X 2=+Z 3=−Z 全仓一致）非共享函数，声明成立。t838 玻璃 3D 全在 C++（resourcepackmanager.cpp:2551/2972 + hotbar.cpp:194/238 程序图集重渲），**QML 无份** ✓。
7. **染羊 tint + 脸罩契约（t832 消费侧 / t816/t777 不回归）**：毛层 baseColor 绑 `revision + sheepWoolTintAt`（`:7120-7127`）→ 染料右键即 bump revision → 绑定重算**即时变色** ✓；脸罩 `visible: sheepTintDyed(index)`（任一分量 <0.999 判非白），罩色 `sheepFaceCoverTint = 裸肤 #d6b890 × 昼夜`（`:2043-2048`）——**不随染**（t777「脸=skin 层不 tint」契约保持）；t777 pack 合成脸判据（`mobSheepPackTex 命中 && sheepWoolFaceActive → 隐 overlay 眼`）原样保留，染态强制显眼分支（`|| sheepTintDyed`）使罩盖 pack 真脸后眼仍可见（`:7160-7163`）✓。狼项链（t831）：`wolfTamedAt` revision 绑定即时显隐、随坐姿变换（压缩+后倾）继承、红 #c22828×昼夜 + 受击红覆盖 ✓；坐站（t480 同款变换 wolfSittingAt/ocelotSittingAt revision 即时切姿）✓；驯服爱心 = C++ `inLoveAt = loveTimer>0 || tameHeartTimer>0` 合一（entitymanager.cpp:1929），QML 复用既有 t400 心形 Model 零新节点、驯服瞬间即显 ✓。
8. **新 delegate 常驻成本（t811 先例对照）**：鱼线/浮标 = **场景级单例** 4 个 Model（`visible: player.fishing` 门控，`:4406-4480`），非 per-slot 实例化——t811 病（每槽全类型实例化 ~8000 节点）不犯；狼项链/眼为 per-type Loader（wolf Loader 仅狼槽 active）内 +1 Model 量级，与眼/尾同档；水花两档（burstWaterSplash 10 颗 vY3.2 vs burstWaterEscape 5 颗 vY1.6，BlockParticles.qml:93-100）走既有 Model+Timer 粒子池零 new、池满静默丢 ✓。

### 架构 / 约定 / IP 门
- **分层（§2）**：全部改动为呈现层只读消费（VM Q_PROPERTY / PlayerController 镜像 / EntityManager revision 系），无反向写；死亡链 UI 编排（关面板/归还/转发 dropAllItems）沿用既有模式；鱼线 tip 计算纯读 yaw/feetPosition。✓
- **约定一致**：NoLighting 红线全部可见 Model 遵守；QUrl 判空 `toString().length`（t497 铁律）在新写的 pack 判据处保持；字面量 id + 注释同源模式（torch=13 同款）沿用；中文注释密度与既有文件一致。✓
- **RHI 囚笼（§2-A）**：本批 QML/C++ 改动无 QRhi/QShader 触碰。✓
- **IP 门（§9）**：改动区无新专有名词标识符/用户可见字串；垃圾桶图标 Canvas 自绘原创、项圈/浮标/线色值原创数值、水花色原创；皮肤/贴图走两态（pack 用户自供 / qrc 程序自绘）。「MC 词根」为 R19.12 已登记遗留改名批（非本批引入、不在本批恶化）。✓

## 总体判定：PASS

0 高危 / 1 中危（M1：死亡链防御性收口缺口，条件触发、契约本体不受影响）/ 4 低危。八项审查点核心结论全部核实（含 agent 自称已复核的 close* grab 时序——**成立**；手写四元数——**逐式正确**）。M1 建议随下批三行收口（finally 补三面板标志复位或 progress.onDeath() 独立 try）；L1 需目视清单销项。

---

## R19.13 收尾批处置记录（fix(review-r1913-final) 6f49f77)

- **M1 已修**：finally 补三面板幂等复位 + progress.onDeath() 独立 try（both，详见 finding 处标记）。
- **L1 登记不修**：鱼线竿尖单锚近似（注释已声明，目视清单项销口时按模式分锚）。
- **L2 半修（注释澄清）**：Inventory.qml 销毁槽注释「单件」字样改「单格（= 整槽清空，非只销毁 1 件）」防误读；语义口径（是否只销毁 1 件）待用户裁定，登记。
- **L3 登记不修**：皮肤 alpha 契约双轨（长期并轨 alphaMode:Mask 留一次批改）。
- **L4 登记不修**：预览面板护甲纯色档差（注释声明有意，用户报观感再迁 ArmorLayerBox）。
- 验证：全量重编零警告（dxcompiler.dll 提示豁免）；矩阵 252 PASS / 0 FAIL；exe 冒烟 14s 双实例存活零 QML 错误。
