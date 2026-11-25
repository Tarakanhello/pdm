#ifndef PDM_SHARED_DIAGNOSTICS_LOGGER_CONFIG_H
#define PDM_SHARED_DIAGNOSTICS_LOGGER_CONFIG_H

#include <cstddef>
#include <filesystem>

#include "log_level.h"

namespace pdm::shared::diagnostic
{

struct LoggerConfig
{
    std::filesystem::path directory{ "logs" };
    std::size_t maxFileSize{ 5 * 1024 * 1024 };
    std::size_t maxFiles{ 10 };
    LogLevel level{ LogLevel::Debug };
};

} // namespace pdm::shared::diagnostic

#endif // PDM_SHARED_DIAGNOSTICS_LOGGER_CONFIG_H
