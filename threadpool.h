#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "sqlpool.h"
#include <cstdio>
#include <exception>
#include <pthread.h>

template<typename T>
class threadpool {
public:
    threadpool(connect_sqlpool *pool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);
    void get(connect_sqlpool* pool);
private:
    static void* work(void* arg);
    void run();

private:
    int my_thread_number;
    int my_requests;
    pthread_t* my_threads;
    std::list<T*> my_workqueue;
    sem my_sem;
    mutex my_mutex;
    connect_sqlpool *my_connPool;
};

template< typename T >
threadpool<T>::threadpool(connect_sqlpool *pool, int thread_number, int max_requests):my_thread_number(thread_number), my_requests(max_requests), 
        my_threads(NULL), my_connPool(pool){
            if(thread_number <= 0 || max_requests <= 0){
                throw std::exception();
            }
            my_threads = new pthread_t[my_thread_number];
            if(!my_threads){
                throw std::exception();
            }
            for(int i = 0; i < thread_number; ++i){
                if(pthread_create(my_threads+i, NULL, work, this) != 0){
                    delete [] my_threads;
                    throw std::exception();
                }
                if(pthread_detach(my_threads[i]) != 0) {
                    delete [] my_threads;
                    throw std::exception();
                }
            }
        }

template< typename T >
threadpool<T>::~threadpool(){
    delete [] my_threads;
}

template< typename T >
bool threadpool<T>::append(T* request, int state){
    my_mutex.lock();
    if(my_workqueue.size() > my_requests){
        printf("the woredqueue is full");
        my_mutex.unlock();
        return false;
    }
    request->my_state = state;
    my_workqueue.push_back(request);
    my_mutex.unlock();
    my_sem.post();
    return true;
}

template< typename T >
void* threadpool<T>::work(void* arg){
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template< typename T>
void threadpool<T>::run(){
    while(true){
        my_sem.wait();
        my_mutex.lock();
        if(my_workqueue.empty()){
            printf("there is no task in queue");
            my_mutex.unlock();
            continue;
        }
        T* request = my_workqueue.front();
        my_workqueue.pop_front();
        my_mutex.unlock();
        if(!request){
            printf("there is no task");
            continue;
        }
        if(request->my_state == 0){
            if(request->read()){
                connectionRAII mysqlcon(&request->mysql, my_connPool);
                request->process();
            }
        }
    }
}

#endif