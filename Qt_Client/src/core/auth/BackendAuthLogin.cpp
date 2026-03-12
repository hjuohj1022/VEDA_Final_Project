#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"

void Backend::login(QString id, QString pw)
{
    BackendAuthRequestService::login(this, d_ptr.get(), id, pw);
}

