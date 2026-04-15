#include "kvs_producer_sdk_minimal.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_TRIGGER_INTERVAL_SEC 30
#define DEFAULT_UPLOAD_DURATION_SEC 12
#define DEFAULT_REGION "us-east-1"
#define DEFAULT_RING_VIDEO_PATH "/tmp/v0"
#define DEFAULT_RING_AUDIO_PATH "/tmp/a0"
#define FRAME_READ_SIZE 4096
#define VIDEO_FRAME_INTERVAL_MS 40
#define AUDIO_FRAME_INTERVAL_MS 20
#define MAX_ENV_INTERVAL_SEC 86400
#define KEY_FRAME_INTERVAL 25

static volatile sig_atomic_t g_stop = 0;

typedef struct {
    FILE* fp;
    char path[256];
    uint8_t buffer[FRAME_READ_SIZE];
} RingFile;

typedef struct {
    int triggerIntervalSec;
    int uploadDurationSec;
    RingFile video;
    RingFile audio;
} AppConfig;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}

static int parse_env_int(const char* envName, int defaultVal)
{
    const char* raw = getenv(envName);
    char* endPtr = NULL;
    long parsed = 0;
    if (raw == NULL || raw[0] == '\0') {
        return defaultVal;
    }
    parsed = strtol(raw, &endPtr, 10);
    if (*endPtr != '\0' || parsed <= 0 || parsed > MAX_ENV_INTERVAL_SEC) {
        return defaultVal;
    }
    return (int) parsed;
}

static int ring_open(RingFile* ring, const char* path)
{
    if (ring == NULL || path == NULL) {
        return -1;
    }
    memset(ring, 0, sizeof(*ring));
    snprintf(ring->path, sizeof(ring->path), "%s", path);
    ring->fp = fopen(path, "rb");
    return ring->fp == NULL ? -1 : 0;
}

static void ring_close(RingFile* ring)
{
    if (ring != NULL && ring->fp != NULL) {
        fclose(ring->fp);
        ring->fp = NULL;
    }
}

static size_t ring_read_chunk(RingFile* ring)
{
    size_t bytesRead = 0;
    if (ring == NULL || ring->fp == NULL) {
        return 0;
    }
    bytesRead = fread(ring->buffer, 1, sizeof(ring->buffer), ring->fp);
    if (bytesRead == 0) {
        clearerr(ring->fp);
        if (fseek(ring->fp, 0, SEEK_SET) != 0) {
            return 0;
        }
        bytesRead = fread(ring->buffer, 1, sizeof(ring->buffer), ring->fp);
    }
    return bytesRead;
}

static void handle_sigint(int sig)
{
    (void) sig;
    g_stop = 1;
}

static int load_client_from_env(KvsProducerClient** outClient)
{
    const char* caPath = getenv(KVS_ENV_CA_CERT_PATH);
    const char* certPath = getenv(KVS_ENV_CLIENT_CERT_PATH);
    const char* keyPath = getenv(KVS_ENV_CLIENT_PRIVATE_KEY_PATH);
    const char* region = getenv("AWS_DEFAULT_REGION");
    KvsProducerClientConfig config;

    if (caPath == NULL || certPath == NULL || keyPath == NULL) {
        fprintf(stderr, "missing cert env vars: %s, %s, %s\n", KVS_ENV_CA_CERT_PATH, KVS_ENV_CLIENT_CERT_PATH,
                KVS_ENV_CLIENT_PRIVATE_KEY_PATH);
        return -1;
    }

    memset(&config, 0, sizeof(config));
    config.region = region == NULL ? DEFAULT_REGION : region;
    config.caCertPath = caPath;
    config.clientCertPath = certPath;
    config.clientPrivateKeyPath = keyPath;
    return kvsProducerClientCreate(&config, outClient);
}

static int upload_event_window(KvsProducerClient* client, AppConfig* app)
{
    int ret = 0;
    char streamName[128];
    char endpoint[512];
    KvsProducerStream* stream = NULL;
    uint64_t startMs = now_ms();
    uint64_t stopMs = startMs + ((uint64_t) app->uploadDurationSec * 1000ULL);
    uint64_t nextVideoMs = startMs;
    uint64_t nextAudioMs = startMs;
    uint64_t eventId = startMs;
    uint64_t videoFrameIndex = 0;

    snprintf(streamName, sizeof(streamName), "event-%llu", (unsigned long long) eventId);

    ret = kvsProducerCreateStream(client, streamName, &stream);
    if (ret != 0) {
        return ret;
    }
    ret = kvsProducerDescribeStream(client, streamName);
    if (ret != 0) {
        kvsProducerFreeStream(stream);
        return ret;
    }
    ret = kvsProducerGetDataEndpoint(client, streamName, endpoint, sizeof(endpoint));
    if (ret != 0) {
        kvsProducerFreeStream(stream);
        return ret;
    }
    fprintf(stdout, "triggered upload stream=%s endpoint=%s duration=%d\n", streamName, endpoint, app->uploadDurationSec);

    while (!g_stop && now_ms() < stopMs) {
        uint64_t ts = now_ms();
        if (ts >= nextVideoMs) {
            KvsProducerFrame videoFrame;
            size_t bytes = ring_read_chunk(&app->video);
            if (bytes > 0) {
                memset(&videoFrame, 0, sizeof(videoFrame));
                videoFrame.data = app->video.buffer;
                videoFrame.size = bytes;
                videoFrame.ptsMs = ts;
                videoFrame.isKeyFrame = (videoFrameIndex % KEY_FRAME_INTERVAL) == 0;
                videoFrame.isAudio = 0;
                kvsProducerPutFrame(stream, &videoFrame);
                videoFrameIndex++;
            }
            nextVideoMs += VIDEO_FRAME_INTERVAL_MS;
        }
        if (ts >= nextAudioMs) {
            KvsProducerFrame audioFrame;
            size_t bytes = ring_read_chunk(&app->audio);
            if (bytes > 0) {
                memset(&audioFrame, 0, sizeof(audioFrame));
                audioFrame.data = app->audio.buffer;
                audioFrame.size = bytes;
                audioFrame.ptsMs = ts;
                audioFrame.isKeyFrame = 0;
                audioFrame.isAudio = 1;
                kvsProducerPutFrame(stream, &audioFrame);
            }
            nextAudioMs += AUDIO_FRAME_INTERVAL_MS;
        }
        {
            uint64_t nextDeadlineMs = nextVideoMs < nextAudioMs ? nextVideoMs : nextAudioMs;
            uint64_t now = now_ms();
            if (nextDeadlineMs > now) {
                uint64_t sleepMs = nextDeadlineMs - now;
                uint64_t sleepUs = sleepMs * 1000ULL;
                if (sleepUs > (uint64_t) UINT_MAX) {
                    sleepUs = (uint64_t) UINT_MAX;
                }
                usleep((useconds_t) sleepUs);
            } else {
                usleep(1000);
            }
        }
    }

    kvsProducerFreeStream(stream);
    return 0;
}

int main(void)
{
    int ret = 0;
    KvsProducerClient* client = NULL;
    AppConfig app;

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    memset(&app, 0, sizeof(app));
    app.triggerIntervalSec = parse_env_int("KVS_EVENT_TRIGGER_SECONDS", DEFAULT_TRIGGER_INTERVAL_SEC);
    app.uploadDurationSec = parse_env_int("KVS_EVENT_UPLOAD_SECONDS", DEFAULT_UPLOAD_DURATION_SEC);

    if (kvsSdkInitialize() != 0) {
        fprintf(stderr, "kvsSdkInitialize failed\n");
        return 1;
    }

    if (load_client_from_env(&client) != 0) {
        kvsSdkDeinitialize();
        return 1;
    }

    if (ring_open(&app.video, DEFAULT_RING_VIDEO_PATH) != 0) {
        fprintf(stderr, "warning: unable to open video ring buffer %s: %s\n", DEFAULT_RING_VIDEO_PATH, strerror(errno));
    }
    if (ring_open(&app.audio, DEFAULT_RING_AUDIO_PATH) != 0) {
        fprintf(stderr, "warning: unable to open audio ring buffer %s: %s\n", DEFAULT_RING_AUDIO_PATH, strerror(errno));
    }

    while (!g_stop) {
        ret = upload_event_window(client, &app);
        if (ret != 0) {
            fprintf(stderr, "event upload failed: %d\n", ret);
        }
        for (int i = 0; i < app.triggerIntervalSec && !g_stop; ++i) {
            sleep(1);
        }
    }

    ring_close(&app.video);
    ring_close(&app.audio);
    kvsProducerClientFree(client);
    kvsSdkDeinitialize();
    return ret == 0 ? 0 : 1;
}
