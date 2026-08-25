# t874 铁砧附魔丢失 + t875 附魔台清洗 —— 根因与复现文档

**版本戳**：build 2026-08-25（git 短哈希以 exe 主菜单角落 / F3 / 启动日志为准；本修复基于 e17325a 之后的工作区，
矩阵 259 PASS / 0 FAIL）。**本修复尚未经你实机确认——请在新版本上按下方清单核对并口头反馈后才关单。**

---

## 根因（一句话）

C++ `Q_INVOKABLE` 返回的附魔列表（`heldEnchants()` / `enchantsAt()` 等）在 QML/JS 侧是「序列对象」
（Array-like：`length` 与下标可读，但 **`Array.isArray` 恒为 false**），而 UI 全部数据链上的守卫写的是
`Array.isArray(enchants) && length===4 ? enchants : [0,0,0,0]` —— 序列被当非法输入**兜底清零**：
物品放进铁砧/附魔台/箱子等本地槽的瞬间附魔字段被写成 `[0,0,0,0]` = 「放入铁砧附魔直接没了」；
同一形态也骗过了附魔台拒入门的判定臂 → 「能放入已附魔物品且清洗」。

## 为什么七八个版本没修好

探针链两头都看不见这一环：

| 探针 | QML 层 | Hotbar | 序列边界 | 结果 |
|---|---|---|---|---|
| t792 实机探针 | 真 AnvilUI.qml | **QML 桩**（返回真 JS 数组） | 不存在 | 47/47 全绿 |
| t822 二探针 | 无 | 真 C++ 直调 | 不过 JS | 全绿 |
| 本轮 t874/t875 | **真 AnvilUI/EnchantingTableUI.qml** | **真 C++ Hotbar** | **真 QVariantList↔JS 序列化** | 修复前红、修复后绿 |

只有「真 QML × 真 C++」同台时序列对象才出现——这正是玩家实机的组合。本轮探针用 QQmlEngine 源树直载
真实面板 + 注入真 Hotbar/PlayerState，首次复现了用户症状。

## 本次修了什么

1. `InventoryOps.js` 新增 `list4(v)`：任何形态（真数组/C++ 序列/undefined）归一为真 JS `Array<int>(4)`。
   接入五个读边界：`resolveClick` / `resolveRightClick` / `readSlot`（所有槽读的单一咽喉）/ `beginLeftDrag`
   / `placeOneInSlot`。
2. 面板写出端终局防御：AnvilUI / EnchantingTableUI 的 `localWriteSlot` 改用 `InventoryOps.list4`。
3. 附魔台拒入门两处同根修复：面板级 `slotLeft/slotRight` 分派函数补门禁（原门禁只在内联 TapHandler，
   分派函数是无门禁旁路）；`localCanPlace` 的 `Array.isArray` 判定臂改序列兼容遍历。
4. 视觉半边：Main.qml HUD 攻击行 / AnvilUI tooltip 攻击行的 `Array.isArray(e)` 守卫改真值守卫
   （旧版附魔物品悬停不显锐锋加成 = 「附魔看似丢了」的观感放大器）。

## 请你在新版上核对的分层数据（逐条回答即可）

1. **放入瞬间**：带附魔工具左键放进铁砧 A 槽 → 槽内物品还有紫色光晕吗？
2. **槽内悬停**：tooltip 还有附魔行（如「锐锋 III」紫字）吗？
3. **取出瞬间**：再点 A 槽拿回光标 → 光标物品光晕还在吗？
4. **放回背包后**：背包格里光晕还在吗？悬停有附魔行吗？
5. **关面板归还**：A/B 槽有物时直接 E 关面板 → 物品回背包，附魔还在吗？
6. **存档重进**：退出世界再进 → 背包里附魔工具/附魔书/改名物品全部保真吗？
7. **铁砧四操作**：修复 / 双件合并 / 敲附魔书 / 改名，产物附魔都在吗？
8. **附魔台拒入**：已附魔工具左键点附魔台输入槽 → 应该**放不进去**（物品留在光标上）。
9. **附魔台正链**：素镐 + 青金石附魔 → 取出带附魔；再想放回去应被拒绝。

## 探针位置

矩阵测试新增段 `t874` / `t875`（tools/redstone_matrix_test.cpp）：真 QQmlEngine × 源树 AnvilUI.qml /
EnchantingTableUI.qml × 真 C++ Hotbar/PlayerState，覆盖放入/取出全入口（左/右键、Shift、拖动、双击、
数字键）、四类别（工具/武器满配/护甲/附魔书）、takeProduct 四 op、关包归还、存档 round-trip 后重绑 VM、
已附魔七入口拒入 + 全链无清洗 + 素品附魔正链。阴性验证两轮（分别禁用 readSlot 归一 /
resolveClick 归一）均使对应探针转红后还原。
