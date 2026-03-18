#include "internal/cctv/BackendMotorControlService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

bool isValidMotor(int motor) // 모터 번호(1~3) 유효성 검증
{
    return (motor >= 1) && (motor <= 3);
}

QString normalizeDirection(const QString &direction) // 방향 문자열 소문자 정규화
{
    return direction.trimmed().toLower();
}

bool isValidDirection(const QString &direction) // 방향 값(left/right) 유효성 검증
{
    return (direction == "left") || (direction == "right");
}

bool checkAuthReady(Backend *backend, BackendPrivate *state, const QString &action) // 로그인 토큰 상태 검증
{
    if (!backend || !state) {
        return false;
    }
    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->cameraControlMessage(QString("%1 failed: login required").arg(action), true);
        return false;
    }
    return true;
}

bool parseMotorApiOk(const QString &body, QString *statusText, QString *responseText) // 모터 API JSON 응답 파싱
{
    bool ok = true;
    QString status;
    QString response = body.trimmed();

    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains("ok") && obj.value("ok").isBool()) {
            ok = obj.value("ok").toBool();
        }
        if (obj.contains("status") && obj.value("status").isString()) {
            status = obj.value("status").toString();
            if (!status.isEmpty() && status.compare("OK", Qt::CaseInsensitive) != 0) {
                ok = false;
            }
        }
        if (obj.contains("response") && obj.value("response").isString()) {
            response = obj.value("response").toString().trimmed();
        } else if (obj.contains("message") && obj.value("message").isString()) {
            response = obj.value("message").toString().trimmed();
        }
    }

    if (statusText) {
        *statusText = status;
    }
    if (responseText) {
        *responseText = response;
    }
    return ok;
}

bool sendMotorPost(Backend *backend,
                   BackendPrivate *state,
                   const QString &path,
                   const QJsonObject &payload,
                   const QString &actionLabel) // 공통 POST 호출 처리
{
    if (!checkAuthReady(backend, state, actionLabel)) {
        return false;
    }

    QNetworkRequest request = backend->makeApiJsonRequest(path);
    backend->applyAuthIfNeeded(request);

    const QByteArray body = payload.isEmpty()
                                ? QByteArray()
                                : QJsonDocument(payload).toJson(QJsonDocument::Compact);

    qInfo() << "[MOTOR] request:" << actionLabel
            << "url=" << request.url().toString()
            << "body=" << QString::fromUtf8(body);

    QNetworkReply *reply = state->m_manager->post(request, body);
    backend->attachIgnoreSslErrors(reply, "MOTOR");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString bodyText = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString status;
            QString response;
            const bool apiOk = parseMotorApiOk(bodyText, &status, &response);
            if (apiOk) {
                const QString msg = response.isEmpty()
                                        ? QString("%1 success").arg(actionLabel)
                                        : QString("%1 success: %2").arg(actionLabel, response);
                emit backend->cameraControlMessage(msg, false);
            } else {
                const QString detail = response.isEmpty() ? bodyText : response;
                const QString msg = QString("%1 failed (%2): %3")
                                        .arg(actionLabel,
                                             status.isEmpty() ? QStringLiteral("API error") : status,
                                             detail);
                qWarning() << "[MOTOR]" << msg << "url=" << reply->request().url();
                emit backend->cameraControlMessage(msg, true);
            }
        } else {
            const QString msg = QString("%1 failed (HTTP %2): %3")
                                    .arg(actionLabel)
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[MOTOR]" << msg << "url=" << reply->request().url() << "body=" << bodyText.left(200);
            emit backend->cameraControlMessage(msg, true);
        }

        reply->deleteLater();
    });

    return true;
}

} // namespace

bool BackendMotorControlService::motorPress(Backend *backend, BackendPrivate *state, int motor, const QString &direction) // 모터 지정 방향 press 처리
{
    const QString dir = normalizeDirection(direction);
    if (!isValidMotor(motor)) {
        emit backend->cameraControlMessage("Motor press failed: invalid motor index (1~3)", true);
        return false;
    }
    if (!isValidDirection(dir)) {
        emit backend->cameraControlMessage("Motor press failed: invalid direction (left/right)", true);
        return false;
    }

    return sendMotorPost(backend,
                         state,
                         "/motor/control/press",
                         QJsonObject{
                             { "motor", motor },
                             { "direction", dir },
                         },
                         QString("Motor press m%1 %2").arg(motor).arg(dir));
}

bool BackendMotorControlService::motorRelease(Backend *backend, BackendPrivate *state, int motor) // 모터 press 상태 release 해제 처리
{
    if (!isValidMotor(motor)) {
        emit backend->cameraControlMessage("Motor release failed: invalid motor index (1~3)", true);
        return false;
    }

    return sendMotorPost(backend,
                         state,
                         "/motor/control/release",
                         QJsonObject{
                             { "motor", motor },
                         },
                         QString("Motor release m%1").arg(motor));
}

bool BackendMotorControlService::motorStop(Backend *backend, BackendPrivate *state, int motor) // 단일 모터 정지 처리
{
    if (!isValidMotor(motor)) {
        emit backend->cameraControlMessage("Motor stop failed: invalid motor index (1~3)", true);
        return false;
    }

    return sendMotorPost(backend,
                         state,
                         "/motor/control/stop",
                         QJsonObject{
                             { "motor", motor },
                         },
                         QString("Motor stop m%1").arg(motor));
}

bool BackendMotorControlService::motorSetAngle(Backend *backend, BackendPrivate *state, int motor, int angle) // 단일 모터 지정 각도 이동 처리
{
    if (!isValidMotor(motor)) {
        emit backend->cameraControlMessage("Motor set-angle failed: invalid motor index (1~3)", true);
        return false;
    }

    return sendMotorPost(backend,
                         state,
                         "/motor/control/set",
                         QJsonObject{
                             { "motor", motor },
                             { "angle", angle },
                         },
                         QString("Motor set m%1 angle=%2").arg(motor).arg(angle));
}

bool BackendMotorControlService::motorCenter(Backend *backend, BackendPrivate *state, int angle) // 전체 모터 동일 각도 센터 정렬 처리
{
    return sendMotorPost(backend,
                         state,
                         "/motor/control/center",
                         QJsonObject{
                             { "angle", angle },
                         },
                         QString("Motor center angle=%1").arg(angle));
}

bool BackendMotorControlService::motorStopAll(Backend *backend, BackendPrivate *state) // 전체 모터 일괄 정지 처리
{
    return sendMotorPost(backend,
                         state,
                         "/motor/control/stopall",
                         QJsonObject(),
                         "Motor stop all");
}
