add_library(modules INTERFACE)

add_subdirectory(rename)

target_include_directories(modules INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(modules INTERFACE
    rename
)
