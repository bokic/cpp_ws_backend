cmake_minimum_required(VERSION 2.8.11)

project(sqlite-demo)

include_directories(
                    ../../src
                    .
)

add_executable(sqlite-demo
               ../../src/main.cpp
               ../../src/main.cpp
               ../../src/wsserver.cpp
               ../../src/wsworker.cpp
               ../../src/wsregex.cpp 
               ../../src/wsthreadpool.cpp
)

target_link_libraries(sqlite-demo
                      fcgi
                      sqlite3
                      json-c
                      pthread
)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")