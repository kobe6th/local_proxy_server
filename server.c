#include "server.h"
#include "tcp.h"
#include "log.h"
#include "utils.h"
#include "cb_method.h"

static int _register_listen_event(int epoll_fd, int fd, int events);
static int _close_listen_socket( worker_process_t *process );
static void _close_conenect(int epoll_fd, connection_t *con );

static int _register_listen_event(int epoll_fd, int fd, int events)    
{    
    struct epoll_event epv = {0, {0}};
    epv.data.fd = fd;  
    epv.events = events;  
    
    int op = EPOLL_CTL_ADD;
    if(epoll_ctl(epoll_fd, op, fd, &epv) < 0)    {
        DEBUG_INFO("epoll listen failed, fd:%d, evnets:%d", fd, events);
        return -1;
    }

    return 0;
} 

static int _close_listen_socket( worker_process_t *process )
{

    if( process->listen_fd == 0)
        return 0;

    struct epoll_event epv = {0, {0}};

    int op = EPOLL_CTL_DEL;
    if( epoll_ctl( process->epoll_fd, op, process->listen_fd, &epv) < 0){
        DEBUG_INFO("epoll del failed, fd:%d", process->listen_fd );
        return -1;
    }

    close( process->listen_fd );
    process->listen_fd = 0;
    return 0;
}

static void _close_conenect(int epoll_fd, connection_t *con )    
{    
    if( con->closed)
        return;
    
    con->closed = 1;

    struct epoll_event epv = {0, {0}};
    epv.data.ptr = con;    
    int fd = con->fd;

    int op = EPOLL_CTL_DEL;
    if( epoll_ctl( epoll_fd, op, con->fd, &epv) < 0)
        DEBUG_INFO("epoll del failed, fd:%d", con->fd );    
    //else  
        //DEBUG_INFO("epoll del ok, fd:%d", con->fd );    

    if( con->fd > 0 ){
        struct linger ling = {0, 0};
        if( setsockopt( con->fd, SOL_SOCKET, SO_LINGER, (void*)&ling, sizeof(ling) ) == -1 )
        {
            DEBUG_INFO("setsockopt(linger) failed, fd:%d, %s", con->fd, strerror(errno));  
        }

        if( close(con->fd ) < 0 ){
            DEBUG_INFO("close socket failed, fd:%d, %s", con->fd, strerror(errno) );   
        }
        else
            con->fd = 0;
    }
    
    DEBUG_INFO("connect closed, fd:%d, peer: %s:%d", fd, con->peer_host.hostname, con->peer_host.port );    
}

void register_session_event(int epoll_fd, connection_t *con, int fd, int events, 
            void (*call_back)(worker_process_t *,int, int, void*))    
{    
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = con;    
    epv.events = events;  
    
    con->fd = fd;    
    con->call_back = call_back;    

    int op = EPOLL_CTL_ADD;
    if(epoll_ctl(epoll_fd, op, fd, &epv) < 0)    
        DEBUG_INFO("epoll add failed, fd:%d, evnets:%d", fd, events);
} 

void change_session_event(int epoll_fd, connection_t *con, int fd, int events, 
        void (*call_back)(worker_process_t *,int, int, void*))    
{    
    struct epoll_event epv = {0, {0}};
    epv.data.ptr = con;    
    epv.events = events;  
    
    con->fd = fd;    
    con->call_back = call_back;    

    int op = EPOLL_CTL_MOD;
    if(epoll_ctl(epoll_fd, op, fd, &epv) < 0)    
        DEBUG_INFO("epoll change failed, fd:%d, evnets:%d", fd, events);    
} 

session_t *create_session( worker_process_t *process, int fd)
{
    session_t *session = (session_t *)malloc(sizeof(session_t)+sizeof(connection_t));
    if( session == NULL ){
        DEBUG_INFO("malloc error,fd: %d", fd );
        return NULL;
    }
    memset( session, 0, sizeof(session_t) );
    
    connection_t *con = (connection_t *)((void *)session+sizeof(session_t));
    if( con == NULL ){
        DEBUG_INFO("malloc error,fd: %d", fd );
        free(session );
        return NULL;
    }

    memset( con, 0, sizeof(connection_t) );
    session->client = con;
    con->session = session;
    con->fd = fd;

    return session;
}

void close_session(worker_process_t *process, session_t *session)
{
    if( session->closed )
        return;

    session->closed = 1;
    session->closed_by = CLOSE_BY_SOCKD;
    
    if( session->client )
    {
        if( session->client->eof )
            session->closed_by = CLOSE_BY_CLIENT;
        _close_conenect( process->epoll_fd, session->client );
    }

    if( session->remote )
    {
        if( session->remote->eof )
            session->closed_by = CLOSE_BY_REMOTE;
        _close_conenect( process->epoll_fd, session->remote );
    }
    if( session->remote ){
        DEBUG_INFO("%s:%d-%s:%d session closed, c-eof:%d, r-eof:%d", 
            session->client->peer_host.hostname, session->client->peer_host.port, 
            session->remote->peer_host.hostname, session->remote->peer_host.port, 
            session->client->eof, session->remote->eof);
         DEBUG_INFO("c-read: %d c-write: %d r-read: %d r-write: %d", 
            session->client->read, session->client->write, session->remote->read, session->remote->write);
    }
    else
    {
        DEBUG_INFO("%s:%d -  session closed, c-eof:%d", 
            session->client->peer_host.hostname, session->client->peer_host.port, session->client->eof );
    }

    process->session_num--;
    list_del(&session->list_node);
    session->close_stamp = get_sys_ms();

    if(session->remote){
        free(session->remote);
        session->remote = NULL;
    }

    free(session);
    session = NULL;

    return;
}   

int _init_listen_socket(  worker_process_t *process)    
{    
    int tries =0;
    int listen_fd = -1;
    int failed = 0;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0); 
    if( listen_fd == -1 ){
        DEBUG_INFO("open socket fail, fd:%d", listen_fd );
        return -1;
    }
    
    process->listen_fd = listen_fd;
    int ret = _register_listen_event( process->epoll_fd, listen_fd, EPOLLIN|EPOLLHUP|EPOLLERR );
    if(ret < 0){
         DEBUG_INFO("register epoll listen events fail, fd:%d", listen_fd );
        return -1;
    }
    
    for( tries=0; tries< 5; tries++ )
    {
        failed = 0;

        if (process->config->reuseaddr ) {
            int value = process->config->reuseaddr ==1?1:0;
            if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *) &value, sizeof(int)) == -1)
            {
                DEBUG_INFO("set SO_REUSEADDR fail, fd:%d", listen_fd );
            }
        }
        
        if (process->config->recv_buf_size ) {
            if (setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, (void *) &process->config->recv_buf_size, sizeof(int)) == -1)
            {
                DEBUG_INFO("set SO_RCVBUF fail, fd:%d", listen_fd );
            }
        }

        if (process->config->send_buf_size ) {
            if (setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, (void *) &process->config->send_buf_size, sizeof(int)) == -1)
            {
                DEBUG_INFO("set SO_SNDBUF fail, fd:%d", listen_fd );
            }
        }

        if (process->config->keepalive ) {
            int value = process->config->keepalive ==1?1:0;
            if (setsockopt(listen_fd, SOL_SOCKET, SO_KEEPALIVE, (void *) &value, sizeof(int)) == -1)
            {
                DEBUG_INFO("set SO_SNDBUF fail, fd:%d", listen_fd );
            }
        }
    
        if( fcntl(listen_fd, F_SETFL, O_NONBLOCK) == -1 ){ // set non-blocking    
            DEBUG_INFO("set O_NONBLOCK failed, fd=%d\n", listen_fd); 
            failed = 1;
            continue;
        }
        

        // bind & listen    
        struct sockaddr_in sin;    
        memset(&sin, 0, sizeof(struct sockaddr_in));    
        sin.sin_family = AF_INET;    
        sin.sin_addr.s_addr = INADDR_ANY;    
        sin.sin_port = htons( process->config->listen_port );  
        
        if( bind(listen_fd, (  struct sockaddr*)&sin, sizeof(sin)) == -1 ){
            failed = 1;
            fprintf(stderr, "try to bind port:%d failed, %s\n", process->config->listen_port, strerror(errno) );
            DEBUG_INFO("bind port:%d failed, fd=%d, %s", 
                process->config->listen_port, listen_fd, strerror(errno) ); 
            //close(listen_fd);
            continue;
        }
        
        if( listen(listen_fd, process->config->listen_backlog ) == -1){
            failed = 1;
            DEBUG_INFO("listen failed, port:%d, backlog:%d, fd=%d\n", 
                process->config->listen_port, process->config->listen_backlog, listen_fd); 
            continue;
        }
        
        if( !failed ){
            break;
        }
    }
    
    if( failed ){
        close( listen_fd );
        process->listen_fd = -1;
        return -1;
    }
    
    return listen_fd;
    
} 

int wait_and_handle_epoll_events( worker_process_t *process, struct epoll_event *events, int timer )
{
    // wait for events to happen 
    int fds = epoll_wait( process->epoll_fd, events, MAX_EVENTS, timer);      
    if(fds < 0){
        if( errno == EINTR ){
            DEBUG_INFO("epoll_wait interrupted, continue.");  
            return 0;
        }
        DEBUG_INFO( "epoll_wait exit, %s", strerror(errno) );  
        return -1;  
    }
    
    int i = 0;
    for( i = 0; i < fds; i++){
        if(events[i].events&(EPOLLIN|EPOLLOUT) )    
        {    
            if(events[i].events&EPOLLIN){
            }

            if( events[i].data.fd == process->listen_fd )
            {
                accept_connect_cb( process, process->listen_fd, events[i].events );
            }
            else
            {
                connection_t *con = (connection_t*)events[i].data.ptr; 
                if( !con || con->closed )
                    continue;
                con->events = events[i].events;
                con->call_back( process, con->fd, events[i].events, con );  
            }   
        }
        if((events[i].events&(EPOLLERR|EPOLLHUP) ))     
        {    
            if( events[i].data.fd == process->listen_fd )
            {
                DEBUG_INFO("epoll error events: %d, listen_fd: %d", 
                    events[i].events, events[i].data.fd );
            }
            else
            {
                connection_t *con = (connection_t*)events[i].data.ptr;  
                if( !con || con->closed ){
                    continue;
                }
                con->events = events[i].events;
                DEBUG_INFO("epoll error events: %d, fd:%d, sock:%s:%d", con->events, 
                    con->fd, con->peer_host.hostname, con->peer_host.port );
                if( con->session )
                    close_session( process, con->session);
            }   
        } 
    }
    return 0;

}

int init_local_server(worker_process_t *process, char* local_gost, int local_port, char* proxy_host, int proxy_port)
{
    config_t *config = process->config;
    strcpy(config->listen_host, local_gost);
    config->listen_port = local_port;
    strcpy(config->target_host, proxy_host);
    config->target_port = proxy_port;
    config->listen_backlog = 2048;
    config->max_sessions = 4096;
    config->recv_buf_size = 4096;
    config->send_buf_size = 4096;
    config->reuseaddr = 1;
    config->keepalive = 1;

    INIT_LIST_HEAD(&process->session_list_head);

    process->epoll_fd = epoll_create(MAX_EVENTS);    
    if(process->epoll_fd <= 0) {
        DEBUG_INFO("create epoll failed:%d, %s", errno, strerror(errno) );  
        return -1;
    }

    int ret = _init_listen_socket(process);
    if(ret < 0){
        DEBUG_INFO("_init_listen_socket faild");
        return -1;
    }
    DEBUG_INFO("listen fd: %d", process->listen_fd);

    return 0;
}

int main(int argc, char **argv)
{
    worker_process_t *process = (worker_process_t *)malloc(sizeof(worker_process_t));
    memset(process, 0, sizeof(worker_process_t));

    config_t* config = (config_t*)malloc(sizeof(config_t));
    memset(config, 0, sizeof(config_t));
    process->config = config;

    int ret = init_local_server(process, "127.0.0.1", 8080, "42.123.76.71", 8080);
    if(ret < 0){
        DEBUG_INFO("init_local_server faild");
        exit(-2);
    }

    struct epoll_event *events = (struct epoll_event *)calloc( MAX_EVENTS, sizeof(struct epoll_event) ); 
    if( events == NULL )
        return ;

    while(1){
        if( wait_and_handle_epoll_events( process, events, 1000 )< 0 )
            break;
        update_sys_ms();
    }

    ret = _close_listen_socket(process);
    if(ret < 0){
        DEBUG_INFO("_close_listen_socket faild");
        exit(-2);
    }

    DEBUG_INFO("server close");
}