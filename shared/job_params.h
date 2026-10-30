#ifndef JOBPARAMS_H
#define JOBPARAMS_H

#include <format>
#include <map>
#include <stdexcept>
#include <string>

namespace pdm::shared
{
    /**
     * @brief Контейнер для передачи динамических параметров в модули.
     *
     * Позволяет ядру (Core) передавать произвольный набор параметров (ключ-значение)
     * в модуль, не зная заранее, какие именно параметры нужны конкретному модулю.
     * Модули сами извлекают нужные им ключи.
     */
    class JobParams
    {
    private:
        /**
         * @brief Хранилище параметров.
         *
         * Используется std::less<> (прозрачный компаратор) для того, чтобы
         * можно было искать по std::string_view без создания временной std::string,
         * что повышает производительность.
         */
        std::map<std::string, std::string, std::less<>> m_params;

    public:
        /**
         * @brief Установить параметр.
         * @param key Ключ параметра (например, "excel_path").
         * @param value Значение параметра (например, "C:/spec.xls").
         */
        void set(std::string_view key, std::string value)
        {
            m_params.emplace(std::string{ key }, std::move(value));
        }

        /**
         * @brief Получить значение параметра по ключу.
         * @param key Ключ параметра.
         * @return Значение параметра в виде строки.
         * @throws std::runtime_error Если параметр с таким ключом не найден.
         */
        std::string get(std::string_view key) const
        {
            auto it{ m_params.find(key) };
            if(it == m_params.end())
            {
                throw std::runtime_error(std::format("Key [{}] is not found", key));
            }

            return it->second;
        }

        /**
         * @brief Проверить наличие параметра.
         * @param key Ключ параметра.
         * @return true, если параметр существует, иначе false.
         */
        bool has(std::string_view key) const
        {
            return m_params.find(key) != m_params.end();
        }
    };
} // end namespace


#endif // JOBPARAMS_H
