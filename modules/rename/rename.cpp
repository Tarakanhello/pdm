#include "rename.h"
#include "rename_keys.h"

#include <algorithm>
#include <format>
#include <iostream>

#include "files_from_folder.h"

namespace pdm::modules
{

std::string Rename::buildNewName(const shared::Document& doc, const std::filesystem::path& oldPath, int page)
{
    // ИСПРАВЛЕНО: Если листов больше 1, добавляем " лN", иначе оставляем пустым
    std::string pageSuffix{ (doc.totalSheets > 1) ? (" л" + std::to_string(page)) : "" };

    return std::format("{}{} {}{}{}", doc.number,
                       shared::DocumentTypeCodeToString(doc.code),
                       doc.name,
                       pageSuffix,
                       oldPath.extension().string());
}



void Rename::execute(const shared::JobParams& params)
{
    std::cout << "[RenameModule] Module runs and ready to work!\n";

    // Проверяем, передало ли ядро нужные пути
    if(!params.has(rename::keys::scansFolder))
    {
        std::cerr << "[RenameModule] Error: paths ways has not found!\n";
        return;
    }

    const auto& scansFolder{ params.get(rename::keys::scansFolder) };

    try
    {
        auto scanFiles{ getScansPathsFromFolder(scansFolder) };

        performRename(scanFiles, scansFolder);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return;
    }

    std::cout << "[RenameModule] Module end work!\n";
}



std::vector<std::filesystem::path> Rename::getScansPathsFromFolder(std::filesystem::path folder)
{
    auto scanFiles{ utilities::getFilesFromFolder(folder, ".jpg", ".JPG", ".jpeg", ".JPEG") };

    std::ranges::sort(scanFiles);

    return scanFiles;
}



void Rename::performRename(const std::vector<std::filesystem::path>& scanFiles,
                           const std::filesystem::path& scansFolder)
{
    auto logPath{ scansFolder / "rename_log.csv" };
    RenameLogger logger{ logPath };

    RenameState state;

    for(const auto& file : scanFiles)
    {
        processSingleFile(file, state, logger);
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Renamed: " << state.successCount << "\n";
    std::cout << "Errors:  " << state.errorCount << "\n";
    std::cout << "Log saved to: " << logPath << "\n";
}



void Rename::processSingleFile(const std::filesystem::path& file,
                               RenameState& state,
                               RenameLogger& logger)
{
    auto recognizedOpt{ m_recognizer->recognize(file) };

    if(!recognizedOpt.has_value())
    {
        std::string errorMsg{ "Cannot recognize stamp on file" };
        logger.log(file.filename().string(), "UNKNOWN", "ERROR: " + errorMsg);
        ++state.errorCount;

        // Сбрасываем состояние, так как последовательность прервалась
        state.previousDoc = std::nullopt;
        return;
    }

    const auto& doc{ recognizedOpt.value() };

    // Проверяем, является ли этот документ продолжением предыдущего
    if (doc.sheet > 0)
    {
        state.currentPage = doc.sheet;
    }
    else
    {
        if(state.previousDoc.has_value() &&
            state.previousDoc->number == doc.number &&
            state.previousDoc->code == doc.code)
        {
            // Это следующая страница того же документа
            ++state.currentPage;
        }
        else
        {
            // Это новый документ
            state.currentPage = 1;
        }
    }
    auto newPath{ file.parent_path() / buildNewName(doc, file, state.currentPage) };

    if(renameOneFile(file, newPath, logger))
    {
        ++state.successCount;
    }
    else
    {
        ++state.errorCount;
    }

    state.previousDoc = doc;
}



bool Rename::renameOneFile(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, RenameLogger& logger)
{
    bool success{ false };

    try
    {
        // Проверяем, не существует ли уже файл с таким именем
        if(std::filesystem::exists(newPath))
        {
            std::string msg{ "Target file already exist" };
            logger.log(oldPath.filename().string(), newPath.filename().string(), "ERROR: " + msg);
        }
        else
        {
            std::filesystem::rename(oldPath, newPath);
            logger.log(oldPath.filename().string(), newPath.filename().string(), "OK");
            success = true;
        }
    }
    catch(const std::filesystem::filesystem_error& e)
    {
        logger.log(oldPath.filename().string(), newPath.filename().string(), std::string{ "ERROR: " } + e.what());
    }

    return success;
}

} // namespace pdm::modules
