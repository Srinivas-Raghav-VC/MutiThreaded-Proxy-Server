#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "proxy_parse.h"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.hh>
    #pragma comment(lib, "ws2_32.lib")

    #define bzero(ptr, size) memset(ptr, 0, size)
    #define close closesocket
    #define SHUT_RDWR SD_BOTH

    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <pthread.h>
    #include <semaphore.h>
    #include <sys/stat.h>
    #include <fcntl.h>

    #ifndef bzero
    #define bzero(ptr, size) memset(ptr, 0, size)
    #endif
#endif

typedef struct cache_element cache_element;

struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next;
};

int checkHTTPversion(char *msg);
int sendErrorMessage(int clientSocketId, int errorCode);
int sendHTTPSErrorMessage(int clientSocketId);
int connectRemoteServer(char* host_addr, int port_num);
int handle_request(int ClientSocketId, struct ParsedRequest* request, char* tempReq);
void* thread_fn(void *socketNew);

cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();

#define MAX_CLIENTS 10
#define MAX_BYTES 1024
#define MAX_CACHE_SIZE 200*(1 << 20)
#define MAX_ELEMENT_SIZE 10 * (1 << 10)

#ifdef _WIN32
    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    typedef struct { volatile int count; HANDLE event; } sem_t;

    static inline int sem_init(sem_t* sem, int pshared, unsigned int value) {
        sem->count = value;
        sem->event = CreateEvent(NULL, FALSE, FALSE, NULL);
        return 0;
    }

    static inline int sem_wait(sem_t* sem) {
        if (InterlockedDecrement(&sem->count) < 0) {
            WaitForSingleObject(sem->event, INFINITE);
        }
        return 0;
    }

    static inline int sem_post(sem_t* sem) {
        if (InterlockedIncrement(&sem->count) <= 0) {
            SetEvent(sem->event);
        }
        return 0;
    }

    static inline int sem_getvalue(sem_t* sem, int* value) {
        *value = sem->count;
        return 0;
    }

    static inline int pthread_mutex_init(pthread_mutex_t* mutex, void* attr) {
        InitializeCriticalSection(mutex);
        return 0;
    }

    static inline int pthread_mutex_lock(pthread_mutex_t* mutex) {
        EnterCriticalSection(mutex);
        return 0;
    }

    static inline int pthread_mutex_unlock(pthread_mutex_t* mutex) {
        LeaveCriticalSection(mutex);
        return 0;
    }

    static inline int pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
        *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
        return (*thread == NULL) ? -1 : 0;
    }
#endif

int port_number = 8080;
int proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element* head;
int cache_size;

void* thread_fn(void *socketNew){
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Clients: %d\n", MAX_CLIENTS - p);
    fflush(stdout);

    int *t = (int*)socketNew;
    int socket = *t;
    int bytes_send_client, len;

    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while(bytes_send_client > 0){
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }else{
            break;
        }
    }

    char *tempReq = (char*)malloc((strlen(buffer) + 1) * sizeof(char));
    strcpy(tempReq, buffer);

    struct cache_element* temp = find(tempReq);
    if (temp != NULL){
        printf("[CACHE HIT] %s\n", temp->url);
        fflush(stdout);
        int size = temp->len;
        int pos = 0;
        char response[MAX_BYTES];

        while(pos < size){
            bzero(response, MAX_BYTES);
            int bytes_to_send = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
            memcpy(response, temp->data + pos, bytes_to_send);
            send(socket, response, bytes_to_send, 0);
            pos += bytes_to_send;
        }
    }else if(bytes_send_client > 0){
        len = strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, len) < 0){
            perror("Request Parse Error");
        }else{
            if (request->protocol && !strcmp(request->protocol, "https")){
                sendHTTPSErrorMessage(socket);
                printf("[REJECT] HTTPS request: %s://%s%s\n",
                       request->protocol, request->host, request->path ? request->path : "/");
                fflush(stdout);
            }
            else if (!strcmp(request->method, "GET")){
                if (request->host && request->path && checkHTTPversion(request->version) == 1){
                    printf("[CACHE MISS] %s%s\n", request->host, request->path);
                    fflush(stdout);
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket, 500);
                    }
                }else{
                    sendErrorMessage(socket, 400);
                }
            }else{
                sendErrorMessage(socket, 405);
            }
            ParsedRequest_destroy(request);
        }
    }else if(bytes_send_client == 0){
        printf("Client disconnected\n");
        fflush(stdout);
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);

    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    free(tempReq);
    return NULL;
}

int connectRemoteServer(char* host_addr , int port_num){
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0){
        perror("Socket Creation Error");
        return -1;
    }

    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL){
        perror("DNS Error");
        return -1;
    }

    struct sockaddr_in server_addr;
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Connection Error");
        return -1;
    }
    return remoteSocket;
}

int handle_request(int ClientSocketId, struct ParsedRequest* request, char* tempReq){
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    strcpy(buffer, "GET ");
    strcat(buffer, request->path);
    strcat(buffer, " ");
    strcat(buffer, request->version);
    strcat(buffer, "\r\n");
    strcat(buffer, "Host: ");
    strcat(buffer, request->host);
    strcat(buffer, "\r\n");
    strcat(buffer, "Connection: close\r\n");
    strcat(buffer, "\r\n");

    size_t len = strlen(buffer);

    if(ParsedHeader_set(request,"Connection", "close") < 0){
        printf("Failed to set connection header\n");
        fflush(stdout);
    }

    if (ParsedHeader_get(request, "Host") != NULL){
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Failed to set host header\n");
            fflush(stdout);
        }
    }

    if (ParsedRequest_unparse_headers(request, buffer + len , (size_t)(MAX_BYTES - len)) < 0){
        printf("Header unparse error\n");
        fflush(stdout);
    }

    int server_port = 80;
    if (request->port != NULL){
        server_port = atoi(request->port);
    }

    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if (remoteSocketId < 0){
        printf("Failed to connect to remote server\n");
        fflush(stdout);
        return -1;
    }

    int bytes_sent = send(remoteSocketId, buffer, strlen(buffer), 0);
    bzero(buffer, MAX_BYTES);

    bytes_sent = recv(remoteSocketId, buffer, MAX_BYTES - 1, 0);
    char *temp_buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while(bytes_sent > 0){
        send(ClientSocketId, buffer, bytes_sent, 0);
        memcpy(temp_buffer + temp_buffer_index, buffer, bytes_sent);
        temp_buffer_index += bytes_sent;

        if (temp_buffer_size - temp_buffer_index < MAX_BYTES) {
            temp_buffer_size *= 2;
            temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
        }

        if (bytes_sent < 0){
            perror("Response sending error");
            break;
        }
        bzero(buffer, MAX_BYTES);
        bytes_sent = recv(remoteSocketId, buffer, MAX_BYTES - 1, 0);
    }

    if (bytes_sent < 0){
        perror("Response receiving error");
        return -1;
    }

    add_cache_element(temp_buffer, temp_buffer_index, tempReq);

    free(buffer);
    free(temp_buffer);
    close(remoteSocketId);
    return 0;
}

int checkHTTPversion(char *msg){
    if (strncmp(msg, "HTTP/1.0", 8) == 0 || strncmp(msg, "HTTP/1.1", 8) == 0){
        return 1;
    }
    return -1;
}

int sendErrorMessage(int clientSocketId, int errorCode){
    char str[1024];
    char currentTime[50];
    time_t now = time(NULL);

    struct tm data = *localtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch(errorCode){
        case 400:
            snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>400 Bad Request</h2></body></html>", currentTime);
            break;
        case 404:
            snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>404 Not Found</h2></body></html>", currentTime);
            break;
        case 500:
            snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>500 Internal Server Error</h2></body></html>", currentTime);
            break;
        case 403:
            snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>403 Forbidden</h2></body></html>", currentTime);
            break;
        case 405:
            snprintf(str, sizeof(str), "HTTP/1.1 405 Method Not Allowed\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>405 Method Not Allowed</h2></body></html>", currentTime);
            break;
        case 408:
            snprintf(str, sizeof(str), "HTTP/1.1 408 Request Timeout\r\nDate: %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>408 Request Timeout</h2></body></html>", currentTime);
            break;
        default:
            return -1;
    }
    send(clientSocketId, str, strlen(str), 0);
    return 1;
}

int sendHTTPSErrorMessage(int clientSocketId){
    char str[1024];
    char currentTime[50];
    time_t now = time(NULL);

    struct tm data = *localtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    snprintf(str, sizeof(str),
        "HTTP/1.1 501 Not Implemented\r\n"
        "Date: %s\r\n"
        "Content-Type: text/html\r\n"
        "\r\n"
        "<html><head><title>HTTPS Not Supported</title></head>"
        "<body><h2>HTTPS Not Supported</h2>"
        "<p>This proxy server does not support HTTPS requests.</p>"
        "</body></html>",
        currentTime);

    send(clientSocketId, str, strlen(str), 0);
    return 1;
}

int main(int argc, char* argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;

    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    if (argc == 2){
        port_number = atoi(argv[1]);
    }else{
        printf("Usage: %s <port>\n", argv[0]);
        fflush(stdout);
        exit(1);
    }

    printf("Proxy server starting on port %d...\n", port_number);
    fflush(stdout);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0){
        perror("Socket creation failed");
        exit(1);
    }

    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(1);
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Port binding failed");
        exit(1);
    }

    if (listen(proxy_socketId, MAX_CLIENTS) < 0){
        perror("Listen failed");
        exit(1);
    }
    printf("Listening on port %d\n", port_number);
    fflush(stdout);

    int i = 0;
    while (1){
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if (client_socketId < 0){
            perror("Client accept failed");
            continue;
        }

        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, ip_str, INET_ADDRSTRLEN);
        printf("Accepted new client: %s:%d\n", ip_str, ntohs(client_pt->sin_port));
        fflush(stdout);

        pthread_create(&tid[i], NULL, thread_fn, (void*)&client_socketId);
        i = (i + 1) % MAX_CLIENTS;
    }

    close(proxy_socketId);
    return 0;
}

cache_element* find(char* url){
    cache_element* site = NULL;
    pthread_mutex_lock(&lock);

    if(head != NULL){
        site = head;
        while(site != NULL){
            if(!strcmp(site->url, url)){
                site->lru_time_track = time(NULL);
                break;
            }
            site = site->next;
        }
    }

    pthread_mutex_unlock(&lock);
    return site;
}

void remove_cache_element(){
    cache_element *p, *q, *temp, *prev;
    pthread_mutex_lock(&lock);

    if (head != NULL){
        for (p = head, q = head, temp = head; p->next != NULL; p = p->next) {
            if (p->next->lru_time_track < temp->lru_time_track) {
                temp = p->next;
                q = p;
            }
        }

        if (temp == head) {
            head = temp->next;
        } else {
            q->next = temp->next;
        }

        cache_size -= temp->len + sizeof(cache_element) + strlen(temp->url) + 1;
        free(temp->data);
        free(temp->url);
        free(temp);
    }
    pthread_mutex_unlock(&lock);
}

int add_cache_element(char* data, int size, char* url){
    pthread_mutex_lock(&lock);
    int element_size = size;

    if (element_size > MAX_ELEMENT_SIZE){
        pthread_mutex_unlock(&lock);
        return 0;
    }

    while(cache_size + element_size > MAX_CACHE_SIZE){
        pthread_mutex_unlock(&lock);
        remove_cache_element();
        pthread_mutex_lock(&lock);
    }

    cache_element* element = (cache_element*)malloc(sizeof(cache_element));
    element->data = (char*)malloc(element_size);
    memcpy(element->data, data, element_size);
    element->url = (char*)malloc((strlen(url)+1)*sizeof(char));
    strcpy(element->url, url);
    element->lru_time_track = time(NULL);
    element->next = head;
    element->len = element_size;

    head = element;
    cache_size += element->len + sizeof(cache_element) + strlen(element->url) + 1;
    pthread_mutex_unlock(&lock);
    return 1;
}
