#ifndef MODULE_ID_H
#define MODULE_ID_H

#include <array>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace pdm::shared
{

    /**
     * @brief Типизированный идентификатор модуля.
     *
     * Внутри ядра используется enum для безопасности и скорости (O(1) без хеширования).
     * Строки используются только на границах (CLI, JSON).
     * При добавлении нового модуля не забудьте добавить его сюда и в kAllModuleIds.
     */
    enum class ModuleId
    {
        Rename,
        NumberOfElements ///< Служебный элемент, обозначающий количество модулей (должен быть последним).
    };



    /**
     * @brief Преобразовать строковое имя модуля в типизированный ID.
     * @param s Строковое имя (например, "rename").
     * @return ModuleId, если имя найдено, иначе std::nullopt.
     */
    constexpr inline std::optional<ModuleId>  ParseModuleId(std::string_view s)
    {
        if( s == "rename")  return ModuleId::Rename;

        return std::nullopt;
    }



    /**
     * @brief Преобразовать типизированный ID в строку (для логов и UI).
     * @param id Идентификатор модуля.
     * @return Строковое имя модуля.
     * @throws std::logic_error Если передан неверный ID (например, NumberOfElements).
     */
    constexpr inline std::string_view idToString(ModuleId id)
    {
        switch(id)
        {
        case ModuleId::Rename:
            return "rename";
        case ModuleId::NumberOfElements:
            throw std::logic_error("pdm::shared::idToString: NumberOfElements is not a valid modul");
        }

        throw std::logic_error("pdm::shared::idToString: unknown ModuleId");

        return "<unknown>";
    }



    /**
     * @brief Массив всех доступных модулей.
     * @details Используется в Composition Root для автоматической регистрации.
     * При добавлении нового модуля просто добавьте его в этот массив.
     */
    inline constexpr std::array<ModuleId, static_cast<size_t>
                                (ModuleId::NumberOfElements)> kAllModuleIds{ ModuleId::Rename };

}




#endif // MODULE_ID_H
