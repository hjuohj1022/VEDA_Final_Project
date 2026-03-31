#include "Backend.h"
#include "internal/core/BackendCoreEnvService.h"
#include "internal/core/Backend_p.h"

// 환경 로드 함수
void Backend::loadEnv()
{
    BackendCoreEnvService::loadEnv(this, d_ptr.get());
}

