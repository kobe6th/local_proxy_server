#include "tcp.h"
#include "log.h"

static int _recv ( connection_t *con, int size, int *err )
{
    int total = 0;  

    if (con->data_length > con->sent_length && con->sent_length > 0){
        int unsent_len = con->data_length - con ->sent_length;
        memmove(con->buf, con->buf + con->sent_length, unsent_len);
        con->data_length = unsent_len;
        con->sent_length = 0;
        memset(con->buf + con->data_length, 0, RECV_BUF_SIZE - con->data_length);
        size = RECV_BUF_SIZE - con->data_length;
    }
    
    if( con->data_length >= RECV_BUF_SIZE ){
        DEBUG_INFO("buf full,no recv, fd: %d, dlen:%d, slen:%d, expect:%d, recv:%d", 
            con->fd, con->data_length, con->sent_length,  size, total );
        return 0;
    }

    do{
        int will_read = size;
        if( con->data_length+size >RECV_BUF_SIZE ){
            will_read = RECV_BUF_SIZE - con->data_length;
        }
        if( will_read <=0 ){
            DEBUG_INFO("recv size error, fd: %d, dlen:%d, slen:%d, expect:%d, recv:%d", 
                con->fd, con->data_length, con->sent_length,  size, total );
            return 0;
        }

        int len = recv(con->fd, &con->buf[con->data_length], will_read, MSG_DONTWAIT ); //MSG_WAITALL
        DEBUG_INFO("fd:%d recv data len: %d", con->fd, len);
        con->session->last_data_stamp = get_sys_ms();

        if (len > 0)
        {
            con->data_length += len;
            total += len;
            return total;
        }
        else if( len < 0 )
        {
            *err = errno;
            if (*err == EAGAIN)
            {   
                DEBUG_INFO("recv EAGAIN : fd: %d, dlen:%d, slen:%d, expect:%d, recv:%d", 
                    con->fd, con->data_length, con->sent_length, size, total );
                break;
            }

            else if (*err == EINTR )
            {
                DEBUG_INFO("recv EINTR : fd: %d, dlen:%d, slen:%d, expect:%d, recv:%d", 
                    con->fd, con->data_length, con->sent_length, size, total );
                continue;
            }
            else
            {
                DEBUG_INFO("recv error:%d, %s. fd: %d, dlen:%d, slen:%d, expect:%d, recv:%d", 
                    *err, strerror(*err), con->fd, con->data_length, con->sent_length, size, total );
                return -1;
            }
        }
        else if( len == 0 ){ 
            DEBUG_INFO("eof. recv eof. fd:%d, dlen:%d, slen:%d, expect:%d, recv:%d",
                con->fd, con->data_length, con->sent_length, size, total );
            con->eof = 1;
            return -1;
        }

    }
    while( 1 );
    
    return total;

}

static int _send( connection_t *con, int send_fd, int *err )
{
    int total = 0;  
    // will send size 
    int size = con->data_length-con->sent_length;
    if( size <=0 | size+con->sent_length>RECV_BUF_SIZE|| con->sent_length < 0 || 
        con->sent_length >=RECV_BUF_SIZE || con->data_length<=0 || con->data_length>RECV_BUF_SIZE ){
        DEBUG_INFO("buf error, fd:%d, send_fd: %d, dlen:%d, slen:%d", con->fd, send_fd, 
            con->data_length, con->sent_length );
        return -1;
    }
    
    do{
        int len = send(send_fd, &con->buf[con->sent_length], size, MSG_DONTWAIT ); //MSG_WAITALL
        DEBUG_INFO("fd:%d send data len: %d", send_fd, len);
        con->session->last_data_stamp = get_sys_ms();
        if (len > 0)
        {
            con->sent_length += len;
            total += len;
            return total;
        }
        else if( len == 0 ){ 
            DEBUG_INFO("net disconnected when send data. fd: %d, dlen:%d, slen:%d, size:%d", 
                send_fd, con->data_length, con->sent_length, size );
            return -1;
        }
        else{
            *err = errno;
            if (*err == EAGAIN)
            {
                DEBUG_INFO("send EAGAIN, fd: %d, dlen:%d, size:%d, %s", 
                    send_fd, con->data_length, size, strerror(errno)  );
                break;
            }

            if (*err == EINTR)
            {
                DEBUG_INFO("send EINTR, fd: %d, %s", send_fd, strerror(*err)  );
                continue;
            }
            DEBUG_INFO("send error:%d, %s, fd: %d", *err, strerror(*err), send_fd );
            return -1;
        }
        
    }
    while( 1 );
    
    
    return con->sent_length;

}

int recv_data(worker_process_t* process, connection_t *con, int up_direct, int* len)
{
    if(!con->eof){
        *len = 0;
        int err = 0;

        *len = _recv( con, RECV_BUF_SIZE-con->data_length, &err);
        DEBUG_INFO("just only %s recv, fd:%d, dlen:%d, slen:%d", 
            up_direct?"client":"remote", con->fd, con->data_length, con->sent_length );

        if( *len <0 || con->eof == 1) {
            DEBUG_INFO("%s recv eof:%d, fd:%d, dlen:%d, slen:%d, len: %d, errno:%d, %s",
                up_direct?"client":"remote", con->eof, con->fd, con->data_length, con->sent_length, *len, err, strerror(err) );
            if( con->eof && con->data_length == 0){
                con->session->err = err;
                close_session( process, con->session);
            }
            return TCP_ERROR;
        }

        if(err == EAGAIN){
            con->read = 0;
            return TCP_ABORT;
        }

        if (*len == 0){
            return TCP_ABORT;
        }
    }
    else{
        if (con->data_length == 0){
            close_session( process, con->session);
        }
        else
            DEBUG_INFO("recv eof, but remain data no sent %d, fd:%d", 
                con->data_length - con->sent_length, con->fd);

        return TCP_ABORT;
    }

    return TCP_OK;
}

int send_data(worker_process_t* process, connection_t *con, int up_direct, int* len)
{
    *len = 0;
    int err = 0;
    connection_t* peer = con->peer_conn;

    if(!peer){
        close_session( process, con->session);
        return TCP_ERROR;
    }

    if( con->data_length > con->sent_length ){
        DEBUG_INFO("continue, send to %s , fd:%d, recv_fd:%d, dlen:%d, slen:%d", 
            up_direct?"client":"remote", peer->fd, con->fd, con->data_length, con->sent_length);
        
        *len = _send( con, peer->fd, &err );
        if( *len < 0 ) {
            if( err == EPIPE || err == ECONNRESET){
                peer->eof = 1;
                peer->session->err = err;
                close_session( process, con->session);
            }
            DEBUG_INFO( "%s send eof:%d, fd:%d, recv_fd:%d, len: %d, errno:%d, %s",
                up_direct?"client":"remote", peer->eof, con->fd, peer->fd, con->data_length, con->sent_length, *len, errno, strerror(errno) );
            return TCP_ERROR;
        }
        
        if( con->sent_length == con->data_length && con->data_length>0 ){
            clean_recv_buf( con );
        }

        if (con->data_length == 0 && con->eof){
            close_session( process, con->session );
            return TCP_ABORT;
        }

        if(err == EAGAIN){
            peer->write = 0;
            return TCP_ABORT;
        }
    }
    else{
        return TCP_ABORT;
    }

    return TCP_OK;
}

void clean_recv_buf( connection_t *con )
{
    memset( con->buf, 0, RECV_BUF_SIZE );
    con->data_length = 0;
    con->sent_length = 0;
}

int recv_data_until_length( connection_t *con, int length )
{
    int err = 0;
    while( con->data_length < length)
    {
        int len = _recv ( con, length-con->data_length, &err );
        if( len<=0 )
            break;
    }
    return con->data_length;
}