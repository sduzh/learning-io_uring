cmake_minimum_required(VERSION 3.28)
project(io_uring_samples)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)

find_package(LibUring REQUIRED)
include_directories(${LIBURING_INCLUDE_DIRS})

find_package(fmt CONFIG REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})

find_package(gflags CONFIG REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address -g)
  add_link_options(-fsanitize=address)
endif()

add_executable(cat src/cat.cpp)
target_link_libraries(cat ${LIBURING_LIBRARIES} fmt::fmt)

add_executable(tcp_echo_server src/tcp_echo_server.cpp)
target_link_libraries(tcp_echo_server ${LIBURING_LIBRARIES} fmt::fmt
                      gflags::gflags_static)

add_executable(tcp_echo_server_epoll src/tcp_echo_server_epoll.cpp)
target_link_libraries(tcp_echo_server_epoll fmt::fmt gflags::gflags_static)

add_executable(tcp_echo_bench src/tcp_echo_bench.cpp)
target_link_libraries(tcp_echo_bench ${LIBURING_LIBRARIES} fmt::fmt
                      gflags::gflags_static)

add_executable(test_features src/test_features.cpp)
target_link_libraries(test_features ${LIBURING_LIBRARIES})
