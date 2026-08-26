// libmpv render API 的 Qt Quick 承载体（CLAUDE.md 深坑 #1）。
// 参考 mpv-examples/libmpv/qml 的成熟模式，Qt5 专有调用已换成 Qt6 等价物：
//   resetOpenGLState()          -> QQuickWindow::begin/endExternalCommands()
//   setPersistentOpenGLContext()-> QQuickWindow::setPersistentGraphics()
// handle 不归本类所有，来自 PlayerController（ARCHITECTURE 要求唯一持有）。
#pragma once

#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickFramebufferObject>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace md::core {

class MpvRenderer;

class MpvObject : public QQuickFramebufferObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideo)

public:
    explicit MpvObject(QQuickItem* parent = nullptr);
    ~MpvObject() override;

    Renderer* createRenderer() const override;

    static void onRenderUpdate(void* ctx);

signals:
    void renderUpdateRequested();

private slots:
    void requestUpdate();

private:
    friend class MpvRenderer;

    mpv_handle* mpv_ = nullptr;                   // 借用，不释放
    mpv_render_context* renderContext_ = nullptr; // 本类持有并释放
};

} // namespace md::core
