#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QUrl>
#include <QVariant>
#include <QStringList>

#include <functional>
#include <memory>

class QJsonObject;
class QFile;
class QNetworkReply;
class QNetworkRequest;
class BackendPrivate;

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

    // Live metrics
    Q_PROPERTY(int activeCameras READ activeCameras WRITE setActiveCameras NOTIFY activeCamerasChanged)
    Q_PROPERTY(int currentFps READ currentFps WRITE setCurrentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(int latency READ latency WRITE setLatency NOTIFY latencyChanged)

    // Storage/system info
    Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageChanged)
    Q_PROPERTY(QString storageTotal READ storageTotal NOTIFY storageChanged)
    Q_PROPERTY(int storagePercent READ storagePercent NOTIFY storageChanged)
    Q_PROPERTY(int detectedObjects READ detectedObjects NOTIFY detectedObjectsChanged)
    Q_PROPERTY(QString networkStatus READ networkStatus NOTIFY networkStatusChanged)

    // Thermal/display info
    Q_PROPERTY(QString thermalFrameDataUrl READ thermalFrameDataUrl NOTIFY thermalFrameDataUrlChanged)
    Q_PROPERTY(QString cctv3dMapFrameDataUrl READ cctv3dMapFrameDataUrl NOTIFY cctv3dMapFrameDataUrlChanged)
    Q_PROPERTY(QString thermalInfoText READ thermalInfoText NOTIFY thermalInfoTextChanged)
    Q_PROPERTY(bool thermalStreaming READ thermalStreaming NOTIFY thermalStreamingChanged)
    Q_PROPERTY(QString thermalPalette READ thermalPalette NOTIFY thermalPaletteChanged)
    Q_PROPERTY(bool thermalAutoRange READ thermalAutoRange NOTIFY thermalAutoRangeChanged)
    Q_PROPERTY(int thermalAutoRangeWindowPercent READ thermalAutoRangeWindowPercent NOTIFY thermalAutoRangeWindowPercentChanged)
    Q_PROPERTY(int thermalManualMin READ thermalManualMin NOTIFY thermalManualRangeChanged)
    Q_PROPERTY(int thermalManualMax READ thermalManualMax NOTIFY thermalManualRangeChanged)
    Q_PROPERTY(int displayContrast READ displayContrast NOTIFY displaySettingsChanged)
    Q_PROPERTY(int displayBrightness READ displayBrightness NOTIFY displaySettingsChanged)
    Q_PROPERTY(int displaySharpnessLevel READ displaySharpnessLevel NOTIFY displaySettingsChanged)
    Q_PROPERTY(bool displaySharpnessEnabled READ displaySharpnessEnabled NOTIFY displaySettingsChanged)
    Q_PROPERTY(int displayColorLevel READ displayColorLevel NOTIFY displaySettingsChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend();

    bool isLoggedIn() const;
    QString userId() const;
    int sessionRemainingSeconds() const;
    bool loginLocked() const;
    int loginFailedAttempts() const;
    int loginMaxAttempts() const;
    QString serverUrl() const;

    QString rtspIp() const;
    void setRtspIp(const QString &ip);
    QString rtspPort() const;
    void setRtspPort(const QString &port);

    int activeCameras() const;
    void setActiveCameras(int count);

    int currentFps() const;
    void setCurrentFps(int fps);

    int latency() const;
    void setLatency(int ms);

    QString storageUsed() const;
    QString storageTotal() const;
    int storagePercent() const;
    int detectedObjects() const;
    QString networkStatus() const;

    QString thermalFrameDataUrl() const;
    QString cctv3dMapFrameDataUrl() const;
    QString thermalInfoText() const;
    bool thermalStreaming() const;
    QString thermalPalette() const;
    bool thermalAutoRange() const;
    int thermalAutoRangeWindowPercent() const;
    int thermalManualMin() const;
    int thermalManualMax() const;
    int displayContrast() const;
    int displayBrightness() const;
    int displaySharpnessLevel() const;
    bool displaySharpnessEnabled() const;
    int displayColorLevel() const;

    // Auth/session
    Q_INVOKABLE void login(QString id, QString pw);
    Q_INVOKABLE void registerUser(QString id, QString pw);
    Q_INVOKABLE void skipLoginTemporarily();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void resetSessionTimer();
    Q_INVOKABLE bool adminUnlock(QString adminCode);

    // RTSP config
    Q_INVOKABLE bool updateRtspIp(QString ip);
    Q_INVOKABLE bool updateRtspConfig(QString ip, QString port);
    Q_INVOKABLE bool resetRtspConfigToEnv();
    Q_INVOKABLE bool updateRtspCredentials(QString username, QString password);
    Q_INVOKABLE void useEnvRtspCredentials();
    Q_INVOKABLE void probeRtspEndpoint(QString ip, QString port, int timeoutMs = 1200);
    Q_INVOKABLE QVariantMap getClientSystemInfo() const;

    // Playback
    Q_INVOKABLE QString buildRtspUrl(int cameraIndex, bool useSubStream) const;
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

    // Thermal
    Q_INVOKABLE void startThermalStream();
    Q_INVOKABLE void stopThermalStream();
    Q_INVOKABLE void setThermalPalette(const QString &palette);
    Q_INVOKABLE void setThermalAutoRange(bool enabled);
    Q_INVOKABLE void setThermalAutoRangeWindowPercent(int percent);
    Q_INVOKABLE void setThermalManualRange(int minValue, int maxValue);

    // SUNAPI PTZ/Focus
    Q_INVOKABLE bool sunapiZoomIn(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomOut(int cameraIndex);
    Q_INVOKABLE bool sunapiZoomStop(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusNear(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusFar(int cameraIndex);
    Q_INVOKABLE bool sunapiFocusStop(int cameraIndex);
    Q_INVOKABLE bool sunapiSimpleAutoFocus(int cameraIndex);

    // SUNAPI display + 3D map
    Q_INVOKABLE void sunapiLoadDisplaySettings(int cameraIndex);
    Q_INVOKABLE bool sunapiSetDisplaySettings(int cameraIndex,
                                              int contrast,
                                              int brightness,
                                              int sharpnessLevel,
                                              int colorLevel,
                                              bool sharpnessEnabled);
    Q_INVOKABLE bool sunapiResetDisplaySettings(int cameraIndex);
    Q_INVOKABLE bool startCctv3dMapPrepareSequence(int cameraIndex);
    Q_INVOKABLE bool startCctv3dMapSequence(int cameraIndex);
    Q_INVOKABLE bool pauseCctv3dMapSequence();
    Q_INVOKABLE bool resumeCctv3dMapSequence();
    Q_INVOKABLE void stopCctv3dMapSequence();
    Q_INVOKABLE bool updateCctv3dMapView(double rx, double ry);

signals:
    // Property notify
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

    // Auth/session events
    void loginSuccess();
    void loginFailed(QString error);
    void registerSuccess(QString message);
    void registerFailed(QString error);
    void sessionExpired();

    // RTSP/camera events
    void cameraControlMessage(QString message, bool isError);
    void rtspProbeFinished(bool success, QString error);

    // Playback prepare/timeline events
    void playbackPrepared(QString url);
    void playbackPrepareFailed(QString error);
    void playbackTimelineLoaded(int channelIndex, QString dateText, QVariantList segments);
    void playbackTimelineFailed(QString error);
    void playbackMonthRecordedDaysLoaded(int channelIndex, QString yearMonth, QVariantList days);
    void playbackMonthRecordedDaysFailed(QString error);

    // Streaming WS events
    void streamingWsStateChanged(QString state);
    void streamingWsFrame(QString direction, QString hexPayload);
    void streamingWsError(QString error);

    // Playback export lifecycle
    void playbackExportStarted(QString message);
    void playbackExportProgress(int percent, QString message);
    void playbackExportFinished(QString path);
    void playbackExportFailed(QString error);

    // Thermal/display notify
    void thermalFrameDataUrlChanged();
    void cctv3dMapFrameDataUrlChanged();
    void thermalInfoTextChanged();
    void thermalStreamingChanged();
    void thermalPaletteChanged();
    void thermalAutoRangeChanged();
    void thermalAutoRangeWindowPercentChanged();
    void thermalManualRangeChanged();
    void displaySettingsChanged();

public slots:
    void checkStorage();
    void onSessionTick();

public:
    // Core/SSL helpers
    void setupSslConfiguration();
    void applySslIfNeeded(QNetworkRequest &request) const;
    void applyAuthIfNeeded(QNetworkRequest &request) const;
    void attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const;

    // Core API URL/request helpers
    QUrl buildApiUrl(const QString &path, const QMap<QString, QString> &query = {}) const;
    QNetworkRequest makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query = {}) const;

    // SUNAPI common body error helper
    bool isSunapiBodyError(const QString &body, QString *reason = nullptr) const;

    // Core initialization
    void loadEnv();
    void setupMqtt();

    // SUNAPI PTZ/Focus helpers
    bool sendSunapiPtzFocusCommand(int cameraIndex,
                                   const QString &command,
                                   const QString &actionLabel);

    // CCTV 3D map sequence helpers
    void runCctv3dMapSequenceStep(int sequenceToken, int step);
    void pollCctv3dMapMoveStatus(int sequenceToken);
    bool postCctvControlStart(int sequenceToken);
    bool postCctvControlStream(int sequenceToken);
    void connectCctvStreamWs(int sequenceToken);
    void disconnectCctvStreamWs(bool expectedStop = false);

    // Playback WS helpers
    QString ensurePlaybackWsSdpSource();
    void forwardPlaybackInterleavedRtp(const QByteArray &bytes);
    void parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket);

    // Playback export entry helpers
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

    // Playback export preparation helpers
    QString resolvePlaybackExportFfmpegBinary() const;
    bool buildPlaybackExportWsOutputPath(const QString &savePath,
                                         bool *wantsAvi,
                                         QString *outPath,
                                         QString *finalOutPath,
                                         QString *error);

    // Playback export RTSP/RTP processing
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

    // Playback export response parsing/download
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

    // Thermal pipeline helpers
    void handleThermalChunkMessage(const QByteArray &message);
    void processThermalFrame(const QMap<int, QByteArray> &chunks,
                             int totalChunks,
                             quint16 minVal,
                             quint16 maxVal,
                             int frameId);

private:
    std::unique_ptr<BackendPrivate> d_ptr;
};

#endif // BACKEND_H
