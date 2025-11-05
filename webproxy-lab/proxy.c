#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
 * proxy.c - A simple, sequential HTTP proxy
 * Based on the Tiny Web server from the CS:APP text
 */
#include "csapp.h"
/* You won't lose style points for including this long line in your code */
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
  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /* --- This is the sequential part --- */
    doit(connfd); // Handle one request
    Close(connfd); // Then close connection
    /* ---------------------------------- */
  }
}

void doit(int fd) 
{
  int server_fd; /* File descriptor for the origin server */
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char request_buf[MAXLINE]; /* Buffer to build the request to the server */
  rio_t rio_browser;         /* RIO buffer for browser connection */
  rio_t rio_server;          /* RIO buffer for server connection */
  /* 1. Read request line from browser */
  Rio_readinitb(&rio_browser, fd);
  if (!Rio_readlineb(&rio_browser, buf, MAXLINE))
    return;
  printf("Request from browser:\n%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  /* We only handle GET requests for this lab */
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
    return;
  }
  /* 2. Parse URI to get hostname, port, and path */
  if (parse_uri(uri, hostname, port, path) < 0) {
    clienterror(fd, uri, "400", "Bad Request",
                "Proxy couldn't parse the URI");
    return;
  }
  /* 3. Build the new HTTP request to send to the origin server */
  sprintf(request_buf, "GET %s HTTP/1.0\r\n", path);
  /* 4. Read headers from browser and add required headers */
  read_requesthdrs(&rio_browser, request_buf, hostname);
  /* 5. Connect to the origin server (Proxy acts as a client) */
  server_fd = Open_clientfd(hostname, port);
  if (server_fd < 0) {
    clienterror(fd, hostname, "502", "Bad Gateway",
                "Proxy couldn't connect to the server");
    return;
  }
  printf("--- Forwarding request to %s:%s ---\n%s", hostname, port, request_buf);
  /* 6. Send the modified request to the origin server */
  Rio_writen(server_fd, request_buf, strlen(request_buf));
  /*
   * 7. Relay the response from the origin server back to the browser
   * Read from server_fd, write to fd (browser)
   */
  Rio_readinitb(&rio_server, server_fd);
  ssize_t n;
  while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
    Rio_writen(fd, buf, n);
  }
  /* 8. Clean up */
  Close(server_fd);
}

void read_requesthdrs(rio_t *rp, char *request_buf, char *hostname) 
{
  char buf[MAXLINE];
  int host_header_found = 0;
  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while (strcmp(buf, "\r\n")) {
    /* If browser sends Host header, use it */
    if (strstr(buf, "Host:")) {
      host_header_found = 1;
      strcat(request_buf, buf);
    }
    /* Ignore other standard headers from browser */
    else if (strstr(buf, "User-Agent:") || strstr(buf, "Connection:") ||
               strstr(buf, "Proxy-Connection:")) {
      // Do nothing, we will add our own
    }
    /* Forward all other headers */
    else {
      strcat(request_buf, buf);
    }
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  /* Add mandatory headers if not found */
  if (!host_header_found) {
    sprintf(buf, "Host: %s\r\n", hostname);
    strcat(request_buf, buf);
  }
  strcat(request_buf, user_agent_hdr);
  strcat(request_buf, conn_hdr);
  strcat(request_buf, proxy_conn_hdr);
  /* Add the final blank line */
  strcat(request_buf, "\r\n");
  return;
}

int parse_uri(char *uri, char *hostname, char *port, char *path) 
{
  char *host_ptr, *port_ptr, *path_ptr;
  /* Check for "http://" prefix */
  if (strncasecmp(uri, "http://", 7) != 0) {
    return -1; /* Not a valid HTTP URI for this proxy */
  }
  host_ptr = uri + 7; /* Move pointer past "http://" */
  /* Find the start of the path (the first '/') */
  path_ptr = strchr(host_ptr, '/');
  if (path_ptr == NULL) {
    /* No path, e.g., http://www.google.com */
    strcpy(path, "/");
  } else {
    /* Path found */
    strcpy(path, path_ptr);
    *path_ptr = '\0'; /* Terminate host string at the path */
  }
  /* Find the port separator (':') */
  port_ptr = strchr(host_ptr, ':');
  if (port_ptr == NULL) {
    /* No port specified, use default 80 */
    strcpy(port, "80");
    strcpy(hostname, host_ptr);
  } else {
    /* Port specified */
    *port_ptr = '\0'; /* Terminate host string */
    strcpy(hostname, host_ptr);
    strcpy(port, port_ptr + 1);
  }
  /* After parsing, host_ptr points to the start of the hostname */
  /* We've already copied it, but let's restore the original string */
  /* just in case, though not strictly necessary here. */
  if(path_ptr) *path_ptr = '/';
  if(port_ptr) *port_ptr = ':';
  return 0; /* Success */
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,char *longmsg) 
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Simple Proxy Server</em>\r\n", body);
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}









// /* 좀 더 이해 쉬운 버전 - 똑같이 순차적이긴 하다 */
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