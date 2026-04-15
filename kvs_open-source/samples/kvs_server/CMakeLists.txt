add_executable(kvs_server
    kvs_main.c
    kvs_service.c
)

target_include_directories(kvs_server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kvs_server
    PRIVATE
        kvsProducerC
        pthread
)
