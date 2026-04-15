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

#define KVS_MIN_REGION_MAX_LEN             (128)
#define KVS_MIN_ACCESS_KEY_MAX_LEN         (256)
#define KVS_MIN_SECRET_KEY_MAX_LEN         (256)
#define KVS_MIN_SESSION_TOKEN_MAX_LEN      (2048)
#define KVS_MIN_CERT_PATH_MAX_LEN          (512)
#define KVS_MIN_PRIVATE_KEY_PATH_MAX_LEN   (512)
#define KVS_MIN_CA_CERT_PATH_MAX_LEN       (512)
#define KVS_MIN_STREAM_NAME_MAX_LEN        (256)

typedef struct {
    kvs_minimal_producer_config_t config;
    char region[KVS_MIN_REGION_MAX_LEN];
    char access_key_id[KVS_MIN_ACCESS_KEY_MAX_LEN];
    char secret_access_key[KVS_MIN_SECRET_KEY_MAX_LEN];
    char session_token[KVS_MIN_SESSION_TOKEN_MAX_LEN];
    char cert_path[KVS_MIN_CERT_PATH_MAX_LEN];
    char private_key_path[KVS_MIN_PRIVATE_KEY_PATH_MAX_LEN];
    char ca_cert_path[KVS_MIN_CA_CERT_PATH_MAX_LEN];
    char active_stream_name[KVS_MIN_STREAM_NAME_MAX_LEN];
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
