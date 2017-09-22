#ifndef TCP_H_
#define TCP_H_

#include "server.h"

#define TCP_OK          0
#define TCP_ABORT       -1
#define TCP_ERROR       -2

void clean_recv_buf( connection_t *con );

int recv_data(worker_process_t* process, connection_t *con, int up_direct, int* len);

int send_data(worker_process_t* process, connection_t *con, int up_direct, int* len);
#endif /*TCP_H_*/