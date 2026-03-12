#include "Backend.h"
#include "internal/core/BackendCoreApiService.h"
#include "internal/core/Backend_p.h"

void Backend::applyAuthIfNeeded(QNetworkRequest &request) const
{
    BackendCoreApiService::applyAuthIfNeeded(const_cast<Backend *>(this), d_ptr.get(), request);
}

QUrl Backend::buildApiUrl(const QString &path, const QMap<QString, QString> &query) const
{
    return BackendCoreApiService::buildApiUrl(const_cast<Backend *>(this), d_ptr.get(), path, query);
}

QNetworkRequest Backend::makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query) const
{
    return BackendCoreApiService::makeApiJsonRequest(const_cast<Backend *>(this), d_ptr.get(), path, query);
}

bool Backend::isSunapiBodyError(const QString &body, QString *reason) const
{
    return BackendCoreApiService::isSunapiBodyError(const_cast<Backend *>(this), d_ptr.get(), body, reason);
}

