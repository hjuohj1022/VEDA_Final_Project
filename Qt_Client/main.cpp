#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "Backend.h"
#include "VlcPlayer.h"
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    // 고해상도 DPI 스케일링 설정
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Team3");
    app.setOrganizationDomain("team3.com");
    app.setApplicationName("Team3VideoReceiver");

    // VlcPlayer를 QML에서 사용할 수 있도록 등록
    qmlRegisterType<VlcPlayer>("Team3VideoReceiver", 1, 0, "VlcPlayer");

    // 기본 스타일 설정 (Basic) - 커스터마이징 가능한 스타일 사용
    QQuickStyle::setStyle("Basic");


    Backend backend;
    QQmlApplicationEngine engine;
    
    // Backend 인스턴스를 QML 컨텍스트 속성으로 설정
    engine.rootContext()->setContextProperty("backend", &backend);

    // QML 모듈에서 Main.qml 로드 (Qt 6.5+ 권장 방식)
    // QTP0001 정책에 따라 리소스 경로가 변경될 수 있으므로 모듈 로딩 방식이 안전합니다.
    engine.loadFromModule("Team3VideoReceiver", "Main");


    return app.exec();
}
