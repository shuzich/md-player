#include "core/PlayerController.h"

#include "app/strings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <stdexcept>

namespace md::core {

namespace {
PlayerController* g_instance = nullptr;

// mpv 用 int 表示布尔属性。
bool toBool(void* data) {
    return *static_cast<int*>(data) != 0;
}
double toDouble(void* data) {
    return *static_cast<double*>(data);
}
} // namespace

PlayerController* playerInstance() {
    return g_instance;
}
void setPlayerInstance(PlayerController* controller) {
    g_instance = controller;
}

QString PlayerController::locateBaselineConf() {
    // 依次尝试：环境变量覆盖 → 可执行文件旁 → 构建时记下的源码目录。
    const QByteArray fromEnv = qgetenv("MD_MPV_CONF");
    if (!fromEnv.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(fromEnv)))
        return QString::fromLocal8Bit(fromEnv);

    const QString beside = QCoreApplication::applicationDirPath() + "/configs/mpv-baseline.conf";
    if (QFileInfo::exists(beside))
        return beside;

    const QString inSource = QStringLiteral(MD_SOURCE_DIR) + "/configs/mpv-baseline.conf";
    if (QFileInfo::exists(inSource))
        return inSource;

    return {};
}

PlayerController::PlayerController(QObject* parent) : QObject(parent) {
    mpv_ = mpv_create();
    if (!mpv_)
        throw std::runtime_error("mpv_create 失败");

    // ---- mpv_initialize 之前必须设好的选项 ----
    // vo=libmpv 是 render API 的前提（无此项 render context 无法建立）。
    mpv_set_option_string(mpv_, "vo", "libmpv");
    // 不读用户 ~/.config/mpv，避免个人配置污染播放行为；基准配置显式 include。
    mpv_set_option_string(mpv_, "config", "no");

    const QString conf = locateBaselineConf();
    if (conf.isEmpty()) {
        qWarning("未找到 configs/mpv-baseline.conf，使用 mpv 内建默认值");
    } else {
        // include 会在 initialize 时被解析；其中未知选项只告警不致命。
        mpv_set_option_string(mpv_, "include", conf.toUtf8().constData());
        qInfo("已加载 mpv 基准配置: %s", qUtf8Printable(conf));
    }

    if (mpv_initialize(mpv_) < 0)
        throw std::runtime_error("mpv_initialize 失败");

    observe("time-pos", MPV_FORMAT_DOUBLE);
    observe("duration", MPV_FORMAT_DOUBLE);
    observe("pause", MPV_FORMAT_FLAG);
    observe("media-title", MPV_FORMAT_STRING);
    observe("dwidth", MPV_FORMAT_INT64);

    connect(this, &PlayerController::mpvWokeUp, this, &PlayerController::drainEvents, Qt::QueuedConnection);
    mpv_set_wakeup_callback(mpv_, &PlayerController::onWakeup, this);
}

PlayerController::~PlayerController() {
    if (g_instance == this)
        g_instance = nullptr;
    if (mpv_) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

void PlayerController::observe(const char* name, mpv_format format) {
    mpv_observe_property(mpv_, 0, name, format);
}

// 在 mpv 内部线程被调用，只能做线程安全的事：发一个队列信号回 GUI 线程。
void PlayerController::onWakeup(void* ctx) {
    auto* self = static_cast<PlayerController*>(ctx);
    emit self->mpvWokeUp();
}

void PlayerController::drainEvents() {
    if (!mpv_)
        return;
    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            handlePropertyChange(static_cast<mpv_event_property*>(event->data));
            break;
        case MPV_EVENT_FILE_LOADED:
            if (!hasMedia_) {
                hasMedia_ = true;
                emit hasMediaChanged();
            }
            break;
        case MPV_EVENT_END_FILE: {
            auto* ef = static_cast<mpv_event_end_file*>(event->data);
            if (ef->reason == MPV_END_FILE_REASON_ERROR)
                emit errorOccurred(QString::fromUtf8(mpv_error_string(ef->error)));
            break;
        }
        case MPV_EVENT_LOG_MESSAGE:
            break;
        default:
            break;
        }
    }
}

void PlayerController::handlePropertyChange(mpv_event_property* prop) {
    if (!prop)
        return;
    const QLatin1String name(prop->name);

    static const bool logProps = !qgetenv("MD_LOG_PROPS").isEmpty();
    if (logProps)
        qInfo("[prop] %s fmt=%d data=%p", prop->name, int(prop->format), prop->data);

    if (!prop->data)
        return;

    if (name == QLatin1String("time-pos") && prop->format == MPV_FORMAT_DOUBLE) {
        position_ = toDouble(prop->data);
        // 诊断用，默认关闭：MD_LOG_PROGRESS=1 时每跨过一整秒打一次点。
        static const bool logProgress = !qgetenv("MD_LOG_PROGRESS").isEmpty();
        if (logProgress) {
            static int lastWhole = -1;
            const int whole = static_cast<int>(position_);
            if (whole != lastWhole) {
                lastWhole = whole;
                qInfo("进度: %.3f / %.3f  暂停=%d", position_, duration_, paused_ ? 1 : 0);
            }
        }
        emit positionChanged();
    } else if (name == QLatin1String("duration") && prop->format == MPV_FORMAT_DOUBLE) {
        duration_ = toDouble(prop->data);
        emit durationChanged();
    } else if (name == QLatin1String("pause") && prop->format == MPV_FORMAT_FLAG) {
        paused_ = toBool(prop->data);
        if (!qgetenv("MD_LOG_PROGRESS").isEmpty())
            qInfo("暂停态 -> %s  (位置 %.3f)", paused_ ? "暂停" : "播放", position_);
        emit pausedChanged();
    } else if (name == QLatin1String("media-title") && prop->format == MPV_FORMAT_STRING) {
        mediaTitle_ = QString::fromUtf8(*static_cast<char**>(prop->data));
        emit mediaTitleChanged();
    } else if (name == QLatin1String("dwidth")) {
        // 拿到显示宽度即说明解码链已出画面参数，用于 T1 自检展示。
        char* w = mpv_get_property_string(mpv_, "dwidth");
        char* h = mpv_get_property_string(mpv_, "dheight");
        char* codec = mpv_get_property_string(mpv_, "video-format");
        char* hwdec = mpv_get_property_string(mpv_, "hwdec-current");
        videoInfo_ =
            QStringLiteral("%1x%2 %3 hwdec=%4")
                .arg(w ? QString::fromUtf8(w) : QStringLiteral("?"), h ? QString::fromUtf8(h) : QStringLiteral("?"),
                     codec ? QString::fromUtf8(codec) : QStringLiteral("?"),
                     hwdec ? QString::fromUtf8(hwdec) : QStringLiteral("?"));
        mpv_free(w);
        mpv_free(h);
        mpv_free(codec);
        mpv_free(hwdec);
        qInfo("视频参数: %s", qUtf8Printable(videoInfo_));
        emit videoInfoChanged();
    }
}

void PlayerController::setPendingUri(const QString& uri) {
    if (renderReady_)
        load(uri);
    else
        pendingUri_ = uri;
}

void PlayerController::notifyRenderReady() {
    if (renderReady_)
        return;
    renderReady_ = true;
    qInfo("渲染上下文就绪");
    if (!pendingUri_.isEmpty()) {
        const QString uri = pendingUri_;
        pendingUri_.clear();
        load(uri);
    }
}

void PlayerController::load(const QString& uri) {
    if (!mpv_ || uri.isEmpty())
        return;
    if (!renderReady_) { // 尚未就绪：挂起，等 notifyRenderReady 再发
        pendingUri_ = uri;
        return;
    }
    const QByteArray raw = uri.toUtf8();
    const char* args[] = {"loadfile", raw.constData(), nullptr};
    if (const int rc = mpv_command(mpv_, args); rc < 0)
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
}

void PlayerController::loadUrl(const QUrl& url) {
    load(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void PlayerController::togglePause() {
    if (!mpv_ || !hasMedia_)
        return;
    const char* args[] = {"cycle", "pause", nullptr};
    mpv_command(mpv_, args);
}

void PlayerController::seekRelative(double seconds) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray amount = QByteArray::number(seconds);
    const char* args[] = {"seek", amount.constData(), "relative", nullptr};
    mpv_command(mpv_, args);
}

// 深坑 #2：拖动过程贴关键帧，立刻有画面反馈。
void PlayerController::seekDrag(double target) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray t = QByteArray::number(target);
    const char* args[] = {"seek", t.constData(), "absolute+keyframes", nullptr};
    mpv_command(mpv_, args);
}

// 深坑 #2：松手时精确落点。
void PlayerController::seekExact(double target) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray t = QByteArray::number(target);
    const char* args[] = {"seek", t.constData(), "absolute+exact", nullptr};
    mpv_command(mpv_, args);
}

} // namespace md::core
