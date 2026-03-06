#include "Backend.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

void Backend::requestPlaybackExport(int channelIndex,
                                    const QString &dateText,
                                    const QString &startTimeText,
                                    const QString &endTimeText,
                                    const QString &savePath) {
    // 입력 형식 검증
    if (channelIndex < 0) {
        emit playbackExportFailed("내보내기 실패: 유효하지 않은 채널 인덱스");
        return;
    }

    const QRegularExpression dateRe("^\\d{4}-\\d{2}-\\d{2}$");
    if (!dateRe.match(dateText.trimmed()).hasMatch()) {
        emit playbackExportFailed("내보내기 실패: 날짜 형식 오류 (YYYY-MM-DD)");
        return;
    }

    const int startSec = sunapiExportParseHmsToSec(startTimeText);
    const int endSec = sunapiExportParseHmsToSec(endTimeText);
    if (startSec < 0 || endSec < 0) {
        emit playbackExportFailed("내보내기 실패: 시간 형식 오류 (HH:MM:SS)");
        return;
    }
    if (startSec > endSec) {
        emit playbackExportFailed("내보내기 실패: 시작 시간이 종료 시간보다 늦음");
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit playbackExportFailed("내보내기 실패: SUNAPI_IP 비어 있음");
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QString outPath = savePath.trimmed();
    if (outPath.startsWith("file:", Qt::CaseInsensitive)) {
        outPath = QUrl(outPath).toLocalFile();
    }
    if (outPath.isEmpty()) {
        emit playbackExportFailed("내보내기 실패: 저장 경로 비어 있음");
        return;
    }

    const QString format = m_env.value("SUNAPI_EXPORT_TYPE", "AVI").trimmed().toUpper();
    const QString startDt = QString("%1 %2").arg(dateText.trimmed(), startTimeText.trimmed());
    const QString endDt = QString("%1 %2").arg(dateText.trimmed(), endTimeText.trimmed());

    struct Candidate {
        QString cgi;
        QString submenu;
        QString action;
        QString extraQuery;
    };

    QList<Candidate> createCandidates;
    // 장비별 export CGI 호환성 차이를 고려해 후보 순차 시도
    createCandidates.push_back({
        m_env.value("SUNAPI_EXPORT_CREATE_CGI", "recording.cgi").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_SUBMENU", "export").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_ACTION", "create").trimmed(),
        m_env.value("SUNAPI_EXPORT_CREATE_QUERY").trimmed()
    });
    createCandidates.push_back({"recording.cgi", "backup", "create", ""});
    createCandidates.push_back({"recording.cgi", "export", "start", ""});

    auto buildUrl = [scheme, host, port](const QString &cgi,
                                         const QString &submenu,
                                         const QString &action,
                                         const QMap<QString, QString> &params,
                                         const QString &extraQuery = QString()) {
        QUrl url;
        url.setScheme(scheme);
        url.setHost(host);
        if (port > 0) url.setPort(port);
        url.setPath(QString("/sunapi/stw-cgi/%1").arg(cgi));
        QUrlQuery q;
        q.addQueryItem("msubmenu", submenu);
        q.addQueryItem("action", action);
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            q.addQueryItem(it.key(), it.value());
        }
        const QString eq = extraQuery.trimmed();
        if (!eq.isEmpty()) {
            const QStringList pairs = eq.split('&', Qt::SkipEmptyParts);
            for (const QString &pair : pairs) {
                const int sep = pair.indexOf('=');
                if (sep > 0) q.addQueryItem(pair.left(sep), pair.mid(sep + 1));
                else q.addQueryItem(pair, QString());
            }
        }
        url.setQuery(q);
        return url;
    };

    m_playbackExportCancelRequested = false;
    m_playbackExportInProgress = true;
    m_playbackExportOutPath = outPath;
    m_playbackExportFinalPath = outPath;
    emit playbackExportStarted("내보내기 요청 시작");
    auto startFfmpegBackup = [this, channelIndex, dateText, startTimeText, endTimeText, outPath](const std::function<void()> &onFailedFallback) -> bool {
        return startPlaybackExportViaFfmpegBackup(channelIndex,
                                                  dateText,
                                                  startTimeText,
                                                  endTimeText,
                                                  outPath,
                                                  onFailedFallback);
    };

    const auto lastCreateReason = std::make_shared<QString>();
    auto tryCreate = std::make_shared<std::function<void(int)>>();
    *tryCreate = [=](int idx) {
        if (m_playbackExportCancelRequested) {
            m_playbackExportInProgress = false;
            return;
        }
        if (idx < 0 || idx >= createCandidates.size()) {
            // 608(Not Supported)인 경우 RTSP-over-WS 경로로 폴백
            if (lastCreateReason->contains("Error Code: 608", Qt::CaseInsensitive)) {
                qInfo() << "[SUNAPI][EXPORT] fallback to RTSP-over-WS backup.smp path (Error 608)";
                const QString ffmpegBackupRaw = m_env.value("PLAYBACK_EXPORT_USE_FFMPEG_BACKUP", "0").trimmed().toLower();
                const bool useFfmpegBackup =
                        (ffmpegBackupRaw == "1" || ffmpegBackupRaw == "true" || ffmpegBackupRaw == "on");
                if (useFfmpegBackup) {
                    const bool started = startFfmpegBackup(
                        [this, channelIndex, dateText, startTimeText, endTimeText, outPath]() {
                            if (m_playbackExportCancelRequested) {
                                m_playbackExportInProgress = false;
                                return;
                            }
                            requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
                        });
                    if (started) {
                        return;
                    }
                }
                if (m_playbackExportCancelRequested) {
                    m_playbackExportInProgress = false;
                    return;
                }
                if (useFfmpegBackup) {
                    qWarning() << "[SUNAPI][EXPORT] ffmpeg backup unavailable -> direct WS fallback";
                } else {
                    qInfo() << "[SUNAPI][EXPORT] ffmpeg backup disabled -> direct WS fallback";
                }
                requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
            } else if (!lastCreateReason->trimmed().isEmpty()) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed(QString("내보내기 실패: create 엔드포인트 호환 실패 (%1)").arg(*lastCreateReason));
            } else {
                m_playbackExportInProgress = false;
                emit playbackExportFailed("내보내기 실패: create 엔드포인트 모두 실패");
            }
            return;
        }

        const Candidate c = createCandidates.at(idx);
        QMap<QString, QString> params;
        params.insert("Channel", QString::number(channelIndex));
        params.insert("ChannelIDList", QString::number(channelIndex));
        params.insert("StartTime", startDt);
        params.insert("EndTime", endDt);
        params.insert("FromDate", startDt);
        params.insert("ToDate", endDt);
        params.insert("Type", format);
        params.insert("FileType", format);

        const QUrl url = buildUrl(c.cgi, c.submenu, c.action, params, c.extraQuery);
        qInfo() << "[SUNAPI][EXPORT] create request url=" << url;
        QNetworkRequest req(url);
        applySslIfNeeded(req);
        QNetworkReply *reply = m_manager->get(req);
        attachIgnoreSslErrors(reply, "SUNAPI_EXPORT_CREATE");

        connect(reply, &QNetworkReply::finished, this, [=]() {
            const QByteArray body = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                *lastCreateReason = reply->errorString();
                reply->deleteLater();
                (*tryCreate)(idx + 1);
                return;
            }

            QString jobId;
            QString downloadUrlText;
            QString reason;
            if (!sunapiExportParseCreateReply(body, &jobId, &downloadUrlText, &reason)) {
                *lastCreateReason = reason;
                qWarning() << "[SUNAPI][EXPORT] create parse failed" << "url=" << url
                           << "reason=" << reason
                           << "body=" << QString::fromUtf8(body.left(180));
                reply->deleteLater();
                (*tryCreate)(idx + 1);
                return;
            }

            reply->deleteLater();

            if (!downloadUrlText.isEmpty()) {
                // create 응답에 다운로드 URL이 바로 있으면 즉시 다운로드
                QUrl dl = QUrl(downloadUrlText);
                if (dl.isRelative()) {
                    dl = url.resolved(dl);
                }
                playbackExportStartDownload(dl, outPath);
                return;
            }

            if (jobId.isEmpty()) {
                m_playbackExportInProgress = false;
                emit playbackExportFailed("내보내기 실패: JobID/다운로드 URL 없음");
                return;
            }
            emit playbackExportProgress(5, QString("내보내기 작업 생성 완료 (JobID=%1)").arg(jobId));
            const QString pollCgi = m_env.value("SUNAPI_EXPORT_POLL_CGI", c.cgi).trimmed();
            const QString pollSubmenu = m_env.value("SUNAPI_EXPORT_POLL_SUBMENU", c.submenu).trimmed().isEmpty()
                    ? c.submenu : m_env.value("SUNAPI_EXPORT_POLL_SUBMENU", c.submenu).trimmed();
            const QString pollAction = m_env.value("SUNAPI_EXPORT_POLL_ACTION", "status").trimmed();
            const QString pollExtra = m_env.value("SUNAPI_EXPORT_POLL_QUERY").trimmed();
            const int pollMs = qMax(500, m_env.value("SUNAPI_EXPORT_POLL_INTERVAL_MS", "1500").toInt());
            const int timeoutMs = qMax(5000, m_env.value("SUNAPI_EXPORT_POLL_TIMEOUT_MS", "120000").toInt());
            const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

            const auto pollOnce = std::make_shared<std::function<void()>>();
            *pollOnce = [=]() {
                // 장시간 대기 시 무한 폴링 방지
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
                if (elapsed > timeoutMs) {
                    m_playbackExportInProgress = false;
                    emit playbackExportFailed("내보내기 실패: 상태 조회 시간 초과");
                    return;
                }

                QMap<QString, QString> pollParams;
                pollParams.insert("JobID", jobId);
                pollParams.insert("ExportID", jobId);
                const QUrl pollUrl = buildUrl(pollCgi, pollSubmenu, pollAction, pollParams, pollExtra);
                QNetworkRequest pollReq(pollUrl);
                applySslIfNeeded(pollReq);
                QNetworkReply *pollReply = m_manager->get(pollReq);
                attachIgnoreSslErrors(pollReply, "SUNAPI_EXPORT_POLL");

                connect(pollReply, &QNetworkReply::finished, this, [=]() {
                    const QByteArray pollBody = pollReply->readAll();
                    if (pollReply->error() != QNetworkReply::NoError) {
                        // 상태 조회 실패는 다음 주기로 재시도
                        pollReply->deleteLater();
                        QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
                        return;
                    }

                    int progress = -1;
                    bool done = false;
                    bool failed = false;
                    QString dlUrlText;
                    QString reason2;
                    sunapiExportParsePollReply(pollBody, &progress, &done, &failed, &dlUrlText, &reason2);
                    if (progress >= 0) {
                        emit playbackExportProgress(progress, QString("내보내기 생성 중 %1%").arg(progress));
                    }

                    if (failed) {
                        m_playbackExportInProgress = false;
                        emit playbackExportFailed(QString("내보내기 실패: %1").arg(reason2.isEmpty() ? "장비 오류" : reason2));
                        pollReply->deleteLater();
                        return;
                    }

                    if (done) {
                        // 완료 시 다운로드 URL을 만들거나 응답 값을 사용해 파일 요청
                        if (dlUrlText.isEmpty()) {
                            const QString dlCgi = m_env.value("SUNAPI_EXPORT_DOWNLOAD_CGI", c.cgi).trimmed();
                            const QString dlSubmenu = m_env.value("SUNAPI_EXPORT_DOWNLOAD_SUBMENU", c.submenu).trimmed().isEmpty()
                                    ? c.submenu : m_env.value("SUNAPI_EXPORT_DOWNLOAD_SUBMENU", c.submenu).trimmed();
                            const QString dlAction = m_env.value("SUNAPI_EXPORT_DOWNLOAD_ACTION", "download").trimmed();
                            const QString dlExtra = m_env.value("SUNAPI_EXPORT_DOWNLOAD_QUERY").trimmed();
                            QMap<QString, QString> dlParams;
                            dlParams.insert("JobID", jobId);
                            dlParams.insert("ExportID", jobId);
                            const QUrl dlUrl = buildUrl(dlCgi, dlSubmenu, dlAction, dlParams, dlExtra);
                            pollReply->deleteLater();
                            playbackExportStartDownload(dlUrl, outPath);
                            return;
                        }

                        QUrl dlUrl = QUrl(dlUrlText);
                        if (dlUrl.isRelative()) {
                            dlUrl = pollUrl.resolved(dlUrl);
                        }
                        pollReply->deleteLater();
                        playbackExportStartDownload(dlUrl, outPath);
                        return;
                    }

                    pollReply->deleteLater();
                    QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
                });
            };

            QTimer::singleShot(pollMs, this, [=]() { (*pollOnce)(); });
        });
    };

    (*tryCreate)(0);
}










