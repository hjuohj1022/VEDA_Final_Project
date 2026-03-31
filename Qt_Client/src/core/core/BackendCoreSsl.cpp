#include "Backend.h"
#include "internal/core/BackendCoreSslService.h"

// 설정 SSL Configuration 처리 함수
void Backend::setupSslConfiguration()
{
    BackendCoreSslService::setupSslConfiguration(this, d_ptr.get());
}

// SSL 필요 시 적용 함수
void Backend::applySslIfNeeded(QNetworkRequest &request) const
{
    BackendCoreSslService::applySslIfNeeded(const_cast<Backend *>(this), d_ptr.get(), request);
}

// SSL 오류 무시 연결 함수
void Backend::attachIgnoreSslErrors(QNetworkReply *reply, const QString &tag) const
{
    BackendCoreSslService::attachIgnoreSslErrors(const_cast<Backend *>(this), d_ptr.get(), reply, tag);
}

