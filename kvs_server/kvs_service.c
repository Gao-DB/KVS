#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <ucontext.h>
#include <stdint.h>
#include <time.h>

#include "Samples.h"
#include "kvs_service.h"
#include "kvs_media.h"

#define KVS_DEFAULT_RETENTION_PERIOD   (2 * HUNDREDS_OF_NANOS_IN_AN_HOUR)
#define KVS_DEFAULT_BUFFER_DURATION    (120 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define KVS_EVENT_UPLOAD_DURATION      (12 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define KVS_DEFAULT_REGION             "us-east-1"
#define KVS_STREAM_NAME_MAX_LEN        128

static pthread_t kvs_pthread_id = 0;
static bool g_kvs_exit = false;
kvs_service_cfg_t g_kvs_service_cfg;

static pthread_mutex_t g_kvs_event_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_kvs_event_cond = PTHREAD_COND_INITIALIZER;
static bool g_kvs_event_pending = false;
static unsigned long long g_kvs_event_seq = 0;
static char g_kvs_event_filename[COMMON_STRING_LEN] = {0};
static pthread_mutex_t g_kvs_stream_name_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    (void) ucontext;
    memset(msg, 0x0, STDERR_OUT_LEN512);
    ret = snprintf(msg+cnt, STDERR_OUT_LEN512, "kvs server Segmentation Fault Trace:\n");
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
    snprintf(cfg->Video_Encode[0].m_StreamName, sizeof(cfg->Video_Encode[0].m_StreamName), "main-0");
    snprintf(cfg->Video_Encode[0].m_EncodeType, sizeof(cfg->Video_Encode[0].m_EncodeType), "H.264");
    cfg->Video_Encode[0].m_Enable = 1;
    cfg->Video_Encode[0].m_FPS = 25;
    cfg->Video_Encode[0].m_GOP = 60;

    return 0;
}

static int kvs_request_temporary_credentials_placeholder(PCHAR *accessKey, PCHAR *secretKey, PCHAR *sessionToken)
{
    PCHAR credEndpoint = getenv("KVS_TEMP_CREDENTIAL_ENDPOINT");

    if (credEndpoint != NULL)
    {
        fii_log_warnning("Temporary credential endpoint configured (%s) but backend exchange is not implemented in kvs_server yet.", credEndpoint);
    }

    *accessKey = NULL;
    *secretKey = NULL;
    *sessionToken = NULL;
    return -1;
}

static int kvs_build_event_stream_name(const char *event_filename, char *stream_name, size_t stream_name_len)
{
    size_t i = 0;
    size_t offset = 0;
    const char *filename = event_filename == NULL ? "unknown" : event_filename;
    time_t now = time(NULL);
    unsigned long long seq = 0;

    if (stream_name == NULL || stream_name_len == 0)
    {
        return -1;
    }

    offset = snprintf(stream_name, stream_name_len, "event-");
    if (offset >= stream_name_len)
    {
        return -1;
    }

    for (i = 0; filename[i] != '\0' && offset + 1 < stream_name_len; i++)
    {
        char c = filename[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_')
        {
            stream_name[offset++] = c;
        }
        else
        {
            stream_name[offset++] = '-';
        }
    }

    if (offset + 24 >= stream_name_len)
    {
        return -1;
    }

    pthread_mutex_lock(&g_kvs_stream_name_mutex);
    seq = g_kvs_event_seq++;
    pthread_mutex_unlock(&g_kvs_stream_name_mutex);

    snprintf(stream_name + offset, stream_name_len - offset, "-%llu-%ld", seq, (long) now);
    return 0;
}

static int kvs_copy_frame_payload(const FrameData_t *frame_data, PBYTE *frame_buffer, UINT32 *frame_size, BOOL *allocated)
{
    int merged_size = 0;
    PBYTE merged = NULL;

    if (frame_data == NULL || frame_buffer == NULL || frame_size == NULL || allocated == NULL)
    {
        return -1;
    }

    *allocated = FALSE;
    *frame_buffer = NULL;
    *frame_size = 0;

    if (frame_data->FrameSize <= 0 || frame_data->FramePtr == NULL)
    {
        return -1;
    }

    if (frame_data->FrameSize1 > 0 && frame_data->FramePtr1 != NULL)
    {
        merged_size = frame_data->FrameSize + frame_data->FrameSize1;
        merged = (PBYTE) MEMALLOC((UINT32) merged_size);
        if (merged == NULL)
        {
            return -1;
        }

        MEMCPY(merged, frame_data->FramePtr, frame_data->FrameSize);
        MEMCPY(merged + frame_data->FrameSize, frame_data->FramePtr1, frame_data->FrameSize1);

        *allocated = TRUE;
        *frame_buffer = merged;
        *frame_size = (UINT32) merged_size;
        return 0;
    }

    *frame_buffer = (PBYTE) frame_data->FramePtr;
    *frame_size = (UINT32) frame_data->FrameSize;
    return 0;
}

static STATUS kvs_put_video_frame(STREAM_HANDLE stream_handle, int *video_index, UINT64 presentation_ts, BOOL *first_video_sent)
{
    STATUS status = STATUS_SUCCESS;
    Frame video_frame;
    FrameData_t ring_frame;
    PBYTE frame_buffer = NULL;
    UINT32 frame_size = 0;
    BOOL allocated = FALSE;
    int ret = -1;

    if (video_index == NULL || first_video_sent == NULL)
    {
        return STATUS_NULL_ARG;
    }

    MEMSET(&ring_frame, 0x0, SIZEOF(FrameData_t));
    ret = get_video_frame_data(0, video_index, &ring_frame);
    if (ret < 0)
    {
        return STATUS_SUCCESS;
    }

    if (kvs_copy_frame_payload(&ring_frame, &frame_buffer, &frame_size, &allocated) < 0)
    {
        return STATUS_SUCCESS;
    }

    MEMSET(&video_frame, 0x0, SIZEOF(Frame));
    video_frame.version = FRAME_CURRENT_VERSION;
    video_frame.trackId = DEFAULT_VIDEO_TRACK_ID;
    video_frame.duration = 0;
    video_frame.decodingTs = presentation_ts;
    video_frame.presentationTs = presentation_ts;
    video_frame.frameData = frame_buffer;
    video_frame.size = frame_size;
    video_frame.flags = (ring_frame.FrameType != 0) ? FRAME_FLAG_KEY_FRAME : FRAME_FLAG_NONE;

    status = putKinesisVideoFrame(stream_handle, &video_frame);
    if (STATUS_SUCCEEDED(status))
    {
        *first_video_sent = TRUE;
    }

    if (allocated)
    {
        MEMFREE(frame_buffer);
    }

    return status;
}

static STATUS kvs_put_audio_frame(STREAM_HANDLE stream_handle, MapInfo_t *audio_map_info, int *audio_index, UINT64 presentation_ts, BOOL first_video_sent)
{
    STATUS status = STATUS_SUCCESS;
    Frame audio_frame;
    FrameData_t ring_frame;
    PBYTE frame_buffer = NULL;
    UINT32 frame_size = 0;
    BOOL allocated = FALSE;
    int ret = -1;

    if (audio_map_info == NULL || audio_index == NULL || !first_video_sent)
    {
        return STATUS_SUCCESS;
    }

    MEMSET(&ring_frame, 0x0, SIZEOF(FrameData_t));
    ret = get_audio_frame_data(audio_map_info, audio_index, &ring_frame);
    if (ret < 0)
    {
        return STATUS_SUCCESS;
    }

    if (kvs_copy_frame_payload(&ring_frame, &frame_buffer, &frame_size, &allocated) < 0)
    {
        return STATUS_SUCCESS;
    }

    MEMSET(&audio_frame, 0x0, SIZEOF(Frame));
    audio_frame.version = FRAME_CURRENT_VERSION;
    audio_frame.trackId = DEFAULT_AUDIO_TRACK_ID;
    audio_frame.duration = 0;
    audio_frame.decodingTs = presentation_ts;
    audio_frame.presentationTs = presentation_ts;
    audio_frame.frameData = frame_buffer;
    audio_frame.size = frame_size;
    audio_frame.flags = FRAME_FLAG_NONE;

    status = putKinesisVideoFrame(stream_handle, &audio_frame);

    if (allocated)
    {
        MEMFREE(frame_buffer);
    }

    return status;
}

static int kvs_upload_event_stream(const char *event_filename)
{
    STATUS status = STATUS_SUCCESS;
    PDeviceInfo device_info = NULL;
    PStreamInfo stream_info = NULL;
    PClientCallbacks client_callbacks = NULL;
    PStreamCallbacks stream_callbacks = NULL;
    CLIENT_HANDLE client_handle = INVALID_CLIENT_HANDLE_VALUE;
    STREAM_HANDLE stream_handle = INVALID_STREAM_HANDLE_VALUE;
    PCHAR access_key = NULL;
    PCHAR secret_key = NULL;
    PCHAR session_token = NULL;
    PCHAR region = NULL;
    PCHAR ca_cert_path = NULL;
    PCHAR client_cert_path = NULL;
    PCHAR client_key_path = NULL;
    CHAR endpoint_override[MAX_URI_CHAR_LEN];
    char stream_name[KVS_STREAM_NAME_MAX_LEN] = {0};
    UINT64 start_time = 0;
    UINT64 now_time = 0;
    UINT64 elapsed = 0;
    int video_index = 0;
    int audio_index = 0;
    BOOL first_video_sent = FALSE;
    MapInfo_t *audio_map_info = NULL;

    if (kvs_build_event_stream_name(event_filename, stream_name, sizeof(stream_name)) < 0)
    {
        fii_log_error("invalid event filename for stream generation.\n");
        return -1;
    }

    access_key = getenv("AWS_ACCESS_KEY_ID");
    secret_key = getenv("AWS_SECRET_ACCESS_KEY");
    session_token = getenv("AWS_SESSION_TOKEN");
    if (access_key == NULL || secret_key == NULL)
    {
        if (kvs_request_temporary_credentials_placeholder(&access_key, &secret_key, &session_token) < 0)
        {
            fii_log_error("missing AWS credentials. set AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY or implement KVS_TEMP_CREDENTIAL_ENDPOINT backend exchange.\n");
            return -1;
        }
    }

    region = getenv("AWS_REGION");
    if (region == NULL)
    {
        region = (PCHAR) KVS_DEFAULT_REGION;
    }

    ca_cert_path = getenv("KVS_CA_CERT_PATH");
    client_cert_path = getenv("KVS_CLIENT_CERT_PATH");
    client_key_path = getenv("KVS_CLIENT_KEY_PATH");

    fii_log_info("upload event stream: %s\n", stream_name);
    if (client_cert_path != NULL || client_key_path != NULL)
    {
        fii_log_warnning("KVS_CLIENT_CERT_PATH/KVS_CLIENT_KEY_PATH are configured; aws-key credential flow ignores client cert/key unless custom callback provider is implemented.\n");
    }

    MEMSET(endpoint_override, 0x0, SIZEOF(endpoint_override));
    getEndpointOverride(endpoint_override, SIZEOF(endpoint_override));

    CHK_STATUS(createDefaultDeviceInfo(&device_info));
    device_info->clientInfo.loggerLogLevel = getSampleLogLevel();

    CHK_STATUS(createRealtimeAudioVideoStreamInfoProvider(stream_name, KVS_DEFAULT_RETENTION_PERIOD, KVS_DEFAULT_BUFFER_DURATION, &stream_info));
    stream_info->streamCaps.absoluteFragmentTimes = FALSE;

    CHK_STATUS(createDefaultCallbacksProviderWithAwsCredentialsAndEndpointOverride(access_key,
                                                                                   secret_key,
                                                                                   session_token,
                                                                                   MAX_UINT64,
                                                                                   region,
                                                                                   ca_cert_path,
                                                                                   NULL,
                                                                                   NULL,
                                                                                   endpoint_override,
                                                                                   &client_callbacks));

    CHK_STATUS(createStreamCallbacks(&stream_callbacks));
    CHK_STATUS(addStreamCallbacks(client_callbacks, stream_callbacks));
    CHK_STATUS(createKinesisVideoClient(device_info, client_callbacks, &client_handle));
    CHK_STATUS(createKinesisVideoStreamSync(client_handle, stream_info, &stream_handle));

    start_time = GETTIME();
    while (kvs_service_get_status() == KVS_SERVICE_STATUS_OK)
    {
        now_time = GETTIME();
        elapsed = now_time - start_time;
        if (elapsed >= KVS_EVENT_UPLOAD_DURATION)
        {
            break;
        }

        if (STATUS_FAILED(kvs_put_video_frame(stream_handle, &video_index, elapsed, &first_video_sent)))
        {
            fii_log_warnning("put video frame failed.\n");
        }

        audio_map_info = rtp_get_audio_stream();
        if (STATUS_FAILED(kvs_put_audio_frame(stream_handle, audio_map_info, &audio_index, elapsed, first_video_sent)))
        {
            fii_log_warnning("put audio frame failed.\n");
        }

        THREAD_SLEEP(10 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

    CHK_STATUS(stopKinesisVideoStreamSync(stream_handle));

CleanUp:

    if (STATUS_FAILED(status))
    {
        fii_log_error("event upload failed with status 0x%08x\n", status);
    }

    freeKinesisVideoStream(&stream_handle);
    freeKinesisVideoClient(&client_handle);
    freeCallbacksProvider(&client_callbacks);
    freeStreamInfoProvider(&stream_info);
    freeDeviceInfo(&device_info);

    return STATUS_SUCCEEDED(status) ? 0 : -1;
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
    char event_filename[COMMON_STRING_LEN] = {0};
    int wait_ret = 0;

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

	while(!(*kvs_exit_flag))
	{
        pthread_mutex_lock(&g_kvs_event_mutex);
        while (!g_kvs_event_pending && !(*kvs_exit_flag))
        {
            wait_ret = pthread_cond_wait(&g_kvs_event_cond, &g_kvs_event_mutex);
            if (wait_ret != 0)
            {
                fii_log_warnning("kvs_service_proc wakeup failed: %d\n", wait_ret);
                break;
            }
        }

        if (*kvs_exit_flag)
        {
            pthread_mutex_unlock(&g_kvs_event_mutex);
            break;
        }

        strncpy(event_filename, g_kvs_event_filename, sizeof(event_filename) - 1);
        event_filename[sizeof(event_filename) - 1] = '\0';
        g_kvs_event_pending = false;
        pthread_mutex_unlock(&g_kvs_event_mutex);

        if (kvs_upload_event_stream(event_filename) < 0)
        {
            fii_log_error("event upload failed for %s\n", event_filename);
        }
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
        pthread_mutex_lock(&g_kvs_event_mutex);
        pthread_cond_signal(&g_kvs_event_cond);
        pthread_mutex_unlock(&g_kvs_event_mutex);
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

int kvs_service_trigger_event(const char *event_filename)
{
    if (event_filename == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&g_kvs_event_mutex);
    strncpy(g_kvs_event_filename, event_filename, sizeof(g_kvs_event_filename) - 1);
    g_kvs_event_filename[sizeof(g_kvs_event_filename) - 1] = '\0';
    g_kvs_event_pending = true;
    pthread_cond_signal(&g_kvs_event_cond);
    pthread_mutex_unlock(&g_kvs_event_mutex);

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
	kvs_service_stop();
	kvs_media_deinit();

	return 0;
}
