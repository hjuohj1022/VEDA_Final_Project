#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"
#include "internal/auth/BackendAuthSessionService.h"

// 로그아웃 함수
void Backend::logout()
{
    BackendAuthSessionService::logout(this, d_ptr.get());
}

// 세션 타이머 초기화 함수
void Backend::resetSessionTimer()
{
    BackendAuthSessionService::resetSessionTimer(this, d_ptr.get());
}

// 관리자 잠금 해제 처리 함수
bool Backend::adminUnlock(QString adminCode)
{
    return BackendAuthRequestService::adminUnlock(this, d_ptr.get(), adminCode);
}

// 이벤트 세션 Tick 처리 함수
void Backend::onSessionTick()
{
    BackendAuthSessionService::onSessionTick(this, d_ptr.get());
}
