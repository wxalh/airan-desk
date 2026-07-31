function(airan_select_libstdcpp_abi_definition out_var system_name compiler_id linux_stl)
    string(TOLOWER "${linux_stl}" _linux_stl)
    set(_definition "")

    if(system_name STREQUAL "Linux"
       AND compiler_id MATCHES "^(GNU|Clang)$"
       AND _linux_stl STREQUAL "gnu")
        set(_definition "_GLIBCXX_USE_CXX11_ABI=1")
    endif()

    set(${out_var} "${_definition}" PARENT_SCOPE)
endfunction()

function(airan_select_libstdcpp_abi_options out_var system_name compiler_id linux_stl)
    airan_select_libstdcpp_abi_definition(_definition "${system_name}" "${compiler_id}" "${linux_stl}")
    if(_definition)
        set(${out_var}
            "-U_GLIBCXX_USE_CXX11_ABI"
            "-D${_definition}"
            PARENT_SCOPE
        )
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()
