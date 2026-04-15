#include "kvs_producer_sdk_minimal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct KvsProducerClient {
    char region[64];
    char caCertPath[512];
    char clientCertPath[512];
    char clientPrivateKeyPath[512];
};

struct KvsProducerStream {
    char name[256];
    uint64_t frameCount;
    uint64_t byteCount;
};

int kvsSdkInitialize(void)
{
    return 0;
}

void kvsSdkDeinitialize(void)
{
}

int kvsProducerClientCreate(const KvsProducerClientConfig* config, KvsProducerClient** outClient)
{
    KvsProducerClient* client = NULL;
    if (config == NULL || outClient == NULL || config->region == NULL || config->caCertPath == NULL || config->clientCertPath == NULL ||
        config->clientPrivateKeyPath == NULL) {
        return -1;
    }
    if (strlen(config->region) >= sizeof(((KvsProducerClient*) 0)->region) ||
        strlen(config->caCertPath) >= sizeof(((KvsProducerClient*) 0)->caCertPath) ||
        strlen(config->clientCertPath) >= sizeof(((KvsProducerClient*) 0)->clientCertPath) ||
        strlen(config->clientPrivateKeyPath) >= sizeof(((KvsProducerClient*) 0)->clientPrivateKeyPath)) {
        return -1;
    }

    client = (KvsProducerClient*) calloc(1, sizeof(KvsProducerClient));
    if (client == NULL) {
        return -1;
    }

    snprintf(client->region, sizeof(client->region), "%s", config->region);
    snprintf(client->caCertPath, sizeof(client->caCertPath), "%s", config->caCertPath);
    snprintf(client->clientCertPath, sizeof(client->clientCertPath), "%s", config->clientCertPath);
    snprintf(client->clientPrivateKeyPath, sizeof(client->clientPrivateKeyPath), "%s", config->clientPrivateKeyPath);

    *outClient = client;
    return 0;
}

void kvsProducerClientFree(KvsProducerClient* client)
{
    free(client);
}

int kvsProducerCreateStream(KvsProducerClient* client, const char* streamName, KvsProducerStream** outStream)
{
    KvsProducerStream* stream = NULL;
    if (client == NULL || streamName == NULL || outStream == NULL) {
        return -1;
    }

    stream = (KvsProducerStream*) calloc(1, sizeof(KvsProducerStream));
    if (stream == NULL) {
        return -1;
    }

    snprintf(stream->name, sizeof(stream->name), "%s", streamName);
    *outStream = stream;
    return 0;
}

int kvsProducerDescribeStream(KvsProducerClient* client, const char* streamName)
{
    if (client == NULL || streamName == NULL) {
        return -1;
    }
    return 0;
}

int kvsProducerGetDataEndpoint(KvsProducerClient* client, const char* streamName, char* endpointBuffer, size_t endpointBufferLen)
{
    int written = 0;
    if (client == NULL || streamName == NULL || endpointBuffer == NULL || endpointBufferLen == 0) {
        return -1;
    }

    written = snprintf(endpointBuffer, endpointBufferLen, "https://kinesisvideo.%s.amazonaws.com/%s", client->region, streamName);
    if (written < 0 || (size_t) written >= endpointBufferLen) {
        return -1;
    }

    return 0;
}

int kvsProducerPutFrame(KvsProducerStream* stream, const KvsProducerFrame* frame)
{
    if (stream == NULL || frame == NULL || frame->data == NULL || frame->size == 0) {
        return -1;
    }

    stream->frameCount++;
    stream->byteCount += frame->size;
    return 0;
}

void kvsProducerFreeStream(KvsProducerStream* stream)
{
    free(stream);
}
