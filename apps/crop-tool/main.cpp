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

#include <filesystem>
#include <iostream>
#include <vector>

#include "files_from_folder.h"
#include "image.h"

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
        pdm::infrastructure::Image image{ file };
        int cropX{ image.width() - kOffsetX - kCropW };
        int cropY{ image.height() - kOffsetY - kCropH };
        image.crop(cropX, cropY, kCropW, kCropH, std::filesystem::path{ file.parent_path() / "cropped" });
    }

    return 0;
}




