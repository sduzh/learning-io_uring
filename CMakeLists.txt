cmake_minimum_required(VERSION 3.28)
project(io_uring_samples)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)

find_package(LibUring REQUIRED)
include_directories(${LIBURING_INCLUDE_DIRS})

find_package(fmt CONFIG REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})

add_executable(cat src/cat.cpp)
target_link_libraries(cat ${LIBURING_LIBRARIES} fmt::fmt)
