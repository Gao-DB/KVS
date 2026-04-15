cmake_minimum_required(VERSION 3.16)
project(kvs_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(KVS_ENABLE_PRODUCER_SDK "Enable amazon-kinesis-video-streams-producer-c integration" ON)
set(KVS_PRODUCER_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../kvs_open-source" CACHE PATH "Path to amazon-kinesis-video-streams-producer-c")

add_executable(kvs_event_uploader
    kvs_main.c
    kvs_media.c
    kvs_service.c
    kvs_service_cfg.c
)

target_include_directories(kvs_event_uploader
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(kvs_event_uploader PRIVATE Threads::Threads)

if(KVS_ENABLE_PRODUCER_SDK)
    if(EXISTS "${KVS_PRODUCER_SDK_ROOT}/CMakeLists.txt")
        message(STATUS "KVS producer SDK root detected at ${KVS_PRODUCER_SDK_ROOT}")
        target_compile_definitions(kvs_event_uploader PRIVATE KVS_PRODUCER_SDK_AVAILABLE=1)
    else()
        message(WARNING "amazon-kinesis-video-streams-producer-c sources are not complete under ${KVS_PRODUCER_SDK_ROOT}. Add SDK (e.g. git submodule) to enable real upload linkage.")
    endif()
endif()
