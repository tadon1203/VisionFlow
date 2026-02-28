#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VisionFlow/input/i_device_scanner.hpp"
#include "VisionFlow/input/i_serial_port.hpp"
#include "VisionFlow/input/makcu_mouse_controller.hpp"
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
    MOCK_METHOD(void, setDataReceivedHandler, (DataReceivedHandler handler), (override));
    MOCK_METHOD((std::expected<std::size_t, std::error_code>), readSome,
                (std::span<std::uint8_t> buffer), (override));
};

class MockDeviceScanner : public IDeviceScanner {
  public:
    MOCK_METHOD((std::expected<std::string, std::error_code>), findPortByHardwareId,
                (const std::string& hardwareId), (const, override));
};

class FakeSerialPort : public ISerialPort {
  public:
    [[nodiscard]] std::expected<void, std::error_code> open(const std::string& /*portName*/,
                                                            std::uint32_t /*baudRate*/) override {
        opened = true;
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> close() override {
        opened = false;
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code>
    configure(std::uint32_t /*baudRate*/) override {
        return {};
    }

    [[nodiscard]] std::expected<void, std::error_code> flush() override { return {}; }

    [[nodiscard]] std::expected<void, std::error_code>
    write(std::span<const std::uint8_t> payload) override {
        if (!opened) {
            return std::unexpected(makeErrorCode(MouseError::PortOpenFailed));
        }

        const std::string command(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (command.starts_with("km.move(")) {
            {
                std::scoped_lock lock(moveMutex);
                moveCommands.push_back(command);
            }
            moveCv.notify_all();

            DataReceivedHandler handlerCopy;
            {
                std::scoped_lock lock(handlerMutex);
                handlerCopy = handler;
            }
            if (handlerCopy) {
                static constexpr std::array<std::uint8_t, 6> kAckData{
                    {'>', '>', '>', ' ', '\r', '\n'}};
                handlerCopy(kAckData);
            }
        }

        return {};
    }

    void setDataReceivedHandler(DataReceivedHandler callback) override {
        std::scoped_lock lock(handlerMutex);
        handler = std::move(callback);
    }

    [[nodiscard]] std::expected<std::size_t, std::error_code>
    readSome(std::span<std::uint8_t> /*buffer*/) override {
        return static_cast<std::size_t>(0);
    }

    bool waitForMoveCount(std::size_t expectedCount, std::chrono::milliseconds timeout) {
        std::unique_lock lock(moveMutex);
        return moveCv.wait_for(lock, timeout, [&] { return moveCommands.size() >= expectedCount; });
    }

    std::vector<std::string> snapshotMoveCommands() {
        std::scoped_lock lock(moveMutex);
        return moveCommands;
    }

  private:
    bool opened = false;

    std::mutex handlerMutex;
    DataReceivedHandler handler;

    std::mutex moveMutex;
    std::condition_variable moveCv;
    std::vector<std::string> moveCommands;
};

class StaticDeviceScanner : public IDeviceScanner {
  public:
    [[nodiscard]] std::expected<std::string, std::error_code>
    findPortByHardwareId(const std::string& /*hardwareId*/) const override {
        return std::string("COM9");
    }
};

TEST(MakcuControllerTest, ConnectFailsWhenPortScanFails) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .WillOnce(testing::Return(std::unexpected(makeErrorCode(MouseError::PortNotFound))));

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
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

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
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

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    const auto result = controller.connect();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::ConfigureDcbFailed));
}

TEST(MakcuControllerTest, MoveFailsWhenControllerIsNotReady) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    const auto result = controller.move(1.0F, 1.0F);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), makeErrorCode(MouseError::NotConnected));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MakcuControllerTest, ReconnectsAfterMoveWriteFailure) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* serialPtr = serial.get();
    auto* scannerPtr = scanner.get();
    ISerialPort::DataReceivedHandler receivedHandler;

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
    EXPECT_CALL(*serialPtr, setDataReceivedHandler(testing::_))
        .Times(testing::AtLeast(2))
        .WillRepeatedly([&receivedHandler](ISerialPort::DataReceivedHandler handler) {
            receivedHandler = std::move(handler);
        });

    int writeCallCount = 0;
    int moveWriteCount = 0;
    EXPECT_CALL(*serialPtr, write(testing::_))
        .Times(testing::AtLeast(5))
        .WillRepeatedly([&writeCallCount, &moveWriteCount, &receivedHandler](
                            std::span<const std::uint8_t>) -> std::expected<void, std::error_code> {
            ++writeCallCount;
            if (writeCallCount > 2) {
                ++moveWriteCount;
            }
            if (moveWriteCount == 3) {
                return std::unexpected(makeErrorCode(MouseError::WriteFailed));
            }

            if (moveWriteCount > 0 && receivedHandler) {
                static constexpr std::array<std::uint8_t, 6> kAckData{
                    {'>', '>', '>', ' ', '\r', '\n'}};
                receivedHandler(kAckData);
            }
            return std::expected<void, std::error_code>{};
        });

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());
    ASSERT_TRUE(controller.move(1.0F, 1.0F).has_value());

    bool moveFailureObserved = false;
    for (int i = 0; i < 100; ++i) {
        const auto moveResult = controller.move(1.0F, 1.0F);
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

TEST(MakcuControllerTest, MoveFailsWhenAckTimeoutOccurs) {
    auto serial = std::make_unique<testing::StrictMock<MockSerialPort>>();
    auto scanner = std::make_unique<testing::StrictMock<MockDeviceScanner>>();
    auto* serialPtr = serial.get();
    auto* scannerPtr = scanner.get();

    EXPECT_CALL(*scannerPtr, findPortByHardwareId(testing::_))
        .WillOnce(testing::Return(std::string("COM9")));
    EXPECT_CALL(*serialPtr, open("COM9", 115200U))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, configure(4000000U))
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));
    EXPECT_CALL(*serialPtr, setDataReceivedHandler(testing::_)).Times(testing::AtLeast(1));
    EXPECT_CALL(*serialPtr, write(testing::_))
        .Times(testing::AtLeast(3))
        .WillRepeatedly(
            [](std::span<const std::uint8_t>) { return std::expected<void, std::error_code>{}; });
    EXPECT_CALL(*serialPtr, close())
        .WillOnce(testing::Return(std::expected<void, std::error_code>{}));

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());
    ASSERT_TRUE(controller.move(5.0F, 7.0F).has_value());

    bool notConnected = false;
    for (int i = 0; i < 200; ++i) {
        const auto moveResult = controller.move(1.0F, 1.0F);
        if (!moveResult) {
            EXPECT_EQ(moveResult.error(), makeErrorCode(MouseError::NotConnected));
            notConnected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(notConnected);
}

TEST(MakcuControllerTest, AccumulatesFractionalMoveInputsIntoIntegerSend) {
    auto serial = std::make_unique<FakeSerialPort>();
    auto* serialPtr = serial.get();
    auto scanner = std::make_unique<StaticDeviceScanner>();

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());

    ASSERT_TRUE(controller.move(0.4F, 0.4F).has_value());
    ASSERT_TRUE(controller.move(0.4F, 0.4F).has_value());
    ASSERT_TRUE(controller.move(0.4F, 0.4F).has_value());

    ASSERT_TRUE(serialPtr->waitForMoveCount(1, std::chrono::milliseconds(100)));
    const auto commands = serialPtr->snapshotMoveCommands();
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front(), "km.move(1,1)\r\n");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(MakcuControllerTest, ClampsMoveCommandAndCarriesOverflowAcrossSends) {
    auto serial = std::make_unique<FakeSerialPort>();
    auto* serialPtr = serial.get();
    auto scanner = std::make_unique<StaticDeviceScanner>();

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());
    ASSERT_TRUE(controller.move(300.0F, 0.0F).has_value());
    ASSERT_TRUE(serialPtr->waitForMoveCount(3, std::chrono::milliseconds(200)));

    const auto commands = serialPtr->snapshotMoveCommands();
    ASSERT_GE(commands.size(), 3U);
    EXPECT_EQ(commands.at(0), "km.move(127,0)\r\n");
    EXPECT_EQ(commands.at(1), "km.move(127,0)\r\n");
    EXPECT_EQ(commands.at(2), "km.move(46,0)\r\n");

    int summedDx = 0;
    for (const std::string& command : commands) {
        const std::size_t begin = command.find('(');
        const std::size_t comma = command.find(',');
        ASSERT_NE(begin, std::string::npos);
        ASSERT_NE(comma, std::string::npos);

        const int dx = std::stoi(command.substr(begin + 1, comma - begin - 1));
        EXPECT_LE(std::abs(dx), 127);
        summedDx += dx;
    }
    EXPECT_EQ(summedDx, 300);
}

TEST(MakcuControllerTest, DropsRemainderAfterTtlGap) {
    auto serial = std::make_unique<FakeSerialPort>();
    auto* serialPtr = serial.get();
    auto scanner = std::make_unique<StaticDeviceScanner>();

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());

    ASSERT_TRUE(controller.move(0.2F, 0.0F).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    ASSERT_TRUE(controller.move(0.9F, 0.0F).has_value());

    EXPECT_FALSE(serialPtr->waitForMoveCount(1, std::chrono::milliseconds(80)));
}

TEST(MakcuControllerTest, KeepsRemainderWithinTtl) {
    auto serial = std::make_unique<FakeSerialPort>();
    auto* serialPtr = serial.get();
    auto scanner = std::make_unique<StaticDeviceScanner>();

    MakcuMouseController controller(std::move(serial), std::move(scanner), MakcuConfig{});
    ASSERT_TRUE(controller.connect().has_value());

    ASSERT_TRUE(controller.move(0.2F, 0.0F).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(controller.move(0.9F, 0.0F).has_value());

    ASSERT_TRUE(serialPtr->waitForMoveCount(1, std::chrono::milliseconds(100)));
    const auto commands = serialPtr->snapshotMoveCommands();
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front(), "km.move(1,0)\r\n");
}

} // namespace
} // namespace vf
