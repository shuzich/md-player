// 蓝光模块的 QML 门面：持有当前碟的枚举结果，并把播放请求转给 PlayerController。
// 结构解析全在 BlurayDisc；本类只做 QML 数据形态转换与错误文案分发。
#pragma once

#include "media/bluray/BlurayDisc.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

namespace md::core {
class PlayerController;
}

namespace md::media::bluray {

class BlurayController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool discOpen READ discOpen NOTIFY discChanged)
    Q_PROPERTY(QString discName READ discName NOTIFY discChanged)
    Q_PROPERTY(QString discPath READ discPath NOTIFY discChanged)
    // playlists 始终是碟内**完整**结构（Studio 模式将来要用），面板展示用 visiblePlaylists。
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY discChanged)
    Q_PROPERTY(QVariantList visiblePlaylists READ visiblePlaylists NOTIFY visibleChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY visibleChanged)
    // 「隐藏 10 秒以下条目」开关，默认开启，状态落 QSettings（D-019）。
    Q_PROPERTY(bool hideShortTitles READ hideShortTitles WRITE setHideShortTitles NOTIFY hideShortTitlesChanged)
    Q_PROPERTY(int mainTitleIndex READ mainTitleIndex NOTIFY discChanged)
    // 当前正在播放的 playlist 在 playlists 中的下标；没有则 -1。
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)

public:
    explicit BlurayController(md::core::PlayerController* player, QObject* parent = nullptr);

    bool discOpen() const { return disc_.status == OpenStatus::Ok; }
    QString discName() const { return disc_.discName; }
    QString discPath() const { return disc_.rootPath; }
    QVariantList playlists() const { return playlists_; }
    QVariantList visiblePlaylists() const { return visible_; }
    int hiddenCount() const { return int(playlists_.size() - visible_.size()); }
    bool hideShortTitles() const { return hideShortTitles_; }
    void setHideShortTitles(bool hide);
    int mainTitleIndex() const { return disc_.mainTitleIndex; }
    int currentIndex() const { return currentIndex_; }

    // 判定 + 打开 + 自动播主标题。返回 false 表示这不是蓝光资源，调用方按普通文件处理。
    // 是蓝光但打不开（加密 / 损坏）时返回 true 并已发出 errorOccurred——那不该再退回普通播放。
    Q_INVOKABLE bool openPath(const QString& path);
    Q_INVOKABLE bool openUrl(const QUrl& url);
    Q_INVOKABLE void playIndex(int index);
    // chapterNumber 从 1 开始。
    Q_INVOKABLE void playChapter(int index, int chapterNumber);
    Q_INVOKABLE void closeDisc();
    // 首次调用返回 true 并落盘，之后恒为 false。用于「按 T 选择标题 / 章节」的一次性提示（D-020）。
    Q_INVOKABLE bool takeTitleHint();

signals:
    void discChanged();
    void visibleChanged();
    void hideShortTitlesChanged();
    void currentIndexChanged();
    void errorOccurred(const QString& message);

private:
    void rebuildPlaylistModel();
    void rebuildVisibleModel();

    md::core::PlayerController* player_ = nullptr;
    DiscInfo disc_;
    QVariantList playlists_;
    QVariantList visible_;
    bool hideShortTitles_ = true;
    int currentIndex_ = -1;
};

} // namespace md::media::bluray
