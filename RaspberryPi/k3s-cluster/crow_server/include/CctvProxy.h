#pragma once

#include "crow.h"
#include "CctvManager.h"

// 역할:
// - HTTP 요청의 CCTV 제어 명령 문자열 변환
// - CCTV 스트림의 Crow WebSocket 재브로드캐스트
// - 백그라운드 명령 worker 상태 조회 API 제공
void registerCctvProxyRoutes(crow::SimpleApp& app, CctvManager& cctv_mgr);

// 서버 종료 직전 호출 대상, 백그라운드 CCTV 명령 worker 정리.
void shutdownCctvProxyWorker();
