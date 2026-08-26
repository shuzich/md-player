#include "core/MpvObject.h"

#include "core/PlayerController.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QtGlobal>

#include <stdexcept>

namespace md::core {

namespace {

void* getProcAddressMpv(void* ctx, const char* name) {
    Q_UNUSED(ctx)
    QOpenGLContext* gl = QOpenGLContext::currentContext();
    if (!gl)
        return nullptr;
    return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
}

void onMpvRedraw(void* ctx) {
    MpvObject::onRenderUpdate(ctx);
}

} // namespace

class MpvRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(MpvObject* obj) : obj_(obj) {}

    // 首帧时被调用；在此建立 mpv_render_context（需要一个当前 GL 上下文）。
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        if (!obj_->renderContext_) {
            mpv_opengl_init_params glInit{getProcAddressMpv, nullptr};
            mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
                                      {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                                      {MPV_RENDER_PARAM_INVALID, nullptr}};

            if (mpv_render_context_create(&obj_->renderContext_, obj_->mpv_, params) < 0)
                throw std::runtime_error("mpv_render_context_create 失败");

            mpv_render_context_set_update_callback(obj_->renderContext_, onMpvRedraw, obj_);
            qInfo("mpv render context 已建立 (OpenGL, %dx%d)", size.width(), size.height());
            // 本函数在渲染线程执行，必须排队回 GUI 线程再通知控制器。
            QMetaObject::invokeMethod(
                obj_,
                [] {
                    if (PlayerController* pc = playerInstance())
                        pc->notifyRenderReady();
                },
                Qt::QueuedConnection);
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() override {
        QQuickWindow* window = obj_->window();
        if (!window || !obj_->renderContext_)
            return;

        // Qt6 取代 Qt5 的 resetOpenGLState()：告知场景图我们要直接发 GL 命令。
        window->beginExternalCommands();

        QOpenGLFramebufferObject* fbo = framebufferObject();
        mpv_opengl_fbo mpfbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flipY = 0; // 与 mpv-examples 一致；QQuickFramebufferObject 自行处理朝向

        mpv_render_param params[]{{MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
                                  {MPV_RENDER_PARAM_FLIP_Y, &flipY},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(obj_->renderContext_, params);

        window->endExternalCommands();
    }

private:
    MpvObject* obj_;
};

MpvObject::MpvObject(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    PlayerController* controller = playerInstance();
    if (!controller)
        throw std::runtime_error("PlayerController 尚未创建，MpvObject 无 handle 可用");
    mpv_ = controller->handle();

    connect(this, &MpvObject::renderUpdateRequested, this, &MpvObject::requestUpdate, Qt::QueuedConnection);
}

MpvObject::~MpvObject() {
    // 只释放 render context；mpv_handle 归 PlayerController。
    if (renderContext_) {
        mpv_render_context_free(renderContext_);
        renderContext_ = nullptr;
    }
}

// mpv 渲染线程回调 → 队列信号转到 GUI 线程。
void MpvObject::onRenderUpdate(void* ctx) {
    auto* self = static_cast<MpvObject*>(ctx);
    emit self->renderUpdateRequested();
}

void MpvObject::requestUpdate() {
    update();
}

QQuickFramebufferObject::Renderer* MpvObject::createRenderer() const {
    if (QQuickWindow* w = window()) {
        w->setPersistentGraphics(true); // Qt6 取代 setPersistentOpenGLContext()
        w->setPersistentSceneGraph(true);
    }
    return new MpvRenderer(const_cast<MpvObject*>(this));
}

} // namespace md::core
