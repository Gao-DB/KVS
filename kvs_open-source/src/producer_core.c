#include <stdlib.h>
#include <string.h>

#include "internal/kvs_producer_internal.h"

struct KvsProducerClient {
    KvsProducerConfig config;
};

struct KvsStreamHandle {
    KvsProducerClient *client;
    char stream_name[KVS_STREAM_NAME_MAX_LEN];
    uint64_t created_at_ms;
    uint64_t frame_count;
    int stopped;
};

int kvs_producer_client_create(const KvsProducerConfig *config, KvsProducerClient **client)
{
    KvsProducerClient *new_client = NULL;

    if (client == NULL || kvs_client_validate_config(config) != 0) {
        return -1;
    }

    new_client = (KvsProducerClient *) calloc(1, sizeof(KvsProducerClient));
    if (new_client == NULL) {
        return -1;
    }

    kvs_client_copy_config(&new_client->config, config);
    *client = new_client;
    return 0;
}

void kvs_producer_client_free(KvsProducerClient *client)
{
    free(client);
}

int kvs_create_stream(KvsProducerClient *client, const char *stream_name, KvsStreamHandle **stream)
{
    KvsStreamHandle *new_stream = NULL;
    char payload[KVS_PAYLOAD_MAX_LEN];

    if (client == NULL || stream_name == NULL || stream == NULL || stream_name[0] == '\0') {
        return -1;
    }

    new_stream = (KvsStreamHandle *) calloc(1, sizeof(KvsStreamHandle));
    if (new_stream == NULL) {
        return -1;
    }

    if (strlen(stream_name) >= sizeof(new_stream->stream_name)) {
        free(new_stream);
        return -1;
    }

    if (kvs_default_serialize_create_stream(stream_name, payload, sizeof(payload)) != 0) {
        free(new_stream);
        return -1;
    }

    if (kvs_default_network_send_request("CreateStream", payload) != 0) {
        free(new_stream);
        return -1;
    }

    new_stream->client = client;
    new_stream->created_at_ms = kvs_time_now_ms();
    memcpy(new_stream->stream_name, stream_name, strlen(stream_name) + 1);

    *stream = new_stream;
    return 0;
}

int kvs_put_media_frame(KvsStreamHandle *stream, const KvsMediaFrame *frame)
{
    char payload[KVS_PAYLOAD_MAX_LEN];

    if (stream == NULL || frame == NULL || frame->data == NULL || frame->size == 0 || stream->stopped) {
        return -1;
    }

    if (kvs_default_serialize_frame(stream->stream_name, frame, payload, sizeof(payload)) != 0) {
        return -1;
    }

    if (kvs_default_network_send_request("PutMedia", payload) != 0) {
        return -1;
    }

    stream->frame_count++;
    return 0;
}

int kvs_stop_stream(KvsStreamHandle *stream)
{
    if (stream == NULL) {
        return -1;
    }

    stream->stopped = 1;
    free(stream);
    return 0;
}

uint64_t kvs_get_time_ms(void)
{
    return kvs_time_now_ms();
}
