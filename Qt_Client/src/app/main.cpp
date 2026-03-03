#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>
#include <QByteArray>

#include "Backend.h"

// 프로그램 진입점
int main(int argc, char *argv[])
{
    // 고해상도 DPI 스케일링 설정(구버전 Qt 대응)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    // Qt FFmpeg 백엔드 로컬 SDP 재생 소스 오픈 시 RTP/UDP 프로토콜 허용 설정
    qputenv("QT_FFMPEG_PROTOCOL_WHITELIST",
            QByteArray("file,crypto,data,udp,rtp,tcp,rtsp,rtsps,tls,http,https"));

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Team3");
    app.setOrganizationDomain("team3.com");
    app.setApplicationName("Team3VideoReceiver");

    // 기본 Qt 퀵 컨트롤 스타일 지정
    QQuickStyle::setStyle("Basic");

    Backend backend;
    QQmlApplicationEngine engine;

    // 백엔드 인스턴스 QML 컨텍스트 주입
    engine.rootContext()->setContextProperty("backend", &backend);

    // QML 메인 모듈 로드
    engine.loadFromModule("Team3VideoReceiver", "Main");

    return app.exec();
}
