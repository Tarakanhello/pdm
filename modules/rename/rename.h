#ifndef RENAME_H
#define RENAME_H

#include <string>
#include <string_view>

#include "imodule.h"

namespace pdm::modules
{
    namespace rename::keys
    {
        inline constexpr std::string_view excelPath = "excel_path";
        inline constexpr std::string_view scansFolder = "scans_folder";
    } // end namespace rename::keys


    class Rename final : public shared::IModule
    {
    private:
        std::string m_id{ "rename" };
        std::string m_name{ "Rename scans" };
        std::string m_version{ "0.0.1" };

    public:
        Rename() = default;
        ~Rename() override = default;

        std::string_view getId() const override { return m_id; }
        std::string_view getName() const override { return m_name; }
        std::string_view getVersion() const override { return m_version; }

        void execute(const shared::JobParams& params) override;
    };

} // end namespace


#endif // RENAME_H
