#ifndef CORE_H
#define CORE_H

#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

#include "imodule.h"
#include "job_params.h"

namespace pdm
{
    /**
     * @brief Ядро системы (оркестратор).
     *
     * Отвечает за регистрацию модулей и их запуск. Ядро ничего не знает
     * о внутреннем устройстве конкретных модулей (rename, ocr и т.д.),
     * оно работает только через интерфейс IModule.
     * Это обеспечивает слабую связанность (loose coupling) и позволяет
     * легко расширять систему новыми модулями.
     */
    class Core final
    {
    private:
        /**
         * @brief Реестр (хранилище) всех зарегистрированных модулей.
         *
         * Используется std::unordered_map для быстрого поиска модуля по его ID
         * за константное время O(1). Ключом является строковый ID модуля,
         * значением — умный указатель unique_ptr, который владеет модулем.
         */
        std::unordered_map<std::string, std::unique_ptr<shared::IModule>> m_modules;

    public:
        Core() = default;
        ~Core() = default;

        /**
         * @brief Зарегистрировать новый модуль в ядре.
         *
         * Ядро забирает модуль себе во владение (передается unique_ptr).
         * Если модуль с таким же ID уже зарегистрирован, выводится предупреждение
         * и новый модуль не добавляется (защита от дубликатов).
         *
         * @param module Умный указатель на модуль, реализующий интерфейс IModule.
         */
        void addModule(std::unique_ptr<shared::IModule> module);

        /**
         * @brief Запустить зарегистрированный модуль по его ID.
         *
         * Выполняет поиск модуля в реестре. Если модуль найден, вызывается его
         * метод execute() с переданными параметрами. Если не найден — выводится
         * сообщение об ошибке.
         *
         * @param moduleId Строковый идентификатор модуля (например, "rename").
         * @param params Набор параметров (JobParams), передаваемых модулю для выполнения.
         */
        void runModule(const std::string& moduleId, const shared::JobParams& params);

    };

}

#endif // CORE_H
