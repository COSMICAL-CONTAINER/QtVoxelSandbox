# t889 暂停语义统一 — 门控矩阵（调研产出）

> 调研基线：HEAD 5aba5a9（改造前）。改造 commit：250ba94。矩阵探针：redstone_matrix_test t889（261 PASS / 0 FAIL）。

## 语义分层（改造后）

两档闸，**单一权威派生**在 Main.qml `window.worldRunning`：

| 档 | UI 态 | captured | worldRunning | 语义 |
|---|---|---|---|---|
| 正常玩 | 无 UI | true | true | 全速 |
| **软档**（GUI 开） | 背包/工作台/熔炉/箱子/附魔台/铁砧/发射器/聊天/死亡屏 | false | **true** | **世界照跑**（火/水/实体/昼夜/熔炉/生长/天气），玩家**物理照跑但输入冻结**（照坠/照烧/照溺/照窒息/照饿，XZ 不动、不能转视角/挖/放/交互） |
| **硬档**（暂停） | ESC 暂停叠层（含设置/进度/统计/资源查看器子态）或主菜单/世界列表/加载 | false | **false** | **一切停**（机制等价 MC Java 单机 ESC） |

派生式（Main.qml）：`worldRunning = playing && (captured || 任一GUI面板 || chatOpen || dead)`；`pauseOverlay.visible === !worldRunning`（改绑取反消费，同源无漂移）。

## 改造前门控矩阵（普查结果 × 处置）

| 系统 | 驱动点 | 改造前：GUI 开 | 改造前：ESC | 改造后：GUI 开 | 改造后：ESC |
|---|---|---|---|---|---|
| 火/水/岩浆/生长×6/结冰/融冰/红石/树叶/天气 | Main.qml onTicked 桥（无门） | 跑 | **跑（分裂根源）** | 跑 | 停（WorldClock.running 停表） |
| 昼夜/太阳/月相/dayCount | WorldClock 100ms QTimer（无门） | 跑 | **跑** | 跑 | 停（同上） |
| 熔炉冶炼 / 云漂移 / 统计 playtime | onTicked 桥 | 跑 | **跑** | 跑 | 停 |
| 掉落物/经验球/船/矿车/mob AI/刷怪/压力板/TNT 陷阱/发射器陷阱/红石矿/按钮 | PlayerController::tickImpl 实体桶（无门常开） | 跑 | 跑 | 跑 | 停（worldRunning 早退） |
| 玩家物理/摔伤/窒息/溺水/火烧/饥饿/中毒 | step()（!captured 早 return 之后） | **冻结（分裂根源）** | 冻结 | **跑（软档 step 零输入）** | 停 |
| 玩家输入（WASD/视角/挖/放/吃/拉弓） | captured 闸 | 冻结 | 冻结 | 冻结（不变） | 冻结 |
| WASD 键入 m_keys | keyInput 透传（曾无门） | 入表（无害：step 不跑） | 入表 | **拦截（必须：step 照跑后防背包内走路）** | 拦截 |
| 钓鱼（浮标/咬钩/寿命） | EntityManager Bobber + updateFishing；release()/!captured 分支曾 cancelFishing | **收竿（t885 诉求）** | 收竿 | **照钓不收（t885）** | 冻结保活（墙钟顺延） |
| 箭 60s / 浮标 180s / 掉落物 5min / 经验球 5min 墙钟寿命 | 各管理器 m_clock.elapsed() | 流逝 | 流逝 | 流逝（Java 同） | **顺延**（deferWallClocks 三管理器，复跑补 pause 时长） |
| 死亡链 / 存档往返 / F5 / F3 | — | — | — | 尸体冻结（软档 skip step when m_dead）；门控态是运行期派生不落盘、load 后由 QML 重绑复位；F5/F3/F6 在 setKey 守卫之前不受影响 | 同左 |

## 钉死取舍（注释已落码）

1. **鱼线寿命墙钟暂停**：ESC 期 `deferWallClocks` 顺延（= 暂停期墙钟不走），非「保活但不顺延」——长暂停不烧穿 180s 寿命，机制等价 MC（一切计时冻结）。dt 镜像（arrowLife / 咬钩窗口）随 tick 停天然冻结，无需顺延。
2. **失焦 = 硬档**：WindowDeactivate/FocusOut → release → 暂停叠层 → worldRunning=false（机制等价 MC 单机失焦自动暂停）。
3. **死亡屏 = 软档**（世界照跑）但**尸体不受物理**（软档 step 跳过 m_dead）。
4. env 桶（水下滤镜/声景扫描）硬暂停仍跑：纯读冻结世界，零副作用。
