#include "internal/sunapi/BackendSunapiExportWsPrepService.h"

#include "internal/core/Backend_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

// 재생 내보내기 FFmpeg 실행 파일 경로 확인 함수
QString BackendSunapiExportWsPrepService::resolvePlaybackExportFfmpegBinary(const BackendPrivate *state)
{
    const QString configured = state->m_env.value("PLAYBACK_EXPORT_FFMPEG_PATH").trimmed();
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

// 재생 내보내기 웹소켓 출력 경로 생성 함수
bool BackendSunapiExportWsPrepService::buildPlaybackExportWsOutputPath(BackendPrivate *state,
                                                                       const QString &savePath,
                                                                       bool *wantsAvi,
                                                                       QString *outPath,
                                                                       QString *finalOutPath,
                                                                       QString *error)
{
    QString requestedOutPath = savePath.trimmed();
    if (requestedOutPath.startsWith("file:", Qt::CaseInsensitive)) {
        requestedOutPath = QUrl(requestedOutPath).toLocalFile();
    }
    if (requestedOutPath.isEmpty()) {
        if (error) {
            *error = "내보내기 실패: 저장 경로가 비어 있음";
        }
        return false;
    }

    // requested Fi 함수
    QFileInfo requestedFi(requestedOutPath);
    QString requestedExt = requestedFi.suffix().trimmed().toLower();
    if (requestedExt.isEmpty()) {
        requestedExt = "avi";
        requestedOutPath += ".avi";
    }

    const bool localWantsAvi = (requestedExt == "avi");
    QString localOutPath = requestedOutPath;
    if (localWantsAvi || !localOutPath.endsWith(".h264", Qt::CaseInsensitive)) {
        localOutPath = QFileInfo(requestedOutPath).absolutePath()
                       + "/"
                       + QFileInfo(requestedOutPath).completeBaseName()
                       + ".h264";
    }
    QDir().mkpath(QFileInfo(localOutPath).absolutePath());

    state->m_playbackExportOutPath = localOutPath;
    state->m_playbackExportFinalPath = requestedOutPath;

    if (wantsAvi) {
        *wantsAvi = localWantsAvi;
    }
    if (outPath) {
        *outPath = localOutPath;
    }
    if (finalOutPath) {
        *finalOutPath = requestedOutPath;
    }
    return true;
}

