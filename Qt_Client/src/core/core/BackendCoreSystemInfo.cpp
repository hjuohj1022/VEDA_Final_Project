#include "Backend.h"
#include "internal/core/BackendCoreSystemInfoService.h"
#include "internal/core/Backend_p.h"

// 클라이언트 시스템 정보 조회 함수
QVariantMap Backend::getClientSystemInfo() const
{
    return BackendCoreSystemInfoService::getClientSystemInfo(const_cast<Backend *>(this), d_ptr.get());
}

// Caps 잠금 이벤트 확인 함수
bool Backend::isCapsLockOn() const
{
    return BackendCoreSystemInfoService::isCapsLockOn();
}
