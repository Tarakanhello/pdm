#ifndef RENAME_H
#define RENAME_H

#include <string>
#include <string_view>

#include "imodule.h"

class RenameModule final : public IModule
{
private:
    std::string m_id{ "Rename" };
    std::string m_name{ "Переименование сканов" };
    std::string m_version{ "0.0.1" };

public:
    RenameModule() = default;
    ~RenameModule() override = default;

    std::string_view getId() const override { return m_id; }
    std::string_view getName() const override { return m_name; }
    std::string_view getVersion() const override { return m_version; }

    void execute() override;
};

#endif // RENAME_H
