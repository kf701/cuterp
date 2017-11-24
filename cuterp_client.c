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
#include <dllist.h>

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

char* g_server_ip   = "172.19.56.33";
int   g_server_port = 5800;
char* g_domain      = "test1";

int g_socks_total = 0;
int g_socks_idle  = 0;

void* sock_thread(void* arg)
{
    pthread_detach(pthread_self());

    int sock = _tcp_connect(g_server_ip, g_server_port);
    if (sock == -1) return NULL;
    _make_non_blocking(sock);

    g_socks_total ++;
    g_socks_idle ++;

	_rio_send(sock, g_domain, strlen(g_domain));

    time_t last = time(NULL);
    int local_sock = -1;
    int nread, nsend;
    char buf[1024];
    int exit_flag = 0;

    while(exit_flag == 0) 
    {
        if ((time(NULL) - last) > 10) {
            if (local_sock != -1) {
                close(local_sock);
                local_sock = -1;
                g_socks_idle ++;
            }
        }

        while (_wait_read_able(sock, 1000))
        {
            last = time(NULL);

            nread = read(sock, buf, 1024);
            if(ret <= 0) {
                if (errno == EAGAIN) {
                    usleep(100*1000);
                    continue;
                }
                printf("read sock error, exit\n");
                exit_flag = 1;
                break;
            }

            if (-1 == local_sock) {
                local_sock = _tcp_connect("127.0.0.1", 80);
                if (-1 == local_sock) {
                    printf("connect to 127.0.0.1 error, exit\n");
                    exit_flag = 1;
                    break;
                }
                g_socks_idle --;
            }

            nsend = _rio_send(local_sock, buf, nread);
            if (nsend != nread) {
                printf ("forward to 127 nsend = %d, nread = %d, errno = %d\n", nsend, nread, errno);
                exit_flag = 1;
                break;
            }
        }

        while (_wait_read_able(local_sock, 1000))
        {
            last = time(NULL);

            nread = read(local_sock, buf, 1024);
            if(ret <= 0) {
                if (errno == EAGAIN) {
                    usleep(100*1000);
                    continue;
                }
                printf("read local sock error, exit\n");
                exit_flag = 1;
                break;
            }

            nsend = _rio_send(sock, buf, nread);
            if (nsend != nread) {
                printf ("forward to server nsend = %d, nread = %d, errno = %d\n", nsend, nread, errno);
                exit_flag = 1;
                break;
            }
        }
    }

    if (local_sock) close(local_sock);
    close(sock);
    return NULL;
}

int main(int argc,char*argv[])
{
    signal(SIGPIPE,SIG_IGN);

    pthread_t tid;

    while(1) {
        if (g_socks_idle == 0 && g_socks_total < 10) {
            if (0 != pthread_create(&tid, NULL, sock_thread, NULL)) {
                perror("create sock thread error");
            }
        }

        sleep (1);
    }

    return 0;
}

