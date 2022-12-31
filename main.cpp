#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
//#include "http_conn.h"
#include "timer.h"
#include <time.h>
#include <iostream>
#include "log.h"
using namespace std;

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
#define my_sql_num 8
const int TIMESLOT = 5;  
string user = "root";
string passwd = "123456";
string databasename = "webserver";


extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );

void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}


int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    //addsig( SIGPIPE, SIG_IGN );
    //创建日志
    Log::get_instance()->init("./ServerLog", 0, 2000, 800000);
    //创建数据库池
    connect_sqlpool *sqlpool = connect_sqlpool::getstance();
    sqlpool->init("localhost", user, passwd, databasename, 10000, my_sql_num);
    //创建线程池、http用户数组
    threadpool<http_conn>* pool = new threadpool<http_conn>(sqlpool);
    http_conn* users = new http_conn[MAX_FD];

    //创建socket
    int ret;
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_adr;
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = INADDR_ANY;
    serv_adr.sin_port = htons(atoi(argv[1]));
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if(bind(listenfd, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1){
        printf("bind fail");
        throw std::exception();
    }
    if(listen(listenfd, 5) == -1){
        printf("listen fail");
        throw std::exception();
    }
    
    //创建epoll对象
    epoll_event events[MAX_EVENT_NUMBER];
    int epfd = epoll_create(5);
    addfd(epfd, listenfd, false);
    http_conn::my_epollfd = epfd;

    //创建时间序列
    int pipefd[2];
    bool timeout = false;
    client_data* user_data = new client_data[MAX_FD];
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    Utils* util = new Utils();
    util->init(TIMESLOT);
    Utils::my_epollfd = epfd;
    Utils::my_pipefd = pipefd;
    util->setnoblocking(pipefd[1]);
    addfd(util->my_epollfd, pipefd[0], false);
    util->addsig(SIGPIPE, SIG_IGN);
    util->addsig(SIGALRM, util->sig_handler, false);
    util->addsig(SIGTERM, util->sig_handler, false);
    alarm(TIMESLOT);
    
    users->initmysql_result(sqlpool);

    //开始监听
    while(1){
        int event_cnt = epoll_wait(epfd, events, MAX_EVENT_NUMBER, -1);
        if(event_cnt < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0; i < event_cnt; ++i){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                struct sockaddr_in clnt_adr;
                socklen_t adr_sz = sizeof(clnt_adr);
                int clnt_sock = accept(listenfd, (struct sockaddr*)&clnt_adr, &adr_sz);
                if(clnt_sock < 0){
                    printf( "errno is: %d\n", errno );
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if( http_conn::my_user_count >= MAX_FD ) {
                    LOG_ERROR("%s", "Internal server busy");
                    close(clnt_sock);
                    continue;
                }
                //创建一个时间
                util_timer* timer = new util_timer();
                user_data[clnt_sock].address = clnt_adr;
                user_data[clnt_sock].sockfd = clnt_sock;
                timer->user_data = &user_data[clnt_sock];
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                timer->cb_func = cb_func;
                user_data[clnt_sock].timer = timer;
                util->my_list.add_timer(timer);
                //cout<<util->my_list.get_head()->expire<<endl;
                users[clnt_sock].init(clnt_sock, clnt_adr, user, passwd, databasename);
            }
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN)){
                int sig;
                char signal[1024];
                ret = recv(pipefd[0], signal, sizeof(signal), 0);
                if(ret == -1)
                    continue;
                else if(ret == 0)
                    continue;
                else{
                    for(int i = 0; i < ret; ++i){
                        switch(signal[i]){
                            case SIGALRM:{
                                timeout = true;
                                break;
                            }
                            case SIGTERM:{
                                //处理服务中断
                                break;
                            }
                        }
                    }
                }
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                cb_func(&user_data[sockfd]);
                util_timer* timer = user_data[sockfd].timer;
                if(timer){
                    util->my_list.del_timer(timer);
                    std::cout<<"close connect"<<endl;
                }
                //users[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLIN){
                util_timer* timer = user_data[sockfd].timer;
                if(timer){
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    //cout<<timer->get_time()<<endl;
                    util->my_list.adjust_timer(timer);
                }
                LOG_INFO("get from the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                pool->append(users+sockfd, 0);
            }
            else if(events[i].events & EPOLLOUT){
                util_timer* timer = user_data[sockfd].timer;
                if(users[sockfd].write()){
                    LOG_INFO("write to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    if(timer){
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        //cout<<timer->get_time()<<endl;
                        util->my_list.adjust_timer(timer);
                    }
                }
                else{
                    std::cout<<"close write connect"<<endl;
                    cb_func(&user_data[sockfd]);
                    if(timer)
                        util->my_list.del_timer(timer);
                }
            }
        }
        if(timeout){
            util->timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
    close(epfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}