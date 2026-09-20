# croco 

A tiny, **zero-dependency** asynchronous runtime for C in a single header file (`croco.h`).

Croco brings Go-like concurrency to pure C: lightweight coroutines, buffered CSP channels, and non-blocking network I/O — all under **400 lines of code**.

[Русская версия документации (README_RU.md)](README_RU.md)

---

##  Why Croco?

- **Zero Dependencies**: Requires only standard libc and Linux kernel syscalls. No `libuv`, no `libevent`, no `pthreads`, no external packages.
- **Single Header**: Just drop `croco.h` into your project and `#define CROCO_IMPLEMENTATION` in one C file.
- **Go-Style Concurrency in C**: Spawn tasks with `croco_go`, synchronize with typed channels (`chan_send` / `chan_recv`), and write asynchronous network code that looks clean and synchronous.
- **Zero-CPU Idle**: Driven by Linux `epoll`. If all coroutines are waiting for network packets or timers, the thread sleeps in the kernel with **0.00% CPU usage**.

---

##  Supported Platforms

> [!WARNING]
> **Croco supports Linux on `x86_64` and `aarch64` (ARM64 / Raspberry Pi / AWS Graviton / Docker on Apple Silicon).**  
> It will **NOT** run on Windows or macOS.
> 
> - Hand-written assembly context switches for both **x86_64** and **AArch64**.
> - The I/O engine relies on Linux-specific **`epoll`** and **`accept4`**.

---

##  Core API Cheat Sheet

### 1. Coroutines & Lifecycle
```c
// Launch a new coroutine
Context *croco_go(void (*coro)(void *arg), void *arg);

// Yield CPU time to the scheduler / other tasks
void croco_yield(void);

// Run the event loop (runs until all coroutines terminate)
void run_event_loop(void);
```

### 2. Timers
```c
// Sleep for N milliseconds (integrated with epoll; 0% CPU while sleeping)
void sleep_ms(uint64_t ms);
```

### 3. Channels (CSP Communication)
```c
// Create a buffered channel
Channel *ch = chan_create(capacity);

// Send / receive (automatically yields if buffer is full / empty)
chan_send(ch, (void *)val);
void *val = chan_recv(ch);
```

### 4. Non-Blocking Async Network & Terminal I/O
```c
// Put socket or terminal fd into non-blocking mode
croco_set_nonblocking(fd);

// Asynchronously wait for I/O readiness (EPOLLIN / EPOLLOUT)
void croco_poll(int fd, uint32_t events);

// Read / write / accept (automatically yields on EAGAIN / EWOULDBLOCK)
ssize_t croco_read(int fd, void *buf, size_t count);
ssize_t croco_write(int fd, void *buf, size_t count);
int     croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen);
```

---

##  Examples: Multi-Client Async Chat

The [`examples/`](examples/) directory contains a complete interactive TCP chat demonstrating coroutines, async socket I/O, and async terminal keyboard input simultaneously.

### Build
```bash
make
```
This builds `./server` and `./client` from the `examples/` directory.

### Run in Multiple Terminals
1. **Terminal 1 (Server):**
   ```bash
   ./server
   ```
2. **Terminal 2 (Client 1):**
   ```bash
   ./client
   ```
3. **Terminal 3 (Client 2):**
   ```bash
   ./client
   ```

Type messages and press Enter. Both incoming network traffic and user typing (`stdin`) are polled concurrently without blocking each other.

You can also connect using standard tools:
```bash
nc 127.0.0.1 9090
```

---

##  License

MIT License.
