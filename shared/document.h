#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <string>

namespace pdm::shared
{
    enum class DocumentTypeCode
    {
        DetailDrawing,                      ///< ЧД  Чертеж детали // основной документ код может отсутствовать
        CADAssemblyModel,                   ///< ЭСБ Электронная модель сборочной единицы
        AssemblyDrawing,                   ///< СБ  Сборочный чертеж
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

    struct Document
    {
        std::string number;
        std::string name;
        DocumentTypeCode code{ DocumentTypeCode::Unknown };
    };
} // end namespace pdm


#endif // DOCUMENT_H
