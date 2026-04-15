#ifndef __KVS_MSG_H_
#define __KVS_MSG_H_

/**
 * @description: Starts the KVS message handling thread
 * @return {int} 0 on success, error code on failure
 * @author: galen
 */
int kvs_msg_start(void);

/**
 * @description: Stops the KVS message handling thread
 * @return {int} Always returns 0 (success)
 * @author: galen
 */
int kvs_msg_thread_stop(void);

#endif // !__KVS_MSG_H_