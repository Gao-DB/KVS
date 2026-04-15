#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kvs_service.h"

#define STREAMING_WINDOW_MS 12000ULL
#define VIDEO_FRAME_INTERVAL_MS 40ULL
#define AUDIO_FRAME_INTERVAL_MS 20ULL

static int kvs_handle_event(KvsSampleServiceContext *ctx, unsigned int event_id)
{
    char stream_name[128];
    KvsStreamHandle *stream = NULL;
    uint8_t video_payload[1024];
    uint8_t audio_payload[320];
    uint64_t start_ms = 0;
    uint64_t now_ms = 0;
    uint64_t next_video_ms = 0;
    uint64_t next_audio_ms = 0;

    memset(video_payload, 0xAB, sizeof(video_payload));
    memset(audio_payload, 0xCD, sizeof(audio_payload));

    snprintf(stream_name,
             sizeof(stream_name),
             "event-%" PRIu64 "-%u",
             kvs_get_time_ms(),
             event_id);

    if (kvs_create_stream(ctx->client, stream_name, &stream) != 0) {
        fprintf(stderr, "[kvs_server] CreateStream failed for %s\n", stream_name);
        return -1;
    }

    start_ms = kvs_get_time_ms();
    next_video_ms = start_ms;
    next_audio_ms = start_ms;

    while ((now_ms = kvs_get_time_ms()) - start_ms < STREAMING_WINDOW_MS) {
        if (now_ms >= next_video_ms) {
            KvsMediaFrame video_frame;
            video_frame.data = video_payload;
            video_frame.size = sizeof(video_payload);
            video_frame.pts_ms = now_ms;
            video_frame.is_key_frame = ((now_ms - start_ms) % 1000ULL) < VIDEO_FRAME_INTERVAL_MS;
            video_frame.track_name = "video";
            if (kvs_put_media_frame(stream, &video_frame) != 0) {
                fprintf(stderr, "[kvs_server] PutMedia(video) failed for %s\n", stream_name);
                break;
            }
            next_video_ms += VIDEO_FRAME_INTERVAL_MS;
        }

        if (now_ms >= next_audio_ms) {
            KvsMediaFrame audio_frame;
            audio_frame.data = audio_payload;
            audio_frame.size = sizeof(audio_payload);
            audio_frame.pts_ms = now_ms;
            audio_frame.is_key_frame = false;
            audio_frame.track_name = "audio";
            if (kvs_put_media_frame(stream, &audio_frame) != 0) {
                fprintf(stderr, "[kvs_server] PutMedia(audio) failed for %s\n", stream_name);
                break;
            }
            next_audio_ms += AUDIO_FRAME_INTERVAL_MS;
        }

        usleep(10 * 1000);
    }

    kvs_stop_stream(stream);
    fprintf(stdout, "[kvs_server] event %u finished: stream=%s duration=%llums\n",
            event_id,
            stream_name,
            (unsigned long long) (kvs_get_time_ms() - start_ms));
    return 0;
}

int kvs_service_init(KvsSampleServiceContext *ctx, KvsProducerClient *client)
{
    if (ctx == NULL || client == NULL) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
    ctx->client = client;
    return 0;
}

void kvs_service_destroy(KvsSampleServiceContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->mutex);
}

int kvs_service_trigger_event(KvsSampleServiceContext *ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->pending_events++;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
    return 0;
}

void kvs_service_request_stop(KvsSampleServiceContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->mutex);
    ctx->stop_requested = 1;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
}

void *kvs_service_proc(void *arg)
{
    KvsSampleServiceContext *ctx = (KvsSampleServiceContext *) arg;

    if (ctx == NULL) {
        return NULL;
    }

    while (1) {
        unsigned int event_id = 0;

        pthread_mutex_lock(&ctx->mutex);
        while (ctx->pending_events == 0 && !ctx->stop_requested) {
            pthread_cond_wait(&ctx->cond, &ctx->mutex);
        }

        if (ctx->pending_events > 0) {
            ctx->pending_events--;
            ctx->event_sequence++;
            event_id = ctx->event_sequence;
        } else if (ctx->stop_requested) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        pthread_mutex_unlock(&ctx->mutex);

        if (event_id > 0) {
            kvs_handle_event(ctx, event_id);
        }
    }

    return NULL;
}
