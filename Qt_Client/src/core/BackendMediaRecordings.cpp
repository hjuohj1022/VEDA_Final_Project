#include "Backend.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

// 서버 녹화 목록 조회
void Backend::refreshRecordings() {
    QUrl url(serverUrl() + "/recordings");
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qDebug() << "Refreshing recordings from:" << url.toString();
    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "RECORDINGS_LIST");
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            qDebug() << "Recordings Response:" << data.left(200);
            QJsonDocument doc = QJsonDocument::fromJson(data);
            const QJsonObject rootObj = doc.object();
            if (!doc.isNull() && rootObj.contains("files")) {
                const QJsonArray arr = rootObj.value("files").toArray();
                qDebug() << "Found" << arr.size() << "files";

                QVariantList fileList;
                for (const QJsonValue &val : arr) {
                    QVariantMap fileMap;
                    if (val.isObject()) {
                        const QJsonObject obj = val.toObject();
                        fileMap["name"] = obj.value("name").toString();
                        fileMap["size"] = obj.value("size").toVariant().toLongLong();
                    } else if (val.isString()) {
                        fileMap["name"] = val.toString();
                        fileMap["size"] = 0;
                    }

                    if (!fileMap.value("name").toString().isEmpty()) {
                        fileList.append(fileMap);
                    }
                }
                emit recordingsLoaded(fileList);
            } else {
                qDebug() << "Response has no 'files' array";
                emit recordingsLoaded(QVariantList());
            }
        } else {
            qDebug() << "Refresh Failed:" << reply->errorString();
            emit recordingsLoadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

// 선택 녹화 파일 삭제
void Backend::deleteRecording(QString name) {
    QUrl url(serverUrl() + "/recordings");
    QUrlQuery query;
    query.addQueryItem("file", name);
    url.setQuery(query);
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    QNetworkReply *reply = m_manager->deleteResource(request);
    attachIgnoreSslErrors(reply, "RECORDINGS_DELETE");
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit deleteSuccess();
            refreshRecordings();
        } else {
            emit deleteFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

// 선택 녹화 파일 이름 변경
void Backend::renameRecording(QString oldName, QString newName) {
    QUrl url(serverUrl() + "/recordings/rename");
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["oldName"] = oldName;
    json["newName"] = newName;
    QJsonDocument doc(json);
    QNetworkReply *reply = m_manager->put(request, doc.toJson());
    attachIgnoreSslErrors(reply, "RECORDINGS_RENAME");
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() == QNetworkReply::NoError) {
            emit renameSuccess();
            refreshRecordings();
        } else {
            emit renameFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

// 녹화 파일 스트림 URL 생성
QString Backend::getStreamUrl(QString fileName) {
    QUrl url(serverUrl() + "/stream");
    QUrlQuery query;
    query.addQueryItem("file", fileName);
    url.setQuery(query);
    return url.toString();
}

// 녹화 파일 다운로드 후 재생
void Backend::downloadAndPlay(QString fileName) {
    qDebug() << "Backend::downloadAndPlay called for:" << fileName;
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString localName = QFileInfo(fileName).fileName();
    m_tempFilePath = tempDir + "/" + (localName.isEmpty() ? QString("recording.mp4") : localName);
    if (QFile::exists(m_tempFilePath)) {
        // 기존 임시 파일 삭제
        QFile::remove(m_tempFilePath);
    }
    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    m_downloadReply = m_manager->get(request);
    attachIgnoreSslErrors(m_downloadReply, "RECORDINGS_DOWNLOAD_PLAY");

    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [=](qint64 received, qint64 total){
        if (total > 0) {
            emit downloadProgress(received, total);
        }
    });

    connect(m_downloadReply, &QNetworkReply::finished, this, [=](){
        if (!m_downloadReply) return;

        if (m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(m_tempFilePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(m_downloadReply->readAll());
                file.close();
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

// 진행 중 다운로드 취소
void Backend::cancelDownload() {
    if (m_downloadReply) {
        QNetworkReply *reply = m_downloadReply;
        m_downloadReply = nullptr;

        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
        qDebug() << "Download cancelled by user";
    }
}

// 녹화 파일 내보내기
void Backend::exportRecording(QString fileName, QString savePath) {
    cancelDownload();
    m_tempFilePath = savePath;

    QUrl url = QUrl(getStreamUrl(fileName));
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    m_downloadReply = m_manager->get(request);
    attachIgnoreSslErrors(m_downloadReply, "RECORDINGS_EXPORT");

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
                emit downloadFinished(savePath);
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

