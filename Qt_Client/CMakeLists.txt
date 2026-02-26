cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX VERSION 1.1)
set(CMAKE_UNITY_BUILD OFF)

# =========================================================================
# VLC SDK/Runtime 경로 설정
# =========================================================================
set(VLC_SDK_PATH "${CMAKE_SOURCE_DIR}/third_party/vlc4-sdk" CACHE PATH "Path to VLC SDK root (headers/libs)")
set(VLC_RUNTIME_PATH "C:/Program Files/VideoLAN/VLC" CACHE PATH "Path to VLC runtime root (dll/plugins)")

if(DEFINED ENV{VLC_SDK_PATH} AND NOT "$ENV{VLC_SDK_PATH}" STREQUAL "")
    set(VLC_SDK_PATH "$ENV{VLC_SDK_PATH}" CACHE PATH "Path to VLC SDK root (headers/libs)" FORCE)
endif()
if(DEFINED ENV{VLC_RUNTIME_PATH} AND NOT "$ENV{VLC_RUNTIME_PATH}" STREQUAL "")
    set(VLC_RUNTIME_PATH "$ENV{VLC_RUNTIME_PATH}" CACHE PATH "Path to VLC runtime root (dll/plugins)" FORCE)
endif()
if(DEFINED ENV{VLC_PATH} AND NOT "$ENV{VLC_PATH}" STREQUAL "")
    set(VLC_SDK_PATH "$ENV{VLC_PATH}" CACHE PATH "Path to VLC SDK root (headers/libs)" FORCE)
    set(VLC_RUNTIME_PATH "$ENV{VLC_PATH}" CACHE PATH "Path to VLC runtime root (dll/plugins)" FORCE)
endif()
set(VLC_CACHE_GEN "${VLC_RUNTIME_PATH}/vlc-cache-gen.exe")

if(NOT EXISTS "${VLC_SDK_PATH}/sdk/include/vlc/vlc.h")
    message(FATAL_ERROR "VLC SDK header not found: ${VLC_SDK_PATH}/sdk/include/vlc/vlc.h")
endif()
if(NOT EXISTS "${VLC_SDK_PATH}/sdk/lib/libvlc.lib")
    message(FATAL_ERROR "VLC SDK import library not found: ${VLC_SDK_PATH}/sdk/lib/libvlc.lib")
endif()
if(NOT EXISTS "${VLC_RUNTIME_PATH}/libvlc.dll")
    message(FATAL_ERROR "VLC runtime DLL not found: ${VLC_RUNTIME_PATH}/libvlc.dll")
endif()
if(NOT EXISTS "${VLC_RUNTIME_PATH}/plugins")
    message(FATAL_ERROR "VLC runtime plugins not found: ${VLC_RUNTIME_PATH}/plugins")
endif()

# Network 모듈 추가 (Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2 Mqtt)

qt_standard_project_setup()

# QTP0001: QML 모듈의 기본 리소스 접두사를 :/qt/qml/로 설정
qt_policy(SET QTP0001 NEW)



qt_add_executable(Team3VideoReceiver
    WIN32 MACOSX_BUNDLE
    main.cpp
    VlcPlayer.cpp
    VlcPlayer.h
    Backend.cpp
    BackendCore.cpp
    BackendAuth.cpp
    BackendMedia.cpp
    BackendRtsp.cpp
    BackendSunapi.cpp
    Backend.h
    .env
)

# Force UTF-8 source/execution charset so Korean literals do not break.
if (MSVC)
    target_compile_options(Team3VideoReceiver PRIVATE /utf-8)
endif()

qt_add_qml_module(Team3VideoReceiver
    URI Team3VideoReceiver
    VERSION 1.0
    QML_FILES
        Main.qml
        Sidebar.qml
        Header.qml
        VideoGrid.qml
        LoginScreen.qml
        VideoPlayer.qml
)

# =========================================================================
# VLC 헤더 파일 경로 추가 (include)
# =========================================================================
target_include_directories(Team3VideoReceiver PRIVATE ${VLC_SDK_PATH}/sdk/include)

# =========================================================================
# VLC 라이브러리 연결 (lib)
# =========================================================================
target_link_libraries(Team3VideoReceiver
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Multimedia
        Qt::MultimediaWidgets
        Qt::Network
        Qt::Quick
        Qt::Qml
        Qt::QuickControls2
        Qt::Mqtt
        ${VLC_SDK_PATH}/sdk/lib/libvlc.lib
        ${VLC_SDK_PATH}/sdk/lib/libvlccore.lib
        dwmapi
)

include(GNUInstallDirs)

install(TARGETS Team3VideoReceiver
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# =========================================================================
# 빌드 후 DLL 및 플러그인 자동 복사 (실행 오류 방지)
# 빌드가 끝나면 자동으로 실행 파일 옆에 libvlc.dll 등을 복사해줌
# =========================================================================
add_custom_command(TARGET Team3VideoReceiver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/.env
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VLC_RUNTIME_PATH}/libvlc.dll
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VLC_RUNTIME_PATH}/libvlccore.dll
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:Team3VideoReceiver>/certs
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/certs/rootCA.crt
        $<TARGET_FILE_DIR:Team3VideoReceiver>/certs/rootCA.crt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/certs/client-qt.crt
        $<TARGET_FILE_DIR:Team3VideoReceiver>/certs/client-qt.crt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/certs/client-qt.key
        $<TARGET_FILE_DIR:Team3VideoReceiver>/certs/client-qt.key
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VLC_RUNTIME_PATH}/plugins
        $<TARGET_FILE_DIR:Team3VideoReceiver>/plugins
    COMMAND ${VLC_CACHE_GEN}
        $<TARGET_FILE_DIR:Team3VideoReceiver>/plugins
    COMMENT "Copying VLC DLLs and Plugins to build directory..."
)

qt_generate_deploy_app_script(
    TARGET Team3VideoReceiver
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

