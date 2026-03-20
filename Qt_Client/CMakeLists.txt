cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX VERSION 1.1)
set(CMAKE_UNITY_BUILD OFF)

# Network 紐⑤뱢 異붽? (Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network Quick Qml QuickControls2 Mqtt WebSockets)

qt_standard_project_setup()

# QTP0001: QML 紐⑤뱢??湲곕낯 由ъ냼???묐몢?щ? :/qt/qml/濡??ㅼ젙
qt_policy(SET QTP0001 NEW)
# QTP0004: ?섏쐞 QML ?붾젆?곕━(qmldir) ?뺤콉 寃쎄퀬 ?듭젣
qt_policy(SET QTP0004 OLD)

set_source_files_properties(
    src/ui/assets/icons/tooltip_dark.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/tooltip_dark.svg
)
set_source_files_properties(
    src/ui/assets/icons/tooltip_light.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/tooltip_light.svg
)
set_source_files_properties(
    src/ui/assets/icons/Hanwha_logo.ico
    PROPERTIES QT_RESOURCE_ALIAS icons/Hanwha_logo.ico
)
set_source_files_properties(
    src/ui/assets/icons/Hanwha_logo.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/Hanwha_logo.svg
)


qt_add_executable(Team3VideoReceiver
    WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/core/core/BackendInit.cpp
    src/core/core/BackendInitService.cpp
    src/core/stream/BackendPlaybackWsRuntime.cpp
    src/core/stream/BackendPlaybackWsRuntimeService.cpp
    src/core/core/BackendCoreEnv.cpp
    src/core/core/BackendCoreEnvService.cpp
    src/core/core/BackendCoreSsl.cpp
    src/core/core/BackendCoreSslService.cpp
    src/core/core/BackendCoreMqtt.cpp
    src/core/core/BackendCoreMqttService.cpp
    src/core/core/BackendCoreApi.cpp
    src/core/core/BackendCoreApiService.cpp
    src/core/core/BackendCoreState.cpp
    src/core/core/BackendCoreStateService.cpp
    src/core/core/BackendCoreSystemInfo.cpp
    src/core/core/BackendCoreSystemInfoService.cpp
    src/core/auth/BackendAuthLogin.cpp
    src/core/auth/BackendAuthRegister.cpp
    src/core/auth/BackendAuthRecovery.cpp
    src/core/auth/BackendAuthRequestService.cpp
    src/core/auth/BackendAuthSession.cpp
    src/core/auth/BackendAuthSessionService.cpp
    src/core/media/BackendMediaStorage.cpp
    src/core/media/BackendMediaStorageService.cpp
    src/core/rtsp/BackendRtspConfig.cpp
    src/core/rtsp/BackendRtspConfigService.cpp
    src/core/rtsp/BackendRtspPlayback.cpp
    src/core/rtsp/BackendRtspPlaybackService.cpp
    src/core/rtsp/BackendRtspProbe.cpp
    src/core/rtsp/BackendRtspProbeService.cpp
    src/core/sunapi/BackendSunapiPtz.cpp
    src/core/sunapi/BackendSunapiPtzService.cpp
    src/core/cctv/BackendCctv3dMap.cpp
    src/core/cctv/BackendCctv3dMapService.cpp
    src/core/cctv/BackendMotorControl.cpp
    src/core/cctv/BackendMotorControlService.cpp
    src/core/sunapi/BackendSunapiDisplay.cpp
    src/core/sunapi/BackendSunapiDisplayService.cpp
    src/core/sunapi/BackendSunapiTimeline.cpp
    src/core/sunapi/BackendSunapiTimelineMonth.cpp
    src/core/sunapi/BackendSunapiTimelineService.cpp
    src/core/sunapi/BackendSunapiExportHttp.cpp
    src/core/sunapi/BackendSunapiExportHttpService.cpp
    src/core/sunapi/BackendSunapiExportDownload.cpp
    src/core/sunapi/BackendSunapiExportDownloadService.cpp
    src/core/sunapi/BackendSunapiExportParse.cpp
    src/core/sunapi/BackendSunapiExportParseService.cpp
    src/core/sunapi/BackendSunapiExportFfmpeg.cpp
    src/core/sunapi/BackendSunapiExportFfmpegService.cpp
    src/core/sunapi/BackendSunapiExportWsPrep.cpp
    src/core/sunapi/BackendSunapiExportWsPrepService.cpp
    src/core/sunapi/BackendSunapiExportWsRtsp.cpp
    src/core/sunapi/BackendSunapiExportWsRtspService.cpp
    src/core/sunapi/BackendSunapiExportWsSession.cpp
    src/core/sunapi/BackendSunapiExportWsSessionService.cpp
    src/core/sunapi/BackendSunapiExportWsMux.cpp
    src/core/sunapi/BackendSunapiExportWsMuxService.cpp
    src/core/stream/BackendStreamingWs.cpp
    src/core/stream/BackendStreamingWsService.cpp
    src/core/thermal/BackendThermal.cpp
    src/core/thermal/BackendThermalService.cpp
    include/core/Backend.h
    .env
)

if (WIN32)
    target_sources(Team3VideoReceiver PRIVATE src/app/app_icon.rc)
endif()

# Force UTF-8 source/execution charset so Korean literals do not break.
if (MSVC)
    target_compile_options(Team3VideoReceiver PRIVATE /utf-8)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(Team3VideoReceiver PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

qt_add_qml_module(Team3VideoReceiver
    URI Team3VideoReceiver
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/common/Header.qml
        src/ui/qml/common/LoginScreen.qml
        src/ui/qml/view/VideoGrid.qml
        src/ui/qml/view/ViewGridContent.qml
        src/ui/qml/view/InlineMainViewContent.qml
        src/ui/qml/playback/PlaybackScreen.qml
        src/ui/qml/playback/PlaybackContent.qml
        src/ui/qml/playback/VideoPlayer.qml
        src/ui/qml/thermal/ThermalViewer.qml
        src/ui/qml/thermal/ThermalContent.qml
        src/ui/qml/sidebar/Sidebar.qml
        src/ui/qml/sidebar/SidebarStore.qml
        src/ui/qml/sidebar/SidebarPlaybackControlsPanel.qml
        src/ui/qml/sidebar/SidebarCameraControlsPanel.qml
        src/ui/qml/sidebar/SidebarThermalControlsPanel.qml
        src/ui/qml/sidebar/SidebarSystemMetricsPanel.qml
        src/ui/qml/dialogs/PlaybackExportSaveDialog.qml
        src/ui/qml/dialogs/RtspSettingsDialog.qml
        src/ui/qml/dialogs/MotorControlDialog.qml
        src/ui/qml/dialogs/StatusDialog.qml
        src/ui/qml/dialogs/SystemSpecsDialog.qml
        src/ui/qml/dialogs/TwoFactorDialog.qml
        src/ui/qml/dialogs/AccountDeleteDialog.qml
        src/ui/qml/dialogs/ChangePasswordDialog.qml
        src/ui/qml/dialogs/PasswordForgotDialog.qml
        src/ui/qml/dialogs/PasswordResetDialog.qml
        src/ui/qml/components/IconButton.qml
        src/ui/qml/components/SidebarControlButton.qml
        src/ui/qml/components/SidebarDisplaySlider.qml
    RESOURCES
        src/ui/assets/icons/README.txt
        src/ui/assets/icons/tooltip_dark.svg
        src/ui/assets/icons/tooltip_light.svg
        src/ui/assets/icons/Hanwha_logo.ico
        src/ui/assets/icons/Hanwha_logo.svg
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





