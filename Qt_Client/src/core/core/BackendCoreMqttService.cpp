#include "internal/core/BackendCoreMqttService.h"

#include "Backend.h"
#include "internal/core/BackendCoreCertConfigService.h"
#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMqttClient>
#include <QMqttSubscription>
#include <QMqttTopicFilter>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>

// 설정 MQTT 처리 함수
void BackendCoreMqttService::setupMqtt(Backend *backend, BackendPrivate *state)
{
    const bool mqttEnabled = (state->m_env.value("MQTT_ENABLED", "1").trimmed() == "1");
    if (!mqttEnabled) {
        if (state->m_networkStatus != "Disabled") {
            state->m_networkStatus = "Disabled";
            emit backend->networkStatusChanged();
        }
        qInfo() << "[MQTT] disabled by MQTT_ENABLED=0";
        return;
    }

    if (state->m_mqttClient) {
        return;
    }

    state->m_mqttClient = new QMqttClient(backend);
    const QString host = state->m_env.value("MQTT_HOST", "localhost").trimmed();
    const int port = state->m_env.value("MQTT_PORT", "1883").toInt();
    const QString user = state->m_env.value("MQTT_USERNAME").trimmed();
    const QString pass = state->m_env.value("MQTT_PASSWORD").trimmed();
    const QString statusTopic = state->m_env.value("MQTT_STATUS_TOPIC", "system/status").trimmed();
    const QString detectTopic = state->m_env.value("MQTT_DETECTED_TOPIC", "system/detected").trimmed();
    const QString eventTopic = state->m_env.value("MQTT_EVENT_ALERT_TOPIC", "system/event").trimmed();
    const QString thermalTopic = state->m_env.value("MQTT_THERMAL_TOPIC", "lepton/frame/chunk").trimmed();
    const QString thermalTransport = state->m_env.value("THERMAL_TRANSPORT", "ws").trimmed().toLower();
    const bool thermalViaWs = (thermalTransport != "mqtt");
    const bool useTls = (state->m_env.value("MQTT_USE_TLS", "1").trimmed() == "1");
    const QString caPathRaw = state->m_env.value("MQTT_CA_CERT", "certs/rootCA.crt").trimmed();
    const QString certPathRaw = state->m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt").trimmed();
    const QString keyPathRaw = state->m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key").trimmed();
    const bool debugThermal = (state->m_env.value("THERMAL_DEBUG", "0").trimmed() == "1");
    const bool debugRx = (state->m_env.value("MQTT_DEBUG_RX", debugThermal ? "1" : "0").trimmed() == "1");
    const QString transportTag = useTls ? "TLS" : "TCP";

    state->m_mqttClient->setHostname(host.isEmpty() ? QString("localhost") : host);
    state->m_mqttClient->setPort(port > 0 ? port : (useTls ? 8883 : 1883));
    if (!user.isEmpty()) {
        state->m_mqttClient->setUsername(user);
    }
    if (!pass.isEmpty()) {
        state->m_mqttClient->setPassword(pass);
    }

    qInfo() << "[MQTT] setup:"
            << "host=" << (host.isEmpty() ? QString("localhost") : host)
            << "port=" << (port > 0 ? port : (useTls ? 8883 : 1883))
            << "tls=" << useTls
            << "debugRx=" << debugRx
            << "thermalTransport=" << thermalTransport;
    qInfo() << "[MQTT] topics:"
            << "status=" << statusTopic
            << "detected=" << detectTopic
            << "event=" << eventTopic
            << "thermal=" << thermalTopic;

    auto clientStateName = [](QMqttClient::ClientState value) -> const char * {
        switch (value) {
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

    auto subStateName = [](QMqttSubscription::SubscriptionState value) -> const char * {
        switch (value) {
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
        QMqttSubscription *subscription = state->m_mqttClient->subscribe(QMqttTopicFilter(topicName), 0);
        if (!subscription) {
            qWarning() << "[MQTT][" << transportTag << "] subscribe failed:" << topicName;
            return;
        }
        if (debugRx) {
            qInfo() << "[MQTT][" << transportTag << "] subscribe requested:" << topicName;
        }
        QObject::connect(subscription, &QMqttSubscription::stateChanged, backend,
                [=](QMqttSubscription::SubscriptionState value) {
                    if (!debugRx && value != QMqttSubscription::Error) {
                        return;
                    }
                    qInfo() << "[MQTT][" << transportTag << "]"
                            << "subscription state:"
                            << topicName
                            << "->" << subStateName(value)
                            << "(" << static_cast<int>(value) << ")";
                });
    };

    auto handleIncomingMessage = [=](const QByteArray &message, const QMqttTopicName &topic) {
        const QString topicName = topic.name();
        const QString payload = QString::fromUtf8(message).trimmed();
        logRxSample(topicName, message);

        if (topicName == statusTopic) {
            if (!payload.isEmpty() && state->m_networkStatus != payload) {
                state->m_networkStatus = payload;
                emit backend->networkStatusChanged();
            } else if (debugRx && payload.isEmpty()) {
                qWarning() << "[MQTT][RX][" << transportTag << "] empty status payload";
            }
            return;
        }

        if (topicName == detectTopic) {
            bool ok = false;
            const int detected = payload.toInt(&ok);
            if (ok && state->m_detectedObjects != detected) {
                state->m_detectedObjects = detected;
                emit backend->detectedObjectsChanged();
            } else if (debugRx && !ok) {
                qWarning() << "[MQTT][RX][" << transportTag << "] invalid detected payload:" << payload;
            }
            return;
        }

        if (topicName == eventTopic) {
            backend->handleEventAlertMessage(message);
            return;
        }

        if (topicName == thermalTopic) {
            if (thermalViaWs) {
                if (debugRx) {
                    qInfo() << "[MQTT][RX][" << transportTag << "]"
                            << "ignore thermal topic because THERMAL_TRANSPORT="
                            << thermalTransport;
                }
                return;
            }
            if (debugThermal && !debugRx) {
                static int thermalMsgCount = 0;
                thermalMsgCount++;
                if (thermalMsgCount <= 5 || (thermalMsgCount % 50) == 0) {
                    qInfo() << "[MQTT][THERMAL][" << transportTag << "] msg#" << thermalMsgCount
                            << "bytes=" << message.size()
                            << "topic=" << topicName;
                }
            }
            backend->handleThermalChunkMessage(message);
            return;
        }

        if (debugRx) {
            qWarning() << "[MQTT][RX][" << transportTag << "] unhandled topic:"
                       << topicName << "bytes=" << message.size();
        }
    };

    QObject::connect(state->m_mqttClient, &QMqttClient::stateChanged, backend, [=](QMqttClient::ClientState value) {
        if (!debugRx) {
            return;
        }
        qInfo() << "[MQTT][" << transportTag << "] stateChanged:"
                << clientStateName(value) << "(" << static_cast<int>(value) << ")";
    });

    QObject::connect(state->m_mqttClient, &QMqttClient::connected, backend, [=]() {
        qInfo() << "[MQTT][" << transportTag << "] connected";
        if (state->m_networkStatus != "Connected") {
            state->m_networkStatus = "Connected";
            emit backend->networkStatusChanged();
        }
        subscribeTopic(statusTopic);
        subscribeTopic(detectTopic);
        subscribeTopic(eventTopic);
        if (!thermalViaWs) {
            subscribeTopic(thermalTopic);
            qInfo() << "[MQTT] subscribed:" << statusTopic << "," << detectTopic << "," << eventTopic << "," << thermalTopic;
        } else {
            qInfo() << "[MQTT] subscribed:" << statusTopic << "," << detectTopic << "," << eventTopic
                    << "(thermal via websocket)";
        }
    });

    QObject::connect(state->m_mqttClient, &QMqttClient::disconnected, backend, [=]() {
        qWarning() << "[MQTT][" << transportTag << "] disconnected";
        if (state->m_networkStatus != "Disconnected") {
            state->m_networkStatus = "Disconnected";
            emit backend->networkStatusChanged();
        }
    });

    QObject::connect(state->m_mqttClient, &QMqttClient::messageReceived, backend,
            [=](const QByteArray &message, const QMqttTopicName &topic) {
                handleIncomingMessage(message, topic);
            });

    QObject::connect(state->m_mqttClient, &QMqttClient::errorChanged, backend, [=](QMqttClient::ClientError error) {
        qWarning() << "[MQTT][" << transportTag << "] errorChanged:" << static_cast<int>(error);
        if (state->m_networkStatus != "Error") {
            state->m_networkStatus = "Error";
            emit backend->networkStatusChanged();
        }
    });

    if (useTls) {
        const QString caPath = BackendCoreCertConfigService::resolveCertificatePath(state, caPathRaw);
        const QString certPath = BackendCoreCertConfigService::resolveCertificatePath(state, certPathRaw);
        const QString keyPath = BackendCoreCertConfigService::resolveCertificatePath(state, keyPathRaw);

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
            if (state->m_networkStatus != "TLS Config Error") {
                state->m_networkStatus = "TLS Config Error";
                emit backend->networkStatusChanged();
            }
            return;
        }

        qInfo() << "[MQTT][TLS] connectToHostEncrypted()";
        state->m_mqttClient->connectToHostEncrypted(sslConfig);
        return;
    }

    qInfo() << "[MQTT][TCP] connectToHost()";
    state->m_mqttClient->connectToHost();
}

// reload MQTT 처리 함수
void BackendCoreMqttService::reloadMqtt(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return;
    }

    if (state->m_mqttClient) {
        // 바뀐 인증서 경로로 MQTT를 다시 연결한다.
        QMqttClient *client = state->m_mqttClient;
        state->m_mqttClient = nullptr;
        client->disconnectFromHost();
        client->deleteLater();
    }

    setupMqtt(backend, state);
}

