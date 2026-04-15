#include "kvs_service.h"

#include "kvs_media.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KVS_UPLOAD_SECONDS 12
#define KVS_CREATE_MAX_RETRY 5
#define KVS_EVENT_STREAM_NAME_MAX 128

typedef enum {
    KVS_UPLOAD_RESULT_OK = 0,
    KVS_UPLOAD_RESULT_NAME_COLLISION = 1,
    KVS_UPLOAD_RESULT_RETRYABLE = 2,
    KVS_UPLOAD_RESULT_FATAL = 3,
} kvs_upload_result_t;

typedef struct {
    const char* region;
    const char* access_key_id;
    const char* secret_access_key;
    const char* session_token;
    const char* sts_endpoint;
    const char* role_arn;
} kvs_credential_ctx_t;

typedef struct {
    bool started;
} kvs_producer_ctx_t;

static pthread_t g_service_thread;
static bool g_service_running = false;
static bool g_service_exit = false;
static int g_service_status = KVS_SERVICE_STATUS_OK;

static pthread_mutex_t g_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_event_cond = PTHREAD_COND_INITIALIZER;
static uint32_t g_pending_events = 0;

static kvs_service_cfg_t g_service_cfg;
static kvs_producer_ctx_t g_producer_ctx;

static void kvs_log(const char* level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[kvs][%s] ", level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int kvs_service_get_status(void)
{
    return g_service_status;
}

void kvs_service_set_status(int status)
{
    g_service_status = status;
}

static void kvs_load_credentials(kvs_credential_ctx_t* creds)
{
    memset(creds, 0, sizeof(*creds));
    creds->region = getenv("AWS_REGION");
    if (creds->region == NULL || creds->region[0] == '\0') {
        creds->region = getenv("AWS_DEFAULT_REGION");
    }
    if (creds->region == NULL || creds->region[0] == '\0') {
        creds->region = "us-west-2";
    }

    creds->access_key_id = getenv("AWS_ACCESS_KEY_ID");
    creds->secret_access_key = getenv("AWS_SECRET_ACCESS_KEY");
    creds->session_token = getenv("AWS_SESSION_TOKEN");

    creds->sts_endpoint = getenv("KVS_STS_ENDPOINT");
    creds->role_arn = getenv("KVS_ROLE_ARN");
}

static void kvs_refresh_short_term_credentials(const kvs_credential_ctx_t* creds)
{
    if (creds->sts_endpoint != NULL && creds->role_arn != NULL) {
        kvs_log("INFO", "STS placeholder: request temporary credentials from backend endpoint=%s role=%s", creds->sts_endpoint, creds->role_arn);
        kvs_log("INFO", "STS placeholder: parse AssumeRole response and refresh AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY/AWS_SESSION_TOKEN in memory.");
    } else {
        kvs_log("INFO", "Using environment AWS credentials for local testing.");
    }
}

static kvs_upload_result_t kvs_producer_start(kvs_producer_ctx_t* ctx, const kvs_credential_ctx_t* creds)
{
    (void) creds;
    ctx->started = true;
    kvs_log("INFO", "Producer started (SDK integration point). region=%s", creds->region);
    return KVS_UPLOAD_RESULT_OK;
}

static kvs_upload_result_t kvs_producer_create_stream(kvs_producer_ctx_t* ctx, const char* stream_name)
{
    (void) ctx;
    (void) stream_name;
    return KVS_UPLOAD_RESULT_OK;
}

static kvs_upload_result_t kvs_producer_get_data_endpoint(kvs_producer_ctx_t* ctx, const char* stream_name)
{
    (void) ctx;
    (void) stream_name;
    return KVS_UPLOAD_RESULT_OK;
}

static kvs_upload_result_t kvs_producer_start_stream(kvs_producer_ctx_t* ctx, const char* stream_name)
{
    (void) ctx;
    kvs_log("INFO", "Start stream: %s", stream_name);
    return KVS_UPLOAD_RESULT_OK;
}

static kvs_upload_result_t kvs_producer_put_frame(kvs_producer_ctx_t* ctx, const char* stream_name, bool is_video, const kvs_media_frame_t* frame)
{
    (void) ctx;
    (void) stream_name;
    (void) is_video;
    (void) frame;
    return KVS_UPLOAD_RESULT_OK;
}

static kvs_upload_result_t kvs_producer_stop_stream(kvs_producer_ctx_t* ctx, const char* stream_name)
{
    (void) ctx;
    kvs_log("INFO", "Stop stream: %s", stream_name);
    return KVS_UPLOAD_RESULT_OK;
}

static void kvs_producer_stop(kvs_producer_ctx_t* ctx)
{
    if (ctx->started) {
        kvs_log("INFO", "Producer stopped");
    }
    ctx->started = false;
}

static uint64_t monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec;
}

static void build_event_stream_name(char* out_name, size_t out_size, uint32_t event_id, uint32_t suffix)
{
    const char* prefix = getenv("KVS_STREAM_PREFIX");
    time_t now = time(NULL);
    if (prefix == NULL || prefix[0] == '\0') {
        prefix = "event";
    }

    if (suffix == 0) {
        (void) snprintf(out_name, out_size, "%s-%lu-%u", prefix, (unsigned long) now, event_id);
    } else {
        (void) snprintf(out_name, out_size, "%s-%lu-%u-%u", prefix, (unsigned long) now, event_id, suffix);
    }
}

static kvs_upload_result_t create_stream_with_retry(kvs_producer_ctx_t* producer,
                                                     uint32_t event_id,
                                                     char* created_stream_name,
                                                     size_t stream_name_size)
{
    uint32_t attempt;
    uint32_t suffix = 0;
    uint32_t backoff_ms = 200;

    for (attempt = 0; attempt < KVS_CREATE_MAX_RETRY; ++attempt) {
        build_event_stream_name(created_stream_name, stream_name_size, event_id, suffix);
        kvs_log("INFO", "CreateStream attempt=%u stream=%s", attempt + 1, created_stream_name);

        kvs_upload_result_t ret = kvs_producer_create_stream(producer, created_stream_name);
        if (ret == KVS_UPLOAD_RESULT_OK) {
            kvs_log("INFO", "CreateStream success stream=%s", created_stream_name);
            return kvs_producer_get_data_endpoint(producer, created_stream_name);
        }

        if (ret == KVS_UPLOAD_RESULT_NAME_COLLISION) {
            kvs_log("WARN", "CreateStream collision stream=%s, appending suffix", created_stream_name);
            suffix++;
            continue;
        }

        if (ret == KVS_UPLOAD_RESULT_RETRYABLE) {
            kvs_log("WARN", "CreateStream retryable failure stream=%s backoff=%ums", created_stream_name, backoff_ms);
            usleep(backoff_ms * 1000U);
            backoff_ms *= 2;
            continue;
        }

        return ret;
    }

    return KVS_UPLOAD_RESULT_FATAL;
}

static int upload_event_clip(kvs_producer_ctx_t* producer, uint32_t event_id)
{
    kvs_upload_result_t result;
    char stream_name[KVS_EVENT_STREAM_NAME_MAX] = {0};
    kvs_media_frame_t frame;
    uint64_t stop_at = monotonic_seconds() + KVS_UPLOAD_SECONDS;

    result = create_stream_with_retry(producer, event_id, stream_name, sizeof(stream_name));
    if (result != KVS_UPLOAD_RESULT_OK) {
        kvs_log("ERROR", "CreateStream/GetDataEndpoint failed for event=%u", event_id);
        return -1;
    }

    result = kvs_producer_start_stream(producer, stream_name);
    if (result != KVS_UPLOAD_RESULT_OK) {
        kvs_log("ERROR", "Start stream failed stream=%s", stream_name);
        return -1;
    }

    kvs_log("INFO", "Begin 12s upload from ringbuf event=%u stream=%s", event_id, stream_name);
    while (monotonic_seconds() < stop_at) {
        if (kvs_media_get_video_frame(&frame) == 0 && frame.data != NULL && frame.len > 0) {
            (void) kvs_producer_put_frame(producer, stream_name, true, &frame);
        }

        if (kvs_media_get_audio_frame(&frame) == 0 && frame.data != NULL && frame.len > 0) {
            (void) kvs_producer_put_frame(producer, stream_name, false, &frame);
        }

        usleep(10 * 1000);
    }

    (void) kvs_producer_stop_stream(producer, stream_name);
    kvs_log("INFO", "Upload finished event=%u stream=%s", event_id, stream_name);
    kvs_log("INFO", "Stream retained for background cleanup stream=%s", stream_name);
    return 0;
}

static void* kvs_service_proc(void* arg)
{
    uint32_t event_id = 0;
    kvs_credential_ctx_t credentials;
    (void) arg;

    if (kvs_service_cfg_get(&g_service_cfg) != 0) {
        kvs_log("ERROR", "kvs_service_cfg_get failed");
        kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }

    if (kvs_media_init() != 0) {
        kvs_log("ERROR", "kvs_media_init failed");
        kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }

    kvs_load_credentials(&credentials);
    kvs_refresh_short_term_credentials(&credentials);
    if (kvs_producer_start(&g_producer_ctx, &credentials) != KVS_UPLOAD_RESULT_OK) {
        kvs_log("ERROR", "Failed to start producer");
        kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }

    kvs_log("INFO", "kvs_service_proc started; waiting for event trigger");

    while (true) {
        pthread_mutex_lock(&g_event_lock);
        while (g_pending_events == 0 && !g_service_exit) {
            pthread_cond_wait(&g_event_cond, &g_event_lock);
        }
        if (g_service_exit) {
            pthread_mutex_unlock(&g_event_lock);
            break;
        }
        g_pending_events--;
        event_id++;
        pthread_mutex_unlock(&g_event_lock);

        if (upload_event_clip(&g_producer_ctx, event_id) != 0) {
            kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        }
    }

    kvs_producer_stop(&g_producer_ctx);
    (void) kvs_media_deinit();
    kvs_log("INFO", "kvs_service_proc exit");
    return NULL;
}

int kvs_service_trigger_event_upload(void)
{
    pthread_mutex_lock(&g_event_lock);
    g_pending_events++;
    pthread_cond_signal(&g_event_cond);
    pthread_mutex_unlock(&g_event_lock);
    kvs_log("INFO", "Event trigger received, pending=%u", g_pending_events);
    return 0;
}

int kvs_service_start(void)
{
    if (g_service_running) {
        return 0;
    }

    g_service_exit = false;
    g_service_status = KVS_SERVICE_STATUS_OK;
    if (pthread_create(&g_service_thread, NULL, kvs_service_proc, NULL) != 0) {
        kvs_log("ERROR", "Failed to create kvs service thread: %s", strerror(errno));
        g_service_status = KVS_SERVICE_STATUS_ERR;
        return -1;
    }

    g_service_running = true;
    return 0;
}

int kvs_service_stop(void)
{
    if (!g_service_running) {
        return 0;
    }

    pthread_mutex_lock(&g_event_lock);
    g_service_exit = true;
    pthread_cond_signal(&g_event_cond);
    pthread_mutex_unlock(&g_event_lock);

    pthread_join(g_service_thread, NULL);
    g_service_running = false;
    return 0;
}
