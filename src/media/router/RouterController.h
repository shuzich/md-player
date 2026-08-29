// 统一资源入口（M1-PLAN T5）：命令行参数、拖拽、将来的「打开」菜单都走这里。
// 自己不解析碟结构——判定交给 DiscRouter，打开交给 Bluray / Dvd 两个 Controller，
// 普通文件直通 PlayerController。本类只负责串起来并统一错误文案与指纹日志。
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

namespace md::core {
class PlayerController;
}
namespace md::media::bluray {
class BlurayController;
}
namespace md::media::dvd {
class DvdController;
}
namespace md::media::sacd {
class SacdController;
}

namespace md::media::router {

class RouterController : public QObject {
    Q_OBJECT

public:
    RouterController(md::core::PlayerController* player, md::media::bluray::BlurayController* bluray,
                     md::media::dvd::DvdController* dvd, md::media::sacd::SacdController* sacd,
                     QObject* parent = nullptr);

    // 打开任意路径。总是「处理掉」——不认的东西也会落到 mpv 或给出明确文案，
    // 所以没有返回值，调用方不需要再兜底。
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);

signals:
    void errorOccurred(const QString& message);

private:
    void openDiscImage(const QString& imagePath);
    void logBlurayFingerprint(const QString& root);
    void logDvdFingerprint(const QString& root);

    md::core::PlayerController* player_ = nullptr;
    md::media::bluray::BlurayController* bluray_ = nullptr;
    md::media::dvd::DvdController* dvd_ = nullptr;
    md::media::sacd::SacdController* sacd_ = nullptr;
};

} // namespace md::media::router
