#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *client_header);
int parse_uri(char *uri, char *filename, char *hostname, char *port);
void make_request(char *hostname, char *filename, char *port, char *version, char *request, char *client_header);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void *doit_thread(void *vargp);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
    int listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        int *connfd = malloc(sizeof(int));
	    clientlen = sizeof(clientaddr);
	    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, doit_thread,connfd);

        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
    }
}
/* $end tinymain */

void *doit_thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{   
    int serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], client_header[MAXLINE];
    char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE], request[MAXLINE];
    char response[100*MAXLINE];
    rio_t rio, server_rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest

    //change version
    strcpy(version,"HTTP/1.0");
    //check method
    if (strcasecmp(method, "GET")) {                     
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }
    //get client_header                           
    read_requesthdrs(&rio,client_header);                              

    //get hostname, filename, port
    parse_uri(uri, hostname, filename, port);

    //make request
    make_request(hostname, filename, port, version, request, client_header);

    //connect to server
    serverfd = open_clientfd(hostname, port);
    rio_readinitb(&server_rio, serverfd);
    rio_writen(serverfd,request,strlen(request));

    //get response from server
    int len = rio_readnb(&server_rio,response,sizeof(response)); 
    printf("response data from server: %s\n",response);

    //send to client
    rio_writen(fd,response,len);

    close(serverfd);
}
/* $end doit */

void make_request(char *hostname, char *filename, char *port, char *version, char *request, char *client_header)
{
    strcpy(request,"");
    sprintf(request, "GET %s %s\r\n", filename, version);
    sprintf(request, "%sHost: %s\r\n", request, hostname);
    sprintf(request, "%s%s", request, client_header);
    sprintf(request, "%s%s", request, user_agent_hdr);
    sprintf(request, "%sConnection: close\r\n", request);
    sprintf(request, "%sProxy-Connection: close\r\n", request);
    strcat(request, "\r\n");
    printf("%s",request);
    return;    
}

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char *client_header) 
{
    char buf[MAXLINE];
    strcpy(client_header,"");
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {         
        strcat(client_header,buf);
        printf("%s", buf);
        Rio_readlineb(rp, buf, MAXLINE);
    }
    printf("%s", buf);
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *hostname, char *filename, char *port) 
{   
    strcpy(hostname,"");
    strcpy(filename,"");
    strcpy(port,"80");
    char *buf;
    buf = uri;
    char *bufcp;
    char *endbuf = uri+strlen(uri);
    buf = buf+7;

    //get hostname
    while(buf!=endbuf){
        if(*buf != '/' && *buf != ':'){
            sprintf(hostname,"%s%c",hostname,*buf);
        }
        else{
            strcat(hostname,"\0");
            bufcp = buf;
            break;
        }
        buf ++;
    }
    //get port and filename
    if(*bufcp == ':'){
        buf = bufcp;
        strcpy(port,"");
        buf ++;
        while(*buf!='/'){
            sprintf(port,"%s%c",port,*buf);  
            buf ++;  
        }
        strcat(port,"\0");
        while(buf!=endbuf){
            sprintf(filename,"%s%c",filename,*buf);
            buf ++;
        }
        strcat(filename,"\0");

    }

    //get filename
    if(*bufcp == '/'){
        buf = bufcp;
        while(buf!=endbuf){
            sprintf(filename,"%s%c",filename,*buf);
            buf ++;
        }
        strcat(filename,"\0");
    }
    return 0;
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve
    printf("Response headers:\n");
    printf("%s", buf);

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
    Close(srcfd);                           //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
    Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
	strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
