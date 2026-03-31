#include "internal/core/BackendCoreEventService.h"

#include "Backend.h"
#include "internal/core/BackendCoreEventLogActionService.h"
#include "internal/core/BackendCoreEventLogService.h"
#include "internal/core/Backend_p.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QVariantMap>
#include <QtGlobal>

namespace {

struct EventControlSpec
{
    int motor1Angle = 90;
    int motor2Angle = 90;
    int motor3Angle = 90;
    bool laserEnabled = false;
};

// Angle 제한 함수
int clampAngle(int angle)
{
    return qBound(0, angle, 180);
}

// emit 이벤트 알림 상태 Changed 처리 함수
void emitEventAlertStateChanged(Backend *backend)
{
    emit backend->eventAlertStateChanged();
}

// emit 이벤트 알림 Preset Changed 처리 함수
void emitEventAlertPresetChanged(Backend *backend)
{
    emit backend->eventAlertPresetChanged();
}

// emit 이벤트 알림 이력 Changed 처리 함수
void emitEventAlertHistoryChanged(Backend *backend)
{
    emit backend->eventAlertHistoryChanged();
}

// preset 제어 처리 함수
EventControlSpec presetControl(const BackendPrivate *state)
{
    EventControlSpec spec;
    spec.motor1Angle = clampAngle(state->m_eventAlertPresetMotor1Angle);
    spec.motor2Angle = clampAngle(state->m_eventAlertPresetMotor2Angle);
    spec.motor3Angle = clampAngle(state->m_eventAlertPresetMotor3Angle);
    spec.laserEnabled = state->m_eventAlertPresetLaserEnabled;
    return spec;
}

// 이벤트 제어 조회 함수
EventControlSpec currentEventControl(const BackendPrivate *state)
{
    EventControlSpec spec;
    spec.motor1Angle = clampAngle(state->m_eventAlertMotor1Angle);
    spec.motor2Angle = clampAngle(state->m_eventAlertMotor2Angle);
    spec.motor3Angle = clampAngle(state->m_eventAlertMotor3Angle);
    spec.laserEnabled = state->m_eventAlertLaserEnabled;
    return spec;
}

// 이벤트 제어 Override 정리 함수
void clearEventControlOverride(BackendPrivate *state)
{
    state->m_eventAlertHasControlOverride = false;
    state->m_eventAlertMotor1Angle = 90;
    state->m_eventAlertMotor2Angle = 90;
    state->m_eventAlertMotor3Angle = 90;
    state->m_eventAlertLaserEnabled = false;
}

// Angle 값 파싱 함수
bool parseAngleValue(const QJsonValue &value, int currentValue, int *target)
{
    if (!target) {
        return false;
    }

    bool ok = false;
    int parsed = currentValue;
    if (value.isDouble()) {
        parsed = value.toInt(currentValue);
        ok = true;
    } else if (value.isString()) {
        parsed = value.toString().trimmed().toInt(&ok);
    }

    if (!ok) {
        return false;
    }

    *target = clampAngle(parsed);
    return true;
}

// 불리언 값 파싱 함수
bool parseBoolValue(const QJsonValue &value, bool fallback)
{
    if (value.isBool()) {
        return value.toBool(fallback);
    }
    if (value.isDouble()) {
        return value.toInt(0) != 0;
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

// Unsigned Long 값 파싱 함수
qulonglong parseUnsignedLongLongValue(const QJsonValue &value, qulonglong fallback = 0)
{
    if (value.isDouble()) {
        const double number = value.toDouble(static_cast<double>(fallback));
        return number >= 0.0 ? static_cast<qulonglong>(number) : fallback;
    }
    if (value.isString()) {
        bool ok = false;
        const qulonglong parsed = value.toString().trimmed().toULongLong(&ok);
        if (ok) {
            return parsed;
        }
    }
    return fallback;
}

// 문자열 값 파싱 함수
QString parseStringValue(const QJsonObject &obj, const QString &key, const QString &fallback, bool lowercase = false)
{
    QString text = obj.value(key).toString().trimmed();
    if (text.isEmpty()) {
        text = fallback;
    }
    return lowercase ? text.toLower() : text;
}

// 제어 Keys 확인 함수
bool hasControlKeys(const QJsonObject &obj)
{
    return obj.contains("motor1Angle")
        || obj.contains("motor2Angle")
        || obj.contains("motor3Angle")
        || obj.contains("laserEnabled");
}

// 제어 요약 처리 함수
QString controlSummary(const EventControlSpec &spec)
{
    return QString("M1=%1, M2=%2, M3=%3, Laser=%4")
        .arg(spec.motor1Angle)
        .arg(spec.motor2Angle)
        .arg(spec.motor3Angle)
        .arg(spec.laserEnabled ? "ON" : "OFF");
}

// 제어 적용 함수
bool applyControl(Backend *backend, const EventControlSpec &spec, const QString &reason)
{
    bool ok = true;
    ok = backend->motorSetAngle(1, spec.motor1Angle) && ok;
    ok = backend->motorSetAngle(2, spec.motor2Angle) && ok;
    ok = backend->motorSetAngle(3, spec.motor3Angle) && ok;
    ok = backend->laserSetEnabled(spec.laserEnabled) && ok;

    emit backend->cameraControlMessage(
        ok
            ? QString("Event alert control requested (%1): %2").arg(reason, controlSummary(spec))
            : QString("Event alert control failed to start (%1): %2").arg(reason, controlSummary(spec)),
        !ok);
    return ok;
}

// 이벤트 Received At 포맷 함수
QString formatEventReceivedAt()
{
    // 새 이벤트 수신 시각을 사용자에게 그대로 보여주기 위한 문자열
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

// 열화상 이벤트 제목 포맷 함수
QString formatThermalEventTitle(const QJsonObject &obj, const QString &fallback)
{
    const QString title = parseStringValue(obj, "title", fallback, false);
    if (title.compare(QStringLiteral("Thermal hotspot detected"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("열화상 이상 고온 감지");
    }
    return title;
}

// 열화상 이벤트 메시지 포맷 함수
QString formatThermalEventMessage(const QJsonObject &obj, const QString &fallback)
{
    const QJsonObject thermalObj = obj.value("thermal").toObject();
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

    // 열화상 payload를 사람이 읽기 쉬운 한글 요약으로 변환
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
    return lines.join('\n');
}

QVariantMap makeEventHistoryItem(const QString &source,
                                 const QString &severity,
                                 const QString &title,
                                 const QString &body,
                                 const QString &receivedAt,
                                 qulonglong eventLogId,
                                 int frameId,
                                 const QString &eventType,
                                 bool autoControl,
                                 bool hasOverride,
                                 const EventControlSpec &overrideSpec)
{
    QVariantMap item;
    item.insert(QStringLiteral("id"), eventLogId);
    item.insert(QStringLiteral("source"), source);
    item.insert(QStringLiteral("severity"), severity);
    item.insert(QStringLiteral("title"), title);
    item.insert(QStringLiteral("message"), body);
    item.insert(QStringLiteral("receivedAt"), receivedAt);
    item.insert(QStringLiteral("frameId"), frameId);
    item.insert(QStringLiteral("eventType"), eventType);
    item.insert(QStringLiteral("autoControl"), autoControl);
    item.insert(QStringLiteral("actionRequested"), false);
    item.insert(QStringLiteral("hasOverride"), hasOverride);
    item.insert(QStringLiteral("motor1Angle"), overrideSpec.motor1Angle);
    item.insert(QStringLiteral("motor2Angle"), overrideSpec.motor2Angle);
    item.insert(QStringLiteral("motor3Angle"), overrideSpec.motor3Angle);
    item.insert(QStringLiteral("laserEnabled"), overrideSpec.laserEnabled);
    item.insert(QStringLiteral("actionType"), QString());
    item.insert(QStringLiteral("actionResult"), QString());
    item.insert(QStringLiteral("actionMessage"), QString());
    return item;
}

} // namespace

// mark 이벤트 알림 Read 처리 함수
void BackendCoreEventService::markEventAlertRead(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_eventAlertUnread) {
        return;
    }

    state->m_eventAlertUnread = false;
    emitEventAlertStateChanged(backend);
}

// 이벤트 알림 정리 함수
void BackendCoreEventService::clearEventAlert(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return;
    }

    state->m_eventAlertActive = false;
    state->m_eventAlertUnread = false;
    state->m_eventAlertSource.clear();
    state->m_eventAlertSeverity = "info";
    state->m_eventAlertTitle.clear();
    state->m_eventAlertMessage.clear();
    state->m_eventAlertReceivedAtText.clear();
    state->m_eventAlertLogId = 0;
    state->m_eventAlertFrameId = -1;
    state->m_eventAlertAutoControl = false;
    clearEventControlOverride(state);
    emitEventAlertStateChanged(backend);
}

void BackendCoreEventService::updateEventAlertPreset(Backend *backend,
                                                     BackendPrivate *state,
                                                     int motor1Angle,
                                                     int motor2Angle,
                                                     int motor3Angle,
                                                     bool laserEnabled)
{
    if (!backend || !state) {
        return;
    }

    const int nextMotor1 = clampAngle(motor1Angle);
    const int nextMotor2 = clampAngle(motor2Angle);
    const int nextMotor3 = clampAngle(motor3Angle);

    if (state->m_eventAlertPresetMotor1Angle == nextMotor1
        && state->m_eventAlertPresetMotor2Angle == nextMotor2
        && state->m_eventAlertPresetMotor3Angle == nextMotor3
        && state->m_eventAlertPresetLaserEnabled == laserEnabled) {
        return;
    }

    state->m_eventAlertPresetMotor1Angle = nextMotor1;
    state->m_eventAlertPresetMotor2Angle = nextMotor2;
    state->m_eventAlertPresetMotor3Angle = nextMotor3;
    state->m_eventAlertPresetLaserEnabled = laserEnabled;
    emitEventAlertPresetChanged(backend);
}

// 이벤트 알림 제어 적용 함수
bool BackendCoreEventService::applyEventAlertControl(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return false;
    }

    if (!state->m_eventAlertActive) {
        emit backend->cameraControlMessage("Event alert control failed: no active event", true);
        return false;
    }

    // 현재 이벤트 적용은 레이저 ON 후 비상 대피 시퀀스를 바로 요청
    const bool laserRequested = backend->laserSetEnabled(true);
    if (!laserRequested) {
        BackendCoreEventLogActionService::requestCurrentEventActionUpdate(
            backend,
            state,
            QStringLiteral("motor_emergency"),
            QStringLiteral("failed"),
            QStringLiteral("laser on request failed"));
        emit backend->cameraControlMessage("Event alert control failed: laser on request failed", true);
        return false;
    }

    const bool emergencyRequested = backend->motorEmergency();
    if (!emergencyRequested) {
        BackendCoreEventLogActionService::requestCurrentEventActionUpdate(
            backend,
            state,
            QStringLiteral("motor_emergency"),
            QStringLiteral("failed"),
            QStringLiteral("motor emergency request failed"));
        emit backend->cameraControlMessage("Event alert control failed: motor emergency request failed", true);
        return false;
    }

    BackendCoreEventLogActionService::requestCurrentEventActionUpdate(
        backend,
        state,
        QStringLiteral("motor_emergency"),
        QStringLiteral("requested"),
        QStringLiteral("laser on -> motor emergency requested"));
    emit backend->cameraControlMessage("Event alert control requested: laser on -> motor emergency", false);
    return true;
}

// 이벤트 알림 제어 중지 함수
bool BackendCoreEventService::stopEventAlertControl(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return false;
    }

    const bool stopRequested = backend->motorStopAll();
    BackendCoreEventLogActionService::requestCurrentEventActionUpdate(
        backend,
        state,
        QStringLiteral("motor_stopall"),
        stopRequested ? QStringLiteral("requested") : QStringLiteral("failed"),
        stopRequested
            ? QStringLiteral("motor stop all requested")
            : QStringLiteral("motor stop all request failed"));
    return stopRequested;
}

// 이벤트 알림 메시지 처리 함수
void BackendCoreEventService::handleEventAlertMessage(Backend *backend, BackendPrivate *state, const QByteArray &message)
{
    if (!backend || !state) {
        return;
    }

    const QByteArray trimmedMessage = message.trimmed();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmedMessage, &parseError);

    QString source = QStringLiteral("system");
    QString severity = QStringLiteral("info");
    QString title = QStringLiteral("이벤트 알림");
    QString body = QString::fromUtf8(trimmedMessage).trimmed();
    qulonglong eventLogId = 0;
    int eventFrameId = -1;
    QString eventType;
    bool autoControl = false;
    bool hasOverride = false;
    EventControlSpec overrideSpec = presetControl(state);

    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        source = parseStringValue(obj, "source", source, true);
        severity = parseStringValue(obj, "severity", severity, true);
        title = parseStringValue(obj, "title", title, false);
        body = parseStringValue(obj, "message", body.isEmpty() ? QStringLiteral("이벤트 메시지를 수신했습니다.") : body, false);
        eventLogId = parseUnsignedLongLongValue(obj.value("eventLogId"), 0);
        eventType = parseStringValue(obj, "eventType", eventType, false);
        autoControl = parseBoolValue(obj.value("autoControl"), false);

        if (source == QStringLiteral("thermal")) {
            // thermal 이벤트는 원본 영문 메시지 대신 UI용 한글 문구로 가공
            title = formatThermalEventTitle(obj, title);
            body = formatThermalEventMessage(obj, body);
            eventType = QStringLiteral("thermal_hotspot");
            eventFrameId = obj.value(QStringLiteral("thermal")).toObject().value(QStringLiteral("frameId")).toInt(-1);
        }

        if (obj.value("control").isObject()) {
            const QJsonObject controlObj = obj.value("control").toObject();
            if (hasControlKeys(controlObj)) {
                hasOverride = true;
                parseAngleValue(controlObj.value("motor1Angle"), overrideSpec.motor1Angle, &overrideSpec.motor1Angle);
                parseAngleValue(controlObj.value("motor2Angle"), overrideSpec.motor2Angle, &overrideSpec.motor2Angle);
                parseAngleValue(controlObj.value("motor3Angle"), overrideSpec.motor3Angle, &overrideSpec.motor3Angle);
                overrideSpec.laserEnabled = parseBoolValue(controlObj.value("laserEnabled"), overrideSpec.laserEnabled);
            }
        }
    } else if (!trimmedMessage.isEmpty() && parseError.error != QJsonParseError::NoError) {
        qWarning() << "[EVENT] invalid JSON payload:" << parseError.errorString();
    }

    if (body.isEmpty()) {
        body = QStringLiteral("이벤트 메시지를 수신했습니다.");
    }

    state->m_eventAlertActive = true;
    state->m_eventAlertUnread = true;
    state->m_eventAlertSource = source;
    state->m_eventAlertSeverity = severity;
    state->m_eventAlertTitle = title;
    state->m_eventAlertMessage = body;
    // 같은 창이 열려 있어도 새 이벤트 도착 여부를 바로 알 수 있게 시각을 갱신
    state->m_eventAlertReceivedAtText = formatEventReceivedAt();
    state->m_eventAlertLogId = eventLogId;
    state->m_eventAlertFrameId = eventFrameId;
    state->m_eventAlertAutoControl = autoControl;
    state->m_eventAlertHasControlOverride = hasOverride;
    if (hasOverride) {
        state->m_eventAlertMotor1Angle = overrideSpec.motor1Angle;
        state->m_eventAlertMotor2Angle = overrideSpec.motor2Angle;
        state->m_eventAlertMotor3Angle = overrideSpec.motor3Angle;
        state->m_eventAlertLaserEnabled = overrideSpec.laserEnabled;
    } else {
        clearEventControlOverride(state);
    }

    // 최근 이벤트를 시간순 목록으로 누적
    state->m_eventAlertHistory.prepend(makeEventHistoryItem(source,
                                                            severity,
                                                            title,
                                                            body,
                                                            state->m_eventAlertReceivedAtText,
                                                            eventLogId,
                                                            eventFrameId,
                                                            eventType,
                                                            autoControl,
                                                            hasOverride,
                                                            hasOverride ? overrideSpec : presetControl(state)));
    while (state->m_eventAlertHistory.size() > 50) {
        state->m_eventAlertHistory.removeLast();
    }

    qInfo() << "[EVENT] received:"
            << "source=" << state->m_eventAlertSource
            << "severity=" << state->m_eventAlertSeverity
            << "title=" << state->m_eventAlertTitle
            << "autoControl=" << state->m_eventAlertAutoControl
            << "hasOverride=" << state->m_eventAlertHasControlOverride;
    emitEventAlertStateChanged(backend);
    emitEventAlertHistoryChanged(backend);

    if (state->m_authToken.trimmed().length() > 0) {
        // 새 이벤트 직후 DB row id를 맞추기 위해 이력을 짧게 재조회한다.
        QTimer::singleShot(300, backend, [backend, state]() {
            BackendCoreEventLogService::loadEventHistory(backend, state, 50);
        });
    }

    if (state->m_eventAlertAutoControl) {
        applyEventAlertControl(backend, state);
    }
}
