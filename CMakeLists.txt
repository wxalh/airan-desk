cmake_minimum_required(VERSION 3.21)

project(airan-desk VERSION 1.1.1 LANGUAGES C CXX)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
        "Minimum supported macOS version")
endif()

if(UNIX AND NOT APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/opt/airan-desk" CACHE PATH "Airan Desk install prefix" FORCE)
endif()

include(GNUInstallDirs)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

if(MSVC)
    add_definitions(-DUNICODE -D_UNICODE)
    add_compile_options(/utf-8 /FS /MP)
endif()

set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
    message(FATAL_ERROR
        "GCC/G++ ${CMAKE_CXX_COMPILER_VERSION} is too old for the bundled WebRTC m144 headers. "
        "Use GCC/G++ 11 or newer, or override CMAKE_C_COMPILER and CMAKE_CXX_COMPILER from CMakeUserPresets.json.")
endif()

include(cmake/AiranLibStdCppAbi.cmake)
if(DEFINED WEBRTC_LINUX_STL AND NOT WEBRTC_LINUX_STL STREQUAL "")
    set(_airan_webrtc_linux_stl "${WEBRTC_LINUX_STL}")
else()
    set(_airan_webrtc_linux_stl "gnu")
endif()
airan_select_libstdcpp_abi_definition(
    _airan_libstdcpp_abi_definition
    "${CMAKE_SYSTEM_NAME}"
    "${CMAKE_CXX_COMPILER_ID}"
    "${_airan_webrtc_linux_stl}"
)
if(_airan_libstdcpp_abi_definition)
    add_compile_definitions(${_airan_libstdcpp_abi_definition})
    airan_select_libstdcpp_abi_options(
        _airan_libstdcpp_abi_options
        "${CMAKE_SYSTEM_NAME}"
        "${CMAKE_CXX_COMPILER_ID}"
        "${_airan_webrtc_linux_stl}"
    )
    add_compile_options(${_airan_libstdcpp_abi_options})
    message(STATUS "Using the C++11 libstdc++ ABI for GNU WebRTC compatibility")
endif()

if(UNIX AND NOT APPLE AND _airan_webrtc_linux_stl STREQUAL "libcxx")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        message(FATAL_ERROR
            "WEBRTC_LINUX_STL=libcxx requires a Clang C++ compiler so the application "
            "uses the same libc++ ABI as the selected WebRTC package.")
    endif()
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>")
    add_link_options("$<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>")
    message(STATUS "Using the libc++ ABI for Linux WebRTC compatibility")
endif()

if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND _airan_webrtc_linux_stl STREQUAL "gnu")
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_QUIET TRUE)
    check_cxx_source_compiles([[#include <bits/c++config.h>
#if !defined(_GLIBCXX_USE_DUAL_ABI) || !_GLIBCXX_USE_DUAL_ABI
#error "The selected libstdc++ does not provide the dual ABI"
#endif
int main() { return 0; }]] AIRAN_LIBSTDCXX_HAS_DUAL_ABI)
    unset(CMAKE_REQUIRED_QUIET)
    if(NOT AIRAN_LIBSTDCXX_HAS_DUAL_ABI)
        message(FATAL_ERROR
            "The selected GNU libstdc++ toolchain was built without dual ABI support, "
            "but the bundled GNU WebRTC package requires the C++11 ABI. "
            "Use a GCC toolchain with dual ABI support (for GLIBC 2.17, use the "
            "conda-forge gcc_linux-64/gxx_linux-64 11 toolchain) or select a matching WebRTC package.")
    endif()
endif()
unset(_airan_libstdcpp_abi_definition)
unset(_airan_libstdcpp_abi_options)
unset(_airan_webrtc_linux_stl)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Enable automoc, autouic, and autorcc for Qt project
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/control"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/transfer"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/settings/window"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terminal"
)

# Detect 64-bit vs 32-bit
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_64 TRUE)
else()
    set(ARCH_64 FALSE)
endif()

# Dependencies
include(cmake/deps/Qt.cmake)
include(cmake/deps/OpenSSL.cmake)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# Third-party dependencies
include(cmake/deps/Spdlog.cmake)
include(cmake/deps/LibWebRTC.cmake)
include(cmake/deps/LibVTerm.cmake)
include(cmake/deps/FFmpeg.cmake)

option(AIRAN_REQUIRE_PIPEWIRE_CAPTURE "Fail configure if Linux PipeWire desktop capture dependencies are unavailable" OFF)
set(AIRAN_ENABLE_PIPEWIRE_CAPTURE OFF)
set(AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON "")
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        set(AIRAN_PIPEWIRE_CAPTURE_MODULES
            gio-2.0
            gio-unix-2.0
            glib-2.0
            gobject-2.0
            libpipewire-0.3
            libspa-0.2
            gbm
            libdrm
            egl
            gl
        )
        pkg_check_modules(AIRAN_PIPEWIRE_CAPTURE QUIET IMPORTED_TARGET
            ${AIRAN_PIPEWIRE_CAPTURE_MODULES}
        )
        if(AIRAN_PIPEWIRE_CAPTURE_FOUND)
            set(AIRAN_ENABLE_PIPEWIRE_CAPTURE ON)
            list(APPEND EXTRA_LIBS PkgConfig::AIRAN_PIPEWIRE_CAPTURE)
            list(APPEND EXTRA_DEFINITIONS
                WEBRTC_USE_PIPEWIRE=1
                WEBRTC_USE_GIO=1
            )
            message(STATUS "Airan Wayland/PipeWire desktop capture backend enabled")
        else()
            set(AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON "development packages not found")
            set(_airan_missing_pipewire_modules)
            foreach(_airan_pipewire_module IN LISTS AIRAN_PIPEWIRE_CAPTURE_MODULES)
                string(MAKE_C_IDENTIFIER "${_airan_pipewire_module}" _airan_pipewire_module_var)
                pkg_check_modules(_AIRAN_PIPEWIRE_MODULE_${_airan_pipewire_module_var} QUIET ${_airan_pipewire_module})
                if(NOT _AIRAN_PIPEWIRE_MODULE_${_airan_pipewire_module_var}_FOUND)
                    list(APPEND _airan_missing_pipewire_modules ${_airan_pipewire_module})
                endif()
            endforeach()
            if(_airan_missing_pipewire_modules)
                list(JOIN _airan_missing_pipewire_modules ", " _airan_missing_pipewire_modules_text)
                set(AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON "missing pkg-config modules: ${_airan_missing_pipewire_modules_text}")
            endif()
            message(STATUS "Airan Wayland/PipeWire desktop capture backend disabled: ${AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON}")
        endif()
    else()
        set(AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON "PkgConfig not found")
        message(STATUS "Airan Wayland/PipeWire desktop capture backend disabled: ${AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON}")
    endif()
    if(AIRAN_REQUIRE_PIPEWIRE_CAPTURE AND NOT AIRAN_ENABLE_PIPEWIRE_CAPTURE)
        message(FATAL_ERROR "Airan Wayland/PipeWire desktop capture backend is required but disabled: ${AIRAN_PIPEWIRE_CAPTURE_DISABLED_REASON}")
    endif()
endif()

include(cmake/ProjectSources.cmake)

set(QM_FILES)
if(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        qt_add_translation(QM_FILES ${TS_FILES})
    else()
        qt5_add_translation(QM_FILES ${TS_FILES})
    endif()
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
else()
    message(WARNING "Qt${QT_VERSION_MAJOR} LinguistTools not found; application translation .qm files will not be generated automatically")
endif()

set(AIRAN_QT_TRANSLATIONS_DIR "" CACHE PATH "Qt translations directory copied next to airan-desk; leave empty to auto-detect")
set(_airan_qt_translation_candidates)

get_target_property(_airan_qmake_location_for_translations Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
if(_airan_qmake_location_for_translations)
    execute_process(
        COMMAND "${_airan_qmake_location_for_translations}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE _airan_query_translations_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_airan_query_translations_dir)
        list(APPEND _airan_qt_translation_candidates "${_airan_query_translations_dir}")
    endif()

    get_filename_component(_airan_qt_install_prefix_for_translations "${_airan_qmake_location_for_translations}" DIRECTORY)
    get_filename_component(_airan_qt_install_prefix_for_translations "${_airan_qt_install_prefix_for_translations}" DIRECTORY)
    list(APPEND _airan_qt_translation_candidates
        "${_airan_qt_install_prefix_for_translations}/translations"
        "${_airan_qt_install_prefix_for_translations}/share/qt5/translations"
        "${_airan_qt_install_prefix_for_translations}/share/qt6/translations"
        "${_airan_qt_install_prefix_for_translations}/share/qt/translations"
    )
endif()

list(APPEND _airan_qt_translation_candidates
    "/usr/share/qt5/translations"
    "/usr/share/qt6/translations"
    "/usr/share/qt/translations"
    "/usr/lib/qt5/translations"
    "/usr/lib/qt6/translations"
)
list(REMOVE_DUPLICATES _airan_qt_translation_candidates)

if(AIRAN_QT_TRANSLATIONS_DIR)
    if(NOT EXISTS "${AIRAN_QT_TRANSLATIONS_DIR}")
        message(WARNING "Configured Qt translations directory does not exist: ${AIRAN_QT_TRANSLATIONS_DIR}; Qt translations will not be deployed automatically")
        set(AIRAN_QT_TRANSLATIONS_DIR "")
    endif()
else()
    foreach(_airan_qt_translations_dir IN LISTS _airan_qt_translation_candidates)
        if(EXISTS "${_airan_qt_translations_dir}")
            set(AIRAN_QT_TRANSLATIONS_DIR "${_airan_qt_translations_dir}")
            break()
        endif()
    endforeach()
endif()

if(AIRAN_QT_TRANSLATIONS_DIR)
    message(STATUS "Using Qt translations: ${AIRAN_QT_TRANSLATIONS_DIR}")
else()
    message(WARNING "Qt translations directory not found; Qt translations will not be deployed automatically")
endif()

if(WIN32)
    list(APPEND EXTRA_LIBS
        advapi32 bcrypt ole32 oleaut32 shell32 psapi opengl32 gdi32 secur32 user32 userenv wtsapi32 winmm
        mfuuid dbghelp
    )
    if(NOT LIBWEBRTC_PLATFORM STREQUAL "win7")
        list(APPEND EXTRA_LIBS d3d11 d3dcompiler dxgi windowsapp runtimeobject)
    endif()
    list(APPEND SRC_FILES
        src/util/input/platform/windows/input_util_events_win.cpp
    )
    list(APPEND EXTRA_DEFINITIONS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(LIBWEBRTC_PLATFORM STREQUAL "win7")
        list(APPEND EXTRA_DEFINITIONS
            AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN7=1
            WINVER=0x0601
            _WIN32_WINNT=0x0601
        )
    else()
        list(APPEND EXTRA_DEFINITIONS
            AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10=1
            RTC_ENABLE_WIN_WGC=1
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
        )
    endif()
    if(MSVC)
        list(APPEND EXTRA_DEFINITIONS
            _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
        )
    endif()
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND EXTRA_LIBS
        "-framework AppKit" "-framework Foundation" "-framework ApplicationServices"
        "-framework Carbon" "-framework CoreGraphics" "-framework CoreAudio"
        "-framework AudioToolbox" "-framework VideoToolbox" "-framework CoreVideo"
        "-framework CoreMedia" "-framework AVFoundation" "-framework CoreFoundation"
        "-framework Security" "-framework QuartzCore" "-framework OpenGL"
        "-Wl,-weak_framework,ScreenCaptureKit"
        util
    )
    list(APPEND SRC_FILES
        src/util/input/platform/macos/input_util_events_macos.cpp
        src/webrtc/audio/system_audio_loopback_worker_macos.mm
    )
    list(APPEND EXTRA_DEFINITIONS
        Q_OS_MACOS=1
    )
elseif(UNIX)
    # Linux 默认静态链接 libstdc++/libgcc，减少运行环境差异。
    option(STATIC_LINK_LIBSTDCPP "Link libstdc++ and libgcc statically on Linux" ON)

    # 输入事件通过 uinput 发送；桌面采集由 Airan capture policy 负责。
    list(APPEND EXTRA_LIBS
        pthread dl rt m z util
    )
    # Linux arm64 WebRTC m144 contains SME objects that reference libgcc_s symbols.
    if(LIBWEBRTC_PACKAGE_TARGET MATCHES "^libwebrtc::linux_arm64_")
        list(APPEND EXTRA_LIBS gcc_s)
    endif()
    list(APPEND EXTRA_DEFINITIONS
        WEBRTC_USE_X11=1
    )
    list(APPEND SRC_FILES
        src/util/input/platform/linux/input_util_events_linux.cpp
        src/util/input/platform/linux/input_util_events_linux_clipboard.cpp
        src/util/input/platform/linux/input_util_events_linux_keymap.cpp
        src/util/input/platform/linux/input_util_events_linux_uinput.cpp
    )

    message(STATUS "Linux Airan desktop capture backend is selected by the main capture source policy")

else()
    list(APPEND SRC_FILES
        src/util/input/platform/fallback/input_util_events_fallback.cpp
    )
endif()

# Windows UAC manifest（requireAdministrator）—— 嵌入资源文件以避免 UIPI 阻断 SendInput
if(WIN32)
    set(RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource/windows/main.rc")
    # RC 编译时需要能找到 uac.manifest，指定资源目录作为 include 路径
    if(MSVC)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/resource/windows\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resource/icons/windows\"")
    else()
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_SOURCE_DIR}/resource/windows\" -I\"${CMAKE_CURRENT_SOURCE_DIR}/resource/icons/windows\"")
    endif()
endif()

# Add executable
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32
        ${SRC_FILES}
        ${HDR_FILES}
        ${UI_FILES}
        ${RESOURCE_FILES}
        ${RC_FILE}
    )
elseif(APPLE)
    set(AIRAN_MACOS_ICON_FILE "airan-desk.icns")
    set(_airan_macos_iconset_dir "${CMAKE_CURRENT_BINARY_DIR}/airan-desk.iconset")
    set(_airan_macos_icon "${CMAKE_CURRENT_BINARY_DIR}/${AIRAN_MACOS_ICON_FILE}")
    set(_airan_macos_icon_source
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/icons/hicolor/256x256/apps/airan-desk.png")
    find_program(_airan_sips sips REQUIRED)
    find_program(_airan_iconutil iconutil REQUIRED)
    add_custom_command(
        OUTPUT "${_airan_macos_icon}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_airan_macos_iconset_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_airan_macos_iconset_dir}"
        COMMAND "${_airan_sips}" -z 16 16 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_16x16.png"
        COMMAND "${_airan_sips}" -z 32 32 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_16x16@2x.png"
        COMMAND "${_airan_sips}" -z 32 32 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_32x32.png"
        COMMAND "${_airan_sips}" -z 64 64 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_32x32@2x.png"
        COMMAND "${_airan_sips}" -z 128 128 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_128x128.png"
        COMMAND "${_airan_sips}" -z 256 256 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_128x128@2x.png"
        COMMAND "${_airan_sips}" -z 256 256 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_256x256.png"
        COMMAND "${_airan_sips}" -z 512 512 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_256x256@2x.png"
        COMMAND "${_airan_sips}" -z 512 512 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_512x512.png"
        COMMAND "${_airan_sips}" -z 1024 1024 "${_airan_macos_icon_source}" --out "${_airan_macos_iconset_dir}/icon_512x512@2x.png"
        COMMAND "${_airan_iconutil}" -c icns "${_airan_macos_iconset_dir}" -o "${_airan_macos_icon}"
        DEPENDS "${_airan_macos_icon_source}"
        COMMENT "Generating the macOS application icon"
        VERBATIM
    )
    set_source_files_properties("${_airan_macos_icon}" PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set(_airan_macos_info_plist_en
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/macos/en.lproj/InfoPlist.strings")
    set(_airan_macos_info_plist_zh_hans
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/macos/zh-Hans.lproj/InfoPlist.strings")
    set_source_files_properties("${_airan_macos_info_plist_en}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/en.lproj")
    set_source_files_properties("${_airan_macos_info_plist_zh_hans}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/zh-Hans.lproj")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-Info.plist"
        @ONLY
    )
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
        ${SRC_FILES}
        ${HDR_FILES}
        ${UI_FILES}
        ${RESOURCE_FILES}
        ${RC_FILE}
        "${_airan_macos_icon}"
        "${_airan_macos_info_plist_en}"
        "${_airan_macos_info_plist_zh_hans}"
    )
else()
    add_executable(${PROJECT_NAME}
        ${SRC_FILES}
        ${HDR_FILES}
        ${UI_FILES}
        ${RESOURCE_FILES}
        ${RC_FILE}
    )
endif()

if(TARGET translations)
    add_dependencies(${PROJECT_NAME} translations)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
)

if(MSVC)
    # main.rc 已嵌入 MANIFEST，关闭链接器自动生成以避免 CVT1100 重复资源
    set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS " /MANIFEST:NO")
endif()

# 立即为主可执行文件设置输出目录，确保不会落到 third_party/bin
if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug"
    )
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release"
    )
endif()

set(AIRAN_WEBRTC_EXTRA_INCLUDE_DIRS)
if(LIBWEBRTC_MILESTONE GREATER_EQUAL 144)
    list(APPEND AIRAN_WEBRTC_EXTRA_INCLUDE_DIRS
        third_party/webrtc/include/m144/third_party/perfetto
        third_party/webrtc/include/m144/third_party/perfetto/build_config
    )
endif()

# Include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
    src
    ${LIBWEBRTC_INCLUDE_DIR}
    ${AIRAN_WEBRTC_EXTRA_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
    spdlog
    libwebrtc
    vterm
    ${EXTRA_LIBS}
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    AIRAN_DESK_VERSION="${PROJECT_VERSION}"
    ${EXTRA_DEFINITIONS}
)

airan_use_ffmpeg_runtime(${PROJECT_NAME})

if(UNIX AND NOT APPLE)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        AIRAN_DESK_INSTALL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/airan-desk"
    )
endif()

if(MSVC AND NOT ARCH_64)
    set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS " /SAFESEH:NO")
elseif(UNIX AND NOT APPLE AND STATIC_LINK_LIBSTDCPP)
    message(STATUS "Static linking libstdc++ and libgcc enabled for ${PROJECT_NAME}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS " -static-libstdc++ -static-libgcc")
    endif()
endif()

set(CMAKE_SKIP_RPATH OFF)

# 设置 RPATH，让程序优先从相对路径查找动态库
if(UNIX AND NOT APPLE)
    # Linux: 设置 RPATH 为 $ORIGIN（可执行文件所在目录）
    # 因为库文件会被拷贝到可执行文件目录，所以只需要 $ORIGIN
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN:$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN:$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
endif()

# 设置可执行文件输出目录（兼容单/多配置）
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/release"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/release"
    )
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/debug")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release")
    endif()
endif()

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "airan-desk"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.wxalh.airan-desk"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-Info.plist"
    )
endif()

# Windows: 拷贝 DLL 到输出目录（如果存在）
if(WIN32)
    # 拷贝 OpenSSL DLL（如果存在）
    if(AIRAN_OPENSSL_RUNTIME_DLLS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${AIRAN_OPENSSL_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endif()

    # 拷贝 Qt DLL（使用 windeployqt 工具自动部署）
    get_target_property(_airan_qmake_location Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_airan_qt_install_prefix "${_airan_qmake_location}" DIRECTORY)
    get_filename_component(_airan_qt_install_prefix "${_airan_qt_install_prefix}" DIRECTORY)
    set(_airan_windeployqt "${_airan_qt_install_prefix}/bin/windeployqt.exe")

    if(EXISTS "${_airan_windeployqt}")
        set(_airan_windeployqt_env)
        if(MSVC AND CMAKE_GENERATOR_INSTANCE AND EXISTS "${CMAKE_GENERATOR_INSTANCE}/VC")
            list(APPEND _airan_windeployqt_env "VCINSTALLDIR=${CMAKE_GENERATOR_INSTANCE}/VC/")
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env ${_airan_windeployqt_env} "${_airan_windeployqt}"
            "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "正在使用 windeployqt 部署 Qt 依赖..."
        )
    else()
        message(WARNING "未找到 windeployqt 工具, Qt DLL 需要手动拷贝")
    endif()
endif()

if(APPLE)
    find_program(_airan_install_name_tool install_name_tool REQUIRED)
    get_target_property(_airan_qmake_location Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    if(_airan_qmake_location)
        get_filename_component(_airan_qt_install_prefix "${_airan_qmake_location}" DIRECTORY)
        get_filename_component(_airan_qt_install_prefix "${_airan_qt_install_prefix}" DIRECTORY)
        set(_airan_macdeployqt "${_airan_qt_install_prefix}/bin/macdeployqt")

        if(EXISTS "${_airan_macdeployqt}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND "${_airan_macdeployqt}" "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>" -always-overwrite -verbose=1
                COMMAND ${CMAKE_COMMAND}
                    "-DAIRAN_BUNDLE_DIR=$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
                    "-DAIRAN_INSTALL_NAME_TOOL=${_airan_install_name_tool}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_macos_bundle.cmake"
                COMMENT "正在使用 macdeployqt 部署 macOS app bundle..."
                VERBATIM
            )
        else()
            message(WARNING "未找到 macdeployqt 工具, macOS .app 依赖需要手动部署")
        endif()
    endif()
endif()

if(APPLE AND (NOT _airan_macdeployqt OR NOT EXISTS "${_airan_macdeployqt}"))
    message(FATAL_ERROR "macdeployqt is required to build a distributable macOS app bundle")
endif()

# Copy shared runtime libraries to the executable folder
if(TARGET spdlog AND SPDLOG_BUILD_SHARED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:spdlog>
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

if(QM_FILES)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/locale"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${QM_FILES}
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/locale"
    )
endif()

if(AIRAN_QT_TRANSLATIONS_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${AIRAN_QT_TRANSLATIONS_DIR}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/translations"
    )
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resource/conf"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/conf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.en.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/PRIVACY.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_SOURCE_OFFER.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_SOURCE_OFFER.zh-CN.md"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/doc/third_party_notices.md"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/Third-Party-Notices.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/doc/third_party_notices.zh-CN.md"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/third_party_notices.zh-CN.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/Cisco-OpenH264-BINARY_LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LIBWEBRTC_PROJECT_LICENSE_FILE}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LIBWEBRTC_PATENTS_FILE}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-PATENTS.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LIBWEBRTC_THIRD_PARTY_LICENSE_FILE}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-Third-Party-Licenses.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LIBWEBRTC_BUILD_ARGS_FILE}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-args.gn"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${LIBWEBRTC_PACKAGE_CHECKSUM_FILE}"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-package.sha256"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/LICENSE"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/spdlog-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include/spdlog/fmt/bundled/fmt.license.rst"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/fmt-LICENSE.rst"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvterm/LICENSE"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/libvterm-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/script"
    COMMAND ${CMAKE_COMMAND} -E rm -f
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/script/README.txt"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/script/README.zh-CN.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/script/airan-notify.sh"
    "${CMAKE_CURRENT_SOURCE_DIR}/script/airan-notify.bat"
    "${CMAKE_CURRENT_SOURCE_DIR}/script/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/script/README.zh-CN.md"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/script"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_FILE=${LIBWEBRTC_SOURCE_REVISION_FILE}"
        "-DDESTINATION_FILE=$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/WebRTC-source-revision.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_optional_file.cmake"
)

if(AIRAN_FFMPEG_AVAILABLE)
    foreach(_airan_ffmpeg_metadata_pair IN LISTS AIRAN_FFMPEG_METADATA_COPY_PAIRS)
        string(REPLACE "|" ";" _airan_ffmpeg_metadata_parts
            "${_airan_ffmpeg_metadata_pair}")
        list(GET _airan_ffmpeg_metadata_parts 0 _airan_ffmpeg_metadata_source)
        list(GET _airan_ffmpeg_metadata_parts 1 _airan_ffmpeg_metadata_name)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_airan_ffmpeg_metadata_source}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/${_airan_ffmpeg_metadata_name}"
        )
    endforeach()
endif()

if(AIRAN_OPENSSL_RUNTIME_DLLS)
    set(_airan_openssl_license "")
    foreach(_airan_openssl_license_candidate
            "${OPENSSL_ROOT_DIR}/license.txt"
            "${OPENSSL_ROOT_DIR}/LICENSE.txt"
            "${OPENSSL_ROOT_DIR}/LICENSE")
        if(EXISTS "${_airan_openssl_license_candidate}")
            set(_airan_openssl_license "${_airan_openssl_license_candidate}")
            break()
        endif()
    endforeach()
    if(NOT _airan_openssl_license)
        message(FATAL_ERROR "OpenSSL runtime packaging requires a license file under OPENSSL_ROOT_DIR")
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_airan_openssl_license}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/OpenSSL-LICENSE.txt"
    )
endif()

foreach(_airan_qt_license_name Qt-LGPLv3.txt Qt-GPLv3.txt)
    set(_airan_qt_license_source
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/${_airan_qt_license_name}")
    if(NOT EXISTS "${_airan_qt_license_source}")
        message(FATAL_ERROR "Qt runtime packaging requires ${_airan_qt_license_source}")
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_airan_qt_license_source}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/${_airan_qt_license_name}"
    )
endforeach()

if(APPLE)
    set(AIRAN_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
        "macOS code-signing identity; '-' performs ad-hoc signing")
    find_program(_airan_codesign codesign REQUIRED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${_airan_codesign}" --force --deep --options runtime
            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/resource/macos/airan-desk.entitlements"
            --sign "${AIRAN_MACOS_CODESIGN_IDENTITY}"
            "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
        COMMENT "Signing the deployed macOS app bundle"
        VERBATIM
    )
endif()

if(UNIX AND NOT APPLE)
    set(UDEV_RULES_INSTALL_DIR "/etc/udev/rules.d" CACHE PATH "Install directory for udev rule files")
    set(AIRAN_SYSTEM_DATADIR "/usr/local/share" CACHE PATH "System data directory for desktop integration files")
    set(AIRAN_LINUX_RUNTIME_LIB_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux-runtime/lib"
        CACHE PATH
        "Optional extra Linux runtime .so directory copied into the package lib directory")
    set(AIRAN_EXTRA_LINUX_RUNTIME_LIBS)
    if(EXISTS "${AIRAN_LINUX_RUNTIME_LIB_DIR}")
        file(GLOB AIRAN_EXTRA_LINUX_RUNTIME_LIBS
            "${AIRAN_LINUX_RUNTIME_LIB_DIR}/*.so"
            "${AIRAN_LINUX_RUNTIME_LIB_DIR}/*.so.*"
        )
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/airan-desk.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/airan-desk.desktop"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/airan-desk-autostart.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-autostart.desktop"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/install-linux.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/install-linux.sh"
        @ONLY
        NEWLINE_STYLE UNIX
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/remove-linux.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/remove-linux.sh"
        @ONLY
        NEWLINE_STYLE UNIX
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/airan-desk-wrapper.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-wrapper.sh"
        @ONLY
        NEWLINE_STYLE UNIX
    )

    add_custom_target(airan_linux_package_payload ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/airan-desk.desktop"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk.desktop"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-autostart.desktop"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk-autostart.desktop"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/install-linux.sh"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/install-linux.sh"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/remove-linux.sh"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/remove-linux.sh"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-wrapper.sh"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk-wrapper.sh"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/60-airan-desk-uinput.rules"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/60-airan-desk-uinput.rules"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/linux/airan-desk-ensure-uinput-rule.sh"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk-ensure-uinput-rule"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/icons/hicolor"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resource/icons/hicolor"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/icons/hicolor"
        COMMAND chmod +x "$<TARGET_FILE_DIR:${PROJECT_NAME}>/install-linux.sh"
        COMMAND chmod +x "$<TARGET_FILE_DIR:${PROJECT_NAME}>/remove-linux.sh"
        COMMAND chmod +x "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk-wrapper.sh"
        COMMAND chmod +x "$<TARGET_FILE_DIR:${PROJECT_NAME}>/airan-desk-ensure-uinput-rule"
    )
    add_dependencies(airan_linux_package_payload ${PROJECT_NAME})

    if(AIRAN_EXTRA_LINUX_RUNTIME_LIBS)
        file(GLOB _airan_libva_runtime_libs
            "${AIRAN_LINUX_RUNTIME_LIB_DIR}/libva.so*"
        )
        if(_airan_libva_runtime_libs)
            set(_airan_libva_metadata_dir
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/linux-runtime/metadata")
            foreach(_airan_libva_metadata_name
                    libva-LICENSE.txt
                    libva-source-revision.txt
                    libva-source-url.txt
                    libva-build-configuration.txt
                    libva-SHA256SUMS)
                if(NOT EXISTS "${_airan_libva_metadata_dir}/${_airan_libva_metadata_name}")
                    message(FATAL_ERROR
                        "libva runtime packaging requires ${_airan_libva_metadata_dir}/${_airan_libva_metadata_name}")
                endif()
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_airan_libva_metadata_dir}/${_airan_libva_metadata_name}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/licenses/${_airan_libva_metadata_name}"
                )
                install(FILES
                    "${_airan_libva_metadata_dir}/${_airan_libva_metadata_name}"
                    DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses)
            endforeach()
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${AIRAN_LINUX_RUNTIME_LIB_DIR}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lib"
        )
        foreach(_airan_extra_linux_runtime_lib ${AIRAN_EXTRA_LINUX_RUNTIME_LIBS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_airan_extra_linux_runtime_lib}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lib"
            )
        endforeach()
        install(FILES ${AIRAN_EXTRA_LINUX_RUNTIME_LIBS}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
        install(DIRECTORY "${AIRAN_LINUX_RUNTIME_LIB_DIR}/"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

    install(PROGRAMS "$<TARGET_FILE:${PROJECT_NAME}>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME airan-desk.real
    )
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-wrapper.sh"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME airan-desk
    )
    install(PROGRAMS resource/linux/airan-desk-ensure-uinput-rule.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME airan-desk-ensure-uinput-rule
    )
    if(AIRAN_FFMPEG_AVAILABLE AND AIRAN_FFMPEG_RUNTIME_DLLS)
        install(FILES ${AIRAN_FFMPEG_RUNTIME_DLLS}
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
    if(TARGET spdlog AND SPDLOG_BUILD_SHARED)
        install(FILES "$<TARGET_FILE:spdlog>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/airan-desk.desktop"
        DESTINATION ${AIRAN_SYSTEM_DATADIR}/applications
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/airan-desk-autostart.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk
    )
    install(FILES resource/linux/60-airan-desk-uinput.rules
        DESTINATION ${UDEV_RULES_INSTALL_DIR}
    )
    install(FILES resource/linux/60-airan-desk-uinput.rules
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/install-linux.sh"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/remove-linux.sh"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    install(DIRECTORY resource/icons/hicolor/
        DESTINATION ${AIRAN_SYSTEM_DATADIR}/icons/hicolor
        FILES_MATCHING PATTERN "*.png"
    )
    install(DIRECTORY resource/conf/
        DESTINATION ${CMAKE_INSTALL_BINDIR}/conf
        FILES_MATCHING
            PATTERN "*.ini"
            PATTERN "*.qss"
    )
    install(FILES LICENSE README.md README.en.md PRIVACY.md THIRD_PARTY_SOURCE_OFFER.md
        THIRD_PARTY_SOURCE_OFFER.zh-CN.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk
    )
    install(FILES doc/third_party_notices.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME Third-Party-Notices.md
    )
    install(FILES doc/third_party_notices.zh-CN.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
    )
    install(FILES third_party/licenses/Cisco-OpenH264-BINARY_LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
    )
    if(AIRAN_FFMPEG_AVAILABLE)
        foreach(_airan_ffmpeg_metadata_pair IN LISTS AIRAN_FFMPEG_METADATA_COPY_PAIRS)
            string(REPLACE "|" ";" _airan_ffmpeg_metadata_parts
                "${_airan_ffmpeg_metadata_pair}")
            list(GET _airan_ffmpeg_metadata_parts 0 _airan_ffmpeg_metadata_source)
            list(GET _airan_ffmpeg_metadata_parts 1 _airan_ffmpeg_metadata_name)
            install(FILES "${_airan_ffmpeg_metadata_source}"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
                RENAME "${_airan_ffmpeg_metadata_name}"
            )
        endforeach()
    endif()
    install(FILES "${LIBWEBRTC_PROJECT_LICENSE_FILE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME WebRTC-LICENSE.txt
    )
    install(FILES "${LIBWEBRTC_PATENTS_FILE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME WebRTC-PATENTS.txt
    )
    install(FILES "${LIBWEBRTC_THIRD_PARTY_LICENSE_FILE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME WebRTC-Third-Party-Licenses.txt
    )
    install(FILES "${LIBWEBRTC_PACKAGE_CHECKSUM_FILE}"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME WebRTC-package.sha256
    )
    if(CMAKE_CONFIGURATION_TYPES)
        install(FILES "${LIBWEBRTC_RELEASE_BUILD_ARGS_FILE}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
            RENAME WebRTC-args.gn
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        )
        install(FILES "${LIBWEBRTC_DEBUG_BUILD_ARGS_FILE}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
            RENAME WebRTC-args.gn
            CONFIGURATIONS Debug
        )
        if(LIBWEBRTC_RELEASE_SOURCE_REVISION_FILE)
            install(FILES "${LIBWEBRTC_RELEASE_SOURCE_REVISION_FILE}"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
                RENAME WebRTC-source-revision.txt
                CONFIGURATIONS Release RelWithDebInfo MinSizeRel
            )
        endif()
        if(LIBWEBRTC_DEBUG_SOURCE_REVISION_FILE)
            install(FILES "${LIBWEBRTC_DEBUG_SOURCE_REVISION_FILE}"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
                RENAME WebRTC-source-revision.txt
                CONFIGURATIONS Debug
            )
        endif()
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_airan_webrtc_install_args "${LIBWEBRTC_DEBUG_BUILD_ARGS_FILE}")
            set(_airan_webrtc_install_revision "${LIBWEBRTC_DEBUG_SOURCE_REVISION_FILE}")
        else()
            set(_airan_webrtc_install_args "${LIBWEBRTC_RELEASE_BUILD_ARGS_FILE}")
            set(_airan_webrtc_install_revision "${LIBWEBRTC_RELEASE_SOURCE_REVISION_FILE}")
        endif()
        install(FILES "${_airan_webrtc_install_args}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
            RENAME WebRTC-args.gn
        )
        if(_airan_webrtc_install_revision)
            install(FILES "${_airan_webrtc_install_revision}"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
                RENAME WebRTC-source-revision.txt
            )
        endif()
    endif()
    install(FILES third_party/spdlog/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME spdlog-LICENSE.txt
    )
    install(FILES third_party/spdlog/include/spdlog/fmt/bundled/fmt.license.rst
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME fmt-LICENSE.rst
    )
    install(FILES third_party/libvterm/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
        RENAME libvterm-LICENSE.txt
    )
    install(FILES
        third_party/licenses/Qt-LGPLv3.txt
        third_party/licenses/Qt-GPLv3.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/licenses
    )
    install(PROGRAMS script/airan-notify.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR}/script
    )
    install(FILES script/airan-notify.bat script/README.md script/README.zh-CN.md
        DESTINATION ${CMAKE_INSTALL_BINDIR}/script
    )
    if(AIRAN_QT_TRANSLATIONS_DIR)
        install(DIRECTORY "${AIRAN_QT_TRANSLATIONS_DIR}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/translations
            FILES_MATCHING PATTERN "*.qm"
        )
    endif()
    if(QM_FILES)
        install(FILES ${QM_FILES}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/airan-desk/locale
        )
    endif()
endif()
