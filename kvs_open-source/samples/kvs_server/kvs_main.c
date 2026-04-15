#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "kvs_service.h"

static unsigned int parse_arg(const char *value, unsigned int default_value)
{
    char *end_ptr = NULL;
    unsigned long parsed = strtoul(value, &end_ptr, 10);

    if (value == NULL || *value == '\0' || end_ptr == value || *end_ptr != '\0') {
        return default_value;
    }

    return (unsigned int) parsed;
}

int main(int argc, char **argv)
{
    unsigned int event_count = 1;
    unsigned int trigger_interval_sec = 2;
    KvsProducerConfig config;
    KvsProducerClient *client = NULL;
    KvsSampleServiceContext service_ctx;
    pthread_t service_tid;
    unsigned int i = 0;

    if (argc > 1) {
        event_count = parse_arg(argv[1], 1);
    }
    if (argc > 2) {
        trigger_interval_sec = parse_arg(argv[2], 2);
    }

    config.region = getenv("AWS_DEFAULT_REGION");
    if (config.region == NULL || config.region[0] == '\0') {
        config.region = "us-east-1";
    }
    config.access_key = getenv("AWS_ACCESS_KEY_ID");
    config.secret_key = getenv("AWS_SECRET_ACCESS_KEY");
    config.session_token = getenv("AWS_SESSION_TOKEN");

    if (kvs_producer_client_create(&config, &client) != 0) {
        fprintf(stderr, "[kvs_server] failed to initialize KVS producer client\n");
        return 1;
    }

    if (kvs_service_init(&service_ctx, client) != 0) {
        fprintf(stderr, "[kvs_server] failed to initialize service context\n");
        kvs_producer_client_free(client);
        return 1;
    }

    if (pthread_create(&service_tid, NULL, kvs_service_proc, &service_ctx) != 0) {
        fprintf(stderr, "[kvs_server] failed to create kvs_service_proc thread\n");
        kvs_service_destroy(&service_ctx);
        kvs_producer_client_free(client);
        return 1;
    }

    for (i = 0; i < event_count; ++i) {
        fprintf(stdout, "[kvs_server] trigger event %u/%u\n", i + 1, event_count);
        kvs_service_trigger_event(&service_ctx);
        if (i + 1 < event_count) {
            sleep(trigger_interval_sec);
        }
    }

    kvs_service_request_stop(&service_ctx);
    pthread_join(service_tid, NULL);

    kvs_service_destroy(&service_ctx);
    kvs_producer_client_free(client);

    fprintf(stdout, "[kvs_server] all events processed\n");
    return 0;
}
