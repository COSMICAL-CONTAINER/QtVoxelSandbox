# 性能调研批（t904 / t905 / t906）— 2026-08-28

用户 F3 快照锚点（2026-08-27 15:30 @ 6b1405d，18FPS）：`main*46.6 render 4.6 qmlSync 2.7 residual 32.8`、
`mob 10.27`、`mob sub: ai 0.00 phys 10.26`、`win: sim 11.18`、`ice 2.5`、`mesh 0.00(0reb)`、`threads: 0/0`。
本批基线：矩阵 325 PASS → 326 PASS（新增 t905 契约探针）；构建零警告。

---

## t904 — residual 32.8ms 归因

### 已落地（本批 commit）
插桩补面：GUI 线程一帧周期按渲染管线 hook 切四段（main.cpp）+ F3 新 `frame2` 行（frameprofiler.cpp）：

```
frameSwapped ──idleA──> afterAnimating ──waitSync──> beforeSynchronizing ──qmlSync(既有)──> afterSynchronizing ──idleB──> 下一 frameSwapped
frame2 ms/f: evA X  waitSync Y  idleB Z   (residual ≈ evA+waitSync+idleB; evA = idleA − sim)
```

- **evA**（idleA−sim）= QML 绑定求值 / 非 sim 的 QML Timer（50Hz BlockParticles 等）/ 事件派发 / 纯空闲；
- **waitSync** = GUI 阻塞等渲染线程同步屏障（渲染/present-vsync 拖帧节奏 → 渲染侧 bound 的标志）；
- **idleB** = sync 放行后到 swap 的等待；basic（单线程）渲染循环下 = 渲染本体在 GUI 线程跑。
- 某段恒 0 = 对应 hook 未发（basic 循环不发 afterAnimating → evA/waitSync 恒 0 本身即判据）。

**验证**（菜单态实测）：`main*16.0 render 0.6 qmlSync 0.0 residual 16.0` ↔ `frame2: evA 14.9 waitSync 0.5 idleB 0.6`
—— 四段和 = main_total（16.0），归因闭合。健康帧率下 residual ≈ vsync 等待（符合 frameprofiler.h 既有预期）。

### 从旧快照可推导的硬事实（无需复测即成立）
1. **口径**：所有 `ms/f` 桶共用分母 = **tick 帧数 m_frameCount**（16ms 游戏 tick 计数），非渲染帧数。
   `main*46.6` ⇒ 该 1s 窗口 tick 数 ≈ Σmain_total/46.6 ≈ 960ms/46.6 ≈ **21 tick/s**（正常 62.5）。
2. ⇒ **游戏 tick 定时器被饿到 ~1/3 频率**：16ms PreciseTimer 只送达 ~21 次/s = 事件循环被长事件/阻塞切割，
   错过的定时器间隔被 Qt 合并（丢拍不补发）。
3. ⇒ sim 聚合 CPU 仅 ~21×11.18 ≈ **235ms/s**——**帧率不是 sim 聚合量bound**；GUI 线程 ~720ms/s 花在
   tick/sync 之外（= residual 实体）。mob 桶聚合 ~215ms/s 是 sim 内大头（见 t905）。
4. render_cpu 仅 4.6ms/帧 → 渲染线程 CPU 侧轻；若 waitSync 复测大，指向 present/vsync 节奏而非绘制量。

### 根因判定（待复测闭合）
插桩已闭合归因路径但**用户快照无 frame2 段**，根因判定待新版本复测。判读树：
- `waitSync 大`（如 20-30ms）→ GUI 等渲染屏障（present/vsync/驱动节奏）→ 查渲染侧（帧节律、D3D 提交）；
- `evA 大` → tick 外主线程重活：QML 绑定扇出（47 槽 mob delegate 高水位、F3 文本重建、50Hz Timer）→ 查 QML 侧；
- `idleB 大`（≈render_cpu+）→ 正常 threaded 循环等待或 basic 循环（结合 waitSync 恒 0 判定）。
- 复测同时看首行 `prof[1s] Nfr`：N 显著 < 60 = tick 饿拍实锤（上述事实 2 的直接读数）。

**登记**：调研完成（插桩落地 + 口径事实推导）；根因判定与修复留待用户复测 frame2 行（指引见文末）。

---

## t905 — mob phys 10.26ms/帧 + ice 2.5ms/s

### A. ice 2.5ms/s —— 根因已定位并修复 ✅
**根因**：`tickIceFreeze`（"ice" 桶）每 5s 节流窗遍历**全部水格索引** m_waterCells，逐格第一判定就是
`biomeAt(x,z)` —— biomeAt 单次调用最多 **5 条 4 阶 fBm（~20 次 Perlin noise2 采样，~206ns/次）**，且是首个
判定（非雪原水格也全价支付后才 continue）。c282bc0 的增量索引只消除了「全图 3.28M 格扫描」，没消除
「每格的群系判定成本」—— 水格多的世界（海域/沼泽/湖）每 5s 一次 ~12ms 主线程尖峰（2.5ms/s 均摊吻合）。

**修复**：`World::biomeAt` 加列级 memo（W×D 扁平字节缓存，懒填充；generate/beginLoad 失效 + 尺寸自检兜底）。
群系纯函数于 seed（§2-K）运行期不变 → 语义零变化（fBm 本体原样迁 `biomeComputeAt`）。

**实测**（矩阵 t905 探针 diag，64×64=4096 列全图）：
- 冷（修复前每窗每格成本）：843µs / 4096 = **206ns/格**
- 暖（修复后命中）：6µs / 4096 = **1.4ns/格**
- **比值 128×**。按旧快照反推（12.5ms/窗 ÷ 206ns ≈ 6 万水格），修复后 ice 窗预期 <1ms（剩余为
  unordered_set 迭代 + blockAt/stateAt/skyLightAt 三查）——F3 ice 桶修后数字待用户复测确认。
- 契约探针（永久入矩阵，326 PASS 之一）：冷/暖两遍一致、换 seed 缓存失效、同 seed 跨实例一致（memo 路径
  == 纯计算路径确定性）。
- 附带收益：tickWeather / F3 biomeIdAt / heightAt（内部调 biomeAt）全路径 O(1) 化。

### B. mob phys 10.26ms —— 解剖插桩落地，热点定位待复测
**排除**（快照已证）：ai 0.00（t890 火扫描/仙人掌/AI 全在 aiTick 节流块内，非本次 10ms 主体）；
hostile/spawn 0.00；mobLoop==mob 桶（tickVehicleRiding/tickHostileLife/tickSpawners ≈ 0）→ 10.27ms 全在
`EntityManager::tick` 主循环的**非 ai 段**。

**落地插桩**（entitymanager.cpp + F3 mob 行扩展）：
- `[head X tail Y ltail Z]`：head = 非 Mob kind 分支（箭/雪球/铁砧落体…）+ Mob 入块前置（尸体倒计时/
  骑乘冻结/空槽跳过）；tail = ai 后每帧段（流推/走相/击退/滑流/窒息节流帧/resting 复探/重力/落地扫描）；
  ltail = 循环尾（releaseSlot/flushPendingShots/tickBreeding/**emit entitiesChanged 的 QML delegate 扇出**）。
- `st[R a F b V c D d]` 状态直方图（每窗 mob-帧数）：resting / 非 resting（重力+落地扫描每帧跑）/ 骑乘 / 尸体。

**假设链**（按嫌疑排序，复测一测定音）：
1. **ltail 大**（emit 扇出）：kEmitEveryN=3 节流后仍每 3 tick 一次 `entitiesChanged` → 47 槽 delegate × ~12
   revision 绑定重算 + 行走 mob MobModel 几何重建（既有注释：历史上「mob 22ms 恒定」主因即此，t500 节流缓解）。
   快照自洽：10.26ms/tick × ~21tick/s ≈ 215ms/s ≈ 每 3tick 一次 ~30ms 扇出的均摊。若证实 → 修法方向：
   delegate 绑定瘦身（按实体粒度 notify 而非全局 revision）/ delegate 只对活体扇出。
2. **st F 高 + tail 大**：resting↔下落振荡（薄地板/雪层 ULP 残差族，注释里有先例）→ 每帧重力+落地扫描+dirty。
   修法方向：振荡格定位（坐标即可反查支撑形态）后对症。
3. **head 大**：骑乘矿车 mob 多（骑乘 continue 在 head）或投射物滞留 —— 结合 st[V] 读数。
4. review26 #1 脚位格豁免（mobAabbHitsSolid y==y0 的 supportTopYAt）只在移动试探时触发，量级不足以独占 10ms，
   若 tail 大且 st 全 R 再回头细查。

**矩阵探针局限说明**：矩阵直驱无渲染帧、无 QML delegate，mob 的毫秒必须在 exe 实跑下量——这正是本批
插桩面向 F3 的原因。

**登记**：ice 部分 ✅ 修复（数字如上）；mob 部分「调研完成（解剖插桩落地 + 嫌疑链排序），热点定位待复测」。

---

## t906 — `threads: 0/0 (sync meshing)` 核实

**结论：非线程池退化 —— 同步 meshing 是从未线程化的现状事实，F3 标签如实。**

证据：
1. `src/` 全树 grep `QThreadPool|QtConcurrent|QThread|moveToThread|createThread` = **零命中**。不存在可退化的池。
2. ChunkManager（src/World/chunkmanager.cpp）是纯 chunk 网格容器（路由/标脏），不建任何线程；
   mesh 由每 chunk 的 `ChunkGeometry::onWorldChanged` 同步直连槽驱动（setBlock → worldChanged → 重建，
   全在 GUI 线程 setBlock 调用栈内）。
3. 该字符串是 Main.qml（draw-calls 行尾）的**硬编码**字面量，非线程池状态读数（已补注释钉死，防再误读）。

**影响评估**（结合快照）：
- 稳态零重建：`mesh 0.00(0reb)` —— 18FPS 时同步 meshing **不是**持续掉帧因素；
- 重建以突发形式落在 GUI 线程：破/放（dirty 驱动，仅脏 chunk + t383 精确光照标脏）、太阳步进
  （sunRebuildDue 量化门 ~3.3s/次 + t472 chunkInRange 视距门控）、水翻页已静态化（tXXX）。
  1.43M 顶点全量重建（进世界/换 seed）是一次性长突发的已知成本。
- **登记**：异步 meshing = 架构级未来工作（工作线程构建 + 原子换入；chunkgeometry.h「不变量 B 形」注释
  已声明 mesh 数据 own/move-only/不可变即为线程化预留的形）。本批不修（非缺陷，改造超出调研批范围）。

---

## 复测指引（用户，新构建 exe）

1. **同场景复现**：进同一存档（160×160×128，原站位），F3 开 ≥10s，抄 3-5 个 1s 窗口的四行：
   `frame` / `frame2` / `mob sub`（现含 [head/tail/ltail] + st[]）/ 首行 `prof[1s] Nfr`。
2. **判读**：
   - residual 大 → 看 frame2：waitSync 大=渲染侧节奏；evA 大=QML/tick 外事件段；idleB 大+waitSync 恒 0=basic 循环。
   - Nfr < 60 = tick 饿拍（事件循环被长事件切割）实锤。
   - mob phys 大 → 看 [head/tail/ltail] 与 st[]：ltail 大=emit 扇出（假设 1）；tail 大+st F 高=resting 振荡（假设 2）；
     head 大+st V 高=矿车骑乘面。
   - ice 桶：预期从 ~2.5 降到 ~0.5 以下（每 5s 窗剩余为索引迭代 + 三查）。
3. kill @e 对照组：清场后同站位再抄一组（用户已做过 —— 新 instrumentation 下这组数据直接分离 ltail/evA）。
4. 日志兜底：`logs/voxelsandbox.log` grep `vo.prof`（每秒一行全量分解，F3 不开也有）。

## 本批改动清单
- `main.cpp` — t904 四段 hook（afterAnimating/beforeSynchronizing/afterSynchronizing/frameSwapped 结算 fIdleA/fWaitSync/qmlSync/fIdleB）
- `src/Core/frameprofiler.cpp` — frame2 行 + mob 行 [head/tail/ltail] + st[] 直方图
- `src/Entities/entitymanager.cpp` — mobHead/mobTail/mobLoopTail 跨迭代计时 + mobSt{Rest,Fall,Ride,Dead} 计数
- `src/World/world.h` / `world.cpp` — t905 biomeAt 列级 memo（+biomeComputeAt 本体迁移、generate/beginLoad 失效）
- `tools/redstone_matrix_test.cpp` — t905 契约探针（memo 等价/失效/跨实例一致）+ 冷暖计时 diag
- `src/ui/Main.qml` — F3 叠层注释补 frame2/mob 细分说明；threads 0/0 行注释钉死 t906 结论
