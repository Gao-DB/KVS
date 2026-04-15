#ifndef KVS_PRODUCER_SDK_MINIMAL_H
#define KVS_PRODUCER_SDK_MINIMAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_ENV_CA_CERT_PATH "AWS_KVS_CA_CERT_PATH"
#define KVS_ENV_CLIENT_CERT_PATH "AWS_KVS_CLIENT_CERT_PATH"
#define KVS_ENV_CLIENT_PRIVATE_KEY_PATH "AWS_KVS_CLIENT_PRIVATE_KEY_PATH"

typedef struct KvsProducerClient KvsProducerClient;
typedef struct KvsProducerStream KvsProducerStream;

typedef struct {
    const char* region;
    const char* caCertPath;
    const char* clientCertPath;
    const char* clientPrivateKeyPath;
} KvsProducerClientConfig;

typedef struct {
    const uint8_t* data;
    size_t size;
    uint64_t ptsMs;
    int isKeyFrame;
    int isAudio;
} KvsProducerFrame;

int kvsSdkInitialize(void);
void kvsSdkDeinitialize(void);

int kvsProducerClientCreate(const KvsProducerClientConfig* config, KvsProducerClient** outClient);
void kvsProducerClientFree(KvsProducerClient* client);

int kvsProducerCreateStream(KvsProducerClient* client, const char* streamName, KvsProducerStream** outStream);
int kvsProducerDescribeStream(KvsProducerClient* client, const char* streamName);
int kvsProducerGetDataEndpoint(KvsProducerClient* client, const char* streamName, char* endpointBuffer, size_t endpointBufferLen);
int kvsProducerPutFrame(KvsProducerStream* stream, const KvsProducerFrame* frame);
void kvsProducerFreeStream(KvsProducerStream* stream);

#ifdef __cplusplus
}
#endif

#endif
