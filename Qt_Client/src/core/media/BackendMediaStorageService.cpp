#include "internal/media/BackendMediaStorageService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QDebug>
#include <algorithm>
#include <functional>
#include <memory>

namespace {
struct StorageParseResult {
    bool ok = false;
    double totalBytes = 0.0;
    double usedBytes = 0.0;
};

QString formatStorageBytes(double bytes)
{
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

bool keyHasUnitToken(const QString &keyLower, const QString &unit)
{
    static const QString seps = "._-";
    const QString u = unit.toLower();
    if (keyLower == u) {
        return true;
    }
    if (keyLower.endsWith(u)) {
        const int pos = keyLower.size() - u.size() - 1;
        if (pos < 0 || seps.contains(keyLower.at(pos))) {
            return true;
        }
    }
    if (keyLower.startsWith(u) && keyLower.size() > u.size()) {
        if (seps.contains(keyLower.at(u.size()))) {
            return true;
        }
    }
    return keyLower.contains("." + u + ".")
        || keyLower.contains("_" + u + "_")
        || keyLower.contains("-" + u + "-");
}

double normalizeToBytes(const QString &keyLower, double value)
{
    if (value <= 0.0) {
        return 0.0;
    }
    if (keyHasUnitToken(keyLower, "tb")) {
        return value * 1024.0 * 1024.0 * 1024.0 * 1024.0;
    }
    if (keyHasUnitToken(keyLower, "gb")) {
        return value * 1024.0 * 1024.0 * 1024.0;
    }
    if (keyHasUnitToken(keyLower, "mb")) {
        return value * 1024.0 * 1024.0;
    }
    if (keyHasUnitToken(keyLower, "kb")) {
        return value * 1024.0;
    }

    // Without explicit unit, first assume GB for small decimal/integer values.
    if (value > 0.0 && value <= 8192.0) {
        return value * 1024.0 * 1024.0 * 1024.0;
    }

    // Fallback: treat large integer-like values as MB.
    if (value >= 1024.0 && value <= (8.0 * 1024.0 * 1024.0)) {
        return value * 1024.0 * 1024.0;
    }

    return value;
}

void collectNumbersFromJson(const QJsonValue &value,
                            const QString &pathLower,
                            double &totalBytes,
                            double &usedBytes,
                            double &freeBytes)
{
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

    if (!value.isDouble()) {
        return;
    }
    const double v = value.toDouble();
    if (v <= 0.0) {
        return;
    }

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

StorageParseResult parseStoragePayload(const QByteArray &payload)
{
    StorageParseResult out;
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.isEmpty()) {
        return out;
    }

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

void BackendMediaStorageService::checkStorage(Backend *backend, BackendPrivate *state)
{
    if (state->m_authToken.trimmed().isEmpty()) {
        return;
    }

    const auto reqTimer = std::make_shared<QElapsedTimer>();
    reqTimer->start();
    const bool debugStorage = (state->m_env.value("STORAGE_DEBUG", "0").trimmed() == "1");

    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/storage");
    backend->applyAuthIfNeeded(request);
    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "SUNAPI_STORAGE_CHECK");

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, reqTimer, debugStorage]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[SUNAPI][STORAGE] crow api failed"
                       << "url=" << reply->request().url()
                       << "status=" << status
                       << "body=" << QString::fromUtf8(data.left(220));
            reply->deleteLater();
            return;
        }

        const StorageParseResult parsed = parseStoragePayload(data);
        if (!parsed.ok || parsed.totalBytes <= 0.0) {
            qWarning() << "[SUNAPI][STORAGE] parse failed from crow api"
                       << "url=" << reply->request().url()
                       << "status=" << status
                       << "body=" << QString::fromUtf8(data.left(220));
            reply->deleteLater();
            return;
        }

        const double totalBytes = parsed.totalBytes;
        const double usedBytes = parsed.usedBytes;
        state->m_storagePercent = static_cast<int>((usedBytes / totalBytes) * 100.0);
        state->m_storageUsed = formatStorageBytes(usedBytes);
        state->m_storageTotal = formatStorageBytes(totalBytes);
        if (debugStorage) {
            qInfo() << "[SUNAPI][STORAGE] parsed"
                    << "url=" << reply->request().url()
                    << "usedBytes=" << usedBytes
                    << "totalBytes=" << totalBytes
                    << "display=" << state->m_storageUsed << "/" << state->m_storageTotal;
        }
        emit backend->storageChanged();
        backend->setLatency(static_cast<int>(reqTimer->elapsed()));
        reply->deleteLater();
    });
}

