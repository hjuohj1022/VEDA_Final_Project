#include "Backend.h"
#include "internal/core/BackendCoreSslService.h"

void Backend::setupSslConfiguration()
{
    BackendCoreSslService::setupSslConfiguration(this, d_ptr.get());
}

void Backend::applySslIfNeeded(QNetworkRequest &request) const
{
    BackendCoreSslService::applySslIfNeeded(const_cast<Backend *>(this), d_ptr.get(), request);
}

void Backend::attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const
{
    BackendCoreSslService::attachIgnoreSslErrors(const_cast<Backend *>(this), d_ptr.get(), reply, tag);
}

