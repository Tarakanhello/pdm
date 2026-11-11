add_executable(crop-tool main.cpp)
target_link_libraries(crop-tool PRIVATE
    stb
    utilities
)

target_include_directories(crop-tool PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/utilities
)
