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
//   鼠标键（攻击/放置/中键拾取是鼠标路径，非键盘域）。
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
bool isValidKeyValue(int key)
{
    return key >= 0x20 && key != Qt::Key_unknown;
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
                    if (isValidKeyValue(key))
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
    return physicalKey;             // 无归属 → 原样透传（未登记键 / 未重绑旧键语义保留）
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
    bool found = false;
    for (const ActionDef &def : kActionTable())
        if (QString::fromLatin1(def.id) == action) { found = true; break; }
    if (!found || !isValidKeyValue(key))
        return int(ApplyUnknownAction);
    const QString owner = actionOfKey(key);
    if (!owner.isEmpty() && owner != action)
        return int(ApplyConflict);   // 一键多动作 → 拒收（QML 提示占用方）
    if (m_bindings.value(action) == key)
        return int(ApplyOk);         // 幂等：同值重复应用零副作用（不重写盘 / 不抖 revision）
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
    if (!isValidKeyValue(key))
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
