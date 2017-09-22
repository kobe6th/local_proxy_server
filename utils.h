#ifndef UTILS_H_
#define UTILS_H_

#include "server.h"

static long g_sys_ms = 0;

long get_current_ms();

long get_sys_ms();

void update_sys_ms();

void copy_sockaddr_to_host_t ( struct sockaddr_in *s_addr, host_t *host );

#endif /*UTILS_H_*/