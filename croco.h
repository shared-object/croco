
#ifndef CROCO_H
#define CROCO_H

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <asm/unistd_64.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#define __NR_io_uring_enter 426
#endif
#define STACK_SIZE 64 * 1024
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
  int64_t io_res;
  void (*entry)(void *arg);
  void *arg;

  struct Context *next;
} Context;

typedef struct {
  int ring_fd;

  unsigned *sq_khead;
  unsigned *sq_ktail;
  unsigned *sq_kring_mask;
  unsigned *sq_array;
  struct io_uring_sqe *sqes;
  size_t sq_ring_sz;
  size_t sqes_sz;

  unsigned *cq_khead;
  unsigned *cq_ktail;
  unsigned *cq_kring_mask;
  struct io_uring_cqe *cqes;
  size_t cq_ring_sz;

  unsigned sq_tail;
} CrocoRing;

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
Context *croco_go_ex(void (*coro)(void *arg), void *arg, size_t stack_size);
void croco_yield(void);
void croco_resume(Context *ctx);
void croco_sleep(uint64_t ms);
void run_event_loop(void);
Channel *chan_create(size_t capacity);
void chan_send(Channel *ch, void *val);
void *chan_recv(Channel *ch);
void sleep_ms(uint64_t ms);
int croco_set_nonblocking(int fd);
ssize_t croco_read(int fd, void *buf, size_t count);
ssize_t croco_write(int fd, void *buf, size_t count);
int croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen);
int croco_connect(int fd, struct sockaddr *addr, socklen_t addrlen);
#endif
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
Context *queue_head = NULL;
Context *queue_tail = NULL;
CrocoRing ring;
int ring_inited = 0;
int active_tasks = 0;

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

Context *croco_go_ex(void (*coro)(void *arg), void *arg, size_t stack_size) {
  Context *context = (Context *)malloc(sizeof(Context));
  size_t total_stack_size;
  void *stack = croco_allocate_stack(stack_size, &total_stack_size);
  context->stack = (uint8_t *)stack;
  context->stack_size = total_stack_size;
  context->sp = (void *)((uintptr_t)context->stack + total_stack_size);

  context->entry = coro;
  context->arg = arg;
  context->finished = 0;
  context->io_res = 0;
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

Context *croco_go(void (*coro)(void *arg), void *arg) {
  return croco_go_ex(coro, arg, STACK_SIZE);
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

  ch->buffer = malloc(capacity * sizeof(void *));

  ch->recv_waiters_head = NULL;
  ch->recv_waiters_tail = NULL;
  ch->send_waiters_head = NULL;
  ch->send_waiters_tail = NULL;

  return ch;
}

int croco_uring_init(unsigned entries) {
  struct io_uring_params p;
  memset(&p, 0, sizeof(p));

  ring.ring_fd = syscall(__NR_io_uring_setup, entries, &p);

  if (ring.ring_fd < 0)
    return -1;

  ring.sq_ring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
  void *sq_ptr =
      mmap(NULL, ring.sq_ring_sz, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_POPULATE, ring.ring_fd, IORING_OFF_SQ_RING);

  if (sq_ptr == MAP_FAILED)
    return -1;

  ring.sq_khead = (unsigned *)((uint8_t *)sq_ptr + p.sq_off.head);
  ring.sq_ktail = (unsigned *)((uint8_t *)sq_ptr + p.sq_off.tail);
  ring.sq_kring_mask = (unsigned *)((uint8_t *)sq_ptr + p.sq_off.ring_mask);
  ring.sq_array = (unsigned *)((uint8_t *)sq_ptr + p.sq_off.array);

  ring.sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
  ring.sqes = (struct io_uring_sqe *)mmap(
      0, ring.sqes_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      ring.ring_fd, IORING_OFF_SQES);
  if (ring.sqes == MAP_FAILED)
    return -1;

  ring.cq_ring_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
  void *cq_ptr =
      mmap(0, ring.cq_ring_sz, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_POPULATE, ring.ring_fd, IORING_OFF_CQ_RING);
  if (cq_ptr == MAP_FAILED)
    return -1;

  ring.cq_khead = (unsigned *)((char *)cq_ptr + p.cq_off.head);
  ring.cq_ktail = (unsigned *)((char *)cq_ptr + p.cq_off.tail);
  ring.cq_kring_mask = (unsigned *)((char *)cq_ptr + p.cq_off.ring_mask);
  ring.cqes = (struct io_uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);

  ring.sq_tail = 0;
  ring_inited = 1;
  return 0;
}

struct io_uring_sqe *croco_uring_get_sqe(void) {
  if (!ring_inited) {
    croco_uring_init(128);
  }

  unsigned next = ring.sq_tail + 1;
  unsigned head = __atomic_load_n(ring.sq_khead, __ATOMIC_ACQUIRE);

  if (next - head > (*ring.sq_kring_mask + 1)) {
    return NULL;
  }

  unsigned idx = ring.sq_tail & *ring.sq_kring_mask;
  struct io_uring_sqe *sqe = &ring.sqes[idx];
  memset(sqe, 0, sizeof(*sqe));

  ring.sq_array[idx] = idx;
  ring.sq_tail = next;
  return sqe;
}

int croco_uring_submit(void) {
  unsigned tail = ring.sq_tail;
  unsigned old_ktail = *ring.sq_ktail;
  unsigned to_submit = tail - old_ktail;
  if (to_submit == 0)
    return 0;

  __atomic_store_n(ring.sq_ktail, tail, __ATOMIC_RELEASE);

  return syscall(__NR_io_uring_enter, ring.ring_fd, to_submit, 0, 0, NULL);
}

void sleep_ms(uint64_t ms) {
  struct __kernel_timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;

  struct io_uring_sqe *sqe = croco_uring_get_sqe();
  sqe->opcode = IORING_OP_TIMEOUT;
  sqe->addr = (uintptr_t)&ts;
  sqe->len = 1;                        // Сработать по истечении времени
  sqe->user_data = (uintptr_t)current; // Метка нашей корутины

  current->waiting_io = 1;
  croco_uring_submit();
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
  struct io_uring_sqe *sqe = croco_uring_get_sqe();

  sqe->opcode = IORING_OP_READ;
  sqe->fd = fd;
  sqe->addr = (uintptr_t)buf;
  sqe->len = count;
  sqe->off = -1ULL;
  sqe->user_data = (uintptr_t)current;
  current->waiting_io = 1;
  croco_uring_submit();
  croco_yield();

  if (current->io_res < 0) {
    errno = -current->io_res;
    return -1;
  }

  return (ssize_t)current->io_res;
}

ssize_t croco_write(int fd, void *buf, size_t count) {
  size_t sent = 0;

  while (sent < count) {
    struct io_uring_sqe *sqe = croco_uring_get_sqe();

    sqe->opcode = IORING_OP_WRITE;
    sqe->fd = fd;
    sqe->addr = (uintptr_t)((const uint8_t *)buf + sent);
    sqe->len = count - sent;
    sqe->off = -1UL;
    sqe->user_data = (uintptr_t)current;

    current->waiting_io = 1;
    croco_uring_submit();
    croco_yield();

    if (current->io_res <= 0) {
      if (current->io_res < 0)
        errno = -current->io_res;
      return (sent > 0) ? (ssize_t)sent : -1;
    }
    sent += (size_t)current->io_res;
  }

  return sent;
}

int croco_accept(int listener_fd, struct sockaddr *addr, socklen_t *addrlen) {
  struct io_uring_sqe *sqe = croco_uring_get_sqe();

  sqe->opcode = IORING_OP_ACCEPT;
  sqe->fd = listener_fd;
  sqe->addr = (uintptr_t)addr;
  sqe->addr2 = (uintptr_t)addrlen;
  sqe->off = -1ULL;
  sqe->accept_flags = SOCK_NONBLOCK | SOCK_CLOEXEC;
  sqe->user_data = (uintptr_t)current;
  current->waiting_io = 1;
  croco_uring_submit();
  croco_yield();

  if (current->io_res < 0) {
    errno = -current->io_res;
    return -1;
  }

  return (int)current->io_res;
}

int croco_connect(int fd, struct sockaddr *addr, socklen_t addrlen) {
  struct io_uring_sqe *sqe = croco_uring_get_sqe();
  sqe->opcode = IORING_OP_CONNECT;
  sqe->fd = fd;
  sqe->addr = (uintptr_t)addr;
  sqe->off = addrlen;
  sqe->user_data = (uintptr_t)current;
  current->waiting_io = 1;

  croco_uring_submit();
  croco_yield();

  if (current->io_res < 0) {
    errno = -current->io_res;
    return -1;
  }
  return 0;
}

void drain_cq() {
  unsigned head = *ring.cq_khead;
  unsigned tail = __atomic_load_n(ring.cq_ktail, __ATOMIC_ACQUIRE);

  while (head != tail) {
    struct io_uring_cqe *cqe = &ring.cqes[head & *ring.cq_kring_mask];

    Context *ctx = (Context *)(uintptr_t)cqe->user_data;
    if (ctx) {
      ctx->io_res = cqe->res;
      ctx->waiting_io = 0;
      QUEUE_PUSH(queue_head, queue_tail, ctx);
    }

    head++;
  }

  __atomic_store_n(ring.cq_khead, head, __ATOMIC_RELEASE);
}

void run_event_loop() {
  if (!ring_inited) {
    croco_uring_init(128);
    ring_inited = 1;
  }

  while (active_tasks > 0) {
    drain_cq();

    if (queue_head == NULL) {
      syscall(__NR_io_uring_enter, ring.ring_fd, 0, 1, IORING_ENTER_GETEVENTS,
              NULL, 0);
      drain_cq();
    }

    Context *ctx;
    QUEUE_POP(queue_head, queue_tail, ctx);

    if (ctx == NULL)
      continue;

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
