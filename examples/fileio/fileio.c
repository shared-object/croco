/*
 * fileio.c — async concurrent file I/O with croco
 *
 * Two coroutines run concurrently:
 *   writer_a: reads /dev/urandom, writes 4MB to /tmp/croco_a.bin
 *   writer_b: reads /dev/urandom, writes 4MB to /tmp/croco_b.bin
 *
 * While one is blocked on io_uring, the other runs — true concurrency
 * on a single thread with zero blocking syscalls from userspace.
 *
 * Then two reader coroutines read both files back concurrently.
 */

#define CROCO_IMPLEMENTATION
#include "../../croco.h"
#include <stdio.h>
#include <time.h>

#define FILE_A    "/tmp/croco_a.bin"
#define FILE_B    "/tmp/croco_b.bin"
#define CHUNK     (8 * 1024)            /* 8 KB per io_uring op   */
#define FILESIZE  (100 * 1024 * 1024)    /* 100 MB total per file    */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------ */

typedef struct {
    const char *path;
    const char *label;
} FileArg;

void writer_coro(void *arg) {
    FileArg *a = (FileArg *)arg;

    int dst = open(a->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) { perror("open dst"); return; }

    int rnd = open("/dev/urandom", O_RDONLY);
    if (rnd < 0) { perror("open urandom"); close(dst); return; }

    char buf[CHUNK];
    size_t written = 0;
    uint64_t t0 = now_ms();

    while (written < FILESIZE) {
        size_t want = CHUNK;
        if (written + want > FILESIZE)
            want = FILESIZE - written;

        /* async read from /dev/urandom */
        ssize_t n = croco_read(rnd, buf, want);
        if (n <= 0) break;

        /* async write to file — yields to scheduler while kernel does I/O */
        ssize_t w = croco_write(dst, buf, (size_t)n);
        if (w <= 0) break;

        written += (size_t)w;
    }

    printf("[%s] wrote %zu bytes in %lu ms\n", a->label, written, now_ms() - t0);
    close(dst);
    close(rnd);
}

void reader_coro(void *arg) {
    FileArg *a = (FileArg *)arg;

    int fd = open(a->path, O_RDONLY);
    if (fd < 0) { perror("open read"); return; }

    char buf[CHUNK];
    size_t total = 0;
    uint64_t checksum = 0;
    uint64_t t0 = now_ms();

    while (1) {
        ssize_t n = croco_read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        /* trivial checksum just to prevent the compiler from optimizing away */
        for (ssize_t i = 0; i < n; i++)
            checksum += (unsigned char)buf[i];

        total += (size_t)n;
    }

    printf("[%s] read  %zu bytes in %lu ms  (checksum=%lu)\n",
           a->label, total, now_ms() - t0, checksum);
    close(fd);
}

/* ------------------------------------------------------------------ */

int main(void) {
    static FileArg wa = { FILE_A, "writer-a" };
    static FileArg wb = { FILE_B, "writer-b" };
    static FileArg ra = { FILE_A, "reader-a" };
    static FileArg rb = { FILE_B, "reader-b" };

    printf("=== Writing two 100 MB files concurrently ===\n");
    uint64_t t0 = now_ms();

    croco_go(writer_coro, &wa);
    croco_go(writer_coro, &wb);
    run_event_loop();

    printf("--- both writers done in %lu ms ---\n\n", now_ms() - t0);

    printf("=== Reading both files back concurrently ===\n");
    t0 = now_ms();

    croco_go(reader_coro, &ra);
    croco_go(reader_coro, &rb);
    run_event_loop();

    printf("--- both readers done in %lu ms ---\n", now_ms() - t0);
    return 0;
}
