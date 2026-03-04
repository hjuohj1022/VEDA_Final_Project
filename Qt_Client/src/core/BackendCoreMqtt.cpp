#include "Backend.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
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
    const bool useTls = (m_env.value("MQTT_USE_TLS", "1").trimmed() == "1");
    const QString caPathRaw = m_env.value("MQTT_CA_CERT", "certs/rootCA.crt").trimmed();
    const QString certPathRaw = m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt").trimmed();
    const QString keyPathRaw = m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key").trimmed();

    m_mqttClient->setHostname(host.isEmpty() ? QString("localhost") : host);
    m_mqttClient->setPort(port > 0 ? port : (useTls ? 8883 : 1883));
    if (!user.isEmpty()) m_mqttClient->setUsername(user);
    if (!pass.isEmpty()) m_mqttClient->setPassword(pass);
    qInfo() << "[MQTT] setup:"
            << "host=" << (host.isEmpty() ? QString("localhost") : host)
            << "port=" << (port > 0 ? port : (useTls ? 8883 : 1883))
            << "tls=" << useTls;

    auto resolvePath = [](const QString &rawPath) {
        QFileInfo info(rawPath);
        if (info.isAbsolute()) return rawPath;

        const QString appSide = QCoreApplication::applicationDirPath() + "/" + rawPath;
        if (QFileInfo::exists(appSide)) {
            return appSide;
        }
        return rawPath;
    };

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

        connect(m_mqttClient, &QMqttClient::connected, this, [=]() {
            qInfo() << "[MQTT][TLS] connected";
            if (m_networkStatus != "Connected") {
                m_networkStatus = "Connected";
                emit networkStatusChanged();
            }
            m_mqttClient->subscribe(QMqttTopicFilter(statusTopic), 0);
            m_mqttClient->subscribe(QMqttTopicFilter(detectTopic), 0);
            qInfo() << "[MQTT] subscribed:" << statusTopic << "," << detectTopic;
        });

        connect(m_mqttClient, &QMqttClient::disconnected, this, [=]() {
            qWarning() << "[MQTT][TLS] disconnected";
            if (m_networkStatus != "Disconnected") {
                m_networkStatus = "Disconnected";
                emit networkStatusChanged();
            }
        });

        connect(m_mqttClient, &QMqttClient::messageReceived, this, [=](const QByteArray &message, const QMqttTopicName &topic) {
            const QString topicName = topic.name();
            const QString payload = QString::fromUtf8(message).trimmed();

            if (topicName == statusTopic) {
                if (!payload.isEmpty() && m_networkStatus != payload) {
                    m_networkStatus = payload;
                    emit networkStatusChanged();
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
                }
            }
        });

        connect(m_mqttClient, &QMqttClient::errorChanged, this, [=](QMqttClient::ClientError error) {
            qWarning() << "[MQTT][TLS] errorChanged:" << static_cast<int>(error);
            if (m_networkStatus != "Error") {
                m_networkStatus = "Error";
                emit networkStatusChanged();
            }
        });

        qInfo() << "[MQTT][TLS] connectToHostEncrypted()";
        m_mqttClient->connectToHostEncrypted(sslConfig);
        return;
    }

    // 비TLS(TCP) 모드 연결 처리
    connect(m_mqttClient, &QMqttClient::connected, this, [=]() {
        qInfo() << "[MQTT][TCP] connected";
        if (m_networkStatus != "Connected") {
            m_networkStatus = "Connected";
            emit networkStatusChanged();
        }
        m_mqttClient->subscribe(QMqttTopicFilter(statusTopic), 0);
        m_mqttClient->subscribe(QMqttTopicFilter(detectTopic), 0);
        qInfo() << "[MQTT] subscribed:" << statusTopic << "," << detectTopic;
    });

    connect(m_mqttClient, &QMqttClient::disconnected, this, [=]() {
        qWarning() << "[MQTT][TCP] disconnected";
        if (m_networkStatus != "Disconnected") {
            m_networkStatus = "Disconnected";
            emit networkStatusChanged();
        }
    });

    connect(m_mqttClient, &QMqttClient::messageReceived, this, [=](const QByteArray &message, const QMqttTopicName &topic) {
        const QString topicName = topic.name();
        const QString payload = QString::fromUtf8(message).trimmed();

        if (topicName == statusTopic) {
            if (!payload.isEmpty() && m_networkStatus != payload) {
                m_networkStatus = payload;
                emit networkStatusChanged();
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
            }
        }
    });

    connect(m_mqttClient, &QMqttClient::errorChanged, this, [=](QMqttClient::ClientError error) {
        qWarning() << "[MQTT][TCP] errorChanged:" << static_cast<int>(error);
        if (m_networkStatus != "Error") {
            m_networkStatus = "Error";
            emit networkStatusChanged();
        }
    });

    qInfo() << "[MQTT][TCP] connectToHost()";
    m_mqttClient->connectToHost();
}

