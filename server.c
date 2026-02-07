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
#include "score.h"
#include "game.h"

#define SERVER_PORT 8080
#define MAX_CLIENTS 4

int sharedMemoryID;
SharedGameState *gameState;
volatile bool serverRunning = true;
volatile sig_atomic_t shuttingDown = 0;

pid_t childsPID[MAX_CLIENTS];
int childCount = 0;
pthread_t loggerThread;
pthread_t gameThread;
pthread_t schedulerThread;

int setupServerSocket()
{
    int server_fd;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("Listen failed");
        exit(1);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);
    return server_fd;
}

void cleanup(int sig)
{
    if (shuttingDown)
        return;
    shuttingDown = 1;
    serverRunning = false;

    if (!gameState)
    {
        printf("\nShutting down server...\n");
        fflush(stdout);
        exit(0);
    }

    pushLogEvent(gameState, LOG_SERVER, "Server shutting down.\n");

    sem_post(&gameState->turnCompleteSemaphore);
    sem_post(&gameState->turnSemaphore);
    sem_post(&gameState->flipDoneSemaphore);
    sem_post(&gameState->logReadySemaphore);
    sem_post(&gameState->logItemsSemaphore);
    sem_post(&gameState->logSpacesSemaphore);

    pthread_join(gameThread, NULL);
    pthread_join(loggerThread, NULL);
    pthread_join(schedulerThread, NULL);

    for (int i = 0; i < childCount; i++)
        kill(childsPID[i], SIGTERM);

    printf("Saving scores to scores.txt...\n");
    fflush(stdout);
    scores_save(gameState);

    sem_destroy(&gameState->turnSemaphore);
    sem_destroy(&gameState->turnCompleteSemaphore);
    sem_destroy(&gameState->logReadySemaphore);
    sem_destroy(&gameState->logItemsSemaphore);
    sem_destroy(&gameState->logSpacesSemaphore);

    pthread_mutex_destroy(&gameState->mutex);
    pthread_mutex_destroy(&gameState->logQueueMutex);

    shmdt(gameState);
    if (sharedMemoryID > 0)
        shmctl(sharedMemoryID, IPC_RMID, NULL);

    printf("Server shutdown complete.\n");
    exit(0);
}

void markPlayerDisconnected(SharedGameState *gameState, int playerID)
{
    pthread_mutex_lock(&gameState->mutex);
    char msg[LOG_MSG_LENGTH];

    gameState->players[playerID].connected = false;
    gameState->players[playerID].readyToStart = false;
    gameState->players[playerID].name[0] = '\0';

    gameState->playerCount--;

    snprintf(msg, LOG_MSG_LENGTH, "Player %d disconnected\n", playerID);
    pushLogEvent(gameState, LOG_PLAYER, msg);
    bool gameRunning = gameState->gameStarted;
    if (gameRunning)
    {
        gameState->gameStarted = false;
        gameState->currentTurn = -1;
        resetGameState(gameState);
    }
    gameState->boardNeedsBroadcast = true;
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (gameState->players[i].connected)
        {
            gameState->players[i].readyToStart = false;
            gameState->players[i].waitingNotified = false;
            gameState->players[i].flipsDone = 0;
            gameState->players[i].firstFlipIndex = -1;
            gameState->players[i].secondFlipIndex = -1;
        }
    }

    pthread_mutex_unlock(&gameState->mutex);

    char notify[512];
    char list[128];
    int pos = 0;
    pos += snprintf(list + pos, sizeof(list) - pos, "Connected players: ");
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (gameState->players[i].connected)
        {
            pos += snprintf(list + pos, sizeof(list) - pos, "%d ", i);
        }
    }
    char scoreMsg[256];
    scoreMsg[0] = '\0';
    pthread_mutex_lock(&gameState->mutex);
    strncat(scoreMsg, "Scoreboard:\n", sizeof(scoreMsg) - strlen(scoreMsg) - 1);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (gameState->players[i].connected)
        {
            const char *name = gameState->players[i].name[0] ? gameState->players[i].name : "Unknown";
            char line[64];
            snprintf(line, sizeof(line), "%s (ID %d): %d\n", name, i, gameState->players[i].score);
            strncat(scoreMsg, line, sizeof(scoreMsg) - strlen(scoreMsg) - 1);
        }
    }
    pthread_mutex_unlock(&gameState->mutex);

    snprintf(notify, sizeof(notify),"GAME_STOPPED\nPlayer %d left the game.\n%s\n%sPlease type 1 to READY.\n<<END>>\n",playerID,list,scoreMsg);

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (gameState->players[i].connected)
        {
            send(gameState->players[i].socket,
                 notify,
                 strlen(notify), 0);
        }
    }

    printf("Game stopped. Player %d left. Waiting for players...\n", playerID);

    sem_post(&gameState->turnCompleteSemaphore);
    sem_post(&gameState->flipDoneSemaphore);

    int connectedCount = 0;
    int readyCount = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (gameState->players[i].connected)
        {
            connectedCount++;
            if (gameState->players[i].readyToStart)
            {
                readyCount++;
            }
        }
    }

    if (!gameState->gameStarted && connectedCount >= MIN_PLAYERS && readyCount == connectedCount)
    {
        gameState->gameStarted = true;

        pushLogEvent(gameState, LOG_PLAYER, "All remaining players ready. Game started.\n");
    }
}

void pushClientCommand(SharedGameState *gameState, int playerID, char *buffer)
{
    pthread_mutex_lock(&gameState->mutex);
    buffer[strcspn(buffer, "\r\n")] = 0;
    bool gameStarted = gameState->gameStarted;
    pthread_mutex_unlock(&gameState->mutex);

    int connectedCount = 0;
    int readyCount = 0;

    if (!gameStarted && strcmp(buffer, "1") == 0)
    {
        if (!gameState->players[playerID].readyToStart)
        {
            pthread_mutex_lock(&gameState->mutex);
            gameState->players[playerID].readyToStart = true;
            gameState->players[playerID].waitingNotified = false;
            pthread_mutex_unlock(&gameState->mutex);

            char msg[LOG_MSG_LENGTH];
            snprintf(msg, LOG_MSG_LENGTH,"Player %d is READY\n",playerID);
            pushLogEvent(gameState, LOG_PLAYER, msg);
        }

        if (!gameState->gameStarted)
        {

            pthread_mutex_lock(&gameState->mutex);

            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (gameState->players[i].connected)
                {
                    connectedCount++;
                    if (gameState->players[i].readyToStart)
                    {
                        readyCount++;
                    }
                }
            }
            pthread_mutex_unlock(&gameState->mutex);
        }

        if (connectedCount >= MIN_PLAYERS && readyCount == connectedCount)
        {
            pthread_mutex_lock(&gameState->mutex);
            gameState->gameStarted = true;
            pthread_mutex_unlock(&gameState->mutex);
        }
    }

    if (strncmp(buffer, "NAME ", 5) == 0)
    {
        char name[PLAYER_NAME_LENGTH];
        if (sscanf(buffer + 5, "%31s", name) == 1)
        {
            pthread_mutex_lock(&gameState->mutex);
            bool nameTaken = false;
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (gameState->players[i].connected &&
                    gameState->players[i].name[0] != '\0' &&
                    strncmp(gameState->players[i].name, name, PLAYER_NAME_LENGTH) == 0)
                {
                    nameTaken = true;
                    break;
                }
            }
            pthread_mutex_unlock(&gameState->mutex);

            if (nameTaken)
            {
                char logMsg[LOG_MSG_LENGTH];
                snprintf(logMsg, LOG_MSG_LENGTH,"Player %d tried duplicate name: %s\n",playerID,name);
                pushLogEvent(gameState, LOG_PLAYER, logMsg);
                const char *errMsg = "NAME_TAKEN\n<<END>>\n";
                send(gameState->players[playerID].socket, errMsg, strlen(errMsg), 0);
                return;
            }

            pthread_mutex_lock(&gameState->mutex);
            strncpy(gameState->players[playerID].name, name, PLAYER_NAME_LENGTH - 1);
            gameState->players[playerID].name[PLAYER_NAME_LENGTH - 1] = '\0';
            pthread_mutex_unlock(&gameState->mutex);

            int savedScore = scores_get_wins(gameState, name);
            pthread_mutex_lock(&gameState->mutex);
            gameState->players[playerID].score = savedScore;
            gameState->players[playerID].roundScore = 0;
            pthread_mutex_unlock(&gameState->mutex);
            char msg[128];
            snprintf(msg, sizeof(msg), "WELCOME %s (Saved Score: %d)\n<<END>>\n", name, savedScore);
            send(gameState->players[playerID].socket, msg, strlen(msg), 0);

            char logMsg[LOG_MSG_LENGTH];
            snprintf(logMsg, LOG_MSG_LENGTH,"Player %d registered name: %s (Score %d)\n",playerID,name,savedScore);
            pushLogEvent(gameState, LOG_PLAYER, logMsg);
        }
        return;
    }

    int cardIndex;

    if (gameStarted && sscanf(buffer, "%d", &cardIndex) == 1)
    {
        pthread_mutex_lock(&gameState->mutex);
        int maxCards = gameState->boardRows * gameState->boardCols;
        bool gameStarted = gameState->gameStarted;
        int currentTurn = gameState->currentTurn;
        int socket = gameState->players[playerID].socket;
        Card *cards = gameState->cards;
        pthread_mutex_unlock(&gameState->mutex);
        if (cardIndex < 0 || cardIndex >= maxCards)
        {
            const char *msg = "Invalid card index!\n<<END>>\n";
            send(socket, msg, strlen(msg), 0);
            return;
        }
        if (currentTurn != playerID)
        {
            const char *msg = "It's not your turn!\n<<END>>\n";
            send(socket, msg, strlen(msg), 0);
            return;
        }
        pthread_mutex_lock(&gameState->mutex);

        if (gameState->players[playerID].flipsDone == 1 &&
            gameState->players[playerID].firstFlipIndex == cardIndex)
        {
            pthread_mutex_unlock(&gameState->mutex);
            send(socket, "You cannot pick the same card twice!\n<<END>>\n", 46, 0);
            return;
        }

        if (cards[cardIndex].isMatched || cards[cardIndex].isFlipped)
        {
            pthread_mutex_unlock(&gameState->mutex);
            const char *msg = "Card already matched or flipped!\n<<END>>\n";
            send(socket, msg, strlen(msg), 0);
            return;
        }
        pthread_mutex_unlock(&gameState->mutex);

        pthread_mutex_lock(&gameState->mutex);
        gameState->players[playerID].pendingAction = true;
        pthread_mutex_unlock(&gameState->mutex);

        char msg[LOG_MSG_LENGTH];
        if (gameStarted)
        {
            pthread_mutex_lock(&gameState->mutex);

            if (gameState->players[playerID].flipsDone == 0)
            {
                gameState->players[playerID].firstFlipIndex = cardIndex;
                gameState->players[playerID].flipsDone = 1;
                printf("Player %d flipped first card. Internal count: %d\n",
                       playerID, gameState->players[playerID].flipsDone);
            }
            else if (gameState->players[playerID].flipsDone == 1)
            {
                gameState->players[playerID].secondFlipIndex = cardIndex;
                gameState->players[playerID].flipsDone = 2;
            }
            pthread_mutex_unlock(&gameState->mutex);
            sem_post(&gameState->flipDoneSemaphore);
        }

        printf("Player flipped done: %d\n", gameState->players[playerID].flipsDone);

        snprintf(msg, LOG_MSG_LENGTH,"Player %d flipped card %d\n",playerID,cardIndex);

        pushLogEvent(gameState, LOG_PLAYER, msg);
    }
}

void handleTCPClient(int sock, SharedGameState *gameState, int myPlayerID)
{
    char buffer[128];
    char msg[128];
    snprintf(msg, sizeof(msg), "Successful connect to Server\nPLAYER ID %d\n<<END>>\n", myPlayerID);
    send(sock, msg, strlen(msg), 0);

    bool sentWaiting = false;

    while (1)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        int rv = select(sock + 1, &readfds, NULL, NULL, &tv);

        if (rv > 0 && FD_ISSET(sock, &readfds))
        {
            memset(buffer, 0, sizeof(buffer));
            int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);

            if (bytes <= 0)
            {
                markPlayerDisconnected(gameState, myPlayerID);
                break;
            }

            buffer[bytes] = '\0';
            char *saveptr = NULL;
            char *line = strtok_r(buffer, "\n", &saveptr);
            while (line)
            {
                //Remove /r
                line[strcspn(line, "\r")] = 0;
                if (line[0] != '\0')
                {
                    pushClientCommand(gameState, myPlayerID, line);
                }
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }

        pthread_mutex_lock(&gameState->mutex);
        bool iAmReady = gameState->players[myPlayerID].readyToStart;
        bool started = gameState->gameStarted;
        int matched = gameState->matchedPaires;
        pthread_mutex_unlock(&gameState->mutex);

        if (iAmReady && !started && !gameState->players[myPlayerID].waitingNotified)
        {
            char scoreMsg[256];
            char waitMsg[512];
            scoreMsg[0] = '\0';
            pthread_mutex_lock(&gameState->mutex);
            strncat(scoreMsg, "Scoreboard:\n", sizeof(scoreMsg) - strlen(scoreMsg) - 1);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (gameState->players[i].connected)
                {
                    const char *name = gameState->players[i].name[0] ? gameState->players[i].name : "Unknown";
                    char line[128];
                    snprintf(line, sizeof(line), "%s (ID %d): Total Score %d | Score This Round %d\n",name,i,gameState->players[i].score,gameState->players[i].roundScore);
                    strncat(scoreMsg, line, sizeof(scoreMsg) - strlen(scoreMsg) - 1);
                }
            }
            pthread_mutex_unlock(&gameState->mutex);
            snprintf(waitMsg, sizeof(waitMsg),"Waiting for other players to connect/ready...\n%s<<END>>\n",scoreMsg);
            send(sock, waitMsg, strlen(waitMsg), 0);
            gameState->players[myPlayerID].waitingNotified = true;
        }

        if (started)
        {
            sentWaiting = true;
        }
    }

    close(sock);
}

int main()
{
    key_t key = ftok("server.c", 65);
    int existingID = shmget(key, 0, 0666);
    //Remove existing shared memory if have
    if (existingID != -1)
    {
        shmctl(existingID, IPC_RMID, NULL);
    }

    sharedMemoryID = shmget(key, sizeof(SharedGameState), 0666 | IPC_CREAT | IPC_EXCL);
    //Check if shmget failed
    if (sharedMemoryID == -1)
    {
        perror("shmget failed");
        exit(1);
    }

    //Attach shared memory
    gameState = (SharedGameState *)shmat(sharedMemoryID, NULL, 0);
    if (gameState == (void *)-1)
    {
        perror("shmat failed");
        exit(1);
    }

    memset(gameState, 0, sizeof(SharedGameState));

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameState->mutex, &attr);

    pthread_mutex_lock(&gameState->mutex);
    initGameState(gameState);
    pthread_mutex_unlock(&gameState->mutex);

    scores_init(gameState);
    scores_load(gameState);
    scores_print(gameState);


    sem_init(&gameState->turnSemaphore, 1, 0);
    sem_init(&gameState->turnCompleteSemaphore, 1, 0);
    sem_init(&gameState->logReadySemaphore, 1, 0);
    sem_init(&gameState->logItemsSemaphore, 1, 0);
    sem_init(&gameState->logSpacesSemaphore, 1, LOG_QUEUE_SIZE);
    sem_init(&gameState->flipDoneSemaphore, 1, 0);

    pthread_mutexattr_t logAttr;
    pthread_mutexattr_init(&logAttr);
    pthread_mutexattr_setpshared(&logAttr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&gameState->logQueueMutex, &logAttr);

    pthread_create(&loggerThread, NULL, loggerLoopThread, gameState);
    sem_wait(&gameState->logReadySemaphore);
    pushLogEvent(gameState, LOG_SERVER, "Server started.\n");   

    pthread_create(&gameThread, NULL, gameLoopThread, gameState);
    pthread_create(&schedulerThread, NULL, schedulerLoopThread, gameState);

    int serverSocket = setupServerSocket();
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGHUP, cleanup);

    printf("Waiting for players...\n");

    while (1)
    {
        struct sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);

        int clientSocket = accept(serverSocket,(struct sockaddr *)&clientAddr, &len);

        pthread_mutex_lock(&gameState->mutex);
        if (gameState->gameStarted)
        {
            pthread_mutex_unlock(&gameState->mutex);
            send(clientSocket, "Game already started\n", 21, 0);
            close(clientSocket);
            continue;
        }
        pthread_mutex_unlock(&gameState->mutex);
        if (clientSocket < 0)
            continue;

        /* assign player section befor fork() */
        int slot = -1;

        pthread_mutex_lock(&gameState->mutex);
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (!gameState->players[i].connected)
            {
                slot = i;
                gameState->players[i].playerID = i;
                gameState->players[i].connected = true;
                gameState->players[i].readyToStart = false;
                gameState->players[i].pid = -1;
                gameState->players[i].socket = clientSocket;
                gameState->playerCount++;
                break;
            }
        }
        pthread_mutex_unlock(&gameState->mutex);

        if (slot == -1)
        {
            const char *msg = "Server full. Try later.\n";
            send(clientSocket, msg, strlen(msg), 0);
            close(clientSocket);
            continue;
        }

        pid_t pid = fork();

        if (pid == 0)
        {
            /* child section */
            close(serverSocket);
            signal(SIGINT, SIG_IGN);

            handleTCPClient(clientSocket, gameState, slot);
            exit(0);
        }
        else if (pid > 0)
        {
            /* parent section */
            childsPID[childCount++] = pid;

            pthread_mutex_lock(&gameState->mutex);
            gameState->players[slot].pid = pid;

            char msg[LOG_MSG_LENGTH];
            snprintf(msg, LOG_MSG_LENGTH,"New connection assigned to Player %d\n",slot);
            pushLogEvent(gameState, LOG_PLAYER, msg);

            pthread_mutex_unlock(&gameState->mutex);
        }
    }
}