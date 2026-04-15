#ifndef KVS_PRODUCER_SDK_H
#define KVS_PRODUCER_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KvsProducerClient KvsProducerClient;
typedef struct KvsStreamHandle KvsStreamHandle;

typedef struct {
    const char *region;
    const char *access_key;
    const char *secret_key;
    const char *session_token;
} KvsProducerConfig;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint64_t pts_ms;
    bool is_key_frame;
    const char *track_name;
} KvsMediaFrame;

int kvs_producer_client_create(const KvsProducerConfig *config, KvsProducerClient **client);
void kvs_producer_client_free(KvsProducerClient *client);

int kvs_create_stream(KvsProducerClient *client, const char *stream_name, KvsStreamHandle **stream);
int kvs_put_media_frame(KvsStreamHandle *stream, const KvsMediaFrame *frame);
int kvs_stop_stream(KvsStreamHandle **stream);

uint64_t kvs_get_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif
