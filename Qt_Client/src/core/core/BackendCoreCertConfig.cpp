#include "Backend.h"
#include "internal/core/BackendCoreCertConfigService.h"
#include "internal/core/Backend_p.h"

// 인증서 Directory 경로 처리 함수
QString Backend::certDirectoryPath() const
{
    return BackendCoreCertConfigService::certDirectoryPath(d_ptr.get());
}

// 인증서 Directory 화면 경로 처리 함수
QString Backend::certDirectoryDisplayPath() const
{
    return BackendCoreCertConfigService::certDirectoryDisplayPath(d_ptr.get());
}

// 인증서 Directory 경로 갱신 함수
bool Backend::updateCertDirectoryPath(QString path)
{
    return BackendCoreCertConfigService::updateCertDirectoryPath(this, d_ptr.get(), path);
}

// 인증서 Directory 경로 초기화 함수
bool Backend::resetCertDirectoryPath()
{
    return BackendCoreCertConfigService::resetCertDirectoryPath(this, d_ptr.get());
}
