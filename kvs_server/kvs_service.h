#ifndef KVS_SERVICE_H
#define KVS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define KVS_SERVICE_STATUS_OK 0
#define KVS_SERVICE_STATUS_ERR -1

typedef struct {
    char stream_name[128];
    char encode_type[16];
    int enable;
    int resolution;
    int fps;
    int gop;
    int bps_mode;
    int bps;
} kvs_video_cfg_t;

typedef struct {
    int enable;
    int type;
    int sample_rate;
    int bit_width;
    int channel_mode;
    int volume;
    int audio_local_enable;
} kvs_audio_cfg_t;

typedef struct {
    int enable;
    kvs_video_cfg_t video_encode[3];
    kvs_audio_cfg_t audio_encode;
} kvs_service_cfg_t;

int kvs_service_cfg_get(kvs_service_cfg_t* cfg);

int kvs_service_start(void);
int kvs_service_stop(void);
int kvs_service_trigger_event_upload(void);

int kvs_service_get_status(void);
void kvs_service_set_status(int status);

#endif
