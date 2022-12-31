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
#include "locker.h"
#include <sys/uio.h>
#include <mysql/mysql.h>
#include "sqlpool.h"
#include "log.h"
#include <map>
#include "locker.h"

class http_conn{
public:
    static const int FILENAME_LEN = 200;        // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小
    
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
public:
    http_conn(){};
    ~http_conn(){};
    void init(int sockfd, const sockaddr_in& addr, string user, string passwd, string sqlname);
    void close_conn();
    void process();
    bool read();
    bool write();
    sockaddr_in* get_address();
    void initmysql_result(connect_sqlpool *connPool);
public:
    MYSQL* mysql;
    connect_sqlpool* connpool;
    int my_state;
public:
    static int my_epollfd;
    static int my_user_count;
private:
    void init();
    //读相关函数
    char* get_line(){return my_read_buf + my_start_line;}
    HTTP_CODE process_read();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    LINE_STATUS parse_line();
    //写相关函数
    bool process_write(HTTP_CODE ret);
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
private:
    int my_sockfd;           
    sockaddr_in my_address;
    mutex my_mutex;
    
    int my_read_index;
    int my_write_index;                        // 写缓冲区中待发送的字节数
    char my_read_buf[READ_BUFFER_SIZE];
    char my_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int my_check_index;                      // 当前正在分析的字符在读缓冲区中的位置
    int my_start_line;                       // 当前正在解析的行的起始位置
    int my_content_length;                   //消息体的长度
    bool my_linger;
    int my_iv_count;
    int bytes_to_send;              // 将要发送的数据的字节数
    int bytes_have_send;            // 已经发送的字节数
    int cgi;

    CHECK_STATE my_check_state;              // 主状态机当前所处的状态
    METHOD my_method;                        // 请求方法

    char* my_url;
    char* my_version;
    char* my_host;
    char* my_file_address;  
    char my_real_file[FILENAME_LEN];
    char *my_string;
    
    struct stat my_file_stat;
    struct iovec my_iv[2]; 
    
    map<string, string> my_users;
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};