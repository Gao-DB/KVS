cmake_minimum_required(VERSION 3.13)
project(kvs_open_source C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(KVS_BUILD_SAMPLES "Build KVS sample applications" ON)

add_library(kvsProducerC STATIC
    src/producer_core.c
    src/client/default_client.c
    src/net/default_network.c
    src/serialization/default_serializer.c
    src/utils/time_utils.c
)

target_include_directories(kvsProducerC
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
)

if(KVS_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
