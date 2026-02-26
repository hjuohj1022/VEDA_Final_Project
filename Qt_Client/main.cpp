#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>

#include "Backend.h"

// 프로그램 진입점
int main(int argc, char *argv[])
{
    // 고해상도 DPI 스케일링 설정(구버전 Qt 대응)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Team3");
    app.setOrganizationDomain("team3.com");
    app.setApplicationName("Team3VideoReceiver");

    // 기본 Qt Quick Controls 스타일 지정
    QQuickStyle::setStyle("Basic");

    Backend backend;
    QQmlApplicationEngine engine;

    // Backend 인스턴스를 QML 컨텍스트에 주입
    engine.rootContext()->setContextProperty("backend", &backend);

    // QML 메인 모듈 로드
    engine.loadFromModule("Team3VideoReceiver", "Main");

    return app.exec();
}
