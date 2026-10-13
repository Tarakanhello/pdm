add_executable(pdm-tests
    test_main.cpp
)

target_link_libraries(pdm-tests
    PRIVATE
        Catch2::Catch2WithMain
)
