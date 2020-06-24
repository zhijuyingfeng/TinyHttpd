#include <assert.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/wait.h>
#include <time.h>

#define STDIN 0
#define STDOUT 1
#define STDERR 2
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

/*
log format:
$remote_addr,($remote_user),$time_local,$request_url,$status,$body_bytes_sent,$http_user_agent;
*/
struct log_record
{
    int status;//200,300, and so on
    int body_bytes_sent;
    uint32_t client_addr;
    char time_local[50];//asctime()
    char request_url[2049];
    char user_agent[2049];
};

struct accept_request_arg
{
    int client_sock;
    struct log_record log;
};

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *,struct log_record*);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *,struct log_record*);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *,struct log_record*);
int startup(u_short *);
void unimplemented(int);
void printlog(struct log_record* log);

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(uint16_t *port)
{
    int fd=-1,on=1;
    struct sockaddr_in name;

    fd=socket(AF_INET,SOCK_STREAM,0);
    assert(fd>=0);

    memset(&name,0,sizeof(name));
    name.sin_family=AF_INET;
    name.sin_port=htons(*port);
    name.sin_addr.s_addr=htonl(INADDR_ANY);

    assert(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof (on))>=0);

    assert(bind(fd,(struct sockaddr*)&name,sizeof (name))>=0);

    if(!(*port))
    {
        socklen_t namelen=sizeof(name);
        assert(getsockname(fd,(struct sockaddr*)&name,&namelen)!=-1);
        *port=ntohs(name.sin_port);
    }
    assert(listen(fd,5)==0);
    return fd;
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i=0;
    char c=0;
    int n;
    while((i<size-1)&&(c!='\n'))
    {
        n=recv(sock,&c,1,0);
        //printf("%02X\n",c);
        if(n>0)
        {
            if(c=='\r')
            {
                n=recv(sock,&c,1,MSG_PEEK);
                if((n>0)&&(c=='\n'))
                    recv(sock,&c,1,0);
                else
                    c='\n';
            }
            buf[i]=c;
            i++;
        }
        else
            c='\n';
    }
    buf[i]=0;
    return i;
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,const char *method, const char *query_string,struct log_record* log)
{
    char buf[1024];
    int cgi_output[2],cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars=1;
    int content_length=-1;

    buf[0]='A';buf[1]=0;
    if(strcasecmp(method,"GET")==0)
    {
        while((numchars>0)&&strcmp("\n",buf))//read and discard headers
            numchars=get_line(client,buf,sizeof (buf));
    }
    else if(strcasecmp(method,"POST")==0)
    {
        numchars=get_line(client,buf,sizeof(buf));
        while((numchars>0)&&strcmp("\n",buf))
        {
            buf[15]=0;
            if(strcasecmp(buf,"Content-Length:")==0)
                content_length=atoi(&(buf[16]));
            numchars=get_line(client,buf,sizeof(buf));
        }
        if(content_length==-1)
        {
            bad_request(client);
            log->status=400;
            return;
        }
    }
    else//HEAD or other
    {}
    if(pipe(cgi_output)<0)
    {
        cannot_execute(client);
        log->status=500;
        return;
    }
    if(pipe(cgi_input)<0)
    {
        cannot_execute(client);
        log->status=500;
        return;
    }

    if((pid=fork())<0)
    {
        cannot_execute(client);
        log->status=500;
        return;
    }

    if(0==pid)//child:CGI script
    {
        char meth_env[255],query_env[255],length_env[255];
        dup2(cgi_output[1],STDOUT);
        dup2(cgi_input[0],STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);

        sprintf(meth_env,"REQUEST_METHOD=%s",method);
        putenv(meth_env);

        if(strcasecmp(method,"GET")==0)
        {
            sprintf(query_env,"QUERY_STRING=%s",query_string);
            putenv(query_env);
        }
        else//POST
        {
            sprintf(length_env,"CONTENT_LENGTH=%d",content_length);
            putenv(length_env);
        }
        execl(path,NULL);
        exit(0);
    }
    else//partent
    {
        sprintf(buf,"HTTP/1.0 200 OK\r\n");
        log->status=200;
        send(client,buf,strlen(buf),0);
        close(cgi_output[1]);
        close(cgi_input[0]);
        if(strcasecmp(method,"POST")==0)
        {
            for(i=0;i<content_length;i++)
            {
                recv(client,&c,1,0);
                write(cgi_input[1],&c,1);
            }
        }
        while(read(cgi_output[0],&c,1)>0)
            send(client,&c,1,0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid,&status,0);
    }
}

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void* arg)
{
    int client=((struct accept_request_arg*)arg)->client_sock;
    struct log_record log=((struct accept_request_arg*)arg)->log;
    char buf[1024];
    size_t numchars,i,j;
    char method[255],url[255],path[512];
    struct stat st;

    int cgi=0;/* becomes true if server decides this is a CGI program */

    char *query_string = NULL;

    numchars=get_line(client,buf,sizeof(buf));
    i=j=0;
    while(!isspace(buf[i])&&(i<sizeof (method)-1))
    {
        method[i]=buf[i];
        i++;
    }
    j=i;
    method[i]=0;

    if(strcasecmp(method,"GET")&&strcasecmp(method,"POST"))
    {
        unimplemented(client);
        return;
    }
    if(strcasecmp(method,"POST")==0)
        cgi=1;
    i=0;
    while(isspace(buf[j])&&j<numchars)j++;
    while(!isspace(buf[j])&&(i<sizeof(url)-1)&&(j<numchars))
    {
        url[i]=buf[j];
        i++;j++;
    }
    url[i]=0;

    if(strcasecmp(method,"GET")==0)
    {
        query_string=url;
        while((*query_string!='?')&&(*query_string!=0))
            query_string++;
        if(*query_string=='?')
        {
            cgi=1;
            *query_string=0;
            query_string++;
        }
    }

    strcpy(log.request_url,url);
    sprintf(path,"htdocs%s",url);
    if(path[strlen(path)-1]=='/')
        strcat(path,"index.html");
    if(stat(path,&st)==-1)
    {
        while((numchars>0)&&strcmp("\n",buf))
            numchars=get_line(client,buf,sizeof(buf));
        not_found(client);
        log.status=404;
    }
    else
    {
        if((st.st_mode&S_IFMT)==S_IFDIR)
            strcat(path,"/index.html");
        if((st.st_mode&S_IXUSR)||(st.st_mode&S_IXGRP)||(st.st_mode&S_IXOTH))
            cgi=1;
        if(!cgi)
            serve_file(client,path,&log);
        else
            execute_cgi(client,path,method,query_string,&log);
    }
    close(client);
    printlog(&log);
    free(arg);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename,struct log_record* log)
{
    FILE* resource=NULL;
    int numchars=1;
    char buf[1024];

    buf[0]='A';buf[1]=0;
    while((numchars>0)&&strcmp("\n",buf))//read and discard headers
        numchars=get_line(client,buf,sizeof(buf));

    resource=fopen(filename,"r");
    if(resource==NULL)
    {
        not_found(client);
        log->status=404;
    }
    else
    {
        headers(client,filename);
        cat(client,resource,log);
    }
    fclose(resource);
    log->status=200;
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client,const char *filename)
{
    char buf[1024];
    (void)filename;/* could use filename to determine file type */

    strcpy(buf,"HTTP/1.0 200 OK\r\n");
    send(client,buf,strlen(buf),0);
    strcpy(buf,SERVER_STRING);
    send(client,buf,strlen(buf),0);
    sprintf(buf,"Content-Type: text/html\r\n");
    send(client,buf,strlen(buf),0);
    strcpy(buf,"\r\n");
    send(client,buf,strlen(buf),0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client,FILE* resource,struct log_record* log)
{
    char buf[1024];
    size_t len;

    fgets(buf,sizeof(buf),resource);
    while(!feof(resource))
    {
        len=strlen(buf);
        send(client,buf,len,0);
        log->body_bytes_sent+=len;
        fgets(buf,sizeof(buf),resource);
    }
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/*
log format:
$remote_addr,($remote_user),$time_local,$request_url,$status,$body_bytes_sent,$http_user_agent;
*/
void printlog(struct log_record* log)
{
    uint32_t client_addr=log->client_addr;
    uint32_t mask=0xFF;
    printf("[");
    for(int i=24;i>=0;i-=8)
    {
        if(i>0)
            printf("%d.",(client_addr>>i)&mask);
        else
            printf("%d] - ",(client_addr>>i)&mask);
    }
    printf("%s - ",log->time_local);
    printf("%s - ",log->request_url);
    printf("%d - ",log->status);
    printf("%d - ",log->body_bytes_sent);
    printf("%s\n",log->user_agent);
}

int main()
{
    int server_sock=-1;
    uint16_t port=4000;
    struct sockaddr_in client_name;
    int pthread_create_ret=-1;
    socklen_t client_name_len=sizeof(client_name);
    pthread_t newthread;

    server_sock=startup(&port);
    printf("httpd running on port %d\n",port);

    while(1)
    {
        time_t rawtime;
        char* time_;
        struct tm *timeinfo;
        struct accept_request_arg *arg;

        arg=(struct accept_request_arg*)malloc(sizeof(struct accept_request_arg));
        memset(arg,0,sizeof(struct accept_request_arg));
        arg->client_sock=-1;

        arg->client_sock=accept(server_sock,
                (struct sockaddr*)&client_name,
                &client_name_len);
//        printf("%d\n",arg->client_sock);

        arg->log.client_addr=ntohl(client_name.sin_addr.s_addr);
        rawtime=time(NULL);
        timeinfo=localtime(&rawtime);
        time_=asctime(timeinfo);
        strcpy(arg->log.time_local,time_);
        arg->log.time_local[strlen(arg->log.time_local)-1]=0;

//        printlog(&arg->log);

        assert(-1!=arg->client_sock);
//        accept_request(&client_sock);
        pthread_create_ret=pthread_create(&newthread,
                NULL,
                (void*)accept_request,
                (void*)(arg));
        assert(!pthread_create_ret);
    }
    close(server_sock);
    return 0;
}
