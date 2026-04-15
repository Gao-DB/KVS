cmake_minimum_required(VERSION 3.10)
project(kvs_open_source C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(kvs_producer_sdk_minimal
    src/kvs_producer_sdk_minimal.c
)
target_include_directories(kvs_producer_sdk_minimal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(kvs_event_uploader
    samples/kvs_event_uploader.c
)
target_link_libraries(kvs_event_uploader
    PRIVATE
        kvs_producer_sdk_minimal
)
