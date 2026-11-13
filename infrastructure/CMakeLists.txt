add_library(infrastructure STATIC
    qxlsx_reader.h
    mock_stamp_recognizer.h
    image.h
    image.cpp
    vlm_stamp_recognizer.h
    vlm_stamp_recognizer.cpp
)

target_include_directories(infrastructure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(infrastructure PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(infrastructure PUBLIC
    shared
    stb
    stb_image_resize
    httplib
    nlohmann_json::nlohmann_json
)

# Ускоряем сборку: предварительно компилируем тяжелые библиотеки
target_precompile_headers(infrastructure PRIVATE
    <httplib.h>
    <nlohmann/json.hpp>
)

