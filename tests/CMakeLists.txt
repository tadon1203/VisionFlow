add_executable(VisionFlowUnitTests
    unit/capture/capture_error_test.cpp
    unit/capture/inference_result_store_test.cpp
    unit/capture/onnx_dml_session_test.cpp
    unit/core/app_test.cpp
    unit/core/config_loader_test.cpp
    unit/core/error_domain_contract_test.cpp
    unit/input/makcu_controller_test.cpp
    unit/input/mouse_error_test.cpp
    ../src/capture/capture_error.cpp
    ../src/capture/inference_result_store.cpp
    ../src/capture/onnx_dml_session.cpp
    ../src/core/app.cpp
    ../src/core/app_error.cpp
    ../src/core/config_error.cpp
    ../src/core/config_loader.cpp
    ../src/core/logger.cpp
    ../src/input/makcu_controller.cpp
    ../src/input/mouse_error.cpp
    ../src/input/winrt_serial_port.cpp
    ../src/input/winrt_device_scanner.cpp
)

target_include_directories(VisionFlowUnitTests PRIVATE
    ../include
    ../src
    ../third_party
)

target_link_libraries(VisionFlowUnitTests PRIVATE
    GTest::gtest_main
    GTest::gmock
    spdlog::spdlog
)

if (WIN32)
    target_link_libraries(VisionFlowUnitTests PRIVATE
        windowsapp
    )

    if (VF_HAS_ONNXRUNTIME_DML)
        target_link_directories(VisionFlowUnitTests PRIVATE
            "${kOnnxRuntimeLibDir}"
        )
        target_link_libraries(VisionFlowUnitTests PRIVATE
            onnxruntime
            onnxruntime_providers_dml
            onnxruntime_providers_shared
        )

        foreach(runtime_dll ${kOnnxRuntimeRuntimeDlls})
            add_custom_command(TARGET VisionFlowUnitTests POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${runtime_dll}"
                        "$<TARGET_FILE_DIR:VisionFlowUnitTests>"
                VERBATIM)
        endforeach()
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(VisionFlowUnitTests)
