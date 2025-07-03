/*
 * ================================================================================
 * HTTP PROXY SERVER WITH LRU CACHE IMPLEMENTATION
 * ================================================================================
 *
 * Learning Log: What is an HTTP Proxy Server?
 * ===========================================
 *
 * An HTTP Proxy Server acts as an intermediary between clients (web browsers) and
 * target servers (websites). Think of it as a "middleman" that:
 *
 * 1. CLIENT PERSPECTIVE: Browser thinks it's talking to the real website
 * 2. SERVER PERSPECTIVE: Website thinks it's talking to a regular browser
 * 3. PROXY PERSPECTIVE: We intercept, process, and forward all communication
 *
 * Data Flow:
 * [Browser] ↔ [Our Proxy] ↔ [Website Server]
 *
 * Why Use a Proxy?
 * - CACHING: Store popular responses to serve them faster
 * - FILTERING: Block unwanted content or websites
 * - LOGGING: Monitor and record web traffic
 * - LOAD BALANCING: Distribute requests across multiple servers
 * - ANONYMITY: Hide client IP addresses from target servers
 *
 * This implementation focuses on CACHING using LRU (Least Recently Used) policy.
 *
 * ================================================================================
 * PROGRAM ARCHITECTURE OVERVIEW
 * ================================================================================
 *
 * Our proxy server implements:
 *
 * 1. MULTI-THREADED CLIENT HANDLING
 *    - Main thread accepts new client connections
 *    - Each client gets a dedicated worker thread
 *    - Semaphore limits concurrent connections (prevents server overload)
 *
 * 2. LRU CACHE SYSTEM
 *    - Stores HTTP responses in memory for fast serving
 *    - Evicts oldest unused entries when cache is full
 *    - Thread-safe operations using mutex locks
 *
 * 3. HTTP PROTOCOL SUPPORT
 *    - Parses HTTP GET requests (HTTP/1.0 and HTTP/1.1)
 *    - Forwards requests to target servers
 *    - Handles HTTP response streaming
 *    - Supports proper error codes (400, 404, 500, etc.)
 *
 * 4. NETWORK PROGRAMMING
 *    - TCP socket creation and management
 *    - DNS resolution for target servers
 *    - Real-time data streaming between client and server
 *
 * ================================================================================
 * USAGE INSTRUCTIONS
 * ================================================================================
 *
 * Compilation: make
 * Usage: ./proxy <port_number>
 * Example: ./proxy 8080
 *
 * Then configure your browser to use 127.0.0.1:8080 as HTTP proxy.
 *
 * ================================================================================
 * CREDITS AND ACKNOWLEDGMENTS
 * ================================================================================
 *
 * This implementation builds upon and integrates with:
 *
 * 1. proxy_parse.h & proxy_parse.c:
 *    - HTTP Request Parsing Library
 *    - Written by: Matvey Arye (COS 518)
 *    - Provides ParsedRequest structure and parsing functions
 *    - Handles HTTP header manipulation and request formatting
 *
 * 2. Standard UNIX/Linux System Libraries:
 *    - pthread.h: POSIX threading for multi-client support
 *    - sys/socket.h: Berkeley sockets for network communication
 *    - netdb.h: Network database operations (DNS resolution)
 *    - semaphore.h: POSIX semaphores for connection limiting
 *
 * 3. Educational Foundation:
 *    - Based on computer networking and systems programming principles
 *    - Implements fundamental concepts from "Computer Networks" coursework
 *    - Demonstrates real-world application of socket programming
 *
 * Author: [Enhanced with comprehensive learning logs and documentation]
 * Date: [Current Date]
 * Course: Computer Networks / Systems Programming
 * ================================================================================
 */

/*
 * ================================================================================
 * SYSTEM INCLUDES AND DEPENDENCIES
 * ================================================================================
 *
 * Learning Log: Why These Specific Includes?
 * ==========================================
 *
 * Each include serves a specific purpose in our networking application:
 */

/*
 * ================================================================================
 * STANDARD C LIBRARY INCLUDES (Platform Independent)
 * ================================================================================
 */
#include <stdio.h>          // Standard I/O: printf, perror, etc.
#include <stdlib.h>         // Memory management: malloc, free, exit
#include <string.h>         // String operations: strlen, strcmp, strcpy
#include <time.h>           // Time functions: time() for LRU timestamps
#include <errno.h>          // Error handling: Access to errno variable

/*
 * Learning Log: Our Custom HTTP Parsing Library
 * =============================================
 *
 * This include brings in the ParsedRequest functionality:
 * - Structures: ParsedRequest, ParsedHeader
 * - Functions: ParsedRequest_create(), ParsedRequest_parse()
 * - Header manipulation: ParsedHeader_set(), ParsedHeader_get()
 *
 * Credit: Written by Matvey Arye for COS 518
 */
#include "proxy_parse.h"

/*
 * ================================================================================
 * PLATFORM-SPECIFIC SYSTEM INCLUDES
 * ================================================================================
 *
 * Learning Log: Cross-Platform Network Programming
 * ===============================================
 *
 * Network programming differs significantly between platforms:
 *
 * WINDOWS (Winsock):
 * - Uses winsock2.h instead of sys/socket.h
 * - Requires WSAStartup() initialization
 * - Functions like closesocket() instead of close()
 * - Links against ws2_32.lib library
 * - Different error handling (WSAGetLastError vs errno)
 *
 * UNIX/LINUX/MACOS (BSD Sockets):
 * - Uses standard POSIX socket headers
 * - Functions like close(), bzero() available
 * - No special initialization required
 * - Standard errno error handling
 *
 * We provide compatibility layer for both platforms.
 */

#ifdef _WIN32
    /*
     * WINDOWS PLATFORM SETUP
     * ======================
     */
    #include <winsock2.h>       // Windows socket programming
    #include <ws2tcpip.h>       // Additional TCP/IP functions
    #include <windows.h>        // Windows API functions
    #pragma comment(lib, "ws2_32.lib")  // Link Windows socket library

    // Windows doesn't have these POSIX functions, so we define them
    #define bzero(ptr, size) memset(ptr, 0, size)
    #define close closesocket
    #define SHUT_RDWR SD_BOTH

    // Windows uses different socket types
    typedef int socklen_t;

#else
    /*
     * UNIX/LINUX/MACOS PLATFORM SETUP
     * ================================
     */
    #include <sys/socket.h>     // Socket creation: socket(), bind(), listen()
    #include <sys/types.h>      // Data types: socklen_t, ssize_t, etc.
    #include <netinet/in.h>     // Internet address family: sockaddr_in
    #include <arpa/inet.h>      // IP conversion: inet_ntop(), htons()
    #include <netdb.h>          // Network database: gethostbyname()
    #include <unistd.h>         // UNIX standard: close(), read(), write()
    #include <pthread.h>        // POSIX threads: pthread_create()
    #include <semaphore.h>      // POSIX semaphores: sem_init(), sem_wait()
    #include <sys/stat.h>       // File status information
    #include <fcntl.h>          // File control operations

    // Some systems don't have bzero, provide fallback
    #ifndef bzero
    #define bzero(ptr, size) memset(ptr, 0, size)
    #endif

#endif

/*
 * ================================================================================
 * PLATFORM-SPECIFIC COMPATIBILITY FIXES
 * ================================================================================
 *
 * Learning Log: Cross-Platform Development Challenges
 * ===================================================
 *
 * Different operating systems provide networking functions through different headers.
 * We need to handle these differences to ensure our code compiles everywhere.
 *
 * UNIX/Linux: Functions are in standard POSIX headers
 * Windows: Functions may need Winsock2 library (ws2_32.lib)
 * macOS: Similar to Linux but some differences in socket options
 */

/*
 * Platform-specific compatibility has been moved to the includes section above.
 * This ensures proper organization and prevents duplicate definitions.
 */

/*
 * ================================================================================
 * CACHE ELEMENT STRUCTURE DEFINITION
 * ================================================================================
 *
 * Learning Log: Why Define Structures First?
 * ==========================================
 *
 * In C, we must define structures before we can use them in function declarations.
 * The cache_element structure represents a single cached HTTP response.
 */

// Forward declaration for the cache element structure
typedef struct cache_element cache_element;

/*
 * Cache Element Structure
 *
 * Learning Log: HTTP Response Caching Design
 * ==========================================
 *
 * This structure represents a single cached HTTP response with everything needed for LRU management:
 *
 * - data: The complete HTTP response (status line + headers + body)
 *   Example: "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<html>...</html>"
 *
 * - len: Size in bytes (needed because HTTP responses can contain binary data)
 *
 * - url: Original request URL used as cache key for lookups
 *   Example: "GET http://www.google.com/ HTTP/1.1"
 *
 * - lru_time_track: UNIX timestamp for LRU eviction policy
 *   Latest access time determines which entries to remove when cache is full
 *
 * - next: Pointer for linked list implementation (simple and efficient)
 */
struct cache_element {
    char* data;                    // Complete HTTP response data
    int len;                       // Length of the data in bytes
    char* url;                     // Request URL (cache key)
    time_t lru_time_track;         // Last access time for LRU policy
    cache_element* next;           // Pointer to next cache element in linked list
};

/*
 * ================================================================================
 * FUNCTION DECLARATIONS AND PROTOTYPES
 * ================================================================================
 *
 * Learning Log: Why Function Declarations Matter
 * ==============================================
 *
 * In C, functions must be declared before they're used. This tells the compiler:
 * 1. Function name and what it does
 * 2. Parameter types and count
 * 3. Return type
 * 4. Allows for compile-time type checking
 *
 * Without declarations, the compiler assumes functions return 'int' and can't
 * verify parameter types, leading to runtime errors and undefined behavior.
 */

// Forward declarations for all functions used in this program
int checkHTTPversion(char *msg);                           // Validate HTTP version string
int sendErrorMessage(int clientSocketId, int errorCode);   // Send HTTP error responses
int sendHTTPSErrorMessage(int clientSocketId);             // Send HTTPS not supported error
int connectRemoteServer(char* host_addr, int port_num);    // Connect to target servers
int handle_request(int ClientSocketId, struct ParsedRequest* request, char* tempReq); // Process HTTP requests
void* thread_fn(void *socketNew);                         // Thread function for client handling

// Cache management function declarations
cache_element* find(char* url);                            // Find cached response by URL
int add_cache_element(char* data, int size, char* url);    // Add new response to cache
void remove_cache_element();                               // Remove least recently used element

/*
 * ================================================================================
 * CONFIGURATION CONSTANTS AND LIMITS
 * ================================================================================
 *
 * Learning Log: Why These Specific Values?
 * ========================================
 *
 * These constants define the operational limits of our proxy server.
 * They're carefully chosen based on typical usage patterns and system limitations.
 */
/* The idea comes from thinking about how many users or clients can be connected to the server at a time */
#define MAX_CLIENTS 10                   // Maximum number of concurrent client connections
/* The idea comes from thinking about how many bytes of data can be sent or received at a time */
#define MAX_BYTES 1024                   // Buffer size for reading/writing data
/* The idea is that if we are going to save reference of the data in the cache then we need to know the total size of the cache */
#define MAX_CACHE_SIZE 200*(1 << 20)     // Maximum total cache size (200MB)
/* The idea is that we are defining the amount of elements that can be saved in the cache */
#define MAX_ELEMENT_SIZE 10 * (1 << 10)  // Maximum size of a single cache element (10KB)

/*
 * Learning Note: Cache Structure Design (ALREADY DEFINED ABOVE)
 * =============================================================
 *
 * The cache_element structure has already been defined earlier in this file
 * with comprehensive learning logs. This section previously contained a duplicate
 * definition which has been removed to prevent compilation errors.
 *
 * Key Cache Design Principles:
 * - Each cache element stores complete HTTP responses
 * - LRU timestamp enables efficient eviction policy
 * - Linked list structure allows O(1) insertion/removal
 * - URL serves as unique identifier for cache lookups
 */

/*
 * Learning Note: Function Declarations Already Defined
 * ====================================================
 *
 * The cache function prototypes have already been declared earlier in this file.
 * This section removes duplicate declarations to prevent compilation errors.
 */

/*
 * ================================================================================
 * THREADING AND SYNCHRONIZATION COMPATIBILITY
 * ================================================================================
 *
 * Learning Log: Cross-Platform Threading Challenges
 * =================================================
 *
 * Different platforms handle threading differently:
 * - POSIX systems: pthread.h, semaphore.h
 * - Windows: Windows threads, critical sections
 * - We'll provide compatibility layer for educational purposes
 */

// Platform-specific threading types and functions
#ifdef _WIN32
    // Windows threading compatibility (simplified for education)
    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    typedef struct { volatile int count; HANDLE event; } sem_t;

    // Simplified semaphore implementation for Windows
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

    // Mutex compatibility
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

    // pthread_create compatibility for Windows
    static inline int pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
        *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
        return (*thread == NULL) ? -1 : 0;
    }

#else
    // POSIX threading - use standard headers if available
    // Note: Some environments may not have full POSIX support
    #ifdef __has_include
        #if __has_include(<pthread.h>)
            // Full POSIX threading available - already included above
        #else
            // Provide stub implementations for educational purposes
            typedef int pthread_t;
            typedef int pthread_mutex_t;
            typedef struct { int count; } sem_t;

            #define sem_init(sem, pshared, value) ((sem)->count = (value), 0)
            #define sem_wait(sem) (--(sem)->count, 0)
            #define sem_post(sem) (++(sem)->count, 0)
            #define sem_getvalue(sem, value) (*(value) = (sem)->count, 0)
            #define pthread_mutex_init(mutex, attr) (*(mutex) = 0, 0)
            #define pthread_mutex_lock(mutex) (0)
            #define pthread_mutex_unlock(mutex) (0)
            #define pthread_create(thread, attr, start, arg) (0)
        #endif
    #endif
#endif

/*
 * Learning Log: Global Server State Variables
 * ===========================================
 *
 * These variables maintain the server's operational state across all threads.
 * They must be carefully managed to ensure thread safety and proper resource usage.
 */

// Global variables for server configuration and state
int port_number = 8080;                            // Default port number
int proxy_socketId;                                // Main server socket
pthread_t tid[MAX_CLIENTS];                        // Array of thread IDs for client handling
sem_t semaphore;                                   // Semaphore for limiting concurrent clients
pthread_mutex_t lock;                              // Mutex for thread-safe cache operations

/*
    Small Detour: In explaining the above global variables, we need to know the purpose of each variable:
    1. The port_number is the port number of the server, on which the Operating System will be listening for our incoming requests.
    2. The proxy_socketId is the main server socket file descriptor.
    3. The pthread_t tid is the thread id array for clients. Here we need multithreading to handle incoming requests from multiple clients at a time.
    4. The sem_t is the semaphore that will be used to limit the number of concurrent clients that can be connected to the server at a time.
       -> Intuitively, the sem_t is a variable that acts as a counter, and when the counter becomes 0, no one can enter until someone leaves and increments the counter.
    5. The pthread_mutex_t lock is the mutex that will be used to lock the cache operations, so that only one thread can access the cache at a time. This avoids race conditions, where two or more
       users are trying to access the cache at the same time.

    Now we have defined the global variables, we need to define the cache management variables.
    The cache management variables are the variables that will be used to manage the cache, and the cache is a linked list of cache_element.
    The head is the head of the linked list, and the cache_size is the current total size of the cache in bytes.
*/

// Cache management variables
cache_element* head;  // Head of the cache linked list
int cache_size; // Current total size of cache in bytes

/*
    Now we have defined the cache management variables, we need to define the functions that will be used to manage the cache.
    The functions that will be used to manage the cache are functions that will be used to add, remove, find, and update cache elements.
    The function that will be used to add the cache element is the function that will be used to add the cache element to the linked list.
    The function that will be used to remove the cache element is the function that will be used to remove the cache element from the linked list.
*/

/*
 * Thread Function: Handles individual client connections
 *
 * This function runs in a separate thread for each client connection and:
 * 1. Receives HTTP requests from the client
 * 2. Checks if the response is cached
 * 3. Serves cached response if available
 * 4. Otherwise forwards request to target server and caches the response
 * 5. Sends response back to client
 *
 * Parameters:
 * - socketNew: Pointer to the client socket file descriptor
 *
 * Returns: NULL (thread function requirement)
 */

/*
 * ================================================================================
 * CLIENT THREAD HANDLING MODULE
 * ================================================================================
 *
 * This module handles individual client connections in separate threads.
 *
 * Key operations:
 * 1. Semaphore-controlled connection limiting
 * 2. Complete HTTP request reading (until \r\n\r\n)
 * 3. Cache lookup and response serving
 * 4. Request forwarding to target servers (cache miss)
 * 5. Response caching for future requests
 *
 * Thread Flow:
 * Client Request → Cache Check → [HIT: Serve from cache] or [MISS: Forward to server + Cache response]
 */

void* thread_fn(void *socketNew){
    // Acquire semaphore to limit concurrent connections
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Number of Clients Connected: %d\n", p);

    // Extract socket descriptor from parameter
    int *t = (int*)socketNew;
    int socket = *t;
    int bytes_send_client, len;

    // Allocate buffer for receiving client request
    char* buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    // Read complete HTTP request (until we see \r\n\r\n)
    while(bytes_send_client > 0){
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }else{
            break;
        }
    }

    // Create a copy of the request for cache lookup
    char *tempReq = (char*)malloc((strlen(buffer) + 1) * sizeof(char));
    strcpy(tempReq, buffer);  // Use strcpy instead of manual loop

    // Check if response is already cached
    struct cache_element* temp = find(tempReq);
    if (temp != NULL){
        // Cache hit: Send cached response to client
        printf("Cache HIT: Serving response from cache\n");
        int size = temp->len;  // Remove unnecessary division by sizeof(char)
        int pos = 0;
        char response[MAX_BYTES];

        // Send cached data in chunks
        while(pos < size){
            bzero(response, MAX_BYTES);
            int bytes_to_send = (size - pos < MAX_BYTES) ? (size - pos) : MAX_BYTES;
            for (int i = 0 ; i < bytes_to_send ; i++){
                response[i] = temp->data[pos + i];
            }
            send(socket, response, bytes_to_send, 0);
            pos += bytes_to_send;
        }
        printf("Data Retrieved from Cache\n");
        printf("URL: %s\n\n", temp->url);

    }else if(bytes_send_client > 0){
        // Cache miss: Forward request to target server
        printf("Cache MISS: Forwarding request to server\n");
        len = strlen(buffer);
        // We now create a request to store the request that is being sent by the client.
        // We call the ParsedRequest_create function to create a request. This function is defined in the proxy_parse.h file.
        // This function creates a request object, and it returns a pointer to the request object.
        struct ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, buffer, len) < 0){
            perror("Error Parsing Request i.e Failed to parse the request\n");
        }else{
            // Check if the request is for HTTPS (not supported by this HTTP proxy)
            if (request->protocol && !strcmp(request->protocol, "https")){
                sendHTTPSErrorMessage(socket);
                printf("HTTPS Request Rejected: %s://%s%s\n",
                       request->protocol, request->host, request->path ? request->path : "/");
            }
            // Check for valid GET request
            else if (!strcmp(request->method, "GET")){
                if (request -> host && request -> path && checkHTTPversion(request -> version) == 1){
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket, 500);
                        printf("Error Sending Request to Server\n");
                    }
                }else{
                    sendErrorMessage(socket, 400);
                    printf("Invalid Request\n");
                }
            }else{
                printf("This code as of now only supports GET requests\n");
                sendErrorMessage(socket, 405);
            }
            ParsedRequest_destroy(request);
        }
    }else if(bytes_send_client == 0){
        printf("Client Disconnected\n");
    }

    // Cleanup: Close socket and release resources
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);

    // Release semaphore and update connection count
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore Post Value: %d\n", p);
    free(tempReq);
    return NULL;
}

/*
 * ================================================================================
 * REMOTE SERVER CONNECTION MODULE
 * ================================================================================
 *
 * This section handles establishing connections to target servers.
 *
 * Previous modules completed:
 * ✓ Thread-based client handling with semaphore-controlled concurrency
 * ✓ LRU cache operations (find, add, remove)
 * ✓ HTTP request parsing and validation (GET requests, HTTP/1.0 & HTTP/1.1)
 *
 * Current module: Remote server connection establishment
 * - DNS resolution (hostname → IP address)
 * - Socket creation and configuration
 * - TCP connection establishment
 */

/*
 * Connect to Remote Server
 *
 * This function establishes a TCP connection to the target server specified
 * by host_addr and port_num. It performs DNS resolution and creates a socket
 * connection to the remote server.
 *
 * Parameters:
 * - host_addr: Hostname or IP address of the target server
 * - port_num: Port number of the target server (default 80 for HTTP)
 *
 * Returns:
 * - Socket file descriptor on success
 * - -1 on failure (with error message printed)
 */
int connectRemoteServer(char* host_addr , int port_num){
    /*
     * STEP 1: Create TCP Socket
     *
     * Create a socket for connecting to the remote server.
     * AF_INET = IPv4, SOCK_STREAM = TCP protocol
     */

    // Create socket for connecting to remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0){
        perror("Error Creating Socket\n");
        return -1;
    }

    /*
     * STEP 2: DNS Resolution
     *
     * Convert hostname (e.g., "www.google.com") to IP address using DNS lookup.
     * The hostent structure contains:
     *   - h_name: Official hostname
     *   - h_aliases: Alternative names
     *   - h_addrtype: Address type (AF_INET for IPv4)
     *   - h_length: Address length (4 bytes for IPv4)
     *   - h_addr_list: Array of IP addresses
     */

    // Perform DNS resolution to get server IP address
    struct hostent* host = gethostbyname(host_addr);
    if (host == NULL){
        perror("Error Getting Host\n");
        return -1;
    }

    /*
     * STEP 3: Configure Server Address Structure
     *
     * Fill the sockaddr_in structure with all connection details:
     * - Address family (IPv4)
     * - Port number (converted to network byte order)
     * - IP address (from DNS resolution)
     */

    // Set up server address structure
    struct sockaddr_in server_addr;

    // Initialize structure to zero (good practice)
    bzero((char*)&server_addr, sizeof(server_addr));

    // Set IPv4 address family
    server_addr.sin_family = AF_INET;

    // Convert port to network byte order (handles endianness)
    server_addr.sin_port = htons(port_num);

    /*
     * Learning Log: Memory Copy Operations
     * ===================================
     *
     * bcopy() is a BSD function not available on all systems.
     * memcpy() is the standard C library equivalent:
     *
     * bcopy(source, destination, length)  - BSD style
     * memcpy(destination, source, length) - Standard C (note parameter order!)
     *
     * We copy the IP address bytes from the DNS result into our socket address structure.
     */

    // Copy IP address from DNS result to address structure (using standard memcpy)
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    /*
     * STEP 4: Establish TCP Connection
     *
     * Use the configured address structure to connect to the target server.
     * Returns 0 on success, -1 on failure.
     */
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error Connecting to Server\n");
        return -1;
    }

    return remoteSocket;
}

/*
 * ================================================================================
 * HTTP REQUEST HANDLING MODULE
 * ================================================================================
 */

/*
 * Handle HTTP Request
 *
 * Processes an HTTP GET request by forwarding it to the target server,
 * receiving the response, caching it, and sending it back to the client.
 *
 * Parameters:
 * - ClientSocketId: Socket descriptor for the client connection
 * - request: Parsed HTTP request structure
 * - tempReq: Original request string (used as cache key)
 *
 * Returns:
 * - 0 on success
 * - -1 on failure
 */
int handle_request(int ClientSocketId, struct ParsedRequest* request, char* tempReq){
    /*
     * STEP 1: Construct HTTP Request for Target Server
     *
     * Build a properly formatted HTTP request to send to the target server.
     * Required components:
     * - HTTP method (GET)
     * - Resource path (e.g., /index.html)
     * - HTTP version (HTTP/1.1)
     * - Host header (for virtual hosting)
     * - Connection: close (prevents connection reuse)
     * - Additional headers from original request
     */
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

    /*
     * STEP 2: Add Additional Headers
     *
     * Ensure required headers are set and preserve any additional
     * headers from the original client request.
     */

    // Force connection closure to simplify proxy connection management
    if(ParsedHeader_set(request,"Connection", "close") < 0){
        printf("Error Setting Connection Header\n");
    }

    // Ensure host header is properly set for virtual hosting support
    if (ParsedHeader_get(request, "Host") != NULL){
        if(!ParsedHeader_set(request, "Host", request -> host) < 0){
            printf("Error Getting Host Header\n");
        }
    }

    /*
     * Learning Log: HTTP Header Reconstruction
     * ========================================
     *
     * ParsedRequest_unparse_headers() converts the parsed header structure
     * back into raw HTTP header format for transmission to target server.
     *
     * This ensures that important headers from the original client request
     * (like User-Agent, Accept-Language, etc.) are preserved and forwarded.
     */

    // Append any additional headers from the original client request
    if (ParsedRequest_unparse_headers(request, buffer + len , (size_t)(MAX_BYTES - len)) < 0){
        printf("Error Unparsing Headers\n");
    }

    /*
     * STEP 3: Connect to Target Server
     */

    // Determine target server port (default 80 for HTTP protocol)
    int server_port = 80;  // HTTP default port
    if (request -> port != NULL){
        server_port = atoi(request -> port);  // Convert string to integer
    }

    // Establish TCP connection to target server
    int remoteSocketId = connectRemoteServer(request -> host, server_port);
    if (remoteSocketId < 0){
        printf("Error Connecting to Server\n");
        return -1;
    }

    /*
     * STEP 4: Forward Request and Handle Response
     *
     * Learning Log: HTTP Request-Response Pattern
     * ==========================================
     *
     * Now we enter the core HTTP communication phase. HTTP follows a simple request-response pattern:
     * 1. CLIENT (our proxy) sends REQUEST to SERVER (target website)
     * 2. SERVER processes the request and sends back RESPONSE
     * 3. CLIENT (our proxy) receives the RESPONSE and forwards it to the original client
     *
     * This is fundamental to how the web works - every web page load follows this pattern.
     * Our proxy acts as an intermediary, making requests on behalf of clients.
     */

    // Send the constructed HTTP request to target server
    // The send() function transmits our formatted HTTP request over the TCP connection
    // remoteSocketId: socket connected to target server (e.g., www.google.com)
    // buffer: contains our complete HTTP request (GET /path HTTP/1.1\r\nHost: ...)
    // strlen(buffer): number of bytes to send (length of our HTTP request)
    // Returns: number of bytes actually sent, or -1 on error
    int bytes_sent = send(remoteSocketId, buffer, strlen(buffer), 0);

    // Clear the buffer to prepare it for receiving the response
    // We reuse the same buffer for efficiency instead of allocating new memory
    // This is safe because we've already sent the request data
    bzero(buffer, MAX_BYTES);

    /*
     * Learning Log: Why recv() Again?
     * ==============================
     *
     * Students often ask: "Why do we need recv() again? Didn't we already receive the client request?"
     *
     * Answer: This is a DIFFERENT recv() for a DIFFERENT purpose!
     *
     * Previous recv() (in thread_fn): Received HTTP request FROM CLIENT → Our Proxy
     * Current recv() (here): Receive HTTP response FROM TARGET SERVER → Our Proxy
     *
     * Data Flow:
     * [Client] --HTTP Request--> [Our Proxy] --HTTP Request--> [Target Server]
     * [Client] <--HTTP Response-- [Our Proxy] <--HTTP Response-- [Target Server]
     *                                        ↑
     *                                   THIS recv() gets the response!
     *
     * The recv() function waits for data to arrive from the target server.
     * This could be HTML, CSS, images, or any HTTP response content.
     * We use MAX_BYTES - 1 to leave space for null terminator.
     *
     * Variable Reuse Note:
     * We reuse 'bytes_sent' variable to store 'bytes_received' - not ideal naming,
     * but it saves memory. The variable now represents bytes received from server.
     */

    // Begin receiving the HTTP response from target server
    // This recv() waits for the target server to send back its HTTP response
    // The response includes status line, headers, and content (HTML, images, etc.)
    bytes_sent = recv(remoteSocketId, buffer, MAX_BYTES - 1, 0);

    /*
     * STEP 5: Forward Response to Client and Cache
     *
     * Learning Log: Real-Time Streaming with Simultaneous Caching
     * ==========================================================
     *
     * This is the most complex part of our proxy! We need to do THREE things simultaneously:
     * 1. Forward data from target server to client (real-time streaming)
     * 2. Store the data for caching (for future identical requests)
     * 3. Handle variable response sizes (could be 1KB or 10MB)
     *
     * Why Streaming?
     * - Without streaming: Client waits for ENTIRE response before seeing anything
     * - With streaming: Client sees data as it arrives (much faster perceived performance)
     *
     * Why Simultaneous Caching?
     * - We MUST cache while streaming because we can't re-read the network data
     * - Once data passes through, it's gone unless we save it
     *
     * Challenge: We don't know the response size in advance!
     * Solution: Dynamic buffer expansion using realloc()
     */

    // Initialize dynamic buffer for caching the complete response
    // We start with MAX_BYTES and expand as needed
    // This buffer will store the ENTIRE response for caching
    char * temp_buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    int temp_buffer_size = MAX_BYTES;      // Current allocated size
    int temp_buffer_index = 0;             // Current position in temp_buffer

    /*
     * Learning Log: The Streaming Loop Explained
     * ==========================================
     *
     * This while loop is the heart of real-time proxy streaming:
     *
     * Loop Condition: while(bytes_sent > 0)
     * - bytes_sent > 0: We received data from target server
     * - bytes_sent = 0: Target server closed connection (end of response)
     * - bytes_sent < 0: Network error occurred
     *
     * Inside each iteration:
     * 1. Forward current chunk to client
     * 2. Store current chunk for caching
     * 3. Expand cache buffer if needed
     * 4. Get next chunk from target server
     *
     * This creates a pipeline: Target Server → Our Proxy → Client (while storing)
     */

    // Forward response to client while collecting it for caching
    while(bytes_sent > 0){
        /*
         * Learning Log: Why send(ClientSocketId, ...) Here?
         * ================================================
         *
         * This send() forwards the received data IMMEDIATELY to the client.
         * We don't wait to receive the entire response - we stream it chunk by chunk.
         *
         * ClientSocketId: socket connected to original client (browser)
         * buffer: contains current chunk from target server
         * bytes_sent: how many bytes we just received (and now sending)
         *
         * Real-World Impact:
         * - User sees webpage loading progressively (like YouTube buffering)
         * - Instead of waiting 5 seconds for complete page, user sees content in 0.1 seconds
         */
        bytes_sent = send(ClientSocketId, buffer, bytes_sent, 0);

        /*
         * Learning Log: Simultaneous Caching Logic
         * ========================================
         *
         * While forwarding to client, we MUST also store the data for caching.
         * We copy each received chunk into our temp_buffer.
         *
         * Why byte-by-byte copy?
         * - We could use memcpy(), but this explicit loop makes the process clear
         * - Each byte from the target server gets stored in our cache buffer
         * - temp_buffer_index tracks where we are in the complete response
         */

        // Store response data for caching (byte-by-byte)
        for(int i = 0 ; i < bytes_sent/sizeof(char) ; i++){
            temp_buffer[temp_buffer_index] = buffer[i];  // Copy current byte
            temp_buffer_index++;                         // Move to next position
        }

        /*
         * Learning Log: Dynamic Buffer Expansion
         * ======================================
         *
         * Problem: We don't know response size in advance!
         * - A simple webpage might be 5KB
         * - A large image might be 5MB
         * - We started with MAX_BYTES (1024 bytes) but need more space
         *
         * Solution: Dynamic expansion with realloc()
         * - When we're running out of space, we expand the buffer
         * - realloc() creates a larger buffer and copies existing data
         * - This allows us to cache responses of any size
         *
         * Memory Management:
         * - Always check if realloc() succeeds (returns non-NULL)
         * - If it fails, free existing memory and return error
         * - This prevents memory leaks and crashes
         */

        // Expand buffer if needed (when approaching current limit)
        temp_buffer_size = temp_buffer_index + MAX_BYTES;  // Reserve space for next chunk
        char* new_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
        if (new_buffer == NULL) {
            perror("Error reallocating buffer\n");
            free(temp_buffer);  // Prevent memory leak
            return -1;
        }
        temp_buffer = new_buffer;  // Update pointer to new larger buffer

        // Error handling: Check if client disconnected
        if (bytes_sent < 0){
            perror("Error Sending Response to Client\n");
            break;
        }

        // Prepare for next chunk: clear buffer and receive more data
        bzero(buffer, MAX_BYTES);                                    // Clear receiving buffer
        bytes_sent = recv(remoteSocketId, buffer, MAX_BYTES - 1, 0); // Get next chunk from server

        /*
         * Learning Log: The Loop Continues...
         * ===================================
         *
         * After recv(), we have three possibilities:
         * 1. bytes_sent > 0: Got more data, loop continues
         * 2. bytes_sent = 0: Server finished sending, loop exits
         * 3. bytes_sent < 0: Network error, loop exits
         *
         * This creates a streaming pipeline that continues until:
         * - Target server closes connection (normal end)
         * - Network error occurs
         * - Client disconnects
         */
    }

    /*
     * Learning Log: Final Error Check
     * ==============================
     *
     * When we exit the streaming loop, we need to check WHY we exited:
     * - bytes_sent = 0: Normal completion (server closed connection)
     * - bytes_sent < 0: Network error (connection lost, timeout, etc.)
     *
     * Only bytes_sent < 0 indicates an actual error condition.
     * bytes_sent = 0 is the normal way HTTP connections end.
     */
    if (bytes_sent < 0){
        perror("Error Receiving Response from Server\n");
        return -1;
    }

    /*
     * STEP 6: Cache Response and Cleanup
     *
     * Learning Log: Finalizing the Cache Entry
     * ========================================
     *
     * At this point, we have successfully:
     * 1. ✓ Received complete response from target server
     * 2. ✓ Forwarded all data to client (client has full response)
     * 3. ✓ Stored all data in temp_buffer (ready for caching)
     *
     * Now we need to:
     * 1. Finalize the cached data (null-terminate for safety)
     * 2. Add it to our LRU cache for future identical requests
     * 3. Clean up all allocated memory and network connections
     *
     * Why Cache After Streaming?
     * - We could only cache after getting the complete response
     * - Caching incomplete data would serve broken responses to future clients
     * - Better to serve fresh data than corrupted cached data
     */

    /*
     * Learning Log: String Termination in C
     * =====================================
     *
     * In C, strings must be null-terminated ('\0') for safety.
     * Our temp_buffer contains raw HTTP response data (bytes).
     * Adding '\0' at the end ensures string functions work correctly.
     *
     * Without this: strlen(), strcmp(), printf() could crash or behave unpredictably
     * With this: Our cached data is safe to use in all string operations
     */

    // Null-terminate the response data for safe string operations
    temp_buffer[temp_buffer_index] = '\0';

    /*
     * Learning Log: Adding to LRU Cache
     * =================================
     *
     * add_cache_element() does several critical things:
     * 1. Checks if response fits in cache (size limits)
     * 2. Evicts old entries if cache is full (LRU policy)
     * 3. Creates new cache_element with current timestamp
     * 4. Adds to front of linked list (most recently used)
     *
     * Parameters:
     * - temp_buffer: complete HTTP response (headers + body)
     * - temp_buffer_index: total size in bytes
     * - tempReq: original client request (used as cache key)
     *
     * Future requests matching tempReq will get temp_buffer instantly!
     */

    // Add response to cache for future requests
    add_cache_element(temp_buffer, temp_buffer_index, tempReq);

    /*
     * Learning Log: Resource Cleanup (Critical!)
     * ==========================================
     *
     * Memory management in C is manual - we must free what we allocate.
     * Network connections must be properly closed.
     *
     * Cleanup Order:
     * 1. free(buffer): deallocate request/response buffer
     * 2. free(temp_buffer): deallocate cache buffer
     * 3. close(remoteSocketId): close connection to target server
     *
     * Note: temp_buffer will be freed later by cache management functions
     * when the cache entry is eventually evicted.
     *
     * Missing cleanup causes:
     * - Memory leaks (system runs out of RAM)
     * - Socket leaks (system runs out of file descriptors)
     * - Server becomes unstable and crashes
     */

    // Cleanup allocated resources
    free(buffer);         // Free request/response working buffer
    free(temp_buffer);    // Free response cache buffer
    close(remoteSocketId); // Close connection to target server

    /*
     * Learning Log: Function Success
     * ==============================
     *
     * return 0 indicates successful completion:
     * ✓ Connected to target server
     * ✓ Forwarded request successfully
     * ✓ Received complete response
     * ✓ Streamed response to client
     * ✓ Cached response for future use
     * ✓ Cleaned up all resources
     *
     * The client now has the complete webpage/resource they requested,
     * and future identical requests will be served from cache instantly!
     */
    return 0;
}

/*
 * ================================================================================
 * UTILITY FUNCTIONS MODULE
 * ================================================================================
 */

/*
 * Check HTTP Version
 *
 * Validates if the HTTP version string is supported.
 * Currently supports HTTP/1.0 and HTTP/1.1.
 *
 * Parameters:
 * - msg: HTTP version string (e.g., "HTTP/1.1")
 *
 * Returns:
 * - 1 if version is supported
 * - -1 if version is not supported
 */
int checkHTTPversion(char *msg){
    int version = -1;

    if (strncmp(msg, "HTTP/1.0", 8) == 0){
        version = 1;
    }else if (strncmp(msg, "HTTP/1.1", 8) == 0){
        version = 1;
    }else{
        version = -1;
    }
    return version;
}

/*
 * Send HTTP Error Message
 *
 * Sends a properly formatted HTTP error response to the client.
 * Includes appropriate HTTP status code, headers, and HTML error page.
 *
 * Parameters:
 * - clientSocketId: Socket descriptor for the client connection
 * - errorCode: HTTP status code (400, 403, 404, 405, 408, 500)
 *
 * Returns:
 * - 1 on success
 * - -1 on failure
 */
int sendErrorMessage(int clientSocketId, int errorCode){
    char str[1024];
    char currentTime[50];
    time_t now = time(NULL);

    // Get current timestamp for HTTP Date header
    struct tm data = *localtime(&now);
    strftime(currentTime, sizeof(currentTime), "%Y-%m-%d %H:%M:%S", &data);

    // Generate appropriate error response based on status code
    switch(errorCode){
        case 400:
            snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>400 Bad Request</h1><p>The request was invalid or cannot be otherwise served. The request should not be repeated.</p></body></html>", currentTime);
            printf("400 Bad Request\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        case 404:
            snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>404 Not Found</h1><p>The requested resource could not be found but may be available again in the future.</p></body></html>", currentTime);
            printf("404 Not Found\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        case 500:
            snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>500 Internal Server Error</h1><p>The server encountered an error while processing your request.</p></body></html>", currentTime);
            printf("500 Internal Server Error\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        case 403:
            snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>403 Forbidden</h1><p>The request was forbidden by the server.</p></body></html>", currentTime);
            printf("403 Forbidden\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        case 405:
            snprintf(str, sizeof(str), "HTTP/1.1 405 Method Not Allowed\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>405 Method Not Allowed</h1><p>The request method is not allowed for the resource.</p></body></html>", currentTime);
            printf("405 Method Not Allowed\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        case 408:
            snprintf(str, sizeof(str), "HTTP/1.1 408 Request Timeout\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: 130\r\n\r\n<html><body><h1>408 Request Timeout</h1><p>The request timed out.</p></body></html>", currentTime);
            printf("408 Request Timeout\n");
            send(clientSocketId, str, strlen(str), 0);
            break;
        default: return -1;
    }
    return 1;
}

/*
 * Send HTTPS Not Supported Error Message
 *
 * Sends a clear explanation that this proxy only supports HTTP, not HTTPS.
 * This prevents the confusing "too many redirections" error that users
 * encounter when trying to access HTTPS sites through an HTTP proxy.
 *
 * Parameters:
 * - clientSocketId: Socket descriptor for the client connection
 *
 * Returns:
 * - 1 on success
 * - -1 on failure
 */
int sendHTTPSErrorMessage(int clientSocketId){
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
 * MAIN SERVER MODULE
 * ================================================================================
 */

/*
 * Main Function: HTTP Proxy Server Entry Point
 *
 * Initializes the proxy server and handles the main server loop.
 * Sets up sockets, synchronization, and spawns threads for each client.
 *
 * Command Line Usage: ./proxy <port_number>
 * Example: ./proxy 8080
 *
 * Parameters:
 * - argc: Number of command line arguments
 * - argv: Array of command line argument strings
 *
 * Returns:
 * - 0 on normal exit
 * - 1 on error
 */
int main(int argc, char* argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;

    // Initialize synchronization primitives
    sem_init(&semaphore, 0, MAX_CLIENTS);  // Limit concurrent connections
    pthread_mutex_init(&lock, NULL);       // Protect cache operations

    // Parse command line arguments
    if (argc == 2){
        port_number = atoi(argv[1]);
    }else{
        printf("Too Few Arguments\n");
        printf("Usage: %s <port_number>\n", argv[0]);
        printf("Example: %s 8080\n", argv[0]);
        exit(1);
    }

    printf("Proxy Server Started on Port %d\n", port_number);

    // Create server socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0){
        perror("Error Creating Socket\n");
        exit(1);
    }

    // Set socket options to allow address reuse
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        perror("Error Setting Socket Options\n");
        exit(1);
    }

    // Configure server address structure
    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to specified port
    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Error Binding Socket\n");
        exit(1);
    }

    printf("Listening on Port %d\n", port_number);

    // Start listening for client connections
    int listen_status = listen(proxy_socketId, 5);
    if (listen_status < 0){
        perror("Error Listening\n");
        exit(1);
    }

    // Main server loop: accept and handle client connections
    int i = 0;
    int connected_clients[MAX_CLIENTS];

    while (1){
        // Accept new client connection
        bzero((char*)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, &client_len);
        if (client_socketId < 0){
            perror("Error Accepting Client\n");
            continue;
        }else{
            connected_clients[i] = client_socketId;
        }

        /*
         * Learning Log: Socket Address Casting and Client Information
         * ==========================================================
         *
         * client_addr is a generic sockaddr structure, but we know it's actually
         * a sockaddr_in (IPv4) structure. We cast it to access IPv4-specific fields.
         *
         * Type Safety Note:
         * - client_pt is a POINTER to the sockaddr_in structure
         * - We extract the IP address for logging purposes
         * - inet_ntop() converts binary IP to human-readable string
         */

        // Log client connection information (cast to access IPv4 fields)
        struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;  // Use -> for pointer access
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, ip_str, INET_ADDRSTRLEN);
        printf("Client Connected with Port %d and IP %s\n", ntohs(client_pt->sin_port), ip_str);

        // Create new thread to handle this client
        pthread_create(&tid[i], NULL, thread_fn, (void*)&client_socketId);
        i = (i + 1) % MAX_CLIENTS; // Wrap around to prevent array overflow
    }

    // Cleanup (this code is never reached in current implementation)
    close(proxy_socketId);
    return 0;
}

/*
 * ================================================================================
 * LRU CACHE IMPLEMENTATION MODULE
 * ================================================================================
 */

/*
 * Find Cached Response
 *
 * Searches the cache for a response matching the given URL.
 * If found, updates the LRU timestamp to mark it as recently used.
 *
 * Parameters:
 * - url: The request URL to search for in cache
 *
 * Returns:
 * - Pointer to cache element if found
 * - NULL if not found
 */
cache_element* find(char* url){
    cache_element* site = NULL;

    // Acquire mutex lock for thread-safe cache access
    int time_lock_val = pthread_mutex_lock(&lock);
    printf("Lock Acquired: %d\n", time_lock_val);

    if(head != NULL){
        site = head;
        // Search through cache linked list
        while(site != NULL){
            if(!strcmp(site->url, url)){
                // Cache hit: update LRU timestamp
                printf("LRU Time Track before: %ld\n", site->lru_time_track);
                printf("URL: %s\n", site->url);
                site->lru_time_track = time(NULL);
                printf("LRU Time Track after: %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }else{
        printf("No Cache Found in the URL\n");
    }

    // Release mutex lock
    time_lock_val = pthread_mutex_unlock(&lock);
    printf("Lock is Unlocked\n");
    return site;
}

/*
 * Remove Least Recently Used Cache Element
 *
 * Implements LRU eviction policy by finding and removing the cache element
 * with the oldest access timestamp. This function is called when the cache
 * is full and we need to make space for new entries.
 *
 * The function:
 * 1. Finds the element with the smallest lru_time_track value
 * 2. Removes it from the linked list
 * 3. Frees the associated memory
 * 4. Updates the cache size counter
 */
void remove_cache_element(){
    cache_element *p;
    cache_element *q;
    cache_element *temp;
    cache_element *prev;

    // Acquire mutex lock for thread-safe cache access
    int time_lock_val = pthread_mutex_lock(&lock);
    printf("Lock Acquired: %d\n", time_lock_val);

    if (head != NULL){
        // Find the least recently used element
        temp = head;
        prev = NULL;
        p = head;
        q = head->next;

        // Traverse the list to find element with oldest timestamp
        while(q != NULL){
            if(q->lru_time_track < temp->lru_time_track){
                temp = q;
                prev = p;
            }
            p = q;
            q = q->next;
        }

        // Remove the least recently used element from linked list
        if (temp == head){
            head = head->next;
        }else{
            prev->next = temp->next;
        }

        // Update cache size and free memory
        cache_size -= temp->len + sizeof(cache_element) + strlen(temp->url) + 1;
        free(temp->data);
        free(temp->url);
        free(temp);
    }

    // Release mutex lock
    time_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove cache lock is unlocked\n");
}

/*
 * Add New Response to Cache
 *
 * Adds a new HTTP response to the cache. If the cache is full, removes
 * least recently used elements to make space. The new element is added
 * at the head of the linked list for O(1) insertion.
 *
 * Parameters:
 * - data: The HTTP response data to cache
 * - size: Size of the data in bytes
 * - url: The request URL (used as cache key)
 *
 * Returns:
 * - 1 if successfully added to cache
 * - 0 if element is too large or other error
 */
int add_cache_element(char* data, int size, char* url){
    // Acquire mutex lock for thread-safe cache access
    int time_lock_val = pthread_mutex_lock(&lock);
    printf("Lock Acquired: %d\n", time_lock_val);

    int element_size = size/sizeof(char);

    // Check if element is too large for cache
    if (element_size > MAX_ELEMENT_SIZE){
        time_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock is unlocked\n");
        return 0;
    }
    else{
        // Remove LRU elements if cache is full
        while(cache_size + size + sizeof(cache_element) + strlen(url) + 1 > MAX_CACHE_SIZE){
            remove_cache_element();
        }

        // Create new cache element
        cache_element* element = (cache_element*)malloc(sizeof(cache_element));
        element->data = (char*)malloc(element_size*sizeof(char));
        strcpy(element->data, data);
        element->url = (char*)malloc(strlen(url)*sizeof(char));
        strcpy(element->url, url);
        element->lru_time_track = time(NULL);
        element -> next = head;
        element -> len = size;

        // Add to head of linked list (most recently used)
        head = element;

        // Update cache size
        cache_size += size + sizeof(cache_element) + strlen(url) + 1;

        // Release mutex lock
        time_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock is unlocked\n");
        return 1;
    }
}




