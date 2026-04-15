#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../internal/kvs_producer_internal.h"

int kvs_default_network_send_request(const char *action, const char *payload)
{
    const char *simulate_failure = NULL;

    if (action == NULL || payload == NULL) {
        return -1;
    }

    simulate_failure = getenv("KVS_SIMULATE_NET_FAIL");
    if (simulate_failure != NULL && strcmp(simulate_failure, "1") == 0) {
        return -1;
    }

    fprintf(stdout, "[kvs-net] action=%s payload=%s\n", action, payload);
    fflush(stdout);
    return 0;
}
