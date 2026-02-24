#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariant>
#include <QTimer>
#include <QMap>
#include <QMqttClient>

class Backend : public QObject
{
    Q_OBJECT
    // 로그인 상태 및 서버 정보
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY isLoggedInChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY userIdChanged)
    Q_PROPERTY(int sessionRemainingSeconds READ sessionRemainingSeconds NOTIFY sessionRemainingSecondsChanged)
    Q_PROPERTY(bool loginLocked READ loginLocked NOTIFY loginLockChanged)
    Q_PROPERTY(int loginFailedAttempts READ loginFailedAttempts NOTIFY loginLockChanged)
    Q_PROPERTY(int loginMaxAttempts READ loginMaxAttempts CONSTANT)
    Q_PROPERTY(QString serverUrl READ serverUrl CONSTANT)
    Q_PROPERTY(QString rtspIp READ rtspIp NOTIFY rtspIpChanged)
    Q_PROPERTY(QString rtspPort READ rtspPort NOTIFY rtspPortChanged)
    
    // 라이브 뷰 메트릭 (FPS, 지연시간, 카메라 수)
    Q_PROPERTY(int activeCameras READ activeCameras WRITE setActiveCameras NOTIFY activeCamerasChanged)
    Q_PROPERTY(int currentFps READ currentFps WRITE setCurrentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(int latency READ latency WRITE setLatency NOTIFY latencyChanged)
    
    // 시스템 저장소 정보
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

    // QML에서 호출 가능한 메서드들
    Q_INVOKABLE void login(QString id, QString pw);
    Q_INVOKABLE void skipLoginTemporarily();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void resetSessionTimer();
    Q_INVOKABLE bool adminUnlock(QString adminCode);
    Q_INVOKABLE bool updateRtspIp(QString ip);
    Q_INVOKABLE bool updateRtspConfig(QString ip, QString port);
    Q_INVOKABLE bool resetRtspConfigToEnv();
    Q_INVOKABLE void refreshRecordings();
    Q_INVOKABLE void deleteRecording(QString name);
    Q_INVOKABLE void renameRecording(QString oldName, QString newName); // 신규: 파일 이름 변경
    Q_INVOKABLE QString getStreamUrl(QString fileName);
    Q_INVOKABLE void downloadAndPlay(QString fileName); // 신규: 다운로드 후 재생
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void exportRecording(QString fileName, QString savePath);
    Q_INVOKABLE QString buildRtspUrl(int cameraIndex, bool useSubStream) const;

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
    
    void loginSuccess();
    void loginFailed(QString error);
    void sessionExpired();
    
    void recordingsLoaded(QVariantList files);
    void recordingsLoadFailed(QString error);
    
    void deleteSuccess();
    void deleteFailed(QString error);

    void renameSuccess();
    void renameFailed(QString error);

    // 다운로드 신호
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(QString path);
    void downloadError(QString error);

private slots:
    void checkStorage();
    void onStorageReply(QNetworkReply *reply);
    void onSessionTick();

private:
    void loadEnv();
    void setupMqtt();
    
    QNetworkAccessManager *m_manager;
    QMap<QString, QString> m_env;
    QTimer *m_storageTimer;
    QTimer *m_sessionTimer;
    
    bool m_isLoggedIn = false;
    QString m_userId;
    int m_sessionRemainingSeconds = 0;
    const int m_sessionTimeoutSeconds = 300;
    bool m_loginLocked = false;
    int m_loginFailedAttempts = 0;
    const int m_loginMaxAttempts = 5;
    bool m_useCustomRtspConfig = false;
    QString m_rtspIp;
    QString m_rtspPort;
    
    // 메트릭 데이터
    int m_activeCameras = 0;
    int m_currentFps = 0;
    int m_latency = 0;
    
    // 저장소 데이터
    QString m_storageUsed = "0 GB";
    QString m_storageTotal = "0 GB";
    int m_storagePercent = 0;
    QMqttClient *m_mqttClient = nullptr;
    int m_detectedObjects = 0;
    QString m_networkStatus = "Disconnected";
    
    // 다운로드 관리
    QNetworkReply *m_downloadReply = nullptr;
    QString m_tempFilePath;
};

#endif // BACKEND_H
