add_executable(core-server
    main.cpp
)

target_link_libraries(core-server PRIVATE
    core
    rename
)
