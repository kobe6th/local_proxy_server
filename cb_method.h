#include "server.h"

void connect_remote_host_complete_cb(  worker_process_t *process, int remote_fd, int events, void *arg);

void accpect_data_cb (  worker_process_t *process, int client_fd, int events, void *arg);

void accept_connect_cb( worker_process_t *process, int listen_fd, int events );

void tcp_data_transform_et_cb(  worker_process_t *process, int fd, int events, void *arg);