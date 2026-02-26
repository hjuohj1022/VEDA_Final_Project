#include "Backend.h"

#include <QRegularExpression>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

// RTSP IP를 설정하고 사용자 설정으로 저장한다.
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

// RTSP 포트를 설정하고 사용자 설정으로 저장한다.
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

// 카메라 인덱스와 스트림 타입으로 RTSP URL을 조합한다.
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

    const QString user = m_useCustomRtspAuth
            ? m_rtspUsernameOverride
            : m_env.value("RTSP_USERNAME").trimmed();
    const QString pass = m_useCustomRtspAuth
            ? m_rtspPasswordOverride
            : m_env.value("RTSP_PASSWORD").trimmed();
    QString authPrefix;
    if (!user.isEmpty()) {
        authPrefix = user;
        if (!pass.isEmpty()) {
            authPrefix += ":" + pass;
        }
        authPrefix += "@";
    }

    QString scheme = "rtsp";
    if (m_rtspPort == "8555") {
        scheme = "rtsps";
    }

    return QString("%1://%2%3:%4%5").arg(scheme, authPrefix, m_rtspIp, m_rtspPort, path);
}

bool Backend::updateRtspIp(QString ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    setRtspIp(trimmed);
    return true;
}

// RTSP IP/포트(선택적으로 URL 경로) 설정을 갱신한다.
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
            portNum = 8555;
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

// RTSP 설정을 .env 기본값으로 되돌린다.
bool Backend::resetRtspConfigToEnv() {
    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8555").trimmed();

    const QString nextIp = envIp.isEmpty() ? QString("127.0.0.1") : envIp;
    const QString nextPort = envPort.isEmpty() ? QString("8555") : envPort;

    QSettings settings;
    settings.setValue("network/use_custom_rtsp", false);
    settings.remove("network/rtsp_ip");
    settings.remove("network/rtsp_port");
    const bool hadCustomAuth = m_useCustomRtspAuth;
    m_useCustomRtspConfig = false;
    m_rtspMainPathTemplateOverride.clear();
    m_rtspSubPathTemplateOverride.clear();
    m_useCustomRtspAuth = false;
    m_rtspUsernameOverride.clear();
    m_rtspPasswordOverride.clear();

    bool changed = hadCustomAuth;
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

// RTSP 인증 정보를 런타임 오버라이드로 설정한다.
bool Backend::updateRtspCredentials(QString username, QString password) {
    const QString nextUser = username.trimmed();
    const QString nextPass = password;
    if (nextUser.isEmpty()) {
        return false;
    }

    m_useCustomRtspAuth = true;
    m_rtspUsernameOverride = nextUser;
    m_rtspPasswordOverride = nextPass;
    return true;
}

// RTSP 인증 정보를 .env 값으로 되돌린다.
void Backend::useEnvRtspCredentials() {
    m_useCustomRtspAuth = false;
    m_rtspUsernameOverride.clear();
    m_rtspPasswordOverride.clear();
}

// RTSP TCP 포트 연결 가능 여부를 빠르게 확인한다.
void Backend::probeRtspEndpoint(QString ip, QString port, int timeoutMs) {
    const QString ipTrimmed = ip.trimmed();
    if (ipTrimmed.isEmpty()) {
        emit rtspProbeFinished(false, "IP가 비어 있습니다.");
        return;
    }

    bool ok = false;
    int portNum = port.trimmed().toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        portNum = 8555;
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

