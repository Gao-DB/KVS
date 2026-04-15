#ifndef KVS_PRODUCER_INTERNAL_H
#define KVS_PRODUCER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "kvs_producer_sdk.h"

#define KVS_STREAM_NAME_MAX_LEN 128
#define KVS_TRACK_NAME_MAX_LEN 64
#define KVS_PAYLOAD_MAX_LEN 512

int kvs_client_validate_config(const KvsProducerConfig *config);
void kvs_client_copy_config(KvsProducerConfig *dst, const KvsProducerConfig *src);

int kvs_default_serialize_create_stream(const char *stream_name, char *buffer, size_t buffer_len);
int kvs_default_serialize_frame(const char *stream_name, const KvsMediaFrame *frame, char *buffer, size_t buffer_len);

int kvs_default_network_send_request(const char *action, const char *payload);

uint64_t kvs_time_now_ms(void);

#endif
