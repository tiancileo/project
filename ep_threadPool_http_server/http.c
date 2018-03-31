#include "http.h"

void err_exit(const char* s){
	perror(s);
	exit(1);
}

conn_t * initConn(int epfd, int fd, short event,EP_CALLBACK cb){
    conn_t *conn =(conn_t *) malloc(sizeof(conn_t));
    if(!conn)   err_exit("malloc conn_t");
    conn->epfd = epfd;
    conn->fd = fd;
    conn->event = event;
    conn->cb = cb;
    return conn;
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

void do_request(conn_t *conn){

    struct epoll_event ev;
    int n = 0,nread,nwrite;
    char cbuf [BUFSIZ];

    while ((nread = read(conn->fd, conn->buf + n, BUFSIZ-1)) > 0) {
        n += nread;
    }
    conn->buf[n]= '\0';
    if (nread == -1 && errno != EAGAIN) {
        perror("read error");
    }
    //printf("%s\n",conn->buf);
    //注：用谷歌浏览器输入url,浏览器会发两次http请求
    //多余的一次是/favorite.icon
    sprintf(conn->buf,"hello http!");
    sprintf(cbuf, "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin:*\r\nContent-Length: %d\r\n\r\n%s", strlen(conn->buf),conn->buf);
    strcpy(conn->buf,cbuf);
    int  data_size = strlen(conn->buf);
    n = data_size;
    while (n > 0) {
        nwrite = write(conn->fd, conn->buf + data_size - n, n);
        if (nwrite < n) {
            if (nwrite == -1 && errno != EAGAIN) {
                perror("write error");
            }
            break;
        }
        n -= nwrite;
    }
    //do not keepAlive ,响应完退出并清除
    ev.data.fd = conn->fd;
    ev.events = conn->event;
    epoll_ctl(conn->epfd, EPOLL_CTL_DEL,conn->fd, &ev);
    close(conn->fd);
}

void handle_accept(conn_t *conn){
	struct sockaddr_in remote ;
	int connfd,addrlen = sizeof(remote);
	struct epoll_event ev;

	while(( connfd = accept(conn->fd,(struct sockaddr*)&remote, (socklen_t*)&addrlen)) > 0){
	    printf("fd =%d,port=%d\n",connfd,remote.sin_port);

		setnonblock(connfd);

		conn_t* cl_cnn = initConn(conn->epfd,connfd,EPOLLIN | EPOLLET,do_request);

		ev.events = EPOLLIN | EPOLLET;
		ev.data.ptr =(void*) cl_cnn;

		if (epoll_ctl(conn->epfd, EPOLL_CTL_ADD,connfd,&ev) == -1){
			err_exit("epoll_ctl:add connfd");
		}
	}
	if(connfd == -1){
		if(errno != EAGAIN && errno != EINTR)
		perror("accept");
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

void* th_fn(void *argc){
    pthread_detach(pthread_self());
    conn_t *conn = (conn_t*)argc;
    printf("do_request\n");
    //do_request(conn);
    conn->cb(conn);
    free(conn);
    return NULL;
}
