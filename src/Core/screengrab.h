#ifndef SCREENGRAB_H
#define SCREENGRAB_H

#include <QObject>
#include <QVariant>
#include <QtQml/qqml.h>
#include <QQuickWindow>   // 完整类型：grab(QQuickWindow*) 作 Q_INVOKABLE 参，moc 需其完整 metatype（前向声明致 moc 报「Pointer Meta Types must point to fully-defined types」+ 与 Q_DECLARE_METATYPE(QQuickWindow*) 冲突）

// t232 世界列表封面黑屏修复（窗口级截图工具）。
//
// 背景：旧封面用 QQuickItem::grabToImage() 对 View3D 抓帧，结果全黑。根因——View3D 的 3D 场景由
//   QtQuick3D 的独立渲染 pass 画进**窗口帧缓冲**（QSG 渲染阶段），而 grabToImage 只把该 Item 经
//   **2D 场景图**离屏重渲一次：重渲时 View3D 的 3D 纹理尚未（重新）生成 → 拿不到 3D 内容 → 空帧（全黑）。
//   这是「编译过 / 三测全过、运行期肉眼全黑」同族坑（3D 内容在 2D grab 路径拍不到），与 lessons-learned
//   「渲染盲区静态化」/「Loader 孤儿」同源——静态/编译期测不出。
//
// 修法：改用 QQuickWindow::grabWindow()。它把**整个场景图**（含 QtQuick3D 的 3D pass + 2D 项）离屏
//   重渲成 QImage，3D 场景会被画进结果 → 可靠拍到画面。配合 caller「抓帧前把 View3D 抬到最上层 z
//   （盖住暂停叠层 / HUD）→ 拍到无 UI 的纯场景」，封面即干净的 3D 场景缩略图。
//
// 时序：grab() 不立即抓，而是等 window 下一帧渲染完成（frameSwapped）再抓——保证 caller 先做的
//   QML 态变更（抬 View3D z 盖住 UI）已被渲染，grab 拍到的是盖好后的画面。frameSwapped 未发
//   （极端，窗口未渲染）由 caller 的兜底定时器收尾，不卡退出（PLAN §2-E）。
//
// 分层（PLAN §2）：仅依赖 Qt（Quick/Gui/Qml），**不** include 任何自有层（World/Renderer/Game/…），
//   依赖只向下铁律自然成立；置 Core 层作叶子工具（同 blockregistry 为 Core 叶子先例）。产物 QImage 经
//   QVariant 边界交回 QML，由 WorldStore（World 层）落盘——ScreenGrab 不接触存档语义。
class ScreenGrab : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ScreenGrab)
public:
    explicit ScreenGrab(QObject *parent = nullptr);

    // 请求抓取 window 当前画面（含 QtQuick3D View3D 的 3D 场景）：连一次性 frameSwapped → 下一帧
    //   渲染完后 grabWindow 离屏重渲 → 缩到缩略图尺寸 → 发 grabbed(QVariant<QImage>)。
    //   null window → 立即发 grabbed(空)，caller 降级不阻塞（§2-E）。
    //   重复调用（未完成的 grab 还在等 frameSwapped）→ 重置目标，避免双发。
    Q_INVOKABLE void grab(QQuickWindow *window);

signals:
    // 抓帧完成。image 为 QVariant 包 QImage（缩略图尺寸）；失败为 null QImage（caller 传 WorldStore
    //   saveCover，其内部 isNull 检查降级为「无封面」灰块）。
    void grabbed(const QVariant &image);

private:
    QQuickWindow *m_window = nullptr;   // 等待 frameSwapped 的目标窗口；抓完即清空
    void doGrab();                      // frameSwapped 触发：断开 → grabWindow → 缩放 → emit grabbed
};

#endif // SCREENGRAB_H
