#include "Backend.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

void Backend::setRtspIp(const QString &ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspIp == trimmed) return;

    m_rtspIp = trimmed;
    // 사용자 수동 설정값을 앱 재시작 후에도 유지
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_ip", m_rtspIp);
    m_useCustomRtspConfig = true;
    emit rtspIpChanged();
}

void Backend::setRtspPort(const QString &port) {
    QString trimmed = port.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspPort == trimmed) return;

    m_rtspPort = trimmed;
    // 사용자 수동 설정값을 앱 재시작 후에도 유지
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

    // override 값이 있으면 override 우선, 없으면 .env 템플릿 사용
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
        authPrefix = QString::fromUtf8(QUrl::toPercentEncoding(user));
        if (!pass.isEmpty()) {
            authPrefix += ":" + QString::fromUtf8(QUrl::toPercentEncoding(pass));
        }
        authPrefix += "@";
    }

    const QString schemeRaw = m_env.value("RTSP_SCHEME", "rtsps").trimmed().toLower();
    const QString scheme = (schemeRaw == "rtsps") ? QString("rtsps") : QString("rtsp");
    return QString("%1://%2%3:%4%5").arg(scheme, authPrefix, m_rtspIp, m_rtspPort, path);
}

QString Backend::buildPlaybackRtspUrl(int channelIndex, const QString &dateText, const QString &timeText) const {
    if (channelIndex < 0) {
        return QString();
    }

    const QString dateTrimmed = dateText.trimmed();
    const QString timeTrimmed = timeText.trimmed();
    const QString dateTimeText = dateTrimmed + " " + timeTrimmed;
    const QDateTime dt = QDateTime::fromString(dateTimeText, "yyyy-MM-dd HH:mm:ss");
    if (!dt.isValid()) {
        return QString();
    }

    const QString ts = dt.toString("yyyyMMddHHmmss");
    const QString rtspHost = m_env.value("SUNAPI_RTSP_HOST").trimmed();
    const QString host = !rtspHost.isEmpty()
            ? rtspHost
            : (m_env.value("SUNAPI_IP").trimmed().isEmpty() ? m_rtspIp : m_env.value("SUNAPI_IP").trimmed());
    if (host.trimmed().isEmpty()) {
        return QString();
    }

    const QString user = m_useCustomRtspAuth
            ? m_rtspUsernameOverride
            : m_env.value("SUNAPI_USER").trimmed();
    const QString pass = m_useCustomRtspAuth
            ? m_rtspPasswordOverride
            : m_env.value("SUNAPI_PASSWORD").trimmed();

    QString authPrefix;
    if (!user.isEmpty()) {
        authPrefix = QString::fromUtf8(QUrl::toPercentEncoding(user));
        if (!pass.isEmpty()) {
            authPrefix += ":" + QString::fromUtf8(QUrl::toPercentEncoding(pass));
        }
        authPrefix += "@";
    }

    const QString portText = m_env.value("SUNAPI_RTSP_PORT", "554").trimmed();
    bool ok = false;
    int port = portText.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        port = 554;
    }

    return QString("rtsp://%1%2:%3/%4/recording/%5/play.smp")
            .arg(authPrefix, host, QString::number(port), QString::number(channelIndex), ts);
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

    // rtsp:// 또는 rtsps:// URL 전체 입력을 허용
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
            // /0/... 형태 입력 시 카메라 인덱스를 {index} 플레이스홀더로 일반화
            QRegularExpression indexHead("^/(\\d+)(/.*)$");
            QRegularExpressionMatch m = indexHead.match(path);
            if (m.hasMatch()) {
                path = "/{index}" + m.captured(2);
            }

            if (!path.startsWith('/')) {
                path.prepend('/');
            }

            if (requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
                // main/sub 중 한쪽만 들어오면 다른 한쪽은 규칙 기반으로 자동 보정
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

    if (!inputIsRtspUrl && requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
        // URL이 아닌 단순 IP 입력이면 기존 path override 초기화
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
    const QString envPort = m_env.value("RTSP_PORT", "8555").trimmed();

    const QString nextIp = envIp.isEmpty() ? QString("127.0.0.1") : envIp;
    const QString nextPort = envPort.isEmpty() ? QString("8555") : envPort;

    // 커스텀 RTSP 설정/인증값을 모두 해제하고 .env 기본값으로 복귀
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

void Backend::useEnvRtspCredentials() {
    m_useCustomRtspAuth = false;
    m_rtspUsernameOverride.clear();
    m_rtspPasswordOverride.clear();
}


