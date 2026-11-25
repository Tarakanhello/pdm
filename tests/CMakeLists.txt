add_executable(pdm-tests
    test_main.cpp
    test_document.cpp
    test_logger.cpp
)

target_link_libraries(pdm-tests
    PRIVATE
        Catch2::Catch2WithMain
        shared
)
