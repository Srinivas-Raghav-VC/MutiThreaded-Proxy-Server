# HTTP Proxy Server with LRU Cache

This project is a multi-threaded HTTP proxy server that features an in-memory LRU (Least Recently Used) cache. It's designed to be a practical tool for understanding network programming, threading, and caching.

## What it does

The proxy server acts as a middleman between a web browser and a website. When you make a request through the proxy, it fetches the content from the website and forwards it to you. If the same content is requested again, it's served from the cache for a significant speed boost.

**Core Features:**
- **Caching:** Speeds up browsing by caching frequently accessed resources.
- **Multi-threaded:** Handles multiple clients at once.
- **Cross-Platform:** Works on both Windows and Unix-like systems (Linux, macOS).

## Architecture

- **Main Thread:** Listens for and accepts new client connections.
- **Worker Threads:** Each client is handled in a separate thread. A semaphore limits the number of concurrent connections to prevent overload.
- **LRU Cache:** A simple linked-list based cache stores HTTP responses. When the cache is full, the least recently used item is evicted. Cache operations are thread-safe, protected by a mutex.
- **HTTP Parsing:** A basic parser handles HTTP GET requests.

## How to Build and Run

### Prerequisites
- A C compiler (GCC for Linux/macOS, MinGW/MSVC for Windows).
- `make` is recommended for easier compilation.

### Windows

1.  **Install a compiler:** If you don't have one, install MinGW:
    ```bash
    choco install mingw
    ```
2.  **Compile:**
    ```bash
    gcc -o proxy_windows.exe proxy_server_windows.c proxy_parse.c -lws2_32
    ```
3.  **Run:**
    ```bash
    ./proxy_windows.exe 8080
    ```

### Unix/Linux/macOS

1.  **Install build tools:**
    ```bash
    # For Debian/Ubuntu
    sudo apt-get install build-essential
    ```
2.  **Compile:**
    ```bash
    make
    ```
    Alternatively, compile manually:
    ```bash
    gcc -o proxy proxy_server_with_cache.c proxy_parse.c -lpthread
    ```
3.  **Run:**
    ```bash
    ./proxy 8080
    ```

## How to Use

1.  **Start the proxy server** as described above.
2.  **Configure your browser** or system to use the proxy:
    -   **Server/Host:** `127.0.0.1`
    -   **Port:** `8080` (or whatever port you used).
3.  **Browse the web.** All your HTTP traffic will now go through the proxy.

### Testing with `curl`

You can also test the proxy from the command line:

```bash
# This request will be a CACHE MISS on the first run
curl -x http://127.0.0.1:8080 http://httpbin.org/get

# This request will be a CACHE HIT on the second run
curl -x http://127.0.0.1:8080 http://httpbin.org/get
```

Watch the console output of the proxy server to see cache hit and miss messages.

## Project Structure

```
.
├── proxy_server_with_cache.c    # Main proxy for Unix/Linux
├── proxy_server_windows.c       # Main proxy for Windows
├── proxy_parse.h                # HTTP parser header
├── proxy_parse.c                # HTTP parser implementation
├── Makefile                     # Build file for Unix/Linux
└── README.md                    # This file
```
