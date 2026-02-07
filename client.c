#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 128
#define BOARD_ROWS 3
#define BOARD_COLS 4
#define TOTAL_CARDS (BOARD_ROWS * BOARD_COLS)

static void updateMatchedFromBoard(const char *msg, bool matched[])
{
    for (int i = 0; i < TOTAL_CARDS; i++)
        matched[i] = false;

    const char *p = msg;
    int idx = 0;
    while (idx < TOTAL_CARDS)
    {
        p = strstr(p, "Values:");
        if (!p)
            break;
        p += strlen("Values:");

        for (int c = 0; c < BOARD_COLS && idx < TOTAL_CARDS; c++)
        {
            p = strchr(p, '[');
            if (!p)
                return;
            if (p[1] == 'X' && p[2] == 'X')
                matched[idx] = true;
            idx++;
            p++;
        }
    }
}

static void printPickPrompt(int pickCardCount)
{
    if (pickCardCount <= 0)
        printf("Enter card index: ");
    else if (pickCardCount == 1)
        printf("Enter second card: ");
}

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
    static char bigBuffer[10000]; // for the board
    static int len = 0;
    int playerTurn = -1;
    bool myTurn = false;
    int myPlayerID = -1;
    bool readyMode = false;
    bool ignoreWaiting = false;
    bool pendingTurnMsg = false;
    int lastAnnouncedTurn = -1;
    bool lastAnnouncedMyTurn = false;
    bool matchedCards[TOTAL_CARDS] = {false};

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
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* ===== CONNECT ===== */
    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    /* ===== RECEIVE WELCOME MESSAGE (FRAMED BY <<END>>) ===== */
    bool gotPlayerID = false;
    bigBuffer[0] = '\0';
    len = 0;
    while (!gotPlayerID)
    {
        int bytes = recv(sock, bigBuffer + len, sizeof(bigBuffer) - len - 1, 0);
        if (bytes <= 0)
        {
            printf("Disconnected from server.\n");
            close(sock);
            return 0;
        }
        len += bytes;
        bigBuffer[len] = '\0';

        char *msgPtr;
        while ((msgPtr = strstr(bigBuffer, "<<END>>")) != NULL)
        {
            *msgPtr = '\0';
            char *currentMsg = bigBuffer;

            if (strstr(currentMsg, "GAME_STARTED") != NULL || strstr(currentMsg, "Game already started") != NULL)
            {
                printf("\nYou cannot join this round.\n");
                close(sock);
                return 0;
            }

            char *playerID = strstr(currentMsg, "PLAYER ID");
            if (playerID != NULL && sscanf(playerID, "PLAYER ID %d", &myPlayerID) == 1)
            {
                gotPlayerID = true;
            }

            if (*currentMsg != '\0')
            {
                printf("%s\n", currentMsg);
            }

            int dLen = 7;
            int processedLen = (msgPtr + dLen) - bigBuffer;
            int remaining = len - processedLen;
            if (remaining > 0)
                memmove(bigBuffer, msgPtr + dLen, remaining);
            len = remaining;
            bigBuffer[len] = '\0';
        }
    }

    len = 0;
    bigBuffer[0] = '\0';

    while (1)
    {
        printf("Enter name (no spaces): ");
        fflush(stdout);
        if (!fgets(buffer, sizeof(buffer), stdin))
            return 0;
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (buffer[0] == '\0' || strchr(buffer, ' ') != NULL || strchr(buffer, '\t') != NULL)
        {
            printf("Invalid name. Use letters/numbers without spaces.\n");
            continue;
        }
        char nameMsg[BUFFER_SIZE + 8];
        snprintf(nameMsg, sizeof(nameMsg), "NAME %s\n", buffer);
        send(sock, nameMsg, strlen(nameMsg), 0);

        len = 0;
        bigBuffer[0] = '\0';
        while (1)
        {
            int bytes = recv(sock, bigBuffer + len, sizeof(bigBuffer) - len - 1, 0);
            if (bytes <= 0)
            {
                printf("Disconnected from server.\n");
                close(sock);
                return 0;
            }
            len += bytes;
            bigBuffer[len] = '\0';

            char *msgPtr;
            while ((msgPtr = strstr(bigBuffer, "<<END>>")) != NULL)
            {
                *msgPtr = '\0';
                char *currentMsg = bigBuffer;

                if (strstr(currentMsg, "NAME_TAKEN") != NULL)
                {
                    printf("Name already taken. Please choose another.\n");
                    int dLen = 7;
                    int processedLen = (msgPtr + dLen) - bigBuffer;
                    int remaining = len - processedLen;
                    if (remaining > 0)
                        memmove(bigBuffer, msgPtr + dLen, remaining);
                    len = remaining;
                    bigBuffer[len] = '\0';
                    goto RETRY_NAME;
                }

                if (*currentMsg != '\0')
                {
                    printf("%s\n", currentMsg);
                }

                int dLen = 7;
                int processedLen = (msgPtr + dLen) - bigBuffer;
                int remaining = len - processedLen;
                if (remaining > 0)
                    memmove(bigBuffer, msgPtr + dLen, remaining);
                len = remaining;
                bigBuffer[len] = '\0';
                goto NAME_ACCEPTED;
            }
        }

RETRY_NAME:
        continue;

NAME_ACCEPTED:
        break;
    }

READY_PHASE:
    /* ===== READY PHASE ===== */
    while (1)
    {
        printf("Please type 1 to READY:");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin))
            return 0;

        buffer[strcspn(buffer, "\r\n")] = '\0';
        char *p = buffer;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!(p[0] == '1' && p[1] == '\0'))
        {
            printf("Unexpected Input. Please type exactly 1.\n\n");
            continue;
        }

        /* Send READY to server */
        send(sock, buffer, strlen(buffer), 0);
        break;
    }
    /* Send READY to server */
    bool gameStarted = false;
    bool pendingBoardMsg = false;
    char pendingBoard[4096];
    pendingBoard[0] = '\0';

    /* ===== WAIT FOR GAME START ===== */
    while (1)
    {
        int bytes = recv(sock, bigBuffer + len, sizeof(bigBuffer) - len - 1, 0);

        if (bytes <= 0)
        {
            printf("Disconnected from server.\n");
            close(sock);
            return 0;
        }

        len += bytes;
        bigBuffer[len] = '\0';

        char *msgPtr;
        while ((msgPtr = strstr(bigBuffer, "<<END>>")) != NULL)
        {
            *msgPtr = '\0';
            char *currentMsg = bigBuffer;

            if (strstr(currentMsg, "GAME_STOPPED") != NULL)
            {
                printf("%s\n", currentMsg);
                myTurn = false;
                readyMode = true;
                playerTurn = -1;
                ignoreWaiting = true;
                gameStarted = false;
                len = 0;
                bigBuffer[0] = '\0';
                goto READY_PHASE;
            }

            if (strstr(currentMsg, "GAME STARTED") != NULL)
            {
                gameStarted = true;
                readyMode = false;
            }

            if (strstr(currentMsg, "Board State") != NULL)
            {
                strncpy(pendingBoard, currentMsg, sizeof(pendingBoard) - 1);
                pendingBoard[sizeof(pendingBoard) - 1] = '\0';
                pendingBoardMsg = true;
                if (gameStarted)
                {
                    printf("\033[H\033[J%s\n", pendingBoard);
                    updateMatchedFromBoard(pendingBoard, matchedCards);
                }
            }

            char *turnIndicator = strstr(currentMsg, "PLAYER TURN");
            if (turnIndicator)
            {
                if (sscanf(turnIndicator, "PLAYER TURN %d", &playerTurn) == 1)
                {
                    myTurn = (playerTurn == myPlayerID);
                    if (gameStarted)
                    {
                        if (playerTurn != lastAnnouncedTurn || myTurn != lastAnnouncedMyTurn)
                        {
                            if (myTurn)
                            {
                                printf("\n*** YOUR TURN ***\nEnter card index: ");
                            }
                            else
                            {
                                printf("\nWaiting for Player %d...\n", playerTurn);
                            }
                            fflush(stdout);
                            lastAnnouncedTurn = playerTurn;
                            lastAnnouncedMyTurn = myTurn;
                        }
                    }
                    else
                    {
                        pendingTurnMsg = true;
                    }
                }
            }

            if (!gameStarted)
            {
                printf("%s\n", currentMsg);
            }

            int dLen = 7;
            int processedLen = (msgPtr + dLen) - bigBuffer;
            int remaining = len - processedLen;
            if (remaining > 0)
                memmove(bigBuffer, msgPtr + dLen, remaining);
            len = remaining;
            bigBuffer[len] = '\0';
        }

        if (gameStarted)
            break;
    }

    if (pendingBoardMsg && !gameStarted)
    {
        printf("\033[H\033[J%s\n", pendingBoard);
        updateMatchedFromBoard(pendingBoard, matchedCards);
        pendingBoardMsg = false;
    }

    if (pendingTurnMsg)
    {
        if (myTurn)
        {
            printf("\n*** YOUR TURN ***\nEnter card index: ");
        }
        else
        {
            printf("\nWaiting for Player %d...\n", playerTurn);
        }
        fflush(stdout);
        pendingTurnMsg = false;
        lastAnnouncedTurn = playerTurn;
        lastAnnouncedMyTurn = myTurn;
    }
    len = 0;
    bigBuffer[0] = '\0';
    /* ===== GAME LOOP ===== */

    int pickCardCount = 0;
    int firstPickIndex = -1;
    int secondPickIndex = -1;
    while (1)
    {
        char *msgPtr;
        int dLen = strlen("<<END>>");
        // 1. Setup 'select' to watch both the socket AND stdin
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        // Wait for activity on either the socket or the keyboard
        if (select(sock + 1, &readfds, NULL, NULL, NULL) < 0)
            break;

        if (FD_ISSET(sock, &readfds))
        {
            int bytes = recv(sock, bigBuffer + len, sizeof(bigBuffer) - len - 1, 0);

            if (bytes <= 0)
            {
                printf("Disconnected from server.\n");
                break;
            }

            len += bytes;
            bigBuffer[len] = '\0';
        }

        while ((msgPtr = strstr(bigBuffer, "<<END>>")) != NULL)
        {
            *msgPtr = '\0';
            char *currentMsg = bigBuffer;
            bool handled = false;

            if (strstr(currentMsg, "Board State") != NULL)
            {
                printf("\033[H\033[J%s\n", currentMsg);
                updateMatchedFromBoard(currentMsg, matchedCards);
                handled = true;
                if (myTurn && pickCardCount == 1)
                {
                    printPickPrompt(pickCardCount);
                    fflush(stdout);
                }
            }

            if (strstr(currentMsg, "Cards ") != NULL && strstr(currentMsg, " match") != NULL)
            {
                int a = -1;
                int b = -1;
                if (sscanf(currentMsg, "Cards %d and %d match", &a, &b) == 2)
                {
                    if (a >= 0 && a < TOTAL_CARDS)
                        matchedCards[a] = true;
                    if (b >= 0 && b < TOTAL_CARDS)
                        matchedCards[b] = true;
                }
            }

            // NEW: Handle Error Messages from server
            if (strstr(currentMsg, "Invalid") != NULL ||
                strstr(currentMsg, "already") != NULL ||
                strstr(currentMsg, "not your turn") != NULL)
            {
                printf("\n[SERVER ERROR]: %s\n", currentMsg);
                handled = true;
                // Decrease pickCardCount because the server rejected the last pick
                if (pickCardCount > 0)
                    pickCardCount--;
                if (myTurn)
                {
                    printPickPrompt(pickCardCount);
                    fflush(stdout);
                }
            }

            if (strstr(currentMsg, "GAME_STOPPED") != NULL)
            {
                myTurn = false;
                pickCardCount = 0;
                readyMode = true;
                playerTurn = -1;
                ignoreWaiting = true;
                handled = true;
                printf("%s\n", currentMsg);
                printf("Please type 1 to READY: ");
                fflush(stdout);
                len = 0;
                bigBuffer[0] = '\0';
                break;
            }

            // Use strstr to find "PLAYER TURN" anywhere in the current segment
            char *turnIndicator = strstr(currentMsg, "PLAYER TURN");
            if (turnIndicator)
            {
                if (sscanf(turnIndicator, "PLAYER TURN %d", &playerTurn) == 1)
                {
                    myTurn = (playerTurn == myPlayerID);
                    handled = true;
                    ignoreWaiting = false;
                    readyMode = false;
                    if (playerTurn != lastAnnouncedTurn || myTurn != lastAnnouncedMyTurn)
                    {
                        pickCardCount = 0;
                        if (myTurn)
                        {
                            printf("\n*** YOUR TURN ***\nEnter card index: ");
                        }
                        else
                        {
                            printf("\nWaiting for Player %d...\n", playerTurn);
                        }
                        fflush(stdout);
                        lastAnnouncedTurn = playerTurn;
                        lastAnnouncedMyTurn = myTurn;
                    }
                }
            }

            if (!handled)
            {
                while (*currentMsg == '\n' || *currentMsg == '\r' || *currentMsg == ' ' || *currentMsg == '\t')
                    currentMsg++;
                if (*currentMsg != '\0')
                {
                    if (ignoreWaiting && strstr(currentMsg, "Waiting for Player") != NULL)
                    {
                        // Ignore stale waiting messages after GAME_STOPPED
                    }
                    else
                    {
                        printf("%s\n", currentMsg);
                    }
                }
            }

            // Shift buffer
            int dLen = 7; // strlen("<<END>>")
            int processedLen = (msgPtr + dLen) - bigBuffer;
            int remaining = len - processedLen;
            if (remaining > 0)
                memmove(bigBuffer, msgPtr + dLen, remaining);
            len = remaining;
            bigBuffer[len] = '\0';
        }

        /* --- CASE B: USER INPUT --- */
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            if (fgets(buffer, sizeof(buffer), stdin))
            {
                if (readyMode)
                {
                    buffer[strcspn(buffer, "\r\n")] = '\0';
                    char *p = buffer;
                    while (*p == ' ' || *p == '\t')
                        p++;
                    if (p[0] == '1' && p[1] == '\0')
                    {
                        send(sock, buffer, strlen(buffer), 0);
                        readyMode = false;
                    }
                    else
                    {
                        printf("Please type exactly 1 to READY: ");
                        fflush(stdout);
                    }
                    continue;
                }

                if (pickCardCount >= 2)
                {
                    printf("Please wait for the next turn...\n");
                    fflush(stdout);
                    continue;
                }
                int val;
                // Check if it's a number AND within board bounds (e.g., 0-23 for a 4x6 board)
                if (sscanf(buffer, "%d", &val) == 1)
                {
                    if (val >= 0 && val < TOTAL_CARDS)
                    {
                        if (matchedCards[val])
                        {
                            printf("That card is already matched. Pick another: ");
                            fflush(stdout);
                            continue;
                        }
                        pickCardCount++;
                        if (pickCardCount == 1)
                        {
                            firstPickIndex = val;
                            printf("Enter second card: ");
                            fflush(stdout);
                        }
                        else if (pickCardCount == 2)
                        {
                            secondPickIndex = val;
                            fflush(stdout);
                        }
                        if (firstPickIndex == secondPickIndex)
                        {
                            printf("You cannot pick the same card twice. Please pick again.\n");
                            pickCardCount = 1;
                            printf("Enter Again Second card: ");
                            fflush(stdout);
                            continue;
                        }
                        send(sock, buffer, strlen(buffer), 0);
                    }
                    else
                    {
                        printf("Error: Index %d is out of bounds (0-%d). ", val, TOTAL_CARDS - 1);
                        if (myTurn)
                            printPickPrompt(pickCardCount);
                        else
                            printf("Try again: ");
                        fflush(stdout);
                    }
                }
                else
                {
                    printf("Invalid input. ");
                    if (myTurn)
                        printPickPrompt(pickCardCount);
                    else
                        printf("Enter a number: ");
                    fflush(stdout);
                }
            }
        }
    }

    close(sock);
    return 0;
}