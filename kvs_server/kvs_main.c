#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "kvs_service.h"
#include "kvs_msg.h"

//extern volatile sig_atomic_t kvs_process_exit_flag;

int main(int argc, char *argv[])
{

	fii_log_tag_set("KVS");
    // fii_log_level_set(FII_LOG_LEVEL_MAX);
	kvs_service_set_status(KVS_SERVICE_STATUS_OK);
	kvs_init();

	while(kvs_service_get_status() == KVS_SERVICE_STATUS_OK)
	{
		
		sleep(1);
	}
	
exit01:
	kvs_exit();
	return 0;
}
