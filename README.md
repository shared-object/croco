# croco

A tiny, **zero-dependency** asynchronous runtime for C in a single header file (`croco.h`).

Croco brings Go-like concurrency to pure C: lightweight coroutines, buffered CSP channels, and non-blocking I/O for sockets and files — all under **~500 lines of code**.

[Русская версия документации (README_RU.md)](README_RU.md)

---

## :zap: Why Croco?

- **Zero Dependencies**: Requires only standard libc and Linux kernel syscalls. No `libuv`, no `libevent`, no `pthreads`, no external packages.
- **Single Header**: Just drop `croco.h` into your project and `#define CROCO_IMPLEMENTATION` in one C file.
- **Go-Style Concurrency in C**: Spawn tasks with `croco_go`, synchronize with typed channels (`chan_send` / `chan_recv`), and write asynchronous code that looks clean and synchronous.
- **Zero-CPU Idle**: Driven by Linux **`io_uring`**. If all coroutines are waiting for I/O or timers, the thread sleeps in the kernel with **0.00% CPU usage**.

> [!NOTE]
> Croco uses **`io_uring`** (Linux 5.1+) as its I/O engine — no `epoll`, no `poll`, no `select`.
> Every read, write, accept, and timer goes through the submission/completion ring directly,
> with zero intermediate syscalls per operation in the fast path.

---

## :warning: Supported Platforms

> [!WARNING]
> **Croco supports Linux on `x86_64` and `aarch64` (ARM64 / Raspberry Pi / AWS Graviton / Docker on Apple Silicon).**
> It will **NOT** run on Windows or macOS.
>
> - Hand-written assembly context switches for both **x86_64** and **AArch64**.
> - The I/O engine relies on Linux-specific **`io_uring`** (`io_uring_setup` / `io_uring_enter` syscalls, kernel ≥ 5.1).

---

## :book: Core API Cheat Sheet

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
// Sleep for N milliseconds (backed by io_uring IORING_OP_TIMEOUT; 0% CPU while sleeping)
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

### 4. Async Network & File I/O
```c
// Put a listener socket into non-blocking mode (needed before croco_accept)
croco_set_nonblocking(fd);

// Read / write / accept — yield to scheduler while io_uring waits in the kernel
ssize_t croco_read(int fd, void *buf, size_t count);
ssize_t croco_write(int fd, void *buf, size_t count);
int     croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen);
```

All three work transparently on **sockets, pipes, stdin/stdout, and regular files**.

---

## :speech_balloon: Examples

The [`examples/`](examples/) directory contains two ready-to-run demos.

### Chat Server — `examples/chat/`

A complete interactive multi-client TCP chat demonstrating coroutines, async socket I/O, and async terminal keyboard input simultaneously.

```
examples/chat/
├── server.c   — broadcast chat server (one coroutine per client)
├── client.c   — interactive console client (concurrent recv + stdin)
└── makefile
```

**Build & run:**
```bash
make -C examples/chat

# Terminal 1
./examples/chat/server

# Terminal 2
./examples/chat/client

# Terminal 3
./examples/chat/client
```

You can also connect with netcat:
```bash
nc 127.0.0.1 9090
```

---

### Async File I/O — `examples/fileio/`

Two coroutines concurrently write 100 MB of random data to two separate files, then two coroutines read them back — all through `io_uring`, zero blocking syscalls.

```
examples/fileio/
├── fileio.c
└── makefile
```

**Build & run:**
```bash
make -C examples/fileio
./examples/fileio/fileio
```

Sample output:
```
=== Writing two 100 MB files concurrently ===
[writer-a] wrote 104857600 bytes in 762 ms
[writer-b] wrote 104857600 bytes in 805 ms
--- both writers done in 842 ms ---

=== Reading both files back concurrently ===
[reader-a] read  104857600 bytes in 446 ms  (checksum=13368120752)
[reader-b] read  104857600 bytes in 446 ms  (checksum=13368013908)
--- both readers done in 446 ms ---
```

**Build all examples at once:**
```bash
make
```

---

## :scroll: License

MIT License.
