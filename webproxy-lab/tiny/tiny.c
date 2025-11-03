/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd); 
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd) 
{
  rio_t rio;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  if(strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "abc", "abcd");
    return;
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  // buf -> http 응답의 헤더, body -> http 응답의 본문
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title> Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  while(1) {
    Rio_readlineb(rp, buf, MAXLINE);
    // strcmp -> str(a, b) 1. 문자열 a, b 가 같음 -> 0 반환 | 2. a가 사전상 더 빠름 -> 음수 | 3. a가 사전상 더 느림 -> 양수 
    // while (strcmp(buf, "\r\n"))는 "buf와 \r\n이 다를 동안 계속 반복해라"라는 뜻.
    if (strcmp(buf, "\r\n") == 0) {
      break;
    }
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;
  /*
    strstr -> string in string -> strstr 함수는 true/false 반환 안 함.
    찾았을때 -> strstr("/cgi-bin/adder", "cgi-bin")를 실행하면, / 다음의 'c'를 가리키는 포인터를 반환.
    못 찾았을때(없을때) -> NULL 포인터 (즉, 0)를 반환.
  */  
  // uri 문자열 안에 cgi-bin이 없다면 ? -> 정적
  if(!strstr(uri, "cgi-bin")) {
    // strcpy -> 덮어쓰기 -> char *strcpy(char *dest, const char *src);
    //                     dest (Destination, 목적지): 복사한 문자열이 저장될 버퍼(문자 배열).
    //                     src (Source, 원본): 복사할 원본 문자열.
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    // strcat -> 이어쓰기
    strcat(filename, uri);
    // 요청된 uri의 끝이 / 이면 -> http://localhost:8000/ 이런식으로 디렉토리를 요청한 것 -> 기본 파일 제공
    if(uri[strlen(uri)-1] == '/') strcat(filename, "home.html");
    return -1;
  }
  // uri 문자열 안에 cgi-bin이 있다면 ? -> 동적
  else {
    // 우선 ? 기준 잡아 -> ? 앞으론 filename, 뒤론 전달할 cgiargs (인자)
    ptr = index(uri, '?');
    if(ptr) {
      // ? 바로 다음 위치부터 문자열 끝까지 cgiargs 버퍼에 복사 (? 바로 다음 위치부터 인자니까)
      strcpy(cgiargs, ptr + 1);
      // 그리고 물음표 \0 -> NULL 로 바꿔버림 (이제 \0 기준으로 앞은 filename 뒤론 인자)
      *ptr = '\0';
    }
    // 문자열 안에 ? 가 없다면
    else 
    // 인자 들어올 버퍼 초기화
    strcpy(cgiargs, "");
    // filename 에 . 복사 -> filname = '.'
    strcpy(filename, ".");
    // filename 뒤에 uri 붙여 넣어 -> filename (예시로 -> ./cgi-bin/adder)
    strcat(filename, uri);
    return 0;
  }
}
