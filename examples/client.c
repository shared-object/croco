#define CROCO_IMPLEMENTATION
#include "../croco.h"
#include <stdio.h>
#include <string.h>

#define PORT 9090

int sock_fd = -1;
int orig_stdin_flags = 0;

void client_receiver(void *arg) {
  int fd = (int)(intptr_t)arg;
  char buf[512];

  while (1) {
    ssize_t n = croco_read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
      printf("\n[Disconnected from server]\n");
      // Restore original terminal flags before exit
      fcntl(STDIN_FILENO, F_SETFL, orig_stdin_flags);
      exit(0);
    }
    buf[n] = '\0';
    printf("%s", buf);
    fflush(stdout);
  }
}

void client_sender(void *arg) {
  int fd = (int)(intptr_t)arg;
  char buf[512];

  while (1) {
    // Asynchronously read from standard input (stdin) via epoll!
    ssize_t n = croco_read(STDIN_FILENO, buf, sizeof(buf) - 1);
    if (n <= 0) {
      break;
    }
    buf[n] = '\0';

    if (strcmp(buf, "/exit\n") == 0 || strcmp(buf, "/quit\n") == 0) {
      printf("[Exiting chat...]\n");
      fcntl(STDIN_FILENO, F_SETFL, orig_stdin_flags);
      close(fd);
      exit(0);
    }

    // Send message to the server
    croco_write(fd, (void *)buf, n);
  }
}

void client_main_coro(void *arg) {
  (void)arg;

  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    perror("socket");
    exit(1);
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
    perror("inet_pton");
    exit(1);
  }

  printf("Connecting to chat server at 127.0.0.1:%d...\n", PORT);
  if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Connection failed. Make sure ./server is running");
    exit(1);
  }

  croco_set_nonblocking(sock_fd);

  // Set terminal stdin to non-blocking mode for async epoll polling:
  orig_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  croco_set_nonblocking(STDIN_FILENO);

  printf("Connected! Type messages and press Enter (or /exit to quit):\n\n");

  // Spawn two concurrent coroutines:
  // 1) Reads incoming broadcast messages from network
  croco_go(client_receiver, (void *)(intptr_t)sock_fd);
  // 2) Reads user typing from keyboard
  croco_go(client_sender, (void *)(intptr_t)sock_fd);
}

int main(void) {
  croco_go(client_main_coro, NULL);
  run_event_loop();

  fcntl(STDIN_FILENO, F_SETFL, orig_stdin_flags);
  return 0;
}
