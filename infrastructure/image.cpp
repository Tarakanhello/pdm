#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"



namespace pdm::infrastructure
{

namespace
{

struct ImageMetadata
{
    int orientation{ 1 };
    std::optional<double> dpiX{};
    std::optional<double> dpiY{};
};

bool isValidDpi(double dpi)
{
    return std::isfinite(dpi) && dpi > 0.0 && dpi <= 100'000.0;
}

std::optional<double> makeDpi(double value)
{
    return isValidDpi(value) ? std::optional<double>{ value } : std::nullopt;
}

std::uint16_t readEndian16(const std::vector<unsigned char>& data, size_t pos, bool isLittle)
{
    if(isLittle)
    {
        return static_cast<uint16_t>(data[pos] |
                                     (static_cast<uint16_t>(data[pos + 1]) << 8));
    }

    return static_cast<uint16_t>((static_cast<std::uint16_t>(data[pos]) << 8) |
                                 static_cast<std::uint16_t>(data[pos + 1]));
}

std::uint32_t readEndian32(const std::vector<unsigned char>& data, size_t pos, bool isLittle)
{
    if(isLittle)
    {
        return static_cast<uint32_t>(data[pos])              |
               (static_cast<uint32_t>(data[pos + 1]) << 8)   |
               (static_cast<uint32_t>(data[pos + 2]) << 16)  |
               (static_cast<uint32_t>(data[pos + 3]) << 24);
    }
    else
        return (static_cast<uint32_t>(data[pos]) << 24)     |
               (static_cast<uint32_t>(data[pos + 1]) << 16) |
               (static_cast<uint32_t>(data[pos + 2]) << 8)  |
                static_cast<uint32_t>(data[pos + 3]);
}

ImageMetadata parseExif(const std::vector<unsigned char>& data, size_t tiffStart, size_t tiffEnd)
{
    ImageMetadata result{};
    if(tiffStart > tiffEnd || tiffEnd > data.size() || tiffEnd - tiffStart < 8)
    {
        return result;
    }

    const bool littleEndian{ data[tiffStart] == 'I' && data[tiffStart + 1] == 'I' };
    const bool bigEndian{ data[tiffStart] == 'M' && data[tiffStart + 1] == 'M' };

    if(!littleEndian && !bigEndian)
    {
        return result;
    }

    const auto hasRange{ [tiffEnd](size_t pos, size_t count){
        return pos <= tiffEnd && count <= tiffEnd - pos;
    } };

    const auto read16{ [&](size_t pos) -> std::optional<std::uint16_t> {
        if(!hasRange(pos, 2))
        {
            return std::nullopt;
        }

        return std::optional<std::uint16_t>{ readEndian16(data, pos, littleEndian) };
    }};

    const auto read32{ [&](size_t pos) -> std::optional<uint32_t>{
        if(!hasRange(pos, 4))
        {
            return std::nullopt;
        }

        return std::optional<std::uint32_t>{ readEndian32(data, pos, littleEndian) };

    } };

    if(read16(tiffStart + 2) != 42)
    {
        return result;
    }

    const auto ifdOffset{ read32(tiffStart + 4) };
    if(!ifdOffset || *ifdOffset > tiffEnd - tiffStart)
    {
        return result;
    }

    const size_t ifdPos{ tiffStart + static_cast<size_t>(*ifdOffset) };

    const auto entryCount{ read16(ifdPos) };

    if(!entryCount)
    {
        return result;
    }

    const size_t entriesPos{ ifdPos + 2 };
    if(!hasRange(entriesPos, static_cast<size_t>(*entryCount) * 12))
    {
        return result;
    }

    std::optional<uint16_t> resolutionUnit{};
    std::optional<double> xResolution{};
    std::optional<double> yResolution{};

    for(uint16_t i{ 0 }; i < *entryCount; ++i)
    {
        const size_t entry{ entriesPos + static_cast<size_t>(i) * 12 };
        const auto tag{ read16(entry) };
        const auto type{ read16(entry + 2) };
        const auto count{ read32(entry + 4) };
        if(!tag || !type || !count)
        {
            continue;
        }

        if(*tag == 0x0112 && *type == 3 && *count >= 1)
        {
            const auto orientation{ read16(entry + 8) };
            if(orientation && *orientation >= 1 && *orientation <= 8)
            {
                result.orientation = static_cast<int>(*orientation);
            }
        }
        else if(*tag == 0x0128 && *type == 3 && *count >= 1)
        {
            resolutionUnit = read16(entry + 8);
        }
        else if((*tag == 0x011A || *tag == 0x011B) && *type == 5 && *count >= 1)
        {
            const auto valueOffset{ read32(entry + 8) };
            if(!valueOffset || *valueOffset > tiffEnd - tiffStart)
            {
                continue;
            }

            const size_t rationalPos{ tiffStart + static_cast<size_t>(*valueOffset) };
            const auto numerator{ read32(rationalPos) };
            const auto denominator{ read32(rationalPos + 4) };
            if(!numerator || !denominator || *denominator == 0)
            {
                continue;
            }

            const double resolution{ static_cast<double>(*numerator) /
                                    static_cast<double>(*denominator) };
            if(*tag == 0x011A) xResolution = resolution;
            else yResolution = resolution;
        }
    }

    if(resolutionUnit && (*resolutionUnit == 2 || *resolutionUnit == 3))
    {
        const double multiplier{ *resolutionUnit == 2 ? 1.0 : 2.54 };
        if(xResolution)
        {
            result.dpiX = makeDpi(*xResolution * multiplier);
        }
        if(yResolution)
        {
            result.dpiY = makeDpi(*yResolution * multiplier);
        }
    }


    return result;
}


ImageMetadata parseImageMetadata(const std::vector<unsigned char>& data)
{
    ImageMetadata result{};

    // BMP: BITMAPINFOHEADER и совместимые заголовки содержат pixels-per-meter.
    if(data.size() >= 54 && data[0] == 'B' && data[1] == 'M')
    {
        const uint32_t dibHeaderSize{ readEndian32(data, 14, true) };
        if(dibHeaderSize >= 40 && data.size() >= 14 + static_cast<size_t>(dibHeaderSize))
        {
            const int32_t xPixelsPerMeter{ static_cast<int32_t>(readEndian32(data, 38, true)) };
            const int32_t yPixelsPerMeter{ static_cast<int32_t>(readEndian32(data, 42, true)) };
            if(xPixelsPerMeter > 0) result.dpiX = makeDpi(xPixelsPerMeter * 0.0254);
            if(yPixelsPerMeter > 0) result.dpiY = makeDpi(yPixelsPerMeter * 0.0254);
        }
        return result;
    }

    // JPEG: читаем APP0 (JFIF) и APP1 (EXIF), не выходя за границы сегментов.
    if(data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8)
    {
        return result;
    }

    std::optional<double> jfifDpiX{};
    std::optional<double> jfifDpiY{};
    ImageMetadata exif{};
    size_t pos{ 2 };

    while(pos + 1 < data.size())
    {
        if(data[pos] != 0xFF)
        {
            ++pos;
            continue;
        }

        while(pos < data.size() && data[pos] == 0xFF)
        {
            ++pos;
        }
        if(pos >= data.size())
        {
            break;
        }

        const unsigned char marker{ data[pos++] };
        if(marker == 0xD9 || marker == 0xDA)
        {
            break;
        }
        if(marker == 0x01 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7))
        {
            continue;
        }
        if(pos + 2 > data.size())
        {
            break;
        }

        const uint16_t segmentLength{ readEndian16(data, pos, false) };
        if(segmentLength < 2)
        {
            break;
        }
        const size_t segmentStart{ pos + 2 };
        const size_t segmentEnd{ pos + segmentLength };
        if(segmentEnd > data.size())
        {
            break;
        }

        if(marker == 0xE0 && segmentEnd - segmentStart >= 12 &&
            data[segmentStart] == 'J' && data[segmentStart + 1] == 'F' &&
            data[segmentStart + 2] == 'I' && data[segmentStart + 3] == 'F' &&
            data[segmentStart + 4] == 0)
        {
            const unsigned char units{ data[segmentStart + 7] };
            const double xDensity{ static_cast<double>(readEndian16(data, segmentStart + 8, false)) };
            const double yDensity{ static_cast<double>(readEndian16(data, segmentStart + 10, false)) };
            const double multiplier{ units == 1 ? 1.0 : (units == 2 ? 2.54 : 0.0) };
            if(multiplier > 0.0)
            {
                jfifDpiX = makeDpi(xDensity * multiplier);
                jfifDpiY = makeDpi(yDensity * multiplier);
            }
        }
        else if(marker == 0xE1 && segmentEnd - segmentStart >= 14 &&
                 data[segmentStart] == 'E' && data[segmentStart + 1] == 'x' &&
                 data[segmentStart + 2] == 'i' && data[segmentStart + 3] == 'f' &&
                 data[segmentStart + 4] == 0 && data[segmentStart + 5] == 0)
        {
            exif = parseExif(data, segmentStart + 6, segmentEnd);
        }

        pos = segmentEnd;
    }

    // Приоритет EXIF: это более полные метаданные, чем плотность JFIF.
    result.orientation = exif.orientation;
    result.dpiX = exif.dpiX ? exif.dpiX : jfifDpiX;
    result.dpiY = exif.dpiY ? exif.dpiY : jfifDpiY;

    // Image физически поворачивает пиксели при EXIF orientation 6/8.
    if(result.orientation == 6 || result.orientation == 8)
    {
        std::swap(result.dpiX, result.dpiY);
    }

    return result;
}

} // end ananimous namespace




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

    return Image{ cropped, cropW, cropH, channels(), m_dpiX, m_dpiY };
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

    const ImageMetadata metadata{ parseImageMetadata(bytes) };
    m_dpiX = metadata.dpiX;
    m_dpiY = metadata.dpiY;

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
    applyOrientation(metadata.orientation);
}




void Image::appendToBuffer(void* context, void* data, int size)
{
    auto* out{ static_cast<std::vector<unsigned char>*>(context) };
    const auto* bytes{ static_cast<const unsigned char*>(data) };
    out->insert(out->end(), bytes, bytes + size);
}




 int Image::getExifOrientation(const std::vector<unsigned char>& data)
{
     return parseImageMetadata(data).orientation;
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

std::vector<unsigned char> Image::convertToJpeg(int quality) const
{
    std::vector<unsigned char> encoded;
    stbi_write_jpg_to_func(appendToBuffer, &encoded, m_width, m_height, m_channels, get(), quality);


    return encoded;
}

} // end namespace pdm::infrastructure
