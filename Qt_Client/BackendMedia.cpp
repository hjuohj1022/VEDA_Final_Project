#include "Backend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>

void Backend::refreshRecordings() {
    QString urlStr = QString("%1/recordings?user=%2").arg(serverUrl(), m_userId);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qDebug() << "Refreshing recordings for user:" << m_userId << "from:" << url.toString();
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
                    const QJsonObject obj = val.toObject();
                    QVariantMap fileMap;
                    fileMap["name"] = obj["name"].toString();
                    fileMap["size"] = obj["size"].toVariant().toLongLong();
                    fileList.append(fileMap);
                }
                emit recordingsLoaded(fileList);
            } else {
                qDebug() << "Response has no 'files' array";
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

QString Backend::getStreamUrl(QString fileName) {
    return QString("%1/stream?file=%2").arg(serverUrl(), fileName);
}

void Backend::downloadAndPlay(QString fileName) {
    qDebug() << "Backend::downloadAndPlay called for:" << fileName;
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_tempFilePath = tempDir + "/" + fileName;
    if (QFile::exists(m_tempFilePath)) {
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

void Backend::checkStorage() {
    QUrl url(serverUrl() + "/system/storage");
    QNetworkRequest request(url);
    applySslIfNeeded(request);
    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "STORAGE_CHECK");
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
