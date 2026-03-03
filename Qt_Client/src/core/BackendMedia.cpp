#include "Backend.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <algorithm>
#include <QSet>
#include <functional>

namespace {
struct StorageParseResult {
    bool ok = false;
    double totalBytes = 0.0;
    double usedBytes = 0.0;
};

QString formatStorageBytes(double bytes) {
    const double b = std::max(0.0, bytes);
    if (b < 1024.0) {
        return QString::number(static_cast<qint64>(b)) + " B";
    }
    const double kb = b / 1024.0;
    if (kb < 1024.0) {
        return QString::number(kb, 'f', 1) + " KB";
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QString::number(mb, 'f', 1) + " MB";
    }
    const double gb = mb / 1024.0;
    if (gb < 1024.0) {
        return QString::number(gb, 'f', 1) + " GB";
    }
    const double tb = gb / 1024.0;
    return QString::number(tb, 'f', 2) + " TB";
}

bool keyHasUnitToken(const QString &keyLower, const QString &unit) {
    static const QString seps = "._-";
    const QString u = unit.toLower();
    if (keyLower == u) return true;
    if (keyLower.endsWith(u)) {
        const int pos = keyLower.size() - u.size() - 1;
        if (pos < 0 || seps.contains(keyLower.at(pos))) return true;
    }
    if (keyLower.startsWith(u) && keyLower.size() > u.size()) {
        if (seps.contains(keyLower.at(u.size()))) return true;
    }
    return keyLower.contains("." + u + ".")
        || keyLower.contains("_" + u + "_")
        || keyLower.contains("-" + u + "-");
}

double normalizeToBytes(const QString &keyLower, double value) {
    if (value <= 0.0) return 0.0;
    if (keyHasUnitToken(keyLower, "tb")) return value * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    if (keyHasUnitToken(keyLower, "gb")) return value * 1024.0 * 1024.0 * 1024.0;
    if (keyHasUnitToken(keyLower, "mb")) return value * 1024.0 * 1024.0;
    if (keyHasUnitToken(keyLower, "kb")) return value * 1024.0;

    // SUNAPI 장비 일부는 단위 없이 57.02, 12.24 같은 GB 스케일 값을 반환
    // 저장소 용량 범위(<= 8TB)에서 단위 미지정 수치는 GB로 간주
    if (value > 0.0 && value <= 8192.0) {
        return value * 1024.0 * 1024.0 * 1024.0;
    }

    // 장비에 따라 57024, 44797처럼 MB 단위 정수값이 단위 없이 전달되는 경우 보정
    // 예: 57024 MB ~= 55.7 GB
    if (value >= 1024.0 && value <= (8.0 * 1024.0 * 1024.0)) {
        return value * 1024.0 * 1024.0;
    }

    return value;
}

void collectNumbersFromJson(const QJsonValue &value,
                            const QString &pathLower,
                            double &totalBytes,
                            double &usedBytes,
                            double &freeBytes) {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString nextPath = pathLower.isEmpty()
                    ? it.key().toLower()
                    : (pathLower + "." + it.key().toLower());
            collectNumbersFromJson(it.value(), nextPath, totalBytes, usedBytes, freeBytes);
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            collectNumbersFromJson(arr.at(i), pathLower + QString(".%1").arg(i), totalBytes, usedBytes, freeBytes);
        }
        return;
    }

    if (!value.isDouble()) return;
    const double v = value.toDouble();
    if (v <= 0.0) return;

    const double bytes = normalizeToBytes(pathLower, v);
    if (pathLower.contains("total") || pathLower.contains("capacity") || pathLower.contains("size")) {
        totalBytes = std::max(totalBytes, bytes);
    }
    if (pathLower.contains("used") || pathLower.contains("usage")) {
        usedBytes = std::max(usedBytes, bytes);
    }
    if (pathLower.contains("free") || pathLower.contains("avail")) {
        freeBytes = std::max(freeBytes, bytes);
    }
}

QList<QUrl> buildStorageCandidateUrls(const QMap<QString, QString> &env) {
    QList<QUrl> out;
    QSet<QString> dedup;

    const QString host = env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) return out;

    const QString schemeRaw = env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    const QString cgiEnv = env.value("SUNAPI_STORAGE_CGI", "recording.cgi").trimmed();
    const QString submenuEnv = env.value("SUNAPI_STORAGE_SUBMENU", "storage").trimmed();
    const QString actionEnv = env.value("SUNAPI_STORAGE_ACTION", "view").trimmed();
    const QString extraQuery = env.value("SUNAPI_STORAGE_QUERY").trimmed();

    auto addCandidate = [&](const QString &cgi, const QString &submenu, const QString &action, const QString &extra = QString()) {
        if (cgi.isEmpty() || submenu.isEmpty() || action.isEmpty()) return;
        QUrl url;
        url.setScheme(scheme);
        url.setHost(host);
        if (port > 0) url.setPort(port);
        url.setPath(QString("/stw-cgi/%1").arg(cgi));

        QUrlQuery q;
        q.addQueryItem("msubmenu", submenu);
        q.addQueryItem("action", action);
        const QString eq = extra.trimmed();
        if (!eq.isEmpty()) {
            const QStringList pairs = eq.split('&', Qt::SkipEmptyParts);
            for (const QString &pair : pairs) {
                const int sep = pair.indexOf('=');
                if (sep > 0) q.addQueryItem(pair.left(sep), pair.mid(sep + 1));
                else q.addQueryItem(pair, QString());
            }
        }
        url.setQuery(q);
        const QString key = url.toString();
        if (!dedup.contains(key)) {
            dedup.insert(key);
            out.push_back(url);
        }
    };

    // 1) .env 사용자 지정 후보
    addCandidate(cgiEnv, submenuEnv, actionEnv, extraQuery);

    // 2) 장비별 자주 쓰이는 후보
    addCandidate("recording.cgi", "storagestatus", "view");
    addCandidate("recording.cgi", "storageinfo", "view");
    addCandidate("recording.cgi", "storage", "view", "Storage=SD");
    addCandidate("recording.cgi", "storage", "view", "Channel=0");
    addCandidate("recording.cgi", "storage", "view", "Storage=SD&Channel=0");
    addCandidate("system.cgi", "storage", "view");
    addCandidate("system.cgi", "storageinfo", "view");
    addCandidate("eventstatus.cgi", "storage", "view");

    return out;
}

StorageParseResult parseStoragePayload(const QByteArray &payload) {
    StorageParseResult out;
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.isEmpty()) return out;

    // 1) JSON 응답 파싱
    {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
        if (!doc.isNull()) {
            double totalBytes = 0.0;
            double usedBytes = 0.0;
            double freeBytes = 0.0;
            collectNumbersFromJson(doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()),
                                   QString(),
                                   totalBytes,
                                   usedBytes,
                                   freeBytes);
            if (totalBytes > 0.0) {
                if (usedBytes <= 0.0 && freeBytes > 0.0) {
                    usedBytes = std::max(0.0, totalBytes - freeBytes);
                }
                if (usedBytes >= 0.0) {
                    out.ok = true;
                    out.totalBytes = totalBytes;
                    out.usedBytes = std::min(totalBytes, std::max(0.0, usedBytes));
                    return out;
                }
            }
        }
    }

    // 2) Key=Value 텍스트 응답 파싱 (SUNAPI 구형 포맷 대응)
    {
        const QString body = QString::fromUtf8(trimmed);
        const QRegularExpression kvRe("([A-Za-z0-9_\\.]+)\\s*=\\s*([0-9]+(?:\\.[0-9]+)?)");
        QRegularExpressionMatchIterator it = kvRe.globalMatch(body);

        double totalBytes = 0.0;
        double usedBytes = 0.0;
        double freeBytes = 0.0;

        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString keyLower = m.captured(1).toLower();
            const double raw = m.captured(2).toDouble();
            const double bytes = normalizeToBytes(keyLower, raw);

            if (keyLower.contains("total") || keyLower.contains("capacity") || keyLower.contains("size")) {
                totalBytes = std::max(totalBytes, bytes);
            }
            if (keyLower.contains("used") || keyLower.contains("usage")) {
                usedBytes = std::max(usedBytes, bytes);
            }
            if (keyLower.contains("free") || keyLower.contains("avail")) {
                freeBytes = std::max(freeBytes, bytes);
            }
        }

        if (totalBytes > 0.0) {
            if (usedBytes <= 0.0 && freeBytes > 0.0) {
                usedBytes = std::max(0.0, totalBytes - freeBytes);
            }
            if (usedBytes >= 0.0) {
                out.ok = true;
                out.totalBytes = totalBytes;
                out.usedBytes = std::min(totalBytes, std::max(0.0, usedBytes));
            }
        }
    }

    return out;
}
} // namespace

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
            qDebug() << "Refesh Failed:" << reply->errorString();
            emit recordingsLoadFailed(reply->errorString());
        }
        reply->deleteLater();
    });
}

// 선택 녹화 파일 삭제
void Backend::deleteRecording(QString name) {
    QMap<QString, QString> query;
    query.insert("file", name);
    QNetworkRequest request = makeApiJsonRequest("/recordings", query);
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
    QNetworkRequest request = makeApiJsonRequest("/recordings/rename");

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
    QMap<QString, QString> query;
    query.insert("file", fileName);
    return buildApiUrl("/stream", query).toString();
}

// 녹화 파일 임시 저장 후 재생
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
        // 기존 임시 파일 선삭제
        QFile::remove(m_tempFilePath);
    }
    
    QMap<QString, QString> query;
    query.insert("file", fileName);
    QNetworkRequest request = makeApiJsonRequest("/stream", query);
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

// 녹화 파일 지정 경로 내보내기
void Backend::exportRecording(QString fileName, QString savePath) {
    cancelDownload();
    m_tempFilePath = savePath;

    QMap<QString, QString> query;
    query.insert("file", fileName);
    QNetworkRequest request = makeApiJsonRequest("/stream", query);
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

// 카메라(SUNAPI) 스토리지 사용량 조회
void Backend::checkStorage() {
    const QList<QUrl> candidates = buildStorageCandidateUrls(m_env);
    if (candidates.isEmpty()) {
        return;
    }

    const auto issueAt = std::make_shared<std::function<void(int)>>();
    *issueAt = [this, candidates, issueAt](int index) {
        if (index < 0 || index >= candidates.size()) {
            return;
        }

        const QUrl url = candidates.at(index);
        const auto reqTimer = std::make_shared<QElapsedTimer>();
        reqTimer->start();
        QNetworkRequest request(url);
        applySslIfNeeded(request);
        QNetworkReply *reply = m_manager->get(request);
        attachIgnoreSslErrors(reply, "SUNAPI_STORAGE_CHECK");

        connect(reply, &QNetworkReply::finished, this, [this, candidates, index, reply, issueAt, url, reqTimer]() {
            const QByteArray data = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->error() == QNetworkReply::NoError) {
                const StorageParseResult parsed = parseStoragePayload(data);
                if (parsed.ok && parsed.totalBytes > 0.0) {
                    const double totalBytes = parsed.totalBytes;
                    const double usedBytes = parsed.usedBytes;
                    m_storagePercent = static_cast<int>((usedBytes / totalBytes) * 100.0);
                    m_storageUsed = formatStorageBytes(usedBytes);
                    m_storageTotal = formatStorageBytes(totalBytes);
                    qInfo() << "[SUNAPI][STORAGE] parsed"
                            << "url=" << url
                            << "usedBytes=" << usedBytes
                            << "totalBytes=" << totalBytes
                            << "display=" << m_storageUsed << "/" << m_storageTotal;
                    emit storageChanged();
                    setLatency(static_cast<int>(reqTimer->elapsed()));
                    reply->deleteLater();
                    return;
                }
            }

            const bool hasNext = (index + 1) < candidates.size();
            if (hasNext) {
                reply->deleteLater();
                (*issueAt)(index + 1);
                return;
            }

            qWarning() << "[SUNAPI][STORAGE] parse failed (all candidates)"
                       << "lastUrl=" << reply->request().url()
                       << "status=" << status
                       << "body=" << QString::fromUtf8(data.left(220));
            reply->deleteLater();
        });
    };

    (*issueAt)(0);
}

