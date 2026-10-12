# croco

Крошечный асинхронный рантайм для Си **без зависимостей** (zero-dependency) в одном заголовочном файле (`croco.h`).

Croco приносит конкурентность в стиле Go в чистый Си: легковесные корутины (грин-треды), буферизированные CSP-каналы и неблокирующий ввод-вывод для сокетов и файлов — всё это в **~500 строках кода**.

[English version (README.md)](README.md)

---

## :zap: Почему Croco?

- **Zero Dependencies (Никаких сторонних библиотек)**: Нужен только стандартный libc и системные вызовы ядра Linux. Никаких `libuv`, `libevent`, `pthreads` или внешних пакетов.
- **Single Header**: Просто закиньте `croco.h` в проект и объявите `#define CROCO_IMPLEMENTATION` в одном файле `.c`.
- **Go-стиль в Си**: Запускайте задачи через `croco_go`, синхронизируйте их через типизированные каналы (`chan_send` / `chan_recv`), и пишите асинхронный код, который читается как простой синхронный.
- **0% CPU в простое**: Работает на **`io_uring`** (Linux). Когда все задачи спят или ждут I/O, поток честно засыпает в ядре — никакой холостой нагрузки на процессор.

> [!NOTE]
> Croco использует **`io_uring`** (Linux 5.1+) как движок I/O — никакого `epoll`, `poll` или `select`.
> Каждая операция чтения, записи, accept'а и таймера проходит через кольцо отправки/завершения напрямую,
> без промежуточных syscall'ов в быстром пути.

---

## :warning: Поддерживаемые платформы

> [!WARNING]
> **Croco работает на Linux под `x86_64` и `aarch64` (ARM64 / Raspberry Pi / AWS Graviton / Docker на Apple Silicon).**
> Он **НЕ** будет работать на Windows или macOS.
>
> - Ассемблерные вставки для переключения контекста реализованы под **x86_64** и **AArch64**.
> - Движок I/O завязан напрямую на **`io_uring`** (системные вызовы `io_uring_setup` / `io_uring_enter`, ядро ≥ 5.1).

---

## :book: Шпаргалка по базовому API

### 1. Корутины и запуск
```c
// Запуск новой корутины
Context *croco_go(void (*coro)(void *arg), void *arg);

// Уступить квант времени планировщику / другим задачам
void croco_yield(void);

// Запуск event loop (работает, пока не завершатся все живые корутины)
void run_event_loop(void);
```

### 2. Таймеры
```c
// Заснуть на N миллисекунд (реализовано через IORING_OP_TIMEOUT; 0% CPU во время сна)
void sleep_ms(uint64_t ms);
```

### 3. Каналы (Синхронизация CSP)
```c
// Создать буферизированный канал на N элементов
Channel *ch = chan_create(capacity);

// Отправить / принять (автоматически уступает управление, если буфер полон / пуст)
chan_send(ch, (void *)val);
void *val = chan_recv(ch);
```

### 4. Асинхронный ввод-вывод (Сеть и Файлы)
```c
// Перевести слушающий сокет в неблокирующий режим (нужно перед croco_accept)
croco_set_nonblocking(fd);

// Чтение / запись / accept — корутина засыпает, пока io_uring ждёт в ядре
ssize_t croco_read(int fd, void *buf, size_t count);
ssize_t croco_write(int fd, void *buf, size_t count);
int     croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen);
```

Все три функции прозрачно работают с **сокетами, пайпами, stdin/stdout и обычными файлами**.

---

## :speech_balloon: Примеры (Examples)

В папке [`examples/`](examples/) два готовых примера.

### Чат-сервер — `examples/chat/`

Полноценный интерактивный TCP-чат, демонстрирующий совместную работу корутин, сетевых сокетов и асинхронного ввода с клавиатуры:

```
examples/chat/
├── server.c   — чат-сервер с broadcast (по корутине на каждого клиента)
├── client.c   — интерактивный консольный клиент (recv + stdin одновременно)
└── makefile
```

**Сборка и запуск:**
```bash
make -C examples/chat

# Терминал 1
./examples/chat/server

# Терминал 2
./examples/chat/client

# Терминал 3
./examples/chat/client
```

Также можно подключиться через netcat:
```bash
nc 127.0.0.1 9090
```

---

### Асинхронный файловый I/O — `examples/fileio/`

Две корутины параллельно записывают случайные данные в два файла, затем две другие корутины читают их обратно — всё через `io_uring`, без единого блокирующего syscall:

```
examples/fileio/
├── fileio.c
└── makefile
```

**Сборка и запуск:**
```bash
make -C examples/fileio
./examples/fileio/fileio
```

Пример вывода:
```
=== Writing two 100 MB files concurrently ===
[writer-a] wrote 104857600 bytes in 762 ms
[writer-b] wrote 104857600 bytes in 805 ms
--- both writers done in 842 ms ---

=== Reading both files back concurrently ===
[reader-a] read  104857600 bytes in 446 ms  (checksum=13368120752)
[reader-b] read  104857600 bytes in 446 ms  (checksum=13368013908)
--- both readers done in 446 ms ---```

**Собрать все примеры сразу:**
```bash
make
```

---

## :scroll: Лицензия

MIT License.
