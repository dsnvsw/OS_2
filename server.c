#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define MAX_CLIENTS 1
#define PORT 1234

volatile sig_atomic_t wasSigHup = 0;

void sigHupHandler(int r) {
    wasSigHup = 1;
}

void sigHupHandlerRegistration() {
    struct sigaction sa;
    sigaction(SIGHUP, NULL, &sa);
    sa.sa_handler = sigHupHandler;
    sa.sa_flags |= SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
}

int createServerSocket() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }
    return serverSocket;
}

void bindServerSocket(int serverSocket) {
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }
}

void listenOnServerSocket(int serverSocket) {
    if (listen(serverSocket, MAX_CLIENTS) == -1) {
        perror("Error listening socket");
        exit(EXIT_FAILURE);
    }
}

int acceptClientConnection(int serverSocket) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int newClientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);

    if (newClientSocket == -1) {
        perror("New connection accepting error");
    } else {
        printf("New connection accepted from %s:%d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
    }

    return newClientSocket;
}

void closeAllClients(int clients[]) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] != -1) {
            close(clients[i]);
            clients[i] = -1;
        }
    }
}

void handleClientCommunication(int clients[], fd_set* fds) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] != -1 && FD_ISSET(clients[i], fds)) {
            char buffer[1024];
            ssize_t bytesRead = read(clients[i], buffer, sizeof(buffer));

            if (bytesRead > 0) {
                printf("%zd bytes received from client\n", bytesRead);
            } else if (bytesRead == 0) {
                printf("Connection closed by client\n");
                close(clients[i]);
                clients[i] = -1;
            } else {
                perror("Error reading from client");
            }
        }
    }
}

int main() {
    int serverSocket = createServerSocket();
    sigHupHandlerRegistration();

    sigset_t blockedMask, origMask;
    sigemptyset(&blockedMask);
    sigaddset(&blockedMask, SIGHUP);
    sigprocmask(SIG_BLOCK, &blockedMask, &origMask);

    bindServerSocket(serverSocket);
    listenOnServerSocket(serverSocket);

    fd_set fds;
    int maxFd = serverSocket;
    int clients[MAX_CLIENTS];
    memset(clients, -1, sizeof(clients));

    while (1) {
        FD_ZERO(&fds);
        FD_SET(serverSocket, &fds);

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i] != -1) {
                FD_SET(clients[i], &fds);
                maxFd = (maxFd > clients[i]) ? maxFd : clients[i];
            }
        }

        if (pselect(maxFd + 1, &fds, NULL, NULL, NULL, &origMask) == -1) {
            if (errno == EINTR && wasSigHup) {
                wasSigHup = 0;
                printf("Received SIGHUP signal\n");
                closeAllClients(clients);
            } else {
                perror("Error in pselect");
                exit(EXIT_FAILURE);
            }
        }if (FD_ISSET(serverSocket, &fds)) {
            int newClientSocket = acceptClientConnection(serverSocket);
            closeAllClients(clients);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clients[i] == -1) {
                    clients[i] = newClientSocket;
                    break;
                }
            }
        }

        handleClientCommunication(clients, &fds);
    }

    close(serverSocket);
    closeAllClients(clients);

    return 0;
}