#include "threadpool.h"

 int WorkerCreate(ThreadPool *pool) {
    sigset_t oset;
    pthread_t thread_id;
//创建线程时屏蔽信号集
    pthread_sigmask(SIG_SETMASK, &fillset, &oset);
    int error = pthread_create(&thread_id, &pool->attr, WorkerThread, pool);
    pthread_sigmask(SIG_SETMASK, &oset, NULL);
//创建完屏蔽信号集复原
    return error;
}


//线程销毁时机
//1.有多余(超过下限)空闲线程且等待超时
//2.销毁线程池时,销毁活跃线程队列中的线程

//线程清理函数 (线程函数中,退出while(1)循环,以线程清理函数调用)
//这是线程清理程序,运行这个程序代表有线程即将销毁了
 void WorkerCleanup(ThreadPool * pool) {
    if(( pool->actives ||pool->head )&& pool->flags & POOL_DESTROY ) printf("cancel quit\n");
    else if(pool->minimum < pool->nthreads ) printf("timeout quit\n");
    else printf("normal  quit\n");

    --pool->nthreads;
    if (pool->flags & POOL_DESTROY) {
        if (pool->nthreads == 0) {
            pthread_cond_broadcast(&pool->busycv);
        }
    }
    else if (pool->head != NULL && pool->nthreads < pool->maximum && WorkerCreate(pool) == 0) {
    //任务队列尚未清空,线程数量未达上限,创建新线程,线程数+1,
        pool->nthreads ++;
       // printf("heavy load,create thread ,nums = %d\n",pool->nthreads);
    }
//释放锁
    pthread_mutex_unlock(&pool->mtx);
}
//通知线程池不用等待(无任务,无工作线程才通知)
 void NotifyWaiters(ThreadPool *pool) {
    if (pool->head == NULL && pool->actives == NULL) {
        pool->flags &= ~POOL_WAIT;//任务为空,且工作线程为空,取消等待
        pthread_cond_broadcast(&pool->waitcv);//该线程池已经处于空闲,通知不用等待
    }
}

//线程函数
 void* WorkerThread(void *arg) {
    ThreadPool *pool = (ThreadPool*)arg;
    nWorker* active = NULL;

    int timeout;			//等待超时标记,1 超时  0 不超时
    struct timespec ts;	    //用于设置定时等待 ,传参
    JOB_CALLBACK func;		//任务处理函数

    pthread_mutex_lock(&pool->mtx);
    //线程退出清洁函数 压栈,如果中途退出,线程数-1,释放锁
    pthread_cleanup_push(WorkerCleanup, pool);

    while (1) {
        //设置线程屏蔽信号集
        pthread_sigmask(SIG_SETMASK, &fillset, NULL);
        //线程设置允许线程取消(pthread_cancel) 且设置pthread_cancel 信号发出后允许线程执行到下一个取消点才结束线程
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        timeout = 0;
        pool->idle ++;

        if (pool->flags & POOL_WAIT) {
            NotifyWaiters(pool);
        }
        //等待任务有任务到来 或者 线程池销毁通知
        while (pool->head == NULL && !(pool->flags & POOL_DESTROY)) {
            if (pool->nthreads <= pool->minimum) {
                //线程数少于下限,  (线程数量少的就可以无限等待)
                pthread_cond_wait(&pool->workcv, &pool->mtx);
            } else { //线程数高于下限,(线程数量高于下限,限时等待)
                //限时等待写法,获得绝对时间,+linger(允许等待时长)作为等待定时
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += pool->linger;

                if (pool->linger == 0 || pthread_cond_timedwait(&pool->workcv, &pool->mtx, &ts) == ETIMEDOUT) {
                    timeout = 1;
                    break;
                }
            }
        }
        //等待到条件,处于工作状态,首先空闲线程数-1
        pool->idle --;
        //收到销毁通知,退出线程
        if (pool->flags & POOL_DESTROY) {
            break;
        }
        nJob *job = pool->head;
        if (job != NULL) { //有任务 消除超时标记
            timeout = 0;
            func = job->func;
            void *job_arg = job->arg;

            pool->head = job->next;
            if (job == pool->tail) {
                pool->tail = NULL;
            }

            active = (nWorker*)malloc(sizeof(nWorker));
            active->active_tid = pthread_self();

            ACTIVES_ADD(active,pool->actives);
            pthread_mutex_unlock(&pool->mtx);//任务运行时解锁

            free(job);
            func(job_arg);

            pthread_mutex_lock(&pool->mtx); //y任务运行结束加锁
            //从工作线程队列中清除active,并释放资源
            ACTIVES_REMOVE(active,pool->actives);
            active = NULL;

            if (pool->flags & POOL_WAIT)    NotifyWaiters(pool);
        }
        //有较多(数量大于下限) 的线程,等待超时且一直没有任务到来,退出线程已减少工作线程
        if (timeout && (pool->nthreads > pool->minimum)) {
            break;
        }
    }

    pthread_cleanup_pop(1);//弹栈线程清理程序
    return NULL;
}

//线程属性拷贝
 void CloneAttributes(pthread_attr_t *new_attr, pthread_attr_t *old_attr) {
    struct sched_param param;
    void *addr;
    size_t size;
    int value;

    pthread_attr_init(new_attr);

    if (old_attr != NULL) {

        pthread_attr_getstack(old_attr, &addr, &size);	//获取线程栈空间
        pthread_attr_setstack(new_attr, NULL, size);		//设置线程栈空间


        pthread_attr_getscope(old_attr, &value);			//获取线程作用域
        //指定了作用域也就是指定了线程与谁竞争
        pthread_attr_setscope(new_attr, value);			//设置线程作用域

        pthread_attr_getinheritsched(old_attr, &value);	//获取线程是否继承调度属性
        pthread_attr_setinheritsched(new_attr, value);	//设置线程是否继承调度属性

        pthread_attr_getschedpolicy(old_attr, &value);	//获取线程的调度策略
        pthread_attr_setschedpolicy(new_attr, value);		//设置线程的调度策略

        pthread_attr_getschedparam(old_attr, &param);		//获取线程的调度参数
        pthread_attr_setschedparam(new_attr, &param);		//设置线程的调度参数

        pthread_attr_getguardsize(old_attr, &size);		//获取线程保护区大小
        pthread_attr_setguardsize(new_attr, size);		//设置线程保护区大小

    }
//设置线程分离
    pthread_attr_setdetachstate(new_attr, PTHREAD_CREATE_DETACHED);

}

// 创建线程池
ThreadPool *ThreadPoolCreate(int min_threads, int max_threads, int linger, pthread_attr_t *attr) {
    sigfillset(&fillset);
    if (min_threads > max_threads || max_threads < 1) {
        errno = EINVAL;   //表示参数无效  EINVAL 22
        return NULL;
    }
    ThreadPool *pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (pool == NULL) {
        errno = ENOMEM;  //表示无内存空间
        return NULL;
    }

    //初始化互斥量  和 条件变量
    pthread_mutex_init(&pool->mtx, NULL);
    pthread_cond_init(&pool->busycv, NULL);
    pthread_cond_init(&pool->workcv, NULL);
    pthread_cond_init(&pool->waitcv, NULL);

    pool->actives = NULL;
    pool->head = NULL;
    pool->tail = NULL;
    pool->flags = 0;
    pool->linger = linger;
    pool->minimum = min_threads;
    pool->maximum = max_threads;
    pool->nthreads = 0;
    pool->idle = 0;

    CloneAttributes(&pool->attr, attr);  //拷贝线程属性
    return pool;
}
//压栈任务入线程池
int ThreadPoolQueue(ThreadPool *pool, JOB_CALLBACK func, void *arg) {

    nJob *job = (nJob*)malloc(sizeof(nJob));
    if (job == NULL) {
        errno = ENOMEM;
        return -1;
    }
    job->next = NULL;
    job->func = func;
    job->arg = arg;

    pthread_mutex_lock(&pool->mtx);
//任务队列(尾)插入新任务
    if (pool->head == NULL) {
        pool->head = job;
    } else {
        pool->tail->next = job;
    }
    pool->tail = job;
//有空闲等待的 唤醒工作
    if (pool->idle > 0) {
        pthread_cond_signal(&pool->workcv);
    } else if (pool->nthreads < pool->maximum && WorkerCreate(pool) == 0) {
        pool->nthreads ++;
    //没有空闲线程,任务队列尚未清空,且线程数未达上限,新建线程去执行,增加执行力
        printf("create threads,nums = %d\n",pool->nthreads);
    }

    pthread_mutex_unlock(&pool->mtx);
    return 0;
}

//线程池等待
void ThreadPoolWait(ThreadPool *pool) {
    pthread_mutex_lock(&pool->mtx);
    //当线程退出时自动执行 解锁  !
    pthread_cleanup_push(pthread_mutex_unlock, &pool->mtx);

    //有任务,有工作线程,不断设置标记等待, 等待条件
    while (pool->head != NULL || pool->actives != NULL) {
        pool->flags |= POOL_WAIT;
        pthread_cond_wait(&pool->waitcv, &pool->mtx);
    }
    pthread_cleanup_pop(1);
}

//线程池销毁
void ThreadPoolDestroy(ThreadPool *pool) {
    nWorker *activep;
    nJob *job;

    pthread_mutex_lock(&pool->mtx);
    //设置线程清理函数,退出自动解锁
    pthread_cleanup_push(pthread_mutex_unlock, &pool->mtx);

    //设置销毁标记,群发通知,唤醒所有正在等待的线程,
    //若之前调用ThreadPoolwait,此时应该只剩 min 个线程,且任务队列为空,活跃线程队列为空
    pool->flags |= POOL_DESTROY;
    pthread_cond_broadcast(&pool->workcv);

    //向线程池所有正在工作的线程 发出线程取消通知
    for (activep = pool->actives; activep != NULL; activep = activep->next) {
        pthread_cancel(activep->active_tid);
    }
    //线程数不为0,等待,线程池处于忙状态
    while (pool->nthreads != 0) {
        pthread_cond_wait(&pool->busycv, &pool->mtx);
    }
    pthread_cleanup_pop(1);//线程清理:  解锁
    //工作队列,从头开始 销毁
    for (job = pool->head; job != NULL; job = pool->head) {
        pool->head = job->next;
        free(job);
    }
    pthread_attr_destroy(&pool->attr);
    free(pool);
}
