if(NOT DEFINED AIRAN_BUNDLE_DIR OR NOT DEFINED AIRAN_INSTALL_NAME_TOOL)
    message(FATAL_ERROR "AIRAN_BUNDLE_DIR and AIRAN_INSTALL_NAME_TOOL are required")
endif()

find_program(_airan_otool NAMES otool REQUIRED)

file(GLOB_RECURSE _airan_qt_plugins
    LIST_DIRECTORIES FALSE
    "${AIRAN_BUNDLE_DIR}/Contents/PlugIns/*.dylib")

foreach(_airan_qt_plugin IN LISTS _airan_qt_plugins)
    get_filename_component(_airan_qt_plugin_name "${_airan_qt_plugin}" NAME)
    execute_process(
        COMMAND "${AIRAN_INSTALL_NAME_TOOL}"
            -id "@rpath/${_airan_qt_plugin_name}"
            "${_airan_qt_plugin}"
        RESULT_VARIABLE _airan_install_name_result
        ERROR_VARIABLE _airan_install_name_error
    )
    if(NOT _airan_install_name_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to repair the Qt plugin install name for "
            "${_airan_qt_plugin}: "
            "${_airan_install_name_error}")
    endif()
endforeach()

file(GLOB_RECURSE _airan_bundle_files
    LIST_DIRECTORIES FALSE
    "${AIRAN_BUNDLE_DIR}/Contents/*")

foreach(_airan_bundle_file IN LISTS _airan_bundle_files)
    execute_process(
        COMMAND "${_airan_otool}" -l "${_airan_bundle_file}"
        RESULT_VARIABLE _airan_otool_result
        OUTPUT_VARIABLE _airan_load_commands
        ERROR_QUIET
    )
    if(NOT _airan_otool_result EQUAL 0)
        continue()
    endif()

    string(REGEX MATCHALL
        "path [^ \r\n]+ \\(offset [0-9]+\\)"
        _airan_rpath_commands
        "${_airan_load_commands}")
    set(_airan_absolute_rpaths)
    foreach(_airan_rpath_command IN LISTS _airan_rpath_commands)
        string(REGEX REPLACE
            "^path ([^ \r\n]+) \\(offset [0-9]+\\)$"
            "\\1"
            _airan_rpath
            "${_airan_rpath_command}")
        if(_airan_rpath MATCHES "^/")
            list(APPEND _airan_absolute_rpaths "${_airan_rpath}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _airan_absolute_rpaths)

    foreach(_airan_rpath IN LISTS _airan_absolute_rpaths)
        execute_process(
            COMMAND "${AIRAN_INSTALL_NAME_TOOL}"
                -delete_rpath "${_airan_rpath}"
                "${_airan_bundle_file}"
            RESULT_VARIABLE _airan_delete_rpath_result
            ERROR_VARIABLE _airan_delete_rpath_error
        )
        if(NOT _airan_delete_rpath_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to remove the nonportable rpath ${_airan_rpath} from "
                "${_airan_bundle_file}: ${_airan_delete_rpath_error}")
        endif()
    endforeach()
endforeach()
