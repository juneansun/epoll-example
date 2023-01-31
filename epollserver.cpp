#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#define MAX_EVENTS 32

void initializeSocket(const char *name);
void initializeEpoll(void);
void processNewConnection(const epoll_event *event);
void processExistConnection(const epoll_event *event);
void setNonblocking(int fd);
void terminate(void);
void handleError(const char *msg);
void signalHandler(int signal);

volatile sig_atomic_t canLoop = 1;

int epfd = -1;
epoll_event endpoint = { 0 };
epoll_event events[MAX_EVENTS];

int udsfd = -1;
sockaddr_un addr = { 0 };

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: epollserver <socket name>\n");
        exit(EXIT_FAILURE);
    }

    initializeSocket(argv[1]);
    initializeEpoll();
    printf("epoll descriptor: 0x%016X\n", epfd);
    printf("socket descriptor: 0x%016X\n", udsfd);

    if (signal(SIGINT, signalHandler) == SIG_ERR) handleError("signal");
    printf("Polling...(Ctrl+C to exit)\n");
    while(canLoop) {
        int epn = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (epn < 0) handleError("epoll_wait");

        for(int i = 0; i < epn; i++) {
            epoll_event *it = events + i;
            if (it->data.fd == udsfd) {
                processNewConnection(it);
            } else {
                processExistConnection(it);
            }
        }
    }

    terminate();
    return EXIT_SUCCESS;
}

// 新規接続を処理
void processNewConnection(const epoll_event *event) {
    socklen_t size = sizeof(sockaddr_un);
    sockaddr_un client = { 0 };
    int clfd = accept(udsfd, (sockaddr*)&client, &size);
    if (clfd < 0) handleError("accept");

    epoll_event newcl = { 0 };
    newcl.events = EPOLLIN;
    newcl.data.fd = clfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, clfd, &newcl);
    printf("Connection from 0x%08X established.\n", clfd);
}

// 既存接続を処理(メッセージ受け)
void processExistConnection(const epoll_event *event) {
    int length = 0;
    epoll_event cl = { 0 };
    int clfd = event->data.fd;

    int state = read(clfd, &length, sizeof(int));
    if (state < 0 || state != sizeof(int)) {
        perror("read");
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        printf("Connection from 0x%08X closed.\n", clfd);
        return;
    }
    if (state == 0) {
        // ソケット切れ
        epoll_ctl(epfd, EPOLL_CTL_DEL, clfd, &cl);
        close(clfd);
        printf("Connection from 0x%08X closed.\n", clfd);
        return;
    }

    // 送る文字列は最後の\n\0込みで頼む
    char *buffer = (char*)malloc(length);
    read(clfd, buffer, length);
    printf("0x%08X: %s", clfd, buffer);
    free(buffer);
}

// UNIXドメインソケットの初期化
void initializeSocket(const char *name) {
    udsfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (udsfd < 0) handleError("socket");

    memset(&addr, 0, sizeof(sockaddr_un));
    addr.sun_family = AF_LOCAL;
    // 抽象名前空間につきsun_path[0]は\0
    // さらにbindに渡すlengthはsun_pathの実質的な終端(\0含まない)までとする必要がある
    strncpy(addr.sun_path + 1, name, 64);
    int addrlen = sizeof(sa_family_t) + strlen(name) + 1;
    if (bind(udsfd, (sockaddr*)&addr, addrlen) < 0) handleError("bind");
    if (listen(udsfd, MAX_EVENTS) < 0) handleError("listen");
}

// epollの初期化
void initializeEpoll(void) {
    epfd = epoll_create(MAX_EVENTS);
    if (epfd < 0) handleError("epoll_create");

    memset(&endpoint, 0, sizeof(epoll_event));
    endpoint.events = EPOLLIN;
    endpoint.data.fd = udsfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, udsfd, &endpoint);
}

// 終了処理
void terminate(void) {
    if (epfd >= 0) close(epfd);
    if (udsfd >= 0) close(udsfd);
}

// エラー処理
void handleError(const char *msg) {
    perror(msg);
    terminate();
    exit(EXIT_FAILURE);
}

// 割り込み処理
void signalHandler(int signal) {
    canLoop = 0;
}
