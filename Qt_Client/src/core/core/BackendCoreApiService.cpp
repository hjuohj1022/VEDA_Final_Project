#include "internal/core/BackendCoreApiService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

void BackendCoreApiService::applyAuthIfNeeded(Backend *backend,
                                              BackendPrivate *state,
                                              QNetworkRequest &request)
{
    if (state->m_authToken.trimmed().isEmpty()) {
        return;
    }

    const QUrl reqUrl = request.url();
    const QUrl apiBase(backend->serverUrl());
    if (!reqUrl.isValid() || !apiBase.isValid()) {
        return;
    }

    const auto defaultPortFor = [](const QString &scheme) {
        return (scheme.compare("https", Qt::CaseInsensitive) == 0) ? 443 : 80;
    };

    const bool sameScheme = (reqUrl.scheme().compare(apiBase.scheme(), Qt::CaseInsensitive) == 0);
    const bool sameHost = (reqUrl.host().compare(apiBase.host(), Qt::CaseInsensitive) == 0);
    const bool samePort = (reqUrl.port(defaultPortFor(reqUrl.scheme())) == apiBase.port(defaultPortFor(apiBase.scheme())));
    if (!sameScheme || !sameHost || !samePort) {
        return;
    }

    request.setRawHeader("Authorization", QByteArray("Bearer ") + state->m_authToken.toUtf8());
}

QUrl BackendCoreApiService::buildApiUrl(Backend *backend,
                                        BackendPrivate *state,
                                        const QString &path,
                                        const QMap<QString, QString> &query)
{
    Q_UNUSED(state);

    QUrl url(backend->serverUrl());
    QString cleanPath = path.trimmed();
    if (!cleanPath.startsWith('/')) {
        cleanPath.prepend('/');
    }
    url.setPath(cleanPath);

    QUrlQuery q(url);
    for (auto it = query.constBegin(); it != query.constEnd(); ++it) {
        q.addQueryItem(it.key(), it.value());
    }
    url.setQuery(q);
    return url;
}

QNetworkRequest BackendCoreApiService::makeApiJsonRequest(Backend *backend,
                                                          BackendPrivate *state,
                                                          const QString &path,
                                                          const QMap<QString, QString> &query)
{
    QNetworkRequest req(buildApiUrl(backend, state, path, query));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(req);
    return req;
}

bool BackendCoreApiService::isSunapiBodyError(Backend *backend,
                                              BackendPrivate *state,
                                              const QString &body,
                                              QString *reason)
{
    Q_UNUSED(backend);
    Q_UNUSED(state);

    const QString trimmed = body.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const QString bodyLower = trimmed.toLower();
    const QRegularExpression errPattern("^\\s*error\\s*=\\s*([^\\r\\n]+)",
                                        QRegularExpression::CaseInsensitiveOption
                                        | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch errMatch = errPattern.match(trimmed);
    if (errMatch.hasMatch()) {
        const QString errValue = errMatch.captured(1).trimmed();
        const QString errValueLower = errValue.toLower();
        if (errValueLower != "0"
            && errValueLower != "ok"
            && errValueLower != "none"
            && errValueLower != "success") {
            if (reason) {
                *reason = QString("Error=%1").arg(errValue);
            }
            return true;
        }
    }

    if (bodyLower.contains("fail")
        || bodyLower.contains("unsupported")
        || bodyLower.contains("not support")
        || bodyLower.contains("invalid")
        || bodyLower.startsWith("ng")) {
        if (reason) {
            *reason = trimmed.left(160);
        }
        return true;
    }

    return false;
}

