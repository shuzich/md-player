// 唯一持有 mpv_handle 的地方（docs/ARCHITECTURE.md §PlayerController）。
// 渲染由 MpvObject 借用本类的 handle 建立 mpv_render_context，不另开 handle。
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

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

    // 播放定位符即 mpv URI（bd:// / dvd:// / sacd:// / 普通路径）。
    Q_INVOKABLE void load(const QString& uri);
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

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void hasMediaChanged();
    void mediaTitleChanged();
    void videoInfoChanged();
    void mpvWokeUp(); // 内部：把 mpv 线程的唤醒转到 GUI 线程
    void errorOccurred(const QString& message);

private slots:
    void drainEvents();

private:
    static void onWakeup(void* ctx);
    void observe(const char* name, mpv_format format);
    void handlePropertyChange(mpv_event_property* prop);
    static QString locateBaselineConf();

    mpv_handle* mpv_ = nullptr;
    QString pendingUri_;
    bool renderReady_ = false;
    double position_ = 0.0;
    double duration_ = 0.0;
    bool paused_ = true;
    bool hasMedia_ = false;
    QString mediaTitle_;
    QString videoInfo_;
};

// 进程内唯一实例（main 中构造后设置），供 MpvObject 取用。
PlayerController* playerInstance();
void setPlayerInstance(PlayerController* controller);

} // namespace md::core
