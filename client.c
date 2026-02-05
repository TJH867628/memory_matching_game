#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 128

bool checkServerConnection(int sock) {
    char temp;
    int bytes = recv(sock, &temp, 1, MSG_PEEK); 
    if (bytes <= 0) {
        printf("Server is not responding. Exiting.\n");
        close(sock);
        return false;
    }
    return true;
}

int main() {
    int sock;
    struct sockaddr_in serverAddr;
    char buffer[BUFFER_SIZE];

    /* ===== CREATE SOCKET ===== */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    /* ===== SERVER ADDRESS ===== */
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("172.24.170.145");

    /* ===== CONNECT ===== */
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    /* ===== RECEIVE WELCOME MESSAGE ===== */
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer) - 1, 0);
    printf("%s\n", buffer);

    /* ===== READY PHASE ===== */
    while (1) {
        printf("Please type 1 to READY: ");
        fgets(buffer, sizeof(buffer), stdin);

        if (buffer[0] != '1') {
            if (!checkServerConnection(sock)) return 0;
            continue;
        }

        /* Send READY to server */
        send(sock, buffer, strlen(buffer), 0);
        break;
    }

    /* ===== WAIT FOR GAME START ===== */
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            printf("Disconnected from server.\n");
            close(sock);
            return 0;
        }

        printf("%s", buffer);

        if (strstr(buffer, "Game Started") != NULL ||
            strstr(buffer, "Game started") != NULL)
            break;
    }

    /* ===== GAME LOOP ===== */
    while (1) {
        printf("Enter move (e.g., FLIP 2): ");
        fgets(buffer, sizeof(buffer), stdin);

        send(sock, buffer, strlen(buffer), 0);

        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            printf("Disconnected from server.\n");
            break;
        }

        printf("%s", buffer);
    }

    close(sock);
    return 0;
}