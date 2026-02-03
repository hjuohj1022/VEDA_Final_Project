cmake_minimum_required(VERSION 3.19)
project(Team3VideoReceiver LANGUAGES CXX)

# Multimedia, MultimediaWidgets 컴포넌트 추가
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(Team3VideoReceiver
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(Team3VideoReceiver
    PRIVATE
        Qt::Core
        Qt::Widgets
        # 라이브러리 링크 추가
        Qt::Multimedia
        Qt::MultimediaWidgets
)

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
