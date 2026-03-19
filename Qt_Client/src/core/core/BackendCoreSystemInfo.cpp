#include "Backend.h"
#include "internal/core/BackendCoreSystemInfoService.h"
#include "internal/core/Backend_p.h"

QVariantMap Backend::getClientSystemInfo() const
{
    return BackendCoreSystemInfoService::getClientSystemInfo(const_cast<Backend *>(this), d_ptr.get());
}

bool Backend::isCapsLockOn() const
{
    return BackendCoreSystemInfoService::isCapsLockOn();
}
