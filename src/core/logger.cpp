#include "VisionFlow/core/logger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace vf {

std::shared_ptr<spdlog::logger> Logger::coreLogger;

namespace {

std::string makeLogFileName() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTm{};
    const bool conversion = (localtime_s(&localTm, &nowTime) == 0);
    if (!conversion) {
        const auto secondsSinceEpoch =
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        return std::to_string(secondsSinceEpoch) + ".txt";
    }

    std::ostringstream oss;
    oss << std::put_time(&localTm, "%Y-%m-%d_%H-%M-%S") << ".txt";
    return oss.str();
}

} // namespace

void Logger::init() {
    static std::mutex initMutex;
    std::scoped_lock lock(initMutex);

    if (coreLogger) {
        return;
    }

    std::vector<spdlog::sink_ptr> sinks;

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(consoleSink);

    const std::filesystem::path logDir{"logs"};
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        std::cerr << "[VisionFlow Logger] failed to create log directory: " << logDir.string()
                  << " (" << ec.message() << ")\n";
    } else {
        const auto logPath = logDir / makeLogFileName();
        try {
            auto fileSink =
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            sinks.push_back(fileSink);
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "[VisionFlow Logger] failed to create file sink: " << ex.what() << "\n";
        }
    }

    coreLogger = std::make_shared<spdlog::logger>("VISIONFLOW", sinks.begin(), sinks.end());

    coreLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

#ifdef NDEBUG
    coreLogger->set_level(spdlog::level::info);
#else
    coreLogger->set_level(spdlog::level::debug);
#endif

    coreLogger->flush_on(spdlog::level::warn);
}

std::shared_ptr<spdlog::logger>& Logger::core() {
    if (!coreLogger) {
        init();
    }
    return coreLogger;
}

} // namespace vf
