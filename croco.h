
#ifndef CROCO_H
#define CROCO_H

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define STACK_SIZE 64 * 1024
#define MAX_EVENTS 32
#define QUEUE_PUSH(head, tail, node)                                           \
  do {                                                                         \
    (node)->next = NULL;                                                       \
    if ((tail) != NULL) {                                                      \
      (tail)->next = (node);                                                   \
    } else {                                                                   \
      (head) = (node);                                                         \
    }                                                                          \
    (tail) = (node);                                                           \
  } while (0)

#define QUEUE_POP(head, tail, out_node)                                        \
  do {                                                                         \
    (out_node) = (head);                                                       \
    if ((head) != NULL) {                                                      \
      (head) = (head)->next;                                                   \
      if ((head) == NULL) {                                                    \
        (tail) = NULL;                                                         \
      }                                                                        \
    }                                                                          \
  } while (0)

typedef struct Context {
  void *sp;
  size_t stack_size;
  uint8_t *stack;
  struct Context *caller;
  int finished;
  int waiting_io;
  void (*entry)(void *arg);
  void *arg;

  uint64_t wake_up_at;

  struct Context *next;
} Context;

typedef struct Channel {
  void **buffer;
  size_t capacity;
  size_t count;
  size_t head;
  size_t tail;

  Context *recv_waiters_head;
  Context *recv_waiters_tail;
  Context *send_waiters_tail;
  Context *send_waiters_head;
} Channel;

extern void switch_context(void **old_sp, void *new_sp);
Context *croco_go(void (*coro)(void *arg), void *arg);
void croco_yield(void);
void croco_resume(Context *ctx);
void croco_sleep(uint64_t ms);
void run_event_loop(void);
Channel *chan_create(size_t capacity);
void chan_send(Channel *ch, void *val);
void *chan_recv(Channel *ch);
void sleep_ms(uint64_t ms);
void croco_poll(int fd, uint32_t events);
int croco_set_nonblocking(int fd);
ssize_t croco_read(int fd, void *buf, size_t count);
ssize_t croco_write(int fd, void *buf, size_t count);
int croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen);

#endif
#define CROCO_IMPLEMENTATION // TODO: remove it
#ifdef CROCO_IMPLEMENTATION
#if defined(__x86_64__)
__asm__(".intel_syntax noprefix\n"
        ".global switch_context\n"
        "switch_context:\n"
        "    push rbx\n"
        "    push rbp\n"
        "    push r12\n"
        "    push r13\n"
        "    push r14\n"
        "    push r15\n"
        "    mov [rdi], rsp\n"
        "    mov rsp, rsi\n"
        "    pop r15\n"
        "    pop r14\n"
        "    pop r13\n"
        "    pop r12\n"
        "    pop rbp\n"
        "    pop rbx\n"
        "    ret\n"
        ".att_syntax prefix\n");

#elif defined(__aarch64__)
__asm__(".global switch_context\n"
        "switch_context:\n"
        "    stp x19, x20, [sp, #-16]!\n"
        "    stp x21, x22, [sp, #-16]!\n"
        "    stp x23, x24, [sp, #-16]!\n"
        "    stp x25, x26, [sp, #-16]!\n"
        "    stp x27, x28, [sp, #-16]!\n"
        "    stp x29, x30, [sp, #-16]!\n"
        "    stp d8,  d9,  [sp, #-16]!\n"
        "    stp d10, d11, [sp, #-16]!\n"
        "    stp d12, d13, [sp, #-16]!\n"
        "    stp d14, d15, [sp, #-16]!\n"
        "    mov x2, sp\n"
        "    str x2, [x0]\n" // Сохраняем старый SP в *old_sp (x0)
        "    mov sp, x1\n"   // Загружаем new_sp из x1
        "    ldp d14, d15, [sp], #16\n"
        "    ldp d12, d13, [sp], #16\n"
        "    ldp d10, d11, [sp], #16\n"
        "    ldp d8,  d9,  [sp], #16\n"
        "    ldp x29, x30, [sp], #16\n"
        "    ldp x27, x28, [sp], #16\n"
        "    ldp x25, x26, [sp], #16\n"
        "    ldp x23, x24, [sp], #16\n"
        "    ldp x21, x22, [sp], #16\n"
        "    ldp x19, x20, [sp], #16\n"
        "    ret\n");
#endif
Context main_ctx;
Context *current = &main_ctx;
Context **contexts = NULL;
Context *queue_head = NULL;
Context *queue_tail = NULL;
int epoll_fd = -1;
int active_tasks = 0;

uint64_t get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void croco_trampoline(void) {
  current->entry(current->arg);

  current->finished = 1;
  croco_yield();
}

void *croco_allocate_stack(size_t stack_size, size_t *out_total_stack_size) {
  size_t page_size = getpagesize();
  size_t total_size = stack_size + page_size;

  void *mem = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE | MAP_STACK, -1, 0);

  if (mem == MAP_FAILED) {
    perror("mmap failed");
    exit(1);
  }

  mprotect(mem, page_size, PROT_NONE);

  if (out_total_stack_size) {
    *out_total_stack_size = total_size;
  }

  return mem;
}

void croco_free_stack(void *mem, size_t stack_size) { munmap(mem, stack_size); }

Context *croco_go(void (*coro)(void *arg), void *arg) {
  Context *context = (Context *)malloc(sizeof(Context));
  size_t total_stack_size;
  void *stack = croco_allocate_stack(STACK_SIZE, &total_stack_size);
  context->stack = (uint8_t *)stack;
  context->stack_size = total_stack_size;
  context->sp = (void *)((uintptr_t)context->stack + total_stack_size);

  context->entry = coro;
  context->arg = arg;
  context->finished = 0;
  context->wake_up_at = 0;
  context->waiting_io = 0;

  uint64_t *sp = (uint64_t *)context->sp;
#if defined(__x86_64__)
  *(--sp) = 0;
  *(--sp) = (uint64_t)croco_trampoline;

  for (int i = 0; i < 6; i++) {
    *(--sp) = 0;
  }
#elif defined(__aarch64__)
  sp -= 20;
  for (int i = 0; i < 20; i++) {
    sp[i] = 0;
  }
  sp[9] = (uint64_t)croco_trampoline;
#endif

  context->sp = sp;

  QUEUE_PUSH(queue_head, queue_tail, context);

  active_tasks++;

  return context;
}

void croco_yield() {
  Context *target = current->caller;
  Context *prev = current;

  current = target;
  switch_context(&prev->sp, target->sp);
}
void croco_resume(Context *ctx) {
  ctx->caller = current;
  Context *prev = current;

  current = ctx;

  switch_context(&prev->sp, ctx->sp);
}

void sleep_ms(uint64_t ms) {
  current->wake_up_at = get_time_ms() + ms;
  croco_yield();
}

void chan_send(Channel *ch, void *val) {
  while (ch->count == ch->capacity) {
    QUEUE_PUSH(ch->send_waiters_head, ch->send_waiters_tail, current);
    croco_yield();
  }

  ch->buffer[ch->tail] = val;
  ch->tail = (ch->tail + 1) % ch->capacity;
  ch->count++;

  if (ch->recv_waiters_head != NULL) {
    Context *ctx;
    QUEUE_POP(ch->recv_waiters_head, ch->recv_waiters_tail, ctx);
    QUEUE_PUSH(queue_head, queue_tail, ctx);
  }
}

void *chan_recv(Channel *ch) {
  while (ch->count == 0) {
    QUEUE_PUSH(ch->recv_waiters_head, ch->recv_waiters_tail, current);
    croco_yield();
  }

  void *result = ch->buffer[ch->head];
  ch->head = (ch->head + 1) % ch->capacity;
  ch->count--;

  if (ch->send_waiters_head != NULL) {
    Context *ctx;
    QUEUE_POP(ch->send_waiters_head, ch->send_waiters_tail, ctx);
    QUEUE_PUSH(queue_head, queue_tail, ctx);
  }

  return result;
}

Channel *chan_create(size_t capacity) {
  Channel *ch = (Channel *)malloc(sizeof(Channel));
  ch->capacity = capacity;
  ch->count = 0;
  ch->head = 0;
  ch->tail = 0;

  // Выделяем массив под указатели нужной вместимости:
  ch->buffer = malloc(capacity * sizeof(void *));

  // Очереди ждущих пока пусты:
  ch->recv_waiters_head = NULL;
  ch->recv_waiters_tail = NULL;
  ch->send_waiters_head = NULL;
  ch->send_waiters_tail = NULL;

  return ch;
}

void croco_poll(int fd, uint32_t events) {
  struct epoll_event ev;
  ev.events = events | EPOLLONESHOT;

  ev.data.ptr = current;

  if (epoll_fd < 0) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  }

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    if (errno == EEXIST) {
      epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }
  }

  ((Context *)ev.data.ptr)->waiting_io = 1;

  croco_yield();
}

int croco_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  if (flags == -1) {
    return -1;
  }

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ssize_t croco_read(int fd, void *buf, size_t count) {
  while (1) {
    ssize_t n = read(fd, buf, count);

    if (n >= 0)
      return n;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      croco_poll(fd, EPOLLIN);
      continue;
    }

    return -1;
  }
}

ssize_t croco_write(int fd, void *buf, size_t count) {
  size_t sent = 0;

  while (sent < count) {
    ssize_t n = write(fd, (const uint8_t *)buf + sent, count - sent);

    if (n > 0) {
      sent += n;
      continue;
    }

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      croco_poll(fd, EPOLLOUT);
      continue;
    }

    return -1;
  }

  return sent;
}

int croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen) {
  while (1) {
    int client_fd =
        accept4(listener_fd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (client_fd >= 0)
      return client_fd;

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      croco_poll(listener_fd, EPOLLIN);
      continue;
    }

    return -1;
  }
}

void run_event_loop() {
  if (epoll_fd == -1) {
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  }

  struct epoll_event events[MAX_EVENTS];

  while (active_tasks > 0) {
    int timeout = (queue_head == NULL) ? 100 : 0;
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);

    for (int i = 0; i < nfds; i++) {
      Context *ready_ctx = (Context *)events[i].data.ptr;
      ready_ctx->waiting_io = 0;
      QUEUE_PUSH(queue_head, queue_tail, ready_ctx);
    }

    if (queue_head == NULL)
      continue;

    Context *ctx;
    QUEUE_POP(queue_head, queue_tail, ctx);

    uint64_t now = get_time_ms();

    if (ctx->wake_up_at > now) {
      uint64_t diff = ctx->wake_up_at - now;

      QUEUE_PUSH(queue_head, queue_tail, ctx);

      if (queue_head == ctx) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, diff);

        for (int i = 0; i < nfds; i++) {
          Context *ioctx = (Context *)events[i].data.ptr;
          ioctx->waiting_io = 0;
          QUEUE_PUSH(queue_head, queue_tail, ioctx);
        }
      }

      continue;
    }

    ctx->wake_up_at = 0;

    croco_resume(ctx);

    if (ctx->waiting_io)
      continue;

    if (!ctx->finished) {
      QUEUE_PUSH(queue_head, queue_tail, ctx);
    } else {
      croco_free_stack(ctx->stack, ctx->stack_size);
      free(ctx);
      active_tasks--;
    }
  }
}

#endif
