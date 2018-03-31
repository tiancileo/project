#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "LibeventData.h"
#include <event.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

#define PORT 8000

static int lastTid = 0;
static WORKER *workers = NULL;
struct event_base* base = NULL;
static int nWorkers = 4;


void accept_cb(int fd, short events, void* arg);
void dispatch_cb(struct bufferevent* bev, void* arg);


typedef struct {
    struct bufferevent *bev;
    void *arg;
}conn_t;


//deal with signal SIGINT,exit from event_loop in 1s.
void sigint_cb(int fd, short event, void *arg)
{
    struct timeval tv_1s = {1,0};
    printf("SIGINT : EXIT IN 1s\n");
    event_base_loopexit(base,&tv_1s);
}

//
void *do_request(void* arg){
    conn_t * cnn = (conn_t*)arg;
    char msg[BUFSIZ];


    size_t len = bufferevent_read(cnn->bev, msg, sizeof(msg));


    msg[len] = '\0';
    printf("recv the client msg: %s", msg);

    char reply_msg[4096] = "I have recvieced the msg: ";

    strcat(reply_msg + strlen(reply_msg), msg);
    bufferevent_write(cnn->bev, reply_msg, strlen(reply_msg));





}


void err_exit(const char* s){
	perror(s);
	exit(1);
}



//设置socket 连接为非阻塞方式
void setnonblock(int sockfd){
	int opts = fcntl(sockfd, F_GETFL);
    if(opts < 0) {
		err_exit("fcntl(F_GETFL)");
	}
	opts |= O_NONBLOCK;
	if(fcntl(sockfd,F_SETFL,opts) < 0){
		err_exit("fcntl(F_SETFL)");
	}
}



int bind_listen(int port){

    int listenfd;
    struct sockaddr_in local;

    	//创建listen socket
	if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		err_exit("sockfd\n");
	}
	//设置地址可重用
	int reuse = 1;
	setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

	//设置非阻塞
	setnonblock(listenfd);

	bzero(&local, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(port);

	if(bind(listenfd, (struct sockaddr*)&local,sizeof(local)) < 0){
		err_exit("bind");
	}
	if(listen(listenfd, 20) < 0 )
		err_exit("listen");
    return listenfd;
}



void accept_cb(int fd, short events, void* arg)
{
    evutil_socket_t sockfd;

    struct sockaddr_in client;
    socklen_t len = sizeof(client);

    sockfd = accept(fd, (struct sockaddr*)&client, &len );
    evutil_make_socket_nonblocking(sockfd);

    printf("accept a client %d\n", sockfd);

    struct event_base* base = (struct event_base*)arg;

    struct bufferevent* bev = bufferevent_socket_new(base, sockfd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, dispatch_cb, NULL, NULL, arg);

    bufferevent_enable(bev, EV_READ | EV_PERSIST);
}


void dispatch_cb(struct bufferevent* bev, void* arg)
{

    conn_t * cnn = (conn_t*)malloc(sizeof(conn_t));
    if(cnn == NULL)   err_exit("malloc error");
    cnn->bev = bev;
    cnn->arg = arg;

    //负载均衡
    int tid = (lastTid + 1) % nWorkers;
    WORKER* worker = workers + tid;
    lastTid = tid;

    task_t *task = task_init(do_request, (void*) cnn);
    cq_pushback(worker->cnn_queue,task);
    //触发(通知)指定线程执行任务
    write(worker->write_fd,"",1);

}



int main()
{
    //开启工作线程
	workers = workers_init(nWorkers);
    //创建监听套接字
    int listenfd = bind_listen(PORT);
    //创建事物根基
    base = event_base_new();

    struct event* ev_listen = event_new(base, listenfd, EV_READ | EV_PERSIST,
                                        accept_cb, base);
    event_add(ev_listen, NULL);
    //SIGINT 信号事件
    struct event *sigint_ev;
    sigint_ev = evsignal_new(base, SIGINT, sigint_cb, NULL);
    event_add(sigint_ev,NULL);


    event_base_dispatch(base);


    event_base_free(base);

    workers_quit(workers,nWorkers);
	workers_release(workers,nWorkers);

	printf("End!\n");

    return 0;

}




