cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX VERSION 1.1)
set(CMAKE_UNITY_BUILD OFF)

# Network 모듈 추가 (Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2 Mqtt WebSockets)

qt_standard_project_setup()

# QTP0001: QML 모듈의 기본 리소스 접두사를 :/qt/qml/로 설정
qt_policy(SET QTP0001 NEW)
# QTP0004: 하위 QML 디렉터리(qmldir) 정책 경고 억제
qt_policy(SET QTP0004 OLD)

set_source_files_properties(
    src/ui/assets/icons/tooltip_dark.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/tooltip_dark.svg
)
set_source_files_properties(
    src/ui/assets/icons/tooltip_light.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/tooltip_light.svg
)


qt_add_executable(Team3VideoReceiver
    WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/core/BackendInit.cpp
    src/core/BackendPlaybackWsRuntime.cpp
    src/core/BackendCoreEnv.cpp
    src/core/BackendCoreSsl.cpp
    src/core/BackendCoreMqtt.cpp
    src/core/BackendCoreApi.cpp
    src/core/BackendCoreState.cpp
    src/core/BackendAuthLogin.cpp
    src/core/BackendAuthRegister.cpp
    src/core/BackendAuthSession.cpp
    src/core/BackendMediaRecordings.cpp
    src/core/BackendMediaStorage.cpp
    src/core/BackendRtspConfig.cpp
    src/core/BackendRtspPlayback.cpp
    src/core/BackendRtspProbe.cpp
    src/core/BackendSunapiPtz.cpp
    src/core/BackendSunapiTimeline.cpp
    src/core/BackendSunapiTimelineMonth.cpp
    src/core/BackendSunapiExportHttp.cpp
    src/core/BackendSunapiExportDownload.cpp
    src/core/BackendSunapiExportParse.cpp
    src/core/BackendSunapiExportFfmpeg.cpp
    src/core/BackendSunapiExportWsPrep.cpp
    src/core/BackendSunapiExportWsRtsp.cpp
    src/core/BackendSunapiExportWsSession.cpp
    src/core/BackendSunapiExportWsMux.cpp
    src/core/BackendStreamingWs.cpp
    include/core/Backend.h
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
        src/ui/qml/Main.qml
        src/ui/qml/Sidebar.qml
        src/ui/qml/Header.qml
        src/ui/qml/VideoGrid.qml
        src/ui/qml/LoginScreen.qml
        src/ui/qml/PlaybackScreen.qml
        src/ui/qml/VideoPlayer.qml
        src/ui/qml/components/IconButton.qml
    RESOURCES
        src/ui/assets/icons/README.txt
        src/ui/assets/icons/tooltip_dark.svg
        src/ui/assets/icons/tooltip_light.svg
)

target_include_directories(Team3VideoReceiver
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/core
)

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
        Qt::WebSockets
        dwmapi
)

include(GNUInstallDirs)

install(TARGETS Team3VideoReceiver
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

add_custom_command(TARGET Team3VideoReceiver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/.env
        $<TARGET_FILE_DIR:Team3VideoReceiver>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:Team3VideoReceiver>/tools
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tools/ffmpeg.exe
        $<TARGET_FILE_DIR:Team3VideoReceiver>/tools/ffmpeg.exe
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
    COMMENT "Copying runtime configuration, ffmpeg, and certificates..."
)

qt_generate_deploy_app_script(
    TARGET Team3VideoReceiver
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})




