# t873 书架→附魔台文字流实机二查（复现与根因记录）

> 用户报告：t823 审计后换新版 exe **仍看不见**文字流 → 升级真 bug 处理。
> 结论：**数据链完好（三级实证）；断点在渲染尺度——#Rectangle 内建面片基尺寸是 100×100 单位
> （实测 scale 0.07 → 7.005 格宽 = 0.07×100.07），字形一直渲染成 5.5–8.5 格宽巨型半透明白幕
> 而非「白色小字」。t765「像爆炸」→ t797 数值缩小两轮无效的真因。**

## 1. 链路图（全链静态核对 + 运行期实证位置）

```
enchantTablePositions (Main.qml ListModel)
  ├─ 放置/破坏事件维护（onBlockPlaced id94 / onBlockBroken → removeEnchantTableAt）
  ├─ 读档重建（collectBlocksOfId(94) 全图扫）
  └─ onWorldChanged 兜底校验
        ↓ tableModel + tableCount(Qt.binding count) 注入（glyphFlowLoader.onLoaded）
EnchantGlyphFlow.rescanPairs（切比雪夫==2 环带 × y/y+1 × 半步 Air；id 95/0 字面量，
  static_assert 钉 BlockRegistry::Bookshelf==95 / EnchantingTable==94）
        ↓ editRev(worldEditRev) / tableCount / active(appState==playing) 三触发
pairs[] → spawnTimer(500ms, running=active&&pairs>0) → 距离门(cam 16 格水平) → spawnGlyph
        ↓ tickTimer(20ms) 弹道 + billboard + 渐隐
池 Model(#Rectangle + glyphs.png 4×4 图集, NoLighting+Blend) × 36 → particlesHost 场景图
```

| 环节 | 验证手段 | 结果 |
|---|---|---|
| 组件加载/注入 | 主 exe 运行 5s，logs/voxelsandbox.log | `[t797] Loader status = Ready` + `[t873] injected: world=true cam=true tableModel=true`（菜单态 tables=0/active=false 符合预期） |
| 重扫/发射/停摆 | **矩阵真链探针**（QQmlEngine 直载源树 EnchantGlyphFlow.qml × 真 World rig × 真 ListModel） | 满环 16 对（=t823 镜像/权威 16/15）、堵半步 15、删台 0 且发射器停摆、12 连发计数全对（260 PASS） |
| 渲染像素 | **qml.exe 视觉探针**（真组件挂 View3D，暗底满环 uiBoost，grabToImage 帧分析） | 修复前：单帧 1–3 万亮像素（巨型白幕）；修复后：每帧 8–10 簇 4–16px 小字形（正确尺寸） |

## 2. 根因：#Rectangle 基尺寸 100×100 单位

- 最小探针实测：`Model { source:"#Rectangle"; scale:(0.07,0.07,1) }` 在相机 (0,0,5)、竖直 FOV 60° 下
  渲染宽 **910px ≈ 7.005 单位**（横向 FOV 换算 909px 自洽；竖向恰裁满屏）→ 基尺寸 = 0.07×100.07 ≈ **100**。
- 旧代码按「1×1 基」把 glyphScaleMin/Max 当「格」直乘 → 实际 5.5–8.5 格宽面片。
  t765 旧值 0.10–0.16 → 10–16 格（用户「像爆炸」）；t797 缩到 0.055–0.085 → 5.5–8.5 格
  （观感几乎不变——**基尺寸假设错 100×，调数值无效**，且从未像素级验证过）。
- 连带陷阱：诊断 harness 自身的参照几何（#Cube 地面板/书架）同因 ×100——相机落入巨型 #Cube 内部
  （背面全剔除→「不渲染」）、900 单位宽不透明地面板顶面 y=+1.5 盖住字形飞行区 y 0.55–1.0
  （修复字形缩到 0.2 格后全部被遮 → 一度误判「小尺寸不渲染」）。**用内建图元做诊断参照时必须 ÷100。**

## 3. 修复（最小修 + 尺度重标定）

1. `scale: (scl/100, scl/100, 1)` —— 恢复 scl「字形面片边长（格）」语义（几何诚实）。
2. `glyphScaleMin/Max 0.055/0.085 → 0.18/0.28` —— ÷100 后旧值在 5–8 格视距下笔画 ~0.5px
   **亚像素不可见**（探针像素级实证：亮像素数=0）；重标定后 18–28px、笔画 1.5–2px，
   机制对标 MC 字形粒子 ~1/4 格。
3. 运行期自检日志（长期诊断资产）：`[t873] injected / rescanPairs / rescan aborted / spawn tick`
   （tables/pairs/nearPairs/want/spawned/totalEmitted/poolFree）—— 一次运行直接读出链断在哪一跳。
4. 矩阵真链探针 +1（260 PASS / 0 FAIL）钉住数据链；渲染侧由本文档像素证据钉住。

## 4. 修复后若用户仍看不见 —— 候选解释 × 验证开关

| # | 候选解释 | 日志/自检证据 | 下一版开关建议 |
|---|---|---|---|
| 1 | 搭法不满足环带口径（贴身环带切比雪夫==1 / 半步被堵 → pairs=0，与附魔档位同因同减） | `[t873] rescanPairs: tables=N pairs=0`；附魔台 UI 档位=0 同因 | 无需开关：UI 档位即自检入口 |
| 2 | 相机距离门：书架离相机 >16 格不发射（第三人称贴墙钳制后更远） | `spawn tick: nearPairs=0`（有 pairs 但全被剔） | 距离门坐标日志 / 「近台必发」调试键 |
| 3 | 白字 × 亮背景（白天天空/沙/雪）对比度不足 | 字形在暗底清晰（本文档帧证据）、亮底待用户截图 | tint 诊断模式（切高对比红色一晚） |
| 4 | 书架过少 → 0.22/s/架低频（1–2 架 4–5s 一粒，寿命 2–4s 渐隐） | `totalEmitted` 增速 | ratePerShelf ×3 调试档 / 开附魔台 UI 已有 ×4 加密 |
| 5 | 尺寸观感：0.18–0.28 格仍偏小 | 帧内 4–16px | glyphScale 用户可调 |

## 5. 复现/验证工具（build/t873_visual/，gitignored 脚手架）

```bash
# 视觉探针（重建：拷 src/ui/EnchantGlyphFlow.qml + textures/glyphs.png 到同目录，
# sed 's|qrc:/textures/glyphs.png|glyphs.png|'，参照几何 scale 已 ÷100）
QT_FORCE_STDERR_LOGGING=1 D:/Qt/6.11.1/mingw_64/bin/qml.exe Harness_g2.qml
# 控制组（不实例化字流）→ 帧差即字形像素
qml.exe Harness_c.qml
# 基尺寸标定（单面片 910px 实测）
qml.exe RectSize.qml / SmallRect.qml / TexRect.qml
```

帧分析方法见本文档提交的 commit（PIL 亮像素统计 + ASCII 掩码渲染）。
