cmake_minimum_required(VERSION 3.16)
project(ThermalDtlsGateway)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

add_executable(thermal_dtls_gateway
  src/dtls_gateway.cpp
  src/gateway_common.cpp
  src/main.cpp
  src/network_utils.cpp
  src/thermal_protocol.cpp
)

target_include_directories(thermal_dtls_gateway
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(thermal_dtls_gateway
  OpenSSL::SSL
  OpenSSL::Crypto
)
