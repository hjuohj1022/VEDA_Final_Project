#include "Backend.h"
#include "internal/media/BackendMediaStorageService.h"

// 저장소 확인 함수
void Backend::checkStorage()
{
    BackendMediaStorageService::checkStorage(this, d_ptr.get());
}

