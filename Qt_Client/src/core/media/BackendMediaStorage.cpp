#include "Backend.h"
#include "internal/media/BackendMediaStorageService.h"

void Backend::checkStorage()
{
    BackendMediaStorageService::checkStorage(this, d_ptr.get());
}

