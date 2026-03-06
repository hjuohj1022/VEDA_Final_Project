#include "Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
QString resolveFfmpegBinary(const QMap<QString, QString> &env) {
    // .env 경로 우선, 없으면 실행 경로 주변 후보 순서대로 탐색
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

bool Backend::startPlaybackExportViaFfmpegBackup(int channelIndex,
                                                 const QString &dateText,
                                                 const QString &startTimeText,
                                                 const QString &endTimeText,
                                                 const QString &outPath,
                                                 const std::function<void()> &onFailedFallback) {
    const QString user = m_useCustomRtspAuth ? m_rtspUsernameOverride : QString();
    const QString pass = m_useCustomRtspAuth ? m_rtspPasswordOverride : QString();
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
    m_playbackExportOutPath = ffOutPath;
    m_playbackExportFinalPath = ffOutPath;

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
    attachIgnoreSslErrors(sessionReply, "SUNAPI_EXPORT_FFMPEG_SESSION");
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

    const QString ffmpegBin = resolveFfmpegBinary(m_env);
    const QString outExt = QFileInfo(ffOutPath).suffix().trimmed().toLower();
    const bool rawH264Out = (outExt == "h264" || outExt == "264");

    emit playbackExportProgress(8, "내보내기 ffmpeg 추출 시작");

    const QStringList inputCandidates = { rtspUrlH264, rtspUrlBase };
    // trackID=H264 우선 시도 후 실패 시 base URL 재시도
    auto launchAttempt = std::make_shared<std::function<void(int)>>();
    *launchAttempt = [this,
                      ffmpegBin,
                      ffOutPath,
                      rawH264Out,
                      inputCandidates,
                      onFailedFallback,
                      launchAttempt](int attemptIdx) {
        if (m_playbackExportCancelRequested) {
            m_playbackExportInProgress = false;
            return;
        }
        if (attemptIdx < 0 || attemptIdx >= inputCandidates.size()) {
            if (onFailedFallback) {
                emit playbackExportProgress(9, "ffmpeg 실패, WS fallback 전환");
                onFailedFallback();
            } else {
                m_playbackExportInProgress = false;
                emit playbackExportFailed("내보내기 실패: ffmpeg 변환 실패");
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

        QProcess *proc = new QProcess(this);
        m_playbackExportFfmpegProc = proc;
        auto attemptHandled = std::make_shared<bool>(false);
        proc->setProcessChannelMode(QProcess::MergedChannels);

        connect(proc, &QProcess::errorOccurred, this, [this, proc, attemptIdx, launchAttempt, attemptHandled](QProcess::ProcessError) {
            if (*attemptHandled) return;
            *attemptHandled = true;
            const QString err = proc->errorString();
            qWarning() << "[SUNAPI][EXPORT][FFMPEG] process error:"
                       << "attempt=" << attemptIdx
                       << "err=" << err;
            m_playbackExportFfmpegProc = nullptr;
            proc->deleteLater();
            (*launchAttempt)(attemptIdx + 1);
        });

        connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, proc, ffOutPath, attemptIdx, launchAttempt, attemptHandled](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (*attemptHandled) return;
                    *attemptHandled = true;
                    const QString logs = QString::fromUtf8(proc->readAll()).trimmed();
                    m_playbackExportFfmpegProc = nullptr;
                    proc->deleteLater();

                    if (m_playbackExportCancelRequested) {
                        m_playbackExportInProgress = false;
                        return;
                    }

                    if (exitStatus == QProcess::NormalExit
                        && exitCode == 0
                        && QFileInfo::exists(ffOutPath)
                        && QFileInfo(ffOutPath).size() > 0) {
                        // 정상 파일 생성 확인 후 완료 이벤트 발행
                        m_playbackExportInProgress = false;
                        m_playbackExportOutPath.clear();
                        m_playbackExportFinalPath.clear();
                        emit playbackExportProgress(100, "내보내기 완료");
                        emit playbackExportFinished(ffOutPath);
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
            m_playbackExportFfmpegProc = nullptr;
            proc->deleteLater();
            (*launchAttempt)(attemptIdx + 1);
            return;
        }

        // ffmpeg가 장시간 멈춘 경우 다음 시도로 넘기기 위한 watchdog
        const int ffmpegTimeoutMs = qMax(5000, m_env.value("PLAYBACK_EXPORT_FFMPEG_TIMEOUT_MS", "20000").toInt());
        QPointer<QProcess> procGuard(proc);
        QTimer::singleShot(ffmpegTimeoutMs, this, [this, procGuard, attemptIdx, launchAttempt, attemptHandled, ffmpegTimeoutMs]() {
            if (!procGuard || *attemptHandled) return;
            if (procGuard->state() == QProcess::NotRunning) return;
            *attemptHandled = true;
            qWarning() << "[SUNAPI][EXPORT][FFMPEG] timeout:"
                       << "attempt=" << attemptIdx
                       << "timeoutMs=" << ffmpegTimeoutMs;
            procGuard->kill();
            m_playbackExportFfmpegProc = nullptr;
            procGuard->deleteLater();
            (*launchAttempt)(attemptIdx + 1);
        });
    };
    (*launchAttempt)(0);

    return true;
}
