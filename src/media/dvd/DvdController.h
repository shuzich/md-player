// DVD 模块的 QML 门面。属性与方法名刻意与 BlurayController 完全对齐（鸭子类型），
// TitlePanel 拿到哪一个都能直接驱动，不需要知道碟的类型。
#pragma once

#include "media/dvd/DvdDisc.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

namespace md::core {
class PlayerController;
}

namespace md::media::dvd {

class DvdController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool discOpen READ discOpen NOTIFY discChanged)
    Q_PROPERTY(QString discName READ discName NOTIFY discChanged)
    Q_PROPERTY(QString discPath READ discPath NOTIFY discChanged)
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY discChanged)
    Q_PROPERTY(QVariantList visiblePlaylists READ visiblePlaylists NOTIFY visibleChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY visibleChanged)
    Q_PROPERTY(bool hideShortTitles READ hideShortTitles WRITE setHideShortTitles NOTIFY hideShortTitlesChanged)
    Q_PROPERTY(int mainTitleIndex READ mainTitleIndex NOTIFY discChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)

public:
    explicit DvdController(md::core::PlayerController* player, QObject* parent = nullptr);

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

    // 返回 false = 这不是 DVD，调用方继续往下分派。是 DVD 但打不开（加密 / 损坏）
    // 时返回 true 并已发出 errorOccurred。
    Q_INVOKABLE bool openPath(const QString& path);
    Q_INVOKABLE bool openUrl(const QUrl& url);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void playChapter(int index, int chapterNumber); // chapterNumber 从 1 起
    Q_INVOKABLE void closeDisc();
    Q_INVOKABLE bool takeTitleHint();

signals:
    void discChanged();
    void visibleChanged();
    void hideShortTitlesChanged();
    void currentIndexChanged();
    void errorOccurred(const QString& message);

private:
    void rebuildTitleModel();
    void rebuildVisibleModel();

    md::core::PlayerController* player_ = nullptr;
    DiscInfo disc_;
    QVariantList playlists_;
    QVariantList visible_;
    bool hideShortTitles_ = true;
    int currentIndex_ = -1;
};

} // namespace md::media::dvd
