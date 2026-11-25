#ifndef PDM_SHARED_DIAGNOSTICS_LOGGER_H
#define PDM_SHARED_DIAGNOSTICS_LOGGER_H

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "log_level.h"
#include "logger_config.h"

namespace pdm::shared::diagnostic
{

class Logger final
{
private:
    Logger() = default;

    mutable std::mutex m_mutex{};
    LoggerConfig m_config{};
    std::ofstream m_currentFile{};
    std::filesystem::path m_currentFilePath{};
    std::size_t m_currentFileSize{}; // текущий размер открытого файла
    std::size_t m_fileSequence{};
    bool m_initialized{};


    void write(LogLevel level, std::string_view message);
    bool openNextFile();
    /** Удаляет самые старые файлы, если их количество превышает m_maxFiles. */
    void enforceMaxFiles();

    static bool shouldLog(LogLevel messageLvl, LogLevel configuredLvl);
    static const char* levelName(LogLevel lvl);
    static std::string makeRecord(LogLevel lvl, std::string_view message);
    std::string makeFileName();

public:
    static Logger& instance();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(const Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
    ~Logger();

    bool init(const LoggerConfig& config);
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
};

} // end namespace pdm::shared::diagnostic

#endif // DIAGNOSNOSTIC_SINK_H
