#ifndef IMODULE_H
#define IMODULE_H

#include <string_view>

#include "job_params.h"

namespace pdm::shared
{
    /**
     * @brief Интерфейс (контракт), который обязан реализовать любой модуль системы.
     *
     * Ядро системы (Core) работает только с этим интерфейсом и ничего не знает
     * о внутреннем устройстве конкретных модулей (rename, ocr, archive и т.д.).
     * Это обеспечивает слабую связанность и позволяет легко добавлять новые модули.
     */
    class IModule
    {
    public:
        /**
         * @brief Виртуальный деструктор.
         * @details Обязателен для полиморфных базовых классов, чтобы при удалении
         * объекта через указатель на базовый класс корректно вызывался деструктор наследника.
         */
        virtual ~IModule() = default;

        /**
         * @brief Получить уникальный строковый идентификатор модуля.
         * @return Уникальный ID (например, "rename").
         */
        virtual std::string_view getId() const = 0;

        /**
         * @brief Получить человекочитаемое имя модуля.
         * @return Имя для отображения в интерфейсе (например, "Переименование сканов").
         */
        virtual std::string_view getName() const = 0;

        /**
         * @brief Получить версию модуля.
         * @return Строка версии (например, "1.0.0").
         */
        virtual std::string_view getVersion() const = 0;

        /**
         * @brief Главный метод запуска работы модуля.
         * @param params Набор входных параметров (пути к файлам, флаги и т.д.).
         */
        virtual void execute(const JobParams& params) = 0;
    };

}

#endif // IMODULE_H
