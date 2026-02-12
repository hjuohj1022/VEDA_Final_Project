cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX)
set(CMAKE_UNITY_BUILD OFF)

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
    SidebarWidget.cpp
    SidebarWidget.h
    HeaderWidget.cpp
    HeaderWidget.h
    VideoContainerWidget.cpp
    VideoContainerWidget.h
    StyleHelper.cpp
    StyleHelper.h
)

# =========================================================================
# VLC 라이브러리 설정 (OS 분기)
# =========================================================================
if(WIN32)
    set(VLC_PATH "C:/vlc-3.0.23")
    target_include_directories(Team3VideoReceiver PRIVATE ${VLC_PATH}/sdk/include)
    target_link_libraries(Team3VideoReceiver
        PRIVATE
            Qt::Core
            Qt::Widgets
            Qt::Multimedia
            Qt::MultimediaWidgets
            Qt::Network
            ${VLC_PATH}/sdk/lib/libvlc.lib
            ${VLC_PATH}/sdk/lib/libvlccore.lib
            dwmapi
    )
    
    # Windows: DLL 복사 커맨드
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
else()
    # Linux (pkg-config 사용)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VLC REQUIRED libvlc)
    
    target_include_directories(Team3VideoReceiver PRIVATE ${VLC_INCLUDE_DIRS})
    target_link_libraries(Team3VideoReceiver
        PRIVATE
            Qt::Core
            Qt::Widgets
            Qt::Multimedia
            Qt::MultimediaWidgets
            Qt::Network
            ${VLC_LIBRARIES}
    )
    
    # Linux: .env 복사만
    add_custom_command(TARGET Team3VideoReceiver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/.env
            $<TARGET_FILE_DIR:Team3VideoReceiver>
    )
endif()

include(GNUInstallDirs)

install(TARGETS Team3VideoReceiver
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET Team3VideoReceiver
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
