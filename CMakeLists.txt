# [....... [..[..[.. [..
#        [..  [..[.    [..
#       [..   [..[.     [..
#     [..     [..[... [.
#    [..      [..[.     [..
#  [..        [..[.      [.
# [...........[..[.... [..

cmake_minimum_required(VERSION 3.7...3.18 FATAL_ERROR)

project(ZIB, CXX)

set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-DRELEASE -O3 -g")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -fuse-linker-plugin")

enable_testing()

add_executable(test-mpsc_queues test/test-mpsc_queues.cpp)

target_compile_options(test-mpsc_queues PUBLIC
    -pthread
    -fcoroutines
    -g
    -Wall
    -Wextra
)


target_link_libraries(
        test-mpsc_queues PUBLIC
        -lpthread -latomic
    )

target_include_directories(test-mpsc_queues PUBLIC
    includes
)

add_test(
    NAME test-mpsc_queues
    COMMAND test-mpsc_queues
)

add_executable(mpsc-benchmark benchmarks/benchmarks.cpp)

target_compile_options(mpsc-benchmark PUBLIC
    -pthread
    -fcoroutines
    -g
    -Wall
    -Wextra
)


target_link_libraries(
        mpsc-benchmark PUBLIC
        -lpthread -latomic
    )

target_include_directories(mpsc-benchmark PUBLIC
    includes
    benchmarks
)

