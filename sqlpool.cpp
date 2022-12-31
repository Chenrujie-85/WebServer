#include "sqlpool.h"
#include "log.h"
using namespace std;


connect_sqlpool::connect_sqlpool(){
    freeconn = 0;
    curconn = 0;
}

connect_sqlpool::~connect_sqlpool(){
    destory_pool();
}

connect_sqlpool* connect_sqlpool::getstance(){
    static connect_sqlpool connPool;
	return &connPool;
}

void connect_sqlpool::init(string url, string user, string password, string DataBaseName, int port, int MaxConn){
    my_url = url;
    my_user = user;
    my_passWord = password;
    my_DatabaseName = DataBaseName;
    my_port = port;

    for(int i = 0; i < MaxConn; ++i){
        MYSQL* conn = mysql_init(NULL);

        if(conn == NULL){
            LOG_ERROR("MySql Error in sqlpool.cpp");
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), DataBaseName.c_str(), port, NULL, 0);
        if(conn == NULL){
            LOG_ERROR("MySql connect Error in sqlpool.cpp");
            exit(1);
        }
        connlist.push_back(conn);
        freeconn++;
    }
    signal = sem(freeconn);
    maxconn = freeconn;
}

MYSQL* connect_sqlpool::getconn(){
    MYSQL* conn = NULL;
    if(connlist.size() == 0)
        return NULL;
    signal.wait();
    my_mutex.lock();
    conn = connlist.front();
    connlist.pop_front();
    freeconn--;
    curconn++;
    my_mutex.unlock();
    return conn;
}

bool connect_sqlpool::releaseconn(MYSQL* conn){
    if(conn == NULL)
        return false;
    my_mutex.lock();
    connlist.push_back(conn);
    freeconn++;
    curconn--;
    my_mutex.unlock();
    signal.post();
    return true;
}

void connect_sqlpool::destory_pool(){
    my_mutex.lock();
    if(connlist.size() > 0){
        for(auto iter = connlist.begin(); iter != connlist.end(); ++iter){
            MYSQL* conn = *iter;
            mysql_close(conn);
        }
        freeconn = 0;
        curconn = 0;
        connlist.clear();
    }
    my_mutex.unlock();
}

int connect_sqlpool::GetFreeConn(){
	return this->freeconn;
}

connectionRAII::connectionRAII(MYSQL **con, connect_sqlpool *connPool){
    *con = connPool->getconn();
    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->releaseconn(conRAII);
}
