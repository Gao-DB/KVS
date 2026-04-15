#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "kvs_service.h"

static volatile sig_atomic_t g_exit = 0;

static void on_signal(int sig)
{
    (void) sig;
    g_exit = 1;
}

static int parse_int_arg(const char* s, int default_value, int min_value)
{
    char* endptr = NULL;
    long value;

    if (s == NULL || s[0] == '\0') {
        return default_value;
    }

    errno = 0;
    value = strtol(s, &endptr, 10);
    if (errno != 0 || endptr == s || *endptr != '\0' || value > INT_MAX || value < min_value) {
        return default_value;
    }

    return (int) value;
}

int main(int argc, char* argv[])
{
    int event_count = 1;
    int trigger_interval_sec = 2;

    if (argc > 1) {
        event_count = parse_int_arg(argv[1], 1, 1);
    }
    if (argc > 2) {
        trigger_interval_sec = parse_int_arg(argv[2], 2, 0);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (kvs_service_start() != 0) {
        fprintf(stderr, "kvs_service_start failed\n");
        return 1;
    }

    fprintf(stderr, "[kvs] simulate %d event trigger(s), interval=%ds\n", event_count, trigger_interval_sec);
    for (int i = 0; i < event_count && !g_exit; ++i) {
        kvs_service_trigger_event_upload();
        if (i + 1 < event_count) {
            sleep(trigger_interval_sec);
        }
    }

    while (!g_exit && kvs_service_get_status() == KVS_SERVICE_STATUS_OK) {
        sleep(1);
    }

    kvs_service_stop();
    return kvs_service_get_status() == KVS_SERVICE_STATUS_OK ? 0 : 1;
}
