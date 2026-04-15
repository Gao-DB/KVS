#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "kvs_service.h"

int main(int argc, char *argv[])
{
	int event_idx = 0;
	char event_name[COMMON_STRING_LEN] = {0};
	time_t now = 0;

	fii_log_tag_set("KVS");
    // fii_log_level_set(FII_LOG_LEVEL_MAX);
	kvs_service_set_status(KVS_SERVICE_STATUS_OK);
	kvs_init();

	while(kvs_service_get_status() == KVS_SERVICE_STATUS_OK)
	{
		now = time(NULL);
		snprintf(event_name, sizeof(event_name), "event-file-%ld-%d", (long) now, event_idx++);
		kvs_service_trigger_event(event_name);
		sleep(15);
	}
	
exit01:
	kvs_exit();
	return 0;
}
