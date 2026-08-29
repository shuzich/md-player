#include "media/sacd/SacdController.h"

#include "app/strings.h"
#include "core/PlayerController.h"
#include "media/DiscModel.h"
#include "media/sacd/SacdProbe.h"

#include <QFileInfo>

namespace md::media::sacd {
namespace {

QString describe(const AreaInfo& area, const TrackInfo& t) {
    QStringList bits;
    bits << (area.multichannel ? QStringLiteral("多声道 %1ch").arg(area.channels)
                               : QStringLiteral("立体声 %1ch").arg(area.channels));
    bits << (area.dst ? QStringLiteral("DST 无损压缩") : QStringLiteral("纯 DSD"));
    if (!t.performer.isEmpty())
        bits << t.performer;
    return bits.join(QStringLiteral(" · "));
}

} // namespace

SacdController::SacdController(md::core::PlayerController* player, QObject* parent)
    : QObject(parent), player_(player) {
    hideShortTitles_ = md::media::hideShortTitlesSetting();
    connect(player_, &md::core::PlayerController::playlistPosChanged, this, &SacdController::onPlaylistPos);
    connect(player_, &md::core::PlayerController::fileLoaded, this, &SacdController::flushQueue);
}

// 首曲载入完毕，把同一区的后续曲目排进 mpv 播放列表，gapless-audio 才有事可做。
void SacdController::flushQueue() {
    if (pendingQueue_.isEmpty() || !disc_.ok || currentIndex_ < 0)
        return;
    const int area = playlists_.at(currentIndex_).toMap().value(QStringLiteral("area")).toInt();
    const QVector<int> queue = pendingQueue_;
    pendingQueue_.clear();
    for (int track : queue)
        player_->enqueueSacd(disc_.isoPath, area, track);
    qInfo("SACD 曲目队列: 后续排入 %lld 曲，mpv 播放列表共 %d 项（gapless-audio 生效于播放列表切换）",
          (long long)queue.size(), player_->playlistCount());
}

// 播放列表往前走一项 = 无缝切到下一曲，面板高亮跟着走。
void SacdController::onPlaylistPos() {
    if (!disc_.ok || queueStartIndex_ < 0)
        return;
    const int pos = player_->playlistPos();
    if (pos < 0)
        return;
    const int idx = queueStartIndex_ + pos;
    if (idx == currentIndex_ || idx >= playlists_.size())
        return;
    currentIndex_ = idx;
    const QVariantMap m = playlists_.at(idx).toMap();
    qInfo("SACD 无缝换轨: playlist-pos=%d → 第 %d 曲 %s", pos, m.value(QStringLiteral("track")).toInt(),
          qUtf8Printable(m.value(QStringLiteral("titleLabel")).toString()));
    rebuildVisibleModel();
    emit currentIndexChanged();
}

bool SacdController::openPath(const QString& path) {
    DiscInfo info = probe(path);
    if (!info.ok) {
        disc_ = DiscInfo{};
        playlists_.clear();
        visible_.clear();
        emit discChanged();
        emit errorOccurred(info.error.isEmpty() ? QString::fromUtf8(md::strings::kSacdOpenFailed) : info.error);
        return false;
    }
    // 专辑名降级链：碟内 SACDText → ISO9660 卷标（由指纹层给）→ 文件名去扩展名。
    // 手头三张 Sarah Brightman 就是碟上没有 master text 的实例，不给降级会显示空白。
    if (info.album.isEmpty())
        info.album = QFileInfo(path).completeBaseName();

    disc_ = info;
    currentIndex_ = -1;
    rebuildTitleModel();
    emit discChanged();
    emit currentIndexChanged();
    return true;
}

void SacdController::closeDisc() {
    if (!disc_.ok)
        return;
    disc_ = DiscInfo{};
    playlists_.clear();
    visible_.clear();
    currentIndex_ = -1;
    emit discChanged();
    emit visibleChanged();
    emit currentIndexChanged();
}

void SacdController::playIndex(int index) {
    if (!disc_.ok || index < 0 || index >= playlists_.size())
        return;
    const QVariantMap m = playlists_.at(index).toMap();
    if (!m.value(QStringLiteral("playable")).toBool()) {
        emit errorOccurred(QString::fromUtf8(md::strings::kSacdMultichannelNotYet));
        return;
    }
    currentIndex_ = index;
    queueStartIndex_ = index;
    const int area = m.value(QStringLiteral("area")).toInt();
    player_->loadSacd(disc_.isoPath, area, m.value(QStringLiteral("track")).toInt());
    // 同一区里后面的曲目排进播放列表，让 gapless-audio 真的有事可做（曲目间无缝）。
    // 跨区不排——多声道区本来就不可播。
    // 后续曲目不能现在就 append：loadfile replace 是异步的，抢在它前面追加会被
    // 它清空（实测 playlist-count 只剩 9，末曲于是变成「最后一项」，keep-open=yes
    // 让它停在曲末不再前进）。改为等 fileLoaded 到了再排。
    pendingQueue_.clear();
    for (int i = index + 1; i < playlists_.size(); ++i) {
        const QVariantMap n = playlists_.at(i).toMap();
        if (n.value(QStringLiteral("area")).toInt() != area || !n.value(QStringLiteral("playable")).toBool())
            break;
        pendingQueue_.append(n.value(QStringLiteral("track")).toInt());
    }
    rebuildVisibleModel();
    emit currentIndexChanged();
}

void SacdController::playChapter(int index, int chapterNumber) {
    Q_UNUSED(chapterNumber); // SACD 没有章节层级
    playIndex(index);
}

void SacdController::setHideShortTitles(bool hide) {
    if (hideShortTitles_ == hide)
        return;
    hideShortTitles_ = hide;
    md::media::setHideShortTitlesSetting(hide);
    rebuildVisibleModel();
    emit hideShortTitlesChanged();
}

bool SacdController::takeTitleHint() {
    return md::media::takeTitleHintSetting();
}

void SacdController::rebuildTitleModel() {
    playlists_.clear();
    mainTitleIndex_ = -1;
    for (const AreaInfo& area : disc_.areas) {
        for (const TrackInfo& t : area.tracks) {
            QVariantMap m;
            m[QStringLiteral("index")] = int(playlists_.size());
            m[QStringLiteral("area")] = area.area;
            m[QStringLiteral("track")] = t.index;
            // 多声道区 M1 只枚举不播放（ARCHITECTURE §SACD），面板复用现成的禁用态。
            m[QStringLiteral("playable")] = !area.multichannel;
            m[QStringLiteral("titleLabel")] =
                t.title.isEmpty() ? QStringLiteral("第 %1 曲").arg(t.index, 2, 10, QLatin1Char('0')) : t.title;
            m[QStringLiteral("duration")] = t.seconds;
            m[QStringLiteral("durationText")] = md::media::formatDuration(t.seconds);
            m[QStringLiteral("chapterCount")] = 0;
            m[QStringLiteral("chapters")] = QVariantList{};
            m[QStringLiteral("summary")] = describe(area, t);
            // 2ch 区的第一曲当作「主标题」：打开碟默认从这里播，与蓝光/DVD 口径一致。
            const bool first2ch = !area.multichannel && mainTitleIndex_ < 0;
            m[QStringLiteral("isMainTitle")] = first2ch;
            if (first2ch)
                mainTitleIndex_ = int(playlists_.size());
            playlists_.append(m);
        }
    }
    rebuildVisibleModel();
}

void SacdController::rebuildVisibleModel() {
    visible_ = md::media::filterVisible(playlists_, hideShortTitles_, currentIndex_);
    emit visibleChanged();
}

} // namespace md::media::sacd
