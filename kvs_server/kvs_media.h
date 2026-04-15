#ifndef KVS_MEDIA_H
#define KVS_MEDIA_H

#include <stdint.h>

/* External ringbuf interfaces provided by upper layer. */
int get_video_frame_data(uint8_t **data, int *len, uint64_t *pts);
int get_audio_frame_data(uint8_t **data, int *len, uint64_t *pts);

typedef struct {
    uint8_t* data;
    int len;
    uint64_t pts;
} kvs_media_frame_t;

int kvs_media_init(void);
int kvs_media_deinit(void);
int kvs_media_get_video_frame(kvs_media_frame_t* frame);
int kvs_media_get_audio_frame(kvs_media_frame_t* frame);

#endif
