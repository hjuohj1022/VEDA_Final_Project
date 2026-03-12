#include "internal/rtsp/BackendRtspPlaybackService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

void BackendRtspPlaybackService::preparePlaybackRtsp(Backend *backend,
                                                     BackendPrivate *state,
                                                     int channelIndex,
                                                     const QString &dateText,
                                                     const QString &timeText)
{
    const QString date = dateText.trimmed();
    const QString time = timeText.trimmed();
    if (channelIndex < 0 || date.isEmpty() || time.isEmpty()) {
        emit backend->playbackPrepareFailed("Playback URL ?앹꽦 ?ㅽ뙣: 梨꾨꼸/?좎쭨/?쒓컙 ?뺤떇???뺤씤??二쇱꽭??");
        return;
    }

    QNetworkRequest sessionReq = backend->makeApiJsonRequest("/api/sunapi/playback/session", {
        {"channel", QString::number(channelIndex)},
        {"date", date},
        {"time", time},
        {"rtsp_port", state->m_env.value("SUNAPI_RTSP_PORT", "554").trimmed()}
    });
    backend->applyAuthIfNeeded(sessionReq);
    sessionReq.setRawHeader("Accept", "application/json");
    sessionReq.setRawHeader("X-Secure-Session", "Normal");

    QNetworkReply *sessionReply = state->m_manager->get(sessionReq);
    backend->attachIgnoreSslErrors(sessionReply, "SUNAPI_PLAYBACK_SESSION");

    QObject::connect(sessionReply, &QNetworkReply::finished, backend, [backend, state, sessionReply]() {
        if (sessionReply->error() != QNetworkReply::NoError) {
            const QString err = QString("Playback 以鍮??ㅽ뙣: %1").arg(sessionReply->errorString());
            sessionReply->deleteLater();
            emit backend->playbackPrepareFailed(err);
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
            wsDigestResponse = state->m_env.value("PLAYBACK_WS_DIGEST_RESPONSE").trimmed();
        }

        if (digestUriText.isEmpty()) {
            emit backend->playbackPrepareFailed("Playback 以鍮??ㅽ뙣: RTSP URI ?꾨씫");
            return;
        }

        const QString autoWs = state->m_env.value("PLAYBACK_WS_AUTO_CONNECT", "1").trimmed().toLower();
        const bool wsEnabled = (autoWs == "1" || autoWs == "true" || autoWs == "on");
        if (!wsEnabled) {
            emit backend->playbackPrepared(digestUriText);
        }

        if (wsEnabled) {
            if (realm.isEmpty() || nonce.isEmpty()) {
                emit backend->playbackPrepareFailed("Playback WS 以鍮??ㅽ뙣: Digest challenge 媛믪씠 鍮꾩뼱 ?덉뒿?덈떎.");
                return;
            }
            if (wsDigestResponse.isEmpty()) {
                emit backend->playbackPrepareFailed("Playback WS 以鍮??ㅽ뙣: digest Response 媛믪씠 鍮꾩뼱 ?덉뒿?덈떎.");
                return;
            }
            if (wsUser.isEmpty()) {
                emit backend->playbackPrepareFailed("Playback WS 以鍮??ㅽ뙣: digest ?ъ슜???뺣낫媛 鍮꾩뼱 ?덉뒿?덈떎.");
                return;
            }

            state->m_playbackWsActive = true;
            state->m_playbackWsPlaySent = false;
            state->m_playbackWsPaused = false;
            state->m_playbackWsSession.clear();
            state->m_playbackWsH264SetupCseq = -1;
            state->m_playbackRtpPayloadType = 96;
            state->m_playbackRtpVideoChannel = 2;
            state->m_playbackRtpVideoAltChannel = 3;
            state->m_playbackWsSdpPublished = false;
            state->m_playbackSps.clear();
            state->m_playbackPps.clear();
            state->m_playbackFuBuffer.clear();
            state->m_playbackFuNalType = 0;
            state->m_playbackInterleavedBuffer.clear();
            state->m_playbackValidRtpCount = 0;
            state->m_playbackWsUri = digestUriText;
            state->m_playbackWsAuthHeader =
                    QString("Authorization: Digest username=\"%1\", realm=\"%2\", uri=\"%3\", nonce=\"%4\", response=\"%5\"")
                    .arg(wsUser, realm, digestUriText, nonce, wsDigestResponse);

            backend->streamingWsConnect();

            int delayMs = 120;
            auto queueRtsp = [backend, state, &delayMs](const QByteArray &request) {
                const QString hex = QString::fromLatin1(request.toHex().toUpper());
                QTimer::singleShot(delayMs, backend, [backend, state, hex]() {
                    if (state->m_streamingWs && state->m_streamingWs->state() == QAbstractSocket::ConnectedState) {
                        backend->streamingWsSendHex(hex);
                    }
                });
                delayMs += 80;
            };

            const QString userAgent = QStringLiteral("UWC[undefined]");
            auto buildAuthHeader = [&](int cseq) -> QByteArray {
                Q_UNUSED(cseq);
                QByteArray auth;
                auth += state->m_playbackWsAuthHeader.toUtf8();
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

            const QString sendDescribe = state->m_env.value("PLAYBACK_WS_SEND_DESCRIBE", "0").trimmed().toLower();
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
            state->m_playbackWsH264SetupCseq = cseq;
            queueRtsp(buildSetup(cseq++, "H264", "2-3"));
            queueRtsp(buildSetup(cseq++, "G726-16", "8-9"));
            queueRtsp(buildSetup(cseq++, "G726-24", "10-11"));
            queueRtsp(buildSetup(cseq++, "G726-32", "12-13"));

            state->m_playbackWsFinalSetupCseq = cseq - 1;
            state->m_playbackWsNextCseq = cseq;
        } else {
            state->m_playbackWsActive = false;
            state->m_playbackWsPlaySent = false;
            state->m_playbackWsPaused = false;
            state->m_playbackWsSession.clear();
            state->m_playbackWsH264SetupCseq = -1;
            state->m_playbackSps.clear();
            state->m_playbackPps.clear();
            state->m_playbackFuBuffer.clear();
            state->m_playbackFuNalType = 0;
            state->m_playbackInterleavedBuffer.clear();
            state->m_playbackValidRtpCount = 0;
            backend->streamingWsDisconnect();
        }
    });
}

