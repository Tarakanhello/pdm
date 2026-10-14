#include <catch2/catch_test_macros.hpp>

#include "document.h"

// -------------------------------------------------------------------
// 1. Проверка создания объекта и заполнения полей
// -------------------------------------------------------------------
TEST_CASE("Document: creation and field initialization", "[document]")
{
    SECTION("Default constructor creates an object")
    {
        pdm::Document doc{};
        // Проверяем, что объект существует и его поля имеют значения по умолчанию
        REQUIRE(doc.number.empty());
        REQUIRE(doc.name.empty());
        REQUIRE(doc.code == pdm::DocumentTypeCode::Unknown);
    }

    SECTION("Fields are initialized correctly")
    {
        pdm::Document doc{"716.36.15", "Полка", pdm::DocumentTypeCode::AssemblyDrawing };

        REQUIRE(doc.number == "716.36.15");
        REQUIRE(doc.name == "Полка");
        REQUIRE(doc.code == pdm::DocumentTypeCode::AssemblyDrawing);
    }
}

// -------------------------------------------------------------------
// 2. Проверка копирования
// -------------------------------------------------------------------
TEST_CASE("Document: copy", "[document][copy]")
{
    // Создаём исходный объект с известными значениями
    pdm::Document original{"716.36.15", "Полка", pdm::DocumentTypeCode::AssemblyDrawing};

    SECTION("Copy constructor")
    {
        pdm::Document copy{ original };

        // Проверяем, что все поля скопированы правильно
        CHECK(copy.number == original.number);
        CHECK(copy.name   == original.name);
        CHECK(copy.code   == original.code);
    }

    SECTION("Copy assignment operator")
    {
        pdm::Document copy;
        copy = original;

        CHECK(copy.number == original.number);
        CHECK(copy.name   == original.name);
        CHECK(copy.code   == original.code);
    }

    SECTION("Modifying the copy does not affect the original")
    {
        pdm::Document copy{ original };
        copy.number = "321.125.765";
        copy.name   = "Кронштейн";
        copy.code   = pdm::DocumentTypeCode::TechnicalSpecifications;

        REQUIRE(copy.number == "321.125.765");
        REQUIRE(copy.name   == "Кронштейн");
        REQUIRE(copy.code   == pdm::DocumentTypeCode::TechnicalSpecifications);

        // Оригинал должен остаться неизменным
        CHECK(original.number == "716.36.15");
        CHECK(original.name   == "Полка");
        CHECK(original.code   == pdm::DocumentTypeCode::AssemblyDrawing);
    }

    SECTION("Russian names work")
    {
        CHECK(original.name == "Полка");
        CHECK(original.name.size() == 10);  // 10 байт UTF-8 для "Полка"
    }
}
