#ifndef PDM_SHARED_DIAGNOSTICS_LOGGER_H
#define PDM_SHARED_DIAGNOSTICS_LOGGER_H

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "logger_config.h"

namespace pdm::shared::diagnostic
{

class Logger final
{
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Returns false if the directory cannot be created or the first file cannot be opened.
    bool init(const LoggerConfig& config);

    // Backward-compatible initialization interface.
    bool init(const std::filesystem::path& directory,
              std::size_t maxFiles = 10,
              std::size_t maxFileSize = 5 * 1024 * 1024);

    void log(std::string_view message);
    void log(LogLevel level, std::string_view message);

    void debug(std::string_view message);
    void info(std::string_view message);
    void warning(std::string_view message);
    void error(std::string_view message);

    void setLevel(LogLevel level);
    void flush();
    void shutdown();

    ~Logger();

private:
    Logger() = default;

    void write(LogLevel level, std::string_view message);
    bool openNextFile();
    void enforceMaxFiles();

    static bool shouldLog(LogLevel messageLevel, LogLevel configuredLevel);
    static const char* levelName(LogLevel level);
    static std::string makeRecord(LogLevel level, std::string_view message);
    static std::string makeFileName();

    mutable std::mutex m_mutex{};
    LoggerConfig m_config{};
    std::ofstream m_currentFile{};
    std::filesystem::path m_currentFilePath{};
    std::size_t m_currentFileSize{};
    std::size_t m_fileSequence{};
    bool m_initialized{};
};

} // namespace pdm::shared::diagnostic

#endif // PDM_SHARED_DIAGNOSTICS_LOGGER_H
