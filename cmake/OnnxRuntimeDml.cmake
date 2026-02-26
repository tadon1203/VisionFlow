function(vf_copy_onnxruntime_runtime_dlls target_name dll_paths)
    foreach(runtime_dll ${dll_paths})
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${runtime_dll}"
                    "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endforeach()
endfunction()

function(vf_configure_onnxruntime_dml target_name)
    set(kOnnxRuntimeVersion "1.25.0")
    set(kOnnxRuntimeRoot "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${kOnnxRuntimeVersion}")
    set(kOnnxRuntimeIncludeDir "${kOnnxRuntimeRoot}/include")
    set(kOnnxRuntimeSessionIncludeDir "${kOnnxRuntimeIncludeDir}/onnxruntime/core/session")
    set(kOnnxRuntimeLibDir "")
    set(kOnnxRuntimeRuntimeDlls "")

    set(kOnnxRuntimeBuildConfig "${CMAKE_BUILD_TYPE}")
    if (NOT kOnnxRuntimeBuildConfig)
        set(kOnnxRuntimeBuildConfig "RelWithDebInfo")
    endif()
    set(kOnnxRuntimeLibDir "${kOnnxRuntimeRoot}/lib/win-x64/${kOnnxRuntimeBuildConfig}")

    set(VF_HAS_ONNXRUNTIME_DML OFF)
    if (EXISTS "${kOnnxRuntimeIncludeDir}/onnxruntime/core/session/onnxruntime_cxx_api.h"
        AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime.lib"
        AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime_providers_dml.lib"
        AND EXISTS "${kOnnxRuntimeLibDir}/onnxruntime_providers_shared.lib")
        set(VF_HAS_ONNXRUNTIME_DML ON)
        foreach(runtime_dll_name
            onnxruntime.dll
            onnxruntime_providers_shared.dll
            onnxruntime_providers_dml.dll
            DirectML.dll)
            set(runtime_dll_path "${kOnnxRuntimeLibDir}/${runtime_dll_name}")
            if (EXISTS "${runtime_dll_path}")
                list(APPEND kOnnxRuntimeRuntimeDlls "${runtime_dll_path}")
            endif()
        endforeach()

        target_include_directories(${target_name} PRIVATE
            "${kOnnxRuntimeIncludeDir}"
            "${kOnnxRuntimeSessionIncludeDir}"
        )
        target_link_directories(${target_name} PRIVATE
            "${kOnnxRuntimeLibDir}"
        )
        target_link_libraries(${target_name} PRIVATE
            onnxruntime
            onnxruntime_providers_dml
            onnxruntime_providers_shared
        )

        vf_copy_onnxruntime_runtime_dlls(${target_name} "${kOnnxRuntimeRuntimeDlls}")
        message(STATUS
            "ONNX Runtime DML enabled (config=${kOnnxRuntimeBuildConfig}, libdir=${kOnnxRuntimeLibDir})")
    else()
        message(STATUS
            "ONNX Runtime DML disabled: required files not found for config=${kOnnxRuntimeBuildConfig} "
            "under ${kOnnxRuntimeLibDir}")
    endif()

    set(VF_HAS_ONNXRUNTIME_DML ${VF_HAS_ONNXRUNTIME_DML} PARENT_SCOPE)
    set(kOnnxRuntimeLibDir "${kOnnxRuntimeLibDir}" PARENT_SCOPE)
    set(kOnnxRuntimeRuntimeDlls "${kOnnxRuntimeRuntimeDlls}" PARENT_SCOPE)
endfunction()
