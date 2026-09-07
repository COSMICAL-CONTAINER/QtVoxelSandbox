// t1022 键位重映射实现（设计契约见 keybindmanager.h 头注释）。
#include "keybindmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtGui/qkeysequence.h>

namespace {

// 动作表本体（单一权威；canonical = 引擎消费键 = 默认键，PlayerController::setKey 规范化目标）。
//   运动六键 canonical 即引擎 m_keys / step() / 蹲态机内部消费的 Qt::Key 原始常量（playercontroller.cpp
//   6482-6485 行进移动、7156 行跳跃、Key_Shift 蹲态机）——重映射只在 setKey 入口翻译成 canonical，
//   引擎内部零改动。UI 动作（inventory/drop/chat/camera/debugTime/modeCycle/debugOverlay）canonical
//   即默认键（QML keyInput 分支经 keyFor 比较当前映射，不经 canonicalKey）。
//   固定不可映射键（登记口径，勿入表）：Esc（指针/菜单逃生口，重绑有软锁风险）、数字 1-9（hotbar
//   九槽阵列）、Enter/Return（chat 固定别名）、B/G（F3 组合弦修饰位）、Ctrl（丢弃整栈修饰位）、
//   鼠标键（攻击/放置/中键拾取是鼠标路径，非键盘域）。黑名单落实在 isBindableKey（仅绑定目标门，
//   显示层 keyDisplayName 不复用——review0907 B #1 回归教训）；自身 canonical 默认键例外放行。
const QList<KeybindManager::ActionDef> &kActionTable()
{
    static const QList<KeybindManager::ActionDef> table = {
        {"forward",       Qt::Key_W},
        {"back",          Qt::Key_S},
        {"left",          Qt::Key_A},
        {"right",         Qt::Key_D},
        {"jump",          Qt::Key_Space},
        {"sneak",         Qt::Key_Shift},
        {"inventory",     Qt::Key_E},
        {"drop",          Qt::Key_Q},
        {"chat",          Qt::Key_T},
        {"camera",        Qt::Key_F5},
        {"debugTime",     Qt::Key_F6},
        {"modeCycle",     Qt::Key_G},
        {"debugOverlay",  Qt::Key_F3},
    };
    return table;
}

// 合法键判定（settings.json 值域守卫）：0 / Key_unknown / <Space 非键值拒收 → 落默认。
bool isKeyLikeValue(int key)
{
    return key >= 0x20 && key != Qt::Key_unknown;
}

// 绑定目标黑名单（review0907 A-P2-2 4b：固定不可映射键，与登记口径同源全列）——原 isValidKeyValue
//   把「值域守卫」与「绑定黑名单」双职责合一，被 keyDisplayName 显示层复用后 sneak(Shift) /
//   modeCycle(G) 两行键名渲染成空串（review0907 B #1 回归）。现拆分：本判定只服务「绑定目标门」
//   （applyBinding / load），keyDisplayName 走 isKeyLikeValue 纯值域。
//   canonical 参数 = 该动作自身默认键：review0907 B #2 单向门修复——自身 canonical 放行（sneak 改绑
//   走后可单独绑回 Shift、modeCycle 绑回 G），其余动作仍恒拒（Shift 是组合弦修饰位，不可给 forward）。
//   注意：黑名单只拦「绑定目标」，不动 kActionTable 默认值（canonical 经构造 / resetDefaults 直播种，
//   不走本守卫）。鼠标键（攻击 / 放置 / 中键拾取）本就非键盘域，无对应键值可入，登记口径在此仅备案。
bool isBindableKey(int key, int canonical)
{
    if (!isKeyLikeValue(key)) return false;
    if (key == canonical) return true;        // 自身 canonical 默认键放行（单键绑回默认的唯一口）
    switch (key) {
    case Qt::Key_Escape:                      // 指针 / 菜单逃生口（重绑软锁风险）
    case Qt::Key_Return:
    case Qt::Key_Enter:                       // chat 固定别名
    case Qt::Key_B:
    case Qt::Key_G:                           // F3 组合弦修饰位
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:                        // 组合弦 / 丢弃整栈修饰位（Ctrl）及其余修饰键
    case Qt::Key_0:
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:                           // hotbar 九槽阵列（含 0 备用位，按登记口径全列）
        return false;
    default:
        return true;
    }
}

} // namespace

const QList<KeybindManager::ActionDef> &KeybindManager::actionTable()
{
    return kActionTable();
}

KeybindManager::KeybindManager(QObject *parent)
    : QObject(parent)
{
    load();
}

void KeybindManager::load()
{
    // 全默认起底（旧档缺节 / 部分缺行 / 值非法 → 该动作保持默认，向后兼容不炸）。
    QHash<QString, int> next;
    for (const ActionDef &def : kActionTable())
        next.insert(QString::fromLatin1(def.id), def.canonical);
    const QString path = resolveReadPath();
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            const QJsonObject kb = obj.value(QStringLiteral("keyBindings")).toObject();
            for (const ActionDef &def : kActionTable()) {
                const QJsonValue v = kb.value(QString::fromLatin1(def.id));
                if (v.isDouble()) {
                    const int key = int(v.toDouble());
                    // 黑名单守卫（isBindableKey）带自身 canonical：档内 "sneak":Shift（默认键）合法收录，
                    //   其余固定键值落默认（值域 + 绑定口径一致，review0907 A-P2-2 4b）。
                    if (isBindableKey(key, def.canonical))
                        next.insert(QString::fromLatin1(def.id), key);
                }
            }
        }
        // 文件缺 / 读失败 → 静默全默认（同 readSettings 缺省语义；构造期无 UI 无需告警）。
    }
    if (next != m_bindings) {
        m_bindings = next;
        ++m_revision;
        emit revisionChanged();
    }
}

bool KeybindManager::persist()
{
    const QString path = resolveWritePath();
    if (path.isEmpty()) {
        qWarning("Keybind: 无法解析 settings.json 写入路径；键位仅存内存。");
        return false;
    }
    // 读现有（保留 resourcePack / playerSkin 等其它字段）——同 resourcepackmanager writeSettings 管线。
    QJsonObject obj;
    QFile fin(path);
    if (fin.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(fin.readAll()).object();
        fin.close();
    }
    QJsonObject kb;
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it)
        kb.insert(it.key(), it.value());
    obj.insert(QStringLiteral("keyBindings"), kb);
    QFile fout(path);
    if (!fout.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("Keybind: 无法写入 settings.json %s（%s）；键位仅存内存。",
                 qPrintable(path), qPrintable(fout.errorString()));
        return false;
    }
    fout.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    fout.close();
    return true;
}

// settings.json 读候选链（与 resourcepackmanager.cpp resolveSettingsPath 同序；显式路径优先——测试密闭）。
QString KeybindManager::resolveReadPath() const
{
    if (!m_storePath.isEmpty())
        return m_storePath;
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .absoluteFilePath(QStringLiteral("settings.json")),
    };
    for (const QString &c : candidates)
        if (QFile::exists(c))
            return c;
    return {};
}

// 写路径（与 resourcepackmanager.cpp resolveSettingsWritePath 同序：沿用已存在文件，否则首候选）。
QString KeybindManager::resolveWritePath() const
{
    if (!m_storePath.isEmpty())
        return m_storePath;
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir + QStringLiteral("/..")).absoluteFilePath(QStringLiteral("settings.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .absoluteFilePath(QStringLiteral("settings.json")),
    };
    for (const QString &c : candidates)
        if (QFile::exists(c))
            return c;
    return candidates.first();
}

int KeybindManager::keyFor(const QString &action) const
{
    return m_bindings.value(action, 0);
}

int KeybindManager::canonicalKey(int physicalKey) const
{
    for (const ActionDef &def : kActionTable())
        if (m_bindings.value(QString::fromLatin1(def.id)) == physicalKey)
            return def.canonical;   // 命中归属动作 → 翻译成引擎 canonical 键
    // review0907 A-P2-2 4a：无归属 → 丢弃（Key_unknown）。原「原样透传」是隐藏别名缺陷——改绑
    //   forward W→C 后物理 W 不在任何动作值集里，透传恒等于 canonical 自身恒等（Key_W==Key_W），
    //   旧键继续驱动前进。引擎消费点（m_keys / step / 蹲态机）全部是绑定动作的 canonical 键，
    //   丢弃即「该键无动作」的正确语义；PlayerController::setKey 侧对称早退（press / release 同门）。
    return Qt::Key_unknown;
}

QString KeybindManager::actionOfKey(int key) const
{
    for (const ActionDef &def : kActionTable())
        if (m_bindings.value(QString::fromLatin1(def.id)) == key)
            return QString::fromLatin1(def.id);
    return {};
}

int KeybindManager::applyBinding(const QString &action, int key)
{
    int canonical = 0;
    bool found = false;
    for (const ActionDef &def : kActionTable())
        if (QString::fromLatin1(def.id) == action) { canonical = def.canonical; found = true; break; }
    // 顶部只查「像键值」（非 0 / 非 Key_unknown / ≥0x20）；黑名单细查后置（见下），保证拒收码分流
    //   正确：未知动作 → ApplyUnknownAction，固定键 → ApplyForbiddenKey。
    if (!found || !isKeyLikeValue(key))
        return int(ApplyUnknownAction);
    if (m_bindings.value(action) == key)
        return int(ApplyOk);         // 幂等：同值重复应用零副作用（不重写盘 / 不抖 revision）
    // review0907 B 收口：黑名单判定**先于** conflict（#5 二义修复——绑到被其它动作占用的固定键
    //   如仍属 modeCycle 的 G，报「固定键」而非「冲突 + 空键名」）；幂等判定在其前（同值重绑如
    //   modeCycle→G 保持 ApplyOk 不回归）。#2 单向门：自身 canonical 默认键放行（sneak 绑回 Shift）。
    if (!isBindableKey(key, canonical))
        return int(ApplyForbiddenKey);
    const QString owner = actionOfKey(key);
    if (!owner.isEmpty() && owner != action)
        return int(ApplyConflict);   // 一键多动作 → 拒收（QML 提示占用方）
    m_bindings.insert(action, key);
    persist();
    ++m_revision;
    emit revisionChanged();
    return int(ApplyOk);
}

void KeybindManager::resetDefaults()
{
    bool changed = false;
    for (const ActionDef &def : kActionTable()) {
        const QString id = QString::fromLatin1(def.id);
        if (m_bindings.value(id) != def.canonical) {
            m_bindings.insert(id, def.canonical);
            changed = true;
        }
    }
    if (!changed)
        return;                      // 已全默认 → 零副作用（不重写盘 / 不抖 revision）
    persist();
    ++m_revision;
    emit revisionChanged();
}

QString KeybindManager::keyDisplayName(int key) const
{
    // review0907 B #1 回归修复：显示层只拒「非键值」（isKeyLikeValue）——不再复用绑定黑名单
    //   （原 isValidKeyValue 双职责把 Shift / G 等默认键滤成空串，设置页 sneak / modeCycle 两行
    //   键名空白 + 冲突提示空键名）。黑名单键的显示名照常给出（Shift / G / Esc…）。
    if (!isKeyLikeValue(key))
        return {};
    return QKeySequence(key).toString(QKeySequence::NativeText);
}

void KeybindManager::setStorePath(const QString &path)
{
    if (m_storePath == path)
        return;
    m_storePath = path;
    load();                          // 指向新存储即重载（探针 round-trip 密闭语义）
}

QString KeybindManager::storePath() const
{
    return m_storePath;
}
