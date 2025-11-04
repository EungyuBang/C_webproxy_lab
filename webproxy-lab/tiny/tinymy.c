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
// 우선 구조체 만들고 버퍼 만들어서, GET 요청 확인 뒤 -> 버퍼에서 요청 라인 읽어서 빈 파일이냐? 확인 후 -> parse 해줘야 함. -> 정적이냐? (파일이 맞냐? | 읽기 전용이냐?)  | 동적이냐 ? -> (파일이 맞냐? | 실행 가능하냐?)  
// static_serve, dynamic_serve 연결 해줘야 하고, 
void doit(int fd) {
  int is_static;
  rio_t rio;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];

  // 초기화 하고 연결해
  Rio_readinitb(&rio, fd);
  // 연결 라인 읽어
  Rio_readlineb(&rio, buf, MAXLINE);
  // 버퍼에 담아
  sscanf(buf, "%s %s %s", method, uri, version);

  if(strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does bot implement this method");
    return; 
  }
  read_requesthdrs(&rio);

  is_static = parse_uri(uri, filename, cgiargs);

  if(stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  // is_static -> 정적일때 1 반환, 동적일때 0 반환
  if(is_static) {
    // 정적일때 -> 우선 검사해줘야됨
    if(!S_ISREG(sbuf.st_mode) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  } 
  // 동적 일때 -> 검사 필요
  else {
    if(!S_ISREG(sbuf.st_mode) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, sbuf.st_size);
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
    if(strcmp(buf, "\r\n") == 0) {
      break;
    }
  }
  return;
}

// uri 정적/ 동적인지 나눠야함
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
  char *ptr;
  // uri에 cgi-bin 이 없어? -> 정적 /home.html
  if(!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if(uri[strlen(uri) -1 ] == '/') strcat(filename, "home.html");
    return 1;
  }
  // 동적
  else {
    ptr = index(uri, '?');
    if(ptr) {                  // '?' 있으면 /cgi-bin?15000&213
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';           
    } else {                 // '?' 없으면
        strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}


void get_filetype(char *filename, char *filetype)
{
  if(strstr(filename, ".html")) strcpy(filetype, "text/html");
  else if(strstr(filename, ".png")) strcpy(filetype, "image/png");
  else if(strstr(filename, ".gif")) strcpy(filetype, "image/gif");
  else if(strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
  else strcpy(filetype, "text/plain");
}

// 정적 보내줘야됨
void serve_static(int fd, char *filename, int filesize) 
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  get_filetype(filename, filetype);
  // send header
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);
  srcp = (char *)malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  free(srcp);
}

// 동적 보내줘야 됨
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }

  // 여기 수정: 자식 종료 대기 비차단 버전
  while (waitpid(-1, NULL, WNOHANG) > 0);
}