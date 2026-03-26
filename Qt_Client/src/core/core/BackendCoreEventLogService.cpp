#include "internal/core/BackendCoreEventLogService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

namespace {

void emitEventAlertHistoryChanged(Backend *backend)
{
    emit backend->eventAlertHistoryChanged();
}

void emitEventAlertStateChanged(Backend *backend)
{
    emit backend->eventAlertStateChanged();
}

QString normalizeString(const QJsonObject &obj, const QString &key, const QString &fallback = QString())
{
    const QString value = obj.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

bool parseBoolValue(const QJsonValue &value, bool fallback = false)
{
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == "1" || text == "true" || text == "yes" || text == "on") {
            return true;
        }
        if (text == "0" || text == "false" || text == "no" || text == "off") {
            return false;
        }
    }
    return fallback;
}

QString formatPersistedThermalTitle(const QString &fallback)
{
    if (fallback.compare(QStringLiteral("Thermal hotspot detected"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("열화상 이상 고온 감지");
    }
    return fallback;
}

QString formatPersistedThermalMessage(const QJsonObject &payload, const QString &fallback)
{
    const QJsonObject thermalObj = payload.value("thermal").toObject();
    if (thermalObj.isEmpty()) {
        return fallback;
    }

    const int frameId = thermalObj.value("frameId").toInt(0);
    const int signalValue = thermalObj.value("signalValue").toInt(0);
    const int activeThreshold = thermalObj.value("activeThreshold").toInt(0);
    const int hotAreaPixels = thermalObj.value("hotAreaPixels").toInt(0);
    const int maxValue = thermalObj.value("maxValue").toInt(0);
    const QJsonObject candidateObj = thermalObj.value("candidate").toObject();
    const int centerX = candidateObj.value("centerX").toInt(-1);
    const int centerY = candidateObj.value("centerY").toInt(-1);
    const int area = candidateObj.value("area").toInt(0);

    // DB에 저장된 thermal payload를 UI용 한글 요약으로 변환한다.
    QStringList lines;
    lines << QStringLiteral("열화상 ROI에서 이상 고온이 감지되었습니다.");
    if (frameId > 0) {
        lines << QStringLiteral("프레임: %1").arg(frameId);
    }
    if (signalValue > 0 || activeThreshold > 0) {
        lines << QStringLiteral("신호값: %1 / 임계값: %2").arg(signalValue).arg(activeThreshold);
    }
    if (maxValue > 0 || hotAreaPixels > 0) {
        lines << QStringLiteral("최대 온도값: %1 / 고온 영역: %2 px").arg(maxValue).arg(hotAreaPixels);
    }
    if (centerX >= 0 && centerY >= 0) {
        lines << QStringLiteral("감지 위치: (%1, %2) / 후보 면적: %3 px").arg(centerX).arg(centerY).arg(area);
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap makeHistoryItemFromLog(const QJsonObject &logObject)
{
    QString source = normalizeString(logObject, QStringLiteral("source"), QStringLiteral("system")).toLower();
    QString severity = normalizeString(logObject, QStringLiteral("severity"), QStringLiteral("info")).toLower();
    QString title = normalizeString(logObject, QStringLiteral("title"), QStringLiteral("이벤트 알림"));
    QString message = normalizeString(logObject,
                                      QStringLiteral("message"),
                                      QStringLiteral("이벤트 설명이 없습니다."));
    const QString occurredAt = normalizeString(logObject, QStringLiteral("occurred_at"));

    const QString payloadText = normalizeString(logObject, QStringLiteral("payload_json"));
    if (!payloadText.isEmpty()) {
        const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadText.toUtf8());
        if (payloadDoc.isObject() && source == QStringLiteral("thermal")) {
            title = formatPersistedThermalTitle(title);
            message = formatPersistedThermalMessage(payloadDoc.object(), message);
        }
    } else if (source == QStringLiteral("thermal")) {
        title = formatPersistedThermalTitle(title);
    }

    QVariantMap item;
    item.insert(QStringLiteral("id"), logObject.value(QStringLiteral("id")).toVariant());
    item.insert(QStringLiteral("source"), source);
    item.insert(QStringLiteral("severity"), severity);
    item.insert(QStringLiteral("title"), title);
    item.insert(QStringLiteral("message"), message);
    item.insert(QStringLiteral("receivedAt"), occurredAt);
    item.insert(QStringLiteral("autoControl"), parseBoolValue(logObject.value(QStringLiteral("action_requested")), false));
    item.insert(QStringLiteral("actionRequested"), parseBoolValue(logObject.value(QStringLiteral("action_requested")), false));
    item.insert(QStringLiteral("hasOverride"), false);
    item.insert(QStringLiteral("motor1Angle"), 90);
    item.insert(QStringLiteral("motor2Angle"), 90);
    item.insert(QStringLiteral("motor3Angle"), 90);
    item.insert(QStringLiteral("laserEnabled"), false);
    item.insert(QStringLiteral("frameId"), logObject.value(QStringLiteral("frame_id")).toVariant());
    item.insert(QStringLiteral("eventType"), normalizeString(logObject, QStringLiteral("event_type")));
    item.insert(QStringLiteral("actionType"), normalizeString(logObject, QStringLiteral("action_type")));
    item.insert(QStringLiteral("actionResult"), normalizeString(logObject, QStringLiteral("action_result")));
    item.insert(QStringLiteral("actionMessage"), normalizeString(logObject, QStringLiteral("action_message")));
    return item;
}

} // namespace

void BackendCoreEventLogService::loadEventHistory(Backend *backend, BackendPrivate *state, int limit)
{
    if (!backend || !state || !state->m_manager || state->m_authToken.trimmed().isEmpty()) {
        return;
    }

    if (state->m_eventLogHistoryReply && state->m_eventLogHistoryReply->isRunning()) {
        state->m_eventLogHistoryReply->abort();
    }

    QNetworkRequest request = backend->makeApiJsonRequest(QStringLiteral("/events"), {
        { QStringLiteral("limit"), QString::number(qBound(1, limit, 200)) }
    });
    backend->applyAuthIfNeeded(request);

    // 로그인 직후 최근 이벤트 목록을 복원한다.
    QNetworkReply *reply = state->m_manager->get(request);
    state->m_eventLogHistoryReply = reply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("EVENT_LOG_HISTORY"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (state->m_eventLogHistoryReply != reply) {
            reply->deleteLater();
            return;
        }

        state->m_eventLogHistoryReply = nullptr;

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (netError != QNetworkReply::NoError || statusCode != 200) {
            qWarning() << "[EVENT][HISTORY] load failed:"
                       << "status=" << statusCode
                       << "netError=" << netError
                       << "body=" << body;
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            qWarning() << "[EVENT][HISTORY] invalid response body";
            return;
        }

        const QJsonArray events = doc.object().value(QStringLiteral("events")).toArray();
        QVariantList history;
        history.reserve(events.size());
        for (const QJsonValue &value : events) {
            if (!value.isObject()) {
                continue;
            }
            history.append(makeHistoryItemFromLog(value.toObject()));
        }

        state->m_eventAlertHistory = history;
        syncCurrentEventLogIdFromHistory(backend, state);
        emitEventAlertHistoryChanged(backend);
        qInfo() << "[EVENT][HISTORY] loaded:" << history.size();
    });
}

bool BackendCoreEventLogService::deleteEventHistory(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_manager || state->m_authToken.trimmed().isEmpty()) {
        return false;
    }

    if (state->m_eventLogDeleteReply && state->m_eventLogDeleteReply->isRunning()) {
        state->m_eventLogDeleteReply->abort();
    }

    QNetworkRequest request = backend->makeApiJsonRequest(QStringLiteral("/events"));
    backend->applyAuthIfNeeded(request);

    // 서버에 저장된 이벤트 목록 전체 삭제를 요청한다.
    QNetworkReply *reply = state->m_manager->sendCustomRequest(
        request,
        QByteArrayLiteral("DELETE"),
        QByteArray());
    state->m_eventLogDeleteReply = reply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("EVENT_LOG_DELETE"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply]() {
        if (state->m_eventLogDeleteReply == reply) {
            state->m_eventLogDeleteReply = nullptr;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (netError != QNetworkReply::NoError || statusCode != 200) {
            qWarning() << "[EVENT][HISTORY] delete failed:"
                       << "status=" << statusCode
                       << "netError=" << netError
                       << "body=" << body;
            emit backend->cameraControlMessage("Event alert history delete failed", true);
            return;
        }

        state->m_eventAlertHistory.clear();
        state->m_eventAlertLogId = 0;
        emitEventAlertHistoryChanged(backend);
        emit backend->cameraControlMessage("Event alert history deleted", false);
    });

    return true;
}

bool BackendCoreEventLogService::deleteEventHistoryItem(Backend *backend,
                                                        BackendPrivate *state,
                                                        qulonglong eventLogId)
{
    if (!backend || !state || !state->m_manager || state->m_authToken.trimmed().isEmpty() || eventLogId == 0) {
        return false;
    }

    if (state->m_eventLogDeleteReply && state->m_eventLogDeleteReply->isRunning()) {
        state->m_eventLogDeleteReply->abort();
    }

    QNetworkRequest request = backend->makeApiJsonRequest(QStringLiteral("/events"), {
        { QStringLiteral("id"), QString::number(eventLogId) }
    });
    backend->applyAuthIfNeeded(request);

    // 선택한 이벤트 1건만 서버 목록에서 삭제한다.
    QNetworkReply *reply = state->m_manager->sendCustomRequest(
        request,
        QByteArrayLiteral("DELETE"),
        QByteArray());
    state->m_eventLogDeleteReply = reply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("EVENT_LOG_DELETE_ITEM"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, eventLogId]() {
        if (state->m_eventLogDeleteReply == reply) {
            state->m_eventLogDeleteReply = nullptr;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (netError != QNetworkReply::NoError || statusCode != 200) {
            qWarning() << "[EVENT][HISTORY] delete item failed:"
                       << "status=" << statusCode
                       << "netError=" << netError
                       << "body=" << body;
            emit backend->cameraControlMessage("Event alert item delete failed", true);
            return;
        }

        QVariantList history;
        history.reserve(state->m_eventAlertHistory.size());
        for (const QVariant &entry : state->m_eventAlertHistory) {
            const QVariantMap item = entry.toMap();
            if (item.value(QStringLiteral("id")).toULongLong() == eventLogId) {
                continue;
            }
            history.append(entry);
        }

        state->m_eventAlertHistory = history;
        if (state->m_eventAlertLogId == eventLogId) {
            state->m_eventAlertLogId = 0;
        }
        emitEventAlertHistoryChanged(backend);
        emit backend->cameraControlMessage("Event alert item deleted", false);
    });

    return true;
}

void BackendCoreEventLogService::syncCurrentEventLogIdFromHistory(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || state->m_eventAlertLogId != 0 || !state->m_eventAlertActive) {
        return;
    }

    const QString currentSource = state->m_eventAlertSource.trimmed().toLower();
    const QString currentTitle = state->m_eventAlertTitle.trimmed();
    const QString currentMessage = state->m_eventAlertMessage.trimmed();

    for (const QVariant &entry : state->m_eventAlertHistory) {
        const QVariantMap item = entry.toMap();
        const qulonglong eventLogId = item.value(QStringLiteral("id")).toULongLong();
        if (eventLogId == 0) {
            continue;
        }

        const QString source = item.value(QStringLiteral("source")).toString().trimmed().toLower();
        if (source != currentSource) {
            continue;
        }

        // frameId가 비어 있으면 매칭 비교에서 제외할 수 있게 -1로 본다.
        const QVariant frameIdValue = item.value(QStringLiteral("frameId"));
        const int frameId =
            (frameIdValue.isValid() && !frameIdValue.isNull()) ? frameIdValue.toInt() : -1;

        if (state->m_eventAlertFrameId >= 0 && frameId == state->m_eventAlertFrameId) {
            // 현재 수신 이벤트와 같은 row를 찾으면 log id를 연결한다.
            state->m_eventAlertLogId = eventLogId;
            return;
        }

        if (state->m_eventAlertFrameId < 0
            && item.value(QStringLiteral("title")).toString().trimmed() == currentTitle
            && item.value(QStringLiteral("message")).toString().trimmed() == currentMessage) {
            state->m_eventAlertLogId = eventLogId;
            return;
        }
    }
}

void BackendCoreEventLogService::clearCachedEventHistory(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return;
    }

    if (state->m_eventLogHistoryReply && state->m_eventLogHistoryReply->isRunning()) {
        state->m_eventLogHistoryReply->abort();
    }
    state->m_eventLogHistoryReply = nullptr;
    if (state->m_eventLogDeleteReply && state->m_eventLogDeleteReply->isRunning()) {
        state->m_eventLogDeleteReply->abort();
    }
    state->m_eventLogDeleteReply = nullptr;

    const bool hadHistory = !state->m_eventAlertHistory.isEmpty();
    const bool hadEventState =
        state->m_eventAlertActive
        || state->m_eventAlertUnread
        || !state->m_eventAlertSource.isEmpty()
        || state->m_eventAlertSeverity != QStringLiteral("info")
        || !state->m_eventAlertTitle.isEmpty()
        || !state->m_eventAlertMessage.isEmpty()
        || !state->m_eventAlertReceivedAtText.isEmpty()
        || state->m_eventAlertAutoControl
        || state->m_eventAlertHasControlOverride
        || state->m_eventAlertMotor1Angle != 90
        || state->m_eventAlertMotor2Angle != 90
        || state->m_eventAlertMotor3Angle != 90
        || state->m_eventAlertLaserEnabled;

    state->m_eventAlertHistory.clear();
    state->m_eventAlertActive = false;
    state->m_eventAlertUnread = false;
    state->m_eventAlertSource.clear();
    state->m_eventAlertSeverity = QStringLiteral("info");
    state->m_eventAlertTitle.clear();
    state->m_eventAlertMessage.clear();
    state->m_eventAlertReceivedAtText.clear();
    state->m_eventAlertLogId = 0;
    state->m_eventAlertFrameId = -1;
    state->m_eventAlertAutoControl = false;
    state->m_eventAlertHasControlOverride = false;
    state->m_eventAlertMotor1Angle = 90;
    state->m_eventAlertMotor2Angle = 90;
    state->m_eventAlertMotor3Angle = 90;
    state->m_eventAlertLaserEnabled = false;

    if (hadHistory) {
        emitEventAlertHistoryChanged(backend);
    }
    if (hadEventState) {
        emitEventAlertStateChanged(backend);
    }
}
