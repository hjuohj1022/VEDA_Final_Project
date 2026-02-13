#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariant>
#include <QTimer>
#include <QMap>

class Backend : public QObject
{
    Q_OBJECT
    // 로그인 상태 및 서버 정보
    Q_PROPERTY(bool isLoggedIn READ isLoggedIn NOTIFY isLoggedInChanged)
    Q_PROPERTY(QString userId READ userId NOTIFY userIdChanged)
    Q_PROPERTY(QString serverUrl READ serverUrl CONSTANT)
    Q_PROPERTY(QString rtspIp READ rtspIp CONSTANT)
    Q_PROPERTY(QString rtspPort READ rtspPort CONSTANT)
    
    // 라이브 뷰 메트릭 (FPS, 지연시간, 카메라 수)
    Q_PROPERTY(int activeCameras READ activeCameras WRITE setActiveCameras NOTIFY activeCamerasChanged)
    Q_PROPERTY(int currentFps READ currentFps WRITE setCurrentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(int latency READ latency WRITE setLatency NOTIFY latencyChanged)
    
    // 시스템 저장소 정보
    Q_PROPERTY(QString storageUsed READ storageUsed NOTIFY storageChanged)
    Q_PROPERTY(QString storageTotal READ storageTotal NOTIFY storageChanged)
    Q_PROPERTY(int storagePercent READ storagePercent NOTIFY storageChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend();

    bool isLoggedIn() const;
    QString userId() const { return m_userId; }
    QString serverUrl() const;
    QString rtspIp() const;
    QString rtspPort() const;

    int activeCameras() const { return m_activeCameras; }
    void setActiveCameras(int count);

    int currentFps() const { return m_currentFps; }
    void setCurrentFps(int fps);

    int latency() const { return m_latency; }
    void setLatency(int ms);

    QString storageUsed() const { return m_storageUsed; }
    QString storageTotal() const { return m_storageTotal; }
    int storagePercent() const { return m_storagePercent; }

    // QML에서 호출 가능한 메서드들
    Q_INVOKABLE void login(QString id, QString pw);
    Q_INVOKABLE void refreshRecordings();
    Q_INVOKABLE void deleteRecording(QString name);
    Q_INVOKABLE void renameRecording(QString oldName, QString newName); // 신규: 파일 이름 변경
    Q_INVOKABLE QString getStreamUrl(QString fileName);
    Q_INVOKABLE void downloadAndPlay(QString fileName); // 신규: 다운로드 후 재생
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void exportRecording(QString fileName, QString savePath);

signals:
    void isLoggedInChanged();
    void userIdChanged();
    void activeCamerasChanged();
    void currentFpsChanged();
    void latencyChanged();
    void storageChanged();
    
    void loginSuccess();
    void loginFailed(QString error);
    
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

private:
    void loadEnv();
    
    QNetworkAccessManager *m_manager;
    QMap<QString, QString> m_env;
    QTimer *m_storageTimer;
    
    bool m_isLoggedIn = false;
    QString m_userId;
    
    // 메트릭 데이터
    int m_activeCameras = 0;
    int m_currentFps = 0;
    int m_latency = 0;
    
    // 저장소 데이터
    QString m_storageUsed = "0 GB";
    QString m_storageTotal = "0 GB";
    int m_storagePercent = 0;
    
    // 다운로드 관리
    QNetworkReply *m_downloadReply = nullptr;
    QString m_tempFilePath;
};

#endif // BACKEND_H
