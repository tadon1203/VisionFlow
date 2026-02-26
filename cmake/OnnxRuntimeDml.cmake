function(vf_add_onnxruntime_runtime_copy_command target_name onnxruntime_root)
    set(kCopyScriptPath
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/vf_copy_onnxruntime_runtime_dlls_${target_name}.cmake")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake")
    file(WRITE "${kCopyScriptPath}" [=[
set(kOnnxRuntimeLibDir "${ONNX_RUNTIME_ROOT}/lib/win-x64/${ONNX_RUNTIME_CONFIG}")
foreach(runtime_dll_name
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    onnxruntime_providers_dml.dll
    DirectML.dll)
    set(runtime_dll_path "${kOnnxRuntimeLibDir}/${runtime_dll_name}")
    if (EXISTS "${runtime_dll_path}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${runtime_dll_path}"
                    "${ONNX_RUNTIME_TARGET_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()
]=])

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                "-DONNX_RUNTIME_ROOT=${onnxruntime_root}"
                "-DONNX_RUNTIME_CONFIG=$<CONFIG>"
                "-DONNX_RUNTIME_TARGET_DIR=$<TARGET_FILE_DIR:${target_name}>"
                -P "${kCopyScriptPath}"
        VERBATIM)
endfunction()

function(vf_configure_onnxruntime_dml target_name)
    set(kOnnxRuntimeVersion "1.25.0")
    set(kOnnxRuntimeRoot "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${kOnnxRuntimeVersion}")
    set(kOnnxRuntimeIncludeDir "${kOnnxRuntimeRoot}/include")
    set(kOnnxRuntimeSessionIncludeDir "${kOnnxRuntimeIncludeDir}/onnxruntime/core/session")

    if (CMAKE_CONFIGURATION_TYPES)
        set(kCandidateConfigs ${CMAKE_CONFIGURATION_TYPES})
    else()
        set(kCandidateConfigs "${CMAKE_BUILD_TYPE}")
        if (NOT kCandidateConfigs)
            set(kCandidateConfigs "RelWithDebInfo")
        endif()
    endif()
    list(REMOVE_DUPLICATES kCandidateConfigs)

    set(kDetectedConfigs "")
    foreach(config_name IN LISTS kCandidateConfigs)
        set(kOnnxRuntimeLibDir "${kOnnxRuntimeRoot}/lib/win-x64/${config_name}")
        if (EXISTS "${kOnnxRuntimeIncludeDir}/onnxruntime/core/session/onnxruntime_cxx_api.h"
            AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime.lib"
            AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime_providers_dml.lib"
            AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime_providers_shared.lib")
            list(APPEND kDetectedConfigs "${config_name}")
        endif()
    endforeach()

    set(kEnabledDefinition "0")
    if (kDetectedConfigs)
        target_include_directories(${target_name} PRIVATE
            "${kOnnxRuntimeIncludeDir}"
            "${kOnnxRuntimeSessionIncludeDir}"
        )

        set(kOnnxRuntimeLinkItems "")
        foreach(config_name IN LISTS kDetectedConfigs)
            set(kOnnxRuntimeLibDir "${kOnnxRuntimeRoot}/lib/win-x64/${config_name}")
            list(APPEND kOnnxRuntimeLinkItems
                "$<$<CONFIG:${config_name}>:${kOnnxRuntimeLibDir}/onnxruntime.lib>"
                "$<$<CONFIG:${config_name}>:${kOnnxRuntimeLibDir}/onnxruntime_providers_dml.lib>"
                "$<$<CONFIG:${config_name}>:${kOnnxRuntimeLibDir}/onnxruntime_providers_shared.lib>"
            )
        endforeach()
        target_link_libraries(${target_name} PRIVATE ${kOnnxRuntimeLinkItems})

        vf_add_onnxruntime_runtime_copy_command(${target_name} "${kOnnxRuntimeRoot}")

        if (CMAKE_CONFIGURATION_TYPES)
            set(kConfigExpressions "")
            foreach(config_name IN LISTS kDetectedConfigs)
                list(APPEND kConfigExpressions "$<CONFIG:${config_name}>")
            endforeach()
            if (kConfigExpressions)
                list(JOIN kConfigExpressions "," kOrArgs)
                set(kEnabledDefinition "$<IF:$<OR:${kOrArgs}>,1,0>")
            endif()
            message(STATUS
                "ONNX Runtime DML enabled for target=${target_name} in configs: ${kDetectedConfigs}")
        else()
            set(kEnabledDefinition "1")
            message(STATUS
                "ONNX Runtime DML enabled for target=${target_name} in config=${kDetectedConfigs}")
        endif()
    else()
        if (CMAKE_CONFIGURATION_TYPES)
            message(STATUS
                "ONNX Runtime DML disabled for target=${target_name}: no matching config libs under "
                "${kOnnxRuntimeRoot}/lib/win-x64")
        else()
            message(STATUS
                "ONNX Runtime DML disabled for target=${target_name} in config=${kCandidateConfigs}")
        endif()
    endif()

    target_compile_definitions(${target_name} PRIVATE VF_HAS_ONNXRUNTIME_DML=${kEnabledDefinition})
endfunction()
