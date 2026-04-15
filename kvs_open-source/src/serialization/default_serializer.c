#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "../internal/kvs_producer_internal.h"

#define KVS_JSON_UNICODE_ESCAPE_LEN 7
#define KVS_JSON_ESCAPE_EXPANSION_RATIO 6

static int kvs_json_escape_string(const char *input, char *output, size_t output_len)
{
    size_t out_idx = 0;
    size_t i = 0;

    if (input == NULL || output == NULL || output_len == 0) {
        return -1;
    }

    for (i = 0; input[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char) input[i];
        const char *escaped = NULL;
        char escape_buffer[KVS_JSON_UNICODE_ESCAPE_LEN];
        size_t escaped_len = 0;

        switch (c) {
            case '\"':
                escaped = "\\\"";
                break;
            case '\\':
                escaped = "\\\\";
                break;
            case '\b':
                escaped = "\\b";
                break;
            case '\f':
                escaped = "\\f";
                break;
            case '\n':
                escaped = "\\n";
                break;
            case '\r':
                escaped = "\\r";
                break;
            case '\t':
                escaped = "\\t";
                break;
            default:
                escaped = NULL;
                break;
        }

        if (escaped != NULL) {
            escaped_len = strlen(escaped);
            if (out_idx + escaped_len >= output_len) {
                return -1;
            }
            memcpy(&output[out_idx], escaped, escaped_len);
            out_idx += escaped_len;
            continue;
        }

        if (c < 0x20U) {
            int written = snprintf(escape_buffer, sizeof(escape_buffer), "\\u%04x", c);
            if (written < 0 || (size_t) written >= sizeof(escape_buffer)) {
                return -1;
            }
            if (out_idx + (size_t) written >= output_len) {
                return -1;
            }
            memcpy(&output[out_idx], escape_buffer, (size_t) written);
            out_idx += (size_t) written;
            continue;
        }

        if (out_idx + 1 >= output_len) {
            return -1;
        }
        output[out_idx++] = (char) c;
    }

    output[out_idx] = '\0';
    return 0;
}

int kvs_default_serialize_create_stream(const char *stream_name, char *buffer, size_t buffer_len)
{
    char escaped_stream_name[KVS_STREAM_NAME_MAX_LEN * KVS_JSON_ESCAPE_EXPANSION_RATIO];
    int written = 0;

    if (stream_name == NULL || buffer == NULL || buffer_len == 0) {
        return -1;
    }

    if (kvs_json_escape_string(stream_name, escaped_stream_name, sizeof(escaped_stream_name)) != 0) {
        return -1;
    }

    written = snprintf(buffer, buffer_len, "{\"StreamName\":\"%s\"}", escaped_stream_name);
    if (written < 0 || (size_t) written >= buffer_len) {
        return -1;
    }

    return 0;
}

int kvs_default_serialize_frame(const char *stream_name, const KvsMediaFrame *frame, char *buffer, size_t buffer_len)
{
    char escaped_stream_name[KVS_STREAM_NAME_MAX_LEN * KVS_JSON_ESCAPE_EXPANSION_RATIO];
    char escaped_track_name[KVS_TRACK_NAME_MAX_LEN * KVS_JSON_ESCAPE_EXPANSION_RATIO];
    const char *track_name = frame != NULL && frame->track_name != NULL ? frame->track_name : "video";
    int written = 0;

    if (stream_name == NULL || frame == NULL || buffer == NULL || buffer_len == 0) {
        return -1;
    }

    if (kvs_json_escape_string(stream_name, escaped_stream_name, sizeof(escaped_stream_name)) != 0 ||
        kvs_json_escape_string(track_name, escaped_track_name, sizeof(escaped_track_name)) != 0) {
        return -1;
    }

    written = snprintf(buffer,
                       buffer_len,
                       "{\"StreamName\":\"%s\",\"Track\":\"%s\",\"PtsMs\":%" PRIu64 ",\"Size\":%zu,\"KeyFrame\":%s}",
                       escaped_stream_name,
                       escaped_track_name,
                       frame->pts_ms,
                       frame->size,
                       frame->is_key_frame ? "true" : "false");
    if (written < 0 || (size_t) written >= buffer_len) {
        return -1;
    }

    return 0;
}
