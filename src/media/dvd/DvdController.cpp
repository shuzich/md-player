#include "media/dvd/DvdController.h"

#include "app/strings.h"
#include "core/PlayerController.h"
#include "media/DiscModel.h"

#include <QDebug>
#include <QFileInfo>
#include <QVariantMap>

namespace md::media::dvd {

namespace {

// 「NTSC 720x480 · ac3 5.1/dts 5.1 · 4 字幕」这种一行摘要。
QString describe(const TitleInfo& t) {
    QStringList parts;
    if (!t.videoFormat.isEmpty())
        parts << t.videoFormat;
    if (t.angleCount > 1)
        parts << QStringLiteral("%1 视角").arg(t.angleCount);
    // 只列编解码器，不列声道数：IFO 的 audio_attr.channels 并不可靠——张学友那张
    // DVD 的 DTS 轨 IFO 写 5 声道，实际码流是 6 声道（mpv 实测）。声道数以播控条上
    // 由 mpv 提供的音轨列表为准，面板这行只是个粗略提示。与蓝光侧口径一致。
    if (!t.audioStreams.isEmpty()) {
        QStringList codecs;
        for (const StreamInfo& s : t.audioStreams)
            if (!s.codec.isEmpty() && !codecs.contains(s.codec))
                codecs << s.codec;
        parts << codecs.join(QStringLiteral("/"));
    }
    if (!t.subtitleStreams.isEmpty())
        parts << QStringLiteral("%1 字幕").arg(t.subtitleStreams.size());
    return parts.join(QStringLiteral(" · "));
}

QVariantList chaptersToVariant(const TitleInfo& t) {
    QVariantList out;
    for (const ChapterInfo& c : t.chapters) {
        QVariantMap m;
        m[QStringLiteral("number")] = c.number;
        m[QStringLiteral("start")] = c.startSeconds;
        // DVD 的 IFO 结构里根本没有章节名这一项，一律显示编号。
        m[QStringLiteral("label")] = QStringLiteral("第 %1 章").arg(c.number);
        m[QStringLiteral("named")] = false;
        out.append(m);
    }
    return out;
}

} // namespace

DvdController::DvdController(md::core::PlayerController* player, QObject* parent) : QObject(parent), player_(player) {
    hideShortTitles_ = md::media::hideShortTitlesSetting();
}

void DvdController::setHideShortTitles(bool hide) {
    if (hideShortTitles_ == hide)
        return;
    hideShortTitles_ = hide;
    md::media::setHideShortTitlesSetting(hide);
    emit hideShortTitlesChanged();
    rebuildVisibleModel();
    emit visibleChanged();
}

void DvdController::rebuildVisibleModel() {
    visible_ = md::media::filterVisible(playlists_, hideShortTitles_, currentIndex_);
}

void DvdController::rebuildTitleModel() {
    playlists_.clear();
    for (int i = 0; i < disc_.titles.size(); ++i) {
        const TitleInfo& t = disc_.titles.at(i);
        QVariantMap m;
        m[QStringLiteral("index")] = i;
        m[QStringLiteral("playlistId")] = t.titleNumber;
        m[QStringLiteral("titleLabel")] = QStringLiteral("标题 %1").arg(t.titleNumber, 2, 10, QLatin1Char('0'));
        m[QStringLiteral("duration")] = t.durationSeconds;
        m[QStringLiteral("durationText")] = md::media::formatDuration(t.durationSeconds);
        m[QStringLiteral("chapterCount")] = t.chapters.size();
        m[QStringLiteral("summary")] = describe(t);
        m[QStringLiteral("isMainTitle")] = t.isMainTitle;
        m[QStringLiteral("chapters")] = chaptersToVariant(t);
        playlists_.append(m);
    }
    rebuildVisibleModel();
}

bool DvdController::openUrl(const QUrl& url) {
    return openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

bool DvdController::openPath(const QString& path) {
    if (!looksLikeDvd(path))
        return false;

    DiscInfo info = open(path);

    // 「后缀是 .iso 但不是 DVD」的情况（SACD 镜像、数据盘镜像）交还上层继续分派。
    if (info.status == OpenStatus::NotDvd)
        return false;

    switch (info.status) {
    case OpenStatus::Encrypted:
        qWarning("加密 DVD 拦截: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kEncryptedDisc));
        return true;
    case OpenStatus::OpenFailed:
        qWarning("DVD 打开失败: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscOpenFailed));
        return true;
    case OpenStatus::NoTitles:
        qWarning("DVD 无可用标题: %s | %s", qUtf8Printable(path), qUtf8Printable(info.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscNoPlaylists));
        return true;
    case OpenStatus::NotDvd: // 上面已返回，列出来是为了让编译器检查覆盖完整
    case OpenStatus::Ok:
        break;
    }

    disc_ = std::move(info);
    currentIndex_ = -1;
    rebuildTitleModel();
    emit discChanged();
    emit visibleChanged();
    emit currentIndexChanged();

    const QString mainSummary =
        disc_.mainTitleIndex >= 0 ? describe(disc_.titles.at(disc_.mainTitleIndex)) : QStringLiteral("(无)");
    qInfo("DVD 已打开: %s | 碟名=%s | title=%d 条（列表显示 %d 条，隐藏 %d 条短标题）| 主标题=#%d [%s]",
          qUtf8Printable(disc_.rootPath), qUtf8Printable(disc_.discName), int(disc_.titles.size()),
          int(visible_.size()), hiddenCount(), disc_.mainTitleIndex, qUtf8Printable(mainSummary));

    if (disc_.mainTitleIndex >= 0)
        playIndex(disc_.mainTitleIndex);
    return true;
}

void DvdController::playIndex(int index) {
    playChapter(index, -1);
}

void DvdController::playChapter(int index, int chapterNumber) {
    if (!player_ || index < 0 || index >= disc_.titles.size())
        return;
    const TitleInfo& t = disc_.titles.at(index);

    if (index == currentIndex_ && chapterNumber >= 1) {
        player_->jumpToChapter(chapterNumber - 1);
        return;
    }

    player_->loadDvd(disc_.rootPath, t.titleNumber, chapterNumber);
    if (currentIndex_ != index) {
        currentIndex_ = index;
        emit currentIndexChanged();
        rebuildVisibleModel();
        emit visibleChanged();
    }
}

bool DvdController::takeTitleHint() {
    return md::media::takeTitleHintSetting();
}

void DvdController::closeDisc() {
    disc_ = DiscInfo{};
    playlists_.clear();
    visible_.clear();
    currentIndex_ = -1;
    emit discChanged();
    emit visibleChanged();
    emit currentIndexChanged();
}

} // namespace md::media::dvd
