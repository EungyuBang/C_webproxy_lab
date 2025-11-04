#include "csapp.h"

int main(void)
{
    char *buf, *p;
    char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
    int n1 = 0, n2 = 0;

    /* Extract the two arguments */
    buf = getenv("QUERY_STRING");
    if (buf != NULL) {
        char query_copy[MAXLINE];
        strcpy(query_copy, buf);  // buf 훼손되지 않게 복사

        p = strchr(query_copy, '&');
        if (p) {
            *p = '\0';
            strcpy(arg1, query_copy);
            strcpy(arg2, p + 1);
        } else {
            strcpy(arg1, query_copy);
            strcpy(arg2, "0");
        }

        if (strchr(arg1, '='))
            n1 = atoi(strchr(arg1, '=') + 1);
        else
            n1 = atoi(arg1);

        if (strchr(arg2, '='))
            n2 = atoi(strchr(arg2, '=') + 1);
        else
            n2 = atoi(arg2);

        /* Make the response body */
        sprintf(content, "QUERY_STRING=%s\r\n<p>", buf);
        sprintf(content + strlen(content), "Welcome to add.com: ");
        sprintf(content + strlen(content), "THE Internet addition portal.\r\n<p>");
        sprintf(content + strlen(content), "The answer is: %d + %d = %d\r\n<p>",
                n1, n2, n1 + n2);
        sprintf(content + strlen(content), "Thanks for visiting!\r\n");

        /* Generate the HTTP response */
        printf("Content-type: text/html\r\n");
        printf("Content-length: %d\r\n", (int)strlen(content));
        printf("\r\n");
        printf("%s", content);
        fflush(stdout);
    }

    exit(0);
}
