//
// Created by fengxu on 24-3-15.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <exception>
#include <pthread.h>
#include <semaphore.h>

#include "locker.h"
#include "http_conn.h"
#include "threadpool.h"

using namespace webserver;

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);
extern char *web_root;

void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;  // recovery the interrupted blocked system call
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char *info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[]) {
    if (argc <= 4) {
        printf("usage: %s ip_address port_number webroot thread_pool_size\n", basename(argv[0]));
        return -1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    web_root = argv[3];
    int pool_size = atoi(argv[4]);

    addsig(SIGPIPE, SIG_IGN);   // ignore SIG_PIPE, avoid process termination when connection is closed

    threadpool<http_conn> *pool = nullptr;  // create threadpool
    try {
        pool = new threadpool<http_conn>(pool_size);
    }
    catch (...) {
        return -1;
    }

    http_conn *users = new http_conn[MAX_FD];   // assign an http_conn obj for each possible client connection
    assert(users);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {0, 1};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));  // bind address and socket
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); // wait for event
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < number; ++ i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {       // new connection
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                // ET mode, should read loop
                while (true) {
                    int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                    if (connfd < 0) {
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD) {
                        show_error(connfd, "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);
                }
                continue;
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {  // HTTP request
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) { // HTTP response
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}