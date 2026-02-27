add_executable(VisionFlowUnitTests
    unit/capture/capture_error_test.cpp
    unit/capture/capture_runtime_winrt_test.cpp
    unit/capture/capture_source_winrt_test.cpp
    unit/capture/frame_sequencer_test.cpp
    unit/capture/inference_result_store_test.cpp
    unit/capture/onnx_dml_session_test.cpp
    unit/core/app_test.cpp
    unit/core/config_loader_test.cpp
    unit/core/error_domain_contract_test.cpp
    unit/core/profiler_test.cpp
    unit/inference/inference_error_test.cpp
    unit/input/makcu_controller_test.cpp
    unit/input/mouse_error_test.cpp
)

target_link_libraries(VisionFlowUnitTests
    PRIVATE
        GTest::gtest_main
        GTest::gmock
        vf_public_headers
        vf_private_headers
        vf_core
        vf_input
        vf_capture
        vf_inference
)

include(GoogleTest)
gtest_discover_tests(
    VisionFlowUnitTests
    DISCOVERY_TIMEOUT 60
)
