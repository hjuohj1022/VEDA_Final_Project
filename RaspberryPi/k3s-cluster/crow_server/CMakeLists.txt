cmake_minimum_required(VERSION 3.11)
project(CrowMqttServer)

set(CMAKE_CXX_STANDARD 17)

# 외부 라이브러리 가져오기
include(FetchContent)
FetchContent_Declare(
  jwt-cpp
  GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
  GIT_TAG v0.6.0
)
FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(jwt-cpp Crow)

# MariaDB 및 OpenSSL 라이브러리 찾기
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
find_package(OpenSSL REQUIRED)

# 실행 파일 이름과 소스 파일 지정
add_executable(crow_server src/main.cpp src/MqttManager.cpp)

# 헤더 파일 경로 포함
target_include_directories(crow_server PRIVATE include ${MYSQL_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})

# 필요한 라이브러리 링크
target_link_libraries(crow_server 
    pthread 
    mosquitto 
    mosquittopp 
    Crow::Crow
    ${MYSQL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    jwt-cpp::jwt-cpp
)
