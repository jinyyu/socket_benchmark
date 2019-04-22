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

find_package(PkgConfig REQUIRED)
pkg_check_modules(glib2 glib-2.0 REQUIRED)
include_directories(${glib2_INCLUDE_DIRS})
set(LIBS ${LIBS} ${glib2_LIBRARIES})

add_executable(client src/client.cpp src/ByteBuffer.cpp)
target_link_libraries(client ${LIBS})

add_executable(sync_server src/sync_server.cpp src/ByteBuffer.cpp)
target_link_libraries(sync_server ${LIBS})

add_executable(async_server src/async_server.cpp src/ByteBuffer.cpp)
target_link_libraries(async_server ${LIBS})