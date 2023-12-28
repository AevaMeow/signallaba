#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stddef.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int connfd;
    struct sockaddr_in addr;
} client_t;

volatile sig_atomic_t wasSigHup = 0;

void sigHupHandler(int r)
{
    wasSigHup = 1;
}

void setupSigHupHandler(sigset_t *origMask) {
    struct sigaction sa;
    sigaction(SIGHUP, NULL, &sa);
    sa.sa_handler = sigHupHandler;
    sa.sa_flags |= SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);

    sigset_t blockedMask;
    sigemptyset(&blockedMask);
    sigaddset (&blockedMask, SIGHUP);
    sigprocmask(SIG_BLOCK, &blockedMask, origMask);
}

int Server(int port) {
    int sockfd;
    struct sockaddr_in servaddr;
  
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        puts("Socket creation failed\n");
        exit(-1);
    }
    memset(&servaddr, 0, sizeof(servaddr));
  
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
  
    if ((bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) != 0) {
        puts("Socket bind failed\n");
        exit(-1);
    }
  
    if ((listen(sockfd, 5)) != 0) {
        puts("Listen failed\n");
        exit(-1);
    }

    return sockfd;
}

int main() {
    int sockfd = Server(5005);
    puts(" Ожидание подключений...\n");
    client_t clients[3];
    int active_clients = 0;
    char buffer[1024] = {0};

    sigset_t origSigMask;
    setupSigHupHandler(&origSigMask);

    while (1) {
        if (wasSigHup) {
            wasSigHup = 0;
            puts(" Клиенты: ");
            for (int i = 0; i < active_clients; i++) {
                printf("[%s:%d]", inet_ntoa(clients[i].addr.sin_addr), htons(clients[i].addr.sin_port));
                puts(" ");
            }
            puts("\n");
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        int maxFd = sockfd;

        for (int i = 0; i < active_clients; i++) {
            FD_SET(clients[i].connfd, &fds);
            if(clients[i].connfd > maxFd) {
                maxFd = clients[i].connfd;
            }
        }

        if (pselect (maxFd + 1, &fds, NULL, NULL, NULL, &origSigMask) < 0 && errno != EINTR) {
            puts("ошибка pselect\n");
            return -1;
        }
        
        if (FD_ISSET(sockfd, &fds) && active_clients < 3) {
            client_t *client = &clients[active_clients];
            int len = sizeof(client->addr);
            int connfd = accept(sockfd, (struct sockaddr*)&client->addr, &len);
            if (connfd >= 0) {
                client->connfd = connfd;
                printf("[%s:%d]", inet_ntoa(client->addr.sin_addr), htons(client->addr.sin_port));
                puts(" Подключено!\n");
                active_clients++;
            }
            else {
                printf("Ошибка подключения: %s\n", strerror(errno));
            }
        }

        for (int i = 0; i < active_clients; i++) {
            client_t *client = &clients[i];
            if (FD_ISSET(client->connfd, &fds)) {
                int read_len = read(client->connfd, &buffer, 1023);
                if (read_len > 0) {
                    buffer[read_len - 1] = 0;
                    printf("[%s:%d]", inet_ntoa(client->addr.sin_addr), htons(client->addr.sin_port));
                    printf(" %s\n", buffer);
                }
                else {
                    close(client->connfd);
                    printf("[%s:%d]", inet_ntoa(client->addr.sin_addr), htons(client->addr.sin_port));
                    puts(" Соединение закрыто\n");
                    clients[i] = clients[active_clients - 1];
                    active_clients--;
                    i--;
                }
            }
        }
    }
}
