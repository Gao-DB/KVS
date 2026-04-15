#ifndef KVS_SAMPLE_SERVICE_H
#define KVS_SAMPLE_SERVICE_H

#include <pthread.h>

#include "kvs_producer_sdk.h"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int stop_requested;
    unsigned int pending_events;
    unsigned int event_sequence;
    KvsProducerClient *client;
} KvsSampleServiceContext;

void *kvs_service_proc(void *arg);
int kvs_service_init(KvsSampleServiceContext *ctx, KvsProducerClient *client);
void kvs_service_destroy(KvsSampleServiceContext *ctx);
int kvs_service_trigger_event(KvsSampleServiceContext *ctx);
void kvs_service_request_stop(KvsSampleServiceContext *ctx);

#endif
