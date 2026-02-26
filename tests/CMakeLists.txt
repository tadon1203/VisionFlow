add_executable(VisionFlowUnitTests
    unit/capture/capture_error_test.cpp
    unit/capture/frame_sequencer_test.cpp
    unit/capture/inference_result_store_test.cpp
    unit/capture/onnx_dml_session_test.cpp
    unit/core/app_test.cpp
    unit/core/config_loader_test.cpp
    unit/core/error_domain_contract_test.cpp
    unit/input/makcu_controller_test.cpp
    unit/input/mouse_error_test.cpp
    ../src/capture/capture_error.cpp
    ../src/capture/frame_sequencer.cpp
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
    vf_configure_onnxruntime_dml(VisionFlowUnitTests)

    target_link_libraries(VisionFlowUnitTests PRIVATE
        windowsapp
    )
endif()

include(GoogleTest)
gtest_discover_tests(VisionFlowUnitTests)
