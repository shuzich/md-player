#include "app/strings.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

int main(int argc, char* argv[]) {
    // CLAUDE.md 深坑 #1：必须在创建任何窗口之前把 RHI 固定为 OpenGL。
    // macOS 默认走 Metal，会导致 T1 接入的 mpv render API 无法工作。
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QString::fromUtf8(md::strings::kAppName));
    QGuiApplication::setApplicationVersion(QString::fromUtf8(MD_PLAYER_VERSION));
    QGuiApplication::setOrganizationName(QString::fromUtf8(md::strings::kOrganizationName));
    QGuiApplication::setOrganizationDomain(QString::fromUtf8(md::strings::kOrganizationDomain));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("MdPlayer", "Main");

    return QGuiApplication::exec();
}
