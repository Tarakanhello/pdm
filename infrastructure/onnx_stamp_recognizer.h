#ifndef ONNX_STAMP_RECOGNIZER_H
#define ONNX_STAMP_RECOGNIZER_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <opencv2/dnn.hpp>

#include "istamp_recognizer.h"

namespace pdm::infrastructure
{

    /**
     * @brief Распознаватель штампа ЕСКД на базе моделей PaddleOCR (ONNX) через OpenCV DNN.
     *
     * @details Выполняет локализацию текстовых блоков в области основной надписи с помощью
     * детектора DBNet (`det.onnx`), распознаёт текст кириллической моделью (`rec.onnx`)
     * и извлекает децимальный номер, литеру исполнения, наименование, код документа
     * и нумерацию листов по ГОСТ 2.104 (Формы 1, 2 и 2а).
     */
    class OnnxStampRecognizer final : public shared::IStampRecognizer
    {
    private:
        cv::dnn::Net m_detNet;              ///< Нейросеть детектора текстовых областей (DBNet).
        // cv::dnn::Net m_clsNet;              ///< Нейросеть классификатора ориентации текста.
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
         * @brief Преобразует латинский символ-двойник буквы исполнения номера в кириллицу.
         * @param ch Латинский символ (например, `'A'`, `'B'`, `'C'`).
         * @return Строка с соответствующей кириллической буквой в UTF-8 или пустая строка.
         */
        static std::string normalizeSuffixLetter(char ch);

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
        cv::Mat prepareStampImage(const std::filesystem::path& imagePath);

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
         * @param doc Заполняемая структура документа.
         * @return Координаты левого верхнего угла блока номера (`cv::Point2f`) или `std::nullopt`, если номер не найден.
         */
        static std::optional<cv::Point2f> locateDecimalNumber(const std::vector<TextBox>& boxes,
                                                              shared::Document& doc);

        /**
         * @brief Извлекает и склеивает многострочное наименование изделия под децимальным номером.
         *
         * @details Попутно уточняет код документа (СБ, ТУ), если он встречается в графе наименования.
         * Для узких штампов последующих листов (Форма 2а) оставляет поле `doc.name` пустым.
         *
         * @param boxes Список распознанных текстовых блоков штампа.
         * @param numPos Опорные координаты левого верхнего угла децимального номера.
         * @param doc Заполняемая структура документа.
         */
        static void extractProductName(const std::vector<TextBox>& boxes,
                                       const cv::Point2f& numPos,
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
         * @param numPos Опорные координаты левого верхнего угла децимального номера.
         * @param isForm2a Признак последующего листа (Форма 2а, без наименования изделия).
         * @param doc Заполняемая структура документа (обновляет поля `sheet` и `totalSheets`).
         */
        void extractSheetNumbers(const cv::Mat& bgrMat,
                                 const std::vector<TextBox>& boxes,
                                 const cv::Point2f& numPos,
                                 bool isForm2a,
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
         * @param isForm2a Признак последующего листа (Форма 2а).
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
        static std::optional<cv::Point> findStampBottomRightCorner(const cv::Mat& fullGray);

        /**
         * @brief Находит пики линий на 1D-проекции (гистограмме) с подавлением соседних пикселей одной линии.
         * @param projection 1D-матрица сумм яркости вдоль строк или столбцов (`CV_32S`).
         * @param minScore Минимальный порог суммарной яркости для признания линии валидной.
         * @param ignoreMargin Отступ от границ зоны поиска (в пикселях).
         * @param minPeakDistance Минимальное расстояние между двумя разными линиями рамки (в пикселях).
         * @return Список координат найденных линий, отсортированный от края к центру (снизу вверх или справа налево).
         */
        static std::vector<int> findLinePeaksFromEnd(const cv::Mat& projection,
                                                     int minScore,
                                                     int ignoreMargin,
                                                     int minPeakDistance);

    private:
        // Параметры морфологического поиска правого нижнего угла рамки штампа
        constexpr static int kSearchZoneWidthPx{ 1600 };        ///< Ширина области поиска в правом нижнем углу листа.
        constexpr static int kSearchZoneHeightPx{ 1000 };       ///< Высота области поиска в правом нижнем углу листа.
        constexpr static int kHorizLineKernelPx{ 250 };         ///< Минимальная длина горизонтальной линии рамки штампа (в пикселях).
        constexpr static int kVertLineKernelPx{ 150 };          ///< Минимальная длина вертикальной линии рамки штампа (в пикселях).
        constexpr static int kMinHorizLineScore{ 300 * 255 };   ///< Минимальная суммарная яркость строки (не менее 300 белых пикселей).
        constexpr static int kMinVertLineScore{ 120 * 255 };    ///< Минимальная суммарная яркость столбца (не менее 120 белых пикселей).
        constexpr static int kEdgeIgnoreMarginPx{ 2 };          ///< Отступ от самого края скана для отсечения чёрных полос сканера.
        constexpr static int kCornerPadRightPx{ 25 };           ///< Запас вправо от найденного угла штампа при вырезке.
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
        constexpr static int kCropW{ 1120 };
        constexpr static int kCropH{ 580 };

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
        constexpr static float kMaxForm1NumberY{ 330.0f };             ///< Максимальный Y номера для Формы 1/2 (ниже находится только Форма 2а без названия).
        constexpr static float kNameSearchLeftMarginPx{ 185.0f };      ///< Допуск влево от X номера для длинных наименований и сдвинутых номеров.
        constexpr static float kNameSearchRightMarginPx{ 160.0f };     ///< Допуск вправо от X номера в пределах графы наименования.
        constexpr static float kNameSearchTopOffsetPx{ 25.0f };        ///< Минимальный отступ вниз от Y номера (чтобы не захватить сам номер).
        constexpr static float kNameSearchBottomOffsetPx{ 250.0f };    ///< Максимальный отступ вниз от Y номера (отсекает графу материала).
        constexpr static size_t kMinRawLineLength{ 2 };                ///< Минимальная длина сырого текстового блока для анализа.
        constexpr static size_t kMinCleanedNameBytes{ 4 };             ///< Минимум 4 байта в UTF-8 (2 кириллических символа, например «Ось»).

        // Геометрические пороги поиска заголовков граф «Лист» и «Листов» относительно децимального номера
        constexpr static float kUnsetCoord{ -1.0f };                   ///< Маркер ненайденной координаты X или Y.
        constexpr static float kSheetHeaderMinOffsetX{ 250.0f };       ///< Минимальный сдвиг вправо от номера до граф «Лист»/«Листов».
        constexpr static float kSheetHeaderTopToleranceY{ 30.0f };     ///< Допуск вверх от Y номера на случай перекоса скана.
        constexpr static float kSameBoxTolerancePx{ 10.0f };           ///< Допуск совпадения координат бокса заголовка.
        constexpr static int kMaxReasonableTotalSheets{ 25 };          ///< Верхний предел количества листов для отсечения артефактов OCR.
        constexpr static int kMaxReasonableSheetNumber{ 100 };         ///< Верхний предел номера текущего листа.

        // Геометрические пороги поиска цифр в соседних боксах относительно заголовков «Лист» и «Листов»
        constexpr static float kCellBelowMaxOffsetX{ 60.0f };          ///< Макс. смещение по X при поиске цифры СНИЗУ (Форма 2).
        constexpr static float kCellBelowMinOffsetY{ 12.0f };          ///< Мин. отступ вниз от заголовка до цифры (Форма 2).
        constexpr static float kCellBelowMaxOffsetY{ 60.0f };          ///< Макс. отступ вниз от заголовка до цифры (Форма 2).
        constexpr static float kCellRightMaxOffsetY{ 22.0f };          ///< Макс. отклонение по Y в той же строке (Форма 1).
        constexpr static float kSheetRightMinOffsetX{ 35.0f };         ///< Мин. отступ вправо от «Лист» до цифры (Форма 1).
        constexpr static float kSheetRightMaxOffsetX{ 140.0f };        ///< Макс. отступ вправо от «Лист» до цифры (Форма 1).
        constexpr static float kTotalRightMinOffsetX{ 45.0f };         ///< Мин. отступ вправо от «Листов» до цифры (Форма 1).
        constexpr static float kTotalRightMaxOffsetX{ 160.0f };        ///< Макс. отступ вправо от «Листов» до цифры (Форма 1).

        // Параметры прямого кропа ячеек через m_recNet, если DBNet пропустил мелкую цифру
        constexpr static int kDirectSheetRightShiftX{ 75 };            ///< Сдвиг вправо от «Лист» до ячейки цифры (Форма 1).
        constexpr static int kDirectTotalRightShiftX{ 82 };            ///< Сдвиг вправо от «Листов» до ячейки цифры (Форма 1).
        constexpr static int kDirectRightTopPadY{ 2 };                 ///< Запас вверх от Y заголовка для захвата цифры.
        constexpr static int kDirectRightCropW{ 36 };                  ///< Ширина области прямого захвата справа от заголовка.
        constexpr static int kDirectRightCropH{ 30 };                  ///< Высота области прямого захвата справа от заголовка.
        constexpr static int kDirectBelowLeftPadX{ 6 };                ///< Запас влево от X заголовка при захвате снизу (Форма 2).
        constexpr static int kDirectBelowShiftY{ 26 };                 ///< Сдвиг вниз от заголовка «Лист» до ячейки цифры (Форма 2).
        constexpr static int kDirectBelowCropW{ 90 };                  ///< Ширина области прямого захвата снизу.
        constexpr static int kDirectBelowCropH{ 38 };                  ///< Высота области прямого захвата снизу.

        // Параметры захвата ячейки «Лист» в правом углу Формы 2а
        constexpr static float kForm2aCellMinFromNumX{ 450.0f };       ///< Минимальный сдвиг вправо от номера до ячейки «Лист».
        constexpr static int kForm2aRightEdgeMarginX{ 165 };           ///< Отступ от правого края кропа штампа до ячейки «Лист».
        constexpr static int kForm2aCellShiftY{ 15 };                  ///< Сдвиг вниз от Y номера до цифры листа на Форме 2а.
        constexpr static int kForm2aCellCropW{ 155 };                  ///< Ширина области захвата ячейки листа на Форме 2а.
        constexpr static int kForm2aCellCropH{ 70 };                   ///< Высота области захвата ячейки листа на Форме 2а.
        constexpr static int kMinValidCropSizePx{ 10 };                ///< Минимальный размер вырезаемой ячейки в пикселях.
        constexpr static int kMinForm2aCropSizePx{ 15 };               ///< Минимальный размер ячейки Формы 2а в пикселях.
        constexpr static int kMinFallbackForm2aSheet{ 2 };             ///< Минимальный номер листа по умолчанию для Формы 2а.

        // Варианты распознавания заголовка графы «Листов» (проверяются первыми, так как включают подстроку «Лист»)
        static constexpr std::array<std::string_view, 14> kTotalSheetsKeywords{
            "Листов", "Листоб", "Лucmob", "Лucmoб", "Листо8", "Листо",
            "Пустоб", "ПистоВ", "Пистов", "Шucmoб", "ucino", "ucmo", "ucmm", "cmm"
        };

        // Варианты распознавания заголовка графы «Лист»
        static constexpr std::array<std::string_view, 9> kCurrentSheetKeywords{
            "Лист", "Лиcт", "Лucm", "llucm", "ucm", "ucт", "ucп", "Шucm", "VZUL"
        };

        // Параметры разбора децимального номера по ЕСКД (формат XXX.XX.XX.XX.XXX)
        constexpr static ptrdiff_t kMinDecimalDots{ 3 };       ///< Минимум 3 точки для признания строки кандидатом в номер.
        constexpr static size_t kLastDecimalDotIndex{ 4 };     ///< Номер 4-й точки, после которой идут последние 3 цифры.
        constexpr static size_t kLastGroupDigitsCount{ 3 };    ///< Количество цифр в последней группе децимального номера.
        constexpr static size_t kStandardDecimalDigits{ 12 };  ///< Стандартное количество цифр в децимальном номере ЕСКД.
        constexpr static size_t kUtf8CyrillicCharBytes{ 2 };   ///< Размер одной кириллической буквы в кодировке UTF-8.

        // Соответствие хвостов номера кодам конструкторских документов (включая типовые ошибки OCR)
        constexpr static std::array<std::pair<std::string_view, pdm::shared::DocumentTypeCode>, 7> kTrailingDocCodes{ {
            { "СБ", pdm::shared::DocumentTypeCode::AssemblyDrawing },
            { "CB", pdm::shared::DocumentTypeCode::AssemblyDrawing },
            { "C5", pdm::shared::DocumentTypeCode::AssemblyDrawing },
            { "C6", pdm::shared::DocumentTypeCode::AssemblyDrawing },
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
           { "Коиплект", "Комплект" },
           { "Соорочной", "Сборочный" },
           { "Сбороиной", "Сборочный" },
           { "Сдорочный", "Сборочный" },
           { "Осв", "Ось" },
           { "Деталв", "Деталь" },
           { "берхн", "верхн" },
           { "Берхн", "Верхн" },
           { "8zе3d", "Въезд" },
           { "8ze3d", "Въезд" },
           { "8zе3д", "Въезд" },
           { "8ze3д", "Въезд" },
           { "Въе3д", "Въезд" },
           { "инбентарный", "инвентарный" },
           { "инбентариый", "инвентарный" },
           { "инвентариый", "инвентарный" },
           { "иньснтарный", "инвентарный" },
           { "РРебро", "Ребро" },
           { "Опорабереговая", "Опора береговая" },
           { "дереговая", "береговая" },
           { "Ьалка", "Балка" },
           { "ьалка", "балка" }
        });
    };

} // namespace pdm::infrastructure

#endif // ONNX_STAMP_RECOGNIZER_H
