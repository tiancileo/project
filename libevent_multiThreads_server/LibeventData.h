#ifndef __LIEVENTDATA_H__
#define __LIEVENTDATA_H__
#include <pthread.h>
#include <event.h>


typedef struct task {
    void* (*run)(void* arg);
    void* arg;
	struct task * next;
}task_t;


typedef struct{
	int cnt;
	task_t *head;
	task_t *tail;
//	pthread_t owner_tid;
	pthread_mutex_t mutex;
}cq_t;

 void cq_init(cq_t *cq);

 void cq_pushback(cq_t *cq,task_t *task);

 void cq_popfront(cq_t *cq,task_t **ppTask);

 void cq_release(cq_t *cq);

//工作线程
typedef struct{
	pthread_t tid;				//线程id
	struct event_base *base;	//事件根基
	struct event event;
	int read_fd;
	int write_fd;
	cq_t *cnn_queue;			//连接队列
}WORKER;
//#define WORKER worker_t



//任务初始化
 task_t* task_init(void* (*func)(void*), void* arg);


//线程函数  只监听管道的输入端，一有数据便执行worker_cb
void* th_func(void *arg);

//工作回调,从管道中读取一个字符，执行一次任务
void worker_cb(int fd,short event, void *arg);
//退出回调，使每个工作线程 退出event_loop
void* worker_quit_cb( void* arg);
//工作线程初始化工作
WORKER* workers_init(int nums);
//结束工作线程，给工作线程派发结束任务
void workers_quit(WORKER *workers,int nWorkers);
//释放工作线程资源
void workers_release(WORKER *workers,int nWorkers);




#endif
