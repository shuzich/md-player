#include "app/strings.h"
#include "core/PlayerController.h"
#include "media/bluray/BlurayController.h"
#include "media/dvd/DvdController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

#include <clocale>
#include <cstdio>

int main(int argc, char* argv[]) {
    // CLAUDE.md 深坑 #1：必须在创建任何窗口之前把 RHI 固定为 OpenGL。
    // macOS 默认走 Metal，会导致 mpv render API 无法工作。
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    // Qt 在 QGuiApplication 构造时设置了本地化 locale，而 libmpv 要求
    // LC_NUMERIC 为 "C"（否则小数点解析会出错）。必须在此改回。
    std::setlocale(LC_NUMERIC, "C");

    QGuiApplication::setApplicationName(QString::fromUtf8(md::strings::kAppName));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(MD_PLAYER_VERSION));
    QGuiApplication::setOrganizationName(QString::fromUtf8(md::strings::kOrganizationName));
    QGuiApplication::setOrganizationDomain(QString::fromUtf8(md::strings::kOrganizationDomain));

    md::core::PlayerController controller;
    md::core::setPlayerInstance(&controller);

    md::media::bluray::BlurayController bluray(&controller);
    md::media::dvd::DvdController dvd(&controller);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("Bluray"), &bluray);
    engine.rootContext()->setContextProperty(QStringLiteral("Dvd"), &dvd);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MdPlayer", "Main");

    // 命令行传入的首个参数当作待播放资源（T5 的统一路由入口先留在此）。
    // 分派顺序：蓝光 → DVD → 普通播放。前两个都是「不认就返回 false」，
    // 所以 SACD / 数据盘 ISO 会一路落到普通播放上（T6 接手后再插进来）。
    // 用 setPendingUri 而非 load：此刻渲染上下文还没建立。
    const QStringList args = QGuiApplication::arguments();
    if (args.size() > 1) {
        const QString& target = args.at(1);
        if (!bluray.openPath(target) && !dvd.openPath(target))
            controller.setPendingUri(target);
    }

    return QGuiApplication::exec();
}
