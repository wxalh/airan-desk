cmake_minimum_required(VERSION 3.21)

project(airan-desk)

if(MSVC)
    add_definitions(-DUNICODE -D_UNICODE)
    add_compile_options(/utf-8 /wd4819 /wd4319 /wd5105)
endif()

set(ENABLE_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automoc, autouic, and autorcc for Qt project
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Detect 64-bit vs 32-bit
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_64 TRUE)
else()
    set(ARCH_64 FALSE)
endif()

# Find Qt modules
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets WebSockets)

if(WIN32)
    set(OPENSSL_USE_STATIC_LIBS OFF CACHE BOOL "" FORCE)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include" CACHE PATH "Path to OpenSSL include directory" FORCE)
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.lib" CACHE FILEPATH "Path to OpenSSL crypto library" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.lib" CACHE FILEPATH "Path to OpenSSL SSL library" FORCE)
    find_package(OpenSSL REQUIRED)

    message(STATUS "[OpenSSL] Found version: ${OPENSSL_VERSION}")
    message(STATUS "[OpenSSL] Include: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "[OpenSSL] SSL Library: ${OPENSSL_SSL_LIBRARY}")
    message(STATUS "[OpenSSL] Crypto Library: ${OPENSSL_CRYPTO_LIBRARY}")
endif()

# ---------------------------
# third_party dependencies (source build)
# ---------------------------
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ===== spdlog =====
# 只编译依赖库：关闭安装/示例/测试/benchmark
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
# 使用共享库构建 spdlog（便于运行时动态链接和调试）
set(SPDLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
# 使用spdlog自带fmt（避免额外引入fmt依赖）
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "" FORCE)
# 强制使用header-only fmt（spdlog内置 third_party/spdlog/include/spdlog/fmt/bundled）
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_DIR}/spdlog)

# ===== libdatachannel =====
# 只编译依赖库：关闭示例/测试
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build libraries as shared" FORCE)

# libdatachannel 自身也提供 WARNINGS_AS_ERRORS 选项
set(WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# 优先使用子模块（避免系统依赖探测），但加密库希望走 Qt 自带 OpenSSL
set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)

add_subdirectory(${THIRD_PARTY_DIR}/libdatachannel)
set(LIBDATACHANNEL_INCLUDE_DIR "${THIRD_PARTY_DIR}/libdatachannel/include" CACHE PATH "libdatachannel 头文件目录(包含 libdatachannel/*.h)")

# 这里在找到 OpenSSL 后为 datachannel 目标补齐 Crypto 依赖。
if(TARGET OpenSSL::Crypto)
    if(TARGET datachannel)
        target_link_libraries(datachannel PRIVATE OpenSSL::Crypto OpenSSL::SSL)
    endif()
    if(TARGET datachannel-static)
        # 如果静态目标存在也尝试补齐（兼容多种构建变体）
        target_link_libraries(datachannel-static PRIVATE OpenSSL::Crypto OpenSSL::SSL)
    endif()
endif()
# ===== FFmpeg =====
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT_DIR}/include" CACHE PATH "FFmpeg 头文件目录(包含 libavcodec 等子目录)")
set(FFMPEG_LIBRARY_DIR "${FFMPEG_ROOT_DIR}/lib" CACHE PATH "FFmpeg 库目录(包含 avcodec.lib/avformat.lib 等导入库)")
set(FFMPEG_BINARY_DIR "${FFMPEG_ROOT_DIR}/bin" CACHE PATH "FFmpeg 动态库目录(Windows下用于拷贝 DLL)")

# 典型最小链接集合（按需增减）
set(FFMPEG_LIBRARIES
    avcodec
    avdevice
    avfilter
    avformat
    avutil
    swresample
    swscale
)

set(SRC_FILES
    src/codec/h264_decoder.cpp
    src/codec/h264_encoder.cpp
    src/codec/hardware_context_manager.cpp
    src/common/logger_manager.cpp
    src/control_window.cpp
    src/desktop/desktop_capture_manager.cpp
    src/desktop/desktop_capture_worker.cpp
    src/desktop/desktop_grab.cpp
    src/desktop/desktop_grab_qt.cpp
    src/file_transfer_window.cpp
    src/main.cpp
    src/main_window.cpp
    src/util/config_util.cpp
    src/util/convert_util.cpp
    src/util/ffmpeg_util.cpp
    src/util/file_packet_util.cpp
    src/util/input_util.cpp
    src/util/json_util.cpp
    src/webrtc/webrtc_cli.cpp
    src/webrtc/webrtc_ctl.cpp
    src/websocket/ws_cli.cpp
)

set(HDR_FILES
    src/codec/h264_decoder.h
    src/codec/h264_encoder.h
    src/codec/hardware_context_manager.h
    src/codec/codec_info.h
    src/common/constant.h
    src/common/logger_manager.h
    src/control_window.h
    src/desktop/desktop_capture_manager.h
    src/desktop/desktop_capture_worker.h
    src/desktop/desktop_grab.h
    src/desktop/desktop_grab_qt.h
    src/file_transfer_window.h
    src/main_window.h
    src/util/config_util.h
    src/util/convert_util.h
    src/util/ffmpeg_util.h
    src/util/file_packet_util.h
    src/util/input_util.h
    src/util/json_util.h
    src/util/key_util.h
    src/webrtc/webrtc_cli.h
    src/webrtc/webrtc_ctl.h
    src/websocket/ws_cli.h
)

set(UI_FILES
    src/file_transfer_window.ui
    src/main_window.ui
)

if(WIN32)
    list(APPEND EXTRA_LIBS
        advapi32 bcrypt ole32 shell32 psapi opengl32 gdi32 secur32 crypt32 user32
    )
    if(COMPATIBLE_WIN7)
        message(STATUS "启用 Windows 7 兼容模式（禁用 DXGI/ WGC 捕获）")
    else()
        message(STATUS "启用 Windows 8+ 专用功能（如 DXGI 捕获）")
        list(APPEND EXTRA_LIBS
            ncrypt vfw32 ws2_32 strmiids uuid dxgi dxguid d3d11 windowsapp
        )
        list(APPEND HDR_FILES
            src/desktop/desktop_grab_dxgi.h
            src/desktop/desktop_grab_wgc.h
        )
        list(APPEND SRC_FILES
            src/desktop/desktop_grab_dxgi.cpp
            src/desktop/desktop_grab_wgc.cpp
            src/codec/h264_encoder_zerocopy_win.cpp
        )
    endif()
elseif(APPLE)
    list(APPEND EXTRA_LIBS
        "-framework Carbon" "-framework CoreGraphics"
        "-framework AudioToolbox" "-framework VideoToolbox" "-framework CoreVideo"
        "-framework CoreMedia" "-framework AVFoundation" "-framework CoreFoundation"
        "-framework Security" "-framework QuartzCore" "-framework OpenGL"
    )
elseif(UNIX)
    # 可选：在 Linux 下静态链接 libstdc++/libgcc，默认关闭
    option(STATIC_LINK_LIBSTDCPP "Link libstdc++ and libgcc statically on Linux" ON)

    # 注意：input_util.cpp 已改用 uinput（内核虚拟设备），不再依赖 Xtst（XTest）
    list(APPEND EXTRA_LIBS
        pthread dl rt m X11 Xext Xfixes Xv z asound
    )
endif()

# Windows UAC manifest（requireAdministrator）—— 嵌入资源文件以避免 UIPI 阻断 SendInput
if(WIN32)
    set(RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resource/main.rc")
    # RC 编译时需要能找到 uac.manifest，指定资源目录作为 include 路径
    if(MSVC)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/resource\"")
    else()
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I\"${CMAKE_CURRENT_SOURCE_DIR}/resource\"")
    endif()
endif()

# Add executable
if(WIN32)
    add_executable(${PROJECT_NAME} WIN32
        ${SRC_FILES}
        ${HDR_FILES}
        ${UI_FILES}
        ${RC_FILE}
    )
else()
    add_executable(${PROJECT_NAME}
        ${SRC_FILES}
        ${HDR_FILES}
        ${UI_FILES}
        ${RC_FILE}
    )
endif()

if(MSVC)
    # main.rc 已嵌入 MANIFEST，关闭链接器自动生成以避免 CVT1100 重复资源
    target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)
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

# Include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
    src
    ${FFMPEG_INCLUDE_DIR}
    ${LIBDATACHANNEL_INCLUDE_DIR}
)

# Link libraries
target_link_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_LIBRARY_DIR}" "${FFMPEG_BINARY_DIR}")

# 将库链接到可执行目标
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::WebSockets
    spdlog
    datachannel
    ${FFMPEG_LIBRARIES}
    ${EXTRA_LIBS}
)
if(WIN32 AND COMPATIBLE_WIN7)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        AIRAN_COMPATIBLE_WIN7=$<BOOL:${COMPATIBLE_WIN7}>
    )
endif()
if(MSVC AND NOT ARCH_64)  # 仅 x86 (32位) + 交叉编译的ffmpeg需要开启
    target_link_options(${PROJECT_NAME} PRIVATE /SAFESEH:NO)
# 如果启用静态链接选项，则添加链接器选项
elseif(UNIX AND NOT APPLE AND STATIC_LINK_LIBSTDCPP)
    message(STATUS "Static linking libstdc++ and libgcc enabled for ${PROJECT_NAME}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

# 立即启用 origin 用于构建时 rpath（使 $ORIGIN 在 build_rpath 中起作用）
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_SKIP_RPATH OFF)

# 设置 RPATH，让程序优先从相对路径查找动态库
if(UNIX AND NOT APPLE)
    # Linux: 设置 RPATH 为 $ORIGIN（可执行文件所在目录）
    # 因为库文件会被拷贝到可执行文件目录，所以只需要 $ORIGIN
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
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
    # 单配置生成器（如 Ninja）：根据 CMAKE_BUILD_TYPE 决定输出目录
    if(NOT CMAKE_BUILD_TYPE)
        # 默认到 release
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/debug")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/release")
    endif()
    # 也为 multi-config 生成器提供通用占位（不会影响单配置）
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<CONFIG>")
endif()

# Windows: 拷贝 DLL 到输出目录（如果存在）
if(WIN32)
    # 拷贝 FFmpeg DLL（如果存在）
    file(GLOB _FFMPEG_DLLS "${FFMPEG_BINARY_DIR}/*.dll")
    if(_FFMPEG_DLLS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_FFMPEG_DLLS}
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endif()

    # 拷贝 OpenSSL DLL（如果存在）
    if(OPENSSL_ROOT_DIR)
        file(GLOB _OPENSSL_DLLS
            "${OPENSSL_ROOT_DIR}/bin/*.dll"
            "${OPENSSL_ROOT_DIR}/bin/libcrypto-*.dll"
            "${OPENSSL_ROOT_DIR}/bin/libssl-*.dll"
        )
        if(_OPENSSL_DLLS)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_OPENSSL_DLLS}
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            )
        endif()
    endif()

    # 拷贝 Qt DLL（使用 windeployqt 工具自动部署）
    get_target_property(_qt5_qmake_location Qt5::qmake IMPORTED_LOCATION)
    get_filename_component(_qt5_install_prefix "${_qt5_qmake_location}" DIRECTORY)
    get_filename_component(_qt5_install_prefix "${_qt5_install_prefix}" DIRECTORY)
    set(_qt5_windeployqt "${_qt5_install_prefix}/bin/windeployqt.exe")

    if(EXISTS "${_qt5_windeployqt}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${_qt5_windeployqt}"
            "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "正在使用 windeployqt 部署 Qt 依赖..."
        )
    else()
        message(WARNING "未找到 windeployqt 工具, Qt DLL 需要手动拷贝")
    endif()
endif()

# Copy spdlog and datachannel shared libraries to the executable folder (if built as shared targets)
if(TARGET spdlog AND SPDLOG_BUILD_SHARED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:spdlog>
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

if(TARGET datachannel AND BUILD_SHARED_LIBS)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:datachannel>
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resource/locale"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/locale"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resource/conf"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/conf"
)
