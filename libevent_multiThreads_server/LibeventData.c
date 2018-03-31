#include "LibeventData.h"
#include <stdio.h>
#include <pthread.h>
#include<malloc.h>
#include <unistd.h>
#include <event.h>
#include <signal.h>
#include <string.h>
#include <event.h>

//可改成宏定义 define XX do{..}while(0)
/****************************  任务队列 API  (cq_t)   ********************************/
void cq_init(cq_t *cq)
{
	cq->cnt = 0;
	cq->head = cq->tail = NULL;
	pthread_mutex_init(&cq->mutex,NULL);
}

void cq_pushback(cq_t *cq,task_t *task)
{
	pthread_mutex_lock(&cq->mutex);
	if(cq->head == NULL){
		cq->head =cq->tail = task;
	}
	else{
		cq->tail->next = task;
		cq->tail = task;
	}
	cq->cnt ++;
	pthread_mutex_unlock(&cq->mutex);
}

void cq_popfront(cq_t *cq,task_t **pptask)
{
	pthread_mutex_lock(&cq->mutex);
	if(cq->head != NULL){
		*pptask = cq->head;
		cq->head = (*pptask)->next;
		cq->cnt--;
	}
	pthread_mutex_unlock(&cq->mutex);
}

void cq_release(cq_t *cq)
{
	if(cq){
		task_t *p = cq->head;
		 task_t *t  = NULL;
		while(p != NULL){
            t = p->next;
			free(p);
			p = t;
		}
	cq->head = cq->tail = NULL;
	pthread_mutex_destroy(&cq->mutex);
	}
}

/****************************   任务 API  (task_t )    ********************************/
//任务初始化
task_t* task_init(void* (*func)(void*), void* arg)
 {
    task_t *task = NULL;
    task = (task_t*)malloc(sizeof(task_t));
    if(task == NULL)
        return NULL;
    task->run = func;
    task->arg = arg;
    task->next = NULL;
    return task;
 };

/************************  工作线程 API  (WORKER )  ********************************/

 void* th_func(void *arg)
{
	WORKER *me = (WORKER*)arg;
	printf("thread %ld is runing !\n",me->tid);

	event_base_loop(me->base,0);//等待管道写入一个字符触发线程回调
	return NULL;
};



void worker_cb(int fd,short event, void *arg)
{
	WORKER *worker = (WORKER*)arg;
	char c;
	read(worker->read_fd, &c, 1);      //这3行必须,
	//从管道读一个字符,便执行一次任务
	//当执行完该次任务后,若管道读缓存区还有字符,Reactor 会再次触发,直至管道读空

	//从任务队列中取出一个任务
	task_t *task  = NULL;
	cq_popfront(worker->cnn_queue,&task);

	//执行真正的任务!
    task->run(task->arg);

	free(task);  //任务运行完释放资源
	task = NULL;
};

 WORKER* workers_init(int nums)
{
    if(nums <= 0)
		return NULL;
	int ret;
	WORKER *workers = (WORKER*)calloc(nums, sizeof(WORKER));
	if(workers == NULL)
		return NULL;
	int i;
	for(i = 0; i < nums; i++){
		//pipe init
		int fd[2];
		ret = pipe(fd);
		if(ret < 0){
			perror("pipe ");
			return NULL;
		}
		workers[i].read_fd = fd[0];
		workers[i].write_fd = fd[1];

		//event init
		struct event_base *base = event_init();
		if(base == NULL){
			perror("event_init ");
			return NULL;
		}
		workers[i].base = base;
		event_set(&workers[i].event, workers[i].read_fd,
						EV_READ | EV_PERSIST, worker_cb,(void*)&workers[i]);
		event_base_set(workers[i].base, &workers[i].event);
		event_add(&workers[i].event,NULL);

		//cq init
		cq_t *cq = (cq_t *)malloc(sizeof(cq_t));
		if(cq == NULL){
			perror("cq_t malloc");
			goto err;
		}
		cq_init(cq);
		workers[i].cnn_queue = cq;

		//pthread init
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		//pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
		ret = pthread_create(&workers[i].tid, &attr,
								th_func,(void*)&workers[i]);
		pthread_attr_destroy(&attr);
		if(ret < 0){
			perror("pthread_create ");
			return NULL;
		}
	}
	return workers;
err:
	workers_release(workers,nums);
	return NULL;
}

void* worker_quit_cb( void* arg)  //退出回调
{
	struct event_base *base = (struct event_base*)arg;
//	event_base_break(base);
//	/*
	struct timeval tv = {1,0};
	printf("After All task finished,thread %ld quits in %d s\n",
			pthread_self(),1);
	event_base_loopexit(base,&tv);
//	*/
}

void workers_quit(WORKER *workers,int nWorkers)
{
	//向所有工作线程添加退出任务
	int i;
	for(i = 0; i < nWorkers; i++){
		task_t *task = task_init(worker_quit_cb,workers[i].base);

		cq_pushback(workers[i].cnn_queue,task);
        write(workers[i].write_fd,"",1);
	}
}

void workers_release(WORKER *workers,int nWorkers)
{
	int i ;
	for(i = 0; i < nWorkers; i++){
		pthread_join(workers[i].tid,NULL);
	}

	for(i = 0; i < nWorkers; i++){
		if(workers[i].base){
			event_base_free(workers[i].base);
			workers[i].base = NULL;
		}
		close(workers[i].read_fd);
		close(workers[i].write_fd);

		if(workers[i].cnn_queue){
			cq_release(workers[i].cnn_queue);
		}
	}
	if(workers){
		free(workers);
		workers = NULL;
	}
}



