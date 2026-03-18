#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"

void Backend::registerUser(QString id, QString pw)
{
    BackendAuthRequestService::registerUser(this, d_ptr.get(), id, pw);
}

