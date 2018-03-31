
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include "http.h"
#include "threadpool.h"
#include <errno.h>

#define MAX_EVENTS 65535

static int port = 8004;
static int pth_min = 3;
static int pth_max = 8;
static int linger = 1;
static int isFinished = 0;

struct epoll_event events[MAX_EVENTS];

void sig_handler(int signo){
    isFinished = 1;
    printf("signo =%d,server ends!\n",signo);
    sleep(2);
}

int main(int argc,char **argv)
{
	int c;
	while((c = getopt(argc,argv,"p:i:a:l:")) != -1){
		switch(c){
		case 'p': port = atol(optarg); break;
		case 'i': pth_min = atoi(optarg); break;
		case 'a': pth_max = atoi(optarg); break;
		case 'l': linger = atoi(optarg); break;
		}
	}
	printf("port = %d\n",port);
	struct epoll_event ev;
	int listenfd,epfd,nfds,fd,i;

	signal(SIGINT,sig_handler);

	//创建listen socket
    listenfd = bind_listen(port);

    epfd = epoll_create(MAX_EVENTS);
    if (epfd == -1) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
	ev.events = EPOLLIN;
	conn_t *conn = initConn(epfd,listenfd,EPOLLIN,handle_accept);
	ev.data.ptr = (void*)conn;

	if(epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) == -1){
		err_exit("epoll_ctl:add listenfd ev");
	}
    ThreadPool *pool = ThreadPoolCreate(pth_min, pth_max, linger, NULL);

    while(!isFinished){
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if(errno == EINTR || errno == EAGAIN)
                continue;
            else
                err_exit("epoll_wait");
        }
        for (i = 0; i < nfds; ++i) {
             conn_t *conn = (conn_t*)events[i].data.ptr;
             fd = conn->fd;

            if (fd == listenfd ) {
                //handle_accept(conn);
                conn->cb(conn);
            }
            else if (events[i].events & EPOLLIN) {
                ThreadPoolQueue(pool,th_fn,(void*)conn);
            }
        }
    }

    ThreadPoolWait(pool);
    ThreadPoolDestroy(pool);
    printf("end!");
    return 0;
}
