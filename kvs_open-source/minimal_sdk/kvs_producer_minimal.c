#include "kvs_producer_minimal.h"

#include <stdio.h>
#include <string.h>

static int kvs_minimal_is_ready(kvs_minimal_producer_t* producer)
{
    return (producer != NULL && producer->initialized != 0);
}

int kvs_minimal_producer_init(const kvs_minimal_producer_config_t* cfg, kvs_minimal_producer_t* producer)
{
    if (cfg == NULL || producer == NULL || cfg->region == NULL || cfg->access_key_id == NULL || cfg->secret_access_key == NULL) {
        return -1;
    }

    memset(producer, 0x0, sizeof(*producer));
    producer->config = *cfg;
    producer->initialized = 1;
    return 0;
}

int kvs_minimal_producer_deinit(kvs_minimal_producer_t* producer)
{
    if (producer == NULL) {
        return -1;
    }

    memset(producer, 0x0, sizeof(*producer));
    return 0;
}

int kvs_minimal_producer_create_stream(kvs_minimal_producer_t* producer, const char* stream_name)
{
    if (!kvs_minimal_is_ready(producer) || stream_name == NULL || *stream_name == '\0') {
        return -1;
    }

    snprintf(producer->active_stream_name, sizeof(producer->active_stream_name), "%s", stream_name);
    producer->stream_created = 1;
    printf("[kvs-minimal] CreateStream stream=%s region=%s cert=%s key=%s ca=%s\n",
           producer->active_stream_name,
           producer->config.region,
           producer->config.cert_path != NULL ? producer->config.cert_path : "(null)",
           producer->config.private_key_path != NULL ? producer->config.private_key_path : "(null)",
           producer->config.ca_cert_path != NULL ? producer->config.ca_cert_path : "(null)");
    return 0;
}

int kvs_minimal_producer_put_video_frame(kvs_minimal_producer_t* producer, const void* frame, unsigned long long timestamp_100ns)
{
    if (!kvs_minimal_is_ready(producer) || producer->stream_created == 0 || frame == NULL) {
        return -1;
    }

    (void) timestamp_100ns;
    return 0;
}

int kvs_minimal_producer_put_audio_frame(kvs_minimal_producer_t* producer, const void* frame, unsigned long long timestamp_100ns)
{
    if (!kvs_minimal_is_ready(producer) || producer->stream_created == 0 || frame == NULL) {
        return -1;
    }

    (void) timestamp_100ns;
    return 0;
}

int kvs_minimal_producer_stop_stream(kvs_minimal_producer_t* producer)
{
    if (!kvs_minimal_is_ready(producer)) {
        return -1;
    }

    if (producer->stream_created != 0) {
        printf("[kvs-minimal] StopStream stream=%s\n", producer->active_stream_name);
    }
    producer->stream_created = 0;
    producer->active_stream_name[0] = '\0';
    return 0;
}
