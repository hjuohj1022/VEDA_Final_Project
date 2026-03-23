#include "internal/core/BackendCoreEventService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

namespace {

struct EventControlSpec
{
    int motor1Angle = 90;
    int motor2Angle = 90;
    int motor3Angle = 90;
    bool laserEnabled = false;
};

int clampAngle(int angle)
{
    return qBound(0, angle, 180);
}

void emitEventAlertStateChanged(Backend *backend)
{
    emit backend->eventAlertStateChanged();
}

void emitEventAlertPresetChanged(Backend *backend)
{
    emit backend->eventAlertPresetChanged();
}

EventControlSpec presetControl(const BackendPrivate *state)
{
    EventControlSpec spec;
    spec.motor1Angle = clampAngle(state->m_eventAlertPresetMotor1Angle);
    spec.motor2Angle = clampAngle(state->m_eventAlertPresetMotor2Angle);
    spec.motor3Angle = clampAngle(state->m_eventAlertPresetMotor3Angle);
    spec.laserEnabled = state->m_eventAlertPresetLaserEnabled;
    return spec;
}

EventControlSpec currentEventControl(const BackendPrivate *state)
{
    EventControlSpec spec;
    spec.motor1Angle = clampAngle(state->m_eventAlertMotor1Angle);
    spec.motor2Angle = clampAngle(state->m_eventAlertMotor2Angle);
    spec.motor3Angle = clampAngle(state->m_eventAlertMotor3Angle);
    spec.laserEnabled = state->m_eventAlertLaserEnabled;
    return spec;
}

void clearEventControlOverride(BackendPrivate *state)
{
    state->m_eventAlertHasControlOverride = false;
    state->m_eventAlertMotor1Angle = 90;
    state->m_eventAlertMotor2Angle = 90;
    state->m_eventAlertMotor3Angle = 90;
    state->m_eventAlertLaserEnabled = false;
}

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

QString parseStringValue(const QJsonObject &obj, const QString &key, const QString &fallback, bool lowercase = false)
{
    QString text = obj.value(key).toString().trimmed();
    if (text.isEmpty()) {
        text = fallback;
    }
    return lowercase ? text.toLower() : text;
}

bool hasControlKeys(const QJsonObject &obj)
{
    return obj.contains("motor1Angle")
        || obj.contains("motor2Angle")
        || obj.contains("motor3Angle")
        || obj.contains("laserEnabled");
}

QString controlSummary(const EventControlSpec &spec)
{
    return QString("M1=%1, M2=%2, M3=%3, Laser=%4")
        .arg(spec.motor1Angle)
        .arg(spec.motor2Angle)
        .arg(spec.motor3Angle)
        .arg(spec.laserEnabled ? "ON" : "OFF");
}

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

} // namespace

void BackendCoreEventService::markEventAlertRead(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state || !state->m_eventAlertUnread) {
        return;
    }

    state->m_eventAlertUnread = false;
    emitEventAlertStateChanged(backend);
}

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

bool BackendCoreEventService::applyEventAlertControl(Backend *backend, BackendPrivate *state)
{
    if (!backend || !state) {
        return false;
    }

    if (!state->m_eventAlertActive) {
        emit backend->cameraControlMessage("Event alert control failed: no active event", true);
        return false;
    }

    const EventControlSpec spec = state->m_eventAlertHasControlOverride
                                      ? currentEventControl(state)
                                      : presetControl(state);
    const QString reason = state->m_eventAlertHasControlOverride
                               ? QStringLiteral("payload override")
                               : QStringLiteral("saved preset");
    return applyControl(backend, spec, reason);
}

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
    QString title = QStringLiteral("Event Alert");
    QString body = QString::fromUtf8(trimmedMessage).trimmed();
    bool autoControl = false;
    bool hasOverride = false;
    EventControlSpec overrideSpec = presetControl(state);

    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        source = parseStringValue(obj, "source", source, true);
        severity = parseStringValue(obj, "severity", severity, true);
        title = parseStringValue(obj, "title", title, false);
        body = parseStringValue(obj, "message", body.isEmpty() ? QStringLiteral("MQTT event received.") : body, false);
        autoControl = parseBoolValue(obj.value("autoControl"), false);

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
        body = QStringLiteral("MQTT event received.");
    }

    state->m_eventAlertActive = true;
    state->m_eventAlertUnread = true;
    state->m_eventAlertSource = source;
    state->m_eventAlertSeverity = severity;
    state->m_eventAlertTitle = title;
    state->m_eventAlertMessage = body;
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

    qInfo() << "[EVENT] received:"
            << "source=" << state->m_eventAlertSource
            << "severity=" << state->m_eventAlertSeverity
            << "title=" << state->m_eventAlertTitle
            << "autoControl=" << state->m_eventAlertAutoControl
            << "hasOverride=" << state->m_eventAlertHasControlOverride;
    emitEventAlertStateChanged(backend);

    if (state->m_eventAlertAutoControl) {
        applyEventAlertControl(backend, state);
    }
}
