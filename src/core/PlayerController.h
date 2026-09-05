// 唯一持有 mpv_handle 的地方（docs/ARCHITECTURE.md §PlayerController）。
// 渲染由 MpvObject 借用本类的 handle 建立 mpv_render_context，不另开 handle。
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <mpv/client.h>

namespace md::core {

class PlayerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY hasMediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(QString videoInfo READ videoInfo NOTIFY videoInfoChanged)
    // 当前资源有没有画面。拖动手感的配方（深坑 #2）只对有画面的资源成立，
    // 纯音频必须走另一套（D-055），所以这个判定要能被 QML 拿到。
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY videoInfoChanged)
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
    Q_PROPERTY(bool sacdGain READ sacdGain NOTIFY sacdGainChanged)
    // 设置页五项（T7 / D-070）。全部运行时下发，configs/mpv-baseline.conf 不动（D-013）。
    Q_PROPERTY(bool hwdecEnabled READ hwdecEnabled NOTIFY hwdecEnabledChanged)
    Q_PROPERTY(bool audioExclusive READ audioExclusive NOTIFY audioExclusiveChanged)
    Q_PROPERTY(bool audioPassthrough READ audioPassthrough NOTIFY audioPassthroughChanged)
    Q_PROPERTY(QString screenshotDirectory READ screenshotDirectory NOTIFY screenshotDirectoryChanged)
    Q_PROPERTY(int playlistPos READ playlistPos NOTIFY playlistPosChanged)

public:
    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // 供 MpvObject 建立 render context 使用；所有权仍在本类。
    mpv_handle* handle() const { return mpv_; }

    double position() const { return position_; }
    double duration() const { return duration_; }
    bool paused() const { return paused_; }
    bool buffering() const { return buffering_; }
    bool hasMedia() const { return hasMedia_; }
    QString mediaTitle() const { return mediaTitle_; }
    QString videoInfo() const { return videoInfo_; }
    bool hasVideo() const { return hasVideo_; }
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
    // DVD：先把 dvd-device 指到碟根（VIDEO_TS 的父目录或 ISO），再播 dvd://<N>。
    // titleNumber 是 libdvdread 口径的 1 起编号，mpv 的 dvd:// 是 0 起，这里负责换算（D-021）。
    Q_INVOKABLE void loadDvd(const QString& deviceRoot, int titleNumber, int startChapter = -1);
    // SACD：走自注册的 sacd:// 协议，实际字节由独立的 helper 进程供给（ARCHITECTURE §SACD）。
    // area 是 helper 侧的区下标，track 从 1 起。
    Q_INVOKABLE void loadSacd(const QString& isoPath, int area, int track);
    // 把同一区的后续曲目排进 mpv 播放列表。gapless-audio 只对播放列表内的切换生效，
    // 每首都用 loadfile replace 的话它就是摆设（T6 阶段 2 实测）。
    Q_INVOKABLE void enqueueSacd(const QString& isoPath, int area, int track);
    // SACD 的 +6dB 增益（深坑 #5）。默认开；临时开关走快捷键 G，正式安家 T7 设置页。
    Q_INVOKABLE void setSacdGain(bool on);
    bool sacdGain() const { return sacdGain_; }
    bool hwdecEnabled() const { return hwdecEnabled_; }
    bool audioExclusive() const { return audioExclusive_; }
    bool audioPassthrough() const { return audioPassthrough_; }
    QString screenshotDirectory() const;
    Q_INVOKABLE void setHwdecEnabled(bool on);
    Q_INVOKABLE void setAudioExclusive(bool on);
    Q_INVOKABLE void setAudioPassthrough(bool on);
    Q_INVOKABLE void setScreenshotDirectory(const QString& dir);
    Q_INVOKABLE QString hwdecCurrent() const;
    Q_INVOKABLE QString audioSpdifCurrent() const;
    int playlistPos() const { return playlistPos_; }
    Q_INVOKABLE int playlistCount() const;
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
    void bufferingChanged();
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
    void sacdGainChanged();
    void hwdecEnabledChanged();
    void audioExclusiveChanged();
    void audioPassthroughChanged();
    void screenshotDirectoryChanged();
    // 设置未能生效时必须让用户看见（D-051）：%1 是 mpv 的错误串。
    void settingFailed(const QString& why);
    void screenshotDirRejected(const QString& dir);
    void screenshotFailed(const QString& dir);
    void passthroughFellBack();
    void passthroughNoAudioOut();
    void audioOutUnavailable();
    void playlistPosChanged();
    // 一个条目真正载入完毕。往播放列表追加后续条目必须等这一刻——
    // loadfile replace 是异步的，抢在它前面 append 会被连锅端掉（T6 阶段 2 实测）。
    void fileLoaded();
    // 载入的文件有可续播记录时发出，QML 弹询问框。
    void resumeAvailable(double position, double duration);
    void screenshotSaved(const QString& path, bool withSubtitles);
    void mpvWokeUp(); // 内部：把 mpv 线程的唤醒转到 GUI 线程
    void errorOccurred(const QString& message);

private slots:
    void drainEvents();

private:
    void applySacdGain();

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
    void logCacheState();
    // 缓冲指示（D-066）。信号只取 demuxer-cache-state；不用 paused-for-cache
    // （实测整个卡顿期一次不触发，D-062/D-064），也不用 [seek] 耗时（量的是
    // position 属性更新，D-064）。
    void armBufferWatch();
    void tickBufferWatch();
    bool demuxerStalled();
    void setBuffering(bool on);
    QTimer bufferWatch_;
    qint64 bufferArmedMs_ = -1;
    int stalledStreak_ = 0;
    bool buffering_ = false;
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
    bool sacdGain_ = true;
    bool hwdecEnabled_ = true;
    bool audioExclusive_ = false;
    bool audioPassthrough_ = false;
    QString screenshotDir_;
    void loadSettings();
    void applyAudioDeviceOptions();
    void checkPassthroughTookEffect();
    bool audioOutHealthy() const;
    bool aoHealthyBeforePassthrough_ = false;
    qint64 lastProgressMs_ = -1;
    double lastProgressPos_ = -1.0;
    bool sacdActive_ = false;
    bool sacdFilterAttached_ = false; // 没挂过就别 af remove，否则 mpv 每次都警告一行
    int playlistPos_ = -1;
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
    bool hasVideo_ = false;
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
