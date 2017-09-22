#include <time.h>
#include "utils.h"

// get current time, in ms
long get_current_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long)tv.tv_sec)*1000+((long)tv.tv_usec)/1000;
}

long get_sys_ms()
{
    return g_sys_ms;
}

void update_sys_ms()
{
    g_sys_ms = get_current_ms();
    return;
}

void copy_sockaddr_to_host_t ( struct sockaddr_in *s_addr, host_t *host )
{
    unsigned char * hostname = inet_ntoa(s_addr->sin_addr);
    size_t host_len = strlen(hostname);
    strncpy( host->hostname, hostname, host_len );
    host->port = ntohs(s_addr->sin_port);
    memcpy(&host->ipv4, s_addr, sizeof(struct sockaddr_in));
    return;
}