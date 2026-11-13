# Отключаем предупреждения как ошибки для всего, что собирается из third_party
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

add_subdirectory(Catch2)
add_subdirectory(QXlsx/QXlsx)


add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(stb_image_resize INTERFACE)
target_include_directories(stb_image_resize SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image_resize)

add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cpp-httplib)
if(MINGW)
    target_link_libraries(httplib INTERFACE ws2_32 crypt32 cryptui)
elseif(MSVC)
    target_link_libraries(httplib INTERFACE ws2_32 crypt32)
endif()

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(nlohmann-json)
