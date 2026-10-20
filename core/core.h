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

    class Core final
    {
    private:
        // хеш-таблица, где ключ - строка ID, значение - умный указатель
        std::unordered_map<std::string, std::unique_ptr<shared::IModule>> m_modules;

    public:
        Core() = default;
        ~Core() = default;

        // Ядро забирает модуль себе во владение (unique_ptr)
        void addModule(std::unique_ptr<shared::IModule> module);

        // Запустить модуль по его ID с параметрами
        void runModule(const std::string& moduleId, const shared::JobParams& params);

    };

}

#endif // CORE_H
