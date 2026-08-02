#include "screengrab.h"

#include <QImage>
#include <QQuickWindow>

// 缩略图最大边长（与旧 grabToImage(224×224) 同量级）：grabWindow 返回窗口原分辨率的 QImage，
//   缩到此尺寸既保 WorldList 44×44 缩略图清晰，又把封面 PNG 控制在几十 KB（不存全屏原图）。
static constexpr int kCoverMaxSide = 224;

ScreenGrab::ScreenGrab(QObject *parent)
    : QObject(parent)
{
}

void ScreenGrab::grab(QQuickWindow *window)
{
    if (!window) {
        // 无窗口（极端）→ 立即空结果，caller 降级（§2-E），绝不卡退出。
        emit grabbed(QVariant::fromValue(QImage()));
        return;
    }
    if (m_window) {
        // 重入：上一次 grab 仍在等 frameSwapped → 先断开旧连接，避免下一帧双发 grabbed。
        disconnect(m_window, &QQuickWindow::frameSwapped, this, &ScreenGrab::doGrab);
    }
    m_window = window;
    // 一次性：下一帧渲染完成（caller 已做的 QML 态变更此时已上屏）后再抓，拍到准确画面。
    // Qt::QueuedConnection：把 doGrab 推到 frameSwapped 发射之后的事件循环迭代执行，避免在 swap
    //   信号栈内重入 grabWindow（grabWindow 会协调渲染线程做离屏重渲，脱离 swap 发射更稳）。
    connect(window, &QQuickWindow::frameSwapped, this, &ScreenGrab::doGrab, Qt::QueuedConnection);
}

void ScreenGrab::doGrab()
{
    if (!m_window)
        return;
    // 断开一次性连接（防后续每帧重复抓）。
    disconnect(m_window, &QQuickWindow::frameSwapped, this, &ScreenGrab::doGrab);
    QQuickWindow *win = m_window;
    m_window = nullptr;

    // grabWindow：把整个场景图（含 QtQuick3D 3D pass + 2D 项）离屏重渲成 QImage → 拍到 3D 场景。
    QImage img = win->grabWindow();
    if (!img.isNull() && (img.width() > kCoverMaxSide || img.height() > kCoverMaxSide)) {
        // 缩到缩略图尺寸（保持宽高比，平滑）。WorldList 用 PreserveAspectCrop 裁中心方图显示。
        img = img.scaled(QSize(kCoverMaxSide, kCoverMaxSide),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    emit grabbed(QVariant::fromValue(img));
}
