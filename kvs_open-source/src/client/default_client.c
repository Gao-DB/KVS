#include <string.h>

#include "../internal/kvs_producer_internal.h"

int kvs_client_validate_config(const KvsProducerConfig *config)
{
    if (config == NULL) {
        return -1;
    }

    if (config->region == NULL || config->region[0] == '\0') {
        return -1;
    }

    return 0;
}

void kvs_client_copy_config(KvsProducerConfig *dst, const KvsProducerConfig *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }

    memcpy(dst, src, sizeof(*dst));
}
