add_executable(sqlite-demo main.cpp)

set_target_properties(sqlite-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(sqlite-demo PRIVATE
    ws_backend
    sqlite3
    json-c
)
