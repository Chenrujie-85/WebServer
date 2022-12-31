#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include "locker.h"
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <iostream>
#include <string>

class connect_sqlpool{
public:
    connect_sqlpool();
    ~connect_sqlpool();
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn);
    MYSQL* getconn();
    bool releaseconn(MYSQL* conn);
    void destory_pool();
    int GetFreeConn();
public:
    static connect_sqlpool* getstance();

public:
	string my_url;			 //主机地址
	string my_port;		 //数据库端口号
	string my_user;		 //登陆数据库用户名
	string my_passWord;	 //登陆数据库密码
	string my_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关

private:
    mutex my_mutex;
    sem signal;
    int freeconn;
    int curconn;
    int maxconn;
    list<MYSQL *> connlist;
};

class connectionRAII{

public:
	connectionRAII(MYSQL **con, connect_sqlpool *connPool);
	~connectionRAII();
private:
	MYSQL* conRAII;
	connect_sqlpool* poolRAII;
};

#endif