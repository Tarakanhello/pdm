#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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

    // 1. Отступ от правого нижнего угла (положительная ось X влево, Y вверх)
    constexpr int kOffsetX{ 30 }; // Отступаем 30 пикселей от правого края
    constexpr int kOffsetY{ 90 }; // Отступаем 100 пикселей от нижнего края


    // 2. Фиксированный размер вырезаемого куска
    constexpr int kCropW{ 1050 };
    constexpr int kCropH{ 500 };


}


class Image
{
private:
    struct ImageRemoval
    {
        void operator()(unsigned char* ptr) const noexcept
        {
            stbi_image_free(ptr);
        }
    };

    int m_width{};
    int m_height{};
    int m_channels{};

    std::unique_ptr<unsigned char, ImageRemoval> m_image{};

    std::filesystem::path m_sourcePath{};

    static std::unique_ptr<unsigned char, ImageRemoval> loadFromPath(const std::filesystem::path& path,
                                                                     int& w, int& h, int& c)
    {
        std::ifstream in(path, std::ios::binary);

        if(!in)
        {
            throw std::runtime_error(std::format("Cannot open file: {}", path.string()));
        }

        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

        if(bytes.empty())
        {
            throw std::runtime_error(std::format("Empty file: {}", path.string()));
        }

        unsigned char* raw{ stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 0) };

        if(!raw)
        {
            throw std::runtime_error(std::format("Cannot decode image: {}", path.string()));
        }

        std::unique_ptr<unsigned char, ImageRemoval> ptr{ raw };

        int orientation{ getExifOrOrienatition(bytes) };

        if(orientation > 1)
        {
            Image tempImage;

            tempImage.m_image = std::move(ptr);
            tempImage.m_width = w;
            tempImage.m_height = h;
            tempImage.m_channels = c;

            tempImage.applyOrientation(orientation);

            w = tempImage.m_width;
            h = tempImage.m_height;
            c = tempImage.m_channels;

            return std::move(tempImage.m_image);
        }

        return ptr;
    }



    // ---------- Запись: stb → буфер в памяти, потом одна операция с ФС ----------
    static void appendToBuffer(void* context, void* data, int size)
    {
        auto* out{ static_cast<std::vector<unsigned char>*>(context) };
        const auto* bytes{ static_cast<const unsigned char*>(data) };
        out->insert(out->end(), bytes, bytes + size);
    }

    /**
     * @brief Ищет в JPEG-файле тег EXIF Orientation (ориентация).
     * @return Значение от 1 до 8 (1 - норма, 3 - 180 градусов, 6 - 90 CW, 8 - 90 CCW).
     */
    static int getExifOrOrienatition(const std::vector<unsigned char>& data)
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
     * @brief Физически поворачивает изображение в памяти.
     */
    void applyOrientation(int orientation)
    {
        if (orientation == 1) return;

        int newW{ m_width };
        int newH{ m_height };
        bool swap{ (orientation == 6 || orientation == 8) };
        if (swap)
        {
            newW = m_height;
            newH = m_width;
        }

        // Вычисляем общий размер в байтах
        const size_t srcSize{ static_cast<size_t>(m_width) * m_height * m_channels };
        const size_t dstSize{ static_cast<size_t>(newW) * newH * m_channels };

        std::vector<unsigned char> rotated(dstSize);

        // Оборачиваем сырые указатели в span, чтобы анализатор знал их размер
        std::span<const unsigned char> srcSpan(m_image.get(), srcSize);
        std::span<unsigned char> dstSpan(rotated.data(), dstSize);

        for (int y = 0; y < newH; ++y)
        {
            for (int x = 0; x < newW; ++x)
            {
                int srcX{ x };
                int srcY{ y };
                if (orientation == 3)
                {
                    srcX = m_width - 1 - x;
                    srcY = m_height - 1 - y;
                }
                else if (orientation == 6)
                {
                    srcX = y;
                    srcY = m_height - 1 - x;
                }
                else if (orientation == 8)
                {
                    srcX = m_width - 1 - y;
                    srcY = x;
                }

                for (int c = 0; c < m_channels; ++c)
                {
                    size_t dstIdx = (static_cast<size_t>(y) * newW + x) * m_channels + c;
                    size_t srcIdx = (static_cast<size_t>(srcY) * m_width + srcX) * m_channels + c;

                    // Теперь используем безопасные span-обертки
                    dstSpan[dstIdx] = srcSpan[srcIdx];
                }
            }
        }

        // =========================================================
        // ФИНАЛЬНЫЙ БЛОК ЗАМЕНЫ ПАМЯТИ
        // =========================================================

        // Выделяем сырую память (stbi_image_free ожидает память, выделенную malloc)
        unsigned char* newRaw = static_cast<unsigned char*>(std::malloc(dstSize));

        if (newRaw)
        {
            // ИСПОЛЬЗУЕМ std::copy ВМЕСТО std::memcpy
            std::copy(rotated.begin(), rotated.end(), newRaw);

            // unique_ptr автоматически вызовет stbi_image_free для СТАРОЙ памяти
            m_image.reset(newRaw);

            // Обновляем размеры картинки
            m_width = newW;
            m_height = newH;
            // m_channels не меняется при повороте
        }
        else
        {
            throw std::runtime_error("Failed to allocate memory for rotated image");
        }
    }


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
            throw std::runtime_error("Unsupported extension: " + extension);
        }

        if (!ok || encoded.empty())
        {
            throw std::runtime_error("Failed to encode image data");
        }

        std::filesystem::path tmp{ target };
        tmp += ".tmp";

        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                throw std::runtime_error(std::format("Cannot create file: {}", tmp.string()));
            }

            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));

            if (!out)
            {
                std::error_code ec;
                std::filesystem::remove(tmp, ec);
                throw std::runtime_error(std::format("Cannot write file: {}", tmp.string()));
            }
        }

        std::error_code ec;
        std::filesystem::remove(target, ec);
        std::filesystem::rename(tmp, target, ec);
        if (ec)
        {
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error(std::format("Cannot replace file: {}", target.string()));
        }
    }

    void reloadFromDisk()
    {
        int w{};
        int h{};
        int c{};
        auto fresh{ loadFromPath(m_sourcePath, w, h, c) };

        m_image    = std::move(fresh);
        m_width    = w;
        m_height   = h;
        m_channels = c;
    }



public:
    Image() = default;

    Image(const std::filesystem::path& path)
        : m_image{ loadFromPath(path, m_width, m_height, m_channels) }
        , m_sourcePath{ path }
    {
        if(!m_image.get())
        {
            throw std::runtime_error(std::format("Cannot load {}", path.string()));
        }
    }

    explicit operator bool() const noexcept{ return m_image.get(); }

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    int channels() const noexcept { return m_channels; }

    void crop(int cropX, int cropY, int cropW, int cropH, std::optional<std::filesystem::path> out = std::nullopt )
    {
        if(cropX < 0 || cropY < 0 || cropW <= 0 || cropH <= 0 ||
            cropX + cropW > width() || cropY + cropH > height())
        {
            throw std::runtime_error("Wrong crop coordinates");
        }

        std::vector<unsigned char> cropped(static_cast<size_t>(cropW * cropH * channels()));

        for(int y{ 0 }; y < cropH; ++y)
        {
            const unsigned char* srcRow{ m_image.get() + static_cast<size_t>(((y + cropY) * width() + cropX) * channels()) };

            std::copy(srcRow,
                      srcRow + static_cast<size_t>((cropW * channels())),
                      cropped.data() + static_cast<size_t>(y * cropW * channels()));
        }

        const std::filesystem::path target{ out.value_or(m_sourcePath) };

        if(target == m_sourcePath)
        {
            saveToPath(m_sourcePath, cropW, cropH, channels(), cropped);
            reloadFromDisk();
        }
        else
        {
            if (!std::filesystem::exists(target))
            {
                std::filesystem::create_directories(target);
            }

            saveToPath(target / std::filesystem::path{ m_sourcePath.stem().string() + "_cropped" + m_sourcePath.extension().string() }, cropW, cropH, channels(), cropped);
        }
    }
};



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




