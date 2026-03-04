#include "Backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QTcpSocket>
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

bool Backend::buildPlaybackExportWsRtspUri(int channelIndex,
                                           const QString &dateText,
                                           const QString &startTimeText,
                                           const QString &endTimeText,
                                           QString *rtspUri,
                                           int *durationSec,
                                           QString *error) const {
    const int startSec = sunapiExportParseHmsToSec(startTimeText);
    const int endSec = sunapiExportParseHmsToSec(endTimeText);
    // 시작/종료 시각 유효성 검사
    if (startSec < 0 || endSec < 0 || endSec < startSec) {
        if (error) *error = "내보내기 실패: 시작/종료 시간 형식 오류";
        return false;
    }
    if (durationSec) {
        *durationSec = qMax(1, (endSec - startSec) + 1);
    }

    const QDateTime dtStart = QDateTime::fromString(dateText.trimmed() + " " + startTimeText.trimmed(),
                                                    "yyyy-MM-dd HH:mm:ss");
    const QDateTime dtEnd = QDateTime::fromString(dateText.trimmed() + " " + endTimeText.trimmed(),
                                                  "yyyy-MM-dd HH:mm:ss");
    if (!dtStart.isValid() || !dtEnd.isValid()) {
        if (error) *error = "내보내기 실패: 날짜/시간 파싱 오류";
        return false;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        if (error) *error = "내보내기 실패: SUNAPI 접속 정보 누락";
        return false;
    }

    const QString tsStart = dtStart.toString("yyyyMMddHHmmss");
    const QString tsEnd = dtEnd.toString("yyyyMMddHHmmss");
    // backup.smp 형식 RTSP URI 생성
    if (rtspUri) {
        *rtspUri = QString("rtsp://%1/%2/recording/%3-%4/OverlappedID=0/backup.smp")
                .arg(host, QString::number(channelIndex), tsStart, tsEnd);
    }
    return true;
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

bool Backend::fetchPlaybackExportRtspChallenge(const QString &host,
                                               int rtspPort,
                                               const QString &rtspUri,
                                               QString *realm,
                                               QString *nonce,
                                               QString *error) const {
    QTcpSocket socket;
    socket.connectToHost(host, static_cast<quint16>(rtspPort));
    if (!socket.waitForConnected(2000)) {
        if (error) *error = QString("내보내기 실패: RTSP 연결 실패 (%1)").arg(socket.errorString());
        return false;
    }

    QByteArray optReq;
    optReq += "OPTIONS " + rtspUri.toUtf8() + " RTSP/1.0\r\n";
    optReq += "CSeq: 1\r\n";
    optReq += "User-Agent: UWC[undefined]\r\n";
    optReq += "\r\n";
    socket.write(optReq);
    socket.flush();
    if (!socket.waitForReadyRead(2000)) {
        socket.disconnectFromHost();
        if (error) *error = "내보내기 실패: RTSP challenge 응답 대기 시간 초과";
        return false;
    }
    QByteArray challengeResp = socket.readAll();
    while (socket.waitForReadyRead(120)) {
        challengeResp += socket.readAll();
    }
    socket.disconnectFromHost();

    const QString challengeText = QString::fromUtf8(challengeResp);
    const QRegularExpression realmRe("realm\\s*=\\s*\"([^\"]+)\"",
                                     QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression nonceRe("nonce\\s*=\\s*\"([^\"]+)\"",
                                     QRegularExpression::CaseInsensitiveOption);
    const QString localRealm = realmRe.match(challengeText).captured(1).trimmed();
    const QString localNonce = nonceRe.match(challengeText).captured(1).trimmed();
    if (localRealm.isEmpty() || localNonce.isEmpty()) {
        if (error) *error = "내보내기 실패: RTSP Digest challenge 파싱 실패";
        return false;
    }

    if (realm) *realm = localRealm;
    if (nonce) *nonce = localNonce;
    return true;
}

