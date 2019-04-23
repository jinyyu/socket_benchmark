cmake_minimum_required(VERSION 3.5)
project(socket_benchmark)

set(CMAKE_CXX_STANDARD 11)

add_definitions(-DDEBUG)

include_directories(${CMAKE_SOURCE_DIR})

find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})
set(LIBS
        ${LIBS}
        pthread
        Boost::system)

find_package(PkgConfig REQUIRED)
pkg_check_modules(thr
        glib-2.0 REQUIRED
        hiredis REQUIRED
        )
include_directories(${thr_INCLUDE_DIRS})
set(LIBS ${LIBS} ${thr_LIBRARIES})


set(common_SRCS
        src/common.cpp
        src/ByteBuffer.cpp
        )


add_library(comm++ ${common_SRCS})
target_link_libraries(comm++ ${LIBS})


add_executable(client src/client.cpp)
target_link_libraries(client comm++ ${LIBS})

add_executable(sync_server src/sync_server.cpp)
target_link_libraries(sync_server comm++ ${LIBS})

add_executable(async_server src/async_server.cpp)
target_link_libraries(async_server comm++ ${LIBS})