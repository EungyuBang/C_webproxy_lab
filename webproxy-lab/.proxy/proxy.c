/* proxy.c - 순차적 HTTP 프록시 서버 예제 */
#include "csapp.h"

#define MAXLINE 8192
#define MAXBUF 8192

void handle_client(int clientfd);
void parse_uri(char *uri, char *host, char *port, char *path);

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("Accepted connection\n");

        handle_client(connfd);
        Close(connfd);  // 순차적이므로 한 요청 처리 후 종료
    }
}

/* 클라이언트 요청을 처리하고 서버에 전달 후 응답 반환 */
void handle_client(int clientfd) {
    rio_t rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[10], path[MAXLINE];

    Rio_readinitb(&rio, clientfd);

    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        fprintf(stderr, "Proxy only implements GET\n");
        return;
    }

    parse_uri(uri, host, port, path);

    /* 서버에 연결 */
    int serverfd = Open_clientfd(host, port);

    /* 요청 라인 보내기 */
    snprintf(buf, MAXLINE, "GET %s %s\r\n", path, version);
    Rio_writen(serverfd, buf, strlen(buf));

    /* 헤더 전달 */
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) {
            Rio_writen(serverfd, buf, strlen(buf));
            break;
        }
        Rio_writen(serverfd, buf, strlen(buf));
    }

    /* 서버 응답을 클라이언트로 전달 */
    int n;
    while ((n = Rio_readn(serverfd, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n);
    }

    Close(serverfd);
}

/* URI를 host, port, path로 분리 */
void parse_uri(char *uri, char *host, char *port, char *path) {
    char *host_begin = strstr(uri, "://");
    host_begin = host_begin ? host_begin + 3 : uri;

    char *port_begin = strchr(host_begin, ':');
    char *path_begin = strchr(host_begin, '/');

    if (!path_begin) path_begin = host_begin + strlen(host_begin);

    if (port_begin && port_begin < path_begin) {
        strncpy(host, host_begin, port_begin - host_begin);
        host[port_begin - host_begin] = '\0';
        strncpy(port, port_begin + 1, path_begin - port_begin - 1);
        port[path_begin - port_begin - 1] = '\0';
    } else {
        strncpy(host, host_begin, path_begin - host_begin);
        host[path_begin - host_begin] = '\0';
        strcpy(port, "80");
    }

    strcpy(path, path_begin);
}
