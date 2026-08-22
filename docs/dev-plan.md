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
| t422 | ✅ | **草侧 tint 修正（dirt 部分不染绿）**：t416 把整块 grass_side 染绿→侧边 dirt 部分也绿，与下方泥土格格不入。**MC 算法**：grass_side = dirt 底 + `grass_block_side_overlay`（仅绿色 alpha 层）tint 绿。**修**：grass_side 瓦片 = dirt 贴图底 + 染绿的 overlay 层合成（仅 overlay 染绿）；grass_top/leaves/tall_grass 整块染绿保持。**验收**：草侧只顶部绿、dirt 部分是泥土色。 | — | resourcepackmanager.cpp（composite：grass_side 用 overlay 合成；查 MC tint 算法） |
| t423 | ✅ | **甘蔗修复（不见了 + 必须邻水种）**：t418 改 Sand-only 后甘蔗不生成（太严）。**修**：恢复生成（沙滩/沙近水）+ **种植必须邻水**（放甘蔗时检查相邻格有水，否则拒绝，同 MC）。cascade-drop(t18) 已做保留。**验收**：沙滩见甘蔗；种甘蔗必须旁边有水。 | — | world.cpp（sugarcane 散布恢复）+ playercontroller.cpp（placeBlock 邻水校验） |
| t424 | ✅ | **创造图标空白修复（fallback 失败）**：t420 pack item override 对未映射/无 pack 贴图的 item 回退程序绘制失败→空白。**修**：itemIconSource 对无 pack 贴图的 item 返回空→ToolIcon/MaterialIcon 正确回退程序 Canvas；且 pack 关时全部回退。**验收**：创造背包所有 item 有图标（pack 开/关都非空白）。 | — | ToolIcon.qml/MaterialIcon.qml/hotbar itemIconSource |
| t425 | ✅ | **流水/帧数爆炸性能修复**：玩几分钟 FPS 掉到个位数（严重回归）。**profile 定位**：累积成本（流动触发 chunk dirty 风暴？实体(item/xp)累积？per-tick 扫描随时间增长？水重写 t350 回归？）。修最热点。**验收**：长时间游玩 FPS 稳（不单调跌）。 | — | world.cpp（tickWaterFlow/dirty）+ entitymanager/itementitymanager + profiling |
| t426 | ✅ | **地牢少而大**：现太多太小（几个格）。**修**：降生成频率 + 增大尺寸（5x5~7x7 房间）。**验收**：地牢少见但像样。 | — | world.cpp（placeDungeons 频率/尺寸） |
| t427 | ✅ | **湖泊减少**：现太多。**修**：降湖生成概率。**验收**：湖适度不密集。 | — | world.cpp（lake 概率） |
| t428 | ✅ | **床改 2 格（head+foot 横置如门）**：现床是单格（t387 简化）。应 MC 式 2 格（头+脚，横置如门）。**修**：床改多格放置（如门的双格逻辑，水平方向）+ 贴图分头/脚。**验收**：床占 2 格、可见头脚。 | — | blockregistry（bed 双格）+ playercontroller placeBlock（多格如门）+ 贴图 |

## B. P1 pack 算法（拆 R18q）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t429 | ✅ | **destroy_stage 破坏纹理**：方块破坏 overlay 用 pack 的 `destroy_stage_0..9.png`（10 阶），替程序绘制。 | — | Renderer(break overlay) + 映射 |
| t430 | ✅ | **bow 拉弓阶段图标**：`bow.png` + `bow_pulling_0/1/2.png`；玩家拉弓时物品栏/手持弓显对应拉弓阶段。 | — | bow 持手/图标 + pulling 映射 |
| t431 | ✅ | **羊毛 16 色 + 羊随机色（白主导）**：`wool_colored_*.png` 16 色；羊生成随机色（白 ~85% 主导，余色稀有）。 | — | recipe/blockregistry(wool 16 色) + entitymanager(sheep 随机色) |
| t432 | ✅ | **睡莲白单独处理**：pack lily_pad 偏白→单独处理（tint 或专用贴图）使其不像纯白方块。 | — | resourcepackmanager(lily tile 特殊处理) |
| t433 | ✅ | **crop 阶段映射核实**：`potatoes_stage_0..` / `wheat_stage_7` 等正确映射到引擎作物阶段。 | — | resourcepackmanager crop 映射核实 |

## C. P1 实体/箱子解析（拆 R18q，需研究 MC UV）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t434 | ✅ | **mob entity 贴图正确解析**：现 t421 best-effort UV 失败→mob 仍原贴图。MC entity 贴图是**单 PNG → 按 entity UV layout 贴到 3D**（如玩家皮肤）。**研究** MC 各 entity（cow/pig/sheep/chicken/zombie/skeleton/creeper/spider）的 UV 布局，正确解析贴到引擎 mob 几何。**验收**：启用 pack 后生物像 MC（贴图正确）。 | — | mobmodel.cpp（按 MC entity UV）+ ResourcePack entity 映射 |
| t435 | ✅ | **chest entity 贴图**：`entity/chest/normal.png`（普通）+ `left`/`right`（大箱子左右）。单 PNG→贴到箱子模型（配合 t409 开合）。**验收**：箱子贴图正确。 | — | chest model + ResourcePack chest 映射 |

## D. P2 功能（拆 R18q）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t436 | ✅ | **资源查看器（JEI 式）**：设置里按钮→打开查看器，预览导入的 **block 3D / item 图标 / entity 3D 模型**（类 JEI），方便查看 pack 加载效果。 | t434,t435 | 新 ResourceViewer QML + 复用 pack 贴图 |

## 执行备注
- **本轮（R18p）只做 P0（t422-t428）**——游戏降级的紧急修复。
- **P1/P2（t429-t436）拆 R18q**——需 compact 后轻装做（mob entity UV 解析 + 资源查看器是大研究/大功能）。
- **法律**：所有 pack 解析仅本地 gitignored，commit 仅代码。
- **建议执行序（R18p）**：t422→t423→t424→t425→t426→t427→t428（t425 流水性能最优先之一）。

---

# R18q 规划（综合大批量：R18p 遗留 + 新反馈 ~30 项）

> 来源：用户 R18p playtest 全量反馈（2026-08-08，~38 项）。t429-t436（旧草案）被本表吸收/更新。
> ⚠️ **本轮量大，必须 compact 后执行**。法律红线同前。
> **P0 = 游戏杀手（性能/流体）；先做 P0 再 P1+。**

## A. P0 性能/流体（游戏杀手）

| 任务ID | 状态 | 标题 | 文件 |
|---|---|---|---|
| t437 | ✅ | **内存泄漏/卡顿深度修复**（t425 未根治；3 min→2-3 FPS）：深度 profile——chunk mesh 累积？实体(item/xp)不回收？纹理内存增长？per-tick 扫描残留？**退存档再进仍卡 = 状态/内存未清**。 | world.cpp/entitymanager/Renderer + profiling |
| t438 | ✅ | **水+岩浆交互**（t411 仍坏；水火共融）：流水+静岩浆→黑曜石；流岩浆+静水→**石头**；流岩浆+流水→圆石；桶放岩浆入静水→黑曜石；桶放水入静岩浆→黑曜石。生成后方块留下、两液体继续流至平衡。 | world.cpp(fluid tick 交互) |
| t439 | ✅ | **透明 Z-fighting**（玻璃/水/草 透过看远处会闪/穿透）：深度排序或 blend 修复透明渲染。 | chunkgeometry/Renderer(透明 pass 排序) |

## B. 渲染修复

| t440 | ✅ | **cross-block 手持/掉落黑底**（火把/枯木/花/蘑菇/睡莲/树苗手持+掉落有黑背景）：billboard/cross 几何在手持/掉落路径用正确 flat 渲染，非黑底方块。 | Main.qml(held/drop delegate) + partialblockgeometry |
| t441 | ✅ | **箱子开合动画**（t409/t416 仍坏；右键打开仍见整方块+额外件）：严格做 MC 式——整方块本体、顶 ~1/4（0.3 格）绕后铰链翻起。**反复修不好，这次彻底搞定。** | Main.qml(chest model) |
| t442 | ✅ | **树叶仍怪**（t416/t422 后仍不对）：核实贴图来源（oak_leaves？）+ tint 正确；用户反复报怪。 | resourcepackmanager(leaves tile) + mobmodel |

## C. XP/死亡

| t443 | ✅ | **XP 系统修**：① 观察者/创造模式隐藏 XP 条（仅生存显）；② 杀**被动 mob**(牛/羊/猪/鸡) **也掉 XP**（现仅敌对掉）；③ 死亡→清空 XP 条 + 死亡地点掉部分 XP（≈1 只怪量）。 | PlayerState + entitymanager + Main.qml(XP 条 visible) |

## D. 农业/植物

| t444 | ✅ | **睡莲全套**：① 绿 tint（现灰）；② 掉落物平面非 6 面叠；③ 手持第一人称正确形状非黑底；④ 仅**静止水面**可放（地上/流水不可）；⑤ **可在上面走**（水上行走辅助）；⑥ 不可叠放睡莲。 | partialblockgeometry + playercontroller + resourcepackmanager |
| t445 | ✅ | **仙人掌全套**：① 缩到 ~80% 居中（非满格）；② 挖掉下方沙→仙人掌掉落；③ **全方位**触碰伤害（非仅上方）；④ 放置需水平 4 邻无方块（否则立即破坏掉落）；⑤ Q 丢物落到仙人掌→被顶掉。 | blockregistry + playercontroller + world.cpp |
| t446 | ✅ | **甘蔗不生在湖里**：仅沙滩/沙近水（t423 修过 worldgen 但仍有湖中草长出？核实）。 | world.cpp |
| t447 | ✅ | **作物修**：① 胡萝卜/马铃薯=同物（种子即作物，右键耕地直接种，非"种子+作物"分开）；② 小麦生长**减速**（现秒熟）；③ 挖耕地→作物**掉落**（非消失）；④ 骨头→**骨粉**合成（催熟作物）。 | recipe + playercontroller + world.cpp |
| t448 | ✅ | **锄头耐久**（用一次就消失→修耐久消耗）。 | toolregistry/hoes |

## E. 战斗/生物/弓/装甲

| t449 | ✅ | **mob 死亡动画**：血量归零→**侧倒+白烟**→再掉物（现红闪+物同出太急）。 | entitymanager + Main.qml(mob death anim) |
| t450 | ✅ | **鱿鱼不生成**：核实 spawn 条件（水中？深度？）。 | entitymanager(spawn) |
| t451 | ✅ | **弓拉弓方向**：现弦+箭**往前**走（错）；应**往后拉**（弦拉伸、弓不动、箭随弦后移）+ bow_pulling_0/1/2 阶段贴图。 | bow.h/cpp + Main.qml |
| t452 | ✅ | **装甲**：① F5 第三人称**见护甲**（t377 仍不显示）；② 耐久用**条**非数字（数字仅 tooltip，同工具套路）。 | Main.qml(player armor) + SurvivalInventory |

## F. 物品/UI

| t453 | ✅ | **创造中键复制→空槽优先**：手已有方块+有空槽→新开空槽复制（非替换）；满才替换。 | playercontroller(pickBlock) |
| t454 | ✅ | **沙子 item 图标对齐**（现太橙，放置却黄）+ **枯木高清**（现糊）。 | hotbar(iconSource) + tools/build_dead_bush |
| t455 | ✅ | **羊毛 16 色**：补全 16 色 wool（创造图标不空）+ 床配方=对应色羊毛+木板→对应色床。 | blockregistry/recipe(wool 16 色) + bed recipe |
| t456 | ✅ | **工作台/熔炉 item 图标从包**（现仍旧版）+ **熔炉朝向**（应朝玩家，现固定方向）。 | ToolIcon/MaterialIcon + playercontroller(furnace facing) |
| t457 | ✅ | **床重做**：低 3D 模型（~0.3 格高，四角木柱腿+木板面+羊毛面，上方空气）；**睡觉动画**（躺下→渐黑→中间"起床"按钮→不按则度过夜晚→按则醒）；**非瞬黑瞬醒**。 | blockregistry + Main.qml(bed model+sleep) |
| t458 | ✅ | **资源查看器按钮**（用户找不到）：在设置面板加醒目按钮→打开 JEI 式 3D 预览(block/item/entity)。 | Main.qml(settings+viewer) |
| t459 | ⏳ | **附魔书+命名牌功能**（现占位无效果）：附魔书=附魔台产物（先占位留附魔系统）；命名牌=铁砧改名（先占位）。 | (占位系统) |

## G. 世界/环境

| t460 | ⏳ | **群系+世界**：① 群系过渡平滑+大片（现碎小）；② 沼泽生**藤蔓**；③ 花/蘑菇 **worldgen 散布**（现不生）；④ 云改**方块体素**（现白道）；⑤ 峡谷**不生水**（干裂缝）；⑥ 地表矿洞入口**多些**。 | world.cpp(biome/worldgen) + Main.qml(clouds) |
| t461 | ⏳ | **湖泊仍多**（t427 不够？再减密度）。 | world.cpp(lake 概率) |
| t462 | ⏳ | **岩浆**：① 音效；② 空桶可收岩浆；③ 岩浆贴图从 pack（现非 MC 风）。 | audiomanager + playercontroller(bucket) + resourcepackmanager(lava) |

## H. 指令

| t463 | ⏳ | **/time set** day|night|midnight|\<num\>：改游戏时间。注册入 commandRegistry。 | Main.qml(commandRegistry) + WorldClock |

## 执行备注
- **P0 先行**（t437 性能最优先——退存档再进仍卡 = 有状态/内存不清，非纯 per-tick）。
- **反复修不好的**（箱子动画 t409→t416→仍坏、树叶 tint、性能 t320→t354→t425→仍卡）= 须找**真根因**，子 agent **先复现再修**。
- **法律**：所有 pack 仅本地 gitignored；commit 仅代码。
- **量大（~27 任务）→ 建议分 2 批 workflow**：批 1 = P0(t437-t439) + 高频bug(t440-t453)；批 2 = 内容/世界(t454-t463)。**compact 后执行。**

---

## 自主想法（compact 后追加，非原 R18q 表；用户「做完子agent后自己想几个想法」）

| 任务 | 状态 | 标题 | 提交 |
|---|---|---|---|
| t464 | ✅ | F3 调试叠层增强（entities/time/biome 三块，验证 t437 + PLAN §F） | 5303777 |
| t465 | ✅ | 打击感包（手挥动复用 swingArm / 破块 Model+Timer 粒子池替代 Particles3D / 受击红屏 vignette+震动） | a6171fd |

---

## MC 功能批（用户指定：云杉延伸 / 雪原浆果 / 冰物理 / 船，t466-t469）

| 任务 | 状态 | 标题 | 提交 |
|---|---|---|---|
| t466 | ✅ | 云杉木板+木制品(slab/fence/door)+配方+程序贴图；isDoor() 单一谓词统一门逻辑 | a80b517 |
| t467 | ✅ | 雪原甜浆果丛(3阶段 cross)+浆果食物+雪原 worldgen(SnowLayer 守卫)+采摘/食用/穿越伤害 | 312d5a2 |
| t468 | ✅ | 冰物理(tickIceFreeze+玩家/物品冰上滑+PackIce/BlueIce+isIce()谓词+iceSlipApproach) | 0fb6c93 |
| t469 | ✅ | 船系统(BoatManager+骑乘闭环step顶部拦截+WASD+冰上加速+橡木/云杉船+合成+撞坏掉落) | b763f76 |
| t470 | ✅ | 性能regression修复：render distance culling(默认3chunk/ESC可调1-8)+空段剔除(600→154段,3.9×)；根因 100chunk×6段=600Model全渲无视距 | edbb235 |

---

# R18r 规划（内容扩展：繁殖/伙伴 + 附魔 + 结构）—— 性能修好后执行

> 用户指定（2026-08-10）。⚠️ **前置硬条件**：性能（main bound：水蔓延 wat 57ms/s + mob 碰撞 phys 23ms + QML scene-graph）必须先修好——这批加 mob/worldgen 会加重 main 线程。**性能护栏（mob kCap + AI/phys 节流 + 水 settle 增量化 + render distance + 离 chunk Model 销毁）必须先焊死**，否则每个新版本比上个更卡。
> **护栏状态（2026-08-10 更新）**：mob 碰撞已修（`15f4655`，entitiesChanged 节流 + walkPhase 量化 → 用户实测 mob 22.35→8.80ms）；水/岩浆批量 tick 光照已合并（`d26cef8`，N 次 per-write refloodBox → 联合盒 1 次）。**水+岩浆交互区（wat 157/lav 138/454reb）仍待用户 playing 实测**（light 合并应降 lav 桶；若 reb 不降则属交互区持续写 + 全量段重建，批 2/3 期间再查）。批 2 开工 = 性能护栏已焊死（见 c885785 usage-report 两条 perf 记录）。
> 依赖：附魔(B) 依赖前置材料(A)；繁殖(C) 独立；结构(D) 独立（丛林神殿需丛林群系）。
> 机制等价 MC 1.0；零 MC 专有资产/名词（原创名 + 机制等价描述）；资源包 PNG 仅本地 gitignored。

## A. 附魔前置材料（附魔依赖，必须先做）

| 任务ID | 状态 | 标题 | 验收细节 |
|---|---|---|---|
| t471 | ✅ | **青金石矿 LapisOre + 青金石物品** | 地下生成（Y<32，矿脉散布，类似铁/钻石层）；挖掉落青金石物品（或原矿烧炼得青金石）。青金石=**附魔台每次附魔消耗 1-3 个 + 经验等级**。blockregistry(LapisOre+Lapis 物品) + world.cpp(矿脉 worldgen) + hotbar(创造列出) + tools/build_lapis(贴图) |
| t472 | ✅ | **黑曜石 Obsidian** | 先核实项目是否已有 Obsidian；无则加：**水(源) + 岩浆(源) 接触 → 黑曜石**（流+流→圆石已由 t438 实现，此处补 source+source→obsidian）；挖掘需**钻石镐 + 慢速**（徒手/低级镐不掉落）。blockregistry + world.cpp(流体交互补 obsidian 分支) |
| t473 | ✅ | **皮革+纸+书** | 皮革=牛/猪掉落（加到 mob 掉落表）；**纸=3 甘蔗横排→3 纸**；**书=3 纸 + 1 皮革**（竖排）。书=附魔台/附魔书/书架材料。recipe + mob 掉落表 + hotbar |

## B. 附魔系统（依赖 A）

| 任务ID | 状态 | 标题 | 验收细节 |
|---|---|---|---|
| t474 | ✅ | **附魔台 EnchantingTable** | 附魔台方块（**2 钻石 + 4 黑曜石 + 1 书** 合成）+ 右键开附魔界面（3 附魔选项预览，消耗经验等级 1/2/3 + 对应青金石 1/2/3）+ **周围书架数（≤15，2 格内）提升可选等级上限**。blockregistry + Main.qml(附魔 UI 3 槽) + recipe + PlayerState(经验消耗) |
| t475 | ✅ | **附魔表 + 附魔逻辑** | 附魔属性集（存物品 metadata：enchantId + level）：武器(锋利/亡灵杀手/节肢杀手/击退/火焰附加)、工具(效率/精准采集/时运/耐久)、护甲(保护/火焰保护/摔落保护/弹射物保护/水下呼吸)。附魔台据等级+随机种子选属性+等级写物品 metadata。blockregistry 附魔表数据 + ItemStack 加 enchant 字段 |
| t476 | ✅ | **附魔效果实装** | 锋利(+攻击伤害)、保护(+减伤%)、效率(+挖掘速度)、耐久(减耐久消耗概率)、时运(+矿物掉落数)、精准采集(掉落原方块非矿物) 等实际生效——playercontroller 挖掘/攻击 + entitymanager mob 受击读物品附魔应用。附魔"有用"的关键 |
| t477 | ✅ | **铁砧 Anvil** | 铁砧方块（**3 铁块 + 4 铁锭** 合成，铁块=9 铁锭）+ 右键开铁砧界面：**修复**（两同物品合并耐久，消耗经验）+ **附魔合并**（附魔书→物品，或两物品附魔合并，消耗经验）+ **重命名**（消耗少量经验）。铁砧自身耐久（3 级损坏）。blockregistry + Main.qml(铁砧 UI) + recipe |

## C. 繁殖 + 伙伴动物（独立，性能护栏内）

| 任务ID | 状态 | 标题 | 验收细节 |
|---|---|---|---|
| t478 | ✅(t400) | **动物繁殖** | 牛/羊喂小麦、猪喂胡萝卜/马铃薯、鸡喂种子 → 爱心模式（需成对，半径内找另一只）→ 繁殖产 1 幼崽 + 短冷却（MC 5min，可缩到 1-2min）+ 消耗手持食物 1。entitymanager(love 模式+冷却) + playercontroller(右键喂食判定)。**t400 已实现**（feedMob 食物匹配 + enterLoveMode + tickBreeding 配对产崽 + 冷却 + kPassiveMobCap）；R18r 仅复核。 |
| t479 | ✅ | **幼崽成长** | 幼崽实体（缩放 mob 模型 ~0.5 + 头大身小）+ 喂食加速成长 + 时间到（MC 20min，可缩）变成年。幼崽不繁殖/不掉落。entitymanager(baby 字段) + MobModel(缩放渲染)。t400 已实现大半；**R18r 补两缺口**（`49f6387`）：feedBaby 每喂减 kBabyFeedGrow=12s（≈10%）加速成长 + mobDied 加 wasBaby → Main.qml onMobDied 早退跳过战利品+XP（幼崽不掉落）。 |
| t480 | ✅ | **狼 Wolf（驯服战斗伙伴）** | 森林/针叶林生成（中性）+ **骨头驯服**（右键，概率）→ 坐/站切换 + 攻击主人攻击/受击的 mob + 跟随主人（站时坐守）+ 尾巴角度示血量 + 繁殖（驯服狼+肉）+ 受击红牌。MobWolf=10 + aiWolf(tameWolf 33%/toggleWolfSit/跟随/防御三来源/喂肉繁殖 tamed 门控) + MobModel 犬科几何 + build_mob.py 程序贴图（`ce279a3`）。 |
| t481 | ✅ | **豹猫 Ocelot** | 丛林生成（**丛林群系 R18r 已加** `857343d`，Biome=6 / biomeAt 第 5 独立 fBm 阈值 0.25 / ~13.4% 覆盖 / 高树浓叶 / 确定性）+ 生鱼驯服 → 变猫（3 毛色变体）+ 驱赶苦力怕(Stalker) + 跟随 + 坐/站。MobOcelot=11 + aiOcelot(tameOcelot 33%/3 色/aiStalker nearestOcelot 逃离) + 程序贴图（`5e98481`）。 |
| t482 | ✅ | **雪傀儡 SnowGolem（防御造物）** | **南瓜 + 雪块×2 竖直放置自动生成**（placeBlock 摆放检测 + 静默移除 3 块）+ 抛雪球攻击敌对 mob（Snowball 弹丸 damageEntity 1HP + 3s 减速）+ 行走留雪层（身后 SnowLayer）+ 沙漠/热群系/下雨融化消失（biomeIdAt==Desert OR isPrecipitatingAt → 致死）。MobSnowGolem=12 + 新方块 Pumpkin(100)/Snow(101)（`907a990`）。 |
| t483 | ✅ | **铁傀儡 IronGolem（防御造物）** | **铁块×4（T 形）+ 南瓜 自动生成**（双向检测 + 移除 5 块）+ 大力攻击敌对（damageEntity 8HP + 1.5× 击退）+ 死亡掉铁锭×3-5/罂粟。MobIronGolem=13 + aiIronGolem 追击（`907a990`）。 |

## D. 世界结构（探索+战利品，独立）

| 任务ID | 状态 | 标题 | 验收细节 |
|---|---|---|---|
| t484 | ✅ | **废弃矿井 Mineshaft** | 地下（Y<50）随机生成：木栅栏立柱 + 矿车道（木地板/铁轨，无矿车可简化）+ 蜘蛛网 + 暴露矿石 + 宝藏箱子（矿物/苹果/附魔书/铁锭）。**`2e40396`**：新 Cobweb(102)/Rail(103)（cross/贴地 + 程序贴图 + 6 铁锭→16 铁轨配方）；placeMineshaft（hashColumn 网格 + hashVoxel，Y<48，kMinePct=40 → 默认 seed ~6 座；Planks 地板 + WoodFence 立柱 + Rail + Cobweb + 暴露煤/铁矿 + 末端宝藏箱）；ChestStateMineshaftFlag bit3 → 首开填 mineshaftChestPool（矿物/附魔书/铁锭）。 |
| t485 | ✅ | **沙漠神殿 DesertTemple** | 沙漠群系生成：金字塔外形（沙岩/切制沙岩）+ 地下密室 + 4 宝藏箱（钻石/金/青金石/骨头/腐肉）+ **TNT 陷阱**（踩压力板引爆）。**`8087799`**：新 TntBlock(104)/CutSandstone(105)/火药物品(0x239，Stalker 掉落 + TNT 配方 5 火药+4 沙)；placeDesertTemple（isDesert 守卫 + grid 48 + 45%，阶梯金字塔底 15×15 顶 3×3 + 7×7×4 密室 + 4 箱 ChestStatePyramidFlag + 中央 3×3 TNT 上垫压力板）；scanTntTraps 每 tick 扫玩家 footprint → detonateTntBlock（复用 destroySphereSilent 爆炸，伤害仅 Survival）；pyramidChestPool 权重表。 |
| t486 | ✅ | **丛林神殿 JungleTemple** | **依赖丛林群系**（R18r 已加 `857343d`，Biome=6）：苔石建筑 + 机关（绊线→发射器射箭，无红石用 dispenser 方块直接触发）+ 宝藏箱。**`52afc8c`**：新 MossyCobble(106)/Dispenser(107)（state 编码朝向同熔炉，放置朝玩家）；placeJungleTemple（Jungle 群系 grid 40/pct 50，苔石围墙+地板+天花板+走廊，实测 160×160 产 1 座）；scanDispenserTraps（压力板 4 水平邻 == Dispenser → spawnArrow 水平射箭，per-dispenser 2s 冷却）；ChestStateJungleFlag bit5 → jungleTempleChestPool。 |
| t487 | ✅ | **要塞 Stronghold** | 地下深（Y<30）生成：石砖迷宫 + **末地传送门房**（末地传送门方块 + 12 末影之眼激活 → 末地预热，末地本身可推迟）+ 图书馆（书架，附魔加成）+ 银鱼刷怪笼。**`187498b`**：新 StoneBrick(108)/石砖台阶/EndPortal(110)/EndEye 物品/银鱼 mob(MobSilverfish=14)；placeStronghold（Y<30 确定性，迷宫大厅 + 书架图书馆 + 中央 3×3 末地传送门房 + 银鱼刷怪笼 + 战利品箱）；持末影之眼右键传送门 → state bit0 翻 → end_portal_active 亮绿旋涡视觉 + 日志（末地维度占位）。 |

## 执行备注
- **性能护栏先焊死**（性能 agent 修水蔓延+mob 碰撞后）：加 mob 前 kCap 上限 + AI/phys 节流确认；加结构前 worldgen 不全网格扫；附魔/铁砧 UI 不每帧重算（同 a36b4b0 的 F3 节流模式）。
- **依赖序**：A(t471-t473 矿物材料) → B(t474-t477 附魔)；C(t478-t483 繁殖伙伴) 独立；D(t484-t487 结构) 独立。丛林神殿 t486 + 豹猫 t481 依赖丛林群系（若缺先加或推迟）。
- **量大（17 任务）→ 分批 workflow**：批1 = A+B 附魔链（t471-t477，7 任务）；批2 = C 繁殖伙伴（t478-t483，6 任务）；批3 = D 结构（t484-t487，4 任务）。
- 法律红线：所有结构/方块/生物原创命名或机制等价描述，不抄 MC 资产/专名。
- **性能修好前不开工**（用户睡前 compact，性能 agent 回来修水+mob，确认 FPS 回升后再启批1）。

---

# R18s 规划（综合大批量：性能残留 + 水岩浆动画 + 渲染/方块机制/UI 修复，27 任务 t488-t514）

> 来源：用户 R18r 后大批量 playtest 反馈（2026-08-11，~27 项）。任务号续 R18r（t487 止）→ t488 起。
> **P0 = 性能（游戏杀手，先做）**；B 渲染/视觉；C 方块/机制；D UI/交互。
> ⚠️ 法律红线（强制）：MC 资源包 PNG 仅本地 gitignored 加载（`docs/Default HD 128x Demo 1.8.2.2/`），commit 仅代码 + 程序生成贴图，**绝不 add 包 PNG 进 git/qrc/构建产物**；子 agent 提示词须重申。原创名（Creeper→Stalker 等）。
> **分批 workflow**（用户要求子 agent + workflow 串行开发）：批1 = A 性能（t488-t490，先做）；批2 = B 渲染（t491-t499）；批3 = C 方块机制（t500-t510）；批4 = D UI（t511-t514）。

> **⚠️ R18s 复盘（2026-08-12 用户 playtest 后）**：用户逐项复查，**t500/t502/t503/t506/t507/t512 保持 ✅**（用户确认 OK），**其余 19 任务降为 ⚠️ 待修**（t491-t499、t501、t504-t505、t508-t511、t513-t514）。每任务「复盘补遗」块（批次 B/C/D 表格下方）含用户报的具体 bug 细节。**本批继续开发 = 按 ⚠️ 任务清单逐个修**（复盘状态比原 ✅ 为准，原 ✅ 只是首轮完成不代表用户验收）。

## A. P0 性能（先做，游戏杀手）

| 任务ID | 状态 | 标题（详细） | 依赖 | 文件 |
|---|---|---|---|---|
| t488 | ✅ | **性能残留诊断（/kill @e 后 main 仍 ~52ms）** `42cfb88` | — | profiling + 全栈 |
| t489 | ✅ | **水 + 岩浆流动动画（材质级，替代静态水）** `f298b87` | t488 | resourcepackmanager + chunkgeometry + Main.qml |
| t490 | ✅ | **TNT 连锁爆炸（沙漠神殿 3×3 陷阱只爆 1 个）** `41f795c` | — | entitymanager.cpp（PrimedTnt）+ playercontroller.cpp（点火源）+ Main.qml（白闪） |

**t488 详细**：用户实测——开局 87 FPS 但 `world 140 [wat 29.7 lav 110.7]`（水+岩浆交互区，老流体瓶颈仍在）；TNT 爆炸+/kill @e 清实体后 mobs 1/items 0，但 **main 仍 ~52ms**（sim 5.77 + qmlSync 1.6 = 7.4 → **残留 ~44ms**），且 **mob 桶 5.23ms 给 1 只怪**（疑实体槽高水位不缩，m_entities vector 不 shrink → 每 tick 迭代空槽）。诊断：(a) EntityManager/ItemEntityManager 的 slot 高水位（vector size）在实体爆发（爆炸掉落/箭）后是否永久膨胀 → 每 tick 迭代大量空槽；(b) main_total − sim − qmlSync 的 44ms 残留在哪（Qt 事件循环 / 某每帧 QML 绑定 / 信号扇出 / chunk Model 绑定）；(c) 流体交互区 wat 29.7 + lav 110.7（d26cef8 light 合并后仍高 → 是否 settled=0 持续写 + mesh 重建，或岩浆 tick 本身重）。**用探针/日志定位**（FrameProfiler 加 residual 桶 / entity 槽利用率日志）。验收：定位残留根因 + 修到 /kill @e 后 main <15ms（回近 87 FPS）。
**t489 详细**：现水翻页改静态（b5cc1c6 消 mesh 风暴）→ 用户要流动动画回来。**正确做法 = 材质级动画（不重建 mesh）**：MC 资源包 `lava_flow.png`/`lava_still.png`/`water_flow.png`/`water_still.png` 是 **32×512（= 16 帧 32×32 竖排 flipbook）**。实现动画纹理系统：loader 把 32×512 切成 16 帧 → 运行期按时间选帧（材质 uniform / 纹理数组 / UV 偏移），**不触发 buildMesh**（水段/岩浆段 mesh 用静态 UV，动画由材质参数驱动）。同时恢复水 + 岩浆（静/流）的流动视觉。验收：水面/岩浆面有流动动画；F3 mesh reb 不回升（材质驱动非 mesh 重建）；性能不退化。注：若 QtQuick3D PrincipledMaterial 不支持 per-vertex UV 动画，评估纹理数组 + shader 或 QtQuick3D 的 Texture flipbook。
**t490 详细**：沙漠神殿 3×3 TNT 陷阱踩压力板只爆 1 个（scanTntTraps → detonateTntBlock 单 TNT）。MC 语义：TNT 爆炸应**连锁点燃邻接 TNT**（爆炸范围内 TNT 被引燃 → 延时引爆并且会tnt会变成白色的又变回来这样的动画播放五秒钟左右才会真正引爆然后破坏方块以及又上海 → 链式引爆就是第一个tnt爆炸了之后，会点燃其他的所有的tnt，）。修：detonateTntBlock（或 destroySphereSilent TNT 路径）爆炸时扫球内 TNT 方块 → 引燃（延时 ~mc 燃丝秒数后引爆），链式炸完全部。验收：踩沙漠神殿压力板 → 3×3 TNT 连锁全爆（大坑 + 战利品箱暴露/破坏按 MC）；单放多 TNT 点燃一个也连锁，然后就是点燃的tnt会有沙子一样的掉落效果，并且人可以穿透过去这样，所以可以再一个方块里面塞很多的tnt，并且tnt引燃状态下就不是完整的方块了，如果上方放的是压力板，就会直接掉落，还有就是再tnt方块水平四个面放压力板然后踩踏过去也能激活tntn引燃，还有就是加入一下拉杆和木制按钮石头按钮，也同样可以点燃tnt。

## B. 渲染/视觉修复

| 任务ID | 状态 | 标题（详细） | 文件 |
|---|---|---|---|
| t491 | ⚠️ | **草挖掘粒子（复盘：草方块挖出绿色粒子，应和泥土同色）** `8890d45` | BlockParticles.qml blockColor 扩全枚举（tall_grass=24 白落 default） |
| t492 | ⚠️ | **创造背包工作台/熔炉 3D 方块显示（复盘：还是 2D 图标）** `a381cfa` | build_cube_icons.py render_front 正面 dimetric（顶投影遮炉口/网格） |
| t493 | ⚠️ | **青金石矿贴图背景（复盘：放下 OK=石头色，但背包 Item 图标没改）** `acac3d5` | resourcepackmanager tile 108→lapis_ore.png（pack 激活用包 stone 底） |
| t494 | ⚠️ | **熔炉燃烧发光（复盘：贴图改了但不会发光，需加光源；火灭光消）** `acac3d5` | FurnaceStateLitFlag=0x04 + tile 134 + FurnaceUI 燃烧态驱动 setFurnaceLit |
| t495 | ⚠️ | **浮冰贴图 + 冰水过渡（复盘：冰水透明度突变难受；高温/高亮融化成水）** `acac3d5` | build_ice.py draw_pack_ice 淡蓝白重做（B>G>R 冰蓝调） |
| t496 | ⚠️ | **床（复盘：Item 图标没换仍整方块；放下朝向错：床脚应落地处、床头应朝玩家反向=熔炉开口对玩家；床头床尾拼接错乱；中间空隙要填实）** `ca159b5+38a37dc` | partialblockgeometry ShapeBed 重写（床头/尾板+白枕+4腿+绗缝）+16色 icon |
| t497 | ⚠️ | **物品图标全替换（复盘：图标链接被吞，钻石/金/铁/皮革护甲 item 图标全没换）** `e823288` | resourcepackmanager emptyArmorSlotSource + EndEye 映射 + SurvivalInventory 空槽 pack 图 |
| t498 | ⚠️ | **玩家装甲 F5 第三人称显示（复盘：修 3-4 次仍不显示，mob 却有）** `3faec95` | Main.qml playerModel 护甲凸出量（z scale 被身体内嵌遮挡） |
| t499 | ⚠️ | **雪傀儡模型（复盘：头朝玩家应固定；没南瓜头；炎热/水扣血无变红动画；剪刀剪头未知）** `3faec95` | Main.qml SnowGolem 眼/嘴 z 凸出 + 头放大 + 刻面嘴 + IronGolem 同修 |

**复盘补遗（2026-08-12 用户 playtest，覆盖 t491-t499）**：
- **t491**：挖掘**草方块**（Grass=1）粒子是**绿色**（草叶绿），应和**泥土同色**（草方块挖出的是泥土，破块粒子应为泥土色 #8a6b3a 系）。
- **t492**：创造背包里工作台/熔炉**仍是 2D item 图标**，未用 3D 方块 icon（上轮 render_front 没生效或路径没走通）。
- **t493**：放下青金石矿 OK（石头背景色），但**背包 Item 图标**还是旧的（需换 lapis_ore 对应 item 图标）。
- **t494**：熔炉燃烧正面贴图改了（furnace_front_on），但**不发光**。需：燃烧时熔炉作为光源（方块光 flood，火把/岩浆同源）；熄灭后光消。
- **t495**：冰/浮冰/蓝冰三者和**水放在一起**透明度突变（冰不透/水透的边界跳变难看）。需平滑过渡。且**普通冰在高温/高亮环境（火把/熔炉/火）有概率融化成水**。
- **t496**：**床完全重做**——① Item 图标：dev-plan 没写路径，未换（应换 `textures/item/bed.png` 红床为模板，16 色变体）；② 放下朝向：现在床头床尾错乱（用户实测朝 +X 放时床脚落地处正确但床头指向玩家脚侧；朝 -X 放时床横在两格中间凸起）→ 应**床脚落在放置处、床头朝远离玩家**（同熔炉开口对玩家反向）；③ 床头床尾中间（羊毛处）**空隙要填实**；④ 放下 3D 模型应为完整床体。
- **t497**：`textures/item/` 下 `diamond_helmet.png`+`diamond_chestplate.png`+`diamond_leggings.png`+`diamond_boots.png` 分别=钻石的头/胸/腿/鞋 item 图标，**金/铁/皮革同族**也是xxx_helmet等前缀；当前**全没换**（上轮链接被吞），然后还有生存模式装甲显示的空装甲图标分别是empty_armor_slot_boots.png靴子+empty_armor_slot_leggings.png裤子+empty_armor_slot_chestplate.png胸甲+empty_armor_slot_helmet.png头盔，在QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\item文件夹，你也来修改一下，以及各种工具也是在这个item目录下diamond_axe.png是钻石斧头，diamond_hoe.png是钻石锄头，diamond_pickaxe.png是钻石镐，diamond_shovel.png是钻石铲子，diamond_sword.png是钻石剑，当然还有铁金石头和木头的。
- **t498**：玩家装甲 F5 穿了**仍无变化**（修 3-4 次未好）；mob 装甲（t377）正常 → 玩家模型护甲叠加路径仍有 bug。
- **t499**：雪傀儡——① 生成头朝向玩家（应固定朝向）；② **没有南瓜头**（用户没看到）；③ 炎热/水扣血**无变红动画掉血**；④ 剪刀剪南瓜头功能未知。

**t491**：破块粒子按方块材质取色（草 = 叶绿 #5a8a3a 系），现硬白。**t492**：创造背包里工作台/熔炉当前是 2D item 贴图，应与其它方块一致用 3D 方块 icon（统一 icon 渲染路径）。**t493**：青金石矿放下来背景是旧石头（材质包前的 stone），应映射 pack 的 stone 贴图为矿背景（现矿脉一眼可见 = 不合理）。**t494**：熔炉燃烧时正面用 furnace_front_on（带火），非燃烧用 furnace_front_off。**t495**：浮冰贴图重做（现像白羊毛，应是淡蓝白压实冰）。**t496**：床创造图标按色（bed.png 红床为模板，16 色变体）；放下的 3D 模型用 pack `entity/bed` 的模型组装（现 2 格但丑）。**t497**：批量替换 item 图标（工具+套装 4 材质×5件 + 4 套套装 + 钓鱼竿 + 末影珍珠 + 4 个空盔甲槽图标），全部从 pack `textures/item/`。**t498**：玩家穿装甲 F5 第二/三人称看不见（mob 能显 t377）→ 玩家模型同样叠加护甲 Model。**t499**：雪傀儡当前纯雪块堆叠无南瓜头无眼 → 加南瓜头 Model + 刻面双眼（机制等价 MC 雪傀儡南瓜头）。

## C. 方块/机制修复

| 任务ID | 状态 | 标题（详细） | 文件 |
|---|---|---|---|
| t500 | ✅ | **草方块生存挖掉泥土（精准采集才掉草方块）** `a10a369` | blockregistry Grass dropId Grass→Dirt（silk_touch 附魔 t475 已覆盖掉 Grass） |
| t501 | ⚠️ | **木梯侧边放置（复盘：贴图未换 ladder.png；爬梯时优先挖梯子，应像火把可透视）** `f864312` | blockregistry ladderFaceFromNormal + placeBlock full-cube 侧校验 + partialblockgeometry 单片贴墙 quad + 失撑掉落 |
| t502 | ✅ | **熔炉 UI 布局修复** `4d32637` | FurnaceUI 成品居中 + 进度箭头居间 + burnTotal 燃料进度（火焰收缩+底条） |
| t503 | ✅ | **仙人掌** `e382d41` | worldgen placeDesertFlora 4 邻 isSolid 守卫（dropCactusColumn/checkCactusOnEdit 核实已全） |
| t504 | ⚠️ | **枯死灌木（复盘：挖下方方块应掉木棍非枯木自身；贴图边缘平滑怪）** `4d32637` | world checkDeadBushOnEdit（破下方支撑→正上方枯灌木掉落，同仙人掌模式） |
| t505 | ⚠️ | **雪方块体系（复盘：雪块铲掉应掉 2-3 雪球；雪球 item 创造栏无显示；雪球可丢出砸怪受击红闪+小击退不扣血；右键发射+雪傀儡发射；砸地破碎消失=实体仿箭）** `51a8b04` | ShapeSnowLayer 薄板（state 0-7 = (state+1)/8 高）+ 铲掉雪球 + 4雪球合雪块 + worldgen 3 级随机 + 堆叠 + auto-step 上行 |
| t506 | ✅ | **冰/浮冰/蓝冰** `e382d41` | Ice 破→生 Water（非精准）/ silk 掉 Ice；PackIce/BlueIce silk 掉自身（船冰加速 t508 修 blockBelow off-by-one） |
| t507 | ✅ | **花/蘑菇** `e382d41` | BrownMushroom=115 + checkFlowerMushroomOnEdit 失撑 + placeBlock 草/土预检 + 蘑菇汤(碗+红+白)配方 |
| t508 | ⚠️ | **船（复盘：贴图错误；模型是碗形非 U 形（四面凸中间凹）；创造背包归入材料应放工具）** `27b48ff` | 水面放置+32 深浮力 lerp + U 形船体 + 挖船→掉落 + 冰加速 blockBelow 修复 + 可推动, 坐上船的时候物品栏上方提示按shift下船，并且可以在船上右键另外一艘船来坐上去，还有船在水里的时候中间不要显示水了，是隔绝的，以及就是船从冰上走下水直接沉底了，有问题，还有生存模式下有可能坐船沉底按shift之后还是下不来 |
| t509 | ⚠️ | **铁傀儡建造修复（复盘：仍生成不了）** `fb56fb1` | T 形检测静态复核正确 + 加诊断 qInfo（probe/miss 日志定位静默失效：南瓜放偏/底排不全/overlaps 拒放） |
| t510 | ⚠️ | **雪傀儡机制（复盘：积雪层显示完整方块但身体可穿过=应半格；底下应永远有积雪层铲掉即时再生可刷雪球）** `5380afa` | aiSnowGolem meltAccum 慢扣血（1HP/s，非即死）+ 水扣血 + 死掉 0-15 雪球 + 剪刀剪南瓜→derpy 无头形态 + 行走留 SnowLayer(t482) |

**t500**：草方块生存挖 → 掉泥土（dirt）；精准采集附魔（silk_touch，附魔书/工具）→ 掉草方块。机制等价 MC 1.0。**t501**：木梯当前放方块中间（错）→ 应贴方块侧边（似火把），须完整方块侧支撑（草/门/活版门等不完整方块侧不可放）；贴图面向所贴侧；玩家对有梯侧按空格爬升 / Shift 下降。**t502**：熔炉 UI——加燃料进度条（显示当前燃料剩余可烧数），成品槽移到两左槽（燃料+原材料）的中间下方（现对齐原材料），熔烧进度条位置移到原材料与成品之间（现贴原材料）。**t503**：仙人掌 worldgen 不生于水平 4 邻有实体方块处（否则立即破坏掉落，t445 有放置校验，worldgen 散布要守同样规则）；挖任意仙人掌格 → 其上整柱掉落（dropCactusColumn 已有，核实 worldgen/挖路径）；挖下方沙 → 整柱掉落（checkCactusOnEdit）。**t504**：枯死灌木（DeadBush）挖其下方方块 → 灌木掉落（同草/花的支撑校验）。**t505**：积雪层重做——薄（1/8 格高），可堆叠 8 层（state 0-7 = 高度），玩家可踩（半格平滑上行，8 级）；雪块（Snow）= 实心整块；雪原 worldgen 改：底雪块 + 顶不同高度积雪层（真实积雪）；挖掘：空手不掉，铲掉雪球（SnowLayer 掉 1 雪球/层，Snow 掉 4 雪球），4 雪球合成 1 雪块，积雪层不可合成但创造栏可见。**t506**：冰生存挖 → 生成水方块（如置水源）；浮冰/蓝冰挖 → 不掉（需精准采集）；浮冰贴图修（t495）；冰上船打滑加速核实。**t507**：花/蘑菇挖其下方草/泥土 → 掉落（支撑校验，同甘蔗/仙人掌模式）；加白蘑菇（BrownMushroom）；蘑菇汤 = 蘑菇碗 + 红蘑菇 + 白蘑菇。**t508**：船重做——实体（可被玩家/方块推动）、水面漂浮、玩家骑乘 WASD 开动、冰上打滑且更快；模型修正（完整船体，现左右空）；挖船 → 掉落船物品（现挖不掉，回收修复）。**t509**：铁傀儡 T 形铁块×4 + 南瓜摆放检测（t483 实装但用户造不出）→ 核实检测逻辑（十字 T 形 vs 玩家朝向）修复。**t510**：雪傀儡机制——沙漠/热群系召唤扣血但不即死（~10 HP 慢扣，现召唤即死）、下水扣血、死掉雪球、南瓜头可剪（剪刀 → 南瓜掉落 + 傀儡变无头 derpy 形态带眼不死的 sheared 版）、行走留积雪层（联动 t505）。

**复盘补遗（2026-08-12 用户 playtest，覆盖 t501-t510）**：
- **t501**：① 木梯贴图未换 pack `textures/block/ladder.png`；② 爬梯时挖掘优先选中梯子（挖不了旁边方块）→ 应像火把：**不优先选中梯子、可透视穿过**，只有指针完全对准梯子才选中挖掘。
- **t504**：① 挖掉枯木**下方方块**应掉**木棍**（枯木挖掉后概率掉的木棍），非枯木自身（像草挖下方不掉草物品）；② 枯木贴图**边缘平滑奇怪**（应像素化粗糙）。
- **t505**：① 雪块被铲子挖掉应掉 **2-3 个雪球**（现掉落数不对）snowball.png雪球的贴图文件，在item里面；② **雪球 item** 创造模式物品栏无显示（应属**材料类**）；③ **雪球可丢弃发射**：右键发射（仿箭实体），砸到怪物**不扣血但红色受击动画 + 少量击退**；雪傀儡也可发射雪球攻击敌对（爬行者/骷髅弓手/僵尸/蜘蛛）；砸地面 → **破碎动画消失**。
- **t508**：① 船贴图错误；② 3D 模型应是**碗形**（四面八方都凸起、中间凹下去），现只有船头船尾凸起（U 形=碗形错）；③ 创造背包里船归入**材料** tab，应放**工具** tab。
- **t509**：铁傀儡**仍生成不了**（上轮加诊断日志但用户反馈未解决，需继续查运行时原因）。
- **t510**：① 积雪层显示**完整方块**但身体可穿过（应半格薄层显示，高度 1-8 格预设是对的）；② 雪傀儡**底下应永远有积雪层**，铲掉后没立即生成回来（应即时再生，可无限刷雪球）。

## D. UI/交互

| 任务ID | 状态 | 标题（详细） | 文件 |
|---|---|---|---|
| t511 | ⚠️ | **创造背包分类标签（复盘：点箱子 tab 竟切到生存模式，应保持创造但显示物品/护甲便于穿上）** `94f20ee` | Inventory.qml 6 tabs（方块/工具/材料/护甲/食物/箱子）+ filteredPalette + 去标题/选中/销毁提示 + chest→setMode(Survival) |
| t512 | ✅ | **创造背包 hover 物品 + 按 1-9 快速换组（强制替换）** `5a9e765` | Inventory.qml creativeHoveredItemId + forceReplaceHotbarFromCreative（setStack 覆盖），keyInput 1-9 分流 |
| t513 | ⚠️ | **食物系统修复（复盘：生猪肉/生牛肉/熟肉都吃不了；长按右键一直吃停不下来）** `527db24` | foodHungerAmount 加胡萝卜+3/土豆+1 + foodColor 按食物色屑粒（甜浆果暗红等）+ m_eatCooldown 1s |
| t514 | ⚠️ | **甜浆果丛可种植（复盘：可种但不知会不会长大/长大贴图/摘成熟浆果/生存碰到扣血）** `527db24` | placeBlock SweetBerryId 分支右键 Grass/Dirt→setBlock SweetBerryBush state 0（eventFilter 种植优先于进食） |

**t511**：创造背包加分类标签（参考 MC 1.0 创造模式 tabs：建筑方块/装饰/红石/交通工具/食物/工具/战斗/酿造/材料 等，按本项目已有内容裁剪）；点击 tab 切换分类页；**移除**「创造物品栏」标题、「当前选中：xxx」行、「点击右侧销毁 xxx」文字（用户嫌冗余）；**chest 标签**点击 → 跳转生存背包（可对物品操作含装甲）。**t512**：创造背包中鼠标 hover 一个方块/物品 + 按数字键 1-9 → 取一组该物品**强制替换**到对应 hotbar 槽（不管原槽有无物品）。**t513**：食物——胡萝卜/马铃薯/马铃薯当前不能吃 → 修可吃；吃的时候甜浆果吐橙色方块（现占位）→ 加专门的食物咀嚼/碎屑粒子贴图（从 pack 或程序生成）；进食机制——右键一次启动进食 → 吃完一个 → 短冷却（非按住右键连续吃），手持动画在冷却期显示。**t514**：甜浆果丛（SweetBerryBush）当前只能采摘吃，不能种下 → 右键草地/泥土种植（浆果物品作种子，机制等价 MC 浆果丛种植）。

**复盘补遗（2026-08-12 用户 playtest，覆盖 t511-t514）**：
- **t511**：点**箱子 tab** 竟**直接切到生存模式**（错）→ 应**保持创造模式**，但显示物品/护甲（便于创造直接拿起护甲穿上去）；箱子 tab 是图标类入口，不该切模式。
- **t513**：① 生猪肉/生牛肉/熟肉**都吃不了**（食物列表有但右键无反应）；② **长按右键一直吃停不下来**（应吃完一个 + 短冷却，非按住连吃）。
- **t514**：浆果**可种植了**，但需确认：① 会不会长大（生长阶段）；② 长大后的贴图做了没；③ 右键摘成熟浆果做好没；④ 生存模式碰到浆果丛**扣血**做了没。

## 执行备注
- **P0 性能先做**（t488-t490）：用户痛点是"清实体仍卡 + TNT 只爆 1 + 要水动画"。t488 诊断残留是后续所有判断的基础（若残留是流体/槽位，可能影响批 2-4）。
- **依赖**：t489（水动画）依赖 t488（确认性能预算）；t494 熔炉正面 + t502 熔炉 UI 同属熔炉可合并；t495 浮冰贴图 + t506 冰掉落同属冰系可合并；t499 雪傀儡模型 + t510 雪傀儡机制 + t505 雪体系 联动（雪傀儡留积雪层）；t497 物品图标批量替换独立大任务。
- **量大（27 任务）→ 4 批 workflow**（用户要求子 agent + workflow 串行）：批1 A 性能（t488-t490）/ 批2 B 渲染（t491-t499）/ 批3 C 方块机制（t500-t510）/ 批4 D UI（t511-t514）。每批内共享文件串行、跨批可并行评估。
- **法律**：所有 pack PNG 仅本地 gitignored 加载，commit 仅代码 + 程序贴图 + 映射表（元数据可提交）。子 agent 提示词重申。
- **参考素材路径**（仅读不改/不 add）：`docs/Default HD 128x Demo 1.8.2.2/assets/minecraft/textures/{block,item,entity}/` —— block（furnace_front_on/lava_flow/lava_still/water_flow/water_still/spruce_sapling 等）、item（工具+套装+bed+empty_armor_slot_*）、entity（bed 模型）。

---

## ⚠️⚠️ R18s 复盘二轮（2026-08-12 用户第二轮 playtest，commit e292121 后）

> **背景**：第一轮 19 ⚠️ 已修并提交 `e292121`。用户第二轮 playtest 逐项复查，报 ~30 项新问题（含第一轮修复不达验收的）。**本节是下一轮开发的权威 bug 清单**。用户在 `logs/voxelsandbox.log` 留了 F3 数据（mob 28.7ms/帧 卡顿 + mesh 161ms/323 rebuild）。

### A. 第一轮修复被退回的（用户明说不行）
- **t492 工作台/熔炉图标** ❌ 反向：用户要的是 **pack 2D item 图标**（「放回到 item 不行吗」），我删掉 9/10 映射后变成程序 3D 立方体反而更丑。→ **恢复 blockItemIconMap 的 {9, crafting_table} {10, furnace} 条目**（2D pack 图标）。
- **t493 青金石矿图标** ❌ 反向：用户要 **3D 方块形式**（「里面也是方块的形式」，其它矿石都是 3D 立方体），我加的 {93, lapis_ore.png} 让它变 2D PNG。→ **删掉 {93} 条目**恢复程序 3D 立方体 icon。
- **t499 雪傀儡** ❌ 全没修到：南瓜头仍消失不见、朝向背对玩家（应朝玩家见眼）、剪刀剪不了、左键攻击无受伤变红。→ 模型/朝向/受击/剪刀全要查。
- **t510 积雪层视觉** ❌ 仍显示**完整方块**（碰撞半格对，但 mesh 盖了整块 → 看起来完整）。partialblockgeometry SnowLayer 薄板没生效/没走对。
- **t497 物品图标全替代** ❌ **工具+护甲一个都没换**（床图标经 blockItemIconMap 成功了，工具/护甲走 itemFilenameMap→ToolIcon/MaterialIcon 却全没显示 pack PNG）——「就一个一个替代，不要偷懒」。
  - **二轮深查结论（2026-08-12 23:05 实测）**：代码链 **已正常工作**，非 bug。实证：(a) `itemFilenameMap` 工具段 0x100-0x112 + 护甲段 0x300-0x313 全映射、pack `item/` 目录确含全部 PNG（433 张）；(b) 运行期 `itemIconSource(0x100)` 返回 `file:///.../wooden_pickaxe.png` 且 `fileExists=1`；(c) ToolIcon/MaterialIcon 的 `packImg Image` `onStatusChanged` 实测全部 `status=1 (Ready)`、**无 Error**（wooden/stone/iron_pickaxe、各档锄/斧/铲/剑、diamond_pickaxe、shears/fishing_rod 全 Ready）。→ pack 图标**确在加载并渲染**，packImg.visible=true、canvas 隐藏。**用户报「没换」可能源于：观察的是 pack 关闭态 / 旧 build / 某具体界面（如生存装备槽的空槽占位 `emptyArmorSlotSource` 待查）。保留任务但降级，需用户提供「哪个界面/哪个物品」截图复现，再定位真实不显示处。**
- **t501 木梯贴图** ❌ 仍没采纳 `textures/block/ladder.png`（ladder tile 78 映射在但没生效）。
  - **二轮深查结论（2026-08-13 00:00 实测）**：tile 78→ladder.png 映射在（resourcepackmanager.cpp:333）、pack 确含 `block/ladder.png`、Ladder def tile=78、partialblockgeometry Ladder case 用 `tileIndex(Ladder)=78`。运行期临时调试 `RPDBG tile 78 (ladder) overridden with ladder.png` 实锤 **tile 78 确被 pack ladder.png 覆盖进合成图集**（覆盖 86 瓦片含 78）。→ 木梯贴图**已生效**，用户「没采纳」疑旧 build/观察。同 t497 模式，保留待用户复现。
- **t494/t513 熔炉烧肉** ❌ 生猪排/生牛肉在熔炉里**烧不了**（熟猪排配方断了）→ 检查 smelting recipe。
- **t508 船** ❌ 大问题（见下）。

### B. 第一轮 OK/部分 OK 但用户补充的
- **t491 草方块** ✅ 确认修好。
- **t500 草方块生存挖→泥土** ✅ OK。
- **t504 枯死灌木** ✅ 挖下方消失修好。
- **t496 床** 🟡 80 分：位置/朝向/图标替换 OK；但①16 色图标全是红色床没法区分；②第一人称手持拿的是方块立方体（应床 item）；③睡觉视角错（人直接倒地→镜头落到底→黑屏，应镜头在床头格看床尾）。
- **t498 玩家装甲 F5** 🟡 部分：第三人称能看到穿了；但①胸甲只护胸、手臂无护甲；②颜色粗糙；③**新严重 bug：生存背包左键拖/取护甲会复制一份**（头盔/胸甲/裤子/靴子都复制）；④耐久显示：只在鼠标停在护甲槽附近才显示、进背包就没了、无进度条、没换行（工具槽有耐久进度条，护甲该有）。
- **t501 木梯** 🟡 爬梯 shift 下不去卡住——用户说「好像 MC 就是这么设计的，保留」→ 不改。
- **t511 箱子 tab** ❌ 反向：点了显示全物品（currentTab=5 综合页）用户不要。→ 用户要：标签改成「生存模式背包」，点击**切到生存背包**（物品栏+装备栏），可先在护甲 tab 拿钻石护腿再切过去穿上（**保留 held item 跨切**，旧 onSwitchToSurvivalRequested 清了 heldBlock 要改）。

### C. 第二轮全新问题
- **熔炉全局唯一** ❌ 重大：全世界的熔炉**共享一个界面/物品栏**（打开都是同一内容）；打掉熔炉内部物品不掉落。→ 需**per-block 熔炉物品栏**（BlockRegistry 存 inventory，位置键控）。箱子大概率同样问题 → per-block 箱子物品栏。
- **t495 冰世界生成** ❌ 冰把整柱海水全填成冰直到沙底（应只顶层 1-2 层薄冰，不填到底）。
- **t495 冰水过渡闪烁** ❌ 静止看 OK，**移动/转视角时冰水接触一圈闪烁**（不透明度/渲染次序问题）。
- **t494 熔炉发光** ❌ 仍不发光（晚上开炉看不见亮）——state-aware lightEmission 或 seed 没生效，需查。且用户报「阴影挖这么深才那」疑似光照/天光问题。
- **t508 船** ❌ 大堆：①橡木/云杉船都是橡木色（贴图没区分）；②放水上直接飞到水下；③放地面悬空半格；④坐上去是站着（应坐姿）；⑤F3+B 船没有碰撞箱（不是实体）；⑥陆地下水立马卡住+沉底；⑦放水上整个悬浮在水中；⑧能开出虚空（世界边缘）；⑨模型是方形（造型先不管）。
- **t509 铁傀儡** ❌ 仍造不出。用户描述摆法：最下 1 铁块 + 第二层 3 铁块成 T + 顶放南瓜 → 不生成。用户说存档里造了、看日志。→ 查 logs/voxelsandbox.log 的 probe/placement 行 + 摆法坐标核对（可能 T 形上下反）。
  - **日志实锤（2026-08-12 22:00-22:17 用户存档）**：`pumpkin placed at 152 61 132` → `iron golem probe ... rowX=0 rowZ=0 | -X=0 +X=0 -Z=0 +Z=0`。probe 查的 **crossbar 层 y-2（=59）全是空气** → 用户实际把 3 铁块 T 摆在 **y-1（=60）**、单 stem 铁块在 y-2（=59）→ **代码期望 crossbar 在下（y-2）、stem 在上（y-1），用户摆法相反（crossbar 在上、stem 在下）→ 永不匹配**。同时段 `snow golem built` 多次成功 → placeBlock + probe 流程本身正常，纯 T 形坐标不匹配。**修法：probe 同时接受 crossbar 在 y-1（stem 下）与 y-2（stem 上）两种 T 形**（或按用户实际摆法校正坐标）；MC 标准是 crossbar 在地面，但用户验收需要其摆法能成。
- **t514 甜浆果** ❌ ①生存碰到**不扣血**（t467/t514 说做了但没生效）；②F6 看不出生长；③挖掉掉浆果（MC 挖掉不掉，只有成熟采摘得）。
- **性能卡顿** ❌ F3：fps 39 / mob 28.70ms / mesh 161.30ms（323 rebuild，317 同步）/ mob phys 13.47 + hostile 15.23 → mob tick 吃满帧 + mesh 重建风暴。用户「没怎么破坏方块」就卡。
- **阴影/天光** ❌ 「挖这么深才那」疑似挖到深坑天光照不到 → 需确认是否 bug 还是正常亮度衰减。

---

## ⚠️⚠️ R18s 复盘三轮（2026-08-13 用户第四轮 playtest，commit 9ddf825 后）

> **背景**：三轮修了熔炉崩溃/仙人掌/成就/铁傀儡/木梯/t497 图标/裸语句触碰全局（3e7a498）/成就树/船/生存物品栏分页（dd722d9）。用户第四轮逐项复查 + 新发现。**本节为下一轮权威 bug 清单**。用户明确：先写 devplan（本文件不提交），用户会改/填 PNG 链接，改完再修。

### A. 反复/方向（图标类，用户第三次改口，务必确认后再动）
- **t492 工作台/熔炉图标** ❌❌ **又要 3D**：用户「创造背包工作台熔炉图标还是 2D，必须想办法弄成 3D 的工作图标」。三轮前用户要 pack 2D（「放回到 item 不行吗」）→ 我恢复 pack 2D；现在又要 3D。**最终需求：像草方块那样 3D 方块图标**（用户「你放下来的是可以的，之前炒方块那些你也是有方块数据，也是一样可以弄出来的」）。→ 参考草方块：`iconFileForBlock` 程序 3D 立方体（icon_crafting_table.png/icon_furnace.png 已存在）→ **从 blockItemIconMap 移除 {9,10} 恢复 3D**（回到二轮前？不，二轮前是 3D，用户当时嫌丑要 2D……现在又要 3D）。**方案：用 build_cube_icons.py 重做更精致的 3D 图标**（正面为主投影，现 icon 可能太简单），而非退回旧 3D。
- **t497 工具/护甲 item 图标** ❌❌ **仍是老贴图**：用户「创造模式背包工具/护甲贴图依旧是老贴图，催促多少次了」。三轮已修 MaterialIcon/ToolIcon 裸触碰（57ecb16）理论上 pack 激活后刷新。**用户要发固定链接的 PNG**（见下 D1 接口）。→ 用户将提供 PNG 链接，**devplan 留接口**，用户填后按链接取图换。
- **t511 创造背包「生存物品栏」UI** ❌ 已做（dd722d9）但**布局错位**：①左人物/装备图标比物品栏突左 1 格；②右 2×2 合成**结果槽被挡看不见**；③底部 hotbar 行比主栏 3 行突左 1 格。→ 对齐修复。

### B. 新 bug（本轮全新/回归）
- **木梯放下形状** ❌ 拿手上已换 pack icon（OK），但**放下形状还是旧粗糙形状**（上下部分太宽）。→ partialblockgeometry Ladder 单片贴墙 quad 比例/贴图查（用户「直接替换」）。
- **夜间火把/熔炉不发光** ❌ 白天有阴影时熔炉发光 OK，但**晚上连火把也不发光** → 光照系统夜间方块光失效。查：夜间方块光 flood / 天光乘子是否误压方块光 / chunkgeometry 顶点色。
- **挖掘声音** ✅ 挖草方块/泥土**没声音**（已修 t520）。根因非映射错/文件缺，是 `break_grass.wav` 频谱重心 ~87Hz 几乎纯次低频扬声器难重放；改 CC0 源 `impact_soft_heavy`→`step_grass`（centroid 254Hz 可辨）+ 提峰值 → 重生 break_grass.wav 可闻。
- **路程统计恒 0** ❌ 统计数据「走过路程」一直是 0。→ 三轮埋点 onMove 跳过（无信号调用方）→ 补 playercontroller 位移信号 → progress.onMove。
- **箱子 shift+左键** ✅✅ 已完成（commit 367cc49）箱子界面 shift+左键物品应**放入箱子**（不是放回背包）。优先级。
- **破箱不掉落内容** ❌ 箱子打掉内部物品不掉落。→ 破箱清 ChestStore + 掉内容（仿破熔炉）。
- **功能方块上放方块** ❌ shift+右键蹲下在功能方块（熔炉/箱子/工作台等）上应**放方块**而非开界面。→ shift+右键放置优先于开界面。
- **附魔台/铁砧/发射器 UI 打不开** ❌❌ 三个功能方块界面没做（很久的老问题）。→ 用户要求：像工作台蓝本（上面功能区 + 下面背包 4 行），**先做界面**（功能后补）。工作台=3×3合成+产物+背包；附魔台/铁砧/发射器同理（界面布局，功能后补）。
- **甘蔗悬空** ✅✅ 已完成（见下方 t524） 甘蔗底下方块没了上面的不掉落（中间打掉最上不掉、底下沙子打掉不掉）。→ 回归（仙人掌写过，甘蔗没写？）。查甘蔗支撑判定。
- **积雪层自然生成** ❌ ①雪原地貌应远离海边（现海边有积雪层）；②积雪层底下应先雪块再泥土（泥→雪块→积雪层），现可能泥上直接积雪层（且不生成草方块）。→ worldgen 雪原调整。
- **积雪块不能浮空** ❌ 打掉积雪层下方方块应掉落（保留层数）。→ 加积雪层重力/支撑掉落。
- **进度系统问题** ❌ 进度系统想要原版那种类似于树一样的从根节点出发一直继续后续成就的，可以通过鼠标拖动查看

### C. 雪傀儡/铁傀儡（造型 + 行为）
- **雪傀儡** ❌ ①**仍无南瓜头**（四轮持续）；②**一直固定朝向玩家**（用户要生成时固定朝，平时随机）；③打了一下**浮空**；④F3+B **碰撞箱很小看不到完整**。→ 造型（南瓜头 Model 明明在，为何不显？）+ 朝向（aiSnowGolem 过度 facePlayer）+ 浮空/碰撞箱，受伤没有红色动画。
- **铁傀儡** ❌ 还是**全白**（用户「铁块没看到你修改，白的跟雪块做的」）。三轮加了深灰 #7d848c + 锈斑（72df223），用户仍说白。→ 可能 72df223 没生效/被覆盖，或用户看旧 build。**用户要提供生物贴图链接解析**（见 D2）。
- **生物贴图解析** ❌ 用户问：能否解析 MC 式立方体展开 PNG 贴图到模型。→ 需调研（用户将给链接）。devplan 留接口。

### D. 用户将提供的素材链接（**留接口，用户填**）
- **D1. 工具/护甲 item PNG 链接**：E:\Qt_Project\QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\item文件夹下面的 `diamond_helmet.png`+`diamond_chestplate.png`+`diamond_leggings.png`+`diamond_boots.png` 分别=钻石的头/胸/腿/鞋 item 图标，**金/铁/皮革同族**也是xxx_helmet等前缀；当前**全没换**（上轮链接被吞），然后还有生存模式装甲显示的空装甲图标分别是empty_armor_slot_boots.png靴子+empty_armor_slot_leggings.png裤子+empty_armor_slot_chestplate.png胸甲+empty_armor_slot_helmet.png头盔，以及各种工具也是在这个item目录下diamond_axe.png是钻石斧头，diamond_hoe.png是钻石锄头，diamond_pickaxe.png是钻石镐，diamond_shovel.png是钻石铲子，diamond_sword.png是钻石剑，当然还有铁金石头和木头的（用户填固定链接，替换创造背包工具/护甲老贴图，之前床和木梯我都看到成功读取进来了）
- **D2. 生物（雪傀儡/铁傀儡等）贴图 PNG 链接**：E:\Qt_Project\QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\entity\snow_golem.png这个是雪傀儡的，E:\Qt_Project\QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\entity\iron_golem这个文件夹是铁傀儡的（用户填；需解析立方体展开图到模型），但是说实话不如先做一个生物显示大全可以显示他们的3D贴图就好了，就是生物实体图鉴一样的东西，现在不是做了一个材质包里面设置可以看方块的3D贴图吗，继续在里面更新好了，可以直接弄在一起好了，然后怪物蛋的3D模型展示的时候就是直接显示你拼接好的3D模型吧，看看能不能直接做到显示先吧。

### E. 船（继续上轮）
- shift 下船提示应**~5 秒后自动消失**（现常驻）。
- 船**太轻**（身体撞就明显动），应更重（碰撞体质量）。
- 坐姿动画**没做好**（用户「你做的就是人直接卡在地底」；明确坐姿=腿 90°折，非下沉卡地）。
- 船**内有水**（船凹下去水显示在里面，应没水——船体应不透明阻水视觉）。
- 船**不能方便上陆地**（碰岸边速度>阈值应损坏）。
- 船撞坏掉**木板+木棍**（非船本身）；正常攻击挖船才掉完整船。
- 橡木/云杉船**同模型同色**（需区分贴图）。

### F. 已确认 OK（用户表扬）
- 熔炉发光（白天）✅、积雪层放下半格 ✅、冰水 ✅、床 ✅、工作台/熔炉 pack 图标切换机制 ✅（仅要 3D）。

---

## ✅ R18s 复盘四轮 Workflow 结果（2026-08-13，HEAD ca0802e）

> Workflow `wf_0d288f6c-b0a`（了解×2 + 实现×4 + 验证×1，7 agent 全成，~1h40m）。本轮聚焦 D1+D2+路程统计。

### 已修复（4 commit）
- **D1 工具/护甲 item 图标（t497 三轮真根因）** ✅ `455b812` fix(t497)
  - **真根因（隐藏极深）**：QML `url` 类型属性（Image/Texture.source）在 JS 是 **QUrl 对象**，`.length` 对空/非空 url 都恒 `undefined` → `visible: source.length > 0` 恒 false → packImg 永隐 → 恒显自绘 canvas。前两轮只修了裸语句触碰（AOT），从未碰 visible 判定。
  - 修：`source.length > 0` → `source.toString().length > 0`，全工程 5 文件（ToolIcon/MaterialIcon/SurvivalInventory/AnvilUI + Main.qml）。
  - **同一 bug 还造成铁傀儡全白**（t421 的 16 处 mob 贴图 `tex.source.length > 0 ? tex : null` 恒 false → 永纯色）。
  - U1 探查说「源码+build 都对，用户该是旧 build」——错（只读查结构抓不到运行期 JS 语义）。D1 agent 用 runtime Qt6.11 探针实证才抓到。**教训：`部分工作部分不工作`（床/木梯 OK、工具/护甲不 OK）是 url-guard bug 的诊断信号。**
- **D2a 生物图鉴（首次尝试加载 entity PNG 拼 3D）** ✅ `cbbca33` feat(ui)
  - ResourceBrowser.qml 新增「生物」区块：8 mob（猪/牛/羊/蹒跚者/骸骨/潜行者/蜘蛛/鸡）+ 雪傀儡/铁傀儡，选中 → 右侧 View3D 旋转 MobModel 3D + pack entity 贴图。
  - 生物蛋（0x20F..0x216/0x22C/0x22E）选中 → 自动显对应 mob 3D 模型（mobTypeForEgg 派生绑定）。
- **D2b 雪傀儡/铁傀儡接 pack 实体贴图 + 修铁傀儡全白** ✅ `625561f` feat(golem)
  - MobModel 扩 mobType 12（雪傀儡=柱身两雪块）/13（铁傀儡=躯干+双腿+双长臂）；mobEntityMap 加 {12,"snow_golem.png"}(扁平)+{13,"iron_golem/iron_golem.png"}(子目录)。
  - Main.qml 傀儡 delegate 身体盒改走 MobModel + pack 贴图（T 字 UV 展开）；南瓜头/眼/嘴仍是独立橙色 overlay（t499 需求，不进贴图）。
  - pack 关 → 纯色雪白/铁灰回退。**「铁傀儡全白」已修**（pack iron_golem.png 铁纹才显铁质；纯色铁灰读作白）。
- **B1 路程统计恒 0** ✅ `ca0802e` fix(stats)
  - playerprogress.onMove 早已存在但没接线。playercontroller::reportHorizSpeed（step 各出口唯一位移瓶颈）算 √(dx²+dz²)，delta>0 emit moved(delta) → Main.qml Connections → progress.onMove。

### 验证（voxel-tester-build 全 PASS）
- 构建零警告（仅 windeployqt dxcompiler 系统噪音）；冒烟 `root objects after load: 1`；红线全守（无 MC 专名进 UI / 全部 Model NoLighting / 无 PNG 进 git）。

### 仍待办（dev-plan 其余，下轮）
- A: t492 工作台/熔炉图标要 3D（第三次改口，待确认）；创造背包生存物品栏 UI 错位对齐。
- B: 夜间火把/熔炉不发光（光照系统）；~~挖草/土没声音（已修 t520）~~；破箱不掉内容；箱子 shift+左键放箱子；功能方块 shift+右键放方块；**附魔台/铁砧/发射器 UI 打不开**（AnvilUI/EnchantingTableUI 已存在，是 playercontroller 右键路由没触发 enchantingTableOpened/anvilOpened）；甘蔗悬空；积雪层生成/雪块不浮空；进度树拖动。
- C: 雪傀儡（朝向/浮空/碰撞箱/受伤动画）；铁傀儡游戏内 pack 贴图（图鉴已 OK，游戏内 in-world 因 UnitCube pos-only 仍纯色 —— #195 部分残留，需 CrackBox 几何换）。
- E: 船（shift提示5秒/太轻/坐姿90°/内有水/碰岸坏/掉木板木棍/橡云杉区分）。

---

## ⚠️⚠️ R19 复盘（2026-08-13 用户第 19 轮 playtest，HEAD ca0802e 后）

> **背景**：R18s 四轮 Workflow（455b812/cbbca33/625561f/ca0802e）修了：D1 url-guard、D2a 生物图鉴、D2b 傀儡贴图、路程统计。R19 Workflow（2df04fc/6239a28/7f238df）修了 C3 实体精确 UV、B1 皮革 retint、B6 夜间方块光。
> **用户 2026-08-14 验证确认**（HEAD 4234621）：①皮革护甲棕 ✅ ②铁傀儡/生物贴图像了 ✅ ③夜间火把发光+路程涨 ✅。**这三项关闭。**
> **本轮铁律（用户明确）**：每项打 **t 号**（pending 从 t515 起），子 agent 串行逐项，每项做完 git commit + dev-plan 打 √，修完 code review + 时长/token 统计。**不许遗漏**（全部 t 号即契约）。
> **分轮**：R19.1（本轮）做交互+UI+快修批 t515-t524/t528（11 项）；R19.2 下轮做积雪层 t525-t527 + 雪傀儡 t529 + 船 t530-t536。

### 🐛 第 19 轮 — 修 bug（已完成项 + 待办 t 号清单）

> **已完成（用户 2026-08-14 验证确认）**：
> - B1 工具/金属护甲图标 ✅（R18s `455b812`）+ 皮革棕 ✅（R19 B1 `6239a28`）
> - B2 铁傀儡/所有生物贴图 ✅（C3 重写精确 box-UV `2df04fc`）
> - B3 路程统计 ✅（R18s `ca0802e`）
> - B6 夜间火把/熔炉发光 ✅（R19 B6 `7f238df`，dayMul 移进顶点色天空分量）

**B4 工作台/熔炉图标要 3D** → ✅✅ 已完成（commit ce1f180） — 从 blockItemIconMap 移除 {9,10}，创造背包工作台/熔炉恒走程序生成 3D 立方体图标（icon_crafting_table / icon_furnace，与草方块同路径），pack 启用也不再覆盖。
- 用户（第三次改口，明确坚持 3D）：「创造背包工作台跟熔炉图标还是 2D 的，必须弄成 3D 的工作图标。你放下来的这个它都可以的，草方块那些也有方块数据一样能弄出来」。
- 最终需求：像草方块那样 3D 立方体图标。方案：从 blockItemIconMap 移除 {9,10}（当前让它们走 pack 2D item 图），恢复程序生成 3D 立方体图标（icon_crafting_table / icon_furnace）；或用 build_cube_icons.py 重做更精致的 3D 等距投影。

**B5 木梯放下形状仍粗糙** → ✅✅ t519 已完成（commit fix(geom): ladder texture fill —— 满格贴图修「放下形状上下宽粗糙」）
- 用户：「放下来的形状还是之前的，上下部分非常宽，粗糙，能不能直接替换？」。拿手上 pack icon 已 OK，但放下几何形状没改。
- 查 partialblockgeometry Ladder：单片贴墙 quad 比例（上下应窄、贴墙薄板）。t501 换了贴图但几何形状没改。
- ✅ 根因：单片贴墙 quad 几何本身即 MC 1.0 ladder 正确做法（薄板贴墙 + cutout 梯级，单面贴图双面可见），问题在贴图比例 —— 旧贴图纵轨居中瓦片中央 8/16 宽（x=4/5,10/11）+ 两侧各 4/16 透明留白 → 整张贴图铺满 face 后梯子只显在格中心半宽、两侧大块透明 → 观感「格中央小梯图标、粗糙上下宽」。
- ✅ 修：tools/build_ladder.py 改纵轨贴瓦片两侧（x=2/3,12/13）+ 横梯级满铺轨间 + 4 道梯级等距覆盖全高 → 整张贴图「满格读作一把梯子」，铺满 face 后梯子铺满整格宽（机制等价 MC 1.0 ladder 贴图：轨靠边 + rung 满轨间）。重建 atlas.png + icon_ladder.png。几何不变（已是 MC 1.0 正确），同步 partialblockgeometry/hotbar/CMakeLists/build_atlas/build_cube_icons 注释。

**B7 挖草方块/泥土没声音** → **t520**（R19.1 本轮）✅✅ 已完成（commit 8e58d49）
- 用户：「挖草方块跟泥土没有声音，挖树叶还有橡木原木都有声音」。
- ✅ 根因：映射与文件加载都对（Grass=1/Dirt=2 → GroupGrass → grass_*.wav，init 日志 grass 组 break/mining/step 全 true），但 `break_grass.wav` 频谱重心仅 ~87Hz（实测）——几乎纯次低频、扬声器难重放、人耳近不可闻，故听感「没声音」。源是 CC0 `impact_soft_heavy`（软体重击）经 finalize 峰值归一化后能量全沉到次低频。挖树叶(200Hz)/原木(190Hz)/石头(491Hz)重心在可闻带故正常。
- ✅ 修：`tools/build_sounds.py` 把 `BREAK_CC0["grass"]` 从 `impact_soft_heavy` 改用 `step_grass`（真实草地表面录制、centroid ~254Hz 明显可辨、已作 grass step 用），并 grass break 路径提 target_peak 到 0.95（不再压 energy=0.70，破坏是强反馈事件须响）。重跑 build_sounds.py 重生 break_grass.wav（新 peak 31128 / rms 1128 / centroid 254Hz，与 leaves/wood 同量级可辨）。映射表与 blockregistry materialGroup 不动（本就对）。

**B8 箱子界面 shift+左键应放入箱子** → **t521** ✅✅ 已完成（commit 367cc49）
- 用户：「箱子打开页面 shift+左键某物品，应直接放到箱子里面去，而不是放回背包。箱子界面得这样做（优先级）」。
- 查箱子界面 shift+左键逻辑（InventoryOps.js / ChestUI.qml）：现在放回背包，应判「当前在箱子界面 → shift+左键放入箱子」。

**B9 破箱不掉落内容** → **t522** ✅✅ 已完成（commit f6d544f）
- 用户：「箱子把东西放进去后直接挖掘掉，它居然不会掉落」。
- 查破箱：清 ChestStore + 把内部物品作为掉落实体（仿破熔炉 t177 模式）。当前破箱只移除方块、不 dump 内容。

**B10 功能方块上 shift+右键应放方块** → **t523** ✅✅ 已完成（commit 52e637f）
- 用户：「shift（蹲）+右键就可以正常放置东西在他们身上。比如想在熔炉上面放方块，shift+右键直接放方块，而不是右键打开熔炉界面」。
- 查 playercontroller useBlock 路由：sneak+右键功能方块（熔炉/箱子/工作台/附魔台/铁砧/发射器）时放置优先（右键放选中方块在该功能方块面上），不触发开界面。
- ✅ 根因：`placeBlock()` 入口对命中工作台/熔炉/箱子/附魔台/铁砧 5 类功能方块无条件 `return` 开 UI，在放置路径之前拦截 → sneak 也无法 fall-through 到放置。
- ✅ 修：5 个开 UI 分支前置 `!sneakPlace` 守卫（`sneakPlace = m_keys.value(Qt::Key_Shift)` 原始 Shift 按下态，覆盖生存蹲/创造飞态 shift 下降/创造走所有模式，非 `m_moveState==Crouch`——后者飞态不进蹲→飞态 shift+右键会失效）。sneak 时跳过开 UI → fall-through 到主放置路径（`tx/ty/tz = hitBlock + hitNormal`，即命中面相邻格放选中方块）。仅绕过「容器/UI」类 useBlock；门/活版门/床/机关/浆果丛/末地传送门不绕过（机制等价 MC shift 右键门仍开门、床仍睡——非容器 UI 语义）。发射器本就无右键 UI（仅红石机关 scanDispenserTraps），无需改。空手 sneak+右键 → 下方 `m_selectedBlock==Air` 守卫拦（不放置不挥手）。

**B11 附魔台/铁砧/发射器 UI 打不开** → **t515 / t516 / t517**（R19.1 本轮，工作台蓝本）
- 用户：「附魔台铁砧跟发射器根本打不开这三个的 UI 界面，这做的实在是不行，很久之前的问题。附魔台铁砧跟发射器应该跟前面这几个一样，按工作台为蓝本：底下四行背包 + 上方功能区。先做界面，功能后补」。
- 现状：AnvilUI.qml / EnchantingTableUI.qml 已存在（shell-mode）；DispenserUI.qml 不存在。
- → 见下方「新增功能」段 t515/t516/t517。

**B12 甘蔗悬空（回归）** → **t524** ✅✅ 已完成（commit a9001b8）
- 用户：「3 格高甘蔗中间打掉，最上面那格没掉；底下沙子打掉也不掉。以前应该写好的，仙人掌写好了，甘蔗没写？」。
- 查甘蔗支撑判定：cactus 有 neighbor-support drop，sugarcane 漏。worldgen / blockupdate 支撑链。

**B16 创造背包「生存物品栏」UI 错位** → **t528** ✅✅ 已完成（commit b0019f6） — 纯布局对齐修复（改 anchors 让三处与 3 行背包列对齐）
- 用户精确描述三处错位：
  1. 左上人物/空装备图标比背包物品栏**往左突出 1 格**。
  2. 右侧 2×2 合成**只看到放东西的地方，产物格被挡看不见**。
  3. 底部 hotbar（手持 1-9）比上面 3 行背包**往左突出 1 格**。3 行背包居中正常。
- 查 Inventory.qml / SurvivalInventory.qml tab 6 分页布局对齐。
- ✅ 根因：survivalView（tab 6）上半 Item 用 `width: parent.width`(442) 坐标系，护甲列 `x:0` 贴左、2×2 合成 `x: parent.width-2*slotSize` 贴右（无箭头/结果槽），hotbar 行 `anchors.left: parent.left` 贴左 —— 三处均以 442 宽坐标系布局，而 3 行主栏 `anchors.horizontalCenter` 居中（360 宽在 442 内起 x=41）→ 护甲/hotbar 比 main 突出 41px(≈1 格)、产物槽缺失。
- ✅ 修：上半 Item 改 `width: mainCols*slotSize`(360) + `anchors.horizontalCenter`（与主栏同宽居中），护甲 `x:0` 即落在 main 第 1 列；2×2 合成按生存背包 SurvivalInventory 坐标重排（2×2 x=212 → 箭头 x=296 → 结果槽 x=320 对齐第 9 列），补绘箭头 + 结果槽空框；底部 hotbar `hbBar` 改 `anchors.horizontalCenter`（360 宽居中起 x=41，销毁槽仍 anchors.right 留右）。三处与主栏 9 列严丝合缝。仅动 Inventory.qml。

**B13 积雪层手持/item 图标仍是整块** → **t525**（R19.2 下轮）
- 用户：「积雪层拿在手上第一人称 + 背包 item 图标跟雪块一模一样都是完整方块。理论上是 1/8 雪块拿在手上。得标识一下（现在只能靠悬浮确认）」。放下已 OK（半格）。
- 查 snow layer item icon / 手持渲染：该用薄板图标（1/8 高）区别于雪块。

**B14 积雪层自然生成地貌问题** → **t526**（R19.2 下轮）
- 用户三点：① 雪原应远离海边/沙滩（现海边有一撮积雪层）；② 积雪层底下应先雪块再泥土（泥→雪块→积雪层），现可能泥上直接积雪层且不生成草方块；③ 雪原 = 正常草原底下（泥土，不生成草方块）→ 雪块 → 积雪层。
- 查 worldgen 雪原 biome 雪层放置逻辑。

**B15 积雪块不能浮空（支撑掉落 + 保留层数）** → **t527**（R19.2 下轮）
- 用户：「积雪块不能浮空。打掉它下面方块应有掉落效果。原本多少层掉下来还是多少层（8 层掉下来还是 8 层 ≈ 雪块；1-2 层掉下来保留 1-2 层）。需查 MC：满 8 层打掉是否掉落」。
- 加积雪层重力/支撑掉落 + 掉落实体携带层数 metadata。

### 🆕 第 19 轮 — 新增功能（t515+，用户要「以工作台蓝本」）

**t515. 附魔台 UI（工作台蓝本重做）** — 现 EnchantingTableUI 是 shell-mode（选中槽操作），改成像工作台：上方附魔功能区（占位，功能后补）+ 底部背包 4 行（能放/取背包物品）。先做界面，功能后补。 ✅✅ 已完成（commit f626d9a）
**t516. 铁砧 UI（工作台蓝本重做）** — 现 AnvilUI 是 shell-mode，改工作台蓝本：上方修复/合并/重命名功能区 + 底部背包 4 行。先界面，功能后补。 ✅✅ 已完成（commit 6dbeaa5）
**t517. 发射器 UI（新建，工作台蓝本）** — DispenserUI.qml 不存在，新建：上方 9 格发射器物品栏 + 底部背包 4 行（像工作台/熔炉的容器+背包布局）。先界面，功能后补。 ✅✅ 已完成（commit 836b8bf）

### 🐉 第 19 轮 — 雪傀儡/铁傀儡（造型 + 行为，承接 C 段）

**C1. 雪傀儡造型/行为问题** → **t529** ✅✅ 已完成（commit 916312e；详见下方 t529 条目 5 子项）
- 1. **仍无南瓜头** ✅ —— 南瓜头 local y 提到 +1.45 + 放大（详见下方）。
- 2. **一直固定朝向玩家** ✅ —— 改生成时固定朝玩家 + 平时 aiWander 随机（详见下方）。
- 3. **打一下浮空** —— 旧观察（受击 y 不归位）；t529 未单独复现，疑与积雪层重叠相关（t529 ③改身后铺雪后底面贴 cell 底不再共面打架）。
- 4. **F3+B 碰撞箱很小** ✅ —— 白框线融白身 → 改青色框线（halfH=0.90 → 1.8 格高已正确；详见下方）。

**C2. 铁傀儡** ✅✅ 已完成（同 B2，C3 修复 `2df04fc`，用户确认）

**C3. 生物贴图精确 UV 解析** ✅✅ 已完成（`2df04fc`，重写 MC box-UV，用户确认「生物贴图像了」）

### ⛵ 第 19 轮 — 船（承接 E 段，继续修）→ **t530-t536（R19.2 下轮）**

**t530. 下船 shift 提示应 ~5 秒自动消失**（现常驻）。
**t531. 船太轻**（身体撞就明显动，应更重，碰撞体质量）。
**t532. 坐姿动画没做** —— 用户明确：「坐姿 = 腿与身体 90°（shift 更厉害，钝角变直角）。现在是人卡地底」。查 boat sit pose（腿 90° 折，非下沉卡地）。
**t533. 船内有水**（船凹下去水显示在里面，应没水 —— 船体应不透明阻水视觉）。
**t534. 船不能方便上陆地**（碰岸边速度>阈值应损坏）。
**t535. 船撞坏掉木板+木棍**（非船本身；正常攻击挖船才掉完整船）。
**t536. 橡木/云杉船同模型同色**（需区分贴图）。

### 📎 第 19 轮 — 素材链接接口（用户填）

**D1. 工具/护甲 item PNG 链接（B1 用）：** ____________
（路径：`E:\Qt_Project\QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\item\`，文件如 diamond_helmet.png / wooden_pickaxe.png 等，含铁/金/石/木/皮革同族 + empty_armor_slot_*.png 空护甲槽）

**D2. 生物贴图 PNG 链接（C3 调研用）：** ____________
（路径：`E:\Qt_Project\QtMinecraft\docs\Default HD 128x Demo 1.8.2.2\assets\minecraft\textures\entity\`，含 snow_golem.png / iron_golem/ / pig/ cow/ sheep/ zombie/ skeleton/ creeper/ spider/ chicken/ 等子目录。需解析立方体展开图 → 精确 UV）

### ✅ 第 19 轮 — 已确认 OK（用户表扬）
- 熔炉白天发光 ✅（用户「这点我非常喜欢」）
- 积雪层放下半格 ✅
- 冰水 ✅
- 床 ✅

---

## ✅ R19 Workflow 结果（2026-08-13，HEAD 7f238df）

> Workflow `wf_e9b5c40a-11a`（了解×2 + 实现×3 + 验证×1，6 agent 全成，~1h31m）。聚焦 C3 实体 UV + B1 皮革 + B6 夜间光。

### 已修复（3 commit）
- **C3 重写实体贴图精确 UV** ✅ `2df04fc` feat(mob)
  - **真修法**：MobModel 的 `mobFaceQtUV()` 用 MC 标准 ModelRenderer.addBox 6 面 box-UV 公式（Top/Left/Front/Right/Bottom/Back 像素矩形）+ 水平面 180° 轴向 remap（`kMcFace={1,0,2,3,5,4}`，像素实测 creeper 论证）+ v 翻（`mcToQtV=1-py/texH`）。每 mob 设 `g_texW/H`（pig 64×32 / zombie-sheep 64×64... / iron_golem 128×128）。
  - **数据源（非编造）**：U1 agent WebSearch 查到 MinecraftConsoles（MC Java 1.8 近逐行 C++ 移植）的 ModelPart.addBox 原始值 + MC wiki，交叉确认。10 mob（pig/cow/sheep/shambler/bones/stalker/spider/chicken/snow_golem/iron_golem）全用真实 MC box textureOffset + size。
  - **视觉「像不像」需人工验证**（GUI 无法自检）。修前是游标格子（全错位），修后按 MC 真实布局。
- **B1 皮革护甲 retint 棕** ✅ `6239a28` fix(rp)
  - 复用床 retint 机制：`retintLeatherTemplate` 用 Rec.601 luma 映射到皮革棕三锚点（#5e3d1c/#8a5a2b/#a87340，同 MaterialIcon drawArmor 皮革配色）。皮革 4 件（0x300-0x303）命中 retint，非皮革 tier 原样。
  - 落盘实证：AppLocalData 有 `voxelsandbox_rp_leather_768/769/770/771.png`，平均 RGB≈(128-141,85-94,42-48) = 棕色（R>G>B，非白底）。
- **B6 夜间方块光失效（光照系统）** ✅ `7f238df` fix(light)
  - **根因**：QML `baseColor = terrainLight(skyLight)` 作为**全局**材质乘数，因 `vertexColorsEnabled:true` → final = baseColor × vertexColor × tex，baseColor 把方块光通道也压了。午夜 skyLight=0 → baseColor=0.4 → 火把(block 0.93) 被压到 0.37（违反 PLAN §H 方块光时间不变）。
  - **修法**：dayMul 从 QML baseColor 移进 C++ 顶点色烘焙的**天空分量**：`vc = max(sky*(1-shadow)*dayMul, block)`（5 处烘焙点：cross/partial/water-变面/greedy/cube）。block 不被 dayMul 压。地形材质 baseColor → 白。配套 sunRebuildDue 量化阈值（dayMul Δ≥0.03 才重建）防光照风暴。
  - **夜间发光需人工验证**（GUI 无法自检夜间）。

### 验证（voxel-tester-build 全 PASS 6/6）
构建零警告（强删 obj 重编 4 文件 exit 0）· 冒烟 `root objects after load: 1` · 红线全守（Shambler/Bones/Stalker 区隔名 · NoLighting · 无 PNG 进 git）。C3/B1/B6 三项复查均 PASS + 运行期落盘实证。

### R19 仍待办（dev-plan R19 段其余，下轮）
- B4 工作台/熔炉图标 3D（第三次改口）；B5 木梯放下几何（已修 t501）；~~B7 挖草/土没声音（已修 t520）~~；B8 箱子 shift+左键放箱子；B9 破箱掉内容；B10 功能方块 shift+右键放方块；B11 附魔台/铁砧/发射器 UI（→ t515-t517 新功能）；B12 甘蔗悬空；B13 积雪层手持图标；B14 积雪层生成地貌；B15 积雪块不浮空；B16 创造生存物品栏 UI 错位。
- C1 雪傀儡（南瓜头/朝向/浮空/碰撞箱）；C2 铁傀儡游戏内（图鉴 C3 已修，in-world 需 CrackBox 几何换）。
- E1-E7 船全套。
- t515-t517 附魔台/铁砧/发射器 UI 工作台蓝本重做。

---

## ⚠️⚠️ R19.2 复盘（2026-08-14 用户验证 R19.1 后，HEAD 0649274）

> **背景**：用户验证 R19.1（t515-t524+t528）→ 部分真没修好（甘蔗只修一半/生存物品栏图标没碰/雪傀儡+积雪层+船根本没做）+ 大量新 bug。**用户铁律：本轮全部修完，不准再拖下轮，一次 workflow 全做完。**
> **已完成确认**（用户表扬）：挖草/土有声 ✅（t520）、箱子 shift+左键放入 ✅（t521）、破箱掉内容 ✅（t522）、功能方块 shift+右键放方块 ✅（t523）、生存物品栏位置对齐 ✅（t528 位置对）、发射器 9 格 UI 布局对 ✅（t517 布局）。

### 🔄 R19.1 回退/重做
**t537. ✅✅ 已完成（commit d95c044） 工作台/熔炉图标换回 2D pack**（用户「3D 做的是一坨，换回 2D，后面我给 PNG 直接替代」）
- 撤销 t518（ce1f180）：blockItemIconMap 加回 {9,10} → crafting_table.png/furnace.png（pack 2D item 图）。等用户后续给 PNG。
- 注：t518 那次「移除映射回 3D」是错的方向，回退。
- 实现：恢复 t492 双候选（item/<name>.png 优先、block/<name>_front.png 兜底）—— demo 包 1.8.2.2 无 item/crafting_table.png 但有 block/crafting_table_front.png/furnace_front.png，故落到 _front 兜底即用户要的 2D 平面 icon；用户后续给 item PNG 时首候选直接命中。同步更新 .h/hotbar.cpp/ResourceBrowser.qml 注释。

**t538. 木梯放下仍有问题**（用户「感觉没什么变化」） ✅✅ 已完成（commit 030c61a）
- t519 重生贴图（轨贴瓦片两侧）可能没生效，或几何仍有问题。核查 t519 贴图是否真进 atlas + 放下渲染路径，必要时改几何（梯子薄板比例）。
- 核查结论：t519 贴图**已真进** atlas（tile 78 逐像素一致）+ icon_ladder.png（4× NEAREST 一致）+ 渲染路径（partialblockgeometry Ladder case 用 tile 78，cutout 材质 Mask+alphaCutoff）。用户「感觉没什么变化」根因 = 启用的 demo 资源包（settings.json resourcePackEnabled=true）把 tile 78 覆盖成包内 128px ladder.png 平滑缩小版 → t519 默认贴图在用户会话不可见。改几何：薄板水平内缩到 12/16（两侧各 2/16，满高）→ 放下读作「贴墙窄薄板梯子」非满格宽板；同步 raycastAABBs 内缩。

### 🆕 R19.2 新 bug（用户本轮新发现）

**t539. 梯子侧穿起立 bug** ✅✅ 已完成（commit a67d9df）
- 用户：「一格宽两格高通道里放梯子，梯子在侧面（左右），我直走穿过（没对着梯子正面），但我还是会被梯子升起来（爬梯动作）。应该：从梯子侧边过 → 不要起立/上升动作，直接穿过。」
- 查 playercontroller 爬梯判定：当前是「身旁任意面有梯子就触发爬梯」。应改：只在**面向梯子（视线/移动方向对准梯子面）**时才爬梯；侧身经过不爬。

**t540. 创造飞行长按 shift 不落地** ✅✅ 已完成（commit 150525c）
- 用户：「创造模式飞行时长按 shift，应该落地后立即切步行模式，得重新按两下空格才能再飞。现在长按 shift 只是贴地飞行（还是飞态），按两下空格才下来。」
- 查 playercontroller 飞行/shift 逻辑：飞行态 + shift + 触地 → 应自动退出飞行切步行。

**t541. 发射器 UI 拿起物品光标消失** ✅✅ 已完成（commit e5b6523）
- 用户：「发射器 UI 里拿起背包物品（左键），物品直接消失不见，再点又出现，没有跟随光标的动画。」
- 查 DispenserUI.qml（t517 新建）：光标手持浮动图标 visible 没接 dispenserOpen，或 InventoryOps 拾起后光标图标没渲染。对比工作台/熔炉的光标跟随实现补全。
- 修复（Main.qml）：光标手持浮动图标 visible 绑定补 `anvilOpen || dispenserOpen`；`hoveredSlotKey`/`swapHoveredWithHotbar`/`dropFromHoveredSlot` 三路由加 anvil/dispenser 分支。

**t542. 发射器/铁砧/附魔台破掉不掉内容** ✅✅ 已完成（commit e5b6523）（通病）
- 用户：「发射器放东西进去挖掉，没掉东西。这几个功能方块通病。」
- 仿 t522（破箱掉内容）：破发射器/铁砧/附魔台时 dump 内部物品。发射器现在 9 槽是面板本地数组（t517 follow-up），需先确认存储位置。附魔台/铁砧目前无容器（shell-mode），主要修发射器。
- 修复：新建 DispenserStore（C++ VM，按方块坐标键控 9 槽 3×3，仿 ChestStore/FurnaceStore）+ WorldStore dispensers 表落盘 + DispenserUI 改 per-block 寻址 + onBlockBroken(Dispenser=107) dump 9 槽 + clearDispenser。铁砧/附魔台 shell 无容器 → 无需 dump。

**t543. 铁砧 UI 三问题** ✅✅ 已完成（commit e5b6523）
- 用户反馈：① 拿起物品光标消失（同 t541 通病）；② **底部 hotbar 标了数字**（1-9），应跟工作台/熔炉统一不标数字；③ **颜色风格不对**（暗橙色），应统一成现有 UI 风格；④ **只有一个格子操作**，应是「空格 + 空格 = 空格」（左槽放武器 + 右槽放附魔书 → 中间产物槽），仿 MC 原版铁砧贴图布局，符合本工程 UI；⑤ 拿了东西放不进去。
- 重做 AnvilUI（t516 的 shell-mode 不符）：左输入槽 + 右输入槽（附魔书/第二件）+ 中产物槽，底部 4 行背包无数字。功能（消耗经验修复/合并/改名）后补，先界面布局对。
- 修复：AnvilUI 重做为三槽布局（左输入 + 右输入 → 中产物，本地 anvil 组可放取物品）+ 深色 #1b1f24 风格 + 底部 hotbar 无数字 + 关包归还背包；功能区保留占位交互。

**t544. 附魔台 UI 布局错** ✅✅ 已完成（commit e5b6523）
- 用户：「附魔台应该有**两个格子**（一个放青金石、一个放要附魔的武器/工具），三个附魔选项放**右边竖排**（1/2/3 竖着）。现在三个选项是横着的，且没有两个格子放东西。青金石槽的空白占位图标 = 青金石轮廓。」
- 重做 EnchantingTableUI（t515）：左输入槽（武器/工具）+ 青金石槽（空白占位用青金石轮廓图标）+ 右侧三个附魔选项竖排。底部 4 行背包。功能后补，先界面。
- 修复：EnchantingTableUI 重做为左武器槽 + 青金石槽（空占位 Canvas 青金石轮廓）+ 右侧 1/2/3 选项竖排 + 底部 4 行背包；本地 enchant 组可放取物品。

**t545. 甘蔗中间/最下挖不掉落（t524 只修一半）** ✅✅ 已完成（commit 030c61a）
- 用户：「3 格高甘蔗挖第二格，最上面那格应同时掉落；挖最下面那格，整柱应全掉。现在没掉。」
- t524 的 checkSugarcaneOnEdit 只处理「破下方支撑方块」。**挖甘蔗本身**（中间/最下）走 PlayerController 级联，可能漏了。查 PlayerController 破甘蔗的级联掉落逻辑，补：挖任一格 → 其上整柱全掉。
- 修复：t418 级联（playercontroller.cpp finishMiningAt）原 `if (drop && ...)` —— 生存正常、创造（drop=false）不连带整柱 → 破甘蔗中间/最下剩悬空。改：级联破格**恒触发**（含创造，机制等价 MC 破任一甘蔗格其上整柱坍落），掉落物 spawnItem 仅 drop=true（生存）时发。

**t546. 生存物品栏装备图标要 3D（创造+生存共用）** ✅✅ 已完成（commit 6afdd6a）
- 用户：「创造模式的生存物品栏 + 生存模式的生存背包，装备/物品图标都要 3D（像现在的人物第三人称那样）。开 F3+B 物品栏也显示 F3+B 状态。完全复刻第三人称视角。两个共用一个 UI 就行。」
- 实现：新 `ArmorSlot3D.qml`（mini View3D 渲染「玩家身体部位 + 该部位护甲」，空槽灰体 / 装备玩家本色+护甲色，复用 Main.qml playerModel 几何/配色）+ `CharacterPreview3D.qml`（完整 3D 玩家模型 + 4 装备槽护甲 overlay，替代 2D Canvas 剪影）。两面板（SurvivalInventory + Inventory tab6）共用同一组件 + 同一 hotbar VM。F3+B（window.showHitboxes）时 3D 预览叠加 AABB 线框（部位 + 玩家全身）。判断取舍：主栏/物品栏槽保留 2D（全槽 3D = ~36 View3D 渲染 pass，性能不划算；用户重点 = 装备图标像人物）。耐久条/数字仍叠在 3D 预览上方。

### 🐉 雪傀儡 + 积雪层（t529/t525-t527，之前分下轮，本轮全做）

**t529. 雪傀儡造型/行为（5 子项）** ✅✅ 已完成（commit 916312e）
- 1. **仍无南瓜头** ✅：南瓜头 local y center 提到 +1.45（旧 +1.23 紧贴顶雪块顶共面读作「与身一体」）+ scale 放大 (0.82,0.72,0.72)（旧 0.78,0.66,0.66）→ 头与顶雪块之间留 0.19 格「脖颈缝隙」一眼辨头在顶上。
- 2. **一直朝玩家** ✅：移除 aiSnowGolem 持续「玩家在范围内 → yaw 朝玩家」覆盖（t499 二轮改过头）；改「生成时固定朝玩家」（spawnMobTypedYaw 生成时 yaw=atan2(-dx,-dz) 朝玩家，让玩家初次见南瓜脸）+「平时 aiWander 随机朝向」（移除覆盖后 yaw 由 aiWander 随机选向）。
- 3. **走路飞起来 + 踩的积雪层变整块** ✅：雪层改「放身后格」（golem 离开格留雪脚印）替代旧「放脚下」—— 旧放脚下使 SnowLayer 与 golem 底雪块（local y[-0.90,0]）在同一格重叠 → 视觉读作「整格雪方块」+ 模型底面贴 cell 底与雪层共面打架读作「踩雪飞起」；改放身后格后 golem 模型（前格）与雪层（身后格）永不在同一格 → 雪层显干净 1/8 薄板 + golem restY 落实体支撑（SnowLayer solid=false 不被当支撑，restY 不抬高）。
- 4. **F3+B 碰撞箱看不到/很小** ✅：根因是 WireCube 白框线融进雪白身不可见（非 halfH 错；halfH=0.90 → AABB 1.8 格高已正确）→ 雪/铁傀儡改青色框线（#00e5ff）与白雪 / 铁灰高对比可见。
- 5. **头在肚子位置** ✅：同 1（南瓜头 local y 提到 +1.45，头与身有清晰分界，不再读作「在肚子位置」）。

**t525. 积雪层手持/item 图标整块** ✅✅ 已完成（commit ebf81c4）
- 用户（第三次说）：「积雪层拿手上 + 背包图标还是整格方块，要 1/8 格。」改 snow layer item icon + 手持渲染为薄板。

**t526. 积雪层自然生成地貌** ✅✅ 已完成（commit ebf81c4）
- ① 远离海边/沙滩；② 底下泥→雪块→积雪层（不直接泥上积雪层，不生成草方块）。

**t527. 积雪块不浮空（支撑掉落+保留层数）** ✅✅ 已完成（commit ebf81c4）
- 用户（第三次说）：「积雪块不能浮空。打掉下面方块要掉落，保留层数（8 层掉 8 层，1-2 层掉 1-2 层）。」加重力/支撑掉落 + 掉落实体携带层数。

### ⛵ 船（t530-t536，之前分下轮，本轮全做）

**t530.** 下船 shift 提示 ~5 秒消失（现常驻）。 ✅✅ 已完成（commit 42ab1ce）
**t531.** 船太轻（碰撞体质量加重）。 ✅✅ 已完成（commit 42ab1ce）
**t532.** 坐姿动画（腿 90°，非卡地底）。 ✅✅ 已完成（commit 42ab1ce）
**t533.** 船内有水（船体不透明阻水）。 ✅✅ 已完成（commit 42ab1ce）
**t534.** 船碰岸速度>阈值损坏。 ✅✅ 已完成（commit 42ab1ce）
**t535.** 船撞坏掉木板+木棍（非船；挖才掉完整船）。 ✅✅ 已完成（commit 42ab1ce）
**t536.** 橡木/云杉船区分贴图。 ✅✅ 已完成（commit 42ab1ce）

### 📎 R19.2 本轮范围（全部，不准拖）
t537-t546（10 项新/回退）+ t529 + t525/t526/t527（雪傀儡+积雪层 4 项）+ t530-t536（船 7 项）= **共 21 项，一次性 workflow 全做完**。

---

## ⚠️⚠️ R19.3 复盘（2026-08-14 用户验证 R19.2 后，HEAD c783974）

> **背景**：R19.2 修了 21 项。用户复核 → 部分修好（木梯✅/创造飞shift✅/甘蔗挖沙✅/积雪层图标+不浮空✅），但大量项用户说没修好/新 bug + 老功能重提（指南针/钟/红石矿）。**用户铁律：写 dev-plan + 全部修完不准拖。**
> **本轮 24 项（t547-t570）**：R19.2 复核仍有问题 + 新 bug + 新功能（指南针/钟/红石矿）。

### 🔄 R19.2 复核 — 仍有问题（已打勾但用户说没修好）

**t547 甘蔗** ✅✅ 已完成（commit 8ab0ef8）（t545 只修了挖沙/挖中格）
- 用户：① 只能放两格，放不下第三格；② 打第一格第二格没掉落（直接消失）；③ 能种在水里面（不对）；④ 沙滩生成太频繁。
- 修：① 叠放邻水门改「沿柱下走到柱基（沙/草/土）再查 baseY/baseY-1 邻水」+ 放置高度上限 3 格（kSugarcanePlaceMaxHeight）；③ 甘蔗目标格须 Air（主射线穿水落水格 → 拒）；② 级联掉落恒发（含创造，破任一甘蔗/仙人掌格 → 该格+其上整柱全掉落实体，同「挖沙」整柱掉落；生存主格 `if(!drop)` 防双掉）；④ worldgen kSugarcanePct 30→10（默认 seed 81→22 块）。

**t548 三功能 UI 底部黑色残留** ✅✅ 已完成（见 t549 commit）
- 用户：「附魔台/铁砧/发射器打开后，除了主 UI 底部还有黑色小 UI，之前没删干净。」
- 修：根因 = openEnchantingTable/openAnvil/openDispenser 误调 progress.onInventoryOpened() →「打开背包」成就解锁 toast（z=170 黑色小 UI）弹在面板之上（新世界每次首开必弹）。删三处调用（三功能方块非背包，语义亦不符）。

**t549 附魔台 shift+左键 + 书架检测 + 附魔逻辑** ✅✅ 已完成（见 t548 同批 commit）
- 用户：① 拿稿子按 shift+左键应把工具直接放进去（现在不行 + shift+左键会蹲下）；② 工作台 shift+左键不会蹲下，但附魔台/铁砧/发射器 shift+左键会蹲（视角蹲）+ 工具放不进去；③ 附魔功能做了但「钻石镐+青金石放背包就能附魔」不对（应在 UI 槽里）；④ 书架检测：旁边放书架显示还是 0。
- 修：① Main.qml bagOpen 守卫（Shift press/release/滚轮/T/Q/暂停叠层）扩到 enchantingTableOpen/anvilOpen/dispenserOpen（防蹲）+ 三 UI 各自 Shift+左键双向语义（附魔台：可附魔物→槽0/青金石→槽1/槽→归包；铁砧：工具护甲→左槽/修复材料·附魔书→右槽；发射器：背包↔9槽）；② 附魔消耗来源改 UI 槽 1 青金石（lapisCount 读 enchantSlots[1] 非背包 materialCount）+ 档位门控 itemReady（槽 0 可附魔且未附魔）+ doEnchant 真附魔（selectEnchantsPreview 同 seed 写入槽 0 物品附魔元数据，紫光晕显示）；③ bookshelfPower 触碰新增 worldEditRev（放/破方块自增 → 绑定重算，修 countBookshelvesAround 无 NOTIFY 永不刷新）。

**t550 铁砧二轮重做** ✅✅ 已完成（commit 693037d）
- 用户：① A+B=C 两个输入都应在左边（不是左输入/右输入分开）；② 格子上不要「左输入/右输入/产物」文字；③ 去掉下面三行文字（修复/附魔合并/重命名）和按钮；④ 只显示最上面消耗等级 + 改名；⑤ 等级显示在产物格下绿字（放东西能出产物就显所需等级，改名 1 级起）；⑥ 重命名输入框按 Esc 退不出卡死（要修）。
- 参考 MC 铁砧：左放铁盔甲+右放铁锭→产物修复（3 铁锭修满，1 锭补 1/3 耐久）；修工具同理。

**t551 生存物品栏 3D 复原** ✅✅ 已完成（commit d893b1b）
- 用户：「搞反了！要复原之前生存模式的空装备栏。现在把人物 3D 模型弄掉了。」旁边有个人但偏左（右移 1 格）；人物不会动（要跟随玩家实际动作动）；朝向看鼠标指针（会旋转/头转/身转）；背包物品栏那个 3D 模型应看鼠标。
- 查：SurvivalInventory 空装备栏（复原生存版）+ 人物 3D 右移 + 跟玩家动作 + 看鼠标指针。
- 完成：① 空装备槽回归 t497 生存版占位（SurvivalInventory：pack empty_armor_slot_*.png + Canvas 剪影 + MaterialIcon；Inventory tab6：Canvas + MaterialIcon），ArmorSlot3D 移除；② CharacterPreview3D x 右移 1 格（slotSize+6 → slotSize*2+6，两面板）；③ 3D 人物跟玩家动作（walkPhase 四肢摆动 + moveState 蹲姿 + Timer 采样 feetPosition 积分离地高度 → 跳升/收腿）；④⑤ 看鼠标（面板绑 Main.qml cursorTracker.point.globalPosition → bodyYaw 65% + headYaw 35% + headPitch）。

**t552 雪傀儡二轮** ✅✅ 已完成（commit 2a522df）（t529 部分）
- 用户：① 底下两个雪块一样大，下面应大一点（雪堆：下大上小）；② 头还是白色雪头没有南瓜头 + 头悬空；③ 没打他莫名倒下死掉。
- 查：雪傀儡雪块比例（下 0.8 上 0.6 之类）、南瓜头 overlay 仍不可见/悬空、AI 莫名死亡。

**t553 雪球不击退** ✅✅ 已完成（commit 2a522df）
- 用户：「雪球打生物不击退，应该像箭一样击退。」
- 查：雪球（snowball 弹丸）命中 mob 击退逻辑（对比箭 arrow 击退）。

**t554 积雪层不能放方块侧边** ✅✅ 已完成（commit 832a99f）
- 用户：「积雪层不能放方块侧边（只能放完整方块上面）。现在能悬空放树侧边。」
- 查：SnowLayer 放置判定——只能放在完整方块顶面，不能放侧边/悬空。

**t555 删生物额外眼睛** ✅✅ 已完成（commit 832a99f）
- 用户：「生物贴图已有眼睛了，不需要额外补的眼睛（牛的眼睛不用）。」
- 查：Main.qml mob delegate 补的眼（猪/牛/羊/蜘蛛等纯色子 Model）——贴图有眼则删。

**t556 船二轮重做** ✅✅ 已完成（commit dbdb381）
- 用户：① 橡木/云杉分色仍错（放云杉变橡木样、撞坏掉橡木板）；② 太轻（随便推就走，还能推上岸）+ 又说走不动（水上）——碰撞/推动调优；③ 碰撞箱太大（应小一点）；④ 轻松上岸（碰到岸边方块应被挡，速度>阈值才坏）；⑤ 坐船不禁走路动画（划船时腿手还在动）；⑥ 船 4 个角闪烁（木板叠一起）。
- 查：boatmodel 碰撞箱 + 船体材质（角闪=两个木板几何重叠）+ 坐船动画禁用 + 上岸挡停 + 橡/云杉分色。
- 修：① 根因 = 实例作用域枚举 `boats.Spruce` 在 QML 解析不可靠（undefined → 恒走 Oak 分支）→ 改类型作用域 `BoatManager.Spruce`（boatBroken/boatWrecked/btBlockId 三处）+ boat delegate 语句块绑定改表达式形式（lessons-learned t498 漏注册防护）。② 玩家推船冲量 0.3→0.08 + 空船摩擦 3.0→5.0（推船滑行 <0.01 格、肉眼不动）。③ 碰撞盒改矩形匹配船体：kBoatHalfW 0.8→0.5（X 宽 1.0）/ 新增 kBoatHalfLen 0.7（Z 长 1.4）/ kBoatHalfH 0.55→0.35（高 0.7）；船体视觉缩 1.6→1.4 长对齐；F3+B hitbox 同步。④ footprint 变小 + 船变重 → 岸边方块被 footprint 挡停，仅速度>kBoatCrashSpeed(7) 才撞毁。⑤ 骑乘分支强制 m_moveSpeed=0 + 不推进 walkPhase → walkBlend=0 四肢归中性（坐姿 sitBlend 独立驱动）。⑥ 四角闪烁根因 = 旧横壁跨满宽 + 舷壁全长 → 四角两块同材质立方体重叠 → 深度测试交替闪烁；改「横壁跨满宽 + 舷壁只嵌中间（长 1.0）」→ 角部无重叠无缝隙。

**t557 金工具+铜工具** ✅✅ 已完成（commit dbdb381）
- 用户：「精制（金）工具没加，铜工具也没加。现在只有铁镐→钻石镐。」
- 查：ToolRegistry 加金（tier 4?）/铜工具档。金 tools 机制等价 MC 1.0（金耐久低挖速快）；铜是本工程已有材料。
- 修：ToolId 末尾追加不重排（保存档兼容）：金（tier 5：speedMul 12.0 最快 / 耐久 32 最脆 = MC 1.0 gold「快而脆」；金剑伤害 4 同木剑）+ 铜（tier 6：speedMul 5.0 / 耐久 180，介于石 / 铁之间；铜剑 5）五类全加（镐/斧/铲/剑/锄 = 10 件）。toolregistry.cpp kTools/kMcToolId 补行 + attackDamage 加 tier 4/5/6；10 条合成配方（金锭/铜锭 + 木棒）；hotbar creativeTools + anvilRepairMaterial（金→金锭/铜→铜锭）；ToolIcon tier 5/6 配色 + itemIdFromTypeTier 特例映射；Main.qml 手持/第三人称/掉落物 tier 5/6 配色；resourcepackmanager 金/铜工具 PNG 映射；docs/item-ids.md 工具段补表。

**t558 雪傀儡 AI 朝向** ✅✅ 已完成（commit 2a522df）
- 用户：「雪傀儡打僵尸应先面向敌对生物再发雪球（现在往脑门后面发）；F3+B 看不到朝向（红线在脑子里被挡）。」
- 查：aiSnowGolem 发雪球前 yaw 转向目标 + F3+B 朝向线可见性。

### 🆕 R19.3 新 bug

**t559 蹲下穿行重做** ✅✅ 已完成（commit a1b652c）
- 用户：① 1.5 格通道松 shift 应自动保持蹲（直到头顶有空间才站）；② 半砖楼梯（下半砖+上方块=1.5 格）应能自动上（不用跳）；③ 通道里松 shift 不应穿墙。
- 查：playercontroller 蹲下/站起自动判定（头顶空间不足自动保持蹲）+ 半砖自动爬 + 松 shift 不穿墙。

**t560 怪物盔甲动画** ✅✅ 已完成（commit a1b652c）
- 用户：「僵尸腿跑步有动画但盔甲像固定（盔甲模型没跟腿动画）。」
- 查：mob armor 子模型是否随腿 walkPhase 动。

**t561 白天着火细节** ✅✅ 已完成（commit a1b652c）
- 用户：① 水边/水里不烧；② 火焰粒子效果不见了（被删了？）；③ 戴帽免疫白天着火。
- 查：白天僵尸/骷髅着火（水下不烧 + 火焰粒子恢复 + 头盔免疫）。

**t562 刷怪上限** ✅✅ 已完成（commit f0551ab）
- 用户：「不能无上限刷怪，白天一堆怪。」
- 修：EntityManager 加**区域敌对上限**（`hostileCountNear` 计玩家周边 48 格内活体敌对，≥ kHostileLocalCap=12 停刷——黑暗刷怪 spawn 调度 + 刷怪笼 tick 两路都过此门，全局 kHostileMobCap=30 兜底不变；「每区块/区域 mob 上限，达上限停刷」）；初始被动散布收紧（scatter 20→10 / 鱿鱼 6→3 / 狼 4→2 / 豹猫 3→2，白天可见被动数显著降）。

**t563 水流动画+流动** ✅✅ 已完成（commit f0551ab）
- 用户：① 大峡谷水流到一半不流了（放方块刷新后）；② 水往下流动画斑点往上走（方向反，岩浆可能也反）；③ 水岩浆混合闪烁。
- 修：① 根因 = 流体 tick「活动盒过滤」饿死盒外级联——盒过滤快照零写入时 dirty 已被清 → 下 tick 早退 → 峡谷瀑布波前在盒外永久停摆（放方块 poke 只救一格）。修：盒过滤 tick 零写入 → 保持 dirty → 下 tick 盒空自动退回全量快照兜底（级联自愈收敛，真稳态仍停扫早退，水/岩浆两 tick 同修）。② 根因 = build_fluid_strips.py 用 `np.flipud` 把帧 0 翻到条带底时**把每帧内容也上下翻**——「下移」流动编码在屏上呈「上移」。修：改 `rows.reverse()` 反序拼帧（帧 0 仍在底、帧内容保持原方向）→ positionV 正向播放 = 图案下移（水/岩浆条带重新生成，qrc 三方约定不变）。③ 根因 = 水段（0.7）/岩浆段（0.95）两独立透明 mesh 在分界面**同一平面各画一张满侧透明面** → z-fighting 闪烁。修：流体段面剔除加「邻接异种流体 → 剔本面」（两侧都剔 → 无共面，交互凝固归 tick）。

**t564 末地传送门生成多个** ✅✅ 已完成（commit f0551ab）
- 用户：「同一区块/出生点附近生成好几个末地传送门（应至多一个）。」
- 修：placeStronghold 改「收集候选 → 选距世界中心（出生点）最近的一座放置」（placeAt lambda 收口建造代码；hash 采样不变 → 同 seed 确定性同位）—— 全图**至多一座要塞 / 一个末地传送门**（旧 40 格网格 × 55% 在 160×160 世界 ≈ 9 座）。

**t565 废弃矿坑重做** ✅✅ 已完成（commit bee6869）
- 用户：① 生成直线（应连通/角落生成洞穴）；② 蜘蛛网（粘人/空手挖不掉/剑挖掉线/4 线合白羊毛）；③ 矿坑自然火把；④ 铁轨（只有普通一种/放下不连接/方向固定/不能横竖/要能转弯）+ 矿车；⑤ 矿坑底可石头非木板；⑥ 矿坑连通。
- 大项：废弃矿坑重做 + 蜘蛛网方块 + 铁轨转弯/连接。

**t566 背包左键平均分恢复** ✅✅ 已完成（commit 34adc3f）
- 用户：「左键几何平均分功能没了，只剩右键。要弄回来。」
- 查：InventoryOps.js 左键均分逻辑（被删了？恢复）。

### 🆕 R19.3 新功能（用户重提）

**t567 指南针** ✅✅ 已完成（commit 34adc3f）（4 铁锭+1 红石；指向出生点；出生点在第一个区块中心非全 0；动画留空白）
**t568 钟** ✅✅ 已完成（commit 34adc3f）（金锭+红石；看当前时间；PNG 链接留 D 接口）
**t569 红石矿石** ✅✅ 已完成（commit be7e82b）
**t570 月亮/星星** ✅✅ 已完成（commit e5f2815）（月亮背景灰 PNG 消不掉→改正方形月亮；星星太大比月亮大）

---

## ⚠️⚠️ R19.4 复盘（2026-08-15 用户验证 R19.3+审查修复批后，HEAD 407a6e1）

> **背景**：R19.3（t547-t570）+ 24h 审查修复批（dc16ca2→407a6e1，11 commit）全落地。用户 playtest 复核 → 红石/僵尸眼删/鸡血块侧放/矿车/铁砧产物/金工具等级 OK；但仍有大量 bug + 铁砧三轮 UI + 附魔系统整体完善 + 生物图鉴贴图细节 + 船/步行物理再调。**用户铁律：写 dev-plan → 审阅通过后开工，全部修完不准拖。**
> **本轮 t571-t604（34 项）**。

### 🅰 创造模式掉落语义（1 项）

**t571** 创造模式挖掘掉落语义修正 ✅✅ 已完成（commit 见 git log，t571）
- 用户：「创造模式打任何方块都不掉落（被破坏的方块本体不掉）。但**自然掉落**要保留：打掉甘蔗中间格 → 最上面格因失撑自然掉落（这个要掉）；打掉甘蔗下面沙子 → 整柱甘蔗失撑掉落（要掉）。箱子例外：箱子本体不掉但**内容物要掉出来**。」
- 查：`finishMiningAt` 创造瞬破 `drop=false` → 本格不掉（对）；但 t547 甘蔗级联「破任一甘蔗格 → 整柱全掉」在创造模式也整柱掉（用户认为中间格上方失撑的自然掉落对，但**被破坏的那一格本身**不应掉）。逐路径核：① 甘蔗/仙人掌级联掉落拆成「本格掉落仅生存（drop 标志）」+「上方失撑格自然掉落恒发（含创造）」；② `dropUnsupportedTorchesAround`/`dropUnsupportedLaddersAround`/`dropUnsupportedCropsAround` 等失撑掉落属「自然掉落」恒发（创造保留，符合用户语义）；③ 破箱子：创造模式箱子本体不掉 + **内容物照常掉**（核 Main.qml 破箱掉内容路径在创造是否被 drop=false 连带跳过）。

### 🅱 物品栏 3D 人物（2 项）

**t572** 创造模式生存物品栏 tab 空装备栏图标对齐生存版 ✅✅ 已完成（commit 8b759d0）
- 用户：「创造模式里的生存物品栏 tab，空白装备栏 4 个图标和生存模式的不一样（生存模式那 4 个是对的）。」
- 查：`Inventory.qml` tab6（创造里的生存 tab）空装备槽占位 vs `SurvivalInventory.qml` —— 统一为生存版（pack `empty_armor_slot_*.png` + Canvas 剪影 + MaterialIcon，t551 已做过 SurvivalInventory，把同款搬到 Inventory tab6）。

**t573** 3D 人物偏移 + 左右看向反转 ✅✅ 已完成（commit 6f0c2ef）
- 用户：「两个背包里 3D 模型太靠右，要往左一点；看鼠标左右反了（鼠标在左人物看向右），上下是对的。」
- 查：`CharacterPreview3D.qml` x 位置回调（t551 移到 slotSize*2+6 过头了，回调一档）+ headYaw/bodyYaw 符号取反（鼠标 x 相对面板中心 dx → yaw 方向）。

### 🅲 蹲下保持（2 项）

**t574** 1.5 格通道开背包关掉后自动站起（头卡方块里）✅✅ 已完成（commit 见 git log，t574）
- 用户：「生存 1.5 格自动蹲后按背包再关掉，突然站起来了，头直接卡进上方方块。必须重新按 shift 才能再蹲。这是 bug，蹲着卡头必须不被允许。」
- 查：`playercontroller` 开/关背包路径（bagOpen 时是否强制清 crouch / UI 焦点切换丢 shift 键态）。修：crouch 保持只由「头顶空间 + shift 键」决定，UI 开关不碰蹲态；且**蹲态下永远不允许站起判定通过**（头顶不足时强制约束保持蹲）。

**t575** 蹲下保持「无论如何不自动站起」✅✅ 已完成（commit 见 git log，t575）
- 用户：「1.5 格高松 shift 之后无论如何都不会自动站起来（除非走出到头顶有空间的地方才自动站），而且生存模式卡在里面会扣血。」
- 查：t559 实现「松 shift 自动保持蹲」是否在搬砖等场景失效 + 卡头扣血（suffocation 伤害在蹲态不该触发）。修：蹲保持条件 = 头顶空间不足（moveAxis 前强制 m_crouch=true 直到 canStandUp()==true 才恢复站姿；站起判定失败不扣血——碰撞嵌入时 suffocation 判定排除「因保持蹲未被允许站起」的合法蹲姿）。

### 🅳 铁砧三轮 UI（3 项，仿 MC 帖子操作流）

**t576** 铁砧布局微调：两输入槽中间加「+」、删「放入物品与材料」提示文字 ✅✅ 已完成（commit 见 git log，t576）
- 查：`AnvilUI.qml` A/B 槽间加号 Image/Text；删顶部提示文字。

**t577** 重命名框移到产物上方 + 放入物品自动显名 + 去掉「重命名」按钮标签 ✅✅ 已完成（commit 见 git log，t577）
- 用户：「输入名字框应在合成产物上面（不是下面）；放入物品（如石镐）后这里直接显示它当前的名字；耐久度不要显示进来；重命名后下方等级行显示改名后的名字（不再需要『重命名』按钮字样）。」
- 查：rename TextField 位置上移；文本 = 当前产物名（有输入则显输入预览）；等级行文案含产物名。

**t578** 铁砧放入规则：同物+对应修复材料才双格生效，不匹配出空产物 ✅✅ 已完成（commit 见 git log，t578）
- 用户：「铁镐+铁锭应出修复产物（现在是空的）；铁镐+随便另一个东西 → 不能合（产物空）。」
- 查：`AnvilUI.qml` 产物计算 gate —— 左槽任意物 + 右槽：① 是该物 repairMaterial（ToolRegistry anvilRepairMaterial）→ 修复产物；② 同物合并（双铁镐 → 合耐久）；③ 是附魔书 → 合附魔；其余 → 产物空。核现 repairMatUse/产物分支为何铁镐+铁锭出空（可能 gate 条件错了）。

### 🅴 发射器（2 项）

**t579** 发射器压力板触发不发射 bug ✅✅ 已完成（commit 见 git log，t579）
- 用户：「放箭进去，踩压力板没有射出东西。现在只有压力板能触发它吗？」
- 查：丛林神殿发射器陷阱（world.cpp `dispenser` 踩板触发）vs **玩家手放的发射器**：t517 发射器 UI 有 9 槽但压力板触发路径是否只接了神殿专用（或发射器内容物读取没接）。修：通用化 —— 压力板触发 → 邻接发射器（任意朝向）取内容物发射（箭=弹道实体/雪球/鸡蛋=投掷物/其余=掉落物弹出）。t565 矿车机关同理共用。

**t580** 发射器可发射剑与雪球 ✅✅ 已完成（commit 见 git log，t580）
- 查：发射器内容物分派表加 sword（短距弹射掉落物带伤害判定）+ snowball（投掷物实体）。鸡蛋（t583 加投掷后）一并接入。

### 🅵 步行自动上台阶回归（1 项，高危）

**t581** ✅✅ 已完成（commit 8c9cb60）
- 用户：「睡莲、压力板、鸡血块（红石矿?）走不上去要跳；shift 蹲下前面一格下半砖也上不去，跳也进不去。之前都可以！」
- 查：t559 把固定 0.55 抬升改成 `autoStepLift()` 精确扫描 —— 疑回归点：① `autoStepLift` 要求 `m_onGround` 且障碍 sub-AABB 与 footprint 严格重叠，但睡莲/压力板/雪层(1/8 高)的 AABB 很薄（ maxY-baseY=0.0625~0.06 ），`top <= baseY + 1e-3f` 过滤条件在玩家脚底略高于障碍底时把薄障碍排除；② 蹲态下（t574/575 通道场景）`canStandUp` 干扰抬升。修：薄障碍判定放宽（障碍 AABB 与玩家 footprint 在 XZ 重叠且 top ∈ (baseY-eps, baseY+kAutoStepMax] 即计入——脚底已嵌入薄障碍上沿的边界）；复测：睡莲/压力板/雪层/下半砖/楼梯直走能上 + 蹲态下半砖能上 + 1.5 格通道不穿墙不卡头。

### 🅶 雪傀儡（2 项）

**t582** ✅✅ 已完成（commit edfbceb）
- 用户：「生成后头还是没有南瓜；头太大，要比中间身子小一截。」
- 查：t552 已做过一轮（commit 2a522df）用户仍不见南瓜 —— 核 pack 贴图 UV（南瓜头可能映射到了雪块区域）+ 模型头 box 尺寸（现比身子大 → 改小一截，MC 1.0 雪傀儡头 8×8×8 比身子 10×8×10 小）。可参照铁傀儡 pack 接入模式（t199 验证可行）。
- 修（实测）：① 头 Model 从纯色橙 UnitCube（宽 0.66 > 顶雪块 0.60 → 读作「头比身子大且没南瓜」）改 BlockCube{blockId:100} + 共享图集（-Z 前面=pumpkin_face 刻面瓦片），缩 **0.50**（MC 8×8×8 半格，比顶块 0.60 小一截），头心 y=1.14；② tileFilenameMap 补 117/118/119 → pack block/pumpkin_side/face_off/top.png（pack 激 = HD 南瓜，关 = 程序生成瓦片，两态都真南瓜）；③ snow_golem.png 实测头部区只是雪+derpy 脸（MC 1.8+ 南瓜不在 entity 贴图内 → 南瓜头走 block 瓦片）；④ 眼/嘴 overlay 改仅 golemSheared（无头 derpy）时显示，防与贴图脸双层。

**t583** ✅✅ 已完成（commit 373686c）
- 用户：「鸡蛋还不能投掷，应该可以丢出来砸出小鸡。另外雪球击退有点大，改小一点。」
- 查：① 鸡蛋 item（已有？）→ 右键投掷物实体（同雪球 thrower 模式）+ 命中地面 1/8 概率生成小鸡；② `kSnowballKnockbackStrength` 2.0 → ~1.2 实测手感；③ 鸡蛋进发射器（t580 同批）。

### 🅷 船物理（1 项大）

**t584** 船地面/水面/冰面速度分档 + 水中碰岸停船 ✅✅ 已完成（commit 0e21aea）
- 用户：「船上方块速度跟水里一样——不对。三档：陆地最慢、水里第二、冰面最快+冰面驾驶有惯性（难操作才是对的）。最关键：从水里开碰岸边方块（哪怕陆地跟水面同高）要停下来，只有直接放陆地上开才不受阻。检测机制可能要重写。」
- 查：boatmanager 推进/摩擦参数按脚下介质分三档（land mul 大 / water 中 / ice 小+惯性保留）；「水中开碰岸停」：船 footprint 前方探测实心方块（非水）→ 速度清零（机制等价 MC 1.0 船撞岸受阻——区别于撞毁阈值）。t556 的 crashSpeed 阈值保留（>阈值撞毁）。

### 🅸 指南针/钟/月亮（2 项）

**t585** 指南针/钟改 pack 动画贴图 + 删手持 HUD 右上角指南针 ✅✅ 已完成（commit 7dd8436）
- 用户：「指南针手持时右上角显示方向——不要这个。给你 pack 动画贴图：compass 34 帧（compass_00..33.png）+ clock 66 帧（clock_00..65.png，另有 .mcmeta）在 `docs/Default HD 128x Demo 1.8.2.2/assets/minecraft/textures/item/`。」
- 修：① 删 Main.qml `compassHud`（9094-9206 区）；② 手持/掉落物/物品栏图标改**按状态选帧**：指南针帧 = 朝向出生点角度 → 帧 index（34 帧环）；钟帧 = 昼夜相位 → 66 帧环（mcmeta 默认逐帧，读 mcmeta 确认 frametime/顺序）。pack 图标管线（resourcepackmanager）加「动画帧序列」支持——按 (id, 状态值) 返回帧文件路径；状态变化时图标刷新（帧切换节流 ~4Hz）。

**t586** 月亮 PNG 修正 ✅✅ 已完成（commit a0c7e75）
- 用户：「月亮还是圆的 + 背景灰色偏距（PNG 还没改）。」
- 查：t570 用了正方形月亮但现仍显示圆形灰底 → 核 sky 渲染月亮贴图路径（moon 阶段 PNG 生成/挂载是否真的接上，可能 qrc 里还是旧圆月）。pack 无 moon.png（environment 只有 clouds/end_sky）→ 自绘方形月亮 PNG（冷色无透明背景问题：PNG 本身不透明方块，避开灰底）。
- 根因：贴图已是全不透明方形（alpha 全 255），但 build_moon.py 的**球面法线着色**在方形四角 |n_xy|>1 → nz 钳 0 → 四角恒判暗 → 暗蓝灰恰好填满内切圆外四角 = 「灰底上的圆月」。修：改**平面 terminator 模型**（明暗分界=直线 s<d，d=0.5·cosα，满相位恰全亮/新相位恰全暗；四角与中心同规则 → 无内切圆、无灰底），tools/build_moon.py 重生成 textures/moon_0..7.png。

### 🅹 工具体系（3 项）

**t587** 工具等级排序修正：铜在石头之后 ✅✅ 已完成（commit 09ca45c）
- 用户：「等级应是 木头→石头→铜→铁→金→钻石；现在铜排在钻石和金后面。」
- 查：creativeTools / ToolIcon / 合成表 UI 里的工具排序展示序（harvestLevel 已对（t-rv56 木1石2铜2铁3钻4），是**展示/排列顺序**错）。统一按 tier 序：wood→stone→copper→iron→gold→diamond（gold 挖掘等级=木但展示位在铁后，机制等价 MC 1.0 工具栏顺序）。
- 修：Hotbar::creativeTools()（hotbar.cpp，创造背包工具 tab + ResourceBrowser 消费同源）各组档序改 木→石→铜→铁→金→钻石（铜的挖掘定位介石/铁之间，展示位紧跟石头；tier 数值仅内部记账 speedMul/配色，与展示序解耦）。钻石档暂仅镐（镐组有第六位；t589 补齐后其余各组同序补位）。

**t588** 铜物品贴图：铁贴图染铜色 ✅✅ 已完成（commit 60b9f63）
- 用户：「铜的物品没贴图还在用老贴图，能不能用铁的染色成铜的，统一贴图。」
- 查：铜锭/铜块/铜矿/铜工具 icon —— pack 无铜贴图（1.8.2 无铜）→ 用对应铁 PNG 染铜色（同皮革 retintLeatherTemplate 模式：luma 保持 + 色相偏铜橙）。resourcepackmanager 加铜色 tint 表。
- 修：resourcepackmanager.cpp 加 retintCopperTemplate（铁头灰阶像素 |r-g|<14&&|g-b|<14 → luma 映射铜橙梯度 #8a4818/#c87850/#e8a088，木柄棕像素保留）+ copperIronFallback 回退表（铜工具 0x118..0x11C → iron_pickaxe/axe/shovel/sword/hoe.png、铜锭 0x21D → iron_ingot.png）。itemIconSource 映射 PNG 缺失时命中回退表 → 染铜落盘 voxelsandbox_rp_copper_<id>.png + 缓存（同皮革 / 床模式）。铜原矿 0x21C 不进表（自绘已是铜配色）。无 pack 时自绘 ToolIcon/MaterialIcon 本就铜色，无需改。

**t589** 钻石工具补全（现在只有镐） ✅✅ 已完成（commit 700fdba）
- 用户：「钻石的工具只有镐子，其他的呢？」
- 查：ToolRegistry 钻石档五件（镐/斧/铲/剑/锄）+ 合成配方 + 图标 + hotbar —— t557 金铜加了五件，钻石可能本来就只有镐（早期只加了镐）。补齐斧/铲/剑/锄四件。
- 修：ToolId 0x11D..0x120 追加（DiamondAxe/Shovel/Sword/Hoe，不重排）；kTools + kMcToolId 补行；recipe.cpp 四配方（钻石+木棒同铁档形状）；creativeTools 各组钻石位补齐；itemFilenameMap → diamond_*.png（demo 包四图全有）；ToolIcon tier4 全类显式表；Main.qml 手持（锄/斧/铲/剑头色）+ 掉落物 tier4 青绿；item-ids.md 同步。

### 🅺 附魔系统整体完善（1 项大）

**t590** 附魔系统完善（附魔台选档真随机 + 装备附魔显示 + 等级/青金石消耗显示） ✅✅ 已完成（commit c7aeda4）
- 用户：「附魔整个系统还没做完善。附魔了但没显示是什么样的附魔情况。」
- 查（t475/t476/t549 已有底子：EnchantRegistry + ItemStack enchants + 三档选档 UI）：① 选档后**装备上的附魔要可见**——物品栏 tooltip/图标角标显示附魔名+等级（如「锋利 III」紫字），手持/掉落物紫光晕；② 附魔消耗：等级（ExperienceLevel）+ 青金石 1/2/3 —— 现在只扣青金石不扣等级？核 doEnchant 消耗路径补经验等级消耗（附魔台 UI 显示当前等级够不够）；③ 三档随机性：MC 1.0 机制=seed 随机（书架数影响档位池），核现 selectEnchantsPreview 是否真随机 + 书架 power 进档位权重；④ 修复附魔台 UI 槽位放入即显三档预览（现可能要点击才出）。
- 注：此项工作量大，agent prompt 里写全 EnchantRegistry 现状（src/Game/enchantregistry.* + t475/t476/t549 三个 commit 上下文）。

### 🅻 生物图鉴（资源查看器）贴图修正（8 项）

**t591** 资源查看器物品区滚动条样式 + 底部物品被滚动条遮挡 ✅✅ 已完成（commit 417273c）
- 用户：「生物这边没被遮挡，下面的物品确实被遮挡住了；滚动条是白色条，不符合 UI 统一风格。」」
- 查：ResourceBrowser 滚动条改项目统一 ScrollView/自定义样式（暗色细条）；物品 GridView 右/底 padding 补滚动条宽度。

**t592** 猪贴图：腿后跟黑色未覆盖 + 没嘴巴 ✅✅ 已完成（commit d0b40c8）
- 查：MobModel 猪 UV（腿 box 的 back/bottom 面采样越界到贴图外/邻区 → 显黑）+ 鼻头/嘴面 UV（MC pig 贴图自带鼻子贴图在特定区）。

**t593** 牛贴图核过最正常（无需修，PASS 项）—— 但顺带核羊贴图换成带羊毛版 ✅✅ 已完成（commit 2d319e1）
- 用户：「牛最正常。羊给的贴图是无羊毛版本，怪怪的，应给长满羊毛的生物。」
- 查：sheep entity 贴图 → pack 带羊毛版（sheep_fleece 或 sheep 贴图叠加 wool 层；1.8.2 entity/sheep.png 是本体+羊毛双色版？核 pack 实际文件）。

**t594** 蹒跚者改名「僵尸」问题 → 保持现名（PLAN §9 红线）；骷髅改名「骷髅弓箭手」+ 右臂贴图 + 脊柱黑色修复 ✅✅ 已完成（commit 548c2e7）
- 用户：「蹒跚者应该是僵尸才对（改名）」——**PLAN §9：Zombie=Shambler 蹒跚者是法律改名红线，不改回「僵尸」**（代码/UI 字串禁 MC 专有名词；MC 专有名词仅注释「机制等价」说明）。向用户说明。骷髅「骸骨」→「骷髅弓箭手」（「骷髅」是通用词非专有名词，可改）；右臂无贴图（单臂）→ 补双臂 box；脊柱黑色 → 脊柱 box UV 采样越界修。
- ⚠️ 同理「潜行者→苦力怕」「Creeper」等一律保持原创名（用户提到「潜行者应该就是苦力怕」——不改名，仅调模型）。

**t595** 潜行者（苦力怕）模型比例：腿太长缩小 + 头加大 + 身体加大 ✅✅ 已完成（commit 2ee746c）
- 查：MobModel Stalker box 尺寸（MC 1.0 creeper：头 8×8×8 / 身 4×12×8 / 四腿 4×6×4 —— 腿短身长）。

**t596** 蜘蛛贴图缺失（显示异常） ✅✅ 已完成（commit 52ba36e）
- 用户：「蜘蛛是不是没找到对应贴图？」
- 查：mobEntityMap 蜘蛛映射 → pack entity/spider.png 是否存在/路径大小写；MobModel 蜘蛛 box-UV 是否走了 pack 路径。
- 结论：映射正确（demo 包实存 entity/spider/spider.png，蜘蛛 head/body/leg 三组 box-UV 六面 100% 不透明）——「无贴图」观感实为 t597 暗色 tint 乘贴图（已修）。

**t597** 苦力怕+蜘蛛颜色暗淡（僵尸/骷髅明亮） ✅✅ 已完成（commit c13abea）
- 查：两 mob 的 Model 材质 brightness/光照通道 —— 是否没走 `PrincipledMaterial.NoLighting`（违反光照不变量）或贴图 tint 乘了暗色。对照 Shambler/Bones 的材质参数拉平。
- 根因：PrincipledMaterial 渲染 = baseColorMap × baseColor；Stalker/Spider 把 pack 关时的纯色体色（暗绿 0.37/0.66/0.23、暗黑红 0.16/0.10/0.10）也乘上 pack 贴图 → 压暗到 ~1/3 与 ~1/10。修：pack 贴图在身时 baseColor 近白（同 Shambler terrainLight 白 tint）；ResourceBrowser 图鉴预览同修（mobFallbackColor 仅 pack 关纯色路径用）。

**t598** 鸡腿贴图缺失 + 雪傀儡无头 + 铁傀儡头/腿/肩黑色 ✅✅ 已完成（commit c143a1b）
- 查：① 鸡腿 box UV 采样（鸡贴图腿区在特定 uv 区）；② 雪傀儡头（同 t582 图鉴路径）；③ 铁傀儡：腿前黑（front 面贴图采样错位）+ 肩黑色（shoulder box UV 越界）—— box-UV 公式对照 MC 1.8 iron_golem 实际 textureOffset 重算。
- 修：① demo 包 chicken.png 腿区不在 vanilla (26,0) 位（该区是翅膀/喙稀疏像素）→ 两腿与躯干共用 body(0,9,6,8,6)（六面 100% 不透明）。② ResourceBrowser 图鉴预览补 BlockCube{blockId:100} 南瓜头（同 Main.qml t582 方案，雪 y=1.14 宽 0.50 / 铁 y=0.95 宽 0.72）。③ 铁傀儡按包实测重算（包绘画布局与 vanilla 源码不符）：body d=9→11（修 Top 面空边=肩黑）、leg (0,30,4,12,4)→(0,70,9,5,6)、arm (40,40,4,16,4)→(60,58,4,16,6)，全部六面 100% 不透明。

**t599** 资源查看器 3D 模型鼠标拖拽旋转（自动旋转基础上可拖） ✅✅ 已完成（commit 70a6ea2）
- 查：ResourceBrowser 3D 预览 —— autoRotate + 鼠标 DragHandler 叠加（拖时暂停自动转，松手恢复；惯性与现自动旋转融合）。
- 实现：DragHandler(target:null，只读位移增量)——active 时 previewDragging=true 暂停 spinAngle 自转 NumberAnimation，水平增量×0.6°写 spinAngle（yaw）、垂直增量累计 userPitch（±60° 限幅）；松手 resumePitchAnim(400ms OutCubic) 平滑归零 pitch，yaw 自转从当前角度续转无跳变。方块/生物 3D 预览共用；enabled 限 3D 预览可见（大图标态不抢手势）。底部提示更新。

### 🅼 方块/图标杂项（3 项）

**t600** 石砖台阶+楼梯背包图标错误（三个都显示石砖整块） ✅✅ 已完成（commit dfdb5ed）
- 查：blockItemIconMap / item icon 路径 —— 石砖台阶、石砖楼梯 icon 应各自独立（现在都映射到 stone_bricks）。3D icon 渲染（cube icon 工具）或 pack 贴图。

**t601** 峡谷生成中间出现单格水源竖直流（worldgen 瑕疵） ✅✅ 已完成（commit abca5a5）
- 用户：「峡谷生成中间会莫名其妙出现一格水源，然后竖直往下流。」
- 查：worldgen 峡谷（ravine）路径水填充逻辑 —— 峡谷壁渗水格（本应只在峡谷壁碰水层时出现）→ 加门控：只在峡谷裁剪格 y 对应世界水层且邻接水时才置 Water，孤立置 Air。

**t602** F3+B 实体朝向箭头（现在只看到框看不到箭头） ✅✅ 已完成（commit a91a7b3）
- 查：F3 debug 生物 AABB 线框 + 朝向线（t558 提过红线被挡）——朝向线加长/加粗或从实体中心沿 yaw 前向画出框外。

### 🅽 统计/成就（1 项）

**t603** 合成工作台成就触发不了 + 统计合成次数恒 0 ✅✅ 已完成（commit f676dfd）
- 查：`playerprogress` 合成事件接线 —— doCraft 后未调 progress->onCraft(item, count)（或信号没接）→ 成就「合成工作台」+ 统计 itemsCrafted 都不涨。核 Main.qml/InventoryOps craft 路径 → progress 调用点。

### 🅾 生存射箭消耗语义（1 项）

**t604** 弓射箭：射出即消耗箭矢（不等插中方块） ✅✅ 已完成（commit e819f3e）
- 用户：「只要射出去就消耗箭矢，不管怎样（不是判定插到方块才消耗）。」
- 查：fireArrow → takeStack(箭,1) 移到发射时刻（现在可能挂在命中/消失回调）。
- 修后结论：扣减本就在射出瞬间（endBowDraw spawnArrowPlayer 后即 takeStack）；用户观感根因是 arrowPickupScan 无拾取延迟 —— 近距射墙的箭嵌入点在拾取半径内被下一帧秒拾回（扣 1 又 +1）。加 kArrowPickupDelayMs=1000 拾取延迟（arrowAgeMsAt 墙钟门控）。

### 📎 R19.4 范围
t571-t604（34 项：修 bug 26 + 系统/贴图/平衡 8）。铁砧三轮（t576-t578）、附魔完善（t590）、船三档（t584）、指南针动画（t585）为四大块，其余为单点修复。全部本轮做完不准拖。

---

## ⚠️⚠️ R19.5 复盘（2026-08-15 用户验证 R19.4 后，HEAD b0937fe）

> **背景**：R19.4 全 34 项落地。用户复核 → A/B/C/F/I（指南针钟）/J 工具排序/L 贴图/M 图标峡谷 N 成就触发 O 射箭 大部分认可；仍有细节 bug + **附魔书全套系统设计**（用户要求先出设计过目）+ 大量 pack 贴图接入。**本轮 t605-t621（17 项）。**

### 🅰 相机穿墙（1 项）

**t605** 第三人称相机 1.5 格通道穿墙查看 ✅✅ 已完成（commit fc82e60）
- 用户：「切第二/三人称时摄像头不应穿墙——只有 1.5 格通道时触发。」
- 查：t40 cameraDistance 沿偏移方向 DDA 钳制（playercontroller.cpp ~802-825）。1.5 格通道（蹲姿通道）场景相机把玩家身后墙穿过去查看。修：DDA 钳制对薄障碍/sub-AABB（上半砖天花板）也生效——现钳制可能只查整格 solid，蹲通道上方半砖的 sub-AABB 挡不住相机。相机距离取「DDA 命中距离 − skin」。

### 🅳 铁砧四轮（1 项多子点）

**t606** 铁砧 UI 细节批（8 子点，一个 commit）✅✅ 已完成
- ① A+B→C 槽行太靠上 → 整体下移一点；
- ② 放入工具/方块 → 其名字自动填进重命名输入框（可直接修改）；
- ③ 创造模式不消耗经验：等级消耗恒绿色可付（levelCost 显示绿、spendLevels 跳过）；
- ④ 重命名语义：只放 A（B 空）可以改名出 C；**B 一旦放不相关物品 → C 直接消失**（不匹配=无产物）；
- ⑤ 重命名输入框 UI：太长缩短 + 颜色调浅 + 文字垂直居中；
- ⑥ C 下方不再重复显示产物名（等级行只显消耗）；
- ⑦ 重命名到一半把物品拿回 → 输入框清空；再次放入 → 重新自动填名（②逻辑）。

### 🅴 发射器/投掷器/丢弃（1 大项 3 任务）

**t607** 发射器最后一个投掷物清零 bug ✅✅ 已完成
- 用户：「发射器里只剩一个投掷物时发射后仍显示在里面、再踩压力板不发射；拿出来鸡蛋消失。」
- 根因已定位：`DispenserStore::setSlot`（dispenserstore.cpp:53-63）空栈归一 `normCount = (normId>0 && count>0) ? count : 0` 逻辑对；问题在 UI 槽刷新链——setSlot 归 0 清槽后 emit dispenserChanged，但 UI 槽显示没刷新（revision 绑定漏）或**箭/鸡蛋分支发射后 setSlot 没走到**（早 return？读 dispenseFromDispenser 确认 EggId 分支后 setSlot 调用 ~3796 是否被跳过）。核对「发射后图标仍在 → 拿出消失」= store 内 count 已 0 但 UI 读的是旧值，取出时按旧 id/count 给物品又被归一清空。修：发射路径统一走 setSlot + UI 绑定 dispenserChanged 刷新。
- 实修结论：真根因不在 UI（dispCoordRev 触碰 revision 绑定本来正确），在 **setSlot 空栈归一顺序**——旧版 `normId` 只看 id，发射器扣最后 1 件写回 (itemId>0, count-1=0) 存成**幽灵栈 {id>0,count=0}**，破「id==0⟺count==0」不变式：UI 按 id 判空 → 图标残留；发射按 count 判空 → 不再发射；点击拾取拿到 count=0 → 「鸡蛋消失」。修：归一以 count 为先（count<=0 → id 一并归 0），setSlot/loadAll 双处 + ChestStore/FurnaceStore 同源防御收口。另补**玩家发射器身份**：放置时 `ensureDispenser` 注册条目（Main.qml onBlockPlaced id==107）+ `hasDispenser` 门控 fallback——有条目（含空）踩板按库存、空了无动作（陷阱解除）；无条目（worldgen 神殿）才默认射箭（旧版玩家空发射器踩板当神殿陷阱无限射箭）；allDispensers 全空条目也落盘（防重载后退回神殿行为）。

**t608** 发射器投掷物统一化（箭可拾取 + 投掷物打生物 + 发射口朝向）✅✅ 已完成
- 用户：「发射器射出的箭玩家应能拾取；投掷物都要能砸到生物互动（鸡蛋/雪球和手持一样：无伤害只击退）；方块等物品和投掷物不是同一个口出来的；发射器应规定朝向（像熔炉放下面朝玩家），我发射器后面放压力板它往前发射。」
- 修：① 箭 → arrowFromPlayer=true 语义（可拾取、命中 mob 伤害）——现在 spawnArrow(false) 命中玩家不对；② 鸡蛋/雪球投掷物统一从**发射口**（state 编码朝向面）出；③ 掉落物弹出也统一口；④ 放置时 state 记朝向（面朝玩家，同熔炉 state 编码 2/3/4/5），发射方向 = 朝向面外向；压力板触发找邻接发射器时读其朝向定发射向（现「发射器→压力板方向」反了：板在发射口侧才对——用户把压力板放发射器后面它朝前发射说明现在取的是板→发射器向量）。

**t609** 投掷器（Dropper）新方块 + Q 丢弃方向修正 ✅✅ 已完成
- 用户：「做一个投掷器，和发射器一样，只不过对所有物品都是直接投掷出掉落物。」
- 新方块 Dropper：pack 贴图 `block/dropper_front_horizontal.png` + `dropper_front_vertical.png`（正面）+ 熔炉侧面贴图（四个侧面）；UI 复用发射器 9 槽（DispenserStore 共用或平行 store）；压力板/触发同发射器；行为=全部物品弹掉落物（不做箭/雪球特殊分派）。放置朝向同 t608。合成配方（7 鹅卵石+? 参照 MC：7 圆石）。
  - 实现：Dropper=117（顶/底=furnace_top(12)、侧=furnace_side(13)、前=dropper_front(139) 新贴图 tools/build_dropper.py；tileFilenameMap {139→dropper_front_horizontal.png} 留 t620）；DispenserStore 共用（同坐标键控）+ DispenserUI 复用（titleText 按方块 id 显「发射器」/「投掷器」）；scanDispenserTraps 扩 isDropper 触发 → dispenseFromDispenser Dropper 分支全部物品 spawnItemAt 弹出（kDropperPopSpeed=4，无 fallback 箭）；配方 7 圆石（缺中心+上中）→ 1 投掷器；破块掉自身+9 槽内容（id===107||117）。
- 顺带 Q 丢弃修正：用户「按 Q 丢弃直接从鼠标指向处喷出且左右喷，应从玩家身体中间/视角摄像头往前丢」——查 dropHeld/dropHeldCursor 生成位置与初速度（spawnItem 的 spawn 位置应为眼位、速度沿 look 方向，不随机左右）。
  - 实现：throwItemInLook 统一原语（dropHeld/dropHeldStack/dropItemAtFront/dropHeldCursor/dropHeldCursorOne 五路径共用）= 眼位+视线×0.3 生成 + 初速视线×6（含俯仰 vy，ItemEntityManager::spawnItemThrown 三维定向）；死亡掉落保留 3×3 散布。

### 🅶 雪傀儡（1 项）

**t610** 雪傀儡受击红闪 + 南瓜脸贴图 ✅✅ 已完成
- 用户：「受伤的时候身体不会闪红（其他生物都会）。南瓜头没脸。」
- 红闪：Main.qml 雪傀儡段 tint 绑定在（~5470 hurtFlashAt>0 → 红），实测不闪——查 damageEntity 对 MobSnowGolem 是否走同一 hurtFlash 路径 / tint 乘 pack 贴图时 baseColor 未变红（pack 贴图在身时 tinted() 只乘 #f0f4f8，红闪时 tint=(1,0,0) 应把贴图乘红——核 baseColor 绑定链路）。
- 南瓜脸：现头用 BlockCube{blockId:100}（pumpkin_face_off=正面刻脸）应已有脸——用户说没有 → 核 -Z 面是否真采到 face 瓦片（tileFilenameMap 117/118/119 映射与 BlockCube 面序），或 pack 关时 default_pumpkin 正面没脸。**用户后续会给南瓜 PNG 链接**——届时替换。
- 实修结论：红闪真根因 = 材质里 `parent.tinted(...)` 的 parent 在 PrincipledMaterial 作用域解析到**外层 Model**（非持 tint/tinted 的 Node）→ 运行期 TypeError（log 实锤"Property 'tinted' of object QQuick3DModel is not a function"）→ baseColor undefined → 红闪/蓝调/昼夜灰阶全失效。修：两傀儡 Node 加显式 id（snowGolemRoot/ironGolemRoot），4 处 parent.tinted → id.tinted。南瓜脸根因 = demo 包 pumpkin_face_off.png 与 pumpkin_side.png **逐像素相同**（懒包复用）→ 图集 118 瓦片 == 侧面无刻脸；修：resourcepackmanager 图集合成对 tile 118 检测退化态（face_off == side）→ 回退候选链 carved_pumpkin.png → pumpkin_face_on.png（实测 carved 生效，log 有回退行）；pack 关路径 default_pumpkin_face.png 本带刻脸无需改。

### 🅷 船（1 项 3 子点）

**t611** 船碰岸可后退 + 坐船可放方块 + 冰面惯性加大 ✅✅ 已完成
- ① 撞岸停后要能倒退（后面是水）——现碰岸 vx/vz 清零后推进也不动？修：碰岸停只作用于「朝岸方向分量」，反向输入仍有效；
- ② 坐船时可以放方块（现被禁）——查 mount 时 placeBlock 门控，允许右键放（但下船交互保留）；
- ③ 冰面惯性再加大（iceSlipApproach 进一步调小/速度上限微升）。
- 实修结论：① t584 旧版「探到岸 → 双轴速度无条件清零」在贴岸后每帧把 lerp 刚建起的倒退速度清掉（清除在位移积分之前）→ 船永不位移 → footprint 永不脱离岸 → 死锁。修：四向（±X/±Z）分别前探 kShoreProbe=0.15 查 footprint 被挡，只清「朝岸方向分量」（v·n>0 部分），背向分量保留（可倒退）；高速撞毁仍整船。② 唯一骑乘门控 = placeBlock 船段的 `ridingIndex()<0`（骑船时持船右键凭空 no-op）→ 放行（坐船放船 = MC 1.0 行为；spawnBoat 不自动换骑，放归放骑归骑）；持方块右键本无骑乘门控（canPlace 仅观察者挡），瞄岸方块正常放置。③ iceSlipApproach 8/4.5/2.8 → 6/3.2/1.9（单一权威，玩家+船同调）；船冰档倍率 1.8/2.2/2.5 → 2.0/2.4/2.7（16~21.6 blocks/s）。

### 🅸 钟相位 + 月相（1 项 2 子点）

**t612** 钟动画反了 + 月相系统（仿 MC 8 相位）✅✅ 已完成
- 钟：用户「设时间 0 显示晚上、设 midnight 显示正午」——t585 锚假设 clock_32=正午错了，实测帧 0=正午 → anchor01 从 0.5 改 0（dayPhase 0=正午 → 帧 0）。核对整个环向（frame = round(dayPhase * N) mod N + 方向：dayPhase 增 → 帧号增或减，白天到夜晚的过渡帧序要对，若反了加 N- 取模）。
- 月相：现只有半边亮半月（terminator 直线模型单相）。仿 MC：8 相位（新月→娥眉→上弦→盈凸→满月→亏凸→下弦→残月），夜晚随机选相位（每世界随机/每晚推进一位——MC 是每天 +1，dayCount 已有）。tools/build_moon.py 生成 moon_0..7 已存在 8 张（t586 重生成过）→ 核月亮 UI 是不是只用了 moon_0；接 dayCount % 8 选相位。满月=完整方形亮面。
- 实修结论：① 钟锚逐帧像素取证（demo 包 clock_00..63 中心窗暖/蓝像素计数）：clock_00=太阳居中窗（正午）、clock_32=月亮居中窗（子夜）→ 帧号与 dayPhase 同向同零，t585 的「clock_32=全昼」系误读；锚按物品独立定（0x240 钟=0.0、0x23F 指南针保持 0.5），帧序方向天然正确（dayPhase 增=帧号增）无需 N- 反转。② 月相系统 t389/t586 已全链在位（WorldClock.moonPhase=dayCount%8 每天 +1、Main.qml moon_<phase>.png、qrc moon_0..7 全有）——用户看到的「半边亮」即第 2/6 天下/上弦相，系统正常工作非缺陷；MC 语义取每天 +1（首晚满月），非每世界随机。③ 勘误 build_moon.py/worldclock.h/Main.qml 旧注释的盈/亏半球命名（相位环正确、天文学名反了，贴图无需重生成）。

### 🅹 工具贴图细节（1 项 2 子点）

**t613** 铜工具图标多 1px 边 + 铜护甲套 ✅✅ 已完成
- ① 铜工具图标凸出 1 像素（t588 染色落盘时边缘多出）——retintCopperTemplate 输出前裁边/收缩 1px（alpha 阈值收缩）；
- ② 铜护甲四件（盔/甲/腿/靴）：从铁护甲 pack 贴图染铜色（同 t588 模式），armorId 段（0x300+tier*4+piece）铜 tier 的 pack 映射 + 染色缓存。查现铜 tier 护甲是纯色还是没做。
- 实修结论：① 像素取证推翻「alpha 边缘多 1px」假设——铁镐 vs 金镐 alpha 蒙版逐像素相同、染色不动 alpha、贴图零半透明像素（0<a<250 = 0），alpha 腐蚀/收缩只会切掉正确像素。真根因 = pack item 贴图惯例「外圈 1px 近黑描边」（铁头外圈 luma≈60、金镐同位 (54,54,32)≈53），旧铜梯度把它映射到 #a75e32（luma≈110 中亮铜橙）→ 亮橙外圈读作「工具胖一圈」。修：retintCopperTemplate 加**描边带**——铁灰阶像素 luma<90 走独立梯度（#3a2212 近黑铜棕 → 带顶衔接主梯度 luma=90 映射值，连续无台阶），外圈读作线而非本体；实测外圈 luma 61→95（旧 110）、本体 170→151，线/体分明且无亮晕。② 铜护甲 0x308..0x30B 入 itemFilenameMap（copper_helmet/chestplate/leggings/boots.png，现代包直用）+ copperIronFallback 加 4 行（iron_* 染铜，铁护甲整张灰白无木柄分区、外圈 luma≈29-35 由描边带保线）；显示端零改动（Inventory/SurvivalInventory 装备槽 + Main.qml 手持/掉落均走 MaterialIcon → itemIconSource，pack 图自动顶替自绘）。铜 tier 3D 身体色（armorBaseColor case 2 #c87850）本就在位不动。缓存文件 voxelsandbox_rp_copper_308..30b.png 实测生成，图标实测铜色 + 描边。

### 🅺 附魔书全套系统（设计先行，2 项大）

**t614** 附魔系统设计文档（dev-plan 内呈现，用户过目后才实现 t615）✅✅ 设计已按默认参数定稿并实现（用户四点回复已采纳，见下方「附魔系统设计」末尾）
- 写入本节下方「附魔系统设计（待过目）」：
  - **附魔书物品**：Book+青金石合成？MC 是附魔台附书。本项目：附魔台 UI 放**书**到槽 0（可附魔物）→ 三档选 → 产附魔书（随机 1-N 条附魔）。附魔书 itemId 新增 + 图标（pack enchanted_book.png 确认存在）。
  - **附魔书铁砧联动**：A=普通/已附魔工具护甲 + B=附魔书 → C=把书上附魔敲上去（合并等级：同附魔等级相加上限 / 已有等级取 max+1？MC 规则：同级合并 +1 级，异级取高）；附魔冲突（互斥组）→ 冲突附魔不上（显示红字或禁用）。
  - **适用规则表**（MC 1.0 附魔适用域 + 冲突组，逐条列）：
    - 锋利 Sharpness I-V：剑/斧 —— 与亡灵杀手/节肢克星互斥
    - 亡灵杀手 Smite I-V：剑/斧 —— 与锋利/节肢克星互斥
    - 节肢克星 Bane I-V：剑/斧 —— 与锋利/亡灵杀手互斥
    - 击退 Knockback I-II：剑
    - 火焰附加 Fire Aspect I-II：剑
    - 抢夺 Looting I-III：剑（本项目如无掉落加成机制可降级/略）
    - 效率 Efficiency I-V：镐/铲/斧/锄（工具）—— 与精准采集互斥
    - 精准采集 Silk Touch I：镐/铲/斧 —— 与效率/时运互斥
    - 时运 Fortune I-III：镐/铲 —— 与精准采集互斥
    - 耐久 Unbreaking I-III：所有工具+所有护甲（通用）
    - 保护 Protection I-IV：全护甲 —— 与火焰/摔落/弹射物保护互斥（同件不共存）
    - 火焰保护 Fire Prot I-IV：全护甲 —— 互斥同上
    - 摔落保护 Feather Falling I-IV：靴 —— 互斥同上
    - 弹射物保护 Proj Prot I-IV：全护甲 —— 互斥同上
    - 水下挖掘 Aqua Affinity：头盔（水下挖掘减速需要先做一下）
    - （本项目已有 EnchantRegistry 14 种——对照上表核现有适用域/冲突组实现，缺的补）
  - **铁砧冲突 UI**：B 放冲突附魔书 → C 产物显示冲突（红字提示「附魔冲突」+产物禁用/仍可合但丢冲突项？MC 是直接不冲突才可合——选定：冲突时产物仍可出但冲突附魔不写入，UI 红字提示哪条没上）。
- 用户过目后开 t615。

**t615** 附魔书实现（t614 设计过目后）✅✅ 已完成
- 附魔台附书 + 附魔书物品 + 铁砧敲附魔 + 冲突组全接线 + 附魔书进发射器可弹（t608 口径）。
- 实修结论：① EnchantRegistry 适用域 / 冲突组按 §3 表全接线（锐锋族扩到剑+斧、组 2 采集系 / 组 3 保护系互斥新补、摔落保护仅靴 / 水上亲和仅头盔走 isApplicableForItem 逐物品精判、BookItem 位 = 书全池随机）；② 附魔书 0x227 从占位升真物品（maxStack=1、enchants 元数据、pack enchanted_book.png 实测存在）；③ 附魔台槽 0 收书（Shift+左键整栈只取 1 本防丢）→ 三档附书产附魔书；④ 铁砧 merge 双分支（工具 A + 书 B / 书 A + 书 B 合并——冲突项 B 替换 A）、逐条适用过滤 + 冲突红字 + 等级合并 max/同 +1、消耗 = 写入条数 ×2 级、40 级上限「过于昂贵」（创造也不免）、B 槽书附魔随实例保真、tooltip 补附魔行；⑤ 发射器弹附魔书走 else 分支自然弹出（store 只存 id/count 附魔不保真，注释已知限制）。

### 🅻 图鉴/生物（1 项 5 子点）

**t616** 图鉴细节批 ✅✅ 已完成
- ① 滚动条与方块间距缩小（t591 改 8 列后间距过大）；
- ② 骷髅弓箭手手举着 → 放下（手臂自然下垂持弓姿：右臂持弓下垂/拉弓姿态只在瞄准时）+ 给他拿上弓（Bow Model 或贴图手臂持弓——最简：主手挂弓形 Model）；
- ③ 潜行者（苦力怕）矮 → 加高到 2 格视觉（MC creeper 1.7 格高——用户要 2 格观感：头+身+腿总高拉到 ~1.8-2.0）；
- ④ 苦力怕爆炸前演出升级：不只是放大——像 TNT 白闪一闪一闪 + 体型增长 + 颤抖 + 撕嘶声（fuse 音）——查 aiStalker 蓄力段加闪白（材质 emissive/baseColor 白脉冲）+ 微抖（position 抖动）+ 音效（若音效系统有 fuse 类 SFX 可复用，没有就先视觉两项）；
- ⑤ 鸡腿应是细黄腿（现毛绒）：chicken 贴图腿区或纯色细黄腿 box（#e8c53a 细 0.08 见方）。
- 实修结论：① cellSize 42→44（内容 364→380，滚动条不遮且间距 ~34→12px）。② mobmodel Bones 双臂改自然下垂（竖直骨杆臂盒），弓移 Main.qml 垂手位（肩枢 (0.24,-0.30,-0.02)，drawAmount×75° 瞄准抬起）+ 图鉴预览补静态 MobBowGeometry（木褐）。③ Stalker 三段整体 y 拉伸 ~1.21（MC 比例观感）→ 总高 1.57→1.705 ≈ MC 1.7 格，眼位随头上移 0.52→0.64、图鉴 centY 0.12→0.05。④ Stalker delegate 加 t494 PrimedTnt 式白闪脉冲（stalkerFlashPhase 循环动画，duration 随蓄力 500→140ms 加速，亮端 sin>0.5 拉纯白，pack 贴图路径同样拉白 tint）+ position 高频 sin 颤抖 ×inflate（Date.now 驱动双轴异频）+ fuse 点燃嘶声（aiStalker fuseTimer 0→正 沿 emit stalkerFuseLit 一次 → Main.qml 路由 playMobAmbient(6) 复用 mob_idle_stalker 嘶声；音效系统无独立 fuse SFX）。⑤ 鸡腿从 MobModel 几何移除（单材质无法双色，t598 共用 body texOffs 采毛绒区是根因）→ Main.qml 补独立纯色细黄腿 #e8c53a 粗 0.06 绕髋 walkPhase 摆动 + 图鉴预览同补静态双腿。

### 🅼 资源查看器交互（1 项 3 子点）

**t617** 拖拽松手跳变 + hover 悬浮窗 + ESC 关闭顺序 ✅✅ 已完成
- ① 拖拽松手「跳变」不舒服 → 松手后按当前拖拽姿态缓动回自转（现 pitch 直接弹回/自转角跳变）——松手时 spinAngle 从当前值续转（已做）+ pitch 缓动（已做 400ms）→ 跳变在别处：核松手瞬间 spinAngle NumberAnimation restart 是否 from 当前值；修成 from=当前 spinAngle。
- ② 删底部提示文字 → 名字用悬浮窗（hover 显示名+描述，同创造背包 tooltip 模式）；
- ③ ESC 先关后面的设置界面再关资源查看器（顺序反了）→ 核 ResourceBrowser 的 ESC 处理（现按 ESC 关闭 appSettings?）——资源查看器打开时应最先吃掉 ESC（叠层最上层优先）。
- 实修结论：① 根因 = `NumberAnimation on spinAngle { from: 0 }` running 绑定重启时 from 恒 0 → 拖到任意角松手瞬间跳回 0；改独立 spinAnim（target/property 显式）+ restartSpinIfIdle() 统一入口（previewDragging/visible/selectedIsCube/selectedIsMob 四源共用），start 前 from=当前 spinAngle、to=from+360 同向续转。② 底部提示条删除；hoveredName+hoveredId+hoveredTipPos（格顶中心 mapToItem(panel)）驱动 hoverTip 黑框（名 · 类别简述，hoveredCategory 谓词 / mob 段「生物」，Inventory t94 itemTip 同模式含边界钳制+顶部不足翻下）。③ resourceBrowserOpen 的 Esc 分支上移到 Keys.onPressed 全部分支之前（叠层 z=160 最上层优先；原排在 settingsOpen 后 → 先关设置 = 顺序反）。

### 🅽 F3+B 朝向线（1 项）

**t618** F3+B 朝向线位置与方向修正 ✅✅ 已完成
- 用户：「背包 3D 人物的朝向线在脚底下——应在头上（像二三人称视角看实体那样从头上/身体中心）；而且箭头上下颠倒了（第三人称发现的）。」
- 修：① CharacterPreview3D 朝向线从实体中心/头部高度出（t602 把所有实体朝向线画在中心——背包预览模型原点/脚底偏移导致线在脚底 → 线 y 提到模型心高）；② 上下颠倒 = pitch/Y 方向反 → 取反（全局实体朝向线也核一遍第三人称玩家箭头朝向是否和视线一致）。
- 实修结论：① CharacterPreview3D 棒 y=0（modelRoot 原点=脚底）→ 提到眼高 y=1.62（同 Main.qml F3+B 玩家线 feet+1.62；MC F3+B 实体线即从眼线伸出）。② 方向核验**不取反**：全局玩家线 yaw-only (0,yaw,0)×本地 -Z = (-sin yaw,0,-cos yaw) = PlayerController::lookDirection 水平前向（同源 Ry(yaw) 四元数，模型身体/头/线同 yaw）→ 第三人称箭头与视线水平分量恒同向；「上下颠倒」观感实为背包预览棒在脚底 y=0 + 相机俯角透视的假象（棒贴地从脚伸出向后上投影），提棒到眼高即消除（两症状同根，①修即②愈）。核验结论记录进 Main.qml 玩家线注释（t618 段）。

### 🅾 成就树状图（1 项大）

**t619** 进度界面重做成树状图 ✅✅ 已完成
- 用户：「不满意，想要树状图，从左到右排布，连线横平竖直，界面可上下左右拖动看不同成就连线。」
- 查现 progress UI（list/grid）→ 重做：成就节点按依赖层级分列（x=层级，y=同层内序），正交折线连线（ elbows）；Flickable 拖动查看；节点状态（未解锁灰/已解锁亮/可进行描边）；新增成就：盘点现有物品/机制再补 8-12 条（如「钻石！」「附魔师」（首次附魔）「船上漂」「发射！」「铁匠」（铁砧修复）「矿工」（挖矿 N 块）「农夫」（收获 N 作物）「狙击手」（箭命中 N 生物）等——实现时按 playerprogress 现有统计字段可支持的来，缺统计的加计数点）。
- 实修结论：① 树状图 = achievements() 携 col/row/iconId 布局字段（C++ 递归子树布局：叶子占 1 行、父居中子女跨度、多根垂直堆叠，JS 模拟验证零行列冲突）→ QML 节点 x=kPad+col×200、y=kPad+row×110，连线 = 父右中点→列间垂直→子左中点三段正交折线（Repeater+Rectangle 拼段，横平竖直），Flickable 上下左右拖（content = 树边界+边距，760×560 面板容 ~1400×770 画布）；节点三态（已解锁绿框✓ / 可进行黄框脉动○ / locked 暗底🔒）+ iconId 三段路由图标（方块 Image/ToolIcon/MaterialIcon）+ hover tooltip 全文。② 新成就 8 条：钻石!(←获得升级)/附魔师(←钻石!)/书虫(←附魔师)/铁匠(←附魔师)/神射手10箭(←怪物猎人)/农夫10作物(根)/起航骑船(根)/发射!(根)——共 15 条 3 根树。③ 新计数点：arrowsHitMobs（Main.qml onArrowHitMob 路由）、cropsHarvested（player.cropHarvested 信号 ← dropCropDrops 成熟判定单点）、onBoatBoarded（ridingBoat 边沿）、player.dispenserFired 信号（dispenseFromDispenser 库存路径末尾）、onEnchanted/onEnchantedBookObtained（doEnchant 末尾，EnchantingTableUI 注入 progress）、onAnvilUsed（takeProduct 末尾，AnvilUI 注入 progress）；两计数进 toVariant/loadVariant 持久化 + 读档回放阈值判定。④ 统计面板补「箭中生物/收获作物」两行。⑤ 顺带修 playercontroller.cpp 既有 eyeY 未用警告（-Wall -Wextra 口径）。

### 🅿 pack 方块贴图接入批（2 项大，用户已给全部 PNG 路径）

**t620** 功能方块贴图接入（投掷器/发射器/附魔台/末影祭坛/门/书架/南瓜/铁轨族/红石族/仙人掌/耕地/作物/矿物块）**〔全部完成 ✅✅（第 1 部分：功能方块组 e260b2d+eb23c7e——发射器/投掷器/附魔台/末影祭坛/门/书架/南瓜/铁轨族；第 2 部分：矿物块+红石灯+补漏——新增五矿物存储块 CoalBlock=118/LapisBlock=119/DiamondBlock=120/GoldBlock=121/RedstoneBlock=122（9 材料↔1 块双向配方 + 煤炭块燃料 800s + 采掘级对齐对应矿物镐门槛 + 创造调色板 + 程序贴图 default_*_block 147..151 + pack 映射 + 图标）、红石灯 RedstoneLamp=123（右键开关 state bit0：on=redstone_lamp_on 贴图+方块光 15 走 lightEmission 状态感知版重 flood，同 t494 熔炉模式；配方 4 红石+1 玻璃十字围心；贴图 152/153）、iron_block pack 映射漏项补齐（tile 112→iron_block.png，t477 遗漏 grep 实证）、仙人掌底面核实不接（0.8 细柱 pushBox 侧·底统一 sideTile，bottomTile 无消费方；cactus_bottom 与 top 像素实测不同但无渲染路径读它）、红石火把/动力轨/探测轨留注释不接（无红石系统无消费方）。AtlasTileCount 147→154；Count 118→124）〕**
- pack 路径（docs/Default HD 128x Demo 1.8.2.2/assets/minecraft/textures/block/，只读运行期引用）：
  - 投掷器：`dropper_front_horizontal.png` + `dropper_front_vertical.png`（正面，随放置朝向选横/竖）+ 熔炉侧面（四个侧面）→ **依赖 t609 新方块**
  - 发射器：`dispenser_front_horizontal.png` + `dispenser_front_vertical.png` + 熔炉侧面 → 依赖 t608 朝向 state
  - 附魔台：`enchanting_table_top.png`（顶）+ `enchanting_table_side.png`（侧，已含上方 0.25 空白）+ `enchanting_table_bottom.png`（底）——**附魔台 0.75 格高**（非整块）模型改半高 + 侧贴图上 0.25 空白正好对齐
  - 末影祭坛（末地传送门框）：`endframe_side.png` + `endframe_top.png`；放末影之眼后顶面换 `endframe_eye.png`
  - 木门：`door_wood_upper.png` + `door_wood_lower.png`（橡木门上下半）；云杉门 `door_spruce_upper/lower.png` + `door_spruce.png`（云杉门 item 图标）
  - 书架：`bookshelf.png`（侧）+ 橡木板贴图（顶/底）
  - 南瓜：`pumpkin_face_off.png` + `pumpkin_side.png`（侧）+ `pumpkin_face_on.png`（雕刻点亮?）+ `pumpkin_top.png`（顶）
  - 铁轨族：`rail_normal.png`（直）+ `rail_normal_turned.png`（转弯，向右转）+ `rail_golden.png` / `rail_golden_powered.png`（动力轨未/已激活）+ `rail_detector.png` / `rail_detector_powered.png`（探测轨）；**仅普通轨可转弯**；`powered_rail.png`（动力轨 item）
  - 红石族：`redstone_block.png`（红石块六面同）+ `redstone_torch_on/off.png`（红石火把燃/熄）+ `redstone_torch.png`（item）+ `redstone_lamp_off/on.png`（红石灯未/已激活——**激活发光有光照等级**）+ `redstone_lamp.png`（item，2D 需处理成 3D 形态）
  - 仙人掌：`cactus_bottom/side/top.png`（底面只为掉落物 3D 完整）
  - 耕地：`farmland_dry/wet.png`（干/湿，锄头右键产物——**核锄头耕地是否已实现**，没实现则加：锄右键草/土 → farmland 干态；邻水 → 湿态）
  - 胡萝卜：`carrots_stage_0..3.png`（4 阶段侧图 + `carrots.png.mcmeta` 元数据）——**核胡萝卜种植是否已实现**
  - 马铃薯：`potatoes_stage_0..3.png`（4 阶段）
  - 矿物块：`coal_block/iron_block/gold_block/redstone_block/lapis_block/diamond_block.png`（六面同）——核哪些已接哪些缺
- 实现：tileFilenameMap / blockTexture 映射逐个接（resourcepackmanager），非整块几何（附魔台 0.75 / 门上下半 / 台阶楼梯已各自有）按几何裁 UV。**本项工作量大，实现时按上面分组拆 3-4 个 commit。**

**t621** 胡萝卜/马铃薯种植体系（若 t620 核出未实现）✅✅ 已完成（**早期轮已实现，本轮核验确认**——耕地 Farmland=23（t234 锄头 useBlock + t408 干湿 4 级 state 低 2 位 + tickFarmlandHydration 周期复算 + tiles 26/27→farmland.png/farmland_moist.png pack 映射在位）与胡萝卜/马铃薯作物 CarrotCrop=55/PotatoCrop=56（t407 种植/生长/收割全链 + tiles 69-76→carrots/potatoes_stage_0..3.png pack 映射在位）均已实现；t620 第 2 轮核验（grep playercontroller 锄头分支/tickCropGrowth/tileFilenameMap 映射实证）全在，无缺口）
- 耕地（farmland）方块 + 锄头右键转耕地 + 邻水湿润 + 干湿贴图切换；胡萝卜/马铃薯种子物品 + 右键种耕地 + stage 0-3 生长 + 成熟收割掉落（胡萝卜/马铃薯物品 + 种子）。查现有小麦体系（WheatCrop 已有）照搬模式。合成/掉落/饥饿值对齐 MC 1.0（胡萝卜+3 饥饿 马铃薯需烤）。

### 📎 R19.5 范围
t605-t621（17 项：相机 1 + 铁砧 1 + 发射器/投掷器/丢弃 3 + 雪傀儡 1 + 船 1 + 钟月 1 + 工具 1 + **附魔系统设计+实现 2** + 图鉴 1 + 资源查看器 1 + F3 1 + 成就树 1 + pack 贴图 2）。**t614 附魔设计先呈现用户过目，t615 待批后做。**全部本轮做完不准拖（t615 除外，看过目结果）。

---

## 📖 附魔系统设计（t614，已过目定稿 → t615 已实现）

> 现状底子：EnchantRegistry（t475，14 种附魔 + 适用域 + 加权随机）、ItemStack enchants 元数据（槽结构 {id,count,durability,enchants}）、效果接线（t476 五计算点）、附魔台 UI 三档（t549/t590：消耗等级+青金石、书架加成、真随机）、附魔可见性（t590：紫光晕+tooltip）。本设计补齐**附魔书**全链路。

### 1. 附魔书物品
- **获取**：附魔台 UI 槽 0 放**书**（Book 物品，皮革+3 纸合成已有？核）→ 书视为可附魔物 → 三档选档 → 消耗等级+青金石 → 产**附魔书**（1 本书随机获 1-3 条附魔，档位越高条数/等级越高）。
- **属性**：附魔书是「附魔载体」：enchants 元数据存书携带的附魔列表；自身无耐久不可穿不可用；堆叠 maxStack=1。
- **图标**：pack `item/enchanted_book.png`（确认存在）；紫光晕同附魔物品。

### 2. 铁砧敲附魔（A 工具 + B 附魔书 → C）
- **规则**（机制等价 MC 1.0）：
  - C 继承 A 的全部属性（耐久/已有附魔/自定义名）；
  - 书上每条附魔尝试写入 C：
    - **适用性**：附魔适用 A 的物品类型（剑类附魔不上镐）——不适用条目**不上**（UI 该条灰显）；
    - **冲突组**：与 C 已有附魔互斥（见 §3）——冲突条目不上（UI 红字「冲突」）；
    - **等级合并**：C 已有同款等级 Lc、书等级 Lb → 新等级 = max(Lc, Lb)，若 Lc==Lb → Lc+1（封顶该附魔 max 级）；C 无该款 → 直接写入 Lb；
  - **消耗**：经验等级 = 书上「成功写入的条目数 × 2」（改名再 +1）；创造模式免（t606③）；
  - B 消耗 1 本附魔书；C 产出后 B 槽清空。
- **两本附魔书合并**：A=附魔书 + B=附魔书 → C=合并后的附魔书（同上合并规则），消耗 2 级。方便把散附魔攒成一本高级书。

### 3. 附魔适用域 + 冲突组总表（EnchantRegistry 现有 14 种对照）

| 附魔 | max | 适用 | 冲突组 |
|---|---|---|---|
| 锋利 | V | 剑·斧 | A（伤害系互斥） |
| 亡灵杀手 | V | 剑·斧 | A |
| 节肢克星 | V | 剑·斧 | A |
| 击退 | II | 剑 | — |
| 火焰附加 | II | 剑 | — |
| 效率 | V | 镐·铲·斧·锄 | B（采集系互斥） |
| 精准采集 | I | 镐·铲·斧 | B |
| 时运 | III | 镐·铲 | B |
| 耐久 | III | **全部工具+全部护甲**（通用） | — |
| 保护 | IV | 全护甲 | C（保护系互斥） |
| 火焰保护 | IV | 全护甲 | C |
| 摔落保护 | IV | **靴** | C |
| 弹射物保护 | IV | 全护甲 | C |
| 水下挖掘 | I | 头盔 | — |

- 冲突组 A/B/C：同组附魔**不能共存于同一物品**（铁砧敲时冲突条不上 + 附魔台随机池排除已冲突）。
- 实现核对点：EnchantRegistry 现有 applicable 域是否与此表一致（t475 时剑类附魔可能只给了剑）；耐久确认全物品通用。

### 4. 附魔台可附物品域
- 工具（镐/铲/斧/锄/剑 全材质）+ 护甲四件（全材质）+ **书**（新增）。
- 附魔台随机池按物品域筛（书 = 全池随机，MC 语义）。

### 5. UI 呈现
- 铁砧 B 槽接受：修复材料 / 同 id 物品 / **附魔书**（三通道）；B=附魔书时等级行显示「附魔 ×N · 消耗 M 级」；冲突/不适条目在等级行下方红字列出。
- 附魔书 tooltip：紫字列出携带附魔（同 t590 物品 tooltip）。

> **待用户过目点**：① 敲附魔消耗公式（条数×2 级）可否；② 冲突时「产物仍可出但冲突条不上+红字提示」还是「直接禁止合成」；③ 两本附魔书合并要不要；④ 附魔台附书是否消耗青金石（MC 是，本项目随现有槽 1 青金石逻辑）。
用户回复：①是的，需要有敲附魔书的等级惩罚，而且有最大上限，如果超过最大上限将显示过于昂贵几个字。②要的，两本附魔书只要是不冲突的附魔就可以合并，如果冲突，再铁砧界面里A+B=C中用B的替代A的生成C。③需要消耗青金石，如果青金石数量不足，即使书架足够，也不能进行对应的附魔，显示不可点击状态，ⅠⅡⅢ等级附魔分别需要一二三个青金石。

---

## ⚠️⚠️ R19.6 复盘（2026-08-16 用户验证 R19.5 后，HEAD ba4b380）

> **背景**：R19.5 全 17 项落地。用户全量 playtest → 相机穿墙✅/发射器投掷器✅/钟相位✅/工具铜✅/成就树✅/方块贴图大部分✅；但 **物品槽结构缺 customName 字段**（铁砧改名/附魔台附魔取出即丢）为多项 bug 共同根因，另有大量细节。**本轮 t622-t657（36 项）。**

### 🅰 物品数据模型重构（1 大项，本轮核心）

**t622** ItemStack 加 customName 字段 + 全链透传（用户点名的设计）✅✅ 已完成（commit 见 git log；含 t623 根因修复 setHeldEnchants 补挂 Q_INVOKABLE）
- 用户：「重命名完全没有用——改名放回背包还是旧名。每个 item 类应该有个变量存铁砧改的名。Item 是基类，工具类继承它加耐久，附魔当子对象嵌进去，name 在基类。」
- 现状核实：`Hotbar::ItemStack {id,count,durability,enchants,customName}` —— **customName 在 hotbar 槽已有**（customNameAt/writeCustomName），但：① InventoryOps.js 槽结构 `{id,count,durability,enchants}` **无 name**——本地槽（铁砧/附魔台/craft）经 InventoryOps 搬运时丢名；② 附魔台/铁砧产物写回时没带名；③ 掉落物实体（ItemEntity）无 name 字段——丢出即丢名。
- 修：InventoryOps readSlot/writeSlot/localWriteSlot 全家加 name 透传（同 dc16ca2 dur/ench 模式）；铁砧产物（takeProduct）带新名写入；附魔台槽 0 取出保名；ItemEntity/spawnItem 加 name 形参（默认空）+ QML 掉落物 tooltip；存档序列化补 name（查存档槽序列化格式——versioned 升级兼容旧档无名=默认）。
- ⚠️ 用户同时点名的「附魔当子对象」——现 enchants[4] packed int 已是等价实现（数据+查询+效果接线全通），不重构为类（存档兼容 + 风险），在返回中说明。

**t623** 附魔台/铁砧产物属性保真（enchants 取出即丢 bug）✅✅ 已完成（commit 见 git log；根因=setHeldEnchants 未挂 Q_INVOKABLE 致 QML 调用静默 TypeError，t622 一并修复；存档 enchants 落盘在 t622 gatherPlayerState v3 补全）
- 用户：「附魔台附出锐锋1的附魔书，左键拿出来瞬间就变成普通附魔书；附魔后的工具拿出来也丢附魔。附魔应存进存档。」
- 现状核实：doEnchant 写槽 0 的 enchants（t549）——但**取出路径**（左键拿取/shift 拿取→背包）经 InventoryOps 或本地槽数组快照，enchants 没透传（同 t622 名字丢失同根因）。修：附魔台/铁砧两 UI 的槽取放全链带 enchants（AnvilUI 已有部分——canMerge 写产物 enchants?核取出路径）；存档：hotbar 槽序列化已含 enchants（t475 做过？核）——补全。
- 附魔持久视觉：取出后紫光晕随物品走（t590 已做 hotbar/手持——核附魔台/铁砧槽内也显）。

### 🅱 创造模式生存 tab（2 项）

**t624** 创造背包生存 tab 2×2 合成格接真合成 ✅✅ 已完成（commit fd66a4d）
- 用户：「创造模式背包的生存物品栏 tab 合成栏用不了；左键批量均分也没了。生存模式切过去一切正常。」
- 现状核实：Inventory.qml tab6 的 2×2 格是**纯占位**（craftSlots 0 命中，t528 注释「合成可占位」）。修：照 SurvivalInventory 的 craft 模式搬过来（craftSlots 本地组 + matchRecipe 检测 + 结果槽 + shift 批量 + 均分/快捷操作参与——InventoryOps 组参与表加 craft）。创造模式合成消耗照生存逻辑（创造拿调色板物放入合成格也正常运作；产物取出不消耗源?——MC 创造背包合成格也是真合成，按生存同款做，注释说明）。

**t625** 左键拖动均分在创造生存 tab 生效 ✅✅ 已完成（commit 2107fd5）
- 同 t624：均分目标组判定（InventoryOps t180 判定表）在 Inventory tab6 面板把 main/hotbar/craft 都纳入。

### 🅲 铁砧五轮细节（1 项多子点）

**t626** 铁砧 UI 批（6 子点）✅✅ 已完成（commit 见 git log；单 commit 自含 dev-plan 故不能内嵌自身 hash，同 t622 写法）
- ① 整体高度压缩：面板/槽行整体再下移+压高（用户「整个 UI 偏高」，宽 OK）；
- ② 改名产物拿取方式：左键取产物 → 到**光标**（held cursor，同普通槽拿取），不是 addToAny 直接入包（现相当于 shift 效果）；
- ③ 改名后输入框退出输入态（focus=false）：按 E 关面板/按 1-9 切槽不再输入进框；只有再点击框才重新聚焦；
- ④ 名字显示与耐久度文字区分（tooltip 里名字行独立配色/位置）；
- ⑤ A 工具 + B 附魔书 C 无限复制 bug：C 可无限取出不消耗（t615 canMerge 产物槽取出逻辑没清 B/A？核 takeProduct merge 分支——r195 review 提过 40 级不可达，本 bug 是**产物槽可重复取**——取后必须清 A+B 槽 + 结果槽）；
- ⑥ 附魔书显示等级：附魔书图标/tooltip 显示携带附魔+等级（t590 enchantListText 已做——核铁砧/附魔台槽内 tooltip 接上）。

### 🅳 压力板/机关触发（2 项）

**t627** 压力板触发语义重做（边沿触发 + 踩下动画 + 家族扩展）✅✅ 已完成（commit 49bbebb）
- 用户：「一直踩着压力板往里放东西会一直喷——应踩一次触发一次，走开回位，下次再踩再触发。踩下时压力板要变矮（被压下去）。还要出石头压力板、铁压力板、金压力板：铁/金要重物（掉落物?玩家?）才能触发，压力板可以被丢过来的掉落物触发（红石系统前置）。」
- 修：① scanDispenserTraps 压力板检测改**边沿触发**（踩下沿 fire 一次；持续踩着不重复；离开重置 armed）；② 踩下视觉：压力板 Model y 压低（state bit 或呈现层直接检测玩家在板上→scale.y 压 0.5 + y 下沉）；③ 新方块 StonePressurePlate/IronPressurePlate/GoldPressurePlate（id 顺延；贴图 pack `stone_pressure_plate?`——查 pack item/block 有没有 pressure_plate 族 PNG，没有程序生成同款不同色）；④ 触发权重：木/石=玩家+mob+掉落物触发；铁=仅玩家（重）；金=仅掉落物（轻，MC 语义金=物品重量、铁=玩家级——照 MC：金压力板掉落物即可触发、铁需玩家/mob）；⑤ 掉落物触发：ItemEntityManager 掉落物落格==压力板 → 触发（轻量：pickupScan 时顺带查或 tick 扫板格）。

**t628** 拉杆/按钮接发射器触发 + 压力板族合成 ✅✅ 已完成（commit 见 git log；单 commit 自含 dev-plan 故不能内嵌自身 hash，同 t626 写法）
- 用户：「拉杆跟按钮很久之前叫你做了（已有 Lever=112/WoodButton=113/StoneButton=114 但只接 TNT 点火）。按钮触发一次自动恢复；拉杆拉开持续激活。以后大红石系统，这是前置。」
- 修：Lever/双 Button 的激活（右键扳/按）→ 除点燃 TNT 外，同时触发邻接发射器/投掷器一次（按钮）或持续（拉杆 on 态邻接机器持续允许+边沿 fire 一次——简化：拉杆扳上沿 fire 一次，扳下沿不 fire；对齐压力板边沿语义）；拉杆贴图程序生成（原石+木棍配方已有?核）+ 按钮 Wood/Stone 分材质贴图微调。

### 🅴 雪傀儡（1 项）

**t629** 雪傀儡三修（积雪层错位/悬空卡方块/易死根因 + 剪头露雪头）✅✅ 已完成（commit 6334d39）
- 用户：「积雪层生成有点偏——应只在离它最近的一格持续生成；打它时有概率卡在空中悬浮在积雪层上一格；它特别容易死——白天阴凉处也一直扣血，是被卡死的吗？剪刀剪了南瓜头应露出里面的雪头（不是没头）。」
- 核实现状：留雪逻辑「放身后格」（entitymanager ~2008）——用户说偏：改「放脚下相邻最近空格」或修正身后向量计算；悬空+易死同根因疑：SnowLayer 铺进 golem 碰撞格 → golem 被托起/卡 → 窒息扣血（mobFeetInWater/窒息判定查 mob 卡方块扣血路径——与玩家 t575 眼位 sub-AABB 判定对齐修 mob 侧）；融化判定（hotBiome/rain/inWater）本身对——「阴凉处扣血」应是卡方块伤害不是太阳。剪头后：golemSheared 态显示**雪块头**（白色方块头替代南瓜头，非无头）。
- 实修：① 留雪改「放脚下最近格」（footprint 覆盖格中离中心 XZ 最近的一格，跨格取最近 = 用户「最近的一格」）；② mob 落地扫描改 mobSupportTopY 真顶承接（SnowLayer 按 snowLayerHeight 1/8..1.0 取薄层真顶，满格方块取 cell+1）→ 修「悬空在积雪层上一格」（旧恒按满格顶承接）+ resting 复探改 feet 所在格及其下一格两格复探（薄层顶非整数时旧公式查到层下空气格 → 周期振荡）；③ mobAabbHitsSolid 薄雪层视穿透（水平碰撞豁免 SnowLayer，防雪原/自铺脚印把 mob 围死）；④ mob 窒息判据收紧「头部格 collidable」→「头部点落入该格某 sub-AABB」（对齐玩家 t575 修法，薄层整格误判窒息 = 「阴凉处持续扣血」真因）；⑤ 剪头 delegate 显雪块头（BlockCube 101 雪块瓦片，同南瓜头 0.50 几何/1.14 头位）+ 眼嘴刻面贴雪头前，替代旧无头形态。

### 🅷 船（1 项）

**t630** 船岸沿掉落阈值 2/3 + 撞荷叶 + 身体推船旋转 ✅✅ 已完成（commit 71bfb65；t643 死亡后船卡水为同一支撑判定 bug，随本修复消解——verified-by-fix）
- 用户：「船从岸上往水里走下不去（一半卡水里一半卡方块——掉落触发太早）：船身 2/3 过去了再掉，1/3 还在岸上时不掉，就不会被卡住。船应能撞碎荷叶（速度够大撞成掉落物）。人撞船应有旋转效果（不只平移）。」
- 修：① 船「有支撑」判定从 1/2 支撑改 2/3 支撑才不掉（boatmanager 支撑格采样权重）；② 船 footprint 碰荷叶（LilyPad）且速度>阈值 → 荷叶破掉掉落物（参照冰碎/雪层塌机制）；③ 玩家推船：推力加**扭矩**——推力作用点=碰撞点（玩家相对船心方向），船 yaw += 横向分量×系数（简化：玩家在船侧推 → yaw 偏转）。
- 实修：① 新 boatFootprintWaterFraction（footprint 覆盖格水柱占比采样）——中心列有水**且**覆盖 ≥ kBoatWaterFraction(0.67) 才判「浮在水里」（tick 空船 + tickRiddenBoat 骑乘两路同门控）；< 2/3 走陆档重力贴支撑面 → 岸沿驶入不落水岸夹缝（旧版 waterSurfaceY 只看中心列 = 根因：中心一入水即钉水面把压岸半船拽沉嵌岸块）。② smashLilyPads：速度 > kBoatLilySmashSpeed(3.0) 时扫 footprint 两层格清 LilyPad（setWaterSilent 静默）+ emit lilyPadSmashed → 呈层 spawnItem 掉睡莲（Main.qml onLilyPadSmashed）；撞碎先于位移碰撞 → 高速碾过不停船、低速叶仍挡（绕行）。③ 推船扭矩：力臂 = 玩家接触点相对船心 (−dpx,−dpz)，2D 叉积（力臂×推开量）× kBoatPushTurnRate(1200) → yawRate，钳 ±kBoatPushTurnMax(25°/s)，yaw 归一 [0,360)；对心推叉积≈0 纯平移不转（力矩物理直觉），偏侧推船头慢偏转。

### 🅸 月相刷新（1 项小）

**t631** time set 命令推进月相 ✅✅ 已完成（commit 38f925f）
- 用户：「time set midnight 时月亮应刷新月相（相当于又过了一天）——方便测试不同月相。」
- 修：time set 命令处理器（聊天命令 timeSet）调 WorldClock setTime 时 dayCount+1（或按时间跳变量取整天数）→ 月相跟着走。
- 实修：WorldClock::setPhase（/time set day|night|midnight|数字 共用入口）内 dayCount+1 后走 applyTime —— dayCount 是月相单一真值源（moonPhase=day%8），跨阶即 emit moonPhaseChanged → QML 月 Model 切 moon_<phase>.png；连续 set 8 次轮回一整圈（测试月相友好）。setDay（数字d）/addPhase 语义不变（setDay 本身直接设天；addPhase 跨天自动推进）。命令回显附「月相刷新」提示。

### 🅹 附魔书创造调色板分种（1 项）

**t632** 创造背包每种附魔各一本附魔书（14 本） ✅✅ 已完成（commit 0ccadb7）
- 用户：「创造模式附魔书什么都没写——应该每一种附魔都有一种附魔书。这样我才能测铁砧冲突。」
- 修：creativeItems 加 14 本「附魔书·锐锋」等（id 同 EnchantedBookId 但预设 enchants——调色板条目结构需支持带 enchants 的预设物品；取出时带附魔入槽）。附魔书 tooltip 显附魔（t590 已有）。

### 🅻 图鉴（1 项 3 子点）

**t633** 图鉴细节：hover 名字空白 + 生物 2D 头像图标 + 羊全白/潜行者头歪 90° ✅✅ 已完成（commit 3b4169d）
- ① hover 悬浮窗显示物品/生物名全空白（t617 hoverTip 名字源没接对——查 ResourceBrowser hoverTip 的 name 取值）；
- ② 生物列表图标（现纯色白/绿）：从 pack 实体贴图**裁头部区域**生成 2D 头像（程序：加载 entity PNG，按 mobmodel 头部 UV 区裁剪缩放 → 缓存 PNG 显示；无 pack 回落现 3D 预览缩略）；
- ③ 羊全白无眼（t593 换 fur 后脸没了——fur 层头前是纯白羊毛 → 叠加程序眼睛但现没显：核羊 delegate 眼睛 gate）；潜行者头歪 90°（t595/t616 比例调整后头 box 旋向错——核 mobmodel Stalker 头 UV 面向）。

### 🅼 末地祭坛方块 + 铁傀儡（2 项）

**t634** 末地祭坛方块进创造背包 ✅✅ 已完成（commit d937cbc）
- 用户：「末地传送门的框架在创造背包没找到。」
- 修：EndPortal（endframe 化 t620）进创造调色板（方块 tab）——核对 t620 后该方块是否可放置/可破坏正常。

**t635** 铁傀儡真头 + 攻击动画 ✅✅ 已完成（commit 1ab0e16）
- 用户：「游戏内铁傀儡头不是南瓜头也不对——贴图包里有它自己的头（生物查看器里那个南瓜头也是错的）。铁傀儡要做攻击动画：玩家打它 → 它双手往前一抬把玩家打飞 4 格以上（摔落伤害）。」
- 修：① 铁傀儡头 = pack iron_golem 贴图头部区（mobmodel 头 box UV 重算——t598 用全图枚举法重定位过 body/leg/arm，头也照做）；图鉴预览同步（t598 补的头是 BlockCube 南瓜——改成贴图头）；② 攻击：铁傀儡被玩家攻击 → 锁定玩家为目标（aiIronGolem 有敌对分支?核）→ 近距双臂上抬动画（walkPhase/attackPose 驱动）→ 命中 = 大伤害 + 玩家 vy 上抛（launch：玩家速度 y=+12 落地 4+ 格摔伤）。

### 🅽 F3+B 朝向线（1 项小）

**t636** 朝向线跟头部俯仰（垂直面部） ✅✅ 已完成（commit 38adb0b）
- 用户：「背包 3D 人物的朝向线应跟鼠标上下移动（垂直于面部）；第三人称玩家线也应跟头 pitch 而不是恒水平。」
- 修：CharacterPreview3D 朝向线加 pitch 分量（线方向 = headPitch 旋转后的前向）；第三人称玩家线同（读 player.pitch）。

### 🅾 成就树交互（1 项）

**t637** 成就树：分支收缩/展开 + 滚轮缩放 + 布局重排 ✅✅ 已完成（commit 3feb0ee）
- 用户：「树状图做得很完美，但要能收缩：点合成台分叉的按钮把后面一串收起来（思维导图式）；解锁到哪自动展开到下一层分叉；鼠标滚轮缩放；移动靠拖拽。」
- 修：① 每个有子节点的成就节点加 +/− 收缩钮（收起子树——布局 row 重排或子树隐藏连线留位）；解锁可达的子树默认展开、远端默认收起；② Flickable+WheelHandler scale（0.5-2.0，缩放中心=鼠标点）；③ 布局重排：「打开背包」最左根 → 获得原木一条线；农夫/起航/发射等独立根线（现在农夫/起航/发射已是根?核 t619 三根树结构，按用户描述调成「打开背包→获得原木→工作台→合成系全挂工作台」+ 农夫/起航/发射独立根）。

### 🅿 方块细节批（1 大项 8 子点）

**t638** 方块细节批： ✅✅ 已完成（commit 2d93140）
- ① 木门中间应镂空可透视（现实体不透光——门上半格栅窗贴图有 alpha：mesher 门几何侧面不透明处理改 per-face alpha 或双面渲染；至少上半窗格透光——lightOpacity 门特例调低）；
- ② 南瓜放置朝向统一面对玩家（现随机——placeState 用 horizontalFacing^1 同熔炉/发射器；核南瓜 placeBlock 是否写了 state）；
- ③ 铁轨不能放在铁轨上（同格已有 Rail → 放置拒绝/射线穿透到下一格：选体时 Rail 薄板被选中优先级?——用户「我选下一格很难选到」：选体应忽略 Rail 面回退到方块后格（同木梯 t501 透视不优先选中模式）+ Rail 上放 Rail 拒绝）；
- ④ 铁轨贴图质量提升（程序 default_rail 分辨率/细节重画；pack 激活时用 pack 已对——用户看的是 pack?核：pack rail_normal 已接 t620——用户说丑可能是程序回退图或 UV 拉伸，复查）；
- ⑤ 动力铁轨/探测铁轨 item 图标（用户给了 powered_rail.png —— block 图兼 item：动力轨+探测轨**做方块**（新 id：GoldenRail/PoweredRail + DetectorRail；贴图 rail_golden/rail_detector（powered 变体留红石轮）；放置/连接同普通轨（无转弯）；矿车交互：动力轨=加速（矿车经过提速）/探测轨=输出信号（占位：踩过变 powered 贴图+可触发邻接发射器?留红石轮，本轮只做方块+贴图+连接+矿车行驶）+ 创造调色板；
- ⑥ 红石火把方块（用户给了 on/off 贴图+item 图：新方块 RedstoneTorch（on 态常亮+光照 7?装饰：右键切换 on/off 如红石灯模式 or 常亮 on——本轮常亮 on + item 图标 + 调色板；真红石信号留红石大轮）；
- ⑦ 仙人掌底面贴图（观察者模式看得到底——cactus_bottom.png 接：mesher 仙人掌 -Y 面用 bottomTile（现侧贴图统一——t620 第 2 部分结论「不接」要翻案：观察者视角能看到，接上）；
- ⑧ 附魔台顶部翻页书（装饰小书：附魔台方块顶上立一本打开的书——partial geometry 小 box 双页（程序贴图白色书页+字线），参照雪层/花 cross 模式挂附魔台方块）。

### 🅺 耕地/种植体系（1 大项 7 子点）

**t639** 耕地/种植细节批： ✅✅ 已完成（commit eced3e9）
- ① 胡萝卜/马铃薯右键耕地变成吃（beginEating 拦截在种植分支前——line ~497 `foodHungerAmount>0 → beginEating` 统一拦截）：修——手持胡萝卜/马铃薯/种子且**射线命中耕地**时优先种植分支（种植门控提前），未命中耕地才可吃；
- ② 耕地邻水应变湿（现状 tickFarmlandHydration 有但用户放水没变湿——核 farmlandHydrationLevel 距离判定/state 写回/贴图切换是否真生效：湿润等级>0 → farmland_moist 贴图（tile 27）mesher 判 state）；
- ③ 小麦种子/小麦物品从「材料」tab 挪到「食物」tab（creativeItems 分组）；
- ④ 踩坏耕地：跳跃落到耕地上 → 耕地变泥土（着地检测：下落速度>阈值 + 落点格==Farmland → setBlock Dirt）；耕地上有作物 → 作物掉落（既有失撑掉落复用）；
- ⑤ 耕地旁水源面突出（水 14/15 高于耕地 15/16 顶——观感水凸）：水面渲染对「邻格是耕地」的面降 1/16（mesher 水面高度采样邻格——查流体段高度计算，邻 Farmland 时 cap 到 15/16）；
- ⑥ 耕地可透视交互（不完整方块）：视线穿过耕地开旁边箱子（选体：Farmland 非满格 → 不挡射线（同木梯模式：raycast Farmland 面命中回退/穿过——MC 是能选中箱子：耕地薄顶命中优先箱子侧?——简单实现：耕地选体只在顶面命中时选中，侧面命中穿透）；耕地上的箱子可打开（已有?核箱子 open 门控没被耕地挡）；
- ⑦ 耕地选中框应贴 0.9375 高度（现满格框——selection box 对非满格方块用 collisionAABB（partialblock 已有先例：雪层/台阶框）核耕地 case）。

### 🅺 工具/战斗平衡（1 项 5 子点）

**t640** 工具/战斗批： ✅✅ 已完成（commit 2a89e40）
- ① 锄头耐久 bug：锄一锄就废（生存一锄即消失）——damageSelectedItem 走通用路径但**新锄 durability 未初始化满值**（t589 钻石锄/t557 金铜锄入槽时 durability=0?查调色板取物/合成产物/附魔台槽回填的耐久初始化路径——hotbar.cpp line ~178 注释提过「0 耐久实例进槽」防御，但锄路径仍触发；grep 各 UI 写槽时 dur 传值）；「所有锄头耐久都没调」（创造可一直锄=耐久显示也没有?核锄的耐久条显示 hotbar 槽 type==Hoe 分支）；
- ② 背包内工具耐久条：剑/斧/铲在 hotbar 有耐久条，打开背包后主栏/合成格/装备槽耐久条不见（SurvivalInventory 主栏槽耐久条绘制分支——t183 做过 hotbar 耐久条，背包槽漏）；
- ③ 铲子挖掘速度全面降两档（钻石铲太快如效率5——speedMul 铲族全体 ×0.6 左右调，对照 MC 1.0 数值铲速=镐速同级——现实现铲 speedMul 可能沿镐表没对，重标：铲对土/沙类基速应与镐对石相当）；
- ④ 骷髅弓箭手拉弓动画：现在箭没举弓就发射（t616 骨骼弓挂垂手位+瞄准抬起——核 drawAmount 驱动在发射瞬间才动?改发射前 0.5s 拉弓姿态（aiBones aimTimer 已有——绑定抬弓进度））；
- ⑤ 斧头伤害：斧砍 mob 应 4+ 伤害（attackDamage 表核斧分支——MC 1.0 斧伤=镐伤+1 各 tier；现可能斧=1）。

### 🅼 杂项（3 项）

**t641** 死亡后经验条不清空 bug ✅✅ 已完成（commit 2d047e7）
- 用户：「我死了复活后经验条没清空。」
- 核实现状：takeDamage 致死分支清 XP（playerstate.cpp ~26-33 t443）在——但用户见未清：疑 ①死亡路径不经 takeDamage（爆炸/箭直接 setHealth?）或 ②经验条绑定读旧值（xpChanged 发了但 QML 绑定 playerState 实例不对——查 Main.qml xpBar 绑的 playerState 是哪个实例/死亡后 respawn 是否重设）。读代码+日志定位修。
- 实查根因：两疑点均排除（所有死亡路径都走 takeDamage、xpBar 绑唯一 playerState 实例且 NOTIFY 正常）——真因是 onDied 在死亡点 spawn 的 1-3 XP 经验球被**尸体**瞬间吸走（XpOrbManager::tick 每帧常开且无 m_dead 门控，尸体停死亡点、球磁吸半径 6 内 ~1s 飞到 → addXp 把刚清空的条又填回）。修：playercontroller.cpp xp tick 加 `!m_dead` 门控（同 pickupScan 掉落物尸体不拾取模式）。

**t642** 僵尸 AI：卡方块 + 跳上作物格 ✅✅ 已完成（commit 55b886e）
- 用户：「晚上僵尸生成卡在方块里；还会跳起来踩在我农作物上走（农田应视为不可通行/不跳跃——MC 怪在耕地会被减速但不跳踩）。」
- 修：① 刷怪位置校验（spawn 候选格碰撞检测——生成点必须能容纳 mob AABB，防卡墙内：刷怪扫描已有?核黑暗刷怪候选验证）；② mob 寻路把耕地/作物格视为「减速可走但不跳」（跳跃判定排除目标格是 Farmland/crop——或者直接：作物格 non-solid 但 mob 跳跃分支只在「前方格 solid 且上方空」才跳，作物不 solid 不该跳）。
- 实查根因：①黑暗刷怪只查目标格 air+下方 solid，敌对 mob 高 1.8 占两格 → 1 格高洞穴气袋/树冠压顶即嵌（刷怪笼本就查双格）→ 加 spawnCellFitsHostile 双格 air 校验；②World::isSolid=「非 air 实存」，作物（cross 形无碰撞盒）恒被当墙 → mobAabbHitsSolid 挡移动 + 三处越障跳当 1 格墙 → 排除作物格（移动可穿越 + isJumpObstacle 不触发跳）+ kCropSlowMul=0.6 作物格减速；③窒息检测（头嵌 collidable）处加 stuck-escape 轻量兜底（每 1.5s 扫邻域移到最近空位，覆盖沙埋/破块/存档残留等运行期嵌入）。

**t643** 死亡后船卡水+复活体验
- 用户：「我死了复活后发现有条小船卡在水里。」（t630 支撑阈值修后应缓解——本项核复活时船实体状态残留（死亡不清实体——正常），卡水=支撑判定 bug 同 t630，并入验证）。
- 并入 t630 验证项，本项只留记录（不单独开发）。

### 🅽 图标管线（1 大项，用户点名要的工具）

**t644** 「放置 3D 贴图 → 背包 item 图标」转换工具 ✅✅ 已完成（commit 0077071）
- 用户：「你给我的 PNG 放下来 3D 是对的，但背包 item 图标跟放下来的方块不一样——做一个放下 3D 贴图转背包 item 图标的小工具，把这些都转了。」
- 修：扩 tools/build_cube_icons.py（已有 dimetric 3D 方块图标渲染器）：新入口 `--from-pack <block> <png>`——用 pack PNG 六面贴图按方块形状（满块/半高/门/台阶）渲染 dimetric 图标（同款投影：顶面亮 1.0/左 0.8/右 0.6），输出 icon png 进 qrc；批量处理本轮新方块（发射器/投掷器/附魔台/末地祭坛/书架/铁轨族/红石灯/矿物块/门）。替代现用 pack front 图/程序图标的混搭，统一为「pack 贴图 3D 渲染图标」。
- ⚠️ pack PNG 只读不进 git——工具读 pack 生成 icon PNG（程序产物）进 git 合法（同 build_cube_icons 现状）。

**t645** itemFilenameMap 遗漏批量补映射（9 条 + blockDir 兜底 + 矿车自绘回退） ✅✅ 已完成（commit 7d46236）
- 用户实测（pack item/ 目录 435 文件全列，与现有映射差集）：以下 9 条**物品已实现但 pack 映射漏了**（pack 启用仍走 MaterialIcon 自绘）：
  | id | 物品 | pack 文件 | 现回退 |
  |---|---|---|---|
  | 0x232 | 骨粉 | bone_meal.png（实测在） | drawBonemeal |
  | 0x233 | 甜浆果 | sweet_berries.png（在） | drawSweetBerry |
  | 0x234 | 橡木船 item | oak_boat.png（在） | drawBoat(false) |
  | 0x235 | 云杉船 item | spruce_boat.png（在） | drawBoat(true) |
  | 0x236 | 青金石 | lapis_lazuli.png（在） | drawLapis |
  | 0x237 | 纸 | paper.png（在） | drawPaper |
  | 0x238 | 书 | book.png（在，**勿与 enchanted_book.png 0x227 混淆——已接**） | drawBook |
  | 0x239 | 火药 | gunpowder.png（在） | drawGunpowder |
  | 0x23E | 矿车 | minecart.png（在） | **MaterialIcon 连 case 都没有（0x23E 漏）→ 补映射 + 补自绘回退分支**（pack 关时不空白） |
- 修：itemFilenameMap 逐条补（机制同既有：包缺文件安全跳过回退自绘不崩）。
- **「映射已写但永远 miss」3 条**：glass.png(0x204)/white_wool.png(0x20E)/oak_sapling.png(0x21B) 目标文件在包内 **block/ 目录**（demo 包把方块类物品放 block/），而 itemIconSource 只探测 itemDir。修：itemIconSource 补 itemDir→blockDir 双探测兜底（参照 blockItemIconSource ~line 680 已有的双探测机制——**不要**手动拷 PNG 进 item/（pack 只读）；block/ 有残留副本 `oak_sapling (2).png` 别碰）。
- 可选进阶（本轮做）：**spawn_egg.png + spawn_egg_overlay.png**（生物蛋两层模板：底图+斑点叠层；包内无 pig_spawn_egg.png 等独立文件 → 9 个生物蛋映射全 miss）→ 参照 retintCopperTemplate/retintLeatherTemplate 先例做**生成式蛋图标**：底图染 mob 主色 + overlay 染副色 → 落盘缓存（每 mob 种配色表：猪粉/牛棕/羊白/蹒跚绿/骷髅骨白/潜行者暗绿/蜘蛛黑红/鸡白红/鱿鱼蓝灰）。运行期合成（composeSpawnEgg luma 映射保模板明暗）+ 落盘 voxelsandbox_rp_egg_<id>.png（运行期派生缓存非提交资产，实测 9 蛋全生成）。
- 暂不做留档：bow_pulling_0/1/2.png（弓拉弓三阶段帧）+ fishing_rod_cast.png（抛竿态）——图标随状态切换属增强，本轮不接（延至后续）。

### 📎 R19.6 范围（更新）
t622-t645（24 项；t643 并入 t630 验证不计开发项 → 23 开发项）。核心：物品数据模型补 name 透传（t622/t623 多 bug 共根因）、创造合成 tab（t624/t625）、压力板边沿+家族（t627/t628）、耕地种植体系（t639）、图标转换工具+物品映射补全（t644/t645）。**红石系统大版本单独下轮规划**（t627/t628 只做前置语义）。全部本轮做完不准拖。

---

## ⚠️⚠️ R19.7 补映射批（2026-08-17 用户 pack block/ 目录实测审计）

**t646** tileFilenameMap 方块贴图批量补映射（12 条直映射 + TNT per-face + 按钮复用） ✅✅ 已完成（commit 223231c）
- 用户实测 pack block/ 目录以下贴图全部存在，tileFilenameMap（resourcepackmanager.cpp）未接 → pack 启用时世界贴图仍走程序瓦片。逐条补（机制同既有：包缺文件安全跳过保程序瓦片；接上后创造 3D 图标采共享图集自动吃到 pack 贴图，blockItemIconMap 不动）：
  | tile | pack 文件 | 方块 |
  |---|---|---|
  | 102 | spruce_planks.png | 云杉木板/台阶/栅栏共用（包内残留 `spruce_planks (2).png` 是垃圾不碰） |
  | 106 | packed_ice.png | 浮冰 |
  | 107 | blue_ice.png | 蓝冰 |
  | 113 | anvil_top.png + anvil_base.png（顶/底侧 per-face） | 完好铁砧 |
  | 115 | anvil_top_damaged_1.png | 微损铁砧顶（**勿用** chipped_anvil_top.png = 现代 1.19+ 命名） |
  | 116 | anvil_top_damaged_2.png | 重损铁砧顶（**勿用** damaged_anvil_top.png） |
  | 120 | cobweb.png | 蜘蛛网 cross cutout（web.png 是 1.8 旧名同图，取现代名） |
  | 123 | cut_sandstone.png | 切制砂岩 |
  | 124 | mossy_cobblestone.png | 苔石（cobblestone_mossy.png 旧命名同图，取现代名） |
  | 128 | stone_bricks.png | 石砖/台阶/楼梯共用（stonebrick.png 是 1.8 旧名，取现代名） |
  | 131 | lever.png | 拉杆贴地扳手 |
  | 135 | brown_mushroom.png | 棕蘑菇 cross cutout（tile 62 红蘑菇已接 red_mushroom.png，不同瓦片勿混） |
- **TNT per-face（t638 铁轨家族同套流程）**：tnt_side/tnt_top/tnt_bottom 三贴图在包内实测存在。现 TntBlock 四槽全 tile 122（顶底也是侧图）。正路：build_atlas.py TILES 尾追加 default_tnt_top(164)/default_tnt_bottom(165)（AtlasTileCount 164→166 同步）；build_tnt.py 补两张程序回退图；BlockDef TntBlock 改 topTile=164/bottomTile=165/sideTile=frontTile=122；tileFilenameMap 接 122→tnt_side/164→tnt_top/165→tnt_bottom。
- **可选（做）**：木/石按钮 vanilla 本就复用木板/圆石贴图 → 132→oak_planks.png、133→cobblestone.png 两行即可（机制等价 MC）。
- **明确不做**：end_portal 瓦片 129/130（包内无 end_portal.png，frame 系 t620 已接）；tile 159 动力轨点亮态（恒断电无消费方，既有注释口径）；压力板 154-156（包内实测无此三文件，t627 已注明保持程序自绘）；**tile 162 附魔台浮书不接**（MC 里那是 entity 模型贴图非 block 目录贴图）——后续若做需换 entity 贴图管线，单列。

---

## ⚠️⚠️ R19.8 大复盘（2026-08-17 用户全量实测 R19.6+rev2+t646 后，HEAD 372eb03）

> **背景**：用户全量 playtest 后判定**附魔系统为 P0 重灾区**（「60 分都给不到」「必须重新修一下我才能去测别的功能」），另点名**红石更新必须做**（「这波你必须得把它做好啊」）。本轮 t647-t679（33 项）。
> **用户指示**：先写 dev-plan 过目，说开工才开工。语音转文字已校对整理。

### 🅰 P0：附魔系统完整性重修（4 项，最高优先）

**t647** 附魔/改名实例数据全链稳定性重修（拿起即丢、关包即丢、丢弃即丢、放入铁砧即丢）✅✅ 已完成（commit cac9478）
- 用户实测链：「附魔台附出锐锋1钻石剑 → 拿到光标变普通（无光晕）→ 放背包第一人称有光晕有文字有耐久，但背包界面无光晕、hotbar 1-9 有光晕 → 丢出成掉落物光晕消失 → 关包再开附魔文字没了 → 丢出捡回彻底普通剑（一次性永久丢失）；但附魔台槽里锐锋1还在（= 拿取未清槽/数据没走）」。附魔书同病：「鼠标拿起来放到物品栏立刻变普通附魔书；hover 有附魔显示、放进铁砧附魔显示消失；一次丢失后再也回不来」。
- 根因方向（开工时逐路径审计定位，不预设单一根因）：① InventoryOps.writeSlot 第 7 参 name 有透传但 **enchants 第 6 参**在 AnvilUI/EnchantingTableUI 的 localWriteSlot 形参是否接收（t622 注释自认「4 参签名面板多收实参无害」——若 anvil/enchant 钩子仍只收前 5 参，附魔在「放入铁砧槽」瞬间即被丢，与用户实测完全吻合）；② 光标手持态 heldEnchants/heldCustomName 在面板切换/关包路径的快照回填；③ glint 渲染点补全：光标拖动显示、背包主栏/生存栏槽、掉落物实体、附魔台/铁砧槽内（用户：hotbar 已有、第一人称已有但模型错——见 t647④）；④ 第一人称手持附魔工具是「浅紫色流淌方块」——手持模型对附魔物品的渲染路径整个错误（疑把 glint 图层画成了主模型），修复为正常工具模型 + glint 边缘光。
- 修法：全链审计 EnchantingTableUI/AnvilUI/InventoryOps/Hotbar 的每一次槽读写，enchant+name+durability 三元组随实例原子透传；补 glint 到上述全部渲染点；修第一人称手持模型。

**t648** 已附魔物品禁止再进附魔台 ✅✅ 已完成（commit 2b043fe）
- 用户实测：「附魔后的钻石剑还能放进附魔台再附一次变锐锋2，再附变耐久1」——t549 注释声称有此 gate 但实测失效（普通左键路径绕过 Shift 语义判定）。修：槽 0 放入判定统一走「itemEnchantCategory≠None 且 enchants 全 0」守卫，所有放入路径（左键/右键/Shift/数字键/拖动）全覆盖。
- 附带：MC 口径已附魔物品经**铁砧合并**可再强化（合法），仅禁附魔台复附。

**t649** 附魔台书架规则对齐 MC 1.0 + 字符粒子 ✅✅ 已完成（commit 4fdeb98）
- 用户：「4 个书架就能附魔前 3 档，不对——应该 10 个左右书架才能 123 级」。核实（已查）：现 countBookshelvesAround 数 **5×5×5 立方体**（含对角/楼下，124 格池）+ offered=[8,15,22]+floor(power/2) 固定基底——4 书架即显示 24 级档。MC 1.0 正解：书架只数**切比雪夫水平距离=2 的环带、高度 y 与 y+1 两层**（中间隔一格空气的空间要求即由此而来），上限 15；第三档位等级 ≈ 书架数驱动（15 书架 → 30 级）。修：计数改环带两层；等级公式改 MC 形态（基底随书架缩放，4 书架第三档 ~8-10 而非 24）。
- 字符粒子：附魔台打开/附魔时**书架 → 附魔台方向漂浮字符粒子**（用户点名；程序绘制小字符 quad 飘行，机制等价 MC 附魔台符文粒子）。

**t650** 铁砧关包吞物 bug 复现修复 ✅✅ 已完成（commit ca92c10）
- 用户：「铁砧上面放东西，一关掉背包东西就没了」。核实（已查）：AnvilUI onVisibleChanged → returnAnvilToHotbar 存在且 rev2-D2e 刚改过 addToAny——但用户实测仍吞。开工先复现（注意 E 关 vs Esc vs 点外关闭三路径 + 关包时光标手持是否也归还），修到「关包后 A/B 槽 + 光标手持全部回到背包/掉落，零丢失」。
- 关联：t647 的实例数据透传一并核对归还路径。

### 🅱 创造背包 / UI 批（4 项）

**t651** 创造背包生存 tab + 布局批 ✅✅ 已完成（commit 558a26f）
- ① 滚动条与格子间距过大（往左贴近）；
- ② 1-9 hotbar 行整体右偏一格（左侧空一列）→ 对齐主栏网格；hotbar 行与右侧垃圾桶连在一起 → 加间隔；
- ③ 切到生存 tab 面板突然变宽 → 统一按生存背包尺寸（「大家都按生存模式物品栏这么高来做」）；生存背包左侧空一整列的问题一并查；
- ④ 创造分类 tab 数量增多（下轮红石 tab 加入后更多）→ 分类标签改**两行**；
- ⑤ 压力板/木按钮/石按钮/拉杆/红石系从「方块」tab 挪出，新开**「红石」tab** 归组。

**t652** 功能 UI 面板拖物到外丢弃 ✅✅ 已完成（commit f4e1b5e）
- 用户：「附魔台/发射器/投掷器/铁砧这些打开的界面，物品应该跟背包一样可以拖到面板外丢出成掉落物」。修：统一拖出判定（面板边界外松手 → dropItemAtFront），七面板（工作台/熔炉/箱子/附魔台/铁砧/发射器/投掷器）全覆盖。

**t653** 创造模式中键复制 + 垃圾桶整组清空 ✅✅ 已完成（commit a9323fa）
- ① 鼠标中键点物品 = 复制**一整组**到光标（创造）；
- ② 中键指向生物 = 复制对应**生物蛋**（创造 pick-block 语义）；
- ③ 垃圾桶 shift+左键 = 一次清空光标**整组**（普通点击仍丢 1 个）。

**t654** 调色板物品批 ✅✅ 已完成（commit a37c4ef）
- ① 附魔书显示名只叫「附魔书」，附魔名（锐锋1 等）只写 tooltip 第二行（现「附魔书·锐锋」+第二行重复，用户点名去掉名字后缀）；
- ② 去掉完全空白的附魔书条目（每本附魔书必须 ≥1 附魔）；
- ③ **矿车进创造调色板**（用户全物品栏找不到矿车——t645 只加了名字没进调色板）；
- ④ 铁块挪到矿物块组（与铁/金/钻石块同组，用户点名没跟下面的块放一起）。

### 🅲 死亡流程（1 项）

**t655** 死亡态输入闸门 + 铁傀儡击飞摔死归属 ✅✅ 已完成（commit 86748d7）
- ① 严重 bug：「你死了」界面按 1/E 能打开生存背包（叠在死亡界面下），关掉后**能走动、能打铁傀儡、完全无敌**（未点重生）。修：m_dead 期间锁移动/攻击/背包键，只接受重生按钮与聊天。
- ② 死因归属：被铁傀儡上抛后摔死 → 死亡播报「被铁傀儡击飞摔死」而非普通坠落。实现：lastDamagerMobType + 落地/超时（~5s）清零窗口（用户提的 10s 定时器思路可行，取 5s 更贴 MC 惯例）。

### 🅳 红石系统 v1（用户点名必做的大更新，5 项）

**t656** 电力模型核心 + 红石粉导线 ✅✅ 已完成（commit 7b8b6d4）
- Power 网格：电源（拉杆 on / 按钮按下窗 / 压力板压下 / 红石火把 / 红石块）→ 红石粉 wire 传播（15 格衰减）→ 接收器（TNT 点燃 / 红石灯亮灭 / 动力轨激活 / 探测轨信号）。
- 红石粉 = 新方块（放置于方块顶面薄层，cross/平面贴图程序绘制 + pack 映射 redstone_dust 查包）+ 配方（红石×4+木棍）。

**t657** 红石火把反相 + 红石块电源 ✅✅ 已完成（commit 81d9d02）
- 火把：附着方块被供能 → 火把熄灭（反相器，MC 核心机制）；on/off 双态贴图（t638 已备 on 态常亮版）；红石块 = 恒电源。
- 用户实测缺口：「红石火把/红石块都激活不了动力铁轨、点不了 TNT」——本项接通。

**t658** 接收器接线 ✅✅ 已完成（commit deef388）
- TNT：电力邻接 → 点燃（按钮/拉杆已通，补火把/红石块/压力板/导线）；红石灯：电源驱动亮灭（用户：「拉杆按钮压力板都激活不了红石灯」）；动力轨：激活态 boost 生效 + 亮贴图（tile 159 终于有消费方）；探测轨 → 输出信号进电力网格。
- 发射器/投掷器接入电力触发（与既有机关触发并存）。

**t659** 压力板正上方触发发射器核查修正 ✅✅ 已完成（commit 5b31718）
- 用户实测：「压力板放在发射器正上方触发不了（算同时更新）」。核查 scanDispenserTraps 邻接集是否含「板在机器正上方」方向；红石电力模型落地后本路径自然并入 Power 网格。

**t660** 红石族创造调色板 + 图示 ✅✅ 已完成（commit 0b7cb6a）
- 红石粉/红石火把/红石块归「红石」tab（t651⑤）；相关配方；红石火把/红石块世界发光已有（用户确认火把发光✓，红石块补微光）。

### 🅴 船四轮（1 项）

**t661** 船岸沿/耐久/冰面/掉落批 ✅✅ 已完成（commit 3a1a8f6）
- 用户实测清单：①「从高处掉到低处就走不了」「到岸边直接掉下去——上次修的没生效」（2/3 阈值与实测不符，需真机复现链路）；②「从海里直接开上岸了」反向太容易（上岸应难/需速度）；③ 创造模式攻击船不掉落船物品；④ 冰上速度太快 + 惯性太小（应更滑：减速更低）；⑤ 冰面下水被卡住；⑥ 船物品 maxStack=1（材料段落 64，已核实——改 1，与桶/附魔书同款特判）；⑦ 撞碎睡莲✓、船上放船✓、丢船复现✓ 保持。
- 睡莲放置限制顺带修：「只能放浅水（鼠标须指到水下方块）」→ 深水区指水面即可放（射线命中水格顶面时允许）。

### 🅼 机关方块外观（1 项）

**t662** 按钮/拉杆几何重做 + 压力板图标修复 ✅✅ 已完成（commit 98e8f7f）
- ① 按钮 ≠ 压力板：小长方体（约 0.375×0.125×0.375 中心小块）+ **可贴墙**（放置吸附命中面：地面/墙面朝向 state，partial geometry 按朝向）；按下压更低。现实现 = 板状贴地（用户：「跟压力板一模一样，不行」）。
- ② 拉杆 = 底座小块 + **斜插有体积的木棍**，右键棍在两个方向间摆动（on/off 双态）；同样可贴墙/贴地。
- ③ 石/铁/金压力板**物品图标**只显示上半截卡底（wood/cobble 正常）——t627 图标采样错位修复。
- 木/石按钮贴图 t646 已接 pack 复用（oak_planks/cobblestone）✓。

### 🅽 铁傀儡 + 生物图鉴批（1 项）

**t663** 铁傀儡四修 + 图鉴补全 ✅✅ 已完成（commit c125844）
- ① 行走无动画（腿不动、平移）；② 不会跳 1 格（台阶卡死）；③ **打怪**攻击无动画（反击玩家的蓄力抬臂 t635 已有，对 mob 目标同样接 attackPose）；④ 贴图拼接悬空：脚-身体断开大缝隙、头-身体断开（t598 区域枚举重校 + 腿/arm 挂点对齐）。
- ⑤ 图鉴补全：蠹虫（要塞已生成但图鉴无）、鱿鱼、怪物蛋方块——「所有做好的生物都必须在资源查看器里」；⑥ 图鉴羊模型回归正常 + 剪毛后裸羊形态；⑦ 雪傀儡剪头后的头与身体贴图不匹配（像骷髅头，应为雪白）——t629 的雪块头换 snow 贴图/调色。
- 顺带：图鉴分类标签「材料 / 护甲」混串（玻璃/小麦种子/床都显示护甲）——isMaterial 把护甲段并进同一类标签，拆成两标签；雪球无名字（displayName 缺条目，补「雪球」）；图鉴 hover 名不带「·方块」后缀（「煤矿石」而非「煤矿石·方块」）。

### 🅾 末地要塞 + 传送门（2 项）

**t664** 末地传送门正确形态 ✅✅ 已完成（commit bf87f7c）
- 现状错：直接 3×3 放置「末地祭坛」方块。正解（用户点名）：12 个**末地传送门框架**（每边 3 个）围出 3×3 内圈 → 每格放**末影之眼**激活 → 内圈 3×3 生成**末地传送门**方块（薄薄一层黑色平面 + 星光点，通往另一宇宙的观感；先只做贴图与方块，不做末地维度）。
- 完整性：传送门开启后挖掉任一框架 → 3×3 门方块全部消失。
- 更名：方块「末地祭坛」→「末地传送门框架」。

**t665** 要塞结构重做 ✅✅ 已完成（commit f4327bd）
- 现状「非常简陋、房间非常小」。调研 MC 1.0 要塞生成（石砖走廊 + 书房（书架+蜘蛛网+蠹虫）+ 传送门房间（悬空熔岩台+框架环）等 piece 拼接式生成），仿其结构逻辑重做 worldgen（规模、房间多样性、连通性）。蠹虫 + 怪物蛋方块随书房/传送门房生成（怪物蛋被打掉出蠹虫——若蠹虫实体已存在则接线，不存在则连同 t663⑤ 一起补）。

### 🅿 铁轨系统（3 项）

**t666** 铁轨方向/连接/拐角重写 ✅✅ 已完成（commit 21b60b7）
- 用户实测症状群：① 放置朝向与玩家面向无关（恒 Z 轴）；② 邻接互连会**翻转已放好的直轨**（沿 Z 放两根 → 双双变 X）；③ 拐角方向反了（右转画成左转）且把旁边直轨带歪；④ 动力/探测轨放旁边，普通轨变奇怪形状（拐角只允许两根**普通轨**互连；动力/探测轨永不拐角）。
- 根因方向：recomputeRailConnections 无向重算互相覆盖 + mesher 拐角形状判定含轨族过宽。重写：放置时初始朝向=玩家面向轴向；互连只**新增**连接不改既有轴向（除非该轨本身是新放的）；拐角判定收紧。

**t667** 铁轨支撑与坡道 ✅✅ 已完成（commit 367745d）
- ① 铁轨只能放在**完整方块顶面**（铁轨上放铁轨禁止——用户实测仍可行，核查 t638 拒绝是否覆盖全部放置路径；悬空拒绝）；
- ② 坡道铁轨：轨道沿 1 格落差爬坡（partial geometry 斜段 + 矿车高度跟随）——动力/探测轨同。

**t668** HD 图集 kTile 16→64（世界贴图保真） ✅✅ 已完成（commit b2b8ffb）
- 用户实测：「第一人称手上/背包/掉落的铁轨图标很清晰（pack 128px），放地上很模糊（图集 16px 采样）」——梯子同理。修：图集瓦片分辨率 16→64（pack 128→64 降采样远好于→16；程序 16px 瓦片上采样无损失感）。UV 数学与 kTile 无关（1/AtlasTileCount 分数），仅图集文件/内存变大——全量目测回归一遍贴图清晰度与内存。

### 🅺 耕地/工具批（1 项）

**t669** 锄头首用耐久全消 + 耕地三修 + 毒马铃薯 ✅✅ 已完成（commit 1b0160d）
- ① 钻石锄右键第一下耐久**全部消失**（t640 归一防御未拦住，需真机复现看 [inv] tool dur anomaly 日志定位写入路径）；
- ② 手持泥土踩踏耕地 → 手上泥土**凭空消失**（复现+修，疑踩踏路径误扣选中槽）；
- ③ 耕地上方放方块 → 耕地变泥土（MC 规则，placeBlock 前置检查）；
- ④ 毒马铃薯：新物品（马铃薯贴图调绿 / pack poisonous_potato 查包），食用概率中毒扣血（与腐肉同款食物中毒机制若已有则复用）。

### 🅻 杂项批（10 项）

**t670** 僵尸 AI：玩家高一格时跳跃追击（现被卡死不跳）；白天燃烧时主动寻阴凉路径。 ✅✅ 已完成（commit c12003e）
**t671** 潜行者：身高提至 ~1.9（现 1.5 偏矮，用户按 2 格口径）；引爆蓄力期**白闪**（与 TNT 同款 null 贴图白闪交替，现只膨胀不闪）。 ✅✅ 已完成（commit 72b9a3c）
**t672** 月亮：左上角斑点状伪影排查修复；`time set` 聊天回显去掉「月相刷新」字样（用户点名不要这行）。 ✅✅ 已完成（commit 92dc1c8）
**t673** 雪傀儡雪头贴图与身体统一（并入 t663⑦ 若同批则删此条）。 ✅✅ 已完成（并入 t663 ⑦，commit c125844）：核查结论 —— t663 ⑦（c125844）已把剪头后的雪头从「雪瓦片冰晶噪点 + 近黑刻痕（读作骷髅）」重做为**纯色雪白 #f0f4f8 同身体**（UnitCube + 柔灰 #4a5568 刻面眼嘴），游戏内 Main.qml 与图鉴预览 ResourceBrowser.qml 两处同源同步；t679 批内核验代码确认无遗漏，本条即 t663 同批完成，无需独立改动。
**t674** 木门细节：镂空窗 4 孔（现 3）；门薄侧边用**普通木板贴图**（现压缩门贴图很抽象）；（可选 S）开门卡人半身 + 空格跳上门沿的 MC 特性——评估碰撞实现成本，做不动则记档。 ✅✅ 已完成（commit af6b523）
**t675** 南瓜 3D 图标重做（拼方块技术，t644 同款）。 ✅✅ 已完成（commit fcbb84a）：FROM_PACK cube_front 方案（满立方 + 可见 +Z 前面贴刻脸，pick_pumpkin_face 复刻引擎 tile 118 退化回退链）；pack 激活与否都显 3D。
**t676** 工作台/熔炉/发射器/投掷器/TNT 创造图标 3D 化（t644 的 front 方案太扁平，用户点名这 4 个 + TNT 背包图标，全部升 cube per-face 方案）。 ✅✅ 已完成（commit 2c7a662）：五件全部升满立方 dimetric（顶 + 右侧 + 前面三面独立贴图）；撤出 blockItemIconMap 的 2D pack front 覆盖（t537 曾恢复）→ pack 关/开都显 3D。
**t677** 图鉴 hover 名去「·方块」后缀；分类标签拆分与雪球名并入 t663（若同批）。 ✅✅ 已完成（并入 t663，commit c125844）：核验结论 —— ① hoveredSuffixText/hoveredCategory 恒空（ResourceBrowser.qml 无「·方块」后缀）；② 分类标签先判 isArmor →「护甲」、再判 isMaterial →「材料」（拆分已生效）；③ 雪球 nameForBlock 返「雪球」（hotbar.cpp）。三项均已在 c125844 落地，本批零代码改动。
**t678** 成就树五修：滚轮缩放失效；拖拽平移失效（死机感）；ESC 关闭面板；单链节点（打开背包→获得原木→合成台 一条线）不显 +/− 钮（只在真分叉显）；返回按钮离下边缘过远。 ✅✅ 已完成（commit 0095a1b）：缩放并入 treeDragArea.onWheel（MouseArea 是视口最顶层命中项，删除 WheelHandler 双通道）；clampPan min/max 交换修拖拽钳死；ESC 加窗口级 Shortcut 兜底；+/− 钮与默认收起仅限 ≥2 子女真分叉；返回钮锚定面板底 8px。
**t679** 附魔台悬浮翻页书：书**悬浮**于附魔台格上方（0.25 间隙处）而非贴台面；有体积的敞开书；随机翻页动画（页片摆动）；贴图走 entity 管线（pack enchanting_book 贴图若包内 entity/ 有则接，无则程序白页+字线，t646 已注记）。 ✅✅ 已完成（commit aeb573b）：静态 C++ 书移出 partialblockgeometry → Main.qml bookDelegate（两页 V 形 + 书脊 + 页片翻页动画 22→338° 随机 2.5-6s + 柔浮）；torchPositions 同款事件驱动位置表（blockPlaced/broken/worldChanged 校验）→ 所有已放置附魔台都渲染。pack entity/enchanting_table_book.png 存在但整书 UV 展开与两页盒不匹配 → 程序白页+字线（spec 允许的兜底，注释记档）。

### 📎 R19.8 范围
t647-t679（33 项）。优先级：**P0 = 🅰 附魔链 t647-t650 + t655 死亡闸门**（用户被阻塞无法测其他功能）→ 🅱 创造背包批 → 🅳 红石 v1（用户点名必做）→ 其余按字母序。红石 t656-t660 为一个纵切（模型→导线→反相→接收器→调色板），串行做完。全部做完 code review + 统计。
**保留决策**：金压力板「仅掉落物」维持 MC 语义（用户已自答确认）；L18「过于昂贵」/L19「神射手计数」沿袭旧档不动。

---

## ⚠️⚠️ R19.9 复盘（2026-08-18 用户实测 R19.8 + 4 路审查裁决后，HEAD f1e989d）

> **背景**：R19.8 全 33 项落地。4 分区审查代理报 ~40 项，用户逐条读码裁决（探测轨两报告相反已判）。**先修审查（t680-t692）→ 再开发（t693-t712）**。本轮 t680-t712（33 项）。

### 🅰 审查修复批 H 级（3 项，最优先）

**t680** 探测轨供电死代码三修（审查 H1，deef388）✅✅ 已完成（commit a493dcf）
- 矿车 pos.y = 轨格 y+1+rise+0.30 → bcy=floor(pos.y) 恒为轨上方空气格，探测循环首格非轨即 break → detY 恒 -1，DetectorRailStateOnFlag 永不置位。三处同 commit：① 钉轨面循环记 pinnedY，探测判 blockAt(bcx,pinnedY,bcz)；② m_detectorOccupied 补 clear()（防「离开沿」永不触发永久带电）；③ 占用标记移出 speed!=0 闸门（停稳不被误判离开）。

**t681** 侧挂红石火把 NOT 门振荡 + mesh 重建风暴（审查 H2，81d9d02）✅✅ 已完成（commit 0ea1b56）
- torchAttachOffset switch(state) 整值匹配，熄灭态 state=attach|0x08 不匹配 → 误判失撑重亮→每 tick 翻转 + 每秒 5 次全量重建。修：switch(state & ~RedstoneTorchStateOffFlag) 掩掉 0x08 再解附着（playercontroller.cpp:1305 失撑同修）。

**t682** 要塞装饰覆盖传送门框架（审查 H3，f4327bd）✅✅ 已完成（commit ecf902e）
- 装饰跳过区 dz∈[-9,-6] 但框架环含 z=-10/-11，每格 18% 被 Cobweb/Stairs 覆盖 → 63% 要塞门环残缺永不能激活。修：跳过区改 dz∈[-11,-6]（或精确排除框架+平台 bbox）。

### 🅱 审查修复批 M 级（8 项）

**t683** 爆炸路径漏 notePowerWrite（M1）：TNT/苦力怕炸红石 → 幽灵电。爆炸收尾循环补 notePowerWrite(d.x,d.y,d.z,oldId,Air)。✅✅ 已完成（commit e7565ff）
**t684** 轨道死端当坡冲出悬空（M2）：minecartmanager.cpp:256 slope<0 不排 INT_MIN → 静止矿车自动出轨。修 slope != INT_MIN && slope < 0。✅✅ 已完成（commit 1ba06a8）
**t685** t648 门禁 no-op 双击洗白光标（M3）：双击判定先于 canPlace + doMergeSameId 空扫描取默认值 → 耐久回满附魔清零。修：doMergeSameId 开头 slots.length===0 return。✅✅ 已完成（commit c70d0ec）
**t686** 死亡掉落漏 durability（M4）：dropStack 读 durabilityAt + emit 第 8 参；Main.qml onSpawnItem 形参补。✅✅ 已完成（commit 79de5d5）
**t687** 附魔书哨兵 off-by-one（M5）：Inventory.qml:158 id <= bookSentinel - maxEnch 把最大附魔 id 的书判死格。改 <。✅✅ 已完成（commit d8320b6）
**t688** 改名栈均分污染 + 双击丢名（M6）：redistributeLive eligible 加 orig.name===""；doMergeSameId 遇带名实例 no-op。✅✅ 已完成（commit c30b045）
**t689** 发射器无真上升沿（M7）：稳定通电时每 2s 连发。消费端维护「上 tick 已通电」集，仅真沿放行。✅✅ 已完成（commit 624eda6）
**t690** 僵尸寻荫烧死（M8）+ 击飞窗口残留（M9）+ 合成格死亡丢失（M10）+ 毒马铃薯磨甲（M11）四合一：✅✅ 已完成（commit dac18d3）
- 寻荫：停驻前验证自身格 skyLight < 阈值；findShadeTarget 加 y+2 净空；
- 击飞：着地沿无条件清窗口（含水抵消分支）；
- 合成格：gatherPlayerState / dropAllItems 前显式同步 returnCraftToHotbar()（t650 同款，工作台面板漏覆盖）；
- 毒伤：独立信号或豁免 cause 不走 damageArmor；respawn/loadSavedState 补清 m_poisonTimer。

**t691** 审查 L 级随批（选摘 8 条）：kMcBlockId monster_egg 行被注释吞掉（拆行）；矿车 rise 四向全叠 vs mesher 只叠本轴（垂直邻线跳变+坡后 30% 错层）；死亡键盘闸门吞 T/Enter/Esc（聊天/暂停不可用）；铁傀儡越障跳漏水平滑流 + golemWindup 目标消失不清；附魔台书读档不重建；冰滑参数玩家/船共用（分离）；四处注释错乱（blockregistry.h:1688 拼接事故 / .cpp:1466 bit0→bit3 / pbg.cpp:593/706 旧编码描述）。✅✅ 已完成（commit ffbd971）
**t692** 红石传播 v1.1 模型对齐 MC（审查 L + 实测综合）：用户实测「火把只亮邻 1 格、打掉火把残留亮、一格粉恒点状、按钮传播断续」= Phase A2 边写边读单 tick 深度不确定 + BFS 2048 截断 + 传播延迟体感乱。修：A2 改双缓冲快照迭代（本 tick 全域基于上 tick 值）；粉亮度按级渐变渲染（16 级至少分 4 档视觉）；单点粉邻接 TNT/火把时画连线形态。✅✅ 已完成（commit 4f9e919）

### 🅲 附魔/创造批（实测）

**t693** 已附魔工具禁入附魔台（实测：t648 只拦了书没拦工具）——canPlace 钩对 itemEnchantCategory≠0 且 enchants 非零全拒。✅✅ 已完成（commit f46b827）
**t694** 创造模式免等级（实测：创造仍扣等级/档位锁）——affordable/doEnchant 创造分支全档免（同 t606③ 铁砧先例）；**生存等级不足必须真拒**（实测生存 0 级也能附——spendLevels 路径核）。✅✅ 已完成（commit 6dff88f）
**t695** /xp 命令：`/xp N`（纯数字=经验点）/`/xp NL`（L 后缀=加 N 级）。聊天命令表新增。✅✅ 已完成（commit 785f22b）
**t696** 附魔 glint 全渲染点统一（实测：创造背包格无紫纹/第一人称无/掉落无；且紫纹应只作用图标不糊整格——波纹改图标内裁剪）。✅✅ 已完成（commit 90aa610）
**t697** 附魔台书视觉（实测：字太少/太白/太平）——页加暗色符文字符纹理；整体前倾 ~20° 朝玩家（讲台观感）；粒子：只要旁有书架常驻循环（非仅放入物品时）；书翻页动画循环播放（风翻页感）。✅✅ 已完成（commit 4deca73）
**t698** 附魔效果生效核验 + 武器伤害显示（实测：击退未生效?）——逐附魔效果接线 audit（锋芒→攻击伤害计算点、击退→knockback 向量、亡灵杀手→对亡灵族增伤）；tooltip 末行蓝字「+N 攻击」动态显示（含锋芒加成）。✅✅ 已完成（commit 4d55294）
**t699** 铁砧放入即丢附魔复修（实测：附魔钻石剑进铁砧附魔消失）——t647 审计后仍残留路径，复现+修。✅✅ 已完成（commit f4c465e）
**t700** 创造背包垃圾桶语义（实测澄清）：① 平时拖一组上去=清空整组（非逐个）；② shift+左键=把**背包里拿出来的全部**（光标手持栈）清空。✅✅ 已完成（commit 2c157a8）
**t701** 创造调色板整理：铁轨+TNT 挪红石 tab；红石 tab 的红石粉贴图改用材料段贴图（同一物品不再两套图标）；材料段不再重复列红石粉（或同 id 共享）；毒马铃薯挪食物 tab；附魔书末尾空格（弹射物保护后仍有空位）排查删除。✅✅ 已完成（commit 4abf1b1）

### 🅳 红石批 v1.1（实测）

**t702** 粉尘传播 15 格衰减 + 形态（与 t692 合并做）：16 级衰减、亮度渐变、拐角连线（邻左+上有粉→画拐角线非十字；真交叉才十字）；粉不能放粉上（须完整方块顶面）；粉上墙（高一格连接，同铁轨坡 t667 模式）；去源后正确熄灭（快照迭代根治）。✅✅ 已完成（commit 38e2fd7）
**t703** 动力/探测轨清晰贴图（实测：两轨仍模糊）——pack 128px 直采或程序 64px 重画（t668 只覆盖普通轨 pack 路径?核 atlas 消费链）。✅✅ 已完成（commit 3fd13d3）
**t704** 动力轨链式激活（实测：红石块只亮贴邻 1 根）——MC 语义：被激活动力轨把信号传给同向相邻动力轨，最长 9 格（块供电 8 格?按 MC 1.0：块直接激活 1 根，该根向同向链传播共 8 格）。boost 接线：矿车过激活轨加速（实测未生效——核 GoldenRailStateOnFlag 置位链）。✅✅ 已完成（commit 38e2fd7）
**t705** 按钮/拉杆/红石火把贴墙修复（实测：贴墙出现在墙背面悬空）——放墙时机关生成在命中面**外侧**格；参考火把插墙代码（torchAttachOffset 先例）；按钮几何砍半改长方体居中；拉杆可东西/南北双轴向。✅✅ 已完成（commit 3bb244b）
**t706** 红石火把/红石块/红石粉激活 TNT + 发射器/投掷器（实测全不通）——powerSourceLevel 表核对：火把（相邻供能）、红石块（相邻）、粉（末端供能）三类全接 TNT 点火与 fireDispenserAt；红石火把光加红色调（现太白）；红石块已有微光✓。红石矿：观察者不触发点亮（isManualIgniter 同款 Spectator gate）；红石火把光偏红罩。✅✅ 已完成（commit 58e1456）
**t707** 压力板接红石（实测：踩板红石亮但延迟灭/断续）——压下=电源 15（t656 已列源，实测不稳）；离开沿正确去源（t692 快照迭代根治延迟）。✅✅ 已完成（commit 8229db0）

### 🅴 矿车/铁轨批（实测）

**t708** 矿车落地修正：放轨上不悬浮（贴轨面）；朝向沿轨延伸双方向（非固定）；无轨不前进（出轨即停）；空车可被玩家推动（推力沿轨道行进）；W 前进/S 后退均有效（实测 S 无反应需转身才动——核 wish 方向与 dir 点积符号）。 ✅✅ 已完成（commit cc7450f：spawnCart 贴轨面+轴向朝向、stepCartAlongRail 共享推进（负速=S 倒行、出轨/死端停）、tickPushedCarts 空车滑行（下坡顺滑/平地磨擦）、pushEmptyCart 推空车、pickTrackStep 轨格改列内下扫（坡顶不错层）。
**t709** 拐角铁轨方向仍反（实测沿 X 正放右转，交界处方向相反）——t666 拐角象限映射再核（四象限逐一推演）；动力/探测轨贴图模糊并入 t703。 ✅✅ 已完成（commit 4cdfe97：四象限 UV 映射逐像素验证全对（程序贴图 + t620 镜像 pack 贴图，轨像素触边=连接对）；真根因=拐角规则要求两臂同层 → 坡臂不配对被规则③/④丢弃、交界格渲染单臂 stub。放宽臂高（同/上/下普通轨均可配对）+ 拐角 quad 沿臂侧 armLift 抬升 + 矿车 pinCartY 双线性中心随动）。
**t710** 坡道捋直 bug（实测：下坡轨前只有一格凹地再放轨会被捋直悬空）——t666 规则集补「单格凹谷不允许直化」判定（坡形优先保持）。 ✅✅ 已完成（commit 4cdfe97：V 形凹谷（本轴两端 δ 均 +1）改画两半格 quad（端 +1→谷心 0→端 +1 下凹曲面）替代旧单 quad 恒 +1 平板悬空；矿车钉轨面同 V 曲面；拐角优先序保证凹谷永不被垂直邻带歪成拐角）。

### 🅵 实体/世界批（实测）

**t711** 船五修：睡莲顶飞概率 bug；船下水口 2/3 判定仍偏严（2/3 在方块外仍不下水）；创造打船不掉落；撞边界沉底后松键恢复海面（陆档稳态 Y 逻辑）；冰面可上/沙滩不可上不一致（冰 exemption 范围核对）；撞毁难度回升（kBoatCrashSpeed 核）。 ✅✅ 已完成（commit 21fff7b：睡莲列不再算支撑（整格顶顶飞根因）、水覆盖改 6 固定几何采样点（2/3=4/6 稳定）、hitBoatFromRay 带 World* 选非实心邻格掉落（防埋方块=「不掉落」真根因，路径本就全模式同链）、撞世界边界视同撞墙（高速撞毁/低速停轴，根治沉底-浮回振荡）、ignoreIce 豁免扩为「与水面同高的任何固体」（沙滩与冰一致可上）、kBoatCrashSpeed 12→14）
**t712** 铁傀儡批：贴图偏暗（tint 核）；伤害 10→回调（两锤秒怪偏高）；敌对 mob 应主动攻击铁傀儡（僵尸/骷髅优先，苦力怕不炸铁傀儡）；被击飞插沙 bug（launch 后嵌入支撑的 extrude/复探）；死因「不明原因」→「被铁傀儡击杀」（近距重拳非摔落时）。 ✅✅ 已完成（commit fd113b5：pack 贴图命中时 baseColor 不再叠铁灰（~50% 压暗根因）、kGolemPlayerDamage 10→7（MC 7-21 中低档，三锤击杀满血玩家）、aiHostile/aiArcher 加 nearestIronGolem 优先锁定（近战伤害+击退 / 拉弓射箭+骷髅箭新增铁傀儡命中分支；潜行者不参与）、击飞 5s 窗口内嵌入向上排出 launchUnburyUpward（常规沙埋 t258 语义不变）、新 DeathCause GolemSlain「被铁傀儡击杀」接 MobIronGolem 映射（摔落仍 GolemLaunchFall））
**t713** 末地要塞扩大 5 倍 + 结构对齐：传送门平台加大可站立；岩浆生成在 3×3 门面下一格；图书馆等房间扩大；楼梯按顺序生成（现在悬浮挡路）+ 通向门框；楼梯尽头蠹虫刷怪笼（MC 要塞生成代码调研）。 ✅✅ 已完成（commit 0433fce：要塞 21×21×5→45×45×7（~4.5× 面积，墙高 5）；传送门房 25×10——13×6 石砖高台（顶面 y=4 可站立，12 框架环 + 3×3 内圈全落台上）、3×3 岩浆盆在 dy=3 恰为门面（dy=4）正下一格、dy=0 环沟岩浆河被高台/石砖栏/墙封闭静态不流、中轴 3 级楼梯每步 Δ0.5 ≤ auto-step 0.55 可步行登台（台下实心填充不悬浮）、银鱼刷怪笼放环中心正上方 (0,5,-18)（MC 1.0 传送门房上方 spawner 布局，复用既有 SpawnerStateSilverfishFlag）；图书馆 10×17 书架墙 + 中央书架岛；战利品房 17×11 双刷怪笼 + 双宝箱；+东北/东南储藏龛；走廊装饰改仅 dy=1 蛛网（移除楼梯散布 → 根除 t682 覆盖框架/悬浮挡路回归类）；Python 静态推演核对环判定一致/岩浆封闭/步高/连通性（980 内部格 flood-fill 全连通））
**t714** 云杉体系：雪原云杉树底须接泥土（现悬空/细雪上）；云杉树叶（雪原树冠现在还是橡树叶）；叶子背包 item 图标同步（现还是旧绿图标）；木台阶/栅栏/楼梯等老图标重做（放置贴图对但图标旧）。 ✅✅ 已完成（commit 33fe31f：①placeTrees 加 SnowLayer 支撑守卫（surfaceY-1 须 Snow/Dirt，峡谷/洞口悬空雪列不种）；②新方块 SpruceLeaves=133（tile 175 深蓝绿针叶，build_spruce.py 程序生成；机制全同 Leaves——衰减认 Log/SpruceLog 支撑、持久位、掉树苗/木棒、silk、焚毁、叶音色；placeSpruceTreeAt 树冠换用）；③pack 管线——tileFilenameMap {175→spruce_leaves.png} + tileTint 固定云杉深蓝绿 #3a6e55（pack 贴图灰度实测 130）+ blockItemIconMap {7,133} 叶 item 图标运行期 retint（retintLeafTemplate 同床模式）；④图标重做——build_cube_icons.py 新 partial 模式（pack 贴图 × render_pack_box 真实形状投影）重烘 wood_slab/stairs/fence + spruce_slab/fence（HD 木板）+ leaves/spruce_leaves（×叶 tint）；粒子色表补 115..132 红石时代缺色 + 133 云杉叶）
**t715** 状态效果系统 v1（用户点名）：mob_effect 图标接入（docs/Default HD 128x Demo 1.8.2.2/assets/minecraft/textures/mob_effect/ 本地参考，程序回退）；中毒（毒马铃薯 60% 概率/蠹虫?）+ 已有减速/着火统一接入效果框架；右上角状态图标 + 持续时间显示；效果 tick（中毒周期扣血不磨甲 t690④）。**pack 只读红线**：图标运行期映射（effect 图标走 item/ 类似管线或程序绘制回退），不拷贝 PNG。 ✅✅ 已完成（commit 47fafe5：状态效果容器 effectRevision+setActiveEffects 信号收编、缓慢 m_slowTimer ×0.85、中毒 1.25s 不致死、/effect 命令、右上角效果栏自绘图标 + pack mob_effect 运行期映射）
**t716** 杂项：仙人掌站立伤害（生存近靠/站上扣血——t190 回归?核 AABB 判定）；橡木门 4 孔回归（t674 改后只剩上 2 孔——云杉门 4 孔对，橡木门 hole 区域 y 范围错）；雪原细雪悬浮峡谷上（worldgen snow 判定）。 ✅✅ 已完成（commit a0e428b：仙人掌站顶伤害含边界 Y 判定、门窗 pack 合成 CompositionMode_Source 替换 + 云杉懒拷贝守卫、worldgen pruneFloatingSnowLayers 清悬浮雪）

### 📎 R19.9 范围与顺序
t680-t716（37 项）。**严格顺序：t680-t692 审查修复（H 先）→ t693-t701 附魔/创造 → t702-t707 红石 v1.1 → t708-t710 矿车铁轨 → t711-t716 实体/世界**。每项独立 commit + dev-plan ✅✅。全部完成后 code review + 统计报告。








---

# R19.10（实体大扩充批：盔甲 3D / 画作 / 铁门铁活板门 / 末影之眼与末影人 / 烈焰人 / 火焰与下界门 / 鱿鱼与皮肤贴图）

> 背景：R19.9 全 37 项完成后用户跳过 review 直接追加新一批。素材均为 docs/Default HD 128x Demo 1.8.2.2/（本地只读参考，红线不变：**禁止拷贝 PNG 进 git/qrc/build**；一切新贴图走 tools/build_*.py 程序自绘，pack 运行期映射除外）。
> 命名映射（PLAN §9，代码/UI 用左侧原创名，本文件可注 MC 等价名）：末影人=**夜行者 Nightwalker**（MobNightwalker=16）、烈焰人=**燃烬者 Emberling**（MobEmberling=17）、末影珍珠=**暗渊珠**、末影之眼=**暗渊之眼**、烈焰粉=**燃烬粉**、下界传送门=**余烬门**、末地要塞=暗渊要塞（沿用 Stronghold）。

### 🅰 贴图补缺（先行，其余任务依赖）
**t717** 生成器批：tools/build_armor_layers.py（armor layer_1/2 三方五档参考构图原创自绘：皮/铁/金/钻/链）+ build_paintings.py（27 张程序自绘，尺寸 16/32/64 系列）+ build_doors_iron.py（铁门上/下 + 铁活板门自绘）+ build_entity_*.py（夜行者体/眼、燃烬者、鱿鱼、矿车、书、皮肤 steve/alex 程序贴图）。全部产 textures/*.png + build_atlas.py 清单登记（AtlasTileCount 右移）。pack 运行期映射 tileFilenameMap 登记（armor/painting/entity 路径，pack 启用时覆盖）。 ✅✅ 已完成（commit 4f317d1：四生成器 + 45 张程序贴图；atlas 176→179（door_iron_upper/lower + iron_trapdoor 三瓦片）；ResourcePackManager 新增 tileFilenameMap{176..178} + paintingSource/paintingFallbackName/entitySource/armorLayerSource 运行期映射（miss 回退程序贴图）；烟测 pack 实际覆盖 141→144 瓦片实证生效）

### 🅱 装备 3D（armor layer 渲染）
**t718** 盔甲穿戴 3D 显示：玩家模型（Main.qml playerModel）+ 生物模型（MobModel）按已穿护甲件叠加 layer_1（身/腿）+ layer_2（靴/头盔）薄壳盒体（partial 风格盒子几何，UV 按 layer 贴图布局自
拼——与生物贴图同“自拼装”模式）。护甲段图标/背包已有（0x300 段），只做 3D 显示层。 ✅✅ 已完成（commit 45ace1e：新增 Renderer ArmorLayerBox 几何——±0.5 单位盒 + MC armor layer box-UV 六面子区（piece 0-5：头/胸/袖/腿采 layer_1、右/左靴采 layer_2，同 MobModel R19 C3 公式）；playerModel 全部 9 个护甲 Model 换 ArmorLayerBox + armorLayerTex(armId, layer)（10 张静态 Texture 实例 = 5 tier × 2 layer，pack 命中 armorLayerSource / 否则程序层 armor_*_layer_*.png；alphaCutoff 0.5 开脸窗/链甲孔）；tint 改 armorTintT 近白（层贴图自带 tier 色，仅保 hurt 红闪——防二次染色）；armorLayerSource 皮革命中接 retintLeatherTemplate 染棕落盘（t717 TODO 兑现）；build_armor_layers.py 补 copper 档（自创档无 pack 等价恒程序层））
**t719** 生物穿甲同步（mob 穿戴显示）：MobModel 支持按 entity armor 字段叠加同款 layer 盒体（先接 Zombie/Shambler 可穿拾取的甲——若 mob 拾取装备体系未实现则本项降级为「玩家+人形 mob 通用 layer 渲染器」+ TODO 注释）。 ✅✅ 已完成（commit 2ccdc56：Shambler/Bones delegate 护甲 Model 全部换 ArmorLayerBox + armorLayerTex（与玩家共用同一通用 layer 渲染器）；mobArmorTintT 近白 tint 保 hurtFlash 红闪（tier 色乘法退役防二次染色，t597 同理）；**降级路径已注明**——mob 拾取装备 AI 未实现 → EntityManager::setMobArmorSet(i, tier) Q_INVOKABLE + /mobarmor <tier> 聊天命令（最近人形 mob 穿/脱整套 tier 护甲，调试驱动；拾取 AI 留后续）；铁傀儡铁灰贴图不动）

### 🅲 画作系统
**t720** 画作物品 + 墙面尺寸检测放置：PaintingId（item 段）；右键墙：对命中墙面测最大可用矩形（横竖逐行/列扫空墙面），随机选一张尺寸 ≤ 可用矩形的画作，放不下则 no-op（物品不消耗）。画作为新 partial 方块（Painting，薄板贴墙，state 编码画 index + 朝向）；27 张贴图 pack 运行期映射（pack 挂 painting/ 目录逐张映射，miss 回退程序图）。 ✅✅ 已完成（commit e1f2775：PaintingId 0x242 材料段 + 配方 8 木棒围 1 羊毛 + maxStack 1 + MaterialIcon drawPainting + pack painting.png 映射；Painting 方块 134（solid=false/ShapeNone 无碰撞、maxStack 0 不进背包、mesher 双 PASS 跳过 + heightmap 跳过）；**渲染走 paintingHost QML delegate**（BillboardQuad + paintingSource(index) pack 命中 / qrc default_painting 回退，贴图不进图集——t717 约定）；state 编码 bit7=锚格/bit[6:5]=朝向(墙面外法线)/bit[4:0]=画 index（27<32）；放置 = playercontroller 画分支（侧面法线 → 锚格左上贪心扩 maxW（观察者右向 u）+ 逐行 maxH（下）、27 张合格等权随机、锚格 setBlock + 非锚格 setWaterSilent 静默多格写；放不下挥臂不消耗）；raycast/selection 贴墙薄盒（可瞄准破、格空气穿过）；BlockRegistry::paintingSize 27 张格尺寸表单一权威 + ResourcePackManager::paintingWidth/Height QML 桥）
**t721** 画作破坏与掉落：挖画掉 PaintingId 掉落物；画下墙破/支撑墙破 → 画掉落（挂 blockBroken 链）。多方块画（2×2 以上）存档 state 编码 (index, w, h, 朝向)。 ✅✅ 已完成（commit c7e07cd：finishMiningAt 画特判——破任一格按 state face flood-fill 同面连通域（±u/±Y，尺寸不进 state 由连通域承载）+ setWaterSilent 静默清余格（防 N 格粒子/音风暴）+ **整张只掉 1 件** PaintingId（drop 标志门控：直挖生存掉/创造不掉）；dropUnsupportedPaintingsAround 挂 blockBroken 链——破墙扫 4 水平邻画解 state 支撑墙 == 破格 → 整画掉落（恒发含创造，t571 自然失撑语义）；画格被实体方块覆盖放置被既有 occ 守卫拒绝（画格非 Air/水/岩浆）；存档 state round-trip（m_states 保真 index/朝向/锚标记，读档 collectBlocksOfId(134) 重建 paintingHost））

### 🅳 铁门与铁活板门
**t722** 铁门：IronDoor 方块（上/下两格放置同木门先例 t674/t620；pack 贴图 door_iron_upper/lower 运行期映射；侧边用铁块贴图同木门用木板先例）；**不能徒手开门**（右键无反应），仅红石信号开（邻格通电 → 开；断电 → 关）。配方：6 铁锭 → 铁门。 ✅✅ 已完成（commit defa12e：IronDoor=135 并入 isDoor 谓词（放置/破坏联动/ShapeDoor 碰撞/cutout 渲染全复用木门机制）；kDefs per-face topTile=176/bottomTile=sideTile=177 + 薄侧边 iron_block(112)（partialblockgeometry door case t674 模式）；右键开合分支显式排除 IronDoor（徒手无效应）；红石挂点=World isPowerFamilyBlock + recomputePowerLocal Phase B 接收器分支（isReceivingPower 上升沿开/下降沿关 bit2 + 配对格同翻幂等静默写）；配方 6 铁锭**竖摆** 2×3→1（与铁轨 3×2 横排包围盒形状区分不冲突）；创造归红石 tab（redstoneIds 135 + creativeBlocks 自动隐藏方块 tab）；icon_iron_door --from-pack；GroupStone 金属音色）
**t723** 铁活板门：IronTrapdoor 方块（同 WoodTrapdoor 几何先例；iron_trapdoor.png 映射）；仅红石驱动开/关。配方：6 铁锭 → 铁活板门（? 核 MC 1.0=6 铁锭 摆 3×2）。 ✅✅ 已完成（commit 902250b：IronTrapdoor=136 复用 ShapeTrapdoor 几何 + state bit0 开合（partialblockgeometry WoodTrapdoor case 并入）；**右键无效应天然成立**（活板门右键分支只认 WoodTrapdoor，铁门铁活板门 fall-through）；红石挂点=isPowerFamilyBlock + recomputePowerLocal Phase B 接收器分支（上升沿开 / 下降沿关 bit0，朝向位不动 → 开门侧固定 +X）；**cutout 段渲染**（iron_trapdoor 178 栅格孔真透明，chunkgeometry isCutoutTrapX 路由——木活板门留 terrain 段零回归）；配方 6 铁锭横摆 3×2→1（MC 1.0 产出 1）——**铁轨配方同时恢复 MC 1.0 形态 6 铁锭+1 木棒（III/ISI）**，因旧铁轨 {Iron:6} 纯横排与铁活板门完全同形冲突（shapedEqual 无法区分），补木棒后 rail/door/trapdoor 三者多重集唯一互不冲突；创造归红石 tab（redstoneIds 136）；icon_iron_trapdoor --from-pack partial trapdoor 形状；lightOpacity 合 15/开 0 同木活板门；GroupStone）

### 🅴 火焰与下界门（余烬门）
**t724** 火焰方块系统 v1：解析 fire_0/1 + fire_layer_0/1 strip（16px 帧 ×32，mcmeta frames 数组已实测 [16..23] 段与 [8..15,0..7] 段）→ 运行期抽帧动画（复用水/岩浆 strip 抽帧管线 extractAnimFrames 先例）；Fire 方块（cross 几何 + cutout + 动画贴图）；打火石点燃：FlintAndSteelId（item，pack flint_and_steel.png 映射，配方燧石+铁锭——本工程燧石=打火石矿? 无则配方=圆石+铁锭 本地化，dev-plan 注明）；点燃：对方块顶/侧面放火。燃烧蔓延：木头制品持续燃烧（相邻木方块随机概率起火→烧毁消失）；烧掉落物/经验球；生物进火格 → 着火效果扣血（接 t715 效果框架 Fire）；**羊/猪/牛着火状态死去掉熟食**（熟猪排/熟羊排/熟牛排——查 loottable 已有 cooked 变体则直接换，没有则加）。 ✅✅ 已完成（commit da99954：Fire id137 cross-quad QML delegate + 32 帧条带 flipbook + tickFire 0.5s 窗（熄 5%/蔓延 5%/上窜 3%/cap 256）+ 可燃表（木族，TNT 排除）+ 生物/玩家点燃 + 掉落物/经验球烧毁 + 打火石 0x121（配方圆石+铁锭本地化、ToolIcon 自绘、创造调色板）；熟食掉落未做（待 t715 效果框架联动，留待后续批次））
**t725** 余烬门（下界传送门）：黑曜石框（最小 2 宽×3 高内圈）+ 打火石点燃内底 → 门方块（NetherPortal，动画贴图 nether_portal.png strip 映射，无程序回退则自绘紫漩涡）+ mcmeta 解析；点燃判定：黑曜石框内圈检测（X 或 Z 向平面，内圈 2×3）；门内持续燃烧不熄（不依赖火）；破坏任一框石 → 门碎。进门的玩家传送（v1 可 stub 为挡+灼伤，dev-plan 注明降级——MC 1.0 下界维度不在本期）。 ✅✅ 已完成（commit 77dda88：NetherPortal=138 lightEmission 11 solid=false maxStack=0 无物品形态，kMcBlockId 90；打火石分支先 tryIgniteNetherPortal（X 平面/Z 平面各试：自点燃格下探 ≤3 步找黑曜石底梁 → 内腔 2 柱（点燃柱 ±u 各试）3 格全 Air → 顶梁黑曜石 → 两外侧边柱高 3 → 填 6 格 state=axis）未中回退普通 Fire；破门格 → removeNetherPortalAt 同 axis flood-fill setWaterSilent 静默清（t721 画模式）；破任一门框格 → breakNetherPortalsAround 6 邻扫整门熄灭（恒熄含创造，结构后果）；站入门格独立 1s 累积器灼烧 1HP/s（不复用 m_fireTimer 随机熄灭路径；**无下界维度** v1 降级已注明）；渲染 portalHost 逐格单片竖直 quad（state bit0 → 绕 Y 0°/90°）NoCulling + **Blend 半透明**（贴图 alpha 155-232 软渐变，非火 cutout Mask）+ NoLighting + portalStripTex 32 帧 150ms 翻书；pack overlay nether_portal.png（16×512=32 帧实测对齐）+ 程序回退 tools/build_portal.py 自绘紫漩涡（帧 0 在底 t563 ② 约定）；mesher 三处 PASS skip + heightmap 双处 skip 同 Fire；tickFire 零交互（flammable 表不含 / m_fireCells 不收 / flint 只点 Air 格不覆写门））

### 🅵 末影之眼链（暗渊之眼）
**t726** 暗渊珠 + 燃烬粉 + 暗渊之眼物品与配方：暗渊珠（夜行者掉落，见 t727）+ 燃烬粉（燃烬者掉落燃烬棒 ×4 粉，见 t728）合成暗渊之眼（珠+粉 无序? MC=珠+粉 shapeless）。item 贴图 pack 映射（ender_pearl/blaze_powder/ender_eye）+ 程序回退。✅✅ 已完成（commit 1aad806：EnderPearlId/BlazePowderId/BlazeRodId 三物品 + 珠+粉 shapeless → EndEyeId 配方 + pack 映射/程序回退；审查修 B4 补 kSmelts 燃烬棒→燃烬粉 +1XP（6535a21）——生存合成链闭合；注：本行 ✅✅ 标记在批次收官时补录（中断窗口遗失，feat 提交早在此前已落地）。
**t727** 夜行者实体（MobNightwalker=16）：三格高（halfH≈1.4）、夜间生成（黑暗刷怪规则同 shambler、地表生成）、贴图 enderman.png+eyes 自拼模型（长臂长腿人形，眼睛独立发光层）；**怕水**：碰水扣血+瞬移逃离；**瞪视激怒**：玩家视线与夜行者面向对视数秒 → 激怒（头嘴张开+头/身体颤抖动画 QML）；激怒后瞬移到玩家背后等 0.5s 攻击；**弹射物免疫**（鸡蛋/雪球/箭/鱼钩命中前瞬移闪避——投射物命中检测跳过本实体+瞬移特效）；近战命中有概率触发瞬移；掉暗渊珠。怪物蛋 + 资源查看器图标（spawn_egg 程序色变体，同既有 mob 蛋先例）。✅✅（commit 7621b2d）EnragedAI 参数：StareTime 2s（眼对眼 dot>0.99 + XZ<32 + mob 面朝玩家 mobDot>0.3，仅最近一只可激怒 nearestEnragableNightwalker 守卫；视线由 PlayerController::setPlayerSight 逐帧注入）；RageTeleportDelay 1s（激怒→瞬移背后，期间张嘴渐开 20°→45°+头 ±0.07 上下抖+身体 ±3° yaw 微抖 0.16s）；Windup 0.5s（背后蓄力）→重拳 6HP；TeleportCooldown 0.6s / dodge 距离带 8-16 格 / 近战 dodge 概率 30% / 弹射物（箭/雪球/蛋）命中即免疫穿透+dodge / 怕水每 1s 扣 1HP+瞬移逃离；死亡掉暗渊珠 50%（0-1）+5XP；死因 Nightwalker「被夜行者重拳击杀」；模型 mobType 16 细长人形（窄躯干+竖头+长臂长腿，halfW 0.35 halfH 1.40）+ 独立眼层（transparent 竖眼贴图 / pack enderman_eyes）+ 夜间 spawn 池 1/4 + 日光燃烧（同 shambler undead 路径）。
**t728** 燃烬者实体（MobEmberling=17）：单头+竖棒环绕旋转（QML 动画旋转棒组）；发射火球投射物（fire_charge 贴图，抛射物砸玩家 → 伤害+着火）；夜间/下界? 本期仅夜间地表生成概率较低；掉燃烬棒。怪物蛋 + 资源查看器图标。✅✅ EmberlingAI 参数：Speed 1.5 blocks/s（慢速悬浮漂移 hover，sin 上下浮动由 QML 动画驱动 ±0.14）；Attack 距离带 [6,16] 格（开火射程）+ Backoff 3 格（玩家贴脸→后退，怕近战）；FireInterval 随机 [2.5,4]s（喷完即冷却）；Fireball 直线弹道 ~8 blocks/s（重力 0，寿命 4s 兜底）命中玩家 → 伤害 5 + ignite 5s（m_fireTimer，t724 点燃先例）+ 死因 Emberling「被燃烬者的火球焚杀」；命中 mob → 伤 5 + 着火（fireTimer=kFireDuration）；命中方块 → 消失 + ~20% 概率落火（Fire 格，t724 火系统）；火力免疫（tick 火烧分支整段跳过，岩浆/火不点燃不扣血）；夜间 spawn 池 1/6（其余敌对摊余）；死亡掉燃烬棒 50%（0-1）+3XP；模型 mobType 17 悬浮单头盒（halfW 0.50 halfH 0.60，kEmberlingHoverOffset 0.4 悬浮）+ Main.qml 4 根竖直烟灰橙棒环绕 Y 匀速旋转 + sin hover；resourcepack {17,"blaze/blaze.png"} + build_mob.py mob_fireball.png 程序贴图。
**t729** 暗渊之眼实体：右键扔出 → 实体飞向最近 Stronghold 传送门（placeStronghold 位置已知）方向，飞行若干秒后：80% 变掉落物、20% 碎掉（碎裂粒子无掉落物）。实体贴图同 item（自绘/映射）。指引半径限制（飞离玩家 ~10 格后判定）。✅✅（commit 33b067b）EnderEye 参数：纹理 entity_endereye.png（build_mob.py 程序生成 16×16 小绿瞳珠：珠绿 #2f9f6f + 暗绿竖瞳 + 白高光；pack 命中 item/ender_eye.png 时切包内 item 贴图）；Speed 4 blocks/s 直线朝强传送门中心格（World.strongholdPortalX/Y/Z getter 记录于 placeStronghold 放置处（cx, cy+4, cz-18）；Game 层 playercontroller 据眼位→传送门方向归一化 × speed + 略向上偏置 0.25）+ C++ tick 再加 kEnderEyeRiseOff 0.25/s 略升；Dist 随机带 [10,16] blocks（~2.5-4s 飞行）后判定：80%（kEnderEyeDropChance）→ emit enderEyeBecameItem(int x,y,z) → 呈现层转发 ItemEntityManager.spawnItem(0x23A EndEyeId ×1) 生成**掉落物实体**可捡回（反复使用逐步逼近要塞）+ 移除；20% → 碎裂态（enderEyeShatter 倒计 kEnderEyeShatterTime 0.6s，呈现层 burstGlassShatter 珠绿玻璃碎屑 + delegate 缩小 shatterScale→0 + 淡出 opacity→0，归零释放槽**无掉落**）；实体 Kind::EnderEye（QML delegate 小绿瞳珠 ~0.28 立方 + 慢自旋 + 碎裂动画；shatteringAt(i) 供 delegate 判碎裂窗口）；右键消耗 1（生存）创造不耗；玩家多次使用可逐步逼近（单要塞方向恒指向它）。

### 🅶 鱿鱼、皮肤、杂项贴图
**t730** 鱿鱼贴图接入：MobSquid=9 已有 AI（t399 喷水推进）但贴图占位——squid.png 自拼 3D 模型（头+8 触腕盒体组）+ 怪物蛋 + 资源查看器图标。✅✅ 已完成（commit b261a45：mobEntityMap 加 {9,"squid/squid.png"}（demo 包实存扁平 entity/squid.png 512×256 = base 64×32 的 8×，mobTextureSource 两级探测扁平回退命中）；MobModel squid 分支补 pack box-UV——mantle 12×16×12 @ (0,0) 六面像素区实测 100% 不透明 + 尖顶复用 mantle 区 + 8 触腕共用 2×12×2 @ (48,0) 单区（vanilla 单触腕区复用，像素实测验证）；Main.qml mobSquidPackTex + delegate packTextured + baseColorMap 切换（t597 近白 tint 规则）/ pack 关回退 mobSquidTex 程序贴图零回归；怪物蛋 0x22E 与 ResourceBrowser 图标 t717 已备，未动）。
**t731** 玩家皮肤：steve.png/alex.png 运行期映射（playerModel 第三人称贴图，程序回退=现纯色）；皮肤选择（设置或 /skin 命令 v1）。✅✅ 已完成（commit 780f8a6：ResourcePackManager playerSkin/setPlayerSkin（settings.json 持久化）+ playerSkinSource（pack steve.png/alex.png 两级探测 + 64×64 老式布局裁上半 32 行落盘缓存，miss 回退 qrc 程序皮肤）；Renderer 新 PlayerSkinBox 几何（piece 0-3 盒区 + subV0/1 腿分段采样，同 ArmorLayerBox box-UV 公式）；Main.qml playerModel 全部件换 PlayerSkinBox + playerSkinTex（pack 命中/ qrc 回退两态，hurtTint 近白 tint 乘子保受伤红闪，旧眼子 Model 移除）；CharacterPreview3D 同套换装即时跟随；commandRegistry 加 /skin default|alex（持久化 + 即时换肤 + 中文回显）；第一人称 viewmodel 纯色未动（范围外））。
**t732** 矿车 + 附魔书 3D 贴图接入：minecart.png（矿车斗形重贴图，替换程序纯色）+ enchanting_table_book.png（附魔台悬浮书重贴图，替换 t679 程序书模型贴图）。✅✅ 已完成（commit 4285ad6：新 MinecartBox（piece 0 底板/1-2 左右纵帮/3 端帮 × 六面像素矩形）+ EnchantBookBox（0 封面/1 纸页/2 书脊/3 翻页片）双几何类，各带 layout 0=qrc 程序布局 / 1=demo 包布局双表——像素实测发现包/程序两贴图分区结构不同（包侧壁双壁窗+亮卷边 vs 程序侧帮带），QML 按 entitySource("minecart")/("enchant_book") pack 命中态切布局（MobModel.packTextured 先例），miss 回退 qrc；cartHost 5 块 UnitCube 纯色全换 MinecartBox + 双 Texture；bookDelegate 页/书脊/翻页片换 EnchantBookBox，t697 GlyphLines 叠层撤下（贴图自带符文，叠层错位重影）；骑乘物理/翻页浮沉动画/bookHost 事件机制零改动；Core 零改动——t717 entitySource 两级探测已备）。

### 📎 R19.10 范围与顺序
t717-t732（16 项）。**严格顺序：t717 贴图批 → t718-t719 盔甲 3D → t720-t721 画作 → t722-t723 铁门/活板门 → t724-t725 火焰/余烬门 → t726-t729 暗渊链 → t730-t732 贴图收尾**。每项独立 commit + dev-plan ✅✅。全部完成后 code review + 统计报告。

> **✅ R19.10 批次收官（2026-08-21，HEAD 16bca9f）**：16/16 任务完成（16 feat 提交）。两轮审查全修复——首审 Review_2026-08-21.md（t724–t729，B1–B13：5 高 3 中 5 低）三段落地 6535a21/0ca6d61/89d12a4（B5 改走 EndPortal(111) 框架簇反推方案，理由：门面仅激活后存在，未激活存档扫门面恒失效）；终审 Review_2026-08-21_final.md（t717–t723 + t730–t732 + 修复复核，0 高 2 中 6 低）两段落地 12a8d08/16bca9f。全批 34 提交 / 110 文件 / +6572 −395；所有构建零警告。遗留人工验证项见两份 Review 的回归测试清单（并排画破坏、燃烬者对峙 20s、熔炉烧棒链、掷眼读档、日光下夜行者、/skin 换肤即时生效、矿车/书 pack 双态贴图）。

# R19.11（用户实测复盘大修批：红石物理与矿车 / 贴图统一原则 / 生物图鉴 / 成就 UI / 死亡与出生点）

> **统一贴图原则（本批总纲，t745 建机制、全批贯彻）**：pack 启用 → 所有已放置方块的 3D 模型**与**背包/物品栏/创造调色板的 item 图标均按 pack 贴图渲染；pack 关闭 → 全部回退程序原生形态。适用一切「放置态 pack 化但图标还是旧贴图」的方块（草/泥土/石头/圆石/原木/木板/矿石/树叶等）。工作台与熔炉现状已达标（参照物）。

### 🅰 铁轨与矿车物理（t733-t737）
**t733** 铁轨支撑依赖：挖掉铁轨（普通/动力/探测三族统一）底部方块 → 铁轨不再浮空，直接破坏并掉落为掉落物（同火把/红石粉支撑语义；验收：任意族铁轨下方被挖/被炸后铁轨成掉落物，无浮空残留）。✅✅ 已完成（commit a7d5e53：**架构抉择=World 层单点收口**——checkRailOnEdit（t565）本就挂在全部 5 个写入入口（setBlock×2/clearBlockSilent/setWaterSilent/destroySphereSilent 逐体素尾），加一个失撑分支即覆盖全部 7 条破坏路径（挖掘/爆炸三路/TNT 变实体三路/水淹）且未来新静默清路自动继承，优于 t744 式按路径补扫；先例=checkPressurePlateOnEdit t494；支撑判定 isTopFlushSupport（t741 单一权威）；先掉落（静默清+blockBroken+blockDroppedAsItem 掉自身 state 丢弃）后重算连接位；直挖铁轨本体守卫不双掉；泛型化五个 dropUnsupported* 为跨层重构搁置并注明；redstone_matrix_test 98 PASS/0 FAIL（新 P10：三族挖掘/爆炸边界/TNT 点火/上半砖边界/零双掉守卫））。
**t734** 矿车贴轨修复：现状矿车悬浮在轨面上方约一整格（同雪傀儡悬浮积雪层的同类 bug——restY 基准算错）→ 矿车紧贴铁轨上表面运行；同时修复「矿车不在铁轨上仍可移动」：离轨矿车不可被推动/骑行位移（现状可悬浮一路滑到地图边界），放置在非轨面时即静止待拾取（验收：轨上贴轨、离轨静止）。✅✅ 已完成（commit ce4ca5f：**双重根因**——①物理侧悬浮=轨薄板 mesher 画在轨格 cell 底+1/16（partialblockgeometry:633）而车公式当格顶多叠 +1.0 → 修 kCartRideH 0.30→0.225（板顶+底板 0.15+防 z-fight 0.0125），平地/爬坡两态贴轨（同族=primed TNT 9116856 基准 bug）；②离轨滑行=**stepCartAlongRail 跨格分支自 t565 起稳定帧率下是死代码**（段长恒>0.5 vs 步长 0.06-0.21，连接重选/拐弯/尽头停永不触发）→ 改「行进向上前方最近格心」逐格重选（帧率无关）+ 格心起步先验 + ε 残差吸收；③tickRiddenCart 离轨钉停（speed=0 跳过全部输入物理，可下车/拾取）；④放置放宽：命中轨→轨上放，命中非轨→邻格地面放置静止待拾取；骑乘玩家脚=车心-0.3 不动随车自然落座；redstone_matrix_test 98 PASS/0 FAIL）。
**t735** 矿车交互与碰撞：① 创造模式手挖矿车 → 掉落物（现状不掉）；② 生存模式矿车有耐久：需打多下，受击摇晃动画，最后一击变掉落物；③ 矿车↔矿车碰撞、玩家模型↔矿车碰撞（实体互推）；④ 被撞击获得速度后的运动学：动力铁轨上保持前进直到离开动力段，普通铁轨上摩擦衰减慢慢停下（速度计算与传递，车撞车传递动量）。✅✅ 已完成（commit 67ce697：**①创造"不掉"真凶与船 t661/t711 同族**——掉落物落攻击者 1.5 半径内免拾窗一过被 pickupScan 秒吸回=观感不掉 → 修为非实心水平邻格随机散布（4 邻全实心退 y+1）；②Cart.hp=3 击+revision bump→QML hpAt 绑定触发横滚摇摆动画（9→-7→4→-2→0°衰减，cartHpSeen 防槽复用误触；MC 一击毁为用户明确要求的有意偏差已注明）；③车-车 O(n²) 水平 AABB+Y 层筛（kCap 64 上界），动量一维沿轨向近似（closing×0.85 投影各自轨四向——同轨追尾全额/道口 ±0.707 部分/垂直穿过不飞轨，注明简化），去穿插沿轨轴推半穿透量防顶出轨道；玩家-车双向（人推车=既有 pushEmptyCart，车推人=2.0 冲量入 m_knockback 通道≈0.44 格不伤害）；④tickPushedCarts 内通电动力轨（OnFlag）摩擦不衰减 lerp 到 boost 12.8/否则 kCartFriction exp 衰减+下坡不衰减，静置车不被弹射（t708 起步闸门保留），**空车吃 boost=t708 语义变更已注记（t736 需按新语义对齐）**；redstone_matrix_test 98 PASS/0 FAIL）。
**t736** 探测铁轨信号：矿车（含空车）驶过探测铁轨 → 该轨发红石信号激活相邻红石装置（红石粉/红石灯/铁门等，接入既有红石供电查询表；车离开→信号断开；验收：探测轨+红石灯串可见亮灭）。✅✅ 已完成（commit 5c9a2c9：bit4 DetectorRailStateOnFlag 复用确认（t691 已统一视觉/电力同位——任务描述的 bit0+定时清位为陈旧信息）；**真缺口=空车半边+统一收口**——旧版只标被骑路径（空车永不触发），且骑乘帧 tickRiddenCart 先清占用表再只标被骑车→邻轨空车占用每帧误判离开沿电力抖动；被骑车挖毁早退不收边沿→探测轨永久带电泄漏 → 新 updateDetectorRailOccupancy 统一 pass 收口 tickPushedCarts 末尾（骑乘/非骑乘帧必经），全部活体车列内向下扫最近轨格，幂等守卫（位变才写，驻轨期间每帧零 state 写）；车离开沿清位→tickRedstone 降沿断电；P12 真实实体驱动探针（空车驻轨亮/稳态 20 帧零写/推离灭/被骑回归）4 断言；redstone_matrix_test 103 PASS/0 FAIL）。
**t737** 铁轨转弯修复：普通铁轨转弯贴图方向现在左右反（前左转显示成右转贴图，反之亦然）→ 修 state→贴图映射；且矿车在弯轨上必须跟随转弯（现状直行穿出），骑乘与空车都要转（验收：铺环线矿车绕圈不脱轨）。✅✅ 已完成（commit 576ae86：**四象限全反根因=t565/t666 编码前提「UV v=1=贴图底行」错误**——本引擎 v=1 采样图顶行（草侧贴图绿带在图顶行铁证），即 v↔z 关系四格全编反=每格显示南北镜像；t620 pack 镜像基准像素级正确无需动，错全在 mesher 表 → 废弃四象限翻表，改 Core 层 railCornerArms 单一权威纯函数（连接位→两臂符号）出臂向推导贴图象限，矿车侧 pickTrackStep 同源等价（P11 逐拐角断言锁死）；**车转弯 t734 已真修好**（骑乘绕圈 295 格/149 转弯实证），本次修复点=空车过弯 yaw 不转 + 两处 dir 重选点同步 yaw（cartYawFromDir 幂等公式统一）；P11 环线探针（3×3 环：连接位/贴图肘角/骑乘绕圈/空车 yaw 覆盖 4 基数向）；redstone_matrix_test 99 PASS/0 FAIL；GUI 弯轨观感需人工目测）。

### 🅱 红石组件与机关修复（t738-t744）
**t738** 红石火把墙置：现状只能放方块顶面且悬浮不落地 → 参照火把（Torch）的墙面附着逻辑，红石火把支持四面墙插 + 顶置，模型紧贴附着面（验收：墙插/顶置/拆支撑块掉落为掉落物）。✅✅ 已完成（commit d9fde48：**根因=t705 渲染把 torchAttachOffset（指向支撑格向量）取反当沉向**——贴地被抬 +4/16 悬空、墙插被推离墙穿界 → partialblockgeometry 重做（贴地满格居中 cross；墙插两片含 30° 倾轴 quad，常量与普通火把 QML delegate t150e/f 同源；亮 161/熄 170 双态同几何）；放置复用 TorchAttach 0..4 bit0..2 + 附着邻 isSolid 守卫（拒 cross/非实体面/天花板悬空，普通火把同分支）；墙态命中盒贴支撑侧半区（旧中央盒墙向全错过）；爆炸失撑 dropUnsupportedTorchesAfterBlast（Stalker/TNT 共用，恒掉落 explosionDroppedItem 通道）；redstone_matrix_test 94 PASS/0 FAIL（t740 语义零回退）；墙面观感建议人工复核）。
**t739** 红石粉路径与支撑：① 爬坡布线：越过一格高方块时不直连斜穿，先贴方块边缘水平延伸、触到方块侧面再竖直爬升（阶梯形路径）；② 支撑：红石粉不得浮空——底部方块被挖 → 红石粉立即变掉落物（激活态同样掉落），信号传导网络即时更新失效段；③ 放置面规则：上半砖（top slab）**可以**放置红石粉，下半砖（bottom slab）不可以（现状按此修正）。✅✅ 已完成（commit f200c85：单一权威谓词 isDustSupport（isFullCube ∨ isSlab∧bit0）统一放置守卫；爬坡=纯呈现层——删旧 t702 斜面直连，线臂贴地平铺 + 被爬边另发竖直 quad 贴方块侧面（内缩 1/64 防 z-fight）拼 L 形，BFS/连接位t/give42
1
零改动（t738/t740 语义零回退）；失撑掉落双路：挖掘 finishMiningAt→dropUnsupportedDustAbove（恒掉 0x224）+ 爆炸 dropUnsupportedDustAfterBlast（Stalker/TNT 共用）；redstone_matrix_test 95 PASS/0 FAIL（新 P10 阶梯探针：电力 15/14/13/12 + 连接位 + 灯亮）；上半砖顶与格顶齐平渲染零改动）。
**t740** 红石激活器件全量排查：现状红石火把与红石粉都不能激活 TNT → 修通；并**系统性排查所有「可被红石激活」的器件**（TNT/铁门/铁活板门/红石灯/音符盒?/发射器族/动力铁轨等）× 所有「红石信号源」（火把/红石粉/拉杆/按钮/压力板/探测铁轨/红石块）的激活矩阵，缺一个补一个，输出核对表进 commit message（用户明确不想逐个人工验证——本任务一次交齐）。✅✅ 已完成（commit 3686e27：新增无 GUI 矩阵 harness tools/redstone_matrix_test.cpp + CMake 控制台目标，84 格矩阵 12 源×7 接收器全 PASS；根因实证——主链（火把邻 TNT/粉传导）t706/t707 已通，用户报告疑含陈旧 exe 因素，**真缺口=火把立方块上贴地斜角环粉收不到电**（v1 仅 6 正交播种）→ BFS 播种补火把斜下 4 格喂粉，连带修 attachPowered 环粉自反馈振荡 + 拆把降沿失达；红石火把熄灭位/粉连接位等有态接收器降沿清位另验；音符盒未注册无此块；矩阵表全文附 commit message；经验教训：声称完整的系统要配可执行最小复现 harness——84 格矩阵 15 分钟实证完，静态核对数小时仍漏判真因）。
**t741** 铁门放置与图标：① 放置校验：铁门只能放置在完整方块或上半砖上方（现状可悬空放在红石粉/铁轨上方）；② item 图标修复：现图标上下两半中间断开成空气 → 修为完整门形（放置的 3D 模型已正确，仅图标错）。✅✅ 已完成（commit c62b374：抽通用谓词 isTopFlushSupport（支撑面与格顶齐平=isFullCube∨isSlab∧bit0），isDustSupport 委托它（t739 调用点不动），门族放置统一走新谓词——**木门/云杉门同病一并修**；校验落 placeBlock isDoor 分支 setBlock 前静默拒（同火把/铁轨先例），已放置门与存档读回不受影响；图标根因=生成器硬编码 cy 基线致两半中缝恒 0.15W≈10px 空气——改几何常量推导（共享缝 y=1 + 整门竖直居中反解），重生成 icon_iron/wood/spruce_door.png 三张，逐列验证内部零空隙不裁边；redstone_matrix_test 全 PASS（铁门×11 源+降沿零回退））。
**t742** 铁活板门修复：① item 图标：现像「厚铁压力板」→ 改为带四个孔的活板门造型（放置态 3D 模型已正确，对齐它）；② 渲染：孔洞背面有方块时现在全黑、只有从下往上看缝才透光 → 孔应为通透（能看到后面方块的贴图/天光）；③ 侧面贴图：前后左右四侧面应使用铁块贴图，而非六面全是活板门贴图。✅✅ 已完成（commit 76c52b7：**全黑真凶=合态 lightOpacity 15 压死孔后天光**（非面剔除——活板门 solid=false 孔后面本就在画，光照 flood 到 kVcMin 才黑）→ IronTrapdoor 恒 lightOpacity 0（木活板门原样）；图标根因=旧 partial 模式把孔填均色 → 新 trapdoor FROM_PACK 模式（top=pack iron_trapdoor alpha=keep 保孔透底 + 两侧 iron_block，--from-pack 按名过滤点名重生成）；贴图孔阵改 2×2 四孔（与 pack 孔位同构，旧 6 孔）；侧面=铁块贴图复用 t722 铁门薄边 pushBox sideTile/sideLargeAxis per-face 机制（合/开态各自轴向正确，木活板门 sideTile=-1 零回归）；redstone_matrix_test 95 PASS/0 FAIL）。
**t743** 红石灯与压力板联动：① 压力板放置在红石灯正上方 → 激活红石灯（向下供电路径）；② 木压力板被**掉落物**压住时也要触发（现状仅实体生物/玩家触发，掉落物放上无反应；石压力板维持仅生物触发语义）。✅✅ 已完成（commit 2019ee4：**①链路本就通**——新竖直板压灯探针实证 setBlock 写板→notePowerWrite→tickRedstone→灯亮/灭全链（t743 前从未生效的真因在②）；**②根因=掉落物触发查错格**——掉落物静止中心=支撑格顶+0.3，floor(pos.y()) 恒为板上空气格 → 修正查支撑格 -1 + 新增 restingAt 着地门控（飞行掠过/浮水不误触；拾走出表松板无沿风暴）；t627 金板"仅掉落物"同根因一并复活；石板对齐 MC 1.0 收紧仅玩家+mob（pressurePlateAccepts 单一权威，木/圆石保留物品灵敏）；灯手动态/电力态=t658 已删右键开关电力唯一驱动，仅注修订；redstone_matrix_test 96 PASS/0 FAIL）。
**t744** 按钮/拉杆修复：① 支撑失效：按钮或拉杆放在 TNT 上、右键激活 TNT 后，TNT 已变实体掉落 → 按钮/拉杆失去支撑应立即变掉落物（现状留在原地悬空；支撑方块消失的通用掉落规则，同 t733 族）；② 地面放置形状：木/石按钮放在地面上现状变正方形 → 应为贴地的扁薄盒（墙面放置的长方体形态正确，仅地面态错）。✅✅ 已完成（commit b3f4e9a：①根因=三条 TNT 引燃路径（打火石右键/scanTntTraps 压板/firePowerTnt 红石）全走 clearBlockSilent+spawnPrimedTnt 绕过 finishMiningAt 的 t662 失撑扫描 → 三处各补 dropUnsupportedMechAround + 爆炸路径新增 dropUnsupportedMechAfterBlast/AfterCell（Stalker/TNT 陆地/水下链式三路，isFullCube 支撑判定与放置预检对齐）；②**地面几何 t662 起本就正确**（顶点 dump 实证 6/16×2/16×6/16 Y[0,2/16]，用户报告疑含陈旧 exe 因素同 t740）→ 不改几何锁进 P9 回归探针（30 组态+12 断言）防回归 + 勘误 blockregistry.h 镜像面注释（照旧注释写代码会复现墙背悬空 bug）；redstone_matrix_test 97 PASS/0 FAIL）。

### 🅲 材质包统一与图标（t745-t746）
**t745** 方块 item 图标 pack 化统一（总纲机制落地）：创造背包/物品栏图标现状仍用旧程序贴图（草方块/泥土/石头/圆石/橡木原木/橡木板等——放置态已是 pack 贴图）→ 建立「pack 启用=图标也按 pack 贴图渲染，pack 关=回退程序原生」的统一机制；矿石图标一并修（煤/铁/铜/金/红石/青金石/钻石——旧版贴图 + 创造调色板排序不按此顺序 → 修排序）；机制建立后**全量扫描方块清单**找同类不一致并统一（用户只点名了部分，要求整体统一；工作台/熔炉已达标为参照）。✅✅ 已完成（commit 69476c6：**四层回退链**①既有 2D pack 立绘（床/门/梯/铁轨/红石火把/叶）→②运行期 blockAtlasIconSource(id,true) 从合成图集（程序瓦片+任意启用 pack 覆盖+tint）按 def.shape 泛化投影渲染（整立方/台阶/楼梯/栅栏/压力板/门/活板门/积雪+仙人掌柱/附魔台/铁砧特型；cross 走 flat；pack 逐像素比对确认覆盖才有产出）→③FROM_PACK 族程序重渲→④qrc 手绘；缓存文件名带 revision（切 pack QML 按 URL 重载）；10 个 QML 文件图标绑定补 resourcePack.active 守卫（AOT 安全，此前仅 HUD 两处）；矿石段排序 煤→铁→铜→金→红石→青金石→钻石；工作台/熔炉双态=pack 关时运行期从程序图集重渲程序原生（不做 git 恢复）；实测 92 方块走新路径（日志文件实证），按钮/红石粉保持手绘（共享瓦片会误导，注释钉死）；树叶留 t746；红线 §9 派生缓存不进 VCS 注释写明与 t644 区别；redstone_matrix_test 103 PASS/0 FAIL）。
**t746** 树叶修复（橡树+云杉两族）：① item 图标：现状 2D 平面观感 → 改 3D 方块图标并 pack 化（同 t745 原则）；② 放置态透明度：现状透明度过高，树叶放地上可透视穿透看到底下方块（穿墙透视级）→ 调整 alpha/cutout 阈值至正常（pack 态与程序态都要验）。✅✅ 已完成（commit 3d4fa88：**透视真凶=叶 solid=true 的边界双侧面互剔**（叶底面被地面剔+地面顶面被叶剔→边界零面→叶孔直通地形 void 到洞穴顶才挡=穿墙透视级；阈值/孔密度/lightOpacity 都不是主因全不动，守 t639 教训零扰动）→ 新增 occludesNeighborFace() 邻面遮挡判定（叶对邻面不遮挡=fancy 叶语义，叶自身面对实体邻仍剔防 z-fight），替换 4 处剔除点（terrain 双路+流体顶底/侧面）；图标 2D 根因=pack 态命中 blockItemIconMap 2D 立绘染色 + pack 关 qrc 图本就是 pack 风烘焙（双态都违原则）→ 叶两族改 blockAtlasIconSource ②③层 3D 投影（AtlasIconSpec.solidify 不透明均值色填孔实心化，对齐离线 load_face 约定），入 isPackDerivedIconFamily，qrc 图降④层兜底；树冠取舍：叶-叶间面双侧绘制（fancy 通透感换面数，贪婪合并部分抵消），孔密度保留 oak 22%/spruce 11.5%；redstone_matrix_test 103 PASS/0 FAIL）。

### 🅳 玩家皮肤修复（t747-t748）
**t747** 皮肤贴图修正：① 头部耳朵区域贴图前后反（眼睛/嘴巴区域正确，仅耳朵错）→ 修 PlayerSkinBox 头部盒区 UV 或贴图绘制朝向；② 清晰度不足 → 换高清版本（程序贴图升分辨率重绘，UV 分数坐标不变分母比例；pack 侧沿用 pack 原图分辨率）。✅✅ 已完成（commit 47e11d9：**环缝连续性双证**——真实包 alex 额发跨界渗入两侧脸区证明六面是环形连续条带（前缘=鬓角/耳发贴脸区边界），推导左手系 +X=角色右 → +X 面采 MC −X 区前缘在大 u 端而旧表把前角接 u=0 → **仅 +X 面 u 向反**（−X 面前缘恰在小 u 端故左脸一直正确，与"仅一侧耳反"症状精确吻合）→ kFace 仅 0/2/5 三面交换角点 u（pos/winding 不动剔除零回归）+ 全环缝连续性证明（四缝+顶前缘行）；②128×64 真重绘（_weave 布纹/_strands 波浪发丝/眼白虹膜瞳孔高光/领口背缝/鞋带鞋底，非最近邻放大），UV 分数分母钉 base 布局零 C++ 改动，MC 区→HD 列反算核对+ASCII 鬓角落位实测；其余 6 张实体贴图字节级不变（git 证实）；hurtTint/腿分段//skin/预览/pack 128×64 与 128×128 裁切全回归 ✓；redstone_matrix_test 106 PASS/0 FAIL）。
**t748** 潜行头部跟随：按住 Shift 潜行时玩家头部不再跟随视角旋转（被固定）→ 修复潜行姿态下的头部 pitch/yaw 绑定（验收：站立/潜行两种状态下头部都平滑跟随视角）。✅✅ 已完成（commit b62ef28：**根因=坐标系前提错**——t66 时代 headNode.eulerRotation.x=clamp(pitch,±60) 是身体系角度，t71 把 headNode 迁入潜行前倾的 upperBody（−crouchBow=−35°）后鞠躬量被烙进头的世界俯仰（头世界 pitch=pitch−35，±60 颈钳低头半段饱和）→ 观感潜行锁头（站立 crouchBow=0 不受影响，与"仅潜行锁头"症状精确吻合）→ 本地角补回 `clamp(pitch+crouchBow,±60)`（蹲/站两态头世界 pitch 恒=视线 pitch，潜行只管身体前倾/髋下沉，MC 蹲下头仍自由看）；yaw 无此问题（root 绑 player.yaw）；CharacterPreview3D 同结构蹲姿镜像同款补偿；站立态数值逐位同旧版；redstone_matrix_test 106 PASS/0 FAIL）。

### 🅴 资源查看器生物图鉴（t749-t751）
**t749** 生物头像图标补全：现状空白头像——鱿鱼/狼/豹猫/雪傀儡/蠹虫/夜行者/燃烬者 → 补齐；羊头像正确但 3D 预览贴图找错（完全不像）→ 换正确羊贴图；被剪羊毛的羊贴图纯色错误 → 重绘（剪毛羊应有裸皮+残毛造型）。✅✅ 已完成（commit 416dc1a：**空白根因=双重**——mobHeadRegions 表缺全部 7 条（mobHeadIconSource 恒空串）+狼/豹猫/蠹虫另缺 mobEntityMap 映射（回退色块缺省白读作空白）→ 7 条逐条像素取证补齐，狼/豹猫/蠹虫走 explicitSrc 头像专用源（其 MobModel 无 setMobTex UV 数据，进 mobEntityMap 会 3D 垃圾采样，刻意隔离注明）；**羊错因=mobTextureSource(3) 返 sheep_fur.png 毛层**（头区是膨胀毛盒布局且无脸）→ 运行期合成贴图（fur 毛身/腿+本体层 sheep.png 真脸区，两层身体/腿行逐像素验证布局一致，BuiltState 缓存 apply 重建）——游戏内羊同路径顺带修正；剪毛羊=entityKindMap sheep_body 双态（pack 本体层/程序 mob_sheep_sheared.png 裸肤+皮肤斑驳+残羊毛块，make_sheep_sheared 程序画）——图鉴预览与游戏内 delegate 共用；原 10 mob 头像/刷怪笼条目零回退；redstone_matrix_test 106 PASS/0 FAIL）。
**t750** 图鉴 3D 模型修复（根因方向：图鉴预览疑似未复用游戏内 mob delegate 而用了占位套皮——统一改为复用/对齐游戏内模型与贴图）：① 鱿鱼：大鱿鱼正确但头顶叠一只小鱿鱼 → 去掉叠加层；② 狼：现模型像兔子（贴图找错）→ 重做；③ 豹猫：模型不对、没有脸 → 重做；④ 夜行者：现状猪模型套皮+黄色末影贴图残留 → 改为三格高细肢人形（僵尸式正常躯体非骨架、不持弓、手臂与腿细但连接躯体，对应游戏内 t727 模型）；⑤ 燃烬者：现状猪模型+多余白色身体 → 改为悬浮单头 + 若干根带贴图的烈焰棒竖直环绕身体旋转（对应游戏内 t728 模型）；⑥ 蠹虫：长方体身体太「可爱」平滑 → 增加凹凸/分节造型。✅✅ 已完成（commit cac273d：**侦查修正——图鉴早已传 mobType 且 MobModel 有 16/17 专属分支**（"猪套皮/黄末影"疑旧构建观察），真缺口=图鉴漏装饰子层；**双层修法**：几何级下沉 MobModel 共享单源（鱿鱼尖顶盒全脸态每面铺整图=头顶小鱿鱼读数 → 尖顶仅 pack 态保留+全脸态省略+黑点眼双态规则镜像 t730 L5；蠹虫 14 分支重做三节体节+背脊甲板+尾须（头/腿/眼/碰撞零动，游戏内同步受益）），装饰层浏览器 1:1 复刻（狼尾+眼/豹猫眼/夜行者紫眼层+暗唇/燃烬者 4 根 #e8b030 竖棒 2200ms 绕 Y 旋转——复刻先例 t598/t616/t663，Main.qml 锚点互指；抽共享组件需全量参数化 17 mob 成本/风险超收益，抉择写明）；redstone_matrix_test 106 PASS/0 FAIL）。
**t751** 羊/雪傀儡变体切换 UI：生物条目不再拆成两个（剪毛羊独立条目合并）；在右侧浏览界面底部加**变体切换按钮**：羊——剪毛/未剪切换 + 不同羊毛颜色选择（仅羊有颜色变体）；雪傀儡——戴南瓜头/剪掉头切换，并修复「剪头后雪傀儡下半身」模型错误（参照正常戴头雪傀儡下半身——那是正确的）。✅✅ 已完成（commit a80acf8：条目合并单条+组件级变体态（sheepSheared/snowGolemSheared/sheepWoolIndex，NOTIFY 即时刷新贴图/头/名行后缀「（剪毛后）·橙色羊毛」）；底部切换 UI（两段 toggle 金边语言+16 格毛色 swatch 行 HoverHandler 复用 tooltip+注记行「毛色为图鉴预览着色·游戏内羊染色待后续」诚实边界；其他 mob 零占位）；毛色=baseColor tint 乘色（t597 同款，16 色与 build_wool.py WOOL_COLORS 同源核对，仅毛茸态着色裸肤不染，蛋路径不 tint）；**剪头下半身根因=t663 绑定把一切剪后变体身体贴图剥成纯白**（t749 重写已恢复，t751 用注释钉死不变式「身体路由=f(mobType,pack 态) 与变体无关，唯一例外=羊裸肤」防回归）；redstone_matrix_test 106 PASS/0 FAIL）。

### 🅵 成就系统（t752-t754）
**t752** 成就树结构与布局：① 农夫（制作锄头）改为有前置「耕种时间到」，挂在合成台分支下、与「出击时间到」「挖矿时间到」并列；② 材质放宽：农夫=任意材质锄头（不限木锄），出击时间到=任意剑（不限木剑）；③ 布局 bug：挖矿时间到→附魔师展开双分支后，并列行上下两条路线不对齐（下方占比多、更贴近）→ 修树形布局对齐算法；④ 无后续子项的成就不再显示 +/- 展开占位符（神射手、启航、发射——等真有后续再加）。✅✅ 已完成（commit 676b843：新 time_to_farm「耕种时间到」=合成任意锄头（合成台第三分支 DFS 先序与出击/挖矿并列）+farmer 重挂其下（独立根仅剩起航/发射!）；**材质放宽=onCraft 按 ToolRegistry type 判**（挖矿/出击/耕种 全 6 材质 type==Pickaxe/Sword/Hoe，替旧单 wood-id 等值；获得升级仍限石质不变；描述去「木」字对齐）；**③布局根因=父行取叶子跨度中点且整除截断**（子树行数不均父被大子树拉偏：上路距 1.0 行/下路 0.5 行=用户看到的下方贴近）→ 父行=首末子中心中点恒对称 + row 升 qreal（.5 半行）+ treeRows real 防画布截断，C++ achievements() 与 QML visibleTree 镜像同步；④hasKids 显式 false 钉死叶子无 +/-（t678 ≥2 真分叉规则保留后继自动放行）；旧档 loadVariant 直插解锁绕父检查兼容；redstone_matrix_test 106 PASS/0 FAIL）。
**t753** 成就导览 minimap：导览界面右上角加缩略导航图（灰色小型总览，同地图软件 overview）：显示整棵成就树的全貌范围 + 当前视口所在位置的矩形框（可选加分：点击 minimap 跳转视口）。✅✅ 已完成（commit 5cfe06d：Main.qml 单文件 +131 行 treeMinimap（160×120 锚视口右上 z=5）三层绑定驱动——连线 Repeater（主画布三段公式×fitScale 浅灰/灰绿两档）/节点点 Repeater（钳 2..5px，已解锁 #a8c8a8 微亮/可进行/锁定三态）/视口框 #ffd76a 亮描边实时绑 contentX/Y/viewScale；映射=复用 t637 viewport 公式反解视口树坐标区 + 整树 bbox 等比 fitScale 居中；**跳转已实现含拖动 scrub**（jumpTo 反解→contentX 平移 clampPan 钳界，缩放档不动；minimap 自有 MouseArea 不误触树拖拽、滚轮向下传播缩放不失效）；redstone_matrix_test 106 PASS/0 FAIL）。
**t754** 成就界面返回按钮：最底部返回按钮现靠左对齐 → 改居中或右对齐（对齐项目既有 UI 惯例选定其一）。✅✅ 已完成（commit 8d15f2c：**根因比猜测深一层**——anchors.horizontalCenter 早就写了但按钮仍在 Column 声明体内（t678(e) 只改锚没移出）→ 锚到祖父 progressPanel=非法锚运行时静默忽略 → x 回退 0=Column 左缘+16px；修法=Rectangle 真移出 Column 成 progressPanel 直属子，居中+贴底 8 双双合法一次生效（括号平衡脚本证实父链）；惯例调研=暂停菜单族多数派居中（选项面板 :10166/统计面板 :10898 同款）；redstone_matrix_test 106 PASS/0 FAIL）。

### 🅶 死亡与出生点（t755-t756）
**t755** 死亡状态硬锁与掉落：① 「你死了」弹窗出现后仍可移动、生命值错误显示半颗心 → 死亡瞬间锁输入（移动/跳跃/交互全禁）+ 生命归零显示；② 物品掉落缺失：死亡时背包物品应全部散落原地（现状 Esc 关弹窗后物品还在包里）——含 R19.4 遗留的**死亡护甲不掉落**缺口一并修（验收：生存死亡→弹窗锁死→重生后原地可拾回全部物品+装备）。✅✅ 已完成（commit 见 git log：**「仍可移动」排查结论=输入硬锁链本已完整无洞**（t655 C++ 闸门族 grab/setKey/beginMining/placeBlock/attackMob/drop/pickBlock + t691 QML 键盘第一闸 + 死亡态暂停叠层抑制 + tickImpl !m_captured 早退跳过 step/pollMouse——死亡屏下 Esc 只能走死亡按钮，grab() m_dead 拒绝兜底）；**「半颗心」根因=heal() 无 dead 守卫**：致死 tick 的 step() 在同步 died→onDied→release() 链后继续跑完，尾部饥饿回血 emit healed(1) 经呈现层路由把刚归零的 health 加回 1（takeDamage 有 m_dead 早退而 heal 漏对称守卫）→ heal 入口补 `if (m_dead) return`；**护甲不掉落根因=dropAllItems 漏 armor 槽段**：只遍历 hotbar/main/held 三段后 resetForMode 把 m_armorSlots 一并清空 → 补护甲 4 槽掉落循环（复用同一 dropStack lambda，附魔/实例名/耐久随实体走，先读后清）；t755 探针进 redstone_matrix_test（致死落库 0+dead+cause / heal 死亡免疫 / respawn 复位链）；redstone_matrix_test 107 PASS/0 FAIL）。
**t756** 出生点生成修复：种子 42 出生在树里 → 出生点选择算法强制：出生格及头部格必须为 Air（清除树叶/原木占据），脚下方块为实体支撑且在地表（不接受树冠/洞顶）；任意种子回归验证（验收：连开 5 个不同种子均落在可站立的裸地表）。✅✅ 已完成（commit 06f2566：根因=出生链固定 (kSpawnX,kSpawnZ)=(80,80) 且只按 `heightAt`（纯 fBm 不含树）贴 Y，而 placeTrees 无出生列豁免——种子 42 密度哈希恰在中心列命中生树；树冠半径 2 越过任何半径 1 排除带 → 选定法优于排除法。修法=World 层 `findSpawnColumn()` 确定性 chebyshev 环扫（支撑=满立方或积雪层 / 出生+头格 Air / heightmapAt==h，h 同 generate 钳制式），generate 末+finishLoad 末解析、beginLoad 复位；snapSpawnToGround 在 m_spawnPos pristine（未被床设过）时采用 World 出生列并同步 spawnPoint（指南针跟随）；新增 onWorldSeedChanged 换代复位（修启动序隐患：componentComplete 先在默认种子世界采用过出生列→换真种子被 pristine 漏过）；床位重生 t388 零回归。5 seed {42,7,1337,2024,99} 全过（42→(46,38) 环扫避让树区；1337/99 中心本就裸地表直命中）；t756 多种子探针进 redstone_matrix_test（96³ 世界 5 种子逐个断言），108 PASS/0 FAIL）。

### 🅷 末影之眼链路与物品补全（t757-t762）
**t757** 末影之眼两段式定位与贴图：① 定位策略：现状掷出即直线朝要塞传送门中心（在地下）钻 → 改两段式——与要塞**水平距离 > ~50 格**时，眼只在地面上方向空中升起、朝要塞方向指示（不钻地）；水平距离进入 ~50 格内才开始向下寻路逼近结构（50 为常量可调）；② 贴图修复：掷出的眼实体现在纯白无贴图 → 排查 t729 的 entity_endereye.png / pack 探测 / delegate 接线（重点查 QUrl 判空与 source 绑定），恢复正常小绿瞳珠渲染。✅✅ 已完成（commit 022b5e1：两段式=kEnderEyeNearDist 50（远段目标=传送门 XZ+眼 Y+8 巡航爬升，垂直分量恒 ≥0 绝不向下）+kEnderEyeTurnRate 6/s 帧率无关指数趋近（单参数平滑覆盖初速修正/阈值切换/邻域穿越三过渡）+爬升分量 >3 格满爬 1:1/0..3 线性收敛改平飞，速度模恒 4 只转向，80/20 结算不动；**贴图白三层根因**：①UnitCube 几何无 UV 属性（主因——贴图采样未定义回退白，全工程唯二 UnitCube+贴图用户=endereye 显性白块+夜行者眼层被 baseColor 兜底遮蔽）→ 补 TexCoord0（stride 5 float，纯色用户零影响）②alphaMode Default 不透明 pass 不吃 alpha → 满幅不透明程序图重生成+Mask（t727 先例）③qrc/QUrl/itemIconSource 三态排查无罪；lessons-learned 新增「共享几何缺 UV→白块；透明底贴图须 Mask」条目；redstone_matrix_test 103 PASS/0 FAIL；视觉终验留 tester-correctness（本沙箱 GPU 呈现不可用））。
**t758** 末影珍珠投掷传送：新增右键掷暗渊珠 → 抛物线飞行（受重力）→ 落点把**玩家传送**过去；生存消耗 1；传送附带伤害对齐 MC（落地扣 ~5HP，参数常量）；实体小珠复用暗渊珠贴图/程序回退。✅✅ 已完成（commit fc44fac：Kind::EnderPearl + spawnEnderPearl（重力 28 同源/寿命 8s 兜底/越界虚空静默不传/mob 命中穿过 v1 不做注明取舍）；落点传送=B11 模式向下扫 solid 支撑+脚/头双格复查（1 格窄缝续扫，全列无立位珍珠白耗）；传送序=下坐骑→m_pos 直写格中心→清 vel/knockback→**m_peakY 重置防瞬移落差误摔伤**→positionChanged；伤害 5HP 走 fallDamageTaken→护甲减伤路由→takeDamage（死因 EnderPearlTp「被暗渊珠传送撕碎」全链复用）；死亡态不传；右键分支对齐 t505 雪球模式（生存扣 1/创造不耗）；QML delegate 三态（pack itemIconSource(0x243)+Mask/程序双层深绿小方珠）；redstone_matrix_test 103 PASS/0 FAIL）。
**t759** 要塞传送门房间加高：传送门框架房间净空不足（框架上方仅 1 格，玩家过不去）→ 房间整体加高 3 格（框架上方至少 3 格通行空间）；同步检查入口通道/楼梯高度；worldgen 常量改动仅新世界生效（旧存档不回填，注明）。✅✅ 已完成（commit b25d114：**根因=框架立于石砖高台顶**（台 dy1..3、框架占 dy=4）→ 旧 kWallH=5 内部清 Air 仅 dy1..5，框架顶之上只剩 1 格，1.8 高玩家跨环必顶头 → kWallH 5→8 顶板上移方案（框架层 dy=4 与全部相对坐标零变化，B5 反推 y 假设与 m_strongholdPortalY 记录值零回归，12 框架逐格在记录层探针断言）；修复后框架顶之上 4 格 Air（超验收 3 格下限），入口/北走廊/楼梯/东走廊同获净高 8（楼梯顶步上方 3 格探针断言）；**附带修复探针逼出的既有堆布局 UB 崩溃**：carveCaves 蠕虫循环 `Worm &w = worms[wi]` 跨分叉 push_back 扩容悬空（B8 同族病，改种子重生成世界即可触发）→ while 每步重取引用；P13 探针（12 环完整/每框架上 4 格 Air/+5 顶板石砖/走廊楼梯断面）；redstone_matrix_test 104 PASS/0 FAIL）。
**t760** 刷怪笼完善：① 刷怪笼方块进创造模式背包（现状缺失）+ 中键复制出的透明 item 修复（pick-block 与图标）；② 笼内显示**缓慢旋转的迷你蠹虫模型**（现状笼子看着空但实际刷怪）——笼心小模型自旋 + 半透明蠹虫贴图。✅✅ 已完成（commit 6ee6fc7：**透明根因=t745 图标回退链 iconFileForBlock(Spawner) 无 case** → 空串图标源 → 补 icon_spawner.png（cutout 孔 load_face 均值色填实=实心铁笼立方）+ case；笼渲染整体改 delegate——Spawner solid true→false（glass/ice 先例：碰撞/选中/射线走 ShapeFull 不变，lightOpacity 转 0 同 t742）+ chunkgeometry 三处 PASS 补 Spawner continue（同 Painting/Fire 先例）+ spawnerHost/spawnerDelegate（blockPlaced(40)/broken/worldChanged/enterWorld collectBlocksOfId(40) 四路维护，createObject 失败不落表防 null destroy）；笼壳 BlockCube{40} scale 1.001 消共面 z-fight + Mask cutout 透视（贴图重制栅格 alpha 栅栏 255/格间 0）；笼心 MobModel{mobType:14} 缩 0.45 蠹虫原样不透明（弃半透明防多体节 Blend 自混合伪影，MC 1.0 亦不透明微型化）绕 Y 4s/圈 + ±0.03 浮沉 + NoLighting 全亮地底可读；creativeBlocks 单一权威补条目（Inventory 调色板与 ResourceBrowser 自动全覆盖）；redstone_matrix_test 104 PASS/0 FAIL）。
**t761** 沙砾-燧石-打火石链路：① 新增沙砾方块（换皮沙子：同受重力下落；生成=地下浅层矿袋+河滩，密度低、参数可调）；掉落规则：挖掉**大概率掉沙砾自身、小概率只掉燧石**（概率常量）；② 新增燧石 item（pack flint 映射+程序回退）；③ 打火石配方从 t724 的占位「圆石+铁锭」改回正统「燧石+铁锭」；④ 沙砾/燧石/打火石三者进创造背包 + 资源查看器图鉴。✅✅ 已完成（commit c69ff3d：Gravel=139（MC 13 对齐，tile 179 新画 default_gravel.png）——重力下落零 C++ 改动（Main.qml falling-block 白名单 8/139 + spawnFallingBlock 按 blockId 泛化）；生成=placeGravelPockets 矿袋（网格 16/45%/浅层带 [4,18]/96×96 约 5-20 袋日志实证，pass 序散矿后洞穴前）+ 沙海沙滩两级哈希混排（25% 粗格带×65% 列兑现≈16%，防撒胡椒面）；掉落 kGravelFlintDropPct=10%（silk 恒自掉）；FlintId=0x248（MaterialIcon drawFlint + pack flint 映射）；配方 Cobble→FlintId（铁上燧下纵列，注释重写）；背包（沙旁/材料段尾）+图鉴（creativeBlocks 权威 concat 自动覆盖）；**守住最小变更面**：build_cube_icons 全量重跑会再编码 27 个无关历史图标 → git checkout 还原；redstone_matrix_test 104 PASS/0 FAIL）。
**t762** 黑曜石与下界门复查：① 黑曜石方块进创造模式背包 + 资源查看器（现状缺失）；② **逐项复查 t725 下界传送门全链路**：黑曜石生存获取路径（水+岩浆源石化机制是否存在——若无需列出缺口）、打火石点火内底、2×3 黑曜石框检测、门方块动画渲染、破框熄灭、站门灼伤、mesher/heightmap skip——核对 t725 声明的全部验收点，缺口修复，commit message 附核对表；③ 黑曜石挖掘规则：生存模式**仅钻石镐可挖**（其余镐挖不动/无掉落），无附魔钻石镐挖掘时长 **~12 秒**（挖掘速度参数表加行）。✅✅ 已完成（commit 3fd6baa：10 项核对 9✓+1 缺口——**暗渊珠命中判据 isSolid 把门面（ShapeNone）当实心**→珠掷过门被拦停传送掷者进门框 → 对齐箭 M7 collisionAABBsAt 点在盒内判据修复；获取路径本就在（t411 pass A 流水触岩浆源 world.cpp:841 + t472 pass B 双源 :1123，桶舀岩浆可达）；hardness 50→96（=12s×钻石镐 speedMul 8.0，原 50=6.25s），minToolTier=4（其余镐 96s 极慢+canHarvest=false 无掉落，效率附魔 ×(1+level) t476 链）；creativeBlocks 补条目（ResourceBrowser concat 自动覆盖）；核对表全文附 commit message；redstone_matrix_test 105 PASS/0 FAIL（新黑曜石规则探针））。

### 🅸 附魔系统与铁砧（t763-t766）
**t763** 附魔数值生效链排查：钻石剑附锋利后攻击力无实际加成、也不显示在物品属性上 → 排查**全部已注册附魔**（锋利/效率/耐久/保护等）是否真实作用于战斗伤害/挖掘速度/耐久损耗/护甲减伤数值，且在物品提示上可见；缺哪个补哪个（同 t740 矩阵思路：附魔×生效点核对表进 commit message，用户不逐个人工验证）。✅✅ 已完成（commit 353936a：**核对表 12 附魔全过**——锐锋战斗链本就完好（pc:1895 暴击前 +0.5*级），用户症状与 t766 铁砧丢附魔数据一致（交叉注记）；实修 3 缺口：火焰保护漏 Emberling(15) EPF 路由 / 摔落保护漏 EnderPearlTp(16) 路由（armorProtectionFactor case 补）/ 水上亲和注册后零生效点死附魔 → 补水下挖掘 ×5 惩罚免除（kUnderwaterMiningTimeMul=5.0）；属性可见：HUD tooltip「攻击： 8（锐锋 III +1.5）」+ Chest/Furnace/Crafting/Dispenser 四 UI 补附魔列表+攻击行（容器 store 不持附魔元数据注明）；锐锋注释去"呈现层叠加"误导；t763 探针（锐锋输入链/EPF 六路/耐久 III 400 受击损耗∈[260,340]/对照组精确 50）；redstone_matrix_test 106 PASS/0 FAIL）。
**t764** 附魔台视觉修复：① item 图标缺悬浮书本 → 补（对齐放置态造型）；② 放置态书贴图：右页看到反面/贴图错 → 修 EnchantBookBox 页面正反面 UV 或材质双面渲染；③ 书本应**时刻朝向玩家**敞开（随玩家位置转向，当前朝向固定）；④ 翻页动画修复：现状只见上下悬停（bob），翻页过程不可见/疑似被 t732 重贴图弄回归 → 恢复可见的翻页片动画。✅✅ 已完成（commit 0bf8ffc：**②反面根因=UV 行向+朝向双错**（±Y 面 v=1 钉近端行序颠倒 + 旧 lean −20° 仰向背侧看到页面底面——非单面剔除非分区错）→ v 翻+lean 翻正+yaw 定正面三管齐下；**④翻页不可见=三因叠加**：目标角 316° 几何误判页片从书底扫过穿台体被挡（台顶 0.75 > 页尖 0.42）+静息位与右页 z-fighting+piece3 采无字白纸区（白贴白不可辨）→ 目标 130°（差 6° 落左页上 0.0039 间隙）+静息藏右页体内+符文区贴图暖 tint；③bookYaw=atan2(dx,dz) 100ms Timer 节流（+Z 正面恒对玩家，正上方防抖），leanNode 解耦前倾动画；①图标书叠层=AtlasIconSpec.bookOverlay（pack enchant_book 探测→封面/纸页镜像分区 V 形叠绘，缓存 icon2 族防陈旧）；t732 双布局/t745 回退链零回退；QImage::mirrored 弃用警告顺手改 .flipped；redstone_matrix_test 106 PASS/0 FAIL）。
**t765** 书架→附魔台符文粒子：书架放置后应有文字/符文粒子从书架**涌入附魔台**（MC glyph particle 流；范围判定=附魔台周围有效书架位；t732 撤掉的 GlyphLines 叠层是静态符文，本任务是动态粒子流，两回事）。✅✅ 已完成（commit 687e8d9：新 EnchantGlyphFlow.qml（#Rectangle 面片+Model/Timer 池，**无 Particles3D 依赖**；每粒独立 Texture 选格 glyphs.png 4×4×16px 图集，随机字形×EnchantRunes 色板染色；参数化 lerp+sinπt 弧线 t=1 恰落书心 y+0.85 + 逐 tick billboard，cam.position 快照不建绑定）；**书架位枚举=QML 侧扫**（countBookshelvesAround 同规则切比雪夫==2 环带×两层+半步格 Air，呈现层派出零 World API，t649 先例）；驱动=仅被打开的台（window.enchantX/Y/Z，MC 语义远处台喷了也看不见纯浪费），worldEditRev 触碰重扫；性能四守卫（UI 关 spawnTimer 停/发射率 ≤20s 每书架 ~2.4/池上限 36 满静默丢/tick 绑 active||live>0 在飞放完全停零常驻）；build_glyph_sprites.py 原创符文生成器（3×3 点阵角形笔画固定种子+防渗色断言）；Qt 6.11 适配两坑自测逼出（Texture 无 magnificationFilter/cullMode 是材质级属性）；注释写明与 GlyphLines 静态叠层区别；redstone_matrix_test 106 PASS/0 FAIL）。
**t766** 铁砧修复：① **附魔保留**：物品/附魔书放入铁砧取出后附魔被吃掉（输出槽构建丢 enchant 数据）→ 修复合并/修复逻辑（附魔书+武器合并=武器获得附魔，两附魔相加/同修耐久等基本语义）；② 3D 模型重做：现状「上下各一半」不完整 → 完整铁砧造型（宽基座+窄腰柱+宽顶砧台，照 MC 造型自查重建；贴图颜色沿用现有正确的）。✅✅ 已完成（commit 25ebe06：**①静态审计=现行代码已不可复现"吃附魔"**——takeProduct 四分支 outEnch 全对（t699/M9/M3 系列修复早覆盖，t763 交叉注记系推断；合 lessons-learned t640「报障与已有修复同症状先验证修复在」），合并语义接线表（repair/rename=左槽原样；combine=并集+冲突保左+同款 max/相等+1+封顶；merge=书附魔逐条适用过滤+冲突组+等级合并）静态核对+审计锚点注释钉死；**实修唯一缺口=Shift 搬运改名书丢实例名**（slotShiftLeftAnvil writeSlot 第 7 参 name 漏传）；②"上下各一半"根因=整立方渲染把侧视立绘 114 两段色带读成两半 → 三盒异形 partial 模型（基座 12×4×12+腰柱 4×6×4+砧台 12×6×10，顶面 per-stage 瓦片 113/115/116 保裂纹，solid=false/ShapeFull 碰撞射线不变/lightOpacity 特例 15 防漏光，Spawner/附魔台先例）；redstone_matrix_test 106 PASS/0 FAIL（附魔合并在 UI/JS 层注明静态核对））。

### 📎 R19.11 范围与顺序
t733-t766（34 项）。**建议顺序：t740 红石激活矩阵先行（排查面最广、结论影响 t736/t743 语义）→ t738-t739/t741-t744 红石组件族 → t733-t735/t737 铁轨矿车族（t736 探测信号依赖 t740 的矩阵表）→ t745-t746 贴图统一（总纲机制早落地，后续图标类任务直接沿用）→ t757-t762 末影链路与物品补全（新物品入背包时直接沿用 t745 统一图标机制）→ t763-t766 附魔/铁砧族 → t747-t748 皮肤 → t749-t751 图鉴 → t752-t754 成就 → t755-t756 死亡/出生点收尾**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。

---

## R19.12 用户实测复盘批（t767-t807，41 项；2026-08-22 立项）

**用户验收结论（R19.11 已确认项，无需任务）**：红石火把可插墙（遗留贴图问题单列 t776）；普通铁轨拐弯贴图 ✓；矿车间碰撞 ✓；铁门/铁活板门红石开合 ✓；TNT 上拉杆/按钮引燃失撑掉落 ✓；材质包切换正常 ✓（"以后就用这样的了"=pack 统一贴图路线确认）。

**皮肤清晰度结论（用户提问已核实）**：pack 开启时皮肤采样自包内 `entity/steve.png / alex.png`，实测 **64×64（MC 标准分辨率）** —— 清晰度瓶颈在用户提供的源图，程序态皮肤已是 128×64（2×）。按用户指示**此项停止，等用户提供更高清皮肤图后再做**。

### 🅰 矿车与铁轨（t767-t771）
**t767** 矿车物品与掉落语义：① 创造模式打掉矿车仍掉落矿车物品 → 创造应无掉落（对齐 t571 创造掉落语义）；② 矿车物品堆叠上限 64 → 应为 **1**（载具单件，对齐船 0x234/0x235 maxStack=1 先例，Hotbar/掉落合并两处同步）。
**t768** 矿车 3D 模型与贴图：① 模型高仅 ~0.25 格 → 应 **~0.75 格**（车斗深；minecartbox 几何重调）；② 贴图四周对但中间凹槽是"布料样" → 中间应为木底板/车斗内衬贴图（entity_minecart.png 布局分区或 UV 重排）。
**t769** 矿车坡道行驶：上/下坡被卡死（上不去也下不去）→ 排查 pickTrackStep/pinCartY 坡道衔接；**车身俯仰角须平行轨道面**（坡上 ~45° 倾斜，非水平硬摆）。
**t770** 矿车低速/倒退转弯脱轨：弯道处瞬间 90° 转向 → 慢速前进未触发旋转即脱轨、倒退大概率脱轨 → 弯道格内**强制贴轨约束**（转向与速度/方向无关，几何连续），杜绝低速脱轨。
**t771** 轨道弯道连接规则扩展（仿 MC）：现状仅普通轨×普通轨可弯 → 补：普通轨×动力轨可弯、动力轨-(中间普通轨)-动力轨可弯；连接判定与贴图象限按 MC 轨道连接规则统一（railCornerArms/mesher 连接表 + cart 转向表同源）。

### 🅱 红石激活矩阵补全（t772-t776）
**t772** 红石块直接激活所有红石器件：现状红石块只点亮红石粉/红石灯，发射器、TNT 等均不响应 → 红石块=电平 15 电源，**直接供电**邻接的全部可激活器件（发射器/TNT/门/活板门/铁轨动力等按各自激活判定），矩阵表进 commit。
**t773** TNT 点燃路径补全：① 红石块**直接邻接点燃** TNT；② 激活态红石粉传递信号点燃（powerTntTriggered 链路审计，t740 修复后仍漏）；③ t736 探测轨有车信号也应触发 TNT。机制等价 MC：TNT 是可被一切激活源点燃的器件。
**t774** TNT 爆炸对生物伤害：现状爆炸只伤玩家、生物无伤 → EntityManager 侧爆炸半径→mob takeDamage（击退+伤害，对齐玩家侧数值）。
**t775** 乘矿车顶头扣血：生存模式坐矿车穿过 1 格高通道（玩家头撞实体方块）应扣血（骑乘态头部方块检测→窒息伤害链），现状无痛穿过。
**t776** 墙上红石火把贴图：横置（墙插）与竖置（立地）贴图未重合对齐 → 火把盒 UV 按 attach 形态采样同一贴图区（partialblockgeometry 红石火把 case，对齐 t738 火把先例）。

### 🅲 资源查看器生物模型（t777-t784）
**t777** 羊：① 浏览器 3D 预览脚被羊毛完全覆盖 → 腿应露羊皮色（羊毛只到躯干/头，腿=skin 层）；② 游戏内羊 pack 开启时多一双眼睛（牛等已隐藏）→ pack 态本体贴图脸部遮挡/换区采样对齐关包态。
**t778** 鱿鱼浏览器 3D 模型：仍有"小鱿鱼"叠加且被加了眼睛 → 鱿鱼=单一生物模型（去小鱿鱼、去眼睛，对齐游戏内 t730 模型）。
**t779** 头像裁剪修复：猪头像缺鼻子、蠹虫头像缺眼睛（mobHeadIconSource 裁剪区域/源图分区）。
**t780** 狼/豹猫浏览器 3D 贴图：头对但身体贴图错——狼仍用兔子贴图、豹猫贴图不对（mobEntityMap 映射/纹理绑定）。
**t781** 夜行者 3D 模型重做：现状猪模型套皮 → 新建**细肢人形骨架**（mobmodel）：3 格高、僵尸式正常身体（非骨架）、不持武器、手/腿极细但与躯干连接；浏览器与游戏内共用。
**t782** 燃烬者 3D 模型重做：现状猪模型套皮 → **仅一颗头 + 4 根烈焰棒**绕身旋转（无白色身体；棒=带贴图长条，旋转动画；对齐 MC 烈焰人造型语义）。
**t783** 浏览器 UI 修复：① 剪羊毛按钮点不了且与返回按钮重叠（点击区域/布局）；② 颜色切换控件从底部移入 3D 预览区（悬浮于动态图像内）。
**t784** 床浏览器 3D 模型：仍是老模型 → 更新为当前游戏内低 3D 床模型（多色联动 t751 变体）。

### 🅳 生物蛋与刷怪笼（t785-t787）
**t785** 生物蛋补全：末影人/烈焰人蛋未与其它蛋同列（贴图也须仿各自配色）、狼/豹猫蛋缺失 → 蛋表补全（生成式染色表扩展），全部蛋进创造背包蛋区。
**t786** 刷怪笼类型化：① 笼改为**带生物类型**（state 编码存储）；② 地底生成的笼不全是蠹虫 → 僵尸/骷髅/苦力怕/蜘蛛按权重随机（蠹虫限要塞）；③ 创造背包放置的笼中间空白 → 放下即显示对应迷你生物；④ tickSpawners 按类型刷怪。
**t787** 生物蛋×刷怪笼交互：手持生物蛋对刷怪笼右键 → 笼变为刷该生物类型（改 state + 迷你模型即时切换），机制等价 MC。

### 🅴 染料与多色羊毛/床（t788-t789）
**t788** 染料体系：① 新增染料 item 族（花类植物→对应色染料，破坏/采集获得）；② 熔炉烧仙人掌 → 仙人掌绿染料；③ 染料+白羊毛=染色羊毛、染料+床=染色床（配方+着色链，机制等价 MC 1.0 染料主干）；④ 染料/染色羊毛/染色床进背包与图鉴（t745 统一图标）。
**t789** 羊自然色：刷出的羊不再只有白色 → 自然色权重（白为主+棕/灰/浅灰/黑/粉），剪毛得对应色羊毛、死亡掉对应色（游戏内渲染+浏览器 t751 变体联动跑通全链）。

### 🅵 成就系统（t790-t791）
**t790** 成就弹窗布局：太占位置 → 移**右下角**；可拖动区域背景与主 UI 视觉区分（半透明/描边）。
**t791** 骨粉催熟平衡：现状多个骨粉催不熟一株 → **3-4 个骨粉应催熟**（生长阶段推进概率调参，机制等价 MC 骨粉 2-5 次成熟语义）。

### 🅶 铁砧（t792-t794）⚠️ t792 最高优先（阻塞全部铁砧测试）
**t792** 铁砧 UI 附魔立即丢失（用户实测回归）：附魔物品放入铁砧 UI 的瞬间附魔就不见了（显示或数据层）→ t766 静态审计认为四分支已保附魔，但用户实测仍丢 → 实机复现定位（重点怀疑：左槽放入时的 UI 展示层未显示附魔 vs takeProduct 前的中间态真丢数据），修复后用户须能连续测试修复/合并/改名。✅✅ 已完成（commit c35adf9：**实机探针洗冤主链**——qml.exe 驱动真实 AnvilUI.qml+InventoryOps.js+Hotbar 桩，11 放入路径 47 检查全过（左键/右键/Shift/拖拽/双击拾取/取回/takeProduct 四操作/关包归还，数据+光晕+tooltip 三层），附魔放置链当前 HEAD 无罪——用户症状最可能 t699（8-19）前旧 exe；首轮 2"FAIL"系预期错（效率×时运同组互斥=设计行为）。顺手修 4 真缺陷：①slotShiftLeftAnvil 材料分支源槽余量短参 writeSlot 丢名 ②B 槽空写入名硬编码""丢带名材料名 ③takeProduct 无条件整组 wipe anvilEnch/Names 抹 B 余量材料元数据→收窄为确空才清 ④B 槽漏 slotDur 绑定不显耐久条；探针 47/47 + 矩阵 111 PASS）。
**t793** 铁砧贴图上部透明缝隙：与仙人掌同根因（d754011 pack 瓦片 alpha → 图集构建期 solidify anvil 族瓦片 anvil_top*/侧/底；程序态正常）。
**t794** 铁砧重力：放置后下方被挖空/无支撑 → 转**下落方块实体**（沙/沙砾同链）；下落砸中生物扣血（伤害随落差）；落地播放音效。

### 🅷 附魔台（t795-t798）
**t795** 附魔选项门槛：放入工具立即显示三选项高亮 → 应**放入青金石后**才显示可点选项（lapis 槽位消耗 1-3 对应等级；创造模式同样需要，且**创造也要书架达标**才有最高等级——书架数上限封顶逻辑补齐）。
**t796** 附魔书模型修复：① 书太低，上下浮动时穿模进附魔台台体 → 抬高悬浮基准；② 贴图一面书页一面书皮（像翻完的书）→ **两面都是书页**；③ 静止时也要有**翻页动画**（不只上下浮动；持续小幅翻页+浮动复合）。
**t797** 书架→附魔台文字动画重做：现状"打开附魔台才播、动画超大像爆炸" → 改：**放置书架即自动播放**（不依赖开 UI）；白色小字、透明背景；从书架**缓慢漂向**附魔台、途中渐隐、到达即删（t765 EnchantGlyphFlow 调参/触发重构）。
**t798** 效率附魔审计：① 木镐附效率后挖掘像效率 V → 应按**等级递增**（每级固定增幅，非全统一到顶级速度）；② 效率只对**匹配工具-方块**生效（镐挖石类加速，挖泥土/沙无加成；斧→木、铲→土石类同理）——全附魔公式复查（对照 t763/t476 数值表）。

### 🅸 方块物理与物品归类（t799-t801）
**t799** 沙/沙砾即时下落：放在火把/睡莲/草丛/半砖等**非实体支撑**上 → 应**立即转下落实体**（现状稳定站住，只有 >1 格落差才变掉落物）；放置路径与更新路径同判（MC 语义：支撑失效即刻落）。
**t800** 物品栏归类清理：① 材料栏的羊毛 item 删除（方块栏已有羊毛方块，多此一举）；② 玻璃移到方块栏（现处材料栏）且玻璃 item 图标改 **2D 平贴图**（非 3D 立方投影）。
**t801** 栅栏视觉高度：视觉改 **1 格高**（mesher 几何裁剪；碰撞/跳跃判定保 1.5 不变）→ 消除 0.5 格悬空穿模观感。

### 🅹 合成体系审计（t802）
**t802** 全配方审计：① 云杉原木→云杉木板→木剑/木镐等**木制品链丢失**（云杉木制品配方核查）；② 打火石合成不了（t761 改的燧石+铁锭配方验证）；③ 燃烬棒→燃烬粉分解配方缺失；④ 举一反三全表过一遍（RecipeRegistry 对照材料/工具/食物/染料族，缺失/错料/错形状逐条修，核对表进 commit）。

### 🅺 火与点燃（t803-t805）
**t803** 打火石与火焰基础修复：① 打火石第一人称**手持不可见**（held 模型路由/图标）；② 火焰动画错位（从格子上方向下播放 → 对齐所在格）；③ 生物碰火**不燃烧**（僵尸实测 → 火 tick 点燃 mob，持续伤害+着火贴图/粒子）。
**t804** 点燃交互扩展（仿 MC）：① 打火石右键**木制品方块**点燃（方块着火贴图+向相邻木方块蔓延）；② 对苦力怕右键 = 按 MC 规则点燃引爆（原地短引信，非不可逆追踪蓄力态）；③ **火中丢弃物品被烧毁**（火格 item 实体销毁+烟粒子，对齐岩浆烧物）。
**t805** 船上岸回归：船又能直接开上岸（此前修过又坏）→ 定位回归提交（git log 船水检测），恢复"离水减速不可加速上岸"语义。

### 🅻 下界传送门（t806）
**t806** 传送门粒子与尺寸：① 创建/破坏时的白色粒子 → 改**门色（紫）**粒子；② 大尺寸门支持：2×3 是**最小**尺寸，更大门框（宽 ≤4 高 ≤5 等按 MC 规则）也能点燃成门（框检测泛化）。

### 🅼 投掷物模型（t807）
**t807** 末影之眼/珍珠投掷模型：现状六面都是"眼睛" → 改为**掉落物式贴图**（单面 item 贴图 billboard/小平面，对齐掉落物渲染语义）。

### 📎 R19.12 范围与顺序
t767-t807（41 项）。**建议顺序：t792 铁砧 UI 附魔丢失最高优先（用户明确被阻塞，无法测试铁砧系统）→ t772-t776 红石激活矩阵（t773 TNT 链依赖 t772 电源语义）→ t767-t771 矿车铁轨族 → t803-t805 火与点燃 → t799-t802 方块物理/归类/合成 → t795-t798 附魔台族 → t785-t787 蛋与笼 → t788-t789 染料 → t777-t784 浏览器模型族 → t790-t791 成就 → t806 传送门 → t807 投掷物**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。皮肤清晰度项已按用户指示**关闭**（等高清源图）。
