#include "locker.h"
#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
using namespace std;

template<class T>
class block_queue{
public:
    block_queue(int max_size = 1000){
        if(max_size <= 0)
            exit(0);
        my_max_size = max_size;
        my_array = new T[max_size];
        my_size = 0;
        my_front = -1;
        my_back = -1;
    }
    
    void clear(){
        my_mutex.lock();
        my_array = 0;
        my_front = -1;
        my_back = -1;
        my_mutex.unlock();
    }

    ~block_queue(){
        my_mutex.lock();
        if(my_array != NULL)
            delete []my_array;
        my_mutex.unlock();
    }
    
    bool full(){
        my_mutex.lock();
        if (my_size >= my_max_size){
            my_mutex.unlock();
            return true;
        }
        my_mutex.unlock();
        return false;
    }

    bool empty(){
        my_mutex.lock();
        if (my_size <= 0){
            my_mutex.unlock();
            return true;
        }
        my_mutex.unlock();
        return false;
    }

    bool front(T& value){
        my_mutex.lock();
        if(my_size == 0){
            my_mutex.unlock();
            return false;
        }
        value = my_array[front];
        my_mutex.unlock();
        return false;
    }

    bool back(T& value){
        my_mutex.lock();
        if(my_size == 0){
            my_mutex.unlock();
            return false;
        }
        value = my_array[my_back];
        my_mutex.unlock();
        return false;
    }
    
    int size(){
        int temp;
        my_mutex.lock();
        temp = my_size;
        my_mutex.unlock();
        return temp;
    }

    int max_size(){
        int temp;
        my_mutex.lock();
        temp = my_max_size;
        my_mutex.unlock();
        return temp;
    }

    bool push(const T& item){
        my_mutex.lock();
        if(my_size >= my_max_size){
            my_cond.broadcast();
            my_mutex.unlock();
            return false;
        }
        my_back = (my_back+1)&my_max_size;
        my_array[my_back] = item;
        my_size++;
        my_cond.broadcast();
        my_mutex.unlock();
        return true;
    }

    bool pop(T& item){
        my_mutex.lock();
        while(my_size <= 0){
            if(!my_cond.wait(my_mutex.get())){
                my_mutex.unlock();
                return false;
            }
        }
        my_front = (my_front+1) % my_max_size;
        item = my_array[my_front];
        my_size--;
        my_mutex.unlock();
        return true;
    }
private:
    mutex my_mutex;
    cond my_cond;

    T *my_array;
    int my_size;
    int my_max_size;
    int my_front;
    int my_back;
};