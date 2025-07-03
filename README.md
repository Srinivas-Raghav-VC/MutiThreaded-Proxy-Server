# HTTP Proxy Server with LRU Cache Implementation

## Educational Overview

This project implements a multi-threaded HTTP proxy server with LRU (Least Recently Used) caching for educational purposes. It demonstrates fundamental concepts in:

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

### Quick Start

1. **Clone and Navigate:**
   ```bash
   cd proxy-server-directory
   ```

2. **Compile:**
   ```bash
   make
   ```

   Or manually:
   ```bash
   gcc -o proxy_parse.o -c proxy_parse.c -lpthread
   gcc -o proxy.o -c proxy_server_with_cache.c -lpthread
   gcc -o proxy proxy_parse.o proxy.o -lpthread
   ```

3. **Run the Proxy:**
   ```bash
   ./proxy 8080
   ```

4. **Configure Your Browser:**
   - Set HTTP proxy to: `127.0.0.1:8080`
   - Or test with curl: `curl -x http://127.0.0.1:8080 http://www.google.com`

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
└── other-projects/              # Additional course projects
    ├── Chat_Room/              # Socket-based chat implementation
    └── usb_driver/             # Linux kernel module example
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

## Educational Value

This project serves as a comprehensive introduction to:

1. **Real-World Networking**: Learn how web browsers and servers communicate
2. **Performance Optimization**: Understand caching strategies and their impact
3. **Concurrent Programming**: Handle multiple clients simultaneously
4. **System Design**: Build scalable, maintainable network applications
5. **Debugging Skills**: Trace network traffic and diagnose issues

## Advanced Features

### Cache Management
- **LRU Eviction**: Automatically removes least recently used entries
- **Size Limits**: Prevents unlimited memory consumption
- **Thread Safety**: Multiple clients can access cache simultaneously
- **Statistics**: Monitor cache hit/miss ratios

### Error Handling
- **HTTP Status Codes**: Proper 400, 404, 500 responses
- **Network Failures**: Graceful handling of connection issues
- **Resource Cleanup**: Prevents memory and socket leaks
- **Logging**: Detailed console output for debugging

### Performance Features
- **Real-Time Streaming**: Progressive content loading
- **Connection Reuse**: Efficient socket management
- **Configurable Limits**: Tunable performance parameters
- **Platform Optimization**: Native threading on each OS

## Credits and Acknowledgments

### Core Components
- **HTTP Parsing Library**: `proxy_parse.h` & `proxy_parse.c`
  - **Author**: Matvey Arye (COS 518)
  - **Function**: Robust HTTP request parsing and header manipulation
  - **Features**: Request creation, parsing, unparsing, header management

### Educational Foundation
- **Course Material**: Computer Networks / Systems Programming curriculum
- **Concepts**: Berkeley Sockets, HTTP protocol, multi-threading patterns
- **Standards**: POSIX threading, RFC HTTP specifications

### Development Tools
- **UNIX/Linux**: Standard socket programming libraries
- **Windows**: Winsock2 compatibility layer
- **Cross-Platform**: Portable C implementation with platform abstractions

## Troubleshooting

### Common Issues

**Compilation Errors:**
- Ensure pthread library is available: `-lpthread`
- On Windows: Install MinGW or use Visual Studio
- Missing headers: Check platform-specific includes

**Runtime Issues:**
- **"Port already in use"**: Choose different port or wait
- **"Permission denied"**: Use port > 1024 or run as administrator
- **"Connection refused"**: Check firewall settings

**Browser Configuration:**
- **Proxy not working**: Verify IP (127.0.0.1) and port (8080)
- **HTTPS sites**: This proxy handles HTTP only
- **DNS issues**: Check `/etc/hosts` or DNS settings

### Debug Mode
Enable detailed logging by modifying debug flags in source code:
```c
#define DEBUG 1  // In proxy_parse.h
```

## Performance Characteristics

### Benchmarks (Typical)
- **Concurrent Clients**: Up to 10 simultaneous connections
- **Cache Size**: 200MB total, 10KB per entry
- **Response Time**:
  - Cache Hit: ~1ms
  - Cache Miss: Network latency + server response time
- **Memory Usage**: Base ~5MB + cached content

### Scalability Notes
- **Thread Model**: One thread per client (suitable for educational use)
- **Production Considerations**: Consider thread pools, epoll/kqueue for high scale
- **Cache Efficiency**: LRU policy provides good hit rates for typical web browsing

## Future Enhancements

### Potential Improvements
- **HTTPS Support**: SSL/TLS tunnel handling
- **HTTP/2**: Modern protocol support
- **Connection Pooling**: Reuse server connections
- **Configuration File**: Runtime parameter adjustment
- **Web Interface**: Browser-based cache management
- **Statistics Dashboard**: Real-time performance metrics

### Advanced Features
- **Content Filtering**: Block unwanted content
- **Bandwidth Limiting**: QoS controls
- **Load Balancing**: Multiple backend servers
- **Persistent Cache**: Disk-based storage
- **Compression**: Gzip encoding support

## License

This project is created for educational purposes. Feel free to use, modify, and distribute for learning and teaching computer networking concepts.

## Support

For questions, issues, or educational discussions about this proxy server implementation, please refer to your course materials or instructor guidance.

---
*This proxy server demonstrates real-world networking concepts in an educational context. It provides hands-on experience with multi-threaded programming, HTTP protocol implementation, and caching strategies commonly used in production web infrastructure.*
