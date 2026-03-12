#include "Backend.h"
#include "internal/core/BackendCoreEnvService.h"
#include "internal/core/Backend_p.h"

void Backend::loadEnv()
{
    BackendCoreEnvService::loadEnv(this, d_ptr.get());
}

