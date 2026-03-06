#include "Backend.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

void Backend::requestPlaybackExportViaWs(int channelIndex,
                                         const QString &dateText,
                                         const QString &startTimeText,
                                         const QString &endTimeText,
                                         const QString &savePath) {
    // 취소 요청이 먼저 들어온 경우 초기화만 수행
    if (m_playbackExportCancelRequested) {
        m_playbackExportInProgress = false;
        return;
    }
    m_playbackExportInProgress = true;

    const int startSec = sunapiExportParseHmsToSec(startTimeText);
    const int endSec = sunapiExportParseHmsToSec(endTimeText);
    if (startSec < 0 || endSec < 0 || endSec < startSec) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed("내보내기 실패: 시작/종료 시간 형식 오류");
        return;
    }
    const int durationSec = qMax(1, (endSec - startSec) + 1);

    bool wantsAvi = false;
    QString outPath;
    QString finalOutPath;
    QString prepError;
    if (!buildPlaybackExportWsOutputPath(savePath, &wantsAvi, &outPath, &finalOutPath, &prepError)) {
        m_playbackExportInProgress = false;
        emit playbackExportFailed(prepError);
        return;
    }

    QNetworkRequest sessionReq = makeApiJsonRequest("/api/sunapi/export/session", {
        {"channel", QString::number(channelIndex)},
        {"date", dateText.trimmed()},
        {"start_time", startTimeText.trimmed()},
        {"end_time", endTimeText.trimmed()},
        {"rtsp_port", m_env.value("SUNAPI_RTSP_PORT", "554").trimmed()}
    });
    applyAuthIfNeeded(sessionReq);
    sessionReq.setRawHeader("Accept", "application/json");
    sessionReq.setRawHeader("X-Secure-Session", "Normal");

    QNetworkReply *sessionReply = m_manager->get(sessionReq);
    attachIgnoreSslErrors(sessionReply, "SUNAPI_EXPORT_WS_SESSION");

    connect(sessionReply, &QNetworkReply::finished, this, [=]() {
        if (sessionReply->error() != QNetworkReply::NoError) {
            const QString e = sessionReply->errorString();
            sessionReply->deleteLater();
            m_playbackExportInProgress = false;
            emit playbackExportFailed(QString("내보내기 실패: 세션 요청 실패 (%1)").arg(e));
            return;
        }

        QString rtspUri;
        QString realm;
        QString nonce;
        QString digestResponse;
        QString wsUser;
        QString wsPathFromSession;
        const QByteArray body = sessionReply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            rtspUri = obj.value("Uri").toString().trimmed();
            if (rtspUri.isEmpty()) rtspUri = obj.value("uri").toString().trimmed();
            realm = obj.value("Realm").toString().trimmed();
            if (realm.isEmpty()) realm = obj.value("realm").toString().trimmed();
            nonce = obj.value("Nonce").toString().trimmed();
            if (nonce.isEmpty()) nonce = obj.value("nonce").toString().trimmed();
            digestResponse = obj.value("Response").toString().trimmed();
            if (digestResponse.isEmpty()) digestResponse = obj.value("response").toString().trimmed();
            wsUser = obj.value("Username").toString().trimmed();
            if (wsUser.isEmpty()) wsUser = obj.value("username").toString().trimmed();
            wsPathFromSession = obj.value("WsPath").toString().trimmed();
            if (wsPathFromSession.isEmpty()) wsPathFromSession = obj.value("ws_path").toString().trimmed();
        }
        sessionReply->deleteLater();
        if (rtspUri.isEmpty()) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed("내보내기 실패: RTSP URI 누락");
            return;
        }
        if (realm.isEmpty() || nonce.isEmpty()) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed("내보내기 실패: digest challenge 누락");
            return;
        }
        if (digestResponse.isEmpty()) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed("내보내기 실패: digest response 누락");
            return;
        }
        if (wsUser.isEmpty()) {
            m_playbackExportInProgress = false;
            emit playbackExportFailed("내보내기 실패: digest 사용자 정보 누락");
            return;
        }

        emit playbackExportProgress(3, "내보내기 세션 연결 준비");

        struct WsExportState {
            QPointer<QWebSocket> ws;
            QPointer<QTimer> keepAliveTimer;
            QPointer<QTimer> hardTimeoutTimer;
            QFile outFile;
            QString authHeader;
            QString uri;
            QString session;
            int nextCseq = 1;
            int setupDoneCount = 0;
            int setupExpected = 9;
            int h264RtpChannel = 2;
            QByteArray interleavedBuf;
            QByteArray fuBuffer;
            int fuNalType = 0;
            bool playSent = false;
            bool playAck = false;
            bool teardownSent = false;
            bool finished = false;
            bool gotRtp = false;
            quint32 firstTs = 0;
            quint32 lastTs = 0;
            qint64 targetTsDelta = 0;
            qint64 writtenBytes = 0;
            qint64 startMs = 0;
            qint64 lastRtpMs = 0;
            int lastProgress = 0;
            qint64 lastProgressMs = 0;
            QString outPath;
            QString finalOutPath;
            bool needsAviRemux = false;
            QHash<int, QString> setupCseqTrack;
            QHash<QString, QByteArray> trackInterleaved;
        };

        auto st = std::make_shared<WsExportState>();
        st->uri = rtspUri;
        st->authHeader = QString("Authorization: Digest username=\"%1\", realm=\"%2\", uri=\"%3\", nonce=\"%4\", response=\"%5\"")
                .arg(wsUser, realm, rtspUri, nonce, digestResponse);
        st->targetTsDelta = static_cast<qint64>(durationSec) * 90000LL;
        st->outPath = outPath;
        st->finalOutPath = finalOutPath;
        st->needsAviRemux = wantsAvi;
        st->outFile.setFileName(outPath);
        if (!st->outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit playbackExportFailed(QString("내보내기 실패: 파일 열기 실패 (%1)").arg(st->outFile.errorString()));
            return;
        }

        auto sendRtsp = [this, st](const QByteArray &rtspText) {
            if (!st->ws || st->ws->state() != QAbstractSocket::ConnectedState) {
                return;
            }
            st->ws->sendBinaryMessage(rtspText);
        };

        auto finishWith = [this, st](bool ok, const QString &message) {
            if (st->finished) {
                return;
            }
            st->finished = true;
            m_playbackExportInProgress = false;

            if (st->keepAliveTimer) {
                st->keepAliveTimer->stop();
                st->keepAliveTimer->deleteLater();
                st->keepAliveTimer = nullptr;
            }
            if (st->hardTimeoutTimer) {
                st->hardTimeoutTimer->stop();
                st->hardTimeoutTimer->deleteLater();
                st->hardTimeoutTimer = nullptr;
            }

            if (st->ws && st->ws->state() == QAbstractSocket::ConnectedState
                && !st->teardownSent && !st->session.isEmpty()) {
                // 완료/실패 경로 공통으로 RTSP 세션 정리 시도
                QByteArray td;
                td += "TEARDOWN " + st->uri.toUtf8() + " RTSP/1.0\r\n";
                td += "CSeq: " + QByteArray::number(st->nextCseq++) + "\r\n";
                if (!st->authHeader.isEmpty()) {
                    td += st->authHeader.toUtf8() + "\r\n";
                }
                td += "User-Agent: UWC[undefined]\r\n";
                td += "Session: " + st->session.toUtf8() + "\r\n";
                td += "\r\n";
                st->ws->sendBinaryMessage(td);
                st->teardownSent = true;
            }

            if (st->ws) {
                st->ws->close();
                st->ws->deleteLater();
                st->ws = nullptr;
            }
            m_playbackExportWs = nullptr;

            st->outFile.flush();
            st->outFile.close();

            if (ok) {
                if (st->needsAviRemux) {
                    // 장비가 raw H264만 내려주는 경우 ffmpeg로 AVI remux
                    emit playbackExportProgress(99, "AVI 변환 중");
                    const QString ffmpegBin = resolvePlaybackExportFfmpegBinary();
                    qInfo() << "[SUNAPI][EXPORT][WS] ffmpeg path=" << ffmpegBin;
                    QProcess ff;
                    QStringList args;
                    args << "-y"
                         << "-loglevel" << "error"
                         << "-f" << "h264"
                         << "-i" << st->outPath
                         << "-c:v" << "copy"
                         << "-an"
                         << st->finalOutPath;
                    ff.start(ffmpegBin, args);
                    const bool started = ff.waitForStarted(3000);
                    const bool done = started && ff.waitForFinished(120000);
                    if (!started || !done || ff.exitStatus() != QProcess::NormalExit || ff.exitCode() != 0) {
                        const QString stderrText = QString::fromUtf8(ff.readAllStandardError()).trimmed();
                        QFile::remove(st->finalOutPath);
                        emit playbackExportFailed(QString("내보내기 실패: AVI 변환 실패 (%1)")
                                                      .arg(stderrText.isEmpty() ? "ffmpeg 실행 오류/미설치" : stderrText));
                        return;
                    }
                    QFile::remove(st->outPath);
                }
                emit playbackExportProgress(100, "내보내기 완료");
                emit playbackExportFinished(st->needsAviRemux ? st->finalOutPath : st->outPath);
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
            } else {
                // 실패 시 부분 파일 정리
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                emit playbackExportFailed(message);
            }
        };

        auto parseRtspResponse = [this, st, sendRtsp, finishWith](const QString &text) {
            if (m_playbackExportCancelRequested) {
                finishWith(false, "내보내기 취소됨");
                return;
            }
            playbackExportHandleRtspResponse(
                text,
                st->setupCseqTrack,
                st->setupDoneCount,
                st->trackInterleaved,
                st->h264RtpChannel,
                st->setupExpected,
                st->playSent,
                st->playAck,
                st->session,
                st->nextCseq,
                st->authHeader,
                st->uri,
                sendRtsp,
                [finishWith](const QString &err) { finishWith(false, err); });
        };
        auto processInterleaved = [this, st, finishWith](const QByteArray &bytes) {
            if (m_playbackExportCancelRequested) {
                finishWith(false, "내보내기 취소됨");
                return;
            }
            if (playbackExportConsumeInterleaved(bytes,
                                                 st->h264RtpChannel,
                                                 st->interleavedBuf,
                                                 st->outFile,
                                                 st->writtenBytes,
                                                 st->fuBuffer,
                                                 st->fuNalType,
                                                 st->gotRtp,
                                                 st->lastRtpMs,
                                                 st->firstTs,
                                                 st->lastTs,
                                                 st->targetTsDelta,
                                                 st->lastProgress,
                                                 st->lastProgressMs)) {
                finishWith(true, QString());
            }
        };

        st->ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        st->keepAliveTimer = new QTimer(st->ws);
        st->keepAliveTimer->setInterval(15000);
        st->hardTimeoutTimer = new QTimer(st->ws);
        st->hardTimeoutTimer->setSingleShot(true);
        st->hardTimeoutTimer->setInterval(qMax(120000, durationSec * 2000));

        connect(st->hardTimeoutTimer, &QTimer::timeout, this, [this, finishWith, st]() {
            if (m_playbackExportCancelRequested) {
                finishWith(false, "내보내기 취소됨");
                return;
            }
            // 데이터가 일정 시간 멈춘 경우 타임아웃 처리
            if (!st->gotRtp || st->writtenBytes <= 0) {
                finishWith(false, "내보내기 실패: 데이터 수신 시간 초과");
            } else {
                finishWith(true, QString());
            }
        });

        connect(st->keepAliveTimer, &QTimer::timeout, this, [this, st, sendRtsp, finishWith, durationSec]() {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (st->gotRtp && st->writtenBytes > 0) {
                const qint64 idleMs = nowMs - st->lastRtpMs;
                const qint64 runMs = nowMs - st->startMs;
                const qint64 expectedMs = qMax<qint64>(30000, static_cast<qint64>(durationSec) * 1000 + 10000);
                if ((st->lastProgress >= 97 && idleMs >= 3000) || (idleMs >= 10000 && runMs >= expectedMs)) {
                    // 진행률 97% 이상에서 데이터 정지 시 정상 완료로 간주
                    finishWith(true, QString());
                    return;
                }
            }

            if (!st->ws || st->ws->state() != QAbstractSocket::ConnectedState || st->session.isEmpty()) {
                return;
            }
            QByteArray req = buildPlaybackExportRtspRequest(st->nextCseq,
                                                            st->authHeader,
                                                            st->session,
                                                            "GET_PARAMETER",
                                                            st->uri.toUtf8(),
                                                            true);
            req += "Content-Length: 0\r\n\r\n";
            sendRtsp(req);
        });

        connect(st->ws, &QWebSocket::connected, this, [this, st, sendRtsp]() {
            // OPTIONS 2회 후 track별 SETUP 순차 전송
            QByteArray options1;
            options1 += "OPTIONS " + st->uri.toUtf8() + " RTSP/1.0\r\n";
            options1 += "CSeq: " + QByteArray::number(st->nextCseq++) + "\r\n";
            options1 += "User-Agent: UWC[undefined]\r\n";
            options1 += "\r\n";
            sendRtsp(options1);

            QByteArray options2 = buildPlaybackExportRtspRequest(st->nextCseq,
                                                                 st->authHeader,
                                                                 st->session,
                                                                 "OPTIONS",
                                                                 st->uri.toUtf8(),
                                                                 false);
            options2 += "\r\n";
            sendRtsp(options2);

            const QStringList tracks = {"JPEG", "H264", "H265", "PCMU", "G726-16", "G726-24", "G726-32", "G726-40", "aac-16"};
            int interleave = 0;
            for (const QString &track : tracks) {
                const int cseq = st->nextCseq;
                st->setupCseqTrack.insert(cseq, track);
                QByteArray setup = buildPlaybackExportRtspRequest(st->nextCseq,
                                                                  st->authHeader,
                                                                  st->session,
                                                                  "SETUP",
                                                                  (st->uri + "/trackID=" + track).toUtf8(),
                                                                  false);
                setup += "Transport: RTP/AVP/TCP;unicast;interleaved="
                        + QByteArray::number(interleave)
                        + "-"
                        + QByteArray::number(interleave + 1)
                        + "\r\n\r\n";
                sendRtsp(setup);
                interleave += 2;
            }
        });

        connect(st->ws, &QWebSocket::binaryMessageReceived, this, [parseRtspResponse, processInterleaved](const QByteArray &payload) {
            // RTSP 제어 응답과 interleaved RTP 데이터를 분기 처리
            if (payload.startsWith("RTSP/1.0")) {
                parseRtspResponse(QString::fromUtf8(payload));
                return;
            }
            processInterleaved(payload);
        });

        connect(st->ws, &QWebSocket::errorOccurred, this, [this, finishWith, st](QAbstractSocket::SocketError) {
            if (m_playbackExportCancelRequested) {
                // 취소 중 에러는 실패 알림 없이 파일 정리만 수행
                st->finished = true;
                m_playbackExportWs = nullptr;
                if (st->outFile.isOpen()) {
                    st->outFile.flush();
                    st->outFile.close();
                }
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                return;
            }
            if (!st->finished) {
                finishWith(false, QString("내보내기 실패: websocket 오류 (%1)").arg(st->ws ? st->ws->errorString() : QString("unknown")));
            }
        });

        connect(st->ws, &QWebSocket::disconnected, this, [this, finishWith, st]() {
            if (m_playbackExportCancelRequested) {
                // 취소 중 disconnect는 정상 해제 경로로 처리
                st->finished = true;
                m_playbackExportWs = nullptr;
                if (st->outFile.isOpen()) {
                    st->outFile.flush();
                    st->outFile.close();
                }
                QFile::remove(st->outPath);
                if (!st->finalOutPath.isEmpty() && st->finalOutPath != st->outPath) {
                    QFile::remove(st->finalOutPath);
                }
                m_playbackExportOutPath.clear();
                m_playbackExportFinalPath.clear();
                return;
            }
            if (!st->finished) {
                if (st->writtenBytes > 0) {
                    finishWith(true, QString());
                } else {
                    finishWith(false, "내보내기 실패: 연결 종료(수신 데이터 없음)");
                }
            }
        });

        st->startMs = QDateTime::currentMSecsSinceEpoch();
        st->hardTimeoutTimer->start();
        st->keepAliveTimer->start();
        emit playbackExportProgress(5, "내보내기 WebSocket 연결 시작");
        m_playbackExportWs = st->ws;
        const QUrl apiBase(serverUrl());
        const QString apiScheme = apiBase.scheme().trimmed().toLower();
        const QString wsScheme = (apiScheme == "https") ? QStringLiteral("wss") : QStringLiteral("ws");
        const int defaultPort = (apiScheme == "https") ? 443 : 80;
        const int httpPort = apiBase.port(defaultPort);

        if (wsScheme == "wss" && m_sslConfigReady) {
            st->ws->setSslConfiguration(m_sslConfig);
            connect(st->ws, &QWebSocket::sslErrors, this, [this, st](const QList<QSslError> &errors) {
                for (const auto &err : errors) {
                    qWarning() << "[SUNAPI_EXPORT_WS][SSL]" << err.errorString();
                }
                if (m_sslIgnoreErrors && st && st->ws) {
                    st->ws->ignoreSslErrors();
                }
            });
        }

        QUrl wsUrl;
        wsUrl.setScheme(wsScheme);
        wsUrl.setHost(apiBase.host());
        if (httpPort > 0) {
            wsUrl.setPort(httpPort);
        }
        QString wsPath = m_env.value("SUNAPI_STREAMING_WS_PATH").trimmed();
        if (wsPath.isEmpty()) {
            wsPath = wsPathFromSession;
        }
        if (wsPath.isEmpty()) {
            wsPath = "/StreamingServer";
        } else if (!wsPath.startsWith('/')) {
            wsPath.prepend('/');
        }
        wsUrl.setPath(wsPath);
        st->ws->open(wsUrl);
    });
}
