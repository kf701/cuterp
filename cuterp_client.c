/*
 * cute reverse proxy client by kf701 2017-11-23
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <signal.h>

int _wait_read_able (int fd, int timeout)
{
    int ret;
    fd_set set;
    struct timeval tv = { timeout / 1000, (timeout % 1000) * 1000 } ;

    FD_ZERO (&set);
    FD_SET (fd, &set);

    ret = select (fd + 1, &set, NULL, NULL, &tv);

    if (ret > 0)
        return 1;

    return 0;
}

int _tcp_connect (char *ip, int port)
{
    int sockfd;
    struct sockaddr_in servaddr;
    struct hostent *hent;

    if (NULL == ip)
        return -1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        return -1;

    hent = gethostbyname(ip);
    if (NULL == hent)
    {
        close(sockfd);
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr = *(struct in_addr *)hent->h_addr;

    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
    {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int _create_and_bind(int port)
{
    int listenfd, optval = 1;
    struct sockaddr_in serveraddr;

    if( ( listenfd = socket( AF_INET, SOCK_STREAM, 0 )) < 0 )
    {
        return -1;
    }

    if( setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int) ) < 0 )
        goto error;

    bzero(( char* )&serveraddr, sizeof(serveraddr) );
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((uint16_t)port);

    if( bind( listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr) ) < 0 )
    {
        goto error ;
    }

    return listenfd;
error:
    close(listenfd);
    return -1;
}

int _make_non_blocking(int sfd)
{
    int flags, s;
    flags = fcntl(sfd, F_GETFL,0);
    if(flags == -1)
    {
        perror("fcntl");
        return-1;
    }

    flags|= O_NONBLOCK;
    s =fcntl(sfd, F_SETFL, flags);
    if(s ==-1)
    {
        perror("fcntl");
        return-1;
    }
    return 0;
}

int _rio_send(int sock, char* buf, int size)
{
    int ret = 0, rest = size;
    while(rest > 0) {
        ret = send(sock, buf, size, 0);
        if (ret <= 0) {
            if (errno == EAGAIN) {
                usleep(100*1000);
                continue;
            }
            break;
        }
        rest -= ret;
    }
    if (rest > 0) return -1;
    return size;
}

#define MAXEVENTS 64

void add_to_epoll(int efd, int sock)
{
    struct epoll_event event;
    event.data.fd = sock;
    event.events= EPOLLIN | EPOLLET;
    event.events= EPOLLIN;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sock, &event)) {
        perror("epoll_ctl");
        abort();
    }
}

#define HEAD_SIZE  8
struct _head_t {
    unsigned char buf[HEAD_SIZE];
    int type;
    int index;
    int length;
};

int _read_head(int sock, struct _head_t * head)
{
    int count = read(sock, head->buf, HEAD_SIZE);
    if (count != HEAD_SIZE) {
        return -1;
    }

    head->type = head->buf[0];
    head->index = head->buf[1];
    head->length = head->buf[4] << 24 | head->buf[5] << 16 | head->buf[6] << 8 |  head->buf[7];
    return HEAD_SIZE;
}

char* g_server_ip   = "139.224.236.202";
int   g_server_port = 5800;
char* g_domain      = "test1";

struct _sock_list_t {
    int index;
    int sock;
};

#define MAX_LIST 100
struct _sock_list_t sock_list[MAX_LIST];

void add_local_sock(int sock, int index)
{
    int i = 0;
    for (i = 0; i < MAX_LIST; i++ )
    {
        if (sock_list[i].index == 0) {
            sock_list[i].index = index;
            sock_list[i].sock = sock;
            break;
        }
    }
}

void del_local_by_sock(int sock)
{
    int i = 0;
    for (i = 0; i < MAX_LIST; i++ )
    {
        if (sock_list[i].sock == sock) {
            sock_list[i].index = 0;
            sock_list[i].sock = 0;
            break;
        }
    }
}

int get_index_by_sock(int sock)
{
    int i = 0;
    for (i = 0; i < MAX_LIST; i++ )
    {
        if (sock_list[i].sock == sock) {
            return sock_list[i].index;
        }
    }
    return 0;
}

int find_local_by_index(int efd, int index)
{
    int i = 0;
    for (i = 0; i < MAX_LIST; i++ )
    {
        if (sock_list[i].index == index) {
            return sock_list[i].sock;
        }
    }

    int local_sock = _tcp_connect("127.0.0.1", 80);
    if (-1 == local_sock) {
        printf("connect to 127.0.0.1 error, exit\n");
        abort();
    }
    _make_non_blocking(local_sock);

    add_local_sock(local_sock, index);

    add_to_epoll(efd, local_sock);

    return local_sock;
}

void send_register_info(int sock)
{
    char buf[128] = {0};
    buf[0] = 'i';
    int len = strlen(g_domain);
    buf[7] = len;
    sprintf(buf+8, "%s", g_domain);
	_rio_send(sock, buf, 8+len);
}

int forward_to_local(int local_sock, int sock, int index, int length)
{
    char buf[4096];
    int count = length, ret = 0, readlen = 0, realsend = 0;

    while (count > 0) {

        if (count > 4096) readlen = 4096;
        else readlen = count ;
        ret = read(sock, buf, readlen);
        if(ret <= 0) {
            if (errno == EAGAIN) {
                usleep(100*1000);
                continue;
            }
            break;
        }
        count -= ret;
        realsend = _rio_send(local_sock, buf, ret);
        if (realsend != ret) {
            printf ("forward_packet, error, send = %d, real = %d, errno = %d\n", ret, realsend, errno);
            break;
        }
    }
    
    return (length - count);
}

int forward_to_server(int local_sock, int sock)
{
    int index = get_index_by_sock(local_sock);
    if (index == 0) return -1;

    char buf[4096], headbuf[8];
    headbuf[0] = 'd';
    headbuf[1] = index;
    int ret = 0, total = 0;

    while (1) {
        ret = read(local_sock, buf, sizeof(buf));
        if (ret <= 0) break;

        headbuf[4] = (ret >> 24) & 0xFF;
        headbuf[5] = (ret >> 16) & 0xFF;
        headbuf[6] = (ret >> 8) & 0xFF;
        headbuf[7] = ret & 0xFF;

        if (8 != _rio_send(sock, headbuf, 8)) {
            break;
        }
        if (ret != _rio_send(sock, buf, ret)) {
            break;
        }

        total += ret;
    }

    return total;
}

int main(int argc,char*argv[])
{
    signal(SIGPIPE,SIG_IGN);

    int sock = _tcp_connect(g_server_ip, g_server_port);
    if (sock == -1) {
        printf ("connect to server error\n");
        abort();
    }
    _make_non_blocking(sock);

    send_register_info(sock);

    int efd = epoll_create1(0);
    if(efd == -1) {
        perror("epoll_create");
        abort();
    }
    add_to_epoll(efd, sock);

    struct epoll_event* events = calloc(MAXEVENTS, sizeof(struct epoll_event));

    struct _head_t head;
    int evn = 0, i = 0, local_sock, nread;

    while (1) 
    {
        evn = epoll_wait(efd, events, MAXEVENTS, -1);

        for(i = 0 ; i < evn ; i++){
            if((events[i].events & EPOLLERR)|| (events[i].events & EPOLLHUP)|| (!(events[i].events & EPOLLIN))) {
                printf("epoll error\n");
                del_local_by_sock(events[i].data.fd);
                close(events[i].data.fd);
                if(sock == events[i].data.fd) {
                    abort();
                }
                continue;
            }

            if(sock == events[i].data.fd) {
                nread = _read_head(sock, &head);
                if (nread != HEAD_SIZE) {
                    if (errno == EAGAIN) {
                        usleep(100*1000);
                        continue;
                    }
                    abort();
                }
             
                local_sock = find_local_by_index(efd, head.index);
                forward_to_local(local_sock, sock, head.index, head.length);
                continue;
            }

            if (forward_to_server(events[i].data.fd, sock) < 0) {
                del_local_by_sock(events[i].data.fd);
                close(events[i].data.fd);
            }
        }
    }

    return 0;
}
