#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
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
class QUdpSocket;

class Backend : public QObject
{
    Q_OBJECT

    // 로그인/세션 및 서버 정보
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY isLoggedInChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY userIdChanged)
    Q_PROPERTY(int sessionRemainingSeconds READ sessionRemainingSeconds NOTIFY sessionRemainingSecondsChanged)
    Q_PROPERTY(bool loginLocked READ loginLocked NOTIFY loginLockChanged)
    Q_PROPERTY(int loginFailedAttempts READ loginFailedAttempts NOTIFY loginLockChanged)
    Q_PROPERTY(int loginMaxAttempts READ loginMaxAttempts CONSTANT)
    Q_PROPERTY(QString serverUrl READ serverUrl CONSTANT)
    Q_PROPERTY(QString rtspIp READ rtspIp NOTIFY rtspIpChanged)
    Q_PROPERTY(QString rtspPort READ rtspPort NOTIFY rtspPortChanged)

    // 라이브 메트릭
    Q_PROPERTY(int activeCameras READ activeCameras WRITE setActiveCameras NOTIFY activeCamerasChanged)
    Q_PROPERTY(int currentFps READ currentFps WRITE setCurrentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(int latency READ latency WRITE setLatency NOTIFY latencyChanged)

    // 열화상 데이터
    Q_PROPERTY(QVariant thermalImage READ thermalImage NOTIFY thermalImageChanged)
    Q_PROPERTY(double minTemperature READ minTemperature NOTIFY thermalDataChanged)
    Q_PROPERTY(double maxTemperature READ maxTemperature NOTIFY thermalDataChanged)

    // 스토리지 정보
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

    QVariant thermalImage() const { return m_thermalImage; }
    double minTemperature() const { return m_minTemp; }
    double maxTemperature() const { return m_maxTemp; }

    QString storageUsed() const { return m_storageUsed; }
    QString storageTotal() const { return m_storageTotal; }
    int storagePercent() const { return m_storagePercent; }
    int detectedObjects() const { return m_detectedObjects; }
    QString networkStatus() const { return m_networkStatus; }

    // QML 호출 가능 인터페이스
    Q_INVOKABLE void login(QString id, QString pw);
    Q_INVOKABLE void skipLoginTemporarily();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void resetSessionTimer();
    Q_INVOKABLE bool adminUnlock(QString adminCode);

    Q_INVOKABLE bool updateRtspIp(QString ip);
    Q_INVOKABLE bool updateRtspConfig(QString ip, QString port);
    Q_INVOKABLE bool resetRtspConfigToEnv();
    Q_INVOKABLE bool updateRtspCredentials(QString username, QString password);
    Q_INVOKABLE void useEnvRtspCredentials();
    Q_INVOKABLE void probeRtspEndpoint(QString ip, QString port, int timeoutMs = 1200);

    Q_INVOKABLE void refreshRecordings();
    Q_INVOKABLE void deleteRecording(QString name);
    Q_INVOKABLE void renameRecording(QString oldName, QString newName);
    Q_INVOKABLE QString getStreamUrl(QString fileName);
    Q_INVOKABLE void downloadAndPlay(QString fileName);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void exportRecording(QString fileName, QString savePath);

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

    Q_INVOKABLE bool sunapiZoomIn(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomOut(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomStop(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusNear(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusFar(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusStop(int cameraIndex);
    Q_INVOKABLE bool sunapiSimpleAutoFocus(int cameraIndex);
    Q_INVOKABLE void sunapiLoadSupportedPtzActions(int cameraIndex);

signals:
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
    void thermalImageChanged();
    void thermalDataChanged();

    void loginSuccess();
    void loginFailed(QString error);
    void sessionExpired();

    void recordingsLoaded(QVariantList files);
    void recordingsLoadFailed(QString error);

    void deleteSuccess();
    void deleteFailed(QString error);
    void renameSuccess();
    void renameFailed(QString error);

    // 다운로드 시그널
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(QString path);
    void downloadError(QString error);

    void cameraControlMessage(QString message, bool isError);
    void rtspProbeFinished(bool success, QString error);
    void sunapiSupportedPtzActionsLoaded(int cameraIndex, QVariantMap actions);
    void playbackPrepared(QString url);
    void playbackPrepareFailed(QString error);
    void playbackTimelineLoaded(int channelIndex, QString dateText, QVariantList segments);
    void playbackTimelineFailed(QString error);
    void playbackMonthRecordedDaysLoaded(int channelIndex, QString yearMonth, QVariantList days);
    void playbackMonthRecordedDaysFailed(QString error);
    void streamingWsStateChanged(QString state);
    void streamingWsFrame(QString direction, QString hexPayload);
    void streamingWsError(QString error);
    void playbackExportStarted(QString message);
    void playbackExportProgress(int percent, QString message);
    void playbackExportFinished(QString path);
    void playbackExportFailed(QString error);

private slots:
    void checkStorage();
    void onStorageReply(QNetworkReply *reply);
    void onSessionTick();

private:
    void setupSslConfiguration();
    void applySslIfNeeded(QNetworkRequest &request) const;
    void attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const;

    QUrl buildApiUrl(const QString &path, const QMap<QString, QString> &query = {}) const;
    QNetworkRequest makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query = {}) const;

    QUrl buildSunapiUrl(const QString &cgiName,
                        const QMap<QString, QString> &params,
                        int cameraIndex,
                        bool includeChannelParam) const;
    bool isSunapiBodyError(const QString &body, QString *reason = nullptr) const;

    void loadEnv();
    void setupMqtt();
    void setupThermalWs();
    bool sendSunapiCommand(const QString &cgiName,
                           const QMap<QString, QString> &params,
                           int cameraIndex,
                           const QString &actionLabel,
                           bool includeChannelParam = true);
    QString ensurePlaybackWsSdpSource();
    void forwardPlaybackInterleavedRtp(const QByteArray &bytes);
    void parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket);

    QNetworkAccessManager *m_manager;
    QMap<QString, QString> m_env;
    QTimer *m_storageTimer;
    QTimer *m_sessionTimer;

    bool m_isLoggedIn = false;
    QString m_userId;
    QString m_accessToken;
    int m_sessionRemainingSeconds = 0;
    const int m_sessionTimeoutSeconds = 300;
    bool m_loginLocked = false;
    int m_loginFailedAttempts = 0;
    const int m_loginMaxAttempts = 5;

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

    // 열화상 데이터
    QVariant m_thermalImage;
    double m_minTemp = 0.0;
    double m_maxTemp = 0.0;

    // 스토리지 데이터
    QString m_storageUsed = "0 GB";
    QString m_storageTotal = "0 GB";
    int m_storagePercent = 0;

    QMqttClient *m_mqttClient = nullptr;
    int m_detectedObjects = 0;
    QString m_networkStatus = "Disconnected";

    // 다운로드 상태
    QNetworkReply *m_downloadReply = nullptr;
    QPointer<QNetworkReply> m_loginReply;
    bool m_loginInProgress = false;
    QString m_tempFilePath;
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
};

#endif // BACKEND_H
