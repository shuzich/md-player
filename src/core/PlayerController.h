// 唯一持有 mpv_handle 的地方（docs/ARCHITECTURE.md §PlayerController）。
// 渲染由 MpvObject 借用本类的 handle 建立 mpv_render_context，不另开 handle。
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <mpv/client.h>

namespace md::core {

class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY hasMediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(QString videoInfo READ videoInfo NOTIFY videoInfoChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int chapter READ chapter NOTIFY chapterChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(qint64 audioTrackId READ audioTrackId NOTIFY audioTrackIdChanged)
    Q_PROPERTY(qint64 subtitleTrackId READ subtitleTrackId NOTIFY subtitleTrackIdChanged)
    Q_PROPERTY(QString currentUri READ currentUri NOTIFY currentUriChanged)
    // MD_LOG_UI=1 时 QML 把提示条文案打到 stdout。截屏权限受限时，这是校验
    // 「用户到底看到了哪句话」的唯一客观手段——T3 的加密盘文案就是这么验的。
    Q_PROPERTY(bool uiLogEnabled READ uiLogEnabled CONSTANT)

public:
    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // 供 MpvObject 建立 render context 使用；所有权仍在本类。
    mpv_handle* handle() const { return mpv_; }

    double position() const { return position_; }
    double duration() const { return duration_; }
    bool paused() const { return paused_; }
    bool hasMedia() const { return hasMedia_; }
    QString mediaTitle() const { return mediaTitle_; }
    QString videoInfo() const { return videoInfo_; }
    double volume() const { return volume_; }
    bool muted() const { return muted_; }
    QVariantList chapters() const { return chapters_; }
    int chapter() const { return chapter_; }
    QVariantList audioTracks() const { return audioTracks_; }
    QVariantList subtitleTracks() const { return subtitleTracks_; }
    qint64 audioTrackId() const { return audioTrackId_; }
    qint64 subtitleTrackId() const { return subtitleTrackId_; }
    QString currentUri() const { return currentUri_; }
    static bool uiLogEnabled();

    // 播放定位符即 mpv URI（bd:// / dvd:// / sacd:// / 普通路径）。
    Q_INVOKABLE void load(const QString& uri);
    // 蓝光：先把 bluray-device 指到碟根（目录或 ISO 均可），再播 bd://mpls/<N>。
    // startChapter >= 1 时载入后直接跳到该章节，并跳过断点询问——用户点的是具体章节，
    // 这就是明确意图，不该再弹框问续播。
    Q_INVOKABLE void loadBluray(const QString& deviceRoot, int playlistId, int startChapter = -1);
    // 渲染上下文就绪前收到的加载请求先挂起。mpv 在初始化视频输出时若没有
    // render context，会直接把视频链路关掉（表现为只出声音、dwidth 永远不可用）。
    void setPendingUri(const QString& uri);
    Q_INVOKABLE void notifyRenderReady();
    Q_INVOKABLE void loadUrl(const QUrl& url);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seekRelative(double seconds);
    // 深坑 #2：拖动中贴关键帧即时出画，松手落精确点。T2 接进度条时使用。
    Q_INVOKABLE void seekDrag(double target);
    Q_INVOKABLE void seekExact(double target);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setAudioTrack(qint64 id);
    Q_INVOKABLE void setSubtitleTrack(qint64 id); // id < 0 表示关闭字幕
    Q_INVOKABLE void jumpToChapter(int index);
    // withSubtitles=true 用 "subtitles"（含字幕），false 用 "video"（纯画面）。
    Q_INVOKABLE void screenshot(bool withSubtitles);
    // 断点续播：QML 询问用户后调用其一。
    Q_INVOKABLE void resumeFromSaved();
    Q_INVOKABLE void discardSaved();

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void hasMediaChanged();
    void mediaTitleChanged();
    void videoInfoChanged();
    void volumeChanged();
    void mutedChanged();
    void chaptersChanged();
    void chapterChanged();
    void tracksChanged();
    void audioTrackIdChanged();
    void subtitleTrackIdChanged();
    void currentUriChanged();
    // 载入的文件有可续播记录时发出，QML 弹询问框。
    void resumeAvailable(double position, double duration);
    void screenshotSaved(const QString& path, bool withSubtitles);
    void mpvWokeUp(); // 内部：把 mpv 线程的唤醒转到 GUI 线程
    void errorOccurred(const QString& message);

private slots:
    void drainEvents();

private:
    static void onWakeup(void* ctx);
    void observe(const char* name, mpv_format format);
    void handlePropertyChange(mpv_event_property* prop);
    static QString locateBaselineConf();
    void refreshChapters();
    void refreshTracks();
    void rememberPosition();
    static QString screenshotDir();
    void noteSeekIssued(double target, const char* flags);
    void loadInternal(const QString& uri, const QString& resumeKey);

    mpv_handle* mpv_ = nullptr;
    class ResumeStore* resume_ = nullptr;
    QString currentUri_;
    // 断点记录的键。普通文件即 URI 本身；碟类资源的 URI（bd://mpls/1）在不同碟之间
    // 会重名，故用「碟根#URI」做键，避免张三的 mpls/1 续播到李四的 mpls/1 上。
    QString resumeKey_;
    QString pendingUri_;
    QString pendingResumeKey_;
    // 碟类资源的截图文件名词干。碟内没有 META 时 mpv 的 media-title 会退化成
    // 「1」这种无意义值（bd://mpls/1 的末段），故由 loadBluray 显式给定。
    QString screenshotStem_;
    int pendingChapter_ = -1;
    double pendingResumePos_ = 0.0;
    // seek 手感埋点（MD_LOG_SEEK=1 时启用）：记录最近一次 seek 的发出时刻与目标，
    // 待 time-pos 追上目标时算出「命令 -> 画面位置更新」的耗时。
    qint64 seekIssuedAtMs_ = 0;
    double seekTarget_ = -1.0;
    bool renderReady_ = false;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool paused_ = true;
    bool hasMedia_ = false;
    QString mediaTitle_;
    QString videoInfo_;
    double volume_ = 100.0;
    bool muted_ = false;
    QVariantList chapters_;
    int chapter_ = -1;
    QVariantList audioTracks_;
    QVariantList subtitleTracks_;
    qint64 audioTrackId_ = -1;
    qint64 subtitleTrackId_ = -1;
};

// 进程内唯一实例（main 中构造后设置），供 MpvObject 取用。
PlayerController* playerInstance();
void setPlayerInstance(PlayerController* controller);

} // namespace md::core
