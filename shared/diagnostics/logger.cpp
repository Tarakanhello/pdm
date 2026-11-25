#include "logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace pdm::shared::diagnostic
{

namespace
{

std::tm toLocalTime(std::time_t time)
{
    std::tm result{};

#if defined(_WIN32) && defined(_MSC_VER)
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif

    return result;
}

void writeToStderr(std::string_view message)
{
    if(message.empty())
    {
        return;
    }

    std::cerr.write(message.data(), static_cast<std::streamsize>(message.size()));
    std::cerr.flush();
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    shutdown();
}

bool Logger::init(const LoggerConfig& config)
{
    std::scoped_lock lock{ m_mutex };

    if(m_currentFile.is_open())
    {
        m_currentFile.flush();
        m_currentFile.close();
    }

    m_initialized = false;
    m_currentFileSize = 0;
    m_currentFilePath.clear();

    if(config.directory.empty())
    {
        writeToStderr("[Logger] Initialization failed: log directory is empty.\n");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(config.directory, ec);
    if(ec || !std::filesystem::is_directory(config.directory, ec) || ec)
    {
        writeToStderr("[Logger] Initialization failed: cannot create or access log directory: ");
        writeToStderr(config.directory.string());
        writeToStderr(ec ? " (" + ec.message() + ")\n" : "\n");
        return false;
    }

    m_config = config;
    m_initialized = true;

    if(!openNextFile())
    {
        m_initialized = false;
        return false;
    }

    return true;
}

bool Logger::init(const std::filesystem::path& directory,
                  std::size_t maxFiles,
                  std::size_t maxFileSize)
{
    LoggerConfig config{};
    config.directory = directory;
    config.maxFiles = maxFiles;
    config.maxFileSize = maxFileSize;
    return init(config);
}

void Logger::log(std::string_view message)
{
    write(LogLevel::Info, message);
}

void Logger::log(LogLevel level, std::string_view message)
{
    write(level, message);
}

void Logger::debug(std::string_view message)
{
    write(LogLevel::Debug, message);
}

void Logger::info(std::string_view message)
{
    write(LogLevel::Info, message);
}

void Logger::warning(std::string_view message)
{
    write(LogLevel::Warning, message);
}

void Logger::error(std::string_view message)
{
    write(LogLevel::Error, message);
}

void Logger::setLevel(LogLevel level)
{
    std::scoped_lock lock{ m_mutex };
    m_config.level = level;
}

void Logger::flush()
{
    std::scoped_lock lock{ m_mutex };

    if(m_currentFile.is_open())
    {
        m_currentFile.flush();
        if(!m_currentFile)
        {
            writeToStderr("[Logger] Failed to flush the current log file.\n");
            m_currentFile.close();
        }
    }
}

void Logger::shutdown()
{
    std::scoped_lock lock{ m_mutex };

    if(m_currentFile.is_open())
    {
        m_currentFile.flush();
        if(!m_currentFile)
        {
            writeToStderr("[Logger] Failed to flush the current log file during shutdown.\n");
        }
        m_currentFile.close();
    }

    m_currentFilePath.clear();
    m_currentFileSize = 0;
    m_initialized = false;
}

void Logger::write(LogLevel level, std::string_view message)
{
    std::scoped_lock lock{ m_mutex };

    if(!shouldLog(level, m_config.level))
    {
        return;
    }

    std::string record{ makeRecord(level, message) };

    if(!m_initialized)
    {
        writeToStderr(record);
        return;
    }

    if(!m_currentFile.is_open() && !openNextFile())
    {
        writeToStderr(record);
        return;
    }

    if(m_config.maxFileSize > 0 &&
       m_currentFileSize > 0 &&
       (m_currentFileSize >= m_config.maxFileSize ||
        record.size() > m_config.maxFileSize - m_currentFileSize))
    {
        if(!openNextFile())
        {
            writeToStderr(record);
            return;
        }
    }

    m_currentFile.write(record.data(), static_cast<std::streamsize>(record.size()));
    if(!m_currentFile)
    {
        writeToStderr("[Logger] Failed to write to the current log file; message redirected to stderr.\n");
        writeToStderr(record);
        m_currentFile.close();
        m_currentFilePath.clear();
        m_currentFileSize = 0;
        return;
    }

    m_currentFileSize += record.size();

    if(level == LogLevel::Warning || level == LogLevel::Error)
    {
        m_currentFile.flush();
        if(!m_currentFile)
        {
            writeToStderr("[Logger] Failed to flush a warning or error message.\n");
            m_currentFile.close();
            m_currentFilePath.clear();
            m_currentFileSize = 0;
        }
    }
}

bool Logger::openNextFile()
{
    if(m_currentFile.is_open())
    {
        m_currentFile.flush();
        m_currentFile.close();
    }

    const std::filesystem::path filePath{ m_config.directory / makeFileName() };
    m_currentFile.clear();
    m_currentFile.open(filePath, std::ios::out | std::ios::app | std::ios::binary);

    if(!m_currentFile.is_open())
    {
        m_currentFilePath.clear();
        m_currentFileSize = 0;
        writeToStderr("[Logger] Failed to open log file: ");
        writeToStderr(filePath.string());
        writeToStderr("\n");
        return false;
    }

    std::error_code ec;
    const auto existingSize{ std::filesystem::file_size(filePath, ec) };
    m_currentFileSize = ec ? 0 : static_cast<std::size_t>(existingSize);
    m_currentFilePath = filePath;

    enforceMaxFiles();
    return true;
}

void Logger::enforceMaxFiles()
{
    if(m_config.maxFiles == 0)
    {
        return;
    }

    using LogFile = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
    std::vector<LogFile> logFiles{};

    std::error_code ec;
    std::filesystem::directory_iterator iterator{ m_config.directory, ec };
    const std::filesystem::directory_iterator end{};

    for(; !ec && iterator != end; iterator.increment(ec))
    {
        const auto& path{ iterator->path() };
        const std::string filename{ path.filename().string() };

        if(path.extension() != ".log" || !filename.starts_with("pdm_"))
        {
            continue;
        }

        std::error_code statusError;
        if(!iterator->is_regular_file(statusError) || statusError)
        {
            continue;
        }

        std::error_code timeError;
        const auto writeTime{ std::filesystem::last_write_time(path, timeError) };
        logFiles.emplace_back(timeError ? std::filesystem::file_time_type::min() : writeTime, path);
    }

    std::sort(logFiles.begin(), logFiles.end(),
              [](const LogFile& left, const LogFile& right) {
                  return left.first < right.first;
              });

    std::size_t filesToRemove{ logFiles.size() > m_config.maxFiles
                                   ? logFiles.size() - m_config.maxFiles
                                   : 0 };

    for(const auto& [writeTime, path] : logFiles)
    {
        (void)writeTime;

        if(filesToRemove == 0)
        {
            break;
        }

        if(path == m_currentFilePath)
        {
            continue;
        }

        std::error_code removeError;
        if(std::filesystem::remove(path, removeError) && !removeError)
        {
            --filesToRemove;
        }
        else if(removeError)
        {
            writeToStderr("[Logger] Failed to remove an old log file: ");
            writeToStderr(path.string());
            writeToStderr(" (" + removeError.message() + ")\n");
        }
    }
}

bool Logger::shouldLog(LogLevel messageLevel, LogLevel configuredLevel)
{
    return static_cast<int>(messageLevel) >= static_cast<int>(configuredLevel);
}

const char* Logger::levelName(LogLevel level)
{
    switch(level)
    {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
    }

    return "UNKNOWN";
}

std::string Logger::makeRecord(LogLevel level, std::string_view message)
{
    const auto now{ std::chrono::system_clock::now() };
    const auto time{ std::chrono::system_clock::to_time_t(now) };
    const auto localTime{ toLocalTime(time) };
    const auto milliseconds{
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000
    };

    std::ostringstream stream{};
    stream << '[' << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << milliseconds
           << "] [" << levelName(level) << "] [thread " << std::this_thread::get_id() << "] ";

    if(!message.empty())
    {
        stream.write(message.data(), static_cast<std::streamsize>(message.size()));
    }

    if(message.empty() || message.back() != '\n')
    {
        stream.put('\n');
    }

    return stream.str();
}

std::string Logger::makeFileName()
{
    const auto now{ std::chrono::system_clock::now() };
    const auto time{ std::chrono::system_clock::to_time_t(now) };
    const auto localTime{ toLocalTime(time) };
    const auto milliseconds{
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000
    };

    std::ostringstream stream{};
    stream << "pdm_" << std::put_time(&localTime, "%Y%m%d_%H%M%S")
           << '_' << std::setw(3) << std::setfill('0') << milliseconds
           << '_' << std::setw(4) << std::setfill('0') << m_fileSequence++
           << ".log";

    return stream.str();
}

} // namespace pdm::shared::diagnostic
