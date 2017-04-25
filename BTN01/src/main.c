#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <json-c/json.h>
#include <json-c/json_tokener.h>

#define BYTE 1024
#define SERVER_STRING "kirix"
#define TRUE 1
#define FALSE 0
#define MAXEVENTS 100

struct Data {
    int result;
    char path[100];
    char query[100];
};

//Init socket for listen from clients
int startup(u_short*, char);

//Handle a bad request error for client
void bad_request(int);

//Handle a file not found error for client
void not_found(int);

//Handle a not implement error for client
void not_implement(int);

//Handle error in console and exit
void error_die(const char*);

//Analize request from clients
struct Data recv_request(void*);

//Analize and response request from client
int accept_request(void *);

//Handle a not allow error !(GET/ POST) for client
void not_allow(int);

//Send header for client
void headers(int, int);

//Compute file size
int file_size(const char*);

//Send data of file for client
void send_file(int, const char*);

//Run a query
void exec_file(int, const char*);

//Get content type in html
void get_type(const char*, char*);

//Init json to manage data
struct json_object *init_capitals();

struct json_object *capitals;

int main(int argc, char **argv)
{
    pthread_t thread;
    int sock_serv = -1, sock_client = -1, *new_sock = NULL;
    u_short port = 3000;
    struct sockaddr_in addr_client;
    socklen_t client_len = sizeof(addr_client);
    capitals = init_capitals();

    if(argc != 2)
    {
        printf("./1312232 -<process>|<thread>|<select>|<poll>|<epoll>\n");
        return EXIT_FAILURE;
    }else if(strcmp(argv[1], "-iterate") == 0)
    {
        printf("Server start in iterate mode\n");
        sock_serv = startup(&port, FALSE);
        while (TRUE)
        {
            sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len);
            if (sock_client == -1)
                perror("accept");
            accept_request(&sock_client);
        }
    }
    else if(strcmp(argv[1], "-process") == 0)
    {
        printf("Server start in process mode\n");
        sock_serv = startup(&port, FALSE);
        while (TRUE)
        {
            sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len);
            if (sock_client == -1)
                perror("accept");

            if(fork() == 0)
            {
                accept_request(&sock_client);
            }
        }
    }
    else if(strcmp(argv[1], "-thread") == 0)
    {
        printf("Server start in thread mode\n");
        sock_serv = startup(&port, FALSE);
        while (TRUE)
        {
            sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len);
            if (sock_client == -1)
                perror("accept");

            new_sock = malloc(1);
            *new_sock = sock_client;

            pthread_create(&thread, NULL, accept_request, (void*)new_sock);
        }
    }
    else if(strcmp(argv[1], "-select") == 0)
    {
        printf("Server start in select mode\n");
        sock_serv = startup(&port, TRUE);
        int client_socket[BYTE] = {0}, max_clients = BYTE, activity, i, max_sd;
        fd_set readfds;

        while (TRUE)
        {
            FD_ZERO(&readfds);
            FD_SET(sock_serv, &readfds);
            max_sd = sock_serv;

            for ( i = 0 ; i < max_clients ; i++)
            {
                sock_client = client_socket[i];
                if(sock_client > 0)
                {
                    FD_SET(sock_client, &readfds);
                }

                if(sock_client > max_sd)
                    max_sd = sock_client;
            }

            activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
            if ((activity < 0) && (errno!=EINTR))
            {
                printf("select error\n");
            }

            if (FD_ISSET(sock_serv, &readfds))
            {
                if ((sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len)) < 0)
                {
                    perror("accept");
                }

                for (i = 0; i < max_clients; i++)
                {
                    if( client_socket[i] == 0 )
                    {
                        client_socket[i] = sock_client;
                        break;
                    }
                }
            }

            for (i = 0; i < max_clients; i++)
            {
                if (FD_ISSET(client_socket[i], &readfds))
                {
                    if(accept_request(&client_socket[i]) < 0)
                        client_socket[i] = 0;
                }
            }
        }
    }
    else if(strcmp(argv[1], "-poll") == 0)
    {
        printf("Server start in poll mode\n");
        struct pollfd fds[200];
        int nfds = 1, compress = FALSE, rc = 0;
        sock_serv = startup(&port, TRUE);

        memset(fds, 0, sizeof(fds));
        fds[0].fd = sock_serv;
        fds[0].events = POLLIN;

        while(TRUE)
        {
            if ((rc = poll(fds, nfds, NULL)) < 0)
            {
                fprintf(stderr, "poll()");
                break;
            }

            for(int i = 0; i < nfds; i++)
            {
                if(fds[i].revents == 0)
                    continue;

                if (fds[i].fd == sock_serv)
                {
                    if ((sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len)) < 0)
                    {
                        perror("accept");
                    }

                    if(nfds < 200)
                    {
                        fds[nfds].fd = sock_client;
                        fds[nfds].events = POLLIN;
                        nfds++;
                        break;
                    }
                }
                else
                {
                    if(accept_request(&fds[i].fd) < 0)
                    {
                        fds[i].fd = -1;
                        compress = TRUE;
                    }
                }
            }

            if(compress)
            {
                for (int i = 0; i < nfds; i++)
                {
                    compress = FALSE;
                    if (fds[i].fd == -1)
                    {
                        for(int j = i; j < nfds; j++)
                        {
                            fds[j].fd = fds[j+1].fd;
                        }
                        nfds--;
                    }
                }
            }

        }

    }
    else if(strcmp(argv[1], "-epoll") == 0)
    {
        struct epoll_event event;
        struct epoll_event events[MAXEVENTS];
        int efd, nfds = -1;

        printf("Server start in epoll mode\n");

        sock_serv = startup(&port, TRUE);

        if((efd = epoll_create(MAXEVENTS)) == -1)
        {
            perror ("epoll_create");
            exit(EXIT_FAILURE);
        }

        event.data.fd = sock_serv;
        event.events = EPOLLIN;
        if(epoll_ctl(efd, EPOLL_CTL_ADD, sock_serv, &event) == -1)
        {
            perror ("epoll_ctl");
            exit(EXIT_FAILURE);
        }

        while(TRUE)
        {
            if((nfds = epoll_wait (efd, events, MAXEVENTS, -1)) == -1)
            {
                perror("epoll_wait");
                exit(EXIT_FAILURE);
            }
            for(int i = 0; i < nfds; i++)
            {
                int fd = events[i].data.fd;
                if(sock_serv == fd)
                {
                    while ((sock_client = accept(sock_serv,(struct sockaddr *)&addr_client, &client_len)) > 0)
                    {
                        setnonblocking(sock_client);
                        event.data.fd = sock_client;
                        event.events = EPOLLIN | EPOLLET;
                        if (epoll_ctl (efd, EPOLL_CTL_ADD, sock_client, &event) == -1)
                        {
                            perror ("epoll_ctl: add");
                            exit(EXIT_FAILURE);
                        }
                    }

                    if (sock_client == -1){
                        if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
                            perror("accept");
                    }
                    continue;
                }
                struct Data result;

                if (events[i].events & EPOLLIN) {
                    result = recv_request(&events[i].data.fd);

                    if(result.result < 0)
                    {
                        close(fd);
                        break;
                    }

                    event.data.fd = fd;
                    event.events = events[i].events | EPOLLOUT;
                    if (epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event) == -1)
                    {
                        perror("epoll_ctl: mod");
                    }
                }

                if(events[i].events & EPOLLOUT){
                    if(result.result == 1)
                        send_file(fd, result.path);
                    else
                        exec_file(fd, result.query);
                }
            }
        }
    }else
    {
        printf("Wrong server method!, Please try again with: \n");
        printf("./1312232 -<process>|<thread>|<select>|<poll>|<epoll>\n");
        return EXIT_FAILURE;
    }

    close(sock_serv);

    return EXIT_SUCCESS;
}

void setnonblocking(int sockfd) {
    int opts;

   opts = fcntl(sockfd, F_GETFL);
    if(opts < 0) {
        perror("fcntl(F_GETFL)\n");
        exit(1);
    }
    opts = (opts | O_NONBLOCK);
    if(fcntl(sockfd, F_SETFL, opts) < 0) {
        perror("fcntl(F_SETFL)\n");
        exit(1);
    }
}

struct json_object *init_capitals()
{
    json_object *capitals;
    int fd = -1;
    char data[BUFSIZ];

    if((fd = open("capital.json", O_RDONLY))!=-1)
    {
        read(fd, data, sizeof(data));
    }
    else
    {
        printf("Can't load data from capital.json\n");
        exit(1);
    }
    close(fd);

    capitals = json_tokener_parse(data);
    return capitals;
};

int accept_request(void *lclient)
{
    int client = *(int*)lclient;
    char buf[BYTE];
    char method[5];
    char query[BYTE];
    char path[BYTE];

    int rcvd;

    bzero(buf, sizeof(buf));
    bzero(method, sizeof(method));
    bzero(path, sizeof(path));
    bzero(query, sizeof(query));

    rcvd = recv(client, buf, sizeof(buf), 0);
    if(rcvd <= 0)
    {
        bad_request(client);
        return -1;
    }

    //string processing
    sscanf(buf, "%s %s", method, query);
    char *tmp = &query[1];
    strcpy(query, tmp);

    //only support get and post method
    if(strncmp(method, "GET", 3) && strncmp(method, "POST", 4))
    {
        not_allow(client);
        return -1;
    }
    else
    {
        char *temp = strstr(query, "?");
        if(temp != NULL)
        {
            strncpy(path, query, strlen(query) - strlen(temp));
            strcpy(query, temp + 1);
        }
        else
        {
            strcpy(path, query);
        }

        if(strlen(path) < 1)
            strcpy(path, "index.html");

        if(strncmp(method, "POST", 4) == 0)
        {
            bzero(query, sizeof(query));
            int buf_len = strlen(buf);
            for(int i = buf_len - 1; i >= 0; i--)
            {
                if(buf[i] == '\n')
                {
                    if(i + 1 < buf_len)
                    {
                        temp = &buf[i + 1];
                        strcpy(query, temp);
                    }
                    break;
                }
            }
        }
    }

    if(strstr(query, "=") == NULL)
        send_file(client, path);
    else
        exec_file(client, query);

    close(client);
}

struct Data recv_request(void *lclient)
{
    int client = *(int*)lclient;
    char buf[BYTE];
    char method[5];
    char query[BYTE];
    char path[BYTE];
    struct Data result;

    int rcvd;

    bzero(buf, sizeof(buf));
    bzero(method, sizeof(method));
    bzero(path, sizeof(path));
    bzero(query, sizeof(query));

    rcvd = recv(client, buf, sizeof(buf), 0);
    if(rcvd <= 0)
    {
        bad_request(client);
        result.result = -1;
        return result;
    }

    //string processing
    sscanf(buf, "%s %s", method, query);
    char *tmp = &query[1];
    strcpy(query, tmp);

    //only support get and post method
    if(strncmp(method, "GET", 3) && strncmp(method, "POST", 4))
    {
        not_allow(client);
        result.result = -1;
        return result;
    }
    else
    {
        char *temp = strstr(query, "?");
        if(temp != NULL)
        {
            strncpy(path, query, strlen(query) - strlen(temp));
            strcpy(query, temp + 1);
        }
        else
        {
            strcpy(path, query);
        }

        if(strlen(path) < 1)
            strcpy(path, "index.html");

        if(strncmp(method, "POST", 4) == 0)
        {
            bzero(query, sizeof(query));
            int buf_len = strlen(buf);
            for(int i = buf_len - 1; i >= 0; i--)
            {
                if(buf[i] == '\n')
                {
                    if(i + 1 < buf_len)
                    {
                        temp = &buf[i + 1];
                        strcpy(query, temp);
                    }
                    break;
                }
            }
        }
    }

    if(strstr(query, "=") != NULL)
        result.result = 2;
    else
        result.result = 1;

    strcpy(result.path, path);
    strcpy(result.query, query);
    return result;
}

void exec_file(int client, const char *query)
{
    char fdata[512], data[1024];
    int fd, fsize;

    if ((fd=open("result.html", O_RDONLY))!=-1 )    //FILE FOUND
    {
	bzero(fdata, sizeof(fdata));
        fsize = read(fd, fdata, sizeof(fdata));
    }
    close(fd);

    char *country = strstr(query, "country="); //
    if(country == NULL)
    {
        bad_request(client);
        return;
    }
    country += 8;

    char temp[255];
    strcpy(temp, country);

    for(int i = 0; i < strlen(temp); i++)
    {
        if(temp[i] == '+')
        {
            temp[i] = ' ';
            country[i] = ' ';
        }

        else
            temp[i] = tolower(temp[i]);
    }

    const char *capital = json_object_get_string(json_object_object_get(capitals, temp));
    if(capital == NULL)
        capital = "This country not exist in database";

    bzero(data, sizeof(data));
    sprintf(data, fdata, country, capital);
    headers(client, strlen(data));
    send(client, data, strlen(data), 0);
    close(client);
}

//run script file
void exec_php_file(int client, const char *filename, const char *query)
{
    FILE *fp, *fp2;
    char buf[BYTE];

    fp2 = fopen(filename, "r");
    if(fp2 == NULL)
    {
        not_found(client);
        return;
    }
    else
        fclose(fp2);

    sprintf(buf, "php -r 'parse_str($argv[2],$_GET);include $argv[1];' %s '%s'", filename, query); //command
    fp = popen(buf, "r");
    if (fp == NULL)
    {
        not_implement(client);
        return;
    }

    bzero(buf, sizeof(buf));
    headers(client, filename);
    while (fgets(buf, sizeof(buf), fp) != NULL)
    {
        send(client, buf, strlen(buf), 0);
        bzero(buf, sizeof(buf));
    }

    printf("Exit code: %i\n", WEXITSTATUS(pclose(fp)));

    close(client);
}

void send_file(int client, const char *filename)
{
    char buf[BYTE];
    int fd = 0, numread = 0;

    bzero(buf, sizeof(buf));

    if ((fd=open(filename, O_RDONLY))!=-1 )    //FILE FOUND
    {
        headers(client, file_size(filename));
        while ( (numread=read(fd, buf, sizeof(buf)))>0 )
            write (client, buf, numread);
    }
    else
        not_found(client);

    close(fd);
    close(client);
}

void get_type(const char *path, char *content_type)
{
    const char *temp = NULL;
    int i;

    for(i = strlen(path) - 1; i >= 0; i--)
        if(path[i] == '.')
            break;

    if(i > 0)
        temp = &path[i + 1];

    if(strncmp(temp, "php",3) == 0 || strncmp(temp, "html",4) == 0)
        sprintf(content_type, "%s", "text/html");
    else if(strncmp(temp, "gif",3) == 0 || strncmp(temp, "jpg",3) == 0 || strncmp(temp, "jpeg",3) == 0 || strncmp(temp, "png",3) == 0 || strncmp(temp, "gif",3) == 0)
        sprintf(content_type, "%s %s", "image/", temp);
    else
        sprintf(content_type, "%s", "text/plain");
}

int file_size(const char *filename)
{
    struct stat st;
    stat(filename, &st);
    int size = st.st_size;
    return size;
}

void headers(int client, int ct_len)
{
    char *msg = "HTTP/1.1 200 OK\r\nConnection:close\r\nServer: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n";
    char buf[1024];
    bzero(buf, sizeof(buf));
    sprintf(buf, msg, SERVER_STRING, ct_len);
    send(client, buf, strlen(buf), 0);
}

void not_found(int client)
{
	char *header = "HTTP/1.1 404 Not Found\r\nServer: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n";
	char *body = "<html>\n<head><title>404 Not Found</title></head>\r\n<body bgcolor=\"white\">\r\n<center><h1>404 Not Found</h1></center>\r\n<hr><center>%s</center>\r\n</body>\n<html>\r\n";
    char buf[1024];
    bzero(buf, sizeof(buf));
    sprintf(buf, header, SERVER_STRING, strlen(body));
    send(client, buf, strlen(buf), 0);

    bzero(buf, sizeof(buf));
    sprintf(buf, body, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    close(client);
}

void bad_request(int client)
{
	char *header = "HTTP/1.1 400 Bad Request\r\nServer: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n";
	char *body = "<html>\n<head><title>400 Bad Request</title></head>\r\n<body bgcolor=\"white\">\r\n<center><h1>400 Bad Request</h1></center>\r\n<hr><center>%s</center>\r\n</body>\n<html>\r\n";
    char buf[1024];
    bzero(buf, sizeof(buf));
    sprintf(buf, header, SERVER_STRING, strlen(body));
    send(client, buf, strlen(buf), 0);

    bzero(buf, sizeof(buf));
    sprintf(buf, body, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    close(client);
}

void not_allow (int client)
{
	char *header = "HTTP/1.1 405 Not Allowed\r\nServer: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n";
	char *body = "<html>\n<head><title>405 Not Allowed</title></head>\r\n<body bgcolor=\"white\">\r\n<center><h1>405 Not Allowed</h1></center>\r\n<hr><center>%s</center>\r\n</body>\n<html>\r\n";
    char buf[1024];
    bzero(buf, sizeof(buf));
    sprintf(buf, header, SERVER_STRING, strlen(body));
    send(client, buf, strlen(buf), 0);

    
    bzero(buf, sizeof(buf));
    sprintf(buf, body, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    close(client);
}

void not_implement(int client)
{
	char *header = "HTTP/1.1 501 Method Not Implemented\r\nServer: %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n";
	char *body = "<html>\n<head><title>501 Method Not Implemented</title></head>\r\n<body bgcolor=\"white\">\r\n<center><h1>501 Method Not Implemented</h1></center>\r\n<hr><center>%s</center>\r\n</body>\n<html>\r\n";
    char buf[1024];
    bzero(buf, sizeof(buf));
    sprintf(buf, header, SERVER_STRING, strlen(body));
    send(client, buf, strlen(buf), 0);

    bzero(buf, sizeof(buf));
    sprintf(buf, body, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    close(client);
}

int startup(u_short *port, char multisocket)
{
    int sock_serv = 0;
    int opt = 1;
    struct sockaddr_in addr_serv;

    sock_serv = socket(PF_INET, SOCK_STREAM, 0);

    //set master socket to allow multiple connections , this is just a good habit, it will work without this
    if( setsockopt(sock_serv, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    if(multisocket == TRUE)
    {
        setnonblocking(sock_serv);
    }

    if (sock_serv == -1)
        error_die("socket");

    memset(&addr_serv, 0, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_port = htons(*port);
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock_serv, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)
        error_die("bind");

    if (listen(sock_serv, 20) < 0)
        error_die("listen");
    return sock_serv;
}

void error_die(const char *msg)
{
    perror(msg);
    exit(1);
}
