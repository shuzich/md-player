#include "media/bluray/BlurayController.h"

#include "app/strings.h"
#include "core/PlayerController.h"

#include <QDebug>
#include <QFileInfo>
#include <QVariantMap>

namespace md::media::bluray {

namespace {

QString formatDuration(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600, 2, 10, QLatin1Char('0'))
        .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// 「hevc 2160p · SDR · 3 音轨 · 2 字幕」这种一行摘要，面板里给用户看的。
QString describe(const PlaylistInfo& p) {
    QStringList parts;
    QString video = p.videoCodec;
    if (!p.videoFormat.isEmpty())
        video += (video.isEmpty() ? QString() : QStringLiteral(" ")) + p.videoFormat;
    if (!video.isEmpty())
        parts << video;
    if (!p.dynamicRange.isEmpty())
        parts << (p.hdrPlus ? p.dynamicRange + QStringLiteral("+") : p.dynamicRange);
    if (!p.audioStreams.isEmpty()) {
        QStringList codecs;
        for (const StreamInfo& s : p.audioStreams)
            if (!s.codec.isEmpty() && !codecs.contains(s.codec))
                codecs << s.codec;
        parts << codecs.join(QStringLiteral("/"));
    }
    if (!p.subtitleStreams.isEmpty())
        parts << QStringLiteral("%1 字幕").arg(p.subtitleStreams.size());
    return parts.join(QStringLiteral(" · "));
}

QVariantList chaptersToVariant(const PlaylistInfo& p) {
    QVariantList out;
    for (const ChapterInfo& c : p.chapters) {
        QVariantMap m;
        m[QStringLiteral("number")] = c.number;
        m[QStringLiteral("start")] = c.startSeconds;
        // 章节名拿不到就降级为编号（M1-PLAN T3）。
        m[QStringLiteral("label")] = c.name.isEmpty() ? QStringLiteral("第 %1 章").arg(c.number) : c.name;
        m[QStringLiteral("named")] = !c.name.isEmpty();
        out.append(m);
    }
    return out;
}

} // namespace

BlurayController::BlurayController(md::core::PlayerController* player, QObject* parent)
    : QObject(parent), player_(player) {
    qInfo("%s", qUtf8Printable(runtimeCapabilities()));
}

void BlurayController::rebuildPlaylistModel() {
    playlists_.clear();
    for (int i = 0; i < disc_.playlists.size(); ++i) {
        const PlaylistInfo& p = disc_.playlists.at(i);
        QVariantMap m;
        m[QStringLiteral("index")] = i;
        m[QStringLiteral("playlistId")] = static_cast<int>(p.playlistId);
        m[QStringLiteral("mpls")] = QStringLiteral("%1.mpls").arg(p.playlistId, 5, 10, QLatin1Char('0'));
        m[QStringLiteral("duration")] = p.durationSeconds;
        m[QStringLiteral("durationText")] = formatDuration(p.durationSeconds);
        m[QStringLiteral("chapterCount")] = p.chapters.size();
        m[QStringLiteral("summary")] = describe(p);
        m[QStringLiteral("isMainTitle")] = p.isMainTitle;
        m[QStringLiteral("chapters")] = chaptersToVariant(p);
        playlists_.append(m);
    }
}

bool BlurayController::openUrl(const QUrl& url) {
    return openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

bool BlurayController::openPath(const QString& path) {
    if (!looksLikeBluray(path))
        return false;

    DiscInfo info = open(path);

    // .iso 的预判是「后缀对得上」而已，真读下来不是蓝光就交还给普通播放路径
    // ——SACD / DVD 的 ISO 会走到这里，它们由 T4 / T6 的模块接手。
    if (info.status == OpenStatus::NotBluray)
        return false;

    switch (info.status) {
    case OpenStatus::Encrypted:
        qWarning("加密盘拦截: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kEncryptedDisc));
        return true;
    case OpenStatus::OpenFailed:
        qWarning("蓝光打开失败: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscOpenFailed));
        return true;
    case OpenStatus::NoPlaylists:
        qWarning("蓝光无可用标题: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscNoPlaylists));
        return true;
    case OpenStatus::NotBluray: // 上面已返回，列出来是为了让编译器检查覆盖完整
    case OpenStatus::Ok:
        break;
    }

    disc_ = std::move(info);
    rebuildPlaylistModel();
    currentIndex_ = -1;
    emit discChanged();
    emit currentIndexChanged();

    int named = 0;
    for (const PlaylistInfo& p : disc_.playlists)
        for (const ChapterInfo& c : p.chapters)
            if (!c.name.isEmpty())
                ++named;
    qInfo("蓝光碟已打开: %s | 碟名=%s | playlist=%d 条 | 主标题=#%d | 章节名=%d 条 | BD-J=%d",
          qUtf8Printable(disc_.rootPath), qUtf8Printable(disc_.discName), int(disc_.playlists.size()),
          disc_.mainTitleIndex, named, int(disc_.bdjDetected));

    // 打开即播主标题：这是最常见的意图，完整列表仍在面板里随时可换。
    if (disc_.mainTitleIndex >= 0)
        playIndex(disc_.mainTitleIndex);
    return true;
}

void BlurayController::playIndex(int index) {
    playChapter(index, -1);
}

void BlurayController::playChapter(int index, int chapterNumber) {
    if (!player_ || index < 0 || index >= disc_.playlists.size())
        return;
    const PlaylistInfo& p = disc_.playlists.at(index);

    // 同一条 playlist 已在播时，换章节不必重新载入——直接 seek，省掉几秒钟的重开销。
    if (index == currentIndex_ && chapterNumber >= 1) {
        player_->jumpToChapter(chapterNumber - 1);
        return;
    }

    player_->loadBluray(disc_.rootPath, static_cast<int>(p.playlistId), chapterNumber);
    if (currentIndex_ != index) {
        currentIndex_ = index;
        emit currentIndexChanged();
    }
}

void BlurayController::closeDisc() {
    disc_ = DiscInfo{};
    playlists_.clear();
    currentIndex_ = -1;
    emit discChanged();
    emit currentIndexChanged();
}

} // namespace md::media::bluray
