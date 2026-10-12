#define CROCO_IMPLEMENTATION
#include "../../croco.h"
#include <stdio.h>
#include <string.h>

#define PORT 9090
#define MAX_CLIENTS 64

int clients[MAX_CLIENTS];
int client_ids[MAX_CLIENTS];
int client_count = 0;
int next_id = 1;

void broadcast(int sender_fd, const char *msg, size_t len) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i] > 0 && clients[i] != sender_fd) {
      croco_write(clients[i], (void *)msg, len);
    }
  }
}

void client_handler(void *arg) {
  int slot = (int)(intptr_t)arg;
  int fd = clients[slot];
  int id = client_ids[slot];

  char buf[512];
  char msg[600];

  snprintf(msg, sizeof(msg), "[Server]: Client #%d joined the chat!\n", id);
  broadcast(fd, msg, strlen(msg));
  printf("[Server]: Client #%d (fd=%d) connected.\n", id, fd);

  const char *welcome = "Welcome to Croco Chat! Type messages and press Enter.\n";
  croco_write(fd, (void *)welcome, strlen(welcome));

  while (1) {
    ssize_t n = croco_read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
      break;
    }
    buf[n] = '\0';

    // Format message: [Client #ID]: text
    snprintf(msg, sizeof(msg), "[Client #%d]: %s", id, buf);
    printf("%s", msg);

    // Broadcast to all other connected clients
    broadcast(fd, msg, strlen(msg));
  }

  printf("[Server]: Client #%d disconnected.\n", id);
  snprintf(msg, sizeof(msg), "[Server]: Client #%d left the chat.\n", id);
  broadcast(fd, msg, strlen(msg));

  close(fd);
  clients[slot] = 0;
  client_ids[slot] = 0;
}

void server_coro(void *arg) {
  (void)arg;

  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    perror("socket");
    return;
  }

  int opt = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listener);
    return;
  }

  if (listen(listener, 32) < 0) {
    perror("listen");
    close(listener);
    return;
  }

  croco_set_nonblocking(listener);

  printf("=========================================\n");
  printf("  Croco Async Chat Server is running!\n");
  printf("  Listening on port %d (127.0.0.1:%d)\n", PORT, PORT);
  printf("=========================================\n");

  while (1) {
    int client_fd = croco_accept(listener, NULL, NULL);
    if (client_fd >= 0) {
      int slot = -1;
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == 0) {
          slot = i;
          break;
        }
      }

      if (slot != -1) {
        clients[slot] = client_fd;
        client_ids[slot] = next_id++;
        croco_go(client_handler, (void *)(intptr_t)slot);
      } else {
        const char *full_msg = "Server is full.\n";
        croco_write(client_fd, (void *)full_msg, strlen(full_msg));
        close(client_fd);
      }
    }
  }
}

int main(void) {
  memset(clients, 0, sizeof(clients));
  memset(client_ids, 0, sizeof(client_ids));

  croco_go(server_coro, NULL);
  run_event_loop();
  return 0;
}
