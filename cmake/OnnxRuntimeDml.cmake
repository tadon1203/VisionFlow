function(vf_add_onnxruntime_runtime_copy_command target_name onnxruntime_root)
    set(kCopyScriptPath
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/vf_copy_onnxruntime_runtime_dlls_${target_name}.cmake")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake")
    file(WRITE "${kCopyScriptPath}" [=[
set(kOnnxRuntimeLibDir "${ONNX_RUNTIME_ROOT}/lib/win-x64/${ONNX_RUNTIME_CONFIG}")
set(kRuntimeDllPaths "")
foreach(runtime_dll_name
    onnxruntime.dll
    onnxruntime_providers_shared.dll
    onnxruntime_providers_dml.dll
    DirectML.dll)
    set(runtime_dll_path "${kOnnxRuntimeLibDir}/${runtime_dll_name}")
    if (EXISTS "${runtime_dll_path}")
        list(APPEND kRuntimeDllPaths "${runtime_dll_path}")
    endif()
endforeach()

if (kRuntimeDllPaths)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${kRuntimeDllPaths}
                "${ONNX_RUNTIME_TARGET_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
]=])

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                "-DONNX_RUNTIME_ROOT=${onnxruntime_root}"
                "-DONNX_RUNTIME_CONFIG=$<CONFIG>"
                "-DONNX_RUNTIME_TARGET_DIR=$<TARGET_FILE_DIR:${target_name}>"
                -P "${kCopyScriptPath}"
        VERBATIM)
endfunction()

function(vf_resolve_onnxruntime_root out_root_var version asset_url asset_sha256)
    set(kSourceOnnxRuntimeRoot "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${version}")
    set(kSourceGuardHeader
        "${kSourceOnnxRuntimeRoot}/include/onnxruntime/core/session/onnxruntime_cxx_api.h")
    set(kSourceGuardLib "${kSourceOnnxRuntimeRoot}/lib/win-x64/RelWithDebInfo/onnxruntime.lib")
    if (EXISTS "${kSourceGuardHeader}" AND EXISTS "${kSourceGuardLib}")
        message(STATUS "Using local ONNX Runtime from source tree: ${kSourceOnnxRuntimeRoot}")
        set(${out_root_var} "${kSourceOnnxRuntimeRoot}" PARENT_SCOPE)
        return()
    endif()

    if (asset_sha256 STREQUAL "REPLACE_WITH_SHA256_FROM_DEPLOY_ASSETS")
        message(FATAL_ERROR
            "ONNX Runtime local files are missing and asset SHA256 is not configured. "
            "Run scripts/deploy_assets.py and update kOnnxRuntimeAssetUrl/kOnnxRuntimeAssetSha256.")
    endif()

    set(kBinaryOnnxRuntimeRoot "${CMAKE_BINARY_DIR}/third_party/onnxruntime/${version}")
    set(kBinaryOnnxRuntimeZip
        "${CMAKE_BINARY_DIR}/third_party/onnxruntime/onnxruntime-${version}-win-x64.zip")
    set(kBinaryGuardHeader
        "${kBinaryOnnxRuntimeRoot}/include/onnxruntime/core/session/onnxruntime_cxx_api.h")
    set(kBinaryGuardLib "${kBinaryOnnxRuntimeRoot}/lib/win-x64/RelWithDebInfo/onnxruntime.lib")

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/third_party/onnxruntime")
    message(STATUS "Downloading ONNX Runtime asset: ${asset_url}")
    file(DOWNLOAD
        "${asset_url}"
        "${kBinaryOnnxRuntimeZip}"
        EXPECTED_HASH "SHA256=${asset_sha256}"
        SHOW_PROGRESS
        STATUS kDownloadStatus)
    list(GET kDownloadStatus 0 kDownloadCode)
    if (NOT kDownloadCode EQUAL 0)
        list(GET kDownloadStatus 1 kDownloadMessage)
        message(FATAL_ERROR "Failed to download ONNX Runtime asset: ${kDownloadMessage}")
    endif()

    if (EXISTS "${kBinaryGuardHeader}" AND EXISTS "${kBinaryGuardLib}")
        message(STATUS "ONNX Runtime archive already extracted; skipping extraction step.")
    else()
        file(REMOVE_RECURSE "${kBinaryOnnxRuntimeRoot}")
        file(MAKE_DIRECTORY "${kBinaryOnnxRuntimeRoot}")
        file(ARCHIVE_EXTRACT
            INPUT "${kBinaryOnnxRuntimeZip}"
            DESTINATION "${kBinaryOnnxRuntimeRoot}")
    endif()

    if (NOT EXISTS "${kBinaryGuardHeader}" OR NOT EXISTS "${kBinaryGuardLib}")
        message(FATAL_ERROR
            "ONNX Runtime archive extraction is incomplete. Missing required files under "
            "${kBinaryOnnxRuntimeRoot}.")
    endif()

    set(${out_root_var} "${kBinaryOnnxRuntimeRoot}" PARENT_SCOPE)
endfunction()

function(vf_configure_onnxruntime_dml target_name)
    set(kOnnxRuntimeVersion "1.25.0")
    set(kOnnxRuntimeAssetUrl
        "https://github.com/tadon1203/VisionFlow/releases/download/onnxruntime-assets-v${kOnnxRuntimeVersion}/onnxruntime-${kOnnxRuntimeVersion}-win-x64.zip")
    set(kOnnxRuntimeAssetSha256 "05723380ebde0d9ce99b2f7e1cb4837c5c40890b142663bafe175065592bd887")

    vf_resolve_onnxruntime_root(
        kOnnxRuntimeRoot
        "${kOnnxRuntimeVersion}"
        "${kOnnxRuntimeAssetUrl}"
        "${kOnnxRuntimeAssetSha256}")

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
