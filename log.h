#ifndef LOG_H
#define LOG_H

#include "block_queue.h"
#include <stdio.h>
#include <iostream>
#include <stdarg.h>
#include <pthread.h>
using namespace std;

class Log{
public:
    static Log* get_instance(){
        static Log instance;
        return &instance;
    };
    static void* flush_log_thread(void* args){
        Log::get_instance()->async_write_log();
    };
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    void write_log(int level, const char *format, ...);
    void flush(void);
private:
    Log();
    ~Log();
    void* async_write_log(){
        string single_log;
        while(my_block_queue->pop(single_log)){
            my_mutex.lock();   
            int ret = fputs(single_log.c_str(), my_fp);
            my_mutex.unlock();
        }
    };
private:
    char dir_name[128];
    char log_name[128];
    char* log_buf;
    int my_buf_size;//缓冲区大小
    int my_max_line;//最大行数
    long long my_line;
    int my_day;
    FILE* my_fp;
    block_queue<string>* my_block_queue;
    int my_close_log;
    mutex my_mutex;
};

#define LOG_DEBUG(format, ...)  {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...)  {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...)  {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...)  {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif