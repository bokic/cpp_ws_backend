# embed_sql.cmake
# Generates embedded_sql.h and embedded_sql.cpp containing all .sql files embedded into binary.
if(NOT DEFINED SQL_DIR)
    message(FATAL_ERROR "SQL_DIR must be defined")
endif()
if(NOT DEFINED OUTPUT_H)
    message(FATAL_ERROR "OUTPUT_H must be defined")
endif()
if(NOT DEFINED OUTPUT_CPP)
    message(FATAL_ERROR "OUTPUT_CPP must be defined")
endif()

file(GLOB_RECURSE SQL_FILES "${SQL_DIR}/*.sql")
list(SORT SQL_FILES)

set(HEADER_CONTENT "// Generated file - DO NOT EDIT
#pragma once
#include <string_view>
#include <vector>

namespace db::embedded {

// Retrieve embedded SQL content by file path or basename (e.g. \"001_init.sql\", \"db/migrations/001_init.sql\", \"schema.sql\")
std::string_view get_sql(std::string_view name);

// Check if embedded SQL exists for the given name
bool has_sql(std::string_view name);

// Returns all registered embedded SQL names
const std::vector<std::string_view> &all_filenames();

} // namespace db::embedded
")

set(CPP_CONTENT "// Generated file - DO NOT EDIT
#include \"embedded_sql.h\"
#include <string_view>
#include <unordered_map>
#include <vector>

namespace db::embedded {
namespace {

")

set(MAP_ENTRIES "")
set(FILENAMES_ENTRIES "")
set(IDX 0)

foreach(SQL_FILE ${SQL_FILES})
    file(READ "${SQL_FILE}" SQL_DATA)
    # Relative path from SQL_DIR
    file(RELATIVE_PATH REL_PATH "${SQL_DIR}" "${SQL_FILE}")
    # Filename only
    get_filename_component(BASE_NAME "${SQL_FILE}" NAME)

    # Variable name
    set(VAR_NAME "kSql_${IDX}")
    math(EXPR IDX "${IDX} + 1")

    # Embed SQL verbatim using raw string literal
    string(APPEND CPP_CONTENT "static constexpr char ${VAR_NAME}[] = R\"SQL_EMBED_DELIM(${SQL_DATA})SQL_EMBED_DELIM\";\n\n")

    string(APPEND MAP_ENTRIES "        {\"db/${REL_PATH}\", std::string_view(${VAR_NAME}, sizeof(${VAR_NAME}) - 1)},\n")
    if(NOT "${REL_PATH}" STREQUAL "${BASE_NAME}")
        string(APPEND MAP_ENTRIES "        {\"${REL_PATH}\", std::string_view(${VAR_NAME}, sizeof(${VAR_NAME}) - 1)},\n")
    endif()
    string(APPEND MAP_ENTRIES "        {\"${BASE_NAME}\", std::string_view(${VAR_NAME}, sizeof(${VAR_NAME}) - 1)},\n")

    string(APPEND FILENAMES_ENTRIES "        \"db/${REL_PATH}\",\n")
endforeach()

string(APPEND CPP_CONTENT "} // namespace

std::string_view get_sql(std::string_view name) {
    static const std::unordered_map<std::string_view, std::string_view> kSqlMap = {
${MAP_ENTRIES}    };
    auto it = kSqlMap.find(name);
    if (it != kSqlMap.end()) {
        return it->second;
    }
    return {};
}

bool has_sql(std::string_view name) {
    return !get_sql(name).empty();
}

const std::vector<std::string_view> &all_filenames() {
    static const std::vector<std::string_view> kFilenames = {
${FILENAMES_ENTRIES}    };
    return kFilenames;
}

} // namespace db::embedded
")

# Ensure output directory exists
get_filename_component(OUT_DIR "${OUTPUT_H}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")

file(WRITE "${OUTPUT_H}" "${HEADER_CONTENT}")
file(WRITE "${OUTPUT_CPP}" "${CPP_CONTENT}")
