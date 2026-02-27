#include "Backend.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMqttTopicFilter>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTextStream>

// 실행 경로 기준으로 .env를 찾아 로드한다.
void Backend::loadEnv() {
    m_env.clear();

    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << (appDir + "/.env")
               << (appDir + "/../.env")
               << (appDir + "/../../.env")
               << (appDir + "/../../../.env")
               << (QDir::currentPath() + "/.env")
               << ".env";

    QString loadedPath;
    for (const QString &path : candidates) {
        QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) {
            continue;
        }

        QFile file(fi.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.isEmpty() || line.startsWith("#")) continue;
            const int eq = line.indexOf('=');
            if (eq <= 0) continue;
            const QString key = line.left(eq).trimmed();
            const QString val = line.mid(eq + 1).trimmed();
            if (!key.isEmpty()) m_env.insert(key, val);
        }
        file.close();
        loadedPath = fi.absoluteFilePath();
        break;
    }

    if (loadedPath.isEmpty()) {
        qWarning() << "[ENV] .env file not found. using defaults.";
    } else {
        qInfo() << "[ENV] loaded from:" << loadedPath
                << "API_URL=" << m_env.value("API_URL", "http://localhost:8080");
    }
}

// HTTPS 요청용 SSL 설정을 초기화한다.
void Backend::setupSslConfiguration() {
    auto resolvePath = [](const QString &rawPath) {
        QFileInfo info(rawPath);
        if (info.isAbsolute()) return rawPath;

        const QString appSide = QCoreApplication::applicationDirPath() + "/" + rawPath;
        if (QFileInfo::exists(appSide)) {
            return appSide;
        }
        return rawPath;
    };

    const QString caPathRaw = m_env.value("SSL_CA_CERT",
                            m_env.value("MQTT_CA_CERT", "certs/rootCA.crt")).trimmed();
    const QString certPathRaw = m_env.value("SSL_CLIENT_CERT",
                              m_env.value("MQTT_CLIENT_CERT", "certs/client-qt.crt")).trimmed();
    const QString keyPathRaw = m_env.value("SSL_CLIENT_KEY",
                             m_env.value("MQTT_CLIENT_KEY", "certs/client-qt.key")).trimmed();

    const QString verifyPeerRaw = m_env.value("SSL_VERIFY_PEER", "1").trimmed().toLower();
    const QString ignoreErrorsRaw = m_env.value("SSL_IGNORE_ERRORS", "0").trimmed().toLower();
    const bool verifyPeer = !(verifyPeerRaw == "0" || verifyPeerRaw == "false" || verifyPeerRaw == "off");
    m_sslIgnoreErrors = (ignoreErrorsRaw == "1" || ignoreErrorsRaw == "true" || ignoreErrorsRaw == "on");

    m_sslConfig = QSslConfiguration::defaultConfiguration();
    m_sslConfigReady = false;

    bool hasAnyConfig = false;

    const QString caPath = resolvePath(caPathRaw);
    QFile caFile(caPath);
    if (caFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            m_sslConfig.setCaCertificates(certs);
            hasAnyConfig = true;
            qInfo() << "[SSL] CA loaded:" << caPath;
        } else {
            qWarning() << "[SSL] invalid CA cert:" << caPath;
        }
    } else {
        qWarning() << "[SSL] CA cert not found:" << caPath;
    }

    const QString certPath = resolvePath(certPathRaw);
    QFile certFile(certPath);
    if (certFile.open(QIODevice::ReadOnly)) {
        const QList<QSslCertificate> certs = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
        if (!certs.isEmpty()) {
            m_sslConfig.setLocalCertificate(certs.first());
            hasAnyConfig = true;
            qInfo() << "[SSL] client cert loaded:" << certPath;
        } else {
            qWarning() << "[SSL] invalid client cert:" << certPath;
        }
    }

    const QString keyPath = resolvePath(keyPathRaw);
    QFile keyFile(keyPath);
    if (keyFile.open(QIODevice::ReadOnly)) {
        QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
        if (!key.isNull()) {
            m_sslConfig.setPrivateKey(key);
            hasAnyConfig = true;
            qInfo() << "[SSL] client key loaded:" << keyPath;
        } else {
            qWarning() << "[SSL] invalid client key:" << keyPath;
        }
    }

    m_sslConfig.setPeerVerifyMode(verifyPeer ? QSslSocket::VerifyPeer : QSslSocket::VerifyNone);
    m_sslConfigReady = hasAnyConfig;
    qInfo() << "[SSL] ready=" << m_sslConfigReady
            << "verifyPeer=" << verifyPeer
            << "ignoreErrors=" << m_sslIgnoreErrors;
}

// HTTPS 요청일 때만 SSL 설정을 적용한다.
void Backend::applySslIfNeeded(QNetworkRequest &request) const {
    const QUrl url = request.url();
    if (url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        return;
    }
    if (m_sslConfigReady) {
        request.setSslConfiguration(m_sslConfig);
    }
}

// 필요 시 SSL 에러 무시 핸들러를 연결한다.
void Backend::attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const {
    if (!reply) return;
    connect(reply, &QNetworkReply::sslErrors, reply, [reply, tag, this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            qWarning() << "[" << tag << "][SSL]" << err.errorString();
        }
        if (m_sslIgnoreErrors) {
            reply->ignoreSslErrors();
        }
    });
}

// MQTT 연결/구독을 초기화한다.
void Backend::setupMqtt() {
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

// 활성 카메라 수를 갱신한다.
void Backend::setActiveCameras(int count) {
    if (m_activeCameras != count) {
        m_activeCameras = count;
        emit activeCamerasChanged();
    }
}

// 평균 FPS 값을 갱신한다.
void Backend::setCurrentFps(int fps) {
    if (m_currentFps != fps) {
        m_currentFps = fps;
        emit currentFpsChanged();
    }
}

// 지연 시간(ms)을 갱신한다.
void Backend::setLatency(int ms) {
    if (m_latency != ms) {
        m_latency = ms;
        emit latencyChanged();
    }
}
