add_executable(journal-demo main.cpp)

set_target_properties(journal-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(journal-demo PRIVATE
    ws_backend
    json-c
    systemd
)
