#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 128

bool checkServerConnection(int sock)
{
    char temp;
    int bytes = recv(sock, &temp, 1, MSG_PEEK);
    if (bytes <= 0)
    {
        printf("Server is not responding. Exiting.\n");
        close(sock);
        return false;
    }
    return true;
}

int main()
{
    int sock;
    struct sockaddr_in serverAddr;
    char buffer[BUFFER_SIZE];

    /* ===== CREATE SOCKET ===== */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        exit(1);
    }

    /* ===== SERVER ADDRESS ===== */
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("172.24.170.145");

    /* ===== CONNECT ===== */
    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    /* ===== RECEIVE WELCOME MESSAGE ===== */
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer) - 1, 0);
    printf("%s\n", buffer);

    if (strstr(buffer, "GAME_STARTED") != NULL || strstr(buffer, "Game already started") != NULL)
    {
        printf("\nYou cannot join this round.\n");
        close(sock);
        return 0;
    }

    READY_PHASE:
    /* ===== READY PHASE ===== */
    while (1)
    {
        printf("Please type 1 to READY: ");
        fgets(buffer, sizeof(buffer), stdin);

        if (buffer[0] != '1')
        {
            if (!checkServerConnection(sock))
                return 0;
            continue;
        }

        /* Send READY to server */
        send(sock, buffer, strlen(buffer), 0);
        break;
    }

    /* ===== WAIT FOR GAME START ===== */
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0)
        {
            printf("Disconnected from server.\n");
            close(sock);
            return 0;
        }

        printf("%s", buffer);

        if (strstr(buffer, "Game Started") != NULL ||
            strstr(buffer, "Game started") != NULL)
            break;
    }

    /* ===== GAME LOOP (FIXED WITH SELECT) ===== */
    fd_set readfds;

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds); // server
        FD_SET(0, &readfds);    // keyboard

        int maxfd = sock > 0 ? sock : 0;

        select(maxfd + 1, &readfds, NULL, NULL, NULL);

        /* If server sent something */
        if (FD_ISSET(sock, &readfds))
        {
            static char bigBuffer[4096];
            static int len = 0;

            int bytes = recv(sock, bigBuffer + len, sizeof(bigBuffer) - len - 1, 0);
            if (bytes <= 0)
            {
                printf("\nDisconnected from server.\n");
                break;
            }

            len += bytes;
            bigBuffer[len] = '\0';

            char *end;
            while ((end = strstr(bigBuffer, "<<END>>")) != NULL)
            {
                *end = '\0';

                printf("%s", bigBuffer);
                fflush(stdout);

                if (strstr(bigBuffer, "GAME_STOPPED") != NULL)
                {
                    printf("\n=== GAME STOPPED ===\n");
                    printf("Returning to READY state...\n");

                    len = 0;
                    memset(bigBuffer, 0, sizeof(bigBuffer));
                    goto READY_PHASE;
                    break;
                }

                

                len -= (end - bigBuffer) + strlen("<<END>>");
                memmove(bigBuffer, end + strlen("<<END>>"), len);
                bigBuffer[len] = '\0';
            }
        }

        /* If user typed something */
        if (FD_ISSET(0, &readfds))
        {
            fgets(buffer, sizeof(buffer), stdin);
            send(sock, buffer, strlen(buffer), 0);
        }
    }

    close(sock);
    return 0;
}