#include "image.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"



namespace pdm::infrastructure
{

Image::Image(const std::filesystem::path& path)
    : m_sourcePath{ path }
{
    loadFromPath(path);

    if(m_image.empty())
    {
        throw std::runtime_error(std::format("Image::Image(const std::filesystem::"
                                             "path&): Cannot load {}", path.string()));
    }
}




void Image::crop(int cropX, int cropY, int cropW, int cropH,
          std::optional<std::filesystem::path> out)
{
    auto cropped{ getPartOfImage(cropX, cropY, cropW, cropH) };

    const std::filesystem::path target{ out.value_or(m_sourcePath) };

    if(target == m_sourcePath)
    {
        // Cохраняем поверх оригинала
        cropped.save(m_sourcePath);
        reloadFromDisk();
    }
    else
    {
        // Cохраняем в другую папку (создаем её при необходимости)
        if (!std::filesystem::exists(target))
        {
            std::filesystem::create_directories(target);
        }

        cropped.save(target / std::filesystem::path{ m_sourcePath.stem().string() +
                                                  "_cropped" +
                                                  m_sourcePath.extension().string() });
    }
}




Image Image::getPartOfImage(int cropX, int cropY, int cropW, int cropH) const
{
    if(cropX < 0 || cropY < 0 || cropW <= 0 || cropH <= 0 ||
        cropX + cropW > width() || cropY + cropH > height())
    {
        throw std::runtime_error("Image::getPartOfImage(int, int, int, int):"
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

    return Image{cropped, cropW, cropH, channels() };
}




const unsigned char* Image::get() const
{
    if(!*this)
    {
        throw std::runtime_error("Image::Img::get(): Empty image!");
    }

    return m_image.data();
}




void Image::loadFromPath(const std::filesystem::path& path)
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




void Image::appendToBuffer(void* context, void* data, int size)
{
    auto* out{ static_cast<std::vector<unsigned char>*>(context) };
    const auto* bytes{ static_cast<const unsigned char*>(data) };
    out->insert(out->end(), bytes, bytes + size);
}




 int Image::getExifOrientation(const std::vector<unsigned char>& data)
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




void Image::save(const std::filesystem::path& target) const
{
    if(target.empty() && m_sourcePath.empty())
    {
        throw std::runtime_error("Image::save(const std::filesystem::path&): empty path. Cannot save");
    }

    saveToPath(target.empty() ? m_sourcePath : target, width(), height(), channels(), m_image);
}





 void Image::saveToPath(const std::filesystem::path& target, int w, int h, int c, const std::vector<unsigned char>& imageInVec)
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




void Image::applyOrientation(int orientation)
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

std::vector<std::byte> Image::convertToJpeg(int quality)
{
    std::vector<unsigned char> encoded;
    stbi_write_jpg_to_func(appendToBuffer, &encoded, m_width, m_height, m_channels, get(), quality);

    std::vector<std::byte> result(encoded.size());
    std::memcpy(result.data(), encoded.data(), encoded.size());
    return result;
}

} // end namespace pdm::infrastructure
