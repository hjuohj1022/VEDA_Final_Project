#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QMqttClient>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWebSocket>
#include <QStringList>
#include <functional>
class QUdpSocket;
class QProcess;
class QJsonObject;
class QFile;

class Backend : public QObject
{
    Q_OBJECT

    // Login/session/server info
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY isLoggedInChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY userIdChanged)
    Q_PROPERTY(int sessionRemainingSeconds READ sessionRemainingSeconds NOTIFY sessionRemainingSecondsChanged)
    Q_PROPERTY(bool loginLocked READ loginLocked NOTIFY loginLockChanged)
    Q_PROPERTY(int loginFailedAttempts READ loginFailedAttempts NOTIFY loginLockChanged)
    Q_PROPERTY(int loginMaxAttempts READ loginMaxAttempts CONSTANT)
    Q_PROPERTY(QString serverUrl READ serverUrl CONSTANT)
    Q_PROPERTY(QString rtspIp READ rtspIp NOTIFY rtspIpChanged)
    Q_PROPERTY(QString rtspPort READ rtspPort NOTIFY rtspPortChanged)

    // 라이브 메트릭 정보
    Q_PROPERTY(int activeCameras READ activeCameras WRITE setActiveCameras NOTIFY activeCamerasChanged)
    Q_PROPERTY(int currentFps READ currentFps WRITE setCurrentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(int latency READ latency WRITE setLatency NOTIFY latencyChanged)

    // Storage info
    Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageChanged)
    Q_PROPERTY(QString storageTotal READ storageTotal NOTIFY storageChanged)
    Q_PROPERTY(int storagePercent READ storagePercent NOTIFY storageChanged)
    Q_PROPERTY(int detectedObjects READ detectedObjects NOTIFY detectedObjectsChanged)
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkStatusChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend();

    bool isLoggedIn() const;
    QString userId() const { return m_userId; }
    int sessionRemainingSeconds() const { return m_sessionRemainingSeconds; }
    bool loginLocked() const { return m_loginLocked; }
    int loginFailedAttempts() const { return m_loginFailedAttempts; }
    int loginMaxAttempts() const { return m_loginMaxAttempts; }
    QString serverUrl() const;
    QString rtspIp() const;
    void setRtspIp(const QString &ip);
    QString rtspPort() const;
    void setRtspPort(const QString &port);

    int activeCameras() const { return m_activeCameras; }
    void setActiveCameras(int count);

    int currentFps() const { return m_currentFps; }
    void setCurrentFps(int fps);

    int latency() const { return m_latency; }
    void setLatency(int ms);

    QString storageUsed() const { return m_storageUsed; }
    QString storageTotal() const { return m_storageTotal; }
    int storagePercent() const { return m_storagePercent; }
    int detectedObjects() const { return m_detectedObjects; }
    QString networkStatus() const { return m_networkStatus; }

    // QML 호출 가능 인터페이스
    // 인증/세션
    Q_INVOKABLE void login(QString id, QString pw);
    Q_INVOKABLE void registerUser(QString id, QString pw);
    Q_INVOKABLE void skipLoginTemporarily();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void resetSessionTimer();
    Q_INVOKABLE bool adminUnlock(QString adminCode);

    // RTSP 설정/점검
    Q_INVOKABLE bool updateRtspIp(QString ip);
    Q_INVOKABLE bool updateRtspConfig(QString ip, QString port);
    Q_INVOKABLE bool resetRtspConfigToEnv();
    Q_INVOKABLE bool updateRtspCredentials(QString username, QString password);
    Q_INVOKABLE void useEnvRtspCredentials();
    Q_INVOKABLE void probeRtspEndpoint(QString ip, QString port, int timeoutMs = 1200);

    // 녹화 목록/파일 관리
    Q_INVOKABLE void refreshRecordings();
    Q_INVOKABLE void deleteRecording(QString name);
    Q_INVOKABLE void renameRecording(QString oldName, QString newName);
    Q_INVOKABLE QString getStreamUrl(QString fileName);
    Q_INVOKABLE void downloadAndPlay(QString fileName);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void exportRecording(QString fileName, QString savePath);

    // Playback 제어
    Q_INVOKABLE QString buildRtspUrl(int cameraIndex, bool useSubStream) const;
    Q_INVOKABLE QString buildPlaybackRtspUrl(int channelIndex, const QString &dateText, const QString &timeText) const;
    Q_INVOKABLE void preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText);
    Q_INVOKABLE void loadPlaybackTimeline(int channelIndex, const QString &dateText);
    Q_INVOKABLE void loadPlaybackMonthRecordedDays(int channelIndex, int year, int month);
    Q_INVOKABLE void streamingWsConnect();
    Q_INVOKABLE void streamingWsDisconnect();
    Q_INVOKABLE bool streamingWsSendHex(QString hexPayload);
    Q_INVOKABLE bool playbackWsPause();
    Q_INVOKABLE bool playbackWsPlay();
    Q_INVOKABLE void requestPlaybackExport(int channelIndex,
                                           const QString &dateText,
                                           const QString &startTimeText,
                                           const QString &endTimeText,
                                           const QString &savePath = QString());
    Q_INVOKABLE void cancelPlaybackExport();

    // SUNAPI PTZ/Focus
    Q_INVOKABLE bool sunapiZoomIn(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomOut(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomStop(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusNear(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusFar(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusStop(int cameraIndex);
    Q_INVOKABLE bool sunapiSimpleAutoFocus(int cameraIndex);

signals:
    // 상태 변경(Property NOTIFY)
    void isLoggedInChanged();
    void userIdChanged();
    void sessionRemainingSecondsChanged();
    void loginLockChanged();
    void rtspIpChanged();
    void rtspPortChanged();
    void activeCamerasChanged();
    void currentFpsChanged();
    void latencyChanged();
    void storageChanged();
    void detectedObjectsChanged();
    void networkStatusChanged();

    // 인증/세션 이벤트
    void loginSuccess();
    void loginFailed(QString error);
    void registerSuccess(QString message);
    void registerFailed(QString error);
    void sessionExpired();

    // 녹화 목록/파일 이벤트
    void recordingsLoaded(QVariantList files);
    void recordingsLoadFailed(QString error);

    void deleteSuccess();
    void deleteFailed(QString error);
    void renameSuccess();
    void renameFailed(QString error);

    // 다운로드 이벤트(일반 녹화 파일)
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(QString path);
    void downloadError(QString error);

    // RTSP/카메라 제어 이벤트
    void cameraControlMessage(QString message, bool isError);
    void rtspProbeFinished(bool success, QString error);

    // Playback 준비/타임라인 이벤트
    void playbackPrepared(QString url);
    void playbackPrepareFailed(QString error);
    void playbackTimelineLoaded(int channelIndex, QString dateText, QVariantList segments);
    void playbackTimelineFailed(QString error);
    void playbackMonthRecordedDaysLoaded(int channelIndex, QString yearMonth, QVariantList days);
    void playbackMonthRecordedDaysFailed(QString error);

    // Streaming WS 이벤트
    void streamingWsStateChanged(QString state);
    void streamingWsFrame(QString direction, QString hexPayload);
    void streamingWsError(QString error);

    // Playback Export 수명주기 이벤트
    void playbackExportStarted(QString message);
    void playbackExportProgress(int percent, QString message);
    void playbackExportFinished(QString path);
    void playbackExportFailed(QString error);

private slots:
    void checkStorage();
    void onSessionTick();

private:
    // Core/SSL 유틸
    void setupSslConfiguration();
    void applySslIfNeeded(QNetworkRequest &request) const;
    void applyAuthIfNeeded(QNetworkRequest &request) const;
    void attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const;

    // Core API URL/Request 유틸
    QUrl buildApiUrl(const QString &path, const QMap<QString, QString> &query = {}) const;
    QNetworkRequest makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query = {}) const;

    // SUNAPI 공통 URL/에러 유틸
    QUrl buildSunapiUrl(const QString &cgiName,
                        const QMap<QString, QString> &params,
                        int cameraIndex,
                        bool includeChannelParam) const;
    bool isSunapiBodyError(const QString &body, QString *reason = nullptr) const;

    // Core 초기화
    void loadEnv();
    void setupMqtt();

    // SUNAPI PTZ/Focus 런타임
    bool sendSunapiPtzFocusCommand(int cameraIndex,
                                   const QString &command,
                                   const QString &actionLabel);
    QString ensurePlaybackWsSdpSource();
    void forwardPlaybackInterleavedRtp(const QByteArray &bytes);
    void parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket);

    // Playback Export 진입점
    void requestPlaybackExportViaWs(int channelIndex,
                                    const QString &dateText,
                                    const QString &startTimeText,
                                    const QString &endTimeText,
                                    const QString &savePath);
    bool startPlaybackExportViaFfmpegBackup(int channelIndex,
                                            const QString &dateText,
                                            const QString &startTimeText,
                                            const QString &endTimeText,
                                            const QString &outPath,
                                            const std::function<void()> &onFailedFallback);

    // Playback Export 준비/경로/인증 유틸
    QString resolvePlaybackExportFfmpegBinary() const;
    bool buildPlaybackExportWsRtspUri(int channelIndex,
                                      const QString &dateText,
                                      const QString &startTimeText,
                                      const QString &endTimeText,
                                      QString *rtspUri,
                                      int *durationSec,
                                      QString *error) const;
    bool buildPlaybackExportWsOutputPath(const QString &savePath,
                                         bool *wantsAvi,
                                         QString *outPath,
                                         QString *finalOutPath,
                                         QString *error);
    bool fetchPlaybackExportRtspChallenge(const QString &rtspUri,
                                          QString *realm,
                                          QString *nonce,
                                          QString *error) const;

    // Playback Export RTSP/RTP 처리
    QByteArray buildPlaybackExportRtspRequest(int &nextCseq,
                                              const QString &authHeader,
                                              const QString &session,
                                              const QByteArray &method,
                                              const QByteArray &uri,
                                              bool withSession) const;
    void playbackExportWriteAnnexBNal(QFile &outFile, qint64 &writtenBytes, const QByteArray &nal);
    void playbackExportProcessRtpH264(const QByteArray &rtp,
                                      QFile &outFile,
                                      qint64 &writtenBytes,
                                      QByteArray &fuBuffer,
                                      int &fuNalType);
    bool playbackExportConsumeInterleaved(const QByteArray &bytes,
                                          int h264RtpChannel,
                                          QByteArray &interleavedBuf,
                                          QFile &outFile,
                                          qint64 &writtenBytes,
                                          QByteArray &fuBuffer,
                                          int &fuNalType,
                                          bool &gotRtp,
                                          qint64 &lastRtpMs,
                                          quint32 &firstTs,
                                          quint32 &lastTs,
                                          qint64 targetTsDelta,
                                          int &lastProgress,
                                          qint64 &lastProgressMs);
    void playbackExportHandleRtspResponse(const QString &text,
                                          const QHash<int, QString> &setupCseqTrack,
                                          int &setupDoneCount,
                                          QHash<QString, QByteArray> &trackInterleaved,
                                          int &h264RtpChannel,
                                          int setupExpected,
                                          bool &playSent,
                                          bool &playAck,
                                          QString &session,
                                          int &nextCseq,
                                          const QString &authHeader,
                                          const QString &uri,
                                          const std::function<void(const QByteArray &)> &sendRtsp,
                                          const std::function<void(const QString &)> &failWith);

    // Playback Export 응답 파싱/다운로드
    QString sunapiExportExtractKvValue(const QString &text, const QStringList &keys) const;
    QString sunapiExportExtractJsonString(const QJsonObject &obj, const QStringList &keys) const;
    int sunapiExportParseHmsToSec(const QString &hms) const;
    bool sunapiExportParseCreateReply(const QByteArray &body,
                                      QString *jobId,
                                      QString *downloadUrl,
                                      QString *reason) const;
    void sunapiExportParsePollReply(const QByteArray &body,
                                    int *progress,
                                    bool *done,
                                    bool *failed,
                                    QString *downloadUrl,
                                    QString *reason) const;
    void playbackExportStartDownload(const QUrl &downloadUrl, const QString &outPath);

    // 공통 런타임 상태
    QNetworkAccessManager *m_manager;
    QMap<QString, QString> m_env;
    QTimer *m_storageTimer;
    QTimer *m_sessionTimer;

    // 인증/세션 상태
    bool m_isLoggedIn = false;
    QString m_userId;
    QString m_authToken;
    int m_sessionRemainingSeconds = 0;
    const int m_sessionTimeoutSeconds = 300;
    bool m_loginLocked = false;
    int m_loginFailedAttempts = 0;
    const int m_loginMaxAttempts = 5;

    // RTSP 설정 상태
    bool m_useCustomRtspConfig = false;
    QString m_rtspIp;
    QString m_rtspPort;
    QString m_rtspMainPathTemplateOverride;
    QString m_rtspSubPathTemplateOverride;

    bool m_useCustomRtspAuth = false;
    QString m_rtspUsernameOverride;
    QString m_rtspPasswordOverride;

    // 메트릭 데이터
    int m_activeCameras = 0;
    int m_currentFps = 0;
    int m_latency = 0;

    // 스토리지 데이터
    QString m_storageUsed = "0 GB";
    QString m_storageTotal = "0 GB";
    int m_storagePercent = 0;

    QMqttClient *m_mqttClient = nullptr;
    int m_detectedObjects = 0;
    QString m_networkStatus = "Disconnected";

    // 다운로드/인증 요청 상태
    QNetworkReply *m_downloadReply = nullptr;
    QPointer<QNetworkReply> m_loginReply;
    bool m_loginInProgress = false;
    QPointer<QNetworkReply> m_registerReply;
    bool m_registerInProgress = false;
    QString m_tempFilePath;

    // SSL/WS 상태
    QSslConfiguration m_sslConfig;
    bool m_sslConfigReady = false;
    bool m_sslIgnoreErrors = false;
    QWebSocket *m_streamingWs = nullptr;
    QTimer *m_playbackWsKeepaliveTimer = nullptr;
    bool m_playbackWsActive = false;
    bool m_playbackWsPlaySent = false;
    bool m_playbackWsPaused = false;
    int m_playbackWsNextCseq = 1;
    int m_playbackWsFinalSetupCseq = 0;
    QString m_playbackWsUri;
    QString m_playbackWsAuthHeader;
    QString m_playbackWsSession;
    QUdpSocket *m_playbackRtpOutSocket = nullptr;
    QString m_playbackWsSdpPath;
    int m_playbackRtpVideoPort = 5004;
    int m_playbackRtpVideoChannel = 2;
    int m_playbackRtpVideoAltChannel = 3;
    int m_playbackWsH264SetupCseq = -1;
    int m_playbackRtpPayloadType = 96;
    bool m_playbackWsSdpPublished = false;
    QByteArray m_playbackSps;
    QByteArray m_playbackPps;
    QByteArray m_playbackFuBuffer;
    int m_playbackFuNalType = 0;
    QByteArray m_playbackInterleavedBuffer;
    int m_playbackValidRtpCount = 0;

    // Playback Export 상태
    QPointer<QWebSocket> m_playbackExportWs;
    QPointer<QProcess> m_playbackExportFfmpegProc;
    QPointer<QNetworkReply> m_playbackExportDownloadReply;
    bool m_playbackExportCancelRequested = false;
    bool m_playbackExportInProgress = false;
    QString m_playbackExportOutPath;
    QString m_playbackExportFinalPath;
};

#endif // BACKEND_H

