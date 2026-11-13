file(GLOB_RECURSE SQL_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/db/*.sql"
)

set(EMBEDDED_SQL_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EMBEDDED_SQL_H "${EMBEDDED_SQL_DIR}/embedded_sql.h")
set(EMBEDDED_SQL_CPP "${EMBEDDED_SQL_DIR}/embedded_sql.cpp")

# Run script at configure time so headers/sources are available immediately
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -DSQL_DIR=${CMAKE_CURRENT_SOURCE_DIR}/db
        -DOUTPUT_H=${EMBEDDED_SQL_H}
        -DOUTPUT_CPP=${EMBEDDED_SQL_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_sql.cmake
)

# Custom command to regenerate at build time if any .sql file changes
add_custom_command(
    OUTPUT ${EMBEDDED_SQL_H} ${EMBEDDED_SQL_CPP}
    COMMAND ${CMAKE_COMMAND}
        -DSQL_DIR=${CMAKE_CURRENT_SOURCE_DIR}/db
        -DOUTPUT_H=${EMBEDDED_SQL_H}
        -DOUTPUT_CPP=${EMBEDDED_SQL_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_sql.cmake
    DEPENDS ${SQL_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_sql.cmake
    COMMENT "Embedding SQL files into binary..."
    VERBATIM
)

add_executable(jiralike-demo
    main.cpp
    src/qrcodegen.cpp
    ${EMBEDDED_SQL_CPP}
    ${EMBEDDED_SQL_H}
)

set_target_properties(jiralike-demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(jiralike-demo PRIVATE
    ${EMBEDDED_SQL_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jiralike-demo PRIVATE
    ws_backend
    json-c
)
