#include "Backend.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

void Backend::preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText) {
    const QString date = dateText.trimmed();
    const QString time = timeText.trimmed();
    if (channelIndex < 0 || date.isEmpty() || time.isEmpty()) {
        emit playbackPrepareFailed("Playback URL 생성 실패: 채널/날짜/시간 형식을 확인해 주세요.");
        return;
    }

    QNetworkRequest sessionReq = makeApiJsonRequest("/api/sunapi/playback/session", {
        {"channel", QString::number(channelIndex)},
        {"date", date},
        {"time", time},
        {"rtsp_port", m_env.value("SUNAPI_RTSP_PORT", "554").trimmed()}
    });
    applyAuthIfNeeded(sessionReq);
    sessionReq.setRawHeader("Accept", "application/json");
    sessionReq.setRawHeader("X-Secure-Session", "Normal");

    QNetworkReply *sessionReply = m_manager->get(sessionReq);
    attachIgnoreSslErrors(sessionReply, "SUNAPI_PLAYBACK_SESSION");

    connect(sessionReply, &QNetworkReply::finished, this, [this, sessionReply]() {
        if (sessionReply->error() != QNetworkReply::NoError) {
            const QString err = QString("Playback 준비 실패: %1").arg(sessionReply->errorString());
            sessionReply->deleteLater();
            emit playbackPrepareFailed(err);
            return;
        }

        QString digestUriText;
        QString realm;
        QString nonce;
        QString wsDigestResponse;
        QString wsUser;

        const QJsonDocument doc = QJsonDocument::fromJson(sessionReply->readAll());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            digestUriText = obj.value("Uri").toString().trimmed();
            if (digestUriText.isEmpty()) digestUriText = obj.value("uri").toString().trimmed();
            realm = obj.value("Realm").toString().trimmed();
            if (realm.isEmpty()) realm = obj.value("realm").toString().trimmed();
            nonce = obj.value("Nonce").toString().trimmed();
            if (nonce.isEmpty()) nonce = obj.value("nonce").toString().trimmed();
            wsDigestResponse = obj.value("Response").toString().trimmed();
            if (wsDigestResponse.isEmpty()) wsDigestResponse = obj.value("response").toString().trimmed();
            wsUser = obj.value("Username").toString().trimmed();
            if (wsUser.isEmpty()) wsUser = obj.value("username").toString().trimmed();
        }
        sessionReply->deleteLater();

        if (wsDigestResponse.isEmpty()) {
            wsDigestResponse = m_env.value("PLAYBACK_WS_DIGEST_RESPONSE").trimmed();
        }

        if (digestUriText.isEmpty()) {
            emit playbackPrepareFailed("Playback 준비 실패: RTSP URI 누락");
            return;
        }

        const QString autoWs = m_env.value("PLAYBACK_WS_AUTO_CONNECT", "1").trimmed().toLower();
        const bool wsEnabled = (autoWs == "1" || autoWs == "true" || autoWs == "on");
        if (!wsEnabled) {
            emit playbackPrepared(digestUriText);
        }

        if (wsEnabled) {
            if (realm.isEmpty() || nonce.isEmpty()) {
                emit playbackPrepareFailed("Playback WS 준비 실패: Digest challenge 값이 비어 있습니다.");
                return;
            }
            if (wsDigestResponse.isEmpty()) {
                emit playbackPrepareFailed("Playback WS 준비 실패: digest Response 값이 비어 있습니다.");
                return;
            }
            if (wsUser.isEmpty()) {
                emit playbackPrepareFailed("Playback WS 준비 실패: digest 사용자 정보가 비어 있습니다.");
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
                    .arg(wsUser, realm, digestUriText, nonce, wsDigestResponse);

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
