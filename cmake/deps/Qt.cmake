# 同时支持 Qt5 (5.9.9+) 和 Qt6，优先使用 Qt6
# 当用户通过 Qt6_DIR / Qt5_DIR 直接指定路径时，提取安装前缀加入 CMAKE_PREFIX_PATH，
# 使 find_package(QT NAMES Qt6 Qt5) 能正确定位包。
foreach(_airan_qt_dir_var IN ITEMS Qt6_DIR Qt5_DIR)
    if(DEFINED ${_airan_qt_dir_var} AND NOT "${${_airan_qt_dir_var}}" STREQUAL "")
        # Qt?_DIR 通常为 <prefix>/lib/cmake/Qt?，上溯三级得到前缀
        get_filename_component(_airan_qt_prefix "${${_airan_qt_dir_var}}" DIRECTORY)
        get_filename_component(_airan_qt_prefix "${_airan_qt_prefix}" DIRECTORY)
        get_filename_component(_airan_qt_prefix "${_airan_qt_prefix}" DIRECTORY)
        list(INSERT CMAKE_PREFIX_PATH 0 "${_airan_qt_prefix}")
    endif()
endforeach()
unset(_airan_qt_dir_var)
unset(_airan_qt_prefix)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Svg Widgets WebSockets Network)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS LinguistTools QUIET)
# DBus 仅在 Linux/Wayland 屏幕捕获后端 (xdg-desktop-portal) 需要
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS DBus QUIET)
endif()

message(STATUS "Using Qt${QT_VERSION_MAJOR} (${Qt${QT_VERSION_MAJOR}_VERSION})")
