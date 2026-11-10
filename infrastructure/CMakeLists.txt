add_library(infrastructure STATIC
    qxlsx_reader.h
    mock_stamp_recognizer.h

)

target_include_directories(infrastructure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(infrastructure PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(infrastructure PUBLIC
    shared
    Qt6::Core
    Qt6::Gui
    httplib
    nlohmann_json::nlohmann_json
)
