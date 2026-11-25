#include "rename.h"
#include "rename_keys.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "files_from_folder.h"

namespace pdm::modules
{

namespace
{

struct RecognizedFile
{
    std::filesystem::path oldPath;
    std::optional<shared::Document> doc;
};

struct DocumentGroup
{
    std::string number;
    std::vector<size_t> indices;
};

} // end anonymuis namespace



std::string Rename::buildNewName(const shared::Document& doc, const std::filesystem::path& oldPath, int page)
{
    // Если листов больше 1, добавляем " лN", перед расширением
    std::string pageSuffix{ (doc.totalSheets > 1) ? (" л" + std::to_string(page)) : "" };

    std::string rawName{ std::format("{}{} {}{}{}",
                                    doc.number,
                                    shared::DocumentTypeCodeToString(doc.code),
                                    doc.name,
                                    pageSuffix,
                                    oldPath.extension().string()) };

    // Удаляем символы, запрещённые в именах файлов Windows
    std::erase_if(rawName, [](char c) {
        return c == '\\' || c == '/' || c == ':' || c == '*' ||
               c == '?'  || c == '"' || c == '<' || c == '>' || c == '|';
    });

    return rawName;
}



void Rename::execute(const shared::JobParams& params)
{
    m_logger.info("[RenameModule] Module runs and ready to work!\n");

    // Проверяем, передало ли ядро нужные пути
    if(!params.has(rename::keys::scansFolder))
    {
        m_logger.error("[RenameModule] Error: paths ways has not found!\n");
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
        m_logger.error(std::format("{}\n",e.what()));
        return;
    }

    m_logger.log("[RenameModule] Module end work!\n");
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
    auto logPath{ scansFolder /  "logs/rename_log.csv" };
    RenameLogger logger{ logPath };

    RenameState state;

    // 1. Проход 1: распознаём все сканы в папке
    std::vector<RecognizedFile> items{};
    items.reserve(scanFiles.size());

    for(const auto& file : scanFiles)
    {
        items.push_back({ file, m_recognizer->recognize(file) });
    }

    // 2. Формируем группы листов по документам:
    // Лист с непустым наименованием (Форма 1) всегда открывает новый документ,
    // а последующие листы без наименования (Форма 2а) прикрепляются к нему по совпадению number.
    std::vector<DocumentGroup> groups{};
    for(size_t i{ 0 }; i < items.size(); ++i)
    {
        if(!items[i].doc.has_value())
        {
            continue;
        }

        const auto& d{ items[i].doc.value() };

        if(!d.name.empty())
        {
            groups.push_back({d.number, { i }});
        }
        else // Ищем последний открытый документ с таким же децимальным номером
        {
            auto it{ std::find_if(groups.rbegin(), groups.rend(), [&d](const DocumentGroup& g){
                                    return g.number == d.number; }) };
            if(it != groups.rend())
            {
                it->indices.push_back(i);
            }
            else
            {
                groups.push_back({ d.number, { i } });
            }
        }
    }

    // 3. Обогащаем метаданные внутри каждой группы (подтягиваем name, code и totalSheets с 1-го листа)
    for(auto& group : groups)
    {
        std::string masterName{};
        shared::DocumentTypeCode masterCode{ shared::DocumentTypeCode::Unknown };
        int maxTotalSheets{ static_cast<int>(group.indices.size()) };

        for(size_t idx : group.indices)
        {
            const auto& d{ items[idx].doc.value() };
            if(!d.name.empty() && masterName.empty())
            {
                masterName = d.name;
            }
            if(d.code != shared::DocumentTypeCode::Unknown &&
               masterCode == shared::DocumentTypeCode::Unknown)
            {
                masterCode = d.code;
            }
            maxTotalSheets = std::max({ maxTotalSheets, d.sheet });
            if(d.totalSheets > 1 && d.totalSheets <= 30)
            {
                maxTotalSheets = std::max(maxTotalSheets, d.totalSheets);
            }
        }

        // Назначаем уникальные номера страниц внутри документа, защищаясь от дублей при нечитаемых цифрах

        std::set<int> usedSheets{};
        for(size_t idx : group.indices)
        {
            auto& d{ items[idx].doc.value() };

            if(!masterName.empty())
            {
                d.name = masterName;
            }

            if(masterCode != shared::DocumentTypeCode::Unknown)
            {
                d.code = masterCode;
            }

            d.totalSheets = maxTotalSheets;


            if(!usedSheets.contains(d.sheet))
            {
                usedSheets.insert(d.sheet);
            }
            else
            {
                int nextFree{ 1 };
                while(usedSheets.contains(nextFree))
                {
                    ++nextFree;
                }
                d.sheet = nextFree;
                usedSheets.insert(nextFree);
            }
        }
    }

    // 4. Проход 2: переименовываем файлы и пишем лог
    for(const auto& item : items)
    {
        if(!item.doc.has_value())
        {
            std::string errorMsg{ "Cannot recognize stamp on file" };
            logger.log(item.oldPath.filename().string(), "UNKNOWN", "ERROR: " + errorMsg);
            ++state.errorCount;
            continue;
        }

        const auto& doc{ item.doc.value() };
        auto newPath{ item.oldPath.parent_path() / buildNewName(doc, item.oldPath, doc.sheet) };

        if(renameOneFile(item.oldPath, newPath, logger))
        {
            ++state.successCount;
        }
        else
        {
            ++state.errorCount;
        }
    }

    m_logger.log("\n=== Summary ===\n");
    m_logger.log(std::format("Renamed: {}\n", state.successCount));
    m_logger.log(std::format("Errors:  {}\n", state.errorCount));
    m_logger.log(std::format("Log saved to: {}\n", logPath.string()));
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
