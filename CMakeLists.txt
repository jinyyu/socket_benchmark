cmake_minimum_required(VERSION 3.5)
project(socket_benchmark)

set(CMAKE_CXX_STANDARD 11)

add_definitions(-Wall)
add_definitions(-DDEBUG)

include_directories(${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})
set(LIBS
        ${LIBS}
        pthread
        Boost::system)

add_executable(client src/client.cpp)
target_link_libraries(client ${LIBS})

add_executable(sync_server src/sync_server.cpp)
target_link_libraries(client ${LIBS})

add_executable(async_server src/async_server.cpp)
target_link_libraries(client ${LIBS})