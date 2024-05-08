cmake_minimum_required(VERSION 3.16)

project(sqlite-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

include_directories(
    ../../src
    .
)

add_executable(sqlite-demo
    webapp.h
    ../../src/main.cpp
    ../../src/wsserver.cpp          ../../src/wsserver.h
    ../../src/wsworker.cpp          ../../src/wsworker.h
    ../../src/wsregex.cpp           ../../src/wsregex.h
    ../../src/wsthreadpool.cpp      ../../src/wsthreadpool.h
)

target_link_libraries(sqlite-demo
    fcgi
    sqlite3
    json-c
    pthread
    systemd
)
