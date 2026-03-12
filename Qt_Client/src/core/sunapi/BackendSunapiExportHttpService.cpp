#include "internal/sunapi/BackendSunapiExportHttpService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

void BackendSunapiExportHttpService::requestPlaybackExport(Backend *backend, BackendPrivate *state, int channelIndex,
                                    const QString &dateText,
                                    const QString &startTimeText,
                                    const QString &endTimeText,
                                    const QString &savePath) {
    // 입력 형식 검증
    if (channelIndex < 0) {
        emit backend->playbackExportFailed("내보내기 실패: 유효하지 않은 채널 인덱스");
        return;
    }

    const QRegularExpression dateRe("^\\d{4}-\\d{2}-\\d{2}$");
    if (!dateRe.match(dateText.trimmed()).hasMatch()) {
        emit backend->playbackExportFailed("내보내기 실패: 날짜 형식 오류 (YYYY-MM-DD)");
        return;
    }

    const int startSec = backend->sunapiExportParseHmsToSec(startTimeText);
    const int endSec = backend->sunapiExportParseHmsToSec(endTimeText);
    if (startSec < 0 || endSec < 0) {
        emit backend->playbackExportFailed("내보내기 실패: 시간 형식 오류 (HH:MM:SS)");
        return;
    }
    if (startSec > endSec) {
        emit backend->playbackExportFailed("내보내기 실패: 시작 시간이 종료 시간보다 늦음");
        return;
    }

    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->playbackExportFailed("내보내기 실패: 로그인 필요");
        return;
    }

    QString outPath = savePath.trimmed();
    if (outPath.startsWith("file:", Qt::CaseInsensitive)) {
        outPath = QUrl(outPath).toLocalFile();
    }
    if (outPath.isEmpty()) {
        emit backend->playbackExportFailed("내보내기 실패: 저장 경로가 비어 있음");
        return;
    }

    const QString format = state->m_env.value("SUNAPI_EXPORT_TYPE", "AVI").trimmed().toUpper();
    const QString startDt = QString("%1 %2").arg(dateText.trimmed(), startTimeText.trimmed());
    const QString endDt = QString("%1 %2").arg(dateText.trimmed(), endTimeText.trimmed());

    const QStringList createModes = {"default", "backup", "start"};

    state->m_playbackExportCancelRequested = false;
    state->m_playbackExportInProgress = true;
    state->m_playbackExportOutPath = outPath;
    state->m_playbackExportFinalPath = outPath;
    emit backend->playbackExportStarted("내보내기 요청 시작");
    auto startFfmpegBackup = [backend, state, channelIndex, dateText, startTimeText, endTimeText, outPath](const std::function<void()> &onFailedFallback) -> bool {
        return backend->startPlaybackExportViaFfmpegBackup(channelIndex,
                                                  dateText,
                                                  startTimeText,
                                                  endTimeText,
                                                  outPath,
                                                  onFailedFallback);
    };

    const auto lastCreateReason = std::make_shared<QString>();
    auto tryCreate = std::make_shared<std::function<void(int)>>();
    *tryCreate = [=](int idx) {
        if (state->m_playbackExportCancelRequested) {
            state->m_playbackExportInProgress = false;
            return;
        }
        if (idx < 0 || idx >= createModes.size()) {
            // 608(Not Supported)인 경우 RTSP-over-WS 경로로 대체
            if (lastCreateReason->contains("Error Code: 608", Qt::CaseInsensitive)) {
                qInfo() << "[SUNAPI][EXPORT] fallback to RTSP-over-WS backup.smp path (Error 608)";
                const QString ffmpegBackupRaw = state->m_env.value("PLAYBACK_EXPORT_USE_FFMPEG_BACKUP", "0").trimmed().toLower();
                const bool useFfmpegBackup =
                        (ffmpegBackupRaw == "1" || ffmpegBackupRaw == "true" || ffmpegBackupRaw == "on");
                if (useFfmpegBackup) {
                    const bool started = startFfmpegBackup(
                        [backend, state, channelIndex, dateText, startTimeText, endTimeText, outPath]() {
                            if (state->m_playbackExportCancelRequested) {
                                state->m_playbackExportInProgress = false;
                                return;
                            }
                            backend->requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
                        });
                    if (started) {
                        return;
                    }
                }
                if (state->m_playbackExportCancelRequested) {
                    state->m_playbackExportInProgress = false;
                    return;
                }
                if (useFfmpegBackup) {
                    qWarning() << "[SUNAPI][EXPORT] ffmpeg backup unavailable -> direct WS fallback";
                } else {
                    qInfo() << "[SUNAPI][EXPORT] ffmpeg backup disabled -> direct WS fallback";
                }
                backend->requestPlaybackExportViaWs(channelIndex, dateText, startTimeText, endTimeText, outPath);
            } else if (!lastCreateReason->trimmed().isEmpty()) {
                state->m_playbackExportInProgress = false;
                emit backend->playbackExportFailed(QString("내보내기 실패: create 모드 재시도 실패 (%1)").arg(*lastCreateReason));
            } else {
                state->m_playbackExportInProgress = false;
                emit backend->playbackExportFailed("내보내기 실패: create 모드 전체 실패");
            }
            return;
        }

        const QString mode = createModes.at(idx);
        const QUrl url = backend->buildApiUrl("/api/sunapi/export/create", {
            {"channel", QString::number(channelIndex)},
            {"start_time", startDt},
            {"end_time", endDt},
            {"format", format},
            {"mode", mode}
        });
        qInfo() << "[SUNAPI][EXPORT] create request url=" << url;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        backend->applySslIfNeeded(req);
        backend->applyAuthIfNeeded(req);
        QNetworkReply *reply = state->m_manager->get(req);
        backend->attachIgnoreSslErrors(reply, "SUNAPI_EXPORT_CREATE");

        QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
            if (state->m_playbackExportCancelRequested) {
                reply->deleteLater();
                state->m_playbackExportInProgress = false;
                return;
            }
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
            if (!backend->sunapiExportParseCreateReply(body, &jobId, &downloadUrlText, &reason)) {
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
                // download URL이 있어도 Crow 고정 API를 우선 사용한다.
                if (!jobId.isEmpty()) {
                    const QUrl dlUrl = backend->buildApiUrl("/api/sunapi/export/download", {
                        {"job_id", jobId},
                        {"mode", mode}
                    });
                    backend->playbackExportStartDownload(dlUrl, outPath);
                    return;
                }
                QUrl dl = QUrl(downloadUrlText);
                if (dl.isRelative()) dl = url.resolved(dl);
                backend->playbackExportStartDownload(dl, outPath);
                return;
            }

            if (jobId.isEmpty()) {
                state->m_playbackExportInProgress = false;
                emit backend->playbackExportFailed("내보내기 실패: JobID/다운로드 URL 없음");
                return;
            }
            emit backend->playbackExportProgress(5, QString("내보내기 작업 생성 완료 (JobID=%1)").arg(jobId));
            const int pollMs = qMax(500, state->m_env.value("SUNAPI_EXPORT_POLL_INTERVAL_MS", "1500").toInt());
            const int timeoutMs = qMax(5000, state->m_env.value("SUNAPI_EXPORT_POLL_TIMEOUT_MS", "120000").toInt());
            const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

            const auto pollOnce = std::make_shared<std::function<void()>>();
            *pollOnce = [=]() {
                if (state->m_playbackExportCancelRequested) {
                    state->m_playbackExportInProgress = false;
                    return;
                }
                // 임시 대기 중 무한 루프 방지
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startMs;
                if (elapsed > timeoutMs) {
                    state->m_playbackExportInProgress = false;
                    emit backend->playbackExportFailed("내보내기 실패: 상태 조회 시간 초과");
                    return;
                }

                const QUrl pollUrl = backend->buildApiUrl("/api/sunapi/export/status", {
                    {"job_id", jobId},
                    {"mode", mode}
                });
                QNetworkRequest pollReq(pollUrl);
                backend->applySslIfNeeded(pollReq);
                backend->applyAuthIfNeeded(pollReq);
                QNetworkReply *pollReply = state->m_manager->get(pollReq);
                backend->attachIgnoreSslErrors(pollReply, "SUNAPI_EXPORT_POLL");

                QObject::connect(pollReply, &QNetworkReply::finished, backend, [=]() {
                    if (state->m_playbackExportCancelRequested) {
                        pollReply->deleteLater();
                        state->m_playbackExportInProgress = false;
                        return;
                    }
                    const QByteArray pollBody = pollReply->readAll();
                    if (pollReply->error() != QNetworkReply::NoError) {
                        // 상태 조회 실패 시 다음 주기에 재시도
                        pollReply->deleteLater();
                        QTimer::singleShot(pollMs, backend, [=]() { (*pollOnce)(); });
                        return;
                    }

                    int progress = -1;
                    bool done = false;
                    bool failed = false;
                    QString dlUrlText;
                    QString reason2;
                    backend->sunapiExportParsePollReply(pollBody, &progress, &done, &failed, &dlUrlText, &reason2);
                    if (progress >= 0) {
                        emit backend->playbackExportProgress(progress, QString("내보내기 생성 중 %1%").arg(progress));
                    }

                    if (failed) {
                        state->m_playbackExportInProgress = false;
                        emit backend->playbackExportFailed(QString("내보내기 실패: %1").arg(reason2.isEmpty() ? "알 수 없는 오류" : reason2));
                        pollReply->deleteLater();
                        return;
                    }

                    if (done) {
                        const QUrl dlUrl = backend->buildApiUrl("/api/sunapi/export/download", {
                            {"job_id", jobId},
                            {"mode", mode}
                        });
                        pollReply->deleteLater();
                        backend->playbackExportStartDownload(dlUrl, outPath);
                        return;
                    }

                    pollReply->deleteLater();
                    QTimer::singleShot(pollMs, backend, [=]() { (*pollOnce)(); });
                });
            };

            QTimer::singleShot(pollMs, backend, [=]() { (*pollOnce)(); });
        });
    };

    (*tryCreate)(0);
}















