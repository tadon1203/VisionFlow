#include "VisionFlow/input/makcu_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_serial_port.hpp"
#include "VisionFlow/input/mouse_error.hpp"

namespace vf {
namespace {

class MockSerialPort : public ISerialPort {
  public:
    MOCK_METHOD((std::expected<void, std::error_code>), open,
                (const std::string& portName, std::uint32_t baudRate), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), close, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), configure, (std::uint32_t baudRate),
                (override));
    MOCK_METHOD((std::expected<void, std::error_code>), flush, (), (override));
    MOCK_METHOD((std::expected<void, std::error_code>), write,
                (std::span<const std::uint8_t> payload), (override));
    MOCK_METHOD((std::expected<std::size_t, std::error_code>), readSome,
                (std::span<std::uint8_t> buffer), (override));
};

class MockDeviceScanner : public IDeviceScanner {
  public:
    MOCK_METHOD((std::expected<std::string, std::error_code>), findPortByHardwareId,
                (const std::string& hardwareId), (const, override));
};

TEST(MakcuControllerTest, ConnectFailsWhenPortScanFails) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .WillOnce(testing::Return(std::unexpected(makeErrorCode(MouseError::PortNotFound))));

    MakcuController controller(std::move(serial), std::move(scanner));
    const auto result = controller.connect();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::PortNotFound));
}

TEST(MakcuControllerTest, ConnectFailsWhenOpenFails) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* serialPtr = serial.get();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .WillOnce(testing::Return(std::string("COM9")));
    EXPECT_CALL(*serialPtr, open("COM9", 115200U))
        .WillOnce(testing::Return(std::unexpected(makeErrorCode(MouseError::PortOpenFailed))));

    MakcuController controller(std::move(serial), std::move(scanner));
    const auto result = controller.connect();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::PortOpenFailed));
}

TEST(MakcuControllerTest, ConnectClosesPortWhenHandshakeFails) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* serialPtr = serial.get();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .WillOnce(testing::Return(std::string("COM9")));
    EXPECT_CALL(*serialPtr, open("COM9", 115200U))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, write(testing::_))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, configure(4000000U))
        .WillOnce(testing::Return(std::unexpected(makeErrorCode(MouseError::ConfigureDcbFailed))));
    EXPECT_CALL(*serialPtr, close())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    MakcuController controller(std::move(serial), std::move(scanner));
    const auto result = controller.connect();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::ConfigureDcbFailed));
}

TEST(MakcuControllerTest, MoveFailsWhenControllerIsNotReady) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();

    MakcuController controller(std::move(serial), std::move(scanner));
    const auto result = controller.move(1, 1);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::NotConnected));
}

TEST(MakcuControllerTest, ReconnectsAfterMoveWriteFailure) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* serialPtr = serial.get();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Return(std::string("COM9")));
    EXPECT_CALL(*serialPtr, open("COM9", 115200U))
        .Times(2)
        .WillRepeatedly(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, configure(4000000U))
        .Times(2)
        .WillRepeatedly(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, close())
        .Times(2)
        .WillRepeatedly(testing::Return(std::expected<void, std::error_code>{}));

    int writeCallCount = 0;
    EXPECT_CALL(*serialPtr, write(testing::_))
        .Times(testing::AtLeast(5))
        .WillRepeatedly([&writeCallCount](
                            std::span<const std::uint8_t>) -> std::expected<void, std::error_code> {
            ++writeCallCount;
            if (writeCallCount == 3) {
                return std::unexpected(makeErrorCode(MouseError::WriteFailed));
            }
            return std::expected<void, std::error_code>{};
        });

    MakcuController controller(std::move(serial), std::move(scanner));
    ASSERT_TRUE(controller.connect().has_value());
    ASSERT_TRUE(controller.move(1, 1).has_value());

    bool moveFailureObserved = false;
    for (int i = 0; i < 100; ++i) {
        const auto moveResult = controller.move(1, 1);
        if (!moveResult) {
            EXPECT_EQ(moveResult.error(), makeErrorCode(MouseError::NotConnected));
            moveFailureObserved = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(moveFailureObserved);

    const auto reconnectResult = controller.connect();
    EXPECT_TRUE(reconnectResult.has_value());
}

} // namespace
} // namespace vf
