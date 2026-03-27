#include "thermal_dtls_gateway/dtls_gateway.h"
#include "thermal_dtls_gateway/gateway_common.h"

#include <exception>
#include <iostream>

// 프로그램 진입점입니다.
// 환경변수와 소켓 설정은 DtlsGateway 내부에서 처리하고,
// 여기서는 게이트웨이 수명 주기와 치명적 예외 처리만 담당합니다.
int main()
{
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    std::cout << "[DTLS] Gateway process starting" << std::endl;

    try {
        thermal_dtls_gateway::DtlsGateway gateway;
        gateway.init();
        gateway.run();
        return 0;
    } catch (const std::exception& ex) {
        // 초기화/실행 중 발생한 예외는 OpenSSL 에러 스택과 함께 출력합니다.
        std::cerr << "[DTLS] Fatal error: " << ex.what() << '\n';
        thermal_dtls_gateway::printOpenSslErrors("[DTLS] OpenSSL error stack");
        return 1;
    }
}
