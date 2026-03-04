cmake_minimum_required(VERSION 3.10)
project(DepthAnythingTRT LANGUAGES CXX)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ALL_BUILD)
enable_testing()

# 1. C++ 표준
set(CMAKE_CXX_STANDARD 17)

# 2. 경로 설정 (로컬 설정 파일 로드)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/config/local_paths.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/config/local_paths.cmake")
else()
    message(FATAL_ERROR "config/local_paths.cmake not found! Copy config/local_paths.cmake.example and set your paths.")
endif()

set(NVCC_PATH "${CUDA_PATH}/bin/nvcc.exe")

# local_paths.cmake can contain relative paths.
# Resolve them against project root so custom commands run from build dir still work.
if(NOT IS_ABSOLUTE "${TRT_PATH}")
    set(TRT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${TRT_PATH}")
endif()
if(NOT IS_ABSOLUTE "${OPENCV_PATH}")
    set(OPENCV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${OPENCV_PATH}")
endif()
if(NOT IS_ABSOLUTE "${CUDA_PATH}")
    set(CUDA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${CUDA_PATH}")
endif()

# 3. Include
include_directories(${TRT_PATH}/include)
include_directories(${CUDA_PATH}/include)
include_directories(${OPENCV_PATH}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/runtime)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/config)

# 4. Lib
link_directories(${TRT_PATH}/lib)
link_directories(${CUDA_PATH}/lib/x64)
link_directories(${OPENCV_PATH}/x64/vc16/lib)

# 5. CUDA 수동 컴파일 (CMake CUDA 언어 지원이 VS에서 꼬였을 때 사용하는 우회법)
set(CUDA_DEPTH_WORKER_OBJ "${CMAKE_CURRENT_BINARY_DIR}/depth_worker_cu.obj")

# OpenCV Include 경로 확보
set(OPENCV_INCLUDE "${OPENCV_PATH}/include")
set(TRT_INCLUDE "${TRT_PATH}/include")
set(CUDA_INCLUDE "${CUDA_PATH}/include")

add_custom_command(
    OUTPUT ${CUDA_DEPTH_WORKER_OBJ}
    COMMAND "${NVCC_PATH}"
    ARGS -c "${CMAKE_CURRENT_SOURCE_DIR}/runtime/depth_worker.cu"
         -o "${CUDA_DEPTH_WORKER_OBJ}"
         -I"${CMAKE_CURRENT_SOURCE_DIR}/runtime"
         -I"${CMAKE_CURRENT_SOURCE_DIR}/config"
         -I"${TRT_INCLUDE}"
         -I"${CUDA_INCLUDE}"
         -I"${OPENCV_INCLUDE}"
         -allow-unsupported-compiler
         -D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
         -Xcompiler "/MD"
         -O3
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/depth_worker.cu" "${CMAKE_CURRENT_SOURCE_DIR}/config/app_config.h"
    COMMENT "Compiling CUDA source runtime/depth_worker.cu manually..."
)

# 6. 실행 파일 (오브젝트 파일 링크 + 링커 유도용 dummy)
add_executable(depth_trt
    ${CUDA_DEPTH_WORKER_OBJ}
    runtime/command_dispatcher.cpp
    runtime/main.cpp
    runtime/dummy.cpp
    runtime/logging.cpp
    runtime/net_protocol.cpp
    runtime/request.cpp
    runtime/request_validator.cpp
    runtime/runtime_config.cpp
    runtime/server_runtime.cpp
    runtime/trt_engine.cpp
)

add_executable(request_parser_smoke
    runtime/request_parser_smoke.cpp
    runtime/request.cpp
    runtime/request_validator.cpp
)

add_test(NAME request_parser_smoke COMMAND request_parser_smoke)

# 7. 링크
target_link_libraries(depth_trt debug ${OPENCV_PATH}/x64/vc16/lib/opencv_world4100d.lib)
target_link_libraries(depth_trt optimized ${OPENCV_PATH}/x64/vc16/lib/opencv_world4100.lib)
target_link_libraries(depth_trt nvinfer_10 nvinfer_plugin_10 cudart)
target_link_libraries(depth_trt Ws2_32)

# 8. DLL 복사
add_custom_command(TARGET depth_trt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TRT_PATH}/bin/nvinfer_10.dll" $<TARGET_FILE_DIR:depth_trt>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TRT_PATH}/bin/nvinfer_plugin_10.dll" $<TARGET_FILE_DIR:depth_trt>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<IF:$<CONFIG:Debug>,${OPENCV_PATH}/x64/vc16/bin/opencv_world4100d.dll,${OPENCV_PATH}/x64/vc16/bin/opencv_world4100.dll>" $<TARGET_FILE_DIR:depth_trt>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_PATH}/x64/vc16/bin/opencv_videoio_ffmpeg4100_64.dll" $<TARGET_FILE_DIR:depth_trt>
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:depth_trt>/ml_assets/engines"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/ml_assets/engines" "$<TARGET_FILE_DIR:depth_trt>/ml_assets/engines"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/certs")
    add_custom_command(TARGET depth_trt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:depth_trt>/certs"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/certs" "$<TARGET_FILE_DIR:depth_trt>/certs"
    )
endif()
