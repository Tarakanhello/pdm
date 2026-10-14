cmake_minimum_required(VERSION 3.23)

project(pdm LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_subdirectory(third_party)
add_subdirectory(shared)
add_subdirectory(core)
add_subdirectory(modules)
add_subdirectory(apps)
add_subdirectory(tests)
add_subdirectory(docs)


file(GLOB_RECURSE PDM_PROJECT_FILES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/apps/*
    ${CMAKE_SOURCE_DIR}/core/*
    ${CMAKE_SOURCE_DIR}/modules/*
    ${CMAKE_SOURCE_DIR}/shared/*
    ${CMAKE_SOURCE_DIR}/tests/*
    ${CMAKE_SOURCE_DIR}/docs/*
)

list(APPEND PDM_PROJECT_FILES
    ${CMAKE_SOURCE_DIR}/README.md
    ${CMAKE_SOURCE_DIR}/.gitignore
    ${CMAKE_SOURCE_DIR}/.gitmodules
)

add_custom_target(project_files SOURCES ${PDM_PROJECT_FILES})
