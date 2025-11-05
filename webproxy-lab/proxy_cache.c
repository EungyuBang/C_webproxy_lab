#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include "csapp.h"

/* 추천 최대 캐시 및 객체 크기 */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* 캐시 구조체 */
typedef struct cache_block {
    char *url;
    char *content;
    size_t size;
    int lru_counter;
    struct cache_block *next;
    struct cache_block *prev;
} cache_block;

typedef struct {
    cache_block *head;
    cache_block *tail;
    size_t total_size;
    int counter;
    sem_t mutex;
    sem_t w;
    int readcnt;
} cache_t;

/* Shared buffer of connected descriptors */
typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

/* 함수 프로토타입 */
void doit(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void build_request_header(rio_t *rio, char *header, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

/* 캐시 함수 */
void cache_init(cache_t *cache);
cache_block *cache_find(cache_t *cache, char *url);
void cache_insert(cache_t *cache, char *url, char *content, size_t size);
void cache_evict(cache_t *cache, size_t needed_size);
void cache_remove_block(cache_t *cache, cache_block *block);

/* 전역 변수 */
sbuf_t sbuf;
cache_t cache;

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* SIGPIPE 무시 */
    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* 캐시 초기화 */
    cache_init(&cache);

    /* Shared buffer 초기화 */
    sbuf_init(&sbuf, SBUFSIZE);

    /* 워커 스레드 생성 */
    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    listenfd = Open_listenfd(argv[1]);
    
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }
    
    return 0;
}

/* 워커 스레드 루틴 */
void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
    return NULL;
}

/* 클라이언트 요청 처리 */
void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char request_header[MAXLINE];
    rio_t rio_client, rio_server;
    int serverfd;
    cache_block *cached;

    /* 클라이언트로부터 요청 읽기 */
    Rio_readinitb(&rio_client, fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;
    
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* 캐시 확인 - Readers lock */
    P(&cache.mutex);
    cache.readcnt++;
    if (cache.readcnt == 1)
        P(&cache.w);
    V(&cache.mutex);

    cached = cache_find(&cache, uri);
    
    P(&cache.mutex);
    cache.readcnt--;
    if (cache.readcnt == 0)
        V(&cache.w);
    V(&cache.mutex);

    if (cached) {
        printf("Cache hit: %s\n", uri);
        Rio_writen(fd, cached->content, cached->size);
        
        /* LRU 카운터 업데이트 - Writer lock */
        P(&cache.w);
        cached->lru_counter = cache.counter++;
        V(&cache.w);
        return;
    }

    printf("Cache miss: %s\n", uri);

    /* URI 파싱 */
    parse_uri(uri, hostname, port, path);

    /* 서버에 연결 */
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(fd, hostname, "404", "Not found", "Could not connect to server");
        return;
    }

    /* 요청 헤더 구성 */
    build_request_header(&rio_client, request_header, hostname, port);

    /* 서버로 요청 전송 */
    Rio_readinitb(&rio_server, serverfd);
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, buf, strlen(buf));
    Rio_writen(serverfd, request_header, strlen(request_header));

    /* 서버 응답 읽고 클라이언트로 전달 + 캐싱 */
    size_t n, total_size = 0;
    char *cache_buf = Malloc(MAX_OBJECT_SIZE);  // 동적 할당으로 변경
    int cacheable = 1;

    // 핵심 수정: Rio_readnb 사용 (바이너리 파일 대응)
    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        
        /* 캐싱 가능한지 확인 */
        if (cacheable && total_size + n <= MAX_OBJECT_SIZE) {
            memcpy(cache_buf + total_size, buf, n);
            total_size += n;
        } else {
            cacheable = 0;
        }
    }

    /* 캐시에 저장 */
    if (cacheable && total_size > 0 && total_size <= MAX_OBJECT_SIZE) {
        char *content = Malloc(total_size);
        memcpy(content, cache_buf, total_size);
        
        P(&cache.w);
        cache_insert(&cache, uri, content, total_size);
        V(&cache.w);
        
        printf("Cached: %s (%zu bytes)\n", uri, total_size);
    }
    
    Free(cache_buf);  // 임시 버퍼 해제
    Close(serverfd);
}

/* URI 파싱 */
void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *ptr;
    char uri_copy[MAXLINE];
    
    strcpy(uri_copy, uri);
    strcpy(port, "80"); /* 기본 포트 */
    
    /* http:// 제거 */
    ptr = strstr(uri_copy, "://");
    if (ptr)
        ptr += 3;
    else
        ptr = uri_copy;
    
    char *path_ptr = strchr(ptr, '/');
    if (path_ptr) {
        strcpy(path, path_ptr);
        *path_ptr = '\0';
    } else {
        strcpy(path, "/");
    }
    
    /* 포트 번호 추출 */
    char *port_ptr = strchr(ptr, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
    }
    
    strcpy(hostname, ptr);
}

/* HTTP 요청 헤더 구성 */
void build_request_header(rio_t *rio, char *header, char *hostname, char *port) {
    char buf[MAXLINE];
    char host_hdr[MAXLINE], other_hdr[MAXLINE];
    
    host_hdr[0] = '\0';
    other_hdr[0] = '\0';
    
    while (Rio_readlineb(rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n"))
            break;
        
        if (strstr(buf, "Host:")) {
            strcpy(host_hdr, buf);
        } else if (!strstr(buf, "User-Agent:") && 
                   !strstr(buf, "Connection:") && 
                   !strstr(buf, "Proxy-Connection:")) {
            strcat(other_hdr, buf);
        }
    }
    
    if (!strlen(host_hdr)) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }
    
    sprintf(header, "%s%s%s%s%s\r\n",
            host_hdr,
            "Connection: close\r\n",
            "Proxy-Connection: close\r\n",
            user_agent_hdr,
            other_hdr);
}

/* 에러 응답 전송 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

/* Shared buffer 함수들 */
void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) {
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear) % (sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front) % (sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

/* 캐시 함수들 */
void cache_init(cache_t *cache) {
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    cache->counter = 0;
    cache->readcnt = 0;
    Sem_init(&cache->mutex, 0, 1);
    Sem_init(&cache->w, 0, 1);
}

cache_block *cache_find(cache_t *cache, char *url) {
    cache_block *block = cache->head;
    while (block) {
        if (strcmp(block->url, url) == 0) {
            return block;
        }
        block = block->next;
    }
    return NULL;
}

void cache_insert(cache_t *cache, char *url, char *content, size_t size) {
    if (size > MAX_OBJECT_SIZE)
        return;

    /* 중복 확인 - 이미 존재하면 업데이트 */
    cache_block *existing = cache_find(cache, url);
    if (existing) {
        Free(existing->content);
        existing->content = content;
        existing->size = size;
        existing->lru_counter = cache->counter++;
        return;
    }

    /* LRU 정책: 필요시 제거 */
    while (cache->total_size + size > MAX_CACHE_SIZE && cache->tail) {
        cache_evict(cache, size);
    }

    cache_block *block = Malloc(sizeof(cache_block));
    block->url = Malloc(strlen(url) + 1);
    strcpy(block->url, url);
    block->content = content;
    block->size = size;
    block->lru_counter = cache->counter++;
    block->next = cache->head;
    block->prev = NULL;

    if (cache->head)
        cache->head->prev = block;
    cache->head = block;
    
    if (!cache->tail)
        cache->tail = block;

    cache->total_size += size;
}

void cache_evict(cache_t *cache, size_t needed_size) {
    if (!cache->tail)
        return;

    cache_block *victim = cache->tail;
    cache_block *block = cache->head;

    /* LRU: 가장 오래된 것 찾기 */
    while (block) {
        if (block->lru_counter < victim->lru_counter)
            victim = block;
        block = block->next;
    }

    printf("Evicting: %s\n", victim->url);
    cache_remove_block(cache, victim);
}

void cache_remove_block(cache_t *cache, cache_block *block) {
    if (block->prev)
        block->prev->next = block->next;
    else
        cache->head = block->next;

    if (block->next)
        block->next->prev = block->prev;
    else
        cache->tail = block->prev;

    cache->total_size -= block->size;
    Free(block->url);
    Free(block->content);
    Free(block);
}