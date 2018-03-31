#ifndef __HTTP_H
#define __HTTP_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h>

void err_exit(const char* s);

struct conn_t;
typedef void (*EP_CALLBACK)(struct conn_t *);

typedef struct conn_t {
    int epfd;
    int fd;
    short event;
    EP_CALLBACK cb;
    char buf[BUFSIZ];
}conn_t;


conn_t * initConn(int epfd, int fd, short event,EP_CALLBACK cb);

//设置socket 连接为非阻塞方式
void setnonblock(int sockfd);

void do_request(conn_t *conn);

void handle_accept(conn_t *conn);

int bind_listen(int port);

void* th_fn(void *argc);


#endif // __HTTP_H
