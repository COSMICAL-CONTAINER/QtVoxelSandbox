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
**t767** 矿车物品与掉落语义：① 创造模式打掉矿车仍掉落矿车物品 → 创造应无掉落（对齐 t571 创造掉落语义）；② 矿车物品堆叠上限 64 → 应为 **1**（载具单件，对齐船 0x234/0x235 maxStack=1 先例，Hotbar/掉落合并两处同步）。✅✅ c4d6e03：根因① hitCartFromRay 全模式同路径 emit cartBroken 无创造门控（t735① 排查注释早自证「非模式门控」）→ instantBreak（=caller 传 m_mode==Creative）提前 return 不发信号；② maxStackSize 两处均无 0x23E 特判落材料段默认 64 → Hotbar::maxStackSize（RecipeRegistry::MinecartId）+ BlockRegistry::maxStackSize（字面量，掉落合并 cap）同步特判 1，创造调色板 heldCount=maxStackSize 自动单件、itementitymanager 合并 cap>1 守卫自动跳过。测试 153 PASS/0 FAIL。
**t768** 矿车 3D 模型与贴图：① 模型高仅 ~0.25 格 → 应 **~0.75 格**（车斗深；minecartbox 几何重调）；② 贴图四周对但中间凹槽是"布料样" → 中间应为木底板/车斗内衬贴图（entity_minecart.png 布局分区或 UV 重排）。✅✅ ① 模型加高：Main.qml 5 盒件本地 Y ±0.15（总高 0.3）→ **±0.375 总高 0.75**（底板件 [-0.375,-0.3125] 厚 1/16 下沿贴轨板上沿 +0.0125 微隙、四帮全高、车斗凹槽深 ~0.66；MC 1.0 矿车 0.7± 语义）；三层耦合常量同值重推（头文件注释本就写明"改须同步"）：kCartRideH 0.225→**0.45**（=1/16 板 +0.375 底板下沿 + 微隙）/ kCartGroundH 0.1625→0.3875 / playercontroller kCartSeatDrop 0.15→**0.3125**（脚踩板面；帮顶高出脚底 ~0.69 → 腿没入斗、胸头露帮上）；0.75 模型 ⊂ ±0.45 命中盒（F3 WireCube 一致；hitbox/碰撞/速度常量零改动）。② "布料样"根因 = qrc 程序贴图底板带 (0,20)-(44,28) 低对比棕底撒点 + 稀疏细竖缝，在 0.8×0.9 大面拉伸下读作麻布 → 生成器重画为 (0,20)-(64,32) 全宽 4 行 3px 橡木板 course（横缝 + 隔行错位端缝 + 板色差 + 顶棱受光 + 竖木纹 + 缝钉，缝对比拉满）+ MinecartBox 布局 0 piece0 采样窗同步（±Y 铺满 64×12 / 薄边 16×12）；pack 态 layout1 木底窗 (46,12)-(62,24) 为包原生木纹区本就正确未动。测试 t737 镜像常量 rideH 0.225→0.45 同步；153 PASS/0 FAIL。**需人工目视**：车斗 0.75 高观感 / 木底板观感（qrc 与 pack 双态）/ 骑乘坐姿（脚踩板面、胸头露帮上）/ 坡道行驶（kCartRideH 抬高 0.225 后钉轨表现）。
**t769** 矿车坡道行驶：上/下坡被卡死（上不去也下不去）→ 排查 pickTrackStep/pinCartY 坡道衔接；**车身俯仰角须平行轨道面**（坡上 ~45° 倾斜，非水平硬摆）。✅✅ 根因 = pickTrackStep 的邻轨存在性防御只查**同层 ry**（坡连接的邻轨在 ry±1）→ 上坡在坡脚格心、下坡在坡顶格心的重选一律误判「连接位失真」，兜底重选也只查同层 → 返 false → 车停死（矩阵 P12b 修前复现：上行停坡脚格心 / 下行停坡顶格心）。修：存在性改三高探针（同 railConnections anyRail / railProbeDelta 同层+上+下），主验 + 兜底同改；Y 衔接本就连续（pinCartY rise=fx 逐 tick 插值，矩阵断言坡段 y=Y+(x-x₀)±0.02 无阶跃）。俯仰：Cart 加 pitch 字段 + pitchAt 读口（正=车头上扬，同 arrowPitchAt 约定 → QML delegate eulerRotation.x 直连 (pitch,yaw,roll)）；updateCartPitch 沿车头向 ±kCartPitchProbe=0.25 两点采样轨面高（railSurfaceYAt = scanRailColumn + railRiseAt，从 pinCartY 抽出的共用纯查询 → 与 Y 钉定 / mesher 同一张面）→ pitch=atan2(Δh,0.5)：坡中段恰 ±45°、跨段折缝窗横跨两段线性过渡（随位置连续、车速/帧率无关、无需时间平滑）、平轨/拐角 0、车头翻转符号自动翻转；**纯呈现量**（碰撞/射线/座位不读）。调点：tickRiddenCart 移动帧（pinCartY 返回轨层）+ 停驻帧 + tickPushedCarts 滑行帧 + spawnCart 放置即贴坡。测试：P12b 坡道探针 3 项（被骑上坡到顶死端+Y 插值+坡段 pitch≈+45/停驻归 0；空车续推下滑到底平台+pitch≈−45；坡格 spawn 静止车 +45）——**156 PASS/0 FAIL**（基线 153+3）。⚠ rig 教训：本 slot 地形达 y≥42（「40 以上必空」假设失效），坡轨上方被地形实心占据时 scanRailColumn 实心断扫把坡中段判离轨冻车 → rig 先净空轨道 box 再铺轨（等价用户露天坡场景）。**需人工目视**：坡上车身 45° 倾斜观感 / 跨折缝过渡 / 倒退下坡俯仰翻转。
**t770** 矿车低速/倒退转弯脱轨：弯道处瞬间 90° 转向 → 慢速前进未触发旋转即脱轨、倒退大概率脱轨 → 弯道格内**强制贴轨约束**（转向与速度/方向无关，几何连续），杜绝低速脱轨。✅✅ a3194bd：**双根因**（stepCartAlongRail）——① 倒行弯道重选结果**不持久化**：到心/起步两处重选都是 `if (sgn > 0)` 才写回 c.dirX/c.dirZ，sgn<0 时新臂只改本子步循环局部 tx/tz → 下一帧 travel = 旧 dir×sign(speed) 沿**旧轴**横切 = 用户报「倒退大概率脱轨」；② **非到心时刻的方向重选**（tickRiddenCart 死区置零帧的停驻重选 + pushEmptyCart 起步选向）可在弯道格**中途**选垂直臂 → 车带横向偏移（至 ~0.5 格）平行驶离中心线 = 「慢速前进未触发旋转即脱轨」。修：① 双符号持久化（到心 + 起步两处同改）：正行 dir=新臂 / 倒行 dir=新臂取反（travel=dir×sign(speed) 下一帧仍 = 新臂，直线上幂等）；② **弯道格内强制贴轨约束**：每子步把行进向的垂直轴钉到所在格中心线（floor+0.5）——与速度/方向/步长无关的几何连续约束，正常行驶恒在 .5 上 → no-op，横向偏移即被拉回；瞬间 90° yaw 保留（MC 语义），位置严格连续在轨。测试：P12c L 形轨探针 3 项（慢爬 ~0.5 blocks/s 入弯 + 摩擦停驻格中仍在中心线；格中再起步钳回出口臂中心线 + yaw 270；倒行过弯逐 tick 折线距离 ≤0.05 + yaw 180 + 南死端格心停驻静止）——**159 PASS/0 FAIL**（基线 156+3）。⚠ 探针教训：`pos - float(c-1) - 0.5f` 左结合 = `pos-(c-1)-0.5`（差一格的邻格心），格心断言必须显式括号 `pos - (float(c)+0.5f)`；倒行起步须在行进中发起（静止重选会把 dir 对齐 wish 使 S 变 W）。**需人工目视**：慢速过弯车贴轨不横漂 / 倒退过弯 / 倒行中停车再前进。
**t771** 轨道弯道连接规则扩展（仿 MC）：现状仅普通轨×普通轨可弯 → 补：普通轨×动力轨可弯、动力轨-(中间普通轨)-动力轨可弯；连接判定与贴图象限按 MC 轨道连接规则统一（railCornerArms/mesher 连接表 + cart 转向表同源）。✅✅ 4c9720c：根因 = railConnections 规则①拐角配对臂限定 id==Rail（armRail 探针）→ 普通轨的跨轨种垂直邻被规则③整个丢弃（单端 stub）→「只有普通×普通能弯」；动力-普通-动力垂直链中间普通轨 con=Nx 单端（矩阵 P16 修前复现：空车行至中间格心 pickTrackStep 无 +Z 位 → 死停）。修：拐角臂存在性 = hasP*（三高探针 isRail 家族任一轨种，机制等价 MC 1.0「弯道形态只呈现在普通轨格上、配对邻轨任意轨种」）——普通×{普通,动力,探测}邻均成弯、动力-普通-动力/探测-普通-探测垂直链中间轨成弯；动力/探测轨自身仍永不弯（规则②直线投影，坐弯位自动直连平行邻）；反向守卫随 nArm==2 垂直配对自动成立（删显式 !hasN*）。贴图象限与车转向零改动天然工作（同源消费 con 位：mesher railCornerArms 单一权威 + pickTrackStep 通用点积环，t737 架构红利）。测试 P16 七探针（混合 L×3 臂种 con+象限+臂恒直 / 动力轨坐弯位恒直 / 双链弯中间+破端轨回落单端 / 空车过混合拐角垂直转向+yaw180+死端停）——**166 PASS/0 FAIL**（基线 159+7）。**需人工目视**：普通轨贴着动力轨转弯的拐角贴图观感 / 动力-普通-动力链整链连贯。

### 🅱 红石激活矩阵补全（t772-t776）
**t772** 红石块直接激活所有红石器件：现状红石块只点亮红石粉/红石灯，发射器、TNT 等均不响应 → 红石块=电平 15 电源，**直接供电**邻接的全部可激活器件（发射器/TNT/门/活板门/铁轨动力等按各自激活判定），矩阵表进 commit。✅✅ 已完成（commit fcbade7：**双放置顺序矩阵实证洗冤**——新增 P15 探针把「放置顺序」独立成维度（旧矩阵只测源+器件同帧在场：order A 器件先稳态后放源走 t657 锚点 6 邻展开 + t706 全红石族脏达 / order B 源先稳态后放器件走器件自身锚点），3 源 × 7 器件 × 2 顺序 = 42 组合全 PASS（信号型断言信号 + 坐标、状态型断言通电位 + 降沿清位）；呈现层静态审计完整（Main.qml 双转发 handler 在 Connections{target:theWorld}、firePowerTnt/fireDispenserAtQml、entityManager/dispenserStore 绑定、placeBlock→setBlock→notePowerWrite、finishLoad 全图红石族重扫）；**用户症状在当前 HEAD 两顺序均不复现 = 陈旧 exe 模式第 4 例**（t740/t744/t792 同款）；矩阵 153 PASS/0 FAIL（基线 111 + 42）。人工复核注意：空库存玩家发射器通电无动作是 t607 设计语义，测试前须装填）。
**t773** TNT 点燃路径补全：① 红石块**直接邻接点燃** TNT；② 激活态红石粉传递信号点燃（powerTntTriggered 链路审计，t740 修复后仍漏）；③ t736 探测轨有车信号也应触发 TNT。机制等价 MC：TNT 是可被一切激活源点燃的器件。✅✅ 已完成（commit 8ef2149：**探针实证三路径全通——无产品代码修复**，P17 三探针补齐防回归锁：(a) 同层粉链 lever-粉-TNT 升沿恰一次 + 坐标 + 消费端清块、断电降沿无再触发、断电后再上电（场内无 TNT）静默、**重放 TNT（粉仍热 15）立即点燃**（器件后放路径）；(b) 粉爬上 TNT 顶（墙斜角一跳）：顶粉距源 2 跳电力 **14**、TNT 经 isReceivingPower +Y 正交读点燃、拆源全线断电无再触发；(c) 探测轨有车**端到端**（真实矿车直落压轨 → bit4 经 setWaterSilent→notePowerWrite 占用链置位——非主矩阵直摆激活态捷径）→ 邻接 TNT 恰一次点燃 + 驻轨稳态幂等零写不重复 + 推离离开沿断电无再触发。**矩阵 harness 教训**（探针头注释固化）：World 层 TNT 分支无升沿守卫——每次电力活动 pass 触达通电 TNT 即 emit，防双触发收口在呈现层消费端 firePowerTnt（isTnt 守卫 + 同步 clearBlockSilent）；裸 World 层「恰一次」断言必假 FAIL（粉 state 写入沿回插 → 下一 pass 复算再触达同块）→ 探针须挂 scoped 消费端镜像连接（isTnt + clearBlockSilent）才等价真实链路。**169 PASS/0 FAIL**（基线 166+3）；①红石块直供由 t772 P15 42 组合覆盖未见回归 → 用户症状与 t772 结论一致 = **陈旧 exe 第 5 例**。
**t774** TNT 爆炸对生物伤害：现状爆炸只伤玩家、生物无伤 → EntityManager 侧爆炸半径→mob takeDamage（击退+伤害，对齐玩家侧数值）。✅✅ 07582f8：缺口实证（非陈旧 exe——爆炸→mob 链历史未做过）：`detonateTntSphere`/`detonateStalker` 两爆炸路径均只 emit mobAttackedPlayer 伤玩家，球内 mob 零伤。修：新增 EntityManager::`damageMobsFromExplosion(ex,ey,ez,skipIdx)` 共用主体（TNT 路径爆心=TNT 格心+skipIdx=-1 / Stalker 路径爆心=e.pos+skipIdx=自爆源自身防自伤）——扫活体 Mob（跳过 dead 尸体 / 非 Mob / skipIdx），mob 身体中心（e.pos）到爆心 3D 距离 → dmg=round(kExplosionDamageMax·(1−d/R)) 半径内至少 1（**同玩家侧公式/常量**）→ `damageEntity`（红闪 + 归零 dead → 既有 t449 延迟 mobDied 掉落链）+ `knockback`（(mob−爆心)XZ 归一、kExplosionMobKnockbackStrength=1.33 ≈6 b/s 对齐玩家 kHitKnockbackHoriz=6.0）；水中爆炸照样伤 mob（originInWater 只门控地形破坏，同玩家侧口径）；玩家链原样保留（防双伤）；damageEntity/knockback 不增删槽 → aiStalker tick 迭代内调用安全（无迭代器失效）。测试 P17 矩阵探针：3 猪距爆心 ~1/2/4 格受伤恰 16/8/0 HP（距离单调）、8HP 猪吃满 8 伤 dead→0.5s 后 mobDied 恰一次（掉落链入口）、球外猪全程无伤、幸存猪短窗（6 tick）击退位移 +X 0.25-1.5 格、玩家 mobAttackedPlayer 恰发一次且远场爆炸不再新增（不双伤）、水中爆炸照样 16 HP——**170 PASS/0 FAIL**（基线 169+1）。**需人工目视**：TNT 炸猪群（贴脸秒杀 + 掉猪排）/ 潜行者自爆伤邻近动物 / 爆炸把幸存 mob 推飞观感。
**t775** 乘矿车顶头扣血：生存模式坐矿车穿过 1 格高通道（玩家头撞实体方块）应扣血（骑乘态头部方块检测→窒息伤害链），现状无痛穿过。 ✅✅ 已完成（fix(t775)：**真缺口非陈旧 exe**——PlayerController.step 矿车骑乘分支早 return 于 t160 窒息块之前，骑乘期头部嵌实心格零伤；矿车高 0.75 可进净空 1 格通道（scanRailColumn 自 floor(pos.y) 起扫，天花板不遮轨），玩家 1.8 高 → 眼位 ≈ 轨格底+1.7575 必嵌天花板。修：① t160 窒息块抽为 tickSuffocation（判据/每秒 1HP 节奏/Suffocation 死因原样，走路路径原位替换调用）；② 点盒判定收口 World::pointBlockedByCollision 单一权威（原 PlayerController 内联，t575 sub-AABB 语义逐字等价）；③ 骑乘分支座位同步后直调 tickSuffocation——仅 Survival 扣血（创造/观察者模式闸免伤），死亡走既有 died 链 + respawn 既有 dismount 清骑乘。船分支不接：眼位在水面之上 + 低顶桥洞自动蹲既有玩法语义（t556），范围外。测试 P18 探针（12 格直轨隧道 ×2 rig）：1 格净空骑乘眼位对 pointBlockedByCollision 全程 true + 4.8s 连续嵌 ≥3 脉冲 @1HP/s（t160 同节奏镜像）；2 格净空对照全程不嵌 0 脉冲；两 rig 车均钉轨面驶完全程（天花板不遮 scanRailColumn 断言）。**171 PASS/0 FAIL**（基线 170+1）。**需人工目视**：生存骑矿车穿 1 格高通道定期红闪掉血、2 格通道无伤、创造骑乘无伤。探针教训：nextSlot 网格已满（尾行 z0=97 越界 setBlock 全拒=假 FAIL），新探针用固定坐标+前置净空含隔离边。）
**t776** 墙上红石火把贴图：横置（墙插）与竖置（立地）贴图未重合对齐 → 火把盒 UV 按 attach 形态采样同一贴图区（partialblockgeometry 红石火把 case，对齐 t738 火把先例）。✅✅ 0e59f80：根因 = t738 墙插 **S 片**（垂直墙面侧视深度片）是柄根→离墙 0.45 的**单向** quad 铺**整张瓦片** → 贴图中央火把列（柄 2px+焰头 4px，u=0.5）落片内中点 = 离墙 0.225 处，而 W 片（平行墙面，整瓦铺 0.8 宽）火把列在火把轴（柄根贴墙）上 → 两片剪影沿轴错开互不重合 = 用户报「横着的竖着的贴图没有重合到一块去」（斜视一把火把裂成两把错位剪影）。修：S 片重做为**绕火把轴对称窄带**（宽 0.2）只采瓦片中央子区 u∈[0.375,0.625]（build_rail_family 焰头 4px 列区 x6..9、柄 2px 居中；170 熄灭态同布局）—— 与 W 片（0.8↔整瓦）**同 texel 密度**（0.2↔1/4 瓦）：柄/焰世界宽两片一致、两片贴图中央火把列都钉共同 30° 火把轴（底/顶边中点=柄根/轴端）→ 正视/侧视/斜视均读作同一把斜插火把；贴墙内侧半带（≤0.1）嵌支撑实体块被遮挡（失撑即掉落故恒实体）。pushCrossQuad 增可选瓦片内 u 子区参数 su0/su1（默认整瓦，既有调用零改动）；立地形态不动。测试 P19（mesher 同源直调，P11 模式）：五形态断言 (a) 每 quad 底/顶边中点==柄根/轴端（火把列共轴重合，修前 S 片中点离轴 0.225 必 FAIL）、(b) 墙插两片恰 {0.8 整瓦采样, 0.2 子区带}（texel 密度等式 0.8/1.0==0.2/0.25）、(c) 亮端离墙（四向各验点积符号）+ 立地亮端朝上（v=0 恒底边）、(d) 熄灭位几何不变换瓦片 170 —— **172 PASS/0 FAIL**（基线 171+1）。**需人工目视**：pack 态（redstone_torch.png）与 qrc 程序态墙插火把正视/侧视/绕行斜视剪影重合成一把、柄根贴墙焰头朝上离墙。

### 🅲 资源查看器生物模型（t777-t784）
**t777** 羊：① 浏览器 3D 预览脚被羊毛完全覆盖 → 腿应露羊皮色（羊毛只到躯干/头，腿=skin 层）；② 游戏内羊 pack 开启时多一双眼睛（牛等已隐藏）→ pack 态本体贴图脸部遮挡/换区采样对齐关包态。 ✅✅ 已完成：① 根因 = MobModel 单材质把羊毛贴图（pack 毛层 box-UV 腿区 / 程序 mob_sheep 全脸）与 t789 毛色 tint 铺满全身**含四腿**；修 = ResourceBrowser 羊预览四腿位叠独立纯色羊皮 Model（#d6b890，t749 前剪毛羊裸肤同源色）罩住毛贴图腿——图鉴静态（walkPhase 恒 0 → 四腿轴对齐）盒位 (±0.18,-0.28,±0.26)×全长 (0.18,0.32,0.18) 镜像 mobmodel.cpp 羊分支 addLegs 实参，外扩 0.01/0.02 防共面 z-fight；独立材质不吃毛色 tint（有色羊腿仍羊皮色 = t789 协同：tint 只乘毛层贴图）；剪毛态同罩（裸肤腿读作平滑皮肤；鸡腿 t616「几何外独立腿 Model」先例）。顺手修同簇潜伏缺陷：浏览器羊眼 overlay 旧 z=-0.35 是把 Main.qml 颈枢**相对**坐标（颈枢 (0,0.10,-0.29) + 眼相对 z −0.35）当绝对坐标用 → 眼盒落头盒 z∈[−0.61,−0.29] 内部被头面遮挡恒不可见（预览羊实际无眼）→ 烘焙正确绝对位白眼底 z=−0.64 / 黑瞳 −0.65（与游戏内同位）。② 根因 = t749 起 mobTextureSource(3) 毛层命中改返**合成贴图**（毛身 + 本体层头区真脸），t593/t633 的「pack 态眼恒显（毛层无脸）」语义未随更新 → 真脸 + 恒显 overlay 眼 = 两双眼；修 = 新增 ResourcePackManager::sheepWoolFaceActive() Q_INVOKABLE 单一权威（合成缓存生效与否，只读不生成，无 NOTIFY 由 QML 绑定依赖 Texture.source 驱动重算），三处消费按牛式「pack 自带脸则隐 overlay 眼」门控：Main.qml 毛茸态眼 Node、剪毛态眼 Node（pack 本体层 sheep.png 命中 = 真脸 → 同隐）、ResourceBrowser 预览眼（sheepPreviewPackFace 派生属性）；合成失败回退毛层原样（头前无脸）→ 眼仍显保唯一脸；pack 关（程序贴图无脸）恒显零回归。已知边界：扁平老包 L15 兜底（主毛层两级 miss → 直接返本体层 sheep.png 原样）不走合成分支，眼 overlay 仍显 = 该布局下双眼——罕见老包降级路径，未扩。矩阵探针 t777（generateSheepWoolFaceFile 纯函数直调 + temp PNG rig，刻意不实例化 ResourcePackManager——BuiltState 进程全局会读宿主机真实 pack 态 = 非密闭）：真 64×32 fur+body 双 PNG → 合成落盘 + 输出保 base 尺寸 + 头区 (0,0)-(28,14) = 本体层色（真脸覆写 = 隐眼依据）+ 毛身区（body/leg 行）= 毛层原色 + body 缺失 → 空串优雅降级（调用方回退毛层 = 眼保留路径）——**188 PASS/0 FAIL**（基线 187+1）；腿 skin 色 overlay / 眼位修正为 QML 呈现层，无 C++ 可测路径（矩阵不链 Quick3D）。**需人工目视**：① 浏览器羊预览四腿羊皮色、毛色圆点切黑/棕时腿仍羊皮色不随染；② 浏览器羊预览眼恢复可见（pack 关白底黑瞳；pack 开显合成贴图真脸、无 overlay 叠眼）；③ 游戏内 pack 开毛茸羊恰一双真脸眼、pack 关 overlay 白底黑瞳眼仍在；④ 游戏内剪毛羊 pack 开真脸单双眼 / pack 关 overlay 眼在；⑤ 羊吃草低头时眼随头俯仰不脱位（颈枢未动，应零变化）。
**t778** 鱿鱼浏览器 3D 模型：仍有"小鱿鱼"叠加且被加了眼睛 → 鱿鱼=单一生物模型（去小鱿鱼、去眼睛，对齐游戏内 t730 模型）。 ✅✅ 已完成：根因 = 「小鱿鱼」与「眼睛」**同源**——mobmodel.cpp squid 分支 pack 态仍建 0.30 宽「尖顶小盒」且**复用 mantle texOffs**（t750 只删了 pack 关态：全脸 UV 下小盒六面铺整张程序贴图更明显；pack 态 box-UV 六面各采 mantle 对应面**整区**，前脸区含包贴图眼纹素——HD 包 face 区带 10% 近白眼纹实测）→ 观感=头顶叠一只带眼缩小鱿鱼（图鉴大图旋转预览尤显；游戏内同一共享几何同样残留，水下远视角未被察觉）；「被加了眼睛」= t750① 图鉴补的 2 颗黑点几何眼（pack 关态显）。修 = ① 尖顶盒**几何层整删**（pack 开关两态统一单 mantle + 8 触腕 = 机制等价 MC 1.0 squid 单身八腕，vanilla 本无独立尖顶盒）；② 呈现层眼层全删（Main.qml 实体 delegate 2 颗 + ResourceBrowser 图鉴 2 颗）——三处渲染（实体 delegate / 刷怪笼 miniEyeTable「贴图自带面部→无眼层」t786 既有语义 / 图鉴）统一无几何眼：pack 态眼来自贴图前脸纹素，程序贴图态无脸纹（纯斑纹软体观感）；③ 顺手：图鉴 mobPreviewCentY(9) 0→0.07（删尖顶后两态跨度统一 [-0.46,0.32]，中心 -0.07 上提居中）+ MaterialIcon 鱿鱼蛋顶小尖注释解耦（2D 图标剪影记号保留，非 3D 模型部件）+ mobmodel.h/reverts 数量注释同步。纯视觉 QML/几何项无 C++ 可测路径（矩阵不链 Quick3D，t777 同例；矩阵中鱿鱼仅 t785 蛋 id 映射探针引用，未涉几何/眼）——矩阵 **188 PASS/0 FAIL 不回归**（基线 188，零新探针）；exe 10s 冒烟零 QML 错误。**需人工目视**：① 图鉴鱿鱼预览（pack 开）= 单 mantle + 8 触腕、前脸贴图自带眼、头顶无小鱿鱼；② pack 关图鉴鱿鱼 = 纯斑纹软体无黑点眼；③ 游戏内水下游鱿鱼同模型无叠影（pack 开关两态）；④ 刷怪笼鱿鱼迷你剪影无尖顶无眼层。
**t779** 头像裁剪修复：猪头像缺鼻子、蠹虫头像缺眼睛（mobHeadIconSource 裁剪区域/源图分区）。 ✅✅ 已完成：根因 = 两类「五官不在头脸矩形里」——①猪鼻不在头脸：MC 机制猪鼻是**独立贴图偏移盒**(beta ModelPig nose offset(16,16) 4×3×1)，mobHeadRegions 猪条目只裁头 Front (8,8)-(16,16)（demo 包×4 实测仅含 row11 双眼）→ 鼻 Front (17,17)-(21,20)（鼻孔 row18 x17/x20 两暗点）天然缺；②蠹虫旧条目 (0,2)8×5×2 的 front (2,4)-(10,9) 实为第二体节甲壳（×8 实测无眼），真头 = 首盒 (0,0)6×2×2、双眼**跨 top/front 边界**（黑斑 (2,1)(4,1)(1,2)(2,2)(4,2)(5,2) 实测）→ 任何单面裁剪都缺眼。修 = ① MobHeadRegion 扩覆写盒字段（ovU0/ovV0/ovW/ovH/ovD + 脸内贴放 ovX/ovY；NSDMI 缺省无 = 其余 13 条目零改动）：猪 = 头脸 + 鼻 Front 贴脸局部 (2,4)（几何实证：鼻 x∈[-2,2] y∈[0,3) 相对头盒 → 脸列 2-5 / 行 4-6，眼 row3 正上方 = 机制等价 MC 猪脸眼上鼻下）；② 蠹虫 d=0 扁平区语义直取「头顶+脸」拼合区 (0,0)-(8,4)（含双眼）；裁剪矩形改走新单一权威出口 mobHeadIconLayout（resourcepackmanager.h 声明，生成器与探针同源防漂移，同 t785 spawnEggTint 教训）+ generateMobHeadIconFor 密闭端到端口（同 t777 rig 语义）。缓存无需 bump：图标盘文件每进程构建期预生成 **无条件覆写**（ensureBuiltLocked 末遍历），无「旧盘缓存永久复用」路径（区别于 t800 icon4 世代缓存）。矩阵探针 t779 新增（①布局常量锁：猪/蠹虫/牛/蜘蛛/豹猫/夜行者 front+覆写全字段 + 表外型恒无条目；②合成 64×32 临时 rig 端到端：猪 pig/pig.png 子目录路径脸 A 鼻 B → 64×64 图标鼻 B 落脸中下/下巴不越界；蠹虫扁平 silverfish.png 显式源头区 C → 图标含 C 横带居中）→ **189 PASS/0 FAIL**（基线 188+1）；编译零警告；exe 10s 冒烟无 QML 错误 + 真包实测落盘图标像素核验（猪 nostril-L/R = 7e4140/9b5150 暗点、蠹虫眼带在位）。程序态零改动（pack 关猪=swatch/蠹虫=mobFallbackTexture 全图，t749 回退链不动）。**需人工目视**：①图鉴猪头像（pack 开）= 眼上鼻下带鼻孔；②图鉴蠹虫头像 = 灰头带双眼黑斑；③其余 14 mob 头像无回归（牛/蜘蛛/豹猫/夜行者已探针锁，其余未动路径）；④ pack 关两态回退链不变。
**t780** 狼/豹猫浏览器 3D 贴图：头对但身体贴图错——狼仍用兔子贴图、豹猫贴图不对（mobEntityMap 映射/纹理绑定）。 ✅✅ 已完成：根因 = 狼(10)/豹猫(11) 自 t749 起**刻意不入 mobEntityMap**（当时几何全脸 UV 无 box-UV 数据，防 packTextured 误采未设定位）→ pack 开时 mobTextureSource 恒 miss → 3D 预览身体恒程序全脸 UV（狼 = 灰身+立耳+四足读作「兔子」），而 2D 头像走 mobHeadRegions explicitSrc 正确显头 = 「头对身错」。修 = ① mobmodel.cpp 补两分支 MC box-UV（demo 包像素取证，六面不透明度逐一实测）：狼 head(0,0)6×6×4 / **躯干采 mane(21,0)6×6×7**——vanilla body(18,14)6×6×8 在 demo 包 HD 重绘里 top/bottom/back 三面 0% 不透明（躯干区未涂满），mane 区六面 100% 灰白渐层长毛 = 狼身唯一完整毛色区 / 腿(0,18)2×8×2；豹猫 head(1,1)5×4×4（同 t749 头区）/ body(20,6)4×5×6（橙底深斑条纹）/ 腿(0,18)2×4×2 / 尾区(12,19) 侧面未涂 → 尾采 body texOffs 随身同纹（机制同程序态语义）。② mobEntityMap 补 {10,"wolf/wolf.png"}{11,"cat/ocelot.png"}（512×256=8× / 256×128=4×，1.8+ 猫科合并 cat/ 目录），mobHeadRegions 两型 explicitSrc 撤除（头像回归主映射同源；蠹虫 14 仍 explicitSrc 头像-only——它几何仍全脸 UV）。③ 呈现层三处接线：Main.qml wolf delegate packTextured+mobWolfPackTex 两态；ocelot delegate 仅**未驯服**采 pack（demo 包 cat/ 无驯服猫变体 PNG，驯服猫恒程序 mob_cat_* 全脸 UV）；刷怪笼 miniPackTex 表补狼/豹猫两行（恒野生形态）；几何眼 overlay pack 命中时隐（贴图前脸自带双瞳/眼点，t777 双眼教训）——Main.qml 狼/豹猫眼 + ResourceBrowser 狼/豹猫眼（selectedMobPackSrc=="" 门控，同铁傀儡先例）；图鉴 3D 预览走既有 selectedMobPackSrc 共享路由自动切换。④ mobEntityMap 定义拆段提到全局作用域 + 声明入 resourcepackmanager.h（同 t785 spawnEggTint 提头动机；同一 TU 匿名 ns 多段 = 同一未命名 ns，符号互通行为不变）。矩阵探针 t780 新增（映射锁：10/11 精确路径 + 蠹虫 14 仍不在表防「无 box-UV 误入表」；头区布局锁：狼 front(4,4)6×6 新增 / 豹猫 (5,5)5×4 回归；端到端：子目录布局 temp rig 直调 generateMobHeadIconFor，验 explicitSrc 撤除后「region 条目→mobEntityMap→文件解析」链路通）→ **190 PASS/0 FAIL**（基线 189+1）；编译零警告；exe 8s 冒烟正常。游戏内同错贴一并修（同一 mobEntityMap/共享几何源，实体 delegate / 刷怪笼迷你态同受益）。**需人工目视**：① 图鉴狼预览（pack 开）= 灰白毛身 + 贴图脸（无 overlay 黑点眼）、头/身/腿贴图连贯；② 图鉴豹猫预览（pack 开）= 橙棕斑点身 + 贴图脸；③ pack 关两型回退程序贴图（狼 mob_wolf / 豹猫 mob_ocelot）+ overlay 眼恢复；④ 游戏内野生狼/豹猫 pack 开 = 贴图身且无双眼、驯服猫仍程序猫贴图；⑤ 刷怪笼狼/豹猫迷你剪影贴图态；⑥ 狼尾（独立纯色 Model）两态观感协调。
**t781** 夜行者 3D 模型重做：现状猪模型套皮 → 新建**细肢人形骨架**（mobmodel）：3 格高、僵尸式正常身体（非骨架）、不持武器、手/腿极细但与躯干连接；浏览器与游戏内共用。 ✅✅ 已完成：根因 = t727 旧模型三缺陷叠加——(a) 几何「细长人形」实为窄板条躯干（半宽 0.13）+ 小竖头 + 双臂悬空（臂内缘 ±0.22 vs 躯干侧面 ±0.13 → 0.09-0.10 缝隙「手不连身」）；(b) pack 态 g_texH=64 但 demo 包 enderman.png 实为 **64×32 基图**（256×128=×4 HD）→ 全部 V 坐标按双倍基图采样错区 = 「黄色贴图残留」乱区花斑的根源；(c) 假人形 UV 区非真实 enderman 盒区 + 头前脸 rows14-15 透明下巴底色 RGB 黄在不透明 pass 直接显 = 黄斑。修 = mobmodel.cpp mobType 16 分支整体重写：g_texW/g_texH=64×32 真实基图 + 真实盒区（头(0,0)8×8×8 / 躯干(32,16)8×12×4 / 四肢共用(56,0)2×30×2，demo 包像素实测）。几何参数表（局部原点=碰撞中心，头朝 -Z）：**腿 ×2** 心(±0.10,-0.85,0) 半(0.07,0.55,0.07) y[-1.40,-0.30] 顶嵌躯干 0.05 髋枢 -0.35；**躯干** 心(0,+0.15,0) 半(0.26,0.50,0.14) y[-0.35,+0.65]（全宽 0.52 僵尸式正常身）；**臂 ×2** 心(±0.29,+0.025,0) 半(0.06,0.575,0.06) y[-0.55,+0.60] 顶嵌肩 0.05 外缘 ±0.35 恰齐 halfW 肩枢 +0.55 手端 -0.55 垂近髋；**头** 心(0,+0.975,0) 半(0.28,0.325,0.28) y[+0.65,+1.30]（全宽 0.56 正常略大坐躯干顶）；总高 2.70（碰撞 2.80 halfH=1.40；模型略短于碰撞=MC 惯例）；臂全宽 0.12/腿全宽 0.14 ≈ 躯干宽 1/4 极细；biped 步态=腿绕髋枢 X 轴左右反相 kLegSwingAmp、臂与同侧腿反相 0.85× 幅；不持武器。三消费端同步：Main.qml delegate（nwHead 层头心 0.975；眼 overlay (0,1.00,-0.30)×(0.34,0.13,0.03) + pack 自带脸门控隐（t777/t780 同规防四眼）；嘴 (0,0.72,-0.29)×(0.18,0.05,0.03)；本体材质 Mask+alphaCutoff 0.5 裁透明下巴防黄斑）；ResourceBrowser（预览 centY 0.13→0.06；眼 overlay 同位同门控；材质 Mask 扩 mobType 16）；刷怪笼（scale 0.16 注释 2.70；yOff 0.019→0.008（脚 -1.40/顶 +1.30）；miniEyeTable E(0.00,1.00,-0.29,0.34,0.13)；mini 材质条件 Mask）。hitbox 核验：EntityManager halfW=0.35/halfH=1.40 与新模型吻合（臂外缘 ±0.35 齐 halfW / 头顶 +1.30<1.40 / 脚 -1.40 贴底）——无需改，AI 语义不动。矩阵不链 Quick3D（t778 先例零新探针）→ **190 PASS/0 FAIL 不回归**；编译零警告；exe 10s 冒烟零 QML 错误。**需人工目视**：① 游戏内夜行者（pack 开）= 僵尸式正常躯干 + 略大头 + 极细双臂双腿自然连身、总高约三格、无黄斑、恰一双灰白眼；② pack 关 = 程序 mob_nightwalker 暗紫黑影 + 紫白 overlay 魅眼在位；③ 激怒瞪视动画（头抖 + 嘴渐张）随新头位不脱位；④ 浏览器图鉴预览全身入镜居中、pack 开单双眼/关 overlay 眼；⑤ 刷怪笼迷你瘦影眼带新位；⑥ 行走摆臂摆腿反相自然、无穿模。
**t782** 燃烬者 3D 模型重做：现状猪模型套皮 → **仅一颗头 + 4 根烈焰棒**绕身旋转（无白色身体；棒=带贴图长条，旋转动画；对齐 MC 烈焰人造型语义）。 ✅✅ 已完成：**「猪模型套皮」根因 = MobModel::setMobType 白名单止于 14——16/17 被静默钳成 1（猪）**，夜行者/燃烬者自 t727/t728 起三处消费端（实体 delegate/图鉴预览/刷怪笼迷你）一直渲染猪几何套 mob 贴图（rebuild 的 16/17 分支是死代码；**t781 夜行者重做也因此从未生效**，本次白名单补 16/17 后 t781 细肢人形随之一并激活）；次根因 = mobType 17 pack 态 g_texH 误 64（demo 包 blaze.png 实为 64×32 base（256×128 ×4HD，t779 头像侧同实测）→ V 按双倍基采样错区，t781 夜行者 enderman 误基同族）。修法：① mobmodel.cpp 17 分支重做——头 = 单一略大方盒 **0.88³**（半 0.44，心 (0,+0.10,0)，顶 +0.54/底 -0.34）+ **4 根烈焰棒** = 细长竖盒 **0.10×1.10×0.10**（半 (0.05,0.55,0.05)，心 y=-0.03 → 跨 [-0.58,+0.52] 伸过碰撞盒上下沿）绕身公转（轨道半径 0.62、径向 90° 分布、棒身恒竖直只轨道心转；棒内缘 0.57 与头半 0.44 间隙 0.13 不穿模、外缘 0.67 微出 halfW 0.5 纯视觉 hitbox/AI 不动；总跨 1.12 < 碰撞 1.2；无身体）；② **新 Q_PROPERTY rodSpin**（度）= 棒组公转角（棒 i 轨道位 = i·90°+rodSpin），walkPhase 同款 set→rebuild 驱动（量化 6°/步 ≈ 27 步/s@2.2s 视觉连续，防每帧微变 rebuild）；③ **两态均 MC box-UV**（新 g_boxUvAlways，仅 17 置 true）：头 head(0,0)8×8×8 / 棒共用 rod(0,16)2×8×2，base 64×32（pack 态 demo blaze / pack 关 entity_emberling 程序贴图（build_entities_pack.py t717 即按 blaze 布局自绘头区黄焰+白热核 / 棒条区烟灰暗黄竖纹——棒=带贴图长条，旧全脸 UV 会把棒条区拉上头脸）；两贴图六面盒区实测 100% 不透明）；眼 = 贴图脸自带（pack 态 blaze 脸暗色眼纹 432px 实测 / 程序态白热焰核）→ 无 overlay 眼层（t777-t781 纪律，miniEyeTable 维持空）。三消费端：Main.qml delegate 删 t728 手搓 4×UnitCube 纯色 Repeater（棒恒亮橙不吃受击/天光 tint）改共享几何 + `NumberAnimation on rodSpin` 0→360 2.2s/圈（帧率无关）+ hover bob 保留；ResourceBrowser 删 t750 ⑤ 手搓棒组（与游戏内各持一份易漂移）改 MobModel 内 rodClock 门控动画（仅选 17 时 rodSpin 非零 → 其余型零 rebuild 开销）+ 摆位 scale 1.6→1.1 / centY 0→0.02；刷怪笼迷你 rodSpin 静态 45°（笼自旋已给动感）+ miniMobScale 0.47→0.38（0.42/1.12）+ yOff 0→0.008。矩阵不链 Quick3D（t778/t781 先例零新探针）→ **190 PASS/0 FAIL 不回归**；编译零警告；exe 冒烟 2×（10s/8s）零 QML 错误。**需人工目视**：① 游戏内燃烬者（pack 开）= 单头 + 4 根 blaze 棒贴图环绕旋转、无猪身无白身；② pack 关 = 黄焰头 + 烟灰棒程序贴图态；③ 受击红闪/昼夜灰阶整只（含棒）着色；④ 图鉴燃烬者预览棒组公转 + 头棒贴图正确；⑤ 刷怪笼燃烬者迷你（含蛋改型）头+棒剪影 45° 静态位；⑥ **夜行者回归**（白名单修复后 t781 模型首次真正生效）：细肢人形 + 眼/嘴 overlay 位对齐 + pack 态 enderman 贴图 + 图鉴/笼迷你三处。
**t783** 浏览器 UI 修复：① 剪羊毛按钮点不了且与返回按钮重叠（点击区域/布局）；② 颜色切换控件从底部移入 3D 预览区（悬浮于动态图像内）。 ✅✅ **单根因双症状**：右列 Column 内容总高（预览区 300 固定 + 名 + 类别 + t751 变体面板 ~86px）≈454px **超视口 366px**——QML Column 不裁剪溢出子项 → 变体面板纵向溢出越过预览矩形 / 面板底缘压进 footer 行；footer 是主 Column **后声明兄弟（painting z 更高）**→ 返回按钮矩形（x 644..764 / y 450..482）盖住「已剪毛」toggle 右半 + 其 MouseArea 吃掉点击 = ①「点不了且重叠」；毛色 16 点行 / 诚实边界注被推出面板底缘（y ≥500）残显 = ② 的「控件在底部」观感。修法（ResourceBrowser.qml 单文件）：变体面板**整组迁入预览区**（previewArea）作下沿内侧锚定悬浮面板 variantPanel——width=预览-12 / anchors.bottom margin 6 / Rectangle **rgba(0.059,0.078,0.102,0.85) 半透明底板**（刻意用 Rectangle rgba 而非 Item opacity——后者连带淡化子控件）+ #3a444f 描边 radius 8（t790「浮层与主 UI 视觉区分」同款语言）/ z 10 悬浮于 View3D 只占预览区下沿 ~1/3（模型中心不盖死、下沿透出）；**锚定不参与 Column 布局 → 显隐零布局影响**（原 Column 跳过 visible:false 语义等价）；右列回归 300+名+类别 ≈360 ≤ 366 恒不溢出，新面板 y 263..356 vs footer 448+ 无任何交集；右列底部留布局不变式注释（新增预览侧控件一律走悬浮模式勿再挂本列）。控件原样迁移零改动：两段 toggle（hover 底色 / 激活金边 #ffd76a）/ 16 色 swatch（金框选中 + mapToItem(panel) tooltip 复用格通道）/ 毛色边界注；stale 注释 6 处（「底部变体面板」→「预览区悬浮」）同步。测试：编译零警告；exe 10s 冒烟零 QML 错误（组件启动即实例化、delegate 即时创建，绑定错误会在 stderr 显现）；qmllint 警告 174→157（纯迁移无新增）；矩阵 **190 PASS / 0 FAIL 不回归**（纯 QML 布局项无 C++ 探针路径，t778 先例）。**需人工目视**：① 羊条目 = 悬浮面板贴 3D 预览下沿（半透明板模型腿部透出、模型中心可见），「未剪羊毛 / 已剪毛」两 toggle 全区域可点且与右下返回按钮无重叠；② 16 色圆点面板内可点切换、金框选中、hover tooltip 正常、预览毛色即时刷新（t777 羊皮腿不随染可顺带复核）；③ 雪傀儡条目 = 悬浮面板只显两段 toggle（无毛色行），戴 / 剪头切换 + 纯雪头五官正常；④ 其他 mob / 物品选中 = 悬浮面板消失、预览 / 名 / 类别布局无回归；⑤ 返回按钮正常关闭浏览器回设置面板。
**t784** 床浏览器 3D 模型：仍是老模型 → 更新为当前游戏内低 3D 床模型（多色联动 t751 变体）。 ✅✅ 已完成：根因 = 床段 ShapeBed 双格低异形不在 isPartialBlock/isCrossBlock/isMaterial 任一渲染路由谓词内 → 浏览器 selectedIsCube 把床当整立方，渲成「满格 BlockCube 六面同贴床色被面瓦片」的旋转立方（=「仍是老模型」）；修 = ①盒布局下沉单一权威：partialblockgeometry 床 case 的 t496 盒数学（腿×2/木板床架/彩色被面床垫/床头板或床尾板/枕头仅 head 半）整段提为 `PartialBlockGeometry::bedHalfBoxes(blockId, isHead, facing, out)`（World 层，.h 带注释契约；床 case 改遍历盒列表调 pushBox，逐字承值 → 游戏内 chunk mesh 输出零回归）；②新增 Renderer 层 `BedModelGeometry`（QML_NAMED_ELEMENT）：一次调 bedHalfBoxes 两半（foot 本格 + head 沿 -front 邻格 x-1，facing 固定 0，与 bedPartnerOffset「foot → -front 找 head」同向）拼完整双格床，床整体中心=原点（x∈[-1,1]/y 以 kBedHeadboardTop 居中/z ±0.5），顶点 pos3+uv2（同 MobModel；TexCoord0 必在——t757 白块教训），atlas UV 读 AtlasTileCount/kAtlasTilePx 单一权威半纹素内缩（t54 漂移教训），Renderer→World 向下依赖合规（同 blockcube include world.h）；③ResourceBrowser：新增 selectedIsBed 路由属性（Hotbar::isBed 单一权威，覆盖两段 16 色），cubeView 内三模型互斥（BlockCube visible 加 !selectedIsBed；新 Model visible: selectedIsBed，geometry BedModelGeometry{blockId: selectedId}，共享图集 atlasSource → pack 开即时刷新，scale 1.15 双格床撑满镜头，spin/userPitch 拖拽复用）；**16 色联动**：调色板 16 个床条目各持独立 id（0x20..0x27 + 0x4E..0x55）→ blockId 绑 selectedId 选色即换被面瓦片（t751 变体联动同族；床色无共用模型控件，不设 variantPanel——t783 悬浮面板模式仅羊/雪傀儡需要）。矩阵 **190 PASS/0 FAIL** 不回归（纯视觉项不加探针，t781 先例；bedHalfBoxes 逐字承值无行为差，矩阵未链 Quick3D）。**需人工目视**：①浏览器选红床 → 低 3D 双格床（床头板高 9/16 带 white wool 枕头 / 床尾板矮 7/16 / 四角 planks 腿 / 被面床垫），非旧满格立方；②依次选橙/白/黑等 16 色床条目 → 仅被面床垫瓦片变色，腿/架/板仍 planks、枕仍白 wool；③pack 开时床预览贴图随合成图集刷新；④游戏内放置床外观与浏览器预览一致（同源几何互证）；⑤床预览可拖拽旋转/俯仰、松手回自转无跳变。

### 🅳 生物蛋与刷怪笼（t785-t787）
**t785** 生物蛋补全：末影人/烈焰人蛋未与其它蛋同列（贴图也须仿各自配色）、狼/豹猫蛋缺失 → 蛋表补全（生成式染色表扩展），全部蛋进创造背包蛋区。 ✅✅ 三缺口全实证：① 夜行者/燃烬者蛋（t727/t728）孤列在暗渊链材料之后（蛋区断裂）→ **移入蛋区**与其它蛋连续同列；且燃烬者蛋 0x247 MaterialIcon 漏 case 落 default 显木棒（t728 只加了 id/name/palette，recipe.h 注释声称的 drawSpawnEgg("emberling") 不存在）→ 补 kind。② 狼/豹猫蛋**整体缺失**（mob 实体 t480/t481 早就有，蛋 id 没有）→ 新 ids SpawnEggWolfId=0x249 / SpawnEggOcelotId=0x24A + nameForBlock 名 + placeBlock 接通（右键生成**野生**个体，驯服走喂食链）+ MaterialIcon drawSpawnEgg("wolf"/"ocelot") 自绘。③ 贴图仿各自配色：t645 生成式染色表扩 4 行（夜行者黑底紫点 #1a1426/#8a50d8、燃烬者金黄底深琥珀斑 #e8b830/#a05818、狼浅灰蓝底深灰纹 #c8ccd4/#50565f、豹猫奶油底褐纹 #e8c890/#7a4a20）+ itemFilenameMap 补 pack 直连 4 行（enderman/blaze/wolf/ocelot_spawn_egg.png，现代包有则用、老包 miss 走生成式染色——不再是「无映射恒自绘」）。**防回归收口**：蛋→mob 映射此前散在 placeBlock 内联 11 路 || 链 + ResourceBrowser mobTypeForEgg 两处手抄（t728 B9「加了蛋漏接」即此类）→ 收口 RecipeRegistry::mobTypeForSpawnEgg 单一权威表（recipe.cpp 直引 EntityManager::MobType 枚举防手抄漂移；placeBlock 蛋判定/mobType 改查此表，color 占位串转 switch）；EggTint+spawnEggTint 声明提到 resourcepackmanager.h（矩阵测试直调）。矩阵探针 t785：13 蛋 id→mobType 全对（枚举同值断言）+ 创造背包蛋区**连续同列**（span==13 无杂项穿插）+ 全蛋有名 + 染色表全有条目 + 非蛋 id（燧石/0x212 钻石占位）不误命中——**183 PASS/0 FAIL**（基线 182+1）。**需人工目视**：蛋区 13 蛋连续排布、夜行者（黑紫紫瞳）/燃烬者（金黄焰尖棒纹）/狼（灰蓝双耳）/豹猫（奶油褐斑）四蛋图标观感、四蛋右键各刷对应野生 mob、图鉴选狼/豹猫蛋显 3D 模型。
**t786** 刷怪笼类型化：① 笼改为**带生物类型**（state 编码存储）；② 地底生成的笼不全是蠹虫 → 僵尸/骷髅/苦力怕/蜘蛛按权重随机（蠹虫限要塞）；③ 创造背包放置的笼中间空白 → 放下即显示对应迷你生物；④ tickSpawners 按类型刷怪。 ✅✅ 四项全落地：① state bit1-5 = mob 类型字段（SpawnerStateMobShift=1/Mask=0x3E；bit0 降级为旧要塞银鱼兼容标记）——编码 BlockRegistry::spawnerStateForMob(int)（Core 层 raw int，数值契约=EntityManager::MobType），解码单源 EntityManager::spawnerMobTypeForState Q_INVOKABLE（合法型守卫 {4,5,6,7,14}、非法位回退僵尸；state=0→僵尸 / state=1→蠹虫旧存档兼容；刻意不默认蠹虫防旧地牢笼全变银鱼），SQLite m_states round-trip 保真；② placeDungeons 地牢笼 hash r bit20-27 加权随机：僵尸 102/骷髅 64/蜘蛛 51/爬行者 39（/256，机制等价 MC 1.0 地牢僵尸主导池；蠹虫要塞专属——placeStronghold 三笼写 SpawnerStateSilverfish=0x1D 显式型，读端两代形态并存）；③ 创造放置 Spawner 分支写 spawnerStateForMob(MobShambler)=0x08（地牢最常见型为默认），spawnerHost.addSpawnerVis 创建时经解码注入 cageMobType → 迷你 MobModel 按型缩放/居中（Shambler/Bones 0.25、Stalker 0.22、Spider 0.50 宽约束、Silverfish 1.30 补偿）+ 贴图/纯色/眼表与实体 delegate 同源（pack 命中感知回退链；修「放下中间空白」根因=放置路径无 type + delegate 恒蠹虫）；④ tickSpawners 改 spawnerMobTypeForState 解码刷对应型（spawn 条件玩家近/cap 语义不变）。矩阵探针 t786：五类型编码↔解码互逆 + 位布局常量锁（枚举漂移即 FAIL）+ 旧存档/非法位分流 + 多 seed 池化 worldgen 分布（地牢池零蠹虫/零无类型格、僵尸主导序）+ tickSpawners 双极性（僵尸笼只出僵尸/骷髅笼只出骷髅，刻写位清空防地形拒刷）+ 放置默认常量——**184 PASS/0 FAIL**（基线 183+1）。**需人工目视**：五型迷你生物在笼内观感（尤其 Spider 宽体/Silverfish 放大后比例）、创造放笼即时显僵尸、地下找笼验多型分布。
**t787** 生物蛋×刷怪笼交互：手持生物蛋对刷怪笼右键 → 笼变为刷该生物类型（改 state + 迷你模型即时切换），机制等价 MC。 ✅✅ 全链落地：① **交互分流**（playercontroller placeBlock 蛋分支前置判定）——准星命中格是 Spawner → 不刷 mob，改走 5 参数 setBlock 写 state=spawnerStateForMob(蛋 mobType)（id 不变只 state 变 → 不发 broken/placed、发 worldChanged；mobType 取自 t785 单一权威蛋表 mobTypeForSpawnEgg，13 型全合法）；没命中笼走原「刷 mob」路径不变。生存扣 1 蛋/创造不耗（对齐蛋消耗语义）；改同型时 World「id+state 均无变化」守卫自动 no-op（蛋照消耗，同 MC）。② **解码白名单扩表**（spawnerMobTypeForState）——合法型从 t786 五敌对扩为 13 蛋型+Silverfish 共 14 型（Pig/Cow/Sheep/Chicken/Squid/Wolf/Ocelot/Nightwalker/Emberling 九新加入；哨兵 MobTest/Tnt/Anvil/golem 与 >18 越界仍兜底 Shambler——「经笼凭空刷哨兵型」防线）。③ **tickSpawners 类型路由**——敌对七型走 spawnHostileMob（同旧）；被动七型走新 spawnPassiveMob（hostile=false + kDefaultMaxHealth + 配色表同蛋分支/散布板），上限判据换「笼周**同型**计数 mobTypeCountNear < kSpawnerLocalCap」（hostileNearby 只数敌对、对被动笼恒 false → 不换判据会无限刷；机制等价 MC「同类 6 只内才刷」按型判；被动 spawn 不计入敌对预算）。④ **迷你模型即时切换**——t786 delegate 创建期注入 cageMobType 的「无原位改型」假设被本任务打破 → spawnerHost.cleanupVis 兜底重读（worldChanged 触发）：幸存条目重读 state 解码，cageMobType 有变即重赋 → property NOTIFY 触发 delegate 内 geometry mobType/贴图查表/摆位/眼表全部绑定重算换型（无需新增信号链）；贴图/摆位/眼表扩全 13 型（四足/鸡/鱿鱼/狼/豹猫/夜行者/燃烬者程序贴图 + pack 感知查表；摆位按各型 MobModel 局部跨度归一 ~0.42 体高；夜行者补紫白魅眼横带）。⑤ **顺带修 t786 潜伏缺口**（探针暴露）：spawnHostileMob 色表漏 Spider 分支 → 防御 else 把它改写 Shambler —— 地牢蜘蛛笼（SpawnerStateSpider）实际刷出僵尸非蜘蛛（t786 探针只测僵尸/骷髅两极性未覆盖）→ 补 Spider 分支保型（色 #2a1a1a 同实体 delegate）。矩阵探针 t787：13 蛋 id→编码→解码 round-trip 全对 + 哨兵/越界仍拒 + 改型写入（同 id setBlock）后 tickSpawners 按新类型刷（猪笼被动非敌对极性/蜘蛛笼敌对极性双探针）+ 被动笼同型 4 只封顶（34s 五周期恰 4 只，mobTypeCountNear 判据生效）——**185 PASS/0 FAIL**（基线 184+1；t786 非法样本 0x21 扩表后成合法 Nightwalker → 换 0x27=type19 越界）。**需人工目视**：手持各蛋右键已放笼 → 笼心迷你模型即时切换观感（四足/鸡/夜行者瘦影/燃烬者单头比例）、生存模式蛋消耗、鱿鱼/蜘蛛笼刷怪行为。

### 🅴 染料与多色羊毛/床（t788-t789）
**t788** 染料体系：① 新增染料 item 族（花类植物→对应色染料，破坏/采集获得）；② 熔炉烧仙人掌 → 仙人掌绿染料；③ 染料+白羊毛=染色羊毛、染料+床=染色床（配方+着色链，机制等价 MC 1.0 染料主干）；④ 染料/染色羊毛/染色床进背包与图鉴（t745 统一图标）。 ✅✅ 四项全落地（commit 5a8105b）：① **染料 16 色 item 族**——材料段新连续段 DyeIdBase=0x24B..DyeBlackId=0x25A（SpawnEggOcelotId 0x24A 后首个空闲段），**行序=羊毛 16 色标准序**（白复用 Wool=27 / 其余 FirstWoolVariant=63 起下标算术），nameForBlock 16 名全接 + 创造材料 tab 末尾**连续同列染料区**（图鉴 ResourceBrowser 由 creativeMaterials 派生=同源在列）；获得链：**四花破坏直接掉对应色染料**（Core 层 blockregistry.cpp dropId 字面量：红 0x259/黄 0x24F/蓝 0x256/白 0x24B——Core 不 include Game，recipe.cpp static_assert 钉死跨层契约同 CoalOre 0x201 模式；花方块本体仍走创造调色板取用，生存不再获得花方块——染色链正道改走染料；**失撑路径同源统一**：checkFlowerMushroomOnEdit 原直传方块 id → 改 dropId(above)，花失撑也掉染料、蘑菇 dropId=自身逐位不变，堵「破支撑绕过染料链取花方块」漏洞）+ ② **熔炉烧仙人掌→绿染料**（kSmelt 补 Cactus→DyeGreenId 行 + kSmeltXp 补 1 XP——两表都接，防 B4「注释声称表漏行」同类缺口）；③ **32 条 shapeless 2×2 染色配方**（多重集 {DyeX, Wool}/{DyeX, BedWhite} 各条唯一不遮蔽）：16 染料+白羊毛（Wool=27）→对应色羊毛（白染+白羊毛→白羊毛无害直染同 MC 骨粉语义）+ 16 染料+白床（BedWhite=78）→对应色床（床色段散布 32..39+78..85 逐条查表）；染色羊毛/床方块 t620/t455 早已在列（多 id 变体+贴图），本任务补的即「生存获得途径」缺口；④ 图标：MaterialIcon **drawDye(main,light,shade) 彩色染粉锥**（同骨粉粉末堆语言无骨碎粒）16 case，三色参数取 tools/build_wool.py 羊毛同源色板（light 向白 ~35%/shade 压暗 ~38%）——染料与所染羊毛同色一眼对上；**pack 染料贴图单张灰底（颜色靠 metadata）→ 刻意不映射恒走自绘**（色彩保真优先；id>0x22E 越 mcMaterialId 表界本就 -1 回退）。**范围控制：只做 16 单色直染，混色（二级染料合成）不做**。矩阵探针 t788：段连续（DyeIdBase+i）+ 16 名全有（四花色精确核对）+ 四花 dropId==染料常量 + 烧仙人掌产物+XP + 32 配方 match 全对（换位摆抽查证无序 + 染料+错基〔木板〕拒配防等价表误扩）+ 调色板染料区连续 16——**186 PASS/0 FAIL**（基线 185+1；32 新配方另被 t802 全表自匹配自动覆盖，表长 147→179）。**需人工目视**：染料 16 色图标配色（白/浅灰对比度、黑染粉在深色槽底可辨）、创造背包染料区排布、红花破坏掉红染料物品实体、烧仙人掌进度条产物图标、橙染+白羊毛合成橙色羊毛全链。
**t789** 羊自然色：刷出的羊不再只有白色 → 自然色权重（白为主+棕/灰/浅灰/黑/粉），剪毛得对应色羊毛、死亡掉对应色（游戏内渲染+浏览器 t751 变体联动跑通全链）。 ✅✅ 全链落地：① **Entities 层**——Entity.sheepWool 字段（0..15 十六色标准序，行序同 RecipeRegistry 染料段与羊毛方块段）；spawnMobCore 按 kSheepNaturalWeights 自然权重随机（白 81.836% 主导 + 粉/灰/浅灰/棕/黑少数，机制等价 MC 1.0 羊生成权重）；繁殖幼崽继承首个父代色（同 ocelotVariant 先例，不走权重重掷）；Q_INVOKABLE 读口三件套 sheepWoolAt(i)/sheepWoolTintAt(i)/sheepWoolTintForIndex(idx)（tint 色板同源 tools/build_wool.py WOOL_COLORS + 浏览器 t751 woolPalette；非 sheep/越界 → 白兜底）。② **掉落链**——mobDied/sheepSheared 两信号各加 woolIndex 参数（emit 处携 e.sheepWool 快照），呈现层 Main.qml sheepWoolDropId(woolIdx)：白=0x20E 材料段羊毛物品（bed_red 配方链与旧观感不变）、有色=羊毛方块段 63..77（方块 id 即物品 id 可放置回 + t788 染料链同产物形态，机制等价 MC 剪彩色羊掉对应色羊毛）；onMobDied/onSheepSheared 羊分支改走该映射。③ **游戏内渲染**——QML delegate 毛茸 Model 材质 baseColor 乘 sheepWoolTintAt 色（白=#ffffff 恒等不着色；裸态 sheared 不 tint 裸肤与毛色无关）。④ **浏览器联动**——ResourceBrowser 羊预览加 sheepWoolIndex 变体态 + 底部 16 色切换圆点（选中金框高亮）+ 预览着色走 sheepWoolTintForIndex 同值色板（图鉴预览=游戏内观感）+ 名称后缀「·X羊毛」。矩阵探针 t789（80 轮×60 只多轮清场重刷累计 4800 样本绕 kCap=64）：16 色 tint 表全有效+白恒等/黑棕锚点镜像浏览器 woolPalette + 权重分布（白>60% 主导、五稀有色全出现、2.5× 上溢护栏、表外色零容忍）+ 猪不受污染 + 剪毛载荷=羊自身下标且重剪幂等静默 + mobDied payload=死亡羊下标（专用 World 驱动 deathTimer 跨 0.5s 延迟窗）+ 幼崽继承父代色不重掷——**187 PASS/0 FAIL**（基线 186+1，两次运行一致）。探针教训入册：① EntityManager::tick(nullptr) 在 world 判空处整帧早退 → deathTimer 不推进 mobDied 永不发（死亡类探针必须传真实 World，对齐 t774 先例）；② 单轮采样撞 kCap 是权重断言隐形天花板（54/600 实为 64 只中 54 只）→ 多轮清场重刷是标准解法；③ 探针段间共享实体槽位，前段遗留生物会触发 kPassiveMobCap 钳死后续配对段 → 段前 clearAll()。**需人工目视**：野外刷羊群白主导带棕/灰/浅灰/黑/粉个体、毛色观感与浏览器 woolPalette 一致、剪彩色羊掉对应色羊毛方块可放置回、杀彩色羊掉对应色、繁殖幼崽继承亲代毛色。

### 🅵 成就系统（t790-t791）
**t790** 成就弹窗布局：太占位置 → 移**右下角**；可拖动区域背景与主 UI 视觉区分（半透明/描边）。 ✅✅ 0fd157b：**定位实证**——全工程成就弹窗仅两处：解锁 toast（infoToast，00ca6bd 起就 anchors.right/bottom margin 20/80 右下角、360×40 无任何拖动能力，谈不上「太占位置」）与进度面板 progressPanel（成就树，760×560 **居中模态** + 自身 0.7 全屏压暗叠在暂停 0.55 之上 ≈ 0.87 整屏黑）；「可拖动区域」全工程唯一成就相关命中 = 树视口 treeViewport（副标题「拖拽查看」，t637/t678 拖拽平移），只在 progressPanel 内 → 两 clause 同指 progressPanel。修法（Main.qml 单文件）：① 面板 anchors.centerIn → **右下角 dock**（right/bottom margin 16，尺寸 760×560 不变；内部全坐标系面板局部——节点公式 / tooltip 钳制 / clampPan / minimap / 返回按钮锚全随面板走，零逻辑改动）；② 撤 progressOverlay 自身 0.7 全屏压暗 → 全屏**透明**点击吸收层（modal 语义不变：不穿透暂停叠层 / 不误「点击恢复」）；③ 面板底 #1e1e1e → rgba(0.059,0.078,0.102,0.85) 半透明 + #3a444f 描边（t783 variantPanel 同款浮层语言；Rectangle rgba 而非 Item opacity——后者连带淡化子控件）；④ **可拖动区域**（treeViewport）加内嵌底板 rgba(0,0,0,0.35) + #3a444f 描边 radius 6（声明在 treeDragArea/treeCanvas 之前 = 绘制最底层不挡节点 / 交互）；treeMinimap 底板同步换一家色；⑤ 暂停菜单本体（420×360 居中块）progressOpen 时 visible:false——防右下悬浮面板半遮居中菜单成残缺观感，返回即回归（副作用：progress+stats 双开不可能了，菜单被隐点不到）。z 序不动（面板 z=155 < toast 170 < 死亡 180 < 主菜单 200）。测试：编译零警告；exe 10s 冒烟稳活零 QML 错误（stderr 0 字节）；矩阵 **190 PASS / 0 FAIL 不回归**（纯 QML 布局无 C++ 探针路径，t778/t783 先例）。**需人工目视**：① 暂停菜单点「进度」= 面板贴右下角（margin 16）、屏幕中心 / 左上让出（背后 0.55 压暗游戏可见，非整屏黑）；② 面板半透明深钢蓝底 + 描边、树视口再深一档内嵌描边框（拖拽区域读得出）、minimap 同族；③ 拖拽平移 / 滚轮缩放 / +− 收起 / 节点 tooltip / minimap 跳转全不回归；④ 面板底居中「返回」关面板 → 暂停菜单回归；⑤ Esc 关面板同效；⑥ 成就解锁 toast 仍在右下角原位（z=170 覆盖面板之上，3s 短暂不冲突）。
**t791** 骨粉催熟平衡：现状多个骨粉催不熟一株 → **3-4 个骨粉应催熟**（生长阶段推进概率调参，机制等价 MC 骨粉 2-5 次成熟语义）。 ✅✅ 已完成（**现状实证**：t447 实现是每骨粉确定性 +1 阶段 → 0..7 共 8 阶段从 0 要 7 骨粉（非概率赌运气，纯推进量不足）。修法 = **机制数值全收口 World 层新统一入口 `World::applyBonemeal(x,y,z)`**（playercontroller 只管命中分流/消耗/挥手 → 矩阵探针可直调锁数值分布），三类生长目标逐个核语义对齐 MC 1.0：① **作物（小麦/胡萝卜/马铃薯，共享 0..7 八阶段）每骨粉 +2..3 阶段**（哈希二值，钳顶 7；机制等价 MC 骨粉 +2~5 阶段推进语义、压缩上界——MC 的 +5 会 2 骨粉催熟出「3-4 次」带）→ 从阶段 0 **恰 3-4 骨粉催熟**（数学保证与哈希质量无关：3 骨粉推进和 ∈[6,9] ≥7 即熟，2+2+2=6 时第 4 骨粉钳到 7），期望 ~3.5 次；写入 5 参数 setBlock（id 不变 → 无破/放、worldChanged 重建阶段贴图）。② **树苗 45%/骨粉即时成树**（kBonemealSaplingPct=45，机制等价 MC 1.0 sapling bone meal——概率判定**非阶段推进**，树苗无阶段）：守卫同 tickSaplingGrowth（草地/泥土支撑 + hashColumn 同源 trunkH 4..6 + 主干列畅通），唯**光照豁免**（骨粉强制生长不等天光）；长成路径逐句复用 tickSaplingGrowth 应用段（清树苗静默 + placeTreeAt 完整橡树 + recomputeLightAround + worldChanged + clearAllDirty）；判定落空/守卫不满足**仍返 true 消耗**（MC 1.0 使用即耗）。③ **浆果丛 +1 阶段**（3 阶段 0/1/2，一骨粉一阶段，机制等价 MC sweet berry bush bone meal 单阶段推进；从 0 两骨粉催满）。已成熟作物/丛 + 非三类目标 → 返 false 不耗不挥（MC 骨粉对成熟/非目标无效应）。确定性（PLAN §2-K）：骰子 = hashVoxel(seed ⊕ m_bonemealUseIndex, x,y,z)，使用序号每次有效使用 +1 → 同株连续骨粉错峰推进、同 seed 同序列可复现零随机源。注释契约同步：recipe.h BonemealId 段 + hotbar.cpp 两处（创造调色板/nameForBlock）+ MaterialIcon.qml drawBonemeal 头注释改新语义。矩阵探针 t791（运行期扫描 y40..47 净空 20×5 区建 rig——P20 先例 nextSlot 已满；树苗须 y≤41 容 4 主干+2 树冠）：10 株作物（8 麦+胡萝卜+马铃薯）uses 全 ∈[3,4] 且带内 3/4 两端都达 + 每骨粉推进 ∈{2,3}（钳顶例外 after==7）+ 2/3 两值都出现 + id 不漂移 + 成熟返 false 状态保持；树苗 6 株全部成树（树基 Log）总投掷 10-24（6/45% 期望 13.3）+ 首掷即中与 ≥2 掷两态都现 + 石支撑/主干阻塞两守卫负例 30 骨粉不成树但消耗（返 true）；浆果丛 0→1→2→封顶 false；泥土/空气非目标 false——**191 PASS/0 FAIL**（基线 190+1）。**需人工目视**：① 手持骨粉右键刚种的小麦 → 阶段贴图连跳 2-3 档、3-4 个骨粉内麦穗全熟（生存每催扣 1 骨粉、成熟后再右键不耗不挥）；② 右键树苗 → 平均 ~2 次骨粉原地长成完整橡树（连点失败几次属正常 45%）、空间不足/石上树苗只挥白不长仍耗骨粉；③ 右键嫩浆果丛 2 次到成熟可采摘。

### 🅶 铁砧（t792-t794）⚠️ t792 最高优先（阻塞全部铁砧测试）
**t792** 铁砧 UI 附魔立即丢失（用户实测回归）：附魔物品放入铁砧 UI 的瞬间附魔就不见了（显示或数据层）→ t766 静态审计认为四分支已保附魔，但用户实测仍丢 → 实机复现定位（重点怀疑：左槽放入时的 UI 展示层未显示附魔 vs takeProduct 前的中间态真丢数据），修复后用户须能连续测试修复/合并/改名。✅✅ 已完成（commit c35adf9：**实机探针洗冤主链**——qml.exe 驱动真实 AnvilUI.qml+InventoryOps.js+Hotbar 桩，11 放入路径 47 检查全过（左键/右键/Shift/拖拽/双击拾取/取回/takeProduct 四操作/关包归还，数据+光晕+tooltip 三层），附魔放置链当前 HEAD 无罪——用户症状最可能 t699（8-19）前旧 exe；首轮 2"FAIL"系预期错（效率×时运同组互斥=设计行为）。顺手修 4 真缺陷：①slotShiftLeftAnvil 材料分支源槽余量短参 writeSlot 丢名 ②B 槽空写入名硬编码""丢带名材料名 ③takeProduct 无条件整组 wipe anvilEnch/Names 抹 B 余量材料元数据→收窄为确空才清 ④B 槽漏 slotDur 绑定不显耐久条；探针 47/47 + 矩阵 111 PASS）。
**t793** 铁砧贴图上部透明缝隙：与仙人掌同根因（d754011 pack 瓦片 alpha → 图集构建期 solidify anvil 族瓦片 anvil_top*/侧/底；程序态正常）。✅✅ 已完成（commit ccb82a1：**PIL 实测锁定 + cactus 同款一处改**——demo 包 anvil 顶面三瓦片（anvil_top / anvil_top_damaged_1 / _2）各带 37.5% 透明孔（6144/16384 像素 alpha<128，MC 铁砧顶面 footprint 小于整格、贴图按满格绘制四周留空；anvil_base 实测 0% 实心，solidify 幂等无害）→ 世界内砧台顶盒 +Y 用 topTile 113/115/116 整张铺面，透明孔被读作「上面没连上的透明缝」。修 = ensureBuiltLocked 瓦片循环加 `contains("anvil")` → solidifyTile（透明孔填不透明像素平均代表色，d754011 仙人掌同款；程序态 pack 关不经此路径恒正常）。**端到端验证**：冒烟运行重写 voxelsandbox_rp_atlas.png 后 PIL 复测瓦片 113-116 全 4096/4096 完全不透明；**图标缓存核查**：铁砧图标经 blockAtlasIconSource 从合成图集渲染（进程内 apply() 换 r<rev> 文件名 / 重启内存缓存清空重渲覆盖），无需换 icon4 文件族名。**顺手核**（全量已映射瓦片 PIL alpha 扫描）：enchanting_table_top / endframe_top 均 0% 实心无同病（两者侧瓦片顶部空白带已由 cropTopBlank 特判裁掉）；其余带 alpha 瓦片均为合法 cutout 语义（cross 立绘/门/铁轨/玻璃/树叶/spawner 铁笼）刻意保孔不处理。build 零警告 + redstone_matrix_test 181 PASS/0 FAIL（基线持平）+ exe 冒烟 12s 不崩。**需人工目视**：pack 开态铁砧上部砧面无缝（完好/微损/重损三阶段顶面均连续不透）。
**t794** 铁砧重力：放置后下方被挖空/无支撑 → 转**下落方块实体**（沙/沙砾同链）；下落砸中生物扣血（伤害随落差）；落地播放音效。✅✅ 已完成（**复用度结论：t799 重力链对铁砧 ~90% 开箱即用** —— isGravityBlock 谓词加 isAnvil（三阶段一处）后失撑坍落 / 下落物理 / 放置与更新同判全通，World 层零新代码；本任务只补三条分叉语义：① **砸伤**（EntityManager tick FallingBlock 新增 crush 检查：本 tick 扫掠盒 [min(pos.y,newY)−halfH, pos.y+halfH] × 活体 mob + 玩家 AABB 重叠，**先伤后落**（着地还原前结算；生物不挡下落体，铁砧穿过它落到下方格），伤害=(floor(落差)−1)×2（kAnvilMinFallBlocks=2 格起伤 / 每多 1 格 +1♥ / kAnvilCrushDamageCap=40HP 上限，机制等价 MC 1.0 anvil crush），每实体每次下落只伤一次（Entity.anvilDamaged 一次性拍 + fallStartY 落差基准，DMI 兜底防槽复用残留）；玩家走 mobAttackedPlayer 携 **MobAnvil=18 哨兵** → DeathCause::Anvil「被落下的铁砧砸死」+ 护甲减伤链（同 MobTnt 先例）；创造/观察者 playerTargetable 门控无敌）。② **着地恒还原铁砧方块**（完整立方支撑 + 落不完整方块（火把/半砖）**两分支都还原**，与沙 t220「落部分方块碎成掉落物」语义分叉 —— 机制等价 MC 铁砧落地不碎成物品；不改损坏阶段落地还原原阶段；还原经 setBlockFromEntity 直写不经 checkGravityBlockOnEdit → 浮在火把上方不二次坍落，破掉火把才再落，无死循环）。③ **落地音**（新信号 fallingBlockLanded（仅铁砧族发，沙/砾维持无着地音旧观感防塌落刷屏）→ Main.qml audio.playBreak(blockId)，铁砧 SoundType 归 GroupStone 金属质 = 重铁落地声，零新音频资产）。视觉：falling 铁砧 BlockCube 单立方 XZ 缩 0.75（12/16 底座足印）+ 满高 1.0 观感折衷（三盒异形 falling 渲染未做，侧贴图 anvil 瓦片自带头座分层仍读作铁砧形；需要精确三盒再补）。测试 t794 矩阵探针：(P) 圈养猪（墙围防走脱；aiWander 无跳跃盒顶恒定）上方铁砧落 3 格恰扣 2HP / 落 5 格恰扣 6HP（落差单调 + 恰一次）+ 着地还原于猪格；(U) 挖支撑 → gravityBlockFell(Anvil) + 还原 + landed 恰 1 + 零掉落物；(T) 落火把上还原火把上方不掉物品 + 火把原位不动；(L) 砸玩家 mobAttackedPlayer(MobAnvil) 2HP→6HP 单调 —— **182 PASS/0 FAIL**（基线 181+1）。附带修：t799 探针 gravityBlockFell 连接 context 误挂 &w（块局部 [&] 捕获块外悬空 → 本探针再发该信号即 UB）改挂块局部守卫对象自动断连。**需人工目视**：铁砧放火把上即刻坍落；高塔落铁砧砸猪红闪掉血与落差成正比、砸玩家红闪 + 死因「被落下的铁砧砸死」；落地重铁声；落半砖上还原铁砧浮其上（破半砖再落）。

### 🅷 附魔台（t795-t798）
**t795** 附魔选项门槛：放入工具立即显示三选项高亮 → 应**放入青金石后**才显示可点选项（lapis 槽位消耗 1-3 对应等级；创造模式同样需要，且**创造也要书架达标**才有最高等级——书架数上限封顶逻辑补齐）。✅✅ 已完成（commit 0657179：**两根因双收口**——① `affordable` 门禁 `creativeMode ||` 短路把 lapisCount 校验一并跳过 → 创造放工具立即三档全亮；改 `lapisCount >= lapCost && (creativeMode || level >= cost)`：青金石**恒须足额**才亮（创造只免 XP 等级，与 doEnchant 恒校验/恒扣 lapis 口径对齐，附魔扣减/关包归还链本就无丢失——doEnchant 余数写回槽 1 + returnEnchantToHotbar 归还，本轮复核未动）；② `maxLevel` 的 t694 `creativeMode ? 3` 创造直通旁路删除——档位公式（1 恒开/≥5→2/≥10→3）与 offered 公式（floor(bs*20*(t+1)/33)+(t+1) 钳 [1,30]，顶格 30 仅满 15 书架）收编 **EnchantRegistry::tierForBookshelves / offeredLevelFor** 单一权威（**函数无模式参数 = 创造也须书架达标**，杜绝 QML 副本漂移），Hotbar 桥接 enchantTierForBookshelves/enchantOfferedLevel 供 QML 绑定。矩阵探针 t795（公式域+单调性 + 桥接同值 + World 环带 0→15→堵半步 14→32 位全放封顶 15；首轮两坑：nextSlot() 已被前序探针耗尽返越界 = 假 FAIL，改运行期扫空区（P20 先例）；堵边中点半步掉 3 本——(dx/2,dz/2) 向零取整令边中点半步被 3 环格共享，角位半步只服务自身，探针改堵角位精确 -1）——**180 PASS/0 FAIL**（基线 179+1）。**需人工目视**：① 放工具后无 lapis 三档灰、放足 lapis 亮（含创造模式）；② 创造模式书架不足高档锁（0 书架只 1 档）；③ 附魔一次扣 lapis 1/2/3、关包余量归还背包。
**t796** 附魔书模型修复：① 书太低，上下浮动时穿模进附魔台台体 → 抬高悬浮基准；② 贴图一面书页一面书皮（像翻完的书）→ **两面都是书页**；③ 静止时也要有**翻页动画**（不只上下浮动；持续小幅翻页+浮动复合）。✅✅ 已完成（commit 6a0c3ba：**三修各根因**——① 穿模算账：全书最低点=书脊条底 −0.035 在 lean+20° 近端 z+0.23 → −0.112，叠 bob 谷底 −0.035 后旧基准 0.82 最低 y=0.673 < 台顶 0.75（穿入 0.077，用户所见属实）→ 基准 0.82→**0.95**（谷底最低 0.803，安全隙 ~0.05≈0.8 像素格；EnchantGlyphFlow 粒子终点同步 0.85→0.95）；② 左页 EnchantBookBox piece 0（封面区）→ 新增 **piece 4 纸页镜像**：与 piece 1 同区采样但 ±Y/±Z 大面 u0/u1 互换——左页几何 u=0 在外缘/右页在书脊（相反），不互换会令暗化纸缘 x[32,34)（qrc 实测均色 222 vs 纸心 235）落到外缘；互换后暗缘两页都落书脊、符文行互为镜像（真开书对称），qrc/pack 双布局同机械互换；封面贴图区自此只余 item 图标叠层消费（resourcepackmanager 注释记录有意分叉）；③ flipPivot 加 flutter 属性（0↔14° 无限往复，起 0.8s/落 0.9s 不对称节拍）与 bob 复合；完整 130° 大摆保留且互斥（onStarted 停 flutter 清零——14° 叠加会令落角 166°>左页平面 158° 穿页背；onFinished 重启）。**冒烟抓到 1 真缺陷**：SequentialAnimation 无 onCompleted（Component 信号≠Animation 信号，QML 整文件拒载黑窗）→ 改 onFinished。**180 PASS/0 FAIL**（基线持平，纯视觉项无 C++ 可测路径——QQuick3DGeometry 无顶点回读 API，矩阵测试不链 Quick3D）。**需人工目视**：① 书浮动全程不触台顶（bob 谷底书脊近端角留隙）；② 两页都是纸页符文（pack 开/关两态）；③ 静息时页片持续小幅翘落 + 每 1.4-3.2s 一次完整翻页；④ 字形粒子涌入新书心高度 0.95。
**t797** 书架→附魔台文字动画重做：现状"打开附魔台才播、动画超大像爆炸" → 改：**放置书架即自动播放**（不依赖开 UI）；白色小字、透明背景；从书架**缓慢漂向**附魔台、途中渐隐、到达即删（t765 EnchantGlyphFlow 调参/触发重构）。✅✅ 已完成（commit 1033646：**触发重构**——驱动源从「当前所开台」（enchantX/Y/Z + enchantingTableOpen 门控）改为**全图台×有效书架对**：Main.qml 注入 enchantTablePositions（tableModel + tableCount 绑 count——增删台/读档重建 clear+append/onWorldChanged 孤儿清理均触发重扫），组件内逐台套 countBookshelvesAround 同规则（切比雪夫==2 环带×两层+半步格 Air，blockAt 只读零 World API）成 pairs；active=playing 常驻（**不再门控 UI 开**）→ 放书架 ≤0.5s 起流、拆书架/台即刻停对；UI 开=可选加分已做：所开台书架发射率 ×uiBoost(4)。**四项视觉调参**——① 面片 scale 0.10-0.16→**0.055-0.085**（「爆炸感」→小字）；② 色板紫系→**纯白系** ["#ffffff","#f4f6ff","#e9eeff"]（透明底图集 × Blend，白色小字）；③ 漂速 2.6→**0.9 格/s** + 寿命钳 1.8-4.0s（一程 2-4s 缓慢漂向）；④ 渐隐律 = 前 12% 淡入 × (1-k) 线性全程渐隐 → t=1 恰落书心且 alpha 已归零即回收（到达即删，书心终点保持 t796 的 y+0.95）；横摆幅 0.05→0.04、弧高 0.30→0.20（慢漂不夺目）。**性能四守卫**——每书架 ~0.22/s（3-6s 一粒，期望值法整数+小数概率补 1 = 随机非节拍）、全局 maxPerTick 4/500ms（≤8/s）、池上限 36 满静默丢、**发射距离门** 书架离相机 >16 格（水平 256 格²）不发射；无对 spawnTimer 停 / 在飞放完 tickTimer 全停零常驻。Main.qml/CMakeLists 注释同步改语义（旧「UI 开期间密集流」表述防误读）。**build 零警告 + 冒烟无 QML 错误**（[t797] ready/Loader Ready/adopted 三行齐全，t796 黑窗教训）+ **redstone_matrix_test 180 PASS/0 FAIL**（基线持平；纯视觉项无 C++ 可测路径）。**需人工目视**：① 不开 UI 放书架 → ≤几秒内白色小字从书架缓慢漂向台、途中渐隐、到书心消失；② 字明显变小（不再「爆炸」感）、纯白透明底；③ 拆书架/台 → 流停；④ 开附魔台 UI → 所开台字流明显加密（×4）；⑤ 远处（>16 格）的台架对不发射。
**t798** 效率附魔审计：① 木镐附效率后挖掘像效率 V → 应按**等级递增**（每级固定增幅，非全统一到顶级速度）；② 效率只对**匹配工具-方块**生效（镐挖石类加速，挖泥土/沙无加成；斧→木、铲→土石类同理）——全附魔公式复查（对照 t763/t476 数值表）。✅✅ 已完成（commit 188ec03：**根因 = t476 旧式「progress ×(1+level) 乘法 + 不查工具-方块匹配」**——乘法对低等级偏强（I 即 ×2 全时长）且镐附效率挖泥土/沙白吃加成（用户两症状同源）。修法**收口单一权威**：ToolRegistry::miningTime 加第 3 参 efficiencyLevel（默认 0，既有调用点签名不变），机制等价 MC 1.0「效率在工具基础速上**加法**叠 level²+1（I +2/II +5/III +10/IV +17/V +26）且仅当工具速度加成已激活（类型匹配+采掘等级达标，mul>1）」；playercontroller updateMining 删本地 effMul 改传参（progress += dt/(miningTime×waterPenalty)）。**数值表（木镐 speedMul 2 挖石 1.5 硬度，秒）**：无附魔 0.750/I 0.375/II 0.214/III 0.125/IV 0.079/V 0.054（旧：0.750/0.375/0.250/0.188/0.150/0.125 各级统一乘）；铁镐效率 III 挖石 1.5/16=0.094；木铲效率 I 挖土 0.5/3.2=0.156（匹配吃加成）；**泥土/沙恒定对照**：效率 V 木镐 == 无附魔 0.5s（旧 0.083s 白吃）；**采掘门槛门控**：木镐效率 V 挖黑曜石仍 96s（mul 1.0 不吃效率，t762「钻石镐 12s/低档 96s」零回归；钻石镐效率 V 96/34≈2.82s）。**全附魔公式复查（t763 表 12 项）**：锐锋 +0.5/级、亡灵/节肢 +2.5/级（对族）、击退 +50%/级、燃焰 4s/级、时运 ×(1+[0,级])（限矿）、保护族 EPF 通用 1/专项 2 每级、耐久按级概率跳过、精准采集/水中亲和 maxLevel 1 二值——全部等级分档，**仅效率旧实现犯「统一不分档+全方块生效」病**。矩阵探针 t798（六档递减表+泥土/沙恒定+铁镐/木铲交叉+黑曜石门槛）——**181 PASS/0 FAIL**（基线 180+1）。

### 🅸 方块物理与物品归类（t799-t801）
**t799** ✅✅ 沙/沙砾即时下落：放在火把/睡莲/草丛/半砖等**非实体支撑**上 → 应**立即转下落实体**（现状稳定站住，只有 >1 格落差才变掉落物）；放置路径与更新路径同判（MC 语义：支撑失效即刻落）。
  - 实现（fix t799）：判定下沉 World 层单一谓词——`BlockRegistry::isGravityBlock(Sand/Gravel)` + `isFullCube(下方)` 支撑判定；`checkGravityBlockOnEdit` 挂全部 7 个网格写入口（setBlock×2 / clearBlockSilent / setWaterSilent / setBlockSilent / tickFire 烧毁 / destroySphereSilent 爆炸）→ 失撑发 `gravityBlockFell` → 呈现层 Main.qml `onGravityBlockFell → spawnFallingBlock`（同 t527 雪层模式）；旧 Main.qml `maybeTriggerFallingBlock` 嵌套判定整体删除（放置==更新同判达成）；坍落列静默直写防重入。矩阵探针 6 场景（放置族/稳定柱/挖撑/半空/水穿/爆炸+TNT 点火）全 PASS，176/0。
**t800** 物品栏归类清理：① 材料栏的羊毛 item 删除（方块栏已有羊毛方块，多此一举）；② 玻璃移到方块栏（现处材料栏）且玻璃 item 图标改 **2D 平贴图**（非 3D 立方投影）。✅✅ 已完成（commit 5f46f12：① creativeMaterials 删 WoolId 0x20E 条目——物品本体保留全部生存链（杀羊/剪羊毛掉落 + bed_red 简化配方原料），仅不再列材料 tab（建筑取色走方块段 16 色 wool，t788 染料链基底=白羊毛方块不受影响）；② creativeMaterials 删 GlassId 0x204 条目 + creativeBlocks 冰族后加 Glass=54（透明整立方同族排列；资源查看器/背包/调色板三处同源表自动一致）；item 图标 atlasIconSpecForBlock 加 Glass→flatSpec(瓦片 68) 2D 平贴（pack 态采包 glass.png / 程序态采 default_glass，对齐 MC 玻璃物品图标=平面贴图语义）；关键补洞：Glass 无 qrc 手绘图 → 入 isPackDerivedIconFamily（否则 pack 关态回退链走空 = 玻璃条目无图标）；图标缓存族 icon3→icon4 换代（54 号画法 dimetric→flat，防旧立方投影缓存永久复用）。矩阵探针 t800：材料段两 id 不在列 + 方块段含玻璃/白羊毛/15 色变体 + 两物品名仍解析（生存掉落 tooltip 源）+ 玻璃方块图标 URL 可解析——**177 PASS/0 FAIL**（基线 176+1）。**需人工目视**：创造方块 tab 玻璃条目显 2D 平贴图（pack 开/关两态）、材料 tab 无羊毛/玻璃残留。
**t801** 栅栏视觉高度：视觉改 **1 格高**（mesher 几何裁剪；碰撞/跳跃判定保 1.5 不变）→ 消除 0.5 格悬空穿模观感。✅✅ 已完成（commit 6d11999：视觉/碰撞分离 = MC 栅栏语义「模型 1 格 / 碰撞箱 1.5 不可越」。① mesher 立柱 y[0,1.5]→[0,1.0]、上档 0.9375..1.125→0.75..0.9375（MC 12-15/16，两道档随立柱同裁；贴图零适配——pushBox 各面 cu,cv 恒单位 {0,1} 整瓦拉伸采样，1.5→1.0 后侧贴图从 1.5:1 回归 1:1 比例反而更正）；② shapeBoxes ShapeFence 1.5 原盒不动（自此**只喂 collisionAABBs**——跳跃顶点 ~1.25<1.5 玩家/mob 越障+支撑链零改动）；③ selectionAABBs + raycastAABBs 对 isFence 特例 1.0 盒（选中框/射线贴视觉，瞄立柱上方 0.5 空带穿过不再优先选中——t639「碰撞/选中/射线/渲染四消费者逐个同步」铁律的栅栏版，耕地矮框/木梯透视同款特例模式，isFence 谓词一次覆盖木/圆石/云杉三变体）；④ solidTopOffset ShapeFence 1.5→1.0（PCF 软影列顶实面随视觉，唯一消费点 columnTopSurfaceY）。资源包图标（resourcepackmanager ShapeFence 盒）本就 1.0 高与新视觉天然一致零改动。矩阵探针 t801（Core 表查询 + mesher 同源直调，纯静态无 rig 不占 nextSlot）：三变体 × 孤立/四向连接两形态全部顶点 y≤1.0+ε 且立柱到顶≈1.0（不缩水）、横档仍达格边（t209 连接不回归）；分离断言 collision 顶 1.5>跳跃顶点 1.25（kJump=8.4 文档镜像值）/ selection+raycast 顶 1.0；对修复前几何复验 FAIL（yMax=1.5 检出）确认灵敏——**178 PASS/0 FAIL**（基线 177+1）。**需人工目视**：栅栏三变体贴墙/摆箱不再 0.5 格穿模悬空、跳跃仍上不去、选中框 1 格贴模型、连接横档位置观感。

### 🅹 合成体系审计（t802）
**t802** 全配方审计：① 云杉原木→云杉木板→木剑/木镐等**木制品链丢失**（云杉木制品配方核查）；② 打火石合成不了（t761 改的燧石+铁锭配方验证）；③ 燃烬棒→燃烬粉分解配方缺失；④ 举一反三全表过一遍（RecipeRegistry 对照材料/工具/食物/染料族，缺失/错料/错形状逐条修，核对表进 commit）。✅✅ 三报障全实证（非陈旧 exe）：① 根因 = MC 1.0 木板是单一物品 id + 木种 metadata（配方通配任意变种），本工程云杉木板 SprucePlanks=86 是独立 id 且 t466 只补了有独立云杉产物的 4 条（板/台阶/栅栏/门），木棒/工作台/五件套木工具/木碗/床/书架等**通用木制品**原料写死 Planks → 精确匹配永认不了云杉板；修法 = 匹配器**两阶段**（recipe.cpp match：第一轮精确——云杉专属配方 spruce_slab/fence/door/boat 优先命中不被截胡；无果且输入含族内变体才第二轮规范化 SprucePlanks→Planks 重试，等价表 kIngredientEquivalents 单一权威，未来新木种补一行全链自动通配）——机制收敛在匹配器而非配方表复制 30+ 条平行变体（平行复制必带形状漂移）。② 部分洗冤+真修：t761 配方存在且纵列（铁上燧下）摆法本可合，但 MC 1.0 原版是**无序**配方 → 玩家按 MC 习惯横摆/斜摆全不匹配 = 报障根因；改 shapeless（torch_coal/bed_red 早有无序双原料先例，t761 注释「无 shapeless-2 先例」不实）。③ MC 正道是**合成**分解（1 棒→2 粉，1.0 无熔炉烧棒配方）；t726 只接了熔炉路径（审查 B4 补）→ 补 shapeless 分解配方，熔炉路径并存同产物。④ 全表审计结论（核对表进 commit message）：补缺 8 条（箱子 8 板环/梯子 7 棒 H→3/砂岩 4 沙/切制砂岩 4 砂岩→4/石砖 4 石→4/石砖台阶 3→6/石砖楼梯 6→4/发射器 7 圆石+弓+红石——全部「方块既存、机制完整、唯配方漏注册」族内断裂）；错形状 1 条（圆石压力板 Table3x3→2×2，t627 注释口径代码漏改）；错料 1 条（箭 t304「无燧石/羽毛」的本地化理由已过时（t398 鸡掉羽毛/t761 沙砾掉燧石）→ 改回 MC 正统燧石+棒+羽毛→4）；云杉熔炉链补 3 行（SpruceLog→木炭 + SpruceLog/SprucePlanks 燃料 15s）；工具五件套×6 材质 30/30、护甲 20/20、存储块 6/6 双向、床 16 色、压力板 5 族全对称无缺。回归基建：RecipeRegistry 新增 recipeCount/recipeAt 只读迭代 → 矩阵探针 t802 全表 147 条「自身 pattern 自匹配」指针相等断言（防遮蔽永不可合 + 防未来匹配算法改动静默丢配方）+ 表长下限 140；三项报障 + 补缺新配方 + 圆石板 2×2 + 箭新旧正反 + 云杉熔炉链逐一探针——**179 PASS/0 FAIL**（基线 178+1）。**需人工目视**：云杉木板按橡木工具形摆出木剑/木镐；打火石任意摆法合成；燃烬棒放合成格出 2 粉。

### 🅺 火与点燃（t803-t805）
**t803** 打火石与火焰基础修复：① 打火石第一人称**手持不可见**（held 模型路由/图标）；② 火焰动画错位（从格子上方向下播放 → 对齐所在格）；③ 生物碰火**不燃烧**（僵尸实测 → 火 tick 点燃 mob，持续伤害+着火贴图/粒子）。✅✅ 1627f09：三缺口全实证（非陈旧 exe）。① 根因 = viewModelHand 工具分支只覆盖 type 1-8，打火石（FlintSteel 9）无渲染分支（工具段 selectedBlock=Air → 方块分支也不命中）= 空手；掉落物 delegate 同缺 type 9。修：手持 + 掉落各补 billboard ToolIcon(type 9) 分支（同剪刀 6/钓竿 8 路径；alphaCutoff:0.5+opacity:0.99 alpha-test 契约）。② 根因 = 旧 fire_strip.png 每帧是基准帧的**纵向循环位移**（numpy roll）→ 翻书时火焰内容逐帧平移穿过格子 = 用户「火从格子上方往下播放」；pack 态 fire_0 帧序本身正确（原地变形）。修：tools/build_fire.py 重写为**原地闪烁**（三火舌高度/底宽/热点随帧确定性散列抖动，绝无帧间位移；帧 0 在底的 t563 ② 约定保持），numpy 复核帧间零 roll + 底行锚定。③ 根因 = mob 碰撞/支撑/越障四谓词（mobAabbHitsSolid/mobFootprintHasSupport/mobSupportTopY/isJumpObstacle）消费 World::isSolid（语义=非 air 实存）→ Fire（ShapeNone 无碰撞盒）被当实体墙 → mob 永远走不进火格（t724 点燃判据「脚位/身体格==Fire」永不命中）且 isJumpObstacle 还对火格起跳翻过；另旧版站火每 AI tick 清零火伤累积器 → 泡火不扣血（玩家侧 t351 的 mob 镜像）。修：四谓词 Fire 视穿透豁免（同作物/水族）+ 站火只刷 fireTimer 不清 fireDamageTimer。测试 P20 矩阵探针：(a) 追击僵尸穿火格点燃（HEAD 旧象停在格边/跳过永不燃）、(b) 困栏僵尸站火 20s 燃烧近全程（随机熄灭复燃隙 ≤60 tick）+ 周期火伤 ≥10HP 不死、(c) 拆火 ≤9.5s 停燃（fireTimer 8s 定时）+ 血量恒定——**173 PASS/0 FAIL**（基线 172+1，3 轮稳定）。**需人工目视**：手持打火石贴脸图标（pack+qrc 两态）/ 火焰原地舔动不上窜 / 僵尸穿火燃 + 持续掉血 + 出火 8s 后熄。探针教训：nextSlot 网格 124 位已满（越界假 FAIL）且 kRigY=41「40 以上必空」不可信（(6,41..43,1) 有生成石柱，spawn 嵌墙窒息）→ 新探针**运行期扫描净空行**而非固定坐标。附带发现（未修，非本任务范围）：钓竿（type 8）掉落物渲染分支同缺。
**t804** 点燃交互扩展（仿 MC）：① 打火石右键**木制品方块**点燃（方块着火贴图+向相邻木方块蔓延）；② 对苦力怕右键 = 按 MC 规则点燃引爆（原地短引信，非不可逆追踪蓄力态）；③ **火中丢弃物品被烧毁**（火格 item 实体销毁+烟粒子，对齐岩浆烧物）。✅✅ 逐项 HEAD 现状实证：① 点燃入口（打火石右键面邻空格生 Fire）+ tickFire 蔓延烧毁链**已在**（t724：可燃邻被 setBlock(Fire) 替换=烧毁、无掉落、发 blockPlaced 不走破块链）——真实缺口是**速率语义**：旧蔓延每窗只随机挑 1/6 邻掷 5%（单块木板期望 ~60s 被吞，5%×1/6/0.5s）→ 用户眼看木墙点不然；改 6 邻**逐格独立掷** 5%/窗（哈希混邻格坐标保邻间独立）→ 每块 ~10s（与头注释既有「~10s/格」文档对齐），kFireCellCap 安全阀不变。② 新增 EntityManager::igniteStalkerFlint（置 Entity.flintIgnited 不可逆短引信态）+ aiStalker 顶部短路分支（无视追踪/距离/猫无条件蓄力至 kFuseTime ~1.5s 原地引爆，优先于猫驱赶/追踪/defuse 全部可熄火路径；嘶声/膨胀/QML 动画全复用近距蓄力链）；PlayerController 打火石分支前置 mob 射线（findMobHit 同剪刀模式 + `mobDist<=m_hitDist` 遮挡守卫同 t653②）命中 Stalker 优先点燃（消耗耐久+挥手；幂等）；普通近距蓄力链零改动。tick Mob 分支 !playerTargetable 门对 flintIgnited 豁免（切创造/观察者已点燃的引信照常烧完）+ 蓄力期 revision bump 扩 flintIgnited（远场点燃膨胀动画照播）。③ 根因 = ItemEntityManager 重力列扫/resting 复探消费 World::isSolid（非 air 实存语义，t803 mob 侧同族）→ 掉落物**骑在火格顶面**永不进格 = t724 火焚分支对「直落火格」恒死代码（岩浆 t343 靠水平抛入格内才触发）；修：两判定局部豁免 Fire（同既有 `!=Water` 例外模式，不动共享谓词）→ item 落进火格；t724 瞬毁改 0.8s 点燃窗（kItemFireBurnSec，首触置 fireBurn 倒计，归零 releaseSlot+emit itemBurned 呈现层 burstDeathSmoke 白烟；出火清 0 可抢回；岩浆保持瞬毁无烟，两者语义刻意不同）。测试 P21 矩阵探针：(a) 6 连木板墙端点火 600 窗全烧毁（blockBroken(Planks)=0 烧毁无掉落）+ 火终自熄、(b) 远场+!targetable 下 igniteStalkerFlint → 1.54s 引爆恰一次爆炸+膨胀可见、猪类型拒、(c) item 直落火格 81 tick 焚毁（落地 ~31+0.8s 窗）+itemBurned 恰一次坐标在火格 / 岩浆格内生成 ≤3 tick 瞬毁对照 / 入火 55 tick 拆火抢救存活 200 tick——**174 PASS/0 FAIL**（基线 173+1）。**需人工目视**：木屋点燃 ~10s/块蔓延烧穿 / 打火石右键苦力怕原地膨胀 1.5s 爆（逃远也爆）/ 丢物入火 0.8s 白烟焚毁（短窗可捡回）。教训（入 lessons）：isSolid 的实体侧消费者（mob 四谓词 t803 / item 列扫+复探 t804）须逐个豁免非实心光源格——同一共享谓词的每个实体族都是独立漏洞面。
**t805** 船上岸回归：船又能直接开上岸（此前修过又坏）→ 定位回归提交（git log 船水检测），恢复"离水减速不可加速上岸"语义。 ✅✅ 已完成（**回归根因 = 21fff7b（t711 五修）**：「冰面可上/沙滩不可上不一致」把碰岸探测的 ignoreIce 豁免从「仅冰族 isIce」扩成「与水面同高的任何固体」→ 世界海缓坡（seaColumnHeight 每 ~12 格升 1）使 h==waterLevel 的同层湿沙带常宽 10+ 格，整条带变「可行驶表面」→ 船从海里顶着 W 直接开上沙滩深处，t661「上岸应难/需速度」语义被冲掉。修复：豁免收回仅冰族（冰=船可行驶表面/沙岸=岸，本就应不同；干沙滩 ring 1 格高仍须 beachTimer 冲量）；离水减速常量保持 t584 原值（kBoatLandSpeedMul 0.3 → 2.4 b/s 怠速，水:陆 ≈ 3.3 骤降比）；贴岸倒退（t611 只清朝向分量）保留。redstone_matrix_test t805 探针：水道满速 8 / 陆道怠速 2.4 速比 ≥2.5 + 同层湿沙挡停（中心不越沙列界、仍浮水面 Y）+ 倒挡退水 ≥3 格 + 同层冰仍可滑上（防过度回退）；对修复前代码复验 FAIL（船越界 6.5 格骑上沙顶）确认探针灵敏。矩阵 175 PASS/0 FAIL）

### 🅻 下界传送门（t806）
**t806** 传送门粒子与尺寸：① 创建/破坏时的白色粒子 → 改**门色（紫）**粒子；② 大尺寸门支持：2×3 是**最小**尺寸，更大门框（宽 ≤4 高 ≤5 等按 MC 规则）也能点燃成门（框检测泛化）。✅✅ 已完成（commit 1e8b577：① 白粒根因 = BlockParticles.qml blockColor 全枚举色表（t513）缺 138 行 → 门创建（burstPlace 逐门格）/ 直挖破坏（burstBreak）全走 default 白；补 case 138 = #7a20b2（tools/build_portal.py 条带主体紫 mid=(122,32,178)，对齐门方块紫漩涡贴图；连通域静默清余格不发粒子同画 t721 模式不变）。② 框检测泛化 + **三件套下沉 World 层单一权威**（同末地门 endPortalRingComplete/tryOpenEndPortal 先例——t725 v1 写死 2×3 且住 PlayerController，矩阵不可直编）：tryIgniteNetherPortal 重写四步「下探底梁 ≤5 步 → 左探开口左沿 ≤3 步（有界防 OOB air 滑出）→ 量宽量高（有界截断，超限部分随后梁柱校验自然判败）→ 矩形+框架校验」；MC 规则参数表：**内腔开口宽 2..4 / 高 3..5（框外沿 4×5..6×7）、矩形开口、黑曜石底梁/顶梁（开口正下/正上各 w 格）+ 左右边柱（各 h 格）、四角不检查**（MC 1.0 角块可选——缺角可点燃、破角不碎门，与失撑扫描够不到对角格两侧自洽）；全命中 → 开口整面 w×h 填 NetherPortal（state=axis 逐格 setBlock → portalHost 逐格 delegate）。removeNetherPortalAt/breakNetherPortalsAround 逐行同源下沉（连通域 BFS 尺寸无关：破任一承重框格（梁/柱）或直挖任一门格 → 整门熄；PlayerController 打火石分支 / finishMiningAt 两分支改转发调用，行为零变）。矩阵探针 t806（独立小世界 World 直调）：2×3 最小 X 面 6 格 state=0 / 4×5 最大 Z 面 20 格 state=1（点燃填满整个开口）/ 四角+中部 5 个点燃位位置无关 / 5 宽 6 高超限拒 / 1 宽 2 高低于最小拒 / 缺角 3×4 成门 12 格 / 缺底梁·顶梁·边柱拒 / 腔内异物（非矩形）拒 / 破底梁·顶梁·双边柱任一 → 20 格全熄 / 破角块门健在——**192 PASS/0 FAIL**（基线 191+1）。**需人工目视**：门创建/破坏粒子呈门色紫（非白）；搭 4×5 内腔大门框点燃整面成门、破任一边柱整门熄、拆角块门不熄。

### 🅼 投掷物模型（t807）
**t807** 末影之眼/珍珠投掷模型：现状六面都是"眼睛" → 改为**掉落物式贴图**（单面 item 贴图 billboard/小平面，对齐掉落物渲染语义）。 ✅✅ 已完成（根因 = 旧版 EnderEye/EnderPearl 投掷物 delegate 用 UnitCube 六面立方铺同一张贴图：眼是 entity_endereye.png 满幅全脸（六面都是眼睛，用户主诉）+ pack 命中 ender_eye.png；珍珠 pack 命中态同病（六面都是珠）。修法 = 两者改掉落物材料段同款渲染：BillboardQuad 单面 billboard（eulerRotation = cam 欧拉恒正对相机，scale 0.30 同掉落物统一）+ MaterialIcon item 图标作图源（0x23A drawEndEye / 0x243 drawEnderPearl；MaterialIcon 内部自带「pack itemIconSource → 自绘 Canvas」两级 → 顺带退役投掷物私有 endereyeTex/endereyePackTex/enderpearlPackTex 三 Texture 声明，entity_endereye.png 留 qrc 不再引用）；alpha 契约沿掉落物段（alphaCutoff 0.5 + opacity 0.99 透明底丢弃）+ baseColor 乘 terrainLight 夜间变暗；飞行自旋从绕 Y（对正对相机面片无意义）改**面内 roll**（eulerRotation.z=spin，Z→X→Y 应用序 → 图标自身平面打转再随相机摆正，眼读作「翻滚的眼珠」）；碎裂动画（缩小淡出 + 玻璃碎屑）原样保留（承载 Node 均匀缩放与 billboard 旋转可交换）。**同族排查结论（t803 教训）**：雪球/鸡蛋/火球/箭均为纯色自绘组合体（无贴图），不存在「同一 item 图标铺满六面」错渲染病（纯色小体各角度读作球/弹非贴图方块）→ 维持原创体积模型不改（结论已钉进 Main.qml 雪球 delegate 注释）。纯视觉项不加矩阵探针（t781 先例）；redstone_matrix_test 复跑 **192 PASS / 0 FAIL** 不回归。**需人工目视**：掷末影之眼 = 正对相机的小眼珠平图标面内自旋、飞距终 80% 变掉落物图标可捡回 / 20% 缩小淡出碎裂；掷暗渊珠 = 正面小珠平图标抛物飞行、落点传送；pack 开（ender_eye/ender_pearl.png）与关（自绘）两态图标正确、夜间随天光变暗。

### 📎 R19.12 范围与顺序
t767-t807（41 项）。**建议顺序：t792 铁砧 UI 附魔丢失最高优先（用户明确被阻塞，无法测试铁砧系统）→ t772-t776 红石激活矩阵（t773 TNT 链依赖 t772 电源语义）→ t767-t771 矿车铁轨族 → t803-t805 火与点燃 → t799-t802 方块物理/归类/合成 → t795-t798 附魔台族 → t785-t787 蛋与笼 → t788-t789 染料 → t777-t784 浏览器模型族 → t790-t791 成就 → t806 传送门 → t807 投掷物**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。皮肤清晰度项已按用户指示**关闭**（等高清源图）。

---

## R19.13 用户实测复盘批二（t808-t840，33 项；2026-08-23 立项）

**立项背景**：R19.12 收官后用户新一轮实测报障。三项「矩阵全 PASS 但用户实机不通」的争议项（红石→TNT/发射器、铁砧附魔丢失、书架文字）统一以 **t813 构建版本戳** 打头——主菜单/F3 显示构建时间+commit 短哈希，用户复测前先核对版本，终结「陈旧 exe 六连疑」（t740/t744/t792/t772/t805/本轮）。

### 🅰 矿车与铁轨（t808-t812）
**t808** 矿车 3D 皮肤四角 z-fighting：模型侧边四个角重叠，玩家运动时一闪一闪 → 重调 MinecartBox 四帮/底板盒的共面边（内缩 epsilon 或消除共面重叠盒），t768 几何复查。
**t809** 空车推送拐弯卡死：推矿车（非骑乘）到拐弯处推不动 → pushEmptyCart 路径的弯道重选/贴轨约束复核（t770 只修了被骑/滑行路径，推送路径可能漏同步——对齐 t770 的双符号持久化 + 中心线钉定）。 ✅✅ 已完成（**根因非 t770 漏同步**——stepCartAlongRail（双符号持久化+中心线钉定）本就被 tickPushedCarts 推送路径共享，真因在 pushEmptyCart 自身的**选向向量**：旧版把 wish 直接当选向向量 → 玩家长按 W 连推（视点/输入不随拐角转）时车过拐角后停在与 wish 垂直的臂上，两臂点积同为 0 平局 → kDirs 枚举序破平局（Px 先于 Nx、Pz 先于 Nz）→ 拐角出口朝枚举序败者（-X/-Z）时选中**指回拐角**的臂 → 车滑回拐角、到心重选（运动向）又把车送回来路 → 「推一下退一格」往返振荡 = 用户「推到拐弯处推不动」。**修法 = 三级合成选向向量**（身体推开语义，机制等价 MC 玩家撞静止矿车 → 车沿轨被推离玩家身体）：① away（车心−玩家脚底，水平归一，权重 1.0 主导）② wish（0.5 输入意图）③ dir（0.25 运动连续兜底，玩家贴车同位 away≈0 时仍沿原行进向）；权重比保证 away 主导。既有探针全兼容复核（P11(d)/P12(b)/t773/t735 下坡全是贴身追随或沿轨 wish → away 与 wish 同向同臂；复审#2 地板隔板守卫 pickTrackStep 列扫描先行拒、不受选向影响）。**矩阵探针 t809**（P12c 同款 L 形：南腿死端+直段+拐角[出口 -X=枚举序败者]+西臂 2；玩家贴身追随+wish 恒北不转）：修前 FAIL（cornerBacks 1 振荡签名）→ 修后 PASS（arrivedTick 312 直达西死端格心 ±0.05+贴中心线+停驻 100 tick 不被推走+cornerBacks 0）。**217 PASS / 0 FAIL**（基线 215+1）。**需人工目视**：铺 L 形轨（拐角朝任意四向）、长按 W 连推空车 → 车顺畅过拐角推到死端停稳不振荡；玩家站在车任意一侧贴身推 → 车沿轨被推离玩家一侧。
**t810** 骑乘过弯速度骤减：人坐矿车过弯速度骤降（动力轨接入也救不回），空车同轨却能匀速跑圈 → tickRiddenCart 弯道格速度衰减项排查（疑弯道重选帧有额外减速/摩擦乘子，或过弯帧 travel 截断）；**验收：满动力轨环形轨道骑乘全程接近匀速、无肉眼可见过弯掉速**。 ✅✅ 已完成（**根因不在弯道格专属衰减项**——是动力段 boost 旧版按 **proj 幅度**（wish·dir 投影）改写目标速：proj>0 → proj×12.8（视线偏 30° 就打折）、proj≈0 → 弹射档 2.8 接管 → 过弯后玩家视点未跟上新行进向的窗口（wish⊥dir → proj≈0）把 boost 12.8 以 ~3 格/s²拉垮到爬行速，下一拐角再砍一刀 = 「两条动力轨喂入也救不回」；空车路径（tickPushedCarts t735 ④）按**运动符号**全额 boost 无此症——用户「同轨空车匀速跑圈」的对照即定位。**修法 = 动力段直接改写 targetV 三分支**（机制等价 MC：动力轨供能看车速方向不看玩家视角）：(a) 无输入且车在动 → 沿 speed 符号全额 boost（对齐空车路径）；(b) 前进输入 → 全 boost 档不按 proj 打折；(c) 反踩刹车 → 不改写（玩家减速意图优先，动力不与刹车角力）；(d) 无输入停驻 → 弹射档 0.35×8（t658 发射器语义保留）。坡道段在其上继续叠加（下坡 max 10 / 上坡 ×0.6）不变。**矩阵探针 t810**（5×5 满动力环：每边 3 动力轨+四角普通轨[动力轨不拐弯 t771]+环内 8 红石块直供 12 轨通电；相位制 wish 模型——A 段无输入[proj≡0 纯维持力断言]+B 段每 16 tick 重采车头向[视点滞后 ~0.26s，从 yaw 采样防小环位移采样的伪反向刹车]+C 段反踩刹车+D 段恢复；3×3 环弃用=拐角占比 50% 病态几何非用户场景）：修前崩（minA 0.6/meanA 2.0/minB 2.5/meanB 8.9/8 圈）→ 修后过（minA 7.2/meanA 10.4/minB 7.9/meanB 11.0/17 圈+刹车压速生效+恢复到 12.0；残差小谷=拐角普通轨摩擦 1 格固有，用户大环上拐角占比低+直段恢复快）。**217 PASS / 0 FAIL**（基线 215+1，与 t809 同轮）。**需人工目视**：满动力轨环形轨道骑乘多圈 → 全程接近匀速、过弯无明显掉速（直段长的大环效果最佳）；动力轨上停驻车无输入被弹射向前；按 S 反踩有效减速刹车不与动力角力。
**t811** 生物自动乘坐矿车/船：矿车附近生物自动上车（矿车限 1 只、船限 2 只）；上车后生物**固定不动**（AI 冻结、姿态锁定），只有矿车/船被挖掉才下来（载具破坏 → 生物释放原地）。船保持玩家骑乘优先（玩家 + 1 生物 / 或 2 生物满员玩家不能再上——按现有玩家骑乘位次序裁决）。 ✅✅ 已完成（**机制非 MC 原版，按用户规格原创**：被动接触自动登乘。**架构 = Entities 层内跨管理器单向链 + 双向对账**：Entity 加 rideCart/rideBoat/rideBoatSeat 字段，Cart.mobPassenger / Boat.mobPassenger[2] 反向链；EntityManager 持 MinecartManager*/BoatManager*（setVehicleManagers 注入，setPlayerSight 先例；载具管理器不反指 → 无环）。**新收口 pass = EntityManager::tickVehicleRiding**（PlayerController 两处调：mob 桶 tick 后常开一次——暂停期船漂移仍钉 [世界模拟连续]；step() 后 profPhys 内再补一次——乘员同帧随车不滞后一帧）：Pass A 载具侧座位对账（mob 死/释放/槽复用/链断 → 清座防幽灵占座拒载；死亡动画期即让座可再接客）；Pass B 骑乘钉位/自释放（矿车 pin = 车心+(0,−0.3125+halfH,0) 与玩家脚踩车底板同基准；船 pin = 船心 ± 右向 0.3 双座 + halfH，right=(cosθ,0,−sinθ)；链断[车被挖/clearAll/槽复用] → 自释放原地+resting 清 [重力落定重接手] = **下车唯一路径 = 载具被破坏**）；Pass C 登乘扫描（非骑乘 mob 最近可乘载具：矿车 XZ≤0.8 / 船≤1.0、垂直≤1.5；矿车 1 座=玩家 XOR 生物、船总乘员 2=玩家+生物座；登乘清 moveSpeed/vx,vz,vy/jumpG = 姿态锁定）。**AI 冻结 = tick Mob 分支 dead 守卫后加骑乘守卫**（continue 跳过 AI/重力/resting/击退/jumpG/流推/火/仙人掌/窒息；仅保 hurtFlash 衰减；掉血/死亡照常走 damageEntity 外部路径 → dead 分支 pos 冻结在载具处 = 尸体/掉落原地释放；resolvePlayerPush 同跳过防玩家推挤出斗）。**满员裁决（报告口径）**：矿车乘员总数 1——玩家骑乘时登乘扫描跳过（mob 不上），mob 占座时玩家 tryMount 拒载（返 false 不改态）；船乘员总数 2——玩家+1 mob 照常、2 mob 满员拒玩家 tryMount（t508 换船同守卫：瞄满员船换船被拒留在原船）。**矩阵探针 t811×2**（车：直轨 6 格+3 宽石板，驱动序镜像 PlayerController 真序[桶 tick→钉①→pushEmptyCart+tickPushedCarts→钉②]——登乘/推动期每 tick 钉位误差<0.01 含 Y 座位公式/停驻 100 tick 零漂/hitCartFromRay 挖车释放+落定重接手[轨格 isSolid=非air实存 → 落定贴轨 cell 顶，引擎既有公式]/第二 mob 满员不登/玩家 tryMount 满员拒/玩家骑乘车不接 mob；船：t805 式凿石水道——双座登乘横向分离 0.6+pinY=船位+halfH/第三 mob 满员拒/玩家 tryMount 满员拒/挖船双释放原地）。**219 PASS / 0 FAIL**（基线 217+2）。**需人工目视**：① 生物走近静止矿车 → 自动上车坐进车斗（腿静止不摆动）；玩家推空车（载着生物）→ 生物随车平滑移动不滞后不漂移；② 挖掉载着生物的矿车/船 → 生物原地恢复走动 AI（矿车上的会落到轨面上站立）；③ 两只生物同乘一船并肩坐 + 第三只不再上 + 玩家右键满员船上不去；④ 玩家骑乘中的矿车旁生物不再自动上车；⑤ 骑乘中被箭射死 → 尸体与掉落物出现在载具处。
**t812** 铁轨四向连接优先级 + 红石变道：普通铁轨周围 3+ 轨连接时不再「一坨不知道咋走」——确立优先连接（MC 语义：直线优先于弯、连接关系一经确定且不变道条件则稳定）；**红石信号可变道**：3 向交汇的普通轨受压力板/按钮/拉杆/红石块控制切换弯道方向（参考 MC 1.0 rail junction 语义；激活前连接稳定不闪变）。railConnections 表 + mesher 象限 + cart 转向三消费端同源改（t771 架构）。 ✅✅ 已完成（**交汇形态收口在 Core 单一权威 railConnections 新规则⑤（3+ 臂拦截）**：① 四向全连 → 直线一对（轴偏好级联：既有贯穿轴 → 既有单端轴 → bit5；全新 state=0 → NS；输出确定 = 重算恒同值不闪变）——旧 t565 十字 4 位输出退役，mesher tile 137 十字分支仅剩**旧存档陈旧 state** 防御性渲染（任意邻编辑复检即落新形态）；② 三向 T 交叉（必为一对贯穿 + 单端岔尖）＝**转辙器**：输出弯道 2 位（岔尖 + 贯穿轴一侧），**稳定优先**（既有 con 已是本布局合法弯〔含岔尖 + 恰一贯穿端〕→ 原样保持 =「激活前连接稳定不闪变」/ 切弯后不被无关邻编辑翻回），否则按 bit6 选侧。**MC 1.0 变道语义对照**：MC 转辙器（现代 wiki 口径）= 未通电保持既有弯向、通电切到另一条弯、断电不回弹；MC 1.0 原版代码实为「按电力态无记忆重解」（通电→固定弯 / 断电→直通）——本工程按 dev-spec 点名取**保持 + 沿记忆**语义：升沿切弯、降沿保持，bit7 通电记忆防「稳定通电期间每次电力复算触达误判新升沿反复切弯振荡」。**state 位分配（普通轨 8 位全占用规划）**：bit0-3 连接位（既有）、bit4 动力/探测通电位（既有）、bit5 轴偏好（既有）、**bit6 RailSwitchCurveFlag=0x40 弯向记忆**（0=贯穿轴正端 +X/+Z、1=负端，**布局相对语义**）、**bit7 RailSwitchPoweredFlag=0x80 通电记忆**（升沿检测跨 tick 记忆；两者存档 round-trip 保真 + recomputeRailConnections 守恒写回；动力/探测轨恒 0）。**三消费端同源（t771 架构，全读同一 state 连接位）**：mesher 象限（railCornerArms → tile 136 拐角）、矿车 pickTrackStep（通用点积环：岔尖进车 dot=0 转入弯侧、贯穿对侧直行穿过）、电力层（tickRedstone 接收器分支：升沿 railSwitchToggledState〔Core 纯函数，与规则⑤**逐字同源**的布局分解：贯穿对+岔尖+正/负端〕切弯 = bit6 翻转 + 连接位重写，降沿只清 bit7 不回弹；写走 m_chunks.setBlock 标脏 + 末尾 worldChanged → mesh 即时重建）。**电力接入**：Rail 入 isPowerFamilyBlock + addReceiver 接收器集（动力轨先例）；isReceivingPower 6 正交邻一处接入 → 红石块/火把/拉杆/按钮/压力板/粉全源覆盖。**矩阵探针 t812×3**：(a) 四向全连 con=12（NS 直线对）+ 邻块编辑复检恒 12（不闪变）+ 非拐角形态；(b) T 交叉：默认弯 con=5（Px|Pz）+ tile136 拐角贴图同源 + 激活前稳定 + 拉杆/红石块/压力板三源升沿切弯（con 5↔9）+ 降沿保持 + bit6/bit7 断言；(c) 矿车三趟：默认弯岔尖进车出 +Z 死端 → 通电切弯改出 -Z → 断电保持仍出 -Z（空车追推跑法同 P11(d)）。**224 PASS / 0 FAIL**（基线 221+3，三跑稳定；t771 七探针零回归）；exe 冒烟 10s 零 QML 错误（root objects=1；两条 Particles3D 降级 WRN 为既有设计）。**需人工目视**：① 十字铺 4 轨 + 中心轨 → 中心显示直线（不再十字一坨）；② T 交叉默认弯道贴图朝向正确 + 扳拉杆弯道贴图即时切换；③ 断电后弯道保持不回弹；④ 矿车从岔尖推进按当前弯向驶出贯穿臂、扳源后改走另一侧；⑤ 压力板踩下切弯（矿车转辙器场景）。

### 🅱 红石激活复现与版本基建（t813-t815）
**t813** ⚠️ 全批最高优先：**构建版本戳**——主菜单角落 + F3 调试屏显示「构建日期时间 + git 短哈希」（CMake 生成 build_stamps.h 注入；每 Exe 更新即变）。解锁三项争议复现：用户复测前先核版本，旧版本报障直接分流。 ✅✅ 已完成（生成链三层：cmake/WriteBuildStamp.cmake（script mode）产 build/generated/build_stamps.h 两宏（VOXEL_BUILD_STAMP=YYYY-MM-DD HH:MM 构建时间 + VOXEL_BUILD_GIT=git rev-parse --short HEAD 短哈希，git 缺回退 "nogit"）——① configure 期 execute_process bootstrap（首编前头必在）② add_custom_target(build_stamps) 恒跑=每 build 刷新 ③ 脚本「内容变才覆写」保 mtime → 同分钟不重编、跨分钟/换 commit 也仅重编唯一 include 该头的 src/Core/buildinfo.cpp 单 TU（宏不外泄，其余 TU 只见 buildinfo.h 的 QString API，实证：改 main.cpp 注释重编仅 main.cpp+buildinfo.cpp 两 obj）；单一权威出口 BuildInfo（Core 叶子 QML 单例，FrameProfiler 同款 QML_SINGLETON；stamp/gitHash/full 三 CONSTANT 只读属性）；三处消费=主菜单右下角半透明小字（MainMenu.qml 补 import VoxelSandbox）+ F3 首行下 build 行（buildF3Text）+ 启动日志 build 行（main.cpp，用户发日志即可判新旧 exe）。生成头在 build/（已 gitignore）→ 不入库无需新 ignore 条目。验证：零警告；构建-运行链实测 stamp 随重编 05:01→05:03→05:04 逐次更新、哈希恒与 git rev-parse --short HEAD 一致（c9119c3）；exe 冒烟 root objects=1 + 日志 build 行在；矩阵探针 t813（stamp 16 字符正则 ^\d{4}-\d{2}-\d{2} \d{2}:\d{2}$ + git ^[0-9a-f]{7,10}$，redstone_matrix_test 直编 Core 源）——**211 PASS / 0 FAIL**（基线 210+1）。**需人工目视**：主菜单右下角半透明小字「构建时间 @ 哈希」清晰不抢眼；进世界按 F3 首行下 build 行显示同值。
**t814** 红石→TNT/发射器/投掷器实机复现批：用户实测「激活红石粉/红石块/红石火把都不能点燃 TNT、不能触发发射器/投掷器」，但矩阵 t772（42 组合）/t773（3 探针）全 PASS 且用户同时确认铁门/铁活板门能开——矛盾点先实证：搭 1:1 演示 rig（新 exe + 版本戳确认后用户按步骤复现）；若新 exe 复现失败则判定旧 exe；若真复现则定位矩阵 rig 与实机路径的差异（重点怀疑：QML 转发消费端在真实 UI 状态下的绑定活性 / finishLoad 重扫时机 / 粉 state 写入沿）。**验收：用户在新版本戳 exe 上按演示步骤逐源实测并口头确认结果**。✅✅（排查完成：静态审计全链〔World 电力层→信号→Main.qml 顶层 Connections 双转发（无 UI 态门控）→PlayerController 消费端→firePowerTnt/fireDispenserAtQml 守卫链→dispenseFromDispenser 库存分派〕无缺口；铁门 vs TNT 分叉实证=铁门是 World 内静默写 state 闭环、TNT/发射器走信号消费端，用户铁门能开证 World 层正常。矩阵盲区收口：真 PlayerController 消费方法此前从未被自动化执行（P15 只数 World 信号、t773 只镜像清块）→ 新增 P-t814 探针（C++ 直连镜像 Main.qml 双转发语义）5 场景全 PASS：拉杆→TNT 清块+引信实体格心坐标、红石块→发射器射 1 箭+库存 3→2、拉杆→投掷器弹 1 掉落物+库存 5→4、空库存通电无动作（t607 语义）、稳定通电/2s 冷却内复触不重发。矩阵 212 PASS/0 FAIL（基线 211+1）。顺带修复：rig 网格耗尽〔96 深度 124 位用罄后 setBlock 越界静默拒绝=假 FAIL 农场〕→ 世界深度 96→128（既有槽位坐标不变）+nextSlot 越界 qFatal。**结论=代码链完整，用户症状最可能是陈旧 exe（第 6 例）或空库存未装填（t607 设计语义）；复现文档 docs/test-reports/t814-redstone-repro-steps.md（版本戳核对+10 场景+日志判别）就绪，待用户新版本戳 exe 实测口头确认后关单**）。
**t815** 红石粉 pick-block 贴图旧：创造模式对地上红石粉右键复制后，手上红石粉贴图是旧版 → pick-block 路径的 item 图标源与现网图集不同步（icon 缓存代际 or 专用旧 PNG 路径），对齐当前红石粉瓦片。 ✅✅ 已完成（**根因 = 图标双源漂移**：pick-block 写入方块段 RedstoneDust=130，其 item 图标回退链落 qrc icon_redstone_dust.png——t660 的独立手绘 64×64 画稿，与 tools/build_redstone_dust.py 瓦片生成器**不同源**；瓦片族 t692 亮度渐变改版后手绘稿永不跟随 = 用户「贴图是旧版」（非缓存代际问题，是源头分叉）。**修法 = 换图标源**（t745 统一贴图原则收口）：atlasIconSpecForBlock 增 RedstoneDust flatSpec(def.topTile=瓦片 166) + isPackDerivedIconFamily 增 RedstoneDust（t800 Glass「语义外延」同款先例）→ pack 关态回退链 ② 从程序图集运行期 flat 重渲、pack 开态粉瓦片未映射 pack 亦落程序瓦片——两态恒与瓦片同源，瓦片再改版图标自动跟随；旧手绘 qrc 稿退役为渲染失败兜底；缓存族 icon4→icon5 一并换代（与 t838 共用一刀）。探针 t815/t838（URL 链路断言：dust 图标 file:/// 且不含 icon_redstone_dust——钉死回退链位置；像素内容实机目视）。**250 PASS / 0 FAIL**（基线 242+8）。**需人工目视**：创造中键 pick 地上红石粉 → 手上 / 背包图标与地面瓦片同款暗红线向（不再是旧粗线稿）；红石 tab 条目同步换新。

### 🅲 资源查看器与 3D 视觉（t816-t821）
**t816** 浏览器羊染脸纠偏 ✅✅：切换毛色把**头上脸也染色了**（应只染身体毛层）→ t789 浏览器 tint 应用到了含头部的整个 fleece Model；与 t777 的腿=skin 层同理，**脸/腿=skin 层不 tint，仅躯干毛层乘色**（游戏内 delegate 同步核查）。〔完成：ResourceBrowser 加 sheepFacePatched 脸罩（皮肤色 #d6b890 盖头前面，眼 overlay 罩显时恒显防「染色羊无眼」）；Main.qml 游戏内同步（sheepTintDyed/sheepFaceCoverTint + 脸罩入颈枢 Node 随吃草低头同转，自然非白毛色羊生效）〕
**t817** 夜行者黄线 ✅✅：3D 模型两个面交界处有黄色线 → t781 enderman 盒区 UV 接缝采样串色（疑 body/limb 区边缘 texel 泄漏），盒区间加半纹素 guard 或修 UV 边界。〔完成：PIL 实测定性——demo 包 enderman.png 全部透明纹素底色为纯黄 RGB(228,228,0)，UV 无重叠，黄线=面矩形边缘双线性外溢混入相邻透明填充纹素（头 Top 面是不透明孤岛/身体 Top 上缘毗邻空带）；修=mobmodel.cpp mobFaceQtUV 每面 UV 四边内缩半 base 纹素（≥半 HD 纹素，任意整数倍包不越界，图集 gutter 同款），全部 pack box-UV mob 三消费端同享〕
**t818** 燃烬者头缩小 ✅✅：t782 头 0.88³ 观感过大 → 缩到 ~0.5³ 量级（棒轨道半径同步收），目视平衡为准。〔完成：头 0.88³→0.7³（半 0.44→0.35）+ 棒轨道半径 0.62→0.52（棒内缘 0.47 与头半 0.35 间隙 0.12 不穿模），总高不变；图鉴 mobPreviewScale(17)=1.1 注释同步〕
**t819** 狼模型缝隙+贴图错位 ✅✅：腿与身体有缝隙、位置不对；身上部分和耳朵上是**狼头的贴图** → t780 box-UV 区映射错位（耳/身采样窗串到 head 区）+ 盒件间隙（嵌肩/髋 0.05 先例）。豹猫同查（同一 UV 表）。〔完成：PIL 复测身=mane 毛区无脸纹（t780 结论成立），「身上是狼头」实为头前伸 0.30 悬空——修=①耳采样窗与头区分区（狼耳采 mane / 豹猫耳采 body，旧复用头 texOffs 把 row6 双瞳+鼻吻整面贴上耳 =「耳朵是狼头」根因）②头后移贴胸（狼 -0.52→-0.42 / 豹猫 -0.46→-0.38，后缘深嵌胸口）③腿嵌髋 0.03→0.05 + legOffX 收进身体轮廓；两侧眼 overlay z 随移〕
**t820** 浏览器 3D 预览上下旋转反向 ✅✅：鼠标上下拖动方向与预期相反 → 俯仰角符号取反（左右已对，只动 pitch 轴）。〔完成：DragHandler userPitch -dy*0.6 → +dy*0.6（推球面直觉：上拖看底/下拖看顶），方块/床/生物三预览共用同一 userPitch 一并修正〕
**t821** 床头尾 z-fighting + 床色板对齐 ✅✅：床头/床尾羊毛与床身模板接触面重叠闪烁 → bedHalfBoxes 头/尾板与床垫盒共面边内缩；床上羊毛色与实际对应色羊毛**色值不一致** → 床瓦片生成色板与 build_wool.py / t751 woolPalette 同源对齐。〔完成：z-fight 根因=床垫/枕头长轴外端面与床板外面同格边共面同法线 → 外端内缩一个 boardThick 收到板内面（反向法线+背面剔除无竞争；板全高封口无缺口，内端仍满触格边保 t496 对接连续）；色差 PIL 实测 t387 首 8 色（red +10/+5/+5、orange −22/−25/0、yellow −20/−10/0、green −10/0/−5、cyan −10/−5/−5、magenta −15/−5/−15、black +8/+8/+6）→ build_bed.py 改 import build_wool.WOOL_COLORS 真同源 + 瓦片/图标/图集再生成（12 色纯被面采样点全对齐）；矩阵 t821 探针 128 rig 钉死盒几何契约〕

### 🅳 附魔（t822-t827）
**t822** 铁砧附魔丢失实机复现二：用户再报「附魔物品放入 UI 即消失附魔、取出变普通」——t792 实机探针 47/47 全过 → **必须新 exe + 版本戳确认后 1:1 复现**（同 t814 方法论）；真复现则查探针未覆盖的入口（疑：特定物品类别 / 特定附魔组合 / 存档读回后的槽恢复路径）。 ✅✅ 已完成（commit fc8ec84 + 复现文档 docs/test-reports/t822-anvil-repro-steps.md：**补测 t792 探针未覆盖的两段真链，全链判定完好**——t792 的 Hotbar 是 QML 桩、存档序列化完全未测，本轮两探针收口：① t822a **真 Hotbar VM 光标序列**：逐行镜像 AnvilUI slotLeft 拾取五连调（清源→setHeldBlock→Count→Durability→Enchants→Name）+ 主栏整格放回 + **setHeldBlock 同 id 早退边角**（光标持同 id 素品拾取附魔品，早退保旧字段后逐项覆盖，附魔必须落上）+ returnAnvilToHotbar addToAny（护甲/附魔书走新槽路径带 ench/name）——工具/护甲/附魔书 + 多附魔四类全过，关「QML 桩≠C++ 本体」嫌疑；② t822b **真 WorldStore SQLite round-trip**（t622 附魔序列化 8-16 落地以来首次自动化覆盖）：gatherPlayerState v3 精确形状（hotbar9+main27+armor4：铁镐 eff3+unb2 半耐久+实例名、附魔书锋利5+燃焰1、四条满配剑带名、附魔胸甲带名半耐久、护甲槽附魔件、空槽）→ savePlayerData → **关库重开** → loadPlayerData → 逐字段比对（显式 toInt——JSON 数字回读是 double，禁整 QVariant==）→ applyPlayerState 回灌新 VM 全槽比对；探针库走临时目录绝对路径（openWorld 绝对输入直通），saves/ 不受影响。静态复审并行：t792 后 AnvilUI.qml/InventoryOps.js 零改动；hotbar.cpp 后续 7 提交全是染料/蛋/调色板/骨粉/附魔台桥接；t798 效率附魔只动 toolregistry/playercontroller（HUD 紫晕直读 enchantsAt 显示链未触）；无自动保存（唯一存档入口 saveAndExitToWorldList 先关面板归还再 gather，无 UI 中间态竞态）；死亡掉落四段全带附魔。矩阵 **212→214 PASS/0 FAIL**（两探针均过）。用户症状分流三案：① 陈旧 exe（第 7 例嫌疑）② **旧存档数据（本轮新结论）**——附魔存档持久化是 t622（08-16）才有，之前的存档无 enchants 字段读回静默素品，用户可能把「读档后就没光晕」误记成「放铁砧丢的」→ 复现文档第 0 节强制「本次新附魔物品测」③ 真复现 → 按文档第 3 节分层判别（A 附魔台/B 放入槽/C 取出光标/D 存档/E 类别组合）回报带层号。**需人工按 docs/test-reports/t822-anvil-repro-steps.md 8 场景实测回报**（重点场景 1 放入取出主干 + 场景 5 存档读回）。
**t823** 书架文字浮现实机核查：用户没看到书架→附魔台文字流 → t797 的 EnchantGlyphFlow 在真实场景验证（放书架 ≤0.5s 起流、距离门 >16 格、拆架即停）；若新 exe 仍不见则查 active=playing 常驻门控在用户场景的失效条件。 ✅✅ 已完成（**静态全链审计五面无缺口 → 结论 = 链路完好，陈旧 exe 嫌疑第 8 例或搭法口径问题**：① 注入链——theWorld（Main.qml:1677 静态声明）+ enchantTablePositions 三重维护（blockPlaced/broken 增删 + 读档 collectBlocksOfId 重建 + onWorldChanged 孤儿清理）在 glyphFlowLoader.onLoaded 时均已存在，tableCount 绑 ListModel.count 反应式；② 时序——enterWorld 先重建表（:486-494）后切 appState="playing"（:601），onActiveChanged 重扫读到的必是已填充表（两序皆安全，pairs 非绑定而是三信号显式重扫无求值期问题）；③ 口径——QML rescanPairs 与 World::countBookshelvesAround **逐行同源**（同一 blockAt 访问器；Math.trunc(dx/2) ≡ C++ 整除向零，dx∈{-2,0,2} 商恰整数），用户重点怀疑「书架要放两层高才有效」**证否**：单层地面环带即满（t823 探针 16 对）；「能附魔高档而无字流」两处分叉路径不存在（同源同访问器）；④ 定时器——spawnTimer 绑 active&&pairs>0 无对零开销 / 池满静默丢 / 相机距离门 16 格（第三人称相机也在门内，站书架 5 格必过）；⑤ 粒子降级——两条 Particles3D WRN 属 Ambient/Weather 族，本组件 Model+Timer 池（t385/t390 教训）不受影响。**实机冒烟**（新 exe f42190f）三条 [t797] 日志齐全（ready pool=36 / Loader Ready / adopted）。**防线加固**：recipe.cpp t823 static_assert 钉 94/95 字面量（t789 QML literal contract 模式，方块段重排忘同步 QML → 编译失败）+ redstone_matrix_test t823 镜像 tripwire 探针（冻结镜像 rescanPairs 语义与 C++ 权威锁同值：净空 0/0 / **贴身环带 8 格全放双侧恒 0**=不出流也不加档的用户搭法口径钉 / 单层满环 16 对 vs 权威封顶 15=单层即满+视觉有意不封顶 / 堵两角半步 -2 / 单列两层叠放 2/2；权威规则改动而 QML 副本未跟 → FAIL 提醒同步 EnchantGlyphFlow.qml+EnchantRunes.qml 两副本）。**矩阵 251 PASS / 0 FAIL**（基线 250+1，三跑稳定）；零警告构建。**复现文档 docs/test-reports/t823-glyphflow-repro.md**（版本戳核对 + 五条设计语义防误判 + 环带搭法图示〔距离恰 2 格+半步留空+单层即够〕+ 最简 5 书架 rig + 日志判别 + A-D 分层回报表；附魔台 UI 档位生效 = 搭法自检入口，档位生效而无字流为最有信息量的回报）。**待用户在新版本戳 exe 上按文档实测口头确认后关单**（同 t814/t822 先例）。
**t824** 附魔池物品过滤：镐子附上**亡灵杀手**（对镐无意义）→ 附魔台三档选项池须按物品类别过滤（镐/铲/斧→效率/耐久/时运/精准；剑→锋利/亡灵/节肢/击退/燃焰；弓/甲各按表）——对齐 t763/t798 附魔适用表收口单一权威。 ✅✅ 已完成（commit 见 git log：**根因 = 选项池按大类 mask 门过滤**——亡灵杀手 appliesToMask=Weapon|Tool|BookItem 含 Tool 位 → 镐/铲全过门；摔落保护 mask=Armor → 胸甲也过门；锄 mask=Tool → 可出效率。修法**收口单一权威**：EnchantRegistry::selectEnchants(category,..) 替换为 **selectEnchantsForItem(itemId,..)**，候选池改 isApplicableForItem 逐条精判（t615 铁砧逐物品权威同表）——镐/铲→{效率,精准,时运,耐久}、斧→+锐锋族（无时运）、剑→锐锋族+击退+燃焰+耐久、胸甲→保护族+耐久（无摔落/水上）、靴+摔落保护、头盔+水上亲和、书=全池；**锄 categoryForItem 判 None**（MC 1.0 无适用附魔）→ 三重门连收：附魔台槽 0 拒入 / isApplicableForItem mask 拒（铁砧书合并）/ 选择器空池。旧 selectEnchants 删除（唯一非 UI 调用点 loottable 改传 BookId 同池）；Hotbar 桥接 selectEnchantsPreviewForItem 替换旧 selectEnchantsPreview + enchantSelected 同源复算；doEnchant 预览=写入同入口。矩阵探针 t824（全 seed 扫池 1200 抽/物逐物品断言 ⊆ 允许集 ∧ 允许集全出现 + 桥接==直调 + enchantSelected 拒锄/拒已附魔））
**t825** 锋利最终伤害显示：钻石剑附锋利后伤害仍显示 +7 → tooltip/伤害显示须算**基础+附魔加成**的最终值（显示与实际伤害公式同源）。 ✅✅ 已完成（commit 见 git log：**静态复核 = 实战链与九处 tooltip 均已算附魔加成**（attackMob t476/t763 锐锋 +0.5×级暴击前叠 base；HUD/背包/铁砧/工作台/熔炉/箱子/发射器/附魔台八面板 + Main.qml hover 九处 tooltip 全含锐锋项），用户症状疑为旧 exe 观感或对「+7」行不含加成的误读——但**公式副本散布十处确属漂移温床**。修法**收口单一权威**：新增 EnchantRegistry::weaponAttackDamage(itemId, enchants[4])（基础 ToolRegistry::attackDamage + 锐锋 ×0.5/级）—— attackMob 以此为起点（再叠亡灵/节肢对族 → 暴击 ×1.5 → 下限 1，语义零变）；Hotbar::displayAttackDamage(itemId, QVariantList) Q_INVOKABLE 桥接取整 round(同函数) → 九处 QML 公式副本全删改走桥接（selectedItemEnchants 快照 accessor 新增供 attackMob 取 4 槽 packed）。矩阵探针 t825（权威精确值 7/7.5/8.5 + 木剑 V 6.5 + 显示桥接 .5 半上取整与缺项降级 + 级 0..5 逐枚 display==round(权威)）。「显示 = 实战的目标无关部分」（对族/暴击实战侧叠加不预告）结构化成立——改系数只改一处，战斗与全部 UI 同步变。）
**t826** 击退实战无效：附击退打生物无击退 → 近战命中链补 knockback 强度按附魔等级递增（对齐 t774 爆炸击退先例的 EntityManager::knockback 路径）。 ✅✅ 已完成（commit 见 git log：**静态复核 = 击退链一直在**（attackMob t476 起即调 EntityManager::knockback，t774 同路径），真缺的是**强度量级**——旧 1+0.5×级：II 仅 ~2.3 格总位移（基线 kKnockbackHoriz/kKnockbackDrag ≈ 1.1 格），与 AI 游荡抖动同量级 → 用户「附了没感觉」。修法收口 **EnchantRegistry::knockbackStrength(level) 单一权威 = 1+3.0×级**（I 4.0 / II 7.0 → 总位移 ~4.5/~7.9 格，MC 1.0「I 明显推离 / II 飞出数格」量级；负级防御钳 0），attackMob 旧本地式删除。矩阵探针 t826（公式面 1.0/4.0/7.0 + 真 EntityManager 三猪短窗 0.128s 位移严格按级递增 3.24/1.74/0.46 格落理论带；两坑记录：spawn 瞬时悬空未先 tick 落定 → mobAabbHitsSolid 撤回水平位移假 FAIL（补 60 tick settle 窗）；短窗位移模型 = v0·dt·(1-0.936^8)/0.064 ≈ 0.10×v0 非 40% 全窗值）。需人工目视：附击退 II 打猪推离 ~4-5 格观感。）
**t827** 附魔全效果逐项审计（用户原话「镀膜效果还是需要一项一项的检查」）：12 项附魔效果逐项实测表（锋利/亡灵/节肢/击退/燃焰/效率/耐久/时运/精准/保护族/水下亲和/附魔书路径）——每项「实测方法 + 预期 + 结果」矩阵化，缺的补实现（重点疑：燃焰点燃、耐久跳过、保护减伤、时运掉落加成是否真接通）。 ✅✅ 已完成（commit 见 git log + 审计表 **docs/test-reports/t827-enchant-audit.md**：12 效果面 × 「生效点代码位置 + 验证方式 + 预期 + 结果」全表。**结论：重点疑四项均已在**——燃焰点燃（本轮 t827 探针首次自动化运行时覆盖：ignite→isBurningAt 即显、1HP/s 结算窗 ≥5/10 计量猪实际扣血（kFireExtinguishChance=0.15 下界防偶发）、1HP 猪 burned=true 走熟肉掉落链）、耐久跳过（t763③ 统计探针 400 击 ∈[260,340]）、保护减伤（t763② EPF 六路路由断言）、时运掉落加成（finishMiningAt 接线 + 七矿 isFortuneOre 表核对）。真缺的即 t824/t825/t826 三项（见各条）。锄判不可附魔为口径修正（MC 1.0 无适用附魔）。矩阵 **230 PASS / 0 FAIL**（基线 226 + t824/t825/t826/t827 四探针）；需人工目视：击退 II 推离观感、燃焰打怪着火视觉、附魔台放镐三档不再出亡灵杀手。）

### 🅴 生物行为与生态（t828-t834）
**t828** 水下窒息与浮力：生物水下有呼吸时间，太久不浮上来掉血（窒息 1HP/s，玩家 t160 同节奏）；水生生物（鱿鱼）默认上浮不沉底（AI 浮力项）。 ✅✅ 已完成（commit 见 git log：Entity 加 mobAirTimer/mobDrownTimer（DMI）+ 常量 kMobBreathSeconds=15s（同玩家满气）/kMobDrownInterval=1s；tick 火烧同区节流块内以累积 aiDt 推进——头位格 floor(pos.y+halfH·0.8) 浸水累积 15s 后每秒 damageEntity(1)，头出水双清零；鱿鱼豁免。浮力 = 物理重力分流（鱿鱼水中 kWaterGravity 缓沉 → 反转 kSquidBuoyancy=3.0 净上涌 + kSquidRiseMax=1.6 钳制 → 水面下 bobbing 悬停）+ resting 复探对「水中鱿鱼」破 resting（水底固体支撑复探恒真会 continue 掉浮力分支——无此打破则浮力死代码）。矩阵探针 t828（pit 水柜 + 同构干 pit 对照：溺水猪 20s 扣至死/≤1、干 pit 对照满血、鱿鱼浮离池底 y≥86.2 且满血豁免）。**需人工目视**：生物溺水红闪节奏 / 鱿鱼悬浮 bobbing 观感。）
**t829** 末影人三修：① 箭射末影人**有时不瞬移直接穿过** → 箭命中检测先于穿透判定（命中即瞬移闪避，race 修复）；② 贴图头部-身体连接处（下巴）**大块透明** → t781 Mask 裁剪过狠/UV 边界串区；③ 末影人**碰水应扣血+瞬移**（当前水中平安无事）。 ✅✅ 三修全落地（commit 见 git log）：① 箭命中 race = 旧分支 nightwalkerDodge 内 teleportCooldown 守卫 + dodge 后箭不 remove（继续飞 + 冷却内零反应穿身两面）→ 改箭分支直调 teleportEntity **绕过冷却强制瞬移 + 箭一并 remove**（瞬移失败退化普通命中扣血——命中必有结算）；近战 30% 掷骰路径不变。② PIL 实测包 enderman.png 头前脸底部 2 行 + 头底面全透明（透明纹素底色黄）→ Mask 裁出「下巴大块透明」；修 = Core 运行期合成 generateNightwalkerChinFile（t749 羊合成同族：头盒六面区内透明纹素**列向延拓**填充为该列上方最近不透明色，眼/区外像素不动；落盘缓存 + revision cache-bust + 旧世代清理；mobTextureSource(16) 懒合成接入，失败回退原样）。③ **真根因**：t727 旧「连续累计满 1s 才扣血」在瞬移先发（0.6s 冷却每 aiTick 试跳）节奏下恒凑不满 1s = 水伤死代码（「水中平安无事」）→ 改 MC 语义「碰水即伤 + 受伤瞬移逃离」：首触 tick 扣 1HP，其后每持续满 kNightwalkerWaterDamageTick 再扣；离水重置。矩阵探针 t829（(a) 首触掉血 + 瞬移逃离（全程最大 XZ 位移断言——瞬移方向随机净位移 flaky）；(b) 箭命中强制瞬移 + 零 Arrow 残留 + mob 无箭伤；(c) 下巴合成器密闭 rig：头区全不透明 + 列向延拓色 + 眼行保留 + 区外不动 + 坏输入返空）。**需人工目视**：包开夜行者下巴闭合无黄斑/无洞；箭射夜行者必瞬移观感。）
**t830** 燃烬者移除主世界刷新：当前只有主世界，燃烬者不该刷（下界更新后再恢复）→ worldgen/自然刷新表摘除（蛋/笼手动刷保留）。 ✅✅ 已完成（commit 见 git log：tickHostileLife 黑暗刷怪池 6 份 → 5 份（Shambler 2/5、Bones/Stalker/Nightwalker 各 1/5，Emberling 摘除——主世界无下界语义）；蛋 / 刷怪笼改型（spawnHostileMob / 被动笼路由）不经本表照常可刷（t787 探针已锁笼路径含 Emberling 不受影响）。矩阵探针 t830：**专用局部世界 height 96**（共享 w 高 48 时 heightAt ~57-71 越 surface 界 → surface 尝试恒败只剩稀有洞穴——首跑 60 周期仅 3 刷的探针环境陷阱）+ skyBrightness=0 全域黑暗 + 周期清场防区域 cap 饱和 → 40+ 自然刷新采样零 Emberling（旧表 P(40 零)≈4.6e-4 必被逮）+ ≥3 型多样防反向回归。）
**t831** 狼/豹猫驯服全链（MC 语义）：豹猫用**鱼**、狼用**骨头**驯服（手持右键投喂，概率成功）；驯服动画冒爱心；驯服后脖子有**项链**、跟随玩家；玩家空手右键 → 坐下（不再跟随）/再右键站起；手持食物右键已驯服个体 → 喂食回血；**血量与尾巴翘起高度挂钩**（低血垂尾）；跟随态玩家离太远 → **瞬移到玩家脚下**。 ✅✅ 全链补齐（commit 见 git log；t480/t481 已落地驯服/跟随/防御/坐站/尾高/远距瞬移——本任务补四缺口）：① **驯服爱心** = Entity.tameHeartTimer（tameWolf/tameOcelot 成功置 kTameHeartDuration=4s）+ inLoveAt 扩「loveTimer **或** tameHeartTimer」（与求偶分离——不触发寻偶 AI/繁殖配对，仅呈现）+ tickBreeding 衰减段统一递减；② **项链** = 狼 delegate 颈根红环带 Model（wolfTamedAt 门控，驯服瞬间即现；昼夜灰阶 × 受击红闪）；③ **空手坐/站** = playercontroller 空手分支补狼（原只有猫——t480 落地时狼的坐站挂在骨头分支）；④ **喂食回血** = EntityManager::healTamedPet（受伤驯服个体回 amount 钳上限；满血/未驯服返 false）+ 肉/生鱼分支改三分流（幼崽 feedBaby → **受伤 healTamedPet(4) 优先** → 满血 enterLoveMode，机制等价 MC 受伤宠物吃食回血优先于繁殖）。矩阵探针 t831（驯服→爱心显→4s 收心、受伤回血钳上限 + 满血/野狼拒、坐态冻结零位移 ↔ 站态跟随 ≥2 格、>24 格瞬移到玩家 ≤10 格、豹猫驯服爱心；Game 层 useBlock 接线静态审）。**需人工目视**：驯服瞬间爱心 + 项链观感、空手右键坐站、受伤狼喂肉回血尾巴翘起变化。
**t832** 染料右键染羊：手持染料对羊右键 → 羊染成对应色（一次性：剪毛得该色，**长回后恢复自然原色**——t789 sheepWool 重掷自然权重）。 ✅✅ 已完成（commit 见 git log：EntityManager::dyeSheep（sheepWool=染下标 + sheepWoolDyed 标记 + sheared=false 染即长毛 + bump；非羊/越界拒）+ playercontroller 染料段分支（DyeIdBase..DyeBlackId → findMobHit 独立 mob 射线 → woolIdx = id−DyeIdBase 同序换算 + 生存扣 1 染料）+ **长回重掷** = 吃草长毛分支据 sheepWoolDyed 走 rollNaturalSheepWool()（从 spawnMobCore 抽出的自然权重单一权威共用）恢复自然色并清标记（未染羊长回保持原色不重掷——自然羊剪后长回同色）；剪毛载荷（sheepSheared 携 sheepWoolAt）链既有 t789/t834 已通——染色羊剪毛得染色自动成立。矩阵探针 t832（染紫→sheepWoolAt=10→剪毛载荷=10→草平台 12s 长回 ∈ 自然色集 {0,6,7,8,12,15} 且绝不再现 10（确定性断言：10 ∉ 自然集）+ 非羊拒染）。**需人工目视**：染料右键羊毛层 tint 即时变色、剪毛掉染色羊毛、长回自然色观感。）
**t833** 刷怪笼两查：① 生物蛋改型后**迷你模型颜色消失**（t787 后笼内颜色丢了）；② 刷怪功能实机核查（放入蛋后的笼是否真的定时刷该生物——玩家近+黑暗条件明示给用户，若真缺失则补）。 ✅✅ 两查收口（commit 见 git log）：① 静态审计 cleanupVis 重读链（worldChanged→重读 state→重赋 cageMobType→geometry/贴图/摆位/眼表六绑定链）与 13 型色表（miniProgTex 11 型 + Bones/Stalker/Spider 纯色分流）**均完备无静默缺口**——但原位改 cageMobType 依赖整条绑定链即时重算，任一环陈旧（MobModel 材质 / 眼层 Repeater / 静态子树绑定族 t498/t177 教训）即呈现「颜色丢失」症状；修 = cleanupVis 改型改走**销毁重建**（destroy + addSpawnerVis 按当前栅格真值整体重建——torch/fire/portal host 世界同步视觉统一模式，全新子树无中间态、对任一陈旧通道免疫；改型罕见事件开销可忽略）。② 实机核查 = tickSpawners 被动路由**已在**（t787 spawnPassiveMob + review-f #31 被动笼闸门修正）+ 玩家激活半径 16 / 周期 6s / 同型 cap 4 语义齐——非缺失；矩阵探针 t833 补锁（猪笼出猪血 10=被动极性（敌对路由 20）+ 鱿鱼笼走水格谓词在 2 深水柱内出水生）。**需人工目视**：手持各蛋右键笼 → 迷你模型即时换型换色（重建修后观感）、被动笼定时刷怪节律。）
**t834** 剪羊毛掉落 3D 可放置：剪下的羊毛是 2D 掉落物、颜色不对、**不能放置** → 掉落链统一走**方块段羊毛**（白→Wool=27 方块、有色→63..77，全 3D BlockCube 掉落物+可放置回）；t789 的 0x20E 材料段映射退役，bed_red 简化配方原料改接 27（isWool 谓词已覆盖），配方矩阵同步。 ✅✅ 已由 Review 2026-08-23 修复批（#7+#8+#32+#33 组合=本任务，commit c9dd880）提前实现：① **掉落链统一方块段**——Main.qml sheepWoolDropId 白→Wool 方块 27 / 有色→63..77 不变（全 16 色方块 id 即物品 id，可放置回 + 3D BlockCube 掉落实体）；剪毛 onSheepSheared / 杀羊 onMobDied 羊分支共用该映射；review #33 顺带补 Math.max/min 钳 [0,15]（对齐 C++ tint 侧 clamp，脏值 16+ 会掉进床段 id）。② **0x20E 退役**——常量+Hotbar 名「羊毛」+图标+pack 映射保留（旧存档物品兼容，常量永不改号），但无任何掉落源/配方消费；recipe.cpp static_assert 改钉「保留 0x20E 仅兼容勿再接线」；bed_red 简化配方原料 0x20E→27。③ **顺带 review #8 红石灯生存链**——配方中心 0x204 玻璃物品→Glass 方块 54（0x204 已退出创造调色板=纯创造死链）；Glass 方块 dropId 改**自掉**（Wool/床族先例）→ 生存链闭环 = 烧沙得 0x204（放置过境物品，t405 放置不变）→放置→破坏回收 Glass 方块→入配方；只改配方侧不加 matcher 等价映射。④ **review #32 mobDied 第 8 参 sheared**——致死瞬间快照 e.deathSheared（同 deathBaby/deathBurned 模式，0.5s 死亡动画窗内剪毛竞态安全）；Main.qml onMobDied 同步 8 参：烧死仍掉熟羊肉（肉非毛不受剪毛影响）、未剪才掉羊毛（机制等价 MC 1.0 sheared sheep 无羊毛掉落）；旧 7 参连接兼容（新式信号槽允许槽参数少于信号）。矩阵探针：review-e 新段（QML 字面量 27/63+idx-1 ↔ BlockRegistry ↔ 16 染料配方产出逐位相等 + bed_red 方块版可合/0x20E 版不再合 + 红石灯 Glass 方块版可合/0x204 版不再合 + Glass/Wool 自掉 + 烧沙仍产 0x204 + mobDied sheared true/false 对照 + mobhead/woolface 缓存 _r<rev> 文件名+逐版清理）+ t802 云杉床探针随原料改 Wool 方块 + t789 死亡探针改挑未剪样本（其③剪过 3 只，#32 前载荷不可分辨）——**202 PASS / 0 FAIL**（基线 201+1）。**需人工目视**：剪白羊掉 Wool 方块可放置回、有色羊掉对应色、剪过的羊打死不掉羊毛（烧死仍掉熟羊肉）、红床/红石灯生存合成全链、0x20E 旧存档物品名/图标不回归。

### 🅵 投掷物与钓鱼（t835-t836）
**t835** 末影珍珠传送修复增强：① 落在**铁轨上/墙上不传送**（碰撞判定漏非整方块面）→ 任何接触（含岩浆）必传送；② 落水/岩浆 → **缓慢沉到液体底再传送**；③ 一路不碰墙地直落虚空 → 不传送；④ 抛距加长；⑤ **疾跑抛更远**（初速乘疾跑系数）。 ✅✅ 已完成（commit f28ff44。**根因双面**：Entities 半面 = t762 引入的 collisionAABBsAt 点在盒内判定漏两族——铁轨/火把等 ShapeNone **无碰撞盒**（珠点穿格不中）+ 压力板等**薄碰撞盒**（0.0625 厚 << 0.4 格/tick 步长，一 tick 跨过薄盒带点恒不在盒内）→ 珠穿透落进下方支撑格；Game 半面 = applyEnderPearlTeleport 立位扫描用 World::isSolid（语义=「非 air 实存」）把铁轨/水等无碰撞盒方块当实心脚位格 → 全列 abort =「落铁轨/水上不传送」。**修**：① 命中判据放宽为**本格任意方块实存**（blockAt 非空；豁免 Air / Water·Lava（②族）/ NetherPortal（t762 穿门保持）/ Fire（效果格，MC 1.0 投掷物 raytrace 同穿过））+ Game 侧支撑/脚/头复查改 collisionAABBsAt 有盒口径（无碰撞盒格可立入——轨/火把格穿模贴脚同 MC、水格立入走游泳链；薄盒（板/台阶/睡莲）照旧当支撑立其顶）；岩浆接触必传送但**传送不点燃**（MC 1.0 无珍珠传送着火，「5 格内着火」是 1.x 后期——注释钉死；传后立岩浆走既有岩浆接触伤害链）。② 液体缓沉：入水/岩浆改缓沉终速 kEnderPearlWaterSink=1.5 / kEnderPearlLavaSink=0.7 b/s（岩浆更粘更慢）+ 水平阻尼 5/s（~0.2s 停）+ 寿命倒计暂停（深柱缓沉可超 8s）→ 沉到液体底接触底面格才传送（落点=底面格，玩家立水格脚踩湖底）。③ 虚空/出界：y<0 / XZ 出界静默移除零传送（既有语义保持 + 新探针锁定）。④ 抛距：kPlayerPearlSpeed 12→24（MC 投掷物 1.5 b/t=30 量级）+ 珠专属轻重力 kEnderPearlGravity=12（MC 投掷物 0.03/t²=12 vs 世界 28）→ **平抛 6 格落差 ~24 格 / 45° 满抛 ~50 格**（旧 12+世界重力 28 仅 ~5 格）；雪球/蛋/眼 12 不动。⑤ 疾跑加成：掷出时 Sprint 态初速 ×1.3（对齐 t51 Sprint 移速 ×1.3 同一系数）。传送本体语义复核：落点=珠消散位、瞬移+清速+m_peakY 重置、Survival 自伤 5HP（EnderPearlTp，摔落保护可减）全保持；使用间隔=既有 m_lastPlaceMs 200ms 放置 CD 共用，未动。**探针 P-t835**（专用世界 96×40×96，y=83 地板带 + EntityManager 直调 + PlayerController applyEnderPearlTeleport 直调）：(a) 轨/火把/板三柱珠落**非整格自身格** + 玩家立格内 y=84 / 板顶 y=85 + Survival 自伤恰一次 (5, EnderPearlTp)；(b) 水稳态带 1.5 b/s / 岩浆 0.7 b/s（水>岩浆）+ 沉底落点=底面格 + 玩家立水格；(c) 虚空柱 + XZ 飞出双路移除零传送；(d) 平抛 ~24 格 + 45° ~50 格带；(e) ×1.3 初速落距比 1.27..1.33（镜像常量 P18 模式锁 24/1.3/12）。矩阵 **241 PASS / 0 FAIL**（基线 240+1，三跑稳定）；exe 冒烟 13s 存活零 stderr。**需人工目视**：① 手持暗渊珠右键 → 抛物明显变远变平（平视 ~16+ 格、上抬 45° ~40-50 格）；② 珠落铁轨/火把/压力板上 → 玩家传送立其上（轨/火把格脚部微穿模、压力板立板顶）；③ 对水面/岩浆抛珠 → 珠入液缓沉（可见慢速下沉动画）到底才传送（水底传送后玩家在水中、岩浆底传送后走岩浆烫伤链）；④ 珠抛出世界边缘/虚空 → 无传送珠消失；⑤ 双击 W 疾跑中掷珠 → 明显比走路掷远 ~三成。
**t836** 钓鱼系统整改（**用户 8-25 指示：本版本做完，不再拖下版本**）：钓鱼线渲染（竿→浮漂可见）、任意位置右键甩竿（不再限定水里才出红点）、可钩住动物、水中上钩粒子/浮漂动静/上钩判定全套（先调研 MC 1.0 钓鱼机制再列实施子任务；鱼肉获取链随本项打通）。 ✅✅ 已完成（commit 0a1acb5。**架构选型 = EntityManager 承载浮标实体 + PlayerController 只发指令收信号结算语义**（暗渊珠 / 掉落物「实体在 Entities、语义在 Game」同款分层）：新 Kind `Bobber` 四态机——Flying（轻重力 12 抛物（t835 珠同源投掷物家族）+ **飞行段与 mob AABB（外扩 0.15）相交即钩定**（玩家非实体天然不可钩自己；已钩 mob 被新浮标命中 = 换绑，旧浮标脱钩下落；落地静止浮标不钩——MC 语义近似取舍））/ Water（浮定水面：XZ 收格心 + Y = 液面−0.125（液面按水 state 折算源=1.0/流=(8−s)/8，mesher renderTop 同口径）→ **确定性等待 hashVoxel(seed⊕0xF15C⊕甩竿序号) ∈ [5,30]s**（PLAN §2-K 禁随机源，t791 骨粉同模式；World::hashVoxel 升 public 保跨层单一哈希权威）→ 咬钩窗口 0.5s（emit bobberBit 水花）→ 窗过 emit bobberEscaped（小水花）+ 序号自增重掷）/ Ground（贴命中面静止，收 = 空收）/ Hooked（钉 mob 身上跟随，spawnSerial 双查防槽复用误绑）+ 出界/180s 寿命消散（Game 层 updateFishing 镜像检测 alive 失效自动收竿）。PlayerController::useFishingRod 收竿三分支：钩住生物 → pullMobToward（水平 6 b/s + 微上抛 2.8，**不伤害**）+ 生存耐久 −5（Hotbar::damageSelectedItem 增 times 参数）；咬钩窗内 → fishingPool 抽物（池不动）+ fishCaught 载荷改 float 位 + 朝玩家水平弹向 + 弹速 4.5 → QML 路由 spawnItemAt 定向弹出（获物飞向玩家，t608 发射器先例）+ 耐久 −1；否则空收无消耗。旧「水射线定点放置 / 本地 m_biteTimer 计时」退役；bobberPosition/hasBite 变实体每 tick 镜像（QML 浮标 delegate + 鱼线绑它）。**鱼线渲染**：竿尖→浮标细长盒（UnitCube 沿 Y 拉长 + **手写 axis-angle 四元数**把本地 +Y 旋到连线方向——零新 API 赌注，qml.exe 运行期实测 Qt.quaternion(scalar,x,y,z) 后落笔）+ 竿尖锚点 = 脚底+1.40+右手侧 0.36+视线前 0.18（第一/三人称共用世界系手部锚，常量+注释）；浮标升级红顶+白杆双件、咬钩下沉 0.15。**熟鱼链**：CookedFishId=0x25B（染料段上首个空闲号）——生鱼→熟鱼 **kSmelt+kSmeltXp 两表都接**（t788 教训）+ 食用 +4（**本工程口径 = 生鱼 +2 的两倍**；MC 原值 +6，注释钉死防误校对）+ MaterialIcon drawCookedFish（生鱼同构鱼形换暖棕烤色 + 三条烤纹）+ pack 映射 0x25B→cooked_cod.png + 创造 tab 排生鱼旁 + 名「熟鱼」+ **喂豹猫仍只认生鱼**（MC 1.0 口径注释钉）。foodHungerAmount 升 public（纯静态表，探针直调）。**探针 P-t836**（独立 48×48×96 seed 77 世界 + 真实 World tick + PlayerController/Hotbar 真消费端）：(a) 抛物落差带（轻重力 12 直证）+ 落水浮定精确断言（(5.5, 液面−0.125, 6.5) 逐位）+ 陆上静止冻结；(b) 等待公式两端恰可达（h=0→5.00s / h=2500→30.00s）+ 600 序号分布带 min<6/max>29 + **行为级 ±1 tick 按预计算等待值咬钩**（镜像盐 P18 双钉）；(c) 窗内收 = fishCaught 恰一次（池内 id + 浮标位 + 朝玩家弹向点积>0.9 + 弹速镜像 4.5）+ 耐久 64→63；窗过 = escaped 信号 + hasBite 翻 false + 重等第二咬可达 + 此后空收零新获物零耐久；(d) pc 真甩竿钩猪（按猪实时位姿算 yaw/pitch）→ 收竿拉拽（位移朝玩家 >0.03 + 耐久 −5 + 猪血量不变 + 零 fishCaught）；(e) 熟鱼两表 + 食用值 + 名 + pack/创造 tab/豹猫 gate 源码钉。矩阵 **252 PASS / 0 FAIL**（基线 251+1，三跑稳定）；exe 冒烟 14s 零 QML 错误。**需人工目视**：① 手持钓竿右键任意方向 → 浮标抛物飞出（弧线可见）落水浮定 / 落地停住 / 甩出世界边缘消散；② 竿尖→浮标的鱼线全程可见跟随（第一人称 / F5 两视角观感，锚点常量可再调）；③ 咬钩瞬间水花上溅 + 浮标下沉 → 0.5s 内收竿获物飞向玩家 + 耐久 −1；错过窗口小水花 + 重新等待；④ 对猪/牛甩竿 → 浮标钉在身上，收竿把生物拉向玩家（耐久 −5、生物不掉血）；⑤ 熔炉放入生鱼 → 熟鱼（+1 XP）食用 +4 饥饿、图标暖棕烤纹、创造 tab 生鱼旁「熟鱼」；⑥ 对豹猫持**熟鱼**右键无效（仍只认生鱼）。）

### 🅶 画作/玻璃/垃圾桶（t837-t839）
**t837** 画作背面与朝向：① 1×2 画挖非承重方块后**背面看不到画**（画应随支撑破坏整体掉落，不残留单面）；② 放置时**有时直接显示背面**（朝向计算取反/墙面判定漏方向）。 ✅✅ 已完成（**①根因 = 失撑钩子只挂玩家挖掘路径**：画支撑墙复检原仅 PlayerController::dropUnsupportedPaintingsAround（finishMiningAt 末尾一处）——玩家镐挖两侧墙格实作正常，但**系统拆墙路径全漏**（TNT 引燃清格 clearBlockSilent / 爆炸 destroySphereSilent / t843 可燃墙烧穿 / 岩浆焚毁 / 冰墙融化）→ 墙没了画残留单面悬空，BillboardQuad 单面 + 背面剔除 = 从破洞侧看「背面看不到画」。**修法 = 钩子下沉 World 层单一权威**（t806 removeNetherPortalAt + t725 review #27 同模式）：removePaintingAt 自 PlayerController 下沉 World（逻辑逐行同源，掉落改 blockDroppedAsItem→Main.qml spawnItem 同链）+ 新 checkPaintingSupportOnEdit 钩子（R1 口径 isCollidable∨isFullCube 复检墙格仍有效——置换为另一完整立方的墙体保留不误清）挂 setBlock×2 / setBlockSilent / setWaterSilent / clearBlockSilent / recheckAttachmentsAfterClear（爆炸 / 岩浆焚毁 / 坍落三系统路径共用单入口）+ m_inRemovePainting 重入守卫（余烬门同款）；PlayerController 两方法删除（单一权威防漂移）。**②根因 = 朝向取命中面法线而非玩家朝向**：斜角 / 掠射命中（瞄墙时射线先中墙角柱 / 突出棱的侧面）时命中面法线与玩家视线近乎垂直 → 画面贴到侧面 = 「放置时有时直接显示背面」。**修法 = face 改玩家水平朝向反方向推导**（dominant 轴取反；机制等价 MC 1.0 ItemHanging 用玩家 yaw 定 direction）——画面恒面向玩家来向，锚格 = 命中格 + 画面法线（其支撑墙恒 = 命中方块，cellOk 逐格复检照旧）。矩阵探针 t837×6（World rig 直编：(a) 破非锚格背后墙→整画 1×2 掉 1 件 / (b) 破锚格背后墙同掉 / (c) 墙置换完整立方画保留 / (d) 直调 removePaintingAt 整画清 / (e) clearBlockSilent 系统路径掉落 / (f) M1 邻画不误伤钉契约）。**250 PASS / 0 FAIL**（基线 242+8）。**需人工目视**：① 破 TNT 引燃挂画的 TNT 墙 / 点燃烧穿木墙 / 爆炸炸掉墙 → 画当场整张掉落画作 item 不残留；② 挖 1×2 画上下任一背后墙格 → 整画掉 1 件；③ 斜角瞄墙角放画 → 画面正对玩家不再贴侧面。
**t838** 玻璃 3D item + 材质增实（需求反转，覆盖 t800②）：① 创造背包玻璃改回 **3D 立方 item**（t800 改 2D 平贴是当时的错向，用户要 3D）；② 玻璃方块边缘**太透明 → 增实**（边框不透明度提高，程序态+pack 态瓦片同步）。 ✅✅ 已完成（**①修法 = 删 atlasIconSpecForBlock 的 Glass flatSpec case**（t800 当时的错向注一并改写）→ Glass 落 ③ ShapeFull 默认 dimetric 立方投影（t800 前观感回归）；**缓存族 icon4→icon5 换代**（t800 同款模式——AppLocalData 已落盘的 icon4_54_* 是 flat 平贴，不换名永久复用旧观感；dust 一并换代）。**②根因 = 边框对比度不足**：旧瓦片 1px 浅灰环（168,184,198 vs 底 214,226,234，delta 仅 ~46/255）经 glassOnly 段材质 opacity 0.45 均匀洗后与面心几乎不可辨 = 「边缘太透明」；**修法 = build_glass.py 边框改 2px 双色环**（外圈 1px 深棱 96,118,142 delta ~120 + 内圈 1px 过渡棱 152,170,188 delta ~60；材质半透是均匀乘子 → 对比度按比例保留，半透后棱线仍明显）+ default_glass.png / atlas.png 再生成（高光斜线收在框内不截断）；pack 态瓦片不适用 solidify（玻璃合法半透，包 glass.png 原样）。探针 t815/t838（玻璃图标 file:/// + icon5 家族名断言；flat→3D 画法切换的像素差异实机目视）+ t800 探针文案同步。**250 PASS / 0 FAIL**。**需人工目视**：① 创造背包 / 手持玻璃 → 3D 立方体图标（三面明暗投影，非平面贴图）；② 世界内玻璃块边缘棱线明显加深加宽（半透面心 + 实感边框）；掉落物 / 创造 tab 图标同步 3D。
**t839** 创造垃圾桶 shift 误清：右上角垃圾桶 shift+点击 → 清空了 1~9 整个快捷栏 → 只应销毁**当前鼠标持有/单件**（shift 语义收窄）。 ✅✅ 已完成（**语义收窄为两档**（Inventory.qml 销毁槽 TapHandler）：① 光标持有 → 清整组光标栈（t700 语义保留，setHeldBlock(0) 同步清 count/耐久/附魔/名）；② 光标空手点击 → 清**当前选中槽单格**（setStack(selectedSlot,0,0) 只动这一槽）。销毁面收窄后 shift 与普通左键同语义（修饰键不再改变作用范围）——无论何种手势都**绝不批量清快捷栏 1-9**。纯 QML 无 C++ 探针路径（t778/t783/t790 先例）零探针；矩阵 250 PASS / 0 FAIL 不回归；exe 冒烟 13s 零 QML 错误。**需人工目视**：① 创造背包拿一组到光标 → 点垃圾桶 → 只光标栈消失（快捷栏 1-9 全保留）；② 空手点垃圾桶 → 只有当前选中槽清空，其余 8 槽不动；③ shift+点击任意时候不再整排清栏。

### 🅷 成就 UI 纠偏（t840）
**t840** t790 纠偏（用户澄清原意）：① **进度大面板恢复居中显示**（t790 移右下角是理解偏差，回滚 anchors.centerIn + 恢复原压暗观感）；② 新建**右下角成就快捷悬浮栏**：像小地图一样可鼠标拖动，快速看到当前已解锁成就计数/最近解锁（右上角若已有同类悬浮件则迁移，无则新建）；与暂停菜单进度面板数据同源（progress VM）。 ✅✅ 已完成（Main.qml 单文件。**回滚点清单**：progressPanel anchors.right/bottom dock → anchors.centerIn 居中（760×560，内部坐标全面板局部零逻辑影响）；progressOverlay 透明吸收层 → 恢复 0.7 整屏压暗（与暂停 0.55 叠 ≈ 0.87 原 t790 前模态观感）；**t790 保留项**（用户口味二选一取保留）：面板半透明 rgba(0.059,0.078,0.102,0.85) + #3a444f 描边、树视口加深底内嵌框、progressOpen 时暂停菜单本体隐藏（居中面板与居中菜单同轴重叠的旧问题仍靠它解）。**悬浮栏 achQuickDock**：仅 playing 显；z=120（暂停叠层 100 之上——游玩态指针被捕获、拖动实际发生在暂停态，低于 100 会被暂停叠层全屏 MouseArea 挡死无法拖；背包 150 / 进度 155 之下）；默认右距 20 / 底距 150（3s toast 闪烁带 + hotbar 底栏上方）；MouseArea drag.target 整体拖动 + drag.min/max 视口钳制（10px 阈值内算点击被吸收防透传误恢复游戏；首次拖动断默认锚定绑定=会话态接管，窗口缩放越界钳回）；**hover 展开**（可选加分项已做）：常驻一行「✦ 成就 N / M」+（可拖动）提示，悬停展开「最近解锁」名单（root clip + 高度塌 36 收起，Column 对不可见子仍占位故不用 visible 切换）；**数据源**：与进度面板同源 progress VM——计数走 achievements()+revision 触碰（review-L3 可见性门 + qml-touch 守卫同款，非 playing 早退不读 revision 防 0.5s flush 全天候重算）；「最近」双源：会话内 achievementUnlocked 信号名单优先（真时序 ≤3，enterWorld 重置、读档回放 silent 无信号故不误填），空则回退定义序末 3 个已解锁（无时间戳近似，定义序 DFS 末尾 ≈ 最新推进）。纯 QML 布局项无 C++ 探针路径（t778/t783/t790 先例）；矩阵 224 PASS / 0 FAIL 不回归；exe 12s 冒烟零 stderr。**需人工目视**：暂停菜单「进度」→ 大面板居中 + 0.7 压暗（半透明底保留）；右下角悬浮栏计数与暂停面板「已解锁 N / M」一致；暂停态抓住悬浮栏拖动（四向箭头光标）任意放置、松手留在视口内；悬停展开最近 2-3 个成就名（读档进世界后显示定义序末 3 个、新解锁后置顶刷新）；窗口缩放后悬浮栏不丢失。

### 🅸 火与点燃语义重做（t841-t846）
**t841** 火上不能叠火：打火石对已有火的格子再点火无效（现状「火上面可以再放火」= 可叠火/刷新，须幂等拒绝——命中 Fire 格直接 no-op 不消耗耐久不挥手）。 ✅✅ 已完成（**Game 层单点收口**：playercontroller flint 分支 `m_hasHit` 后首查 `hitId==Fire ∨ world->isBurningAt(hit)` → **纯 no-op return**（不消耗耐久不挥手不计时刷新），插在 t843 igniteFlammableAt 调用之前（该入口内部的「已燃幂等不重置」退居双保险）。**三态覆盖**：① 立地火格 blockAt==Fire ② 燃烧态方块格 isBurningAt 侧表（板/门外表裹火但栅格 id 未变——只有侧表知道它在燃）③ 火把不算火（Torch 既非 Fire id 也非 flammable → 守卫天然放行，火把上点火行为不变）。**探针**：P21 新增 (a2) World 侧守卫子探针（Fire 格拒点 / Planks 点燃后再点拒 / 燃烧中 10 窗后仍烧毁=**计时未被二次点火重置** / Torch 非火可照旧处置 / LilyPad 不可燃）；打火石守卫本体在 PlayerController（私有 m_hasHit 不可直编）→ P20 先例代码同构+人工目视收口。矩阵 220 PASS / 0 FAIL 三跑稳定。**需人工目视**：对火格/燃烧中木板右键打火石 → 无挥手无耐久消耗无火变化；对火把右键 → 行为照旧（可正常点火）。
**t842** 火焰动画顶部毛刺：火动画顶部有 2-3 像素高的「叉」叠在火上方 → flipbook 帧顶部溢出采样（疑帧区上沿采到相邻贴图/UV 顶边未钳），build_fire.py 帧窗或 fire 面片 v 上界复查。 ✅✅ 已完成（**实测结论：帧图无病，病在采样边界**。PIL 逐帧量 qrc fire_strip.png 与 pack fire_0.png 抽帧两路：所有帧顶部 1-2 行全透明、火体内容自第 2-3 行起、基行（15 行）近全宽不透明 → 帧窗无溢出；真源 = **UV 窗界双线性混叠**：BillboardQuad UV 顶 v=1 经 scaleV=1/N + positionV=k/N 恰映射到窗界 (k+1)/N，Repeat 环绕下双线性以 ~50% 权重混入**下一帧全宽不透明基行**（α≈127 > alphaCutoff 0.1）→ 面片顶部 2-3px 横条，双交叉面片两道横条即「叉」。**修法 = 材质级采样窗上沿内收 1 纹素**：Main.qml fireStripTex `scaleV = 1/N − 1/(N·16)`（16=帧像素高；positionV 不动 → 底缘仍钉 k/N，基行行为零回归）；burningDelegate 共享 fireStripTex 自动同修；portalStripTex 同 UV 模式但帧全不透明 → 混叠不可见不动；**不改** BillboardQuad 共享几何（图标/传送门/掉落物全用它，动了全面回归）也**不改** build_fire.py（帧不是病根，无 PNG 再生成）。pack 路径核对：extractAnimFrames 原帧直取 + paintColumnFrames yTop=H−(k+1)·16 与 qrc 同布局（帧序差异仅换动画相位）。纯 QML 无 C++ 路径。**需人工目视**：qrc 态 + pack 态火顶横条/「叉」均消失、火焰动画本身无跳变。
**t843** 🔥 可燃物直燃语义重做（t724/t803/t804 后第 4 次调整，本次按 MC 语义重设计非补丁）：打火石右键**可燃方块**（木制品/树叶/草丛等）→ 火不出现在旁边，而是**该方块本身点燃**：外表覆一层火焰贴图（面火 overlay，无需 3D），燃烧计时到 → 方块烧毁（无掉落），蔓延 = 相邻可燃方块进入同态（对齐 MC fire-on-face）；**只有打火石点空地/非可燃面**才生成现有 3D 立地火焰；生存模式接触燃烧中的方块 → 着火扣血；生存着火后**切创造 → 着火效果清除**（计时+视觉全清）；3D 立地火支撑消失 → 立即熄灭（失撑链核）。 ✅✅ 已完成（**燃烧态存储 = World 侧表 m_burningCells**（QHash<打包坐标, quint8 剩余窗数>；不用 state 位——门/台阶/熔炉各族 state 位各有语义不可挪用，燃烧是运行期瞬态本就不进存档，读档侧表丢失=自然熄灭（dev-spec 明示可接受），beginLoad/rebuildFireCells 双清）。**渲染 = QML 面火 overlay**（点燃不改栅格 id → 零 mesh 重建；Main.qml burningHost+burningDelegate：每燃烧格 5 片 quad〔±X 绕 Y90°/±Z 正向/顶面绕 X90°〕fireStripTex NoLighting+NoCulling+Mask alphaCutoff 0.1、偏移 0.505 防 z-fight；blockIgnited 挂 + blockBroken/cleanupVis/enterWorld 收（enterWorld 只清不重建——瞬态不还原）、addBurningVis 内 isBurningAt 真值校验——fireHost 同款模式；mesher 出面片路线否决：mesher 无从感知侧表，为渲染改架构不值）。**单一入口 World::igniteFlammableAt**（Q_INVOKABLE；三入口共用：打火石右键 / tickFire (b) 火格掷骰 / (d) 同态掷骰）：非可燃/越界/已燃（幂等不重置）/**湿燃料**（fireWaterNeighborAt——批 G 防火带 verdict 三入口统一收口在此，原蔓延点位重复 gate 移除）→ false（打火石回退现有 3D 立地火路径）；命中 → 插表 + emit blockIgnited；**门整扇联动**（#16 迁移版）：目标 isDoor → 配对半扇（state bit3 互补 y∓1）同为门且非湿 → 一并点燃（同计时同窗烧毁）。**tickFire 四 pass**：(a0) 立地火失撑即灭（fireSupportedAt：6 邻 isSolid∨isCollidable〔门/活板门薄板碰撞实体可撑火，草丛/树苗不算〕∨下方 Fire 火柱链）+ (a) 寿命/批 G 双抑制谓词 + (b) 蔓延掷骰 25‰(2.5%)→igniteFlammableAt + (c) 上窜 + **(d) 燃烧态推进**：越界/陈旧摘除 → 抑制掷 0xF17D 40% **火灭块存**（摘表不动栅格）→ 计时-1 归零烧毁（**无掉落**：setBlock(Fire) 放置语义不发 blockBroken——t724 语义保持；cap 内燃起 3D 余烬火=链式烧穿能量源〔机制等价 MC 烧穿位留火，木墙烧穿可靠性不再依赖单块掷骰〕，cap 外 Air）→ 门烧毁收尾（对偶仍是门**且不在燃**才同窗 Air 收尾；在燃则留它本窗自烧——防误发 blockBroken+摘对偶燃烧态，**首跑矩阵实毙此竞态后修**）→ 同态蔓延掷 0xF17E 25‰(2.5%)（独立盐值与火格掷骰解耦）。计时档位 kBurnWindowsWood=10 窗（5s）/kBurnWindowsLight=4 窗（2s，叶/苗/草闪燃）；kFireCellCap=256 改为火格+燃烧格**合计**预算（同为链式 mesh 重建预算消耗方）。**setBlock 契约扩展**：任何显式写（含同 id no-op——测试复原语义）先清燃烧侧表再走无变化早退；4/5 参数双入口 + **checkFireOnEdit** 编辑钩子（编辑格 6 邻 Fire 失撑 → 同一 setBlock 调用内 setBlock Air 即时熄灭，checkRailOnEdit 钩子族模式；tickFire (a0) 逐窗兜底静默直写路径）。**接触点燃**（t803 链扩展）：玩家 step() 与 EntityManager mob tick 同款三格判定（脚下一格〔踩燃烧板顶面〕/脚位/眼|身体位〔穿入燃烧草丛〕）→ 并入既有 fireTimer→burning 链；切创造清火=既有非 Survival 分支（每帧 m_fireTimer=0+burningChanged）全覆盖。**矩阵**：t804 P21a 木墙探针按新语义重写（+首燃中途观测 isBurningAt∧id 保留）；review#16 门探针重写（联动=同调用双 isBurningAt∧双半仍 WoodDoor + 烧毁 doorBreaks==0）；review-g#5(a) 检测改 isBurningAt（每窗复原依赖同 id 写清态契约）+(b) 加 !isBurningAt 反证；**新增 P-t843 六段探针**（专用世界 48×48×96 gy=88 免凿高空层〔heightAt∈57..71+树冠~+10〕：a 直燃进态+精确 10 窗计时〔恰 9 窗在燃/第 10 窗烧毁余烬火/600 窗无燃料自熄 Air〕+Stone 拒+幂等拒；b 湿燃料防火带确定性拒+燃烧中变湿 40%/窗浇熄火灭块存〔双板≥1 块存，烧穿尾部 0.6%/板〕；c 同态蔓延存在性〔10 泳道 400 窗全场无 Fire 格纯燃板掷骰，P(全未中)≈3e-44〕；d mob 踩燃烧板 40 tick 内着火〔<首次火伤 1s，无随机熄灭混淆〕；e 立地火失撑同 setBlock 调用内即灭+有撑有料对照确定性存活〔有燃料无自熄掷骰〕；f 替换/同 id 写清态）——**220 PASS / 0 FAIL**（基线 219+1；三跑稳定）；exe 冒烟 12s 零 QML 错误。玩家侧接触/切创造清火=mob 同款谓词复制于 step()（Game 层 captured 物理闸门+私有 step 不可直编，P20 先例），代码同构+人工目视收口。**外围项均未顺带覆盖**：t841（火上叠火走回退立地火路径不改）、t842（贴图毛刺）、t844（入火 0.8s 窗仍在）、t845（火灭白粒子）、t846（水域禁火=回退路径放置判定）不受影响仍开放。**需人工目视**：① 打火石右键木板/原木 → 板身裹火焰 overlay 持续 ~5s 烧毁无掉落、木墙沿链烧穿；② 点门任一半 → 整扇同烧同灭；③ 生存踩燃烧木板/穿燃烧草丛 → 着火扣血、切创造火焰立清；④ 水邻木块点不着（防火带）、泼水到燃烧板 → 火灭板存；⑤ 点空地/石头仍出 3D 立地火、拆其支撑 → 火当场熄灭。〔review27 #7 顺带收口（49b5759）：(d) 烧毁分支 setBlock 后补调 recheckAttachmentsAfterClear(x,y,z,id)（门格除外——配对半扇走带湿/雨守卫的专用分支）——本路径烧尽后贴墙火把/活板门/门不再悬空残留，岩浆点燃改道（t891）同终局同修。〕
**t844** 掉落物入火瞬灭（需求反转，覆盖 t804③ 的 0.8s 点燃窗）：丢进火里的物品**立即消失**、无销毁动画无白烟（岩浆瞬灭同款语义；t804「短窗可抢回」设计退役）。 ✅✅ 已完成（**itementitymanager tick 火查改瞬灭**：`blockAt(cx,cy,cz)==Fire` → `releaseSlot + continue`（岩浆 t343 分支同款镜像，无动画无烟）。**三件套退役**（h：itemBurned 信号 + fireBurn 倒计字段 + kItemFireBurnSec 常量全撤，墓碑注释留档；alive 仍末位保 tail-default 聚合初始化契约）；Main.qml onItemBurned Connections 移除（burstDeathSmoke 保留——mob 死亡/TNT 烟仍有消费端）；原白烟第三源随信号退役消灭。**语义选择（spec 要求明示）**：只有**立地火格**（blockAt==Fire）烧掉落物；**燃烧态方块格不烧**——燃烧板栅格 id 仍是 Planks（t843 不改栅格设计），tick 火查 `==Fire` 结构上够不到 → 燃烧木板上的物品存活（机制等价 MC：方块着火≠格子是火，物品落火块上不被「块火」点燃）。**探针 P21(c) 重写**：c1 火格上方 spawn 物品 [20,40] tick 内消失（物品先坠落 ~27 tick 入格；上界 40 证无 0.8s 窗——旧语义 ≥65）/ c2 岩浆对照 [1,3]（格内生成即灭）/ c3 新燃烧板探针：板点燃 + 板上方 2.5 格 spawn 物品 **200 tick 存活**且板燃计时冻结（items.tick 不驱动 tickFire）。矩阵 220 PASS / 0 FAIL 三跑稳定。**需人工目视**：物品丢进立地火 → 瞬间消失无白烟无动画；木板点燃燃烧中把物品丢板上 → 物品安然存活可拾回。
**t845** 火灭粒子白色修：火熄灭弹出的粒子是白色 → BlockParticles blockColor 表缺 Fire 行走 default 白（t806 传送门同款病），补火色（橙红系）。 ✅✅ 已完成（BlockParticles blockColor 表补 `case 137: return "#e86010"`（外焰橙红 = build_fire.py 火体橙 (232,96,16) 同源取色）。**两源分开核**：① burstBreak 经 blockBroken(137)——立地火被浇熄/挖掘/失撑即灭的粒子；② burstPlace 经 blockPlaced(137)——(d) 燃尽余烬火点燃放置的火苗粒子；两路同过 137 行同色。原第三源 itemBurned 白烟已随 t844 信号退役消灭（本项提交时白烟路径只剩零）。t806 传送门 default 白同款病同款修法。**需人工目视**：火熄灭粒子与余烬火生成粒子均橙红非白（贴近火体色）。
**t846** 打火石水域禁火：现状能在水面/荷叶旁点火 → 火悬浮水上。修：点火目标格含水/荷叶顶 → 拒绝（不消耗）；火方块不可存在于水面（生成链全查）。 ✅✅ 已完成（**主漏定位 = 打火石命中睡莲**：LilyPad ShapeNone 但 isCollidable 特例真 → 射线全格命中睡莲面；点火失败（非可燃）走回退 → 落火位 = hit+normal 上方 ==Air → 火浮睡莲顶 = 火悬浮水上（水格本体因 ==Air 门早被拒——Water 非 Air，睡莲是唯一绕道）。**修**：flint 分支 `hitId==LilyPad` → return 拒绝（不消耗不挥手，与「目标被占」同语义）。**立地火生成链全查闭合**：写点共 4 处——① 打火石回退（==Air 门 + 本修补睡莲漏）② tickFire (c) 上窜（above==Air 门）③ (d) 余烬（仅替可燃格，水格非可燃）④ entitymanager 火球点燃（==Air + hitWater 双门）→ 睡莲是唯一漏点，已闭合。**已知边缘不修**：睡莲上放木板再点木板 → 板燃（可燃格在睡莲上，语义合规——烧的是板不是水）。**探针**：P21(a2) LilyPad igniteFlammableAt=false 断言（World 侧）；打火石守卫本体 PlayerController 层同 t841 人工目视收口。矩阵 220 PASS / 0 FAIL 三跑稳定。**需人工目视**：对睡莲/水面右键打火石 → 无火无消耗；对水邻空地 → 立地火正常但贴水即被 t843 防火带抑制熄灭。

### 🅹 植物放置规则（t847）
**t847** 草丛放置校验：草丛只能放**泥土/草方块顶面**——不能草上叠草、不能放树叶上、不能直接放水下；枯灌木/花族同核核查（花 t788 后 dropId 链已动过，校验侧一并过）。 ✅✅ 已完成（**核查基线**：枯灌木（沙限定）/花（泥土/草/耕地，恰 MC 1.0 BlockFlower.canBlockStay 同集含 tilledField）/蘑菇（泥土/草）三条预检已在——**唯 TallGrass 完全无预检**（草上叠草 / 树叶上 / 悬空 / 水下全能放）。**修法 = 收口 Core 单一权威 plantGroundBlock(plantId, groundId)**（草丛→泥土/草方块；花→+耕地；蘑菇→泥土/草；枯灌木→沙；非植物恒 false）——playercontroller 三处分散内联判定 + 新增草丛分支**统一并为一条 cross 植物族预检**：① 目标格须 ==Air（**水下拒绝**——主选体射线不挡水 → 瞄水面目标格落水格，通用放置门放行水格（排开流体）→ 旧版直接「种进水」；甘蔗 t547③ 同口径）；② 下方须合法着地面（谓词同源）。拒绝 = 不挥不消耗（仙人掌 / 铁轨预检同口径）。地面集只在一处定义，与失撑掉落链（checkFlowerMushroomOnEdit / dropUnsupportedCropsAround 的「下方唯一支撑被破 → 整株掉」）互为表里不漂移。矩阵探针 t847（真值表 17 断言 + 花失撑 dropId 链 t788 回归钉）；placeBlock 私有不可直编 → 放置拒绝人工目视收口（t841 P20 先例）。**250 PASS / 0 FAIL**（基线 242+8）。**需人工目视**：① 草丛对草丛 / 树叶 / 沙顶 / 水面右键 → 拒放（不挥手不消耗）；对泥土 / 草方块顶正常放；② 花放耕地可、放沙拒；枯灌木放沙可、放泥土拒；③ 草丛对水下地面（透水瞄底）→ 拒放。

### 🅺 下界传送门无限尺寸（t848）
**t848** 传送门尺寸上限 23×23（用户 8-24 更正：不无限，对齐 MC 1.0 上限）：t806 现上限 4 宽×5 高——用户实测**最大只有 4×4 能点燃，再大就激活不了**（比 t806 声称的还小，真 bug 定位）→ 轴向 for 循环检测放宽到内腔 2×3 最小 .. 21×21 内腔（框外沿 23×23 上限，MC 语义）；**共用边框门**：两门中间共用一竖列黑曜石时各自可点燃（框检测从「独占框」放宽为「共享柱可复用」）。 ✅✅ 已完成（**复现结论**：修复前探针实测 5×4 内腔 lit=false / 0 门格——拒绝位 = **③ 量宽**（旧 `kMaxW-1=3` 截断 → w 恒测 4）→ **④ 右柱校验**打到第 5 内腔列（空气格）判败；4×5 本身并未失效（t806 探针 ⑧ 四角点燃位全过）——**t806 探针盲区** = ⑧ 只测「上限内 4×5」、③ 反把 5 宽当「超限拒」断言，验的是「设计上限合规」而非「用户期望的更大门」，轴向 X/Z / 点火位 / 缺角均非变量。**修**：World::tryIgniteNetherPortal 单一权威常量 `kMinW=2, kPortalMaxInteriorW=21` / `kMinH=3, kPortalMaxInteriorH=21`（改门尺寸只动 2 行；四步算法天然随界缩放：① 下探 ≤21 步 / ② 左探 ≤20 步 / ③ 量宽量高 21 截断 → 22+ 超限开口在 ④ 柱/梁校验打内腔空气格自然拒点 / ④ 矩形复验 O(w×h)≤441 格读）。**共享柱**：柱检查本就只验「本格是黑曜石」不验独占 → 双门共柱天然各自成门（探针钉契约：先点 A 门 B 侧零误填 → 再点 B 门 A 门 12 格健在；破共享柱**双门同熄**——共享柱对两门都是承重格，批 F 熄灭钩子按各自连通域收域）。**性能**：检测 ≤441 格读；点燃 ≤441 格 setBlock（Air→门格纯放置不触发熄门钩子 → 填门不自扰；逐写 clearAllDirty 即时收口无脏 chunk 累积；一次性代价同小规模爆炸可接受）。**探针**（t848 新 rig 64×64×64 独立世界）：① 用户复现位 5×4 成门恰 20 格 ② 21×21 X 平面成门 441 格=内腔面积 state=0（点燃位=开口右上角，压满 ①21 步下探+②20 步左探两扫描上界）③ 21×21 Z 平面 441+state=1 ④ 22 宽/22 高拒零格 ⑤ 2×3 最小仍可 ⑥ 共享柱双门 12→12+12→破柱同熄 ⑦ 缺角 21×21 成门 ⑧ 破底梁中格整门 441 格全熄；t806 ③ 超限样本 5w/6h→22w/22h（rig 世界 32→64 高）。矩阵 **221 PASS / 0 FAIL 三跑稳定**（基线 220+1）。**需人工目视**：搭 21×21 内腔黑曜石巨门打火石点燃 → 整面紫色漩涡门即时成形（441 格 delegate，允许一次成门短暂卡顿）；两门共用中间竖柱各自点燃互不干扰、破共享柱双门同熄；22 宽内腔拒点回落普通火苗。

### 🅻 铁砧与活板门（t849-t851）
**t849** 铁砧非整格三件套 + falling 形状：① falling 下落渲染是「一块完整石头」（t794 单立方妥协）→ 改铁砧窄形（XZ 0.75 足印的三盒或窄单盒）；② **选中框**黑色边框按整格显示 → 窄 AABB（对齐 t801 栅栏 selection 特例模式）；③ **碰撞盒**整格挡人 → 0.75 宽，玩家可进入边缘缝隙；④ 底下**阴影**整格被挡 → 光照/PCF 列顶按窄足印。仙人掌同查（同是非整格方块：选中/碰撞/阴影三件套对齐实际形状）。 ✅✅ 已完成（**单一权威 `BlockRegistry::anvilShapeBoxes`**（底座 12×4×12 @y[0,4] + 腰柱 4×6×4 @y[4,10] + 顶砧台 12×6×10 @y[10,16]，16 像素格坐标逐字镜像 partialblockgeometry 铁砧 case）喂 **collisionAABBs/selectionAABBs/raycastAABBs 三消费端**（t801 栅栏特例模式——铁砧 def.shape 保持 ShapeFull 不动：isGravityBlock 重力族 / mesher 邻居剔除 / lightOpacity 满遮等共享谓词零回归，lessons-learned 四消费者铁律逐消费者特例）；**raycast fullCell 特判**同步收口（raycast.cpp `isFullCube(b) && b!=Farmland && !isAnvil(b) && b!=Cactus`，t639 耕地同款——足印外环隙 2/16 的射线透视命中后方块）。**仙人掌三件套同查**：碰撞/选中/射线同盒 {0.1,0,0.1,0.9,1,0.9}（0.8 居中柱贴 kCactusInset 渲染；接触伤害 tick 的 collisionAABBsAt 点测自动收紧到柱内）。**④ 阴影/光照 = heightmap 排除**（chunk.cpp setBlock 增量维护 + recomputeColumnHeightmap 重扫两处，t150b Torch 排除先例模式）：铁砧三阶段 / 仙人掌不入 heightmap → PCF 列顶实面（columnTopSurfaceY = hm + solidTopOffset）自动落到下方支撑块（不再被异形行抬高整格黑影一环）；消费端兼容性逐个核过（出生搜索由「脚位非空」一票否决兜底、闪电落点、鱿鱼水面生成水不排除）。**① falling 形状 = 三盒 Model**（Main.qml fallingAnvilLoader：Loader 门控仅铁砧下落实体实例化——[perf] mob 类型 Loader 同款零常驻节点；三个 BlockCube 子块按 anvilShapeBoxes 布局 scale+position 摆位，per-face 图集 UV 自动取 def.topTile 113/115/116 三阶段裂纹 + 侧底 anvil_base 114、光照/昼夜/软影走 BlockCube world 链与地形同亮度曲线；旧 t794 XZ 0.75 单立方折衷退役，主 fallingBlockModel.visible 排除铁砧三 id）。**探针**：P-t849 (A) Core 三件套值锁（三阶段 collision=selection=raycast 三盒、足印 [2,14]/16、腰柱 [6,10]、顶台满高；仙人掌 0.8 柱）(B) World rig 净空带搜索（防 worldgen 地形抬 hm 空转——仙人掌列支撑用 Stone 非 Sand，Sand 是重力块放置即坍落）+ 环隙点测（足印外点 pointBlockedByCollision=false 整格时代必挡 + 腰柱中心仍挡）+ heightmapAt 不变 + columnTopSurfaceY=支撑顶。矩阵 **225 PASS / 0 FAIL**（基线 224+1，三跑稳定）；exe 冒烟 12s 零 QML 错误。**需人工目视**：① 准星瞄铁砧 → 黑色选中框贴三段轮廓（非整格框）；② 站铁砧旁 → 可走入足印外 2 像素环隙（贴得更近才被挡）；③ 铁砧/仙人掌旁地面阴影不再整格黑块；④ 挖铁砧下方支撑 → 铁砧以**三盒窄形**下落（非单立方石头），落地还原；⑤ 挖仙人掌旁地 → 视线可穿仙人掌四侧 0.1 环隙选中后方块。
**t850** 铁活板门阴影被挡（t849④同族）：活板门/铁活板门按整格投影遮光 → 按实际薄板形状（开启态/闭合态分别核）。 ✅✅ 已完成（与 t849④ 同一刀：**活板门两材质（Wood/IronTrapdoor）并入 heightmap 排除**（chunk.cpp isTrapdoor 谓词）——合态薄板（顶 0.1875）/ 开态贴边竖板均不再抬升列顶，PCF columnTopSurfaceY 恒落下方支撑面（t360 solidTopOffset 的 0.1875/1.0 区分仅在「板是列顶」时参与，排除后列顶契约简化为「支撑块顶」——开/合两态一致，机制等价 MC 薄板不投整格影）。天光 flood 不读 heightmap（独立 BFS 走 lightOpacity：合=7 半遮 / 开=0 全透，t742 既有）不受影响。**探针**：P-t849 (B) 木/铁 × 开/合四态 heightmapAt==支撑行 + columnTopSurfaceY==支撑顶逐格断言。矩阵 225 PASS / 0 FAIL。**需人工目视**：合态活板门正下方地面不再出现整格方形暗斑（半遮 7 的 flood 渐变保留）；开态竖板旁地面无整格黑影。
**t851** 活板门/门放置支撑校验：活板门必须依附实体方块面（顶面或侧面）放置——不能活板门套活板门悬浮叠；**门不能门上叠门通天**（每扇门须站在实体支撑上）；对齐 MC 1.0 附着语义，失败时拒绝放置（红石破坏支撑 → 掉落的失撑链一并核）。 ✅✅ 已完成（**活板门放置预检**：playercontroller placeBlock isTrapdoor 分支（Wood+Iron 统一）——目标格下方或四侧水平邻任一 **trapdoorSupportBlock**（新单一权威谓词：isCollidable 且排除活板门/门自身——附着族不互相依附，「板套板悬浮叠」「板贴门板」两侧一致拒，torchSupportBlock 排除火把同款口径）才放，否则拒（不挥）；与 World 失撑复检同谓词零漂移。**门叠门拒放**：现状核查确认 t741 isTopFlushSupport(Door)=false 已天然拒（探针钉契约），零新代码。**失撑掉落链**（World 层单一权威）：`checkTrapdoorDoorSupportOnEdit` 编辑钩子（本格编辑后非本族 → ②正上方活板门依附面全失/门下扇失去齐平支撑 + ③**本层四水平邻贴墙板拆墙**侧撑丢失两路扫）+ `dropUnsupportedDoorsAbove` 级联（连续活板门柱 + 连续双格门逐格清，门两半各一掉落物——MC 双格门出 2 件；门叠门通天链逐扇脱落；静默直写 + N 写 1 emit 批量收口，dropCactusColumn 先例）。**接线四入口全覆盖**：setBlock 5 参主入口（玩家挖掘/红石）+ setBlockSilent（系统静默写）+ destroySphereSilent 逐破坏格（**爆炸拆支撑**）+ recheckAttachmentsAfterClear（TNT 点火/重力坍落清格——批 C 公共复检收口）。玩家直破防双掉：门配对联动 finishMining 负责直破（上扇入口跳过 + 本族编辑早退守卫）；PlayerController::dropUnsupportedDoorsAround 玩家路径补刀（与 World 钩子同谓词）。**探针**：P-t849 (C) 贴墙板拆墙级联掉 1 件 + 3 扇门叠柱（直写模拟绕过预检的脏世界）静默拆台 6 格全掉 6 件 + 有撑门邻破零误伤；(D) trapdoorSupportBlock 谓词锁（Stone 允 / Air 拒 / 板-门自身拒）+ isTopFlushSupport(Door)=false 门叠门拒放口径 + isCollidable(Trapdoor) 恒真（碰撞语义不动）。placeBlock 射线段 PlayerController 私有不可直编 → 放置拒绝人工目视收口（t841/t846 P20 先例）。矩阵 **225 PASS / 0 FAIL**（三跑稳定）。**需人工目视**：① 手持活板门对空处/对另一块活板门顶面右键 → 拒放（不挥手不消耗）；② 对石顶/石侧正常放；③ 挖掉贴墙活板门的那面墙 → 板当场掉落成物品；④ 挖门下地面（含 TNT 炸）→ 整扇门掉落；⑤ 门上放门 → 拒。

### 🅼 死亡流程修复（t852-t853）
**t852** 死亡掉落回归修复：用户实测死亡后**背包物品都在、不掉落了**（「之前一直都有」= 回归）→ git log 定位丢失死亡掉落的提交（搜 deathLoot/dropAll/mobDied 链），恢复死亡时全部物品+护甲掉落（对齐 R19.4 死亡掉落备忘）。 ✅✅ 已完成（**回归定位**：丢失点=commit dac18d3 fix(t690)（R19.6）——其在 onDied 辅助段写 `window.craftingTablePanel.returnCraftToHotbar()` 等三行（9570-9572），**QML id 不是 Window 对象属性 → `window.<面板id>` 恒 undefined → `undefined.returnCraftToHotbar()` 抛 TypeError**（本工程 t603「window.progress 恒 undefined」同款已实证病），而 QML 信号处理器异常被静默吞（t312 教训）→ onDied 在辅助段中断 → `player.dropAllItems()`（掉落+清空+置 m_dead）与 `player.release()`（释放指针）**从未执行** = 用户四症状一根因：①死亡不掉落+背包不清（t852 本体）②指针锁死须 ESC 才能点死亡屏按钮（t853③）③m_dead 未置位→t655 输入闸门全开（尸体能走/转视角，t853①②）。t690 同批在 saveAndExitToWorldList（708-710）用的是**裸 id**（正确形态）——同批两种写法、死亡路径拿到坏的那种。**修复**：① 三行改裸 id（`craftingTablePanel.returnCraftToHotbar()`，对齐已验证形态）；② 死亡主链结构性收口——辅助段（关面板/归还槽）包 try、主链（dropAllItems→release→XP 球→播报）入 **finally**：任何单点 UI 异常至多丢「合成格归还」支线，死亡契约恒执行（「处理器异常静默退化」教训在死亡链上的结构封印，release 提前到花絮之前防花絮异常反噬指针）。**矩阵探针 t852**（t814 真消费端模式：PlayerController+Hotbar 直编+spawnItem 直连计数）：hotbar 泥 64+附魔改名磨损钻石剑 / main 木棍 32 / 光标石 3 / 护甲附魔头盔四段全掉，附魔+实例名+耐久末三参透传逐位断言，3×3 散布 |dx|,|dz|≤1，掉落即清（四段+held 全空），二次调用零发射——**215 PASS / 0 FAIL**（基线 214+1）。C++ 本体链实证恒掉恒清，断裂面纯在 QML 路由层（已修+静态契约钉在 onDied 头注释）。**需人工目视**：生存死亡（摔死/怪杀）→ 死亡点地面喷出全部 hotbar+背包+护甲物品（附魔工具带光晕、改名带名、耐久不回满），走回可捡回；聊天栏死亡播报即时出现。
**t853** 死亡输入锁全套：① 死后**视角还能转动/晃动** → 死亡态锁鼠标视角；② 移动/打开背包全锁；③ 「你死了」UI 出现即**自动释放鼠标指针**（现状须按 ESC 才能点按钮）；④ 死亡态按 ESC **不打开暂停菜单**——只能点「立即重生」/「回到主菜单」两按钮（ESC 至多无效或再次释放指针）。 ✅✅ 已完成（①③ 本体修复=t852 同一根因：onDied 被 TypeError 掐断 → release() 不跑（指针锁死=captured 残留 true → pollMouse 持续转视角）→ 修复后 release 在 finally 最先段执行=死亡屏出现即指针自由可点按钮；**单一权威**钉死：playerState.dead（Game 层 Q_PROPERTY）= QML 输入路由唯一判定（keyInput 闸门/WheelHandler/pauseOverlay.visible/chatDisplay.visible 全读它），PlayerController.m_dead 是 C++ 物理闸门镜像（dropAllItems 置位/respawn 复位）不进 QML，两层经 onDied→dropAllItems 单点同步。① 纵深防御：pollMouse() 加 m_dead 早 return（兜「死亡但 captured 残留」的任何漏 release 路径，同 grab()/setKey() 的 t655 闸门族）。② 既有 t655 全套闸门此前被同一根因架空（m_dead 未置）→ 修复后自然恢复：C++ setKey/placeBlock/attack/eat/bow/grab 全拒 + QML keyInput 死亡闸门吞 E/WASD/1-9/Q/F5 + WheelHandler 滚轮锁。④ keyInput 死亡闸门把 **Esc 从放行表移除**（t691 旧「死亡态 Esc 开暂停叠层」语义退役，注释同步改写）——死亡态 ESC 键盘层直接吞掉；pauseOverlay.visible 本就含 !playerState.dead（双保险，防未来暂停路径漏判）；聊天 T/Enter 仍放行（t691 遗言语义不动，chatInput 自持焦点其 ESC 关聊天不受影响）。矩阵随 t852 探针 215 PASS/0 FAIL；exe 冒烟 10s 无 QML 错误。**需人工目视**：死亡屏出现瞬间光标即可见且直接可点两按钮（不按 ESC）；死亡态晃鼠标视角不动、WASD/E/Q/滚轮全无效、ESC 无暂停菜单弹出；T 开聊天仍可发遗言。

### 🅽 装备与玩家模型（t854-t855）
**t854** mob 护甲覆盖修：僵尸/骷髅穿装备——胸甲**没覆盖手臂**（MC layer_1 = 躯干+双臂一体壳）、铁裤**只有膝盖段有护甲**（layer_1 腿件应全腿高）、靴子**没全覆盖**（layer_2 靴壳包脚+踝）→ ArmorLayerBox 盒几何覆盖范围对齐 MC 分层（t718 盒复查，逐件目视核）。 ✅✅ 已完成（e6b2681）：根因=ArmorLayerBox C++ 盒几何（±0.5 单位盒 + MC box-UV）本身无误，**覆盖范围全由 QML delegate 的 position/scale 决定**——t377/t560 期 mob 护甲盒按「视觉提示」拍的尺寸系统性偏离 MC 分层语义。修法（Main.qml Shambler/Bones 两段 delegate）：① 胸甲壳全盖躯干（(0,0.05)@全高 0.60+探 0.04——MobModel 躯干 y∈[-0.25,0.35]，旧 (0,0.12)@0.50 露肚段）+ **补双袖壳 ArmorLayerBox{piece:2} 绑胸甲槽**（MC layer_1 胸甲=躯干+双臂一体壳，旧版 mob 完全没有袖）；Shambler 前伸横臂袖壳 eulerRotation.x=90° 使 12px 袖条带高轴沿臂长（机制等价 MC 僵尸甲袖随前伸臂），Bones 垂直竖臂无旋转、**右袖挂弓肩枢 Node 同枢同角**（刚体随 addBoxRot 瞄准抬臂，review M10 模式——静态袖会被满拉抬臂穿出）；② 护腿全腿高（腿 local y∈[0,-0.65] → (0,-0.325)@0.70；旧 (0,-0.05)@0.40 只盖髋下 40% 且 X 0.20 比腿 0.22 还窄=部分嵌进腿内）；③ 靴=脚+踝段（(0,-0.50,-0.03)@(0.26,0.34,0.30)，y∈[-0.67,-0.33] 包踝+脚、z 前探成靴头同玩家靴先例；旧 (0,-0.57)@0.16 只盖脚底一小截）；Bones 按细骨比例缩窄同修。**玩家侧同修（共用语义 blessed）**：playerModel + CharacterPreview3D 胸甲袖 0.52→全臂高 (0,-0.35)@(0.30,0.74,0.30)（MC layer_1 袖盒 4×12×4 与臂同高 12px 到腕；玩家躯干/腿/靴三件原已达标不动）。纯 QML 改动零新探针（C++ 几何未动），矩阵 240 PASS / 0 FAIL 不变，exe 冒烟 12s 无 QML 错误。**需人工目视**：① 蹒跚者（僵尸）穿全套铁甲——胸甲壳盖住整条前伸双臂（袖到手端）+ 躯干全高无露肚；② 骸骨（骷髅）穿铁甲——垂臂两袖全臂高、瞄准拉弓时右袖随臂抬起不脱袖；③ 铁裤全腿高（走动摆腿时护腿跟腿整段摆）；④ 靴子包脚+踝有靴头（前伸超出脚尖）；⑤ 玩家 F5 自穿胸甲袖到腕（不再只包上臂）。
**t855** Steve/Alex 手臂混搭修：F5 第三人称从背后看**后手臂一半是 Steve 一半是 Alex 的手**（左右臂贴图源/UV 混绑——Alex 3px 臂 vs Steve 4px 臂采样区错位）+ **第一人称手持模型与当前皮肤不同步**（viewModelHand 固定贴图不随皮肤选择变）→ 单一皮肤源贯通三处（第一人称/第三人称左右臂/挥手动画）。 ✅✅ 已完成（e6b2681）：① 「后臂一半 Steve 一半 Alex」根因实证（PIL 实测 demo 包 alex.png）：slim 臂条带右缘 u=54，其外 u[54,56) 两列 **alpha=0 但 RGB=steve 肤色 (170,125,102)**——按 classic 4px 采样时臂背面 [52,56) 采到这两列，且皮肤材质 opacity 1.0 走 Opaque 路径 **alpha 被忽略 → steve 色 RGB 整列不透明显出**（从背后看=背面=混搭观感的确切来源）。采样区错位本体已被 Review #8/#1 系列修复（probeSlimSkinLayout 探测区 u[54,56) + kPiecesSlim 臂 3×12×4，矩阵已有探针锁 Core 侧），本任务收口剩余两环：**(a) 皮肤材质 alpha 契约**——Main.qml 8 件身体部位 + CharacterPreview3D 8 件 + 第一人称臂全部加 alphaCutoff 0.5 + opacity 0.99（观察者 0.35 路径保留，同 t718 护甲壳既有模式）：任何布局差列今后按 alpha 丢弃而非显成异色鬼影列（纵深防御，兼防探测保守误判向）；**(b) 第一人称手持皮肤化**——viewModelHand 固定色两段 UnitCube（蓝袖+肤色）退役，改 PlayerSkinBox{piece:2} 整臂盒 + playerSkinTex + skinIsSlim() 同判定（= 第三人称左右臂同一贴图源同一 slim 判定；/skin 切肤、pack 开关、slim 布局三态即时同步到手持），eulerRotation.x=180 使手纹素（strip 底行）朝上（手臂从屏幕下缘伸出=袖下手上），挥手/进食/拉弓动画驱动父 Node 自动跟随（挥手与皮肤同源达成）；几何对齐旧两段合并包络（中心 -0.045 长 0.29、粗 0.12 沿用，z 深度未动→不穿模契约不变）。纯 QML 改动零新探针（PlayerSkinBox/探测 Core 侧均未动、既有 #1 探针+static_assert 已锁），矩阵 240 PASS / 0 FAIL 不变，exe 冒烟 12s 无 QML 错误。**需人工目视**：① /skin alex + demo 包 → F5 背面双臂全 Alex（无 steve 肤色条纹）；② /skin 切换 → 第一人称手持手臂即时换肤（Alex=绿袖细臂、default=蓝袖，与第三人称一致）；③ 第一人称挥手/进食/拉弓动画正常；④ 背包 3D 预览人物与游戏内皮肤一致。

### 🅱 组追加（t856）
**t856** 发射器发射点燃 TNT：发射器内放 TNT + 激活 → 弹出**已点燃的 TNT 实体**（MC 1.0 dispenser 语义：发射即点燃，短引信落地爆），现有 spawnPrimedTnt 链接通 dispenser 发射路径。 ✅✅ 已完成（**分派表加 TNT 分支**（dispenseFromDispenser：EggId 后、兜底 else 前插 `!isDropper && itemId==TntBlock`——dropper 全物品分支在前天然排除，投掷器弹 TNT 仍是**普通掉落物不点燃**（只投不射口径，机制等价 MC dropper））：**发射面邻格**（发射器格 + state 朝向一格）`spawnPrimedTnt` 走既有链（fuseProgress 白闪 / 重力落地坐支撑顶 / 引爆链式引燃 / QML delegate 全复用）+ **标准引信**（不传 fuseSec → kPrimedTntFuseSec ~5s 自然落地爆，不发明新引信时长；链式短 fuse kChainFuseSec 是爆炸链式专用口径不沾）+ **定向初速** = 朝向 × kDispenserTntPopSpeed 4.0（spawnPrimedTnt 新增**可选** velX/velZ 尾参，primed tick t494 水平积分段消费 → 弹出 ~1 格落地；默认 0 = 机关点火/电力点火/链式既有路径零位移行为逐字不变）。库存同箭/掉落物先例激活一次扣 1（共享尾段恒扣 + dispenserFired 埋点）；空库存无动作（t607）；per-dispenser 2s 冷却 + 全激活源（红石块/拉杆/粉，t772/t814）**零改动**。**两路径并存边界**（注释即契约）：红石直接邻接 TNT 仍是原地引爆（firePowerTnt 既有链不动），只有放进发射器库存经发射才弹出点燃实体——MC 双路径语义。**探针 P-t856**（t814 真消费端模式）：拉杆贴背面激活装 3 TNT 的发射器 → PrimedTnt @ 发射面邻格格心 + fuseProgress==1.0 钉标准引信（链式 1.2s 会给 0.24）+ tick 0.25s 引信递减 + +X 定向位移钉弹射方向 + 库存 3→2；2s 冷却内真上升沿零发射（信号确发计数差分）+ 冷却驱动过 2s 再造沿必再弹（库存 2→1）；投掷器 + TNT → 掉落物 +1 / 零 primed。红石直接邻接原地引爆回归由 t814(a) 复跑覆盖。矩阵 **242 PASS / 0 FAIL**（基线 241+1，两跑稳定）；exe 冒烟 13s 无崩溃。**需人工目视**：① 发射器 UI 装 TNT + 背面拉杆扳开 → 出口弹出**白闪点燃 TNT** 沿排出口朝向小幅飞出（~1 格）落地，~5s 后爆炸（破坏地形 + 链式引燃邻 TNT）；② 发射器内 TNT 计数 -1；③ 对发射器出口正前方放 TNT 方块再激活 → 弹出的实体爆炸链式引燃该方块（既有链式语义）；④ 投掷器装 TNT 激活 → 弹出的是**未点燃 TNT 掉落物**（可拾回）。

### 📎 R19.13 范围与顺序（v2，含 8-24 增补）
t808-t856（49 项；t836 钓鱼为下版本占位）。**建议顺序：t813 构建版本戳最先（三项争议复现的前置）→ t814 红石实机复现 → t822 铁砧附魔复现二 → t852/t853 死亡双修（掉落回归+输入锁，用户痛点）→ t809/t810 矿车玩法阻塞双修 → t811 生物乘坐 → t843-led 火语义组（t841-t846，重做大件）→ t848 传送门无限 → t812 铁轨变道 → t840 成就纠偏 → t834 剪羊毛 → t849-t851 铁砧/活板门组 → t816-t821 浏览器视觉组 → t824-t827 附魔组 → t828-t833 生物组 → t854/t855 装备模型 → t835 珍珠 → t856 发射器 TNT → t815/t837-t839/t847 杂项收尾**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。实机复现类（t814/t822/t823）与新观测类（t848 的 5×4 失败、t852 回归定位）产出无论真缺陷还是旧 exe，都须用户在新版本戳上口头确认后才关单。

---

## R19.14 性能起步批（t857-t860，4 项；2026-08-24 立项，基于 docs/vulkan-rhi-and-simd-survey-2026-08-21.md 调研）

**立项背景**：Vulkan/SIMD 调研结论——瓶颈在主线程 CPU（9FPS 时 main*131ms / render 5.0ms），切 Vulkan 后端帧率≈0 提升。本批只做调研推荐的第 0 步快赢 + 第 1 步中最廉价独立项（C6），全部无后端依赖；C3 halo 快照/C2 单遍多段/C1 meshing 线程化/SIMD 双 TU 分派等 4-10 周级大项**不在本批**，待本批验收后单独立项。

### 🅰 渲染真值与合批（t857-t858）
**t857** F3 渲染统计换真值：`~drawEst`（Main.qml:345 估算值）改 `view3d.renderStats.drawCallCount / drawVertexCount / renderPassCount / renderTime`（View3D RenderStats QML 原生类型，无需新 C++）；顺带修 F3 顶点求和只覆盖 9 chunk 的显示偏差（t178-correctness.md:87 登记项）。验收：F3 显示值与 RenderStats 真值一致、不再随估算公式漂移。
✅✅（1be40f1）View3D 加 `renderStats.extendedDataCollectionEnabled: window.f3Visible`（扩展统计默认关有收集开销 → F3 开才收、关零成本；分组属性语法实测装载过）；draw 行改 `draw-calls: N verts drawn: V passes: P render R ms [RenderStats]`（真值经视锥剔除 / 含透明拆 pass），mesh 行显式标注「built 地形段」闭 t178:87 登记项（built 求和 vs drawn 真值两行各明域）；旧估算公式退役（visibleSegmentCount 降级为 culling 诊断计数）；矩阵源码钉探针断真值四读 + drawEst token 绝迹 + 收集门绑定（306 PASS）；t895 探针窗 4200→5200 适配函数加长。顺带项同 commit：lessons :119 Vulkan 误记勘误（实为 D3D11、setGraphicsApi 从未调用）、:39 greedy 默认开关勘误（t183 默认 false）。
**t858** 掉落物/经验球 instancing 试点：掉落物 + 经验球两类刚体 delegate（数量大、无逐部件动画）改 `Model.instancing` + C++ `QQuick3DInstancing` 子类喂实例表（位置/旋转/缩放/光照 tint），每类压成 1 Model/1 draw；mob 不适用（48 模板逐部件动画，调研已明确排除）。验收：地面 50+ 掉落物场景 F3 drawCallCount 可见下降、渲染观感无回归（拾取判定纯 C++ 侧不受渲染层影响）；若与 alpha 契约/光照 tint 冲突则降级回退并记录。
✅✅（5b44b9f，**范围收敛为经验球整族 + 掉落物取舍登记**）：新 `src/Game/xporbinstancing.{h,cpp}`（QQuick3DInstancing 子类，纯公开 API 无自定义 shader——RHI 囚笼合规）；经验球 Repeater×delegate 全删 → 1 Model + 1 draw（bob 0↔0.12@700ms/leg、pulse 0.85↔1.15@500ms/leg 动画下沉 C++ 解析式逐字对齐旧 QML 数字，per-slot 0.37s 错峰；amount 二值绿走 per-instance color；16ms Precise QTimer→markDirty 驱动逐帧表重算 ≤64×80B）；拾取/磁吸纯 C++ 零影响；t170/t256 delegate 生命周期问题对该族整体消除；xpOrbHost 空壳保留供 clearEntDelegates 链。**掉落物各族保持逐 Model**（取舍钉死在 xporbinstancing.h 头注释）：per-item 贴图（billboard Canvas/图标文件）与 per-item 几何（BlockCube/ItemShape/工具五形）使单 Model 单材质实例表无法覆盖，需按 itemId 分桶动态建 Model+Texture，且该链正是 t170/t256/t437/t492 slot-reuse/hardReset 契约密集区——留独立任务。矩阵 feeder 探针（实例表内容级：空表/位置派生 ±bob 带/clearAll 清空）309 PASS。经验球数字量化待用户实测（t857 真值行）；50+ 掉落物场景的 draw 下降属后续掉落物分桶任务。

### 🅱 CPU 分配修复（t859）
**t859** collisionAABBsAt 堆分配消除：`World::collisionAABBsAt` 按值返回 `std::vector<BlockAABB>`（world.h:135）= 玩家 3 轴 × ~12 格/tick + 60 mob 各自调 = 每帧数百次堆分配；改 out-param（调用方栈上 small_buffer / 复用 vector）+ 全部调用点（playercontroller footprint + mob 四谓词）同步。验收：零警告构建、矩阵全 PASS、行为零变（纯分配路径改写，F3 帧分解 main 桶改善为加分项非门槛）。
✅✅（7a2aa50）`World::collisionAABBsAt(x,y,z,out,cap)→int` + `BlockRegistry::collisionAABBsInto` 单一权威（putAABB 受界定容写入：越界钳制 + 响亮 qWarning 非静默截断；kMaxAABBsPerCell=4 封顶铁砧 3 盒）+ shapeBoxesInto 同构改造；by-value 版全变薄壳（矩阵/冷路径继续用）；**调用点全清**（先 grep 全仓清点后动手）：playercontroller 九处（overlapSubAABBs 最热足迹循环 / overlapsPlayerAABB / canStandUp / autoStepLift / isLockedBuried / extrudeEmbedded / launchUnburyUpward×2 / 末影珠+发射器 cellBlocked 谓词）+ entitymanager 四处（箭嵌入点测 / primed TNT 支撑顶 / mob 窒息 / 鱿鱼天花板钳）+ world.cpp pointBlockedByCollision + 矩阵测试两处诊断；supportTopYAt/collisionTopY 免构建族本就零分配未动。矩阵新探针：全 65536 id×state Into↔薄壳逐盒逐字段等价 + cap=0 只报计数不写字节 + World 下半砖偏移抽查（307 PASS）。行为零变（纯分配路径：旧每查 2 次 vector 堆分配 → 栈上定容直写）。

### 🅲 试验项（t860，可降级）
**t860** cutout 段折叠试验：地形材质已 `alphaMode: Mask`（Main.qml:3888），alpha test 不需要独立半透段 → 尝试 cutout 段（树苗/草丛/花）并入 terrain 不透明段，6 段减 1（600 Model 满配 → 500）；**风险前置**：受 chunkgeometry.h:67-70 D3D11 alphaCutoff 契约（仅 opacity<1 生效）制约，需实测剔除排序/贴图渗色；任何观感回归（草丛边缘/树苗阴影）即降级关闭记录结论。验收：若保留 → cutout 类方块观感无回归 + F3 drawCall 下降；若关闭 → 调研文档登记结论。
✅✅（92c942c，**保留交付**）关键论证：t439（cutout 段改 alphaMode:Mask）+ t442（terrain 段为 leaves 加同款 Mask+0.5）后**两段材质逐字相同**（NoLighting+voxelAtlas+vertexColors+Mask+cutoff 0.5+白 baseColor）→ 合并是同材质同管线同深度写 opaque pass 的逐像素等价并入，chunkgeometry.h:67-70 的旧 opacity 契约前提（terrain 段 Opaque）已不存在；terrain PASS1 路由停止跳过 cross/门/活板门（PASS2 跳过清单不动——这些形状本就仅 PASS1，无双重发射）。实测：app 启动日志 `[t276] built 500 chunk Models`（原 600），F3 drawCalls 真值随之 −100 量级（满配可见段内每 chunk 少 1）。**降级杠杆保留**：cutoutOnly 属性 + 路由分支 + crossChunkComp 模板未删，观感回归时恢复 chunkAnchor 一行 createObject + segmentsPerChunk 5→6 即回 6 段。矩阵探针：行为级（TallGrass 放置使 terrain 段 mesh 顶点增加；扫空位放置——t799/t814 rig 教训）+ 源码钉（无 crossChunkComp createObject / segmentsPerChunk=5），308 PASS。**待用户目视**：草丛边缘 / 树苗阴影 / 门窗格透视 / 活板门栅格孔。

### 📎 R19.14 范围与说明
- 顺带项（并入 t857 提交）：lessons-learned.md 两处勘误（:119「Vulkan 已生效」陈旧条目、:39「greedy 默认开」与代码不符）。
- **明确不做**：Vulkan 后端切换（路线 A，1-2 天回归项，单独排期）；C3/C2/C1/SIMD（4-10 周级，验收本批后立项）；C4 自定义材质（与 B1 重复投资，调研已排除）；C5 图集 mipmap（渗色坑）。
- 每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。

---

## R19.15 用户实测复盘批三（t861-t903，43 项；2026-08-25 立项）

**立项背景**：R19.13+两轮 review 全闭后的第三轮实测报障。两个多版本顽疾升级为最高优先：铁砧附魔丢失（用户原话「七八个版本修复这个没有修复成功了」）、附魔台可放入已附魔物品并清洗附魔（同痛点级）。矿车物理语义、钓鱼可见性反馈、成就 UI 理解纠偏（t840 方向做反）为主要块。

**跨任务说明**：
- **暂停语义调研先行**（t889）：Java 版单机「打开背包/GUI 不暂停世界，仅 ESC 暂停」——本轮统一前，t888/t890 的行为口径以 t889 调研结论为准。
- **t887 成就 UI 纠偏**：t840 理解反了——用户要的是**进度面板内右上角 treeMinimap 可拖出移动**（像小地图一样），不是新增常驻悬浮窗 achQuickDock。本轮移除 achQuickDock 常驻显示 + treeMinimap 可拖动化（拖动手柄语义，非新任务栏 UI）。
- **t898 睡觉动画**（用户 8-25 已澄清）：右键床入睡 → **玩家模型瞬移躺到床上、视角/相机随之移到床位置**（对齐 MC：睡下时人物直接瞬移躺床，不是站在原地）——现阶段人物与视角都留在原地，属缺失。
- **t900 垃圾桶语义反转**：t839 按当时口径收窄为单格；用户本轮明确「shift+左键要能清空整个背包」→ 最终语义：**普通左键=清光标持有（t839 保持），shift+左键=清空整个背包（恢复批量）**。

### 🅰 矿车与铁轨（t861-t867）⚠️ t862-t864 玩法阻塞
**t861** 矿车物品归类：创造背包矿车从工具栏移到**红石栏**（发射器/投掷器同批处理见 t868，此处只管矿车）。✅✅（bbacca5）Inventory.qml 两表同改：vehicleIds 去 0x23E（船保留工具段）+ redstoneIds 尾部追加 0x23E（TNT 后——轨族载具件与动力/探测轨/发射器同页）；Component.onCompleted 契约钉三断言（在红石表内 / 不在 vehicleIds / 恰为表尾）防单边表改回归。纯 QML 表改无探针；矩阵 266 PASS 不变。
**t862** 矿车 3D 贴图两修（t768/t808 返修）：① 四根竖直棱边**重叠共面 z-fighting**（移动视角闪烁）——四帮盒相邻面内缩 epsilon 消共面；② 侧面**一半黑一半正常**——疑侧板左右/内外贴图 UV 反了（盒 piece 采样窗与几何面不匹配），PIL 对照 demo 包 minecart.png 逐面核采样区。✅✅（c2ecf0f）①端帮 X 0.775/Y 0.725 + 纵帮 Z 0.875 + 底板 XZ 0.775/0.875 内缩 + 底缘 −0.3775 下探（帮/板相邻面全没入体内，外轮廓 ±0.40/±0.45 不变、板面 −0.3125 契约不动）；②PIL 实测两壁窗 = 外亮（中灰）/内暗（近黑）预烘——非左右：kPackParts p1/p2 外面同采亮窗、内面同采暗窗。visual-only 无探针（t781 先例），矩阵 261 PASS 不变。
**t863** 矿车坡道物理四修（MC 1.0 语义）：① 上坡速度归零 → **反向滑落**（不悬停半空）；② 悬停/停驻态挖掉下方轨或支撑 → **受重力坠落**（转下落或直接掉落物，按现状语义链）；③ 坡顶前端无轨 + 速度够 → **飞出做平抛运动**（不自动暂停），速度不足才停驻；④ 轨末端静止车**可被玩家推离轨道**进入自由物理。
✅✅（f0a9930）新 derailed 自由物理态（平抛 + 落地轨面重挂/地面真顶贴面 + 虚空移除）。①死区归零点坡面梯度采样 → 反溜起步滑回坡脚（被骑/空车两路）；②停驻/滑行每帧支撑复探（pinCartY→supportTopYAt 两级），失支撑转坠落；③死端判定收窄「坡顶」（后邻轨低一格）+ |speed|≥3 → 飞出（平死端保停靠——车站死端玩法 + t769/t811 探针面不动；平端交互归④）；④pushEmptyCart 死端分支推离出轨。探针 t863 四段（反溜/挖支撑坠落/坡顶飞出落板/死端推离），阴性轮 ×2（反溜禁用红、失支撑坠落禁用红）。t769/t770/t771/t809/t811 追推探针适配（死端格停推）。矩阵 263→264。
**t864** 矿车互卡悬浮：上坡被前方矿车卡住时**悬浮原地一直向上**——碰撞卡阻时应停驻/滑回，不向上漂。
✅✅（2ae8ea2）互卡契约探针：B 停驻坡顶死端格、A 被骑 W 持续 14.4s（PlayerController 同序驱动）——Y 恒贴轨面 ±0.1（上漂签名钳死）、不穿透前车；下车后无供能 → 碰撞冲量压速 + 摩擦死区 + t863① 反溜链滑回坡脚。上漂本体在 HEAD 无法复现（review#3 clampShift + 面钉 Y 已约束）——探针锁行为契约防回归；阴性轮（反溜禁用）红证实滑回半边受护。矩阵 264→265。
**t865** 生物贴轨行走 + 草丛误跳：生物走铁轨**直接踩在轨面上**（不悬浮上方一格——支撑判定把非整格当满格抬高）；同族：僵尸遇草丛**跳过去**——草丛/花等无碰撞植物应可直接走过（isJumpObstacle 对无碰撞格误判，t803 火焰豁免同族反向）。✅✅（b02be01）新增 World::supportTopYAt 单一权威（碰撞 sub-AABB 真顶；无碰撞族 -1）；mobSupportTopY/mobAabbHitsSolid/mobFootprintHasSupport/isJumpObstacle 四谓词收口碰撞语义（枚举豁免退役）；轨上生物穿透轨格踩地板（MC 同款）。t811 探针 (d) 随语义更新（旧断言钉了悬浮行为）。新探针 t865（贴轨 feet 偏差 ≤0.02 + 草丛零起跳），阴性轮旧语义红。矩阵 261→262。
**t866** 载具攻击/摧毁语义：① 矿车载生物时**打矿车本体 → 进矿车掉落逻辑**（现在是打到生物 → 生物永远下不来）；乘员自动释放。② 矿车运动中**碰仙人掌 → 矿车变掉落物**、乘员自动下来；**岩浆同样**。
✅✅（aa6dc17）①beginMining mob 分支乘骑改判（rideCartAt/rideBoatAt → hitCartFromRay/hitBoatFromRay，冷却门 + attackMob 前截断）——乘员 AABB 与车体重叠射线恒先中乘员的根因链闭死；车毁对账链自动释放乘员。②checkCartEnvironment 帧级环境检查（朝向定向 AABB 覆盖格扫仙人掌/岩浆 → destroyCartTail 生存掉落；虚空无掉落移除）；hitCartFromRay 摧毁尾部抽公共 destroyCartTail（t863 提交带骨架）。探针 t866 六段（射线前置钉/生存改判/创造释放/源码钉/仙人掌/岩浆）；源码钉走 t889 先例（captured 门内不可行为级直驱）。阴性轮（环境检查禁用）红。矩阵 265→266。
**t867** 压力板掉落物贴板：掉落物落在压力板上应**紧贴板面**（restY 取板顶，现在悬上方一格——item 支撑判定把非整格当满格，t865 同族）。✅✅（83eadd1）ItemEntityManager 落地 / resting 复探 / 冰面摩擦面三处收口 World::supportTopYAt；复探补下贴（薄支撑拆除滑落到下方真顶，D1-b 同款）；updatePressurePlates 物品板格两格窗派生。隐性收益：岩浆不再被当整格落点（高处落入岩浆的物品沉入格内走瞬毁）。探针 t867 三段（贴板 / 满格回归 / 拆板重落），阴性轮（两路同退旧语义）红。矩阵 262→263。

### 🅱 红石（t868-t871）
**t868** 发射器/投掷器：① 归类**红石栏**（创造背包，与 t861 矿车同批表改）；② **冷却缩短**（现 2s 太长）+ 高频红石可多次激活（用户实测「只能激活一次」——冷却期内高频信号全吞，核冷却语义是否该按上升沿计）。
✅✅（30ffda1 + 3d3735a）① 发射器 107 / 投掷器 117 入 redstoneIds（机关件组拉杆后、粉条目前；方块 tab 按表排除自动隐藏），t861 契约钉扩到 107/117。② 冷却 2.0→0.5s 且语义重钉「短防抖闸」（只拦同 tick 双路径双发 / 沿抖动；逐沿发射归 fireDispenserAtQml t689 上升沿基线）——≥0.5s 间隔的每个新沿都过闸发射（MC 触发间隔下限口径）。探针 t868（拉杆快速循环 + 60Hz 帧驱动冷却递减镜像 + 防抖窗内双发拦截 + 库存对账），阴性轮（常量回 2.0f）红。t814(e)/t856(b) 冷却两向断言随新值更新仍绿（review25 #8 的值→0 钉死不动）。矩阵 266→267；① 后补提交后 269 维持。
**t869** 红石高频/无限电路复刻：用户称上版本有、本版没了（回归定位：git log 搜红石时钟/闪频/无稳态相关；重点疑 t812 转辙器 bit7 通电记忆或 review 批信号节流把快速翻转吞了）。
✅✅（0cfb780）回归考古：v1 电网从未支持*合法*无稳态——用户记忆的「上版本」= t740 前（3686e27，08-21）任意邻粉回灌基座的振荡（装饰环误闪与有意时钟同源）；t740 为修「灯闪」整体豁免火把斜下 4 格粉 → 时钟 / 粉输入 NOT 门一并哑火。t812 bit7 / t689 沿检测 / t707 BFS 均排除（各自升降沿对称）。修复 = MC **形状输出**语义（Core 新单一权威 redstoneDustPowersNeighbor，连接位反推开放端）：粉终止 / 拐入基座 → 供能（时钟恢复：石块+立顶火把+两格 stub 自持振荡 ~2 tick 周期）；贯穿直线贴基座而过 / 单格 dot → 不供能（t740 反闪烁场景保持稳定，P4/P14 探针原样绿）。探针 t869 三段（振荡翻转计数 / 锁存 NOT 门 / 贯穿稳定对照），阴性轮（豁免回填）恰 t869 红。已知取舍：运行中时钟每红石 tick 重写态（10Hz 持续 mesh 重建，MC 同款；无 burnout）。矩阵 267→268。
**t870** 红石粉中键复制图标返修（t815 未愈）：用户仍见非红石粉图标——核 pick-block 拿到的 item id 是否真是红石粉物品（可能复制成别的物品），不只图标源。
✅✅（a495a74）根因确在 id 非图标源：pickBlock 直写**方块形态** 130 进 hotbar → 图标走方块段图集瓦片（连接形随电力态变）且与红石 tab / 材料段 0x224 条目 id 失配——t815 只修了 130 的渲染源没修「该给什么」。修 = pickItemIdForBlock 单一权威（130→0x224 与 dropId 同源；其余方块恒自身，红石矿石 pick 给矿石本体）。探针 t870（映射直调行为级 + pickBlock 路由源码钉 t889/a3 先例），阴性轮（映射中和）红。矩阵 268→269。
**t871** 发射器 TNT 弹出距离：**出现在发射口前一格即可**（弹出速度调小或直接放置在邻格，t856 弹太远）。
✅✅（e7b1610）kDispenserTntPopSpeed 4.0→1.6（摩擦积分连续极限位移 = v/摩擦率 = 0.4 格 → 落定 ≈x0+1.91 恒在发射面邻格内；旧 4.0 飞 ~1 格落到第二格）。t856(a) 探针加落定格断言（生产帧率 15.6ms 细步驱动 1.05s 后 floor==x0+1）+ 方向钉阈值随新初速下调；粗 0.25s 单步欧拉驱动换细步循环（单步一步跳 v·dt=0.4 格失真，生产永不见）。阴性轮（4.0 回填）落 28.5=第二格红。矩阵 269 PASS 不变（原探针就地加固）。

### 🅲 附魔台与铁砧（t872-t875）⚠️ t874/t875 最高优先（用户阻塞七八版本）
**t872** 附魔台书本开合：玩家**靠近 4 格内才敞开**，否则合拢；合拢态显示**书本封面**（demo 包 book.png 封面区，t796 翻页几何补合拢态）。
**t873** 书架文字流实机二查（t823 未愈升级）：用户在**新版仍不见**文字流 → 按真 bug 处理：加运行期自检日志（tableModel 计数/pairs 数/发射率进 voxelsandbox.log），qml.exe 挂真场景实测，与 t874 同批实机定位。✅✅ 已完成（f2b9a4c+fix(t873)）：三级实证数据链完好（矩阵真链探针 260 PASS + qml.exe 像素级 + 主 exe 注入日志），真断点=**#Rectangle 内建面片基尺寸 100×100 单位**（实测 0.07 scale → 7.005 格宽）——字形一直渲染成 5.5–8.5 格巨型白幕（t765「像爆炸」/t797 缩小两轮无效的真因），修=scl÷100 + 尺度按像素可读重标定 0.18–0.28 格；详见 docs/test-reports/t873-glyphflow-repro2.md（含修复后仍不可见的候选解释 × 验证开关表）。
**t874** 铁砧附魔丢失根治（**七八版本顽疾，最高优先**）：用户「放入铁砧附魔直接没了，元数据丢失还是怎么搞的」。方法论升级：① qml.exe 真实 AnvilUI.qml 全路径探针（t792 47 项 + t822 两轮探针都过但用户实测丢 → 探针与实机必有一环不同）；② 对比探针 Hotbar 桩 vs 真 Hotbar vs QML 信号路径逐环差异；③ 新版本戳 + 定向复现文档让用户分层数据（放入瞬间光晕还在吗/取出后 tooltip 还有吗）；④ 怀疑面：特定放入入口（探针只测从 hotbar 直放）、物品类别分支、存档往返后再放入。修复前禁止关单。✅✅ 根因 = C++ Q_INVOKABLE 返回的 QVariantList 在 QML 侧是序列对象（Array.isArray 恒 false）→ 全 UI 数据链 `Array.isArray && length===4` 守卫把它当非法输入兜底清零（放入本地槽瞬间附魔写 [0,0,0,0]）；t792 桩返回真数组/t822 纯 C++ 都不见此环，本轮 QQmlEngine 真面板×真 Hotbar 探针首次复现。修法 = InventoryOps.list4() 五读边界归一 + localWriteSlot 终局防御 + HUD/tooltip 攻击行同根守卫；矩阵 259 PASS/0 FAIL + 阴性验证两轮；复现文档 docs/test-reports/t874-anvil-repro.md（待用户新版口头确认后关单）。commit e307e4c。
**t875** 附魔台拒入已附魔物品：现在**能放入且清洗附魔属性**（严重）——补「已附魔物品拒绝放入附魔台槽」（MC 语义：附魔台不能给已附魔物品再附）；排查「清洗」从哪来（放入路径是否走了 wipe）。✅✅ 清洗源与 t874 同根（同一条序列兜底清零写链，已在 e307e4c 修）；本条补两处拒入门：面板级 slotLeft/slotRight 分派函数原是无门禁旁路（门禁只在内联 TapHandler）→ 门禁收敛进分派函数单一权威；localCanPlace 的 Array.isArray 判定臂改序列兼容遍历（否则带附魔形参被当空 → 拒绝恒不触发）。矩阵探针 t875 七入口拒入 + 全链无清洗 + 正链拒再入全过。commit f163f7d。

### 🅳 资源查看器（t876-t880）
**t876** 羊头颜色重做（t816 返修，用户不满「贴张脸敷衍」）：正确做法 = 羊头盒**采样本体层贴图头区**（自然羊头色），不是叠加脸罩遮盖——毛色 tint 只作用于躯干毛层，头盒换绑本体层纹理源，去脸罩方案。✅✅（df62d8f）MobModel 加 `sheepSkinHead` 属性（仅羊分支读）：true → 几何输出两 subset（0=躯干+腿毛层 / 1=头盒，盒序头最后、盒集不变零顶点差），QML `materials[1]` 换绑头区纹理源——pack 开采 mobTextureSource(3) 合成贴图本体层头区（真脸 box-UV head(0,0)6×6×8），pack 关走**程序 mob_sheep_head.png**（裸肤脸+头顶羊毛帽+吻部暗带，build_mob.py 新增）；毛色 tint 只乘 subset 0，头材质只乘昼夜/红闪（t777「脸=skin 层不 tint」契约贴图化满足）；t816 实色脸罩在 Main.qml/ResourceBrowser 两侧整删（sheepTintDyed/sheepFaceCoverTint/sheepFacePatched 退役，眼显隐回归 sheepWoolFaceActive 单一判据）。默认 false 无 subset = 剪毛态/刷怪笼迷你/其余 mob 零回归。纯视觉（t781 先例不进矩阵）；矩阵 274 PASS 不变。
**t877** 预览上下拖动方向改回：t820 改反了（用户本轮明确上下又反了）→ 恢复原始方向 + **代码注释写死「用户确认方向，勿再改」**。✅✅（fe85e24）恢复 t599 原符号 `userPitch - dy*0.6`（上拖看顶/下拖看底；yaw 不动）——t820「推球面」直觉取反被用户两轮实测判反。userPitch 属性注释 + DragHandler 写点两处钉死「用户确认方向，勿再改」（任何方向直觉推导不得再翻此符号）。纯 QML 一行 + 注释，矩阵不受影响（274 PASS 基线）。
**t878** 狼驯服修复包：① 驯服后**不跟随**（核 taming 后 follow AI 分支）；② 右键坐姿错误——现为身体前倾趴下，应为**后腿折叠坐下、头抬起看玩家**；③ 驯服**爱心粒子**效果不对；④ 创造模式**中键复制不了狼/豹猫生物蛋**（pick-block 对蛋物品，通查全部蛋）；⑤ 离远**不瞬移跟随**。豹猫同查同修。✅✅（d9849e6）①根因=驯服 ~33% 概率多点骨头、成功后补点的每一口都走「已驯服→toggleWolfSit」把狼坐死——骨头对已驯服狼改 **no-op**（MC 1.0 语义；坐/站只走空手右键，t831 已备），Entities 层跟随链 t831 探针本就钉好；②MobModel 新增 `sitPose` 属性（狼/豹猫分支）：躯干绕后髋 +40° 上仰+下沉（臀落地胸抬起）、头高位净 +20/+23° 微仰、后腿前折平收臀下、前腿伸长垂直撑地、坐猫竖尾，替代旧「整模压缩+前倾」（趴下观感根源），QML 眼/项圈/尾 overlay 成对偏移；③爱心改 3 颗 BillboardQuad 像素心（程序 mob_heart.png）相位错开升腾+渐隐+微摆（1.2s 确定性循环，替代静态菱形立方）；④`mobTypeEggId`（改 static）补狼/豹猫/夜行者/燃烬者 4 缺口=覆盖全部 13 蛋，探针钉集合相等+单射（加蛋不跟表即红）；⑤瞬移阈值 24→12（狼/猫同；MC 语义），t831 探针加中距 16 必跳/近距 6 不跳两断言。矩阵 274→275 PASS/0 FAIL。②③ 视觉项待用户目视确认（参数已注释钉死）。
**t879** 活板门双修：① 铁活板门**侧边贴图**资源查看器里还没改对；② 木活板门应为**四镂空造型**（同铁活板门风格中间四孔），现在像木压力板——mesher 盒几何 + 图标同步。✅✅（5bab33c）①根因=运行期图集图标 spec 的 ShapeTrapdoor 泛化 case 薄侧边用 def.sideTile（=镂空板瓦片，查看器/背包两态都走此 spec）→ 改与放置态 t742 per-face 同源分流（铁薄边=iron_block/木薄边=planks，大面保镂空板 drawIsoFace 保孔）。②新瓦片 180 default_wood_trapdoor（木板色四镂空板，孔位与铁 178 一一对应；AtlasTileCount 180→181 图集重生）+ WoodTrapdoor 大面 8→180/薄边 planks + 入 cutout 段（isCutoutTrapX 双活板门）+ mesher per-family sideTile + pack 映射 {180→trapdoor_oak.png} + 离线图标重烘（保孔顶+planks 侧，新增 load_program_face）。图标缓存族 icon5→icon6（画法变更换代；t815/t838 探针 URL 断言同步）。矩阵探针 t879（def 契约+图集宽/孔断言+源码钉 cutout 路由与 sideTile 分流）+ 阴性轮（def 回退全 planks）红→恢复绿。矩阵 275→276 PASS/0 FAIL（t882 一次已知 mob 漂移 flake 复跑清）。
**t880** 3D 模型补全（资源查看器 + 掉落物）：有 3D 模型的物品全部上 3D——**木/铁活板门、火把、台阶、木板楼梯、雪层、草丛、附魔台（带书）**等；掉落物同步 3D 化。附魔台 3D 预览**上面要有书**。红石粉图标改**矿石粉堆形状**（不是一条线）。明确不做：只有平面贴图的（铁轨、木楼梯**掉落物**等按 MC 平贴语义保留——即木楼梯查看器上 3D 但掉落物保持平贴）与工具/材料/装备。✅✅（4831402）新 Renderer 几何 **ItemShapeGeometry**（blockId → 真实异形：活板门合态薄板（薄边 per-family 铁块/木板）、台阶半高、楼梯两盒、雪层 1/8、火把 2/16 细柱（torch 瓦片中央列带 UV 窗）、草丛对角双面交叉片、附魔台 0.75 矮盒；各形状形心居中=图标 y_mid 口径；per-face 图集 UV + 半纹素内缩）。查看器：selectedIsItem3D 家族（含木楼梯 16——查看器楼梯上 3D）→ cubeView 旋转 3D 预览（共用 spinAngle/userPitch 拖拽）；附魔台预览顶叠**静态敞开书**（EnchantBookBox 纸页+镜像两页 V 形，pack 两态布局；材质显式 id 引用防 t610 parent-null TypeError）；大图标分支排除家族。掉落物：isItem3DFamily 3D 分支（scale 0.3 + Mask cutout），**木楼梯掉落物保持 billboard**（MC 平贴语义）、旧火把 billboard 掉落分支退役；附魔台掉落物顶叠迷你书。红石粉 item 图标 flatSpec 166 线→**167 粉堆瓦片**（源码钉）。工具链：Main.qml qmlcachegen AOT 超 PE/COFF 32768 段上限 → target 加 **-Wa,-mbig-obj**。矩阵探针 t880 钉各族顶点数+形心 bounds（开发期真抓到两个居中 bug：模板角点须乘全长、X/Z 须 −0.5 平移）+ dust 源码钉；阴性轮红→绿。矩阵 276→277 PASS/0 FAIL。

### 🅴 钓鱼增强（t881-t886）
**t881** 鱼线**最大长度**限制（超长收线/扯断语义，MC 为 32 格断线）。✅✅ 已完成（a871650）：updateFishing 镜像段查玩家眼位—浮标 3D 距离超 kFishLineMaxLen=32 → 浮标移除 + 钓鱼态复位（无获物/无耐久/无挥手——扯断≠收竿）；检测收口 Game 层（Entities tick 不持玩家真实位）。甩程 ≤19 格永不触发——触发面=玩家走远 / 被钩 mob 拖远。行为级探针（真甩竿 settle→pc.tick 线仍持；珍珠传送拉玩家 ~53 格→首 tick 断线四阴性）；矩阵 269→270。
**t882** 拉拽反馈增强：收杆拉力**随距离增强**（越远越猛）、**不同收杆角度力度不同**；被钩生物**明显飞起/拉向玩家**（用户「没看到生物被拉起来飞」——拉拽速度/抛物弧加大，空中钩起直接拽飞）。✅✅ 已完成（e6da2c3）：useFishingRod 钩住分支调制冲量——速度 = 6+0.35×min(dist,32)（32 格 ≈17 b/s）、上抛 = 2.8+0.18×min(dist,32)（远距峰值 ~1.3 格 + 空中无摩擦 → 明显拽飞）、角度系数 = 0.4+0.6×|cos(视线,线向)|（正对满力/侧背向卸力；look 水平归一防俯仰误伤水靶）；pullMobToward 加 upSpeed 参数、冲量常量全数上移 Game 层（kBobberHookPullUp 退役）。探针：近(~3 格直瞄) vs 远(~12.5 格仰角 12-34° 扫描、每次新猪) 行为级（远位移 >1.25× 近 + 升幅 >0.06/tick）+ 角度支源码钉（yaw 无 WRITE 不可达）；矩阵 270→271。
**t883** 夜行者对鱼钩瞬移：鱼钩对夜行者算投射物 → **瞬移闪避**（同箭链），钩不住夜行者族。✅✅ 已完成（7e707a9）：Flying 钩 mob 扫描在钉定前查候选——夜行者命中 → teleportEntity 强制瞬移（绕冷却，同 t829① 箭链修法）且永不进 Hooked 态（浮标不消耗、穿过原站位继续飞，区别于箭 remove=true）；瞬移失败（被围）仍免疫下帧重试。行为级探针：3 格直瞄落定夜行者 → 24 tick 零钩定 + 位移 >4 格；rig 教训：夜行者 3 格身位 settle 需 16 tick（5 tick 抓到中途下坠位瞄空，首跑红）。矩阵 271→272。
**t884** 咬钩可见性全套（用户「完全看不到水花/上钩动画导致钓不到」）：① 甩竿入水**水花**；② 浮标待机**水面上下微飘动画**（极小幅）；③ 上钩前**水粒子路径**——水面移动轨迹（前端生成、尾端消除，像有东西游向鱼钩）；④ 上钩瞬间**鱼钩被往水下拖**（明显下沉 + 水花加强）——可读的「现在右键」信号。✅✅ 已完成（0e5017c）：① Entities 发 bobberSplashed（Flying→Water 浮定沿、坐标=浮定水面）→ Main.qml 路由 BlockParticles.burstWaterCast（7 颗入水花，幅度档 < 咬钩）；② EntityManager 加 bobberInWaterAt、Game 层镜像 Q_PROPERTY bobberInWater（六条清态路径全复位）→ QML 浮标 ±0.035 sin 微飘（NumberAnimation-on-property 房规模式；纯视觉层偏移不污染 Water 态 blockAt 复查），gated bobberInWater && !hasBite；③ 380ms Timer 调 burstWaterApproach——黄金角螺旋确定性相位（无随机源，§2-K 同口径）出生水色微粒游向浮标、抵达即寿终；④ 咬钩下沉 0.15→0.35 + 咬钩水花 10→14 颗加强；两驱动均加 window.worldRunning 门（ESC 全停）。探针：splash 恰一次精确坐标（静置 3s 不重发/陆上零发）+ bobberInWaterAt 三态分辨 + Game 镜像翻转（视觉半的驱动条件链行为级锁死，视觉半钉 visual-only）。矩阵 272→273。
**t885** 鱼线持久：甩出后**开背包/ESC 不消失**（只要不是玩家扯断或主动收杆就一直在）；核 cancelFishing 的暂停/失焦钩子是否过度清理（按 t889 暂停语义统一后一并处理）。✅✅ 已完成（**主体 t889 已落（250ba94），本任务收尾核验零缺口**）：核验面——① release()（失焦/开背包唯一入口）体无 cancelFishing（t889(e) 源码钉）；② tick 软档（!captured，GUI 开）不清钓鱼态 + updateFishing 照跑（t889(a) 行为级：GUI 开 12 tick 浮标活 + fishing 保持）；③ ESC 硬档（!worldRunning）tickImpl 早退于实体桶、钓鱼态刻意不清（头注释明示），墙钟寿命 setWorldRunning 复跑 deferWallClocks 顺延（t889(b) 行为级：暂停 12 tick 位全冻结 + 复跑续钓）；④ 换世界边界 = Main.qml 三处 ents.clearAll + loadWorld/loadSavedState 显式 cancelFishing（浮标属旧世界，正确收走；即使漏 cancel，clearAll 后 updateFishing 失效自收兜底）；⑤ 死亡重生/换持物收竿 = MC 口径有意语义。无代码改动，纯核验。
**t886** 鱼获反馈：掉落物**从鱼钩处抛物线弹向玩家**（准确可捡）；钓鱼**掉落经验球**（MC 1.0 钓鱼 1-6 XP）。✅✅ 已完成（0444b2f）：获物改 Game 层 C++ 直调 spawnItemThrown（发射器/投掷器先例，QML onFishCaught 转发退役防双生成）——抛物解：目标=玩家中心、T=clamp(0.45+0.055D,0.5,1.4)、vy=Δy/T+½gT（g=28 镜像）→ 精确落点；**弹出点抬升 0.35 出水面上空气格**（浮标浮在顶水格内，原位生成落掉落物浮水分支 vy 清零吞弧线）；经验球 1-6 XP 直调 spawnOrb 落浮标格中心；kFishCatchFlySpeed 4.5 退役（弹速=解算 |v| 远近自适应）。探针：真 pc（三管理器注入）行为级——掉落物出生于抬升点、3s 物理后落玩家中心 2.2 格内、恰一 orb 量∈[1,6]、弹速=公式镜像、耐久 -1（首跑红教训：漏 setHotbar 注入唯 dur=0 暴露）；t836(c) 弹速断言改公式镜像（文件级 fishCatchSpeedMirror 双钉）。矩阵 273→274。

### 🅵 成就 UI 纠偏（t887）
**t887** t840 纠偏二（用户澄清）：① **移除 achQuickDock 常驻悬浮窗**（新 UI 是误解）；② **进度面板右上角 treeMinimap 改为可拖动**（拖出手柄语义，可在面板内/外自由放置，像小地图一样）；保留 hover 光标变化效果（用户认可）。✅✅ 已完成（8d10666）：① achQuickDock 整块删除（Item+MouseArea+Column+clampIntoView、review25 #15 onHeightChanged 钳位、parent resize Connections）+ enterWorld recentNames 重置 + onAchievementUnlocked 名单驱动分支（toast 路由保留）+ 两处头注交叉引用改写记因，全工程零悬垂 id 引用；② treeMinimap 拖手柄语义 = 边缘 10px mmDragGrip 环带为移动手柄（SizeAllCursor 四向箭头），中央跳转区原点击/拖动跳转语义与 PointingHandCursor 保留；首次 drag 写 x/y 断 x/y 锚定绑定 + onXChanged 检测断锚后摘除剩余 anchors.top/right/margins（drag.target 只破 x/y 两绑，锚定线是另一套约束不摘则写位无效——旧 dock 无此环因无锚定线），此后可整块拖出面板边界（treeViewport clip 只裁内容子树不裁本件，progressOverlay 全屏无裁剪），越界钳制 clampIntoOverlay 与 drag.min/max 同口径只越界才写（未拖动时锚定绑定不受扰）；位置会话态不入存档（纯呈现态同旧 dock 口径，重开面板回默认位）。visual/UI-only no probe（t781 先例）；矩阵 277 PASS/0 FAIL 不变 + clean rebuild 零警告 + exe 冒烟 14s 存活日志零字节。待用户目视：拖动手柄手感/可出面板边界/hover 光标。

### 🅶 火焰（t888-t891）
**t888** 火伤节奏调快：生存碰火掉血太慢/存活太久——燃烧伤害间隔或持续致死性对齐 MC（核伤害间隔与熄灭概率参数）。✅✅ 已完成（9b2d914）：`EntityManager::kFire*` 三常量对齐 MC 1.0 基准——`kFireDamageInterval` 1.0→**0.75s**（首拍提前 + 每秒期望伤 1.33HP；MC 火伤 10gt=0.5s 扣半心在整数 HP 粒度下取档的本地化，取舍注释钉死）、`kFireExtinguishChance` 0.15→**0 退役**（MC 常态火不自灭，随机提前灭是无雨时代的降级近似——它让 8s 余焰平均只活 3-4s 且吞 ~40% 火伤脉冲 =「存活太久」另一面；雨灭独立路径不受影响）、`kFireDuration` 8.0 不动（MC fire 字段基准已对，注释钉出处防漂移）。玩家/mob 同链（Game→Entities 复用常量，玩家侧掷熄分支保留为永假）。探针（矩阵 277→278）：常量钉 + 真 pc 站立地火 10.2s 窗**恰 13 拍**（零吞拍）+ 首拍 ∈[0.55,1.15]s + mob ignite() 3s ≥3HP；阴性轮双证（间隔回 1.0 → 10 拍红 / 掷熄回 0.15 → 6 拍红）。
**t889** 暂停语义统一（**先行调研**）：Java 版单机**打开背包/GUI 不暂停世界**（时间/实体/火继续 tick），仅 ESC 暂停。当前工程开背包玩家状态冻结但世界火还在蔓延（用户实测分裂）→ 统一：GUI 开=世界照跑玩家照烧；ESC=全暂停。全工程 tick 门控排查（cancelFishing 失焦清空等钩子按新语义复核）。✅✅ 已完成（250ba94）：两档语义收敛为 `window.worldRunning` 单一权威派生（软档 GUI/聊天/死亡屏=世界照跑+玩家 step 零输入照坠照烧但输入冻结；硬档 ESC 菜单/非 playing=全停——tickImpl 早退实体桶 + WorldClock.running 停表使火/水/昼夜/熔炉全停，根治「ESC 世界火还在烧」旧分裂）；WASD 在 !captured 拦截（step 照跑后必需）；cancelFishing 摘出 release()/tick 分支（t885 鱼线持久），墙钟寿命（箭/浮标/掉落物/经验球）ESC 期 deferWallClocks 三管理器顺延钉「暂停不走」；矩阵 260→261 PASS（t889 探针软/硬行为级+停表行为级+免拾窗翻转+源码钉）+ 阴性轮 3 反转 3 红各自子旗标。门控矩阵详见 docs/test-reports/t889-pause-matrix.md。
**t890** 接触点燃复核：核燃烧方块接触判定是否漏**侧面碰触**（贴着燃烧方块侧壁走不点燃），补侧向接触。✅✅ 已完成（f149041）：确认旧三格判定（foot-1/foot/eye 中心列）结构性漏侧壁——身体 AABB 半宽 0.3 在邻格、中心列不含燃烧格 → 恒不点燃（用户症状本体）。修法 = 仙人掌接触伤害判据族先例（misc 三轮）：**满格 AABB 重叠扫描**（自身 footprint 格 + 正交 4 邻 × 身体 Y 层，XZ 含 kTouchSkin=0.002 容差皮吸收碰撞 snap 的 1e-4 缝；Y 严格主循环 + 站顶支撑面分支同 t716 先例）；**立地火格（Fire）同口径并入**（MC entity-in-fire 本就是 AABB 相交）；岩浆保持中心列流体接触语义不动（防隔墙误燃）。mob 侧（entitymanager）同款扫描（玩家/mob 口径统一）。探针五断言（矩阵 278→279）：侧壁贴走点燃 / 斜对角阴性（AABB 过滤） / 未燃墙阴性 / 站燃块顶回归 / mob 贴墙点燃；阴性轮（容差皮翻 -1 → sideLit 红后还原）。
**t891** 点火源扩展：① **岩浆**：邻可燃方块点燃（核现路径只有焚毁没有点燃）；② **烈焰弹物品**新增：右键发射火球（复用火球投射链）撞击生火，合成配方 + 图标。✅✅ 已完成（00ed81d）：① tickLavaFlow ignite pass 改**单掷双义（点燃优先）**——同盐 0x1A7A 同率 8%/窗掷中 → 先 igniteFlammableAt 单一入口进**燃烧态**（栅格 id 不变 + 面火 overlay + 计时烧毁 + 同态蔓延；湿燃料防火带 / 门整扇湿判收口在入口内三入口同口径）；点燃成功或已在燃 → 本窗焚毁让位（防一格两份消耗的双触发）；入口拒（箱子等非可燃木类 / 湿料）→ 回落既有焚毁（语义节奏零回归）；入口门 isWoodLike ∪ **flammable 全表**（书架/树苗/草丛此前岩浆完全不碰）。review24#3 岩浆泳道探针同步改驱动 ignite→烧尽（轨仍经 setBlock 烧毁路径的 checkRailOnEdit 掉落）。② 烈焰弹 FireChargeId=**0x25C**（染料段上首空号，static_assert 钉位）：右键发射复用燃烬者火球链（kind=Fireball 直线弹道），per-entity `fireballIgnitePct=100` 撞击**必生火**（Emberling 默认 20 不动，spawnFireball 第 3 参默认 20 零改既有调用）；撞击口径打火石同源——命中格可燃 → 直燃进燃烧态，非可燃 → 来向空气格置立地火（旧「命中格正上方置火」顶面启发式退役）；玩家侧火球 fireballShooter=-1 → 玩家命中分支跳过（低头发射不自伤，MC 投射物所有者豁免同语义）；配方燃烬粉+煤炭/木炭+火药 → **3 发**（无序 2×2 两变体）；创造材料 tab（排燃烬粉后）+ 名「烈焰弹」+ MaterialIcon drawFireCharge 自绘（暗壳火核球）+ pack fire_charge.png 映射。矩阵 279→**280** PASS（探针：岩浆干板点燃→烧尽 / 湿书架恒静（强断言防火带——湿木板会落回旧焚毁不混证）/ 书架点燃 / 烈焰弹全链——配方双证+调色板+创造不耗+石墙立地火+板墙直燃+生存消耗+直下发射零自伤（playerTargetable=true 行为级钉豁免））；阴性轮三证（点燃分支禁用→岩浆道全红 / pct=0→无火 / 豁免移除→自击红）。

### 🅷 杂项（t892-t903）
**t892** 静止水位降低：静止水表面比方块顶**低 2 像素（1/8 格）**（MC 语义），连带修耕地透视错乱（用户见「耕地比水还低」）。✅✅（9c6e1cb）`BlockRegistry::waterSurfaceFrac` 单一权威（源 7/8 / 流 (8−min(s,7))/8 / 越界 clamp），四方消费同源：mesher renderTop（柱内判序前置——深水柱内部源格满块、仅暴露顶降 14/16）、浮标浮定、掉落物 restY、船 waterSurfaceY；耕地 15/16 顶随水位降自然高于水面（t639 cap 仅剩柱内格守卫），玩家 eyeInWater 加液面分数判（7/8 上空段不算水下、柱内格仍湿）。船侧两联动修：探测层锚点 y−1→y−0.5（降位后旧锚 floor 到水底层→四向全「岸」清速焊死）+ 新 boatFootprintIceTopAt 冰面同层 snap-up（kShoreProbe 前瞻 + 双层持 + ≥ 自持判据）：冰顶比降位液面高 1/8，船仍可从水面滑上同层冰面；沙岸不受影响（非冰不 snap + 碰岸探测照常清向岸分量）。矩阵 280→281 PASS（探针钉 frac 阶梯 + 物/船浮定高度 + 眼位带/柱语义 + 既有浮标 settle 钉同步 +0.75）；阴性轮：frac(0) 回退 1.0 → 源值钉 + 双浮定高度 + 眼位空段全红。t882 已知 mob flake 复跑清。
**t893** 水动画流向匹配：流水动画方向**固定但流向四向**——按流向选动画 UV 旋转/帧序（四向匹配，静止面无向）。✅✅（292b3a5）mesher 流格（st>0、仅水）按 4 向水邻居 state 梯度（掉落物随流 / 玩家推流同源算法）量化离源主轴四向 → 旋转条带 UV 把「−v 图案移动向」映射到流向（顶面 cv≡−D 四分支；侧壁仅沿墙流向横置动画轴，瀑布/正交流保持竖直下淌 t563 语义）；静止源 / 岩浆 / 孤立流格无向恒等。只重排角点坐标：u 锁列窗、v 锁帧 0 子区 → positionV 翻书零扰动。源码钉探针（ChunkGeometry 需渲染后端，t879/t889 先例）钉梯度门 + 四向旋转路由；矩阵 281→282 PASS。
**t894** 苦力怕模型缩小到 **0.85**（现偏大）。✅✅（290b115）Main.qml stalkerBodyModel 三轴基 0.85（蓄力膨胀相对量 1+inflate·0.5 保持 → 满蓄力 ≈1.28）+ mobModelYOff Stalker 分支腿底补偿 0.90×0.85=0.765（成对契约：漏补偿脚下悬空 0.135）；**碰撞盒不缩**（halfW/halfH 0.30/0.90 走 mobType 表单一权威，移动/近战/爆炸判定不动，注释钉死取舍）；图鉴预览 mobPreviewScale(6) 同源 0.85。源码钉探针（t781 纯视觉先例）钉三轴基 + 成对 Y 补偿 + 预览同源；矩阵 282→283 PASS。
**t895** F3 修复：① 黄绿文字**重叠**（布局分列）；② 内容**严格对齐 MC F3**（逐行核对增删）。✅✅（c5357fd）①根因=FrameProfiler 绿块钉死 y=62+200（注释还写「主块约 12 行」）而主 F3 黄块逐轮长到 ~20 行 → 两 Text 并入同一 Column 自动纵向排布（行数增减永不重叠）；②buildF3Text 重排为 MC 1.0 行结构：标题行带版本（BuildInfo 并入，退役独立 build 行）、fps 行、x/y/z 三行（MC「坐标 // 所在格 // 格内 16 取余」，眼位口径；feet 格移诊断尾段）、f 朝向行（MC 基数码表 +Z→0/−X→1/−Z→2/+X→3 + (yaw / pitch)）、biome 行（删非 MC 的 col 尾缀）、bl/ol 脚下方块光/天光行（World Q_INVOKABLE 真值）；工程诊断尾段空行分隔保留（§2-F 验收铁律）。源码钉探针钉 Column 配对 + 旧绝对定位消失 + 六行 MC 前缀 + 旧格式行已删；矩阵 283→284 PASS。
**t896** 创造拿取/复制语义：左键拿取**默认 1 个**；**中键=复制一整组**（对背包物品中键复制整组，不是 2变4 翻倍）。✅✅（1b9fbea）调色板左键 heldCount=1（t174 满栈上手退役，整组需求走中键）；copyStackToCursor 改复制 maxStackSize(id) 整组（旧 min(count,maxStack) 复制源槽数量 → 2件放回变4 的「翻倍」；元数据保真 + 空槽守卫保留）；数量单一权威 Hotbar::maxStackSize 三处 QML 同读。源码钉（两 TapHandler + copyStackToCursor 旧式必须消失）+ 行为级 VM 数量钉（石/红石 64、桶/附魔书 1）；矩阵 284→285 PASS。
**t897** 羊两修：① 吃草动画**只在脚下草方块触发**；② 静止时**走路动画归零**（现在静止卡住）。✅✅（3f293ec）①sheepEatGrass 目标从「身前 reach 0.7 草丛」收紧为**自身列支撑格 == Grass**（与 t300 长毛链 / 脚步声同列口径；纯草地无草丛也吃、朝向不再参与，kEatReach 退役；消耗 = Grass→Dirt 静默写，长毛仍归 t300 链不越权）；②walkPhase 静止（moveSpeed==0）归零（腿回中立位，旧冻结半步「静止卡住」）+ stepAccum 同步清防鬼脚步。行为级探针：草平台（全场零草丛）羊吃出 Dirt / 石平台 + 草丛诱饵环 24 棵全完好零 Dirt（旧前方逻辑会吃掉一棵 = 双判别面）+ 猪 walkPhase 观测推进后 idle 归 0；矩阵 285→286 PASS（复跑清）。②的腿回正视觉待用户目视。flake 修复（探针鲁棒性，主Agent 复跑抓红）：裸平台羊 RNG 游走坠出 7×7 平台在野草地吃 → 平台 Dirt 计数 0 假红（t836/t882 几何漂移同族）；修 = 两平台各加 9×9 外环 **2 格高**石墙（1 格会被越障自动跳翻出，isJumpObstacle 上方空气即跳）+ 断言面均匀化（栏内全 Grass，事件必落断言内）+ 窗口 2400→3600 帧；矩阵 6 轮 t897 全绿（唯一红 = 已知 t882 mob flake 复跑清），286 PASS 基线保持。
**t898** 上床睡觉动画（用户 8-25 澄清）：右键床入睡 → **玩家模型瞬移到床上躺平、视角/相机移到床位置**（对齐 MC：睡下时人物直接瞬移躺床，非原地睡觉）；躺姿第三人称可见（F5 模型躺平在床上）、视角落床头；醒来/起床瞬移回床边（MC 下床语义）。现阶段视角没变还留在人物身上。✅✅（144fe17）①瞬移躺位：trySleepAt 把 m_pos 钉到**床脚端**（foot 格心 +0.4·head→foot 轴向：1.8 身长恰嵌 2.0 床长、头/脚各距床头/床尾外缘 0.1；Y=床顶 by+1），yaw 转床轴朝床尾（bedPartnerOffset 单一权威解码 state head/foot + 方向），速度/击退/走路摆臂清零 + 摔伤基准重置；②躺姿：新派生门 `sleepLying`（sleeping && 非 Waking；NOTIFY 复用 sleepingChanged、进 Waking 补发翻转）→ Main.qml playerModel 根欧拉瞬切 **+90°X**（+Y 头向旋向模型背后=床头方向、脸朝上仰卧，F5 可见）；③相机：第一人称锚随 position() 到床后，sleepLie ramp 再沿 -look 平移 **1.4 格到床头格心** + 竖直降 1.35 → 眼位=床顶+0.27（床垫枕头高平视床尾）；第一人称手睡觉期隐藏；④出床：新 `leaveBedTeleport`（幂等 m_sleepOutDone 门）扫床头/床脚两格 4 邻首个可站位格（身体两格无碰撞+下方支撑）瞬移回床边地面、全堵 fallback 床顶——Waking 入口（跳清晨/按钮醒）与 cancelSleep（受惊醒/暂停/重生/读档）三路共用（MC 下床语义）；fade-in 渐显期模型已站床边不显「躺地上」。trySleepAt public 化（t814 firePowerTnt 先例，探针直调）。矩阵探针 t898 钉：夜睡瞬移坐标+yaw+躺姿门 / 中断醒出床位 / 白天拒绝零位移副作用；矩阵 286→287 PASS。躺姿观感（F5 模型贴床、相机落床头）待用户目视。
**t899** 玻璃透明度再增实（t838② 返修——用户仍觉得不够透，再增透明度/降低边缘框感，具体度以视觉迭代为准，注释钉参数与迭代史）。✅✅（fa61cb3）两条正交轴分开调并同批落地：①**透明度轴**——glassChunkComp 材质 opacity 0.45→**0.30**（整面均匀乘子，透视感显著增）；②**框感轴**——default_glass.png 棱环自 t838② 的 2px 双色环（外深 ~120 + 内过渡 ~60）收回**单圈 1px 中等棱**（delta~65，内圈回底色；实测边棱 (142,162,182) / 面心 (214,226,234)）。迭代史双钉：build_glass.py 文件头 + Main.qml 材质旁注释（t405 初版 0.45+1px 浅棱 → t838② 增实 2px 双环 → t899 用户反转「整体不够透、框感重」→ 增透+软化）。default_glass.png + atlas.png 离线烘焙重生（派生资产同步铁律）。纯视觉无探针（t781 先例）；矩阵 287 PASS 不变。观感待用户目视。
**t900** 垃圾桶语义终版（用户 8-25 定稿）：**普通左键=清光标持有**（t839 语义保持）+ **shift+左键=清空整个背包**。✅✅（31ae7b0）定稿原话钉进 Inventory.qml 销毁槽处理器注释。普通左键两档不动（光标持有→整组销毁 / 空手→选中槽单格）；新 shift+左键分支清**整个背包**（hotbar 9 setStack + main 27 mainSetStack + 光标 heldBlock 归零，创造/生存同语义）。事件源 TapHandler→**MouseArea**（TapHandler 不分辨修饰键，t700 教训；仅收左键——右键仍传播归 root 右键 TapHandler，t79/t138 语义保持）。矩阵探针 t900 双腿：行为级（填满 hotbar+main+光标 → 重放 QML shift 支同序清空序列 → 全槽读零残留）+ 源码钉（destroyWrap 块锚定：MouseArea 修饰分流 + 9/27/光标三清 + 两档保留 + TapHandler 元素形态退役）；矩阵 287→288 PASS。
**t901** 画作背面返修（t837 未愈）：背面仍全透明——画 quad 双面可见或背面显示画框木板。✅✅（b8ed1c1）根因 = 画面 BillboardQuad 默认背面剔除 → 墙后侧（玻璃墙 / 透视支撑后）看画 quad 被剔 = 零像素。修法（推荐案）= **第二张反向法线 quad**（绕 Y +180°）贴木板背板：qrc default_wood.png（= 图集 tile 8 planks 同源文件；pack per-tile 文件不暴露给 QML → pack 态背面保持程序木板，同画族程序回退先例）、baseColor 0.72 压暗（背光面语义）。两 quad 法线相反各自背面剔除 → 任一像素至多一张朝相机 = 无共面 z-fight；背板再向墙侧收 1/64（离墙面 3/64）双保险；XY 中心 / scale 与正面同。轻量源码钉探针（t781/t893 先例）锚 paintingDelegate 块三件依序（反向欧拉→木板贴图→压暗）；矩阵 288→289 PASS。**实机玻璃墙后看画待用户目视确认**。
**t902** 耕地 item 图标：两面都是耕地 → 应**只顶面耕地、其余面泥土**。✅✅（058ea8f）根因 = Farmland def.frontTile 字段被 mesher 复用为**湿态顶面瓦片 27**（Farmland 无 -Z 前面语义，字段唯一消费点 = tileFor 湿态顶面）→ ShapeFull 泛化 addBox(topT,sideT,frontT) 把它喂给图标**左前面**（renderAtlasIcon 左前 0.80 面贴 frontTile）→ 顶=干耕 + 左前=湿耕 + 右=泥 = 用户「两面都是耕地」。修法 = atlasIconSpecForBlock 显式 Farmland case 钉 side/front=sideT（= def.sideTile = dirt 单一权威）；pack-off 态 legacy qrc icon_farmland.png 本就正确（像素采样核实顶=干耕/侧=泥，不受影响）；消费面 = pack-on 重渲 + 资源查看器大图标。缓存族 **icon6→icon7** 换代（陈旧落盘耕地图标重生）。探针 t902：def 字段复用契约钉（top=26/side=2/front=27——复用事实使泛化路径对耕地必错）+ case 源码钉（先于泛化）；t815/t838 URL 族断言同步 icon7；矩阵 289→290 PASS。图标观感待用户目视。
**t903** 草丛放置收紧：**只能放草方块**（泥土也不行，对齐 MC；t847 plantGroundPoint 收紧 + 失撑链同口径）。✅✅（a7747d7）①放置面：`plantGroundBlock` 单一权威 TallGrass 地面集「泥土/草方块」→**仅草方块**（放置预检经谓词自动收紧；花 / 蘑菇 / 枯灌木地面集不动——只收紧草丛防误伤）；②失撑面同口径：`checkFlowerMushroomOnEdit` 除 Air 破坏（t507 全族）外，**草丛置换面**收口——支撑被置换为任意非草面（plantGroundBlock 判）且正上方是草丛 → 清 Air + 掉 dropId；羊吃草路径（t897 Grass→Dirt 走 setWaterSilent）给该入口**补挂本钩子**（审查修 L6 给 checkRailOnEdit 补挂同先例：非族格单次 blockAt 早退，流体批量热路径可承受）；花 / 蘑菇置换面口径保留（t507 族决策，只草丛 carve-out，注释钉死）。探针 t903 三腿：setWaterSilent 羊吃路径掉 / setBlockSilent 锄地置换掉 / 花换耕地阴性不掉；t847 真值表探针同步（TallGrass,Dirt → false）。矩阵 290→291 PASS（连跑两轮全绿）。

### 📎 R19.15 范围与顺序
t861-t903（43 项）。**建议顺序：t874/t875 铁砧+附魔台顽疾最高优先（用户阻塞）→ t873 书架实机二查（同批）→ t889 暂停语义调研（t888/t890/t885 前置）→ t862-t867 矿车物理组（玩法阻塞）→ t868-t871 红石组 → t881-t886 钓鱼增强组 → t876-t880 资源查看器组 → t887 成就纠偏 → t888-t891 火焰组 → t892-t903 杂项**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。实机复现类（t873/t874/t875/t869/t870/t901）产出须用户在新版本戳 exe 上口头确认后关单。

## R19.16 用户实测复盘批四 + 性能调研（t904-t932，29 项；2026-08-27 立项）

**立项背景**：R19.15+R19.14 全闭后第四轮实测。两条主线：① **性能退化**（用户 F3 实测 18FPS/55.6ms 帧、main\*46.6ms 但 sim 仅 11.18ms、**residual 32.8ms 未插桩**、mob phys **10.26ms/帧**（14 活体）、world 桶 ice **2.5ms/帧**、`threads: 0/0 (sync meshing)` 疑异步 mesh 退化、kill @e 后仍卡、苦力怕仅炸两次——用户点名**先性能调研**）；② 矿车密闭挤压飞出/推车脱轨/V 形动力永动失败等玩法阻塞批。

**F3 快照存档（用户 2026-08-27 15:30 @ 6b1405d，160×160×128，visible 36/100 chunks，mesh culled terrain 1.43M verts / 715k tris，draw-calls 134 / verts drawn 859k / passes 1 / render 0.7ms）**：
- `frame ms/f: main*46.6 render 4.6 qmlSync 2.7 residual 32.8`（frame>max(main,render)；residual=未插桩/等渲染——**32.8ms 去向是第一谜题**）
- `tick ms/f: env 0.00 item 0.00 xp 0.00 boat 0.00 mob 10.27 pick 0.00 phys 0.90 ray 0.00`（mob 桶独大）
- `mob sub ms/f: ai 0.00 phys 10.26 hostile 0.00 spawn 0.00 loop 10.27`（phys 子桶吃满——14 活体 ~0.73ms/只）
- `win ms: sim11.18 mesh0.00(0reb) world2.59 bp0.00 [ice2.5 其余全 0]`（ice 2.5ms 与增量索引修法相悖——第二疑点）
- `threads: 0/0 (sync meshing)`（异步 mesh 线程池状态——第三疑点）

### 🅰 性能调研（t904-t906）⚠️ 用户点名先跑
**t904** 帧耗时 residual 32.8ms 归因：frame 与 main/render 桶之间 32.8ms 未插桩——补插桩（Present/vsync 等待、QQuickWindow sync 未计入 qmlSync 的部分、事件循环、帧首尾空闲）逐项归因；产出根因报告（docs/test-reports/）+ 修复或登记。kill @e 后仍卡说明大头不在实体数。
  > **调研完成，根因判定待复测**（9550a0d，报告 docs/test-reports/perf-investigation-2026-08-28.md）：插桩落地——
  > main.cpp 四段 hook + F3 新 `frame2` 行（evA=QML 绑定/其它 Timer/空闲、waitSync=GUI 等渲染同步屏障、
  > idleB=sync 后到 swap），构造上 residual≈evA+waitSync+idleB 全归因；菜单态验证 16.0=14.9+0.5+0.6 闭合。
  > 旧快照可推硬事实：所有 ms/f 桶分母=tick 数，main\*46.6 ⇒ **tick 只 ~21 次/s（16ms 定时器被饿到 1/3）**、
  > sim 聚合仅 ~235ms/s ⇒ 帧率非 sim 聚合 bound，~720ms/s 花在 tick/sync 之外。根因判定按报告判读树待用户复测 frame2。
**t905** mob phys 10.26ms/帧解剖：14 活体 ~0.73ms/只异常——逐 mob 计时插桩定位到函数级；重点疑面（均为 R19.14/R19.15 新面）：supportTopYAt/collisionAABBsInto 脚位格豁免循环（review26 #1）每帧全盒表重扫、t863 derailed 每帧支撑复探、t866 checkCartEnvironment 帧级全车扫、t891 岩浆 ignite pass、t897 静止 walkPhase 归零链；冰 ice 2.5ms/帧同查（增量索引 c282bc0 之后 ice 扫描为何还贵）。
  > **ice 部分 ✅✅ 修复**（b070a28）：根因=tickIceFreeze 每 5s 窗遍历全水格索引逐格调 biomeAt（5 条 4 阶 fBm
  > ~20 次 Perlin ≈206ns/格，c282bc0 只消了全图扫描没消每格判定）→ biomeAt 列级 memo（矩阵实测 4096 列
  > 冷 843µs/暖 6µs=**128×**；按快照反推 6 万水格 → ice 窗 12.5ms→预期 <1ms，F3 修后数字待复测）；
  > 契约探针入矩阵（冷/暖一致 + 换 seed 失效 + 跨实例一致）325→326 PASS。
  > **mob 部分调研完成（插桩落地），热点定位待复测**：ai 0.00 已排除 aiTick 族疑面（t890 火扫/仙人掌/
  > wander 全在节流块）；新 [head/tail/ltail] 拆分 + st[R/F/V/D] 状态直方图——头号假设=ltail（emit
  > entitiesChanged 的 47 槽 delegate 扇出，10.26×21tick≈215ms/s 与 3tick 一次 ~30ms 扇出均摊自洽），
  > 次假设=st F 高的 resting↔下落振荡；复测定音（判读树见报告）。
**t906** 同步 meshing 疑点：`threads: 0/0 (sync meshing)`——异步 mesh 线程池是否退化/未启用（chunkmanager 线程池状态、构建路径分支）；1.43M 顶点若同步重建直接吃主线程。核实 + 修或登记。
  > **调研完成（核实非退化，无需修复）**：src/ 全树零线程原语（QThreadPool/QThread/QtConcurrent 均无）——
  > 不存在可退化的池；ChunkManager 是纯容器，mesh 由 ChunkGeometry::onWorldChanged 同步直连槽驱动
  > （F3 该串是 Main.qml 硬编码事实标签，已补注释钉死）。影响：稳态 0reb（快照 mesh 0.00）非持续掉帧因素；
  > 重建以突发落 GUI（编辑 dirty / sun 量化步进 ~3.3s，t472 视距门控 + t383 精确标脏已限幅）。
  > 异步 meshing 登记为架构级未来工作（chunkgeometry.h 不变量 B 形已为线程化保 move-only）。

### 🅱 矿车与铁轨（t907-t913）⚠️ 玩法阻塞
**t907** 密闭单格 cart-cart 挤压飞穿：全封闭单格空间内矿车互挤，碰撞解析把车**飞出牢笼**（穿实体方块）——实体碰撞改硬约束（解析结果不得写车入实体格；冲量/位移钳制），任何弹射不得越实体墙。 ✅✅（6b62676）三修：① clampShift 轨列探测升级为**轨层 Y**，位移跨格叠「同层闸 + 腰位闸」——宽容扫描容差窗可放行隔层下隧轨（rise 抬面场景），旧版「目标列有轨」即全额放行 → 车写进墙列后 pinCartY 沿同一容差把它钉到墙内/墙后轨（飞出牢笼的写位半边）；② 冲量后速度钳 ±kCartBoostSpeed；③ tickDerailedCart 水平积分**子步化**（≤0.45 格/子步逐格墙检）——dt 尖峰（tick 饿死/拖窗，t904 实测）下弹射车单步欧拉跳过 1 格厚墙（只验落点格）。探针三段：密闭双格轨笼对挤车心永不入实体格+不出笼+分离 ≥0.85；单格地面笼双车不出笼；弹射 4 blocks/s × 0.5s 尖峰 tick 不穿 1 格厚墙（负验证：单步欧拉复现穿墙后复原）。矩阵 326→327。
**t908** 玩家推车向量分解：轨上矿车被玩家身体/推动时**只接受沿轨前后分量**，横向推无效（不脱轨）；pushEmptyCart 与玩家碰撞挤推两路都按轨向投影（向量分解语义）。 ✅✅（0082e72）有定向轨（连接位非 0/孤轨轴偏好位）上的车：合成推向量（away+wish+dir）先投影轨向——全部连接臂上 |sel·d| 最大值 < kCartPushProjMin(0.3，恰高于 dir 兜底项 0.25 → 纯横推必拦) → no-op；死端推离方向改**轨轴符号向**（旧合成主轴斜推时落垂直向 = 侧向脱轨面）。绝对值口径保「沿轨后退」合法（t809 拐角推穿保留）；state=0 孤轨与地面车不分解（review26 #14 / t863④ 既有语义）。探针四段：中格横推零位移/纵推沿轨贴面/死端横推不动/死端沿轴外向推离保留。矩阵 327→328。
**t909** V 形动力永动三修：① V 底两格激活动力轨应**无限往复**（底部加速→上坡减速→顶部反溜下滑；现加速上坡后可能直接停驻不反溜——t863 反溜链在连续 V 坡失效场景）；② 初始放在 V 半山腰应**往下坡方向运动**（现静止不动）；③ 下坡初速与加速度**加大**（现五六格都到不了最大速度）。 ✅✅（0f130a6）①③根因 = 空车坡道无重力：新 kCartSlopeGravity = g·sin45°（28×0.7071≈19.8 blocks/s²）——上坡减速 v²=v0²−2ad → 12.8 boost 4.1 格内失速（旧摩擦 2/s 走 6.4 格 = 短 V 坡冲顶飞出/停驻不反溜）；下坡加速 v²=2ad → 起步 ~2 格到 10（旧恒速不加速）。②静置闸坡道让位：行进/背向侧邻轨低一格 → 翻向起步溜（kCartSlopeKick 1.0 > 反溜 kick 0.5 =「初速加大」），平地静止车不动；与 t863① 反溜链互补闭合全部停驻面（坡中失速→反溜、平顶停驻→自溜翻向）。被骑路径（lerp 供能）不动；t864 挡路车钉随语义更新（坡顶死端停驻车自然滚落）。探针：V rig（底 2 动力轨直供+两侧 4 格爬升）半山腰起步/首降 4.5 格内达 9.0+/24s ≥6 次反转/不出 rig（负验证：重力归零 → 零反转+车飞出）。矩阵 328→329。
**t910** 动力铁轨充能沿坡传播：上坡的动力铁轨被红石激活应传播到**上下坡固定距离**的动力铁轨（现只有平地传远）——充能扩散沿轨走向含升降。 ✅✅（f483d5d）根因 = 链 BFS 钉同 y（「信号不爬坡」）——改每向 railProbeDelta 三高探针解邻轨层差（same 优先，railConnections/pickTrackStep 同一权威）步进 c.y+delta；两处波前入脏集（kAx2/kAx3）扩 ±1 层（降沿沿坡收缩不卡首格）。探针：侧邻直供种子 + 6 根逐格 +1 爬坡链 vs 同长平链——两链全亮 6/6（传播距离与平地一致）+ 拆源对称全灭；探针源放**种子侧邻**（脚下源被拆时种子轨同步失撑掉落 t733——bring-up 踩坑）（负验证：同 y BFS 坡链 1/6）。矩阵 329→330。
**t911** 铁轨贴仙人掌破坏：仙人掌旁放铁轨 → **仙人掌被破坏掉落**（MC 语义：铁轨非仙人掌合法邻面；自动下矿车系统前提——已验证仙人掌能撞掉矿车）。 ✅✅（6fb91c1）checkCactusOnEdit ④ 非空邻面反应规则天然覆盖铁轨（无需白名单）——显式探针钉场景防回归；真修 = **整柱口径**：命中在柱中段（铁轨贴 2+ 高仙人掌上层）时旧版 dropCactusColumn 从命中层起只清上半（下半悬空残留）——先下探柱基再整柱坍落。探针：贴上层放铁轨 → 两格全 Air+每格各一次掉落+铁轨留存；贴 1 高基座同坍；距 2 格阴性不触发（负验证：命中层起清 → 下半残留）。矩阵 330→331。
**t912** 发射器发射矿车：发射矿车物品 → 在发射口前邻格**放置矿车实体**（同 TNT 弹出语义）；邻格是铁轨则**对齐轨向**。 ✅✅（bb0a2bc）dispenseFromDispenser 新增 MinecartId 分支（置于投掷器兜底后——dropper 只投不放实体，MC 口径）：邻格 spawnCart——轨格 → 轨上模式 + t708② 连接位定向（yaw 轴向取轨向，零特判）；净空非轨 → t734 地面静止模式；堵口 → review26 #24 门（TNT 分支的排出口占用检查提为共用 spoutCellClear lambda）降级普通掉落物弹出；库存照扣。探针：轨口车 y=kCartRideH+yaw 270（+X 轴向）/地面口车 y=kCartGroundH/堵口无实体+掉落物+1（负验证：分支禁用 → 零车放置）。矩阵 331→332。
**t913** 发射器冷却对齐 MC：无限红石电路持续闪烁但只射出几根箭（0.5s 仍吞沿）——**调研 MC 1.0 发射器实际延迟**（约 4 game ticks = 0.2s 一带，以调研值为准重钉常量与探针）。 ✅✅（42f57f6）调研：实网核验被反爬 403 拦 → 以 dev-plan 所钉 MC 语义为准（Minecraft Wiki Dispenser 行为节：重触发间隔 4 game ticks @ 20Hz = **0.2s**；同沿只触发一次=上升沿触发——t689 基线集既有语义正交不动）。kDispenserCooldown 0.5→0.2：≥4 game tick 周期时钟（含中继器最短时钟）逐沿发射。探针钉常量窗 (0.1, 0.3]：首沿恰 1 发 / 0.112s 新沿拦 / 0.24s 新沿必再发（负验证：回 0.5 → 0.24s 沿被吞 = 用户症状复现）；t814(e)/t856(b) 口径保持。矩阵 332→333。

### 🅲 附魔与铁砧（t914-t919）
**t914** t872 书本开合返修：书本**恒开未合**（用户「完全没有」）——靠近 4 格内敞开/远离合拢 + 合拢显封面。探针与实机再不同步则实机定位（渲染面/条件面第 N 例）。 ✅✅（85fec2c）条件面真接进几何/材质（历次「探针过实机没过」的同类根因）：faceTimer（10Hz 已有玩家位读取，不加 Timer）并入 3D 距离判定 + 迟滞带（≤4.0 敞开 / >4.4 合拢，防阈值徘徊抖动）——左页 -22°→-178° 翻扣过书脊落右页上方（240ms InOutQuad Behavior；2° 残角防两薄盒共面 z-fight、近书脊交叠区被书脊条遮住）、右页 +22°→+1°；左页 piece 4 纸页镜像→**piece 0 封面**（合拢过程与合拢态上面读作封面——qrc 布局 0 两面同封面矩形 / pack 布局 1 上=左封下=右封，「前封随翻动翻上来」同真实书；敞开恢复 t796② 纸页定稿）；翻页片 flipPivot.visible + flutter/大摆两动画 running 全并 bookOpen 门（合着的书不翻页，世界锚定暂停门保留在前）。faceTimer 恒跑（合拢态仍需探测接近）。review27-13 源码钉探针同步契约（flutter 串追加 bookOpen）。待用户目视确认。
**t915** t873 文字流返修三面：① **常驻**——平时看附魔台就有文字飘入（不是附魔完成时才触发；用户看到的完成时小透明方块疑是既有别的粒子效果）；② **丝滑提速**（现在很慢）；③ **字形态**——用户看到的是「小透明方块」不是文字：字形贴图/渲染疑似实机未生效（t873 修的 scale 百倍在探针可测、实机观感仍是方块——查真机渲染路径与贴图绑定）。 ✅✅（c5a74cb）①常驻链路 t797 起已在（active=playing 不依赖 UI 开）；用户看到的「完成时小透明方块」= **EnchantRunes 彩色小立方**（t649/t697 另一路常驻氛围流，两套并存各司其职——头注释互相钉开防混淆）。②提速：漂速 0.9→2.6 格/s（一程 ~1s）、寿命钳 1.8-4.0→0.7-1.5s、每书架 0.22→0.55 字/s（满 15 书架 ~8 字/s 持续流）、池 36→48、单轮上限 4→5、弧高 0.20→0.12。③字形态（渲染侧根因 = 渐隐律 × 笔画宽复合）：图集笔画 1px→2px + 点饰 2×2→3×3（墨量 498→899px；点阵右沿 13→12 令 2px 笔画不出格、2px 防渗边圈断言不松——build_glyph_sprites.py 重生成）、面片 0.18-0.28→0.26-0.40 格、渐隐律 (1-k) 全程衰减（中段 alpha~0.5）→前 15% 淡入 × 前 70% 全显 × 末 30% 线性归零（飞行主体可辨「是字」、到达书心仍透明回收）。spawn 节拍日志收编 --verbose-glyphs 门（同 review26 #23 口径，生产零日志）。待用户目视确认。
**t916** 附魔台槽位说明文字：UI 内嵌说明——「左槽放工具/武器/书 · 右槽放足青金石即解锁高栏（创造亦须摆满）· 书附魔成附魔书不需要显示」（按用户原话落地；「不需要显示」= 书+青金石合成附魔书路径不显示高栏/说明行，细节以实现时 MC 口径为准并注释钉死）。 ✅✅（0e16284）状态分档说明行（触碰 enchantRev，空态/未满态提示、满态静默）：槽 0 空 →「左槽放工具 / 武器 / 书」；槽 0 有物但无青金石 →「右槽放足青金石即解锁高栏（创造亦须摆满）」（用户原话；书未放青金石**同显**——t795 口径附书同样消耗槽 1 青金石，缺料提示对书路径同样成立，注释钉死）；槽 0 有物 + 已放青金石（含 书+青金石 → 附魔书 路径）→ 整行不显示（「书附魔成附魔书不需要显示」；非书就绪态同理——选项已亮说明行无信息量）；已附魔未取走 → 不显示（绿 flash 已反馈）。待用户目视确认。
**t917** 附魔选项 hover 悬浮窗：三选项悬停显示「锋利? 耐久?」式预告——**只显示一种**附魔、**必定出现**该附魔、等级未知（MC 语义：附魔台预告一条必出附魔、等级模糊）。 ✅✅（4f8487e）诚实预告的前提 = 预告与施放读同一确定性种子：旧 doEnchant 种子掺 Date.now()&0xffff（每次点击结果都变 → 任何静态预告必假）→ 收口 **tierSeed 单一权威**（台位 ^ 物品 ^ 档位 ^ optionReroll 位异或派生，PLAN §2-K 无时钟无随机）；hover 读 tierPreviewName = 同 seed 复算 selectEnchantsPreviewForItem 取**首条** →「必得 <附魔名> ?」紫字悬浮窗（只显一种 / 必定出现=首条就在产物 picks 里 / 等级模糊 ?——MC 1.0 悬停预告口径，书全池路径同适用）；optionReroll 槽 0 换入新物品实例 +1（同物品逗留稳定不闪烁；附魔后产物写回 id/count 不变 → 预告保持当轮直至换件）。itemReady+已解锁即显（攒青金石期间可先看）。源码钉+行为探针（时钟混种绝迹 / 剑 offered 1..30+书抽样 picks 非空可解析）。矩阵 333→334。待用户目视确认。
**t918** 铁砧 shift+左键合成品直入背包：附魔书敲进工具完成后，shift+左键取合成品应**直接进背包**（现无反应被鼠标拿取）——对齐 MC 取出语义（hotbar 优先/main 兜底，满则 fallback 光标）。 ✅✅（9b8bc3d）takeProduct(toInventory) 双路由：普通左键/回车 = 光标（t626② 语义保持——t874 真 QQmlEngine×真 Hotbar 探针四 op 全链复验不回归）；Shift+左键 = **直入背包**（hotbar.addToAny：同 id 无名栈就地合并 → 空槽 hotbar 优先→main，耐久/附魔/实例名随 cap=1 空槽开新全套保真）；预检 = main+hotbar 容量 + 光标兜底位（slotShiftLeftCraft 同口径；带名栈不并保守计容量），装不下 → 零消耗 no-op（t626②「副作用只在探路全过后的落定段」防复制不变量不破）；余量 fallback 光标（MC 取出语义兜底半边）。产物槽 TapHandler 传 window.shiftHeld。源码钉探针 +1（双路由签名/shift 接线/addToAny 落定/预检兜底与零消耗门）。矩阵 334→335。shift 路径待用户目视确认。
**t919** 钻石剑伤害核账：仅火焰附加、显示 +7 伤害，**两刀砍死 20 血僵尸**（7×2=14<20 为何死？）——调查伤害显示口径 vs 实际结算（火焰附加 DoT 补刀？显示含附魔加成结算不含？近战基础值口径？）；产出账目对齐或 bug 修复，公式注释钉死。 ✅✅（ccf8b8e）核账结论：**「火焰附加 DoT 补刀 = 正常，非 bug」**。账目四路全查：① 显示口径正确——displayAttackDamage = round(weaponAttackDamage) = 钻石剑基础 7（tier 4）+ 锐锋 0.5/级；**燃焰不进直伤公式**（MC 1.0 fire-aspect 同样不加攻击面板，输出全在点燃 DoT）→ +7 显示与实战直伤同源同值；② 近战结算与显示同源（attackMob 起点 = weaponAttackDamage，无第二套数值）；③ 火焰附加输出 = 每刀 ignite(级×4s) 点燃 × 1HP/0.75s（kFireDamageInterval，第二刀刷新燃烧窗）→ 两刀 14 直伤 + ≥6 拍火伤 ≥ 20HP → 僵尸在第二刀后数秒内死亡（用户观感「两刀死」）；暴击另路：滞空下落 ×1.5 → 7→11，两记跳劈 22 ≥ 20 独立致死；④ 僵尸（Shambler）满血 kHostileDefaultHealth=20（MC 1.0 口径）。运行时双子探针实证：同 rig 两只 20HP Shambler 各吃两刀 7 直伤——**无火对照存活 6HP / 每刀点燃的孪生死且 mobDied(burned=true)**（无火不死、有火死的对照 = 用户疑问的直接答案）。公式注释钉（attackMob 燃焰段 + EnchantRegistry FireAspect 条目）。矩阵 335→336。

### 🅳 资源查看器（t920-t925）
**t920** 狼/豹猫驯服态查看选项：查看器加**驯服/未驯服**切换（雪傀儡/羊的变体选择先例）；驯服态下再加**站立/趴下（坐）**切换（豹猫同）。 ✅✅（b34db1b）变体面板扩狼（10）/豹猫（11）：第一段 驯服/未驯服 + 第二段仅驯服后 站立/坐下（野生态不可命令坐，MC 1.0 口径）；坐姿走 **MobModel sitPose**（t878② 同一几何源，非浏览器侧复刻）；狼驯服态视觉 = t831 红项圈 overlay（站/坐颈位成对契约随切）+ 尾根/眼位随坐姿成对偏移；豹猫驯服 = 程序家猫贴图 mob_cat_black（游戏内 3 变体随机，图鉴取代表；demo 包无驯服猫 PNG——Main.qml「驯服猫恒程序贴图」同口径）+ 眼恒显（程序猫无脸纹）；未驯服无后缀=常规形态名；蛋路径不设变体（同羊）。待用户目视确认。
**t921** 拖拽方向随旋转面反转修复：物品**自动旋转到背面时上下拖拽感知反转**（用户定位的规律）——拖拽 pitch 增量按当前展示面相位（yaw 过 ±90°）翻转补偿，或拖拽时暂停自转。 ✅✅（7d26211）补偿实现：pitch 增量乘 sign(cos(显示yaw = spinAngle−35))——自转过 ±90°（背面朝相机）时俯仰铰链轴屏幕投影反号 = 「上下反了」根因；**正面相位（cos≥0）符号与 t877 用户定稿逐字节不变**（注释钉死与 t877 契约的关系：不是改方向，是把「上拖看顶」的屏幕感知扩到全程两态同向）；水平 yaw 拖拽不动；拖拽期自转本就暂停（t599）。待用户目视确认。
**t922** 预览滚轮缩放：鼠标滚轮放大/缩小 + **右下角重置按钮**恢复默认大小。 ✅✅（4e5fb23）WheelHandler 滚轮 ×1.1/档钳 [0.5,3.0]，由 PerspectiveCamera 距离承载（z = 3.2/zoom——方块/床/异形/生物全 3D 分支零改动共享）；右下角「重置」钮仅 3D 预览且已缩放时显示（回 1.0 自隐）；大图标态不抢滚轮（左侧网格 Flickable 不受影响）；变体面板收窄（-12→-58）让位防叠。待用户目视确认。
**t923** 狼驯服生态包（实机，t878 返修+扩面）：① **跟随返修**（t878 修过逻辑仍不跟随——实机定位）；② **浮水**（狼困水里淹死——mob 主动浮面缺失或狼分支漏，浮水先例已有）；③ **护主战斗**：玩家被 mob 攻击 → 驯服狼主动攻击攻击者（僵尸/骷髅等）；敌对攻击狼 → 狼反击（仇恨传递/协作战系统，豹猫同查）。 ✅✅（244c85c）三面根因+修复：① 跟随分支本身触发正常（t831 探针已钉 tamed&&!sitting 跟随/坐冻结）——用户实测「走了还在水里淹死」根因 = **水困**：陆栖 mob 无主动浮面（浮力仅鱿鱼 t828 有），狼缓沉贴塘底、水平追击被岸壁挡死、15s 呼吸耗尽 1HP/s 溺亡。② 修 = 全陆栖 mob **主动浮面**：头格浸水（与溺水判定同式——「会淹才游」）施 kMobSwimBuoyancy(3.0)/kMobSwimRiseMax(1.2) 净浮力 → 水面贴平 bobbing 呼吸恢复；resting 打破自鱿鱼特例泛化（同头格门；浅水跋涉头未没水不破——贴底站立无翻转振荡）；rise 钳制只作用浮力累积（泳跃 vy 直设原样穿过，防跃出速度被夹回）。③ chase 补 aiHostile 同款**越障跳**（墙顶两格空气 + isJumpObstacle 排作物 + t670 水平滑流；野狼追击同受益）+ 水中贴壁**泳跃**（kJumpSpeed 跃出水面登岸，全水位满格引擎的常规掘塘岸顶=水面等高需跃 ~0.8 格）。④ 护主：主人受击注册链 **t480 已有**（近战 3292/骷髅箭 5093/爆炸 4350/夜行者重拳 3831 + 主人攻击 setWolfTarget）；本批补「**敌对攻击狼 → 狼反击**」wolfRetaliateAgainst 共享目标入口，接线两处 mob→mob 活体攻击者伤害点——骷髅箭碰撞滤网 t712 铁傀儡 single 扩**狼**（宠物挡箭，MC 1.0 箭不穿透生物体；其余 mob 仍穿透保生态）+ 燃烬者火球（fireballShooter slot+serial 双查防槽复用误绑）；爆炸伤狼不注册（Stalker 当帧自毁无活体可反击）；**豹猫永不反击**（MC 1.0 猫不攻击怪物，头注释钉死；豹猫只做跟随/浮水同享）。探针+阴性轮：坐宠浮面但 XZ 静止 / 站宠浮面+泳跃上岸逼近主人 / 狼群咬攻击者 / 同入口对驯服猫 no-op / 两接线点源码钉；t828 rig 水池**加盖**（敞顶猪合法浮面呼吸——溺水须被按在水下，MC 语义）。矩阵 336→337。实机三面（跟随/浮水/护主）待用户在新版戳 exe 目视确认后关单。
**t924** 附魔台查看器模型对齐世界：查看器现重建**整格黑曜石且抖动**——对齐世界放置形态（**0.75 格高** + 悬浮书；去看实际放置的附魔台 mesher 设置，勿重建）。 ✅✅（772e1d9）核账：症状（整格黑曜石+抖动）根因 = **review27 #4**——附魔台 94 不在 isPartialBlock → selectedIsCube 对 94 仍 true，满格 BlockCube（六面深色瓦片=「整格黑曜石」）与 ItemShapeGeometry 0.75 矮盒**叠渲 z-fight**（=「抖动」），家族互斥已在 review27 #4 修；悬浮书坐标 review27 #9 已钉三处同源（查看器 etBookNode = 掉落物 dropBookNode：书心 y=+0.46 > 台顶 +0.375、页 ±0.176、0.38×0.03×0.46）——HEAD 已渲世界一致形态（0.75 矮盒与 partialblockgeometry kEnchantTop 同高、侧/顶/底瓦片同 def）。本项交付 = 五面契约**源码钉探针**防回归（查看器家族含 94 / 立方+大图标分支双排除 item3D / 附魔台 case 0.75f / 书坐标两 QML 同值）。矩阵 337→338。待用户目视复核查看器形态。
**t925** 3D 模型扩面第二批（t880 返修扩面）：枯死灌木/小麦作物/木栅栏/云杉栅栏/木门/云杉门/白蘑菇/红蘑菇/蜘蛛网/红石火把/拉杆/按钮/楼梯——**凡右键可放置的方块都应有 3D 模型**（查看器 + 掉落物按 t880 口径分派；明确排除项沿用 t880 清单）。 ✅✅（3f8d2a0）十二族 + 家族补全（圆石墙 60 / 铁门 71 与木栅栏/木门同 case，共 15 id）全走 **mesher 同源几何勿手搓**：栅栏 = 中心柱 + 四向上下双档**满连展示形态**（盒区常数逐项同 fence case：柱 [0.3,0.7]²×1、档 y[6/16,9/16]/[12/16,15/16]，t801 视觉 1 格高）；门 = 合态 facing+X 3/16 薄板（[0.8125,1] mesher 盒区）+ **t674 同族基材薄边**（木→planks/云杉→spruce_planks/铁→iron_block，addShapeBox 新增 per-face 瓦片覆写位掩码承载）；拉杆/按钮 = **mechBoxes 单一几何源**（与 World mesher t662 / raycastAABBs 同一盒集——零第二套数值；拉杆底座 cobble+棍 planks 同 mesher 贴图分流）；cross 扩面（枯灌木 43/小麦 25 **成熟金黄穗瓦片**=调色板「图标显成熟态」同口径/红蘑菇 48/白蘑菇 115/蛛网 102/红石火把 129）走交叉双面片。两侧家族表同步（查看器 selectedIsItem3D ∪ 掉落物 isItem3DFamily 各 +15 id，源码钉互指防单侧漂移）；t880 排除清单沿用（铁轨平贴族 + 楼梯三族掉落物 billboard）；查看器小体型族放大（机关 ×1.8 / 蘑菇·红石火把剪影 ×1.5 / 栅栏 ×1.2，火把 ×1.6 先例）。探针逐形状顶点+包络（栅栏 216 / 门 24 且 xMin≥0.31 薄板非对称 / 拉杆 72 且 yMax≈0 / 按钮负 yMax=-0.375 贴地小凸块 / cross 16）+ 家族表同步源码钉。矩阵 338→339。待用户目视确认。

### 🅴 钓鱼（t926-t927）
**t926** 咬钩信号重做：① 浮标待机悬浮幅度**缩小**（现幅度大难判咬钩）；② **鱼粒子**=复用现有收缩粒子链——咬钩前水粒子在鱼钩**随机方位、随机距离 ≤4 格**出现、**游向鱼钩**，触钩瞬间**鱼钩大幅下沉 + 水花**，随后 **~1s 判定窗**右键收杆（可读的「现在收」信号；替代/整合 t884 现行下沉 0.35 方案）。 ✅✅（6596907）四要素全落：① 待机微飘 0.035→**0.018**（缩幅防喧宾夺主，与咬钩下沉的对比度即修面）；② 鱼粒子域取代 t884 近距涟漪（0.9..1.32）→ **随机方位（黄金角螺旋）+ 随机距离 0.7..4.0 格（≤4 上限）**，游速按距离解算 1.2+0.8×rad → 任意出生 ≤1.1s 抵钩聚合、抵达即寿终（确定性相位驱动，PLAN §2-K；t884 收缩粒子链模式保留）；③ 触钩下沉 0.35→**0.7** + 水花加强（14→16 颗 / 起跳 4.4 / 横向 1.9）；④ 判定窗 kBobberBiteWindowSec 0.5→**1.0s**（用户口径覆写 MC 1.0 ~0.5s，注释钉死取舍）。探针 P-t926：窗长行为级（bit→escaped 实测 1.00s±0.1 + 窗内 hasBite 恒 true / 逃走翻 false）+ 三视觉面源码钉（t887b/t924 手法）+ 两轮阴性（下沉翻回 0.35 → 恰 t926 FAIL；窗口翻回 0.5 → t926 FAIL——t836 到期断言单侧设计，缩短只被新探针抓）；t836 镜像 kMirrorBiteWindow 同步 1.0（P18 双钉）。矩阵 339→340。待用户目视：鱼粒子 4 格逼近 + 触钩大幅下沉水花。
**t927** 拉拽生物飞天返修（t882 参数返修）：空中右键收杆**看不到生物飞起**——拉拽高度**至少到玩家高度**，按 mob 重量微调（重型少抬、轻型拽过头顶）。 ✅✅（fcb345e）根因 = t882 冲量式上抛 2.8+0.18d 的峰值 vy²/56（32 格远也只 ~1.3 格）在玩家居高时够不着玩家高度。修 = **落差解算式抛物**：Δh = 玩家眼位（m_pos.y+m_eyeHeight）− mobY + **kFishHookLiftOverhead 0.6**（轻型过头），下限 **kFishHookLiftFloorDh 1.3**（同高保底小弧），vy = √(2·**kFishHookLiftGravity 28**·Δh)（镜像 Entities kGravity，P18 双钉）；重量口径 = mobType halfH 质量代理：≥ **kFishHookHeavyHalfH 1.0** 判重型族（铁傀儡 1.20 / 夜行者 1.40）→ Δh × **kFishHookHeavyLift 0.55** 折扣（猪 0.45 / 狼 0.45 / 骨族 0.90 不折）；角度调制与 -5 耐久不变；kFishHookLiftBase/LiftGain 退役留碑。探针 P-t927：玩家 Y+6 石柱高台收杆地面猪 → 峰值 ≥ 玩家脚位 −1（解算目标眼位+0.6，裕量 3 格）；铁傀儡峰 > 起点+2 且 < 猪峰 −1（折扣可观察）；t882 探针同步（源码钉改锁 √ 解算 + 过头余量 + 重型折扣，近/远位移与角度支不回归）。阴性两轮：折扣移除 → 恰 t927 FAIL；解算常数 2.0→1.0 → t882 钉 + t927 行为双 FAIL。矩阵 340→341。待用户目视：高台收杆拽猪飞天过头顶。

### 🅵 成就（t928）
**t928** treeMinimap 拖动返修（t887 未生效）：用户实测**仍拖不动**（悬停已显示可移动光标但固定死）——实机 rig 定位（拖手柄命中区/事件透传被上层 MouseArea 吃/面板 clip/锚定重写失败）。
　根因已由 review27 #2 修复定位（fd0b3f4）：锚未清（drag 写 x/y 被锚布局同步吞回）+ 守卫恒假，onPressed 清锚 + userMoved 旗已修，待用户目视确认后关单。 ✅✅（7e6982d）核验收尾：修复五要素（onPressed 清 top/right 两锚 / userMoved 守卫 / 旧死守卫负向 / treeViewport 口径 drag 边界 / 缩窗 Connections 钳回）在 t920-t927 多轮 Main.qml 改动后**全部在位**（t887b 源码钉探针持续 PASS）；本项补上钉链最后一环——**drag.target: treeMinimap 接线本体**入钉（无此行清锚/守卫全在也拖不动），阴性轮改指 nullItem → 恰 t887b FAIL。矩阵 341 持平。行为级拖动需真窗口输入 headless 不可达（取舍已在探针头声明）——待用户目视确认拖动后关单。

### 🅶 杂项（t929-t932）
**t929** 大峡谷孤立水方块：峡谷中央生成**孤立无支撑水格**（直接掉落/悬空孤水）——canyon 生成期水位裁剪修复（孤立格不生成或需支撑面）。 ✅✅（6ee759c）根因 = t376/t601 高源瀑布置源 pass 本体：峡心柱悬空一格 Water 源（下方构造性恒 canyon air、水平四邻恒无水；水源永不蒸发但起 tick 即泄成孤立下落水柱 = 用户所见「单独的水方块直接掉落」）。修 = **整 pass 退役**（置源 + (1a) 壁环含水检测死码一并移除），峡谷定版干涸地貌（carve 盘内排干 + kDrainRadius 排水带 + 不补新源；游玩期玩家倒水造瀑不受影响）。探针 P-t929：6 个确定性 seed（1337/42/7/2024/8888/555）全图扫孤立水格（Water 且下方 Air 且 4 水平邻皆非水）== 0；阴性轮（退役前代码）：1337/42/7/2024 四 seed 各恰 1 孤立格（坐标 y=40 = 瀑布源）→ 恰 t929 FAIL。矩阵 341→342。待目视：新版峡谷中央无孤立下落水柱。
**t930** 沙子悬浮链：破坏沙子或在其 1 格内（**26 邻域**）放置方块 → 触发周围悬空沙检测**连锁掉落**（真实沙重力传播；现只有直接破坏才掉）。 ✅✅（0d275d9）checkGravityBlockOnEdit 新增 **③ cascadeGravityAround**：任何编辑（破坏 / 放置，含旧版 `isFullCube(id)` 早退的「放置完整立方」路径——正是用户「放置一格内方块也要更新沙悬浮态」缺口）→ BFS 扫编辑格 **26 邻域**（3×3×3 减自身，含斜对角）失撑重力方块（谓词同 ①：下方非完整立方 / 世界底）→ dropGravityColumn 整柱坍落；**每个被清柱格 + 柱顶上格入队续扫 26 邻** → 连锁传播（距编辑格 dx=2 的浮沙经首柱坍落格续扫带落；柱顶上格覆盖「柱顶附着物被连带脱落后其上方」间隙链）。终止性 = 重力格单调递减（Air 复扫天然跳过，无 visited 集）；邻格非沙族单次 blockAt 早退（26 读/格热路径可承受）；无 check*OnEdit 重入（dropGravityColumn→recheck 反重入约定保持）；放置重力方块分支不再提前 return（放置同属一格内编辑）；t799 直接上方支线 ② 保留。探针 P-t930 五腿（悬空沙以 **setBlockFromEntity 直写构造**——实体着地入口无编辑钩子，「落地后被挖穿支撑」滞留态的确定性等价孤本；晚位 slot 山体内先 setBlock 清空气袋）：(a) 破坏对角邻格触发 / (b) 放置完整立方（斜角）触发 / (c) 连锁到初扫不可达的 dx=2 浮沙 / (d) 阴性——有支撑沙邻域编辑不掉 + 放置支撑面救活悬空沙 / (e) 破坏沙柱底格全柱坍落（②回归钉）。阴性轮（去 ③）：a/b/c 浮沙滞留 id=8 恰 t930 FAIL。矩阵 342→343。待目视：沙堆 / 斜角浮沙结构随编辑连锁塌净。
**t931** 满耐久不显耐久条：背包等 UI 面板耐久条对齐 hotbar 行为——**满耐久不显示**、掉耐久才显示（现背包恒显）。 ✅✅（4229532）根因 = DurabilityBar 组件 visible 只判 `maxDur>0 && curDur>0`（t498 曾定「背包常显」口径）→ 满耐久显满绿条，与 HUD hotbar（t315/t349 `curDur<maxDur` 满耐久隐）两套口径。修 = 组件 visible 增 `curDur < maxDur`（**t498 口径被用户翻案**：新工具 / 新护甲满耐久槽内无条，受损后才见且持续）——Inventory 生存 tab 主栏 / hotbar / 装备槽 + SurvivalInventory 主栏 / hotbar 全走组件自动统一；唯二不迁移的内联位同判：SurvivalInventory **armorDurBar**（内联护甲条）+ 护甲**耐久数字 Text**（满耐久无数字，`armorDurabilityAt < armorMaxDurability` 才显）；HUD hotbar 参照实现不动（正锚）。源码钉探针 P-t931（t902 先例）：组件 visible / armorDurBar visible / 数字 visible / Main.qml hotbar 参照四面文本钉；阴性轮（组件 visible 去比较项）→ 恰 t931 FAIL。矩阵 343→344。纯 UI 修，待目视：新工具进背包无条、砍一刀后出现。
**t932** 第一人称手与静水重叠：手贴图与**降位后静水面**（t892 水面降 1/8）深度冲突显「手在水里」——手渲染层与水面高度带的判定/深度关系修复（手不在水格却按水下渲染或 z 穿透）。 ✅✅（9e6e473）根因 = **透明通道深度缺写**：手臂皮肤盒（t855 opacity 0.99）与水段（opacity 0.7）同走透明队列，材质默认 `OpaqueOnlyDepthDraw` **不写深度**——当某水段 chunk Model 在透明队列中排到手之后画，水片元做深度测试时深度缓冲里只有不透明地形（手没留深度）→ 水直接盖过手臂 =「手贴图与静水重合、手在水里面」（与 t892 降位无因果，降位只是让用户盯到水面）。修 = **深度关系钉死**：手子树四处透明材质（手臂皮肤盒 / 手持玻璃 Blend 立方 / 异形·床 billboard / 火把 billboard）加 `depthDrawMode: Material.AlwaysDepthDraw`（blend 与 t855 alpha 契约不动，仅写深度）→ 后画的水/岩浆/玻璃按像素深度测试输给手；**真正更近的水**（眼贴水面涉水、手臂确在液面下）深度更小仍正确盖手（真在水里的部分照常显水下）；不透明 UnitCube 手持工具本就写深度无需同修。参数钉在 commit（t781 先例纯视觉零探针，矩阵 344 不变；exe 冒烟 12s 存活零崩溃；QML 枚举名对照 Qt 6.11.1 官方文档核实 `Material.AlwaysDepthDraw` 非 `Material.Always`——错名会静默无效）。待目视：岸边 / 涉水旁看静水，手臂完整在水面之前不没入。

### 📎 R19.16 范围与顺序
t904-t932（29 项）。**建议顺序：t904-t906 性能调研三连最高优先（用户点名；产出根因报告与修复面，后续修复按调研结论展开）→ t907-t913 矿车组（玩法阻塞）→ t914-t919 附魔组 → t920-t925 查看器组 → t926/t927 钓鱼 → t928 成就 → t929-t932 杂项**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。实机复现类（t914/t915/t923/t928/t919）产出须用户在新版本戳 exe 上口头确认后关单。

## R19.17 用户实测复盘批五 + 性能批二（t933-t977，45 项；2026-08-28 立项）

**立项背景**：R19.16+review28 全闭后第五轮实测。两条主线：① **性能批二**——t904 插桩拿到第一份真机 frame2 数据：TNT 炸沙坑后 **8FPS/125ms 帧**，kill @e 无效，**保存退出新建世界仍个位数**，重启 exe 恢复 100FPS（进程级状态泄漏实锤）；waitSync 76ms 主导 + mob ltail 10.59ms（t905 头号假设获实锤）；② 红石铁轨传播/重算风暴 + 狼/豹猫生态返修 + 附魔文案/随机性 + 查看器形态切换系统。

**F3 frame2 快照存档（用户 2026-08-28 20:40 @ fc66617，TNT-on-sand 爆炸后）**：
- `frame ms/f: main*88.4 render 7.4 qmlSync 2.6 residual 63.3`（residual 63.3 仍大）
- `frame2 ms/f: evA 15.2 waitSync 76.0 idleB 34.1`（idleA 37.7）——**waitSync 76ms 一家独大 = 渲染线程同步等待**（渲染侧过载：疑似爆炸后光照/阴影重算风暴驱动 mesh 重建风暴）
- `mob sub ms/f: phys 1.59 [head 0.00 tail 0.00 ltail 10.59] st[R 140 F 5 V 0 D 0]`——**ltail 10.59 实锤 t905 头号假设**（emit entitiesChanged 47 槽 delegate 扇出）
- 世界 160×160×128、5 只 mob、draw-calls 102、render 1.0ms（渲染本身不慢——忙在别处）
- 用户怀疑「环境阴影光照一直重建」；跨世界仍卡+重启恢复 = 进程级泄漏（对照 cross-world delegate leak 先例）

### 🅰 性能批二（t933-t935）⚠️ 最高优先
**t933** 跨世界卡顿泄漏定位：TNT 炸沙坑 → 8FPS → kill @e 无效 → 保存退出新建世界仍卡 → 重启 exe 恢复。进程级状态泄漏调查（光照重算队列/静态 dirty/静态缓存未随世界清理/爆炸后 recomputeLightAround 循环重建）。复现 rig：沙丘+TNT 爆炸 → 量化每帧 recompute/mesh 重建数 → 换世界后残留面清点。修复或登记根因。 ✅✅（bc887c6）**同世界风暴根因（修复）**= t930 级联把 t320 已批量化掉的逐格重算重新引入爆炸链：dropGravityColumn 旧版**每格**调 recomputeLightAround（每次 = ±15×到世界顶两通道全盒 reflood ~数十 k 体素 + 一次 qInfo 落盘）+ **每柱** 1 次 worldChanged QML 扇出（recomputeMeshStats + 六 host cleanupVis + chunk 重建）→ 沙坑一次爆炸 = 数百次全盒重 flood + 数百次日志刷盘 + 数百次扇出 = 一帧数百 ms。修（destroySphereSilent 批量收口同先例）= 柱末一次 refloodBox（①/② 直落支线）+ cascadeGravityAround 全 BFS 批（m_batchGravity + m_gravLight* 联合盒，m_fluidAct* 活动盒同模式）级联末**一次**联合盒 reflood + **一次** worldChanged；等价性 = 每格影响 ⊆ 其 ±15 盒 ⊆ 联合盒、边界种子法终态一致（flushPendingLightEdits 同论证），行为钉 skyLight==15 / 遮挡 <15。recomputeLightAround 的 per-call qInfo 退役为 FrameProfiler 计数（>3ms 才落日志）。**跨世界判决（登记）**= World C++ 层无泄漏：全套 tick 电池（水/岩浆/火/生长/冰/叶衰/天气/红石）爆炸收敛后**零** worldChanged/blockBroken/reflood，且 regenerate（新建世界）与 beginLoad+finishLoad（读档）两真实退出路径后同零——全部索引集 / 侧表 / 脏标志 / 活动盒 / t933 联合盒均清；**用户实测的跨世界残留不在 World 层，在 QML 场景层**（实体槽高水位 delegate revision 扇出 ltail 10.59ms = t935；渲染侧 waitSync 76ms = t934——两者均由新增 F3 `act ct` 行（reflood/ledit/casc/gcol/gcell 1s 窗聚合，稳态恒 0、换世界后非 0 = 跨世界写入者）直接判读）。探针 P-t933（独立 96×96×64 世界，水位 58 弹坑干燥；rig 位**程序化搜索**（干盒 / 空气盒——山体到顶的种子不可假定固定坐标为空）+ **先沉降后找位**顺序契约（worldgen 水系首轮全量扫描后才稳定，先找位会把 rig 压在沉降后才出现的泉眼上、平台堵水柱污染零断言））：(a) 128..200 格悬空沙一次编辑全坍落 reflood ≤4 / worldChanged ≤3 + 光照终态等价；(b) 同世界稳态零；(c) regenerate / load 后零。阴性轮（去级联批）：rf 101 / wc 101 → 恰 t933 FAIL（347/1）；复原 348 PASS / 0 FAIL。exe 冒烟 12s 存活、`act ct` 行落 log 稳态全零。待目视（t933 实机复现类）：新版本沙坑 TNT 爆炸帧时间 + 换世界后 F3 `act ct` 全零确认。
**t934** waitSync 76ms 归因：渲染线程同步等待主导——结合 t933 场景解剖渲染侧过载源（阴影/光照重算驱动的 mesh 重建风暴 vs 透明段重排）；插桩/日志量化后修（节流/增量/合批）或登记。 ✅✅（待实机复现确认）**归因（登记 + 插桩交付）**= waitSync（afterAnimating→beforeSynchronizing，main.cpp t904 四段）量的是 GUI 阻塞等渲染线程抵达同步屏障；渲染线程须先跑完**上一帧**的 [渲染 pass（render_cpu / RenderStats render ~1-7ms——确实不慢）+ **present/vsync 阻塞**（D3D11 FIFO 2 缓冲、GPU 落后时 swap 可等数个帧周期）+ 帧尾清理] 才到屏障 → render_cpu 小而 waitSync 76ms 的时间去向收敛为三汇：①GPU/present bound（真 GPU 时间此前无测量面）；②渲染线程 prep/上传风暴（t930/t933 mesh 重建风暴的渲染侧回声——用户快照采于 bc887c6 修复前，风暴本体即 76ms 主源，t933 已把触发面 reflood/worldChanged 101→2）；③渲染合帧/hook 多发（**测量口径非独立开销**：四段按事件计样本、main_total 按 frameSwapped 到达 GUI 计，拥塞下恒等式不可加——用户实测 main 88.4 vs 四段和 150.4 的机械解释）。用户怀疑的「阴影/光照一直重建」在渲染侧无对应面：本工程无 shadow map（PCF 软影烘顶点色，chunkgeometry sunRebuildDue 量化门控，非每帧重建）。**插桩交付（本任务主体，rig 无 GUI → 三级钉）**= (1) `fPresent` 桶（main.cpp：afterRendering[渲染线程 DirectConnection]→frameSwapped[GUI 收到] = present 阻塞 + queued 派发延迟）；(2) 帧/帧二行每段样本数 `(N)`（FrameProfiler addSampleMs 每有效样本 cnt:<name> +1，忽略样本不计数——N 不齐本身即判据）；(3) **F3 render-side 真值行**（RenderStats 6.11：frameTime/syncTime/renderPrepareTime/maxFrameTime + **lastCompletedGpuTime 真 GPU ms**（RHI timestamp 查询，补上 perf-t520「无 GPU 计时」的诚实缺口）+ vmemUsedBytes（跨世界单调涨/重启才回落 = 进程级 GPU 泄漏签名）+ pipelineCount）。**实机判读指引（F3）**：waitSync 大 + render-side `gpu` 同量级大 + frame2 `present` 大 → ①（治理面 = renderDistance/段折叠/透明 overdraw）；waitSync 大 + `prep` 大 + win 行 mesh reb 非 0 → ②；都小 + (N) 不齐 → ③。F3 关闭时 extended 采集关——读数以 F3 开为准（且 F3 本身有开销，帧率对照须 F3 关）。探针 P-t934：(a) 行为级计数 roundtrip（忽略样本不计数 + 报告 `ms(N)` 格式含 present 段）+ (b) fPresent 接线/计数 bump 源码钉 + (c) F3 render-side 属性名源码钉（拼错名 = 运行期 TypeError 静默断行，headless 不可见）。两轮阴性验证：故意留注释内残文 → 行为腿红（源码钉被注释蒙蔽的教训即时验证）；彻底删字面量 → 恰 P-t934 三腿全红（349→348/1）。连带：t895 的 buildF3Text 切片窗 5200→9600（函数随 render-side 段长到 8736，窗口不足 = 假红，其注释自带的维护规则）。矩阵 348→349 PASS / 0 FAIL（t882 阴性轮偶发 flake 复跑清零）；exe 冒烟 12s 存活 root objects=1 零 TypeError。**修 vs 登记的取舍**：风暴本体（②的主源）已由 t933 修掉；①/③ 的治理（GPU 负载调优 / 帧合并口径）无实机数据不动——等用户在新版本复测沙坑 TNT 爆炸，按上述判读行读数后再定（实机复现类，须用户口头确认关单）。
**t935** mob ltail 10.59ms 修复：emit entitiesChanged 全 47 槽 delegate revision 扇出（t905 假设实锤）——按帧只发一次已做（节流）但单次 emit 激活全部 delegate 绑定仍是成本——收口面：粒度化 revision（按需槽刷新/脏名单）/delegate 只绑 position 不绑全量/沿用 t858 instancing 思路压节点数。量化 before/after。 ✅✅（02f958b）**选刀（粒度化 revision / 脏名单）**：三收口面里 delegate-只绑-position 要拆 ~50 绑定 × 19 mob Loader 的低频/高频分层（改动面大且静置 mob 仍吃 emit 扇出）、instancing 需重写全部 mobType 视觉块（t858 掉落物是单一立方才轻），而脏名单对「空槽 + 静置 mob 白算」这一实测主因是精确打击 → 槽位指纹差分 + 每槽 EntitySlotMonitor。**ltail 构成分账**：单次 emit 成本 = 全 N 槽 delegate × ~50 个 `{revision; xxxAt(index)}` 绑定重求值（每条含 Q_INVOKABLE 调用 + QVariant 装箱）+ 行走 mob MobModel 几何重建（12 腿姿量化后仍每 emit 跨步即重建）；47 槽高水位下空槽 / 静置 mob 的重求值全读回相同值 = 纯浪费（t933 判决的 QML 侧残留面）。**实现**：34 处 `++m_revision; emit` 对 + clearAll 收口进 notifyEntitiesChanged 漏斗，emit 前对每槽算**可见态指纹**（slotFingerprint = At() 访问器底层字段全集契约：alive/pos/half/kind/blockId/state/primed/fuse/color/v(箭朝向)/mobType/血量/红闪/燃烧/减速/yaw/walkPhase/eat(羊低头)/sheared/毛色/love/baby/狼猫驯服坐态/蓄力/拉弓/激怒/铁傀儡 windup/碎裂/护甲×4；**刻意排除**每 tick 恒变但零视觉的 AI/经济计时器（wanderTimer/aiAccum/eggTimer…）——入指纹会让静置 mob 每 emit 空转 bump，恰是本任务要消灭的浪费；「翻转才可见」的计时器（growTimer→baby 等）以对应布尔入指纹，翻转帧必 bump），只 bump 指纹真变槽的监视器（slotMonitorAt 惰性建、父对象托管永不销毁——delegate var 引用与槽 index 同样终身稳定）；Main.qml 117 处 delegate 绑定 + 21 处 tint helper 调用点迁 `mon.revision`（表达式形式守 t498 铁律；helper 函数体的裸 revision 语句同步退役），全局 revision 照常 bump（F3/遗留面兼容）但不再被任何 delegate 绑定触碰。碰撞口径：64 位混合、47 槽×20Hz 连玩一年生日碰撞 ~2e-10，失败模式 = 单槽错过一帧中间态（下次真变化自愈）。**after 成本面**：每 notify = O(count) 指纹混合（微秒级）+ bump 槽 × ~50 求值；死槽 / 高水位槽 / 跨世界残留槽零绑定成本（槽池保留——t256 防泄漏设计不动，只是不再付费）；行走 mob MobModel 重建保留（量化门不变，只发生在 bump 槽）。**量化（代理指标进 F3 mob 行，1s 窗）**：`emit`(notify 次数) / `bump`(指纹差分实际刷新槽数 = 真实成本) / `fan`(旧口径 count×emit 全扇出 = 修复前实付口径)——bump<<fan 即收口生效，差值全部来自静置/空槽/高水位槽；FrameProfiler 加 addCount(name,n) 批量计数口。探针 P-t935：(a) 3 猪 damage 槽 1 → 恰槽 1 revision+1、全局 +1、计数差分 emit=1/bump=1/fan=3；(b) clearAll 全 bump（隐藏）后逐只复用空槽重 spawn → 每次恰复用槽 +1、死槽零 bump（高水位残留归零判据）；(c) 第 4 只 count 3→4 → fan=4 而 bump=1（成本 O(变化槽) 非 O(count)）+ 新槽监视器首 damage 正常 bump；(d) 源码钉漏斗/指纹/监视器 + QML 迁移面（mon.revision ≥100 处、entityManager.revision 全文件残留 0）。阴性轮（指纹比较旁路 → 全 bump）：恰 P-t935 FAIL（349/1，行为腿 a/b2/c1/c2 红——t934 教训的行为腿优先）；复原 350 PASS / 0 FAIL（原 349）。exe 冒烟 14s 存活零 TypeError、mobHost 领养正常、emit/bump/fan 读数落 vo.prof。待目视（实机复现类）：用户 TNT 炸沙坑重放，读新 mob 行 ltail 直降幅度 + bump vs fan 差值。

### 🅱 红石与铁轨（t936-t945）
**t936** 动力轨传播顺序无关：先放上坡动力轨再激活一段，**后放**的其他上坡动力轨不被激活（要全部摆好再激活才行）——放置动力轨时若邻轨链可达已激活源应立即传播（t910 补放置沿重算；「不管先放什么，激活都沿动力铁轨链传到红石最远可达范围」）。 ✅✅（f303366）**根因（扫描域 ≠ 链几何域）**= recomputePowerLocal 的接收器扫描域 = 锚点 + 其 **6 正交邻**，而坡链相邻轨是斜角（轴向 ±1 层，t910 几何）→ 后放的动力轨虽贴着已通电链，pass 内 receivers 够不到链尾 ≤8 格外的直供种子 → 链 BFS 无种子 → 恒判灭 = 用户实测顺序依赖（「要全部摆好再激活才行」）；同洞对称面 = 破坡链中段轨后**远翼轨**不在任何 6 正交扫描域 → 残留通电位永不熄（放置/破坏对称缺口）。**修法（放置沿重算）**= notePowerWrite 对动力轨编辑（放/破/置换）从编辑格沿链走 kGoldenRailChainMax 步、沿途动力轨全部入脏集 → 下 tick 成锚点、链上直供轨（若有）进 receivers 成种子、t704/t910 BFS 从**真种子**定深重亮/收缩 → 激活集 = 布局纯函数（任意摆放序收敛同一激活集；平链尾接轨的 1 tick 闪灭同消失——走查把种子同 pass 拉回）。影响半径证明：轨 R 因编辑 E 翻转必链距 ≤ 上限-1（翻亮 = 存在种子 P 使 d(R,P)≤7 且路径经 E；断链翻暗同理）→ 定深走查全覆盖。代价 = 每动力轨编辑一次性 ≤4 向×8 深走（seen 去重环轨安全），编辑路径非热路径、稳态零增量。**单一权威加固（lessons「校验/派生扫描的探测域必须与写入侧权威一致」）**= 抽 `goldenRailChainStep`（三高探针 railProbeDelta + GoldenRail 过滤，同层优先序原样）——链 BFS（t910 坡层步原样迁入）与放置走查**同一步**，全库无第二套链几何判定；kGoldenRailChainMax 提文件级（BFS 定深与走查半径同一深度源，恰一处声明）。**探针 P-t936 五腿**：(a) 用户序主腿（坡链 A..C 先摆 + 源激活段亮 → **后放** D..F 接链 → 6 根全亮）；(b) 阴性·无源链延伸保持灭 + 源**最后**放整链 7 根全亮（第三种摆放序同收敛）；(c) 下坡 9 根链恰亮种子+7、第 9 根灭（kGoldenRailChainMax 钉住不过度点亮，兼钉 -1 层传播）；(d) 破亮坡链中段 → 源侧 2 根保持亮、远翼 2 根熄灭（破坏对称）；(e) 源码钉（helper 声明+定义 + notePowerWrite 走查块 + BFS 消费同 helper + 深度常量恰一处）。阴性轮（走查守卫临时禁用）：leg A litAll 3/6 + leg D 远翼残留亮（两症状均复现）恰 t936 FAIL（t882 flake 复跑清）；复原 351 PASS / 0 FAIL（350→351）。exe 冒烟 12s 存活。
**t937** 平行轨道独立激活 + 重算风暴：① 一个红石激活了**两条独立无连接**的平行轨道（应只激活红石可直接作用的那条——传播必须沿轨链物理连接，不沿空间范围）；② 挖/放动力轨或**旁边放普通方块**都触发全轨激活重算（肉眼见动力轨灭一下又亮）——用户怀疑 t930 26 邻域级联连带红石全量重算/重渲染——核编辑钩子的红石重算触发面，收窄到「真影响轨/能量源连通性的编辑」。 ✅✅（fcb3d8e）**① 根因（链步 = 纯空间存在性）**= goldenRailChainStep 只查「该向三高有动力轨」，平行贴邻的两条独立轨（各走贯穿轴、互不设跨向连接位）被当作链 → 信号横穿 = 用户实测症状；直供判定本身（isReceivingPower 6 正交邻）**无**盒子/半径域——空间泄漏在链步不在种子。**修法（连接位门槛）**= 步源轨须持该轴向 RailConnPx/Nx/Pz/Nz 位（railConnections 写入、mesher 形态、矿车 pickTrackStep 消费的同一物理连接权威）——贯穿轴轨结构上不持跨向位 → 横穿路封死；t936 破坏沿的编辑格探针（破后 Air 无 state）不设门槛（那是入脏集的拓扑发现，点亮由重算侧门槛把关）——与 t936 走查/脏集机制同一 helper 兼容，无第二套判定。**② 触发面枚举结论**= 全部写入口（setBlock×2 / setWaterSilent / clearBlockSilent / setBlockFromEntity / destroySphereSilent / checkRailOnEdit 失撑 / 爆炸逐格 / 火把到期）统一收口 notePowerWrite 快路径「6 邻任意红石族格」——接收器（轨/灯/TNT/门）也算族 → 普通方块编辑（放/挖土石、**t930 级联沙逐格着地**——用户的级联怀疑属实，路径=着地 setBlockFromEntity→快路径邻轨命中）全触发；而普通方块与空气在电力读数（powerSourceLevel/isRedstoneDust）里**同为 0** → 这类编辑不改变任何读数。**修法（收窄到输入端）**= 快路径只认 6 邻**粉 ∪ 电源**（isPowerEmitterBlock 纯 id 谓词，与 powerSourceLevel 源集合同员）；粉/源邻保留触发（t706 可达性、粉失撑邻粉入脏集语义不变；接收器/源本体失撑掉落走各自编辑写——oldId 属族天然慢路径）。**② 附加（先算后清，灭→亮两拍根治）**= 误熄中间态根因 = 正向 BFS 只见本 pass 扫描域（锚点+6 邻）内种子，触发点落在亮链中段旁（拉杆/灯/粉编辑）时种子在域外 → 误写暗、波前数 tick 后摸到真种子重亮；修法 = 欲写灭前 goldenRailChainHasFedSeed 反向有界走查（同链步权威同深度 ≤7 找直供轨——激活集纯函数的完备判据，终态不变、t936 顺序无关不变量保持）+ goldenPowered 链传集并入接收器写集（亮沿一次 pass 点亮整链，旧版波前逐 tick 一趟重算 machinery 空转 = 风暴放大器）+ t704 波前重插改经 goldenRailChainStep（平行轨不再吃横穿脏标记）。**探针 P-t937**：(a) 平行双线 rig A 全亮/B 恒灭；(b) 亮链中段旁放+挖 Stone → powerRecomputePasses 计数器不动 + 链恒 6/6（零重算判据，计数器 = recomputePowerLocal 实跑 pass 数）；(c) 未扳拉杆放中段旁 → 计数增长（真触发面不回缩）且不闪 + 拆源全灭（降沿终态）；(d) 源码钉五枚（门槛/收窄谓词/走查消费/并入/计数器）。阴性轮三发：门槛禁用 → litB 6（用户症状复现）；快路径还原任意族邻 → 计数 639→641；走查禁用 → aLever 5 后 settled 6（灭→亮拍复现）；复原全绿。矩阵 351→352 PASS / 0 FAIL（t882 flake 复跑清）。exe 冒烟 12s 存活。
**t938** 铁轨可选中：鼠标 raycast 穿过铁轨选中后面方块——挖铁轨变挖后面、放矿车不便。raycast 对薄/非满格方块（铁轨族）命中判定补全。 ✅✅（fab83cb）根因 = t638③ 给铁轨族的选体命中盒是 **2/16 贴地薄板**（raycastAABBs）→ 瞄轨格中上部（薄板上方 14/16 空气段）的射线全部穿到后格，轨的屏幕可点击区域只剩贴地一线（站姿眼位 1.62 对 2-3 格外轨的入射 y_frac 0.4-0.9 轻松越过薄板）= 实际不可选；矿车放置分支恰以「命中格 isRail」为轨上放车判据 → 放矿车同堵。修 = **选体专用 HitRail 位**（RayFilter 新 1u<<5；updateRaycast 过滤器改 HitTorch|HitLadder|HitRail）+ raycast.cpp fullCell 特判 `|| ((filter & HitRail) && isRail(b))`——选体模式下轨格**整格命中**（进格即中、法线=进格面），四消费者铁律只动射线命中这一个消费者（t639 耕地 / t849 铁砧·仙人掌的 raycast.cpp 收口先例反向同款；isFullCube / collision / selection 各自语义不动）。透视语义分工：穿**轨格** → 命中轨；穿轨**上方空域**（轨格上一格）→ 照旧命中后方（t638③ 经上方路径保留，round-3「选不到后格」靠抬高视线越过轨线仍修）；相机（HitPartial 不设 HitRail）对轨仍薄板 sub-AABB（轨无碰撞不拉近视距，t605 零回归）；桶/钓竿非精确模式对轨本就整格（!preciseMode 支路）不变。附：selectionAABBs 补轨薄板分支（ShapeNone 原先选中框/裂纹叠层**恒无形状**——选中轨零描边反馈）——描边/裂纹贴实际轨形非满格黑边（t801/t849 口径）。交互连锁核验：挖轨（既有 drop 链，选体可达即通）、放矿车（命中格 isRail → spawnCart 轨上直达）、放方块（hit+normal 通用推导结构性永不写轨格 + 轨格非 Air/水/岩浆不满足任何放置预检=不可替换）、描边贴薄板。探针 P-t938 七腿（直调 raycastVoxel，rig=轨格+后 2 格石墙）：a 浅俯角穿轨格→命中轨格+法线 -X+命中点在格内（旧行为恰命中墙=用户症状复现）/ b 陡俯角轨心回归钉 / c 穿轨上方空域→命中后墙（透视保留）/ d 动力·探测轨同构（isRail 家族）/ e 同射线换相机过滤器仍穿轨命中墙（相机零回归钉）/ f 放置回归（邻格≠轨格且 Air、放置后轨 id 不变、轨格不满足任何预检）/ g 六枚源码钉。阴性轮（特判加 && false）：a/d/f+钉 g2 红、恰 t938 FAIL（352/1）；复原 353 PASS / 0 FAIL（t891 偶发 flake 复跑清）；编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视：瞄轨格即选中轨（描边贴薄板）、挖轨掉轨、手持矿车右键轨上落车、抬高视线仍可选轨后目标。
**t939** 坡上静置矿车下滑规则补全：**单格上/下坡**静置矿车仍静止——应往下坡运动。口径（用户定稿）：未激活动力轨=减速可平衡坡上；**普通铁轨=下滑**；**激活动力轨+探测轨=往下坡运动**。t909② 自溜链在单格坡/各轨型边界补全。 ✅✅（c4d13c4）根因 = t909② 静置闸的坡向判定只读**邻轨层差**（railProbeDelta 查下一格，连续坡每格 ±1）——单格坡（平轨嵌一格凸/凹）的坡度全部落在**本格面**上（railRiseAt 的 fx 线性坡），本格邻轨探针读 {上坡侧 +1, 平侧 0}，两头无 -1 → 判平面停驻 = 用户症状。修（用户定稿三口径落点）= ① **轨型闸**（先于一切坡向判定）：未激活动力轨 = 减速闸刹住坡上静车（MC 1.0 断电 powered rail 语义；激活态沿 t937 goldenPowered 写入的同一 GoldenRailStateOnFlag 位读，无第二套判定）；② 普通 / 探测 / 通电动力轨 → 有**下坡分量**即起步溜（通电动力轨起步后 t735④ boost lerp 接管）；平地无下坡分量三轨型一律静止 → t735④「平地静置空车不被弹射」不破（弹射豁免看坡向不看轨型）。③ 坡向补全本体 = **cartRailGradient 本格面梯度采样**（与 Y 钉定 / 俯仰同一张面 ±kCartPitchProbe 窗；越出本格列扫读邻格面 = 真实连续坡面梯度；阈 kCartSlopeGradMin=0.1 与 t863① 0.05 高差同口径）——同时喂**静置闸**（下坡侧起步 kCartSlopeKick，车头朝上坡先翻向）与**滑行坡向分类**（沿行进向采样——负速倒行梯度跟行进不跟车头；本格面梯度显著时覆盖邻轨层差：连续 1:1 坡两口径逐格同判 t909/t864 零变化，单格坡 / V 谷翼解锁，滚落是坡道重力自然加速非摩擦蠕动半格一停）。V 形永动探针（t909）与 t735④/t864/t907 既有探针全绿零适配。探针 P-t939 七腿（驼峰 rig：西 4 格引道+上坡格+峰+下坡格+东 2 格引出+独立电源行）：a 单格上坡普通轨西滚 ≥1.5（kick-only 摩擦滑仅 ~0.5 → 阈值兼钉滑行半边的重力覆盖）/ b 单格下坡东滚 ≥1.5 / c 未激活动力轨同场景位移 0（口径①平衡钉）/ d 通电动力轨（RedstoneBlock 直供，flag 先验）西滚 ≥1.5 / e 探测轨东滚 ≥1.5 / f 平地普通轨+通电动力轨平地静车均 ≈0（t735④ 阴性回归）/ g 五枚源码钉。阴性两轮：静置闸梯度语义禁用 → a/b/d/e 全零位移（用户症状复现）恰 t939 FAIL；brake 禁用 → c 溜 3.42 格恰 t939 FAIL。矩阵 353→354 PASS / 0 FAIL；编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视：平轨嵌一格凸/凹，放上矿车自然滚向下坡侧；未激活动力轨上停得住。
**t940** 推车回轨吸附：玩家身体碰撞把矿车推到旁边铁轨上时，矿车**自动吸附回轨**恢复正常移动形态（近轨 snap）。 ✅✅（04c5051）根因 = 脱轨 / 地面车被推进轨格列后**仍走 tickDerailedCart 自由物理**——轨列只「接住」了位置（Y 被列扫反复钉在贴地 0.3875，车底穿轨板 0.05），移动形态永不回轨上（骑乘高 0.45 / 轨约束推进 / 沿轨推全失联），沿轨方向的推入也整线穿轨滑行。修 = tickDerailedCart 末尾新增 **trySnapDerailedToRail** 近轨吸附（被骑 / 空车两路共享该 tick → 单点收口），吸附域三闸防误吸：①**同层闸**（严格列扫解析到的轨层必须恰为车心格层 Δ=0——车心在轨上方的坠落途中不吸〔自上而下落轨由既有落地重挂承接，全程速度保留不被抢答〕、轨在车心下方层的隔板 / 隧道场景严格断扫不吸）；②**死端外向不吸**（pickTrackStep 以当前速度向选臂——推离死端的车唯一连接臂与速度向反平行 dot<0 被滤 → false 不吸，t863④/t908 推离语义零破坏；中途轨道沿轴滑行者连接臂顺速度向照吸 = 「被推上轨」正确终态）；③孤轨（0 连接）无臂恒不吸（推离孤轨既有探针零适配全绿）。吸附动作**全部复用放置 / 行驶同一套钉定**（不写第二套轨位解析）：垂直轴钉轨心线（一次性 ≤0.5「吸」位移，行进中的持续贴轨仍归 stepCartAlongRail 限速收敛）+ Y=pinCartY（railRiseAt 同源坡面——坡轨钉到坡面不平地高度）+ 俯仰 updateCartPitch（放置即贴坡同函数）+ 车头 cartYawFromDir 沿选中臂（= 速度符号侧）+ 速度投影轨轴（沿轨分量保留 ≥0，横向分量按 t908 分解口径截断；rail-locked 即正确终态 → 不设速度阈、不等减速、无条件吸附）。探针 P-t940 七断言（EW 三格主线 + 西引道 + 南腿 + NS 坡腿 rig）：a 横推上轨（用户场景）N tick 内吸回（轨心对齐 ±0.01 + 骑乘高 + 停驻轨上，吸回后持续横推 = t908 no-op 不二次脱轨）/ b 沿轨入线保留沿轨速度（贴面滑行 ≥1.2 格磨停，终态轨上形态）/ c 阴性·死端沿轴推离照旧出轨西滑落定地面 groundH（不被吸回）+ 远离轨续跑 100 tick 不动 / d 坡轨吸附 Y 钉坡面（railY+rise(0.5)+rideH=+0.95 非平地 +0.45）+ 45° 贴坡俯仰 / e 三枚源码钉（调用点 / 定义 / 声明）。阴性轮（调用禁用）：a/b/d 全红且签名恰为旧行为（groundH 穿轨滑行、横推车整线穿过后跌出 rig）、c 照绿、其余 354 探针零波动。矩阵 354→355 PASS / 0 FAIL（t882 偶发 flake 复跑清）；编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视：把脱轨 / 地面矿车朝轨道方向撞推上轨 → 车应「啪」地吸回轨心线并恢复轨上形态（骑得上、沿轨推得动、坡轨上贴坡面）。
**t941** 矿车 3D 侧贴图返修：侧边是**石头贴图**（错）——前后左右统一贴图（用前后贴图即可）。 ✅✅（b001f82）根因 = MinecartBox **kPackParts（demo 包布局 1）**纵帮 piece 1/2 大面分采壁带亮/暗窗（t862② 外亮/内暗分采）——PIL 复测壁带内区 = 平灰噪点（lum≈74、sd≈6、无结构），8× 包贴图上观感即石头纹理 = 用户实测「侧边是石头贴图」；端帮 piece 3 大面才是框栏端面窗（亮框+暗板，读作车端栏板）。修 = **piece 1/2 六面与端帮 piece 3 同套采样**（大面外内 ±X = 端面窗 (0,2)-(20,10) = 前后贴图；顶/底/薄端条带 = 端帮同款浅纹内壁行 (20,2)-(36,4)/(20,8)-(36,10)/(20,2)-(24,10)）→ 前后左右四面墙同一贴图套、**壁带整条不再被任何 piece 引用**；t862② 内外明暗层次随统一口径废止（用户最新口径优先，t931 同款翻案先例，表注释留痕）。qrc 程序布局 0 **不动**：程序壁窗有铆钉列 + 亮棱结构（读作铁壁非石头）、前后左右本就同族（左帮行字面 = 端帮窗）——正锚钉不变。注释契约同步：minecartbox.h（piece 表 + 布局 1 分区清单）、Main.qml 车斗 delegate（piece 1/2/3）。纯视觉项按 t931 先例源码钉（QML 几何表无 static_assert 面）：探针 P-t941 五腿 = (a) 统一纵帮 PartRects 行恰 ×2；(b) 端帮参照行不变；(c) 旧壁带亮/暗窗坐标串（2,10,22,28 / 24,10,44,28）全文件清零；(d) 布局 0 两行正锚不变；(e) Main.qml 五个车斗 Model 同一 `baseColorMap: cartPackHit ? cartPackTex : cartTex` 绑定恰 ×5。阴性轮（piece 2 行还原旧壁窗）：恰 P-t941 FAIL（a=false 半修可检 + c=false 石头窗回归），其余全绿。矩阵 355→356 PASS / 0 FAIL；编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视：demo 包下矿车四壁同为端面栏板贴图，侧面不再呈石头噪点。
**t942** 爆炸毁能量源后激活残留：TNT/苦力怕炸掉红石块/火把后部分动力轨仍激活——爆炸批量 setBlock 后补红石激活沿重算（destroySphere 链尾挂红石重扫）。 ✅✅（9ff73f7）根因（HEAD=4ce51b3 实测定位，非「批量绕过钩子」）：t683 已把爆炸批量逐破坏格补 notePowerWrite（destroySphereSilent 链尾，TNT detonateTntSphere / Stalker detonateStalker 两入口共用的单一收口）、t936 已给**轨编辑**挂链走查——缺口在「源 / 粉编辑格不走链」：炸掉红石块 / 火把后只有编辑格 + 其 6 正交邻（直供种子轨）进评估域，种子轨熄灭后链其余轨只能靠**翻转波前**逐 tick 拉进脏集（每熄一根才脏下一根），8 根平链实测 **5 tick 仍部分亮** = 用户「部分动力轨仍激活」残留窗；粉传形态（源—粉—轨—链）更久——Phase A2 粉电平翻转回插只回插邻粉不触链。修 = 链走查**单源提函数 + 触发面扩位**：t936 内联走查提取为 `dirtyGoldenRailChainFrom(x,y,z)`（world.cpp，步进仍只经 goldenRailChainStep 一套判定；编辑格非轨无连接位门槛 = t936 破坏沿同口径，三高探针发现 ±1 层种子轨），触发从「轨编辑」扩到「轨 ∪ 电源（isPowerEmitterBlock）∪ 粉（isRedstoneDust）」——被毁能量源喂着的整链同 tick 入脏集、**下一 tick 一次 pass 全灭**（升沿本就一次 pass，t937 goldenPowered 并入写集；灭沿对齐同语义，dev-plan「链全灭（下一 tick 内）」验收达成）；Phase A2 粉电平翻转回插同调 helper（经**幸存粉**中继的链也无波前）。t937 快路径收窄零触碰：普通方块编辑仍在收窄谓词早退（探针腿 d 经爆炸路径钉死不回退）；t936 注释锚与 lambda 行在 helper 内原样保留 → P-t936 零适配全绿。探针 P-t942 六腿（destroySphereSilent 直编）：a 红石块直供平链 ×8 爆心钉源（r0.9 只毁源）→ **1 tick 全灭** / b 火把源同腿 / c 爆心钉中段轨 → 源侧 3 亮 + 远翼 4 灭（t936 破坏对称经爆炸路径）/ d 爆掉远离 rig 的孤石 → powerRecomputePasses 计数不动 + 链 8/8（收窄不回退）/ e 坡链（t910 几何）爆源 1 tick 全灭 / f 源码钉（helper 声明+定义恰一处〔禁第二套链判定〕、t942 ② 触发行、Phase A2 走查行对、destroySphereSilent 链尾挂点注释、t936 注释正锚）。阴性轮（触发还原轨编辑单条件）：a/b/e 红且签名恰为波前症状（t1 仍 7/7/6 亮 = 用户症状复现）、c/d 绿 → 恰 P-t942 FAIL；复原矩阵 356→**357 PASS / 0 FAIL**（t882/t891 偶发 flake 复跑清）；编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视：TNT/苦力怕炸掉供能红石块/火把后整条动力轨链同拍熄灭（无逐根熄灭的拖尾）。
**t943** V 字载人变慢 + 多车卡出：① 载人后速度变慢、最高点速度正转负时**特别慢**（往返换向阻尼过大？）；② 多矿车丝滑运动有概率**卡出 V 字到隔壁**/**横着卡在坡上**（非 45° 状态）/挤压颤抖卡死——t907/t909 去穿插与坡向参数返修。 ✅✅（eb1980e）**① 根因分账（载人 coasting 无坡道重力）**：被骑路径旧版无输入（|wish 投影|≈0）走 targetV lerp——上坡 targetV=0 → 唯一减速 = kCartFriction(2/s) 指数衰减（12.8 要滑 ~6.4 格才停、顶点前长时间 0.x b/s 爬行 = 「最高点速度正转负时特别慢」的精确机制；指数渐近零、永无干脆的重力翻向），下坡靠 slopeDownAuto 抬 targetV 到 ±10 再 kCartAccel(3/s) 缓起（τ0.33s）；而空车路径（t909③）是 kCartSlopeGravity(19.8/s²) 沿轨重力直接积分——同一 V 空车丝滑、载人爬行 = 物理口径劈叉，非「换向翻向惩罚」。**修** = tickRiddenCart 新增 **coasting 分支**：|proj|≤ε 且非通电动力段时改走空车同一套坡道积分（上坡 19.8/s² 带号减速 / 下坡向 ±kCartSlopeDownSpeed 收敛 / 平·无轨摩擦衰减 / 死区归零接共享 t863① tryStallSlideback；坡向分类同空车 = 邻轨层差 + t939 本格面梯度覆盖、负速跟行进向）；railPowered 从 targetV 块提出（t810 四分支语义不动）、有输入仍走 lerp（t667 不动）、空车路径零触碰（t909/t735④/t939 探针原样）。**② 三症状各自根因与修法**：〔卡出 V 到隔壁〕clampShift 近层闸（review28 #7 |Δ层|≤1）只验「目标列有轨」不验「本链延续」——跨线立体同列的下线/桥下线轨被当坡面延续（宽容列扫自 topY 向下先摸到**下线**轨 Δ=−1 过闸）→ 去穿插把车写进下线列、下一帧 pinCartY 沿同一列扫钉到下线轨 = 跨链跳线；修 = **链可达闸**（本格持位移向连接位〔pickTrackStep/t937 同一物理连接权威〕+ 自 rySelf 三高 railProbeDelta 层差 == ryTgt−rySelf，任一不成立钳回本格边界；正常行驶跨格走 stepCartAlongRail 逐格心重选不经此闸）。〔挤压颤抖卡死〕冲量模型对撞分支把对撞两车沿 n 反向弹开（各 ~0.7|closing| 反向速），坡面 t909② 静置 kick（±1.0）/ t863① 反溜（−0.5）下一 tick 回灌 → 弹开-回灌**极限环**；修 = **持续挤压对速度一致性**（位移被钳未分离且解算后仍重叠 → 沿 n 追得更快一侧速度钳到被追侧、限幅 ±boost，下帧无相对逼近不再互撞，对以耦合速度整体脱困）+ **钳向清速**（被钳一侧不得持指向钳制边界的速度——stepCartAlongRail 段内位移无跨格校验〔格心才重选〕，破之则滑过钳制边界入墙/无轨列 → pinCartY 失轨坠落；旧代码此不变量仅由 impulseDirOk 冲量向守卫**间接**保证，开发中 match 首稿复破 t907(a) 实证：钳边车被赋 0.55 b/s 入墙速 ~6 tick 滑穿 1 格厚墙坠落——现显式化、只清穿入分量〔lessons「闸门只挡穿入速度」〕）。〔横着卡在坡上〕呈现面收口 = 解析收尾对全部轨上态车重钉 cartYawFromDir(dir)（yaw 恒沿轨轴、幂等；derailed 自由体豁免）。〔附加：V 闭合终端速度〕下坡收敛**双向化**（空车路径 + 载人 coasting 同改）：高于滑档的残速（boost 出段/碰撞获速/**去穿插沿坡免费抬升积累的势能**——能量棘轮：满落差 5 格达 14.07 b/s > 对臂顶心起飞阈 13.68）按 kCartFriction 衰减回 kCartSlopeDownSpeed = 滑档成系统真终端速度、V 成闭合捕获系统；≤10 常规振荡零触碰（t909 逐字节不变）。**探针 P-t943 四腿**（5 臂 V rig：捕获阈 √(2·19.8·5)=14.1 > 冲量钳 12.8 = 闭合系统 + V 底两格通电动力轨）：(a) 空车参照 1500 tick 反转 ≥6 + 臂上爬行 tick（|vApp|<0.5）≤90（实测 rev18/crawl45）；(b) 载人同位 spawn+tryMount 无输入同驱——同阈全绿 = 载人曲线与空车同物理（旧代码恰本腿红：rev1/crawl1226 + 出 rig = 用户症状签名）；(c) 4 空车（谷底×2+两臂半山）+ resolveCartCollisions 同帧 1500 tick——全程存活/恒贴轨线（|z−心|<0.02）/ yaw 恒轴向（fmod90°±0.5）/ 含留 / 末 400 tick 每车路程 ≥1.0（无冻结无颤抖死锁）；(d) 七枚源码钉（coasting 闸/链可达两行/match/钳向清速两行/yaw 重钉）。**阴性两轮**：coasting 还原 → 恰 (b) 红且签名 = 爬行+出 rig（空车参照照绿）；钳向清速还原 → t907(a) 笼挤压坠墙签名复现 + 源码钉红；链可达闸/match 无确定性 headless 行为窗（跨线立体/深挤压无 staging 面）→ 源码钉覆盖。**矩阵 357→358**（357 基线 + P-t943）；t943 与全部矿车/物理探针历轮全绿；t891（火充能直燃，pumpFor 实时钟敏感）在当前机器负载下多数复跑挂红——**clean HEAD 同挂**（matrix_head_check.log：t882+t891 红 @84f4c1e）且本树有复清轮（matrix_t943_p4.log：仅 t927 一次）→ 按「复跑清再判」判既存环境抖动、与矿车物理无关。编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视（实机复现类）：V 形轨道上车松手往返，顶点换向应干脆无爬行；多车同 V 长跑不漏车/不横姿/不挤压颤抖；跨线立体交叉桥旁挤车不跳线。
**t944** 上坡顶方块阻挡：上坡处上方放方块 → 矿车**被挡住**不能穿墙过去（移动积分对坡向阻挡格的碰撞）。 ✅✅（b7fcb1c）**根因（轨态推进 = 无碰撞「轨道特权」通道）**= stepCartAlongRail 只受轨连接位约束、从不读世界碰撞（对照：脱轨自由物理 tickDerailedCart 有撞墙清速）；上坡段车体随 railRiseAt 梯度面升高——车顶 = rise+0.9 在 rise>0.1 起进入**坡格正上方格**，用户在该格放的方块（坡顶上墙）被直接穿墙。**修（提交位探测 + 上行闸 + 二分回钳）**= stepCartAlongRail 每子步位移提交后对候选位做 cartBodyBlockedAt（车体 AABB 按行进轴定向〔长轴 kCartHalfL，checkCartEnvironment 同口径〕+ pinCartY 钉候选位 Y——上坡升后车体格自然覆盖阻挡判定；覆盖格 isCollidable 快筛 → World::collisionAABBsAt sub-AABB 严格重叠〔贴面接触不算，贴墙停驻不抖〕——世界碰撞盒单一权威、禁第二套盒表），**仅上坡向位移启用**（本格面梯度沿行进向 >kCartSlopeGradMin = t939 同一张面同阈；梯度跟行进向不跟车头，负速倒行对称）——下坡/平移的重叠不拦（用户口径「下坡方向不做额外阻挡」：下坡穿顶属既有低顶净空〔紧凑螺旋〕延续语义；平移重叠几何上不存在——平轨车体格只含轨列而轨非碰撞体）；采样失联（死端前探/拐角垂直臂）同不拦（deadEnd 停车/飞出保留管辖）。命中且有自由锚 → clampRailMoveToFree 沿行进轴在〔子步锚点,受阻位〕二分收窄 6 轮（~1/64 格分辨率）+ 就地重钉坡面 + 速度清零（t943 钳向清速同口径：穿入分量 = 全部沿轨标量速）；**不掉轨**（仍轨上态贴在坡下侧，Y 由 caller 既有 pinCartY 钉定），移除方块后从静止被动力轨/推力/骑乘输入自然恢复。每 tick 一次入点位锚探测（上一次提交位已被本探测保证自由、tick 首位无此保证——方块可被外生写入车体〔放置进实体/落沙掩埋〕）；嵌入车逃逸豁免（重叠期放行移动 + 锚点记不自由，防永久冻结）。探测全程只写局部副本 → 无障碍轨迹逐位不变（t909 V 探针零适配）。**探针 P-t944 六腿**（EW 低平+坡格+高平 rig，阻挡格 = 坡格正上方 Stone）：(a) 上坡被挡停在坡下侧（x0+1 < x < x0+1.45，接触面 rise≈0.1）+ 轨态保持（Y 恒钉轨面）+ 持续 W 稳定不穿墙（20 tick 漂移 <0.05）；(b) 移除方块 → 同车恢复通行到高平死端格心；(c) 阴性·无障碍新鲜车静止起步全程通行不变；(d) 阴性·平轨 1 格净空隧道口（P18 几何 + 露天引道）照常穿行（车顶 0.9125 < 天花板底 1.0 恒不相交 = 不误拦合法净空）；(e) 阴性·阻挡格在场峰上 spawn 车向西下坡穿阻挡格列直达低平死端（「下坡方向不做额外阻挡」口径钉住，防上行闸被未来简化掉）；(f) 七枚源码钉（入点锚/子步探测调用/上行梯度闸/回钳调用/两函数定义/头文件声明）。**阴性轮**（上行闸禁用）：恰 (a) 红且签名 = 穿墙到顶死端格心停驻（用户症状复现）+ P21 相1 红 + 闸源码钉红；复原 359 PASS / 0 FAIL。**矩阵 358→359**（358 基线 + P-t944）：**P21（review#2 低顶净空坡道）按 t944 用户最新口径重定scope**（t931/t941 翻案先例，注释留痕）——坡格正上方贴顶实心 = 用户「上坡处上方放方块」的同一格，旧「贴顶穿越」承诺废止：相1（天花板在场）钉被挡但轨上态停驻（宽容列扫「坡上车位居上不失联」的另一承重面由停驻态延续钉住），相2（清顶）跑原 review#2 全套断言（Y 钉定/俯仰连续且钳 45/坡中段 ~+45/顶死端停稳守卫）兼作 t944 恢复通行腿；(c) af9ec8e 隔板防线腿不动。其余矿车/物理探针零适配全绿（P12b/t863/t909/t939/t940/t943 皆露天或清空 rig；P18 平轨隧道不受影响；t882 本轮复跑清）。编译零警告（dxcompiler 豁免；minecartmanager.cpp 过 -Wall -Wextra -fsyntax-only）；exe 冒烟 12s 存活。待目视：上坡顶上方放方块，矿车上坡应顶在坡下侧停下（不掉轨不抖动），挖掉方块后车可被推/动力轨/骑行恢复通行；下坡穿顶（紧凑螺旋）行为不变。
**t945** 仙人掌旁放铁轨返修（t911 未愈）：现在**放不了**——用户要的是「放置成功且仙人掌被破坏掉落」（自动下矿车系统前提）。核放置预检为何拒绝（checkCactusOnEdit 方向反了？）。 ✅✅（0a6014a）**核验结论：任务假设被行为级证伪**——放置预检链（playercontroller placeBlock 全段逐行核）**不含任何「邻仙人掌 → 拒」条件**，checkCactusOnEdit ④ 也无方向反转（它在 setBlock **成功后**反应、只毁仙人掌不拒放置）；玩家全链（loadSavedState 定向 → tick 刷射线 → placeBlock 预检 → setBlock → World ④ 整柱坍落）在柱基旁合法支撑位**全通**：瞄柱旁地面顶面 / 瞄 0.8 细柱侧面 / Survival hotbar 铁轨栈（t669 消耗 1）三口径皆放置成功 + 整柱坍落掉落 + 轨留存。**唯一可达的拒绝路径** = 轨支撑预检②（`!isFullCube(below) → return`）：瞄 2+ 高柱**上层**侧面 → 目标格悬空（野生柱 worldgen 4 邻守卫保证柱旁地形不高于柱基 → 上层邻格下方恒 Air）→ 拒且**拒放不触发坍落**（仙人掌无恙）= 用户定稿口径「对轨本就非法的位置照旧拒绝——仙人掌坍落不是非法放置的免死金牌」+ MC 同构（rail 须支撑）。第五轮实测「放不了」与此一致：2-3 高柱的上层格占准星视野主导，瞄上层即拒（瞄基座即成）。**探针 P-t945 八腿**（玩家全链行为级，矩阵首例 placeBlock 直驱：t814 真消费端 + review27-8 挂窗 grab；**每腿 release+grab 重居中光标**——pollMouse 每捕获 tick 读 QCursor::pos−窗心差改写 yaw/pitch，headless 残留位差被一次性吞成视角踢变〔首轮 c/d 假红的根因〕，重居中归零后 loadSavedState 的 yaw/pitch 原样进射线）：(a) 地面顶面瞄准 → 放置成功 + 2 高整柱坍落（两格 Air + 各一次 blockDroppedAsItem）+ 轨留存；(b) 细柱侧面瞄准 → 同格同果；(c) 阴性·上层侧面拒放且仙人掌无恙零掉落（免死金牌禁令钉住）；(d) 阴性·空场悬空轨位照旧拒（断言命中格本体，防「射线落空」假阳性）；(e) Survival hotbar 铁轨 ×16 → 放置 + 坍落 + 消耗 1（t669 链走通）；(f) 对称面·火把贴柱旁放置 + 坍落（薄格非实体族同口径——无任何族被非法化）；(g) 对称面·石头挤占柱旁放置 + 坍落（t445 ④ 非空门全族覆盖，钉口径防漂移）；(h) 源码钉（轨支撑预检行 + dropCactusColumn 调用行）。**阴性两轮（各半边独立验证）**：World ④ 坍落调用禁用 → 恰 (a/b/e/f/g) 红且签名 = 放置仍成功（tgt 103/13/3）而仙人掌无恙零掉落 + (c/d) 阴性照绿 + t911 同红（证明放置链从未依赖坍落、坍落链可被独立捕获）；轨支撑预检禁用 → 恰 (c/d) 红且签名 = 悬空位被放置 + 整柱照坍（免死金牌回归）+ (h) 红；复原 **360 PASS / 0 FAIL**（矩阵 359→360，359 基线 + P-t945；t891 环境抖动复跑清）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。**注释契约同步**：轨预检块（playercontroller.cpp）记「无非法化条件 + 唯一拒绝 = 支撑② + 上层瞄准口径」、checkCactusOnEdit ④（world.cpp）记「放置成功后反应 + 被拒放置仙人掌无恙」。**待目视**：实机瞄仙人掌柱基旁地面放轨 → 轨落 + 整柱倒 + 掉落物；瞄柱上层拒属支撑语义非 bug（用户定稿口径）。

### 🅲 生物组（t946-t952）
**t946** 狼坐姿返修（t878 sitPose bug）：身体翘太高、身体与躯体**分离中间透明**——几何变换链断开，重调坐姿分段变换。 ✅✅（a6a9610）**断链点结论（t878② 各段独立世界坐标 vs 躯干独绕枢）**：旧坐姿只把躯干绕枢 (−0.26,+0.24) 转 +40°，头/耳/四腿仍钉在各自绝对坐标——① 躯干抬起后底面从髋区 −0.11 升到 0.09..−0.14，而臀下折叠腿顶只有 −0.14 → 髋带悬空缝 0.10..0.23（「分离中间透明」实测列签名：z∈[0.16,0.36] 带内空隙 0.110-0.234）；② 胸前顶角推到 y≈0.48 反压头心 0.30（「翘太高」——头顶被胸埋）；③ 前腿顶 0.05 够不到抬起后的胸底 0.25（前带悬空 0.20）。**修（臀部着地点单根锚链派生）**= mobmodel.cpp 狼/豹猫坐姿分支重写：根锚 kSitRootY/kSitRootZ（狼 −0.14/+0.36、豹猫 −0.12/+0.32 = 折叠大腿块顶面着地高）+ 躯干后仰 kSitPitch 18°（用户 10-20°「不翘太高」带）+ sitRot lambda——躯干随动段（头=旋转后颈附点 + 净 +10° kSitHeadNet 微仰；耳=头偏移同角旋转）一律由根锚派生、禁独立世界坐标；后腿折叠为臀下大腿块（顶 0.00 沿 z[0.10,0.38] 全线嵌入旋转后躯干底）+ 贴地折叠前爪；前腿垂直加长（狼 0.54 / 豹猫 0.49）嵌入抬升后的胸底。剪影：狼胸顶 0.39/头顶 0.47/耳顶 0.57（总高 ~0.99 ≈1 格坐狼）；豹猫胸顶 0.35/耳顶 0.50（0.90 坐猫）；竖尾锚在链派生尾根位贴臀。**探针 P-t946**（矩阵 360→361；真几何顶点行为级——MobModel 直编读 vertexData，t880 ItemShapeGeometry 先例；**mobmodel.cpp 首次链入矩阵 target**，review24「只用头 constexpr」口径在注释内明示放开）：逐列覆盖 = 法线定向穿透深度并集（**奇偶法不可用**——嵌接关节是重叠实体，重叠段深度 2 ≡ 偶被误判外部；本探针首轮即踩此坑，box-区间并集 rig 对照定位后改法线定向深度计数）+ (a) 坐姿 minY=碰撞底（狼 −0.42/豹猫 −0.40±0.03）(b) maxY 上界 0.62/0.58（防再抬高）(c) 髋带 z[0.16,0.36]/[0.10,0.30] 逐列触地 + 带内空隙 ≤0.075（断链形态 0.11-0.23 必红）(d) 前腿带 z[−0.30,−0.20]/[−0.26,−0.18] 同判 (e) 站姿剪影界零回归（狼 [−0.42,0.37]/豹猫 [−0.40,0.24]——豹猫耳 x∈[0.03,0.09] 不含采样列故列顶=头顶）(f) 源码钉（kSitRootY/kSitRootZ 值 + 躯干枢轴引用 + sitRot 颈附链 + 大腿块/尾根锚绑定 + Main.qml/ResourceBrowser.qml 眼/项圈/尾成对契约新位：狼尾根 (0,0.14,0.47)/项圈 (0,0.35,−0.175)/眼 (±0.08,0.40,−0.49)、豹猫眼 (±0.07,0.36,−0.42)，站姿位全部不动）。**阴性轮**：狼+豹猫躯干变换回退 t878 枢轴 → 恰 P-t946 红且签名 = 前带空隙（狼 0.075-0.080/豹猫 0.105-0.113）+ 根锚源码钉红；复原绿。**附加核验**：坐姿几何三消费端同源（游戏内 delegate / 图鉴坐下预览 / 豹猫=同 MobModel——t963 的坐姿面随本任务闭合，其贴图串色仍归 t963）；AI 侧坐=留守 moveSpeed=0（拴绳/跟随不会把坐姿变换拖进运动帧）、坐/站切换仍用户指令驱动零改动。**矩阵 361 PASS / 0 FAIL**（361 = 360 基线 + P-t946；t891 环境抖动按 clean-HEAD 对照实证与本任务无关：clean HEAD 连跑 3 轮 t891 挂 2 轮〔另 t882/t927 各 1〕、本树终轮全绿）。编译零警告（dxcompiler 豁免；mobmodel.cpp 过 -Wall -Wextra -fsyntax-only）；exe 冒烟 12s 存活。**待目视（实机复现类）**：驯服狼/猫空手右键坐下——臀落地、胸微抬不夸张、头在胸顶微仰看玩家、前后腿与躯干无透明缝、坐狼尾下垂搭地、坐猫竖尾贴臀；图鉴「坐下」预览同观感。
**t947** 狼三修：① 跟随门=创造/生存跟随、**观察者模式不跟随**；② 伤害太高（两口打死穿甲僵尸）——下调至 MC 1.0 量级（狼 4 HP/口一带）；③ 攻击被方块挡住不会跳——chase 寻路越障跳（isJumpObstacle 接入狼 chase）。 ✅✅（1d76f9e）**① 跟随门**：aiWolf 增 playerSpectator 通道（PlayerController 传 mode==Spectator，同 playerTargetable「Game 层派生 bool 向下传」先例，Entities 不反查玩家模式）——观察者 → 整段跟随跳过（走近 + >12 格瞬移补位一并停：瞬移是跟随段的防掉队机制，门罩段即双双停），回退 aiWander（同 t290 不可锁定回退游荡先例）；门放防御/寻偶分支**之后**——防御追击与求偶是 mob-mob 语义不随主人模式翻转（观察者下狼被打仍反击）；创造/生存（false）零变化。**② 伤害口径 + 护甲链核验**：kWolfAttackDamage 维持 4 并钉为用户第五轮口径（头注释从旧「MC 1.0 ~3 心=6HP」数值式改写为用户口径）；**护甲减伤链核验结论：被咬方不存在可走的减伤链**——t377 mob 随机护甲自始即「仅视觉、不参与 mob 减伤计算」（spec 明文），damageEntity 原值扣血，全工程唯一减伤链（armor.h + Main.qml 路由）只服务**玩家受击**——故常量即穿甲僵尸的每口实伤：20HP − 2×4 = 12 > 0，满血穿甲僵尸两口**不可能**咬死（用户观察应为残血目标 / 多狼共享 m_wolfTarget 群咬的聚合观感），非「护甲对 mob 攻击不生效」的新 bug 而是既有口径。**③ 越障跳重构**：t923 版 chase 把整段跳门在 `!moved && distXZ > 0.6f`，两洞各漏一类攻击场景——①斜向滑墙单轴恒可动 → moved 恒 true 永不探跳（贴墙溜且纵向抖动步长恒非零、等不到「撞全停」拍）；②隔墙贴脸目标 XZ ≤0.6 被距离门短路——两型都是用户「攻击被方块挡住不跳」。改回 **aiHostile 主动判定同款**（resting 且朝目标移动即每 AI tick 探前方脚位，先于移动、独立于 moved 与距离）：isJumpObstacle 单一权威（作物/矮支撑豁免沿用）+ 墙顶两格净空 → kJumpSpeed + t670 朝目标滑流；无墙恒 false 不原地蹦。泳跃保留「被挡才跃」语义、同摘 0.6 距离门（主人贴岸俯视 XZ 可 ≤0.6 会拦死最后一步上岸）。**探针 P-t947**（矩阵 361→362；独立凿平小世界不碰共享分配器，t923 同款）：(a1) 观察者距 6 → 1.5s 主狼距 ≥4.0（游荡漂移上界 1.5 vs 跟随 2.5 停步带——两行为带不相交，游向 RNG 无关）；(a2) 观察者距 15 → 不瞬移（≥10；瞬移会落 ≤7）；(a3) 创造/生存对照收进 ≤3.0；(b) 事件驱动逐帧盯血至恰两口：tier4 全套穿甲 20HP Shambler 逐口 ==4、终血 ==12（总伤 8 < 20 存活）+ 头文件源码钉「kWolfAttackDamage   = 4」（私有 constexpr 测试 TU 不可直读 → t923(d) 文件读先例，钉声明形态防注释同名词误匹配）；(c) **两列厚**全深 1 格高墙（厚 2 > 咬距 1.6 → 隔墙咬几何不可能）：防御追击狼越墙（中心 x > 24.2）+ 越墙后咬击掉血。**阴性三轮各独立独红**：门禁用 → (a1) 跟上 dist 2.42 + (a2) 瞬移 dist 2.24；4→10 → 逐口 10/10、hp 0 + 源码钉红；主动探跳禁用 → 贴墙 x=21.52 整 48s 帽不越墙零咬（用户症状复现）；复原 **362 PASS / 0 FAIL**（t891 一轮红复跑清 = 既有环境抖动）。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活。**待目视**：G 切观察者后驯服站狼原地游荡不跟随不瞬移、切回创造/生存恢复跟随；驯服狼隔一格矮墙/台阶咬敌对会起跳翻越后继续追咬；狼咬满血穿甲僵尸约 5 口才倒。
**t948** 狼攻击仇恨转移核验：狼咬敌对生物后敌对应转向攻击狼（t923 反击注册面核——狼主动攻击也要注册被咬者对狼的仇恨，不只狼被打了才反击）。 ✅✅（本次）**单一入口结论**：工程内 mob→mob 仇恨注册面此前只有 t923 `wolfRetaliateAgainst`（狼被打 → 狼群反击，骷髅箭/火球两接线点）；敌对侧**没有**仇恨目标字段（玩家攻击怪不注册——敌对恒追玩家无需注册；玩家不在 m_entities 槽位也无法被槽索引引用），故新建注册单一入口 `mobAggroAgainst(victim, attacker)` + 消费单一入口 `resolveAggroTarget(e)`，狼咬击命中处（aiWolf 防御分支 damageEntity 后）调前者——与 wolfRetaliateAgainst 构成**双向仇恨面**（狼被打→狼群反击 / 狼主动咬→被咬者转火）。**slot+serial 双快照**对齐骷髅箭 arrowShooter/serial 槽复用先例（Entity 末尾区新增 aggroIdx/aggroSerial，t256 元教训）；仇恨不设时限不设距离（MC revenge target 持续到目标死亡——死亡/槽复用换任经 resolveAggroTarget 清 -1/0 落回玩家路径，同 m_wolfTarget 攻击者死亡回落先例；驯服狼瞬移回主人身边出侦测范围→本 tick 落回玩家路径但仇恨保留，狼回范围即恢复追狼）。**目标优先级**：仇恨目标 > golem 视线目标（t712 分支）> 玩家。**消费面**：aiHostile（Shambler/Spider，含 Silverfish 同分发面）追咬+近战（照抄 golem 分支体例：逐轴 AABB 撤回+越障跳+垂直同层，近战命中调 wolfRetaliateAgainst 保 t923 反击面接线完整——互咬循环下去重 no-op）；aiArcher（Bones）保持距离带+拉弓**射击狼**（照抄 golem 射击分支；箭命中狼走 t712/t923 既有结算名单 damageEntity+wolfRetaliateAgainst——**t712 滤网语义核验：它是「骷髅箭 mob 结算名单」（仅铁傀儡/狼受伤，其余穿透），非「骷髅不射狼」，与近战转火天然兼容，未改一字**）。**语义边界**：Stalker（spec 只锁玩家不对生物自爆）/Nightwalker、Emberling（独立 AI）入口门排除（t712 同界）；被动七型无仇恨系统入口门 no-op（被咬猪/羊逃跑链不变）；无群体 aggro 传播（只有被咬者转火，MC 语义）。**探针 P-t948**（矩阵 362→363；独立凿平小世界 t947 同款）：(a) 近战腿——玩家在僵尸侦测圈内、setWolfTarget→狼咬（首口落地=注册面前置）→ 僵尸还手（狼掉血=僵尸近战是本世界狼唯一伤害源且只有仇恨分支近战 mob）+ 贴身 ≤3.0，事件驱动双证齐即停；(b) 远程腿——狼咬骷髅→骷髅转火射击狼（箭命中狼 hp<10）；(c) 阴性·被动——狼咬猪、猪恒不还手（狼满血 10）；(d) 源码钉 `mobAggroAgainst(m_wolfTarget, idx)` + `wolfRetaliateAgainst(aggroIdx, idx)`。**阴性轮**：回退咬击点注册 → P-t948 独红（a bitten=1 fought=0 狼满血 10 / b bitten=1 shot=0——「咬后仍追玩家不还手」复现；t891 同轮红=既有环境抖动），复原 **363 PASS / 0 FAIL**。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活。**待目视**：生存模式驯服狼咬僵尸/蜘蛛后僵尸改追狼互殴；狼咬骷髅后骷髅后撤拉弓射狼；狼被骷髅箭射中狼群仍反击咬骷髅（t923 链不回归）。
**t949** 豹猫两修：① 鱼肉驯服没用（MC 1.0 豹猫用生鱼驯——查驯服物品判定为何不吃鱼肉/生鱼 id）；② 3D 贴图**混入狼的灰色贴图**——贴图源串了（查看器/游戏内 delegate 纹理绑定错）。 ✅✅（ab48c69）**两根因都不是 id 表**。**① 输入翻译缝**：placeBlock 生鱼分支 gate `RawFishId` 判定本就正确（t481 单一权威无缺口）——缝在 eventFilter 右键链的**进食优先分支**（`foodHungerAmount>0 → beginEating + return`）：生鱼/狼肉**既是食物又是 mob 交互材料**，每一次 live 右键都被进食分支先行吞掉 → placeBlock 内生鱼驯服分支与狼肉分支永不可达（t514 甜浆果 / t639① 胡萝卜马铃薯的「使用方块优先于进食」分流只盖了种植、没盖 mob 喂食；旧矩阵探针直调 tameOcelot/EntityManager，测不到输入翻译层）。修 = 同址补**喂食分流**（复用 placeBlock 各喂食分支同一条 findMobHit + kReach 射线）：生鱼+命中豹猫（野/驯都算「对实体使用」，驯服/回血/求偶由分支自分流）、狼肉+命中**已驯服**狼（野狼不吃肉）→ placeBlock；未命中 → fall-through 进食。**顺带登记（任务③喂养回血面）**：t480 狼肉喂养面（驯服狼喂肉回血/求偶）同缝同病——七种肉全是食物、live 同样不可达——由同一 gate 一并修通（行为腿 c 证：驯服狼 6HP 喂生牛肉 → 恰 10 + 耗 1）。**② 图鉴驯服猫预览双开关漂移**：唯一能显灰的豹猫面（程序/包豹猫源像素实测全为橙棕斑纹；游戏内 delegate 的 ocelotPackHit 自带 `!ocatTamed`；刷怪笼迷你态恒野生）= ResourceBrowser 3D 预览的 packTextured（t780「pack 命中 → box-UV 几何」）与 t920「驯服猫 → 程序全脸 mob_cat_*」是**两个条件不同源的独立开关**——开包 + 驯服拨杆时几何以 box-UV 窗采程序猫贴图任意像素 = 暗灰斑驳（读作「混入狼的灰色贴图」）。修 = packTextured 门加与贴图切换**逐字同条件**的驯服猫例外（两消费端一致；t963 的查看器花纹面不越界）。**探针 P-t949**（矩阵 363→364）：(a) 真实输入链行为腿——PlayerController 挂 headless QQuickWindow（t891 grab 先例）+ 合成右键 press 经 QCoreApplication::sendEvent 投递进窗（onWindowChanged 里 pc installEventFilter 于窗 = 用户真实投递链；override 是 protected 虚不可直调）+ 生存 64 生鱼 + 野豹猫 2.74 格前 → 40 次帽内驯中（~1/3 RNG；0.67^40≈1e-7 残差）+ 变体 0..2 + 生鱼消耗；(b) 阴性·熟鱼不驯（t836 只吃生鱼口径）+ 鱼不耗；(c) 狼肉回血面过同一 gate（见上）；(d) ②映射单源 + 两消费端源码钉：mobEntityMap() 直调 11→cat/ocelot.png 与 10→wolf/wolf.png 两源互异、Main.qml 钉 `!ocatTamed &&` pack 命中判据 + baseColorMap 收口永不落狼、ResourceBrowser.qml 钉 packTextured 门含驯服猫例外 + t920 贴图切换并存。**阴性两轮各独立独红**：A 禁用 gate → 恰 P-t949 红（a tamed=0 consumed=0 = 用户症状复现；c hp=6 count=64；pinGate=0）；B 回退查看器门 → 恰 pinBrowser=0；复原 **364 PASS / 0 FAIL**。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活干净。**待目视**：生存手持生鱼对野生豹猫右键 1-3 次内变猫（随机毛色）+ 空手右键坐/站；受伤驯服猫喂生鱼回血；驯服狼喂生/熟肉回血/求偶；图鉴开包 + 豹猫条目拨「已驯服」预览为全脸黑猫贴图（无灰斑串采）。
**t950** 装备拾取穿着系统：僵尸/骷髅**经过**装备掉落物时有概率拾取并穿上（不主动寻路；玩家死亡掉落被路过捡起穿上——按槽位规则：更好护甲/武器才换）。 ✅✅（f087c8f）**归属定盘：扫描放 Game 层 PlayerController 而非 EntityManager**——拾取判定需要 Game 层注册表知识（ArmorRegistry 护甲点数/部位、ToolRegistry::attackDamage）+ 掉落物表（ItemEntityManager），Entities 不得向上依赖；照 updatePressurePlates 先例做「Game 层桥接扫描双管理器」（mob 侧读 posAt/mobTypeAt/halfHeightAt/mobArmorAt，掉落物侧读 aliveAt/isPickupReady/posAt/itemIdAt），Entities 侧只添**哑数据写入口**：`equipMobArmorPiece`（piece 0..3，返回被换下旧 id 供 caller 掉地，拒绝返 -1，notifyEntitiesChanged → t377/t719 护甲壳视觉链 {revision; mobArmorAt} QML 绑定自动重算 = 穿上即可见，零渲染层改动）+ 新 `Entity.heldItemId` 尾部字段（t256 元教训，DMI + 整体 move 入槽 → 槽复用自动清 0）配 `mobHeldItemAt`/`equipMobHeldItem`。**口径登记**（用户未明说面，取稳从简，实现头注释全录）：① 概率 = 每 0.5s 扫描窗每件接触装备独立掷骰 0.3（MC 1.0 无此概率面——canPickUpLoot 必拾；0.3 = 路过偶拾、连续经过必拾 1−0.7ⁿ），0/≥1 两端为行为钉（setEquipmentPickupChance 缝）；② 「更好」=**严格更大**：护甲同部位比 armorPoints、武器比 attackDamage（剑+斧 > 徒手算武器；弓/钓竿/镐铲锄=徒手级不算），严格全序有限档天然无环且钻石封顶 → 无需「每槽换一次」次数帽（严格序已覆盖防循环）；③ 换下旧装备掉回 mob 脚格（spawnItem 自带确定性弹出 + 0.5s 免拾窗 → 同窗不回吸；被另一 mob 拾走属正确语义）；mob 装备槽 id-only（t377 遗产）→ 附魔/耐久/改名实例元数据不保真、mob 死亡不掉装备（既有登记缺口不扩）；④ 武器拾取=**数据登记面**：heldItemId 入槽按同规则换、旧武器掉回，但 mob 攻击力维持 AI 常量不加成（用户口径未要伤害面）、骸骨弓是 AI 固有不入槽、QML 无 mob 手持物渲染端（视觉留未来项）；食物/材料不拾（MC 僵尸会捡食物——登记未来食物面）；⑤ 门：mobType ∈ {Shambler, Bones}、活体（healthAt>0，死亡动画窗不拾）、isPickupReady（同玩家 0.5s 免拾窗）、水平 1.0 格 + 竖直脚平面 −0.5..+1.5 窗（同层地面/上一格台阶可达、坑内不吸）、每 mob 每窗至多一件。驱动挂在 tickImpl pickup 桶（pickupScan 同族常开；上方 worldRunning 总闸 → 硬暂停期冻结不拾），0.5s dt 累积窗节流。**探针 P-t950 六腿**（矩阵 364→365；PlayerController 直造不启 16ms tick + 双管理器注入，实体物理不 tick——mob 定格掉落物所在格 =「经过」接触稳态、游走 RNG 不进断言，扫描窗直调 tickMobEquipmentPickup(0.5) 定步长递推〔= tickImpl 喂真 dt 的等价累积，接线行由 (f) 源码钉覆盖——t948(d) 先例〕，500ms 免拾墙钟 msleep 真睡越过）：(a) chance=1 裸装 Shambler 站铁胸甲格 → 1 窗穿上（armorChest 变铁胸甲 id）+ 掉落物消失 + 空槽无回掉；(b) 更好规则腿——铁套路过皮革胸甲（差）6 窗不拾（身上不变 + 掉落物留存），路过钻石胸甲 1 窗换上 + 被换下铁胸甲回掉为新地面活体，陈化后续窗不回吸（严格序起效而非免拾窗——防弱断言假绿）；(c) 武器腿——裸手拾木剑入 heldItemId + 地面消失，铁剑换持 + 木剑回掉 + 陈化后不回吸（4<6），骸骨同规则拾木剑（攻击力不加成=登记取舍无行为面）；(d) chance=0 八窗恒不拾；(e) 非装备不掉腿——生猪排（食物）+ 弓（attackDamage=徒手非武器口径）恒留存；(f) 源码钉七枚（tickImpl 接线行/类型门/护甲与武器严格更大比较行/removeAt 行/旧装备掉地行/heldItemId 字段行〔entitymanager.h〕/Entities 侧类型门）。**阴性轮**：扫描入口禁用 → 恰 P-t950 红且签名 = a/b/c 全零拾取（worn=0/wood=0/bHeld=0；b 腿 worse=1 照绿 = 扫描不跑时「不拾」消极面天然成立）；同轮 t882 挂红 = 既有环境抖动（复跑清）。复原 **365 PASS / 0 FAIL**。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活干净。**待目视**：生存模式丢一件铁甲在僵尸/骷髅游荡路径上，路过时应偶发（多次经过必发生）穿到身上（对应部位出现护甲壳贴图）且地面掉落物消失；手持更好剑的它遇更差剑不再换、换下的旧装备落在原地可捡回。
**t951** 白天阴影 AI：僵尸/骷髅白天**优先找阴凉保命**（现在太阳伤害驱动的追玩家会抽搐——来回转向走出/退回阴影）；等玩家进阴影才发起攻击；骷髅可在阴影内射箭（走位不出阴影）。 ✅✅（9a69e39）**抽搐根因（t670 版结构极限环，非参数问题）**= 寻影由 `e.burning` 驱动，而 tickHostileLife 在身体格 skyLight 跌破 15 的**当拍**即清 burning → 进影即停燃 → 旧版 else 分支立清阴凉目标回追玩家 → 一步出影复燃 → 又寻影——追击与避光向量逐 AI tick 交替占优 = 用户「来回转向走出/退回阴影」。**修（三件套，只动 aiHostile/aiArcher 玩家目标路径；仇恨狼/铁傀儡转火是 mob-mob 语义不随日光翻转〔t947/t948 探针依赖〕；夜间整段旁路行为逐位不变）**：① **灼烧采样单一权威**——tickHostileLife 内联日光判定提炼为 `sunBurnExposureAt` 静态成员（界内 + 身体格 skyLightAt≥15 + 白天亮度门 + t385 降水豁免 + t561① 水豁免；**豁免=视同安全**），燃烧扣血 / 寻影机 / 自身判定 / 玩家暴晒判定四处同源（禁第二套光照判定），审查修 B6 白名单同提炼 `undeadBurnsInDaylight`（仅 Shambler/Bones；Spider/Silverfish 零波及）；② **白天双状态机 + 迟滞**——暴晒（**先于点燃**，保命不等烧）→ 寻影优先（t670 findShadeTarget 机制沿用：目标缓存 + kShadeRescanInterval 重扫；**半径内无阴凉 → 兜底维持追玩家照旧**〔沙漠/雪原等无遮蔽 biome 照烧不冻结，登记取舍〕）；真遮蔽 + 玩家暴晒 → 持影停驻 + `attackSuppressed` 压近战攻击（等玩家进阴影才发起；玩家入影/入水/降水=安全即照常追击）；`Entity.shadeHoldTimer`（kShadeHoldSeconds=1.0s，遮蔽刷新/暴晒衰减，>0 视作遮蔽）压住光影边界一步踏出与天光传播瞬态的立即 180° 折返——「追击↔寻影」翻向最少隔一个迟滞窗 = 抽搐解药本体；③ **骸骨弓手走位不出影**——暴晒时寻影覆盖保持带；真遮蔽时保持带**移动候选落点暴晒即弃选**（可在影内挪位、永不踏进日光），射击门照旧（射程+视线+冷却）——远程无需接近即无需玩家入影，檐下骷髅可对露天玩家照射（用户口径「可在阴影内射箭」）。**接线**：`EntityManager::tick` 尾添 `float skyBrightness = 0.0f`（缺省=夜间语义 → 全部既有 7 参调用面〔全矩阵探针〕行为逐位零扰动）；PlayerController::tickImpl 传 `m_worldClock->skyLight()`（与 tickHostileLife 同源同帧）。**探针 P-t951**（矩阵 365→366；石檐 rig 经 setBlock 重光照出真 skyLight<15 遮荫，腿内先钉 rig 光照真值防伪绿）：(a) 暴晒僵尸（玩家置侦测圈加追击压力）走入石檐停驻 + 200 tick 稳定滞留；(b) 檐下僵尸 vs 阳光下 2.4 格玩家 ≥300 tick 零攻击不出檐、玩家入檐数 tick 内攻击发起（mobAttackedPlayer 计数）；(c) 檐下骸骨在 <keepMin 退避压力全程在场下 19s 定窗内箭落玩家身上且 floor(XZ) 恒不出檐格；(d) 夜间腿保留旧 7 参调用形态=零回归钉（暴晒僵尸照旧追击攻击）；(e) 源码钉十枚（迟滞刷新行×2 / 谓词定义 + ≥6 消费 / 白名单 + ≥3 消费 / 攻击压门 / 候选闸 / 声明+常量+字段 / 生产接线）。**阴性两轮（终版代码形上）**：状态机整体禁用 → 恰 P-t951 红且签名=用户症状（僵尸不寻影+咬阳光下玩家 / 骷髅出檐）；仅禁候选闸 → 恰 c 腿红（退避压力把骷髅推出檐）；复原 **366 PASS / 0 FAIL**（t882 偶发 flake 复跑清）。编译零警告（dxcompiler 豁免）；exe 冒烟 15s 存活。**待目视（实机复现类）**：白天野僵尸/骷髅见玩家先就近钻树荫/屋檐且不再来回折返；玩家站阳光下僵尸隔影观望不追不出、玩家进影即扑；骷髅在影内照射不迈出影界；入夜全部恢复现行凶性。
**t952** 小僵尸生物新增：<1 格高、移速快、可穿盔甲、**概率与小鸡组合成小鸡僵尸骑士**（小鸡驮小僵尸）；怪物蛋+资源查看器+贴图（程序生成 qrc 程序贴图先例）。 ✅✅（本次）**前情核验：鸡已存在**（t398 MobChicken=8，被动生成表 / 生物蛋 0x22C / 下蛋链 / baby 鸡蛋孵化全备）→ 骑士组合直接复用，不新增小鸡。**①新 mobType MobBabyShambler=19**（§9 区隔命名「小蹒跚者」）：碰撞盒 0.5×0.9（halfH=0.45<0.5 =「<1 格高」口径）、hostile=true、满血同敌对默认 20；AI 完全复用 aiHostile——追击基准速 chaseBase = kChaseSpeed × kBabyShamblerChaseSpeedMul（1.4，落「×1.3~1.5 快」口径带）统一缩放本函数全部三条追击路径（仇恨狼/铁傀儡/玩家，防单点漏改），近战 meleeDamage = kBabyShamblerAttackDamage=2（成体 3 的一半档，「快速低伤」口径）覆盖三条伤害出口（emit mobAttackedPlayer 含死因链）；晒燃白名单 undeadBurnsInDaylight 加白（t951 白天阴影 AI 随之自动生效）。**②骑士（小鸡骑士，mob-on-mob 骑乘最小实现）**：Entity 尾部双向链 `rideMob`/`mobRider`（同 rideCart 先例放 struct 末尾区，DMI+move 入槽槽复用自清）；组合时机收口在 **spawnMobCore 末段**（黑暗刷怪/生物蛋/刷怪笼全生成路径单一权威）——kChickenJockeyChance=5% 掷骰（**运行时缝写 setChickenJockeyChance**，0/1 端钉行为面，同 t950 setEquipmentPickupChance 先例）命中 → 同格 spawnMobCore 一只小鸡 + 挂双向链 + 骑手即时钉载具顶；**只生成时组合，分离后不再合并**（登记取舍）；**骑乘架构 = 冻结+挂载 pass**（「垂直物理权威单一」）：骑手在主循环沿 t811 rideCart 冻结口径全停（重力+落地扫描的 `vy<0→snap 支撑顶` 会把骑手从鸡背拽到地面穿模，故垂直物理必停；aiAccum 照累积），新 `tickMobMounts`（tick 尾、tickBreeding 后）三段式：双向对账（任一侧死/槽复用/链断 → 解除，小鸡死→小僵尸落地恢复独立 AI〔resting=false 重力接管〕，小僵尸死→小鸡恢复自主漫步）→ 骑手 AI 以 kAiTickInterval 错峰节拍消费累积（playerTargetable→aiHostile，观察者→aiWander，t290 同口径）→ 钉位（**载具 XZ←骑手 XZ**〔骑手 AI 位移权威、追击照常被 1 格墙阻挡——骑手盒底没入墙行〕、**骑手 Y←载具顶+清 vy**、载具 moveSpeed←骑手速度驱动小鸡腿摆、骑手 moveSpeed 清零=被驮不迈腿同矿车乘客口径）+ 钉位值真变帧 bump rideRevision（review26 #10 专用通道 60Hz 呈现）；resolvePlayerPush 成对豁免、tickVehicleRiding 登乘扫描跳过挂载组合（两套钉位权威不打架）；击退在挂载态不应用（vx/vz 留存解除后自然衰减，与矿车乘客击退不生效同口径，登记）；载具 AI 挂起但物理/下蛋照跑（骑乘中的鸡照常周期下蛋，机制等价 MC）。**③装备面**：t950 拾取类型门 / setMobArmorSet / equipMobArmorPiece / equipMobHeldItem / t377 spawn 随机甲五门同口径加白小蹒跚者（可穿盔甲；delegate 有 baby 尺码 ArmorLayerBox 甲壳——盔甲近原大「幼体戴成体盔」观感、躯干四肢 ×0.5、髋枢腿摆同构）。**④生成/掉落/经验面**：黑暗刷怪选中蹒跚者后 kBabyShamblerSpawnChance=5% 翻幼体（机制等价 MC 小僵尸稀有自然生成）；蛋 SpawnEggBabyShamblerId=0x25D（recipe 单一权威表 + spawnEggTint 染色行 + 创造调色板蛋区尾 + nameForBlock + MaterialIcon drawSpawnEgg babyshambler + 图鉴 mobTypeForEgg 0x25D→19）；刷怪笼解码白名单加 19（蛋右键笼改型可用）；掉落腐肉 ×1（成体 1-2+稀有的低档，无稀有）、XP 3（成体 5 低档）；死因同族映射 PlayerState.Shambler；/kill type 名 babyshambler；/mobarmor 人形门扩段；ambient 音同族采蹒跚者 clip。**⑤模型/贴图/图鉴**：MobModel mobType 19 幼体比例人形——躯干四肢 ×0.5、头 0.17³ 近原大（成体 0.22³）=「头大身小」幼体视觉语言（头少缩身多缩），碰撞腿底 −0.45 贴底、biped 反相摆腿同构、UV 采同张 64×64 humanoid 区；程序贴图 mob_baby_shambler.png（build_mob.py make_baby_shambler：亮一档黄绿幼体腐肉底 + 密霉斑 + 同族破布/缝合，qrc 程序贴图先例；pack 无幼体独立皮 → 恒程序贴图无 pack 分流）；kValidMobModelType 表行 + kValidMobTypeCount 20 + 矩阵 static_assert 上界互钉三级全过；ResourceBrowser 生物图鉴条目 + 预览缩放/居中 + 程序贴图回退；Main.qml Loader（红眼 + 全套 baby 尺码护甲壳 + 走相绑定）+ mobModelYOff + 刷怪笼迷你四表（缩放/居中/程序贴图/红眼）。**探针 P-t952 九腿**（矩阵 366→367；EntityManager/PlayerController 直造直调 t950/t951 先例；所有非组合腿先钉 chance=0 防缺省 5% 随机鸡污染）：(a) 生成表项 halfH=0.45<0.5<成体 0.90 + 敌对语义；(b) 移速快——同距 5 格追击同一玩家位 30 tick 位移 baby > adult×1.15（夜间语义 skyBrightness=0 旁路 t951 → 纯确定性追击无 RNG 路径）；(c) 可穿盔甲——t950 拾取链 1 窗给小蹒跚者穿上铁胸甲（chance=1 端钉 + msleep 越免拾窗）；(d) 骑士上端钉 chance=1 全组合——同格出现小鸡 + 双向链互指 + 骑手钉载具顶 + 10 tick 推进后钉位关系保持（XZ 重合 + Y 恒载具顶）；(e) 下端钉 chance=0 全独立——rideMob=-1 + 全场无小鸡 + liveCount=1；(f) 分离腿①小鸡死 → 小僵尸存活 + 链清 + Y 从 86.25 落回 <86.0；(g) 分离腿②小僵尸死 → 小鸡活体 + 链清；(h) 蛋表/刷怪笼行为腿（mobTypeForSpawnEgg(0x25D)==19 + state 编码解码 round-trip + 成体蛋映射不污染）；(i) 源码钉 24 枚（枚举/双链字段/概率/缝写口/组合钩子/掷骰/黑暗刷怪翻变/冻结+节拍累积/载具 AI 挂起/pass 接线/推挤豁免/晒燃白名单/快速低伤参数/Renderer 分支+表行/拾取门扩段/蛋表 case/调色板/图鉴条目+蛋映射/Loader+贴图/蛋图标）。**探针接线面同步（既有钉合法演化）**：P-t950(f) 两处类型门钉改钉扩段后文本；t951 白名单钉改钉加白后整行；t786 越界样本 0x27(type19)→0x29(type20)（19 扩为合法）；t785/t787 蛋表 13→14 全蛋覆盖。**阴性轮**：tryFormChickenJockey 双向链写入临时旁路 → 恰 P-t952 独红（d linked=0/held=0、f chicken=-1、g cAlive=0——组合/分离面真被钉住），复原 **367 PASS / 0 FAIL**（t882 偶发 flake 复跑清）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s×2 存活干净。**登记取舍**：①只生成时组合，分离后不再合并；②挂载组合被 1 格墙阻挡不跳（小鸡本不能跳，机制等价 MC）；③击退在挂载态不生效（矿车乘客同口径）；④小僵尸不用 t400 baby 标志（幼体外观全在 mobType 19 几何，babyScaleAt 不介入、永不长大）；⑤骑手挂载期火烧由 tickHostileLife 独立面照常推进（晒燃+避光随骑手 AI 生效），主循环内窒息/溺水/走路声停（同矿车乘客冻结面）。**待目视（实机复现类）**：夜间自然刷出的小蹒跚者明显小于成体且跑得更快、贴身咬人伤害更低；刷怪蛋右键生成后 5% 概率见「鸡背骑小僵尸」组合体（鸡走它走、打鸡僵尸落地、打僵尸鸡自由）；丢铁甲在小蹒跚者路径上偶发穿上（幼体甲壳贴图）；图鉴生物段「小蹒跚者」3D 预览头大身小亮绿配色。

### 🅳 附魔台与铁砧（t953-t962）
**t953** 文字流两调：字还有点大、速度偏快——再调小调慢；**书架变更后停止且不恢复**（多放/挖一个书架文字流停，需保存退出才恢复）——rescan 周期（定秒级轮询或书架编辑事件钩）修复。 ✅✅（adbcdab）①两调（常量源码钉）：字再小一档 = 面片 0.26-0.40→0.21-0.32 格（×0.8，2px 笔画保可读下限）；速度再慢一档 = 漂速 2.6→1.8 格/s（×0.7，一程 ~1.5s）+ 每书架发射率 0.55→0.40 字/s（15 书架 ~6 字/s）+ 单轮上限 5→4（全局封顶 ≤8 字/s）+ 寿命钳 0.7-1.5→0.9-2.2（钳不随降速放宽会截断慢飞令 t=1 提前到达 = 尾段重新加速，与调慢背反）。②书架变更流停病灶：字形流台×书架集合（pairs）重扫唯一事件驱动是 editRev（= window.worldEditRev，Main.qml 仅在 blockPlaced/blockBroken 处自增 = 玩家放/破路径）；一切系统改写栅格路径（爆炸 destroySphereSilent / 落块着地 setBlockFromEntity / 焚毁 / 流体静默写）按约定只发 worldChanged → 集合永不重算，冻结在世界级缓存上（enchantTablePositions 读档重建才刷新 = 「需保存退出才恢复」的观测面），且玩家 editRev 链无任何自愈兜底。修 = 用户菜单双通道、重扫复用 rescanPairs 单一实现：主 = worldChanged 事件钩（组件内 Connections 直连注入的 world，宿主 Main.qml 零改动）+ rescanPending 脏标记 200ms 一次性合并窗（爆炸风暴 N 写只扫一次，放书架 ≤0.5s 起流承诺不破）；兜底 = 1s 自愈轮询 watchdog（门 active && worldRunning，review26-11 零常驻约定）；editRev/tableCount/active 同步通道逐字保留（t873 探针契约）。事件级/秒级，无每帧直发回归。附魔面板 bookshelfPower 数字面登记为同根因未修（系统路径毁架短暂陈旧，任一玩家编辑自愈；用户未点名）。探针 P-t953 真 QQmlEngine×真 World rig 五腿（爆炸 16→15 / 系统放回 16 / 玩家挖放断链自愈 15/16 / 风暴 3 写恰 1 扫 / watchdog 2.3s ≥1 扫）+ 参数/通道源码钉；阴性轮回退 QML → 恰两行 t953 FAIL（367/2）→ 复原 → 369 全 PASS。exe 冒烟干净。目视（字大小/慢速观感、放挖书架流续播）待用户复测。
**t954** 书本合拢动画重做：现「一瞬间+只有左边合并」——应**左页向右、右页向左**对向合拢成**有厚度的关闭书籍**（封面+书脊厚度感）。 ✅✅（7e57967）病灶 = 左右页 pageAngle 绑 bookOpen + Behavior 240ms（读作「一瞬间」）且合拢面只有左页 -22°→-178° 大摆（右页 21° 微动读作不动），合拢后两薄盒叠平零厚度。修 = **命令式双页对向过渡**（bookOpenAnim/bookCloseAnim，ParallelAnimation running:false + restart()，review28 #9 pageFlipAnim 同款先例——绑定与动画两套驱动不可混用；from 省略 = 从当前值起摆，迟滞带边缘反向平滑改向不跳变）：左页 -22°→-178° 外缘向右扫过书脊落右半（「左页向右」）、右页 +22°→+1° 压平并内移 gather（页盒 x 0.19→0.165，OutQuad 先落成基座让封面后程扫叠其上，「右页向左」），850ms 缓出（对标大摆单摆 550/500ms 的 1s 内一档）；**厚度层**（dev-plan「封面盒+页叠层…厚度=内页层」）= 左页随合拢抬升 stackLift 0→0.026（封面拱于页叠上方、外缘 0.015 出檐）+ 右页下沉 -0.006 + 新增 rightPageBlock 纸页内缩薄片骑右页上方（敞开态即在 = 右侧页叠，无弹入弹出）+ 书脊条 closeAmt 增高 scale.y 0.03→0.075 读装订边——合拢态总厚 ~0.06 ≈ 单页 2.7×，各层面错位叠合互不共面免 z-fight；flipPivot 静息位契约保持（⊥区仍全嵌入，载体从单页变「页+页叠」，position.y 0.004 未调）。**兼修 review28 #9 运行期断链**：window 级 onWorldRunningChanged 裸引用 pageFlipAnim/flipPivot 属 inline Component 子组件作用域 id——window 作用域链不含 → 运行期 ReferenceError 被引擎吞掉、ESC 硬档静默失效（P-review28b 源码钉语句面的探针盲区）；真实生效点移 delegate 实例内 Connections（每台附魔书一份：大摆停摆复位语义原样 + 开合过渡 stop + snapBookPose 全驱动属性落定到 bookOpen 静息位；恢复侧无需命令——faceTimer 复跑后迟滞带内状态不变），window 级语句面保留加 typeof 守卫（id 可解析与否两路幂等）。探针 P-t954（纯视觉项源码钉，t931/t941/t953 先例）：(a) 对向双页动画腿（open/close 切片各恰 5×850ms + 合拢含左右两角度驱动 + onBookOpenChanged 起摆）(b) 厚度层腿（rightPageBlock + stackLift/gather/closeAmt 绑定）(c) ESC 落定腿（snapBookPose + delegate Connections + typeof 守卫）；阴性轮（删合拢右页腿 + 850→240 回退「一瞬间+只有左边」）→ 恰 t954 FAIL（369/1）→ 复原 **370 PASS / 0 FAIL**（369 基线 + P-t954）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活 root objects=1 零 TypeError。待目视：走近/离开附魔台看 850ms 双页对向合拢与合拢态厚度感、ESC 落在过渡中途形态落定不卡半途。
**t955** hover 预告格式改：去掉「必得」字样——直接「效率......?」（附魔名+省略号+问号）。 ✅✅（e5b9472）EnchantingTableUI previewText 文案行 `"必得 " + previewName + " ?"` → `previewName + "......?"`（附魔名本身即「必出」承诺，前置铺垫词无信息量；后缀语义不变——省略号 = 产物词条列表未揭〔预告只显首条〕、? = 等级未知；MC 1.0 悬停预告口径只显一种/必定出现/等级模糊不变，t917 预告==施放同源链零触碰——tierPreviewName 仍是预览文本唯一来源）；铁砧侧核过无同款字样（用户只点名附魔台）。注释契约同步：旧格式指纹词全文件清零（含注释——残留即旧口径漂回温床），t917 两处格式描述同步改「名 + ......?」。源码钉 P-t955 三面（t931/t941/t954 先例，QML 无 static_assert 面）：(a) 新格式形态钉（名拼接 + 「......?」后缀；旧式前缀拼接与「 ?」尾巴形态全无）(b) 旧字样全文件不存在断言（含注释）(c) tierPreviewName 定义 + 调用点管线正锚。阴性轮：text 行回退旧式 → 恰 t955 FAIL（1 FAIL）→ 复原 **371 PASS / 0 FAIL**（矩阵 370→371）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视：悬停附魔台档位，预告浮签直显「〈附魔名〉......?」无前置词。
**t956** 附魔台/铁砧 UI 创造中键复制返修（t896 链在这两个 UI 不管用了）。 ✅✅（本次）根因 = t653①/t896 的中键复制只落在 Inventory 面板（调色板/护甲/craft/main/hotbar 五面），EnchantingTableUI/AnvilUI 的槽交互面只有左/右两个 TapHandler —— acceptedButtons 漏中键、中键分支整个缺席（非坏链，是没接过）。修 = 两面板各补面板级 copyStackToCursor（口径逐字对齐 Inventory.qml 单一权威：maxStackSize(id) **整组**数量 + InventoryOps.list4 序列归一〔t874 C++ 序列对象防线〕+ 耐久/附魔/名实例保真 + 空槽 no-op + 旧光标直覆盖=创造「归还虚空」同效）+ 各恰 3 个中键 TapHandler（acceptedButtons: Qt.MiddleButton）：附魔台 = EnchantInputSlot 组件一处改覆盖武器工具槽/青金石槽两实例 + main 行 + hotbar 行；铁砧 = AnvilSlot 组件一处改覆盖左/右输入两实例 + main 行 + hotbar 行；全部 enabled: root.creativeMode 创造门（t288 中键 pick 仅创造，非创造不响应）。**铁砧产物预览槽排除**（… && !aslot.preview）——预览 slotId 是投射产物非槽内实有物品，中键复制 = 绕过 takeProduct 消耗凭空量产（同 Inventory.qml 合成**结果槽**无中键的先例）；铁砧三处中键带 defocusNameBox（t626③ 点槽=意图离开改名框输入态，与 slotLeft/slotRight 同款）。读数走 VM Q_INVOKABLE 恒最新五读数（mainDurabilityAt/mainEnchantsAt/mainCustomNameAt 补用），不依赖 delegate 绑定快照（t699 stale 面）。探针 P-t956（源码钉×2 + 真 QML×真 C++ Hotbar 行为腿）：① 两面板函数体三要素钉（maxStackSize 整组 / list4 / 耐久保真；旧 Math.min(count,…) 全仓绝迹断言）② 逐分支钉（每 MiddleButton 400 字符窗内同现创造门 + 复制调用，恰 3 处）③ 预览排除逐字钉 ④ 行为腿按 t874 装配法定案（真中键事件投递在该 harness 已证不可达——合成 QMouseEvent 拖动伪影，函数链直调）：源树 AnvilUI.qml 临时目录直载 + 真 Hotbar——源槽 5 件方块→光标 64 整组（非 min(5,64)=5）、钻石剑复制=1 件且耐久 37/锐锋 III/改名保真、预置旧光标被覆盖、空槽 no-op。阴性轮（AnvilUI 函数改名）→ 恰 t956 FAIL（371/1）→ 复原 **372 PASS / 0 FAIL**（371 基线 + P-t956）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视：创造开附魔台/铁砧中键点输入槽/主栏/hotbar 行整组上手、生存中键无反应、铁砧产物格中键无反应。
**t957** 附魔台 UI 文案+青金石图标：① 删「武器/工具」字样与「左槽放工具/武器/书」提示行；② 青金石轮廓图标不像——**写边缘提取算法**处理青金石 png（边缘变黑的空缺风格图标）。 ✅✅（57f87f2）① 文案退场：槽 0 的类别 caption 字样删（用户 8-28 口径）——EnchantInputSlot 的 caption 属性 + 居中渲染 Text **整段删除**（两实例删后均无 caption，机制留即死代码；空槽静默自明，物品类别由 tooltip / 选项亮灯表达）+ 全文件「武器/工具」注释指纹改写「待附魔物品槽」；t916 提示行三态收两态——槽 0 空态提示行整行退场，「右槽放足青金石即解锁高栏（创造亦须摆满）」缺料提示保留（t916 语义半保留）；布局零影响由构造保证（caption 是 centerIn 叠层、提示行锚区底，均不入 Column 流）。② 青金石「空缺风格」轮廓图标：t544 手绘 Canvas 八边形描边+「青金」小字整段退场（用户「轮廓图标不像」），换**边缘提取离线管线** tools/build_lapis_outline.py（确定性、仅 PIL、构建期一次生成零运行时成本）：A 段把青金石物品像素艺术（此前只以 QML Canvas 存在——MaterialIcon drawLapis）按**同一像素规格**（逐条矩形+同源色板，镜像契约改一同步二）物化为 textures/icon_lapis_item.png；B 段边缘提取 = 预乘亮度图（亮度×alpha，透明底恒 0 → 轮廓环与内部切面棱线同图同权）→ Sobel 3×3 梯度幅值（色差 Δ 在边界两侧各 ~3Δ）→ 阈值 36；C 段空缺风格输出 textures/icon_lapis_outline.png（48×48：边缘像素近黑不透明=「边缘变黑」、非边缘内部原色×0.45+alpha 80 半透=镂空感、背景恒全透）；EnchantInputSlot 空槽占位改 Image 引用 qrc:/textures/icon_lapis_outline.png（qrc 资源行登记），showLapisOutline 门不变；仅处理本工程自带程序像素图（§9a），无 MC 资产。探针 P-t957（源码钉×2 + PNG 数据钉）：① caption 绑定字面+属性声明+消费点+空态提示行字面四重绝迹 + 存留缺料提示正锚（删行没误伤整组件）② onPaint 指纹绝迹 + 新资产引用 + showLapisOutline 门 ③ PNG 结构断言（48×48 / 四角全透 / 近黑不透明边缘 ≥120px / 半透冷蓝内部 ≥60px / 提取源图在盘）。阴性轮两轮：caption 绑定回退 → t957 连带 t874/t875 COLLATERAL（赋不存在的属性=面板 QML 整文档加载失败，真链腿同挂——**源码钉阴性要选不破坏 QML 加载的回退形态**）→ 改属性声明单回退 → 恰 t957 FAIL（372/1）→ 复原 **373 PASS / 0 FAIL**（372 基线 + P-t957）。编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视：附魔台空槽 0 无任何文字、青金石槽空位显黑边镂空青金石轮廓图标（对比旧手绘描边辨识度）。
**t958** 铁砧 UI 上下居中：改名栏与 A+B→C 下方空一行——面板内容垂直居中。 ✅✅（9b645bc）病灶 = 操作内容（改名框 / A+B→C 槽行 / 等级·冲突提示行）整块 top 钉死在 134 高的 anvilArea 顶（改名框 topMargin 2 起步〔t626① 压高遗留〕、其余元素沿 renameBox/slotRow 底链），内容自然底 ~y110 → 操作区底部恒留 ~24px 空行（上贴标题、下空洞 = 用户「下方空一行」）。修 = 四元素包进 opBlock 内容块（width 随操作区 / height = 内容自然高 108：改名框 26 + 距 10 + 槽行 40 + 等级行 2+~16 + 冲突行区），整块 anchors.verticalCenter 钉操作区中线 → 余白均分（上下各 13px）= 内容块垂直居中（t606① 时代「改名框上距 2」固定钉退役，上距由居中留白承担）；块内锚关系逐字未动（槽行仍 renameBox.bottom+10 / 两提示行仍 slotRow.bottom +2/+20）——居中只平移整块不重排内部；「操作成功」flash 叠层仍 anvilArea 直子铺满全区；面板高 370 与其余行全不动（Column 恰好填满的算式不变）。探针 P-t958 源码钉三腿（t954/t955 纯视觉项先例；AnvilUI 整文档可加载性由 t956 真链腿覆盖、不重复 harness）：① 居中形态（opBlock 声明邻域 width/height:108/verticalCenter 锚三要素 + 旧「top 钉+固定 2px 上距」形态全文件绝迹）② 包裹结构（id 链 opBlock→renameBox→slotRow→两条 slotRow.bottom 提示行依序全落在 flash 叠层注释之前）③ 内部刚性（改名框段无 topMargin + 槽行仍锚 +10——居中不许重排内部）。阴性轮：AnvilUI.qml 回退 HEAD 形态 → 恰 t958 FAIL（373/1，三腿全红 blk@ -1）→ 复原 **374 PASS / 0 FAIL**（373 基线 + P-t958）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活 root objects=1 零 TypeError。待目视：开铁砧面板看改名栏与 A+B→C 行上下留白均衡（下方不再坠一整行空白）、放物/取产物/改名框交互无碍。
**t959** 附魔池随机性收窄：书本附魔把很多**工具+装甲附魔冲突地混在一起**——书附魔池按类别（工具/武器/装甲/书）收窄 + 冲突组规则（同组互斥如保护系/锋利+截肢系）。 ✅✅（b948848）**直附面判决（现状核）**：物品直附（对工具/武器/护甲直接施法）**t824 起已按物品逐条精判过滤**（isApplicableForItem——镐 ⊆ 采集池、剑 ⊆ 武器池、护甲按部位），无跨类混出 → 直附面零改动；混出病灶只在**书载体**（isApplicableForItem 对书全过 → 单次施法从全 14 并集抽 1-3 条，产物如「保护+效率+锐锋」任何单件物品都戴不上）。**修（注册表数据 + 共用层单规则，非 QML 补丁）**：① EnchantDef 加 **homeCategory**（新 EnchantCategory 枚举：武器系 5 条 / 工具系 3 条 / 护甲系 5 条 / universal=耐久〔任何主类别池均可出〕/ none=占位行）+ **conflictGroup**（既有 exclusiveGroup 的单值形式，homeCategory()/conflictGroup() 两查询导出供探针/UI）；② selectEnchantsForItem 对书载体**先由同一确定性 LCG roll 一个主类别**（武器/工具/装甲三选一均匀轮——用户口径四类中「书」类本作暂无专属附魔，t960 弓/竿系在 kBookMainCats 留扩展位），候选收窄到「该类 ∪ 通用（耐久）」→ **单次产物同类成簇**、跨类混出绝迹；跨施法主类别随机轮换 → 全 14 附魔书池**长期仍都可达**（union 不收窄，只收窄单次产物内部）；**冲突组（同组互斥）**由既有 review-M1 组位集抽样在收窄池上照旧生效（组 1 锐锋/亡灵杀手/节肢克星三选一、组 2 效率/精准/时运三选一、组 3 保护/火焰保护/摔落保护/弹射物保护四选一）。**单一权威落点**：收窄在 selectEnchantsForItem 内部 = 预告（tierPreviewName→Hotbar::selectEnchantsPreviewForItem）与施放（doEnchant 同桥）共用的那一层，t917「预告==施放严格同源」结构性保持（review28 #3 种子快照序零触碰）；LCG 初值上移到 roll 之前但非书路径首个消费点不变（非书流逐位零漂移）；战利品附魔书（LootTable::enchantedBookEnchants）同选择器同收窄；铁砧合并同组冲突**已有校验**（书+书 B 替换 A / 装备合并拒上冲突条）→ 保持。**探针 P-t959**（Game 层 + Hotbar 桥接，无 World/QML，t824 同台先例；矩阵 374→375）：(a) 注册表数据钉（14 条逐 id homeCategory/conflictGroup 对语义表，多列初始化错位必红）(b) 书行为腿 1200 次施法（offered 2/12/30 × seed 0..399）：单次产物无非通用跨类混出（成簇腿 = 判别腿）+ 产物内无同组互斥对 + 每条书适用且等级 ∈ [1,maxLevel] + 全 14 附魔跨施法都出现 + 同 seed 产物逐位复现 (c) 直附腿（镐/铲 ⊆ 采集池、胸甲 ⊆ 护甲池，subset+requireAll 双向）(d) 书物品桥接==直调同 seed 逐条相等（收窄在桥下，t917 同源钉保持）。**阴性轮**：收窄谓词停用 → 恰 P-t959 FAIL（374/1，diag 仅 cluster false 余腿全绿 = 修前跨类混出实证复现）→ 复原 **375 PASS / 0 FAIL**（374 基线 + P-t959）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机复现类）：附魔台附书多次 → 每本附魔书词条同类成簇（不再「保护+效率」混栏）、铁砧两附魔书合并冲突条仍红字/替换。
**t960** 弓/钓竿专属附魔：弓（力量/冲击/火矢/无限类）、钓竿（海之眷顾/饵钓类）——enchantregistry 扩池 + 适用物品门。 ✅✅（c184140）**扩池（id 15-20 末尾续号，存档兼容）**：弓四件——劲射 Might（箭伤 +1HP/级，max 3，权重 10）、震击 BowShock（箭击退 ×(1+级)，max 2，权重 5）、燃箭 BrightDraw（命中点燃 5s，max 1，权重 2）、不竭 NeverRun（射箭不耗箭——完全无限口径；背包有 ≥1 箭门槛不变，弓耐久照扣；max 1，权重 1）；竿两件——唤潮 TideCall（咬钩等待期 ×(1−0.2×级)，max 3，权重 2）、缠咬 BiteCall（判定窗 +0.5s×级**附加式**——kBobberBiteWindowSec=1.0 基值用户口径钉死字节不动，max 3，权重 5）；全 conflictGroup 0（不竭与「修复系」互斥为惯例，本作无修复系 → 登记待未来改组号）。**类别**：EnchantCategory 加 EnchantCatBow/Rod（t959 预留扩展位生效——kBookMainCats 主类别轮 3→5 类，书施法可轮出弓/竿附魔且单次产物仍同类成簇）；物品 Category 加 BowItem=16/RodItem=32 位，categoryForItem Bow→BowItem / FishingRod→RodItem（锄/剪刀仍 None），耐久 mask 扩弓/竿位；isApplicableForItem「专属」门严：弓四件只上弓、竿两件只上钓竿、其余物品全拒、书载体全过。**效果单一权威**：EnchantRegistry 五纯函数（bowArrowDamage / bowKnockbackMultiplier / bowIgniteSeconds / rodWaitScale / rodBiteWindowExtra），接线面——劲射 endBowDraw 伤害计算、不竭 endBowDraw 免箭消耗、震击/燃箭 Game 层算值经 spawnArrowPlayer 新参下传（Entities 存 per-entity 载荷字段 arrowKbStrength/arrowIgniteSec，t505 先例，分层不 reverse）、命中点燃按 t919 燃焰同序（damage 后 ignite）、唤潮/缠咬 useFishingRod 甩竿经 spawnBobber 新参下传（bobberWaitScale/bobberBiteWindow 字段，落水首掷 + 鱼跑重掷同乘）；全部新参带缺省值 → 旧调用点（发射器/既有探针）行为逐位零漂移。**探针 P-t960**（矩阵 375→376）：(a) 6 行注册表数据钉（主类别/组/等级/权重/显示名 + 单类位适用）(b) 专属门腿（弓四件 ⊆ 弓、竿两件 ⊆ 钓竿、剑/镐/护甲/锄/剪刀全拒、书全过、耐久扩弓竿位）(c) 书池 800 施法六条全可达 (d) 五公式数值腿含钳制 (e) 唤潮/缠咬行为腿（t926 水槽同格同序号：等待 settle 相对 tick ×0.4 同比 + 基线复跑确定性；+0.5s 窗实测 1.55s ∈ [1.4,1.6]）(f) 震击/燃箭行为腿（天空台 y=92 + 短窗抢拍：0.8s 量测窗 −x 位移积分随倍率缩放，×6 对基线确定界天然分离 <2.2 / >5.2 / 间隔>3.0——×2 首版与游走噪声同量级偶发假红返修；燃箭 isBurningAt 真 / 基线假）(g) endBowDraw/useFishingRod 接线源码钉（t927(c) 手法）。**连带**：t824 探针重定域（弓/竿可附魔专属池 requireAll、锄/剪刀恒空、书全 20）+ t959 数组 15→21 扩位并钉弓/竿主类别。**阴性轮**：rodWaitScale 中性化 → 恰 P-t960 FAIL → 复原 **376 PASS / 0 FAIL**（t882/t891/t927 既有偶发 flake 复跑清再判）；编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机复现类）：弓附魔台三档出专属池、附魔书铁砧敲弓上震击/燃箭生效、唤潮钓竿等待明显缩短、缠咬咬钩窗更从容、不竭射箭不耗箭。
**t961** 杀手附魔攻击力显示：亡灵杀手/截肢杀手的加成写进剑攻击行——「攻击+7(+3)」格式（括号=对特定族加成）。 ✅✅（d1f398b）**口径钉**（用户第五轮读法）：+N = 现行 displayAttackDamage（物品基伤 + 锐锋；杀手系与锐锋互斥组 1 → 杀手剑的 N 即基伤，不动），(+M) = 对特定族加成合计——括号本身即族加成语义，hover 详情不展开族名（从简）。**加成取数链**（单一权威）：EnchantRegistry::familyAttackBonus（亡灵杀手+节肢克星 各级 ×2.5 合计，与 attackMob 实战 t476 对族分支同公式同值；互斥组 1 → 显示合计 = 实际可生效的那一支）→ Hotbar::displayFamilyBonusText 取整组装「(+M)」（亡灵 III 7.5→8 / 节肢 I 2.5→3）/ 无杀手返空串（行形态逐字不变）；数值只活注册表一处，QML 零常量。**显示面**：九处攻击行组装点在 N 后拼后缀——八面板 tooltip（背包/生存背包/箱子/发射器/合成台/熔炉/铁砧/附魔台）保持「+N(+M) 攻击」形，HUD hotbar hover 保持「攻击: N(+M)…」形；各面标签形态不动只插 (+M)；攻击结算逻辑零触碰（t961 未改 attackMob）。**探针 P-t961**（矩阵 376→377）：(a) 注册表数值腿（亡灵 III=7.5 / 节肢 I=2.5 / 无=0 / 锐锋不算 + 双杀手工 1 钉）(b) Hotbar 直调行为腿（t763 同台：钻石剑+节肢 I → displayAttackDamage 仍 7 且后缀「(+3)」= 用户口径「+7(+3」拼形；亡灵 III →「(+8）」；无杀手空串）(c) 源码钉（九组装点全含 displayFamilyBonusText( + hotbar.cpp 桥本体含 familyAttackBonus 调用链与「(+」括号分支，t960(g 手法）。**阴性轮**：桥括号分支中性化 → 恰 P-t961 FAIL（show+pin 双红）→ 复原 **377 PASS / 0 FAIL**（t891/t960 既有偶发 flake 复跑清再判）；编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（QML hover 类）：背包/铁砧 hover 带杀手剑见「+N(+M) 攻击」、HUD hotbar hover 带杀手武器见「攻击: N(+M)」、无杀手剑行形态不变。
**t962** 附魔书↔附魔书交换：背包拿附魔书左键物品交换是对的，但附魔书对附魔书槽的交换没做——补齐。 ✅✅（b298232）**分叉点根因双闸**（对「普通物品交换是对的」）：① `InventoryOps.resolveClick` 同 id 恒入 C 合并臂 → 附魔书 maxStack=1 → 槽恒满 space≤0 → return null = 静默 no-op（旧头注「A/B/D 路径覆盖工具搬运」对同 id 不成立——D 互换臂在同 id 下旧不可达，这正是分叉点：普通异 id 走 D 交换照旧，附魔书同 id 被 C 拦死）；② `EnchantingTableUI.localCanPlace`（t648 门禁）把附魔书与非可附魔物一并扫进拒入面（itemEnchantCategory==None）——就算换算出来也进不去槽 0。**修（单一权威两处，调用点零改动）**：① C 合并臂收窄为「同 id 且 maxStackSize>1（可堆叠）」→ cap≤1 同 id（工具/护甲/附魔书/桶）落 D 互换（MC 语义「不可合并即互换」；耐久/附魔/名随各自实例走），可堆叠满槽 no-op 与全部合并语义逐位不变；② 附魔书门禁豁免（先于 category/已附魔两拒）——书是附魔**载体**（category=None 属常态非「不可附魔物」），刷属性防线不破：书在槽 0 时 itemReady 恒假（category=0 + slot0HasEnch）→ 三档位恒灰、doEnchant 前置守卫零消耗拒，t959 施法链只从「可附魔且未附魔」态出发对书不可达=零触碰；铁砧面无门禁 → ①单独即愈 A/B 两输入槽（同病登记）。**探针 P-t962**（矩阵 377→378；t874/t956 真链 harness：源树 EnchantingTableUI.qml+AnvilUI.qml × 真 C++ Hotbar，函数链直调）：(a) 槽 0 书书左键交换+换回——A（锐锋 V 带名）↔ B（火触 I 落附魔 1 号槽位无名）双向逐槽/逐名保真；(b) 豁免自由进出（空槽 0 放书/取回元数据保真）+ 书在槽 0 doEnchant 恒拒（等级/青金石零消耗槽内容不动）；(c) 阴性腿：普通异 id 交换照旧 / 已附魔剑仍拒入槽 0（豁免不外溢）/ 64 书满槽撞同 id 仍 no-op；(d) 铁砧两输入槽书书交换。**阴性轮**：两修回退 → 恰 P-t962 FAIL（a/b/d 红而 c1/c2/c3 绿，1 FAIL 无扩散）→ 复原 **378 PASS / 0 FAIL**（t882/t891/t927/t960 既有偶发 flake 复跑清再判）；编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机交互类）：槽 0 已附书（台内附出）+ 手持另一本附魔书左键槽 0 → 换出换入、tooltip 各显各的附魔；铁砧 A/B 槽同款；附魔书放槽 0 三档位不亮。

### 🅴 资源查看器（t963-t969）
**t963** 豹猫查看器返修：驯服后**变黑色**（错，MC 驯服=家猫花纹）+没看到项圈；坐下姿势与狼同 bug（随 t946 连修）。 ✅✅（bf65c29）**现行全黑贴图来源**：驯服毛色变体 0 的程序贴图 **mob_cat_black 本身就是全黑档**（t920 系乌黑底+深灰高光纹）——图鉴驯服拨杆恒取变体 0 作代表（t949 注记「开包拨已驯服显全脸黑猫无灰斑」的出处），游戏内驯服也有 1/3 概率落黑 → 用户观感「驯服=变黑」；t949 修的是**串采灰斑**（贴图源 × UV 模式同源），花纹本身不在其射程。**修①（全黑档退役 → 家猫花纹）**：build_mob.py make_cat_black → make_cat_tabby 生成 mob_cat_tabby.png（暖橘棕底 #b8844a + 深棕虎斑横带 #5c3a1a + 浅奶黄口鼻/腹斑 #f0dcb4）——花纹**自创程序生成**：确定性条带函数（每 4 行取 2 行作横带、逐带横向错位 3px、带内 (x+off)%6 固定相位留 1px 断缝成短段）+ (x·7+y·13)%11 相位噪点（毛皮质感非平面色块），非任何既有家猫花色照搬（§9 红线）；变体 1 姜黄虎斑 / 2 奶油点色本就是家猫色不动 → 驯服后三变体皆家猫花纹、全黑永不出现；mob_cat_black 资产与全部引用（Main.qml Texture+变体分支 / ResourceBrowser selectedMobTexSource+形态注文案 / CMakeLists qrc / entitymanager 注释）四文件绝迹。**修②（项圈两处补齐）**：狼 t831 红项圈豹猫侧无镜像 → 游戏内 Main.qml 豹猫 delegate + 图鉴 ResourceBrowser 3D 预览**两消费端同补**横扁环带（豹猫颈围镜像 t946 数值系 0.36/0.05/0.06，狼 0.42/0.06/0.07 系缩小，x 半 0.18 微出躯干侧缘 ±0.15 同狼 0.03 出缘口径；站姿颈根 (0,0.14,-0.30) = 头后缘 -0.24 与躯干前缘 -0.36 嵌接区；**坐姿位 = 站姿绕豹猫坐姿根锚 (-0.12,0.32)（mobmodel.cpp t946 分支）旋 18° = (0,0.319,-0.189)**，t946 成对契约同款换算，ocatSit 切换即时随移）；项圈红 #c22828（狼同值），游戏内侧带昼夜灰阶乘法 + 受击红闪 #ff0000（狼项圈同语义），图鉴侧纯色预览（同狼 overlay 约定）。**单源**：贴图切换 / pack 判据 / 项圈 visible 三消费端全挂 **ocatTamed 同一驯服态位**——Main.qml 全文 entityManager.ocelotTamedAt 直读恰 1 处（=8948 属性声明源），无第三处散写。**坐姿确认结论**：豹猫坐姿随 t946 连修已闭（MobModel 单根锚链派生，图鉴/游戏内/几何三消费端同源），本任务零触碰坐姿面，P-t946 零适配全绿实证。**探针 P-t963**（矩阵 378→379；QML 呈现/资产 headless 不可见 → t931/t941/t957 纯视觉项源码钉 + PNG 数据钉先例；驯服行为链零改动由 P-t949(a) 真实输入链持续覆盖）：(a) 两消费端贴图源钉（Texture 源 + 变体分支 / selectedMobTexSource 指 mob_cat_tabby）+ 全黑档四文件绝迹断言 + 旧资产不在盘；(b) **PNG 数据钉**（16×16 全不透明 + 暖底 meanR−meanB≥40 实测 92 + 花纹非纯色 sd≥15 实测 46 + 非全黑 lum<40 像素≤5 实测 0 + 浅色腹/口鼻斑 ≥8px 实测 20——旧全黑档 meanLum 27.6 / sd 8.5 / 88% 暗像素全部断言必红 =「驯服变黑」回归即测即红）；(c) 项圈两处存在钉 + ocatTamed 单源 count 钉（visible: ocatTamed 唯一 / ocelotTamedAt 直读恰 1 / 两处坐姿-站姿成对位置串）。P-t949(d) 查看器钉合法演化 black→tabby（P-t950(f) 先例，钉意图「图鉴驯服态=程序家猫贴图源」不变）。**阴性两轮各独立独红**：① 贴图重生成纯黑（用户症状签名）→ 恰 P-t963 FAIL（pngData 红：meanL 24.6/sd 0/256 暗/0 亮，texSrc+collarPins 绿）；② 项圈 visible 回退 → 恰 P-t963 FAIL（collarPins 红）；复原 **379 PASS / 0 FAIL**（t882/t891/t960 既有偶发 flake 复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机复现类）：驯服豹猫变暖棕虎斑/姜黄/奶油家猫色之一（不再全黑）+ 颈部红项圈（站立/坐下都贴颈随移）、图鉴豹猫条目拨「已驯服」显虎斑猫 + 红项圈、「坐下」预览项圈随坐姿。
**t964** 重置按钮 z 序：方块预览滚轮放大后**遮住右下角重置按钮**——按钮应永远最前。 ✅✅（068c600）**层级结构结论**：t922 重置按钮声明在 cubeView（View3D，anchors.fill 铺满预览区）**之前**——QML 兄弟层缺省按声明序绘制，后声明的全预览区视口恒绘在按钮上层；且按钮仅在已缩放态显示（zoom≠1）= 恰逢镜头拉至 3.2/zoom、模型投影像素铺进视口右下角的时刻 → 放大即遮（headless 无合成器不可见，纯视觉源码钉先例 t931/t957/t963）。**修**：按钮显式 `z: 10` 提到视口（缺省 z 0）之上——variantPanel t783「显式 z 防后人插层翻序」同款约定；输入层叠随绘制层叠，点击目标不再依赖 View3D 忽略鼠标事件的隐性通道。按钮与变体面板同 z 10 但不相交（面板 t922 已收窄 58px 让位右下角）。**同类面核**：variantPanel（z: 10 且后声明）/ hoverTip（z: 1000）/ 底部返回按钮（主 Column 后声明子项）全健康——重置按钮是预览区唯一带病浮层；**「预览区浮层永远最前」层级契约登记**（变体面板 / 重置按钮 / t965 后续形态·分类按钮组一律显式 z ≥ 10），t965 按约定落 z 即可、勿另起炉灶。**探针 P-t964**（矩阵 379→380；源码钉）：(a) 按钮块钉——「// t922 重置缩放按钮」唯一锚 → `id: cubeView` 段内必含**独立行** `z: 10`（首次阴性轮遗留注释 `// z: 10` 曾骗过子串匹配 → 先行硬化为多行正则整行匹配再做净阴性）+ 「t964 永远最前」契约锚 + cubeView 头段（id → PerspectiveCamera）无 z 覆盖（缺省 0）——两侧合钉 =「按钮层 z > 视口层 z」；(b) 约定钉——全文独立 `z: 10` 行恰 2（面板 + 按钮；hoverTip z: 1000 不计入）；(c) 身份钉——缩放显隐谓词 / reset 写 1.0 / 右下角锚，防钉漂移。**阴性轮**：修复整体回退 → 恰 t964 FAIL（zPin false + convention zRows 1 红、identity 绿）→ 复原 **380 PASS / 0 FAIL**（t882/t891/t960 既有偶发 flake 复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机复现类）：图鉴选整立方方块 → 滚轮放大至高倍 → 右下角「重置」按钮始终浮在 3D 模型之上可点，点击回默认大小后按钮自隐。
**t965** 形态切换按钮组系统：变体面板扩展成编号按钮组（1 2 3…默认最普通形态）——耕地干/湿两态、门+活板门未激活/激活、草丛低/中/高三态、红石火把亮/灭、动力铁轨未激活/激活、末地传送门框架有眼/无眼、作物（小麦/胡萝卜/马铃薯）生长阶段（默认最初形态）。 ✅✅（b8d09cb）**支持清单核实**（state 表达逐项核自数据层，无臆造 API；末地传送门框架**存在**=EndPortal 111，state bit0 末影之眼放眼位——「无缺席登记」）：耕地 23 = state 低 2 位湿润等级（干 0/湿 3 最深，顶瓦 26/27）；门三族 19/89/135 = state bit2 开合（合 0/开 4，**135 系本任务订正**——两处 QML 家族表旧字面量 71 是错 id=青色羊毛：青羊毛被暗路由进 ItemShapeGeometry 满格兜底〔观感凑巧同 BlockCube 掩盖〕、铁门掉落反而拿不到 3D 薄板形态；几何类本就用 BR::IronDoor 不受影响）；活板门两族 20/136 = bit0 开合（合 0/开 1）；草丛 24 = state 即变种 0/1/2（cross 高 0.5/1.0/2.0）；红石火把 129 = bit3 熄灭位（亮 0=放置缺省常亮/灭 8——钮 1=亮 按用户原文「亮/灭」序+放置缺省原则，灭是受供电特殊态）；动力轨 127 = bit4 通电位（0/16，瓦 157/159）；末地框 111 = bit0（无眼 0/有眼 1，顶瓦 141/142）；作物三族 25/55/56 = state 即阶段 0..7（小麦瓦 29+age 全 8 档；胡萝卜/马铃薯基底+age/2 四视觉阶段——25/55/56 中胡萝卜/马铃薯不在创造调色板，支持表照收、浏览器经调色板不可达如实登记）。**单一权威落点**：① 支持表 = `Hotbar::blockFormStates`（Game 层 Q_INVOKABLE，按钮 index→state 值表，空表=无组；位编码直读 BlockRegistry 具名常量，QML 零复制）；② 态变瓦片/参数 = 新 `BlockRegistry::stateTileOverride` + `tallGrassVariantHeight`（Core）——chunkgeometry EndPortal 分支与 partialblockgeometry 耕地/草丛/作物/轨/火把五处态变局部实现**收敛改为委托**，预览几何（BlockCube blockState 顶面态变 / ItemShapeGeometry blockState 开合盒·高度·瓦片）同源消费 =「state→瓦片映射」结构性无双份；门/活板门开合位由散字面量收编为 `DoorStateOpenFlag/DoorStateUpperFlag/TrapdoorStateOpenFlag`。**ItemShapeGeometry 的 blockState 用 -1 auto 哨兵**：掉落物/旧消费端不设属性 → 各形状旧默认（小麦=成熟穗 t925 口径、草丛=中草满格、余 0）行为逐位零回归，查看器显式写钮组 state。**面板**：previewArea 下沿悬浮 formPanel（variantPanel 同款语言，互斥显隐；width−58 让位重置钮；**独立 z: 10 行**入 t964 浮层契约；编号钮 26×26 金框选中；换选物品 onSelectedIdChanged 回钮 1）。动力轨 127 **查看器限定**入 selectedIsItem3D（贴地薄板 quad 新 case + addFlatQuad；掉落物侧 isItem3DFamily 排除清单不变，两侧差集 {16,59,110,127} 注释互指）。**探针 P-t965**（矩阵 380→381）：(a) 支持表行为钉（Hotbar 直调 13 支持项逐 id 数量/编码/默认态 + 8 不支持空表含 71 青羊毛/103 普通轨/128 探测轨邻位防外溢）；(b) 预览几何行为级（ItemShapeGeometry/BlockCube 直调 t880 先例 + blockcube.cpp 新链矩阵工程：活板门/门开合 bounds 换边、草丛三态高度递增、作物/火把/动力轨态变瓦片 vertexData UV 直读、耕地干湿/末地框顶面瓦片、非形态方块 state 惰性逐字节一致零漂移钉）；(c) 查看器源码钉（面板+z:10 独立行+默认钮+换选重置+blockState 接线恰 2+家族 135/127 与 71 绝迹）。**探针合法演化三处**（P-t949(d) 先例）：P-t964(b) z 行数 2→3（formPanel 入约定；首次矩阵跑暴露「z: 10 并注释入行」不满足独立行正则——正是 t964 首次阴性教训的复刻，改独立行）；P-t925 家族同步钉 71→135；review27-4 BlockCube 绑定锚随 blockState 接线演化。**阴性轮**：支持表中性化（switch 恒不命中→表恒空）→ 恰 P-t965 FAIL（380/1，table 腿红 geometry/source 绿）→ 复原 **381 PASS / 0 FAIL**。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活。待目视（实机复现类）：图鉴选耕地→钮 1 干土/钮 2 深色湿土；木门/铁门钮 2 门板旋到 +Z 边、活板门钮 2 竖板贴边；草丛三钮高度渐长；红石火把钮 2 焰头熄灭；动力轨钮 2 亮金轨；末地框钮 2 框顶之眼；小麦八钮嫩芽→金黄穗渐变、胡萝卜/马铃薯四视觉档；选青色羊毛仍显普通立方、选石头等无按钮组。
**t966** 拖拽方向再返修（t921 未愈）：用户实测左右旋转到背面**还是反的**——彻底查相位判定（spinAngle 基准/拖拽轴映射），实机 rig 验证。 ✅✅（8eef0f4）**符号面全景结论**（git -S 全链 + 实机 rig 证伪主 Agent 预演清单）：水平 yaw 拖拽自 t599 起即纯线性 `spinAngle += dx*0.6`，全相位零符号面（rig 四相位水平腿即证——嫌疑①②④均不存在）；**唯一相位相关符号面是 t921 的 faceSign=sign(cos(显示yaw)) 俯仰补偿**，而真病根再深一层在**变换图**：单节点 eulerRotation 的合成序 = Ry(yaw)·Rx(pitch)（yaw 世界系最外——对本机 Qt 6.11.1 真实 View3D 读回子节点 scenePosition 实测钉死，QQuick3DNode **无 rotationOrder 旋钮**）→ 俯仰铰链 = Ry(θ)·X̂，屏幕投影带 cos(θ) 因子：±90° 显示 yaw 处归零、过界翻号（第五轮「背面上下拖反」根因），且侧相位铰链顺向视口、竖直拖泄出绕视轴平面内打转分量（**拖拽轴被换走** = 用户「转到背面的过程手感乱」）；t921 在定律上叠相位补丁 = 过 ±90° 边界拖拽中途换向 → 第六轮复测仍读作反。**修法 = 相位恒定性移入变换图、定律零相位判定**：四预览分支（整立方 BlockCube / 床 / 异形 ItemShapeGeometry / 生物）统一拆 **pitch 父**（eulerRotation.x = -22+userPitch，世界系俯仰，铰链恒屏幕水平，投影永不随自转翻号；生物分支 position 上提至 pitch 父 = 双旋转同绕生物自身中心，与旧单节点枢轴一致）+ **yaw 子**（eulerRotation.y = spinAngle−35，t599 转台与 -35 基偏逐字保留）；DragHandler 定律回归 t877 用户定稿纯形式（`userPitch -= dy*0.6`，yaw 未动）——正面相位感知与钉死契约逐字节一致（faceSign 在正面恒 +1），背/侧相位由结构继承同一恒铰链。**探针 P-t966**（矩阵 381→382；rig 不复算数学——真 QQmlEngine 直载源树 ResourceBrowser.qml〔t956 临时目录+私有 URI 装配法，四 UI 文件改写 + 九 Renderer/Game 类型注册，宿主不 show 的 QQuickWindow，headless 节点变换照常传播——先行废 rig 实测确认〕，找可见 BlockCube Model 真实 sceneRotation/scenePosition 读回；近面锚 = 单位立方六面心世界 z 最大者，+Z 轴相机透视不改位移符号）：(b1) 四相位水平腿 spin 0/90/180/270 各 ±0.6°（1px = DragHandler 0.6°/px 定律）→ 近面严格右/左；(b2) 俯仰腿 前/侧/背（显示 yaw -35/+90/+180）userPitch ±0.6° →「上拖看顶」全相位一致 + 横向泄漏 ≤25% 竖直（旧图侧相位实测比值 1.33 且竖直响应 ~4e-8 死区、背相位干净翻号 上=+0.0049/下=-0.0048——**用户症状计算级复现**）+ 竖直拖不触碰 spinAngle（轴纯度）；(a) 源码钉：纯线性定律行逐字 + `const faceSign`/`Math.cos` 全文绝迹 + 旧单节点合成串绝迹 + 四分支 pitch/yaw 拆层计数 + 契约注释锚。**阴性轮**：修复 stash 回退 → 恰 t966 FAIL（源码钉 + 两相位腿红、水平腿与正面绿——病灶精确落在用户定位的相位带）→ 复原 **382 PASS / 0 FAIL**（t882 flake 复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活。待目视（实机复现类，须用户新版本戳 exe 口头确认关单）：图鉴 3D 预览左右拖全相位同向跟手；转过侧面到背面全程上下拖不再反/不再乱；正面「上拖看顶」与历轮定稿一致。
**t967** 分类单选化+三大类重划：现在生物和物品能**分别同时选中**（选一个物品又选一个生物，出现在一起导致问题）——共用一个选中态；分类重划三大类：**生物/方块/物品材料**（无 3D 贴图的归物品材料）。 ✅✅（3cf2e78）**双选中病灶**：生物段与物品段是两个独立选中变量（selectedMobFromSection+selectedMobName ↔ selectedId），互斥只有单向——物品格点击清生物段、**生物格点击不清 selectedId**；预览四分支里整立方分支带 review27 #4 家族互斥守卫，但**床分支（selectedIsBed）与异形 3D 分支（selectedIsItem3D）的 visible 无 !selectedIsMob 守卫** → 先选床/活板门/火把/栅栏再点生物 = BedModelGeometry / ItemShapeGeometry 与 MobModel 同时可见叠渲，而名字/类别行与形态·变体面板因 selectedIsMob 优先只显生物侧（渲染双份 + 面板单份 = 用户「出现在一起导致问题」的复合坏面）。**修 = 选中收口单一权威**：selectMob(mobType,name) 写生物侧同时把 selectedId 归 0（无物品哨兵，selectedIsCube/Bed/Item3D 对 0 恒 false → 预览自然只剩生物分支；onSelectedIdChanged 形态钮重置经哨兵路径正确触发）、selectItem(id) 写 selectedId 同时清生物段——任何时刻 (selectedId, selectedMobFromSection) 至多一方有效，双选中结构性不可再现，既有消费端零新增守卫。**三大类归属（单一权威表 categoryOfEntry）**：判据 = 预览路由谓词本体——三个 selectedId 绑定布尔（selectedIsCube/Bed/Item3D，t880/t925/t965 家族链）函数化为 isCubeId/isBedId/isItem3DId + selected* 薄委托 =「分类表即预览路由表」（家族扩面只改一处，分类自动跟随）：**生物(0)** = 生物图鉴段 + 生物蛋（蛋预览 = 3D mob 模型，「有 3D 形态的进方块或生物类」）；**方块(1)** = 整立方 ∪ 床 ∪ 异形 3D 家族；**物品材料(2)** = 其余全部（无 3D 展示、平面大图标：工具/材料/护甲 + 未入 3D 家族的 cross 族〔树苗·花·睡莲·甘蔗·木梯〕与普通·探测轨/压力板族）。左栏三段渲染（生物格 + 生物蛋小格 + 方块 + 物品材料），调色板三分区 egg/block/matEntries 两两不交并集 = paletteModel；物品格 delegate 收编为共享 Component（三段零复制）；底部类别行加所属三大类前缀。**探针 P-t967**（矩阵 382→383）：(a) 源码钉（权威函数体互清形态 + 双 TapHandler 路由 + 旧内联双写绝迹 + 三分区 Repeater/表头 + 谓词委托）；(b) 真 QQmlEngine rig（t966 装配法）分区读回——两两不交 + 多重集并集 == paletteModel + 代表条目逐类（石头/火把/木活板门/白床/草丛→方块、木棍/弓/护甲→物品材料、猪/狼蛋→生物、狼图鉴→生物）+ categoryOfEntry 直调；(c) 行为腿驱动真选中态机：火把→狼 selectedId 归 0 且**有效可见几何集**只剩 MobModel（可见性全 Quick3D 祖链行走 + qobject_cast 判几何——QML 内联自定义属性会铸 _QML_N 动态元对象，精确类名匹配漏检 Node 门控与 MobModel 本体，首轮 rig 即栽在此假红面）、白床→狼同钉床分支、狼→石头反向翻回 + BlockCube 独显、木棍/蛋类别名逐字、代表条目 ×4 独占循环。**阴性轮**：selectMob 清物品行注释化（QML 仍加载，t957 教训）→ 恰 t967 FAIL（c1/c2/c5 三行为腿全红——双渲染与 selectedId 滞留 3/13/512/527 复现；分类腿绿 = 病灶精确落在单选中修）→ 复原 **383 PASS / 0 FAIL**（t891/t960/t927 既有偶发 flake 复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 14s 存活。待目视：图鉴先点床/活板门再点生物不再叠模（只剩生物 3D）；左栏三段「生物（图鉴+生物蛋）/方块/物品材料」分区正确（树苗·花·铁轨·压力板在物品材料段）；底部类别行带「方块 · /物品材料 · /生物」前缀。
**t968** 烈焰人两修：头**×0.6** 再缩；烈焰棒上下错开一点（不在同一平面）。 ✅✅（cdb4033）**单点修**：两修全落 mobmodel.cpp mobType 17 分支（三消费端 delegate / 图鉴 / 笼迷你共享同一几何，t782 同源纪律——一处修三处）。① 头 ×0.6 再缩：t818 的 0.7³（半 0.35）→ **0.42³（半 0.21=0.35×0.6）**，绕头心 (0,+0.10,0) 缩、头位不动（y 跨 [-0.11,+0.31]；棒内缘 0.47 与头间隙 0.12→0.26 更不穿模）。② 棒 Y 交错：4 根棒循环内基心 y 按奇偶交错 -0.03 +0.10（偶）/−0.10（奇）→ 棒心 +0.07/−0.13 两档（**棒 Y 值集合恰 2 个不同值=非共面**；每根仍竖直 1.10 长、公转转轴/轨道半径 0.52 全不动）；总跨 [-0.58,+0.52]=1.10 → [-0.68,+0.62]=1.30（微出碰撞 1.2 上下沿 ≤0.08——同外缘 0.57>halfW 0.5 先例，纯视觉 hitbox/AI 不动）。**消费端呈现表随跨度演化**（体心 -0.02→-0.03）：Main.qml 笼迷你 0.38→0.32（0.42/1.30 归一口径）/ yOff 0.008→0.010；ResourceBrowser 图鉴预览 1.1→0.95 / centY 0.02→0.03；陈旧 t782 代注释（0.88³/半径 0.62/共面棒心）在 mobmodel.h 与两 QML 全部钉到现值（注释即契约）。**探针 P-t968**（矩阵 383→384）：(a) 行为级真几何腿 = MobModel 直编读 vertexData（P-t946 先例；rodSpin=0 轴对齐分桶最稳）：头桶（|x|,|z|≤0.25，棒内缘 0.47 在桶外）恰 24 顶点 max|x|=max|z|=0.21（×0.6 钉）+ y∈[-0.11,+0.31]（绕心缩钉）；棒桶 96 顶点顶沿集合 {0.62,0.42} / 底沿集合 {-0.48,-0.68} 各恰 2 个不同值（非共面断言）+ 径向内缘 0.47 / 外缘 0.57（轨道保持）+ 总跨 1.30；(b) 源码钉（t931/t941 先例）：头尺寸行 + 交错三元式逐字 + 旧共面棒心/旧头三连尺寸绝迹 + 两消费端呈现值随动 + rodSpin 公转动画两处保持。**阴性轮**：分支几何两行回退旧形 → 恰 P-t968 FAIL（头腿+棒腿+源码钉同红；t891 偶发 flake 同跑出现、与几何无关）→ 复原 **384 PASS / 0 FAIL**（t891 复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 12s 存活。待目视（实机复现类）：燃烬者头明显更小（×0.6）、4 根环绕棒上下错开不再齐平（游戏内/图鉴/刷怪笼三处一致）、公转照常。
**t969** 火把贴图修：中间悬空黑色部分——贴图采样窗/UV 修正。 ✅✅（3e0955b）**病灶（headless 实渲复现钉死**：scratch View3D rig 直载真 ItemShapeGeometry(13) + 图集 + Mask 材质，grabToImage 像素差分——柄底下方 6px 一片菱形碎片、与柄身隔一条全黑断口）**：火把分支只对 u 开了子窗 [7/16,9/16]、v 恒满高 [0,1] 铺全部六脸——torch 瓦片图像 row0 与 row14..15 是透明底（Mask 丢弃）→ ①侧脸上下各留死带，柄身可见内容止于盒底真边上方 ~12%，透明断口成形；②±Y 端面把含透明行的整条竖带压扁成 2/16 见方碎片，落在断口下方、透明孔漏背景底色 = 「悬空黑色部分」（查看器 3D 预览与世界内火把掉落实体同几何同病）。**修 = UV 矩形收正内容区（正刀）**：侧脸 v 窗收正到不透明内容带 **[2/16,15/16]**（侧脸零死带、柄身贴到盒底——断口消失）；±Y 端面经新增 capTopV0..1/capBotV0..1 addShapeBox 形参覆写（负哨兵保既有调用逐位零改动）采柄木**全双列不透明**带（顶 row6..7 / 底 row12..13，机制等价 MC 火把方块模型 up 面采 [7,6]..[9,8] 柄顶截面）——**端面窗禁含焰行**：焰尖 row1 只占 x7 单列（x8 透明），含焰端面窗中央必有透明孔 = 陡俯仰下端面中央黑斑（首版焰带端面即栽在这，同 rig 抓回）。**V 朝向契约再实证**：图像顶 ↔ v=1（t489 像素级实测 + 本次 rig 复测：v 窗 [12/16,14/16] 采到图像 row8..16 焰带即证）——窗口常数全按 v = 1 − 图像行/16 折算，首版按行号直写的未翻转窗被同一渲染回路当场抓回改判。游戏内火把（torchHost 木柄+焰三立方纯色）/ 手持 billboard（icon_torch.png）/ 图标走其它路径不受影响。**探针 P-t969**（矩阵 384→385）：UV 矩形钉 = 真几何顶点 UV × 运行时逐像素扫 textures/atlas.png tile 17 alpha≥128 内容包围盒比对——①六脸窗 ⊆ 内容带 ②侧四脸与内容带**相互贴合**（防缩窗躲绿丢内容）③端面窗 ⊆ 柄木全不透明带（焰行黑斑防钉）④u 窗 ⊆ 本体柱内容列；源码钉（内容带/端面窗常量行 + capTop/capBot 形参管线 + 旧满窗调用形绝迹）。**阴性轮**：火把分支回退满窗形 → 恰 t969 FAIL（diag 显旧 v 跨 [0.008,0.992] 越内容带）→ 复原 **385 PASS / 0 FAIL**（t882/t960 既有偶发 flake 轮换复跑清再判；基线 HEAD 追加采样 4 轮全绿）。编译零警告（dxcompiler 豁免）；exe 冒烟 15s 存活。**待目视（实机）**：查看器选火把 3D 预览柄底无悬空碎片/黑断口、拖俯仰看顶面无黑斑；世界内丢落地上的火把掉落物同观感。

### 🅵 钓鱼（t970-t971）
**t970** 钓获生物坠伤：被拉上来的生物落地有掉落伤害——免除/大幅减轻该次拉拽产生的坠伤（拉拽是玩家动作，不该顺带摔死目标）。 ✅✅（本次）**前提核验（全库 grep + git -S 双证）：mob 通用坠落链此前缺席**——mob 落地沿（tick Mob resting 翻 true 分支）无任何摔伤结算（fallStartY 仅铁砧 FallingBlock 用；玩家侧 t22 独立成链），对照腿「同落差自走跳下照摔」要求链条存在 → 本任务两件套：**① mob 通用落地摔伤链**（镜像玩家 t22 同式同阈值）：Entity 尾部新增 `fallPeakY`（滞空最高**脚位** Y = pos.y−halfH）——贴地（resting）每 aiTick 保鲜为当前脚位（腾空起算点，防跨事件陈旧高基准伪摔伤）、滞空每帧 max 刷新（弧顶离散采样）、落地沿结算后复位到落点；落地沿 `fall > kMobFallSafeBlocks(3.0)` → `dmg = floor(fall−3)`，落点脚位格 Water 豁免（t200 玩家镜像；判定走 t298 `mobFeetInWater` 单一权威——pos 已 snap restY，脚位格即支撑面所在格），伤害走 damageEntity 既有受击链（红闪 + 归零 mobDied 掉落）。**② 拉拽一次性豁免**（用户口径二选一取「免除」）：`pullMobToward` 置 `Entity.fallExemptOnce`，**下一个落地沿无条件消费**（t690 着地沿无条件清窗同型：伤害判定读消费前值）——豁免只覆盖拉拽抛物线自身那次落地，之后的自体坠落（挖支撑/推挤/再跳）照常结算，**非常驻免摔**；DMI 兜底 + spawnMobCore move 入槽 → 槽复用自动清。**探针 P-t970**（矩阵 389→390）：(a) 高台猪直驱 pullMobToward（竿→该入口接线由 P-t882(c)/P-t927 源码钉+行为腿锁死，直驱消甩钩 RNG 不向基线引新 flake 源）~11 格解算弧落地 HP 不变（峰值 ≥ 起点+6 先钉「弧够高」防小弧假绿）；(b) 对照腿同落差自落（空投 61→50）恰摔 floor(11−3)=8（HP 10→2 精确钉，镜像常量现算）；(c) 豁免一次性消费双钉：同猪 20HP 满血穿过拉拽弧 → 挖穿站立柱（3×3 支撑移除=确定性坠落驱动，替代击退推挤——推挤位移被 RNG 游走对冲实测一轮假红）落差 50→40 恰 7 伤（HP 20→13 精确钉：豁免随体存活则 0 伤、峰值滞留弧顶则 18 伤——消费与落地沿峰值复位两病同钉）；(d) 水缓冲：井心空投 61→井底 48（无水必摔 10）零伤（5×5 井使空投期游走横漂 ≤0.91 格结构性逃不出去，出缘需 ≥2.1）；(e) 源码钉（pullMobToward 体内置位行 + 落地沿消费/峰值复位/水豁免/伤害式 + 头文件 3.0 阈值）。**连带加固**：t919 探针 rig 围 2 格高石墙（t897 口径）——其对照腿 7.5s 长窗 Shambler RNG 游走可走出无栏平台缘，新坠落链结算边缘摔伤（1/3 轮 hp5 假红实测）；伤害核账语义与墙无关。**阴性两轮各独立独红**：①豁免置位回退 → 恰 t970 FAIL（hpA=2 用户症状计算级复现 + 置位钉红；b/d 绿）；②落地沿 damageEntity 回退 → 恰 t970 FAIL（hpB=10/hpC=20 对照腿红，a/d 绿——豁免面不受影响）；复原 **390 PASS / 0 FAIL**（t882/t891/t960 已知偶发 flake 轮换复跑清再判）。编译零警告（dxcompiler 豁免）；exe 冒烟 10s 存活。待目视（实机复现类）：高台收杆拽生物上天落地不再掉血；生物自己跳下高台照摔；钓到水里生物拉出水面同口径。
**t971** 鱼粒子时机：现在**一开始（入水待机）就有**水粒子——应收窄到**临近咬钩**才出现（t926 粒子链的触发门收窄到咬钩前 N 秒或等待期后段）。 ✅✅（4ef7413）触发门收窄落地为**案① 咬钩前 N 秒前瞻窗**（案② 等待期后段 25% 落选——N 前瞻与判定窗衔接更自然、预告感更强）：常量 `EntityManager::kBobberParticleLeadSec = 2.5s`（注释钉用户口径「临近咬钩才冒粒子」；kBobberBiteWindowSec=1.0 用户口径钉死不动——只动「粒子何时开始冒」，不碰咬钩时序），单一权威谓词 `bobberApproachAt`（Water 态 ∧ 等待阶段 !hasBite ∧ 剩余等待 ≤N——**纯读查询，tick 本体零改动**，倒计时既有时序即权威源）；Game 层镜像 `bobberApproach` Q_PROPERTY（fishingChanged 通知族，值变才发——每等待周期仅预告窗开沿 / 咬钩沿各一次，非每帧信号）→ Main.qml 待机逼近粒子链（t884③ 380ms 拍）running 加 `player.bobberApproach`。**粒子面清单**：入水水花（bobberSplashed→burstWaterCast，抛竿反馈）保留；咬钩水花（bobberBit→burstWaterSplash）与鱼跑水花（bobberEscaped→burstWaterEscape）不动；只收窄待机逼近链；发射节流（380ms）/ 粒子池不碰。**短等待处理**：剩余等待 ≤N 的门在等待总长 <N 时落定即开——唤潮 III ×0.4（等待下界 2.0s）全程出现（短等待不裸奔，预告窗盖满剩余等待）。**咬钩窗口行为不变**（hasBite → 链停，t926「窗口期水面突然安静」对比保留——本任务只收窄待机前段，不延窗）。**探针 P-t971**（矩阵 390→391，t884/t926 水槽 rig）：(a) 基线腿六钉——settle 后 2.0s 静默（最早可开门 = 等待下界 5s − N，留 0.5s 裕量，「等待期前段粒子计数=0」）/ 门开→咬钩沿恰 2.5s±0.1（dt 0.05 下 50 tick，**N 值行为级钉死**）/ 门开后每 tick 恒真到咬钩沿（预告窗无间隙）/ 咬钩判定窗内恒假（窗口停拍钉住）/ 逃走重掷后恒假（新等待 ≥5s > N）/ 入水水花恰一次（反馈与预告门解耦证明）；(b) 唤潮腿：直调 `bobberWaitSeconds∘hashVoxel` 确定性预选短等待 serial（base ≤5.5s → ×0.4 ≤2.2s < N）→ settle 当拍即真且全程到咬钩（全程预告不裸奔）；(c) Game 镜像腿：pc 真甩竿逐 tick 与实体谓词一致（前段恒假 → 预告窗翻真 → 收竿四镜像清零）；(d) 源码钉：Main.qml 钓鱼段 running 含新门 + 保留 `!player.hasBite` + `interval: 380`（发射面不回退）。**阴性轮**：谓词摘除剩余等待门（= t971 前全待机恒真）→ 恰 t971 FAIL（390/1——settle 当拍即开门 = 用户症状复现；唤潮/源码钉腿如预告不受影响）；复原 **391 PASS / 0 FAIL**（两次全量跑均清，无 flake）。编译零警告（dxcompiler 豁免；-Wall -Wextra 三 TU 审计仅既有警告）；exe 冒烟 20s 存活（root objects 1）。待目视（实机复现类）：甩竿入水后待机前段无水泡、临近咬钩 ~2.5s 起鱼群逼近粒子出现、咬钩瞬间链停接水花。

### 🅶 杂项（t972-t977）
**t972** 载入世界空白区：进世界看到大片空白透过去（疑似回到原点计算/区块未请求），走近挖/放才刷新——chunk 加载/mesh 生成时序（进入世界时的可见集 mesh 请求优先级）。 ✅✅（6377e87）根因两层（用户「回到原点计算」猜测证伪：可见集/重建时序均按玩家真实所格计算，实测矩阵 + 自动驾驶 exe 五场景——新建直进/世界内瞬移/存退重进/冷重启载入——载入链数据与窗内重建全部正确）：①**主因** = t470/t472 把各段 Model.visible 链在 chunkInRange（r=3 重建窗口）上——有限 160×160 世界中心 51/100、角落 84/100 chunk 被强制隐藏（多数本就有 mesh），且窗内只随跨 chunk 边界 catch-up 点状出现 = 「大片空白透过去、走近挖/放才刷新」；②**连带真缺陷** = World::finishLoad 的 worldChanged 载入首建被 t188 流体专用跳过整段吞掉——recreate 出厂 chunk 的 m_fluidOnlyDirty=true 而存档 blob 直写不经 ChunkManager::setBlock（唯一清除点），生产被 positionChanged catch-up 与「同世界重进显上局 mesh（存档=退出态，巧合正确）」掩盖。修法三件套（t472 重建窗口经济学原样保留）：(a) 六段模板 visible 只由 vertexCount>0 决定（有限世界全幅渲染，机制对标 MC 1.0 有限地图；t470 自测「600→154 段」绘制剔除零 FPS 收益 → 绘制侧放开零成本；启动遥测 segs visible 101→202）；(b) enterWorld 在位姿/窗口定稿后（adoptSpawnColumn 之后、chestStore.loadAll 之前）kickWorldMeshSync：窗外段 clearMesh 作废旧世界/上局残 mesh（防换世界显陈旧错景）+ 近→远入队，meshSyncTimer 每帧限量 1 段 refreshMesh 排空（走与编辑同一条 buildMesh(Dirty) 链，无同步全量卡顿，秒级渐进填满 = 正常渐进加载）；(c) 窗外错过显式记账替代 catch-up 巧合——onWorldChanged 窗口跳过时记 deferredRebuildPending（dirty 被跳过）、setSunDir/setDayMul 静默跟随时 sunRebuildDue 跨门记 lightStale（不排空则放开绘制后夜晚远处显上烘正午亮度），排空泵稳态 1Hz 扫描同队列排空，远处可见地形内容/光照最终一致。**探针 P-t972**（矩阵 391→392，局部 2×2 chunk 世界 ChunkGeometry 直驱，t860 先例）：finishLoad 后窗内段当帧按载入数据重建非空（视距内空 mesh 计数=0 载入腿）/ 载入把窗外错过记 deferredRebuildPending / clearMesh 归零 + refreshMesh 按当前世界数据重建（顶点数随新世界变，非旧 mesh 保留）/ 窗外编辑与跨 dayMul 烘门各记账并 refreshMesh 双清 / Main.qml 源码钉编排链（六模板解链 + kickWorldMeshSync 定义且挂载于 enterWorld 内位姿定稿后 + clearMesh + shift/refreshMesh 排空泵）。**阴性轮**：terrain 模板 visible 回链 chunkInRange → 恰 t972 FAIL（391/1，visTerrain 腿红 = 空白区复现）；复原 **392 PASS / 0 FAIL**（t960 已知 flake 复跑清）。-Wall -Wextra -fsyntax-only 两 TU 审计零告警；exe 冒烟 20s（root objects 1）。待目视（实机复现类）：进世界窗外区域 ~秒级由近及远渐进填满、无永久空白；换世界后窗外不显上一世界地形；夜晚远处地形与近处亮度一致。
**t973** 生物格放方块检测：生物占据的格子**不能放置方块**（防活埋）——放置预检加实体占用门。
**t974** 保存退出偶发未保存：有概率重进是上一次存档点——保存时序/异步写完成前的退出竞态。
**t975** 创造拿取语义再反转（用户 8-28 定稿）：**左键=拿一组**、**右键=只拿一个**（t896 的左键 1 个被翻案）。
**t976** 耐久条显示回归修（t931 后反了）：已消耗耐久的镐子背包里**不显示**耐久条（hover 能看到耐久掉了）——t931 修复引入的回归（显示条件写反或绑定面漏）。
**t977** 切换物品栏物品名浮显：切槽显示当前物品名——血量/饱食度**上方中间**、白字；**附魔物品显示详情**（名称+耐久度+换行+逐条附魔带等级）；改名物品显示改名后名字（多附魔工具区分用）。一定时长淡出。

### 📎 R19.17 范围与顺序
t933-t977（45 项）。**建议顺序：t933-t935 性能批二最高优先（用户 frame2 实测数据在手，跨世界泄漏+waitSync+ltail 三面）→ t936-t945 红石铁轨组 → t946-t952 生物组 → t953-t962 附魔铁砧组 → t963-t969 查看器组 → t970/t971 钓鱼 → t972-t977 杂项**。每项独立 commit + dev-plan ✅✅；全部完成后 code review + 统计报告。实机复现类（t933/t934/t953/t966/t976）产出须用户在新版本戳 exe 上口头确认后关单。
