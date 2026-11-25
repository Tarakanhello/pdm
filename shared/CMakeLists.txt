add_library(shared STATIC
    diagnostic/logger.cpp
)

target_sources(shared PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        document.h
        iexcel_reader.h
        imodule.h
        istamp_recognizer.h
        diagnostic/log_level.h
        diagnostic/logger_config.h
        diagnostic/logger.h
        module_id.h
        job_params.h
)

target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/diagnostics
)
