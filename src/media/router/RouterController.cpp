#include "media/router/RouterController.h"

#include "app/strings.h"
#include "core/PlayerController.h"
#include "media/Fingerprint.h"
#include "media/bluray/BlurayController.h"
#include "media/dvd/DvdController.h"
#include "media/router/DiscRouter.h"
#include "media/sacd/SacdController.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace md::media::router {

namespace {

const char* kindName(Kind k) {
    switch (k) {
    case Kind::Bluray:
        return "bluray";
    case Kind::Dvd:
        return "dvd";
    case Kind::Sacd:
        return "sacd";
    case Kind::DiscImage:
        return "disc-image";
    case Kind::Plain:
        return "plain";
    case Kind::Ambiguous:
        return "ambiguous";
    case Kind::Truncated:
        return "truncated";
    case Kind::NotFound:
        return "not-found";
    }
    return "?";
}

} // namespace

RouterController::RouterController(md::core::PlayerController* player, md::media::bluray::BlurayController* bluray,
                                   md::media::dvd::DvdController* dvd, md::media::sacd::SacdController* sacd,
                                   QObject* parent)
    : QObject(parent), player_(player), bluray_(bluray), dvd_(dvd), sacd_(sacd) {}

void RouterController::openUrl(const QUrl& url) {
    // 非本地 URL（http/rtsp/…）没有碟根可谈，直接交给 mpv——别拿去做文件系统
    // 判定，那只会得到一句「找不到这个路径」。
    if (!url.isLocalFile() && !url.isRelative()) {
        qInfo("路由: %s → 远程 URL，直通 mpv", qUtf8Printable(url.toString()));
        player_->setPendingUri(url.toString());
        return;
    }
    openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void RouterController::logBlurayFingerprint(const QString& root) {
    logFingerprint(md::media::computeBluray(root, bluray_->discName()));
}

void RouterController::logDvdFingerprint(const QString& root) {
    logFingerprint(md::media::computeDvd(root, dvd_->discName()));
}

// 镜像的具体形态要真打开才知道：蓝光 → DVD 依次试，都不认就交给 mpv
// （数据盘、游戏盘之类）。两个 Controller 都是「不认就返回 false」的约定。
void RouterController::openDiscImage(const QString& imagePath) {
    if (bluray_->openPath(imagePath)) {
        if (bluray_->discOpen())
            logBlurayFingerprint(imagePath);
        return;
    }
    if (dvd_->openPath(imagePath)) {
        if (dvd_->discOpen())
            logDvdFingerprint(imagePath);
        return;
    }
    qInfo("路由: 镜像不是蓝光也不是 DVD，交给 mpv: %s", qUtf8Printable(imagePath));
    player_->setPendingUri(imagePath);
}

void RouterController::openPath(const QString& path) {
    const Route r = resolve(path);
    qInfo("路由: %s → %s | target=%s%s%s", qUtf8Printable(path), kindName(r.kind), qUtf8Printable(r.target),
          r.detail.isEmpty() ? "" : " | ", qUtf8Printable(r.detail));

    switch (r.kind) {
    case Kind::NotFound:
        emit errorOccurred(QString::fromUtf8(md::strings::kPathNotFound));
        return;

    case Kind::Truncated:
        qWarning("镜像不完整: %s | %s", qUtf8Printable(r.target), qUtf8Printable(r.detail));
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscImageTruncated));
        return;

    case Kind::Ambiguous:
        qWarning("发现多个碟根: %s | %s", qUtf8Printable(r.target), qUtf8Printable(r.candidates.join(u'|')));
        emit errorOccurred(
            QString::fromUtf8(md::strings::kMultipleDiscRoots).arg(describeCandidates(r.target, r.candidates)));
        return;

    case Kind::Sacd:
        // T6 阶段 2 起真播。指纹的 label 用碟内专辑名（拿不到时由 SacdController
        // 按降级链定），比 ISO9660 卷标有意义得多——T5 遗留 5 就此关闭。
        if (sacd_ && sacd_->openPath(r.target)) {
            bluray_->closeDisc();
            dvd_->closeDisc();
            md::media::Fingerprint fp = md::media::computeSacd(r.target);
            if (!sacd_->albumLabel().isEmpty())
                fp.label = sacd_->albumLabel();
            logFingerprint(fp);
            return;
        }
        // openPath 失败时 SacdController 已经发过具体文案，这里不再叠一层。
        logFingerprint(md::media::computeSacd(r.target));
        return;

    case Kind::Bluray:
        if (bluray_->openPath(r.target)) {
            if (bluray_->discOpen())
                logBlurayFingerprint(r.target);
            return;
        }
        // 目录里确实有 BDMV/index.bdmv 却被判为「不是蓝光」，属于结构异常，
        // 不要悄悄退回普通播放——那只会让 mpv 报一句英文原文。
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscOpenFailed));
        return;

    case Kind::Dvd:
        if (dvd_->openPath(r.target)) {
            if (dvd_->discOpen())
                logDvdFingerprint(r.target);
            return;
        }
        emit errorOccurred(QString::fromUtf8(md::strings::kDiscOpenFailed));
        return;

    case Kind::DiscImage:
        openDiscImage(r.target);
        return;

    case Kind::Plain:
        // 目录走到这里说明下探没找着碟根。交给 mpv 只会得到一句英文的
        // unrecognized file format，所以在这里就给出明确文案（M1-PLAN T5
        // 「误拖普通文件夹」）。文件则照旧直通 mpv，它自己的报错更准确。
        if (QFileInfo(r.target).isDir()) {
            emit errorOccurred(QString::fromUtf8(md::strings::kNoDiscInFolder));
            return;
        }
        player_->setPendingUri(r.target);
        return;
    }
}

} // namespace md::media::router
