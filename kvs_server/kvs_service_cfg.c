#include "kvs_service.h"

#include <string.h>

int kvs_service_cfg_get(kvs_service_cfg_t* cfg)
{
    if (cfg == NULL) {
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->enable = 1;

    cfg->video_encode[0].enable = 1;
    cfg->video_encode[0].resolution = 1;
    cfg->video_encode[0].fps = 25;
    cfg->video_encode[0].gop = 60;
    cfg->video_encode[0].bps_mode = 0;
    cfg->video_encode[0].bps = 2048;
    strncpy(cfg->video_encode[0].stream_name, "kvs-main-0", sizeof(cfg->video_encode[0].stream_name) - 1);
    strncpy(cfg->video_encode[0].encode_type, "H264", sizeof(cfg->video_encode[0].encode_type) - 1);

    cfg->audio_encode.enable = 1;
    cfg->audio_encode.sample_rate = 8000;
    cfg->audio_encode.bit_width = 16;
    cfg->audio_encode.channel_mode = 0;

    return 0;
}
