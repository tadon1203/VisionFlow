#pragma once

#include <memory>

#include "spdlog/logger.h"

#ifndef SPDLOG_ACTIVE_LEVEL
#ifdef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#endif
#endif

#include <spdlog/spdlog.h>

namespace vf {

class Logger {
  public:
    static void init();
    static std::shared_ptr<spdlog::logger>& core();

  private:
    static std::shared_ptr<spdlog::logger> coreLogger;
};

} // namespace vf

#define VF_TRACE(...) SPDLOG_LOGGER_TRACE(::vf::Logger::core(), __VA_ARGS__)
#define VF_DEBUG(...) SPDLOG_LOGGER_DEBUG(::vf::Logger::core(), __VA_ARGS__)
#define VF_INFO(...) SPDLOG_LOGGER_INFO(::vf::Logger::core(), __VA_ARGS__)
#define VF_WARN(...) SPDLOG_LOGGER_WARN(::vf::Logger::core(), __VA_ARGS__)
#define VF_ERROR(...) SPDLOG_LOGGER_ERROR(::vf::Logger::core(), __VA_ARGS__)
#define VF_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::vf::Logger::core(), __VA_ARGS__)
