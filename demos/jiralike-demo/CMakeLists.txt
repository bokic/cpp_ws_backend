add_executable(jiralike-demo main.cpp)

set_target_properties(jiralike-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(jiralike-demo PRIVATE
    ws_backend
    sqlite3
    json-c
)
