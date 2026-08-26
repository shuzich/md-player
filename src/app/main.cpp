#include "app/strings.h"
#include "core/PlayerController.h"

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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Player"), &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MdPlayer", "Main");

    // 命令行传入的首个参数当作待播放资源（T5 的统一路由入口先留在此）。
    // 用 setPendingUri 而非 load：此刻渲染上下文还没建立。
    const QStringList args = QGuiApplication::arguments();
    if (args.size() > 1)
        controller.setPendingUri(args.at(1));

    return QGuiApplication::exec();
}
