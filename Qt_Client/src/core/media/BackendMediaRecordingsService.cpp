#include "internal/media/BackendMediaRecordingsService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

void BackendMediaRecordingsService::refreshRecordings(Backend *backend, BackendPrivate *state)
{
    QUrl url(backend->serverUrl() + "/recordings");
    QNetworkRequest request(url);
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);
    qDebug() << "Refreshing recordings from:" << url.toString();

    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "RECORDINGS_LIST");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        Q_UNUSED(state);
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            qDebug() << "Recordings response:" << data.left(200);

            const QJsonDocument doc = QJsonDocument::fromJson(data);
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
                emit backend->recordingsLoaded(fileList);
            } else {
                qDebug() << "Response has no 'files' array";
                emit backend->recordingsLoaded(QVariantList());
            }
        } else {
            qDebug() << "Refresh failed:" << reply->errorString();
            emit backend->recordingsLoadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void BackendMediaRecordingsService::deleteRecording(Backend *backend, BackendPrivate *state, QString name)
{
    QUrl url(backend->serverUrl() + "/recordings");
    QUrlQuery query;
    query.addQueryItem("file", name);
    url.setQuery(query);

    QNetworkRequest request(url);
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);

    QNetworkReply *reply = state->m_manager->deleteResource(request);
    backend->attachIgnoreSslErrors(reply, "RECORDINGS_DELETE");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            emit backend->deleteSuccess();
            BackendMediaRecordingsService::refreshRecordings(backend, state);
        } else {
            emit backend->deleteFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

void BackendMediaRecordingsService::renameRecording(Backend *backend, BackendPrivate *state, QString oldName, QString newName)
{
    QUrl url(backend->serverUrl() + "/recordings/rename");
    QNetworkRequest request(url);
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["oldName"] = oldName;
    json["newName"] = newName;
    const QJsonDocument doc(json);

    QNetworkReply *reply = state->m_manager->put(request, doc.toJson());
    backend->attachIgnoreSslErrors(reply, "RECORDINGS_RENAME");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            emit backend->renameSuccess();
            BackendMediaRecordingsService::refreshRecordings(backend, state);
        } else {
            emit backend->renameFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

QString BackendMediaRecordingsService::getStreamUrl(Backend *backend, QString fileName)
{
    QUrl url(backend->serverUrl() + "/stream");
    QUrlQuery query;
    query.addQueryItem("file", fileName);
    url.setQuery(query);
    return url.toString();
}

void BackendMediaRecordingsService::downloadAndPlay(Backend *backend, BackendPrivate *state, QString fileName)
{
    qDebug() << "Backend::downloadAndPlay called for:" << fileName;
    if (state->m_downloadReply) {
        state->m_downloadReply->abort();
        state->m_downloadReply->deleteLater();
        state->m_downloadReply = nullptr;
    }

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString localName = QFileInfo(fileName).fileName();
    state->m_tempFilePath = tempDir + "/" + (localName.isEmpty() ? QString("recording.mp4") : localName);
    if (QFile::exists(state->m_tempFilePath)) {
        QFile::remove(state->m_tempFilePath);
    }

    const QUrl url(getStreamUrl(backend, fileName));
    QNetworkRequest request(url);
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);
    state->m_downloadReply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(state->m_downloadReply, "RECORDINGS_DOWNLOAD_PLAY");

    QObject::connect(state->m_downloadReply, &QNetworkReply::downloadProgress, backend, [backend](qint64 received, qint64 total) {
        if (total > 0) {
            emit backend->downloadProgress(received, total);
        }
    });

    QObject::connect(state->m_downloadReply, &QNetworkReply::finished, backend, [backend, state]() {
        if (!state->m_downloadReply) {
            return;
        }

        if (state->m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(state->m_tempFilePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(state->m_downloadReply->readAll());
                file.close();
                emit backend->downloadFinished("file:///" + state->m_tempFilePath);
            } else {
                emit backend->downloadError("Failed to save file: " + file.errorString());
            }
        } else if (state->m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
            emit backend->downloadError(state->m_downloadReply->errorString());
        }

        state->m_downloadReply->deleteLater();
        state->m_downloadReply = nullptr;
    });
}

void BackendMediaRecordingsService::cancelDownload(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);
    if (state->m_downloadReply) {
        QNetworkReply *reply = state->m_downloadReply;
        state->m_downloadReply = nullptr;

        if (reply->isRunning()) {
            reply->abort();
        }
        reply->deleteLater();
        qDebug() << "Download cancelled by user";
    }
}

void BackendMediaRecordingsService::exportRecording(Backend *backend, BackendPrivate *state, QString fileName, QString savePath)
{
    cancelDownload(backend, state);
    state->m_tempFilePath = savePath;

    const QUrl url(getStreamUrl(backend, fileName));
    QNetworkRequest request(url);
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);
    state->m_downloadReply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(state->m_downloadReply, "RECORDINGS_EXPORT");

    QObject::connect(state->m_downloadReply, &QNetworkReply::downloadProgress, backend, [backend](qint64 received, qint64 total) {
        if (total > 0) {
            emit backend->downloadProgress(received, total);
        }
    });

    QObject::connect(state->m_downloadReply, &QNetworkReply::finished, backend, [backend, state, savePath]() {
        if (!state->m_downloadReply) {
            return;
        }

        if (state->m_downloadReply->error() == QNetworkReply::NoError) {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(state->m_downloadReply->readAll());
                file.close();
                emit backend->downloadFinished(savePath);
            } else {
                emit backend->downloadError("Failed to ensure file: " + file.errorString());
            }
        } else if (state->m_downloadReply->error() != QNetworkReply::OperationCanceledError) {
            emit backend->downloadError(state->m_downloadReply->errorString());
        }
        state->m_downloadReply->deleteLater();
        state->m_downloadReply = nullptr;
    });
}

