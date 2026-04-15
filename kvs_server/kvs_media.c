#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include "ringbuf.h"

#include "log_module/fii_log.h"

MapInfo_t *g_videoStream0Ptr=NULL;
MapInfo_t *g_videoStream1Ptr=NULL;
MapInfo_t *g_audioStreamPtr=NULL;
pthread_mutex_t ringbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

MapInfo_t* rtp_get_video_stream0(void)
{
	return g_videoStream0Ptr;
}

MapInfo_t* rtp_get_video_stream1(void)
{
	return g_videoStream1Ptr;
}

MapInfo_t* rtp_get_audio_stream(void)
{
	return g_audioStreamPtr;
}

//----------------------------------------------------------------------------
//function: 	open_video_stream
//description:  open the video streamer 
//parameter:	[IN]    int streamerNum  ----   in [0,1])
//              [OUT]   videoStreamPtr  ----  video streamer point
// return:	    int  ----  return 0 if sucessed  , return -1 if failed
// Date:   					author: Stephen He
//----------------------------------------------------------------------------
int open_video_stream(int streamerNum)
{
	char file_path[128] = { 0 };
	memset(file_path, 0x00, 128);
	int ret=0;
	pthread_mutex_lock(&ringbuf_mutex);  
	switch (streamerNum)
	{
		case 0:
			sprintf(file_path, "/tmp/v%d", 0);
			if(g_videoStream0Ptr == NULL)
			{
				g_videoStream0Ptr = FrameBufferOpen(file_path, MS_K_FRAME_NUM, MS_K_FRAME_SIZE, MS_N_FRAME_NUM, MS_N_FRAME_SIZE, 0);

				if (g_videoStream0Ptr == NULL)
				{	
					fii_log_error("FrameBufferOpen fail.\n");
					ret= -1;
				}			
			}
			break;
		case 1:
			sprintf(file_path, "/tmp/v%d", 1);
			if(g_videoStream1Ptr == NULL)
			{
				g_videoStream1Ptr = FrameBufferOpen(file_path, SS0_K_FRAME_NUM, SS0_K_FRAME_SIZE, SS0_N_FRAME_NUM, SS0_N_FRAME_SIZE, 0);
				if (g_videoStream1Ptr == NULL)
				{	
					fii_log_error("FrameBufferOpen fail.\n");
					ret= -1;
				}
			}
			break;
		default:
			ret= -1;
			break;
	}
	pthread_mutex_unlock(&ringbuf_mutex);
	return ret;
}


//----------------------------------------------------------------------------
//function: 	open_audio_stream
//description:  open audio streamer
//parameter:	[IN]    none
//              [OUT]   audioStreamPtr  ----  audio streamer point
// return:	    int  ----  return 0 if sucessed  , return -1 if failed
// Date:   					author: Stephen He
//----------------------------------------------------------------------------
int open_audio_stream(void)
{
	char file_path[128]={ 0 };
	memset(file_path, 0x00, 128);
	sprintf(file_path, "/tmp/a0");
	pthread_mutex_lock(&ringbuf_mutex);
	if(g_audioStreamPtr == NULL)
	{
		g_audioStreamPtr = FrameBufferOpen(file_path, AU_K_FRAME_NUM, AU_K_FRAME_SIZE, AU_N_FRAME_NUM, AU_N_FRAME_SIZE, 0);
		if (g_audioStreamPtr == NULL)
		{	
			fii_log_error("FrameBufferOpen  audio failed\n");
			pthread_mutex_unlock(&ringbuf_mutex);
			
			return -1;
		}
	}
	pthread_mutex_unlock(&ringbuf_mutex);
	return 0;
}



//----------------------------------------------------------------------------
//function: 	close_stream
//description:  close all stream 
//parameter:	[IN]    none
//              [OUT]   none
// return:	    none 
// Date:   					author: Stephen He
//----------------------------------------------------------------------------
int close_stream(void)
{
	printf("close_stream\n");
	pthread_mutex_lock(&ringbuf_mutex);
	MapInfo_t *tmp_StreamPtr=NULL;
	int ret=0;
	if(g_audioStreamPtr!=NULL)
	{
		tmp_StreamPtr=g_audioStreamPtr;
		g_audioStreamPtr=NULL;
		ret=FrameBufferClose(tmp_StreamPtr, 0);
		
	}
	if(g_videoStream0Ptr!=NULL)
	{
		tmp_StreamPtr=g_videoStream0Ptr;
		g_videoStream0Ptr=NULL;
		ret=FrameBufferClose(tmp_StreamPtr, 0);

	}
	if(g_videoStream1Ptr!=NULL)
	{
		tmp_StreamPtr=g_videoStream1Ptr;
		g_videoStream1Ptr=NULL;
		ret=FrameBufferClose(tmp_StreamPtr, 0);
	}
	pthread_mutex_unlock(&ringbuf_mutex);
	return ret;

}


//----------------------------------------------------------------------------
//function: 	get_video_frame_data
//description:  get  video frame data,
//parameter:	[IN]   int index  ----  read index
//              [OUT]    FrameData_t* Frame  ----  frame data
// return:	    int  ----  the status of read 
// Date:   					author: Stephen He
//----------------------------------------------------------------------------
int get_video_frame_data(int stream_id,int* index, FrameData_t* Frame)
{
	int ret = -1;
	pthread_mutex_lock(&ringbuf_mutex);
	MapInfo_t* videoStreamPtr = NULL;
	switch (stream_id)
	{
		case 0:
			videoStreamPtr = g_videoStream0Ptr;
			break;
		case 1:
			videoStreamPtr = g_videoStream1Ptr;
			break;
		default:
			fii_log_error("stream_id fail.\n");
			break;
		
	}
	if(videoStreamPtr==NULL)
	{
		pthread_mutex_unlock(&ringbuf_mutex);
		//printf(" %s %d get_video_frame_data fail.\n", __func__, __LINE__);
		return -1;
	}
	
	ret = FrameBufferRead(videoStreamPtr, index, Frame);
	pthread_mutex_unlock(&ringbuf_mutex);
	if (ret < 0)
	{	
		// printf("FrameBufferRead video buffer error.\n");
		usleep(10*1000);
	}
	
	return ret;
}




//----------------------------------------------------------------------------
//function: 	get_audio_frame_data
//description:  get audio frame data 
//parameter:	[IN]    int index  ----  read index
//              [OUT]    FrameData_t* Frame  ----  audio frame data
// return:	    int  ----  the status of read 
// Date:   					author: Stephen He
//----------------------------------------------------------------------------
int get_audio_frame_data(MapInfo_t *map_info,int* index, FrameData_t* Frame)
{
	pthread_mutex_lock(&ringbuf_mutex);
	if(map_info==NULL)
	{
		pthread_mutex_unlock(&ringbuf_mutex);
		 //printf(" %s %d get_audio_frame_data fail.\n", __func__, __LINE__);
		return -1;
	}
	int ret = FrameBufferRead(map_info, index, Frame);
	pthread_mutex_unlock(&ringbuf_mutex);
	if (ret < 0)
	{	
		// printf("FrameBufferRead audio buffer error.\n");
		usleep(10*1000);
	}
	
	return ret;
}


