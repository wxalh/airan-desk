set(AIRAN_OPENSSL_RUNTIME_DLLS "" CACHE INTERNAL "OpenSSL runtime DLLs to deploy")

if(WIN32)
    option(AIRAN_DEPLOY_OPENSSL_RUNTIME "Copy OpenSSL runtime DLLs next to the executable when detected" ON)

    if(AIRAN_DEPLOY_OPENSSL_RUNTIME)
        set(OPENSSL_USE_STATIC_LIBS OFF CACHE BOOL "Prefer dynamic OpenSSL runtime for Qt TLS support")

        foreach(_openssl_cache_var OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY)
            if(DEFINED ${_openssl_cache_var}
               AND "${${_openssl_cache_var}}" STREQUAL ""
               AND DEFINED CACHE{${_openssl_cache_var}})
                unset(${_openssl_cache_var} CACHE)
            elseif(DEFINED ${_openssl_cache_var}
                   AND NOT EXISTS "${${_openssl_cache_var}}"
                   AND DEFINED CACHE{${_openssl_cache_var}})
                unset(${_openssl_cache_var} CACHE)
            endif()
        endforeach()

        find_package(OpenSSL QUIET)

        if(OpenSSL_FOUND)
            set(_openssl_runtime_dirs)
            if(OPENSSL_ROOT_DIR)
                list(APPEND _openssl_runtime_dirs "${OPENSSL_ROOT_DIR}/bin")
            endif()
            if(OPENSSL_INCLUDE_DIR)
                get_filename_component(_openssl_include_parent "${OPENSSL_INCLUDE_DIR}/.." ABSOLUTE)
                list(APPEND _openssl_runtime_dirs "${_openssl_include_parent}/bin")
            endif()
            foreach(_openssl_lib IN ITEMS "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}")
                if(_openssl_lib)
                    get_filename_component(_openssl_lib_dir "${_openssl_lib}" DIRECTORY)
                    list(APPEND _openssl_runtime_dirs
                        "${_openssl_lib_dir}"
                        "${_openssl_lib_dir}/../bin"
                        "${_openssl_lib_dir}/../../bin"
                    )
                endif()
            endforeach()

            set(_openssl_runtime_dlls)
            foreach(_openssl_runtime_dir IN LISTS _openssl_runtime_dirs)
                get_filename_component(_openssl_runtime_dir_abs "${_openssl_runtime_dir}" ABSOLUTE)
                if(EXISTS "${_openssl_runtime_dir_abs}")
                    file(GLOB _openssl_runtime_dir_dlls
                        "${_openssl_runtime_dir_abs}/libcrypto-*.dll"
                        "${_openssl_runtime_dir_abs}/libssl-*.dll"
                    )
                    list(APPEND _openssl_runtime_dlls ${_openssl_runtime_dir_dlls})
                endif()
            endforeach()
            if(_openssl_runtime_dlls)
                list(REMOVE_DUPLICATES _openssl_runtime_dlls)
                set(AIRAN_OPENSSL_RUNTIME_DLLS "${_openssl_runtime_dlls}" CACHE INTERNAL "OpenSSL runtime DLLs to deploy")
                message(STATUS "[OpenSSL] Runtime DLLs detected: ${AIRAN_OPENSSL_RUNTIME_DLLS}")
            else()
                set(AIRAN_OPENSSL_RUNTIME_DLLS "" CACHE INTERNAL "OpenSSL runtime DLLs to deploy")
                message(STATUS "[OpenSSL] Found ${OPENSSL_VERSION}, but runtime DLLs were not found; skipping deployment")
            endif()
        else()
            set(AIRAN_OPENSSL_RUNTIME_DLLS "" CACHE INTERNAL "OpenSSL runtime DLLs to deploy")
            message(STATUS "[OpenSSL] Not found; skipping runtime DLL deployment")
        endif()
    endif()
endif()
