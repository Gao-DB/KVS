#include "kvs_media.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__)
__attribute__((weak)) int get_video_frame_data(uint8_t **data, int *len, uint64_t *pts)
{
    if (data == NULL || len == NULL || pts == NULL) {
        return -1;
    }
    return -1;
}

__attribute__((weak)) int get_audio_frame_data(uint8_t **data, int *len, uint64_t *pts)
{
    if (data == NULL || len == NULL || pts == NULL) {
        return -1;
    }
    return -1;
}
#endif

int kvs_media_init(void)
{
    return 0;
}

int kvs_media_deinit(void)
{
    return 0;
}

int kvs_media_get_video_frame(kvs_media_frame_t* frame)
{
    if (frame == NULL) {
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    return get_video_frame_data(&frame->data, &frame->len, &frame->pts);
}

int kvs_media_get_audio_frame(kvs_media_frame_t* frame)
{
    if (frame == NULL) {
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    return get_audio_frame_data(&frame->data, &frame->len, &frame->pts);
}
