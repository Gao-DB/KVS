#ifndef __KVS_PRODUCER_MINIMAL_H_
#define __KVS_PRODUCER_MINIMAL_H_

#include <stddef.h>

typedef struct {
    const char* region;
    const char* access_key_id;
    const char* secret_access_key;
    const char* session_token;
    const char* cert_path;
    const char* private_key_path;
    const char* ca_cert_path;
} kvs_minimal_producer_config_t;

typedef struct {
    kvs_minimal_producer_config_t config;
    char region[128];
    char access_key_id[256];
    char secret_access_key[256];
    char session_token[2048];
    char cert_path[512];
    char private_key_path[512];
    char ca_cert_path[512];
    char active_stream_name[256];
    int initialized;
    int stream_created;
} kvs_minimal_producer_t;

int kvs_minimal_producer_init(const kvs_minimal_producer_config_t* cfg, kvs_minimal_producer_t* producer);
int kvs_minimal_producer_deinit(kvs_minimal_producer_t* producer);
int kvs_minimal_producer_create_stream(kvs_minimal_producer_t* producer, const char* stream_name);
int kvs_minimal_producer_put_video_frame(kvs_minimal_producer_t* producer, const void* frame, unsigned long long timestamp_100ns);
int kvs_minimal_producer_put_audio_frame(kvs_minimal_producer_t* producer, const void* frame, unsigned long long timestamp_100ns);
int kvs_minimal_producer_stop_stream(kvs_minimal_producer_t* producer);

#endif // !__KVS_PRODUCER_MINIMAL_H_
