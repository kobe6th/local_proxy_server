#ifndef SERVER_H_
#define SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>    
#include <sys/epoll.h>    
#include <sys/ioctl.h>
#include <sys/timeb.h>
#include <sys/msg.h>
#include <netinet/in.h>    
#include <arpa/inet.h>
#include <net/if.h>
#include <math.h>
#include <fcntl.h>    
#include <unistd.h>    
#include <stdio.h>    
#include <errno.h>  
#include <stdlib.h>  
#include <netdb.h>  
#include <string.h> 
#include <signal.h>
#include <setjmp.h>

#include "rbtree.h"
#include "list.h"

#define HOST_NAME_LEN 32
#define RECV_BUF_SIZE 4096
#define MAX_EVENTS 4096

#define SERVER_ACCPECT 1
#define SERVER_CONNECT_REMOTE 2
#define SERVER_DATA 3

#define CLOSE_BY_CLIENT 1
#define CLOSE_BY_SOCKD 2
#define CLOSE_BY_REMOTE 3

typedef struct host_s host_t;
typedef struct session_s session_t;
typedef struct connection_s connection_t;
typedef struct udp_connection_s udp_connection_t;
typedef struct worker_process_s worker_process_t;
typedef struct config_s config_t;

struct host_s
{
    struct sockaddr_in     ipv4;
    unsigned char hostname[HOST_NAME_LEN];
    unsigned int port;
};


struct session_s
{
    connection_t *client;         //client: data connection(tcp), tcp controller(udp)
    connection_t *remote;         //remote: tcp or udp socket
    udp_connection_t *udp_client;     //client: udp socket
    udp_connection_t *udp_remote;     //remote: udp socket


    long connect_stamp;         // stamp of connected
    long close_stamp;           // stamp of closed
    long last_data_stamp;       // last stamp of data send or recv

    unsigned int up_byte_num;
    unsigned int down_byte_num;
    unsigned int total_kbyte_num;

    long session_id;
    rb_node_t rbtree_node;
    list_node list_node;


    int err;
    unsigned int stage:4;
    unsigned int closed:1;
    unsigned int closed_by:2;   // 1:client, 2:sockd, 3:remote

} __attribute__((aligned(sizeof(long))));

// connection_s 和 udp_connection_s 的头部结构应尽可能保持一致
struct connection_s
{    
    int fd;    
    int events;
    void (*call_back)(worker_process_t *process, int fd, int events, void *arg);    

    unsigned int read:1;
    unsigned int write:1;
    unsigned int eof:1;
    unsigned int closed:1;

    session_t *session;  
    connection_t* peer_conn;
    
    host_t peer_host;
    host_t local_host;

    ssize_t data_length; 
    ssize_t sent_length; 
    unsigned char buf[RECV_BUF_SIZE];
} __attribute__((aligned(sizeof(long))));


struct udp_connection_s
{    
    int fd;    
    int events;
    void (*call_back)(worker_process_t *process, int fd, int events, void *arg);

     session_t *session;  
} __attribute__((aligned(sizeof(long))));

struct config_s
{    
    // config of listen socket
    struct in_addr outer_addr_cache;
    char listen_host[HOST_NAME_LEN];
    int listen_port;
    char target_host[HOST_NAME_LEN];
    int target_port;
    int udp_listen_port;
    int listen_backlog;
    int max_sessions;
    
    int recv_buf_size;
    int send_buf_size;
    
    unsigned int reuseaddr;
    unsigned int keepalive;
} __attribute__((aligned(sizeof(long))));


struct worker_process_s
{
    int epoll_fd;
    int listen_fd;
    int session_num;
    config_t* config;
    rb_root_t session_tree_root;
    list_node session_list_head;
} __attribute__((aligned(sizeof(long))));


void copy_sockaddr_to_host_t ( struct sockaddr_in *s_addr, host_t *host );

void register_session_event(int epoll_fd, connection_t *con, int fd, int events, 
            void (*call_back)(worker_process_t *,int, int, void*));

void change_session_event(int epoll_fd, connection_t *con, int fd, int events, 
        void (*call_back)(worker_process_t *,int, int, void*));

session_t *create_session( worker_process_t *process, int fd);

void close_session(worker_process_t *process, session_t *session);

#endif /*SERVER_H_*/