# 体素沙盒开发计划（依据 docs/PLAN.md Phase 1.0）

> 设计权威：`docs/PLAN.md`（§2 不变量 A–M + §4 Phase 1.0 范围/验收）。本文件把 Phase 1.0 拆成可独立验证的任务，供 voxel-dev 子 Agent 顺序开工。
> 现状以实际 `src`（根目录扁平结构：`world.*` / `chunkgeometry.*` / `playercontroller.*` / `main.cpp` / `Main.qml` / `CMakeLists.txt`）为准。

---

## 现状盘点（对照 PLAN §4 Phase 1.0 验收清单）

### 范围项
| §4 范围 | 状态 | 证据 |
|---|---|---|
| 第一人称 + 鼠标看(捕获指针) + WASD + 跳 + fly 开关 | ✅ 已实现 | `playercontroller.{h,cpp}`：`grab/release`（BlankCursor override + 居中轮询）、`pollMouse`、`setKey`、双击空格切飞、逐轴碰撞。`Main.qml` 绑定。 |
| culled meshing（每实体方块只发"邻居是空气"的面，越界=空气） | ✅ 已实现 | `chunkgeometry.cpp` `buildMesh()`：6 面 × 邻居判定，单 draw call。 |
| 纹理图集 + per-face UV + 半像素内缩 | ✅ 已实现 | `chunkgeometry.cpp`（5 瓦片横排，`hx/hy` 内缩）；`tools/build_atlas.py`。 |
| 有限世界 256×256 平原 + OpenSimplex + 树 | ⚠️ 部分 | `world.cpp`：当前 **16×16×16 单 chunk**，**Perlin fBm**（非 OpenSimplex），**无树**。 |
| 8 方块（草/土/石/圆石/原木/木板/树叶/沙） | ⚠️ 部分 | 仅 **5**（air/grass/dirt/stone/sand），缺 cobble/log/planks/leaves。源 PNG 已齐（`textures/default_*.png` 共 10 张覆盖 8 类）。 |
| 射线选体 + 线框高亮 | ✅ 完成 | DDA 体素射线 + 命中面线框（a219039）。 |
| 左破/右放 | ✅ 完成 | `World::setBlock` 破/放（099a555）。 |
| QML hotbar（9 槽，1–9/滚轮） | ✅ 完成 | 9 槽 hotbar + 1-9/滚轮/高亮（45f2374）。 |
| 昼夜（天光亮度乘子 lerp ~20min） | ✅ 完成 | 动态太阳光 / 昼夜循环（R17）。 |
| 原创占位贴图 16×16 + 3 SFX | ⚠️ 部分 | 贴图源已齐（**来源/CC0 未文档化**）；**3 SFX 缺**。 |
| F3 调试叠层 | ⚠️ 部分 | `Main.qml` HUD 仅 fps/pos/yaw/pitch/ground；缺 chunk/mesh/线程/draw-call。 |

### 验收项（可证伪）
| §4 验收 | 状态 | 证据 |
|---|---|---|
| 鼠标看/WASD/跳/fly 无抖动 | ✅ 已实现 | 同上。 |
| 射线命中 + 线框渲染在命中面 | ✅ 完成 | DDA 命中 + 命中面线框（a219039）。 |
| 左破/右放 + hotbar 1–9/滚轮，选中槽高亮 | ✅ 完成 | 破/放 + hotbar 高亮（099a555 / 45f2374）。 |
| 跨 chunk 边界破放不破坏邻居 mesh（脏标记邻接失效） | ✅ 完成 | 多 chunk + dirty 邻接失效（见 [[chunk-dirty-flag-race]]）。 |
| 性能分档（最低配 1080p@≥30 / 推荐配 @≥60） | ⏳ 待做 | 无 benchmark/帧时间切分。 |
| 窗口缩放 RHI 重建不崩/不拉伸 | ⚠️ 部分 | View3D 自处理，**未压测**。 |
| `isFeatureSupported(TextureArrays)` 已 probe | ⚠️ 部分 | 当前走 **QtQuick3D + Texture 图集**（非裸 QRhi），无 TextureArrays 对应物；**即图集兜底路径**。 |
| 零 MC 资产 / 零专有名词 | ✅ 已实现 | 方块名为通用词；无 Creeper 等。 |
| 零警告 `/W4` / `-Wall -Wextra`（自有代码） | ⏳ 待做 | 未核。 |
| Win + Linux CI 绿（仅编译） | ⏳ 待做 | 无 CI 配置。 |
| 资产门（每文件具名来源） | ⚠️ 部分 | 源 PNG 齐，**来源未文档化**。 |

### 关键偏差 / 开放决策（须在 Phase 1.0 内复核）
1. **渲染层偏差**：当前用 **QtQuick3D**（`QQuick3DGeometry` + `PrincipledMaterial` + `Texture` 图集），与 PLAN §1 "不用 Qt Quick 3D 画体素世界" 决策**不一致**。理由：Phase 1.0 定位为 engine spike，QtQuick3D 路径最快满足 §4 玩法/性能验收；不变量 **A（RHI 囚笼）在当前路径下不触发**（代码未直接使用 `QRhi*`）。**决策点**：256×256 多 chunk 压测时若性能预算不达标 → 迁移到自研 QRhi 渲染层（届时补 §4 的 TextureArrays probe + 不变量 A 的 CI include-guard）。Phase 1.0 内不阻塞。
2. **TextureArrays probe**：QtQuick3D 路径下无对应物；当前即图集兜底。完整 probe 推迟到 RHI 迁移，Phase 1.0 以"图集路径文档化"形式落 §4 验收（见 t12）。
3. **噪声**：当前 Perlin，§4 指定 OpenSimplex。t07 允许保留 Perlin 类噪声（确定性 + 外观达标即可），不强求库替换。

---

## 任务清单

> 拆分粒度黄金法则：一个任务 = 一个能独立编译、独立验证的功能单元（约 100–300 行）。
> 状态：⏳ 待办 | 🔄 进行中 | ✅ 完成 | ⚠️ 低质量通过 | 🔜 推迟（放大阶段，本回合不做）

> **策略**：功能优先——第 1 轮（2026-07-26）单 chunk 创造沙盒（t01/t04/t05/t06/t14）✅；第 2 轮（2026-07-27）3×3 地形 + 主菜单/背包 + bug 修（t15/t16/t02/t03/t17/t18）✅。
> 第 3 轮（✅ 已完成 2026-07-27）：UI 贴近 MC 1.0 风格 + 模式机制（t19-t24）。PLAN §9 override 放行 1.0 布局/中文命名（硬底线=素材全原创）。
> 第 4 轮（✅ 已完成 2026-07-27）：树木/树叶 worldgen + F5 第三人称相机 + 玩家模型/幽灵 + 第一人称手 viewmodel（t25-t29）。
> 完整 256×256 / 昼夜 / F3 / 音效 / 资产门 / CI 仍推迟到放大阶段（树已在第 4 轮落地）。

### 第 2 轮（已完成 2026-07-27）—— 已全做（按表顺序）
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t15 | 输入键位（G 循环模式）+ hotbar 图标修正（统一尺寸 / 正确方块 / 立方体图标） | ✅ | — | 修第 1 轮 bug：N→G；图标 4/5/6 不可辨、7/8 显同色 |
| 2 | t16 | 破/放粒子可见性修复（**必须运行实测**，静态编译通过不算） | ✅ | t05,t14 | 修 t14 隐性失败：怀疑 Loader 静默降级 |
| 3 | t02 | 多 chunk 化（3×3=9 chunks，ChunkManager + 跨 chunk blockAt/setBlock + Perlin 放大 48×48；QML API 不变） | ✅ | t01 | 原 🔜 提前，重定为 3×3（非 256×256） |
| 4 | t03 | 每 chunk culled mesher + 跨边界剔除（3×3 无缝，dirty 仅重建相关 chunk） | ✅ | t02 | 含"跨边界破放不破坏邻居 mesh"验收 |
| 5 | t17 | 主菜单（开始游戏 / 退出游戏，启动先显菜单，app 状态 menu↔playing） | ✅ | — | |
| 6 | t18 | 背包/物品栏（E 键开关，创造风格全方块网格，点击装备到 hotbar 当前槽） | ✅ | t06,t01 | 完整生存背包（栈/拖放/合成）属 Phase 1.1 |

### 第 3 轮（UI 1.0 风格 + 模式机制，✅ 已完成 2026-07-27）—— 已全做
> 用户决议（2026-07-27）：UI 贴近 Minecraft 1.0 布局/中文命名（PLAN §9 override）；创造≠生存背包；观察者禁放破。全部 GUI **自绘原创**（不拷贝 MC 素材文件）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t19 | 方块中文命名 + BlockRegistry.displayName | ✅ | t01 | §9 override (b)；HUD/背包显中文 |
| 2 | t20 | HUD 翻新：准星 + 1.0 风格 9 槽 hotbar + 选框 | ✅ | t15,t19 | spectator 隐 hotbar；§9 override (a) 自绘 |
| 3 | t21 | 模式行为门控（观察者禁放破 / 创造飞 / 生存重力） | ✅ | t15 | 用户核心诉求：spectator 不能放 |
| 4 | t22 | 生存 HUD：心 + 饥饿条（仅 Survival 显） | ✅ | t20,t21 | §9 override (a) 自绘 |
| 5 | t23 | 创造背包 1.0（全方块调色板 + hotbar 栏 + 销毁槽） | ✅ | t18,t19,t21 | Creative E 开 |
| 6 | t24 | 生存背包 1.0（3×9 + hotbar + 2×2 合成 + 4 护甲 + 角色预览） | ✅ | t19,t21,t23 | Survival E 开；合成/护甲占位 |

### 第 4 轮（树木 worldgen + 第三人称相机 + 玩家模型/手 viewmodel，✅ 已完成 2026-07-27）—— 已全做
> 用户诉求（2026-07-27）：加入树/树叶生成；玩家模型（第三人称可见）；F5 循环第一/第三人称；创造/生存实体模型、观察者幽灵半透；第一人称见手 + 左键挖掘挥动动画。全部自绘原创（§9 override (a)）。
> 含 2 项直接修复（已 commit `c490c05`：生存背包合成/护甲左右换边 + 破放粒子满屏大方块修复）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t25 | 纹理图集重建 10 瓦片 + mesher N=10（解锁 cobble/log/planks/leaves 世界内贴图） | ✅ | t01 | build_atlas.py 加 5 瓦片 + chunkgeometry N=10 |
| 2 | t26 | 树与树叶生成（确定性 worldgen） | ✅ | t25,t02 | grass 上种橡树（原木主干+树叶球冠）；§2-K 确定性 |
| 3 | t27 | F5 相机模式循环（第一/第三-后/第三-前） | ✅ | — | CameraMode enum + cycleCamera + 相机绑定；t28/t29 基础 |
| 4 | t28 | 玩家 3D 模型（第三人称可见）+ 观察者幽灵半透 | ✅ | t27 | 方块化人形纯色原创；spectator opacity≈0.35 |
| 5 | t29 | 第一人称手 viewmodel + 挖掘挥动动画 | ✅ | t27 | 手 Model 附相机 + swingArm 信号驱动挥动 |

### 第 1 轮（功能切片，已完成 2026-07-26）
| 任务ID | 标题 | 状态 | 备注 |
|--------|------|------|------|
| t01 | BlockRegistry：8 方块 + tile + solid | ✅ | |
| t04 | 射线选体（DDA）+ 线框高亮 | ✅ | 单 chunk |
| t05 | 左破/右放 + setBlock + 重建 mesh | ✅ | 单 chunk |
| t06 | Hotbar 9 槽 + 1–9/滚轮/高亮 | ✅ | |
| t14 | 破/放粒子（骨架） | ✅⚠️ | 骨架编译 PASS 但肉眼不可见 → t16 修 |

### 第 5 轮（验收 bug 修复，✅ 已完成 2026-07-27，commit `9a1de7c`）—— 主编排直接修（非 workflow）
> 用户验收第 4 轮后报 8 项 bug。因全是视觉/交互 bug（harness 三测结构性测不出），由主编排读码定位+亲自 run 验证，未走 workflow。
| # | 项 | 状态 | 备注 |
|---|---|------|------|
| 1 | 粒子满屏遮罩 | ⚠️ 部分 | particleScale 1.0→0.15（变小但**仍过大**→t30 续修） |
| 2 | 玩家模型三视角全空 | ❌ 未修 | NoLighting 假设**未生效**→t31 真诊断 |
| 3 | 第一人称手透明 | ❌ 未修 | 同上→t31 |
| 4 | 背包点/拖无效 | ⚠️ 部分 | 加 heldBlock 能拾取，但**放不下+拾取复制**→t37/t38 续修 |
| 5 | 点背包外部误关闭 | ✅ | 遮罩去 onClicked |
| 6 | hotbar 选框右/下发灰 | ✅ | 四边白 |
| 7 | 树缺随机 | ✅ | 4 层树冠+随机角叶+主干 4-7 |
| 8 | git c490c05 中文/「待人工」 | ✅ | rebase reword + purge |

### 第 6 轮（生存机制 + 物品系统 + bug 续修，✅ 已完成 2026-07-27）—— 已跑 workflow
> 用户诉求：修剩 bug（粒子/模型/手/背包放置）+ 加生存核心循环（限时挖掘+裂纹、工具、掉落/拾取/丢弃、栈式背包）。**本文件为计划稿，本轮只规划不实现**，之后由用户跑 `voxel-autopilot` workflow。
> ⚠️ **执行约束**：t31/t34/t35 是视觉项（模型可见/裂纹/掉落实体），workflow 测不出，verdict 应为 `needs-run`，**workflow 结束后主编排必须逐个 run+肉眼复核**（见 lessons-learned「渲染盲区静态化」+ 元教训）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t30 | 粒子碎屑仍过大（续修） | ✅ | — | particleScale 更小+减量+收束速度，碎屑明显小于方块 |
| 2 | t31 | 玩家模型+第一人称手不可见：**真因运行期诊断+修复** ⚠️needs-run | ✅ | — | **已完成 commit `58db1a0`**（主编排直接做）：诊断证实模型已在场景图、同 chunk 父节点、位置合法 → 排除领养坑；唯一差异=静态 `#Cube` 不渲染（手无 opacity 也invisible→非 opacity 因）→ 新增 `UnitCube` 自定义几何，模型/手 7 处 `#Cube`→`UnitCube{}`，对齐地形/线框已验证路径。run 日志：模型 in-scene、root objects=1、exit 0。视觉确认待人工（本轮无人工） |
| 3 | t32 | ItemStack 栈数据模型（基础） | ✅ | — | 槽位 block-id→{itemId,count}；selectedBlock 从栈派生；数量显示 |
| 4 | t33 | 工具系统（镐类 + 挖掘速度表） | ✅ | t32 | 工具物品；影响挖掘速度/可采掘 |
| 5 | t34 | 挖掘系统（创造秒破 / 生存限时+裂纹，工具感知） ⚠️needs-run | ✅ | t32,t33 | 生存持续挖掘进度+裂纹叠层；空手 vs 工具速度 |
| 6 | t35 | 方块掉落实体（生存挖掘产出） ⚠️needs-run | ✅ | t32,t34 | item entity 入世界（旋转方块图） |
| 7 | t36 | 拾取 + 丢弃 | ✅ | t32,t35 | 走近拾取→空槽/空手规则；Q 丢弃为实体 |
| 8 | t37 | 创造背包交互完善 | ✅ | t32 | 放置覆盖；背包外**中键拾取方块**（pick block） |
| 9 | t38 | 生存背包栈操作 | ✅ | t32 | 左键整组移动/放置；数量显示；空背包起 |

**建议执行序**：t30→t31（独立 bug，可并行）→ t32（基础，必先）→ t33→t34→t35→t36（生存链）／ t37、t38（t32 后可插）。t31 须主编排 run 诊断，**不建议纯 workflow 闭眼跑**。

### 第 7 轮（模型/相机修复 + 昼夜/F3 新功能，✅ 已完成 2026-07-28）—— 已跑 workflow
> 用户验收第 6 轮反馈：模型太简陋（无脸/眼/手）、相机穿墙；并要新功能。背包 id 错乱由主编排另加日志诊断（commit `ef094ed`，需用户跑后看 [inv] 日志定位，非本轮 workflow 任务）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t39 | 玩家模型细化（脸/眼/手臂+手） ⚠️needs-run | ✅ | t31 | UnitCube 加眼睛 + 手臂末端肤色手 |
| 2 | t40 | 第三人称相机不穿墙（raycast 距离钳制） | ✅ | t27 | cameraDistance 射线，相机贴墙不穿入 |
| 3 | t09 | 昼夜（天光亮度 lerp ~20min）〔放大阶段重激活〕 ⚠️needs-run | ✅ | — | dayPhase 绑环境色/光 |
| 4 | t10 | F3 调试叠层（fps/chunk/mesh/pos/模式）〔放大阶段重激活〕 | ✅ | — | F3 切 Text 叠层 |

**建议执行序**：t39→t40（用户报的 bug 先）→ t10→t09（新功能，机械可静态判）。t39 视觉 → needs-run，主编排 run 复核。

### 第 8 轮（实体/掉落 + 方块表 + 模型动画 + 工程重组 + 日志，✅ 2026-07-28）—— 已跑 workflow
> 用户验收第 7 轮反馈：模型动画/续挖/方块表/掉落实体+Q丢弃/观察者隐手/昼夜地形光/分文件夹/日志管理。背包图标 bug 已由主编排修（`06f7296`，本轮不动）。视觉项 needs-run，主编排 run 复核。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t41 | 工程分文件夹重组（src/Core/World/Renderer/Game/ui） | ✅ | — | 必最先；中性重构，CMake/QML 路径同步 |
| 2 | t42 | BlockDef 通用方块属性表（硬度/工具/掉落/堆叠） | ✅ | t41 | 集中定义，挖掘/掉落/背包复用 |
| 3 | t43 | 掉落实体系统（地面实体+近距拾取+堆叠+Q丢弃，移除 auto-collect） ⚠️needs-run | ✅ | t42 | 浅灰球包裹小方块 |
| 4 | t44 | 挖掘连续（长按续挖视线下一块）+ 复用 BlockDef | ✅ | t42 | 不松手连挖到射程外 |
| 5 | t45 | 第三人称模型动画（走/跑腿摆 + 挖掘挥臂） ⚠️needs-run | ✅ | t39,t41 | 部件化骨骼，为皮肤系统铺垫 |
| 6 | t46 | 背包内 hotbar 行左键交互统一 + 观察者隐手 | ✅ | t41 | 创造/生存 hotbar 行 resolveClick |
| 7 | t47 | 昼夜影响地形光照（全屏 tint 叠层） ⚠️needs-run | ✅ | t09,t41 | NoLighting 地形不受光 → tint 方案 |
| 8 | t48 | 日志移出 build/（→logs/）+ docs gitignore 核对 | ✅ | t41 | 文件卫生 |

**执行序**：t41（重组，必先）→ t42（方块表）→ t43/t44（掉落+续挖）／ t45/t46/t47/t48（t41后可并行）。t43/t45/t47 视觉 → needs-run。

### 第 9 轮（背包交互完善 + 合成 + 状态机 + 模型修正 + 掉落修复，✅ 2026-07-29）—— 已跑 workflow
> 用户验收第 8 轮大量反馈。README 由主编排写（不 commit）。视觉/交互项 needs-run，主编排 run 复核。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t49 | 背包交互全面修正（hotbar行不切真实选中/初始空/右键半份单放/拖出丢弃/关包归还合成栏） | ✅ | t41 | 精确：删两处 selectedSlot=index；resetForMode 全空 |
| 2 | t50 | 合成系统（2×2 背包 + 3×3 工作台；原木/木板/木棒/工作台/木镐；MC式数量） ⚠️needs-run | ✅ | t42,t49 | RecipeRegistry + 工作台新方块 |
| 3 | t51 | 玩家状态机（双击W疾跑/Shift蹲下边缘安全/受伤红闪） ⚠️needs-run | ✅ | t45,t41 | MoveState enum + 蹲碰撞 |
| 4 | t52 | 模型/手/选中修正（只右手动/手持方块/眼睛贴脸/手不穿模/选中立方体框） ⚠️needs-run | ✅ | t39,t45 | WireCube 选中框 |
| 5 | t53 | 修复掉落实体不可见（排查 t43 Repeater 渲染 / 坐标 / auto-collect 残留） ⚠️needs-run | ✅ | t42,t43 | 关键：实体肉眼可见 |

**执行序**：t49（背包，高优先+解锁 t50）→ t53（掉落）→ t50（合成）→ t51/t52（状态机/模型，可并行）。README 主编排最后写（不 commit）。

### 第 10 轮（掉落/背包/挖掘 bug 修 + 工作台/重力/粒子，✅ 2026-07-29）—— 已跑 workflow
> 用户验收第 9 轮：掉落可见了（parent=null 修）。新反馈见 dev-spec 第 10 轮。视觉/交互项 needs-run。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t54 | 修掉落物贴图错（BlockCube UV/face bug，对照 ChunkGeometry）⚠️needs-run | ✅ | — | 树叶显木板/泥土显半石 |
| 2 | t55 | 修 HUD hotbar 不显拾取/放入物品（显示层+诊断日志）⚠️needs-run | ✅ | — | 数据在、显示空白 |
| 3 | t56 | 修 Q 丢弃无效（排查 dropHeld 调用链/捕获态/实体渲染）⚠️needs-run | ✅ | — | 按Q不丢+还在手上 |
| 4 | t57 | 修空手挖石头掉落（canHarvest 调用链/m_selectedItem） | ✅ | — | 石头需镐才掉 |
| 5 | t58 | 修 shift 蹲下边缘安全（不掉下）⚠️needs-run | ✅ | — | Crouch 预查脚下 |
| 6 | t59 | 工作台（创造调色板+放置+右键开3×3+空手破）⚠️needs-run | ✅ | t50 | CraftingTable 已有 BlockDef |
| 7 | t60 | 掉落物重力（落到方块表面）⚠️needs-run | ✅ | t53 | vy+落地停 |
| 8 | t61 | 挖掘过程粒子（复用破块粒子+破块+30%）⚠️needs-run | ✅ | — | stage 变化迸发 |

**执行序**：t54→t55→t56→t57→t58（bug 先）→ t59→t60→t61（功能）。多数 needs-run，主编排 run 复核。

### 第 11 轮（工作台物品栏 + 掉落实体 count/贴图 + 模型组 + 挥空手 + 爱心，✅ 2026-07-29）—— 已跑 workflow
> 用户验收第 10 轮：空手挖石头已不掉（t57 OK）。新反馈 11 项；t62（蹲下疾跑 + 删测试实体）由主编排自修已提交（7fcd3ac）。视觉/交互项 needs-run，主编排 run 复核。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t63 | 工作台完整 UI（3×9 物品栏+3×3 合成+产物，接背包可取放）+ 修生存/创造背包 hotbar 行 t55 复发 | ✅ | t59,t49 | SurvivalInventory.qml:503 / Inventory.qml:280 buggy model |
| 2 | t64 | 掉落实体加 count 字段 + dropHeldCursor 传 count（修丢整栈只生 1 实体）+ 实体贴图按 id 分流（木棒/木镐非默认方块） | ✅ | t53 | itementitymanager.h:89 无 count；blockcube.cpp:58 越界兜底 Stone |
| 3 | t65 | 蹲下模型姿态（第2/3人称 Shift 身体下沉+腿弯；现仅 swingAmp 步幅） | ✅ | t51,t45 | playerModel 不绑 moveState 姿态 |
| 4 | t66 | 头部跟随视线 pitch（眼睛看鼠标方向；现 playerModel 只 yaw） | ✅ | t45 | pitch 已暴露(h:41)；head Node 加 eulerRotation.x |
| 5 | t67 | 受伤改为模型变红 + 视角晃动（替换全屏 damageOverlay 红闪） | ✅ | t51 | Main.qml:748 全屏 Rectangle |
| 6 | t68 | 左键无目标挥空手（beginMining !m_hasHit 也 emit swingArm；为打怪铺垫） | ✅ | t45 | playercontroller.cpp:363 早 return |
| 7 | t69 | 爱心半心显示修复（VitalIcon.qml:69 缺 level>=1 守门→空=半心无法区分） | ✅ | t51 | 一行加守门 |

**执行序**：t64（丢物品恶性 bug 先）→ t63（工作台+物品栏，用户重点）→ t69（爱心小修）→ t68（挥空手）→ t65→t66→t67（模型组，都改 playerModel 须串行）。t63/t64 都改 Main.qml 不同区域、串行。多数 needs-run，主编排 run 复核。

### 第 12 轮（手持/木镐3D/拾取/线框/工作台/死亡/均分/熔炉 + 疾跑蹲下回归，✅ 2026-07-29）—— t70-t73 主编排自修，t74-t80 workflow
> 用户验收第 11 轮反馈 ~17 项。疾跑/蹲下/第三人称手持/第一人称手为 t62/t65/t52 回归（主编排曾改过，自修最快最准）；新功能（木镐3D/死亡/右键均分/熔炉）+ 独立 bug（拾取槽/线框/工作台布局）进工作流。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t70 | 疾跑回归（release/setMode 清 m_lastWms + 窗口 300→250ms） | ✅ | — | 主编排自修 5598063 |
| 2 | t71 | 蹲下姿态改上前倾鞠躬（upperBody Node 绕髋 pitch，非下沉）+ 修 crouchKnee 递归笔误 | ✅ | — | 主编排自修；Main.qml playerModel 重构 |
| 3 | t72 | 第三人称手持方块角度（突出手前 z≈-0.3 + 旋转，像 MC） | ✅ | — | 主编排自修 5598063；t71/t73 转工作流 |
| 4 | t73 | 第一人称手持方块可见（脱离手臂遮挡）+ 蓝袖子 + 防穿模（t52 z/tilt/scale） | ✅ | — | 主编排自修；Main.qml viewModelHand:239 |
| 5 | t74 | 拾取槽 addStack 顺序（先合并已有同 id 未满槽，再空槽） | ✅ | — | hotbar.cpp:241 步骤0 删空槽开新栈分支 |
| 6 | t75 | 木镐3D 模型（PickaxeGeometry 镐形）+ 丢弃/第一/第三人称手持渲染 + 修工具贴图黑（alphaCutoff） | ✅ | t72,t73 | 三处复用；实体 CrackBox→billboard 或真 3D |
| 7 | t76 | 选中线框收紧（scale 1.02→1.005 或 1.0+禁 depth） | ✅ | — | Main.qml:434 |
| 8 | t77 | 工作台布局（合成行居中对齐 + 删提示文字） | ✅ | — | CraftingTableUI.qml:339 删 Text |
| 9 | t78 | 死亡界面（血量 0 → 立即重生 / 回主菜单） | ✅ | — | PlayerState + Main.qml 死亡 UI |
| 10 | t79 | 右键拖拽均分（右键扫过 N 格等分，余数留手；背包 + 工作台） | ✅ | — | Inventory/SurvivalInventory/CraftingTableUI TapHandler |
| 11 | t80 | 熔炉方块（BlockDef + tile）+ 8 原石围圈配方 | ✅ | — | blockregistry + recipe + atlas tile |

**执行序**：t70-t73 主编排自修先（集中改 playerModel/viewModelHand/playercontroller，避免与工作流冲突）→ 提交 → t74-t80 工作流（t75 木镐3D 依赖 t72/t73 后的 viewModelHand/rightArmPivot 结构）。视觉/交互项 needs-run，主编排 run 复核 + 每环节 token 汇报。

### 第 13 轮（矿物链/冶炼/光照火把/音效 + 手回归/均分/dropId，✅ 2026-07-30）—— t81/t83 主编排自修，t82/t84-t89 workflow（长）
> 用户验收第 12 轮：手变小(t73 回归)/均分要持续到填满/加音效/加铁矿煤矿(需镐,铁需石镐)/光照+火把动态光源。光照 PointLight 不可行(lit 不渲染红线)→火把用伪光源(NoLighting 发光精灵)。矿物链前提=修 dropId BUG(现 finishMiningAt 传 brokenId 非 dropId)。用户要「做长一点时间」。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t81 | 第一人称手调大（scale 0.09→0.12 + 加长 Y + tilt 30→40，零穿模） | ✅ | — | 主编排自修 bc3f8ff |
| 2 | t82 | 右键均分持续到填满（applyDragDistribute 循环 +1 到 remaining=0；三文件同步） | ✅ | — | Inventory/SurvivalInventory/CraftingTableUI |
| 3 | t83 | dropId BUG 修复（finishMiningAt 读 dropId 非 brokenId） | ✅ | — | 主编排自修 bc3f8ff |
| 4 | t84 | 矿石方块（CoalOre/IronOre BlockDef + tile + worldgen 散布；IronOre minTier2 需石镐） | ✅ | t83,t85 | blockregistry + world.cpp + build_atlas |
| 5 | t85 | 材料段扩展（Coal 0x201/IronOreDrop 0x202/IronIngot 0x203 + nameForBlock + MaterialIcon 图标 + 实体 Repeater 材料段分流） | ✅ | — | recipe.h + hotbar.cpp + MaterialIcon + Main.qml |
| 6 | t86 | 石镐/铁镐配方（3 圆石+2 棒→石镐；3 铁锭+2 棒→铁镐） | ✅ | t85 | recipe.cpp |
| 7 | t87 | 熔炉冶炼系统（furnaceOpened 信号 + placeBlock Furnace 分支 + FurnaceUI.qml 输入/燃料/输出/进度 + SmeltingRegistry + 燃料表 + 冶炼 tick） | ✅ | t85,t80 | playercontroller + 新 FurnaceUI + 新 SmeltingRegistry |
| 8 | t88 | 火把方块 + 伪光源（Torch BlockDef+tile+放置 + NoLighting 高 baseColor 发光精灵光晕，非 PointLight） | ✅ | — | blockregistry + Main.qml 发光 Model |
| 9 | t89 | 音效系统（miniaudio 集成 + AudioManager + break/place/step SFX + 原创 wav + CMake） | ✅ | — | 新 src/Audio + CMake + Main.qml |

**执行序**：t83(dropId 前提)→t85(材料)→t84(矿石)→t86(镐配方)→t87(冶炼)／t88(火把)／t89(音效)／t82(均分)／t81(手)。t81/t83 主编排自修先 → 提交 → t82/t84-t89 工作流长跑。视觉/交互项 needs-run，主编排 run 复核 + token 汇报。

### 第 14 轮（均分修复/手分层/拾取/木炭/tooltip/实体 + 光照调研，✅ 2026-07-30）—— t91 主编排自修，t90/t92-t95 workflow，t96 光照§M 后续轮
> 用户验收第 13 轮：均分变刷物品(t82改错)/手分层反+方块太靠右/火把要真光源+洞穴暗(阴影系统,参考MC,先调研)/熔炉背包不能操作/木炭+木燃料/tooltip/实体生物/打开背包拾取不了。光照调研结论：PointLight 违 lit 红线+多光源阴影性能崩 → 走路径b(MC式顶点flood-fill,契合§2-H/I/M,4-6轮工程,本轮只记录后续专项)。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t90 | 均分修复（t82 回归：仅 dragSlots N 等分 floor(count/N) 余数留手，删自动纳入全背包+while 循环） | ✅ | — | Inventory/SurvivalInventory/CraftingTableUI 三处 applyDragDistribute |
| 2 | t91 | 第一人称手分层+位置（交换袖子/手 Y：袖→-0.10 下蓝、手→+0.02 上肤色；手持方块/工具 Y 跟手；父 Node X 0.35→0.20 左移） | ✅ | — | 主编排自修；Main.qml viewModelHand |
| 3 | t92 | 打开背包拾取修复（pickupScan 提到 tick 的 if(!m_captured) 早 return 之前） | ✅ | — | playercontroller.cpp:249/261 |
| 4 | t93 | 木炭+木燃料补全（smelting.cpp kFuel 加木棒 5s + 工作台 15s；木炭配方 CharcoalId=0x205 已实现） | ✅ | t87 | smelting.cpp:23 |
| 5 | t94 | 背包 hover tooltip（悬停方块/工具显名字；工具后续加攻击力，现阶段只名字） | ✅ | — | Inventory/SurvivalInventory/CraftingTableUI/FurnaceUI + ToolTip |
| 6 | t95 | 实体生物测试（地图中间地表纯方块实体纯色突出，可被玩家推动；掉落物同实体但不被推动被拾取——统一 EntityManager 设计） | ✅ | — | 新 src/Entities + playercontroller 推动碰撞 |
| 7 | t96 | 光照里程碑 §M（路径b 顶点flood-fill：顶点格式+light通道 / heightmap+天光 / 方块光BFS跨chunk / 平滑+洞穴 / 性能） | 🔜 | — | 调研完成(路径b)；实现拆 4-6 轮后续专项，本轮不做 |

**执行序**：t91 主编排自修先 → 提交 → t90/t92-t95 工作流。t96 光照留后续专项多轮（用户预期「先调研后做」）。视觉/交互项 needs-run，主编排 run 复核 + 每轮 token/时间汇报。

### 第 15 轮（背包VM共享/右键实时/丢弃/石头反转/火把/熔炉布局/手/实体，✅ 2026-07-30）—— t103 主编排自修，t97-t102/t104 workflow
> 用户验收第 14 轮 18 项反馈，选「直接推进 t97-t104」8 项；沙子重力/音效按方块/地形水/F3+B 留第 16 轮。背包三件套主栏不同步是架构根因（27 主栏在 QML 本地、C++ Hotbar VM 只管 9 hotbar），t97 主栏上移 VM 解锁 t98/t99。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t97 | 背包主栏 VM 共享（Hotbar 加 main 27 槽 + mainBlockIdAt/Set/Add + mainRevision NOTIFY；三 QML 删本地 mainSlots 改读 VM；returnHeldToHotbar/pickupScan 改 addToAny 先 main 同id合并再 hotbar） | ✅ | — | hotbar.{h,cpp} + SurvivalInventory/CraftingTableUI/FurnaceUI + Main.qml |
| 2 | t98 | 右键实时分配（addDragSlot 内联 redistributeLive：每滑格用原始 total 重算 N 等分、先撤销旧写入再重分、余数实时回光标；endRightDrag 退化）+ 双击合并（lastTapMs+同槽<400ms→合并同类 64） | ✅ | t97 | 三 UI applyDragDistribute/addDragSlot + TapHandler 双击 |
| 3 | t99 | tooltip 残留修复（丢弃后槽空主动清 hoveredItemId 或绑 mainRevision）+ 丢弃回栏合并（addToAny 依赖 t97） | ✅ | t97 | 四 UI HoverHandler + Main.qml returnHeldToHotbar |
| 4 | t100 | 石头/原石贴图反转（互换 default_stone.png ↔ default_cobble.png 内容；世界 tile+背包图标一次改对；dropId 逻辑 MC 正确不动） | ✅ | — | textures/default_stone.png + default_cobble.png |
| 5 | t101 | 火把配方（2 条 shapeless：煤+棒、木炭+棒 → 4 火把，Inventory2x2） | ✅ | — | recipe.cpp |
| 6 | t102 | 熔炉烧制区上移（燃料槽底边 y=130 落进主栏；furnaceRow height 48→84 + panel height 332→368） | ✅ | — | FurnaceUI.qml |
| 7 | t103 | 第一人称手前旋 60°（baseTilt 40→100；穿模风险同步收 position.z -0.2→-0.15） | ✅ | — | 主编排自修 66cd177；用户自调 Main.qml:337/336 |
| 8 | t104 | 实体推动 jitter+穿墙修复（resolvePlayerPush 改 mob AABB footprint 全格扫，仿 player aabbHitsSolid；非中心格单格检查） | ✅ | — | entitymanager.cpp resolvePlayerPush |

**执行序**：t103 主编排自修先 → 提交 → t97（VM 架构核心）→ t98/t99（依赖 t97）／t100/t101/t102/t104（独立）。视觉/交互项 needs-run，主编排 run 复核 + token 汇报。沙子重力/音效/地形水/F3+B = 第 16 轮。

### 第 16 轮（背包交互/方块视觉/实体物理/沙子/音效/基岩64/光照轮1，✅ 2026-07-31）—— t107-t121 workflow（长，用户允许 10h）
> 用户验收第 15 轮 18 项反馈 + 光照长任务。手翻转 t106 主编排已自修（2c339da baseTilt +100→-100）。背包卡顿=创造 DragHandler 抢 grab + cursorTracker 被面板截断；实体缩小=Y 轴 resting-flip（非 scale）；矿石=PNG 过时；掉落 6 面=CrackBox；火把=黑底立方+无光；光照路径 b 轮 1 启动。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t107 | 背包拿起卡顿修复（cursorTracker HoverHandler 提升到面板上层不被截断；创造 Inventory DragHandler acceptedButtons 限定右键或删，避免与左键 TapHandler 抢 grab） | ✅ | — | Main.qml cursorTracker/浮动光标 + Inventory.qml DragHandler |
| 2 | t108 | 右键分配增强（HoverHandler else 分支 removeDragSlot 回滑减格 + redistributeLive 撤销；n>total 截断 eligible 到 total 项；异物槽不入 dragSlots 且绿框加空/同id 条件） | ✅ | t107 | 三 UI addDragSlot/redistributeLive + 绿框 |
| 3 | t109 | 拾取优先 hotbar（addToAny 空槽顺序：main 同id→hotbar 同id→**hotbar 空优先**→main 空；交换 hotbar.cpp 空槽两循环） | ✅ | — | hotbar.cpp addToAny |
| 4 | t110 | Shift/数字键（背包开 Shift 不触发蹲下守卫；Shift+左键 main↔hotbar 搬运；数字键 1-9 背包开时与 hoveredKey 槽交换） | ✅ | t107 | Main.qml 键盘 + 槽 TapHandler + window.hoveredSlotKey |
| 5 | t111 | 矿石背景（重跑 build_ore.py 用最新 stone 底 + build_atlas.py + build_cube_icons.py；不改代码） | ✅ | — | tools/ 脚本重跑 |
| 6 | t112 | 煤/木炭/铁原矿掉落 BillboardQuad（新建 BillboardQuad 单面朝相机几何；实体 Repeater 材料段 CrackBox→BillboardQuad + lookAt 相机） | ✅ | — | 新 src/Renderer/billboardquad + Main.qml 实体Repeater |
| 7 | t113 | 熔炉布局（FurnaceUI panel height 368→334 删底部空白带，hotbar 贴底同工作台） | ✅ | — | FurnaceUI.qml:218 |
| 8 | t114 | 火把（creativeMaterials 加煤/木炭/铁锭等；mesher Torch 特例跳过立方；torchHost 渲染木柄+火焰 Model；朝向运行时据邻居 solid 推断 平地垂直/墙面侧面） | ✅ | — | hotbar creativeMaterials + chunkgeometry Torch特例 + Main.qml torchHost + placeBlock 朝向 |
| 9 | t115 | 实体 Y 抖修复（resolvePlayerPush 行152 resting 不无条件清，按新位置下方支撑格 isSolid 判定才解除） | ✅ | — | entitymanager.cpp resolvePlayerPush:152 |
| 10 | t116 | F3+B 碰撞箱（showHitboxes + B 键仅 f3Visible 时；mob/掉落物/玩家 WireCube AABB + 朝向箭头） | ✅ | — | Main.qml showHitboxes/B键 + WireCube Repeater |
| 11 | t117 | 沙子重力方块（EntityManager 加 FallingBlock kind+blockId+spawnFallingBlock+tick 着地 setBlock+pushable=false；onBlockPlaced/Broken 查沙下方空气触发；worldgen 沙漠二次 fbm biome） | ✅ | — | entitymanager + world + Main.qml + worldgen |
| 12 | t118 | 音效节奏（AudioManager playMining/playPickup + miningParticle 每 stage 接音「每挥一次响」+ itemPickedUp 拾取音 + 按方块材质分组 clip 石/木/草/沙） | ✅ | t120 | AudioManager + playercontroller + sounds/ + Main.qml |
| 13 | t119 | 基岩+高度 64（World height 16→64 + heightAt 重定标；Bedrock id14 hardness=-1.0 canMine 自动 false；**beginMining 创造分支加 canMine 守卫**防秒破；generate 基岩层 0-4 hashVoxel 坑洼） | ✅ | — | Main.qml:142 + blockregistry + world generate + playercontroller beginMining |
| 14 | t120 | 拾取/拿取动画（pickupScan emit itemPickedUp 信号 + viewModelHand handPopAnim popY 弹跳 + 创造拿物品也触发） | ✅ | — | playercontroller pickupScan + Main.qml handPopAnim |
| 15 | t121 | 光照轮 1（Vtx 加 rgb 通道+ColorSemantic 注册+chunk.h heightmap+mesher 写天光 heightmap 见天=1.0/地下=0.2；PrincipledMaterial 自动 baseColor×vertexColor；洞穴变暗验证） | ✅ | — | chunkgeometry Vtx/attribute + chunk.h heightmap + world heightmapAt + mesher |

**执行序**：独立项先（t111 矿石/t113 熔炉/t115 实体/t116 F3B/t109 拾取/t119 基岩/t120 拾取动画/t121 光照）→ 背包组串行（t107→t108→t110）→ t112 掉落/t114 火把/t117 沙子/t118 音效。光照轮 1 是路径 b 第一步（后续轮 2 方光 flood-fill/轮 3 AO）。视觉/交互项 needs-run，主编排 run 复核 + 每轮 token/时间汇报。

### 第 17 轮（动态太阳光照/火把全套/创造滚动/沙子CD/手翻转，✅ 2026-07-31）—— t122 主编排自修，t123-t128 workflow
> 用户验收第 16 轮：光照仍摆设(要太阳时间流逝真阴影)/火把贴图方块底+墙朝向错+选中框全格/手又反/沙子放太快/创造缺滚动。手翻转 t122 主编排已自修（c73be41 baseTilt -100→+100，t106 几何判断写反）。光照动态阴影调研：真 lit+shadowmap 违 PLAN §2-H + 9 chunk 闪烁 + MC 自己无真阴影 → 走方案②顶点光动态太阳（mesher sunFactor + heightmap 列投影）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t122 | 第一人称手翻转纠正（baseTilt -100→+100，t106 几何判断写反） | ✅ | — | 主编排自修 c73be41 |
| 2 | t123 | 动态太阳光照（方案②顶点光：WorldClock 加 sunDir/elevation/azimuth 随 dayPhase；mesher 加 sunFactor=max(0,faceNormal·sunDir)×dayPhase 调制顶点 sky；可选 heightmap 列投影阴影） | ✅ | t121 | worldclock + chunkgeometry mesher；多轮本轮轮1 |
| 3 | t124 | 火把贴图透明底（build_torch.py blank alpha=0 透明底非黑实心；build_cube_icons.py torch 走平面2D图标路径非立方体） | ✅ | — | tools/build_torch.py + build_cube_icons.py |
| 4 | t125 | 火把朝向修正（recomputeOrient 优先玩家点击面 hitNormal（经 torchPositions 传入）非固定优先级；核对 orient→position 翻号，柄嵌墙非悬空） | ✅ | — | Main.qml torchHost + playercontroller placeBlock 传 hitNormal |
| 5 | t126 | 火把选中框按实际形状（WireCube scale 按 hitBlockId 分流：Torch→小立柱 0.12/0.6/0.12 + position 按 orient；其他→全格 1.005） | ✅ | t125 | Main.qml 选中框 Model |
| 6 | t127 | 创造调色板滚动条（Flickable 加 ScrollBar.vertical policy AsNeeded；视口 cellSize*2+8→cellSize*3+12 容 3 行，火把第13项可见） | ✅ | — | Inventory.qml paletteFlick |
| 7 | t128 | 沙子放置 CD（playercontroller 加 m_lastPlaceMs；placeBlock 入口 200ms CD 防连点溢出，仅沙子或全部） | ✅ | — | playercontroller placeBlock |

**执行序**：t122 主编排自修先（已提交）→ 独立项（t124 火把贴图/t127 创造滚动/t128 沙子CD）→ t125 火把朝向→t126 选中框 → t123 动态太阳光照（依赖 t121 顶点光，多轮本轮轮1）。视觉/交互项 needs-run，主编排 run 复核 + token 汇报。

### 第 17 轮 重做批次（手滑动条/火把全套修/不完整方块系统/光照太阳+阴影/创造bug，✅ 2026-07-31）—— t129-t136 workflow（用户验收 t122-t128 不合格，重做仍第17轮）
> 用户验收 t122-t128 不合格：光照阴影一大坨太阳不动 / 火把放下方板透明+挖掉光源残留+墙垂直非60度 / 要不完整方块(slab/stairs/fence/door/trapdoor/pressure plate)/创造拿物丢回消失 / 手要滑动条自调。3 Explore 完整根因+方案（PartialBlockGeometry 合批渲染 / chunk state / 光照方案②增强+可视太阳）。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t129 | 手臂角度滑动条（生存背包加 Slider 调 viewModelHand baseTilt/position.xyz + 实时数值显示，临时调试用） | ✅ | — | SurvivalInventory + Main.qml viewModelHand 绑定 |
| 2 | t130 | 火把透明修复（mesher 邻居面剔除把 Torch 当 air：chunkgeometry.cpp:199 `n!=0 && n!=Torch` 或新 isOpaque） | ✅ | — | chunkgeometry.cpp |
| 3 | t131 | 火把光源残留修复（removeTorchAt 移除所有匹配 + onTorchPlaced 去重 + worldChanged 兜底清孤儿） | ✅ | — | Main.qml torchPositions |
| 4 | t132 | 火把墙 60°（torchHandleEuler 4 朝向 ±90→±60 倾斜 + 火焰位置重算到倾斜柄末端） | ✅ | — | Main.qml torchHandleEuler + 火焰 pos |
| 5 | t133 | 不完整方块渲染系统（新 PartialBlockGeometry 类 switch(blockId) 生成异形顶点；chunkgeometry Torch 特例同级加 `if(b>=FirstPartial){append;continue}` 合批进 chunk mesh；chunk 扩 quint8 state 并行数组 + setBlock state 形参） | ✅ | — | 新 src/Renderer/partialblockgeometry + chunkgeometry + chunk/chunkmanager/world state |
| 6 | t134 | 不完整方块 6 类（WoodSlab=15/WoodStairs=16/WoodFence=17/WoodPressurePlate=18/WoodDoor=19/WoodTrapdoor=20；BlockDef + MC 配方照搬 slab3板→6/stairs6板阶梯→4/fence6板2棒→3/pressure plate2板→1/door3板纵列→3/trapdoor4板→1；图标 flat 2D + 掉落 v1 BlockCube 近似；放置算 state 据命中面；door 两格 + 右键开合 door/trapdoor） | ✅ | t133 | blockregistry + recipe + hotbar creativeBlocks + playercontroller placeBlock state + useBlock 开合 + build_icons |
| 7 | t135 | 光照可视太阳+投影增强（方案②：加太阳 Model 绑 worldClock.sunDir 划天空；kMaxShadow 10→32；sunIntensity gate 放宽 sdy>0 就算；vc 下限 0.3→0.15；步进 72→360 debug 模式） | ✅ | t123 | Main.qml 太阳 Model + chunkgeometry 投影参数 + worldclock 步进 |
| 8 | t136 | 创造丢回消失 bug（面板 Rectangle 加 TapHandler 吸收空点击不到遮罩；调色板 onTapped 覆盖 heldBlock 前先 discardHeldRequested 丢旧手持为实体） | ✅ | — | Inventory.qml 面板 + 调色板 TapHandler |

**执行序**：t133（渲染+state 基础设施）→ t134（6 方块）→ t130/t131/t132（火把，改 chunkgeometry/torchHost）／t129 手滑动条／t135 光照／t136 创造bug 独立并行。视觉/交互项 needs-run，主编排 run 复核 + 完成后自动 codereview（子 agent 查 cpp/qml 逻辑 bug）+ token/时间汇报。

### 第 17 轮 c（出生/背包右键/ESC设置/基岩创造破/水系统/不完整方块碰撞架构/火把真光场/光影PCF/门活版门，✅ 2026-07-31）—— t137-t153 workflow（仍第17轮，用户验收 t129-t136 不合格）
> 用户验收 t129-t136 不合格 25 项：出生高空摔伤 / 背包右键失效 / ESC设置(手调试移) / 光影重做 / 创造破基岩+挖掘音 / 删(0,0,0)实体 / F3+B组合+1人称隐 / 掉落物阴影亮 / 水从未做 / 沙子叠高山应跟水 / 创造滚轮切hotbar+tooltip / 不完整方块图标区分+碰撞箱(基类)+选中框+楼梯方向 / 火把全套(黑边/整格阴影/残像/附着掉落/墙杆/30°/真光照) / 门活版门声音+开关碰撞。3 Explore 完整根因。
| # | 任务ID | 标题 | 状态 | 依赖 | 备注 |
|---|--------|------|------|------|------|
| 1 | t137 | 出生贴地表（componentComplete/setWorld 后查 heightAt 贴地表 m_pos.y=h+1/m_peakY，respawn 同） | ✅ | — | playercontroller 出生点 |
| 2 | t138 | 背包右键失效修复（Inventory.qml DragHandler acceptedButtons 改回非独占右键，或销毁槽走 DropArea，让 root 右键 TapHandler 独占） | ✅ | — | Inventory.qml:612 DragHandler |
| 3 | t139 | ESC 设置菜单（pauseOverlay 加「设置」按钮 + 手调试 ArmSlider 移到设置面板；SurvivalInventory 调试块移除/隐藏） | ✅ | — | Main.qml pauseOverlay + SurvivalInventory ArmSlider |
| 4 | t140 | 创造滚轮守卫 + tooltip（WheelHandler onWheel 加 inventoryOpen 守卫不切 hotbar；创造 tooltip 已挂确认显，rebuild） | ✅ | — | Main.qml WheelHandler + Inventory tooltip |
| 5 | t141 | 基岩创造可破 + 挖掘音（删 beginMining 创造 canMine 守卫；基岩进 mining 态推 stage 挥臂音但 canMine 守 finishMiningAt 不破） | ✅ | — | playercontroller beginMining/updateMining |
| 6 | t142 | 删 (0,0,0) 测试 mob（Main.qml Component.onCompleted spawnMob 删） | ✅ | — | Main.qml:194-198 |
| 7 | t143 | F3+B 组合 + 第一人称隐（f3Held 跟踪+B 条件 f3Held；玩家 hitbox visible 加 cameraMode!==FirstPerson） | ✅ | — | Main.qml F3/B 键 + hitbox visible |
| 8 | t144 | 掉落物亮度适配光照（掉落物材质 baseColor 乘 terrainLight(skyLight) + 顶点色） | ✅ | — | Main.qml 实体 Repeater 掉落物材质 |
| 9 | t145 | 不完整方块图标区分（build_cube_icons 加 6 类 flat 2D 区分图标：半砖半高/楼梯L阶/栅栏柱档/门高板/活版门方格/压力板薄；hotbar 各 case 返对应文件） | ✅ | — | tools/build_cube_icons + hotbar.cpp |
| 10 | t146 | 不完整方块碰撞架构（BlockDef 加 Shape + BlockAABB；BlockRegistry collisionAABBs/selectionAABBs(id,state)；world collidesAt 返回 AABB 列表；玩家碰撞 vs sub-AABB；选中框 WireBox 按 AABB——下半砖 y[0,0.5] 可走/楼梯可走/选中按形） | ✅ | t133 | blockregistry + world + playercontroller 碰撞 + WireBox 选中 |
| 11 | t147 | 楼梯方向排查（state 已 4 向编码，排查 chunkgeometry stateAt 传递/chunk state 存储/horizontalFacing yaw 缓存；扩 8 向 if 要） | ✅ | t146 | chunkgeometry stateAt + chunk state + horizontalFacing |
| 12 | t148 | 水系统（Water id=21/Count22，solid=false/hardness -1/dropId 0；tile 蓝半透 + chunkgeometry N+1；worldgen waterLevel=8 填水；透明渲染 opacity 0.7 + Water 互剔；物理 v1 穿过） | ✅ | — | blockregistry + build_atlas + world generate + chunkgeometry + Main.qml 材质 |
| 13 | t149 | 沙子水位地形（waterLevel=8 + beach 带 h∈[wl-1,wl+1] 沙表层 + 沙漠整柱沙 + h<wl 填水；树/矿石阈值同步水位） | ✅ | t148 | world generate + placeTrees + scatterOres |
| 14 | t150 | 火把全套修（黑边手部材质透明 / 整格阴影 heightmap 回扫跳 Torch 列 / 残像 prefOrient 排查 / 附着挖掉 finishMiningAt 扫邻 Torch 无支撑掉落 / 墙杆 ±0.20→±0.30 深嵌 / 60°→30° + 火焰位置） | ✅ | — | Main.qml torchHost + chunkgeometry heightmap + playercontroller finishMiningAt |
| 15 | t151 | 火把真光场（per-voxel flood-fill 光场 BFS 天光+火把 radius14 存 chunk 第三数组；mesher 写顶点色替代 faceVc；不开 lit/PointLight） | ✅ | t121 | chunk.h 第三光场数组 + chunkgeometry/partialblockgeometry mesher + world flood-fill |
| 16 | t152 | 门/活版门声音 + 开关碰撞（playDoorOpen/Close + doorToggled 信号；isCollidableWhenClosed 合态挡/开态通 → isCollidable 读 state） | ✅ | t146 | audiomanager + playercontroller useBlock + world isCollidable |
| 17 | t153 | 光影 PCF 软影重做（方案③：顶点光基底 + PCF 软影 heightmap 正交深度图 mesher PCF 0..1 软过渡 + t151 真光场；kMaxShadow 短/kSunMin 高调参；步进顺滑） | ✅ | t151 | chunkgeometry PCF 软影 + worldclock 步进 |

**执行序**：独立小修先（t137 出生/t138 右键/t140 滚轮/t141 基岩/t142 删mob/t143 F3B/t144 掉落物/t139 ESC设置/t145 图标）→ t146 碰撞架构（解锁 t147/t152）→ t148 水系统（解锁 t149）／t150 火把／t151 真光场（解锁 t153 光影）。水系统/碰撞架构/真光场/PCF 是 PLAN 级，工作流长跑。视觉/交互 needs-run，主编排 run 复核 + 完成后 codereview + token/时间汇报。

## 第 17 轮 D —— 光影卡顿 + 火把 + 手臂 + 移动 + 地形 + 不完整方块（用户 playtest 反馈）

> **本轮主诉求（用户原话）**：「17D 主要想解决光影还有这个挖掘方块的卡顿问题。放置方块其实也会卡一下。」
> 破/放后贴图停留 3-4 秒才消失 —— 根因已定位（见 t154/t155）。

### 根因分析（主编排 Explore，已确认）
1. **每 `setBlock` 全量光场 BFS**（`world.cpp:49/109/122` `recomputeLightField()`）：48×48×64≈147k 体素 ×2 通道（sky+block）全图 flood，主线程卡顿 → 破/放「卡一下」。
2. **太阳量化步进每 3.3s 全量重建 18 个 mesh**（`worldclock.cpp` `kSunSteps=360`/1200s → 一步/3.3s；`sunChanged` → 所有 `ChunkGeometry.setSunDir→buildMesh()` 绕过 dirty）。日志 `09:33:34→37→41...` 每 3.5s 一次全 9 chunk×2 段重建即此。破块后若编辑 chunk 未即时重建，贴图会停到下一个 sun-step 才消失 = 用户感「3-4 秒」。

| # | 任务ID | 标题（含根因/修法/文件/验收） | 状态 | 依赖 | 备注 |
|---|--------|------------------------------|------|------|------|
| 1 | t154 | **增量光场（核心 perf）**：`recomputeLightField()` 全量 BFS → 改 `setBlock` 后**局部增量**重 flood。破/放：重 seed 受影响列天光 + 有界重传播；火把增删：火把格为中心有界 block-light 重 flood（半径≈16）。全量重算仅留 worldgen 末一次。改 `world.cpp`（setBlock 调局部 `recomputeLightAround(x,y,z,oldId,newId)`）+ `chunk.h`（按需 clear 局部）。验收：破/放单块主线程 <5ms（日志无长 stall） | ✅ | t151 | world.cpp recomputeLightField + chunk.h lightField |
| 2 | t155 | **编辑即时重建 + 太阳步进节流**：确保 setBlock 后编辑 chunk 同步重建（不延迟到 sun-step）；sun-step 重建做帧内合批（已合批，确认 18 重建 <16ms）+ 编辑活跃期（近 N 秒有 setBlock）跳过/延后 sun-step 重建避免抢帧。验收：破块贴图立刻消失（<1 帧），无 3-4s 残留 | ✅ | t154 | chunkgeometry onWorldChanged + worldclock sunChanged 节流 |
| 3 | t156 | **手臂参数固化（用户给定）**：`window.handBaseTilt` 默认 100→**-34.56**；`handPosX/Y/Z` 默认 (0.20,0.05,-0.15)→**(0.36,-0.12,-0.39)** 写死 Main.qml window 属性默认。手持方块（viewModelHand 内 BlockCube）从「手下方」移到「手前方」位置。验收：手臂在默认位置；手持方块在手腕前方 not 下方 | ✅ | — | Main.qml window 属性 + viewModelHand BlockCube position |
| 4 | t157 | **火把全套**：(a) 破后贴图残留 → torchHost Repeater 据 `blockBroken(Torch)` 移除 delegate（排查 model 列表未删）；(b) 去外层静态大橙光源，仅留最内层动态白立方体放大缩小动画；(c) 顶部加少量烟雾粒子（≤3-5 颗，淡出上升，Loader 隔离防崩）；(d) 射线穿透不完整方块：raycast DDA 跳过 Torch（及未来 pressure-plate 等薄格）→ 选中框落其后/下实体方块（火把失支撑→掉落已有 finishMiningAt 扫邻）。验收：破火把贴图即消；只剩内层白立方+少量烟；墙上火把可选中其后墙 | ✅ | t150 | Main.qml torchHost + raycast.cpp + playercontroller finishMiningAt |
| 5 | t158 | **物品栏 tooltip+右键+生存底部**：(a) hover tooltip 创造/生存均恢复显示（排查 MouseArea hover/Tooltip visible）；(b) 右键分半/单个修复（Inventory/SurvivalInventory DragHandler acceptedButtons 不独占右键，让 root 右键 TapHandler/分流生效）；(c) 生存背包底部「手槽区」空缺 → 恢复原布局。验收：hover 显 tooltip；右键可分半/单个；生存背包底部完整 | ✅ | — | Inventory.qml + SurvivalInventory.qml + tooltip |
| 6 | t159 | **疾跑+F3速度+飞行滚轮+水下倍数**：(a) 双击 W 疾跑真正生效（排查 m_lastWms 双击窗 + speedMul Sprint×1.3 实际乘入）；(b) F3 叠层加 `speed` 行（blocks/sec，=水平速度标量，PlayerController 加 `Q_PROPERTY float speed` NOTIFY moveSpeedChanged）；(c) 创造/观察者**飞行**滚轮调速：min 4 / max 20 blocks/s（加 `flySpeedMul` 属性 + WheelHandler 仅 flying 时生效，前滚+后滚-）；(d) 水下（眼位格==Water）速度 *= `kUnderwaterSpeedMul`（常量，~0.4，用户自调）。验收：双击W明显加速；F3 显速度；飞行滚轮变速；水下变慢 | ✅ | — | commit e80d2f5；主编排 build 零警告 + run 健康（root=1,exit0）；F3 speed/fly 行已加 |
| 7 | t160 | **窒息伤害**：生存模式玩家 AABB 嵌入实体方块（脚或身位格 isCollidable）→ 每 ~1s 扣 1HP（发 fallDamageTaken 同路径或新 suffocationDamage 信号）+ 身体红屏闪（HUD overlay）+ 每次扣血视角晃动（相机小抖动）。创造/观察者无伤。验收：生存卡方块里持续扣血+红闪+晃动 | ✅ | — | playercontroller.cpp tick + PlayerState + Main.qml 红屏 overlay |
| 8 | t161 | **沙柱瞬移上爬 + 沙透视挤出方向**：(a) 挖沙柱底+前行不「瞬移到顶」—— FallingBlock 着地/碰撞 resolvePlayerPush 把玩家向上推的 bug，改向外（水平）挤出 not 向上；(b) 被沙覆盖（前方 3 格高+顶放 2 沙）时玩家应被向外（未堵侧）挤出 not 向上。验收：挖沙柱前行不上爬；被覆盖向外挤 | ✅ | t146 | playercontroller moveAxis/overlapSubAABBs + entitymanager resolvePlayerPush |
| 9 | t162 | **地形平滑+减沙+5×5**：(a) heightAt 振幅 `28+n*12`→减小（如 `30+n*6`）更平缓少陡山；fbm 可加平滑；(b) 沙比例降：沙漠阈值/沙滩带收紧（沙主要靠水边 wl±1，减少干旱整柱沙）；(c) chunk 网格 3×3→**5×5=25 chunk**（世界 48→80），QML chunk Model 从 9 扩到 25（terrain+water 各 25），出生居中。验收：地形更平；沙减少靠水；世界明显变大（25 chunk） | ✅ | t148 | world generate/heightAt + Main.qml chunk Models + chunkmanager |
| 10 | t163 | **半砖上下+双半合整+楼梯可走/朝向+3D图标**：(a) 半砖分上半/下半独立放置（据命中面/玩家视线定上半下半）；(b) 同格下半砖上再放下半砖→合并为**完整方块**（state 编码或转 full block）阻挡行走；(c) 楼梯 auto-step ≤0.5 自动抬升（走楼梯不跳）+ 朝向修正（不背对玩家，楼梯开口朝玩家）；(d) slab/stairs/trapdoor/pressure-plate 图标改 3D 立体（同完整方块 cube icon 路径，按形状缩放）。验收：上下半砖可放；双半合整挡走；楼梯可走上不背对；4 类图标立体 | ✅ | t146 | blockregistry + partialblockgeometry + playercontroller auto-step + build_cube_icons + hotbar |
| 11 | t164 | **太阳贴图**：天空太阳 Model 加贴图（非纯色 sphere）—— 复用图集或新增小 sun.png（原创/CC0）。验收：天空太阳显贴图 not 纯色 | ✅ | — | Main.qml sun Model + assets |
| 12 | t165 | **水下可挖+基岩生存持续挖不破**：(a) 水下（眼位 Water）可挖掘（排查水下射线/挖掘被守卫拦截）；(b) 生存基岩：可一直按住挖（保持 mining 态挥臂+音）但**永不破 + 无裂纹**（hardness=-1 → progress 不推进 / finishMiningAt 守卫 + miningStage 恒 -1）。验收：水下可挖；生存基岩可持续挖不破无裂纹 | ✅ | t141 | playercontroller beginMining/updateMining/finishMiningAt |
| 13 | t166 | **阴影暗度参数（ESC）**：ESC 设置加 `shadowDarkness`/`minLight` 滑条 → 调 terrainLight floor 或 kVcMin（用户嫌「黑的地方太黑」）。默认值待用户后续调好给（先给合理默认 + 滑条可调 + 写回 window 属性）。验收：ESC 可调暗度；暗处变亮/暗实时 | ✅ | t153 | Main.qml ESC 设置 + terrainLight/kVcMin |

**执行序**：perf 先（t154 增量光场 → t155 即时重建，解锁「不卡」基础）→ 独立小修并行（t156 手/t158 背包/t164 太阳/t166 阴影参数）→ t157 火把 → t159/t160 移动+窒息 → t161 沙 bug → t163 不完整方块（架构级）→ t162 5×5 地形（最后，因扩 chunk 影响面广）。视觉/交互 needs-run，主编排 run 复核 + 完成后 codereview + token/时间汇报。

**完成状态（17D，2026-08-01）**：13 任务全落盘，主编排 build 零警告 + run 健康（root=1, exit 0, 无 WRN/ERR）。
- workflow（wffqmph9a）跑 t154–t158（2.07M tok / 114 agents / 606 tools / ~227min）后 5h 限额（429）断在 t159；主编排接手 t159–t166 手动实现 + 逐个 build/run 验证 + git 提交。
- ✅ t154 增量光场(16b6e2b) / t155 太阳步进节流+编辑即时重建(d65969f) / t156 手臂参数固化(611aef6) / t157 火把(65e11b0,needs-run) / t158 背包(8e4ce78,needs-run) / t159 疾跑+速度+飞行滚轮+水下(e80d2f5) / t160 窒息(1772c9f) / t161 沙瞬移+挤出方向(fab580e) / t162 5×5地形+减沙平滑(b6b34e0) / t163 楼梯朝向+auto-step(bf75ec8,核心done) / t164 太阳贴图(19f95c0) / t165 水下挖+基岩(t165 8737e80) / t166 ESC暗度参数(19f95c0)。
- 🔜 t163 余项：同格双半砖→完整方块合并；slab/stairs/trapdoor/pressure-plate 3D 立体图标（当前 flat）。
- needs-run（用户肉眼）：t157 火把视觉/射线穿透 / t158 背包 tooltip+右键+底部 / t163 楼梯朝向+auto-step 手感 / t166 暗度滑条。

## 第 18 轮（R18a）—— 背包 UX + 物品方块所有贴图系统 + 世界系统（已完成 ✅）

> 17d 收尾后状态：卡顿已修（commit 2b888d1，dirty-flag 竞态）、hover 已修（cursorTrackLayer→overlayRoot 祖先，
> tooltip 恢复）、工作台右键/双击/手持方块/阴影开关/配方收紧 已落地。R18 聚焦：背包拖动均分 +
> 操作统一重构（用户强调"不能每面板写几份"），

| 任务ID | 状态 | 标题 | 依赖 | 备注 |
|--------|------|------|------|------|
| t167 | ✅ | **左键拖动均分**：背包/工作台/熔炉槽位左键按住拖过 N 格 → 实时均分（floor(count/N)，余数留光标）。旧"右键拖动"改"左键"。hover 已修（overlayRoot 祖先），slot HoverHandler 跟踪划过的槽 + redistributeLive（t79/t98 逻辑改左键） | hover(已修) | Inventory/SurvivalInventory/CraftingTableUI/FurnaceUI |
| t168 | ✅ | **背包操作统一重构**：`resolveClick/resolveRightClick/doMergeSameId/redistributeLive/readSlot/writeSlot` 抽共享 JS 库 `InventoryOps.js`，4 面板+箱子 import 共用 → 一处改处处生效。**t167/t170/t173 前提** | — | 新建 src/ui/InventoryOps.js + 4 面板迁移 |
| t169 | ✅ | **物品方块贴图系统**：四类贴图都要有——①背包槽图标 ②第一人称手持图标 ③丢弃成掉落物的图标 ④放置成方块也好不完整的方块也好的3d方块。修火把掉落物黑底（alpha 透明处理）修复太阳依旧不显示的bug、不完整方块 6 面 flat→3D 立体（slab/stairs/trapdoor/pressure-plate/door/fence 按形状缩放出立体感）、材料段（木棒/煤/木炭/铁锭）图标统一。| t163余 | build_cube_icons + hotbar + Main.qml(手持/掉落物) |
| t170 | ✅ | **火把 bug**：挖掉火把后贴图不清除 → 永久残留无法消除。修：torchHost Repeater 据 `blockBroken(Torch)`/`worldChanged` 移除 delegate + 确保 chunk mesh 不再画该火把格（mesher 已跳 Torch，查 torchHost 列表未删根因） | t157 | Main.qml torchHost + onBlockBroken |
| t171 | ✅ | **创造↔生存切换不清空背包**：当前 cycleMode 切换可能清空主栏/hotbar → 改为保留物品（仅切模式，不动背包） | — | hotbar VM + playercontroller cycleMode |
| t172 | ✅ | **木炭+木棒→火把 配方**：recipe.cpp `torchCharcoal` 已存在，确认生效（原木→木炭冶炼→木炭+棒→火把 闭环）；若不生效排查，并且得维护一个现在存在所有的配方的md文件，放在一个不会被git提交的目录下 | — | recipe.cpp + smelting |
| t173 | ✅ | **箱子方块**：增加的方块必须检查一下t169所有的四种贴图是否完善 + 右键打开有箱子打开动画，关闭也有箱子关闭动画，一共有有 27 槽 UI；物品存 chunk state；复用 t168 共享背包操作 | t168 | 新 Chest block + ChestUI + chunk state |
| t174 | ✅ | **水物理 + 铁桶**：水流蔓延（源+流，MC 式扩散）+ 浮力/游泳；**铁桶（空）+ 装水铁桶** 作创造背包物品 + 合成配方（铁锭→空桶）+ 右键舀水/倒水交互 | t148 | world 水流 tick + bucket 物品段 + recipe + playercontroller useBlock |
| t175 | ✅ | **死亡掉落 + 重生完善**（生存）：死亡时背包全部掉落为物品实体（死亡点）；respawn 已有基础（t78）补掉落 + 出生点 出身点应该固定，而不是死亡后原地复活，唯一全局指定重生点 | — | playercontroller died + ItemEntityManager |
| t176 | ✅ | **ui界面更新+存档系统**（SQLite）：ui更新添加新建世界，在玩家点击游戏主菜单中的单人游戏按钮(原Start Game改成单人模式)后进入，用户输入种子(默认已经填充42)新建世界，并且有esc菜单有退出并保存按钮回退到世界列表 chunk blob（voxels+state+light）+ 玩家态（pos/血/背包/模式）+ 迁移注册表 user_version；退出存/启动读 | — | 暂定新 WorldStore(SQLite) + main 退出钩子， 仿照minecraft游戏本体，使用save保存玩家存档 |
| t177 | ✅ | **音效完善**：脚步（按地形材质）/受伤/环境音；miniaudio 已就绪（t11）补 clip | — | AudioManager |
| t178 | ✅ | **性能打磨**：贪婪网格化（greedy meshing 合并同面，draw-call↓）+ F3 帧时间切分（CPU/GPU/draw-call 预算）；PLAN §4 验收 | — | chunkgeometry greedy + F3 |

**R18 执行序建议**：t169 物品方块贴图系统设计+ t170 火把 bug 先修→ t168 重构 → t167 左键拖动（重构后一处实现）→ t171 模式不清空 + t172 木炭火把 → t173 箱子 → t174 水物理+桶 → t175 死亡掉落 → t176 ui+存档 → t177 音效 → t178 性能。
工作流（voxel-autopilot）跑 t167–t178；视觉/交互项 needs-run，主编排 run 复核。

## 第 18 轮 B（R18b）—— R18a 回归修复 + 补充（已完成 ✅）

> R18a 落地后用户实测发现回归：贪婪网格化拉伸地面贴图、第一人称/掉落物贴图杂交、箱子图标缺失+右键失效、
> 水物理一闪填平。R18b 修这些回归 + 补充背包操作（工作台/熔炉槽参与快捷操作、右键拖动放1个、双击拿手上）。

| 任务ID | 状态 | 标题 | 依赖 | 备注 |
|--------|------|------|------|------|
| t179 | ✅ | **箱子修复**：`icon_chest.png` 注册进 CMake `qt_add_resources`（当前漏注册→运行时 WRN「无法打开」无图标）+ 箱子右键开 ChestUI（当前右键无效，排查 placeBlock→useBlock 的 Chest 路由/ChestUI 打开）+ 确认箱子四类贴图（背包/手持/掉落/放置）齐全 | t173 | CMakeLists + Main.qml + playercontroller useBlock |
| t180 | ✅ | **工作台3×3 + 熔炉输入2格 参与快捷操作**：这些槽也支持 双击拿同类 / 左键拖动均分 / 右键分半（复用 InventoryOps.js；当前只主栏+hotbar 支持，craft 3×3 + furnace 输入槽未接入） | t167,t168 | CraftingTableUI + FurnaceUI + InventoryOps.js |
| t181 | ✅ | **右键拖动=每格放1个 + 双击拿手上**：(a) 右键按住拖过 N 格 → 每滑入新格放 1 个（区别于左键均分 floor(count/N)）；(b) 双击快速拿取同类 → **拿到光标手上**（当前错误：自动合并到背包首个同类槽，且坐标不准）。双击语义=拾取全部同类到光标，不自动合并 | t168 | InventoryOps.js + 4 面板 |
| t182 | ✅ | **第一人称手持 + 掉落物 贴图修复**：二者显示错误"杂交"贴图（不是实际方块；水桶/铁桶两件正常作参考）。排查 t169 重构后 BlockCube/ItemCube 的 tile/blockId 映射（held viewmodel + 掉落物 entity），恢复 per-block 正确贴图。用户强调"以前都好好的，重构后坏" | t169 | Main.qml(手持 viewModelHand + 掉落物 Repeater) + blockregistry tileFor |
| t183 | ✅ | **贪婪网格化贴图拉伸修复**：地面方块贴图被合并拉伸（greedy 合面后一整片大 quad 铺一张贴图，UV 未按格 tile）。修：greedy 合并的 quad **按格平铺 UV**（每 block-unit 重复贴图，非拉伸），保贴图分辨率；若不可快速修则**默认关 greedy**（`setGreedyMeshing` 默认 false，留 ESC 开关供后续调） | t178 | chunkgeometry greedy UV 平铺 |
| t184 | ✅ | **火把可直挖（raycast 细化）**：当前射线永远穿透火把 → 火把不可直接挖（须先挖支撑方块）。改：射线命中火把时**显示火把边界框 + 可挖**；仅当光标在空气 / 不完整方块的空气部分时才穿透到后方实体。即"火把可选可挖、空气可穿"（用户原意） | t157,t170 | raycast.cpp + Main.qml 选中框 |
| t185 | ✅ | **水物理重做**：当前放水桶→水一闪一闪 + 瞬间填平周围（leetcode 接雨水式，错）。改定时器驱动：水源向外**1 格 1 格流动**（有流动动画），最多流 8 格，每流 1 格水位降 1，流到下方为空气的格 = 满水位继续衰减，最终停（不填满整个平面）。修闪烁/填平 bug | t174 | world 水流 tick（QTimer/WorldClock.ticked 驱动） |
| t186 | ✅ | **桶修复**：(a) 空桶右键水源 → 变水桶（当前舀水不变桶）；(b) 第一人称手持 桶/水桶 开口朝下（反了）→ 修正朝向（开口朝上，单独设该两物或整体翻） | t174 | playercontroller useBlock(舀水换桶) + Main.qml 持物朝向 |

**R18b 执行序建议**：t183 贪婪贴图（影响全局视觉，最优先）+ t182 手持/掉落贴图 → t179 箱子 → t185 水物理 → t184 火把直挖 → t180/t181 背包操作 → t186 桶。
工作流（voxel-autopilot）跑 t179–t186；视觉/交互项 needs-run，主编排 run 复核。
> **R18b 完成（2026-08-02）**：8/8 全 ✅；t179 箱子 / t186 桶 needs-run（用户眼下验证中）。构建零错零警，日志零 ERR/WRN（icon_chest WRN 已消）。
> codereview 1 个 HIGH（saves/新世界_2.sqlite 误进 git → 已修：gitignore + git rm --cached，提交 `chore(gitignore)`）；MEDIUM 2（水流每 0.3s 全图扫、贪婪关后顶点升）；LOW 1（遗留 vo.edit/vo.light 诊断 qInfo）。R18b 统计：41 agents / 3.72M tok / 1039 tools / 174min；R18 累计 152 agents / 13.03M tok / 3770 tools / ~634min。

---

## 第 18 轮 C（R18c）—— 用户 playtest 批量反馈：存档/箱子/水/背包/不完整方块/模式（已完成 ✅，2026-08-02）

> R18b 后用户实测一批 bug + UI 改进需求，全部归入 R18c，**一轮做完**。按用户定序排组：玩法优先（箱子 / 背包 / 不完整方块 / 模式），水系统 + 存档/世界管理 UI 靠后。共 25 任务（含 1 验证）。

### A. 箱子
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t194 | ✅ | **箱子放置后透明（根因已定位）**：Chest id=22 ≥ FirstPartial(15) → mesher 路由进 PartialBlockGeometry，但其 switch 无 `case Chest:` → 零几何 → 放置后透明（透视格子）；碰撞/右键正常（ShapeFull + useBlock）。修：给世界 mesher 加 Chest 几何（整立方 + chest 面 tile 映射），或 chunkgeometry 对 Chest 走 culled 立方路径 | t173 | partialblockgeometry / chunkgeometry + blockregistry tile |
| t195 | ✅ | **箱子贴图重做（MC 简洁风）**：现贴图「像工作台」太繁。改 MC 1.0 风：木板顶/侧 + 暗色边框 + 铁箍锁扣（顶盖缝 + 正面锁），极简。重画 default_chest_top/side/front.png | t194 | textures/default_chest*.png + CMake |
| t196 | ✅ | **箱子开合动画**：右键开箱时盖子翻开动画（放置的箱子 Model 盖板旋转 + ChestUI 打开同步） | t194 | ChestUI + Main.qml |

### B. 背包操作
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t203 | ✅ | **2×2 合成栏接右键放1/拖拽均分**：SurvivalInventory 2×2 craft 槽已 import InventoryOps 但右键放1/右键拖未接（t180 只接了 craft 3×3 + furnace）。补 craft 组 resolveRightClick + 右拖，与主栏/hotbar 同 | t180 | SurvivalInventory + InventoryOps |
| t204 | ✅ | **左键拖拽上限=手持数**：现拖过多少格高亮多少（绿格），手持 4 物却能涂 >4 格 → 超分。改：绿格数 ≤ heldCount，超出不高亮不分配 | t167 | InventoryOps redistributeLive |
| t205 | ✅ | **右键拖拽放1机制修复**：右键单击放1正常，但右键「拖」的激活/突出方式有 bug。排查 InventoryOps 右键拖动门控（dragActive 右键路径与左键分发差异） | t181 | InventoryOps |

### C. 不完整方块
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t206 | ✅ | **双半砖挖掉掉 2 个半砖**：现双半砖（合并态）挖掉掉 1 个全木板。改：state 标双砖时破块掉落 2× WoodSlab 物品（非 Planks） | t145 | block break drop + blockregistry |
| t207 | ✅ | **门 UI 图标两格高**：现门背包图标显 1 格。改图标呈现 2 格高（门是 2 格方块），构图/缩放调 | t146 | icon_wood_door.png + InvSlot |
| t208 | ✅ | **门碰撞体积（现可穿过）**：blockregistry 已有 ShapeDoor + isCollidableWhenClosed，但实测可穿。排查：门放置时上下半 state/开合默认值、collisionAABBs 是否对两格都生效、isCollidable 是否读对 state | t146 | blockregistry + playercontroller |
| t209 | ✅ | **栅栏连接 + 1.5 格高**：现 ShapeFence={0.3,0,0.3,0.7,1,0.7}（仅 1.0 高、无连接）。改：(a) 相邻栅栏渲染连接（横臂）；(b) 碰撞 1.5 格高（跳不过，MC 正确） | t146 | partialblockgeometry + blockregistry AABB |

### D. 模式 / 控制
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t210 | ✅ | **滚轮行为按模式切换**：现创造飞行时滚轮=飞行加速（t159 adjustFlySpeed）。改：创造模式滚轮=选 hotbar 槽；仅观察者(spectator)模式滚轮=飞行加速 | t159 | playercontroller/Main.qml wheel handler |

### E. 存档 / 世界管理 UI
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t187 | ✅ | **背包新世界未清空（串世界根因）**：`applyPlayerState` 空分支（新世界首次进入）只 respawn 没清 hotbarVM → 上一世界物品残留带进新世界。补清 9 hotbar + 27 main + heldBlock（镜像非空分支 Main.qml:167-169） | — | Main.qml applyPlayerState |
| t188 | ✅ | **箱子按世界持久化 + 修跨世界泄漏**：cheststore 纯内存不落盘 + enterWorld 没清 chestStore → 内容退出即丢且串世界。加 worldstore `chests` 表 + saveChests/loadChests（纳 saveAll 事务）+ cheststore.clearAll/allPositions + Main.qml 编排 | t194 | cheststore + worldstore + Main.qml |
| t189 | ✅ | **创建世界按钮溢出**：WorldList 新建面板 height:200 < 内容 ~238px → 「创建并进入」按钮挤出底边框。改 ~250 / `height:implicitHeight` / 右列 Flickable | — | WorldList.qml |
| t190 | ✅ | **双击进入世界**：列表 delegate itemArea 加 `onDoubleClicked → playRequested`（单击仍只选中） | — | WorldList.qml |
| t191 | ✅ | **截图封面**：saveAndExit 前 `View3D.grabToImage` 存 sidecar PNG（saves/<file>.png）；worldstore.coverPath(file)；WorldList delegate 左侧 56×56 缩略图（无封面→灰块） | — | worldstore + WorldList + Main.qml |
| t192 | ✅ | **重命名世界**：worldstore.renameWorld(file,newName)（只改 meta `name`，.sqlite 文件名不动，免路径穿越/重命名复杂度）+ WorldList 选中面板重命名 UI | — | worldstore + WorldList |
| t193 | 🔍 | **存档破坏块 round-trip 验证（needs-run）**：用户报「破坏的块重载后消失」，但 saveAll 写全 chunk voxels 看似正确，疑与 t187 串世界同源（新建同种子世界走 regenerate 覆盖存档）。t187 修后复测确认 | t187 | 验证项 |

### F. 水系统
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t197 | ✅ | **水位视觉（流动感）**：现所有水格同高满水位 → 看着是静止水面而非流动。按 state(level) 降水面高度/透明度：水源满高，level↑ → 水面↓ + 边缘流动贴图，显 MC 式逐格衰减流动 | t185 | chunkgeometry 水段渲染 |
| t198 | ✅ | **水中可放方块（排开水）**：现仅桶能收水，方块填不进水格。setBlock/placeBlock 目标格==Water 时直接覆盖（水被排开），水源/流水均可被方块替换 | t185 | world.setBlock / playercontroller placeBlock |
| t199 | ✅ | **空桶只舀水源**：现桶可舀任意水含流水（playercontroller:818 无 state 校验）。改：仅 state==0 水源可舀；流水右键无效（MC 正确） | t186 | playercontroller bucket scoop |
| t200 | ✅ | **水抵消摔落伤害**：落地格==Water → 免摔伤（现高处跳入水仍扣血，playercontroller:1475 无水判）。落地前查脚位水格，是水则不发 fallDamageTaken | — | playercontroller fallDamage |
| t201 | ✅ | **水下蓝滤镜**：眼位 eyeInWater → 浅蓝半透全屏叠层（1/2/3 人称统一），表示在水里 | t202 | Main.qml overlay |
| t202 | ✅ | **气泡 + 溺水系统**：PlayerState 加 air 属性（满 10 气泡）；眼位入水 → 气泡逐格减；归零 → 溺水扣血（1HP/间隔）+ 红闪 + 视角晃（复用 damaged 链）；出水 → 气泡回满后消失。仅头没入水首次出现；UI 气泡条置食物上一行，仅生存模式 | — | PlayerState + playercontroller + Main.qml UI |
| t211 | ✅ | **水流推动玩家**：创造（非飞行）+ 生存模式下，玩家在流水中被水流沿流动方向水平推动（机制等价 MC 水流冲走实体）。流向据脚位水格 4 向邻居 state 梯度推算（从低 state 近源 → 高 state 远源，即离源方向；水源 state=0 格本身不产生水平推力，仅其扩散出的流水格推）；悬崖边落水额外向下带。飞行 / 观察者态不生效（飞行覆盖水中物理）。playercontroller `feetInWater` 分支加水平推力分量 | t185 | playercontroller 水中物理 |

**R18c 执行序**：一轮做完，按组顺序 A→B→C→D→E→F（箱子 → 背包 → 不完整方块 → 模式 → 存档/世界管理 → 水）。工作流（voxel-autopilot）跑全 25 任务；视觉/交互项 needs-run，主编排 run 复核。

---

## 第 18 轮 D（R18d）—— R18c 实测回归 + 不完整方块系统/贴图/沙子水/背包 UX（已完成 ✅，2026-08-03）

> R18c 后用户实测一大批回归 + 新需求，分 8 组。核心痛点：不完整方块系统（半砖放置/射线穿透空气/选中框/门碰撞）、手持掉落贴图（火把黑边 + 木板衍生全显木板）、沙子水交互、水退场动画、箱子开盖、背包丢弃/快捷操作。共 21 任务。
> 基础：需一个 **full-vs-partial 谓词**（BlockRegistry 已有 Shape 枚举；`isFullCube(id)` = shape ∈ {Full, ShapeFull}）—— t213/t220/t226 共用。

### A. 不完整方块系统（最大簇）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t212 | ✅ | **半砖上/下半放置 + 命中面检测**：瞄已放方块的**上 50%**右键→放上半砖；**下 50%**→下半砖（现恒下半砖）。同格上半+下半→合并整砖；墙凹槽下方空位也要能放。修"放了上半砖后同格下半砖放不下"/"瞄上方却放旁边" | t146 | playercontroller placeBlock（命中点 y 与 hitCell y 差值判上下半）+ blockregistry slab state |
| t213 | ✅ | **不完整方块空气透明（射线穿透）**：半砖/火把/栅栏的**空气部分**让射线穿过命中后方方块；仅命中**实体部分**才选中该不完整方块。修"挖半砖背后的方块却撸掉了半砖/火把"（命中点是否落在该方块 sub-AABB 内） | t184 | raycast + blockregistry selectionAABBs（命中点 vs sub-AABB） |
| t214 | ✅ | **火把失支撑立即掉落**：火把支撑方块被打掉→火把**直接掉落为物品**，不粘到附近能支撑的方块 | t157 | blockBroken 链 + itementity（火把支撑检查，失败→掉落非重附着） |
| t215 | ✅ | **双半砖挖掉掉 2 个物品**：现挖双半砖掉 1 个 count-2 栈（捡起加 2）；改掉**2 个独立物品实体** | t206 | block break drop（双砖 state→spawn 2 实体） |
| t216 | ✅ | **选中框去叉叉 + 不完整方块轮廓**：不完整方块选中框现带对角叉叉→去叉只留 AABB 棱；栅栏等 hover 应显**不完整轮廓**（非整立方黑边） | t146 | selectionwireboxes（去对角线）+ partial 轮廓复用 selectionAABBs |
| t217 | ✅ | **门重做（薄板碰撞）**：现门=360°整立方（合=四面挡/开=四面通）。改 MC 风：门占**一面薄板**，3 面恒通，仅门面板那一面合时挡/开则通。修"不打开完全进不去" | t208 | blockregistry door collisionAABBs（薄板 sub-AABB 按朝向）+ isCollidableWhenClosed |

### B. 手持 / 掉落物贴图
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t218 | ✅ | **火把手持+掉落贴图**：现手持/掉落是带黑边的整立方方块（仅放置正常）。改正确火把贴图（细立柱图标，非方块） | t169 | Main.qml viewModelHand + itementity（火把特例：billboard 细长贴图） |
| t219 | ✅ | **木板衍生方块手持+掉落贴图**：楼梯/半砖/压力板/门/栅栏 手持+掉落**全显木板**。改各自正确贴图（t182 重构后 tile/blockId 映射又错） | t182 | blockregistry tileFor（per-block-id 手持贴图映射）+ Main.qml + billboardquad |

### C. 沙子物理
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t220 | ✅ | **沙子下落物理 + 与不完整方块/水交互**：(a) 挖沙柱底→整柱逐格下落；(b) 下落沙遇**不完整方块**（火把/半砖）→变掉落物（仅完整方块可支撑沙）；(c) 沙子落水→穿透下落填堵水格（现沙把水当实心卡在水上一格） | — | FallingBlock（沙可下落判定 + 下方为水/不完整→继续落/变掉落） |

### D. 水系统
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t221 | ✅ | **水退场动画平滑化**：现水被收时**棋盘叉叉一闪一闪**退场（一下有一下无）。改逐格回退（蔓延动画的镜像，一格一格合理退） | t185 | world tickWaterFlow 蒸发 pass（逐环顺序化，避免棋盘震荡） |
| t222 | ✅ | **流动水上放方块水面变透明（bug）**：在**流水**（已降水面）上放方块→水面贴图消失/透明、可透视攻击底下。贴图不应消失（t197 逐水位 + t198 放置交互 bug） | t197,t198 | chunkgeometry 水段 + setBlock（流水格被占→邻接水面仍渲） |
| t223 | ✅ | **水贴图动画 + 水流声**：静止水 2 帧慢播（勿快）；流水流体流动效果（参考 MC）。近流动水一定范围持续水流声（ambience loop） | t197 | textures 水动画帧 + chunkgeometry/Audio（水流声 proximity） |
| t224 | ✅ | **两滩水融合（调研 MC）**：两股流水相遇应融合（现明显边界/各为固方块）。调研 MC 1.0 水合并 + 源再生规则后实现 | t185 | world tickWaterFlow（水合并/源再生；**先调研写结论**） |

### E. 箱子
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t225 | ✅ | **箱子放置朝向玩家 + 开盖动画在格内**：放置时开口朝玩家；开盖动画在**同格上 1/4**（现看上去像是占用上一格方块刷新动画） | t196 | ChestUI + Main.qml（朝向 state + 格内盖板旋转） |
| t226 | ✅ | **箱子上方阻挡开盖判定**：上方**完整方块**→不能开；上方**不完整方块**（半砖/栅栏/楼梯/箱子/火把）→能开。用 isFullCube 谓词 | t213 | Main.qml useBlock（上方格 isFullCube 检查） |

### F. 背包 / UI 操作
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t227 | ✅ | **气泡条位置**：移到**食物（饱食度）条正上方**（现居中于血+食上方） | t202 | Main.qml 气泡 HUD 锚定 |
| t228 | ✅ | **背包内丢弃逻辑**：左键拿物在**面板内非槽位**松手→**不丢**（现直接丢地下）；只有丢出**整栏外**才丢。左键=全丢/右键=逐个 | t167 | InventoryOps + 4 面板（拖出面板边界判定） |
| t229 | ✅ | **Q / Ctrl+Q 丢弃热键**：第一人称 Q=丢 1 / Ctrl+Q=丢整栈（手持槽）；背包内**悬停槽** Q=丢 1 / Ctrl+Q=丢整栈。适用所有背包面板 | t228 | playercontroller Q 键 + InventoryOps（hover slot 丢弃） |
| t230 | ✅ | **Shift+左键快速转移 + 批量合成**：(a) 背包内 hotbar↔main 移 背包内合成槽物品按shift左键也会回到背包槽；(b) 熔炉 Shift+点击→智能入（可烧物→上格/燃料→下格）/ 出；(c) 工作台 3×3 / 生存 2×2 **Shift+点合成产物**→批量合成（耗尽最小原料数，如火把 4 煤+3 棍→3×4=12 根）一次入背包 | t168 | InventoryOps + CraftingTableUI/FurnaceUI/recipe |

### G. 音效
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t231 | ✅ | **基岩挖掘音效节流**：长按挖基岩（不可破）音效连播太快；改与普通挖掘同节奏（几百 ms 间隔） | t165 | playercontroller/AudioManager 挖掘音触发节流 |

### H. 世界列表
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t232 | ✅ | **世界列表封面黑屏**：保存退出后缩略图全黑（t191 grabToImage 未拍到场景）。修截图时机/目标（渲染完成后再抓） | t191 | Main.qml saveAndExit grabToImage + WorldList 缩略图 |

**R18d 执行序**（按依赖 + 痛点优先级，一轮做完）：
1. **第 1 段（基础 + 阻断性 bug）**：t213 不完整方块射线穿透（基础谓词）→ t212 半砖上下半放置 → t217 门薄板重做 → t218 火把贴图 → t219 木板衍生贴图 → t214 火把失撑掉落 → t216 选中框去叉叉。
2. **第 2 段（物理 + 水）**：t220 沙子下落+水交互 → t222 流水放方块水面透明 → t221 水退场动画 → t223 水动画+水流声 → t224 水融合（先调研）。
3. **第 3 段（箱子 + 背包 + 音效 + 列表）**：t225 箱子朝向+格内动画 → t226 箱子上方判定 → t215 双砖掉落 → t227 气泡位置 → t228 丢弃逻辑 → t229 Q/Ctrl+Q → t230 Shift+转移/批量合成 → t231 基岩音节流 → t232 封面黑屏。
工作流（voxel-autopilot）跑全 21 任务；视觉/交互项 needs-run，主编排 run 复核。本轮大，可能撞 5h 限额 → 断在尾部（t224/t231/t232 等低优）。

---

## 第 18 轮 E（R18e）—— 农耕系统 + 生物系统（已完成 ✅，2026-08-03）

> 用户新需求两大系统。农耕：锄头(木/石/铁)→耕地→草丛/种子→小麦生长阶段→收割→面包→吃补饥饿(饥饿开始随时间掉)。生物：猪/牛/羊实体(AI 移动/行走动画/羊吃草/受击红闪/死亡掉落) + 生物蛋 + 创造背包补全。共 12 任务，分 2 组 A 农耕 / B 生物。
> 区隔（§9）：生物名/模型原创方块化，不照搬 MC 美术；机制对齐 MC 1.0。

### A. 农耕系统
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t233 | ✅ | **锄头(木/石/铁)**：3 级工具物品 + 各自贴图 + 合成配方（2 木棍 + 2 木板/圆石/铁锭）。ToolRegistry 注册 hoe 类（专用耕地，不参与挖掘速度） | — | ToolRegistry + recipe + textures(hoe_wood/stone/iron) |
| t234 | ✅ | **耕地方块**：持锄右键泥土/草方块→变耕地（Farmland，新 blockId）；耕地贴图（干/湿两态，水源邻近判定湿润）；碰撞略矮（0.9375） | t233 | blockregistry(Farmland) + playercontroller useBlock(锄头→耕地) + chunkgeometry/partial 渲染 |
| t235 | ✅ | **草丛植被 + 小麦种子**：worldgen 草方块上方随机生成草丛（TallGrass，新 blockId，billboard X 形贴图）；挖草丛→掉**小麦种子**；玩家也可手持种子种 | — | worldgen + blockregistry(TallGrass/Seed) + partialbillboard + itementity drop |
| t236 | ✅ | **小麦作物 + 生长阶段**：种子右键耕地→种小麦作物（WheatCrop，新 blockId，state=阶段 0..7）；WorldClock tick 推进成长（随机/timed）；每阶段不同贴图 | t234,t235 | blockregistry(WheatCrop,state 阶段) + worldclock tick + textures(wheat_stage_0..7) |
| t237 | ✅ | **收割**：挖成熟(state=max)小麦→掉**小麦物品** + 1-2 种子（可再种）；未成熟挖→仅返种子 | t236 | block break drop（按 state 判成熟→掉落表） |
| t238 | ✅ | **面包 + 饥饿系统**：小麦×3 合成面包；右键食面包→恢复饱食度（hunger+）；**饥饿随时间/运动掉落**（hunger depletion tick，WorldClock 驱动；到 0→开始扣血，复用 takeDamage 链） | t237 | recipe(bread) + PlayerState(hunger depletion) + playercontroller useItem(食) + worldclock |

### B. 生物系统
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t239 | ✅ | **生物基类（AI/物理/血量/受击）**：扩展 Entities 层 Entity/EntityManager—AI wander 自主移动（随机选向 + 时间片）、重力、AABB 碰撞、血量、受击/死亡态。为猪牛羊 + 后续 mob 统一基 | — | Entities/entitymanager + entity 基类（mob AI + health + death） |
| t240 | ✅ | **猪/牛/羊 模型 + 贴图**：3 种方块化原创 3D 模型（四肢+躯干+头，§9 区隔不照搬 MC）+ 各自贴图；EntityManager 注册 3 类 | t239 | Entities(模型几何) + textures(pig/cow/sheep) |
| t241 | ✅ | **行走动画 + 羊吃草**：腿摆动 walk cycle（移动时驱动）；羊低头吃草动画→吃掉草丛（草丛变空气）+ 其下草方块变泥土（MC 机制） | t240 | Entities 动画（leg swing）+ 羊 eatGrass AI |
| t242 | ✅ | **攻击/受击/死亡掉落**：玩家左键攻击生物→受伤音效（hurt）+ 身体红闪（受击染色）+ 扣血；血 0→死亡掉落物（猪:生猪排 / 牛:皮革+生牛肉 / 羊:羊毛） | t239,t240 | playercontroller attack(ray hit mob) + Entity(takeDamage/redFlash/drop) + AudioManager.hurt + itementity |
| t243 | ✅ | **生物蛋（spawn eggs）**：创造模式物品（猪/牛/羊 3 蛋），右键地面→生成对应生物 | t239,t240 | blockregistry/item(spawn_egg_*) + playercontroller useItem(spawn mob) |
| t244 | ✅ | **创造背包补全**：加入所有新方块/物品—锄头×3、耕地、草丛、小麦种子、小麦、面包、生物蛋×3、新掉落物（生猪排/皮革/牛肉/羊毛）。创造调色板一览 | 全部 | Main.qml 创造背包调色板 + blockregistry/item 注册 |

**R18e 执行序**（按依赖，一轮做完）：
1. **A 农耕**：t233 锄头 → t234 耕地 → t235 草丛/种子 → t236 小麦生长 → t237 收割 → t238 面包+饥饿。
2. **B 生物**：t239 生物基类 → t240 猪牛羊模型 → t241 行走+吃草 → t242 攻击/死亡 → t243 生物蛋。
3. t244 创造背包补全（最后，依赖前面所有新物品）。
工作流（voxel-autopilot）跑全 12 任务；视觉/交互项 needs-run，主编排 run 复核。本轮任务重（模型/AI/动画），可能撞 5h 限额 → 断在尾部（t243/t244）；主体 agent 接手。

---

## 第 18 轮 F（R18f）—— R18e 实测回归 + 工具系统/生物完善/沙漏水/性能（已完成 30/30 ✅）

> R18e 后用户大批反馈，分 9 组 30 任务。**最痛**：掉落沙内存泄漏(10min→2GB)、沙漠贯穿基岩、玩家被埋可穿出掉到基岩外、mob 1 击即死、工具耐久缺失。新系统：完整工具(剑/斧/铲)+ 耐久 + 暴击。

### A. 草丛 / 植物
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t245 | ✅ | **草丛贴图黑边 + 草/小麦苗区分**：草丛两交叉平面有黑边（alpha 边缘处理）；小麦苗 stage0 与草丛太像 → 视觉区分（贴图重画） | t235 | textures(tall_grass/wheat_stage_0) + billboard alpha |
| t246 | ✅ | **挖草概率掉种子**：现 100% 掉种；改概率掉落（MC ~12.5%，可配） | t235 | block break drop（概率门控） |
| t247 | ✅ | **草方块/小麦作物失撑掉落**：挖底方块→其上草方块/小麦应**掉落**（现悬空）；草根+作物须依附下方实体方块 | t235,t236 | blockBroken 链（失撑→变掉落物，同火把支撑） |

### B. 生物系统（完善簇）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t248 | ✅ | **mob 血量修正 + 受击专属音效**：1 击即死→改 10HP（5 心，空手需多击）；受击音复用挖方声→换**专属 mob 受伤声** | t239,t242 | EntityManager(maxHealth 10) + AudioManager（mob hurt 音，非 mining） |
| t249 | ✅ | **击退 + 跳劈暴击**：受击往攻击方向**小跳击退**；玩家跳起攻击=**暴击**（+50% 伤害，research MC crit 计算） | t242 | EntityManager(knockback) + playercontroller(jumpAttack crit) |
| t250 | ✅ | **mob 环境音**：牛叫/羊叫/猪叫 idle 叫声（周期）+ 走路声 | t240 | AudioManager（mob ambient + step，程序合成） |
| t251 | ✅ | **mob 加眼睛 + spawn egg 贴图重做**：现无眼睛（怪）；3 蛋贴图难辨→重画区分 | t240,t243 | MobModel(眼睛部件) + textures(spawn_egg_*) |
| t252 | ✅ | **mob 碰撞箱缩小 + F3+B 显朝向**：碰撞感整立方大→缩小（猪 0.9×0.9 / 牛 0.9×1.4）；F3+B 实体框无朝向→加 mob facing 线 | t239 | EntityManager(radius/AABB) + wiresquare debug |
| t253 | ✅ | **攻击单体选中**：近距两 mob 只打**一个**（射线最近命中，非 AoE 多尸） | t242 | playercontroller attack（ray→最近 mob） |
| t254 | ✅ | **mob 窒息**：被沙/方块埋住→窒息扣血（机制同玩家，t160 链） | t239,t256 | EntityManager(suffocation tick) |

### C. 沙子 / 地形 / 性能（critical）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t255 | ✅ | **沙漠 worldgen 修正**：沙**贯穿到基岩**（错）；改仅表层 4-6 格下接石头 | — | world.cpp worldgen（沙漠沙层厚度） |
| t256 | ✅ | **掉落沙内存泄漏排查**：玩 ~10min→2GB/卡顿，重启恢复（疑沙掉落实体/光场/重建未释放）。根因=QML mobHost/itemHost Repeater 的 reparent 3D delegate count 减小不销毁（t170 族）× 掉落沙高频 spawn/land 抖动 → delegate 累积；C++ 审计干净（泄漏在 QML 场景图侧）。修法 slot-reuse（两 manager 移除改 release 不 erase → count 单调不降 → Repeater 不需销毁 delegate）+ delegate visible:aliveAt + F3 draw 用 liveCount | t220 | EntityManager/ItemEntityManager（slot-reuse）+ Main.qml（delegate visible） |
| t257 | ✅ | **掉落沙光影 bug**：沙掉落时变亮（未用顶点光/软影）；暗处挖底沙→掉落沙明显变亮 | t220 | FallingBlock 渲染（顶点色光 + PCF 软影接入） |
| t274 | ✅ | **地形平整 + 草原群系**：现纯山地凹凸不平；改平整——大草原=平地+多草丛，山地仅特定群系。heightAt 振幅降低 + 群系分流（plains 平 / hills 起伏 / desert 沙）。新世界生效（旧存档走 chunk blob 不受影响） | t162 | world.cpp worldgen（heightAt 振幅 + biome 分流 + plains 草丛密度） |

### D. 玩家物理
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t258 | ✅ | **被埋锁定（穿出 bug）**：玩家被沙埋→现可前后左右穿出/掉出基岩外（像观察者）；改**锁定不能动**，只能挖出卡住的方块脱困 | t256 | playercontroller（被实体方块完全包围→禁移） |
| t259 | ✅ | **蹲下 1.5 格碰撞**：shift 蹲→碰撞高 1.5（可通过 整砖+下半砖=1.5 通道） | — | playercontroller(sneak AABB 1.5) |

### E. 火把
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t260 | ✅ | **火把光效 + 手持动画 + 手持贴图放大**：现仅白光（像白炽灯）→多色火焰 + 偶发烟雾粒子（research MC 火把）；手持火把加燃烧动画；手持贴图太小→放大 | t218 | TorchSmoke/Main.qml（火把光多色 + 烟 + 手持 anim） |

### F. 门 / 半砖
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t261 | ✅ | **门开态仍挡一面**：开门后四向全通（错）；开态应仍挡铰链那一面 | t217 | blockregistry door collisionAABBs（开态保留铰链侧） |
| t262 | ✅ | **半砖角落：墙上侧面放上半砖**：角落下半砖上想沿邻墙**侧面**放上半砖（非顶面）→现不行，应支持 | t212 | playercontroller placeBlock（邻墙命中面→上半砖） |

### G. 工具系统（新）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t263 | ✅ | **工具耐久系统**：现锄/镐用一次就消耗（太贵）；参考我的世界耐久系统，配置不同工具的耐久度，木头耐久度最低以此类推，最好能在鼠标放在背包物品的悬浮框显示信息的里面加上耐久度数字比如5/255表示还剩下五次耐久，即挖掘五个方块，或者剑的话就是造成五次攻击，锄头是锄五次耕地 durability（使用-1，归零破坏） | t233 | ToolRegistry/Hotbar（item durability 字段 + 消耗） |
| t264 | ✅ | **完整工具集**：加**剑/斧/铲**（+ 既有镐/锄）；木/石/铁 三材质各 5 件 | t233 | ToolRegistry + recipe + textures |
| t265 | ✅ | **工具挖掘速度效果**：斧→木制品(原木/板/工作台/箱/木台阶)加速；铲→沙/土/草/砾加速；镐→石/石制品加速（**铁镐削弱**，留金/钻石档空间）；剑→加攻击伤害 | t264 | ToolRegistry materialGroup×tool 速度表 + playercontroller 剑攻击 |
| t266 | ✅ | **镐手持贴图修**：现铁镐手持=纯白铁棍 + 手拿镐头中间（错）；应显木质柄 + 镐头、正握 | t264 | Main.qml viewModelHand（工具手持朝向/贴图） |

### H. 食物 / 背包
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t267 | ✅ | **面包长按右键进食**：单击即食→改**长按**右键（手落下+抖动+屑粒动画→消耗）；非单击 | t238 | playercontroller useItem（hold 进食 + 粒子） |
| t268 | ✅ | **工作台界面左键拿取物品的时候 shift+左键批量合成**：鼠标左键拿取物品的时候在工作台 shift+左键→应触发一键批量合成（查 shift+craft 路径覆盖手持态） | t230 | InventoryOps shift+craft（手持态入口） |

### I. 水
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t269 | ✅ | **水流声改"流水" + 水中走路声**：现像海浪→改潺潺流水声；水中走加 underwater step 音 | t223 | AudioManager（流水声替换 + underwater step） |
| t270 | ✅ | **流水推力增强**：浮水按空格被推太弱→增强（流水中持续外推） | t211 | playercontroller 水流推力系数 |
| t271 | ✅ | **水冲走掉落物**：item 掉落物入水→浮水面 + 随流移动（被水流冲） | t220,t211 | ItemEntity（浮力 + 水流水平推） |
| t272 | ✅ | **平面边缘 cascade 多流一格 + 排开水复测 + 水融合汇报**：水平边缘水流应多流一格再下落（现直接断）；流水/静水仍可放方块（排开，复测 R18d t222）；附 t224(R18d) 水融合源再生状态说明 | t185,t198 | world tickWaterFlow（边缘 cascade）+ setBlock 排开复测 |
| t273 | ✅ | **流动水里放水 + 放水后不流动**：(a) 水桶右键**流动水**格→现放不下（应能放，覆盖/升源）；(b) 放置的水源**不立即流动**（应下一 tick 触发蔓延）。查 bucket 水放置（t186 桶路径只认水源舀，未覆盖"放"在流水）+ setBlock 后 tickWaterFlow 触发 | t185,t186,t198 | playercontroller bucket（放水路径）+ world setBlock/tickWaterFlow |

**R18f 执行序**（critical bug 优先，5 段，一轮做完 / 限额断尾部主体接手）：
1. **第 1 段（阻断/critical）**：t256 掉落沙内存泄漏（最痛）→ t255 沙漠穿基岩 → t258 被埋锁定穿出 → t248 mob 血量+受击声 → t257 掉落沙光。
2. **第 2 段（生物完善）**：t249 击退+暴击 → t250 环境音 → t251 眼睛+蛋贴图 → t252 碰撞箱+朝向 → t253 单体选中 → t254 mob 窒息。
3. **第 3 段（植物/火把/门/半砖/物理）**：t245 草贴图 → t246 概率 → t247 失撑掉落 → t260 火把 → t261 门 → t262 半砖角 → t259 蹲下。
4. **第 4 段（工具系统）**：t263 耐久 → t264 工具集 → t265 速度 → t266 镐贴图。
5. **第 5 段（食物/背包/水）**：t267 面包进食 → t268 shift 合成 → t269 水声 → t270 推力 → t271 冲物 → t272 cascade+排开复测。
工作流（voxel-autopilot）跑全 28 任务；视觉/交互项 needs-run，主编排 run 复核。本轮最大（28 任务 + 新工具系统），可能撞 5h 限额 → 断在尾部第 4/5 段；主体 agent 接手。

> **关于 t224 水融合（你问的）**：R18d t224 已实现 MC 1.0 源再生（流水格被 ≥2 水源夹+grounded→升源）+ re-leveling（取 min→V 形平滑融合），verdict pass。两滩水靠近（中间格被两源夹）会融合成连续水源体；单桶水扩散出的流水不升源（同 MC）。t272 附带复测。

---

## 第 18 轮 G（R18g）—— 大世界 + 洞穴系统 + 敌对生物（已完成 ✅，2026-08-04）

> 大更新。A 大世界扩展 + F3 区块边界 → B 洞穴生成（carve 隧道/分叉/裸露矿物）→ C 黑暗刷怪系统（光照+距离门控）→ D 四种敌对生物（僵尸/骷髅弓箭手/苦力怕/蜘蛛 + 寻路 AI + 动画 + spawn egg）。共 12 任务。区隔（§9）：怪物名/模型原创，机制对齐 MC 1.0。

### A. 大世界 + 区块显示
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t276 | ✅ | **大世界扩展**：5×5(25 chunk/80×80)→ 更大固定网格（如 10×10=100 chunk / 160×160，可配）；worldgen 覆盖全幅 + 性能预算；流式加载推迟 Phase 2 | — | CMake/World dims + ChunkManager 扩容 + worldgen |
| t277 | ✅ | **F3 区块边界显示**：16×16 网格线叠层（toggle，MC 式显示 chunk 边界） | t276 | Main.qml + Renderer（chunk grid wireframe overlay） |

### B. 洞穴生成
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t278 | ✅ | **洞穴隧道生成**：terrain 后 carve（3D Perlin 阈值 / random-worm 隧道 + 分叉路口）；内部黑暗（不填天光）；连通性 | t276 | world.cpp worldgen（cave carve pass） |
| t279 | ✅ | **洞穴裸露矿物**：矿物 worldgen（已有）+ 洞穴 carve 自然暴露；调矿物密度/高度分层（煤浅/铁中/钻石深） | t278 | worldgen（ore 分布 + 暴露） |

### C. 黑暗刷怪系统
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t280 | ✅ | **黑暗刷怪调度**：周期 spawn——light < 阈值(7) + 距玩家 > N 格(24) + 总数上限；夜晚地表 + 洞穴均可刷；白天 zombies/skeletons 燃烧消失（research MC 刷怪规则） | t278,t281 | EntityManager spawn scheduler + skyLightAt 门控 + WorldClock |

### D. 敌对生物（4 种 + AI）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t281 | ✅ | **敌对生物基类（AI/寻路）**：detect player（4-5 格 or MC 规则）+ 寻路（向玩家走 + 跳/绕障，简化 A*）+ attack；扩展 EntityManager 敌对分支 | t239 | Entities（hostile AI + pathfind + attack） |
| t282 | ✅ | **僵尸**：近战，走向玩家攻击（原创模型 + 贴图，§9） | t281 | Entities + textures |
| t283 | ✅ | **骷髅弓箭手**：远程射箭（arrow 实体 + 抛物 + 命中伤害；保持距离） | t281 | Entities（arrow projectile） + textures |
| t284 | ✅ | **苦力怕**：近距蓄力膨胀动画 → 爆炸（破坏方块 + 伤害玩家 + 音效） | t281 | Entities（creeper inflate + explode + block break） |
| t285 | ✅ | **蜘蛛**：快速移动（可爬墙；昼伏夜出） | t281 | Entities（spider climb/fast） + textures |
| t286 | ✅ | **敌对生物动画**：walk + attack 动画（腿摆/挥手/爆炸膨胀） | t282-t285 | Entities 动画 + MobModel |
| t287 | ✅ | **怪物 spawn eggs + 创造背包补全**：4 怪 spawn egg（右键生成）+ 创造调色板加 4 怪蛋 | t282-t285 | blockregistry/item(spawn_egg) + Main.qml 创造背包 |

**R18g 执行序**（按依赖，一轮做完 / 限额断尾部主体接手）：
1. **第 1 段（世界基础）**：t276 大世界 → t277 F3 区块显示 → t278 洞穴生成 → t279 裸露矿物。
2. **第 2 段（刷怪 + 敌对 AI）**：t281 敌对基类 → t280 刷怪调度 → t282 僵尸 → t283 骷髅 → t284 苦力怕 → t285 蜘蛛。
3. **第 3 段（动画 + egg）**：t286 动画 → t287 spawn egg + 创造背包。
工作流（voxel-autopilot）跑全 12 任务；视觉/交互项 needs-run，主编排 run 复核。本轮重（4 怪模型/AI/寻路/箭实体/爆炸），可能撞 5h 限额 → 断在尾部（t285/t286/t287）；主体 agent 接手。

---

## 第 18 轮 H（R18h）—— 生存/模式 bug 修复 + 弓箭/羊毛剪刀/树叶树苗/生态地形/死亡聊天指令（待开工）

> R18g 后用户大批反馈，分 10 组 29 任务。**最痛（critical）**：生存中键复制方块、玩家移动偶发锁定（WASD/空格失效）、观察者能捡物/被怪仇。新系统：弓箭、羊毛+剪刀、树叶树苗衰减、生态群系（森林+草原）、地形抬高、铜金锭、死亡原因+聊天+`/give` 指令。⚠️ 本轮最大（29 任务 + 多新系统），可能需 2 段工作流（撞 5h 限额分段）。

### A. 玩家 / 模式 bug（critical）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t288 | ✅ | **生存中键复制方块 bug**：生存模式按中键能复制方块（应仅创造）。门控中键 pick-block 仅 Creative | — | playercontroller/MouseArea pick-block（mode 守卫） |
| t289 | ⚠️ | **玩家移动偶发锁定**：WASD 脚步声有但画面不动、空格无效、仅 shift 蹲；切观察者可动；创造也偶发。查 step()/wishHoriz/速度门控（疑 t258 被埋锁定的判定误触发或 wish 输入丢失） | t258 | playercontroller step/moveAxis（最优先排查） |
| t290 | ✅ | **观察者交互门控**：观察者能捡物品（错——不应放/破/捡任何东西）；敌对怪仇恨+射观察者/创造玩家（错——只仇生存玩家）。pick 门控 + hostile target 仅 Survival | — | playercontroller pickup + EntityManager hostile target（mode 判） |
| t291 | ✅ | **创造中键切槽**：中键时若 hotbar 1-9 已有同方块→切到该槽（非复制替代当前手持） | t288 | pick-block 逻辑（先查同 id 槽） |
| t292 | ✅ | **创造背包归还物品消失**：创造背包拿起物品再放回→应**消失**（非丢出到世界） | — | InventoryOps 创造归还路径 |

### B. 生物碰撞 / 音效
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t293 | ✅ | **mob 碰撞箱仅 F3+B**：现 hover 常显碰撞箱（应仅 F3+B）+ 缩小碰撞箱贴合身体（现大一圈） | t252 | Main.qml hitbox visible（仅 showHitboxes）+ EntityManager AABB 收紧 |
| t294 | ✅ | **被动 mob 环境音**：牛叫/羊叫/猪叫/怪物叫声 idle 叫声（现只有脚步声） | t250 | AudioManager（mob ambient 程序合成） |
| t295 | ✅ | **mob 受击音效 + 敌对专属**：受击无音（现击退有）；敌对各:骨头敲击/蜘蛛嘶(近)/僵尸哀嚎/苦力怕爆炸声 | t248 | AudioManager（hurt + 敌对专属音） |

### C. 敌对 AI 门控 + 爆炸
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t296 | ✅ | **敌对仇恨仅生存**：创造/观察者不被仇恨（苦力怕不走向/僵尸不追/骷髅不射）；玩家攻击/箭对 mob 有击退 | t290 | EntityManager hostile AI（target 仅 Survival player）+ knockback |
| t297 | ✅ | **苦力怕爆炸掉落 + 水中不破坏**：爆炸破坏方块但无掉落→改 ~50% 成掉落物；水中爆炸不破坏方块 | t284 | EntityManager detonateStalker（drop 50% + water check） |
| t298 | ✅ | **怪物受水流影响**：怪在水中正常走（错）→减速/浮（同玩家水中物理） | t211 | EntityManager tick（water physics for mobs） |

### D. 敌对掉落 + 羊毛 / 剪刀（新）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t299 | ✅ | **敌对掉落物**：Bones(骷髅)→弓(带耐久)+剑+骨头；Shambler(僵尸)→腐肉；Spider(蜘蛛)→线 | t242 | mobDied 掉落表 |
| t300 | ✅ | **剪刀 + 羊毛 + 剪羊毛**：铁锭→剪刀；右键羊→剪羊毛（羊变秃+掉羊毛）；羊毛方块；羊吃草方块→长回毛（草方块→泥土） | t299 | recipe(剪刀/羊毛) + EntityManager shear + sheep eat grass block |

### E. 骷髅 / 蜘蛛模型 + 蛋图标
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t301 | ✅ | **骷髅模型 + 持弓**：现纯白人形（同僵尸但白）→骷髅外观 + 持弓（因射箭）打死之后掉落物有弓+箭不是100%掉落 | t283 | MobModel Bones 分支 + 弓部件 |
| t302 | ✅ | **蜘蛛模型**：现在的问题是全黑/无眼/无腿像蟑螂（一长方体+小方块）→加 8 腿爬行 + 眼 +走动动画以及声音| t285 | MobModel Spider 分支（腿+眼） |
| t303 | ✅ | **生物蛋图标**：创造背包蛋显方块→蛋形图标（区分各 mob 配色斑点） | t287 | MaterialIcon spawn-egg 自绘蛋形 |

### F. 弓箭系统（新）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t304 | ✅ | **弓 + 箭**：木棍+蜘蛛丝→弓；箭；长按右键拉弓动画→松开射箭（抛物+伤害 mobs）；拉弓减速（叠 shift）；需箭在背包；弓伤害 tooltip | t299,t249 | recipe(弓/箭) + playercontroller bow draw/fire + Arrow（复用 t283 箭实体） |

### G. 树叶 / 树苗（新）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t305 | ✅ | **树叶衰减 + 树苗**：挖光一棵树所有原木→树叶消失；叶掉木棍/树苗；树苗种植→长大成完整树（时间推进） | t26 | worldgen tree + leaves decay（邻接原木判定）+ sapling growth |

### H. 生态 / 地形 / 洞穴 / 水（新大）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t306 | ✅ | **生态系统（森林+草原）**：5×5 划分群系——森林（现多树）+ 草原（少树多草）；worldgen biome 路由 | t274 | worldgen biome（forest/plains 分流） |
| t307 | ✅ | **地形高度提升**：现地表 ~30 格（计划 ~64+）；抬高 heightAt 振幅基线 至少地面要64格左右| t162 | world.cpp heightAt（基线抬高） |
| t308 | ✅ | **铜锭 + 金锭 + 钻石深度修正**：加铜/金锭（钻石工具前）；钻石生成太高→改 ≤Y=40（research MC 钻石深度） | t279 | worldgen ore 深度 + item(铜/金锭) 从铁开始掉落的矿石都要烧制，也就是掉落的是矿石，得再熔炉里面来烧制成锭，铜 铁 金，他们也是按照顺序更加稀少的，但是钻石挖掘就还是钻石的样子|
| t309 | ✅ | **洞穴入口 + 地下水 + 地表湖**：多地表连通洞穴入口（草原/森林概率）+ 地下水池（封闭洞穴静止水层）+ 地表小湖泊（部分露出） | t278,t306 | worldgen cave（地表连通 + 水池/湖） |
| t310 | ✅ | **草变种（矮/中/高）**：草丛现恒 1 格满 opaque（像 A4 纸）→改:矮草(1格半高)/中/高草(2格)，半透细立柱（像火把）；各群系密度 | t235 | TallGrass（变种 state + 半透 billboard） |

### I. 死亡 / 聊天 / 指令（新）
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t311 | ✅ | **死亡原因**：窒息/淹死/被僵尸/骷髅/蜘蛛/苦力怕杀——各来源记死因 | t202 | PlayerState/takeDamage（cause 字段） |
| t312 | ✅ | **聊天栏 + 死亡播报**：T/Enter 打开聊天栏打字（显示「名: 文本」）；死亡信息在聊天栏播报 | t311 | Main.qml chat bar + 死亡消息路由 |
| t313 | ✅ | **死亡画面显原因**：死亡重生屏加死因文案（两处:聊天+死亡屏） | t311 | Main.qml death overlay |
| t314 | ✅ | **指令 `/give`**：`/give <id> [count] [durability]`（调试用，如 `/give 10 1 100` 给耐久 100 铁剑）并且要产出一个md文件来记录你编写的id，要有合理性，最好可以参考一下真实的我的世界的id，反正物品都叫这个名字，没事的 | t312,t263 | chat 命令解析 + hotbar give |

### J. 耐久 UI + F3+G
| 任务ID | 状态 | 标题 | 依赖 | 备注/文件 |
|--------|------|------|------|------|
| t315 | ✅ | **工具耐久 UI**：hover 格式「名\n\n耐久: x/x」；全满不显耐久条；用后显耐久条 绿→黄→红→0 破坏+音效+移除 | t263 | InventoryOps tooltip + hotbar durability bar |
| t316 | ✅ | **F3+G 区块边界改进**：现细红线太简→参考 MC 更明显（黄/紫边框线） | t277 | chunkgridlines（颜色/粗细） |

**R18h 执行序**（critical bug 优先，10 段，可能 2 段工作流跑完 / 限额分段主体接手）：
1. **第 1 段（critical 玩家 bug）**：t289 移动锁定（最痛）→ t288 中键复制 → t290 观察者门控 → t291 中键切槽 → t292 创造归还。
2. **第 2 段（生物碰撞/音效/AI 门控）**：t293 碰撞箱 → t294 环境音 → t295 受击音 → t296 敌对仇恨门控 → t297 苦力怕掉落 → t298 怪水中。
3. **第 3 段（掉落/羊毛剪刀/模型）**：t299 敌对掉落 → t300 剪刀羊毛 → t301 骷髅持弓 → t302 蜘蛛模型 → t303 蛋图标。
4. **第 4 段（弓箭/树叶树苗）**：t304 弓箭 → t305 树叶树苗。
5. **第 5 段（生态地形水矿物）**：t306 群系 → t307 地形抬高 → t308 铜金+钻石深 → t309 洞穴水湖 → t310 草变种。
6. **第 6 段（死亡聊天指令/UI）**：t311 死因 → t312 聊天 → t313 死亡屏 → t314 give → t315 耐久 UI → t316 F3+G。
工作流（voxel-autopilot）跑；视觉/交互项 needs-run，主编排 run 复核。本轮最大，建议分 2 批工作流跑（第 1-3 段一批、4-6 段一批）或按序跑让限额断尾部、主体接手。

### 放大阶段（🔜 推迟，本轮后）—— 不做
| 任务ID | 标题 | 状态 | 依赖 | 备注 |
|--------|------|------|------|------|
| t07 | 世界放大 256×256 + simplex 高度图 | 🔜 | t02,t03 | 3×3 之后再放大 |
| t08 | 树生成（确定性，烘 WorldgenVersion） | 🔜 | t07 | §2 不变量 K |
| t09 | 昼夜（天光亮度乘子 lerp ~20min） | 🔜 | — | 独立可插；§2 不变量 H |
| t10 | F3 调试叠层（fps/chunk/mesh/线程/pos） | 🔜 | t03 | §2 不变量 F |
| t11 | 3 SFX（破/放/脚步）via miniaudio | 🔜 | t05 | §4 原创 SFX |
| t12 | 资产门（贴图/字体/GUI 铬，具名来源） | 🔜 | t01 | §4 资产门 |
| t13 | 质量门：零警告 + Win/Linux CI | 🔜 | (全部) | 收尾 |

共 **38 个任务**（R1 5✅；R2 6✅；R3 6✅；R4 5✅；R5 验收 bug 修复 8 项 ✅/⚠️；**R6 9⏳（本轮规划）**；放大阶段 7🔜）。

### 执行序（第 2 轮，建议）
```
t15 ─> t16                 （先修 bug：键位/图标 + 粒子）
t02 ─> t03                 （3×3 地形：数据 chunkify + 每 chunk mesher）
t17                        （主菜单，独立）
t18                        （背包，依赖 hotbar）
```
本轮 6 任务约 **5–7h**；无 `+Nk` 则跑完全部 ⏳。建议序：t15→t16→t02→t03→t17→t18（先修 bug、再地形、最后 UI）。

---

# R18i 规划（R18h playtest 反馈修复 + 新系统）

> 来源：用户 R18h 全量 playtest 反馈（2026-08-05）。任务号续 R18h（t316 止）→ t317 起。
> 状态符号同主 plan：⏳ 待做 | 🔄 进行中 | ✅ 完成 | ⚠️ 低质量通过 | 🔜 推迟。
> **执行原则**：P0（崩溃/卡顿/锁死/回归）先做；资产重做（用户 0-1 分）紧随；新系统（岩浆/装甲）最后且可拆 R18j。

## ⚠️ 资产政策（适用 t326/t328-t332/t336/t345 等所有音/图任务）
- **绝不使用 MC 本体音频/贴图/名称**（PLAN §9 红线，Mojang 版权）。
- 当前音效 0-1 分、草丛"两片 A4"、弓像两根棍——**是生成器/几何的 bug，本轮重做**：程序合成按真实音色频谱；贴图按像素 alpha billboard。
- 后续做 **resource-pack 加载器**（功能）让用户自填包，仓库不打包 MC 资产。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **P0** | t317 t319 t320 t321 | 跳跃/水中爆炸/爆炸卡顿内存/攻击频率——卡死或瞬死 |
| **P1** | t318 t322 t323 t324 t325 t327 t333 t334 | 玩法 bug（归还/弓箭/树叶/水流/光照） |
| **P2** | t326 t328 t329 t330 t331 t332 | 资产重做（草丛/音效/剪刀/弓/骷髅/工具贴图） |
| **P3** | t335 t336 t338 t339 t341 t346 t347 t349 | 活板门碰撞/木楼梯/沙海/矿井洞/洞穴入口/指令/UI |
| **P4** | t337 t340 t342 t348 | 群系密度/湖泊形态/大峡谷/ID 对齐 |
| **P5** | t343 t344 t345 | 新系统（岩浆/着火/装甲）——可拆 R18j |

**建议执行序**：P0 → P1 → P2 → P3 → P4 → P5（拆轮）。

## A. 关键 bug（P0/P1）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t317 | ✅ | **生存跳跃高度不足**：按空格只跳半格，上不去 1 格方块（创造飞行不受影响→问题在生存跳跃冲量/重力积分）。排查 t289 容差改动是否波及跳跃，或重力步长致跳高<1.0。**验收**：生存原地起跳稳定落到 y+1 方块顶面（跳高≥1.05 留余量）。 | — | playercontroller.cpp（jumpVelocity/tickImpl 重力） |
| t318 | ✅ | **创造背包归还物品应为切换**：拿起物品（跟鼠标）后，再点背包格不是放回而是又拿起该格。**应为切换式**：点空格/原格=放回（消失，创造不丢世界），再点=拿起。注：t292 标 ✅ 但用户仍报错→复查 heldCursor 归还路径。**验收**：创造背包点格拿起→再点放回（消失）→再点拿起，切换正常不重复拾取。 | t292 复查 | Main.qml（创造背包 click）+ hotbar.cpp/InventoryOps |
| t319 | ✅ | **苦力怕水中爆炸仍破坏方块（回归）**：t297 应已修（originInWater 跳过破坏球）但失效。**验收**：爬行者水中（身/脚入水）爆炸→不破坏任何方块；陆地正常破坏。扩大判定：爆炸球内任意点触水即跳过，或 origin+半径扫描。 | t297 复查 | entitymanager.cpp detonateStalker |
| t320 | ✅ | **苦力怕爆炸后严重卡顿 + 内存 1GB+**：爆炸后 FPS 8-9（常态100），内存>1GB，需重启。排查：① 爆炸 50% 掉落×大量方块→几百 item entity 每帧更新；② 大量方块破坏触发 chunk dirty 风暴（[[chunk-dirty-flag-race]]）；③ 掉落物/实体不回收（泄漏）。**验收**：爆炸后 30s 内 FPS 回升≥60；掉落物硬上限（如 200，超时 oldest 消失）；内存稳定不单调涨。 | — | entitymanager.cpp（掉落物上限+回收）+ Renderer chunk 重建批合并 |
| t321 | ✅ | **怪物攻击频率过高**：被围殴瞬死（尤其僵尸），攻击不停。**验收**：每怪攻击有冷却（僵尸~1s/次，骷髅拉弓+射击有间隔），单怪 DPS 合理，群怪不叠加瞬死。 | — | entitymanager.cpp（aiHostile/aiArcher attack cooldown+单次伤害） |
| t322 | ✅ | **无箭可拉弓**：创造/生存没箭也能拉弓。MC 规则：**创造免费射箭**（不消耗），**生存必须有箭**才能拉/射，每发-1。**验收**：生存无箭不能拉弓/射箭；有箭消耗 1/发；创造不消耗。 | — | playercontroller.cpp bow draw（箭检查） |
| t323 | ✅ | **箭碰方块应插入+可拾取**：现箭碰方块消失。**应为**：插入方块持续显示；**玩家箭→走近自动拾取（+1）**；**骷髅箭→插入但不可拾取**（防刷）；插箭有超时清理（~60s）。**验收**：箭命中方块→插命中面；玩家箭拾取；骷髅箭不拾取。 | t304 | Arrow 实体（entitymanager）命中方块状态 |
| t324 | ✅ | **玩家自身箭下落伤害**：朝天射箭落下砸自己无伤。**应为**：玩家射出的箭飞行一段后启用自伤，生存扣血（创造免）。**验收**：生存朝天射箭，落下命中自己→扣伤害。 | t323 | Arrow 实体碰撞（shooter 忽略窗口后启用自伤） |
| t325 | ✅ | **树叶衰减过激**：砍原木后半棵叶子瞬间消失。**应为**：检测整棵树无原木→启动定时器→每 tick 随机概率消失单片（渐进非瞬间，10-30s 内逐片）。**验收**：砍光原木后叶子随机逐片消失有间隔。 | — | world.cpp decayLeavesAround（定时器+随机概率模型） |
| t327 | ✅ | **死亡播报未在聊天栏**：死亡屏有原因但聊天栏没播报。t313 报已实现但用户没见→复查信号路由。**验收**：死亡时聊天栏显"玩家 <死因>"（与死亡屏同文案）。 | t313 复查 | Main.qml onDied（确认 appendChatMessage 生效） |

## B. 资产重做（P2，用户 0-1 分）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t326 | ✅ | **草丛/树苗：半透像素可透视**：现"两片 A4 纸"叠，不透明挡全视野。**应为**像素 alpha 贴图（草叶图案中间镂空）+ 半透 billboard（X 形双面），能看穿。**验收**：草丛半透像素风，站其内视野不被全挡；树苗同（小像素苗）。 | t310 复查 | partialblockgeometry.cpp（TallGrass alpha 材质）+ 草贴图生成 |
| t328 | ✅ | **音效系统全面重做**：环境音/受击音/收集音/敌对专属全听不到或深沉怪异。根因推测：合成器基频低、缺高频泛音、包络慢→沉闷；或音量路由没生效（"一点没听到"=没在播）。重写 build_sounds.py 按真实音色：脚步=宽带噪声 1ms 瞬态；牛叫=200-400Hz 共振峰+颤音；羊叫更高频；猪叫=低吼脉冲；僵尸=多谐波下行呻吟；骷髅=高频噪声咔嗒；蜘蛛=高通嘶嘶；爬行者=fuse 嘶嘶+爆炸；收集/破坏/UI 各异。**验收**：各 mob 可辨认叫声；脚步/破坏/收集清晰不沉闷。 | — | tools/build_sounds.py + audiomanager.* + sounds/*.wav（**程序合成/CC0，非 MC**） |
| t329 | ✅ | **剪刀贴图（铁非木）+ 掉落/手持图标**：剪刀像木剪；掉落物+第一人称手持**空白**（没做）。**验收**：剪刀银铁色；掉落物可见；手持可见。 | t300 复查 | ToolIcon.qml + 第一人称手持模型（Main.qml/playercontroller）+ MaterialIcon |
| t330 | ✅ | **弓第一人称+掉落贴图修正**：手持像两根棍（一粗一细）；弦木色（应白如蛛丝）；弦在弓反侧；弯曲度不够；掉落物也差。重画：木色弓身（明显 C 弯）+ 白弦在凹侧（弓手侧）。**验收**：手持见完整 C 形木弓+白弦凹侧；掉落物同。 | t304 复查 | 弓手持模型（Main.qml 第一人称/ToolIcon）+ 掉落图标 |
| t331 | ✅ | **骷髅弓木色 + 拉弓动画 + 拉弓减速瞄准**：骷髅弓白色（同身体）；拉弓动作抽象；无减速瞄准。**验收**：骷髅持木棕弓；射前拉弓+停顿瞄准；拉弓时移动减速。 | t301 复查 | mobmodel.cpp（弓材质+动画）+ entitymanager.cpp aiArcher（拉弓减速+瞄准延迟） |
| t332 | ✅ | **剑/工具贴图（木柄+金属头）**：剑柄应木制，只刃/头是金属（按 tier）。复查所有工具第一人称+UI。**验收**：木/石/铁/金/钻石剑镐等，柄木、头对应材质色。 | — | ToolIcon.qml + 第一人称手持模型 |

## C. 物理/光照/方块（P3）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t333 | ✅ | **怪物受水流影响（非水上走）**：怪把水当方块走上去不被推。t298 只做了 speedScale，补**流向推力**。**验收**：怪入水浮/减速；水流推其顺流移（逆慢顺快）。 | t298 复查 | entitymanager.cpp tick（mob water physics + 流向推力） |
| t334 | ✅ | **活板门/半砖光照遮挡**：活板门关闭仍透光；半砖应半透光。**验收**：关闭活板门=挡光（实体）；打开=透光；半砖=按遮挡比例减光。 | — | World 光照 flood-fill（活板门/半砖 light-opacity） |
| t335 | ✅ | **活板门开态碰撞（可站边沿）**：开活板门像门——主体挡人，但开态可站其边沿小空间不掉落（用于通道）；关态可踩顶面。**验收**：开活板门上方可站薄边；关态踩顶面。 | — | partialblockgeometry.cpp/碰撞 AABB（活板门开/关碰撞） |
| t336 | ✅ | **木楼梯（新方块）**：配合竖井——走上+按前进攀爬（阶梯式升，不需跳）。**验收**：放木楼梯对它按前逐级上行；可踩。**配方**：木板→楼梯。 | — | BlockRegistry（Stairs）+ 碰撞（阶梯半砖高）+ 贴图 + recipe |

## D. 世界生成/生态（P3/P4）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t337 | ✅ | **群系密度修正**：全图都有树+草（除沙）。**应为**森林=密树多草，草原=少树适量草，沙/海=无草。**验收**：草原开阔少树，森林密集，过渡自然。 | t306 复查 | world.cpp worldgen（biome 树/草密度参数） |
| t338 | ✅ | **沙集中一角→沙滩+海**：沙散落草原/森林。**应为**选地图一角做海（海平面水）+沙滩，其余无散沙。**验收**：一角海+沙滩；内陆无散沙。 | t337 | world.cpp worldgen（海+沙滩 corner） |
| t339 | ✅ | **地下竖直矿井洞修复**：地下很多 1 格竖直柱洞（像矿井）。定位是哪个 worldgen 引入，移除/修正。**验收**：地下无规则竖直 1 格柱洞。 | — | world.cpp carveCaves/worldgen（排查移除） |
| t340 | ✅ | **湖泊形态（表层湖+下空溶洞）**：湖太规则（纯竖直）。**应为**部分湖=表层水+下方中空溶洞，水平也挖掘（不规则）。**验收**：湖形态自然，有水平扩展+下方空洞。 | t309 复查 | world.cpp placeUndergroundWaterPools/湖生成 |
| t341 | ✅ | **洞穴入口概率+山坡半腰+更大洞口；森林起伏**：增地表洞穴暴露概率；洞口置于山坡半腰；洞口更大；森林地形更起伏。**验收**：地表常见洞穴入口（山坡上）可走入。 | t309 | world.cpp carveCaveEntrances + heightAt（森林起伏） |
| t342 | ✅ | **大峡谷地貌（新）**：地表长条裂缝（露天峡谷），内壁露矿石。**验收**：地图有 1+ 大峡谷可见矿层。 | t341 | world.cpp worldgen（canyon carve） |

## E. 新系统（P5，可拆 R18j）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t343 | ✅ | **岩浆流体**：慢流（比水慢）；Y<30 随机岩浆湖（封闭洞）；岩浆桶（铁桶舀/放）；岩浆音效；旁木制品概率着火；Q 键物品丢岩浆→摧毁。**验收**：岩浆慢流；Y<30 有岩浆湖；桶可舀/放；触岩浆着火；丢物摧毁。 | — | World（Lava fluid 仿 Water）+ recipe（lava bucket）+ audio |
| t344 | ✅ | **着火系统**：触岩浆/火→着火扣血；屏底 35% 燃烧覆盖；灭火（随机/时间）；着火死亡→掉熟肉（牛羊猪）；生物也着火。**验收**：踩岩浆着火扣血+屏覆盖；火灭；着火死掉熟肉。 | t343 | entitymanager（burning）+ playercontroller + Main.qml（屏覆盖）+ drops |
| t345 | ✅ | **装甲系统**：皮革/铁/铜/金/钻石 5 套×头/胸/腿/靴；护甲值（hover 显示）+护甲条（心上一排）；耐久；穿脱音；皮革=牛掉。**验收**：5 套可合成/穿戴；护甲值减伤；UI 显示护甲值+条；穿脱音。 | — | recipe.h（Armor ids）+ Game/armor + Main.qml（护甲槽+条）+ hotbar |

## F. 指令/UI（P3/P4）
| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t346 | ✅ | **/give MC 风格参数提示+输出文案**：打 /give 后显参数提示（下一=ID，再=数量）；输出"给予玩家 X ×N"，耐久变体"给予玩家耐久 N 的铁剑 ×1"。**验收**：/give 有参数提示；输出含 ×N + 耐久文案。 | t314 复查 | Main.qml chat（命令提示）+ hotbar.give（输出文案） |
| t347 | ✅ | **聊天指令历史(↑)+/help+可扩展解析器**：↑键调历史；/help 列可用指令；命令解析可扩展（后续指令多）。**验收**：↑循环历史；/help 列表；新增命令易加。 | t346 | Main.qml chat（历史栈+help）+ 命令分发 |
| t348 | ✅ | **方块/物品 ID 对齐 MC 1.0 原版**：为将来材质包加载，ID 尽量对齐 MC 1.0 数值。**验收**：item-ids.md 与 MC 1.0 一致（或映射表）；注意存档向后兼容/迁移。 | t314 | BlockRegistry/recipe.h id 重排 + 迁移 |
| t349 | ✅ | **剪刀耐久条显示**：t315 漏剪刀。**验收**：剪刀受损后耐久条显示（同其他工具）。 | t315 复查 | hotbar.cpp/Main.qml（isTool 含 Shears） |

## 执行备注
- **P0 四项优先**：t317/t319/t320/t321 直接影响可玩性（瞬死/卡死）。建议首轮先跑这 4 个。
- **复查项**：标"复查"的（t292/t297/t298/t301/t304/t309/t310/t313/t314/t315）= R18h 标 ✅ 但用户报仍有问题，子 agent 需先复现再修，勿假设已对。
- **资产任务（B 段）**：一律程序合成或 CC0，子 agent 提示词须重申"禁用 MC 本体资产"。
- **P5 新系统大**：岩浆/装甲各是独立大系统，建议拆 R18j 单独跑，避免一轮过载。
- **爆炸卡顿（t320）** 与 [[chunk-dirty-flag-race]] 相关，可能牵出全局性能问题，子 agent 需带 profiling 思路。

---

# R18j 规划（R18i playtest 反馈 + 流体重做 + 装甲完善 + /kill）

> 来源：用户 R18i 全量 playtest 反馈（2026-08-06）。任务号续 R18i（t349 止）→ t350 起。
> ⚠️ 多项是 R18i「✅ 但实测仍坏」的**复发项**（t317跳/t321攻频/t320爆炸卡/t327死亡聊天/t328音效/t334光照/t335活板门等）——子 agent 须先**复现+找真根因**，勿重贴旧补丁。

## ⚠️ 资产政策（同前）
绝不用 MC 本体音频/贴图/名称（PLAN §9）。音效/贴图程序合成或 CC0。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **P0** | t350 t351 t352 t353 t354 t355 | **水流体重做**/岩浆补全/跳跃卡死/攻频/爆炸卡/橡皮筋 |
| **P1** | t356-t365 | 创造丢弃/引信/死亡聊天/活板门碰撞/光照/半砖放置/怪卡方块/羊色/头盔错字/tooltip |
| **P2** | t366-t371 | 音效真修/草清晰/弓箭nock/工具FP位/骷髅镂空/着火视觉 |
| **P3** | t372-t376 | 沙滩过渡/草原扩大/生物群系分布/湖数量/峡谷水泛滥 |
| **P4** | t377 t378 t379 | 装甲UX+视觉+怪物护甲 / /kill / 树叶调慢 |

## A. P0 关键（流体 + 卡死 + 性能）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t350 | ✅ | **水流体重做（最优先）**：放一桶水→整平面海啸式蔓延、永不下跌（错）。MC 规则：水水平扩散最多 ~7 格，但**每扩一格若下方是空气→先垂直下落成柱**，只有落在实体上方才继续水平扩；水源+流动分层（越远越矮）。还修：竖直水柱间**透明间隙**（透视见空气，应连续）；水**卡顿**（批 tick？）；排查「保存并退出」是否真存（疑只遮罩）。**验收**：单桶水在竖墙凹槽只流一格即下落，不平面泛滥；竖直流连续无间隙；放水不卡。 | — | src/World/world.cpp tickWaterFlow + 水段几何 + WorldStore 存档 |
| t351 | ✅ | **岩浆系统补全**：现半成品——地底不**发光**（应 emissive/光源）、无音效（滚烫沸腾声）、创造背包**无岩浆桶**（只有水桶）、**不能在岩浆里放方块**、**高度全平**（应如水分层越远越矮）、岩浆下方**无橙色雾**（水有蓝雾，岩浆应有橙雾）、**伤害时有时无**。岩浆应**平行水**（t350 修好水后岩浆复用其流动，仅参数：更慢/发光/着火）。**验收**：岩浆发光+有声+创造桶+可放方块+分层流+橙雾+稳定扣血。 | t350 | src/World/world.cpp (Lava) + audiomanager + chunkgeometry + Main.qml(雾) + hotbar(创造桶) |
| t352 | ✅ | **跳跃仍偶发卡死**（t317 复发）：正常能跳 1 格，但**偶尔**只跳半格卡住，须切创造/观察者再切回。t317 只豁免了 ground-jump，isLockedBuried/着陆吸附/嵌入仍有残余路径吃掉跳跃。深挖「偶发」精确条件（落地点 FP？半砖/活板门边缘？knockback 中？）。**验收**：长时间生存不再出现跳不起来。 | t317 复发 | playercontroller.cpp（isLockedBuried/moveAxis/跳跃全审） |
| t353 | ✅ | **怪物攻击频率仍过高**（t321 复发）：骷髅弓箭手+僵尸「飞快」。t321 全局玩家受击节流(0.5s)不够。再调：提高节流/降单次伤害/降骷髅射速。**验收**：被围有合理反应窗不瞬死。 | t321 复发 | entitymanager.cpp（aiHostile/aiArcher + m_playerHitThrottle） |
| t354 | ✅ | **苦力怕爆炸卡顿**（t320 复发）：爆炸瞬间+之后卡。t320 批处理了 worldChanged 仍卡——profiling 定位剩余热点（掉落物数？光照 reflood？粒子？）。**验收**：爆炸 FPS 不暴跌、即时恢复。 | t320 复发 | entitymanager.cpp detonateStalker + Renderer（掉落/光照/粒子） |
| t355 | ✅ | **玩家橡皮筋/瞬移**：生存/创造偶发「已站定却被传送回」如网络延迟。查 logs/voxelsandbox.log 定位（位置回退？存档重载？碰撞纠正？某 tick 重置 m_pos？）。**验收**：不再无故瞬移。 | — | playercontroller.cpp + 查日志 |

## B. P1 玩法/视觉 bug

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t356 | ✅ | **创造模式丢弃物品**：t318 做了归还切换，但创造现在**丢不出**物品栏（应：Q 键/拖出→生成掉落物，同生存）。**验收**：创造 Q/拖出→世界生成掉落物。 | t318 复查 | InventoryOps/Main.qml（创造丢弃路径） |
| t357 | ✅ | **苦力怕引信逻辑**：现无论距离持续蓄力（靠近一点涨一点→爆）。应：进入引爆距离→**蓄力变白**；**远离→解除、恢复普通**（非累加）。**验收**：靠近变白、远离恢复，反复进出可控。 | — | entitymanager.cpp aiStalker（fuse reset on loss-of-target） |
| t358 | ✅ | **死亡聊天播报**（t327 复发）：仍只在死亡屏，聊天栏没有。t327 改了 z/visible 没生效——再查 onDied→appendChatMessage 路由 + chatDisplay 实际可见条件。**验收**：死亡时聊天栏显死因行。 | t327 复发 | Main.qml onDied/chatDisplay |
| t359 | ✅ | **活板门开态碰撞**（t335 复发）：开活板门玩家应能站上去（半木门高）+shift 移动；现**直接穿透**。**验收**：开活板门可站、可 shift 走。 | t335 复发 | partialblockgeometry.cpp/碰撞 AABB |
| t360 | ✅ | **活板门/半砖光照阴影**（t334 复发）：白天地表放关活板门就有阴影（应在下方挡光，自身不应有怪阴影）；**下半砖阴影错**（上半砖 OK）。**验收**：关活板门/下半砖光照同 MC（下方暗、本身无怪阴影）。 | t334 复发 | World 光照 lightOpacity + 几何 |
| t361 | ✅ | **半砖放置**：对**上半砖底面**点击应放**下半砖**（现须点旁边方块底面）。**验收**：点上半砖底面→同格放下半砖。 | — | playercontroller.cpp placeBlock（半砖面判定） |
| t362 | ✅ | **怪物卡方块**：落差时怪一腿卡进后方高块→不动、任宰。修 mob 碰撞/stepping（落差不卡腿）。**验收**：怪自然跨越 1 格落差不卡死。 | — | entitymanager.cpp（mob moveAxis/step） |
| t363 | ✅ | **剪毛羊颜色**：剪后变粉猪色。应肉色（近玩家手肤色）+少许白残毛。**验收**：剪毛羊肉色微白非纯粉。 | t300 复查 | Main.qml（sheep bare 模型色）/mobmodel |
| t364 | ✅ | **头盔 displayName 错字**：所有头盔显示「头盲」/「头芒」（应「头盔」）。皮革/铁/金/铜/钻石头盔全查。**验收**：显示「皮革头盔」等。 | — | ArmorRegistry::displayName（helmet 分支） |
| t365 | ✅ | **物品 tooltip 缺失**：生/熟猪牛肉等无 hover tooltip。补 nameForBlock/displayName 覆盖。**验收**：所有创造物品 hover 有名称。 | — | hotbar.cpp nameForBlock |

## C. P2 资产/模型质量

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t366 | ✅ | **音效真修**（t328 复发，依旧 0 分）：进游戏就有持续**白噪声**（像雨/电视雪花），动物叫「一个都不像」，脚步被噪声毁（比之前更差）。根因深挖：白噪声=某 ambient/loop clip 一直播且全是噪声（合成参数错或路由 leak）；**先定位止住白噪声**，再逐 clip 重做到可辨认。**验收**：无持续白噪；脚步/破坏/收集/各 mob 叫可辨认。 | t328 复发 | tools/build_sounds.py + audiomanager.* + sounds/*.wav |
| t367 | ✅ | **草丛清晰度**：现半透但**模糊费眼**。锐化像素（更高对比 alpha 边）。**验收**：草丛清晰不糊。 | t326 复查 | 草贴图生成/partialblockgeometry |
| t368 | ✅ | **弓拉弓箭可视化**：拉弓时只见弦动**弓上无箭**。应在弓上显示 nocked 箭。**验收**：拉弓时弓上见箭。 | t330 复查 | bow 持手模型（Main.qml）/bow.cpp |
| t369 | ✅ | **工具第一人称位置**：手持工具位置/角度不佳，调各工具 FP 变换。（设置加物品位置调节？后续）**验收**：手持工具观感合理。 | — | Main.qml（手持 Model 变换） |
| t370 | ✅ | **骷髅模型镂空感**：现像白杆无镂空骨骼感。加骨架结构（肋骨/颅骨/细肢透出）。**验收**：远看像骷髅非白块。 | t301 复查 | mobmodel.cpp（Bones 分支） |
| t371 | ✅ | **着火视觉**：现像放大的火把/橙色立方体罩住怪。应**火焰动画贴身**（粒子/flipbook 火焰覆盖体表，非整块橙光）。**验收**：着火见火焰动画贴体。 | t344 复查 | Main.qml（burning 可视）/粒子 |

## D. P3 世界/群系

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t372 | ✅ | **沙滩+群系过渡**：沙滩太完美、与森林**高度差突兀**不衔接。柔化岸线+高度过渡。**验收**：沙滩自然、与邻群系高度顺接。 | t338 复查 | world.cpp（sea/beach + 高度过渡） |
| t373 | ✅ | **草原扩大**：现地图多森林、草原不明显。增大草原占比。**验收**：草原成片可辨。 | t337 复查 | world.cpp（biome 路由占比） |
| t374 | ✅ | **生物群系分布**：牛羊草原多、猪森林多（概率差异）。**验收**：草原多见牛羊、森林多见猪。 | t373 | entitymanager.cpp（spawn biome 权重） |
| t375 | ✅ | **湖泊数量**：t340 后湖太少。增湖密度。**验收**：地图多见湖。 | t340 复查 | world.cpp（lake 概率） |
| t376 | ✅ | **大峡谷水泛滥修复**：峡谷被水洞贯穿→水涌出泛滥。**根因关联 t350**（水流平铺 bug）；修水后应不泛滥；另：峡谷内水极简（高处一格瀑布源下流即可），加更多邻接洞穴。**验收**：峡谷不泛水、有瀑布点缀、邻接洞穴多。 | t350 | world.cpp（carveCanyon + 水避开/fillWater 顺序） |

## E. P4 系统/指令

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t377 | ✅ | **装甲 UX+视觉+怪物护甲**：①手持护甲**右键=穿戴/替换**（槽空→装备；有旧→替换旧的入背包）；②生存背包**Shift+左键护甲=装备到槽**、**Shift+左键已装备=卸入背包**；③**第三人称玩家模型显护甲**（按件/色）；④**怪物（骷髅/僵尸）随机护甲**（~80% 无）。**验收**：右键/Shift 穿脱顺；第三人称见护甲；偶尔遇戴甲怪。 | t345 复查 | armor.cpp + hotbar（穿脱 API）+ Main.qml（玩家护甲模型）+ entitymanager（mob 护甲） |
| t378 | ✅ | **/kill 指令**：/kill=自杀；/kill @e=清地图所有实体（除玩家）；预留 /kill @e[type=xxx]（按类型）。实体需有名字（type 名）。注册入 t347 的 commandRegistry。**验收**：/kill 自杀、@e 清实体。 | t347 | Main.qml（commandRegistry）+ entitymanager（clear/实体命名） |
| t379 | ✅ | **树叶衰减调慢+掉率**：t325 后仍偏快；木棍/树苗掉率偏低。降衰减速度+提高掉率。**验收**：叶子更慢消失；叶掉木棍/树苗更常见。 | t325 复查 | world.cpp tickLeafDecay + 叶掉落表 |

## 执行备注
- **流体是本轮核心**（t350 水 + t351 岩浆）：水流 bug 是 t376 峡谷泛滥、岩浆分层、整体卡顿的共同根因，须**先修水、岩浆复用**。
- **复发项**（t352/t353/t354/t358/t366 等标「复发」）= R18i 标 ✅ 但实测仍坏，子 agent 须复现+找真根因，勿重贴旧补丁。
- **t355 橡皮筋 / t350 存档退出**须查 logs/voxelsandbox.log 定位。
- **资产任务（C 段音效 t366）**：先止白噪声（疑 ambient loop leak），再逐 clip；程序合成/CC0，禁 MC。
- **建议执行序**：P0 → P1 → P2 → P3 → P4；**流体(t350)最优先**，岩浆/峡谷都依赖它。

---

# R18k 规划（稳态轮 + 天气/云 + 床/睡觉 + 视觉细节）

> 来源：用户决策（2026-08-07）——「按你说的来」+ 加**云** + 一些**细节**。任务号续 R18j（t379 止）→ t380 起。
> 原则：**先稳地基（P0 性能/音质/存档），再做天气+云+床（性价比最高体感），最后视觉细节。**

## ⚠️ 资产政策（同前）
绝不用 MC 本体音频/贴图/名称（PLAN §9）。程序合成或 CC0；床色变用纯色不抄 MC。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **P0 稳态** | t380-t383 | 性能profiling/音质真修/存档鲁棒/chunk dirty 根治 |
| **P1 天气+云** | t384-t386 | 云层/天气(雨雪)/雷电 |
| **P2 床+睡** | t387-t388 | 床方块/睡觉机制 |
| **P3 细节** | t389-t391 | 月星天穹/环境粒子/水面视觉 |

## A. P0 稳态（地基先稳）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t380 | ✅ | **性能 profiling + 修复**：实战负载下 profile（苦力怕爆炸/大水流/多 mob/快速飞行），找 top 热点并修，目标稳 60fps。关联 t320/t354 屡次卡顿。**验收**：爆炸/大水后 FPS 不崩、回升快。 | — | Renderer/World/entitymanager（profiling 定位） |
| t381 | ✅ | **音效质量真修**（t366 复查）：止了白噪但叫声「一个都不像」。大幅升级合成器（按真实音色频谱：共振峰兽叫/噪声瞬态脚步）**或**引入 CC0 voxel 风格音效包；验证路由+音量。**验收**：脚步/破坏/收集/各 mob 叫可辨认、不沉闷。 | t366 | tools/build_sounds.py + audiomanager.* |
| t382 | ✅ | **存档鲁棒性**：chunk save/load round-trip 测试；加 world_version + 迁移注册表（为 t348 ID 变更铺路）；核查「保存并退出」真持久化全部状态（方块/背包/护甲/实体/天气）。**验收**：存档 round-trip 无损；版本可迁移。 | — | WorldStore/SQLite + World |
| t383 | ✅ | **chunk dirty 风暴根治**（[[chunk-dirty-flag-race]] 反复复发）：一次性根治 dirty 标记/批合并/邻接失效。**验收**：连续破放/爆炸不触发 dirty 风暴卡顿。 | — | World/ChunkManager |

## B. P1 天气 + 云

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t384 | ✅ | **天空云层**：高空漂移云层（MC 风格扁平云块或云面），缓慢移动，随昼夜变色。**验收**：抬头见自然漂移的云。 | — | Main.qml/Renderer（天空云层） |
| t385 | ✅ | **天气系统（雨/雪）**：天气状态机（晴/雨/雪/雷）+ 随机转换；天空变暗；雨/雪粒子；**按群系**（冷→雪、沙漠→无、其余→雨）；雨灭 mob 火（t344）+ 浇作物。**验收**：随机雨天/雪天，氛围正确、群系正确。 | t384 | World（天气 tick）+ Main.qml（粒子/天暗） |
| t386 | ✅ | **雷电**：雨天随机闪电（闪光+雷声），可点燃木/伤害实体。**验收**：雷雨天气有闪电+雷声、能引燃。 | t385 | World + Main.qml + audiomanager |

## C. P2 床 + 睡觉

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t387 | ✅ | **床方块（含色变）**：可放置床（头+脚双格，或简化单格），羊毛色变（红/蓝/绿等）。配方：木板+羊毛。**验收**：床可放置、可见、有色变。 | — | BlockRegistry + recipe + 贴图 |
| t388 | ✅ | **睡觉机制**：夜晚右键床→跳到清晨 + 设重生点；白天/附近有怪不能睡（提示）；受伤立即醒。**验收**：夜间右键床跳清晨 + 重生点更新；白天/怪近拒睡。 | t387 | playercontroller + PlayerState + Main.qml（睡觉过渡遮罩） |

## D. P3 视觉细节

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t389 | ✅ | **月相+星空+天穹渐变**：夜间月相 + 星点；天穹日出日落颜色渐变（非仅亮度变）。**验收**：夜有月星、日出日落天色红黄渐变。 | t384 | Main.qml/Renderer（天空） |
| t390 | ✅ | **环境粒子**：雨溅（联动 t385）+ 叶飘 + 火把/着火火花。**验收**：各场景有点缀粒子。 | t385 | Main.qml（粒子系统） |
| t391 | ✅ | **水面视觉**：水面轻微波动/透明度（t350 修了功能，这是视觉润色）。**验收**：水面有波动质感、非死板。 | t350 | chunkgeometry 水段 |

## 执行备注
- **先 P0 稳态**：性能/音质/存档是地基，在大系统前先稳（避免重蹈「大系统摞在卡顿/音质差上」覆辙）。
- **联动**：天气(t385) 联动 mob 火(t344)/群系(t337)/作物；月星(t389)/粒子(t390) 联动天气。
- **建议执行序**：P0 → P1 → P2 → P3。

---

# R18l 规划（探索奖励 + 群系扩展 + 生态补全）

> 来源：用户决策（2026-08-07）——「按你推荐的来」（A 地牢 / B 群系 / D 生态）。任务号续 R18k（t391 止）→ t392 起。
> 原则：给生存加「下洞寻宝」目标 + 世界多样 + 生态完整。废弃矿井/要塞/附魔/下界留给后续专项轮。

## ⚠️ 资产政策（同前）
绝不用 MC 本体音频/贴图/名称（PLAN §9）。程序合成或 CC0。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **A 探索** | t392-t393 | 地牢+刷怪笼 / 战利品箱+表 |
| **B 群系** | t394-t397 | 沙漠 / 雪原针叶 / 沼泽 / 花+甘蔗+内容 |
| **D 生态** | t398-t401 | 鸡 / 鱿鱼 / 繁殖 / 钓鱼 |

## A. 探索奖励（先做——最大杠杆）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t392 | ✅ | **地牢 + 刷怪笼**：地下小结构（圆石/石砖/苔石房），中央**刷怪笼**方块（周期性刷 1 敌对 mob，玩家在范围内才刷），含 1 战利品箱；worldgen 地下随机放置（一定密度）。**验收**：地下能找到地牢，刷怪笼持续刷怪、可破坏停止。 | — | world.cpp（地牢 worldgen）+ BlockRegistry(Spawner) + entitymanager（刷怪） |
| t393 | ✅ | **战利品箱 + 战利品表**：随机战利品表（煤/红石/面包/线/铁锭/马鞍/命名牌/附魔书占位），地牢箱 + 渔获共用。**验收**：地牢箱开启获随机战利品；表可复用。 | t392 | loot table + chest populate（hotbar/InventoryOps） |

## B. 群系扩展

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t394 | ✅ | **沙漠群系 + 内容**：沙地表、**仙人掌**（可放/触碰伤害）、枯木、沙岩；天气**不下雨**（联动 t385）。**验收**：沙漠成片、仙人掌可放且扎手、无雨。 | t385 | world.cpp biome + BlockRegistry(Cactus/DeadBush/Sandstone) |
| t395 | ✅ | **雪原/针叶群系 + 内容**：雪层、冰、云杉（变种树）；天气**下雪非雨**。**验收**：雪原成片、雪/冰可踩、下雪。 | t385 | world.cpp biome + BlockRegistry(SnowLayer/Ice/SpruceLog) |
| t396 | ✅ | **沼泽群系 + 内容**：浅水洼、莲花、蘑菇、偏暗色调。**验收**：沼泽可见浅水+莲花+蘑菇。 | — | world.cpp biome + BlockRegistry(LilyPad/Mushroom) |
| t397 | ✅ | **通用群系内容**：花（红/黄等）、甘蔗（水边长高 3 格）。多群系草地生成。**验收**：草地上有花、水边有甘蔗。 | — | BlockRegistry(Flower/Sugarcane) + worldgen scatter |

## D. 生态补全

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t398 | ✅ | **鸡 mob**：被动，掉羽毛 + 生鸡肉；周期下蛋（掉落蛋物品）。**验收**：鸡在草原生成、杀掉羽毛/鸡肉、周期下蛋。 | — | entitymanager(MobChicken) + recipe |
| t399 | ✅ | **鱿鱼 mob**：水中游动，掉墨囊。**验收**：水中见鱿鱼、杀掉墨囊。 | — | entitymanager(MobSquid) |
| t400 | ✅ | **繁殖机制**：同种 2 只喂对应食物 → 生幼崽（牛/羊/猪/鸡）；种群上限防泛滥。**验收**：喂两只同种→生幼崽；有上限。 | t398 | entitymanager(breeding) |
| t401 | ✅ | **钓鱼竿 + 钓鱼**：抛浮标入水 → 等待咬钩 → 拉起（生鱼/垃圾/宝藏，按 t393 战利品表）。**验收**：可抛竿钓鱼、获随机物。 | t393 | playercontroller(fishing) + recipe(FishingRod) |

## 执行备注
- **A 先**（探索目标是最大杠杆）；B 协同（群系多样 + 地牢分布按群系）；D 补全生态。
- **联动**：沙漠(t394)/雪原(t395) 接天气(t385) 不下雨/下雪；钓鱼(t401) 复用战利品表(t393)；鸡(t398) 是繁殖(t400) 前置。
- **资产**：仙人掌/花/鸡/鱿鱼 贴图 + 音效 程序生成或 CC0，禁 MC 本体；mob 命名用通用名（鸡/鱿鱼，非专有）。
- **建议执行序**：A → B → D。

---

# R18m 规划（XP 系统 + 沙/玻璃 + 农业完善 + 圆石变体 + 垂直楼梯 + t401 钓鱼补做）

> 来源：用户 R18l 反馈（2026-08-08）+ t401 钓鱼（R18l 限额 429 失败，破碎半成品已 stash）干净重做。任务号：t401(redo) + t402 起。
> 原则：进度深度(XP) + 视觉/交互修复 + 农业完善 + 方块变体。

## ⚠️ 资产政策（同前）
绝不用 MC 本体音频/贴图/名称（PLAN §9）。程序合成或 CC0。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **A XP 进度** | t402-t403 | 经验球实体+拾取+来源 / 经验条 UI+升级 |
| **B 沙/玻璃** | t404-t405 | 沙贴图改黄 / 沙烧玻璃+玻璃透光 |
| **C 农业/耕地** | t406-t408 | 甘蔗5+耕地湿润 / 胡萝卜马铃薯 / 耕地低+箱上不完整可开 |
| **D 修复/视觉** | t409-t411 | 箱子开合动画 / 不完整方块破坏动画 / 流体交互 |
| **E 方块变体** | t412-t413 | 圆石半砖/台阶/栅栏/压力板 / 垂直木楼梯 |
| **F 补做** | t401 | 钓鱼（R18l 限额失败重做） |

## F. 补做（先做，干净重做）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t401 | ✅ | **钓鱼竿 + 钓鱼（重做）**：R18l 因 429 失败、半成品已 stash。干净重做：钓鱼竿（木棍+线合成）；右键抛浮标入水→等咬钩→拉起，按 t393 战利品表获物（生鱼/垃圾/宝藏）。**验收**：可合成钓鱼竿、抛竿钓鱼、获随机物。 | t393 | playercontroller(fishing) + recipe(FishingRod) |

## A. XP 进度系统

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t402 | ✅ | **经验球实体 + 拾取 + 来源**：经验球为实体，**被玩家磁吸**（近距自动飞向玩家），拾取+经验值；**杀 mob 掉经验球**；**熔炉取出烧成品给经验**（按物品种类，如烧铁锭给得多）。**验收**：杀怪/烧物产经验球，玩家吸经验。 | — | entitymanager(XpOrb) + PlayerState(xp) + smelting |
| t403 | ✅ | **经验条 UI + 升级**：经验条（hotbar 上方）随经验增长，**满→升级**；每级所需经验**递增**（参考 MC 曲线）；显示等级数。升级可后续接附魔台。**验收**：经验条增长、满升级、显等级。 | t402 | PlayerState(level/xp) + Main.qml(经验条+等级) |

## B. 沙 / 玻璃

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t404 | ✅ | **沙子贴图改黄**：现太橙像泥沙→改黄（像真实沙滩沙）。**验收**：沙子明显偏黄非橙。 | — | tools/build_sand.py + atlas |
| t405 | ✅ | **沙→玻璃冶炼 + 玻璃透光**：沙子熔炉烧成玻璃；玻璃**透明**（可见背后物品/方块）——**调研透明渲染**（alpha blend / depthWrite=false / cutout），实现真正透视。**验收**：沙烧玻璃；玻璃能看穿见背后。 | — | recipe(玻璃) + chunkgeometry(玻璃材质) |

## C. 农业 / 耕地

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t406 | ✅ | **甘蔗(max5+邻水) + 耕地湿润(4级)**：甘蔗**最高 5 格**、仅**邻水**处长高；耕地被**附近水**（半径内）湿润，**4 级湿润**，越湿作物长得越快，**颜色深浅**肉眼可辨。**验收**：甘蔗邻水长到 5；耕地近水变深色+作物加速。 | — | world.cpp(甘蔗 tick) + Farmland(湿润) + crop growth |
| t407 | ✅ | **胡萝卜 + 马铃薯**：**僵尸(Shambler) 低概率掉落**（参考 MC ~2.5%/件，查证）；均可种植作物（耕地）。**验收**：杀僵尸偶尔掉胡萝卜/马铃薯；可种植收获。 | t406 | entitymanager(Shambler drop) + recipe/crop(Carrot/Potato) |
| t408 | ✅ | **耕地低于草方块 + 缝隙 + 箱上不完整可开**：耕地渲染**矮于**整块（留一条缝）；箱子顶部只有**不完整方块**（半砖/楼梯/活板门等）时**仍可开启**（非实体方块不算阻挡）。**验收**：耕地有矮缝；箱上有半砖仍能开。 | t406 | partialblockgeometry(Farmland 高度) + chest-open 阻挡判定 |

## D. 修复 / 视觉

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t409 | ✅ | **箱子开合动画**：现生成"盖子"实体（错）。应 MC 式：箱子保持**整方块**，**顶 ~1/4 绕铰链上转**打开（单方块），关时回落。**验收**：开箱见顶 1/4 翻起、非分离实体。 | — | Main.qml/chest model(顶旋转) |
| t410 | ✅ | **不完整方块破坏动画**：半砖/楼梯等破坏时，破裂动画**按其实际形状**（半砖=半高），现显整方块动画。**验收**：破半砖见半高破裂。 | — | Renderer(break overlay 按形状) |
| t411 | ✅ | **流体交互生成**：**流水 + 静岩浆 → 黑曜石**；**流岩浆 + 静水 → 圆石**（非石头）。**验收**：两种交互正确生成黑曜石/圆石。 | — | world.cpp(fluid tick 交互) |

## E. 方块变体

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t412 | ✅ | **圆石变体**：圆石**半砖 / 台阶(楼梯) / 栅栏 / 压力板**。复用现有半砖/楼梯/栅栏/压力板系统换贴图。**验收**：4 种圆石变体可合成/放置。 | — | BlockRegistry + recipe + 复用既有 |
| t413 | ✅ | **垂直木楼梯（爬梯式）**：之前要的竖直爬行楼梯（既有 WoodStairs 是台阶式，不是）。做**竖直**梯：对它按前=逐级上行（竖井用）。**验收**：放垂直梯、对它按前能爬升。 | — | BlockRegistry(Ladder/VertStair) + 爬升逻辑 |

## 执行备注
- **t401 先做**（清干净重做，避免破碎半成品污染）。
- **联动**：t403 XP 条 接 t402 经验球；t407 胡萝卜/马铃薯 接 t406 耕地；t408 耕地矮 接 t406；玻璃(t405)透光需调研渲染。
- **资产**：沙子/胡萝卜/马铃薯/玻璃 贴图程序生成或 CC0，禁 MC；僵尸=Shambler。
- **建议执行序**：t401 → A → B → C → D → E。

---

# R18n 规划（材质包系统 — 方块部分）

> 来源：用户（2026-08-08）提供 MC 资源包（Default HD 128x，放 docs/，**gitignored 本地**），要做材质包加载器，先适配方块。
> ⚠️ **法律红线（强制，子 agent 提示词须重申）**：仅做**加载器功能**；MC 贴图**绝不进 git/qrc/构建产物**（pack 留本地 docs/ 或 resourcepacks/，gitignored）；引擎默认仍**程序生成贴图**；pack 为**可选本地覆盖**（运行期从磁盘读）；仅个人使用，**不得随游戏分发 MC 资产**。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **核心** | t414 | 材质包加载器核心（方块）：config + 扫包 + 瓦片→MC映射 + 运行时覆写 atlas |
| **完善** | t415 | 映射表全 + 设置开关 UI |

## 任务

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t414 | ✅ | **材质包加载器核心（方块）**：新 `ResourcePackManager`——config 读 pack 路径（settings.json `resourcePack` 字段，默认查 `resourcepacks/active/` 或环境变量）；扫 `assets/minecraft/textures/block/*.png`；用「atlas 瓦片→MC 文件名」映射表（源自 build_atlas.py TILES 注释：tile0 grass_top→grass_block_top.png, tile2 dirt→dirt.png, tile3 stone→stone.png, tile5 cobble→cobblestone.png …）把 pack 贴图**缩到 TILE=16** 覆写默认 atlas 对应瓦片；**运行时**合成覆写后的 atlas 并上传为纹理（默认仍 qrc 程序 atlas，仅 pack 启用时本地加载覆盖）。**验收**：启用 pack 后地形方块贴图变 MC 风（grass/dirt/stone/sand/cobble/log/planks/leaves 等）。 | — | 新 ResourcePackManager.{h,cpp} + 找 atlas 纹理上传点（chunkgeometry/Renderer/Main.qml 材质）+ .gitignore 加 `resourcepacks/` |
| t415 | ✅ | **映射表完善 + 设置开关**：补全所有有 MC 对应的 atlas 瓦片映射（grass_top/side、dirt、stone、sand、cobble、log_top/side、planks、leaves、ores 煤/铁/铜/金/钻石、wool、glass、bed_*、sandstone_*、cobblestone-变体、lava、water、farmland、chest_*、crafting_table_*、furnace_* 等）→ MC 文件名；设置 UI 加「启用材质包」开关 + pack 路径输入。**验收**：多数方块正确切换 + UI 可开关 pack。 | t414 | ResourcePackManager 映射表 + 设置 UI（Main.qml）+ settings 持久化 |

## 执行备注
- **法律**：子 agent 提示词须重申「MC 贴图禁入 git/qrc；loader 仅从本地 gitignored 路径读；commit 时绝不 add 贴图文件」。
- **先方块**（用户要求）；物品/实体贴图留后续轮。
- **HD 暂缓**：phase 1 pack 缩到 TILE=16（简单可用）；HD（TILE=128 + 程序贴图重生成高清版）留后续。
- 映射源自 `tools/build_atlas.py` 的 TILES 注释（每瓦片语义→MC 文件名）。pack 路径默认指向用户提供的 `docs/Default HD 128x Demo 1.8.2.2/`（dev 验证用；该目录已 gitignored）。
- **建议执行序**：t414 → t415。

---

# R18o 规划（资源包/农业 bug 修复 + 包扩展：物品图标/生物贴图）

> 来源：用户 R18n playtest 反馈（2026-08-08）。任务号续 R18n（t415 止）→ t416 起。
> ⚠️ 法律红线同前：MC 贴图仅本地 gitignored 加载，绝不进 git/qrc。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **P0 修复** | t416-t419 | 叶子染色 / 睡莲水面 / 甘蔗 worldgen / 文件夹选包根 |
| **P1 包扩展** | t420-t421 | 物品图标从包 / 生物模型贴图 |
| **.deferred** | — | 资产 MC-pack 结构重组 + HD（拆 R18p） |

## A. P0 修复（先做）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t416 | ✅ | **叶子/草贴图绿色染色**：MC 的 oak_leaves / grass_block_top/side / tall_grass 是**灰度 tintable 贴图**（MC 用 foliage 颜色染绿），引擎现直接用 → 灰像苔藓圆石。**修**：loader 覆盖这些瓦片时对灰度贴图**乘以叶绿色**（foliage green，~#5a8a3a）染色后再写入 atlas。**验收**：启用 pack 后叶子是绿的、草顶/侧绿。 | — | resourcepackmanager.cpp（compositeAtlas 染色；tintable 瓦片集：0 grass_top,1 grass_side,9 leaves,28 tall_grass,等） |
| t417 | ✅ | **睡莲水面放置**：现睡莲被放到**水下方**（错）。应浮在**水面**（y 在水位顶面）。修 placement/几何使睡莲在水面。**验收**：睡莲浮水面不下沉。 | — | partialblockgeometry.cpp / world.cpp（lily pad 放置高度） |
| t418 | ✅ | **甘蔗 worldgen 修正**：现固定/偏高。应：自然生成**高度 1-3 为主，5 格罕见**（不是每根都 5）；生成于**沙滩/沙近水**处，**不在森林湖泊**。而且挖掉最下面的一格会全都掉落，修 worldgen 散布 + 高度分布。**验收**：沙滩见 1-3 高甘蔗，森林湖无。 | — | world.cpp（sugarcane scatter + tickSugarcaneGrowth 高度概率） |
| t419 | ✅ | **文件夹选择器接受包根目录**：现要选到最里层 `.../textures/block`。应选**包主目录**（`Default HD 128x.../`）即可，loader 自找 `assets/minecraft/textures/block`。**修**：loader 给定 packPath（任意层级）时，**搜索**其下 `assets/minecraft/textures/block`（先试 `<path>/assets/.../block`，再浅层递归找）。**验收**：选包根目录即生效。 | — | resourcepackmanager.cpp（resolve block dir 搜索） |

## B. P1 包扩展

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t420 | ✅ | **背包物品图标从 pack**：pack 内 `assets/minecraft/textures/item/*.png`（剑/镐/斧/铲/锄/弓/箭/剪刀/桶/...）。建「引擎 item id → pack item 文件名」映射；启用 pack 时物品图标（ToolIcon/MaterialIcon）用 pack 的 item 贴图覆盖（缩到图标尺寸）。**验收**：启用 pack 后背包工具/物品图标变 MC 风。 | t419 | 新 item 映射 + icon source 覆盖（ToolIcon.qml/MaterialIcon.qml/hotbar iconSourceForBlock） |
| t421 | ✅ | **生物模型贴图**：pack 内 `assets/minecraft/textures/entity/<mob>/*.png`（cow/pig/sheep/chicken/带护甲的 zombie/skeleton/spider）。mob 现是纯色 box；加 **UV 贴图**让 mob 用 pack 的 entity 贴图（按部位映射贴图区域）。**验收**：启用 pack 后生物外观像 MC（贴图而非纯色）。注：mob 几何需加 UV（较大）。| t419 | mobmodel.cpp（UV）+ ResourcePack 扩 entity 映射 + Main.qml mob Texture |

## 执行备注
- **资产 MC-pack 结构重组**（把引擎程序美术重组成 `assets/minecraft/textures/{block,item,entity,gui}/` + 让引擎自身美术=一个 MC 材质包 + 背包 GUI 贴图从包挑）= 大重构，**拆 R18p 专项**（牵涉 qrc/路径全改）。
- **HD**（TILE=128 + 程序贴图高清重做，解甘蔗糊）= 也拆 R18p。
- **法律**：所有 pack 扩展仅本地 gitignored 加载，commit 仅代码（映射表=元数据可提交，贴图文件绝不）。
- **建议执行序**：A（t416→t417→t418→t419）→ B（t420→t421）。

---

# R18p 规划（pack/世界 bug 修复 P0 + pack 算法/资源查看器 P1/P2）

> 来源：用户 R18o playtest 反馈（2026-08-08，大批量）。任务号续 R18o（t421 止）→ t422 起。
> ⚠️ 法律红线同前：MC 贴图仅本地 gitignored 加载，绝不进 git/qrc。
> **本轮先做 P0（游戏降级的紧急修复）；P1/P2 拆 R18q**（compact 后做）。

## 优先级总览
| 级 | 任务 | 主题 |
|---|---|---|
| **P0 紧急**（本轮） | t422-t428 | 草侧 tint / 甘蔗 / 图标空白 / 流水性能 / 地牢 / 湖泊 / 床 2 格 |
| **P1 pack 算法**（R18q） | t429-t433 | destroy_stage / bow 拉弓阶段 / 羊毛 16 色+羊随机 / 睡莲白 / crop 阶段核实 |
| **P1 实体/箱子解析**（R18q） | t434-t435 | mob entity UV 正确解析 / chest entity 贴图 |
| **P2 功能**（R18q） | t436 | 资源查看器（JEI 式 3D 预览） |
| **（更早记的 R18q 项）** | — | 资产 MC-pack 结构重组 + HD + GUI 贴图从包 |

## A. P0 紧急修复（本轮先做）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t422 | ⏳ | **草侧 tint 修正（dirt 部分不染绿）**：t416 把整块 grass_side 染绿→侧边 dirt 部分也绿，与下方泥土格格不入。**MC 算法**：grass_side = dirt 底 + `grass_block_side_overlay`（仅绿色 alpha 层）tint 绿。**修**：grass_side 瓦片 = dirt 贴图底 + 染绿的 overlay 层合成（仅 overlay 染绿）；grass_top/leaves/tall_grass 整块染绿保持。**验收**：草侧只顶部绿、dirt 部分是泥土色。 | — | resourcepackmanager.cpp（composite：grass_side 用 overlay 合成；查 MC tint 算法） |
| t423 | ⏳ | **甘蔗修复（不见了 + 必须邻水种）**：t418 改 Sand-only 后甘蔗不生成（太严）。**修**：恢复生成（沙滩/沙近水）+ **种植必须邻水**（放甘蔗时检查相邻格有水，否则拒绝，同 MC）。cascade-drop(t18) 已做保留。**验收**：沙滩见甘蔗；种甘蔗必须旁边有水。 | — | world.cpp（sugarcane 散布恢复）+ playercontroller.cpp（placeBlock 邻水校验） |
| t424 | ⏳ | **创造图标空白修复（fallback 失败）**：t420 pack item override 对未映射/无 pack 贴图的 item 回退程序绘制失败→空白。**修**：itemIconSource 对无 pack 贴图的 item 返回空→ToolIcon/MaterialIcon 正确回退程序 Canvas；且 pack 关时全部回退。**验收**：创造背包所有 item 有图标（pack 开/关都非空白）。 | — | ToolIcon.qml/MaterialIcon.qml/hotbar itemIconSource |
| t425 | ⏳ | **流水/帧数爆炸性能修复**：玩几分钟 FPS 掉到个位数（严重回归）。**profile 定位**：累积成本（流动触发 chunk dirty 风暴？实体(item/xp)累积？per-tick 扫描随时间增长？水重写 t350 回归？）。修最热点。**验收**：长时间游玩 FPS 稳（不单调跌）。 | — | world.cpp（tickWaterFlow/dirty）+ entitymanager/itementitymanager + profiling |
| t426 | ⏳ | **地牢少而大**：现太多太小（几个格）。**修**：降生成频率 + 增大尺寸（5x5~7x7 房间）。**验收**：地牢少见但像样。 | — | world.cpp（placeDungeons 频率/尺寸） |
| t427 | ⏳ | **湖泊减少**：现太多。**修**：降湖生成概率。**验收**：湖适度不密集。 | — | world.cpp（lake 概率） |
| t428 | ⏳ | **床改 2 格（head+foot 横置如门）**：现床是单格（t387 简化）。应 MC 式 2 格（头+脚，横置如门）。**修**：床改多格放置（如门的双格逻辑，水平方向）+ 贴图分头/脚。**验收**：床占 2 格、可见头脚。 | — | blockregistry（bed 双格）+ playercontroller placeBlock（多格如门）+ 贴图 |

## B. P1 pack 算法（拆 R18q）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t429 | ⏳ | **destroy_stage 破坏纹理**：方块破坏 overlay 用 pack 的 `destroy_stage_0..9.png`（10 阶），替程序绘制。 | — | Renderer(break overlay) + 映射 |
| t430 | ⏳ | **bow 拉弓阶段图标**：`bow.png` + `bow_pulling_0/1/2.png`；玩家拉弓时物品栏/手持弓显对应拉弓阶段。 | — | bow 持手/图标 + pulling 映射 |
| t431 | ⏳ | **羊毛 16 色 + 羊随机色（白主导）**：`wool_colored_*.png` 16 色；羊生成随机色（白 ~85% 主导，余色稀有）。 | — | recipe/blockregistry(wool 16 色) + entitymanager(sheep 随机色) |
| t432 | ⏳ | **睡莲白单独处理**：pack lily_pad 偏白→单独处理（tint 或专用贴图）使其不像纯白方块。 | — | resourcepackmanager(lily tile 特殊处理) |
| t433 | ⏳ | **crop 阶段映射核实**：`potatoes_stage_0..` / `wheat_stage_7` 等正确映射到引擎作物阶段。 | — | resourcepackmanager crop 映射核实 |

## C. P1 实体/箱子解析（拆 R18q，需研究 MC UV）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t434 | ⏳ | **mob entity 贴图正确解析**：现 t421 best-effort UV 失败→mob 仍原贴图。MC entity 贴图是**单 PNG → 按 entity UV layout 贴到 3D**（如玩家皮肤）。**研究** MC 各 entity（cow/pig/sheep/chicken/zombie/skeleton/creeper/spider）的 UV 布局，正确解析贴到引擎 mob 几何。**验收**：启用 pack 后生物像 MC（贴图正确）。 | — | mobmodel.cpp（按 MC entity UV）+ ResourcePack entity 映射 |
| t435 | ⏳ | **chest entity 贴图**：`entity/chest/normal.png`（普通）+ `left`/`right`（大箱子左右）。单 PNG→贴到箱子模型（配合 t409 开合）。**验收**：箱子贴图正确。 | — | chest model + ResourcePack chest 映射 |

## D. P2 功能（拆 R18q）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t436 | ⏳ | **资源查看器（JEI 式）**：设置里按钮→打开查看器，预览导入的 **block 3D / item 图标 / entity 3D 模型**（类 JEI），方便查看 pack 加载效果。 | t434,t435 | 新 ResourceViewer QML + 复用 pack 贴图 |

## 执行备注
- **本轮（R18p）只做 P0（t422-t428）**——游戏降级的紧急修复。
- **P1/P2（t429-t436）拆 R18q**——需 compact 后轻装做（mob entity UV 解析 + 资源查看器是大研究/大功能）。
- **法律**：所有 pack 解析仅本地 gitignored，commit 仅代码。
- **建议执行序（R18p）**：t422→t423→t424→t425→t426→t427→t428（t425 流水性能最优先之一）。
