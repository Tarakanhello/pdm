add_library(rename STATIC
    rename.h
    rename.cpp
    rename_keys.h
)

target_include_directories(rename PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rename PUBLIC
    shared
)
