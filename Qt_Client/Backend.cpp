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
#include <QUrlQuery>
#include <QRegularExpression>
#include <QByteArray>
#include <QAuthenticator>

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this));
    setActiveCameras(0);
    loadEnv();
    connect(m_manager, &QNetworkAccessManager::authenticationRequired,
            this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QString user = m_env.value("SUNAPI_USER").trimmed();
                const QString pass = m_env.value("SUNAPI_PASSWORD").trimmed();
                const QString sunapiHost = m_env.value("SUNAPI_IP").trimmed();
                const QString host = reply ? reply->url().host() : QString();
                qInfo() << "[SUNAPI][AUTH] challenge host=" << host
                        << "realm=" << authenticator->realm()
                        << "userConfigured=" << !user.isEmpty();
                if (!user.isEmpty() && !sunapiHost.isEmpty() && host.compare(sunapiHost, Qt::CaseInsensitive) == 0) {
                    authenticator->setUser(user);
                    authenticator->setPassword(pass);
                }
            });
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
    const QString envMainTemplate = m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed().isEmpty()
            ? defaultMainTemplate
            : m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed();
    const QString envSubTemplate = m_env.value("RTSP_SUB_PATH_TEMPLATE").trimmed();

    const QString mainTemplate = m_rtspMainPathTemplateOverride.isEmpty()
            ? envMainTemplate
            : m_rtspMainPathTemplateOverride;
    const QString subTemplate = m_rtspSubPathTemplateOverride.isEmpty()
            ? (envSubTemplate.isEmpty() ? defaultSubTemplate : envSubTemplate)
            : m_rtspSubPathTemplateOverride;

    QString pathTemplate = mainTemplate;

    if (useSubStream) {
        pathTemplate = subTemplate;
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

bool Backend::sendSunapiCommand(const QString &cgiName,
                                const QMap<QString, QString> &params,
                                int cameraIndex,
                                const QString &actionLabel,
                                bool includeChannelParam) {
    if (cameraIndex < 0) {
        emit cameraControlMessage(QString("%1 실패: 잘못된 카메라 인덱스").arg(actionLabel), true);
        return false;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage(QString("%1 실패: SUNAPI_IP가 비어 있습니다").arg(actionLabel), true);
        return false;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();
    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath(QString("/stw-cgi/%1").arg(cgiName));

    QUrlQuery query;
    // Some SUNAPI firmware is sensitive to parameter order.
    // Keep canonical order: msubmenu -> action -> Channel -> others.
    if (params.contains("msubmenu")) {
        query.addQueryItem("msubmenu", params.value("msubmenu"));
    }
    if (params.contains("action")) {
        query.addQueryItem("action", params.value("action"));
    }

    if (params.contains("Channel")) {
        query.addQueryItem("Channel", params.value("Channel"));
    } else if (params.contains("channel")) {
        query.addQueryItem("channel", params.value("channel"));
    } else if (includeChannelParam) {
        query.addQueryItem("Channel", QString::number(cameraIndex));
    }

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == "msubmenu" || it.key() == "action" || it.key() == "Channel" || it.key() == "channel") {
            continue;
        }
        query.addQueryItem(it.key(), it.value());
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << url.toString();
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString bodyLower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            bool sunapiBodyError = false;
            QString sunapiErrMsg;

            // SUNAPI는 HTTP 200이어도 본문에 Error/Fail을 내려주는 경우가 있어 본문 검사 필요.
            if (!body.isEmpty()) {
                const QRegularExpression errPattern("^\\s*error\\s*=\\s*([^\\r\\n]+)",
                                                    QRegularExpression::CaseInsensitiveOption
                                                    | QRegularExpression::MultilineOption);
                const QRegularExpressionMatch errMatch = errPattern.match(body);
                if (errMatch.hasMatch()) {
                    const QString errValue = errMatch.captured(1).trimmed();
                    const QString errValueLower = errValue.toLower();
                    if (errValueLower != "0"
                        && errValueLower != "ok"
                        && errValueLower != "none"
                        && errValueLower != "success") {
                        sunapiBodyError = true;
                        sunapiErrMsg = QString("Error=%1").arg(errValue);
                    }
                }

                if (!sunapiBodyError
                    && (bodyLower.contains("fail")
                        || bodyLower.contains("unsupported")
                        || bodyLower.contains("not support")
                        || bodyLower.contains("invalid"))) {
                    // 일부 모델은 Error= 대신 문자열만 반환함.
                    sunapiBodyError = true;
                    sunapiErrMsg = body.left(120);
                }
            }

            if (sunapiBodyError) {
                const QString err = QString("%1 실패: 장비 응답 오류 (%2)")
                                        .arg(actionLabel, sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit cameraControlMessage(err, true);
            } else {
                emit cameraControlMessage(QString("%1 성공").arg(actionLabel), false);
            }
        } else {
            const QString err = QString("%1 실패 (HTTP %2): %3")
                                    .arg(actionLabel)
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
        }
        reply->deleteLater();
    });

    return true;
}

bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "In"}},
        cameraIndex,
        "줌 인");
}

bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Out"}},
        cameraIndex,
        "줌 아웃");
}

bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Stop"}},
        cameraIndex,
        "줌 정지");
}

bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Near"}},
        cameraIndex,
        "초점 Near");
}

bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Far"}},
        cameraIndex,
        "초점 Far");
}

bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Stop"}},
        cameraIndex,
        "초점 정지");
}

bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"Mode", "SimpleFocus"}},
        cameraIndex,
        "오토포커스");
}

bool Backend::sunapiMovePreset(int cameraIndex, int presetId) {
    if (presetId < 1) {
        emit cameraControlMessage("프리셋 이동 실패: 프리셋 번호는 1 이상이어야 합니다.", true);
        return false;
    }
    return sendSunapiCommand(
        "ptzcontrol.cgi",
        {{"msubmenu", "preset"}, {"action", "control"}, {"Preset", QString::number(presetId)}},
        cameraIndex,
        QString("프리셋 %1 이동").arg(presetId));
}

bool Backend::sunapiSetExposureMode(int cameraIndex, QString mode) {
    const QString normalized = mode.trimmed();
    if (normalized.isEmpty()) {
        emit cameraControlMessage("노출 모드 변경 실패: 모드 값이 비어 있습니다.", true);
        return false;
    }
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "exposure"}, {"action", "control"}, {"Mode", normalized}},
        cameraIndex,
        QString("노출 모드 %1").arg(normalized));
}

bool Backend::sunapiSetWhiteBalanceMode(int cameraIndex, QString mode) {
    const QString normalized = mode.trimmed();
    if (normalized.isEmpty()) {
        emit cameraControlMessage("화이트밸런스 변경 실패: 모드 값이 비어 있습니다.", true);
        return false;
    }
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "whitebalance"}, {"action", "control"}, {"Mode", normalized}},
        cameraIndex,
        QString("화이트밸런스 %1").arg(normalized));
}

void Backend::sunapiLoadSupportedPtzActions(int cameraIndex) {
    if (cameraIndex < 0) {
        emit cameraControlMessage("지원 기능 조회 실패: 잘못된 카메라 인덱스", true);
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage("지원 기능 조회 실패: SUNAPI_IP가 비어 있습니다", true);
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath("/stw-cgi/ptzcontrol.cgi");

    QUrlQuery query;
    query.addQueryItem("msubmenu", "supportedptzactions");
    query.addQueryItem("action", "view");
    query.addQueryItem("Channel", QString::number(cameraIndex));
    url.setQuery(query);

    QNetworkRequest request(url);
    qInfo() << "[SUNAPI] request:" << "지원 기능 조회" << "url=" << url.toString();
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cameraIndex]() {
        QVariantMap actions;
        actions.insert("zoom", true);
        actions.insert("focus", true);
        actions.insert("preset", true);

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString lower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            // Best-effort parser: disable only when body explicitly says unsupported/unavailable.
            const bool zoomUnsupported =
                (lower.contains("zoom=\"false\"")
                 || lower.contains("zoom=false")
                 || lower.contains("zoom: false")
                 || lower.contains("zoom unsupported"));
            const bool focusUnsupported =
                (lower.contains("focus=\"false\"")
                 || lower.contains("focus=false")
                 || lower.contains("focus: false")
                 || lower.contains("focus unsupported"));
            const bool presetUnsupported =
                (lower.contains("preset=\"false\"")
                 || lower.contains("preset=false")
                 || lower.contains("preset: false")
                 || lower.contains("preset unsupported"));

            if (zoomUnsupported) actions.insert("zoom", false);
            if (focusUnsupported) actions.insert("focus", false);
            if (presetUnsupported) actions.insert("preset", false);

            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
            emit cameraControlMessage("지원 기능 조회 완료", false);
        } else {
            const QString err = QString("지원 기능 조회 실패 (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
        }
        reply->deleteLater();
    });
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
    if (m_loginInProgress) {
        emit loginFailed("로그인 요청 처리 중입니다. 잠시만 기다려 주세요.");
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
    m_loginReply = reply;
    m_loginInProgress = true;
    connect(reply, &QNetworkReply::sslErrors, this, [=](const QList<QSslError> &errors) {
        for (const QSslError &e : errors) {
            qWarning() << "[LOGIN][SSL]" << e.errorString();
        }
    });

    QTimer *loginTimeout = new QTimer(reply);
    loginTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, m_env.value("LOGIN_TIMEOUT_MS", "15000").toInt());
    loginTimeout->setInterval(timeoutMs);
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
        if (loginTimeout) {
            loginTimeout->stop();
        }
        qWarning() << "[LOGIN] status=" << statusCode
                   << "netError=" << static_cast<int>(netError)
                   << "timedOut=" << timedOut
                   << "timeoutMs=" << timeoutMs
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
            } else {
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

                if (netError == QNetworkReply::OperationCanceledError) {
                    emit loginFailed("로그인 요청이 취소되었습니다. 네트워크 상태를 확인 후 다시 시도해 주세요.");
                } else if (isSslFailure) {
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
        }
        if (m_loginReply == reply) {
            m_loginReply = nullptr;
        }
        m_loginInProgress = false;
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
    QString inputTrimmed = ip.trimmed();
    QString portTrimmed = port.trimmed();
    if (inputTrimmed.isEmpty()) {
        return false;
    }

    QString ipTrimmed = inputTrimmed;
    int portNum = 0;
    bool ok = false;
    if (!portTrimmed.isEmpty()) {
        portNum = portTrimmed.toInt(&ok);
        if (!ok || portNum < 1 || portNum > 65535) {
            return false;
        }
    }

    QString requestedMainPath;
    QString requestedSubPath;

    const bool inputIsRtspUrl = inputTrimmed.startsWith("rtsp://", Qt::CaseInsensitive)
                                || inputTrimmed.startsWith("rtsps://", Qt::CaseInsensitive);

    if (inputIsRtspUrl) {
        QUrl rtspUrl(inputTrimmed);
        if (!rtspUrl.isValid() || rtspUrl.host().trimmed().isEmpty()) {
            return false;
        }

        ipTrimmed = rtspUrl.host().trimmed();
        if (portNum == 0) {
            int parsedPort = rtspUrl.port();
            if (parsedPort > 0) {
                portNum = parsedPort;
            }
        }

        QString path = rtspUrl.path().trimmed();
        if (!path.isEmpty() && path != "/") {
            QRegularExpression indexHead("^/(\\d+)(/.*)$");
            QRegularExpressionMatch m = indexHead.match(path);
            if (m.hasMatch()) {
                path = "/{index}" + m.captured(2);
            }

            if (!path.startsWith('/')) {
                path.prepend('/');
            }

            if (requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
                if (path.contains("/main")) {
                    requestedMainPath = path;
                    requestedSubPath = path;
                    requestedSubPath.replace("/main", "/sub");
                } else if (path.contains("/sub")) {
                    requestedSubPath = path;
                    requestedMainPath = path;
                    requestedMainPath.replace("/sub", "/main");
                }
            }
        }
    }

    if (portNum == 0) {
        portNum = m_rtspPort.trimmed().toInt(&ok);
        if (!ok || portNum < 1 || portNum > 65535) {
            portNum = 8554;
        }
    }

    if (ipTrimmed.isEmpty()) {
        return false;
    }

    // If user entered plain host/IP+port in settings popup,
    // keep path policy from .env (clear runtime overrides).
    if (!inputIsRtspUrl && requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
        m_rtspMainPathTemplateOverride.clear();
        m_rtspSubPathTemplateOverride.clear();
    }

    if (!requestedMainPath.isEmpty()) {
        if (!requestedMainPath.startsWith('/')) {
            requestedMainPath.prepend('/');
        }
        if (!requestedMainPath.contains("{index}")) {
            return false;
        }
        m_rtspMainPathTemplateOverride = requestedMainPath;
    }

    if (!requestedSubPath.isEmpty()) {
        if (!requestedSubPath.startsWith('/')) {
            requestedSubPath.prepend('/');
        }
        if (!requestedSubPath.contains("{index}")) {
            return false;
        }
        m_rtspSubPathTemplateOverride = requestedSubPath;
    }

    if (!requestedMainPath.isEmpty() && requestedSubPath.isEmpty() && m_rtspSubPathTemplateOverride.isEmpty()) {
        m_rtspSubPathTemplateOverride = requestedMainPath;
        m_rtspSubPathTemplateOverride.replace("/main", "/sub");
    }
    if (!requestedSubPath.isEmpty() && requestedMainPath.isEmpty() && m_rtspMainPathTemplateOverride.isEmpty()) {
        m_rtspMainPathTemplateOverride = requestedSubPath;
        m_rtspMainPathTemplateOverride.replace("/sub", "/main");
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
    m_rtspMainPathTemplateOverride.clear();
    m_rtspSubPathTemplateOverride.clear();

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

void Backend::probeRtspEndpoint(QString ip, QString port, int timeoutMs) {
    const QString ipTrimmed = ip.trimmed();
    if (ipTrimmed.isEmpty()) {
        emit rtspProbeFinished(false, "IP가 비어 있습니다.");
        return;
    }

    bool ok = false;
    int portNum = port.trimmed().toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        portNum = 8554;
    }

    const int safeTimeoutMs = qBound(300, timeoutMs, 5000);

    QTcpSocket *socket = new QTcpSocket(this);
    QTimer *timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->setInterval(safeTimeoutMs);

    auto done = [this, socket, timer](bool success, const QString &errorMsg) {
        if (socket->property("probe_done").toBool()) {
            return;
        }
        socket->setProperty("probe_done", true);
        timer->stop();
        socket->abort();
        emit rtspProbeFinished(success, errorMsg);
        socket->deleteLater();
    };

    connect(socket, &QTcpSocket::connected, this, [done]() {
        done(true, QString());
    });

    connect(socket, &QTcpSocket::errorOccurred, this,
            [done, socket](QAbstractSocket::SocketError) {
                done(false, QString("RTSP 서버 연결 실패: %1").arg(socket->errorString()));
            });

    connect(timer, &QTimer::timeout, this, [done]() {
        done(false, QString("RTSP 연결 확인 시간 초과"));
    });

    timer->start();
    socket->connectToHost(ipTrimmed, static_cast<quint16>(portNum));
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



