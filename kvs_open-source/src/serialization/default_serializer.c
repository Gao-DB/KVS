#include <inttypes.h>
#include <stdio.h>

#include "../internal/kvs_producer_internal.h"

int kvs_default_serialize_create_stream(const char *stream_name, char *buffer, size_t buffer_len)
{
    int written = 0;

    if (stream_name == NULL || buffer == NULL || buffer_len == 0) {
        return -1;
    }

    written = snprintf(buffer, buffer_len, "{\"StreamName\":\"%s\"}", stream_name);
    if (written < 0 || (size_t) written >= buffer_len) {
        return -1;
    }

    return 0;
}

int kvs_default_serialize_frame(const char *stream_name, const KvsMediaFrame *frame, char *buffer, size_t buffer_len)
{
    int written = 0;

    if (stream_name == NULL || frame == NULL || buffer == NULL || buffer_len == 0) {
        return -1;
    }

    written = snprintf(buffer,
                       buffer_len,
                       "{\"StreamName\":\"%s\",\"Track\":\"%s\",\"PtsMs\":%" PRIu64 ",\"Size\":%zu,\"KeyFrame\":%s}",
                       stream_name,
                       frame->track_name != NULL ? frame->track_name : "video",
                       frame->pts_ms,
                       frame->size,
                       frame->is_key_frame ? "true" : "false");
    if (written < 0 || (size_t) written >= buffer_len) {
        return -1;
    }

    return 0;
}
