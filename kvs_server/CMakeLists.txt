cmake_minimum_required(VERSION 3.13)
project(kvs_event_uploader C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(KVS_OPEN_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../kvs_open-source")

if (NOT EXISTS "${KVS_OPEN_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "amazon-kinesis-video-streams-producer-c SDK is expected at ${KVS_OPEN_SOURCE_DIR}")
endif()

add_subdirectory("${KVS_OPEN_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/kvs_open_source")

add_executable(kvs_event_uploader
    kvs_main.c
    kvs_service.c
    kvs_media.c
)

target_include_directories(kvs_event_uploader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${KVS_OPEN_SOURCE_DIR}/src/include"
    "${KVS_OPEN_SOURCE_DIR}/samples"
)

target_link_libraries(kvs_event_uploader PRIVATE
    kvsProducerC
    pthread
)
