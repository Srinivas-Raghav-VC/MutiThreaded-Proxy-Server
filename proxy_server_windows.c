#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include "proxy_parse.h"

#pragma comment(lib, "ws2_32.lib")

#define bzero(ptr, size) memset(ptr, 0, size)
#define close closesocket
#define SHUT_RDWR SD_BOTH

typedef int socklen_t;

#define MAX_CLIENTS 10
#define MAX_BYTES 1024
#define MAX_CACHE_SIZE 200*(1 << 20)
#define MAX_ELEMENT_SIZE 10 * (1 << 10)

typedef struct cache_element {
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    struct cache_element* next;
} cache_element;

typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef struct {
    volatile LONG count;
    HANDLE event;
} sem_t;

int sem_init(sem_t* sem, int pshared, unsigned int value) {
    sem->count = value;
    sem->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    return (sem->event != NULL) ? 0 : -1;
}

int sem_wait(sem_t* sem) {
    if (InterlockedDecrement(&sem->count) < 0) {
        WaitForSingleObject(sem->event, INFINITE);
    }
    return 0;
}

int sem_post(sem_t* sem) {
    if (InterlockedIncrement(&sem->count) <= 0) {
        SetEvent(sem->event);
    }
    return 0;
}

int sem_getvalue(sem_t* sem, int* value) {
    *value = sem->count;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, void* attr) {
    InitializeCriticalSection(mutex);
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    EnterCriticalSection(mutex);
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    LeaveCriticalSection(mutex);
    return 0;
}

int pthread_create(pthread_t* thread, void* attr, unsigned int (__stdcall *start_routine)(void*), void* arg) {
    *thread = (HANDLE)_beginthreadex(NULL, 0, start_routine, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
}

int port_number = 8080;
SOCKET proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;
cache_element* head = NULL;
int cache_size = 0;

int init_winsock(void);
void cleanup_winsock(void);
int checkHTTPversion(char *msg);
int sendErrorMessage(SOCKET clientSocketId, int errorCode);
int sendHTTPSErrorMessage(SOCKET clientSocketId);
SOCKET connectRemoteServer(char* host_addr, int port_num);
int handle_request(SOCKET ClientSocketId, struct ParsedRequest* request, char* tempReq);
unsigned int __stdcall thread_fn(void *socketNew);
cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element(void);

int init_winsock(void) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return -1;
    }
    return 0;
}

void cleanup_winsock(void) {
    WSACleanup();
}

unsigned int __stdcall thread_fn(void *socketNew) {
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Clients: %d\n", MAX_CLIENTS - p);
    fflush(stdout);

    SOCKET socket = *(SOCKET*)socketNew;
    int bytes_received, len;

    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    if (!buffer) {
        sem_post(&semaphore);
        return 1;
    }

    bytes_received = recv(socket, buffer, MAX_BYTES - 1, 0);
    if (bytes_received == SOCKET_ERROR) {
        printf("recv failed: %d\n", WSAGetLastError());
        fflush(stdout);
        free(buffer);
        sem_post(&semaphore);
        return 1;
    }

    while (bytes_received > 0) {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL) {
            bytes_received = recv(socket, buffer + len, MAX_BYTES - len - 1, 0);
            if (bytes_received == SOCKET_ERROR) {
                break;
            }
        } else {
            break;
        }
    }

    if (bytes_received <= 0) {
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return 1;
    }

    char *tempReq = (char*)malloc(strlen(buffer) + 1);
    if (!tempReq) {
        printf("malloc failed\n");
        fflush(stdout);
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return 1;
    }
    strcpy(tempReq, buffer);

    cache_element* temp = find(tempReq);
    if (temp != NULL) {
        printf("[CACHE HIT] %s\n", temp->url);
        fflush(stdout);
        int size = temp->len;
        int pos = 0;
        while (pos < size) {
            int bytes_to_send = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
            int sent = send(socket, temp->data + pos, bytes_to_send, 0);
            if (sent == SOCKET_ERROR) {
                break;
            }
            pos += sent;
        }
    } else {
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, strlen(buffer)) < 0) {
            sendErrorMessage(socket, 400);
        } else {
            if (request->protocol && strcmp(request->protocol, "https") == 0) {
                sendHTTPSErrorMessage(socket);
                printf("[REJECT] HTTPS request: %s://%s%s\n",
                       request->protocol, request->host, request->path ? request->path : "/");
                fflush(stdout);
            }
            else if (strcmp(request->method, "GET") == 0) {
                if (request->host && request->path && checkHTTPversion(request->version) == 1) {
                    printf("[CACHE MISS] %s%s\n", request->host, request->path);
                    fflush(stdout);
                    handle_request(socket, request, tempReq);
                } else {
                    sendErrorMessage(socket, 400);
                }
            } else {
                sendErrorMessage(socket, 405);
            }
        }
        ParsedRequest_destroy(request);
    }

    closesocket(socket);
    free(buffer);
    free(tempReq);
    sem_post(&semaphore);
    return 0;
}

SOCKET connectRemoteServer(char* host_addr, int port_num) {
    SOCKET remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL) {
        printf("gethostbyname failed: %d\n", WSAGetLastError());
        closesocket(remoteSocket);
        return INVALID_SOCKET;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connect failed: %d\n", WSAGetLastError());
        closesocket(remoteSocket);
        return INVALID_SOCKET;
    }
    return remoteSocket;
}

int handle_request(SOCKET ClientSocketId, struct ParsedRequest* request, char* tempReq) {
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    if (!buffer) return -1;

    snprintf(buffer, MAX_BYTES, "GET %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n",
             request->path, request->version, request->host);

    int server_port = (request->port) ? atoi(request->port) : 80;

    SOCKET remoteSocket = connectRemoteServer(request->host, server_port);
    if (remoteSocket == INVALID_SOCKET) {
        free(buffer);
        return -1;
    }

    if (send(remoteSocket, buffer, strlen(buffer), 0) == SOCKET_ERROR) {
        free(buffer);
        closesocket(remoteSocket);
        return -1;
    }

    char *cache_buffer = (char*)malloc(MAX_CACHE_SIZE);
    int cache_index = 0;
    int bytes_received;

    do {
        memset(buffer, 0, MAX_BYTES);
        bytes_received = recv(remoteSocket, buffer, MAX_BYTES - 1, 0);
        if (bytes_received > 0) {
            send(ClientSocketId, buffer, bytes_received, 0);
            if (cache_index + bytes_received < MAX_CACHE_SIZE) {
                memcpy(cache_buffer + cache_index, buffer, bytes_received);
                cache_index += bytes_received;
            }
        }
    } while (bytes_received > 0);

    if (cache_index > 0) {
        add_cache_element(cache_buffer, cache_index, tempReq);
    }

    free(buffer);
    free(cache_buffer);
    closesocket(remoteSocket);
    return 0;
}

int checkHTTPversion(char *msg) {
    return (strncmp(msg, "HTTP/1.0", 8) == 0 || strncmp(msg, "HTTP/1.1", 8) == 0) ? 1 : -1;
}

int sendErrorMessage(SOCKET clientSocketId, int errorCode) {
    char response[1024];
    const char* status;
    switch (errorCode) {
        case 400: status = "400 Bad Request"; break;
        case 404: status = "404 Not Found"; break;
        case 405: status = "405 Method Not Allowed"; break;
        case 500: status = "500 Internal Server Error"; break;
        default: return -1;
    }
    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\nContent-Type: text/html\r\n\r\n<html><body><h2>%s</h2></body></html>",
        status, status);
    send(clientSocketId, response, strlen(response), 0);
    return 0;
}

int sendHTTPSErrorMessage(SOCKET clientSocketId) {
    char str[1024];
    snprintf(str, sizeof(str),
        "HTTP/1.1 501 Not Implemented\r\nContent-Type: text/html\r\n\r\n"
        "<html><body><h2>HTTPS Not Supported</h2>"
        "<p>This proxy does not support HTTPS requests.</p></body></html>");
    send(clientSocketId, str, strlen(str), 0);
    return 1;
}

int main(int argc, char* argv[]) {
    if (init_winsock() != 0) return 1;

    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    if (argc == 2) {
        port_number = atoi(argv[1]);
    } else {
        printf("Usage: %s <port>\n", argv[0]);
        fflush(stdout);
        cleanup_winsock();
        return 1;
    }

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        fflush(stdout);
        cleanup_winsock();
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        fflush(stdout);
        closesocket(proxy_socketId);
        cleanup_winsock();
        return 1;
    }

    if (listen(proxy_socketId, 5) == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        fflush(stdout);
        closesocket(proxy_socketId);
        cleanup_winsock();
        return 1;
    }

    printf("Proxy server listening on port %d\n", port_number);
    fflush(stdout);

    int i = 0;
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_socket = accept(proxy_socketId, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed: %d\n", WSAGetLastError());
            fflush(stdout);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Accepted new client: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        fflush(stdout);

        pthread_create(&tid[i], NULL, thread_fn, &client_socket);
        i = (i + 1) % MAX_CLIENTS;
    }

    closesocket(proxy_socketId);
    cleanup_winsock();
    return 0;
}

cache_element* find(char* url) {
    pthread_mutex_lock(&lock);
    cache_element* current = head;
    while (current != NULL) {
        if (strcmp(current->url, url) == 0) {
            current->lru_time_track = time(NULL);
            pthread_mutex_unlock(&lock);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

void remove_cache_element(void) {
    pthread_mutex_lock(&lock);
    if (head == NULL) {
        pthread_mutex_unlock(&lock);
        return;
    }

    cache_element* lru = head;
    cache_element* lru_prev = NULL;
    cache_element* current = head;
    cache_element* prev = NULL;

    while (current != NULL) {
        if (current->lru_time_track < lru->lru_time_track) {
            lru = current;
            lru_prev = prev;
        }
        prev = current;
        current = current->next;
    }

    if (lru_prev == NULL) {
        head = lru->next;
    } else {
        lru_prev->next = lru->next;
    }

    cache_size -= lru->len + sizeof(cache_element) + strlen(lru->url) + 1;
    free(lru->data);
    free(lru->url);
    free(lru);
    pthread_mutex_unlock(&lock);
}

int add_cache_element(char* data, int size, char* url) {
    if (size > MAX_ELEMENT_SIZE) {
        return 0;
    }

    pthread_mutex_lock(&lock);
    while (cache_size + size > MAX_CACHE_SIZE) {
        pthread_mutex_unlock(&lock);
        remove_cache_element();
        pthread_mutex_lock(&lock);
    }

    cache_element* element = (cache_element*)malloc(sizeof(cache_element));
    if (!element) {
        pthread_mutex_unlock(&lock);
        return 0;
    }

    element->data = (char*)malloc(size);
    element->url = (char*)malloc(strlen(url) + 1);
    if (!element->data || !element->url) {
        free(element->data);
        free(element->url);
        free(element);
        pthread_mutex_unlock(&lock);
        return 0;
    }

    memcpy(element->data, data, size);
    strcpy(element->url, url);
    element->len = size;
    element->lru_time_track = time(NULL);
    element->next = head;
    head = element;
    cache_size += size + sizeof(cache_element) + strlen(url) + 1;
    pthread_mutex_unlock(&lock);
    return 1;
}
