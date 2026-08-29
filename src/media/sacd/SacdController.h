// SACD 模块的 QML 门面。属性与方法名与 Bluray / Dvd 两个 Controller 对齐（鸭子类型），
// TitlePanel 拿到哪一个都能直接驱动。SACD 没有「章节」，曲目就是最小单位，
// 于是每条曲目的 chapterCount 恒为 0，面板自然不显示展开箭头。
#pragma once

#include "media/sacd/SacdTypes.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace md::core {
class PlayerController;
}

namespace md::media::sacd {

class SacdController : public QObject {
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
    explicit SacdController(md::core::PlayerController* player, QObject* parent = nullptr);

    bool discOpen() const { return disc_.ok; }
    QString discName() const { return disc_.album; }
    QString discPath() const { return disc_.isoPath; }
    QVariantList playlists() const { return playlists_; }
    QVariantList visiblePlaylists() const { return visible_; }
    int hiddenCount() const { return int(playlists_.size() - visible_.size()); }
    bool hideShortTitles() const { return hideShortTitles_; }
    void setHideShortTitles(bool hide);
    int mainTitleIndex() const { return mainTitleIndex_; }
    int currentIndex() const { return currentIndex_; }

    // 返回 false = 这不是 SACD（或 helper 不可用），调用方继续往下分派。
    Q_INVOKABLE bool openPath(const QString& path);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void playChapter(int index, int chapterNumber); // SACD 无章节，等价 playIndex
    Q_INVOKABLE void closeDisc();
    Q_INVOKABLE bool takeTitleHint();

    // 指纹日志用的显示名（碟上没有专辑名时的降级链在 openPath 里定）。
    QString albumLabel() const { return disc_.album; }

signals:
    void discChanged();
    void visibleChanged();
    void hideShortTitlesChanged();
    void currentIndexChanged();
    void errorOccurred(const QString& message);

private:
    void onPlaylistPos();
    void flushQueue();
    void rebuildTitleModel();
    void rebuildVisibleModel();

    md::core::PlayerController* player_ = nullptr;
    DiscInfo disc_;
    QVariantList playlists_;
    QVariantList visible_;
    bool hideShortTitles_ = true;
    int mainTitleIndex_ = -1;
    int currentIndex_ = -1;
    int queueStartIndex_ = -1; // 播放列表第 0 项对应的曲目下标
    QVector<int> pendingQueue_;  // 等 fileLoaded 后再追加的曲目号
};

} // namespace md::media::sacd
