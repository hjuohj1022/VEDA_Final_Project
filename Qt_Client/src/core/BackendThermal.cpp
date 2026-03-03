#include "Backend.h"
#include <QImage>
#include <QColor>
#include <QtEndian>
#include <QDebug>
#include <QVariant>

// 열화상 해상도 (FLIR Lepton 3.5 기준 160x120)
static const int THERMAL_WIDTH = 160;
static const int THERMAL_HEIGHT = 120;
static const int FRAME_SIZE = THERMAL_WIDTH * THERMAL_HEIGHT * 2; // 38400 bytes

// 간단한 컬러맵 (Ironbow 유사)
static QColor getThermalColor(float normalized) {
    // 0.0 (차가움) -> 1.0 (뜨거움)
    // Blue -> Purple -> Red -> Yellow -> White
    if (normalized < 0.25f) {
        return QColor(0, 0, static_cast<int>(normalized * 4 * 255));
    } else if (normalized < 0.5f) {
        return QColor(static_cast<int>((normalized - 0.25f) * 4 * 255), 0, 255);
    } else if (normalized < 0.75f) {
        return QColor(255, 0, static_cast<int>(255 - (normalized - 0.5f) * 4 * 255));
    } else {
        return QColor(255, static_cast<int>((normalized - 0.75f) * 4 * 255), 0);
    }
}

// ---------------------------------------------------------
// (추가 작업 필요) Backend.cpp의 생성자나 별도 메서드에서 호출되어야 함
// ---------------------------------------------------------
void Backend::setupThermalWs() {
    // 이미 BackendStreamingWs.cpp에 WebSocket 관리 로직이 있으나, 
2    // 여기서는 개념 증명을 위해 독립적인 QWebSocket 사용 예시를 보여줍니다.
    
    static QWebSocket *thermalWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    connect(thermalWs, &QWebSocket::connected, this, []() {
        qInfo() << "[THERMAL] WebSocket Connected";
    });

    connect(thermalWs, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &data) {
        if (data.size() != FRAME_SIZE) {
            return;
        }

        const uint16_t *raw = reinterpret_cast<const uint16_t*>(data.constData());
        uint16_t minVal = 65535;
        uint16_t maxVal = 0;

        // 1. 통계 및 엔디안 변환
        QVector<uint16_t> frame(THERMAL_WIDTH * THERMAL_HEIGHT);
        for (int i = 0; i < frame.size(); ++i) {
            uint16_t v = qFromBigEndian(raw[i]);
            frame[i] = v;
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
        }

        // 2. 이미지 생성
        QImage img(THERMAL_WIDTH, THERMAL_HEIGHT, QImage::Format_RGB32);
        float range = static_cast<float>(maxVal - minVal);
        if (range <= 0) range = 1.0f;

        for (int y = 0; y < THERMAL_HEIGHT; ++y) {
            for (int x = 0; x < THERMAL_WIDTH; ++x) {
                uint16_t v = frame[y * THERMAL_WIDTH + x];
                float norm = (v - minVal) / range;
                img.setPixelColor(x, y, getThermalColor(norm));
            }
        }

        // 3. UI 업데이트용 속성 설정
        // 실제 운영 환경에서는 QQuickImageProvider를 쓰는 것이 효율적이나, 
        // 여기서는 간단히 QVariant(QImage)로 넘깁니다.
        m_thermalImage = QVariant::fromValue(img);
        m_minTemp = minVal / 100.0 - 273.15; // 켈빈 -> 섭씨 변환 가정
        m_maxTemp = maxVal / 100.0 - 273.15;

        emit thermalImageChanged();
        emit thermalDataChanged();
    });

    // 서버 주소로 연결 (API_URL 기반)
    QString wsUrl = serverUrl().replace("http", "ws") + "/thermal/ws";
    thermalWs->open(QUrl(wsUrl));
}
