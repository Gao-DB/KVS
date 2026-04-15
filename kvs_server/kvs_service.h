#ifndef KVS_SERVICE_H
#define KVS_SERVICE_H

#include "system_config.h"

#define STDERR_OUT_LEN64 64
#define STDERR_OUT_LEN512 512

#define KVS_SERVICE_STATUS_OK    (0)
#define KVS_SERVICE_STATUS_ERR   (-1)

 //kvs service config  
typedef struct
{
	int Enable;
	VencCfg 			Video_Encode[MAX_STREAM_NUM];		//Max support 3 line stream
    AencCfg				audio_Encode;		//audio encode
}kvs_service_cfg_t;


int kvs_service_get_status(void);

void kvs_service_set_status(int status);

#endif // KVS_SERVICE_H 