#include <stdio.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#include "csapp.h"

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *request_buf, char *hostname);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 먼저 proxy 서버 열고
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    // 클라이언트가 프록시 서버에 연결 요청 
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // doit 함수 호출
    doit(connfd);
    // 열린 connfd 닫아주고
    Close(connfd);
  }
}

void doit(int fd) 
{
  int server_fd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char request_buf[MAXLINE];
  rio_t rio_browser;
  rio_t rio_server;
  // rio 구조체 초기화 해주고, fd에 연결
  Rio_readinitb(&rio_browser, fd);
  // rio 구조체 읽었는데 없어? -> 데이터 안 들어온거니까 return
  if (!Rio_readlineb(&rio_browser, buf, MAXLINE))
    return;

  printf("Request from browser:\n%s", buf);
  // 요청라인 공백 기준으로 잘라 -> method, uri, version 됨
  sscanf(buf, "%s %s %s", method, uri, version);

  // 우리는 GET 요청만 처리할거니깐 -> GET 으로 안 들어오면 에러문 띄우고 종료
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
    return;
  }
  // uri 가 parse 가 안 되면 종료
  if (parse_uri(uri, hostname, port, path) < 0) {
    clienterror(fd, uri, "400", "Bad Request", "Proxy couldn't parse the URI");
    return;
  }

  sprintf(request_buf, "GET %s HTTP/1.0\r\n", path);
  read_requesthdrs(&rio_browser, request_buf, hostname);

  // 이번엔 프록시가 클라이언트가 되는거임 -> tiny 서버에 연결 요청 -> 받은 fd 저장
  server_fd = Open_clientfd(hostname, port);
  // 받은 fd 가 0 이면? -> 서버랑 연결 안 됐다는 뜻, 에러문 띄우고 종료시켜
  if (server_fd < 0) {
    clienterror(fd, hostname, "502", "Bad Gateway", "Proxy couldn't connect to the server");
    return;
  }

  printf("--- Forwarding request to %s:%s ---\n%s", hostname, port, request_buf);
  // tiny 서버에 연결됐으니, 요청 보내
  Rio_writen(server_fd, request_buf, strlen(request_buf));

  // tiny 서버에서 돌아온 응답값 받을 rio 버퍼 초기화하고, 연결
  Rio_readinitb(&rio_server, server_fd);
  ssize_t n;
  // tiny 서버에서 돌아온 응답값 읽어서 클라이언트로 보내
  while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
    Rio_writen(fd, buf, n);
  }
  // 연결 끝났으니 닫아줘야겠지?
  Close(server_fd);
}

// 이건 아직 왜 있는지 잘 모르겠음..;
void read_requesthdrs(rio_t *rp, char *request_buf, char *hostname) 
{
  char buf[MAXLINE];
  int host_header_found = 0;
  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);

  while (strcmp(buf, "\r\n")) {
    if (strstr(buf, "Host:")) {
      host_header_found = 1;
      strcat(request_buf, buf);
    } else if (strstr(buf, "User-Agent:") || strstr(buf, "Connection:") || strstr(buf, "Proxy-Connection:")) {
    } else {
      strcat(request_buf, buf);
    }
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }

  if (!host_header_found) {
    sprintf(buf, "Host: %s\r\n", hostname);
    strcat(request_buf, buf);
  }
  strcat(request_buf, user_agent_hdr);
  strcat(request_buf, conn_hdr);
  strcat(request_buf, proxy_conn_hdr);
  strcat(request_buf, "\r\n");
  return;
}

// uri 짜르는 부분
int parse_uri(char *uri, char *hostname, char *port, char *path) 
{
  char *host_ptr, *port_ptr, *path_ptr;
  // uri
  if (strncasecmp(uri, "http://", 7) != 0) {
    return -1;
  }

  host_ptr = uri + 7;
  path_ptr = strchr(host_ptr, '/');

  if (path_ptr == NULL) {
    strcpy(path, "/");
  } else {
    strcpy(path, path_ptr);
    *path_ptr = '\0';
  }

  port_ptr = strchr(host_ptr, ':');
  if (port_ptr == NULL) {
    strcpy(port, "80");
    *host_ptr = '\0';
    strcpy(hostname, host_ptr);
  } else {
    *port_ptr = '\0';
    strcpy(hostname, host_ptr);
    strcpy(port, port_ptr + 1);
  }

  if (path_ptr) *path_ptr = '/';
  if (port_ptr) *port_ptr = ':';
  return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
  char buf[MAXLINE], body[MAXBUF];
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=ffffff>\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Simple Proxy Server</em>\r\n", body);
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}










/* 좀 더 이해 쉬운 버전 - 똑같이 순차적이긴 하다 */
// void handle_client(int clientfd);
// int parse_uri(char *uri, char *hostname, char *port, char *path);

// int main(int argc, char **argv) {
//     int listenfd, connfd;
//     socklen_t clientlen;
//     struct sockaddr_storage clientaddr;

//     if (argc != 2) {
//         fprintf(stderr, "Usage: %s <port>\n", argv[0]);
//         exit(1);
//     }
//     // 여기서 proxy 서버는 port번호에서 listen 까지 해놓고 기다리고 있는거임
//     listenfd = Open_listenfd(argv[1]);
//     while (1) {
//         clientlen = sizeof(clientaddr);
//         // 이후 클라이언트가 프록시에 접속했을 때, 연결 받아서 connfd 생성
//         connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
//         printf("Accepted connection\n");

//         // handle_client -> 클라이언트 요청을 읽고, 서버에 전달하고, 서버 응답을 클라이언트로 보내는 역할
//         handle_client(connfd);
//         Close(connfd);  // 순차적이므로 한 요청 처리 후 종료
//     }
// }

// /* 클라이언트 요청을 처리하고 서버에 전달 후 응답 반환 */
// void handle_client(int clientfd) {
//     rio_t rio;
//     char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
//     char host[MAXLINE], port[10], path[MAXLINE];
//     //Rio 구조체 초기화 하고, clientfd 랑 연결  
//     Rio_readinitb(&rio, clientfd);
//     // 버퍼에서 한 줄 읽었는데 없다? -> 데이터 안 들어온거니까 return
//     if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
//     // 공백 기준으로 클라이언트에게 받은 요청라인 method, uri, version 으로 쪼개주고
//     sscanf(buf, "%s %s %s", method, uri, version);

//     // GET 요청만 받을 거니깐 비교해서 get 아니면 에러문 띄워주고 return
//     if (strcasecmp(method, "GET")) {
//         fprintf(stderr, "Proxy only implements GET\n");
//         return;
//     }
//     // 전에서 uri는 경로였었는데, porxy 들어오니까 요청전부가 되어버렸다... 
//     // uri  : http://localhost:8000/home.html
//     // host : localhost
//     // port : 8000
//     // path : /home.html
//     parse_uri(uri, host, port, path);

//     /* tiny 서버에 연결 */
//     // Open_clientfd 통해서 연결된 fd 받아주고
//     int serverfd = Open_clientfd(host, port);

//     /* tiny 서버에 요청 라인 보내기 */
//     snprintf(buf, MAXLINE, "GET %s %s\r\n", path, version);
//     Rio_writen(serverfd, buf, strlen(buf));

//     /* tiny 서버에 헤더 전달 */
//     while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
//         if (strcmp(buf, "\r\n") == 0) {
//             Rio_writen(serverfd, buf, strlen(buf));
//             break;
//         }
//         Rio_writen(serverfd, buf, strlen(buf));
//     }

//     /* tiny서버 응답을 porxy가 받고 클라이언트로 전달 */
//     int n;
//     while ((n = Rio_readn(serverfd, buf, MAXLINE)) > 0) {
//         Rio_writen(clientfd, buf, n);
//     }

//     Close(serverfd);
// }

// /* URI를 host, port, path로 분리 */
// int parse_uri(char *uri, char *hostname, char *port, char *path) {
//   char *host_ptr, *port_ptr, *path_ptr;
//   /* Check for "http://" prefix */
//   if (strncasecmp(uri, "http://", 7) != 0) {
//     return -1; /* Not a valid HTTP URI for this proxy */
//   }
//   host_ptr = uri + 7; /* Move pointer past "http://" */
//   /* Find the start of the path (the first '/') */
//   path_ptr = strchr(host_ptr, '/');
//   if (path_ptr == NULL) {
//     /* No path, e.g., http://www.google.com */
//     strcpy(path, "/");
//   } else {
//     /* Path found */
//     strcpy(path, path_ptr);
//     *path_ptr = '\0'; /* Terminate host string at the path */
//   }
//   /* Find the port separator (':') */
//   port_ptr = strchr(host_ptr, ':');
//   if (port_ptr == NULL) {
//     /* No port specified, use default 80 */
//     strcpy(port, "80");
//     *host_ptr = '\0'; /* This is safe because path_ptr was already used */
//     strcpy(hostname, host_ptr);
//   } else {
//     /* Port specified */
//     *port_ptr = '\0'; /* Terminate host string */
//     strcpy(hostname, host_ptr);
//     strcpy(port, port_ptr + 1);
//   }
//   /* After parsing, host_ptr points to the start of the hostname */
//   /* We've already copied it, but let's restore the original string */
//   /* just in case, though not strictly necessary here. */
//   if(path_ptr) *path_ptr = '/';
//   if(port_ptr) *port_ptr = ':';
//   return 0; /* Success */
// }