#include "Backend.h"

#include <QRegularExpression>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QDebug>

// RTSP IP 설정 및 사용자 설정 저장
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

// RTSP 포트 설정 및 사용자 설정 저장
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

// 카메라 인덱스/스트림 타입 기반 RTSP URL 조합
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

// Playback용 RTSP URL 조합
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
    const QString host = m_env.value("SUNAPI_IP").trimmed().isEmpty()
            ? m_rtspIp
            : m_env.value("SUNAPI_IP").trimmed();
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

// Playback RTSP 세션 준비용 SUNAPI 인증 절차(21/22) 수행
void Backend::preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText) {
    const QString playbackUrl = buildPlaybackRtspUrl(channelIndex, dateText, timeText);
    if (playbackUrl.isEmpty()) {
        emit playbackPrepareFailed("Playback URL 생성 실패: 채널/날짜/시간 형식을 확인해 주세요.");
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    const QString user = m_useCustomRtspAuth ? m_rtspUsernameOverride : m_env.value("SUNAPI_USER").trimmed();
    if (host.isEmpty() || user.isEmpty()) {
        emit playbackPrepareFailed("Playback 준비 실패: SUNAPI_IP 또는 SUNAPI_USER가 비어 있습니다.");
        return;
    }

    QUrl digestUri(playbackUrl);
    digestUri.setUserName(QString());
    digestUri.setPassword(QString());
    // Hanwha 웹뷰어 캡처 기준 WS 터널 RTSP URI 기본 포트(554) 생략
    if (digestUri.port() == 554) {
        digestUri.setPort(-1);
    }
    const QString digestUriText = digestUri.toString();

    // 1) RTSP OPTIONS 챌린지 인증 nonce/realm 추출
    bool portOk = false;
    const int rtspPort = m_env.value("SUNAPI_RTSP_PORT", "554").trimmed().toInt(&portOk);
    const int port = portOk ? rtspPort : 554;

    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(port));
    if (!socket.waitForConnected(1500)) {
        emit playbackPrepareFailed(QString("Playback 준비 실패: RTSP 연결 실패 (%1)").arg(socket.errorString()));
        return;
    }

    const QByteArray optionsReq =
            "OPTIONS " + digestUriText.toUtf8() + " RTSP/1.0\r\n"
            "CSeq: 1\r\n"
            "User-Agent: Team3VideoReceiver\r\n"
            "\r\n";
    socket.write(optionsReq);
    socket.flush();
    if (!socket.waitForReadyRead(1500)) {
        emit playbackPrepareFailed("Playback 준비 실패: RTSP 챌린지 응답 대기 시간 초과");
        return;
    }

    QByteArray rtspResp = socket.readAll();
    while (socket.waitForReadyRead(120)) {
        rtspResp += socket.readAll();
    }
    socket.disconnectFromHost();

    const QString rtspText = QString::fromUtf8(rtspResp);
    QRegularExpression realmRe("realm\\s*=\\s*\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nonceRe("nonce\\s*=\\s*\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
    const QString realm = realmRe.match(rtspText).captured(1).trimmed();
    const QString nonce = nonceRe.match(rtspText).captured(1).trimmed();
    if (realm.isEmpty() || nonce.isEmpty()) {
        qWarning() << "[PLAYBACK] RTSP challenge parse failed:" << rtspText.left(400);
        emit playbackPrepareFailed("Playback 준비 실패: RTSP Digest realm/nonce 추출 실패");
        return;
    }

    // 2) 추출 nonce/realm 기반 SUNAPI 인증 호출 및 playback 세션 준비
    const QString cnonce = QString::number(QRandomGenerator::global()->generate(), 16).left(8).toUpper();
    QUrl step2Url(QString("http://%1/stw-cgi/security.cgi").arg(host));
    QUrlQuery q2;
    q2.addQueryItem("msubmenu", "digestauth");
    q2.addQueryItem("action", "view");
    q2.addQueryItem("Method", "OPTIONS");
    q2.addQueryItem("Realm", realm);
    q2.addQueryItem("Nonce", nonce);
    q2.addQueryItem("Uri", digestUriText);
    q2.addQueryItem("username", user);
    q2.addQueryItem("password", "");
    q2.addQueryItem("Nc", "00000001");
    q2.addQueryItem("Cnonce", cnonce);
    q2.addQueryItem("SunapiSeqId", QString::number(QRandomGenerator::global()->bounded(100000, 999999)));
    step2Url.setQuery(q2);

    QNetworkRequest req2(step2Url);
    req2.setRawHeader("Accept", "application/json");
    req2.setRawHeader("X-Secure-Session", "Normal");
    QNetworkReply *reply2 = m_manager->get(req2);
    connect(reply2, &QNetworkReply::finished, this, [this, reply2, playbackUrl, digestUriText, realm, nonce, user]() {
        if (reply2->error() != QNetworkReply::NoError) {
            const QString err = QString("Playback 준비 2단계 실패: %1").arg(reply2->errorString());
            reply2->deleteLater();
            emit playbackPrepareFailed(err);
            return;
        }

        QString wsDigestResponse;
        const QByteArray body = reply2->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            wsDigestResponse = doc.object().value("Response").toString().trimmed();
        }
        if (wsDigestResponse.isEmpty()) {
            wsDigestResponse = m_env.value("PLAYBACK_WS_DIGEST_RESPONSE").trimmed();
        }

        const QString autoWs = m_env.value("PLAYBACK_WS_AUTO_CONNECT", "1").trimmed().toLower();
        const bool wsEnabled = (autoWs == "1" || autoWs == "true" || autoWs == "on");

        reply2->deleteLater();
        if (!wsEnabled) {
            emit playbackPrepared(playbackUrl);
        }
        if (wsEnabled) {
            if (wsDigestResponse.isEmpty()) {
                emit playbackPrepareFailed("Playback WS 준비 실패: digest Response 값이 비어 있습니다.");
                return;
            }

            m_playbackWsActive = true;
            m_playbackWsPlaySent = false;
            m_playbackWsPaused = false;
            m_playbackWsSession.clear();
            m_playbackWsH264SetupCseq = -1;
            m_playbackRtpPayloadType = 96;
            m_playbackRtpVideoChannel = 2;
            m_playbackRtpVideoAltChannel = 3;
            m_playbackWsSdpPublished = false;
            m_playbackSps.clear();
            m_playbackPps.clear();
            m_playbackFuBuffer.clear();
            m_playbackFuNalType = 0;
            m_playbackInterleavedBuffer.clear();
            m_playbackValidRtpCount = 0;
            m_playbackWsUri = digestUriText;
            m_playbackWsAuthHeader =
                    QString("Authorization: Digest username=\"%1\", realm=\"%2\", uri=\"%3\", nonce=\"%4\", response=\"%5\"")
                    .arg(user, realm, digestUriText, nonce, wsDigestResponse);

            streamingWsConnect();

            int delayMs = 120;
            auto queueRtsp = [this, &delayMs](const QByteArray &request) {
                const QString hex = QString::fromLatin1(request.toHex().toUpper());
                QTimer::singleShot(delayMs, this, [this, hex]() {
                    if (m_streamingWs && m_streamingWs->state() == QAbstractSocket::ConnectedState) {
                        streamingWsSendHex(hex);
                    }
                });
                delayMs += 80;
            };

            const QString userAgent = QStringLiteral("UWC[undefined]");
            auto buildAuthHeader = [&](int cseq) -> QByteArray {
                Q_UNUSED(cseq);
                QByteArray auth;
                auth += m_playbackWsAuthHeader.toUtf8();
                auth += "\r\n";
                return auth;
            };
            auto buildSetup = [&](int cseq, const QByteArray &trackId, const QByteArray &interleaved) -> QByteArray {
                const QByteArray trackUri = digestUriText.toUtf8() + "/trackID=" + trackId;
                QByteArray req;
                req += "SETUP " + trackUri + " RTSP/1.0\r\n";
                req += "CSeq: " + QByteArray::number(cseq) + "\r\n";
                req += buildAuthHeader(cseq);
                req += "User-Agent: " + userAgent.toUtf8() + "\r\n";
                req += "Transport: RTP/AVP/TCP;unicast;interleaved=" + interleaved + "\r\n";
                req += "\r\n";
                return req;
            };

            const QByteArray options1 =
                    "OPTIONS " + digestUriText.toUtf8() + " RTSP/1.0\r\n"
                    "CSeq: 1\r\n"
                    "User-Agent: " + userAgent.toUtf8() + "\r\n"
                    "\r\n";
            queueRtsp(options1);

            QByteArray options2;
            options2 += "OPTIONS " + digestUriText.toUtf8() + " RTSP/1.0\r\n";
            options2 += "CSeq: 2\r\n";
            options2 += buildAuthHeader(2);
            options2 += "User-Agent: " + userAgent.toUtf8() + "\r\n";
            options2 += "\r\n";
            queueRtsp(options2);

            // 일부 장비 DESCRIBE 503 반환 이슈로 기본 생략
            const QString sendDescribe = m_env.value("PLAYBACK_WS_SEND_DESCRIBE", "0").trimmed().toLower();
            int cseq = 3;
            if (sendDescribe == "1" || sendDescribe == "true" || sendDescribe == "on") {
                QByteArray describe;
                describe += "DESCRIBE " + digestUriText.toUtf8() + " RTSP/1.0\r\n";
                describe += "CSeq: " + QByteArray::number(cseq++) + "\r\n";
                describe += buildAuthHeader(cseq - 1);
                describe += "User-Agent: " + userAgent.toUtf8() + "\r\n";
                describe += "\r\n";
                queueRtsp(describe);
            }

            queueRtsp(buildSetup(cseq++, "JPEG", "0-1"));
            m_playbackWsH264SetupCseq = cseq;
            queueRtsp(buildSetup(cseq++, "H264", "2-3"));
            queueRtsp(buildSetup(cseq++, "G726-16", "8-9"));
            queueRtsp(buildSetup(cseq++, "G726-24", "10-11"));
            queueRtsp(buildSetup(cseq++, "G726-32", "12-13"));

            m_playbackWsFinalSetupCseq = cseq - 1;
            m_playbackWsNextCseq = cseq;
        } else {
            m_playbackWsActive = false;
            m_playbackWsPlaySent = false;
            m_playbackWsPaused = false;
            m_playbackWsSession.clear();
            m_playbackWsH264SetupCseq = -1;
            m_playbackSps.clear();
            m_playbackPps.clear();
            m_playbackFuBuffer.clear();
            m_playbackFuNalType = 0;
            m_playbackInterleavedBuffer.clear();
            m_playbackValidRtpCount = 0;
            streamingWsDisconnect();
        }
    });
}

// 입력 RTSP IP 검증 후 반영
bool Backend::updateRtspIp(QString ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    setRtspIp(trimmed);
    return true;
}

// RTSP IP/포트(선택적 URL 경로 포함) 설정 갱신
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

    // 설정 팝업 평문 호스트/IP/포트 입력 시
    // .env 경로 정책 유지(런타임 오버라이드 초기화)
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

// RTSP 설정 .env 기본값 복원
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

// RTSP 인증 정보 런타임 오버라이드 설정
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

// RTSP 인증 정보 .env 값 복원
void Backend::useEnvRtspCredentials() {
    m_useCustomRtspAuth = false;
    m_rtspUsernameOverride.clear();
    m_rtspPasswordOverride.clear();
}

// RTSP TCP 포트 연결 가능 여부 고속 확인
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

