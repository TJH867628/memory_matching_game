#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "shared_state.h"
#include "scheduler.h"
#include "logger.h"
#include "player.h"
#include "score.h"

#define SERVER_PORT 8080
#define MAX_CLIENTS 5

int sharedMemoryID;
SharedGameState *gameState;
volatile bool serverRunning = true;
volatile sig_atomic_t shuttingDown = 0;

/* ---------------- TCP SETUP ---------------- */

int setupServerSocket() {
    int server_fd;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);
    return server_fd;
}

/* ---------------- CLEANUP ---------------- */

void cleanup(int sig) {
    serverRunning = 0;
    printf("\nShutting down server...\n");

    scores_save(gameState);

    sem_destroy(&gameState->turnSemaphore);
    sem_destroy(&gameState->turnCompleteSemaphore);
    sem_destroy(&gameState->logReadySemaphore);
    sem_destroy(&gameState->logItemsSemaphore);
    sem_destroy(&gameState->logSpacesSemaphore);

    pthread_mutex_destroy(&gameState->mutex);
    pthread_mutex_destroy(&gameState->logQueueMutex);

    shmdt(gameState);
    shmctl(sharedMemoryID, IPC_RMID, NULL);

    printf("Server shutdown complete.\n");
    exit(0);
}

void handleTCPClient(int sock, SharedGameState *gameState)
{
    char buffer[128];
    int playerID;
    int readyHandled = 0;

    /* Assign player */
    pthread_mutex_lock(&gameState->mutex);
    playerID = gameState->playerCount;
    gameState->players[playerID].playerID = playerID;
    gameState->players[playerID].connected = true;
    gameState->players[playerID].readyToStart = false;
    gameState->playerCount++;
    pthread_mutex_unlock(&gameState->mutex);

    char msg[LOG_MSG_LENGTH];
    snprintf(msg, LOG_MSG_LENGTH, "Player %d connected\n", playerID);
    pushLogEvent(gameState, LOG_PLAYER, msg);

    send(sock, "Type 1 to READY\n", 17, 0);

    /* ================= MAIN LOOP ================= */
    while (serverRunning) {

        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            printf("Player %d disconnected\n", playerID);
            break;
        }

        /* ========== READY ========= */
        if (!readyHandled &&
            (strcmp(buffer, "1\n") == 0 || strcmp(buffer, "1") == 0)) {

            pthread_mutex_lock(&gameState->mutex);
            gameState->players[playerID].readyToStart = true;
            readyHandled = 1;

            int readyCount = 0;
            for (int i = 0; i < gameState->playerCount; i++) {
                if (gameState->players[i].connected &&
                    gameState->players[i].readyToStart)
                    readyCount++;
            }
            pthread_mutex_unlock(&gameState->mutex);

            char logMsg[LOG_MSG_LENGTH];
            snprintf(logMsg, LOG_MSG_LENGTH,
                     "Player %d is READY\n", playerID);
            pushLogEvent(gameState, LOG_PLAYER, logMsg);

            send(sock, "READY received\n", 15, 0);

            /* Tell client to wait */
            if (!gameState->gameStarted) {
                send(sock, "Waiting for other players...\n", 30, 0);
            }

            /* Start game when >= 2 ready */
            if (!gameState->gameStarted && readyCount >= 2) {
                gameState->gameStarted = true;

                pushLogEvent(gameState, LOG_GAME,
                             "Game started\n");

                if (!gameState->gameStarted && readyCount >= 2) {
                    gameState->gameStarted = true;

                    pushLogEvent(gameState, LOG_GAME,
                                "Game started\n");

                    send(sock, "Game started\n", 13, 0);
                }
            }
            continue;
        }

        /* ===== BLOCK UNTIL GAME STARTS ===== */
        if (!gameState->gameStarted)
            continue;

        /* ===== GAME COMMAND ===== */
        if (strncmp(buffer, "FLIP", 4) == 0) {
            int card;
            sscanf(buffer, "FLIP %d", &card);

            PlayerAction action;
            action.type = ACTION_FLIP;
            action.cardIndex = card;
            action.playerID = playerID;

            applyAction(gameState, action);
            sem_post(&gameState->turnCompleteSemaphore);
        }
        else {
            send(sock, "Invalid command\n", 16, 0);
        }
    }

    close(sock);
}

/* ---------------- MAIN ---------------- */
int main() {

    key_t key = ftok("server.c", 65);
    sharedMemoryID = shmget(key, sizeof(SharedGameState), 0666 | IPC_CREAT);
    gameState = (SharedGameState*) shmat(sharedMemoryID, NULL, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameState->mutex, &attr);

    pthread_mutex_lock(&gameState->mutex);
    initGameState(gameState);
    pthread_mutex_unlock(&gameState->mutex);

    scores_init(gameState);
    scores_load(gameState);

    sem_init(&gameState->turnSemaphore, 1, 0);
    sem_init(&gameState->turnCompleteSemaphore, 1, 0);
    sem_init(&gameState->logReadySemaphore, 1, 0);
    sem_init(&gameState->logItemsSemaphore, 1, 0);
    sem_init(&gameState->logSpacesSemaphore, 1, LOG_QUEUE_SIZE);

    pthread_mutexattr_t logAttr;
    pthread_mutexattr_init(&logAttr);
    pthread_mutexattr_setpshared(&logAttr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameState->logQueueMutex, &logAttr);

    pthread_t loggerThread;
    pthread_create(&loggerThread, NULL, loggerThreadFunc, gameState);
    sem_wait(&gameState->logReadySemaphore);

    int serverSocket = setupServerSocket();
    signal(SIGINT, cleanup);

    printf("Waiting for players...\n");

    while (1) {
        struct sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);

        int clientSocket = accept(serverSocket,
                                  (struct sockaddr*)&clientAddr,
                                  &len);

        if (clientSocket < 0)
            continue;

        pid_t pid = fork();

        if (pid == 0) {
            signal(SIGINT, SIG_IGN);
            handleTCPClient(clientSocket, gameState);
            exit(0);
        }

        close(clientSocket);
    }
}
