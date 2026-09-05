#include "core/PlayerController.h"

#include "app/strings.h"
#include "core/ResumeStore.h"
#include "media/sacd/SacdStream.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantMap>

#include <stdexcept>

namespace md::core {

namespace {

// aid / sid 关闭时（`--aid=no` / `--sid=no`）mpv 发来的 property-change 事件 format 是
// MPV_FORMAT_NONE 而不是 INT64——早先只认 INT64，于是「字幕：关」在 mpv 侧生效了、
// 播控条却还显示着上一条字幕轨，看起来像点了没反应（issue #2）。统一折算成 -1。
qint64 trackIdFromEvent(const mpv_event_property* prop) {
    if (prop->format != MPV_FORMAT_INT64)
        return -1;
    return *static_cast<int64_t*>(prop->data);
}

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
// 起显延迟：seek 发出后仍在等这么久，才认为值得打扰用户。落在缓存里的 seek
// 实测 34–83 ms，视频拖动的每次 keyframes seek 是几十 ms，都够不着这个门槛。
static constexpr qint64 kBufferHintDelayMs = 300;
// 轮询周期。要比起显延迟小，否则第一次检查就迟到。
static constexpr int kBufferPollMs = 150;
// 兜底：等到这个份上就别再点着灯了（正常最长一次实测 8.2 秒）。
static constexpr qint64 kBufferGiveUpMs = 120000;

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

bool PlayerController::uiLogEnabled() {
    static const bool on = !qgetenv("MD_LOG_UI").isEmpty();
    return on;
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

    // sacd:// 由本进程自己供给字节（T6 阶段 2）。必须在 initialize 之后注册。
    md::media::sacd::registerProtocol(mpv_);

    bufferWatch_.setInterval(kBufferPollMs);
    connect(&bufferWatch_, &QTimer::timeout, this, &PlayerController::tickBufferWatch);

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
    observe("playlist-pos", MPV_FORMAT_INT64);

    // helper 死了之后 mpv 只是静默停住、不发 end-file，用户看不到任何解释。
    // 解复用线程碰不了 QObject，所以让 GUI 侧每秒收一次旗标（T6 阶段 2 实测发现）。
    auto* sacdWatch = new QTimer(this);
    sacdWatch->setInterval(1000);
    connect(sacdWatch, &QTimer::timeout, this, [this]() {
        if (md::media::sacd::takeStreamFailure()) {
            qWarning("SACD helper 供流中断");
            emit errorOccurred(QString::fromUtf8(md::strings::kSacdHelperLost));
        }
    });
    sacdWatch->start();

    if (const QByteArray lvl = qgetenv("MD_LOG_MPV"); !lvl.isEmpty())
        mpv_request_log_messages(mpv_, lvl.constData());

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
            if (!qgetenv("MD_LOG_MPV").isEmpty()) {
                // H3：确认这条流在 mpv 眼里的可 seek 属性与它自己算出来的大小。
                // 注意：这一刻 dwidth 事件通常还没到，hasVideo_ 可能仍是初值。
                // 终态以下面 dwidth 分支打的「画面:」那行为准。
                for (const char* k :
                     {"video-format", "current-tracks/video/albumart", "seekable", "partially-seekable",
                      "demuxer-via-network", "file-size", "demuxer-cache-state/bof-cached", "current-demuxer"}) {
                    if (char* v = mpv_get_property_string(mpv_, k)) {
                        qInfo("[属性] %-32s = %s", k, v);
                        mpv_free(v);
                    } else {
                        qInfo("[属性] %-32s = <取不到>", k);
                    }
                }
            }
            emit fileLoaded();
            // 有断点记录就问用户，不擅自跳转。
            if (pendingChapter_ >= 1) {
                // 用户点的是具体章节：直接跳，且不再弹断点询问。
                int64_t target = pendingChapter_ - 1;
                mpv_set_property(mpv_, "chapter", MPV_FORMAT_INT64, &target);
                pendingChapter_ = -1;
            } else if (resume_ && !resumeKey_.isEmpty()) {
                const ResumeEntry e = resume_->lookup(resumeKey_);
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
        case MPV_EVENT_LOG_MESSAGE: {
            // MD_LOG_MPV=<级别> 时把 mpv 自己的日志转出来。查音频链路（ao 重启、
            // 欠载、滤镜重配）只能靠它——这些事件 mpv 不发结构化事件，只写日志。
            auto* m = static_cast<mpv_event_log_message*>(event->data);
            // 带上墙钟：查「seek 后过几秒才出声」这类问题，时间差就是全部证据。
            qInfo("[%8lldms][mpv/%s/%s] %s", seekClock().elapsed(), m->prefix, m->level,
                  QByteArray(m->text).trimmed().constData());
            break;
        }
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

    // aid / sid 必须在下面的空指针闸门之前处理：轨道关闭时 mpv 发的是 MPV_FORMAT_NONE
    // 事件，data 为空指针，会被闸门原样丢掉——「字幕：关」于是在 mpv 侧生效了、
    // 播控条却还显示着上一条字幕轨（issue #2）。
    if (name == QLatin1String("playlist-pos")) {
        const int p = prop->format == MPV_FORMAT_INT64 ? int(*static_cast<int64_t*>(prop->data)) : -1;
        if (p != playlistPos_) {
            playlistPos_ = p;
            emit playlistPosChanged();
        }
        return;
    }
    if (name == QLatin1String("aid")) {
        audioTrackId_ = trackIdFromEvent(prop);
        emit audioTrackIdChanged();
        return;
    }
    if (name == QLatin1String("sid")) {
        subtitleTrackId_ = trackIdFromEvent(prop);
        emit subtitleTrackIdChanged();
        return;
    }

    // dwidth 也是「能被关闭」的属性：纯音频资源上 mpv 发的是 MPV_FORMAT_NONE +
    // 空指针，会被下面的闸门整个吞掉（深坑 #10）。而「有没有画面」正是拖动手感
    // 走哪条路的开关（D-055），必须在闸门之前定下来。
    if (name == QLatin1String("dwidth")) {
        bool has = prop->format == MPV_FORMAT_INT64 && prop->data && *static_cast<int64_t*>(prop->data) > 0;
        // 内嵌封面也会被 mpv 报成视频轨、给出 dwidth——带封面的 flac 于是被误判成
        // 「有画面」，拖动又走回了 30Hz 连拍那条路。封面不是画面，按纯音频处理。
        if (has) {
            int flag = 0;
            if (mpv_get_property(mpv_, "current-tracks/video/albumart", MPV_FORMAT_FLAG, &flag) >= 0 && flag)
                has = false;
        }
        if (has != hasVideo_) {
            hasVideo_ = has;
            qInfo("画面: %s → 拖动走%s", has ? "有" : "无（纯音频）",
                  has ? "视频路径（30Hz keyframes，深坑 #2）" : "纯音频路径（松手才 seek，D-055）");
            emit videoInfoChanged();
        }
        if (!has) {
            videoInfo_.clear();
            emit videoInfoChanged();
            return;
        }
    }

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
        // MD_LOG_CACHE=1 顺带把 demuxer 缓存前沿打出来。SACD 的「seek 之后卡几秒」
        // 只能靠这个量定位：卡多久 = (目标 − 缓存前沿) ÷ DST 解码速度（约 20× 实时），
        // 与 readahead 无关（D-063 实测 10 与 3600 曲线一致）。
        static const bool logCache = !qgetenv("MD_LOG_CACHE").isEmpty();
        if (logProgress || logCache) {
            static int lastWhole = -1;
            const int whole = static_cast<int>(position_);
            if (whole != lastWhole) {
                lastWhole = whole;
                if (logProgress)
                    qInfo("进度: %.3f / %.3f  暂停=%d", position_, duration_, paused_ ? 1 : 0);
                if (logCache)
                    logCacheState();
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
    if (renderReady_) {
        load(uri);
    } else {
        pendingUri_ = uri;
        pendingResumeKey_ = uri;
    }
}

void PlayerController::notifyRenderReady() {
    if (renderReady_)
        return;
    renderReady_ = true;
    qInfo("渲染上下文就绪");
    if (!pendingUri_.isEmpty()) {
        const QString uri = pendingUri_;
        // 挂起期间存的可能是碟类资源的「碟根#URI」键，不能退回用 uri 本身当键，
        // 否则不同碟的 mpls/1 会共用一条断点记录。
        const QString key = pendingResumeKey_.isEmpty() ? uri : pendingResumeKey_;
        pendingUri_.clear();
        pendingResumeKey_.clear();
        loadInternal(uri, key);
    }
}

void PlayerController::load(const QString& uri) {
    pendingChapter_ = -1;
    screenshotStem_.clear();
    sacdActive_ = false;
    applySacdGain();
    loadInternal(uri, uri);
}

void PlayerController::loadBluray(const QString& deviceRoot, int playlistId, int startChapter) {
    if (!mpv_ || deviceRoot.isEmpty() || playlistId < 0)
        return;
    sacdActive_ = false;
    applySacdGain();
    // bluray-device 是全局选项，必须在 loadfile 之前设好；目录与 ISO 路径 libbluray 都能直接吃。
    const QByteArray rawDevice = deviceRoot.toUtf8();
    if (const int rc = mpv_set_property_string(mpv_, "bluray-device", rawDevice.constData()); rc < 0) {
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
        return;
    }
    pendingChapter_ = startChapter;
    screenshotStem_ = QStringLiteral("%1_mpls%2")
                          .arg(QFileInfo(deviceRoot).completeBaseName())
                          .arg(playlistId, 5, 10, QLatin1Char('0'));
    const QString uri = QStringLiteral("bd://mpls/%1").arg(playlistId);
    qInfo("蓝光播放: %s | playlist=%d | 起始章节=%d", qUtf8Printable(deviceRoot), playlistId, startChapter);
    loadInternal(uri, QStringLiteral("%1#%2").arg(deviceRoot, uri));
}

void PlayerController::loadDvd(const QString& deviceRoot, int titleNumber, int startChapter) {
    if (!mpv_ || deviceRoot.isEmpty() || titleNumber < 1)
        return;
    sacdActive_ = false;
    applySacdGain();
    const QByteArray rawDevice = deviceRoot.toUtf8();
    if (const int rc = mpv_set_property_string(mpv_, "dvd-device", rawDevice.constData()); rc < 0) {
        emit errorOccurred(QString::fromUtf8(mpv_error_string(rc)));
        return;
    }
    pendingChapter_ = startChapter;
    screenshotStem_ = QStringLiteral("%1_title%2")
                          .arg(QFileInfo(deviceRoot).completeBaseName())
                          .arg(titleNumber, 2, 10, QLatin1Char('0'));
    // mpv 的 dvd://N 是 0 起（实测：dvd://2 落在 libdvdread 的 title 3 上，等价于 --edition=2）。
    const QString uri = QStringLiteral("dvd://%1").arg(titleNumber - 1);
    qInfo("DVD 播放: %s | title=%d (%s) | 起始章节=%d", qUtf8Printable(deviceRoot), titleNumber, qUtf8Printable(uri),
          startChapter);
    loadInternal(uri, QStringLiteral("%1#%2").arg(deviceRoot, uri));
}

void PlayerController::loadSacd(const QString& isoPath, int area, int track) {
    if (!mpv_ || isoPath.isEmpty() || track < 1)
        return;
    pendingChapter_ = -1;
    screenshotStem_.clear(); // SACD 没有画面，截图无从谈起
    const QString uri = md::media::sacd::makeUri(isoPath, area, track);
    sacdActive_ = true;
    applySacdGain();
    qInfo("SACD 播放: %s | area=%d track=%d | uri=%s", qUtf8Printable(isoPath), area, track, qUtf8Printable(uri));
    loadInternal(uri, QStringLiteral("%1#area%2/track%3").arg(isoPath).arg(area).arg(track));
}

void PlayerController::enqueueSacd(const QString& isoPath, int area, int track) {
    if (!mpv_ || isoPath.isEmpty() || track < 1)
        return;
    const QString uri = md::media::sacd::makeUri(isoPath, area, track);
    const QByteArray raw = uri.toUtf8();
    const char* args[] = {"loadfile", raw.constData(), "append", nullptr};
    if (const int rc = mpv_command(mpv_, args); rc < 0)
        qWarning("SACD 排队失败: %s (%s)", qUtf8Printable(uri), mpv_error_string(rc));
}

int PlayerController::playlistCount() const {
    if (!mpv_)
        return 0;
    int64_t n = 0;
    if (mpv_get_property(mpv_, "playlist-count", MPV_FORMAT_INT64, &n) < 0)
        return 0;
    return int(n);
}

void PlayerController::setSacdGain(bool on) {
    if (sacdGain_ == on)
        return;
    sacdGain_ = on;
    applySacdGain();
    emit sacdGainChanged();
}

// DSF 经 ffmpeg 解为 PCM 后比 foobar2000 + SACD 插件低约 6dB（CLAUDE.md 深坑 #5）。
// 用 lavfi 的 volume 滤镜补，只在放 SACD 时挂上，换回别的资源就摘掉——
// 免得给普通片源平白加 6dB。
//
// 滤镜串的顺序是有讲究的，三段缺一不可（D-053）：
//
// 1) **aresample=88200 必须排在最前**。DSD 解出来是 352800 Hz，里面堆着 DSD 固有的
//    超声整形噪声（实测 30 kHz 以上 mean -30.1 dB / peak -14.3 dB）。不重采样的话
//    mpv 会照原样向 CoreAudio 要 352800 Hz，而内置扬声器只支持 44100/48000/88200/
//    96000——设备端只能自己做 7.35 倍降采样，那层抗混叠滤波不由我们控制，超声噪声
//    整片折回可听带，听感就是「沙」。而且每次 AO 重启（暂停/恢复、seek、换轨）都会
//    重新起这个转换器，所以杂音正好出现在这几个时刻。88200 是本机设备直接支持的
//    速率，也是 352800 的整数分频；即便别的机器不支持，此时信号已带限，
//    CoreAudio 再转一次也不会有混叠。
// 2) volume=6dB：DSF 经 ffmpeg 解为 PCM 后比 foobar2000 + SACD 插件低约 6dB
//    （CLAUDE.md 深坑 #5）。**必须排在重采样之后**，否则增益作用在还带着超声噪声的
//    信号上，白白抬高设备端要处理的峰值。
// 3) alimiter：DSD 母带常压到 -3.6 dBFS，加 6dB 就冲到 +3.5 dBFS，设备只能硬削
//    （实测整轨 160578 个样本越界，占 0.046%）。限幅器把峰值压到 -0.2 dBFS。
//    **同样必须在重采样之后**——否则它会被超声噪声的峰值牵着走，对可听内容做出
//    莫名其妙的增益起伏。
// 末尾 aformat 保住浮点：不加它 mpv 会把整条链路谈判成 s16（实测）。
void PlayerController::applySacdGain() {
    if (!mpv_)
        return;
    if (sacdFilterAttached_) {
        const char* args_clear[] = {"af", "remove", "@sacdgain", nullptr};
        mpv_command(mpv_, args_clear);
        sacdFilterAttached_ = false;
    }
    if (!sacdActive_ || !sacdGain_)
        return;
    const char* args_add[] = {"af", "add",
                              "@sacdgain:lavfi=[aresample=88200,volume=6dB,alimiter=limit=0.977:level=disabled,"
                              "aformat=sample_fmts=fltp]",
                              nullptr};
    if (const int rc = mpv_command(mpv_, args_add); rc < 0)
        qWarning("SACD 音频链挂载失败: %s", mpv_error_string(rc));
    else
        sacdFilterAttached_ = true;
}

void PlayerController::loadInternal(const QString& uri, const QString& resumeKey) {
    if (!mpv_ || uri.isEmpty())
        return;
    if (!renderReady_) { // 尚未就绪：挂起，等 notifyRenderReady 再发
        pendingUri_ = uri;
        pendingResumeKey_ = resumeKey;
        return;
    }
    rememberPosition(); // 切片前先把上一个文件的位置存下来
    currentUri_ = uri;
    resumeKey_ = resumeKey;
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
    armBufferWatch();
    mpv_command(mpv_, args);
}

// 深坑 #2：拖动过程贴关键帧，立刻有画面反馈。
void PlayerController::seekDrag(double target) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray t = QByteArray::number(target);
    const char* args[] = {"seek", t.constData(), "absolute+keyframes", nullptr};
    // 拖动中的 keyframes seek **不武装**缓冲看门狗。实测蓝光上一次 5 秒拖动发出
    // 65 次 keyframes seek，其中有一次远跳让解复用器停了 400 ms 以上，指示器就闪了
    // 一下（亮 1.37 秒）——判据没错，错在场景：拖动时用户盯着进度条，反馈不缺，
    // 闪一下反而是干扰。松手那次 absolute+exact 照旧武装，落点真要等仍然会提示。
    noteSeekIssued(target, "absolute+keyframes");
    mpv_command(mpv_, args);
}

// 深坑 #2：松手时精确落点。
void PlayerController::seekExact(double target) {
    if (!mpv_ || !hasMedia_)
        return;
    const QByteArray t = QByteArray::number(target);
    const char* args[] = {"seek", t.constData(), "absolute+exact", nullptr};
    armBufferWatch();
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
    qInfo("跳章节: #%d/%d (当前 %.1fs)", index + 1, int(chapters_.size()), position_);
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

    // bd://mpls/1 这类 URI 的 basename 是「1」，毫无意义；碟类资源用 loadBluray
    // 给定的「碟根名_mplsNNNNN」，比 media-title 可靠（无 META 的碟 title 也是「1」）。
    QString stem = screenshotStem_.isEmpty() ? QFileInfo(currentUri_).completeBaseName() : screenshotStem_;
    stem.remove(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")));
    stem = stem.trimmed();
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
        // 音轨再补一段声道描述。这个下拉框最常见的用途就是在「立体声」和「多声道」
        // 之间挑一条，而 codec 名（pcm_bluray / dsd_lsbf_planar）根本看不出声道数；
        // 只有一条音轨时下拉框是置灰的，这段描述就是它唯一还能给出的信息（D-058）。
        if (type == QLatin1String("audio")) {
            const int ch = e[QStringLiteral("channels")].toInt();
            if (ch == 1)
                parts << QStringLiteral("单声道");
            else if (ch == 2)
                parts << QStringLiteral("立体声 2ch");
            else if (ch > 2)
                parts << QStringLiteral("多声道 %1ch").arg(ch);
        }
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

// demuxer-cache-state 里我们只关心两件事：缓存前沿到了第几秒（cache-end），
// 以及为此占了多少内存（fw-bytes）。前沿之前的 seek 命中内存，之后的要等解码。
void PlayerController::logCacheState() {
    if (!mpv_)
        return;
    mpv_node node;
    if (mpv_get_property(mpv_, "demuxer-cache-state", MPV_FORMAT_NODE, &node) < 0)
        return;
    double cacheEnd = -1.0;
    int64_t fwBytes = -1;
    if (node.format == MPV_FORMAT_NODE_MAP) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const QLatin1String key(node.u.list->keys[i]);
            const mpv_node& v = node.u.list->values[i];
            if (key == QLatin1String("cache-end") && v.format == MPV_FORMAT_DOUBLE)
                cacheEnd = v.u.double_;
            else if (key == QLatin1String("fw-bytes") && v.format == MPV_FORMAT_INT64)
                fwBytes = v.u.int64;
        }
    }
    mpv_free_node_contents(&node);
    qInfo("[cache] t=%lldms 位置=%.1f 缓存前沿=%.1fs (领先 %.1fs) 前向占用=%.1fMB", seekClock().elapsed(), position_,
          cacheEnd, cacheEnd >= 0 ? cacheEnd - position_ : 0.0, fwBytes >= 0 ? double(fwBytes) / 1048576.0 : 0.0);
}

// ---------------------------------------------------------------------------
// 缓冲指示（D-066）
//
// 只做「有没有在等」，**不做进度百分比**：实测卡顿全程 demuxer-cache-state 的
// 每一个字段都是冻结的（cache-end=-1、fw-bytes=0、total-bytes 与 raw-input-rate
// 一动不动、underrun=true），mpv 侧也没有任何别的量在推进（stream-pos 返回 -1）
// ——解复用器线程阻塞在 avformat_seek_file 里，它自己都不知道进度。按前沿推进量
// 算出来的条只会卡在 0 再跳到 100%，那是假动画。
//
// **这里一个由模型推出来的常量都不许出现**（不许有 R / 19.65 / 时长÷20 / B）：
// DST 的解码速度逐碟不同，同一批实测就给出过 17.4 / 18.2 / 20.5 三个折合值，
// 硬编码任何一个都会在别的碟上偏。判据全部来自当场读数。
// ---------------------------------------------------------------------------

void PlayerController::armBufferWatch() {
    // 缓冲期间再次 seek：重置起点，不叠加（指示灯不会因为连点而提前亮）。
    bufferArmedMs_ = seekClock().elapsed();
    if (!bufferWatch_.isActive()) {
        bufferWatch_.setInterval(kBufferPollMs);
        bufferWatch_.start();
    }
}

bool PlayerController::demuxerStalled() {
    if (!mpv_)
        return false;
    mpv_node node;
    if (mpv_get_property(mpv_, "demuxer-cache-state", MPV_FORMAT_NODE, &node) < 0)
        return false;
    bool haveEnd = false, underrun = false;
    int64_t fwBytes = -1;
    if (node.format == MPV_FORMAT_NODE_MAP) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const QLatin1String key(node.u.list->keys[i]);
            const mpv_node& v = node.u.list->values[i];
            if (key == QLatin1String("cache-end") && v.format == MPV_FORMAT_DOUBLE)
                haveEnd = v.u.double_ >= 0.0;
            else if (key == QLatin1String("fw-bytes") && v.format == MPV_FORMAT_INT64)
                fwBytes = v.u.int64;
            else if (key == QLatin1String("underrun") && v.format == MPV_FORMAT_FLAG)
                underrun = v.u.flag != 0;
        }
    }
    mpv_free_node_contents(&node);
    // 正在 seek 时 mpv 连 cache-end 都不给（键直接不出现，或为负）；
    // 缓存被丢光而又还没铺回来时 fw-bytes=0 且 underrun。两者取或。
    return !haveEnd || (fwBytes == 0 && underrun);
}

void PlayerController::tickBufferWatch() {
    if (bufferArmedMs_ < 0) {
        bufferWatch_.stop();
        setBuffering(false);
        return;
    }
    const qint64 waited = seekClock().elapsed() - bufferArmedMs_;
    if (waited < kBufferHintDelayMs)
        return;
    if (waited > kBufferGiveUpMs) {
        bufferArmedMs_ = -1;
        bufferWatch_.stop();
        setBuffering(false);
        return;
    }
    if (demuxerStalled()) {
        setBuffering(true);
        return;
    }
    bufferArmedMs_ = -1;
    bufferWatch_.stop();
    setBuffering(false);
}

void PlayerController::setBuffering(bool on) {
    if (buffering_ == on)
        return;
    buffering_ = on;
    if (!qgetenv("MD_LOG_CACHE").isEmpty())
        qInfo("[buffer] t=%lldms 缓冲指示 -> %s（seek 后 %lld ms）", seekClock().elapsed(), on ? "显示" : "隐藏",
              bufferArmedMs_ >= 0 ? seekClock().elapsed() - bufferArmedMs_ : -1);
    emit bufferingChanged();
}

void PlayerController::noteSeekIssued(double target, const char* flags) {
    if (!seekLogEnabled())
        return;
    seekIssuedAtMs_ = seekClock().elapsed();
    seekTarget_ = target;
    qInfo("[seek] t=%lldms 发出 %-18s 目标=%.3f", seekIssuedAtMs_, flags, target);
}

void PlayerController::rememberPosition() {
    if (resume_ && !resumeKey_.isEmpty() && position_ > 0.0)
        resume_->remember(resumeKey_, position_, duration_);
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
    if (resume_ && !resumeKey_.isEmpty())
        resume_->forget(resumeKey_);
}

} // namespace md::core
