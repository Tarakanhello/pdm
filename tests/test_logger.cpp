#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "diagnostic/logger.h"

namespace
{

using pdm::shared::diagnostic::LogLevel;
using pdm::shared::diagnostic::Logger;
using pdm::shared::diagnostic::LoggerConfig;

std::filesystem::path makeTestDirectory()
{
    const auto uniquePart{
        std::chrono::steady_clock::now().time_since_epoch().count()
    };

    return std::filesystem::temp_directory_path() /
           ("pdm_logger_test_" + std::to_string(uniquePart));
}

std::string readLogFiles(const std::filesystem::path& directory)
{
    std::string result{};

    for(const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if(entry.path().extension() != ".log")
        {
            continue;
        }

        std::ifstream file{ entry.path(), std::ios::binary };
        result.append(std::istreambuf_iterator<char>{ file },
                      std::istreambuf_iterator<char>{});
    }

    return result;
}

std::size_t countLogFiles(const std::filesystem::path& directory)
{
    std::size_t count{};

    for(const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if(entry.path().extension() == ".log" &&
            entry.path().filename().string().starts_with("pdm_"))
        {
            ++count;
        }
    }

    return count;
}

} // namespace

TEST_CASE("Logger writes structured records and creates nested directories", "[logger]")
{
    auto& logger{ Logger::instance() };
    const auto root{ makeTestDirectory() };
    const auto logDirectory{ root / "nested" / "logs" };

    LoggerConfig config{};
    config.directory = logDirectory;

    REQUIRE(logger.init(config));
    logger.info("startup message");
    logger.error("recognition failed");
    logger.flush();
    logger.shutdown();

    REQUIRE(std::filesystem::exists(logDirectory));

    const std::string contents{ readLogFiles(logDirectory) };
    CHECK(contents.find("[INFO]") != std::string::npos);
    CHECK(contents.find("[ERROR]") != std::string::npos);
    CHECK(contents.find("startup message") != std::string::npos);
    CHECK(contents.find("recognition failed") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("Logger filters messages below the configured level", "[logger]")
{
    auto& logger{ Logger::instance() };
    const auto root{ makeTestDirectory() };

    LoggerConfig config{};
    config.directory = root;
    config.level = LogLevel::Warning;

    REQUIRE(logger.init(config));
    logger.debug("debug-hidden");
    logger.info("info-hidden");
    logger.warning("warning-visible");
    logger.error("error-visible");
    logger.flush();
    logger.shutdown();

    const std::string contents{ readLogFiles(root) };
    CHECK(contents.find("debug-hidden") == std::string::npos);
    CHECK(contents.find("info-hidden") == std::string::npos);
    CHECK(contents.find("warning-visible") != std::string::npos);
    CHECK(contents.find("error-visible") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("Logger rotates files and preserves unrelated log files", "[logger]")
{
    auto& logger{ Logger::instance() };
    const auto root{ makeTestDirectory() };
    std::filesystem::create_directories(root);

    {
        std::ofstream unrelated{ root / "unrelated.log" };
        unrelated << "keep";
    }

    LoggerConfig config{};
    config.directory = root;
    config.maxFileSize = 128;
    config.maxFiles = 2;

    REQUIRE(logger.init(config));

    for(int i{}; i < 8; ++i)
    {
        logger.info(std::string(100, 'x') + " marker-" + std::to_string(i));
    }

    logger.flush();
    logger.shutdown();

    CHECK(countLogFiles(root) <= 2);
    CHECK(std::filesystem::exists(root / "unrelated.log"));
    CHECK(readLogFiles(root).find("marker-7") != std::string::npos);

    std::filesystem::remove_all(root);
}

TEST_CASE("Logger serializes concurrent writes", "[logger]")
{
    auto& logger{ Logger::instance() };
    const auto root{ makeTestDirectory() };

    LoggerConfig config{};
    config.directory = root;
    config.maxFileSize = 1024 * 1024;
    config.maxFiles = 2;

    REQUIRE(logger.init(config));

    constexpr int kThreadCount{ 4 };
    constexpr int kMessagesPerThread{ 50 };

    std::vector<std::thread> threads{};
    threads.reserve(kThreadCount);

    for(int threadIndex{}; threadIndex < kThreadCount; ++threadIndex)
    {
        threads.emplace_back([&logger, threadIndex] {
            for(int messageIndex{}; messageIndex < kMessagesPerThread; ++messageIndex)
            {
                logger.info("thread-" + std::to_string(threadIndex) +
                            " message-" + std::to_string(messageIndex));
            }
        });
    }

    for(auto& thread : threads)
    {
        thread.join();
    }

    logger.flush();
    logger.shutdown();

    const std::string contents{ readLogFiles(root) };
    std::size_t recordCount{};
    std::size_t position{};

    while((position = contents.find("[INFO]", position)) != std::string::npos)
    {
        ++recordCount;
        position += 6;
    }

    CHECK(recordCount == static_cast<std::size_t>(kThreadCount * kMessagesPerThread));

    std::filesystem::remove_all(root);
}
