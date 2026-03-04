#include "Backend.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

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
    if (digestUri.port() == 554) {
        digestUri.setPort(-1);
    }
    const QString digestUriText = digestUri.toString();

    bool portOk = false;
    const int rtspPort = m_env.value("SUNAPI_RTSP_PORT", "554").trimmed().toInt(&portOk);
    const int port = portOk ? rtspPort : 554;

    QTcpSocket socket;
    // 먼저 RTSP OPTIONS로 Digest challenge(realm/nonce)만 획득
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
                // 카메라가 명령 폭주를 받지 않도록 요청 간격을 단계적으로 증가
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
            // WS 비활성 모드에서는 일반 RTSP URL 재생으로 폴백
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
