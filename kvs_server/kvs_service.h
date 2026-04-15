#ifndef KVS_SERVICE_H
#define KVS_SERVICE_H

#include "system_config.h"

#define STDERR_OUT_LEN64 64
#define STDERR_OUT_LEN512 512

#define KVS_SERVICE_STATUS_OK    (0)
#define KVS_SERVICE_STATUS_ERR   (-1)

typedef struct
{
    char m_StreamName[COMMON_STRING_LEN];
    char m_EncodeType[COMMON_STRING_LEN];
    int  m_Enable;
    int  m_Resolution;  //0 :for D1(960*540), 1:720P(1280*720) 2:1080P(1920*1080) 3:300w(2304*1296) 4:400w:2K(2560*1440) 5:4K(3840*2160)
    int  m_FPS;
    int  m_GOP;
    int  m_mBpsMode;    //0: VBR; 1:CBR mode;
    int  m_mBps;	
}VencCfg;
// 4.7 Audio encode function config
typedef struct
{
    int m_Enable;
    int m_Type;             //0:G711A 1:G711U 2:G722 3:G726
    int m_SampleRate;		//in:8000;11025;12000;16000;22050;24000;32000;44100;48000;64000;96000;
    int m_bit_Width;		//sample bit width : 8,16,24;
    int m_Channel_Mode;     //0:MONO 1:STEREO
    int m_Volume;			// [0,100]
    int m_audio_local_enable;  // sound location
}AencCfg;

 //kvs service config  
typedef struct
{
	int Enable;
	VencCfg 			Video_Encode[MAX_STREAM_NUM];		//Max support 3 line stream
    AencCfg				audio_Encode;		//audio encode
}kvs_service_cfg_t;


int kvs_service_get_status(void);

void kvs_service_set_status(int status);

int kvs_service_start(void);

int kvs_service_stop(void);

int kvs_init(void);

int kvs_exit(void);

int kvs_service_trigger_event(const char *event_filename);

#endif // KVS_SERVICE_H 
