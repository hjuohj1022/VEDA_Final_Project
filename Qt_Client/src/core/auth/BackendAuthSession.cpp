#include "Backend.h"
#include "internal/auth/BackendAuthSessionService.h"

void Backend::logout()
{
    BackendAuthSessionService::logout(this, d_ptr.get());
}

void Backend::resetSessionTimer()
{
    BackendAuthSessionService::resetSessionTimer(this, d_ptr.get());
}

bool Backend::adminUnlock(QString adminCode)
{
    return BackendAuthSessionService::adminUnlock(this, d_ptr.get(), adminCode);
}

void Backend::onSessionTick()
{
    BackendAuthSessionService::onSessionTick(this, d_ptr.get());
}

