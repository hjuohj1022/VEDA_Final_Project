#include <QByteArray>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QWindow>

#include "Backend.h"
#include "ui/CaptureHelper.h"
#include "ui/QrCodeImageProvider.h"

// 애플리케이션 시작 함수
int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    qputenv("QT_FFMPEG_PROTOCOL_WHITELIST",
            QByteArray("file,crypto,data,udp,rtp,tcp,rtsp,rtsps,tls,http,https"));

    QGuiApplication app(argc, argv);
    app.setOrganizationName("VEDA_Team3");
    app.setOrganizationDomain("veda.team3.com");
    app.setApplicationName("AEGIS Vision VMS");
    app.setApplicationDisplayName("AEGIS Vision VMS");

    QIcon appIcon(":/qt/qml/Team3VideoReceiver/icons/Hanwha_logo.ico");
    if (appIcon.isNull())
        appIcon = QIcon(":/icons/Hanwha_logo.ico");
    if (!appIcon.isNull())
        app.setWindowIcon(appIcon);

    QQuickStyle::setStyle("Basic");

    Backend backend;
    CaptureHelper captureHelper;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("qrcode"), new QrCodeImageProvider());

    if (!appIcon.isNull()) {
        // QML top-level window icons
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                         [appIcon](QObject *obj, const QUrl &) {
            if (auto *window = qobject_cast<QWindow *>(obj)) {
                window->setIcon(appIcon);
            }
        });
    }

    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("captureHelper", &captureHelper);
    engine.loadFromModule("Team3VideoReceiver", "Main");

    if (!appIcon.isNull()) {
        const auto windows = app.topLevelWindows();
        for (QWindow *window : windows) {
            if (window)
                window->setIcon(appIcon);
        }
    }

    return app.exec();
}
