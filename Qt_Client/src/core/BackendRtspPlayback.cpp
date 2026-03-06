#include "Backend.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

void Backend::preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText) {
    const QString playbackUrl = buildPlaybackRtspUrl(channelIndex, dateText, timeText);
    if (playbackUrl.isEmpty()) {
        emit playbackPrepareFailed("Playback URL 생성 실패: 채널/날짜/시간 형식을 확인해 주세요.");
        return;
    }

    const QString sunapiHost = m_env.value("SUNAPI_IP").trimmed();
    if (sunapiHost.isEmpty()) {
        emit playbackPrepareFailed("Playback 준비 실패: SUNAPI_IP가 비어 있습니다.");
        return;
    }

    QUrl digestUri(playbackUrl);
    digestUri.setUserName(QString());
    digestUri.setPassword(QString());
    if (digestUri.port() == 554) {
        digestUri.setPort(-1);
    }
    const QString digestUriText = digestUri.toString();

    const QString sunapiScheme = m_env.value("SUNAPI_SCHEME", "https").trimmed().toLower();
    const QString httpScheme = (sunapiScheme == "https") ? QStringLiteral("https") : QStringLiteral("http");
    const int defaultPort = (httpScheme == "https") ? 443 : 80;
    const int httpPort = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();
    QUrl step1Url;
    step1Url.setScheme(httpScheme);
    step1Url.setHost(sunapiHost);
    if (httpPort > 0) {
        step1Url.setPort(httpPort);
    }
    step1Url.setPath("/api/sunapi/playback/challenge");
    QUrlQuery q1;
    q1.addQueryItem("uri", digestUriText);
    q1.addQueryItem("rtsp_port", m_env.value("SUNAPI_RTSP_PORT", "554").trimmed());
    step1Url.setQuery(q1);

    QNetworkRequest req1(step1Url);
    applySslIfNeeded(req1);
    req1.setRawHeader("Accept", "application/json");
    QNetworkReply *reply1 = m_manager->get(req1);
    attachIgnoreSslErrors(reply1, "SUNAPI_PLAYBACK_CHALLENGE");

    connect(reply1, &QNetworkReply::finished, this, [this, reply1, playbackUrl, digestUriText, sunapiHost, httpScheme, httpPort]() {
        if (reply1->error() != QNetworkReply::NoError) {
            const QString err = QString("Playback 준비 1단계 실패: %1").arg(reply1->errorString());
            reply1->deleteLater();
            emit playbackPrepareFailed(err);
            return;
        }

        QString realm;
        QString nonce;
        const QByteArray challengeBody = reply1->readAll();
        const QJsonDocument challengeDoc = QJsonDocument::fromJson(challengeBody);
        if (challengeDoc.isObject()) {
            const QJsonObject obj = challengeDoc.object();
            realm = obj.value("Realm").toString().trimmed();
            if (realm.isEmpty()) realm = obj.value("realm").toString().trimmed();
            nonce = obj.value("Nonce").toString().trimmed();
            if (nonce.isEmpty()) nonce = obj.value("nonce").toString().trimmed();
        }
        reply1->deleteLater();
        if (realm.isEmpty() || nonce.isEmpty()) {
            emit playbackPrepareFailed("Playback 준비 1단계 실패: RTSP Digest realm/nonce 누락");
            return;
        }

        const QString cnonce = QString::number(QRandomGenerator::global()->generate(), 16).left(8).toUpper();
        QUrl step2Url;
        step2Url.setScheme(httpScheme);
        // Qt는 digestauth 쿼리를 직접 stw-cgi로 보내지 않고 Crow 고정 API를 사용한다.
        step2Url.setHost(sunapiHost);
        if (httpPort > 0) {
            step2Url.setPort(httpPort);
        }
        step2Url.setPath("/api/sunapi/playback/digestauth");
        QUrlQuery q2;
        q2.addQueryItem("method", "OPTIONS");
        q2.addQueryItem("realm", realm);
        q2.addQueryItem("nonce", nonce);
        q2.addQueryItem("uri", digestUriText);
        q2.addQueryItem("nc", "00000001");
        q2.addQueryItem("cnonce", cnonce);
        step2Url.setQuery(q2);

        QNetworkRequest req2(step2Url);
        applySslIfNeeded(req2);
        req2.setRawHeader("Accept", "application/json");
        req2.setRawHeader("X-Secure-Session", "Normal");
        QNetworkReply *reply2 = m_manager->get(req2);
        connect(reply2, &QNetworkReply::finished, this, [this, reply2, playbackUrl, digestUriText, realm, nonce]() {
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

        const QString wsUser = QString::fromUtf8(reply2->rawHeader("X-SUNAPI-USER")).trimmed();

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
    });
}
