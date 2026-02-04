cmake_minimum_required(VERSION 3.10)
project(CrowMqttServer)

set(CMAKE_CXX_STANDARD 17)

# 실행 파일 이름과 소스 파일 지정
add_executable(crow_server src/main.cpp src/MqttManager.cpp)

# 헤더 파일 경로 포함
target_include_directories(crow_server PRIVATE include)

# 필요한 라이브러리 링크 (pthread, mosquitto, mosquittopp)
target_link_libraries(crow_server pthread mosquitto mosquittopp)
