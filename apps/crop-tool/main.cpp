/**
 * @file main.cpp
 * @brief Утилита командной строки для обрезки штампа (основной надписи) с отсканированных чертежей.
 *
 * @details
 * Программа читает изображения из указанной папки, автоматически корректирует
 * их ориентацию на основе метаданных EXIF (если сканер повернул лист),
 * вырезает заданную область (обычно правый нижний угол) и безопасно
 * сохраняет результат в подпапку "cropped".
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "files_from_folder.h"



namespace
{

    /// @brief Отступ от правого края изображения (в пикселях).
    constexpr int kOffsetX{ 30 };

    /// @brief Отступ от нижнего края изображения (в пикселях)
    constexpr int kOffsetY{ 90 }; // Отступаем 90 пикселей от нижнего края

    /// @brief Ширина вырезаемой области штампа (в пикселях).
    constexpr int kCropW{ 1050 };

    /// @brief Высота вырезаемой области штампа (в пикселях).
    constexpr int kCropH{ 500 };

} // end namespace



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

    /**
     * @brief Конструктор, создающий объект изображения из файла.
     * @param path Путь к файлу изображения.
     * @throws std::runtime_error Если файл не найден, пуст или поврежден.
     */
    Image(const std::filesystem::path& path)
        : m_sourcePath{ path }
    {
        loadFromPath(path);

        if(m_image.empty())
        {
            throw std::runtime_error(std::format("Image::Image(const std::filesystem::"
                                                 "path&): Cannot load {}", path.string()));
        }
    }

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
              std::optional<std::filesystem::path> out = std::nullopt)
    {
        if(cropX < 0 || cropY < 0 || cropW <= 0 || cropH <= 0 ||
            cropX + cropW > width() || cropY + cropH > height())
        {
            throw std::runtime_error("Image::crop(int, int, int, int, std::optional):"
                                     " Wrong crop coordinates");
        }

        std::vector<unsigned char> cropped(static_cast<size_t>(cropW * cropH * channels()));

        // Построчное копирование пикселей
        for(int y{ 0 }; y < cropH; ++y)
        {
            const unsigned char* srcRow{ get() +
                                        static_cast<size_t>(((y + cropY) * width() + cropX)
                                                                            * channels()) };

            std::copy(srcRow,
                      srcRow + static_cast<size_t>((cropW * channels())),
                      cropped.data() + static_cast<size_t>(y * cropW * channels()));
        }

        const std::filesystem::path target{ out.value_or(m_sourcePath) };

        if(target == m_sourcePath)
        {
            // Cохраняем поверх оригинала
            saveToPath(m_sourcePath, cropW, cropH, channels(), cropped);
            reloadFromDisk();
        }
        else
        {
            // Cохраняем в другую папку (создаем её при необходимости)
            if (!std::filesystem::exists(target))
            {
                std::filesystem::create_directories(target);
            }

            saveToPath(target / std::filesystem::path{ m_sourcePath.stem().string() +
                                                      "_cropped" +
                                                      m_sourcePath.extension().string() },
                       cropW, cropH, channels(), cropped);
        }
    }



    /**
     * @brief Проверяет, содержит ли объект валидные данные изображения.
     */
    explicit operator bool() const noexcept { return !m_image.empty(); }



    /**
     * @brief Получить сырой указатель на начало пиксельных данных.
     * @return Указатель на массив байтов.
     * @throws std::runtime_error Если изображение пустое.
     */
    const unsigned char* get() const
    {
        if(!*this)
        {
            throw std::runtime_error("Image::Img::get(): Empty image!");
        }

        return m_image.data();
    }


private:
    /**
     * @brief Загружает изображение из файла в память, обрабатывая EXIF-ориентацию.
     * @param path Путь к файлу.
     * @throws std::runtime_error При ошибках чтения или декодирования.
     */
    void loadFromPath(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);

        if(!in)
        {
            throw std::runtime_error(std::format("Image::loadFromPath(const std::filesystem::"
                                                 "path&): Cannot open file: {}",
                                                 path.string()));
        }

        // Читаем весь файл в буфер (для чтения EXIF и декодирования из памяти)
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>() };

        if(bytes.empty())
        {
            throw std::runtime_error(std::format("Image::loadFromPath(const std::filesystem::"
                                                 "path&): Empty file: {}",
                                                 path.string()));
        }

        // RAII-обертка для безопасного освобождения памяти stb при исключениях
        struct ptrDeleter
        {
            void operator()(unsigned char* ch)
            {
                stbi_image_free(ch);
            }
        };

        std::unique_ptr<unsigned char, ptrDeleter> raw{ stbi_load_from_memory(bytes.data(),
                                                        static_cast<int>(bytes.size()), &m_width, &m_height, &m_channels, 0) };

        if(!raw)
        {
            throw std::runtime_error(std::format("Image::loadFromPath(const std::filesystem::"
                                                 "path&): Cannot decode image: {}",
                                                 path.string()));
        }

        // Копируем данные из сырого указателя в вектор (безопасное владение)
        m_image = std::vector<unsigned char>{ raw.get(), raw.get() + static_cast<size_t>(m_width * m_height * m_channels) };

        // Применяем поворот, если этого требует EXIF
        int orientation{ getExifOrientation(bytes) };
        applyOrientation(orientation);
    }



    /**
     * @brief Callback-функция для stb_image_write для записи в память (вместо файла).
     * @param context Указатель на вектор, куда пишутся байты.
     * @param data Указатель на сжатые данные.
     * @param size Размер данных.
     */
    static void appendToBuffer(void* context, void* data, int size)
    {
        auto* out{ static_cast<std::vector<unsigned char>*>(context) };
        const auto* bytes{ static_cast<const unsigned char*>(data) };
        out->insert(out->end(), bytes, bytes + size);
    }



    /**
     * @brief Ищет в JPEG-файле тег EXIF Orientation (ориентация).
     * @param data Буфер с сырыми байтами файла.
     * @return Значение от 1 до 8 (1 - норма, 3 - 180 градусов, 6 - 90 CW, 8 - 90 CCW).
     */
    static int getExifOrientation(const std::vector<unsigned char>& data)
    {
        if(data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) // Не JPEG
        {
            return 1;
        }

        size_t i{ 2 };
        while(i < data.size() - 4)
        {
            if(data[i] != 0xFF)
            {
                i++;
                continue;
            }

            unsigned char marker{ data[i + 1] };
            unsigned int length{ static_cast<unsigned int>((data[i + 2] << 8) | data[i + 3]) };

            // Маркер APP1 (EXIF)
            if (marker == 0xE1)
            {
                if(length >= 8 && data[i + 4] == 'E' && data[i + 5] == 'x' && data[i + 6] == 'i' && data[i + 7] == 'f')
                {
                    size_t offset{ i + 10 };
                    bool littleEndian{ (data[offset] == 'I' && data[offset + 1] == 'I') };

                    auto read16{ [&](size_t pos) -> unsigned short {
                        if (littleEndian) return data[pos] | (data[pos + 1] << 8);
                        return (data[pos] << 8) | data[pos + 1];
                    } };
                    auto read32{ [&](size_t pos) -> unsigned int {
                        if (littleEndian) return data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16) | (data[pos + 3] << 24);
                        return (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
                    } };

                    unsigned int ifdOffset{ read32(offset + 4) };
                    size_t ifdPos{ offset + ifdOffset };
                    unsigned short numEntries{ read16(ifdPos) };
                    ifdPos += 2;

                    for(int k{ 0 }; k < numEntries; ++k)
                    {
                        unsigned short tag{ read16(ifdPos) };
                        if (tag == 0x0112)
                        { // Тег Orientation
                            return read16(ifdPos + 8);
                        }
                        ifdPos += 12;
                    }
                }
            }
            i += 2 + length;
        }

        return 1;
    }



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
    static void saveToPath(const std::filesystem::path& target, int w, int h, int c, const std::vector<unsigned char>& imageInVec)
    {
        std::string extension{ target.extension().string() };

        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](const auto& ch)
                       { return std::tolower(ch); });

        std::vector<unsigned char> encoded;

        const int stride{ w * c };
        bool ok{ false };

        if(extension == ".png")
        {
            ok = stbi_write_png_to_func(appendToBuffer, &encoded, w, h, c, imageInVec.data(), stride);
        }
        else if(extension == ".jpg" || extension == ".jpeg")
        {
            ok = stbi_write_jpg_to_func(appendToBuffer, &encoded, w, h, c, imageInVec.data(), 90);
        }
        else if(extension == ".bmp")
        {
            ok = stbi_write_bmp_to_func(appendToBuffer, &encoded, w, h, c, imageInVec.data());
        }
        else if(extension == ".tga")
        {
            ok = stbi_write_tga_to_func(appendToBuffer, &encoded, w, h, c, imageInVec.data());
        }
        else
        {
            throw std::runtime_error("Image::saveToPath: Unsupported extension: " + extension);
        }

        if (!ok || encoded.empty())
        {
            throw std::runtime_error("Image::saveToPath: Failed to encode image data");
        }

        std::filesystem::path tmp{ target };
        tmp += ".tmp";

        // Запись во временный файл
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                throw std::runtime_error(std::format("Image::saveToPath: Cannot create file: {}", tmp.string()));
            }

            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));

            if (!out)
            {
                std::error_code ec;
                std::filesystem::remove(tmp, ec);
                throw std::runtime_error(std::format("Image::saveToPath: Cannot write file: {}", tmp.string()));
            }
        }

        std::error_code ec;
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target, ec);
        if(ec)
        {
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error(std::format("Image::saveToPath: Cannot replace file: {}", target.string()));
        }
    }



    /**
     * @brief Перезагружает изображение с диска (сбрасывает изменения в памяти).
     */
    void reloadFromDisk()
    {
        *this =  Image{ m_sourcePath };
    }



    /**
     * @brief Физически поворачивает изображение в памяти согласно значению EXIF.
     * @param orientation Ориентация (1 - норма, 3 - 180, 6 - 90 CW, 8 - 90 CCW).
     */
    void applyOrientation(int orientation)
    {
        if(orientation == 1)
        {
            return;
        }

        int newW{ m_width };
        int newH{ m_height };
        bool swap{ (orientation == 6 || orientation == 8) };
        if(swap)
        {
            newW = m_height;
            newH = m_width;
        }

        const size_t dstSize{ static_cast<size_t>(newW) * newH * m_channels };

        std::vector<unsigned char> rotated(dstSize);

        for(int y{ 0 }; y < newH; ++y)
        {
            for (int x{ 0 }; x < newW; ++x)
            {
                int srcX{ x };
                int srcY{ y };
                if(orientation == 3)
                {
                    srcX = m_width - 1 - x;
                    srcY = m_height - 1 - y;
                }
                else if(orientation == 6)
                {
                    srcX = y;
                    srcY = m_height - 1 - x;
                }
                else if(orientation == 8)
                {
                    srcX = m_width - 1 - y;
                    srcY = x;
                }

                for(int c{ 0 }; c < m_channels; ++c)
                {
                    size_t dstIdx{ (static_cast<size_t>(y) * newW + x) * m_channels + c };
                    size_t srcIdx{ (static_cast<size_t>(srcY) * m_width + srcX) * m_channels + c };

                    rotated[dstIdx] = m_image[srcIdx];
                }
            }
        }

        m_image = std::move(rotated);

        // Обновляем размеры картинки
        m_width = newW;
        m_height = newH;
        // m_channels не меняется при повороте
    }
};



/**
 * @brief Точка входа в программу.
 *
 * @details Ожидает один аргумент командной строки — путь к папке со сканами.
 * Для каждого изображения вычисляет координаты правого нижнего угла
 * (с учетом отступов) и вызывает метод обрезки.
 *
 * @param argc Количество аргументов.
 * @param argv Массив аргументов. argv[1] должен содержать путь к папке.
 * @return 0 в случае успеха, 1 при ошибке аргументов.
 */
int main(int argc, const char* argv[])
{
    if(argc < 2)
    {
        std::cerr << "Usage: crop_test <path_to_scans_folder>\n";
        return 1;
    }

    std::vector<std::filesystem::path> imagesFiles{ pdm::utilities::getFilesFromFolder(std::filesystem::path{ argv[1] },
                                                                                ".jpg", ".JPG", ".jpeg", ".JPEG") };

    for(const auto& file : imagesFiles)
    {
        Image image{ file };
        int cropX{ image.width() - kOffsetX - kCropW };
        int cropY{ image.height() - kOffsetY - kCropH };
        image.crop(cropX, cropY, kCropW, kCropH, std::filesystem::path{ file.parent_path() / "cropped" });
    }

    return 0;
}




