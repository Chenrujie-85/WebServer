#ifndef LOCKER_H
#define LOCKER_H
//条件变量 
#include <exception>
#include <pthread.h>
#include <semaphore.h>
#include <iostream>
using namespace std;

class mutex{
public:
    mutex(){
        if(pthread_mutex_init(&my_locker, NULL) != 0){
            throw std::exception();
        }
    }
    
    ~mutex(){
        pthread_mutex_destroy(&my_locker);
    }

    bool lock(){
        return pthread_mutex_lock(&my_locker) == 0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&my_locker) == 0;
    }

    bool trylock(){
        return pthread_mutex_trylock(&my_locker) == 0;
    }

    pthread_mutex_t* get(){
        return &my_locker;
    }
private:
    pthread_mutex_t my_locker;
};

class sem{
public:
    sem(){
        if(sem_init(&my_sem, 0, 0) != 0)
            throw std::exception();
    }

    sem(int num){
        if(sem_init(&my_sem, 0, num) != 0)
            throw std::exception();
    }

    ~sem(){
        sem_destroy(&my_sem);
    }

    bool wait(){
        return sem_wait(&my_sem) == 0;
    }

    bool post(){
        return sem_post(&my_sem) == 0;
    }
private:
    sem_t my_sem;
};

class cond{
public:
    cond(){
        if (pthread_cond_init(&my_cond, NULL) != 0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&my_cond);
    }
    bool wait(pthread_mutex_t* my_mutex){
        int ret = 0;
        ret = pthread_cond_wait(&my_cond, my_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *my_mutex, struct timespec t){
        int ret = 0;
        ret = pthread_cond_timedwait(&my_cond, my_mutex, &t);
        return ret == 0;
    }
    bool signal(){
        return pthread_cond_signal(&my_cond) == 0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&my_cond) == 0;
    }

private:
    pthread_cond_t my_cond;
};

#endif