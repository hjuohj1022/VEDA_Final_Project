#include "Backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

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

    const QString rtspHost = m_env.value("SUNAPI_RTSP_HOST").trimmed();
    const QString host = !rtspHost.isEmpty() ? rtspHost : m_env.value("SUNAPI_IP").trimmed();
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

bool Backend::fetchPlaybackExportRtspChallenge(const QString &rtspUri,
                                               QString *realm,
                                               QString *nonce,
                                               QString *error) const {
    const QString sunapiScheme = m_env.value("SUNAPI_SCHEME", "https").trimmed().toLower();
    const QString httpScheme = (sunapiScheme == "https") ? QStringLiteral("https") : QStringLiteral("http");
    const int defaultPort = (httpScheme == "https") ? 443 : 80;
    const int httpPort = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(httpScheme);
    url.setHost(m_env.value("SUNAPI_IP").trimmed());
    if (httpPort > 0) {
        url.setPort(httpPort);
    }
    url.setPath("/api/sunapi/playback/challenge");
    QUrlQuery q;
    q.addQueryItem("uri", rtspUri);
    q.addQueryItem("rtsp_port", m_env.value("SUNAPI_RTSP_PORT", "554").trimmed());
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applySslIfNeeded(req);

    QNetworkReply *reply = m_manager->get(req);
    attachIgnoreSslErrors(reply, "SUNAPI_EXPORT_WS_CHALLENGE");

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else if (reply->isRunning()) {
        reply->abort();
    }

    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = QString("내보내기 실패: RTSP challenge 요청 실패 (%1)").arg(reply->errorString());
        reply->deleteLater();
        return false;
    }

    QString localRealm;
    QString localNonce;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        localRealm = obj.value("Realm").toString().trimmed();
        if (localRealm.isEmpty()) localRealm = obj.value("realm").toString().trimmed();
        localNonce = obj.value("Nonce").toString().trimmed();
        if (localNonce.isEmpty()) localNonce = obj.value("nonce").toString().trimmed();
    }
    if (localRealm.isEmpty() || localNonce.isEmpty()) {
        if (error) *error = "내보내기 실패: RTSP Digest challenge 파싱 실패";
        return false;
    }

    if (realm) *realm = localRealm;
    if (nonce) *nonce = localNonce;
    return true;
}

