#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "http_conn.h"
#include <vector>
#include <queue>

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
    bool operator<(const client_data& a){
        return timer->expire < a.timer->expire;
    }
};

class util_timer
{
public:
    util_timer() : pre(NULL), next(NULL) {}
    time_t get_time(){return expire;};
public:
    time_t expire;
    
    void (* cb_func)(client_data *);
    client_data *user_data;
    util_timer *pre;
    util_timer *next;
};

class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();
    util_timer* get_head(){return head;};
private:
    void add_timer(util_timer *timer, util_timer *lst_head);
    priority_queue<util_timer*, vector<util_timer*>, greater<util_timer*>> time_list;
    util_timer* head;
    util_timer* tail;
};

class Utils{
public:
    Utils() {}
    ~Utils() {}
    void init(int TIMESLOT);
    int setnoblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot);
    static void sig_handler(int sig);
    void addsig(int sig, void(handler)(int), bool restart = true);
    void timer_handler();
public: 
    static int my_epollfd;
    static int* my_pipefd;
    sort_timer_lst my_list;
private:
    int my_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif