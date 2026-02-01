#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
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
#define MAX_CLIENTS 4

int sharedMemoryID;
SharedGameState *gameState;
volatile bool serverRunning = true;
volatile sig_atomic_t shuttingDown = 0;

pid_t childsPID[MAX_CLIENTS];
int childCount = 0;

/* ---------------- TCP SETUP ---------------- */

int setupServerSocket(){
    int server_fd;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0){
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0){
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0){
        perror("Listen failed");
        exit(1);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);
    return server_fd;
}

/* ---------------- CLEANUP ---------------- */

void cleanup(int sig){
    serverRunning = 0;
    printf("\nShutting down server...\n");

    for(int i = 0; i < childCount ; i++){
        kill(childsPID[i],SIGTERM);
    }

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

void markPlayerDisconnected(SharedGameState *gameState,int playerID){
    pthread_mutex_lock(&gameState->mutex);

    gameState->players[playerID].connected = false;
    gameState->players[playerID].readyToStart = false;

    gameState->playerCount--;

    pthread_mutex_unlock(&gameState->mutex);
    char msg[LOG_MSG_LENGTH];
    snprintf(msg, LOG_MSG_LENGTH,"Player %d disconnected\n", playerID);

    pushLogEvent(gameState, LOG_PLAYER, msg);

}

void pushClientCommand(SharedGameState *gameState, int playerID, char *buffer)
{
    pthread_mutex_lock(&gameState->mutex);

    buffer[strcspn(buffer, "\n")] = 0;

    if (strcmp(buffer, "1") == 0) {
        if (!gameState->players[playerID].readyToStart) {
            gameState->players[playerID].readyToStart = true;

            char msg[LOG_MSG_LENGTH];
            snprintf(msg, LOG_MSG_LENGTH, "Player %d is READY\n", playerID);
            pushLogEvent(gameState, LOG_PLAYER, msg);
        }

        if (!gameState->gameStarted) {
            int connectedCount = 0;
            int readyCount = 0;

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (gameState->players[i].connected) {
                    connectedCount++;
                    if (gameState->players[i].readyToStart) {
                        readyCount++;
                    }
                }
            }
            
            //change to >= 3 later
            if (connectedCount >= 2 && readyCount == connectedCount) {
                gameState->gameStarted = true;
                pushLogEvent(gameState, LOG_GAME, "Game Started\n");
            }
        }
    }

    pthread_mutex_unlock(&gameState->mutex);
}

void handleTCPClient(int sock, SharedGameState *gameState, int myPlayerID)
{
    char buffer[128];

    send(sock, "Successful connect to Server\n", 30, 0);

    bool sentWaiting = false;
    bool sentGameStart = false;

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int rv = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (rv > 0 && FD_ISSET(sock, &readfds)) {
            memset(buffer, 0, sizeof(buffer));
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

            if (bytes <= 0) {
                markPlayerDisconnected(gameState, myPlayerID);
                break;
            }

            pushClientCommand(gameState, myPlayerID, buffer);
        }

        pthread_mutex_lock(&gameState->mutex);
        bool iAmReady = gameState->players[myPlayerID].readyToStart;
        bool started = gameState->gameStarted;
        pthread_mutex_unlock(&gameState->mutex);

        if (iAmReady && !started && !sentWaiting) {
            send(sock,
                "Waiting for other players to connect/ready...\n",
                49, 0);
            sentWaiting = true;
        }

        if (started) {
            sentWaiting = true;
        }

        if (started && !sentGameStart) {
            const char *msg = "Game Started\n";
            send(sock, msg, strlen(msg), 0);
            sentGameStart = true;
        }
    }

    close(sock);
}

/* ---------------- MAIN ---------------- */
int main()
{
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

        /* assign player section befor fork() */
        int slot = -1;

        pthread_mutex_lock(&gameState->mutex);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!gameState->players[i].connected) {
                slot = i;
                gameState->players[i].playerID = i;
                gameState->players[i].connected = true;
                gameState->players[i].readyToStart = false;
                gameState->players[i].pid = -1;
                gameState->playerCount++;
                break;
            }
        }
        pthread_mutex_unlock(&gameState->mutex);

        if (slot == -1) {
            const char *msg = "Server full. Try later.\n";
            send(clientSocket, msg, strlen(msg), 0);
            close(clientSocket);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0) {
            /* child section */
            close(serverSocket);
            signal(SIGINT, SIG_IGN);

            handleTCPClient(clientSocket, gameState, slot);
            exit(0);
        }
        else if (pid > 0) {
            /* parent section */
            childsPID[childCount++] = pid;

            pthread_mutex_lock(&gameState->mutex);
            gameState->players[slot].pid = pid;

            char msg[LOG_MSG_LENGTH];
            snprintf(msg, LOG_MSG_LENGTH,
                     "New connection assigned to Player %d\n", slot);
            pushLogEvent(gameState, LOG_PLAYER, msg);

            pthread_mutex_unlock(&gameState->mutex);

            close(clientSocket);
        }
    }
}