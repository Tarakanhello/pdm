cmake_minimum_required(VERSION 3.23)

set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

project(pdm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_subdirectory(apps)
add_subdirectory(composition)
add_subdirectory(core)
add_subdirectory(docs)
add_subdirectory(infrastructure)
add_subdirectory(modules)
add_subdirectory(shared)
add_subdirectory(tests)
add_subdirectory(third_party)
add_subdirectory(utilities)

file(GLOB_RECURSE PDM_PROJECT_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/apps/*
    ${CMAKE_SOURCE_DIR}/apps/core-server/*
    ${CMAKE_SOURCE_DIR}/composition/*
    ${CMAKE_SOURCE_DIR}/core/*
    ${CMAKE_SOURCE_DIR}/docs/*
    ${CMAKE_SOURCE_DIR}/docs/modules/*
    ${CMAKE_SOURCE_DIR}/docs/architecture/*
    ${CMAKE_SOURCE_DIR}/infrastructure/*
    ${CMAKE_SOURCE_DIR}/modules/*
    ${CMAKE_SOURCE_DIR}/shared/*
    ${CMAKE_SOURCE_DIR}/tests/*
    ${CMAKE_SOURCE_DIR}/utilities/*
)

list(APPEND PDM_PROJECT_FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/.gitignore
    ${CMAKE_SOURCE_DIR}/.gitmodules
    ${CMAKE_SOURCE_DIR}/vcpkg.json
)

add_custom_target(project_files SOURCES ${PDM_PROJECT_FILES})

# Копируем models и onnxruntime.dll в папку сборки
if(EXISTS "${CMAKE_SOURCE_DIR}/models")
    add_custom_target(copy_ocr_assets ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/models"
                "${CMAKE_BINARY_DIR}/models"
        )
endif()

