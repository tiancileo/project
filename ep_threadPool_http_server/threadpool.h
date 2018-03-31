#ifndef  __THREADPOOL_H
#define  __THTREADPOOL_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

typedef void* (*JOB_CALLBACK)(void *);

#define ACTIVES_ADD(item, list) do { \
 item->prev = NULL;                  \
 item->next = list;                  \
 if(list != NULL) list->prev = item; \
 list = item;                        \
} while(0)

#define ACTIVES_REMOVE(item, list) do {                 \
 if (item->prev != NULL) item->prev->next = item->next; \
 if (item->next != NULL) item->next->prev = item->prev; \
 if (list == item) list = item->next;                   \
 item->prev = item->next = NULL;                        \
 free(item);                                            \
} while(0)


typedef struct NJOB {
    struct NJOB *next;
    JOB_CALLBACK func;
    void *arg;
} nJob; //任务

typedef struct NWORKER {
    struct NWORKER *prev;
    struct NWORKER *next;
    pthread_t active_tid;
} nWorker;//活跃线程

typedef struct THREADPOOL {
    pthread_mutex_t mtx;  //同步
//*用于线程池销毁,
//在ThreadPoolDestroy(pool)中 * 等待 *所有线程销毁
//在WorkerCleanup(pool) (在线程函数中,退出while(1),以线程清理函数弹栈调用),当线程数为0(所有线程销毁)发出 * 广播 *broadcast
    pthread_cond_t busycv;

//*用于线程函数中任务等待
//在WorkerThread 中,任务队列空时* 等待 * 新任务的到来
//在ThreadPoolQueue 中,有空闲线程(正在等待) 发出* 通知(signal) *
    pthread_cond_t workcv;

//*用于线程池空闲等待(任务队列清空,活跃线程队列清空)
//在ThreadPoolWait 中,* 等待 *任务队列清空,活跃线程队列清空
//在NotifyWaiters(pool)中,当任务队列清空,活跃线程队列清空,发出* 广播 *
    pthread_cond_t waitcv;

    nWorker *actives;		//活跃线程队列
    nJob *head;			    //任务队列头
    nJob *tail;			    //任务队列尾
    pthread_attr_t attr;	//线程属性

    int flags;				//等待标记 POOL_WAIT 0X01 POOL_DESTROY 0X02
    unsigned int linger;	//线程允许等待时长
    int minimum;			//线程池线程数下限
    int maximum;			//线程池线程数上限
    int nthreads;		    //线程池当前线程数
    int idle;	            //线程池空闲线程数(正在等待的)

} ThreadPool;

static void* WorkerThread(void *arg);

#define POOL_WAIT   0x01
#define POOL_DESTROY  0x02
static sigset_t fillset;

//线程创建
//在线程函数中退出 线程清理函数调用,当任务队列未清空,线程数未达上限,新增线程增加执行力
//在线程池添加任务函数中 调用(主要:新建线程执行任务)
 int WorkerCreate(ThreadPool *pool) ;

//线程销毁时机
//1.有多余(超过下限)空闲线程且等待超时
//2.销毁线程池时,销毁活跃线程队列中的线程

//线程清理函数 (线程函数中,退出while(1)循环,以线程清理函数调用)
//这是线程清理程序,运行这个程序代表有线程即将销毁了
 void WorkerCleanup(ThreadPool * pool) ;

//线程函数
 void* WorkerThread(void *arg) ;

//线程属性拷贝
 void CloneAttributes(pthread_attr_t *new_attr, pthread_attr_t *old_attr);

// 创建线程池
ThreadPool *ThreadPoolCreate(int min_threads, int max_threads, int linger, pthread_attr_t *attr) ;
//压栈任务入线程池
int ThreadPoolQueue(ThreadPool *pool, JOB_CALLBACK func, void *arg);

//线程池等待
void ThreadPoolWait(ThreadPool *pool);

//线程池销毁
void ThreadPoolDestroy(ThreadPool *pool) ;

#endif  // __THREADPOOL_H
