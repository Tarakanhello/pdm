add_library(shared INTERFACE)

target_sources(shared INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        document.h
        iexcel_reader.h
        imodule.h
        istamp_recognizer.h
        module_id.h
        job_params.h
)


target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
