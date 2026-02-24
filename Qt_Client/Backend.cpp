#include "Backend.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QNetworkCookieJar>
#include <QSettings>
#include <QMqttTopicFilter>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <memory>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslError>

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this));
    setActiveCameras(0);
    loadEnv();
    setupMqtt();

    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8554").trimmed();

    // Always prefer .env values on app startup.
    m_useCustomRtspConfig = false;
    m_rtspIp = envIp;
    m_rtspPort = envPort;

    if (m_rtspIp.isEmpty()) {
        m_rtspIp = envIp;
    }
    if (m_rtspPort.isEmpty()) {
        m_rtspPort = envPort;
    }

    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setInterval(1000);
    connect(m_sessionTimer, &QTimer::timeout, this, &Backend::onSessionTick);
    
    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &Backend::checkStorage);
    m_storageTimer->start(5000);
    checkStorage();
    QTimer *simTimer = new QTimer(this);
    simTimer->setInterval(5000);
    connect(simTimer, &QTimer::timeout, this, [this](){
        static bool probeInFlight = false;
        if (probeInFlight) {
            return;
        }
        probeInFlight = true;

        QTcpSocket *socket = new QTcpSocket(this);
        QElapsedTimer *timer = new QElapsedTimer();
        timer->start();

        QString ip = rtspIp();
        int port = rtspPort().toInt();
        if (port == 0) port = 8554;

        auto finished = std::make_shared<bool>(false);
        auto finishProbe = [socket, timer, finished]() {
            if (*finished) {
                return;
            }
            *finished = true;
            socket->deleteLater();
            delete timer;
            probeInFlight = false;
        };

        connect(socket, &QTcpSocket::connected, this, [this, socket, timer, finishProbe]() {
            const int elapsed = timer->elapsed();
            setLatency(elapsed);
            socket->disconnectFromHost();
            finishProbe();
        });

        connect(socket, &QTcpSocket::errorOccurred, this, [this, finishProbe](QAbstractSocket::SocketError socketError) {
            Q_UNUSED(socketError);
            setLatency(999);
            finishProbe();
        });

        QPointer<QTcpSocket> socketGuard(socket);
        QTimer::singleShot(1200, this, [socketGuard, finishProbe]() {
            if (socketGuard && socketGuard->state() == QAbstractSocket::ConnectingState) {
                socketGuard->abort();
                finishProbe();
            }
        });

        socket->connectToHost(ip, port);
    });
    simTimer->start();
}

Backend::~Backend() {}

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

bool Backend::isLoggedIn() const { return m_isLoggedIn; }
QString Backend::serverUrl() const { return m_env.value("API_URL", "http://localhost:8080"); }
QString Backend::rtspIp() const { return m_rtspIp; }
QString Backend::rtspPort() const { return m_rtspPort; }

void Backend::setRtspIp(const QString &ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspIp == trimmed) return;

    m_rtspIp = trimmed;
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_ip", m_rtspIp);
    m_useCustomRtspConfig = true;
    emit rtspIpChanged();
}

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

void Backend::setRtspPort(const QString &port) {
    QString trimmed = port.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspPort == trimmed) return;

    m_rtspPort = trimmed;
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_port", m_rtspPort);
    m_useCustomRtspConfig = true;
    emit rtspPortChanged();
}

QString Backend::buildRtspUrl(int cameraIndex, bool useSubStream) const {
    if (cameraIndex < 0) {
        return QString();
    }

    const QString defaultMainTemplate = "/{index}/onvif/profile{profile}/media.smp";
    const QString defaultSubTemplate = "/{index}/onvif/profile{profile}/media.smp";
    const QString mainTemplate = m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed().isEmpty()
            ? defaultMainTemplate
            : m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed();
    QString pathTemplate = mainTemplate;

    if (useSubStream) {
        const QString subTemplate = m_env.value("RTSP_SUB_PATH_TEMPLATE").trimmed();
        if (!subTemplate.isEmpty()) {
            pathTemplate = subTemplate;
        } else {
            pathTemplate = defaultSubTemplate;
        }
    }

    const QString mainProfile = m_env.value("RTSP_MAIN_PROFILE", "1").trimmed();
    const QString subProfile = m_env.value("RTSP_SUB_PROFILE", "2").trimmed();
    const QString selectedProfile = useSubStream
            ? (subProfile.isEmpty() ? QString("2") : subProfile)
            : (mainProfile.isEmpty() ? QString("1") : mainProfile);

    QString path = pathTemplate;
    path.replace("{index}", QString::number(cameraIndex));
    path.replace("{profile}", selectedProfile);
    if (!path.startsWith('/')) {
        path.prepend('/');
    }

    const QString user = m_env.value("RTSP_USERNAME").trimmed();
    const QString pass = m_env.value("RTSP_PASSWORD").trimmed();
    QString authPrefix;
    if (!user.isEmpty()) {
        authPrefix = user;
        if (!pass.isEmpty()) {
            authPrefix += ":" + pass;
        }
        authPrefix += "@";
    }

    return QString("rtsp://%1%2:%3%4").arg(authPrefix, m_rtspIp, m_rtspPort, path);
}

void Backend::setActiveCameras(int count) {
    if (m_activeCameras != count) {
        m_activeCameras = count;
        emit activeCamerasChanged();
    }
}

void Backend::setCurrentFps(int fps) {
    if (m_currentFps != fps) {
        m_currentFps = fps;
        emit currentFpsChanged();
    }
}

void Backend::setLatency(int ms) {
    if (m_latency != ms) {
        m_latency = ms;
        emit latencyChanged();
    }
}

void Backend::login(QString id, QString pw) {
    if (m_loginLocked) {
        emit loginFailed("로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
        return;
    }

    const QString loginUrl = serverUrl() + "/login";
    qInfo() << "[LOGIN] request URL:" << loginUrl;
    QUrl url(loginUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Apply custom CA trust for HTTPS login request.
    if (url.scheme().compare("https", Qt::CaseInsensitive) == 0) {
        const QString caPathRaw = m_env.value("MQTT_CA_CERT", "certs/rootCA.crt").trimmed();
        auto resolvePath = [](const QString &rawPath) {
            QFileInfo info(rawPath);
            if (info.isAbsolute()) return rawPath;

            const QString appSide = QCoreApplication::applicationDirPath() + "/" + rawPath;
            if (QFileInfo::exists(appSide)) {
                return appSide;
            }
            return rawPath;
        };

        const QString caPath = resolvePath(caPathRaw);
        QFile caFile(caPath);
        if (caFile.open(QIODevice::ReadOnly)) {
            const QList<QSslCertificate> certs = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
            if (!certs.isEmpty()) {
                QSslConfiguration sslConfig = request.sslConfiguration();
                sslConfig.setCaCertificates(certs);
                request.setSslConfiguration(sslConfig);
                qInfo() << "[LOGIN][SSL] custom CA loaded:" << caPath;
            } else {
                qWarning() << "[LOGIN][SSL] invalid CA cert data:" << caPath;
            }
        } else {
            qWarning() << "[LOGIN][SSL] CA file not found:" << caPath;
        }
    }
    
    QJsonObject json;
    json["id"] = id;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = m_manager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::sslErrors, this, [=](const QList<QSslError> &errors) {
        for (const QSslError &e : errors) {
            qWarning() << "[LOGIN][SSL]" << e.errorString();
        }
    });

    QTimer *loginTimeout = new QTimer(reply);
    loginTimeout->setSingleShot(true);
    loginTimeout->setInterval(5000);
    connect(loginTimeout, &QTimer::timeout, this, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    loginTimeout->start();

    connect(reply, &QNetworkReply::finished, this, [=](){
        const bool timedOut = reply->property("timedOut").toBool();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        qWarning() << "[LOGIN] status=" << statusCode
                   << "netError=" << static_cast<int>(netError)
                   << "errorString=" << reply->errorString();

        if (reply->error() == QNetworkReply::NoError) {
            m_isLoggedIn = true;
            m_userId = id;
            m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
            m_sessionTimer->start();
            if (m_loginFailedAttempts != 0 || m_loginLocked) {
                m_loginFailedAttempts = 0;
                m_loginLocked = false;
                emit loginLockChanged();
            }
            emit isLoggedInChanged();
            emit userIdChanged();
            emit sessionRemainingSecondsChanged();
            emit loginSuccess();
        } else {
            if (timedOut) {
                emit loginFailed("서버 응답 시간이 초과되었습니다. 서버 상태를 확인해 주세요.");
                reply->deleteLater();
                return;
            }

            const bool isAuthFailure = (statusCode == 401 || statusCode == 403
                                     || netError == QNetworkReply::AuthenticationRequiredError
                                     || netError == QNetworkReply::ContentAccessDenied);

            const bool isServerUnavailable =
                    (netError == QNetworkReply::ConnectionRefusedError
                  || netError == QNetworkReply::RemoteHostClosedError
                  || netError == QNetworkReply::HostNotFoundError
                  || netError == QNetworkReply::TimeoutError
                  || netError == QNetworkReply::TemporaryNetworkFailureError
                  || netError == QNetworkReply::NetworkSessionFailedError
                  || netError == QNetworkReply::ServiceUnavailableError
                  || statusCode >= 500);

            const bool isSslFailure =
                    (netError == QNetworkReply::SslHandshakeFailedError
                  || netError == QNetworkReply::UnknownNetworkError);

            if (isSslFailure) {
                emit loginFailed("서버 SSL 인증서 검증에 실패했습니다. 인증서(CN/SAN) 또는 API_URL(HTTP/HTTPS)을 확인해 주세요.");
            } else if (isServerUnavailable) {
                emit loginFailed("서버에 연결할 수 없습니다. 서버 상태를 확인해 주세요.");
            } else if (isAuthFailure) {
                if (!m_loginLocked) {
                    m_loginFailedAttempts++;
                    if (m_loginFailedAttempts >= m_loginMaxAttempts) {
                        m_loginLocked = true;
                    }
                    emit loginLockChanged();
                }

                if (m_loginLocked) {
                    emit loginFailed("비밀번호를 여러 번 잘못 입력하여 로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
                } else {
                    int remaining = m_loginMaxAttempts - m_loginFailedAttempts;
                    emit loginFailed(QString("비밀번호가 잘못되었습니다. (%1회 남음)").arg(remaining));
                }
            } else {
                emit loginFailed("로그인 요청에 실패했습니다. 네트워크 또는 서버 상태를 확인해 주세요.");
            }
        }
        reply->deleteLater();
    });
}

void Backend::skipLoginTemporarily() {
    if (m_isLoggedIn) {
        return;
    }

    m_isLoggedIn = true;
    m_userId = "Skip";
    m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
    m_sessionTimer->start();

    if (m_loginFailedAttempts != 0 || m_loginLocked) {
        m_loginFailedAttempts = 0;
        m_loginLocked = false;
        emit loginLockChanged();
    }

    emit isLoggedInChanged();
    emit userIdChanged();
    emit sessionRemainingSecondsChanged();
    emit loginSuccess();
}

void Backend::logout() {
    if (!m_isLoggedIn) return;

    m_isLoggedIn = false;
    m_userId.clear();
    m_sessionTimer->stop();
    m_sessionRemainingSeconds = 0;

    emit isLoggedInChanged();
    emit userIdChanged();
    emit sessionRemainingSecondsChanged();
}

void Backend::resetSessionTimer() {
    if (!m_isLoggedIn) return;

    if (m_sessionRemainingSeconds != m_sessionTimeoutSeconds) {
        m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
        emit sessionRemainingSecondsChanged();
    }

    if (!m_sessionTimer->isActive()) {
        m_sessionTimer->start();
    }
}

bool Backend::adminUnlock(QString adminCode) {
    QString expected = m_env.value("ADMIN_UNLOCK_KEY").trimmed();
    if (expected.isEmpty()) {
        emit loginFailed("관리자 해제 키가 설정되어 있지 않습니다.");
        return false;
    }

    if (adminCode.trimmed() != expected) {
        emit loginFailed("관리자 해제 키가 올바르지 않습니다.");
        return false;
    }

    m_loginLocked = false;
    m_loginFailedAttempts = 0;
    emit loginLockChanged();
    return true;
}

bool Backend::updateRtspIp(QString ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    setRtspIp(trimmed);
    return true;
}

bool Backend::updateRtspConfig(QString ip, QString port) {
    QString ipTrimmed = ip.trimmed();
    QString portTrimmed = port.trimmed();
    if (ipTrimmed.isEmpty() || portTrimmed.isEmpty()) {
        return false;
    }

    bool ok = false;
    int portNum = portTrimmed.toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        return false;
    }

    setRtspIp(ipTrimmed);
    setRtspPort(QString::number(portNum));
    return true;
}

bool Backend::resetRtspConfigToEnv() {
    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8554").trimmed();

    const QString nextIp = envIp.isEmpty() ? QString("127.0.0.1") : envIp;
    const QString nextPort = envPort.isEmpty() ? QString("8554") : envPort;

    QSettings settings;
    settings.setValue("network/use_custom_rtsp", false);
    settings.remove("network/rtsp_ip");
    settings.remove("network/rtsp_port");
    m_useCustomRtspConfig = false;

    bool changed = false;
    if (m_rtspIp != nextIp) {
        m_rtspIp = nextIp;
        emit rtspIpChanged();
        changed = true;
    }
    if (m_rtspPort != nextPort) {
        m_rtspPort = nextPort;
        emit rtspPortChanged();
        changed = true;
    }

    return changed;
}

void Backend::refreshRecordings() {
    QString urlStr = QString("%1/recordings?user=%2").arg(serverUrl(), m_userId);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    qDebug() << "Refreshing recordings for user:" << m_userId << "from:" << url.toString();
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            qDebug() << "Recordings Response:" << data.left(200);
            QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject rootObj = doc.object();
            if (!doc.isNull() && rootObj.contains("files")) {
                const QJsonArray arr = rootObj.value("files").toArray();
                qDebug() << "Found" << arr.size() << "files";
                
                QVariantList fileList;
                for (const QJsonValue &val : arr) {
                    const QJsonObject obj = val.toObject();
                    QVariantMap fileMap;
                    fileMap["name"] = obj["name"].toString();
                    fileMap["size"] = obj["size"].toVariant().toLongLong();
                    fileList.append(fileMap);
                }
                emit recordingsLoaded(fileList);
            } else {
                qDebug() << "Response has no 'files' array";
                emit recordingsLoaded(QVariantList());
            }
        } else {
            qDebug() << "Refesh Failed:" << reply->errorString();
            emit recordingsLoadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void Backend::deleteRecording(QString name) {
    QUrl url(serverUrl() + "/recordings?file=" + name);
    QNetworkRequest request(url);
    QNetworkReply *reply = m_manager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit deleteSuccess();
            refreshRecordings();
        } else {
            emit deleteFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void Backend::renameRecording(QString oldName, QString newName) {
    QUrl url(serverUrl() + "/recordings/rename");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["oldName"] = oldName;
    json["newName"] = newName;
    QJsonDocument doc(json);
    QNetworkReply *reply = m_manager->put(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit renameSuccess();
            refreshRecordings();
        } else {
            emit renameFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

QString Backend::getStreamUrl(QString fileName) {
    return QString("%1/stream?file=%2").arg(serverUrl(), fileName);
}

void Backend::downloadAndPlay(QString fileName) {
    qDebug() << "Backend::downloadAndPlay called for:" << fileName;
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFilePath = tempDir + "/" + fileName;
    if (QFile::exists(m_tempFilePath)) {
        QFile::remove(m_tempFilePath);
    }
    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    m_downloadReply = m_manager->get(request);
    
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total){
        if (total > 0) {
            emit downloadProgress(received, total);
        }
    });
    
    connect(m_downloadReply, &QNetworkReply::finished, this, [=](){
        if (!m_downloadReply) return; 

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(m_tempFilePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_downloadReply->readAll());
                file.close();
                emit downloadFinished("file:///" + m_tempFilePath);
            } else {
                emit downloadError("Failed to save file: " + file.errorString());
            }
        } else if (m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
             emit downloadError(m_downloadReply->errorString());
        }
        
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    });
}

void Backend::cancelDownload() {
    if (m_downloadReply) {
        QNetworkReply *reply = m_downloadReply;
        m_downloadReply = nullptr;

        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
        qDebug() << "Download cancelled by user";
    }
}

void Backend::exportRecording(QString fileName, QString savePath) {
    cancelDownload();
    m_tempFilePath = savePath;
    
    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    m_downloadReply = m_manager->get(request);
    
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total){
        if (total > 0) emit downloadProgress(received, total);
    });
    
    connect(m_downloadReply, &QNetworkReply::finished, this, [=](){
        if (!m_downloadReply) return;

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_downloadReply->readAll());
                file.close();
                emit downloadFinished(savePath);
            } else {
                emit downloadError("Failed to ensure file: " + file.errorString());
            }
        } else if (m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
             emit downloadError(m_downloadReply->errorString());
        }
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    });
}

void Backend::checkStorage() {
    QUrl url(serverUrl() + "/system/storage");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){ onStorageReply(reply); });
}

void Backend::onStorageReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            double totalBytes = obj["total_bytes"].toDouble();
            double usedBytes = obj["used_bytes"].toDouble();
            
            if(totalBytes > 0) {
                m_storagePercent = (int)((usedBytes / totalBytes) * 100.0);
                m_storageUsed = QString::number(usedBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                m_storageTotal = QString::number(totalBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                
                emit storageChanged();
            }
        }
    }
    reply->deleteLater();
}

void Backend::onSessionTick() {
    if (!m_isLoggedIn) {
        m_sessionTimer->stop();
        return;
    }

    if (m_sessionRemainingSeconds > 0) {
        m_sessionRemainingSeconds--;
        emit sessionRemainingSecondsChanged();
    }

    if (m_sessionRemainingSeconds <= 0) {
        logout();
        emit sessionExpired();
    }
}



