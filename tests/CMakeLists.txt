add_executable(VisionFlowUnitTests
    unit/core/app_test.cpp
    unit/core/config_loader_test.cpp
    unit/core/error_domain_contract_test.cpp
    unit/input/makcu_controller_test.cpp
    unit/input/mouse_error_test.cpp
    ../src/core/app.cpp
    ../src/core/config_error.cpp
    ../src/core/config_loader.cpp
    ../src/core/logger.cpp
    ../src/input/makcu_controller.cpp
    ../src/input/mouse_error.cpp
    ../src/input/win32_serial_port.cpp
    ../src/input/win32_device_scanner.cpp
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
        setupapi
    )
endif()

include(GoogleTest)
gtest_discover_tests(VisionFlowUnitTests)
