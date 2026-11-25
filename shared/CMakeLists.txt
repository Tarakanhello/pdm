find_package(Threads REQUIRED)

add_library(shared STATIC
    diagnostics/logger.cpp
)

target_sources(shared PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        document.h
        iexcel_reader.h
        imodule.h
        istamp_recognizer.h
        module_id.h
        job_params.h
        diagnostics/log_level.h
        diagnostics/logger_config.h
        diagnostics/logger.h
)

target_link_libraries(shared PUBLIC Threads::Threads)

target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics
)
