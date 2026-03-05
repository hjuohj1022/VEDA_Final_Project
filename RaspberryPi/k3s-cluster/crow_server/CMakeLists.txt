cmake_minimum_required(VERSION 3.11)
project(CrowMqttServer)

set(CMAKE_CXX_STANDARD 17)

# Crow가 Boost Asio를 사용하도록 변수 설정 (FetchContent 이전에 설정해야 함)
set(CROW_USE_BOOST ON CACHE BOOL "Use Boost Asio instead of standalone Asio")
add_definitions(-DCROW_USE_BOOST)

# Boost 라이브러리를 Crow보다 먼저 로드
find_package(Boost REQUIRED COMPONENTS system thread)

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

find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(OpenSSL REQUIRED)

add_executable(
  crow_server
  src/main.cpp
  src/MqttManager.cpp
  src/SunapiProxy.cpp
  src/SunapiWsProxy.cpp
  src/CctvManager.cpp
  src/CctvProxy.cpp
)

target_compile_definitions(crow_server PRIVATE CROW_ENABLE_WEBSOCKETS)

target_include_directories(
  crow_server PRIVATE
  include
  ${MYSQL_INCLUDE_DIRS}
  ${CURL_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(
  crow_server
  pthread
  mosquitto
  mosquittopp
  Crow::Crow
  ${MYSQL_LIBRARIES}
  ${CURL_LIBRARIES}
  OpenSSL::SSL
  OpenSSL::Crypto
  jwt-cpp::jwt-cpp
  Boost::system
  Boost::thread
)
