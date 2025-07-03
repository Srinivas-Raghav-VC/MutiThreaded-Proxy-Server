# HTTP Proxy Server with LRU Cache Implementation

## Educational Overview

This project implements a multi-threaded HTTP proxy server with LRU (Least Recently Used) caching
- **Network Programming**: TCP sockets, HTTP protocol, DNS resolution
- **Systems Programming**: Multi-threading, synchronization, memory management
- **Data Structures**: Linked lists, LRU cache implementation
- **Cross-Platform Development**: Windows/UNIX compatibility layer

## What is an HTTP Proxy Server?

An HTTP Proxy Server acts as an intermediary between web browsers (clients) and web servers:

```
[Browser] ↔ [Our Proxy] ↔ [Website Server]
```

### Key Benefits:
- **Caching**: Store popular responses for faster serving
- **Filtering**: Control access to websites and content
- **Logging**: Monitor and analyze web traffic
- **Load Balancing**: Distribute requests across servers
- **Anonymity**: Hide client IP addresses

## Architecture Overview

### 1. Multi-Threaded Client Handling
- Main thread accepts new client connections
- Each client gets a dedicated worker thread
- Semaphore limits concurrent connections (prevents overload)
- Thread-safe operations using mutex locks

### 2. LRU Cache System
- Stores complete HTTP responses in memory
- Evicts oldest unused entries when cache is full
- O(1) insertion and lookup using linked list
- Configurable size limits (200MB default, 10KB per element)

### 3. HTTP Protocol Support
- Parses HTTP GET requests (HTTP/1.0 and HTTP/1.1)
- Forwards requests to target servers via DNS resolution
- Real-time response streaming (progressive loading)
- Proper error handling (400, 404, 500, etc.)

### 4. Cross-Platform Compatibility
- Works on Windows, Linux, macOS
- Automatic platform detection and adaptation
- Compatible threading and socket implementations

## Compilation and Setup

### Prerequisites
- **C Compiler**: GCC (Linux/macOS) or MinGW/MSVC (Windows)
- **Standard Libraries**: pthread, socket libraries
- **Make** (optional but recommended)

### Windows Setup

```bash
# Check if GCC is installed
gcc --version

# Install MinGW (if needed)
choco install mingw

# Compile Windows version
make -f Makefile.windows

# Start the proxy server
./proxy_windows.exe 8080

# Test immediately
curl -x http://127.0.0.1:8080 http://httpbin.org/json

# Launch Chrome with proxy
start chrome --proxy-server="127.0.0.1:8080"
```

### Unix/Linux Setup

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install build-essential gcc make

# Compile Unix version
make

# Start the proxy server
./proxy 8080

# Test with curl
curl -x http://127.0.0.1:8080 http://www.google.com
```

### Manual Compilation

**Windows:**
```bash
gcc -o proxy_windows.exe proxy_server_windows.c proxy_parse.c -lws2_32
```

**Unix/Linux:**
```bash
gcc -o proxy proxy_server_with_cache.c proxy_parse.c -lpthread
```

## Usage Examples

### Basic Testing
```bash
# Start the proxy server
./proxy 8080

# Test with curl (in another terminal)
curl -x http://127.0.0.1:8080 http://www.google.com
curl -x http://127.0.0.1:8080 http://www.example.com
```

### Browser Configuration
1. **Chrome/Firefox**: Settings → Network → Proxy Settings
2. **HTTP Proxy**: `127.0.0.1`
3. **Port**: `8080`
4. **Browse normally** - all HTTP traffic will go through your proxy

### Observing Cache Behavior
1. **First Request**: Will show "Cache MISS" - fetches from target server
2. **Repeat Request**: Will show "Cache HIT" - serves from cache instantly
3. **Monitor**: Watch console output for cache hit/miss statistics

## Project Structure

```
proxy-server/
├── proxy_server_with_cache.c    # Main proxy implementation
├── proxy_parse.h                # HTTP parsing library header
├── proxy_parse.c                # HTTP parsing library source
├── Makefile                     # Build configuration
├── README.md                    # This documentation

```

## Key Learning Concepts

### Network Programming
- **Berkeley Sockets API**: `socket()`, `bind()`, `listen()`, `accept()`
- **DNS Resolution**: `gethostbyname()` for hostname-to-IP conversion
- **HTTP Protocol**: Request/response format, headers, status codes
- **TCP Communication**: Reliable, connection-oriented data transfer

### Systems Programming
- **Multi-threading**: `pthread_create()`, thread synchronization
- **Semaphores**: Limiting concurrent connections (`sem_wait()`, `sem_post()`)
- **Mutex Locks**: Thread-safe cache operations (`pthread_mutex_lock()`)
- **Memory Management**: Dynamic allocation, preventing leaks

### Data Structures
- **Linked Lists**: O(1) cache insertion/removal
- **LRU Algorithm**: Timestamp-based eviction policy
- **Hash-like Lookups**: URL-based cache key matching

### Cross-Platform Development
- **Preprocessor Directives**: `#ifdef _WIN32` for platform detection
- **Function Compatibility**: Wrapping platform-specific APIs
- **Build Systems**: Makefile for automated compilation

## Benchmarks (Typical)
- **Concurrent Clients**: Up to 10 simultaneous connections
- **Cache Size**: 200MB total, 10KB per entry
- **Response Time**:
  - Cache Hit: ~1ms
  - Cache Miss: Network latency + server response time
- **Memory Usage**: Base ~5MB + cached content


