//
// Created by fengxu on 24-3-15.
//

#include "http_conn.h"
using namespace webserver;


/* status information of HTTP response */
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

/* root of website */
const char *web_root = "/var/www/";


/* modify fd attr to nonblocking */
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/* register fd in epoll kernel event table */
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // data read | ET | TCP disconnect
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/* remove fd from kernel event table */
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* modify registered event of fd */
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* close connection */
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count --;    // close a connection, count of user minus 1
    }
}

/* init the new connection */
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    // to avoid TIME_WAIT, for debugging
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count ++;
    init();
}

/* init connection */
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/* loop the client data until there is no data to read or the client closes the connection */
bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // non-blocking return, no data to read
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/* slave state machine, to get a complete line*/
http_conn::LINE_STATUS http_conn::parse_line(bool& has_data) {
    char temp;
    // m_read_buf[0, m_checked_idx) is analysed
    // m_read_buf[m_checked_idx, m_read_idx) is to be analysed
    for ( ; m_checked_idx < m_read_idx; ++ m_checked_idx) {
        has_data = true;
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) { // \r is the last char, the line is not complete
                m_checked_idx ++;
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {   // get the complete line
                m_read_buf[m_checked_idx ++] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    // there isn't \n after \r, so the line is invalid
        } else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {   // get the complete line
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    // there isn't \r before \n, so the line is invalid
        }
    }

    return LINE_OPEN;   // if there isn't \r or \n, the line is not complete
}


// HTTP request example:
// GET http://localhost:8080/dir/readme.txt HTTP/1.0
// Host: localhost:8888
// User-Agent: curl/7.81.0

/* parse HTTP request line */
/* to get request method, URL and HTTP version */
http_conn::HTTP_CODE http_conn::read_request_line(char *text) {
    m_url = strpbrk(text, " \t");   // get the space or \t before url
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url ++ = '\0';   // get url

    char *method = text;
    if (strcasecmp(method, "GET") == 0) {// this server only support GET method
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");  // remove space and \t
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version ++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.0") != 0) {// this server only support HTTP/1.0
        return BAD_REQUEST;
    }
    if (strncasecmp(m_url, "http://", 7) == 0) {// check validity of url
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // finish request line parse, convert state to analysis of header field
    return NO_REQUEST;
}

/* master state machine, to parse HTTP request */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;  // status of current line
    char *text = 0;

    while (true) {
        bool has_data = false;
        line_status = parse_line(has_data);
        if (line_status == LINE_BAD) {
            return BAD_REQUEST;        
        }    

        if (line_status == LINE_OPEN) {
            if (has_data == false) { // if there is no data and line is incomplete, the request is bad
                return BAD_REQUEST;
            } else {
                continue;
            }
        }

        text = get_line();  // get a line (m_read_buf + m_start_line)
        m_start_line = m_checked_idx;   // record start position of next line

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: { // read request line
                HTTP_CODE ret = read_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: { // according to the requirement, ingore the header
                if (text[0] == '\0') { // found an empty line, means header parsing finishes
                    return do_request();
                }
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/* when get a complete and correct HTTP request, analysis it */
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, web_root);
    int len = strlen(web_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH)) { // whether can be read by other group
        return FORBIDEEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) { // whether directory
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // use mmap to map the file to the memory address m_file_address
    close(fd);
    return FILE_REQUEST;
}

/* upmap memory */
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/* write data to write buffer */
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - 1 - m_write_idx) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/* add status line to response */
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.0", status, title);
}

/* add header field to response */
void http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

/* add content length to response header */
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

/* add connection to response header */
bool http_conn::add_linger() {
    return add_response("Connection: close\r\n");
}

/* add blank line to response header, means the end of the header */
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

/* add message body to response */
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

/* write HTTP response according the HTTP request process result */
bool http_conn::process_write(http_conn::HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDEEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            } else {
                const char *empty_file_string = "Empty file\n";
                add_headers(strlen(empty_file_string));
                if (!add_content(empty_file_string)) {
                    return false;
                }
            }
            break;
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/* entry function for processing HTTP request */
/* called by thread in threadpool */
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

/* write HTTP response */
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            if (errno == EAGAIN) {  // if write buffer has no space, wait for next EPOLLOUT event
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send) { // send HTTP response success
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            return false;
        }
    }
}
