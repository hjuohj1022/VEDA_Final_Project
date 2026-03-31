#include "Backend.h"
#include "internal/core/BackendCoreApiService.h"
#include "internal/core/Backend_p.h"

// 인증 필요 시 적용 함수
void Backend::applyAuthIfNeeded(QNetworkRequest &request) const
{
    BackendCoreApiService::applyAuthIfNeeded(const_cast<Backend *>(this), d_ptr.get(), request);
}

// API URL 생성 함수
QUrl Backend::buildApiUrl(const QString &path, const QMap<QString, QString> &query) const
{
    return BackendCoreApiService::buildApiUrl(const_cast<Backend *>(this), d_ptr.get(), path, query);
}

// API JSON 요청 생성 함수
QNetworkRequest Backend::makeApiJsonRequest(const QString &path, const QMap<QString, QString> &query) const
{
    return BackendCoreApiService::makeApiJsonRequest(const_cast<Backend *>(this), d_ptr.get(), path, query);
}

// Sunapi Body 오류 확인 함수
bool Backend::isSunapiBodyError(const QString &body, QString *reason) const
{
    return BackendCoreApiService::isSunapiBodyError(const_cast<Backend *>(this), d_ptr.get(), body, reason);
}

