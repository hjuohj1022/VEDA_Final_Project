#pragma once

#include "crow.h"

// 인증된 Crow 요청을 카메라의 SUNAPI HTTP 인터페이스로 프록시하는 REST 라우트를 등록한다.
// 저장소 조회, 타임라인 조회, PTZ 제어, export 요청처럼 프런트엔드가 직접 쓰는 진입점이 여기에 모여 있다.
void registerSunapiProxyRoutes(crow::SimpleApp& app);
