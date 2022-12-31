#include "timer.h"

sort_timer_lst::sort_timer_lst(){
    head = NULL;
    tail = NULL;
}

sort_timer_lst::~sort_timer_lst(){
    util_timer* temp = head;
    while(temp){
        head = temp->next;
        delete temp;
        temp = head;
    }
    while(!time_list.empty()){
        util_timer* temp = time_list.top();
        time_list.pop();
        delete temp;
    }
}

void sort_timer_lst::add_timer(util_timer* timer){
    if (!timer){
        return;
    }
    if (!head){
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire){
        timer->next = head;
        head->pre = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);

}

void sort_timer_lst::adjust_timer(util_timer* timer){
    if(!timer)
        return;
    util_timer* temp = timer->next;
    if(!temp || timer->expire < temp->expire)
        return;
    if (timer == head){
        head = head->next;
        head->pre = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else{
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer* timer){
    if(!timer)
        return;
    if(timer == head && timer == tail){
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if(timer == head){
        head = timer->next;
        head->pre = NULL;
    }
    else if(timer == tail){
        tail = timer->pre;
        tail->next = NULL;
    }
    else{
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
    }
    delete timer;
    return;
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* head){
    util_timer *pre = head;
    util_timer *temp = pre->next;
    while (temp){
        if (timer->expire < temp->expire){
            pre->next = timer;
            timer->next = temp;
            temp->pre = timer;
            timer->pre = pre;
            break;
        }
        pre = temp;
        temp = temp->next;
    }
    if (!temp){
        pre->next = timer;
        timer->pre = pre;
        timer->next = NULL;
        tail = timer;
    }
}

void sort_timer_lst::tick(){
    if(!head)
        return;
    util_timer* temp = head;
    time_t cur = time(NULL);
    while(temp){
        if(temp->expire < cur){
            break;
        }
        temp->cb_func(temp->user_data);
        head = temp->next;
        if(head){
            head->pre = NULL;
        }
        delete temp;
        temp = head;
    }
}

void Utils::init(int TIMESLOT){
    my_TIMESLOT = TIMESLOT;
}

int Utils::setnoblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events |= EPOLLIN | EPOLLRDHUP;
    if(one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}

void Utils::sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(my_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler(){
    my_list.tick();
    alarm(my_TIMESLOT);
}

int *Utils::my_pipefd = 0;
int Utils::my_epollfd = -1;

class Utils;
void cb_func(client_data *user_data){
    epoll_ctl(Utils::my_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::my_user_count--;
}