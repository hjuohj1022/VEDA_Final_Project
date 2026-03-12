#include "internal/rtsp/BackendRtspConfigService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

void BackendRtspConfigService::setRtspIp(Backend *backend, BackendPrivate *state, const QString &ip)
{
    const QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (state->m_rtspIp == trimmed) {
        return;
    }

    state->m_rtspIp = trimmed;

    // 사용자의 수동 설정값을 앱 시작 시에도 유지
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_ip", state->m_rtspIp);
    state->m_useCustomRtspConfig = true;
    emit backend->rtspIpChanged();
}

void BackendRtspConfigService::setRtspPort(Backend *backend, BackendPrivate *state, const QString &port)
{
    const QString trimmed = port.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (state->m_rtspPort == trimmed) {
        return;
    }

    state->m_rtspPort = trimmed;

    // 사용자의 수동 설정값을 앱 시작 시에도 유지
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_port", state->m_rtspPort);
    state->m_useCustomRtspConfig = true;
    emit backend->rtspPortChanged();
}

QString BackendRtspConfigService::buildRtspUrl(const Backend *backend,
                                               const BackendPrivate *state,
                                               int cameraIndex,
                                               bool useSubStream)
{
    Q_UNUSED(backend);

    if (cameraIndex < 0) {
        return QString();
    }

    // override 값이 있으면 override 우선, 없으면 .env 템플릿 사용
    const QString defaultMainTemplate = "/{index}/onvif/profile{profile}/media.smp";
    const QString defaultSubTemplate = "/{index}/onvif/profile{profile}/media.smp";
    const QString envMainTemplate = state->m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed().isEmpty()
            ? defaultMainTemplate
            : state->m_env.value("RTSP_MAIN_PATH_TEMPLATE", defaultMainTemplate).trimmed();
    const QString envSubTemplate = state->m_env.value("RTSP_SUB_PATH_TEMPLATE").trimmed();

    const QString mainTemplate = state->m_rtspMainPathTemplateOverride.isEmpty()
            ? envMainTemplate
            : state->m_rtspMainPathTemplateOverride;
    const QString subTemplate = state->m_rtspSubPathTemplateOverride.isEmpty()
            ? (envSubTemplate.isEmpty() ? defaultSubTemplate : envSubTemplate)
            : state->m_rtspSubPathTemplateOverride;

    QString pathTemplate = mainTemplate;
    if (useSubStream) {
        pathTemplate = subTemplate;
    }

    const QString mainProfile = state->m_env.value("RTSP_MAIN_PROFILE", "1").trimmed();
    const QString subProfile = state->m_env.value("RTSP_SUB_PROFILE", "2").trimmed();
    const QString selectedProfile = useSubStream
            ? (subProfile.isEmpty() ? QString("2") : subProfile)
            : (mainProfile.isEmpty() ? QString("1") : mainProfile);

    QString path = pathTemplate;
    path.replace("{index}", QString::number(cameraIndex));
    path.replace("{profile}", selectedProfile);
    if (!path.startsWith('/')) {
        path.prepend('/');
    }

    const QString user = state->m_useCustomRtspAuth
            ? state->m_rtspUsernameOverride
            : state->m_env.value("RTSP_USERNAME").trimmed();
    const QString pass = state->m_useCustomRtspAuth
            ? state->m_rtspPasswordOverride
            : state->m_env.value("RTSP_PASSWORD").trimmed();
    QString authPrefix;
    if (!user.isEmpty()) {
        authPrefix = QString::fromUtf8(QUrl::toPercentEncoding(user));
        if (!pass.isEmpty()) {
            authPrefix += ":" + QString::fromUtf8(QUrl::toPercentEncoding(pass));
        }
        authPrefix += "@";
    }

    const QString schemeRaw = state->m_env.value("RTSP_SCHEME", "rtsps").trimmed().toLower();
    const QString scheme = (schemeRaw == "rtsps") ? QString("rtsps") : QString("rtsp");
    return QString("%1://%2%3:%4%5").arg(scheme, authPrefix, state->m_rtspIp, state->m_rtspPort, path);
}

bool BackendRtspConfigService::updateRtspIp(Backend *backend, BackendPrivate *state, const QString &ip)
{
    const QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    setRtspIp(backend, state, trimmed);
    return true;
}

bool BackendRtspConfigService::updateRtspConfig(Backend *backend,
                                                BackendPrivate *state,
                                                const QString &ip,
                                                const QString &port)
{
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

    // rtsp:// 또는 rtsps:// URL 전체 입력도 허용
    const bool inputIsRtspUrl = inputTrimmed.startsWith("rtsp://", Qt::CaseInsensitive)
                                || inputTrimmed.startsWith("rtsps://", Qt::CaseInsensitive);

    if (inputIsRtspUrl) {
        QUrl rtspUrl(inputTrimmed);
        if (!rtspUrl.isValid() || rtspUrl.host().trimmed().isEmpty()) {
            return false;
        }

        ipTrimmed = rtspUrl.host().trimmed();
        if (portNum == 0) {
            const int parsedPort = rtspUrl.port();
            if (parsedPort > 0) {
                portNum = parsedPort;
            }
        }

        QString path = rtspUrl.path().trimmed();
        if (!path.isEmpty() && path != "/") {
            // /0/... 형태 입력 시 카메라 인덱스를 {index} 플레이스홀더로 일반화
            const QRegularExpression indexHead("^/(\\d+)(/.*)$");
            const QRegularExpressionMatch m = indexHead.match(path);
            if (m.hasMatch()) {
                path = "/{index}" + m.captured(2);
            }

            if (!path.startsWith('/')) {
                path.prepend('/');
            }

            if (requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
                // main/sub 중 한쪽만 들어오면 다른 쪽도 규칙 기반으로 자동 보정
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
        portNum = state->m_rtspPort.trimmed().toInt(&ok);
        if (!ok || portNum < 1 || portNum > 65535) {
            portNum = 8555;
        }
    }

    if (ipTrimmed.isEmpty()) {
        return false;
    }

    if (!inputIsRtspUrl && requestedMainPath.isEmpty() && requestedSubPath.isEmpty()) {
        // URL이 아닌 순수 IP 입력이면 기존 path override 초기화
        state->m_rtspMainPathTemplateOverride.clear();
        state->m_rtspSubPathTemplateOverride.clear();
    }

    if (!requestedMainPath.isEmpty()) {
        if (!requestedMainPath.startsWith('/')) {
            requestedMainPath.prepend('/');
        }
        if (!requestedMainPath.contains("{index}")) {
            return false;
        }
        state->m_rtspMainPathTemplateOverride = requestedMainPath;
    }

    if (!requestedSubPath.isEmpty()) {
        if (!requestedSubPath.startsWith('/')) {
            requestedSubPath.prepend('/');
        }
        if (!requestedSubPath.contains("{index}")) {
            return false;
        }
        state->m_rtspSubPathTemplateOverride = requestedSubPath;
    }

    if (!requestedMainPath.isEmpty() && requestedSubPath.isEmpty() && state->m_rtspSubPathTemplateOverride.isEmpty()) {
        state->m_rtspSubPathTemplateOverride = requestedMainPath;
        state->m_rtspSubPathTemplateOverride.replace("/main", "/sub");
    }
    if (!requestedSubPath.isEmpty() && requestedMainPath.isEmpty() && state->m_rtspMainPathTemplateOverride.isEmpty()) {
        state->m_rtspMainPathTemplateOverride = requestedSubPath;
        state->m_rtspMainPathTemplateOverride.replace("/sub", "/main");
    }

    setRtspIp(backend, state, ipTrimmed);
    setRtspPort(backend, state, QString::number(portNum));
    return true;
}

bool BackendRtspConfigService::resetRtspConfigToEnv(Backend *backend, BackendPrivate *state)
{
    const QString envIp = state->m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = state->m_env.value("RTSP_PORT", "8555").trimmed();

    const QString nextIp = envIp.isEmpty() ? QString("127.0.0.1") : envIp;
    const QString nextPort = envPort.isEmpty() ? QString("8555") : envPort;

    // 커스텀 RTSP 설정/인증값을 모두 해제하고 .env 기본값으로 복귀
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", false);
    settings.remove("network/rtsp_ip");
    settings.remove("network/rtsp_port");
    const bool hadCustomAuth = state->m_useCustomRtspAuth;
    state->m_useCustomRtspConfig = false;
    state->m_rtspMainPathTemplateOverride.clear();
    state->m_rtspSubPathTemplateOverride.clear();
    state->m_useCustomRtspAuth = false;
    state->m_rtspUsernameOverride.clear();
    state->m_rtspPasswordOverride.clear();

    bool changed = hadCustomAuth;
    if (state->m_rtspIp != nextIp) {
        state->m_rtspIp = nextIp;
        emit backend->rtspIpChanged();
        changed = true;
    }
    if (state->m_rtspPort != nextPort) {
        state->m_rtspPort = nextPort;
        emit backend->rtspPortChanged();
        changed = true;
    }

    return changed;
}

bool BackendRtspConfigService::updateRtspCredentials(Backend *backend,
                                                     BackendPrivate *state,
                                                     const QString &username,
                                                     const QString &password)
{
    Q_UNUSED(backend);

    const QString nextUser = username.trimmed();
    const QString nextPass = password;
    if (nextUser.isEmpty()) {
        return false;
    }

    state->m_useCustomRtspAuth = true;
    state->m_rtspUsernameOverride = nextUser;
    state->m_rtspPasswordOverride = nextPass;
    return true;
}

void BackendRtspConfigService::useEnvRtspCredentials(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    state->m_useCustomRtspAuth = false;
    state->m_rtspUsernameOverride.clear();
    state->m_rtspPasswordOverride.clear();
}

