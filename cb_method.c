#include "cb_method.h"
#include "log.h"
#include "tcp.h"
#include "utils.h"

static int _test_tcp_connect_result( int fd )
{
    int err = 0;
    socklen_t len = sizeof(int);

    /*
     * BSDs and Linux return 0 and set a pending error in err
     * Solaris returns -1 and sets errno
    */

    if (getsockopt( fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len) == -1)
    {
        err = errno;
    }

    return err;
}

static int _connect_remote(worker_process_t* process, connection_t* client)
{
    connection_t *remote = (connection_t*)malloc(sizeof(connection_t));
    memset(remote, 0, sizeof(connection_t));

    remote->session = client->session;
    client->session->remote = remote;

    client->peer_conn = remote;
    remote->peer_conn = client;

    client->session->stage = SERVER_CONNECT_REMOTE;

    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(struct sockaddr_in));    
    s_addr.sin_family = AF_INET;    
    inet_aton(process->config->target_host, &s_addr.sin_addr);
    s_addr.sin_port = htons(process->config->target_port);
    copy_sockaddr_to_host_t(&s_addr, &remote->peer_host);
    DEBUG_INFO("%s %d", inet_ntoa(s_addr.sin_addr), ntohs(s_addr.sin_port));

    int fd = remote->fd = socket(AF_INET, SOCK_STREAM, 0);

    if ( fd < 0) {
        DEBUG_INFO("create remote socket error, fd:%d, %s:%d", fd, remote->peer_host.hostname, 
            remote->peer_host.port );
        return -1;
    }

    int value = process->config->reuseaddr ==1?1:0;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *) &value, sizeof(int)) == -1){
        DEBUG_INFO("set SO_REUSEADDR fail, fd:%d", fd );
    }

    int flags = fcntl( fd, F_GETFL, 0);
    if (flags < 0) {
        DEBUG_INFO("%s: get socket flags errorfd:%d, %s:%d", fd, remote->peer_host.hostname, 
            remote->peer_host.port );
        return -1;
    }

    if (fcntl( fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        DEBUG_INFO("%s: set remote socket nonblock error,fd:%d, %s:%d", fd, remote->peer_host.hostname, 
            remote->peer_host.port );
        return -1;
    }

    register_session_event( process->epoll_fd, remote, fd, EPOLLOUT|EPOLLIN|EPOLLHUP|EPOLLERR, connect_remote_host_complete_cb );
    int ret = connect( fd, (struct sockaddr*) &s_addr, sizeof (struct sockaddr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            DEBUG_INFO("connect remote error, fd:%d, %s:%d", fd,  remote->peer_host.hostname, 
                remote->peer_host.port );
            DEBUG_INFO("%s %d", strerror(errno), errno);
            return errno;
        }
    }
    if( ret == 0 ){
        DEBUG_INFO("quick connect remote ok, fd:%d, %s:%d", fd,  remote->peer_host.hostname, 
            remote->peer_host.port );
    }

    return 0;
}

// connect remote host completed callback, then reply the result to client
// only for ipv4
void connect_remote_host_complete_cb(  worker_process_t *process, int remote_fd, int events, void *arg)   
{
    connection_t *remote = (connection_t*)arg;
    connection_t *client = remote->session->client;

    if( remote->session->stage != SERVER_CONNECT_REMOTE ){
        close_session( process, remote->session);
        return;
    }

    int error = _test_tcp_connect_result( remote_fd );
    if (error) {
        return;
    }

    remote->session->stage = SERVER_DATA;

    // connect successfully  
    if( events & (EPOLLOUT) ){
        
        struct sockaddr_in local_addr; 
        socklen_t len = sizeof(local_addr);
        getsockname( remote_fd, (struct sockaddr*)&local_addr, &len);
        
        copy_sockaddr_to_host_t(&local_addr, &remote->local_host);
        
        DEBUG_INFO("connect remote ok, fd:%d, local: %s:%d", remote_fd, remote->local_host.hostname, remote->local_host.port );
        clean_recv_buf( remote );
        change_session_event( process->epoll_fd, remote, remote_fd, EPOLLOUT|EPOLLIN|EPOLLHUP|EPOLLERR| EPOLLET, tcp_data_transform_et_cb );

        client->session->stage = SERVER_DATA;
        change_session_event( process->epoll_fd, client, client->fd, EPOLLOUT|EPOLLIN|EPOLLHUP|EPOLLERR| EPOLLET, tcp_data_transform_et_cb );

    }

    return; 

}

void accpect_data_cb (  worker_process_t *process, int client_fd, int events, void *arg)
{
    connection_t *con = (connection_t*)arg;

    if( con->session->stage != SERVER_ACCPECT ){
        DEBUG_INFO("error stage: %d, fd:%d ", con->session->stage, client_fd );
        close_session( process, con->session);
        return;
    }
    
    int len, err;
    len = recv_data_until_length ( con, RECV_BUF_SIZE - con->data_length, &err);
    if( con->eof ){
        //net disconnected. close session
        DEBUG_INFO("disconnected when recv negotiation, len: %d from %s:%d", len,
                con->peer_host.hostname, con->peer_host.port);
        close_session( process, con->session);
        DEBUG_INFO("recv error");
        return;
    }

    con->session->connect_stamp = get_sys_ms();
    int ret = _connect_remote(process, con);
    if(ret < 0){
        DEBUG_INFO("connect remote faild!");
        close_session( process, con->session );
    }

    return;
}

void accept_connect_cb( worker_process_t *process, int listen_fd, int events )    
{    
    int fd;    
    struct sockaddr_in sin;    
    socklen_t len = sizeof(struct sockaddr_in);    
    fd = accept(listen_fd, (struct sockaddr*)&sin, &len);
    if(fd == -1)    
    {    
        if(errno != EAGAIN && errno != EINTR)    
        {    
            DEBUG_INFO("accept error:%s, listen_fd:%d, %s", listen_fd, strerror(errno) );    
        }  
        return;    
    }

    DEBUG_INFO("accept connection success %s:%d", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

    int flags = fcntl( fd, F_GETFL, 0);
    if (flags < 0) {
        DEBUG_INFO("get socket flags error,fd: %d, %s", fd, strerror(errno) );
        close( fd );
        return ;
    }

    // set nonblocking  
    if( fcntl(fd, F_SETFL, flags|O_NONBLOCK) < 0){  
        DEBUG_INFO("fcntl nonblocking error,fd: %d, %s", fd, strerror(errno) );
        close(fd);
        return;
    }
    
    session_t *session = create_session( process, fd );
    if( session == NULL ){
        DEBUG_INFO("no memory,fd: %d", fd );
        close(fd);
        return;
    }

    process->session_num++;
    list_add_tail(&session->list_node, &process->session_list_head);
    connection_t *con = session->client;

    copy_sockaddr_to_host_t( &sin, &con->peer_host );


    len = sizeof(sin);
    getsockname( fd, (struct sockaddr*)&sin, &len);
    copy_sockaddr_to_host_t( &sin, &con->local_host );

    DEBUG_INFO("new connection, %s:%d, sessions: %d, stage:%d",  
        con->peer_host.hostname, con->peer_host.port, process->session_num, session->stage );
    
    clean_recv_buf( con );
    session->stage = SERVER_ACCPECT;
    register_session_event( process->epoll_fd, con, fd, EPOLLIN|EPOLLHUP|EPOLLERR, accpect_data_cb );
    return;
     
}

// while data from client or remote host, then transform to the orther peer
void tcp_data_transform_et_cb(  worker_process_t *process, int fd, int events, void *arg)
{
    connection_t *con = (connection_t*)arg;
    connection_t *peer = con->peer_conn;
    int err = 0;
    int len = 0;
    int ret = 0;
    long total_len = 0;

    if( con->session->stage != SERVER_DATA ){
        DEBUG_INFO("error stage: %d, fd:%d", con->session->stage, fd );
        close_session( process, con->session);
        return;
    }
    
    int up_direct =  0;
    if( fd == con->session->client->fd )
        up_direct = 1;

    if( con->closed ){
        DEBUG_INFO("%s closed by %d, fd:%d, dlen:%d, slen:%d, ", 
            up_direct?"client":"remote", con->session->closed_by, fd, con->data_length, con->sent_length);
        return;
    }
    
    if( events & EPOLLIN)
    {
        con->read = 1;

        for(;;){
            if( con->read ){
                ret = recv_data(process, con, up_direct, &len);
                if(len>0 && up_direct)
                    total_len += len;

                if( ret == TCP_ABORT )
                    break;
                else if( ret == TCP_ERROR ){
                    return;
                }
            }

            if( peer->write ){
                ret = send_data(process, con, up_direct ? 0 : 1, &len);
                if(len>0 && up_direct ? 0 : 1)
                    total_len += len;

                if( ret == TCP_ABORT )
                    break;
                else if( ret == TCP_ERROR ){
                    return;
                }
            }

        }
    }

    if( events & EPOLLOUT )
    {
        con->write = 1;

        for(;;){
            if(con->write){
                ret = send_data(process, peer, up_direct, &len );
                if(len > 0 && up_direct)
                    total_len += len;

                if( ret == TCP_ABORT )
                    break;
                else if( ret == TCP_ERROR ){
                    return;
                }
            }

            if(peer->read){
                ret = recv_data(process, peer, up_direct ? 0 : 1, &len );
                if(len > 0 && up_direct ? 0 : 1)
                    total_len += len;

                if( ret == TCP_ABORT )
                    break;
                else if( ret == TCP_ERROR ){
                    return;
                }
            }

        }
    }


}