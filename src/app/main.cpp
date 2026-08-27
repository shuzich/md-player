#include "app/strings.h"
#include "core/PlayerController.h"
#include "media/bluray/BlurayController.h"
#include "media/dvd/DvdController.h"
#include "media/router/RouterController.h"

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
    md::media::router::RouterController router(&controller, &bluray, &dvd);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("Bluray"), &bluray);
    engine.rootContext()->setContextProperty(QStringLiteral("Dvd"), &dvd);
    engine.rootContext()->setContextProperty(QStringLiteral("Router"), &router);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MdPlayer", "Main");

    // 命令行传入的首个参数当作待播放资源。判定、下探、错误文案与指纹日志
    // 全在 RouterController 里，与拖拽入口共用同一条路径（T5）。
    // 此刻渲染上下文还没建立，普通文件会被 setPendingUri 挂起，不会丢。
    const QStringList args = QGuiApplication::arguments();
    if (args.size() > 1)
        router.openPath(args.at(1));

    return QGuiApplication::exec();
}
