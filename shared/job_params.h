#ifndef JOBPARAMS_H
#define JOBPARAMS_H

#include <format>
#include <map>
#include <stdexcept>
#include <string>

namespace pdm::shared
{
    class JobParams
    {
    private:
        std::map<std::string, std::string, std::less<>> m_params;

    public:
        // Установить параметр
        void set(std::string_view key, std::string value)
        {
            m_params.emplace(std::string{ key }, std::move(value));
        }

        // Получить параметр (выбрасывает исключение, если ключа нет)
        std::string get(std::string_view key) const
        {
            auto it{ m_params.find(key) };
            if(it == m_params.end())
            {
                throw std::runtime_error(std::format("Key [{}] is not found", key));
            }

            return it->second;
        }

        // Проверить, есть ли параметр
        bool has(std::string_view key) const
        {
            return m_params.find(key) != m_params.end();
        }
    };
} // end namespace


#endif // JOBPARAMS_H
