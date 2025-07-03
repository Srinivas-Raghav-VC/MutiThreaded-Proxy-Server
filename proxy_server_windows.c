/*
 * ================================================================================
 * HTTP PROXY SERVER WITH LRU CACHE - WINDOWS VERSION
 * ================================================================================
 *
 * Learning Log: Windows vs Unix Network Programming
 * =================================================
 *
 * This Windows-specific implementation demonstrates how network programming
 * differs across platforms while maintaining the same core functionality.
 *
 * Key Windows Differences:
 * - Uses Winsock2 (winsock2.h) instead of BSD sockets (sys/socket.h)
 * - Requires WSAStartup() initialization and WSACleanup()
 * - Uses _beginthreadex() instead of pthread_create()
 * - Error handling with WSAGetLastError() instead of errno
 * - Different socket types (SOCKET instead of int)
 *
 * Educational Value:
 * - Shows cross-platform programming techniques
 * - Demonstrates Windows threading model
 * - Provides working proxy server for Windows development
 *
 * Compilation: gcc -o proxy_windows.exe proxy_server_windows.c proxy_parse.c -lws2_32
 * Usage: ./proxy_windows.exe 8080
 * ================================================================================
 */

// Standard C library includes (platform independent)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Windows-specific networking includes
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

// Link Windows socket library
#pragma comment(lib, "ws2_32.lib")

// HTTP parsing library (from proxy_parse.h)
#include "proxy_parse.h"

/*
 * ================================================================================
 * WINDOWS COMPATIBILITY LAYER
 * ================================================================================
 *
 * Learning Log: Cross-Platform Compatibility
 * ==========================================
 *
 * To make networking code portable, we define Windows equivalents
 * for POSIX functions that aren't available on Windows.
 */

// Function name compatibility
#define bzero(ptr, size) memset(ptr, 0, size)
#define close closesocket
#define SHUT_RDWR SD_BOTH

// Type compatibility
typedef int socklen_t;

/*
 * ================================================================================
 * CONFIGURATION CONSTANTS
 * ================================================================================
 */
#define MAX_CLIENTS 10                    // Maximum concurrent client connections
#define MAX_BYTES 1024                    // Buffer size for network I/O
#define MAX_CACHE_SIZE 200*(1 << 20)      // Maximum total cache size (200MB)
#define MAX_ELEMENT_SIZE 10 * (1 << 10)   // Maximum size per cache element (10KB)

/*
 * ================================================================================
 * CACHE ELEMENT STRUCTURE
 * ================================================================================
 */
typedef struct cache_element {
    char* data;                           // Complete HTTP response data
    int len;                              // Length of data in bytes
    char* url;                            // Request URL (cache key)
    time_t lru_time_track;                // Timestamp for LRU policy
    struct cache_element* next;           // Next element in linked list
} cache_element;

/*
 * ================================================================================
 * WINDOWS THREADING IMPLEMENTATION
 * ================================================================================
 *
 * Learning Log: Windows vs POSIX Threading
 * ========================================
 *
 * Windows doesn't have POSIX threads (pthread) natively.
 * We implement our own compatibility layer using Windows threading APIs.
 */

// Windows thread and synchronization types
typedef HANDLE pthread_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef struct {
    volatile LONG count;
    HANDLE event;
} sem_t;

// Semaphore implementation using Windows events
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

// Mutex implementation using Windows critical sections
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

// Thread creation using Windows _beginthreadex
int pthread_create(pthread_t* thread, void* attr, unsigned int (__stdcall *start_routine)(void*), void* arg) {
    *thread = (HANDLE)_beginthreadex(NULL, 0, start_routine, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
}

/*
 * ================================================================================
 * GLOBAL VARIABLES
 * ================================================================================
 */
int port_number = 8080;
SOCKET proxy_socketId;
pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;
cache_element* head = NULL;
int cache_size = 0;

/*
 * ================================================================================
 * FUNCTION DECLARATIONS
 * ================================================================================
 */
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

/*
 * ================================================================================
 * WINSOCK INITIALIZATION
 * ================================================================================
 *
 * Learning Log: Windows Socket Initialization
 * ===========================================
 *
 * Unlike UNIX systems, Windows requires explicit initialization of the
 * Winsock library before any socket operations can be performed.
 */
int init_winsock(void) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return -1;
    }
    printf("Winsock initialized successfully\n");
    return 0;
}

void cleanup_winsock(void) {
    WSACleanup();
    printf("Winsock cleaned up\n");
}

/*
 * ================================================================================
 * CLIENT THREAD HANDLING
 * ================================================================================
 *
 * Learning Log: Thread-Based Client Handling
 * ==========================================
 *
 * Each client connection is handled in a separate thread to allow
 * concurrent processing of multiple requests. This demonstrates:
 * - Windows threading with _beginthreadex
 * - Semaphore-based connection limiting
 * - HTTP request/response processing
 * - Cache lookup and management
 */
unsigned int __stdcall thread_fn(void *socketNew) {
    // Limit concurrent connections using semaphore
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Active clients: %d\n", MAX_CLIENTS - p);

    // Extract socket from parameter
    SOCKET socket = *(SOCKET*)socketNew;
    int bytes_received, len;

    // Allocate buffer for HTTP request
    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    if (!buffer) {
        printf("Memory allocation failed\n");
        sem_post(&semaphore);
        return 1;
    }

    // Receive initial HTTP request data
    bytes_received = recv(socket, buffer, MAX_BYTES - 1, 0);
    if (bytes_received == SOCKET_ERROR) {
        printf("recv failed: %d\n", WSAGetLastError());
        free(buffer);
        sem_post(&semaphore);
        return 1;
    }

    // Read complete HTTP request (until \r\n\r\n)
    while (bytes_received > 0) {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL) {
            bytes_received = recv(socket, buffer + len, MAX_BYTES - len - 1, 0);
            if (bytes_received == SOCKET_ERROR) {
                printf("recv failed: %d\n", WSAGetLastError());
                break;
            }
        } else {
            break;
        }
    }

    if (bytes_received <= 0) {
        printf("Client disconnected or error occurred\n");
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return 1;
    }

    // Create copy of request for cache lookup
    char *tempReq = (char*)malloc(strlen(buffer) + 1);
    if (!tempReq) {
        printf("Memory allocation failed\n");
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return 1;
    }
    strcpy(tempReq, buffer);

    // Check cache for existing response
    cache_element* temp = find(tempReq);
    if (temp != NULL) {
        // Cache HIT: Serve from cache
        printf("Cache HIT: %s\n", temp->url);
        int size = temp->len;
        int pos = 0;

        // Send cached data in chunks
        while (pos < size) {
            int bytes_to_send = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
            int sent = send(socket, temp->data + pos, bytes_to_send, 0);
            if (sent == SOCKET_ERROR) {
                printf("send failed: %d\n", WSAGetLastError());
                break;
            }
            pos += sent;
        }
        printf("Served %d bytes from cache\n", size);

    } else if (bytes_received > 0) {
        // Cache MISS: Forward to target server
        printf("Cache MISS: Processing new request\n");

        // Parse HTTP request
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, strlen(buffer)) < 0) {
            printf("Failed to parse HTTP request\n");
            sendErrorMessage(socket, 400);
        } else {
            // Check if the request is for HTTPS (not supported by this HTTP proxy)
            if (request->protocol && strcmp(request->protocol, "https") == 0) {
                sendHTTPSErrorMessage(socket);
                printf("HTTPS Request Rejected: %s://%s%s\n",
                       request->protocol, request->host, request->path ? request->path : "/");
            }
            // Validate and process GET request
            else if (strcmp(request->method, "GET") == 0) {
                if (request->host && request->path && checkHTTPversion(request->version) == 1) {
                    handle_request(socket, request, tempReq);
                } else {
                    sendErrorMessage(socket, 400);
                    printf("Invalid request parameters\n");
                }
            } else {
                sendErrorMessage(socket, 405);
                printf("Method not supported: %s\n", request->method);
            }
        }
        ParsedRequest_destroy(request);
    }

    // Cleanup resources
    closesocket(socket);
    free(buffer);
    free(tempReq);
    sem_post(&semaphore);

    printf("Client thread terminated\n");
    return 0;
}

/*
 * ================================================================================
 * REMOTE SERVER CONNECTION
 * ================================================================================
 */
SOCKET connectRemoteServer(char* host_addr, int port_num) {
    // Create TCP socket
    SOCKET remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return INVALID_SOCKET;
    }

    // Resolve hostname to IP address
    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL) {
        printf("gethostbyname failed: %d\n", WSAGetLastError());
        closesocket(remoteSocket);
        return INVALID_SOCKET;
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // Connect to server
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connect failed: %d\n", WSAGetLastError());
        closesocket(remoteSocket);
        return INVALID_SOCKET;
    }

    return remoteSocket;
}

/*
 * ================================================================================
 * HTTP REQUEST HANDLING
 * ================================================================================
 */
int handle_request(SOCKET ClientSocketId, struct ParsedRequest* request, char* tempReq) {
    // Build HTTP request for target server
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    if (!buffer) return -1;

    snprintf(buffer, MAX_BYTES, "GET %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n",
             request->path, request->version, request->host);

    // Determine target port
    int server_port = (request->port) ? atoi(request->port) : 80;

    // Connect to target server
    SOCKET remoteSocket = connectRemoteServer(request->host, server_port);
    if (remoteSocket == INVALID_SOCKET) {
        free(buffer);
        return -1;
    }

    // Send request to target server
    if (send(remoteSocket, buffer, strlen(buffer), 0) == SOCKET_ERROR) {
        printf("Failed to send request: %d\n", WSAGetLastError());
        free(buffer);
        closesocket(remoteSocket);
        return -1;
    }

    // Receive and forward response
    char *cache_buffer = (char*)malloc(MAX_CACHE_SIZE);
    int cache_index = 0;
    int bytes_received;

    do {
        memset(buffer, 0, MAX_BYTES);
        bytes_received = recv(remoteSocket, buffer, MAX_BYTES - 1, 0);

        if (bytes_received > 0) {
            // Forward to client
            send(ClientSocketId, buffer, bytes_received, 0);

            // Store for caching
            if (cache_index + bytes_received < MAX_CACHE_SIZE) {
                memcpy(cache_buffer + cache_index, buffer, bytes_received);
                cache_index += bytes_received;
            }
        }
    } while (bytes_received > 0);

    // Cache the complete response
    if (cache_index > 0) {
        add_cache_element(cache_buffer, cache_index, tempReq);
        printf("Cached response: %d bytes\n", cache_index);
    }

    // Cleanup
    free(buffer);
    free(cache_buffer);
    closesocket(remoteSocket);
    return 0;
}

/*
 * ================================================================================
 * UTILITY FUNCTIONS
 * ================================================================================
 */
int checkHTTPversion(char *msg) {
    return (strncmp(msg, "HTTP/1.0", 8) == 0 || strncmp(msg, "HTTP/1.1", 8) == 0) ? 1 : -1;
}

int sendErrorMessage(SOCKET clientSocketId, int errorCode) {
    char response[1024];
    const char* status;
    const char* message;

    switch (errorCode) {
        case 400:
            status = "400 Bad Request";
            message = "Bad Request";
            break;
        case 404:
            status = "404 Not Found";
            message = "Not Found";
            break;
        case 405:
            status = "405 Method Not Allowed";
            message = "Method Not Allowed";
            break;
        case 500:
            status = "500 Internal Server Error";
            message = "Internal Server Error";
            break;
        default:
            return -1;
    }

    snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "<html><body><h1>%s</h1></body></html>",
        status, (int)(strlen(message) + 27), message);

    send(clientSocketId, response, strlen(response), 0);
    return 0;
}

/*
 * Send HTTPS Not Supported Error Message (Windows Version)
 *
 * Sends a clear explanation that this proxy only supports HTTP, not HTTPS.
 * This prevents the confusing "too many redirections" error that users
 * encounter when trying to access HTTPS sites through an HTTP proxy.
 *
 * Parameters:
 * - clientSocketId: SOCKET descriptor for the client connection
 *
 * Returns:
 * - 1 on success
 * - -1 on failure
 */
int sendHTTPSErrorMessage(SOCKET clientSocketId){
    char str[2048];
    char currentTime[50];
    time_t now = time(NULL);

    // Get current timestamp for HTTP Date header
    struct tm data = *localtime(&now);
    strftime(currentTime, sizeof(currentTime), "%Y-%m-%d %H:%M:%S", &data);

    // Create comprehensive HTTPS error response
    snprintf(str, sizeof(str),
        "HTTP/1.1 501 Not Implemented\r\n"
        "Date: %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 800\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><head><title>HTTPS Not Supported</title></head>"
        "<body style='font-family:Arial; margin:50px; background:#f5f5f5;'>"
        "<div style='background:white; padding:30px; border-radius:10px; box-shadow:0 2px 10px rgba(0,0,0,0.1);'>"
        "<h1 style='color:#d73027; margin-top:0;'>🔒 HTTPS Not Supported</h1>"
        "<p><strong>This is an HTTP-only proxy server.</strong></p>"
        "<p>You tried to access an <code>https://</code> URL, but this proxy only supports <code>http://</code> URLs.</p>"
        "<h3>📋 What You Can Do:</h3>"
        "<ul>"
        "<li>Use <code>http://</code> URLs instead of <code>https://</code></li>"
        "<li>Try: <code>http://httpbin.org/json</code> for testing</li>"
        "<li>Try: <code>http://example.com</code> for a simple webpage</li>"
        "</ul>"
        "<h3>💡 Why This Happens:</h3>"
        "<p>HTTPS requires SSL/TLS encryption that this educational proxy doesn't implement.</p>"
        "</div></body></html>",
        currentTime);

    printf("HTTPS Request Blocked - Educational HTTP Proxy\n");
    send(clientSocketId, str, strlen(str), 0);
    return 1;
}

/*
 * ================================================================================
 * MAIN FUNCTION
 * ================================================================================
 */
int main(int argc, char* argv[]) {
    printf("HTTP Proxy Server with LRU Cache - Windows Version\n");
    printf("===================================================\n");

    // Initialize Winsock
    if (init_winsock() != 0) {
        return 1;
    }

    // Initialize synchronization
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    // Parse command line
    if (argc == 2) {
        port_number = atoi(argv[1]);
    } else {
        printf("Usage: %s <port>\n", argv[0]);
        printf("Example: %s 8080\n", argv[0]);
        cleanup_winsock();
        return 1;
    }

    // Create server socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        cleanup_winsock();
        return 1;
    }

    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind to port
    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(proxy_socketId);
        cleanup_winsock();
        return 1;
    }

    // Start listening
    if (listen(proxy_socketId, 5) == SOCKET_ERROR) {
        printf("Listen failed: %d\n", WSAGetLastError());
        closesocket(proxy_socketId);
        cleanup_winsock();
        return 1;
    }

    printf("Proxy server listening on port %d\n", port_number);
    printf("Configure your browser to use 127.0.0.1:%d as HTTP proxy\n\n", port_number);

    // Accept connections
    int i = 0;
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_socket = accept(proxy_socketId, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed: %d\n", WSAGetLastError());
            continue;
        }

        // Log client info
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("New client: %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // Create thread for client
        pthread_create(&tid[i], NULL, thread_fn, &client_socket);
        i = (i + 1) % MAX_CLIENTS;
    }

    // Cleanup (never reached)
    closesocket(proxy_socketId);
    cleanup_winsock();
    return 0;
}

/*
 * ================================================================================
 * LRU CACHE IMPLEMENTATION
 * ================================================================================
 */
cache_element* find(char* url) {
    pthread_mutex_lock(&lock);

    cache_element* current = head;
    while (current != NULL) {
        if (strcmp(current->url, url) == 0) {
            current->lru_time_track = time(NULL);  // Update access time
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

    // Find LRU element
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

    // Remove LRU element
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

    // Remove elements if cache is full
    while (cache_size + size + sizeof(cache_element) + strlen(url) + 1 > MAX_CACHE_SIZE) {
        pthread_mutex_unlock(&lock);
        remove_cache_element();
        pthread_mutex_lock(&lock);
    }

    // Create new element
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
