# Отключаем предупреждения как ошибки для всего, что собирается из third_party
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

add_subdirectory(Catch2)
