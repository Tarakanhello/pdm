#ifndef ONNX_STAMP_RECOGNIZER_H
#define ONNX_STAMP_RECOGNIZER_H

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <opencv2/dnn.hpp>

#include "istamp_recognizer.h"
#include "diagnostic/logger.h"

namespace pdm::infrastructure
{

/**
     * @brief Распознаватель штампа ЕСКД на базе моделей PaddleOCR (ONNX) через OpenCV DNN.
     *
     * @details Выполняет локализацию текстовых блоков в области основной надписи с помощью
     * детектора DBNet (`det.onnx`), распознаёт текст кириллической моделью (`rec.onnx`)
     * и извлекает децимальный номер, литеру исполнения, наименование, код документа
     * и нумерацию листов по ГОСТ 2.104 (Формы 1, 2 и 2а).
     *
     * @details Общая схема работы класса:
     * 1. Конструктор один раз загружает det.onnx, rec.onnx и словарь символов.
     * 2. recognize() подготавливает изображение: читает скан, находит правый нижний
     *    угол штампа, вырезает и нормализует область основной надписи.
     * 3. detectTextBoxes() запускает DBNet (det.onnx): модель находит области,
     *    похожие на текст, но ещё не определяет сами символы.
     * 4. recognizeTextBoxes() вырезает каждую найденную область и запускает
     *    rec.onnx; decodeCtcOutput() превращает выход модели в строки.
     * 5. locateDecimalNumber() выбирает основной децимальный номер и извлекает
     *    код документа и литеру исполнения.
     * 6. detectStampForm() определяет форму штампа по положению номера.
     * 7. extractProductName() собирает наименование (для Формы 2а оно пропускается).
     * 8. extractSheetNumbers() ищет номер листа и общее количество листов:
     *    сначала по распознанным блокам, затем при необходимости напрямую
     *    распознаёт изображения соответствующих ячеек той же моделью rec.onnx.
     * 9. recognize() проверяет обязательные поля и возвращает shared::Document
     *    либо std::nullopt.
     *
     * Важно: детектор и распознаватель — две разные нейросети с разными задачами.
     * TextBox служит промежуточным результатом между ними: сначала в нём есть
     * координаты и оценка детектора, затем в поле text появляется распознанная строка.
     */
class OnnxStampRecognizer final : public shared::IStampRecognizer
{
private:
    shared::diagnostic::Logger& m_logger{ shared::diagnostic::Logger::instance() };

    cv::dnn::Net m_detNet;              ///< Нейросеть детектора текстовых областей (DBNet).
    // cv::dnn::Net m_clsNet;           ///< Нейросеть классификатора ориентации текста.
    cv::dnn::Net m_recNet;              ///< Нейросеть распознавания символов в текстовых строках (CRNN/SVTR).
    std::vector<std::string> m_keys;    ///< Словарь символов для декодирования CTC-выхода модели.
    bool m_initialized{ false };        ///< Флаг успешной загрузки моделей и словаря.
    std::filesystem::path m_debugDir{}; ///< Путь к папке для сохранения отладочных кропов (если пуст — отладка выключена).

public:
    /**
         * @brief Конструктор, инициализирующий ONNX-модели и словарь символов из указанной директории.
         * @param modelsDir Путь к каталогу с файлами `det.onnx`, `rec.onnx` и `cyrillic_dict.txt`.
         */
    explicit OnnxStampRecognizer(const std::filesystem::path& modelsDir);
    ~OnnxStampRecognizer() override = default;

    /**
         * @brief Распознаёт основную надпись (штамп) на скане конструкторского документа.
         *
         * @param imagePath Путь к файлу изображения чертежа или текстового документа.
         * @return Заполненная структура документа `shared::Document` или `std::nullopt`,
         *         если штамп не найден или не удалось извлечь обязательные реквизиты.
         *
         * @details Порядок выполнения внутри метода:
         * 1. Проверяется флаг m_initialized; без успешно загруженных моделей работа невозможна.
         * 2. prepareStampImage() возвращает PreparedStamp: BGR-кроп штампа и координаты
         *    его правого нижнего угла в координатах этого кропа.
         * 3. detectTextBoxes() возвращает найденные DBNet текстовые области; пустой список
         *    означает немедленный отказ от распознавания.
         * 4. recognizeTextBoxes() заполняет поле text каждого TextBox результатом rec.onnx.
         * 5. Создаётся shared::Document; sheet и totalSheets первоначально равны 1.
         * 6. locateDecimalNumber() находит основной номер; если он не найден, метод завершается.
         * 7. detectStampForm() классифицирует штамп как Form1, Form2 или Form2a.
         * 8. extractProductName() заполняет doc.name (для Form2a поле не требуется).
         * 9. extractSheetNumbers() уточняет doc.sheet и doc.totalSheets.
         * 10. Проверяются обязательные поля: номер обязателен всегда, наименование —
         *     для Form1 и Form2. После этого возвращается документ.
         *
         * Любое std::exception внутри основного процесса перехватывается: метод
         * записывает сообщение об ошибке и возвращает std::nullopt.
         */
    std::optional<shared::Document> recognize(const std::filesystem::path& imagePath) override;

    /**
         * @brief Включает сохранение отладочных изображений с визуализацией найденной рамки штампа.
         * @param debugDir Путь к каталогу для сохранения `.jpg` файлов (создаётся автоматически).
         */
    void setDebugOutputDir(const std::filesystem::path& debugDir);

private:
    /**
         * @brief Описывает обнаруженный текстовый блок в области штампа.
         */
    struct TextBox
    {
        std::vector<cv::Point2f> box;   ///< Четыре вершины ограничивающего прямоугольника (по часовой стрелке).
        float score{ 0.0f };            ///< Средняя уверенность детектора для данного контура.
        std::string text;               ///< Распознанный текст внутри блока.
    };

    /**
         * @brief Результат разбора строки с децимальным номером и кодом документа.
         */
    struct ParsedNumberInfo
    {
        std::string number;
        pdm::shared::DocumentTypeCode code{ pdm::shared::DocumentTypeCode::Unknown };
    };

    /**
         * @brief Тип формы основной надписи по ГОСТ 2.104.
         */
    enum class StampFormType
    {
        Form1,  ///< Чертежи и схемы (высота 55 мм, номер в верхней строке).
        Form2,  ///< Первый лист спецификации и текстовых документов (высота 40 мм).
        Form2a  ///< Последующие листы чертежей и текстовых документов (высота 15 мм, без наименования).
    };

    /**
         * @brief Подготовленное изображение штампа и координаты его правого нижнего угла в пространстве кропа.
         */
    struct PreparedStamp
    {
        cv::Mat bgrMat;                 ///< Нормализованный кроп штампа (1150x650, BGR).
        cv::Point2f bottomRightCorner;  ///< Координаты правого нижнего угла рамки внутри bgrMat.
    };

    /**
         * @brief Расширяет контур текстового блока наружу для компенсации сжатия при обучении DBNet.
         * @param box Исходные координаты вершин полигона.
         * @param unclipRatio Коэффициент расширения контура.
         * @return Вектор координат расширенного полигона.
         */
    static std::vector<cv::Point2f> unclipBox(const std::vector<cv::Point2f>& box, float unclipRatio);

    /**
         * @brief Упорядочивает 4 точки прямоугольника по часовой стрелке начиная с левого верхнего угла.
         * @param pts Неотсортированный набор из 4 точек.
         * @return Упорядоченный вектор точек: левый-верхний, правый-верхний, правый-нижний, левый-нижний.
         */
    static std::vector<cv::Point2f> orderPointsClockwise(std::vector<cv::Point2f> pts);

    /**
         * @brief Вырезает фрагмент изображения по ограничивающему прямоугольнику с защитными отступами.
         * @param src Исходное изображение штампа.
         * @param box Координаты вершин текстового блока.
         * @return Вырезанное изображение текстовой строки.
         */
    static cv::Mat getRotateCropImage(const cv::Mat& src, const std::vector<cv::Point2f>& box);

    /**
         * @brief Находит текстовые блоки на карте вероятностей, полученной от детектора.
         * @param probMap Карта вероятностей текста (одноканальная матрица `CV_32FC1`).
         * @param scaleX Коэффициент масштабирования координат по оси X к исходному кропу.
         * @param scaleY Коэффициент масштабирования координат по оси Y к исходному кропу.
         * @param boxThresh Порог бинаризации карты вероятностей.
         * @param boxScoreThresh Минимальный порог средней уверенности для фильтрации шума.
         * @param unclipRatio Коэффициент расширения найденных боксов.
         * @return Отсортированный сверху вниз и слева направо список текстовых блоков.
         */
    static std::vector<TextBox> findTextBoxes(const cv::Mat& probMap, float scaleX, float scaleY,
                                              float boxThresh = kDbBoxThresh, float boxScoreThresh = kDbBoxScoreThresh,
                                              float unclipRatio = kDbUnclipRatio);

    /**
         * @brief Заменяет все вхождения подстроки `from` на `to` в переданной строке.
         * @param s Изменяемая строка.
         * @param from Искомая подстрока.
         * @param to Подстрока замены.
         */
    static void replaceAll(std::string& s, std::string_view from, std::string_view to);

    /**
         * @brief Очищает распознанное наименование изделия от латинских двойников и типовых ошибок OCR.
         * @param s Сырая строка наименования.
         * @return Нормализованное кириллическое наименование.
         */
    static std::string cleanRussianName(std::string s);

    /**
         * @brief Извлекает децимальный номер, литеру исполнения и код документа из распознанной строки.
         * @param rawText Исходная строка из блока децимального номера.
         * @return Структура `ParsedNumberInfo` с очищенным номером и кодом документа.
         */
    static ParsedNumberInfo parseDecimalAndCode(const std::string& rawText);

    /**
         * @brief Выполняет распознавание текста на произвольном вырезанном фрагменте изображения.
         * @param recNet Инициализированная нейросеть распознавания текста.
         * @param crop Вырезанное изображение строки или ячейки таблицы.
         * @param keys Словарь символов для CTC-декодирования.
         * @return Распознанная строка текста.
         */
    static std::string recognizeCrop(cv::dnn::Net& recNet, const cv::Mat& crop, const std::vector<std::string>& keys);

    /**
         * @brief Извлекает целочисленный номер листа или общее количество листов из строки ячейки.
         * @param raw Распознанный текст ячейки «Лист» или «Листов».
         * @param isForm2aCell Признак узкой ячейки Формы 2а для фильтрации вертикальных линий рамки.
         * @return Распознанное число больше нуля или `0`, если значение невалидно.
         */
    static int parseSheetNumber(const std::string& raw, bool isForm2aCell = false);

    /**
         * @brief Проверяет, является ли строка служебной надписью штампа или рамки, не относящейся к наименованию.
         * @param s Проверяемая строка текста.
         * @return `true`, если строка содержит стоп-слово и должна быть проигнорирована.
         */
    static bool isStopWord(const std::string& s);

    /**
         * @brief Определяет код документа (СБ, ТУ и др.) по текстовой строке, если он ещё не установлен.
         * @param text Строка для анализа.
         * @param code Ссылка на поле кода документа для обновления.
         */
    static void detectDocumentCode(const std::string& text, pdm::shared::DocumentTypeCode& code);

    /**
         * @brief Загружает изображение чертежа, вырезает область штампа и конвертирует её в BGR-матрицу OpenCV.
         * @param imagePath Путь к файлу изображения.
         * @return 3-канальная матрица `cv::Mat` (формат `CV_8UC3`, BGR) или пустая матрица при ошибке данных.
         */
    PreparedStamp prepareStampImage(const std::filesystem::path& imagePath);

    /**
         * @brief Запускает нейросеть-детектор (DBNet) на подготовленном штампе и возвращает координаты текстовых блоков.
         * @param bgrMat Изображение штампа в формате BGR (`CV_8UC3`).
         * @return Вектор найденных текстовых боксов в координатах исходного `bgrMat` или пустой вектор при ошибке.
         */
    std::vector<TextBox> detectTextBoxes(const cv::Mat& bgrMat);

    /**
         * @brief Преобразует отдельный контур с карты вероятностей в текстовый блок, если он проходит пороги качества.
         * @param contour Замкнутый контур, найденный на бинарной маске.
         * @param probMap Исходная карта вероятностей текста (`CV_32FC1`).
         * @param scaleX Коэффициент масштабирования по оси X.
         * @param scaleY Коэффициент масштабирования по оси Y.
         * @param boxScoreThresh Минимальная средняя уверенность внутри контура.
         * @param unclipRatio Коэффициент расширения контура DBNet.
         * @return Готовый `TextBox` в координатах штампа или `std::nullopt`, если контур отфильтрован как шум.
         */
    static std::optional<TextBox> contourToTextBox(const std::vector<cv::Point>& contour,
                                                   const cv::Mat& probMap,
                                                   float scaleX, float scaleY,
                                                   float boxScoreThresh, float unclipRatio);

    /**
         * @brief Распознаёт текст внутри каждого обнаруженного текстового блока штампа.
         *
         * @details Для каждого бокса вырезает фрагмент строки с защитными отступами,
         * выполняет инференс нейросети распознавания и записывает результат в поле `text`.
         *
         * @param bgrMat Подготовленное изображение штампа в формате BGR (`CV_8UC3`).
         * @param boxes Вектор текстовых блоков с координатами рамок для заполнения распознанным текстом.
         */
    void recognizeTextBoxes(const cv::Mat& bgrMat, std::vector<TextBox>& boxes);

    /**
         * @brief Масштабирует фрагмент текстовой строки с сохранением пропорций и формирует входной 4D-тензор для `rec.onnx`.
         * @param crop Вырезанное изображение текстовой строки в формате BGR (`CV_8UC3`).
         * @return 4D-тензор в формате NCHW (`1x3x48x640`) с нормализацией в диапазон `[-1.0..1.0]`.
         */
    static cv::Mat prepareRecBlob(const cv::Mat& crop);

    /**
         * @brief Выполняет жадное CTC-декодирование выходного тензора нейросети распознавания в строку текста.
         * @param recOut Выходной 3D-тензор модели `rec.onnx` размерности `[1, SeqLen, NumClasses]`.
         * @param keys Словарь символов, где индекс `0` соответствует служебному blank-токену.
         * @return Декодированная строка текста.
         */
    static std::string decodeCtcOutput(const cv::Mat& recOut, const std::vector<std::string>& keys);

    /**
         * @brief Находит основной децимальный номер документа и заполняет поля `number` и `code` в `doc`.
         *
         * @details Если на скане обнаружено несколько обозначений (например, графа «Первичное применение»
         * вверху рамки и основная надпись внизу), выбирается самый нижний блок с наибольшей координатой Y.
         *
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param corner Координаты правого нижнего угла рамки штампа.
         * @param doc Заполняемая структура документа.
         * @return Координаты левого верхнего угла блока номера (`cv::Point2f`) или `std::nullopt`, если номер не найден.
         */
    std::optional<cv::Point2f> locateDecimalNumber(const std::vector<TextBox>& boxes,
                                                          const cv::Point2f& corner,
                                                          shared::Document& doc);

    /**
         * @brief Определяет форму основной надписи по положению децимального номера относительно нижнего угла рамки.
         * @param numY Координата Y верхней границы блока децимального номера.
         * @param cornerY Координата Y правого нижнего угла рамки штампа.
         * @return Определённая форма основной надписи: Form1, Form2 или Form2a.
         */
    static StampFormType detectStampForm(float numY, float cornerY);

    /**
         * @brief Извлекает и склеивает многострочное наименование изделия под децимальным номером.
         *
         * @details Попутно уточняет код документа (СБ, ТУ), если он встречается в графе наименования.
         * Для узких штампов последующих листов (Форма 2а) оставляет поле `doc.name` пустым.
         *
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param numPos Опорные координаты левого верхнего угла децимального номера.
         * @param corner Координаты правого нижнего угла рамки штампа.
         * @param formType Определённая форма основной надписи.
         * @param doc Заполняемая структура документа.
         */
    static void extractProductName(const std::vector<TextBox>& boxes,
                                   const cv::Point2f& numPos,
                                   const cv::Point2f& corner,
                                   StampFormType formType,
                                   shared::Document& doc);

    /**
         * @brief Проверяет наличие одного из ключевых слов в строке и извлекает число из её остатка.
         *
         * @details Используется для поиска заголовков граф «Лист» и «Листов» (с учётом типовых
         * OCR-искажений) и одновременного извлечения цифры, если детектор объединил слово
         * заголовка и значение в один текстовый блок (например, `"Листов 1"`).
         *
         * @param text Распознанный текст проверяемого блока.
         * @param keywords Набор допустимых вариантов написания ключевого слова.
         * @param outTrailingNumber Выходной параметр для числа, извлечённого после ключевого слова (или `0`, если числа нет).
         * @return `true`, если одно из ключевых слов найдено в строке; иначе `false`.
         */
    static bool matchKeywordAndExtractTail(const std::string& text,
                                           std::span<const std::string_view> keywords,
                                           int& outTrailingNumber);

    /**
         * @brief Определяет номер текущего листа и общее количество листов документа.
         *
         * @details Ищет заголовки «Лист» и «Листов» правее децимального номера, считывает
         * значения снизу (для Формы 2 — спецификаций) или справа (для Формы 1 — чертежей).
         * При необходимости выполняет прямое дораспознавание ячеек через `m_recNet`.
         *
         * @param bgrMat Подготовленное изображение штампа в формате BGR (`CV_8UC3`).
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param corner Координаты правого нижнего угла рамки штампа.
         * @param formType Определённая форма основной надписи.
         * @param doc Заполняемая структура документа (обновляет поля `sheet` и `totalSheets`).
         */
    void extractSheetNumbers(const cv::Mat& bgrMat,
                             const std::vector<TextBox>& boxes,
                             const cv::Point2f& corner,
                             StampFormType formType,
                             shared::Document& doc);

    /**
         * @brief Выполняет прямое распознавание ячеек листа и общего количества листов по координатам штампа.
         * @param bgrMat Подготовленное изображение штампа в формате BGR (`CV_8UC3`).
         * @param corner Координаты правого нижнего угла рамки штампа.
         * @param formType Определённая форма основной надписи.
         * @param sheetHeaderPos Координаты заголовка «Лист» или невалидная точка, если он не найден.
         * @param totalHeaderPos Координаты заголовка «Листов» или невалидная точка, если он не найден.
         * @param sheetFound Признак того, что номер листа уже найден.
         * @param totalFound Признак того, что общее количество листов уже найдено.
         * @param doc Заполняемая структура документа.
         */
    void recognizeSheetNumbersInCells(const cv::Mat& bgrMat,
                                      const cv::Point2f& corner,
                                      StampFormType formType,
                                      const cv::Point2f& sheetHeaderPos,
                                      const cv::Point2f& totalHeaderPos,
                                      bool sheetFound,
                                      bool totalFound,
                                      shared::Document& doc);

    /**
         * @brief Вырезает заданную прямоугольную область штампа и распознаёт в ней число.
         * @param bgrMat Исходное изображение штампа в формате BGR (`CV_8UC3`).
         * @param roi Прямоугольник области интереса (может выходить за края кадра — обрезается безопасно).
         * @param minSizePx Минимально допустимая ширина и высота вырезанной области в пикселях.
         * @param isForm2aCell Признак узкой ячейки Формы 2а для фильтрации линий рамки.
         * @return Распознанное целое число больше нуля или `0`, если область пуста или не содержит цифр.
         */
    int recognizeRegionNumber(const cv::Mat& bgrMat,
                              const cv::Rect& roi,
                              int minSizePx,
                              bool isForm2aCell);


    /**
         * @brief Находит координаты заголовков «Лист» и «Листов» и проверяет слитные с ними числа.
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param numPos Опорные координаты левого верхнего угла децимального номера.
         * @param sheetHeaderPos Выходные координаты заголовка «Лист».
         * @param totalHeaderPos Выходные координаты заголовка «Листов».
         * @param sheetFound Выходной флаг, что номер текущего листа уже найден.
         * @param totalFound Выходной флаг, что общее количество листов уже найдено.
         * @param doc Заполняемая структура документа.
         */
    static void findSheetHeaders(const std::vector<TextBox>& boxes,
                                 const cv::Point2f& numPos,
                                 cv::Point2f& sheetHeaderPos,
                                 cv::Point2f& totalHeaderPos,
                                 bool& sheetFound,
                                 bool& totalFound,
                                 shared::Document& doc);

    /**
         * @brief Ищет значения «Лист» и «Листов» в соседних распознанных боксах (снизу или справа от заголовков).
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param sheetHeaderPos Координаты заголовка «Лист».
         * @param totalHeaderPos Координаты заголовка «Листов».
         * @param isForm2a Признак последующего листа (Форма 2а).
         * @param sheetFound Флаг наличия номера текущего листа (обновляется при находке).
         * @param totalFound Флаг наличия общего количества листов (обновляется при находке).
         * @param doc Заполняемая структура документа.
         */
    static void findSheetValuesInBoxes(const std::vector<TextBox>& boxes,
                                       const cv::Point2f& sheetHeaderPos,
                                       const cv::Point2f& totalHeaderPos,
                                       bool isForm2a,
                                       bool& sheetFound,
                                       bool& totalFound,
                                       shared::Document& doc);

    /**
         * @brief Нормализует разделители и исправляет OCR-искажения цифр в цифровой части децимального номера.
         * @param rawText Исходная строка блока децимального номера.
         * @return Строка с исправленными точками и цифрами до конца 5-й группы включительно.
         */
    static std::string fixDecimalDigitArtifacts(const std::string& rawText);

    /**
         * @brief Извлекает и удаляет код документа (СБ, ТУ и др.) с конца хвостовой части обозначения.
         * @param tail Изменяемый остаток строки после 12-значного децимального номера.
         * @return Определённый код документа или `DocumentTypeCode::Unknown`.
         */
    static pdm::shared::DocumentTypeCode extractTrailingDocCode(std::string& tail);

    /**
         * @brief Ищет числовое значение в ячейке снизу (Форма 2) или справа (Форма 1) от указанного заголовка.
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param headerPos Координаты левого верхнего угла блока заголовка («Лист» или «Листов»).
         * @param minRightOffsetX Минимальный отступ вправо от заголовка для поиска в той же строке (Форма 1).
         * @param maxRightOffsetX Максимальный отступ вправо от заголовка для поиска в той же строке (Форма 1).
         * @param isForm2aCell Признак узкой ячейки Формы 2а для фильтрации линий рамки.
         * @param maxValidValue Верхний допустимый предел распознанного числа (исключительно).
         * @return Распознанное целое число в диапазоне `(0, maxValidValue)` или `std::nullopt`, если ячейка не найдена.
         */
    static std::optional<int> findValueNearHeader(const std::vector<TextBox>& boxes,
                                                  const cv::Point2f& headerPos,
                                                  float minRightOffsetX,
                                                  float maxRightOffsetX,
                                                  bool isForm2aCell,
                                                  int maxValidValue);

    /**
         * @brief Удаляет указанные символы с начала и конца переданной строки на месте.
         * @param s Изменяемая строка.
         * @param chars Набор символов для удаления (по умолчанию — пробелы).
         */
    static void trimInPlace(std::string& s, std::string_view chars = " ");

    /**
         * @brief Нормализует литеру исполнения децимального номера (латинские двойники и строчную кириллицу в заглавную кириллицу).
         * @param rawSuffix Хвостовая строка после отсечения номера и кода документа.
         * @return Заглавная кириллическая буква в UTF-8 или пустая строка, если символ не является валидной литерой.
         */
    static std::string normalizeExecutionSuffix(std::string_view rawSuffix);

    /**
         * @brief Находит координаты правого нижнего угла рамки основной надписи на полном скане.
         *
         * @details Вырезает правую нижнюю область чертежа, выделяет длинные горизонтальные и
         * вертикальные линии морфологическим открытием и находит пересечение самой нижней
         * и самой правой линий рамки по гистограммам проекций.
         *
         * @param fullGray Полное изображение чертежа в градациях серого (`CV_8UC1`).
         * @return Координаты `(x, y)` правого нижнего угла штампа на полном изображении
         *         или `std::nullopt`, если линии рамки не обнаружены.
         */
    std::optional<cv::Point> findStampBottomRightCorner(const cv::Mat& fullGray, [[maybe_unused]] const std::string& debugFilename = "");

private:
    // Параметры морфологического поиска правого нижнего угла рамки штампа
    constexpr static int kSearchZoneWidthPx{ 1600 };        ///< Ширина области поиска в правом нижнем углу листа.
    constexpr static int kSearchZoneHeightPx{ 1000 };       ///< Высота области поиска в правом нижнем углу листа.
    constexpr static int kCornerPadRightPx{ 45 };           ///< Запас вправо от найденного угла штампа при вырезке.
    constexpr static int kCornerPadBottomPx{ 135 };          ///< Запас вниз от найденного угла (чтобы захватить строку «Формат»).

    // Параметры постобработки детектора DBNet (стандартные значения PaddleOCR / RapidOCR)
    constexpr static float kDbBoxThresh{ 0.3f };           ///< Порог бинаризации карты вероятностей (0.0..1.0).
    constexpr static float kDbBoxScoreThresh{ 0.5f };      ///< Минимальная средняя уверенность контура для отсева шума.
    constexpr static float kDbUnclipRatio{ 1.65f };         ///< Коэффициент расширения сжатого контура DBNet наружу.
    constexpr static float kMinRawBoxSizePx{ 3.0f };       ///< Минимальная сторона контура до расширения (в пикселях 1024x1024).
    constexpr static float kMinUnclippedBoxSizePx{ 5.0f }; ///< Минимальная сторона рамки после расширения.
    constexpr static float kSameLineThresholdPx{ 12.0f };  ///< Допуск по оси Y для сортировки слов в пределах одной строки.
    constexpr static size_t kMinContourPoints{ 4 };        ///< Минимум 4 вершины для образования прямоугольной рамки текста.

    // Параметры предобработки и тензора для модели распознавания текста PP-OCRv5 (rec.onnx)
    constexpr static int kRecInputHeight{ 48 };            ///< Фиксированная высота входного тензора (1x3x48x640).
    constexpr static int kRecMaxWidth{ 640 };              ///< Максимальная ширина входного тензора (1x3x48x640).
    constexpr static int kRecMinWidth{ 8 };                ///< Минимальная ширина строки после масштабирования (защита узких цифр).
    constexpr static unsigned char kRecPadGrayValue{ 128 };///< Нейтральный серый цвет (128 -> ~0.0 после нормализации) для правого поля.
    static inline const cv::Scalar kRecNormMean{ 0.5, 0.5, 0.5 }; ///< Среднее значение для симметричной нормализации [-1..1] в PP-OCRv5.
    static inline const cv::Scalar kRecNormStd{ 0.5, 0.5, 0.5 };  ///< Стандартное отклонение для симметричной нормализации в PP-OCRv5.
    constexpr static int kRecOutputMinDims{ 3 };           ///< Ожидаемое число измерений выходного тензора [Batch, SeqLen, NumClasses].
    constexpr static int kCtcBlankIndex{ 0 };              ///< Индекс служебного blank-токена ("#") в словаре CTC.

    // Геометрия вырезки области основной надписи (правый нижний угол чертежа)
    constexpr static int kOffsetX{ 5 };
    constexpr static int kOffsetY{ 5 };
    constexpr static int kCropW{ 1150 };
    constexpr static int kCropH{ 650 };

    // Параметры адаптивного выравнивания контраста (CLAHE) для тусклых сканов
    constexpr static double kClaheClipLimit{ 2.0 };
    constexpr static int kClaheGridSize{ 8 };

    // Фиксированные размеры входных тензоров ONNX-моделей (PaddleOCR)
    constexpr static int kDetInputWidth{ 1024 };   ///< Ширина входа детектора det.onnx (1x3x1024x1024).
    constexpr static int kDetInputHeight{ 1024 };  ///< Высота входа детектора det.onnx (1x3x1024x1024).

    // Стандартные коэффициенты нормализации ImageNet (R, G, B), заданные в конфиге
    // оператора NormalizeImage для детектора PP-OCRv4 (PaddleOCR / RapidOCR)
    constexpr static double kPixelScale{ 1.0 / 255.0 };
    static inline const cv::Scalar kImageNetMean{ 0.485, 0.456, 0.406 };
    static inline const cv::Scalar kImageNetStd{ 0.229, 0.224, 0.225 };

    // Защитные отступы (в пикселях) при вырезке текстовой строки перед распознаванием:
    // большой отступ слева компенсирует тесную обрезку первой заглавной буквы ("Б", "О", "Щ") детектором DBNet
    constexpr static int kCropPadLeftPx{ 10 };   ///< Запас слева от найденного бокса (в пикселях).
    constexpr static int kCropPadRightPx{ 16 };  ///< Запас справа от найденного бокса (в пикселях).
    constexpr static int kCropPadTopPx{ 4 };     ///< Запас сверху над строкой (в пикселях).
    constexpr static int kCropPadBottomPx{ 4 };  ///< Запас снизу под строкой (в пикселях).

    // Геометрические пороги поиска многострочного наименования изделия относительно децимального номера (ГОСТ 2.104)
    constexpr static float kNameSearchTopOffsetPx{ 25.0f };        ///< Минимальный отступ вниз от Y номера (чтобы не захватить сам номер).
    constexpr static size_t kMinRawLineLength{ 2 };                ///< Минимальная длина сырого текстового блока для анализа.
    constexpr static size_t kMinCleanedNameBytes{ 4 };             ///< Минимум 4 байта в UTF-8 (2 кириллических символа, например «Ось»).

    // Геометрические пороги поиска заголовков граф «Лист» и «Листов» относительно децимального номера
    constexpr static float kUnsetCoord{ -1.0f };                   ///< Маркер ненайденной координаты X или Y.
    constexpr static float kSheetHeaderMinOffsetX{ 250.0f };       ///< Минимальный сдвиг вправо от номера до граф «Лист»/«Листов».
    constexpr static float kSheetHeaderTopToleranceY{ 30.0f };     ///< Допуск вверх от Y номера на случай перекоса скана.
    constexpr static int kMaxReasonableTotalSheets{ 25 };          ///< Верхний предел количества листов для отсечения артефактов OCR.
    constexpr static int kMaxReasonableSheetNumber{ 100 };         ///< Верхний предел номера текущего листа.

    // Геометрические пороги поиска цифр в соседних боксах относительно заголовков «Лист» и «Листов»
    constexpr static float kCellBelowMaxOffsetX{ 60.0f };          ///< Макс. смещение по X при поиске цифры СНИЗУ (Форма 2).
    constexpr static float kCellBelowMinOffsetY{ 12.0f };          ///< Мин. отступ вниз от заголовка до цифры (Форма 2).
    constexpr static float kCellBelowMaxOffsetY{ 85.0f };          ///< Макс. отступ вниз от заголовка до цифры (Форма 2).
    constexpr static float kCellRightMaxOffsetY{ 22.0f };          ///< Макс. отклонение по Y в той же строке (Форма 1).
    constexpr static float kSheetRightMinOffsetX{ 35.0f };         ///< Мин. отступ вправо от «Лист» до цифры (Форма 1).
    constexpr static float kSheetRightMaxOffsetX{ 140.0f };        ///< Макс. отступ вправо от «Лист» до цифры (Форма 1).
    constexpr static float kTotalRightMinOffsetX{ 45.0f };         ///< Мин. отступ вправо от «Листов» до цифры (Форма 1).
    constexpr static float kTotalRightMaxOffsetX{ 160.0f };        ///< Макс. отступ вправо от «Листов» до цифры (Форма 1).

    // Параметры прямого кропа ячеек через m_recNet, если DBNet пропустил мелкую цифру
    constexpr static int kDirectTotalRightShiftX{ 82 };            ///< Сдвиг вправо от «Листов» до ячейки цифры (Форма 1).
    constexpr static int kDirectRightTopPadY{ 2 };                 ///< Запас вверх от Y заголовка для захвата цифры.
    constexpr static int kDirectRightCropW{ 36 };                  ///< Ширина области прямого захвата справа от заголовка.
    constexpr static int kDirectRightCropH{ 30 };                  ///< Высота области прямого захвата справа от заголовка.
    constexpr static int kDirectBelowLeftPadX{ 6 };                ///< Запас влево от X заголовка при захвате снизу (Форма 2).
    constexpr static int kDirectBelowShiftY{ 26 };                 ///< Сдвиг вниз от заголовка «Лист» до ячейки цифры (Форма 2).
    constexpr static int kDirectBelowCropW{ 90 };                  ///< Ширина области прямого захвата снизу.
    constexpr static int kDirectBelowCropH{ 38 };                  ///< Высота области прямого захвата снизу.

    // Параметры захвата ячейки «Лист» в правом углу Формы 2а
    constexpr static int kMinValidCropSizePx{ 10 };                ///< Минимальный размер вырезаемой ячейки в пикселях.
    constexpr static int kMinForm2aCropSizePx{ 15 };               ///< Минимальный размер ячейки Формы 2а в пикселях.
    constexpr static int kMinFallbackForm2aSheet{ 2 };             ///< Минимальный номер листа по умолчанию для Формы 2а.

    // Геометрические пороги поиска полей относительно правого нижнего угла рамки штампа (corner)
    // В нормализованном кропе 1150x650 (с kCropExpansion = 1.3) масштаб составляет ~4.53 px/мм по ГОСТ 2.104:
    // полная ширина штампа (185 мм) ≈ 840 px, высота Формы 1 (55 мм) ≈ 250 px, Формы 2 (40 мм) ≈ 180 px, Формы 2а (15 мм) ≈ 70 px.
    // Геометрические пороги поиска полей относительно правого нижнего угла рамки штампа (corner)
    constexpr static float kStampTotalWidthPx{ 1100.0f };         ///< Полная ширина штампа 185 мм влево от corner.x (захватывает x=66 на Data005).
    constexpr static float kStampMaxHeightPx{ 560.0f };           ///< Максимальная высота штампа вверх от нижнего угла.
    constexpr static float kStampRightColsWidthPx{ 550.0f };      ///< Ширина правой колонки 65 мм (отсекает Лит x=696 и АО 192 ЦЗЖТ x=653).
    constexpr static float kForm2aRightColWidthPx{ 115.0f };      ///< Ширина ячейки «Лист» 10 мм справа на Форме 2а.
    constexpr static float kStampBottomEdgeTolPx{ 65.0f };        ///< Отступ от нижней линии рамки (отсекает «Копировал / Формат»).

    // Геометрические пороги поиска графы «Наименование» (Графа 1 по ГОСТ 2.104)
    // Привязаны к децимальному номеру (numPos), поэтому автоматически следуют за сдвигами штампа на 10–20 мм:
    constexpr static float kNameLeftFromNumPx{ 305.0f };          ///< Макс. сдвиг влево от X номера (отсекает колонку «Дата» при любом сдвиге штампа).
    constexpr static float kNameMaxHeightFromNumPx{ 265.0f };     ///< Макс. высота от Y номера вниз до графы «Материал» (отсекает ГОСТы, сталь и «Лист»).

    // Пороги расстояния от Y децимального номера до нижней линии рамки (corner.y - numPos.y)
    constexpr static float kForm2aMaxDistFromBottomPx{ 210.0f };  ///< Форма 2а (высота 15 мм, dist ≈ 160..200 px).
    constexpr static float kForm2MaxDistFromBottomPx{ 390.0f };   ///< Форма 2 (высота 40 мм, dist ≈ 297..367 px).

    // Прямые ROI-области ячеек «Лист» и «Листов» относительно правого нижнего угла рамки (corner)
    constexpr static int kForm2aSheetCellOffsetX{ 95 };           ///< Отступ влево от corner.x до ячейки «Лист» на Форме 2а (графа 10 мм).
    constexpr static int kForm2aSheetCellOffsetY{ 65 };           ///< Отступ вверх от corner.y до ячейки цифры «Лист» на Форме 2а.
    constexpr static int kForm2aSheetCellW{ 90 };                 ///< Ширина прямого кропа ячейки «Лист» на Форме 2а.
    constexpr static int kForm2aSheetCellH{ 60 };                 ///< Высота прямого кропа ячейки «Лист» на Форме 2а.

    constexpr static int kForm12TotalCellOffsetX{ 140 };          ///< Отступ влево от corner.x до ячейки «Листов» (Форма 1 и 2).
    constexpr static int kForm2TotalCellOffsetY{ 150 };           ///< Отступ вверх от corner.y до ячейки цифры «Листов» (Форма 2).
    constexpr static int kForm12CellCropW{ 115 };                 ///< Ширина прямого кропа ячейки «Лист»/«Листов» (Форма 1 и 2).
    constexpr static int kForm12CellCropH{ 55 };                  ///< Высота прямого кропа ячейки «Лист»/«Листов» (Форма 1 и 2).

    // Варианты распознавания заголовка графы «Листов» (проверяются первыми, так как включают подстроку «Лист»)
    static constexpr std::array<std::string_view, 18> kTotalSheetsKeywords{
        "Листов", "Листоб", "Лucmob", "Лucmoб", "Листо8", "Листо", "Лисmов",
        "Пустоб", "ПистоВ", "Пистов", "Шucmoб", "ucino", "ucmo", "ucmm", "cmm",
        "Jiucmo", "lucmob", "Лустоб"
    };

    // Варианты распознавания заголовка графы «Лист»
    static constexpr std::array<std::string_view, 13> kCurrentSheetKeywords{
        "Лист", "Лиcт", "Лucm", "llucm", "ucm", "ucт", "ucп", "Шucm", "VZUL",
        "lucm", "Jucm", "Llucm", "ЛиЕТ"
    };

    // Параметры разбора децимального номера по ЕСКД (формат XXX.XX.XX.XX.XXX)
    constexpr static ptrdiff_t kMinDecimalDots{ 3 };       ///< Минимум 3 точки для признания строки кандидатом в номер.
    constexpr static size_t kLastDecimalDotIndex{ 4 };     ///< Номер 4-й точки, после которой идут последние 3 цифры.
    constexpr static size_t kLastGroupDigitsCount{ 3 };    ///< Количество цифр в последней группе децимального номера.
    constexpr static size_t kStandardDecimalDigits{ 12 };  ///< Стандартное количество цифр в децимальном номере ЕСКД.
    constexpr static size_t kUtf8CyrillicCharBytes{ 2 };   ///< Размер одной кириллической буквы в кодировке UTF-8.

    // Соответствие хвостов номера кодам конструкторских документов (включая типовые ошибки OCR)
    constexpr static std::array<std::pair<std::string_view, pdm::shared::DocumentTypeCode>, 11> kTrailingDocCodes{ {
        { "СБ", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "CB", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "C5", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "С5", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "С6", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "C6", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "C8", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "С8", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "05", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "ТУ", pdm::shared::DocumentTypeCode::TechnicalSpecifications },
        { "TY", pdm::shared::DocumentTypeCode::TechnicalSpecifications }
    } };

    // Полнотекстовые маркеры кода документа, встречающиеся в графе наименования изделия
    constexpr static std::array<std::pair<std::string_view, pdm::shared::DocumentTypeCode>, 2> kNameDocCodeKeywords{ {
        { "Сборочный", pdm::shared::DocumentTypeCode::AssemblyDrawing },
        { "Технические условия", pdm::shared::DocumentTypeCode::TechnicalSpecifications }
    } };

    // Таблица замены латинских двойников на кириллицу в наименованиях изделий (здесь 'B' -> 'В')
    constexpr static auto kNameLatinToCyrillic = std::to_array<std::pair<std::string_view, std::string_view>>({
        { "A", "А" }, { "B", "В" }, { "C", "С" }, { "E", "Е" }, { "H", "Н" },
        { "K", "К" }, { "M", "М" }, { "O", "О" }, { "P", "Р" }, { "T", "Т" }, { "X", "Х" }, { "W", "Щ" },
        { "a", "а" }, { "b", "в" }, { "c", "с" }, { "e", "е" }, { "k", "к" },
        { "m", "т" }, { "n", "п" }, { "o", "о" }, { "p", "р" }, { "u", "и" }, { "x", "х" }, { "y", "у" }, { "w", "щ" }
    });

    // Типовые словарные искажения OCR в наименованиях конструкторских документов
    constexpr static auto kNameWordCorrections = std::to_array<std::pair<std::string_view, std::string_view>>({
        { "Ьалка",          "Балка" },
        { "ьалка",          "балка" },
        { "дереговая",      "береговая" },
        { "Берхн",          "Верхн" },
        { "берхн",          "верхн" },
        { "8ze3d",          "Въезд" },
        { "8ze3д",          "Въезд" },
        { "8zе3d",          "Въезд" },
        { "8zе3д",          "Въезд" },
        { "Вze3d",          "Въезд" },
        { "Вze3д",          "Въезд" },
        { "Вzе3d",          "Въезд" },
        { "Вzе3д",          "Въезд" },
        { "Въе3д",          "Въезд" },
        { "Деталв",         "Деталь" },
        { "3yδ",            "Зуб" },
        { "3yб",            "Зуб" },
        { "3уδ",            "Зуб" },
        { "3уб",            "Зуб" },
        { "инбентариый",    "инвентарный" },
        { "инбентарный",    "инвентарный" },
        { "инвентариый",    "инвентарный" },
        { "иньснтарный",    "инвентарный" },
        { "Колесоотдой",    "Колесоотбой"},
        { "колесоотдой",    "колесоотбой"},
        { "компенсацижнная",  "компенсационная" },
        { "компенсацижнный",  "компенсационный" },
        { "компенсацижнный",  "компенсационный" },
        { "Коиплект",       "Комплект" },
        { "краинего",       "крайнего" },
        { "краинии",        "крайний" },
        { "Aист",           "Лист" },
        { "aист",           "Лист" },
        { "Аист",           "Лист" },
        { "Лyст",           "Лист" },
        { "Луст",           "Лист" },
        { "аист",           "Лист" },
        { "лyст",           "Лист" },
        { "луст",           "Лист" },
        { "Льжа",           "Лыжа" },
        { "льжа",           "лыжа" },
        { "Настия",         "Настил" },
        { "настия",         "настил" },
        { "нижсн",          "нижн" },
        { "нижн яя",        "нижняя" },
        { "нижсн яя",       "нижняя" },
        { "Оббязка",        "Обвязка" },
        { "Одвязка",        "Обвязка" },
        { "оббязка",        "обвязка" },
        { "одвязка",        "обвязка" },
        { "Обйивка",        "Обшивка" },
        { "Одшивка",        "Обшивка" },
        { "обйивка",        "обшивка" },
        { "одшивка",        "обшивка" },
        { "Опорабереговая", "Опора береговая" },
        { "Осв",            "Ось" },
        { "Палудныи",       "Палубный" },
        { "Палудный",       "Палубный" },
        { "палудныи",       "палубный" },
        { "палудный",       "палубный" },
        { "палудный",       "палубный" },
        { "Нетли",          "Петли" },
        { "нетли",          "петли" },
        { "Нетля",          "Петля" },
        { "нетля",          "петля" },
        { "Реδр",           "Ребро" },
        { "Редро",          "Ребро" },
        { "реδр",           "Ребро" },
        { "редро",          "ребро" },
        { "Сбороиной",      "Сборочный" },
        { "Сдорочный",      "Сборочный" },
        { "Соорочной",      "Сборочный" },
        { "Стенкd",         "Стенка" },
        { "Стенкд",         "Стенка" },
        { "стенкd",         "стенка" },
        { "стенкд",         "стенка" },
        { "tтоика",         "Стойка" },
        { "tтойка",         "Стойка" },
        { "Стоика",         "Стойка" },
        { "Стоика",         "Стойка" },
    });
};

} // namespace pdm::infrastructure

#endif // ONNX_STAMP_RECOGNIZER_H
