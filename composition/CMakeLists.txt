add_library(composition STATIC
    module_factory.h
    module_factory.cpp
    build_core.h
    build_core.cpp
)
target_include_directories(composition PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(composition PUBLIC
    core
    modules
    shared
    infrastructure
)

target_link_libraries(composition PRIVATE
    infrastructure
)

