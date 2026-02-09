cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX)

# =========================================================================
# VLC SDK 경로 설정
# =========================================================================
set(VLC_PATH "C:/vlc-3.0.23")

# Network 모듈 추가 (Core Widgets Multimedia MultimediaWidgets Network)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network)

qt_standard_project_setup()

qt_add_executable(Team3VideoReceiver
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    .env
)

# =========================================================================
# VLC 헤더 파일 경로 추가 (include)
# =========================================================================
target_include_directories(Team3VideoReceiver PRIVATE ${VLC_PATH}/sdk/include)

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
        ${VLC_PATH}/sdk/lib/libvlc.lib
        ${VLC_PATH}/sdk/lib/libvlccore.lib
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
        ${VLC_PATH}/libvlc.dll
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VLC_PATH}/libvlccore.dll
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${VLC_PATH}/plugins
        $<TARGET_FILE_DIR:Team3VideoReceiver>/plugins
    COMMENT "Copying VLC DLLs and Plugins to build directory..."
)

qt_generate_deploy_app_script(
    TARGET Team3VideoReceiver
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
