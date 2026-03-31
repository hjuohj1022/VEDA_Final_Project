#pragma once

#include "crow.h"

#include "features/cctv/CctvManager.h"

// CCTV 제어용 REST API와 바이너리 스트림 중계용 웹소켓 API를 등록한다.
// 이 함수는 HTTP 요청을 CCTV 명령 문자열로 변환하고, CctvManager가 받은 스트림 프레임을
// Crow 웹소켓 클라이언트에게 전달하는 상위 프록시 계층의 진입점이다.
void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr);

// 서버 종료 직전에 백그라운드 CCTV 제어 작업 스레드를 정리한다.
// join 가능한 스레드를 안전하게 닫아 프로세스 종료 시 멈춤 현상이 나지 않게 한다.
void shutdownCctvProxyWorker();
