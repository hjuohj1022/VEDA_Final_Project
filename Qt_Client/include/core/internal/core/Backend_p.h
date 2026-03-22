#ifndef BACKEND_P_H
#define BACKEND_P_H

#include <QByteArray>
#include <QMap>
#include <QMqttClient>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QSslConfiguration>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QWebSocket>

struct ThermalAssemblyBuffer
{
    int frameId = -1;
    int totalChunksExpected = 0;
    qint64 frameStartedMs = 0;
    quint16 headerMin = 0;
    quint16 headerMax = 0;
    bool hasFrameId = false;
    QMap<int, QByteArray> chunks;
};

struct BackendPrivate
{
    // Core common state
    QPointer<QNetworkAccessManager> m_manager;
    QMap<QString, QString> m_env;
    QPointer<QTimer> m_storageTimer;
    QPointer<QTimer> m_sessionTimer;

    // Auth/session state
    bool m_isLoggedIn = false;
    bool m_twoFactorRequired = false;
    bool m_twoFactorEnabled = false;
    QString m_userId;
    QString m_authToken;
    QString m_preAuthToken;
    QString m_pendingLoginId;
    int m_sessionRemainingSeconds = 0;
    int m_sessionTimeoutSeconds = 300;
    bool m_loginLocked = false;
    int m_loginFailedAttempts = 0;
    int m_loginMaxAttempts = 5;

    // RTSP config/runtime state
    bool m_useCustomRtspConfig = false;
    QString m_rtspIp;
    QString m_rtspPort;
    QString m_rtspMainPathTemplateOverride;
    QString m_rtspSubPathTemplateOverride;

    bool m_useCustomRtspAuth = false;
    QString m_rtspUsernameOverride;
    QString m_rtspPasswordOverride;

    // Metrics/storage state
    int m_activeCameras = 0;
    int m_currentFps = 0;
    int m_latency = 0;
    int m_latencyRaw = 0;
    double m_latencyEma = 0.0;
    bool m_latencyEmaInitialized = false;
    QString m_storageUsed = "0 GB";
    QString m_storageTotal = "0 GB";
    int m_storagePercent = 0;
    int m_detectedObjects = 0;
    QString m_networkStatus = "Disconnected";

    // Request runtime state
    QPointer<QNetworkReply> m_loginReply;
    bool m_loginInProgress = false;
    QPointer<QNetworkReply> m_adminUnlockReply;
    bool m_adminUnlockInProgress = false;
    QPointer<QNetworkReply> m_twoFactorVerifyReply;
    bool m_twoFactorVerifyInProgress = false;
    QPointer<QNetworkReply> m_twoFactorStatusReply;
    bool m_twoFactorStatusInProgress = false;
    QPointer<QNetworkReply> m_twoFactorSetupReply;
    bool m_twoFactorSetupInProgress = false;
    QPointer<QNetworkReply> m_twoFactorConfirmReply;
    bool m_twoFactorConfirmInProgress = false;
    QPointer<QNetworkReply> m_twoFactorDisableReply;
    bool m_twoFactorDisableInProgress = false;
    QPointer<QNetworkReply> m_accountDeleteReply;
    bool m_accountDeleteInProgress = false;
    QPointer<QNetworkReply> m_passwordChangeReply;
    bool m_passwordChangeInProgress = false;
    QPointer<QNetworkReply> m_emailVerifyRequestReply;
    bool m_emailVerifyRequestInProgress = false;
    QPointer<QNetworkReply> m_emailVerifyConfirmReply;
    bool m_emailVerifyConfirmInProgress = false;
    QPointer<QNetworkReply> m_passwordResetRequestReply;
    bool m_passwordResetRequestInProgress = false;
    QPointer<QNetworkReply> m_passwordResetReply;
    bool m_passwordResetInProgress = false;
    QPointer<QNetworkReply> m_registerReply;
    bool m_registerInProgress = false;

    // TLS/SSL runtime state
    QSslConfiguration m_sslConfig;
    bool m_sslConfigReady = false;
    bool m_sslIgnoreErrors = false;

    // MQTT runtime state
    QPointer<QMqttClient> m_mqttClient;

    // Streaming/playback WS runtime state
    QPointer<QWebSocket> m_streamingWs;
    QPointer<QTimer> m_playbackWsKeepaliveTimer;
    bool m_playbackWsActive = false;
    bool m_playbackWsPlaySent = false;
    bool m_playbackWsPaused = false;
    int m_playbackWsNextCseq = 1;
    int m_playbackWsFinalSetupCseq = 0;
    QString m_playbackWsUri;
    QString m_playbackWsAuthHeader;
    QString m_playbackWsSession;
    QPointer<QUdpSocket> m_playbackRtpOutSocket;
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

    // Playback export state
    QPointer<QWebSocket> m_playbackExportWs;
    QPointer<QProcess> m_playbackExportFfmpegProc;
    QPointer<QNetworkReply> m_playbackExportDownloadReply;
    bool m_playbackExportCancelRequested = false;
    bool m_playbackExportInProgress = false;
    QString m_playbackExportOutPath;
    QString m_playbackExportFinalPath;

    // Thermal + display state
    QPointer<QWebSocket> m_thermalWs;
    QPointer<QNetworkReply> m_thermalStartReply;
    QPointer<QNetworkReply> m_thermalStopReply;
    QString m_thermalFrameDataUrl;
    QString m_thermalInfoText = "Thermal idle";
    bool m_thermalStreaming = false;
    bool m_thermalStopExpected = false;
    QString m_thermalPalette = "Jet";
    bool m_thermalAutoRange = true;
    int m_thermalAutoRangeWindowPercent = 96;
    int m_thermalManualMin = 7000;
    int m_thermalManualMax = 10000;
    int m_thermalCurrentFrameId = -1;
    int m_thermalLastFrameId = -1;
    int m_thermalTotalChunksExpected = 0;
    qint64 m_thermalFrameStartedMs = 0;
    qint64 m_thermalLastDisplayMs = 0;
    qint64 m_thermalChunkCount = 0;
    qint64 m_thermalTotalBytes = 0;
    double m_thermalDisplayFps = 0.0;
    QMap<int, QByteArray> m_thermalFrameChunks;
    QMap<int, ThermalAssemblyBuffer> m_thermalAssemblyBuffers;
    int m_thermalLegacyFrameSequence = 0;
    quint16 m_thermalHeaderMin = 0;
    quint16 m_thermalHeaderMax = 0;
    int m_displayContrast = 50;
    int m_displayBrightness = 50;
    int m_displaySharpnessLevel = 12;
    bool m_displaySharpnessEnabled = true;
    int m_displayColorLevel = 50;

    // CCTV 3D map state (phase 1: API + WS receive)
    QPointer<QWebSocket> m_cctvStreamWs;
    QPointer<QTimer> m_cctv3dMapStepTimer;
    int m_cctv3dMapSequenceToken = 0;
    int m_cctv3dMapPendingStep = 0;
    int m_cctv3dMapMoveStatusPollCount = 0;
    int m_cctv3dMapStartRetryCount = 0;
    int m_cctv3dMapStreamRetryCount = 0;
    bool m_cctv3dMapStopInFlight = false;
    qint64 m_cctv3dMapLastStopRequestMs = 0;
    int m_cctv3dMapCameraIndex = -1;
    bool m_cctv3dMapPrepareOnly = false;
    int m_cctv3dMapWsActiveToken = 0;
    bool m_cctv3dMapStoppingExpected = false;
    qint64 m_cctv3dMapFrameCount = 0;
    qint64 m_cctv3dMapTotalBytes = 0;
    qint64 m_cctv3dMapLastRenderMs = 0;
    QString m_cctv3dMapFrameDataUrl;
    double m_cctv3dMapViewRx = -20.0;
    double m_cctv3dMapViewRy = 35.0;
    int m_cctv3dMapRgbdWidth = 0;
    int m_cctv3dMapRgbdHeight = 0;
    QByteArray m_cctv3dMapRgbdDepthBytes;
    QByteArray m_cctv3dMapRgbdBgrBytes;
    QByteArray m_cctv3dMapWsStreamBuffer;
};

#endif // BACKEND_P_H
