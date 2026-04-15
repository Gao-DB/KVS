#ifndef __KVS_MEDIA_H_
#define __KVS_MEDIA_H_
#ifdef __cplusplus
 extern "C" {
#endif

#include "ringbuf.h"

#define FMTP_MAX_SIZE 		400
#define FRAME_BUFF_MAX_SIZE	512*1024

MapInfo_t* rtp_get_video_stream0(void);

MapInfo_t* rtp_get_video_stream1(void);

MapInfo_t* rtp_get_audio_stream(void);

int open_video_stream(int streamerNum);
int open_audio_stream(void);
int close_stream(void);
int get_video_frame_data(int stream_id, int* index, FrameData_t* Frame);
int get_audio_frame_data(MapInfo_t *map_info,int* index, FrameData_t* Frame);
int open_local_file(const char* filePath);
#ifdef __cplusplus
	}
#endif


#endif // !__KVS_MEDIA_H_