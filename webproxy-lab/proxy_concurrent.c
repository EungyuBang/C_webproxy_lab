/*
 * proxy.c - A concurrent HTTP proxy
 *
 * Based on the sequential proxy, now with concurrency support using:
 * - Pre-threading with a shared buffer (producer-consumer pattern)
 * - Multiple worker threads to handle requests simultaneously
 */
#include "csapp.h"
#include <pthread.h>

/* Recommended max cache and object sizes (for later stages) */
// #define MAX_CACHE_SIZE 1049000
// #define MAX_OBJECT_SIZE 102400

/* Thread pool configuration */
#define NTHREADS 4      // Number of worker threads
#define SBUFSIZE 16     // Size of the shared buffer

/* You'll need these globals for your proxy */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

/* Shared buffer of connected descriptors */
typedef struct {
  int *buf;          /* Buffer array */
  int n;             /* Maximum number of slots */
  int front;         /* buf[(front+1)%n] is first item */
  int rear;          /* buf[rear%n] is last item */
  sem_t mutex;       /* Protects accesses to buf */
  sem_t slots;       /* Counts available slots */
  sem_t items;       /* Counts available items */
} sbuf_t;

/* Global shared buffer */
sbuf_t sbuf;

/* Function prototypes */
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *request_buf, char *hostname);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void *thread(void *vargp);
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

/*
 * main - The main server loop (now concurrent)
 */
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* Ignore SIGPIPE signals */
  Signal(SIGPIPE, SIG_IGN);

  /* Initialize the shared buffer */
  sbuf_init(&sbuf, SBUFSIZE);

  /* Create worker threads (pre-threading) */
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  listenfd = Open_listenfd(argv[1]);
  printf("Proxy listening on port %s\n", argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    /* --- This is now concurrent --- */
    /* Insert the connection descriptor into the shared buffer */
    sbuf_insert(&sbuf, connfd);
    /* Worker threads will handle it */
    /* ------------------------------ */
  }
  
  return 0;
}

/*
 * thread - Worker thread routine
 * Each thread continuously removes connfd from the buffer and serves it
 */
void *thread(void *vargp) {
  Pthread_detach(pthread_self());
  
  while (1) {
    int connfd = sbuf_remove(&sbuf);  /* Remove connfd from buffer */
    doit(connfd);                      /* Service client */
    Close(connfd);                     /* Close connection */
  }
  
  return NULL;
}

/*
 * doit - Handle one HTTP request/response transaction
 * This is the core proxy logic (unchanged from sequential version)
 */
void doit(int fd) {
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

/*
 * read_requesthdrs - Read HTTP headers from browser (rp) and
 * build the new request (request_buf) to be
 * sent to the origin server.
 */
void read_requesthdrs(rio_t *rp, char *request_buf, char *hostname) {
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

/*
 * parse_uri - Parse a URI into hostname, port, and path.
 * URI format: http://<hostname>[:<port>]/<path>
 * Returns 0 on success, -1 on error.
 */
int parse_uri(char *uri, char *hostname, char *port, char *path) {
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

  /* Restore the original string */
  if (path_ptr) *path_ptr = '/';
  if (port_ptr) *port_ptr = ':';

  return 0; /* Success */
}

/*
 * clienterror - returns an error message to the client
 * This is identical to the sequential version
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Concurrent Proxy Server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* ============================================ */
/* Shared Buffer Implementation (sbuf_t)       */
/* ============================================ */

/*
 * sbuf_init - Initialize a shared buffer with n slots
 */
void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = Calloc(n, sizeof(int));
  sp->n = n;                    /* Buffer holds max of n items */
  sp->front = sp->rear = 0;     /* Empty buffer iff front == rear */
  Sem_init(&sp->mutex, 0, 1);   /* Binary semaphore for locking */
  Sem_init(&sp->slots, 0, n);   /* Initially, buf has n empty slots */
  Sem_init(&sp->items, 0, 0);   /* Initially, buf has zero items */
}

/*
 * sbuf_deinit - Clean up buffer sp
 */
void sbuf_deinit(sbuf_t *sp) {
  Free(sp->buf);
}

/*
 * sbuf_insert - Insert item onto the rear of shared buffer sp
 * Producer operation
 */
void sbuf_insert(sbuf_t *sp, int item) {
  P(&sp->slots);                          /* Wait for available slot */
  P(&sp->mutex);                          /* Lock the buffer */
  sp->buf[(++sp->rear) % (sp->n)] = item; /* Insert the item */
  V(&sp->mutex);                          /* Unlock the buffer */
  V(&sp->items);                          /* Announce available item */
}

/*
 * sbuf_remove - Remove and return the first item from buffer sp
 * Consumer operation
 */
int sbuf_remove(sbuf_t *sp) {
  int item;
  P(&sp->items);                           /* Wait for available item */
  P(&sp->mutex);                           /* Lock the buffer */
  item = sp->buf[(++sp->front) % (sp->n)]; /* Remove the item */
  V(&sp->mutex);                           /* Unlock the buffer */
  V(&sp->slots);                           /* Announce available slot */
  return item;
}