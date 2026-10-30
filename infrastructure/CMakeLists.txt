add_library(infrastructure STATIC
    qxlsx_reader.h
)

target_include_directories(infrastructure PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(infrastructure PUBLIC
    shared
    QXlsx::QXlsx)
