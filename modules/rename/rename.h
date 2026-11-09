#ifndef RENAME_H
#define RENAME_H

#include <concepts>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "imodule.h"
#include "istamp_recognizer.h"
#include "module_id.h"
#include "rename_logger.h"

namespace pdm::modules
{

    /**
     * @brief Модуль переименования отсканированных чертежей.
     *
     * Читает спецификацию, анализирует файлы в папке и переименовывает их
     * в соответствии с обозначениями и наименованиями из спецификации.
     */
    class Rename final : public shared::IModule
    {
    private:
        /**
         * @brief Внутреннее состояние процесса переименования.
         *
         * Хранит контекст, необходимый для отслеживания последовательности
         * листов одного документа и сбора статистики.
         */
        struct RenameState
        {
            std::optional<shared::Document> previousDoc{ std::nullopt };    ///< Документ, распознанный на предыдущем шаге (для склейки листов).
            int currentPage{ 1 };       ///< Текущий номер листа многолистового документа.
            size_t successCount{ 0 };   ///< Количество успешно переименованных файлов.
            size_t errorCount{ 0 };     ///< Количество ошибок при переименовании.
        };

        shared::ModuleId m_id{ shared::ModuleId::Rename };
        std::string m_name{ "Rename scans" };   ///< Имя модуля для отображения.
        std::string m_version{ "0.0.1" };       ///< Текущая версия модуля.

        std::unique_ptr<shared::IStampRecognizer> m_recognizer; ///< Интерфейс для распознавания штампа.

        // ===================================================================
        // ГРУППА 1: Утилиты поиска файлов
        // ===================================================================

        /**
         * @brief Найти файлы с заданными расширениями в папке.
         *
         * Использует вариативные шаблоны и свёртки (fold expressions) для
         * эффективного поиска файлов сразу по нескольким расширениям.
         *
         * @tparam ARGs Типы аргументов (должны конвертироваться в std::string_view).
         * @param folder Путь к папке для поиска.
         * @param args Одно или несколько расширений (например, ".xlsx" или ".jpg", ".jpeg").
         * @return Вектор путей к найденным файлам.
         * @throws std::runtime_error Если папки не существует или не найдено ни одного файла.
         */
        template<std::convertible_to<std::string_view>... ARGs>
        static std::vector<std::filesystem::path> getFilesFromFolder(const std::filesystem::path& folder, const ARGs&... args)
        {
            if(folder.empty())
            {
                throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: folder path is empty");
            }
            if(sizeof...(args) == 0)
            {
                throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: extensions list is empty");
            }
            if(!std::filesystem::exists(folder))
            {
                throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: folder does not exist: "
                                         + folder.string());
            }

            std::vector<std::filesystem::path> foundFiles;

            for(const auto& entry : std::filesystem::directory_iterator{ folder })
            {
                if(entry.is_regular_file() && ( (args == entry.path().extension()) || ... ) )
                {
                    foundFiles.push_back(entry.path());
                }
            }

            if(foundFiles.empty())
            {
                std::string extList;
                ((extList += std::string(args) + " "), ...);
                throw std::runtime_error("[pdm::modules::Rename::getFilesFromFolder] Error: no files with extensions ["
                                         + extList
                                         + "] in folder: "
                                         + folder.string());
            }

            return foundFiles;
        }

        /**
         * @brief Найти все отсканированные изображения в папке и отсортировать их.
         *
         * @param folder Путь к папке со сканами.
         * @return Отсортированный вектор путей к найденным изображениям.
         * @throws std::runtime_error Если в папке нет изображений.
         */
        std::vector<std::filesystem::path> getScansPathsFromFolder(std::filesystem::path folder);

        // ===================================================================
        // ГРУППА 2: Основная логика переименования
        // ===================================================================

        /**
         * @brief Запустить процесс переименования для всех найденных файлов.
         *
         * @details Создает лог-файл, инициализирует состояние (RenameState) и
         * поочередно передает файлы в processSingleFile.
         *
         * @param scanFiles Список путей к сканам (отсортированный).
         * @param scansFolder Папка со сканами (используется для сохранения лога).
         */
        void performRename(const std::vector<std::filesystem::path>& scanFiles,
                           const std::filesystem::path& scansFolder);

        /**
         * @brief Обработать один файл: распознать, сформировать имя и переименовать.
         *
         * @details Если штамп не распознан, пишет ошибку в лог и сбрасывает состояние.
         * Если распознан, проверяет, является ли файл продолжением предыдущего документа.
         *
         * @param file Путь к текущему файлу.
         * @param state Ссылка на объект состояния (обновляется в процессе работы).
         * @param logger Ссылка на логгер.
         */
        void processSingleFile(const std::filesystem::path& file,
                               RenameState& state,
                               RenameLogger& logger);

        /**
         * @brief Сформировать новое имя файла на основе документа и старого пути.
         *
         * @param doc Документ (содержит номер, код, наименование и количество листов).
         * @param oldPath Старый путь к файлу (используется для извлечения расширения).
         * @param page Номер текущего листа (если листов больше 1).
         * @return Новое имя файла (без пути).
         */
        std::string buildNewName(const shared::Document& doc, const std::filesystem::path& oldPath, int page = 0);

        // ===================================================================
        // ГРУППА 3: Файловые операции
        // ===================================================================

        /**
         * @brief Безопасно переименовать один файл.
         *
         * @details Проверяет, не существует ли уже файл с целевым именем.
         * Выполняет переименование через std::filesystem. Логирует результат.
         *
         * @param oldPath Старый путь к файлу.
         * @param newPath Новый путь к файлу.
         * @param logger Ссылка на логгер для записи результата.
         * @return true, если переименование прошло успешно, иначе false.
         */
        bool renameOneFile(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, RenameLogger& logger);


    public:
        // ===================================================================
        // Публичный интерфейс (Реализация контракта IModule)
        // ===================================================================

        /**
         * @brief Конструктор с внедрением зависимости (Dependency Injection).
         *
         * @param recognizer Умный указатель на распознаватель штампов (IStampRecognizer).
         */
        Rename(std::unique_ptr<shared::IStampRecognizer> recognizer)
            : m_recognizer{ std::move(recognizer) }
        {}

        ~Rename() override = default;

        /**
         * @brief Получить ID модуля.
         * @return ModuleId::Rename.
         */
        shared::ModuleId getId() const override { return m_id; }

        /**
         * @brief Получить имя модуля.
         * @return Строка с именем.
         */
        std::string_view getName() const override { return m_name; }

        /**
         * @brief Получить версию модуля.
         * @return Строка с версией.
         */
        std::string_view getVersion() const override { return m_version; }

        /**
         * @brief Запустить процесс переименования (главный метод модуля).
         * @param params Параметры задачи (должны содержать ключ scansFolder).
         */
        void execute(const shared::JobParams& params) override;
    };




} // end namespace


#endif // RENAME_H
