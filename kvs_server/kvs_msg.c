
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include "nic/socket_api.h"
#include "log_module/fii_log.h"
#include "IPCProtocol.h"
#include "kvs_service.h"
#include "kvs_msg.h"

#define MSG_RECV_BUF_MAX   (2048)

static pthread_t kvs_msg_pthrid = 0;
static bool g_kvs_msg_exit = false;

/**
 * @description: KVS message handling thread for processing control commands
 * @param {void*} arg Thread arguments (unused in current implementation)
 * @return {void*} Always returns NULL upon thread exit
 * @author: galen
 */
static void *kvs_msg_thread(void *arg)
{
    prctl(PR_SET_NAME, __func__);
	fii_log_normal("kvs_msg_thread start!\n");
    int ret = -1;
    int kvs_msg_fd = -1;
    int cli_fd= -1;
    int recv_len = -1;
    int send_len = -1;
    char recv_buf[MSG_RECV_BUF_MAX]={0};
    //char sendbuf[1024] = {0};
    char payload[MSG_RECV_BUF_MAX]={0};
    IPC_HEAD *msg_head;
    RESP_PKT resp_pkt;

    fd_set kvs_msg_set;
	struct timeval time;

    kvs_msg_fd = socket_create_to_listen(AF_UNIX, SOCK_STREAM, 0,fii_get_server_list_info(FII_SOURCE_TYPE_RTSP_SERVER)->m_server_url,0,5);
    if(kvs_msg_fd < 0)
    {
        fii_log_error("socket_listen failed %d\n",kvs_msg_fd);
		kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
        return NULL;
    }
    socket_set_nonblock(kvs_msg_fd);

    while(!g_kvs_msg_exit)
	{
        time.tv_sec = 0;
    	time.tv_usec = 500000;
		FD_ZERO(&kvs_msg_set);
		FD_SET(kvs_msg_fd, &kvs_msg_set);
		ret=select(kvs_msg_fd+1, &kvs_msg_set, NULL, NULL, &time);
		if(ret < 0)
		{
            fii_log_error( "kvs_server_thread select:%s", strerror(errno));
			usleep(100000);
			continue;
		}
		else if(ret == 0)
		{
			usleep(100000);
			continue;
		}
		else
		{
			if(!(FD_ISSET(kvs_msg_fd, &kvs_msg_set)))
			{
                fii_log_error("FD_ISSET:%s", strerror(errno));
				continue;
			}
		}

        if((cli_fd = socket_accept(AF_UNIX,kvs_msg_fd, NULL, NULL)) < 0)
        {
			fii_log_error("The service socket_accept error\n");
			//通知错误，重启KVS
            kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
            usleep(500*1000);
        	break;
        }
        socket_set_nonblock(cli_fd);
		memset(recv_buf , 0 , sizeof(recv_buf));
		recv_len = socket_recv(cli_fd, recv_buf, sizeof(recv_buf), 0, 0);
		if (recv_len <= 0) 
		{            
			fii_log_error("The service recv error\n");
			usleep(50*1000);
        	continue;
		}

		msg_head = (IPC_HEAD *)&recv_buf[0];
		if(msg_head->m_pl)
		{
		   memcpy(&payload,(void*)(recv_buf+sizeof(IPC_HEAD)),msg_head->m_pl);
		}
		
		if(msg_head->m_cmdgroup==FII_MEDIA_MANAGER_CMD)
		{
			switch(msg_head->m_subcmd)
			{
				case FII_MEDIA_STREAM_MAIN_CMD:         
				case FII_MEDIA_STREAM_SUB_CMD:         
				case FII_MEDIA_STREAM_THIRD_CMD:
					
					kvs_handle_media_operation();				
					break; 
					
				default:		
					fii_log_normal("FII_MEDIA_MANAGER_CMD  %d not support\n",msg_head->m_subcmd);
					break;					
			}
		}
		if(msg_head->m_cmdgroup==FII_NETWORK_MANAGER_CMD)
		{
			switch(msg_head->m_subcmd)
			{
				case FII_NET_CMD_APP_PORT:         
					fii_log_normal("kvs server restart\n");
					kvs_service_set_status(KVS_SERVICE_STATUS_ERR);
					kvs_service_stop();
					sleep(1);
					kvs_server_start();			
					break; 
					
				default:		
					fii_log_normal("FII_NETWORK_MANAGER_CMD  %d not support\n",msg_head->m_subcmd);
					break;					
			}
		}
		if(msg_head->m_cmdgroup==FII_APP_MANAGER_CMD)
		{
			switch(msg_head->m_subcmd)
			{
				case FII_APP_CMD_RTSP_SERVER:         
					fii_log_normal("FII_APP_CMD_RTSP_SERVER\n");
				
					if(msg_head->m_source == FII_SOURCE_TYPE_ONVIF_SERVER )
					{

					}			
					break;
					
				default:		
					fii_log_normal("FII_APP_MANAGER_CMD  %d not support\n",msg_head->m_subcmd);
					break;					
			}
		}
		fii_log_normal("recv The kvs server:main_cmd:%d sub_cmd:%d\n",msg_head->m_cmdgroup,msg_head->m_subcmd);
        socket_close(cli_fd);
	}

	socket_close(kvs_msg_fd);
	pthread_exit(NULL);
	return NULL;
}


/**
 * @description: Stops the KVS message handling thread
 * @return {int} Always returns 0 (success)
 * @author: galen
 */
int kvs_msg_thread_stop(void)
{
    fii_log_normal("kvs_msg_thread_stop!\n");
	if (kvs_msg_pthrid != 0)
	{
	    g_kvs_msg_exit = true;
		pthread_join(kvs_msg_pthrid, NULL);
		kvs_msg_pthrid = 0;
	}
	else
	{
	    fii_log_warnning("kvs_msg_pthrid = 0, kvs_msg_thread_stop has been exited.");
	}

	return 0;
}


/**
 * @description: Starts the KVS message handling thread
 * @return {int} 0 on success, error code on failure
 * @author: galen
 */
int kvs_msg_start(void)
{
    int ret = -1;
	if(kvs_msg_pthrid == 0)
	{
		g_kvs_msg_exit = false;
		ret = pthread_create(&kvs_msg_pthrid, NULL, kvs_msg_thread, NULL);
		if(ret)
		{
			fii_log_error("pthread_create error.\n");
			return ret;
		}
	}
	else
	{
	    fii_log_error("kvs_msg_pthrid !=0, kvs_msg_thread has been created.\n");
		return ret;
	}

	ret = 0;
	return ret;
}