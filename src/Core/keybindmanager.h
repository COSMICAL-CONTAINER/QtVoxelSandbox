#ifndef KEYBINDMANAGER_H
#define KEYBINDMANAGER_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QObject>
#include <QString>
#include <QtQml/qqml.h>

// t1022 键位重映射（Core 叶子，单一权威）：settings.json 持久化链上的键位映射表。
//
// 「动作 → Qt 键」映射（13 个可映射动作；canonical = 引擎消费键 = 默认键）：
//   forward=W  back=S  left=A  right=D  jump=Space  sneak=Shift  inventory=E  drop=Q
//   chat=T  camera=F5  debugTime=F6  modeCycle=G  debugOverlay=F3。
//   表本体（kActionTable）是**单一权威**：QML 设置页行集 / PlayerController::setKey 规范化 /
//   矩阵探针（P-t1022 默认表完整性）三方同源。
//
// 两类查询（t976/t977 AOT 教训：QML 只在信号 handler 或 revision 触碰绑定里调 Q_INVOKABLE，
//   不做跨组件单元裸属性绑定）：
//   1) 正向 keyFor(action)：动作 → 当前映射键（QML keyInput 各分支比较用；设置页行显示用）；
//   2) 反向 canonicalKey(physicalKey)：物理键 → 归属动作的 canonical 键（PlayerController::setKey
//      入口规范化用）。canonical 恒等于该动作的默认键 —— 引擎内部（m_keys / step / shift 状态机）
//      **零改动**照旧读 Qt::Key_W/Space/Shift 等原始常量，重映射只在 setKey 单 choke 点翻译。
//      无归属的物理键返回 Key_unknown 丢弃（review0907 A-P2-2 4a 语义收紧：防改绑后旧键经透传
//      恒等继续驱动 —— 「forward 改 ↑ 后 W 仍前进」正是被修的隐藏别名缺陷）。
//
// 冲突检测（spec「一键多动作提示」）：applyBinding(action, key) 时 key 已被其它动作占用 → 拒收
//   （返回 kApplyConflict，映射不变）；QML 经 actionOfKey(key) 取占用方中文名提示。
//
// 持久化（settings.json "keyBindings" 节）：{"forward":87,...}（Qt::Key 整数值）。读：缺节 / 部分
//   缺行 / 值非法 → 该动作落默认（旧档向后兼容不炸）；未知动作名忽略（前向兼容）。写：读现有文件
//   保留其它字段（resourcePack / playerSkin 等），仅覆盖 keyBindings 节。settings.json 候选链与
//   resourcepackmanager.cpp resolveSettingsPath/resolveSettingsWritePath 同序（exe/../settings.json →
//   AppLocalData）——若未来增删候选两处同步改。写失败降级内存态（同 writeSettings 先例，告警不崩）。
//
// 测试密闭（t779/t785 显式路径先例）：setStorePath() 显式指存储路径（并即时重载）——矩阵探针指向
//   临时目录做「重映射→持久化→重载→映射保持」全程真链 round-trip，绝不触碰工程根真实 settings.json。
//   生产 Main.qml 不调用（空 = 默认候选链）。
//
// 分层（PLAN §2）：Core 叶子，只依赖 Qt Core/Gui（QKeySequence 出显示名）。被 Main.qml 实例化
//   （QML_NAMED_ELEMENT 门面，同 ResourcePackManager 模式）+ 经 PlayerController::keybinds
//   Q_PROPERTY 注入引擎侧（Game→Core 向下依赖合规）。
class KeybindManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(KeybindManager)
    // 映射版本号：任何映射变化（load / applyBinding / resetDefaults）++。QML 行显示绑定**触碰本值**
    //   再查 keyFor（t976 AOT 教训：Q_INVOKABLE 无 NOTIFY，绑定须有可见依赖才会随重映射刷新）。
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    // applyBinding 返回码（QML 冲突提示分流；0 = 成功并已持久化）。
    enum ApplyResult {
        ApplyOk = 0,        // 应用成功（内存 + settings.json）
        ApplyUnknownAction, // 动作 id 不在表内（QML 传错 id 的防线）
        ApplyConflict,      // 目标键已被其它动作占用（拒收；actionOfKey 可取占用方）
        ApplyForbiddenKey   // review0907 A-P2-2 4b：目标键在固定不可映射黑名单（Esc / 数字 1-9 / Enter /
                            //   B / G / 修饰键等登记口径固定键；拒收，映射不变。QML 录制器单出「该键为
                            //   固定功能键，不可绑定」文案）。例外：动作**自身 canonical 默认键**放行
                            //   （review0907 B #2 单向门修复——sneak 可单独绑回 Shift）。判定先于
                            //   ApplyConflict（#5：被占用的固定键报「固定键」非「冲突」）
    };
    Q_ENUM(ApplyResult)

    // 动作表条目（单一权威；id 是持久化键 + QML Repeater model id + 探针断言面）。
    struct ActionDef {
        const char *id;   // 动作 id（settings.json keyBindings 键名；稳定契约勿改）
        int canonical;    // 引擎消费键 = 默认键（Qt::Key 值）
    };

    explicit KeybindManager(QObject *parent = nullptr);

    int revision() const { return m_revision; }

    // 动作默认表（顺序稳定：设置页 / 探针遍历序）。静态纯表（无实例态，探针直调先例）。
    static const QList<ActionDef> &actionTable();

    // 正向查询：动作 id → 当前映射键。未知动作 → 0。
    Q_INVOKABLE int keyFor(const QString &action) const;
    // 反向规范化：物理键 → 归属动作的 canonical 键；**无归属 → Qt::Key_unknown（丢弃）**。
    //   review0907 A-P2-2 4a 语义收紧（原「无归属原样透传」为隐藏别名缺陷）：动作值集之外的物理键不再
    //   透传 —— 改绑 forward W→C 后物理 W 不在任何动作值集里 → 丢弃，不再经 canonical 恒等继续驱动前进
    //   （引擎消费点全部是绑定动作的 canonical 键，丢弃即「该键无动作」的正确语义）。旁路面不受影响：
    //   Esc / 鼠标 / 录制器走各自独立路径不经本 choke。PlayerController::setKey 消费（Key_unknown 早退）。
    Q_INVOKABLE int canonicalKey(int physicalKey) const;
    // 键 → 当前占用它的动作 id（空串 = 无占用；QML 冲突提示取占用方显示名）。
    Q_INVOKABLE QString actionOfKey(int key) const;
    // 应用重映射：见 ApplyResult。成功即写 settings.json（失败降级内存）+ revision++。
    Q_INVOKABLE int applyBinding(const QString &action, int key);
    // 全部动作恢复默认 + 持久化 + revision++（设置页「恢复默认键位」按钮）。
    Q_INVOKABLE void resetDefaults();
    // 键显示名（设置页行按钮文本）：QKeySequence NativeText（"W"/"Space"/"Shift"/"F5"/"↑"…）。
    Q_INVOKABLE QString keyDisplayName(int key) const;

    // 测试密闭：显式存储路径（空 = 默认候选链）；设置即重载（探针 round-trip 用）。生产不调用。
    Q_INVOKABLE void setStorePath(const QString &path);
    Q_INVOKABLE QString storePath() const;

signals:
    void revisionChanged();

private:
    void load();       // 从存储（显式路径或候选链首个存在的文件）读 keyBindings 节；缺/坏 → 全默认
    bool persist();    // 读现有文件保留其它字段，覆盖 keyBindings 节写回；失败告警返 false
    QString resolveReadPath() const;
    QString resolveWritePath() const;

    QHash<QString, int> m_bindings; // 动作 id → 当前映射键（与 actionTable 同键集）
    QString m_storePath;            // 显式存储路径（空 = 默认候选链）
    int m_revision = 1;             // 版本号（初值 1；任何映射变化 ++）
};

#endif // KEYBINDMANAGER_H
