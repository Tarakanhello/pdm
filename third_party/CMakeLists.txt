# Отключаем предупреждения как ошибки для всего, что собирается из third_party
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

add_subdirectory(Catch2)
add_subdirectory(QXlsx/QXlsx)

add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cpp-httplib)

# Для работы с сетью на Windows/MinGW требуются системные библиотеки
if(MINGW)
    target_link_libraries(httplib INTERFACE ws2_32 crypt32 cryptui)
elseif(MSVC)
    target_link_libraries(httplib INTERFACE ws2_32 crypt32)
endif()

# --- nlohmann/json (header-only) ---
# У json есть свой CMakeLists.txt, поэтому просто добавляем как подкаталог
set(JSON_BuildTests OFF CACHE INTERNAL "") # Отключаем тесты, чтобы не тратить время
add_subdirectory(nlohmann-json)


add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(stb_image_resize INTERFACE)
target_include_directories(stb_image_resize SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image_resize)
