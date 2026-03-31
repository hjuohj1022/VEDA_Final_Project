#include "internal/core/BackendCoreEventLogActionService.h"

#include "Backend.h"
#include "internal/core/BackendCoreEventLogService.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariantMap>

namespace {

// 현재 이력 목록에서 같은 이벤트 row의 action 상태를 함께 갱신한다.
bool updateHistoryActionState(BackendPrivate *state,
                              qulonglong eventLogId,
                              const QString &actionType,
                              const QString &actionResult,
                              const QString &actionMessage)
{
    if (!state || eventLogId == 0) {
        return false;
    }

    bool changed = false;
    for (int index = 0; index < state->m_eventAlertHistory.size(); ++index) {
        QVariantMap item = state->m_eventAlertHistory.at(index).toMap();
        if (item.value(QStringLiteral("id")).toULongLong() != eventLogId) {
            continue;
        }

        item.insert(QStringLiteral("actionRequested"), true);
        item.insert(QStringLiteral("actionType"), actionType);
        item.insert(QStringLiteral("actionResult"), actionResult);
        item.insert(QStringLiteral("actionMessage"), actionMessage);
        state->m_eventAlertHistory[index] = item;
        changed = true;
    }
    return changed;
}

} // namespace

bool BackendCoreEventLogActionService::requestCurrentEventActionUpdate(Backend *backend,
                                                                       BackendPrivate *state,
                                                                       const QString &actionType,
                                                                       const QString &actionResult,
                                                                       const QString &actionMessage,
                                                                       bool allowRetry)
{
    if (!backend || !state || !state->m_manager || state->m_authToken.trimmed().isEmpty()) {
        return false;
    }

    BackendCoreEventLogService::syncCurrentEventLogIdFromHistory(backend, state);
    if (state->m_eventAlertLogId == 0) {
        if (allowRetry && state->m_eventAlertFrameId >= 0) {
            // 현재 이벤트 id가 아직 없으면 이력을 다시 불러온 뒤 한 번 더 시도한다.
            BackendCoreEventLogService::loadEventHistory(backend, state, 50);
            QTimer::singleShot(400, backend, [backend, state, actionType, actionResult, actionMessage]() {
                BackendCoreEventLogActionService::requestCurrentEventActionUpdate(
                    backend,
                    state,
                    actionType,
                    actionResult,
                    actionMessage,
                    false);
            });
        }
        return false;
    }

    if (state->m_eventLogActionReply && state->m_eventLogActionReply->isRunning()) {
        state->m_eventLogActionReply->abort();
    }

    const qulonglong eventLogId = state->m_eventAlertLogId;
    QNetworkRequest request = backend->makeApiJsonRequest(
        QStringLiteral("/events/%1/action").arg(QString::number(eventLogId)));
    backend->applyAuthIfNeeded(request);

    QJsonObject payload{
        // Q 문자열 Literal 초기화 함수
        { QStringLiteral("action_requested"), true },
        // Q 문자열 Literal 초기화 함수
        { QStringLiteral("action_type"), actionType },
        // Q 문자열 Literal 초기화 함수
        { QStringLiteral("action_result"), actionResult },
        // Q 문자열 Literal 초기화 함수
        { QStringLiteral("action_message"), actionMessage },
    };

    QNetworkReply *reply = state->m_manager->sendCustomRequest(
        request,
        QByteArrayLiteral("PATCH"),
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    state->m_eventLogActionReply = reply;
    backend->attachIgnoreSslErrors(reply, QStringLiteral("EVENT_LOG_ACTION"));

    QObject::connect(reply, &QNetworkReply::finished, backend, [backend,
                                                                state,
                                                                reply,
                                                                eventLogId,
                                                                actionType,
                                                                actionResult,
                                                                actionMessage]() {
        if (state->m_eventLogActionReply == reply) {
            state->m_eventLogActionReply = nullptr;
        }

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray body = reply->readAll();
        reply->deleteLater();

        if (netError != QNetworkReply::NoError || statusCode != 200) {
            qWarning() << "[EVENT][ACTION] update failed:"
                       << "id=" << eventLogId
                       << "status=" << statusCode
                       << "netError=" << netError
                       << "body=" << body;
            return;
        }

        // 목록에서도 같은 이벤트 row의 action 상태를 바로 갱신한다.
        if (updateHistoryActionState(state, eventLogId, actionType, actionResult, actionMessage)) {
            emit backend->eventAlertHistoryChanged();
        }
    });

    return true;
}
