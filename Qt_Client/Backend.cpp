#include "Backend.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QNetworkCookieJar>
#include <QSettings>

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this)); // 쿠키 저장소 추가 (세션 유지)
    setActiveCameras(4);
    loadEnv();
    QSettings settings;
    m_useCustomRtspConfig = settings.value("network/use_custom_rtsp", false).toBool();

    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8554").trimmed();

    if (m_useCustomRtspConfig) {
        m_rtspIp = settings.value("network/rtsp_ip", envIp).toString().trimmed();
        m_rtspPort = settings.value("network/rtsp_port", envPort).toString().trimmed();
    } else {
        // 초기에는 .env 값을 우선 사용
        m_rtspIp = envIp;
        m_rtspPort = envPort;
    }

    if (m_rtspIp.isEmpty()) {
        m_rtspIp = envIp;
    }
    if (m_rtspPort.isEmpty()) {
        m_rtspPort = envPort;
    }

    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setInterval(1000);
    connect(m_sessionTimer, &QTimer::timeout, this, &Backend::onSessionTick);
    
    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &Backend::checkStorage);
    m_storageTimer->start(5000); // 5초마다 확인
    
    // 초기 확인
    checkStorage();

    // 데이터 시뮬레이션 타이머 (데모용 -> 일부 실제 데이터로 교체)
    QTimer *simTimer = new QTimer(this);
    connect(simTimer, &QTimer::timeout, this, [=](){
        // FPS: 25~30 (여전히 시뮬레이션, VLC에서 집계하려면 구조 변경 필요)
        int fps = 25 + (rand() % 6);
        setCurrentFps(fps);
        
        // 활성 카메라: 가끔 변동
        if (rand() % 10 == 0) setActiveCameras(3 + (rand() % 2));
        
        // 지연 시간 측정 (RTSP 서버로 TCP 핑)
        QTcpSocket *socket = new QTcpSocket(this);
        QElapsedTimer *timer = new QElapsedTimer();
        timer->start();
        
        QString ip = rtspIp();
        int port = rtspPort().toInt();
        if(port == 0) port = 8554;

        // qDebug() << "Pinging" << ip << ":" << port;
        
        connect(socket, &QTcpSocket::connected, this, [=](){
            int elapsed = timer->elapsed();
            // qDebug() << "Ping Success:" << elapsed << "ms";
            setLatency(elapsed);
            socket->disconnectFromHost();
            socket->deleteLater();
            delete timer;
        });
        
        connect(socket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError socketError){
            // qDebug() << "Ping Error:" << socket->errorString();
            setLatency(999); // 높은 지연 시간으로 오류 표시
            socket->deleteLater();
            delete timer;
        });
        
        socket->connectToHost(ip, port);
    });
    simTimer->start(2000); // 2초마다 측정
}

Backend::~Backend() {}

void Backend::loadEnv() {
    QFile file(".env");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty() || line.startsWith("#")) continue;
        QStringList parts = line.split("=");
        if (parts.length() == 2) m_env.insert(parts[0].trimmed(), parts[1].trimmed());
    }
    file.close();
}

bool Backend::isLoggedIn() const { return m_isLoggedIn; }
QString Backend::serverUrl() const { return m_env.value("API_URL", "http://localhost:8080"); }
QString Backend::rtspIp() const { return m_rtspIp; }
QString Backend::rtspPort() const { return m_rtspPort; }

void Backend::setRtspIp(const QString &ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspIp == trimmed) return;

    m_rtspIp = trimmed;
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_ip", m_rtspIp);
    m_useCustomRtspConfig = true;
    emit rtspIpChanged();
}

void Backend::setRtspPort(const QString &port) {
    QString trimmed = port.trimmed();
    if (trimmed.isEmpty()) return;
    if (m_rtspPort == trimmed) return;

    m_rtspPort = trimmed;
    QSettings settings;
    settings.setValue("network/use_custom_rtsp", true);
    settings.setValue("network/rtsp_port", m_rtspPort);
    m_useCustomRtspConfig = true;
    emit rtspPortChanged();
}

void Backend::setActiveCameras(int count) {
    if (m_activeCameras != count) {
        m_activeCameras = count;
        emit activeCamerasChanged();
    }
}

void Backend::setCurrentFps(int fps) {
    if (m_currentFps != fps) {
        m_currentFps = fps;
        emit currentFpsChanged();
    }
}

void Backend::setLatency(int ms) {
    if (m_latency != ms) {
        m_latency = ms;
        emit latencyChanged();
    }
}

void Backend::login(QString id, QString pw) {
    if (m_loginLocked) {
        emit loginFailed("로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
        return;
    }

    QUrl url(serverUrl() + "/login");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject json;
    json["id"] = id;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = m_manager->post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            m_isLoggedIn = true;
            m_userId = id;
            m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
            m_sessionTimer->start();
            if (m_loginFailedAttempts != 0 || m_loginLocked) {
                m_loginFailedAttempts = 0;
                m_loginLocked = false;
                emit loginLockChanged();
            }
            emit isLoggedInChanged();
            emit userIdChanged();
            emit sessionRemainingSecondsChanged();
            emit loginSuccess();
        } else {
            if (!m_loginLocked) {
                m_loginFailedAttempts++;
                if (m_loginFailedAttempts >= m_loginMaxAttempts) {
                    m_loginLocked = true;
                }
                emit loginLockChanged();
            }

            if (m_loginLocked) {
                emit loginFailed("비밀번호를 여러 번 틀려 로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
            } else {
                int remaining = m_loginMaxAttempts - m_loginFailedAttempts;
                emit loginFailed(QString("비밀번호가 잘못 되었습니다. (%1회 남음)").arg(remaining));
            }
        }
        reply->deleteLater();
    });
}

void Backend::logout() {
    if (!m_isLoggedIn) return;

    m_isLoggedIn = false;
    m_userId.clear();
    m_sessionTimer->stop();
    m_sessionRemainingSeconds = 0;

    emit isLoggedInChanged();
    emit userIdChanged();
    emit sessionRemainingSecondsChanged();
}

void Backend::resetSessionTimer() {
    if (!m_isLoggedIn) return;

    if (m_sessionRemainingSeconds != m_sessionTimeoutSeconds) {
        m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
        emit sessionRemainingSecondsChanged();
    }

    if (!m_sessionTimer->isActive()) {
        m_sessionTimer->start();
    }
}

bool Backend::adminUnlock(QString adminCode) {
    QString expected = m_env.value("ADMIN_UNLOCK_KEY").trimmed();
    if (expected.isEmpty()) {
        emit loginFailed("관리자 해제 키가 설정되지 않았습니다.");
        return false;
    }

    if (adminCode.trimmed() != expected) {
        emit loginFailed("관리자 해제 키가 올바르지 않습니다.");
        return false;
    }

    m_loginLocked = false;
    m_loginFailedAttempts = 0;
    emit loginLockChanged();
    return true;
}

bool Backend::updateRtspIp(QString ip) {
    QString trimmed = ip.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    setRtspIp(trimmed);
    return true;
}

bool Backend::updateRtspConfig(QString ip, QString port) {
    QString ipTrimmed = ip.trimmed();
    QString portTrimmed = port.trimmed();
    if (ipTrimmed.isEmpty() || portTrimmed.isEmpty()) {
        return false;
    }

    bool ok = false;
    int portNum = portTrimmed.toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        return false;
    }

    setRtspIp(ipTrimmed);
    setRtspPort(QString::number(portNum));
    return true;
}

void Backend::refreshRecordings() {
    QString urlStr = QString("%1/recordings?user=%2").arg(serverUrl(), m_userId);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    qDebug() << "Refreshing recordings for user:" << m_userId << "from:" << url.toString();
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            qDebug() << "Recordings Response:" << data.left(200); // 처음 200자 출력
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull() && doc.object().contains("files")) {
                QJsonArray arr = doc.object()["files"].toArray();
                qDebug() << "Found" << arr.size() << "files";
                
                QVariantList fileList;
                for(const QJsonValue &val : arr) {
                    QJsonObject obj = val.toObject();
                    QVariantMap fileMap;
                    fileMap["name"] = obj["name"].toString();
                    fileMap["size"] = obj["size"].toVariant().toLongLong(); // size 필드 추가
                    fileList.append(fileMap);
                }
                emit recordingsLoaded(fileList);
            } else {
                qDebug() << "응답에 'files' 배열 없음";
                emit recordingsLoaded(QVariantList());
            }
        } else {
            qDebug() << "Refesh Failed:" << reply->errorString();
            emit recordingsLoadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void Backend::deleteRecording(QString name) {
    QUrl url(serverUrl() + "/recordings?file=" + name);
    QNetworkRequest request(url);
    QNetworkReply *reply = m_manager->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit deleteSuccess();
            refreshRecordings(); // 자동 새로고침
        } else {
            emit deleteFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void Backend::renameRecording(QString oldName, QString newName) {
    QUrl url(serverUrl() + "/recordings/rename"); // 가정된 엔드포인트
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["oldName"] = oldName;
    json["newName"] = newName;
    QJsonDocument doc(json);

    // PUT 요청으로 이름 변경 시도
    QNetworkReply *reply = m_manager->put(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit renameSuccess();
            refreshRecordings(); // 목록 갱신
        } else {
            emit renameFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

QString Backend::getStreamUrl(QString fileName) {
    return QString("%1/stream?file=%2").arg(serverUrl(), fileName);
}

void Backend::downloadAndPlay(QString fileName) {
    qDebug() << "Backend::downloadAndPlay called for:" << fileName; // 즉시 로그 추가

    // 1. 기존 다운로드 취소
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }

    // 2. 임시 경로 설정
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFilePath = tempDir + "/" + fileName;
    
    // 존재하면 삭제
    if (QFile::exists(m_tempFilePath)) {
        QFile::remove(m_tempFilePath);
    }

    // 3. 다운로드 시작
    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    m_downloadReply = m_manager->get(request);
    
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total){
        if (total > 0) {
            emit downloadProgress(received, total);
        }
    });
    
    connect(m_downloadReply, &QNetworkReply::finished, this, [=](){
        // 취소된 경우 처리 안함 (cancelDownload에서 처리됨)
        if (!m_downloadReply) return; 

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            // 파일로 저장
            QFile file(m_tempFilePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_downloadReply->readAll());
                file.close();
                // 로컬 파일 재생
                emit downloadFinished("file:///" + m_tempFilePath);
            } else {
                emit downloadError("Failed to save file: " + file.errorString());
            }
        } else if (m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
             emit downloadError(m_downloadReply->errorString());
        }
        
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    });
}

void Backend::cancelDownload() {
    if (m_downloadReply) {
        // 멤버 변수를 먼저 초기화하여 finished 시그널 핸들러가 이를 감지하고 종료하도록 함
        QNetworkReply *reply = m_downloadReply;
        m_downloadReply = nullptr;

        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
        qDebug() << "Download cancelled by user";
    }
}

void Backend::exportRecording(QString fileName, QString savePath) {
    // 1. 기존 다운로드 취소 (충돌 방지)
    cancelDownload();

    // 2. 경로 설정 (사용자 지정 경로)
    m_tempFilePath = savePath; // 재사용 (다운로드 로직 공유를 위해 멤버변수 사용)
    
    // 3. 다운로드 시작 (downloadAndPlay 로직과 유사하지만 완료 시그널 다르게 처리 가능)
    // 하지만 downloadFinished 로직을 공유하면 "재생"이 되어버림.
    // 별도 함수로 분리하거나 플래그를 두는 것이 좋음.
    // 여기서는 간단히 별도 로직 구현.
    
    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    m_downloadReply = m_manager->get(request);
    
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total){
        if (total > 0) emit downloadProgress(received, total);
    });
    
    connect(m_downloadReply, &QNetworkReply::finished, this, [=](){
        if (!m_downloadReply) return;

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_downloadReply->readAll());
                file.close();
                // Export는 재생하지 않고 성공 메시지 (여기서는 downloadFinished 대신 별도 처리가 없으므로 그냥 둠? 
                // 아니면 downloadFinished를 QML에서 구분하도록 함? -> QML에서 처리)
                // QML에서 "파일 저장 완료" 알림을 띄우기 위해 downloadFinished 활용하되, Path를 주면 됨.
                emit downloadFinished(savePath); // 완료 시그널
            } else {
                emit downloadError("Failed to ensure file: " + file.errorString());
            }
        } else if (m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
             emit downloadError(m_downloadReply->errorString());
        }
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    });
}

void Backend::checkStorage() {
    QUrl url(serverUrl() + "/system/storage");
    QNetworkRequest request(url);
    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){ onStorageReply(reply); });
}

void Backend::onStorageReply(QNetworkReply *reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            double totalBytes = obj["total_bytes"].toDouble();
            double usedBytes = obj["used_bytes"].toDouble();
            
            if(totalBytes > 0) {
                m_storagePercent = (int)((usedBytes / totalBytes) * 100.0);
                m_storageUsed = QString::number(usedBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                m_storageTotal = QString::number(totalBytes / 1024 / 1024 / 1024, 'f', 1) + " GB";
                
                emit storageChanged();
            }
        }
    }
    reply->deleteLater();
}

void Backend::onSessionTick() {
    if (!m_isLoggedIn) {
        m_sessionTimer->stop();
        return;
    }

    if (m_sessionRemainingSeconds > 0) {
        m_sessionRemainingSeconds--;
        emit sessionRemainingSecondsChanged();
    }

    if (m_sessionRemainingSeconds <= 0) {
        logout();
        emit sessionExpired();
    }
}
