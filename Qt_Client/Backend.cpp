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

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this)); // 쿠키 저장소 추가 (세션 유지)
    loadEnv();
    
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
QString Backend::rtspIp() const { return m_env.value("RTSP_IP", "127.0.0.1"); }
QString Backend::rtspPort() const { return m_env.value("RTSP_PORT", "8554"); }

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
            emit isLoggedInChanged();
            emit userIdChanged();
            emit loginSuccess();
        } else {
            emit loginFailed(reply->errorString());
        }
        reply->deleteLater();
    });
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
