#include "internal/sunapi/BackendSunapiExportFfmpegService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
QString resolveFfmpegBinary(const QMap<QString, QString> &env)
{
    const QString configured = env.value("PLAYBACK_EXPORT_FFMPEG_PATH").trimmed();
    if (!configured.isEmpty()) {
        if (QFileInfo::exists(configured)) {
            return configured;
        }
        const QString appSide = QDir(QCoreApplication::applicationDirPath()).filePath(configured);
        if (QFileInfo::exists(appSide)) {
            return appSide;
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("ffmpeg.exe"),
        QDir(appDir).filePath("tools/ffmpeg.exe"),
        QDir(appDir).filePath("../tools/ffmpeg.exe"),
        QDir::current().filePath("ffmpeg.exe"),
        QDir::current().filePath("tools/ffmpeg.exe")
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p)) {
            return p;
        }
    }
    return QStringLiteral("ffmpeg");
}
}

bool BackendSunapiExportFfmpegService::startPlaybackExportViaFfmpegBackup(
    Backend *backend,
    BackendPrivate *state,
    int channelIndex,
    const QString &dateText,
    const QString &startTimeText,
    const QString &endTimeText,
    const QString &outPath,
    const std::function<void()> &onFailedFallback)
{
    const QString user = state->m_useCustomRtspAuth ? state->m_rtspUsernameOverride : QString();
    const QString pass = state->m_useCustomRtspAuth ? state->m_rtspPasswordOverride : QString();
    if (user.isEmpty()) {
        return false;
    }

    QString ffOutPath = outPath.trimmed();
    if (ffOutPath.startsWith("file:", Qt::CaseInsensitive)) {
        ffOutPath = QUrl(ffOutPath).toLocalFile();
    }
    if (ffOutPath.isEmpty()) {
        return false;
    }
    const QString suffixLower = QFileInfo(ffOutPath).suffix().trimmed().toLower();
    if (suffixLower.isEmpty()) {
        ffOutPath += ".avi";
    }
    QDir().mkpath(QFileInfo(ffOutPath).absolutePath());
    state->m_playbackExportOutPath = ffOutPath;
    state->m_playbackExportFinalPath = ffOutPath;

    QNetworkRequest sessionReq = backend->makeApiJsonRequest("/api/sunapi/export/session", {
        {"channel", QString::number(channelIndex)},
        {"date", dateText.trimmed()},
        {"start_time", startTimeText.trimmed()},
        {"end_time", endTimeText.trimmed()},
        {"rtsp_port", state->m_env.value("SUNAPI_RTSP_PORT", "554").trimmed()}
    });
    backend->applyAuthIfNeeded(sessionReq);
    sessionReq.setRawHeader("Accept", "application/json");
    sessionReq.setRawHeader("X-Secure-Session", "Normal");

    QNetworkReply *sessionReply = state->m_manager->get(sessionReq);
    backend->attachIgnoreSslErrors(sessionReply, "SUNAPI_EXPORT_FFMPEG_SESSION");
    QEventLoop loop;
    QObject::connect(sessionReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();
    if (timer.isActive()) {
        timer.stop();
    } else if (sessionReply->isRunning()) {
        sessionReply->abort();
    }

    if (sessionReply->error() != QNetworkReply::NoError) {
        sessionReply->deleteLater();
        return false;
    }

    QString rtspUrlBase;
    const QJsonDocument sessionDoc = QJsonDocument::fromJson(sessionReply->readAll());
    if (sessionDoc.isObject()) {
        const QJsonObject obj = sessionDoc.object();
        rtspUrlBase = obj.value("Uri").toString().trimmed();
        if (rtspUrlBase.isEmpty()) {
            rtspUrlBase = obj.value("uri").toString().trimmed();
        }
    }
    sessionReply->deleteLater();
    if (rtspUrlBase.isEmpty()) {
        return false;
    }

    QUrl baseUri(rtspUrlBase);
    if (!baseUri.isValid() || baseUri.host().trimmed().isEmpty()) {
        return false;
    }
    baseUri.setUserName(user);
    baseUri.setPassword(pass);
    rtspUrlBase = baseUri.toString();
    const QString rtspUrlH264 = rtspUrlBase + "/trackID=H264";

    const QString ffmpegBin = resolveFfmpegBinary(state->m_env);
    const QString outExt = QFileInfo(ffOutPath).suffix().trimmed().toLower();
    const bool rawH264Out = (outExt == "h264" || outExt == "264");

    emit backend->playbackExportProgress(8, "내보내기 ffmpeg 처리 시작");

    const QStringList inputCandidates = {rtspUrlH264, rtspUrlBase};
    auto launchAttempt = std::make_shared<std::function<void(int)>>();
    *launchAttempt = [backend,
                      state,
                      ffmpegBin,
                      ffOutPath,
                      rawH264Out,
                      inputCandidates,
                      onFailedFallback,
                      launchAttempt](int attemptIdx) {
        if (state->m_playbackExportCancelRequested) {
            state->m_playbackExportInProgress = false;
            return;
        }
        if (attemptIdx < 0 || attemptIdx >= inputCandidates.size()) {
            if (onFailedFallback) {
                emit backend->playbackExportProgress(9, "ffmpeg 실패, WS fallback 전환");
                onFailedFallback();
            } else {
                state->m_playbackExportInProgress = false;
                emit backend->playbackExportFailed("내보내기 실패: ffmpeg 경로/실행 실패");
            }
            return;
        }

        const QString inUrl = inputCandidates.at(attemptIdx);
        QStringList args;
        args << "-y"
             << "-hide_banner"
             << "-loglevel" << "error"
             << "-rtsp_transport" << "tcp"
             << "-analyzeduration" << "20000000"
             << "-probesize" << "20000000"
             << "-i" << inUrl;

        if (rawH264Out) {
            args << "-map" << "0:v:0"
                 << "-an"
                 << "-c:v" << "copy"
                 << "-f" << "h264"
                 << ffOutPath;
        } else {
            args << "-map" << "0"
                 << "-c" << "copy"
                 << "-f" << "avi"
                 << ffOutPath;
        }

        QProcess *proc = new QProcess(backend);
        state->m_playbackExportFfmpegProc = proc;
        auto attemptHandled = std::make_shared<bool>(false);
        proc->setProcessChannelMode(QProcess::MergedChannels);

        QObject::connect(proc,
                         &QProcess::errorOccurred,
                         backend,
                         [backend, state, proc, attemptIdx, launchAttempt, attemptHandled](QProcess::ProcessError) {
                             if (*attemptHandled) {
                                 return;
                             }
                             *attemptHandled = true;
                             const QString err = proc->errorString();
                             qWarning() << "[SUNAPI][EXPORT][FFMPEG] process error:"
                                        << "attempt=" << attemptIdx
                                        << "err=" << err;
                             state->m_playbackExportFfmpegProc = nullptr;
                             proc->deleteLater();
                             (*launchAttempt)(attemptIdx + 1);
                         });

        QObject::connect(proc,
                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         backend,
                         [backend, state, proc, ffOutPath, attemptIdx, launchAttempt, attemptHandled](
                             int exitCode,
                             QProcess::ExitStatus exitStatus) {
                             if (*attemptHandled) {
                                 return;
                             }
                             *attemptHandled = true;
                             const QString logs = QString::fromUtf8(proc->readAll()).trimmed();
                             state->m_playbackExportFfmpegProc = nullptr;
                             proc->deleteLater();

                             if (state->m_playbackExportCancelRequested) {
                                 state->m_playbackExportInProgress = false;
                                 return;
                             }

                             if (exitStatus == QProcess::NormalExit
                                 && exitCode == 0
                                 && QFileInfo::exists(ffOutPath)
                                 && QFileInfo(ffOutPath).size() > 0) {
                                 state->m_playbackExportInProgress = false;
                                 state->m_playbackExportOutPath.clear();
                                 state->m_playbackExportFinalPath.clear();
                                  emit backend->playbackExportProgress(100, "내보내기 완료");
                                  emit backend->playbackExportFinished(ffOutPath);
                                  return;
                              }

                             qWarning() << "[SUNAPI][EXPORT][FFMPEG] failed:"
                                        << "attempt=" << attemptIdx
                                        << "exitCode=" << exitCode
                                        << "status=" << static_cast<int>(exitStatus)
                                        << "log=" << logs.left(300);
                             (*launchAttempt)(attemptIdx + 1);
                         });

        proc->start(ffmpegBin, args);
        if (!proc->waitForStarted(1500)) {
            const QString err = proc->errorString();
            qWarning() << "[SUNAPI][EXPORT][FFMPEG] start failed:"
                       << "attempt=" << attemptIdx
                       << "err=" << err;
            state->m_playbackExportFfmpegProc = nullptr;
            proc->deleteLater();
            (*launchAttempt)(attemptIdx + 1);
            return;
        }

        const int ffmpegTimeoutMs = qMax(5000, state->m_env.value("PLAYBACK_EXPORT_FFMPEG_TIMEOUT_MS", "20000").toInt());
        QPointer<QProcess> procGuard(proc);
        QTimer::singleShot(ffmpegTimeoutMs,
                           backend,
                           [backend, state, procGuard, attemptIdx, launchAttempt, attemptHandled, ffmpegTimeoutMs]() {
                               if (!procGuard || *attemptHandled) {
                                   return;
                               }
                               if (procGuard->state() == QProcess::NotRunning) {
                                   return;
                               }
                               *attemptHandled = true;
                               qWarning() << "[SUNAPI][EXPORT][FFMPEG] timeout:"
                                          << "attempt=" << attemptIdx
                                          << "timeoutMs=" << ffmpegTimeoutMs;
                               procGuard->kill();
                               state->m_playbackExportFfmpegProc = nullptr;
                               procGuard->deleteLater();
                               (*launchAttempt)(attemptIdx + 1);
                           });
    };
    (*launchAttempt)(0);

    return true;
}

