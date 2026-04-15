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
#include <inttypes.h>
#include <time.h>
#include <netinet/in.h>

#include "kvs_service.h"
#include "kvs_msg.h"
#include "kvs_media.h"
#include "../kvs_open-source/minimal_sdk/kvs_producer_minimal.h"

#define KVS_DEFAULT_EVENT_INTERVAL_MS      (3000ULL)
#define KVS_DEFAULT_EVENT_DURATION_MS      (10000ULL)
#define KVS_DEFAULT_FRAME_LOOP_IDLE_US     (10 * 1000)

typedef struct {
    const char* region;
    const char* access_key_id;
    const char* secret_access_key;
    const char* session_token;
    const char* cert_path;
    const char* private_key_path;
    const char* ca_cert_path;
    const char* stream_prefix;
    uint64_t event_interval_ms;
    uint64_t event_duration_ms;
} kvs_uplink_env_t;

static uint64_t kvs_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}

static uint64_t kvs_ms_to_100ns(uint64_t ms)
{
    return ms * 10000ULL;
}

static uint64_t kvs_get_env_u64(const char* name, uint64_t default_value)
{
    const char* value = getenv(name);
    if (value == NULL || *value == '\0') {
        return default_value;
    }

    char* end_ptr = NULL;
    uint64_t parsed = strtoull(value, &end_ptr, 10);
    if (end_ptr == value || (end_ptr != NULL && *end_ptr != '\0')) {
        return default_value;
    }

    return parsed;
}

static int kvs_load_uplink_env(kvs_uplink_env_t* env)
{
    if (env == NULL) {
        return -1;
    }

    memset(env, 0x0, sizeof(*env));

    env->region = getenv("AWS_REGION");
    env->access_key_id = getenv("AWS_ACCESS_KEY_ID");
    env->secret_access_key = getenv("AWS_SECRET_ACCESS_KEY");
    env->session_token = getenv("AWS_SESSION_TOKEN");
    env->cert_path = getenv("KVS_CERT_PATH");
    env->private_key_path = getenv("KVS_PRIVATE_KEY_PATH");
    env->ca_cert_path = getenv("KVS_CA_CERT_PATH");
    env->stream_prefix = getenv("KVS_STREAM_PREFIX");
    env->event_interval_ms = kvs_get_env_u64("KVS_EVENT_INTERVAL_MS", KVS_DEFAULT_EVENT_INTERVAL_MS);
    env->event_duration_ms = kvs_get_env_u64("KVS_EVENT_DURATION_MS", KVS_DEFAULT_EVENT_DURATION_MS);

    if (env->region == NULL || env->access_key_id == NULL || env->secret_access_key == NULL) {
        /*
         * 设备端上行必须提供以下环境变量：
         * 1) AWS_REGION
         * 2) AWS_ACCESS_KEY_ID
         * 3) AWS_SECRET_ACCESS_KEY
         * 4) AWS_SESSION_TOKEN (可选，推荐使用短期凭证时提供)
         * 5) KVS_CERT_PATH (可选，客户端证书路径)
         * 6) KVS_PRIVATE_KEY_PATH (可选，客户端私钥路径)
         * 7) KVS_CA_CERT_PATH (可选，CA 证书路径)
         * 8) KVS_STREAM_PREFIX (可选，默认 event)
         * 9) KVS_EVENT_INTERVAL_MS / KVS_EVENT_DURATION_MS (可选，事件模拟参数)
         */
        fii_log_error("Missing mandatory AWS env vars. Required: AWS_REGION/AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY\n");
        return -1;
    }

    if (env->stream_prefix == NULL || env->stream_prefix[0] == '\0') {
        env->stream_prefix = "event";
    }

    return 0;
}

static int kvs_generate_event_stream_name(const kvs_uplink_env_t* env, uint64_t seq, char* out_name, size_t out_size)
{
    if (env == NULL || out_name == NULL || out_size == 0) {
        return -1;
    }

    uint64_t now_ms = kvs_now_ms();
    int written = snprintf(out_name, out_size, "%s-%" PRIu64 "-%" PRIu64, env->stream_prefix, now_ms, seq);
    return (written > 0 && (size_t) written < out_size) ? 0 : -1;
}

static pthread_t kvs_pthread_id = 0;
static bool g_kvs_exit = false;
static pthread_t kvs_select_pthrid = 0;
static bool g_kvs_select_exit = false;
g_kvs_service_cfg_t g_kvs_service_cfg;

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
    int video_read_index = 0;
    int audio_read_index = 0;
    uint64_t event_seq = 0;
    uint64_t next_event_trigger_ms = 0;
    uint64_t current_event_stop_ms = 0;
    bool event_active = false;
    char stream_name[COMMON_STRING_LEN] = {0};
    kvs_uplink_env_t uplink_env;
    kvs_minimal_producer_config_t producer_cfg;
    kvs_minimal_producer_t producer;

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
	
    if (kvs_load_uplink_env(&uplink_env) != 0) {
        kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }

    memset(&producer_cfg, 0x0, sizeof(producer_cfg));
    producer_cfg.region = uplink_env.region;
    producer_cfg.access_key_id = uplink_env.access_key_id;
    producer_cfg.secret_access_key = uplink_env.secret_access_key;
    producer_cfg.session_token = uplink_env.session_token;
    producer_cfg.cert_path = uplink_env.cert_path;
    producer_cfg.private_key_path = uplink_env.private_key_path;
    producer_cfg.ca_cert_path = uplink_env.ca_cert_path;

    if (kvs_minimal_producer_init(&producer_cfg, &producer) != 0) {
        fii_log_error("kvs_minimal_producer_init failed\n");
        kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }

    next_event_trigger_ms = kvs_now_ms();

	while(!(*kvs_exit_flag))
	{
		uint64_t now_ms = kvs_now_ms();

        if (!event_active && now_ms >= next_event_trigger_ms) {
            if (kvs_generate_event_stream_name(&uplink_env, ++event_seq, stream_name, sizeof(stream_name)) == 0) {
                if (kvs_minimal_producer_create_stream(&producer, stream_name) == 0) {
                    event_active = true;
                    current_event_stop_ms = now_ms + uplink_env.event_duration_ms;
                    fii_log_info("CreateStream success, stream=%s\n", stream_name);
                }
            }
            next_event_trigger_ms = now_ms + uplink_env.event_interval_ms;
        }

        if (event_active) {
            FrameData_t video_frame;
            FrameData_t audio_frame;
            memset(&video_frame, 0x0, sizeof(video_frame));
            memset(&audio_frame, 0x0, sizeof(audio_frame));

            /*
             * 帧获取接口（由 ringbuf 外部实现提供）：
             * 1) int get_video_frame_data(int stream_id, int* index, FrameData_t* Frame)
             * 2) int get_audio_frame_data(MapInfo_t* map_info, int* index, FrameData_t* Frame)
             * 返回值 < 0 表示暂时无帧/读取失败；FrameData_t 中由外部实现填充帧数据与时间戳。
             */
            if (get_video_frame_data(0, &video_read_index, &video_frame) >= 0) {
                kvs_minimal_producer_put_video_frame(&producer, &video_frame, kvs_ms_to_100ns(now_ms));
            }

            if (get_audio_frame_data(rtp_get_audio_stream(), &audio_read_index, &audio_frame) >= 0) {
                kvs_minimal_producer_put_audio_frame(&producer, &audio_frame, kvs_ms_to_100ns(now_ms));
            }

            if (now_ms >= current_event_stop_ms || *kvs_exit_flag) {
                kvs_minimal_producer_stop_stream(&producer);
                event_active = false;
                memset(stream_name, 0x0, sizeof(stream_name));
            }
        } else {
            usleep(KVS_DEFAULT_FRAME_LOOP_IDLE_US);
        }
	}

    if (event_active) {
        kvs_minimal_producer_stop_stream(&producer);
    }
    kvs_minimal_producer_deinit(&producer);
	
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
