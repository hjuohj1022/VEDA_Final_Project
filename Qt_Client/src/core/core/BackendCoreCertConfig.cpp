#include "Backend.h"
#include "internal/core/BackendCoreCertConfigService.h"
#include "internal/core/Backend_p.h"

QString Backend::certDirectoryPath() const
{
    return BackendCoreCertConfigService::certDirectoryPath(d_ptr.get());
}

bool Backend::updateCertDirectoryPath(QString path)
{
    return BackendCoreCertConfigService::updateCertDirectoryPath(this, d_ptr.get(), path);
}

bool Backend::resetCertDirectoryPath()
{
    return BackendCoreCertConfigService::resetCertDirectoryPath(this, d_ptr.get());
}
