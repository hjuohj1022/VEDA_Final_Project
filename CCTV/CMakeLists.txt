cmake_minimum_required(VERSION 3.10)
project(DepthAnythingTRT LANGUAGES CXX)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ALL_BUILD)

# 1. C++ 표준
set(CMAKE_CXX_STANDARD 17)

# 2. 경로 설정 (로컬 설정 파일 로드)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local_paths.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/local_paths.cmake")
else()
    message(FATAL_ERROR "local_paths.cmake not found! Copy local_paths.cmake.example to local_paths.cmake and set your paths.")
endif()

set(NVCC_PATH "${CUDA_PATH}/bin/nvcc.exe")

# 3. Include
include_directories(${TRT_PATH}/include)
include_directories(${CUDA_PATH}/include)
include_directories(${OPENCV_PATH}/include)

# 4. Lib
link_directories(${TRT_PATH}/lib)
link_directories(${CUDA_PATH}/lib/x64)
link_directories(${OPENCV_PATH}/x64/vc16/lib)

# 5. CUDA 수동 컴파일 (CMake CUDA 언어 지원이 VS에서 꼬였을 때 사용하는 우회법)
set(CUDA_OBJ "${CMAKE_CURRENT_BINARY_DIR}/main_cu.obj")

# OpenCV Include 경로 확보
set(OPENCV_INCLUDE "${OPENCV_PATH}/include")
set(TRT_INCLUDE "${TRT_PATH}/include")
set(CUDA_INCLUDE "${CUDA_PATH}/include")

add_custom_command(
    OUTPUT ${CUDA_OBJ}
    COMMAND "${NVCC_PATH}"
    ARGS -c "${CMAKE_CURRENT_SOURCE_DIR}/main.cu"
         -o "${CUDA_OBJ}"
         -I"${CMAKE_CURRENT_SOURCE_DIR}"
         -I"${TRT_INCLUDE}"
         -I"${CUDA_INCLUDE}"
         -I"${OPENCV_INCLUDE}"
         -allow-unsupported-compiler
         -Xcompiler "/MD"
         -O3
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/main.cu" "${CMAKE_CURRENT_SOURCE_DIR}/app_config.h"
    COMMENT "Compiling CUDA source main.cu manually..."
)

# 6. 실행 파일 (오브젝트 파일 링크 + 링커 유도용 dummy)
add_executable(depth_trt ${CUDA_OBJ} dummy.cpp)

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
)
