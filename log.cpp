#include "log.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
using namespace std;

Log::Log(){
    my_line = 0;
}

Log::~Log(){
    if(my_fp != NULL)
        fclose(my_fp);
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    my_block_queue = new block_queue<std::string>;
    pthread_t pid;
    pthread_create(&pid, NULL, flush_log_thread, NULL);
    my_close_log = close_log;
    my_buf_size = log_buf_size;
    my_max_line = split_lines;

    log_buf = new char[my_buf_size];
    memset(log_buf, '\0', my_buf_size);

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if(p == NULL)
       snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    else{
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);    
    }
    my_day = my_tm.tm_mday;
    my_fp = fopen(log_full_name, "aw");
    
    if(my_fp == NULL){
        cout<<"open fail"<<endl;
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch(level){
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    my_mutex.lock();
    my_line++;
    if(my_day != my_tm.tm_mday || my_line % my_max_line == 0){
        char new_log[256] = {0};
        fflush(my_fp);
        fclose(my_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if(my_day != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            my_day = my_tm.tm_mday;
            my_line = 0;
        }
        else{
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, my_line / my_max_line);
        }
        my_fp = fopen(new_log, "a");
    }
    my_mutex.unlock();

    va_list valst;
    va_start(valst, format);
    string log_str;
    my_mutex.lock();

    int n = snprintf(log_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    int m = vsnprintf(log_buf + n, my_buf_size - 1, format, valst);

    log_buf[n + m] = '\n';
    log_buf[n + m + 1] = '\0';
    log_str = log_buf;

    my_mutex.unlock();
    my_block_queue->push(log_str);
    va_end(valst);
}

void Log::flush(void){
    my_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(my_fp);
    my_mutex.unlock();
}

