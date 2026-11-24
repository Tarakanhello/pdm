#ifndef DIAGNOSNOSTIC_SINK_H
#define DIAGNOSNOSTIC_SINK_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

namespace pdm::shared::diagnostic
{

class Logger final
{
private:
    std::filesystem::path m_directory{};
    std::size_t m_maxFiles{};
    std::size_t m_maxFileSize{};
    std::ofstream m_currentFile{};
    std::size_t m_currentFileSize{}; // текущий размер открытого файла

    void openNextFile()
    {
        if(m_currentFile.is_open())
        {
            m_currentFile.close();
        }

        const auto now{ std::chrono::system_clock::now() };

        const auto localTime{ std::chrono::current_zone()->to_local(now) };

        std::string filename{ std::format("logs_{:%Y-%m-%d_%H-%M-%S}.log", localTime) };

        const auto filePath{ m_directory / filename };

        m_currentFile.open(filePath, std::ios::out | std::ios::app);
        if(!m_currentFile.is_open())
        {
            std::cerr << std::format("[pdm::shared::diagnostic::Logger] Failed to open log file: {} ({})\n",
                                     filePath.string(),
                                     std::strerror(errno));
            m_currentFileSize = 0;
            return;
        }

        m_currentFile.seekp(0, std::ios::end);
        m_currentFileSize = static_cast<std::size_t>(m_currentFile.tellp());

        enforceMaxFiles();
    }

    /** Удаляет самые старые файлы, если их количество превышает m_maxFiles. */
    void enforceMaxFiles()
    {
        std::vector<std::filesystem::path> logFiles{};
        std::error_code ec;

        // Собираем все обычные файлы с расширением .log в заданной директории
        for(const auto& entry : std::filesystem::directory_iterator(m_directory, ec))
        {
            if(entry.is_regular_file() && entry.path().extension() == ".log")
            {
                logFiles.push_back(entry.path());
            }
        }

        if(logFiles.size() <= m_maxFiles)
        {
            return;
        }

        std::ranges::sort(logFiles, [](const std::filesystem::path& a, const std::filesystem::path& b){
            return std::filesystem::last_write_time(a) < std::filesystem::last_write_time(b);
        });

        for(std::size_t i{ 0 }; i < logFiles.size() - m_maxFiles; ++i)
        {
            std::filesystem::remove(logFiles[i], ec);
            if(ec)
            {
                std::cerr << "[pdm::shared::diagnostic::Logger::enforceMaxFiles()] Failed to remove old log file: "
                          << logFiles[i] << " (" << ec.message() << ")" << std::endl;
            }
        }
    }

    Logger() = default;
    ~Logger()
    {
        if(m_currentFile.is_open())
        {
            m_currentFile.close();
        }
    }

public:
    static Logger& instance()
    {
        static Logger logger;
        return logger;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void init(const std::filesystem::path& directory,
              std::size_t maxFiles = 10,
              std::size_t maxFileSize = 5 * 1024 * 1024)
    {
        m_directory = directory;
        m_maxFiles = maxFiles;
        m_maxFileSize = maxFileSize;

        std::error_code ec;
        std::filesystem::create_directory(m_directory, ec);
        if(ec)
        {
            std::cerr << "[pdm::shared::diagnostic::Logger::init()]: Failed to create directory: ]"
                      << m_directory << ": " << ec.message() << std::endl;
        }
    }

    void log(const std::string& str)
    {
        if(!m_currentFile.is_open() || (m_currentFileSize + str.size() > m_maxFileSize))
        {
            openNextFile();
        }

        if(m_currentFile.is_open())
        {
            m_currentFile << str << std::flush;
            m_currentFileSize += str.size();
        }
        else
        {
            std::cerr << str << std::flush;
        }
    }

};

} // end namespace pdm::shared::diagnostic

#endif // DIAGNOSNOSTIC_SINK_H
