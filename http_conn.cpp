#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include "http_conn.h"
#include <iostream>
using namespace std;
//请求类型更改add_content_type()
//查询需要修改
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int http_conn::my_user_count = 0;
// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::my_epollfd = -1;
map<string, string> users;

void http_conn::initmysql_result(connect_sqlpool *connPool){
    //先从连接池中取一个连接
    connpool = connPool;
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,password FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 向epoll中添加需要监听的文件描述符
void addfd( int epollfd, int fd, bool one_shot ) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(one_shot) {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}

// 从epoll中移除监听的文件描述符
void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in& addr, string user, string passwd, string sqlname){
    my_sockfd = sockfd;
    my_address = addr;
    
    // 端口复用
    int reuse = 1;
    setsockopt( my_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( my_epollfd, sockfd, true );
    my_user_count++;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

void http_conn::init(){
    mysql = NULL;
    my_read_index = 0;
    my_write_index = 0;
    my_check_state = CHECK_STATE_REQUESTLINE;
    my_linger = false;
    my_method = GET;
    my_url = "";
    my_version = "";
    my_content_length = 0;
    my_host = "";
    my_start_line = 0;
    my_check_index = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;
    cgi = 0;
    my_state = 0;
    memset(my_read_buf, '\0', READ_BUFFER_SIZE);
    memset(my_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(my_real_file, '\0', FILENAME_LEN);
}

void http_conn::close_conn(){
    if(my_sockfd != -1){
        removefd(my_epollfd, my_sockfd);
        my_sockfd = -1;
        my_user_count--;
    }
}

bool http_conn::read(){
    if( my_read_index >= READ_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    while(true) {
        // 从m_read_buf + m_read_idx索引出开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
        bytes_read = recv(my_sockfd, my_read_buf + my_read_index, 
        READ_BUFFER_SIZE - my_read_index, 0 );
        if (bytes_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // 没有数据
                break;
            }
            return false;   
        } 
        else if (bytes_read == 0) {   // 对方关闭连接
            return false;
        }
        my_read_index += bytes_read;
    }
    return true;
}

http_conn::LINE_STATUS http_conn::parse_line(){
    for(; my_check_index < my_read_index; ++my_check_index){
        if(my_read_buf[my_check_index] == '\r'){
            if(my_check_index+1 == my_read_index)
                return LINE_OPEN;
            if(my_read_buf[my_check_index+1] == '\n'){
                my_read_buf[my_check_index++] = '\0';
                my_read_buf[my_check_index++] = '\0';
                return LINE_OK; 
            }
            return LINE_BAD;
        }
        else if(my_read_buf[my_check_index] == '\n'){
            if(my_check_index > 1 && my_read_buf[my_check_index-1] == '\r'){
                my_read_buf[my_check_index-1] = '\0';
                my_read_buf[my_check_index++] = '\0';
                return LINE_OK; 
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* head){
    //对输入的请求行按照空格来划分
    //结果：my_url变成请求的东西，例如index/html， my_version变成版本号如HTTP/1.1
    char* temp = head;
    my_url = strpbrk(head, " ");
    if(!my_url){
        return NO_REQUEST;
    }
    *my_url++ = '\0';
    if(strcasecmp(temp, "GET") == 0){
        my_method = GET;
    }
    else if(strcasecmp(temp, "POST") == 0){
        my_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    my_version = strpbrk(my_url, " ");
    if(!my_version){
        return NO_REQUEST;
    }
    *my_version++ = '\0';
    if(strcasecmp(my_version, "HTTP/1.1") == 0){
        my_method = GET;
    }
    else{
        return BAD_REQUEST;
    }

    if (strncasecmp(my_url, "http://", 7) == 0 ) {
        //跳过http://   
        my_url += 7;
        //跳过ip地址  
        my_url = strchr( my_url, '/' );
    }
    if (strncasecmp(my_url, "https://", 8) == 0){
        my_url += 8;
        my_url = strchr(my_url, '/');
    }
    if ( !my_url || my_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    if (strlen(my_url) == 1)
        strcat(my_url, "judge.html");
    my_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST; 
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    if( text[0] == '\0' ) {
        if (my_content_length != 0) {
            my_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp( text, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " ");
        if (strcasecmp(text, "keep-alive") == 0) {
            my_linger = true;
        }
    }
    else if (strncasecmp( text, "Content-Length:", 15) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 15;
        text += strspn(text, " ");
        my_content_length = atol(text);
    }
    else if(strncasecmp( text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " ");
        my_host = text;
    }
    else{
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(my_read_index >= my_check_index + my_content_length){
        my_read_buf[my_content_length] = '\0';
        my_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){
    const char* root = "/home/nagomi/websocket/sources";
    strcpy(my_real_file, root);
    int len = strlen(root);
    const char *p = strrchr(my_url, '/');
    //cout<<my_url<<endl;

    if(cgi == 1 && (*(p+1) == '2' || *(p+1) == '3')){
        char flag = my_url[1];
        char *my_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(my_url_real, "/");
        strcat(my_url_real, my_url + 2);
        strncpy(my_real_file + len, my_url_real, FILENAME_LEN - len - 1);
        free(my_url_real);

        char name[100], password[100];
        int index = 0, index_name = 0, index_password = 0;
        for(; my_string[index] != '='; index++);
        index++;
        while(my_string[index] != '&'){
            name[index_name] = my_string[index];
            index_name++;
            index++;
        }
        name[index_name] = '\0';
        for(; my_string[index] != '='; index++);
        index++;
        while(my_string[index] != '\0'){
            password[index_password] = my_string[index];
            index_password++;
            index++;
        }
        password[index_password] = '\0';
        if(*(p+1) == '3'){
            //connectionRAII mysqlcon(&mysql, connpool);
            //cout<<mysql<<endl;
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if(users.find(name) == users.end()){
                my_mutex.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                my_mutex.unlock();
                if (!res)
                    strcpy(my_url, "/log.html");
                else
                    strcpy(my_url, "/registerError.html");
            }
            else{
                strcpy(my_url, "/registerError.html");
            }
        }
        else if(*(p+1) == '2'){
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(my_url, "/welcome.html");
            else
                strcpy(my_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(my_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(my_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(my_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(my_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(my_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(my_real_file + len, my_url, FILENAME_LEN - len - 1);
    
    //cout<<my_real_file<<endl;
    //获取文件的信息
    if(stat(my_real_file, &my_file_stat) < 0){
        return NO_RESOURCE;
    }
    if(!(my_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(my_file_stat.st_mode)){
        return BAD_REQUEST;
    }
    int fd = open(my_real_file, O_RDONLY);
    my_file_address = (char*)mmap(0, my_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}


http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    //cout<<my_read_buf<<endl;
    while((my_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
        || ((line_status = parse_line()) == LINE_OK)){
            text = get_line();
            my_start_line = my_check_index;
            LOG_INFO("%s", text);
            switch(my_check_state){
                case CHECK_STATE_REQUESTLINE:{
                    ret = parse_request_line(text);
                    if (ret == BAD_REQUEST) {
                        //cout<<text<<endl;
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER:{
                    ret = parse_headers(text);
                    if (ret == BAD_REQUEST) {
                        //cout<<text<<endl;
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST){
                        return do_request();
                    }
                    break;
                }
                case CHECK_STATE_CONTENT:{
                    ret = parse_content(text);
                    if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    }
                    else if(ret == GET_REQUEST){
                        //cout<<text<<endl;
                        return do_request();
                    }
                    break;
                }
                default:{
                    return INTERNAL_ERROR;
                }
            }
        }
    return NO_REQUEST;
}

void http_conn::unmap() {
    if(my_file_address){
        munmap(my_file_address, my_file_stat.st_size);
        my_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write(){
    //cout<<my_write_buf<<endl;
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( my_epollfd, my_sockfd, EPOLLIN ); 
        init();
        return true;
    }
    while(1) {
        // 分散写
        temp = writev(my_sockfd, my_iv, my_iv_count);
        if(temp >= 0){
            bytes_have_send += temp;

        }
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                if (bytes_have_send >= my_iv[0].iov_len){
                    my_iv[0].iov_len = 0;
                    my_iv[1].iov_base = my_file_address + (bytes_have_send - my_write_index);
                    my_iv[1].iov_len = bytes_to_send;
                }
                else{
                    my_iv[0].iov_base = my_write_buf + bytes_have_send;
                    my_iv[0].iov_len = my_iv[0].iov_len - bytes_have_send;
                }
                modfd( my_epollfd, my_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        if ( bytes_to_send <= 0 ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            modfd( my_epollfd, my_sockfd, EPOLLIN );
            if(my_linger) {
                init();
                return true;
            } else {
                return false;
            } 
        }
    }
}

bool http_conn::add_response( const char* format, ... ) {
    if(my_write_index >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf(my_write_buf + my_write_index, WRITE_BUFFER_SIZE - 1 - my_write_index, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - my_write_index ) ) {
        va_end(arg_list);
        return false;
    }
    my_write_index += len;
    va_end( arg_list );
    LOG_INFO("request:%s", my_write_buf);
    return true;
}


bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_content_type() && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger(){
    return add_response( "Connection: %s\r\n", ( my_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line(){
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content ){
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");//请求类型更改add_content_type()
}

bool http_conn::process_write(HTTP_CODE ret){
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            if(my_file_stat.st_size != 0){
                add_headers(my_file_stat.st_size);
                my_iv[ 0 ].iov_base = my_write_buf;
                my_iv[ 0 ].iov_len = my_write_index;
                my_iv[ 1 ].iov_base = my_file_address;
                my_iv[ 1 ].iov_len = my_file_stat.st_size;
                my_iv_count = 2;
                bytes_to_send = my_write_index + my_file_stat.st_size;
                return true;
            }
            else{
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        default:
            return false;
    }

    my_iv[ 0 ].iov_base = my_write_buf;
    my_iv[ 0 ].iov_len = my_write_index;
    my_iv_count = 1;
    bytes_to_send = my_write_index;
    return true;
}

void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd( my_epollfd, my_sockfd, EPOLLIN );
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(my_epollfd, my_sockfd, EPOLLOUT);
}

sockaddr_in* http_conn::get_address(){
    return &my_address;
}



#endif