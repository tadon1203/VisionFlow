cmake_minimum_required(VERSION 4.0.0)
project(VisionFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(kCompileCommandsMirrorDir "${CMAKE_SOURCE_DIR}/build")
file(MAKE_DIRECTORY "${kCompileCommandsMirrorDir}")

include(FetchContent)
include(CTest)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

if (BUILD_TESTING)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_library(vf_public_headers INTERFACE)
target_include_directories(vf_public_headers
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
)

add_library(vf_private_headers INTERFACE)
target_include_directories(vf_private_headers
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>"
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party>"
)

if (WIN32)
    add_library(vf_win32_definitions INTERFACE)
    target_compile_definitions(vf_win32_definitions
        INTERFACE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
endif()

function(vf_apply_target_defaults targetName)
    target_link_libraries(${targetName}
        PUBLIC
            vf_public_headers
        PRIVATE
            vf_private_headers
    )

    if (TARGET vf_win32_definitions)
        target_link_libraries(${targetName}
            PRIVATE
                vf_win32_definitions
        )
    endif()
endfunction()

add_library(vf_core STATIC
    src/core/app.cpp
    src/core/app_error.cpp
    src/core/config_error.cpp
    src/core/config_loader.cpp
    src/core/logger.cpp
)
vf_apply_target_defaults(vf_core)
target_link_libraries(vf_core
    PUBLIC
        spdlog::spdlog
)

add_library(vf_platform_winrt STATIC
    src/platform/winrt/platform_context_winrt.cpp
)
vf_apply_target_defaults(vf_platform_winrt)
target_link_libraries(vf_platform_winrt
    PUBLIC
        vf_core
)

add_library(vf_input STATIC
    src/input/mouse_error.cpp
    src/input/mouse_controller_factory.cpp
    src/input/makcu_mouse_controller.cpp
    src/input/makcu/makcu_ack_gate.cpp
    src/input/makcu/makcu_command_queue.cpp
    src/input/makcu/makcu_controller_state.cpp
    src/input/platform/serial_port_winrt.cpp
    src/input/platform/device_scanner_winrt.cpp
)
vf_apply_target_defaults(vf_input)
target_link_libraries(vf_input
    PUBLIC
        vf_core
)

add_library(vf_capture STATIC
    src/capture/capture_error.cpp
    src/capture/runtime/capture_runtime_winrt.cpp
    src/capture/sources/winrt/capture_source_winrt.cpp
    src/capture/sources/winrt/winrt_capture_session.cpp
)
vf_apply_target_defaults(vf_capture)
target_link_libraries(vf_capture
    PUBLIC
        vf_core
)

add_library(vf_inference STATIC
    src/inference/api/winrt_inference_factory.cpp
    src/inference/engine/debug_inference_processor.cpp
    src/inference/engine/dml_inference_worker.cpp
    src/inference/engine/inference_result_store.cpp
    src/inference/engine/onnx_dml_inference_processor.cpp
    src/inference/platform/dml/compute_pipeline.cpp
    src/inference/platform/dml/d3d11_d3d12_interop.cpp
    src/inference/platform/dml/dml_image_processor.cpp
    src/inference/platform/dml/onnx_dml_session_common.cpp
    src/inference/platform/dml/onnx_dml_session_win32_dml.cpp
    src/inference/platform/dml/onnx_dml_session_stub.cpp
)
vf_apply_target_defaults(vf_inference)
target_link_libraries(vf_inference
    PUBLIC
        vf_core
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/core/app_factory.cpp
)
vf_apply_target_defaults(${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        vf_core
        vf_input
        vf_capture
        vf_inference
        vf_platform_winrt
)

add_custom_target(syncCompileCommands ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${kCompileCommandsMirrorDir}/compile_commands.json"
    COMMENT "Sync compile_commands.json to ${kCompileCommandsMirrorDir}"
    VERBATIM)

if (WIN32)
    include("${CMAKE_SOURCE_DIR}/cmake/OnnxRuntimeDml.cmake")

    vf_configure_onnxruntime_dml(vf_inference)
    vf_configure_onnxruntime_dml(${PROJECT_NAME})

    target_link_libraries(vf_capture
        PUBLIC
            d3d11
            dxgi
            windowsapp
    )

    target_link_libraries(vf_inference
        PUBLIC
            d3d11
            d3d12
            d3dcompiler
            dxgi
            windowsapp
    )

    target_link_libraries(vf_input
        PUBLIC
            windowsapp
    )

    target_link_libraries(vf_platform_winrt
        PUBLIC
            windowsapp
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
