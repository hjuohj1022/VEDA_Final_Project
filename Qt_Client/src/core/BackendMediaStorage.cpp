#include "Backend.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <algorithm>
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

    // 단위 정보 없는 소수 값은 GB로 간주
    // 장치 범위(8TB 이하)에서 비정상 단위 혼선 방지
    if (value > 0.0 && value <= 8192.0) {
        return value * 1024.0 * 1024.0 * 1024.0;
    }

    // 큰 정수 값은 MB 입력으로 판단
    // 예: 57024 -> 55.7GB 환산
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

    // .env 기반 우선 후보 URL 추가
    addCandidate(cgiEnv, submenuEnv, actionEnv, extraQuery);

    // 장비별 대체 후보 URL 추가
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

    // JSON 응답 파싱
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

    // Key=Value 텍스트 응답 파싱
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

// 카메라 저장소 사용량 조회
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
