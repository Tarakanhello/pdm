#ifndef IMAGE_H
#define IMAGE_H

#include <cctype>
#include <filesystem>
#include <optional>
#include <vector>



namespace pdm::infrastructure
{

/**
 * @brief Класс для работы с изображением: загрузка, поворот, обрезка и сохранение.
 *
 * @details Использует библиотеку stb для работы с форматами изображений и
 * обеспечивает безопасное управление памятью (RAII) без использования сырых указателей.
 */
class Image final
{
private:
    std::vector<unsigned char> m_image{};   ///< Сырые пиксельные данные (R, G, B, R, G, B...).
    int m_width{};                          ///< Ширина изображения в пикселях.
    int m_height{};                         ///< Высота изображения в пикселях.
    int m_channels{};                       ///< Количество цветовых каналов (например, 3 для RGB, 4 для RGBA).

    std::filesystem::path m_sourcePath{};   ///< Исходный путь к файлу (для перезаписи или перезагрузки).

public:
    Image() = default;

    Image(const std::vector<unsigned char> image, int width, int height, int channels)
        : m_image{ image }, m_width{ width }, m_height{ height }, m_channels{ channels }
    {}

    /**
     * @brief Конструктор, создающий объект изображения из файла.
     * @param path Путь к файлу изображения.
     * @throws std::runtime_error Если файл не найден, пуст или поврежден.
     */
    Image(const std::filesystem::path& path);

    /// @brief Получить ширину изображения.
    int width()     const noexcept { return m_width; }

    /// @brief Получить высоту изображения.
    int height()    const noexcept { return m_height; }

    /// @brief Получить количество цветовых каналов.
    int channels()  const noexcept { return m_channels; }



    /**
     * @brief Вырезает прямоугольную область из изображения и сохраняет её.
     *
     * @param cropX Координата X левого верхнего угла вырезаемой области.
     * @param cropY Координата Y левого верхнего угла вырезаемой области.
     * @param cropW Ширина вырезаемой области.
     * @param cropH Высота вырезаемой области.
     * @param out Необязательный путь для сохранения. Если не указан, перезаписывает исходный файл.
     * @throws std::runtime_error Если координаты выходят за пределы изображения.
     */
    void crop(int cropX, int cropY, int cropW, int cropH,
              std::optional<std::filesystem::path> out = std::nullopt);



    Image getPartOfImage(int cropX, int cropY, int cropW, int cropH) const;



    /**
     * @brief Проверяет, содержит ли объект валидные данные изображения.
     */
    explicit operator bool() const noexcept { return !m_image.empty(); }



    /**
     * @brief Получить сырой указатель на начало пиксельных данных.
     * @return Указатель на массив байтов.
     * @throws std::runtime_error Если изображение пустое.
     */
    const unsigned char* get() const;



    void save(const std::filesystem::path& target = {}) const;



     std::vector<std::byte> convertToJpeg(int quality);


private:
    /**
     * @brief Загружает изображение из файла в память, обрабатывая EXIF-ориентацию.
     * @param path Путь к файлу.
     * @throws std::runtime_error При ошибках чтения или декодирования.
     */
    void loadFromPath(const std::filesystem::path& path);



    /**
     * @brief Callback-функция для stb_image_write для записи в память (вместо файла).
     * @param context Указатель на вектор, куда пишутся байты.
     * @param data Указатель на сжатые данные.
     * @param size Размер данных.
     */
    static void appendToBuffer(void* context, void* data, int size);



    /**
     * @brief Ищет в JPEG-файле тег EXIF Orientation (ориентация).
     * @param data Буфер с сырыми байтами файла.
     * @return Значение от 1 до 8 (1 - норма, 3 - 180 градусов, 6 - 90 CW, 8 - 90 CCW).
     */
    static int getExifOrientation(const std::vector<unsigned char>& data);



    /**
     * @brief Кодирует пиксели в формат файла и безопасно записывает на диск.
     *
     * @details Использует паттерн "атомарной записи": сначала данные пишутся во временный
     * файл (target.tmp), и только при успехе он переименовывается в целевой. Это
     * защищает от порчи файла при внезапном отключении питания.
     *
     * @param target Путь к целевому файлу.
     * @param w Ширина изображения.
     * @param h Высота изображения.
     * @param c Количество каналов.
     * @param imageInVec Вектор с пиксельными данными.
     */
    static void saveToPath(const std::filesystem::path& target,
                           int w, int h, int c,
                           const std::vector<unsigned char>& imageInVec);



    /**
     * @brief Перезагружает изображение с диска (сбрасывает изменения в памяти).
     */
    void reloadFromDisk() { *this =  Image{ m_sourcePath }; }



    /**
     * @brief Физически поворачивает изображение в памяти согласно значению EXIF.
     * @param orientation Ориентация (1 - норма, 3 - 180, 6 - 90 CW, 8 - 90 CCW).
     */
    void applyOrientation(int orientation);
};

} // end namespace pdm::infrastructure


#endif // IMAGE_H
