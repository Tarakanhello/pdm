#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <array>
#include <string>
#include <utility>

/**
 * @brief Пространство имен для общих типов и интерфейсов системы PDM.
 */
namespace pdm::shared
{
    /**
     * @brief Классификатор типов конструкторских документов по ГОСТ 2.102.
     *
     * Используется для определения того, как обрабатывать документ.
     * Например, у чертежа детали есть материал, у ТУ — нет.
     */
    enum class DocumentTypeCode
    {
        DetailDrawing,                      ///< ЧД  Чертеж детали // основной документ код может отсутствовать
        CADAssemblyModel,                   ///< ЭСБ Электронная модель сборочной единицы
        AssemblyDrawing,                    ///< СБ  Сборочный чертеж
        GeneralArrangementDrawing,          ///< ВО  Чертеж общего вида
        TheoreticalDrawing,                 ///< ТЧ  Теоретический чертеж
        DimensionalDrawing,                 ///< ГЧ  Габаритный чертеж
        WiringDiagram,                      ///< МЭ  Электромонтажный чертеж
        InstallationDrawing,                ///< МЧ  Монтажный чертеж
        PackingDrawing,                     ///< УЧ  Упаковочный чертеж

        Specification,                      ///< СП  Спецификация // основной документ код может отсутствовать
        ListOfSpecifications,               ///< ВС  Ведомость спецификаций
        ListOfReferenceDocuments,           ///< ВД  Ведомость ссылочных документов
        PurchasedPartsList,                 ///< ВП  Ведомость покупных изделий
        ApprovedPurchasedPartsList,         ///< ВИ  Ведомость разрешения применения покупных изделий
        ListOfOriginalsHolders,             ///< ДП  Ведомость держателей подлинников
        ScheduleOfTechnicalProposal,        ///< ПТ  Ведомость технического предложения
        ScheduleOfPreliminaryDesign,        ///< ЭП  Ведомость эскизного проекта
        ScheduleOfDetailedDesign,           ///< ТП  Ведомость технического проекта
        ExplanatoryNote,                    ///< ПЗ  Пояснительная записка
        ScheduleOfElectronicDocuments,      ///< ВДЭ Ведомость электронных документов
        TechnicalSpecifications,            ///< ТУ  Технические условия
        TestProgramAndProcedure,            ///< ПМ  Программа и методика испытаний
        Tables,                             ///< ТБ  Таблицы
        Calculations,                       ///< РР  Расчеты
        Instruction,                        ///< И   Инструкция
        OtherDocuments,                     ///< Д   Документы прочие

        // Схемы

        ElectricalBlockDiagram,             ///< Э1  Схема электрическая структурная
        ElectricalFunctionalDiagram,        ///< Э2  Схема электрическая функциональная
        ElectricalSchematicDiagram,         ///< Э3  Схема электрическая принципиальная
        ElectricalWiringDiagram,            ///< Э4  Схема электрическая соединений (монтажная)
        ElectricalConnectionDiagram,        ///< Э5  Схема электрическая подключения
        ElectricalGeneralDiagram,           ///< Э6  Схема электрическая общая
        ElectricalLayoutDiagram,            ///< Э7  Схема электрическая расположения
        ElectricalCombinedDiagram,          ///< Э0  Схема электрическая объединенная

        HydraulicBlockDiagram,              ///< Г1  Схема гидравлическая структурная
        HydraulicFunctionalDiagram,         ///< Г2  Схема гидравлическая функциональная
        HydraulicSchematicDiagram,          ///< Г3  Схема гидравлическая принципиальная
        HydraulicPipingDiagram,             ///< Г4  Схема гидравлическая соединений (монтажная)
        HydraulicConnectionDiagram,         ///< Г5  Схема гидравлическая подключения
        HydraulicGeneralDiagram,            ///< Г6  Схема гидравлическая общая
        HydraulicLayoutDiagram,             ///< Г7  Схема гидравлическая расположения
        HydraulicCombinedDiagram,           ///< Г0  Схема гидравлическая объединенная

        PneumaticBlockDiagram,              ///< П1  Схема пневматическая структурная
        PneumaticFunctionalDiagram,         ///< П2  Схема пневматическая функциональная
        PneumaticSchematicDiagram,          ///< П3  Схема пневматическая принципиальная
        PneumaticPipingDiagram,             ///< П4  Схема пневматическая соединений (монтажная)
        PneumaticConnectionDiagram,         ///< П5  Схема пневматическая подключения
        PneumaticGeneralDiagram,            ///< П6  Схема пневматическая общая
        PneumaticLayoutDiagram,             ///< П7  Схема пневматическая расположения
        PneumaticCombinedDiagram,           ///< П0  Схема пневматическая объединенная

        GasBlockDiagram,                    ///< Х1  Схема газовая структурная
        GasFunctionalDiagram,               ///< Х2  Схема газовая функциональная
        GasSchematicDiagram,                ///< Х3  Схема газовая принципиальная
        GasPipingDiagram,                   ///< Х4  Схема газовая соединений (монтажная)
        GasConnectionDiagram,               ///< Х5  Схема газовая подключения
        GasGeneralDiagram,                  ///< Х6  Схема газовая общая
        GasLayoutDiagram,                   ///< Х7  Схема газовая расположения
        GasCombinedDiagram,                 ///< Х0  Схема газовая объединенная

        KinematicBlockDiagram,              ///< К1  Схема кинематическая структурная
        KinematicFunctionalDiagram,         ///< К2  Схема кинематическая функциональная
        KinematicSchematicDiagram,          ///< К3  Схема кинематическая принципиальная
        KinematicLinkageDiagram,            ///< К4  Схема кинематическая соединений (монтажная)
        KinematicConnectionDiagram,         ///< К5  Схема кинематическая подключения
        KinematicGeneralDiagram,            ///< К6  Схема кинематическая общая
        KinematicLayoutDiagram,             ///< К7  Схема кинематическая расположения
        KinematicCombinedDiagram,           ///< К0  Схема кинематическая объединенная

        VacuumBlockDiagram,                 ///< В1  Схема вакуумная структурная
        VacuumFunctionalDiagram,            ///< В2  Схема вакуумная функциональная
        VacuumSchematicDiagram,             ///< В3  Схема вакуумная принципиальная
        VacuumPipingDiagram,                ///< В4  Схема вакуумная соединений (монтажная)
        VacuumConnectionDiagram,            ///< В5  Схема вакуумная подключения
        VacuumGeneralDiagram,               ///< В6  Схема вакуумная общая
        VacuumLayoutDiagram,                ///< В7  Схема вакуумная расположения
        VacuumCombinedDiagram,              ///< В0  Схема вакуумная объединенная

        OpticalBlockDiagram,                ///< Л1  Схема оптическая структурная
        OpticalFunctionalDiagram,           ///< Л2  Схема оптическая функциональная
        OpticalSchematicDiagram,            ///< Л3  Схема оптическая принципиальная
        OpticalInterconnectionDiagram,      ///< Л4  Схема оптическая соединений (монтажная)
        OpticalConnectionDiagram,           ///< Л5  Схема оптическая подключения
        OpticalGeneralDiagram,              ///< Л6  Схема оптическая общая
        OpticalLayoutDiagram,               ///< Л7  Схема оптическая расположения
        OpticalCombinedDiagram,             ///< Л0  Схема оптическая объединенная

        PowerBlockDiagram,                  ///< Р1  Схема энергетическая структурная
        PowerFunctionalDiagram,             ///< Р2  Схема энергетическая функциональная
        PowerSchematicDiagram,              ///< Р3  Схема энергетическая принципиальная
        PowerInterconnectionDiagram,        ///< Р4  Схема энергетическая соединений (монтажная)
        PowerConnectionDiagram,             ///< Р5  Схема энергетическая подключения
        PowerGeneralDiagram,                ///< Р6  Схема энергетическая общая
        PowerLayoutDiagram,                 ///< Р7  Схема энергетическая расположения
        PowerCombinedDiagram,               ///< Р0  Схема энергетическая объединенная

        StructureBlockDiagram,              ///< Е1  Схема деления структурная
        StructureFunctionalDiagram,         ///< Е2  Схема деления функциональная
        StructureSchematicDiagram,          ///< Е3  Схема деления принципиальная
        StructureInterconnectionDiagram,    ///< Е4  Схема деления соединений (монтажная)
        StructureConnectionDiagram,         ///< Е5  Схема деления подключения
        StructureGeneralDiagram,            ///< Е6  Схема деления общая
        StructureLayoutDiagram,             ///< Е7  Схема деления расположения
        StructureCombinedDiagram,           ///< Е0  Схема деления объединенная

        CombinedBlockDiagram,               ///< С1  Схема комбинированная структурная
        CombinedFunctionalDiagram,          ///< С2  Схема комбинированная функциональная
        CombinedSchematicDiagram,           ///< С3  Схема комбинированная принципиальная
        CombinedInterconnectionDiagram,     ///< С4  Схема комбинированная соединений (монтажная)
        CombinedConnectionDiagram,          ///< С5  Схема комбинированная подключения
        CombinedGeneralDiagram,             ///< С6  Схема комбинированная общая
        CombinedLayoutDiagram,              ///< С7  Схема комбинированная расположения
        CombinedCombinedDiagram,            ///< С0  Схема комбинированная объединенная

        // Эксплуатационные документы

        UserManual,                         ///< РЭ	 Руководство по эксплуатации
        Instructions,                       ///< ИМ  Инструкция по монтажу, пуску, регулированию и обкатке изделия
        Logbook,                            ///< ФО  Формуляр
        DataSheet,                          ///< ПС	 Паспорт
        Label,                              ///< ЭТ  Этикетка
        ProductCatalog,                     ///< КИ	 Каталог изделия
        SparePartsNorms,                    ///< НЗЧ Нормы расхода запасных частей
        MaterialsNorms,                     ///< НМ	 Нормы расхода материалов
        SpareParts,                         ///< ЗИ  Ведомость ЗИП
        EducationalAndTechnicalPosters,     ///< УП	 Учебно-технические плакаты
        SpecialOperationalInstructions,     ///< ИС	 Инструкции эксплуатационные специальные
        ListOfOperationalDocuments,         ///< ВЭ	 Ведомость эксплуатационных документов

        // Ремонтные документы - отсутствуют TODO! пока не требуется. в последнюю очередь

        Unknown,                            ///< Тип ещё не определён
    }; // end enum DocumentTypeCode



    /**
     * @brief Основная сущность предметной области — документ.
     *
     * Содержит минимальный набор полей, необходимых для идентификации документа
     * на ранних этапах работы системы.
     */
    struct Document
    {
        std::string number;                                 ///< Обозначение (номер) документа, например "13084-1-00000-00".
        DocumentTypeCode code{ DocumentTypeCode::Unknown }; ///< Тип документа по ГОСТ (по умолчанию Unknown).
        std::string name;                                   ///< Наименование документа, например "Домкрат для ЖДВ".
        int totalSheets{ 1 };                               ///< Всего листов
    };



    constexpr std::array<std::pair<DocumentTypeCode, std::string_view>, static_cast<size_t>(DocumentTypeCode::Unknown) + 1> k_documentTypeCodeMap
    {{
        { DocumentTypeCode::DetailDrawing,                  "ЧД"  },
        { DocumentTypeCode::CADAssemblyModel,               "ЭСБ" },
        { DocumentTypeCode::AssemblyDrawing,                "СБ"  },
        { DocumentTypeCode::GeneralArrangementDrawing,      "ВО"  },
        { DocumentTypeCode::TheoreticalDrawing,             "ТЧ"  },
        { DocumentTypeCode::DimensionalDrawing,             "ГЧ"  },
        { DocumentTypeCode::WiringDiagram,                  "МЭ"  },
        { DocumentTypeCode::InstallationDrawing,            "МЧ"  },
        { DocumentTypeCode::PackingDrawing,                 "УЧ"  },

        { DocumentTypeCode::Specification,                  "СП"  },
        { DocumentTypeCode::ListOfSpecifications,           "ВС"  },
        { DocumentTypeCode::ListOfReferenceDocuments,       "ВД"  },
        { DocumentTypeCode::PurchasedPartsList,             "ВП"  },
        { DocumentTypeCode::ApprovedPurchasedPartsList,     "ВИ"  },
        { DocumentTypeCode::ListOfOriginalsHolders,         "ДП"  },
        { DocumentTypeCode::ScheduleOfTechnicalProposal,    "ПТ"  },
        { DocumentTypeCode::ScheduleOfPreliminaryDesign,    "ЭП"  },
        { DocumentTypeCode::ScheduleOfDetailedDesign,       "ТП"  },
        { DocumentTypeCode::ExplanatoryNote,                "ПЗ"  },
        { DocumentTypeCode::ScheduleOfElectronicDocuments,  "ВДЭ" },
        { DocumentTypeCode::TechnicalSpecifications,        "ТУ"  },
        { DocumentTypeCode::TestProgramAndProcedure,        "ПМ"  },
        { DocumentTypeCode::Tables,                         "ТБ"  },
        { DocumentTypeCode::Calculations,                   "РР"  },
        { DocumentTypeCode::Instruction,                    "И"   },
        { DocumentTypeCode::OtherDocuments,                 "Д"   },

        { DocumentTypeCode::ElectricalBlockDiagram,         "Э1"  },
        { DocumentTypeCode::ElectricalFunctionalDiagram,    "Э2"  },
        { DocumentTypeCode::ElectricalSchematicDiagram,     "Э3"  },
        { DocumentTypeCode::ElectricalWiringDiagram,        "Э4"  },
        { DocumentTypeCode::ElectricalConnectionDiagram,    "Э5"  },
        { DocumentTypeCode::ElectricalGeneralDiagram,       "Э6"  },
        { DocumentTypeCode::ElectricalLayoutDiagram,        "Э7"  },
        { DocumentTypeCode::ElectricalCombinedDiagram,      "Э0"  },

        { DocumentTypeCode::HydraulicBlockDiagram,          "Г1"  },
        { DocumentTypeCode::HydraulicFunctionalDiagram,     "Г2"  },
        { DocumentTypeCode::HydraulicSchematicDiagram,      "Г3"  },
        { DocumentTypeCode::HydraulicPipingDiagram,         "Г4"  },
        { DocumentTypeCode::HydraulicConnectionDiagram,     "Г5"  },
        { DocumentTypeCode::HydraulicGeneralDiagram,        "Г6"  },
        { DocumentTypeCode::HydraulicLayoutDiagram,         "Г7"  },
        { DocumentTypeCode::HydraulicCombinedDiagram,       "Г0"  },

        { DocumentTypeCode::PneumaticBlockDiagram,          "П1"  },
        { DocumentTypeCode::PneumaticFunctionalDiagram,     "П2"  },
        { DocumentTypeCode::PneumaticSchematicDiagram,      "П3"  },
        { DocumentTypeCode::PneumaticPipingDiagram,         "П4"  },
        { DocumentTypeCode::PneumaticConnectionDiagram,     "П5"  },
        { DocumentTypeCode::PneumaticGeneralDiagram,        "П6"  },
        { DocumentTypeCode::PneumaticLayoutDiagram,         "П7"  },
        { DocumentTypeCode::PneumaticCombinedDiagram,       "П0"  },

        { DocumentTypeCode::GasBlockDiagram,                "Х1"  },
        { DocumentTypeCode::GasFunctionalDiagram,           "Х2"  },
        { DocumentTypeCode::GasSchematicDiagram,            "Х3"  },
        { DocumentTypeCode::GasPipingDiagram,               "Х4"  },
        { DocumentTypeCode::GasConnectionDiagram,           "Х5"  },
        { DocumentTypeCode::GasGeneralDiagram,              "Х6"  },
        { DocumentTypeCode::GasLayoutDiagram,               "Х7"  },
        { DocumentTypeCode::GasCombinedDiagram,             "Х0"  },

        { DocumentTypeCode::KinematicBlockDiagram,          "К1"  },
        { DocumentTypeCode::KinematicFunctionalDiagram,     "К2"  },
        { DocumentTypeCode::KinematicSchematicDiagram,      "К3"  },
        { DocumentTypeCode::KinematicLinkageDiagram,        "К4"  },
        { DocumentTypeCode::KinematicConnectionDiagram,     "К5"  },
        { DocumentTypeCode::KinematicGeneralDiagram,        "К6"  },
        { DocumentTypeCode::KinematicLayoutDiagram,         "К7"  },
        { DocumentTypeCode::KinematicCombinedDiagram,       "К0"  },

        { DocumentTypeCode::VacuumBlockDiagram,             "В1"  },
        { DocumentTypeCode::VacuumFunctionalDiagram,        "В2"  },
        { DocumentTypeCode::VacuumSchematicDiagram,         "В3"  },
        { DocumentTypeCode::VacuumPipingDiagram,            "В4"  },
        { DocumentTypeCode::VacuumConnectionDiagram,        "В5"  },
        { DocumentTypeCode::VacuumGeneralDiagram,           "В6"  },
        { DocumentTypeCode::VacuumLayoutDiagram,            "В7"  },
        { DocumentTypeCode::VacuumCombinedDiagram,          "В0"  },

        { DocumentTypeCode::OpticalBlockDiagram,            "Л1"  },
        { DocumentTypeCode::OpticalFunctionalDiagram,       "Л2"  },
        { DocumentTypeCode::OpticalSchematicDiagram,        "Л3"  },
        { DocumentTypeCode::OpticalInterconnectionDiagram,  "Л4"  },
        { DocumentTypeCode::OpticalConnectionDiagram,       "Л5"  },
        { DocumentTypeCode::OpticalGeneralDiagram,          "Л6"  },
        { DocumentTypeCode::OpticalLayoutDiagram,           "Л7"  },
        { DocumentTypeCode::OpticalCombinedDiagram,         "Л0"  },

        { DocumentTypeCode::PowerBlockDiagram,              "Р1"  },
        { DocumentTypeCode::PowerFunctionalDiagram,         "Р2"  },
        { DocumentTypeCode::PowerSchematicDiagram,          "Р3"  },
        { DocumentTypeCode::PowerInterconnectionDiagram,    "Р4"  },
        { DocumentTypeCode::PowerConnectionDiagram,         "Р5"  },
        { DocumentTypeCode::PowerGeneralDiagram,            "Р6"  },
        { DocumentTypeCode::PowerLayoutDiagram,             "Р7"  },
        { DocumentTypeCode::PowerCombinedDiagram,           "Р0"  },

        { DocumentTypeCode::StructureBlockDiagram,          "Е1"  },
        { DocumentTypeCode::StructureFunctionalDiagram,     "Е2"  },
        { DocumentTypeCode::StructureSchematicDiagram,      "Е3"  },
        { DocumentTypeCode::StructureInterconnectionDiagram,"Е4"  },
        { DocumentTypeCode::StructureConnectionDiagram,     "Е5"  },
        { DocumentTypeCode::StructureGeneralDiagram,        "Е6"  },
        { DocumentTypeCode::StructureLayoutDiagram,         "Е7"  },
        { DocumentTypeCode::StructureCombinedDiagram,       "Е0"  },

        { DocumentTypeCode::CombinedBlockDiagram,           "С1"  },
        { DocumentTypeCode::CombinedFunctionalDiagram,      "С2"  },
        { DocumentTypeCode::CombinedSchematicDiagram,       "С3"  },
        { DocumentTypeCode::CombinedInterconnectionDiagram, "С4"  },
        { DocumentTypeCode::CombinedConnectionDiagram,      "С5"  },
        { DocumentTypeCode::CombinedGeneralDiagram,         "С6"  },
        { DocumentTypeCode::CombinedLayoutDiagram,          "С7"  },
        { DocumentTypeCode::CombinedCombinedDiagram,        "С0"  },

        { DocumentTypeCode::UserManual,                     "РЭ"  },
        { DocumentTypeCode::Instructions,                   "ИМ"  },
        { DocumentTypeCode::Logbook,                        "ФО"  },
        { DocumentTypeCode::DataSheet,                      "ПС"  },
        { DocumentTypeCode::Label,                          "ЭТ"  },
        { DocumentTypeCode::ProductCatalog,                 "КИ"  },
        { DocumentTypeCode::SparePartsNorms,                "НЗЧ" },
        { DocumentTypeCode::MaterialsNorms,                 "НМ"  },
        { DocumentTypeCode::SpareParts,                     "ЗИ"  },
        { DocumentTypeCode::EducationalAndTechnicalPosters, "УП"  },
        { DocumentTypeCode::SpecialOperationalInstructions, "ИС"  },
        { DocumentTypeCode::ListOfOperationalDocuments,     "ВЭ"  },

        { DocumentTypeCode::Unknown,                        ""    },
    }};


    inline std::string_view DocumentTypeCodeToString(DocumentTypeCode code, bool isFullCode = false)
    {
        if(!isFullCode && (code == DocumentTypeCode::DetailDrawing || code == DocumentTypeCode::Specification))
        {
            return {};
        }

        for(const auto& entry : k_documentTypeCodeMap)
        {
            if(entry.first == code)
            {
                return entry.second;
            }
        }

        return {};
    }

} // end namespace pdm


#endif // DOCUMENT_H
