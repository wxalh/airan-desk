set(WEBRTC_ROOT_DIR "${THIRD_PARTY_DIR}/webrtc" CACHE PATH "Google WebRTC static package root")
set(WEBRTC_MILESTONE "" CACHE STRING "Preferred WebRTC milestone; leave empty for package default")
set(WEBRTC_WINDOWS_PLATFORM "" CACHE STRING "Preferred Windows WebRTC package platform: win10 or win7; leave empty for package default")
set(WEBRTC_MSVC_RUNTIME "md" CACHE STRING "Preferred Windows WebRTC MSVC runtime package: md or mt")
set(WEBRTC_LINUX_STL "gnu" CACHE STRING "Preferred Linux WebRTC STL ABI package: gnu or libcxx")
set(WEBRTC_LINUX_COMPAT "ubuntu18" CACHE STRING "Preferred Linux WebRTC compatibility package: ubuntu18 or centos7")

function(_airan_webrtc_arch out_var)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES _osx_arch_count)
        if(_osx_arch_count GREATER 1)
            message(FATAL_ERROR "Universal macOS WebRTC builds are not supported by the current package selector; configure one CMAKE_OSX_ARCHITECTURES value.")
        endif()
        list(GET CMAKE_OSX_ARCHITECTURES 0 _osx_arch)
        if(_osx_arch MATCHES "^(arm64|aarch64|ARM64|AARCH64)$")
            set(${out_var} "arm64" PARENT_SCOPE)
        elseif(_osx_arch MATCHES "^(x86_64|x64|AMD64)$")
            set(${out_var} "x64" PARENT_SCOPE)
        else()
            set(${out_var} "${_osx_arch}" PARENT_SCOPE)
        endif()
    elseif(ANDROID)
        if(CMAKE_ANDROID_ARCH_ABI)
            set(${out_var} "${CMAKE_ANDROID_ARCH_ABI}" PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
            set(${out_var} "arm64-v8a" PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm")
            set(${out_var} "armeabi-v7a" PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            set(${out_var} "x86_64" PARENT_SCOPE)
        else()
            set(${out_var} "x86" PARENT_SCOPE)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64"
           OR CMAKE_VS_PLATFORM_NAME MATCHES "ARM64")
        set(${out_var} "arm64" PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|armhf|arm")
        set(${out_var} "armhf" PARENT_SCOPE)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(${out_var} "x64" PARENT_SCOPE)
    else()
        set(${out_var} "x86" PARENT_SCOPE)
    endif()
endfunction()

function(_airan_select_libwebrtc_target out_target out_platform out_milestone)
    _airan_webrtc_arch(_arch)

    if(WIN32)
        if(WEBRTC_WINDOWS_PLATFORM AND NOT WEBRTC_WINDOWS_PLATFORM MATCHES "^(win10|win7)$")
            message(FATAL_ERROR "WEBRTC_WINDOWS_PLATFORM must be win10 or win7, got: ${WEBRTC_WINDOWS_PLATFORM}")
        endif()
        string(TOLOWER "${WEBRTC_MSVC_RUNTIME}" _runtime)
        if(NOT _runtime MATCHES "^(md|mt)$")
            message(FATAL_ERROR "WEBRTC_MSVC_RUNTIME must be md or mt, got: ${WEBRTC_MSVC_RUNTIME}")
        endif()

        if(_arch STREQUAL "arm64")
            if(WEBRTC_WINDOWS_PLATFORM STREQUAL "win7")
                message(FATAL_ERROR "No win7/arm64 WebRTC package exists under ${WEBRTC_ROOT_DIR}; use win10/arm64/m144 or win7/x86/m109.")
            endif()
            set(_platform "win10")
            set(_milestone "m144")
            set(_target "libwebrtc::win10_arm64_m144_${_runtime}")
        elseif(_arch STREQUAL "x64")
            if(WEBRTC_WINDOWS_PLATFORM STREQUAL "win7")
                message(FATAL_ERROR "No win7/x64 WebRTC package exists under ${WEBRTC_ROOT_DIR}; use win7/x86/m109 or win10/x64/m144.")
            else()
                set(_platform "win10")
                set(_milestone "m144")
                set(_target "libwebrtc::win10_x64_m144_${_runtime}")
            endif()
        elseif(_arch STREQUAL "x86")
            if(WEBRTC_WINDOWS_PLATFORM STREQUAL "win10")
                message(FATAL_ERROR "No win10/x86 WebRTC package exists under ${WEBRTC_ROOT_DIR}; use win7/x86/m109, win10/x64/m144, or win10/arm64/m144.")
            endif()
            set(_platform "win7")
            set(_milestone "m109")
            set(_target "libwebrtc::win7_x86_m109_${_runtime}")
        endif()
    elseif(ANDROID)
        set(_platform "android")
        set(_milestone "m144")
        if(_arch STREQUAL "armeabi-v7a")
            set(_target "libwebrtc::android_armeabi_v7a_m144")
        elseif(_arch STREQUAL "arm64-v8a")
            set(_target "libwebrtc::android_arm64_v8a_m144")
        elseif(_arch STREQUAL "x86")
            set(_target "libwebrtc::android_x86_m144")
        elseif(_arch STREQUAL "x86_64")
            set(_target "libwebrtc::android_x86_64_m144")
        endif()
    elseif(APPLE)
        set(_platform "macos")
        set(_milestone "m144")
        if(_arch STREQUAL "arm64")
            set(_target "libwebrtc::macos_arm64_m144")
        elseif(_arch STREQUAL "x64")
            set(_target "libwebrtc::macos_x64_m144")
        endif()
    elseif(UNIX AND NOT APPLE)
        set(_platform "linux")
        set(_milestone "m144")
        string(TOLOWER "${WEBRTC_LINUX_STL}" _linux_stl)
        string(TOLOWER "${WEBRTC_LINUX_COMPAT}" _linux_compat)
        if(NOT _linux_stl MATCHES "^(gnu|libcxx)$")
            message(FATAL_ERROR "WEBRTC_LINUX_STL must be gnu or libcxx, got: ${WEBRTC_LINUX_STL}")
        endif()
        if(NOT _linux_compat MATCHES "^(ubuntu18|centos7)$")
            message(FATAL_ERROR "WEBRTC_LINUX_COMPAT must be ubuntu18 or centos7, got: ${WEBRTC_LINUX_COMPAT}")
        endif()
        if(_linux_compat STREQUAL "centos7")
            if(NOT _arch STREQUAL "x64" OR NOT _linux_stl STREQUAL "libcxx")
                message(FATAL_ERROR "The CentOS 7 WebRTC package requires x64 and WEBRTC_LINUX_STL=libcxx.")
            endif()
            set(_platform "linux-centos7")
            set(_target "libwebrtc::linux_centos7_x64_m144_libcxx")
        elseif(_arch STREQUAL "arm64")
            set(_target "libwebrtc::linux_arm64_m144_${_linux_stl}")
        elseif(_arch STREQUAL "armhf")
            set(_target "libwebrtc::linux_armhf_m144_${_linux_stl}")
        else()
            set(_target "libwebrtc::linux_x64_m144_${_linux_stl}")
        endif()
    endif()

    if(NOT _target)
        message(FATAL_ERROR "No WebRTC package target is known for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}/${_arch}")
    endif()

    if(WEBRTC_MILESTONE AND NOT WEBRTC_MILESTONE STREQUAL _milestone)
        message(FATAL_ERROR "Requested WEBRTC_MILESTONE=${WEBRTC_MILESTONE}, but ${_platform}/${_arch} uses ${_milestone}.")
    endif()

    set(${out_target} "${_target}" PARENT_SCOPE)
    set(${out_platform} "${_platform}" PARENT_SCOPE)
    string(REGEX REPLACE "^m" "" _milestone_number "${_milestone}")
    set(${out_milestone} "${_milestone_number}" PARENT_SCOPE)
endfunction()

_airan_select_libwebrtc_target(LIBWEBRTC_PACKAGE_TARGET LIBWEBRTC_PLATFORM LIBWEBRTC_MILESTONE)

if(WIN32)
    # Downstream dependency selection (notably FFmpeg) must see the resolved
    # platform even when the user left WEBRTC_WINDOWS_PLATFORM empty.
    set(WEBRTC_WINDOWS_PLATFORM "${LIBWEBRTC_PLATFORM}" CACHE STRING
        "Preferred Windows WebRTC package platform: win10 or win7; leave empty for package default" FORCE)
endif()

if(WIN32 AND LIBWEBRTC_PLATFORM STREQUAL "win7")
    set(LIBWEBRTC_WINDOWS_FAMILY "win7" CACHE STRING "WebRTC Windows package family" FORCE)
endif()
if(WIN32)
    set(LIBWEBRTC_MSVC_RUNTIME "${WEBRTC_MSVC_RUNTIME}" CACHE STRING "WebRTC Windows MSVC runtime package" FORCE)
endif()
if(UNIX AND NOT APPLE)
    set(LIBWEBRTC_LINUX_STL "${WEBRTC_LINUX_STL}" CACHE STRING "WebRTC Linux STL ABI package" FORCE)
    set(LIBWEBRTC_LINUX_COMPAT "${WEBRTC_LINUX_COMPAT}" CACHE STRING "WebRTC Linux compatibility package" FORCE)
endif()

# Keep CMake's package cache aligned when switching between the Win7 and
# Win10 package roots in an existing build directory.
set(LibWebRTC_DIR "${WEBRTC_ROOT_DIR}" CACHE PATH
    "Selected LibWebRTC package directory" FORCE)

find_package(LibWebRTC CONFIG QUIET PATHS "${WEBRTC_ROOT_DIR}" NO_DEFAULT_PATH)

if(NOT TARGET "${LIBWEBRTC_PACKAGE_TARGET}")
    if(LibWebRTC_NOT_FOUND_MESSAGE)
        message(STATUS "LibWebRTC package auto-selection message: ${LibWebRTC_NOT_FOUND_MESSAGE}")
    endif()
    message(FATAL_ERROR "WebRTC package target ${LIBWEBRTC_PACKAGE_TARGET} was not found in ${WEBRTC_ROOT_DIR}")
endif()

add_library(libwebrtc INTERFACE)
set_property(TARGET libwebrtc PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
set(LIBWEBRTC_PERFETTO_BUILD_CONFIG_DIR
    "${WEBRTC_ROOT_DIR}/include/m${LIBWEBRTC_MILESTONE}/third_party/perfetto/build_config")
set(LIBWEBRTC_PERFETTO_ROOT_DIR
    "${WEBRTC_ROOT_DIR}/include/m${LIBWEBRTC_MILESTONE}/third_party/perfetto")
set(LIBWEBRTC_DEBUG_PACKAGE_TARGET "${LIBWEBRTC_PACKAGE_TARGET}_debug")
if(TARGET "${LIBWEBRTC_DEBUG_PACKAGE_TARGET}")
    target_link_libraries(libwebrtc INTERFACE
        "$<$<CONFIG:Debug>:${LIBWEBRTC_DEBUG_PACKAGE_TARGET}>"
        "$<$<NOT:$<CONFIG:Debug>>:${LIBWEBRTC_PACKAGE_TARGET}>"
    )
else()
    unset(LIBWEBRTC_DEBUG_PACKAGE_TARGET)
    target_link_libraries(libwebrtc INTERFACE "${LIBWEBRTC_PACKAGE_TARGET}")
endif()
if(EXISTS "${LIBWEBRTC_PERFETTO_BUILD_CONFIG_DIR}/perfetto_build_flags.h")
    target_include_directories(libwebrtc INTERFACE
        "${LIBWEBRTC_PERFETTO_ROOT_DIR}"
        "${LIBWEBRTC_PERFETTO_BUILD_CONFIG_DIR}")
endif()

target_compile_definitions(libwebrtc INTERFACE
    AIRAN_USE_GOOGLE_WEBRTC=1
    AIRAN_WEBRTC_MILESTONE=${LIBWEBRTC_MILESTONE}
)

if(WIN32)
    target_compile_definitions(libwebrtc INTERFACE
        _CRT_SECURE_NO_WARNINGS
    )
    if(LIBWEBRTC_PLATFORM STREQUAL "win7")
        target_compile_definitions(libwebrtc INTERFACE
            AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN7=1
            WINVER=0x0601
            _WIN32_WINNT=0x0601
        )
    else()
        target_compile_definitions(libwebrtc INTERFACE
            AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10=1
            RTC_ENABLE_WIN_WGC=1
            WINVER=0x0A00
            _WIN32_WINNT=0x0A00
        )
    endif()
endif()

get_target_property(LIBWEBRTC_LIBRARY "${LIBWEBRTC_PACKAGE_TARGET}" IMPORTED_LOCATION)
get_target_property(LIBWEBRTC_INCLUDE_DIR "${LIBWEBRTC_PACKAGE_TARGET}" INTERFACE_INCLUDE_DIRECTORIES)
if(LIBWEBRTC_DEBUG_PACKAGE_TARGET)
    get_target_property(LIBWEBRTC_DEBUG_LIBRARY
        "${LIBWEBRTC_DEBUG_PACKAGE_TARGET}" IMPORTED_LOCATION)
else()
    set(LIBWEBRTC_DEBUG_LIBRARY "${LIBWEBRTC_LIBRARY}")
endif()

function(_airan_webrtc_metadata_for_library library_path output_prefix)
    get_filename_component(_library_dir "${library_path}" DIRECTORY)
    file(RELATIVE_PATH _slice_relative_dir
        "${WEBRTC_ROOT_DIR}/lib" "${_library_dir}")
    if(_slice_relative_dir MATCHES "^\\.\\." OR
       IS_ABSOLUTE "${_slice_relative_dir}")
        message(FATAL_ERROR
            "Selected WebRTC library is outside ${WEBRTC_ROOT_DIR}/lib: ${library_path}")
    endif()

    set(_metadata_dir "${WEBRTC_ROOT_DIR}/meta/${_slice_relative_dir}")
    set(_build_args_file "${_metadata_dir}/args.gn")
    if(NOT EXISTS "${_build_args_file}")
        message(FATAL_ERROR
            "Selected WebRTC package is missing build arguments: ${_build_args_file}")
    endif()

    set(_source_revision_file "")
    if(EXISTS "${_metadata_dir}/source_revision.txt")
        set(_source_revision_file "${_metadata_dir}/source_revision.txt")
    endif()

    set(${output_prefix}_METADATA_DIR "${_metadata_dir}" PARENT_SCOPE)
    set(${output_prefix}_BUILD_ARGS_FILE "${_build_args_file}" PARENT_SCOPE)
    set(${output_prefix}_SOURCE_REVISION_FILE
        "${_source_revision_file}" PARENT_SCOPE)
endfunction()

_airan_webrtc_metadata_for_library(
    "${LIBWEBRTC_LIBRARY}" LIBWEBRTC_RELEASE)
_airan_webrtc_metadata_for_library(
    "${LIBWEBRTC_DEBUG_LIBRARY}" LIBWEBRTC_DEBUG)

set(LIBWEBRTC_METADATA_DIR
    "$<IF:$<CONFIG:Debug>,${LIBWEBRTC_DEBUG_METADATA_DIR},${LIBWEBRTC_RELEASE_METADATA_DIR}>")
set(LIBWEBRTC_BUILD_ARGS_FILE
    "$<IF:$<CONFIG:Debug>,${LIBWEBRTC_DEBUG_BUILD_ARGS_FILE},${LIBWEBRTC_RELEASE_BUILD_ARGS_FILE}>")
set(LIBWEBRTC_SOURCE_REVISION_FILE
    "$<IF:$<CONFIG:Debug>,${LIBWEBRTC_DEBUG_SOURCE_REVISION_FILE},${LIBWEBRTC_RELEASE_SOURCE_REVISION_FILE}>")
get_target_property(LIBWEBRTC_THIRD_PARTY_LICENSE_FILE
    "${LIBWEBRTC_PACKAGE_TARGET}" INTERFACE_LIBWEBRTC_LICENSE_FILE)
get_target_property(LIBWEBRTC_PROJECT_LICENSE_FILE
    "${LIBWEBRTC_PACKAGE_TARGET}" INTERFACE_LIBWEBRTC_PROJECT_LICENSE_FILE)
get_target_property(LIBWEBRTC_PATENTS_FILE
    "${LIBWEBRTC_PACKAGE_TARGET}" INTERFACE_LIBWEBRTC_PATENTS_FILE)

if(NOT LIBWEBRTC_THIRD_PARTY_LICENSE_FILE OR
   LIBWEBRTC_THIRD_PARTY_LICENSE_FILE MATCHES "-NOTFOUND$" OR
   NOT EXISTS "${LIBWEBRTC_THIRD_PARTY_LICENSE_FILE}")
    set(LIBWEBRTC_THIRD_PARTY_LICENSE_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/WebRTC-m${LIBWEBRTC_MILESTONE}-Third-Party-Licenses.txt")
endif()
if(NOT LIBWEBRTC_PROJECT_LICENSE_FILE OR
   LIBWEBRTC_PROJECT_LICENSE_FILE MATCHES "-NOTFOUND$" OR
   NOT EXISTS "${LIBWEBRTC_PROJECT_LICENSE_FILE}")
    set(LIBWEBRTC_PROJECT_LICENSE_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/WebRTC-LICENSE.txt")
endif()
if(NOT LIBWEBRTC_PATENTS_FILE OR
   LIBWEBRTC_PATENTS_FILE MATCHES "-NOTFOUND$" OR
   NOT EXISTS "${LIBWEBRTC_PATENTS_FILE}")
    set(LIBWEBRTC_PATENTS_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/WebRTC-PATENTS.txt")
endif()
foreach(_airan_webrtc_legal_file
        LIBWEBRTC_THIRD_PARTY_LICENSE_FILE
        LIBWEBRTC_PROJECT_LICENSE_FILE
        LIBWEBRTC_PATENTS_FILE)
    if(NOT EXISTS "${${_airan_webrtc_legal_file}}")
        message(FATAL_ERROR "Selected WebRTC package is missing required legal file: ${${_airan_webrtc_legal_file}}")
    endif()
endforeach()

message(STATUS "Using Google WebRTC package target: ${LIBWEBRTC_PACKAGE_TARGET}")
if(LIBWEBRTC_DEBUG_PACKAGE_TARGET)
    message(STATUS "Using Google WebRTC debug package target: ${LIBWEBRTC_DEBUG_PACKAGE_TARGET}")
    message(STATUS "Using Google WebRTC debug static library: ${LIBWEBRTC_DEBUG_LIBRARY}")
endif()
message(STATUS "Using Google WebRTC static library: ${LIBWEBRTC_LIBRARY}")
message(STATUS "Using Google WebRTC includes: ${LIBWEBRTC_INCLUDE_DIR}")
message(STATUS "Using Google WebRTC dependency licenses: ${LIBWEBRTC_THIRD_PARTY_LICENSE_FILE}")
message(STATUS "Using Google WebRTC release build arguments: ${LIBWEBRTC_RELEASE_BUILD_ARGS_FILE}")
message(STATUS "Using Google WebRTC debug build arguments: ${LIBWEBRTC_DEBUG_BUILD_ARGS_FILE}")
if(NOT LIBWEBRTC_RELEASE_SOURCE_REVISION_FILE OR
   NOT LIBWEBRTC_DEBUG_SOURCE_REVISION_FILE)
    message(STATUS "Selected Google WebRTC slice has no exact source revision metadata; release packaging must reject it")
endif()
