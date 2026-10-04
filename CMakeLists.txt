cmake_minimum_required(VERSION 3.16)

project(cpp_ws_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -s")

option(BUILD_TESTS "Build demo applications and tests" ON)

find_package(Threads REQUIRED)

add_library(ws_backend STATIC
    src/wsregex.cpp
    src/wsregex.h
    src/wsroute.h
    src/wsserver.cpp
    src/wsserver.h
    src/wsthreadpool.cpp
    src/wsthreadpool.h
    src/wsworker.cpp
    src/wsworker.h
)

target_include_directories(ws_backend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(ws_backend PUBLIC
    Threads::Threads
    fcgi
)

include(GNUInstallDirs)

install(TARGETS ws_backend
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    src/wsregex.h
    src/wsroute.h
    src/wsserver.h
    src/wsthreadpool.h
    src/wsworker.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/backend
)

if(BUILD_TESTS)
    add_subdirectory(tests/sqlite-demo)
endif()
