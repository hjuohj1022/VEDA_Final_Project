#include "Backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

QString Backend::resolvePlaybackExportFfmpegBinary() const {
    // .env 경로 우선, 없으면 실행 디렉터리 인접 경로 탐색
    const QString configured = m_env.value("PLAYBACK_EXPORT_FFMPEG_PATH").trimmed();
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

bool Backend::buildPlaybackExportWsOutputPath(const QString &savePath,
                                              bool *wantsAvi,
                                              QString *outPath,
                                              QString *finalOutPath,
                                              QString *error) {
    QString requestedOutPath = savePath.trimmed();
    if (requestedOutPath.startsWith("file:", Qt::CaseInsensitive)) {
        requestedOutPath = QUrl(requestedOutPath).toLocalFile();
    }
    if (requestedOutPath.isEmpty()) {
        if (error) *error = "내보내기 실패: 저장 경로 비어 있음";
        return false;
    }

    QFileInfo requestedFi(requestedOutPath);
    QString requestedExt = requestedFi.suffix().trimmed().toLower();
    if (requestedExt.isEmpty()) {
        requestedExt = "avi";
        requestedOutPath += ".avi";
    }

    const bool localWantsAvi = (requestedExt == "avi");
    // 수집은 항상 .h264로 받고, 사용자가 AVI를 원하면 후처리 remux
    QString localOutPath = requestedOutPath;
    if (localWantsAvi || !localOutPath.endsWith(".h264", Qt::CaseInsensitive)) {
        localOutPath = QFileInfo(requestedOutPath).absolutePath()
                       + "/"
                       + QFileInfo(requestedOutPath).completeBaseName()
                       + ".h264";
    }
    QDir().mkpath(QFileInfo(localOutPath).absolutePath());

    m_playbackExportOutPath = localOutPath;
    m_playbackExportFinalPath = requestedOutPath;

    if (wantsAvi) *wantsAvi = localWantsAvi;
    if (outPath) *outPath = localOutPath;
    if (finalOutPath) *finalOutPath = requestedOutPath;
    return true;
}

