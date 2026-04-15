#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <ucontext.h>
#include <stdint.h>
#include <netinet/in.h>

#include "kvs_service.h"
#include "kvs_msg.h"

static pthread_t kvs_pthread_id = 0;
static bool g_kvs_exit = false;
static pthread_t kvs_select_pthrid = 0;
static bool g_kvs_select_exit = false;

int g_kvs_service_status = 0;

int kvs_service_get_status(void)
{
	return g_kvs_service_status;
}

void kvs_service_set_status(int status)
{
	g_kvs_service_status = status;
	return;
}

//sigal handler for SIGSEGV and SIGBUS to log the crash information and exit immediately to avoid potential secondary crashes during cleanup.
static ssize_t safe_write(int fd, const void *buf, size_t count)
{
    size_t written = 0;
    const char *ptr = (const char *)buf;

    while (written < count)
    {
        ssize_t ret = write(fd, ptr + written, count - written);
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue; //Interrupted by signal, retry
            }

            //Dealing with unrecoverable errors
            switch (errno)
            {
                case EPIPE:
                    fprintf(stderr, "Broken pipe, closing fd %d\n", fd);
                    close(fd);
                    break;
                case ENOSPC:
                    fprintf(stderr, "Disk full, aborting write\n");
                    break;
                default:
                    fprintf(stderr, "Write failed: %s\n", strerror(errno));
            }
            return -1;
        }
        written += ret;
    }
    return written;
}

static void sigsegv_handler(int signum, siginfo_t *info, void *ptr)
{
    static const char *si_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};
    ucontext_t *ucontext = (ucontext_t *)ptr;
    char msg[STDERR_OUT_LEN512];
    int ret = 0, cnt = 0;

    memset(msg, 0x0, STDERR_OUT_LEN512);
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "rtsp server Segmentation Fault Trace:\n");
    cnt += ret;
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "info.si_signo = %d\n", signum);
    cnt += ret;
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "info.si_errno = %d\n", info->si_errno);
    cnt += ret;
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "info.si_code  = %d (%s)\n", info->si_code, si_codes[info->si_code]);
    cnt += ret;
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "info.si_addr  = %p\n", info->si_addr);
    cnt += ret;

    // safe_write(STDERR_FILENO, (void *)msg, cnt);
    fii_log_warnning("%s", msg); //non-async-signal-safe functions...

	//When there is a segment error, the process memory may be damaged, and continuing to perform cleanup operations (such as exit) may lead to a second crash. The safest approach is to terminate the process directly with '_exit'.
    _exit(EXIT_FAILURE);
}

static void shutdown_hander(int sig)
{
    char msg[STDERR_OUT_LEN64];
    
    memset(msg, 0x0, STDERR_OUT_LEN64);
    if(sig == SIGTERM)
	{
        memcpy(msg, "kvs server got term signal, exit!\n", 35);
    }
    else
    {
        memcpy(msg, "kvs server got int signal, exit!\n", 34);
    }
    int len = strlen(msg);
	safe_write(STDERR_FILENO, (void *)msg, len);
	// fii_log_warnning("%s", msg); //non-async-signal-safe functions...
    kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
}

void init_signals(void)
{
    struct sigaction sa;
    
    /* 配置终止信号 */
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGINT);

    sa.sa_handler = shutdown_hander;
    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        fii_log_error("Failed to set SIGTERM handler.");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        fii_log_error("Failed to set SIGINT handler.");
        exit(EXIT_FAILURE);
    }

    /* 配置段错误信号 */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigfillset(&sa.sa_mask);  // 处理期间阻塞所有信号

    if (sigaction(SIGSEGV, &sa, NULL) != 0)
    {
        fii_log_error("Failed to set SIGSEGV handler.");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGBUS, &sa, NULL) != 0)
    {
        fii_log_error("Failed to set SIGBUS handler.");
        exit(EXIT_FAILURE);
    }

    /* 忽略SIGPIPE */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0)
    {
        fii_log_error("Failed to set SIGPIPE handler.");
        exit(EXIT_FAILURE);
    }
}

//---cfg----
int kvs_service_cfg_get(kvs_service_cfg_t *cfg)
{
    if(cfg == NULL)
    {
        return -1;
    }
    memset(cfg, 0x0, sizeof(kvs_service_cfg_t));
    cfg->Enable = 1;
    return 0;
}


int kvs_media_init(void)
{
    int video_ret = -1;
    int audio_ret = -1;
    fii_log_info("kvs init media!\n");
    video_ret = open_video_stream(0);
    if (video_ret < 0)
    {
        fii_log_error("failed to open the video stream0\n");
        return -1;
    }
    video_ret = open_video_stream(1);
    if (video_ret < 0)
    {
        fii_log_error("failed to open the video stream1\n");
        return -1;
    }
    audio_ret = open_audio_stream();
    if (audio_ret < 0)
    {
        fii_log_error("failed to open the audio stream\n");
        return -1;
    }

    fii_log_info("open_stream success!\n");
    return 0;
}

int kvs_media_deinit(void)
{
    int ret = -1;

    fii_log_info("kvs deinit media!\n");
    ret = close_stream();
    if (ret != 0)
    {
        fii_log_error("failed to close_stream\n");
        return -1;
    }
    
    fii_log_info("close_stream success!\n");
    return 0;
}

/**
 * @description: Main KVS service processing thread
 * @param {void*} exit_flag Pointer to server exit flag
 * @return {void*} Always returns NULL
 * @author: galen
 */
static void *kvs_service_proc(void *exit_flag)
{
	prctl(PR_SET_NAME, __func__);
	fii_log_normal("kvs_service_proc start!\n");
	bool *kvs_exit_flag = exit_flag;
    int ret = -1;

	ret = kvs_service_cfg_get(&g_kvs_service_cfg);
	if(ret < 0)
	{
		fii_log_error("kvs_service_cfg_get fail!\n");
		kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
		return NULL;
	}

	ret = kvs_media_init();
    if(ret < 0)
    {
		fii_log_info("kvs media init failed 0!\n");
		usleep(500*1000);
		ret = kvs_media_init();
		if(ret < 0)
		{
			fii_log_info("kvs media init failed 1\n");
        	kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        	return NULL;	
		}

    }
	
    //

	while(!(*kvs_exit_flag))
	{
		

	}

	
	pthread_exit(NULL);
	
	return NULL;
}

/**
 * @description: Starts the KVS server thread
 * @return {int} 0 on success, error code on failure
 * @author: galen
 */
int kvs_service_start(void)
{
	int ret = -1;
	if(kvs_pthread_id == 0)
	{
		g_kvs_exit = false;
		ret = pthread_create(&kvs_pthread_id, NULL, kvs_service_proc, &g_kvs_exit);
		if(ret)
		{
			fii_log_error("pthread_create error.\n");
			return ret;
		}
	}
	else
	{
	    fii_log_warnning("kvs_pthread_id !=0, kvs_server_proc has been created.\n");
		return ret;
	}

	ret = 0;
	return ret;
}

/**
 * @description: Stops the KVS server thread
 * @return {int} Always returns 0
 * @author: galen
 */
int kvs_service_stop(void)
{
	fii_log_normal("start!\n");
	if (kvs_pthread_id != 0)
	{
	    g_kvs_exit = true;
		pthread_join(kvs_pthread_id, NULL);
		kvs_pthread_id = 0;
	}
	else
	{
	    fii_log_warnning("kvs_pthread_id = 0, kvs_pthread has been exited.");
	}
	fii_log_normal("end!\n");
	return 0;
}

/**
 * @description: Initializes the KVS server system
 * @return {int} Always returns 0
 * @author: galen
 */
int kvs_init(void)
{
	fii_log_normal("kvs_init!\n");
	init_signals();
	kvs_msg_start();
	kvs_service_start();
	return 0;
}

/**
 * @description: Shuts down the KVS server system
 * @return {int} Always returns 0
 * @author: galen
 */
int kvs_exit(void)
{
	fii_log_normal("kvs_exit!\n");
	kvs_msg_thread_stop();
	kvs_service_stop();
	kvs_media_deinit();
	
	return 0;
}