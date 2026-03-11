#include "Backend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMqttSubscription>
#include <QMqttTopicFilter>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>

void Backend::setupMqtt() {
    // MQTT 비활성화 시 네트워크 상태만 Disabled로 갱신
    const bool mqttEnabled = (m_env.value("MQTT_ENABLED", "1").trimmed() == "1");
    if (!mqttEnabled) {
        if (m_networkStatus != "Disabled") {
            m_networkStatus = "Disabled";
            emit networkStatusChanged();
        }
        qInfo() << "[MQTT] disabled by MQTT_ENABLED=0";
        return;
    }

    if (m_mqttClient) {
        return;
    }

    m_mqttClient = new QMqttClient(this);
    const QString host = m_env.value("MQTT_HOST", "localhost").trimmed();
    const int port = m_env.value("MQTT_PORT", "1883").toInt();
    const QString user = m_env.value("MQTT_USERNAME").trimmed();
    const QString pass = m_env.value("MQTT_PASSWORD").trimmed();
    const QString statusTopic = m_env.value("MQTT_STATUS_TOPIC", "system/status").trimmed();
    const QString detectTopic = m_env.value("MQTT_DETECTED_TOPIC", "system/detected").trimmed();
    const QString thermalTopic = m_env.value("MQTT_THERMAL_TOPIC", "lepton/frame/chunk").trimmed();
    const bool useTls = (m_env.value("MQTT_USE_TLS", "1").trimmed() == "1");
    const QString caPathRaw = m_env.value("MQTT_CA_CERT", "certs/rootCA.crt").trimmed();
    const QString certPathRaw = m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt").trimmed();
    const QString keyPathRaw = m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key").trimmed();
    const bool debugThermal = (m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    const bool debugRx = (m_env.value("MQTT_DEBUG_RX", debugThermal ? "1" : "0").trimmed() == "1");
    const QString transportTag = useTls ? "TLS" : "TCP";

    m_mqttClient->setHostname(host.isEmpty() ? QString("localhost") : host);
    m_mqttClient->setPort(port > 0 ? port : (useTls ? 8883 : 1883));
    if (!user.isEmpty()) m_mqttClient->setUsername(user);
    if (!pass.isEmpty()) m_mqttClient->setPassword(pass);
    qInfo() << "[MQTT] setup:"
            << "host=" << (host.isEmpty() ? QString("localhost") : host)
            << "port=" << (port > 0 ? port : (useTls ? 8883 : 1883))
            << "tls=" << useTls
            << "debugRx=" << debugRx;
    qInfo() << "[MQTT] topics:"
            << "status=" << statusTopic
            << "detected=" << detectTopic
            << "thermal=" << thermalTopic;

    auto resolvePath = [](const QString &rawPath) {
        QFileInfo info(rawPath);
        if (info.isAbsolute()) return rawPath;

        const QString appSide = QCoreApplication::applicationDirPath() + "/" + rawPath;
        if (QFileInfo::exists(appSide)) {
            return appSide;
        }
        return rawPath;
    };

    auto clientStateName = [](QMqttClient::ClientState state) -> const char * {
        switch (state) {
        case QMqttClient::Disconnected:
            return "Disconnected";
        case QMqttClient::Connecting:
            return "Connecting";
        case QMqttClient::Connected:
            return "Connected";
        default:
            return "Unknown";
        }
    };
    auto subStateName = [](QMqttSubscription::SubscriptionState state) -> const char * {
        switch (state) {
        case QMqttSubscription::Unsubscribed:
            return "Unsubscribed";
        case QMqttSubscription::SubscriptionPending:
            return "SubscriptionPending";
        case QMqttSubscription::Subscribed:
            return "Subscribed";
        case QMqttSubscription::UnsubscriptionPending:
            return "UnsubscriptionPending";
        case QMqttSubscription::Error:
            return "Error";
        default:
            return "Unknown";
        }
    };
    auto previewUtf8 = [](const QByteArray &data, int maxChars) -> QString {
        QString text = QString::fromUtf8(data);
        text.replace('\n', "\\n");
        text.replace('\r', "\\r");
        if (text.size() > maxChars) {
            text = text.left(maxChars) + "...";
        }
        return text;
    };
    auto readBe16 = [](const QByteArray &data, int offset) -> quint16 {
        const uchar *ptr = reinterpret_cast<const uchar *>(data.constData()) + offset;
        return static_cast<quint16>((static_cast<quint16>(ptr[0]) << 8) | static_cast<quint16>(ptr[1]));
    };
    auto logRxSample = [=](const QString &topicName, const QByteArray &message) {
        if (!debugRx) {
            return;
        }

        static QHash<QString, int> counters;
        const QString counterKey = transportTag + "|" + topicName;
        const int count = ++counters[counterKey];
        if (!(count <= 5 || (count % 100) == 0)) {
            return;
        }

        if (topicName == thermalTopic) {
            if (message.size() >= 10) {
                const quint16 frameId = readBe16(message, 0);
                const quint16 idx = readBe16(message, 2);
                const quint16 total = readBe16(message, 4);
                const quint16 minVal = readBe16(message, 6);
                const quint16 maxVal = readBe16(message, 8);
                qInfo() << "[MQTT][RX][" << transportTag << "]"
                        << "topic=" << topicName
                        << "count=" << count
                        << "bytes=" << message.size()
                        << "frame=" << frameId
                        << "chunk=" << idx << "/" << total
                        << "minMax=" << minVal << maxVal;
            } else if (message.size() >= 8) {
                const quint16 idx = readBe16(message, 0);
                const quint16 total = readBe16(message, 2);
                const quint16 minVal = readBe16(message, 4);
                const quint16 maxVal = readBe16(message, 6);
                qInfo() << "[MQTT][RX][" << transportTag << "]"
                        << "topic=" << topicName
                        << "count=" << count
                        << "bytes=" << message.size()
                        << "chunk=" << idx << "/" << total
                        << "minMax=" << minVal << maxVal;
            } else if (message.size() >= 4) {
                const quint16 idx = readBe16(message, 0);
                const quint16 total = readBe16(message, 2);
                qInfo() << "[MQTT][RX][" << transportTag << "]"
                        << "topic=" << topicName
                        << "count=" << count
                        << "bytes=" << message.size()
                        << "chunk=" << idx << "/" << total;
            } else {
                qInfo() << "[MQTT][RX][" << transportTag << "]"
                        << "topic=" << topicName
                        << "count=" << count
                        << "bytes=" << message.size()
                        << "(short thermal payload)";
            }
            return;
        }

        qInfo() << "[MQTT][RX][" << transportTag << "]"
                << "topic=" << topicName
                << "count=" << count
                << "bytes=" << message.size()
                << "preview=" << previewUtf8(message, 96);
    };
    auto subscribeTopic = [=](const QString &topicName) {
        QMqttSubscription *subscription = m_mqttClient->subscribe(QMqttTopicFilter(topicName), 0);
        if (!subscription) {
            qWarning() << "[MQTT][" << transportTag << "] subscribe failed:" << topicName;
            return;
        }
        if (debugRx) {
            qInfo() << "[MQTT][" << transportTag << "] subscribe requested:" << topicName;
        }
        connect(subscription, &QMqttSubscription::stateChanged, this,
                [=](QMqttSubscription::SubscriptionState state) {
                    if (!debugRx && state != QMqttSubscription::Error) {
                        return;
                    }
                    qInfo() << "[MQTT][" << transportTag << "]"
                            << "subscription state:"
                            << topicName
                            << "->" << subStateName(state)
                            << "(" << static_cast<int>(state) << ")";
                });
    };
    auto handleIncomingMessage = [=](const QByteArray &message, const QMqttTopicName &topic) {
        const QString topicName = topic.name();
        const QString payload = QString::fromUtf8(message).trimmed();
        logRxSample(topicName, message);

        if (topicName == statusTopic) {
            if (!payload.isEmpty() && m_networkStatus != payload) {
                m_networkStatus = payload;
                emit networkStatusChanged();
            } else if (debugRx && payload.isEmpty()) {
                qWarning() << "[MQTT][RX][" << transportTag << "] empty status payload";
            }
            return;
        }

        if (topicName == detectTopic) {
            // detected 토픽 payload를 정수로 변환해 UI 카운트 갱신
            bool ok = false;
            const int detected = payload.toInt(&ok);
            if (ok && m_detectedObjects != detected) {
                m_detectedObjects = detected;
                emit detectedObjectsChanged();
            } else if (debugRx && !ok) {
                qWarning() << "[MQTT][RX][" << transportTag << "] invalid detected payload:" << payload;
            }
            return;
        }

        if (topicName == thermalTopic) {
            if (debugThermal && !debugRx) {
                static int thermalMsgCount = 0;
                thermalMsgCount++;
                if (thermalMsgCount <= 5 || (thermalMsgCount % 50) == 0) {
                    qInfo() << "[MQTT][THERMAL][" << transportTag << "] msg#" << thermalMsgCount
                            << "bytes=" << message.size()
                            << "topic=" << topicName;
                }
            }
            handleThermalChunkMessage(message);
            return;
        }

        if (debugRx) {
            qWarning() << "[MQTT][RX][" << transportTag << "] unhandled topic:"
                       << topicName << "bytes=" << message.size();
        }
    };

    connect(m_mqttClient, &QMqttClient::stateChanged, this, [=](QMqttClient::ClientState state) {
        if (!debugRx) {
            return;
        }
        qInfo() << "[MQTT][" << transportTag << "] stateChanged:"
                << clientStateName(state) << "(" << static_cast<int>(state) << ")";
    });
    connect(m_mqttClient, &QMqttClient::connected, this, [=]() {
        qInfo() << "[MQTT][" << transportTag << "] connected";
        if (m_networkStatus != "Connected") {
            m_networkStatus = "Connected";
            emit networkStatusChanged();
        }
        subscribeTopic(statusTopic);
        subscribeTopic(detectTopic);
        subscribeTopic(thermalTopic);
        qInfo() << "[MQTT] subscribed:" << statusTopic << "," << detectTopic << "," << thermalTopic;
    });
    connect(m_mqttClient, &QMqttClient::disconnected, this, [=]() {
        qWarning() << "[MQTT][" << transportTag << "] disconnected";
        if (m_networkStatus != "Disconnected") {
            m_networkStatus = "Disconnected";
            emit networkStatusChanged();
        }
    });
    connect(m_mqttClient, &QMqttClient::messageReceived, this,
            [=](const QByteArray &message, const QMqttTopicName &topic) {
                handleIncomingMessage(message, topic);
            });
    connect(m_mqttClient, &QMqttClient::errorChanged, this, [=](QMqttClient::ClientError error) {
        qWarning() << "[MQTT][" << transportTag << "] errorChanged:" << static_cast<int>(error);
        if (m_networkStatus != "Error") {
            m_networkStatus = "Error";
            emit networkStatusChanged();
        }
    });

    if (useTls) {
        // TLS 모드에서는 CA/클라이언트 인증서/키를 모두 검증 후 연결
        const QString caPath = resolvePath(caPathRaw);
        const QString certPath = resolvePath(certPathRaw);
        const QString keyPath = resolvePath(keyPathRaw);

        QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
        bool tlsReady = true;

        QFile caFile(caPath);
        if (caFile.open(QIODevice::ReadOnly)) {
            const QList<QSslCertificate> certs = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
            if (!certs.isEmpty()) {
                sslConfig.setCaCertificates(certs);
                qInfo() << "[MQTT][TLS] CA loaded:" << caPath;
            } else {
                tlsReady = false;
                qWarning() << "MQTT CA certificate is invalid:" << caPath;
            }
        } else {
            tlsReady = false;
            qWarning() << "MQTT CA certificate not found:" << caPath;
        }

        QFile certFile(certPath);
        if (certFile.open(QIODevice::ReadOnly)) {
            const QList<QSslCertificate> certs = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
            if (!certs.isEmpty()) {
                sslConfig.setLocalCertificate(certs.first());
                qInfo() << "[MQTT][TLS] client cert loaded:" << certPath;
            } else {
                tlsReady = false;
                qWarning() << "MQTT client certificate is invalid:" << certPath;
            }
        } else {
            tlsReady = false;
            qWarning() << "MQTT client certificate not found:" << certPath;
        }

        QFile keyFile(keyPath);
        if (keyFile.open(QIODevice::ReadOnly)) {
            QSslKey clientKey(&keyFile, QSsl::Rsa, QSsl::Pem);
            if (!clientKey.isNull()) {
                sslConfig.setPrivateKey(clientKey);
                qInfo() << "[MQTT][TLS] client key loaded:" << keyPath;
            } else {
                tlsReady = false;
                qWarning() << "MQTT client key is invalid:" << keyPath;
            }
        } else {
            tlsReady = false;
            qWarning() << "MQTT client key not found:" << keyPath;
        }

        if (!tlsReady) {
            qWarning() << "[MQTT][TLS] config not ready. connection skipped.";
            if (m_networkStatus != "TLS Config Error") {
                m_networkStatus = "TLS Config Error";
                emit networkStatusChanged();
            }
            return;
        }

        qInfo() << "[MQTT][TLS] connectToHostEncrypted()";
        m_mqttClient->connectToHostEncrypted(sslConfig);
        return;
    }

    // 비TLS(TCP) 모드 연결 처리
    qInfo() << "[MQTT][TCP] connectToHost()";
    m_mqttClient->connectToHost();
}

