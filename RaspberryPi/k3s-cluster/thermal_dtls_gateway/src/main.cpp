#include "thermal_dtls_gateway/dtls_gateway.h"
#include "thermal_dtls_gateway/gateway_common.h"

#include <exception>
#include <iostream>

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
        std::cerr << "[DTLS] Fatal error: " << ex.what() << '\n';
        thermal_dtls_gateway::printOpenSslErrors("[DTLS] OpenSSL error stack");
        return 1;
    }
}
