#include "core/PlayerController.h"

#include "app/strings.h"
#include "core/ResumeStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantMap>

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

// 单调时钟，避免系统时间被调整时算出负数。
QElapsedTimer& seekClock() {
    static QElapsedTimer t = [] {
        QElapsedTimer x;
        x.start();
        return x;
    }();
    return t;
}
bool seekLogEnabled() {
    static const bool on = !qgetenv("MD_LOG_SEEK").isEmpty();
    return on;
}

// mpv 上游已不再随发行版提供 qthelper.hpp，这里自带最小的 node -> QVariant 转换。
QVariant nodeToVariant(const mpv_node* node) {
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return node->u.flag != 0;
    case MPV_FORMAT_INT64:
        return static_cast<qint64>(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; i++)
            list.append(nodeToVariant(&node->u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; i++)
            map.insert(QString::fromUtf8(node->u.list->keys[i]), nodeToVariant(&node->u.list->values[i]));
        return map;
    }
    default:
        return {};
    }
}

// 取一个属性并转成 QVariant；失败返回无效值。
QVariant getPropertyVariant(mpv_handle* mpv, const char* name) {
    mpv_node node;
    if (mpv_get_property(mpv, name, MPV_FORMAT_NODE, &node) < 0)
        return {};
    const QVariant v = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return v;
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
    observe("volume", MPV_FORMAT_DOUBLE);
    observe("mute", MPV_FORMAT_FLAG);
    observe("chapter", MPV_FORMAT_INT64);
    observe("chapter-list", MPV_FORMAT_NODE);
    observe("track-list", MPV_FORMAT_NODE);
    observe("aid", MPV_FORMAT_INT64);
    observe("sid", MPV_FORMAT_INT64);

    resume_ = new ResumeStore(this);

    connect(this, &PlayerController::mpvWokeUp, this, &PlayerController::drainEvents, Qt::QueuedConnection);
    mpv_set_wakeup_callback(mpv_, &PlayerController::onWakeup, this);
}

PlayerController::~PlayerController() {
    rememberPosition(); // 退出时把当前位置写进断点记录
    if (resume_)
        resume_->save();
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
        case MPV_EVENT_FILE_LOADED: {
            if (!hasMedia_) {
                hasMedia_ = true;
                emit hasMediaChanged();
            }
            refreshChapters();
            refreshTracks();
            // 有断点记录就问用户，不擅自跳转。
            if (resume_ && !currentUri_.isEmpty()) {
                const ResumeEntry e = resume_->lookup(currentUri_);
                if (e.isValid()) {
                    pendingResumePos_ = e.position;
                    qInfo("发现断点记录: 位置=%.3f / 时长=%.3f，等待用户选择", e.position, e.duration);
                    emit resumeAvailable(e.position, e.duration);
                }
            }
            break;
        }
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
        // seek 到位耗时：位置进入目标 ±1s 即认为画面已更新到位。
        if (seekTarget_ >= 0.0 && qAbs(position_ - seekTarget_) < 1.0) {
            qInfo("[seek] t=%lldms 到位 位置=%.3f  耗时=%lldms", seekClock().elapsed(), position_,
                  seekClock().elapsed() - seekIssuedAtMs_);
            seekTarget_ = -1.0;
        }
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
    } else if (name == QLatin1String("volume") && prop->format == MPV_FORMAT_DOUBLE) {
        volume_ = toDouble(prop->data);
        emit volumeChanged();
    } else if (name == QLatin1String("mute") && prop->format == MPV_FORMAT_FLAG) {
        muted_ = toBool(prop->data);
        emit mutedChanged();
    } else if (name == QLatin1String("chapter") && prop->format == MPV_FORMAT_INT64) {
        chapter_ = static_cast<int>(*static_cast<int64_t*>(prop->data));
        emit chapterChanged();
    } else if (name == QLatin1String("chapter-list")) {
        refreshChapters();
    } else if (name == QLatin1String("track-list")) {
        refreshTracks();
    } else if (name == QLatin1String("aid") && prop->format == MPV_FORMAT_INT64) {
        audioTrackId_ = *static_cast<int64_t*>(prop->data);
        emit audioTrackIdChanged();
    } else if (name == QLatin1String("sid") && prop->format == MPV_FORMAT_INT64) {
        subtitleTrackId_ = *static_cast<int64_t*>(prop->data);
        emit subtitleTrackIdChanged();
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
    rememberPosition(); // 切片前先把上一个文件的位置存下来
    currentUri_ = uri;
    pendingResumePos_ = 0.0;
    emit currentUriChanged();
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
    noteSeekIssued(target, "absolute+keyframes");
    mpv_command(mpv_, args);
}

// 深坑 #2：松手时精确落点。
void PlayerController::seekExact(double target) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray t = QByteArray::number(target);
    const char* args[] = {"seek", t.constData(), "absolute+exact", nullptr};
    noteSeekIssued(target, "absolute+exact");
    mpv_command(mpv_, args);
}

void PlayerController::setVolume(double volume) {
    if (!mpv_)
        return;
    double clamped = qBound(0.0, volume, 130.0); // 上限与 baseline.conf 的 volume-max 一致
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &clamped);
}

void PlayerController::setMuted(bool muted) {
    if (!mpv_)
        return;
    int flag = muted ? 1 : 0;
    mpv_set_property(mpv_, "mute", MPV_FORMAT_FLAG, &flag);
}

void PlayerController::setAudioTrack(qint64 id) {
    if (!mpv_)
        return;
    int64_t v = id;
    mpv_set_property(mpv_, "aid", MPV_FORMAT_INT64, &v);
}

void PlayerController::setSubtitleTrack(qint64 id) {
    if (!mpv_)
        return;
    if (id < 0) { // 关闭字幕：sid 需要字符串 "no"，不能用负数
        mpv_set_property_string(mpv_, "sid", "no");
        return;
    }
    int64_t v = id;
    mpv_set_property(mpv_, "sid", MPV_FORMAT_INT64, &v);
}

void PlayerController::jumpToChapter(int index) {
    if (!mpv_ || index < 0 || index >= chapters_.size())
        return;
    int64_t v = index;
    mpv_set_property(mpv_, "chapter", MPV_FORMAT_INT64, &v);
}

QString PlayerController::screenshotDir() {
    // M1-PLAN T2 指定落 ~/Pictures/md-player/。
    const QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString dir = base + QStringLiteral("/md-player");
    QDir().mkpath(dir);
    return dir;
}

void PlayerController::screenshot(bool withSubtitles) {
    if (!mpv_ || !hasMedia_)
        return;

    QString stem = QFileInfo(currentUri_).completeBaseName();
    if (stem.isEmpty())
        stem = QStringLiteral("screenshot");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString suffix = withSubtitles ? QStringLiteral("subs") : QStringLiteral("clean");
    const QString path = QStringLiteral("%1/%2_%3_%4.png").arg(screenshotDir(), stem, stamp, suffix);

    // "subtitles" = 含字幕与 OSD；"video" = 纯画面（不含任何叠加）。
    const QByteArray rawPath = path.toUtf8();
    const char* args[] = {"screenshot-to-file", rawPath.constData(), withSubtitles ? "subtitles" : "video", nullptr};
    if (const int rc = mpv_command(mpv_, args); rc < 0) {
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
        return;
    }
    qInfo("截图已保存: %s", qUtf8Printable(path));
    emit screenshotSaved(path, withSubtitles);
}

void PlayerController::refreshChapters() {
    if (!mpv_)
        return;
    const QVariantList raw = getPropertyVariant(mpv_, "chapter-list").toList();
    QVariantList out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QVariantMap m = raw.at(i).toMap();
        QVariantMap e;
        e[QStringLiteral("index")] = i;
        e[QStringLiteral("time")] = m.value(QStringLiteral("time")).toDouble();
        // 章节名可能缺省，降级为「章节 N」（ARCHITECTURE §bluray 的降级要求同理）。
        const QString title = m.value(QStringLiteral("title")).toString();
        e[QStringLiteral("title")] = title.isEmpty() ? QStringLiteral("章节 %1").arg(i + 1) : title;
        out.append(e);
    }
    if (out != chapters_) {
        chapters_ = out;
        if (!qgetenv("MD_LOG_TRACKS").isEmpty()) {
            qInfo("章节共 %lld 个:", static_cast<qint64>(out.size()));
            for (const QVariant& c : out)
                qInfo("  [%lld] %.3fs  %s", c.toMap()["index"].toLongLong(), c.toMap()["time"].toDouble(),
                      qUtf8Printable(c.toMap()["title"].toString()));
        }
        emit chaptersChanged();
    }
}

void PlayerController::refreshTracks() {
    if (!mpv_)
        return;
    const QVariantList raw = getPropertyVariant(mpv_, "track-list").toList();
    QVariantList audio;
    QVariantList subs;
    for (const QVariant& item : raw) {
        const QVariantMap m = item.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();

        QVariantMap e;
        e[QStringLiteral("id")] = m.value(QStringLiteral("id")).toLongLong();
        e[QStringLiteral("codec")] = m.value(QStringLiteral("codec")).toString();
        e[QStringLiteral("lang")] = m.value(QStringLiteral("lang")).toString();
        e[QStringLiteral("title")] = m.value(QStringLiteral("title")).toString();
        e[QStringLiteral("selected")] = m.value(QStringLiteral("selected")).toBool();
        e[QStringLiteral("channels")] = m.value(QStringLiteral("demux-channel-count")).toInt();

        // 显示名：优先轨道标题，其次语言，最后编码，都没有就用编号。
        QStringList parts;
        if (!e[QStringLiteral("title")].toString().isEmpty())
            parts << e[QStringLiteral("title")].toString();
        if (!e[QStringLiteral("lang")].toString().isEmpty())
            parts << e[QStringLiteral("lang")].toString();
        if (!e[QStringLiteral("codec")].toString().isEmpty())
            parts << e[QStringLiteral("codec")].toString();
        e[QStringLiteral("label")] = parts.isEmpty() ? QStringLiteral("#%1").arg(e[QStringLiteral("id")].toLongLong())
                                                     : parts.join(QStringLiteral(" · "));

        if (type == QLatin1String("audio"))
            audio.append(e);
        else if (type == QLatin1String("sub"))
            subs.append(e);
    }
    if (audio != audioTracks_ || subs != subtitleTracks_) {
        audioTracks_ = audio;
        subtitleTracks_ = subs;
        if (!qgetenv("MD_LOG_TRACKS").isEmpty()) {
            qInfo("音轨 %lld 条 / 字幕轨 %lld 条", static_cast<qint64>(audio.size()), static_cast<qint64>(subs.size()));
            for (const QVariant& t : audio)
                qInfo("  [音] id=%lld %s", t.toMap()["id"].toLongLong(), qUtf8Printable(t.toMap()["label"].toString()));
            for (const QVariant& t : subs)
                qInfo("  [字] id=%lld %s", t.toMap()["id"].toLongLong(), qUtf8Printable(t.toMap()["label"].toString()));
        }
        emit tracksChanged();
    }
}

void PlayerController::noteSeekIssued(double target, const char* flags) {
    if (!seekLogEnabled())
        return;
    seekIssuedAtMs_ = seekClock().elapsed();
    seekTarget_ = target;
    qInfo("[seek] t=%lldms 发出 %-18s 目标=%.3f", seekIssuedAtMs_, flags, target);
}

void PlayerController::rememberPosition() {
    if (resume_ && !currentUri_.isEmpty() && position_ > 0.0)
        resume_->remember(currentUri_, position_, duration_);
}

void PlayerController::resumeFromSaved() {
    qInfo("用户选择：继续播放 (%.3f)", pendingResumePos_);
    if (pendingResumePos_ > 0.0) {
        seekExact(pendingResumePos_);
        pendingResumePos_ = 0.0;
    }
}

void PlayerController::discardSaved() {
    qInfo("用户选择：从头播放，丢弃断点记录");
    pendingResumePos_ = 0.0;
    if (resume_ && !currentUri_.isEmpty())
        resume_->forget(currentUri_);
}

} // namespace md::core
