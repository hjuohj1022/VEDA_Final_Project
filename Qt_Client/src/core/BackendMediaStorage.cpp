#include "Backend.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
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
    // 로그인 전에는 Crow SUNAPI API 호출을 생략
    if (m_authToken.trimmed().isEmpty()) {
        return;
    }
    const auto reqTimer = std::make_shared<QElapsedTimer>();
    reqTimer->start();

    QNetworkRequest request = makeApiJsonRequest("/api/sunapi/storage");
    applyAuthIfNeeded(request);
    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI_STORAGE_CHECK");

    connect(reply, &QNetworkReply::finished, this, [this, reply, reqTimer]() {
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
        m_storagePercent = static_cast<int>((usedBytes / totalBytes) * 100.0);
        m_storageUsed = formatStorageBytes(usedBytes);
        m_storageTotal = formatStorageBytes(totalBytes);
        qInfo() << "[SUNAPI][STORAGE] parsed"
                << "url=" << reply->request().url()
                << "usedBytes=" << usedBytes
                << "totalBytes=" << totalBytes
                << "display=" << m_storageUsed << "/" << m_storageTotal;
        emit storageChanged();
        setLatency(static_cast<int>(reqTimer->elapsed()));
        reply->deleteLater();
    });
}
